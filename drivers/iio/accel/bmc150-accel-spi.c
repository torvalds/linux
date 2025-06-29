// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * 3-axis accelerometer driver supporting SPI Bosch-Sensortec accelerometer chip
 * Copyright © 2015 Pengutronix, Markus Pargmann <mpa@pengutronix.de>
 */

#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>

#include "bmc150-accel.h"

static int bmc150_accel_probe(struct spi_device *spi)
{
	struct regmap *regmap;
	const char *name = NULL;
	enum bmc150_type type = BOSCH_UNKNOWN;
	const struct spi_device_id *id = spi_get_device_id(spi);

	regmap = devm_regmap_init_spi(spi, &bmc150_regmap_conf);
	if (IS_ERR(regmap)) {
		dev_err(&spi->dev, "Failed to initialize spi regmap\n");
		return PTR_ERR(regmap);
	}

	if (id) {
		name = id->name;
		type = id->driver_data;
	}

	return bmc150_accel_core_probe(&spi->dev, regmap, spi->irq, type, name,
				       true);
}

static void bmc150_accel_remove(struct spi_device *spi)
{
	bmc150_accel_core_remove(&spi->dev);
}

static const struct acpi_device_id bmc150_accel_acpi_match[] = {
	{"BMA0255"},
	{"BMA0280"},
	{"BMA222"},
	{"BMA222E"},
	{"BMA250E"},
	{"BMC150A"},
	{"BMI055A"},
	{"BSBA0150"},
	{ }
};
MODULE_DEVICE_TABLE(acpi, bmc150_accel_acpi_match);

static const struct spi_device_id bmc150_accel_id[] = {
	{"bma222"},
	{"bma222e"},
	{"bma250e"},
	{"bma253"},
	{"bma255"},
	{"bma280"},
	{"bmc150_accel"},
	{"bmc156_accel", BOSCH_BMC156},
	{"bmi055_accel"},
	{ }
};
MODULE_DEVICE_TABLE(spi, bmc150_accel_id);

static struct spi_driver bmc150_accel_driver = {
	.driver = {
		.name	= "bmc150_accel_spi",
		.acpi_match_table = bmc150_accel_acpi_match,
		.pm	= &bmc150_accel_pm_ops,
	},
	.probe		= bmc150_accel_probe,
	.remove		= bmc150_accel_remove,
	.id_table	= bmc150_accel_id,
};
module_spi_driver(bmc150_accel_driver);

MODULE_AUTHOR("Markus Pargmann <mpa@pengutronix.de>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("BMC150 SPI accelerometer driver");
MODULE_IMPORT_NS("IIO_BMC150");
