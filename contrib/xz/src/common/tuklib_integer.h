///////////////////////////////////////////////////////////////////////////////
//
/// \file       tuklib_integer.h
/// \brief      Various integer and bit operations
///
/// This file provides macros or functions to do some basic integer and bit
/// operations.
///
/// Endianness related integer operations (XX = 16, 32, or 64; Y = b or l):
///   - Byte swapping: bswapXX(num)
///   - Byte order conversions to/from native: convXXYe(num)
///   - Aligned reads: readXXYe(ptr)
///   - Aligned writes: writeXXYe(ptr, num)
///   - Unaligned reads (16/32-bit only): unaligned_readXXYe(ptr)
///   - Unaligned writes (16/32-bit only): unaligned_writeXXYe(ptr, num)
///
/// Since they can macros, the arguments should have no side effects since
/// they may be evaluated more than once.
///
/// \todo       PowerPC and possibly some other architectures support
///             byte swapping load and store instructions. This file
///             doesn't take advantage of those instructions.
///
/// Bit scan operations for non-zero 32-bit integers:
///   - Bit scan reverse (find highest non-zero bit): bsr32(num)
///   - Count leading zeros: clz32(num)
///   - Count trailing zeros: ctz32(num)
///   - Bit scan forward (simply an alias for ctz32()): bsf32(num)
///
/// The above bit scan operations return 0-31. If num is zero,
/// the result is undefined.
//
//  Authors:    Lasse Collin
//              Joachim Henke
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#ifndef TUKLIB_INTEGER_H
#define TUKLIB_INTEGER_H

#include "tuklib_common.h"


////////////////////////////////////////
// Operating system specific features //
////////////////////////////////////////

#if defined(HAVE_BYTESWAP_H)
	// glibc, uClibc, dietlibc
#	include <byteswap.h>
#	ifdef HAVE_BSWAP_16
#		define bswap16(num) bswap_16(num)
#	endif
#	ifdef HAVE_BSWAP_32
#		define bswap32(num) bswap_32(num)
#	endif
#	ifdef HAVE_BSWAP_64
#		define bswap64(num) bswap_64(num)
#	endif

#elif defined(HAVE_SYS_ENDIAN_H)
	// *BSDs and Darwin
#	include <sys/endian.h>

#elif defined(HAVE_SYS_BYTEORDER_H)
	// Solaris
#	include <sys/byteorder.h>
#	ifdef BSWAP_16
#		define bswap16(num) BSWAP_16(num)
#	endif
#	ifdef BSWAP_32
#		define bswap32(num) BSWAP_32(num)
#	endif
#	ifdef BSWAP_64
#		define bswap64(num) BSWAP_64(num)
#	endif
#	ifdef BE_16
#		define conv16be(num) BE_16(num)
#	endif
#	ifdef BE_32
#		define conv32be(num) BE_32(num)
#	endif
#	ifdef BE_64
#		define conv64be(num) BE_64(num)
#	endif
#	ifdef LE_16
#		define conv16le(num) LE_16(num)
#	endif
#	ifdef LE_32
#		define conv32le(num) LE_32(num)
#	endif
#	ifdef LE_64
#		define conv64le(num) LE_64(num)
#	endif
#endif


////////////////////////////////
// Compiler-specific features //
////////////////////////////////

// Newer Intel C compilers require immintrin.h for _bit_scan_reverse()
// and such functions.
#if defined(__INTEL_COMPILER) && (__INTEL_COMPILER >= 1500)
#	include <immintrin.h>
#endif


///////////////////
// Byte swapping //
///////////////////

#ifndef bswap16
#	define bswap16(num) \
		(((uint16_t)(num) << 8) | ((uint16_t)(num) >> 8))
#endif

#ifndef bswap32
#	define bswap32(num) \
		( (((uint32_t)(num) << 24)                       ) \
		| (((uint32_t)(num) <<  8) & UINT32_C(0x00FF0000)) \
		| (((uint32_t)(num) >>  8) & UINT32_C(0x0000FF00)) \
		| (((uint32_t)(num) >> 24)                       ) )
#endif

#ifndef bswap64
#	define bswap64(num) \
		( (((uint64_t)(num) << 56)                               ) \
		| (((uint64_t)(num) << 40) & UINT64_C(0x00FF000000000000)) \
		| (((uint64_t)(num) << 24) & UINT64_C(0x0000FF0000000000)) \
		| (((uint64_t)(num) <<  8) & UINT64_C(0x000000FF00000000)) \
		| (((uint64_t)(num) >>  8) & UINT64_C(0x00000000FF000000)) \
		| (((uint64_t)(num) >> 24) & UINT64_C(0x0000000000FF0000)) \
		| (((uint64_t)(num) >> 40) & UINT64_C(0x000000000000FF00)) \
		| (((uint64_t)(num) >> 56)                               ) )
#endif

// Define conversion macros using the basic byte swapping macros.
#ifdef WORDS_BIGENDIAN
#	ifndef conv16be
#		define conv16be(num) ((uint16_t)(num))
#	endif
#	ifndef conv32be
#		define conv32be(num) ((uint32_t)(num))
#	endif
#	ifndef conv64be
#		define conv64be(num) ((uint64_t)(num))
#	endif
#	ifndef conv16le
#		define conv16le(num) bswap16(num)
#	endif
#	ifndef conv32le
#		define conv32le(num) bswap32(num)
#	endif
#	ifndef conv64le
#		define conv64le(num) bswap64(num)
#	endif
#else
#	ifndef conv16be
#		define conv16be(num) bswap16(num)
#	endif
#	ifndef conv32be
#		define conv32be(num) bswap32(num)
#	endif
#	ifndef conv64be
#		define conv64be(num) bswap64(num)
#	endif
#	ifndef conv16le
#		define conv16le(num) ((uint16_t)(num))
#	endif
#	ifndef conv32le
#		define conv32le(num) ((uint32_t)(num))
#	endif
#	ifndef conv64le
#		define conv64le(num) ((uint64_t)(num))
#	endif
#endif


//////////////////////////////
// Aligned reads and writes //
//////////////////////////////

static inline uint16_t
read16be(const uint8_t *buf)
{
	uint16_t num = *(const uint16_t *)buf;
	return conv16be(num);
}


static inline uint16_t
read16le(const uint8_t *buf)
{
	uint16_t num = *(const uint16_t *)buf;
	return conv16le(num);
}


static inline uint32_t
read32be(const uint8_t *buf)
{
	uint32_t num = *(const uint32_t *)buf;
	return conv32be(num);
}


static inline uint32_t
read32le(const uint8_t *buf)
{
	uint32_t num = *(const uint32_t *)buf;
	return conv32le(num);
}


static inline uint64_t
read64be(const uint8_t *buf)
{
	uint64_t num = *(const uint64_t *)buf;
	return conv64be(num);
}


static inline uint64_t
read64le(const uint8_t *buf)
{
	uint64_t num = *(const uint64_t *)buf;
	return conv64le(num);
}


// NOTE: Possible byte swapping must be done in a macro to allow GCC
// to optimize byte swapping of constants when using glibc's or *BSD's
// byte swapping macros. The actual write is done in an inline function
// to make type checking of the buf pointer possible similarly to readXXYe()
// functions.

#define write16be(buf, num) write16ne((buf), conv16be(num))
#define write16le(buf, num) write16ne((buf), conv16le(num))
#define write32be(buf, num) write32ne((buf), conv32be(num))
#define write32le(buf, num) write32ne((buf), conv32le(num))
#define write64be(buf, num) write64ne((buf), conv64be(num))
#define write64le(buf, num) write64ne((buf), conv64le(num))


static inline void
write16ne(uint8_t *buf, uint16_t num)
{
	*(uint16_t *)buf = num;
	return;
}


static inline void
write32ne(uint8_t *buf, uint32_t num)
{
	*(uint32_t *)buf = num;
	return;
}


static inline void
write64ne(uint8_t *buf, uint64_t num)
{
	*(uint64_t *)buf = num;
	return;
}


////////////////////////////////
// Unaligned reads and writes //
////////////////////////////////

// NOTE: TUKLIB_FAST_UNALIGNED_ACCESS indicates only support for 16-bit and
// 32-bit unaligned integer loads and stores. It's possible that 64-bit
// unaligned access doesn't work or is slower than byte-by-byte access.
// Since unaligned 64-bit is probably not needed as often as 16-bit or
// 32-bit, we simply don't support 64-bit unaligned access for now.
#ifdef TUKLIB_FAST_UNALIGNED_ACCESS
#	define unaligned_read16be read16be
#	define unaligned_read16le read16le
#	define unaligned_read32be read32be
#	define unaligned_read32le read32le
#	define unaligned_write16be write16be
#	define unaligned_write16le write16le
#	define unaligned_write32be write32be
#	define unaligned_write32le write32le

#else

static inline uint16_t
unaligned_read16be(const uint8_t *buf)
{
	uint16_t num = ((uint16_t)buf[0] << 8) | (uint16_t)buf[1];
	return num;
}


static inline uint16_t
unaligned_read16le(const uint8_t *buf)
{
	uint16_t num = ((uint16_t)buf[0]) | ((uint16_t)buf[1] << 8);
	return num;
}


static inline uint32_t
unaligned_read32be(const uint8_t *buf)
{
	uint32_t num = (uint32_t)buf[0] << 24;
	num |= (uint32_t)buf[1] << 16;
	num |= (uint32_t)buf[2] << 8;
	num |= (uint32_t)buf[3];
	return num;
}


static inline uint32_t
unaligned_read32le(const uint8_t *buf)
{
	uint32_t num = (uint32_t)buf[0];
	num |= (uint32_t)buf[1] << 8;
	num |= (uint32_t)buf[2] << 16;
	num |= (uint32_t)buf[3] << 24;
	return num;
}


static inline void
unaligned_write16be(uint8_t *buf, uint16_t num)
{
	buf[0] = (uint8_t)(num >> 8);
	buf[1] = (uint8_t)num;
	return;
}


static inline void
unaligned_write16le(uint8_t *buf, uint16_t num)
{
	buf[0] = (uint8_t)num;
	buf[1] = (uint8_t)(num >> 8);
	return;
}


static inline void
unaligned_write32be(uint8_t *buf, uint32_t num)
{
	buf[0] = (uint8_t)(num >> 24);
	buf[1] = (uint8_t)(num >> 16);
	buf[2] = (uint8_t)(num >> 8);
	buf[3] = (uint8_t)num;
	return;
}


static inline void
unaligned_write32le(uint8_t *buf, uint32_t num)
{
	buf[0] = (uint8_t)num;
	buf[1] = (uint8_t)(num >> 8);
	buf[2] = (uint8_t)(num >> 16);
	buf[3] = (uint8_t)(num >> 24);
	return;
}

#endif


static inline uint32_t
bsr32(uint32_t n)
{
	// Check for ICC first, since it tends to define __GNUC__ too.
#if defined(__INTEL_COMPILER)
	return _bit_scan_reverse(n);

#elif TUKLIB_GNUC_REQ(3, 4) && UINT_MAX == UINT32_MAX
	// GCC >= 3.4 has __builtin_clz(), which gives good results on
	// multiple architectures. On x86, __builtin_clz() ^ 31U becomes
	// either plain BSR (so the XOR gets optimized away) or LZCNT and
	// XOR (if -march indicates that SSE4a instructions are supported).
	return __builtin_clz(n) ^ 31U;

#elif defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
	uint32_t i;
	__asm__("bsrl %1, %0" : "=r" (i) : "rm" (n));
	return i;

#elif defined(_MSC_VER) && _MSC_VER >= 1400
	// MSVC isn't supported by tuklib, but since this code exists,
	// it doesn't hurt to have it here anyway.
	uint32_t i;
	_BitScanReverse((DWORD *)&i, n);
	return i;

#else
	uint32_t i = 31;

	if ((n & UINT32_C(0xFFFF0000)) == 0) {
		n <<= 16;
		i = 15;
	}

	if ((n & UINT32_C(0xFF000000)) == 0) {
		n <<= 8;
		i -= 8;
	}

	if ((n & UINT32_C(0xF0000000)) == 0) {
		n <<= 4;
		i -= 4;
	}

	if ((n & UINT32_C(0xC0000000)) == 0) {
		n <<= 2;
		i -= 2;
	}

	if ((n & UINT32_C(0x80000000)) == 0)
		--i;

	return i;
#endif
}


static inline uint32_t
clz32(uint32_t n)
{
#if defined(__INTEL_COMPILER)
	return _bit_scan_reverse(n) ^ 31U;

#elif TUKLIB_GNUC_REQ(3, 4) && UINT_MAX == UINT32_MAX
	return __builtin_clz(n);

#elif defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
	uint32_t i;
	__asm__("bsrl %1, %0\n\t"
		"xorl $31, %0"
		: "=r" (i) : "rm" (n));
	return i;

#elif defined(_MSC_VER) && _MSC_VER >= 1400
	uint32_t i;
	_BitScanReverse((DWORD *)&i, n);
	return i ^ 31U;

#else
	uint32_t i = 0;

	if ((n & UINT32_C(0xFFFF0000)) == 0) {
		n <<= 16;
		i = 16;
	}

	if ((n & UINT32_C(0xFF000000)) == 0) {
		n <<= 8;
		i += 8;
	}

	if ((n & UINT32_C(0xF0000000)) == 0) {
		n <<= 4;
		i += 4;
	}

	if ((n & UINT32_C(0xC0000000)) == 0) {
		n <<= 2;
		i += 2;
	}

	if ((n & UINT32_C(0x80000000)) == 0)
		++i;

	return i;
#endif
}


static inline uint32_t
ctz32(uint32_t n)
{
#if defined(__INTEL_COMPILER)
	return _bit_scan_forward(n);

#elif TUKLIB_GNUC_REQ(3, 4) && UINT_MAX >= UINT32_MAX
	return __builtin_ctz(n);

#elif defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
	uint32_t i;
	__asm__("bsfl %1, %0" : "=r" (i) : "rm" (n));
	return i;

#elif defined(_MSC_VER) && _MSC_VER >= 1400
	uint32_t i;
	_BitScanForward((DWORD *)&i, n);
	return i;

#else
	uint32_t i = 0;

	if ((n & UINT32_C(0x0000FFFF)) == 0) {
		n >>= 16;
		i = 16;
	}

	if ((n & UINT32_C(0x000000FF)) == 0) {
		n >>= 8;
		i += 8;
	}

	if ((n & UINT32_C(0x0000000F)) == 0) {
		n >>= 4;
		i += 4;
	}

	if ((n & UINT32_C(0x00000003)) == 0) {
		n >>= 2;
		i += 2;
	}

	if ((n & UINT32_C(0x00000001)) == 0)
		++i;

	return i;
#endif
}

#define bsf32 ctz32

#endif
