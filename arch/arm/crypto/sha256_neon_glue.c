/*
 * Glue code for the SHA256 Secure Hash Algorithm assembly implementation
 * using NEON instructions.
 *
 * Copyright © 2015 Google Inc.
 *
 * This file is based on sha512_neon_glue.c:
 *   Copyright © 2014 Jussi Kivilinna <jussi.kivilinna@iki.fi>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#include <crypto/internal/hash.h>
#include <linux/cryptohash.h>
#include <linux/types.h>
#include <linux/string.h>
#include <crypto/sha.h>
#include <asm/byteorder.h>
#include <asm/simd.h>
#include <asm/neon.h>
#include "sha256_glue.h"

asmlinkage void sha256_block_data_order_neon(u32 *digest, const void *data,
				      unsigned int num_blks);


static int __sha256_neon_update(struct shash_desc *desc, const u8 *data,
				unsigned int len, unsigned int partial)
{
	struct sha256_state *sctx = shash_desc_ctx(desc);
	unsigned int done = 0;

	sctx->count += len;

	if (partial) {
		done = SHA256_BLOCK_SIZE - partial;
		memcpy(sctx->buf + partial, data, done);
		sha256_block_data_order_neon(sctx->state, sctx->buf, 1);
	}

	if (len - done >= SHA256_BLOCK_SIZE) {
		const unsigned int rounds = (len - done) / SHA256_BLOCK_SIZE;

		sha256_block_data_order_neon(sctx->state, data + done, rounds);
		done += rounds * SHA256_BLOCK_SIZE;
	}

	memcpy(sctx->buf, data + done, len - done);

	return 0;
}

static int sha256_neon_update(struct shash_desc *desc, const u8 *data,
			      unsigned int len)
{
	struct sha256_state *sctx = shash_desc_ctx(desc);
	unsigned int partial = sctx->count % SHA256_BLOCK_SIZE;
	int res;

	/* Handle the fast case right here */
	if (partial + len < SHA256_BLOCK_SIZE) {
		sctx->count += len;
		memcpy(sctx->buf + partial, data, len);

		return 0;
	}

	if (!may_use_simd()) {
		res = __sha256_update(desc, data, len, partial);
	} else {
		kernel_neon_begin();
		res = __sha256_neon_update(desc, data, len, partial);
		kernel_neon_end();
	}

	return res;
}

/* Add padding and return the message digest. */
static int sha256_neon_final(struct shash_desc *desc, u8 *out)
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

	if (!may_use_simd()) {
		sha256_update(desc, padding, padlen);
		sha256_update(desc, (const u8 *)&bits, sizeof(bits));
	} else {
		kernel_neon_begin();
		/* We need to fill a whole block for __sha256_neon_update() */
		if (padlen <= 56) {
			sctx->count += padlen;
			memcpy(sctx->buf + index, padding, padlen);
		} else {
			__sha256_neon_update(desc, padding, padlen, index);
		}
		__sha256_neon_update(desc, (const u8 *)&bits,
					sizeof(bits), 56);
		kernel_neon_end();
	}

	/* Store state in digest */
	for (i = 0; i < 8; i++)
		dst[i] = cpu_to_be32(sctx->state[i]);

	/* Wipe context */
	memset(sctx, 0, sizeof(*sctx));

	return 0;
}

static int sha224_neon_final(struct shash_desc *desc, u8 *out)
{
	u8 D[SHA256_DIGEST_SIZE];

	sha256_neon_final(desc, D);

	memcpy(out, D, SHA224_DIGEST_SIZE);
	memset(D, 0, SHA256_DIGEST_SIZE);

	return 0;
}

struct shash_alg sha256_neon_algs[] = { {
	.digestsize	=	SHA256_DIGEST_SIZE,
	.init		=	sha256_init,
	.update		=	sha256_neon_update,
	.final		=	sha256_neon_final,
	.export		=	sha256_export,
	.import		=	sha256_import,
	.descsize	=	sizeof(struct sha256_state),
	.statesize	=	sizeof(struct sha256_state),
	.base		=	{
		.cra_name	=	"sha256",
		.cra_driver_name =	"sha256-neon",
		.cra_priority	=	250,
		.cra_flags	=	CRYPTO_ALG_TYPE_SHASH,
		.cra_blocksize	=	SHA256_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
}, {
	.digestsize	=	SHA224_DIGEST_SIZE,
	.init		=	sha224_init,
	.update		=	sha256_neon_update,
	.final		=	sha224_neon_final,
	.export		=	sha256_export,
	.import		=	sha256_import,
	.descsize	=	sizeof(struct sha256_state),
	.statesize	=	sizeof(struct sha256_state),
	.base		=	{
		.cra_name	=	"sha224",
		.cra_driver_name =	"sha224-neon",
		.cra_priority	=	250,
		.cra_flags	=	CRYPTO_ALG_TYPE_SHASH,
		.cra_blocksize	=	SHA224_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
} };
