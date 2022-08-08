/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/types.h>
#include <crypto/aes.h>

struct aes_key {
	u8 key[AES_MAX_KEYLENGTH];
	int rounds;
};

extern struct shash_alg p8_ghash_alg;
extern struct crypto_alg p8_aes_alg;
extern struct skcipher_alg p8_aes_cbc_alg;
extern struct skcipher_alg p8_aes_ctr_alg;
extern struct skcipher_alg p8_aes_xts_alg;

int aes_p8_set_encrypt_key(const u8 *userKey, const int bits,
			   struct aes_key *key);
int aes_p8_set_decrypt_key(const u8 *userKey, const int bits,
			   struct aes_key *key);
void aes_p8_encrypt(const u8 *in, u8 *out, const struct aes_key *key);
void aes_p8_decrypt(const u8 *in, u8 *out, const struct aes_key *key);
void aes_p8_cbc_encrypt(const u8 *in, u8 *out, size_t len,
			const struct aes_key *key, u8 *iv, const int enc);
void aes_p8_ctr32_encrypt_blocks(const u8 *in, u8 *out,
				 size_t len, const struct aes_key *key,
				 const u8 *iv);
void aes_p8_xts_encrypt(const u8 *in, u8 *out, size_t len,
			const struct aes_key *key1, const struct aes_key *key2, u8 *iv);
void aes_p8_xts_decrypt(const u8 *in, u8 *out, size_t len,
			const struct aes_key *key1, const struct aes_key *key2, u8 *iv);
