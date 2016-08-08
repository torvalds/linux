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
#include <linux/crypto.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <crypto/chacha20.h>

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
	static const char constant[16] = "expand 32-byte k";

	state[0]  = le32_to_cpuvp(constant +  0);
	state[1]  = le32_to_cpuvp(constant +  4);
	state[2]  = le32_to_cpuvp(constant +  8);
	state[3]  = le32_to_cpuvp(constant + 12);
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

int crypto_chacha20_setkey(struct crypto_tfm *tfm, const u8 *key,
			   unsigned int keysize)
{
	struct chacha20_ctx *ctx = crypto_tfm_ctx(tfm);
	int i;

	if (keysize != CHACHA20_KEY_SIZE)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(ctx->key); i++)
		ctx->key[i] = le32_to_cpuvp(key + i * sizeof(u32));

	return 0;
}
EXPORT_SYMBOL_GPL(crypto_chacha20_setkey);

int crypto_chacha20_crypt(struct blkcipher_desc *desc, struct scatterlist *dst,
			  struct scatterlist *src, unsigned int nbytes)
{
	struct blkcipher_walk walk;
	u32 state[16];
	int err;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	err = blkcipher_walk_virt_block(desc, &walk, CHACHA20_BLOCK_SIZE);

	crypto_chacha20_init(state, crypto_blkcipher_ctx(desc->tfm), walk.iv);

	while (walk.nbytes >= CHACHA20_BLOCK_SIZE) {
		chacha20_docrypt(state, walk.dst.virt.addr, walk.src.virt.addr,
				 rounddown(walk.nbytes, CHACHA20_BLOCK_SIZE));
		err = blkcipher_walk_done(desc, &walk,
					  walk.nbytes % CHACHA20_BLOCK_SIZE);
	}

	if (walk.nbytes) {
		chacha20_docrypt(state, walk.dst.virt.addr, walk.src.virt.addr,
				 walk.nbytes);
		err = blkcipher_walk_done(desc, &walk, 0);
	}

	return err;
}
EXPORT_SYMBOL_GPL(crypto_chacha20_crypt);

static struct crypto_alg alg = {
	.cra_name		= "chacha20",
	.cra_driver_name	= "chacha20-generic",
	.cra_priority		= 100,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		= 1,
	.cra_type		= &crypto_blkcipher_type,
	.cra_ctxsize		= sizeof(struct chacha20_ctx),
	.cra_alignmask		= sizeof(u32) - 1,
	.cra_module		= THIS_MODULE,
	.cra_u			= {
		.blkcipher = {
			.min_keysize	= CHACHA20_KEY_SIZE,
			.max_keysize	= CHACHA20_KEY_SIZE,
			.ivsize		= CHACHA20_IV_SIZE,
			.geniv		= "seqiv",
			.setkey		= crypto_chacha20_setkey,
			.encrypt	= crypto_chacha20_crypt,
			.decrypt	= crypto_chacha20_crypt,
		},
	},
};

static int __init chacha20_generic_mod_init(void)
{
	return crypto_register_alg(&alg);
}

static void __exit chacha20_generic_mod_fini(void)
{
	crypto_unregister_alg(&alg);
}

module_init(chacha20_generic_mod_init);
module_exit(chacha20_generic_mod_fini);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Martin Willi <martin@strongswan.org>");
MODULE_DESCRIPTION("chacha20 cipher algorithm");
MODULE_ALIAS_CRYPTO("chacha20");
MODULE_ALIAS_CRYPTO("chacha20-generic");
