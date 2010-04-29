/*
 * linux/arch/arm/mach-nuc93x/time.c
 *
 * Copyright (c) 2009 Nuvoton technology corporation.
 *
 * Wan ZongShun <mcuos.com@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/leds.h>

#include <asm/mach-types.h>
#include <asm/mach/irq.h>
#include <asm/mach/time.h>

#include <mach/system.h>
#include <mach/map.h>
#include <mach/regs-timer.h>

#define RESETINT	0x01
#define PERIOD		(0x01 << 27)
#define ONESHOT		(0x00 << 27)
#define COUNTEN		(0x01 << 30)
#define INTEN		(0x01 << 29)

#define TICKS_PER_SEC	100
#define PRESCALE	0x63 /* Divider = prescale + 1 */

unsigned int timer0_load;

static unsigned long nuc93x_gettimeoffset(void)
{
	return 0;
}

/*IRQ handler for the timer*/

static irqreturn_t nuc93x_timer_interrupt(int irq, void *dev_id)
{
	timer_tick();
	__raw_writel(0x01, REG_TISR); /* clear TIF0 */
	return IRQ_HANDLED;
}

static struct irqaction nuc93x_timer_irq = {
	.name		= "nuc93x Timer Tick",
	.flags		= IRQF_DISABLED | IRQF_TIMER | IRQF_IRQPOLL,
	.handler	= nuc93x_timer_interrupt,
};

/*Set up timer reg.*/

static void nuc93x_timer_setup(void)
{
	struct clk *ck_ext = clk_get(NULL, "ext");
	struct clk *ck_timer = clk_get(NULL, "timer");
	unsigned int rate, val = 0;

	BUG_ON(IS_ERR(ck_ext) || IS_ERR(ck_timer));

	clk_enable(ck_timer);
	rate = clk_get_rate(ck_ext);
	clk_put(ck_ext);
	rate = rate / (PRESCALE + 0x01);

	 /* set a known state */
	__raw_writel(0x00, REG_TCSR0);
	__raw_writel(RESETINT, REG_TISR);

	timer0_load = (rate / TICKS_PER_SEC);
	__raw_writel(timer0_load, REG_TICR0);

	val |= (PERIOD | COUNTEN | INTEN | PRESCALE);;
	__raw_writel(val, REG_TCSR0);

}

static void __init nuc93x_timer_init(void)
{
	nuc93x_timer_setup();
	setup_irq(IRQ_TIMER0, &nuc93x_timer_irq);
}

struct sys_timer nuc93x_timer = {
	.init		= nuc93x_timer_init,
	.offset		= nuc93x_gettimeoffset,
	.resume		= nuc93x_timer_setup
};
