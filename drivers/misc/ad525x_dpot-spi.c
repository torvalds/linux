/*
 * Driver for the Analog Devices digital potentiometers (SPI bus)
 *
 * Copyright (C) 2010 Michael Hennerich, Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/spi/spi.h>
#include <linux/module.h>

#include "ad525x_dpot.h"

static const struct ad_dpot_id ad_dpot_spi_devlist[] = {
	{.name = "ad5160", .devid = AD5160_ID},
	{.name = "ad5161", .devid = AD5161_ID},
	{.name = "ad5162", .devid = AD5162_ID},
	{.name = "ad5165", .devid = AD5165_ID},
	{.name = "ad5200", .devid = AD5200_ID},
	{.name = "ad5201", .devid = AD5201_ID},
	{.name = "ad5203", .devid = AD5203_ID},
	{.name = "ad5204", .devid = AD5204_ID},
	{.name = "ad5206", .devid = AD5206_ID},
	{.name = "ad5207", .devid = AD5207_ID},
	{.name = "ad5231", .devid = AD5231_ID},
	{.name = "ad5232", .devid = AD5232_ID},
	{.name = "ad5233", .devid = AD5233_ID},
	{.name = "ad5235", .devid = AD5235_ID},
	{.name = "ad5260", .devid = AD5260_ID},
	{.name = "ad5262", .devid = AD5262_ID},
	{.name = "ad5263", .devid = AD5263_ID},
	{.name = "ad5290", .devid = AD5290_ID},
	{.name = "ad5291", .devid = AD5291_ID},
	{.name = "ad5292", .devid = AD5292_ID},
	{.name = "ad5293", .devid = AD5293_ID},
	{.name = "ad7376", .devid = AD7376_ID},
	{.name = "ad8400", .devid = AD8400_ID},
	{.name = "ad8402", .devid = AD8402_ID},
	{.name = "ad8403", .devid = AD8403_ID},
	{.name = "adn2850", .devid = ADN2850_ID},
	{}
};

/* ------------------------------------------------------------------------- */

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

static const struct ad_dpot_id *dpot_match_id(const struct ad_dpot_id *id,
						char *name)
{
	while (id->name && id->name[0]) {
		if (strcmp(name, id->name) == 0)
			return id;
		id++;
	}
	return NULL;
}

static int __devinit ad_dpot_spi_probe(struct spi_device *spi)
{
	char *name = spi->dev.platform_data;
	const struct ad_dpot_id *dpot_id;

	struct ad_dpot_bus_data bdata = {
		.client = spi,
		.bops = &bops,
	};

	dpot_id = dpot_match_id(ad_dpot_spi_devlist, name);

	if (dpot_id == NULL) {
		dev_err(&spi->dev, "%s not in supported device list", name);
		return -ENODEV;
	}

	return ad_dpot_probe(&spi->dev, &bdata, dpot_id);
}

static int __devexit ad_dpot_spi_remove(struct spi_device *spi)
{
	return ad_dpot_remove(&spi->dev);
}

static struct spi_driver ad_dpot_spi_driver = {
	.driver = {
		.name	= "ad_dpot",
		.bus	= &spi_bus_type,
		.owner	= THIS_MODULE,
	},
	.probe		= ad_dpot_spi_probe,
	.remove		= __devexit_p(ad_dpot_spi_remove),
};

static int __init ad_dpot_spi_init(void)
{
	return spi_register_driver(&ad_dpot_spi_driver);
}
module_init(ad_dpot_spi_init);

static void __exit ad_dpot_spi_exit(void)
{
	spi_unregister_driver(&ad_dpot_spi_driver);
}
module_exit(ad_dpot_spi_exit);

MODULE_AUTHOR("Michael Hennerich <hennerich@blackfin.uclinux.org>");
MODULE_DESCRIPTION("digital potentiometer SPI bus driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("spi:ad_dpot");
