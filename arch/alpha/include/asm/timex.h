/* SPDX-License-Identifier: GPL-2.0 */
/*
 * linux/include/asm-alpha/timex.h
 *
 * ALPHA architecture timex specifications
 */
#ifndef _ASMALPHA_TIMEX_H
#define _ASMALPHA_TIMEX_H

/* With only one or two oddballs, we use the RTC as the ticker, selecting
   the 32.768kHz reference clock, which nicely divides down to our HZ.  */
#define CLOCK_TICK_RATE	32768

/*
 * Standard way to access the cycle counter.
 * Currently only used on SMP for scheduling.
 *
 * Only the low 32 bits are available as a continuously counting entity. 
 * But this only means we'll force a reschedule every 8 seconds or so,
 * which isn't an evil thing.
 */

typedef unsigned int cycles_t;

static inline cycles_t get_cycles (void)
{
	cycles_t ret;
	__asm__ __volatile__ ("rpcc %0" : "=r"(ret));
	return ret;
}

#endif
