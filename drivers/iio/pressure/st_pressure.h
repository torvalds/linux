/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * STMicroelectronics pressures driver
 *
 * Copyright 2013 STMicroelectronics Inc.
 *
 * Denis Ciocca <denis.ciocca@st.com>
 * v. 1.0.0
 */

#ifndef ST_PRESS_H
#define ST_PRESS_H

#include <linux/types.h>
#include <linux/iio/common/st_sensors.h>

enum st_press_type {
	LPS001WP,
	LPS25H,
	LPS331AP,
	LPS22HB,
	LPS33HW,
	LPS35HW,
	LPS22HH,
	ST_PRESS_MAX,
};

#define LPS001WP_PRESS_DEV_NAME		"lps001wp"
#define LPS25H_PRESS_DEV_NAME		"lps25h"
#define LPS331AP_PRESS_DEV_NAME		"lps331ap"
#define LPS22HB_PRESS_DEV_NAME		"lps22hb"
#define LPS33HW_PRESS_DEV_NAME		"lps33hw"
#define LPS35HW_PRESS_DEV_NAME		"lps35hw"
#define LPS22HH_PRESS_DEV_NAME		"lps22hh"

/**
 * struct st_sensors_platform_data - default press platform data
 * @drdy_int_pin: default press DRDY is available on INT1 pin.
 */
static __maybe_unused const struct st_sensors_platform_data default_press_pdata = {
	.drdy_int_pin = 1,
};

#ifdef CONFIG_IIO_BUFFER
int st_press_allocate_ring(struct iio_dev *indio_dev);
int st_press_trig_set_state(struct iio_trigger *trig, bool state);
#define ST_PRESS_TRIGGER_SET_STATE (&st_press_trig_set_state)
#else /* CONFIG_IIO_BUFFER */
static inline int st_press_allocate_ring(struct iio_dev *indio_dev)
{
	return 0;
}
#define ST_PRESS_TRIGGER_SET_STATE NULL
#endif /* CONFIG_IIO_BUFFER */

#endif /* ST_PRESS_H */
