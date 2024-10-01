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
#include <linux/property.h>

#include <asm/unaligned.h>

#include "ltc2497.h"

enum ltc2497_chip_type {
	TYPE_LTC2497,
	TYPE_LTC2499,
};

struct ltc2497_driverdata {
	/* this must be the first member */
	struct ltc2497core_driverdata common_ddata;
	struct i2c_client *client;
	u32 recv_size;
	/*
	 * DMA (thus cache coherency maintenance) may require the
	 * transfer buffers to live in their own cache lines.
	 */
	union {
		__be32 d32;
		u8 d8[3];
	} data __aligned(IIO_DMA_MINALIGN);
};

static int ltc2497_result_and_measure(struct ltc2497core_driverdata *ddata,
				      u8 address, int *val)
{
	struct ltc2497_driverdata *st =
		container_of(ddata, struct ltc2497_driverdata, common_ddata);
	int ret;

	if (val) {
		if (st->recv_size == 3)
			ret = i2c_master_recv(st->client, (char *)&st->data.d8,
					      st->recv_size);
		else
			ret = i2c_master_recv(st->client, (char *)&st->data.d32,
					      st->recv_size);
		if (ret < 0) {
			dev_err(&st->client->dev, "i2c_master_recv failed\n");
			return ret;
		}

		/*
		 * The data format is 16/24 bit 2s complement, but with an upper sign bit on the
		 * resolution + 1 position, which is set for positive values only. Given this
		 * bit's value, subtracting BIT(resolution + 1) from the ADC's result is
		 * equivalent to a sign extension.
		 */
		if (st->recv_size == 3) {
			*val = (get_unaligned_be24(st->data.d8) >> 6)
				- BIT(ddata->chip_info->resolution + 1);
		} else {
			*val = (be32_to_cpu(st->data.d32) >> 6)
				- BIT(ddata->chip_info->resolution + 1);
		}

		/*
		 * The part started a new conversion at the end of the above i2c
		 * transfer, so if the address didn't change since the last call
		 * everything is fine and we can return early.
		 * If not (which should only happen when some sort of bulk
		 * conversion is implemented) we have to program the new
		 * address. Note that this probably fails as the conversion that
		 * was triggered above is like not complete yet and the two
		 * operations have to be done in a single transfer.
		 */
		if (ddata->addr_prev == address)
			return 0;
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
	const struct ltc2497_chip_info *chip_info;
	struct iio_dev *indio_dev;
	struct ltc2497_driverdata *st;
	struct device *dev = &client->dev;
	u32 resolution;

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

	chip_info = device_get_match_data(dev);
	if (!chip_info)
		chip_info = (const struct ltc2497_chip_info *)id->driver_data;
	st->common_ddata.chip_info = chip_info;

	resolution = chip_info->resolution;
	st->recv_size = BITS_TO_BYTES(resolution) + 1;

	return ltc2497core_probe(dev, indio_dev);
}

static void ltc2497_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);

	ltc2497core_remove(indio_dev);
}

static const struct ltc2497_chip_info ltc2497_info[] = {
	[TYPE_LTC2497] = {
		.resolution = 16,
		.name = NULL,
	},
	[TYPE_LTC2499] = {
		.resolution = 24,
		.name = "ltc2499",
	},
};

static const struct i2c_device_id ltc2497_id[] = {
	{ "ltc2497", (kernel_ulong_t)&ltc2497_info[TYPE_LTC2497] },
	{ "ltc2499", (kernel_ulong_t)&ltc2497_info[TYPE_LTC2499] },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ltc2497_id);

static const struct of_device_id ltc2497_of_match[] = {
	{ .compatible = "lltc,ltc2497", .data = &ltc2497_info[TYPE_LTC2497] },
	{ .compatible = "lltc,ltc2499", .data = &ltc2497_info[TYPE_LTC2499] },
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
