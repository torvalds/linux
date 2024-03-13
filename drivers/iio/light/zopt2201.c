// SPDX-License-Identifier: GPL-2.0-only
/*
 * zopt2201.c - Support for IDT ZOPT2201 ambient light and UV B sensor
 *
 * Copyright 2017 Peter Meerwald-Stadler <pmeerw@pmeerw.net>
 *
 * Datasheet: https://www.idt.com/document/dst/zopt2201-datasheet
 * 7-bit I2C slave addresses 0x53 (default) or 0x52 (programmed)
 *
 * TODO: interrupt support, ALS/UVB raw mode
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/delay.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#include <asm/unaligned.h>

#define ZOPT2201_DRV_NAME "zopt2201"

/* Registers */
#define ZOPT2201_MAIN_CTRL		0x00
#define ZOPT2201_LS_MEAS_RATE		0x04
#define ZOPT2201_LS_GAIN		0x05
#define ZOPT2201_PART_ID		0x06
#define ZOPT2201_MAIN_STATUS		0x07
#define ZOPT2201_ALS_DATA		0x0d /* LSB first, 13 to 20 bits */
#define ZOPT2201_UVB_DATA		0x10 /* LSB first, 13 to 20 bits */
#define ZOPT2201_UV_COMP_DATA		0x13 /* LSB first, 13 to 20 bits */
#define ZOPT2201_COMP_DATA		0x16 /* LSB first, 13 to 20 bits */
#define ZOPT2201_INT_CFG		0x19
#define ZOPT2201_INT_PST		0x1a

#define ZOPT2201_MAIN_CTRL_LS_MODE	BIT(3) /* 0 .. ALS, 1 .. UV B */
#define ZOPT2201_MAIN_CTRL_LS_EN	BIT(1)

/* Values for ZOPT2201_LS_MEAS_RATE resolution / bit width */
#define ZOPT2201_MEAS_RES_20BIT		0 /* takes 400 ms */
#define ZOPT2201_MEAS_RES_19BIT		1 /* takes 200 ms */
#define ZOPT2201_MEAS_RES_18BIT		2 /* takes 100 ms, default */
#define ZOPT2201_MEAS_RES_17BIT		3 /* takes 50 ms */
#define ZOPT2201_MEAS_RES_16BIT		4 /* takes 25 ms */
#define ZOPT2201_MEAS_RES_13BIT		5 /* takes 3.125 ms */
#define ZOPT2201_MEAS_RES_SHIFT		4

/* Values for ZOPT2201_LS_MEAS_RATE measurement rate */
#define ZOPT2201_MEAS_FREQ_25MS		0
#define ZOPT2201_MEAS_FREQ_50MS		1
#define ZOPT2201_MEAS_FREQ_100MS	2 /* default */
#define ZOPT2201_MEAS_FREQ_200MS	3
#define ZOPT2201_MEAS_FREQ_500MS	4
#define ZOPT2201_MEAS_FREQ_1000MS	5
#define ZOPT2201_MEAS_FREQ_2000MS	6

/* Values for ZOPT2201_LS_GAIN */
#define ZOPT2201_LS_GAIN_1		0
#define ZOPT2201_LS_GAIN_3		1
#define ZOPT2201_LS_GAIN_6		2
#define ZOPT2201_LS_GAIN_9		3
#define ZOPT2201_LS_GAIN_18		4

/* Values for ZOPT2201_MAIN_STATUS */
#define ZOPT2201_MAIN_STATUS_POWERON	BIT(5)
#define ZOPT2201_MAIN_STATUS_INT	BIT(4)
#define ZOPT2201_MAIN_STATUS_DRDY	BIT(3)

#define ZOPT2201_PART_NUMBER		0xb2

struct zopt2201_data {
	struct i2c_client *client;
	struct mutex lock;
	u8 gain;
	u8 res;
	u8 rate;
};

static const struct {
	unsigned int gain; /* gain factor */
	unsigned int scale; /* micro lux per count */
} zopt2201_gain_als[] = {
	{  1, 19200000 },
	{  3,  6400000 },
	{  6,  3200000 },
	{  9,  2133333 },
	{ 18,  1066666 },
};

static const struct {
	unsigned int gain; /* gain factor */
	unsigned int scale; /* micro W/m2 per count */
} zopt2201_gain_uvb[] = {
	{  1, 460800 },
	{  3, 153600 },
	{  6,  76800 },
	{  9,  51200 },
	{ 18,  25600 },
};

static const struct {
	unsigned int bits; /* sensor resolution in bits */
	unsigned long us; /* measurement time in micro seconds */
} zopt2201_resolution[] = {
	{ 20, 400000 },
	{ 19, 200000 },
	{ 18, 100000 },
	{ 17,  50000 },
	{ 16,  25000 },
	{ 13,   3125 },
};

static const struct {
	unsigned int scale, uscale; /* scale factor as integer + micro */
	u8 gain; /* gain register value */
	u8 res; /* resolution register value */
} zopt2201_scale_als[] = {
	{ 19, 200000, 0, 5 },
	{  6, 400000, 1, 5 },
	{  3, 200000, 2, 5 },
	{  2, 400000, 0, 4 },
	{  2, 133333, 3, 5 },
	{  1, 200000, 0, 3 },
	{  1,  66666, 4, 5 },
	{  0, 800000, 1, 4 },
	{  0, 600000, 0, 2 },
	{  0, 400000, 2, 4 },
	{  0, 300000, 0, 1 },
	{  0, 266666, 3, 4 },
	{  0, 200000, 2, 3 },
	{  0, 150000, 0, 0 },
	{  0, 133333, 4, 4 },
	{  0, 100000, 2, 2 },
	{  0,  66666, 4, 3 },
	{  0,  50000, 2, 1 },
	{  0,  33333, 4, 2 },
	{  0,  25000, 2, 0 },
	{  0,  16666, 4, 1 },
	{  0,   8333, 4, 0 },
};

static const struct {
	unsigned int scale, uscale; /* scale factor as integer + micro */
	u8 gain; /* gain register value */
	u8 res; /* resolution register value */
} zopt2201_scale_uvb[] = {
	{ 0, 460800, 0, 5 },
	{ 0, 153600, 1, 5 },
	{ 0,  76800, 2, 5 },
	{ 0,  57600, 0, 4 },
	{ 0,  51200, 3, 5 },
	{ 0,  28800, 0, 3 },
	{ 0,  25600, 4, 5 },
	{ 0,  19200, 1, 4 },
	{ 0,  14400, 0, 2 },
	{ 0,   9600, 2, 4 },
	{ 0,   7200, 0, 1 },
	{ 0,   6400, 3, 4 },
	{ 0,   4800, 2, 3 },
	{ 0,   3600, 0, 0 },
	{ 0,   3200, 4, 4 },
	{ 0,   2400, 2, 2 },
	{ 0,   1600, 4, 3 },
	{ 0,   1200, 2, 1 },
	{ 0,    800, 4, 2 },
	{ 0,    600, 2, 0 },
	{ 0,    400, 4, 1 },
	{ 0,    200, 4, 0 },
};

static int zopt2201_enable_mode(struct zopt2201_data *data, bool uvb_mode)
{
	u8 out = ZOPT2201_MAIN_CTRL_LS_EN;

	if (uvb_mode)
		out |= ZOPT2201_MAIN_CTRL_LS_MODE;

	return i2c_smbus_write_byte_data(data->client, ZOPT2201_MAIN_CTRL, out);
}

static int zopt2201_read(struct zopt2201_data *data, u8 reg)
{
	struct i2c_client *client = data->client;
	int tries = 10;
	u8 buf[3];
	int ret;

	mutex_lock(&data->lock);
	ret = zopt2201_enable_mode(data, reg == ZOPT2201_UVB_DATA);
	if (ret < 0)
		goto fail;

	while (tries--) {
		unsigned long t = zopt2201_resolution[data->res].us;

		if (t <= 20000)
			usleep_range(t, t + 1000);
		else
			msleep(t / 1000);
		ret = i2c_smbus_read_byte_data(client, ZOPT2201_MAIN_STATUS);
		if (ret < 0)
			goto fail;
		if (ret & ZOPT2201_MAIN_STATUS_DRDY)
			break;
	}

	if (tries < 0) {
		ret = -ETIMEDOUT;
		goto fail;
	}

	ret = i2c_smbus_read_i2c_block_data(client, reg, sizeof(buf), buf);
	if (ret < 0)
		goto fail;

	ret = i2c_smbus_write_byte_data(client, ZOPT2201_MAIN_CTRL, 0x00);
	if (ret < 0)
		goto fail;
	mutex_unlock(&data->lock);

	return get_unaligned_le24(&buf[0]);

fail:
	mutex_unlock(&data->lock);
	return ret;
}

static const struct iio_chan_spec zopt2201_channels[] = {
	{
		.type = IIO_LIGHT,
		.address = ZOPT2201_ALS_DATA,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_INT_TIME),
	},
	{
		.type = IIO_INTENSITY,
		.modified = 1,
		.channel2 = IIO_MOD_LIGHT_UV,
		.address = ZOPT2201_UVB_DATA,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_INT_TIME),
	},
	{
		.type = IIO_UVINDEX,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),
	},
};

static int zopt2201_read_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan,
				int *val, int *val2, long mask)
{
	struct zopt2201_data *data = iio_priv(indio_dev);
	u64 tmp;
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = zopt2201_read(data, chan->address);
		if (ret < 0)
			return ret;
		*val = ret;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_PROCESSED:
		ret = zopt2201_read(data, ZOPT2201_UVB_DATA);
		if (ret < 0)
			return ret;
		*val = ret * 18 *
			(1 << (20 - zopt2201_resolution[data->res].bits)) /
			zopt2201_gain_uvb[data->gain].gain;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		switch (chan->address) {
		case ZOPT2201_ALS_DATA:
			*val = zopt2201_gain_als[data->gain].scale;
			break;
		case ZOPT2201_UVB_DATA:
			*val = zopt2201_gain_uvb[data->gain].scale;
			break;
		default:
			return -EINVAL;
		}

		*val2 = 1000000;
		*val2 *= (1 << (zopt2201_resolution[data->res].bits - 13));
		tmp = div_s64(*val * 1000000ULL, *val2);
		*val = div_s64_rem(tmp, 1000000, val2);

		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_INT_TIME:
		*val = 0;
		*val2 = zopt2201_resolution[data->res].us;
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

static int zopt2201_set_resolution(struct zopt2201_data *data, u8 res)
{
	int ret;

	ret = i2c_smbus_write_byte_data(data->client, ZOPT2201_LS_MEAS_RATE,
					(res << ZOPT2201_MEAS_RES_SHIFT) |
					data->rate);
	if (ret < 0)
		return ret;

	data->res = res;

	return 0;
}

static int zopt2201_write_resolution(struct zopt2201_data *data,
				     int val, int val2)
{
	int i, ret;

	if (val != 0)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(zopt2201_resolution); i++)
		if (val2 == zopt2201_resolution[i].us) {
			mutex_lock(&data->lock);
			ret = zopt2201_set_resolution(data, i);
			mutex_unlock(&data->lock);
			return ret;
		}

	return -EINVAL;
}

static int zopt2201_set_gain(struct zopt2201_data *data, u8 gain)
{
	int ret;

	ret = i2c_smbus_write_byte_data(data->client, ZOPT2201_LS_GAIN, gain);
	if (ret < 0)
		return ret;

	data->gain = gain;

	return 0;
}

static int zopt2201_write_scale_als_by_idx(struct zopt2201_data *data, int idx)
{
	int ret;

	mutex_lock(&data->lock);
	ret = zopt2201_set_resolution(data, zopt2201_scale_als[idx].res);
	if (ret < 0)
		goto unlock;

	ret = zopt2201_set_gain(data, zopt2201_scale_als[idx].gain);

unlock:
	mutex_unlock(&data->lock);
	return ret;
}

static int zopt2201_write_scale_als(struct zopt2201_data *data,
				     int val, int val2)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(zopt2201_scale_als); i++)
		if (val == zopt2201_scale_als[i].scale &&
		    val2 == zopt2201_scale_als[i].uscale) {
			return zopt2201_write_scale_als_by_idx(data, i);
		}

	return -EINVAL;
}

static int zopt2201_write_scale_uvb_by_idx(struct zopt2201_data *data, int idx)
{
	int ret;

	mutex_lock(&data->lock);
	ret = zopt2201_set_resolution(data, zopt2201_scale_als[idx].res);
	if (ret < 0)
		goto unlock;

	ret = zopt2201_set_gain(data, zopt2201_scale_als[idx].gain);

unlock:
	mutex_unlock(&data->lock);
	return ret;
}

static int zopt2201_write_scale_uvb(struct zopt2201_data *data,
				     int val, int val2)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(zopt2201_scale_uvb); i++)
		if (val == zopt2201_scale_uvb[i].scale &&
		    val2 == zopt2201_scale_uvb[i].uscale)
			return zopt2201_write_scale_uvb_by_idx(data, i);

	return -EINVAL;
}

static int zopt2201_write_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int val, int val2, long mask)
{
	struct zopt2201_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_INT_TIME:
		return zopt2201_write_resolution(data, val, val2);
	case IIO_CHAN_INFO_SCALE:
		switch (chan->address) {
		case ZOPT2201_ALS_DATA:
			return zopt2201_write_scale_als(data, val, val2);
		case ZOPT2201_UVB_DATA:
			return zopt2201_write_scale_uvb(data, val, val2);
		default:
			return -EINVAL;
		}
	}

	return -EINVAL;
}

static ssize_t zopt2201_show_int_time_available(struct device *dev,
						struct device_attribute *attr,
						char *buf)
{
	size_t len = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(zopt2201_resolution); i++)
		len += scnprintf(buf + len, PAGE_SIZE - len, "0.%06lu ",
				 zopt2201_resolution[i].us);
	buf[len - 1] = '\n';

	return len;
}

static IIO_DEV_ATTR_INT_TIME_AVAIL(zopt2201_show_int_time_available);

static ssize_t zopt2201_show_als_scale_avail(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	ssize_t len = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(zopt2201_scale_als); i++)
		len += scnprintf(buf + len, PAGE_SIZE - len, "%d.%06u ",
				 zopt2201_scale_als[i].scale,
				 zopt2201_scale_als[i].uscale);
	buf[len - 1] = '\n';

	return len;
}

static ssize_t zopt2201_show_uvb_scale_avail(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	ssize_t len = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(zopt2201_scale_uvb); i++)
		len += scnprintf(buf + len, PAGE_SIZE - len, "%d.%06u ",
				 zopt2201_scale_uvb[i].scale,
				 zopt2201_scale_uvb[i].uscale);
	buf[len - 1] = '\n';

	return len;
}

static IIO_DEVICE_ATTR(in_illuminance_scale_available, 0444,
		       zopt2201_show_als_scale_avail, NULL, 0);
static IIO_DEVICE_ATTR(in_intensity_uv_scale_available, 0444,
		       zopt2201_show_uvb_scale_avail, NULL, 0);

static struct attribute *zopt2201_attributes[] = {
	&iio_dev_attr_integration_time_available.dev_attr.attr,
	&iio_dev_attr_in_illuminance_scale_available.dev_attr.attr,
	&iio_dev_attr_in_intensity_uv_scale_available.dev_attr.attr,
	NULL
};

static const struct attribute_group zopt2201_attribute_group = {
	.attrs = zopt2201_attributes,
};

static const struct iio_info zopt2201_info = {
	.read_raw = zopt2201_read_raw,
	.write_raw = zopt2201_write_raw,
	.attrs = &zopt2201_attribute_group,
};

static int zopt2201_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct zopt2201_data *data;
	struct iio_dev *indio_dev;
	int ret;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_READ_I2C_BLOCK))
		return -EOPNOTSUPP;

	ret = i2c_smbus_read_byte_data(client, ZOPT2201_PART_ID);
	if (ret < 0)
		return ret;
	if (ret != ZOPT2201_PART_NUMBER)
		return -ENODEV;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client = client;
	mutex_init(&data->lock);

	indio_dev->info = &zopt2201_info;
	indio_dev->channels = zopt2201_channels;
	indio_dev->num_channels = ARRAY_SIZE(zopt2201_channels);
	indio_dev->name = ZOPT2201_DRV_NAME;
	indio_dev->modes = INDIO_DIRECT_MODE;

	data->rate = ZOPT2201_MEAS_FREQ_100MS;
	ret = zopt2201_set_resolution(data, ZOPT2201_MEAS_RES_18BIT);
	if (ret < 0)
		return ret;

	ret = zopt2201_set_gain(data, ZOPT2201_LS_GAIN_3);
	if (ret < 0)
		return ret;

	return devm_iio_device_register(&client->dev, indio_dev);
}

static const struct i2c_device_id zopt2201_id[] = {
	{ "zopt2201", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, zopt2201_id);

static struct i2c_driver zopt2201_driver = {
	.driver = {
		.name   = ZOPT2201_DRV_NAME,
	},
	.probe  = zopt2201_probe,
	.id_table = zopt2201_id,
};

module_i2c_driver(zopt2201_driver);

MODULE_AUTHOR("Peter Meerwald-Stadler <pmeerw@pmeerw.net>");
MODULE_DESCRIPTION("IDT ZOPT2201 ambient light and UV B sensor driver");
MODULE_LICENSE("GPL");
