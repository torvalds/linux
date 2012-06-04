/*
 *  max517.c - Support for Maxim MAX517, MAX518 and MAX519
 *
 *  Copyright (C) 2010, 2011 Roland Stigge <stigge@antcom.de>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/err.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include "dac.h"

#include "max517.h"

#define MAX517_DRV_NAME	"max517"

/* Commands */
#define COMMAND_CHANNEL0	0x00
#define COMMAND_CHANNEL1	0x01 /* for MAX518 and MAX519 */
#define COMMAND_PD		0x08 /* Power Down */

enum max517_device_ids {
	ID_MAX517,
	ID_MAX518,
	ID_MAX519,
};

struct max517_data {
	struct iio_dev		*indio_dev;
	struct i2c_client	*client;
	unsigned short		vref_mv[2];
};

/*
 * channel: bit 0: channel 1
 *          bit 1: channel 2
 * (this way, it's possible to set both channels at once)
 */
static ssize_t max517_set_value(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count, int channel)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct max517_data *data = iio_priv(indio_dev);
	struct i2c_client *client = data->client;
	u8 outbuf[4]; /* 1x or 2x command + value */
	int outbuf_size = 0;
	int res;
	long val;

	res = strict_strtol(buf, 10, &val);

	if (res)
		return res;

	if (val < 0 || val > 255)
		return -EINVAL;

	if (channel & 1) {
		outbuf[outbuf_size++] = COMMAND_CHANNEL0;
		outbuf[outbuf_size++] = val;
	}
	if (channel & 2) {
		outbuf[outbuf_size++] = COMMAND_CHANNEL1;
		outbuf[outbuf_size++] = val;
	}

	/*
	 * At this point, there are always 1 or 2 two-byte commands in
	 * outbuf. With 2 commands, the device can set two outputs
	 * simultaneously, latching the values upon the end of the I2C
	 * transfer.
	 */

	res = i2c_master_send(client, outbuf, outbuf_size);
	if (res < 0)
		return res;

	return count;
}

static ssize_t max517_set_value_1(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	return max517_set_value(dev, attr, buf, count, 1);
}
static IIO_DEV_ATTR_OUT_RAW(1, max517_set_value_1, 0);

static ssize_t max517_set_value_2(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	return max517_set_value(dev, attr, buf, count, 2);
}
static IIO_DEV_ATTR_OUT_RAW(2, max517_set_value_2, 1);

static ssize_t max517_set_value_both(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	return max517_set_value(dev, attr, buf, count, 3);
}
static IIO_DEVICE_ATTR_NAMED(out_voltage1and2_raw,
			     out_voltage1&2_raw, S_IWUSR, NULL,
			     max517_set_value_both, -1);

static ssize_t max517_show_scale(struct device *dev,
				struct device_attribute *attr,
				char *buf, int channel)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct max517_data *data = iio_priv(indio_dev);
	/* Corresponds to Vref / 2^(bits) */
	unsigned int scale_uv = (data->vref_mv[channel - 1] * 1000) >> 8;

	return sprintf(buf, "%d.%03d\n", scale_uv / 1000, scale_uv % 1000);
}

static ssize_t max517_show_scale1(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	return max517_show_scale(dev, attr, buf, 1);
}
static IIO_DEVICE_ATTR(out_voltage1_scale, S_IRUGO,
		       max517_show_scale1, NULL, 0);

static ssize_t max517_show_scale2(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	return max517_show_scale(dev, attr, buf, 2);
}
static IIO_DEVICE_ATTR(out_voltage2_scale, S_IRUGO,
		       max517_show_scale2, NULL, 0);

/* On MAX517 variant, we have one output */
static struct attribute *max517_attributes[] = {
	&iio_dev_attr_out_voltage1_raw.dev_attr.attr,
	&iio_dev_attr_out_voltage1_scale.dev_attr.attr,
	NULL
};

static struct attribute_group max517_attribute_group = {
	.attrs = max517_attributes,
};

/* On MAX518 and MAX519 variant, we have two outputs */
static struct attribute *max518_attributes[] = {
	&iio_dev_attr_out_voltage1_raw.dev_attr.attr,
	&iio_dev_attr_out_voltage1_scale.dev_attr.attr,
	&iio_dev_attr_out_voltage2_raw.dev_attr.attr,
	&iio_dev_attr_out_voltage2_scale.dev_attr.attr,
	&iio_dev_attr_out_voltage1and2_raw.dev_attr.attr,
	NULL
};

static struct attribute_group max518_attribute_group = {
	.attrs = max518_attributes,
};

#ifdef CONFIG_PM_SLEEP
static int max517_suspend(struct device *dev)
{
	u8 outbuf = COMMAND_PD;

	return i2c_master_send(to_i2c_client(dev), &outbuf, 1);
}

static int max517_resume(struct device *dev)
{
	u8 outbuf = 0;

	return i2c_master_send(to_i2c_client(dev), &outbuf, 1);
}

static SIMPLE_DEV_PM_OPS(max517_pm_ops, max517_suspend, max517_resume);
#define MAX517_PM_OPS (&max517_pm_ops)
#else
#define MAX517_PM_OPS NULL
#endif

static const struct iio_info max517_info = {
	.attrs = &max517_attribute_group,
	.driver_module = THIS_MODULE,
};

static const struct iio_info max518_info = {
	.attrs = &max518_attribute_group,
	.driver_module = THIS_MODULE,
};

static int max517_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct max517_data *data;
	struct iio_dev *indio_dev;
	struct max517_platform_data *platform_data = client->dev.platform_data;
	int err;

	indio_dev = iio_device_alloc(sizeof(*data));
	if (indio_dev == NULL) {
		err = -ENOMEM;
		goto exit;
	}
	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client = client;

	/* establish that the iio_dev is a child of the i2c device */
	indio_dev->dev.parent = &client->dev;

	/* reduced attribute set for MAX517 */
	if (id->driver_data == ID_MAX517)
		indio_dev->info = &max517_info;
	else
		indio_dev->info = &max518_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	/*
	 * Reference voltage on MAX518 and default is 5V, else take vref_mv
	 * from platform_data
	 */
	if (id->driver_data == ID_MAX518 || !platform_data) {
		data->vref_mv[0] = data->vref_mv[1] = 5000; /* mV */
	} else {
		data->vref_mv[0] = platform_data->vref_mv[0];
		data->vref_mv[1] = platform_data->vref_mv[1];
	}

	err = iio_device_register(indio_dev);
	if (err)
		goto exit_free_device;

	dev_info(&client->dev, "DAC registered\n");

	return 0;

exit_free_device:
	iio_device_free(indio_dev);
exit:
	return err;
}

static int max517_remove(struct i2c_client *client)
{
	iio_device_unregister(i2c_get_clientdata(client));
	iio_device_free(i2c_get_clientdata(client));

	return 0;
}

static const struct i2c_device_id max517_id[] = {
	{ "max517", ID_MAX517 },
	{ "max518", ID_MAX518 },
	{ "max519", ID_MAX519 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max517_id);

static struct i2c_driver max517_driver = {
	.driver = {
		.name	= MAX517_DRV_NAME,
		.pm		= MAX517_PM_OPS,
	},
	.probe		= max517_probe,
	.remove		= max517_remove,
	.id_table	= max517_id,
};
module_i2c_driver(max517_driver);

MODULE_AUTHOR("Roland Stigge <stigge@antcom.de>");
MODULE_DESCRIPTION("MAX517/MAX518/MAX519 8-bit DAC");
MODULE_LICENSE("GPL");
