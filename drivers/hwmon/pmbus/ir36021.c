// SPDX-License-Identifier: GPL-2.0+
/*
 * Hardware monitoring driver for Infineon IR36021
 *
 * Copyright (c) 2021 Allied Telesis
 */
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include "pmbus.h"

static struct pmbus_driver_info ir36021_info = {
	.pages = 1,
	.format[PSC_VOLTAGE_IN] = linear,
	.format[PSC_VOLTAGE_OUT] = linear,
	.format[PSC_CURRENT_IN] = linear,
	.format[PSC_CURRENT_OUT] = linear,
	.format[PSC_POWER] = linear,
	.format[PSC_TEMPERATURE] = linear,
	.func[0] = PMBUS_HAVE_VIN | PMBUS_HAVE_VOUT
		| PMBUS_HAVE_IIN | PMBUS_HAVE_IOUT
		| PMBUS_HAVE_PIN | PMBUS_HAVE_POUT
		| PMBUS_HAVE_TEMP | PMBUS_HAVE_TEMP2
		| PMBUS_HAVE_STATUS_TEMP,
};

static int ir36021_probe(struct i2c_client *client)
{
	u8 buf[I2C_SMBUS_BLOCK_MAX];
	int ret;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_READ_BYTE_DATA
				     | I2C_FUNC_SMBUS_READ_WORD_DATA
				     | I2C_FUNC_SMBUS_READ_BLOCK_DATA))
		return -ENODEV;

	ret = i2c_smbus_read_i2c_block_data(client, PMBUS_MFR_MODEL, 2, buf);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to read PMBUS_MFR_MODEL\n");
		return ret;
	}
	if (ret != 2 || buf[0] != 0x01 || buf[1] != 0x2d) {
		dev_err(&client->dev, "MFR_MODEL unrecognised\n");
		return -ENODEV;
	}

	return pmbus_do_probe(client, &ir36021_info);
}

static const struct i2c_device_id ir36021_id[] = {
	{ "ir36021", 0 },
	{},
};
MODULE_DEVICE_TABLE(i2c, ir36021_id);

static const struct of_device_id __maybe_unused ir36021_of_id[] = {
	{ .compatible = "infineon,ir36021" },
	{},
};
MODULE_DEVICE_TABLE(of, ir36021_of_id);

static struct i2c_driver ir36021_driver = {
	.driver = {
		.name = "ir36021",
		.of_match_table = of_match_ptr(ir36021_of_id),
	},
	.probe = ir36021_probe,
	.id_table = ir36021_id,
};

module_i2c_driver(ir36021_driver);

MODULE_AUTHOR("Chris Packham <chris.packham@alliedtelesis.co.nz>");
MODULE_DESCRIPTION("PMBus driver for Infineon IR36021");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(PMBUS);
