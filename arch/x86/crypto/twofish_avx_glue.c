// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Glue Code for AVX assembler version of Twofish Cipher
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
#include <crypto/internal/simd.h>
#include <crypto/twofish.h>

#include "twofish.h"
#include "ecb_cbc_helpers.h"

#define TWOFISH_PARALLEL_BLOCKS 8

/* 8-way parallel cipher functions */
asmlinkage void twofish_ecb_enc_8way(const void *ctx, u8 *dst, const u8 *src);
asmlinkage void twofish_ecb_dec_8way(const void *ctx, u8 *dst, const u8 *src);

asmlinkage void twofish_cbc_dec_8way(const void *ctx, u8 *dst, const u8 *src);

static int twofish_setkey_skcipher(struct crypto_skcipher *tfm,
				   const u8 *key, unsigned int keylen)
{
	return twofish_setkey(&tfm->base, key, keylen);
}

static inline void twofish_enc_blk_3way(const void *ctx, u8 *dst, const u8 *src)
{
	__twofish_enc_blk_3way(ctx, dst, src, false);
}

static int ecb_encrypt(struct skcipher_request *req)
{
	ECB_WALK_START(req, TF_BLOCK_SIZE, TWOFISH_PARALLEL_BLOCKS);
	ECB_BLOCK(TWOFISH_PARALLEL_BLOCKS, twofish_ecb_enc_8way);
	ECB_BLOCK(3, twofish_enc_blk_3way);
	ECB_BLOCK(1, twofish_enc_blk);
	ECB_WALK_END();
}

static int ecb_decrypt(struct skcipher_request *req)
{
	ECB_WALK_START(req, TF_BLOCK_SIZE, TWOFISH_PARALLEL_BLOCKS);
	ECB_BLOCK(TWOFISH_PARALLEL_BLOCKS, twofish_ecb_dec_8way);
	ECB_BLOCK(3, twofish_dec_blk_3way);
	ECB_BLOCK(1, twofish_dec_blk);
	ECB_WALK_END();
}

static int cbc_encrypt(struct skcipher_request *req)
{
	CBC_WALK_START(req, TF_BLOCK_SIZE, -1);
	CBC_ENC_BLOCK(twofish_enc_blk);
	CBC_WALK_END();
}

static int cbc_decrypt(struct skcipher_request *req)
{
	CBC_WALK_START(req, TF_BLOCK_SIZE, TWOFISH_PARALLEL_BLOCKS);
	CBC_DEC_BLOCK(TWOFISH_PARALLEL_BLOCKS, twofish_cbc_dec_8way);
	CBC_DEC_BLOCK(3, twofish_dec_blk_cbc_3way);
	CBC_DEC_BLOCK(1, twofish_dec_blk);
	CBC_WALK_END();
}

static struct skcipher_alg twofish_algs[] = {
	{
		.base.cra_name		= "__ecb(twofish)",
		.base.cra_driver_name	= "__ecb-twofish-avx",
		.base.cra_priority	= 400,
		.base.cra_flags		= CRYPTO_ALG_INTERNAL,
		.base.cra_blocksize	= TF_BLOCK_SIZE,
		.base.cra_ctxsize	= sizeof(struct twofish_ctx),
		.base.cra_module	= THIS_MODULE,
		.min_keysize		= TF_MIN_KEY_SIZE,
		.max_keysize		= TF_MAX_KEY_SIZE,
		.setkey			= twofish_setkey_skcipher,
		.encrypt		= ecb_encrypt,
		.decrypt		= ecb_decrypt,
	}, {
		.base.cra_name		= "__cbc(twofish)",
		.base.cra_driver_name	= "__cbc-twofish-avx",
		.base.cra_priority	= 400,
		.base.cra_flags		= CRYPTO_ALG_INTERNAL,
		.base.cra_blocksize	= TF_BLOCK_SIZE,
		.base.cra_ctxsize	= sizeof(struct twofish_ctx),
		.base.cra_module	= THIS_MODULE,
		.min_keysize		= TF_MIN_KEY_SIZE,
		.max_keysize		= TF_MAX_KEY_SIZE,
		.ivsize			= TF_BLOCK_SIZE,
		.setkey			= twofish_setkey_skcipher,
		.encrypt		= cbc_encrypt,
		.decrypt		= cbc_decrypt,
	},
};

static struct simd_skcipher_alg *twofish_simd_algs[ARRAY_SIZE(twofish_algs)];

static int __init twofish_init(void)
{
	const char *feature_name;

	if (!cpu_has_xfeatures(XFEATURE_MASK_SSE | XFEATURE_MASK_YMM, &feature_name)) {
		pr_info("CPU feature '%s' is not supported.\n", feature_name);
		return -ENODEV;
	}

	return simd_register_skciphers_compat(twofish_algs,
					      ARRAY_SIZE(twofish_algs),
					      twofish_simd_algs);
}

static void __exit twofish_exit(void)
{
	simd_unregister_skciphers(twofish_algs, ARRAY_SIZE(twofish_algs),
				  twofish_simd_algs);
}

module_init(twofish_init);
module_exit(twofish_exit);

MODULE_DESCRIPTION("Twofish Cipher Algorithm, AVX optimized");
MODULE_LICENSE("GPL");
MODULE_ALIAS_CRYPTO("twofish");
