/*
 * Copyright (C) 2016 National Instruments Corp.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/acpi.h>
#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/watchdog.h>

#define LOCK			0xA5
#define UNLOCK			0x5A

#define WDT_CTRL_RESET_EN	BIT(7)
#define WDT_RELOAD_PORT_EN	BIT(7)

#define WDT_CTRL		1
#define WDT_RELOAD_CTRL		2
#define WDT_PRESET_PRESCALE	4
#define WDT_REG_LOCK		5
#define WDT_COUNT		6
#define WDT_RELOAD_PORT		7

#define WDT_MIN_TIMEOUT		1
#define WDT_MAX_TIMEOUT		464
#define WDT_DEFAULT_TIMEOUT	80

#define WDT_MAX_COUNTER		15

static unsigned int timeout;
module_param(timeout, uint, 0);
MODULE_PARM_DESC(timeout,
		 "Watchdog timeout in seconds. (default="
		 __MODULE_STRING(WDT_DEFAULT_TIMEOUT) ")");

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout,
		 "Watchdog cannot be stopped once started. (default="
		 __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

struct nic7018_wdt {
	u16 io_base;
	u32 period;
	struct watchdog_device wdd;
};

struct nic7018_config {
	u32 period;
	u8 divider;
};

static const struct nic7018_config nic7018_configs[] = {
	{  2, 4 },
	{ 32, 5 },
};

static inline u32 nic7018_timeout(u32 period, u8 counter)
{
	return period * counter - period / 2;
}

static const struct nic7018_config *nic7018_get_config(u32 timeout,
						       u8 *counter)
{
	const struct nic7018_config *config;
	u8 count;

	if (timeout < 30 && timeout != 16) {
		config = &nic7018_configs[0];
		count = timeout / 2 + 1;
	} else {
		config = &nic7018_configs[1];
		count = DIV_ROUND_UP(timeout + 16, 32);

		if (count > WDT_MAX_COUNTER)
			count = WDT_MAX_COUNTER;
	}
	*counter = count;
	return config;
}

static int nic7018_set_timeout(struct watchdog_device *wdd,
			       unsigned int timeout)
{
	struct nic7018_wdt *wdt = watchdog_get_drvdata(wdd);
	const struct nic7018_config *config;
	u8 counter;

	config = nic7018_get_config(timeout, &counter);

	outb(counter << 4 | config->divider,
	     wdt->io_base + WDT_PRESET_PRESCALE);

	wdd->timeout = nic7018_timeout(config->period, counter);
	wdt->period = config->period;

	return 0;
}

static int nic7018_start(struct watchdog_device *wdd)
{
	struct nic7018_wdt *wdt = watchdog_get_drvdata(wdd);
	u8 control;

	nic7018_set_timeout(wdd, wdd->timeout);

	control = inb(wdt->io_base + WDT_RELOAD_CTRL);
	outb(control | WDT_RELOAD_PORT_EN, wdt->io_base + WDT_RELOAD_CTRL);

	outb(1, wdt->io_base + WDT_RELOAD_PORT);

	control = inb(wdt->io_base + WDT_CTRL);
	outb(control | WDT_CTRL_RESET_EN, wdt->io_base + WDT_CTRL);

	return 0;
}

static int nic7018_stop(struct watchdog_device *wdd)
{
	struct nic7018_wdt *wdt = watchdog_get_drvdata(wdd);

	outb(0, wdt->io_base + WDT_CTRL);
	outb(0, wdt->io_base + WDT_RELOAD_CTRL);
	outb(0xF0, wdt->io_base + WDT_PRESET_PRESCALE);

	return 0;
}

static int nic7018_ping(struct watchdog_device *wdd)
{
	struct nic7018_wdt *wdt = watchdog_get_drvdata(wdd);

	outb(1, wdt->io_base + WDT_RELOAD_PORT);

	return 0;
}

static unsigned int nic7018_get_timeleft(struct watchdog_device *wdd)
{
	struct nic7018_wdt *wdt = watchdog_get_drvdata(wdd);
	u8 count;

	count = inb(wdt->io_base + WDT_COUNT) & 0xF;
	if (!count)
		return 0;

	return nic7018_timeout(wdt->period, count);
}

static const struct watchdog_info nic7018_wdd_info = {
	.options = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE,
	.identity = "NIC7018 Watchdog",
};

static const struct watchdog_ops nic7018_wdd_ops = {
	.owner = THIS_MODULE,
	.start = nic7018_start,
	.stop = nic7018_stop,
	.ping = nic7018_ping,
	.set_timeout = nic7018_set_timeout,
	.get_timeleft = nic7018_get_timeleft,
};

static int nic7018_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct watchdog_device *wdd;
	struct nic7018_wdt *wdt;
	struct resource *io_rc;
	int ret;

	wdt = devm_kzalloc(dev, sizeof(*wdt), GFP_KERNEL);
	if (!wdt)
		return -ENOMEM;

	platform_set_drvdata(pdev, wdt);

	io_rc = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (!io_rc) {
		dev_err(dev, "missing IO resources\n");
		return -EINVAL;
	}

	if (!devm_request_region(dev, io_rc->start, resource_size(io_rc),
				 KBUILD_MODNAME)) {
		dev_err(dev, "failed to get IO region\n");
		return -EBUSY;
	}

	wdt->io_base = io_rc->start;
	wdd = &wdt->wdd;
	wdd->info = &nic7018_wdd_info;
	wdd->ops = &nic7018_wdd_ops;
	wdd->min_timeout = WDT_MIN_TIMEOUT;
	wdd->max_timeout = WDT_MAX_TIMEOUT;
	wdd->timeout = WDT_DEFAULT_TIMEOUT;
	wdd->parent = dev;

	watchdog_set_drvdata(wdd, wdt);
	watchdog_set_nowayout(wdd, nowayout);

	ret = watchdog_init_timeout(wdd, timeout, dev);
	if (ret)
		dev_warn(dev, "unable to set timeout value, using default\n");

	/* Unlock WDT register */
	outb(UNLOCK, wdt->io_base + WDT_REG_LOCK);

	ret = watchdog_register_device(wdd);
	if (ret) {
		outb(LOCK, wdt->io_base + WDT_REG_LOCK);
		dev_err(dev, "failed to register watchdog\n");
		return ret;
	}

	dev_dbg(dev, "io_base=0x%04X, timeout=%d, nowayout=%d\n",
		wdt->io_base, timeout, nowayout);
	return 0;
}

static int nic7018_remove(struct platform_device *pdev)
{
	struct nic7018_wdt *wdt = platform_get_drvdata(pdev);

	watchdog_unregister_device(&wdt->wdd);

	/* Lock WDT register */
	outb(LOCK, wdt->io_base + WDT_REG_LOCK);

	return 0;
}

static const struct acpi_device_id nic7018_device_ids[] = {
	{"NIC7018", 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, nic7018_device_ids);

static struct platform_driver watchdog_driver = {
	.probe = nic7018_probe,
	.remove = nic7018_remove,
	.driver = {
		.name = KBUILD_MODNAME,
		.acpi_match_table = ACPI_PTR(nic7018_device_ids),
	},
};

module_platform_driver(watchdog_driver);

MODULE_DESCRIPTION("National Instruments NIC7018 Watchdog driver");
MODULE_AUTHOR("Hui Chun Ong <hui.chun.ong@ni.com>");
MODULE_LICENSE("GPL");
