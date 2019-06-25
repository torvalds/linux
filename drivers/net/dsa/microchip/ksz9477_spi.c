// SPDX-License-Identifier: GPL-2.0
/*
 * Microchip KSZ9477 series register access through SPI
 *
 * Copyright (C) 2017-2019 Microchip Technology Inc.
 */

#include <asm/unaligned.h>

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>

#include "ksz_priv.h"

#define SPI_ADDR_SHIFT			24
#define SPI_ADDR_ALIGN			3
#define SPI_TURNAROUND_SHIFT		5

/* SPI frame opcodes */
#define KS_SPIOP_RD			3
#define KS_SPIOP_WR			2

#define KS_SPIOP_FLAG_MASK(opcode)		\
	swab32((opcode) << (SPI_ADDR_SHIFT + SPI_TURNAROUND_SHIFT))

#define KSZ_REGMAP_COMMON(width)					\
	{								\
		.val_bits = (width),					\
		.reg_stride = (width) / 8,				\
		.reg_bits = SPI_ADDR_SHIFT + SPI_ADDR_ALIGN,		\
		.pad_bits = SPI_TURNAROUND_SHIFT,			\
		.max_register = BIT(SPI_ADDR_SHIFT) - 1,		\
		.cache_type = REGCACHE_NONE,				\
		.read_flag_mask = KS_SPIOP_FLAG_MASK(KS_SPIOP_RD),	\
		.write_flag_mask = KS_SPIOP_FLAG_MASK(KS_SPIOP_WR),	\
		.reg_format_endian = REGMAP_ENDIAN_BIG,			\
		.val_format_endian = REGMAP_ENDIAN_BIG			\
	}

static const struct regmap_config ksz9477_regmap_config[] = {
	KSZ_REGMAP_COMMON(8),
	KSZ_REGMAP_COMMON(16),
	KSZ_REGMAP_COMMON(32),
};

static int ksz9477_spi_probe(struct spi_device *spi)
{
	struct ksz_device *dev;
	int i, ret;

	dev = ksz_switch_alloc(&spi->dev, spi);
	if (!dev)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(ksz9477_regmap_config); i++) {
		dev->regmap[i] = devm_regmap_init_spi(spi,
					&ksz9477_regmap_config[i]);
		if (IS_ERR(dev->regmap[i])) {
			ret = PTR_ERR(dev->regmap[i]);
			dev_err(&spi->dev,
				"Failed to initialize regmap%i: %d\n",
				ksz9477_regmap_config[i].val_bits, ret);
			return ret;
		}
	}

	if (spi->dev.platform_data)
		dev->pdata = spi->dev.platform_data;

	ret = ksz9477_switch_register(dev);

	/* Main DSA driver may not be started yet. */
	if (ret)
		return ret;

	spi_set_drvdata(spi, dev);

	return 0;
}

static int ksz9477_spi_remove(struct spi_device *spi)
{
	struct ksz_device *dev = spi_get_drvdata(spi);

	if (dev)
		ksz_switch_remove(dev);

	return 0;
}

static void ksz9477_spi_shutdown(struct spi_device *spi)
{
	struct ksz_device *dev = spi_get_drvdata(spi);

	if (dev && dev->dev_ops->shutdown)
		dev->dev_ops->shutdown(dev);
}

static const struct of_device_id ksz9477_dt_ids[] = {
	{ .compatible = "microchip,ksz9477" },
	{ .compatible = "microchip,ksz9897" },
	{ .compatible = "microchip,ksz9893" },
	{ .compatible = "microchip,ksz9563" },
	{},
};
MODULE_DEVICE_TABLE(of, ksz9477_dt_ids);

static struct spi_driver ksz9477_spi_driver = {
	.driver = {
		.name	= "ksz9477-switch",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(ksz9477_dt_ids),
	},
	.probe	= ksz9477_spi_probe,
	.remove	= ksz9477_spi_remove,
	.shutdown = ksz9477_spi_shutdown,
};

module_spi_driver(ksz9477_spi_driver);

MODULE_AUTHOR("Woojung Huh <Woojung.Huh@microchip.com>");
MODULE_DESCRIPTION("Microchip KSZ9477 Series Switch SPI access Driver");
MODULE_LICENSE("GPL");
