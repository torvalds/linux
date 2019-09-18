// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Glue Code for AVX assembler versions of Serpent Cipher
 *
 * Copyright (C) 2012 Johannes Goetzfried
 *     <Johannes.Goetzfried@informatik.stud.uni-erlangen.de>
 *
 * Copyright © 2011-2013 Jussi Kivilinna <jussi.kivilinna@iki.fi>
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/crypto.h>
#include <linux/err.h>
#include <crypto/algapi.h>
#include <crypto/internal/simd.h>
#include <crypto/serpent.h>
#include <crypto/xts.h>
#include <asm/crypto/glue_helper.h>
#include <asm/crypto/serpent-avx.h>

/* 8-way parallel cipher functions */
asmlinkage void serpent_ecb_enc_8way_avx(struct serpent_ctx *ctx, u8 *dst,
					 const u8 *src);
EXPORT_SYMBOL_GPL(serpent_ecb_enc_8way_avx);

asmlinkage void serpent_ecb_dec_8way_avx(struct serpent_ctx *ctx, u8 *dst,
					 const u8 *src);
EXPORT_SYMBOL_GPL(serpent_ecb_dec_8way_avx);

asmlinkage void serpent_cbc_dec_8way_avx(struct serpent_ctx *ctx, u8 *dst,
					 const u8 *src);
EXPORT_SYMBOL_GPL(serpent_cbc_dec_8way_avx);

asmlinkage void serpent_ctr_8way_avx(struct serpent_ctx *ctx, u8 *dst,
				     const u8 *src, le128 *iv);
EXPORT_SYMBOL_GPL(serpent_ctr_8way_avx);

asmlinkage void serpent_xts_enc_8way_avx(struct serpent_ctx *ctx, u8 *dst,
					 const u8 *src, le128 *iv);
EXPORT_SYMBOL_GPL(serpent_xts_enc_8way_avx);

asmlinkage void serpent_xts_dec_8way_avx(struct serpent_ctx *ctx, u8 *dst,
					 const u8 *src, le128 *iv);
EXPORT_SYMBOL_GPL(serpent_xts_dec_8way_avx);

void __serpent_crypt_ctr(void *ctx, u128 *dst, const u128 *src, le128 *iv)
{
	be128 ctrblk;

	le128_to_be128(&ctrblk, iv);
	le128_inc(iv);

	__serpent_encrypt(ctx, (u8 *)&ctrblk, (u8 *)&ctrblk);
	u128_xor(dst, src, (u128 *)&ctrblk);
}
EXPORT_SYMBOL_GPL(__serpent_crypt_ctr);

void serpent_xts_enc(void *ctx, u128 *dst, const u128 *src, le128 *iv)
{
	glue_xts_crypt_128bit_one(ctx, dst, src, iv,
				  GLUE_FUNC_CAST(__serpent_encrypt));
}
EXPORT_SYMBOL_GPL(serpent_xts_enc);

void serpent_xts_dec(void *ctx, u128 *dst, const u128 *src, le128 *iv)
{
	glue_xts_crypt_128bit_one(ctx, dst, src, iv,
				  GLUE_FUNC_CAST(__serpent_decrypt));
}
EXPORT_SYMBOL_GPL(serpent_xts_dec);

static int serpent_setkey_skcipher(struct crypto_skcipher *tfm,
				   const u8 *key, unsigned int keylen)
{
	return __serpent_setkey(crypto_skcipher_ctx(tfm), key, keylen);
}

int xts_serpent_setkey(struct crypto_skcipher *tfm, const u8 *key,
		       unsigned int keylen)
{
	struct serpent_xts_ctx *ctx = crypto_skcipher_ctx(tfm);
	int err;

	err = xts_verify_key(tfm, key, keylen);
	if (err)
		return err;

	/* first half of xts-key is for crypt */
	err = __serpent_setkey(&ctx->crypt_ctx, key, keylen / 2);
	if (err)
		return err;

	/* second half of xts-key is for tweak */
	return __serpent_setkey(&ctx->tweak_ctx, key + keylen / 2, keylen / 2);
}
EXPORT_SYMBOL_GPL(xts_serpent_setkey);

static const struct common_glue_ctx serpent_enc = {
	.num_funcs = 2,
	.fpu_blocks_limit = SERPENT_PARALLEL_BLOCKS,

	.funcs = { {
		.num_blocks = SERPENT_PARALLEL_BLOCKS,
		.fn_u = { .ecb = GLUE_FUNC_CAST(serpent_ecb_enc_8way_avx) }
	}, {
		.num_blocks = 1,
		.fn_u = { .ecb = GLUE_FUNC_CAST(__serpent_encrypt) }
	} }
};

static const struct common_glue_ctx serpent_ctr = {
	.num_funcs = 2,
	.fpu_blocks_limit = SERPENT_PARALLEL_BLOCKS,

	.funcs = { {
		.num_blocks = SERPENT_PARALLEL_BLOCKS,
		.fn_u = { .ctr = GLUE_CTR_FUNC_CAST(serpent_ctr_8way_avx) }
	}, {
		.num_blocks = 1,
		.fn_u = { .ctr = GLUE_CTR_FUNC_CAST(__serpent_crypt_ctr) }
	} }
};

static const struct common_glue_ctx serpent_enc_xts = {
	.num_funcs = 2,
	.fpu_blocks_limit = SERPENT_PARALLEL_BLOCKS,

	.funcs = { {
		.num_blocks = SERPENT_PARALLEL_BLOCKS,
		.fn_u = { .xts = GLUE_XTS_FUNC_CAST(serpent_xts_enc_8way_avx) }
	}, {
		.num_blocks = 1,
		.fn_u = { .xts = GLUE_XTS_FUNC_CAST(serpent_xts_enc) }
	} }
};

static const struct common_glue_ctx serpent_dec = {
	.num_funcs = 2,
	.fpu_blocks_limit = SERPENT_PARALLEL_BLOCKS,

	.funcs = { {
		.num_blocks = SERPENT_PARALLEL_BLOCKS,
		.fn_u = { .ecb = GLUE_FUNC_CAST(serpent_ecb_dec_8way_avx) }
	}, {
		.num_blocks = 1,
		.fn_u = { .ecb = GLUE_FUNC_CAST(__serpent_decrypt) }
	} }
};

static const struct common_glue_ctx serpent_dec_cbc = {
	.num_funcs = 2,
	.fpu_blocks_limit = SERPENT_PARALLEL_BLOCKS,

	.funcs = { {
		.num_blocks = SERPENT_PARALLEL_BLOCKS,
		.fn_u = { .cbc = GLUE_CBC_FUNC_CAST(serpent_cbc_dec_8way_avx) }
	}, {
		.num_blocks = 1,
		.fn_u = { .cbc = GLUE_CBC_FUNC_CAST(__serpent_decrypt) }
	} }
};

static const struct common_glue_ctx serpent_dec_xts = {
	.num_funcs = 2,
	.fpu_blocks_limit = SERPENT_PARALLEL_BLOCKS,

	.funcs = { {
		.num_blocks = SERPENT_PARALLEL_BLOCKS,
		.fn_u = { .xts = GLUE_XTS_FUNC_CAST(serpent_xts_dec_8way_avx) }
	}, {
		.num_blocks = 1,
		.fn_u = { .xts = GLUE_XTS_FUNC_CAST(serpent_xts_dec) }
	} }
};

static int ecb_encrypt(struct skcipher_request *req)
{
	return glue_ecb_req_128bit(&serpent_enc, req);
}

static int ecb_decrypt(struct skcipher_request *req)
{
	return glue_ecb_req_128bit(&serpent_dec, req);
}

static int cbc_encrypt(struct skcipher_request *req)
{
	return glue_cbc_encrypt_req_128bit(GLUE_FUNC_CAST(__serpent_encrypt),
					   req);
}

static int cbc_decrypt(struct skcipher_request *req)
{
	return glue_cbc_decrypt_req_128bit(&serpent_dec_cbc, req);
}

static int ctr_crypt(struct skcipher_request *req)
{
	return glue_ctr_req_128bit(&serpent_ctr, req);
}

static int xts_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct serpent_xts_ctx *ctx = crypto_skcipher_ctx(tfm);

	return glue_xts_req_128bit(&serpent_enc_xts, req,
				   XTS_TWEAK_CAST(__serpent_encrypt),
				   &ctx->tweak_ctx, &ctx->crypt_ctx, false);
}

static int xts_decrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct serpent_xts_ctx *ctx = crypto_skcipher_ctx(tfm);

	return glue_xts_req_128bit(&serpent_dec_xts, req,
				   XTS_TWEAK_CAST(__serpent_encrypt),
				   &ctx->tweak_ctx, &ctx->crypt_ctx, true);
}

static struct skcipher_alg serpent_algs[] = {
	{
		.base.cra_name		= "__ecb(serpent)",
		.base.cra_driver_name	= "__ecb-serpent-avx",
		.base.cra_priority	= 500,
		.base.cra_flags		= CRYPTO_ALG_INTERNAL,
		.base.cra_blocksize	= SERPENT_BLOCK_SIZE,
		.base.cra_ctxsize	= sizeof(struct serpent_ctx),
		.base.cra_module	= THIS_MODULE,
		.min_keysize		= SERPENT_MIN_KEY_SIZE,
		.max_keysize		= SERPENT_MAX_KEY_SIZE,
		.setkey			= serpent_setkey_skcipher,
		.encrypt		= ecb_encrypt,
		.decrypt		= ecb_decrypt,
	}, {
		.base.cra_name		= "__cbc(serpent)",
		.base.cra_driver_name	= "__cbc-serpent-avx",
		.base.cra_priority	= 500,
		.base.cra_flags		= CRYPTO_ALG_INTERNAL,
		.base.cra_blocksize	= SERPENT_BLOCK_SIZE,
		.base.cra_ctxsize	= sizeof(struct serpent_ctx),
		.base.cra_module	= THIS_MODULE,
		.min_keysize		= SERPENT_MIN_KEY_SIZE,
		.max_keysize		= SERPENT_MAX_KEY_SIZE,
		.ivsize			= SERPENT_BLOCK_SIZE,
		.setkey			= serpent_setkey_skcipher,
		.encrypt		= cbc_encrypt,
		.decrypt		= cbc_decrypt,
	}, {
		.base.cra_name		= "__ctr(serpent)",
		.base.cra_driver_name	= "__ctr-serpent-avx",
		.base.cra_priority	= 500,
		.base.cra_flags		= CRYPTO_ALG_INTERNAL,
		.base.cra_blocksize	= 1,
		.base.cra_ctxsize	= sizeof(struct serpent_ctx),
		.base.cra_module	= THIS_MODULE,
		.min_keysize		= SERPENT_MIN_KEY_SIZE,
		.max_keysize		= SERPENT_MAX_KEY_SIZE,
		.ivsize			= SERPENT_BLOCK_SIZE,
		.chunksize		= SERPENT_BLOCK_SIZE,
		.setkey			= serpent_setkey_skcipher,
		.encrypt		= ctr_crypt,
		.decrypt		= ctr_crypt,
	}, {
		.base.cra_name		= "__xts(serpent)",
		.base.cra_driver_name	= "__xts-serpent-avx",
		.base.cra_priority	= 500,
		.base.cra_flags		= CRYPTO_ALG_INTERNAL,
		.base.cra_blocksize	= SERPENT_BLOCK_SIZE,
		.base.cra_ctxsize	= sizeof(struct serpent_xts_ctx),
		.base.cra_module	= THIS_MODULE,
		.min_keysize		= 2 * SERPENT_MIN_KEY_SIZE,
		.max_keysize		= 2 * SERPENT_MAX_KEY_SIZE,
		.ivsize			= SERPENT_BLOCK_SIZE,
		.setkey			= xts_serpent_setkey,
		.encrypt		= xts_encrypt,
		.decrypt		= xts_decrypt,
	},
};

static struct simd_skcipher_alg *serpent_simd_algs[ARRAY_SIZE(serpent_algs)];

static int __init serpent_init(void)
{
	const char *feature_name;

	if (!cpu_has_xfeatures(XFEATURE_MASK_SSE | XFEATURE_MASK_YMM,
				&feature_name)) {
		pr_info("CPU feature '%s' is not supported.\n", feature_name);
		return -ENODEV;
	}

	return simd_register_skciphers_compat(serpent_algs,
					      ARRAY_SIZE(serpent_algs),
					      serpent_simd_algs);
}

static void __exit serpent_exit(void)
{
	simd_unregister_skciphers(serpent_algs, ARRAY_SIZE(serpent_algs),
				  serpent_simd_algs);
}

module_init(serpent_init);
module_exit(serpent_exit);

MODULE_DESCRIPTION("Serpent Cipher Algorithm, AVX optimized");
MODULE_LICENSE("GPL");
MODULE_ALIAS_CRYPTO("serpent");
