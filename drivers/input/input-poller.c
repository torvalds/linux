// SPDX-License-Identifier: GPL-2.0-only
/*
 * Support for polling mode for input devices.
 */

#include <linux/device.h>
#include <linux/input.h>
#include <linux/jiffies.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include "input-poller.h"

struct input_dev_poller {
	void (*poll)(struct input_dev *dev);

	unsigned int poll_interval; /* msec */
	unsigned int poll_interval_max; /* msec */
	unsigned int poll_interval_min; /* msec */

	struct input_dev *input;
	struct delayed_work work;
};

static void input_dev_poller_queue_work(struct input_dev_poller *poller)
{
	unsigned long delay;

	delay = msecs_to_jiffies(poller->poll_interval);
	if (delay >= HZ)
		delay = round_jiffies_relative(delay);

	queue_delayed_work(system_freezable_wq, &poller->work, delay);
}

static void input_dev_poller_work(struct work_struct *work)
{
	struct input_dev_poller *poller =
		container_of(work, struct input_dev_poller, work.work);

	poller->poll(poller->input);
	input_dev_poller_queue_work(poller);
}

void input_dev_poller_finalize(struct input_dev_poller *poller)
{
	if (!poller->poll_interval)
		poller->poll_interval = 500;
	if (!poller->poll_interval_max)
		poller->poll_interval_max = poller->poll_interval;
}

void input_dev_poller_start(struct input_dev_poller *poller)
{
	/* Only start polling if polling is enabled */
	if (poller->poll_interval > 0) {
		poller->poll(poller->input);
		input_dev_poller_queue_work(poller);
	}
}

void input_dev_poller_stop(struct input_dev_poller *poller)
{
	cancel_delayed_work_sync(&poller->work);
}

int input_setup_polling(struct input_dev *dev,
			void (*poll_fn)(struct input_dev *dev))
{
	struct input_dev_poller *poller;

	poller = kzalloc(sizeof(*poller), GFP_KERNEL);
	if (!poller) {
		/*
		 * We want to show message even though kzalloc() may have
		 * printed backtrace as knowing what instance of input
		 * device we were dealing with is helpful.
		 */
		dev_err(dev->dev.parent ?: &dev->dev,
			"%s: unable to allocate poller structure\n", __func__);
		return -ENOMEM;
	}

	INIT_DELAYED_WORK(&poller->work, input_dev_poller_work);
	poller->input = dev;
	poller->poll = poll_fn;

	dev->poller = poller;
	return 0;
}
EXPORT_SYMBOL(input_setup_polling);

static bool input_dev_ensure_poller(struct input_dev *dev)
{
	if (!dev->poller) {
		dev_err(dev->dev.parent ?: &dev->dev,
			"poller structure has not been set up\n");
		return false;
	}

	return true;
}

void input_set_poll_interval(struct input_dev *dev, unsigned int interval)
{
	if (input_dev_ensure_poller(dev))
		dev->poller->poll_interval = interval;
}
EXPORT_SYMBOL(input_set_poll_interval);

void input_set_min_poll_interval(struct input_dev *dev, unsigned int interval)
{
	if (input_dev_ensure_poller(dev))
		dev->poller->poll_interval_min = interval;
}
EXPORT_SYMBOL(input_set_min_poll_interval);

void input_set_max_poll_interval(struct input_dev *dev, unsigned int interval)
{
	if (input_dev_ensure_poller(dev))
		dev->poller->poll_interval_max = interval;
}
EXPORT_SYMBOL(input_set_max_poll_interval);

/* SYSFS interface */

static ssize_t input_dev_get_poll_interval(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct input_dev *input = to_input_dev(dev);

	return sprintf(buf, "%d\n", input->poller->poll_interval);
}

static ssize_t input_dev_set_poll_interval(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	struct input_dev *input = to_input_dev(dev);
	struct input_dev_poller *poller = input->poller;
	unsigned int interval;
	int err;

	err = kstrtouint(buf, 0, &interval);
	if (err)
		return err;

	if (interval < poller->poll_interval_min)
		return -EINVAL;

	if (interval > poller->poll_interval_max)
		return -EINVAL;

	mutex_lock(&input->mutex);

	poller->poll_interval = interval;

	if (input->users) {
		cancel_delayed_work_sync(&poller->work);
		if (poller->poll_interval > 0)
			input_dev_poller_queue_work(poller);
	}

	mutex_unlock(&input->mutex);

	return count;
}

static DEVICE_ATTR(poll, 0644,
		   input_dev_get_poll_interval, input_dev_set_poll_interval);

static ssize_t input_dev_get_poll_max(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct input_dev *input = to_input_dev(dev);

	return sprintf(buf, "%d\n", input->poller->poll_interval_max);
}

static DEVICE_ATTR(max, 0444, input_dev_get_poll_max, NULL);

static ssize_t input_dev_get_poll_min(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct input_dev *input = to_input_dev(dev);

	return sprintf(buf, "%d\n", input->poller->poll_interval_min);
}

static DEVICE_ATTR(min, 0444, input_dev_get_poll_min, NULL);

static umode_t input_poller_attrs_visible(struct kobject *kobj,
					  struct attribute *attr, int n)
{
	struct device *dev = kobj_to_dev(kobj);
	struct input_dev *input = to_input_dev(dev);

	return input->poller ? attr->mode : 0;
}

static struct attribute *input_poller_attrs[] = {
	&dev_attr_poll.attr,
	&dev_attr_max.attr,
	&dev_attr_min.attr,
	NULL
};

struct attribute_group input_poller_attribute_group = {
	.is_visible	= input_poller_attrs_visible,
	.attrs		= input_poller_attrs,
};
