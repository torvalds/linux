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

#include <asm/mach/time.h>
#include <mach/mxs.h>
#include <mach/common.h>

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
#define BV_TIMROTv1_TIMCTRLn_SELECT__32KHZ_XTAL	0x8
#define BV_TIMROTv2_TIMCTRLn_SELECT__32KHZ_XTAL	0xb

static struct clock_event_device mxs_clockevent_device;
static enum clock_event_mode mxs_clockevent_mode = CLOCK_EVT_MODE_UNUSED;

static void __iomem *mxs_timrot_base = MXS_IO_ADDRESS(MXS_TIMROT_BASE_ADDR);
static u32 timrot_major_version;

static inline void timrot_irq_disable(void)
{
	__mxs_clrl(BM_TIMROT_TIMCTRLn_IRQ_EN,
			mxs_timrot_base + HW_TIMROT_TIMCTRLn(0));
}

static inline void timrot_irq_enable(void)
{
	__mxs_setl(BM_TIMROT_TIMCTRLn_IRQ_EN,
			mxs_timrot_base + HW_TIMROT_TIMCTRLn(0));
}

static void timrot_irq_acknowledge(void)
{
	__mxs_clrl(BM_TIMROT_TIMCTRLn_IRQ,
			mxs_timrot_base + HW_TIMROT_TIMCTRLn(0));
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

#ifdef DEBUG
static const char *clock_event_mode_label[] const = {
	[CLOCK_EVT_MODE_PERIODIC] = "CLOCK_EVT_MODE_PERIODIC",
	[CLOCK_EVT_MODE_ONESHOT]  = "CLOCK_EVT_MODE_ONESHOT",
	[CLOCK_EVT_MODE_SHUTDOWN] = "CLOCK_EVT_MODE_SHUTDOWN",
	[CLOCK_EVT_MODE_UNUSED]   = "CLOCK_EVT_MODE_UNUSED"
};
#endif /* DEBUG */

static void mxs_set_mode(enum clock_event_mode mode,
				struct clock_event_device *evt)
{
	/* Disable interrupt in timer module */
	timrot_irq_disable();

	if (mode != mxs_clockevent_mode) {
		/* Set event time into the furthest future */
		if (timrot_is_v1())
			__raw_writel(0xffff,
				mxs_timrot_base + HW_TIMROT_TIMCOUNTn(1));
		else
			__raw_writel(0xffffffff,
				mxs_timrot_base + HW_TIMROT_FIXED_COUNTn(1));

		/* Clear pending interrupt */
		timrot_irq_acknowledge();
	}

#ifdef DEBUG
	pr_info("%s: changing mode from %s to %s\n", __func__,
		clock_event_mode_label[mxs_clockevent_mode],
		clock_event_mode_label[mode]);
#endif /* DEBUG */

	/* Remember timer mode */
	mxs_clockevent_mode = mode;

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		pr_err("%s: Periodic mode is not implemented\n", __func__);
		break;
	case CLOCK_EVT_MODE_ONESHOT:
		timrot_irq_enable();
		break;
	case CLOCK_EVT_MODE_SHUTDOWN:
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_RESUME:
		/* Left event sources disabled, no more interrupts appear */
		break;
	}
}

static struct clock_event_device mxs_clockevent_device = {
	.name		= "mxs_timrot",
	.features	= CLOCK_EVT_FEAT_ONESHOT,
	.shift		= 32,
	.set_mode	= mxs_set_mode,
	.set_next_event	= timrotv2_set_next_event,
	.rating		= 200,
};

static int __init mxs_clockevent_init(struct clk *timer_clk)
{
	unsigned int c = clk_get_rate(timer_clk);

	mxs_clockevent_device.mult =
		div_sc(c, NSEC_PER_SEC, mxs_clockevent_device.shift);
	mxs_clockevent_device.cpumask = cpumask_of(0);
	if (timrot_is_v1()) {
		mxs_clockevent_device.set_next_event = timrotv1_set_next_event;
		mxs_clockevent_device.max_delta_ns =
			clockevent_delta2ns(0xfffe, &mxs_clockevent_device);
		mxs_clockevent_device.min_delta_ns =
			clockevent_delta2ns(0xf, &mxs_clockevent_device);
	} else {
		mxs_clockevent_device.max_delta_ns =
			clockevent_delta2ns(0xfffffffe, &mxs_clockevent_device);
		mxs_clockevent_device.min_delta_ns =
			clockevent_delta2ns(0xf, &mxs_clockevent_device);
	}

	clockevents_register_device(&mxs_clockevent_device);

	return 0;
}

static struct clocksource clocksource_mxs = {
	.name		= "mxs_timer",
	.rating		= 200,
	.read		= timrotv1_get_cycles,
	.mask		= CLOCKSOURCE_MASK(16),
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

static int __init mxs_clocksource_init(struct clk *timer_clk)
{
	unsigned int c = clk_get_rate(timer_clk);

	if (timrot_is_v1())
		clocksource_register_hz(&clocksource_mxs, c);
	else
		clocksource_mmio_init(mxs_timrot_base + HW_TIMROT_RUNNING_COUNTn(1),
			"mxs_timer", c, 200, 32, clocksource_mmio_readl_down);

	return 0;
}

void __init mxs_timer_init(struct clk *timer_clk, int irq)
{
	if (!timer_clk) {
		timer_clk = clk_get_sys("timrot", NULL);
		if (IS_ERR(timer_clk)) {
			pr_err("%s: failed to get clk\n", __func__);
			return;
		}
	}

	clk_prepare_enable(timer_clk);

	/*
	 * Initialize timers to a known state
	 */
	mxs_reset_block(mxs_timrot_base + HW_TIMROT_ROTCTRL);

	/* get timrot version */
	timrot_major_version = __raw_readl(mxs_timrot_base +
				(cpu_is_mx23() ? MX23_TIMROT_VERSION_OFFSET :
						MX28_TIMROT_VERSION_OFFSET));
	timrot_major_version >>= BP_TIMROT_MAJOR_VERSION;

	/* one for clock_event */
	__raw_writel((timrot_is_v1() ?
			BV_TIMROTv1_TIMCTRLn_SELECT__32KHZ_XTAL :
			BV_TIMROTv2_TIMCTRLn_SELECT__32KHZ_XTAL) |
			BM_TIMROT_TIMCTRLn_UPDATE |
			BM_TIMROT_TIMCTRLn_IRQ_EN,
			mxs_timrot_base + HW_TIMROT_TIMCTRLn(0));

	/* another for clocksource */
	__raw_writel((timrot_is_v1() ?
			BV_TIMROTv1_TIMCTRLn_SELECT__32KHZ_XTAL :
			BV_TIMROTv2_TIMCTRLn_SELECT__32KHZ_XTAL) |
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
	mxs_clocksource_init(timer_clk);
	mxs_clockevent_init(timer_clk);

	/* Make irqs happen */
	setup_irq(irq, &mxs_timer_irq);
}
