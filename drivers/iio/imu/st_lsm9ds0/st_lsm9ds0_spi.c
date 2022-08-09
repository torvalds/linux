// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics LSM9DS0 IMU driver
 *
 * Copyright (C) 2021, Intel Corporation
 *
 * Author: Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>

#include <linux/iio/common/st_sensors_spi.h>

#include "st_lsm9ds0.h"

static const struct of_device_id st_lsm9ds0_of_match[] = {
	{
		.compatible = "st,lsm9ds0-imu",
		.data = LSM9DS0_IMU_DEV_NAME,
	},
	{}
};
MODULE_DEVICE_TABLE(of, st_lsm9ds0_of_match);

static const struct spi_device_id st_lsm9ds0_id_table[] = {
	{ LSM9DS0_IMU_DEV_NAME },
	{}
};
MODULE_DEVICE_TABLE(spi, st_lsm9ds0_id_table);

static const struct regmap_config st_lsm9ds0_regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.read_flag_mask	= 0xc0,
};

static int st_lsm9ds0_spi_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct st_lsm9ds0 *lsm9ds0;
	struct regmap *regmap;

	st_sensors_dev_name_probe(dev, spi->modalias, sizeof(spi->modalias));

	lsm9ds0 = devm_kzalloc(dev, sizeof(*lsm9ds0), GFP_KERNEL);
	if (!lsm9ds0)
		return -ENOMEM;

	lsm9ds0->dev = dev;
	lsm9ds0->name = spi->modalias;
	lsm9ds0->irq = spi->irq;

	regmap = devm_regmap_init_spi(spi, &st_lsm9ds0_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	spi_set_drvdata(spi, lsm9ds0);

	return st_lsm9ds0_probe(lsm9ds0, regmap);
}

static struct spi_driver st_lsm9ds0_driver = {
	.driver = {
		.name = "st-lsm9ds0-spi",
		.of_match_table = st_lsm9ds0_of_match,
	},
	.probe = st_lsm9ds0_spi_probe,
	.id_table = st_lsm9ds0_id_table,
};
module_spi_driver(st_lsm9ds0_driver);

MODULE_AUTHOR("Andy Shevchenko <andriy.shevchenko@linux.intel.com>");
MODULE_DESCRIPTION("STMicroelectronics LSM9DS0 IMU SPI driver");
MODULE_LICENSE("GPL v2");
