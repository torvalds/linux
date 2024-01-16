// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Glue Code for x86_64/AVX/AES-NI assembler optimized version of Camellia
 *
 * Copyright © 2012-2013 Jussi Kivilinna <jussi.kivilinna@iki.fi>
 */

#include <crypto/algapi.h>
#include <crypto/internal/simd.h>
#include <linux/crypto.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/types.h>

#include "camellia.h"
#include "ecb_cbc_helpers.h"

#define CAMELLIA_AESNI_PARALLEL_BLOCKS 16

/* 16-way parallel cipher functions (avx/aes-ni) */
asmlinkage void camellia_ecb_enc_16way(const void *ctx, u8 *dst, const u8 *src);
EXPORT_SYMBOL_GPL(camellia_ecb_enc_16way);

asmlinkage void camellia_ecb_dec_16way(const void *ctx, u8 *dst, const u8 *src);
EXPORT_SYMBOL_GPL(camellia_ecb_dec_16way);

asmlinkage void camellia_cbc_dec_16way(const void *ctx, u8 *dst, const u8 *src);
EXPORT_SYMBOL_GPL(camellia_cbc_dec_16way);

static int camellia_setkey(struct crypto_skcipher *tfm, const u8 *key,
			   unsigned int keylen)
{
	return __camellia_setkey(crypto_skcipher_ctx(tfm), key, keylen);
}

static int ecb_encrypt(struct skcipher_request *req)
{
	ECB_WALK_START(req, CAMELLIA_BLOCK_SIZE, CAMELLIA_AESNI_PARALLEL_BLOCKS);
	ECB_BLOCK(CAMELLIA_AESNI_PARALLEL_BLOCKS, camellia_ecb_enc_16way);
	ECB_BLOCK(2, camellia_enc_blk_2way);
	ECB_BLOCK(1, camellia_enc_blk);
	ECB_WALK_END();
}

static int ecb_decrypt(struct skcipher_request *req)
{
	ECB_WALK_START(req, CAMELLIA_BLOCK_SIZE, CAMELLIA_AESNI_PARALLEL_BLOCKS);
	ECB_BLOCK(CAMELLIA_AESNI_PARALLEL_BLOCKS, camellia_ecb_dec_16way);
	ECB_BLOCK(2, camellia_dec_blk_2way);
	ECB_BLOCK(1, camellia_dec_blk);
	ECB_WALK_END();
}

static int cbc_encrypt(struct skcipher_request *req)
{
	CBC_WALK_START(req, CAMELLIA_BLOCK_SIZE, -1);
	CBC_ENC_BLOCK(camellia_enc_blk);
	CBC_WALK_END();
}

static int cbc_decrypt(struct skcipher_request *req)
{
	CBC_WALK_START(req, CAMELLIA_BLOCK_SIZE, CAMELLIA_AESNI_PARALLEL_BLOCKS);
	CBC_DEC_BLOCK(CAMELLIA_AESNI_PARALLEL_BLOCKS, camellia_cbc_dec_16way);
	CBC_DEC_BLOCK(2, camellia_decrypt_cbc_2way);
	CBC_DEC_BLOCK(1, camellia_dec_blk);
	CBC_WALK_END();
}

static struct skcipher_alg camellia_algs[] = {
	{
		.base.cra_name		= "__ecb(camellia)",
		.base.cra_driver_name	= "__ecb-camellia-aesni",
		.base.cra_priority	= 400,
		.base.cra_flags		= CRYPTO_ALG_INTERNAL,
		.base.cra_blocksize	= CAMELLIA_BLOCK_SIZE,
		.base.cra_ctxsize	= sizeof(struct camellia_ctx),
		.base.cra_module	= THIS_MODULE,
		.min_keysize		= CAMELLIA_MIN_KEY_SIZE,
		.max_keysize		= CAMELLIA_MAX_KEY_SIZE,
		.setkey			= camellia_setkey,
		.encrypt		= ecb_encrypt,
		.decrypt		= ecb_decrypt,
	}, {
		.base.cra_name		= "__cbc(camellia)",
		.base.cra_driver_name	= "__cbc-camellia-aesni",
		.base.cra_priority	= 400,
		.base.cra_flags		= CRYPTO_ALG_INTERNAL,
		.base.cra_blocksize	= CAMELLIA_BLOCK_SIZE,
		.base.cra_ctxsize	= sizeof(struct camellia_ctx),
		.base.cra_module	= THIS_MODULE,
		.min_keysize		= CAMELLIA_MIN_KEY_SIZE,
		.max_keysize		= CAMELLIA_MAX_KEY_SIZE,
		.ivsize			= CAMELLIA_BLOCK_SIZE,
		.setkey			= camellia_setkey,
		.encrypt		= cbc_encrypt,
		.decrypt		= cbc_decrypt,
	}
};

static struct simd_skcipher_alg *camellia_simd_algs[ARRAY_SIZE(camellia_algs)];

static int __init camellia_aesni_init(void)
{
	const char *feature_name;

	if (!boot_cpu_has(X86_FEATURE_AVX) ||
	    !boot_cpu_has(X86_FEATURE_AES) ||
	    !boot_cpu_has(X86_FEATURE_OSXSAVE)) {
		pr_info("AVX or AES-NI instructions are not detected.\n");
		return -ENODEV;
	}

	if (!cpu_has_xfeatures(XFEATURE_MASK_SSE | XFEATURE_MASK_YMM,
				&feature_name)) {
		pr_info("CPU feature '%s' is not supported.\n", feature_name);
		return -ENODEV;
	}

	return simd_register_skciphers_compat(camellia_algs,
					      ARRAY_SIZE(camellia_algs),
					      camellia_simd_algs);
}

static void __exit camellia_aesni_fini(void)
{
	simd_unregister_skciphers(camellia_algs, ARRAY_SIZE(camellia_algs),
				  camellia_simd_algs);
}

module_init(camellia_aesni_init);
module_exit(camellia_aesni_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Camellia Cipher Algorithm, AES-NI/AVX optimized");
MODULE_ALIAS_CRYPTO("camellia");
MODULE_ALIAS_CRYPTO("camellia-asm");
