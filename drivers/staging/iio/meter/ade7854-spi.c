/*
 * ADE7854/58/68/78 Polyphase Multifunction Energy Metering IC Driver (SPI Bus)
 *
 * Copyright 2010 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <linux/module.h>

#include <linux/iio/iio.h>
#include "ade7854.h"

static int ade7854_spi_write_reg(struct device *dev,
				 u16 reg_address,
				 u32 val,
				 int bits)
{
	int ret;
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ade7854_state *st = iio_priv(indio_dev);
	struct spi_transfer xfer = {
		.tx_buf = st->tx,
		.bits_per_word = 8,
		.len = 4,
	};

	mutex_lock(&st->buf_lock);
	st->tx[0] = ADE7854_WRITE_REG;
	st->tx[1] = (reg_address >> 8) & 0xFF;
	st->tx[2] = reg_address & 0xFF;
	switch (bits) {
	case 8:
		st->tx[3] = val & 0xFF;
		break;
	case 16:
		xfer.len = 5;
		st->tx[3] = (val >> 8) & 0xFF;
		st->tx[4] = val & 0xFF;
		break;
	case 24:
		xfer.len = 6;
		st->tx[3] = (val >> 16) & 0xFF;
		st->tx[4] = (val >> 8) & 0xFF;
		st->tx[5] = val & 0xFF;
		break;
	case 32:
		xfer.len = 7;
		st->tx[3] = (val >> 24) & 0xFF;
		st->tx[4] = (val >> 16) & 0xFF;
		st->tx[5] = (val >> 8) & 0xFF;
		st->tx[6] = val & 0xFF;
		break;
	default:
		ret = -EINVAL;
		goto unlock;
	}

	ret = spi_sync_transfer(st->spi, &xfer, 1);
unlock:
	mutex_unlock(&st->buf_lock);

	return ret;
}

static int ade7854_spi_read_reg(struct device *dev,
				u16 reg_address,
				u32 *val,
				int bits)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ade7854_state *st = iio_priv(indio_dev);
	int ret;
	struct spi_transfer xfers[] = {
		{
			.tx_buf = st->tx,
			.bits_per_word = 8,
			.len = 3,
		}, {
			.rx_buf = st->rx,
			.bits_per_word = 8,
			.len = bits,
		}
	};

	mutex_lock(&st->buf_lock);

	st->tx[0] = ADE7854_READ_REG;
	st->tx[1] = (reg_address >> 8) & 0xFF;
	st->tx[2] = reg_address & 0xFF;

	ret = spi_sync_transfer(st->spi, xfers, ARRAY_SIZE(xfers));
	if (ret < 0) {
		dev_err(&st->spi->dev, "problem when reading register 0x%02X",
			reg_address);
		goto unlock;
	}

	switch (bits) {
	case 8:
		*val = st->rx[0];
		break;
	case 16:
		*val = be16_to_cpup((const __be16 *)st->rx);
		break;
	case 24:
		*val = (st->rx[0] << 16) | (st->rx[1] << 8) | st->rx[2];
		break;
	case 32:
		*val = be32_to_cpup((const __be32 *)st->rx);
		break;
	}

unlock:
	mutex_unlock(&st->buf_lock);
	return ret;
}

static int ade7854_spi_probe(struct spi_device *spi)
{
	struct ade7854_state *st;
	struct iio_dev *indio_dev;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;
	st = iio_priv(indio_dev);
	spi_set_drvdata(spi, indio_dev);
	st->read_reg = ade7854_spi_read_reg;
	st->write_reg = ade7854_spi_write_reg;
	st->irq = spi->irq;
	st->spi = spi;

	return ade7854_probe(indio_dev, &spi->dev);
}

static const struct spi_device_id ade7854_id[] = {
	{ "ade7854", 0 },
	{ "ade7858", 0 },
	{ "ade7868", 0 },
	{ "ade7878", 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, ade7854_id);

static struct spi_driver ade7854_driver = {
	.driver = {
		.name = "ade7854",
	},
	.probe = ade7854_spi_probe,
	.id_table = ade7854_id,
};
module_spi_driver(ade7854_driver);

MODULE_AUTHOR("Barry Song <21cnbao@gmail.com>");
MODULE_DESCRIPTION("Analog Devices ADE7854/58/68/78 SPI Driver");
MODULE_LICENSE("GPL v2");
