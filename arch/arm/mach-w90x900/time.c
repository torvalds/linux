/*
 * linux/arch/arm/mach-w90x900/time.c
 *
 * Based on linux/arch/arm/plat-s3c24xx/time.c by Ben Dooks
 *
 * Copyright (c) 2008 Nuvoton technology corporation
 * All rights reserved.
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

#include <mach/map.h>
#include <mach/regs-timer.h>

static unsigned long w90x900_gettimeoffset(void)
{
	return 0;
}

/*IRQ handler for the timer*/

static irqreturn_t
w90x900_timer_interrupt(int irq, void *dev_id)
{
	timer_tick();
	__raw_writel(0x01, REG_TISR); /* clear TIF0 */
	return IRQ_HANDLED;
}

static struct irqaction w90x900_timer_irq = {
	.name		= "w90x900 Timer Tick",
	.flags		= IRQF_DISABLED | IRQF_TIMER | IRQF_IRQPOLL,
	.handler	= w90x900_timer_interrupt,
};

/*Set up timer reg.*/

static void w90x900_timer_setup(void)
{
	__raw_writel(0, REG_TCSR0);
	__raw_writel(0, REG_TCSR1);
	__raw_writel(0, REG_TCSR2);
	__raw_writel(0, REG_TCSR3);
	__raw_writel(0, REG_TCSR4);
	__raw_writel(0x1F, REG_TISR);
	__raw_writel(15000000/(100 * 100), REG_TICR0);
	__raw_writel(0x68000063, REG_TCSR0);
}

static void __init w90x900_timer_init(void)
{
	w90x900_timer_setup();
	setup_irq(IRQ_TIMER0, &w90x900_timer_irq);
}

struct sys_timer w90x900_timer = {
	.init		= w90x900_timer_init,
	.offset		= w90x900_gettimeoffset,
	.resume		= w90x900_timer_setup
};
