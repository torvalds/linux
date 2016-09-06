/*
 * mcp4725.c - Support for Microchip MCP4725
 *
 * Copyright (C) 2012 Peter Meerwald <pmeerw@pmeerw.net>
 *
 * Based on max517 by Roland Stigge <stigge@antcom.de>
 *
 * This file is subject to the terms and conditions of version 2 of
 * the GNU General Public License.  See the file COPYING in the main
 * directory of this archive for more details.
 *
 * driver for the Microchip I2C 12-bit digital-to-analog converter (DAC)
 * (7-bit I2C slave address 0x60, the three LSBs can be configured in
 * hardware)
 *
 * writing the DAC value to EEPROM is not supported
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/err.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#include <linux/iio/dac/mcp4725.h>

#define MCP4725_DRV_NAME "mcp4725"

struct mcp4725_data {
	struct i2c_client *client;
	u16 vref_mv;
	u16 dac_value;
};

#ifdef CONFIG_PM_SLEEP
static int mcp4725_suspend(struct device *dev)
{
	u8 outbuf[2];

	outbuf[0] = 0x3 << 4; /* power-down bits, 500 kOhm resistor */
	outbuf[1] = 0;

	return i2c_master_send(to_i2c_client(dev), outbuf, 2);
}

static int mcp4725_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct mcp4725_data *data = iio_priv(indio_dev);
	u8 outbuf[2];

	/* restore previous DAC value */
	outbuf[0] = (data->dac_value >> 8) & 0xf;
	outbuf[1] = data->dac_value & 0xff;

	return i2c_master_send(to_i2c_client(dev), outbuf, 2);
}

static SIMPLE_DEV_PM_OPS(mcp4725_pm_ops, mcp4725_suspend, mcp4725_resume);
#define MCP4725_PM_OPS (&mcp4725_pm_ops)
#else
#define MCP4725_PM_OPS NULL
#endif

static const struct iio_chan_spec mcp4725_channel = {
	.type		= IIO_VOLTAGE,
	.indexed	= 1,
	.output		= 1,
	.channel	= 0,
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
	.scan_type	= IIO_ST('u', 12, 16, 0),
};

static int mcp4725_set_value(struct iio_dev *indio_dev, int val)
{
	struct mcp4725_data *data = iio_priv(indio_dev);
	u8 outbuf[2];
	int ret;

	if (val >= (1 << 12) || val < 0)
		return -EINVAL;

	outbuf[0] = (val >> 8) & 0xf;
	outbuf[1] = val & 0xff;

	ret = i2c_master_send(data->client, outbuf, 2);
	if (ret < 0)
		return ret;
	else if (ret != 2)
		return -EIO;
	else
		return 0;
}

static int mcp4725_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long mask)
{
	struct mcp4725_data *data = iio_priv(indio_dev);
	unsigned long scale_uv;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		*val = data->dac_value;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		scale_uv = (data->vref_mv * 1000) >> 12;
		*val =  scale_uv / 1000000;
		*val2 = scale_uv % 1000000;
		return IIO_VAL_INT_PLUS_MICRO;
	}
	return -EINVAL;
}

static int mcp4725_write_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int val, int val2, long mask)
{
	struct mcp4725_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = mcp4725_set_value(indio_dev, val);
		data->dac_value = val;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static const struct iio_info mcp4725_info = {
	.read_raw = mcp4725_read_raw,
	.write_raw = mcp4725_write_raw,
	.driver_module = THIS_MODULE,
};

static int mcp4725_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct mcp4725_data *data;
	struct iio_dev *indio_dev;
	struct mcp4725_platform_data *platform_data = client->dev.platform_data;
	u8 inbuf[3];
	int err;

	if (!platform_data || !platform_data->vref_mv) {
		dev_err(&client->dev, "invalid platform data");
		err = -EINVAL;
		goto exit;
	}

	indio_dev = iio_device_alloc(sizeof(*data));
	if (indio_dev == NULL) {
		err = -ENOMEM;
		goto exit;
	}
	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client = client;

	indio_dev->dev.parent = &client->dev;
	indio_dev->name = id->name;
	indio_dev->info = &mcp4725_info;
	indio_dev->channels = &mcp4725_channel;
	indio_dev->num_channels = 1;
	indio_dev->modes = INDIO_DIRECT_MODE;

	data->vref_mv = platform_data->vref_mv;

	/* read current DAC value */
	err = i2c_master_recv(client, inbuf, 3);
	if (err < 0) {
		dev_err(&client->dev, "failed to read DAC value");
		goto exit_free_device;
	}
	data->dac_value = (inbuf[1] << 4) | (inbuf[2] >> 4);

	err = iio_device_register(indio_dev);
	if (err)
		goto exit_free_device;

	dev_info(&client->dev, "MCP4725 DAC registered\n");

	return 0;

exit_free_device:
	iio_device_free(indio_dev);
exit:
	return err;
}

static int mcp4725_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);

	iio_device_unregister(indio_dev);
	iio_device_free(indio_dev);

	return 0;
}

static const struct i2c_device_id mcp4725_id[] = {
	{ "mcp4725", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mcp4725_id);

static struct i2c_driver mcp4725_driver = {
	.driver = {
		.name	= MCP4725_DRV_NAME,
		.pm	= MCP4725_PM_OPS,
	},
	.probe		= mcp4725_probe,
	.remove		= mcp4725_remove,
	.id_table	= mcp4725_id,
};
module_i2c_driver(mcp4725_driver);

MODULE_AUTHOR("Peter Meerwald <pmeerw@pmeerw.net>");
MODULE_DESCRIPTION("MCP4725 12-bit DAC");
MODULE_LICENSE("GPL");
