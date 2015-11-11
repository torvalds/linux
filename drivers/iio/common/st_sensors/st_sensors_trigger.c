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


int st_sensors_allocate_trigger(struct iio_dev *indio_dev,
				const struct iio_trigger_ops *trigger_ops)
{
	int err;
	struct st_sensor_data *sdata = iio_priv(indio_dev);

	sdata->trig = iio_trigger_alloc("%s-trigger", indio_dev->name);
	if (sdata->trig == NULL) {
		err = -ENOMEM;
		dev_err(&indio_dev->dev, "failed to allocate iio trigger.\n");
		goto iio_trigger_alloc_error;
	}

	err = request_threaded_irq(sdata->get_irq_data_ready(indio_dev),
			iio_trigger_generic_data_rdy_poll,
			NULL,
			IRQF_TRIGGER_RISING,
			sdata->trig->name,
			sdata->trig);
	if (err)
		goto request_irq_error;

	iio_trigger_set_drvdata(sdata->trig, indio_dev);
	sdata->trig->ops = trigger_ops;
	sdata->trig->dev.parent = sdata->dev;

	err = iio_trigger_register(sdata->trig);
	if (err < 0) {
		dev_err(&indio_dev->dev, "failed to register iio trigger.\n");
		goto iio_trigger_register_error;
	}
	indio_dev->trig = sdata->trig;

	return 0;

iio_trigger_register_error:
	free_irq(sdata->get_irq_data_ready(indio_dev), sdata->trig);
request_irq_error:
	iio_trigger_free(sdata->trig);
iio_trigger_alloc_error:
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
