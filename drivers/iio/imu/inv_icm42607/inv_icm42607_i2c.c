// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2026 InvenSense, Inc.
 */

#include <linux/bitfield.h>
#include <linux/dev_printk.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/regmap.h>

#include "inv_icm42607.h"

static int inv_icm42607_i2c_bus_setup(struct inv_icm42607_state *st)
{
	unsigned int val;
	int ret;

	ret = regmap_clear_bits(st->map, INV_ICM42607_REG_INTF_CONFIG1,
				INV_ICM42607_INTF_CONFIG1_I3C_DDR_EN |
				INV_ICM42607_INTF_CONFIG1_I3C_SDR_EN);
	if (ret)
		return ret;

	val = FIELD_PREP(INV_ICM42607_DRIVE_CONFIG2_I2C_MASK,
			 INV_ICM42607_SLEW_RATE_12_36NS);
	ret = regmap_update_bits(st->map, INV_ICM42607_REG_DRIVE_CONFIG2,
				 INV_ICM42607_DRIVE_CONFIG2_I2C_MASK, val);
	if (ret)
		return ret;

	val = FIELD_PREP(INV_ICM42607_INTF_CONFIG0_UI_SIFS_CFG_MASK,
			 INV_ICM42607_INTF_CONFIG0_UI_SIFS_CFG_SPI_DIS);

	return regmap_update_bits(st->map, INV_ICM42607_REG_INTF_CONFIG0,
				  INV_ICM42607_INTF_CONFIG0_UI_SIFS_CFG_MASK,
				  val);
}

static int inv_icm42607_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	const struct inv_icm42607_hw *hw;
	struct regmap *regmap;

	hw = i2c_get_match_data(client);
	if (!hw)
		return dev_err_probe(dev, -ENODEV, "Failed to get i2c data\n");

	regmap = devm_regmap_init_i2c(client, &inv_icm42607_regmap_config);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap),
				     "Failed to register i2c regmap\n");

	return inv_icm42607_core_probe(regmap, hw, inv_icm42607_i2c_bus_setup);
}

static const struct i2c_device_id inv_icm42607_id[] = {
	{
		.name = "icm42607",
		.driver_data = (kernel_ulong_t)&inv_icm42607_hw_data,
	}, {
		.name = "icm42607p",
		.driver_data = (kernel_ulong_t)&inv_icm42607p_hw_data,
	},
	{ }
};
MODULE_DEVICE_TABLE(i2c, inv_icm42607_id);

static const struct of_device_id inv_icm42607_of_matches[] = {
	{
		.compatible = "invensense,icm42607",
		.data = &inv_icm42607_hw_data,
	}, {
		.compatible = "invensense,icm42607p",
		.data = &inv_icm42607p_hw_data,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, inv_icm42607_of_matches);

static struct i2c_driver inv_icm42607_driver = {
	.driver = {
		.name = "inv-icm42607-i2c",
		.of_match_table = inv_icm42607_of_matches,
	},
	.id_table = inv_icm42607_id,
	.probe = inv_icm42607_probe,
};
module_i2c_driver(inv_icm42607_driver);

MODULE_AUTHOR("InvenSense, Inc.");
MODULE_DESCRIPTION("InvenSense ICM-42607 I2C driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_ICM42607");
