// SPDX-License-Identifier: GPL-2.0-only
/*
 * Cryptographic API.
 */

#include <crypto/internal/scompress.h>
#include <linux/init.h>
#include <linux/lzo.h>
#include <linux/module.h>
#include <linux/slab.h>

static void *lzo_alloc_ctx(void)
{
	void *ctx;

	ctx = kvmalloc(LZO1X_MEM_COMPRESS, GFP_KERNEL);
	if (!ctx)
		return ERR_PTR(-ENOMEM);

	return ctx;
}

static void lzo_free_ctx(void *ctx)
{
	kvfree(ctx);
}

static int __lzo_compress(const u8 *src, unsigned int slen,
			  u8 *dst, unsigned int *dlen, void *ctx)
{
	size_t tmp_len = *dlen; /* size_t(ulong) <-> uint on 64 bit */
	int err;

	err = lzo1x_1_compress_safe(src, slen, dst, &tmp_len, ctx);

	if (err != LZO_E_OK)
		return -EINVAL;

	*dlen = tmp_len;
	return 0;
}

static int lzo_scompress(struct crypto_scomp *tfm, const u8 *src,
			 unsigned int slen, u8 *dst, unsigned int *dlen,
			 void *ctx)
{
	return __lzo_compress(src, slen, dst, dlen, ctx);
}

static int __lzo_decompress(const u8 *src, unsigned int slen,
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

static int lzo_sdecompress(struct crypto_scomp *tfm, const u8 *src,
			   unsigned int slen, u8 *dst, unsigned int *dlen,
			   void *ctx)
{
	return __lzo_decompress(src, slen, dst, dlen);
}

static struct scomp_alg scomp = {
	.streams		= {
		.alloc_ctx	= lzo_alloc_ctx,
		.free_ctx	= lzo_free_ctx,
	},
	.compress		= lzo_scompress,
	.decompress		= lzo_sdecompress,
	.base			= {
		.cra_name	= "lzo",
		.cra_driver_name = "lzo-scomp",
		.cra_module	 = THIS_MODULE,
	}
};

static int __init lzo_mod_init(void)
{
	return crypto_register_scomp(&scomp);
}

static void __exit lzo_mod_fini(void)
{
	crypto_unregister_scomp(&scomp);
}

module_init(lzo_mod_init);
module_exit(lzo_mod_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("LZO Compression Algorithm");
MODULE_ALIAS_CRYPTO("lzo");
