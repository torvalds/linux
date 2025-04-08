// SPDX-License-Identifier: GPL-2.0
/*
 * MIPS accelerated ChaCha and XChaCha stream ciphers,
 * including ChaCha20 (RFC7539)
 *
 * Copyright (C) 2019 Linaro, Ltd. <ard.biesheuvel@linaro.org>
 */

#include <asm/byteorder.h>
#include <crypto/algapi.h>
#include <crypto/internal/chacha.h>
#include <crypto/internal/skcipher.h>
#include <linux/kernel.h>
#include <linux/module.h>

asmlinkage void chacha_crypt_arch(u32 *state, u8 *dst, const u8 *src,
				  unsigned int bytes, int nrounds);
EXPORT_SYMBOL(chacha_crypt_arch);

asmlinkage void hchacha_block_arch(const u32 *state, u32 *stream, int nrounds);
EXPORT_SYMBOL(hchacha_block_arch);

static int chacha_mips_stream_xor(struct skcipher_request *req,
				  const struct chacha_ctx *ctx, const u8 *iv)
{
	struct skcipher_walk walk;
	u32 state[16];
	int err;

	err = skcipher_walk_virt(&walk, req, false);

	chacha_init(state, ctx->key, iv);

	while (walk.nbytes > 0) {
		unsigned int nbytes = walk.nbytes;

		if (nbytes < walk.total)
			nbytes = round_down(nbytes, walk.stride);

		chacha_crypt(state, walk.dst.virt.addr, walk.src.virt.addr,
			     nbytes, ctx->nrounds);
		err = skcipher_walk_done(&walk, walk.nbytes - nbytes);
	}

	return err;
}

static int chacha_mips(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct chacha_ctx *ctx = crypto_skcipher_ctx(tfm);

	return chacha_mips_stream_xor(req, ctx, req->iv);
}

static int xchacha_mips(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct chacha_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct chacha_ctx subctx;
	u32 state[16];
	u8 real_iv[16];

	chacha_init(state, ctx->key, req->iv);

	hchacha_block(state, subctx.key, ctx->nrounds);
	subctx.nrounds = ctx->nrounds;

	memcpy(&real_iv[0], req->iv + 24, 8);
	memcpy(&real_iv[8], req->iv + 16, 8);
	return chacha_mips_stream_xor(req, &subctx, real_iv);
}

static struct skcipher_alg algs[] = {
	{
		.base.cra_name		= "chacha20",
		.base.cra_driver_name	= "chacha20-mips",
		.base.cra_priority	= 200,
		.base.cra_blocksize	= 1,
		.base.cra_ctxsize	= sizeof(struct chacha_ctx),
		.base.cra_module	= THIS_MODULE,

		.min_keysize		= CHACHA_KEY_SIZE,
		.max_keysize		= CHACHA_KEY_SIZE,
		.ivsize			= CHACHA_IV_SIZE,
		.chunksize		= CHACHA_BLOCK_SIZE,
		.setkey			= chacha20_setkey,
		.encrypt		= chacha_mips,
		.decrypt		= chacha_mips,
	}, {
		.base.cra_name		= "xchacha20",
		.base.cra_driver_name	= "xchacha20-mips",
		.base.cra_priority	= 200,
		.base.cra_blocksize	= 1,
		.base.cra_ctxsize	= sizeof(struct chacha_ctx),
		.base.cra_module	= THIS_MODULE,

		.min_keysize		= CHACHA_KEY_SIZE,
		.max_keysize		= CHACHA_KEY_SIZE,
		.ivsize			= XCHACHA_IV_SIZE,
		.chunksize		= CHACHA_BLOCK_SIZE,
		.setkey			= chacha20_setkey,
		.encrypt		= xchacha_mips,
		.decrypt		= xchacha_mips,
	}, {
		.base.cra_name		= "xchacha12",
		.base.cra_driver_name	= "xchacha12-mips",
		.base.cra_priority	= 200,
		.base.cra_blocksize	= 1,
		.base.cra_ctxsize	= sizeof(struct chacha_ctx),
		.base.cra_module	= THIS_MODULE,

		.min_keysize		= CHACHA_KEY_SIZE,
		.max_keysize		= CHACHA_KEY_SIZE,
		.ivsize			= XCHACHA_IV_SIZE,
		.chunksize		= CHACHA_BLOCK_SIZE,
		.setkey			= chacha12_setkey,
		.encrypt		= xchacha_mips,
		.decrypt		= xchacha_mips,
	}
};

static int __init chacha_simd_mod_init(void)
{
	return IS_REACHABLE(CONFIG_CRYPTO_SKCIPHER) ?
		crypto_register_skciphers(algs, ARRAY_SIZE(algs)) : 0;
}

static void __exit chacha_simd_mod_fini(void)
{
	if (IS_REACHABLE(CONFIG_CRYPTO_SKCIPHER))
		crypto_unregister_skciphers(algs, ARRAY_SIZE(algs));
}

module_init(chacha_simd_mod_init);
module_exit(chacha_simd_mod_fini);

MODULE_DESCRIPTION("ChaCha and XChaCha stream ciphers (MIPS accelerated)");
MODULE_AUTHOR("Ard Biesheuvel <ard.biesheuvel@linaro.org>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS_CRYPTO("chacha20");
MODULE_ALIAS_CRYPTO("chacha20-mips");
MODULE_ALIAS_CRYPTO("xchacha20");
MODULE_ALIAS_CRYPTO("xchacha20-mips");
MODULE_ALIAS_CRYPTO("xchacha12");
MODULE_ALIAS_CRYPTO("xchacha12-mips");
