// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPI interface for the BMP280 driver
 *
 * Inspired by the older BMP085 driver drivers/misc/bmp085-spi.c
 */
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/err.h>
#include <linux/regmap.h>

#include "bmp280.h"

static int bmp280_regmap_spi_write(void *context, const void *data,
                                   size_t count)
{
	struct device *dev = context;
	struct spi_device *spi = to_spi_device(dev);
	u8 buf[2];

	memcpy(buf, data, 2);
	/*
	 * The SPI register address (= full register address without bit 7) and
	 * the write command (bit7 = RW = '0')
	 */
	buf[0] &= ~0x80;

	return spi_write_then_read(spi, buf, 2, NULL, 0);
}

static int bmp280_regmap_spi_read(void *context, const void *reg,
                                  size_t reg_size, void *val, size_t val_size)
{
	struct device *dev = context;
	struct spi_device *spi = to_spi_device(dev);

	return spi_write_then_read(spi, reg, reg_size, val, val_size);
}

static struct regmap_bus bmp280_regmap_bus = {
	.write = bmp280_regmap_spi_write,
	.read = bmp280_regmap_spi_read,
	.reg_format_endian_default = REGMAP_ENDIAN_BIG,
	.val_format_endian_default = REGMAP_ENDIAN_BIG,
};

static int bmp280_spi_probe(struct spi_device *spi)
{
	const struct spi_device_id *id = spi_get_device_id(spi);
	const struct bmp280_chip_info *chip_info;
	struct regmap *regmap;
	int ret;

	spi->bits_per_word = 8;
	ret = spi_setup(spi);
	if (ret < 0) {
		dev_err(&spi->dev, "spi_setup failed!\n");
		return ret;
	}

	chip_info = device_get_match_data(&spi->dev);
	if (!chip_info)
		chip_info = (const struct bmp280_chip_info *) id->driver_data;

	regmap = devm_regmap_init(&spi->dev,
				  &bmp280_regmap_bus,
				  &spi->dev,
				  chip_info->regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&spi->dev, "failed to allocate register map\n");
		return PTR_ERR(regmap);
	}

	return bmp280_common_probe(&spi->dev,
				   regmap,
				   chip_info,
				   id->name,
				   spi->irq);
}

static const struct of_device_id bmp280_of_spi_match[] = {
	{ .compatible = "bosch,bmp085", .data = &bmp180_chip_info },
	{ .compatible = "bosch,bmp180", .data = &bmp180_chip_info },
	{ .compatible = "bosch,bmp181", .data = &bmp180_chip_info },
	{ .compatible = "bosch,bmp280", .data = &bmp280_chip_info },
	{ .compatible = "bosch,bme280", .data = &bmp280_chip_info },
	{ .compatible = "bosch,bmp380", .data = &bmp380_chip_info },
	{ .compatible = "bosch,bmp580", .data = &bmp580_chip_info },
	{ },
};
MODULE_DEVICE_TABLE(of, bmp280_of_spi_match);

static const struct spi_device_id bmp280_spi_id[] = {
	{ "bmp180", (kernel_ulong_t)&bmp180_chip_info },
	{ "bmp181", (kernel_ulong_t)&bmp180_chip_info },
	{ "bmp280", (kernel_ulong_t)&bmp280_chip_info },
	{ "bme280", (kernel_ulong_t)&bmp280_chip_info },
	{ "bmp380", (kernel_ulong_t)&bmp380_chip_info },
	{ "bmp580", (kernel_ulong_t)&bmp580_chip_info },
	{ }
};
MODULE_DEVICE_TABLE(spi, bmp280_spi_id);

static struct spi_driver bmp280_spi_driver = {
	.driver = {
		.name = "bmp280",
		.of_match_table = bmp280_of_spi_match,
		.pm = pm_ptr(&bmp280_dev_pm_ops),
	},
	.id_table = bmp280_spi_id,
	.probe = bmp280_spi_probe,
};
module_spi_driver(bmp280_spi_driver);

MODULE_DESCRIPTION("BMP280 SPI bus driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(IIO_BMP280);
