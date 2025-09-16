// SPDX-License-Identifier: GPL-2.0
/*
 * Hardware monitoring driver for Analog Devices LT3074
 *
 * Copyright (C) 2025 Analog Devices, Inc.
 */
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>

#include "pmbus.h"

#define LT3074_MFR_READ_VBIAS			0xc6
#define LT3074_MFR_BIAS_OV_WARN_LIMIT		0xc7
#define LT3074_MFR_BIAS_UV_WARN_LIMIT		0xc8
#define LT3074_MFR_SPECIAL_ID			0xe7

#define LT3074_SPECIAL_ID_VALUE			0x1c1d

static const struct regulator_desc __maybe_unused lt3074_reg_desc[] = {
	PMBUS_REGULATOR_ONE("regulator"),
};

static int lt3074_read_word_data(struct i2c_client *client, int page,
				 int phase, int reg)
{
	switch (reg) {
	case PMBUS_VIRT_READ_VMON:
		return pmbus_read_word_data(client, page, phase,
					   LT3074_MFR_READ_VBIAS);
	case PMBUS_VIRT_VMON_UV_WARN_LIMIT:
		return pmbus_read_word_data(client, page, phase,
					   LT3074_MFR_BIAS_UV_WARN_LIMIT);
	case PMBUS_VIRT_VMON_OV_WARN_LIMIT:
		return pmbus_read_word_data(client, page, phase,
					   LT3074_MFR_BIAS_OV_WARN_LIMIT);
	default:
		return -ENODATA;
	}
}

static int lt3074_write_word_data(struct i2c_client *client, int page,
				  int reg, u16 word)
{
	switch (reg) {
	case PMBUS_VIRT_VMON_UV_WARN_LIMIT:
		return pmbus_write_word_data(client, 0,
					    LT3074_MFR_BIAS_UV_WARN_LIMIT,
					    word);
	case PMBUS_VIRT_VMON_OV_WARN_LIMIT:
		return pmbus_write_word_data(client, 0,
					    LT3074_MFR_BIAS_OV_WARN_LIMIT,
					    word);
	default:
		return -ENODATA;
	}
}

static struct pmbus_driver_info lt3074_info = {
	.pages = 1,
	.format[PSC_VOLTAGE_IN] = linear,
	.format[PSC_VOLTAGE_OUT] = linear,
	.format[PSC_CURRENT_OUT] = linear,
	.format[PSC_TEMPERATURE] = linear,
	.func[0] = PMBUS_HAVE_VIN | PMBUS_HAVE_VOUT | PMBUS_HAVE_IOUT |
		   PMBUS_HAVE_TEMP | PMBUS_HAVE_VMON |
		   PMBUS_HAVE_STATUS_VOUT | PMBUS_HAVE_STATUS_IOUT |
		   PMBUS_HAVE_STATUS_INPUT | PMBUS_HAVE_STATUS_TEMP,
	.read_word_data = lt3074_read_word_data,
	.write_word_data = lt3074_write_word_data,
#if IS_ENABLED(CONFIG_SENSORS_LT3074_REGULATOR)
	.num_regulators = 1,
	.reg_desc = lt3074_reg_desc,
#endif
};

static int lt3074_probe(struct i2c_client *client)
{
	int ret;
	struct device *dev = &client->dev;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_READ_WORD_DATA))
		return -ENODEV;

	ret = i2c_smbus_read_word_data(client, LT3074_MFR_SPECIAL_ID);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to read ID\n");

	if (ret != LT3074_SPECIAL_ID_VALUE)
		return dev_err_probe(dev, -ENODEV, "ID mismatch\n");

	return pmbus_do_probe(client, &lt3074_info);
}

static const struct i2c_device_id lt3074_id[] = {
	{ "lt3074", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, lt3074_id);

static const struct of_device_id __maybe_unused lt3074_of_match[] = {
	{ .compatible = "adi,lt3074" },
	{}
};
MODULE_DEVICE_TABLE(of, lt3074_of_match);

static struct i2c_driver lt3074_driver = {
	.driver = {
		.name = "lt3074",
		.of_match_table = of_match_ptr(lt3074_of_match),
	},
	.probe = lt3074_probe,
	.id_table = lt3074_id,
};
module_i2c_driver(lt3074_driver);

MODULE_AUTHOR("Cedric Encarnacion <cedricjustine.encarnacion@analog.com>");
MODULE_DESCRIPTION("PMBus driver for Analog Devices LT3074");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("PMBUS");
