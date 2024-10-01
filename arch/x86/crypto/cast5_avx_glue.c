// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Glue Code for the AVX assembler implementation of the Cast5 Cipher
 *
 * Copyright (C) 2012 Johannes Goetzfried
 *     <Johannes.Goetzfried@informatik.stud.uni-erlangen.de>
 */

#include <crypto/algapi.h>
#include <crypto/cast5.h>
#include <crypto/internal/simd.h>
#include <linux/crypto.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/types.h>

#include "ecb_cbc_helpers.h"

#define CAST5_PARALLEL_BLOCKS 16

asmlinkage void cast5_ecb_enc_16way(struct cast5_ctx *ctx, u8 *dst,
				    const u8 *src);
asmlinkage void cast5_ecb_dec_16way(struct cast5_ctx *ctx, u8 *dst,
				    const u8 *src);
asmlinkage void cast5_cbc_dec_16way(struct cast5_ctx *ctx, u8 *dst,
				    const u8 *src);

static int cast5_setkey_skcipher(struct crypto_skcipher *tfm, const u8 *key,
				 unsigned int keylen)
{
	return cast5_setkey(&tfm->base, key, keylen);
}

static int ecb_encrypt(struct skcipher_request *req)
{
	ECB_WALK_START(req, CAST5_BLOCK_SIZE, CAST5_PARALLEL_BLOCKS);
	ECB_BLOCK(CAST5_PARALLEL_BLOCKS, cast5_ecb_enc_16way);
	ECB_BLOCK(1, __cast5_encrypt);
	ECB_WALK_END();
}

static int ecb_decrypt(struct skcipher_request *req)
{
	ECB_WALK_START(req, CAST5_BLOCK_SIZE, CAST5_PARALLEL_BLOCKS);
	ECB_BLOCK(CAST5_PARALLEL_BLOCKS, cast5_ecb_dec_16way);
	ECB_BLOCK(1, __cast5_decrypt);
	ECB_WALK_END();
}

static int cbc_encrypt(struct skcipher_request *req)
{
	CBC_WALK_START(req, CAST5_BLOCK_SIZE, -1);
	CBC_ENC_BLOCK(__cast5_encrypt);
	CBC_WALK_END();
}

static int cbc_decrypt(struct skcipher_request *req)
{
	CBC_WALK_START(req, CAST5_BLOCK_SIZE, CAST5_PARALLEL_BLOCKS);
	CBC_DEC_BLOCK(CAST5_PARALLEL_BLOCKS, cast5_cbc_dec_16way);
	CBC_DEC_BLOCK(1, __cast5_decrypt);
	CBC_WALK_END();
}

static struct skcipher_alg cast5_algs[] = {
	{
		.base.cra_name		= "__ecb(cast5)",
		.base.cra_driver_name	= "__ecb-cast5-avx",
		.base.cra_priority	= 200,
		.base.cra_flags		= CRYPTO_ALG_INTERNAL,
		.base.cra_blocksize	= CAST5_BLOCK_SIZE,
		.base.cra_ctxsize	= sizeof(struct cast5_ctx),
		.base.cra_module	= THIS_MODULE,
		.min_keysize		= CAST5_MIN_KEY_SIZE,
		.max_keysize		= CAST5_MAX_KEY_SIZE,
		.setkey			= cast5_setkey_skcipher,
		.encrypt		= ecb_encrypt,
		.decrypt		= ecb_decrypt,
	}, {
		.base.cra_name		= "__cbc(cast5)",
		.base.cra_driver_name	= "__cbc-cast5-avx",
		.base.cra_priority	= 200,
		.base.cra_flags		= CRYPTO_ALG_INTERNAL,
		.base.cra_blocksize	= CAST5_BLOCK_SIZE,
		.base.cra_ctxsize	= sizeof(struct cast5_ctx),
		.base.cra_module	= THIS_MODULE,
		.min_keysize		= CAST5_MIN_KEY_SIZE,
		.max_keysize		= CAST5_MAX_KEY_SIZE,
		.ivsize			= CAST5_BLOCK_SIZE,
		.setkey			= cast5_setkey_skcipher,
		.encrypt		= cbc_encrypt,
		.decrypt		= cbc_decrypt,
	}
};

static struct simd_skcipher_alg *cast5_simd_algs[ARRAY_SIZE(cast5_algs)];

static int __init cast5_init(void)
{
	const char *feature_name;

	if (!cpu_has_xfeatures(XFEATURE_MASK_SSE | XFEATURE_MASK_YMM,
				&feature_name)) {
		pr_info("CPU feature '%s' is not supported.\n", feature_name);
		return -ENODEV;
	}

	return simd_register_skciphers_compat(cast5_algs,
					      ARRAY_SIZE(cast5_algs),
					      cast5_simd_algs);
}

static void __exit cast5_exit(void)
{
	simd_unregister_skciphers(cast5_algs, ARRAY_SIZE(cast5_algs),
				  cast5_simd_algs);
}

module_init(cast5_init);
module_exit(cast5_exit);

MODULE_DESCRIPTION("Cast5 Cipher Algorithm, AVX optimized");
MODULE_LICENSE("GPL");
MODULE_ALIAS_CRYPTO("cast5");
