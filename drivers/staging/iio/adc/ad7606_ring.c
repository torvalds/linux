/*
 * Copyright 2011-2012 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 *
 */

#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>

#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

#include "ad7606.h"

/**
 * ad7606_trigger_handler_th() th/bh of trigger launched polling to ring buffer
 *
 **/
static irqreturn_t ad7606_trigger_handler_th_bh(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct ad7606_state *st = iio_priv(pf->indio_dev);

	gpio_set_value(st->pdata->gpio_convst, 1);

	return IRQ_HANDLED;
}

/**
 * ad7606_poll_bh_to_ring() bh of trigger launched polling to ring buffer
 * @work_s:	the work struct through which this was scheduled
 *
 * Currently there is no option in this driver to disable the saving of
 * timestamps within the ring.
 * I think the one copy of this at a time was to avoid problems if the
 * trigger was set far too high and the reads then locked up the computer.
 **/
static void ad7606_poll_bh_to_ring(struct work_struct *work_s)
{
	struct ad7606_state *st = container_of(work_s, struct ad7606_state,
						poll_work);
	struct iio_dev *indio_dev = iio_priv_to_dev(st);
	__u8 *buf;
	int ret;

	buf = kzalloc(indio_dev->scan_bytes, GFP_KERNEL);
	if (!buf)
		return;

	if (gpio_is_valid(st->pdata->gpio_frstdata)) {
		ret = st->bops->read_block(st->dev, 1, buf);
		if (ret)
			goto done;
		if (!gpio_get_value(st->pdata->gpio_frstdata)) {
			/* This should never happen. However
			 * some signal glitch caused by bad PCB desgin or
			 * electrostatic discharge, could cause an extra read
			 * or clock. This allows recovery.
			 */
			ad7606_reset(st);
			goto done;
		}
		ret = st->bops->read_block(st->dev,
			st->chip_info->num_channels - 1, buf + 2);
		if (ret)
			goto done;
	} else {
		ret = st->bops->read_block(st->dev,
			st->chip_info->num_channels, buf);
		if (ret)
			goto done;
	}

	iio_push_to_buffers_with_timestamp(indio_dev, buf, iio_get_time_ns());
done:
	gpio_set_value(st->pdata->gpio_convst, 0);
	iio_trigger_notify_done(indio_dev->trig);
	kfree(buf);
}

int ad7606_register_ring_funcs_and_init(struct iio_dev *indio_dev)
{
	struct ad7606_state *st = iio_priv(indio_dev);

	INIT_WORK(&st->poll_work, &ad7606_poll_bh_to_ring);

	return iio_triggered_buffer_setup(indio_dev,
		&ad7606_trigger_handler_th_bh, &ad7606_trigger_handler_th_bh,
		NULL);
}

void ad7606_ring_cleanup(struct iio_dev *indio_dev)
{
	iio_triggered_buffer_cleanup(indio_dev);
}
