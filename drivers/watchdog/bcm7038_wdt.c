/*
 * Copyright (C) 2015 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/watchdog.h>

#define WDT_START_1		0xff00
#define WDT_START_2		0x00ff
#define WDT_STOP_1		0xee00
#define WDT_STOP_2		0x00ee

#define WDT_TIMEOUT_REG		0x0
#define WDT_CMD_REG		0x4

#define WDT_MIN_TIMEOUT		1 /* seconds */
#define WDT_DEFAULT_TIMEOUT	30 /* seconds */
#define WDT_DEFAULT_RATE	27000000

struct bcm7038_watchdog {
	void __iomem		*base;
	struct watchdog_device	wdd;
	u32			rate;
	struct clk		*clk;
};

static bool nowayout = WATCHDOG_NOWAYOUT;

static void bcm7038_wdt_set_timeout_reg(struct watchdog_device *wdog)
{
	struct bcm7038_watchdog *wdt = watchdog_get_drvdata(wdog);
	u32 timeout;

	timeout = wdt->rate * wdog->timeout;

	writel(timeout, wdt->base + WDT_TIMEOUT_REG);
}

static int bcm7038_wdt_ping(struct watchdog_device *wdog)
{
	struct bcm7038_watchdog *wdt = watchdog_get_drvdata(wdog);

	writel(WDT_START_1, wdt->base + WDT_CMD_REG);
	writel(WDT_START_2, wdt->base + WDT_CMD_REG);

	return 0;
}

static int bcm7038_wdt_start(struct watchdog_device *wdog)
{
	bcm7038_wdt_set_timeout_reg(wdog);
	bcm7038_wdt_ping(wdog);

	return 0;
}

static int bcm7038_wdt_stop(struct watchdog_device *wdog)
{
	struct bcm7038_watchdog *wdt = watchdog_get_drvdata(wdog);

	writel(WDT_STOP_1, wdt->base + WDT_CMD_REG);
	writel(WDT_STOP_2, wdt->base + WDT_CMD_REG);

	return 0;
}

static int bcm7038_wdt_set_timeout(struct watchdog_device *wdog,
				   unsigned int t)
{
	/* Can't modify timeout value if watchdog timer is running */
	bcm7038_wdt_stop(wdog);
	wdog->timeout = t;
	bcm7038_wdt_start(wdog);

	return 0;
}

static unsigned int bcm7038_wdt_get_timeleft(struct watchdog_device *wdog)
{
	struct bcm7038_watchdog *wdt = watchdog_get_drvdata(wdog);
	u32 time_left;

	time_left = readl(wdt->base + WDT_CMD_REG);

	return time_left / wdt->rate;
}

static struct watchdog_info bcm7038_wdt_info = {
	.identity	= "Broadcom BCM7038 Watchdog Timer",
	.options	= WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING |
				WDIOF_MAGICCLOSE
};

static struct watchdog_ops bcm7038_wdt_ops = {
	.owner		= THIS_MODULE,
	.start		= bcm7038_wdt_start,
	.stop		= bcm7038_wdt_stop,
	.set_timeout	= bcm7038_wdt_set_timeout,
	.get_timeleft	= bcm7038_wdt_get_timeleft,
};

static int bcm7038_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct bcm7038_watchdog *wdt;
	struct resource *res;
	int err;

	wdt = devm_kzalloc(dev, sizeof(*wdt), GFP_KERNEL);
	if (!wdt)
		return -ENOMEM;

	platform_set_drvdata(pdev, wdt);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	wdt->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(wdt->base))
		return PTR_ERR(wdt->base);

	wdt->clk = devm_clk_get(dev, NULL);
	/* If unable to get clock, use default frequency */
	if (!IS_ERR(wdt->clk)) {
		clk_prepare_enable(wdt->clk);
		wdt->rate = clk_get_rate(wdt->clk);
		/* Prevent divide-by-zero exception */
		if (!wdt->rate)
			wdt->rate = WDT_DEFAULT_RATE;
	} else {
		wdt->rate = WDT_DEFAULT_RATE;
		wdt->clk = NULL;
	}

	wdt->wdd.info		= &bcm7038_wdt_info;
	wdt->wdd.ops		= &bcm7038_wdt_ops;
	wdt->wdd.min_timeout	= WDT_MIN_TIMEOUT;
	wdt->wdd.timeout	= WDT_DEFAULT_TIMEOUT;
	wdt->wdd.max_timeout	= 0xffffffff / wdt->rate;
	wdt->wdd.parent		= dev;
	watchdog_set_drvdata(&wdt->wdd, wdt);

	err = watchdog_register_device(&wdt->wdd);
	if (err) {
		dev_err(dev, "Failed to register watchdog device\n");
		clk_disable_unprepare(wdt->clk);
		return err;
	}

	dev_info(dev, "Registered BCM7038 Watchdog\n");

	return 0;
}

static int bcm7038_wdt_remove(struct platform_device *pdev)
{
	struct bcm7038_watchdog *wdt = platform_get_drvdata(pdev);

	if (!nowayout)
		bcm7038_wdt_stop(&wdt->wdd);

	watchdog_unregister_device(&wdt->wdd);
	clk_disable_unprepare(wdt->clk);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int bcm7038_wdt_suspend(struct device *dev)
{
	struct bcm7038_watchdog *wdt = dev_get_drvdata(dev);

	if (watchdog_active(&wdt->wdd))
		return bcm7038_wdt_stop(&wdt->wdd);

	return 0;
}

static int bcm7038_wdt_resume(struct device *dev)
{
	struct bcm7038_watchdog *wdt = dev_get_drvdata(dev);

	if (watchdog_active(&wdt->wdd))
		return bcm7038_wdt_start(&wdt->wdd);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(bcm7038_wdt_pm_ops, bcm7038_wdt_suspend,
			 bcm7038_wdt_resume);

static void bcm7038_wdt_shutdown(struct platform_device *pdev)
{
	struct bcm7038_watchdog *wdt = platform_get_drvdata(pdev);

	if (watchdog_active(&wdt->wdd))
		bcm7038_wdt_stop(&wdt->wdd);
}

static const struct of_device_id bcm7038_wdt_match[] = {
	{ .compatible = "brcm,bcm7038-wdt" },
	{},
};

static struct platform_driver bcm7038_wdt_driver = {
	.probe		= bcm7038_wdt_probe,
	.remove		= bcm7038_wdt_remove,
	.shutdown	= bcm7038_wdt_shutdown,
	.driver		= {
		.name		= "bcm7038-wdt",
		.of_match_table	= bcm7038_wdt_match,
		.pm		= &bcm7038_wdt_pm_ops,
	}
};
module_platform_driver(bcm7038_wdt_driver);

module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
	__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Driver for Broadcom 7038 SoCs Watchdog");
MODULE_AUTHOR("Justin Chen");
