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
#include <linux/sched.h>

#include <asm/div64.h>
#include <asm/cnt32_to_63.h>
#include <asm/mach/irq.h>
#include <asm/mach/time.h>
#include <asm/arch/pxa-regs.h>
#include <asm/mach-types.h>

/*
 * This is PXA's sched_clock implementation. This has a resolution
 * of at least 308 ns and a maximum value of 208 days.
 *
 * The return value is guaranteed to be monotonic in that range as
 * long as there is always less than 582 seconds between successive
 * calls to sched_clock() which should always be the case in practice.
 */

#define OSCR2NS_SCALE_FACTOR 10

static unsigned long oscr2ns_scale;

static void __init set_oscr2ns_scale(unsigned long oscr_rate)
{
	unsigned long long v = 1000000000ULL << OSCR2NS_SCALE_FACTOR;
	do_div(v, oscr_rate);
	oscr2ns_scale = v;
	/*
	 * We want an even value to automatically clear the top bit
	 * returned by cnt32_to_63() without an additional run time
	 * instruction. So if the LSB is 1 then round it up.
	 */
	if (oscr2ns_scale & 1)
		oscr2ns_scale++;
}

unsigned long long sched_clock(void)
{
	unsigned long long v = cnt32_to_63(OSCR);
	return (v * oscr2ns_scale) >> OSCR2NS_SCALE_FACTOR;
}


static irqreturn_t
pxa_ost0_interrupt(int irq, void *dev_id)
{
	int next_match;
	struct clock_event_device *c = dev_id;

	if (c->mode == CLOCK_EVT_MODE_ONESHOT) {
		/* Disarm the compare/match, signal the event. */
		OIER &= ~OIER_E0;
		c->event_handler(c);
	} else if (c->mode == CLOCK_EVT_MODE_PERIODIC) {
		/* Call the event handler as many times as necessary
		 * to recover missed events, if any (if we update
		 * OSMR0 and OSCR0 is still ahead of us, we've missed
		 * the event).  As we're dealing with that, re-arm the
		 * compare/match for the next event.
		 *
		 * HACK ALERT:
		 *
		 * There's a latency between the instruction that
		 * writes to OSMR0 and the actual commit to the
		 * physical hardware, because the CPU doesn't (have
		 * to) run at bus speed, there's a write buffer
		 * between the CPU and the bus, etc. etc.  So if the
		 * target OSCR0 is "very close", to the OSMR0 load
		 * value, the update to OSMR0 might not get to the
		 * hardware in time and we'll miss that interrupt.
		 *
		 * To be safe, if the new OSMR0 is "very close" to the
		 * target OSCR0 value, we call the event_handler as
		 * though the event actually happened.  According to
		 * Nico's comment in the previous version of this
		 * code, experience has shown that 6 OSCR ticks is
		 * "very close" but he went with 8.  We will use 16,
		 * based on the results of testing on PXA270.
		 *
		 * To be doubly sure, we also tell clkevt via
		 * clockevents_register_device() not to ask for
		 * anything that might put us "very close".
	 */
#define MIN_OSCR_DELTA 16
	do {
			OSSR = OSSR_M0;
		next_match = (OSMR0 += LATCH);
			c->event_handler(c);
		} while (((signed long)(next_match - OSCR) <= MIN_OSCR_DELTA)
			 && (c->mode == CLOCK_EVT_MODE_PERIODIC));
	}

	return IRQ_HANDLED;
}

static int
pxa_osmr0_set_next_event(unsigned long delta, struct clock_event_device *dev)
{
	unsigned long irqflags;

	raw_local_irq_save(irqflags);
	OSMR0 = OSCR + delta;
	OSSR = OSSR_M0;
	OIER |= OIER_E0;
	raw_local_irq_restore(irqflags);
	return 0;
}

static void
pxa_osmr0_set_mode(enum clock_event_mode mode, struct clock_event_device *dev)
{
	unsigned long irqflags;

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		raw_local_irq_save(irqflags);
		OSMR0 = OSCR + LATCH;
		OSSR = OSSR_M0;
		OIER |= OIER_E0;
		raw_local_irq_restore(irqflags);
		break;

	case CLOCK_EVT_MODE_ONESHOT:
		raw_local_irq_save(irqflags);
		OIER &= ~OIER_E0;
		raw_local_irq_restore(irqflags);
		break;

	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
		/* initializing, released, or preparing for suspend */
		raw_local_irq_save(irqflags);
		OIER &= ~OIER_E0;
		raw_local_irq_restore(irqflags);
		break;
	}
}

static struct clock_event_device ckevt_pxa_osmr0 = {
	.name		= "osmr0",
	.features	= CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
	.shift		= 32,
	.rating		= 200,
	.cpumask	= CPU_MASK_CPU0,
	.set_next_event	= pxa_osmr0_set_next_event,
	.set_mode	= pxa_osmr0_set_mode,
};

static cycle_t pxa_read_oscr(void)
{
	return OSCR;
}

static struct clocksource cksrc_pxa_oscr0 = {
	.name           = "oscr0",
	.rating         = 200,
	.read           = pxa_read_oscr,
	.mask           = CLOCKSOURCE_MASK(32),
	.shift          = 20,
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

static struct irqaction pxa_ost0_irq = {
	.name		= "ost0",
	.flags		= IRQF_DISABLED | IRQF_TIMER | IRQF_IRQPOLL,
	.handler	= pxa_ost0_interrupt,
	.dev_id		= &ckevt_pxa_osmr0,
};

static void __init pxa_timer_init(void)
{
	unsigned long clock_tick_rate;

	OIER = 0;
	OSSR = OSSR_M0 | OSSR_M1 | OSSR_M2 | OSSR_M3;

	if (cpu_is_pxa21x() || cpu_is_pxa25x())
		clock_tick_rate = 3686400;
	else if (machine_is_mainstone())
		clock_tick_rate = 3249600;
	else
		clock_tick_rate = 3250000;

	set_oscr2ns_scale(clock_tick_rate);

	ckevt_pxa_osmr0.mult =
		div_sc(clock_tick_rate, NSEC_PER_SEC, ckevt_pxa_osmr0.shift);
	ckevt_pxa_osmr0.max_delta_ns =
		clockevent_delta2ns(0x7fffffff, &ckevt_pxa_osmr0);
	ckevt_pxa_osmr0.min_delta_ns =
		clockevent_delta2ns(MIN_OSCR_DELTA, &ckevt_pxa_osmr0) + 1;

	cksrc_pxa_oscr0.mult =
		clocksource_hz2mult(clock_tick_rate, cksrc_pxa_oscr0.shift);

	setup_irq(IRQ_OST0, &pxa_ost0_irq);

	clocksource_register(&cksrc_pxa_oscr0);
	clockevents_register_device(&ckevt_pxa_osmr0);
}

#ifdef CONFIG_PM
static unsigned long osmr[4], oier;

static void pxa_timer_suspend(void)
{
	osmr[0] = OSMR0;
	osmr[1] = OSMR1;
	osmr[2] = OSMR2;
	osmr[3] = OSMR3;
	oier = OIER;
}

static void pxa_timer_resume(void)
{
	OSMR0 = osmr[0];
	OSMR1 = osmr[1];
	OSMR2 = osmr[2];
	OSMR3 = osmr[3];
	OIER = oier;

	/*
	 * OSCR0 is the system timer, which has to increase
	 * monotonically until it rolls over in hardware.  The value
	 * (OSMR0 - LATCH) is OSCR0 at the most recent system tick,
	 * which is a handy value to restore to OSCR0.
	 */
	OSCR = OSMR0 - LATCH;
}
#else
#define pxa_timer_suspend NULL
#define pxa_timer_resume NULL
#endif

struct sys_timer pxa_timer = {
	.init		= pxa_timer_init,
	.suspend	= pxa_timer_suspend,
	.resume		= pxa_timer_resume,
};
