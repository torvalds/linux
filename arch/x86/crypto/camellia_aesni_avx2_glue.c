// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Glue Code for x86_64/AVX2/AES-NI assembler optimized version of Camellia
 *
 * Copyright © 2013 Jussi Kivilinna <jussi.kivilinna@mbnet.fi>
 */

#include <asm/crypto/camellia.h>
#include <asm/crypto/glue_helper.h>
#include <crypto/algapi.h>
#include <crypto/internal/simd.h>
#include <crypto/xts.h>
#include <linux/crypto.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/types.h>

#define CAMELLIA_AESNI_PARALLEL_BLOCKS 16
#define CAMELLIA_AESNI_AVX2_PARALLEL_BLOCKS 32

/* 32-way AVX2/AES-NI parallel cipher functions */
asmlinkage void camellia_ecb_enc_32way(const void *ctx, u8 *dst, const u8 *src);
asmlinkage void camellia_ecb_dec_32way(const void *ctx, u8 *dst, const u8 *src);

asmlinkage void camellia_cbc_dec_32way(const void *ctx, u8 *dst, const u8 *src);
asmlinkage void camellia_ctr_32way(const void *ctx, u8 *dst, const u8 *src,
				   le128 *iv);

asmlinkage void camellia_xts_enc_32way(const void *ctx, u8 *dst, const u8 *src,
				       le128 *iv);
asmlinkage void camellia_xts_dec_32way(const void *ctx, u8 *dst, const u8 *src,
				       le128 *iv);

static const struct common_glue_ctx camellia_enc = {
	.num_funcs = 4,
	.fpu_blocks_limit = CAMELLIA_AESNI_PARALLEL_BLOCKS,

	.funcs = { {
		.num_blocks = CAMELLIA_AESNI_AVX2_PARALLEL_BLOCKS,
		.fn_u = { .ecb = camellia_ecb_enc_32way }
	}, {
		.num_blocks = CAMELLIA_AESNI_PARALLEL_BLOCKS,
		.fn_u = { .ecb = camellia_ecb_enc_16way }
	}, {
		.num_blocks = 2,
		.fn_u = { .ecb = camellia_enc_blk_2way }
	}, {
		.num_blocks = 1,
		.fn_u = { .ecb = camellia_enc_blk }
	} }
};

static const struct common_glue_ctx camellia_ctr = {
	.num_funcs = 4,
	.fpu_blocks_limit = CAMELLIA_AESNI_PARALLEL_BLOCKS,

	.funcs = { {
		.num_blocks = CAMELLIA_AESNI_AVX2_PARALLEL_BLOCKS,
		.fn_u = { .ctr = camellia_ctr_32way }
	}, {
		.num_blocks = CAMELLIA_AESNI_PARALLEL_BLOCKS,
		.fn_u = { .ctr = camellia_ctr_16way }
	}, {
		.num_blocks = 2,
		.fn_u = { .ctr = camellia_crypt_ctr_2way }
	}, {
		.num_blocks = 1,
		.fn_u = { .ctr = camellia_crypt_ctr }
	} }
};

static const struct common_glue_ctx camellia_enc_xts = {
	.num_funcs = 3,
	.fpu_blocks_limit = CAMELLIA_AESNI_PARALLEL_BLOCKS,

	.funcs = { {
		.num_blocks = CAMELLIA_AESNI_AVX2_PARALLEL_BLOCKS,
		.fn_u = { .xts = camellia_xts_enc_32way }
	}, {
		.num_blocks = CAMELLIA_AESNI_PARALLEL_BLOCKS,
		.fn_u = { .xts = camellia_xts_enc_16way }
	}, {
		.num_blocks = 1,
		.fn_u = { .xts = camellia_xts_enc }
	} }
};

static const struct common_glue_ctx camellia_dec = {
	.num_funcs = 4,
	.fpu_blocks_limit = CAMELLIA_AESNI_PARALLEL_BLOCKS,

	.funcs = { {
		.num_blocks = CAMELLIA_AESNI_AVX2_PARALLEL_BLOCKS,
		.fn_u = { .ecb = camellia_ecb_dec_32way }
	}, {
		.num_blocks = CAMELLIA_AESNI_PARALLEL_BLOCKS,
		.fn_u = { .ecb = camellia_ecb_dec_16way }
	}, {
		.num_blocks = 2,
		.fn_u = { .ecb = camellia_dec_blk_2way }
	}, {
		.num_blocks = 1,
		.fn_u = { .ecb = camellia_dec_blk }
	} }
};

static const struct common_glue_ctx camellia_dec_cbc = {
	.num_funcs = 4,
	.fpu_blocks_limit = CAMELLIA_AESNI_PARALLEL_BLOCKS,

	.funcs = { {
		.num_blocks = CAMELLIA_AESNI_AVX2_PARALLEL_BLOCKS,
		.fn_u = { .cbc = camellia_cbc_dec_32way }
	}, {
		.num_blocks = CAMELLIA_AESNI_PARALLEL_BLOCKS,
		.fn_u = { .cbc = camellia_cbc_dec_16way }
	}, {
		.num_blocks = 2,
		.fn_u = { .cbc = camellia_decrypt_cbc_2way }
	}, {
		.num_blocks = 1,
		.fn_u = { .cbc = camellia_dec_blk }
	} }
};

static const struct common_glue_ctx camellia_dec_xts = {
	.num_funcs = 3,
	.fpu_blocks_limit = CAMELLIA_AESNI_PARALLEL_BLOCKS,

	.funcs = { {
		.num_blocks = CAMELLIA_AESNI_AVX2_PARALLEL_BLOCKS,
		.fn_u = { .xts = camellia_xts_dec_32way }
	}, {
		.num_blocks = CAMELLIA_AESNI_PARALLEL_BLOCKS,
		.fn_u = { .xts = camellia_xts_dec_16way }
	}, {
		.num_blocks = 1,
		.fn_u = { .xts = camellia_xts_dec }
	} }
};

static int camellia_setkey(struct crypto_skcipher *tfm, const u8 *key,
			   unsigned int keylen)
{
	return __camellia_setkey(crypto_skcipher_ctx(tfm), key, keylen);
}

static int ecb_encrypt(struct skcipher_request *req)
{
	return glue_ecb_req_128bit(&camellia_enc, req);
}

static int ecb_decrypt(struct skcipher_request *req)
{
	return glue_ecb_req_128bit(&camellia_dec, req);
}

static int cbc_encrypt(struct skcipher_request *req)
{
	return glue_cbc_encrypt_req_128bit(camellia_enc_blk, req);
}

static int cbc_decrypt(struct skcipher_request *req)
{
	return glue_cbc_decrypt_req_128bit(&camellia_dec_cbc, req);
}

static int ctr_crypt(struct skcipher_request *req)
{
	return glue_ctr_req_128bit(&camellia_ctr, req);
}

static int xts_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct camellia_xts_ctx *ctx = crypto_skcipher_ctx(tfm);

	return glue_xts_req_128bit(&camellia_enc_xts, req, camellia_enc_blk,
				   &ctx->tweak_ctx, &ctx->crypt_ctx, false);
}

static int xts_decrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct camellia_xts_ctx *ctx = crypto_skcipher_ctx(tfm);

	return glue_xts_req_128bit(&camellia_dec_xts, req, camellia_enc_blk,
				   &ctx->tweak_ctx, &ctx->crypt_ctx, true);
}

static struct skcipher_alg camellia_algs[] = {
	{
		.base.cra_name		= "__ecb(camellia)",
		.base.cra_driver_name	= "__ecb-camellia-aesni-avx2",
		.base.cra_priority	= 500,
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
		.base.cra_driver_name	= "__cbc-camellia-aesni-avx2",
		.base.cra_priority	= 500,
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
	}, {
		.base.cra_name		= "__ctr(camellia)",
		.base.cra_driver_name	= "__ctr-camellia-aesni-avx2",
		.base.cra_priority	= 500,
		.base.cra_flags		= CRYPTO_ALG_INTERNAL,
		.base.cra_blocksize	= 1,
		.base.cra_ctxsize	= sizeof(struct camellia_ctx),
		.base.cra_module	= THIS_MODULE,
		.min_keysize		= CAMELLIA_MIN_KEY_SIZE,
		.max_keysize		= CAMELLIA_MAX_KEY_SIZE,
		.ivsize			= CAMELLIA_BLOCK_SIZE,
		.chunksize		= CAMELLIA_BLOCK_SIZE,
		.setkey			= camellia_setkey,
		.encrypt		= ctr_crypt,
		.decrypt		= ctr_crypt,
	}, {
		.base.cra_name		= "__xts(camellia)",
		.base.cra_driver_name	= "__xts-camellia-aesni-avx2",
		.base.cra_priority	= 500,
		.base.cra_flags		= CRYPTO_ALG_INTERNAL,
		.base.cra_blocksize	= CAMELLIA_BLOCK_SIZE,
		.base.cra_ctxsize	= sizeof(struct camellia_xts_ctx),
		.base.cra_module	= THIS_MODULE,
		.min_keysize		= 2 * CAMELLIA_MIN_KEY_SIZE,
		.max_keysize		= 2 * CAMELLIA_MAX_KEY_SIZE,
		.ivsize			= CAMELLIA_BLOCK_SIZE,
		.setkey			= xts_camellia_setkey,
		.encrypt		= xts_encrypt,
		.decrypt		= xts_decrypt,
	},
};

static struct simd_skcipher_alg *camellia_simd_algs[ARRAY_SIZE(camellia_algs)];

static int __init camellia_aesni_init(void)
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
MODULE_DESCRIPTION("Camellia Cipher Algorithm, AES-NI/AVX2 optimized");
MODULE_ALIAS_CRYPTO("camellia");
MODULE_ALIAS_CRYPTO("camellia-asm");
