/*
 *  linux/drivers/devfreq/governor_simpleondemand.c
 *
 *  Copyright (C) 2011 Samsung Electronics
 *	MyungJoo Ham <myungjoo.ham@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/slab.h>
#include <linux/device.h>
#include <linux/devfreq.h>
#include <linux/pm.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include "governor.h"

struct userspace_data {
	unsigned long user_frequency;
	bool valid;
};

static int devfreq_userspace_func(struct devfreq *df, unsigned long *freq)
{
	struct userspace_data *data = df->data;

	if (data->valid) {
		unsigned long adjusted_freq = data->user_frequency;

		if (df->max_freq && adjusted_freq > df->max_freq)
			adjusted_freq = df->max_freq;

		if (df->min_freq && adjusted_freq < df->min_freq)
			adjusted_freq = df->min_freq;

		*freq = adjusted_freq;
	} else {
		*freq = df->previous_freq; /* No user freq specified yet */
	}
	return 0;
}

static ssize_t store_freq(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct devfreq *devfreq = to_devfreq(dev);
	struct userspace_data *data;
	unsigned long wanted;
	int err = 0;


	mutex_lock(&devfreq->lock);
	data = devfreq->data;

	sscanf(buf, "%lu", &wanted);
	data->user_frequency = wanted;
	data->valid = true;
	err = update_devfreq(devfreq);
	if (err == 0)
		err = count;
	mutex_unlock(&devfreq->lock);
	return err;
}

static ssize_t show_freq(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct devfreq *devfreq = to_devfreq(dev);
	struct userspace_data *data;
	int err = 0;

	mutex_lock(&devfreq->lock);
	data = devfreq->data;

	if (data->valid)
		err = sprintf(buf, "%lu\n", data->user_frequency);
	else
		err = sprintf(buf, "undefined\n");
	mutex_unlock(&devfreq->lock);
	return err;
}

static DEVICE_ATTR(set_freq, 0644, show_freq, store_freq);
static struct attribute *dev_entries[] = {
	&dev_attr_set_freq.attr,
	NULL,
};
static struct attribute_group dev_attr_group = {
	.name	= "userspace",
	.attrs	= dev_entries,
};

static int userspace_init(struct devfreq *devfreq)
{
	int err = 0;
	struct userspace_data *data = kzalloc(sizeof(struct userspace_data),
					      GFP_KERNEL);

	if (!data) {
		err = -ENOMEM;
		goto out;
	}
	data->valid = false;
	devfreq->data = data;

	err = sysfs_create_group(&devfreq->dev.kobj, &dev_attr_group);
out:
	return err;
}

static void userspace_exit(struct devfreq *devfreq)
{
	sysfs_remove_group(&devfreq->dev.kobj, &dev_attr_group);
	kfree(devfreq->data);
	devfreq->data = NULL;
}

static int devfreq_userspace_handler(struct devfreq *devfreq,
			unsigned int event, void *data)
{
	int ret = 0;

	switch (event) {
	case DEVFREQ_GOV_START:
		ret = userspace_init(devfreq);
		break;
	case DEVFREQ_GOV_STOP:
		userspace_exit(devfreq);
		break;
	default:
		break;
	}

	return ret;
}

static struct devfreq_governor devfreq_userspace = {
	.name = "userspace",
	.get_target_freq = devfreq_userspace_func,
	.event_handler = devfreq_userspace_handler,
};

static int __init devfreq_userspace_init(void)
{
	return devfreq_add_governor(&devfreq_userspace);
}
subsys_initcall(devfreq_userspace_init);

static void __exit devfreq_userspace_exit(void)
{
	int ret;

	ret = devfreq_remove_governor(&devfreq_userspace);
	if (ret)
		pr_err("%s: failed remove governor %d\n", __func__, ret);

	return;
}
module_exit(devfreq_userspace_exit);
MODULE_LICENSE("GPL");
