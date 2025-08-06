// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Cryptographic API.
 *
 * SHA1 Secure Hash Algorithm.
 *
 * Adapted for OCTEON by Aaro Koskinen <aaro.koskinen@iki.fi>.
 *
 * Based on crypto/sha1_generic.c, which is:
 *
 * Copyright (c) Alan Smithee.
 * Copyright (c) Andrew McDonald <andrew@mcdonald.org.uk>
 * Copyright (c) Jean-Francois Dive <jef@linuxbe.org>
 */

#include <asm/octeon/octeon.h>
#include <crypto/internal/hash.h>
#include <crypto/sha1.h>
#include <crypto/sha1_base.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "octeon-crypto.h"

/*
 * We pass everything as 64-bit. OCTEON can handle misaligned data.
 */

static void octeon_sha1_store_hash(struct sha1_state *sctx)
{
	u64 *hash = (u64 *)sctx->state;
	union {
		u32 word[2];
		u64 dword;
	} hash_tail = { { sctx->state[4], } };

	write_octeon_64bit_hash_dword(hash[0], 0);
	write_octeon_64bit_hash_dword(hash[1], 1);
	write_octeon_64bit_hash_dword(hash_tail.dword, 2);
	memzero_explicit(&hash_tail.word[0], sizeof(hash_tail.word[0]));
}

static void octeon_sha1_read_hash(struct sha1_state *sctx)
{
	u64 *hash = (u64 *)sctx->state;
	union {
		u32 word[2];
		u64 dword;
	} hash_tail;

	hash[0]		= read_octeon_64bit_hash_dword(0);
	hash[1]		= read_octeon_64bit_hash_dword(1);
	hash_tail.dword	= read_octeon_64bit_hash_dword(2);
	sctx->state[4]	= hash_tail.word[0];
	memzero_explicit(&hash_tail.dword, sizeof(hash_tail.dword));
}

static void octeon_sha1_transform(struct sha1_state *sctx, const u8 *src,
				  int blocks)
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
		octeon_sha1_start(block[7]);

		src += SHA1_BLOCK_SIZE;
	} while (--blocks);
}

static int octeon_sha1_update(struct shash_desc *desc, const u8 *data,
			unsigned int len)
{
	struct sha1_state *sctx = shash_desc_ctx(desc);
	struct octeon_cop2_state state;
	unsigned long flags;
	int remain;

	flags = octeon_crypto_enable(&state);
	octeon_sha1_store_hash(sctx);

	remain = sha1_base_do_update_blocks(desc, data, len,
					    octeon_sha1_transform);

	octeon_sha1_read_hash(sctx);
	octeon_crypto_disable(&state, flags);
	return remain;
}

static int octeon_sha1_finup(struct shash_desc *desc, const u8 *src,
			     unsigned int len, u8 *out)
{
	struct sha1_state *sctx = shash_desc_ctx(desc);
	struct octeon_cop2_state state;
	unsigned long flags;

	flags = octeon_crypto_enable(&state);
	octeon_sha1_store_hash(sctx);

	sha1_base_do_finup(desc, src, len, octeon_sha1_transform);

	octeon_sha1_read_hash(sctx);
	octeon_crypto_disable(&state, flags);
	return sha1_base_finish(desc, out);
}

static struct shash_alg octeon_sha1_alg = {
	.digestsize	=	SHA1_DIGEST_SIZE,
	.init		=	sha1_base_init,
	.update		=	octeon_sha1_update,
	.finup		=	octeon_sha1_finup,
	.descsize	=	SHA1_STATE_SIZE,
	.base		=	{
		.cra_name	=	"sha1",
		.cra_driver_name=	"octeon-sha1",
		.cra_priority	=	OCTEON_CR_OPCODE_PRIORITY,
		.cra_flags	=	CRYPTO_AHASH_ALG_BLOCK_ONLY,
		.cra_blocksize	=	SHA1_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
};

static int __init octeon_sha1_mod_init(void)
{
	if (!octeon_has_crypto())
		return -ENOTSUPP;
	return crypto_register_shash(&octeon_sha1_alg);
}

static void __exit octeon_sha1_mod_fini(void)
{
	crypto_unregister_shash(&octeon_sha1_alg);
}

module_init(octeon_sha1_mod_init);
module_exit(octeon_sha1_mod_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SHA1 Secure Hash Algorithm (OCTEON)");
MODULE_AUTHOR("Aaro Koskinen <aaro.koskinen@iki.fi>");
