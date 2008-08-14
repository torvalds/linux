#ifndef _ASM_POWERPC_BYTEORDER_H
#define _ASM_POWERPC_BYTEORDER_H

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <asm/types.h>
#include <linux/compiler.h>

#ifdef __GNUC__
#ifdef __KERNEL__

static __inline__ __u16 ld_le16(const volatile __u16 *addr)
{
	__u16 val;

	__asm__ __volatile__ ("lhbrx %0,0,%1" : "=r" (val) : "r" (addr), "m" (*addr));
	return val;
}

static __inline__ void st_le16(volatile __u16 *addr, const __u16 val)
{
	__asm__ __volatile__ ("sthbrx %1,0,%2" : "=m" (*addr) : "r" (val), "r" (addr));
}

static __inline__ __u32 ld_le32(const volatile __u32 *addr)
{
	__u32 val;

	__asm__ __volatile__ ("lwbrx %0,0,%1" : "=r" (val) : "r" (addr), "m" (*addr));
	return val;
}

static __inline__ void st_le32(volatile __u32 *addr, const __u32 val)
{
	__asm__ __volatile__ ("stwbrx %1,0,%2" : "=m" (*addr) : "r" (val), "r" (addr));
}

static __inline__ __attribute_const__ __u16 ___arch__swab16(__u16 value)
{
	__u16 result;

	__asm__("rlwimi %0,%1,8,16,23"
	    : "=r" (result)
	    : "r" (value), "0" (value >> 8));
	return result;
}

static __inline__ __attribute_const__ __u32 ___arch__swab32(__u32 value)
{
	__u32 result;

	__asm__("rlwimi %0,%1,24,16,23\n\t"
	    "rlwimi %0,%1,8,8,15\n\t"
	    "rlwimi %0,%1,24,0,7"
	    : "=r" (result)
	    : "r" (value), "0" (value >> 24));
	return result;
}

#define __arch__swab16(x) ___arch__swab16(x)
#define __arch__swab32(x) ___arch__swab32(x)

/* The same, but returns converted value from the location pointer by addr. */
#define __arch__swab16p(addr) ld_le16(addr)
#define __arch__swab32p(addr) ld_le32(addr)

/* The same, but do the conversion in situ, ie. put the value back to addr. */
#define __arch__swab16s(addr) st_le16(addr,*addr)
#define __arch__swab32s(addr) st_le32(addr,*addr)

#endif /* __KERNEL__ */

#ifndef __STRICT_ANSI__
#define __BYTEORDER_HAS_U64__
#ifndef __powerpc64__
#define __SWAB_64_THRU_32__
#endif /* __powerpc64__ */
#endif /* __STRICT_ANSI__ */

#endif /* __GNUC__ */

#include <linux/byteorder/big_endian.h>

#endif /* _ASM_POWERPC_BYTEORDER_H */
