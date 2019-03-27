///////////////////////////////////////////////////////////////////////////////
//
/// \file       memcmplen.h
/// \brief      Optimized comparison of two buffers
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#ifndef LZMA_MEMCMPLEN_H
#define LZMA_MEMCMPLEN_H

#include "common.h"

#ifdef HAVE_IMMINTRIN_H
#	include <immintrin.h>
#endif


/// Find out how many equal bytes the two buffers have.
///
/// \param      buf1    First buffer
/// \param      buf2    Second buffer
/// \param      len     How many bytes have already been compared and will
///                     be assumed to match
/// \param      limit   How many bytes to compare at most, including the
///                     already-compared bytes. This must be significantly
///                     smaller than UINT32_MAX to avoid integer overflows.
///                     Up to LZMA_MEMCMPLEN_EXTRA bytes may be read past
///                     the specified limit from both buf1 and buf2.
///
/// \return     Number of equal bytes in the buffers is returned.
///             This is always at least len and at most limit.
///
/// \note       LZMA_MEMCMPLEN_EXTRA defines how many extra bytes may be read.
///             It's rounded up to 2^n. This extra amount needs to be
///             allocated in the buffers being used. It needs to be
///             initialized too to keep Valgrind quiet.
static inline uint32_t lzma_attribute((__always_inline__))
lzma_memcmplen(const uint8_t *buf1, const uint8_t *buf2,
		uint32_t len, uint32_t limit)
{
	assert(len <= limit);
	assert(limit <= UINT32_MAX / 2);

#if defined(TUKLIB_FAST_UNALIGNED_ACCESS) \
		&& ((TUKLIB_GNUC_REQ(3, 4) && defined(__x86_64__)) \
			|| (defined(__INTEL_COMPILER) && defined(__x86_64__)) \
			|| (defined(__INTEL_COMPILER) && defined(_M_X64)) \
			|| (defined(_MSC_VER) && defined(_M_X64)))
	// NOTE: This will use 64-bit unaligned access which
	// TUKLIB_FAST_UNALIGNED_ACCESS wasn't meant to permit, but
	// it's convenient here at least as long as it's x86-64 only.
	//
	// I keep this x86-64 only for now since that's where I know this
	// to be a good method. This may be fine on other 64-bit CPUs too.
	// On big endian one should use xor instead of subtraction and switch
	// to __builtin_clzll().
#define LZMA_MEMCMPLEN_EXTRA 8
	while (len < limit) {
		const uint64_t x = *(const uint64_t *)(buf1 + len)
				- *(const uint64_t *)(buf2 + len);
		if (x != 0) {
#	if defined(_M_X64) // MSVC or Intel C compiler on Windows
			unsigned long tmp;
			_BitScanForward64(&tmp, x);
			len += (uint32_t)tmp >> 3;
#	else // GCC, clang, or Intel C compiler
			len += (uint32_t)__builtin_ctzll(x) >> 3;
#	endif
			return my_min(len, limit);
		}

		len += 8;
	}

	return limit;

#elif defined(TUKLIB_FAST_UNALIGNED_ACCESS) \
		&& defined(HAVE__MM_MOVEMASK_EPI8) \
		&& ((defined(__GNUC__) && defined(__SSE2_MATH__)) \
			|| (defined(__INTEL_COMPILER) && defined(__SSE2__)) \
			|| (defined(_MSC_VER) && defined(_M_IX86_FP) \
				&& _M_IX86_FP >= 2))
	// NOTE: Like above, this will use 128-bit unaligned access which
	// TUKLIB_FAST_UNALIGNED_ACCESS wasn't meant to permit.
	//
	// SSE2 version for 32-bit and 64-bit x86. On x86-64 the above
	// version is sometimes significantly faster and sometimes
	// slightly slower than this SSE2 version, so this SSE2
	// version isn't used on x86-64.
#	define LZMA_MEMCMPLEN_EXTRA 16
	while (len < limit) {
		const uint32_t x = 0xFFFF ^ _mm_movemask_epi8(_mm_cmpeq_epi8(
			_mm_loadu_si128((const __m128i *)(buf1 + len)),
			_mm_loadu_si128((const __m128i *)(buf2 + len))));

		if (x != 0) {
#	if defined(__INTEL_COMPILER)
			len += _bit_scan_forward(x);
#	elif defined(_MSC_VER)
			unsigned long tmp;
			_BitScanForward(&tmp, x);
			len += tmp;
#	else
			len += __builtin_ctz(x);
#	endif
			return my_min(len, limit);
		}

		len += 16;
	}

	return limit;

#elif defined(TUKLIB_FAST_UNALIGNED_ACCESS) && !defined(WORDS_BIGENDIAN)
	// Generic 32-bit little endian method
#	define LZMA_MEMCMPLEN_EXTRA 4
	while (len < limit) {
		uint32_t x = *(const uint32_t *)(buf1 + len)
				- *(const uint32_t *)(buf2 + len);
		if (x != 0) {
			if ((x & 0xFFFF) == 0) {
				len += 2;
				x >>= 16;
			}

			if ((x & 0xFF) == 0)
				++len;

			return my_min(len, limit);
		}

		len += 4;
	}

	return limit;

#elif defined(TUKLIB_FAST_UNALIGNED_ACCESS) && defined(WORDS_BIGENDIAN)
	// Generic 32-bit big endian method
#	define LZMA_MEMCMPLEN_EXTRA 4
	while (len < limit) {
		uint32_t x = *(const uint32_t *)(buf1 + len)
				^ *(const uint32_t *)(buf2 + len);
		if (x != 0) {
			if ((x & 0xFFFF0000) == 0) {
				len += 2;
				x <<= 16;
			}

			if ((x & 0xFF000000) == 0)
				++len;

			return my_min(len, limit);
		}

		len += 4;
	}

	return limit;

#else
	// Simple portable version that doesn't use unaligned access.
#	define LZMA_MEMCMPLEN_EXTRA 0
	while (len < limit && buf1[len] == buf2[len])
		++len;

	return len;
#endif
}

#endif
