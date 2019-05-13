/*
 * Copyright (c) 2016 Yang Ling <gnaygnil@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/watchdog.h>
#include <loongson1.h>

#define DEFAULT_HEARTBEAT	30

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0444);

static unsigned int heartbeat;
module_param(heartbeat, uint, 0444);

struct ls1x_wdt_drvdata {
	void __iomem *base;
	struct clk *clk;
	unsigned long clk_rate;
	struct watchdog_device wdt;
};

static int ls1x_wdt_ping(struct watchdog_device *wdt_dev)
{
	struct ls1x_wdt_drvdata *drvdata = watchdog_get_drvdata(wdt_dev);

	writel(0x1, drvdata->base + WDT_SET);

	return 0;
}

static int ls1x_wdt_set_timeout(struct watchdog_device *wdt_dev,
				unsigned int timeout)
{
	struct ls1x_wdt_drvdata *drvdata = watchdog_get_drvdata(wdt_dev);
	unsigned int max_hw_heartbeat = wdt_dev->max_hw_heartbeat_ms / 1000;
	unsigned int counts;

	wdt_dev->timeout = timeout;

	counts = drvdata->clk_rate * min(timeout, max_hw_heartbeat);
	writel(counts, drvdata->base + WDT_TIMER);

	return 0;
}

static int ls1x_wdt_start(struct watchdog_device *wdt_dev)
{
	struct ls1x_wdt_drvdata *drvdata = watchdog_get_drvdata(wdt_dev);

	writel(0x1, drvdata->base + WDT_EN);

	return 0;
}

static int ls1x_wdt_stop(struct watchdog_device *wdt_dev)
{
	struct ls1x_wdt_drvdata *drvdata = watchdog_get_drvdata(wdt_dev);

	writel(0x0, drvdata->base + WDT_EN);

	return 0;
}

static const struct watchdog_info ls1x_wdt_info = {
	.options = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE,
	.identity = "Loongson1 Watchdog",
};

static const struct watchdog_ops ls1x_wdt_ops = {
	.owner = THIS_MODULE,
	.start = ls1x_wdt_start,
	.stop = ls1x_wdt_stop,
	.ping = ls1x_wdt_ping,
	.set_timeout = ls1x_wdt_set_timeout,
};

static void ls1x_clk_disable_unprepare(void *data)
{
	clk_disable_unprepare(data);
}

static int ls1x_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ls1x_wdt_drvdata *drvdata;
	struct watchdog_device *ls1x_wdt;
	unsigned long clk_rate;
	int err;

	drvdata = devm_kzalloc(dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	drvdata->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(drvdata->base))
		return PTR_ERR(drvdata->base);

	drvdata->clk = devm_clk_get(dev, pdev->name);
	if (IS_ERR(drvdata->clk))
		return PTR_ERR(drvdata->clk);

	err = clk_prepare_enable(drvdata->clk);
	if (err) {
		dev_err(dev, "clk enable failed\n");
		return err;
	}
	err = devm_add_action_or_reset(dev, ls1x_clk_disable_unprepare,
				       drvdata->clk);
	if (err)
		return err;

	clk_rate = clk_get_rate(drvdata->clk);
	if (!clk_rate)
		return -EINVAL;
	drvdata->clk_rate = clk_rate;

	ls1x_wdt = &drvdata->wdt;
	ls1x_wdt->info = &ls1x_wdt_info;
	ls1x_wdt->ops = &ls1x_wdt_ops;
	ls1x_wdt->timeout = DEFAULT_HEARTBEAT;
	ls1x_wdt->min_timeout = 1;
	ls1x_wdt->max_hw_heartbeat_ms = U32_MAX / clk_rate * 1000;
	ls1x_wdt->parent = dev;

	watchdog_init_timeout(ls1x_wdt, heartbeat, dev);
	watchdog_set_nowayout(ls1x_wdt, nowayout);
	watchdog_set_drvdata(ls1x_wdt, drvdata);

	err = devm_watchdog_register_device(dev, &drvdata->wdt);
	if (err) {
		dev_err(dev, "failed to register watchdog device\n");
		return err;
	}

	platform_set_drvdata(pdev, drvdata);

	dev_info(dev, "Loongson1 Watchdog driver registered\n");

	return 0;
}

static struct platform_driver ls1x_wdt_driver = {
	.probe = ls1x_wdt_probe,
	.driver = {
		.name = "ls1x-wdt",
	},
};

module_platform_driver(ls1x_wdt_driver);

MODULE_AUTHOR("Yang Ling <gnaygnil@gmail.com>");
MODULE_DESCRIPTION("Loongson1 Watchdog Driver");
MODULE_LICENSE("GPL");
