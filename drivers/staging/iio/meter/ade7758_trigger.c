/*
 * ADE7758 Poly Phase Multifunction Energy Metering IC driver
 *
 * Copyright 2010-2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 */

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/sysfs.h>
#include <linux/list.h>
#include <linux/spi/spi.h>

#include "../iio.h"
#include "../sysfs.h"
#include "../trigger.h"
#include "ade7758.h"

/**
 * ade7758_data_rdy_trig_poll() the event handler for the data rdy trig
 **/
static irqreturn_t ade7758_data_rdy_trig_poll(int irq, void *private)
{
	disable_irq_nosync(irq);
	iio_trigger_poll(private, iio_get_time_ns());

	return IRQ_HANDLED;
}

/**
 * ade7758_data_rdy_trigger_set_state() set datardy interrupt state
 **/
static int ade7758_data_rdy_trigger_set_state(struct iio_trigger *trig,
						bool state)
{
	struct ade7758_state *st = trig->private_data;
	struct iio_dev *indio_dev = st->indio_dev;

	dev_dbg(&indio_dev->dev, "%s (%d)\n", __func__, state);
	return ade7758_set_irq(&indio_dev->dev, state);
}

/**
 * ade7758_trig_try_reen() try renabling irq for data rdy trigger
 * @trig:	the datardy trigger
 **/
static int ade7758_trig_try_reen(struct iio_trigger *trig)
{
	struct ade7758_state *st = trig->private_data;

	enable_irq(st->us->irq);
	/* irq reenabled so success! */
	return 0;
}

int ade7758_probe_trigger(struct iio_dev *indio_dev)
{
	int ret;
	struct ade7758_state *st = indio_dev->dev_data;

	st->trig = iio_allocate_trigger("%s-dev%d",
					spi_get_device_id(st->us)->name,
					indio_dev->id);
	if (st->trig == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}

	ret = request_irq(st->us->irq,
			  ade7758_data_rdy_trig_poll,
			  IRQF_TRIGGER_LOW,
			  spi_get_device_id(st->us)->name,
			  st->trig);
	if (ret)
		goto error_free_trig;

	st->trig->dev.parent = &st->us->dev;
	st->trig->owner = THIS_MODULE;
	st->trig->private_data = st;
	st->trig->set_trigger_state = &ade7758_data_rdy_trigger_set_state;
	st->trig->try_reenable = &ade7758_trig_try_reen;
	ret = iio_trigger_register(st->trig);

	/* select default trigger */
	indio_dev->trig = st->trig;
	if (ret)
		goto error_free_irq;

	return 0;

error_free_irq:
	free_irq(st->us->irq, st->trig);
error_free_trig:
	iio_free_trigger(st->trig);
error_ret:
	return ret;
}

void ade7758_remove_trigger(struct iio_dev *indio_dev)
{
	struct ade7758_state *state = indio_dev->dev_data;

	iio_trigger_unregister(state->trig);
	free_irq(state->us->irq, state->trig);
	iio_free_trigger(state->trig);
}
