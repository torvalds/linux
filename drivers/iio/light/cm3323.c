// SPDX-License-Identifier: GPL-2.0-only
/*
 * CM3323 - Capella Color Light Sensor
 *
 * Copyright (c) 2015, Intel Corporation.
 *
 * IIO driver for CM3323 (7-bit I2C slave address 0x10)
 *
 * TODO: calibscale to correct the lens factor
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/mutex.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#define CM3323_DRV_NAME "cm3323"

#define CM3323_CMD_CONF		0x00
#define CM3323_CMD_RED_DATA	0x08
#define CM3323_CMD_GREEN_DATA	0x09
#define CM3323_CMD_BLUE_DATA	0x0A
#define CM3323_CMD_CLEAR_DATA	0x0B

#define CM3323_CONF_SD_BIT	BIT(0) /* sensor disable */
#define CM3323_CONF_AF_BIT	BIT(1) /* auto/manual force mode */
#define CM3323_CONF_IT_MASK	GENMASK(6, 4)
#define CM3323_CONF_IT_SHIFT	4

#define CM3323_INT_TIME_AVAILABLE "0.04 0.08 0.16 0.32 0.64 1.28"

static const struct {
	int val;
	int val2;
} cm3323_int_time[] = {
	{0, 40000},  /* 40 ms */
	{0, 80000},  /* 80 ms */
	{0, 160000}, /* 160 ms */
	{0, 320000}, /* 320 ms */
	{0, 640000}, /* 640 ms */
	{1, 280000}, /* 1280 ms */
};

struct cm3323_data {
	struct i2c_client *client;
	u16 reg_conf;
	struct mutex mutex;
};

#define CM3323_COLOR_CHANNEL(_color, _addr) { \
	.type = IIO_INTENSITY, \
	.modified = 1, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
	.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_INT_TIME), \
	.channel2 = IIO_MOD_LIGHT_##_color, \
	.address = _addr, \
}

static const struct iio_chan_spec cm3323_channels[] = {
	CM3323_COLOR_CHANNEL(RED, CM3323_CMD_RED_DATA),
	CM3323_COLOR_CHANNEL(GREEN, CM3323_CMD_GREEN_DATA),
	CM3323_COLOR_CHANNEL(BLUE, CM3323_CMD_BLUE_DATA),
	CM3323_COLOR_CHANNEL(CLEAR, CM3323_CMD_CLEAR_DATA),
};

static IIO_CONST_ATTR_INT_TIME_AVAIL(CM3323_INT_TIME_AVAILABLE);

static struct attribute *cm3323_attributes[] = {
	&iio_const_attr_integration_time_available.dev_attr.attr,
	NULL
};

static const struct attribute_group cm3323_attribute_group = {
	.attrs = cm3323_attributes,
};

static int cm3323_init(struct iio_dev *indio_dev)
{
	int ret;
	struct cm3323_data *data = iio_priv(indio_dev);

	ret = i2c_smbus_read_word_data(data->client, CM3323_CMD_CONF);
	if (ret < 0) {
		dev_err(&data->client->dev, "Error reading reg_conf\n");
		return ret;
	}

	/* enable sensor and set auto force mode */
	ret &= ~(CM3323_CONF_SD_BIT | CM3323_CONF_AF_BIT);

	ret = i2c_smbus_write_word_data(data->client, CM3323_CMD_CONF, ret);
	if (ret < 0) {
		dev_err(&data->client->dev, "Error writing reg_conf\n");
		return ret;
	}

	data->reg_conf = ret;

	return 0;
}

static void cm3323_disable(void *data)
{
	int ret;
	struct iio_dev *indio_dev = data;
	struct cm3323_data *cm_data = iio_priv(indio_dev);

	ret = i2c_smbus_write_word_data(cm_data->client, CM3323_CMD_CONF,
					CM3323_CONF_SD_BIT);
	if (ret < 0)
		dev_err(&cm_data->client->dev, "Error writing reg_conf\n");
}

static int cm3323_set_it_bits(struct cm3323_data *data, int val, int val2)
{
	int i, ret;
	u16 reg_conf;

	for (i = 0; i < ARRAY_SIZE(cm3323_int_time); i++) {
		if (val == cm3323_int_time[i].val &&
		    val2 == cm3323_int_time[i].val2) {
			reg_conf = data->reg_conf & ~CM3323_CONF_IT_MASK;
			reg_conf |= i << CM3323_CONF_IT_SHIFT;

			ret = i2c_smbus_write_word_data(data->client,
							CM3323_CMD_CONF,
							reg_conf);
			if (ret < 0)
				return ret;

			data->reg_conf = reg_conf;

			return 0;
		}
	}

	return -EINVAL;
}

static int cm3323_get_it_bits(struct cm3323_data *data)
{
	int bits;

	bits = (data->reg_conf & CM3323_CONF_IT_MASK) >>
		CM3323_CONF_IT_SHIFT;

	if (bits >= ARRAY_SIZE(cm3323_int_time))
		return -EINVAL;

	return bits;
}

static int cm3323_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan, int *val,
			   int *val2, long mask)
{
	int ret;
	struct cm3323_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&data->mutex);
		ret = i2c_smbus_read_word_data(data->client, chan->address);
		if (ret < 0) {
			mutex_unlock(&data->mutex);
			return ret;
		}
		*val = ret;
		mutex_unlock(&data->mutex);

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_INT_TIME:
		mutex_lock(&data->mutex);
		ret = cm3323_get_it_bits(data);
		if (ret < 0) {
			mutex_unlock(&data->mutex);
			return ret;
		}

		*val = cm3323_int_time[ret].val;
		*val2 = cm3323_int_time[ret].val2;
		mutex_unlock(&data->mutex);

		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

static int cm3323_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan, int val,
			    int val2, long mask)
{
	struct cm3323_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_INT_TIME:
		mutex_lock(&data->mutex);
		ret = cm3323_set_it_bits(data, val, val2);
		mutex_unlock(&data->mutex);

		return ret;
	default:
		return -EINVAL;
	}
}

static const struct iio_info cm3323_info = {
	.read_raw	= cm3323_read_raw,
	.write_raw	= cm3323_write_raw,
	.attrs		= &cm3323_attribute_group,
};

static int cm3323_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct cm3323_data *data;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client = client;

	mutex_init(&data->mutex);

	indio_dev->dev.parent = &client->dev;
	indio_dev->info = &cm3323_info;
	indio_dev->name = CM3323_DRV_NAME;
	indio_dev->channels = cm3323_channels;
	indio_dev->num_channels = ARRAY_SIZE(cm3323_channels);
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = cm3323_init(indio_dev);
	if (ret < 0) {
		dev_err(&client->dev, "cm3323 chip init failed\n");
		return ret;
	}

	ret = devm_add_action_or_reset(&client->dev, cm3323_disable, indio_dev);
	if (ret < 0)
		return ret;

	return devm_iio_device_register(&client->dev, indio_dev);
}

static const struct i2c_device_id cm3323_id[] = {
	{"cm3323", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, cm3323_id);

static struct i2c_driver cm3323_driver = {
	.driver = {
		.name = CM3323_DRV_NAME,
	},
	.probe		= cm3323_probe,
	.id_table	= cm3323_id,
};

module_i2c_driver(cm3323_driver);

MODULE_AUTHOR("Daniel Baluta <daniel.baluta@intel.com>");
MODULE_DESCRIPTION("Capella CM3323 Color Light Sensor driver");
MODULE_LICENSE("GPL v2");
