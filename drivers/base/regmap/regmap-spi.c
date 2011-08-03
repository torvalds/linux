/*
 * Register map access API - SPI support
 *
 * Copyright 2011 Wolfson Microelectronics plc
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/regmap.h>
#include <linux/spi/spi.h>
#include <linux/init.h>

static int regmap_spi_write(struct device *dev, const void *data, size_t count)
{
	struct spi_device *spi = to_spi_device(dev);

	return spi_write(spi, data, count);
}

static int regmap_spi_gather_write(struct device *dev,
				   const void *reg, size_t reg_len,
				   const void *val, size_t val_len)
{
	struct spi_device *spi = to_spi_device(dev);
	struct spi_message m;
	struct spi_transfer t[2] = { { .tx_buf = reg, .len = reg_len, },
				     { .tx_buf = val, .len = val_len, }, };

	spi_message_init(&m);
	spi_message_add_tail(&t[0], &m);
	spi_message_add_tail(&t[1], &m);

	return spi_sync(spi, &m);
}

static int regmap_spi_read(struct device *dev,
			   const void *reg, size_t reg_size,
			   void *val, size_t val_size)
{
	struct spi_device *spi = to_spi_device(dev);

	return spi_write_then_read(spi, reg, reg_size, val, val_size);
}

static struct regmap_bus regmap_spi = {
	.type = &spi_bus_type,
	.write = regmap_spi_write,
	.gather_write = regmap_spi_gather_write,
	.read = regmap_spi_read,
	.owner = THIS_MODULE,
	.read_flag_mask = 0x80,
};

/**
 * regmap_init_spi(): Initialise register map
 *
 * @spi: Device that will be interacted with
 * @config: Configuration for register map
 *
 * The return value will be an ERR_PTR() on error or a valid pointer to
 * a struct regmap.
 */
struct regmap *regmap_init_spi(struct spi_device *spi,
			       const struct regmap_config *config)
{
	return regmap_init(&spi->dev, &regmap_spi, config);
}
EXPORT_SYMBOL_GPL(regmap_init_spi);
