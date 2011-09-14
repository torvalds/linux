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

static bool clkevt_periodic;
static u32 clk_prescale;
static u32 nmdk_cycle;		/* write-once */

void __iomem *mtu_base; /* Assigned by machine code */

#ifdef CONFIG_NOMADIK_MTU_SCHED_CLOCK
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
#endif

/* Clockevent device: use one-shot mode */
static int nmdk_clkevt_next(unsigned long evt, struct clock_event_device *ev)
{
	writel(1 << 1, mtu_base + MTU_IMSC);
	writel(evt, mtu_base + MTU_LR(1));
	/* Load highest value, enable device, enable interrupts */
	writel(MTU_CRn_ONESHOT | clk_prescale |
	       MTU_CRn_32BITS | MTU_CRn_ENA,
	       mtu_base + MTU_CR(1));

	return 0;
}

static void nmdk_clkevt_reset(void)
{
	if (clkevt_periodic) {

		/* Timer: configure load and background-load, and fire it up */
		writel(nmdk_cycle, mtu_base + MTU_LR(1));
		writel(nmdk_cycle, mtu_base + MTU_BGLR(1));

		writel(MTU_CRn_PERIODIC | clk_prescale |
		       MTU_CRn_32BITS | MTU_CRn_ENA,
		       mtu_base + MTU_CR(1));
		writel(1 << 1, mtu_base + MTU_IMSC);
	} else {
		/* Generate an interrupt to start the clockevent again */
		(void) nmdk_clkevt_next(nmdk_cycle, NULL);
	}
}

static void nmdk_clkevt_mode(enum clock_event_mode mode,
			     struct clock_event_device *dev)
{

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		clkevt_periodic = true;
		nmdk_clkevt_reset();
		break;
	case CLOCK_EVT_MODE_ONESHOT:
		clkevt_periodic = false;
		break;
	case CLOCK_EVT_MODE_SHUTDOWN:
	case CLOCK_EVT_MODE_UNUSED:
		writel(0, mtu_base + MTU_IMSC);
		/* disable timer */
		writel(0, mtu_base + MTU_CR(1));
		/* load some high default value */
		writel(0xffffffff, mtu_base + MTU_LR(1));
		break;
	case CLOCK_EVT_MODE_RESUME:
		break;
	}
}

static struct clock_event_device nmdk_clkevt = {
	.name		= "mtu_1",
	.features	= CLOCK_EVT_FEAT_ONESHOT | CLOCK_EVT_FEAT_PERIODIC,
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

static void nmdk_clksrc_reset(void)
{
	/* Disable */
	writel(0, mtu_base + MTU_CR(0));

	/* ClockSource: configure load and background-load, and fire it up */
	writel(nmdk_cycle, mtu_base + MTU_LR(0));
	writel(nmdk_cycle, mtu_base + MTU_BGLR(0));

	writel(clk_prescale | MTU_CRn_32BITS | MTU_CRn_ENA,
	       mtu_base + MTU_CR(0));
}

void __init nmdk_timer_init(void)
{
	unsigned long rate;
	struct clk *clk0;

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
		clk_prescale = MTU_CRn_PRESCALE_16;
	} else {
		clk_prescale = MTU_CRn_PRESCALE_1;
	}

	nmdk_cycle = (rate + HZ/2) / HZ;


	/* Timer 0 is the free running clocksource */
	nmdk_clksrc_reset();

	if (clocksource_mmio_init(mtu_base + MTU_VAL(0), "mtu_0",
			rate, 200, 32, clocksource_mmio_readl_down))
		pr_err("timer: failed to initialize clock source %s\n",
		       "mtu_0");
#ifdef CONFIG_NOMADIK_MTU_SCHED_CLOCK
	init_sched_clock(&cd, nomadik_update_sched_clock, 32, rate);
#endif
	/* Timer 1 is used for events */

	clockevents_calc_mult_shift(&nmdk_clkevt, rate, MTU_MIN_RANGE);

	nmdk_clkevt.max_delta_ns =
		clockevent_delta2ns(0xffffffff, &nmdk_clkevt);
	nmdk_clkevt.min_delta_ns =
		clockevent_delta2ns(0x00000002, &nmdk_clkevt);
	nmdk_clkevt.cpumask	= cpumask_of(0);

	/* Register irq and clockevents */
	setup_irq(IRQ_MTU0, &nmdk_timer_irq);
	clockevents_register_device(&nmdk_clkevt);
}
