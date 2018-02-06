/*
 * ChaCha20 256-bit cipher algorithm, RFC7539, SIMD glue code
 *
 * Copyright (C) 2015 Martin Willi
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <crypto/algapi.h>
#include <crypto/chacha20.h>
#include <crypto/internal/skcipher.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <asm/fpu/api.h>
#include <asm/simd.h>

#define CHACHA20_STATE_ALIGN 16

asmlinkage void chacha20_block_xor_ssse3(u32 *state, u8 *dst, const u8 *src);
asmlinkage void chacha20_4block_xor_ssse3(u32 *state, u8 *dst, const u8 *src);
#ifdef CONFIG_AS_AVX2
asmlinkage void chacha20_8block_xor_avx2(u32 *state, u8 *dst, const u8 *src);
static bool chacha20_use_avx2;
#endif

static void chacha20_dosimd(u32 *state, u8 *dst, const u8 *src,
			    unsigned int bytes)
{
	u8 buf[CHACHA20_BLOCK_SIZE];

#ifdef CONFIG_AS_AVX2
	if (chacha20_use_avx2) {
		while (bytes >= CHACHA20_BLOCK_SIZE * 8) {
			chacha20_8block_xor_avx2(state, dst, src);
			bytes -= CHACHA20_BLOCK_SIZE * 8;
			src += CHACHA20_BLOCK_SIZE * 8;
			dst += CHACHA20_BLOCK_SIZE * 8;
			state[12] += 8;
		}
	}
#endif
	while (bytes >= CHACHA20_BLOCK_SIZE * 4) {
		chacha20_4block_xor_ssse3(state, dst, src);
		bytes -= CHACHA20_BLOCK_SIZE * 4;
		src += CHACHA20_BLOCK_SIZE * 4;
		dst += CHACHA20_BLOCK_SIZE * 4;
		state[12] += 4;
	}
	while (bytes >= CHACHA20_BLOCK_SIZE) {
		chacha20_block_xor_ssse3(state, dst, src);
		bytes -= CHACHA20_BLOCK_SIZE;
		src += CHACHA20_BLOCK_SIZE;
		dst += CHACHA20_BLOCK_SIZE;
		state[12]++;
	}
	if (bytes) {
		memcpy(buf, src, bytes);
		chacha20_block_xor_ssse3(state, buf, buf);
		memcpy(dst, buf, bytes);
	}
}

static int chacha20_simd(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct chacha20_ctx *ctx = crypto_skcipher_ctx(tfm);
	u32 *state, state_buf[16 + 2] __aligned(8);
	struct skcipher_walk walk;
	int err;

	BUILD_BUG_ON(CHACHA20_STATE_ALIGN != 16);
	state = PTR_ALIGN(state_buf + 0, CHACHA20_STATE_ALIGN);

	if (req->cryptlen <= CHACHA20_BLOCK_SIZE || !may_use_simd())
		return crypto_chacha20_crypt(req);

	err = skcipher_walk_virt(&walk, req, true);

	crypto_chacha20_init(state, ctx, walk.iv);

	kernel_fpu_begin();

	while (walk.nbytes >= CHACHA20_BLOCK_SIZE) {
		chacha20_dosimd(state, walk.dst.virt.addr, walk.src.virt.addr,
				rounddown(walk.nbytes, CHACHA20_BLOCK_SIZE));
		err = skcipher_walk_done(&walk,
					 walk.nbytes % CHACHA20_BLOCK_SIZE);
	}

	if (walk.nbytes) {
		chacha20_dosimd(state, walk.dst.virt.addr, walk.src.virt.addr,
				walk.nbytes);
		err = skcipher_walk_done(&walk, 0);
	}

	kernel_fpu_end();

	return err;
}

static struct skcipher_alg alg = {
	.base.cra_name		= "chacha20",
	.base.cra_driver_name	= "chacha20-simd",
	.base.cra_priority	= 300,
	.base.cra_blocksize	= 1,
	.base.cra_ctxsize	= sizeof(struct chacha20_ctx),
	.base.cra_module	= THIS_MODULE,

	.min_keysize		= CHACHA20_KEY_SIZE,
	.max_keysize		= CHACHA20_KEY_SIZE,
	.ivsize			= CHACHA20_IV_SIZE,
	.chunksize		= CHACHA20_BLOCK_SIZE,
	.setkey			= crypto_chacha20_setkey,
	.encrypt		= chacha20_simd,
	.decrypt		= chacha20_simd,
};

static int __init chacha20_simd_mod_init(void)
{
	if (!boot_cpu_has(X86_FEATURE_SSSE3))
		return -ENODEV;

#ifdef CONFIG_AS_AVX2
	chacha20_use_avx2 = boot_cpu_has(X86_FEATURE_AVX) &&
			    boot_cpu_has(X86_FEATURE_AVX2) &&
			    cpu_has_xfeatures(XFEATURE_MASK_SSE | XFEATURE_MASK_YMM, NULL);
#endif
	return crypto_register_skcipher(&alg);
}

static void __exit chacha20_simd_mod_fini(void)
{
	crypto_unregister_skcipher(&alg);
}

module_init(chacha20_simd_mod_init);
module_exit(chacha20_simd_mod_fini);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Martin Willi <martin@strongswan.org>");
MODULE_DESCRIPTION("chacha20 cipher algorithm, SIMD accelerated");
MODULE_ALIAS_CRYPTO("chacha20");
MODULE_ALIAS_CRYPTO("chacha20-simd");
