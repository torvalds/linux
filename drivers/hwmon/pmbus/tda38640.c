// SPDX-License-Identifier: GPL-2.0+
/*
 * Hardware monitoring driver for Infineon TDA38640
 *
 * Copyright (c) 2023 9elements GmbH
 *
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/regulator/driver.h>
#include "pmbus.h"

static const struct regulator_desc __maybe_unused tda38640_reg_desc[] = {
	PMBUS_REGULATOR("vout", 0),
};

static struct pmbus_driver_info tda38640_info = {
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
#if IS_ENABLED(CONFIG_SENSORS_TDA38640_REGULATOR)
	.num_regulators = 1,
	.reg_desc = tda38640_reg_desc,
#endif
};

static int tda38640_probe(struct i2c_client *client)
{
	return pmbus_do_probe(client, &tda38640_info);
}

static const struct i2c_device_id tda38640_id[] = {
	{"tda38640", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, tda38640_id);

static const struct of_device_id __maybe_unused tda38640_of_match[] = {
	{ .compatible = "infineon,tda38640"},
	{ },
};
MODULE_DEVICE_TABLE(of, tda38640_of_match);

/* This is the driver that will be inserted */
static struct i2c_driver tda38640_driver = {
	.driver = {
		.name = "tda38640",
		.of_match_table = of_match_ptr(tda38640_of_match),
	},
	.probe_new = tda38640_probe,
	.id_table = tda38640_id,
};

module_i2c_driver(tda38640_driver);

MODULE_AUTHOR("Patrick Rudolph <patrick.rudolph@9elements.com>");
MODULE_DESCRIPTION("PMBus driver for Infineon TDA38640");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(PMBUS);
