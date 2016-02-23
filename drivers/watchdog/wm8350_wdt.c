/*
 * Watchdog driver for the wm8350
 *
 * Copyright (C) 2007, 2008 Wolfson Microelectronics <linux@wolfsonmicro.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/watchdog.h>
#include <linux/uaccess.h>
#include <linux/mfd/wm8350/core.h>

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout,
		 "Watchdog cannot be stopped once started (default="
		 __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

static DEFINE_MUTEX(wdt_mutex);

static struct {
	unsigned int time;  /* Seconds */
	u16 val;	    /* To be set in WM8350_SYSTEM_CONTROL_2 */
} wm8350_wdt_cfgs[] = {
	{ 1, 0x02 },
	{ 2, 0x04 },
	{ 4, 0x05 },
};

static int wm8350_wdt_set_timeout(struct watchdog_device *wdt_dev,
				  unsigned int timeout)
{
	struct wm8350 *wm8350 = watchdog_get_drvdata(wdt_dev);
	int ret, i;
	u16 reg;

	for (i = 0; i < ARRAY_SIZE(wm8350_wdt_cfgs); i++)
		if (wm8350_wdt_cfgs[i].time == timeout)
			break;
	if (i == ARRAY_SIZE(wm8350_wdt_cfgs))
		return -EINVAL;

	mutex_lock(&wdt_mutex);
	wm8350_reg_unlock(wm8350);

	reg = wm8350_reg_read(wm8350, WM8350_SYSTEM_CONTROL_2);
	reg &= ~WM8350_WDOG_TO_MASK;
	reg |= wm8350_wdt_cfgs[i].val;
	ret = wm8350_reg_write(wm8350, WM8350_SYSTEM_CONTROL_2, reg);

	wm8350_reg_lock(wm8350);
	mutex_unlock(&wdt_mutex);

	wdt_dev->timeout = timeout;
	return ret;
}

static int wm8350_wdt_start(struct watchdog_device *wdt_dev)
{
	struct wm8350 *wm8350 = watchdog_get_drvdata(wdt_dev);
	int ret;
	u16 reg;

	mutex_lock(&wdt_mutex);
	wm8350_reg_unlock(wm8350);

	reg = wm8350_reg_read(wm8350, WM8350_SYSTEM_CONTROL_2);
	reg &= ~WM8350_WDOG_MODE_MASK;
	reg |= 0x20;
	ret = wm8350_reg_write(wm8350, WM8350_SYSTEM_CONTROL_2, reg);

	wm8350_reg_lock(wm8350);
	mutex_unlock(&wdt_mutex);

	return ret;
}

static int wm8350_wdt_stop(struct watchdog_device *wdt_dev)
{
	struct wm8350 *wm8350 = watchdog_get_drvdata(wdt_dev);
	int ret;
	u16 reg;

	mutex_lock(&wdt_mutex);
	wm8350_reg_unlock(wm8350);

	reg = wm8350_reg_read(wm8350, WM8350_SYSTEM_CONTROL_2);
	reg &= ~WM8350_WDOG_MODE_MASK;
	ret = wm8350_reg_write(wm8350, WM8350_SYSTEM_CONTROL_2, reg);

	wm8350_reg_lock(wm8350);
	mutex_unlock(&wdt_mutex);

	return ret;
}

static int wm8350_wdt_ping(struct watchdog_device *wdt_dev)
{
	struct wm8350 *wm8350 = watchdog_get_drvdata(wdt_dev);
	int ret;
	u16 reg;

	mutex_lock(&wdt_mutex);

	reg = wm8350_reg_read(wm8350, WM8350_SYSTEM_CONTROL_2);
	ret = wm8350_reg_write(wm8350, WM8350_SYSTEM_CONTROL_2, reg);

	mutex_unlock(&wdt_mutex);

	return ret;
}

static const struct watchdog_info wm8350_wdt_info = {
	.options = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE,
	.identity = "WM8350 Watchdog",
};

static const struct watchdog_ops wm8350_wdt_ops = {
	.owner = THIS_MODULE,
	.start = wm8350_wdt_start,
	.stop = wm8350_wdt_stop,
	.ping = wm8350_wdt_ping,
	.set_timeout = wm8350_wdt_set_timeout,
};

static struct watchdog_device wm8350_wdt = {
	.info = &wm8350_wdt_info,
	.ops = &wm8350_wdt_ops,
	.timeout = 4,
	.min_timeout = 1,
	.max_timeout = 4,
};

static int wm8350_wdt_probe(struct platform_device *pdev)
{
	struct wm8350 *wm8350 = platform_get_drvdata(pdev);

	if (!wm8350) {
		pr_err("No driver data supplied\n");
		return -ENODEV;
	}

	watchdog_set_nowayout(&wm8350_wdt, nowayout);
	watchdog_set_drvdata(&wm8350_wdt, wm8350);
	wm8350_wdt.parent = &pdev->dev;

	/* Default to 4s timeout */
	wm8350_wdt_set_timeout(&wm8350_wdt, 4);

	return watchdog_register_device(&wm8350_wdt);
}

static int wm8350_wdt_remove(struct platform_device *pdev)
{
	watchdog_unregister_device(&wm8350_wdt);
	return 0;
}

static struct platform_driver wm8350_wdt_driver = {
	.probe = wm8350_wdt_probe,
	.remove = wm8350_wdt_remove,
	.driver = {
		.name = "wm8350-wdt",
	},
};

module_platform_driver(wm8350_wdt_driver);

MODULE_AUTHOR("Mark Brown");
MODULE_DESCRIPTION("WM8350 Watchdog");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:wm8350-wdt");
