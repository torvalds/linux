// SPDX-License-Identifier: GPL-2.0
/*
 * Hardware monitoring driver for Analog Devices MAX17616/MAX17616A
 *
 * Copyright (C) 2025 Analog Devices, Inc.
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>

#include "pmbus.h"

static struct pmbus_driver_info max17616_info = {
	.pages = 1,
	.format[PSC_VOLTAGE_IN] = direct,
	.m[PSC_VOLTAGE_IN] = 512,
	.b[PSC_VOLTAGE_IN] = -18,
	.R[PSC_VOLTAGE_IN] = -1,

	.format[PSC_VOLTAGE_OUT] = direct,
	.m[PSC_VOLTAGE_OUT] = 512,
	.b[PSC_VOLTAGE_OUT] = -18,
	.R[PSC_VOLTAGE_OUT] = -1,

	.format[PSC_CURRENT_OUT] = direct,
	.m[PSC_CURRENT_OUT] = 5845,
	.b[PSC_CURRENT_OUT] = 80,
	.R[PSC_CURRENT_OUT] = -1,

	.format[PSC_TEMPERATURE] = direct,
	.m[PSC_TEMPERATURE] = 71,
	.b[PSC_TEMPERATURE] = 19653,
	.R[PSC_TEMPERATURE] = -1,

	.func[0] =  PMBUS_HAVE_VIN | PMBUS_HAVE_VOUT | PMBUS_HAVE_IOUT |
		    PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_VOUT |
		    PMBUS_HAVE_STATUS_IOUT | PMBUS_HAVE_STATUS_INPUT |
		    PMBUS_HAVE_STATUS_TEMP,
};

static int max17616_probe(struct i2c_client *client)
{
	return pmbus_do_probe(client, &max17616_info);
}

static const struct i2c_device_id max17616_id[] = {
	{ "max17616" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max17616_id);

static const struct of_device_id max17616_of_match[] = {
	{ .compatible = "adi,max17616" },
	{ }
};
MODULE_DEVICE_TABLE(of, max17616_of_match);

static struct i2c_driver max17616_driver = {
	.driver = {
		.name = "max17616",
		.of_match_table = max17616_of_match,
	},
	.probe = max17616_probe,
	.id_table = max17616_id,
};
module_i2c_driver(max17616_driver);

MODULE_AUTHOR("Kim Seer Paller <kimseer.paller@analog.com>");
MODULE_DESCRIPTION("PMBus driver for Analog Devices MAX17616/MAX17616A");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("PMBUS");
