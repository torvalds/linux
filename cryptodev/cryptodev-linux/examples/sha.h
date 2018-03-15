#ifndef SHA_H
# define SHA_H

#include <stdint.h>

struct cryptodev_ctx {
	int cfd;
	struct session_op sess;
	uint16_t alignmask;
};

int sha_ctx_init(struct cryptodev_ctx* ctx, int cfd, const uint8_t *key, unsigned int key_size);
void sha_ctx_deinit();
int sha_hash(struct cryptodev_ctx* ctx, const void* text, size_t size, void* digest);

#endif
