/*
 * include/asm-xtensa/delay.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 *
 */

#ifndef _XTENSA_DELAY_H
#define _XTENSA_DELAY_H

#include <asm/timex.h>
#include <asm/param.h>

extern unsigned long loops_per_jiffy;

static inline void __delay(unsigned long loops)
{
	if (__builtin_constant_p(loops) && loops < 2)
		__asm__ __volatile__ ("nop");
	else if (loops >= 2)
		/* 2 cycles per loop. */
		__asm__ __volatile__ ("1: addi %0, %0, -2; bgeui %0, 2, 1b"
				: "+r" (loops));
}

/* Undefined function to get compile-time error */
void __bad_udelay(void);
void __bad_ndelay(void);

#define __MAX_UDELAY 30000
#define __MAX_NDELAY 30000

static inline void __udelay(unsigned long usecs)
{
	unsigned long start = get_ccount();
	unsigned long cycles = (usecs * (ccount_freq >> 15)) >> 5;

	/* Note: all variables are unsigned (can wrap around)! */
	while (((unsigned long)get_ccount()) - start < cycles)
		cpu_relax();
}

static inline void udelay(unsigned long usec)
{
	if (__builtin_constant_p(usec) && usec >= __MAX_UDELAY)
		__bad_udelay();
	else
		__udelay(usec);
}

static inline void __ndelay(unsigned long nsec)
{
	/*
	 * Inner shift makes sure multiplication doesn't overflow
	 * for legitimate nsec values
	 */
	unsigned long cycles = (nsec * (ccount_freq >> 15)) >> 15;
	__delay(cycles);
}

#define ndelay(n) ndelay(n)

static inline void ndelay(unsigned long nsec)
{
	if (__builtin_constant_p(nsec) && nsec >= __MAX_NDELAY)
		__bad_ndelay();
	else
		__ndelay(nsec);
}

#endif
