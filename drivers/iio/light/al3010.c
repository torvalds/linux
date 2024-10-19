// SPDX-License-Identifier: GPL-2.0-only
/*
 * AL3010 - Dyna Image Ambient Light Sensor
 *
 * Copyright (c) 2014, Intel Corporation.
 * Copyright (c) 2016, Dyna-Image Corp.
 * Copyright (c) 2020, David Heidelberg, Michał Mirosław, Dmitry Osipenko
 *
 * IIO driver for AL3010 (7-bit I2C slave address 0x1C).
 *
 * TODO: interrupt support, thresholds
 * When the driver will get support for interrupt handling, then interrupt
 * will need to be disabled before turning sensor OFF in order to avoid
 * potential races with the interrupt handling.
 */

#include <linux/bitfield.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#define AL3010_DRV_NAME "al3010"

#define AL3010_REG_SYSTEM		0x00
#define AL3010_REG_DATA_LOW		0x0c
#define AL3010_REG_CONFIG		0x10

#define AL3010_CONFIG_DISABLE		0x00
#define AL3010_CONFIG_ENABLE		0x01

#define AL3010_GAIN_MASK		GENMASK(6,4)

#define AL3010_SCALE_AVAILABLE "1.1872 0.2968 0.0742 0.018"

enum al3xxxx_range {
	AL3XXX_RANGE_1, /* 77806 lx */
	AL3XXX_RANGE_2, /* 19542 lx */
	AL3XXX_RANGE_3, /*  4863 lx */
	AL3XXX_RANGE_4  /*  1216 lx */
};

static const int al3010_scales[][2] = {
	{0, 1187200}, {0, 296800}, {0, 74200}, {0, 18600}
};

struct al3010_data {
	struct i2c_client *client;
};

static const struct iio_chan_spec al3010_channels[] = {
	{
		.type	= IIO_LIGHT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE),
	}
};

static IIO_CONST_ATTR(in_illuminance_scale_available, AL3010_SCALE_AVAILABLE);

static struct attribute *al3010_attributes[] = {
	&iio_const_attr_in_illuminance_scale_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group al3010_attribute_group = {
	.attrs = al3010_attributes,
};

static int al3010_set_pwr(struct i2c_client *client, bool pwr)
{
	u8 val = pwr ? AL3010_CONFIG_ENABLE : AL3010_CONFIG_DISABLE;
	return i2c_smbus_write_byte_data(client, AL3010_REG_SYSTEM, val);
}

static void al3010_set_pwr_off(void *_data)
{
	struct al3010_data *data = _data;

	al3010_set_pwr(data->client, false);
}

static int al3010_init(struct al3010_data *data)
{
	int ret;

	ret = al3010_set_pwr(data->client, true);
	if (ret < 0)
		return ret;

	ret = devm_add_action_or_reset(&data->client->dev,
				       al3010_set_pwr_off,
				       data);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_write_byte_data(data->client, AL3010_REG_CONFIG,
					FIELD_PREP(AL3010_GAIN_MASK,
						   AL3XXX_RANGE_3));
	if (ret < 0)
		return ret;

	return 0;
}

static int al3010_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan, int *val,
			   int *val2, long mask)
{
	struct al3010_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		/*
		 * ALS ADC value is stored in two adjacent registers:
		 * - low byte of output is stored at AL3010_REG_DATA_LOW
		 * - high byte of output is stored at AL3010_REG_DATA_LOW + 1
		 */
		ret = i2c_smbus_read_word_data(data->client,
					       AL3010_REG_DATA_LOW);
		if (ret < 0)
			return ret;
		*val = ret;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		ret = i2c_smbus_read_byte_data(data->client,
					       AL3010_REG_CONFIG);
		if (ret < 0)
			return ret;

		ret = FIELD_GET(AL3010_GAIN_MASK, ret);
		*val = al3010_scales[ret][0];
		*val2 = al3010_scales[ret][1];

		return IIO_VAL_INT_PLUS_MICRO;
	}
	return -EINVAL;
}

static int al3010_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan, int val,
			    int val2, long mask)
{
	struct al3010_data *data = iio_priv(indio_dev);
	int i;

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		for (i = 0; i < ARRAY_SIZE(al3010_scales); i++) {
			if (val != al3010_scales[i][0] ||
			    val2 != al3010_scales[i][1])
				continue;

			return i2c_smbus_write_byte_data(data->client,
					AL3010_REG_CONFIG,
					FIELD_PREP(AL3010_GAIN_MASK, i));
		}
		break;
	}
	return -EINVAL;
}

static const struct iio_info al3010_info = {
	.read_raw	= al3010_read_raw,
	.write_raw	= al3010_write_raw,
	.attrs		= &al3010_attribute_group,
};

static int al3010_probe(struct i2c_client *client)
{
	struct al3010_data *data;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client = client;

	indio_dev->info = &al3010_info;
	indio_dev->name = AL3010_DRV_NAME;
	indio_dev->channels = al3010_channels;
	indio_dev->num_channels = ARRAY_SIZE(al3010_channels);
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = al3010_init(data);
	if (ret < 0) {
		dev_err(&client->dev, "al3010 chip init failed\n");
		return ret;
	}

	return devm_iio_device_register(&client->dev, indio_dev);
}

static int al3010_suspend(struct device *dev)
{
	return al3010_set_pwr(to_i2c_client(dev), false);
}

static int al3010_resume(struct device *dev)
{
	return al3010_set_pwr(to_i2c_client(dev), true);
}

static DEFINE_SIMPLE_DEV_PM_OPS(al3010_pm_ops, al3010_suspend, al3010_resume);

static const struct i2c_device_id al3010_id[] = {
	{"al3010", },
	{}
};
MODULE_DEVICE_TABLE(i2c, al3010_id);

static const struct of_device_id al3010_of_match[] = {
	{ .compatible = "dynaimage,al3010", },
	{},
};
MODULE_DEVICE_TABLE(of, al3010_of_match);

static struct i2c_driver al3010_driver = {
	.driver = {
		.name = AL3010_DRV_NAME,
		.of_match_table = al3010_of_match,
		.pm = pm_sleep_ptr(&al3010_pm_ops),
	},
	.probe		= al3010_probe,
	.id_table	= al3010_id,
};
module_i2c_driver(al3010_driver);

MODULE_AUTHOR("Daniel Baluta <daniel.baluta@nxp.com>");
MODULE_AUTHOR("David Heidelberg <david@ixit.cz>");
MODULE_DESCRIPTION("AL3010 Ambient Light Sensor driver");
MODULE_LICENSE("GPL v2");
