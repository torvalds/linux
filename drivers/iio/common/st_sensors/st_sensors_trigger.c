// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics sensors trigger library driver
 *
 * Copyright 2012-2013 STMicroelectronics Inc.
 *
 * Denis Ciocca <denis.ciocca@st.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/iio/iio.h>
#include <linux/iio/trigger.h>
#include <linux/interrupt.h>
#include <linux/regmap.h>
#include <linux/iio/common/st_sensors.h>
#include "st_sensors_core.h"

/**
 * st_sensors_new_samples_available() - check if more samples came in
 * @indio_dev: IIO device reference.
 * @sdata: Sensor data.
 *
 * returns:
 * 0 - no new samples available
 * 1 - new samples available
 * negative - error or unknown
 */
static int st_sensors_new_samples_available(struct iio_dev *indio_dev,
					    struct st_sensor_data *sdata)
{
	int ret, status;

	/* How would I know if I can't check it? */
	if (!sdata->sensor_settings->drdy_irq.stat_drdy.addr)
		return -EINVAL;

	/* No scan mask, no interrupt */
	if (!indio_dev->active_scan_mask)
		return 0;

	ret = regmap_read(sdata->regmap,
			  sdata->sensor_settings->drdy_irq.stat_drdy.addr,
			  &status);
	if (ret < 0) {
		dev_err(sdata->dev,
			"error checking samples available\n");
		return ret;
	}

	if (status & sdata->sensor_settings->drdy_irq.stat_drdy.mask)
		return 1;

	return 0;
}

/**
 * st_sensors_irq_handler() - top half of the IRQ-based triggers
 * @irq: irq number
 * @p: private handler data
 */
static irqreturn_t st_sensors_irq_handler(int irq, void *p)
{
	struct iio_trigger *trig = p;
	struct iio_dev *indio_dev = iio_trigger_get_drvdata(trig);
	struct st_sensor_data *sdata = iio_priv(indio_dev);

	/* Get the time stamp as close in time as possible */
	sdata->hw_timestamp = iio_get_time_ns(indio_dev);
	return IRQ_WAKE_THREAD;
}

/**
 * st_sensors_irq_thread() - bottom half of the IRQ-based triggers
 * @irq: irq number
 * @p: private handler data
 */
static irqreturn_t st_sensors_irq_thread(int irq, void *p)
{
	struct iio_trigger *trig = p;
	struct iio_dev *indio_dev = iio_trigger_get_drvdata(trig);
	struct st_sensor_data *sdata = iio_priv(indio_dev);

	/*
	 * If this trigger is backed by a hardware interrupt and we have a
	 * status register, check if this IRQ came from us. Notice that
	 * we will process also if st_sensors_new_samples_available()
	 * returns negative: if we can't check status, then poll
	 * unconditionally.
	 */
	if (sdata->hw_irq_trigger &&
	    st_sensors_new_samples_available(indio_dev, sdata)) {
		iio_trigger_poll_chained(p);
	} else {
		dev_dbg(sdata->dev, "spurious IRQ\n");
		return IRQ_NONE;
	}

	/*
	 * If we have proper level IRQs the handler will be re-entered if
	 * the line is still active, so return here and come back in through
	 * the top half if need be.
	 */
	if (!sdata->edge_irq)
		return IRQ_HANDLED;

	/*
	 * If we are using edge IRQs, new samples arrived while processing
	 * the IRQ and those may be missed unless we pick them here, so poll
	 * again. If the sensor delivery frequency is very high, this thread
	 * turns into a polled loop handler.
	 */
	while (sdata->hw_irq_trigger &&
	       st_sensors_new_samples_available(indio_dev, sdata)) {
		dev_dbg(sdata->dev, "more samples came in during polling\n");
		sdata->hw_timestamp = iio_get_time_ns(indio_dev);
		iio_trigger_poll_chained(p);
	}

	return IRQ_HANDLED;
}

int st_sensors_allocate_trigger(struct iio_dev *indio_dev,
				const struct iio_trigger_ops *trigger_ops)
{
	struct st_sensor_data *sdata = iio_priv(indio_dev);
	unsigned long irq_trig;
	int err;

	sdata->trig = iio_trigger_alloc("%s-trigger", indio_dev->name);
	if (sdata->trig == NULL) {
		dev_err(&indio_dev->dev, "failed to allocate iio trigger.\n");
		return -ENOMEM;
	}

	iio_trigger_set_drvdata(sdata->trig, indio_dev);
	sdata->trig->ops = trigger_ops;
	sdata->trig->dev.parent = sdata->dev;

	irq_trig = irqd_get_trigger_type(irq_get_irq_data(sdata->irq));
	/*
	 * If the IRQ is triggered on falling edge, we need to mark the
	 * interrupt as active low, if the hardware supports this.
	 */
	switch(irq_trig) {
	case IRQF_TRIGGER_FALLING:
	case IRQF_TRIGGER_LOW:
		if (!sdata->sensor_settings->drdy_irq.addr_ihl) {
			dev_err(&indio_dev->dev,
				"falling/low specified for IRQ "
				"but hardware supports only rising/high: "
				"will request rising/high\n");
			if (irq_trig == IRQF_TRIGGER_FALLING)
				irq_trig = IRQF_TRIGGER_RISING;
			if (irq_trig == IRQF_TRIGGER_LOW)
				irq_trig = IRQF_TRIGGER_HIGH;
		} else {
			/* Set up INT active low i.e. falling edge */
			err = st_sensors_write_data_with_mask(indio_dev,
				sdata->sensor_settings->drdy_irq.addr_ihl,
				sdata->sensor_settings->drdy_irq.mask_ihl, 1);
			if (err < 0)
				goto iio_trigger_free;
			dev_info(&indio_dev->dev,
				 "interrupts on the falling edge or "
				 "active low level\n");
		}
		break;
	case IRQF_TRIGGER_RISING:
		dev_info(&indio_dev->dev,
			 "interrupts on the rising edge\n");
		break;
	case IRQF_TRIGGER_HIGH:
		dev_info(&indio_dev->dev,
			 "interrupts active high level\n");
		break;
	default:
		/* This is the most preferred mode, if possible */
		dev_err(&indio_dev->dev,
			"unsupported IRQ trigger specified (%lx), enforce "
			"rising edge\n", irq_trig);
		irq_trig = IRQF_TRIGGER_RISING;
	}

	/* Tell the interrupt handler that we're dealing with edges */
	if (irq_trig == IRQF_TRIGGER_FALLING ||
	    irq_trig == IRQF_TRIGGER_RISING)
		sdata->edge_irq = true;
	else
		/*
		 * If we're not using edges (i.e. level interrupts) we
		 * just mask off the IRQ, handle one interrupt, then
		 * if the line is still low, we return to the
		 * interrupt handler top half again and start over.
		 */
		irq_trig |= IRQF_ONESHOT;

	/*
	 * If the interrupt pin is Open Drain, by definition this
	 * means that the interrupt line may be shared with other
	 * peripherals. But to do this we also need to have a status
	 * register and mask to figure out if this sensor was firing
	 * the IRQ or not, so we can tell the interrupt handle that
	 * it was "our" interrupt.
	 */
	if (sdata->int_pin_open_drain &&
	    sdata->sensor_settings->drdy_irq.stat_drdy.addr)
		irq_trig |= IRQF_SHARED;

	err = request_threaded_irq(sdata->irq,
				   st_sensors_irq_handler,
				   st_sensors_irq_thread,
				   irq_trig,
				   sdata->trig->name,
				   sdata->trig);
	if (err) {
		dev_err(&indio_dev->dev, "failed to request trigger IRQ.\n");
		goto iio_trigger_free;
	}

	err = iio_trigger_register(sdata->trig);
	if (err < 0) {
		dev_err(&indio_dev->dev, "failed to register iio trigger.\n");
		goto iio_trigger_register_error;
	}
	indio_dev->trig = iio_trigger_get(sdata->trig);

	return 0;

iio_trigger_register_error:
	free_irq(sdata->irq, sdata->trig);
iio_trigger_free:
	iio_trigger_free(sdata->trig);
	return err;
}
EXPORT_SYMBOL(st_sensors_allocate_trigger);

void st_sensors_deallocate_trigger(struct iio_dev *indio_dev)
{
	struct st_sensor_data *sdata = iio_priv(indio_dev);

	iio_trigger_unregister(sdata->trig);
	free_irq(sdata->irq, sdata->trig);
	iio_trigger_free(sdata->trig);
}
EXPORT_SYMBOL(st_sensors_deallocate_trigger);

int st_sensors_validate_device(struct iio_trigger *trig,
			       struct iio_dev *indio_dev)
{
	struct iio_dev *indio = iio_trigger_get_drvdata(trig);

	if (indio != indio_dev)
		return -EINVAL;

	return 0;
}
EXPORT_SYMBOL(st_sensors_validate_device);

MODULE_AUTHOR("Denis Ciocca <denis.ciocca@st.com>");
MODULE_DESCRIPTION("STMicroelectronics ST-sensors trigger");
MODULE_LICENSE("GPL v2");
