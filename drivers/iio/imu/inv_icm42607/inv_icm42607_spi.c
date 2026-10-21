// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2026 InvenSense, Inc.
 */

#include <linux/bitfield.h>
#include <linux/dev_printk.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>

#include "inv_icm42607.h"

static int inv_icm42607_spi_bus_setup(struct inv_icm42607_state *st)
{
	unsigned int val;
	int ret;

	/* Only support 4-wire mode for now. */
	ret = regmap_set_bits(st->map, INV_ICM42607_REG_DEVICE_CONFIG,
				      INV_ICM42607_DEVICE_CONFIG_SPI_AP_4WIRE);
	if (ret)
		return ret;

	ret = regmap_clear_bits(st->map, INV_ICM42607_REG_INTF_CONFIG1,
				INV_ICM42607_INTF_CONFIG1_I3C_DDR_EN |
				INV_ICM42607_INTF_CONFIG1_I3C_SDR_EN);
	if (ret)
		return ret;

	val = FIELD_PREP(INV_ICM42607_DRIVE_CONFIG3_SPI_MASK,
			 INV_ICM42607_SLEW_RATE_2NS);
	ret = regmap_update_bits(st->map, INV_ICM42607_REG_DRIVE_CONFIG3,
				 INV_ICM42607_DRIVE_CONFIG3_SPI_MASK, val);
	if (ret)
		return ret;

	val = FIELD_PREP(INV_ICM42607_INTF_CONFIG0_UI_SIFS_CFG_MASK,
			 INV_ICM42607_INTF_CONFIG0_UI_SIFS_CFG_I2C_DIS);

	return regmap_update_bits(st->map, INV_ICM42607_REG_INTF_CONFIG0,
				  INV_ICM42607_INTF_CONFIG0_UI_SIFS_CFG_MASK,
				  val);
}

static int inv_icm42607_probe(struct spi_device *spi)
{
	const struct inv_icm42607_hw *hw;
	struct device *dev = &spi->dev;
	struct regmap *regmap;

	hw = spi_get_device_match_data(spi);
	if (!hw)
		return dev_err_probe(dev, -ENODATA, "Failed to get SPI data\n");

	if (spi->mode & SPI_3WIRE)
		return dev_err_probe(dev, -ENODEV, "SPI 3-wire mode not supported\n");

	regmap = devm_regmap_init_spi(spi, &inv_icm42607_regmap_config);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap),
				     "Failed to register spi regmap\n");

	return inv_icm42607_core_probe(regmap, hw, inv_icm42607_spi_bus_setup);
}

static const struct spi_device_id inv_icm42607_spi_id_table[] = {
	{
		.name = "icm42607",
		.driver_data = (kernel_ulong_t)&inv_icm42607_hw_data,
	}, {
		.name = "icm42607p",
		.driver_data = (kernel_ulong_t)&inv_icm42607p_hw_data,
	},
	{ }
};
MODULE_DEVICE_TABLE(spi, inv_icm42607_spi_id_table);

static const struct of_device_id inv_icm42607_of_matches[] = {
	{
		.compatible = "invensense,icm42607",
		.data = &inv_icm42607_hw_data,
	},
	{
		.compatible = "invensense,icm42607p",
		.data = &inv_icm42607p_hw_data,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, inv_icm42607_of_matches);

static struct spi_driver inv_icm42607_driver = {
	.driver = {
		.name = "inv-icm42607-spi",
		.of_match_table = inv_icm42607_of_matches,
		.pm = pm_ptr(&inv_icm42607_pm_ops),
	},
	.id_table = inv_icm42607_spi_id_table,
	.probe = inv_icm42607_probe,
};
module_spi_driver(inv_icm42607_driver);

MODULE_AUTHOR("InvenSense, Inc.");
MODULE_DESCRIPTION("InvenSense ICM-42607 SPI driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_ICM42607");
