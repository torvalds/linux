// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for NXP Fxas21002c Gyroscope - SPI
 *
 * Copyright (C) 2019 Linaro Ltd.
 */

#include <linux/err.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>

#include "fxas21002c.h"

static const struct regmap_config fxas21002c_regmap_spi_conf = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = FXAS21002C_REG_CTRL3,
};

static int fxas21002c_spi_probe(struct spi_device *spi)
{
	const struct spi_device_id *id = spi_get_device_id(spi);
	struct regmap *regmap;

	regmap = devm_regmap_init_spi(spi, &fxas21002c_regmap_spi_conf);
	if (IS_ERR(regmap)) {
		dev_err(&spi->dev, "Failed to register spi regmap: %ld\n",
			PTR_ERR(regmap));
		return PTR_ERR(regmap);
	}

	return fxas21002c_core_probe(&spi->dev, regmap, spi->irq, id->name);
}

static void fxas21002c_spi_remove(struct spi_device *spi)
{
	fxas21002c_core_remove(&spi->dev);
}

static const struct spi_device_id fxas21002c_spi_id[] = {
	{ "fxas21002c", 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, fxas21002c_spi_id);

static const struct of_device_id fxas21002c_spi_of_match[] = {
	{ .compatible = "nxp,fxas21002c", },
	{ }
};
MODULE_DEVICE_TABLE(of, fxas21002c_spi_of_match);

static struct spi_driver fxas21002c_spi_driver = {
	.driver = {
		.name = "fxas21002c_spi",
		.pm = pm_ptr(&fxas21002c_pm_ops),
		.of_match_table = fxas21002c_spi_of_match,
	},
	.probe		= fxas21002c_spi_probe,
	.remove		= fxas21002c_spi_remove,
	.id_table	= fxas21002c_spi_id,
};
module_spi_driver(fxas21002c_spi_driver);

MODULE_AUTHOR("Rui Miguel Silva <rui.silva@linaro.org>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("FXAS21002C SPI Gyro driver");
MODULE_IMPORT_NS("IIO_FXAS21002C");
