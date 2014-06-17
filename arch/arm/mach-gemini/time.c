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
#include <linux/clockchips.h>
#include <linux/clocksource.h>

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

static unsigned int tick_rate;

static int gemini_timer_set_next_event(unsigned long cycles,
				       struct clock_event_device *evt)
{
	u32 cr;

	cr = readl(TIMER_CR(IO_ADDRESS(GEMINI_TIMER_BASE)));

	/* This may be overdoing it, feel free to test without this */
	cr &= ~TIMER_2_CR_ENABLE;
	cr &= ~TIMER_2_CR_INT;
	writel(cr, TIMER_CR(IO_ADDRESS(GEMINI_TIMER_BASE)));

	/* Set next event */
	writel(cycles, TIMER_COUNT(IO_ADDRESS(GEMINI_TIMER2_BASE)));
	writel(cycles, TIMER_LOAD(IO_ADDRESS(GEMINI_TIMER2_BASE)));
	cr |= TIMER_2_CR_ENABLE;
	cr |= TIMER_2_CR_INT;
	writel(cr, TIMER_CR(IO_ADDRESS(GEMINI_TIMER_BASE)));

	return 0;
}

static void gemini_timer_set_mode(enum clock_event_mode mode,
				  struct clock_event_device *evt)
{
	u32 period = DIV_ROUND_CLOSEST(tick_rate, HZ);
	u32 cr;

	switch (mode) {
        case CLOCK_EVT_MODE_PERIODIC:
		/* Start the timer */
		writel(period,
		       TIMER_COUNT(IO_ADDRESS(GEMINI_TIMER2_BASE)));
		writel(period,
		       TIMER_LOAD(IO_ADDRESS(GEMINI_TIMER2_BASE)));
		cr = readl(TIMER_CR(IO_ADDRESS(GEMINI_TIMER_BASE)));
		cr |= TIMER_2_CR_ENABLE;
		cr |= TIMER_2_CR_INT;
		writel(cr, TIMER_CR(IO_ADDRESS(GEMINI_TIMER_BASE)));
		break;
	case CLOCK_EVT_MODE_ONESHOT:
	case CLOCK_EVT_MODE_UNUSED:
        case CLOCK_EVT_MODE_SHUTDOWN:
	case CLOCK_EVT_MODE_RESUME:
		/*
		 * Disable also for oneshot: the set_next() call will
		 * arm the timer instead.
		 */
		cr = readl(TIMER_CR(IO_ADDRESS(GEMINI_TIMER_BASE)));
		cr &= ~TIMER_2_CR_ENABLE;
		cr &= ~TIMER_2_CR_INT;
		writel(cr, TIMER_CR(IO_ADDRESS(GEMINI_TIMER_BASE)));
		break;
	default:
                break;
	}
}

/* Use TIMER2 as clock event */
static struct clock_event_device gemini_clockevent = {
	.name		= "TIMER2",
	.rating		= 300, /* Reasonably fast and accurate clock event */
	.features	= CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
	.set_next_event	= gemini_timer_set_next_event,
	.set_mode	= gemini_timer_set_mode,
};

/*
 * IRQ handler for the timer
 */
static irqreturn_t gemini_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = &gemini_clockevent;

	evt->event_handler(evt);
	return IRQ_HANDLED;
}

static struct irqaction gemini_timer_irq = {
	.name		= "Gemini Timer Tick",
	.flags		= IRQF_TIMER,
	.handler	= gemini_timer_interrupt,
};

/*
 * Set up timer interrupt, and return the current time in seconds.
 */
void __init gemini_timer_init(void)
{
	u32 reg_v;

	reg_v = readl(IO_ADDRESS(GEMINI_GLOBAL_BASE + GLOBAL_STATUS));
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

	/* Enable and use TIMER1 as clock source */
	writel(0xffffffff, TIMER_COUNT(IO_ADDRESS(GEMINI_TIMER1_BASE)));
	writel(0xffffffff, TIMER_LOAD(IO_ADDRESS(GEMINI_TIMER1_BASE)));
	writel(TIMER_1_CR_ENABLE, TIMER_CR(IO_ADDRESS(GEMINI_TIMER_BASE)));
	if (clocksource_mmio_init(TIMER_COUNT(IO_ADDRESS(GEMINI_TIMER1_BASE)),
				  "TIMER1", tick_rate, 300, 32,
				  clocksource_mmio_readl_up))
		pr_err("timer: failed to initialize gemini clock source\n");

	/* Configure and register the clockevent */
	clockevents_config_and_register(&gemini_clockevent, tick_rate,
					1, 0xffffffff);
}
