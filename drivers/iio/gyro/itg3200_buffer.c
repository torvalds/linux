/*
 * itg3200_buffer.c -- support InvenSense ITG3200
 *                     Digital 3-Axis Gyroscope driver
 *
 * Copyright (c) 2011 Christian Strobel <christian.strobel@iis.fraunhofer.de>
 * Copyright (c) 2011 Manuel Stahl <manuel.stahl@iis.fraunhofer.de>
 * Copyright (c) 2012 Thorsten Nowak <thorsten.nowak@iis.fraunhofer.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>

#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/gyro/itg3200.h>


static int itg3200_read_all_channels(struct i2c_client *i2c, __be16 *buf)
{
	u8 tx = 0x80 | ITG3200_REG_TEMP_OUT_H;
	struct i2c_msg msg[2] = {
		{
			.addr = i2c->addr,
			.flags = i2c->flags,
			.len = 1,
			.buf = &tx,
		},
		{
			.addr = i2c->addr,
			.flags = i2c->flags | I2C_M_RD,
			.len = ITG3200_SCAN_ELEMENTS * sizeof(s16),
			.buf = (char *)&buf,
		},
	};

	return i2c_transfer(i2c->adapter, msg, 2);
}

static irqreturn_t itg3200_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct itg3200 *st = iio_priv(indio_dev);
	__be16 buf[ITG3200_SCAN_ELEMENTS + sizeof(s64)/sizeof(u16)];

	int ret = itg3200_read_all_channels(st->i2c, buf);
	if (ret < 0)
		goto error_ret;

	iio_push_to_buffers_with_timestamp(indio_dev, buf, pf->timestamp);

	iio_trigger_notify_done(indio_dev->trig);

error_ret:
	return IRQ_HANDLED;
}

int itg3200_buffer_configure(struct iio_dev *indio_dev)
{
	return iio_triggered_buffer_setup(indio_dev, &iio_pollfunc_store_time,
		itg3200_trigger_handler, NULL);
}

void itg3200_buffer_unconfigure(struct iio_dev *indio_dev)
{
	iio_triggered_buffer_cleanup(indio_dev);
}


static int itg3200_data_rdy_trigger_set_state(struct iio_trigger *trig,
		bool state)
{
	struct iio_dev *indio_dev = iio_trigger_get_drvdata(trig);
	int ret;
	u8 msc;

	ret = itg3200_read_reg_8(indio_dev, ITG3200_REG_IRQ_CONFIG, &msc);
	if (ret)
		goto error_ret;

	if (state)
		msc |= ITG3200_IRQ_DATA_RDY_ENABLE;
	else
		msc &= ~ITG3200_IRQ_DATA_RDY_ENABLE;

	ret = itg3200_write_reg_8(indio_dev, ITG3200_REG_IRQ_CONFIG, msc);
	if (ret)
		goto error_ret;

error_ret:
	return ret;

}

static const struct iio_trigger_ops itg3200_trigger_ops = {
	.owner = THIS_MODULE,
	.set_trigger_state = &itg3200_data_rdy_trigger_set_state,
};

int itg3200_probe_trigger(struct iio_dev *indio_dev)
{
	int ret;
	struct itg3200 *st = iio_priv(indio_dev);

	st->trig = iio_trigger_alloc("%s-dev%d", indio_dev->name,
				     indio_dev->id);
	if (!st->trig)
		return -ENOMEM;

	ret = request_irq(st->i2c->irq,
			  &iio_trigger_generic_data_rdy_poll,
			  IRQF_TRIGGER_RISING,
			  "itg3200_data_rdy",
			  st->trig);
	if (ret)
		goto error_free_trig;


	st->trig->dev.parent = &st->i2c->dev;
	st->trig->ops = &itg3200_trigger_ops;
	iio_trigger_set_drvdata(st->trig, indio_dev);
	ret = iio_trigger_register(st->trig);
	if (ret)
		goto error_free_irq;

	/* select default trigger */
	indio_dev->trig = st->trig;

	return 0;

error_free_irq:
	free_irq(st->i2c->irq, st->trig);
error_free_trig:
	iio_trigger_free(st->trig);
	return ret;
}

void itg3200_remove_trigger(struct iio_dev *indio_dev)
{
	struct itg3200 *st = iio_priv(indio_dev);

	iio_trigger_unregister(st->trig);
	free_irq(st->i2c->irq, st->trig);
	iio_trigger_free(st->trig);
}
