/*
 * Poly1305 authenticator algorithm, RFC7539, SIMD glue code
 *
 * Copyright (C) 2015 Martin Willi
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <crypto/algapi.h>
#include <crypto/internal/hash.h>
#include <crypto/poly1305.h>
#include <linux/crypto.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <asm/fpu/api.h>
#include <asm/simd.h>

asmlinkage void poly1305_block_sse2(u32 *h, const u8 *src,
				    const u32 *r, unsigned int blocks);

static unsigned int poly1305_simd_blocks(struct poly1305_desc_ctx *dctx,
					 const u8 *src, unsigned int srclen)
{
	unsigned int blocks, datalen;

	if (unlikely(!dctx->sset)) {
		datalen = crypto_poly1305_setdesckey(dctx, src, srclen);
		src += srclen - datalen;
		srclen = datalen;
	}

	if (srclen >= POLY1305_BLOCK_SIZE) {
		blocks = srclen / POLY1305_BLOCK_SIZE;
		poly1305_block_sse2(dctx->h, src, dctx->r, blocks);
		srclen -= POLY1305_BLOCK_SIZE * blocks;
	}
	return srclen;
}

static int poly1305_simd_update(struct shash_desc *desc,
				const u8 *src, unsigned int srclen)
{
	struct poly1305_desc_ctx *dctx = shash_desc_ctx(desc);
	unsigned int bytes;

	/* kernel_fpu_begin/end is costly, use fallback for small updates */
	if (srclen <= 288 || !may_use_simd())
		return crypto_poly1305_update(desc, src, srclen);

	kernel_fpu_begin();

	if (unlikely(dctx->buflen)) {
		bytes = min(srclen, POLY1305_BLOCK_SIZE - dctx->buflen);
		memcpy(dctx->buf + dctx->buflen, src, bytes);
		src += bytes;
		srclen -= bytes;
		dctx->buflen += bytes;

		if (dctx->buflen == POLY1305_BLOCK_SIZE) {
			poly1305_simd_blocks(dctx, dctx->buf,
					     POLY1305_BLOCK_SIZE);
			dctx->buflen = 0;
		}
	}

	if (likely(srclen >= POLY1305_BLOCK_SIZE)) {
		bytes = poly1305_simd_blocks(dctx, src, srclen);
		src += srclen - bytes;
		srclen = bytes;
	}

	kernel_fpu_end();

	if (unlikely(srclen)) {
		dctx->buflen = srclen;
		memcpy(dctx->buf, src, srclen);
	}

	return 0;
}

static struct shash_alg alg = {
	.digestsize	= POLY1305_DIGEST_SIZE,
	.init		= crypto_poly1305_init,
	.update		= poly1305_simd_update,
	.final		= crypto_poly1305_final,
	.setkey		= crypto_poly1305_setkey,
	.descsize	= sizeof(struct poly1305_desc_ctx),
	.base		= {
		.cra_name		= "poly1305",
		.cra_driver_name	= "poly1305-simd",
		.cra_priority		= 300,
		.cra_flags		= CRYPTO_ALG_TYPE_SHASH,
		.cra_alignmask		= sizeof(u32) - 1,
		.cra_blocksize		= POLY1305_BLOCK_SIZE,
		.cra_module		= THIS_MODULE,
	},
};

static int __init poly1305_simd_mod_init(void)
{
	if (!cpu_has_xmm2)
		return -ENODEV;

	return crypto_register_shash(&alg);
}

static void __exit poly1305_simd_mod_exit(void)
{
	crypto_unregister_shash(&alg);
}

module_init(poly1305_simd_mod_init);
module_exit(poly1305_simd_mod_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Martin Willi <martin@strongswan.org>");
MODULE_DESCRIPTION("Poly1305 authenticator");
MODULE_ALIAS_CRYPTO("poly1305");
MODULE_ALIAS_CRYPTO("poly1305-simd");
