// SPDX-License-Identifier: GPL-2.0
/*
 * OpenSSL/Cryptogams accelerated Poly1305 transform for MIPS
 *
 * Copyright (C) 2019 Linaro Ltd. <ard.biesheuvel@linaro.org>
 */

#include <asm/unaligned.h>
#include <crypto/algapi.h>
#include <crypto/internal/hash.h>
#include <crypto/internal/poly1305.h>
#include <linux/cpufeature.h>
#include <linux/crypto.h>
#include <linux/module.h>

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

static int mips_poly1305_init(struct shash_desc *desc)
{
	struct poly1305_desc_ctx *dctx = shash_desc_ctx(desc);

	dctx->buflen = 0;
	dctx->rset = 0;
	dctx->sset = false;

	return 0;
}

static void mips_poly1305_blocks(struct poly1305_desc_ctx *dctx, const u8 *src,
				 u32 len, u32 hibit)
{
	if (unlikely(!dctx->sset)) {
		if (!dctx->rset) {
			poly1305_init_mips(&dctx->h, src);
			src += POLY1305_BLOCK_SIZE;
			len -= POLY1305_BLOCK_SIZE;
			dctx->rset = 1;
		}
		if (len >= POLY1305_BLOCK_SIZE) {
			dctx->s[0] = get_unaligned_le32(src +  0);
			dctx->s[1] = get_unaligned_le32(src +  4);
			dctx->s[2] = get_unaligned_le32(src +  8);
			dctx->s[3] = get_unaligned_le32(src + 12);
			src += POLY1305_BLOCK_SIZE;
			len -= POLY1305_BLOCK_SIZE;
			dctx->sset = true;
		}
		if (len < POLY1305_BLOCK_SIZE)
			return;
	}

	len &= ~(POLY1305_BLOCK_SIZE - 1);

	poly1305_blocks_mips(&dctx->h, src, len, hibit);
}

static int mips_poly1305_update(struct shash_desc *desc, const u8 *src,
				unsigned int len)
{
	struct poly1305_desc_ctx *dctx = shash_desc_ctx(desc);

	if (unlikely(dctx->buflen)) {
		u32 bytes = min(len, POLY1305_BLOCK_SIZE - dctx->buflen);

		memcpy(dctx->buf + dctx->buflen, src, bytes);
		src += bytes;
		len -= bytes;
		dctx->buflen += bytes;

		if (dctx->buflen == POLY1305_BLOCK_SIZE) {
			mips_poly1305_blocks(dctx, dctx->buf, POLY1305_BLOCK_SIZE, 1);
			dctx->buflen = 0;
		}
	}

	if (likely(len >= POLY1305_BLOCK_SIZE)) {
		mips_poly1305_blocks(dctx, src, len, 1);
		src += round_down(len, POLY1305_BLOCK_SIZE);
		len %= POLY1305_BLOCK_SIZE;
	}

	if (unlikely(len)) {
		dctx->buflen = len;
		memcpy(dctx->buf, src, len);
	}
	return 0;
}

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

static int mips_poly1305_final(struct shash_desc *desc, u8 *dst)
{
	struct poly1305_desc_ctx *dctx = shash_desc_ctx(desc);

	if (unlikely(!dctx->sset))
		return -ENOKEY;

	poly1305_final_arch(dctx, dst);
	return 0;
}

static struct shash_alg mips_poly1305_alg = {
	.init			= mips_poly1305_init,
	.update			= mips_poly1305_update,
	.final			= mips_poly1305_final,
	.digestsize		= POLY1305_DIGEST_SIZE,
	.descsize		= sizeof(struct poly1305_desc_ctx),

	.base.cra_name		= "poly1305",
	.base.cra_driver_name	= "poly1305-mips",
	.base.cra_priority	= 200,
	.base.cra_blocksize	= POLY1305_BLOCK_SIZE,
	.base.cra_module	= THIS_MODULE,
};

static int __init mips_poly1305_mod_init(void)
{
	return IS_REACHABLE(CONFIG_CRYPTO_HASH) ?
		crypto_register_shash(&mips_poly1305_alg) : 0;
}

static void __exit mips_poly1305_mod_exit(void)
{
	if (IS_REACHABLE(CONFIG_CRYPTO_HASH))
		crypto_unregister_shash(&mips_poly1305_alg);
}

module_init(mips_poly1305_mod_init);
module_exit(mips_poly1305_mod_exit);

MODULE_DESCRIPTION("Poly1305 transform (MIPS accelerated");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS_CRYPTO("poly1305");
MODULE_ALIAS_CRYPTO("poly1305-mips");
