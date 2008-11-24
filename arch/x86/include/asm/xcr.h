/* -*- linux-c -*- ------------------------------------------------------- *
 *
 *   Copyright 2008 rPath, Inc. - All Rights Reserved
 *
 *   This file is part of the Linux kernel, and is made available under
 *   the terms of the GNU General Public License version 2 or (at your
 *   option) any later version; incorporated herein by reference.
 *
 * ----------------------------------------------------------------------- */

/*
 * asm-x86/xcr.h
 *
 * Definitions for the eXtended Control Register instructions
 */

#ifndef _ASM_X86_XCR_H
#define _ASM_X86_XCR_H

#define XCR_XFEATURE_ENABLED_MASK	0x00000000

#ifdef __KERNEL__
# ifndef __ASSEMBLY__

#include <linux/types.h>

static inline u64 xgetbv(u32 index)
{
	u32 eax, edx;

	asm volatile(".byte 0x0f,0x01,0xd0" /* xgetbv */
		     : "=a" (eax), "=d" (edx)
		     : "c" (index));
	return eax + ((u64)edx << 32);
}

static inline void xsetbv(u32 index, u64 value)
{
	u32 eax = value;
	u32 edx = value >> 32;

	asm volatile(".byte 0x0f,0x01,0xd1" /* xsetbv */
		     : : "a" (eax), "d" (edx), "c" (index));
}

# endif /* __ASSEMBLY__ */
#endif /* __KERNEL__ */

#endif /* _ASM_X86_XCR_H */
