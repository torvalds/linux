// SPDX-License-Identifier: GPL-2.0+
/*
 * Hardware monitoring driver for Infineon IR38064
 *
 * Copyright (c) 2017 Google Inc
 *
 * VOUT_MODE is not supported by the device. The driver fakes VOUT linear16
 * mode with exponent value -8 as direct mode with m=256/b=0/R=0.
 *
 * The device supports VOUT_PEAK, IOUT_PEAK, and TEMPERATURE_PEAK, however
 * this driver does not currently support them.
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/driver.h>
#include "pmbus.h"

#if IS_ENABLED(CONFIG_SENSORS_IR38064_REGULATOR)
static const struct regulator_desc ir38064_reg_desc[] = {
	PMBUS_REGULATOR_ONE("vout"),
};
#endif /* CONFIG_SENSORS_IR38064_REGULATOR */

static struct pmbus_driver_info ir38064_info = {
	.pages = 1,
	.format[PSC_VOLTAGE_IN] = linear,
	.format[PSC_VOLTAGE_OUT] = direct,
	.format[PSC_CURRENT_OUT] = linear,
	.format[PSC_POWER] = linear,
	.format[PSC_TEMPERATURE] = linear,
	.m[PSC_VOLTAGE_OUT] = 256,
	.b[PSC_VOLTAGE_OUT] = 0,
	.R[PSC_VOLTAGE_OUT] = 0,
	.func[0] = PMBUS_HAVE_VIN | PMBUS_HAVE_STATUS_INPUT
	    | PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP
	    | PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT
	    | PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT
	    | PMBUS_HAVE_POUT,
#if IS_ENABLED(CONFIG_SENSORS_IR38064_REGULATOR)
	.num_regulators = 1,
	.reg_desc = ir38064_reg_desc,
#endif
};

static int ir38064_probe(struct i2c_client *client)
{
	return pmbus_do_probe(client, &ir38064_info);
}

static const struct i2c_device_id ir38064_id[] = {
	{"ir38060", 0},
	{"ir38064", 0},
	{"ir38164", 0},
	{"ir38263", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, ir38064_id);

static const struct of_device_id __maybe_unused ir38064_of_match[] = {
	{ .compatible = "infineon,ir38060" },
	{ .compatible = "infineon,ir38064" },
	{ .compatible = "infineon,ir38164" },
	{ .compatible = "infineon,ir38263" },
	{}
};

MODULE_DEVICE_TABLE(of, ir38064_of_match);

/* This is the driver that will be inserted */
static struct i2c_driver ir38064_driver = {
	.driver = {
		   .name = "ir38064",
		   .of_match_table = of_match_ptr(ir38064_of_match),
		   },
	.probe = ir38064_probe,
	.id_table = ir38064_id,
};

module_i2c_driver(ir38064_driver);

MODULE_AUTHOR("Maxim Sloyko <maxims@google.com>");
MODULE_DESCRIPTION("PMBus driver for Infineon IR38064 and compatible chips");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(PMBUS);
