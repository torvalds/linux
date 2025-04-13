// SPDX-License-Identifier: GPL-2.0
/*
 * OpenSSL/Cryptogams accelerated Poly1305 transform for MIPS
 *
 * Copyright (C) 2019 Linaro Ltd. <ard.biesheuvel@linaro.org>
 */

#include <crypto/poly1305.h>
#include <linux/cpufeature.h>
#include <linux/module.h>
#include <linux/unaligned.h>

asmlinkage void poly1305_init_mips(void *state, const u8 *key);
asmlinkage void poly1305_blocks_mips(void *state, const u8 *src, u32 len, u32 hibit);
asmlinkage void poly1305_emit_mips(void *state, u8 *digest, const u32 *nonce);

void poly1305_init_arch(struct poly1305_desc_ctx *dctx, const u8 key[POLY1305_KEY_SIZE])
{
	poly1305_init_mips(&dctx->h, key);
	dctx->s[0] = get_unaligned_le32(key + 16);
	dctx->s[1] = get_unaligned_le32(key + 20);
	dctx->s[2] = get_unaligned_le32(key + 24);
	dctx->s[3] = get_unaligned_le32(key + 28);
	dctx->buflen = 0;
}
EXPORT_SYMBOL(poly1305_init_arch);

void poly1305_update_arch(struct poly1305_desc_ctx *dctx, const u8 *src,
			  unsigned int nbytes)
{
	if (unlikely(dctx->buflen)) {
		u32 bytes = min(nbytes, POLY1305_BLOCK_SIZE - dctx->buflen);

		memcpy(dctx->buf + dctx->buflen, src, bytes);
		src += bytes;
		nbytes -= bytes;
		dctx->buflen += bytes;

		if (dctx->buflen == POLY1305_BLOCK_SIZE) {
			poly1305_blocks_mips(&dctx->h, dctx->buf,
					     POLY1305_BLOCK_SIZE, 1);
			dctx->buflen = 0;
		}
	}

	if (likely(nbytes >= POLY1305_BLOCK_SIZE)) {
		unsigned int len = round_down(nbytes, POLY1305_BLOCK_SIZE);

		poly1305_blocks_mips(&dctx->h, src, len, 1);
		src += len;
		nbytes %= POLY1305_BLOCK_SIZE;
	}

	if (unlikely(nbytes)) {
		dctx->buflen = nbytes;
		memcpy(dctx->buf, src, nbytes);
	}
}
EXPORT_SYMBOL(poly1305_update_arch);

void poly1305_final_arch(struct poly1305_desc_ctx *dctx, u8 *dst)
{
	if (unlikely(dctx->buflen)) {
		dctx->buf[dctx->buflen++] = 1;
		memset(dctx->buf + dctx->buflen, 0,
		       POLY1305_BLOCK_SIZE - dctx->buflen);
		poly1305_blocks_mips(&dctx->h, dctx->buf, POLY1305_BLOCK_SIZE, 0);
	}

	poly1305_emit_mips(&dctx->h, dst, dctx->s);
	*dctx = (struct poly1305_desc_ctx){};
}
EXPORT_SYMBOL(poly1305_final_arch);

bool poly1305_is_arch_optimized(void)
{
	return true;
}
EXPORT_SYMBOL(poly1305_is_arch_optimized);

MODULE_DESCRIPTION("Poly1305 transform (MIPS accelerated");
MODULE_LICENSE("GPL v2");
