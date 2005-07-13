/*
 *  linux/arch/arm/mach-imx/time.c
 *
 *  Copyright (C) 2000-2001 Deep Blue Solutions
 *  Copyright (C) 2002 Shane Nay (shane@minirl.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/time.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/leds.h>
#include <asm/irq.h>
#include <asm/mach/time.h>

/* Use timer 1 as system timer */
#define TIMER_BASE IMX_TIM1_BASE

/*
 * Returns number of us since last clock interrupt.  Note that interrupts
 * will have been disabled by do_gettimeoffset()
 */
static unsigned long imx_gettimeoffset(void)
{
	unsigned long ticks;

	/*
	 * Get the current number of ticks.  Note that there is a race
	 * condition between us reading the timer and checking for
	 * an interrupt.  We get around this by ensuring that the
	 * counter has not reloaded between our two reads.
	 */
	ticks = IMX_TCN(TIMER_BASE);

	/*
	 * Interrupt pending?  If so, we've reloaded once already.
	 */
	if (IMX_TSTAT(TIMER_BASE) & TSTAT_COMP)
		ticks += LATCH;

	/*
	 * Convert the ticks to usecs
	 */
	return (1000000 / CLK32) * ticks;
}

/*
 * IRQ handler for the timer
 */
static irqreturn_t
imx_timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	write_seqlock(&xtime_lock);

	/* clear the interrupt */
	if (IMX_TSTAT(TIMER_BASE))
		IMX_TSTAT(TIMER_BASE) = 0;

	timer_tick(regs);
	write_sequnlock(&xtime_lock);

	return IRQ_HANDLED;
}

static struct irqaction imx_timer_irq = {
	.name		= "i.MX Timer Tick",
	.flags		= SA_INTERRUPT | SA_TIMER,
	.handler	= imx_timer_interrupt,
};

/*
 * Set up timer interrupt, and return the current time in seconds.
 */
static void __init imx_timer_init(void)
{
	/*
	 * Initialise to a known state (all timers off, and timing reset)
	 */
	IMX_TCTL(TIMER_BASE) = 0;
	IMX_TPRER(TIMER_BASE) = 0;
	IMX_TCMP(TIMER_BASE) = LATCH - 1;
	IMX_TCTL(TIMER_BASE) = TCTL_CLK_32 | TCTL_IRQEN | TCTL_TEN;

	/*
	 * Make irqs happen for the system timer
	 */
	setup_irq(TIM1_INT, &imx_timer_irq);
}

struct sys_timer imx_timer = {
	.init		= imx_timer_init,
	.offset		= imx_gettimeoffset,
};
