// SPDX-License-Identifier: GPL-2.0
/*
 * ScioSense ENS160 multi-gas sensor SPI driver
 *
 * Copyright (c) 2024 Gustavo Silva <gustavograzs@gmail.com>
 */

#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>

#include "ens160.h"

#define ENS160_SPI_READ BIT(0)

static const struct regmap_config ens160_regmap_spi_conf = {
	.reg_bits = 8,
	.val_bits = 8,
	.reg_shift = -1,
	.read_flag_mask = ENS160_SPI_READ,
};

static int ens160_spi_probe(struct spi_device *spi)
{
	struct regmap *regmap;

	regmap = devm_regmap_init_spi(spi, &ens160_regmap_spi_conf);
	if (IS_ERR(regmap))
		return dev_err_probe(&spi->dev, PTR_ERR(regmap),
				     "Failed to register spi regmap\n");

	return devm_ens160_core_probe(&spi->dev, regmap, spi->irq, "ens160");
}

static const struct of_device_id ens160_spi_of_match[] = {
	{ .compatible = "sciosense,ens160" },
	{ }
};
MODULE_DEVICE_TABLE(of, ens160_spi_of_match);

static const struct spi_device_id ens160_spi_id[] = {
	{ "ens160" },
	{ }
};
MODULE_DEVICE_TABLE(spi, ens160_spi_id);

static struct spi_driver ens160_spi_driver = {
	.driver = {
		.name	= "ens160",
		.of_match_table = ens160_spi_of_match,
		.pm = pm_sleep_ptr(&ens160_pm_ops),
	},
	.probe		= ens160_spi_probe,
	.id_table	= ens160_spi_id,
};
module_spi_driver(ens160_spi_driver);

MODULE_AUTHOR("Gustavo Silva <gustavograzs@gmail.com>");
MODULE_DESCRIPTION("ScioSense ENS160 SPI driver");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS("IIO_ENS160");
