/*
 * linux/arch/unicore32/kernel/time.c
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 *	Maintained by GUAN Xue-tao <gxt@mprc.pku.edu.cn>
 *	Copyright (C) 2001-2010 Guan Xuetao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/timex.h>
#include <linux/clockchips.h>

#include <mach/hardware.h>

#define MIN_OSCR_DELTA 2

static irqreturn_t puv3_ost0_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *c = dev_id;

	/* Disarm the compare/match, signal the event. */
	OST_OIER &= ~OST_OIER_E0;
	OST_OSSR &= ~OST_OSSR_M0;
	c->event_handler(c);

	return IRQ_HANDLED;
}

static int
puv3_osmr0_set_next_event(unsigned long delta, struct clock_event_device *c)
{
	unsigned long next, oscr;

	OST_OIER |= OST_OIER_E0;
	next = OST_OSCR + delta;
	OST_OSMR0 = next;
	oscr = OST_OSCR;

	return (signed)(next - oscr) <= MIN_OSCR_DELTA ? -ETIME : 0;
}

static void
puv3_osmr0_set_mode(enum clock_event_mode mode, struct clock_event_device *c)
{
	switch (mode) {
	case CLOCK_EVT_MODE_ONESHOT:
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
		OST_OIER &= ~OST_OIER_E0;
		OST_OSSR &= ~OST_OSSR_M0;
		break;

	case CLOCK_EVT_MODE_RESUME:
	case CLOCK_EVT_MODE_PERIODIC:
		break;
	}
}

static struct clock_event_device ckevt_puv3_osmr0 = {
	.name		= "osmr0",
	.features	= CLOCK_EVT_FEAT_ONESHOT,
#ifdef CONFIG_ARCH_FPGA
	.shift		= 18, /* correct shift val: 16, but warn_on_slowpath */
#else
	.shift          = 30,
#endif
	.rating		= 200,
	.set_next_event	= puv3_osmr0_set_next_event,
	.set_mode	= puv3_osmr0_set_mode,
};

static cycle_t puv3_read_oscr(struct clocksource *cs)
{
	return OST_OSCR;
}

static struct clocksource cksrc_puv3_oscr = {
	.name		= "oscr",
	.rating		= 200,
	.read		= puv3_read_oscr,
	.mask		= CLOCKSOURCE_MASK(32),
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

static struct irqaction puv3_timer_irq = {
	.name		= "ost0",
	.flags		= IRQF_DISABLED | IRQF_TIMER | IRQF_IRQPOLL,
	.handler	= puv3_ost0_interrupt,
	.dev_id		= &ckevt_puv3_osmr0,
};

void __init time_init(void)
{
	OST_OIER = 0;		/* disable any timer interrupts */
	OST_OSSR = 0;		/* clear status on all timers */

	ckevt_puv3_osmr0.mult =
		div_sc(CLOCK_TICK_RATE, NSEC_PER_SEC, ckevt_puv3_osmr0.shift);
	ckevt_puv3_osmr0.max_delta_ns =
		clockevent_delta2ns(0x7fffffff, &ckevt_puv3_osmr0);
	ckevt_puv3_osmr0.min_delta_ns =
		clockevent_delta2ns(MIN_OSCR_DELTA * 2, &ckevt_puv3_osmr0) + 1;
	ckevt_puv3_osmr0.cpumask = cpumask_of(0);

	setup_irq(IRQ_TIMER0, &puv3_timer_irq);

	clocksource_register_hz(&cksrc_puv3_oscr, CLOCK_TICK_RATE);
	clockevents_register_device(&ckevt_puv3_osmr0);
}

#ifdef CONFIG_PM
unsigned long osmr[4], oier;

void puv3_timer_suspend(void)
{
	osmr[0] = OST_OSMR0;
	osmr[1] = OST_OSMR1;
	osmr[2] = OST_OSMR2;
	osmr[3] = OST_OSMR3;
	oier = OST_OIER;
}

void puv3_timer_resume(void)
{
	OST_OSSR = 0;
	OST_OSMR0 = osmr[0];
	OST_OSMR1 = osmr[1];
	OST_OSMR2 = osmr[2];
	OST_OSMR3 = osmr[3];
	OST_OIER = oier;

	/*
	 * OSMR0 is the system timer: make sure OSCR is sufficiently behind
	 */
	OST_OSCR = OST_OSMR0 - LATCH;
}
#else
void puv3_timer_suspend(void) { };
void puv3_timer_resume(void) { };
#endif

