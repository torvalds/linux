#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <crypto/cryptodev.h>
#include <openssl/aes.h>
#include <openssl/engine.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include "hash.h"
#include "threshold.h"

void sha_hash(void* text, int size, void* digest)
{
SHA_CTX ctx;

	SHA_Init(&ctx);

	SHA_Update(&ctx, text, size);

	SHA_Final(digest, &ctx);
}

void aes_sha_combo(void* ctx, void* plaintext, void* ciphertext, int size, void* tag)
{
uint8_t iv[16];
AES_KEY* key = ctx;
HMAC_CTX hctx;
unsigned int rlen = 20;

	HMAC_CTX_init(&hctx);
	HMAC_Init_ex(&hctx, iv, 16, EVP_sha1(), NULL);

	HMAC_Update(&hctx, plaintext, size);

	HMAC_Final(&hctx, tag, &rlen);
	HMAC_CTX_cleanup(&hctx);

	AES_cbc_encrypt(plaintext, ciphertext, size, key, iv, 1);
}

int get_sha1_threshold()
{
	return hash_test(CRYPTO_SHA1, sha_hash);
}

int get_aes_sha1_threshold()
{
AES_KEY key;
uint8_t ukey[16];

	ENGINE_load_builtin_engines();
        ENGINE_register_all_complete();

	memset(ukey, 0xaf, sizeof(ukey));
	AES_set_encrypt_key(ukey, 16*8, &key);

	return aead_test(CRYPTO_AES_CBC, CRYPTO_SHA1, ukey, 16, &key, aes_sha_combo);
}

