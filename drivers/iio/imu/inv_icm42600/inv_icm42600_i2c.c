// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2020 InvenSense, Inc.
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/property.h>

#include "inv_icm42600.h"

static int inv_icm42600_i2c_bus_setup(struct inv_icm42600_state *st)
{
	unsigned int mask, val;
	int ret;

	/* setup interface registers */
	ret = regmap_update_bits(st->map, INV_ICM42600_REG_INTF_CONFIG6,
				 INV_ICM42600_INTF_CONFIG6_MASK,
				 INV_ICM42600_INTF_CONFIG6_I3C_EN);
	if (ret)
		return ret;

	ret = regmap_update_bits(st->map, INV_ICM42600_REG_INTF_CONFIG4,
				 INV_ICM42600_INTF_CONFIG4_I3C_BUS_ONLY, 0);
	if (ret)
		return ret;

	/* set slew rates for I2C and SPI */
	mask = INV_ICM42600_DRIVE_CONFIG_I2C_MASK |
	       INV_ICM42600_DRIVE_CONFIG_SPI_MASK;
	val = INV_ICM42600_DRIVE_CONFIG_I2C(INV_ICM42600_SLEW_RATE_12_36NS) |
	      INV_ICM42600_DRIVE_CONFIG_SPI(INV_ICM42600_SLEW_RATE_12_36NS);
	ret = regmap_update_bits(st->map, INV_ICM42600_REG_DRIVE_CONFIG,
				 mask, val);
	if (ret)
		return ret;

	/* disable SPI bus */
	return regmap_update_bits(st->map, INV_ICM42600_REG_INTF_CONFIG0,
				  INV_ICM42600_INTF_CONFIG0_UI_SIFS_CFG_MASK,
				  INV_ICM42600_INTF_CONFIG0_UI_SIFS_CFG_SPI_DIS);
}

static int inv_icm42600_probe(struct i2c_client *client)
{
	const void *match;
	enum inv_icm42600_chip chip;
	struct regmap *regmap;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_I2C_BLOCK))
		return -ENOTSUPP;

	match = device_get_match_data(&client->dev);
	if (!match)
		return -EINVAL;
	chip = (enum inv_icm42600_chip)match;

	regmap = devm_regmap_init_i2c(client, &inv_icm42600_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	return inv_icm42600_core_probe(regmap, chip, client->irq,
				       inv_icm42600_i2c_bus_setup);
}

static const struct of_device_id inv_icm42600_of_matches[] = {
	{
		.compatible = "invensense,icm42600",
		.data = (void *)INV_CHIP_ICM42600,
	}, {
		.compatible = "invensense,icm42602",
		.data = (void *)INV_CHIP_ICM42602,
	}, {
		.compatible = "invensense,icm42605",
		.data = (void *)INV_CHIP_ICM42605,
	}, {
		.compatible = "invensense,icm42622",
		.data = (void *)INV_CHIP_ICM42622,
	},
	{}
};
MODULE_DEVICE_TABLE(of, inv_icm42600_of_matches);

static struct i2c_driver inv_icm42600_driver = {
	.driver = {
		.name = "inv-icm42600-i2c",
		.of_match_table = inv_icm42600_of_matches,
		.pm = &inv_icm42600_pm_ops,
	},
	.probe_new = inv_icm42600_probe,
};
module_i2c_driver(inv_icm42600_driver);

MODULE_AUTHOR("InvenSense, Inc.");
MODULE_DESCRIPTION("InvenSense ICM-426xx I2C driver");
MODULE_LICENSE("GPL");
