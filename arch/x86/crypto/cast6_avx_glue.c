/*
 * Glue Code for the AVX assembler implemention of the Cast6 Cipher
 *
 * Copyright (C) 2012 Johannes Goetzfried
 *     <Johannes.Goetzfried@informatik.stud.uni-erlangen.de>
 *
 * Copyright © 2013 Jussi Kivilinna <jussi.kivilinna@iki.fi>
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 * USA
 *
 */

#include <linux/module.h>
#include <linux/hardirq.h>
#include <linux/types.h>
#include <linux/crypto.h>
#include <linux/err.h>
#include <crypto/algapi.h>
#include <crypto/cast6.h>
#include <crypto/cryptd.h>
#include <crypto/b128ops.h>
#include <crypto/ctr.h>
#include <crypto/lrw.h>
#include <crypto/xts.h>
#include <asm/xcr.h>
#include <asm/xsave.h>
#include <asm/crypto/ablk_helper.h>
#include <asm/crypto/glue_helper.h>

#define CAST6_PARALLEL_BLOCKS 8

asmlinkage void cast6_ecb_enc_8way(struct cast6_ctx *ctx, u8 *dst,
				   const u8 *src);
asmlinkage void cast6_ecb_dec_8way(struct cast6_ctx *ctx, u8 *dst,
				   const u8 *src);

asmlinkage void cast6_cbc_dec_8way(struct cast6_ctx *ctx, u8 *dst,
				   const u8 *src);
asmlinkage void cast6_ctr_8way(struct cast6_ctx *ctx, u8 *dst, const u8 *src,
			       le128 *iv);

asmlinkage void cast6_xts_enc_8way(struct cast6_ctx *ctx, u8 *dst,
				   const u8 *src, le128 *iv);
asmlinkage void cast6_xts_dec_8way(struct cast6_ctx *ctx, u8 *dst,
				   const u8 *src, le128 *iv);

static void cast6_xts_enc(void *ctx, u128 *dst, const u128 *src, le128 *iv)
{
	glue_xts_crypt_128bit_one(ctx, dst, src, iv,
				  GLUE_FUNC_CAST(__cast6_encrypt));
}

static void cast6_xts_dec(void *ctx, u128 *dst, const u128 *src, le128 *iv)
{
	glue_xts_crypt_128bit_one(ctx, dst, src, iv,
				  GLUE_FUNC_CAST(__cast6_decrypt));
}

static void cast6_crypt_ctr(void *ctx, u128 *dst, const u128 *src, le128 *iv)
{
	be128 ctrblk;

	le128_to_be128(&ctrblk, iv);
	le128_inc(iv);

	__cast6_encrypt(ctx, (u8 *)&ctrblk, (u8 *)&ctrblk);
	u128_xor(dst, src, (u128 *)&ctrblk);
}

static const struct common_glue_ctx cast6_enc = {
	.num_funcs = 2,
	.fpu_blocks_limit = CAST6_PARALLEL_BLOCKS,

	.funcs = { {
		.num_blocks = CAST6_PARALLEL_BLOCKS,
		.fn_u = { .ecb = GLUE_FUNC_CAST(cast6_ecb_enc_8way) }
	}, {
		.num_blocks = 1,
		.fn_u = { .ecb = GLUE_FUNC_CAST(__cast6_encrypt) }
	} }
};

static const struct common_glue_ctx cast6_ctr = {
	.num_funcs = 2,
	.fpu_blocks_limit = CAST6_PARALLEL_BLOCKS,

	.funcs = { {
		.num_blocks = CAST6_PARALLEL_BLOCKS,
		.fn_u = { .ctr = GLUE_CTR_FUNC_CAST(cast6_ctr_8way) }
	}, {
		.num_blocks = 1,
		.fn_u = { .ctr = GLUE_CTR_FUNC_CAST(cast6_crypt_ctr) }
	} }
};

static const struct common_glue_ctx cast6_enc_xts = {
	.num_funcs = 2,
	.fpu_blocks_limit = CAST6_PARALLEL_BLOCKS,

	.funcs = { {
		.num_blocks = CAST6_PARALLEL_BLOCKS,
		.fn_u = { .xts = GLUE_XTS_FUNC_CAST(cast6_xts_enc_8way) }
	}, {
		.num_blocks = 1,
		.fn_u = { .xts = GLUE_XTS_FUNC_CAST(cast6_xts_enc) }
	} }
};

static const struct common_glue_ctx cast6_dec = {
	.num_funcs = 2,
	.fpu_blocks_limit = CAST6_PARALLEL_BLOCKS,

	.funcs = { {
		.num_blocks = CAST6_PARALLEL_BLOCKS,
		.fn_u = { .ecb = GLUE_FUNC_CAST(cast6_ecb_dec_8way) }
	}, {
		.num_blocks = 1,
		.fn_u = { .ecb = GLUE_FUNC_CAST(__cast6_decrypt) }
	} }
};

static const struct common_glue_ctx cast6_dec_cbc = {
	.num_funcs = 2,
	.fpu_blocks_limit = CAST6_PARALLEL_BLOCKS,

	.funcs = { {
		.num_blocks = CAST6_PARALLEL_BLOCKS,
		.fn_u = { .cbc = GLUE_CBC_FUNC_CAST(cast6_cbc_dec_8way) }
	}, {
		.num_blocks = 1,
		.fn_u = { .cbc = GLUE_CBC_FUNC_CAST(__cast6_decrypt) }
	} }
};

static const struct common_glue_ctx cast6_dec_xts = {
	.num_funcs = 2,
	.fpu_blocks_limit = CAST6_PARALLEL_BLOCKS,

	.funcs = { {
		.num_blocks = CAST6_PARALLEL_BLOCKS,
		.fn_u = { .xts = GLUE_XTS_FUNC_CAST(cast6_xts_dec_8way) }
	}, {
		.num_blocks = 1,
		.fn_u = { .xts = GLUE_XTS_FUNC_CAST(cast6_xts_dec) }
	} }
};

static int ecb_encrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		       struct scatterlist *src, unsigned int nbytes)
{
	return glue_ecb_crypt_128bit(&cast6_enc, desc, dst, src, nbytes);
}

static int ecb_decrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		       struct scatterlist *src, unsigned int nbytes)
{
	return glue_ecb_crypt_128bit(&cast6_dec, desc, dst, src, nbytes);
}

static int cbc_encrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		       struct scatterlist *src, unsigned int nbytes)
{
	return glue_cbc_encrypt_128bit(GLUE_FUNC_CAST(__cast6_encrypt), desc,
				       dst, src, nbytes);
}

static int cbc_decrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		       struct scatterlist *src, unsigned int nbytes)
{
	return glue_cbc_decrypt_128bit(&cast6_dec_cbc, desc, dst, src,
				       nbytes);
}

static int ctr_crypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		     struct scatterlist *src, unsigned int nbytes)
{
	return glue_ctr_crypt_128bit(&cast6_ctr, desc, dst, src, nbytes);
}

static inline bool cast6_fpu_begin(bool fpu_enabled, unsigned int nbytes)
{
	return glue_fpu_begin(CAST6_BLOCK_SIZE, CAST6_PARALLEL_BLOCKS,
			      NULL, fpu_enabled, nbytes);
}

static inline void cast6_fpu_end(bool fpu_enabled)
{
	glue_fpu_end(fpu_enabled);
}

struct crypt_priv {
	struct cast6_ctx *ctx;
	bool fpu_enabled;
};

static void encrypt_callback(void *priv, u8 *srcdst, unsigned int nbytes)
{
	const unsigned int bsize = CAST6_BLOCK_SIZE;
	struct crypt_priv *ctx = priv;
	int i;

	ctx->fpu_enabled = cast6_fpu_begin(ctx->fpu_enabled, nbytes);

	if (nbytes == bsize * CAST6_PARALLEL_BLOCKS) {
		cast6_ecb_enc_8way(ctx->ctx, srcdst, srcdst);
		return;
	}

	for (i = 0; i < nbytes / bsize; i++, srcdst += bsize)
		__cast6_encrypt(ctx->ctx, srcdst, srcdst);
}

static void decrypt_callback(void *priv, u8 *srcdst, unsigned int nbytes)
{
	const unsigned int bsize = CAST6_BLOCK_SIZE;
	struct crypt_priv *ctx = priv;
	int i;

	ctx->fpu_enabled = cast6_fpu_begin(ctx->fpu_enabled, nbytes);

	if (nbytes == bsize * CAST6_PARALLEL_BLOCKS) {
		cast6_ecb_dec_8way(ctx->ctx, srcdst, srcdst);
		return;
	}

	for (i = 0; i < nbytes / bsize; i++, srcdst += bsize)
		__cast6_decrypt(ctx->ctx, srcdst, srcdst);
}

struct cast6_lrw_ctx {
	struct lrw_table_ctx lrw_table;
	struct cast6_ctx cast6_ctx;
};

static int lrw_cast6_setkey(struct crypto_tfm *tfm, const u8 *key,
			      unsigned int keylen)
{
	struct cast6_lrw_ctx *ctx = crypto_tfm_ctx(tfm);
	int err;

	err = __cast6_setkey(&ctx->cast6_ctx, key, keylen - CAST6_BLOCK_SIZE,
			     &tfm->crt_flags);
	if (err)
		return err;

	return lrw_init_table(&ctx->lrw_table, key + keylen - CAST6_BLOCK_SIZE);
}

static int lrw_encrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		       struct scatterlist *src, unsigned int nbytes)
{
	struct cast6_lrw_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	be128 buf[CAST6_PARALLEL_BLOCKS];
	struct crypt_priv crypt_ctx = {
		.ctx = &ctx->cast6_ctx,
		.fpu_enabled = false,
	};
	struct lrw_crypt_req req = {
		.tbuf = buf,
		.tbuflen = sizeof(buf),

		.table_ctx = &ctx->lrw_table,
		.crypt_ctx = &crypt_ctx,
		.crypt_fn = encrypt_callback,
	};
	int ret;

	desc->flags &= ~CRYPTO_TFM_REQ_MAY_SLEEP;
	ret = lrw_crypt(desc, dst, src, nbytes, &req);
	cast6_fpu_end(crypt_ctx.fpu_enabled);

	return ret;
}

static int lrw_decrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		       struct scatterlist *src, unsigned int nbytes)
{
	struct cast6_lrw_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	be128 buf[CAST6_PARALLEL_BLOCKS];
	struct crypt_priv crypt_ctx = {
		.ctx = &ctx->cast6_ctx,
		.fpu_enabled = false,
	};
	struct lrw_crypt_req req = {
		.tbuf = buf,
		.tbuflen = sizeof(buf),

		.table_ctx = &ctx->lrw_table,
		.crypt_ctx = &crypt_ctx,
		.crypt_fn = decrypt_callback,
	};
	int ret;

	desc->flags &= ~CRYPTO_TFM_REQ_MAY_SLEEP;
	ret = lrw_crypt(desc, dst, src, nbytes, &req);
	cast6_fpu_end(crypt_ctx.fpu_enabled);

	return ret;
}

static void lrw_exit_tfm(struct crypto_tfm *tfm)
{
	struct cast6_lrw_ctx *ctx = crypto_tfm_ctx(tfm);

	lrw_free_table(&ctx->lrw_table);
}

struct cast6_xts_ctx {
	struct cast6_ctx tweak_ctx;
	struct cast6_ctx crypt_ctx;
};

static int xts_cast6_setkey(struct crypto_tfm *tfm, const u8 *key,
			      unsigned int keylen)
{
	struct cast6_xts_ctx *ctx = crypto_tfm_ctx(tfm);
	u32 *flags = &tfm->crt_flags;
	int err;

	/* key consists of keys of equal size concatenated, therefore
	 * the length must be even
	 */
	if (keylen % 2) {
		*flags |= CRYPTO_TFM_RES_BAD_KEY_LEN;
		return -EINVAL;
	}

	/* first half of xts-key is for crypt */
	err = __cast6_setkey(&ctx->crypt_ctx, key, keylen / 2, flags);
	if (err)
		return err;

	/* second half of xts-key is for tweak */
	return __cast6_setkey(&ctx->tweak_ctx, key + keylen / 2, keylen / 2,
			      flags);
}

static int xts_encrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		       struct scatterlist *src, unsigned int nbytes)
{
	struct cast6_xts_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);

	return glue_xts_crypt_128bit(&cast6_enc_xts, desc, dst, src, nbytes,
				     XTS_TWEAK_CAST(__cast6_encrypt),
				     &ctx->tweak_ctx, &ctx->crypt_ctx);
}

static int xts_decrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		       struct scatterlist *src, unsigned int nbytes)
{
	struct cast6_xts_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);

	return glue_xts_crypt_128bit(&cast6_dec_xts, desc, dst, src, nbytes,
				     XTS_TWEAK_CAST(__cast6_encrypt),
				     &ctx->tweak_ctx, &ctx->crypt_ctx);
}

static struct crypto_alg cast6_algs[10] = { {
	.cra_name		= "__ecb-cast6-avx",
	.cra_driver_name	= "__driver-ecb-cast6-avx",
	.cra_priority		= 0,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		= CAST6_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct cast6_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_blkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_u = {
		.blkcipher = {
			.min_keysize	= CAST6_MIN_KEY_SIZE,
			.max_keysize	= CAST6_MAX_KEY_SIZE,
			.setkey		= cast6_setkey,
			.encrypt	= ecb_encrypt,
			.decrypt	= ecb_decrypt,
		},
	},
}, {
	.cra_name		= "__cbc-cast6-avx",
	.cra_driver_name	= "__driver-cbc-cast6-avx",
	.cra_priority		= 0,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		= CAST6_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct cast6_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_blkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_u = {
		.blkcipher = {
			.min_keysize	= CAST6_MIN_KEY_SIZE,
			.max_keysize	= CAST6_MAX_KEY_SIZE,
			.setkey		= cast6_setkey,
			.encrypt	= cbc_encrypt,
			.decrypt	= cbc_decrypt,
		},
	},
}, {
	.cra_name		= "__ctr-cast6-avx",
	.cra_driver_name	= "__driver-ctr-cast6-avx",
	.cra_priority		= 0,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		= 1,
	.cra_ctxsize		= sizeof(struct cast6_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_blkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_u = {
		.blkcipher = {
			.min_keysize	= CAST6_MIN_KEY_SIZE,
			.max_keysize	= CAST6_MAX_KEY_SIZE,
			.ivsize		= CAST6_BLOCK_SIZE,
			.setkey		= cast6_setkey,
			.encrypt	= ctr_crypt,
			.decrypt	= ctr_crypt,
		},
	},
}, {
	.cra_name		= "__lrw-cast6-avx",
	.cra_driver_name	= "__driver-lrw-cast6-avx",
	.cra_priority		= 0,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		= CAST6_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct cast6_lrw_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_blkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_exit		= lrw_exit_tfm,
	.cra_u = {
		.blkcipher = {
			.min_keysize	= CAST6_MIN_KEY_SIZE +
					  CAST6_BLOCK_SIZE,
			.max_keysize	= CAST6_MAX_KEY_SIZE +
					  CAST6_BLOCK_SIZE,
			.ivsize		= CAST6_BLOCK_SIZE,
			.setkey		= lrw_cast6_setkey,
			.encrypt	= lrw_encrypt,
			.decrypt	= lrw_decrypt,
		},
	},
}, {
	.cra_name		= "__xts-cast6-avx",
	.cra_driver_name	= "__driver-xts-cast6-avx",
	.cra_priority		= 0,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		= CAST6_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct cast6_xts_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_blkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_u = {
		.blkcipher = {
			.min_keysize	= CAST6_MIN_KEY_SIZE * 2,
			.max_keysize	= CAST6_MAX_KEY_SIZE * 2,
			.ivsize		= CAST6_BLOCK_SIZE,
			.setkey		= xts_cast6_setkey,
			.encrypt	= xts_encrypt,
			.decrypt	= xts_decrypt,
		},
	},
}, {
	.cra_name		= "ecb(cast6)",
	.cra_driver_name	= "ecb-cast6-avx",
	.cra_priority		= 200,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= CAST6_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct async_helper_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= ablk_init,
	.cra_exit		= ablk_exit,
	.cra_u = {
		.ablkcipher = {
			.min_keysize	= CAST6_MIN_KEY_SIZE,
			.max_keysize	= CAST6_MAX_KEY_SIZE,
			.setkey		= ablk_set_key,
			.encrypt	= ablk_encrypt,
			.decrypt	= ablk_decrypt,
		},
	},
}, {
	.cra_name		= "cbc(cast6)",
	.cra_driver_name	= "cbc-cast6-avx",
	.cra_priority		= 200,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= CAST6_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct async_helper_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= ablk_init,
	.cra_exit		= ablk_exit,
	.cra_u = {
		.ablkcipher = {
			.min_keysize	= CAST6_MIN_KEY_SIZE,
			.max_keysize	= CAST6_MAX_KEY_SIZE,
			.ivsize		= CAST6_BLOCK_SIZE,
			.setkey		= ablk_set_key,
			.encrypt	= __ablk_encrypt,
			.decrypt	= ablk_decrypt,
		},
	},
}, {
	.cra_name		= "ctr(cast6)",
	.cra_driver_name	= "ctr-cast6-avx",
	.cra_priority		= 200,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= 1,
	.cra_ctxsize		= sizeof(struct async_helper_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= ablk_init,
	.cra_exit		= ablk_exit,
	.cra_u = {
		.ablkcipher = {
			.min_keysize	= CAST6_MIN_KEY_SIZE,
			.max_keysize	= CAST6_MAX_KEY_SIZE,
			.ivsize		= CAST6_BLOCK_SIZE,
			.setkey		= ablk_set_key,
			.encrypt	= ablk_encrypt,
			.decrypt	= ablk_encrypt,
			.geniv		= "chainiv",
		},
	},
}, {
	.cra_name		= "lrw(cast6)",
	.cra_driver_name	= "lrw-cast6-avx",
	.cra_priority		= 200,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= CAST6_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct async_helper_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= ablk_init,
	.cra_exit		= ablk_exit,
	.cra_u = {
		.ablkcipher = {
			.min_keysize	= CAST6_MIN_KEY_SIZE +
					  CAST6_BLOCK_SIZE,
			.max_keysize	= CAST6_MAX_KEY_SIZE +
					  CAST6_BLOCK_SIZE,
			.ivsize		= CAST6_BLOCK_SIZE,
			.setkey		= ablk_set_key,
			.encrypt	= ablk_encrypt,
			.decrypt	= ablk_decrypt,
		},
	},
}, {
	.cra_name		= "xts(cast6)",
	.cra_driver_name	= "xts-cast6-avx",
	.cra_priority		= 200,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= CAST6_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct async_helper_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= ablk_init,
	.cra_exit		= ablk_exit,
	.cra_u = {
		.ablkcipher = {
			.min_keysize	= CAST6_MIN_KEY_SIZE * 2,
			.max_keysize	= CAST6_MAX_KEY_SIZE * 2,
			.ivsize		= CAST6_BLOCK_SIZE,
			.setkey		= ablk_set_key,
			.encrypt	= ablk_encrypt,
			.decrypt	= ablk_decrypt,
		},
	},
} };

static int __init cast6_init(void)
{
	u64 xcr0;

	if (!cpu_has_avx || !cpu_has_osxsave) {
		pr_info("AVX instructions are not detected.\n");
		return -ENODEV;
	}

	xcr0 = xgetbv(XCR_XFEATURE_ENABLED_MASK);
	if ((xcr0 & (XSTATE_SSE | XSTATE_YMM)) != (XSTATE_SSE | XSTATE_YMM)) {
		pr_info("AVX detected but unusable.\n");
		return -ENODEV;
	}

	return crypto_register_algs(cast6_algs, ARRAY_SIZE(cast6_algs));
}

static void __exit cast6_exit(void)
{
	crypto_unregister_algs(cast6_algs, ARRAY_SIZE(cast6_algs));
}

module_init(cast6_init);
module_exit(cast6_exit);

MODULE_DESCRIPTION("Cast6 Cipher Algorithm, AVX optimized");
MODULE_LICENSE("GPL");
MODULE_ALIAS("cast6");
