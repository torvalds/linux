// SPDX-License-Identifier: GPL-2.0-only
/*
 * ltc2497.c - Driver for Analog Devices/Linear Technology LTC2497 ADC
 *
 * Copyright (C) 2017 Analog Devices Inc.
 *
 * Datasheet: http://cds.linear.com/docs/en/datasheet/2497fd.pdf
 */

#include <linux/i2c.h>
#include <linux/iio/iio.h>
#include <linux/iio/driver.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>

#include "ltc2497.h"

struct ltc2497_driverdata {
	/* this must be the first member */
	struct ltc2497core_driverdata common_ddata;
	struct i2c_client *client;
	/*
	 * DMA (thus cache coherency maintenance) requires the
	 * transfer buffers to live in their own cache lines.
	 */
	__be32 buf ____cacheline_aligned;
};

static int ltc2497_result_and_measure(struct ltc2497core_driverdata *ddata,
				      u8 address, int *val)
{
	struct ltc2497_driverdata *st =
		container_of(ddata, struct ltc2497_driverdata, common_ddata);
	int ret;

	if (val) {
		ret = i2c_master_recv(st->client, (char *)&st->buf, 3);
		if (ret < 0) {
			dev_err(&st->client->dev, "i2c_master_recv failed\n");
			return ret;
		}

		*val = (be32_to_cpu(st->buf) >> 14) - (1 << 17);
	}

	ret = i2c_smbus_write_byte(st->client,
				   LTC2497_ENABLE | address);
	if (ret)
		dev_err(&st->client->dev, "i2c transfer failed: %pe\n",
			ERR_PTR(ret));
	return ret;
}

static int ltc2497_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct iio_dev *indio_dev;
	struct ltc2497_driverdata *st;
	struct device *dev = &client->dev;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C |
				     I2C_FUNC_SMBUS_WRITE_BYTE))
		return -EOPNOTSUPP;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	st->client = client;
	st->common_ddata.result_and_measure = ltc2497_result_and_measure;

	return ltc2497core_probe(dev, indio_dev);
}

static int ltc2497_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);

	ltc2497core_remove(indio_dev);

	return 0;
}

static const struct i2c_device_id ltc2497_id[] = {
	{ "ltc2497", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ltc2497_id);

static const struct of_device_id ltc2497_of_match[] = {
	{ .compatible = "lltc,ltc2497", },
	{},
};
MODULE_DEVICE_TABLE(of, ltc2497_of_match);

static struct i2c_driver ltc2497_driver = {
	.driver = {
		.name = "ltc2497",
		.of_match_table = ltc2497_of_match,
	},
	.probe = ltc2497_probe,
	.remove = ltc2497_remove,
	.id_table = ltc2497_id,
};
module_i2c_driver(ltc2497_driver);

MODULE_AUTHOR("Michael Hennerich <michael.hennerich@analog.com>");
MODULE_DESCRIPTION("Linear Technology LTC2497 ADC driver");
MODULE_LICENSE("GPL v2");
