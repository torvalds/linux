///////////////////////////////////////////////////////////////////////////////
//
/// \file       lz_encoder_hash.h
/// \brief      Hash macros for match finders
//
//  Author:     Igor Pavlov
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#ifndef LZMA_LZ_ENCODER_HASH_H
#define LZMA_LZ_ENCODER_HASH_H

#if defined(WORDS_BIGENDIAN) && !defined(HAVE_SMALL)
	// This is to make liblzma produce the same output on big endian
	// systems that it does on little endian systems. lz_encoder.c
	// takes care of including the actual table.
	extern const uint32_t lzma_lz_hash_table[256];
#	define hash_table lzma_lz_hash_table
#else
#	include "check.h"
#	define hash_table lzma_crc32_table[0]
#endif

#define HASH_2_SIZE (UINT32_C(1) << 10)
#define HASH_3_SIZE (UINT32_C(1) << 16)
#define HASH_4_SIZE (UINT32_C(1) << 20)

#define HASH_2_MASK (HASH_2_SIZE - 1)
#define HASH_3_MASK (HASH_3_SIZE - 1)
#define HASH_4_MASK (HASH_4_SIZE - 1)

#define FIX_3_HASH_SIZE (HASH_2_SIZE)
#define FIX_4_HASH_SIZE (HASH_2_SIZE + HASH_3_SIZE)
#define FIX_5_HASH_SIZE (HASH_2_SIZE + HASH_3_SIZE + HASH_4_SIZE)

// Endianness doesn't matter in hash_2_calc() (no effect on the output).
#ifdef TUKLIB_FAST_UNALIGNED_ACCESS
#	define hash_2_calc() \
		const uint32_t hash_value = *(const uint16_t *)(cur)
#else
#	define hash_2_calc() \
		const uint32_t hash_value \
			= (uint32_t)(cur[0]) | ((uint32_t)(cur[1]) << 8)
#endif

#define hash_3_calc() \
	const uint32_t temp = hash_table[cur[0]] ^ cur[1]; \
	const uint32_t hash_2_value = temp & HASH_2_MASK; \
	const uint32_t hash_value \
			= (temp ^ ((uint32_t)(cur[2]) << 8)) & mf->hash_mask

#define hash_4_calc() \
	const uint32_t temp = hash_table[cur[0]] ^ cur[1]; \
	const uint32_t hash_2_value = temp & HASH_2_MASK; \
	const uint32_t hash_3_value \
			= (temp ^ ((uint32_t)(cur[2]) << 8)) & HASH_3_MASK; \
	const uint32_t hash_value = (temp ^ ((uint32_t)(cur[2]) << 8) \
			^ (hash_table[cur[3]] << 5)) & mf->hash_mask


// The following are not currently used.

#define hash_5_calc() \
	const uint32_t temp = hash_table[cur[0]] ^ cur[1]; \
	const uint32_t hash_2_value = temp & HASH_2_MASK; \
	const uint32_t hash_3_value \
			= (temp ^ ((uint32_t)(cur[2]) << 8)) & HASH_3_MASK; \
	uint32_t hash_4_value = (temp ^ ((uint32_t)(cur[2]) << 8) ^ \
			^ hash_table[cur[3]] << 5); \
	const uint32_t hash_value \
			= (hash_4_value ^ (hash_table[cur[4]] << 3)) \
				& mf->hash_mask; \
	hash_4_value &= HASH_4_MASK

/*
#define hash_zip_calc() \
	const uint32_t hash_value \
			= (((uint32_t)(cur[0]) | ((uint32_t)(cur[1]) << 8)) \
				^ hash_table[cur[2]]) & 0xFFFF
*/

#define hash_zip_calc() \
	const uint32_t hash_value \
			= (((uint32_t)(cur[2]) | ((uint32_t)(cur[0]) << 8)) \
				^ hash_table[cur[1]]) & 0xFFFF

#define mt_hash_2_calc() \
	const uint32_t hash_2_value \
			= (hash_table[cur[0]] ^ cur[1]) & HASH_2_MASK

#define mt_hash_3_calc() \
	const uint32_t temp = hash_table[cur[0]] ^ cur[1]; \
	const uint32_t hash_2_value = temp & HASH_2_MASK; \
	const uint32_t hash_3_value \
			= (temp ^ ((uint32_t)(cur[2]) << 8)) & HASH_3_MASK

#define mt_hash_4_calc() \
	const uint32_t temp = hash_table[cur[0]] ^ cur[1]; \
	const uint32_t hash_2_value = temp & HASH_2_MASK; \
	const uint32_t hash_3_value \
			= (temp ^ ((uint32_t)(cur[2]) << 8)) & HASH_3_MASK; \
	const uint32_t hash_4_value = (temp ^ ((uint32_t)(cur[2]) << 8) ^ \
			(hash_table[cur[3]] << 5)) & HASH_4_MASK

#endif
