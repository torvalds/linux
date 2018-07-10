/*
 * Private includes and definitions for userspace use of XZ Embedded
 *
 * Author: Lasse Collin <lasse.collin@tukaani.org>
 *
 * This file has been put into the public domain.
 * You can do whatever you want with this file.
 */

#ifndef XZ_CONFIG_H
#define XZ_CONFIG_H

/* Uncomment as needed to enable BCJ filter decoders. */
/* #define XZ_DEC_X86 */
/* #define XZ_DEC_POWERPC */
/* #define XZ_DEC_IA64 */
/* #define XZ_DEC_ARM */
/* #define XZ_DEC_ARMTHUMB */
/* #define XZ_DEC_SPARC */

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "xz.h"

#define kmalloc(size, flags) malloc(size)
#define kfree(ptr) free(ptr)
#define vmalloc(size) malloc(size)
#define vfree(ptr) free(ptr)

#define memeq(a, b, size) (memcmp(a, b, size) == 0)
#define memzero(buf, size) memset(buf, 0, size)

#undef min
#undef min_t
#define min(x, y) ((x) < (y) ? (x) : (y))
#define min_t(type, x, y) min(x, y)

/*
 * Some functions have been marked with __always_inline to keep the
 * performance reasonable even when the compiler is optimizing for
 * small code size. You may be able to save a few bytes by #defining
 * __always_inline to plain inline, but don't complain if the code
 * becomes slow.
 *
 * NOTE: System headers on GNU/Linux may #define this macro already,
 * so if you want to change it, you need to #undef it first.
 */
#ifndef __always_inline
#	ifdef __GNUC__
#		define __always_inline \
			inline __attribute__((__always_inline__))
#	else
#		define __always_inline inline
#	endif
#endif

/*
 * Some functions are marked to never be inlined to reduce stack usage.
 * If you don't care about stack usage, you may want to modify this so
 * that noinline_for_stack is #defined to be empty even when using GCC.
 * Doing so may save a few bytes in binary size.
 */
#ifndef noinline_for_stack
#	ifdef __GNUC__
#		define noinline_for_stack __attribute__((__noinline__))
#	else
#		define noinline_for_stack
#	endif
#endif

/* Inline functions to access unaligned unsigned 32-bit integers */
#ifndef get_unaligned_le32
static inline uint32_t XZ_FUNC get_unaligned_le32(const uint8_t *buf)
{
	return (uint32_t)buf[0]
			| ((uint32_t)buf[1] << 8)
			| ((uint32_t)buf[2] << 16)
			| ((uint32_t)buf[3] << 24);
}
#endif

#ifndef get_unaligned_be32
static inline uint32_t XZ_FUNC get_unaligned_be32(const uint8_t *buf)
{
	return (uint32_t)(buf[0] << 24)
			| ((uint32_t)buf[1] << 16)
			| ((uint32_t)buf[2] << 8)
			| (uint32_t)buf[3];
}
#endif

#ifndef put_unaligned_le32
static inline void XZ_FUNC put_unaligned_le32(uint32_t val, uint8_t *buf)
{
	buf[0] = (uint8_t)val;
	buf[1] = (uint8_t)(val >> 8);
	buf[2] = (uint8_t)(val >> 16);
	buf[3] = (uint8_t)(val >> 24);
}
#endif

#ifndef put_unaligned_be32
static inline void XZ_FUNC put_unaligned_be32(uint32_t val, uint8_t *buf)
{
	buf[0] = (uint8_t)(val >> 24);
	buf[1] = (uint8_t)(val >> 16);
	buf[2] = (uint8_t)(val >> 8);
	buf[3] = (uint8_t)val;
}
#endif

/*
 * Use get_unaligned_le32() also for aligned access for simplicity. On
 * little endian systems, #define get_le32(ptr) (*(const uint32_t *)(ptr))
 * could save a few bytes in code size.
 */
#ifndef get_le32
#	define get_le32 get_unaligned_le32
#endif

#endif
