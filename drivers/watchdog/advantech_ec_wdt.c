// SPDX-License-Identifier: GPL-2.0-only
/*
 *	Advantech Embedded Controller Watchdog Driver
 *
 *	This driver supports Advantech products with ITE based Embedded Controller.
 *	It does not support Advantech products with other ECs or without EC.
 *
 *	Copyright (C) 2022 Advantech Europe B.V.
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/isa.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/watchdog.h>

#define DRIVER_NAME		"advantech_ec_wdt"

/* EC IO region */
#define EC_BASE_ADDR		0x299
#define EC_ADDR_EXTENT		2

/* EC minimum IO access delay in ms */
#define EC_MIN_DELAY		10

/* EC interface definitions */
#define EC_ADDR_CMD		(EC_BASE_ADDR + 1)
#define EC_ADDR_DATA		EC_BASE_ADDR
#define EC_CMD_EC_PROBE		0x30
#define EC_CMD_COMM		0x89
#define EC_CMD_WDT_START	0x28
#define EC_CMD_WDT_STOP		0x29
#define EC_CMD_WDT_RESET	0x2A
#define EC_DAT_EN_DLY_H		0x58
#define EC_DAT_EN_DLY_L		0x59
#define EC_DAT_RST_DLY_H	0x5E
#define EC_DAT_RST_DLY_L	0x5F
#define EC_MAGIC		0x95

/* module parameters */
#define MIN_TIME		1
#define MAX_TIME		6000 /* 100 minutes */
#define DEFAULT_TIME		60

static unsigned int timeout;
static ktime_t ec_timestamp;

module_param(timeout, uint, 0);
MODULE_PARM_DESC(timeout,
		 "Default Watchdog timer setting (" __MODULE_STRING(DEFAULT_TIME) "s). The range is from " __MODULE_STRING(MIN_TIME) " to " __MODULE_STRING(MAX_TIME) ".");

static void adv_ec_wdt_timing_gate(void)
{
	ktime_t time_cur, time_delta;

	/* ensure minimum delay between IO accesses*/
	time_cur = ktime_get();
	time_delta = ktime_to_ms(ktime_sub(time_cur, ec_timestamp));
	if (time_delta < EC_MIN_DELAY) {
		time_delta = EC_MIN_DELAY - time_delta;
		usleep_range(time_delta * 1000, (time_delta + 1) * 1000);
	}
	ec_timestamp = ktime_get();
}

static void adv_ec_wdt_outb(unsigned char value, unsigned short port)
{
	adv_ec_wdt_timing_gate();
	outb(value, port);
}

static unsigned char adv_ec_wdt_inb(unsigned short port)
{
	adv_ec_wdt_timing_gate();
	return inb(port);
}

static int adv_ec_wdt_ping(struct watchdog_device *wdd)
{
	adv_ec_wdt_outb(EC_CMD_WDT_RESET, EC_ADDR_CMD);
	return 0;
}

static int adv_ec_wdt_set_timeout(struct watchdog_device *wdd, unsigned int t)
{
	unsigned int val;

	/* scale time to EC 100 ms base */
	val = t * 10;

	/* reset enable delay, just in case it was set by BIOS etc. */
	adv_ec_wdt_outb(EC_CMD_COMM, EC_ADDR_CMD);
	adv_ec_wdt_outb(EC_DAT_EN_DLY_H, EC_ADDR_DATA);
	adv_ec_wdt_outb(0, EC_ADDR_DATA);

	adv_ec_wdt_outb(EC_CMD_COMM, EC_ADDR_CMD);
	adv_ec_wdt_outb(EC_DAT_EN_DLY_L, EC_ADDR_DATA);
	adv_ec_wdt_outb(0, EC_ADDR_DATA);

	/* set reset delay */
	adv_ec_wdt_outb(EC_CMD_COMM, EC_ADDR_CMD);
	adv_ec_wdt_outb(EC_DAT_RST_DLY_H, EC_ADDR_DATA);
	adv_ec_wdt_outb(val >> 8, EC_ADDR_DATA);

	adv_ec_wdt_outb(EC_CMD_COMM, EC_ADDR_CMD);
	adv_ec_wdt_outb(EC_DAT_RST_DLY_L, EC_ADDR_DATA);
	adv_ec_wdt_outb(val & 0xFF, EC_ADDR_DATA);

	wdd->timeout = t;
	return 0;
}

static int adv_ec_wdt_start(struct watchdog_device *wdd)
{
	adv_ec_wdt_set_timeout(wdd, wdd->timeout);
	adv_ec_wdt_outb(EC_CMD_WDT_START, EC_ADDR_CMD);

	return 0;
}

static int adv_ec_wdt_stop(struct watchdog_device *wdd)
{
	adv_ec_wdt_outb(EC_CMD_WDT_STOP, EC_ADDR_CMD);

	return 0;
}

static const struct watchdog_info adv_ec_wdt_info = {
	.identity =	DRIVER_NAME,
	.options =	WDIOF_SETTIMEOUT |
			WDIOF_MAGICCLOSE |
			WDIOF_KEEPALIVEPING,
};

static const struct watchdog_ops adv_ec_wdt_ops = {
	.owner =	THIS_MODULE,
	.start =	adv_ec_wdt_start,
	.stop =		adv_ec_wdt_stop,
	.ping =		adv_ec_wdt_ping,
	.set_timeout =	adv_ec_wdt_set_timeout,
};

static struct watchdog_device adv_ec_wdt_dev = {
	.info =		&adv_ec_wdt_info,
	.ops =		&adv_ec_wdt_ops,
	.min_timeout =	MIN_TIME,
	.max_timeout =	MAX_TIME,
	.timeout =	DEFAULT_TIME,
};

static int adv_ec_wdt_probe(struct device *dev, unsigned int id)
{
	if (!devm_request_region(dev, EC_BASE_ADDR, EC_ADDR_EXTENT, dev_name(dev))) {
		dev_err(dev, "Unable to lock port addresses (0x%X-0x%X)\n",
			EC_BASE_ADDR, EC_BASE_ADDR + EC_ADDR_EXTENT);
		return -EBUSY;
	}

	watchdog_init_timeout(&adv_ec_wdt_dev, timeout, dev);
	watchdog_stop_on_reboot(&adv_ec_wdt_dev);
	watchdog_stop_on_unregister(&adv_ec_wdt_dev);

	return devm_watchdog_register_device(dev, &adv_ec_wdt_dev);
}

static struct isa_driver adv_ec_wdt_driver = {
	.probe		= adv_ec_wdt_probe,
	.driver		= {
	.name		= DRIVER_NAME,
	},
};

static int __init adv_ec_wdt_init(void)
{
	unsigned int val;

	/* quick probe for EC */
	if (!request_region(EC_BASE_ADDR, EC_ADDR_EXTENT, DRIVER_NAME))
		return -EBUSY;

	adv_ec_wdt_outb(EC_CMD_EC_PROBE, EC_ADDR_CMD);
	val = adv_ec_wdt_inb(EC_ADDR_DATA);
	release_region(EC_BASE_ADDR, EC_ADDR_EXTENT);

	if (val != EC_MAGIC)
		return -ENODEV;

	return isa_register_driver(&adv_ec_wdt_driver, 1);
}

static void __exit adv_ec_wdt_exit(void)
{
	isa_unregister_driver(&adv_ec_wdt_driver);
}

module_init(adv_ec_wdt_init);
module_exit(adv_ec_wdt_exit);

MODULE_AUTHOR("Thomas Kastner <thomas.kastner@advantech.com>");
MODULE_DESCRIPTION("Advantech Embedded Controller Watchdog Device Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("20221019");
MODULE_ALIAS("isa:" DRIVER_NAME);
