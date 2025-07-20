// SPDX-License-Identifier: GPL-2.0-only
/*
 * GHASH: hash function for GCM (Galois/Counter Mode).
 *
 * Copyright (c) 2007 Nokia Siemens Networks - Mikko Herranen <mh1@iki.fi>
 * Copyright (c) 2009 Intel Corp.
 *   Author: Huang Ying <ying.huang@intel.com>
 */

/*
 * GHASH is a keyed hash function used in GCM authentication tag generation.
 *
 * The original GCM paper [1] presents GHASH as a function GHASH(H, A, C) which
 * takes a 16-byte hash key H, additional authenticated data A, and a ciphertext
 * C.  It formats A and C into a single byte string X, interprets X as a
 * polynomial over GF(2^128), and evaluates this polynomial at the point H.
 *
 * However, the NIST standard for GCM [2] presents GHASH as GHASH(H, X) where X
 * is the already-formatted byte string containing both A and C.
 *
 * "ghash" in the Linux crypto API uses the 'X' (pre-formatted) convention,
 * since the API supports only a single data stream per hash.  Thus, the
 * formatting of 'A' and 'C' is done in the "gcm" template, not in "ghash".
 *
 * The reason "ghash" is separate from "gcm" is to allow "gcm" to use an
 * accelerated "ghash" when a standalone accelerated "gcm(aes)" is unavailable.
 * It is generally inappropriate to use "ghash" for other purposes, since it is
 * an "ε-almost-XOR-universal hash function", not a cryptographic hash function.
 * It can only be used securely in crypto modes specially designed to use it.
 *
 * [1] The Galois/Counter Mode of Operation (GCM)
 *     (http://citeseerx.ist.psu.edu/viewdoc/download?doi=10.1.1.694.695&rep=rep1&type=pdf)
 * [2] Recommendation for Block Cipher Modes of Operation: Galois/Counter Mode (GCM) and GMAC
 *     (https://csrc.nist.gov/publications/detail/sp/800-38d/final)
 */

#include <crypto/gf128mul.h>
#include <crypto/ghash.h>
#include <crypto/internal/hash.h>
#include <crypto/utils.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>

static int ghash_init(struct shash_desc *desc)
{
	struct ghash_desc_ctx *dctx = shash_desc_ctx(desc);

	memset(dctx, 0, sizeof(*dctx));

	return 0;
}

static int ghash_setkey(struct crypto_shash *tfm,
			const u8 *key, unsigned int keylen)
{
	struct ghash_ctx *ctx = crypto_shash_ctx(tfm);
	be128 k;

	if (keylen != GHASH_BLOCK_SIZE)
		return -EINVAL;

	if (ctx->gf128)
		gf128mul_free_4k(ctx->gf128);

	BUILD_BUG_ON(sizeof(k) != GHASH_BLOCK_SIZE);
	memcpy(&k, key, GHASH_BLOCK_SIZE); /* avoid violating alignment rules */
	ctx->gf128 = gf128mul_init_4k_lle(&k);
	memzero_explicit(&k, GHASH_BLOCK_SIZE);

	if (!ctx->gf128)
		return -ENOMEM;

	return 0;
}

static int ghash_update(struct shash_desc *desc,
			 const u8 *src, unsigned int srclen)
{
	struct ghash_desc_ctx *dctx = shash_desc_ctx(desc);
	struct ghash_ctx *ctx = crypto_shash_ctx(desc->tfm);
	u8 *dst = dctx->buffer;

	do {
		crypto_xor(dst, src, GHASH_BLOCK_SIZE);
		gf128mul_4k_lle((be128 *)dst, ctx->gf128);
		src += GHASH_BLOCK_SIZE;
		srclen -= GHASH_BLOCK_SIZE;
	} while (srclen >= GHASH_BLOCK_SIZE);

	return srclen;
}

static void ghash_flush(struct shash_desc *desc, const u8 *src,
			unsigned int len)
{
	struct ghash_ctx *ctx = crypto_shash_ctx(desc->tfm);
	struct ghash_desc_ctx *dctx = shash_desc_ctx(desc);
	u8 *dst = dctx->buffer;

	if (len) {
		crypto_xor(dst, src, len);
		gf128mul_4k_lle((be128 *)dst, ctx->gf128);
	}
}

static int ghash_finup(struct shash_desc *desc, const u8 *src,
		       unsigned int len, u8 *dst)
{
	struct ghash_desc_ctx *dctx = shash_desc_ctx(desc);
	u8 *buf = dctx->buffer;

	ghash_flush(desc, src, len);
	memcpy(dst, buf, GHASH_BLOCK_SIZE);

	return 0;
}

static void ghash_exit_tfm(struct crypto_tfm *tfm)
{
	struct ghash_ctx *ctx = crypto_tfm_ctx(tfm);
	if (ctx->gf128)
		gf128mul_free_4k(ctx->gf128);
}

static struct shash_alg ghash_alg = {
	.digestsize	= GHASH_DIGEST_SIZE,
	.init		= ghash_init,
	.update		= ghash_update,
	.finup		= ghash_finup,
	.setkey		= ghash_setkey,
	.descsize	= sizeof(struct ghash_desc_ctx),
	.base		= {
		.cra_name		= "ghash",
		.cra_driver_name	= "ghash-generic",
		.cra_priority		= 100,
		.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
		.cra_blocksize		= GHASH_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct ghash_ctx),
		.cra_module		= THIS_MODULE,
		.cra_exit		= ghash_exit_tfm,
	},
};

static int __init ghash_mod_init(void)
{
	return crypto_register_shash(&ghash_alg);
}

static void __exit ghash_mod_exit(void)
{
	crypto_unregister_shash(&ghash_alg);
}

module_init(ghash_mod_init);
module_exit(ghash_mod_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("GHASH hash function");
MODULE_ALIAS_CRYPTO("ghash");
MODULE_ALIAS_CRYPTO("ghash-generic");
