// SPDX-License-Identifier: GPL-2.0-only
/*
 * Cryptographic API.
 *
 * Copyright (c) 2013 Chanho Min <chanho.min@lge.com>
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/crypto.h>
#include <linux/vmalloc.h>
#include <linux/lz4.h>
#include <crypto/internal/scompress.h>

static void *lz4_alloc_ctx(void)
{
	void *ctx;

	ctx = vmalloc(LZ4_MEM_COMPRESS);
	if (!ctx)
		return ERR_PTR(-ENOMEM);

	return ctx;
}

static void lz4_free_ctx(void *ctx)
{
	vfree(ctx);
}

static int __lz4_compress_crypto(const u8 *src, unsigned int slen,
				 u8 *dst, unsigned int *dlen, void *ctx)
{
	int out_len = LZ4_compress_default(src, dst,
		slen, *dlen, ctx);

	if (!out_len)
		return -EINVAL;

	*dlen = out_len;
	return 0;
}

static int lz4_scompress(struct crypto_scomp *tfm, const u8 *src,
			 unsigned int slen, u8 *dst, unsigned int *dlen,
			 void *ctx)
{
	return __lz4_compress_crypto(src, slen, dst, dlen, ctx);
}

static int __lz4_decompress_crypto(const u8 *src, unsigned int slen,
				   u8 *dst, unsigned int *dlen, void *ctx)
{
	int out_len = LZ4_decompress_safe(src, dst, slen, *dlen);

	if (out_len < 0)
		return -EINVAL;

	*dlen = out_len;
	return 0;
}

static int lz4_sdecompress(struct crypto_scomp *tfm, const u8 *src,
			   unsigned int slen, u8 *dst, unsigned int *dlen,
			   void *ctx)
{
	return __lz4_decompress_crypto(src, slen, dst, dlen, NULL);
}

static struct scomp_alg scomp = {
	.streams		= {
		.alloc_ctx	= lz4_alloc_ctx,
		.free_ctx	= lz4_free_ctx,
	},
	.compress		= lz4_scompress,
	.decompress		= lz4_sdecompress,
	.base			= {
		.cra_name	= "lz4",
		.cra_driver_name = "lz4-scomp",
		.cra_module	 = THIS_MODULE,
	}
};

static int __init lz4_mod_init(void)
{
	return crypto_register_scomp(&scomp);
}

static void __exit lz4_mod_fini(void)
{
	crypto_unregister_scomp(&scomp);
}

module_init(lz4_mod_init);
module_exit(lz4_mod_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("LZ4 Compression Algorithm");
MODULE_ALIAS_CRYPTO("lz4");
