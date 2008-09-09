/* 
 * Cryptographic API.
 *
 * CRC32C chksum
 *
 * This module file is a wrapper to invoke the lib/crc32c routines.
 *
 * Copyright (c) 2008 Herbert Xu <herbert@gondor.apana.org.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option) 
 * any later version.
 *
 */

#include <crypto/internal/hash.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/crc32c.h>
#include <linux/kernel.h>

#define CHKSUM_BLOCK_SIZE	1
#define CHKSUM_DIGEST_SIZE	4

struct chksum_ctx {
	u32 key;
};

struct chksum_desc_ctx {
	u32 crc;
};

/*
 * Steps through buffer one byte at at time, calculates reflected 
 * crc using table.
 */

static int chksum_init(struct shash_desc *desc)
{
	struct chksum_ctx *mctx = crypto_shash_ctx(desc->tfm);
	struct chksum_desc_ctx *ctx = shash_desc_ctx(desc);

	ctx->crc = mctx->key;

	return 0;
}

/*
 * Setting the seed allows arbitrary accumulators and flexible XOR policy
 * If your algorithm starts with ~0, then XOR with ~0 before you set
 * the seed.
 */
static int chksum_setkey(struct crypto_shash *tfm, const u8 *key,
			 unsigned int keylen)
{
	struct chksum_ctx *mctx = crypto_shash_ctx(tfm);

	if (keylen != sizeof(mctx->key)) {
		crypto_shash_set_flags(tfm, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}
	mctx->key = le32_to_cpu(*(__le32 *)key);
	return 0;
}

static int chksum_update(struct shash_desc *desc, const u8 *data,
			 unsigned int length)
{
	struct chksum_desc_ctx *ctx = shash_desc_ctx(desc);

	ctx->crc = crc32c(ctx->crc, data, length);
	return 0;
}

static int chksum_final(struct shash_desc *desc, u8 *out)
{
	struct chksum_desc_ctx *ctx = shash_desc_ctx(desc);

	*(__le32 *)out = ~cpu_to_le32p(&ctx->crc);
	return 0;
}

static int __chksum_finup(u32 *crcp, const u8 *data, unsigned int len, u8 *out)
{
	*(__le32 *)out = ~cpu_to_le32(crc32c(*crcp, data, len));
	return 0;
}

static int chksum_finup(struct shash_desc *desc, const u8 *data,
			unsigned int len, u8 *out)
{
	struct chksum_desc_ctx *ctx = shash_desc_ctx(desc);

	return __chksum_finup(&ctx->crc, data, len, out);
}

static int chksum_digest(struct shash_desc *desc, const u8 *data,
			 unsigned int length, u8 *out)
{
	struct chksum_ctx *mctx = crypto_shash_ctx(desc->tfm);

	return __chksum_finup(&mctx->key, data, length, out);
}

static int crc32c_cra_init(struct crypto_tfm *tfm)
{
	struct chksum_ctx *mctx = crypto_tfm_ctx(tfm);

	mctx->key = ~0;
	return 0;
}

static struct shash_alg alg = {
	.digestsize		=	CHKSUM_DIGEST_SIZE,
	.setkey			=	chksum_setkey,
	.init   		= 	chksum_init,
	.update 		=	chksum_update,
	.final  		=	chksum_final,
	.finup  		=	chksum_finup,
	.digest  		=	chksum_digest,
	.descsize		=	sizeof(struct chksum_desc_ctx),
	.base			=	{
		.cra_name		=	"crc32c",
		.cra_driver_name	=	"crc32c-generic",
		.cra_priority		=	100,
		.cra_blocksize		=	CHKSUM_BLOCK_SIZE,
		.cra_alignmask		=	3,
		.cra_ctxsize		=	sizeof(struct chksum_ctx),
		.cra_module		=	THIS_MODULE,
		.cra_init		=	crc32c_cra_init,
	}
};

static int __init crc32c_mod_init(void)
{
	return crypto_register_shash(&alg);
}

static void __exit crc32c_mod_fini(void)
{
	crypto_unregister_shash(&alg);
}

module_init(crc32c_mod_init);
module_exit(crc32c_mod_fini);

MODULE_AUTHOR("Clay Haapala <chaapala@cisco.com>");
MODULE_DESCRIPTION("CRC32c (Castagnoli) calculations wrapper for lib/crc32c");
MODULE_LICENSE("GPL");
