/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2026 InvenSense, Inc.
 */

#ifndef INV_ICM42607_TEMP_H_
#define INV_ICM42607_TEMP_H_

#include <linux/bitops.h>

struct iio_dev;
struct iio_chan_spec;

#define INV_ICM42607_TEMP_CHAN(_index)				\
{								\
	.type = IIO_TEMP,					\
	.info_mask_separate =					\
		BIT(IIO_CHAN_INFO_RAW) |			\
		BIT(IIO_CHAN_INFO_OFFSET) |			\
		BIT(IIO_CHAN_INFO_SCALE),			\
	.info_mask_shared_by_all =				\
		BIT(IIO_CHAN_INFO_SAMP_FREQ),			\
	.info_mask_shared_by_all_available =			\
		BIT(IIO_CHAN_INFO_SAMP_FREQ),			\
	.scan_index = _index,					\
	.scan_type = {						\
		.sign = 's',					\
		.realbits = 16,					\
		.storagebits = 16,				\
		.endianness = IIO_BE,				\
	},							\
}

int inv_icm42607_temp_read_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int *val, int *val2, long mask);

#endif
