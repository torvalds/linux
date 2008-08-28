/*
 * asm-blackfin/timex.h: cpu cycles!
 *
 * Copyright 2004-2008 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef _ASM_BLACKFIN_TIMEX_H
#define _ASM_BLACKFIN_TIMEX_H

#define CLOCK_TICK_RATE	1000000	/* Underlying HZ */

typedef unsigned long long cycles_t;

static inline cycles_t get_cycles(void)
{
	unsigned long tmp, tmp2;
	__asm__("%0 = cycles; %1 = cycles2;" : "=d"(tmp), "=d"(tmp2));
	return tmp | ((cycles_t)tmp2 << 32);
}

#endif
