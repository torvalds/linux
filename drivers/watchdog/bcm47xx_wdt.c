/*
 *  Watchdog driver for Broadcom BCM47XX
 *
 *  Copyright (C) 2008 Aleksandar Radovanovic <biblbroks@sezampro.rs>
 *  Copyright (C) 2009 Matthieu CASTET <castet.matthieu@free.fr>
 *  Copyright (C) 2012-2013 Hauke Mehrtens <hauke@hauke-m.de>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/bcm47xx_wdt.h>
#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/types.h>
#include <linux/watchdog.h>
#include <linux/timer.h>
#include <linux/jiffies.h>

#define DRV_NAME		"bcm47xx_wdt"

#define WDT_DEFAULT_TIME	30	/* seconds */
#define WDT_SOFTTIMER_MAX	255	/* seconds */

static int timeout = WDT_DEFAULT_TIME;
static bool nowayout = WATCHDOG_NOWAYOUT;

module_param(timeout, int, 0);
MODULE_PARM_DESC(timeout, "Watchdog time in seconds. (default="
				__MODULE_STRING(WDT_DEFAULT_TIME) ")");

module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout,
		"Watchdog cannot be stopped once started (default="
				__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

static inline struct bcm47xx_wdt *bcm47xx_wdt_get(struct watchdog_device *wdd)
{
	return container_of(wdd, struct bcm47xx_wdt, wdd);
}

static void bcm47xx_wdt_soft_timer_tick(unsigned long data)
{
	struct bcm47xx_wdt *wdt = (struct bcm47xx_wdt *)data;
	u32 next_tick = min(wdt->wdd.timeout * 1000, wdt->max_timer_ms);

	if (!atomic_dec_and_test(&wdt->soft_ticks)) {
		wdt->timer_set_ms(wdt, next_tick);
		mod_timer(&wdt->soft_timer, jiffies + HZ);
	} else {
		pr_crit("Watchdog will fire soon!!!\n");
	}
}

static int bcm47xx_wdt_soft_keepalive(struct watchdog_device *wdd)
{
	struct bcm47xx_wdt *wdt = bcm47xx_wdt_get(wdd);

	atomic_set(&wdt->soft_ticks, wdd->timeout);

	return 0;
}

static int bcm47xx_wdt_soft_start(struct watchdog_device *wdd)
{
	struct bcm47xx_wdt *wdt = bcm47xx_wdt_get(wdd);

	bcm47xx_wdt_soft_keepalive(wdd);
	bcm47xx_wdt_soft_timer_tick((unsigned long)wdt);

	return 0;
}

static int bcm47xx_wdt_soft_stop(struct watchdog_device *wdd)
{
	struct bcm47xx_wdt *wdt = bcm47xx_wdt_get(wdd);

	del_timer_sync(&wdt->soft_timer);
	wdt->timer_set(wdt, 0);

	return 0;
}

static int bcm47xx_wdt_soft_set_timeout(struct watchdog_device *wdd,
					unsigned int new_time)
{
	if (new_time < 1 || new_time > WDT_SOFTTIMER_MAX) {
		pr_warn("timeout value must be 1<=x<=%d, using %d\n",
			WDT_SOFTTIMER_MAX, new_time);
		return -EINVAL;
	}

	wdd->timeout = new_time;
	return 0;
}

static const struct watchdog_info bcm47xx_wdt_info = {
	.identity	= DRV_NAME,
	.options	= WDIOF_SETTIMEOUT |
				WDIOF_KEEPALIVEPING |
				WDIOF_MAGICCLOSE,
};

static int bcm47xx_wdt_notify_sys(struct notifier_block *this,
				  unsigned long code, void *unused)
{
	struct bcm47xx_wdt *wdt;

	wdt = container_of(this, struct bcm47xx_wdt, notifier);
	if (code == SYS_DOWN || code == SYS_HALT)
		wdt->wdd.ops->stop(&wdt->wdd);
	return NOTIFY_DONE;
}

static struct watchdog_ops bcm47xx_wdt_soft_ops = {
	.owner		= THIS_MODULE,
	.start		= bcm47xx_wdt_soft_start,
	.stop		= bcm47xx_wdt_soft_stop,
	.ping		= bcm47xx_wdt_soft_keepalive,
	.set_timeout	= bcm47xx_wdt_soft_set_timeout,
};

static int bcm47xx_wdt_probe(struct platform_device *pdev)
{
	int ret;
	struct bcm47xx_wdt *wdt = dev_get_platdata(&pdev->dev);

	if (!wdt)
		return -ENXIO;

	setup_timer(&wdt->soft_timer, bcm47xx_wdt_soft_timer_tick,
		    (long unsigned int)wdt);

	wdt->wdd.ops = &bcm47xx_wdt_soft_ops;
	wdt->wdd.info = &bcm47xx_wdt_info;
	wdt->wdd.timeout = WDT_DEFAULT_TIME;
	ret = wdt->wdd.ops->set_timeout(&wdt->wdd, timeout);
	if (ret)
		goto err_timer;
	watchdog_set_nowayout(&wdt->wdd, nowayout);

	wdt->notifier.notifier_call = &bcm47xx_wdt_notify_sys;

	ret = register_reboot_notifier(&wdt->notifier);
	if (ret)
		goto err_timer;

	ret = watchdog_register_device(&wdt->wdd);
	if (ret)
		goto err_notifier;

	pr_info("BCM47xx Watchdog Timer enabled (%d seconds%s)\n",
		timeout, nowayout ? ", nowayout" : "");
	return 0;

err_notifier:
	unregister_reboot_notifier(&wdt->notifier);
err_timer:
	del_timer_sync(&wdt->soft_timer);

	return ret;
}

static int bcm47xx_wdt_remove(struct platform_device *pdev)
{
	struct bcm47xx_wdt *wdt = dev_get_platdata(&pdev->dev);

	if (!wdt)
		return -ENXIO;

	watchdog_unregister_device(&wdt->wdd);
	unregister_reboot_notifier(&wdt->notifier);

	return 0;
}

static struct platform_driver bcm47xx_wdt_driver = {
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "bcm47xx-wdt",
	},
	.probe		= bcm47xx_wdt_probe,
	.remove		= bcm47xx_wdt_remove,
};

module_platform_driver(bcm47xx_wdt_driver);

MODULE_AUTHOR("Aleksandar Radovanovic");
MODULE_AUTHOR("Hauke Mehrtens <hauke@hauke-m.de>");
MODULE_DESCRIPTION("Watchdog driver for Broadcom BCM47xx");
MODULE_LICENSE("GPL");
