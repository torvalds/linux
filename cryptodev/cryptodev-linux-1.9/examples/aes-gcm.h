#ifndef AES_H
# define AES_H

#include <stdint.h>

struct cryptodev_ctx {
	int cfd;
	struct session_op sess;
	uint16_t alignmask;
};

#define	AES_BLOCK_SIZE	16

int aes_gcm_ctx_init(struct cryptodev_ctx* ctx, int cfd, const uint8_t *key, unsigned int key_size);
void aes_gcm_ctx_deinit();

/* Note that encryption assumes that ciphertext has enough size
 * for the tag to be appended. In decryption the tag is assumed
 * to be the last bytes of ciphertext.
 */
int aes_gcm_encrypt(struct cryptodev_ctx* ctx, const void* iv, 
	const void* auth, size_t auth_size,
	const void* plaintext, void* ciphertext, size_t size);
int aes_gcm_decrypt(struct cryptodev_ctx* ctx, const void* iv, 
	const void* auth, size_t auth_size,
	const void* ciphertext, void* plaintext, size_t size);

#endif
