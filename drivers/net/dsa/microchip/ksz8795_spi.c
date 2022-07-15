// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Microchip KSZ8795 series register access through SPI
 *
 * Copyright (C) 2017 Microchip Technology Inc.
 *	Tristram Ha <Tristram.Ha@microchip.com>
 */

#include <asm/unaligned.h>

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>

#include "ksz_common.h"

#define SPI_ADDR_SHIFT			12
#define SPI_ADDR_ALIGN			3
#define SPI_TURNAROUND_SHIFT		1

KSZ_REGMAP_TABLE(ksz8795, 16, SPI_ADDR_SHIFT,
		 SPI_TURNAROUND_SHIFT, SPI_ADDR_ALIGN);

static int ksz8795_spi_probe(struct spi_device *spi)
{
	struct regmap_config rc;
	struct ksz_device *dev;
	int i, ret;

	dev = ksz_switch_alloc(&spi->dev, spi);
	if (!dev)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(ksz8795_regmap_config); i++) {
		rc = ksz8795_regmap_config[i];
		rc.lock_arg = &dev->regmap_mutex;
		dev->regmap[i] = devm_regmap_init_spi(spi, &rc);
		if (IS_ERR(dev->regmap[i])) {
			ret = PTR_ERR(dev->regmap[i]);
			dev_err(&spi->dev,
				"Failed to initialize regmap%i: %d\n",
				ksz8795_regmap_config[i].val_bits, ret);
			return ret;
		}
	}

	if (spi->dev.platform_data)
		dev->pdata = spi->dev.platform_data;

	ret = ksz8795_switch_register(dev);

	/* Main DSA driver may not be started yet. */
	if (ret)
		return ret;

	spi_set_drvdata(spi, dev);

	return 0;
}

static int ksz8795_spi_remove(struct spi_device *spi)
{
	struct ksz_device *dev = spi_get_drvdata(spi);

	if (dev)
		ksz_switch_remove(dev);

	return 0;
}

static void ksz8795_spi_shutdown(struct spi_device *spi)
{
	struct ksz_device *dev = spi_get_drvdata(spi);

	if (dev && dev->dev_ops->shutdown)
		dev->dev_ops->shutdown(dev);
}

static const struct of_device_id ksz8795_dt_ids[] = {
	{ .compatible = "microchip,ksz8765" },
	{ .compatible = "microchip,ksz8794" },
	{ .compatible = "microchip,ksz8795" },
	{},
};
MODULE_DEVICE_TABLE(of, ksz8795_dt_ids);

static const struct spi_device_id ksz8795_spi_ids[] = {
	{ "ksz8765" },
	{ "ksz8794" },
	{ "ksz8795" },
	{ "ksz8863" },
	{ "ksz8873" },
	{ },
};
MODULE_DEVICE_TABLE(spi, ksz8795_spi_ids);

static struct spi_driver ksz8795_spi_driver = {
	.driver = {
		.name	= "ksz8795-switch",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(ksz8795_dt_ids),
	},
	.id_table = ksz8795_spi_ids,
	.probe	= ksz8795_spi_probe,
	.remove	= ksz8795_spi_remove,
	.shutdown = ksz8795_spi_shutdown,
};

module_spi_driver(ksz8795_spi_driver);

MODULE_AUTHOR("Tristram Ha <Tristram.Ha@microchip.com>");
MODULE_DESCRIPTION("Microchip KSZ8795 Series Switch SPI Driver");
MODULE_LICENSE("GPL");
