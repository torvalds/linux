/*
 * STMicroelectronics sensors trigger library driver
 *
 * Copyright 2012-2013 STMicroelectronics Inc.
 *
 * Denis Ciocca <denis.ciocca@st.com>
 *
 * Licensed under the GPL-2.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/iio/iio.h>
#include <linux/iio/trigger.h>
#include <linux/interrupt.h>
#include <linux/iio/common/st_sensors.h>
#include "st_sensors_core.h"

/**
 * st_sensors_irq_handler() - top half of the IRQ-based triggers
 * @irq: irq number
 * @p: private handler data
 */
irqreturn_t st_sensors_irq_handler(int irq, void *p)
{
	struct iio_trigger *trig = p;
	struct iio_dev *indio_dev = iio_trigger_get_drvdata(trig);
	struct st_sensor_data *sdata = iio_priv(indio_dev);

	/* Get the time stamp as close in time as possible */
	sdata->hw_timestamp = iio_get_time_ns();
	return IRQ_WAKE_THREAD;
}

/**
 * st_sensors_irq_thread() - bottom half of the IRQ-based triggers
 * @irq: irq number
 * @p: private handler data
 */
irqreturn_t st_sensors_irq_thread(int irq, void *p)
{
	struct iio_trigger *trig = p;
	struct iio_dev *indio_dev = iio_trigger_get_drvdata(trig);
	struct st_sensor_data *sdata = iio_priv(indio_dev);
	int ret;

	/*
	 * If this trigger is backed by a hardware interrupt and we have a
	 * status register, check if this IRQ came from us
	 */
	if (sdata->sensor_settings->drdy_irq.addr_stat_drdy) {
		u8 status;

		ret = sdata->tf->read_byte(&sdata->tb, sdata->dev,
			   sdata->sensor_settings->drdy_irq.addr_stat_drdy,
			   &status);
		if (ret < 0) {
			dev_err(sdata->dev, "could not read channel status\n");
			goto out_poll;
		}
		/*
		 * the lower bits of .active_scan_mask[0] is directly mapped
		 * to the channels on the sensor: either bit 0 for
		 * one-dimensional sensors, or e.g. x,y,z for accelerometers,
		 * gyroscopes or magnetometers. No sensor use more than 3
		 * channels, so cut the other status bits here.
		 */
		status &= 0x07;

		/*
		 * If this was not caused by any channels on this sensor,
		 * return IRQ_NONE
		 */
		if (!indio_dev->active_scan_mask)
			return IRQ_NONE;
		if (!(status & (u8)indio_dev->active_scan_mask[0]))
			return IRQ_NONE;
	}

out_poll:
	/* It's our IRQ: proceed to handle the register polling */
	iio_trigger_poll_chained(p);
	return IRQ_HANDLED;
}

int st_sensors_allocate_trigger(struct iio_dev *indio_dev,
				const struct iio_trigger_ops *trigger_ops)
{
	int err, irq;
	struct st_sensor_data *sdata = iio_priv(indio_dev);
	unsigned long irq_trig;

	sdata->trig = iio_trigger_alloc("%s-trigger", indio_dev->name);
	if (sdata->trig == NULL) {
		dev_err(&indio_dev->dev, "failed to allocate iio trigger.\n");
		return -ENOMEM;
	}

	irq = sdata->get_irq_data_ready(indio_dev);
	irq_trig = irqd_get_trigger_type(irq_get_irq_data(irq));
	/*
	 * If the IRQ is triggered on falling edge, we need to mark the
	 * interrupt as active low, if the hardware supports this.
	 */
	if (irq_trig == IRQF_TRIGGER_FALLING) {
		if (!sdata->sensor_settings->drdy_irq.addr_ihl) {
			dev_err(&indio_dev->dev,
				"falling edge specified for IRQ but hardware "
				"only support rising edge, will request "
				"rising edge\n");
			irq_trig = IRQF_TRIGGER_RISING;
		} else {
			/* Set up INT active low i.e. falling edge */
			err = st_sensors_write_data_with_mask(indio_dev,
				sdata->sensor_settings->drdy_irq.addr_ihl,
				sdata->sensor_settings->drdy_irq.mask_ihl, 1);
			if (err < 0)
				goto iio_trigger_free;
			dev_info(&indio_dev->dev,
				 "interrupts on the falling edge\n");
		}
	} else if (irq_trig == IRQF_TRIGGER_RISING) {
		dev_info(&indio_dev->dev,
			 "interrupts on the rising edge\n");

	} else {
		dev_err(&indio_dev->dev,
		"unsupported IRQ trigger specified (%lx), only "
			"rising and falling edges supported, enforce "
			"rising edge\n", irq_trig);
		irq_trig = IRQF_TRIGGER_RISING;
	}

	/*
	 * If the interrupt pin is Open Drain, by definition this
	 * means that the interrupt line may be shared with other
	 * peripherals. But to do this we also need to have a status
	 * register and mask to figure out if this sensor was firing
	 * the IRQ or not, so we can tell the interrupt handle that
	 * it was "our" interrupt.
	 */
	if (sdata->int_pin_open_drain &&
	    sdata->sensor_settings->drdy_irq.addr_stat_drdy)
		irq_trig |= IRQF_SHARED;

	/* Let's create an interrupt thread masking the hard IRQ here */
	irq_trig |= IRQF_ONESHOT;

	err = request_threaded_irq(sdata->get_irq_data_ready(indio_dev),
			st_sensors_irq_handler,
			st_sensors_irq_thread,
			irq_trig,
			sdata->trig->name,
			sdata->trig);
	if (err) {
		dev_err(&indio_dev->dev, "failed to request trigger IRQ.\n");
		goto iio_trigger_free;
	}

	iio_trigger_set_drvdata(sdata->trig, indio_dev);
	sdata->trig->ops = trigger_ops;
	sdata->trig->dev.parent = sdata->dev;

	err = iio_trigger_register(sdata->trig);
	if (err < 0) {
		dev_err(&indio_dev->dev, "failed to register iio trigger.\n");
		goto iio_trigger_register_error;
	}
	indio_dev->trig = iio_trigger_get(sdata->trig);

	return 0;

iio_trigger_register_error:
	free_irq(sdata->get_irq_data_ready(indio_dev), sdata->trig);
iio_trigger_free:
	iio_trigger_free(sdata->trig);
	return err;
}
EXPORT_SYMBOL(st_sensors_allocate_trigger);

void st_sensors_deallocate_trigger(struct iio_dev *indio_dev)
{
	struct st_sensor_data *sdata = iio_priv(indio_dev);

	iio_trigger_unregister(sdata->trig);
	free_irq(sdata->get_irq_data_ready(indio_dev), sdata->trig);
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
