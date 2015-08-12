/*
 * STMicroelectronics magnetometers driver
 *
 * Copyright 2012-2013 STMicroelectronics Inc.
 *
 * Denis Ciocca <denis.ciocca@st.com>
 * v. 1.0.0
 * Licensed under the GPL-2.
 */

#ifndef ST_MAGN_H
#define ST_MAGN_H

#include <linux/types.h>
#include <linux/iio/common/st_sensors.h>

#define LSM303DLH_MAGN_DEV_NAME		"lsm303dlh_magn"
#define LSM303DLHC_MAGN_DEV_NAME	"lsm303dlhc_magn"
#define LSM303DLM_MAGN_DEV_NAME		"lsm303dlm_magn"
#define LIS3MDL_MAGN_DEV_NAME		"lis3mdl"
#define LSM303AGR_MAGN_DEV_NAME		"lsm303agr_magn"

int st_magn_common_probe(struct iio_dev *indio_dev);
void st_magn_common_remove(struct iio_dev *indio_dev);

#ifdef CONFIG_IIO_BUFFER
int st_magn_allocate_ring(struct iio_dev *indio_dev);
void st_magn_deallocate_ring(struct iio_dev *indio_dev);
int st_magn_trig_set_state(struct iio_trigger *trig, bool state);
#define ST_MAGN_TRIGGER_SET_STATE (&st_magn_trig_set_state)
#else /* CONFIG_IIO_BUFFER */
static inline int st_magn_probe_trigger(struct iio_dev *indio_dev, int irq)
{
	return 0;
}
static inline void st_magn_remove_trigger(struct iio_dev *indio_dev, int irq)
{
	return;
}
static inline int st_magn_allocate_ring(struct iio_dev *indio_dev)
{
	return 0;
}
static inline void st_magn_deallocate_ring(struct iio_dev *indio_dev)
{
}
#endif /* CONFIG_IIO_BUFFER */

#endif /* ST_MAGN_H */
