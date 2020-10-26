// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Hardware monitoring driver for TI TPS40422
 *
 * Copyright (c) 2014 Nokia Solutions and Networks.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include "pmbus.h"

static struct pmbus_driver_info tps40422_info = {
	.pages = 2,
	.format[PSC_VOLTAGE_IN] = linear,
	.format[PSC_VOLTAGE_OUT] = linear,
	.format[PSC_TEMPERATURE] = linear,
	.func[0] = PMBUS_HAVE_VOUT | PMBUS_HAVE_TEMP2
		| PMBUS_HAVE_STATUS_VOUT | PMBUS_HAVE_STATUS_TEMP
		| PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT,
	.func[1] = PMBUS_HAVE_VOUT | PMBUS_HAVE_TEMP2
		| PMBUS_HAVE_STATUS_VOUT | PMBUS_HAVE_STATUS_TEMP
		| PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT,
};

static int tps40422_probe(struct i2c_client *client)
{
	return pmbus_do_probe(client, &tps40422_info);
}

static const struct i2c_device_id tps40422_id[] = {
	{"tps40422", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, tps40422_id);

/* This is the driver that will be inserted */
static struct i2c_driver tps40422_driver = {
	.driver = {
		   .name = "tps40422",
		   },
	.probe_new = tps40422_probe,
	.remove = pmbus_do_remove,
	.id_table = tps40422_id,
};

module_i2c_driver(tps40422_driver);

MODULE_AUTHOR("Zhu Laiwen <richard.zhu@nsn.com>");
MODULE_DESCRIPTION("PMBus driver for TI TPS40422");
MODULE_LICENSE("GPL");
