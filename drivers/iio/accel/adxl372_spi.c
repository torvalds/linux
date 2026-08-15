// SPDX-License-Identifier: GPL-2.0+
/*
 * ADXL371/ADXL372 3-Axis Digital Accelerometer SPI driver
 *
 * Copyright 2018 Analog Devices Inc.
 */

#include <linux/module.h>
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
	const struct adxl372_chip_info *chip_info;
	struct regmap *regmap;

	chip_info = spi_get_device_match_data(spi);

	regmap = devm_regmap_init_spi(spi, &adxl372_spi_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	return adxl372_probe(&spi->dev, regmap, spi->irq, chip_info);
}

static const struct spi_device_id adxl372_spi_id[] = {
	{ "adxl371", (kernel_ulong_t)&adxl371_chip_info },
	{ "adxl372", (kernel_ulong_t)&adxl372_chip_info },
	{ }
};
MODULE_DEVICE_TABLE(spi, adxl372_spi_id);

static const struct of_device_id adxl372_of_match[] = {
	{ .compatible = "adi,adxl371", .data = &adxl371_chip_info },
	{ .compatible = "adi,adxl372", .data = &adxl372_chip_info },
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
MODULE_AUTHOR("Antoniu Miclaus <antoniu.miclaus@analog.com>");
MODULE_DESCRIPTION("Analog Devices ADXL371/ADXL372 3-axis accelerometer SPI driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_ADXL372");
