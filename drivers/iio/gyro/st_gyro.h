/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * STMicroelectronics gyroscopes driver
 *
 * Copyright 2012-2013 STMicroelectronics Inc.
 *
 * Denis Ciocca <denis.ciocca@st.com>
 * v. 1.0.0
 */

#ifndef ST_GYRO_H
#define ST_GYRO_H

#include <linux/types.h>
#include <linux/iio/common/st_sensors.h>

#define L3G4200D_GYRO_DEV_NAME		"l3g4200d"
#define LSM330D_GYRO_DEV_NAME		"lsm330d_gyro"
#define LSM330DL_GYRO_DEV_NAME		"lsm330dl_gyro"
#define LSM330DLC_GYRO_DEV_NAME		"lsm330dlc_gyro"
#define L3GD20_GYRO_DEV_NAME		"l3gd20"
#define L3GD20H_GYRO_DEV_NAME		"l3gd20h"
#define L3G4IS_GYRO_DEV_NAME		"l3g4is_ui"
#define LSM330_GYRO_DEV_NAME		"lsm330_gyro"
#define LSM9DS0_GYRO_DEV_NAME		"lsm9ds0_gyro"

/**
 * struct st_sensors_platform_data - gyro platform data
 * @drdy_int_pin: DRDY on gyros is available only on INT2 pin.
 */
static const struct st_sensors_platform_data gyro_pdata = {
	.drdy_int_pin = 2,
};

const struct st_sensor_settings *st_gyro_get_settings(const char *name);
int st_gyro_common_probe(struct iio_dev *indio_dev);
void st_gyro_common_remove(struct iio_dev *indio_dev);

#ifdef CONFIG_IIO_BUFFER
int st_gyro_allocate_ring(struct iio_dev *indio_dev);
void st_gyro_deallocate_ring(struct iio_dev *indio_dev);
int st_gyro_trig_set_state(struct iio_trigger *trig, bool state);
#define ST_GYRO_TRIGGER_SET_STATE (&st_gyro_trig_set_state)
#else /* CONFIG_IIO_BUFFER */
static inline int st_gyro_allocate_ring(struct iio_dev *indio_dev)
{
	return 0;
}
static inline void st_gyro_deallocate_ring(struct iio_dev *indio_dev)
{
}
#define ST_GYRO_TRIGGER_SET_STATE NULL
#endif /* CONFIG_IIO_BUFFER */

#endif /* ST_GYRO_H */
