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
#include <linux/module.h>

#include "internal.h"

struct regmap_async_spi {
	struct regmap_async core;
	struct spi_message m;
	struct spi_transfer t[2];
};

static void regmap_spi_complete(void *data)
{
	struct regmap_async_spi *async = data;

	regmap_async_complete_cb(&async->core, async->m.status);
}

static int regmap_spi_write(void *context, const void *data, size_t count)
{
	struct device *dev = context;
	struct spi_device *spi = to_spi_device(dev);

	return spi_write(spi, data, count);
}

static int regmap_spi_gather_write(void *context,
				   const void *reg, size_t reg_len,
				   const void *val, size_t val_len)
{
	struct device *dev = context;
	struct spi_device *spi = to_spi_device(dev);
	struct spi_message m;
	struct spi_transfer t[2] = { { .tx_buf = reg, .len = reg_len, },
				     { .tx_buf = val, .len = val_len, }, };

	spi_message_init(&m);
	spi_message_add_tail(&t[0], &m);
	spi_message_add_tail(&t[1], &m);

	return spi_sync(spi, &m);
}

static int regmap_spi_async_write(void *context,
				  const void *reg, size_t reg_len,
				  const void *val, size_t val_len,
				  struct regmap_async *a)
{
	struct regmap_async_spi *async = container_of(a,
						      struct regmap_async_spi,
						      core);
	struct device *dev = context;
	struct spi_device *spi = to_spi_device(dev);

	async->t[0].tx_buf = reg;
	async->t[0].len = reg_len;
	async->t[1].tx_buf = val;
	async->t[1].len = val_len;

	spi_message_init(&async->m);
	spi_message_add_tail(&async->t[0], &async->m);
	if (val)
		spi_message_add_tail(&async->t[1], &async->m);

	async->m.complete = regmap_spi_complete;
	async->m.context = async;

	return spi_async(spi, &async->m);
}

static struct regmap_async *regmap_spi_async_alloc(void)
{
	struct regmap_async_spi *async_spi;

	async_spi = kzalloc(sizeof(*async_spi), GFP_KERNEL);
	if (!async_spi)
		return NULL;

	return &async_spi->core;
}

static int regmap_spi_read(void *context,
			   const void *reg, size_t reg_size,
			   void *val, size_t val_size)
{
	struct device *dev = context;
	struct spi_device *spi = to_spi_device(dev);

	return spi_write_then_read(spi, reg, reg_size, val, val_size);
}

static struct regmap_bus regmap_spi = {
	.write = regmap_spi_write,
	.gather_write = regmap_spi_gather_write,
	.async_write = regmap_spi_async_write,
	.async_alloc = regmap_spi_async_alloc,
	.read = regmap_spi_read,
	.read_flag_mask = 0x80,
	.reg_format_endian_default = REGMAP_ENDIAN_BIG,
	.val_format_endian_default = REGMAP_ENDIAN_BIG,
};

struct regmap *__regmap_init_spi(struct spi_device *spi,
				 const struct regmap_config *config,
				 struct lock_class_key *lock_key,
				 const char *lock_name)
{
	return __regmap_init(&spi->dev, &regmap_spi, &spi->dev, config,
			     lock_key, lock_name);
}
EXPORT_SYMBOL_GPL(__regmap_init_spi);

struct regmap *__devm_regmap_init_spi(struct spi_device *spi,
				      const struct regmap_config *config,
				      struct lock_class_key *lock_key,
				      const char *lock_name)
{
	return __devm_regmap_init(&spi->dev, &regmap_spi, &spi->dev, config,
				  lock_key, lock_name);
}
EXPORT_SYMBOL_GPL(__devm_regmap_init_spi);

MODULE_LICENSE("GPL");
