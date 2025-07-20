// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for MPS MPQ8785 Step-Down Converter
 */

#include <linux/i2c.h>
#include <linux/bitops.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/of_device.h>
#include "pmbus.h"

#define MPM82504_READ_TEMPERATURE_1_SIGN_POS	9

enum chips { mpm3695, mpm3695_25, mpm82504, mpq8785 };

static u16 voltage_scale_loop_max_val[] = {
	[mpm3695] = GENMASK(9, 0),
	[mpm3695_25] = GENMASK(11, 0),
	[mpm82504] = GENMASK(9, 0),
	[mpq8785] = GENMASK(10, 0),
};

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

static int mpm82504_read_word_data(struct i2c_client *client, int page,
				   int phase, int reg)
{
	int ret;

	ret = pmbus_read_word_data(client, page, phase, reg);

	if (ret < 0 || reg != PMBUS_READ_TEMPERATURE_1)
		return ret;

	/* Fix PMBUS_READ_TEMPERATURE_1 signedness */
	return sign_extend32(ret, MPM82504_READ_TEMPERATURE_1_SIGN_POS) & 0xffff;
}

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
};

static const struct i2c_device_id mpq8785_id[] = {
	{ "mpm3695", mpm3695 },
	{ "mpm3695-25", mpm3695_25 },
	{ "mpm82504", mpm82504 },
	{ "mpq8785", mpq8785 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, mpq8785_id);

static const struct of_device_id __maybe_unused mpq8785_of_match[] = {
	{ .compatible = "mps,mpm3695", .data = (void *)mpm3695 },
	{ .compatible = "mps,mpm3695-25", .data = (void *)mpm3695_25 },
	{ .compatible = "mps,mpm82504", .data = (void *)mpm82504 },
	{ .compatible = "mps,mpq8785", .data = (void *)mpq8785 },
	{}
};
MODULE_DEVICE_TABLE(of, mpq8785_of_match);

static int mpq8785_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct pmbus_driver_info *info;
	enum chips chip_id;
	u32 voltage_scale;
	int ret;

	info = devm_kmemdup(dev, &mpq8785_info, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	if (dev->of_node)
		chip_id = (kernel_ulong_t)of_device_get_match_data(dev);
	else
		chip_id = (kernel_ulong_t)i2c_get_match_data(client);

	switch (chip_id) {
	case mpm3695:
	case mpm3695_25:
	case mpm82504:
		info->format[PSC_VOLTAGE_OUT] = direct;
		info->m[PSC_VOLTAGE_OUT] = 8;
		info->b[PSC_VOLTAGE_OUT] = 0;
		info->R[PSC_VOLTAGE_OUT] = 2;
		info->read_word_data = mpm82504_read_word_data;
		break;
	case mpq8785:
		info->identify = mpq8785_identify;
		break;
	default:
		return -ENODEV;
	}

	if (!device_property_read_u32(dev, "mps,vout-fb-divider-ratio-permille",
				      &voltage_scale)) {
		if (voltage_scale > voltage_scale_loop_max_val[chip_id])
			return -EINVAL;

		ret = i2c_smbus_write_word_data(client, PMBUS_VOUT_SCALE_LOOP,
						voltage_scale);
		if (ret)
			return ret;
	}

	return pmbus_do_probe(client, info);
};

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
