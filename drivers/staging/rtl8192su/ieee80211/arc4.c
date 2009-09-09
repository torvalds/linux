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
#include "rtl_crypto.h"

#define ARC4_MIN_KEY_SIZE	1
#define ARC4_MAX_KEY_SIZE	256
#define ARC4_BLOCK_SIZE		1

struct arc4_ctx {
	u8 S[256];
	u8 x, y;
};

static int arc4_set_key(void *ctx_arg, const u8 *in_key, unsigned int key_len, u32 *flags)
{
	struct arc4_ctx *ctx = ctx_arg;
	int i, j = 0, k = 0;

	ctx->x = 1;
	ctx->y = 0;

	for(i = 0; i < 256; i++)
		ctx->S[i] = i;

	for(i = 0; i < 256; i++)
	{
		u8 a = ctx->S[i];
		j = (j + in_key[k] + a) & 0xff;
		ctx->S[i] = ctx->S[j];
		ctx->S[j] = a;
		if((unsigned int)++k >= key_len)
			k = 0;
	}

	return 0;
}

static void arc4_crypt(void *ctx_arg, u8 *out, const u8 *in)
{
	struct arc4_ctx *ctx = ctx_arg;

	u8 *const S = ctx->S;
	u8 x = ctx->x;
	u8 y = ctx->y;
	u8 a, b;

	a = S[x];
	y = (y + a) & 0xff;
	b = S[y];
	S[x] = b;
	S[y] = a;
	x = (x + 1) & 0xff;
	*out++ = *in ^ S[(a + b) & 0xff];

	ctx->x = x;
	ctx->y = y;
}

static struct crypto_alg arc4_alg = {
	.cra_name		=	"arc4",
	.cra_flags		=	CRYPTO_ALG_TYPE_CIPHER,
	.cra_blocksize		=	ARC4_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct arc4_ctx),
	.cra_module		=	THIS_MODULE,
	.cra_list		=	LIST_HEAD_INIT(arc4_alg.cra_list),
	.cra_u			=	{ .cipher = {
	.cia_min_keysize	=	ARC4_MIN_KEY_SIZE,
	.cia_max_keysize	=	ARC4_MAX_KEY_SIZE,
	.cia_setkey	   	= 	arc4_set_key,
	.cia_encrypt	 	=	arc4_crypt,
	.cia_decrypt	  	=	arc4_crypt } }
};

static int __init arc4_init(void)
{
	return crypto_register_alg(&arc4_alg);
}


static void __exit arc4_exit(void)
{
	crypto_unregister_alg(&arc4_alg);
}

module_init(arc4_init);
module_exit(arc4_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ARC4 Cipher Algorithm");
MODULE_AUTHOR("Jon Oberheide <jon@oberheide.org>");
