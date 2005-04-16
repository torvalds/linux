/*
 * arch/arm/mach-iop3xx/iop321-time.c
 *
 * Timer code for IOP321 based systems
 *
 * Author: Deepak Saxena <dsaxena@mvista.com>
 *
 * Copyright 2002-2003 MontaVista Software Inc.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/timex.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/mach-types.h>
#include <asm/mach/irq.h>
#include <asm/mach/time.h>

#define IOP321_TIME_SYNC 0

static inline unsigned long get_elapsed(void)
{
	return LATCH - *IOP321_TU_TCR0;
}

static unsigned long iop321_gettimeoffset(void)
{
	unsigned long elapsed, usec;
	u32 tisr1, tisr2;

	/*
	 * If an interrupt was pending before we read the timer,
	 * we've already wrapped.  Factor this into the time.
	 * If an interrupt was pending after we read the timer,
	 * it may have wrapped between checking the interrupt
	 * status and reading the timer.  Re-read the timer to
	 * be sure its value is after the wrap.
	 */

	asm volatile("mrc p6, 0, %0, c6, c1, 0" : "=r" (tisr1));
	elapsed = get_elapsed();
	asm volatile("mrc p6, 0, %0, c6, c1, 0" : "=r" (tisr2));

	if(tisr1 & 1)
		elapsed += LATCH;
	else if (tisr2 & 1)
		elapsed = LATCH + get_elapsed();

	/*
	 * Now convert them to usec.
	 */
	usec = (unsigned long)(elapsed * (tick_nsec / 1000)) / LATCH;

	return usec;
}

static irqreturn_t
iop321_timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	u32 tisr;

	write_seqlock(&xtime_lock);

	asm volatile("mrc p6, 0, %0, c6, c1, 0" : "=r" (tisr));
	tisr |= 1;
	asm volatile("mcr p6, 0, %0, c6, c1, 0" : : "r" (tisr));

	timer_tick(regs);

	write_sequnlock(&xtime_lock);

	return IRQ_HANDLED;
}

static struct irqaction iop321_timer_irq = {
	.name		= "IOP321 Timer Tick",
	.handler	= iop321_timer_interrupt,
	.flags		= SA_INTERRUPT
};

static void __init iop321_timer_init(void)
{
	u32 timer_ctl;

	setup_irq(IRQ_IOP321_TIMER0, &iop321_timer_irq);

	timer_ctl = IOP321_TMR_EN | IOP321_TMR_PRIVILEGED | IOP321_TMR_RELOAD |
			IOP321_TMR_RATIO_1_1;

	asm volatile("mcr p6, 0, %0, c4, c1, 0" : : "r" (LATCH));

	asm volatile("mcr p6, 0, %0, c0, c1, 0"	: : "r" (timer_ctl));
}

struct sys_timer iop321_timer = {
	.init		= &iop321_timer_init,
	.offset		= iop321_gettimeoffset,
};
