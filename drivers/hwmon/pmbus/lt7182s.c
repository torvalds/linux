// SPDX-License-Identifier: GPL-2.0+
/*
 * Hardware monitoring driver for Analog Devices LT7182S
 *
 * Copyright (c) 2022 Guenter Roeck
 *
 */

#include <linux/bits.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include "pmbus.h"

#define LT7182S_NUM_PAGES	2

#define MFR_READ_EXTVCC		0xcd
#define MFR_READ_ITH		0xce
#define MFR_CONFIG_ALL_LT7182S	0xd1
#define MFR_IOUT_PEAK		0xd7
#define MFR_ADC_CONTROL_LT7182S 0xd8

#define MFR_DEBUG_TELEMETRY	BIT(0)

#define MFR_VOUT_PEAK		0xdd
#define MFR_VIN_PEAK		0xde
#define MFR_TEMPERATURE_1_PEAK	0xdf
#define MFR_CLEAR_PEAKS		0xe3

#define MFR_CONFIG_IEEE		BIT(8)

static int lt7182s_read_word_data(struct i2c_client *client, int page, int phase, int reg)
{
	int ret;

	switch (reg) {
	case PMBUS_VIRT_READ_VMON:
		if (page == 0 || page == 1)
			ret = pmbus_read_word_data(client, page, phase, MFR_READ_ITH);
		else
			ret = pmbus_read_word_data(client, 0, phase, MFR_READ_EXTVCC);
		break;
	case PMBUS_VIRT_READ_IOUT_MAX:
		ret = pmbus_read_word_data(client, page, phase, MFR_IOUT_PEAK);
		break;
	case PMBUS_VIRT_READ_VOUT_MAX:
		ret = pmbus_read_word_data(client, page, phase, MFR_VOUT_PEAK);
		break;
	case PMBUS_VIRT_READ_VIN_MAX:
		ret = pmbus_read_word_data(client, page, phase, MFR_VIN_PEAK);
		break;
	case PMBUS_VIRT_READ_TEMP_MAX:
		ret = pmbus_read_word_data(client, page, phase, MFR_TEMPERATURE_1_PEAK);
		break;
	case PMBUS_VIRT_RESET_VIN_HISTORY:
		ret = (page == 0) ? 0 : -ENODATA;
		break;
	default:
		ret = -ENODATA;
		break;
	}
	return ret;
}

static int lt7182s_write_word_data(struct i2c_client *client, int page, int reg, u16 word)
{
	int ret;

	switch (reg) {
	case PMBUS_VIRT_RESET_VIN_HISTORY:
		ret = pmbus_write_byte(client, 0, MFR_CLEAR_PEAKS);
		break;
	default:
		ret = -ENODATA;
		break;
	}
	return ret;
}

static struct pmbus_driver_info lt7182s_info = {
	.pages = LT7182S_NUM_PAGES,
	.format[PSC_VOLTAGE_IN] = linear,
	.format[PSC_VOLTAGE_OUT] = linear,
	.format[PSC_CURRENT_IN] = linear,
	.format[PSC_CURRENT_OUT] = linear,
	.format[PSC_TEMPERATURE] = linear,
	.format[PSC_POWER] = linear,
	.func[0] = PMBUS_HAVE_VIN | PMBUS_HAVE_VOUT |
	  PMBUS_HAVE_IIN | PMBUS_HAVE_IOUT | PMBUS_HAVE_POUT |
	  PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_VOUT | PMBUS_HAVE_STATUS_IOUT |
	  PMBUS_HAVE_STATUS_INPUT | PMBUS_HAVE_STATUS_TEMP,
	.func[1] = PMBUS_HAVE_VIN | PMBUS_HAVE_VOUT |
	  PMBUS_HAVE_IIN | PMBUS_HAVE_IOUT | PMBUS_HAVE_POUT |
	  PMBUS_HAVE_STATUS_VOUT | PMBUS_HAVE_STATUS_IOUT |
	  PMBUS_HAVE_STATUS_INPUT,
	.read_word_data = lt7182s_read_word_data,
	.write_word_data = lt7182s_write_word_data,
};

static int lt7182s_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct pmbus_driver_info *info;
	u8 buf[I2C_SMBUS_BLOCK_MAX + 1];
	int ret;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_READ_BYTE_DATA |
				     I2C_FUNC_SMBUS_READ_WORD_DATA |
				     I2C_FUNC_SMBUS_READ_BLOCK_DATA))
		return -ENODEV;

	ret = i2c_smbus_read_block_data(client, PMBUS_MFR_ID, buf);
	if (ret < 0) {
		dev_err(dev, "Failed to read PMBUS_MFR_ID\n");
		return ret;
	}
	if (ret != 3 || strncmp(buf, "ADI", 3)) {
		buf[ret] = '\0';
		dev_err(dev, "Manufacturer '%s' not supported\n", buf);
		return -ENODEV;
	}

	ret = i2c_smbus_read_block_data(client, PMBUS_MFR_MODEL, buf);
	if (ret < 0) {
		dev_err(dev, "Failed to read PMBUS_MFR_MODEL\n");
		return ret;
	}
	if (ret != 7 || strncmp(buf, "LT7182S", 7)) {
		buf[ret] = '\0';
		dev_err(dev, "Model '%s' not supported\n", buf);
		return -ENODEV;
	}

	info = devm_kmemdup(dev, &lt7182s_info,
			    sizeof(struct pmbus_driver_info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	/* Set data format to IEEE754 if configured */
	ret = i2c_smbus_read_word_data(client, MFR_CONFIG_ALL_LT7182S);
	if (ret < 0)
		return ret;
	if (ret & MFR_CONFIG_IEEE) {
		info->format[PSC_VOLTAGE_IN] = ieee754;
		info->format[PSC_VOLTAGE_OUT] = ieee754;
		info->format[PSC_CURRENT_IN] = ieee754;
		info->format[PSC_CURRENT_OUT] = ieee754;
		info->format[PSC_TEMPERATURE] = ieee754;
		info->format[PSC_POWER] = ieee754;
	}

	/* Enable VMON output if configured */
	ret = i2c_smbus_read_byte_data(client, MFR_ADC_CONTROL_LT7182S);
	if (ret < 0)
		return ret;
	if (ret & MFR_DEBUG_TELEMETRY) {
		info->pages = 3;
		info->func[0] |= PMBUS_HAVE_VMON;
		info->func[1] |= PMBUS_HAVE_VMON;
		info->func[2] = PMBUS_HAVE_VMON;
	}

	return pmbus_do_probe(client, info);
}

static const struct i2c_device_id lt7182s_id[] = {
	{ "lt7182s", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, lt7182s_id);

static const struct of_device_id __maybe_unused lt7182s_of_match[] = {
	{ .compatible = "adi,lt7182s" },
	{}
};

static struct i2c_driver lt7182s_driver = {
	.driver = {
		.name = "lt7182s",
		.of_match_table = of_match_ptr(lt7182s_of_match),
	},
	.probe_new = lt7182s_probe,
	.id_table = lt7182s_id,
};

module_i2c_driver(lt7182s_driver);

MODULE_AUTHOR("Guenter Roeck <linux@roeck-us.net>");
MODULE_DESCRIPTION("PMBus driver for Analog Devices LT7182S");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(PMBUS);
