/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2014-2015 The Linux Foundation. All rights reserved.
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
#include <asm/sysreg.h>

static inline u32 __dcc_getstatus(void)
{
	return read_sysreg(mdccsr_el0);
}

static inline char __dcc_getchar(void)
{
	char c = read_sysreg(dbgdtrrx_el0);
	isb();

	return c;
}

static inline void __dcc_putchar(char c)
{
	/*
	 * The typecast is to make absolutely certain that 'c' is
	 * zero-extended.
	 */
	write_sysreg((unsigned char)c, dbgdtrtx_el0);
	isb();
}

#endif
