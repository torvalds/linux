// SPDX-License-Identifier: GPL-2.0-only
/*
 * Cryptographic API.
 *
 * Copyright (c) 2013 Chanho Min <chanho.min@lge.com>
 */
#include <crypto/internal/scompress.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/lz4.h>

struct lz4hc_ctx {
	void *lz4hc_comp_mem;
};

static void *lz4hc_alloc_ctx(void)
{
	void *ctx;

	ctx = vmalloc(LZ4HC_MEM_COMPRESS);
	if (!ctx)
		return ERR_PTR(-ENOMEM);

	return ctx;
}

static void lz4hc_free_ctx(void *ctx)
{
	vfree(ctx);
}

static int __lz4hc_compress_crypto(const u8 *src, unsigned int slen,
				   u8 *dst, unsigned int *dlen, void *ctx)
{
	int out_len = LZ4_compress_HC(src, dst, slen,
		*dlen, LZ4HC_DEFAULT_CLEVEL, ctx);

	if (!out_len)
		return -EINVAL;

	*dlen = out_len;
	return 0;
}

static int lz4hc_scompress(struct crypto_scomp *tfm, const u8 *src,
			   unsigned int slen, u8 *dst, unsigned int *dlen,
			   void *ctx)
{
	return __lz4hc_compress_crypto(src, slen, dst, dlen, ctx);
}

static int __lz4hc_decompress_crypto(const u8 *src, unsigned int slen,
				     u8 *dst, unsigned int *dlen, void *ctx)
{
	int out_len = LZ4_decompress_safe(src, dst, slen, *dlen);

	if (out_len < 0)
		return -EINVAL;

	*dlen = out_len;
	return 0;
}

static int lz4hc_sdecompress(struct crypto_scomp *tfm, const u8 *src,
			     unsigned int slen, u8 *dst, unsigned int *dlen,
			     void *ctx)
{
	return __lz4hc_decompress_crypto(src, slen, dst, dlen, NULL);
}

static struct scomp_alg scomp = {
	.alloc_ctx		= lz4hc_alloc_ctx,
	.free_ctx		= lz4hc_free_ctx,
	.compress		= lz4hc_scompress,
	.decompress		= lz4hc_sdecompress,
	.base			= {
		.cra_name	= "lz4hc",
		.cra_driver_name = "lz4hc-scomp",
		.cra_module	 = THIS_MODULE,
	}
};

static int __init lz4hc_mod_init(void)
{
	return crypto_register_scomp(&scomp);
}

static void __exit lz4hc_mod_fini(void)
{
	crypto_unregister_scomp(&scomp);
}

subsys_initcall(lz4hc_mod_init);
module_exit(lz4hc_mod_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("LZ4HC Compression Algorithm");
MODULE_ALIAS_CRYPTO("lz4hc");
