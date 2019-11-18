/*
 * x64 SIMD accelerated ChaCha and XChaCha stream ciphers,
 * including ChaCha20 (RFC7539)
 *
 * Copyright (C) 2015 Martin Willi
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <crypto/algapi.h>
#include <crypto/internal/chacha.h>
#include <crypto/internal/skcipher.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <asm/fpu/api.h>
#include <asm/simd.h>

#define CHACHA_STATE_ALIGN 16

asmlinkage void chacha_block_xor_ssse3(u32 *state, u8 *dst, const u8 *src,
				       unsigned int len, int nrounds);
asmlinkage void chacha_4block_xor_ssse3(u32 *state, u8 *dst, const u8 *src,
					unsigned int len, int nrounds);
asmlinkage void hchacha_block_ssse3(const u32 *state, u32 *out, int nrounds);

asmlinkage void chacha_2block_xor_avx2(u32 *state, u8 *dst, const u8 *src,
				       unsigned int len, int nrounds);
asmlinkage void chacha_4block_xor_avx2(u32 *state, u8 *dst, const u8 *src,
				       unsigned int len, int nrounds);
asmlinkage void chacha_8block_xor_avx2(u32 *state, u8 *dst, const u8 *src,
				       unsigned int len, int nrounds);

asmlinkage void chacha_2block_xor_avx512vl(u32 *state, u8 *dst, const u8 *src,
					   unsigned int len, int nrounds);
asmlinkage void chacha_4block_xor_avx512vl(u32 *state, u8 *dst, const u8 *src,
					   unsigned int len, int nrounds);
asmlinkage void chacha_8block_xor_avx512vl(u32 *state, u8 *dst, const u8 *src,
					   unsigned int len, int nrounds);

static __ro_after_init DEFINE_STATIC_KEY_FALSE(chacha_use_simd);
static __ro_after_init DEFINE_STATIC_KEY_FALSE(chacha_use_avx2);
static __ro_after_init DEFINE_STATIC_KEY_FALSE(chacha_use_avx512vl);

static unsigned int chacha_advance(unsigned int len, unsigned int maxblocks)
{
	len = min(len, maxblocks * CHACHA_BLOCK_SIZE);
	return round_up(len, CHACHA_BLOCK_SIZE) / CHACHA_BLOCK_SIZE;
}

static void chacha_dosimd(u32 *state, u8 *dst, const u8 *src,
			  unsigned int bytes, int nrounds)
{
	if (IS_ENABLED(CONFIG_AS_AVX512) &&
	    static_branch_likely(&chacha_use_avx512vl)) {
		while (bytes >= CHACHA_BLOCK_SIZE * 8) {
			chacha_8block_xor_avx512vl(state, dst, src, bytes,
						   nrounds);
			bytes -= CHACHA_BLOCK_SIZE * 8;
			src += CHACHA_BLOCK_SIZE * 8;
			dst += CHACHA_BLOCK_SIZE * 8;
			state[12] += 8;
		}
		if (bytes > CHACHA_BLOCK_SIZE * 4) {
			chacha_8block_xor_avx512vl(state, dst, src, bytes,
						   nrounds);
			state[12] += chacha_advance(bytes, 8);
			return;
		}
		if (bytes > CHACHA_BLOCK_SIZE * 2) {
			chacha_4block_xor_avx512vl(state, dst, src, bytes,
						   nrounds);
			state[12] += chacha_advance(bytes, 4);
			return;
		}
		if (bytes) {
			chacha_2block_xor_avx512vl(state, dst, src, bytes,
						   nrounds);
			state[12] += chacha_advance(bytes, 2);
			return;
		}
	}

	if (IS_ENABLED(CONFIG_AS_AVX2) &&
	    static_branch_likely(&chacha_use_avx2)) {
		while (bytes >= CHACHA_BLOCK_SIZE * 8) {
			chacha_8block_xor_avx2(state, dst, src, bytes, nrounds);
			bytes -= CHACHA_BLOCK_SIZE * 8;
			src += CHACHA_BLOCK_SIZE * 8;
			dst += CHACHA_BLOCK_SIZE * 8;
			state[12] += 8;
		}
		if (bytes > CHACHA_BLOCK_SIZE * 4) {
			chacha_8block_xor_avx2(state, dst, src, bytes, nrounds);
			state[12] += chacha_advance(bytes, 8);
			return;
		}
		if (bytes > CHACHA_BLOCK_SIZE * 2) {
			chacha_4block_xor_avx2(state, dst, src, bytes, nrounds);
			state[12] += chacha_advance(bytes, 4);
			return;
		}
		if (bytes > CHACHA_BLOCK_SIZE) {
			chacha_2block_xor_avx2(state, dst, src, bytes, nrounds);
			state[12] += chacha_advance(bytes, 2);
			return;
		}
	}

	while (bytes >= CHACHA_BLOCK_SIZE * 4) {
		chacha_4block_xor_ssse3(state, dst, src, bytes, nrounds);
		bytes -= CHACHA_BLOCK_SIZE * 4;
		src += CHACHA_BLOCK_SIZE * 4;
		dst += CHACHA_BLOCK_SIZE * 4;
		state[12] += 4;
	}
	if (bytes > CHACHA_BLOCK_SIZE) {
		chacha_4block_xor_ssse3(state, dst, src, bytes, nrounds);
		state[12] += chacha_advance(bytes, 4);
		return;
	}
	if (bytes) {
		chacha_block_xor_ssse3(state, dst, src, bytes, nrounds);
		state[12]++;
	}
}

void hchacha_block_arch(const u32 *state, u32 *stream, int nrounds)
{
	state = PTR_ALIGN(state, CHACHA_STATE_ALIGN);

	if (!static_branch_likely(&chacha_use_simd) || !may_use_simd()) {
		hchacha_block_generic(state, stream, nrounds);
	} else {
		kernel_fpu_begin();
		hchacha_block_ssse3(state, stream, nrounds);
		kernel_fpu_end();
	}
}
EXPORT_SYMBOL(hchacha_block_arch);

void chacha_init_arch(u32 *state, const u32 *key, const u8 *iv)
{
	state = PTR_ALIGN(state, CHACHA_STATE_ALIGN);

	chacha_init_generic(state, key, iv);
}
EXPORT_SYMBOL(chacha_init_arch);

void chacha_crypt_arch(u32 *state, u8 *dst, const u8 *src, unsigned int bytes,
		       int nrounds)
{
	state = PTR_ALIGN(state, CHACHA_STATE_ALIGN);

	if (!static_branch_likely(&chacha_use_simd) || !may_use_simd() ||
	    bytes <= CHACHA_BLOCK_SIZE)
		return chacha_crypt_generic(state, dst, src, bytes, nrounds);

	kernel_fpu_begin();
	chacha_dosimd(state, dst, src, bytes, nrounds);
	kernel_fpu_end();
}
EXPORT_SYMBOL(chacha_crypt_arch);

static int chacha_simd_stream_xor(struct skcipher_request *req,
				  const struct chacha_ctx *ctx, const u8 *iv)
{
	u32 *state, state_buf[16 + 2] __aligned(8);
	struct skcipher_walk walk;
	int err;

	err = skcipher_walk_virt(&walk, req, false);

	BUILD_BUG_ON(CHACHA_STATE_ALIGN != 16);
	state = PTR_ALIGN(state_buf + 0, CHACHA_STATE_ALIGN);

	chacha_init_generic(state, ctx->key, iv);

	while (walk.nbytes > 0) {
		unsigned int nbytes = walk.nbytes;

		if (nbytes < walk.total)
			nbytes = round_down(nbytes, walk.stride);

		if (!static_branch_likely(&chacha_use_simd) ||
		    !may_use_simd()) {
			chacha_crypt_generic(state, walk.dst.virt.addr,
					     walk.src.virt.addr, nbytes,
					     ctx->nrounds);
		} else {
			kernel_fpu_begin();
			chacha_dosimd(state, walk.dst.virt.addr,
				      walk.src.virt.addr, nbytes,
				      ctx->nrounds);
			kernel_fpu_end();
		}
		err = skcipher_walk_done(&walk, walk.nbytes - nbytes);
	}

	return err;
}

static int chacha_simd(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct chacha_ctx *ctx = crypto_skcipher_ctx(tfm);

	return chacha_simd_stream_xor(req, ctx, req->iv);
}

static int xchacha_simd(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct chacha_ctx *ctx = crypto_skcipher_ctx(tfm);
	u32 *state, state_buf[16 + 2] __aligned(8);
	struct chacha_ctx subctx;
	u8 real_iv[16];

	BUILD_BUG_ON(CHACHA_STATE_ALIGN != 16);
	state = PTR_ALIGN(state_buf + 0, CHACHA_STATE_ALIGN);
	chacha_init_generic(state, ctx->key, req->iv);

	if (req->cryptlen > CHACHA_BLOCK_SIZE && irq_fpu_usable()) {
		kernel_fpu_begin();
		hchacha_block_ssse3(state, subctx.key, ctx->nrounds);
		kernel_fpu_end();
	} else {
		hchacha_block_generic(state, subctx.key, ctx->nrounds);
	}
	subctx.nrounds = ctx->nrounds;

	memcpy(&real_iv[0], req->iv + 24, 8);
	memcpy(&real_iv[8], req->iv + 16, 8);
	return chacha_simd_stream_xor(req, &subctx, real_iv);
}

static struct skcipher_alg algs[] = {
	{
		.base.cra_name		= "chacha20",
		.base.cra_driver_name	= "chacha20-simd",
		.base.cra_priority	= 300,
		.base.cra_blocksize	= 1,
		.base.cra_ctxsize	= sizeof(struct chacha_ctx),
		.base.cra_module	= THIS_MODULE,

		.min_keysize		= CHACHA_KEY_SIZE,
		.max_keysize		= CHACHA_KEY_SIZE,
		.ivsize			= CHACHA_IV_SIZE,
		.chunksize		= CHACHA_BLOCK_SIZE,
		.setkey			= chacha20_setkey,
		.encrypt		= chacha_simd,
		.decrypt		= chacha_simd,
	}, {
		.base.cra_name		= "xchacha20",
		.base.cra_driver_name	= "xchacha20-simd",
		.base.cra_priority	= 300,
		.base.cra_blocksize	= 1,
		.base.cra_ctxsize	= sizeof(struct chacha_ctx),
		.base.cra_module	= THIS_MODULE,

		.min_keysize		= CHACHA_KEY_SIZE,
		.max_keysize		= CHACHA_KEY_SIZE,
		.ivsize			= XCHACHA_IV_SIZE,
		.chunksize		= CHACHA_BLOCK_SIZE,
		.setkey			= chacha20_setkey,
		.encrypt		= xchacha_simd,
		.decrypt		= xchacha_simd,
	}, {
		.base.cra_name		= "xchacha12",
		.base.cra_driver_name	= "xchacha12-simd",
		.base.cra_priority	= 300,
		.base.cra_blocksize	= 1,
		.base.cra_ctxsize	= sizeof(struct chacha_ctx),
		.base.cra_module	= THIS_MODULE,

		.min_keysize		= CHACHA_KEY_SIZE,
		.max_keysize		= CHACHA_KEY_SIZE,
		.ivsize			= XCHACHA_IV_SIZE,
		.chunksize		= CHACHA_BLOCK_SIZE,
		.setkey			= chacha12_setkey,
		.encrypt		= xchacha_simd,
		.decrypt		= xchacha_simd,
	},
};

static int __init chacha_simd_mod_init(void)
{
	if (!boot_cpu_has(X86_FEATURE_SSSE3))
		return 0;

	static_branch_enable(&chacha_use_simd);

	if (IS_ENABLED(CONFIG_AS_AVX2) &&
	    boot_cpu_has(X86_FEATURE_AVX) &&
	    boot_cpu_has(X86_FEATURE_AVX2) &&
	    cpu_has_xfeatures(XFEATURE_MASK_SSE | XFEATURE_MASK_YMM, NULL)) {
		static_branch_enable(&chacha_use_avx2);

		if (IS_ENABLED(CONFIG_AS_AVX512) &&
		    boot_cpu_has(X86_FEATURE_AVX512VL) &&
		    boot_cpu_has(X86_FEATURE_AVX512BW)) /* kmovq */
			static_branch_enable(&chacha_use_avx512vl);
	}
	return crypto_register_skciphers(algs, ARRAY_SIZE(algs));
}

static void __exit chacha_simd_mod_fini(void)
{
	if (boot_cpu_has(X86_FEATURE_SSSE3))
		crypto_unregister_skciphers(algs, ARRAY_SIZE(algs));
}

module_init(chacha_simd_mod_init);
module_exit(chacha_simd_mod_fini);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Martin Willi <martin@strongswan.org>");
MODULE_DESCRIPTION("ChaCha and XChaCha stream ciphers (x64 SIMD accelerated)");
MODULE_ALIAS_CRYPTO("chacha20");
MODULE_ALIAS_CRYPTO("chacha20-simd");
MODULE_ALIAS_CRYPTO("xchacha20");
MODULE_ALIAS_CRYPTO("xchacha20-simd");
MODULE_ALIAS_CRYPTO("xchacha12");
MODULE_ALIAS_CRYPTO("xchacha12-simd");
