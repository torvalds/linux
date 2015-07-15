/*
 * ST's LPC Watchdog
 *
 * Copyright (C) 2014 STMicroelectronics -- All Rights Reserved
 *
 * Author: David Paris <david.paris@st.com> for STMicroelectronics
 *         Lee Jones <lee.jones@linaro.org> for STMicroelectronics
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#include <linux/clk.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/watchdog.h>

#include <dt-bindings/mfd/st-lpc.h>

/* Low Power Alarm */
#define LPC_LPA_LSB_OFF			0x410
#define LPC_LPA_START_OFF		0x418

/* LPC as WDT */
#define LPC_WDT_OFF			0x510

static struct watchdog_device st_wdog_dev;

struct st_wdog_syscfg {
	unsigned int reset_type_reg;
	unsigned int reset_type_mask;
	unsigned int enable_reg;
	unsigned int enable_mask;
};

struct st_wdog {
	void __iomem *base;
	struct device *dev;
	struct regmap *regmap;
	struct st_wdog_syscfg *syscfg;
	struct clk *clk;
	unsigned long clkrate;
	bool warm_reset;
};

static struct st_wdog_syscfg stid127_syscfg = {
	.reset_type_reg		= 0x004,
	.reset_type_mask	= BIT(2),
	.enable_reg		= 0x000,
	.enable_mask		= BIT(2),
};

static struct st_wdog_syscfg stih415_syscfg = {
	.reset_type_reg		= 0x0B8,
	.reset_type_mask	= BIT(6),
	.enable_reg		= 0x0B4,
	.enable_mask		= BIT(7),
};

static struct st_wdog_syscfg stih416_syscfg = {
	.reset_type_reg		= 0x88C,
	.reset_type_mask	= BIT(6),
	.enable_reg		= 0x888,
	.enable_mask		= BIT(7),
};

static struct st_wdog_syscfg stih407_syscfg = {
	.enable_reg		= 0x204,
	.enable_mask		= BIT(19),
};

static const struct of_device_id st_wdog_match[] = {
	{
		.compatible = "st,stih407-lpc",
		.data = &stih407_syscfg,
	},
	{
		.compatible = "st,stih416-lpc",
		.data = &stih416_syscfg,
	},
	{
		.compatible = "st,stih415-lpc",
		.data = &stih415_syscfg,
	},
	{
		.compatible = "st,stid127-lpc",
		.data = &stid127_syscfg,
	},
	{},
};
MODULE_DEVICE_TABLE(of, st_wdog_match);

static void st_wdog_setup(struct st_wdog *st_wdog, bool enable)
{
	/* Type of watchdog reset - 0: Cold 1: Warm */
	if (st_wdog->syscfg->reset_type_reg)
		regmap_update_bits(st_wdog->regmap,
				   st_wdog->syscfg->reset_type_reg,
				   st_wdog->syscfg->reset_type_mask,
				   st_wdog->warm_reset);

	/* Mask/unmask watchdog reset */
	regmap_update_bits(st_wdog->regmap,
			   st_wdog->syscfg->enable_reg,
			   st_wdog->syscfg->enable_mask,
			   enable ? 0 : st_wdog->syscfg->enable_mask);
}

static void st_wdog_load_timer(struct st_wdog *st_wdog, unsigned int timeout)
{
	unsigned long clkrate = st_wdog->clkrate;

	writel_relaxed(timeout * clkrate, st_wdog->base + LPC_LPA_LSB_OFF);
	writel_relaxed(1, st_wdog->base + LPC_LPA_START_OFF);
}

static int st_wdog_start(struct watchdog_device *wdd)
{
	struct st_wdog *st_wdog = watchdog_get_drvdata(wdd);

	writel_relaxed(1, st_wdog->base + LPC_WDT_OFF);

	return 0;
}

static int st_wdog_stop(struct watchdog_device *wdd)
{
	struct st_wdog *st_wdog = watchdog_get_drvdata(wdd);

	writel_relaxed(0, st_wdog->base + LPC_WDT_OFF);

	return 0;
}

static int st_wdog_set_timeout(struct watchdog_device *wdd,
			       unsigned int timeout)
{
	struct st_wdog *st_wdog = watchdog_get_drvdata(wdd);

	wdd->timeout = timeout;
	st_wdog_load_timer(st_wdog, timeout);

	return 0;
}

static int st_wdog_keepalive(struct watchdog_device *wdd)
{
	struct st_wdog *st_wdog = watchdog_get_drvdata(wdd);

	st_wdog_load_timer(st_wdog, wdd->timeout);

	return 0;
}

static const struct watchdog_info st_wdog_info = {
	.options = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE,
	.identity = "ST LPC WDT",
};

static const struct watchdog_ops st_wdog_ops = {
	.owner		= THIS_MODULE,
	.start		= st_wdog_start,
	.stop		= st_wdog_stop,
	.ping		= st_wdog_keepalive,
	.set_timeout	= st_wdog_set_timeout,
};

static struct watchdog_device st_wdog_dev = {
	.info		= &st_wdog_info,
	.ops		= &st_wdog_ops,
};

static int st_wdog_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct device_node *np = pdev->dev.of_node;
	struct st_wdog *st_wdog;
	struct regmap *regmap;
	struct resource *res;
	struct clk *clk;
	void __iomem *base;
	uint32_t mode;
	int ret;

	ret = of_property_read_u32(np, "st,lpc-mode", &mode);
	if (ret) {
		dev_err(&pdev->dev, "An LPC mode must be provided\n");
		return -EINVAL;
	}

	/* LPC can either run as a Clocksource or in RTC or WDT mode */
	if (mode != ST_LPC_MODE_WDT)
		return -ENODEV;

	st_wdog = devm_kzalloc(&pdev->dev, sizeof(*st_wdog), GFP_KERNEL);
	if (!st_wdog)
		return -ENOMEM;

	match = of_match_device(st_wdog_match, &pdev->dev);
	if (!match) {
		dev_err(&pdev->dev, "Couldn't match device\n");
		return -ENODEV;
	}
	st_wdog->syscfg	= (struct st_wdog_syscfg *)match->data;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	regmap = syscon_regmap_lookup_by_phandle(np, "st,syscfg");
	if (IS_ERR(regmap)) {
		dev_err(&pdev->dev, "No syscfg phandle specified\n");
		return PTR_ERR(regmap);
	}

	clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "Unable to request clock\n");
		return PTR_ERR(clk);
	}

	st_wdog->dev		= &pdev->dev;
	st_wdog->base		= base;
	st_wdog->clk		= clk;
	st_wdog->regmap		= regmap;
	st_wdog->warm_reset	= of_property_read_bool(np, "st,warm_reset");
	st_wdog->clkrate	= clk_get_rate(st_wdog->clk);

	if (!st_wdog->clkrate) {
		dev_err(&pdev->dev, "Unable to fetch clock rate\n");
		return -EINVAL;
	}
	st_wdog_dev.max_timeout = 0xFFFFFFFF / st_wdog->clkrate;

	ret = clk_prepare_enable(clk);
	if (ret) {
		dev_err(&pdev->dev, "Unable to enable clock\n");
		return ret;
	}

	watchdog_set_drvdata(&st_wdog_dev, st_wdog);
	watchdog_set_nowayout(&st_wdog_dev, WATCHDOG_NOWAYOUT);

	/* Init Watchdog timeout with value in DT */
	ret = watchdog_init_timeout(&st_wdog_dev, 0, &pdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "Unable to initialise watchdog timeout\n");
		clk_disable_unprepare(clk);
		return ret;
	}

	ret = watchdog_register_device(&st_wdog_dev);
	if (ret) {
		dev_err(&pdev->dev, "Unable to register watchdog\n");
		clk_disable_unprepare(clk);
		return ret;
	}

	st_wdog_setup(st_wdog, true);

	dev_info(&pdev->dev, "LPC Watchdog driver registered, reset type is %s",
		 st_wdog->warm_reset ? "warm" : "cold");

	return ret;
}

static int st_wdog_remove(struct platform_device *pdev)
{
	struct st_wdog *st_wdog = watchdog_get_drvdata(&st_wdog_dev);

	st_wdog_setup(st_wdog, false);
	watchdog_unregister_device(&st_wdog_dev);
	clk_disable_unprepare(st_wdog->clk);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int st_wdog_suspend(struct device *dev)
{
	struct st_wdog *st_wdog = watchdog_get_drvdata(&st_wdog_dev);

	if (watchdog_active(&st_wdog_dev))
		st_wdog_stop(&st_wdog_dev);

	st_wdog_setup(st_wdog, false);

	clk_disable(st_wdog->clk);

	return 0;
}

static int st_wdog_resume(struct device *dev)
{
	struct st_wdog *st_wdog = watchdog_get_drvdata(&st_wdog_dev);
	int ret;

	ret = clk_enable(st_wdog->clk);
	if (ret) {
		dev_err(dev, "Unable to re-enable clock\n");
		watchdog_unregister_device(&st_wdog_dev);
		clk_unprepare(st_wdog->clk);
		return ret;
	}

	st_wdog_setup(st_wdog, true);

	if (watchdog_active(&st_wdog_dev)) {
		st_wdog_load_timer(st_wdog, st_wdog_dev.timeout);
		st_wdog_start(&st_wdog_dev);
	}

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(st_wdog_pm_ops,
			 st_wdog_suspend,
			 st_wdog_resume);

static struct platform_driver st_wdog_driver = {
	.driver	= {
		.name = "st-lpc-wdt",
		.pm = &st_wdog_pm_ops,
		.of_match_table = st_wdog_match,
	},
	.probe = st_wdog_probe,
	.remove = st_wdog_remove,
};
module_platform_driver(st_wdog_driver);

MODULE_AUTHOR("David Paris <david.paris@st.com>");
MODULE_DESCRIPTION("ST LPC Watchdog Driver");
MODULE_LICENSE("GPL");
