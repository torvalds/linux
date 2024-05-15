// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Hardware monitoring driver for Maxim MAX20751
 *
 * Copyright (c) 2015 Guenter Roeck
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include "pmbus.h"

static struct pmbus_driver_info max20751_info = {
	.pages = 1,
	.format[PSC_VOLTAGE_IN] = linear,
	.format[PSC_VOLTAGE_OUT] = vid,
	.vrm_version[0] = vr12,
	.format[PSC_TEMPERATURE] = linear,
	.format[PSC_CURRENT_OUT] = linear,
	.format[PSC_POWER] = linear,
	.func[0] = PMBUS_HAVE_VIN | PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT |
		PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT |
		PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP |
		PMBUS_HAVE_POUT,
};

static int max20751_probe(struct i2c_client *client)
{
	return pmbus_do_probe(client, &max20751_info);
}

static const struct i2c_device_id max20751_id[] = {
	{"max20751"},
	{}
};

MODULE_DEVICE_TABLE(i2c, max20751_id);

static struct i2c_driver max20751_driver = {
	.driver = {
		   .name = "max20751",
		   },
	.probe = max20751_probe,
	.id_table = max20751_id,
};

module_i2c_driver(max20751_driver);

MODULE_AUTHOR("Guenter Roeck <linux@roeck-us.net>");
MODULE_DESCRIPTION("PMBus driver for Maxim MAX20751");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(PMBUS);
