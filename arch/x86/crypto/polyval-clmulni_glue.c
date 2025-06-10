// SPDX-License-Identifier: GPL-2.0-only
/*
 * Glue code for POLYVAL using PCMULQDQ-NI
 *
 * Copyright (c) 2007 Nokia Siemens Networks - Mikko Herranen <mh1@iki.fi>
 * Copyright (c) 2009 Intel Corp.
 *   Author: Huang Ying <ying.huang@intel.com>
 * Copyright 2021 Google LLC
 */

/*
 * Glue code based on ghash-clmulni-intel_glue.c.
 *
 * This implementation of POLYVAL uses montgomery multiplication
 * accelerated by PCLMULQDQ-NI to implement the finite field
 * operations.
 */

#include <asm/cpu_device_id.h>
#include <asm/fpu/api.h>
#include <crypto/internal/hash.h>
#include <crypto/polyval.h>
#include <crypto/utils.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>

#define POLYVAL_ALIGN	16
#define POLYVAL_ALIGN_ATTR __aligned(POLYVAL_ALIGN)
#define POLYVAL_ALIGN_EXTRA ((POLYVAL_ALIGN - 1) & ~(CRYPTO_MINALIGN - 1))
#define POLYVAL_CTX_SIZE (sizeof(struct polyval_tfm_ctx) + POLYVAL_ALIGN_EXTRA)
#define NUM_KEY_POWERS	8

struct polyval_tfm_ctx {
	/*
	 * These powers must be in the order h^8, ..., h^1.
	 */
	u8 key_powers[NUM_KEY_POWERS][POLYVAL_BLOCK_SIZE] POLYVAL_ALIGN_ATTR;
};

struct polyval_desc_ctx {
	u8 buffer[POLYVAL_BLOCK_SIZE];
};

asmlinkage void clmul_polyval_update(const struct polyval_tfm_ctx *keys,
	const u8 *in, size_t nblocks, u8 *accumulator);
asmlinkage void clmul_polyval_mul(u8 *op1, const u8 *op2);

static inline struct polyval_tfm_ctx *polyval_tfm_ctx(struct crypto_shash *tfm)
{
	return PTR_ALIGN(crypto_shash_ctx(tfm), POLYVAL_ALIGN);
}

static void internal_polyval_update(const struct polyval_tfm_ctx *keys,
	const u8 *in, size_t nblocks, u8 *accumulator)
{
	kernel_fpu_begin();
	clmul_polyval_update(keys, in, nblocks, accumulator);
	kernel_fpu_end();
}

static void internal_polyval_mul(u8 *op1, const u8 *op2)
{
	kernel_fpu_begin();
	clmul_polyval_mul(op1, op2);
	kernel_fpu_end();
}

static int polyval_x86_setkey(struct crypto_shash *tfm,
			const u8 *key, unsigned int keylen)
{
	struct polyval_tfm_ctx *tctx = polyval_tfm_ctx(tfm);
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

static int polyval_x86_init(struct shash_desc *desc)
{
	struct polyval_desc_ctx *dctx = shash_desc_ctx(desc);

	memset(dctx, 0, sizeof(*dctx));

	return 0;
}

static int polyval_x86_update(struct shash_desc *desc,
			 const u8 *src, unsigned int srclen)
{
	struct polyval_desc_ctx *dctx = shash_desc_ctx(desc);
	const struct polyval_tfm_ctx *tctx = polyval_tfm_ctx(desc->tfm);
	unsigned int nblocks;

	do {
		/* Allow rescheduling every 4K bytes. */
		nblocks = min(srclen, 4096U) / POLYVAL_BLOCK_SIZE;
		internal_polyval_update(tctx, src, nblocks, dctx->buffer);
		srclen -= nblocks * POLYVAL_BLOCK_SIZE;
		src += nblocks * POLYVAL_BLOCK_SIZE;
	} while (srclen >= POLYVAL_BLOCK_SIZE);

	return srclen;
}

static int polyval_x86_finup(struct shash_desc *desc, const u8 *src,
			     unsigned int len, u8 *dst)
{
	struct polyval_desc_ctx *dctx = shash_desc_ctx(desc);
	const struct polyval_tfm_ctx *tctx = polyval_tfm_ctx(desc->tfm);

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
	.init		= polyval_x86_init,
	.update		= polyval_x86_update,
	.finup		= polyval_x86_finup,
	.setkey		= polyval_x86_setkey,
	.descsize	= sizeof(struct polyval_desc_ctx),
	.base		= {
		.cra_name		= "polyval",
		.cra_driver_name	= "polyval-clmulni",
		.cra_priority		= 200,
		.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
		.cra_blocksize		= POLYVAL_BLOCK_SIZE,
		.cra_ctxsize		= POLYVAL_CTX_SIZE,
		.cra_module		= THIS_MODULE,
	},
};

__maybe_unused static const struct x86_cpu_id pcmul_cpu_id[] = {
	X86_MATCH_FEATURE(X86_FEATURE_PCLMULQDQ, NULL),
	{}
};
MODULE_DEVICE_TABLE(x86cpu, pcmul_cpu_id);

static int __init polyval_clmulni_mod_init(void)
{
	if (!x86_match_cpu(pcmul_cpu_id))
		return -ENODEV;

	if (!boot_cpu_has(X86_FEATURE_AVX))
		return -ENODEV;

	return crypto_register_shash(&polyval_alg);
}

static void __exit polyval_clmulni_mod_exit(void)
{
	crypto_unregister_shash(&polyval_alg);
}

module_init(polyval_clmulni_mod_init);
module_exit(polyval_clmulni_mod_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("POLYVAL hash function accelerated by PCLMULQDQ-NI");
MODULE_ALIAS_CRYPTO("polyval");
MODULE_ALIAS_CRYPTO("polyval-clmulni");
