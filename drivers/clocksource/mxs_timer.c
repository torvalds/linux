/*
 *  Copyright (C) 2000-2001 Deep Blue Solutions
 *  Copyright (C) 2002 Shane Nay (shane@minirl.com)
 *  Copyright (C) 2006-2007 Pavel Pisa (ppisa@pikron.com)
 *  Copyright (C) 2008 Juergen Beisert (kernel@pengutronix.de)
 *  Copyright (C) 2010 Freescale Semiconductor, Inc. All Rights Reserved.
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

#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/clockchips.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/stmp_device.h>
#include <linux/sched_clock.h>

/*
 * There are 2 versions of the timrot on Freescale MXS-based SoCs.
 * The v1 on MX23 only gets 16 bits counter, while v2 on MX28
 * extends the counter to 32 bits.
 *
 * The implementation uses two timers, one for clock_event and
 * another for clocksource. MX28 uses timrot 0 and 1, while MX23
 * uses 0 and 2.
 */

#define MX23_TIMROT_VERSION_OFFSET	0x0a0
#define MX28_TIMROT_VERSION_OFFSET	0x120
#define BP_TIMROT_MAJOR_VERSION		24
#define BV_TIMROT_VERSION_1		0x01
#define BV_TIMROT_VERSION_2		0x02
#define timrot_is_v1()	(timrot_major_version == BV_TIMROT_VERSION_1)

/*
 * There are 4 registers for each timrotv2 instance, and 2 registers
 * for each timrotv1. So address step 0x40 in macros below strides
 * one instance of timrotv2 while two instances of timrotv1.
 *
 * As the result, HW_TIMROT_XXXn(1) defines the address of timrot1
 * on MX28 while timrot2 on MX23.
 */
/* common between v1 and v2 */
#define HW_TIMROT_ROTCTRL		0x00
#define HW_TIMROT_TIMCTRLn(n)		(0x20 + (n) * 0x40)
/* v1 only */
#define HW_TIMROT_TIMCOUNTn(n)		(0x30 + (n) * 0x40)
/* v2 only */
#define HW_TIMROT_RUNNING_COUNTn(n)	(0x30 + (n) * 0x40)
#define HW_TIMROT_FIXED_COUNTn(n)	(0x40 + (n) * 0x40)

#define BM_TIMROT_TIMCTRLn_RELOAD	(1 << 6)
#define BM_TIMROT_TIMCTRLn_UPDATE	(1 << 7)
#define BM_TIMROT_TIMCTRLn_IRQ_EN	(1 << 14)
#define BM_TIMROT_TIMCTRLn_IRQ		(1 << 15)
#define BP_TIMROT_TIMCTRLn_SELECT	0
#define BV_TIMROTv1_TIMCTRLn_SELECT__32KHZ_XTAL		0x8
#define BV_TIMROTv2_TIMCTRLn_SELECT__32KHZ_XTAL		0xb
#define BV_TIMROTv2_TIMCTRLn_SELECT__TICK_ALWAYS	0xf

static struct clock_event_device mxs_clockevent_device;

static void __iomem *mxs_timrot_base;
static u32 timrot_major_version;

static inline void timrot_irq_disable(void)
{
	__raw_writel(BM_TIMROT_TIMCTRLn_IRQ_EN, mxs_timrot_base +
		     HW_TIMROT_TIMCTRLn(0) + STMP_OFFSET_REG_CLR);
}

static inline void timrot_irq_enable(void)
{
	__raw_writel(BM_TIMROT_TIMCTRLn_IRQ_EN, mxs_timrot_base +
		     HW_TIMROT_TIMCTRLn(0) + STMP_OFFSET_REG_SET);
}

static void timrot_irq_acknowledge(void)
{
	__raw_writel(BM_TIMROT_TIMCTRLn_IRQ, mxs_timrot_base +
		     HW_TIMROT_TIMCTRLn(0) + STMP_OFFSET_REG_CLR);
}

static cycle_t timrotv1_get_cycles(struct clocksource *cs)
{
	return ~((__raw_readl(mxs_timrot_base + HW_TIMROT_TIMCOUNTn(1))
			& 0xffff0000) >> 16);
}

static int timrotv1_set_next_event(unsigned long evt,
					struct clock_event_device *dev)
{
	/* timrot decrements the count */
	__raw_writel(evt, mxs_timrot_base + HW_TIMROT_TIMCOUNTn(0));

	return 0;
}

static int timrotv2_set_next_event(unsigned long evt,
					struct clock_event_device *dev)
{
	/* timrot decrements the count */
	__raw_writel(evt, mxs_timrot_base + HW_TIMROT_FIXED_COUNTn(0));

	return 0;
}

static irqreturn_t mxs_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;

	timrot_irq_acknowledge();
	evt->event_handler(evt);

	return IRQ_HANDLED;
}

static struct irqaction mxs_timer_irq = {
	.name		= "MXS Timer Tick",
	.dev_id		= &mxs_clockevent_device,
	.flags		= IRQF_TIMER | IRQF_IRQPOLL,
	.handler	= mxs_timer_interrupt,
};

static void mxs_irq_clear(char *state)
{
	/* Disable interrupt in timer module */
	timrot_irq_disable();

	/* Set event time into the furthest future */
	if (timrot_is_v1())
		__raw_writel(0xffff, mxs_timrot_base + HW_TIMROT_TIMCOUNTn(1));
	else
		__raw_writel(0xffffffff,
			     mxs_timrot_base + HW_TIMROT_FIXED_COUNTn(1));

	/* Clear pending interrupt */
	timrot_irq_acknowledge();

#ifdef DEBUG
	pr_info("%s: changing mode to %s\n", __func__, state)
#endif /* DEBUG */
}

static int mxs_shutdown(struct clock_event_device *evt)
{
	mxs_irq_clear("shutdown");

	return 0;
}

static int mxs_set_oneshot(struct clock_event_device *evt)
{
	if (clockevent_state_oneshot(evt))
		mxs_irq_clear("oneshot");
	timrot_irq_enable();
	return 0;
}

static struct clock_event_device mxs_clockevent_device = {
	.name			= "mxs_timrot",
	.features		= CLOCK_EVT_FEAT_ONESHOT,
	.set_state_shutdown	= mxs_shutdown,
	.set_state_oneshot	= mxs_set_oneshot,
	.tick_resume		= mxs_shutdown,
	.set_next_event		= timrotv2_set_next_event,
	.rating			= 200,
};

static int __init mxs_clockevent_init(struct clk *timer_clk)
{
	if (timrot_is_v1())
		mxs_clockevent_device.set_next_event = timrotv1_set_next_event;
	mxs_clockevent_device.cpumask = cpumask_of(0);
	clockevents_config_and_register(&mxs_clockevent_device,
					clk_get_rate(timer_clk),
					timrot_is_v1() ? 0xf : 0x2,
					timrot_is_v1() ? 0xfffe : 0xfffffffe);

	return 0;
}

static struct clocksource clocksource_mxs = {
	.name		= "mxs_timer",
	.rating		= 200,
	.read		= timrotv1_get_cycles,
	.mask		= CLOCKSOURCE_MASK(16),
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

static u64 notrace mxs_read_sched_clock_v2(void)
{
	return ~readl_relaxed(mxs_timrot_base + HW_TIMROT_RUNNING_COUNTn(1));
}

static int __init mxs_clocksource_init(struct clk *timer_clk)
{
	unsigned int c = clk_get_rate(timer_clk);

	if (timrot_is_v1())
		clocksource_register_hz(&clocksource_mxs, c);
	else {
		clocksource_mmio_init(mxs_timrot_base + HW_TIMROT_RUNNING_COUNTn(1),
			"mxs_timer", c, 200, 32, clocksource_mmio_readl_down);
		sched_clock_register(mxs_read_sched_clock_v2, 32, c);
	}

	return 0;
}

static int __init mxs_timer_init(struct device_node *np)
{
	struct clk *timer_clk;
	int irq, ret;

	mxs_timrot_base = of_iomap(np, 0);
	WARN_ON(!mxs_timrot_base);

	timer_clk = of_clk_get(np, 0);
	if (IS_ERR(timer_clk)) {
		pr_err("%s: failed to get clk\n", __func__);
		return PTR_ERR(timer_clk);
	}

	ret = clk_prepare_enable(timer_clk);
	if (ret)
		return ret;

	/*
	 * Initialize timers to a known state
	 */
	stmp_reset_block(mxs_timrot_base + HW_TIMROT_ROTCTRL);

	/* get timrot version */
	timrot_major_version = __raw_readl(mxs_timrot_base +
			(of_device_is_compatible(np, "fsl,imx23-timrot") ?
						MX23_TIMROT_VERSION_OFFSET :
						MX28_TIMROT_VERSION_OFFSET));
	timrot_major_version >>= BP_TIMROT_MAJOR_VERSION;

	/* one for clock_event */
	__raw_writel((timrot_is_v1() ?
			BV_TIMROTv1_TIMCTRLn_SELECT__32KHZ_XTAL :
			BV_TIMROTv2_TIMCTRLn_SELECT__TICK_ALWAYS) |
			BM_TIMROT_TIMCTRLn_UPDATE |
			BM_TIMROT_TIMCTRLn_IRQ_EN,
			mxs_timrot_base + HW_TIMROT_TIMCTRLn(0));

	/* another for clocksource */
	__raw_writel((timrot_is_v1() ?
			BV_TIMROTv1_TIMCTRLn_SELECT__32KHZ_XTAL :
			BV_TIMROTv2_TIMCTRLn_SELECT__TICK_ALWAYS) |
			BM_TIMROT_TIMCTRLn_RELOAD,
			mxs_timrot_base + HW_TIMROT_TIMCTRLn(1));

	/* set clocksource timer fixed count to the maximum */
	if (timrot_is_v1())
		__raw_writel(0xffff,
			mxs_timrot_base + HW_TIMROT_TIMCOUNTn(1));
	else
		__raw_writel(0xffffffff,
			mxs_timrot_base + HW_TIMROT_FIXED_COUNTn(1));

	/* init and register the timer to the framework */
	ret = mxs_clocksource_init(timer_clk);
	if (ret)
		return ret;

	ret = mxs_clockevent_init(timer_clk);
	if (ret)
		return ret;

	/* Make irqs happen */
	irq = irq_of_parse_and_map(np, 0);
	if (irq <= 0)
		return -EINVAL;

	return setup_irq(irq, &mxs_timer_irq);
}
CLOCKSOURCE_OF_DECLARE(mxs, "fsl,timrot", mxs_timer_init);
