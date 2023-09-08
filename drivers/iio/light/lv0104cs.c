// SPDX-License-Identifier: GPL-2.0+
/*
 * lv0104cs.c: LV0104CS Ambient Light Sensor Driver
 *
 * Copyright (C) 2018
 * Author: Jeff LaBundy <jeff@labundy.com>
 *
 * 7-bit I2C slave address: 0x13
 *
 * Link to data sheet: https://www.onsemi.com/pub/Collateral/LV0104CS-D.PDF
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#define LV0104CS_REGVAL_MEASURE		0xE0
#define LV0104CS_REGVAL_SLEEP		0x00

#define LV0104CS_SCALE_0_25X		0
#define LV0104CS_SCALE_1X		1
#define LV0104CS_SCALE_2X		2
#define LV0104CS_SCALE_8X		3
#define LV0104CS_SCALE_SHIFT		3

#define LV0104CS_INTEG_12_5MS		0
#define LV0104CS_INTEG_100MS		1
#define LV0104CS_INTEG_200MS		2
#define LV0104CS_INTEG_SHIFT		1

#define LV0104CS_CALIBSCALE_UNITY	31

struct lv0104cs_private {
	struct i2c_client *client;
	struct mutex lock;
	u8 calibscale;
	u8 scale;
	u8 int_time;
};

struct lv0104cs_mapping {
	int val;
	int val2;
	u8 regval;
};

static const struct lv0104cs_mapping lv0104cs_calibscales[] = {
	{ 0, 666666, 0x81 },
	{ 0, 800000, 0x82 },
	{ 0, 857142, 0x83 },
	{ 0, 888888, 0x84 },
	{ 0, 909090, 0x85 },
	{ 0, 923076, 0x86 },
	{ 0, 933333, 0x87 },
	{ 0, 941176, 0x88 },
	{ 0, 947368, 0x89 },
	{ 0, 952380, 0x8A },
	{ 0, 956521, 0x8B },
	{ 0, 960000, 0x8C },
	{ 0, 962962, 0x8D },
	{ 0, 965517, 0x8E },
	{ 0, 967741, 0x8F },
	{ 0, 969696, 0x90 },
	{ 0, 971428, 0x91 },
	{ 0, 972972, 0x92 },
	{ 0, 974358, 0x93 },
	{ 0, 975609, 0x94 },
	{ 0, 976744, 0x95 },
	{ 0, 977777, 0x96 },
	{ 0, 978723, 0x97 },
	{ 0, 979591, 0x98 },
	{ 0, 980392, 0x99 },
	{ 0, 981132, 0x9A },
	{ 0, 981818, 0x9B },
	{ 0, 982456, 0x9C },
	{ 0, 983050, 0x9D },
	{ 0, 983606, 0x9E },
	{ 0, 984126, 0x9F },
	{ 1, 0, 0x80 },
	{ 1, 16129, 0xBF },
	{ 1, 16666, 0xBE },
	{ 1, 17241, 0xBD },
	{ 1, 17857, 0xBC },
	{ 1, 18518, 0xBB },
	{ 1, 19230, 0xBA },
	{ 1, 20000, 0xB9 },
	{ 1, 20833, 0xB8 },
	{ 1, 21739, 0xB7 },
	{ 1, 22727, 0xB6 },
	{ 1, 23809, 0xB5 },
	{ 1, 24999, 0xB4 },
	{ 1, 26315, 0xB3 },
	{ 1, 27777, 0xB2 },
	{ 1, 29411, 0xB1 },
	{ 1, 31250, 0xB0 },
	{ 1, 33333, 0xAF },
	{ 1, 35714, 0xAE },
	{ 1, 38461, 0xAD },
	{ 1, 41666, 0xAC },
	{ 1, 45454, 0xAB },
	{ 1, 50000, 0xAA },
	{ 1, 55555, 0xA9 },
	{ 1, 62500, 0xA8 },
	{ 1, 71428, 0xA7 },
	{ 1, 83333, 0xA6 },
	{ 1, 100000, 0xA5 },
	{ 1, 125000, 0xA4 },
	{ 1, 166666, 0xA3 },
	{ 1, 250000, 0xA2 },
	{ 1, 500000, 0xA1 },
};

static const struct lv0104cs_mapping lv0104cs_scales[] = {
	{ 0, 250000, LV0104CS_SCALE_0_25X << LV0104CS_SCALE_SHIFT },
	{ 1, 0, LV0104CS_SCALE_1X << LV0104CS_SCALE_SHIFT },
	{ 2, 0, LV0104CS_SCALE_2X << LV0104CS_SCALE_SHIFT },
	{ 8, 0, LV0104CS_SCALE_8X << LV0104CS_SCALE_SHIFT },
};

static const struct lv0104cs_mapping lv0104cs_int_times[] = {
	{ 0, 12500, LV0104CS_INTEG_12_5MS << LV0104CS_INTEG_SHIFT },
	{ 0, 100000, LV0104CS_INTEG_100MS << LV0104CS_INTEG_SHIFT },
	{ 0, 200000, LV0104CS_INTEG_200MS << LV0104CS_INTEG_SHIFT },
};

static int lv0104cs_write_reg(struct i2c_client *client, u8 regval)
{
	int ret;

	ret = i2c_master_send(client, (char *)&regval, sizeof(regval));
	if (ret < 0)
		return ret;
	if (ret != sizeof(regval))
		return -EIO;

	return 0;
}

static int lv0104cs_read_adc(struct i2c_client *client, u16 *adc_output)
{
	__be16 regval;
	int ret;

	ret = i2c_master_recv(client, (char *)&regval, sizeof(regval));
	if (ret < 0)
		return ret;
	if (ret != sizeof(regval))
		return -EIO;

	*adc_output = be16_to_cpu(regval);

	return 0;
}

static int lv0104cs_get_lux(struct lv0104cs_private *lv0104cs,
				int *val, int *val2)
{
	u8 regval = LV0104CS_REGVAL_MEASURE;
	u16 adc_output;
	int ret;

	regval |= lv0104cs_scales[lv0104cs->scale].regval;
	regval |= lv0104cs_int_times[lv0104cs->int_time].regval;
	ret = lv0104cs_write_reg(lv0104cs->client, regval);
	if (ret)
		return ret;

	/* wait for integration time to pass (with margin) */
	switch (lv0104cs->int_time) {
	case LV0104CS_INTEG_12_5MS:
		msleep(50);
		break;

	case LV0104CS_INTEG_100MS:
		msleep(150);
		break;

	case LV0104CS_INTEG_200MS:
		msleep(250);
		break;

	default:
		return -EINVAL;
	}

	ret = lv0104cs_read_adc(lv0104cs->client, &adc_output);
	if (ret)
		return ret;

	ret = lv0104cs_write_reg(lv0104cs->client, LV0104CS_REGVAL_SLEEP);
	if (ret)
		return ret;

	/* convert ADC output to lux */
	switch (lv0104cs->scale) {
	case LV0104CS_SCALE_0_25X:
		*val = adc_output * 4;
		*val2 = 0;
		return 0;

	case LV0104CS_SCALE_1X:
		*val = adc_output;
		*val2 = 0;
		return 0;

	case LV0104CS_SCALE_2X:
		*val = adc_output / 2;
		*val2 = (adc_output % 2) * 500000;
		return 0;

	case LV0104CS_SCALE_8X:
		*val = adc_output / 8;
		*val2 = (adc_output % 8) * 125000;
		return 0;

	default:
		return -EINVAL;
	}
}

static int lv0104cs_read_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan,
				int *val, int *val2, long mask)
{
	struct lv0104cs_private *lv0104cs = iio_priv(indio_dev);
	int ret;

	if (chan->type != IIO_LIGHT)
		return -EINVAL;

	mutex_lock(&lv0104cs->lock);

	switch (mask) {
	case IIO_CHAN_INFO_PROCESSED:
		ret = lv0104cs_get_lux(lv0104cs, val, val2);
		if (ret)
			goto err_mutex;
		ret = IIO_VAL_INT_PLUS_MICRO;
		break;

	case IIO_CHAN_INFO_CALIBSCALE:
		*val = lv0104cs_calibscales[lv0104cs->calibscale].val;
		*val2 = lv0104cs_calibscales[lv0104cs->calibscale].val2;
		ret = IIO_VAL_INT_PLUS_MICRO;
		break;

	case IIO_CHAN_INFO_SCALE:
		*val = lv0104cs_scales[lv0104cs->scale].val;
		*val2 = lv0104cs_scales[lv0104cs->scale].val2;
		ret = IIO_VAL_INT_PLUS_MICRO;
		break;

	case IIO_CHAN_INFO_INT_TIME:
		*val = lv0104cs_int_times[lv0104cs->int_time].val;
		*val2 = lv0104cs_int_times[lv0104cs->int_time].val2;
		ret = IIO_VAL_INT_PLUS_MICRO;
		break;

	default:
		ret = -EINVAL;
	}

err_mutex:
	mutex_unlock(&lv0104cs->lock);

	return ret;
}

static int lv0104cs_set_calibscale(struct lv0104cs_private *lv0104cs,
				int val, int val2)
{
	int calibscale = val * 1000000 + val2;
	int floor, ceil, mid;
	int ret, i, index;

	/* round to nearest quantized calibscale (sensitivity) */
	for (i = 0; i < ARRAY_SIZE(lv0104cs_calibscales) - 1; i++) {
		floor = lv0104cs_calibscales[i].val * 1000000
				+ lv0104cs_calibscales[i].val2;
		ceil = lv0104cs_calibscales[i + 1].val * 1000000
				+ lv0104cs_calibscales[i + 1].val2;
		mid = (floor + ceil) / 2;

		/* round down */
		if (calibscale >= floor && calibscale < mid) {
			index = i;
			break;
		}

		/* round up */
		if (calibscale >= mid && calibscale <= ceil) {
			index = i + 1;
			break;
		}
	}

	if (i == ARRAY_SIZE(lv0104cs_calibscales) - 1)
		return -EINVAL;

	mutex_lock(&lv0104cs->lock);

	/* set calibscale (sensitivity) */
	ret = lv0104cs_write_reg(lv0104cs->client,
			lv0104cs_calibscales[index].regval);
	if (ret)
		goto err_mutex;

	lv0104cs->calibscale = index;

err_mutex:
	mutex_unlock(&lv0104cs->lock);

	return ret;
}

static int lv0104cs_set_scale(struct lv0104cs_private *lv0104cs,
				int val, int val2)
{
	int i;

	/* hard matching */
	for (i = 0; i < ARRAY_SIZE(lv0104cs_scales); i++) {
		if (val != lv0104cs_scales[i].val)
			continue;

		if (val2 == lv0104cs_scales[i].val2)
			break;
	}

	if (i == ARRAY_SIZE(lv0104cs_scales))
		return -EINVAL;

	mutex_lock(&lv0104cs->lock);
	lv0104cs->scale = i;
	mutex_unlock(&lv0104cs->lock);

	return 0;
}

static int lv0104cs_set_int_time(struct lv0104cs_private *lv0104cs,
				int val, int val2)
{
	int i;

	/* hard matching */
	for (i = 0; i < ARRAY_SIZE(lv0104cs_int_times); i++) {
		if (val != lv0104cs_int_times[i].val)
			continue;

		if (val2 == lv0104cs_int_times[i].val2)
			break;
	}

	if (i == ARRAY_SIZE(lv0104cs_int_times))
		return -EINVAL;

	mutex_lock(&lv0104cs->lock);
	lv0104cs->int_time = i;
	mutex_unlock(&lv0104cs->lock);

	return 0;
}

static int lv0104cs_write_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan,
				int val, int val2, long mask)
{
	struct lv0104cs_private *lv0104cs = iio_priv(indio_dev);

	if (chan->type != IIO_LIGHT)
		return -EINVAL;

	switch (mask) {
	case IIO_CHAN_INFO_CALIBSCALE:
		return lv0104cs_set_calibscale(lv0104cs, val, val2);

	case IIO_CHAN_INFO_SCALE:
		return lv0104cs_set_scale(lv0104cs, val, val2);

	case IIO_CHAN_INFO_INT_TIME:
		return lv0104cs_set_int_time(lv0104cs, val, val2);

	default:
		return -EINVAL;
	}
}

static ssize_t lv0104cs_show_calibscale_avail(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	ssize_t len = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(lv0104cs_calibscales); i++) {
		len += scnprintf(buf + len, PAGE_SIZE - len, "%d.%06d ",
				lv0104cs_calibscales[i].val,
				lv0104cs_calibscales[i].val2);
	}

	buf[len - 1] = '\n';

	return len;
}

static ssize_t lv0104cs_show_scale_avail(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	ssize_t len = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(lv0104cs_scales); i++) {
		len += scnprintf(buf + len, PAGE_SIZE - len, "%d.%06d ",
				lv0104cs_scales[i].val,
				lv0104cs_scales[i].val2);
	}

	buf[len - 1] = '\n';

	return len;
}

static ssize_t lv0104cs_show_int_time_avail(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	ssize_t len = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(lv0104cs_int_times); i++) {
		len += scnprintf(buf + len, PAGE_SIZE - len, "%d.%06d ",
				lv0104cs_int_times[i].val,
				lv0104cs_int_times[i].val2);
	}

	buf[len - 1] = '\n';

	return len;
}

static IIO_DEVICE_ATTR(calibscale_available, 0444,
				lv0104cs_show_calibscale_avail, NULL, 0);
static IIO_DEVICE_ATTR(scale_available, 0444,
				lv0104cs_show_scale_avail, NULL, 0);
static IIO_DEV_ATTR_INT_TIME_AVAIL(lv0104cs_show_int_time_avail);

static struct attribute *lv0104cs_attributes[] = {
	&iio_dev_attr_calibscale_available.dev_attr.attr,
	&iio_dev_attr_scale_available.dev_attr.attr,
	&iio_dev_attr_integration_time_available.dev_attr.attr,
	NULL
};

static const struct attribute_group lv0104cs_attribute_group = {
	.attrs = lv0104cs_attributes,
};

static const struct iio_info lv0104cs_info = {
	.attrs = &lv0104cs_attribute_group,
	.read_raw = &lv0104cs_read_raw,
	.write_raw = &lv0104cs_write_raw,
};

static const struct iio_chan_spec lv0104cs_channels[] = {
	{
		.type = IIO_LIGHT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED) |
				      BIT(IIO_CHAN_INFO_CALIBSCALE) |
				      BIT(IIO_CHAN_INFO_SCALE) |
				      BIT(IIO_CHAN_INFO_INT_TIME),
	},
};

static int lv0104cs_probe(struct i2c_client *client)
{
	struct iio_dev *indio_dev;
	struct lv0104cs_private *lv0104cs;
	int ret;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*lv0104cs));
	if (!indio_dev)
		return -ENOMEM;

	lv0104cs = iio_priv(indio_dev);

	i2c_set_clientdata(client, lv0104cs);
	lv0104cs->client = client;

	mutex_init(&lv0104cs->lock);

	lv0104cs->calibscale = LV0104CS_CALIBSCALE_UNITY;
	lv0104cs->scale = LV0104CS_SCALE_1X;
	lv0104cs->int_time = LV0104CS_INTEG_200MS;

	ret = lv0104cs_write_reg(lv0104cs->client,
			lv0104cs_calibscales[LV0104CS_CALIBSCALE_UNITY].regval);
	if (ret)
		return ret;

	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = lv0104cs_channels;
	indio_dev->num_channels = ARRAY_SIZE(lv0104cs_channels);
	indio_dev->name = client->name;
	indio_dev->info = &lv0104cs_info;

	return devm_iio_device_register(&client->dev, indio_dev);
}

static const struct i2c_device_id lv0104cs_id[] = {
	{ "lv0104cs", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, lv0104cs_id);

static struct i2c_driver lv0104cs_i2c_driver = {
	.driver = {
		.name	= "lv0104cs",
	},
	.id_table	= lv0104cs_id,
	.probe_new	= lv0104cs_probe,
};
module_i2c_driver(lv0104cs_i2c_driver);

MODULE_AUTHOR("Jeff LaBundy <jeff@labundy.com>");
MODULE_DESCRIPTION("LV0104CS Ambient Light Sensor Driver");
MODULE_LICENSE("GPL");
