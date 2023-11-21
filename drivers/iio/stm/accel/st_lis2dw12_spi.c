// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics lis2dw12 spi driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2016 STMicroelectronics Inc.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <linux/of.h>

#include "st_lis2dw12.h"

static const struct regmap_config st_lis2dw12_spi_regmap_config = {
	.name = ST_LIS2DW12_REGMAP,
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = ST_LIS2DW12_ABS_INT_CFG_ADDR,
	.cache_type = REGCACHE_RBTREE,
	.volatile_reg = st_lis2dw12_is_volatile_reg,
};

static int st_lis2dw12_spi_probe(struct spi_device *spi)
{
	struct regmap *regmap;

	regmap = devm_regmap_init_spi(spi, &st_lis2dw12_spi_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&spi->dev,
			"Failed to register spi regmap %d\n",
			(int)PTR_ERR(regmap));
		return PTR_ERR(regmap);
	}

	return st_lis2dw12_probe(&spi->dev, spi->irq, spi->modalias, regmap);
}

static const struct of_device_id st_lis2dw12_spi_of_match[] = {
	{
		.compatible = "st,lis2dw12",
		.data = ST_LIS2DW12_DEV_NAME,
	},
	{
		.compatible = "st,iis2dlpc",
		.data = ST_IIS2DLPC_DEV_NAME,
	},
	{
		.compatible = "st,ais2ih",
		.data = ST_AIS2IH_DEV_NAME,
	},
	{},
};
MODULE_DEVICE_TABLE(of, st_lis2dw12_spi_of_match);

static const struct spi_device_id st_lis2dw12_spi_id_table[] = {
	{ ST_LIS2DW12_DEV_NAME },
	{ ST_IIS2DLPC_DEV_NAME },
	{ ST_AIS2IH_DEV_NAME },
	{},
};
MODULE_DEVICE_TABLE(spi, st_lis2dw12_spi_id_table);

static struct spi_driver st_lis2dw12_driver = {
	.driver = {
		.name = "st_lis2dw12_spi",
		.pm = &st_lis2dw12_pm_ops,
		.of_match_table = of_match_ptr(st_lis2dw12_spi_of_match),
	},
	.probe = st_lis2dw12_spi_probe,
	.id_table = st_lis2dw12_spi_id_table,
};
module_spi_driver(st_lis2dw12_driver);

MODULE_AUTHOR("MEMS Software Solutions Team");
MODULE_DESCRIPTION("STMicroelectronics st_lis2dw12 spi driver");
MODULE_LICENSE("GPL v2");
