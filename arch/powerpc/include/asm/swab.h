/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#ifndef _ASM_POWERPC_SWAB_H
#define _ASM_POWERPC_SWAB_H

#include <uapi/asm/swab.h>

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

#endif /* _ASM_POWERPC_SWAB_H */
