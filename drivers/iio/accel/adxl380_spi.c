// SPDX-License-Identifier: GPL-2.0+
/*
 * ADXL380 3-Axis Digital Accelerometer SPI driver
 *
 * Copyright 2024 Analog Devices Inc.
 */

#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>

#include "adxl380.h"

static const struct regmap_config adxl380_spi_regmap_config = {
	.reg_bits = 7,
	.pad_bits = 1,
	.val_bits = 8,
	.read_flag_mask = BIT(0),
	.readable_noinc_reg = adxl380_readable_noinc_reg,
};

static int adxl380_spi_probe(struct spi_device *spi)
{
	const struct adxl380_chip_info *chip_data;
	struct regmap *regmap;

	chip_data = spi_get_device_match_data(spi);

	regmap = devm_regmap_init_spi(spi, &adxl380_spi_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	return adxl380_probe(&spi->dev, regmap, chip_data);
}

static const struct spi_device_id adxl380_spi_id[] = {
	{ "adxl380", (kernel_ulong_t)&adxl380_chip_info },
	{ "adxl382", (kernel_ulong_t)&adxl382_chip_info },
	{ }
};
MODULE_DEVICE_TABLE(spi, adxl380_spi_id);

static const struct of_device_id adxl380_of_match[] = {
	{ .compatible = "adi,adxl380", .data = &adxl380_chip_info },
	{ .compatible = "adi,adxl382", .data = &adxl382_chip_info },
	{ }
};
MODULE_DEVICE_TABLE(of, adxl380_of_match);

static struct spi_driver adxl380_spi_driver = {
	.driver = {
		.name = "adxl380_spi",
		.of_match_table = adxl380_of_match,
	},
	.probe = adxl380_spi_probe,
	.id_table = adxl380_spi_id,
};

module_spi_driver(adxl380_spi_driver);

MODULE_AUTHOR("Ramona Gradinariu <ramona.gradinariu@analog.com>");
MODULE_AUTHOR("Antoniu Miclaus <antoniu.miclaus@analog.com>");
MODULE_DESCRIPTION("Analog Devices ADXL380 3-axis accelerometer SPI driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_ADXL380");
