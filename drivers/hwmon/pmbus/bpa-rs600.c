// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Hardware monitoring driver for BluTek BPA-RS600 Power Supplies
 *
 * Copyright 2021 Allied Telesis Labs
 */

#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pmbus.h>
#include "pmbus.h"

#define BPARS600_MFR_VIN_MIN	0xa0
#define BPARS600_MFR_VIN_MAX	0xa1
#define BPARS600_MFR_IIN_MAX	0xa2
#define BPARS600_MFR_PIN_MAX	0xa3
#define BPARS600_MFR_VOUT_MIN	0xa4
#define BPARS600_MFR_VOUT_MAX	0xa5
#define BPARS600_MFR_IOUT_MAX	0xa6
#define BPARS600_MFR_POUT_MAX	0xa7

static int bpa_rs600_read_byte_data(struct i2c_client *client, int page, int reg)
{
	int ret;

	if (page > 0)
		return -ENXIO;

	switch (reg) {
	case PMBUS_FAN_CONFIG_12:
		/*
		 * Two fans are reported in PMBUS_FAN_CONFIG_12 but there is
		 * only one fan in the module. Mask out the FAN2 bits.
		 */
		ret = pmbus_read_byte_data(client, 0, PMBUS_FAN_CONFIG_12);
		if (ret >= 0)
			ret &= ~(PB_FAN_2_INSTALLED | PB_FAN_2_PULSE_MASK);
		break;
	default:
		ret = -ENODATA;
		break;
	}

	return ret;
}

static int bpa_rs600_read_word_data(struct i2c_client *client, int page, int phase, int reg)
{
	int ret;

	if (page > 0)
		return -ENXIO;

	switch (reg) {
	case PMBUS_VIN_UV_WARN_LIMIT:
		ret = pmbus_read_word_data(client, 0, 0xff, BPARS600_MFR_VIN_MIN);
		break;
	case PMBUS_VIN_OV_WARN_LIMIT:
		ret = pmbus_read_word_data(client, 0, 0xff, BPARS600_MFR_VIN_MAX);
		break;
	case PMBUS_VOUT_UV_WARN_LIMIT:
		ret = pmbus_read_word_data(client, 0, 0xff, BPARS600_MFR_VOUT_MIN);
		break;
	case PMBUS_VOUT_OV_WARN_LIMIT:
		ret = pmbus_read_word_data(client, 0, 0xff, BPARS600_MFR_VOUT_MAX);
		break;
	case PMBUS_IIN_OC_WARN_LIMIT:
		ret = pmbus_read_word_data(client, 0, 0xff, BPARS600_MFR_IIN_MAX);
		break;
	case PMBUS_IOUT_OC_WARN_LIMIT:
		ret = pmbus_read_word_data(client, 0, 0xff, BPARS600_MFR_IOUT_MAX);
		break;
	case PMBUS_PIN_OP_WARN_LIMIT:
		ret = pmbus_read_word_data(client, 0, 0xff, BPARS600_MFR_PIN_MAX);
		break;
	case PMBUS_POUT_OP_WARN_LIMIT:
		ret = pmbus_read_word_data(client, 0, 0xff, BPARS600_MFR_POUT_MAX);
		break;
	case PMBUS_VIN_UV_FAULT_LIMIT:
	case PMBUS_VIN_OV_FAULT_LIMIT:
	case PMBUS_VOUT_UV_FAULT_LIMIT:
	case PMBUS_VOUT_OV_FAULT_LIMIT:
		/* These commands return data but it is invalid/un-documented */
		ret = -ENXIO;
		break;
	default:
		if (reg >= PMBUS_VIRT_BASE)
			ret = -ENXIO;
		else
			ret = -ENODATA;
		break;
	}

	return ret;
}

static struct pmbus_driver_info bpa_rs600_info = {
	.pages = 1,
	.format[PSC_VOLTAGE_IN] = linear,
	.format[PSC_VOLTAGE_OUT] = linear,
	.format[PSC_CURRENT_IN] = linear,
	.format[PSC_CURRENT_OUT] = linear,
	.format[PSC_POWER] = linear,
	.format[PSC_TEMPERATURE] = linear,
	.format[PSC_FAN] = linear,
	.func[0] = PMBUS_HAVE_VIN | PMBUS_HAVE_VOUT |
		PMBUS_HAVE_IIN | PMBUS_HAVE_IOUT |
		PMBUS_HAVE_PIN | PMBUS_HAVE_POUT |
		PMBUS_HAVE_TEMP | PMBUS_HAVE_TEMP2 |
		PMBUS_HAVE_FAN12 |
		PMBUS_HAVE_STATUS_VOUT | PMBUS_HAVE_STATUS_IOUT |
		PMBUS_HAVE_STATUS_INPUT | PMBUS_HAVE_STATUS_TEMP |
		PMBUS_HAVE_STATUS_FAN12,
	.read_byte_data = bpa_rs600_read_byte_data,
	.read_word_data = bpa_rs600_read_word_data,
};

static int bpa_rs600_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	u8 buf[I2C_SMBUS_BLOCK_MAX + 1];
	int ret;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_READ_BYTE_DATA
				     | I2C_FUNC_SMBUS_READ_WORD_DATA
				     | I2C_FUNC_SMBUS_READ_BLOCK_DATA))
		return -ENODEV;

	ret = i2c_smbus_read_block_data(client, PMBUS_MFR_MODEL, buf);
	if (ret < 0) {
		dev_err(dev, "Failed to read Manufacturer Model\n");
		return ret;
	}

	if (strncmp(buf, "BPA-RS600", 8)) {
		buf[ret] = '\0';
		dev_err(dev, "Unsupported Manufacturer Model '%s'\n", buf);
		return -ENODEV;
	}

	return pmbus_do_probe(client, &bpa_rs600_info);
}

static const struct i2c_device_id bpa_rs600_id[] = {
	{ "bpars600", 0 },
	{},
};
MODULE_DEVICE_TABLE(i2c, bpa_rs600_id);

static const struct of_device_id __maybe_unused bpa_rs600_of_match[] = {
	{ .compatible = "blutek,bpa-rs600" },
	{},
};
MODULE_DEVICE_TABLE(of, bpa_rs600_of_match);

static struct i2c_driver bpa_rs600_driver = {
	.driver = {
		.name = "bpa-rs600",
		.of_match_table = of_match_ptr(bpa_rs600_of_match),
	},
	.probe_new = bpa_rs600_probe,
	.id_table = bpa_rs600_id,
};

module_i2c_driver(bpa_rs600_driver);

MODULE_AUTHOR("Chris Packham");
MODULE_DESCRIPTION("PMBus driver for BluTek BPA-RS600");
MODULE_LICENSE("GPL");
