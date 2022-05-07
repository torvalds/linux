// SPDX-License-Identifier: GPL-2.0-only
/*
 * mpl115.c - Support for Freescale MPL115A pressure/temperature sensor
 *
 * Copyright (c) 2014 Peter Meerwald <pmeerw@pmeerw.net>
 *
 * TODO: shutdown pin
 */

#include <linux/module.h>
#include <linux/iio/iio.h>
#include <linux/delay.h>

#include "mpl115.h"

#define MPL115_PADC 0x00 /* pressure ADC output value, MSB first, 10 bit */
#define MPL115_TADC 0x02 /* temperature ADC output value, MSB first, 10 bit */
#define MPL115_A0 0x04 /* 12 bit integer, 3 bit fraction */
#define MPL115_B1 0x06 /* 2 bit integer, 13 bit fraction */
#define MPL115_B2 0x08 /* 1 bit integer, 14 bit fraction */
#define MPL115_C12 0x0a /* 0 bit integer, 13 bit fraction */
#define MPL115_CONVERT 0x12 /* convert temperature and pressure */

struct mpl115_data {
	struct device *dev;
	struct mutex lock;
	s16 a0;
	s16 b1, b2;
	s16 c12;
	const struct mpl115_ops *ops;
};

static int mpl115_request(struct mpl115_data *data)
{
	int ret = data->ops->write(data->dev, MPL115_CONVERT, 0);

	if (ret < 0)
		return ret;

	usleep_range(3000, 4000);

	return 0;
}

static int mpl115_comp_pressure(struct mpl115_data *data, int *val, int *val2)
{
	int ret;
	u16 padc, tadc;
	int a1, y1, pcomp;
	unsigned kpa;

	mutex_lock(&data->lock);
	ret = mpl115_request(data);
	if (ret < 0)
		goto done;

	ret = data->ops->read(data->dev, MPL115_PADC);
	if (ret < 0)
		goto done;
	padc = ret >> 6;

	ret = data->ops->read(data->dev, MPL115_TADC);
	if (ret < 0)
		goto done;
	tadc = ret >> 6;

	/* see Freescale AN3785 */
	a1 = data->b1 + ((data->c12 * tadc) >> 11);
	y1 = (data->a0 << 10) + a1 * padc;

	/* compensated pressure with 4 fractional bits */
	pcomp = (y1 + ((data->b2 * (int) tadc) >> 1)) >> 9;

	kpa = pcomp * (115 - 50) / 1023 + (50 << 4);
	*val = kpa >> 4;
	*val2 = (kpa & 15) * (1000000 >> 4);
done:
	mutex_unlock(&data->lock);
	return ret;
}

static int mpl115_read_temp(struct mpl115_data *data)
{
	int ret;

	mutex_lock(&data->lock);
	ret = mpl115_request(data);
	if (ret < 0)
		goto done;
	ret = data->ops->read(data->dev, MPL115_TADC);
done:
	mutex_unlock(&data->lock);
	return ret;
}

static int mpl115_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val, int *val2, long mask)
{
	struct mpl115_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_PROCESSED:
		ret = mpl115_comp_pressure(data, val, val2);
		if (ret < 0)
			return ret;
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_RAW:
		/* temperature -5.35 C / LSB, 472 LSB is 25 C */
		ret = mpl115_read_temp(data);
		if (ret < 0)
			return ret;
		*val = ret >> 6;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_OFFSET:
		*val = -605;
		*val2 = 750000;
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_SCALE:
		*val = -186;
		*val2 = 915888;
		return IIO_VAL_INT_PLUS_MICRO;
	}
	return -EINVAL;
}

static const struct iio_chan_spec mpl115_channels[] = {
	{
		.type = IIO_PRESSURE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),
	},
	{
		.type = IIO_TEMP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type =
			BIT(IIO_CHAN_INFO_OFFSET) | BIT(IIO_CHAN_INFO_SCALE),
	},
};

static const struct iio_info mpl115_info = {
	.read_raw = &mpl115_read_raw,
};

int mpl115_probe(struct device *dev, const char *name,
			const struct mpl115_ops *ops)
{
	struct mpl115_data *data;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	data->dev = dev;
	data->ops = ops;
	mutex_init(&data->lock);

	indio_dev->info = &mpl115_info;
	indio_dev->name = name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = mpl115_channels;
	indio_dev->num_channels = ARRAY_SIZE(mpl115_channels);

	ret = data->ops->init(data->dev);
	if (ret)
		return ret;

	ret = data->ops->read(data->dev, MPL115_A0);
	if (ret < 0)
		return ret;
	data->a0 = ret;
	ret = data->ops->read(data->dev, MPL115_B1);
	if (ret < 0)
		return ret;
	data->b1 = ret;
	ret = data->ops->read(data->dev, MPL115_B2);
	if (ret < 0)
		return ret;
	data->b2 = ret;
	ret = data->ops->read(data->dev, MPL115_C12);
	if (ret < 0)
		return ret;
	data->c12 = ret;

	return devm_iio_device_register(dev, indio_dev);
}
EXPORT_SYMBOL_GPL(mpl115_probe);

MODULE_AUTHOR("Peter Meerwald <pmeerw@pmeerw.net>");
MODULE_DESCRIPTION("Freescale MPL115 pressure/temperature driver");
MODULE_LICENSE("GPL");
