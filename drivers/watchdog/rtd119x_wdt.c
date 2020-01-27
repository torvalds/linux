/*
 * Realtek RTD129x watchdog
 *
 * Copyright (c) 2017 Andreas Färber
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/watchdog.h>

#define RTD119X_TCWCR		0x0
#define RTD119X_TCWTR		0x4
#define RTD119X_TCWOV		0xc

#define RTD119X_TCWCR_WDEN_DISABLED		0xa5
#define RTD119X_TCWCR_WDEN_ENABLED		0xff
#define RTD119X_TCWCR_WDEN_MASK			0xff

#define RTD119X_TCWTR_WDCLR			BIT(0)

struct rtd119x_watchdog_device {
	struct watchdog_device wdt_dev;
	void __iomem *base;
	struct clk *clk;
};

static int rtd119x_wdt_start(struct watchdog_device *wdev)
{
	struct rtd119x_watchdog_device *data = watchdog_get_drvdata(wdev);
	u32 val;

	val = readl_relaxed(data->base + RTD119X_TCWCR);
	val &= ~RTD119X_TCWCR_WDEN_MASK;
	val |= RTD119X_TCWCR_WDEN_ENABLED;
	writel(val, data->base + RTD119X_TCWCR);

	return 0;
}

static int rtd119x_wdt_stop(struct watchdog_device *wdev)
{
	struct rtd119x_watchdog_device *data = watchdog_get_drvdata(wdev);
	u32 val;

	val = readl_relaxed(data->base + RTD119X_TCWCR);
	val &= ~RTD119X_TCWCR_WDEN_MASK;
	val |= RTD119X_TCWCR_WDEN_DISABLED;
	writel(val, data->base + RTD119X_TCWCR);

	return 0;
}

static int rtd119x_wdt_ping(struct watchdog_device *wdev)
{
	struct rtd119x_watchdog_device *data = watchdog_get_drvdata(wdev);

	writel_relaxed(RTD119X_TCWTR_WDCLR, data->base + RTD119X_TCWTR);

	return rtd119x_wdt_start(wdev);
}

static int rtd119x_wdt_set_timeout(struct watchdog_device *wdev, unsigned int val)
{
	struct rtd119x_watchdog_device *data = watchdog_get_drvdata(wdev);

	writel(val * clk_get_rate(data->clk), data->base + RTD119X_TCWOV);

	data->wdt_dev.timeout = val;

	return 0;
}

static const struct watchdog_ops rtd119x_wdt_ops = {
	.owner = THIS_MODULE,
	.start		= rtd119x_wdt_start,
	.stop		= rtd119x_wdt_stop,
	.ping		= rtd119x_wdt_ping,
	.set_timeout	= rtd119x_wdt_set_timeout,
};

static const struct watchdog_info rtd119x_wdt_info = {
	.identity = "rtd119x-wdt",
	.options = 0,
};

static const struct of_device_id rtd119x_wdt_dt_ids[] = {
	 { .compatible = "realtek,rtd1295-watchdog" },
	 { }
};

static int rtd119x_wdt_probe(struct platform_device *pdev)
{
	struct rtd119x_watchdog_device *data;
	struct resource *res;
	int ret;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	data->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(data->base))
		return PTR_ERR(data->base);

	data->clk = of_clk_get(pdev->dev.of_node, 0);
	if (IS_ERR(data->clk))
		return PTR_ERR(data->clk);

	ret = clk_prepare_enable(data->clk);
	if (ret) {
		clk_put(data->clk);
		return ret;
	}

	data->wdt_dev.info = &rtd119x_wdt_info;
	data->wdt_dev.ops = &rtd119x_wdt_ops;
	data->wdt_dev.timeout = 120;
	data->wdt_dev.max_timeout = 0xffffffff / clk_get_rate(data->clk);
	data->wdt_dev.min_timeout = 1;
	data->wdt_dev.parent = &pdev->dev;

	watchdog_stop_on_reboot(&data->wdt_dev);
	watchdog_set_drvdata(&data->wdt_dev, data);
	platform_set_drvdata(pdev, data);

	writel_relaxed(RTD119X_TCWTR_WDCLR, data->base + RTD119X_TCWTR);
	rtd119x_wdt_set_timeout(&data->wdt_dev, data->wdt_dev.timeout);
	rtd119x_wdt_stop(&data->wdt_dev);

	ret = watchdog_register_device(&data->wdt_dev);
	if (ret) {
		clk_disable_unprepare(data->clk);
		clk_put(data->clk);
		return ret;
	}

	return 0;
}

static int rtd119x_wdt_remove(struct platform_device *pdev)
{
	struct rtd119x_watchdog_device *data = platform_get_drvdata(pdev);

	watchdog_unregister_device(&data->wdt_dev);

	clk_disable_unprepare(data->clk);
	clk_put(data->clk);

	return 0;
}

static struct platform_driver rtd119x_wdt_driver = {
	.probe = rtd119x_wdt_probe,
	.remove = rtd119x_wdt_remove,
	.driver = {
		.name = "rtd1295-watchdog",
		.of_match_table	= rtd119x_wdt_dt_ids,
	},
};
builtin_platform_driver(rtd119x_wdt_driver);
