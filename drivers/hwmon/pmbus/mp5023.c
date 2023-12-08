// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for MPS MP5023 Hot-Swap Controller
 */

#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include "pmbus.h"

static struct pmbus_driver_info mp5023_info = {
	.pages = 1,

	.format[PSC_VOLTAGE_IN] = direct,
	.format[PSC_VOLTAGE_OUT] = direct,
	.format[PSC_CURRENT_OUT] = direct,
	.format[PSC_POWER] = direct,
	.format[PSC_TEMPERATURE] = direct,

	.m[PSC_VOLTAGE_IN] = 32,
	.b[PSC_VOLTAGE_IN] = 0,
	.R[PSC_VOLTAGE_IN] = 0,
	.m[PSC_VOLTAGE_OUT] = 32,
	.b[PSC_VOLTAGE_OUT] = 0,
	.R[PSC_VOLTAGE_OUT] = 0,
	.m[PSC_CURRENT_OUT] = 16,
	.b[PSC_CURRENT_OUT] = 0,
	.R[PSC_CURRENT_OUT] = 0,
	.m[PSC_POWER] = 1,
	.b[PSC_POWER] = 0,
	.R[PSC_POWER] = 0,
	.m[PSC_TEMPERATURE] = 2,
	.b[PSC_TEMPERATURE] = 0,
	.R[PSC_TEMPERATURE] = 0,

	.func[0] =
		PMBUS_HAVE_VIN | PMBUS_HAVE_VOUT | PMBUS_HAVE_PIN |
		PMBUS_HAVE_TEMP | PMBUS_HAVE_IOUT |
		PMBUS_HAVE_STATUS_INPUT | PMBUS_HAVE_STATUS_TEMP,
};

static int mp5023_probe(struct i2c_client *client)
{
	return pmbus_do_probe(client, &mp5023_info);
}

static const struct of_device_id __maybe_unused mp5023_of_match[] = {
	{ .compatible = "mps,mp5023", },
	{}
};

MODULE_DEVICE_TABLE(of, mp5023_of_match);

static struct i2c_driver mp5023_driver = {
	.driver = {
		   .name = "mp5023",
		   .of_match_table = of_match_ptr(mp5023_of_match),
	},
	.probe_new = mp5023_probe,
};

module_i2c_driver(mp5023_driver);

MODULE_AUTHOR("Howard Chiu <howard.chiu@quantatw.com>");
MODULE_DESCRIPTION("PMBus driver for MPS MP5023 HSC");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(PMBUS);
