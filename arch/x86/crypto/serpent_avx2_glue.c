// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Glue Code for x86_64/AVX2 assembler optimized version of Serpent
 *
 * Copyright © 2012-2013 Jussi Kivilinna <jussi.kivilinna@mbnet.fi>
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/crypto.h>
#include <linux/err.h>
#include <crypto/algapi.h>
#include <crypto/serpent.h>

#include "serpent-avx.h"
#include "ecb_cbc_helpers.h"

#define SERPENT_AVX2_PARALLEL_BLOCKS 16

/* 16-way AVX2 parallel cipher functions */
asmlinkage void serpent_ecb_enc_16way(const void *ctx, u8 *dst, const u8 *src);
asmlinkage void serpent_ecb_dec_16way(const void *ctx, u8 *dst, const u8 *src);
asmlinkage void serpent_cbc_dec_16way(const void *ctx, u8 *dst, const u8 *src);

static int serpent_setkey_skcipher(struct crypto_skcipher *tfm,
				   const u8 *key, unsigned int keylen)
{
	return __serpent_setkey(crypto_skcipher_ctx(tfm), key, keylen);
}

static int ecb_encrypt(struct skcipher_request *req)
{
	ECB_WALK_START(req, SERPENT_BLOCK_SIZE, SERPENT_PARALLEL_BLOCKS);
	ECB_BLOCK(SERPENT_AVX2_PARALLEL_BLOCKS, serpent_ecb_enc_16way);
	ECB_BLOCK(SERPENT_PARALLEL_BLOCKS, serpent_ecb_enc_8way_avx);
	ECB_BLOCK(1, __serpent_encrypt);
	ECB_WALK_END();
}

static int ecb_decrypt(struct skcipher_request *req)
{
	ECB_WALK_START(req, SERPENT_BLOCK_SIZE, SERPENT_PARALLEL_BLOCKS);
	ECB_BLOCK(SERPENT_AVX2_PARALLEL_BLOCKS, serpent_ecb_dec_16way);
	ECB_BLOCK(SERPENT_PARALLEL_BLOCKS, serpent_ecb_dec_8way_avx);
	ECB_BLOCK(1, __serpent_decrypt);
	ECB_WALK_END();
}

static int cbc_encrypt(struct skcipher_request *req)
{
	CBC_WALK_START(req, SERPENT_BLOCK_SIZE, -1);
	CBC_ENC_BLOCK(__serpent_encrypt);
	CBC_WALK_END();
}

static int cbc_decrypt(struct skcipher_request *req)
{
	CBC_WALK_START(req, SERPENT_BLOCK_SIZE, SERPENT_PARALLEL_BLOCKS);
	CBC_DEC_BLOCK(SERPENT_AVX2_PARALLEL_BLOCKS, serpent_cbc_dec_16way);
	CBC_DEC_BLOCK(SERPENT_PARALLEL_BLOCKS, serpent_cbc_dec_8way_avx);
	CBC_DEC_BLOCK(1, __serpent_decrypt);
	CBC_WALK_END();
}

static struct skcipher_alg serpent_algs[] = {
	{
		.base.cra_name		= "ecb(serpent)",
		.base.cra_driver_name	= "ecb-serpent-avx2",
		.base.cra_priority	= 600,
		.base.cra_blocksize	= SERPENT_BLOCK_SIZE,
		.base.cra_ctxsize	= sizeof(struct serpent_ctx),
		.base.cra_module	= THIS_MODULE,
		.min_keysize		= SERPENT_MIN_KEY_SIZE,
		.max_keysize		= SERPENT_MAX_KEY_SIZE,
		.setkey			= serpent_setkey_skcipher,
		.encrypt		= ecb_encrypt,
		.decrypt		= ecb_decrypt,
	}, {
		.base.cra_name		= "cbc(serpent)",
		.base.cra_driver_name	= "cbc-serpent-avx2",
		.base.cra_priority	= 600,
		.base.cra_blocksize	= SERPENT_BLOCK_SIZE,
		.base.cra_ctxsize	= sizeof(struct serpent_ctx),
		.base.cra_module	= THIS_MODULE,
		.min_keysize		= SERPENT_MIN_KEY_SIZE,
		.max_keysize		= SERPENT_MAX_KEY_SIZE,
		.ivsize			= SERPENT_BLOCK_SIZE,
		.setkey			= serpent_setkey_skcipher,
		.encrypt		= cbc_encrypt,
		.decrypt		= cbc_decrypt,
	},
};

static int __init serpent_avx2_init(void)
{
	const char *feature_name;

	if (!boot_cpu_has(X86_FEATURE_AVX2) || !boot_cpu_has(X86_FEATURE_OSXSAVE)) {
		pr_info("AVX2 instructions are not detected.\n");
		return -ENODEV;
	}
	if (!cpu_has_xfeatures(XFEATURE_MASK_SSE | XFEATURE_MASK_YMM,
				&feature_name)) {
		pr_info("CPU feature '%s' is not supported.\n", feature_name);
		return -ENODEV;
	}

	return crypto_register_skciphers(serpent_algs,
					 ARRAY_SIZE(serpent_algs));
}

static void __exit serpent_avx2_fini(void)
{
	crypto_unregister_skciphers(serpent_algs, ARRAY_SIZE(serpent_algs));
}

module_init(serpent_avx2_init);
module_exit(serpent_avx2_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Serpent Cipher Algorithm, AVX2 optimized");
MODULE_ALIAS_CRYPTO("serpent");
MODULE_ALIAS_CRYPTO("serpent-asm");
