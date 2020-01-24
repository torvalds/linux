/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * AEGIS common definitions
 *
 * Copyright (c) 2018 Ondrej Mosnacek <omosnacek@gmail.com>
 * Copyright (c) 2018 Red Hat, Inc. All rights reserved.
 */

#ifndef _CRYPTO_AEGIS_H
#define _CRYPTO_AEGIS_H

#include <crypto/aes.h>
#include <linux/bitops.h>
#include <linux/types.h>

#define AEGIS_BLOCK_SIZE 16

union aegis_block {
	__le64 words64[AEGIS_BLOCK_SIZE / sizeof(__le64)];
	__le32 words32[AEGIS_BLOCK_SIZE / sizeof(__le32)];
	u8 bytes[AEGIS_BLOCK_SIZE];
};

#define AEGIS_BLOCK_ALIGN (__alignof__(union aegis_block))
#define AEGIS_ALIGNED(p) IS_ALIGNED((uintptr_t)p, AEGIS_BLOCK_ALIGN)

static __always_inline void crypto_aegis_block_xor(union aegis_block *dst,
						   const union aegis_block *src)
{
	dst->words64[0] ^= src->words64[0];
	dst->words64[1] ^= src->words64[1];
}

static __always_inline void crypto_aegis_block_and(union aegis_block *dst,
						   const union aegis_block *src)
{
	dst->words64[0] &= src->words64[0];
	dst->words64[1] &= src->words64[1];
}

static __always_inline void crypto_aegis_aesenc(union aegis_block *dst,
						const union aegis_block *src,
						const union aegis_block *key)
{
	const u8  *s  = src->bytes;
	const u32 *t = crypto_ft_tab[0];
	u32 d0, d1, d2, d3;

	d0 = t[s[ 0]] ^ rol32(t[s[ 5]], 8) ^ rol32(t[s[10]], 16) ^ rol32(t[s[15]], 24);
	d1 = t[s[ 4]] ^ rol32(t[s[ 9]], 8) ^ rol32(t[s[14]], 16) ^ rol32(t[s[ 3]], 24);
	d2 = t[s[ 8]] ^ rol32(t[s[13]], 8) ^ rol32(t[s[ 2]], 16) ^ rol32(t[s[ 7]], 24);
	d3 = t[s[12]] ^ rol32(t[s[ 1]], 8) ^ rol32(t[s[ 6]], 16) ^ rol32(t[s[11]], 24);

	dst->words32[0] = cpu_to_le32(d0) ^ key->words32[0];
	dst->words32[1] = cpu_to_le32(d1) ^ key->words32[1];
	dst->words32[2] = cpu_to_le32(d2) ^ key->words32[2];
	dst->words32[3] = cpu_to_le32(d3) ^ key->words32[3];
}

#endif /* _CRYPTO_AEGIS_H */
