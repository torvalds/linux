// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics hts221 spi driver
 *
 * Copyright 2016 STMicroelectronics Inc.
 *
 * Lorenzo Bianconi <lorenzo.bianconi@st.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <linux/regmap.h>

#include "hts221.h"

#define HTS221_SPI_READ			BIT(7)
#define HTS221_SPI_AUTO_INCREMENT	BIT(6)

static const struct regmap_config hts221_spi_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.write_flag_mask = HTS221_SPI_AUTO_INCREMENT,
	.read_flag_mask = HTS221_SPI_READ | HTS221_SPI_AUTO_INCREMENT,
};

static int hts221_spi_probe(struct spi_device *spi)
{
	struct regmap *regmap;

	regmap = devm_regmap_init_spi(spi, &hts221_spi_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&spi->dev, "Failed to register spi regmap %d\n",
			(int)PTR_ERR(regmap));
		return PTR_ERR(regmap);
	}

	return hts221_probe(&spi->dev, spi->irq,
			    spi->modalias, regmap);
}

static const struct of_device_id hts221_spi_of_match[] = {
	{ .compatible = "st,hts221", },
	{},
};
MODULE_DEVICE_TABLE(of, hts221_spi_of_match);

static const struct spi_device_id hts221_spi_id_table[] = {
	{ HTS221_DEV_NAME },
	{},
};
MODULE_DEVICE_TABLE(spi, hts221_spi_id_table);

static struct spi_driver hts221_driver = {
	.driver = {
		.name = "hts221_spi",
		.pm = &hts221_pm_ops,
		.of_match_table = of_match_ptr(hts221_spi_of_match),
	},
	.probe = hts221_spi_probe,
	.id_table = hts221_spi_id_table,
};
module_spi_driver(hts221_driver);

MODULE_AUTHOR("Lorenzo Bianconi <lorenzo.bianconi@st.com>");
MODULE_DESCRIPTION("STMicroelectronics hts221 spi driver");
MODULE_LICENSE("GPL v2");
