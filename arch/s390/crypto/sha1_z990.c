/*
 * Cryptographic API.
 *
 * z990 implementation of the SHA1 Secure Hash Algorithm.
 *
 * Derived from cryptoapi implementation, adapted for in-place
 * scatterlist interface.  Originally based on the public domain
 * implementation written by Steve Reid.
 *
 * s390 Version:
 *   Copyright (C) 2003 IBM Deutschland GmbH, IBM Corporation
 *   Author(s): Thomas Spatzier (tspat@de.ibm.com)
 *
 * Derived from "crypto/sha1.c"
 *   Copyright (c) Alan Smithee.
 *   Copyright (c) Andrew McDonald <andrew@mcdonald.org.uk>
 *   Copyright (c) Jean-Francois Dive <jef@linuxbe.org>
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
#include <asm/scatterlist.h>
#include <asm/byteorder.h>
#include "crypt_z990.h"

#define SHA1_DIGEST_SIZE	20
#define SHA1_BLOCK_SIZE		64

struct crypt_z990_sha1_ctx {
        u64 count;
        u32 state[5];
	u32 buf_len;
        u8 buffer[2 * SHA1_BLOCK_SIZE];
};

static void
sha1_init(void *ctx)
{
	static const struct crypt_z990_sha1_ctx initstate = {
		.state = {
			0x67452301,
			0xEFCDAB89,
			0x98BADCFE,
			0x10325476,
			0xC3D2E1F0
		},
	};
	memcpy(ctx, &initstate, sizeof(initstate));
}

static void
sha1_update(void *ctx, const u8 *data, unsigned int len)
{
	struct crypt_z990_sha1_ctx *sctx;
	long imd_len;

	sctx = ctx;
	sctx->count += len * 8; //message bit length

	//anything in buffer yet? -> must be completed
	if (sctx->buf_len && (sctx->buf_len + len) >= SHA1_BLOCK_SIZE) {
		//complete full block and hash
		memcpy(sctx->buffer + sctx->buf_len, data,
				SHA1_BLOCK_SIZE - sctx->buf_len);
		crypt_z990_kimd(KIMD_SHA_1, sctx->state, sctx->buffer,
				SHA1_BLOCK_SIZE);
		data += SHA1_BLOCK_SIZE - sctx->buf_len;
		len -= SHA1_BLOCK_SIZE - sctx->buf_len;
		sctx->buf_len = 0;
	}

	//rest of data contains full blocks?
	imd_len = len & ~0x3ful;
	if (imd_len){
		crypt_z990_kimd(KIMD_SHA_1, sctx->state, data, imd_len);
		data += imd_len;
		len -= imd_len;
	}
	//anything left? store in buffer
	if (len){
		memcpy(sctx->buffer + sctx->buf_len , data, len);
		sctx->buf_len += len;
	}
}


static void
pad_message(struct crypt_z990_sha1_ctx* sctx)
{
	int index;

	index = sctx->buf_len;
	sctx->buf_len = (sctx->buf_len < 56)?
		SHA1_BLOCK_SIZE:2 * SHA1_BLOCK_SIZE;
	//start pad with 1
	sctx->buffer[index] = 0x80;
	//pad with zeros
	index++;
	memset(sctx->buffer + index, 0x00, sctx->buf_len - index);
	//append length
	memcpy(sctx->buffer + sctx->buf_len - 8, &sctx->count,
			sizeof sctx->count);
}

/* Add padding and return the message digest. */
static void
sha1_final(void* ctx, u8 *out)
{
	struct crypt_z990_sha1_ctx *sctx = ctx;

	//must perform manual padding
	pad_message(sctx);
	crypt_z990_kimd(KIMD_SHA_1, sctx->state, sctx->buffer, sctx->buf_len);
	//copy digest to out
	memcpy(out, sctx->state, SHA1_DIGEST_SIZE);
	/* Wipe context */
	memset(sctx, 0, sizeof *sctx);
}

static struct crypto_alg alg = {
	.cra_name	=	"sha1",
	.cra_flags	=	CRYPTO_ALG_TYPE_DIGEST,
	.cra_blocksize	=	SHA1_BLOCK_SIZE,
	.cra_ctxsize	=	sizeof(struct crypt_z990_sha1_ctx),
	.cra_module	=	THIS_MODULE,
	.cra_list       =       LIST_HEAD_INIT(alg.cra_list),
	.cra_u		=	{ .digest = {
	.dia_digestsize	=	SHA1_DIGEST_SIZE,
	.dia_init   	= 	sha1_init,
	.dia_update 	=	sha1_update,
	.dia_final  	=	sha1_final } }
};

static int
init(void)
{
	int ret = -ENOSYS;

	if (crypt_z990_func_available(KIMD_SHA_1)){
		ret = crypto_register_alg(&alg);
		if (ret == 0){
			printk(KERN_INFO "crypt_z990: sha1_z990 loaded.\n");
		}
	}
	return ret;
}

static void __exit
fini(void)
{
	crypto_unregister_alg(&alg);
}

module_init(init);
module_exit(fini);

MODULE_ALIAS("sha1");

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SHA1 Secure Hash Algorithm");
