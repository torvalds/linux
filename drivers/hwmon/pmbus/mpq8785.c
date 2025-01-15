// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for MPS MPQ8785 Step-Down Converter
 */

#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include "pmbus.h"

static int mpq8785_identify(struct i2c_client *client,
			    struct pmbus_driver_info *info)
{
	int vout_mode;

	vout_mode = pmbus_read_byte_data(client, 0, PMBUS_VOUT_MODE);
	if (vout_mode < 0 || vout_mode == 0xff)
		return vout_mode < 0 ? vout_mode : -ENODEV;
	switch (vout_mode >> 5) {
	case 0:
		info->format[PSC_VOLTAGE_OUT] = linear;
		break;
	case 1:
	case 2:
		info->format[PSC_VOLTAGE_OUT] = direct;
		info->m[PSC_VOLTAGE_OUT] = 64;
		info->b[PSC_VOLTAGE_OUT] = 0;
		info->R[PSC_VOLTAGE_OUT] = 1;
		break;
	default:
		return -ENODEV;
	}

	return 0;
};

static struct pmbus_driver_info mpq8785_info = {
	.pages = 1,
	.format[PSC_VOLTAGE_IN] = direct,
	.format[PSC_CURRENT_OUT] = direct,
	.format[PSC_TEMPERATURE] = direct,
	.m[PSC_VOLTAGE_IN] = 4,
	.b[PSC_VOLTAGE_IN] = 0,
	.R[PSC_VOLTAGE_IN] = 1,
	.m[PSC_CURRENT_OUT] = 16,
	.b[PSC_CURRENT_OUT] = 0,
	.R[PSC_CURRENT_OUT] = 0,
	.m[PSC_TEMPERATURE] = 1,
	.b[PSC_TEMPERATURE] = 0,
	.R[PSC_TEMPERATURE] = 0,
	.func[0] =
		PMBUS_HAVE_VIN | PMBUS_HAVE_STATUS_INPUT |
		PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT |
		PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT |
		PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP,
	.identify = mpq8785_identify,
};

static int mpq8785_probe(struct i2c_client *client)
{
	return pmbus_do_probe(client, &mpq8785_info);
};

static const struct i2c_device_id mpq8785_id[] = {
	{ "mpq8785" },
	{ },
};
MODULE_DEVICE_TABLE(i2c, mpq8785_id);

static const struct of_device_id __maybe_unused mpq8785_of_match[] = {
	{ .compatible = "mps,mpq8785" },
	{}
};
MODULE_DEVICE_TABLE(of, mpq8785_of_match);

static struct i2c_driver mpq8785_driver = {
	.driver = {
		   .name = "mpq8785",
		   .of_match_table = of_match_ptr(mpq8785_of_match),
	},
	.probe = mpq8785_probe,
	.id_table = mpq8785_id,
};

module_i2c_driver(mpq8785_driver);

MODULE_AUTHOR("Charles Hsu <ythsu0511@gmail.com>");
MODULE_DESCRIPTION("PMBus driver for MPS MPQ8785");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("PMBUS");
