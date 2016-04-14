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

	err = request_threaded_irq(irq,
			iio_trigger_generic_data_rdy_poll,
			NULL,
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

MODULE_AUTHOR("Denis Ciocca <denis.ciocca@st.com>");
MODULE_DESCRIPTION("STMicroelectronics ST-sensors trigger");
MODULE_LICENSE("GPL v2");
