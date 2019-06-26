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
#include <asm/unaligned.h>

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

int crypto_poly1305_init(struct shash_desc *desc)
{
	struct poly1305_desc_ctx *dctx = shash_desc_ctx(desc);

	poly1305_core_init(&dctx->h);
	dctx->buflen = 0;
	dctx->rset = false;
	dctx->sset = false;

	return 0;
}
EXPORT_SYMBOL_GPL(crypto_poly1305_init);

void poly1305_core_setkey(struct poly1305_key *key, const u8 *raw_key)
{
	/* r &= 0xffffffc0ffffffc0ffffffc0fffffff */
	key->r[0] = (get_unaligned_le32(raw_key +  0) >> 0) & 0x3ffffff;
	key->r[1] = (get_unaligned_le32(raw_key +  3) >> 2) & 0x3ffff03;
	key->r[2] = (get_unaligned_le32(raw_key +  6) >> 4) & 0x3ffc0ff;
	key->r[3] = (get_unaligned_le32(raw_key +  9) >> 6) & 0x3f03fff;
	key->r[4] = (get_unaligned_le32(raw_key + 12) >> 8) & 0x00fffff;
}
EXPORT_SYMBOL_GPL(poly1305_core_setkey);

/*
 * Poly1305 requires a unique key for each tag, which implies that we can't set
 * it on the tfm that gets accessed by multiple users simultaneously. Instead we
 * expect the key as the first 32 bytes in the update() call.
 */
unsigned int crypto_poly1305_setdesckey(struct poly1305_desc_ctx *dctx,
					const u8 *src, unsigned int srclen)
{
	if (!dctx->sset) {
		if (!dctx->rset && srclen >= POLY1305_BLOCK_SIZE) {
			poly1305_core_setkey(&dctx->r, src);
			src += POLY1305_BLOCK_SIZE;
			srclen -= POLY1305_BLOCK_SIZE;
			dctx->rset = true;
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
EXPORT_SYMBOL_GPL(crypto_poly1305_setdesckey);

static void poly1305_blocks_internal(struct poly1305_state *state,
				     const struct poly1305_key *key,
				     const void *src, unsigned int nblocks,
				     u32 hibit)
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
		h4 += (get_unaligned_le32(src + 12) >> 8) | hibit;

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

void poly1305_core_blocks(struct poly1305_state *state,
			  const struct poly1305_key *key,
			  const void *src, unsigned int nblocks)
{
	poly1305_blocks_internal(state, key, src, nblocks, 1 << 24);
}
EXPORT_SYMBOL_GPL(poly1305_core_blocks);

static void poly1305_blocks(struct poly1305_desc_ctx *dctx,
			    const u8 *src, unsigned int srclen, u32 hibit)
{
	unsigned int datalen;

	if (unlikely(!dctx->sset)) {
		datalen = crypto_poly1305_setdesckey(dctx, src, srclen);
		src += srclen - datalen;
		srclen = datalen;
	}

	poly1305_blocks_internal(&dctx->h, &dctx->r,
				 src, srclen / POLY1305_BLOCK_SIZE, hibit);
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
		poly1305_blocks(dctx, src, srclen, 1 << 24);
		src += srclen - (srclen % POLY1305_BLOCK_SIZE);
		srclen %= POLY1305_BLOCK_SIZE;
	}

	if (unlikely(srclen)) {
		dctx->buflen = srclen;
		memcpy(dctx->buf, src, srclen);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(crypto_poly1305_update);

void poly1305_core_emit(const struct poly1305_state *state, void *dst)
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
EXPORT_SYMBOL_GPL(poly1305_core_emit);

int crypto_poly1305_final(struct shash_desc *desc, u8 *dst)
{
	struct poly1305_desc_ctx *dctx = shash_desc_ctx(desc);
	__le32 digest[4];
	u64 f = 0;

	if (unlikely(!dctx->sset))
		return -ENOKEY;

	if (unlikely(dctx->buflen)) {
		dctx->buf[dctx->buflen++] = 1;
		memset(dctx->buf + dctx->buflen, 0,
		       POLY1305_BLOCK_SIZE - dctx->buflen);
		poly1305_blocks(dctx, dctx->buf, POLY1305_BLOCK_SIZE, 0);
	}

	poly1305_core_emit(&dctx->h, digest);

	/* mac = (h + s) % (2^128) */
	f = (f >> 32) + le32_to_cpu(digest[0]) + dctx->s[0];
	put_unaligned_le32(f, dst + 0);
	f = (f >> 32) + le32_to_cpu(digest[1]) + dctx->s[1];
	put_unaligned_le32(f, dst + 4);
	f = (f >> 32) + le32_to_cpu(digest[2]) + dctx->s[2];
	put_unaligned_le32(f, dst + 8);
	f = (f >> 32) + le32_to_cpu(digest[3]) + dctx->s[3];
	put_unaligned_le32(f, dst + 12);

	return 0;
}
EXPORT_SYMBOL_GPL(crypto_poly1305_final);

static struct shash_alg poly1305_alg = {
	.digestsize	= POLY1305_DIGEST_SIZE,
	.init		= crypto_poly1305_init,
	.update		= crypto_poly1305_update,
	.final		= crypto_poly1305_final,
	.descsize	= sizeof(struct poly1305_desc_ctx),
	.base		= {
		.cra_name		= "poly1305",
		.cra_driver_name	= "poly1305-generic",
		.cra_priority		= 100,
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

subsys_initcall(poly1305_mod_init);
module_exit(poly1305_mod_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Martin Willi <martin@strongswan.org>");
MODULE_DESCRIPTION("Poly1305 authenticator");
MODULE_ALIAS_CRYPTO("poly1305");
MODULE_ALIAS_CRYPTO("poly1305-generic");
