// SPDX-License-Identifier: GPL-2.0+
/*
 * ADXL372 3-Axis Digital Accelerometer SPI driver
 *
 * Copyright 2018 Analog Devices Inc.
 */

#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>

#include "adxl372.h"

static const struct regmap_config adxl372_spi_regmap_config = {
	.reg_bits = 7,
	.pad_bits = 1,
	.val_bits = 8,
	.read_flag_mask = BIT(0),
	.readable_noinc_reg = adxl372_readable_noinc_reg,
};

static int adxl372_spi_probe(struct spi_device *spi)
{
	const struct spi_device_id *id = spi_get_device_id(spi);
	struct regmap *regmap;

	regmap = devm_regmap_init_spi(spi, &adxl372_spi_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	return adxl372_probe(&spi->dev, regmap, spi->irq, id->name);
}

static const struct spi_device_id adxl372_spi_id[] = {
	{ "adxl372", 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, adxl372_spi_id);

static const struct of_device_id adxl372_of_match[] = {
	{ .compatible = "adi,adxl372" },
	{ }
};
MODULE_DEVICE_TABLE(of, adxl372_of_match);

static struct spi_driver adxl372_spi_driver = {
	.driver = {
		.name = "adxl372_spi",
		.of_match_table = adxl372_of_match,
	},
	.probe = adxl372_spi_probe,
	.id_table = adxl372_spi_id,
};

module_spi_driver(adxl372_spi_driver);

MODULE_AUTHOR("Stefan Popa <stefan.popa@analog.com>");
MODULE_DESCRIPTION("Analog Devices ADXL372 3-axis accelerometer SPI driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_ADXL372");
