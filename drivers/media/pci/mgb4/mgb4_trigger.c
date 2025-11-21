// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021-2023 Digiteq Automotive
 *     author: Martin Tuma <martin.tuma@digiteqautomotive.com>
 *
 * This module handles the IIO trigger device. The card has two signal inputs
 * for event triggers that can be used to record events related to the video
 * stream. A standard linux IIO device with triggered buffer capability is
 * created and configured that can be used to fetch the events with the same
 * clock source as the video frames.
 */

#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/pci.h>
#include <linux/dma/amd_xdma.h>
#include "mgb4_core.h"
#include "mgb4_trigger.h"

struct trigger_data {
	struct mgb4_dev *mgbdev;
	struct iio_trigger *trig;
};

static int trigger_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan, int *val,
			    int *val2, long mask)
{
	struct trigger_data *st = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (iio_buffer_enabled(indio_dev))
			return -EBUSY;
		*val = mgb4_read_reg(&st->mgbdev->video, 0xA0);

		return IIO_VAL_INT;
	}

	return -EINVAL;
}

static int trigger_set_state(struct iio_trigger *trig, bool state)
{
	struct iio_dev *indio_dev = iio_trigger_get_drvdata(trig);
	struct trigger_data *st = iio_priv(indio_dev);
	int irq = xdma_get_user_irq(st->mgbdev->xdev, 11);

	if (state)
		xdma_enable_user_irq(st->mgbdev->xdev, irq);
	else
		xdma_disable_user_irq(st->mgbdev->xdev, irq);

	return 0;
}

static const struct iio_trigger_ops trigger_ops = {
	.set_trigger_state = &trigger_set_state,
};

static const struct iio_info trigger_info = {
	.read_raw         = trigger_read_raw,
};

#define TRIGGER_CHANNEL(_si) {                    \
	.type = IIO_ACTIVITY,                         \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
	.scan_index = _si,                            \
	.scan_type = {                                \
		.sign = 'u',                              \
		.realbits = 32,                           \
		.storagebits = 32,                        \
		.shift = 0,                               \
		.endianness = IIO_CPU                     \
	},                                            \
}

static const struct iio_chan_spec trigger_channels[] = {
	TRIGGER_CHANNEL(0),
	IIO_CHAN_SOFT_TIMESTAMP(1),
};

static irqreturn_t trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct trigger_data *st = iio_priv(indio_dev);
	struct {
		u32 data;
		s64 ts __aligned(8);
	} scan = { };

	scan.data = mgb4_read_reg(&st->mgbdev->video, 0xA0);
	mgb4_write_reg(&st->mgbdev->video, 0xA0, scan.data);

	iio_push_to_buffers_with_timestamp(indio_dev, &scan, pf->timestamp);
	iio_trigger_notify_done(indio_dev->trig);

	mgb4_write_reg(&st->mgbdev->video, 0xB4, 1U << 11);

	return IRQ_HANDLED;
}

static int probe_trigger(struct iio_dev *indio_dev, int irq)
{
	int ret;
	struct trigger_data *st = iio_priv(indio_dev);

	st->trig = iio_trigger_alloc(&st->mgbdev->pdev->dev, "%s-dev%d",
				     indio_dev->name, iio_device_id(indio_dev));
	if (!st->trig)
		return -ENOMEM;

	ret = request_irq(irq, &iio_trigger_generic_data_rdy_poll, 0,
			  "mgb4-trigger", st->trig);
	if (ret)
		goto error_free_trig;

	st->trig->ops = &trigger_ops;
	iio_trigger_set_drvdata(st->trig, indio_dev);
	ret = iio_trigger_register(st->trig);
	if (ret)
		goto error_free_irq;

	indio_dev->trig = iio_trigger_get(st->trig);

	return 0;

error_free_irq:
	free_irq(irq, st->trig);
error_free_trig:
	iio_trigger_free(st->trig);

	return ret;
}

static void remove_trigger(struct iio_dev *indio_dev, int irq)
{
	struct trigger_data *st = iio_priv(indio_dev);

	iio_trigger_unregister(st->trig);
	free_irq(irq, st->trig);
	iio_trigger_free(st->trig);
}

struct iio_dev *mgb4_trigger_create(struct mgb4_dev *mgbdev)
{
	struct iio_dev *indio_dev;
	struct trigger_data *data;
	struct pci_dev *pdev = mgbdev->pdev;
	struct device *dev = &pdev->dev;
	int rv, irq;

	indio_dev = iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return NULL;

	indio_dev->info = &trigger_info;
	indio_dev->name = "mgb4";
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = trigger_channels;
	indio_dev->num_channels = ARRAY_SIZE(trigger_channels);

	data = iio_priv(indio_dev);
	data->mgbdev = mgbdev;

	irq = xdma_get_user_irq(mgbdev->xdev, 11);
	rv = probe_trigger(indio_dev, irq);
	if (rv < 0) {
		dev_err(dev, "iio triggered setup failed\n");
		goto error_alloc;
	}
	rv = iio_triggered_buffer_setup(indio_dev, &iio_pollfunc_store_time,
					trigger_handler, NULL);
	if (rv < 0) {
		dev_err(dev, "iio triggered buffer setup failed\n");
		goto error_trigger;
	}
	rv = iio_device_register(indio_dev);
	if (rv < 0) {
		dev_err(dev, "iio device register failed\n");
		goto error_buffer;
	}

	return indio_dev;

error_buffer:
	iio_triggered_buffer_cleanup(indio_dev);
error_trigger:
	remove_trigger(indio_dev, irq);
error_alloc:
	iio_device_free(indio_dev);

	return NULL;
}

void mgb4_trigger_free(struct iio_dev *indio_dev)
{
	struct trigger_data *st = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	iio_triggered_buffer_cleanup(indio_dev);
	remove_trigger(indio_dev, xdma_get_user_irq(st->mgbdev->xdev, 11));
	iio_device_free(indio_dev);
}
