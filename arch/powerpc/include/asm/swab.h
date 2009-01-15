#ifndef _ASM_POWERPC_SWAB_H
#define _ASM_POWERPC_SWAB_H

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <asm/types.h>
#include <linux/compiler.h>

#ifdef __GNUC__

#ifndef __powerpc64__
#define __SWAB_64_THRU_32__
#endif /* __powerpc64__ */

#ifdef __KERNEL__

static __inline__ __u16 ld_le16(const volatile __u16 *addr)
{
	__u16 val;

	__asm__ __volatile__ ("lhbrx %0,0,%1" : "=r" (val) : "r" (addr), "m" (*addr));
	return val;
}
#define __arch_swab16p ld_le16

static __inline__ void st_le16(volatile __u16 *addr, const __u16 val)
{
	__asm__ __volatile__ ("sthbrx %1,0,%2" : "=m" (*addr) : "r" (val), "r" (addr));
}

static inline void __arch_swab16s(__u16 *addr)
{
	st_le16(addr, *addr);
}
#define __arch_swab16s __arch_swab16s

static __inline__ __u32 ld_le32(const volatile __u32 *addr)
{
	__u32 val;

	__asm__ __volatile__ ("lwbrx %0,0,%1" : "=r" (val) : "r" (addr), "m" (*addr));
	return val;
}
#define __arch_swab32p ld_le32

static __inline__ void st_le32(volatile __u32 *addr, const __u32 val)
{
	__asm__ __volatile__ ("stwbrx %1,0,%2" : "=m" (*addr) : "r" (val), "r" (addr));
}

static inline void __arch_swab32s(__u32 *addr)
{
	st_le32(addr, *addr);
}
#define __arch_swab32s __arch_swab32s

static inline __attribute_const__ __u16 __arch_swab16(__u16 value)
{
	__u16 result;

	__asm__("rlwimi %0,%1,8,16,23"
	    : "=r" (result)
	    : "r" (value), "0" (value >> 8));
	return result;
}
#define __arch_swab16 __arch_swab16

static inline __attribute_const__ __u32 __arch_swab32(__u32 value)
{
	__u32 result;

	__asm__("rlwimi %0,%1,24,16,23\n\t"
	    "rlwimi %0,%1,8,8,15\n\t"
	    "rlwimi %0,%1,24,0,7"
	    : "=r" (result)
	    : "r" (value), "0" (value >> 24));
	return result;
}
#define __arch_swab32 __arch_swab32

#endif /* __KERNEL__ */

#endif /* __GNUC__ */

#endif /* _ASM_POWERPC_SWAB_H */
