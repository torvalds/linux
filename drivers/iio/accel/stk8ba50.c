/**
 * Sensortek STK8BA50 3-Axis Accelerometer
 *
 * Copyright (c) 2015, Intel Corporation.
 *
 * This file is subject to the terms and conditions of version 2 of
 * the GNU General Public License. See the file COPYING in the main
 * directory of this archive for more details.
 *
 * STK8BA50 7-bit I2C address: 0x18.
 */

#include <linux/acpi.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#define STK8BA50_REG_XOUT			0x02
#define STK8BA50_REG_YOUT			0x04
#define STK8BA50_REG_ZOUT			0x06
#define STK8BA50_REG_RANGE			0x0F
#define STK8BA50_REG_POWMODE			0x11
#define STK8BA50_REG_SWRST			0x14

#define STK8BA50_MODE_NORMAL			0
#define STK8BA50_MODE_SUSPEND			1
#define STK8BA50_MODE_POWERBIT			BIT(7)
#define STK8BA50_DATA_SHIFT			6
#define STK8BA50_RESET_CMD			0xB6

#define STK8BA50_DRIVER_NAME			"stk8ba50"

#define STK8BA50_SCALE_AVAIL			"0.0384 0.0767 0.1534 0.3069"

/*
 * The accelerometer has four measurement ranges:
 * +/-2g; +/-4g; +/-8g; +/-16g
 *
 * Acceleration values are 10-bit, 2's complement.
 * Scales are calculated as following:
 *
 * scale1 = (2 + 2) * 9.81 / (2^10 - 1)   = 0.0384
 * scale2 = (4 + 4) * 9.81 / (2^10 - 1)   = 0.0767
 * etc.
 *
 * Scales are stored in this format:
 * { <register value>, <scale value> }
 *
 * Locally, the range is stored as a table index.
 */
static const int stk8ba50_scale_table[][2] = {
	{3, 38400}, {5, 76700}, {8, 153400}, {12, 306900}
};

struct stk8ba50_data {
	struct i2c_client *client;
	struct mutex lock;
	int range;
};

#define STK8BA50_ACCEL_CHANNEL(reg, axis) {			\
	.type = IIO_ACCEL,					\
	.address = reg,						\
	.modified = 1,						\
	.channel2 = IIO_MOD_##axis,				\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),	\
}

static const struct iio_chan_spec stk8ba50_channels[] = {
	STK8BA50_ACCEL_CHANNEL(STK8BA50_REG_XOUT, X),
	STK8BA50_ACCEL_CHANNEL(STK8BA50_REG_YOUT, Y),
	STK8BA50_ACCEL_CHANNEL(STK8BA50_REG_ZOUT, Z),
};

static IIO_CONST_ATTR(in_accel_scale_available, STK8BA50_SCALE_AVAIL);

static struct attribute *stk8ba50_attributes[] = {
	&iio_const_attr_in_accel_scale_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group stk8ba50_attribute_group = {
	.attrs = stk8ba50_attributes
};

static int stk8ba50_read_accel(struct stk8ba50_data *data, u8 reg)
{
	int ret;
	struct i2c_client *client = data->client;

	ret = i2c_smbus_read_word_data(client, reg);
	if (ret < 0) {
		dev_err(&client->dev, "register read failed\n");
		return ret;
	}

	return sign_extend32(ret >> STK8BA50_DATA_SHIFT, 9);
}

static int stk8ba50_read_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int *val, int *val2, long mask)
{
	struct stk8ba50_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&data->lock);
		*val = stk8ba50_read_accel(data, chan->address);
		mutex_unlock(&data->lock);
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = 0;
		*val2 = stk8ba50_scale_table[data->range][1];
		return IIO_VAL_INT_PLUS_MICRO;
	}

	return -EINVAL;
}

static int stk8ba50_write_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int val, int val2, long mask)
{
	int ret;
	int i;
	int index = -1;
	struct stk8ba50_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		if (val != 0)
			return -EINVAL;

		for (i = 0; i < ARRAY_SIZE(stk8ba50_scale_table); i++)
			if (val2 == stk8ba50_scale_table[i][1]) {
				index = i;
				break;
			}
		if (index < 0)
			return -EINVAL;

		ret = i2c_smbus_write_byte_data(data->client,
				STK8BA50_REG_RANGE,
				stk8ba50_scale_table[index][0]);
		if (ret < 0)
			dev_err(&data->client->dev,
					"failed to set measurement range\n");
		else
			data->range = index;

		return ret;
	}

	return -EINVAL;
}

static const struct iio_info stk8ba50_info = {
	.driver_module		= THIS_MODULE,
	.read_raw		= stk8ba50_read_raw,
	.write_raw		= stk8ba50_write_raw,
	.attrs			= &stk8ba50_attribute_group,
};

static int stk8ba50_set_power(struct stk8ba50_data *data, bool mode)
{
	int ret;
	u8 masked_reg;
	struct i2c_client *client = data->client;

	ret = i2c_smbus_read_byte_data(client, STK8BA50_REG_POWMODE);
	if (ret < 0)
		goto exit_err;

	if (mode)
		masked_reg = ret | STK8BA50_MODE_POWERBIT;
	else
		masked_reg = ret & (~STK8BA50_MODE_POWERBIT);

	ret = i2c_smbus_write_byte_data(client, STK8BA50_REG_POWMODE,
					masked_reg);
	if (ret < 0)
		goto exit_err;

	return ret;

exit_err:
	dev_err(&client->dev, "failed to change sensor mode\n");
	return ret;
}

static int stk8ba50_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	int ret;
	struct iio_dev *indio_dev;
	struct stk8ba50_data *data;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev) {
		dev_err(&client->dev, "iio allocation failed!\n");
		return -ENOMEM;
	}

	data = iio_priv(indio_dev);
	data->client = client;
	i2c_set_clientdata(client, indio_dev);
	mutex_init(&data->lock);

	indio_dev->dev.parent = &client->dev;
	indio_dev->info = &stk8ba50_info;
	indio_dev->name = STK8BA50_DRIVER_NAME;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = stk8ba50_channels;
	indio_dev->num_channels = ARRAY_SIZE(stk8ba50_channels);

	/* Reset all registers on startup */
	ret = i2c_smbus_write_byte_data(client,
			STK8BA50_REG_SWRST, STK8BA50_RESET_CMD);
	if (ret < 0) {
		dev_err(&client->dev, "failed to reset sensor\n");
		return ret;
	}

	/* The default range is +/-2g */
	data->range = 0;

	ret = iio_device_register(indio_dev);
	if (ret < 0) {
		dev_err(&client->dev, "device_register failed\n");
		stk8ba50_set_power(data, STK8BA50_MODE_SUSPEND);
	}

	return ret;
}

static int stk8ba50_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);

	iio_device_unregister(indio_dev);

	return stk8ba50_set_power(iio_priv(indio_dev), STK8BA50_MODE_SUSPEND);
}

#ifdef CONFIG_PM_SLEEP
static int stk8ba50_suspend(struct device *dev)
{
	struct stk8ba50_data *data;

	data = iio_priv(i2c_get_clientdata(to_i2c_client(dev)));

	return stk8ba50_set_power(data, STK8BA50_MODE_SUSPEND);
}

static int stk8ba50_resume(struct device *dev)
{
	struct stk8ba50_data *data;

	data = iio_priv(i2c_get_clientdata(to_i2c_client(dev)));

	return stk8ba50_set_power(data, STK8BA50_MODE_NORMAL);
}

static SIMPLE_DEV_PM_OPS(stk8ba50_pm_ops, stk8ba50_suspend, stk8ba50_resume);

#define STK8BA50_PM_OPS (&stk8ba50_pm_ops)
#else
#define STK8BA50_PM_OPS NULL
#endif

static const struct i2c_device_id stk8ba50_i2c_id[] = {
	{"stk8ba50", 0},
	{}
};

static const struct acpi_device_id stk8ba50_acpi_id[] = {
	{"STK8BA50", 0},
	{}
};

MODULE_DEVICE_TABLE(acpi, stk8ba50_acpi_id);

static struct i2c_driver stk8ba50_driver = {
	.driver = {
		.name = "stk8ba50",
		.pm = STK8BA50_PM_OPS,
		.acpi_match_table = ACPI_PTR(stk8ba50_acpi_id),
	},
	.probe =            stk8ba50_probe,
	.remove =           stk8ba50_remove,
	.id_table =         stk8ba50_i2c_id,
};

module_i2c_driver(stk8ba50_driver);

MODULE_AUTHOR("Tiberiu Breana <tiberiu.a.breana@intel.com>");
MODULE_DESCRIPTION("STK8BA50 3-Axis Accelerometer driver");
MODULE_LICENSE("GPL v2");
