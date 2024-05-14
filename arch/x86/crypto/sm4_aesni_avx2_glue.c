/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * SM4 Cipher Algorithm, AES-NI/AVX2 optimized.
 * as specified in
 * https://tools.ietf.org/id/draft-ribose-cfrg-sm4-10.html
 *
 * Copyright (c) 2021, Alibaba Group.
 * Copyright (c) 2021 Tianjia Zhang <tianjia.zhang@linux.alibaba.com>
 */

#include <linux/module.h>
#include <linux/crypto.h>
#include <linux/kernel.h>
#include <asm/simd.h>
#include <crypto/internal/simd.h>
#include <crypto/internal/skcipher.h>
#include <crypto/sm4.h>
#include "sm4-avx.h"

#define SM4_CRYPT16_BLOCK_SIZE	(SM4_BLOCK_SIZE * 16)

asmlinkage void sm4_aesni_avx2_ctr_enc_blk16(const u32 *rk, u8 *dst,
					const u8 *src, u8 *iv);
asmlinkage void sm4_aesni_avx2_cbc_dec_blk16(const u32 *rk, u8 *dst,
					const u8 *src, u8 *iv);
asmlinkage void sm4_aesni_avx2_cfb_dec_blk16(const u32 *rk, u8 *dst,
					const u8 *src, u8 *iv);

static int sm4_skcipher_setkey(struct crypto_skcipher *tfm, const u8 *key,
			unsigned int key_len)
{
	struct sm4_ctx *ctx = crypto_skcipher_ctx(tfm);

	return sm4_expandkey(ctx, key, key_len);
}

static int cbc_decrypt(struct skcipher_request *req)
{
	return sm4_avx_cbc_decrypt(req, SM4_CRYPT16_BLOCK_SIZE,
				sm4_aesni_avx2_cbc_dec_blk16);
}


static int cfb_decrypt(struct skcipher_request *req)
{
	return sm4_avx_cfb_decrypt(req, SM4_CRYPT16_BLOCK_SIZE,
				sm4_aesni_avx2_cfb_dec_blk16);
}

static int ctr_crypt(struct skcipher_request *req)
{
	return sm4_avx_ctr_crypt(req, SM4_CRYPT16_BLOCK_SIZE,
				sm4_aesni_avx2_ctr_enc_blk16);
}

static struct skcipher_alg sm4_aesni_avx2_skciphers[] = {
	{
		.base = {
			.cra_name		= "__ecb(sm4)",
			.cra_driver_name	= "__ecb-sm4-aesni-avx2",
			.cra_priority		= 500,
			.cra_flags		= CRYPTO_ALG_INTERNAL,
			.cra_blocksize		= SM4_BLOCK_SIZE,
			.cra_ctxsize		= sizeof(struct sm4_ctx),
			.cra_module		= THIS_MODULE,
		},
		.min_keysize	= SM4_KEY_SIZE,
		.max_keysize	= SM4_KEY_SIZE,
		.walksize	= 16 * SM4_BLOCK_SIZE,
		.setkey		= sm4_skcipher_setkey,
		.encrypt	= sm4_avx_ecb_encrypt,
		.decrypt	= sm4_avx_ecb_decrypt,
	}, {
		.base = {
			.cra_name		= "__cbc(sm4)",
			.cra_driver_name	= "__cbc-sm4-aesni-avx2",
			.cra_priority		= 500,
			.cra_flags		= CRYPTO_ALG_INTERNAL,
			.cra_blocksize		= SM4_BLOCK_SIZE,
			.cra_ctxsize		= sizeof(struct sm4_ctx),
			.cra_module		= THIS_MODULE,
		},
		.min_keysize	= SM4_KEY_SIZE,
		.max_keysize	= SM4_KEY_SIZE,
		.ivsize		= SM4_BLOCK_SIZE,
		.walksize	= 16 * SM4_BLOCK_SIZE,
		.setkey		= sm4_skcipher_setkey,
		.encrypt	= sm4_cbc_encrypt,
		.decrypt	= cbc_decrypt,
	}, {
		.base = {
			.cra_name		= "__cfb(sm4)",
			.cra_driver_name	= "__cfb-sm4-aesni-avx2",
			.cra_priority		= 500,
			.cra_flags		= CRYPTO_ALG_INTERNAL,
			.cra_blocksize		= 1,
			.cra_ctxsize		= sizeof(struct sm4_ctx),
			.cra_module		= THIS_MODULE,
		},
		.min_keysize	= SM4_KEY_SIZE,
		.max_keysize	= SM4_KEY_SIZE,
		.ivsize		= SM4_BLOCK_SIZE,
		.chunksize	= SM4_BLOCK_SIZE,
		.walksize	= 16 * SM4_BLOCK_SIZE,
		.setkey		= sm4_skcipher_setkey,
		.encrypt	= sm4_cfb_encrypt,
		.decrypt	= cfb_decrypt,
	}, {
		.base = {
			.cra_name		= "__ctr(sm4)",
			.cra_driver_name	= "__ctr-sm4-aesni-avx2",
			.cra_priority		= 500,
			.cra_flags		= CRYPTO_ALG_INTERNAL,
			.cra_blocksize		= 1,
			.cra_ctxsize		= sizeof(struct sm4_ctx),
			.cra_module		= THIS_MODULE,
		},
		.min_keysize	= SM4_KEY_SIZE,
		.max_keysize	= SM4_KEY_SIZE,
		.ivsize		= SM4_BLOCK_SIZE,
		.chunksize	= SM4_BLOCK_SIZE,
		.walksize	= 16 * SM4_BLOCK_SIZE,
		.setkey		= sm4_skcipher_setkey,
		.encrypt	= ctr_crypt,
		.decrypt	= ctr_crypt,
	}
};

static struct simd_skcipher_alg *
simd_sm4_aesni_avx2_skciphers[ARRAY_SIZE(sm4_aesni_avx2_skciphers)];

static int __init sm4_init(void)
{
	const char *feature_name;

	if (!boot_cpu_has(X86_FEATURE_AVX) ||
	    !boot_cpu_has(X86_FEATURE_AVX2) ||
	    !boot_cpu_has(X86_FEATURE_AES) ||
	    !boot_cpu_has(X86_FEATURE_OSXSAVE)) {
		pr_info("AVX2 or AES-NI instructions are not detected.\n");
		return -ENODEV;
	}

	if (!cpu_has_xfeatures(XFEATURE_MASK_SSE | XFEATURE_MASK_YMM,
				&feature_name)) {
		pr_info("CPU feature '%s' is not supported.\n", feature_name);
		return -ENODEV;
	}

	return simd_register_skciphers_compat(sm4_aesni_avx2_skciphers,
					ARRAY_SIZE(sm4_aesni_avx2_skciphers),
					simd_sm4_aesni_avx2_skciphers);
}

static void __exit sm4_exit(void)
{
	simd_unregister_skciphers(sm4_aesni_avx2_skciphers,
				ARRAY_SIZE(sm4_aesni_avx2_skciphers),
				simd_sm4_aesni_avx2_skciphers);
}

module_init(sm4_init);
module_exit(sm4_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Tianjia Zhang <tianjia.zhang@linux.alibaba.com>");
MODULE_DESCRIPTION("SM4 Cipher Algorithm, AES-NI/AVX2 optimized");
MODULE_ALIAS_CRYPTO("sm4");
MODULE_ALIAS_CRYPTO("sm4-aesni-avx2");
