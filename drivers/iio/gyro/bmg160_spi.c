// SPDX-License-Identifier: GPL-2.0-only
#include <linux/spi/spi.h>
#include <linux/regmap.h>
#include <linux/iio/iio.h>
#include <linux/module.h>

#include "bmg160.h"

static const struct regmap_config bmg160_regmap_spi_conf = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0x3f,
};

static int bmg160_spi_probe(struct spi_device *spi)
{
	struct regmap *regmap;
	const struct spi_device_id *id = spi_get_device_id(spi);

	regmap = devm_regmap_init_spi(spi, &bmg160_regmap_spi_conf);
	if (IS_ERR(regmap)) {
		dev_err(&spi->dev, "Failed to register spi regmap: %pe\n",
			regmap);
		return PTR_ERR(regmap);
	}

	return bmg160_core_probe(&spi->dev, regmap, spi->irq, id->name);
}

static int bmg160_spi_remove(struct spi_device *spi)
{
	bmg160_core_remove(&spi->dev);

	return 0;
}

static const struct spi_device_id bmg160_spi_id[] = {
	{"bmg160", 0},
	{"bmi055_gyro", 0},
	{"bmi088_gyro", 0},
	{}
};

MODULE_DEVICE_TABLE(spi, bmg160_spi_id);

static struct spi_driver bmg160_spi_driver = {
	.driver = {
		.name	= "bmg160_spi",
		.pm	= &bmg160_pm_ops,
	},
	.probe		= bmg160_spi_probe,
	.remove		= bmg160_spi_remove,
	.id_table	= bmg160_spi_id,
};
module_spi_driver(bmg160_spi_driver);

MODULE_AUTHOR("Markus Pargmann <mpa@pengutronix.de>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("BMG160 SPI Gyro driver");
