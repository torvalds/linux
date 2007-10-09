/*
 * Cryptographic API.
 *
 * s390 implementation of the SHA1 Secure Hash Algorithm.
 *
 * Derived from cryptoapi implementation, adapted for in-place
 * scatterlist interface.  Originally based on the public domain
 * implementation written by Steve Reid.
 *
 * s390 Version:
 *   Copyright IBM Corp. 2003,2007
 *   Author(s): Thomas Spatzier
 *		Jan Glauber (jan.glauber@de.ibm.com)
 *
 * Derived from "crypto/sha1_generic.c"
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
#include <linux/crypto.h>
#include <crypto/sha.h>

#include "crypt_s390.h"

struct s390_sha1_ctx {
	u64 count;		/* message length */
	u32 state[5];
	u8 buf[2 * SHA1_BLOCK_SIZE];
};

static void sha1_init(struct crypto_tfm *tfm)
{
	struct s390_sha1_ctx *sctx = crypto_tfm_ctx(tfm);

	sctx->state[0] = SHA1_H0;
	sctx->state[1] = SHA1_H1;
	sctx->state[2] = SHA1_H2;
	sctx->state[3] = SHA1_H3;
	sctx->state[4] = SHA1_H4;
	sctx->count = 0;
}

static void sha1_update(struct crypto_tfm *tfm, const u8 *data,
			unsigned int len)
{
	struct s390_sha1_ctx *sctx = crypto_tfm_ctx(tfm);
	unsigned int index;
	int ret;

	/* how much is already in the buffer? */
	index = sctx->count & 0x3f;

	sctx->count += len;

	if (index + len < SHA1_BLOCK_SIZE)
		goto store;

	/* process one stored block */
	if (index) {
		memcpy(sctx->buf + index, data, SHA1_BLOCK_SIZE - index);
		ret = crypt_s390_kimd(KIMD_SHA_1, sctx->state, sctx->buf,
				      SHA1_BLOCK_SIZE);
		BUG_ON(ret != SHA1_BLOCK_SIZE);
		data += SHA1_BLOCK_SIZE - index;
		len -= SHA1_BLOCK_SIZE - index;
	}

	/* process as many blocks as possible */
	if (len >= SHA1_BLOCK_SIZE) {
		ret = crypt_s390_kimd(KIMD_SHA_1, sctx->state, data,
				      len & ~(SHA1_BLOCK_SIZE - 1));
		BUG_ON(ret != (len & ~(SHA1_BLOCK_SIZE - 1)));
		data += ret;
		len -= ret;
	}

store:
	/* anything left? */
	if (len)
		memcpy(sctx->buf + index , data, len);
}

/* Add padding and return the message digest. */
static void sha1_final(struct crypto_tfm *tfm, u8 *out)
{
	struct s390_sha1_ctx *sctx = crypto_tfm_ctx(tfm);
	u64 bits;
	unsigned int index, end;
	int ret;

	/* must perform manual padding */
	index = sctx->count & 0x3f;
	end =  (index < 56) ? SHA1_BLOCK_SIZE : (2 * SHA1_BLOCK_SIZE);

	/* start pad with 1 */
	sctx->buf[index] = 0x80;

	/* pad with zeros */
	index++;
	memset(sctx->buf + index, 0x00, end - index - 8);

	/* append message length */
	bits = sctx->count * 8;
	memcpy(sctx->buf + end - 8, &bits, sizeof(bits));

	ret = crypt_s390_kimd(KIMD_SHA_1, sctx->state, sctx->buf, end);
	BUG_ON(ret != end);

	/* copy digest to out */
	memcpy(out, sctx->state, SHA1_DIGEST_SIZE);

	/* wipe context */
	memset(sctx, 0, sizeof *sctx);
}

static struct crypto_alg alg = {
	.cra_name	=	"sha1",
	.cra_driver_name=	"sha1-s390",
	.cra_priority	=	CRYPT_S390_PRIORITY,
	.cra_flags	=	CRYPTO_ALG_TYPE_DIGEST,
	.cra_blocksize	=	SHA1_BLOCK_SIZE,
	.cra_ctxsize	=	sizeof(struct s390_sha1_ctx),
	.cra_module	=	THIS_MODULE,
	.cra_list	=	LIST_HEAD_INIT(alg.cra_list),
	.cra_u		=	{ .digest = {
	.dia_digestsize	=	SHA1_DIGEST_SIZE,
	.dia_init	=	sha1_init,
	.dia_update	=	sha1_update,
	.dia_final	=	sha1_final } }
};

static int __init init(void)
{
	if (!crypt_s390_func_available(KIMD_SHA_1))
		return -EOPNOTSUPP;

	return crypto_register_alg(&alg);
}

static void __exit fini(void)
{
	crypto_unregister_alg(&alg);
}

module_init(init);
module_exit(fini);

MODULE_ALIAS("sha1");

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SHA1 Secure Hash Algorithm");
