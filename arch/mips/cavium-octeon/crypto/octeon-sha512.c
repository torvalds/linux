// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Cryptographic API.
 *
 * SHA-512 and SHA-384 Secure Hash Algorithm.
 *
 * Adapted for OCTEON by Aaro Koskinen <aaro.koskinen@iki.fi>.
 *
 * Based on crypto/sha512_generic.c, which is:
 *
 * Copyright (c) Jean-Luc Cooke <jlcooke@certainkey.com>
 * Copyright (c) Andrew McDonald <andrew@mcdonald.org.uk>
 * Copyright (c) 2003 Kyle McMartin <kyle@debian.org>
 */

#include <linux/mm.h>
#include <crypto/sha.h>
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

static void octeon_sha512_store_hash(struct sha512_state *sctx)
{
	write_octeon_64bit_hash_sha512(sctx->state[0], 0);
	write_octeon_64bit_hash_sha512(sctx->state[1], 1);
	write_octeon_64bit_hash_sha512(sctx->state[2], 2);
	write_octeon_64bit_hash_sha512(sctx->state[3], 3);
	write_octeon_64bit_hash_sha512(sctx->state[4], 4);
	write_octeon_64bit_hash_sha512(sctx->state[5], 5);
	write_octeon_64bit_hash_sha512(sctx->state[6], 6);
	write_octeon_64bit_hash_sha512(sctx->state[7], 7);
}

static void octeon_sha512_read_hash(struct sha512_state *sctx)
{
	sctx->state[0] = read_octeon_64bit_hash_sha512(0);
	sctx->state[1] = read_octeon_64bit_hash_sha512(1);
	sctx->state[2] = read_octeon_64bit_hash_sha512(2);
	sctx->state[3] = read_octeon_64bit_hash_sha512(3);
	sctx->state[4] = read_octeon_64bit_hash_sha512(4);
	sctx->state[5] = read_octeon_64bit_hash_sha512(5);
	sctx->state[6] = read_octeon_64bit_hash_sha512(6);
	sctx->state[7] = read_octeon_64bit_hash_sha512(7);
}

static void octeon_sha512_transform(const void *_block)
{
	const u64 *block = _block;

	write_octeon_64bit_block_sha512(block[0], 0);
	write_octeon_64bit_block_sha512(block[1], 1);
	write_octeon_64bit_block_sha512(block[2], 2);
	write_octeon_64bit_block_sha512(block[3], 3);
	write_octeon_64bit_block_sha512(block[4], 4);
	write_octeon_64bit_block_sha512(block[5], 5);
	write_octeon_64bit_block_sha512(block[6], 6);
	write_octeon_64bit_block_sha512(block[7], 7);
	write_octeon_64bit_block_sha512(block[8], 8);
	write_octeon_64bit_block_sha512(block[9], 9);
	write_octeon_64bit_block_sha512(block[10], 10);
	write_octeon_64bit_block_sha512(block[11], 11);
	write_octeon_64bit_block_sha512(block[12], 12);
	write_octeon_64bit_block_sha512(block[13], 13);
	write_octeon_64bit_block_sha512(block[14], 14);
	octeon_sha512_start(block[15]);
}

static int octeon_sha512_init(struct shash_desc *desc)
{
	struct sha512_state *sctx = shash_desc_ctx(desc);

	sctx->state[0] = SHA512_H0;
	sctx->state[1] = SHA512_H1;
	sctx->state[2] = SHA512_H2;
	sctx->state[3] = SHA512_H3;
	sctx->state[4] = SHA512_H4;
	sctx->state[5] = SHA512_H5;
	sctx->state[6] = SHA512_H6;
	sctx->state[7] = SHA512_H7;
	sctx->count[0] = sctx->count[1] = 0;

	return 0;
}

static int octeon_sha384_init(struct shash_desc *desc)
{
	struct sha512_state *sctx = shash_desc_ctx(desc);

	sctx->state[0] = SHA384_H0;
	sctx->state[1] = SHA384_H1;
	sctx->state[2] = SHA384_H2;
	sctx->state[3] = SHA384_H3;
	sctx->state[4] = SHA384_H4;
	sctx->state[5] = SHA384_H5;
	sctx->state[6] = SHA384_H6;
	sctx->state[7] = SHA384_H7;
	sctx->count[0] = sctx->count[1] = 0;

	return 0;
}

static void __octeon_sha512_update(struct sha512_state *sctx, const u8 *data,
				   unsigned int len)
{
	unsigned int part_len;
	unsigned int index;
	unsigned int i;

	/* Compute number of bytes mod 128. */
	index = sctx->count[0] % SHA512_BLOCK_SIZE;

	/* Update number of bytes. */
	if ((sctx->count[0] += len) < len)
		sctx->count[1]++;

	part_len = SHA512_BLOCK_SIZE - index;

	/* Transform as many times as possible. */
	if (len >= part_len) {
		memcpy(&sctx->buf[index], data, part_len);
		octeon_sha512_transform(sctx->buf);

		for (i = part_len; i + SHA512_BLOCK_SIZE <= len;
			i += SHA512_BLOCK_SIZE)
			octeon_sha512_transform(&data[i]);

		index = 0;
	} else {
		i = 0;
	}

	/* Buffer remaining input. */
	memcpy(&sctx->buf[index], &data[i], len - i);
}

static int octeon_sha512_update(struct shash_desc *desc, const u8 *data,
				unsigned int len)
{
	struct sha512_state *sctx = shash_desc_ctx(desc);
	struct octeon_cop2_state state;
	unsigned long flags;

	/*
	 * Small updates never reach the crypto engine, so the generic sha512 is
	 * faster because of the heavyweight octeon_crypto_enable() /
	 * octeon_crypto_disable().
	 */
	if ((sctx->count[0] % SHA512_BLOCK_SIZE) + len < SHA512_BLOCK_SIZE)
		return crypto_sha512_update(desc, data, len);

	flags = octeon_crypto_enable(&state);
	octeon_sha512_store_hash(sctx);

	__octeon_sha512_update(sctx, data, len);

	octeon_sha512_read_hash(sctx);
	octeon_crypto_disable(&state, flags);

	return 0;
}

static int octeon_sha512_final(struct shash_desc *desc, u8 *hash)
{
	struct sha512_state *sctx = shash_desc_ctx(desc);
	static u8 padding[128] = { 0x80, };
	struct octeon_cop2_state state;
	__be64 *dst = (__be64 *)hash;
	unsigned int pad_len;
	unsigned long flags;
	unsigned int index;
	__be64 bits[2];
	int i;

	/* Save number of bits. */
	bits[1] = cpu_to_be64(sctx->count[0] << 3);
	bits[0] = cpu_to_be64(sctx->count[1] << 3 | sctx->count[0] >> 61);

	/* Pad out to 112 mod 128. */
	index = sctx->count[0] & 0x7f;
	pad_len = (index < 112) ? (112 - index) : ((128+112) - index);

	flags = octeon_crypto_enable(&state);
	octeon_sha512_store_hash(sctx);

	__octeon_sha512_update(sctx, padding, pad_len);

	/* Append length (before padding). */
	__octeon_sha512_update(sctx, (const u8 *)bits, sizeof(bits));

	octeon_sha512_read_hash(sctx);
	octeon_crypto_disable(&state, flags);

	/* Store state in digest. */
	for (i = 0; i < 8; i++)
		dst[i] = cpu_to_be64(sctx->state[i]);

	/* Zeroize sensitive information. */
	memset(sctx, 0, sizeof(struct sha512_state));

	return 0;
}

static int octeon_sha384_final(struct shash_desc *desc, u8 *hash)
{
	u8 D[64];

	octeon_sha512_final(desc, D);

	memcpy(hash, D, 48);
	memzero_explicit(D, 64);

	return 0;
}

static struct shash_alg octeon_sha512_algs[2] = { {
	.digestsize	=	SHA512_DIGEST_SIZE,
	.init		=	octeon_sha512_init,
	.update		=	octeon_sha512_update,
	.final		=	octeon_sha512_final,
	.descsize	=	sizeof(struct sha512_state),
	.base		=	{
		.cra_name	=	"sha512",
		.cra_driver_name=	"octeon-sha512",
		.cra_priority	=	OCTEON_CR_OPCODE_PRIORITY,
		.cra_blocksize	=	SHA512_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
}, {
	.digestsize	=	SHA384_DIGEST_SIZE,
	.init		=	octeon_sha384_init,
	.update		=	octeon_sha512_update,
	.final		=	octeon_sha384_final,
	.descsize	=	sizeof(struct sha512_state),
	.base		=	{
		.cra_name	=	"sha384",
		.cra_driver_name=	"octeon-sha384",
		.cra_priority	=	OCTEON_CR_OPCODE_PRIORITY,
		.cra_blocksize	=	SHA384_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
} };

static int __init octeon_sha512_mod_init(void)
{
	if (!octeon_has_crypto())
		return -ENOTSUPP;
	return crypto_register_shashes(octeon_sha512_algs,
				       ARRAY_SIZE(octeon_sha512_algs));
}

static void __exit octeon_sha512_mod_fini(void)
{
	crypto_unregister_shashes(octeon_sha512_algs,
				  ARRAY_SIZE(octeon_sha512_algs));
}

module_init(octeon_sha512_mod_init);
module_exit(octeon_sha512_mod_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SHA-512 and SHA-384 Secure Hash Algorithms (OCTEON)");
MODULE_AUTHOR("Aaro Koskinen <aaro.koskinen@iki.fi>");
