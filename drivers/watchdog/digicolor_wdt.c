/*
 * Watchdog driver for Conexant Digicolor
 *
 * Copyright (C) 2015 Paradox Innovation Ltd.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/watchdog.h>
#include <linux/platform_device.h>
#include <linux/of_address.h>

#define TIMER_A_CONTROL		0
#define TIMER_A_COUNT		4

#define TIMER_A_ENABLE_COUNT	BIT(0)
#define TIMER_A_ENABLE_WATCHDOG	BIT(1)

struct dc_wdt {
	void __iomem		*base;
	struct clk		*clk;
	spinlock_t		lock;
};

static unsigned timeout;
module_param(timeout, uint, 0);
MODULE_PARM_DESC(timeout, "Watchdog timeout in seconds");

static void dc_wdt_set(struct dc_wdt *wdt, u32 ticks)
{
	unsigned long flags;

	spin_lock_irqsave(&wdt->lock, flags);

	writel_relaxed(0, wdt->base + TIMER_A_CONTROL);
	writel_relaxed(ticks, wdt->base + TIMER_A_COUNT);
	writel_relaxed(TIMER_A_ENABLE_COUNT | TIMER_A_ENABLE_WATCHDOG,
		       wdt->base + TIMER_A_CONTROL);

	spin_unlock_irqrestore(&wdt->lock, flags);
}

static int dc_wdt_restart(struct watchdog_device *wdog, unsigned long action,
			  void *data)
{
	struct dc_wdt *wdt = watchdog_get_drvdata(wdog);

	dc_wdt_set(wdt, 1);
	/* wait for reset to assert... */
	mdelay(500);

	return 0;
}

static int dc_wdt_start(struct watchdog_device *wdog)
{
	struct dc_wdt *wdt = watchdog_get_drvdata(wdog);

	dc_wdt_set(wdt, wdog->timeout * clk_get_rate(wdt->clk));

	return 0;
}

static int dc_wdt_stop(struct watchdog_device *wdog)
{
	struct dc_wdt *wdt = watchdog_get_drvdata(wdog);

	writel_relaxed(0, wdt->base + TIMER_A_CONTROL);

	return 0;
}

static int dc_wdt_set_timeout(struct watchdog_device *wdog, unsigned int t)
{
	struct dc_wdt *wdt = watchdog_get_drvdata(wdog);

	dc_wdt_set(wdt, t * clk_get_rate(wdt->clk));
	wdog->timeout = t;

	return 0;
}

static unsigned int dc_wdt_get_timeleft(struct watchdog_device *wdog)
{
	struct dc_wdt *wdt = watchdog_get_drvdata(wdog);
	uint32_t count = readl_relaxed(wdt->base + TIMER_A_COUNT);

	return count / clk_get_rate(wdt->clk);
}

static const struct watchdog_ops dc_wdt_ops = {
	.owner		= THIS_MODULE,
	.start		= dc_wdt_start,
	.stop		= dc_wdt_stop,
	.set_timeout	= dc_wdt_set_timeout,
	.get_timeleft	= dc_wdt_get_timeleft,
	.restart        = dc_wdt_restart,
};

static const struct watchdog_info dc_wdt_info = {
	.options	= WDIOF_SETTIMEOUT | WDIOF_MAGICCLOSE
			| WDIOF_KEEPALIVEPING,
	.identity	= "Conexant Digicolor Watchdog",
};

static struct watchdog_device dc_wdt_wdd = {
	.info		= &dc_wdt_info,
	.ops		= &dc_wdt_ops,
	.min_timeout	= 1,
};

static int dc_wdt_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct device *dev = &pdev->dev;
	struct dc_wdt *wdt;
	int ret;

	wdt = devm_kzalloc(dev, sizeof(struct dc_wdt), GFP_KERNEL);
	if (!wdt)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	wdt->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(wdt->base))
		return PTR_ERR(wdt->base);

	wdt->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(wdt->clk))
		return PTR_ERR(wdt->clk);
	dc_wdt_wdd.max_timeout = U32_MAX / clk_get_rate(wdt->clk);
	dc_wdt_wdd.timeout = dc_wdt_wdd.max_timeout;
	dc_wdt_wdd.parent = dev;

	spin_lock_init(&wdt->lock);

	watchdog_set_drvdata(&dc_wdt_wdd, wdt);
	watchdog_set_restart_priority(&dc_wdt_wdd, 128);
	watchdog_init_timeout(&dc_wdt_wdd, timeout, dev);
	watchdog_stop_on_reboot(&dc_wdt_wdd);
	ret = devm_watchdog_register_device(dev, &dc_wdt_wdd);
	if (ret) {
		dev_err(dev, "Failed to register watchdog device");
		return ret;
	}

	return 0;
}

static const struct of_device_id dc_wdt_of_match[] = {
	{ .compatible = "cnxt,cx92755-wdt", },
	{},
};
MODULE_DEVICE_TABLE(of, dc_wdt_of_match);

static struct platform_driver dc_wdt_driver = {
	.probe		= dc_wdt_probe,
	.driver = {
		.name =		"digicolor-wdt",
		.of_match_table = dc_wdt_of_match,
	},
};
module_platform_driver(dc_wdt_driver);

MODULE_AUTHOR("Baruch Siach <baruch@tkos.co.il>");
MODULE_DESCRIPTION("Driver for Conexant Digicolor watchdog timer");
MODULE_LICENSE("GPL");
