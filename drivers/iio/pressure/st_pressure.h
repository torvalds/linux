/*
 * STMicroelectronics pressures driver
 *
 * Copyright 2013 STMicroelectronics Inc.
 *
 * Denis Ciocca <denis.ciocca@st.com>
 * v. 1.0.0
 * Licensed under the GPL-2.
 */

#ifndef ST_PRESS_H
#define ST_PRESS_H

#include <linux/types.h>
#include <linux/iio/common/st_sensors.h>

#define LPS001WP_PRESS_DEV_NAME		"lps001wp"
#define LPS331AP_PRESS_DEV_NAME		"lps331ap"

/**
 * struct st_sensors_platform_data - default press platform data
 * @drdy_int_pin: default press DRDY is available on INT1 pin.
 */
static const struct st_sensors_platform_data default_press_pdata = {
	.drdy_int_pin = 1,
};

int st_press_common_probe(struct iio_dev *indio_dev,
					struct st_sensors_platform_data *pdata);
void st_press_common_remove(struct iio_dev *indio_dev);

#ifdef CONFIG_IIO_BUFFER
int st_press_allocate_ring(struct iio_dev *indio_dev);
void st_press_deallocate_ring(struct iio_dev *indio_dev);
int st_press_trig_set_state(struct iio_trigger *trig, bool state);
#define ST_PRESS_TRIGGER_SET_STATE (&st_press_trig_set_state)
#else /* CONFIG_IIO_BUFFER */
static inline int st_press_allocate_ring(struct iio_dev *indio_dev)
{
	return 0;
}

static inline void st_press_deallocate_ring(struct iio_dev *indio_dev)
{
}
#define ST_PRESS_TRIGGER_SET_STATE NULL
#endif /* CONFIG_IIO_BUFFER */

#endif /* ST_PRESS_H */
