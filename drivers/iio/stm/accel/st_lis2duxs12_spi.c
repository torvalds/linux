// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics st_lis2duxs12 spi driver
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
#include <linux/version.h>

#include "st_lis2duxs12.h"

static const struct regmap_config st_lis2duxs12_spi_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static int st_lis2duxs12_spi_probe(struct spi_device *spi)
{
	const struct spi_device_id *id = spi_get_device_id(spi);
	int hw_id = id->driver_data;
	struct regmap *regmap;

	regmap = devm_regmap_init_spi(spi,
				      &st_lis2duxs12_spi_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&spi->dev,
			"Failed to register spi regmap %d\n",
			(int)PTR_ERR(regmap));
		return PTR_ERR(regmap);
	}

	return st_lis2duxs12_probe(&spi->dev, spi->irq, hw_id, regmap);
}

#if KERNEL_VERSION(5, 18, 0) <= LINUX_VERSION_CODE
static void st_lis2duxs12_spi_remove(struct spi_device *spi)
{
	st_lis2duxs12_remove(&spi->dev);
}
#else /* LINUX_VERSION_CODE */
static int st_lis2duxs12_spi_remove(struct spi_device *spi)
{
	return st_lis2duxs12_remove(&spi->dev);
}
#endif /* LINUX_VERSION_CODE */

static const struct of_device_id st_lis2duxs12_spi_of_match[] = {
	{
		.compatible = "st," ST_LIS2DUX12_DEV_NAME,
		.data = (void *)ST_LIS2DUX12_ID,
	},
	{
		.compatible = "st," ST_LIS2DUXS12_DEV_NAME,
		.data = (void *)ST_LIS2DUXS12_ID,
	},
	{},
};
MODULE_DEVICE_TABLE(of, st_lis2duxs12_spi_of_match);

static const struct spi_device_id st_lis2duxs12_spi_id_table[] = {
	{ ST_LIS2DUX12_DEV_NAME, ST_LIS2DUX12_ID },
	{ ST_LIS2DUXS12_DEV_NAME, ST_LIS2DUXS12_ID },
	{},
};
MODULE_DEVICE_TABLE(spi, st_lis2duxs12_spi_id_table);

static struct spi_driver st_lis2duxs12_driver = {
	.driver = {
		.name = "st_" ST_LIS2DUXS12_DEV_NAME "_spi",
		.pm = &st_lis2duxs12_pm_ops,
		.of_match_table = of_match_ptr(st_lis2duxs12_spi_of_match),
	},
	.probe = st_lis2duxs12_spi_probe,
	.remove = st_lis2duxs12_spi_remove,
	.id_table = st_lis2duxs12_spi_id_table,
};
module_spi_driver(st_lis2duxs12_driver);

MODULE_AUTHOR("MEMS Software Solutions Team");
MODULE_DESCRIPTION("STMicroelectronics st_lis2duxs12 spi driver");
MODULE_LICENSE("GPL v2");
