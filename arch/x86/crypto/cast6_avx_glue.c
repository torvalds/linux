// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Glue Code for the AVX assembler implementation of the Cast6 Cipher
 *
 * Copyright (C) 2012 Johannes Goetzfried
 *     <Johannes.Goetzfried@informatik.stud.uni-erlangen.de>
 *
 * Copyright © 2013 Jussi Kivilinna <jussi.kivilinna@iki.fi>
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/crypto.h>
#include <linux/err.h>
#include <crypto/algapi.h>
#include <crypto/cast6.h>
#include <crypto/internal/simd.h>
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

static int cast6_setkey_skcipher(struct crypto_skcipher *tfm,
				 const u8 *key, unsigned int keylen)
{
	return cast6_setkey(&tfm->base, key, keylen);
}

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

static int ecb_encrypt(struct skcipher_request *req)
{
	return glue_ecb_req_128bit(&cast6_enc, req);
}

static int ecb_decrypt(struct skcipher_request *req)
{
	return glue_ecb_req_128bit(&cast6_dec, req);
}

static int cbc_encrypt(struct skcipher_request *req)
{
	return glue_cbc_encrypt_req_128bit(GLUE_FUNC_CAST(__cast6_encrypt),
					   req);
}

static int cbc_decrypt(struct skcipher_request *req)
{
	return glue_cbc_decrypt_req_128bit(&cast6_dec_cbc, req);
}

static int ctr_crypt(struct skcipher_request *req)
{
	return glue_ctr_req_128bit(&cast6_ctr, req);
}

struct cast6_xts_ctx {
	struct cast6_ctx tweak_ctx;
	struct cast6_ctx crypt_ctx;
};

static int xts_cast6_setkey(struct crypto_skcipher *tfm, const u8 *key,
			    unsigned int keylen)
{
	struct cast6_xts_ctx *ctx = crypto_skcipher_ctx(tfm);
	u32 *flags = &tfm->base.crt_flags;
	int err;

	err = xts_verify_key(tfm, key, keylen);
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

static int xts_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct cast6_xts_ctx *ctx = crypto_skcipher_ctx(tfm);

	return glue_xts_req_128bit(&cast6_enc_xts, req,
				   XTS_TWEAK_CAST(__cast6_encrypt),
				   &ctx->tweak_ctx, &ctx->crypt_ctx);
}

static int xts_decrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct cast6_xts_ctx *ctx = crypto_skcipher_ctx(tfm);

	return glue_xts_req_128bit(&cast6_dec_xts, req,
				   XTS_TWEAK_CAST(__cast6_encrypt),
				   &ctx->tweak_ctx, &ctx->crypt_ctx);
}

static struct skcipher_alg cast6_algs[] = {
	{
		.base.cra_name		= "__ecb(cast6)",
		.base.cra_driver_name	= "__ecb-cast6-avx",
		.base.cra_priority	= 200,
		.base.cra_flags		= CRYPTO_ALG_INTERNAL,
		.base.cra_blocksize	= CAST6_BLOCK_SIZE,
		.base.cra_ctxsize	= sizeof(struct cast6_ctx),
		.base.cra_module	= THIS_MODULE,
		.min_keysize		= CAST6_MIN_KEY_SIZE,
		.max_keysize		= CAST6_MAX_KEY_SIZE,
		.setkey			= cast6_setkey_skcipher,
		.encrypt		= ecb_encrypt,
		.decrypt		= ecb_decrypt,
	}, {
		.base.cra_name		= "__cbc(cast6)",
		.base.cra_driver_name	= "__cbc-cast6-avx",
		.base.cra_priority	= 200,
		.base.cra_flags		= CRYPTO_ALG_INTERNAL,
		.base.cra_blocksize	= CAST6_BLOCK_SIZE,
		.base.cra_ctxsize	= sizeof(struct cast6_ctx),
		.base.cra_module	= THIS_MODULE,
		.min_keysize		= CAST6_MIN_KEY_SIZE,
		.max_keysize		= CAST6_MAX_KEY_SIZE,
		.ivsize			= CAST6_BLOCK_SIZE,
		.setkey			= cast6_setkey_skcipher,
		.encrypt		= cbc_encrypt,
		.decrypt		= cbc_decrypt,
	}, {
		.base.cra_name		= "__ctr(cast6)",
		.base.cra_driver_name	= "__ctr-cast6-avx",
		.base.cra_priority	= 200,
		.base.cra_flags		= CRYPTO_ALG_INTERNAL,
		.base.cra_blocksize	= 1,
		.base.cra_ctxsize	= sizeof(struct cast6_ctx),
		.base.cra_module	= THIS_MODULE,
		.min_keysize		= CAST6_MIN_KEY_SIZE,
		.max_keysize		= CAST6_MAX_KEY_SIZE,
		.ivsize			= CAST6_BLOCK_SIZE,
		.chunksize		= CAST6_BLOCK_SIZE,
		.setkey			= cast6_setkey_skcipher,
		.encrypt		= ctr_crypt,
		.decrypt		= ctr_crypt,
	}, {
		.base.cra_name		= "__xts(cast6)",
		.base.cra_driver_name	= "__xts-cast6-avx",
		.base.cra_priority	= 200,
		.base.cra_flags		= CRYPTO_ALG_INTERNAL,
		.base.cra_blocksize	= CAST6_BLOCK_SIZE,
		.base.cra_ctxsize	= sizeof(struct cast6_xts_ctx),
		.base.cra_module	= THIS_MODULE,
		.min_keysize		= 2 * CAST6_MIN_KEY_SIZE,
		.max_keysize		= 2 * CAST6_MAX_KEY_SIZE,
		.ivsize			= CAST6_BLOCK_SIZE,
		.setkey			= xts_cast6_setkey,
		.encrypt		= xts_encrypt,
		.decrypt		= xts_decrypt,
	},
};

static struct simd_skcipher_alg *cast6_simd_algs[ARRAY_SIZE(cast6_algs)];

static int __init cast6_init(void)
{
	const char *feature_name;

	if (!cpu_has_xfeatures(XFEATURE_MASK_SSE | XFEATURE_MASK_YMM,
				&feature_name)) {
		pr_info("CPU feature '%s' is not supported.\n", feature_name);
		return -ENODEV;
	}

	return simd_register_skciphers_compat(cast6_algs,
					      ARRAY_SIZE(cast6_algs),
					      cast6_simd_algs);
}

static void __exit cast6_exit(void)
{
	simd_unregister_skciphers(cast6_algs, ARRAY_SIZE(cast6_algs),
				  cast6_simd_algs);
}

module_init(cast6_init);
module_exit(cast6_exit);

MODULE_DESCRIPTION("Cast6 Cipher Algorithm, AVX optimized");
MODULE_LICENSE("GPL");
MODULE_ALIAS_CRYPTO("cast6");
