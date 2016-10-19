/*
 * AD7606 SPI ADC driver
 *
 * Copyright 2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 */

#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/types.h>
#include <linux/err.h>

#include <linux/iio/iio.h>
#include "ad7606.h"

#define MAX_SPI_FREQ_HZ		23500000	/* VDRIVE above 4.75 V */

static int ad7606_spi_read_block(struct device *dev,
				 int count, void *buf)
{
	struct spi_device *spi = to_spi_device(dev);
	int i, ret;
	unsigned short *data = buf;
	__be16 *bdata = buf;

	ret = spi_read(spi, buf, count * 2);
	if (ret < 0) {
		dev_err(&spi->dev, "SPI read error\n");
		return ret;
	}

	for (i = 0; i < count; i++)
		data[i] = be16_to_cpu(bdata[i]);

	return 0;
}

static const struct ad7606_bus_ops ad7606_spi_bops = {
	.read_block	= ad7606_spi_read_block,
};

static int ad7606_spi_probe(struct spi_device *spi)
{
	const struct spi_device_id *id = spi_get_device_id(spi);

	return ad7606_probe(&spi->dev, spi->irq, NULL,
			    id->name, id->driver_data,
			    &ad7606_spi_bops);
}

static int ad7606_spi_remove(struct spi_device *spi)
{
	return ad7606_remove(&spi->dev, spi->irq);
}

static const struct spi_device_id ad7606_id[] = {
	{"ad7606-8", ID_AD7606_8},
	{"ad7606-6", ID_AD7606_6},
	{"ad7606-4", ID_AD7606_4},
	{}
};
MODULE_DEVICE_TABLE(spi, ad7606_id);

static struct spi_driver ad7606_driver = {
	.driver = {
		.name = "ad7606",
		.pm = AD7606_PM_OPS,
	},
	.probe = ad7606_spi_probe,
	.remove = ad7606_spi_remove,
	.id_table = ad7606_id,
};
module_spi_driver(ad7606_driver);

MODULE_AUTHOR("Michael Hennerich <hennerich@blackfin.uclinux.org>");
MODULE_DESCRIPTION("Analog Devices AD7606 ADC");
MODULE_LICENSE("GPL v2");
