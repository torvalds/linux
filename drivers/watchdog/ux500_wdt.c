/*
 * Copyright (C) ST-Ericsson SA 2011-2013
 *
 * License Terms: GNU General Public License v2
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
#include <linux/platform_data/ux500_wdt.h>

#include <linux/mfd/dbx500-prcmu.h>

#define WATCHDOG_TIMEOUT 600 /* 10 minutes */

#define WATCHDOG_MIN	0
#define WATCHDOG_MAX28	268435  /* 28 bit resolution in ms == 268435.455 s */
#define WATCHDOG_MAX32	4294967 /* 32 bit resolution in ms == 4294967.295 s */

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

static int ux500_wdt_start(struct watchdog_device *wdd)
{
	return prcmu_enable_a9wdog(PRCMU_WDOG_ALL);
}

static int ux500_wdt_stop(struct watchdog_device *wdd)
{
	return prcmu_disable_a9wdog(PRCMU_WDOG_ALL);
}

static int ux500_wdt_keepalive(struct watchdog_device *wdd)
{
	return prcmu_kick_a9wdog(PRCMU_WDOG_ALL);
}

static int ux500_wdt_set_timeout(struct watchdog_device *wdd,
				 unsigned int timeout)
{
	ux500_wdt_stop(wdd);
	prcmu_load_a9wdog(PRCMU_WDOG_ALL, timeout * 1000);
	ux500_wdt_start(wdd);

	return 0;
}

static const struct watchdog_info ux500_wdt_info = {
	.options = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE,
	.identity = "Ux500 WDT",
	.firmware_version = 1,
};

static const struct watchdog_ops ux500_wdt_ops = {
	.owner = THIS_MODULE,
	.start = ux500_wdt_start,
	.stop  = ux500_wdt_stop,
	.ping  = ux500_wdt_keepalive,
	.set_timeout = ux500_wdt_set_timeout,
};

static struct watchdog_device ux500_wdt = {
	.info = &ux500_wdt_info,
	.ops = &ux500_wdt_ops,
	.min_timeout = WATCHDOG_MIN,
	.max_timeout = WATCHDOG_MAX32,
};

static int ux500_wdt_probe(struct platform_device *pdev)
{
	int ret;
	struct ux500_wdt_data *pdata = dev_get_platdata(&pdev->dev);

	if (pdata) {
		if (pdata->timeout > 0)
			timeout = pdata->timeout;
		if (pdata->has_28_bits_resolution)
			ux500_wdt.max_timeout = WATCHDOG_MAX28;
	}

	watchdog_set_nowayout(&ux500_wdt, nowayout);

	/* disable auto off on sleep */
	prcmu_config_a9wdog(PRCMU_WDOG_CPU1, false);

	/* set HW initial value */
	prcmu_load_a9wdog(PRCMU_WDOG_ALL, timeout * 1000);

	ret = watchdog_register_device(&ux500_wdt);
	if (ret)
		return ret;

	dev_info(&pdev->dev, "initialized\n");

	return 0;
}

static int ux500_wdt_remove(struct platform_device *dev)
{
	watchdog_unregister_device(&ux500_wdt);

	return 0;
}

#ifdef CONFIG_PM
static int ux500_wdt_suspend(struct platform_device *pdev,
			     pm_message_t state)
{
	if (watchdog_active(&ux500_wdt)) {
		ux500_wdt_stop(&ux500_wdt);
		prcmu_config_a9wdog(PRCMU_WDOG_CPU1, true);

		prcmu_load_a9wdog(PRCMU_WDOG_ALL, timeout * 1000);
		ux500_wdt_start(&ux500_wdt);
	}
	return 0;
}

static int ux500_wdt_resume(struct platform_device *pdev)
{
	if (watchdog_active(&ux500_wdt)) {
		ux500_wdt_stop(&ux500_wdt);
		prcmu_config_a9wdog(PRCMU_WDOG_CPU1, false);

		prcmu_load_a9wdog(PRCMU_WDOG_ALL, timeout * 1000);
		ux500_wdt_start(&ux500_wdt);
	}
	return 0;
}
#else
#define ux500_wdt_suspend NULL
#define ux500_wdt_resume NULL
#endif

static struct platform_driver ux500_wdt_driver = {
	.probe		= ux500_wdt_probe,
	.remove		= ux500_wdt_remove,
	.suspend	= ux500_wdt_suspend,
	.resume		= ux500_wdt_resume,
	.driver		= {
		.name	= "ux500_wdt",
	},
};

module_platform_driver(ux500_wdt_driver);

MODULE_AUTHOR("Jonas Aaberg <jonas.aberg@stericsson.com>");
MODULE_DESCRIPTION("Ux500 Watchdog Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ux500_wdt");
