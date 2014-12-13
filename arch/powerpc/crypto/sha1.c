/*
 * Cryptographic API.
 *
 * powerpc implementation of the SHA1 Secure Hash Algorithm.
 *
 * Derived from cryptoapi implementation, adapted for in-place
 * scatterlist interface.
 *
 * Derived from "crypto/sha1.c"
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
#include <crypto/internal/hash.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/cryptohash.h>
#include <linux/types.h>
#include <crypto/sha.h>
#include <asm/byteorder.h>

extern void powerpc_sha_transform(u32 *state, const u8 *src, u32 *temp);

static int sha1_init(struct shash_desc *desc)
{
	struct sha1_state *sctx = shash_desc_ctx(desc);

	*sctx = (struct sha1_state){
		.state = { SHA1_H0, SHA1_H1, SHA1_H2, SHA1_H3, SHA1_H4 },
	};

	return 0;
}

static int sha1_update(struct shash_desc *desc, const u8 *data,
			unsigned int len)
{
	struct sha1_state *sctx = shash_desc_ctx(desc);
	unsigned int partial, done;
	const u8 *src;

	partial = sctx->count & 0x3f;
	sctx->count += len;
	done = 0;
	src = data;

	if ((partial + len) > 63) {
		u32 temp[SHA_WORKSPACE_WORDS];

		if (partial) {
			done = -partial;
			memcpy(sctx->buffer + partial, data, done + 64);
			src = sctx->buffer;
		}

		do {
			powerpc_sha_transform(sctx->state, src, temp);
			done += 64;
			src = data + done;
		} while (done + 63 < len);

		memzero_explicit(temp, sizeof(temp));
		partial = 0;
	}
	memcpy(sctx->buffer + partial, src, len - done);

	return 0;
}


/* Add padding and return the message digest. */
static int sha1_final(struct shash_desc *desc, u8 *out)
{
	struct sha1_state *sctx = shash_desc_ctx(desc);
	__be32 *dst = (__be32 *)out;
	u32 i, index, padlen;
	__be64 bits;
	static const u8 padding[64] = { 0x80, };

	bits = cpu_to_be64(sctx->count << 3);

	/* Pad out to 56 mod 64 */
	index = sctx->count & 0x3f;
	padlen = (index < 56) ? (56 - index) : ((64+56) - index);
	sha1_update(desc, padding, padlen);

	/* Append length */
	sha1_update(desc, (const u8 *)&bits, sizeof(bits));

	/* Store state in digest */
	for (i = 0; i < 5; i++)
		dst[i] = cpu_to_be32(sctx->state[i]);

	/* Wipe context */
	memset(sctx, 0, sizeof *sctx);

	return 0;
}

static int sha1_export(struct shash_desc *desc, void *out)
{
	struct sha1_state *sctx = shash_desc_ctx(desc);

	memcpy(out, sctx, sizeof(*sctx));
	return 0;
}

static int sha1_import(struct shash_desc *desc, const void *in)
{
	struct sha1_state *sctx = shash_desc_ctx(desc);

	memcpy(sctx, in, sizeof(*sctx));
	return 0;
}

static struct shash_alg alg = {
	.digestsize	=	SHA1_DIGEST_SIZE,
	.init		=	sha1_init,
	.update		=	sha1_update,
	.final		=	sha1_final,
	.export		=	sha1_export,
	.import		=	sha1_import,
	.descsize	=	sizeof(struct sha1_state),
	.statesize	=	sizeof(struct sha1_state),
	.base		=	{
		.cra_name	=	"sha1",
		.cra_driver_name=	"sha1-powerpc",
		.cra_flags	=	CRYPTO_ALG_TYPE_SHASH,
		.cra_blocksize	=	SHA1_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
};

static int __init sha1_powerpc_mod_init(void)
{
	return crypto_register_shash(&alg);
}

static void __exit sha1_powerpc_mod_fini(void)
{
	crypto_unregister_shash(&alg);
}

module_init(sha1_powerpc_mod_init);
module_exit(sha1_powerpc_mod_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SHA1 Secure Hash Algorithm");

MODULE_ALIAS_CRYPTO("sha1-powerpc");
