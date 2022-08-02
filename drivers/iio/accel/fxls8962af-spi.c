// SPDX-License-Identifier: GPL-2.0
/*
 * NXP FXLS8962AF/FXLS8964AF Accelerometer SPI Driver
 *
 * Copyright 2021 Connected Cars A/S
 */

#include <linux/dev_printk.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/spi/spi.h>
#include <linux/regmap.h>

#include "fxls8962af.h"

static int fxls8962af_probe(struct spi_device *spi)
{
	struct regmap *regmap;

	regmap = devm_regmap_init_spi(spi, &fxls8962af_spi_regmap_conf);
	if (IS_ERR(regmap)) {
		dev_err(&spi->dev, "Failed to initialize spi regmap\n");
		return PTR_ERR(regmap);
	}

	return fxls8962af_core_probe(&spi->dev, regmap, spi->irq);
}

static const struct of_device_id fxls8962af_spi_of_match[] = {
	{ .compatible = "nxp,fxls8962af" },
	{ .compatible = "nxp,fxls8964af" },
	{}
};
MODULE_DEVICE_TABLE(of, fxls8962af_spi_of_match);

static const struct spi_device_id fxls8962af_spi_id_table[] = {
	{ "fxls8962af", fxls8962af },
	{ "fxls8964af", fxls8964af },
	{}
};
MODULE_DEVICE_TABLE(spi, fxls8962af_spi_id_table);

static struct spi_driver fxls8962af_driver = {
	.driver = {
		   .name = "fxls8962af_spi",
		   .pm = &fxls8962af_pm_ops,
		   .of_match_table = fxls8962af_spi_of_match,
		   },
	.probe = fxls8962af_probe,
	.id_table = fxls8962af_spi_id_table,
};
module_spi_driver(fxls8962af_driver);

MODULE_AUTHOR("Sean Nyekjaer <sean@geanix.com>");
MODULE_DESCRIPTION("NXP FXLS8962AF/FXLS8964AF accelerometer spi driver");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(IIO_FXLS8962AF);
