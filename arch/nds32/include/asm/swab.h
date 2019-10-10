/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (C) 2005-2017 Andes Technology Corporation

#ifndef __NDS32_SWAB_H__
#define __NDS32_SWAB_H__

#include <linux/types.h>
#include <linux/compiler.h>

static __inline__ __attribute_const__ __u32 ___arch__swab32(__u32 x)
{
	__asm__("wsbh   %0, %0\n\t"	/* word swap byte within halfword */
		"rotri  %0, %0, #16\n"
		:"=r"(x)
		:"0"(x));
	return x;
}

static __inline__ __attribute_const__ __u16 ___arch__swab16(__u16 x)
{
	__asm__("wsbh   %0, %0\n"	/* word swap byte within halfword */
		:"=r"(x)
		:"0"(x));
	return x;
}

#define __arch_swab32(x) ___arch__swab32(x)
#define __arch_swab16(x) ___arch__swab16(x)

#if !defined(__STRICT_ANSI__) || defined(__KERNEL__)
#define __BYTEORDER_HAS_U64__
#define __SWAB_64_THRU_32__
#endif

#endif /* __NDS32_SWAB_H__ */
