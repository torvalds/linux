// SPDX-License-Identifier: GPL-2.0-only
/*
 * Cryptographic API.
 */

#include <crypto/internal/scompress.h>
#include <linux/init.h>
#include <linux/lzo.h>
#include <linux/module.h>
#include <linux/slab.h>

struct lzorle_ctx {
	void *lzorle_comp_mem;
};

static void *lzorle_alloc_ctx(void)
{
	void *ctx;

	ctx = kvmalloc(LZO1X_MEM_COMPRESS, GFP_KERNEL);
	if (!ctx)
		return ERR_PTR(-ENOMEM);

	return ctx;
}

static void lzorle_free_ctx(void *ctx)
{
	kvfree(ctx);
}

static int __lzorle_compress(const u8 *src, unsigned int slen,
			  u8 *dst, unsigned int *dlen, void *ctx)
{
	size_t tmp_len = *dlen; /* size_t(ulong) <-> uint on 64 bit */
	int err;

	err = lzorle1x_1_compress_safe(src, slen, dst, &tmp_len, ctx);

	if (err != LZO_E_OK)
		return -EINVAL;

	*dlen = tmp_len;
	return 0;
}

static int lzorle_scompress(struct crypto_scomp *tfm, const u8 *src,
			 unsigned int slen, u8 *dst, unsigned int *dlen,
			 void *ctx)
{
	return __lzorle_compress(src, slen, dst, dlen, ctx);
}

static int __lzorle_decompress(const u8 *src, unsigned int slen,
			    u8 *dst, unsigned int *dlen)
{
	int err;
	size_t tmp_len = *dlen; /* size_t(ulong) <-> uint on 64 bit */

	err = lzo1x_decompress_safe(src, slen, dst, &tmp_len);

	if (err != LZO_E_OK)
		return -EINVAL;

	*dlen = tmp_len;
	return 0;
}

static int lzorle_sdecompress(struct crypto_scomp *tfm, const u8 *src,
			   unsigned int slen, u8 *dst, unsigned int *dlen,
			   void *ctx)
{
	return __lzorle_decompress(src, slen, dst, dlen);
}

static struct scomp_alg scomp = {
	.alloc_ctx		= lzorle_alloc_ctx,
	.free_ctx		= lzorle_free_ctx,
	.compress		= lzorle_scompress,
	.decompress		= lzorle_sdecompress,
	.base			= {
		.cra_name	= "lzo-rle",
		.cra_driver_name = "lzo-rle-scomp",
		.cra_module	 = THIS_MODULE,
	}
};

static int __init lzorle_mod_init(void)
{
	return crypto_register_scomp(&scomp);
}

static void __exit lzorle_mod_fini(void)
{
	crypto_unregister_scomp(&scomp);
}

subsys_initcall(lzorle_mod_init);
module_exit(lzorle_mod_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("LZO-RLE Compression Algorithm");
MODULE_ALIAS_CRYPTO("lzo-rle");
