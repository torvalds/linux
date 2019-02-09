/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *  Copyright (C) 2011 Texas Instruments Incorporated
 *  Author: Mark Salter <msalter@redhat.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */
#ifndef _ASM_C6X_SWAB_H
#define _ASM_C6X_SWAB_H

static inline __attribute_const__ __u16 __c6x_swab16(__u16 val)
{
	asm("swap4 .l1 %0,%0\n" : "+a"(val));
	return val;
}

static inline __attribute_const__ __u32 __c6x_swab32(__u32 val)
{
	asm("swap4 .l1 %0,%0\n"
	    "swap2 .l1 %0,%0\n"
	    : "+a"(val));
	return val;
}

static inline __attribute_const__ __u64 __c6x_swab64(__u64 val)
{
	asm("   swap2 .s1 %p0,%P0\n"
	    "|| swap2 .l1 %P0,%p0\n"
	    "   swap4 .l1 %p0,%p0\n"
	    "   swap4 .l1 %P0,%P0\n"
	    : "+a"(val));
	return val;
}

static inline __attribute_const__ __u32 __c6x_swahw32(__u32 val)
{
	asm("swap2 .l1 %0,%0\n" : "+a"(val));
	return val;
}

static inline __attribute_const__ __u32 __c6x_swahb32(__u32 val)
{
	asm("swap4 .l1 %0,%0\n" : "+a"(val));
	return val;
}

#define __arch_swab16 __c6x_swab16
#define __arch_swab32 __c6x_swab32
#define __arch_swab64 __c6x_swab64
#define __arch_swahw32 __c6x_swahw32
#define __arch_swahb32 __c6x_swahb32

#endif /* _ASM_C6X_SWAB_H */
