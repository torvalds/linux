// SPDX-License-Identifier: GPL-2.0-only
/*
 * IIO accel SPI driver for Freescale MMA7455L 3-axis 10-bit accelerometer
 * Copyright 2015 Joachim Eastwood <manabian@gmail.com>
 */

#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>

#include "mma7455.h"

static int mma7455_spi_probe(struct spi_device *spi)
{
	const struct spi_device_id *id = spi_get_device_id(spi);
	struct regmap *regmap;

	regmap = devm_regmap_init_spi(spi, &mma7455_core_regmap);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	return mma7455_core_probe(&spi->dev, regmap, id->name);
}

static void mma7455_spi_remove(struct spi_device *spi)
{
	mma7455_core_remove(&spi->dev);
}

static const struct spi_device_id mma7455_spi_ids[] = {
	{ "mma7455", 0 },
	{ "mma7456", 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, mma7455_spi_ids);

static struct spi_driver mma7455_spi_driver = {
	.probe = mma7455_spi_probe,
	.remove = mma7455_spi_remove,
	.id_table = mma7455_spi_ids,
	.driver = {
		.name = "mma7455-spi",
	},
};
module_spi_driver(mma7455_spi_driver);

MODULE_AUTHOR("Joachim Eastwood <manabian@gmail.com>");
MODULE_DESCRIPTION("Freescale MMA7455L SPI accelerometer driver");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(IIO_MMA7455);
