/*
 * ARM NEON accelerated ChaCha and XChaCha stream ciphers,
 * including ChaCha20 (RFC7539)
 *
 * Copyright (C) 2016 - 2017 Linaro, Ltd. <ard.biesheuvel@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Based on:
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
#include <crypto/chacha.h>
#include <crypto/internal/skcipher.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include <asm/hwcap.h>
#include <asm/neon.h>
#include <asm/simd.h>

asmlinkage void chacha_block_xor_neon(u32 *state, u8 *dst, const u8 *src,
				      int nrounds);
asmlinkage void chacha_4block_xor_neon(u32 *state, u8 *dst, const u8 *src,
				       int nrounds, int bytes);
asmlinkage void hchacha_block_neon(const u32 *state, u32 *out, int nrounds);

static void chacha_doneon(u32 *state, u8 *dst, const u8 *src,
			  int bytes, int nrounds)
{
	while (bytes > 0) {
		int l = min(bytes, CHACHA_BLOCK_SIZE * 5);

		if (l <= CHACHA_BLOCK_SIZE) {
			u8 buf[CHACHA_BLOCK_SIZE];

			memcpy(buf, src, l);
			chacha_block_xor_neon(state, buf, buf, nrounds);
			memcpy(dst, buf, l);
			state[12] += 1;
			break;
		}
		chacha_4block_xor_neon(state, dst, src, nrounds, l);
		bytes -= CHACHA_BLOCK_SIZE * 5;
		src += CHACHA_BLOCK_SIZE * 5;
		dst += CHACHA_BLOCK_SIZE * 5;
		state[12] += 5;
	}
}

static int chacha_neon_stream_xor(struct skcipher_request *req,
				  struct chacha_ctx *ctx, u8 *iv)
{
	struct skcipher_walk walk;
	u32 state[16];
	int err;

	err = skcipher_walk_virt(&walk, req, false);

	crypto_chacha_init(state, ctx, iv);

	while (walk.nbytes > 0) {
		unsigned int nbytes = walk.nbytes;

		if (nbytes < walk.total)
			nbytes = rounddown(nbytes, walk.stride);

		kernel_neon_begin();
		chacha_doneon(state, walk.dst.virt.addr, walk.src.virt.addr,
			      nbytes, ctx->nrounds);
		kernel_neon_end();
		err = skcipher_walk_done(&walk, walk.nbytes - nbytes);
	}

	return err;
}

static int chacha_neon(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct chacha_ctx *ctx = crypto_skcipher_ctx(tfm);

	if (req->cryptlen <= CHACHA_BLOCK_SIZE || !may_use_simd())
		return crypto_chacha_crypt(req);

	return chacha_neon_stream_xor(req, ctx, req->iv);
}

static int xchacha_neon(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct chacha_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct chacha_ctx subctx;
	u32 state[16];
	u8 real_iv[16];

	if (req->cryptlen <= CHACHA_BLOCK_SIZE || !may_use_simd())
		return crypto_xchacha_crypt(req);

	crypto_chacha_init(state, ctx, req->iv);

	kernel_neon_begin();
	hchacha_block_neon(state, subctx.key, ctx->nrounds);
	kernel_neon_end();
	subctx.nrounds = ctx->nrounds;

	memcpy(&real_iv[0], req->iv + 24, 8);
	memcpy(&real_iv[8], req->iv + 16, 8);
	return chacha_neon_stream_xor(req, &subctx, real_iv);
}

static struct skcipher_alg algs[] = {
	{
		.base.cra_name		= "chacha20",
		.base.cra_driver_name	= "chacha20-neon",
		.base.cra_priority	= 300,
		.base.cra_blocksize	= 1,
		.base.cra_ctxsize	= sizeof(struct chacha_ctx),
		.base.cra_module	= THIS_MODULE,

		.min_keysize		= CHACHA_KEY_SIZE,
		.max_keysize		= CHACHA_KEY_SIZE,
		.ivsize			= CHACHA_IV_SIZE,
		.chunksize		= CHACHA_BLOCK_SIZE,
		.walksize		= 5 * CHACHA_BLOCK_SIZE,
		.setkey			= crypto_chacha20_setkey,
		.encrypt		= chacha_neon,
		.decrypt		= chacha_neon,
	}, {
		.base.cra_name		= "xchacha20",
		.base.cra_driver_name	= "xchacha20-neon",
		.base.cra_priority	= 300,
		.base.cra_blocksize	= 1,
		.base.cra_ctxsize	= sizeof(struct chacha_ctx),
		.base.cra_module	= THIS_MODULE,

		.min_keysize		= CHACHA_KEY_SIZE,
		.max_keysize		= CHACHA_KEY_SIZE,
		.ivsize			= XCHACHA_IV_SIZE,
		.chunksize		= CHACHA_BLOCK_SIZE,
		.walksize		= 5 * CHACHA_BLOCK_SIZE,
		.setkey			= crypto_chacha20_setkey,
		.encrypt		= xchacha_neon,
		.decrypt		= xchacha_neon,
	}, {
		.base.cra_name		= "xchacha12",
		.base.cra_driver_name	= "xchacha12-neon",
		.base.cra_priority	= 300,
		.base.cra_blocksize	= 1,
		.base.cra_ctxsize	= sizeof(struct chacha_ctx),
		.base.cra_module	= THIS_MODULE,

		.min_keysize		= CHACHA_KEY_SIZE,
		.max_keysize		= CHACHA_KEY_SIZE,
		.ivsize			= XCHACHA_IV_SIZE,
		.chunksize		= CHACHA_BLOCK_SIZE,
		.walksize		= 5 * CHACHA_BLOCK_SIZE,
		.setkey			= crypto_chacha12_setkey,
		.encrypt		= xchacha_neon,
		.decrypt		= xchacha_neon,
	}
};

static int __init chacha_simd_mod_init(void)
{
	if (!(elf_hwcap & HWCAP_ASIMD))
		return -ENODEV;

	return crypto_register_skciphers(algs, ARRAY_SIZE(algs));
}

static void __exit chacha_simd_mod_fini(void)
{
	crypto_unregister_skciphers(algs, ARRAY_SIZE(algs));
}

module_init(chacha_simd_mod_init);
module_exit(chacha_simd_mod_fini);

MODULE_DESCRIPTION("ChaCha and XChaCha stream ciphers (NEON accelerated)");
MODULE_AUTHOR("Ard Biesheuvel <ard.biesheuvel@linaro.org>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS_CRYPTO("chacha20");
MODULE_ALIAS_CRYPTO("chacha20-neon");
MODULE_ALIAS_CRYPTO("xchacha20");
MODULE_ALIAS_CRYPTO("xchacha20-neon");
MODULE_ALIAS_CRYPTO("xchacha12");
MODULE_ALIAS_CRYPTO("xchacha12-neon");
