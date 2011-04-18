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
#include "adis16204.h"

/**
 * adis16204_data_rdy_trig_poll() the event handler for the data rdy trig
 **/
static irqreturn_t adis16204_data_rdy_trig_poll(int irq, void *private)
{
	disable_irq_nosync(irq);
	iio_trigger_poll(private, iio_get_time_ns());
	return IRQ_HANDLED;
}

static DEVICE_ATTR(name, S_IRUGO, iio_trigger_read_name, NULL);

static struct attribute *adis16204_trigger_attrs[] = {
	&dev_attr_name.attr,
	NULL,
};

static const struct attribute_group adis16204_trigger_attr_group = {
	.attrs = adis16204_trigger_attrs,
};

/**
 * adis16204_data_rdy_trigger_set_state() set datardy interrupt state
 **/
static int adis16204_data_rdy_trigger_set_state(struct iio_trigger *trig,
						bool state)
{
	struct adis16204_state *st = trig->private_data;
	struct iio_dev *indio_dev = st->indio_dev;

	dev_dbg(&indio_dev->dev, "%s (%d)\n", __func__, state);
	return adis16204_set_irq(&st->indio_dev->dev, state);
}

/**
 * adis16204_trig_try_reen() try renabling irq for data rdy trigger
 * @trig:	the datardy trigger
 **/
static int adis16204_trig_try_reen(struct iio_trigger *trig)
{
	struct adis16204_state *st = trig->private_data;
	enable_irq(st->us->irq);
	return 0;
}

int adis16204_probe_trigger(struct iio_dev *indio_dev)
{
	int ret;
	struct adis16204_state *st = indio_dev->dev_data;

	st->trig = iio_allocate_trigger();
	if (st->trig == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}
	ret = request_irq(st->us->irq,
			  adis16204_data_rdy_trig_poll,
			  IRQF_TRIGGER_RISING,
			  "adis16204",
			  st->trig);
	if (ret)
		goto error_free_trig;

	st->trig->name = kasprintf(GFP_KERNEL,
				"adis16204-dev%d",
				indio_dev->id);
	if (!st->trig->name) {
		ret = -ENOMEM;
		goto error_free_irq;
	}
	st->trig->dev.parent = &st->us->dev;
	st->trig->owner = THIS_MODULE;
	st->trig->private_data = st;
	st->trig->set_trigger_state = &adis16204_data_rdy_trigger_set_state;
	st->trig->try_reenable = &adis16204_trig_try_reen;
	st->trig->control_attrs = &adis16204_trigger_attr_group;
	ret = iio_trigger_register(st->trig);

	/* select default trigger */
	indio_dev->trig = st->trig;
	if (ret)
		goto error_free_trig_name;

	return 0;

error_free_trig_name:
	kfree(st->trig->name);
error_free_irq:
	free_irq(st->us->irq, st->trig);
error_free_trig:
	iio_free_trigger(st->trig);
error_ret:
	return ret;
}

void adis16204_remove_trigger(struct iio_dev *indio_dev)
{
	struct adis16204_state *state = indio_dev->dev_data;

	iio_trigger_unregister(state->trig);
	kfree(state->trig->name);
	free_irq(state->us->irq, state->trig);
	iio_free_trigger(state->trig);
}
