/* ASB2305-specific timer specifications
 *
 * Copyright (C) 2007, 2010 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#ifndef _ASM_UNIT_TIMEX_H
#define _ASM_UNIT_TIMEX_H

#include <asm/timer-regs.h>
#include <unit/clock.h>
#include <asm/param.h>

/*
 * jiffies counter specifications
 */

#define	TMJCBR_MAX		0xffff
#define	TMJCIRQ			TM1IRQ
#define	TMJCICR			TM1ICR

#ifndef __ASSEMBLY__

#define MN10300_SRC_IOCLK	MN10300_IOCLK

#ifndef HZ
# error HZ undeclared.
#endif /* !HZ */
/* use as little prescaling as possible to avoid losing accuracy */
#if (MN10300_SRC_IOCLK + HZ / 2) / HZ - 1 <= TMJCBR_MAX
# define IOCLK_PRESCALE		1
# define JC_TIMER_CLKSRC	TM0MD_SRC_IOCLK
# define TSC_TIMER_CLKSRC	TM4MD_SRC_IOCLK
#elif (MN10300_SRC_IOCLK / 8 + HZ / 2) / HZ - 1 <= TMJCBR_MAX
# define IOCLK_PRESCALE		8
# define JC_TIMER_CLKSRC	TM0MD_SRC_IOCLK_8
# define TSC_TIMER_CLKSRC	TM4MD_SRC_IOCLK_8
#elif (MN10300_SRC_IOCLK / 32 + HZ / 2) / HZ - 1 <= TMJCBR_MAX
# define IOCLK_PRESCALE		32
# define JC_TIMER_CLKSRC	TM0MD_SRC_IOCLK_32
# define TSC_TIMER_CLKSRC	TM4MD_SRC_IOCLK_32
#else
# error You lose.
#endif

#define MN10300_JCCLK		(MN10300_SRC_IOCLK / IOCLK_PRESCALE)
#define MN10300_TSCCLK		(MN10300_SRC_IOCLK / IOCLK_PRESCALE)

#define MN10300_JC_PER_HZ	((MN10300_JCCLK + HZ / 2) / HZ)
#define MN10300_TSC_PER_HZ	((MN10300_TSCCLK + HZ / 2) / HZ)

static inline void stop_jiffies_counter(void)
{
	u16 tmp;
	TM01MD = JC_TIMER_CLKSRC | TM1MD_SRC_TM0CASCADE << 8;
	tmp = TM01MD;
}

static inline void reload_jiffies_counter(u32 cnt)
{
	u32 tmp;

	TM01BR = cnt;
	tmp = TM01BR;

	TM01MD = JC_TIMER_CLKSRC |		\
		 TM1MD_SRC_TM0CASCADE << 8 |	\
		 TM0MD_INIT_COUNTER |		\
		 TM1MD_INIT_COUNTER << 8;


	TM01MD = JC_TIMER_CLKSRC |		\
		 TM1MD_SRC_TM0CASCADE << 8 |	\
		 TM0MD_COUNT_ENABLE |		\
		 TM1MD_COUNT_ENABLE << 8;

	tmp = TM01MD;
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
	u32 t32;

	/* set up timer 4 & 5 cascaded as a 32-bit counter to count real time
	 * - count down from 4Gig-1 to 0 and wrap at IOCLK rate
	 */
	TM45BR = TMTSCBR_MAX;
	t32 = TM45BR;

	TM4MD = TSC_TIMER_CLKSRC;
	TM4MD |= TM4MD_INIT_COUNTER;
	TM4MD &= ~TM4MD_INIT_COUNTER;
	TM4ICR = 0;
	t32 = TM4ICR;

	TM5MD = TM5MD_SRC_TM4CASCADE;
	TM5MD |= TM5MD_INIT_COUNTER;
	TM5MD &= ~TM5MD_INIT_COUNTER;
	TM5ICR = 0;
	t32 = TM5ICR;

	TM5MD |= TM5MD_COUNT_ENABLE;
	TM4MD |= TM4MD_COUNT_ENABLE;
	t32 = TM5MD;
	t32 = TM4MD;
}

static inline void shutdown_timestamp_counter(void)
{
	u8 t8;
	TM4MD = 0;
	TM5MD = 0;
	t8 = TM4MD;
	t8 = TM5MD;
}

/*
 * we use a cascaded pair of 16-bit down-counting timers to count I/O
 * clock cycles for the purposes of time keeping
 */
typedef unsigned long cycles_t;

static inline cycles_t read_timestamp_counter(void)
{
	return (cycles_t)~TMTSCBC;
}

#endif /* !__ASSEMBLY__ */

#endif /* _ASM_UNIT_TIMEX_H */
