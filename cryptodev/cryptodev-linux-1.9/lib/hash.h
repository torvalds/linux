#ifndef HASH_H
# define HASH_H

#include <stdint.h>

struct cryptodev_ctx {
	int cfd;
	struct session_op sess;
	uint16_t alignmask;
};

int hash_ctx_init(struct cryptodev_ctx* ctx, int hash, int cfd);
void hash_ctx_deinit(struct cryptodev_ctx* ctx);
int hash(struct cryptodev_ctx* ctx, const void* text, size_t size, void* digest);
int hash_test(int algo, void (*user_hash)(void* text, int size, void* res));

int aead_test(int cipher, int mac, void* ukey, int ukey_size,
		void* user_ctx, void (*user_combo)(void* user_ctx, void* plaintext, void* ciphertext, int size, void* res));

#endif
