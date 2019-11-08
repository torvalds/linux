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
#include <crypto/internal/poly1305.h>
#include <linux/crypto.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <asm/fpu/api.h>
#include <asm/simd.h>

asmlinkage void poly1305_block_sse2(u32 *h, const u8 *src,
				    const u32 *r, unsigned int blocks);
asmlinkage void poly1305_2block_sse2(u32 *h, const u8 *src, const u32 *r,
				     unsigned int blocks, const u32 *u);
asmlinkage void poly1305_4block_avx2(u32 *h, const u8 *src, const u32 *r,
				     unsigned int blocks, const u32 *u);

static bool poly1305_use_avx2 __ro_after_init;

static void poly1305_simd_mult(u32 *a, const u32 *b)
{
	u8 m[POLY1305_BLOCK_SIZE];

	memset(m, 0, sizeof(m));
	/* The poly1305 block function adds a hi-bit to the accumulator which
	 * we don't need for key multiplication; compensate for it. */
	a[4] -= 1 << 24;
	poly1305_block_sse2(a, m, b, 1);
}

static unsigned int poly1305_scalar_blocks(struct poly1305_desc_ctx *dctx,
					   const u8 *src, unsigned int srclen)
{
	unsigned int datalen;

	if (unlikely(!dctx->sset)) {
		datalen = crypto_poly1305_setdesckey(dctx, src, srclen);
		src += srclen - datalen;
		srclen = datalen;
	}
	if (srclen >= POLY1305_BLOCK_SIZE) {
		poly1305_core_blocks(&dctx->h, dctx->r, src,
				     srclen / POLY1305_BLOCK_SIZE, 1);
		srclen %= POLY1305_BLOCK_SIZE;
	}
	return srclen;
}

static unsigned int poly1305_simd_blocks(struct poly1305_desc_ctx *dctx,
					 const u8 *src, unsigned int srclen)
{
	unsigned int blocks, datalen;

	if (unlikely(!dctx->sset)) {
		datalen = crypto_poly1305_setdesckey(dctx, src, srclen);
		src += srclen - datalen;
		srclen = datalen;
	}

	if (IS_ENABLED(CONFIG_AS_AVX2) &&
	    poly1305_use_avx2 &&
	    srclen >= POLY1305_BLOCK_SIZE * 4) {
		if (unlikely(dctx->rset < 4)) {
			if (dctx->rset < 2) {
				dctx->r[1] = dctx->r[0];
				poly1305_simd_mult(dctx->r[1].r, dctx->r[0].r);
			}
			dctx->r[2] = dctx->r[1];
			poly1305_simd_mult(dctx->r[2].r, dctx->r[0].r);
			dctx->r[3] = dctx->r[2];
			poly1305_simd_mult(dctx->r[3].r, dctx->r[0].r);
			dctx->rset = 4;
		}
		blocks = srclen / (POLY1305_BLOCK_SIZE * 4);
		poly1305_4block_avx2(dctx->h.h, src, dctx->r[0].r, blocks,
				     dctx->r[1].r);
		src += POLY1305_BLOCK_SIZE * 4 * blocks;
		srclen -= POLY1305_BLOCK_SIZE * 4 * blocks;
	}

	if (likely(srclen >= POLY1305_BLOCK_SIZE * 2)) {
		if (unlikely(dctx->rset < 2)) {
			dctx->r[1] = dctx->r[0];
			poly1305_simd_mult(dctx->r[1].r, dctx->r[0].r);
			dctx->rset = 2;
		}
		blocks = srclen / (POLY1305_BLOCK_SIZE * 2);
		poly1305_2block_sse2(dctx->h.h, src, dctx->r[0].r,
				     blocks, dctx->r[1].r);
		src += POLY1305_BLOCK_SIZE * 2 * blocks;
		srclen -= POLY1305_BLOCK_SIZE * 2 * blocks;
	}
	if (srclen >= POLY1305_BLOCK_SIZE) {
		poly1305_block_sse2(dctx->h.h, src, dctx->r[0].r, 1);
		srclen -= POLY1305_BLOCK_SIZE;
	}
	return srclen;
}

static int poly1305_simd_update(struct shash_desc *desc,
				const u8 *src, unsigned int srclen)
{
	struct poly1305_desc_ctx *dctx = shash_desc_ctx(desc);
	unsigned int bytes;

	if (unlikely(dctx->buflen)) {
		bytes = min(srclen, POLY1305_BLOCK_SIZE - dctx->buflen);
		memcpy(dctx->buf + dctx->buflen, src, bytes);
		src += bytes;
		srclen -= bytes;
		dctx->buflen += bytes;

		if (dctx->buflen == POLY1305_BLOCK_SIZE) {
			if (likely(may_use_simd())) {
				kernel_fpu_begin();
				poly1305_simd_blocks(dctx, dctx->buf,
						     POLY1305_BLOCK_SIZE);
				kernel_fpu_end();
			} else {
				poly1305_scalar_blocks(dctx, dctx->buf,
						       POLY1305_BLOCK_SIZE);
			}
			dctx->buflen = 0;
		}
	}

	if (likely(srclen >= POLY1305_BLOCK_SIZE)) {
		if (likely(may_use_simd())) {
			kernel_fpu_begin();
			bytes = poly1305_simd_blocks(dctx, src, srclen);
			kernel_fpu_end();
		} else {
			bytes = poly1305_scalar_blocks(dctx, src, srclen);
		}
		src += srclen - bytes;
		srclen = bytes;
	}

	if (unlikely(srclen)) {
		dctx->buflen = srclen;
		memcpy(dctx->buf, src, srclen);
	}
}

static int crypto_poly1305_init(struct shash_desc *desc)
{
	struct poly1305_desc_ctx *dctx = shash_desc_ctx(desc);

	poly1305_core_init(&dctx->h);
	dctx->buflen = 0;
	dctx->rset = 0;
	dctx->sset = false;

	return 0;
}

static int crypto_poly1305_final(struct shash_desc *desc, u8 *dst)
{
	struct poly1305_desc_ctx *dctx = shash_desc_ctx(desc);

	if (unlikely(!dctx->sset))
		return -ENOKEY;

	poly1305_final_generic(dctx, dst);
	return 0;
}

static struct shash_alg alg = {
	.digestsize	= POLY1305_DIGEST_SIZE,
	.init		= crypto_poly1305_init,
	.update		= poly1305_simd_update,
	.final		= crypto_poly1305_final,
	.descsize	= sizeof(struct poly1305_desc_ctx),
	.base		= {
		.cra_name		= "poly1305",
		.cra_driver_name	= "poly1305-simd",
		.cra_priority		= 300,
		.cra_blocksize		= POLY1305_BLOCK_SIZE,
		.cra_module		= THIS_MODULE,
	},
};

static int __init poly1305_simd_mod_init(void)
{
	if (!boot_cpu_has(X86_FEATURE_XMM2))
		return -ENODEV;

	poly1305_use_avx2 = IS_ENABLED(CONFIG_AS_AVX2) &&
			    boot_cpu_has(X86_FEATURE_AVX) &&
			    boot_cpu_has(X86_FEATURE_AVX2) &&
			    cpu_has_xfeatures(XFEATURE_MASK_SSE | XFEATURE_MASK_YMM, NULL);
	alg.descsize = sizeof(struct poly1305_desc_ctx) + 5 * sizeof(u32);
	if (poly1305_use_avx2)
		alg.descsize += 10 * sizeof(u32);

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
