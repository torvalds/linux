// SPDX-License-Identifier: GPL-2.0
/*
 * Cryptographic API.
 *
 * s390 implementation of the GHASH algorithm for GCM (Galois/Counter Mode).
 *
 * Copyright IBM Corp. 2011
 * Author(s): Gerald Schaefer <gerald.schaefer@de.ibm.com>
 */

#include <asm/cpacf.h>
#include <crypto/ghash.h>
#include <crypto/internal/hash.h>
#include <linux/cpufeature.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>

struct s390_ghash_ctx {
	u8 key[GHASH_BLOCK_SIZE];
};

struct s390_ghash_desc_ctx {
	u8 icv[GHASH_BLOCK_SIZE];
	u8 key[GHASH_BLOCK_SIZE];
};

static int ghash_init(struct shash_desc *desc)
{
	struct s390_ghash_ctx *ctx = crypto_shash_ctx(desc->tfm);
	struct s390_ghash_desc_ctx *dctx = shash_desc_ctx(desc);

	memset(dctx, 0, sizeof(*dctx));
	memcpy(dctx->key, ctx->key, GHASH_BLOCK_SIZE);

	return 0;
}

static int ghash_setkey(struct crypto_shash *tfm,
			const u8 *key, unsigned int keylen)
{
	struct s390_ghash_ctx *ctx = crypto_shash_ctx(tfm);

	if (keylen != GHASH_BLOCK_SIZE)
		return -EINVAL;

	memcpy(ctx->key, key, GHASH_BLOCK_SIZE);

	return 0;
}

static int ghash_update(struct shash_desc *desc,
			 const u8 *src, unsigned int srclen)
{
	struct s390_ghash_desc_ctx *dctx = shash_desc_ctx(desc);
	unsigned int n;

	n = srclen & ~(GHASH_BLOCK_SIZE - 1);
	cpacf_kimd(CPACF_KIMD_GHASH, dctx, src, n);
	return srclen - n;
}

static void ghash_flush(struct s390_ghash_desc_ctx *dctx, const u8 *src,
			unsigned int len)
{
	if (len) {
		u8 buf[GHASH_BLOCK_SIZE] = {};

		memcpy(buf, src, len);
		cpacf_kimd(CPACF_KIMD_GHASH, dctx, buf, GHASH_BLOCK_SIZE);
		memzero_explicit(buf, sizeof(buf));
	}
}

static int ghash_finup(struct shash_desc *desc, const u8 *src,
		       unsigned int len, u8 *dst)
{
	struct s390_ghash_desc_ctx *dctx = shash_desc_ctx(desc);

	ghash_flush(dctx, src, len);
	memcpy(dst, dctx->icv, GHASH_BLOCK_SIZE);
	return 0;
}

static int ghash_export(struct shash_desc *desc, void *out)
{
	struct s390_ghash_desc_ctx *dctx = shash_desc_ctx(desc);

	memcpy(out, dctx->icv, GHASH_DIGEST_SIZE);
	return 0;
}

static int ghash_import(struct shash_desc *desc, const void *in)
{
	struct s390_ghash_ctx *ctx = crypto_shash_ctx(desc->tfm);
	struct s390_ghash_desc_ctx *dctx = shash_desc_ctx(desc);

	memcpy(dctx->icv, in, GHASH_DIGEST_SIZE);
	memcpy(dctx->key, ctx->key, GHASH_BLOCK_SIZE);
	return 0;
}

static struct shash_alg ghash_alg = {
	.digestsize	= GHASH_DIGEST_SIZE,
	.init		= ghash_init,
	.update		= ghash_update,
	.finup		= ghash_finup,
	.setkey		= ghash_setkey,
	.export		= ghash_export,
	.import		= ghash_import,
	.statesize	= sizeof(struct ghash_desc_ctx),
	.descsize	= sizeof(struct s390_ghash_desc_ctx),
	.base		= {
		.cra_name		= "ghash",
		.cra_driver_name	= "ghash-s390",
		.cra_priority		= 300,
		.cra_flags		= CRYPTO_AHASH_ALG_BLOCK_ONLY,
		.cra_blocksize		= GHASH_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct s390_ghash_ctx),
		.cra_module		= THIS_MODULE,
	},
};

static int __init ghash_mod_init(void)
{
	if (!cpacf_query_func(CPACF_KIMD, CPACF_KIMD_GHASH))
		return -ENODEV;

	return crypto_register_shash(&ghash_alg);
}

static void __exit ghash_mod_exit(void)
{
	crypto_unregister_shash(&ghash_alg);
}

module_cpu_feature_match(S390_CPU_FEATURE_MSA, ghash_mod_init);
module_exit(ghash_mod_exit);

MODULE_ALIAS_CRYPTO("ghash");

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("GHASH hash function, s390 implementation");
