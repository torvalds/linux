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
#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/sched/clock.h>
#include <linux/sched_clock.h>

#include <clocksource/pxa.h>

#include <asm/div64.h>

#define OSMR0		0x00	/* OS Timer 0 Match Register */
#define OSMR1		0x04	/* OS Timer 1 Match Register */
#define OSMR2		0x08	/* OS Timer 2 Match Register */
#define OSMR3		0x0C	/* OS Timer 3 Match Register */

#define OSCR		0x10	/* OS Timer Counter Register */
#define OSSR		0x14	/* OS Timer Status Register */
#define OWER		0x18	/* OS Timer Watchdog Enable Register */
#define OIER		0x1C	/* OS Timer Interrupt Enable Register */

#define OSSR_M3		(1 << 3)	/* Match status channel 3 */
#define OSSR_M2		(1 << 2)	/* Match status channel 2 */
#define OSSR_M1		(1 << 1)	/* Match status channel 1 */
#define OSSR_M0		(1 << 0)	/* Match status channel 0 */

#define OIER_E0		(1 << 0)	/* Interrupt enable channel 0 */

/*
 * This is PXA's sched_clock implementation. This has a resolution
 * of at least 308 ns and a maximum value of 208 days.
 *
 * The return value is guaranteed to be monotonic in that range as
 * long as there is always less than 582 seconds between successive
 * calls to sched_clock() which should always be the case in practice.
 */

#define timer_readl(reg) readl_relaxed(timer_base + (reg))
#define timer_writel(val, reg) writel_relaxed((val), timer_base + (reg))

static void __iomem *timer_base;

static u64 notrace pxa_read_sched_clock(void)
{
	return timer_readl(OSCR);
}


#define MIN_OSCR_DELTA 16

static irqreturn_t
pxa_ost0_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *c = dev_id;

	/* Disarm the compare/match, signal the event. */
	timer_writel(timer_readl(OIER) & ~OIER_E0, OIER);
	timer_writel(OSSR_M0, OSSR);
	c->event_handler(c);

	return IRQ_HANDLED;
}

static int
pxa_osmr0_set_next_event(unsigned long delta, struct clock_event_device *dev)
{
	unsigned long next, oscr;

	timer_writel(timer_readl(OIER) | OIER_E0, OIER);
	next = timer_readl(OSCR) + delta;
	timer_writel(next, OSMR0);
	oscr = timer_readl(OSCR);

	return (signed)(next - oscr) <= MIN_OSCR_DELTA ? -ETIME : 0;
}

static int pxa_osmr0_shutdown(struct clock_event_device *evt)
{
	/* initializing, released, or preparing for suspend */
	timer_writel(timer_readl(OIER) & ~OIER_E0, OIER);
	timer_writel(OSSR_M0, OSSR);
	return 0;
}

#ifdef CONFIG_PM
static unsigned long osmr[4], oier, oscr;

static void pxa_timer_suspend(struct clock_event_device *cedev)
{
	osmr[0] = timer_readl(OSMR0);
	osmr[1] = timer_readl(OSMR1);
	osmr[2] = timer_readl(OSMR2);
	osmr[3] = timer_readl(OSMR3);
	oier = timer_readl(OIER);
	oscr = timer_readl(OSCR);
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

	timer_writel(osmr[0], OSMR0);
	timer_writel(osmr[1], OSMR1);
	timer_writel(osmr[2], OSMR2);
	timer_writel(osmr[3], OSMR3);
	timer_writel(oier, OIER);
	timer_writel(oscr, OSCR);
}
#else
#define pxa_timer_suspend NULL
#define pxa_timer_resume NULL
#endif

static struct clock_event_device ckevt_pxa_osmr0 = {
	.name			= "osmr0",
	.features		= CLOCK_EVT_FEAT_ONESHOT,
	.rating			= 200,
	.set_next_event		= pxa_osmr0_set_next_event,
	.set_state_shutdown	= pxa_osmr0_shutdown,
	.set_state_oneshot	= pxa_osmr0_shutdown,
	.suspend		= pxa_timer_suspend,
	.resume			= pxa_timer_resume,
};

static struct irqaction pxa_ost0_irq = {
	.name		= "ost0",
	.flags		= IRQF_TIMER | IRQF_IRQPOLL,
	.handler	= pxa_ost0_interrupt,
	.dev_id		= &ckevt_pxa_osmr0,
};

static int __init pxa_timer_common_init(int irq, unsigned long clock_tick_rate)
{
	int ret;

	timer_writel(0, OIER);
	timer_writel(OSSR_M0 | OSSR_M1 | OSSR_M2 | OSSR_M3, OSSR);

	sched_clock_register(pxa_read_sched_clock, 32, clock_tick_rate);

	ckevt_pxa_osmr0.cpumask = cpumask_of(0);

	ret = setup_irq(irq, &pxa_ost0_irq);
	if (ret) {
		pr_err("Failed to setup irq\n");
		return ret;
	}

	ret = clocksource_mmio_init(timer_base + OSCR, "oscr0", clock_tick_rate, 200,
				    32, clocksource_mmio_readl_up);
	if (ret) {
		pr_err("Failed to init clocksource\n");
		return ret;
	}

	clockevents_config_and_register(&ckevt_pxa_osmr0, clock_tick_rate,
					MIN_OSCR_DELTA * 2, 0x7fffffff);

	return 0;
}

static int __init pxa_timer_dt_init(struct device_node *np)
{
	struct clk *clk;
	int irq, ret;

	/* timer registers are shared with watchdog timer */
	timer_base = of_iomap(np, 0);
	if (!timer_base) {
		pr_err("%s: unable to map resource\n", np->name);
		return -ENXIO;
	}

	clk = of_clk_get(np, 0);
	if (IS_ERR(clk)) {
		pr_crit("%s: unable to get clk\n", np->name);
		return PTR_ERR(clk);
	}

	ret = clk_prepare_enable(clk);
	if (ret) {
		pr_crit("Failed to prepare clock\n");
		return ret;
	}

	/* we are only interested in OS-timer0 irq */
	irq = irq_of_parse_and_map(np, 0);
	if (irq <= 0) {
		pr_crit("%s: unable to parse OS-timer0 irq\n", np->name);
		return -EINVAL;
	}

	return pxa_timer_common_init(irq, clk_get_rate(clk));
}
TIMER_OF_DECLARE(pxa_timer, "marvell,pxa-timer", pxa_timer_dt_init);

/*
 * Legacy timer init for non device-tree boards.
 */
void __init pxa_timer_nodt_init(int irq, void __iomem *base)
{
	struct clk *clk;

	timer_base = base;
	clk = clk_get(NULL, "OSTIMER0");
	if (clk && !IS_ERR(clk)) {
		clk_prepare_enable(clk);
		pxa_timer_common_init(irq, clk_get_rate(clk));
	} else {
		pr_crit("%s: unable to get clk\n", __func__);
	}
}
