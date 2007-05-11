/*
 * linux/arch/arm/mach-at91/at91sam926x_time.c
 *
 * Copyright (C) 2005-2006 M. Amine SAYA, ATMEL Rousset, France
 * Revision	 2005 M. Nicolas Diremdjian, ATMEL Rousset, France
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/time.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/mach/time.h>

#include <asm/arch/at91_pit.h>


#define PIT_CPIV(x)	((x) & AT91_PIT_CPIV)
#define PIT_PICNT(x)	(((x) & AT91_PIT_PICNT) >> 20)

/*
 * Returns number of microseconds since last timer interrupt.  Note that interrupts
 * will have been disabled by do_gettimeofday()
 *  'LATCH' is hwclock ticks (see CLOCK_TICK_RATE in timex.h) per jiffy.
 */
static unsigned long at91sam926x_gettimeoffset(void)
{
	unsigned long elapsed;
	unsigned long t = at91_sys_read(AT91_PIT_PIIR);

	elapsed = (PIT_PICNT(t) * LATCH) + PIT_CPIV(t);		/* hardware clock cycles */

	return (unsigned long)(elapsed * jiffies_to_usecs(1)) / LATCH;
}

/*
 * IRQ handler for the timer.
 */
static irqreturn_t at91sam926x_timer_interrupt(int irq, void *dev_id)
{
	volatile long nr_ticks;

	if (at91_sys_read(AT91_PIT_SR) & AT91_PIT_PITS) {	/* This is a shared interrupt */
		write_seqlock(&xtime_lock);

		/* Get number to ticks performed before interrupt and clear PIT interrupt */
		nr_ticks = PIT_PICNT(at91_sys_read(AT91_PIT_PIVR));
		do {
			timer_tick();
			nr_ticks--;
		} while (nr_ticks);

		write_sequnlock(&xtime_lock);
		return IRQ_HANDLED;
	} else
		return IRQ_NONE;		/* not handled */
}

static struct irqaction at91sam926x_timer_irq = {
	.name		= "at91_tick",
	.flags		= IRQF_SHARED | IRQF_DISABLED | IRQF_TIMER | IRQF_IRQPOLL,
	.handler	= at91sam926x_timer_interrupt
};

void at91sam926x_timer_reset(void)
{
	/* Disable timer */
	at91_sys_write(AT91_PIT_MR, 0);

	/* Clear any pending interrupts */
	(void) at91_sys_read(AT91_PIT_PIVR);

	/* Set Period Interval timer and enable its interrupt */
	at91_sys_write(AT91_PIT_MR, (LATCH & AT91_PIT_PIV) | AT91_PIT_PITIEN | AT91_PIT_PITEN);
}

/*
 * Set up timer interrupt.
 */
void __init at91sam926x_timer_init(void)
{
	/* Initialize and enable the timer */
	at91sam926x_timer_reset();

	/* Make IRQs happen for the system timer. */
	setup_irq(AT91_ID_SYS, &at91sam926x_timer_irq);
}

#ifdef CONFIG_PM
static void at91sam926x_timer_suspend(void)
{
	/* Disable timer */
	at91_sys_write(AT91_PIT_MR, 0);
}
#else
#define at91sam926x_timer_suspend	NULL
#endif

struct sys_timer at91sam926x_timer = {
	.init		= at91sam926x_timer_init,
	.offset		= at91sam926x_gettimeoffset,
	.suspend	= at91sam926x_timer_suspend,
	.resume		= at91sam926x_timer_reset,
};

