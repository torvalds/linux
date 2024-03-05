// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics ilps22qs spi driver
 *
 * Copyright 2023 STMicroelectronics Inc.
 *
 * MEMS Software Solutions Team
 */

#include <linux/spi/spi.h>
#include <linux/version.h>

#include "st_ilps22qs.h"

static const struct regmap_config st_ilps22qs_spi_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static int st_ilps22qs_spi_probe(struct spi_device *spi)
{
	struct regmap *regmap;

	regmap = devm_regmap_init_spi(spi, &st_ilps22qs_spi_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&spi->dev, "Failed to register spi regmap %d\n",
			(int)PTR_ERR(regmap));

		return PTR_ERR(regmap);
	}

	return st_ilps22qs_probe(&spi->dev, regmap);
}

#if KERNEL_VERSION(5, 18, 0) <= LINUX_VERSION_CODE
static void st_ilps22qs_spi_remove(struct spi_device *spi)
{
	st_ilps22qs_remove(&spi->dev);
}
#else /* LINUX_VERSION_CODE */
static int st_ilps22qs_spi_remove(struct spi_device *spi)
{
	return st_ilps22qs_remove(&spi->dev);
}
#endif /* LINUX_VERSION_CODE */

static const struct spi_device_id st_ilps22qs_ids[] = {
	{ ST_ILPS22QS_DEV_NAME },
	{ ST_ILPS28QWS_DEV_NAME },
	{}
};
MODULE_DEVICE_TABLE(spi, st_ilps22qs_ids);

static const struct of_device_id st_ilps22qs_id_table[] = {
	{ .compatible = "st," ST_ILPS22QS_DEV_NAME },
	{ .compatible = "st," ST_ILPS28QWS_DEV_NAME },
	{},
};
MODULE_DEVICE_TABLE(of, st_ilps22qs_id_table);

static struct spi_driver st_ilps22qs_spi_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "st_" ST_ILPS22QS_DEV_NAME "_spi",
		.pm = &st_ilps22qs_pm_ops,
		.of_match_table = of_match_ptr(st_ilps22qs_id_table),
	},
	.probe = st_ilps22qs_spi_probe,
	.remove = st_ilps22qs_spi_remove,
	.id_table = st_ilps22qs_ids,
};
module_spi_driver(st_ilps22qs_spi_driver);

MODULE_AUTHOR("MEMS Software Solutions Team");
MODULE_DESCRIPTION("STMicroelectronics ilps22qs spi driver");
MODULE_LICENSE("GPL v2");
