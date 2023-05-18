// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics st_lsm6dsv16bx spi driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2023 STMicroelectronics Inc.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/of.h>

#include "st_lsm6dsv16bx.h"

static const struct regmap_config st_lsm6dsv16bx_spi_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static int st_lsm6dsv16bx_spi_probe(struct spi_device *spi)
{
	const struct spi_device_id *id = spi_get_device_id(spi);
	int hw_id = id->driver_data;
	struct regmap *regmap;

	regmap = devm_regmap_init_spi(spi, &st_lsm6dsv16bx_spi_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&spi->dev, "Failed to register spi regmap %d\n",
			(int)PTR_ERR(regmap));

		return PTR_ERR(regmap);
	}

	return st_lsm6dsv16bx_probe(&spi->dev, spi->irq, hw_id, regmap);
}

static const struct of_device_id st_lsm6dsv16bx_spi_of_match[] = {
	{
		.compatible = "st," ST_LSM6DSV16BX_DEV_NAME,
		.data = (void *)ST_LSM6DSV16BX_ID,
	},
	{
		.compatible = "st," ST_LSM6DSV16B_DEV_NAME,
		.data = (void *)ST_LSM6DSV16B_ID,
	},
	{},
};
MODULE_DEVICE_TABLE(of, st_lsm6dsv16bx_spi_of_match);

static const struct spi_device_id st_lsm6dsv16bx_spi_id_table[] = {
	{ ST_LSM6DSV16BX_DEV_NAME, ST_LSM6DSV16BX_ID },
	{ ST_LSM6DSV16B_DEV_NAME, ST_LSM6DSV16B_ID },
	{},
};
MODULE_DEVICE_TABLE(spi, st_lsm6dsv16bx_spi_id_table);

static struct spi_driver st_lsm6dsv16bx_driver = {
	.driver = {
		.name = "st_lsm6dsv16bx_spi",
		.pm = &st_lsm6dsv16bx_pm_ops,
		.of_match_table = of_match_ptr(st_lsm6dsv16bx_spi_of_match),
	},
	.probe = st_lsm6dsv16bx_spi_probe,
	.id_table = st_lsm6dsv16bx_spi_id_table,
};
module_spi_driver(st_lsm6dsv16bx_driver);

MODULE_AUTHOR("MEMS Software Solutions Team");
MODULE_DESCRIPTION("STMicroelectronics st_lsm6dsv16bx spi driver");
MODULE_LICENSE("GPL v2");
