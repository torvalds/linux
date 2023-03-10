// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics st_lsm6dsox spi driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2021 STMicroelectronics Inc.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <linux/of.h>

#include "st_lsm6dsox.h"

static const struct regmap_config st_lsm6dsox_spi_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static int st_lsm6dsox_spi_probe(struct spi_device *spi)
{
	const struct spi_device_id *id = spi_get_device_id(spi);
	int hw_id = id->driver_data;
	struct regmap *regmap;

	regmap = devm_regmap_init_spi(spi, &st_lsm6dsox_spi_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&spi->dev, "Failed to register spi regmap %d\n",
			(int)PTR_ERR(regmap));
		return PTR_ERR(regmap);
	}

	return st_lsm6dsox_probe(&spi->dev, spi->irq, hw_id, regmap);
}

#if KERNEL_VERSION(5, 18, 0) <= LINUX_VERSION_CODE
static void st_lsm6dsox_spi_remove(struct spi_device *spi)
{
	struct st_lsm6dsox_hw *hw = dev_get_drvdata(&spi->dev);

	if (hw->settings->st_mlc_probe)
		st_lsm6dsox_mlc_remove(&spi->dev);
}
#else /* LINUX_VERSION_CODE */
static int st_lsm6dsox_spi_remove(struct spi_device *spi)
{
	struct st_lsm6dsox_hw *hw = dev_get_drvdata(&spi->dev);
	int err = 0;

	if (hw->settings->st_mlc_probe)
		err = st_lsm6dsox_mlc_remove(&spi->dev);

	return err;
}
#endif /* LINUX_VERSION_CODE */

static const struct of_device_id st_lsm6dsox_spi_of_match[] = {
	{
		.compatible = "st,lsm6dso",
		.data = (void *)ST_LSM6DSO_ID,
	},
	{
		.compatible = "st,lsm6dsox",
		.data = (void *)ST_LSM6DSOX_ID,
	},
	{
		.compatible = "st,lsm6dso32",
		.data = (void *)ST_LSM6DSO32_ID,
	},
	{
		.compatible = "st,lsm6dso32x",
		.data = (void *)ST_LSM6DSO32X_ID,
	},
	{},
};
MODULE_DEVICE_TABLE(of, st_lsm6dsox_spi_of_match);

static const struct spi_device_id st_lsm6dsox_spi_id_table[] = {
	{ ST_LSM6DSO_DEV_NAME, ST_LSM6DSO_ID },
	{ ST_LSM6DSOX_DEV_NAME, ST_LSM6DSOX_ID },
	{ ST_LSM6DSO32_DEV_NAME, ST_LSM6DSO32_ID },
	{ ST_LSM6DSO32X_DEV_NAME, ST_LSM6DSO32X_ID },
	{},
};
MODULE_DEVICE_TABLE(spi, st_lsm6dsox_spi_id_table);

static struct spi_driver st_lsm6dsox_driver = {
	.driver = {
		.name = "st_" ST_LSM6DSOX_DEV_NAME "_spi",
		.pm = &st_lsm6dsox_pm_ops,
		.of_match_table = of_match_ptr(st_lsm6dsox_spi_of_match),
	},
	.probe = st_lsm6dsox_spi_probe,
	.remove = st_lsm6dsox_spi_remove,
	.id_table = st_lsm6dsox_spi_id_table,
};
module_spi_driver(st_lsm6dsox_driver);

MODULE_AUTHOR("MEMS Software Solutions Team");
MODULE_DESCRIPTION("STMicroelectronics st_lsm6dsox spi driver");
MODULE_LICENSE("GPL v2");
