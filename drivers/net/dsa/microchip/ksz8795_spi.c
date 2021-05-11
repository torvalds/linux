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

#include "ksz8.h"
#include "ksz_common.h"

#define KSZ8795_SPI_ADDR_SHIFT			12
#define KSZ8795_SPI_ADDR_ALIGN			3
#define KSZ8795_SPI_TURNAROUND_SHIFT		1

#define KSZ8863_SPI_ADDR_SHIFT			8
#define KSZ8863_SPI_ADDR_ALIGN			8
#define KSZ8863_SPI_TURNAROUND_SHIFT		0

KSZ_REGMAP_TABLE(ksz8795, 16, KSZ8795_SPI_ADDR_SHIFT,
		 KSZ8795_SPI_TURNAROUND_SHIFT, KSZ8795_SPI_ADDR_ALIGN);

KSZ_REGMAP_TABLE(ksz8863, 16, KSZ8863_SPI_ADDR_SHIFT,
		 KSZ8863_SPI_TURNAROUND_SHIFT, KSZ8863_SPI_ADDR_ALIGN);

static int ksz8795_spi_probe(struct spi_device *spi)
{
	const struct regmap_config *regmap_config;
	struct device *ddev = &spi->dev;
	struct regmap_config rc;
	struct ksz_device *dev;
	struct ksz8 *ksz8;
	int i, ret = 0;

	ksz8 = devm_kzalloc(&spi->dev, sizeof(struct ksz8), GFP_KERNEL);
	ksz8->priv = spi;

	dev = ksz_switch_alloc(&spi->dev, ksz8);
	if (!dev)
		return -ENOMEM;

	regmap_config = device_get_match_data(ddev);
	if (!regmap_config)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(ksz8795_regmap_config); i++) {
		rc = regmap_config[i];
		rc.lock_arg = &dev->regmap_mutex;
		dev->regmap[i] = devm_regmap_init_spi(spi, &rc);
		if (IS_ERR(dev->regmap[i])) {
			ret = PTR_ERR(dev->regmap[i]);
			dev_err(&spi->dev,
				"Failed to initialize regmap%i: %d\n",
				regmap_config[i].val_bits, ret);
			return ret;
		}
	}

	if (spi->dev.platform_data)
		dev->pdata = spi->dev.platform_data;

	/* setup spi */
	spi->mode = SPI_MODE_3;
	ret = spi_setup(spi);
	if (ret)
		return ret;

	ret = ksz8_switch_register(dev);

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
	{ .compatible = "microchip,ksz8765", .data = &ksz8795_regmap_config },
	{ .compatible = "microchip,ksz8794", .data = &ksz8795_regmap_config },
	{ .compatible = "microchip,ksz8795", .data = &ksz8795_regmap_config },
	{ .compatible = "microchip,ksz8863", .data = &ksz8863_regmap_config },
	{ .compatible = "microchip,ksz8873", .data = &ksz8863_regmap_config },
	{},
};
MODULE_DEVICE_TABLE(of, ksz8795_dt_ids);

static struct spi_driver ksz8795_spi_driver = {
	.driver = {
		.name	= "ksz8795-switch",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(ksz8795_dt_ids),
	},
	.probe	= ksz8795_spi_probe,
	.remove	= ksz8795_spi_remove,
	.shutdown = ksz8795_spi_shutdown,
};

module_spi_driver(ksz8795_spi_driver);

MODULE_AUTHOR("Tristram Ha <Tristram.Ha@microchip.com>");
MODULE_DESCRIPTION("Microchip KSZ8795 Series Switch SPI Driver");
MODULE_LICENSE("GPL");
