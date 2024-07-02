// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Hardware monitoring driver for MP5920 and compatible chips.
 */

#include <linux/i2c.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include "pmbus.h"

static struct pmbus_driver_info mp5920_info = {
	.pages = 1,
	.format[PSC_VOLTAGE_IN] = direct,
	.format[PSC_VOLTAGE_OUT] = direct,
	.format[PSC_CURRENT_OUT] = direct,
	.format[PSC_POWER] = direct,
	.format[PSC_TEMPERATURE] = direct,
	.m[PSC_VOLTAGE_IN] = 2266,
	.b[PSC_VOLTAGE_IN] = 0,
	.R[PSC_VOLTAGE_IN] = -1,
	.m[PSC_VOLTAGE_OUT] = 2266,
	.b[PSC_VOLTAGE_OUT] = 0,
	.R[PSC_VOLTAGE_OUT] = -1,
	.m[PSC_CURRENT_OUT] = 546,
	.b[PSC_CURRENT_OUT] = 0,
	.R[PSC_CURRENT_OUT] = -2,
	.m[PSC_POWER] = 5840,
	.b[PSC_POWER] = 0,
	.R[PSC_POWER] = -3,
	.m[PSC_TEMPERATURE] = 1067,
	.b[PSC_TEMPERATURE] = 20500,
	.R[PSC_TEMPERATURE] = -2,
	.func[0] = PMBUS_HAVE_VIN  | PMBUS_HAVE_VOUT |
		PMBUS_HAVE_IOUT | PMBUS_HAVE_POUT |
		PMBUS_HAVE_TEMP,
};

static int mp5920_probe(struct i2c_client *client)
{
	struct device *dev =  &client->dev;
	int ret;
	u8 buf[I2C_SMBUS_BLOCK_MAX];

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_READ_WORD_DATA))
		return -ENODEV;

	ret = i2c_smbus_read_block_data(client, PMBUS_MFR_MODEL, buf);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to read PMBUS_MFR_MODEL\n");

	if (ret != 6 || strncmp(buf, "MP5920", 6)) {
		return dev_err_probe(dev, -ENODEV, "Model '%.*s' not supported\n",
				     min_t(int, ret, sizeof(buf)), buf);
	}

	return pmbus_do_probe(client, &mp5920_info);
}

static const struct of_device_id mp5920_of_match[] = {
	{ .compatible = "mps,mp5920" },
	{ }
};

MODULE_DEVICE_TABLE(of, mp5920_of_match);

static const struct i2c_device_id mp5920_id[] = {
	{ "mp5920" },
	{ }
};

MODULE_DEVICE_TABLE(i2c, mp5920_id);

static struct i2c_driver mp5920_driver = {
	.driver = {
		.name = "mp5920",
		.of_match_table = mp5920_of_match,
	},
	.probe = mp5920_probe,
	.id_table = mp5920_id,
};

module_i2c_driver(mp5920_driver);

MODULE_AUTHOR("Tony Ao <tony_ao@wiwynn.com>");
MODULE_AUTHOR("Alex Vdovydchenko <xzeol@yahoo.com>");
MODULE_DESCRIPTION("PMBus driver for MP5920 HSC");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(PMBUS);
