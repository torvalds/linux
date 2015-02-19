/* Copyright (c) 2010, 2014 The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
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
