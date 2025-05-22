// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics uvis25 spi driver
 *
 * Copyright 2017 STMicroelectronics Inc.
 *
 * Lorenzo Bianconi <lorenzo.bianconi83@gmail.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <linux/regmap.h>

#include "st_uvis25.h"

#define UVIS25_SENSORS_SPI_READ		BIT(7)
#define UVIS25_SPI_AUTO_INCREMENT	BIT(6)

static const struct regmap_config st_uvis25_spi_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.read_flag_mask = UVIS25_SENSORS_SPI_READ | UVIS25_SPI_AUTO_INCREMENT,
	.write_flag_mask = UVIS25_SPI_AUTO_INCREMENT,
};

static int st_uvis25_spi_probe(struct spi_device *spi)
{
	struct regmap *regmap;

	regmap = devm_regmap_init_spi(spi, &st_uvis25_spi_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&spi->dev, "Failed to register spi regmap %ld\n",
			PTR_ERR(regmap));
		return PTR_ERR(regmap);
	}

	return st_uvis25_probe(&spi->dev, spi->irq, regmap);
}

static const struct of_device_id st_uvis25_spi_of_match[] = {
	{ .compatible = "st,uvis25", },
	{ }
};
MODULE_DEVICE_TABLE(of, st_uvis25_spi_of_match);

static const struct spi_device_id st_uvis25_spi_id_table[] = {
	{ ST_UVIS25_DEV_NAME },
	{ }
};
MODULE_DEVICE_TABLE(spi, st_uvis25_spi_id_table);

static struct spi_driver st_uvis25_driver = {
	.driver = {
		.name = "st_uvis25_spi",
		.pm = pm_sleep_ptr(&st_uvis25_pm_ops),
		.of_match_table = st_uvis25_spi_of_match,
	},
	.probe = st_uvis25_spi_probe,
	.id_table = st_uvis25_spi_id_table,
};
module_spi_driver(st_uvis25_driver);

MODULE_AUTHOR("Lorenzo Bianconi <lorenzo.bianconi83@gmail.com>");
MODULE_DESCRIPTION("STMicroelectronics uvis25 spi driver");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS("IIO_UVIS25");
