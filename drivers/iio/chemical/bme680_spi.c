// SPDX-License-Identifier: GPL-2.0
/*
 * BME680 - SPI Driver
 *
 * Copyright (C) 2018 Himanshu Jha <himanshujha199640@gmail.com>
 */
#include <linux/acpi.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>

#include "bme680.h"

static int bme680_regmap_spi_write(void *context, const void *data,
				   size_t count)
{
	struct spi_device *spi = context;
	u8 buf[2];

	memcpy(buf, data, 2);
	/*
	 * The SPI register address (= full register address without bit 7)
	 * and the write command (bit7 = RW = '0')
	 */
	buf[0] &= ~0x80;

	return spi_write_then_read(spi, buf, 2, NULL, 0);
}

static int bme680_regmap_spi_read(void *context, const void *reg,
				  size_t reg_size, void *val, size_t val_size)
{
	struct spi_device *spi = context;

	return spi_write_then_read(spi, reg, reg_size, val, val_size);
}

static struct regmap_bus bme680_regmap_bus = {
	.write = bme680_regmap_spi_write,
	.read = bme680_regmap_spi_read,
	.reg_format_endian_default = REGMAP_ENDIAN_BIG,
	.val_format_endian_default = REGMAP_ENDIAN_BIG,
};

static int bme680_spi_probe(struct spi_device *spi)
{
	const struct spi_device_id *id = spi_get_device_id(spi);
	struct regmap *regmap;
	unsigned int val;
	int ret;

	spi->bits_per_word = 8;
	ret = spi_setup(spi);
	if (ret < 0) {
		dev_err(&spi->dev, "spi_setup failed!\n");
		return ret;
	}

	regmap = devm_regmap_init(&spi->dev, &bme680_regmap_bus,
				  &spi->dev, &bme680_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&spi->dev, "Failed to register spi regmap %d\n",
				(int)PTR_ERR(regmap));
		return PTR_ERR(regmap);
	}

	ret = regmap_write(regmap, BME680_REG_SOFT_RESET_SPI,
			   BME680_CMD_SOFTRESET);
	if (ret < 0) {
		dev_err(&spi->dev, "Failed to reset chip\n");
		return ret;
	}

	/* after power-on reset, Page 0(0x80-0xFF) of spi_mem_page is active */
	ret = regmap_read(regmap, BME680_REG_CHIP_SPI_ID, &val);
	if (ret < 0) {
		dev_err(&spi->dev, "Error reading SPI chip ID\n");
		return ret;
	}

	if (val != BME680_CHIP_ID_VAL) {
		dev_err(&spi->dev, "Wrong chip ID, got %x expected %x\n",
				val, BME680_CHIP_ID_VAL);
		return -ENODEV;
	}
	/*
	 * select Page 1 of spi_mem_page to enable access to
	 * to registers from address 0x00 to 0x7F.
	 */
	ret = regmap_write_bits(regmap, BME680_REG_STATUS,
				BME680_SPI_MEM_PAGE_BIT,
				BME680_SPI_MEM_PAGE_1_VAL);
	if (ret < 0) {
		dev_err(&spi->dev, "failed to set page 1 of spi_mem_page\n");
		return ret;
	}

	return bme680_core_probe(&spi->dev, regmap, id->name);
}

static const struct spi_device_id bme680_spi_id[] = {
	{"bme680", 0},
	{},
};
MODULE_DEVICE_TABLE(spi, bme680_spi_id);

static const struct acpi_device_id bme680_acpi_match[] = {
	{"BME0680", 0},
	{},
};
MODULE_DEVICE_TABLE(acpi, bme680_acpi_match);

static const struct of_device_id bme680_of_spi_match[] = {
	{ .compatible = "bosch,bme680", },
	{},
};
MODULE_DEVICE_TABLE(of, bme680_of_spi_match);

static struct spi_driver bme680_spi_driver = {
	.driver = {
		.name			= "bme680_spi",
		.acpi_match_table	= ACPI_PTR(bme680_acpi_match),
		.of_match_table		= bme680_of_spi_match,
	},
	.probe = bme680_spi_probe,
	.id_table = bme680_spi_id,
};
module_spi_driver(bme680_spi_driver);

MODULE_AUTHOR("Himanshu Jha <himanshujha199640@gmail.com>");
MODULE_DESCRIPTION("Bosch BME680 SPI driver");
MODULE_LICENSE("GPL v2");
