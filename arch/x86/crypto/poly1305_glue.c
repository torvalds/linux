// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Poly1305 authenticator algorithm, RFC7539, SIMD glue code
 *
 * Copyright (C) 2015 Martin Willi
 */

#include <crypto/algapi.h>
#include <crypto/internal/hash.h>
#include <crypto/internal/poly1305.h>
#include <crypto/internal/simd.h>
#include <linux/crypto.h>
#include <linux/jump_label.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <asm/simd.h>

asmlinkage void poly1305_block_sse2(u32 *h, const u8 *src,
				    const u32 *r, unsigned int blocks);
asmlinkage void poly1305_2block_sse2(u32 *h, const u8 *src, const u32 *r,
				     unsigned int blocks, const u32 *u);
asmlinkage void poly1305_4block_avx2(u32 *h, const u8 *src, const u32 *r,
				     unsigned int blocks, const u32 *u);

static __ro_after_init DEFINE_STATIC_KEY_FALSE(poly1305_use_simd);
static __ro_after_init DEFINE_STATIC_KEY_FALSE(poly1305_use_avx2);

static inline u64 mlt(u64 a, u64 b)
{
	return a * b;
}

static inline u32 sr(u64 v, u_char n)
{
	return v >> n;
}

static inline u32 and(u32 v, u32 mask)
{
	return v & mask;
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

static void poly1305_integer_setkey(struct poly1305_key *key, const u8 *raw_key)
{
	/* r &= 0xffffffc0ffffffc0ffffffc0fffffff */
	key->r[0] = (get_unaligned_le32(raw_key +  0) >> 0) & 0x3ffffff;
	key->r[1] = (get_unaligned_le32(raw_key +  3) >> 2) & 0x3ffff03;
	key->r[2] = (get_unaligned_le32(raw_key +  6) >> 4) & 0x3ffc0ff;
	key->r[3] = (get_unaligned_le32(raw_key +  9) >> 6) & 0x3f03fff;
	key->r[4] = (get_unaligned_le32(raw_key + 12) >> 8) & 0x00fffff;
}

static void poly1305_integer_blocks(struct poly1305_state *state,
				    const struct poly1305_key *key,
				    const void *src,
				    unsigned int nblocks, u32 hibit)
{
	u32 r0, r1, r2, r3, r4;
	u32 s1, s2, s3, s4;
	u32 h0, h1, h2, h3, h4;
	u64 d0, d1, d2, d3, d4;

	if (!nblocks)
		return;

	r0 = key->r[0];
	r1 = key->r[1];
	r2 = key->r[2];
	r3 = key->r[3];
	r4 = key->r[4];

	s1 = r1 * 5;
	s2 = r2 * 5;
	s3 = r3 * 5;
	s4 = r4 * 5;

	h0 = state->h[0];
	h1 = state->h[1];
	h2 = state->h[2];
	h3 = state->h[3];
	h4 = state->h[4];

	do {
		/* h += m[i] */
		h0 += (get_unaligned_le32(src +  0) >> 0) & 0x3ffffff;
		h1 += (get_unaligned_le32(src +  3) >> 2) & 0x3ffffff;
		h2 += (get_unaligned_le32(src +  6) >> 4) & 0x3ffffff;
		h3 += (get_unaligned_le32(src +  9) >> 6) & 0x3ffffff;
		h4 += (get_unaligned_le32(src + 12) >> 8) | (hibit << 24);

		/* h *= r */
		d0 = mlt(h0, r0) + mlt(h1, s4) + mlt(h2, s3) +
		     mlt(h3, s2) + mlt(h4, s1);
		d1 = mlt(h0, r1) + mlt(h1, r0) + mlt(h2, s4) +
		     mlt(h3, s3) + mlt(h4, s2);
		d2 = mlt(h0, r2) + mlt(h1, r1) + mlt(h2, r0) +
		     mlt(h3, s4) + mlt(h4, s3);
		d3 = mlt(h0, r3) + mlt(h1, r2) + mlt(h2, r1) +
		     mlt(h3, r0) + mlt(h4, s4);
		d4 = mlt(h0, r4) + mlt(h1, r3) + mlt(h2, r2) +
		     mlt(h3, r1) + mlt(h4, r0);

		/* (partial) h %= p */
		d1 += sr(d0, 26);     h0 = and(d0, 0x3ffffff);
		d2 += sr(d1, 26);     h1 = and(d1, 0x3ffffff);
		d3 += sr(d2, 26);     h2 = and(d2, 0x3ffffff);
		d4 += sr(d3, 26);     h3 = and(d3, 0x3ffffff);
		h0 += sr(d4, 26) * 5; h4 = and(d4, 0x3ffffff);
		h1 += h0 >> 26;       h0 = h0 & 0x3ffffff;

		src += POLY1305_BLOCK_SIZE;
	} while (--nblocks);

	state->h[0] = h0;
	state->h[1] = h1;
	state->h[2] = h2;
	state->h[3] = h3;
	state->h[4] = h4;
}

static void poly1305_integer_emit(const struct poly1305_state *state, void *dst)
{
	u32 h0, h1, h2, h3, h4;
	u32 g0, g1, g2, g3, g4;
	u32 mask;

	/* fully carry h */
	h0 = state->h[0];
	h1 = state->h[1];
	h2 = state->h[2];
	h3 = state->h[3];
	h4 = state->h[4];

	h2 += (h1 >> 26);     h1 = h1 & 0x3ffffff;
	h3 += (h2 >> 26);     h2 = h2 & 0x3ffffff;
	h4 += (h3 >> 26);     h3 = h3 & 0x3ffffff;
	h0 += (h4 >> 26) * 5; h4 = h4 & 0x3ffffff;
	h1 += (h0 >> 26);     h0 = h0 & 0x3ffffff;

	/* compute h + -p */
	g0 = h0 + 5;
	g1 = h1 + (g0 >> 26);             g0 &= 0x3ffffff;
	g2 = h2 + (g1 >> 26);             g1 &= 0x3ffffff;
	g3 = h3 + (g2 >> 26);             g2 &= 0x3ffffff;
	g4 = h4 + (g3 >> 26) - (1 << 26); g3 &= 0x3ffffff;

	/* select h if h < p, or h + -p if h >= p */
	mask = (g4 >> ((sizeof(u32) * 8) - 1)) - 1;
	g0 &= mask;
	g1 &= mask;
	g2 &= mask;
	g3 &= mask;
	g4 &= mask;
	mask = ~mask;
	h0 = (h0 & mask) | g0;
	h1 = (h1 & mask) | g1;
	h2 = (h2 & mask) | g2;
	h3 = (h3 & mask) | g3;
	h4 = (h4 & mask) | g4;

	/* h = h % (2^128) */
	put_unaligned_le32((h0 >>  0) | (h1 << 26), dst +  0);
	put_unaligned_le32((h1 >>  6) | (h2 << 20), dst +  4);
	put_unaligned_le32((h2 >> 12) | (h3 << 14), dst +  8);
	put_unaligned_le32((h3 >> 18) | (h4 <<  8), dst + 12);
}

void poly1305_init_arch(struct poly1305_desc_ctx *desc, const u8 *key)
{
	poly1305_integer_setkey(desc->opaque_r, key);
	desc->s[0] = get_unaligned_le32(key + 16);
	desc->s[1] = get_unaligned_le32(key + 20);
	desc->s[2] = get_unaligned_le32(key + 24);
	desc->s[3] = get_unaligned_le32(key + 28);
	poly1305_core_init(&desc->h);
	desc->buflen = 0;
	desc->sset = true;
	desc->rset = 1;
}
EXPORT_SYMBOL_GPL(poly1305_init_arch);

static unsigned int crypto_poly1305_setdesckey(struct poly1305_desc_ctx *dctx,
					       const u8 *src, unsigned int srclen)
{
	if (!dctx->sset) {
		if (!dctx->rset && srclen >= POLY1305_BLOCK_SIZE) {
			poly1305_integer_setkey(dctx->r, src);
			src += POLY1305_BLOCK_SIZE;
			srclen -= POLY1305_BLOCK_SIZE;
			dctx->rset = 1;
		}
		if (srclen >= POLY1305_BLOCK_SIZE) {
			dctx->s[0] = get_unaligned_le32(src +  0);
			dctx->s[1] = get_unaligned_le32(src +  4);
			dctx->s[2] = get_unaligned_le32(src +  8);
			dctx->s[3] = get_unaligned_le32(src + 12);
			src += POLY1305_BLOCK_SIZE;
			srclen -= POLY1305_BLOCK_SIZE;
			dctx->sset = true;
		}
	}
	return srclen;
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
		poly1305_integer_blocks(&dctx->h, dctx->opaque_r, src,
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
	    static_branch_likely(&poly1305_use_avx2) &&
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

void poly1305_update_arch(struct poly1305_desc_ctx *dctx, const u8 *src,
			  unsigned int srclen)
{
	unsigned int bytes;

	if (unlikely(dctx->buflen)) {
		bytes = min(srclen, POLY1305_BLOCK_SIZE - dctx->buflen);
		memcpy(dctx->buf + dctx->buflen, src, bytes);
		src += bytes;
		srclen -= bytes;
		dctx->buflen += bytes;

		if (dctx->buflen == POLY1305_BLOCK_SIZE) {
			if (static_branch_likely(&poly1305_use_simd) &&
			    likely(crypto_simd_usable())) {
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
		if (static_branch_likely(&poly1305_use_simd) &&
		    likely(crypto_simd_usable())) {
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
EXPORT_SYMBOL(poly1305_update_arch);

void poly1305_final_arch(struct poly1305_desc_ctx *desc, u8 *dst)
{
	__le32 digest[4];
	u64 f = 0;

	if (unlikely(desc->buflen)) {
		desc->buf[desc->buflen++] = 1;
		memset(desc->buf + desc->buflen, 0,
		       POLY1305_BLOCK_SIZE - desc->buflen);
		poly1305_integer_blocks(&desc->h, desc->opaque_r, desc->buf, 1, 0);
	}

	poly1305_integer_emit(&desc->h, digest);

	/* mac = (h + s) % (2^128) */
	f = (f >> 32) + le32_to_cpu(digest[0]) + desc->s[0];
	put_unaligned_le32(f, dst + 0);
	f = (f >> 32) + le32_to_cpu(digest[1]) + desc->s[1];
	put_unaligned_le32(f, dst + 4);
	f = (f >> 32) + le32_to_cpu(digest[2]) + desc->s[2];
	put_unaligned_le32(f, dst + 8);
	f = (f >> 32) + le32_to_cpu(digest[3]) + desc->s[3];
	put_unaligned_le32(f, dst + 12);

	*desc = (struct poly1305_desc_ctx){};
}
EXPORT_SYMBOL(poly1305_final_arch);

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

	poly1305_final_arch(dctx, dst);
	return 0;
}

static int poly1305_simd_update(struct shash_desc *desc,
				const u8 *src, unsigned int srclen)
{
	struct poly1305_desc_ctx *dctx = shash_desc_ctx(desc);

	poly1305_update_arch(dctx, src, srclen);
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
		return 0;

	static_branch_enable(&poly1305_use_simd);

	if (IS_ENABLED(CONFIG_AS_AVX2) &&
	    boot_cpu_has(X86_FEATURE_AVX) &&
	    boot_cpu_has(X86_FEATURE_AVX2) &&
	    cpu_has_xfeatures(XFEATURE_MASK_SSE | XFEATURE_MASK_YMM, NULL))
		static_branch_enable(&poly1305_use_avx2);

	return IS_REACHABLE(CONFIG_CRYPTO_HASH) ? crypto_register_shash(&alg) : 0;
}

static void __exit poly1305_simd_mod_exit(void)
{
	if (IS_REACHABLE(CONFIG_CRYPTO_HASH))
		crypto_unregister_shash(&alg);
}

module_init(poly1305_simd_mod_init);
module_exit(poly1305_simd_mod_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Martin Willi <martin@strongswan.org>");
MODULE_DESCRIPTION("Poly1305 authenticator");
MODULE_ALIAS_CRYPTO("poly1305");
MODULE_ALIAS_CRYPTO("poly1305-simd");
