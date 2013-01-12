/*
 *  Watchdog driver for Broadcom BCM47XX
 *
 *  Copyright (C) 2008 Aleksandar Radovanovic <biblbroks@sezampro.rs>
 *  Copyright (C) 2009 Matthieu CASTET <castet.matthieu@free.fr>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/reboot.h>
#include <linux/types.h>
#include <linux/watchdog.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/ssb/ssb_embedded.h>
#include <asm/mach-bcm47xx/bcm47xx.h>

#define DRV_NAME		"bcm47xx_wdt"

#define WDT_DEFAULT_TIME	30	/* seconds */
#define WDT_MAX_TIME		255	/* seconds */

static int wdt_time = WDT_DEFAULT_TIME;
static bool nowayout = WATCHDOG_NOWAYOUT;

module_param(wdt_time, int, 0);
MODULE_PARM_DESC(wdt_time, "Watchdog time in seconds. (default="
				__MODULE_STRING(WDT_DEFAULT_TIME) ")");

module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout,
		"Watchdog cannot be stopped once started (default="
				__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

static struct timer_list wdt_timer;
static atomic_t ticks;

static inline void bcm47xx_wdt_hw_start(void)
{
	/* this is 2,5s on 100Mhz clock  and 2s on 133 Mhz */
	switch (bcm47xx_bus_type) {
#ifdef CONFIG_BCM47XX_SSB
	case BCM47XX_BUS_TYPE_SSB:
		ssb_watchdog_timer_set(&bcm47xx_bus.ssb, 0xfffffff);
		break;
#endif
#ifdef CONFIG_BCM47XX_BCMA
	case BCM47XX_BUS_TYPE_BCMA:
		bcma_chipco_watchdog_timer_set(&bcm47xx_bus.bcma.bus.drv_cc,
					       0xfffffff);
		break;
#endif
	}
}

static inline int bcm47xx_wdt_hw_stop(void)
{
	switch (bcm47xx_bus_type) {
#ifdef CONFIG_BCM47XX_SSB
	case BCM47XX_BUS_TYPE_SSB:
		return ssb_watchdog_timer_set(&bcm47xx_bus.ssb, 0);
#endif
#ifdef CONFIG_BCM47XX_BCMA
	case BCM47XX_BUS_TYPE_BCMA:
		bcma_chipco_watchdog_timer_set(&bcm47xx_bus.bcma.bus.drv_cc, 0);
		return 0;
#endif
	}
	return -EINVAL;
}

static void bcm47xx_timer_tick(unsigned long unused)
{
	if (!atomic_dec_and_test(&ticks)) {
		bcm47xx_wdt_hw_start();
		mod_timer(&wdt_timer, jiffies + HZ);
	} else {
		pr_crit("Watchdog will fire soon!!!\n");
	}
}

static int bcm47xx_wdt_keepalive(struct watchdog_device *wdd)
{
	atomic_set(&ticks, wdt_time);

	return 0;
}

static int bcm47xx_wdt_start(struct watchdog_device *wdd)
{
	bcm47xx_wdt_pet();
	bcm47xx_timer_tick(0);

	return 0;
}

static int bcm47xx_wdt_stop(struct watchdog_device *wdd)
{
	del_timer_sync(&wdt_timer);
	bcm47xx_wdt_hw_stop();

	return 0;
}

static int bcm47xx_wdt_set_timeout(struct watchdog_device *wdd,
				   unsigned int new_time)
{
	if ((new_time <= 0) || (new_time > WDT_MAX_TIME))
		return -EINVAL;

	wdt_time = new_time;
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
	if (code == SYS_DOWN || code == SYS_HALT)
		bcm47xx_wdt_stop();
	return NOTIFY_DONE;
}

static struct watchdog_ops bcm47xx_wdt_ops = {
	.owner		= THIS_MODULE,
	.start		= bcm47xx_wdt_start,
	.stop		= bcm47xx_wdt_stop,
	.ping		= bcm47xx_wdt_keepalive,
	.set_timeout	= bcm47xx_wdt_set_timeout,
};

static struct watchdog_device bcm47xx_wdt_wdd = {
	.info		= &bcm47xx_wdt_info,
	.ops		= &bcm47xx_wdt_ops,
};

static struct notifier_block bcm47xx_wdt_notifier = {
	.notifier_call = bcm47xx_wdt_notify_sys,
};

static int __init bcm47xx_wdt_init(void)
{
	int ret;

	if (bcm47xx_wdt_hw_stop() < 0)
		return -ENODEV;

	setup_timer(&wdt_timer, bcm47xx_timer_tick, 0L);

	if (bcm47xx_wdt_settimeout(wdt_time)) {
		bcm47xx_wdt_settimeout(WDT_DEFAULT_TIME);
		pr_info("wdt_time value must be 0 < wdt_time < %d, using %d\n",
			(WDT_MAX_TIME + 1), wdt_time);
	}
	watchdog_set_nowayout(&bcm47xx_wdt_wdd, nowayout);

	ret = register_reboot_notifier(&bcm47xx_wdt_notifier);
	if (ret)
		return ret;

	ret = watchdog_register_device(&bcm47xx_wdt_wdd);
	if (ret) {
		unregister_reboot_notifier(&bcm47xx_wdt_notifier);
		return ret;
	}

	pr_info("BCM47xx Watchdog Timer enabled (%d seconds%s)\n",
		wdt_time, nowayout ? ", nowayout" : "");
	return 0;
}

static void __exit bcm47xx_wdt_exit(void)
{
	watchdog_unregister_device(&bcm47xx_wdt_wdd);

	unregister_reboot_notifier(&bcm47xx_wdt_notifier);
}

module_init(bcm47xx_wdt_init);
module_exit(bcm47xx_wdt_exit);

MODULE_AUTHOR("Aleksandar Radovanovic");
MODULE_DESCRIPTION("Watchdog driver for Broadcom BCM47xx");
MODULE_LICENSE("GPL");
