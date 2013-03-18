/*
 * linux/arch/arm/mach-sa1100/time.c
 *
 * Copyright (C) 1998 Deborah Wallach.
 * Twiddles  (C) 1999 Hugo Fiennes <hugo@empeg.com>
 *
 * 2000/03/29 (C) Nicolas Pitre <nico@fluxnic.net>
 *	Rewritten: big cleanup, much simpler, better HZ accuracy.
 *
 */
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/timex.h>
#include <linux/clockchips.h>

#include <asm/mach/time.h>
#include <asm/sched_clock.h>
#include <mach/hardware.h>
#include <mach/irqs.h>

static u32 notrace sa1100_read_sched_clock(void)
{
	return readl_relaxed(OSCR);
}

#define MIN_OSCR_DELTA 2

static irqreturn_t sa1100_ost0_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *c = dev_id;

	/* Disarm the compare/match, signal the event. */
	writel_relaxed(readl_relaxed(OIER) & ~OIER_E0, OIER);
	writel_relaxed(OSSR_M0, OSSR);
	c->event_handler(c);

	return IRQ_HANDLED;
}

static int
sa1100_osmr0_set_next_event(unsigned long delta, struct clock_event_device *c)
{
	unsigned long next, oscr;

	writel_relaxed(readl_relaxed(OIER) | OIER_E0, OIER);
	next = readl_relaxed(OSCR) + delta;
	writel_relaxed(next, OSMR0);
	oscr = readl_relaxed(OSCR);

	return (signed)(next - oscr) <= MIN_OSCR_DELTA ? -ETIME : 0;
}

static void
sa1100_osmr0_set_mode(enum clock_event_mode mode, struct clock_event_device *c)
{
	switch (mode) {
	case CLOCK_EVT_MODE_ONESHOT:
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
		writel_relaxed(readl_relaxed(OIER) & ~OIER_E0, OIER);
		writel_relaxed(OSSR_M0, OSSR);
		break;

	case CLOCK_EVT_MODE_RESUME:
	case CLOCK_EVT_MODE_PERIODIC:
		break;
	}
}

#ifdef CONFIG_PM
unsigned long osmr[4], oier;

static void sa1100_timer_suspend(struct clock_event_device *cedev)
{
	osmr[0] = readl_relaxed(OSMR0);
	osmr[1] = readl_relaxed(OSMR1);
	osmr[2] = readl_relaxed(OSMR2);
	osmr[3] = readl_relaxed(OSMR3);
	oier = readl_relaxed(OIER);
}

static void sa1100_timer_resume(struct clock_event_device *cedev)
{
	writel_relaxed(0x0f, OSSR);
	writel_relaxed(osmr[0], OSMR0);
	writel_relaxed(osmr[1], OSMR1);
	writel_relaxed(osmr[2], OSMR2);
	writel_relaxed(osmr[3], OSMR3);
	writel_relaxed(oier, OIER);

	/*
	 * OSMR0 is the system timer: make sure OSCR is sufficiently behind
	 */
	writel_relaxed(OSMR0 - LATCH, OSCR);
}
#else
#define sa1100_timer_suspend NULL
#define sa1100_timer_resume NULL
#endif

static struct clock_event_device ckevt_sa1100_osmr0 = {
	.name		= "osmr0",
	.features	= CLOCK_EVT_FEAT_ONESHOT,
	.rating		= 200,
	.set_next_event	= sa1100_osmr0_set_next_event,
	.set_mode	= sa1100_osmr0_set_mode,
	.suspend	= sa1100_timer_suspend,
	.resume		= sa1100_timer_resume,
};

static struct irqaction sa1100_timer_irq = {
	.name		= "ost0",
	.flags		= IRQF_DISABLED | IRQF_TIMER | IRQF_IRQPOLL,
	.handler	= sa1100_ost0_interrupt,
	.dev_id		= &ckevt_sa1100_osmr0,
};

void __init sa1100_timer_init(void)
{
	writel_relaxed(0, OIER);
	writel_relaxed(OSSR_M0 | OSSR_M1 | OSSR_M2 | OSSR_M3, OSSR);

	setup_sched_clock(sa1100_read_sched_clock, 32, 3686400);

	ckevt_sa1100_osmr0.cpumask = cpumask_of(0);

	setup_irq(IRQ_OST0, &sa1100_timer_irq);

	clocksource_mmio_init(OSCR, "oscr", CLOCK_TICK_RATE, 200, 32,
		clocksource_mmio_readl_up);
	clockevents_config_and_register(&ckevt_sa1100_osmr0, 3686400,
					MIN_OSCR_DELTA * 2, 0x7fffffff);
}
