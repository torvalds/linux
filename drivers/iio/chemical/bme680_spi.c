// SPDX-License-Identifier: GPL-2.0
/*
 * BME680 - SPI Driver
 *
 * Copyright (C) 2018 Himanshu Jha <himanshujha199640@gmail.com>
 */
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>

#include "bme680.h"

struct bme680_spi_bus_context {
	struct spi_device *spi;
	u8 current_page;
};

/*
 * In SPI mode there are only 7 address bits, a "page" register determines
 * which part of the 8-bit range is active. This function looks at the address
 * and writes the page selection bit if needed
 */
static int bme680_regmap_spi_select_page(
	struct bme680_spi_bus_context *ctx, u8 reg)
{
	struct spi_device *spi = ctx->spi;
	int ret;
	u8 buf[2];
	u8 page = (reg & 0x80) ? 0 : 1; /* Page "1" is low range */

	if (page == ctx->current_page)
		return 0;

	/*
	 * Data sheet claims we're only allowed to change bit 4, so we must do
	 * a read-modify-write on each and every page select
	 */
	buf[0] = BME680_REG_STATUS;
	ret = spi_write_then_read(spi, buf, 1, buf + 1, 1);
	if (ret < 0) {
		dev_err(&spi->dev, "failed to set page %u\n", page);
		return ret;
	}

	buf[0] = BME680_REG_STATUS;
	if (page)
		buf[1] |= BME680_SPI_MEM_PAGE_BIT;
	else
		buf[1] &= ~BME680_SPI_MEM_PAGE_BIT;

	ret = spi_write(spi, buf, 2);
	if (ret < 0) {
		dev_err(&spi->dev, "failed to set page %u\n", page);
		return ret;
	}

	ctx->current_page = page;

	return 0;
}

static int bme680_regmap_spi_write(void *context, const void *data,
				   size_t count)
{
	struct bme680_spi_bus_context *ctx = context;
	struct spi_device *spi = ctx->spi;
	int ret;
	u8 buf[2];

	memcpy(buf, data, 2);

	ret = bme680_regmap_spi_select_page(ctx, buf[0]);
	if (ret)
		return ret;

	/*
	 * The SPI register address (= full register address without bit 7)
	 * and the write command (bit7 = RW = '0')
	 */
	buf[0] &= ~0x80;

	return spi_write(spi, buf, 2);
}

static int bme680_regmap_spi_read(void *context, const void *reg,
				  size_t reg_size, void *val, size_t val_size)
{
	struct bme680_spi_bus_context *ctx = context;
	struct spi_device *spi = ctx->spi;
	int ret;
	u8 addr = *(const u8 *)reg;

	ret = bme680_regmap_spi_select_page(ctx, addr);
	if (ret)
		return ret;

	addr |= 0x80; /* bit7 = RW = '1' */

	return spi_write_then_read(spi, &addr, 1, val, val_size);
}

static const struct regmap_bus bme680_regmap_bus = {
	.write = bme680_regmap_spi_write,
	.read = bme680_regmap_spi_read,
	.reg_format_endian_default = REGMAP_ENDIAN_BIG,
	.val_format_endian_default = REGMAP_ENDIAN_BIG,
};

static int bme680_spi_probe(struct spi_device *spi)
{
	const struct spi_device_id *id = spi_get_device_id(spi);
	struct bme680_spi_bus_context *bus_context;
	struct regmap *regmap;
	int ret;

	spi->bits_per_word = 8;
	ret = spi_setup(spi);
	if (ret < 0) {
		dev_err(&spi->dev, "spi_setup failed!\n");
		return ret;
	}

	bus_context = devm_kzalloc(&spi->dev, sizeof(*bus_context), GFP_KERNEL);
	if (!bus_context)
		return -ENOMEM;

	bus_context->spi = spi;
	bus_context->current_page = 0xff; /* Undefined on warm boot */

	regmap = devm_regmap_init(&spi->dev, &bme680_regmap_bus,
				  bus_context, &bme680_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&spi->dev, "Failed to register spi regmap %ld\n", PTR_ERR(regmap));
		return PTR_ERR(regmap);
	}

	return bme680_core_probe(&spi->dev, regmap, id->name);
}

static const struct spi_device_id bme680_spi_id[] = {
	{"bme680", 0},
	{},
};
MODULE_DEVICE_TABLE(spi, bme680_spi_id);

static const struct of_device_id bme680_of_spi_match[] = {
	{ .compatible = "bosch,bme680", },
	{},
};
MODULE_DEVICE_TABLE(of, bme680_of_spi_match);

static struct spi_driver bme680_spi_driver = {
	.driver = {
		.name			= "bme680_spi",
		.of_match_table		= bme680_of_spi_match,
	},
	.probe = bme680_spi_probe,
	.id_table = bme680_spi_id,
};
module_spi_driver(bme680_spi_driver);

MODULE_AUTHOR("Himanshu Jha <himanshujha199640@gmail.com>");
MODULE_DESCRIPTION("Bosch BME680 SPI driver");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(IIO_BME680);
