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
#include <crypto/ablk_helper.h>
#include <crypto/algapi.h>
#include <crypto/cast6.h>
#include <crypto/cryptd.h>
#include <crypto/b128ops.h>
#include <crypto/ctr.h>
#include <crypto/xts.h>
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

	err = xts_check_key(tfm, key, keylen);
	if (err)
		return err;

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

static struct crypto_alg cast6_algs[] = { {
	.cra_name		= "__ecb-cast6-avx",
	.cra_driver_name	= "__driver-ecb-cast6-avx",
	.cra_priority		= 0,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER |
				  CRYPTO_ALG_INTERNAL,
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
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER |
				  CRYPTO_ALG_INTERNAL,
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
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER |
				  CRYPTO_ALG_INTERNAL,
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
	.cra_name		= "__xts-cast6-avx",
	.cra_driver_name	= "__driver-xts-cast6-avx",
	.cra_priority		= 0,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER |
				  CRYPTO_ALG_INTERNAL,
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
	const char *feature_name;

	if (!cpu_has_xfeatures(XFEATURE_MASK_SSE | XFEATURE_MASK_YMM,
				&feature_name)) {
		pr_info("CPU feature '%s' is not supported.\n", feature_name);
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
MODULE_ALIAS_CRYPTO("cast6");
