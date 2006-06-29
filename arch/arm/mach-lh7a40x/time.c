/*
 *  arch/arm/mach-lh7a40x/time.c
 *
 *  Copyright (C) 2004 Logic Product Development
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  version 2 as published by the Free Software Foundation.
 *
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/time.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/leds.h>

#include <asm/mach/time.h>
#include "common.h"

#if HZ < 100
# define TIMER_CONTROL	TIMER_CONTROL2
# define TIMER_LOAD	TIMER_LOAD2
# define TIMER_CONSTANT	(508469/HZ)
# define TIMER_MODE	(TIMER_C_ENABLE | TIMER_C_PERIODIC | TIMER_C_508KHZ)
# define TIMER_EOI	TIMER_EOI2
# define TIMER_IRQ	IRQ_T2UI
#else
# define TIMER_CONTROL	TIMER_CONTROL3
# define TIMER_LOAD	TIMER_LOAD3
# define TIMER_CONSTANT	(3686400/HZ)
# define TIMER_MODE	(TIMER_C_ENABLE | TIMER_C_PERIODIC)
# define TIMER_EOI	TIMER_EOI3
# define TIMER_IRQ	IRQ_T3UI
#endif

static irqreturn_t
lh7a40x_timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	write_seqlock(&xtime_lock);

	TIMER_EOI = 0;
	timer_tick(regs);

	write_sequnlock(&xtime_lock);

	return IRQ_HANDLED;
}

static struct irqaction lh7a40x_timer_irq = {
	.name		= "LHA740x Timer Tick",
	.flags		= SA_INTERRUPT | SA_TIMER,
	.handler	= lh7a40x_timer_interrupt,
};

static void __init lh7a40x_timer_init (void)
{
				/* Stop/disable all timers */
	TIMER_CONTROL1 = 0;
	TIMER_CONTROL2 = 0;
	TIMER_CONTROL3 = 0;

	setup_irq (TIMER_IRQ, &lh7a40x_timer_irq);

	TIMER_LOAD = TIMER_CONSTANT;
	TIMER_CONTROL = TIMER_MODE;
}

struct sys_timer lh7a40x_timer = {
	.init		= &lh7a40x_timer_init,
};
