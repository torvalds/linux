/*
 * Copyright (c) 2016 Golden Delicious Comp. GmbH&Co. KG
 *	Nikolaus Schaller <hns@goldelico.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/i2c.h>
#include <linux/iio/iio.h>
#include "tsc2007.h"

struct tsc2007_iio {
	struct tsc2007 *ts;
};

#define TSC2007_CHAN_IIO(_chan, _name, _type, _chan_info) \
{ \
	.datasheet_name = _name, \
	.type = _type, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |	\
			BIT(_chan_info), \
	.indexed = 1, \
	.channel = _chan, \
}

static const struct iio_chan_spec tsc2007_iio_channel[] = {
	TSC2007_CHAN_IIO(0, "x", IIO_VOLTAGE, IIO_CHAN_INFO_RAW),
	TSC2007_CHAN_IIO(1, "y", IIO_VOLTAGE, IIO_CHAN_INFO_RAW),
	TSC2007_CHAN_IIO(2, "z1", IIO_VOLTAGE, IIO_CHAN_INFO_RAW),
	TSC2007_CHAN_IIO(3, "z2", IIO_VOLTAGE, IIO_CHAN_INFO_RAW),
	TSC2007_CHAN_IIO(4, "adc", IIO_VOLTAGE, IIO_CHAN_INFO_RAW),
	TSC2007_CHAN_IIO(5, "rt", IIO_VOLTAGE, IIO_CHAN_INFO_RAW), /* Ohms? */
	TSC2007_CHAN_IIO(6, "pen", IIO_PRESSURE, IIO_CHAN_INFO_RAW),
	TSC2007_CHAN_IIO(7, "temp0", IIO_TEMP, IIO_CHAN_INFO_RAW),
	TSC2007_CHAN_IIO(8, "temp1", IIO_TEMP, IIO_CHAN_INFO_RAW),
};

static int tsc2007_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val, int *val2, long mask)
{
	struct tsc2007_iio *iio = iio_priv(indio_dev);
	struct tsc2007 *tsc = iio->ts;
	int adc_chan = chan->channel;
	int ret = 0;

	if (adc_chan >= ARRAY_SIZE(tsc2007_iio_channel))
		return -EINVAL;

	if (mask != IIO_CHAN_INFO_RAW)
		return -EINVAL;

	mutex_lock(&tsc->mlock);

	switch (chan->channel) {
	case 0:
		*val = tsc2007_xfer(tsc, READ_X);
		break;
	case 1:
		*val = tsc2007_xfer(tsc, READ_Y);
		break;
	case 2:
		*val = tsc2007_xfer(tsc, READ_Z1);
		break;
	case 3:
		*val = tsc2007_xfer(tsc, READ_Z2);
		break;
	case 4:
		*val = tsc2007_xfer(tsc, (ADC_ON_12BIT | TSC2007_MEASURE_AUX));
		break;
	case 5: {
		struct ts_event tc;

		tc.x = tsc2007_xfer(tsc, READ_X);
		tc.z1 = tsc2007_xfer(tsc, READ_Z1);
		tc.z2 = tsc2007_xfer(tsc, READ_Z2);
		*val = tsc2007_calculate_resistance(tsc, &tc);
		break;
	}
	case 6:
		*val = tsc2007_is_pen_down(tsc);
		break;
	case 7:
		*val = tsc2007_xfer(tsc,
				    (ADC_ON_12BIT | TSC2007_MEASURE_TEMP0));
		break;
	case 8:
		*val = tsc2007_xfer(tsc,
				    (ADC_ON_12BIT | TSC2007_MEASURE_TEMP1));
		break;
	}

	/* Prepare for next touch reading - power down ADC, enable PENIRQ */
	tsc2007_xfer(tsc, PWRDOWN);

	mutex_unlock(&tsc->mlock);

	ret = IIO_VAL_INT;

	return ret;
}

static const struct iio_info tsc2007_iio_info = {
	.read_raw = tsc2007_read_raw,
	.driver_module = THIS_MODULE,
};

int tsc2007_iio_configure(struct tsc2007 *ts)
{
	struct iio_dev *indio_dev;
	struct tsc2007_iio *iio;
	int error;

	indio_dev = devm_iio_device_alloc(&ts->client->dev, sizeof(*iio));
	if (!indio_dev) {
		dev_err(&ts->client->dev, "iio_device_alloc failed\n");
		return -ENOMEM;
	}

	iio = iio_priv(indio_dev);
	iio->ts = ts;

	indio_dev->name = "tsc2007";
	indio_dev->dev.parent = &ts->client->dev;
	indio_dev->info = &tsc2007_iio_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = tsc2007_iio_channel;
	indio_dev->num_channels = ARRAY_SIZE(tsc2007_iio_channel);

	error = devm_iio_device_register(&ts->client->dev, indio_dev);
	if (error) {
		dev_err(&ts->client->dev,
			"iio_device_register() failed: %d\n", error);
		return error;
	}

	return 0;
}
