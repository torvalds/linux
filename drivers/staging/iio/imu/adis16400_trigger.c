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
#include "adis16400.h"

/**
 * adis16400_data_rdy_trig_poll() the event handler for the data rdy trig
 **/
static int adis16400_data_rdy_trig_poll(struct iio_dev *dev_info,
				       int index,
				       s64 timestamp,
				       int no_test)
{
	struct adis16400_state *st = iio_dev_get_devdata(dev_info);
	struct iio_trigger *trig = st->trig;

	iio_trigger_poll(trig, timestamp);

	return IRQ_HANDLED;
}

IIO_EVENT_SH(data_rdy_trig, &adis16400_data_rdy_trig_poll);

static IIO_TRIGGER_NAME_ATTR;

static struct attribute *adis16400_trigger_attrs[] = {
	&dev_attr_name.attr,
	NULL,
};

static const struct attribute_group adis16400_trigger_attr_group = {
	.attrs = adis16400_trigger_attrs,
};

/**
 * adis16400_data_rdy_trigger_set_state() set datardy interrupt state
 **/
static int adis16400_data_rdy_trigger_set_state(struct iio_trigger *trig,
						bool state)
{
	struct adis16400_state *st = trig->private_data;
	struct iio_dev *indio_dev = st->indio_dev;
	int ret = 0;

	dev_dbg(&indio_dev->dev, "%s (%d)\n", __func__, state);
	ret = adis16400_set_irq(&st->indio_dev->dev, state);
	if (state == false) {
		iio_remove_event_from_list(&iio_event_data_rdy_trig,
					   &indio_dev->interrupts[0]
					   ->ev_list);
		/* possible quirk with handler currently worked around
		   by ensuring the work queue is empty */
		flush_scheduled_work();
	} else {
		iio_add_event_to_list(&iio_event_data_rdy_trig,
				      &indio_dev->interrupts[0]->ev_list);
	}
	return ret;
}

/**
 * adis16400_trig_try_reen() try renabling irq for data rdy trigger
 * @trig:	the datardy trigger
 **/
static int adis16400_trig_try_reen(struct iio_trigger *trig)
{
	struct adis16400_state *st = trig->private_data;
	enable_irq(st->us->irq);
	/* irq reenabled so success! */
	return 0;
}

int adis16400_probe_trigger(struct iio_dev *indio_dev)
{
	int ret;
	struct adis16400_state *st = indio_dev->dev_data;

	st->trig = iio_allocate_trigger();
	st->trig->name = kasprintf(GFP_KERNEL,
				   "adis16400-dev%d",
				   indio_dev->id);
	if (!st->trig->name) {
		ret = -ENOMEM;
		goto error_free_trig;
	}
	st->trig->dev.parent = &st->us->dev;
	st->trig->owner = THIS_MODULE;
	st->trig->private_data = st;
	st->trig->set_trigger_state = &adis16400_data_rdy_trigger_set_state;
	st->trig->try_reenable = &adis16400_trig_try_reen;
	st->trig->control_attrs = &adis16400_trigger_attr_group;
	ret = iio_trigger_register(st->trig);

	/* select default trigger */
	indio_dev->trig = st->trig;
	if (ret)
		goto error_free_trig_name;

	return 0;

error_free_trig_name:
	kfree(st->trig->name);
error_free_trig:
	iio_free_trigger(st->trig);

	return ret;
}

void adis16400_remove_trigger(struct iio_dev *indio_dev)
{
	struct adis16400_state *state = indio_dev->dev_data;

	iio_trigger_unregister(state->trig);
	kfree(state->trig->name);
	iio_free_trigger(state->trig);
}
