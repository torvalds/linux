// SPDX-License-Identifier: GPL-2.0
/*
 * Hardware monitoring driver for Analog Devices MAX20830
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 */

#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/string.h>
#include "pmbus.h"

#define MAX20830_IC_DEVICE_ID_LENGTH	9

struct max20830_data {
	struct pmbus_driver_info info;
	u32 vout_rfb1;
	u32 vout_rfb2;
};

static const char * const supported_chip_ids[] = {
	"MAX20830",
	"MAX20830C",
	"MAX20840C",
};

/*
 * MAX20830 only supports READ_VOUT for VOUT monitoring.
 *
 * Limit registers (VOUT_OV_WARN_LIMIT, VOUT_OV_FAULT_LIMIT, etc.) are not
 * supported by this driver and return -ENODATA. This means sysfs attributes
 * like in1_max, in1_crit, etc. will not be available. Only in1_input (the
 * scaled output voltage) is supported.
 *
 * MAX20830 uses an external resistor divider for voltage sensing:
 * - VOUT_COMMAND and VOUT_MAX set the reference voltage at the feedback pin
 * - READ_VOUT reports the feedback voltage, which needs to be scaled for actual
 *   output voltage
 *
 * Scaling formula: vout_actual = vout_fb × (1 + RFB1 / RFB2)
 *
 * If regulator support is added in the future, some adjustments are needed to
 * ensure correct feedback voltages are set.
 */
static int max20830_read_word_data(struct i2c_client *client, int page,
				   int phase, int reg)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	const struct max20830_data *data = container_of(info, struct max20830_data, info);
	int ret;
	u64 temp;

	switch (reg) {
	case PMBUS_READ_VOUT:
		ret = pmbus_read_word_data(client, page, phase, reg);
		if (ret < 0)
			return ret;

		/* Apply voltage divider scaling if resistors are non-zero */
		if (data->vout_rfb1 && data->vout_rfb2) {
			temp = (u64)data->vout_rfb1 + (u64)data->vout_rfb2;
			temp = DIV_ROUND_CLOSEST_ULL((u64)ret * temp, data->vout_rfb2);
			ret = clamp_val(temp, 0, 0xFFFF);
		}
		return ret;
	default:
		return -ENODATA;
	}
}

static struct pmbus_driver_info max20830_info = {
	.pages = 1,
	.format[PSC_VOLTAGE_IN] = linear,
	.format[PSC_VOLTAGE_OUT] = linear,
	.format[PSC_CURRENT_OUT] = linear,
	.format[PSC_TEMPERATURE] = linear,
	.func[0] = PMBUS_HAVE_VIN | PMBUS_HAVE_VOUT | PMBUS_HAVE_IOUT |
		PMBUS_HAVE_TEMP |
		PMBUS_HAVE_STATUS_VOUT | PMBUS_HAVE_STATUS_IOUT |
		PMBUS_HAVE_STATUS_INPUT | PMBUS_HAVE_STATUS_TEMP,
	.read_word_data = max20830_read_word_data,
	.have_pmbus_revision = true,
	.pmbus_revision = PMBUS_REV_13,
};

static int max20830_probe(struct i2c_client *client)
{
	u8 buf[I2C_SMBUS_BLOCK_MAX + 1] = {};
	struct max20830_data *data;
	int i, ret;

	data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->info = max20830_info;

	/* Read optional voltage divider resistor values */
	device_property_read_u32(&client->dev, "adi,vout-rfb1-ohms", &data->vout_rfb1);
	device_property_read_u32(&client->dev, "adi,vout-rfb2-ohms", &data->vout_rfb2);

	ret = pmbus_read_smbus_i2c_block_data(client, PMBUS_IC_DEVICE_ID, buf);
	if (ret < 0)
		return dev_err_probe(&client->dev, ret,
				     "Failed to read IC_DEVICE_ID\n");

	/* Verify we read the expected number of bytes */
	if (ret < MAX20830_IC_DEVICE_ID_LENGTH)
		return dev_err_probe(&client->dev, -ENODEV,
				     "IC_DEVICE_ID too short: expected %d bytes, got %d\n",
				     MAX20830_IC_DEVICE_ID_LENGTH, ret);

	/* Null-terminate the string */
	buf[ret] = '\0';

	/* Verify the device ID matches what we expect */
	for (i = 0; i < ARRAY_SIZE(supported_chip_ids); i++) {
		if (!strcmp(buf, supported_chip_ids[i]))
			break;
	}

	/* No match found - unsupported device */
	if (i == ARRAY_SIZE(supported_chip_ids))
		return dev_err_probe(&client->dev, -ENODEV,
				     "Unsupported device: '%*pE'\n", ret, buf);

	return pmbus_do_probe(client, &data->info);
}

static const struct i2c_device_id max20830_id[] = {
	{"max20830"},
	{ }
};
MODULE_DEVICE_TABLE(i2c, max20830_id);

static const struct of_device_id max20830_of_match[] = {
	{ .compatible = "adi,max20830" },
	{ }
};
MODULE_DEVICE_TABLE(of, max20830_of_match);

static struct i2c_driver max20830_driver = {
	.driver = {
		.name = "max20830",
		.of_match_table = max20830_of_match,
	},
	.probe = max20830_probe,
	.id_table = max20830_id,
};

module_i2c_driver(max20830_driver);

MODULE_AUTHOR("Alexis Czezar Torreno <alexisczezar.torreno@analog.com>");
MODULE_DESCRIPTION("PMBus driver for Analog Devices MAX20830");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("PMBUS");
