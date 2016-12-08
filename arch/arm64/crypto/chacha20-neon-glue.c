/*
 * ChaCha20 256-bit cipher algorithm, RFC7539, arm64 NEON functions
 *
 * Copyright (C) 2016 Linaro, Ltd. <ard.biesheuvel@linaro.org>
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
#include <crypto/chacha20.h>
#include <linux/crypto.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include <asm/neon.h>

asmlinkage void chacha20_block_xor_neon(u32 *state, u8 *dst, const u8 *src);
asmlinkage void chacha20_4block_xor_neon(u32 *state, u8 *dst, const u8 *src);

static void chacha20_dosimd(u32 *state, u8 *dst, const u8 *src,
			    unsigned int bytes)
{
	u8 buf[CHACHA20_BLOCK_SIZE];

	while (bytes >= CHACHA20_BLOCK_SIZE * 4) {
		chacha20_4block_xor_neon(state, dst, src);
		bytes -= CHACHA20_BLOCK_SIZE * 4;
		src += CHACHA20_BLOCK_SIZE * 4;
		dst += CHACHA20_BLOCK_SIZE * 4;
		state[12] += 4;
	}
	while (bytes >= CHACHA20_BLOCK_SIZE) {
		chacha20_block_xor_neon(state, dst, src);
		bytes -= CHACHA20_BLOCK_SIZE;
		src += CHACHA20_BLOCK_SIZE;
		dst += CHACHA20_BLOCK_SIZE;
		state[12]++;
	}
	if (bytes) {
		memcpy(buf, src, bytes);
		chacha20_block_xor_neon(state, buf, buf);
		memcpy(dst, buf, bytes);
	}
}

static int chacha20_simd(struct blkcipher_desc *desc, struct scatterlist *dst,
			 struct scatterlist *src, unsigned int nbytes)
{
	struct blkcipher_walk walk;
	u32 state[16];
	int err;

	if (nbytes <= CHACHA20_BLOCK_SIZE)
		return crypto_chacha20_crypt(desc, dst, src, nbytes);

	blkcipher_walk_init(&walk, dst, src, nbytes);
	err = blkcipher_walk_virt_block(desc, &walk, CHACHA20_BLOCK_SIZE);

	crypto_chacha20_init(state, crypto_blkcipher_ctx(desc->tfm), walk.iv);

	kernel_neon_begin();

	while (walk.nbytes >= CHACHA20_BLOCK_SIZE) {
		chacha20_dosimd(state, walk.dst.virt.addr, walk.src.virt.addr,
				rounddown(walk.nbytes, CHACHA20_BLOCK_SIZE));
		err = blkcipher_walk_done(desc, &walk,
					  walk.nbytes % CHACHA20_BLOCK_SIZE);
	}

	if (walk.nbytes) {
		chacha20_dosimd(state, walk.dst.virt.addr, walk.src.virt.addr,
				walk.nbytes);
		err = blkcipher_walk_done(desc, &walk, 0);
	}

	kernel_neon_end();

	return err;
}

static struct crypto_alg alg = {
	.cra_name		= "chacha20",
	.cra_driver_name	= "chacha20-neon",
	.cra_priority		= 300,
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
			.encrypt	= chacha20_simd,
			.decrypt	= chacha20_simd,
		},
	},
};

static int __init chacha20_simd_mod_init(void)
{
	return crypto_register_alg(&alg);
}

static void __exit chacha20_simd_mod_fini(void)
{
	crypto_unregister_alg(&alg);
}

module_init(chacha20_simd_mod_init);
module_exit(chacha20_simd_mod_fini);

MODULE_AUTHOR("Ard Biesheuvel <ard.biesheuvel@linaro.org>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS_CRYPTO("chacha20");
