// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ChaCha and XChaCha stream ciphers, including ChaCha20 (RFC7539)
 *
 * Copyright (C) 2015 Martin Willi
 * Copyright (C) 2018 Google LLC
 */

#include <asm/unaligned.h>
#include <crypto/algapi.h>
#include <crypto/internal/chacha.h>
#include <crypto/internal/skcipher.h>
#include <linux/module.h>

static int chacha_stream_xor(struct skcipher_request *req,
			     const struct chacha_ctx *ctx, const u8 *iv)
{
	struct skcipher_walk walk;
	u32 state[16];
	int err;

	err = skcipher_walk_virt(&walk, req, false);

	chacha_init_generic(state, ctx->key, iv);

	while (walk.nbytes > 0) {
		unsigned int nbytes = walk.nbytes;

		if (nbytes < walk.total)
			nbytes = round_down(nbytes, CHACHA_BLOCK_SIZE);

		chacha_crypt_generic(state, walk.dst.virt.addr,
				     walk.src.virt.addr, nbytes, ctx->nrounds);
		err = skcipher_walk_done(&walk, walk.nbytes - nbytes);
	}

	return err;
}

static int crypto_chacha20_setkey(struct crypto_skcipher *tfm, const u8 *key,
				  unsigned int keysize)
{
	return chacha_setkey(tfm, key, keysize, 20);
}

static int crypto_chacha12_setkey(struct crypto_skcipher *tfm, const u8 *key,
				 unsigned int keysize)
{
	return chacha_setkey(tfm, key, keysize, 12);
}

static int crypto_chacha_crypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct chacha_ctx *ctx = crypto_skcipher_ctx(tfm);

	return chacha_stream_xor(req, ctx, req->iv);
}

static int crypto_xchacha_crypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct chacha_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct chacha_ctx subctx;
	u32 state[16];
	u8 real_iv[16];

	/* Compute the subkey given the original key and first 128 nonce bits */
	chacha_init_generic(state, ctx->key, req->iv);
	hchacha_block_generic(state, subctx.key, ctx->nrounds);
	subctx.nrounds = ctx->nrounds;

	/* Build the real IV */
	memcpy(&real_iv[0], req->iv + 24, 8); /* stream position */
	memcpy(&real_iv[8], req->iv + 16, 8); /* remaining 64 nonce bits */

	/* Generate the stream and XOR it with the data */
	return chacha_stream_xor(req, &subctx, real_iv);
}

static struct skcipher_alg algs[] = {
	{
		.base.cra_name		= "chacha20",
		.base.cra_driver_name	= "chacha20-generic",
		.base.cra_priority	= 100,
		.base.cra_blocksize	= 1,
		.base.cra_ctxsize	= sizeof(struct chacha_ctx),
		.base.cra_module	= THIS_MODULE,

		.min_keysize		= CHACHA_KEY_SIZE,
		.max_keysize		= CHACHA_KEY_SIZE,
		.ivsize			= CHACHA_IV_SIZE,
		.chunksize		= CHACHA_BLOCK_SIZE,
		.setkey			= crypto_chacha20_setkey,
		.encrypt		= crypto_chacha_crypt,
		.decrypt		= crypto_chacha_crypt,
	}, {
		.base.cra_name		= "xchacha20",
		.base.cra_driver_name	= "xchacha20-generic",
		.base.cra_priority	= 100,
		.base.cra_blocksize	= 1,
		.base.cra_ctxsize	= sizeof(struct chacha_ctx),
		.base.cra_module	= THIS_MODULE,

		.min_keysize		= CHACHA_KEY_SIZE,
		.max_keysize		= CHACHA_KEY_SIZE,
		.ivsize			= XCHACHA_IV_SIZE,
		.chunksize		= CHACHA_BLOCK_SIZE,
		.setkey			= crypto_chacha20_setkey,
		.encrypt		= crypto_xchacha_crypt,
		.decrypt		= crypto_xchacha_crypt,
	}, {
		.base.cra_name		= "xchacha12",
		.base.cra_driver_name	= "xchacha12-generic",
		.base.cra_priority	= 100,
		.base.cra_blocksize	= 1,
		.base.cra_ctxsize	= sizeof(struct chacha_ctx),
		.base.cra_module	= THIS_MODULE,

		.min_keysize		= CHACHA_KEY_SIZE,
		.max_keysize		= CHACHA_KEY_SIZE,
		.ivsize			= XCHACHA_IV_SIZE,
		.chunksize		= CHACHA_BLOCK_SIZE,
		.setkey			= crypto_chacha12_setkey,
		.encrypt		= crypto_xchacha_crypt,
		.decrypt		= crypto_xchacha_crypt,
	}
};

static int __init chacha_generic_mod_init(void)
{
	return crypto_register_skciphers(algs, ARRAY_SIZE(algs));
}

static void __exit chacha_generic_mod_fini(void)
{
	crypto_unregister_skciphers(algs, ARRAY_SIZE(algs));
}

subsys_initcall(chacha_generic_mod_init);
module_exit(chacha_generic_mod_fini);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Martin Willi <martin@strongswan.org>");
MODULE_DESCRIPTION("ChaCha and XChaCha stream ciphers (generic)");
MODULE_ALIAS_CRYPTO("chacha20");
MODULE_ALIAS_CRYPTO("chacha20-generic");
MODULE_ALIAS_CRYPTO("xchacha20");
MODULE_ALIAS_CRYPTO("xchacha20-generic");
MODULE_ALIAS_CRYPTO("xchacha12");
MODULE_ALIAS_CRYPTO("xchacha12-generic");
