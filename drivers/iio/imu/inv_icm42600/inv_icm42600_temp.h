/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2020 Invensense, Inc.
 */

#ifndef INV_ICM42600_TEMP_H_
#define INV_ICM42600_TEMP_H_

#include <linux/iio/iio.h>

#define INV_ICM42600_TEMP_CHAN(_index)					\
	{								\
		.type = IIO_TEMP,					\
		.info_mask_separate =					\
			BIT(IIO_CHAN_INFO_RAW) |			\
			BIT(IIO_CHAN_INFO_OFFSET) |			\
			BIT(IIO_CHAN_INFO_SCALE),			\
		.scan_index = _index,					\
		.scan_type = {						\
			.sign = 's',					\
			.realbits = 16,					\
			.storagebits = 16,				\
		},							\
	}

int inv_icm42600_temp_read_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int *val, int *val2, long mask);

#endif
