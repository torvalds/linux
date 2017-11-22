/*
 * ChaCha20 256-bit cipher algorithm, RFC7539
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
#include <linux/module.h>

static inline u32 le32_to_cpuvp(const void *p)
{
	return le32_to_cpup(p);
}

static void chacha20_docrypt(u32 *state, u8 *dst, const u8 *src,
			     unsigned int bytes)
{
	u8 stream[CHACHA20_BLOCK_SIZE];

	if (dst != src)
		memcpy(dst, src, bytes);

	while (bytes >= CHACHA20_BLOCK_SIZE) {
		chacha20_block(state, stream);
		crypto_xor(dst, stream, CHACHA20_BLOCK_SIZE);
		bytes -= CHACHA20_BLOCK_SIZE;
		dst += CHACHA20_BLOCK_SIZE;
	}
	if (bytes) {
		chacha20_block(state, stream);
		crypto_xor(dst, stream, bytes);
	}
}

void crypto_chacha20_init(u32 *state, struct chacha20_ctx *ctx, u8 *iv)
{
	state[0]  = 0x61707865; /* "expa" */
	state[1]  = 0x3320646e; /* "nd 3" */
	state[2]  = 0x79622d32; /* "2-by" */
	state[3]  = 0x6b206574; /* "te k" */
	state[4]  = ctx->key[0];
	state[5]  = ctx->key[1];
	state[6]  = ctx->key[2];
	state[7]  = ctx->key[3];
	state[8]  = ctx->key[4];
	state[9]  = ctx->key[5];
	state[10] = ctx->key[6];
	state[11] = ctx->key[7];
	state[12] = le32_to_cpuvp(iv +  0);
	state[13] = le32_to_cpuvp(iv +  4);
	state[14] = le32_to_cpuvp(iv +  8);
	state[15] = le32_to_cpuvp(iv + 12);
}
EXPORT_SYMBOL_GPL(crypto_chacha20_init);

int crypto_chacha20_setkey(struct crypto_skcipher *tfm, const u8 *key,
			   unsigned int keysize)
{
	struct chacha20_ctx *ctx = crypto_skcipher_ctx(tfm);
	int i;

	if (keysize != CHACHA20_KEY_SIZE)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(ctx->key); i++)
		ctx->key[i] = le32_to_cpuvp(key + i * sizeof(u32));

	return 0;
}
EXPORT_SYMBOL_GPL(crypto_chacha20_setkey);

int crypto_chacha20_crypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct chacha20_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_walk walk;
	u32 state[16];
	int err;

	err = skcipher_walk_virt(&walk, req, true);

	crypto_chacha20_init(state, ctx, walk.iv);

	while (walk.nbytes > 0) {
		unsigned int nbytes = walk.nbytes;

		if (nbytes < walk.total)
			nbytes = round_down(nbytes, walk.stride);

		chacha20_docrypt(state, walk.dst.virt.addr, walk.src.virt.addr,
				 nbytes);
		err = skcipher_walk_done(&walk, walk.nbytes - nbytes);
	}

	return err;
}
EXPORT_SYMBOL_GPL(crypto_chacha20_crypt);

static struct skcipher_alg alg = {
	.base.cra_name		= "chacha20",
	.base.cra_driver_name	= "chacha20-generic",
	.base.cra_priority	= 100,
	.base.cra_blocksize	= 1,
	.base.cra_ctxsize	= sizeof(struct chacha20_ctx),
	.base.cra_alignmask	= sizeof(u32) - 1,
	.base.cra_module	= THIS_MODULE,

	.min_keysize		= CHACHA20_KEY_SIZE,
	.max_keysize		= CHACHA20_KEY_SIZE,
	.ivsize			= CHACHA20_IV_SIZE,
	.chunksize		= CHACHA20_BLOCK_SIZE,
	.setkey			= crypto_chacha20_setkey,
	.encrypt		= crypto_chacha20_crypt,
	.decrypt		= crypto_chacha20_crypt,
};

static int __init chacha20_generic_mod_init(void)
{
	return crypto_register_skcipher(&alg);
}

static void __exit chacha20_generic_mod_fini(void)
{
	crypto_unregister_skcipher(&alg);
}

module_init(chacha20_generic_mod_init);
module_exit(chacha20_generic_mod_fini);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Martin Willi <martin@strongswan.org>");
MODULE_DESCRIPTION("chacha20 cipher algorithm");
MODULE_ALIAS_CRYPTO("chacha20");
MODULE_ALIAS_CRYPTO("chacha20-generic");
