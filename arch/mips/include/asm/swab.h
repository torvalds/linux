/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 99, 2003 by Ralf Baechle
 */
#ifndef _ASM_SWAB_H
#define _ASM_SWAB_H

#include <linux/compiler.h>
#include <linux/types.h>

#define __SWAB_64_THRU_32__

#ifdef CONFIG_CPU_MIPSR2

static inline __attribute_const__ __u16 __arch_swab16(__u16 x)
{
	__asm__(
	"	wsbh	%0, %1			\n"
	: "=r" (x)
	: "r" (x));

	return x;
}
#define __arch_swab16 __arch_swab16

static inline __attribute_const__ __u32 __arch_swab32(__u32 x)
{
	__asm__(
	"	wsbh	%0, %1			\n"
	"	rotr	%0, %0, 16		\n"
	: "=r" (x)
	: "r" (x));

	return x;
}
#define __arch_swab32 __arch_swab32

/*
 * Having already checked for CONFIG_CPU_MIPSR2, enable the
 * optimized version for 64-bit kernel on r2 CPUs.
 */
#ifdef CONFIG_64BIT
static inline __attribute_const__ __u64 __arch_swab64(__u64 x)
{
	__asm__(
	"	dsbh	%0, %1\n"
	"	dshd	%0, %0"
	: "=r" (x)
	: "r" (x));

	return x;
}
#define __arch_swab64 __arch_swab64
#endif /* CONFIG_64BIT */
#endif /* CONFIG_CPU_MIPSR2 */
#endif /* _ASM_SWAB_H */
