// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2016 National Instruments Corp.
 */

#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/watchdog.h>

#define NIWD_CONTROL	0x01
#define NIWD_COUNTER2	0x02
#define NIWD_COUNTER1	0x03
#define NIWD_COUNTER0	0x04
#define NIWD_SEED2	0x05
#define NIWD_SEED1	0x06
#define NIWD_SEED0	0x07

#define NIWD_IO_SIZE	0x08

#define NIWD_CONTROL_MODE		0x80
#define NIWD_CONTROL_PROC_RESET		0x20
#define NIWD_CONTROL_PET		0x10
#define NIWD_CONTROL_RUNNING		0x08
#define NIWD_CONTROL_CAPTURECOUNTER	0x04
#define NIWD_CONTROL_RESET		0x02
#define NIWD_CONTROL_ALARM		0x01

#define NIWD_PERIOD_NS		30720
#define NIWD_MIN_TIMEOUT	1
#define NIWD_MAX_TIMEOUT	515
#define NIWD_DEFAULT_TIMEOUT	60

#define NIWD_NAME		"ni903x_wdt"

struct ni903x_wdt {
	struct device *dev;
	u16 io_base;
	struct watchdog_device wdd;
};

static unsigned int timeout;
module_param(timeout, uint, 0);
MODULE_PARM_DESC(timeout,
		 "Watchdog timeout in seconds. (default="
		 __MODULE_STRING(NIWD_DEFAULT_TIMEOUT) ")");

static int nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, int, S_IRUGO);
MODULE_PARM_DESC(nowayout,
		 "Watchdog cannot be stopped once started (default="
		 __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

static void ni903x_start(struct ni903x_wdt *wdt)
{
	u8 control = inb(wdt->io_base + NIWD_CONTROL);

	outb(control | NIWD_CONTROL_RESET, wdt->io_base + NIWD_CONTROL);
	outb(control | NIWD_CONTROL_PET, wdt->io_base + NIWD_CONTROL);
}

static int ni903x_wdd_set_timeout(struct watchdog_device *wdd,
				  unsigned int timeout)
{
	struct ni903x_wdt *wdt = watchdog_get_drvdata(wdd);
	u32 counter = timeout * (1000000000 / NIWD_PERIOD_NS);

	outb(((0x00FF0000 & counter) >> 16), wdt->io_base + NIWD_SEED2);
	outb(((0x0000FF00 & counter) >> 8), wdt->io_base + NIWD_SEED1);
	outb((0x000000FF & counter), wdt->io_base + NIWD_SEED0);

	wdd->timeout = timeout;

	return 0;
}

static unsigned int ni903x_wdd_get_timeleft(struct watchdog_device *wdd)
{
	struct ni903x_wdt *wdt = watchdog_get_drvdata(wdd);
	u8 control, counter0, counter1, counter2;
	u32 counter;

	control = inb(wdt->io_base + NIWD_CONTROL);
	control |= NIWD_CONTROL_CAPTURECOUNTER;
	outb(control, wdt->io_base + NIWD_CONTROL);

	counter2 = inb(wdt->io_base + NIWD_COUNTER2);
	counter1 = inb(wdt->io_base + NIWD_COUNTER1);
	counter0 = inb(wdt->io_base + NIWD_COUNTER0);

	counter = (counter2 << 16) | (counter1 << 8) | counter0;

	return counter / (1000000000 / NIWD_PERIOD_NS);
}

static int ni903x_wdd_ping(struct watchdog_device *wdd)
{
	struct ni903x_wdt *wdt = watchdog_get_drvdata(wdd);
	u8 control;

	control = inb(wdt->io_base + NIWD_CONTROL);
	outb(control | NIWD_CONTROL_PET, wdt->io_base + NIWD_CONTROL);

	return 0;
}

static int ni903x_wdd_start(struct watchdog_device *wdd)
{
	struct ni903x_wdt *wdt = watchdog_get_drvdata(wdd);

	outb(NIWD_CONTROL_RESET | NIWD_CONTROL_PROC_RESET,
	     wdt->io_base + NIWD_CONTROL);

	ni903x_wdd_set_timeout(wdd, wdd->timeout);
	ni903x_start(wdt);

	return 0;
}

static int ni903x_wdd_stop(struct watchdog_device *wdd)
{
	struct ni903x_wdt *wdt = watchdog_get_drvdata(wdd);

	outb(NIWD_CONTROL_RESET, wdt->io_base + NIWD_CONTROL);

	return 0;
}

static acpi_status ni903x_resources(struct acpi_resource *res, void *data)
{
	struct ni903x_wdt *wdt = data;
	u16 io_size;

	switch (res->type) {
	case ACPI_RESOURCE_TYPE_IO:
		if (wdt->io_base != 0) {
			dev_err(wdt->dev, "too many IO resources\n");
			return AE_ERROR;
		}

		wdt->io_base = res->data.io.minimum;
		io_size = res->data.io.address_length;

		if (io_size < NIWD_IO_SIZE) {
			dev_err(wdt->dev, "memory region too small\n");
			return AE_ERROR;
		}

		if (!devm_request_region(wdt->dev, wdt->io_base, io_size,
					 NIWD_NAME)) {
			dev_err(wdt->dev, "failed to get memory region\n");
			return AE_ERROR;
		}

		return AE_OK;

	case ACPI_RESOURCE_TYPE_END_TAG:
	default:
		/* Ignore unsupported resources, e.g. IRQ */
		return AE_OK;
	}
}

static const struct watchdog_info ni903x_wdd_info = {
	.options = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE,
	.identity = "NI Watchdog",
};

static const struct watchdog_ops ni903x_wdd_ops = {
	.owner = THIS_MODULE,
	.start = ni903x_wdd_start,
	.stop = ni903x_wdd_stop,
	.ping = ni903x_wdd_ping,
	.set_timeout = ni903x_wdd_set_timeout,
	.get_timeleft = ni903x_wdd_get_timeleft,
};

static int ni903x_acpi_add(struct acpi_device *device)
{
	struct device *dev = &device->dev;
	struct watchdog_device *wdd;
	struct ni903x_wdt *wdt;
	acpi_status status;
	int ret;

	wdt = devm_kzalloc(dev, sizeof(*wdt), GFP_KERNEL);
	if (!wdt)
		return -ENOMEM;

	device->driver_data = wdt;
	wdt->dev = dev;

	status = acpi_walk_resources(device->handle, METHOD_NAME__CRS,
				     ni903x_resources, wdt);
	if (ACPI_FAILURE(status) || wdt->io_base == 0) {
		dev_err(dev, "failed to get resources\n");
		return -ENODEV;
	}

	wdd = &wdt->wdd;
	wdd->info = &ni903x_wdd_info;
	wdd->ops = &ni903x_wdd_ops;
	wdd->min_timeout = NIWD_MIN_TIMEOUT;
	wdd->max_timeout = NIWD_MAX_TIMEOUT;
	wdd->timeout = NIWD_DEFAULT_TIMEOUT;
	wdd->parent = dev;
	watchdog_set_drvdata(wdd, wdt);
	watchdog_set_nowayout(wdd, nowayout);
	watchdog_init_timeout(wdd, timeout, dev);

	ret = watchdog_register_device(wdd);
	if (ret) {
		dev_err(dev, "failed to register watchdog\n");
		return ret;
	}

	/* Switch from boot mode to user mode */
	outb(NIWD_CONTROL_RESET | NIWD_CONTROL_MODE,
	     wdt->io_base + NIWD_CONTROL);

	dev_dbg(dev, "io_base=0x%04X, timeout=%d, nowayout=%d\n",
		wdt->io_base, timeout, nowayout);

	return 0;
}

static int ni903x_acpi_remove(struct acpi_device *device)
{
	struct ni903x_wdt *wdt = acpi_driver_data(device);

	ni903x_wdd_stop(&wdt->wdd);
	watchdog_unregister_device(&wdt->wdd);

	return 0;
}

static const struct acpi_device_id ni903x_device_ids[] = {
	{"NIC775C", 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, ni903x_device_ids);

static struct acpi_driver ni903x_acpi_driver = {
	.name = NIWD_NAME,
	.ids = ni903x_device_ids,
	.ops = {
		.add = ni903x_acpi_add,
		.remove = ni903x_acpi_remove,
	},
};

module_acpi_driver(ni903x_acpi_driver);

MODULE_DESCRIPTION("NI 903x Watchdog");
MODULE_AUTHOR("Jeff Westfahl <jeff.westfahl@ni.com>");
MODULE_AUTHOR("Kyle Roeschley <kyle.roeschley@ni.com>");
MODULE_LICENSE("GPL");
