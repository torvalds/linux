// SPDX-License-Identifier: GPL-2.0
/*
 * Cryptographic API.
 *
 * s390 implementation of the GHASH algorithm for GCM (Galois/Counter Mode).
 *
 * Copyright IBM Corp. 2011
 * Author(s): Gerald Schaefer <gerald.schaefer@de.ibm.com>
 */

#include <crypto/internal/hash.h>
#include <linux/module.h>
#include <linux/cpufeature.h>
#include <asm/cpacf.h>

#define GHASH_BLOCK_SIZE	16
#define GHASH_DIGEST_SIZE	16

struct ghash_ctx {
	u8 key[GHASH_BLOCK_SIZE];
};

struct ghash_desc_ctx {
	u8 icv[GHASH_BLOCK_SIZE];
	u8 key[GHASH_BLOCK_SIZE];
	u8 buffer[GHASH_BLOCK_SIZE];
	u32 bytes;
};

static int ghash_init(struct shash_desc *desc)
{
	struct ghash_desc_ctx *dctx = shash_desc_ctx(desc);
	struct ghash_ctx *ctx = crypto_shash_ctx(desc->tfm);

	memset(dctx, 0, sizeof(*dctx));
	memcpy(dctx->key, ctx->key, GHASH_BLOCK_SIZE);

	return 0;
}

static int ghash_setkey(struct crypto_shash *tfm,
			const u8 *key, unsigned int keylen)
{
	struct ghash_ctx *ctx = crypto_shash_ctx(tfm);

	if (keylen != GHASH_BLOCK_SIZE) {
		crypto_shash_set_flags(tfm, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}

	memcpy(ctx->key, key, GHASH_BLOCK_SIZE);

	return 0;
}

static int ghash_update(struct shash_desc *desc,
			 const u8 *src, unsigned int srclen)
{
	struct ghash_desc_ctx *dctx = shash_desc_ctx(desc);
	unsigned int n;
	u8 *buf = dctx->buffer;

	if (dctx->bytes) {
		u8 *pos = buf + (GHASH_BLOCK_SIZE - dctx->bytes);

		n = min(srclen, dctx->bytes);
		dctx->bytes -= n;
		srclen -= n;

		memcpy(pos, src, n);
		src += n;

		if (!dctx->bytes) {
			cpacf_kimd(CPACF_KIMD_GHASH, dctx, buf,
				   GHASH_BLOCK_SIZE);
		}
	}

	n = srclen & ~(GHASH_BLOCK_SIZE - 1);
	if (n) {
		cpacf_kimd(CPACF_KIMD_GHASH, dctx, src, n);
		src += n;
		srclen -= n;
	}

	if (srclen) {
		dctx->bytes = GHASH_BLOCK_SIZE - srclen;
		memcpy(buf, src, srclen);
	}

	return 0;
}

static int ghash_flush(struct ghash_desc_ctx *dctx)
{
	u8 *buf = dctx->buffer;

	if (dctx->bytes) {
		u8 *pos = buf + (GHASH_BLOCK_SIZE - dctx->bytes);

		memset(pos, 0, dctx->bytes);
		cpacf_kimd(CPACF_KIMD_GHASH, dctx, buf, GHASH_BLOCK_SIZE);
		dctx->bytes = 0;
	}

	return 0;
}

static int ghash_final(struct shash_desc *desc, u8 *dst)
{
	struct ghash_desc_ctx *dctx = shash_desc_ctx(desc);
	int ret;

	ret = ghash_flush(dctx);
	if (!ret)
		memcpy(dst, dctx->icv, GHASH_BLOCK_SIZE);
	return ret;
}

static struct shash_alg ghash_alg = {
	.digestsize	= GHASH_DIGEST_SIZE,
	.init		= ghash_init,
	.update		= ghash_update,
	.final		= ghash_final,
	.setkey		= ghash_setkey,
	.descsize	= sizeof(struct ghash_desc_ctx),
	.base		= {
		.cra_name		= "ghash",
		.cra_driver_name	= "ghash-s390",
		.cra_priority		= 300,
		.cra_flags		= CRYPTO_ALG_TYPE_SHASH,
		.cra_blocksize		= GHASH_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct ghash_ctx),
		.cra_module		= THIS_MODULE,
	},
};

static int __init ghash_mod_init(void)
{
	if (!cpacf_query_func(CPACF_KIMD, CPACF_KIMD_GHASH))
		return -EOPNOTSUPP;

	return crypto_register_shash(&ghash_alg);
}

static void __exit ghash_mod_exit(void)
{
	crypto_unregister_shash(&ghash_alg);
}

module_cpu_feature_match(MSA, ghash_mod_init);
module_exit(ghash_mod_exit);

MODULE_ALIAS_CRYPTO("ghash");

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("GHASH Message Digest Algorithm, s390 implementation");
