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
#include "adis16209.h"

/**
 * adis16209_data_rdy_trig_poll() the event handler for the data rdy trig
 **/
static irqreturn_t adis16209_data_rdy_trig_poll(int irq, void *trig)
{
	iio_trigger_poll(trig, iio_get_time_ns());
	return IRQ_HANDLED;
}

/**
 * adis16209_data_rdy_trigger_set_state() set datardy interrupt state
 **/
static int adis16209_data_rdy_trigger_set_state(struct iio_trigger *trig,
						bool state)
{
	struct iio_dev *indio_dev = trig->private_data;

	dev_dbg(&indio_dev->dev, "%s (%d)\n", __func__, state);
	return adis16209_set_irq(indio_dev, state);
}

int adis16209_probe_trigger(struct iio_dev *indio_dev)
{
	int ret;
	struct adis16209_state *st = iio_priv(indio_dev);

	st->trig = iio_allocate_trigger("adis16209-dev%d", indio_dev->id);
	if (st->trig == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}

	ret = request_irq(st->us->irq,
			  adis16209_data_rdy_trig_poll,
			  IRQF_TRIGGER_RISING,
			  "adis16209",
			  st->trig);
	if (ret)
		goto error_free_trig;
	st->trig->dev.parent = &st->us->dev;
	st->trig->owner = THIS_MODULE;
	st->trig->private_data = indio_dev;
	st->trig->set_trigger_state = &adis16209_data_rdy_trigger_set_state;
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

void adis16209_remove_trigger(struct iio_dev *indio_dev)
{
	struct adis16209_state *st = iio_priv(indio_dev);

	iio_trigger_unregister(st->trig);
	free_irq(st->us->irq, st->trig);
	iio_free_trigger(st->trig);
}
