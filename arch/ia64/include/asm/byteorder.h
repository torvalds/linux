#ifndef _ASM_IA64_BYTEORDER_H
#define _ASM_IA64_BYTEORDER_H

/*
 * Modified 1998, 1999
 *	David Mosberger-Tang <davidm@hpl.hp.com>, Hewlett-Packard Co.
 */

#include <asm/types.h>
#include <asm/intrinsics.h>
#include <linux/compiler.h>

static __inline__ __attribute_const__ __u64
__ia64_swab64 (__u64 x)
{
	__u64 result;

	result = ia64_mux1(x, ia64_mux1_rev);
	return result;
}

static __inline__ __attribute_const__ __u32
__ia64_swab32 (__u32 x)
{
	return __ia64_swab64(x) >> 32;
}

static __inline__ __attribute_const__ __u16
__ia64_swab16(__u16 x)
{
	return __ia64_swab64(x) >> 48;
}

#define __arch__swab64(x) __ia64_swab64(x)
#define __arch__swab32(x) __ia64_swab32(x)
#define __arch__swab16(x) __ia64_swab16(x)

#define __BYTEORDER_HAS_U64__

#include <linux/byteorder/little_endian.h>

#endif /* _ASM_IA64_BYTEORDER_H */
