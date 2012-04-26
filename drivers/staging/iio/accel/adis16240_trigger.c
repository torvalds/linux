#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/export.h>

#include <linux/iio/iio.h>
#include <linux/iio/trigger.h>
#include "adis16240.h"

/**
 * adis16240_data_rdy_trig_poll() the event handler for the data rdy trig
 **/
static irqreturn_t adis16240_data_rdy_trig_poll(int irq, void *trig)
{
	iio_trigger_poll(trig, iio_get_time_ns());
	return IRQ_HANDLED;
}

/**
 * adis16240_data_rdy_trigger_set_state() set datardy interrupt state
 **/
static int adis16240_data_rdy_trigger_set_state(struct iio_trigger *trig,
						bool state)
{
	struct iio_dev *indio_dev = trig->private_data;

	dev_dbg(&indio_dev->dev, "%s (%d)\n", __func__, state);
	return adis16240_set_irq(indio_dev, state);
}

static const struct iio_trigger_ops adis16240_trigger_ops = {
	.owner = THIS_MODULE,
	.set_trigger_state = &adis16240_data_rdy_trigger_set_state,
};

int adis16240_probe_trigger(struct iio_dev *indio_dev)
{
	int ret;
	struct adis16240_state *st = iio_priv(indio_dev);

	st->trig = iio_trigger_alloc("adis16240-dev%d", indio_dev->id);
	if (st->trig == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}

	ret = request_irq(st->us->irq,
			  adis16240_data_rdy_trig_poll,
			  IRQF_TRIGGER_RISING,
			  "adis16240",
			  st->trig);
	if (ret)
		goto error_free_trig;

	st->trig->dev.parent = &st->us->dev;
	st->trig->ops = &adis16240_trigger_ops;
	st->trig->private_data = indio_dev;
	ret = iio_trigger_register(st->trig);

	/* select default trigger */
	indio_dev->trig = st->trig;
	if (ret)
		goto error_free_irq;

	return 0;

error_free_irq:
	free_irq(st->us->irq, st->trig);
error_free_trig:
	iio_trigger_free(st->trig);
error_ret:
	return ret;
}

void adis16240_remove_trigger(struct iio_dev *indio_dev)
{
	struct adis16240_state *st = iio_priv(indio_dev);

	iio_trigger_unregister(st->trig);
	free_irq(st->us->irq, st->trig);
	iio_trigger_free(st->trig);
}
