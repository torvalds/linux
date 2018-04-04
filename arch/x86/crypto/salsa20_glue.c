/*
 * Glue code for optimized assembly version of  Salsa20.
 *
 * Copyright (c) 2007 Tan Swee Heng <thesweeheng@gmail.com>
 *
 * The assembly codes are public domain assembly codes written by Daniel. J.
 * Bernstein <djb@cr.yp.to>. The codes are modified to include indentation
 * and to remove extraneous comments and functions that are not needed.
 * - i586 version, renamed as salsa20-i586-asm_32.S
 *   available from <http://cr.yp.to/snuffle/salsa20/x86-pm/salsa20.s>
 * - x86-64 version, renamed as salsa20-x86_64-asm_64.S
 *   available from <http://cr.yp.to/snuffle/salsa20/amd64-3/salsa20.s>
 *
 * Also modified to set up the initial state using the generic C code rather
 * than in assembly.
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

asmlinkage void salsa20_encrypt_bytes(u32 state[16], const u8 *src, u8 *dst,
				      u32 bytes);

static int salsa20_asm_crypt(struct skcipher_request *req)
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

		salsa20_encrypt_bytes(state, walk.src.virt.addr,
				      walk.dst.virt.addr, nbytes);
		err = skcipher_walk_done(&walk, walk.nbytes - nbytes);
	}

	return err;
}

static struct skcipher_alg alg = {
	.base.cra_name		= "salsa20",
	.base.cra_driver_name	= "salsa20-asm",
	.base.cra_priority	= 200,
	.base.cra_blocksize	= 1,
	.base.cra_ctxsize	= sizeof(struct salsa20_ctx),
	.base.cra_module	= THIS_MODULE,

	.min_keysize		= SALSA20_MIN_KEY_SIZE,
	.max_keysize		= SALSA20_MAX_KEY_SIZE,
	.ivsize			= SALSA20_IV_SIZE,
	.chunksize		= SALSA20_BLOCK_SIZE,
	.setkey			= crypto_salsa20_setkey,
	.encrypt		= salsa20_asm_crypt,
	.decrypt		= salsa20_asm_crypt,
};

static int __init init(void)
{
	return crypto_register_skcipher(&alg);
}

static void __exit fini(void)
{
	crypto_unregister_skcipher(&alg);
}

module_init(init);
module_exit(fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION ("Salsa20 stream cipher algorithm (optimized assembly version)");
MODULE_ALIAS_CRYPTO("salsa20");
MODULE_ALIAS_CRYPTO("salsa20-asm");
