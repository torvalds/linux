// SPDX-License-Identifier: GPL-2.0
/*
 * AD5446 SPI DAC driver
 *
 * Copyright 2025 Analog Devices Inc.
 */
#include <linux/err.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/spi/spi.h>
#include <linux/unaligned.h>

#include <asm/byteorder.h>

#include "ad5446.h"

static int ad5446_write(struct ad5446_state *st, unsigned int val)
{
	struct spi_device *spi = to_spi_device(st->dev);

	st->d16 = cpu_to_be16(val);

	return spi_write(spi, &st->d16, sizeof(st->d16));
}

static int ad5660_write(struct ad5446_state *st, unsigned int val)
{
	struct spi_device *spi = to_spi_device(st->dev);

	put_unaligned_be24(val, st->d24);

	return spi_write(spi, st->d24, sizeof(st->d24));
}

static int ad5446_spi_probe(struct spi_device *spi)
{
	const struct spi_device_id *id = spi_get_device_id(spi);
	const struct ad5446_chip_info *chip_info;

	chip_info = spi_get_device_match_data(spi);
	if (!chip_info)
		return -ENODEV;

	return ad5446_probe(&spi->dev, id->name, chip_info);
}

/*
 * ad5446_supported_spi_device_ids:
 * The AD5620/40/60 parts are available in different fixed internal reference
 * voltage options. The actual part numbers may look differently
 * (and a bit cryptic), however this style is used to make clear which
 * parts are supported here.
 */

static const struct ad5446_chip_info ad5300_chip_info = {
	.channel = AD5446_CHANNEL_POWERDOWN(8, 16, 4),
	.write = ad5446_write,
};

static const struct ad5446_chip_info ad5310_chip_info = {
	.channel = AD5446_CHANNEL_POWERDOWN(10, 16, 2),
	.write = ad5446_write,
};

static const struct ad5446_chip_info ad5320_chip_info = {
	.channel = AD5446_CHANNEL_POWERDOWN(12, 16, 0),
	.write = ad5446_write,
};

static const struct ad5446_chip_info ad5444_chip_info = {
	.channel = AD5446_CHANNEL(12, 16, 2),
	.write = ad5446_write,
};

static const struct ad5446_chip_info ad5446_chip_info = {
	.channel = AD5446_CHANNEL(14, 16, 0),
	.write = ad5446_write,
};

static const struct ad5446_chip_info ad5450_chip_info = {
	.channel = AD5446_CHANNEL(8, 16, 6),
	.write = ad5446_write,
};

static const struct ad5446_chip_info ad5451_chip_info = {
	.channel = AD5446_CHANNEL(10, 16, 4),
	.write = ad5446_write,
};

static const struct ad5446_chip_info ad5541a_chip_info = {
	.channel = AD5446_CHANNEL(16, 16, 0),
	.write = ad5446_write,
};

static const struct ad5446_chip_info ad5512a_chip_info = {
	.channel = AD5446_CHANNEL(12, 16, 4),
	.write = ad5446_write,
};

static const struct ad5446_chip_info ad5553_chip_info = {
	.channel = AD5446_CHANNEL(14, 16, 0),
	.write = ad5446_write,
};

static const struct ad5446_chip_info ad5601_chip_info = {
	.channel = AD5446_CHANNEL_POWERDOWN(8, 16, 6),
	.write = ad5446_write,
};

static const struct ad5446_chip_info ad5611_chip_info = {
	.channel = AD5446_CHANNEL_POWERDOWN(10, 16, 4),
	.write = ad5446_write,
};

static const struct ad5446_chip_info ad5621_chip_info = {
	.channel = AD5446_CHANNEL_POWERDOWN(12, 16, 2),
	.write = ad5446_write,
};

static const struct ad5446_chip_info ad5641_chip_info = {
	.channel = AD5446_CHANNEL_POWERDOWN(14, 16, 0),
	.write = ad5446_write,
};

static const struct ad5446_chip_info ad5620_2500_chip_info = {
	.channel = AD5446_CHANNEL_POWERDOWN(12, 16, 2),
	.int_vref_mv = 2500,
	.write = ad5446_write,
};

static const struct ad5446_chip_info ad5620_1250_chip_info = {
	.channel = AD5446_CHANNEL_POWERDOWN(12, 16, 2),
	.int_vref_mv = 1250,
	.write = ad5446_write,
};

static const struct ad5446_chip_info ad5640_2500_chip_info = {
	.channel = AD5446_CHANNEL_POWERDOWN(14, 16, 0),
	.int_vref_mv = 2500,
	.write = ad5446_write,
};

static const struct ad5446_chip_info ad5640_1250_chip_info = {
	.channel = AD5446_CHANNEL_POWERDOWN(14, 16, 0),
	.int_vref_mv = 1250,
	.write = ad5446_write,
};

static const struct ad5446_chip_info ad5660_2500_chip_info = {
	.channel = AD5446_CHANNEL_POWERDOWN(16, 16, 0),
	.int_vref_mv = 2500,
	.write = ad5660_write,
};

static const struct ad5446_chip_info ad5660_1250_chip_info = {
	.channel = AD5446_CHANNEL_POWERDOWN(16, 16, 0),
	.int_vref_mv = 1250,
	.write = ad5660_write,
};

static const struct ad5446_chip_info ad5662_chip_info = {
	.channel = AD5446_CHANNEL_POWERDOWN(16, 16, 0),
	.write = ad5660_write,
};

static const struct spi_device_id ad5446_spi_ids[] = {
	{"ad5300", (kernel_ulong_t)&ad5300_chip_info},
	{"ad5310", (kernel_ulong_t)&ad5310_chip_info},
	{"ad5320", (kernel_ulong_t)&ad5320_chip_info},
	{"ad5444", (kernel_ulong_t)&ad5444_chip_info},
	{"ad5446", (kernel_ulong_t)&ad5446_chip_info},
	{"ad5450", (kernel_ulong_t)&ad5450_chip_info},
	{"ad5451", (kernel_ulong_t)&ad5451_chip_info},
	{"ad5452", (kernel_ulong_t)&ad5444_chip_info}, /* ad5452 is compatible to the ad5444 */
	{"ad5453", (kernel_ulong_t)&ad5446_chip_info}, /* ad5453 is compatible to the ad5446 */
	{"ad5512a", (kernel_ulong_t)&ad5512a_chip_info},
	{"ad5541a", (kernel_ulong_t)&ad5541a_chip_info},
	{"ad5542", (kernel_ulong_t)&ad5541a_chip_info}, /* ad5541a and ad5542 are compatible */
	{"ad5542a", (kernel_ulong_t)&ad5541a_chip_info}, /* ad5541a and ad5542a are compatible */
	{"ad5543", (kernel_ulong_t)&ad5541a_chip_info}, /* ad5541a and ad5543 are compatible */
	{"ad5553", (kernel_ulong_t)&ad5553_chip_info},
	{"ad5600", (kernel_ulong_t)&ad5541a_chip_info}, /* ad5541a and ad5600 are compatible */
	{"ad5601", (kernel_ulong_t)&ad5601_chip_info},
	{"ad5611", (kernel_ulong_t)&ad5611_chip_info},
	{"ad5621", (kernel_ulong_t)&ad5621_chip_info},
	{"ad5641", (kernel_ulong_t)&ad5641_chip_info},
	{"ad5620-2500", (kernel_ulong_t)&ad5620_2500_chip_info}, /* AD5620/40/60: */
	/* part numbers may look differently */
	{"ad5620-1250", (kernel_ulong_t)&ad5620_1250_chip_info},
	{"ad5640-2500", (kernel_ulong_t)&ad5640_2500_chip_info},
	{"ad5640-1250", (kernel_ulong_t)&ad5640_1250_chip_info},
	{"ad5660-2500", (kernel_ulong_t)&ad5660_2500_chip_info},
	{"ad5660-1250", (kernel_ulong_t)&ad5660_1250_chip_info},
	{"ad5662", (kernel_ulong_t)&ad5662_chip_info},
	{"dac081s101", (kernel_ulong_t)&ad5300_chip_info}, /* compatible Texas Instruments chips */
	{"dac101s101", (kernel_ulong_t)&ad5310_chip_info},
	{"dac121s101", (kernel_ulong_t)&ad5320_chip_info},
	{"dac7512", (kernel_ulong_t)&ad5320_chip_info},
	{ }
};
MODULE_DEVICE_TABLE(spi, ad5446_spi_ids);

static const struct of_device_id ad5446_of_ids[] = {
	{ .compatible = "adi,ad5300", .data = &ad5300_chip_info },
	{ .compatible = "adi,ad5310", .data = &ad5310_chip_info },
	{ .compatible = "adi,ad5320", .data = &ad5320_chip_info },
	{ .compatible = "adi,ad5444", .data = &ad5444_chip_info },
	{ .compatible = "adi,ad5446", .data = &ad5446_chip_info },
	{ .compatible = "adi,ad5450", .data = &ad5450_chip_info },
	{ .compatible = "adi,ad5451", .data = &ad5451_chip_info },
	{ .compatible = "adi,ad5452", .data = &ad5444_chip_info },
	{ .compatible = "adi,ad5453", .data = &ad5446_chip_info },
	{ .compatible = "adi,ad5512a", .data = &ad5512a_chip_info },
	{ .compatible = "adi,ad5541a", .data = &ad5541a_chip_info },
	{ .compatible = "adi,ad5542", .data = &ad5541a_chip_info },
	{ .compatible = "adi,ad5542a", .data = &ad5541a_chip_info },
	{ .compatible = "adi,ad5543", .data = &ad5541a_chip_info },
	{ .compatible = "adi,ad5553", .data = &ad5553_chip_info },
	{ .compatible = "adi,ad5600", .data = &ad5541a_chip_info },
	{ .compatible = "adi,ad5601", .data = &ad5601_chip_info },
	{ .compatible = "adi,ad5611", .data = &ad5611_chip_info },
	{ .compatible = "adi,ad5621", .data = &ad5621_chip_info },
	{ .compatible = "adi,ad5641", .data = &ad5641_chip_info },
	{ .compatible = "adi,ad5620-2500", .data = &ad5620_2500_chip_info },
	{ .compatible = "adi,ad5620-1250", .data = &ad5620_1250_chip_info },
	{ .compatible = "adi,ad5640-2500", .data = &ad5640_2500_chip_info },
	{ .compatible = "adi,ad5640-1250", .data = &ad5640_1250_chip_info },
	{ .compatible = "adi,ad5660-2500", .data = &ad5660_2500_chip_info },
	{ .compatible = "adi,ad5660-1250", .data = &ad5660_1250_chip_info },
	{ .compatible = "adi,ad5662", .data = &ad5662_chip_info },
	{ .compatible = "ti,dac081s101", .data = &ad5300_chip_info },
	{ .compatible = "ti,dac101s101", .data = &ad5310_chip_info },
	{ .compatible = "ti,dac121s101", .data = &ad5320_chip_info },
	{ .compatible = "ti,dac7512", .data = &ad5320_chip_info },
	{ }
};
MODULE_DEVICE_TABLE(of, ad5446_of_ids);

static struct spi_driver ad5446_spi_driver = {
	.driver = {
		.name	= "ad5446",
		.of_match_table = ad5446_of_ids,
	},
	.probe = ad5446_spi_probe,
	.id_table = ad5446_spi_ids,
};
module_spi_driver(ad5446_spi_driver);

MODULE_AUTHOR("Nuno Sá <nuno.sa@analog.com>");
MODULE_DESCRIPTION("Analog Devices AD5446 and similar SPI DACs");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_AD5446");
