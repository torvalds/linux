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

#include <asm/octeon/octeon.h>
#include <crypto/internal/hash.h>
#include <crypto/sha2.h>
#include <crypto/sha512_base.h>
#include <linux/kernel.h>
#include <linux/module.h>

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

static void octeon_sha512_transform(struct sha512_state *sctx,
				    const u8 *src, int blocks)
{
	do {
		const u64 *block = (const u64 *)src;

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

		src += SHA512_BLOCK_SIZE;
	} while (--blocks);
}

static int octeon_sha512_update(struct shash_desc *desc, const u8 *data,
				unsigned int len)
{
	struct sha512_state *sctx = shash_desc_ctx(desc);
	struct octeon_cop2_state state;
	unsigned long flags;
	int remain;

	flags = octeon_crypto_enable(&state);
	octeon_sha512_store_hash(sctx);

	remain = sha512_base_do_update_blocks(desc, data, len,
					      octeon_sha512_transform);

	octeon_sha512_read_hash(sctx);
	octeon_crypto_disable(&state, flags);
	return remain;
}

static int octeon_sha512_finup(struct shash_desc *desc, const u8 *src,
			       unsigned int len, u8 *hash)
{
	struct sha512_state *sctx = shash_desc_ctx(desc);
	struct octeon_cop2_state state;
	unsigned long flags;

	flags = octeon_crypto_enable(&state);
	octeon_sha512_store_hash(sctx);

	sha512_base_do_finup(desc, src, len, octeon_sha512_transform);

	octeon_sha512_read_hash(sctx);
	octeon_crypto_disable(&state, flags);
	return sha512_base_finish(desc, hash);
}

static struct shash_alg octeon_sha512_algs[2] = { {
	.digestsize	=	SHA512_DIGEST_SIZE,
	.init		=	sha512_base_init,
	.update		=	octeon_sha512_update,
	.finup		=	octeon_sha512_finup,
	.descsize	=	SHA512_STATE_SIZE,
	.base		=	{
		.cra_name	=	"sha512",
		.cra_driver_name=	"octeon-sha512",
		.cra_priority	=	OCTEON_CR_OPCODE_PRIORITY,
		.cra_flags	=	CRYPTO_AHASH_ALG_BLOCK_ONLY |
					CRYPTO_AHASH_ALG_FINUP_MAX,
		.cra_blocksize	=	SHA512_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
}, {
	.digestsize	=	SHA384_DIGEST_SIZE,
	.init		=	sha384_base_init,
	.update		=	octeon_sha512_update,
	.finup		=	octeon_sha512_finup,
	.descsize	=	SHA512_STATE_SIZE,
	.base		=	{
		.cra_name	=	"sha384",
		.cra_driver_name=	"octeon-sha384",
		.cra_priority	=	OCTEON_CR_OPCODE_PRIORITY,
		.cra_flags	=	CRYPTO_AHASH_ALG_BLOCK_ONLY |
					CRYPTO_AHASH_ALG_FINUP_MAX,
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
