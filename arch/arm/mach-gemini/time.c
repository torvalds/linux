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
#include <linux/sched_clock.h>

/*
 * Register definitions for the timers
 */

#define TIMER1_BASE		GEMINI_TIMER_BASE
#define TIMER2_BASE		(GEMINI_TIMER_BASE + 0x10)
#define TIMER3_BASE		(GEMINI_TIMER_BASE + 0x20)

#define TIMER_COUNT(BASE)	(IO_ADDRESS(BASE) + 0x00)
#define TIMER_LOAD(BASE)	(IO_ADDRESS(BASE) + 0x04)
#define TIMER_MATCH1(BASE)	(IO_ADDRESS(BASE) + 0x08)
#define TIMER_MATCH2(BASE)	(IO_ADDRESS(BASE) + 0x0C)
#define TIMER_CR		(IO_ADDRESS(GEMINI_TIMER_BASE) + 0x30)
#define TIMER_INTR_STATE	(IO_ADDRESS(GEMINI_TIMER_BASE) + 0x34)
#define TIMER_INTR_MASK		(IO_ADDRESS(GEMINI_TIMER_BASE) + 0x38)

#define TIMER_1_CR_ENABLE	(1 << 0)
#define TIMER_1_CR_CLOCK	(1 << 1)
#define TIMER_1_CR_INT		(1 << 2)
#define TIMER_2_CR_ENABLE	(1 << 3)
#define TIMER_2_CR_CLOCK	(1 << 4)
#define TIMER_2_CR_INT		(1 << 5)
#define TIMER_3_CR_ENABLE	(1 << 6)
#define TIMER_3_CR_CLOCK	(1 << 7)
#define TIMER_3_CR_INT		(1 << 8)
#define TIMER_1_CR_UPDOWN	(1 << 9)
#define TIMER_2_CR_UPDOWN	(1 << 10)
#define TIMER_3_CR_UPDOWN	(1 << 11)
#define TIMER_DEFAULT_FLAGS	(TIMER_1_CR_UPDOWN | \
				 TIMER_3_CR_ENABLE | \
				 TIMER_3_CR_UPDOWN)

#define TIMER_1_INT_MATCH1	(1 << 0)
#define TIMER_1_INT_MATCH2	(1 << 1)
#define TIMER_1_INT_OVERFLOW	(1 << 2)
#define TIMER_2_INT_MATCH1	(1 << 3)
#define TIMER_2_INT_MATCH2	(1 << 4)
#define TIMER_2_INT_OVERFLOW	(1 << 5)
#define TIMER_3_INT_MATCH1	(1 << 6)
#define TIMER_3_INT_MATCH2	(1 << 7)
#define TIMER_3_INT_OVERFLOW	(1 << 8)
#define TIMER_INT_ALL_MASK	0x1ff


static unsigned int tick_rate;

static u64 notrace gemini_read_sched_clock(void)
{
	return readl(TIMER_COUNT(TIMER3_BASE));
}

static int gemini_timer_set_next_event(unsigned long cycles,
				       struct clock_event_device *evt)
{
	u32 cr;

	/* Setup the match register */
	cr = readl(TIMER_COUNT(TIMER1_BASE));
	writel(cr + cycles, TIMER_MATCH1(TIMER1_BASE));
	if (readl(TIMER_COUNT(TIMER1_BASE)) - cr > cycles)
		return -ETIME;

	return 0;
}

static int gemini_timer_shutdown(struct clock_event_device *evt)
{
	u32 cr;

	/*
	 * Disable also for oneshot: the set_next() call will arm the timer
	 * instead.
	 */
	/* Stop timer and interrupt. */
	cr = readl(TIMER_CR);
	cr &= ~(TIMER_1_CR_ENABLE | TIMER_1_CR_INT);
	writel(cr, TIMER_CR);

	/* Setup counter start from 0 */
	writel(0, TIMER_COUNT(TIMER1_BASE));
	writel(0, TIMER_LOAD(TIMER1_BASE));

	/* enable interrupt */
	cr = readl(TIMER_INTR_MASK);
	cr &= ~(TIMER_1_INT_OVERFLOW | TIMER_1_INT_MATCH2);
	cr |= TIMER_1_INT_MATCH1;
	writel(cr, TIMER_INTR_MASK);

	/* start the timer */
	cr = readl(TIMER_CR);
	cr |= TIMER_1_CR_ENABLE;
	writel(cr, TIMER_CR);

	return 0;
}

static int gemini_timer_set_periodic(struct clock_event_device *evt)
{
	u32 period = DIV_ROUND_CLOSEST(tick_rate, HZ);
	u32 cr;

	/* Stop timer and interrupt */
	cr = readl(TIMER_CR);
	cr &= ~(TIMER_1_CR_ENABLE | TIMER_1_CR_INT);
	writel(cr, TIMER_CR);

	/* Setup timer to fire at 1/HT intervals. */
	cr = 0xffffffff - (period - 1);
	writel(cr, TIMER_COUNT(TIMER1_BASE));
	writel(cr, TIMER_LOAD(TIMER1_BASE));

	/* enable interrupt on overflow */
	cr = readl(TIMER_INTR_MASK);
	cr &= ~(TIMER_1_INT_MATCH1 | TIMER_1_INT_MATCH2);
	cr |= TIMER_1_INT_OVERFLOW;
	writel(cr, TIMER_INTR_MASK);

	/* Start the timer */
	cr = readl(TIMER_CR);
	cr |= TIMER_1_CR_ENABLE;
	cr |= TIMER_1_CR_INT;
	writel(cr, TIMER_CR);

	return 0;
}

/* Use TIMER1 as clock event */
static struct clock_event_device gemini_clockevent = {
	.name			= "TIMER1",
	/* Reasonably fast and accurate clock event */
	.rating			= 300,
	.shift                  = 32,
	.features		= CLOCK_EVT_FEAT_PERIODIC |
				  CLOCK_EVT_FEAT_ONESHOT,
	.set_next_event		= gemini_timer_set_next_event,
	.set_state_shutdown	= gemini_timer_shutdown,
	.set_state_periodic	= gemini_timer_set_periodic,
	.set_state_oneshot	= gemini_timer_shutdown,
	.tick_resume		= gemini_timer_shutdown,
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
	 * Reset the interrupt mask and status
	 */
	writel(TIMER_INT_ALL_MASK, TIMER_INTR_MASK);
	writel(0, TIMER_INTR_STATE);
	writel(TIMER_DEFAULT_FLAGS, TIMER_CR);

	/*
	 * Setup free-running clocksource timer (interrupts
	 * disabled.)
	 */
	writel(0, TIMER_COUNT(TIMER3_BASE));
	writel(0, TIMER_LOAD(TIMER3_BASE));
	writel(0, TIMER_MATCH1(TIMER3_BASE));
	writel(0, TIMER_MATCH2(TIMER3_BASE));
	clocksource_mmio_init(TIMER_COUNT(TIMER3_BASE),
			      "gemini_clocksource", tick_rate,
			      300, 32, clocksource_mmio_readl_up);
	sched_clock_register(gemini_read_sched_clock, 32, tick_rate);

	/*
	 * Setup clockevent timer (interrupt-driven.)
	*/
	writel(0, TIMER_COUNT(TIMER1_BASE));
	writel(0, TIMER_LOAD(TIMER1_BASE));
	writel(0, TIMER_MATCH1(TIMER1_BASE));
	writel(0, TIMER_MATCH2(TIMER1_BASE));
	setup_irq(IRQ_TIMER1, &gemini_timer_irq);
	gemini_clockevent.cpumask = cpumask_of(0);
	clockevents_config_and_register(&gemini_clockevent, tick_rate,
					1, 0xffffffff);

}
