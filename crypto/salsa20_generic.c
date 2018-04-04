/*
 * Salsa20: Salsa20 stream cipher algorithm
 *
 * Copyright (c) 2007 Tan Swee Heng <thesweeheng@gmail.com>
 *
 * Derived from:
 * - salsa20.c: Public domain C code by Daniel J. Bernstein <djb@cr.yp.to>
 *
 * Salsa20 is a stream cipher candidate in eSTREAM, the ECRYPT Stream
 * Cipher Project. It is designed by Daniel J. Bernstein <djb@cr.yp.to>.
 * More information about eSTREAM and Salsa20 can be found here:
 *   http://www.ecrypt.eu.org/stream/
 *   http://cr.yp.to/snuffle.html
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#include <asm/unaligned.h>
#include <crypto/internal/skcipher.h>
#include <crypto/salsa20.h>
#include <linux/module.h>

static void salsa20_block(u32 *state, __le32 *stream)
{
	u32 x[16];
	int i;

	memcpy(x, state, sizeof(x));

	for (i = 0; i < 20; i += 2) {
		x[ 4] ^= rol32((x[ 0] + x[12]),  7);
		x[ 8] ^= rol32((x[ 4] + x[ 0]),  9);
		x[12] ^= rol32((x[ 8] + x[ 4]), 13);
		x[ 0] ^= rol32((x[12] + x[ 8]), 18);
		x[ 9] ^= rol32((x[ 5] + x[ 1]),  7);
		x[13] ^= rol32((x[ 9] + x[ 5]),  9);
		x[ 1] ^= rol32((x[13] + x[ 9]), 13);
		x[ 5] ^= rol32((x[ 1] + x[13]), 18);
		x[14] ^= rol32((x[10] + x[ 6]),  7);
		x[ 2] ^= rol32((x[14] + x[10]),  9);
		x[ 6] ^= rol32((x[ 2] + x[14]), 13);
		x[10] ^= rol32((x[ 6] + x[ 2]), 18);
		x[ 3] ^= rol32((x[15] + x[11]),  7);
		x[ 7] ^= rol32((x[ 3] + x[15]),  9);
		x[11] ^= rol32((x[ 7] + x[ 3]), 13);
		x[15] ^= rol32((x[11] + x[ 7]), 18);
		x[ 1] ^= rol32((x[ 0] + x[ 3]),  7);
		x[ 2] ^= rol32((x[ 1] + x[ 0]),  9);
		x[ 3] ^= rol32((x[ 2] + x[ 1]), 13);
		x[ 0] ^= rol32((x[ 3] + x[ 2]), 18);
		x[ 6] ^= rol32((x[ 5] + x[ 4]),  7);
		x[ 7] ^= rol32((x[ 6] + x[ 5]),  9);
		x[ 4] ^= rol32((x[ 7] + x[ 6]), 13);
		x[ 5] ^= rol32((x[ 4] + x[ 7]), 18);
		x[11] ^= rol32((x[10] + x[ 9]),  7);
		x[ 8] ^= rol32((x[11] + x[10]),  9);
		x[ 9] ^= rol32((x[ 8] + x[11]), 13);
		x[10] ^= rol32((x[ 9] + x[ 8]), 18);
		x[12] ^= rol32((x[15] + x[14]),  7);
		x[13] ^= rol32((x[12] + x[15]),  9);
		x[14] ^= rol32((x[13] + x[12]), 13);
		x[15] ^= rol32((x[14] + x[13]), 18);
	}

	for (i = 0; i < 16; i++)
		stream[i] = cpu_to_le32(x[i] + state[i]);

	if (++state[8] == 0)
		state[9]++;
}

static void salsa20_docrypt(u32 *state, u8 *dst, const u8 *src,
			    unsigned int bytes)
{
	__le32 stream[SALSA20_BLOCK_SIZE / sizeof(__le32)];

	if (dst != src)
		memcpy(dst, src, bytes);

	while (bytes >= SALSA20_BLOCK_SIZE) {
		salsa20_block(state, stream);
		crypto_xor(dst, (const u8 *)stream, SALSA20_BLOCK_SIZE);
		bytes -= SALSA20_BLOCK_SIZE;
		dst += SALSA20_BLOCK_SIZE;
	}
	if (bytes) {
		salsa20_block(state, stream);
		crypto_xor(dst, (const u8 *)stream, bytes);
	}
}

void crypto_salsa20_init(u32 *state, const struct salsa20_ctx *ctx,
			 const u8 *iv)
{
	memcpy(state, ctx->initial_state, sizeof(ctx->initial_state));
	state[6] = get_unaligned_le32(iv + 0);
	state[7] = get_unaligned_le32(iv + 4);
}
EXPORT_SYMBOL_GPL(crypto_salsa20_init);

int crypto_salsa20_setkey(struct crypto_skcipher *tfm, const u8 *key,
			  unsigned int keysize)
{
	static const char sigma[16] = "expand 32-byte k";
	static const char tau[16] = "expand 16-byte k";
	struct salsa20_ctx *ctx = crypto_skcipher_ctx(tfm);
	const char *constants;

	if (keysize != SALSA20_MIN_KEY_SIZE &&
	    keysize != SALSA20_MAX_KEY_SIZE)
		return -EINVAL;

	ctx->initial_state[1] = get_unaligned_le32(key + 0);
	ctx->initial_state[2] = get_unaligned_le32(key + 4);
	ctx->initial_state[3] = get_unaligned_le32(key + 8);
	ctx->initial_state[4] = get_unaligned_le32(key + 12);
	if (keysize == 32) { /* recommended */
		key += 16;
		constants = sigma;
	} else { /* keysize == 16 */
		constants = tau;
	}
	ctx->initial_state[11] = get_unaligned_le32(key + 0);
	ctx->initial_state[12] = get_unaligned_le32(key + 4);
	ctx->initial_state[13] = get_unaligned_le32(key + 8);
	ctx->initial_state[14] = get_unaligned_le32(key + 12);
	ctx->initial_state[0]  = get_unaligned_le32(constants + 0);
	ctx->initial_state[5]  = get_unaligned_le32(constants + 4);
	ctx->initial_state[10] = get_unaligned_le32(constants + 8);
	ctx->initial_state[15] = get_unaligned_le32(constants + 12);

	/* space for the nonce; it will be overridden for each request */
	ctx->initial_state[6] = 0;
	ctx->initial_state[7] = 0;

	/* initial block number */
	ctx->initial_state[8] = 0;
	ctx->initial_state[9] = 0;

	return 0;
}
EXPORT_SYMBOL_GPL(crypto_salsa20_setkey);

static int salsa20_crypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	const struct salsa20_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_walk walk;
	u32 state[16];
	int err;

	err = skcipher_walk_virt(&walk, req, true);

	crypto_salsa20_init(state, ctx, walk.iv);

	while (walk.nbytes > 0) {
		unsigned int nbytes = walk.nbytes;

		if (nbytes < walk.total)
			nbytes = round_down(nbytes, walk.stride);

		salsa20_docrypt(state, walk.dst.virt.addr, walk.src.virt.addr,
				nbytes);
		err = skcipher_walk_done(&walk, walk.nbytes - nbytes);
	}

	return err;
}

static struct skcipher_alg alg = {
	.base.cra_name		= "salsa20",
	.base.cra_driver_name	= "salsa20-generic",
	.base.cra_priority	= 100,
	.base.cra_blocksize	= 1,
	.base.cra_ctxsize	= sizeof(struct salsa20_ctx),
	.base.cra_module	= THIS_MODULE,

	.min_keysize		= SALSA20_MIN_KEY_SIZE,
	.max_keysize		= SALSA20_MAX_KEY_SIZE,
	.ivsize			= SALSA20_IV_SIZE,
	.chunksize		= SALSA20_BLOCK_SIZE,
	.setkey			= crypto_salsa20_setkey,
	.encrypt		= salsa20_crypt,
	.decrypt		= salsa20_crypt,
};

static int __init salsa20_generic_mod_init(void)
{
	return crypto_register_skcipher(&alg);
}

static void __exit salsa20_generic_mod_fini(void)
{
	crypto_unregister_skcipher(&alg);
}

module_init(salsa20_generic_mod_init);
module_exit(salsa20_generic_mod_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION ("Salsa20 stream cipher algorithm");
MODULE_ALIAS_CRYPTO("salsa20");
MODULE_ALIAS_CRYPTO("salsa20-generic");
