// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025 Pengutronix
 *
 * Author: Steffen Trumtrar <kernel@pengutronix.de>
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>

#include "leds-lp5860.h"

#define LP5860_SPI_WRITE_FLAG BIT(13)

/*
 * The lp5860 uses a rather uncommon SPI data format: The R/W flag is on BIT(5) in the two address
 * bytes; BIT(4) to BIT(0) are don't care. Therefore it has 10 bits for the address and 6 bits for
 * padding the address. The address bytes are sent MSB first. Matching the cores registers to regmap
 * results in write_flag_mask being BIT(13).
 */
static const struct regmap_config lp5860_regmap_config = {
	.name = "lp5860",
	.reg_bits = 10,
	.pad_bits = 6,
	.val_bits = 8,
	.write_flag_mask = LP5860_SPI_WRITE_FLAG,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.max_register = LP5860_MAX_REG,
};

static int lp5860_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct lp5860 *lp5860;
	unsigned int multi_leds;

	multi_leds = device_get_child_node_count(dev);
	if (!multi_leds) {
		dev_err(dev, "LEDs are not defined in Device Tree!");
		return -ENODEV;
	}

	if (multi_leds > LP5860_MAX_LED) {
		dev_err(dev, "Too many LEDs specified.\n");
		return -EINVAL;
	}

	lp5860 = devm_kzalloc(dev, struct_size(lp5860, leds, multi_leds),
			      GFP_KERNEL);
	if (!lp5860)
		return -ENOMEM;

	lp5860->regmap = devm_regmap_init_spi(spi, &lp5860_regmap_config);
	if (IS_ERR(lp5860->regmap))
		return dev_err_probe(&spi->dev, PTR_ERR(lp5860->regmap),
				     "Failed to initialise Regmap.\n");

	lp5860->dev = dev;
	mutex_init(&lp5860->lock);

	spi_set_drvdata(spi, lp5860);

	return lp5860_device_init(dev);
}

static void lp5860_remove(struct spi_device *spi)
{
	struct lp5860 *lp5860 = spi_get_drvdata(spi);

	mutex_destroy(&lp5860->lock);

	lp5860_device_remove(&spi->dev);
}

static const struct of_device_id lp5860_of_match[] = {
	{ .compatible = "ti,lp5860" },
	{}
};
MODULE_DEVICE_TABLE(of, lp5860_of_match);

static struct spi_driver lp5860_driver = {
	.driver = {
		.name = "lp5860-spi",
		.of_match_table = lp5860_of_match,
	},
	.probe	= lp5860_probe,
	.remove = lp5860_remove,
};
module_spi_driver(lp5860_driver);

MODULE_AUTHOR("Steffen Trumtrar <kernel@pengutronix.de>");
MODULE_DESCRIPTION("TI LP5860 RGB LED SPI driver");
MODULE_LICENSE("GPL");
