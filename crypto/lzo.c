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
#include <linux/lzo.h>

struct lzo_ctx {
	void *lzo_comp_mem;
};

static int lzo_init(struct crypto_tfm *tfm)
{
	struct lzo_ctx *ctx = crypto_tfm_ctx(tfm);

	ctx->lzo_comp_mem = vmalloc(LZO1X_MEM_COMPRESS);
	if (!ctx->lzo_comp_mem)
		return -ENOMEM;

	return 0;
}

static void lzo_exit(struct crypto_tfm *tfm)
{
	struct lzo_ctx *ctx = crypto_tfm_ctx(tfm);

	vfree(ctx->lzo_comp_mem);
}

static int lzo_compress(struct crypto_tfm *tfm, const u8 *src,
			    unsigned int slen, u8 *dst, unsigned int *dlen)
{
	struct lzo_ctx *ctx = crypto_tfm_ctx(tfm);
	size_t tmp_len = *dlen; /* size_t(ulong) <-> uint on 64 bit */
	int err;

	err = lzo1x_1_compress(src, slen, dst, &tmp_len, ctx->lzo_comp_mem);

	if (err != LZO_E_OK)
		return -EINVAL;

	*dlen = tmp_len;
	return 0;
}

static int lzo_decompress(struct crypto_tfm *tfm, const u8 *src,
			      unsigned int slen, u8 *dst, unsigned int *dlen)
{
	int err;
	size_t tmp_len = *dlen; /* size_t(ulong) <-> uint on 64 bit */

	err = lzo1x_decompress_safe(src, slen, dst, &tmp_len);

	if (err != LZO_E_OK)
		return -EINVAL;

	*dlen = tmp_len;
	return 0;

}

static struct crypto_alg alg = {
	.cra_name		= "lzo",
	.cra_flags		= CRYPTO_ALG_TYPE_COMPRESS,
	.cra_ctxsize		= sizeof(struct lzo_ctx),
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(alg.cra_list),
	.cra_init		= lzo_init,
	.cra_exit		= lzo_exit,
	.cra_u			= { .compress = {
	.coa_compress 		= lzo_compress,
	.coa_decompress  	= lzo_decompress } }
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

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("LZO Compression Algorithm");
