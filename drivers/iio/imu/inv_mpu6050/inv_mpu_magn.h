/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2019 TDK-InvenSense, Inc.
 */

#ifndef INV_MPU_MAGN_H_
#define INV_MPU_MAGN_H_

#include <linux/kernel.h>

#include "inv_mpu_iio.h"

int inv_mpu_magn_probe(struct inv_mpu6050_state *st);

/**
 * inv_mpu_magn_get_scale() - get magnetometer scale value
 * @st: driver internal state
 *
 * Returns IIO data format.
 */
static inline int inv_mpu_magn_get_scale(const struct inv_mpu6050_state *st,
					 const struct iio_chan_spec *chan,
					 int *val, int *val2)
{
	*val = 0;
	*val2 = st->magn_raw_to_gauss[chan->address];
	return IIO_VAL_INT_PLUS_MICRO;
}

int inv_mpu_magn_set_rate(const struct inv_mpu6050_state *st, int fifo_rate);

int inv_mpu_magn_set_orient(struct inv_mpu6050_state *st);

int inv_mpu_magn_read(const struct inv_mpu6050_state *st, int axis, int *val);

#endif		/* INV_MPU_MAGN_H_ */
