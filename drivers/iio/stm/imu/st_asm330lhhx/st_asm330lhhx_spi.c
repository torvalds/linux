// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics st_asm330lhhx spi driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2022 STMicroelectronics Inc.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <linux/of.h>

#include "st_asm330lhhx.h"

static const struct regmap_config st_asm330lhhx_spi_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static int st_asm330lhhx_spi_probe(struct spi_device *spi)
{
	const struct spi_device_id *id = spi_get_device_id(spi);
	int hw_id = id->driver_data;
	struct regmap *regmap;

	regmap = devm_regmap_init_spi(spi,
				      &st_asm330lhhx_spi_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&spi->dev, "Failed to register spi regmap %d\n",
			(int)PTR_ERR(regmap));
		return PTR_ERR(regmap);
	}

	return st_asm330lhhx_probe(&spi->dev, spi->irq, hw_id, regmap);
}


static const struct of_device_id st_asm330lhhx_spi_of_match[] = {
	{
		.compatible = "st,asm330lhhx",
		.data = (void *)ST_ASM330LHHX_ID,
	},
	{
		.compatible = "st,asm330lhh",
		.data = (void *)ST_ASM330LHH_ID,
	},
	{
		.compatible = "st,asm330lhhxg1",
		.data = (void *)ST_ASM330LHHXG1_ID,
	},
	{
		.compatible = "st,asm330lhb",
		.data = (void *)ST_ASM330LHB_ID,
	},
	{},
};
MODULE_DEVICE_TABLE(of, st_asm330lhhx_spi_of_match);

static const struct spi_device_id st_asm330lhhx_spi_id_table[] = {
	{ ST_ASM330LHHX_DEV_NAME, ST_ASM330LHHX_ID },
	{ ST_ASM330LHH_DEV_NAME , ST_ASM330LHH_ID },
	{ ST_ASM330LHHXG1_DEV_NAME, ST_ASM330LHHXG1_ID },
	{ ST_ASM330LHB_DEV_NAME, ST_ASM330LHB_ID },
	{},
};
MODULE_DEVICE_TABLE(spi, st_asm330lhhx_spi_id_table);

static struct spi_driver st_asm330lhhx_driver = {
	.driver = {
		.name = "st_asm330lhhx_spi",
		.pm = &st_asm330lhhx_pm_ops,
		.of_match_table = st_asm330lhhx_spi_of_match,
	},
	.probe = st_asm330lhhx_spi_probe,
	.id_table = st_asm330lhhx_spi_id_table,
};
module_spi_driver(st_asm330lhhx_driver);

MODULE_AUTHOR("MEMS Software Solutions Team");
MODULE_DESCRIPTION("STMicroelectronics st_asm330lhhx spi driver");
MODULE_LICENSE("GPL v2");
