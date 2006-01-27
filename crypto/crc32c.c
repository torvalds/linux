/* 
 * Cryptographic API.
 *
 * CRC32C chksum
 *
 * This module file is a wrapper to invoke the lib/crc32c routines.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option) 
 * any later version.
 *
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/crypto.h>
#include <linux/crc32c.h>
#include <linux/types.h>
#include <asm/byteorder.h>

#define CHKSUM_BLOCK_SIZE	32
#define CHKSUM_DIGEST_SIZE	4

struct chksum_ctx {
	u32 crc;
};

/*
 * Steps through buffer one byte at at time, calculates reflected 
 * crc using table.
 */

static void chksum_init(void *ctx)
{
	struct chksum_ctx *mctx = ctx;

	mctx->crc = ~(u32)0;			/* common usage */
}

/*
 * Setting the seed allows arbitrary accumulators and flexible XOR policy
 * If your algorithm starts with ~0, then XOR with ~0 before you set
 * the seed.
 */
static int chksum_setkey(void *ctx, const u8 *key, unsigned int keylen,
	                  u32 *flags)
{
	struct chksum_ctx *mctx = ctx;

	if (keylen != sizeof(mctx->crc)) {
		if (flags)
			*flags = CRYPTO_TFM_RES_BAD_KEY_LEN;
		return -EINVAL;
	}
	mctx->crc = __cpu_to_le32(*(u32 *)key);
	return 0;
}

static void chksum_update(void *ctx, const u8 *data, unsigned int length)
{
	struct chksum_ctx *mctx = ctx;
	u32 mcrc;

	mcrc = crc32c(mctx->crc, data, (size_t)length);

	mctx->crc = mcrc;
}

static void chksum_final(void *ctx, u8 *out)
{
	struct chksum_ctx *mctx = ctx;
	u32 mcrc = (mctx->crc ^ ~(u32)0);
	
	*(u32 *)out = __le32_to_cpu(mcrc);
}

static struct crypto_alg alg = {
	.cra_name	=	"crc32c",
	.cra_flags	=	CRYPTO_ALG_TYPE_DIGEST,
	.cra_blocksize	=	CHKSUM_BLOCK_SIZE,
	.cra_ctxsize	=	sizeof(struct chksum_ctx),
	.cra_module	=	THIS_MODULE,
	.cra_list	=	LIST_HEAD_INIT(alg.cra_list),
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

static int __init init(void)
{
	return crypto_register_alg(&alg);
}

static void __exit fini(void)
{
	crypto_unregister_alg(&alg);
}

module_init(init);
module_exit(fini);

MODULE_AUTHOR("Clay Haapala <chaapala@cisco.com>");
MODULE_DESCRIPTION("CRC32c (Castagnoli) calculations wrapper for lib/crc32c");
MODULE_LICENSE("GPL");
