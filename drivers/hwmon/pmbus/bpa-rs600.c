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

enum chips { bpa_rs600, bpd_rs600 };

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

/*
 * The BPA-RS600 violates the PMBus spec. Specifically it treats the
 * mantissa as unsigned. Deal with this here to allow the PMBus core
 * to work with correctly encoded data.
 */
static int bpa_rs600_read_vin(struct i2c_client *client)
{
	int ret, exponent, mantissa;

	ret = pmbus_read_word_data(client, 0, 0xff, PMBUS_READ_VIN);
	if (ret < 0)
		return ret;

	if (ret & BIT(10)) {
		exponent = ret >> 11;
		mantissa = ret & 0x7ff;

		exponent++;
		mantissa >>= 1;

		ret = (exponent << 11) | mantissa;
	}

	return ret;
}

/*
 * Firmware V5.70 incorrectly reports 1640W for MFR_PIN_MAX.
 * Deal with this by returning a sensible value.
 */
static int bpa_rs600_read_pin_max(struct i2c_client *client)
{
	int ret;

	ret = pmbus_read_word_data(client, 0, 0xff, PMBUS_MFR_PIN_MAX);
	if (ret < 0)
		return ret;

	/* Detect invalid 1640W (linear encoding) */
	if (ret == 0x0b34)
		/* Report 700W (linear encoding) */
		return 0x095e;

	return ret;
}

static int bpa_rs600_read_word_data(struct i2c_client *client, int page, int phase, int reg)
{
	int ret;

	if (page > 0)
		return -ENXIO;

	switch (reg) {
	case PMBUS_VIN_UV_WARN_LIMIT:
	case PMBUS_VIN_OV_WARN_LIMIT:
	case PMBUS_VOUT_UV_WARN_LIMIT:
	case PMBUS_VOUT_OV_WARN_LIMIT:
	case PMBUS_IIN_OC_WARN_LIMIT:
	case PMBUS_IOUT_OC_WARN_LIMIT:
	case PMBUS_PIN_OP_WARN_LIMIT:
	case PMBUS_POUT_OP_WARN_LIMIT:
	case PMBUS_VIN_UV_FAULT_LIMIT:
	case PMBUS_VIN_OV_FAULT_LIMIT:
	case PMBUS_VOUT_UV_FAULT_LIMIT:
	case PMBUS_VOUT_OV_FAULT_LIMIT:
		/* These commands return data but it is invalid/un-documented */
		ret = -ENXIO;
		break;
	case PMBUS_READ_VIN:
		ret = bpa_rs600_read_vin(client);
		break;
	case PMBUS_MFR_PIN_MAX:
		ret = bpa_rs600_read_pin_max(client);
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

static const struct i2c_device_id bpa_rs600_id[] = {
	{ "bpa-rs600", bpa_rs600 },
	{ "bpd-rs600", bpd_rs600 },
	{},
};
MODULE_DEVICE_TABLE(i2c, bpa_rs600_id);

static int bpa_rs600_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	u8 buf[I2C_SMBUS_BLOCK_MAX + 1];
	int ret;
	const struct i2c_device_id *mid;

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

	for (mid = bpa_rs600_id; mid->name[0]; mid++) {
		if (!strncasecmp(buf, mid->name, strlen(mid->name)))
			break;
	}
	if (!mid->name[0]) {
		buf[ret] = '\0';
		dev_err(dev, "Unsupported Manufacturer Model '%s'\n", buf);
		return -ENODEV;
	}

	return pmbus_do_probe(client, &bpa_rs600_info);
}

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
	.probe = bpa_rs600_probe,
	.id_table = bpa_rs600_id,
};

module_i2c_driver(bpa_rs600_driver);

MODULE_AUTHOR("Chris Packham");
MODULE_DESCRIPTION("PMBus driver for BluTek BPA-RS600");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(PMBUS);
