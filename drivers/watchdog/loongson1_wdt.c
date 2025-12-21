// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2016 Yang Ling <gnaygnil@gmail.com>
 * Copyright (C) 2025 Binbin Zhou <zhoubinbin@loongson.cn>
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/watchdog.h>

/* Loongson Watchdog Register Definitions */
#define WDT_EN			0x0

#define DEFAULT_HEARTBEAT	30

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
		 __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

static unsigned int heartbeat;
module_param(heartbeat, uint, 0);
MODULE_PARM_DESC(heartbeat, "Watchdog heartbeat in seconds. (default="
		 __MODULE_STRING(DEFAULT_HEARTBEAT) ")");

struct ls1x_wdt_pdata {
	u32 timer_offset;
	u32 set_offset;
	u32 wdt_en_bit;
};

static const struct ls1x_wdt_pdata ls1b_wdt_pdata = {
	.timer_offset = 0x4,
	.set_offset = 0x8,
	.wdt_en_bit = BIT(0),
};

static const struct ls1x_wdt_pdata ls2k0300_wdt_pdata = {
	.timer_offset = 0x8,
	.set_offset = 0x4,
	.wdt_en_bit = BIT(1),
};

struct ls1x_wdt_drvdata {
	void __iomem *base;
	struct clk *clk;
	unsigned long clk_rate;
	struct watchdog_device wdt;
	const struct ls1x_wdt_pdata *pdata;
};

static int ls1x_wdt_ping(struct watchdog_device *wdt_dev)
{
	struct ls1x_wdt_drvdata *drvdata = watchdog_get_drvdata(wdt_dev);

	writel(0x1, drvdata->base + drvdata->pdata->set_offset);

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
	writel(counts, drvdata->base + drvdata->pdata->timer_offset);

	return 0;
}

static int ls1x_wdt_start(struct watchdog_device *wdt_dev)
{
	struct ls1x_wdt_drvdata *drvdata = watchdog_get_drvdata(wdt_dev);

	writel(drvdata->pdata->wdt_en_bit, drvdata->base + WDT_EN);

	return 0;
}

static int ls1x_wdt_stop(struct watchdog_device *wdt_dev)
{
	struct ls1x_wdt_drvdata *drvdata = watchdog_get_drvdata(wdt_dev);
	u32 val = readl(drvdata->base + WDT_EN);

	val &= ~(drvdata->pdata->wdt_en_bit);
	writel(val, drvdata->base + WDT_EN);

	return 0;
}

static int ls1x_wdt_restart(struct watchdog_device *wdt_dev,
			    unsigned long action, void *data)
{
	struct ls1x_wdt_drvdata *drvdata = watchdog_get_drvdata(wdt_dev);

	writel(drvdata->pdata->wdt_en_bit, drvdata->base + WDT_EN);
	writel(0x1, drvdata->base + drvdata->pdata->timer_offset);
	writel(0x1, drvdata->base + drvdata->pdata->set_offset);

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
	.restart = ls1x_wdt_restart,
};

static int ls1x_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ls1x_wdt_drvdata *drvdata;
	struct watchdog_device *ls1x_wdt;
	unsigned long clk_rate;

	drvdata = devm_kzalloc(dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;
	platform_set_drvdata(pdev, drvdata);

	drvdata->pdata = of_device_get_match_data(dev);

	drvdata->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(drvdata->base))
		return PTR_ERR(drvdata->base);

	drvdata->clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(drvdata->clk))
		return PTR_ERR(drvdata->clk);

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

	return devm_watchdog_register_device(dev, &drvdata->wdt);
}

static int ls1x_wdt_resume(struct device *dev)
{
	struct ls1x_wdt_drvdata *data = dev_get_drvdata(dev);

	if (watchdog_active(&data->wdt))
		ls1x_wdt_start(&data->wdt);

	return 0;
}

static int ls1x_wdt_suspend(struct device *dev)
{
	struct ls1x_wdt_drvdata *data = dev_get_drvdata(dev);

	if (watchdog_active(&data->wdt))
		ls1x_wdt_stop(&data->wdt);

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(ls1x_wdt_pm_ops, ls1x_wdt_suspend, ls1x_wdt_resume);

static const struct of_device_id ls1x_wdt_dt_ids[] = {
	{ .compatible = "loongson,ls1b-wdt", .data = &ls1b_wdt_pdata },
	{ .compatible = "loongson,ls1c-wdt", .data = &ls1b_wdt_pdata },
	{ .compatible = "loongson,ls2k0300-wdt", .data = &ls2k0300_wdt_pdata },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ls1x_wdt_dt_ids);

static struct platform_driver ls1x_wdt_driver = {
	.probe = ls1x_wdt_probe,
	.driver = {
		.name = "ls1x-wdt",
		.of_match_table = ls1x_wdt_dt_ids,
		.pm = pm_ptr(&ls1x_wdt_pm_ops),
	},
};

module_platform_driver(ls1x_wdt_driver);

MODULE_AUTHOR("Yang Ling <gnaygnil@gmail.com>");
MODULE_AUTHOR("Binbin Zhou <zhoubinbin@loongson.cn>");
MODULE_DESCRIPTION("Loongson Watchdog Driver");
MODULE_LICENSE("GPL");
