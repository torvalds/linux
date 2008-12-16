/*
 *  linux/arch/arm/plat-mxc/time.c
 *
 *  Copyright (C) 2000-2001 Deep Blue Solutions
 *  Copyright (C) 2002 Shane Nay (shane@minirl.com)
 *  Copyright (C) 2006-2007 Pavel Pisa (ppisa@pikron.com)
 *  Copyright (C) 2008 Juergen Beisert (kernel@pengutronix.de)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/clockchips.h>
#include <linux/clk.h>

#include <mach/hardware.h>
#include <asm/mach/time.h>
#include <mach/common.h>
#include <mach/mxc_timer.h>

static struct clock_event_device clockevent_mxc;
static enum clock_event_mode clockevent_mode = CLOCK_EVT_MODE_UNUSED;

/* clock source for the timer */
static struct clk *timer_clk;

/* clock source */

static cycle_t mxc_get_cycles(void)
{
	return __raw_readl(TIMER_BASE + MXC_TCN);
}

static struct clocksource clocksource_mxc = {
	.name 		= "mxc_timer1",
	.rating		= 200,
	.read		= mxc_get_cycles,
	.mask		= CLOCKSOURCE_MASK(32),
	.shift 		= 20,
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

static int __init mxc_clocksource_init(void)
{
	unsigned int clock;

	clock = clk_get_rate(timer_clk);

	clocksource_mxc.mult = clocksource_hz2mult(clock,
					clocksource_mxc.shift);
	clocksource_register(&clocksource_mxc);

	return 0;
}

/* clock event */

static int mxc_set_next_event(unsigned long evt,
			      struct clock_event_device *unused)
{
	unsigned long tcmp;

	tcmp = __raw_readl(TIMER_BASE + MXC_TCN) + evt;
	__raw_writel(tcmp, TIMER_BASE + MXC_TCMP);

	return (int)(tcmp - __raw_readl(TIMER_BASE + MXC_TCN)) < 0 ?
				-ETIME : 0;
}

#ifdef DEBUG
static const char *clock_event_mode_label[] = {
	[CLOCK_EVT_MODE_PERIODIC] = "CLOCK_EVT_MODE_PERIODIC",
	[CLOCK_EVT_MODE_ONESHOT]  = "CLOCK_EVT_MODE_ONESHOT",
	[CLOCK_EVT_MODE_SHUTDOWN] = "CLOCK_EVT_MODE_SHUTDOWN",
	[CLOCK_EVT_MODE_UNUSED]   = "CLOCK_EVT_MODE_UNUSED"
};
#endif /* DEBUG */

static void mxc_set_mode(enum clock_event_mode mode,
				struct clock_event_device *evt)
{
	unsigned long flags;

	/*
	 * The timer interrupt generation is disabled at least
	 * for enough time to call mxc_set_next_event()
	 */
	local_irq_save(flags);

	/* Disable interrupt in GPT module */
	gpt_irq_disable();

	if (mode != clockevent_mode) {
		/* Set event time into far-far future */
		__raw_writel(__raw_readl(TIMER_BASE + MXC_TCN) - 3,
				TIMER_BASE + MXC_TCMP);
		/* Clear pending interrupt */
		gpt_irq_acknowledge();
	}

#ifdef DEBUG
	printk(KERN_INFO "mxc_set_mode: changing mode from %s to %s\n",
		clock_event_mode_label[clockevent_mode],
		clock_event_mode_label[mode]);
#endif /* DEBUG */

	/* Remember timer mode */
	clockevent_mode = mode;
	local_irq_restore(flags);

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		printk(KERN_ERR"mxc_set_mode: Periodic mode is not "
				"supported for i.MX\n");
		break;
	case CLOCK_EVT_MODE_ONESHOT:
	/*
	 * Do not put overhead of interrupt enable/disable into
	 * mxc_set_next_event(), the core has about 4 minutes
	 * to call mxc_set_next_event() or shutdown clock after
	 * mode switching
	 */
		local_irq_save(flags);
		gpt_irq_enable();
		local_irq_restore(flags);
		break;
	case CLOCK_EVT_MODE_SHUTDOWN:
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_RESUME:
		/* Left event sources disabled, no more interrupts appear */
		break;
	}
}

/*
 * IRQ handler for the timer
 */
static irqreturn_t mxc_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = &clockevent_mxc;
	uint32_t tstat;

	tstat = __raw_readl(TIMER_BASE + MXC_TSTAT);

	gpt_irq_acknowledge();

	evt->event_handler(evt);

	return IRQ_HANDLED;
}

static struct irqaction mxc_timer_irq = {
	.name		= "i.MX Timer Tick",
	.flags		= IRQF_DISABLED | IRQF_TIMER | IRQF_IRQPOLL,
	.handler	= mxc_timer_interrupt,
};

static struct clock_event_device clockevent_mxc = {
	.name		= "mxc_timer1",
	.features	= CLOCK_EVT_FEAT_ONESHOT,
	.shift		= 32,
	.set_mode	= mxc_set_mode,
	.set_next_event	= mxc_set_next_event,
	.rating		= 200,
};

static int __init mxc_clockevent_init(void)
{
	unsigned int clock;

	clock = clk_get_rate(timer_clk);

	clockevent_mxc.mult = div_sc(clock, NSEC_PER_SEC,
					clockevent_mxc.shift);
	clockevent_mxc.max_delta_ns =
			clockevent_delta2ns(0xfffffffe, &clockevent_mxc);
	clockevent_mxc.min_delta_ns =
			clockevent_delta2ns(0xff, &clockevent_mxc);

	clockevent_mxc.cpumask = cpumask_of(0);

	clockevents_register_device(&clockevent_mxc);

	return 0;
}

void __init mxc_timer_init(const char *clk_timer)
{
	timer_clk = clk_get(NULL, clk_timer);
	if (!timer_clk) {
		printk(KERN_ERR"Cannot determine timer clock. Giving up.\n");
		return;
	}

	clk_enable(timer_clk);

	/*
	 * Initialise to a known state (all timers off, and timing reset)
	 */
	__raw_writel(0, TIMER_BASE + MXC_TCTL);
	__raw_writel(0, TIMER_BASE + MXC_TPRER); /* see datasheet note */

	__raw_writel(TCTL_FRR |	/* free running */
		     TCTL_VAL |	/* set clocksource and arch specific bits */
		     TCTL_TEN,	/* start the timer */
		     TIMER_BASE + MXC_TCTL);

	/* init and register the timer to the framework */
	mxc_clocksource_init();
	mxc_clockevent_init();

	/* Make irqs happen */
	setup_irq(TIMER_INTERRUPT, &mxc_timer_irq);
}

