/*
 * ltc2497.c - Driver for Analog Devices/Linear Technology LTC2497 ADC
 *
 * Copyright (C) 2017 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 *
 * Datasheet: http://cds.linear.com/docs/en/datasheet/2497fd.pdf
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/iio/iio.h>
#include <linux/iio/driver.h>
#include <linux/iio/sysfs.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#define LTC2497_ENABLE			0xA0
#define LTC2497_SGL			BIT(4)
#define LTC2497_DIFF			0
#define LTC2497_SIGN			BIT(3)
#define LTC2497_CONFIG_DEFAULT		LTC2497_ENABLE
#define LTC2497_CONVERSION_TIME_MS	150ULL

struct ltc2497_st {
	struct i2c_client *client;
	struct regulator *ref;
	ktime_t	time_prev;
	u8 addr_prev;
	/*
	 * DMA (thus cache coherency maintenance) requires the
	 * transfer buffers to live in their own cache lines.
	 */
	__be32 buf ____cacheline_aligned;
};

static int ltc2497_wait_conv(struct ltc2497_st *st)
{
	s64 time_elapsed;

	time_elapsed = ktime_ms_delta(ktime_get(), st->time_prev);

	if (time_elapsed < LTC2497_CONVERSION_TIME_MS) {
		/* delay if conversion time not passed
		 * since last read or write
		 */
		if (msleep_interruptible(
		    LTC2497_CONVERSION_TIME_MS - time_elapsed))
			return -ERESTARTSYS;

		return 0;
	}

	if (time_elapsed - LTC2497_CONVERSION_TIME_MS <= 0) {
		/* We're in automatic mode -
		 * so the last reading is stil not outdated
		 */
		return 0;
	}

	return 1;
}

static int ltc2497_read(struct ltc2497_st *st, u8 address, int *val)
{
	struct i2c_client *client = st->client;
	int ret;

	ret = ltc2497_wait_conv(st);
	if (ret < 0)
		return ret;

	if (ret || st->addr_prev != address) {
		ret = i2c_smbus_write_byte(st->client,
					   LTC2497_ENABLE | address);
		if (ret < 0)
			return ret;
		st->addr_prev = address;
		if (msleep_interruptible(LTC2497_CONVERSION_TIME_MS))
			return -ERESTARTSYS;
	}
	ret = i2c_master_recv(client, (char *)&st->buf, 3);
	if (ret < 0)  {
		dev_err(&client->dev, "i2c_master_recv failed\n");
		return ret;
	}
	st->time_prev = ktime_get();

	/* convert and shift the result,
	 * and finally convert from offset binary to signed integer
	 */
	*val = (be32_to_cpu(st->buf) >> 14) - (1 << 17);

	return ret;
}

static int ltc2497_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val, int *val2, long mask)
{
	struct ltc2497_st *st = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&indio_dev->mlock);
		ret = ltc2497_read(st, chan->address, val);
		mutex_unlock(&indio_dev->mlock);
		if (ret < 0)
			return ret;

		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		ret = regulator_get_voltage(st->ref);
		if (ret < 0)
			return ret;

		*val = ret / 1000;
		*val2 = 17;

		return IIO_VAL_FRACTIONAL_LOG2;

	default:
		return -EINVAL;
	}
}

#define LTC2497_CHAN(_chan, _addr, _ds_name) { \
	.type = IIO_VOLTAGE, \
	.indexed = 1, \
	.channel = (_chan), \
	.address = (_addr | (_chan / 2) | ((_chan & 1) ? LTC2497_SIGN : 0)), \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE), \
	.datasheet_name = (_ds_name), \
}

#define LTC2497_CHAN_DIFF(_chan, _addr) { \
	.type = IIO_VOLTAGE, \
	.indexed = 1, \
	.channel = (_chan) * 2 + ((_addr) & LTC2497_SIGN ? 1 : 0), \
	.channel2 = (_chan) * 2 + ((_addr) & LTC2497_SIGN ? 0 : 1),\
	.address = (_addr | _chan), \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE), \
	.differential = 1, \
}

static const struct iio_chan_spec ltc2497_channel[] = {
	LTC2497_CHAN(0, LTC2497_SGL, "CH0"),
	LTC2497_CHAN(1, LTC2497_SGL, "CH1"),
	LTC2497_CHAN(2, LTC2497_SGL, "CH2"),
	LTC2497_CHAN(3, LTC2497_SGL, "CH3"),
	LTC2497_CHAN(4, LTC2497_SGL, "CH4"),
	LTC2497_CHAN(5, LTC2497_SGL, "CH5"),
	LTC2497_CHAN(6, LTC2497_SGL, "CH6"),
	LTC2497_CHAN(7, LTC2497_SGL, "CH7"),
	LTC2497_CHAN(8, LTC2497_SGL, "CH8"),
	LTC2497_CHAN(9, LTC2497_SGL, "CH9"),
	LTC2497_CHAN(10, LTC2497_SGL, "CH10"),
	LTC2497_CHAN(11, LTC2497_SGL, "CH11"),
	LTC2497_CHAN(12, LTC2497_SGL, "CH12"),
	LTC2497_CHAN(13, LTC2497_SGL, "CH13"),
	LTC2497_CHAN(14, LTC2497_SGL, "CH14"),
	LTC2497_CHAN(15, LTC2497_SGL, "CH15"),
	LTC2497_CHAN_DIFF(0, LTC2497_DIFF),
	LTC2497_CHAN_DIFF(1, LTC2497_DIFF),
	LTC2497_CHAN_DIFF(2, LTC2497_DIFF),
	LTC2497_CHAN_DIFF(3, LTC2497_DIFF),
	LTC2497_CHAN_DIFF(4, LTC2497_DIFF),
	LTC2497_CHAN_DIFF(5, LTC2497_DIFF),
	LTC2497_CHAN_DIFF(6, LTC2497_DIFF),
	LTC2497_CHAN_DIFF(7, LTC2497_DIFF),
	LTC2497_CHAN_DIFF(0, LTC2497_DIFF | LTC2497_SIGN),
	LTC2497_CHAN_DIFF(1, LTC2497_DIFF | LTC2497_SIGN),
	LTC2497_CHAN_DIFF(2, LTC2497_DIFF | LTC2497_SIGN),
	LTC2497_CHAN_DIFF(3, LTC2497_DIFF | LTC2497_SIGN),
	LTC2497_CHAN_DIFF(4, LTC2497_DIFF | LTC2497_SIGN),
	LTC2497_CHAN_DIFF(5, LTC2497_DIFF | LTC2497_SIGN),
	LTC2497_CHAN_DIFF(6, LTC2497_DIFF | LTC2497_SIGN),
	LTC2497_CHAN_DIFF(7, LTC2497_DIFF | LTC2497_SIGN),
};

static const struct iio_info ltc2497_info = {
	.read_raw = ltc2497_read_raw,
	.driver_module = THIS_MODULE,
};

static int ltc2497_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct iio_dev *indio_dev;
	struct ltc2497_st *st;
	struct iio_map *plat_data;
	int ret;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C |
				     I2C_FUNC_SMBUS_WRITE_BYTE))
		return -EOPNOTSUPP;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	st->client = client;

	indio_dev->dev.parent = &client->dev;
	indio_dev->name = id->name;
	indio_dev->info = &ltc2497_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = ltc2497_channel;
	indio_dev->num_channels = ARRAY_SIZE(ltc2497_channel);

	st->ref = devm_regulator_get(&client->dev, "vref");
	if (IS_ERR(st->ref))
		return PTR_ERR(st->ref);

	ret = regulator_enable(st->ref);
	if (ret < 0)
		return ret;

	if (client->dev.platform_data) {
		plat_data = ((struct iio_map *)client->dev.platform_data);
		ret = iio_map_array_register(indio_dev, plat_data);
		if (ret) {
			dev_err(&indio_dev->dev, "iio map err: %d\n", ret);
			goto err_regulator_disable;
		}
	}

	ret = i2c_smbus_write_byte(st->client, LTC2497_CONFIG_DEFAULT);
	if (ret < 0)
		goto err_array_unregister;

	st->addr_prev = LTC2497_CONFIG_DEFAULT;
	st->time_prev = ktime_get();

	ret = iio_device_register(indio_dev);
	if (ret < 0)
		goto err_array_unregister;

	return 0;

err_array_unregister:
	iio_map_array_unregister(indio_dev);

err_regulator_disable:
	regulator_disable(st->ref);

	return ret;
}

static int ltc2497_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct ltc2497_st *st = iio_priv(indio_dev);

	iio_map_array_unregister(indio_dev);
	iio_device_unregister(indio_dev);
	regulator_disable(st->ref);

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
		.of_match_table = of_match_ptr(ltc2497_of_match),
	},
	.probe = ltc2497_probe,
	.remove = ltc2497_remove,
	.id_table = ltc2497_id,
};
module_i2c_driver(ltc2497_driver);

MODULE_AUTHOR("Michael Hennerich <michael.hennerich@analog.com>");
MODULE_DESCRIPTION("Linear Technology LTC2497 ADC driver");
MODULE_LICENSE("GPL v2");
