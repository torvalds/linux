/*
 * Cryptographic API for the 842 software compression algorithm.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
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

struct crypto842_ctx {
	char wmem[SW842_MEM_COMPRESS];	/* working memory for compress */
};

static int crypto842_compress(struct crypto_tfm *tfm,
			      const u8 *src, unsigned int slen,
			      u8 *dst, unsigned int *dlen)
{
	struct crypto842_ctx *ctx = crypto_tfm_ctx(tfm);

	return sw842_compress(src, slen, dst, dlen, ctx->wmem);
}

static int crypto842_decompress(struct crypto_tfm *tfm,
				const u8 *src, unsigned int slen,
				u8 *dst, unsigned int *dlen)
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
	.cra_u			= { .compress = {
	.coa_compress		= crypto842_compress,
	.coa_decompress		= crypto842_decompress } }
};

static int __init crypto842_mod_init(void)
{
	return crypto_register_alg(&alg);
}
module_init(crypto842_mod_init);

static void __exit crypto842_mod_exit(void)
{
	crypto_unregister_alg(&alg);
}
module_exit(crypto842_mod_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("842 Software Compression Algorithm");
MODULE_ALIAS_CRYPTO("842");
MODULE_ALIAS_CRYPTO("842-generic");
MODULE_AUTHOR("Dan Streetman <ddstreet@ieee.org>");
