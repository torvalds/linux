/*
 * Watchdog driver for Broadcom BCM2835
 *
 * "bcm2708_wdog" driver written by Luke Diamand that was obtained from
 * branch "rpi-3.6.y" of git://github.com/raspberrypi/linux.git was used
 * as a hardware reference for the Broadcom BCM2835 watchdog timer.
 *
 * Copyright (C) 2013 Lubomir Rintel <lkundrak@v3.sk>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/watchdog.h>
#include <linux/platform_device.h>
#include <linux/of_address.h>

#define PM_RSTC				0x1c
#define PM_WDOG				0x24

#define PM_PASSWORD			0x5a000000

#define PM_WDOG_TIME_SET		0x000fffff
#define PM_RSTC_WRCFG_CLR		0xffffffcf
#define PM_RSTC_WRCFG_SET		0x00000030
#define PM_RSTC_WRCFG_FULL_RESET	0x00000020
#define PM_RSTC_RESET			0x00000102

#define SECS_TO_WDOG_TICKS(x) ((x) << 16)
#define WDOG_TICKS_TO_SECS(x) ((x) >> 16)

struct bcm2835_wdt {
	void __iomem		*base;
	spinlock_t		lock;
};

static unsigned int heartbeat;
static bool nowayout = WATCHDOG_NOWAYOUT;

static int bcm2835_wdt_start(struct watchdog_device *wdog)
{
	struct bcm2835_wdt *wdt = watchdog_get_drvdata(wdog);
	uint32_t cur;
	unsigned long flags;

	spin_lock_irqsave(&wdt->lock, flags);

	writel_relaxed(PM_PASSWORD | (SECS_TO_WDOG_TICKS(wdog->timeout) &
				PM_WDOG_TIME_SET), wdt->base + PM_WDOG);
	cur = readl_relaxed(wdt->base + PM_RSTC);
	writel_relaxed(PM_PASSWORD | (cur & PM_RSTC_WRCFG_CLR) |
		  PM_RSTC_WRCFG_FULL_RESET, wdt->base + PM_RSTC);

	spin_unlock_irqrestore(&wdt->lock, flags);

	return 0;
}

static int bcm2835_wdt_stop(struct watchdog_device *wdog)
{
	struct bcm2835_wdt *wdt = watchdog_get_drvdata(wdog);

	writel_relaxed(PM_PASSWORD | PM_RSTC_RESET, wdt->base + PM_RSTC);
	dev_info(wdog->dev, "Watchdog timer stopped");
	return 0;
}

static int bcm2835_wdt_set_timeout(struct watchdog_device *wdog, unsigned int t)
{
	wdog->timeout = t;
	return 0;
}

static unsigned int bcm2835_wdt_get_timeleft(struct watchdog_device *wdog)
{
	struct bcm2835_wdt *wdt = watchdog_get_drvdata(wdog);

	uint32_t ret = readl_relaxed(wdt->base + PM_WDOG);
	return WDOG_TICKS_TO_SECS(ret & PM_WDOG_TIME_SET);
}

static struct watchdog_ops bcm2835_wdt_ops = {
	.owner =	THIS_MODULE,
	.start =	bcm2835_wdt_start,
	.stop =		bcm2835_wdt_stop,
	.set_timeout =	bcm2835_wdt_set_timeout,
	.get_timeleft =	bcm2835_wdt_get_timeleft,
};

static struct watchdog_info bcm2835_wdt_info = {
	.options =	WDIOF_SETTIMEOUT | WDIOF_MAGICCLOSE |
			WDIOF_KEEPALIVEPING,
	.identity =	"Broadcom BCM2835 Watchdog timer",
};

static struct watchdog_device bcm2835_wdt_wdd = {
	.info =		&bcm2835_wdt_info,
	.ops =		&bcm2835_wdt_ops,
	.min_timeout =	1,
	.max_timeout =	WDOG_TICKS_TO_SECS(PM_WDOG_TIME_SET),
	.timeout =	WDOG_TICKS_TO_SECS(PM_WDOG_TIME_SET),
};

static int bcm2835_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct bcm2835_wdt *wdt;
	int err;

	wdt = devm_kzalloc(dev, sizeof(struct bcm2835_wdt), GFP_KERNEL);
	if (!wdt) {
		dev_err(dev, "Failed to allocate memory for watchdog device");
		return -ENOMEM;
	}
	platform_set_drvdata(pdev, wdt);

	spin_lock_init(&wdt->lock);

	wdt->base = of_iomap(np, 0);
	if (!wdt->base) {
		dev_err(dev, "Failed to remap watchdog regs");
		return -ENODEV;
	}

	watchdog_set_drvdata(&bcm2835_wdt_wdd, wdt);
	watchdog_init_timeout(&bcm2835_wdt_wdd, heartbeat, dev);
	watchdog_set_nowayout(&bcm2835_wdt_wdd, nowayout);
	err = watchdog_register_device(&bcm2835_wdt_wdd);
	if (err) {
		dev_err(dev, "Failed to register watchdog device");
		iounmap(wdt->base);
		return err;
	}

	dev_info(dev, "Broadcom BCM2835 watchdog timer");
	return 0;
}

static int bcm2835_wdt_remove(struct platform_device *pdev)
{
	struct bcm2835_wdt *wdt = platform_get_drvdata(pdev);

	watchdog_unregister_device(&bcm2835_wdt_wdd);
	iounmap(wdt->base);

	return 0;
}

static void bcm2835_wdt_shutdown(struct platform_device *pdev)
{
	bcm2835_wdt_stop(&bcm2835_wdt_wdd);
}

static const struct of_device_id bcm2835_wdt_of_match[] = {
	{ .compatible = "brcm,bcm2835-pm-wdt", },
	{},
};
MODULE_DEVICE_TABLE(of, bcm2835_wdt_of_match);

static struct platform_driver bcm2835_wdt_driver = {
	.probe		= bcm2835_wdt_probe,
	.remove		= bcm2835_wdt_remove,
	.shutdown	= bcm2835_wdt_shutdown,
	.driver = {
		.name =		"bcm2835-wdt",
		.owner =	THIS_MODULE,
		.of_match_table = bcm2835_wdt_of_match,
	},
};
module_platform_driver(bcm2835_wdt_driver);

module_param(heartbeat, uint, 0);
MODULE_PARM_DESC(heartbeat, "Initial watchdog heartbeat in seconds");

module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
				__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

MODULE_AUTHOR("Lubomir Rintel <lkundrak@v3.sk>");
MODULE_DESCRIPTION("Driver for Broadcom BCM2835 watchdog timer");
MODULE_LICENSE("GPL");
