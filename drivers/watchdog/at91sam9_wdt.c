/*
 * Watchdog driver for Atmel AT91SAM9x processors.
 *
 * Copyright (C) 2008 Renaud CERRATO r.cerrato@til-technologies.fr
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

/*
 * The Watchdog Timer Mode Register can be only written to once. If the
 * timeout need to be set from Linux, be sure that the bootstrap or the
 * bootloader doesn't write to this register.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/errno.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/watchdog.h>
#include <linux/jiffies.h>
#include <linux/timer.h>
#include <linux/bitops.h>
#include <linux/uaccess.h>
#include <linux/of.h>

#include "at91sam9_wdt.h"

#define DRV_NAME "AT91SAM9 Watchdog"

#define wdt_read(field) \
	__raw_readl(at91wdt_private.base + field)
#define wdt_write(field, val) \
	__raw_writel((val), at91wdt_private.base + field)

/* AT91SAM9 watchdog runs a 12bit counter @ 256Hz,
 * use this to convert a watchdog
 * value from/to milliseconds.
 */
#define ms_to_ticks(t)	(((t << 8) / 1000) - 1)
#define ticks_to_ms(t)	(((t + 1) * 1000) >> 8)

/* Hardware timeout in seconds */
#define WDT_HW_TIMEOUT 2

/* Timer heartbeat (500ms) */
#define WDT_TIMEOUT	(HZ/2)

/* User land timeout */
#define WDT_HEARTBEAT 15
static int heartbeat;
module_param(heartbeat, int, 0);
MODULE_PARM_DESC(heartbeat, "Watchdog heartbeats in seconds. "
	"(default = " __MODULE_STRING(WDT_HEARTBEAT) ")");

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started "
	"(default=" __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

static struct watchdog_device at91_wdt_dev;
static void at91_ping(unsigned long data);

static struct {
	void __iomem *base;
	unsigned long next_heartbeat;	/* the next_heartbeat for the timer */
	struct timer_list timer;	/* The timer that pings the watchdog */
} at91wdt_private;

/* ......................................................................... */

/*
 * Reload the watchdog timer.  (ie, pat the watchdog)
 */
static inline void at91_wdt_reset(void)
{
	wdt_write(AT91_WDT_CR, AT91_WDT_KEY | AT91_WDT_WDRSTT);
}

/*
 * Timer tick
 */
static void at91_ping(unsigned long data)
{
	if (time_before(jiffies, at91wdt_private.next_heartbeat) ||
	    (!watchdog_active(&at91_wdt_dev))) {
		at91_wdt_reset();
		mod_timer(&at91wdt_private.timer, jiffies + WDT_TIMEOUT);
	} else
		pr_crit("I will reset your machine !\n");
}

static int at91_wdt_ping(struct watchdog_device *wdd)
{
	/* calculate when the next userspace timeout will be */
	at91wdt_private.next_heartbeat = jiffies + wdd->timeout * HZ;
	return 0;
}

static int at91_wdt_start(struct watchdog_device *wdd)
{
	/* calculate the next userspace timeout and modify the timer */
	at91_wdt_ping(wdd);
	mod_timer(&at91wdt_private.timer, jiffies + WDT_TIMEOUT);
	return 0;
}

static int at91_wdt_stop(struct watchdog_device *wdd)
{
	/* The watchdog timer hardware can not be stopped... */
	return 0;
}

static int at91_wdt_set_timeout(struct watchdog_device *wdd, unsigned int new_timeout)
{
	wdd->timeout = new_timeout;
	return 0;
}

/*
 * Set the watchdog time interval in 1/256Hz (write-once)
 * Counter is 12 bit.
 */
static int at91_wdt_settimeout(unsigned int timeout)
{
	unsigned int reg;
	unsigned int mr;

	/* Check if disabled */
	mr = wdt_read(AT91_WDT_MR);
	if (mr & AT91_WDT_WDDIS) {
		pr_err("sorry, watchdog is disabled\n");
		return -EIO;
	}

	/*
	 * All counting occurs at SLOW_CLOCK / 128 = 256 Hz
	 *
	 * Since WDV is a 12-bit counter, the maximum period is
	 * 4096 / 256 = 16 seconds.
	 */
	reg = AT91_WDT_WDRSTEN	/* causes watchdog reset */
		/* | AT91_WDT_WDRPROC	causes processor reset only */
		| AT91_WDT_WDDBGHLT	/* disabled in debug mode */
		| AT91_WDT_WDD		/* restart at any time */
		| (timeout & AT91_WDT_WDV);  /* timer value */
	wdt_write(AT91_WDT_MR, reg);

	return 0;
}

/* ......................................................................... */

static const struct watchdog_info at91_wdt_info = {
	.identity	= DRV_NAME,
	.options	= WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING |
						WDIOF_MAGICCLOSE,
};

static const struct watchdog_ops at91_wdt_ops = {
	.owner =	THIS_MODULE,
	.start =	at91_wdt_start,
	.stop =		at91_wdt_stop,
	.ping =		at91_wdt_ping,
	.set_timeout =	at91_wdt_set_timeout,
};

static struct watchdog_device at91_wdt_dev = {
	.info =		&at91_wdt_info,
	.ops =		&at91_wdt_ops,
	.timeout =	WDT_HEARTBEAT,
	.min_timeout =	1,
	.max_timeout =	0xFFFF,
};

static int __init at91wdt_probe(struct platform_device *pdev)
{
	struct resource	*r;
	int res;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r)
		return -ENODEV;
	at91wdt_private.base = ioremap(r->start, resource_size(r));
	if (!at91wdt_private.base) {
		dev_err(&pdev->dev, "failed to map registers, aborting.\n");
		return -ENOMEM;
	}

	at91_wdt_dev.parent = &pdev->dev;
	watchdog_init_timeout(&at91_wdt_dev, heartbeat, &pdev->dev);
	watchdog_set_nowayout(&at91_wdt_dev, nowayout);

	/* Set watchdog */
	res = at91_wdt_settimeout(ms_to_ticks(WDT_HW_TIMEOUT * 1000));
	if (res)
		return res;

	res = watchdog_register_device(&at91_wdt_dev);
	if (res)
		return res;

	at91wdt_private.next_heartbeat = jiffies + at91_wdt_dev.timeout * HZ;
	setup_timer(&at91wdt_private.timer, at91_ping, 0);
	mod_timer(&at91wdt_private.timer, jiffies + WDT_TIMEOUT);

	pr_info("enabled (heartbeat=%d sec, nowayout=%d)\n",
		at91_wdt_dev.timeout, nowayout);

	return 0;
}

static int __exit at91wdt_remove(struct platform_device *pdev)
{
	watchdog_unregister_device(&at91_wdt_dev);

	pr_warn("I quit now, hardware will probably reboot!\n");
	del_timer(&at91wdt_private.timer);

	return 0;
}

#if defined(CONFIG_OF)
static const struct of_device_id at91_wdt_dt_ids[] = {
	{ .compatible = "atmel,at91sam9260-wdt" },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, at91_wdt_dt_ids);
#endif

static struct platform_driver at91wdt_driver = {
	.remove		= __exit_p(at91wdt_remove),
	.driver		= {
		.name	= "at91_wdt",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(at91_wdt_dt_ids),
	},
};

module_platform_driver_probe(at91wdt_driver, at91wdt_probe);

MODULE_AUTHOR("Renaud CERRATO <r.cerrato@til-technologies.fr>");
MODULE_DESCRIPTION("Watchdog driver for Atmel AT91SAM9x processors");
MODULE_LICENSE("GPL");
