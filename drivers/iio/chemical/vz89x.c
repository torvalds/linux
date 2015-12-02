/*
 * vz89x.c - Support for SGX Sensortech MiCS VZ89X VOC sensors
 *
 * Copyright (C) 2015 Matt Ranostay <mranostay@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/init.h>
#include <linux/i2c.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#define VZ89X_REG_MEASUREMENT		0x09
#define VZ89X_REG_MEASUREMENT_SIZE	6

#define VZ89X_VOC_CO2_IDX		0
#define VZ89X_VOC_SHORT_IDX		1
#define VZ89X_VOC_TVOC_IDX		2
#define VZ89X_VOC_RESISTANCE_IDX	3

struct vz89x_data {
	struct i2c_client *client;
	struct mutex lock;
	int (*xfer)(struct vz89x_data *data, u8 cmd);

	unsigned long last_update;
	u8 buffer[VZ89X_REG_MEASUREMENT_SIZE];
};

static const struct iio_chan_spec vz89x_channels[] = {
	{
		.type = IIO_CONCENTRATION,
		.channel2 = IIO_MOD_CO2,
		.modified = 1,
		.info_mask_separate =
			BIT(IIO_CHAN_INFO_OFFSET) | BIT(IIO_CHAN_INFO_RAW),
		.address = VZ89X_VOC_CO2_IDX,
	},
	{
		.type = IIO_CONCENTRATION,
		.channel2 = IIO_MOD_VOC,
		.modified = 1,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.address = VZ89X_VOC_SHORT_IDX,
		.extend_name = "short",
	},
	{
		.type = IIO_CONCENTRATION,
		.channel2 = IIO_MOD_VOC,
		.modified = 1,
		.info_mask_separate =
			BIT(IIO_CHAN_INFO_OFFSET) | BIT(IIO_CHAN_INFO_RAW),
		.address = VZ89X_VOC_TVOC_IDX,
	},
	{
		.type = IIO_RESISTANCE,
		.info_mask_separate =
			BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
		.address = VZ89X_VOC_RESISTANCE_IDX,
	},
};

static IIO_CONST_ATTR(in_concentration_co2_scale, "0.00000698689");
static IIO_CONST_ATTR(in_concentration_voc_scale, "0.00000000436681223");

static struct attribute *vz89x_attributes[] = {
	&iio_const_attr_in_concentration_co2_scale.dev_attr.attr,
	&iio_const_attr_in_concentration_voc_scale.dev_attr.attr,
	NULL,
};

static const struct attribute_group vz89x_attrs_group = {
	.attrs = vz89x_attributes,
};

/*
 * Chipset sometime updates in the middle of a reading causing it to reset the
 * data pointer, and causing invalid reading of previous data.
 * We can check for this by reading MSB of the resistance reading that is
 * always zero, and by also confirming the VOC_short isn't zero.
 */

static int vz89x_measurement_is_valid(struct vz89x_data *data)
{
	if (data->buffer[VZ89X_VOC_SHORT_IDX] == 0)
		return 1;

	return !!(data->buffer[VZ89X_REG_MEASUREMENT_SIZE - 1] > 0);
}

static int vz89x_i2c_xfer(struct vz89x_data *data, u8 cmd)
{
	struct i2c_client *client = data->client;
	struct i2c_msg msg[2];
	int ret;
	u8 buf[3] = { cmd, 0, 0};

	msg[0].addr = client->addr;
	msg[0].flags = client->flags;
	msg[0].len = 3;
	msg[0].buf  = (char *) &buf;

	msg[1].addr = client->addr;
	msg[1].flags = client->flags | I2C_M_RD;
	msg[1].len = VZ89X_REG_MEASUREMENT_SIZE;
	msg[1].buf = (char *) &data->buffer;

	ret = i2c_transfer(client->adapter, msg, 2);

	return (ret == 2) ? 0 : ret;
}

static int vz89x_smbus_xfer(struct vz89x_data *data, u8 cmd)
{
	struct i2c_client *client = data->client;
	int ret;
	int i;

	ret = i2c_smbus_write_word_data(client, cmd, 0);
	if (ret < 0)
		return ret;

	for (i = 0; i < VZ89X_REG_MEASUREMENT_SIZE; i++) {
		ret = i2c_smbus_read_byte(client);
		if (ret < 0)
			return ret;
		data->buffer[i] = ret;
	}

	return 0;
}

static int vz89x_get_measurement(struct vz89x_data *data)
{
	int ret;

	/* sensor can only be polled once a second max per datasheet */
	if (!time_after(jiffies, data->last_update + HZ))
		return 0;

	ret = data->xfer(data, VZ89X_REG_MEASUREMENT);
	if (ret < 0)
		return ret;

	ret = vz89x_measurement_is_valid(data);
	if (ret)
		return -EAGAIN;

	data->last_update = jiffies;

	return 0;
}

static int vz89x_get_resistance_reading(struct vz89x_data *data)
{
	u8 *buf = &data->buffer[VZ89X_VOC_RESISTANCE_IDX];

	return buf[0] | (buf[1] << 8);
}

static int vz89x_read_raw(struct iio_dev *indio_dev,
			  struct iio_chan_spec const *chan, int *val,
			  int *val2, long mask)
{
	struct vz89x_data *data = iio_priv(indio_dev);
	int ret = -EINVAL;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&data->lock);
		ret = vz89x_get_measurement(data);
		mutex_unlock(&data->lock);

		if (ret)
			return ret;

		switch (chan->address) {
		case VZ89X_VOC_CO2_IDX:
		case VZ89X_VOC_SHORT_IDX:
		case VZ89X_VOC_TVOC_IDX:
			*val = data->buffer[chan->address];
			return IIO_VAL_INT;
		case VZ89X_VOC_RESISTANCE_IDX:
			*val = vz89x_get_resistance_reading(data);
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
		break;
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_RESISTANCE:
			*val = 10;
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
		break;
	case IIO_CHAN_INFO_OFFSET:
		switch (chan->address) {
		case VZ89X_VOC_CO2_IDX:
			*val = 44;
			*val2 = 250000;
			return IIO_VAL_INT_PLUS_MICRO;
		case VZ89X_VOC_TVOC_IDX:
			*val = -13;
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	}

	return ret;
}

static const struct iio_info vz89x_info = {
	.attrs		= &vz89x_attrs_group,
	.read_raw	= vz89x_read_raw,
	.driver_module	= THIS_MODULE,
};

static int vz89x_probe(struct i2c_client *client,
		       const struct i2c_device_id *id)
{
	struct iio_dev *indio_dev;
	struct vz89x_data *data;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;
	data = iio_priv(indio_dev);

	if (i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		data->xfer = vz89x_i2c_xfer;
	else if (i2c_check_functionality(client->adapter,
				I2C_FUNC_SMBUS_WORD_DATA | I2C_FUNC_SMBUS_BYTE))
		data->xfer = vz89x_smbus_xfer;
	else
		return -ENOTSUPP;

	i2c_set_clientdata(client, indio_dev);
	data->client = client;
	data->last_update = jiffies - HZ;
	mutex_init(&data->lock);

	indio_dev->dev.parent = &client->dev;
	indio_dev->info = &vz89x_info,
	indio_dev->name = dev_name(&client->dev);
	indio_dev->modes = INDIO_DIRECT_MODE;

	indio_dev->channels = vz89x_channels;
	indio_dev->num_channels = ARRAY_SIZE(vz89x_channels);

	return devm_iio_device_register(&client->dev, indio_dev);
}

static const struct i2c_device_id vz89x_id[] = {
	{ "vz89x", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, vz89x_id);

static const struct of_device_id vz89x_dt_ids[] = {
	{ .compatible = "sgx,vz89x" },
	{ }
};
MODULE_DEVICE_TABLE(of, vz89x_dt_ids);

static struct i2c_driver vz89x_driver = {
	.driver = {
		.name	= "vz89x",
		.of_match_table = of_match_ptr(vz89x_dt_ids),
	},
	.probe = vz89x_probe,
	.id_table = vz89x_id,
};
module_i2c_driver(vz89x_driver);

MODULE_AUTHOR("Matt Ranostay <mranostay@gmail.com>");
MODULE_DESCRIPTION("SGX Sensortech MiCS VZ89X VOC sensors");
MODULE_LICENSE("GPL v2");
