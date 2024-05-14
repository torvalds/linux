/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2010, 2014 The Linux Foundation. All rights reserved.
 */

#include <asm/barrier.h>

static inline u32 __dcc_getstatus(void)
{
	u32 __ret;
	asm volatile("mrc p14, 0, %0, c0, c1, 0	@ read comms ctrl reg"
		: "=r" (__ret) : : "cc");

	return __ret;
}

static inline char __dcc_getchar(void)
{
	char __c;

	asm volatile("mrc p14, 0, %0, c0, c5, 0	@ read comms data reg"
		: "=r" (__c));
	isb();

	return __c;
}

static inline void __dcc_putchar(char c)
{
	asm volatile("mcr p14, 0, %0, c0, c5, 0	@ write a char"
		: /* no output register */
		: "r" (c));
	isb();
}
