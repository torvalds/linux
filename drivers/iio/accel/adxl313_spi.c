// SPDX-License-Identifier: GPL-2.0-only
/*
 * ADXL313 3-Axis Digital Accelerometer
 *
 * Copyright (c) 2021 Lucas Stankus <lucas.p.stankus@gmail.com>
 *
 * Datasheet: https://www.analog.com/media/en/technical-documentation/data-sheets/ADXL313.pdf
 */

#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>

#include "adxl313.h"

static const struct regmap_config adxl313_spi_regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.rd_table	= &adxl313_readable_regs_table,
	.wr_table	= &adxl313_writable_regs_table,
	.max_register	= 0x39,
	 /* Setting bits 7 and 6 enables multiple-byte read */
	.read_flag_mask	= BIT(7) | BIT(6),
};

static int adxl313_spi_setup(struct device *dev, struct regmap *regmap)
{
	struct spi_device *spi = container_of(dev, struct spi_device, dev);
	int ret;

	if (spi->mode & SPI_3WIRE) {
		ret = regmap_write(regmap, ADXL313_REG_DATA_FORMAT,
				   ADXL313_SPI_3WIRE);
		if (ret)
			return ret;
	}

	return regmap_update_bits(regmap, ADXL313_REG_POWER_CTL,
				  ADXL313_I2C_DISABLE, ADXL313_I2C_DISABLE);
}

static int adxl313_spi_probe(struct spi_device *spi)
{
	const struct spi_device_id *id = spi_get_device_id(spi);
	struct regmap *regmap;
	int ret;

	spi->mode |= SPI_MODE_3;
	ret = spi_setup(spi);
	if (ret)
		return ret;

	regmap = devm_regmap_init_spi(spi, &adxl313_spi_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&spi->dev, "Error initializing spi regmap: %ld\n",
			PTR_ERR(regmap));
		return PTR_ERR(regmap);
	}

	return adxl313_core_probe(&spi->dev, regmap, id->name,
				  &adxl313_spi_setup);
}

static const struct spi_device_id adxl313_spi_id[] = {
	{ "adxl313" },
	{ }
};

MODULE_DEVICE_TABLE(spi, adxl313_spi_id);

static const struct of_device_id adxl313_of_match[] = {
	{ .compatible = "adi,adxl313" },
	{ }
};

MODULE_DEVICE_TABLE(of, adxl313_of_match);

static struct spi_driver adxl313_spi_driver = {
	.driver = {
		.name	= "adxl313_spi",
		.of_match_table = adxl313_of_match,
	},
	.probe		= adxl313_spi_probe,
	.id_table	= adxl313_spi_id,
};

module_spi_driver(adxl313_spi_driver);

MODULE_AUTHOR("Lucas Stankus <lucas.p.stankus@gmail.com>");
MODULE_DESCRIPTION("ADXL313 3-Axis Digital Accelerometer SPI driver");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(IIO_ADXL313);
