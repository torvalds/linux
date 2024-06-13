/* SPDX-License-Identifier: GPL-2.0 */
/*
 * linux/include/asm-parisc/timex.h
 *
 * PARISC architecture timex specifications
 */
#ifndef _ASMPARISC_TIMEX_H
#define _ASMPARISC_TIMEX_H

#include <asm/special_insns.h>

#define CLOCK_TICK_RATE	1193180 /* Underlying HZ */

typedef unsigned long cycles_t;

static inline cycles_t get_cycles(void)
{
	return mfctl(16);
}
#define get_cycles get_cycles

#endif
