/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * Copyright (C) 2013 John Crispin <john@phrozen.org>
*/

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/of_gpio.h>
#include <linux/clk.h>

#include <asm/mach-ralink/ralink_regs.h>

#define TIMER_REG_TMRSTAT		0x00
#define TIMER_REG_TMR0LOAD		0x10
#define TIMER_REG_TMR0CTL		0x18

#define TMRSTAT_TMR0INT			BIT(0)

#define TMR0CTL_ENABLE			BIT(7)
#define TMR0CTL_MODE_PERIODIC		BIT(4)
#define TMR0CTL_PRESCALER		1
#define TMR0CTL_PRESCALE_VAL		(0xf - TMR0CTL_PRESCALER)
#define TMR0CTL_PRESCALE_DIV		(65536 / BIT(TMR0CTL_PRESCALER))

struct rt_timer {
	struct device	*dev;
	void __iomem	*membase;
	int		irq;
	unsigned long	timer_freq;
	unsigned long	timer_div;
};

static inline void rt_timer_w32(struct rt_timer *rt, u8 reg, u32 val)
{
	__raw_writel(val, rt->membase + reg);
}

static inline u32 rt_timer_r32(struct rt_timer *rt, u8 reg)
{
	return __raw_readl(rt->membase + reg);
}

static irqreturn_t rt_timer_irq(int irq, void *_rt)
{
	struct rt_timer *rt =  (struct rt_timer *) _rt;

	rt_timer_w32(rt, TIMER_REG_TMR0LOAD, rt->timer_freq / rt->timer_div);
	rt_timer_w32(rt, TIMER_REG_TMRSTAT, TMRSTAT_TMR0INT);

	return IRQ_HANDLED;
}


static int rt_timer_request(struct rt_timer *rt)
{
	int err = request_irq(rt->irq, rt_timer_irq, 0,
						dev_name(rt->dev), rt);
	if (err) {
		dev_err(rt->dev, "failed to request irq\n");
	} else {
		u32 t = TMR0CTL_MODE_PERIODIC | TMR0CTL_PRESCALE_VAL;
		rt_timer_w32(rt, TIMER_REG_TMR0CTL, t);
	}
	return err;
}

static void rt_timer_free(struct rt_timer *rt)
{
	free_irq(rt->irq, rt);
}

static int rt_timer_config(struct rt_timer *rt, unsigned long divisor)
{
	if (rt->timer_freq < divisor)
		rt->timer_div = rt->timer_freq;
	else
		rt->timer_div = divisor;

	rt_timer_w32(rt, TIMER_REG_TMR0LOAD, rt->timer_freq / rt->timer_div);

	return 0;
}

static int rt_timer_enable(struct rt_timer *rt)
{
	u32 t;

	rt_timer_w32(rt, TIMER_REG_TMR0LOAD, rt->timer_freq / rt->timer_div);

	t = rt_timer_r32(rt, TIMER_REG_TMR0CTL);
	t |= TMR0CTL_ENABLE;
	rt_timer_w32(rt, TIMER_REG_TMR0CTL, t);

	return 0;
}

static void rt_timer_disable(struct rt_timer *rt)
{
	u32 t;

	t = rt_timer_r32(rt, TIMER_REG_TMR0CTL);
	t &= ~TMR0CTL_ENABLE;
	rt_timer_w32(rt, TIMER_REG_TMR0CTL, t);
}

static int rt_timer_probe(struct platform_device *pdev)
{
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	struct rt_timer *rt;
	struct clk *clk;

	rt = devm_kzalloc(&pdev->dev, sizeof(*rt), GFP_KERNEL);
	if (!rt) {
		dev_err(&pdev->dev, "failed to allocate memory\n");
		return -ENOMEM;
	}

	rt->irq = platform_get_irq(pdev, 0);
	if (!rt->irq) {
		dev_err(&pdev->dev, "failed to load irq\n");
		return -ENOENT;
	}

	rt->membase = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(rt->membase))
		return PTR_ERR(rt->membase);

	clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "failed get clock rate\n");
		return PTR_ERR(clk);
	}

	rt->timer_freq = clk_get_rate(clk) / TMR0CTL_PRESCALE_DIV;
	if (!rt->timer_freq)
		return -EINVAL;

	rt->dev = &pdev->dev;
	platform_set_drvdata(pdev, rt);

	rt_timer_request(rt);
	rt_timer_config(rt, 2);
	rt_timer_enable(rt);

	dev_info(&pdev->dev, "maximum frequency is %luHz\n", rt->timer_freq);

	return 0;
}

static int rt_timer_remove(struct platform_device *pdev)
{
	struct rt_timer *rt = platform_get_drvdata(pdev);

	rt_timer_disable(rt);
	rt_timer_free(rt);

	return 0;
}

static const struct of_device_id rt_timer_match[] = {
	{ .compatible = "ralink,rt2880-timer" },
	{},
};
MODULE_DEVICE_TABLE(of, rt_timer_match);

static struct platform_driver rt_timer_driver = {
	.probe = rt_timer_probe,
	.remove = rt_timer_remove,
	.driver = {
		.name		= "rt-timer",
		.of_match_table	= rt_timer_match
	},
};

module_platform_driver(rt_timer_driver);

MODULE_DESCRIPTION("Ralink RT2880 timer");
MODULE_AUTHOR("John Crispin <john@phrozen.org");
MODULE_LICENSE("GPL");
