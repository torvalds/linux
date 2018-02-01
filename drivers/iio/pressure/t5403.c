/*
 * t5403.c - Support for EPCOS T5403 pressure/temperature sensor
 *
 * Copyright (c) 2014 Peter Meerwald <pmeerw@pmeerw.net>
 *
 * This file is subject to the terms and conditions of version 2 of
 * the GNU General Public License.  See the file COPYING in the main
 * directory of this archive for more details.
 *
 * (7-bit I2C slave address 0x77)
 *
 * TODO: end-of-conversion irq
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/delay.h>

#define T5403_DATA 0xf5 /* data, LSB first, 16 bit */
#define T5403_CALIB_DATA 0x8e /* 10 calibration coeff., LSB first, 16 bit */
#define T5403_SLAVE_ADDR 0x88 /* I2C slave address, 0x77 */
#define T5403_COMMAND 0xf1

/* command bits */
#define T5403_MODE_SHIFT 3 /* conversion time: 2, 8, 16, 66 ms */
#define T5403_PT BIT(1) /* 0 .. pressure, 1 .. temperature measurement */
#define T5403_SCO BIT(0) /* start conversion */

#define T5403_MODE_LOW 0
#define T5403_MODE_STANDARD 1
#define T5403_MODE_HIGH 2
#define T5403_MODE_ULTRA_HIGH 3

#define T5403_I2C_MASK (~BIT(7))
#define T5403_I2C_ADDR 0x77

static const int t5403_pressure_conv_ms[] = {2, 8, 16, 66};

struct t5403_data {
	struct i2c_client *client;
	struct mutex lock;
	int mode;
	__le16 c[10];
};

#define T5403_C_U16(i) le16_to_cpu(data->c[(i) - 1])
#define T5403_C(i) sign_extend32(T5403_C_U16(i), 15)

static int t5403_read(struct t5403_data *data, bool pressure)
{
	int wait_time = 3;  /* wakeup time in ms */

	int ret = i2c_smbus_write_byte_data(data->client, T5403_COMMAND,
		(pressure ? (data->mode << T5403_MODE_SHIFT) : T5403_PT) |
		T5403_SCO);
	if (ret < 0)
		return ret;

	wait_time += pressure ? t5403_pressure_conv_ms[data->mode] : 2;

	msleep(wait_time);

	return i2c_smbus_read_word_data(data->client, T5403_DATA);
}

static int t5403_comp_pressure(struct t5403_data *data, int *val, int *val2)
{
	int ret;
	s16 t_r;
	u16 p_r;
	s32 S, O, X;

	mutex_lock(&data->lock);

	ret = t5403_read(data, false);
	if (ret < 0)
		goto done;
	t_r = ret;

	ret = t5403_read(data, true);
	if (ret < 0)
		goto done;
	p_r = ret;

	/* see EPCOS application note */
	S = T5403_C_U16(3) + (s32) T5403_C_U16(4) * t_r / 0x20000 +
		T5403_C(5) * t_r / 0x8000 * t_r / 0x80000 +
		T5403_C(9) * t_r / 0x8000 * t_r / 0x8000 * t_r / 0x10000;

	O = T5403_C(6) * 0x4000 + T5403_C(7) * t_r / 8 +
		T5403_C(8) * t_r / 0x8000 * t_r / 16 +
		T5403_C(9) * t_r / 0x8000 * t_r / 0x10000 * t_r;

	X = (S * p_r + O) / 0x4000;

	X += ((X - 75000) * (X - 75000) / 0x10000 - 9537) *
	    T5403_C(10) / 0x10000;

	*val = X / 1000;
	*val2 = (X % 1000) * 1000;

done:
	mutex_unlock(&data->lock);
	return ret;
}

static int t5403_comp_temp(struct t5403_data *data, int *val)
{
	int ret;
	s16 t_r;

	mutex_lock(&data->lock);
	ret = t5403_read(data, false);
	if (ret < 0)
		goto done;
	t_r = ret;

	/* see EPCOS application note */
	*val = ((s32) T5403_C_U16(1) * t_r / 0x100 +
		(s32) T5403_C_U16(2) * 0x40) * 1000 / 0x10000;

done:
	mutex_unlock(&data->lock);
	return ret;
}

static int t5403_read_raw(struct iio_dev *indio_dev,
			  struct iio_chan_spec const *chan,
			  int *val, int *val2, long mask)
{
	struct t5403_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_PROCESSED:
		switch (chan->type) {
		case IIO_PRESSURE:
			ret = t5403_comp_pressure(data, val, val2);
			if (ret < 0)
				return ret;
			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_TEMP:
			ret = t5403_comp_temp(data, val);
			if (ret < 0)
				return ret;
			return IIO_VAL_INT;
		default:
			return -EINVAL;
	    }
	case IIO_CHAN_INFO_INT_TIME:
		*val = 0;
		*val2 = t5403_pressure_conv_ms[data->mode] * 1000;
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

static int t5403_write_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int val, int val2, long mask)
{
	struct t5403_data *data = iio_priv(indio_dev);
	int i;

	switch (mask) {
	case IIO_CHAN_INFO_INT_TIME:
		if (val != 0)
			return -EINVAL;
		for (i = 0; i < ARRAY_SIZE(t5403_pressure_conv_ms); i++)
			if (val2 == t5403_pressure_conv_ms[i] * 1000) {
				mutex_lock(&data->lock);
				data->mode = i;
				mutex_unlock(&data->lock);
				return 0;
			}
		return -EINVAL;
	default:
		return -EINVAL;
	}
}

static const struct iio_chan_spec t5403_channels[] = {
	{
		.type = IIO_PRESSURE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED) |
		    BIT(IIO_CHAN_INFO_INT_TIME),
	},
	{
		.type = IIO_TEMP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),
	},
};

static IIO_CONST_ATTR_INT_TIME_AVAIL("0.002 0.008 0.016 0.066");

static struct attribute *t5403_attributes[] = {
	&iio_const_attr_integration_time_available.dev_attr.attr,
	NULL
};

static const struct attribute_group t5403_attribute_group = {
	.attrs = t5403_attributes,
};

static const struct iio_info t5403_info = {
	.read_raw = &t5403_read_raw,
	.write_raw = &t5403_write_raw,
	.attrs = &t5403_attribute_group,
};

static int t5403_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct t5403_data *data;
	struct iio_dev *indio_dev;
	int ret;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_WORD_DATA |
	    I2C_FUNC_SMBUS_I2C_BLOCK))
		return -EOPNOTSUPP;

	ret = i2c_smbus_read_byte_data(client, T5403_SLAVE_ADDR);
	if (ret < 0)
		return ret;
	if ((ret & T5403_I2C_MASK) != T5403_I2C_ADDR)
		return -ENODEV;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	data->client = client;
	mutex_init(&data->lock);

	i2c_set_clientdata(client, indio_dev);
	indio_dev->info = &t5403_info;
	indio_dev->name = id->name;
	indio_dev->dev.parent = &client->dev;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = t5403_channels;
	indio_dev->num_channels = ARRAY_SIZE(t5403_channels);

	data->mode = T5403_MODE_STANDARD;

	ret = i2c_smbus_read_i2c_block_data(data->client, T5403_CALIB_DATA,
	    sizeof(data->c), (u8 *) data->c);
	if (ret < 0)
		return ret;

	return devm_iio_device_register(&client->dev, indio_dev);
}

static const struct i2c_device_id t5403_id[] = {
	{ "t5403", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, t5403_id);

static struct i2c_driver t5403_driver = {
	.driver = {
		.name	= "t5403",
	},
	.probe = t5403_probe,
	.id_table = t5403_id,
};
module_i2c_driver(t5403_driver);

MODULE_AUTHOR("Peter Meerwald <pmeerw@pmeerw.net>");
MODULE_DESCRIPTION("EPCOS T5403 pressure/temperature sensor driver");
MODULE_LICENSE("GPL");
