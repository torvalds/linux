/*
 * Cryptographic API.
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
#include <linux/mm.h>
#include <linux/lzo.h>
#include <crypto/internal/scompress.h>

struct lzorle_ctx {
	void *lzorle_comp_mem;
};

static void *lzorle_alloc_ctx(struct crypto_scomp *tfm)
{
	void *ctx;

	ctx = kvmalloc(LZO1X_MEM_COMPRESS, GFP_KERNEL);
	if (!ctx)
		return ERR_PTR(-ENOMEM);

	return ctx;
}

static int lzorle_init(struct crypto_tfm *tfm)
{
	struct lzorle_ctx *ctx = crypto_tfm_ctx(tfm);

	ctx->lzorle_comp_mem = lzorle_alloc_ctx(NULL);
	if (IS_ERR(ctx->lzorle_comp_mem))
		return -ENOMEM;

	return 0;
}

static void lzorle_free_ctx(struct crypto_scomp *tfm, void *ctx)
{
	kvfree(ctx);
}

static void lzorle_exit(struct crypto_tfm *tfm)
{
	struct lzorle_ctx *ctx = crypto_tfm_ctx(tfm);

	lzorle_free_ctx(NULL, ctx->lzorle_comp_mem);
}

static int __lzorle_compress(const u8 *src, unsigned int slen,
			  u8 *dst, unsigned int *dlen, void *ctx)
{
	size_t tmp_len = *dlen; /* size_t(ulong) <-> uint on 64 bit */
	int err;

	err = lzorle1x_1_compress(src, slen, dst, &tmp_len, ctx);

	if (err != LZO_E_OK)
		return -EINVAL;

	*dlen = tmp_len;
	return 0;
}

static int lzorle_compress(struct crypto_tfm *tfm, const u8 *src,
			unsigned int slen, u8 *dst, unsigned int *dlen)
{
	struct lzorle_ctx *ctx = crypto_tfm_ctx(tfm);

	return __lzorle_compress(src, slen, dst, dlen, ctx->lzorle_comp_mem);
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

static int lzorle_decompress(struct crypto_tfm *tfm, const u8 *src,
			  unsigned int slen, u8 *dst, unsigned int *dlen)
{
	return __lzorle_decompress(src, slen, dst, dlen);
}

static int lzorle_sdecompress(struct crypto_scomp *tfm, const u8 *src,
			   unsigned int slen, u8 *dst, unsigned int *dlen,
			   void *ctx)
{
	return __lzorle_decompress(src, slen, dst, dlen);
}

static struct crypto_alg alg = {
	.cra_name		= "lzo-rle",
	.cra_flags		= CRYPTO_ALG_TYPE_COMPRESS,
	.cra_ctxsize		= sizeof(struct lzorle_ctx),
	.cra_module		= THIS_MODULE,
	.cra_init		= lzorle_init,
	.cra_exit		= lzorle_exit,
	.cra_u			= { .compress = {
	.coa_compress		= lzorle_compress,
	.coa_decompress		= lzorle_decompress } }
};

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

static void __exit lzorle_mod_fini(void)
{
	crypto_unregister_alg(&alg);
	crypto_unregister_scomp(&scomp);
}

subsys_initcall(lzorle_mod_init);
module_exit(lzorle_mod_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("LZO-RLE Compression Algorithm");
MODULE_ALIAS_CRYPTO("lzo-rle");
