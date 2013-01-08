/*
 * arch/arm/mach-pxa/time.c
 *
 * PXA clocksource, clockevents, and OST interrupt handlers.
 * Copyright (c) 2007 by Bill Gatliff <bgat@billgatliff.com>.
 *
 * Derived from Nicolas Pitre's PXA timer handler Copyright (c) 2001
 * by MontaVista Software, Inc.  (Nico, your code rocks!)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/clockchips.h>

#include <asm/div64.h>
#include <asm/mach/irq.h>
#include <asm/mach/time.h>
#include <asm/sched_clock.h>
#include <mach/regs-ost.h>
#include <mach/irqs.h>

/*
 * This is PXA's sched_clock implementation. This has a resolution
 * of at least 308 ns and a maximum value of 208 days.
 *
 * The return value is guaranteed to be monotonic in that range as
 * long as there is always less than 582 seconds between successive
 * calls to sched_clock() which should always be the case in practice.
 */

static u32 notrace pxa_read_sched_clock(void)
{
	return readl_relaxed(OSCR);
}


#define MIN_OSCR_DELTA 16

static irqreturn_t
pxa_ost0_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *c = dev_id;

	/* Disarm the compare/match, signal the event. */
	writel_relaxed(readl_relaxed(OIER) & ~OIER_E0, OIER);
	writel_relaxed(OSSR_M0, OSSR);
	c->event_handler(c);

	return IRQ_HANDLED;
}

static int
pxa_osmr0_set_next_event(unsigned long delta, struct clock_event_device *dev)
{
	unsigned long next, oscr;

	writel_relaxed(readl_relaxed(OIER) | OIER_E0, OIER);
	next = readl_relaxed(OSCR) + delta;
	writel_relaxed(next, OSMR0);
	oscr = readl_relaxed(OSCR);

	return (signed)(next - oscr) <= MIN_OSCR_DELTA ? -ETIME : 0;
}

static void
pxa_osmr0_set_mode(enum clock_event_mode mode, struct clock_event_device *dev)
{
	switch (mode) {
	case CLOCK_EVT_MODE_ONESHOT:
		writel_relaxed(readl_relaxed(OIER) & ~OIER_E0, OIER);
		writel_relaxed(OSSR_M0, OSSR);
		break;

	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
		/* initializing, released, or preparing for suspend */
		writel_relaxed(readl_relaxed(OIER) & ~OIER_E0, OIER);
		writel_relaxed(OSSR_M0, OSSR);
		break;

	case CLOCK_EVT_MODE_RESUME:
	case CLOCK_EVT_MODE_PERIODIC:
		break;
	}
}

#ifdef CONFIG_PM
static unsigned long osmr[4], oier, oscr;

static void pxa_timer_suspend(struct clock_event_device *cedev)
{
	osmr[0] = readl_relaxed(OSMR0);
	osmr[1] = readl_relaxed(OSMR1);
	osmr[2] = readl_relaxed(OSMR2);
	osmr[3] = readl_relaxed(OSMR3);
	oier = readl_relaxed(OIER);
	oscr = readl_relaxed(OSCR);
}

static void pxa_timer_resume(struct clock_event_device *cedev)
{
	/*
	 * Ensure that we have at least MIN_OSCR_DELTA between match
	 * register 0 and the OSCR, to guarantee that we will receive
	 * the one-shot timer interrupt.  We adjust OSMR0 in preference
	 * to OSCR to guarantee that OSCR is monotonically incrementing.
	 */
	if (osmr[0] - oscr < MIN_OSCR_DELTA)
		osmr[0] += MIN_OSCR_DELTA;

	writel_relaxed(osmr[0], OSMR0);
	writel_relaxed(osmr[1], OSMR1);
	writel_relaxed(osmr[2], OSMR2);
	writel_relaxed(osmr[3], OSMR3);
	writel_relaxed(oier, OIER);
	writel_relaxed(oscr, OSCR);
}
#else
#define pxa_timer_suspend NULL
#define pxa_timer_resume NULL
#endif

static struct clock_event_device ckevt_pxa_osmr0 = {
	.name		= "osmr0",
	.features	= CLOCK_EVT_FEAT_ONESHOT,
	.rating		= 200,
	.set_next_event	= pxa_osmr0_set_next_event,
	.set_mode	= pxa_osmr0_set_mode,
	.suspend	= pxa_timer_suspend,
	.resume		= pxa_timer_resume,
};

static struct irqaction pxa_ost0_irq = {
	.name		= "ost0",
	.flags		= IRQF_DISABLED | IRQF_TIMER | IRQF_IRQPOLL,
	.handler	= pxa_ost0_interrupt,
	.dev_id		= &ckevt_pxa_osmr0,
};

void __init pxa_timer_init(void)
{
	unsigned long clock_tick_rate = get_clock_tick_rate();

	writel_relaxed(0, OIER);
	writel_relaxed(OSSR_M0 | OSSR_M1 | OSSR_M2 | OSSR_M3, OSSR);

	setup_sched_clock(pxa_read_sched_clock, 32, clock_tick_rate);

	clockevents_calc_mult_shift(&ckevt_pxa_osmr0, clock_tick_rate, 4);
	ckevt_pxa_osmr0.max_delta_ns =
		clockevent_delta2ns(0x7fffffff, &ckevt_pxa_osmr0);
	ckevt_pxa_osmr0.min_delta_ns =
		clockevent_delta2ns(MIN_OSCR_DELTA * 2, &ckevt_pxa_osmr0) + 1;
	ckevt_pxa_osmr0.cpumask = cpumask_of(0);

	setup_irq(IRQ_OST0, &pxa_ost0_irq);

	clocksource_mmio_init(OSCR, "oscr0", clock_tick_rate, 200, 32,
		clocksource_mmio_readl_up);
	clockevents_register_device(&ckevt_pxa_osmr0);
}
