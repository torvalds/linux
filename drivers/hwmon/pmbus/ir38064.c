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
#include "pmbus.h"

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
};

static int ir38064_probe(struct i2c_client *client)
{
	return pmbus_do_probe(client, &ir38064_info);
}

static const struct i2c_device_id ir38064_id[] = {
	{"ir38064", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, ir38064_id);

/* This is the driver that will be inserted */
static struct i2c_driver ir38064_driver = {
	.driver = {
		   .name = "ir38064",
		   },
	.probe_new = ir38064_probe,
	.id_table = ir38064_id,
};

module_i2c_driver(ir38064_driver);

MODULE_AUTHOR("Maxim Sloyko <maxims@google.com>");
MODULE_DESCRIPTION("PMBus driver for Infineon IR38064");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(PMBUS);
