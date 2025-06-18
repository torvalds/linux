// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Crypto API wrappers for the ChaCha20, XChaCha20, and XChaCha12 stream ciphers
 *
 * Copyright (C) 2015 Martin Willi
 * Copyright (C) 2018 Google LLC
 */

#include <linux/unaligned.h>
#include <crypto/algapi.h>
#include <crypto/chacha.h>
#include <crypto/internal/skcipher.h>
#include <linux/module.h>

struct chacha_ctx {
	u32 key[8];
	int nrounds;
};

static int chacha_setkey(struct crypto_skcipher *tfm,
			 const u8 *key, unsigned int keysize, int nrounds)
{
	struct chacha_ctx *ctx = crypto_skcipher_ctx(tfm);
	int i;

	if (keysize != CHACHA_KEY_SIZE)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(ctx->key); i++)
		ctx->key[i] = get_unaligned_le32(key + i * sizeof(u32));

	ctx->nrounds = nrounds;
	return 0;
}

static int chacha20_setkey(struct crypto_skcipher *tfm,
			   const u8 *key, unsigned int keysize)
{
	return chacha_setkey(tfm, key, keysize, 20);
}

static int chacha12_setkey(struct crypto_skcipher *tfm,
			   const u8 *key, unsigned int keysize)
{
	return chacha_setkey(tfm, key, keysize, 12);
}

static int chacha_stream_xor(struct skcipher_request *req,
			     const struct chacha_ctx *ctx,
			     const u8 iv[CHACHA_IV_SIZE], bool arch)
{
	struct skcipher_walk walk;
	struct chacha_state state;
	int err;

	err = skcipher_walk_virt(&walk, req, false);

	chacha_init(&state, ctx->key, iv);

	while (walk.nbytes > 0) {
		unsigned int nbytes = walk.nbytes;

		if (nbytes < walk.total)
			nbytes = round_down(nbytes, CHACHA_BLOCK_SIZE);

		if (arch)
			chacha_crypt(&state, walk.dst.virt.addr,
				     walk.src.virt.addr, nbytes, ctx->nrounds);
		else
			chacha_crypt_generic(&state, walk.dst.virt.addr,
					     walk.src.virt.addr, nbytes,
					     ctx->nrounds);
		err = skcipher_walk_done(&walk, walk.nbytes - nbytes);
	}

	return err;
}

static int crypto_chacha_crypt_generic(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	const struct chacha_ctx *ctx = crypto_skcipher_ctx(tfm);

	return chacha_stream_xor(req, ctx, req->iv, false);
}

static int crypto_chacha_crypt_arch(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	const struct chacha_ctx *ctx = crypto_skcipher_ctx(tfm);

	return chacha_stream_xor(req, ctx, req->iv, true);
}

static int crypto_xchacha_crypt(struct skcipher_request *req, bool arch)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	const struct chacha_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct chacha_ctx subctx;
	struct chacha_state state;
	u8 real_iv[16];

	/* Compute the subkey given the original key and first 128 nonce bits */
	chacha_init(&state, ctx->key, req->iv);
	if (arch)
		hchacha_block(&state, subctx.key, ctx->nrounds);
	else
		hchacha_block_generic(&state, subctx.key, ctx->nrounds);
	subctx.nrounds = ctx->nrounds;

	/* Build the real IV */
	memcpy(&real_iv[0], req->iv + 24, 8); /* stream position */
	memcpy(&real_iv[8], req->iv + 16, 8); /* remaining 64 nonce bits */

	/* Generate the stream and XOR it with the data */
	return chacha_stream_xor(req, &subctx, real_iv, arch);
}

static int crypto_xchacha_crypt_generic(struct skcipher_request *req)
{
	return crypto_xchacha_crypt(req, false);
}

static int crypto_xchacha_crypt_arch(struct skcipher_request *req)
{
	return crypto_xchacha_crypt(req, true);
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
		.setkey			= chacha20_setkey,
		.encrypt		= crypto_chacha_crypt_generic,
		.decrypt		= crypto_chacha_crypt_generic,
	},
	{
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
		.setkey			= chacha20_setkey,
		.encrypt		= crypto_xchacha_crypt_generic,
		.decrypt		= crypto_xchacha_crypt_generic,
	},
	{
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
		.setkey			= chacha12_setkey,
		.encrypt		= crypto_xchacha_crypt_generic,
		.decrypt		= crypto_xchacha_crypt_generic,
	},
	{
		.base.cra_name		= "chacha20",
		.base.cra_driver_name	= "chacha20-" __stringify(ARCH),
		.base.cra_priority	= 300,
		.base.cra_blocksize	= 1,
		.base.cra_ctxsize	= sizeof(struct chacha_ctx),
		.base.cra_module	= THIS_MODULE,

		.min_keysize		= CHACHA_KEY_SIZE,
		.max_keysize		= CHACHA_KEY_SIZE,
		.ivsize			= CHACHA_IV_SIZE,
		.chunksize		= CHACHA_BLOCK_SIZE,
		.setkey			= chacha20_setkey,
		.encrypt		= crypto_chacha_crypt_arch,
		.decrypt		= crypto_chacha_crypt_arch,
	},
	{
		.base.cra_name		= "xchacha20",
		.base.cra_driver_name	= "xchacha20-" __stringify(ARCH),
		.base.cra_priority	= 300,
		.base.cra_blocksize	= 1,
		.base.cra_ctxsize	= sizeof(struct chacha_ctx),
		.base.cra_module	= THIS_MODULE,

		.min_keysize		= CHACHA_KEY_SIZE,
		.max_keysize		= CHACHA_KEY_SIZE,
		.ivsize			= XCHACHA_IV_SIZE,
		.chunksize		= CHACHA_BLOCK_SIZE,
		.setkey			= chacha20_setkey,
		.encrypt		= crypto_xchacha_crypt_arch,
		.decrypt		= crypto_xchacha_crypt_arch,
	},
	{
		.base.cra_name		= "xchacha12",
		.base.cra_driver_name	= "xchacha12-" __stringify(ARCH),
		.base.cra_priority	= 300,
		.base.cra_blocksize	= 1,
		.base.cra_ctxsize	= sizeof(struct chacha_ctx),
		.base.cra_module	= THIS_MODULE,

		.min_keysize		= CHACHA_KEY_SIZE,
		.max_keysize		= CHACHA_KEY_SIZE,
		.ivsize			= XCHACHA_IV_SIZE,
		.chunksize		= CHACHA_BLOCK_SIZE,
		.setkey			= chacha12_setkey,
		.encrypt		= crypto_xchacha_crypt_arch,
		.decrypt		= crypto_xchacha_crypt_arch,
	}
};

static unsigned int num_algs;

static int __init crypto_chacha_mod_init(void)
{
	/* register the arch flavours only if they differ from generic */
	num_algs = ARRAY_SIZE(algs);
	BUILD_BUG_ON(ARRAY_SIZE(algs) % 2 != 0);
	if (!chacha_is_arch_optimized())
		num_algs /= 2;

	return crypto_register_skciphers(algs, num_algs);
}

static void __exit crypto_chacha_mod_fini(void)
{
	crypto_unregister_skciphers(algs, num_algs);
}

module_init(crypto_chacha_mod_init);
module_exit(crypto_chacha_mod_fini);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Martin Willi <martin@strongswan.org>");
MODULE_DESCRIPTION("Crypto API wrappers for the ChaCha20, XChaCha20, and XChaCha12 stream ciphers");
MODULE_ALIAS_CRYPTO("chacha20");
MODULE_ALIAS_CRYPTO("chacha20-generic");
MODULE_ALIAS_CRYPTO("chacha20-"  __stringify(ARCH));
MODULE_ALIAS_CRYPTO("xchacha20");
MODULE_ALIAS_CRYPTO("xchacha20-generic");
MODULE_ALIAS_CRYPTO("xchacha20-"  __stringify(ARCH));
MODULE_ALIAS_CRYPTO("xchacha12");
MODULE_ALIAS_CRYPTO("xchacha12-generic");
MODULE_ALIAS_CRYPTO("xchacha12-"  __stringify(ARCH));
