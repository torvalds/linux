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
#include <linux/cnt32_to_63.h>
#include <linux/timer.h>
#include <asm/mach/time.h>

#include <plat/mtu.h>

void __iomem *mtu_base; /* Assigned by machine code */

/*
 * Kernel assumes that sched_clock can be called early
 * but the MTU may not yet be initialized.
 */
static cycle_t nmdk_read_timer_dummy(struct clocksource *cs)
{
	return 0;
}

/* clocksource: MTU decrements, so we negate the value being read. */
static cycle_t nmdk_read_timer(struct clocksource *cs)
{
	return -readl(mtu_base + MTU_VAL(0));
}

static struct clocksource nmdk_clksrc = {
	.name		= "mtu_0",
	.rating		= 200,
	.read		= nmdk_read_timer_dummy,
	.mask		= CLOCKSOURCE_MASK(32),
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

/*
 * Override the global weak sched_clock symbol with this
 * local implementation which uses the clocksource to get some
 * better resolution when scheduling the kernel.
 *
 * Because the hardware timer period may be quite short
 * (32.3 secs on the 133 MHz MTU timer selection on ux500)
 * and because cnt32_to_63() needs to be called at least once per
 * half period to work properly, a kernel keepwarm() timer is set up
 * to ensure this requirement is always met.
 *
 * Also the sched_clock timer will wrap around at some point,
 * here we set it to run continously for a year.
 */
#define SCHED_CLOCK_MIN_WRAP 3600*24*365
static struct timer_list cnt32_to_63_keepwarm_timer;
static u32 sched_mult;
static u32 sched_shift;

unsigned long long notrace sched_clock(void)
{
	u64 cycles;

	if (unlikely(!mtu_base))
		return 0;

	cycles = cnt32_to_63(-readl(mtu_base + MTU_VAL(0)));
	/*
	 * sched_mult is guaranteed to be even so will
	 * shift out bit 63
	 */
	return (cycles * sched_mult) >> sched_shift;
}

/* Just kick sched_clock every so often */
static void cnt32_to_63_keepwarm(unsigned long data)
{
	mod_timer(&cnt32_to_63_keepwarm_timer, round_jiffies(jiffies + data));
	(void) sched_clock();
}

/*
 * Set up a timer to keep sched_clock():s 32_to_63 algorithm warm
 * once in half a 32bit timer wrap interval.
 */
static void __init nmdk_sched_clock_init(unsigned long rate)
{
	u32 v;
	unsigned long delta;
	u64 days;

	/* Find the apropriate mult and shift factors */
	clocks_calc_mult_shift(&sched_mult, &sched_shift,
			       rate, NSEC_PER_SEC, SCHED_CLOCK_MIN_WRAP);
	/* We need to multiply by an even number to get rid of bit 63 */
	if (sched_mult & 1)
		sched_mult++;

	/* Let's see what we get, take max counter and scale it */
	days = (0xFFFFFFFFFFFFFFFFLLU * sched_mult) >> sched_shift;
	do_div(days, NSEC_PER_SEC);
	do_div(days, (3600*24));

	pr_info("sched_clock: using %d bits @ %lu Hz wrap in %lu days\n",
		(64 - sched_shift), rate, (unsigned long) days);

	/*
	 * Program a timer to kick us at half 32bit wraparound
	 * Formula: seconds per wrap = (2^32) / f
	 */
	v = 0xFFFFFFFFUL / rate;
	/* We want half of the wrap time to keep cnt32_to_63 warm */
	v /= 2;
	pr_debug("sched_clock: prescaled timer rate: %lu Hz, "
		 "initialize keepwarm timer every %d seconds\n", rate, v);
	/* Convert seconds to jiffies */
	delta = msecs_to_jiffies(v*1000);
	setup_timer(&cnt32_to_63_keepwarm_timer, cnt32_to_63_keepwarm, delta);
	mod_timer(&cnt32_to_63_keepwarm_timer, round_jiffies(jiffies + delta));
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
	clocksource_calc_mult_shift(&nmdk_clksrc, rate, MTU_MIN_RANGE);

	/* Timer 0 is the free running clocksource */
	writel(cr, mtu_base + MTU_CR(0));
	writel(0, mtu_base + MTU_LR(0));
	writel(0, mtu_base + MTU_BGLR(0));
	writel(cr | MTU_CRn_ENA, mtu_base + MTU_CR(0));

	/* Now the clock source is ready */
	nmdk_clksrc.read = nmdk_read_timer;

	if (clocksource_register(&nmdk_clksrc))
		pr_err("timer: failed to initialize clock source %s\n",
		       nmdk_clksrc.name);

	nmdk_sched_clock_init(rate);

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
