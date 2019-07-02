/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Fallback for sync aes(ctr) in contexts where kernel mode NEON
 * is not allowed
 *
 * Copyright (C) 2017 Linaro Ltd <ard.biesheuvel@linaro.org>
 */

#include <crypto/aes.h>
#include <crypto/internal/skcipher.h>

asmlinkage void __aes_arm64_encrypt(u32 *rk, u8 *out, const u8 *in, int rounds);

static inline int aes_ctr_encrypt_fallback(struct crypto_aes_ctx *ctx,
					   struct skcipher_request *req)
{
	struct skcipher_walk walk;
	u8 buf[AES_BLOCK_SIZE];
	int err;

	err = skcipher_walk_virt(&walk, req, true);

	while (walk.nbytes > 0) {
		u8 *dst = walk.dst.virt.addr;
		u8 *src = walk.src.virt.addr;
		int nbytes = walk.nbytes;
		int tail = 0;

		if (nbytes < walk.total) {
			nbytes = round_down(nbytes, AES_BLOCK_SIZE);
			tail = walk.nbytes % AES_BLOCK_SIZE;
		}

		do {
			int bsize = min(nbytes, AES_BLOCK_SIZE);

			__aes_arm64_encrypt(ctx->key_enc, buf, walk.iv,
					    6 + ctx->key_length / 4);
			crypto_xor_cpy(dst, src, buf, bsize);
			crypto_inc(walk.iv, AES_BLOCK_SIZE);

			dst += AES_BLOCK_SIZE;
			src += AES_BLOCK_SIZE;
			nbytes -= AES_BLOCK_SIZE;
		} while (nbytes > 0);

		err = skcipher_walk_done(&walk, tail);
	}
	return err;
}
