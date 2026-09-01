// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for the Analog Devices digital potentiometers (SPI bus)
 *
 * Copyright (C) 2010-2011 Michael Hennerich, Analog Devices Inc.
 */

#include <linux/spi/spi.h>
#include <linux/module.h>

#include "ad525x_dpot.h"

/* SPI bus functions */
static int write8(void *client, u8 val)
{
	u8 data = val;

	return spi_write(client, &data, 1);
}

static int write16(void *client, u8 reg, u8 val)
{
	u8 data[2] = {reg, val};

	return spi_write(client, data, 2);
}

static int write24(void *client, u8 reg, u16 val)
{
	u8 data[3] = {reg, val >> 8, val};

	return spi_write(client, data, 3);
}

static int read8(void *client)
{
	int ret;
	u8 data;

	ret = spi_read(client, &data, 1);
	if (ret < 0)
		return ret;

	return data;
}

static int read16(void *client, u8 reg)
{
	int ret;
	u8 buf_rx[2];

	write16(client, reg, 0);
	ret = spi_read(client, buf_rx, 2);
	if (ret < 0)
		return ret;

	return (buf_rx[0] << 8) |  buf_rx[1];
}

static int read24(void *client, u8 reg)
{
	int ret;
	u8 buf_rx[3];

	write24(client, reg, 0);
	ret = spi_read(client, buf_rx, 3);
	if (ret < 0)
		return ret;

	return (buf_rx[1] << 8) |  buf_rx[2];
}

static const struct ad_dpot_bus_ops bops = {
	.read_d8	= read8,
	.read_r8d8	= read16,
	.read_r8d16	= read24,
	.write_d8	= write8,
	.write_r8d8	= write16,
	.write_r8d16	= write24,
};
static int ad_dpot_spi_probe(struct spi_device *spi)
{
	struct ad_dpot_bus_data bdata = {
		.client = spi,
		.bops = &bops,
	};

	return ad_dpot_probe(&spi->dev, &bdata,
			     spi_get_device_id(spi)->driver_data,
			     spi_get_device_id(spi)->name);
}

static void ad_dpot_spi_remove(struct spi_device *spi)
{
	ad_dpot_remove(&spi->dev);
}

static const struct spi_device_id ad_dpot_spi_id[] = {
	{ .name = "ad5160", .driver_data = AD5160_ID },
	{ .name = "ad5161", .driver_data = AD5161_ID },
	{ .name = "ad5162", .driver_data = AD5162_ID },
	{ .name = "ad5165", .driver_data = AD5165_ID },
	{ .name = "ad5200", .driver_data = AD5200_ID },
	{ .name = "ad5201", .driver_data = AD5201_ID },
	{ .name = "ad5203", .driver_data = AD5203_ID },
	{ .name = "ad5204", .driver_data = AD5204_ID },
	{ .name = "ad5206", .driver_data = AD5206_ID },
	{ .name = "ad5207", .driver_data = AD5207_ID },
	{ .name = "ad5231", .driver_data = AD5231_ID },
	{ .name = "ad5232", .driver_data = AD5232_ID },
	{ .name = "ad5233", .driver_data = AD5233_ID },
	{ .name = "ad5235", .driver_data = AD5235_ID },
	{ .name = "ad5260", .driver_data = AD5260_ID },
	{ .name = "ad5262", .driver_data = AD5262_ID },
	{ .name = "ad5263", .driver_data = AD5263_ID },
	{ .name = "ad5290", .driver_data = AD5290_ID },
	{ .name = "ad5291", .driver_data = AD5291_ID },
	{ .name = "ad5292", .driver_data = AD5292_ID },
	{ .name = "ad5293", .driver_data = AD5293_ID },
	{ .name = "ad7376", .driver_data = AD7376_ID },
	{ .name = "ad8400", .driver_data = AD8400_ID },
	{ .name = "ad8402", .driver_data = AD8402_ID },
	{ .name = "ad8403", .driver_data = AD8403_ID },
	{ .name = "adn2850", .driver_data = ADN2850_ID },
	{ .name = "ad5270", .driver_data = AD5270_ID },
	{ .name = "ad5271", .driver_data = AD5271_ID },
	{ }
};
MODULE_DEVICE_TABLE(spi, ad_dpot_spi_id);

static struct spi_driver ad_dpot_spi_driver = {
	.driver = {
		.name	= "ad_dpot",
		.dev_groups = ad_dpot_groups,
	},
	.probe		= ad_dpot_spi_probe,
	.remove		= ad_dpot_spi_remove,
	.id_table	= ad_dpot_spi_id,
};

module_spi_driver(ad_dpot_spi_driver);

MODULE_AUTHOR("Michael Hennerich <michael.hennerich@analog.com>");
MODULE_DESCRIPTION("digital potentiometer SPI bus driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("spi:ad_dpot");
