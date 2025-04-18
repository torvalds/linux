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

#include <asm/octeon/octeon.h>
#include <crypto/internal/hash.h>
#include <crypto/sha2.h>
#include <crypto/sha256_base.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "octeon-crypto.h"

/*
 * We pass everything as 64-bit. OCTEON can handle misaligned data.
 */

static void octeon_sha256_store_hash(struct crypto_sha256_state *sctx)
{
	u64 *hash = (u64 *)sctx->state;

	write_octeon_64bit_hash_dword(hash[0], 0);
	write_octeon_64bit_hash_dword(hash[1], 1);
	write_octeon_64bit_hash_dword(hash[2], 2);
	write_octeon_64bit_hash_dword(hash[3], 3);
}

static void octeon_sha256_read_hash(struct crypto_sha256_state *sctx)
{
	u64 *hash = (u64 *)sctx->state;

	hash[0] = read_octeon_64bit_hash_dword(0);
	hash[1] = read_octeon_64bit_hash_dword(1);
	hash[2] = read_octeon_64bit_hash_dword(2);
	hash[3] = read_octeon_64bit_hash_dword(3);
}

static void octeon_sha256_transform(struct crypto_sha256_state *sctx,
				    const u8 *src, int blocks)
{
	do {
		const u64 *block = (const u64 *)src;

		write_octeon_64bit_block_dword(block[0], 0);
		write_octeon_64bit_block_dword(block[1], 1);
		write_octeon_64bit_block_dword(block[2], 2);
		write_octeon_64bit_block_dword(block[3], 3);
		write_octeon_64bit_block_dword(block[4], 4);
		write_octeon_64bit_block_dword(block[5], 5);
		write_octeon_64bit_block_dword(block[6], 6);
		octeon_sha256_start(block[7]);

		src += SHA256_BLOCK_SIZE;
	} while (--blocks);
}

static int octeon_sha256_update(struct shash_desc *desc, const u8 *data,
				unsigned int len)
{
	struct crypto_sha256_state *sctx = shash_desc_ctx(desc);
	struct octeon_cop2_state state;
	unsigned long flags;
	int remain;

	flags = octeon_crypto_enable(&state);
	octeon_sha256_store_hash(sctx);

	remain = sha256_base_do_update_blocks(desc, data, len,
					      octeon_sha256_transform);

	octeon_sha256_read_hash(sctx);
	octeon_crypto_disable(&state, flags);
	return remain;
}

static int octeon_sha256_finup(struct shash_desc *desc, const u8 *src,
			       unsigned int len, u8 *out)
{
	struct crypto_sha256_state *sctx = shash_desc_ctx(desc);
	struct octeon_cop2_state state;
	unsigned long flags;

	flags = octeon_crypto_enable(&state);
	octeon_sha256_store_hash(sctx);

	sha256_base_do_finup(desc, src, len, octeon_sha256_transform);

	octeon_sha256_read_hash(sctx);
	octeon_crypto_disable(&state, flags);
	return sha256_base_finish(desc, out);
}

static struct shash_alg octeon_sha256_algs[2] = { {
	.digestsize	=	SHA256_DIGEST_SIZE,
	.init		=	sha256_base_init,
	.update		=	octeon_sha256_update,
	.finup		=	octeon_sha256_finup,
	.descsize	=	sizeof(struct crypto_sha256_state),
	.base		=	{
		.cra_name	=	"sha256",
		.cra_driver_name=	"octeon-sha256",
		.cra_priority	=	OCTEON_CR_OPCODE_PRIORITY,
		.cra_flags	=	CRYPTO_AHASH_ALG_BLOCK_ONLY,
		.cra_blocksize	=	SHA256_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
}, {
	.digestsize	=	SHA224_DIGEST_SIZE,
	.init		=	sha224_base_init,
	.update		=	octeon_sha256_update,
	.finup		=	octeon_sha256_finup,
	.descsize	=	sizeof(struct crypto_sha256_state),
	.base		=	{
		.cra_name	=	"sha224",
		.cra_driver_name=	"octeon-sha224",
		.cra_priority	=	OCTEON_CR_OPCODE_PRIORITY,
		.cra_flags	=	CRYPTO_AHASH_ALG_BLOCK_ONLY,
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
