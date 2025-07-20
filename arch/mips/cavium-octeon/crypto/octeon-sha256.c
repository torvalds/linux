// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * SHA-256 Secure Hash Algorithm.
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
#include <crypto/internal/sha2.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "octeon-crypto.h"

/*
 * We pass everything as 64-bit. OCTEON can handle misaligned data.
 */

void sha256_blocks_arch(u32 state[SHA256_STATE_WORDS],
			const u8 *data, size_t nblocks)
{
	struct octeon_cop2_state cop2_state;
	u64 *state64 = (u64 *)state;
	unsigned long flags;

	if (!octeon_has_crypto())
		return sha256_blocks_generic(state, data, nblocks);

	flags = octeon_crypto_enable(&cop2_state);
	write_octeon_64bit_hash_dword(state64[0], 0);
	write_octeon_64bit_hash_dword(state64[1], 1);
	write_octeon_64bit_hash_dword(state64[2], 2);
	write_octeon_64bit_hash_dword(state64[3], 3);

	do {
		const u64 *block = (const u64 *)data;

		write_octeon_64bit_block_dword(block[0], 0);
		write_octeon_64bit_block_dword(block[1], 1);
		write_octeon_64bit_block_dword(block[2], 2);
		write_octeon_64bit_block_dword(block[3], 3);
		write_octeon_64bit_block_dword(block[4], 4);
		write_octeon_64bit_block_dword(block[5], 5);
		write_octeon_64bit_block_dword(block[6], 6);
		octeon_sha256_start(block[7]);

		data += SHA256_BLOCK_SIZE;
	} while (--nblocks);

	state64[0] = read_octeon_64bit_hash_dword(0);
	state64[1] = read_octeon_64bit_hash_dword(1);
	state64[2] = read_octeon_64bit_hash_dword(2);
	state64[3] = read_octeon_64bit_hash_dword(3);
	octeon_crypto_disable(&cop2_state, flags);
}
EXPORT_SYMBOL_GPL(sha256_blocks_arch);

bool sha256_is_arch_optimized(void)
{
	return octeon_has_crypto();
}
EXPORT_SYMBOL_GPL(sha256_is_arch_optimized);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SHA-256 Secure Hash Algorithm (OCTEON)");
MODULE_AUTHOR("Aaro Koskinen <aaro.koskinen@iki.fi>");
