/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (c) 2011 Jonathan Cameron
 *
 * Join together the various functionality of iio_simple_dummy driver
 */

#ifndef _IIO_SIMPLE_DUMMY_H_
#define _IIO_SIMPLE_DUMMY_H_
#include <linux/kernel.h>

struct iio_dummy_accel_calibscale;
struct iio_dummy_regs;

/**
 * struct iio_dummy_state - device instance specific state.
 * @dac_val:			cache for dac value
 * @single_ended_adc_val:	cache for single ended adc value
 * @differential_adc_val:	cache for differential adc value
 * @accel_val:			cache for acceleration value
 * @accel_calibbias:		cache for acceleration calibbias
 * @accel_calibscale:		cache for acceleration calibscale
 * @lock:			lock to ensure state is consistent
 * @event_irq:			irq number for event line (faked)
 * @event_val:			cache for event threshold value
 * @event_en:			cache of whether event is enabled
 */
struct iio_dummy_state {
	int dac_val;
	int single_ended_adc_val;
	int differential_adc_val[2];
	int accel_val;
	int accel_calibbias;
	int activity_running;
	int activity_walking;
	const struct iio_dummy_accel_calibscale *accel_calibscale;
	struct mutex lock;
	struct iio_dummy_regs *regs;
	int steps_enabled;
	int steps;
	int height;
#ifdef CONFIG_IIO_SIMPLE_DUMMY_EVENTS
	int event_irq;
	int event_val;
	bool event_en;
	s64 event_timestamp;
#endif /* CONFIG_IIO_SIMPLE_DUMMY_EVENTS */
};

#ifdef CONFIG_IIO_SIMPLE_DUMMY_EVENTS

struct iio_dev;

int iio_simple_dummy_read_event_config(struct iio_dev *indio_dev,
				       const struct iio_chan_spec *chan,
				       enum iio_event_type type,
				       enum iio_event_direction dir);

int iio_simple_dummy_write_event_config(struct iio_dev *indio_dev,
					const struct iio_chan_spec *chan,
					enum iio_event_type type,
					enum iio_event_direction dir,
					bool state);

int iio_simple_dummy_read_event_value(struct iio_dev *indio_dev,
				      const struct iio_chan_spec *chan,
				      enum iio_event_type type,
				      enum iio_event_direction dir,
				      enum iio_event_info info, int *val,
				      int *val2);

int iio_simple_dummy_write_event_value(struct iio_dev *indio_dev,
				       const struct iio_chan_spec *chan,
				       enum iio_event_type type,
				       enum iio_event_direction dir,
				       enum iio_event_info info, int val,
				       int val2);

int iio_simple_dummy_events_register(struct iio_dev *indio_dev);
void iio_simple_dummy_events_unregister(struct iio_dev *indio_dev);

#else /* Stubs for when events are disabled at compile time */

static inline int
iio_simple_dummy_events_register(struct iio_dev *indio_dev)
{
	return 0;
}

static inline void
iio_simple_dummy_events_unregister(struct iio_dev *indio_dev)
{}

#endif /* CONFIG_IIO_SIMPLE_DUMMY_EVENTS*/

/**
 * enum iio_simple_dummy_scan_elements - scan index enum
 * @DUMMY_INDEX_VOLTAGE_0:         the single ended voltage channel
 * @DUMMY_INDEX_DIFFVOLTAGE_1M2:   first differential channel
 * @DUMMY_INDEX_DIFFVOLTAGE_3M4:   second differential channel
 * @DUMMY_INDEX_ACCELX:            acceleration channel
 *
 * Enum provides convenient numbering for the scan index.
 */
enum iio_simple_dummy_scan_elements {
	DUMMY_INDEX_VOLTAGE_0,
	DUMMY_INDEX_DIFFVOLTAGE_1M2,
	DUMMY_INDEX_DIFFVOLTAGE_3M4,
	DUMMY_INDEX_ACCELX,
};

#ifdef CONFIG_IIO_SIMPLE_DUMMY_BUFFER
int iio_simple_dummy_configure_buffer(struct iio_dev *indio_dev);
void iio_simple_dummy_unconfigure_buffer(struct iio_dev *indio_dev);
#else
static inline int iio_simple_dummy_configure_buffer(struct iio_dev *indio_dev)
{
	return 0;
}

static inline
void iio_simple_dummy_unconfigure_buffer(struct iio_dev *indio_dev)
{}

#endif /* CONFIG_IIO_SIMPLE_DUMMY_BUFFER */
#endif /* _IIO_SIMPLE_DUMMY_H_ */
