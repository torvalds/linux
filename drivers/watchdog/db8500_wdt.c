// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) ST-Ericsson SA 2011-2013
 *
 * Author: Mathieu Poirier <mathieu.poirier@linaro.org> for ST-Ericsson
 * Author: Jonas Aaberg <jonas.aberg@stericsson.com> for ST-Ericsson
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/moduleparam.h>
#include <linux/err.h>
#include <linux/uaccess.h>
#include <linux/watchdog.h>
#include <linux/platform_device.h>

#include <linux/mfd/dbx500-prcmu.h>

#define WATCHDOG_TIMEOUT 600 /* 10 minutes */

#define WATCHDOG_MIN	0
#define WATCHDOG_MAX28	268435  /* 28 bit resolution in ms == 268435.455 s */

static unsigned int timeout = WATCHDOG_TIMEOUT;
module_param(timeout, uint, 0);
MODULE_PARM_DESC(timeout,
	"Watchdog timeout in seconds. default="
				__MODULE_STRING(WATCHDOG_TIMEOUT) ".");

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout,
	"Watchdog cannot be stopped once started (default="
				__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

static int db8500_wdt_start(struct watchdog_device *wdd)
{
	return prcmu_enable_a9wdog(PRCMU_WDOG_ALL);
}

static int db8500_wdt_stop(struct watchdog_device *wdd)
{
	return prcmu_disable_a9wdog(PRCMU_WDOG_ALL);
}

static int db8500_wdt_keepalive(struct watchdog_device *wdd)
{
	return prcmu_kick_a9wdog(PRCMU_WDOG_ALL);
}

static int db8500_wdt_set_timeout(struct watchdog_device *wdd,
				 unsigned int timeout)
{
	db8500_wdt_stop(wdd);
	prcmu_load_a9wdog(PRCMU_WDOG_ALL, timeout * 1000);
	db8500_wdt_start(wdd);

	return 0;
}

static const struct watchdog_info db8500_wdt_info = {
	.options = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE,
	.identity = "DB8500 WDT",
	.firmware_version = 1,
};

static const struct watchdog_ops db8500_wdt_ops = {
	.owner = THIS_MODULE,
	.start = db8500_wdt_start,
	.stop  = db8500_wdt_stop,
	.ping  = db8500_wdt_keepalive,
	.set_timeout = db8500_wdt_set_timeout,
};

static struct watchdog_device db8500_wdt = {
	.info = &db8500_wdt_info,
	.ops = &db8500_wdt_ops,
	.min_timeout = WATCHDOG_MIN,
	.max_timeout = WATCHDOG_MAX28,
};

static int db8500_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret;

	timeout = 600; /* Default to 10 minutes */
	db8500_wdt.parent = dev;
	watchdog_set_nowayout(&db8500_wdt, nowayout);

	/* disable auto off on sleep */
	prcmu_config_a9wdog(PRCMU_WDOG_CPU1, false);

	/* set HW initial value */
	prcmu_load_a9wdog(PRCMU_WDOG_ALL, timeout * 1000);

	ret = devm_watchdog_register_device(dev, &db8500_wdt);
	if (ret)
		return ret;

	dev_info(dev, "initialized\n");

	return 0;
}

#ifdef CONFIG_PM
static int db8500_wdt_suspend(struct platform_device *pdev,
			     pm_message_t state)
{
	if (watchdog_active(&db8500_wdt)) {
		db8500_wdt_stop(&db8500_wdt);
		prcmu_config_a9wdog(PRCMU_WDOG_CPU1, true);

		prcmu_load_a9wdog(PRCMU_WDOG_ALL, timeout * 1000);
		db8500_wdt_start(&db8500_wdt);
	}
	return 0;
}

static int db8500_wdt_resume(struct platform_device *pdev)
{
	if (watchdog_active(&db8500_wdt)) {
		db8500_wdt_stop(&db8500_wdt);
		prcmu_config_a9wdog(PRCMU_WDOG_CPU1, false);

		prcmu_load_a9wdog(PRCMU_WDOG_ALL, timeout * 1000);
		db8500_wdt_start(&db8500_wdt);
	}
	return 0;
}
#else
#define db8500_wdt_suspend NULL
#define db8500_wdt_resume NULL
#endif

static struct platform_driver db8500_wdt_driver = {
	.probe		= db8500_wdt_probe,
	.suspend	= db8500_wdt_suspend,
	.resume		= db8500_wdt_resume,
	.driver		= {
		.name	= "db8500_wdt",
	},
};

module_platform_driver(db8500_wdt_driver);

MODULE_AUTHOR("Jonas Aaberg <jonas.aberg@stericsson.com>");
MODULE_DESCRIPTION("DB8500 Watchdog Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:db8500_wdt");
