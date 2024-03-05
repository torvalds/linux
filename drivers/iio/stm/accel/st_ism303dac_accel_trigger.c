// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics ism303dac driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2018 STMicroelectronics Inc.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/iio/iio.h>
#include <linux/iio/trigger.h>
#include <linux/interrupt.h>
#include <linux/iio/events.h>
#include <linux/version.h>

#include "st_ism303dac_accel.h"

static void ism303dac_event_management(struct ism303dac_data *cdata,
				       u8 int_reg_val)
{
	u8 status;

	/* Must read TAP_SRC to remove irq bits */
	cdata->tf->read(cdata, ISM303DAC_TAP_SRC_ADDR, 1, &status, true);

	if (CHECK_BIT(cdata->enabled_sensor, ISM303DAC_TAP) &&
	    (int_reg_val & ISM303DAC_TAP_MASK))
		iio_push_event(cdata->iio_sensors_dev[ISM303DAC_TAP],
			       IIO_UNMOD_EVENT_CODE(STM_IIO_TAP, 0,
			       IIO_EV_TYPE_THRESH,
			       IIO_EV_DIR_EITHER),
			       cdata->timestamp);

	if (CHECK_BIT(cdata->enabled_sensor, ISM303DAC_DOUBLE_TAP) &&
	    (int_reg_val & ISM303DAC_DOUBLE_TAP_MASK))
		iio_push_event(cdata->iio_sensors_dev[ISM303DAC_DOUBLE_TAP],
			       IIO_UNMOD_EVENT_CODE(STM_IIO_TAP_TAP, 0,
			       IIO_EV_TYPE_THRESH,
			       IIO_EV_DIR_EITHER),
			       cdata->timestamp);
}

static inline s64 st_ism303dac_ewma(s64 old, s64 new, int weight)
{
	s64 diff, incr;

	diff = new - old;
	incr = div_s64((ISM303DAC_EWMA_DIV - weight) *
			diff, ISM303DAC_EWMA_DIV);

	return old + incr;
}

static irqreturn_t ism303dac_irq_handler(int irq, void *private)
{
	u8 ewma_level;
	struct ism303dac_data *cdata = private;
	s64 ts;

	ewma_level = (cdata->common_odr >= 100) ? 120 : 96;
	ts = ism303dac_get_time_ns(cdata->iio_sensors_dev[ISM303DAC_ACCEL]);
	cdata->accel_deltatime = st_ism303dac_ewma(cdata->accel_deltatime,
						   ts - cdata->timestamp,
						   ewma_level);
	cdata->timestamp = ts;

	return IRQ_WAKE_THREAD;
}

static irqreturn_t ism303dac_irq_thread(int irq, void *private)
{
	u8 status;
	struct ism303dac_data *cdata = private;

	if (CHECK_BIT(cdata->enabled_sensor, ISM303DAC_ACCEL)) { 
		if (cdata->hwfifo_enabled) {
			mutex_lock(&cdata->fifo_lock);
			ism303dac_read_fifo(cdata, true);
			mutex_unlock(&cdata->fifo_lock);
		} else {
			cdata->tf->read(cdata, ISM303DAC_STATUS_DUP_ADDR, 1, &status, true);
			if (status & (ISM303DAC_DRDY_MASK))
				ism303dac_read_xyz(cdata);
		}
	}

	if (cdata->enabled_sensor & ~(1 << ISM303DAC_ACCEL)) {
		cdata->tf->read(cdata, ISM303DAC_STATUS_DUP_ADDR, 1, &status, true);
		if (status & ISM303DAC_EVENT_MASK)
			ism303dac_event_management(cdata, status);
	}

	return IRQ_HANDLED;
}

int ism303dac_allocate_triggers(struct ism303dac_data *cdata,
				const struct iio_trigger_ops *trigger_ops)
{
	int err, i, n;

	for (i = 0; i < ISM303DAC_SENSORS_NUMB; i++) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,13,0)
		cdata->iio_trig[i] = iio_trigger_alloc(cdata->dev,
					"%s-trigger",
					cdata->iio_sensors_dev[i]->name);
#else /* LINUX_VERSION_CODE */
		cdata->iio_trig[i] = iio_trigger_alloc("%s-trigger",
					cdata->iio_sensors_dev[i]->name);
#endif /* LINUX_VERSION_CODE */

		if (!cdata->iio_trig[i]) {
			dev_err(cdata->dev, "failed to allocate iio trigger.\n");
			err = -ENOMEM;

			goto deallocate_trigger;
		}
		iio_trigger_set_drvdata(cdata->iio_trig[i],
					cdata->iio_sensors_dev[i]);
		cdata->iio_trig[i]->ops = trigger_ops;
		cdata->iio_trig[i]->dev.parent = cdata->dev;
	}

	err = request_threaded_irq(cdata->irq,
				   ism303dac_irq_handler,
				   ism303dac_irq_thread,
				   IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
				   cdata->name, cdata);
	if (err)
		goto deallocate_trigger;

	for (n = 0; n < ISM303DAC_SENSORS_NUMB; n++) {
		err = iio_trigger_register(cdata->iio_trig[n]);
		if (err < 0) {
			dev_err(cdata->dev, "failed to register iio trigger.\n");

			goto free_irq;
		}

		cdata->iio_sensors_dev[n]->trig = cdata->iio_trig[n];
	}

	return 0;

free_irq:
	free_irq(cdata->irq, cdata);
	for (n--; n >= 0; n--)
		iio_trigger_unregister(cdata->iio_trig[n]);
deallocate_trigger:
	for (i--; i >= 0; i--)
		iio_trigger_free(cdata->iio_trig[i]);

	return err;
}

void ism303dac_deallocate_triggers(struct ism303dac_data *cdata)
{
	int i;

	free_irq(cdata->irq, cdata);

	for (i = 0; i < ISM303DAC_SENSORS_NUMB; i++)
		iio_trigger_unregister(cdata->iio_trig[i]);
}
