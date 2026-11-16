#include <Arduino.h>
#include "mbedtls/aes.h"
#include "mbedtls/md.h"

// Krypteringsnycklar (128-bit AES + 256-bit HMAC)
const uint8_t AES_KEY[16] = { 0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6, 0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C };
const uint8_t HMAC_KEY[32] = { 0x60, 0x3D, 0xEB, 0x10, 0x15, 0xCA, 0x71, 0xBE, 0x2B, 0x73, 0xAE, 0xF0, 0x85, 0x7D, 0x77, 0x81,
0x1F, 0x35, 0x2C, 0x07, 0x3B, 0x61, 0x08, 0xD7, 0x2D, 0x98, 0x10, 0xA3, 0x09, 0x14, 0xDF, 0xF4 };

// Paketstruktur (24 bytes) som skickas över 868 MHz LoRa
struct __attribute__((packed)) SecurePacket {
uint32_t counter;
uint8_t command;
uint8_t padding[3];
uint8_t hmac[16];
};

uint32_t txCounter = 1;
uint32_t lastValidRxCounter = 0;
SecurePacket interceptedPacket; // Sparar ett paket för att simulera replay-attack

void computeHMAC(uint32_t counter, uint8_t command, uint8_t* outputHmac16) {
uint8_t dataToSign[5];
memcpy(dataToSign, &counter, 4);
dataToSign[4] = command;

uint8_t fullHmac[32];
mbedtls_md_context_t ctx;
mbedtls_md_init(&ctx);
mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
mbedtls_md_hmac_starts(&ctx, HMAC_KEY, sizeof(HMAC_KEY));
mbedtls_md_hmac_update(&ctx, dataToSign, sizeof(dataToSign));
mbedtls_md_hmac_finish(&ctx, fullHmac);
mbedtls_md_free(&ctx);

memcpy(outputHmac16, fullHmac, 16);
}

void cryptPayload(uint32_t counter, uint8_t* data, size_t len) {
mbedtls_aes_context aes;
mbedtls_aes_init(&aes);
mbedtls_aes_setkey_enc(&aes, AES_KEY, 128);

uint8_t nonce_counter[16] = {0};
memcpy(nonce_counter, &counter, 4);

size_t nc_off = 0;
uint8_t stream_block[16] = {0};
mbedtls_aes_crypt_ctr(&aes, len, &nc_off, nonce_counter, stream_block, data, data);
mbedtls_aes_free(&aes);
}

SecurePacket buildSecurePacket(uint8_t cmd) {
SecurePacket pkt;
pkt.counter = txCounter++;
computeHMAC(pkt.counter, cmd, pkt.hmac);

uint8_t payload[4] = { cmd, 0xAA, 0xBB, 0xCC };
cryptPayload(pkt.counter, payload, 4);

pkt.command = payload[0];
memcpy(pkt.padding, &payload[1], 3);
return pkt;
}

void receiveAndVerify(SecurePacket pkt, const char* label) {
Serial.printf("\n--- [%s] Tar emot LoRa-paket (Counter: %u) ---\n", label, pkt.counter);
Serial.printf("Rå krypterad payload (Hex): %02X %02X %02X %02X\n", pkt.command, pkt.padding[0], pkt.padding[1], pkt.padding[2]);

// 1. Replay-skydd
if (pkt.counter <= lastValidRxCounter) {
Serial.println("[BLOCKERAD] Replay-attack upptäckt! Räknaren är redan förbrukad.");
return;
}

// 2. Dekryptera med AES-128-CTR
uint8_t payload[4];
payload[0] = pkt.command;
memcpy(&payload[1], pkt.padding, 3);
cryptPayload(pkt.counter, payload, 4);
uint8_t decryptedCmd = payload[0];

// 3. Verifiera HMAC-SHA256
uint8_t expectedHmac[16];
computeHMAC(pkt.counter, decryptedCmd, expectedHmac);

if (memcmp(pkt.hmac, expectedHmac, 16) != 0) {
Serial.println("[BLOCKERAD] Ogiltig HMAC-signatur! Paketet har manipulerats.");
return;
}

// 4. Godkänt!
lastValidRxCounter = pkt.counter;
Serial.printf("[GODKÄND] Autentiserat kommando dekrypterat: 0x%02X -> Kör payload!\n", decryptedCmd);
}

void setup() {
Serial.begin(115200);
delay(1000);
Serial.println("=== LoRa C2 Säkerhetsarkitektur (AES-128 + HMAC-SHA256) ===");

// Test 1: Skicka legitimt kommando (0x01 = Aktivera Rogue AP)
SecurePacket pkt1 = buildSecurePacket(0x01);
interceptedPacket = pkt1; // En angripare snappar upp paketet i luften
receiveAndVerify(pkt1, "Legitim Sändning 1");

delay(1500);

// Test 2: Angripare spelar upp exakt samma radiopaket igen (Replay Attack)
receiveAndVerify(interceptedPacket, "Simulerad Replay-Attack");

delay(1500);

// Test 3: Angripare manipulerar en byte i ett nytt paket (MITM / Bit-flipping)
SecurePacket pkt2 = buildSecurePacket(0x02);
pkt2.command ^= 0xFF; // Förstör krypterad byte
receiveAndVerify(pkt2, "Simulerad Manipulering");

delay(1500);

// Test 4: Legitimt kommando för att stänga av (0x00 = Stealth Mode)
SecurePacket pkt3 = buildSecurePacket(0x00);
receiveAndVerify(pkt3, "Legitim Sändning 2");
}

void loop() {
// Allt körs i setup() för tydlig demonstration
}
