/*
 * Cryptographic API.
 *
 * Blowfish Cipher Algorithm, by Bruce Schneier.
 * http://www.counterpane.com/blowfish.html
 *
 * Adapted from Kerneli implementation.
 *
 * Copyright (c) Herbert Valerio Riedel <hvr@hvrlab.org>
 * Copyright (c) Kyle McMartin <kyle@debian.org>
 * Copyright (c) 2002 James Morris <jmorris@intercode.com.au>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <asm/byteorder.h>
#include <linux/crypto.h>
#include <linux/types.h>
#include <crypto/blowfish.h>

/*
 * Round loop unrolling macros, S is a pointer to a S-Box array
 * organized in 4 unsigned longs at a row.
 */
#define GET32_3(x) (((x) & 0xff))
#define GET32_2(x) (((x) >> (8)) & (0xff))
#define GET32_1(x) (((x) >> (16)) & (0xff))
#define GET32_0(x) (((x) >> (24)) & (0xff))

#define bf_F(x) (((S[GET32_0(x)] + S[256 + GET32_1(x)]) ^ \
		S[512 + GET32_2(x)]) + S[768 + GET32_3(x)])

#define ROUND(a, b, n) ({ b ^= P[n]; a ^= bf_F(b); })

static void bf_encrypt(struct crypto_tfm *tfm, u8 *dst, const u8 *src)
{
	struct bf_ctx *ctx = crypto_tfm_ctx(tfm);
	const __be32 *in_blk = (const __be32 *)src;
	__be32 *const out_blk = (__be32 *)dst;
	const u32 *P = ctx->p;
	const u32 *S = ctx->s;
	u32 yl = be32_to_cpu(in_blk[0]);
	u32 yr = be32_to_cpu(in_blk[1]);

	ROUND(yr, yl, 0);
	ROUND(yl, yr, 1);
	ROUND(yr, yl, 2);
	ROUND(yl, yr, 3);
	ROUND(yr, yl, 4);
	ROUND(yl, yr, 5);
	ROUND(yr, yl, 6);
	ROUND(yl, yr, 7);
	ROUND(yr, yl, 8);
	ROUND(yl, yr, 9);
	ROUND(yr, yl, 10);
	ROUND(yl, yr, 11);
	ROUND(yr, yl, 12);
	ROUND(yl, yr, 13);
	ROUND(yr, yl, 14);
	ROUND(yl, yr, 15);

	yl ^= P[16];
	yr ^= P[17];

	out_blk[0] = cpu_to_be32(yr);
	out_blk[1] = cpu_to_be32(yl);
}

static void bf_decrypt(struct crypto_tfm *tfm, u8 *dst, const u8 *src)
{
	struct bf_ctx *ctx = crypto_tfm_ctx(tfm);
	const __be32 *in_blk = (const __be32 *)src;
	__be32 *const out_blk = (__be32 *)dst;
	const u32 *P = ctx->p;
	const u32 *S = ctx->s;
	u32 yl = be32_to_cpu(in_blk[0]);
	u32 yr = be32_to_cpu(in_blk[1]);

	ROUND(yr, yl, 17);
	ROUND(yl, yr, 16);
	ROUND(yr, yl, 15);
	ROUND(yl, yr, 14);
	ROUND(yr, yl, 13);
	ROUND(yl, yr, 12);
	ROUND(yr, yl, 11);
	ROUND(yl, yr, 10);
	ROUND(yr, yl, 9);
	ROUND(yl, yr, 8);
	ROUND(yr, yl, 7);
	ROUND(yl, yr, 6);
	ROUND(yr, yl, 5);
	ROUND(yl, yr, 4);
	ROUND(yr, yl, 3);
	ROUND(yl, yr, 2);

	yl ^= P[1];
	yr ^= P[0];

	out_blk[0] = cpu_to_be32(yr);
	out_blk[1] = cpu_to_be32(yl);
}

static struct crypto_alg alg = {
	.cra_name		=	"blowfish",
	.cra_driver_name	=	"blowfish-generic",
	.cra_priority		=	100,
	.cra_flags		=	CRYPTO_ALG_TYPE_CIPHER,
	.cra_blocksize		=	BF_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct bf_ctx),
	.cra_alignmask		=	3,
	.cra_module		=	THIS_MODULE,
	.cra_u			=	{ .cipher = {
	.cia_min_keysize	=	BF_MIN_KEY_SIZE,
	.cia_max_keysize	=	BF_MAX_KEY_SIZE,
	.cia_setkey		=	blowfish_setkey,
	.cia_encrypt		=	bf_encrypt,
	.cia_decrypt		=	bf_decrypt } }
};

static int __init blowfish_mod_init(void)
{
	return crypto_register_alg(&alg);
}

static void __exit blowfish_mod_fini(void)
{
	crypto_unregister_alg(&alg);
}

subsys_initcall(blowfish_mod_init);
module_exit(blowfish_mod_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Blowfish Cipher Algorithm");
MODULE_ALIAS_CRYPTO("blowfish");
MODULE_ALIAS_CRYPTO("blowfish-generic");
