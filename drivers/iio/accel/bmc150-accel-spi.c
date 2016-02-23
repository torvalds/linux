/*
 * 3-axis accelerometer driver supporting SPI Bosch-Sensortec accelerometer chip
 * Copyright © 2015 Pengutronix, Markus Pargmann <mpa@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/acpi.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>

#include "bmc150-accel.h"

static const struct regmap_config bmc150_spi_regmap_conf = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0x3f,
};

static int bmc150_accel_probe(struct spi_device *spi)
{
	struct regmap *regmap;
	const struct spi_device_id *id = spi_get_device_id(spi);

	regmap = devm_regmap_init_spi(spi, &bmc150_spi_regmap_conf);
	if (IS_ERR(regmap)) {
		dev_err(&spi->dev, "Failed to initialize spi regmap\n");
		return PTR_ERR(regmap);
	}

	return bmc150_accel_core_probe(&spi->dev, regmap, spi->irq, id->name,
				       true);
}

static int bmc150_accel_remove(struct spi_device *spi)
{
	return bmc150_accel_core_remove(&spi->dev);
}

static const struct acpi_device_id bmc150_accel_acpi_match[] = {
	{"BSBA0150",	bmc150},
	{"BMC150A",	bmc150},
	{"BMI055A",	bmi055},
	{"BMA0255",	bma255},
	{"BMA250E",	bma250e},
	{"BMA222E",	bma222e},
	{"BMA0280",	bma280},
	{ },
};
MODULE_DEVICE_TABLE(acpi, bmc150_accel_acpi_match);

static const struct spi_device_id bmc150_accel_id[] = {
	{"bmc150_accel",	bmc150},
	{"bmi055_accel",	bmi055},
	{"bma255",		bma255},
	{"bma250e",		bma250e},
	{"bma222e",		bma222e},
	{"bma280",		bma280},
	{}
};
MODULE_DEVICE_TABLE(spi, bmc150_accel_id);

static struct spi_driver bmc150_accel_driver = {
	.driver = {
		.name	= "bmc150_accel_spi",
		.acpi_match_table = ACPI_PTR(bmc150_accel_acpi_match),
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
