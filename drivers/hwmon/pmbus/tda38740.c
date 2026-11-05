// SPDX-License-Identifier: GPL-2.0+
/*
 * Hardware monitoring driver for Infineon TDA38725/TDA38740
 *
 * Copyright (c) 2023 9elements GmbH
 *
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include "pmbus.h"

static struct pmbus_driver_info tda38740_info = {
	.pages = 1,
	.format[PSC_VOLTAGE_IN] = linear,
	.format[PSC_VOLTAGE_OUT] = linear,
	.format[PSC_CURRENT_OUT] = linear,
	.format[PSC_CURRENT_IN] = linear,
	.format[PSC_POWER] = linear,
	.format[PSC_TEMPERATURE] = linear,
	.func[0] = PMBUS_HAVE_VIN | PMBUS_HAVE_STATUS_INPUT
	    | PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP
	    | PMBUS_HAVE_IIN
	    | PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT
	    | PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT
	    | PMBUS_HAVE_POUT | PMBUS_HAVE_PIN,
};

static int tda38740_probe(struct i2c_client *client)
{
	return pmbus_do_probe(client, &tda38740_info);
}

static const struct i2c_device_id tda38740_id[] = {
	{ .name = "tda38725"},
	{ .name = "tda38740"},
	{}
};
MODULE_DEVICE_TABLE(i2c, tda38740_id);

static const struct of_device_id tda38740_of_match[] = {
	{ .compatible = "infineon,tda38725"},
	{ .compatible = "infineon,tda38740"},
	{}
};
MODULE_DEVICE_TABLE(of, tda38740_of_match);

static struct i2c_driver tda38740_driver = {
	.driver = {
		.name = "tda38740",
		.of_match_table = tda38740_of_match,
	},
	.probe = tda38740_probe,
	.id_table = tda38740_id,
};

module_i2c_driver(tda38740_driver);

MODULE_DESCRIPTION("PMBus driver for Infineon TDA38725/TDA38740");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("PMBUS");
