/*
 * linux/include/asm-m68k/timex.h
 *
 * m68k architecture timex specifications
 */
#ifndef _ASMm68K_TIMEX_H
#define _ASMm68K_TIMEX_H

#ifdef CONFIG_COLDFIRE
/*
 * CLOCK_TICK_RATE should give the underlying frequency of the tick timer
 * to make ntp work best.  For Coldfires, that's the main clock.
 */
#include <asm/coldfire.h>
#define CLOCK_TICK_RATE	MCF_CLK
#else
/*
 * This default CLOCK_TICK_RATE is probably wrong for many 68k boards
 * Users of those boards will need to check and modify accordingly
 */
#define CLOCK_TICK_RATE	1193180 /* Underlying HZ */
#endif

typedef unsigned long cycles_t;

static inline cycles_t get_cycles(void)
{
	return 0;
}

#endif
