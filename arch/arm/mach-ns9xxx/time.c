/*
 * arch/arm/mach-ns9xxx/time.c
 *
 * Copyright (C) 2006 by Digi International Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
#include <linux/jiffies.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <asm/arch-ns9xxx/regs-sys.h>
#include <asm/arch-ns9xxx/clock.h>
#include <asm/arch-ns9xxx/irqs.h>
#include <asm/arch/system.h>
#include "generic.h"

#define TIMERCLOCKSELECT 64

static u32 usecs_per_tick;

static irqreturn_t
ns9xxx_timer_interrupt(int irq, void *dev_id)
{
	write_seqlock(&xtime_lock);
	timer_tick();
	write_sequnlock(&xtime_lock);

	return IRQ_HANDLED;
}

static unsigned long ns9xxx_timer_gettimeoffset(void)
{
	/* return the microseconds which have passed since the last interrupt
	 * was _serviced_.  That is, if an interrupt is pending or the counter
	 * reloads, return one period more. */

	u32 counter1 = SYS_TR(0);
	int pending = SYS_ISR & (1 << IRQ_TIMER0);
	u32 counter2 = SYS_TR(0);
	u32 elapsed;

	if (pending || counter2 > counter1)
		elapsed = 2 * SYS_TRC(0) - counter2;
	else
		elapsed = SYS_TRC(0) - counter1;

	return (elapsed * usecs_per_tick) >> 16;

}

static struct irqaction ns9xxx_timer_irq = {
	.name = "NS9xxx Timer Tick",
	.flags = IRQF_DISABLED | IRQF_TIMER | IRQF_IRQPOLL,
	.handler = ns9xxx_timer_interrupt,
};

static void __init ns9xxx_timer_init(void)
{
	int tc;

	usecs_per_tick =
		SH_DIV(1000000 * TIMERCLOCKSELECT, ns9xxx_cpuclock(), 16);

	/* disable timer */
	if ((tc = SYS_TC(0)) & SYS_TCx_TEN)
		SYS_TC(0) = tc & ~SYS_TCx_TEN;

	SYS_TRC(0) = SH_DIV(ns9xxx_cpuclock(), (TIMERCLOCKSELECT * HZ), 0);

	REGSET(tc, SYS_TCx, TEN, EN);
	REGSET(tc, SYS_TCx, TLCS, DIV64); /* This must match TIMERCLOCKSELECT */
	REGSET(tc, SYS_TCx, INTS, EN);
	REGSET(tc, SYS_TCx, UDS, DOWN);
	REGSET(tc, SYS_TCx, TDBG, STOP);
	REGSET(tc, SYS_TCx, TSZ, 32);
	REGSET(tc, SYS_TCx, REN, EN);
	SYS_TC(0) = tc;

	setup_irq(IRQ_TIMER0, &ns9xxx_timer_irq);
}

struct sys_timer ns9xxx_timer = {
	.init = ns9xxx_timer_init,
	.offset = ns9xxx_timer_gettimeoffset,
};
