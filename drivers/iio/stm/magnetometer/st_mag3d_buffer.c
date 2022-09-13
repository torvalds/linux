// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics st_mag3d buffer library driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2016 STMicroelectronics Inc.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/version.h>

#include "st_mag3d.h"

#define ST_MAG3D_STATUS_ADDR		0x27
#define ST_MAG3D_DA_MASK		0x07

static const struct iio_trigger_ops st_mag3d_trigger_ops = {
	NULL
};

static inline s64 st_mag3d_ewma(s64 old, s64 new)
{
	s64 diff, incr;

	diff = new - old;
	incr = div_s64((ST_MAG3D_EWMA_DIV - ST_MAG3D_EWMA_WEIGHT) * diff,
			ST_MAG3D_EWMA_DIV);

	return old + incr;
}

static irqreturn_t st_mag3d_trigger_handler_irq(int irq, void *p)
{
	struct st_mag3d_hw *hw = (struct st_mag3d_hw *)p;
	s64 irq_ts;

	irq_ts = st_mag3d_get_time_ns(hw->iio_dev);

	if (hw->stodis == 0)
		hw->delta_ts = st_mag3d_ewma(hw->delta_ts,
					     irq_ts - hw->timestamp);

	hw->timestamp = irq_ts;

	return IRQ_WAKE_THREAD;
}

static irqreturn_t st_mag3d_trigger_handler_thread(int irq, void *p)
{
	struct st_mag3d_hw *hw = (struct st_mag3d_hw *)p;
	struct iio_dev *iio_dev = hw->iio_dev;
	struct iio_chan_spec const *ch = iio_dev->channels;
	u8 status;
	int err;

	err = hw->tf->read(hw->dev, ST_MAG3D_STATUS_ADDR, sizeof(status),
			   &status);
	if (err < 0)
		goto out;

	if (!(status & ST_MAG3D_DA_MASK))
		return IRQ_NONE;

	err = hw->tf->read(hw->dev, ch->address, ST_MAG3D_SAMPLE_SIZE,
			   hw->buffer);
	if (err < 0)
		goto out;

	iio_trigger_poll_chained(hw->trig);

out:
	return IRQ_HANDLED;
}

int st_mag3d_allocate_trigger(struct iio_dev *iio_dev)
{
	struct st_mag3d_hw *hw = iio_priv(iio_dev);
	int err;

	err = devm_request_threaded_irq(hw->dev, hw->irq,
					st_mag3d_trigger_handler_irq,
					st_mag3d_trigger_handler_thread,
					IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
					"st_mag3d", hw);
	if (err) {
		dev_err(hw->dev, "failed to request trigger irq %d\n",
			hw->irq);
		return err;
	}

	hw->trig = devm_iio_trigger_alloc(hw->dev, "%s-trigger",
					  iio_dev->name);
	if (!hw->trig)
		return -ENOMEM;

	iio_trigger_set_drvdata(hw->trig, iio_dev);
	hw->trig->ops = &st_mag3d_trigger_ops,
	hw->trig->dev.parent = hw->dev;
	iio_dev->trig = iio_trigger_get(hw->trig);

	return iio_trigger_register(hw->trig);
}

void st_mag3d_deallocate_trigger(struct iio_dev *iio_dev)
{
	struct st_mag3d_hw *hw = iio_priv(iio_dev);

	iio_trigger_unregister(hw->trig);
}

static int st_mag3d_buffer_preenable(struct iio_dev *iio_dev)
{
	return st_mag3d_enable_sensor(iio_priv(iio_dev), true);
}

static int st_mag3d_buffer_postdisable(struct iio_dev *iio_dev)
{
	return st_mag3d_enable_sensor(iio_priv(iio_dev), false);
}

static const struct iio_buffer_setup_ops st_mag3d_buffer_ops = {
	.preenable = st_mag3d_buffer_preenable,
#if LINUX_VERSION_CODE < KERNEL_VERSION(5,10,0)
	.postenable = iio_triggered_buffer_postenable,
	.predisable = iio_triggered_buffer_predisable,
#endif /* LINUX_VERSION_CODE */
	.postdisable = st_mag3d_buffer_postdisable,
};

static irqreturn_t st_mag3d_buffer_handler_thread(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct st_mag3d_hw *hw = iio_priv(pf->indio_dev);

	if (hw->stodis == 0)
		iio_push_to_buffers_with_timestamp(pf->indio_dev, hw->buffer,
						   hw->mag_ts);
	else
		hw->stodis--;

	hw->mag_ts += hw->delta_ts;
	iio_trigger_notify_done(hw->trig);

	return IRQ_HANDLED;
}

int st_mag3d_allocate_buffer(struct iio_dev *iio_dev)
{
	return iio_triggered_buffer_setup(iio_dev, NULL,
					  st_mag3d_buffer_handler_thread,
					  &st_mag3d_buffer_ops);
}

void st_mag3d_deallocate_buffer(struct iio_dev *iio_dev)
{
	iio_triggered_buffer_cleanup(iio_dev);
}

MODULE_DESCRIPTION("STMicroelectronics mag3d buffer driver");
MODULE_AUTHOR("MEMS Software Solutions Team");
MODULE_LICENSE("GPL v2");
