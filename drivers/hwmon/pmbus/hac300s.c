// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: 2024 CERN (home.cern)
/*
 * Hardware monitoring driver for Hi-Tron HAC300S PSU.
 *
 * NOTE: The HAC300S device does not support the PMBUS_VOUT_MODE register.
 * On top of that, it returns the Voltage output values in Linear11 which is
 * not adhering to the PMBus specifications. (PMBus Specification Part II,
 * Section 7.1-7.3). For that reason the PMBUS_VOUT_MODE register is being faked
 * and returns the exponent value of the READ_VOUT register. The exponent part
 * of the VOUT_* registers is being cleared in order to return the mantissa to
 * the pmbus core.
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pmbus.h>

#include "pmbus.h"

#define LINEAR11_EXPONENT_MASK GENMASK(15, 11)
#define LINEAR11_MANTISSA_MASK GENMASK(10, 0)

#define to_hac300s_data(x) container_of(x, struct hac300s_data, info)

struct hac300s_data {
	struct pmbus_driver_info info;
	s8 exponent;
};

static int hac300s_read_byte_data(struct i2c_client *client, int page, int reg)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	struct hac300s_data *data = to_hac300s_data(info);

	if (reg == PMBUS_VOUT_MODE)
		return data->exponent;

	return -ENODATA;
}

static int hac300s_read_word_data(struct i2c_client *client, int page,
				  int phase, int reg)
{
	int rv;

	switch (reg) {
	case PMBUS_VOUT_OV_WARN_LIMIT:
	case PMBUS_VOUT_UV_WARN_LIMIT:
	case PMBUS_VOUT_OV_FAULT_LIMIT:
	case PMBUS_VOUT_UV_FAULT_LIMIT:
	case PMBUS_MFR_VOUT_MAX:
	case PMBUS_MFR_VOUT_MIN:
	case PMBUS_READ_VOUT:
		rv = pmbus_read_word_data(client, page, phase, reg);
		return FIELD_GET(LINEAR11_MANTISSA_MASK, rv);
	default:
		return -ENODATA;
	}
}

static struct pmbus_driver_info hac300s_info = {
	.pages = 1,
	.func[0] = PMBUS_HAVE_TEMP | PMBUS_HAVE_TEMP2 | \
		   PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT | \
		   PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT | \
		   PMBUS_HAVE_POUT | PMBUS_HAVE_STATUS_TEMP,
	.read_byte_data = hac300s_read_byte_data,
	.read_word_data = hac300s_read_word_data,
	.format[PSC_VOLTAGE_OUT] = linear,
};

static struct pmbus_platform_data hac300s_pdata = {
	.flags = PMBUS_NO_CAPABILITY,
};

static int hac300s_probe(struct i2c_client *client)
{
	struct hac300s_data *data;
	int rv;

	data = devm_kzalloc(&client->dev, sizeof(struct hac300s_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_READ_BYTE_DATA |
				     I2C_FUNC_SMBUS_READ_WORD_DATA))
		return -ENODEV;

	rv = i2c_smbus_read_word_data(client, PMBUS_READ_VOUT);
	if (rv < 0)
		return dev_err_probe(&client->dev, rv, "Failed to read vout_mode\n");

	data->exponent = FIELD_GET(LINEAR11_EXPONENT_MASK, rv);
	data->info = hac300s_info;
	client->dev.platform_data = &hac300s_pdata;
	return pmbus_do_probe(client, &data->info);
}

static const struct of_device_id hac300s_of_match[] = {
	{ .compatible = "hitron,hac300s" },
	{}
};
MODULE_DEVICE_TABLE(of, hac300s_of_match);

static const struct i2c_device_id hac300s_id[] = {
	{"hac300s", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, hac300s_id);

static struct i2c_driver hac300s_driver = {
	.driver = {
		   .name = "hac300s",
		   .of_match_table = hac300s_of_match,
	},
	.probe = hac300s_probe,
	.id_table = hac300s_id,

};
module_i2c_driver(hac300s_driver);

MODULE_AUTHOR("Vasileios Amoiridis");
MODULE_DESCRIPTION("PMBus driver for Hi-Tron HAC300S PSU");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("PMBUS");
