// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) Nokia Corporation
 *
 * Written by Timo Kokkonen <timo.t.kokkonen at nokia.com>
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/watchdog.h>
#include <linux/platform_device.h>
#include <linux/mfd/twl.h>

#define TWL4030_WATCHDOG_CFG_REG_OFFS	0x3

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started "
	"(default=" __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

static int twl4030_wdt_write(unsigned char val)
{
	return twl_i2c_write_u8(TWL_MODULE_PM_RECEIVER, val,
					TWL4030_WATCHDOG_CFG_REG_OFFS);
}

static int twl4030_wdt_start(struct watchdog_device *wdt)
{
	return twl4030_wdt_write(wdt->timeout + 1);
}

static int twl4030_wdt_stop(struct watchdog_device *wdt)
{
	return twl4030_wdt_write(0);
}

static int twl4030_wdt_set_timeout(struct watchdog_device *wdt,
				   unsigned int timeout)
{
	wdt->timeout = timeout;
	return 0;
}

static const struct watchdog_info twl4030_wdt_info = {
	.options = WDIOF_SETTIMEOUT | WDIOF_MAGICCLOSE | WDIOF_KEEPALIVEPING,
	.identity = "TWL4030 Watchdog",
};

static const struct watchdog_ops twl4030_wdt_ops = {
	.owner		= THIS_MODULE,
	.start		= twl4030_wdt_start,
	.stop		= twl4030_wdt_stop,
	.set_timeout	= twl4030_wdt_set_timeout,
};

static int twl4030_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct watchdog_device *wdt;

	wdt = devm_kzalloc(dev, sizeof(*wdt), GFP_KERNEL);
	if (!wdt)
		return -ENOMEM;

	wdt->info		= &twl4030_wdt_info;
	wdt->ops		= &twl4030_wdt_ops;
	wdt->status		= 0;
	wdt->timeout		= 30;
	wdt->min_timeout	= 1;
	wdt->max_timeout	= 30;
	wdt->parent = dev;

	watchdog_set_nowayout(wdt, nowayout);
	platform_set_drvdata(pdev, wdt);

	twl4030_wdt_stop(wdt);

	return devm_watchdog_register_device(dev, wdt);
}

static int twl4030_wdt_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct watchdog_device *wdt = platform_get_drvdata(pdev);
	if (watchdog_active(wdt))
		return twl4030_wdt_stop(wdt);

	return 0;
}

static int twl4030_wdt_resume(struct platform_device *pdev)
{
	struct watchdog_device *wdt = platform_get_drvdata(pdev);
	if (watchdog_active(wdt))
		return twl4030_wdt_start(wdt);

	return 0;
}

static const struct of_device_id twl_wdt_of_match[] = {
	{ .compatible = "ti,twl4030-wdt", },
	{ },
};
MODULE_DEVICE_TABLE(of, twl_wdt_of_match);

static struct platform_driver twl4030_wdt_driver = {
	.probe		= twl4030_wdt_probe,
	.suspend	= pm_ptr(twl4030_wdt_suspend),
	.resume		= pm_ptr(twl4030_wdt_resume),
	.driver		= {
		.name		= "twl4030_wdt",
		.of_match_table	= twl_wdt_of_match,
	},
};

module_platform_driver(twl4030_wdt_driver);

MODULE_AUTHOR("Nokia Corporation");
MODULE_DESCRIPTION("TWL4030 Watchdog");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:twl4030_wdt");

