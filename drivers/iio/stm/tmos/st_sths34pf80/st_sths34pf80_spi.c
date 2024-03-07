// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics st_sths34pf80 spi driver
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

#include "st_sths34pf80.h"

static const struct regmap_config st_sths34pf80_spi_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static int st_sths34pf80_spi_probe(struct spi_device *spi)
{
	struct regmap *regmap;

	regmap = devm_regmap_init_spi(spi,
				      &st_sths34pf80_spi_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&spi->dev,
			"Failed to register spi regmap %d\n",
			(int)PTR_ERR(regmap));
		return PTR_ERR(regmap);
	}

	return st_sths34pf80_probe(&spi->dev, spi->irq, regmap);
}

#if KERNEL_VERSION(5, 18, 0) <= LINUX_VERSION_CODE
static void st_sths34pf80_i2c_remove(struct spi_device *spi)
{
	st_sths34pf80_remove(&spi->dev);
}
#else /* LINUX_VERSION_CODE */
static int st_sths34pf80_i2c_remove(struct spi_device *spi)
{
	return st_sths34pf80_remove(&spi->dev);
}
#endif /* LINUX_VERSION_CODE */

static const struct of_device_id st_sths34pf80_spi_of_match[] = {
	{ .compatible = "st," ST_STHS34PF80_DEV_NAME },
	{},
};
MODULE_DEVICE_TABLE(of, st_sths34pf80_spi_of_match);

static const struct spi_device_id st_sths34pf80_spi_id_table[] = {
	{ ST_STHS34PF80_DEV_NAME },
	{},
};
MODULE_DEVICE_TABLE(spi, st_sths34pf80_spi_id_table);

static struct spi_driver st_sths34pf80_driver = {
	.driver = {
		.name = "st_" ST_STHS34PF80_DEV_NAME "_spi",
#ifdef CONFIG_PM_SLEEP
		.pm = &st_sths34pf80_pm_ops,
#endif /* CONFIG_PM_SLEEP */
		.of_match_table =
			       of_match_ptr(st_sths34pf80_spi_of_match),
	},
	.probe = st_sths34pf80_spi_probe,
	.remove = st_sths34pf80_i2c_remove,
	.id_table = st_sths34pf80_spi_id_table,
};
module_spi_driver(st_sths34pf80_driver);

MODULE_AUTHOR("MEMS Software Solutions Team");
MODULE_DESCRIPTION("STMicroelectronics st_sths34pf80 spi driver");
MODULE_LICENSE("GPL v2");
