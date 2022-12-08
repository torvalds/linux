// SPDX-License-Identifier: GPL-2.0
/*
 * Analog Devices LTC2947 high precision power and energy monitor over SPI
 *
 * Copyright 2019 Analog Devices Inc.
 */
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>

#include "ltc2947.h"

static const struct regmap_config ltc2947_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.read_flag_mask = BIT(0),
};

static int ltc2947_probe(struct spi_device *spi)
{
	struct regmap *map;

	map = devm_regmap_init_spi(spi, &ltc2947_regmap_config);
	if (IS_ERR(map))
		return PTR_ERR(map);

	return ltc2947_core_probe(map, spi_get_device_id(spi)->name);
}

static const struct spi_device_id ltc2947_id[] = {
	{"ltc2947", 0},
	{}
};
MODULE_DEVICE_TABLE(spi, ltc2947_id);

static struct spi_driver ltc2947_driver = {
	.driver = {
		.name = "ltc2947",
		.of_match_table = ltc2947_of_match,
		.pm = pm_sleep_ptr(&ltc2947_pm_ops),
	},
	.probe = ltc2947_probe,
	.id_table = ltc2947_id,
};
module_spi_driver(ltc2947_driver);

MODULE_AUTHOR("Nuno Sa <nuno.sa@analog.com>");
MODULE_DESCRIPTION("LTC2947 SPI power and energy monitor driver");
MODULE_LICENSE("GPL");
