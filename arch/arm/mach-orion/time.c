/*
 * arch/arm/mach-orion/time.c
 *
 * Core time functions for Marvell Orion System On Chip
 *
 * Maintainer: Tzachi Perelstein <tzachi@marvell.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <asm/mach/time.h>
#include <asm/arch/orion.h>
#include "common.h"

/*
 * Timer0: clock_event_device, Tick.
 * Timer1: clocksource, Free running.
 * WatchDog: Not used.
 *
 * Timers are counting down.
 */
#define CLOCKEVENT	0
#define CLOCKSOURCE	1

/*
 * Timers bits
 */
#define BRIDGE_INT_TIMER(x)	(1 << ((x) + 1))
#define TIMER_EN(x)		(1 << ((x) * 2))
#define TIMER_RELOAD_EN(x)	(1 << (((x) * 2) + 1))
#define BRIDGE_INT_TIMER_WD	(1 << 3)
#define TIMER_WD_EN		(1 << 4)
#define TIMER_WD_RELOAD_EN	(1 << 5)

static cycle_t orion_clksrc_read(void)
{
	return (0xffffffff - orion_read(TIMER_VAL(CLOCKSOURCE)));
}

static struct clocksource orion_clksrc = {
	.name		= "orion_clocksource",
	.shift		= 20,
	.rating		= 300,
	.read		= orion_clksrc_read,
	.mask		= CLOCKSOURCE_MASK(32),
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

static int
orion_clkevt_next_event(unsigned long delta, struct clock_event_device *dev)
{
	unsigned long flags;

	if (delta == 0)
		return -ETIME;

	local_irq_save(flags);

	/*
	 * Clear and enable timer interrupt bit
	 */
	orion_write(BRIDGE_CAUSE, ~BRIDGE_INT_TIMER(CLOCKEVENT));
	orion_setbits(BRIDGE_MASK, BRIDGE_INT_TIMER(CLOCKEVENT));

	/*
	 * Setup new timer value
	 */
	orion_write(TIMER_VAL(CLOCKEVENT), delta);

	/*
	 * Disable auto reload and kickoff the timer
	 */
	orion_clrbits(TIMER_CTRL, TIMER_RELOAD_EN(CLOCKEVENT));
	orion_setbits(TIMER_CTRL, TIMER_EN(CLOCKEVENT));

	local_irq_restore(flags);

	return 0;
}

static void
orion_clkevt_mode(enum clock_event_mode mode, struct clock_event_device *dev)
{
	unsigned long flags;

	local_irq_save(flags);

	if (mode == CLOCK_EVT_MODE_PERIODIC) {
		/*
		 * Setup latch cycles in timer and enable reload interrupt.
		 */
		orion_write(TIMER_VAL_RELOAD(CLOCKEVENT), LATCH);
		orion_write(TIMER_VAL(CLOCKEVENT), LATCH);
		orion_setbits(BRIDGE_MASK, BRIDGE_INT_TIMER(CLOCKEVENT));
		orion_setbits(TIMER_CTRL, TIMER_RELOAD_EN(CLOCKEVENT) |
					  TIMER_EN(CLOCKEVENT));
	} else {
		/*
		 * Disable timer and interrupt
		 */
		orion_clrbits(BRIDGE_MASK, BRIDGE_INT_TIMER(CLOCKEVENT));
		orion_write(BRIDGE_CAUSE, ~BRIDGE_INT_TIMER(CLOCKEVENT));
		orion_clrbits(TIMER_CTRL, TIMER_RELOAD_EN(CLOCKEVENT) |
					  TIMER_EN(CLOCKEVENT));
	}

	local_irq_restore(flags);
}

static struct clock_event_device orion_clkevt = {
	.name		= "orion_tick",
	.features	= CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
	.shift		= 32,
	.rating		= 300,
	.cpumask	= CPU_MASK_CPU0,
	.set_next_event	= orion_clkevt_next_event,
	.set_mode	= orion_clkevt_mode,
};

static irqreturn_t orion_timer_interrupt(int irq, void *dev_id)
{
	/*
	 * Clear cause bit and do event
	 */
	orion_write(BRIDGE_CAUSE, ~BRIDGE_INT_TIMER(CLOCKEVENT));
	orion_clkevt.event_handler(&orion_clkevt);
	return IRQ_HANDLED;
}

static struct irqaction orion_timer_irq = {
	.name		= "orion_tick",
	.flags		= IRQF_DISABLED | IRQF_TIMER,
	.handler	= orion_timer_interrupt
};

static void orion_timer_init(void)
{
	/*
	 * Setup clocksource free running timer (no interrupt on reload)
	 */
	orion_write(TIMER_VAL(CLOCKSOURCE), 0xffffffff);
	orion_write(TIMER_VAL_RELOAD(CLOCKSOURCE), 0xffffffff);
	orion_clrbits(BRIDGE_MASK, BRIDGE_INT_TIMER(CLOCKSOURCE));
	orion_setbits(TIMER_CTRL, TIMER_RELOAD_EN(CLOCKSOURCE) |
				  TIMER_EN(CLOCKSOURCE));

	/*
	 * Register clocksource
	 */
	orion_clksrc.mult =
		clocksource_hz2mult(CLOCK_TICK_RATE, orion_clksrc.shift);

	clocksource_register(&orion_clksrc);

	/*
	 * Connect and enable tick handler
	 */
	setup_irq(IRQ_ORION_BRIDGE, &orion_timer_irq);

	/*
	 * Register clockevent
	 */
	orion_clkevt.mult =
		div_sc(CLOCK_TICK_RATE, NSEC_PER_SEC, orion_clkevt.shift);
	orion_clkevt.max_delta_ns =
		clockevent_delta2ns(0xfffffffe, &orion_clkevt);
	orion_clkevt.min_delta_ns =
		clockevent_delta2ns(1, &orion_clkevt);

	clockevents_register_device(&orion_clkevt);
}

struct sys_timer orion_timer = {
	.init = orion_timer_init,
};
