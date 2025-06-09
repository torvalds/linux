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

#include "ecb_cbc_helpers.h"

#define CAST6_PARALLEL_BLOCKS 8

asmlinkage void cast6_ecb_enc_8way(const void *ctx, u8 *dst, const u8 *src);
asmlinkage void cast6_ecb_dec_8way(const void *ctx, u8 *dst, const u8 *src);

asmlinkage void cast6_cbc_dec_8way(const void *ctx, u8 *dst, const u8 *src);

static int cast6_setkey_skcipher(struct crypto_skcipher *tfm,
				 const u8 *key, unsigned int keylen)
{
	return cast6_setkey(&tfm->base, key, keylen);
}

static int ecb_encrypt(struct skcipher_request *req)
{
	ECB_WALK_START(req, CAST6_BLOCK_SIZE, CAST6_PARALLEL_BLOCKS);
	ECB_BLOCK(CAST6_PARALLEL_BLOCKS, cast6_ecb_enc_8way);
	ECB_BLOCK(1, __cast6_encrypt);
	ECB_WALK_END();
}

static int ecb_decrypt(struct skcipher_request *req)
{
	ECB_WALK_START(req, CAST6_BLOCK_SIZE, CAST6_PARALLEL_BLOCKS);
	ECB_BLOCK(CAST6_PARALLEL_BLOCKS, cast6_ecb_dec_8way);
	ECB_BLOCK(1, __cast6_decrypt);
	ECB_WALK_END();
}

static int cbc_encrypt(struct skcipher_request *req)
{
	CBC_WALK_START(req, CAST6_BLOCK_SIZE, -1);
	CBC_ENC_BLOCK(__cast6_encrypt);
	CBC_WALK_END();
}

static int cbc_decrypt(struct skcipher_request *req)
{
	CBC_WALK_START(req, CAST6_BLOCK_SIZE, CAST6_PARALLEL_BLOCKS);
	CBC_DEC_BLOCK(CAST6_PARALLEL_BLOCKS, cast6_cbc_dec_8way);
	CBC_DEC_BLOCK(1, __cast6_decrypt);
	CBC_WALK_END();
}

static struct skcipher_alg cast6_algs[] = {
	{
		.base.cra_name		= "ecb(cast6)",
		.base.cra_driver_name	= "ecb-cast6-avx",
		.base.cra_priority	= 200,
		.base.cra_blocksize	= CAST6_BLOCK_SIZE,
		.base.cra_ctxsize	= sizeof(struct cast6_ctx),
		.base.cra_module	= THIS_MODULE,
		.min_keysize		= CAST6_MIN_KEY_SIZE,
		.max_keysize		= CAST6_MAX_KEY_SIZE,
		.setkey			= cast6_setkey_skcipher,
		.encrypt		= ecb_encrypt,
		.decrypt		= ecb_decrypt,
	}, {
		.base.cra_name		= "cbc(cast6)",
		.base.cra_driver_name	= "cbc-cast6-avx",
		.base.cra_priority	= 200,
		.base.cra_blocksize	= CAST6_BLOCK_SIZE,
		.base.cra_ctxsize	= sizeof(struct cast6_ctx),
		.base.cra_module	= THIS_MODULE,
		.min_keysize		= CAST6_MIN_KEY_SIZE,
		.max_keysize		= CAST6_MAX_KEY_SIZE,
		.ivsize			= CAST6_BLOCK_SIZE,
		.setkey			= cast6_setkey_skcipher,
		.encrypt		= cbc_encrypt,
		.decrypt		= cbc_decrypt,
	},
};

static int __init cast6_init(void)
{
	const char *feature_name;

	if (!cpu_has_xfeatures(XFEATURE_MASK_SSE | XFEATURE_MASK_YMM,
				&feature_name)) {
		pr_info("CPU feature '%s' is not supported.\n", feature_name);
		return -ENODEV;
	}

	return crypto_register_skciphers(cast6_algs, ARRAY_SIZE(cast6_algs));
}

static void __exit cast6_exit(void)
{
	crypto_unregister_skciphers(cast6_algs, ARRAY_SIZE(cast6_algs));
}

module_init(cast6_init);
module_exit(cast6_exit);

MODULE_DESCRIPTION("Cast6 Cipher Algorithm, AVX optimized");
MODULE_LICENSE("GPL");
MODULE_ALIAS_CRYPTO("cast6");
