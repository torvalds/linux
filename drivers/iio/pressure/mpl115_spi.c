// SPDX-License-Identifier: GPL-2.0-only
/*
 * Freescale MPL115A1 pressure/temperature sensor
 *
 * Copyright (c) 2016 Akinobu Mita <akinobu.mita@gmail.com>
 *
 * Datasheet: http://www.nxp.com/files/sensors/doc/data_sheet/MPL115A1.pdf
 */

#include <linux/module.h>
#include <linux/spi/spi.h>

#include "mpl115.h"

#define MPL115_SPI_WRITE(address)	((address) << 1)
#define MPL115_SPI_READ(address)	(0x80 | (address) << 1)

struct mpl115_spi_buf {
	u8 tx[4];
	u8 rx[4];
};

static int mpl115_spi_init(struct device *dev)
{
	struct spi_device *spi = to_spi_device(dev);
	struct mpl115_spi_buf *buf;

	buf = devm_kzalloc(dev, sizeof(*buf), GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	spi_set_drvdata(spi, buf);

	return 0;
}

static int mpl115_spi_read(struct device *dev, u8 address)
{
	struct spi_device *spi = to_spi_device(dev);
	struct mpl115_spi_buf *buf = spi_get_drvdata(spi);
	struct spi_transfer xfer = {
		.tx_buf = buf->tx,
		.rx_buf = buf->rx,
		.len = 4,
	};
	int ret;

	buf->tx[0] = MPL115_SPI_READ(address);
	buf->tx[2] = MPL115_SPI_READ(address + 1);

	ret = spi_sync_transfer(spi, &xfer, 1);
	if (ret)
		return ret;

	return (buf->rx[1] << 8) | buf->rx[3];
}

static int mpl115_spi_write(struct device *dev, u8 address, u8 value)
{
	struct spi_device *spi = to_spi_device(dev);
	struct mpl115_spi_buf *buf = spi_get_drvdata(spi);
	struct spi_transfer xfer = {
		.tx_buf = buf->tx,
		.len = 2,
	};

	buf->tx[0] = MPL115_SPI_WRITE(address);
	buf->tx[1] = value;

	return spi_sync_transfer(spi, &xfer, 1);
}

static const struct mpl115_ops mpl115_spi_ops = {
	.init = mpl115_spi_init,
	.read = mpl115_spi_read,
	.write = mpl115_spi_write,
};

static int mpl115_spi_probe(struct spi_device *spi)
{
	const struct spi_device_id *id = spi_get_device_id(spi);

	return mpl115_probe(&spi->dev, id->name, &mpl115_spi_ops);
}

static const struct spi_device_id mpl115_spi_ids[] = {
	{ "mpl115", 0 },
	{}
};
MODULE_DEVICE_TABLE(spi, mpl115_spi_ids);

static struct spi_driver mpl115_spi_driver = {
	.driver = {
		.name   = "mpl115",
		.pm = pm_ptr(&mpl115_dev_pm_ops),
	},
	.probe = mpl115_spi_probe,
	.id_table = mpl115_spi_ids,
};
module_spi_driver(mpl115_spi_driver);

MODULE_AUTHOR("Akinobu Mita <akinobu.mita@gmail.com>");
MODULE_DESCRIPTION("Freescale MPL115A1 pressure/temperature driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(IIO_MPL115);
