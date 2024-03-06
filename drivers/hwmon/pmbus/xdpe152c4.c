// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Hardware monitoring driver for Infineon Multi-phase Digital VR Controllers
 *
 * Copyright (c) 2022 Infineon Technologies. All rights reserved.
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include "pmbus.h"

#define XDPE152_PAGE_NUM 2

static struct pmbus_driver_info xdpe152_info = {
	.pages = XDPE152_PAGE_NUM,
	.format[PSC_VOLTAGE_IN] = linear,
	.format[PSC_VOLTAGE_OUT] = linear,
	.format[PSC_TEMPERATURE] = linear,
	.format[PSC_CURRENT_IN] = linear,
	.format[PSC_CURRENT_OUT] = linear,
	.format[PSC_POWER] = linear,
	.func[0] = PMBUS_HAVE_VIN | PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT |
		PMBUS_HAVE_IIN | PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT |
		PMBUS_HAVE_TEMP | PMBUS_HAVE_TEMP2 | PMBUS_HAVE_STATUS_TEMP |
		PMBUS_HAVE_POUT | PMBUS_HAVE_PIN | PMBUS_HAVE_STATUS_INPUT,
	.func[1] = PMBUS_HAVE_VIN | PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT |
		PMBUS_HAVE_IIN | PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT |
		PMBUS_HAVE_POUT | PMBUS_HAVE_PIN | PMBUS_HAVE_STATUS_INPUT,
};

static int xdpe152_probe(struct i2c_client *client)
{
	struct pmbus_driver_info *info;

	info = devm_kmemdup(&client->dev, &xdpe152_info, sizeof(*info),
			    GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	return pmbus_do_probe(client, info);
}

static const struct i2c_device_id xdpe152_id[] = {
	{"xdpe152c4", 0},
	{"xdpe15284", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, xdpe152_id);

static const struct of_device_id __maybe_unused xdpe152_of_match[] = {
	{.compatible = "infineon,xdpe152c4"},
	{.compatible = "infineon,xdpe15284"},
	{}
};
MODULE_DEVICE_TABLE(of, xdpe152_of_match);

static struct i2c_driver xdpe152_driver = {
	.driver = {
		.name = "xdpe152c4",
		.of_match_table = of_match_ptr(xdpe152_of_match),
	},
	.probe = xdpe152_probe,
	.id_table = xdpe152_id,
};

module_i2c_driver(xdpe152_driver);

MODULE_AUTHOR("Greg Schwendimann <greg.schwendimann@infineon.com>");
MODULE_DESCRIPTION("PMBus driver for Infineon XDPE152 family");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(PMBUS);
