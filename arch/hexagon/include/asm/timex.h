/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 */

#ifndef _ASM_TIMEX_H
#define _ASM_TIMEX_H

#include <asm-generic/timex.h>
#include <asm/timer-regs.h>

/* Using TCX0 as our clock.  CLOCK_TICK_RATE scheduled to be removed. */
#define CLOCK_TICK_RATE              TCX0_CLK_RATE

#define ARCH_HAS_READ_CURRENT_TIMER

static inline int read_current_timer(unsigned long *timer_val)
{
	*timer_val = (unsigned long) __vmgettime();
	return 0;
}

#endif
