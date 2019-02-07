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
#include <crypto/chacha.h>
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
#ifdef CONFIG_AS_AVX2
asmlinkage void chacha_2block_xor_avx2(u32 *state, u8 *dst, const u8 *src,
				       unsigned int len, int nrounds);
asmlinkage void chacha_4block_xor_avx2(u32 *state, u8 *dst, const u8 *src,
				       unsigned int len, int nrounds);
asmlinkage void chacha_8block_xor_avx2(u32 *state, u8 *dst, const u8 *src,
				       unsigned int len, int nrounds);
static bool chacha_use_avx2;
#ifdef CONFIG_AS_AVX512
asmlinkage void chacha_2block_xor_avx512vl(u32 *state, u8 *dst, const u8 *src,
					   unsigned int len, int nrounds);
asmlinkage void chacha_4block_xor_avx512vl(u32 *state, u8 *dst, const u8 *src,
					   unsigned int len, int nrounds);
asmlinkage void chacha_8block_xor_avx512vl(u32 *state, u8 *dst, const u8 *src,
					   unsigned int len, int nrounds);
static bool chacha_use_avx512vl;
#endif
#endif

static unsigned int chacha_advance(unsigned int len, unsigned int maxblocks)
{
	len = min(len, maxblocks * CHACHA_BLOCK_SIZE);
	return round_up(len, CHACHA_BLOCK_SIZE) / CHACHA_BLOCK_SIZE;
}

static void chacha_dosimd(u32 *state, u8 *dst, const u8 *src,
			  unsigned int bytes, int nrounds)
{
#ifdef CONFIG_AS_AVX2
#ifdef CONFIG_AS_AVX512
	if (chacha_use_avx512vl) {
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
#endif
	if (chacha_use_avx2) {
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
#endif
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

static int chacha_simd_stream_xor(struct skcipher_walk *walk,
				  struct chacha_ctx *ctx, u8 *iv)
{
	u32 *state, state_buf[16 + 2] __aligned(8);
	int next_yield = 4096; /* bytes until next FPU yield */
	int err = 0;

	BUILD_BUG_ON(CHACHA_STATE_ALIGN != 16);
	state = PTR_ALIGN(state_buf + 0, CHACHA_STATE_ALIGN);

	crypto_chacha_init(state, ctx, iv);

	while (walk->nbytes > 0) {
		unsigned int nbytes = walk->nbytes;

		if (nbytes < walk->total) {
			nbytes = round_down(nbytes, walk->stride);
			next_yield -= nbytes;
		}

		chacha_dosimd(state, walk->dst.virt.addr, walk->src.virt.addr,
			      nbytes, ctx->nrounds);

		if (next_yield <= 0) {
			/* temporarily allow preemption */
			kernel_fpu_end();
			kernel_fpu_begin();
			next_yield = 4096;
		}

		err = skcipher_walk_done(walk, walk->nbytes - nbytes);
	}

	return err;
}

static int chacha_simd(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct chacha_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_walk walk;
	int err;

	if (req->cryptlen <= CHACHA_BLOCK_SIZE || !irq_fpu_usable())
		return crypto_chacha_crypt(req);

	err = skcipher_walk_virt(&walk, req, true);
	if (err)
		return err;

	kernel_fpu_begin();
	err = chacha_simd_stream_xor(&walk, ctx, req->iv);
	kernel_fpu_end();
	return err;
}

static int xchacha_simd(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct chacha_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_walk walk;
	struct chacha_ctx subctx;
	u32 *state, state_buf[16 + 2] __aligned(8);
	u8 real_iv[16];
	int err;

	if (req->cryptlen <= CHACHA_BLOCK_SIZE || !irq_fpu_usable())
		return crypto_xchacha_crypt(req);

	err = skcipher_walk_virt(&walk, req, true);
	if (err)
		return err;

	BUILD_BUG_ON(CHACHA_STATE_ALIGN != 16);
	state = PTR_ALIGN(state_buf + 0, CHACHA_STATE_ALIGN);
	crypto_chacha_init(state, ctx, req->iv);

	kernel_fpu_begin();

	hchacha_block_ssse3(state, subctx.key, ctx->nrounds);
	subctx.nrounds = ctx->nrounds;

	memcpy(&real_iv[0], req->iv + 24, 8);
	memcpy(&real_iv[8], req->iv + 16, 8);
	err = chacha_simd_stream_xor(&walk, &subctx, real_iv);

	kernel_fpu_end();

	return err;
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
		.setkey			= crypto_chacha20_setkey,
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
		.setkey			= crypto_chacha20_setkey,
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
		.setkey			= crypto_chacha12_setkey,
		.encrypt		= xchacha_simd,
		.decrypt		= xchacha_simd,
	},
};

static int __init chacha_simd_mod_init(void)
{
	if (!boot_cpu_has(X86_FEATURE_SSSE3))
		return -ENODEV;

#ifdef CONFIG_AS_AVX2
	chacha_use_avx2 = boot_cpu_has(X86_FEATURE_AVX) &&
			  boot_cpu_has(X86_FEATURE_AVX2) &&
			  cpu_has_xfeatures(XFEATURE_MASK_SSE | XFEATURE_MASK_YMM, NULL);
#ifdef CONFIG_AS_AVX512
	chacha_use_avx512vl = chacha_use_avx2 &&
			      boot_cpu_has(X86_FEATURE_AVX512VL) &&
			      boot_cpu_has(X86_FEATURE_AVX512BW); /* kmovq */
#endif
#endif
	return crypto_register_skciphers(algs, ARRAY_SIZE(algs));
}

static void __exit chacha_simd_mod_fini(void)
{
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
