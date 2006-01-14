/*
 * Cryptographic API.
 *
 * s390 implementation of the SHA256 Secure Hash Algorithm.
 *
 * s390 Version:
 *   Copyright (C) 2005 IBM Deutschland GmbH, IBM Corporation
 *   Author(s): Jan Glauber (jang@de.ibm.com)
 *
 * Derived from "crypto/sha256.c"
 * and "arch/s390/crypto/sha1_s390.c"
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/crypto.h>

#include "crypt_s390.h"

#define SHA256_DIGEST_SIZE	32
#define SHA256_BLOCK_SIZE	64

struct s390_sha256_ctx {
	u64 count;
	u32 state[8];
	u8 buf[2 * SHA256_BLOCK_SIZE];
};

static void sha256_init(void *ctx)
{
	struct s390_sha256_ctx *sctx = ctx;

	sctx->state[0] = 0x6a09e667;
	sctx->state[1] = 0xbb67ae85;
	sctx->state[2] = 0x3c6ef372;
	sctx->state[3] = 0xa54ff53a;
	sctx->state[4] = 0x510e527f;
	sctx->state[5] = 0x9b05688c;
	sctx->state[6] = 0x1f83d9ab;
	sctx->state[7] = 0x5be0cd19;
	sctx->count = 0;
	memset(sctx->buf, 0, sizeof(sctx->buf));
}

static void sha256_update(void *ctx, const u8 *data, unsigned int len)
{
	struct s390_sha256_ctx *sctx = ctx;
	unsigned int index;
	int ret;

	/* how much is already in the buffer? */
	index = sctx->count / 8 & 0x3f;

	/* update message bit length */
	sctx->count += len * 8;

	if ((index + len) < SHA256_BLOCK_SIZE)
		goto store;

	/* process one stored block */
	if (index) {
		memcpy(sctx->buf + index, data, SHA256_BLOCK_SIZE - index);
		ret = crypt_s390_kimd(KIMD_SHA_256, sctx->state, sctx->buf,
				      SHA256_BLOCK_SIZE);
		BUG_ON(ret != SHA256_BLOCK_SIZE);
		data += SHA256_BLOCK_SIZE - index;
		len -= SHA256_BLOCK_SIZE - index;
	}

	/* process as many blocks as possible */
	if (len >= SHA256_BLOCK_SIZE) {
		ret = crypt_s390_kimd(KIMD_SHA_256, sctx->state, data,
				      len & ~(SHA256_BLOCK_SIZE - 1));
		BUG_ON(ret != (len & ~(SHA256_BLOCK_SIZE - 1)));
		data += ret;
		len -= ret;
	}

store:
	/* anything left? */
	if (len)
		memcpy(sctx->buf + index , data, len);
}

static void pad_message(struct s390_sha256_ctx* sctx)
{
	int index, end;

	index = sctx->count / 8 & 0x3f;
	end = index < 56 ? SHA256_BLOCK_SIZE : 2 * SHA256_BLOCK_SIZE;

	/* start pad with 1 */
	sctx->buf[index] = 0x80;

	/* pad with zeros */
	index++;
	memset(sctx->buf + index, 0x00, end - index - 8);

	/* append message length */
	memcpy(sctx->buf + end - 8, &sctx->count, sizeof sctx->count);

	sctx->count = end * 8;
}

/* Add padding and return the message digest */
static void sha256_final(void* ctx, u8 *out)
{
	struct s390_sha256_ctx *sctx = ctx;

	/* must perform manual padding */
	pad_message(sctx);

	crypt_s390_kimd(KIMD_SHA_256, sctx->state, sctx->buf,
			sctx->count / 8);

	/* copy digest to out */
	memcpy(out, sctx->state, SHA256_DIGEST_SIZE);

	/* wipe context */
	memset(sctx, 0, sizeof *sctx);
}

static struct crypto_alg alg = {
	.cra_name	=	"sha256",
	.cra_flags	=	CRYPTO_ALG_TYPE_DIGEST,
	.cra_blocksize	=	SHA256_BLOCK_SIZE,
	.cra_ctxsize	=	sizeof(struct s390_sha256_ctx),
	.cra_module	=	THIS_MODULE,
	.cra_list	=	LIST_HEAD_INIT(alg.cra_list),
	.cra_u		=	{ .digest = {
	.dia_digestsize	=	SHA256_DIGEST_SIZE,
	.dia_init	=	sha256_init,
	.dia_update	=	sha256_update,
	.dia_final	=	sha256_final } }
};

static int init(void)
{
	int ret;

	if (!crypt_s390_func_available(KIMD_SHA_256))
		return -ENOSYS;

	ret = crypto_register_alg(&alg);
	if (ret != 0)
		printk(KERN_INFO "crypt_s390: sha256_s390 couldn't be loaded.");
	return ret;
}

static void __exit fini(void)
{
	crypto_unregister_alg(&alg);
}

module_init(init);
module_exit(fini);

MODULE_ALIAS("sha256");

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SHA256 Secure Hash Algorithm");
