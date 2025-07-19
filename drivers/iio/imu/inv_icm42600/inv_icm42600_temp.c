// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2020 Invensense, Inc.
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/iio/iio.h>

#include "inv_icm42600.h"
#include "inv_icm42600_temp.h"

static int inv_icm42600_temp_read(struct inv_icm42600_state *st, s16 *temp)
{
	struct device *dev = regmap_get_device(st->map);
	__be16 *raw;
	int ret;

	pm_runtime_get_sync(dev);
	mutex_lock(&st->lock);

	ret = inv_icm42600_set_temp_conf(st, true, NULL);
	if (ret)
		goto exit;

	raw = (__be16 *)&st->buffer[0];
	ret = regmap_bulk_read(st->map, INV_ICM42600_REG_TEMP_DATA, raw, sizeof(*raw));
	if (ret)
		goto exit;

	*temp = (s16)be16_to_cpup(raw);
	if (*temp == INV_ICM42600_DATA_INVALID)
		ret = -EINVAL;

exit:
	mutex_unlock(&st->lock);
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	return ret;
}

int inv_icm42600_temp_read_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int *val, int *val2, long mask)
{
	struct inv_icm42600_state *st = iio_device_get_drvdata(indio_dev);
	s16 temp;
	int ret;

	if (chan->type != IIO_TEMP)
		return -EINVAL;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (!iio_device_claim_direct(indio_dev))
			return -EBUSY;
		ret = inv_icm42600_temp_read(st, &temp);
		iio_device_release_direct(indio_dev);
		if (ret)
			return ret;
		*val = temp;
		return IIO_VAL_INT;
	/*
	 * T°C = (temp / 132.48) + 25
	 * Tm°C = 1000 * ((temp / 132.48) + 25)
	 * Tm°C = 7.548309 * temp + 25000
	 * Tm°C = (temp + 3312) * 7.548309
	 * scale: 100000 / 13248 ~= 7.548309
	 * offset: 3312
	 */
	case IIO_CHAN_INFO_SCALE:
		*val = 7;
		*val2 = 548309;
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_OFFSET:
		*val = 3312;
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}
