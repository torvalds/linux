// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Cryptographic API for the 842 software compression algorithm.
 *
 * Copyright (C) IBM Corporation, 2011-2015
 *
 * Original Authors: Robert Jennings <rcj@linux.vnet.ibm.com>
 *                   Seth Jennings <sjenning@linux.vnet.ibm.com>
 *
 * Rewrite: Dan Streetman <ddstreet@ieee.org>
 *
 * This is the software implementation of compression and decompression using
 * the 842 format.  This uses the software 842 library at lib/842/ which is
 * only a reference implementation, and is very, very slow as compared to other
 * software compressors.  You probably do not want to use this software
 * compression.  If you have access to the PowerPC 842 compression hardware, you
 * want to use the 842 hardware compression interface, which is at:
 * drivers/crypto/nx/nx-842-crypto.c
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/crypto.h>
#include <linux/sw842.h>
#include <crypto/internal/scompress.h>

struct crypto842_ctx {
	void *wmem;	/* working memory for compress */
};

static void *crypto842_alloc_ctx(struct crypto_scomp *tfm)
{
	void *ctx;

	ctx = kmalloc(SW842_MEM_COMPRESS, GFP_KERNEL);
	if (!ctx)
		return ERR_PTR(-ENOMEM);

	return ctx;
}

static int crypto842_init(struct crypto_tfm *tfm)
{
	struct crypto842_ctx *ctx = crypto_tfm_ctx(tfm);

	ctx->wmem = crypto842_alloc_ctx(NULL);
	if (IS_ERR(ctx->wmem))
		return -ENOMEM;

	return 0;
}

static void crypto842_free_ctx(struct crypto_scomp *tfm, void *ctx)
{
	kfree(ctx);
}

static void crypto842_exit(struct crypto_tfm *tfm)
{
	struct crypto842_ctx *ctx = crypto_tfm_ctx(tfm);

	crypto842_free_ctx(NULL, ctx->wmem);
}

static int crypto842_compress(struct crypto_tfm *tfm,
			      const u8 *src, unsigned int slen,
			      u8 *dst, unsigned int *dlen)
{
	struct crypto842_ctx *ctx = crypto_tfm_ctx(tfm);

	return sw842_compress(src, slen, dst, dlen, ctx->wmem);
}

static int crypto842_scompress(struct crypto_scomp *tfm,
			       const u8 *src, unsigned int slen,
			       u8 *dst, unsigned int *dlen, void *ctx)
{
	return sw842_compress(src, slen, dst, dlen, ctx);
}

static int crypto842_decompress(struct crypto_tfm *tfm,
				const u8 *src, unsigned int slen,
				u8 *dst, unsigned int *dlen)
{
	return sw842_decompress(src, slen, dst, dlen);
}

static int crypto842_sdecompress(struct crypto_scomp *tfm,
				 const u8 *src, unsigned int slen,
				 u8 *dst, unsigned int *dlen, void *ctx)
{
	return sw842_decompress(src, slen, dst, dlen);
}

static struct crypto_alg alg = {
	.cra_name		= "842",
	.cra_driver_name	= "842-generic",
	.cra_priority		= 100,
	.cra_flags		= CRYPTO_ALG_TYPE_COMPRESS,
	.cra_ctxsize		= sizeof(struct crypto842_ctx),
	.cra_module		= THIS_MODULE,
	.cra_init		= crypto842_init,
	.cra_exit		= crypto842_exit,
	.cra_u			= { .compress = {
	.coa_compress		= crypto842_compress,
	.coa_decompress		= crypto842_decompress } }
};

static struct scomp_alg scomp = {
	.alloc_ctx		= crypto842_alloc_ctx,
	.free_ctx		= crypto842_free_ctx,
	.compress		= crypto842_scompress,
	.decompress		= crypto842_sdecompress,
	.base			= {
		.cra_name	= "842",
		.cra_driver_name = "842-scomp",
		.cra_priority	 = 100,
		.cra_module	 = THIS_MODULE,
	}
};

static int __init crypto842_mod_init(void)
{
	int ret;

	ret = crypto_register_alg(&alg);
	if (ret)
		return ret;

	ret = crypto_register_scomp(&scomp);
	if (ret) {
		crypto_unregister_alg(&alg);
		return ret;
	}

	return ret;
}
subsys_initcall(crypto842_mod_init);

static void __exit crypto842_mod_exit(void)
{
	crypto_unregister_alg(&alg);
	crypto_unregister_scomp(&scomp);
}
module_exit(crypto842_mod_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("842 Software Compression Algorithm");
MODULE_ALIAS_CRYPTO("842");
MODULE_ALIAS_CRYPTO("842-generic");
MODULE_AUTHOR("Dan Streetman <ddstreet@ieee.org>");
