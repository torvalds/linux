/* Copyright (c) 2014-2015 The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * A call to __dcc_getchar() or __dcc_putchar() is typically followed by
 * a call to __dcc_getstatus().  We want to make sure that the CPU does
 * not speculative read the DCC status before executing the read or write
 * instruction.  That's what the ISBs are for.
 *
 * The 'volatile' ensures that the compiler does not cache the status bits,
 * and instead reads the DCC register every time.
 */
#ifndef __ASM_DCC_H
#define __ASM_DCC_H

#include <asm/barrier.h>

static inline u32 __dcc_getstatus(void)
{
	u32 ret;

	asm volatile("mrs %0, mdccsr_el0" : "=r" (ret));

	return ret;
}

static inline char __dcc_getchar(void)
{
	char c;

	asm volatile("mrs %0, dbgdtrrx_el0" : "=r" (c));
	isb();

	return c;
}

static inline void __dcc_putchar(char c)
{
	/*
	 * The typecast is to make absolutely certain that 'c' is
	 * zero-extended.
	 */
	asm volatile("msr dbgdtrtx_el0, %0"
			: : "r" ((unsigned long)(unsigned char)c));
	isb();
}

#endif
