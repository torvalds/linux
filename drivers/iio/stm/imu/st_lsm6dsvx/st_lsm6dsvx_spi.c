// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics st_lsm6dsvx spi driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2022 STMicroelectronics Inc.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/of.h>

#include "st_lsm6dsvx.h"

static const struct regmap_config st_lsm6dsvx_spi_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static int st_lsm6dsvx_spi_probe(struct spi_device *spi)
{
	struct regmap *regmap;

	regmap = devm_regmap_init_spi(spi, &st_lsm6dsvx_spi_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&spi->dev, "Failed to register spi regmap %d\n",
			(int)PTR_ERR(regmap));

		return PTR_ERR(regmap);
	}

	return st_lsm6dsvx_probe(&spi->dev, spi->irq, regmap);
}

static int st_lsm6dsvx_spi_remove(struct spi_device *spi)
{
	return st_lsm6dsvx_remove(&spi->dev);
}

static const struct of_device_id st_lsm6dsvx_spi_of_match[] = {
	{
		.compatible = "st," ST_LSM6DSV16X_DEV_NAME,
	},
	{},
};
MODULE_DEVICE_TABLE(of, st_lsm6dsvx_spi_of_match);

static const struct spi_device_id st_lsm6dsvx_spi_id_table[] = {
	{ ST_LSM6DSV16X_DEV_NAME },
	{},
};
MODULE_DEVICE_TABLE(spi, st_lsm6dsvx_spi_id_table);

static struct spi_driver st_lsm6dsvx_driver = {
	.driver = {
		.name = "st_lsm6dsvx_spi",
		.pm = &st_lsm6dsvx_pm_ops,
		.of_match_table = of_match_ptr(st_lsm6dsvx_spi_of_match),
	},
	.probe = st_lsm6dsvx_spi_probe,
	.remove = st_lsm6dsvx_spi_remove,
	.id_table = st_lsm6dsvx_spi_id_table,
};
module_spi_driver(st_lsm6dsvx_driver);

MODULE_AUTHOR("MEMS Software Solutions Team");
MODULE_DESCRIPTION("STMicroelectronics st_lsm6dsvx spi driver");
MODULE_LICENSE("GPL v2");
