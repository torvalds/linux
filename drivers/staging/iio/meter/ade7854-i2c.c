// SPDX-License-Identifier: GPL-2.0+
/*
 * ADE7854/58/68/78 Polyphase Multifunction Energy Metering IC Driver (I2C Bus)
 *
 * Copyright 2010 Analog Devices Inc.
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/module.h>

#include <linux/iio/iio.h>
#include "ade7854.h"

static int ade7854_i2c_write_reg(struct device *dev,
				 u16 reg_address,
				 u32 val,
				 int bits)
{
	int ret;
	int count;
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ade7854_state *st = iio_priv(indio_dev);

	mutex_lock(&st->buf_lock);
	st->tx[0] = (reg_address >> 8) & 0xFF;
	st->tx[1] = reg_address & 0xFF;

	switch (bits) {
	case 8:
		st->tx[2] = val & 0xFF;
		count = 3;
		break;
	case 16:
		st->tx[2] = (val >> 8) & 0xFF;
		st->tx[3] = val & 0xFF;
		count = 4;
		break;
	case 24:
		st->tx[2] = (val >> 16) & 0xFF;
		st->tx[3] = (val >> 8) & 0xFF;
		st->tx[4] = val & 0xFF;
		count = 5;
		break;
	case 32:
		st->tx[2] = (val >> 24) & 0xFF;
		st->tx[3] = (val >> 16) & 0xFF;
		st->tx[4] = (val >> 8) & 0xFF;
		st->tx[5] = val & 0xFF;
		count = 6;
		break;
	default:
		ret = -EINVAL;
		goto unlock;
	}

	ret = i2c_master_send(st->i2c, st->tx, count);

unlock:
	mutex_unlock(&st->buf_lock);

	return ret < 0 ? ret : 0;
}

static int ade7854_i2c_read_reg(struct device *dev,
				u16 reg_address,
				u32 *val,
				int bits)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ade7854_state *st = iio_priv(indio_dev);
	int ret;

	mutex_lock(&st->buf_lock);
	st->tx[0] = (reg_address >> 8) & 0xFF;
	st->tx[1] = reg_address & 0xFF;

	ret = i2c_master_send(st->i2c, st->tx, 2);
	if (ret < 0)
		goto unlock;

	ret = i2c_master_recv(st->i2c, st->rx, bits);
	if (ret < 0)
		goto unlock;

	switch (bits) {
	case 8:
		*val = st->rx[0];
		break;
	case 16:
		*val = (st->rx[0] << 8) | st->rx[1];
		break;
	case 24:
		*val = (st->rx[0] << 16) | (st->rx[1] << 8) | st->rx[2];
		break;
	case 32:
		*val = (st->rx[0] << 24) | (st->rx[1] << 16) |
			(st->rx[2] << 8) | st->rx[3];
		break;
	default:
		ret = -EINVAL;
		goto unlock;
	}

unlock:
	mutex_unlock(&st->buf_lock);
	return ret;
}

static int ade7854_i2c_probe(struct i2c_client *client,
			     const struct i2c_device_id *id)
{
	struct ade7854_state *st;
	struct iio_dev *indio_dev;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;
	st = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	st->read_reg = ade7854_i2c_read_reg;
	st->write_reg = ade7854_i2c_write_reg;
	st->i2c = client;
	st->irq = client->irq;

	return ade7854_probe(indio_dev, &client->dev);
}

static const struct i2c_device_id ade7854_id[] = {
	{ "ade7854", 0 },
	{ "ade7858", 0 },
	{ "ade7868", 0 },
	{ "ade7878", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ade7854_id);

static struct i2c_driver ade7854_i2c_driver = {
	.driver = {
		.name = "ade7854",
	},
	.probe    = ade7854_i2c_probe,
	.id_table = ade7854_id,
};
module_i2c_driver(ade7854_i2c_driver);

MODULE_AUTHOR("Barry Song <21cnbao@gmail.com>");
MODULE_DESCRIPTION("Analog Devices ADE7854/58/68/78 Polyphase Multifunction Energy Metering IC I2C Driver");
MODULE_LICENSE("GPL v2");
