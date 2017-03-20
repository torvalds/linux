/*
 * Cryptographic API.
 *
 * Copyright (c) 2013 Chanho Min <chanho.min@lge.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/crypto.h>
#include <linux/vmalloc.h>
#include <linux/lz4.h>
#include <crypto/internal/scompress.h>

struct lz4hc_ctx {
	void *lz4hc_comp_mem;
};

static void *lz4hc_alloc_ctx(struct crypto_scomp *tfm)
{
	void *ctx;

	ctx = vmalloc(LZ4HC_MEM_COMPRESS);
	if (!ctx)
		return ERR_PTR(-ENOMEM);

	return ctx;
}

static int lz4hc_init(struct crypto_tfm *tfm)
{
	struct lz4hc_ctx *ctx = crypto_tfm_ctx(tfm);

	ctx->lz4hc_comp_mem = lz4hc_alloc_ctx(NULL);
	if (IS_ERR(ctx->lz4hc_comp_mem))
		return -ENOMEM;

	return 0;
}

static void lz4hc_free_ctx(struct crypto_scomp *tfm, void *ctx)
{
	vfree(ctx);
}

static void lz4hc_exit(struct crypto_tfm *tfm)
{
	struct lz4hc_ctx *ctx = crypto_tfm_ctx(tfm);

	lz4hc_free_ctx(NULL, ctx->lz4hc_comp_mem);
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

static int lz4hc_compress_crypto(struct crypto_tfm *tfm, const u8 *src,
				 unsigned int slen, u8 *dst,
				 unsigned int *dlen)
{
	struct lz4hc_ctx *ctx = crypto_tfm_ctx(tfm);

	return __lz4hc_compress_crypto(src, slen, dst, dlen,
					ctx->lz4hc_comp_mem);
}

static int __lz4hc_decompress_crypto(const u8 *src, unsigned int slen,
				     u8 *dst, unsigned int *dlen, void *ctx)
{
	int out_len = LZ4_decompress_safe(src, dst, slen, *dlen);

	if (out_len < 0)
		return out_len;

	*dlen = out_len;
	return 0;
}

static int lz4hc_sdecompress(struct crypto_scomp *tfm, const u8 *src,
			     unsigned int slen, u8 *dst, unsigned int *dlen,
			     void *ctx)
{
	return __lz4hc_decompress_crypto(src, slen, dst, dlen, NULL);
}

static int lz4hc_decompress_crypto(struct crypto_tfm *tfm, const u8 *src,
				   unsigned int slen, u8 *dst,
				   unsigned int *dlen)
{
	return __lz4hc_decompress_crypto(src, slen, dst, dlen, NULL);
}

static struct crypto_alg alg_lz4hc = {
	.cra_name		= "lz4hc",
	.cra_flags		= CRYPTO_ALG_TYPE_COMPRESS,
	.cra_ctxsize		= sizeof(struct lz4hc_ctx),
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(alg_lz4hc.cra_list),
	.cra_init		= lz4hc_init,
	.cra_exit		= lz4hc_exit,
	.cra_u			= { .compress = {
	.coa_compress		= lz4hc_compress_crypto,
	.coa_decompress		= lz4hc_decompress_crypto } }
};

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
	int ret;

	ret = crypto_register_alg(&alg_lz4hc);
	if (ret)
		return ret;

	ret = crypto_register_scomp(&scomp);
	if (ret) {
		crypto_unregister_alg(&alg_lz4hc);
		return ret;
	}

	return ret;
}

static void __exit lz4hc_mod_fini(void)
{
	crypto_unregister_alg(&alg_lz4hc);
	crypto_unregister_scomp(&scomp);
}

module_init(lz4hc_mod_init);
module_exit(lz4hc_mod_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("LZ4HC Compression Algorithm");
MODULE_ALIAS_CRYPTO("lz4hc");
