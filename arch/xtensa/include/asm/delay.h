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
	/* 2 cycles per loop. */
	__asm__ __volatile__ ("1: addi %0, %0, -2; bgeui %0, 2, 1b"
			      : "=r" (loops) : "0" (loops));
}

/* For SMP/NUMA systems, change boot_cpu_data to something like
 * local_cpu_data->... where local_cpu_data points to the current
 * cpu. */

static __inline__ void udelay (unsigned long usecs)
{
	unsigned long start = get_ccount();
	unsigned long cycles = usecs * (loops_per_jiffy / (1000000UL / HZ));

	/* Note: all variables are unsigned (can wrap around)! */
	while (((unsigned long)get_ccount()) - start < cycles)
		;
}

#endif
