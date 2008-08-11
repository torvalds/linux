/*
 * linux/include/asm-sh/timex.h
 *
 * sh architecture timex specifications
 */
#ifndef __ASM_SH_TIMEX_H
#define __ASM_SH_TIMEX_H

#define CLOCK_TICK_RATE		(CONFIG_SH_PCLK_FREQ / 4) /* Underlying HZ */

typedef unsigned long long cycles_t;

static __inline__ cycles_t get_cycles (void)
{
	return 0;
}

#endif /* __ASM_SH_TIMEX_H */
