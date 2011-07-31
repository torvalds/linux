/* ASB2305 timer specifcations
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#ifndef _ASM_UNIT_TIMEX_H
#define _ASM_UNIT_TIMEX_H

#ifndef __ASSEMBLY__
#include <linux/irq.h>
#endif /* __ASSEMBLY__ */

#include <asm/timer-regs.h>
#include <unit/clock.h>

/*
 * jiffies counter specifications
 */

#define	TMJCBR_MAX		0xffff
#define	TMJCBC			TM01BC

#define	TMJCMD			TM01MD
#define	TMJCBR			TM01BR
#define	TMJCIRQ			TM1IRQ
#define	TMJCICR			TM1ICR
#define	TMJCICR_LEVEL		GxICR_LEVEL_5

#ifndef __ASSEMBLY__

static inline void startup_jiffies_counter(void)
{
	unsigned rate;
	u16 md, t16;

	/* use as little prescaling as possible to avoid losing accuracy */
	md = TM0MD_SRC_IOCLK;
	rate = MN10300_JCCLK / HZ;

	if (rate > TMJCBR_MAX) {
		md = TM0MD_SRC_IOCLK_8;
		rate = MN10300_JCCLK / 8 / HZ;

		if (rate > TMJCBR_MAX) {
			md = TM0MD_SRC_IOCLK_32;
			rate = MN10300_JCCLK / 32 / HZ;

			if (rate > TMJCBR_MAX)
				BUG();
		}
	}

	TMJCBR = rate - 1;
	t16 = TMJCBR;

	TMJCMD =
		md |
		TM1MD_SRC_TM0CASCADE << 8 |
		TM0MD_INIT_COUNTER |
		TM1MD_INIT_COUNTER << 8;

	TMJCMD =
		md |
		TM1MD_SRC_TM0CASCADE << 8 |
		TM0MD_COUNT_ENABLE |
		TM1MD_COUNT_ENABLE << 8;

	t16 = TMJCMD;

	TMJCICR |= GxICR_ENABLE | GxICR_DETECT | GxICR_REQUEST;
	t16 = TMJCICR;
}

static inline void shutdown_jiffies_counter(void)
{
}

#endif /* !__ASSEMBLY__ */


/*
 * timestamp counter specifications
 */

#define	TMTSCBR_MAX		0xffffffff
#define	TMTSCBC			TM45BC

#ifndef __ASSEMBLY__

static inline void startup_timestamp_counter(void)
{
	/* set up timer 4 & 5 cascaded as a 32-bit counter to count real time
	 * - count down from 4Gig-1 to 0 and wrap at IOCLK rate
	 */
	TM45BR = TMTSCBR_MAX;

	TM4MD = TM4MD_SRC_IOCLK;
	TM4MD |= TM4MD_INIT_COUNTER;
	TM4MD &= ~TM4MD_INIT_COUNTER;
	TM4ICR = 0;

	TM5MD = TM5MD_SRC_TM4CASCADE;
	TM5MD |= TM5MD_INIT_COUNTER;
	TM5MD &= ~TM5MD_INIT_COUNTER;
	TM5ICR = 0;

	TM5MD |= TM5MD_COUNT_ENABLE;
	TM4MD |= TM4MD_COUNT_ENABLE;
}

static inline void shutdown_timestamp_counter(void)
{
	TM4MD = 0;
	TM5MD = 0;
}

/*
 * we use a cascaded pair of 16-bit down-counting timers to count I/O
 * clock cycles for the purposes of time keeping
 */
typedef unsigned long cycles_t;

static inline cycles_t read_timestamp_counter(void)
{
	return (cycles_t) TMTSCBC;
}

#endif /* !__ASSEMBLY__ */

#endif /* _ASM_UNIT_TIMEX_H */
