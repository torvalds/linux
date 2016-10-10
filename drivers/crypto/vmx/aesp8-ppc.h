#include <linux/types.h>
#include <crypto/aes.h>

#define AES_BLOCK_MASK  (~(AES_BLOCK_SIZE-1))

struct aes_key {
	u8 key[AES_MAX_KEYLENGTH];
	int rounds;
};

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
