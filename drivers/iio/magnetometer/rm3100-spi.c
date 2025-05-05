// SPDX-License-Identifier: GPL-2.0
/*
 * Support for PNI RM3100 3-axis geomagnetic sensor on a spi bus.
 *
 * Copyright (C) 2018 Song Qiang <songqiang1304521@gmail.com>
 */

#include <linux/module.h>
#include <linux/spi/spi.h>

#include "rm3100.h"

static const struct regmap_config rm3100_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.rd_table = &rm3100_readable_table,
	.wr_table = &rm3100_writable_table,
	.volatile_table = &rm3100_volatile_table,

	.read_flag_mask = 0x80,

	.cache_type = REGCACHE_RBTREE,
};

static int rm3100_probe(struct spi_device *spi)
{
	struct regmap *regmap;
	int ret;

	/* Actually this device supports both mode 0 and mode 3. */
	spi->mode = SPI_MODE_0;
	/* Data rates cannot exceed 1Mbits. */
	spi->max_speed_hz = 1000000;
	ret = spi_setup(spi);
	if (ret)
		return ret;

	regmap = devm_regmap_init_spi(spi, &rm3100_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	return rm3100_common_probe(&spi->dev, regmap, spi->irq);
}

static const struct of_device_id rm3100_dt_match[] = {
	{ .compatible = "pni,rm3100", },
	{ }
};
MODULE_DEVICE_TABLE(of, rm3100_dt_match);

static struct spi_driver rm3100_driver = {
	.driver = {
		.name = "rm3100-spi",
		.of_match_table = rm3100_dt_match,
	},
	.probe = rm3100_probe,
};
module_spi_driver(rm3100_driver);

MODULE_AUTHOR("Song Qiang <songqiang1304521@gmail.com>");
MODULE_DESCRIPTION("PNI RM3100 3-axis magnetometer spi driver");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS("IIO_RM3100");
