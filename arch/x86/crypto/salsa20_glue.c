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
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#include <crypto/algapi.h>
#include <linux/module.h>
#include <linux/crypto.h>

#define SALSA20_IV_SIZE        8U
#define SALSA20_MIN_KEY_SIZE  16U
#define SALSA20_MAX_KEY_SIZE  32U

// use the ECRYPT_* function names
#define salsa20_keysetup        ECRYPT_keysetup
#define salsa20_ivsetup         ECRYPT_ivsetup
#define salsa20_encrypt_bytes   ECRYPT_encrypt_bytes

struct salsa20_ctx
{
	u32 input[16];
};

asmlinkage void salsa20_keysetup(struct salsa20_ctx *ctx, const u8 *k,
				 u32 keysize, u32 ivsize);
asmlinkage void salsa20_ivsetup(struct salsa20_ctx *ctx, const u8 *iv);
asmlinkage void salsa20_encrypt_bytes(struct salsa20_ctx *ctx,
				      const u8 *src, u8 *dst, u32 bytes);

static int setkey(struct crypto_tfm *tfm, const u8 *key,
		  unsigned int keysize)
{
	struct salsa20_ctx *ctx = crypto_tfm_ctx(tfm);
	salsa20_keysetup(ctx, key, keysize*8, SALSA20_IV_SIZE*8);
	return 0;
}

static int encrypt(struct blkcipher_desc *desc,
		   struct scatterlist *dst, struct scatterlist *src,
		   unsigned int nbytes)
{
	struct blkcipher_walk walk;
	struct crypto_blkcipher *tfm = desc->tfm;
	struct salsa20_ctx *ctx = crypto_blkcipher_ctx(tfm);
	int err;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	err = blkcipher_walk_virt_block(desc, &walk, 64);

	salsa20_ivsetup(ctx, walk.iv);

	if (likely(walk.nbytes == nbytes))
	{
		salsa20_encrypt_bytes(ctx, walk.src.virt.addr,
				      walk.dst.virt.addr, nbytes);
		return blkcipher_walk_done(desc, &walk, 0);
	}

	while (walk.nbytes >= 64) {
		salsa20_encrypt_bytes(ctx, walk.src.virt.addr,
				      walk.dst.virt.addr,
				      walk.nbytes - (walk.nbytes % 64));
		err = blkcipher_walk_done(desc, &walk, walk.nbytes % 64);
	}

	if (walk.nbytes) {
		salsa20_encrypt_bytes(ctx, walk.src.virt.addr,
				      walk.dst.virt.addr, walk.nbytes);
		err = blkcipher_walk_done(desc, &walk, 0);
	}

	return err;
}

static struct crypto_alg alg = {
	.cra_name           =   "salsa20",
	.cra_driver_name    =   "salsa20-asm",
	.cra_priority       =   200,
	.cra_flags          =   CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_type           =   &crypto_blkcipher_type,
	.cra_blocksize      =   1,
	.cra_ctxsize        =   sizeof(struct salsa20_ctx),
	.cra_alignmask      =	3,
	.cra_module         =   THIS_MODULE,
	.cra_list           =   LIST_HEAD_INIT(alg.cra_list),
	.cra_u              =   {
		.blkcipher = {
			.setkey         =   setkey,
			.encrypt        =   encrypt,
			.decrypt        =   encrypt,
			.min_keysize    =   SALSA20_MIN_KEY_SIZE,
			.max_keysize    =   SALSA20_MAX_KEY_SIZE,
			.ivsize         =   SALSA20_IV_SIZE,
		}
	}
};

static int __init init(void)
{
	return crypto_register_alg(&alg);
}

static void __exit fini(void)
{
	crypto_unregister_alg(&alg);
}

module_init(init);
module_exit(fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION ("Salsa20 stream cipher algorithm (optimized assembly version)");
MODULE_ALIAS("salsa20");
MODULE_ALIAS("salsa20-asm");
