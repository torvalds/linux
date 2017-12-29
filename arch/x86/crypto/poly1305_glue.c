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

struct poly1305_simd_desc_ctx {
	struct poly1305_desc_ctx base;
	/* derived key u set? */
	bool uset;
#ifdef CONFIG_AS_AVX2
	/* derived keys r^3, r^4 set? */
	bool wset;
#endif
	/* derived Poly1305 key r^2 */
	u32 u[5];
	/* ... silently appended r^3 and r^4 when using AVX2 */
};

asmlinkage void poly1305_block_sse2(u32 *h, const u8 *src,
				    const u32 *r, unsigned int blocks);
asmlinkage void poly1305_2block_sse2(u32 *h, const u8 *src, const u32 *r,
				     unsigned int blocks, const u32 *u);
#ifdef CONFIG_AS_AVX2
asmlinkage void poly1305_4block_avx2(u32 *h, const u8 *src, const u32 *r,
				     unsigned int blocks, const u32 *u);
static bool poly1305_use_avx2;
#endif

static int poly1305_simd_init(struct shash_desc *desc)
{
	struct poly1305_simd_desc_ctx *sctx = shash_desc_ctx(desc);

	sctx->uset = false;
#ifdef CONFIG_AS_AVX2
	sctx->wset = false;
#endif

	return crypto_poly1305_init(desc);
}

static void poly1305_simd_mult(u32 *a, const u32 *b)
{
	u8 m[POLY1305_BLOCK_SIZE];

	memset(m, 0, sizeof(m));
	/* The poly1305 block function adds a hi-bit to the accumulator which
	 * we don't need for key multiplication; compensate for it. */
	a[4] -= 1 << 24;
	poly1305_block_sse2(a, m, b, 1);
}

static unsigned int poly1305_simd_blocks(struct poly1305_desc_ctx *dctx,
					 const u8 *src, unsigned int srclen)
{
	struct poly1305_simd_desc_ctx *sctx;
	unsigned int blocks, datalen;

	BUILD_BUG_ON(offsetof(struct poly1305_simd_desc_ctx, base));
	sctx = container_of(dctx, struct poly1305_simd_desc_ctx, base);

	if (unlikely(!dctx->sset)) {
		datalen = crypto_poly1305_setdesckey(dctx, src, srclen);
		src += srclen - datalen;
		srclen = datalen;
	}

#ifdef CONFIG_AS_AVX2
	if (poly1305_use_avx2 && srclen >= POLY1305_BLOCK_SIZE * 4) {
		if (unlikely(!sctx->wset)) {
			if (!sctx->uset) {
				memcpy(sctx->u, dctx->r, sizeof(sctx->u));
				poly1305_simd_mult(sctx->u, dctx->r);
				sctx->uset = true;
			}
			memcpy(sctx->u + 5, sctx->u, sizeof(sctx->u));
			poly1305_simd_mult(sctx->u + 5, dctx->r);
			memcpy(sctx->u + 10, sctx->u + 5, sizeof(sctx->u));
			poly1305_simd_mult(sctx->u + 10, dctx->r);
			sctx->wset = true;
		}
		blocks = srclen / (POLY1305_BLOCK_SIZE * 4);
		poly1305_4block_avx2(dctx->h, src, dctx->r, blocks, sctx->u);
		src += POLY1305_BLOCK_SIZE * 4 * blocks;
		srclen -= POLY1305_BLOCK_SIZE * 4 * blocks;
	}
#endif
	if (likely(srclen >= POLY1305_BLOCK_SIZE * 2)) {
		if (unlikely(!sctx->uset)) {
			memcpy(sctx->u, dctx->r, sizeof(sctx->u));
			poly1305_simd_mult(sctx->u, dctx->r);
			sctx->uset = true;
		}
		blocks = srclen / (POLY1305_BLOCK_SIZE * 2);
		poly1305_2block_sse2(dctx->h, src, dctx->r, blocks, sctx->u);
		src += POLY1305_BLOCK_SIZE * 2 * blocks;
		srclen -= POLY1305_BLOCK_SIZE * 2 * blocks;
	}
	if (srclen >= POLY1305_BLOCK_SIZE) {
		poly1305_block_sse2(dctx->h, src, dctx->r, 1);
		srclen -= POLY1305_BLOCK_SIZE;
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
	.init		= poly1305_simd_init,
	.update		= poly1305_simd_update,
	.final		= crypto_poly1305_final,
	.setkey		= crypto_poly1305_setkey,
	.descsize	= sizeof(struct poly1305_simd_desc_ctx),
	.base		= {
		.cra_name		= "poly1305",
		.cra_driver_name	= "poly1305-simd",
		.cra_priority		= 300,
		.cra_flags		= CRYPTO_ALG_TYPE_SHASH,
		.cra_blocksize		= POLY1305_BLOCK_SIZE,
		.cra_module		= THIS_MODULE,
	},
};

static int __init poly1305_simd_mod_init(void)
{
	if (!boot_cpu_has(X86_FEATURE_XMM2))
		return -ENODEV;

#ifdef CONFIG_AS_AVX2
	poly1305_use_avx2 = boot_cpu_has(X86_FEATURE_AVX) &&
			    boot_cpu_has(X86_FEATURE_AVX2) &&
			    cpu_has_xfeatures(XFEATURE_MASK_SSE | XFEATURE_MASK_YMM, NULL);
	alg.descsize = sizeof(struct poly1305_simd_desc_ctx);
	if (poly1305_use_avx2)
		alg.descsize += 10 * sizeof(u32);
#endif
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
