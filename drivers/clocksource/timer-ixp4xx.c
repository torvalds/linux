// SPDX-License-Identifier: GPL-2.0
/*
 * IXP4 timer driver
 * Copyright (C) 2019 Linus Walleij <linus.walleij@linaro.org>
 *
 * Based on arch/arm/mach-ixp4xx/common.c
 * Copyright 2002 (C) Intel Corporation
 * Copyright 2003-2004 (C) MontaVista, Software, Inc.
 * Copyright (C) Deepak Saxena <dsaxena@plexity.net>
 */
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/sched_clock.h>
#include <linux/slab.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>

/*
 * Constants to make it easy to access Timer Control/Status registers
 */
#define IXP4XX_OSTS_OFFSET	0x00  /* Continuous Timestamp */
#define IXP4XX_OST1_OFFSET	0x04  /* Timer 1 Timestamp */
#define IXP4XX_OSRT1_OFFSET	0x08  /* Timer 1 Reload */
#define IXP4XX_OST2_OFFSET	0x0C  /* Timer 2 Timestamp */
#define IXP4XX_OSRT2_OFFSET	0x10  /* Timer 2 Reload */
#define IXP4XX_OSST_OFFSET	0x20  /* Timer Status */

/*
 * Timer register values and bit definitions
 */
#define IXP4XX_OST_ENABLE		0x00000001
#define IXP4XX_OST_ONE_SHOT		0x00000002
/* Low order bits of reload value ignored */
#define IXP4XX_OST_RELOAD_MASK		0x00000003
#define IXP4XX_OST_DISABLED		0x00000000
#define IXP4XX_OSST_TIMER_1_PEND	0x00000001
#define IXP4XX_OSST_TIMER_2_PEND	0x00000002
#define IXP4XX_OSST_TIMER_TS_PEND	0x00000004
/* Remaining registers are for the watchdog and defined in the watchdog driver */

struct ixp4xx_timer {
	void __iomem *base;
	u32 latch;
	struct clock_event_device clkevt;
#ifdef CONFIG_ARM
	struct delay_timer delay_timer;
#endif
};

/*
 * A local singleton used by sched_clock and delay timer reads, which are
 * fast and stateless
 */
static struct ixp4xx_timer *local_ixp4xx_timer;

static inline struct ixp4xx_timer *
to_ixp4xx_timer(struct clock_event_device *evt)
{
	return container_of(evt, struct ixp4xx_timer, clkevt);
}

static unsigned long ixp4xx_read_timer(void)
{
	return __raw_readl(local_ixp4xx_timer->base + IXP4XX_OSTS_OFFSET);
}

static u64 notrace ixp4xx_read_sched_clock(void)
{
	return ixp4xx_read_timer();
}

static u64 ixp4xx_clocksource_read(struct clocksource *c)
{
	return ixp4xx_read_timer();
}

static irqreturn_t ixp4xx_timer_interrupt(int irq, void *dev_id)
{
	struct ixp4xx_timer *tmr = dev_id;
	struct clock_event_device *evt = &tmr->clkevt;

	/* Clear Pending Interrupt */
	__raw_writel(IXP4XX_OSST_TIMER_1_PEND,
		     tmr->base + IXP4XX_OSST_OFFSET);

	evt->event_handler(evt);

	return IRQ_HANDLED;
}

static int ixp4xx_set_next_event(unsigned long cycles,
				 struct clock_event_device *evt)
{
	struct ixp4xx_timer *tmr = to_ixp4xx_timer(evt);
	u32 val;

	val = __raw_readl(tmr->base + IXP4XX_OSRT1_OFFSET);
	/* Keep enable/oneshot bits */
	val &= IXP4XX_OST_RELOAD_MASK;
	__raw_writel((cycles & ~IXP4XX_OST_RELOAD_MASK) | val,
		     tmr->base + IXP4XX_OSRT1_OFFSET);

	return 0;
}

static int ixp4xx_shutdown(struct clock_event_device *evt)
{
	struct ixp4xx_timer *tmr = to_ixp4xx_timer(evt);
	u32 val;

	val = __raw_readl(tmr->base + IXP4XX_OSRT1_OFFSET);
	val &= ~IXP4XX_OST_ENABLE;
	__raw_writel(val, tmr->base + IXP4XX_OSRT1_OFFSET);

	return 0;
}

static int ixp4xx_set_oneshot(struct clock_event_device *evt)
{
	struct ixp4xx_timer *tmr = to_ixp4xx_timer(evt);

	__raw_writel(IXP4XX_OST_ENABLE | IXP4XX_OST_ONE_SHOT,
		     tmr->base + IXP4XX_OSRT1_OFFSET);

	return 0;
}

static int ixp4xx_set_periodic(struct clock_event_device *evt)
{
	struct ixp4xx_timer *tmr = to_ixp4xx_timer(evt);
	u32 val;

	val = tmr->latch & ~IXP4XX_OST_RELOAD_MASK;
	val |= IXP4XX_OST_ENABLE;
	__raw_writel(val, tmr->base + IXP4XX_OSRT1_OFFSET);

	return 0;
}

static int ixp4xx_resume(struct clock_event_device *evt)
{
	struct ixp4xx_timer *tmr = to_ixp4xx_timer(evt);
	u32 val;

	val = __raw_readl(tmr->base + IXP4XX_OSRT1_OFFSET);
	val |= IXP4XX_OST_ENABLE;
	__raw_writel(val, tmr->base + IXP4XX_OSRT1_OFFSET);

	return 0;
}

/*
 * IXP4xx timer tick
 * We use OS timer1 on the CPU for the timer tick and the timestamp
 * counter as a source of real clock ticks to account for missed jiffies.
 */
static __init int ixp4xx_timer_register(void __iomem *base,
					int timer_irq,
					unsigned int timer_freq)
{
	struct ixp4xx_timer *tmr;
	int ret;

	tmr = kzalloc(sizeof(*tmr), GFP_KERNEL);
	if (!tmr)
		return -ENOMEM;
	tmr->base = base;

	/*
	 * The timer register doesn't allow to specify the two least
	 * significant bits of the timeout value and assumes them being zero.
	 * So make sure the latch is the best value with the two least
	 * significant bits unset.
	 */
	tmr->latch = DIV_ROUND_CLOSEST(timer_freq,
				       (IXP4XX_OST_RELOAD_MASK + 1) * HZ)
		* (IXP4XX_OST_RELOAD_MASK + 1);

	local_ixp4xx_timer = tmr;

	/* Reset/disable counter */
	__raw_writel(0, tmr->base + IXP4XX_OSRT1_OFFSET);

	/* Clear any pending interrupt on timer 1 */
	__raw_writel(IXP4XX_OSST_TIMER_1_PEND,
		     tmr->base + IXP4XX_OSST_OFFSET);

	/* Reset time-stamp counter */
	__raw_writel(0, tmr->base + IXP4XX_OSTS_OFFSET);

	clocksource_mmio_init(NULL, "OSTS", timer_freq, 200, 32,
			      ixp4xx_clocksource_read);

	tmr->clkevt.name = "ixp4xx timer1";
	tmr->clkevt.features = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT;
	tmr->clkevt.rating = 200;
	tmr->clkevt.set_state_shutdown = ixp4xx_shutdown;
	tmr->clkevt.set_state_periodic = ixp4xx_set_periodic;
	tmr->clkevt.set_state_oneshot = ixp4xx_set_oneshot;
	tmr->clkevt.tick_resume = ixp4xx_resume;
	tmr->clkevt.set_next_event = ixp4xx_set_next_event;
	tmr->clkevt.cpumask = cpumask_of(0);
	tmr->clkevt.irq = timer_irq;
	ret = request_irq(timer_irq, ixp4xx_timer_interrupt,
			  IRQF_TIMER, "IXP4XX-TIMER1", tmr);
	if (ret) {
		pr_crit("no timer IRQ\n");
		return -ENODEV;
	}
	clockevents_config_and_register(&tmr->clkevt, timer_freq,
					0xf, 0xfffffffe);

	sched_clock_register(ixp4xx_read_sched_clock, 32, timer_freq);

#ifdef CONFIG_ARM
	/* Also use this timer for delays */
	tmr->delay_timer.read_current_timer = ixp4xx_read_timer;
	tmr->delay_timer.freq = timer_freq;
	register_current_timer_delay(&tmr->delay_timer);
#endif

	return 0;
}

static struct platform_device ixp4xx_watchdog_device = {
	.name = "ixp4xx-watchdog",
	.id = -1,
};

/*
 * This probe gets called after the timer is already up and running. The main
 * function on this platform is to spawn the watchdog device as a child.
 */
static int ixp4xx_timer_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	/* Pass the base address as platform data and nothing else */
	ixp4xx_watchdog_device.dev.platform_data = local_ixp4xx_timer->base;
	ixp4xx_watchdog_device.dev.parent = dev;
	return platform_device_register(&ixp4xx_watchdog_device);
}

static const struct of_device_id ixp4xx_timer_dt_id[] = {
	{ .compatible = "intel,ixp4xx-timer", },
	{ /* sentinel */ },
};

static struct platform_driver ixp4xx_timer_driver = {
	.probe  = ixp4xx_timer_probe,
	.driver = {
		.name = "ixp4xx-timer",
		.of_match_table = ixp4xx_timer_dt_id,
		.suppress_bind_attrs = true,
	},
};
builtin_platform_driver(ixp4xx_timer_driver);

static __init int ixp4xx_of_timer_init(struct device_node *np)
{
	void __iomem *base;
	int irq;
	int ret;

	base = of_iomap(np, 0);
	if (!base) {
		pr_crit("IXP4xx: can't remap timer\n");
		return -ENODEV;
	}

	irq = irq_of_parse_and_map(np, 0);
	if (irq <= 0) {
		pr_err("Can't parse IRQ\n");
		ret = -EINVAL;
		goto out_unmap;
	}

	/* TODO: get some fixed clocks into the device tree */
	ret = ixp4xx_timer_register(base, irq, 66666000);
	if (ret)
		goto out_unmap;
	return 0;

out_unmap:
	iounmap(base);
	return ret;
}
TIMER_OF_DECLARE(ixp4xx, "intel,ixp4xx-timer", ixp4xx_of_timer_init);
