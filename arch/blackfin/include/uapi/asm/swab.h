/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * Copyright 2009 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef _UAPI_BLACKFIN_SWAB_H
#define _UAPI_BLACKFIN_SWAB_H

#include <linux/types.h>
#include <asm-generic/swab.h>

#ifdef __GNUC__

static __inline__ __attribute_const__ __u32 __arch_swahb32(__u32 xx)
{
	__u32 tmp;
	__asm__("%1 = %0 >> 8 (V);\n\t"
		"%0 = %0 << 8 (V);\n\t"
		"%0 = %0 | %1;\n\t"
		: "+d"(xx), "=&d"(tmp));
	return xx;
}
#define __arch_swahb32 __arch_swahb32

static __inline__ __attribute_const__ __u32 __arch_swahw32(__u32 xx)
{
	__u32 rv;
	__asm__("%0 = PACK(%1.L, %1.H);\n\t": "=d"(rv): "d"(xx));
	return rv;
}
#define __arch_swahw32 __arch_swahw32

static __inline__ __attribute_const__ __u32 __arch_swab32(__u32 xx)
{
	return __arch_swahb32(__arch_swahw32(xx));
}
#define __arch_swab32 __arch_swab32

static __inline__ __attribute_const__ __u16 __arch_swab16(__u16 xx)
{
	__u32 xw = xx;
	__asm__("%0 <<= 8;\n	%0.L = %0.L + %0.H (NS);\n": "+d"(xw));
	return (__u16)xw;
}
#define __arch_swab16 __arch_swab16

#endif /* __GNUC__ */

#endif /* _UAPI_BLACKFIN_SWAB_H */
