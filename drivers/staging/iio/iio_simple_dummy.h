/**
 * Copyright (c) 2011 Jonathan Cameron
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * Join together the various functionality of iio_simple_dummy driver
 */

#include <linux/kernel.h>

struct iio_dummy_accel_calibscale;

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
 * @event_val:			cache for event theshold value
 * @event_en:			cache of whether event is enabled
 */
struct iio_dummy_state {
	int dac_val;
	int single_ended_adc_val;
	int differential_adc_val[2];
	int accel_val;
	int accel_calibbias;
	const struct iio_dummy_accel_calibscale *accel_calibscale;
	struct mutex lock;
#ifdef CONFIG_IIO_SIMPLE_DUMMY_EVENTS
	int event_irq;
	int event_val;
	bool event_en;
#endif /* CONFIG_IIO_SIMPLE_DUMMY_EVENTS */
};

#ifdef CONFIG_IIO_SIMPLE_DUMMY_EVENTS

struct iio_dev;

int iio_simple_dummy_read_event_config(struct iio_dev *indio_dev,
				       u64 event_code);

int iio_simple_dummy_write_event_config(struct iio_dev *indio_dev,
					u64 event_code,
					int state);

int iio_simple_dummy_read_event_value(struct iio_dev *indio_dev,
				      u64 event_code,
				      int *val);

int iio_simple_dummy_write_event_value(struct iio_dev *indio_dev,
				       u64 event_code,
				       int val);

int iio_simple_dummy_events_register(struct iio_dev *indio_dev);
int iio_simple_dummy_events_unregister(struct iio_dev *indio_dev);

#else /* Stubs for when events are disabled at compile time */

static inline int
iio_simple_dummy_events_register(struct iio_dev *indio_dev)
{
	return 0;
};

static inline int
iio_simple_dummy_events_unregister(struct iio_dev *indio_dev)
{
	return 0;
};

#endif /* CONFIG_IIO_SIMPLE_DUMMY_EVENTS*/

/**
 * enum iio_simple_dummy_scan_elements - scan index enum
 * @voltage0:		the single ended voltage channel
 * @diffvoltage1m2:	first differential channel
 * @diffvoltage3m4:	second differenial channel
 * @accelx:		acceleration channel
 *
 * Enum provides convenient numbering for the scan index.
 */
enum iio_simple_dummy_scan_elements {
	voltage0,
	diffvoltage1m2,
	diffvoltage3m4,
	accelx,
};

#ifdef CONFIG_IIO_SIMPLE_DUMMY_BUFFER
int iio_simple_dummy_configure_buffer(struct iio_dev *indio_dev,
	const struct iio_chan_spec *channels, unsigned int num_channels);
void iio_simple_dummy_unconfigure_buffer(struct iio_dev *indio_dev);
#else
static inline int iio_simple_dummy_configure_buffer(struct iio_dev *indio_dev,
	const struct iio_chan_spec *channels, unsigned int num_channels)
{
	return 0;
};
static inline
void iio_simple_dummy_unconfigure_buffer(struct iio_dev *indio_dev)
{};
#endif /* CONFIG_IIO_SIMPLE_DUMMY_BUFFER */
