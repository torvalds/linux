/*
 * Cryptographic API
 *
 * ARC4 Cipher Algorithm
 *
 * Jon Oberheide <jon@oberheide.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/crypto.h>
#include <crypto/algapi.h>

#define ARC4_MIN_KEY_SIZE	1
#define ARC4_MAX_KEY_SIZE	256
#define ARC4_BLOCK_SIZE		1

struct arc4_ctx {
	u32 S[256];
	u32 x, y;
};

static int arc4_set_key(struct crypto_tfm *tfm, const u8 *in_key,
			unsigned int key_len)
{
	struct arc4_ctx *ctx = crypto_tfm_ctx(tfm);
	int i, j = 0, k = 0;

	ctx->x = 1;
	ctx->y = 0;

	for (i = 0; i < 256; i++)
		ctx->S[i] = i;

	for (i = 0; i < 256; i++) {
		u32 a = ctx->S[i];
		j = (j + in_key[k] + a) & 0xff;
		ctx->S[i] = ctx->S[j];
		ctx->S[j] = a;
		if (++k >= key_len)
			k = 0;
	}

	return 0;
}

static void arc4_crypt(struct arc4_ctx *ctx, u8 *out, const u8 *in,
		       unsigned int len)
{
	u32 *const S = ctx->S;
	u32 x, y, a, b;
	u32 ty, ta, tb;

	if (len == 0)
		return;

	x = ctx->x;
	y = ctx->y;

	a = S[x];
	y = (y + a) & 0xff;
	b = S[y];

	do {
		S[y] = a;
		a = (a + b) & 0xff;
		S[x] = b;
		x = (x + 1) & 0xff;
		ta = S[x];
		ty = (y + ta) & 0xff;
		tb = S[ty];
		*out++ = *in++ ^ S[a];
		if (--len == 0)
			break;
		y = ty;
		a = ta;
		b = tb;
	} while (true);

	ctx->x = x;
	ctx->y = y;
}

static void arc4_crypt_one(struct crypto_tfm *tfm, u8 *out, const u8 *in)
{
	arc4_crypt(crypto_tfm_ctx(tfm), out, in, 1);
}

static int ecb_arc4_crypt(struct blkcipher_desc *desc, struct scatterlist *dst,
			  struct scatterlist *src, unsigned int nbytes)
{
	struct arc4_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	struct blkcipher_walk walk;
	int err;

	blkcipher_walk_init(&walk, dst, src, nbytes);

	err = blkcipher_walk_virt(desc, &walk);

	while (walk.nbytes > 0) {
		u8 *wsrc = walk.src.virt.addr;
		u8 *wdst = walk.dst.virt.addr;

		arc4_crypt(ctx, wdst, wsrc, walk.nbytes);

		err = blkcipher_walk_done(desc, &walk, 0);
	}

	return err;
}

static struct crypto_alg arc4_algs[2] = { {
	.cra_name		=	"arc4",
	.cra_flags		=	CRYPTO_ALG_TYPE_CIPHER,
	.cra_blocksize		=	ARC4_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct arc4_ctx),
	.cra_module		=	THIS_MODULE,
	.cra_u			=	{
		.cipher = {
			.cia_min_keysize	=	ARC4_MIN_KEY_SIZE,
			.cia_max_keysize	=	ARC4_MAX_KEY_SIZE,
			.cia_setkey		=	arc4_set_key,
			.cia_encrypt		=	arc4_crypt_one,
			.cia_decrypt		=	arc4_crypt_one,
		},
	},
}, {
	.cra_name		=	"ecb(arc4)",
	.cra_priority		=	100,
	.cra_flags		=	CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		=	ARC4_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct arc4_ctx),
	.cra_alignmask		=	0,
	.cra_type		=	&crypto_blkcipher_type,
	.cra_module		=	THIS_MODULE,
	.cra_u			=	{
		.blkcipher = {
			.min_keysize	=	ARC4_MIN_KEY_SIZE,
			.max_keysize	=	ARC4_MAX_KEY_SIZE,
			.setkey		=	arc4_set_key,
			.encrypt	=	ecb_arc4_crypt,
			.decrypt	=	ecb_arc4_crypt,
		},
	},
} };

static int __init arc4_init(void)
{
	return crypto_register_algs(arc4_algs, ARRAY_SIZE(arc4_algs));
}

static void __exit arc4_exit(void)
{
	crypto_unregister_algs(arc4_algs, ARRAY_SIZE(arc4_algs));
}

module_init(arc4_init);
module_exit(arc4_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ARC4 Cipher Algorithm");
MODULE_AUTHOR("Jon Oberheide <jon@oberheide.org>");
