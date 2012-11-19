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

static int ade7854_spi_write_reg_8(struct device *dev,
		u16 reg_address,
		u8 value)
{
	int ret;
	struct spi_message msg;
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
	st->tx[3] = value & 0xFF;

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);
	ret = spi_sync(st->spi, &msg);
	mutex_unlock(&st->buf_lock);

	return ret;
}

static int ade7854_spi_write_reg_16(struct device *dev,
		u16 reg_address,
		u16 value)
{
	int ret;
	struct spi_message msg;
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ade7854_state *st = iio_priv(indio_dev);
	struct spi_transfer xfer = {
		.tx_buf = st->tx,
		.bits_per_word = 8,
		.len = 5,
	};

	mutex_lock(&st->buf_lock);
	st->tx[0] = ADE7854_WRITE_REG;
	st->tx[1] = (reg_address >> 8) & 0xFF;
	st->tx[2] = reg_address & 0xFF;
	st->tx[3] = (value >> 8) & 0xFF;
	st->tx[4] = value & 0xFF;

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);
	ret = spi_sync(st->spi, &msg);
	mutex_unlock(&st->buf_lock);

	return ret;
}

static int ade7854_spi_write_reg_24(struct device *dev,
		u16 reg_address,
		u32 value)
{
	int ret;
	struct spi_message msg;
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ade7854_state *st = iio_priv(indio_dev);
	struct spi_transfer xfer = {
		.tx_buf = st->tx,
		.bits_per_word = 8,
		.len = 6,
	};

	mutex_lock(&st->buf_lock);
	st->tx[0] = ADE7854_WRITE_REG;
	st->tx[1] = (reg_address >> 8) & 0xFF;
	st->tx[2] = reg_address & 0xFF;
	st->tx[3] = (value >> 16) & 0xFF;
	st->tx[4] = (value >> 8) & 0xFF;
	st->tx[5] = value & 0xFF;

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);
	ret = spi_sync(st->spi, &msg);
	mutex_unlock(&st->buf_lock);

	return ret;
}

static int ade7854_spi_write_reg_32(struct device *dev,
		u16 reg_address,
		u32 value)
{
	int ret;
	struct spi_message msg;
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ade7854_state *st = iio_priv(indio_dev);
	struct spi_transfer xfer = {
		.tx_buf = st->tx,
		.bits_per_word = 8,
		.len = 7,
	};

	mutex_lock(&st->buf_lock);
	st->tx[0] = ADE7854_WRITE_REG;
	st->tx[1] = (reg_address >> 8) & 0xFF;
	st->tx[2] = reg_address & 0xFF;
	st->tx[3] = (value >> 24) & 0xFF;
	st->tx[4] = (value >> 16) & 0xFF;
	st->tx[5] = (value >> 8) & 0xFF;
	st->tx[6] = value & 0xFF;

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);
	ret = spi_sync(st->spi, &msg);
	mutex_unlock(&st->buf_lock);

	return ret;
}

static int ade7854_spi_read_reg_8(struct device *dev,
		u16 reg_address,
		u8 *val)
{
	struct spi_message msg;
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
			.len = 1,
		}
	};

	mutex_lock(&st->buf_lock);

	st->tx[0] = ADE7854_READ_REG;
	st->tx[1] = (reg_address >> 8) & 0xFF;
	st->tx[2] = reg_address & 0xFF;

	spi_message_init(&msg);
	spi_message_add_tail(&xfers[0], &msg);
	spi_message_add_tail(&xfers[1], &msg);
	ret = spi_sync(st->spi, &msg);
	if (ret) {
		dev_err(&st->spi->dev, "problem when reading 8 bit register 0x%02X",
				reg_address);
		goto error_ret;
	}
	*val = st->rx[0];

error_ret:
	mutex_unlock(&st->buf_lock);
	return ret;
}

static int ade7854_spi_read_reg_16(struct device *dev,
		u16 reg_address,
		u16 *val)
{
	struct spi_message msg;
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
			.len = 2,
		}
	};

	mutex_lock(&st->buf_lock);
	st->tx[0] = ADE7854_READ_REG;
	st->tx[1] = (reg_address >> 8) & 0xFF;
	st->tx[2] = reg_address & 0xFF;

	spi_message_init(&msg);
	spi_message_add_tail(&xfers[0], &msg);
	spi_message_add_tail(&xfers[1], &msg);
	ret = spi_sync(st->spi, &msg);
	if (ret) {
		dev_err(&st->spi->dev, "problem when reading 16 bit register 0x%02X",
				reg_address);
		goto error_ret;
	}
	*val = be16_to_cpup((const __be16 *)st->rx);

error_ret:
	mutex_unlock(&st->buf_lock);
	return ret;
}

static int ade7854_spi_read_reg_24(struct device *dev,
		u16 reg_address,
		u32 *val)
{
	struct spi_message msg;
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
			.len = 3,
		}
	};

	mutex_lock(&st->buf_lock);

	st->tx[0] = ADE7854_READ_REG;
	st->tx[1] = (reg_address >> 8) & 0xFF;
	st->tx[2] = reg_address & 0xFF;

	spi_message_init(&msg);
	spi_message_add_tail(&xfers[0], &msg);
	spi_message_add_tail(&xfers[1], &msg);
	ret = spi_sync(st->spi, &msg);
	if (ret) {
		dev_err(&st->spi->dev, "problem when reading 24 bit register 0x%02X",
				reg_address);
		goto error_ret;
	}
	*val = (st->rx[0] << 16) | (st->rx[1] << 8) | st->rx[2];

error_ret:
	mutex_unlock(&st->buf_lock);
	return ret;
}

static int ade7854_spi_read_reg_32(struct device *dev,
		u16 reg_address,
		u32 *val)
{
	struct spi_message msg;
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
			.len = 4,
		}
	};

	mutex_lock(&st->buf_lock);

	st->tx[0] = ADE7854_READ_REG;
	st->tx[1] = (reg_address >> 8) & 0xFF;
	st->tx[2] = reg_address & 0xFF;

	spi_message_init(&msg);
	spi_message_add_tail(&xfers[0], &msg);
	spi_message_add_tail(&xfers[1], &msg);
	ret = spi_sync(st->spi, &msg);
	if (ret) {
		dev_err(&st->spi->dev, "problem when reading 32 bit register 0x%02X",
				reg_address);
		goto error_ret;
	}
	*val = be32_to_cpup((const __be32 *)st->rx);

error_ret:
	mutex_unlock(&st->buf_lock);
	return ret;
}

static int ade7854_spi_probe(struct spi_device *spi)
{
	int ret;
	struct ade7854_state *st;
	struct iio_dev *indio_dev;

	indio_dev = iio_device_alloc(sizeof(*st));
	if (indio_dev == NULL)
		return -ENOMEM;
	st = iio_priv(indio_dev);
	spi_set_drvdata(spi, indio_dev);
	st->read_reg_8 = ade7854_spi_read_reg_8;
	st->read_reg_16 = ade7854_spi_read_reg_16;
	st->read_reg_24 = ade7854_spi_read_reg_24;
	st->read_reg_32 = ade7854_spi_read_reg_32;
	st->write_reg_8 = ade7854_spi_write_reg_8;
	st->write_reg_16 = ade7854_spi_write_reg_16;
	st->write_reg_24 = ade7854_spi_write_reg_24;
	st->write_reg_32 = ade7854_spi_write_reg_32;
	st->irq = spi->irq;
	st->spi = spi;


	ret = ade7854_probe(indio_dev, &spi->dev);
	if (ret)
		iio_device_free(indio_dev);

	return 0;
}

static int __devexit ade7854_spi_remove(struct spi_device *spi)
{
	ade7854_remove(spi_get_drvdata(spi));

	return 0;
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
		.owner = THIS_MODULE,
	},
	.probe = ade7854_spi_probe,
	.remove = __devexit_p(ade7854_spi_remove),
	.id_table = ade7854_id,
};
module_spi_driver(ade7854_driver);

MODULE_AUTHOR("Barry Song <21cnbao@gmail.com>");
MODULE_DESCRIPTION("Analog Devices ADE7854/58/68/78 SPI Driver");
MODULE_LICENSE("GPL v2");
