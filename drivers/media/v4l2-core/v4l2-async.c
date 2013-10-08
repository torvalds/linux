/*
 * V4L2 asynchronous subdevice registration API
 *
 * Copyright (C) 2012-2013, Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/types.h>

#include <media/v4l2-async.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>

static bool match_i2c(struct device *dev, struct v4l2_async_subdev *asd)
{
#if IS_ENABLED(CONFIG_I2C)
	struct i2c_client *client = i2c_verify_client(dev);
	return client &&
		asd->match.i2c.adapter_id == client->adapter->nr &&
		asd->match.i2c.address == client->addr;
#else
	return false;
#endif
}

static bool match_devname(struct device *dev, struct v4l2_async_subdev *asd)
{
	return !strcmp(asd->match.device_name.name, dev_name(dev));
}

static bool match_of(struct device *dev, struct v4l2_async_subdev *asd)
{
	return dev->of_node == asd->match.of.node;
}

static LIST_HEAD(subdev_list);
static LIST_HEAD(notifier_list);
static DEFINE_MUTEX(list_lock);

static struct v4l2_async_subdev *v4l2_async_belongs(struct v4l2_async_notifier *notifier,
						    struct v4l2_subdev *sd)
{
	struct v4l2_async_subdev *asd;
	bool (*match)(struct device *, struct v4l2_async_subdev *);

	list_for_each_entry(asd, &notifier->waiting, list) {
		/* bus_type has been verified valid before */
		switch (asd->match_type) {
		case V4L2_ASYNC_MATCH_CUSTOM:
			match = asd->match.custom.match;
			if (!match)
				/* Match always */
				return asd;
			break;
		case V4L2_ASYNC_MATCH_DEVNAME:
			match = match_devname;
			break;
		case V4L2_ASYNC_MATCH_I2C:
			match = match_i2c;
			break;
		case V4L2_ASYNC_MATCH_OF:
			match = match_of;
			break;
		default:
			/* Cannot happen, unless someone breaks us */
			WARN_ON(true);
			return NULL;
		}

		/* match cannot be NULL here */
		if (match(sd->dev, asd))
			return asd;
	}

	return NULL;
}

static int v4l2_async_test_notify(struct v4l2_async_notifier *notifier,
				  struct v4l2_subdev *sd,
				  struct v4l2_async_subdev *asd)
{
	int ret;

	/* Remove from the waiting list */
	list_del(&asd->list);
	sd->asd = asd;
	sd->notifier = notifier;

	if (notifier->bound) {
		ret = notifier->bound(notifier, sd, asd);
		if (ret < 0)
			return ret;
	}
	/* Move from the global subdevice list to notifier's done */
	list_move(&sd->async_list, &notifier->done);

	ret = v4l2_device_register_subdev(notifier->v4l2_dev, sd);
	if (ret < 0) {
		if (notifier->unbind)
			notifier->unbind(notifier, sd, asd);
		return ret;
	}

	if (list_empty(&notifier->waiting) && notifier->complete)
		return notifier->complete(notifier);

	return 0;
}

static void v4l2_async_cleanup(struct v4l2_subdev *sd)
{
	v4l2_device_unregister_subdev(sd);
	/* Subdevice driver will reprobe and put the subdev back onto the list */
	list_del_init(&sd->async_list);
	sd->asd = NULL;
	sd->dev = NULL;
}

int v4l2_async_notifier_register(struct v4l2_device *v4l2_dev,
				 struct v4l2_async_notifier *notifier)
{
	struct v4l2_subdev *sd, *tmp;
	struct v4l2_async_subdev *asd;
	int i;

	if (!notifier->num_subdevs || notifier->num_subdevs > V4L2_MAX_SUBDEVS)
		return -EINVAL;

	notifier->v4l2_dev = v4l2_dev;
	INIT_LIST_HEAD(&notifier->waiting);
	INIT_LIST_HEAD(&notifier->done);

	for (i = 0; i < notifier->num_subdevs; i++) {
		asd = notifier->subdevs[i];

		switch (asd->match_type) {
		case V4L2_ASYNC_MATCH_CUSTOM:
		case V4L2_ASYNC_MATCH_DEVNAME:
		case V4L2_ASYNC_MATCH_I2C:
		case V4L2_ASYNC_MATCH_OF:
			break;
		default:
			dev_err(notifier->v4l2_dev ? notifier->v4l2_dev->dev : NULL,
				"Invalid match type %u on %p\n",
				asd->match_type, asd);
			return -EINVAL;
		}
		list_add_tail(&asd->list, &notifier->waiting);
	}

	mutex_lock(&list_lock);

	/* Keep also completed notifiers on the list */
	list_add(&notifier->list, &notifier_list);

	list_for_each_entry_safe(sd, tmp, &subdev_list, async_list) {
		int ret;

		asd = v4l2_async_belongs(notifier, sd);
		if (!asd)
			continue;

		ret = v4l2_async_test_notify(notifier, sd, asd);
		if (ret < 0) {
			mutex_unlock(&list_lock);
			return ret;
		}
	}

	mutex_unlock(&list_lock);

	return 0;
}
EXPORT_SYMBOL(v4l2_async_notifier_register);

void v4l2_async_notifier_unregister(struct v4l2_async_notifier *notifier)
{
	struct v4l2_subdev *sd, *tmp;
	unsigned int notif_n_subdev = notifier->num_subdevs;
	unsigned int n_subdev = min(notif_n_subdev, V4L2_MAX_SUBDEVS);
	struct device *dev[n_subdev];
	int i = 0;

	if (!notifier->v4l2_dev)
		return;

	mutex_lock(&list_lock);

	list_del(&notifier->list);

	list_for_each_entry_safe(sd, tmp, &notifier->done, async_list) {
		dev[i] = get_device(sd->dev);

		v4l2_async_cleanup(sd);

		/* If we handled USB devices, we'd have to lock the parent too */
		device_release_driver(dev[i++]);

		if (notifier->unbind)
			notifier->unbind(notifier, sd, sd->asd);
	}

	mutex_unlock(&list_lock);

	while (i--) {
		struct device *d = dev[i];

		if (d && device_attach(d) < 0) {
			const char *name = "(none)";
			int lock = device_trylock(d);

			if (lock && d->driver)
				name = d->driver->name;
			dev_err(d, "Failed to re-probe to %s\n", name);
			if (lock)
				device_unlock(d);
		}
		put_device(d);
	}

	notifier->v4l2_dev = NULL;

	/*
	 * Don't care about the waiting list, it is initialised and populated
	 * upon notifier registration.
	 */
}
EXPORT_SYMBOL(v4l2_async_notifier_unregister);

int v4l2_async_register_subdev(struct v4l2_subdev *sd)
{
	struct v4l2_async_notifier *notifier;

	mutex_lock(&list_lock);

	INIT_LIST_HEAD(&sd->async_list);

	list_for_each_entry(notifier, &notifier_list, list) {
		struct v4l2_async_subdev *asd = v4l2_async_belongs(notifier, sd);
		if (asd) {
			int ret = v4l2_async_test_notify(notifier, sd, asd);
			mutex_unlock(&list_lock);
			return ret;
		}
	}

	/* None matched, wait for hot-plugging */
	list_add(&sd->async_list, &subdev_list);

	mutex_unlock(&list_lock);

	return 0;
}
EXPORT_SYMBOL(v4l2_async_register_subdev);

void v4l2_async_unregister_subdev(struct v4l2_subdev *sd)
{
	struct v4l2_async_notifier *notifier = sd->notifier;

	if (!sd->asd) {
		if (!list_empty(&sd->async_list))
			v4l2_async_cleanup(sd);
		return;
	}

	mutex_lock(&list_lock);

	list_add(&sd->asd->list, &notifier->waiting);

	v4l2_async_cleanup(sd);

	if (notifier->unbind)
		notifier->unbind(notifier, sd, sd->asd);

	mutex_unlock(&list_lock);
}
EXPORT_SYMBOL(v4l2_async_unregister_subdev);
