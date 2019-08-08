// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPI driver for hmc5983
 *
 * Copyright (C) Josef Gajdusek <atx@atx.name>
 */

#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/iio/iio.h>

#include "hmc5843.h"

static const struct regmap_range hmc5843_readable_ranges[] = {
		regmap_reg_range(0, HMC5843_ID_END),
};

static const struct regmap_access_table hmc5843_readable_table = {
		.yes_ranges = hmc5843_readable_ranges,
		.n_yes_ranges = ARRAY_SIZE(hmc5843_readable_ranges),
};

static const struct regmap_range hmc5843_writable_ranges[] = {
		regmap_reg_range(0, HMC5843_MODE_REG),
};

static const struct regmap_access_table hmc5843_writable_table = {
		.yes_ranges = hmc5843_writable_ranges,
		.n_yes_ranges = ARRAY_SIZE(hmc5843_writable_ranges),
};

static const struct regmap_range hmc5843_volatile_ranges[] = {
		regmap_reg_range(HMC5843_DATA_OUT_MSB_REGS, HMC5843_STATUS_REG),
};

static const struct regmap_access_table hmc5843_volatile_table = {
		.yes_ranges = hmc5843_volatile_ranges,
		.n_yes_ranges = ARRAY_SIZE(hmc5843_volatile_ranges),
};

static const struct regmap_config hmc5843_spi_regmap_config = {
		.reg_bits = 8,
		.val_bits = 8,

		.rd_table = &hmc5843_readable_table,
		.wr_table = &hmc5843_writable_table,
		.volatile_table = &hmc5843_volatile_table,

		/* Autoincrement address pointer */
		.read_flag_mask = 0xc0,

		.cache_type = REGCACHE_RBTREE,
};

static int hmc5843_spi_probe(struct spi_device *spi)
{
	int ret;
	struct regmap *regmap;
	const struct spi_device_id *id = spi_get_device_id(spi);

	spi->mode = SPI_MODE_3;
	spi->max_speed_hz = 8000000;
	spi->bits_per_word = 8;
	ret = spi_setup(spi);
	if (ret)
		return ret;

	regmap = devm_regmap_init_spi(spi, &hmc5843_spi_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	return hmc5843_common_probe(&spi->dev,
			regmap,
			id->driver_data, id->name);
}

static int hmc5843_spi_remove(struct spi_device *spi)
{
	return hmc5843_common_remove(&spi->dev);
}

static const struct spi_device_id hmc5843_id[] = {
	{ "hmc5983", HMC5983_ID },
	{ }
};
MODULE_DEVICE_TABLE(spi, hmc5843_id);

static struct spi_driver hmc5843_driver = {
		.driver = {
				.name = "hmc5843",
				.pm = HMC5843_PM_OPS,
		},
		.id_table = hmc5843_id,
		.probe = hmc5843_spi_probe,
		.remove = hmc5843_spi_remove,
};

module_spi_driver(hmc5843_driver);

MODULE_AUTHOR("Josef Gajdusek <atx@atx.name>");
MODULE_DESCRIPTION("HMC5983 SPI driver");
MODULE_LICENSE("GPL");
