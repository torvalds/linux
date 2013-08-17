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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/crypto.h>
#include <linux/types.h>
#include <linux/bitops.h>
#include <crypto/algapi.h>
#include <asm/byteorder.h>

#define SALSA20_IV_SIZE        8U
#define SALSA20_MIN_KEY_SIZE  16U
#define SALSA20_MAX_KEY_SIZE  32U

/*
 * Start of code taken from D. J. Bernstein's reference implementation.
 * With some modifications and optimizations made to suit our needs.
 */

/*
salsa20-ref.c version 20051118
D. J. Bernstein
Public domain.
*/

#define U32TO8_LITTLE(p, v) \
	{ (p)[0] = (v >>  0) & 0xff; (p)[1] = (v >>  8) & 0xff; \
	  (p)[2] = (v >> 16) & 0xff; (p)[3] = (v >> 24) & 0xff; }
#define U8TO32_LITTLE(p)   \
	(((u32)((p)[0])      ) | ((u32)((p)[1]) <<  8) | \
	 ((u32)((p)[2]) << 16) | ((u32)((p)[3]) << 24)   )

struct salsa20_ctx
{
	u32 input[16];
};

static void salsa20_wordtobyte(u8 output[64], const u32 input[16])
{
	u32 x[16];
	int i;

	memcpy(x, input, sizeof(x));
	for (i = 20; i > 0; i -= 2) {
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
	for (i = 0; i < 16; ++i)
		x[i] += input[i];
	for (i = 0; i < 16; ++i)
		U32TO8_LITTLE(output + 4 * i,x[i]);
}

static const char sigma[16] = "expand 32-byte k";
static const char tau[16] = "expand 16-byte k";

static void salsa20_keysetup(struct salsa20_ctx *ctx, const u8 *k, u32 kbytes)
{
	const char *constants;

	ctx->input[1] = U8TO32_LITTLE(k + 0);
	ctx->input[2] = U8TO32_LITTLE(k + 4);
	ctx->input[3] = U8TO32_LITTLE(k + 8);
	ctx->input[4] = U8TO32_LITTLE(k + 12);
	if (kbytes == 32) { /* recommended */
		k += 16;
		constants = sigma;
	} else { /* kbytes == 16 */
		constants = tau;
	}
	ctx->input[11] = U8TO32_LITTLE(k + 0);
	ctx->input[12] = U8TO32_LITTLE(k + 4);
	ctx->input[13] = U8TO32_LITTLE(k + 8);
	ctx->input[14] = U8TO32_LITTLE(k + 12);
	ctx->input[0] = U8TO32_LITTLE(constants + 0);
	ctx->input[5] = U8TO32_LITTLE(constants + 4);
	ctx->input[10] = U8TO32_LITTLE(constants + 8);
	ctx->input[15] = U8TO32_LITTLE(constants + 12);
}

static void salsa20_ivsetup(struct salsa20_ctx *ctx, const u8 *iv)
{
	ctx->input[6] = U8TO32_LITTLE(iv + 0);
	ctx->input[7] = U8TO32_LITTLE(iv + 4);
	ctx->input[8] = 0;
	ctx->input[9] = 0;
}

static void salsa20_encrypt_bytes(struct salsa20_ctx *ctx, u8 *dst,
				  const u8 *src, unsigned int bytes)
{
	u8 buf[64];

	if (dst != src)
		memcpy(dst, src, bytes);

	while (bytes) {
		salsa20_wordtobyte(buf, ctx->input);

		ctx->input[8]++;
		if (!ctx->input[8])
			ctx->input[9]++;

		if (bytes <= 64) {
			crypto_xor(dst, buf, bytes);
			return;
		}

		crypto_xor(dst, buf, 64);
		bytes -= 64;
		dst += 64;
	}
}

/*
 * End of code taken from D. J. Bernstein's reference implementation.
 */

static int setkey(struct crypto_tfm *tfm, const u8 *key,
		  unsigned int keysize)
{
	struct salsa20_ctx *ctx = crypto_tfm_ctx(tfm);
	salsa20_keysetup(ctx, key, keysize);
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
		salsa20_encrypt_bytes(ctx, walk.dst.virt.addr,
				      walk.src.virt.addr, nbytes);
		return blkcipher_walk_done(desc, &walk, 0);
	}

	while (walk.nbytes >= 64) {
		salsa20_encrypt_bytes(ctx, walk.dst.virt.addr,
				      walk.src.virt.addr,
				      walk.nbytes - (walk.nbytes % 64));
		err = blkcipher_walk_done(desc, &walk, walk.nbytes % 64);
	}

	if (walk.nbytes) {
		salsa20_encrypt_bytes(ctx, walk.dst.virt.addr,
				      walk.src.virt.addr, walk.nbytes);
		err = blkcipher_walk_done(desc, &walk, 0);
	}

	return err;
}

static struct crypto_alg alg = {
	.cra_name           =   "salsa20",
	.cra_driver_name    =   "salsa20-generic",
	.cra_priority       =   100,
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

static int __init salsa20_generic_mod_init(void)
{
	return crypto_register_alg(&alg);
}

static void __exit salsa20_generic_mod_fini(void)
{
	crypto_unregister_alg(&alg);
}

module_init(salsa20_generic_mod_init);
module_exit(salsa20_generic_mod_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION ("Salsa20 stream cipher algorithm");
MODULE_ALIAS("salsa20");
