// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics st_ism330is spi driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2023 STMicroelectronics Inc.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <linux/of.h>

#include "st_ism330is.h"

static const struct regmap_config st_ism330is_spi_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static int st_ism330is_spi_probe(struct spi_device *spi)
{
	struct regmap *regmap;

	regmap = devm_regmap_init_spi(spi, &st_ism330is_spi_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&spi->dev, "Failed to register spi regmap %d\n",
			(int)PTR_ERR(regmap));

		return PTR_ERR(regmap);
	}

	return st_ism330is_probe(&spi->dev, spi->irq, regmap);
}

static const struct of_device_id st_ism330is_spi_of_match[] = {
	{ .compatible = "st,ism330is" },
	{},
};
MODULE_DEVICE_TABLE(of, st_ism330is_spi_of_match);

static const struct spi_device_id st_ism330is_spi_id_table[] = {
	{ ST_ISM330IS_DEV_NAME },
	{},
};
MODULE_DEVICE_TABLE(spi, st_ism330is_spi_id_table);

static struct spi_driver st_ism330is_driver = {
	.driver = {
		.name = "st_" ST_ISM330IS_DEV_NAME "_spi",
		.pm = &st_ism330is_pm_ops,
		.of_match_table = of_match_ptr(st_ism330is_spi_of_match),
	},
	.probe = st_ism330is_spi_probe,
	.id_table = st_ism330is_spi_id_table,
};
module_spi_driver(st_ism330is_driver);

MODULE_AUTHOR("MEMS Software Solutions Team");
MODULE_DESCRIPTION("STMicroelectronics st_ism330is spi driver");
MODULE_LICENSE("GPL v2");
