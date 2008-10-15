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
	u32 crc;
	u32 key;
};

/*
 * Steps through buffer one byte at at time, calculates reflected 
 * crc using table.
 */

static void chksum_init(struct crypto_tfm *tfm)
{
	struct chksum_ctx *mctx = crypto_tfm_ctx(tfm);

	mctx->crc = mctx->key;
}

/*
 * Setting the seed allows arbitrary accumulators and flexible XOR policy
 * If your algorithm starts with ~0, then XOR with ~0 before you set
 * the seed.
 */
static int chksum_setkey(struct crypto_tfm *tfm, const u8 *key,
			 unsigned int keylen)
{
	struct chksum_ctx *mctx = crypto_tfm_ctx(tfm);

	if (keylen != sizeof(mctx->crc)) {
		tfm->crt_flags |= CRYPTO_TFM_RES_BAD_KEY_LEN;
		return -EINVAL;
	}
	mctx->key = le32_to_cpu(*(__le32 *)key);
	return 0;
}

static void chksum_update(struct crypto_tfm *tfm, const u8 *data,
			  unsigned int length)
{
	struct chksum_ctx *mctx = crypto_tfm_ctx(tfm);

	mctx->crc = crc32c(mctx->crc, data, length);
}

static void chksum_final(struct crypto_tfm *tfm, u8 *out)
{
	struct chksum_ctx *mctx = crypto_tfm_ctx(tfm);
	
	*(__le32 *)out = ~cpu_to_le32(mctx->crc);
}

static int crc32c_cra_init_old(struct crypto_tfm *tfm)
{
	struct chksum_ctx *mctx = crypto_tfm_ctx(tfm);

	mctx->key = ~0;
	return 0;
}

static struct crypto_alg old_alg = {
	.cra_name	=	"crc32c",
	.cra_flags	=	CRYPTO_ALG_TYPE_DIGEST,
	.cra_blocksize	=	CHKSUM_BLOCK_SIZE,
	.cra_ctxsize	=	sizeof(struct chksum_ctx),
	.cra_module	=	THIS_MODULE,
	.cra_list	=	LIST_HEAD_INIT(old_alg.cra_list),
	.cra_init	=	crc32c_cra_init_old,
	.cra_u		=	{
		.digest = {
			 .dia_digestsize=	CHKSUM_DIGEST_SIZE,
			 .dia_setkey	=	chksum_setkey,
			 .dia_init   	= 	chksum_init,
			 .dia_update 	=	chksum_update,
			 .dia_final  	=	chksum_final
		 }
	}
};

/*
 * Setting the seed allows arbitrary accumulators and flexible XOR policy
 * If your algorithm starts with ~0, then XOR with ~0 before you set
 * the seed.
 */
static int crc32c_setkey(struct crypto_ahash *hash, const u8 *key,
			 unsigned int keylen)
{
	u32 *mctx = crypto_ahash_ctx(hash);

	if (keylen != sizeof(u32)) {
		crypto_ahash_set_flags(hash, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}
	*mctx = le32_to_cpup((__le32 *)key);
	return 0;
}

static int crc32c_init(struct ahash_request *req)
{
	u32 *mctx = crypto_ahash_ctx(crypto_ahash_reqtfm(req));
	u32 *crcp = ahash_request_ctx(req);

	*crcp = *mctx;
	return 0;
}

static int crc32c_update(struct ahash_request *req)
{
	struct crypto_hash_walk walk;
	u32 *crcp = ahash_request_ctx(req);
	u32 crc = *crcp;
	int nbytes;

	for (nbytes = crypto_hash_walk_first(req, &walk); nbytes;
	     nbytes = crypto_hash_walk_done(&walk, 0))
		crc = crc32c(crc, walk.data, nbytes);

	*crcp = crc;
	return 0;
}

static int crc32c_final(struct ahash_request *req)
{
	u32 *crcp = ahash_request_ctx(req);
	
	*(__le32 *)req->result = ~cpu_to_le32p(crcp);
	return 0;
}

static int crc32c_digest(struct ahash_request *req)
{
	struct crypto_hash_walk walk;
	u32 *mctx = crypto_ahash_ctx(crypto_ahash_reqtfm(req));
	u32 crc = *mctx;
	int nbytes;

	for (nbytes = crypto_hash_walk_first(req, &walk); nbytes;
	     nbytes = crypto_hash_walk_done(&walk, 0))
		crc = crc32c(crc, walk.data, nbytes);

	*(__le32 *)req->result = ~cpu_to_le32(crc);
	return 0;
}

static int crc32c_cra_init(struct crypto_tfm *tfm)
{
	u32 *key = crypto_tfm_ctx(tfm);

	*key = ~0;

	tfm->crt_ahash.reqsize = sizeof(u32);

	return 0;
}

static struct crypto_alg alg = {
	.cra_name		=	"crc32c",
	.cra_driver_name	=	"crc32c-generic",
	.cra_priority		=	100,
	.cra_flags		=	CRYPTO_ALG_TYPE_AHASH,
	.cra_blocksize		=	CHKSUM_BLOCK_SIZE,
	.cra_alignmask		=	3,
	.cra_ctxsize		=	sizeof(u32),
	.cra_module		=	THIS_MODULE,
	.cra_list		=	LIST_HEAD_INIT(alg.cra_list),
	.cra_init		=	crc32c_cra_init,
	.cra_type		=	&crypto_ahash_type,
	.cra_u			=	{
		.ahash = {
			 .digestsize	=	CHKSUM_DIGEST_SIZE,
			 .setkey	=	crc32c_setkey,
			 .init   	= 	crc32c_init,
			 .update 	=	crc32c_update,
			 .final  	=	crc32c_final,
			 .digest  	=	crc32c_digest,
		 }
	}
};

static int __init crc32c_mod_init(void)
{
	int err;

	err = crypto_register_alg(&old_alg);
	if (err)
		return err;

	err = crypto_register_alg(&alg);
	if (err)
		crypto_unregister_alg(&old_alg);

	return err;
}

static void __exit crc32c_mod_fini(void)
{
	crypto_unregister_alg(&alg);
	crypto_unregister_alg(&old_alg);
}

module_init(crc32c_mod_init);
module_exit(crc32c_mod_fini);

MODULE_AUTHOR("Clay Haapala <chaapala@cisco.com>");
MODULE_DESCRIPTION("CRC32c (Castagnoli) calculations wrapper for lib/crc32c");
MODULE_LICENSE("GPL");
