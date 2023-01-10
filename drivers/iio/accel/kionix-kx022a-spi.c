// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 ROHM Semiconductors
 *
 * ROHM/KIONIX KX022A accelerometer driver
 */

#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>

#include "kionix-kx022a.h"

static int kx022a_spi_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct regmap *regmap;

	if (!spi->irq) {
		dev_err(dev, "No IRQ configured\n");
		return -EINVAL;
	}

	regmap = devm_regmap_init_spi(spi, &kx022a_regmap);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap),
				     "Failed to initialize Regmap\n");

	return kx022a_probe_internal(dev);
}

static const struct spi_device_id kx022a_id[] = {
	{ "kx022a" },
	{ }
};
MODULE_DEVICE_TABLE(spi, kx022a_id);

static const struct of_device_id kx022a_of_match[] = {
	{ .compatible = "kionix,kx022a", },
	{ }
};
MODULE_DEVICE_TABLE(of, kx022a_of_match);

static struct spi_driver kx022a_spi_driver = {
	.driver = {
		.name   = "kx022a-spi",
		.of_match_table = kx022a_of_match,
	},
	.probe = kx022a_spi_probe,
	.id_table = kx022a_id,
};
module_spi_driver(kx022a_spi_driver);

MODULE_DESCRIPTION("ROHM/Kionix kx022A accelerometer driver");
MODULE_AUTHOR("Matti Vaittinen <matti.vaittinen@fi.rohmeurope.com>");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(IIO_KX022A);
