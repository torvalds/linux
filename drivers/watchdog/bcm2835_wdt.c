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

#include <linux/delay.h>
#include <linux/reboot.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/watchdog.h>
#include <linux/platform_device.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>

#define PM_RSTC				0x1c
#define PM_RSTS				0x20
#define PM_WDOG				0x24

#define PM_PASSWORD			0x5a000000

#define PM_WDOG_TIME_SET		0x000fffff
#define PM_RSTC_WRCFG_CLR		0xffffffcf
#define PM_RSTS_HADWRH_SET		0x00000040
#define PM_RSTC_WRCFG_SET		0x00000030
#define PM_RSTC_WRCFG_FULL_RESET	0x00000020
#define PM_RSTC_RESET			0x00000102

/*
 * The Raspberry Pi firmware uses the RSTS register to know which partiton
 * to boot from. The partiton value is spread into bits 0, 2, 4, 6, 8, 10.
 * Partiton 63 is a special partition used by the firmware to indicate halt.
 */
#define PM_RSTS_RASPBERRYPI_HALT	0x555

#define SECS_TO_WDOG_TICKS(x) ((x) << 16)
#define WDOG_TICKS_TO_SECS(x) ((x) >> 16)

struct bcm2835_wdt {
	void __iomem		*base;
	spinlock_t		lock;
	struct notifier_block	restart_handler;
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
	return 0;
}

static unsigned int bcm2835_wdt_get_timeleft(struct watchdog_device *wdog)
{
	struct bcm2835_wdt *wdt = watchdog_get_drvdata(wdog);

	uint32_t ret = readl_relaxed(wdt->base + PM_WDOG);
	return WDOG_TICKS_TO_SECS(ret & PM_WDOG_TIME_SET);
}

static const struct watchdog_ops bcm2835_wdt_ops = {
	.owner =	THIS_MODULE,
	.start =	bcm2835_wdt_start,
	.stop =		bcm2835_wdt_stop,
	.get_timeleft =	bcm2835_wdt_get_timeleft,
};

static const struct watchdog_info bcm2835_wdt_info = {
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

static int
bcm2835_restart(struct notifier_block *this, unsigned long mode, void *cmd)
{
	struct bcm2835_wdt *wdt = container_of(this, struct bcm2835_wdt,
					       restart_handler);
	u32 val;

	/* use a timeout of 10 ticks (~150us) */
	writel_relaxed(10 | PM_PASSWORD, wdt->base + PM_WDOG);
	val = readl_relaxed(wdt->base + PM_RSTC);
	val &= PM_RSTC_WRCFG_CLR;
	val |= PM_PASSWORD | PM_RSTC_WRCFG_FULL_RESET;
	writel_relaxed(val, wdt->base + PM_RSTC);

	/* No sleeping, possibly atomic. */
	mdelay(1);

	return 0;
}

/*
 * We can't really power off, but if we do the normal reset scheme, and
 * indicate to bootcode.bin not to reboot, then most of the chip will be
 * powered off.
 */
static void bcm2835_power_off(void)
{
	struct device_node *np =
		of_find_compatible_node(NULL, NULL, "brcm,bcm2835-pm-wdt");
	struct platform_device *pdev = of_find_device_by_node(np);
	struct bcm2835_wdt *wdt = platform_get_drvdata(pdev);
	u32 val;

	/*
	 * We set the watchdog hard reset bit here to distinguish this reset
	 * from the normal (full) reset. bootcode.bin will not reboot after a
	 * hard reset.
	 */
	val = readl_relaxed(wdt->base + PM_RSTS);
	val |= PM_PASSWORD | PM_RSTS_RASPBERRYPI_HALT;
	writel_relaxed(val, wdt->base + PM_RSTS);

	/* Continue with normal reset mechanism */
	bcm2835_restart(&wdt->restart_handler, REBOOT_HARD, NULL);
}

static int bcm2835_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct bcm2835_wdt *wdt;
	int err;

	wdt = devm_kzalloc(dev, sizeof(struct bcm2835_wdt), GFP_KERNEL);
	if (!wdt)
		return -ENOMEM;
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
	bcm2835_wdt_wdd.parent = &pdev->dev;
	err = watchdog_register_device(&bcm2835_wdt_wdd);
	if (err) {
		dev_err(dev, "Failed to register watchdog device");
		iounmap(wdt->base);
		return err;
	}

	wdt->restart_handler.notifier_call = bcm2835_restart;
	wdt->restart_handler.priority = 128;
	register_restart_handler(&wdt->restart_handler);
	if (pm_power_off == NULL)
		pm_power_off = bcm2835_power_off;

	dev_info(dev, "Broadcom BCM2835 watchdog timer");
	return 0;
}

static int bcm2835_wdt_remove(struct platform_device *pdev)
{
	struct bcm2835_wdt *wdt = platform_get_drvdata(pdev);

	unregister_restart_handler(&wdt->restart_handler);
	if (pm_power_off == bcm2835_power_off)
		pm_power_off = NULL;
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
