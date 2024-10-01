// SPDX-License-Identifier: GPL-2.0-only
/*
 * 3-axis magnetometer driver support following SPI Bosch-Sensortec chips:
 *  - BMC150
 *  - BMC156
 *  - BMM150
 *
 * Copyright (c) 2016, Intel Corporation.
 */
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/spi/spi.h>
#include <linux/acpi.h>
#include <linux/regmap.h>

#include "bmc150_magn.h"

static int bmc150_magn_spi_probe(struct spi_device *spi)
{
	struct regmap *regmap;
	const struct spi_device_id *id = spi_get_device_id(spi);

	regmap = devm_regmap_init_spi(spi, &bmc150_magn_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&spi->dev, "Failed to register spi regmap: %pe\n",
			regmap);
		return PTR_ERR(regmap);
	}
	return bmc150_magn_probe(&spi->dev, regmap, spi->irq, id->name);
}

static void bmc150_magn_spi_remove(struct spi_device *spi)
{
	bmc150_magn_remove(&spi->dev);
}

static const struct spi_device_id bmc150_magn_spi_id[] = {
	{"bmc150_magn", 0},
	{"bmc156_magn", 0},
	{"bmm150_magn", 0},
	{}
};
MODULE_DEVICE_TABLE(spi, bmc150_magn_spi_id);

static const struct acpi_device_id bmc150_magn_acpi_match[] = {
	{"BMC150B", 0},
	{"BMC156B", 0},
	{"BMM150B", 0},
	{},
};
MODULE_DEVICE_TABLE(acpi, bmc150_magn_acpi_match);

static struct spi_driver bmc150_magn_spi_driver = {
	.probe		= bmc150_magn_spi_probe,
	.remove		= bmc150_magn_spi_remove,
	.id_table	= bmc150_magn_spi_id,
	.driver = {
		.acpi_match_table = ACPI_PTR(bmc150_magn_acpi_match),
		.name	= "bmc150_magn_spi",
	},
};
module_spi_driver(bmc150_magn_spi_driver);

MODULE_AUTHOR("Daniel Baluta <daniel.baluta@intel.com");
MODULE_DESCRIPTION("BMC150 magnetometer SPI driver");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(IIO_BMC150_MAGN);
