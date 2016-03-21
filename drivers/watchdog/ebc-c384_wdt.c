/*
 * Watchdog timer driver for the WinSystems EBC-C384
 * Copyright (C) 2016 William Breathitt Gray
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */
#include <linux/device.h>
#include <linux/dmi.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/watchdog.h>

#define MODULE_NAME		"ebc-c384_wdt"
#define WATCHDOG_TIMEOUT	60
/*
 * The timeout value in minutes must fit in a single byte when sent to the
 * watchdog timer; the maximum timeout possible is 15300 (255 * 60) seconds.
 */
#define WATCHDOG_MAX_TIMEOUT	15300
#define BASE_ADDR		0x564
#define ADDR_EXTENT		5
#define CFG_ADDR		(BASE_ADDR + 1)
#define PET_ADDR		(BASE_ADDR + 2)

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
	__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

static unsigned timeout;
module_param(timeout, uint, 0);
MODULE_PARM_DESC(timeout, "Watchdog timeout in seconds (default="
	__MODULE_STRING(WATCHDOG_TIMEOUT) ")");

static int ebc_c384_wdt_start(struct watchdog_device *wdev)
{
	unsigned t = wdev->timeout;

	/* resolution is in minutes for timeouts greater than 255 seconds */
	if (t > 255)
		t = DIV_ROUND_UP(t, 60);

	outb(t, PET_ADDR);

	return 0;
}

static int ebc_c384_wdt_stop(struct watchdog_device *wdev)
{
	outb(0x00, PET_ADDR);

	return 0;
}

static int ebc_c384_wdt_set_timeout(struct watchdog_device *wdev, unsigned t)
{
	/* resolution is in minutes for timeouts greater than 255 seconds */
	if (t > 255) {
		/* round second resolution up to minute granularity */
		wdev->timeout = roundup(t, 60);

		/* set watchdog timer for minutes */
		outb(0x00, CFG_ADDR);
	} else {
		wdev->timeout = t;

		/* set watchdog timer for seconds */
		outb(0x80, CFG_ADDR);
	}

	return 0;
}

static const struct watchdog_ops ebc_c384_wdt_ops = {
	.start = ebc_c384_wdt_start,
	.stop = ebc_c384_wdt_stop,
	.set_timeout = ebc_c384_wdt_set_timeout
};

static const struct watchdog_info ebc_c384_wdt_info = {
	.options = WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE | WDIOF_SETTIMEOUT,
	.identity = MODULE_NAME
};

static int __init ebc_c384_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct watchdog_device *wdd;

	if (!devm_request_region(dev, BASE_ADDR, ADDR_EXTENT, dev_name(dev))) {
		dev_err(dev, "Unable to lock port addresses (0x%X-0x%X)\n",
			BASE_ADDR, BASE_ADDR + ADDR_EXTENT);
		return -EBUSY;
	}

	wdd = devm_kzalloc(dev, sizeof(*wdd), GFP_KERNEL);
	if (!wdd)
		return -ENOMEM;

	wdd->info = &ebc_c384_wdt_info;
	wdd->ops = &ebc_c384_wdt_ops;
	wdd->timeout = WATCHDOG_TIMEOUT;
	wdd->min_timeout = 1;
	wdd->max_timeout = WATCHDOG_MAX_TIMEOUT;

	watchdog_set_nowayout(wdd, nowayout);

	if (watchdog_init_timeout(wdd, timeout, dev))
		dev_warn(dev, "Invalid timeout (%u seconds), using default (%u seconds)\n",
			timeout, WATCHDOG_TIMEOUT);

	platform_set_drvdata(pdev, wdd);

	return watchdog_register_device(wdd);
}

static int ebc_c384_wdt_remove(struct platform_device *pdev)
{
	struct watchdog_device *wdd = platform_get_drvdata(pdev);

	watchdog_unregister_device(wdd);

	return 0;
}

static struct platform_driver ebc_c384_wdt_driver = {
	.driver = {
		.name = MODULE_NAME
	},
	.remove = ebc_c384_wdt_remove
};

static struct platform_device *ebc_c384_wdt_device;

static int __init ebc_c384_wdt_init(void)
{
	int err;

	if (!dmi_match(DMI_BOARD_NAME, "EBC-C384 SBC"))
		return -ENODEV;

	ebc_c384_wdt_device = platform_device_alloc(MODULE_NAME, -1);
	if (!ebc_c384_wdt_device)
		return -ENOMEM;

	err = platform_device_add(ebc_c384_wdt_device);
	if (err)
		goto err_platform_device;

	err = platform_driver_probe(&ebc_c384_wdt_driver, ebc_c384_wdt_probe);
	if (err)
		goto err_platform_driver;

	return 0;

err_platform_driver:
	platform_device_del(ebc_c384_wdt_device);
err_platform_device:
	platform_device_put(ebc_c384_wdt_device);
	return err;
}

static void __exit ebc_c384_wdt_exit(void)
{
	platform_device_unregister(ebc_c384_wdt_device);
	platform_driver_unregister(&ebc_c384_wdt_driver);
}

module_init(ebc_c384_wdt_init);
module_exit(ebc_c384_wdt_exit);

MODULE_AUTHOR("William Breathitt Gray <vilhelm.gray@gmail.com>");
MODULE_DESCRIPTION("WinSystems EBC-C384 watchdog timer driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" MODULE_NAME);
