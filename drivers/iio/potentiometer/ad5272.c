// SPDX-License-Identifier: GPL-2.0+
/*
 * Analog Devices AD5272 digital potentiometer driver
 * Copyright (C) 2018 Phil Reid <preid@electromag.com.au>
 *
 * Datasheet: https://www.analog.com/media/en/technical-documentation/data-sheets/AD5272_5274.pdf
 *
 * DEVID	#Wipers	#Positions	Resistor Opts (kOhm)	i2c address
 * ad5272	1	1024		20, 50, 100		01011xx
 * ad5274	1	256		20, 100			01011xx
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/iio/iio.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>

#define  AD5272_RDAC_WR  1
#define  AD5272_RDAC_RD  2
#define  AD5272_RESET    4
#define  AD5272_CTL      7

#define  AD5272_RDAC_WR_EN  BIT(1)

struct ad5272_cfg {
	int max_pos;
	int kohms;
	int shift;
};

enum ad5272_type {
	AD5272_020,
	AD5272_050,
	AD5272_100,
	AD5274_020,
	AD5274_100,
};

static const struct ad5272_cfg ad5272_cfg[] = {
	[AD5272_020] = { .max_pos = 1024, .kohms = 20 },
	[AD5272_050] = { .max_pos = 1024, .kohms = 50 },
	[AD5272_100] = { .max_pos = 1024, .kohms = 100 },
	[AD5274_020] = { .max_pos = 256,  .kohms = 20,  .shift = 2 },
	[AD5274_100] = { .max_pos = 256,  .kohms = 100, .shift = 2 },
};

struct ad5272_data {
	struct i2c_client       *client;
	struct mutex            lock;
	const struct ad5272_cfg *cfg;
	u8                      buf[2] __aligned(IIO_DMA_MINALIGN);
};

static const struct iio_chan_spec ad5272_channel = {
	.type = IIO_RESISTANCE,
	.output = 1,
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
};

static int ad5272_write(struct ad5272_data *data, int reg, int val)
{
	int ret;

	data->buf[0] = (reg << 2) | ((val >> 8) & 0x3);
	data->buf[1] = (u8)val;

	mutex_lock(&data->lock);
	ret = i2c_master_send(data->client, data->buf, sizeof(data->buf));
	mutex_unlock(&data->lock);
	return ret < 0 ? ret : 0;
}

static int ad5272_read(struct ad5272_data *data, int reg, int *val)
{
	int ret;

	data->buf[0] = reg << 2;
	data->buf[1] = 0;

	mutex_lock(&data->lock);
	ret = i2c_master_send(data->client, data->buf, sizeof(data->buf));
	if (ret < 0)
		goto error;

	ret = i2c_master_recv(data->client, data->buf, sizeof(data->buf));
	if (ret < 0)
		goto error;

	*val = ((data->buf[0] & 0x3) << 8) | data->buf[1];
	ret = 0;
error:
	mutex_unlock(&data->lock);
	return ret;
}

static int ad5272_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long mask)
{
	struct ad5272_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW: {
		ret = ad5272_read(data, AD5272_RDAC_RD, val);
		*val = *val >> data->cfg->shift;
		return ret ? ret : IIO_VAL_INT;
	}
	case IIO_CHAN_INFO_SCALE:
		*val = 1000 * data->cfg->kohms;
		*val2 = data->cfg->max_pos;
		return IIO_VAL_FRACTIONAL;
	}

	return -EINVAL;
}

static int ad5272_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val, int val2, long mask)
{
	struct ad5272_data *data = iio_priv(indio_dev);

	if (mask != IIO_CHAN_INFO_RAW)
		return -EINVAL;

	if (val >= data->cfg->max_pos || val < 0 || val2)
		return -EINVAL;

	return ad5272_write(data, AD5272_RDAC_WR, val << data->cfg->shift);
}

static const struct iio_info ad5272_info = {
	.read_raw = ad5272_read_raw,
	.write_raw = ad5272_write_raw,
};

static int ad5272_reset(struct ad5272_data *data)
{
	struct gpio_desc *reset_gpio;

	reset_gpio = devm_gpiod_get_optional(&data->client->dev, "reset",
		GPIOD_OUT_HIGH);
	if (IS_ERR(reset_gpio))
		return PTR_ERR(reset_gpio);

	if (reset_gpio) {
		udelay(1);
		gpiod_set_value(reset_gpio, 0);
	} else {
		ad5272_write(data, AD5272_RESET, 0);
	}
	usleep_range(1000, 2000);

	return 0;
}

static int ad5272_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct iio_dev *indio_dev;
	struct ad5272_data *data;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	i2c_set_clientdata(client, indio_dev);

	data = iio_priv(indio_dev);
	data->client = client;
	mutex_init(&data->lock);
	data->cfg = &ad5272_cfg[id->driver_data];

	ret = ad5272_reset(data);
	if (ret)
		return ret;

	ret = ad5272_write(data, AD5272_CTL, AD5272_RDAC_WR_EN);
	if (ret < 0)
		return -ENODEV;

	indio_dev->info = &ad5272_info;
	indio_dev->channels = &ad5272_channel;
	indio_dev->num_channels = 1;
	indio_dev->name = client->name;

	return devm_iio_device_register(dev, indio_dev);
}

static const struct of_device_id ad5272_dt_ids[] = {
	{ .compatible = "adi,ad5272-020", .data = (void *)AD5272_020 },
	{ .compatible = "adi,ad5272-050", .data = (void *)AD5272_050 },
	{ .compatible = "adi,ad5272-100", .data = (void *)AD5272_100 },
	{ .compatible = "adi,ad5274-020", .data = (void *)AD5274_020 },
	{ .compatible = "adi,ad5274-100", .data = (void *)AD5274_100 },
	{}
};
MODULE_DEVICE_TABLE(of, ad5272_dt_ids);

static const struct i2c_device_id ad5272_id[] = {
	{ "ad5272-020", AD5272_020 },
	{ "ad5272-050", AD5272_050 },
	{ "ad5272-100", AD5272_100 },
	{ "ad5274-020", AD5274_020 },
	{ "ad5274-100", AD5274_100 },
	{}
};
MODULE_DEVICE_TABLE(i2c, ad5272_id);

static struct i2c_driver ad5272_driver = {
	.driver = {
		.name	= "ad5272",
		.of_match_table = ad5272_dt_ids,
	},
	.probe		= ad5272_probe,
	.id_table	= ad5272_id,
};

module_i2c_driver(ad5272_driver);

MODULE_AUTHOR("Phil Reid <preid@eletromag.com.au>");
MODULE_DESCRIPTION("AD5272 digital potentiometer");
MODULE_LICENSE("GPL v2");
