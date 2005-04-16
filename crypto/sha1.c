/*
 * Cryptographic API.
 *
 * SHA1 Secure Hash Algorithm.
 *
 * Derived from cryptoapi implementation, adapted for in-place
 * scatterlist interface.
 *
 * Copyright (c) Alan Smithee.
 * Copyright (c) Andrew McDonald <andrew@mcdonald.org.uk>
 * Copyright (c) Jean-Francois Dive <jef@linuxbe.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/crypto.h>
#include <linux/cryptohash.h>
#include <asm/scatterlist.h>
#include <asm/byteorder.h>

#define SHA1_DIGEST_SIZE	20
#define SHA1_HMAC_BLOCK_SIZE	64

struct sha1_ctx {
        u64 count;
        u32 state[5];
        u8 buffer[64];
};

static void sha1_init(void *ctx)
{
	struct sha1_ctx *sctx = ctx;
	static const struct sha1_ctx initstate = {
	  0,
	  { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 },
	  { 0, }
	};

	*sctx = initstate;
}

static void sha1_update(void *ctx, const u8 *data, unsigned int len)
{
	struct sha1_ctx *sctx = ctx;
	unsigned int i, j;
	u32 temp[SHA_WORKSPACE_WORDS];

	j = (sctx->count >> 3) & 0x3f;
	sctx->count += len << 3;

	if ((j + len) > 63) {
		memcpy(&sctx->buffer[j], data, (i = 64-j));
		sha_transform(sctx->state, sctx->buffer, temp);
		for ( ; i + 63 < len; i += 64) {
			sha_transform(sctx->state, &data[i], temp);
		}
		j = 0;
	}
	else i = 0;
	memset(temp, 0, sizeof(temp));
	memcpy(&sctx->buffer[j], &data[i], len - i);
}


/* Add padding and return the message digest. */
static void sha1_final(void* ctx, u8 *out)
{
	struct sha1_ctx *sctx = ctx;
	u32 i, j, index, padlen;
	u64 t;
	u8 bits[8] = { 0, };
	static const u8 padding[64] = { 0x80, };

	t = sctx->count;
	bits[7] = 0xff & t; t>>=8;
	bits[6] = 0xff & t; t>>=8;
	bits[5] = 0xff & t; t>>=8;
	bits[4] = 0xff & t; t>>=8;
	bits[3] = 0xff & t; t>>=8;
	bits[2] = 0xff & t; t>>=8;
	bits[1] = 0xff & t; t>>=8;
	bits[0] = 0xff & t;

	/* Pad out to 56 mod 64 */
	index = (sctx->count >> 3) & 0x3f;
	padlen = (index < 56) ? (56 - index) : ((64+56) - index);
	sha1_update(sctx, padding, padlen);

	/* Append length */
	sha1_update(sctx, bits, sizeof bits); 

	/* Store state in digest */
	for (i = j = 0; i < 5; i++, j += 4) {
		u32 t2 = sctx->state[i];
		out[j+3] = t2 & 0xff; t2>>=8;
		out[j+2] = t2 & 0xff; t2>>=8;
		out[j+1] = t2 & 0xff; t2>>=8;
		out[j  ] = t2 & 0xff;
	}

	/* Wipe context */
	memset(sctx, 0, sizeof *sctx);
}

static struct crypto_alg alg = {
	.cra_name	=	"sha1",
	.cra_flags	=	CRYPTO_ALG_TYPE_DIGEST,
	.cra_blocksize	=	SHA1_HMAC_BLOCK_SIZE,
	.cra_ctxsize	=	sizeof(struct sha1_ctx),
	.cra_module	=	THIS_MODULE,
	.cra_list       =       LIST_HEAD_INIT(alg.cra_list),
	.cra_u		=	{ .digest = {
	.dia_digestsize	=	SHA1_DIGEST_SIZE,
	.dia_init   	= 	sha1_init,
	.dia_update 	=	sha1_update,
	.dia_final  	=	sha1_final } }
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
MODULE_DESCRIPTION("SHA1 Secure Hash Algorithm");
