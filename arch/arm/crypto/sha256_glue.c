/*
 * Glue code for the SHA256 Secure Hash Algorithm assembly implementation
 * using optimized ARM assembler and NEON instructions.
 *
 * Copyright © 2015 Google Inc.
 *
 * This file is based on sha256_ssse3_glue.c:
 *   Copyright (C) 2013 Intel Corporation
 *   Author: Tim Chen <tim.c.chen@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#include <crypto/internal/hash.h>
#include <linux/crypto.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/cryptohash.h>
#include <linux/types.h>
#include <linux/string.h>
#include <crypto/sha.h>
#include <asm/byteorder.h>
#include <asm/simd.h>
#include <asm/neon.h>
#include "sha256_glue.h"

asmlinkage void sha256_block_data_order(u32 *digest, const void *data,
				      unsigned int num_blks);


int sha256_init(struct shash_desc *desc)
{
	struct sha256_state *sctx = shash_desc_ctx(desc);

	sctx->state[0] = SHA256_H0;
	sctx->state[1] = SHA256_H1;
	sctx->state[2] = SHA256_H2;
	sctx->state[3] = SHA256_H3;
	sctx->state[4] = SHA256_H4;
	sctx->state[5] = SHA256_H5;
	sctx->state[6] = SHA256_H6;
	sctx->state[7] = SHA256_H7;
	sctx->count = 0;

	return 0;
}

int sha224_init(struct shash_desc *desc)
{
	struct sha256_state *sctx = shash_desc_ctx(desc);

	sctx->state[0] = SHA224_H0;
	sctx->state[1] = SHA224_H1;
	sctx->state[2] = SHA224_H2;
	sctx->state[3] = SHA224_H3;
	sctx->state[4] = SHA224_H4;
	sctx->state[5] = SHA224_H5;
	sctx->state[6] = SHA224_H6;
	sctx->state[7] = SHA224_H7;
	sctx->count = 0;

	return 0;
}

int __sha256_update(struct shash_desc *desc, const u8 *data, unsigned int len,
		    unsigned int partial)
{
	struct sha256_state *sctx = shash_desc_ctx(desc);
	unsigned int done = 0;

	sctx->count += len;

	if (partial) {
		done = SHA256_BLOCK_SIZE - partial;
		memcpy(sctx->buf + partial, data, done);
		sha256_block_data_order(sctx->state, sctx->buf, 1);
	}

	if (len - done >= SHA256_BLOCK_SIZE) {
		const unsigned int rounds = (len - done) / SHA256_BLOCK_SIZE;

		sha256_block_data_order(sctx->state, data + done, rounds);
		done += rounds * SHA256_BLOCK_SIZE;
	}

	memcpy(sctx->buf, data + done, len - done);

	return 0;
}

int sha256_update(struct shash_desc *desc, const u8 *data, unsigned int len)
{
	struct sha256_state *sctx = shash_desc_ctx(desc);
	unsigned int partial = sctx->count % SHA256_BLOCK_SIZE;

	/* Handle the fast case right here */
	if (partial + len < SHA256_BLOCK_SIZE) {
		sctx->count += len;
		memcpy(sctx->buf + partial, data, len);

		return 0;
	}

	return __sha256_update(desc, data, len, partial);
}

/* Add padding and return the message digest. */
static int sha256_final(struct shash_desc *desc, u8 *out)
{
	struct sha256_state *sctx = shash_desc_ctx(desc);
	unsigned int i, index, padlen;
	__be32 *dst = (__be32 *)out;
	__be64 bits;
	static const u8 padding[SHA256_BLOCK_SIZE] = { 0x80, };

	/* save number of bits */
	bits = cpu_to_be64(sctx->count << 3);

	/* Pad out to 56 mod 64 and append length */
	index = sctx->count % SHA256_BLOCK_SIZE;
	padlen = (index < 56) ? (56 - index) : ((SHA256_BLOCK_SIZE+56)-index);

	/* We need to fill a whole block for __sha256_update */
	if (padlen <= 56) {
		sctx->count += padlen;
		memcpy(sctx->buf + index, padding, padlen);
	} else {
		__sha256_update(desc, padding, padlen, index);
	}
	__sha256_update(desc, (const u8 *)&bits, sizeof(bits), 56);

	/* Store state in digest */
	for (i = 0; i < 8; i++)
		dst[i] = cpu_to_be32(sctx->state[i]);

	/* Wipe context */
	memset(sctx, 0, sizeof(*sctx));

	return 0;
}

static int sha224_final(struct shash_desc *desc, u8 *out)
{
	u8 D[SHA256_DIGEST_SIZE];

	sha256_final(desc, D);

	memcpy(out, D, SHA224_DIGEST_SIZE);
	memzero_explicit(D, SHA256_DIGEST_SIZE);

	return 0;
}

int sha256_export(struct shash_desc *desc, void *out)
{
	struct sha256_state *sctx = shash_desc_ctx(desc);

	memcpy(out, sctx, sizeof(*sctx));

	return 0;
}

int sha256_import(struct shash_desc *desc, const void *in)
{
	struct sha256_state *sctx = shash_desc_ctx(desc);

	memcpy(sctx, in, sizeof(*sctx));

	return 0;
}

static struct shash_alg algs[] = { {
	.digestsize	=	SHA256_DIGEST_SIZE,
	.init		=	sha256_init,
	.update		=	sha256_update,
	.final		=	sha256_final,
	.export		=	sha256_export,
	.import		=	sha256_import,
	.descsize	=	sizeof(struct sha256_state),
	.statesize	=	sizeof(struct sha256_state),
	.base		=	{
		.cra_name	=	"sha256",
		.cra_driver_name =	"sha256-asm",
		.cra_priority	=	150,
		.cra_flags	=	CRYPTO_ALG_TYPE_SHASH,
		.cra_blocksize	=	SHA256_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
}, {
	.digestsize	=	SHA224_DIGEST_SIZE,
	.init		=	sha224_init,
	.update		=	sha256_update,
	.final		=	sha224_final,
	.export		=	sha256_export,
	.import		=	sha256_import,
	.descsize	=	sizeof(struct sha256_state),
	.statesize	=	sizeof(struct sha256_state),
	.base		=	{
		.cra_name	=	"sha224",
		.cra_driver_name =	"sha224-asm",
		.cra_priority	=	150,
		.cra_flags	=	CRYPTO_ALG_TYPE_SHASH,
		.cra_blocksize	=	SHA224_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
} };

static int __init sha256_mod_init(void)
{
	int res = crypto_register_shashes(algs, ARRAY_SIZE(algs));

	if (res < 0)
		return res;

	if (IS_ENABLED(CONFIG_KERNEL_MODE_NEON) && cpu_has_neon()) {
		res = crypto_register_shashes(sha256_neon_algs,
					      ARRAY_SIZE(sha256_neon_algs));

		if (res < 0)
			crypto_unregister_shashes(algs, ARRAY_SIZE(algs));
	}

	return res;
}

static void __exit sha256_mod_fini(void)
{
	crypto_unregister_shashes(algs, ARRAY_SIZE(algs));

	if (IS_ENABLED(CONFIG_KERNEL_MODE_NEON) && cpu_has_neon())
		crypto_unregister_shashes(sha256_neon_algs,
					  ARRAY_SIZE(sha256_neon_algs));
}

module_init(sha256_mod_init);
module_exit(sha256_mod_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SHA256 Secure Hash Algorithm (ARM), including NEON");

MODULE_ALIAS_CRYPTO("sha256");
