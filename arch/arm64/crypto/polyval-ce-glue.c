// SPDX-License-Identifier: GPL-2.0-only
/*
 * Glue code for POLYVAL using ARMv8 Crypto Extensions
 *
 * Copyright (c) 2007 Nokia Siemens Networks - Mikko Herranen <mh1@iki.fi>
 * Copyright (c) 2009 Intel Corp.
 *   Author: Huang Ying <ying.huang@intel.com>
 * Copyright 2021 Google LLC
 */

/*
 * Glue code based on ghash-clmulni-intel_glue.c.
 *
 * This implementation of POLYVAL uses montgomery multiplication accelerated by
 * ARMv8 Crypto Extensions instructions to implement the finite field operations.
 */

#include <asm/neon.h>
#include <crypto/internal/hash.h>
#include <crypto/polyval.h>
#include <crypto/utils.h>
#include <linux/cpufeature.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>

#define NUM_KEY_POWERS	8

struct polyval_tfm_ctx {
	/*
	 * These powers must be in the order h^8, ..., h^1.
	 */
	u8 key_powers[NUM_KEY_POWERS][POLYVAL_BLOCK_SIZE];
};

struct polyval_desc_ctx {
	u8 buffer[POLYVAL_BLOCK_SIZE];
};

asmlinkage void pmull_polyval_update(const struct polyval_tfm_ctx *keys,
	const u8 *in, size_t nblocks, u8 *accumulator);
asmlinkage void pmull_polyval_mul(u8 *op1, const u8 *op2);

static void internal_polyval_update(const struct polyval_tfm_ctx *keys,
	const u8 *in, size_t nblocks, u8 *accumulator)
{
	kernel_neon_begin();
	pmull_polyval_update(keys, in, nblocks, accumulator);
	kernel_neon_end();
}

static void internal_polyval_mul(u8 *op1, const u8 *op2)
{
	kernel_neon_begin();
	pmull_polyval_mul(op1, op2);
	kernel_neon_end();
}

static int polyval_arm64_setkey(struct crypto_shash *tfm,
			const u8 *key, unsigned int keylen)
{
	struct polyval_tfm_ctx *tctx = crypto_shash_ctx(tfm);
	int i;

	if (keylen != POLYVAL_BLOCK_SIZE)
		return -EINVAL;

	memcpy(tctx->key_powers[NUM_KEY_POWERS-1], key, POLYVAL_BLOCK_SIZE);

	for (i = NUM_KEY_POWERS-2; i >= 0; i--) {
		memcpy(tctx->key_powers[i], key, POLYVAL_BLOCK_SIZE);
		internal_polyval_mul(tctx->key_powers[i],
				     tctx->key_powers[i+1]);
	}

	return 0;
}

static int polyval_arm64_init(struct shash_desc *desc)
{
	struct polyval_desc_ctx *dctx = shash_desc_ctx(desc);

	memset(dctx, 0, sizeof(*dctx));

	return 0;
}

static int polyval_arm64_update(struct shash_desc *desc,
			 const u8 *src, unsigned int srclen)
{
	struct polyval_desc_ctx *dctx = shash_desc_ctx(desc);
	const struct polyval_tfm_ctx *tctx = crypto_shash_ctx(desc->tfm);
	unsigned int nblocks;

	do {
		/* allow rescheduling every 4K bytes */
		nblocks = min(srclen, 4096U) / POLYVAL_BLOCK_SIZE;
		internal_polyval_update(tctx, src, nblocks, dctx->buffer);
		srclen -= nblocks * POLYVAL_BLOCK_SIZE;
		src += nblocks * POLYVAL_BLOCK_SIZE;
	} while (srclen >= POLYVAL_BLOCK_SIZE);

	return srclen;
}

static int polyval_arm64_finup(struct shash_desc *desc, const u8 *src,
			       unsigned int len, u8 *dst)
{
	struct polyval_desc_ctx *dctx = shash_desc_ctx(desc);
	const struct polyval_tfm_ctx *tctx = crypto_shash_ctx(desc->tfm);

	if (len) {
		crypto_xor(dctx->buffer, src, len);
		internal_polyval_mul(dctx->buffer,
				     tctx->key_powers[NUM_KEY_POWERS-1]);
	}

	memcpy(dst, dctx->buffer, POLYVAL_BLOCK_SIZE);

	return 0;
}

static struct shash_alg polyval_alg = {
	.digestsize	= POLYVAL_DIGEST_SIZE,
	.init		= polyval_arm64_init,
	.update		= polyval_arm64_update,
	.finup		= polyval_arm64_finup,
	.setkey		= polyval_arm64_setkey,
	.descsize	= sizeof(struct polyval_desc_ctx),
	.base		= {
		.cra_name		= "polyval",
		.cra_driver_name	= "polyval-ce",
		.cra_priority		= 200,
		.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
		.cra_blocksize		= POLYVAL_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct polyval_tfm_ctx),
		.cra_module		= THIS_MODULE,
	},
};

static int __init polyval_ce_mod_init(void)
{
	return crypto_register_shash(&polyval_alg);
}

static void __exit polyval_ce_mod_exit(void)
{
	crypto_unregister_shash(&polyval_alg);
}

module_cpu_feature_match(PMULL, polyval_ce_mod_init)
module_exit(polyval_ce_mod_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("POLYVAL hash function accelerated by ARMv8 Crypto Extensions");
MODULE_ALIAS_CRYPTO("polyval");
MODULE_ALIAS_CRYPTO("polyval-ce");
