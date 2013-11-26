/*
 * Copyright 2013 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 *
 * Provide a mask based on the pointer alignment that
 * sets up non-zero bytes before the beginning of the string.
 * The MASK expression works because shift counts are taken mod 64.
 * Also, specify how to count "first" and "last" bits
 * when the bits have been read as a word.
 */

#include <asm/byteorder.h>

#ifdef __LITTLE_ENDIAN
#define MASK(x) (__insn_shl(1ULL, (x << 3)) - 1)
#define NULMASK(x) ((2ULL << x) - 1)
#define CFZ(x) __insn_ctz(x)
#define REVCZ(x) __insn_clz(x)
#else
#define MASK(x) (__insn_shl(-2LL, ((-x << 3) - 1)))
#define NULMASK(x) (-2LL << (63 - x))
#define CFZ(x) __insn_clz(x)
#define REVCZ(x) __insn_ctz(x)
#endif

/*
 * Create eight copies of the byte in a uint64_t.  Byte Shuffle uses
 * the bytes of srcB as the index into the dest vector to select a
 * byte.  With all indices of zero, the first byte is copied into all
 * the other bytes.
 */
static inline uint64_t copy_byte(uint8_t byte)
{
	return __insn_shufflebytes(byte, 0, 0);
}
