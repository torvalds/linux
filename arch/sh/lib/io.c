// SPDX-License-Identifier: GPL-2.0
/*
 * arch/sh/lib/io.c - SH32 optimized I/O routines
 *
 * Copyright (C) 2000  Stuart Menefy
 * Copyright (C) 2005  Paul Mundt
 *
 * Provide real functions which expand to whatever the header file defined.
 * Also definitions of machine independent IO functions.
 */
#include <linux/module.h>
#include <linux/io.h>

void __raw_readsl(const volatile void __iomem *addr, void *datap, int len)
{
	u32 *data;

	for (data = datap; (len != 0) && (((u32)data & 0x1f) != 0); len--)
		*data++ = __raw_readl(addr);

	if (likely(len >= (0x20 >> 2))) {
		int tmp2, tmp3, tmp4, tmp5, tmp6;

		__asm__ __volatile__(
			"1:			\n\t"
			"mov.l	@%7, r0		\n\t"
			"mov.l	@%7, %2		\n\t"
#ifdef CONFIG_CPU_SH4
			"movca.l r0, @%0	\n\t"
#else
			"mov.l	r0, @%0		\n\t"
#endif
			"mov.l	@%7, %3		\n\t"
			"mov.l	@%7, %4		\n\t"
			"mov.l	@%7, %5		\n\t"
			"mov.l	@%7, %6		\n\t"
			"mov.l	@%7, r7		\n\t"
			"mov.l	@%7, r0		\n\t"
			"mov.l	%2, @(0x04,%0)	\n\t"
			"mov	#0x20>>2, %2	\n\t"
			"mov.l	%3, @(0x08,%0)	\n\t"
			"sub	%2, %1		\n\t"
			"mov.l	%4, @(0x0c,%0)	\n\t"
			"cmp/hi	%1, %2		! T if 32 > len	\n\t"
			"mov.l	%5, @(0x10,%0)	\n\t"
			"mov.l	%6, @(0x14,%0)	\n\t"
			"mov.l	r7, @(0x18,%0)	\n\t"
			"mov.l	r0, @(0x1c,%0)	\n\t"
			"bf.s	1b		\n\t"
			" add	#0x20, %0	\n\t"
			: "=&r" (data), "=&r" (len),
			  "=&r" (tmp2), "=&r" (tmp3), "=&r" (tmp4),
			  "=&r" (tmp5), "=&r" (tmp6)
			: "r"(addr), "0" (data), "1" (len)
			: "r0", "r7", "t", "memory");
	}

	for (; len != 0; len--)
		*data++ = __raw_readl(addr);
}
EXPORT_SYMBOL(__raw_readsl);

void __raw_writesl(volatile void __iomem *addr, const void *data, int len)
{
	if (likely(len != 0)) {
		int tmp1;

		__asm__ __volatile__ (
			"1:				\n\t"
			"mov.l	@%0+, %1	\n\t"
			"dt		%3		\n\t"
			"bf.s		1b		\n\t"
			" mov.l	%1, @%4		\n\t"
			: "=&r" (data), "=&r" (tmp1)
			: "0" (data), "r" (len), "r"(addr)
			: "t", "memory");
	}
}
EXPORT_SYMBOL(__raw_writesl);
