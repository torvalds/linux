/*
 * Poly1305 authenticator algorithm, RFC7539
 *
 * Copyright (C) 2015 Martin Willi
 *
 * Based on public domain code by Andrew Moon and Daniel J. Bernstein.
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

static inline u32 le32_to_cpuvp(const void *p)
{
	return le32_to_cpup(p);
}

int crypto_poly1305_init(struct shash_desc *desc)
{
	struct poly1305_desc_ctx *dctx = shash_desc_ctx(desc);

	memset(dctx->h, 0, sizeof(dctx->h));
	dctx->buflen = 0;
	dctx->rset = false;
	dctx->sset = false;

	return 0;
}
EXPORT_SYMBOL_GPL(crypto_poly1305_init);

int crypto_poly1305_setkey(struct crypto_shash *tfm,
			   const u8 *key, unsigned int keylen)
{
	/* Poly1305 requires a unique key for each tag, which implies that
	 * we can't set it on the tfm that gets accessed by multiple users
	 * simultaneously. Instead we expect the key as the first 32 bytes in
	 * the update() call. */
	return -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(crypto_poly1305_setkey);

static void poly1305_setrkey(struct poly1305_desc_ctx *dctx, const u8 *key)
{
	/* r &= 0xffffffc0ffffffc0ffffffc0fffffff */
	dctx->r[0] = (le32_to_cpuvp(key +  0) >> 0) & 0x3ffffff;
	dctx->r[1] = (le32_to_cpuvp(key +  3) >> 2) & 0x3ffff03;
	dctx->r[2] = (le32_to_cpuvp(key +  6) >> 4) & 0x3ffc0ff;
	dctx->r[3] = (le32_to_cpuvp(key +  9) >> 6) & 0x3f03fff;
	dctx->r[4] = (le32_to_cpuvp(key + 12) >> 8) & 0x00fffff;
}

static void poly1305_setskey(struct poly1305_desc_ctx *dctx, const u8 *key)
{
	dctx->s[0] = le32_to_cpuvp(key +  0);
	dctx->s[1] = le32_to_cpuvp(key +  4);
	dctx->s[2] = le32_to_cpuvp(key +  8);
	dctx->s[3] = le32_to_cpuvp(key + 12);
}

unsigned int crypto_poly1305_setdesckey(struct poly1305_desc_ctx *dctx,
					const u8 *src, unsigned int srclen)
{
	if (!dctx->sset) {
		if (!dctx->rset && srclen >= POLY1305_BLOCK_SIZE) {
			poly1305_setrkey(dctx, src);
			src += POLY1305_BLOCK_SIZE;
			srclen -= POLY1305_BLOCK_SIZE;
			dctx->rset = true;
		}
		if (srclen >= POLY1305_BLOCK_SIZE) {
			poly1305_setskey(dctx, src);
			src += POLY1305_BLOCK_SIZE;
			srclen -= POLY1305_BLOCK_SIZE;
			dctx->sset = true;
		}
	}
	return srclen;
}
EXPORT_SYMBOL_GPL(crypto_poly1305_setdesckey);

static unsigned int poly1305_blocks(struct poly1305_desc_ctx *dctx,
				    const u8 *src, unsigned int srclen,
				    u32 hibit)
{
	u32 r0, r1, r2, r3, r4;
	u32 s1, s2, s3, s4;
	u32 h0, h1, h2, h3, h4;
	u64 d0, d1, d2, d3, d4;
	unsigned int datalen;

	if (unlikely(!dctx->sset)) {
		datalen = crypto_poly1305_setdesckey(dctx, src, srclen);
		src += srclen - datalen;
		srclen = datalen;
	}

	r0 = dctx->r[0];
	r1 = dctx->r[1];
	r2 = dctx->r[2];
	r3 = dctx->r[3];
	r4 = dctx->r[4];

	s1 = r1 * 5;
	s2 = r2 * 5;
	s3 = r3 * 5;
	s4 = r4 * 5;

	h0 = dctx->h[0];
	h1 = dctx->h[1];
	h2 = dctx->h[2];
	h3 = dctx->h[3];
	h4 = dctx->h[4];

	while (likely(srclen >= POLY1305_BLOCK_SIZE)) {

		/* h += m[i] */
		h0 += (le32_to_cpuvp(src +  0) >> 0) & 0x3ffffff;
		h1 += (le32_to_cpuvp(src +  3) >> 2) & 0x3ffffff;
		h2 += (le32_to_cpuvp(src +  6) >> 4) & 0x3ffffff;
		h3 += (le32_to_cpuvp(src +  9) >> 6) & 0x3ffffff;
		h4 += (le32_to_cpuvp(src + 12) >> 8) | hibit;

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
		srclen -= POLY1305_BLOCK_SIZE;
	}

	dctx->h[0] = h0;
	dctx->h[1] = h1;
	dctx->h[2] = h2;
	dctx->h[3] = h3;
	dctx->h[4] = h4;

	return srclen;
}

int crypto_poly1305_update(struct shash_desc *desc,
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
			poly1305_blocks(dctx, dctx->buf,
					POLY1305_BLOCK_SIZE, 1 << 24);
			dctx->buflen = 0;
		}
	}

	if (likely(srclen >= POLY1305_BLOCK_SIZE)) {
		bytes = poly1305_blocks(dctx, src, srclen, 1 << 24);
		src += srclen - bytes;
		srclen = bytes;
	}

	if (unlikely(srclen)) {
		dctx->buflen = srclen;
		memcpy(dctx->buf, src, srclen);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(crypto_poly1305_update);

int crypto_poly1305_final(struct shash_desc *desc, u8 *dst)
{
	struct poly1305_desc_ctx *dctx = shash_desc_ctx(desc);
	__le32 *mac = (__le32 *)dst;
	u32 h0, h1, h2, h3, h4;
	u32 g0, g1, g2, g3, g4;
	u32 mask;
	u64 f = 0;

	if (unlikely(!dctx->sset))
		return -ENOKEY;

	if (unlikely(dctx->buflen)) {
		dctx->buf[dctx->buflen++] = 1;
		memset(dctx->buf + dctx->buflen, 0,
		       POLY1305_BLOCK_SIZE - dctx->buflen);
		poly1305_blocks(dctx, dctx->buf, POLY1305_BLOCK_SIZE, 0);
	}

	/* fully carry h */
	h0 = dctx->h[0];
	h1 = dctx->h[1];
	h2 = dctx->h[2];
	h3 = dctx->h[3];
	h4 = dctx->h[4];

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
	h0 = (h0 >>  0) | (h1 << 26);
	h1 = (h1 >>  6) | (h2 << 20);
	h2 = (h2 >> 12) | (h3 << 14);
	h3 = (h3 >> 18) | (h4 <<  8);

	/* mac = (h + s) % (2^128) */
	f = (f >> 32) + h0 + dctx->s[0]; mac[0] = cpu_to_le32(f);
	f = (f >> 32) + h1 + dctx->s[1]; mac[1] = cpu_to_le32(f);
	f = (f >> 32) + h2 + dctx->s[2]; mac[2] = cpu_to_le32(f);
	f = (f >> 32) + h3 + dctx->s[3]; mac[3] = cpu_to_le32(f);

	return 0;
}
EXPORT_SYMBOL_GPL(crypto_poly1305_final);

static struct shash_alg poly1305_alg = {
	.digestsize	= POLY1305_DIGEST_SIZE,
	.init		= crypto_poly1305_init,
	.update		= crypto_poly1305_update,
	.final		= crypto_poly1305_final,
	.setkey		= crypto_poly1305_setkey,
	.descsize	= sizeof(struct poly1305_desc_ctx),
	.base		= {
		.cra_name		= "poly1305",
		.cra_driver_name	= "poly1305-generic",
		.cra_priority		= 100,
		.cra_flags		= CRYPTO_ALG_TYPE_SHASH,
		.cra_alignmask		= sizeof(u32) - 1,
		.cra_blocksize		= POLY1305_BLOCK_SIZE,
		.cra_module		= THIS_MODULE,
	},
};

static int __init poly1305_mod_init(void)
{
	return crypto_register_shash(&poly1305_alg);
}

static void __exit poly1305_mod_exit(void)
{
	crypto_unregister_shash(&poly1305_alg);
}

module_init(poly1305_mod_init);
module_exit(poly1305_mod_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Martin Willi <martin@strongswan.org>");
MODULE_DESCRIPTION("Poly1305 authenticator");
MODULE_ALIAS_CRYPTO("poly1305");
MODULE_ALIAS_CRYPTO("poly1305-generic");
