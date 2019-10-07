/*
 * Allwinner A1X SoCs timer handling.
 *
 * Copyright (C) 2012 Maxime Ripard
 *
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 *
 * Based on code from
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Benn Huang <benn@allwinnertech.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqreturn.h>
#include <linux/sched_clock.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#include "timer-of.h"

#define TIMER_IRQ_EN_REG	0x00
#define TIMER_IRQ_EN(val)		BIT(val)
#define TIMER_IRQ_ST_REG	0x04
#define TIMER_CTL_REG(val)	(0x10 * val + 0x10)
#define TIMER_CTL_ENABLE		BIT(0)
#define TIMER_CTL_RELOAD		BIT(1)
#define TIMER_CTL_CLK_SRC(val)		(((val) & 0x3) << 2)
#define TIMER_CTL_CLK_SRC_OSC24M		(1)
#define TIMER_CTL_CLK_PRES(val)		(((val) & 0x7) << 4)
#define TIMER_CTL_ONESHOT		BIT(7)
#define TIMER_INTVAL_REG(val)	(0x10 * (val) + 0x14)
#define TIMER_CNTVAL_REG(val)	(0x10 * (val) + 0x18)

#define TIMER_SYNC_TICKS	3

/*
 * When we disable a timer, we need to wait at least for 2 cycles of
 * the timer source clock. We will use for that the clocksource timer
 * that is already setup and runs at the same frequency than the other
 * timers, and we never will be disabled.
 */
static void sun4i_clkevt_sync(void __iomem *base)
{
	u32 old = readl(base + TIMER_CNTVAL_REG(1));

	while ((old - readl(base + TIMER_CNTVAL_REG(1))) < TIMER_SYNC_TICKS)
		cpu_relax();
}

static void sun4i_clkevt_time_stop(void __iomem *base, u8 timer)
{
	u32 val = readl(base + TIMER_CTL_REG(timer));
	writel(val & ~TIMER_CTL_ENABLE, base + TIMER_CTL_REG(timer));
	sun4i_clkevt_sync(base);
}

static void sun4i_clkevt_time_setup(void __iomem *base, u8 timer,
				    unsigned long delay)
{
	writel(delay, base + TIMER_INTVAL_REG(timer));
}

static void sun4i_clkevt_time_start(void __iomem *base, u8 timer,
				    bool periodic)
{
	u32 val = readl(base + TIMER_CTL_REG(timer));

	if (periodic)
		val &= ~TIMER_CTL_ONESHOT;
	else
		val |= TIMER_CTL_ONESHOT;

	writel(val | TIMER_CTL_ENABLE | TIMER_CTL_RELOAD,
	       base + TIMER_CTL_REG(timer));
}

static int sun4i_clkevt_shutdown(struct clock_event_device *evt)
{
	struct timer_of *to = to_timer_of(evt);

	sun4i_clkevt_time_stop(timer_of_base(to), 0);

	return 0;
}

static int sun4i_clkevt_set_oneshot(struct clock_event_device *evt)
{
	struct timer_of *to = to_timer_of(evt);

	sun4i_clkevt_time_stop(timer_of_base(to), 0);
	sun4i_clkevt_time_start(timer_of_base(to), 0, false);

	return 0;
}

static int sun4i_clkevt_set_periodic(struct clock_event_device *evt)
{
	struct timer_of *to = to_timer_of(evt);

	sun4i_clkevt_time_stop(timer_of_base(to), 0);
	sun4i_clkevt_time_setup(timer_of_base(to), 0, timer_of_period(to));
	sun4i_clkevt_time_start(timer_of_base(to), 0, true);

	return 0;
}

static int sun4i_clkevt_next_event(unsigned long evt,
				   struct clock_event_device *clkevt)
{
	struct timer_of *to = to_timer_of(clkevt);

	sun4i_clkevt_time_stop(timer_of_base(to), 0);
	sun4i_clkevt_time_setup(timer_of_base(to), 0, evt - TIMER_SYNC_TICKS);
	sun4i_clkevt_time_start(timer_of_base(to), 0, false);

	return 0;
}

static void sun4i_timer_clear_interrupt(void __iomem *base)
{
	writel(TIMER_IRQ_EN(0), base + TIMER_IRQ_ST_REG);
}

static irqreturn_t sun4i_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = (struct clock_event_device *)dev_id;
	struct timer_of *to = to_timer_of(evt);

	sun4i_timer_clear_interrupt(timer_of_base(to));
	evt->event_handler(evt);

	return IRQ_HANDLED;
}

static struct timer_of to = {
	.flags = TIMER_OF_IRQ | TIMER_OF_CLOCK | TIMER_OF_BASE,

	.clkevt = {
		.name = "sun4i_tick",
		.rating = 350,
		.features = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
		.set_state_shutdown = sun4i_clkevt_shutdown,
		.set_state_periodic = sun4i_clkevt_set_periodic,
		.set_state_oneshot = sun4i_clkevt_set_oneshot,
		.tick_resume = sun4i_clkevt_shutdown,
		.set_next_event = sun4i_clkevt_next_event,
		.cpumask = cpu_possible_mask,
	},

	.of_irq = {
		.handler = sun4i_timer_interrupt,
		.flags = IRQF_TIMER | IRQF_IRQPOLL,
	},
};

static u64 notrace sun4i_timer_sched_read(void)
{
	return ~readl(timer_of_base(&to) + TIMER_CNTVAL_REG(1));
}

static int __init sun4i_timer_init(struct device_node *node)
{
	int ret;
	u32 val;

	ret = timer_of_init(node, &to);
	if (ret)
		return ret;

	writel(~0, timer_of_base(&to) + TIMER_INTVAL_REG(1));
	writel(TIMER_CTL_ENABLE | TIMER_CTL_RELOAD |
	       TIMER_CTL_CLK_SRC(TIMER_CTL_CLK_SRC_OSC24M),
	       timer_of_base(&to) + TIMER_CTL_REG(1));

	/*
	 * sched_clock_register does not have priorities, and on sun6i and
	 * later there is a better sched_clock registered by arm_arch_timer.c
	 */
	if (of_machine_is_compatible("allwinner,sun4i-a10") ||
	    of_machine_is_compatible("allwinner,sun5i-a13") ||
	    of_machine_is_compatible("allwinner,sun5i-a10s") ||
	    of_machine_is_compatible("allwinner,suniv-f1c100s"))
		sched_clock_register(sun4i_timer_sched_read, 32,
				     timer_of_rate(&to));

	ret = clocksource_mmio_init(timer_of_base(&to) + TIMER_CNTVAL_REG(1),
				    node->name, timer_of_rate(&to), 350, 32,
				    clocksource_mmio_readl_down);
	if (ret) {
		pr_err("Failed to register clocksource\n");
		return ret;
	}

	writel(TIMER_CTL_CLK_SRC(TIMER_CTL_CLK_SRC_OSC24M),
	       timer_of_base(&to) + TIMER_CTL_REG(0));

	/* Make sure timer is stopped before playing with interrupts */
	sun4i_clkevt_time_stop(timer_of_base(&to), 0);

	/* clear timer0 interrupt */
	sun4i_timer_clear_interrupt(timer_of_base(&to));

	clockevents_config_and_register(&to.clkevt, timer_of_rate(&to),
					TIMER_SYNC_TICKS, 0xffffffff);

	/* Enable timer0 interrupt */
	val = readl(timer_of_base(&to) + TIMER_IRQ_EN_REG);
	writel(val | TIMER_IRQ_EN(0), timer_of_base(&to) + TIMER_IRQ_EN_REG);

	return ret;
}
TIMER_OF_DECLARE(sun4i, "allwinner,sun4i-a10-timer",
		       sun4i_timer_init);
TIMER_OF_DECLARE(sun8i_a23, "allwinner,sun8i-a23-timer",
		 sun4i_timer_init);
TIMER_OF_DECLARE(sun8i_v3s, "allwinner,sun8i-v3s-timer",
		 sun4i_timer_init);
TIMER_OF_DECLARE(suniv, "allwinner,suniv-f1c100s-timer",
		       sun4i_timer_init);
