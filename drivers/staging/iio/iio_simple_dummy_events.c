/**
 * Copyright (c) 2011 Jonathan Cameron
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * Event handling elements of industrial I/O reference driver.
 */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/irq.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/events.h>
#include "iio_simple_dummy.h"

/* Evgen 'fakes' interrupt events for this example */
#include "iio_dummy_evgen.h"

/**
 * iio_simple_dummy_read_event_config() - is event enabled?
 * @indio_dev: the device instance data
 * @chan: channel for the event whose state is being queried
 * @type: type of the event whose state is being queried
 * @dir: direction of the vent whose state is being queried
 *
 * This function would normally query the relevant registers or a cache to
 * discover if the event generation is enabled on the device.
 */
int iio_simple_dummy_read_event_config(struct iio_dev *indio_dev,
				       const struct iio_chan_spec *chan,
				       enum iio_event_type type,
				       enum iio_event_direction dir)
{
	struct iio_dummy_state *st = iio_priv(indio_dev);

	return st->event_en;
}

/**
 * iio_simple_dummy_write_event_config() - set whether event is enabled
 * @indio_dev: the device instance data
 * @chan: channel for the event whose state is being set
 * @type: type of the event whose state is being set
 * @dir: direction of the vent whose state is being set
 * @state: whether to enable or disable the device.
 *
 * This function would normally set the relevant registers on the devices
 * so that it generates the specified event. Here it just sets up a cached
 * value.
 */
int iio_simple_dummy_write_event_config(struct iio_dev *indio_dev,
					const struct iio_chan_spec *chan,
					enum iio_event_type type,
					enum iio_event_direction dir,
					int state)
{
	struct iio_dummy_state *st = iio_priv(indio_dev);

	/*
	 *  Deliberately over the top code splitting to illustrate
	 * how this is done when multiple events exist.
	 */
	switch (chan->type) {
	case IIO_VOLTAGE:
		switch (type) {
		case IIO_EV_TYPE_THRESH:
			if (dir == IIO_EV_DIR_RISING)
				st->event_en = state;
			else
				return -EINVAL;
		default:
			return -EINVAL;
		}
		break;
	case IIO_ACTIVITY:
		switch (type) {
		case IIO_EV_TYPE_THRESH:
			st->event_en = state;
			break;
		default:
			return -EINVAL;
		}
	case IIO_STEPS:
		switch (type) {
		case IIO_EV_TYPE_CHANGE:
			st->event_en = state;
			break;
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}

	return 0;
}

/**
 * iio_simple_dummy_read_event_value() - get value associated with event
 * @indio_dev: device instance specific data
 * @chan: channel for the event whose value is being read
 * @type: type of the event whose value is being read
 * @dir: direction of the vent whose value is being read
 * @info: info type of the event whose value is being read
 * @val: value for the event code.
 *
 * Many devices provide a large set of events of which only a subset may
 * be enabled at a time, with value registers whose meaning changes depending
 * on the event enabled. This often means that the driver must cache the values
 * associated with each possible events so that the right value is in place when
 * the enabled event is changed.
 */
int iio_simple_dummy_read_event_value(struct iio_dev *indio_dev,
				      const struct iio_chan_spec *chan,
				      enum iio_event_type type,
				      enum iio_event_direction dir,
					  enum iio_event_info info,
				      int *val, int *val2)
{
	struct iio_dummy_state *st = iio_priv(indio_dev);

	*val = st->event_val;

	return IIO_VAL_INT;
}

/**
 * iio_simple_dummy_write_event_value() - set value associate with event
 * @indio_dev: device instance specific data
 * @chan: channel for the event whose value is being set
 * @type: type of the event whose value is being set
 * @dir: direction of the vent whose value is being set
 * @info: info type of the event whose value is being set
 * @val: the value to be set.
 */
int iio_simple_dummy_write_event_value(struct iio_dev *indio_dev,
				       const struct iio_chan_spec *chan,
				       enum iio_event_type type,
				       enum iio_event_direction dir,
					   enum iio_event_info info,
				       int val, int val2)
{
	struct iio_dummy_state *st = iio_priv(indio_dev);

	st->event_val = val;

	return 0;
}

/**
 * iio_simple_dummy_event_handler() - identify and pass on event
 * @irq: irq of event line
 * @private: pointer to device instance state.
 *
 * This handler is responsible for querying the device to find out what
 * event occurred and for then pushing that event towards userspace.
 * Here only one event occurs so we push that directly on with locally
 * grabbed timestamp.
 */
static irqreturn_t iio_simple_dummy_event_handler(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct iio_dummy_state *st = iio_priv(indio_dev);

	dev_dbg(&indio_dev->dev, "id %x event %x\n",
		st->regs->reg_id, st->regs->reg_data);

	switch (st->regs->reg_data) {
	case 0:
		iio_push_event(indio_dev,
			       IIO_EVENT_CODE(IIO_VOLTAGE, 0, 0,
					      IIO_EV_DIR_RISING,
					      IIO_EV_TYPE_THRESH, 0, 0, 0),
			       iio_get_time_ns());
		break;
	case 1:
		if (st->activity_running > st->event_val)
			iio_push_event(indio_dev,
				       IIO_EVENT_CODE(IIO_ACTIVITY, 0,
						      IIO_MOD_RUNNING,
						      IIO_EV_DIR_RISING,
						      IIO_EV_TYPE_THRESH,
						      0, 0, 0),
				       iio_get_time_ns());
		break;
	case 2:
		if (st->activity_walking < st->event_val)
			iio_push_event(indio_dev,
				       IIO_EVENT_CODE(IIO_ACTIVITY, 0,
						      IIO_MOD_WALKING,
						      IIO_EV_DIR_FALLING,
						      IIO_EV_TYPE_THRESH,
						      0, 0, 0),
				       iio_get_time_ns());
		break;
	case 3:
		iio_push_event(indio_dev,
			       IIO_EVENT_CODE(IIO_STEPS, 0, IIO_NO_MOD,
					      IIO_EV_DIR_NONE,
					      IIO_EV_TYPE_CHANGE, 0, 0, 0),
			       iio_get_time_ns());
		break;
	default:
		break;
	}

	return IRQ_HANDLED;
}

/**
 * iio_simple_dummy_events_register() - setup interrupt handling for events
 * @indio_dev: device instance data
 *
 * This function requests the threaded interrupt to handle the events.
 * Normally the irq is a hardware interrupt and the number comes
 * from board configuration files.  Here we get it from a companion
 * module that fakes the interrupt for us. Note that module in
 * no way forms part of this example. Just assume that events magically
 * appear via the provided interrupt.
 */
int iio_simple_dummy_events_register(struct iio_dev *indio_dev)
{
	struct iio_dummy_state *st = iio_priv(indio_dev);
	int ret;

	/* Fire up event source - normally not present */
	st->event_irq = iio_dummy_evgen_get_irq();
	if (st->event_irq < 0) {
		ret = st->event_irq;
		goto error_ret;
	}
	st->regs = iio_dummy_evgen_get_regs(st->event_irq);

	ret = request_threaded_irq(st->event_irq,
				   NULL,
				   &iio_simple_dummy_event_handler,
				   IRQF_ONESHOT,
				   "iio_simple_event",
				   indio_dev);
	if (ret < 0)
		goto error_free_evgen;
	return 0;

error_free_evgen:
	iio_dummy_evgen_release_irq(st->event_irq);
error_ret:
	return ret;
}

/**
 * iio_simple_dummy_events_unregister() - tidy up interrupt handling on remove
 * @indio_dev: device instance data
 */
int iio_simple_dummy_events_unregister(struct iio_dev *indio_dev)
{
	struct iio_dummy_state *st = iio_priv(indio_dev);

	free_irq(st->event_irq, indio_dev);
	/* Not part of normal driver */
	iio_dummy_evgen_release_irq(st->event_irq);

	return 0;
}
