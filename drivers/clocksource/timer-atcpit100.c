// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2017 Andes Technology Corporation
/*
 *  Andestech ATCPIT100 Timer Device Driver Implementation
 * Rick Chen, Andes Technology Corporation <rick@andestech.com>
 *
 */

#include <linux/irq.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/cpufreq.h>
#include <linux/sched.h>
#include <linux/sched_clock.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include "timer-of.h"
#ifdef CONFIG_NDS32
#include <asm/vdso_timer_info.h>
#endif

/*
 * Definition of register offsets
 */

/* ID and Revision Register */
#define ID_REV		0x0

/* Configuration Register */
#define CFG		0x10

/* Interrupt Enable Register */
#define INT_EN		0x14
#define CH_INT_EN(c, i)	((1<<i)<<(4*c))
#define CH0INT0EN	0x01

/* Interrupt Status Register */
#define INT_STA		0x18
#define CH0INT0		0x01

/* Channel Enable Register */
#define CH_EN		0x1C
#define CH0TMR0EN	0x1
#define CH1TMR0EN	0x10

/* Channel 0 , 1 Control Register */
#define CH0_CTL		(0x20)
#define CH1_CTL		(0x20 + 0x10)

/* Channel clock source , bit 3 , 0:External clock , 1:APB clock */
#define APB_CLK		BIT(3)

/* Channel mode , bit 0~2 */
#define TMR_32		0x1
#define TMR_16		0x2
#define TMR_8		0x3

/* Channel 0 , 1 Reload Register */
#define CH0_REL		(0x24)
#define CH1_REL		(0x24 + 0x10)

/* Channel 0 , 1 Counter Register */
#define CH0_CNT		(0x28)
#define CH1_CNT		(0x28 + 0x10)

#define TIMER_SYNC_TICKS	3

static void atcpit100_ch1_tmr0_en(void __iomem *base)
{
	writel(~0, base + CH1_REL);
	writel(APB_CLK|TMR_32, base + CH1_CTL);
}

static void atcpit100_ch0_tmr0_en(void __iomem *base)
{
	writel(APB_CLK|TMR_32, base + CH0_CTL);
}

static void atcpit100_clkevt_time_setup(void __iomem *base, unsigned long delay)
{
	writel(delay, base + CH0_CNT);
	writel(delay, base + CH0_REL);
}

static void atcpit100_timer_clear_interrupt(void __iomem *base)
{
	u32 val;

	val = readl(base + INT_STA);
	writel(val | CH0INT0, base + INT_STA);
}

static void atcpit100_clocksource_start(void __iomem *base)
{
	u32 val;

	val = readl(base + CH_EN);
	writel(val | CH1TMR0EN, base + CH_EN);
}

static void atcpit100_clkevt_time_start(void __iomem *base)
{
	u32 val;

	val = readl(base + CH_EN);
	writel(val | CH0TMR0EN, base + CH_EN);
}

static void atcpit100_clkevt_time_stop(void __iomem *base)
{
	u32 val;

	atcpit100_timer_clear_interrupt(base);
	val = readl(base + CH_EN);
	writel(val & ~CH0TMR0EN, base + CH_EN);
}

static int atcpit100_clkevt_next_event(unsigned long evt,
	struct clock_event_device *clkevt)
{
	u32 val;
	struct timer_of *to = to_timer_of(clkevt);

	val = readl(timer_of_base(to) + CH_EN);
	writel(val & ~CH0TMR0EN, timer_of_base(to) + CH_EN);
	writel(evt, timer_of_base(to) + CH0_REL);
	writel(val | CH0TMR0EN, timer_of_base(to) + CH_EN);

	return 0;
}

static int atcpit100_clkevt_set_periodic(struct clock_event_device *evt)
{
	struct timer_of *to = to_timer_of(evt);

	atcpit100_clkevt_time_setup(timer_of_base(to), timer_of_period(to));
	atcpit100_clkevt_time_start(timer_of_base(to));

	return 0;
}
static int atcpit100_clkevt_shutdown(struct clock_event_device *evt)
{
	struct timer_of *to = to_timer_of(evt);

	atcpit100_clkevt_time_stop(timer_of_base(to));

	return 0;
}
static int atcpit100_clkevt_set_oneshot(struct clock_event_device *evt)
{
	struct timer_of *to = to_timer_of(evt);
	u32 val;

	writel(~0x0, timer_of_base(to) + CH0_REL);
	val = readl(timer_of_base(to) + CH_EN);
	writel(val | CH0TMR0EN, timer_of_base(to) + CH_EN);

	return 0;
}

static irqreturn_t atcpit100_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = (struct clock_event_device *)dev_id;
	struct timer_of *to = to_timer_of(evt);

	atcpit100_timer_clear_interrupt(timer_of_base(to));

	evt->event_handler(evt);

	return IRQ_HANDLED;
}

static struct timer_of to = {
	.flags = TIMER_OF_IRQ | TIMER_OF_CLOCK | TIMER_OF_BASE,

	.clkevt = {
		.name = "atcpit100_tick",
		.rating = 300,
		.features = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
		.set_state_shutdown = atcpit100_clkevt_shutdown,
		.set_state_periodic = atcpit100_clkevt_set_periodic,
		.set_state_oneshot = atcpit100_clkevt_set_oneshot,
		.tick_resume = atcpit100_clkevt_shutdown,
		.set_next_event = atcpit100_clkevt_next_event,
		.cpumask = cpu_all_mask,
	},

	.of_irq = {
		.handler = atcpit100_timer_interrupt,
		.flags = IRQF_TIMER | IRQF_IRQPOLL,
	},

	/*
	 * FIXME: we currently only support clocking using PCLK
	 * and using EXTCLK is not supported in the driver.
	 */
	.of_clk = {
		.name = "PCLK",
	}
};

static u64 notrace atcpit100_timer_sched_read(void)
{
	return ~readl(timer_of_base(&to) + CH1_CNT);
}

#ifdef CONFIG_NDS32
static void fill_vdso_need_info(struct device_node *node)
{
	struct resource timer_res;
	of_address_to_resource(node, 0, &timer_res);
	timer_info.mapping_base = (unsigned long)timer_res.start;
	timer_info.cycle_count_down = true;
	timer_info.cycle_count_reg_offset = CH1_CNT;
}
#endif

static int __init atcpit100_timer_init(struct device_node *node)
{
	int ret;
	u32 val;
	void __iomem *base;

	ret = timer_of_init(node, &to);
	if (ret)
		return ret;

	base = timer_of_base(&to);

	sched_clock_register(atcpit100_timer_sched_read, 32,
		timer_of_rate(&to));

	ret = clocksource_mmio_init(base + CH1_CNT,
		node->name, timer_of_rate(&to), 300, 32,
		clocksource_mmio_readl_down);

	if (ret) {
		pr_err("Failed to register clocksource\n");
		return ret;
	}

	/* clear channel 0 timer0 interrupt */
	atcpit100_timer_clear_interrupt(base);

	clockevents_config_and_register(&to.clkevt, timer_of_rate(&to),
					TIMER_SYNC_TICKS, 0xffffffff);
	atcpit100_ch0_tmr0_en(base);
	atcpit100_ch1_tmr0_en(base);
	atcpit100_clocksource_start(base);
	atcpit100_clkevt_time_start(base);

	/* Enable channel 0 timer0 interrupt */
	val = readl(base + INT_EN);
	writel(val | CH0INT0EN, base + INT_EN);

#ifdef CONFIG_NDS32
	fill_vdso_need_info(node);
#endif

	return ret;
}

TIMER_OF_DECLARE(atcpit100, "andestech,atcpit100", atcpit100_timer_init);
