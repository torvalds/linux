// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Cryptographic API.
 *
 * SHA-224 and SHA-256 Secure Hash Algorithm.
 *
 * Adapted for OCTEON by Aaro Koskinen <aaro.koskinen@iki.fi>.
 *
 * Based on crypto/sha256_generic.c, which is:
 *
 * Copyright (c) Jean-Luc Cooke <jlcooke@certainkey.com>
 * Copyright (c) Andrew McDonald <andrew@mcdonald.org.uk>
 * Copyright (c) 2002 James Morris <jmorris@intercode.com.au>
 * SHA224 Support Copyright 2007 Intel Corporation <jonathan.lynch@intel.com>
 */

#include <linux/mm.h>
#include <crypto/sha2.h>
#include <crypto/sha256_base.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <asm/byteorder.h>
#include <asm/octeon/octeon.h>
#include <crypto/internal/hash.h>

#include "octeon-crypto.h"

/*
 * We pass everything as 64-bit. OCTEON can handle misaligned data.
 */

static void octeon_sha256_store_hash(struct sha256_state *sctx)
{
	u64 *hash = (u64 *)sctx->state;

	write_octeon_64bit_hash_dword(hash[0], 0);
	write_octeon_64bit_hash_dword(hash[1], 1);
	write_octeon_64bit_hash_dword(hash[2], 2);
	write_octeon_64bit_hash_dword(hash[3], 3);
}

static void octeon_sha256_read_hash(struct sha256_state *sctx)
{
	u64 *hash = (u64 *)sctx->state;

	hash[0] = read_octeon_64bit_hash_dword(0);
	hash[1] = read_octeon_64bit_hash_dword(1);
	hash[2] = read_octeon_64bit_hash_dword(2);
	hash[3] = read_octeon_64bit_hash_dword(3);
}

static void octeon_sha256_transform(const void *_block)
{
	const u64 *block = _block;

	write_octeon_64bit_block_dword(block[0], 0);
	write_octeon_64bit_block_dword(block[1], 1);
	write_octeon_64bit_block_dword(block[2], 2);
	write_octeon_64bit_block_dword(block[3], 3);
	write_octeon_64bit_block_dword(block[4], 4);
	write_octeon_64bit_block_dword(block[5], 5);
	write_octeon_64bit_block_dword(block[6], 6);
	octeon_sha256_start(block[7]);
}

static void __octeon_sha256_update(struct sha256_state *sctx, const u8 *data,
				   unsigned int len)
{
	unsigned int partial;
	unsigned int done;
	const u8 *src;

	partial = sctx->count % SHA256_BLOCK_SIZE;
	sctx->count += len;
	done = 0;
	src = data;

	if ((partial + len) >= SHA256_BLOCK_SIZE) {
		if (partial) {
			done = -partial;
			memcpy(sctx->buf + partial, data,
			       done + SHA256_BLOCK_SIZE);
			src = sctx->buf;
		}

		do {
			octeon_sha256_transform(src);
			done += SHA256_BLOCK_SIZE;
			src = data + done;
		} while (done + SHA256_BLOCK_SIZE <= len);

		partial = 0;
	}
	memcpy(sctx->buf + partial, src, len - done);
}

static int octeon_sha256_update(struct shash_desc *desc, const u8 *data,
				unsigned int len)
{
	struct sha256_state *sctx = shash_desc_ctx(desc);
	struct octeon_cop2_state state;
	unsigned long flags;

	/*
	 * Small updates never reach the crypto engine, so the generic sha256 is
	 * faster because of the heavyweight octeon_crypto_enable() /
	 * octeon_crypto_disable().
	 */
	if ((sctx->count % SHA256_BLOCK_SIZE) + len < SHA256_BLOCK_SIZE)
		return crypto_sha256_update(desc, data, len);

	flags = octeon_crypto_enable(&state);
	octeon_sha256_store_hash(sctx);

	__octeon_sha256_update(sctx, data, len);

	octeon_sha256_read_hash(sctx);
	octeon_crypto_disable(&state, flags);

	return 0;
}

static int octeon_sha256_final(struct shash_desc *desc, u8 *out)
{
	struct sha256_state *sctx = shash_desc_ctx(desc);
	static const u8 padding[64] = { 0x80, };
	struct octeon_cop2_state state;
	__be32 *dst = (__be32 *)out;
	unsigned int pad_len;
	unsigned long flags;
	unsigned int index;
	__be64 bits;
	int i;

	/* Save number of bits. */
	bits = cpu_to_be64(sctx->count << 3);

	/* Pad out to 56 mod 64. */
	index = sctx->count & 0x3f;
	pad_len = (index < 56) ? (56 - index) : ((64+56) - index);

	flags = octeon_crypto_enable(&state);
	octeon_sha256_store_hash(sctx);

	__octeon_sha256_update(sctx, padding, pad_len);

	/* Append length (before padding). */
	__octeon_sha256_update(sctx, (const u8 *)&bits, sizeof(bits));

	octeon_sha256_read_hash(sctx);
	octeon_crypto_disable(&state, flags);

	/* Store state in digest */
	for (i = 0; i < 8; i++)
		dst[i] = cpu_to_be32(sctx->state[i]);

	/* Zeroize sensitive information. */
	memset(sctx, 0, sizeof(*sctx));

	return 0;
}

static int octeon_sha224_final(struct shash_desc *desc, u8 *hash)
{
	u8 D[SHA256_DIGEST_SIZE];

	octeon_sha256_final(desc, D);

	memcpy(hash, D, SHA224_DIGEST_SIZE);
	memzero_explicit(D, SHA256_DIGEST_SIZE);

	return 0;
}

static int octeon_sha256_export(struct shash_desc *desc, void *out)
{
	struct sha256_state *sctx = shash_desc_ctx(desc);

	memcpy(out, sctx, sizeof(*sctx));
	return 0;
}

static int octeon_sha256_import(struct shash_desc *desc, const void *in)
{
	struct sha256_state *sctx = shash_desc_ctx(desc);

	memcpy(sctx, in, sizeof(*sctx));
	return 0;
}

static struct shash_alg octeon_sha256_algs[2] = { {
	.digestsize	=	SHA256_DIGEST_SIZE,
	.init		=	sha256_base_init,
	.update		=	octeon_sha256_update,
	.final		=	octeon_sha256_final,
	.export		=	octeon_sha256_export,
	.import		=	octeon_sha256_import,
	.descsize	=	sizeof(struct sha256_state),
	.statesize	=	sizeof(struct sha256_state),
	.base		=	{
		.cra_name	=	"sha256",
		.cra_driver_name=	"octeon-sha256",
		.cra_priority	=	OCTEON_CR_OPCODE_PRIORITY,
		.cra_blocksize	=	SHA256_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
}, {
	.digestsize	=	SHA224_DIGEST_SIZE,
	.init		=	sha224_base_init,
	.update		=	octeon_sha256_update,
	.final		=	octeon_sha224_final,
	.descsize	=	sizeof(struct sha256_state),
	.base		=	{
		.cra_name	=	"sha224",
		.cra_driver_name=	"octeon-sha224",
		.cra_blocksize	=	SHA224_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
} };

static int __init octeon_sha256_mod_init(void)
{
	if (!octeon_has_crypto())
		return -ENOTSUPP;
	return crypto_register_shashes(octeon_sha256_algs,
				       ARRAY_SIZE(octeon_sha256_algs));
}

static void __exit octeon_sha256_mod_fini(void)
{
	crypto_unregister_shashes(octeon_sha256_algs,
				  ARRAY_SIZE(octeon_sha256_algs));
}

module_init(octeon_sha256_mod_init);
module_exit(octeon_sha256_mod_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SHA-224 and SHA-256 Secure Hash Algorithm (OCTEON)");
MODULE_AUTHOR("Aaro Koskinen <aaro.koskinen@iki.fi>");
