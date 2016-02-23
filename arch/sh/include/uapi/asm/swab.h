#ifndef __ASM_SH_SWAB_H
#define __ASM_SH_SWAB_H

/*
 * Copyright (C) 1999  Niibe Yutaka
 * Copyright (C) 2000, 2001  Paolo Alberelli
 */
#include <linux/compiler.h>
#include <linux/types.h>
#include <asm-generic/swab.h>

static inline __attribute_const__ __u32 __arch_swab32(__u32 x)
{
	__asm__(
#ifdef __SH5__
		"byterev	%1, %0\n\t"
		"shari		%0, 32, %0"
#else
		"swap.b		%1, %0\n\t"
		"swap.w		%0, %0\n\t"
		"swap.b		%0, %0"
#endif
		: "=r" (x)
		: "r" (x));

	return x;
}
#define __arch_swab32 __arch_swab32

static inline __attribute_const__ __u16 __arch_swab16(__u16 x)
{
	__asm__(
#ifdef __SH5__
		"byterev	%1, %0\n\t"
		"shari		%0, 32, %0"
#else
		"swap.b		%1, %0"
#endif
		: "=r" (x)
		:  "r" (x));

	return x;
}
#define __arch_swab16 __arch_swab16

static inline __u64 __arch_swab64(__u64 val)
{
	union {
		struct { __u32 a,b; } s;
		__u64 u;
	} v, w;
	v.u = val;
	w.s.b = __arch_swab32(v.s.a);
	w.s.a = __arch_swab32(v.s.b);
	return w.u;
}
#define __arch_swab64 __arch_swab64

#endif /* __ASM_SH_SWAB_H */
