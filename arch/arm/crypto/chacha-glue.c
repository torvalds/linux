// SPDX-License-Identifier: GPL-2.0
/*
 * ARM NEON accelerated ChaCha and XChaCha stream ciphers,
 * including ChaCha20 (RFC7539)
 *
 * Copyright (C) 2016-2019 Linaro, Ltd. <ard.biesheuvel@linaro.org>
 * Copyright (C) 2015 Martin Willi
 */

#include <crypto/algapi.h>
#include <crypto/internal/chacha.h>
#include <crypto/internal/simd.h>
#include <crypto/internal/skcipher.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include <asm/cputype.h>
#include <asm/hwcap.h>
#include <asm/neon.h>
#include <asm/simd.h>

asmlinkage void chacha_block_xor_neon(const u32 *state, u8 *dst, const u8 *src,
				      int nrounds);
asmlinkage void chacha_4block_xor_neon(const u32 *state, u8 *dst, const u8 *src,
				       int nrounds);
asmlinkage void hchacha_block_arm(const u32 *state, u32 *out, int nrounds);
asmlinkage void hchacha_block_neon(const u32 *state, u32 *out, int nrounds);

asmlinkage void chacha_doarm(u8 *dst, const u8 *src, unsigned int bytes,
			     const u32 *state, int nrounds);

static inline bool neon_usable(void)
{
	return crypto_simd_usable();
}

static void chacha_doneon(u32 *state, u8 *dst, const u8 *src,
			  unsigned int bytes, int nrounds)
{
	u8 buf[CHACHA_BLOCK_SIZE];

	while (bytes >= CHACHA_BLOCK_SIZE * 4) {
		chacha_4block_xor_neon(state, dst, src, nrounds);
		bytes -= CHACHA_BLOCK_SIZE * 4;
		src += CHACHA_BLOCK_SIZE * 4;
		dst += CHACHA_BLOCK_SIZE * 4;
		state[12] += 4;
	}
	while (bytes >= CHACHA_BLOCK_SIZE) {
		chacha_block_xor_neon(state, dst, src, nrounds);
		bytes -= CHACHA_BLOCK_SIZE;
		src += CHACHA_BLOCK_SIZE;
		dst += CHACHA_BLOCK_SIZE;
		state[12]++;
	}
	if (bytes) {
		memcpy(buf, src, bytes);
		chacha_block_xor_neon(state, buf, buf, nrounds);
		memcpy(dst, buf, bytes);
	}
}

static int chacha_stream_xor(struct skcipher_request *req,
			     const struct chacha_ctx *ctx, const u8 *iv,
			     bool neon)
{
	struct skcipher_walk walk;
	u32 state[16];
	int err;

	err = skcipher_walk_virt(&walk, req, false);

	chacha_init_generic(state, ctx->key, iv);

	while (walk.nbytes > 0) {
		unsigned int nbytes = walk.nbytes;

		if (nbytes < walk.total)
			nbytes = round_down(nbytes, walk.stride);

		if (!neon) {
			chacha_doarm(walk.dst.virt.addr, walk.src.virt.addr,
				     nbytes, state, ctx->nrounds);
			state[12] += DIV_ROUND_UP(nbytes, CHACHA_BLOCK_SIZE);
		} else {
			kernel_neon_begin();
			chacha_doneon(state, walk.dst.virt.addr,
				      walk.src.virt.addr, nbytes, ctx->nrounds);
			kernel_neon_end();
		}
		err = skcipher_walk_done(&walk, walk.nbytes - nbytes);
	}

	return err;
}

static int do_chacha(struct skcipher_request *req, bool neon)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct chacha_ctx *ctx = crypto_skcipher_ctx(tfm);

	return chacha_stream_xor(req, ctx, req->iv, neon);
}

static int chacha_arm(struct skcipher_request *req)
{
	return do_chacha(req, false);
}

static int chacha_neon(struct skcipher_request *req)
{
	return do_chacha(req, neon_usable());
}

static int do_xchacha(struct skcipher_request *req, bool neon)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct chacha_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct chacha_ctx subctx;
	u32 state[16];
	u8 real_iv[16];

	chacha_init_generic(state, ctx->key, req->iv);

	if (!neon) {
		hchacha_block_arm(state, subctx.key, ctx->nrounds);
	} else {
		kernel_neon_begin();
		hchacha_block_neon(state, subctx.key, ctx->nrounds);
		kernel_neon_end();
	}
	subctx.nrounds = ctx->nrounds;

	memcpy(&real_iv[0], req->iv + 24, 8);
	memcpy(&real_iv[8], req->iv + 16, 8);
	return chacha_stream_xor(req, &subctx, real_iv, neon);
}

static int xchacha_arm(struct skcipher_request *req)
{
	return do_xchacha(req, false);
}

static int xchacha_neon(struct skcipher_request *req)
{
	return do_xchacha(req, neon_usable());
}

static struct skcipher_alg arm_algs[] = {
	{
		.base.cra_name		= "chacha20",
		.base.cra_driver_name	= "chacha20-arm",
		.base.cra_priority	= 200,
		.base.cra_blocksize	= 1,
		.base.cra_ctxsize	= sizeof(struct chacha_ctx),
		.base.cra_module	= THIS_MODULE,

		.min_keysize		= CHACHA_KEY_SIZE,
		.max_keysize		= CHACHA_KEY_SIZE,
		.ivsize			= CHACHA_IV_SIZE,
		.chunksize		= CHACHA_BLOCK_SIZE,
		.setkey			= chacha20_setkey,
		.encrypt		= chacha_arm,
		.decrypt		= chacha_arm,
	}, {
		.base.cra_name		= "xchacha20",
		.base.cra_driver_name	= "xchacha20-arm",
		.base.cra_priority	= 200,
		.base.cra_blocksize	= 1,
		.base.cra_ctxsize	= sizeof(struct chacha_ctx),
		.base.cra_module	= THIS_MODULE,

		.min_keysize		= CHACHA_KEY_SIZE,
		.max_keysize		= CHACHA_KEY_SIZE,
		.ivsize			= XCHACHA_IV_SIZE,
		.chunksize		= CHACHA_BLOCK_SIZE,
		.setkey			= chacha20_setkey,
		.encrypt		= xchacha_arm,
		.decrypt		= xchacha_arm,
	}, {
		.base.cra_name		= "xchacha12",
		.base.cra_driver_name	= "xchacha12-arm",
		.base.cra_priority	= 200,
		.base.cra_blocksize	= 1,
		.base.cra_ctxsize	= sizeof(struct chacha_ctx),
		.base.cra_module	= THIS_MODULE,

		.min_keysize		= CHACHA_KEY_SIZE,
		.max_keysize		= CHACHA_KEY_SIZE,
		.ivsize			= XCHACHA_IV_SIZE,
		.chunksize		= CHACHA_BLOCK_SIZE,
		.setkey			= chacha12_setkey,
		.encrypt		= xchacha_arm,
		.decrypt		= xchacha_arm,
	},
};

static struct skcipher_alg neon_algs[] = {
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
		.walksize		= 4 * CHACHA_BLOCK_SIZE,
		.setkey			= chacha20_setkey,
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
		.walksize		= 4 * CHACHA_BLOCK_SIZE,
		.setkey			= chacha20_setkey,
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
		.walksize		= 4 * CHACHA_BLOCK_SIZE,
		.setkey			= chacha12_setkey,
		.encrypt		= xchacha_neon,
		.decrypt		= xchacha_neon,
	}
};

static int __init chacha_simd_mod_init(void)
{
	int err;

	err = crypto_register_skciphers(arm_algs, ARRAY_SIZE(arm_algs));
	if (err)
		return err;

	if (IS_ENABLED(CONFIG_KERNEL_MODE_NEON) && (elf_hwcap & HWCAP_NEON)) {
		int i;

		switch (read_cpuid_part()) {
		case ARM_CPU_PART_CORTEX_A7:
		case ARM_CPU_PART_CORTEX_A5:
			/*
			 * The Cortex-A7 and Cortex-A5 do not perform well with
			 * the NEON implementation but do incredibly with the
			 * scalar one and use less power.
			 */
			for (i = 0; i < ARRAY_SIZE(neon_algs); i++)
				neon_algs[i].base.cra_priority = 0;
			break;
		}

		err = crypto_register_skciphers(neon_algs, ARRAY_SIZE(neon_algs));
		if (err)
			crypto_unregister_skciphers(arm_algs, ARRAY_SIZE(arm_algs));
	}
	return err;
}

static void __exit chacha_simd_mod_fini(void)
{
	crypto_unregister_skciphers(arm_algs, ARRAY_SIZE(arm_algs));
	if (IS_ENABLED(CONFIG_KERNEL_MODE_NEON) && (elf_hwcap & HWCAP_NEON))
		crypto_unregister_skciphers(neon_algs, ARRAY_SIZE(neon_algs));
}

module_init(chacha_simd_mod_init);
module_exit(chacha_simd_mod_fini);

MODULE_DESCRIPTION("ChaCha and XChaCha stream ciphers (scalar and NEON accelerated)");
MODULE_AUTHOR("Ard Biesheuvel <ard.biesheuvel@linaro.org>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS_CRYPTO("chacha20");
MODULE_ALIAS_CRYPTO("chacha20-arm");
MODULE_ALIAS_CRYPTO("xchacha20");
MODULE_ALIAS_CRYPTO("xchacha20-arm");
MODULE_ALIAS_CRYPTO("xchacha12");
MODULE_ALIAS_CRYPTO("xchacha12-arm");
#ifdef CONFIG_KERNEL_MODE_NEON
MODULE_ALIAS_CRYPTO("chacha20-neon");
MODULE_ALIAS_CRYPTO("xchacha20-neon");
MODULE_ALIAS_CRYPTO("xchacha12-neon");
#endif
