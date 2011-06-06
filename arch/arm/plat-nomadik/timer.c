/*
 *  linux/arch/arm/plat-nomadik/timer.c
 *
 * Copyright (C) 2008 STMicroelectronics
 * Copyright (C) 2010 Alessandro Rubini
 * Copyright (C) 2010 Linus Walleij for ST-Ericsson
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 */
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/clockchips.h>
#include <linux/clk.h>
#include <linux/jiffies.h>
#include <linux/err.h>
#include <linux/sched.h>
#include <asm/mach/time.h>
#include <asm/sched_clock.h>

#include <plat/mtu.h>

void __iomem *mtu_base; /* Assigned by machine code */

/*
 * Override the global weak sched_clock symbol with this
 * local implementation which uses the clocksource to get some
 * better resolution when scheduling the kernel.
 */
static DEFINE_CLOCK_DATA(cd);

unsigned long long notrace sched_clock(void)
{
	u32 cyc;

	if (unlikely(!mtu_base))
		return 0;

	cyc = -readl(mtu_base + MTU_VAL(0));
	return cyc_to_sched_clock(&cd, cyc, (u32)~0);
}

static void notrace nomadik_update_sched_clock(void)
{
	u32 cyc = -readl(mtu_base + MTU_VAL(0));
	update_sched_clock(&cd, cyc, (u32)~0);
}

/* Clockevent device: use one-shot mode */
static void nmdk_clkevt_mode(enum clock_event_mode mode,
			     struct clock_event_device *dev)
{
	u32 cr;

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		pr_err("%s: periodic mode not supported\n", __func__);
		break;
	case CLOCK_EVT_MODE_ONESHOT:
		/* Load highest value, enable device, enable interrupts */
		cr = readl(mtu_base + MTU_CR(1));
		writel(0, mtu_base + MTU_LR(1));
		writel(cr | MTU_CRn_ENA, mtu_base + MTU_CR(1));
		writel(1 << 1, mtu_base + MTU_IMSC);
		break;
	case CLOCK_EVT_MODE_SHUTDOWN:
	case CLOCK_EVT_MODE_UNUSED:
		/* disable irq */
		writel(0, mtu_base + MTU_IMSC);
		/* disable timer */
		cr = readl(mtu_base + MTU_CR(1));
		cr &= ~MTU_CRn_ENA;
		writel(cr, mtu_base + MTU_CR(1));
		/* load some high default value */
		writel(0xffffffff, mtu_base + MTU_LR(1));
		break;
	case CLOCK_EVT_MODE_RESUME:
		break;
	}
}

static int nmdk_clkevt_next(unsigned long evt, struct clock_event_device *ev)
{
	/* writing the value has immediate effect */
	writel(evt, mtu_base + MTU_LR(1));
	return 0;
}

static struct clock_event_device nmdk_clkevt = {
	.name		= "mtu_1",
	.features	= CLOCK_EVT_FEAT_ONESHOT,
	.rating		= 200,
	.set_mode	= nmdk_clkevt_mode,
	.set_next_event	= nmdk_clkevt_next,
};

/*
 * IRQ Handler for timer 1 of the MTU block.
 */
static irqreturn_t nmdk_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evdev = dev_id;

	writel(1 << 1, mtu_base + MTU_ICR); /* Interrupt clear reg */
	evdev->event_handler(evdev);
	return IRQ_HANDLED;
}

static struct irqaction nmdk_timer_irq = {
	.name		= "Nomadik Timer Tick",
	.flags		= IRQF_DISABLED | IRQF_TIMER,
	.handler	= nmdk_timer_interrupt,
	.dev_id		= &nmdk_clkevt,
};

void __init nmdk_timer_init(void)
{
	unsigned long rate;
	struct clk *clk0;
	u32 cr = MTU_CRn_32BITS;

	clk0 = clk_get_sys("mtu0", NULL);
	BUG_ON(IS_ERR(clk0));

	clk_enable(clk0);

	/*
	 * Tick rate is 2.4MHz for Nomadik and 2.4Mhz, 100MHz or 133 MHz
	 * for ux500.
	 * Use a divide-by-16 counter if the tick rate is more than 32MHz.
	 * At 32 MHz, the timer (with 32 bit counter) can be programmed
	 * to wake-up at a max 127s a head in time. Dividing a 2.4 MHz timer
	 * with 16 gives too low timer resolution.
	 */
	rate = clk_get_rate(clk0);
	if (rate > 32000000) {
		rate /= 16;
		cr |= MTU_CRn_PRESCALE_16;
	} else {
		cr |= MTU_CRn_PRESCALE_1;
	}

	/* Timer 0 is the free running clocksource */
	writel(cr, mtu_base + MTU_CR(0));
	writel(0, mtu_base + MTU_LR(0));
	writel(0, mtu_base + MTU_BGLR(0));
	writel(cr | MTU_CRn_ENA, mtu_base + MTU_CR(0));

	if (clocksource_mmio_init(mtu_base + MTU_VAL(0), "mtu_0",
			rate, 200, 32, clocksource_mmio_readl_down))
		pr_err("timer: failed to initialize clock source %s\n",
		       "mtu_0");

	init_sched_clock(&cd, nomadik_update_sched_clock, 32, rate);

	/* Timer 1 is used for events */

	clockevents_calc_mult_shift(&nmdk_clkevt, rate, MTU_MIN_RANGE);

	writel(cr | MTU_CRn_ONESHOT, mtu_base + MTU_CR(1)); /* off, currently */

	nmdk_clkevt.max_delta_ns =
		clockevent_delta2ns(0xffffffff, &nmdk_clkevt);
	nmdk_clkevt.min_delta_ns =
		clockevent_delta2ns(0x00000002, &nmdk_clkevt);
	nmdk_clkevt.cpumask	= cpumask_of(0);

	/* Register irq and clockevents */
	setup_irq(IRQ_MTU0, &nmdk_timer_irq);
	clockevents_register_device(&nmdk_clkevt);
}
