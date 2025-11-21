// SPDX-License-Identifier: GPL-2.0-or-later
/* Copyright (C) 2025 InvenSense, Inc. */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/regmap.h>

#include "inv_icm45600.h"

static const struct regmap_config inv_icm45600_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static int inv_icm45600_probe(struct i2c_client *client)
{
	const struct inv_icm45600_chip_info *chip_info;
	struct regmap *regmap;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_I2C_BLOCK))
		return -ENODEV;

	chip_info = device_get_match_data(&client->dev);
	if (!chip_info)
		return -ENODEV;

	regmap = devm_regmap_init_i2c(client, &inv_icm45600_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	return inv_icm45600_core_probe(regmap, chip_info, true, NULL);
}

/*
 * The device id table is used to identify which device is
 * supported by this driver.
 */
static const struct i2c_device_id inv_icm45600_id[] = {
	{ "icm45605", (kernel_ulong_t)&inv_icm45605_chip_info },
	{ "icm45606", (kernel_ulong_t)&inv_icm45606_chip_info },
	{ "icm45608", (kernel_ulong_t)&inv_icm45608_chip_info },
	{ "icm45634", (kernel_ulong_t)&inv_icm45634_chip_info },
	{ "icm45686", (kernel_ulong_t)&inv_icm45686_chip_info },
	{ "icm45687", (kernel_ulong_t)&inv_icm45687_chip_info },
	{ "icm45688p", (kernel_ulong_t)&inv_icm45688p_chip_info },
	{ "icm45689", (kernel_ulong_t)&inv_icm45689_chip_info },
	{ }
};
MODULE_DEVICE_TABLE(i2c, inv_icm45600_id);

static const struct of_device_id inv_icm45600_of_matches[] = {
	{
		.compatible = "invensense,icm45605",
		.data = &inv_icm45605_chip_info,
	}, {
		.compatible = "invensense,icm45606",
		.data = &inv_icm45606_chip_info,
	}, {
		.compatible = "invensense,icm45608",
		.data = &inv_icm45608_chip_info,
	}, {
		.compatible = "invensense,icm45634",
		.data = &inv_icm45634_chip_info,
	}, {
		.compatible = "invensense,icm45686",
		.data = &inv_icm45686_chip_info,
	}, {
		.compatible = "invensense,icm45687",
		.data = &inv_icm45687_chip_info,
	}, {
		.compatible = "invensense,icm45688p",
		.data = &inv_icm45688p_chip_info,
	}, {
		.compatible = "invensense,icm45689",
		.data = &inv_icm45689_chip_info,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, inv_icm45600_of_matches);

static struct i2c_driver inv_icm45600_driver = {
	.driver = {
		.name = "inv-icm45600-i2c",
		.of_match_table = inv_icm45600_of_matches,
		.pm = pm_ptr(&inv_icm45600_pm_ops),
	},
	.id_table = inv_icm45600_id,
	.probe = inv_icm45600_probe,
};
module_i2c_driver(inv_icm45600_driver);

MODULE_AUTHOR("InvenSense, Inc.");
MODULE_DESCRIPTION("InvenSense ICM-456xx I2C driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_ICM45600");
