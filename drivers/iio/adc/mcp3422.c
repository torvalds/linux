/*
 * mcp3422.c - driver for the Microchip mcp3422/3/4 chip family
 *
 * Copyright (C) 2013, Angelo Compagnucci
 * Author: Angelo Compagnucci <angelo.compagnucci@gmail.com>
 *
 * Datasheet: http://ww1.microchip.com/downloads/en/devicedoc/22088b.pdf
 *
 * This driver exports the value of analog input voltage to sysfs, the
 * voltage unit is nV.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/sysfs.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

/* Masks */
#define MCP3422_CHANNEL_MASK	0x60
#define MCP3422_PGA_MASK	0x03
#define MCP3422_SRATE_MASK	0x0C
#define MCP3422_SRATE_240	0x0
#define MCP3422_SRATE_60	0x1
#define MCP3422_SRATE_15	0x2
#define MCP3422_SRATE_3	0x3
#define MCP3422_PGA_1	0
#define MCP3422_PGA_2	1
#define MCP3422_PGA_4	2
#define MCP3422_PGA_8	3
#define MCP3422_CONT_SAMPLING	0x10

#define MCP3422_CHANNEL(config)	(((config) & MCP3422_CHANNEL_MASK) >> 5)
#define MCP3422_PGA(config)	((config) & MCP3422_PGA_MASK)
#define MCP3422_SAMPLE_RATE(config)	(((config) & MCP3422_SRATE_MASK) >> 2)

#define MCP3422_CHANNEL_VALUE(value) (((value) << 5) & MCP3422_CHANNEL_MASK)
#define MCP3422_PGA_VALUE(value) ((value) & MCP3422_PGA_MASK)
#define MCP3422_SAMPLE_RATE_VALUE(value) ((value << 2) & MCP3422_SRATE_MASK)

#define MCP3422_CHAN(_index) \
	{ \
		.type = IIO_VOLTAGE, \
		.indexed = 1, \
		.channel = _index, \
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) \
				| BIT(IIO_CHAN_INFO_SCALE), \
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SAMP_FREQ), \
	}

/* LSB is in nV to eliminate floating point */
static const u32 rates_to_lsb[] = {1000000, 250000, 62500, 15625};

/*
 *  scales calculated as:
 *  rates_to_lsb[sample_rate] / (1 << pga);
 *  pga is 1 for 0, 2
 */

static const int mcp3422_scales[4][4] = {
	{ 1000000, 250000, 62500, 15625 },
	{ 500000 , 125000, 31250, 7812 },
	{ 250000 , 62500 , 15625, 3906 },
	{ 125000 , 31250 , 7812 , 1953 } };

/* Constant msleep times for data acquisitions */
static const int mcp3422_read_times[4] = {
	[MCP3422_SRATE_240] = 1000 / 240,
	[MCP3422_SRATE_60] = 1000 / 60,
	[MCP3422_SRATE_15] = 1000 / 15,
	[MCP3422_SRATE_3] = 1000 / 3 };

/* sample rates to integer conversion table */
static const int mcp3422_sample_rates[4] = {
	[MCP3422_SRATE_240] = 240,
	[MCP3422_SRATE_60] = 60,
	[MCP3422_SRATE_15] = 15,
	[MCP3422_SRATE_3] = 3 };

/* sample rates to sign extension table */
static const int mcp3422_sign_extend[4] = {
	[MCP3422_SRATE_240] = 12,
	[MCP3422_SRATE_60] = 14,
	[MCP3422_SRATE_15] = 16,
	[MCP3422_SRATE_3] = 18 };

/* Client data (each client gets its own) */
struct mcp3422 {
	struct i2c_client *i2c;
	u8 config;
	u8 pga[4];
	struct mutex lock;
};

static int mcp3422_update_config(struct mcp3422 *adc, u8 newconfig)
{
	int ret;

	mutex_lock(&adc->lock);

	ret = i2c_master_send(adc->i2c, &newconfig, 1);
	if (ret > 0) {
		adc->config = newconfig;
		ret = 0;
	}

	mutex_unlock(&adc->lock);

	return ret;
}

static int mcp3422_read(struct mcp3422 *adc, int *value, u8 *config)
{
	int ret = 0;
	u8 sample_rate = MCP3422_SAMPLE_RATE(adc->config);
	u8 buf[4] = {0, 0, 0, 0};
	u32 temp;

	if (sample_rate == MCP3422_SRATE_3) {
		ret = i2c_master_recv(adc->i2c, buf, 4);
		temp = buf[0] << 16 | buf[1] << 8 | buf[2];
		*config = buf[3];
	} else {
		ret = i2c_master_recv(adc->i2c, buf, 3);
		temp = buf[0] << 8 | buf[1];
		*config = buf[2];
	}

	*value = sign_extend32(temp, mcp3422_sign_extend[sample_rate]);

	return ret;
}

static int mcp3422_read_channel(struct mcp3422 *adc,
				struct iio_chan_spec const *channel, int *value)
{
	int ret;
	u8 config;
	u8 req_channel = channel->channel;

	if (req_channel != MCP3422_CHANNEL(adc->config)) {
		config = adc->config;
		config &= ~MCP3422_CHANNEL_MASK;
		config |= MCP3422_CHANNEL_VALUE(req_channel);
		config &= ~MCP3422_PGA_MASK;
		config |= MCP3422_PGA_VALUE(adc->pga[req_channel]);
		ret = mcp3422_update_config(adc, config);
		if (ret < 0)
			return ret;
		msleep(mcp3422_read_times[MCP3422_SAMPLE_RATE(adc->config)]);
	}

	return mcp3422_read(adc, value, &config);
}

static int mcp3422_read_raw(struct iio_dev *iio,
			struct iio_chan_spec const *channel, int *val1,
			int *val2, long mask)
{
	struct mcp3422 *adc = iio_priv(iio);
	int err;

	u8 sample_rate = MCP3422_SAMPLE_RATE(adc->config);
	u8 pga		 = MCP3422_PGA(adc->config);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		err = mcp3422_read_channel(adc, channel, val1);
		if (err < 0)
			return -EINVAL;
		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:

		*val1 = 0;
		*val2 = mcp3422_scales[sample_rate][pga];
		return IIO_VAL_INT_PLUS_NANO;

	case IIO_CHAN_INFO_SAMP_FREQ:
		*val1 = mcp3422_sample_rates[MCP3422_SAMPLE_RATE(adc->config)];
		return IIO_VAL_INT;

	default:
		break;
	}

	return -EINVAL;
}

static int mcp3422_write_raw(struct iio_dev *iio,
			struct iio_chan_spec const *channel, int val1,
			int val2, long mask)
{
	struct mcp3422 *adc = iio_priv(iio);
	u8 temp;
	u8 config = adc->config;
	u8 req_channel = channel->channel;
	u8 sample_rate = MCP3422_SAMPLE_RATE(config);
	u8 i;

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		if (val1 != 0)
			return -EINVAL;

		for (i = 0; i < ARRAY_SIZE(mcp3422_scales[0]); i++) {
			if (val2 == mcp3422_scales[sample_rate][i]) {
				adc->pga[req_channel] = i;

				config &= ~MCP3422_CHANNEL_MASK;
				config |= MCP3422_CHANNEL_VALUE(req_channel);
				config &= ~MCP3422_PGA_MASK;
				config |= MCP3422_PGA_VALUE(adc->pga[req_channel]);

				return mcp3422_update_config(adc, config);
			}
		}
		return -EINVAL;

	case IIO_CHAN_INFO_SAMP_FREQ:
		switch (val1) {
		case 240:
			temp = MCP3422_SRATE_240;
			break;
		case 60:
			temp = MCP3422_SRATE_60;
			break;
		case 15:
			temp = MCP3422_SRATE_15;
			break;
		case 3:
			temp = MCP3422_SRATE_3;
			break;
		default:
			return -EINVAL;
		}

		config &= ~MCP3422_CHANNEL_MASK;
		config |= MCP3422_CHANNEL_VALUE(req_channel);
		config &= ~MCP3422_SRATE_MASK;
		config |= MCP3422_SAMPLE_RATE_VALUE(temp);

		return mcp3422_update_config(adc, config);

	default:
		break;
	}

	return -EINVAL;
}

static int mcp3422_write_raw_get_fmt(struct iio_dev *indio_dev,
		struct iio_chan_spec const *chan, long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		return IIO_VAL_INT_PLUS_NANO;
	case IIO_CHAN_INFO_SAMP_FREQ:
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

static ssize_t mcp3422_show_scales(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mcp3422 *adc = iio_priv(dev_to_iio_dev(dev));
	u8 sample_rate = MCP3422_SAMPLE_RATE(adc->config);

	return sprintf(buf, "0.%09u 0.%09u 0.%09u 0.%09u\n",
		mcp3422_scales[sample_rate][0],
		mcp3422_scales[sample_rate][1],
		mcp3422_scales[sample_rate][2],
		mcp3422_scales[sample_rate][3]);
}

static IIO_CONST_ATTR_SAMP_FREQ_AVAIL("240 60 15 3");
static IIO_DEVICE_ATTR(in_voltage_scale_available, S_IRUGO,
		mcp3422_show_scales, NULL, 0);

static struct attribute *mcp3422_attributes[] = {
	&iio_const_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_in_voltage_scale_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group mcp3422_attribute_group = {
	.attrs = mcp3422_attributes,
};

static const struct iio_chan_spec mcp3422_channels[] = {
	MCP3422_CHAN(0),
	MCP3422_CHAN(1),
};

static const struct iio_chan_spec mcp3424_channels[] = {
	MCP3422_CHAN(0),
	MCP3422_CHAN(1),
	MCP3422_CHAN(2),
	MCP3422_CHAN(3),
};

static const struct iio_info mcp3422_info = {
	.read_raw = mcp3422_read_raw,
	.write_raw = mcp3422_write_raw,
	.write_raw_get_fmt = mcp3422_write_raw_get_fmt,
	.attrs = &mcp3422_attribute_group,
	.driver_module = THIS_MODULE,
};

static int mcp3422_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct iio_dev *indio_dev;
	struct mcp3422 *adc;
	int err;
	u8 config;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -ENODEV;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*adc));
	if (!indio_dev)
		return -ENOMEM;

	adc = iio_priv(indio_dev);
	adc->i2c = client;

	mutex_init(&adc->lock);

	indio_dev->dev.parent = &client->dev;
	indio_dev->name = dev_name(&client->dev);
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &mcp3422_info;

	switch ((unsigned int)(id->driver_data)) {
	case 2:
	case 3:
		indio_dev->channels = mcp3422_channels;
		indio_dev->num_channels = ARRAY_SIZE(mcp3422_channels);
		break;
	case 4:
		indio_dev->channels = mcp3424_channels;
		indio_dev->num_channels = ARRAY_SIZE(mcp3424_channels);
		break;
	}

	/* meaningful default configuration */
	config = (MCP3422_CONT_SAMPLING
		| MCP3422_CHANNEL_VALUE(1)
		| MCP3422_PGA_VALUE(MCP3422_PGA_1)
		| MCP3422_SAMPLE_RATE_VALUE(MCP3422_SRATE_240));
	mcp3422_update_config(adc, config);

	err = iio_device_register(indio_dev);
	if (err < 0)
		return err;

	i2c_set_clientdata(client, indio_dev);

	return 0;
}

static int mcp3422_remove(struct i2c_client *client)
{
	iio_device_unregister(i2c_get_clientdata(client));
	return 0;
}

static const struct i2c_device_id mcp3422_id[] = {
	{ "mcp3422", 2 },
	{ "mcp3423", 3 },
	{ "mcp3424", 4 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mcp3422_id);

#ifdef CONFIG_OF
static const struct of_device_id mcp3422_of_match[] = {
	{ .compatible = "mcp3422" },
	{ }
};
MODULE_DEVICE_TABLE(of, mcp3422_of_match);
#endif

static struct i2c_driver mcp3422_driver = {
	.driver = {
		.name = "mcp3422",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(mcp3422_of_match),
	},
	.probe = mcp3422_probe,
	.remove = mcp3422_remove,
	.id_table = mcp3422_id,
};
module_i2c_driver(mcp3422_driver);

MODULE_AUTHOR("Angelo Compagnucci <angelo.compagnucci@gmail.com>");
MODULE_DESCRIPTION("Microchip mcp3422/3/4 driver");
MODULE_LICENSE("GPL v2");
