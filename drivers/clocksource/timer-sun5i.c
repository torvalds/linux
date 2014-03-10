/*
 * Allwinner SoCs hstimer driver.
 *
 * Copyright (C) 2013 Maxime Ripard
 *
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqreturn.h>
#include <linux/sched_clock.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#define TIMER_IRQ_EN_REG		0x00
#define TIMER_IRQ_EN(val)			BIT(val)
#define TIMER_IRQ_ST_REG		0x04
#define TIMER_CTL_REG(val)		(0x20 * (val) + 0x10)
#define TIMER_CTL_ENABLE			BIT(0)
#define TIMER_CTL_RELOAD			BIT(1)
#define TIMER_CTL_CLK_PRES(val)			(((val) & 0x7) << 4)
#define TIMER_CTL_ONESHOT			BIT(7)
#define TIMER_INTVAL_LO_REG(val)	(0x20 * (val) + 0x14)
#define TIMER_INTVAL_HI_REG(val)	(0x20 * (val) + 0x18)
#define TIMER_CNTVAL_LO_REG(val)	(0x20 * (val) + 0x1c)
#define TIMER_CNTVAL_HI_REG(val)	(0x20 * (val) + 0x20)

#define TIMER_SYNC_TICKS	3

static void __iomem *timer_base;
static u32 ticks_per_jiffy;

/*
 * When we disable a timer, we need to wait at least for 2 cycles of
 * the timer source clock. We will use for that the clocksource timer
 * that is already setup and runs at the same frequency than the other
 * timers, and we never will be disabled.
 */
static void sun5i_clkevt_sync(void)
{
	u32 old = readl(timer_base + TIMER_CNTVAL_LO_REG(1));

	while ((old - readl(timer_base + TIMER_CNTVAL_LO_REG(1))) < TIMER_SYNC_TICKS)
		cpu_relax();
}

static void sun5i_clkevt_time_stop(u8 timer)
{
	u32 val = readl(timer_base + TIMER_CTL_REG(timer));
	writel(val & ~TIMER_CTL_ENABLE, timer_base + TIMER_CTL_REG(timer));

	sun5i_clkevt_sync();
}

static void sun5i_clkevt_time_setup(u8 timer, u32 delay)
{
	writel(delay, timer_base + TIMER_INTVAL_LO_REG(timer));
}

static void sun5i_clkevt_time_start(u8 timer, bool periodic)
{
	u32 val = readl(timer_base + TIMER_CTL_REG(timer));

	if (periodic)
		val &= ~TIMER_CTL_ONESHOT;
	else
		val |= TIMER_CTL_ONESHOT;

	writel(val | TIMER_CTL_ENABLE | TIMER_CTL_RELOAD,
	       timer_base + TIMER_CTL_REG(timer));
}

static void sun5i_clkevt_mode(enum clock_event_mode mode,
			      struct clock_event_device *clk)
{
	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		sun5i_clkevt_time_stop(0);
		sun5i_clkevt_time_setup(0, ticks_per_jiffy);
		sun5i_clkevt_time_start(0, true);
		break;
	case CLOCK_EVT_MODE_ONESHOT:
		sun5i_clkevt_time_stop(0);
		sun5i_clkevt_time_start(0, false);
		break;
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
	default:
		sun5i_clkevt_time_stop(0);
		break;
	}
}

static int sun5i_clkevt_next_event(unsigned long evt,
				   struct clock_event_device *unused)
{
	sun5i_clkevt_time_stop(0);
	sun5i_clkevt_time_setup(0, evt - TIMER_SYNC_TICKS);
	sun5i_clkevt_time_start(0, false);

	return 0;
}

static struct clock_event_device sun5i_clockevent = {
	.name = "sun5i_tick",
	.rating = 340,
	.features = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
	.set_mode = sun5i_clkevt_mode,
	.set_next_event = sun5i_clkevt_next_event,
};


static irqreturn_t sun5i_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = (struct clock_event_device *)dev_id;

	writel(0x1, timer_base + TIMER_IRQ_ST_REG);
	evt->event_handler(evt);

	return IRQ_HANDLED;
}

static struct irqaction sun5i_timer_irq = {
	.name = "sun5i_timer0",
	.flags = IRQF_TIMER | IRQF_IRQPOLL,
	.handler = sun5i_timer_interrupt,
	.dev_id = &sun5i_clockevent,
};

static u64 sun5i_timer_sched_read(void)
{
	return ~readl(timer_base + TIMER_CNTVAL_LO_REG(1));
}

static void __init sun5i_timer_init(struct device_node *node)
{
	unsigned long rate;
	struct clk *clk;
	int ret, irq;
	u32 val;

	timer_base = of_iomap(node, 0);
	if (!timer_base)
		panic("Can't map registers");

	irq = irq_of_parse_and_map(node, 0);
	if (irq <= 0)
		panic("Can't parse IRQ");

	clk = of_clk_get(node, 0);
	if (IS_ERR(clk))
		panic("Can't get timer clock");
	clk_prepare_enable(clk);
	rate = clk_get_rate(clk);

	writel(~0, timer_base + TIMER_INTVAL_LO_REG(1));
	writel(TIMER_CTL_ENABLE | TIMER_CTL_RELOAD,
	       timer_base + TIMER_CTL_REG(1));

	sched_clock_register(sun5i_timer_sched_read, 32, rate);
	clocksource_mmio_init(timer_base + TIMER_CNTVAL_LO_REG(1), node->name,
			      rate, 340, 32, clocksource_mmio_readl_down);

	ticks_per_jiffy = DIV_ROUND_UP(rate, HZ);

	ret = setup_irq(irq, &sun5i_timer_irq);
	if (ret)
		pr_warn("failed to setup irq %d\n", irq);

	/* Enable timer0 interrupt */
	val = readl(timer_base + TIMER_IRQ_EN_REG);
	writel(val | TIMER_IRQ_EN(0), timer_base + TIMER_IRQ_EN_REG);

	sun5i_clockevent.cpumask = cpu_possible_mask;
	sun5i_clockevent.irq = irq;

	clockevents_config_and_register(&sun5i_clockevent, rate,
					TIMER_SYNC_TICKS, 0xffffffff);
}
CLOCKSOURCE_OF_DECLARE(sun5i_a13, "allwinner,sun5i-a13-hstimer",
		       sun5i_timer_init);
CLOCKSOURCE_OF_DECLARE(sun7i_a20, "allwinner,sun7i-a20-hstimer",
		       sun5i_timer_init);
