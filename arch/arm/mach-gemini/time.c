/*
 *  Copyright (C) 2001-2006 Storlink, Corp.
 *  Copyright (C) 2008-2009 Paulius Zaleckas <paulius.zaleckas@teltonika.lt>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <mach/hardware.h>
#include <mach/global_reg.h>
#include <asm/mach/time.h>

/*
 * Register definitions for the timers
 */
#define TIMER_COUNT(BASE_ADDR)		(BASE_ADDR  + 0x00)
#define TIMER_LOAD(BASE_ADDR)		(BASE_ADDR  + 0x04)
#define TIMER_MATCH1(BASE_ADDR)		(BASE_ADDR  + 0x08)
#define TIMER_MATCH2(BASE_ADDR)		(BASE_ADDR  + 0x0C)
#define TIMER_CR(BASE_ADDR)		(BASE_ADDR  + 0x30)

#define TIMER_1_CR_ENABLE		(1 << 0)
#define TIMER_1_CR_CLOCK		(1 << 1)
#define TIMER_1_CR_INT			(1 << 2)
#define TIMER_2_CR_ENABLE		(1 << 3)
#define TIMER_2_CR_CLOCK		(1 << 4)
#define TIMER_2_CR_INT			(1 << 5)
#define TIMER_3_CR_ENABLE		(1 << 6)
#define TIMER_3_CR_CLOCK		(1 << 7)
#define TIMER_3_CR_INT			(1 << 8)

/*
 * IRQ handler for the timer
 */
static irqreturn_t gemini_timer_interrupt(int irq, void *dev_id)
{
	timer_tick();

	return IRQ_HANDLED;
}

static struct irqaction gemini_timer_irq = {
	.name		= "Gemini Timer Tick",
	.flags		= IRQF_DISABLED | IRQF_TIMER,
	.handler	= gemini_timer_interrupt,
};

/*
 * Set up timer interrupt, and return the current time in seconds.
 */
void __init gemini_timer_init(void)
{
	unsigned int tick_rate, reg_v;

	reg_v = __raw_readl(IO_ADDRESS(GEMINI_GLOBAL_BASE + GLOBAL_STATUS));
	tick_rate = REG_TO_AHB_SPEED(reg_v) * 1000000;

	printk(KERN_INFO "Bus: %dMHz", tick_rate / 1000000);

	tick_rate /= 6;		/* APB bus run AHB*(1/6) */

	switch(reg_v & CPU_AHB_RATIO_MASK) {
	case CPU_AHB_1_1:
		printk(KERN_CONT "(1/1)\n");
		break;
	case CPU_AHB_3_2:
		printk(KERN_CONT "(3/2)\n");
		break;
	case CPU_AHB_24_13:
		printk(KERN_CONT "(24/13)\n");
		break;
	case CPU_AHB_2_1:
		printk(KERN_CONT "(2/1)\n");
		break;
	}

	/*
	 * Make irqs happen for the system timer
	 */
	setup_irq(IRQ_TIMER2, &gemini_timer_irq);
	/* Start the timer */
	__raw_writel(tick_rate / HZ, TIMER_COUNT(IO_ADDRESS(GEMINI_TIMER2_BASE)));
	__raw_writel(tick_rate / HZ, TIMER_LOAD(IO_ADDRESS(GEMINI_TIMER2_BASE)));
	__raw_writel(TIMER_2_CR_ENABLE | TIMER_2_CR_INT, TIMER_CR(IO_ADDRESS(GEMINI_TIMER_BASE)));
}
