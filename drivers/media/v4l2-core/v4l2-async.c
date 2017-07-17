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
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/types.h>

#include <media/v4l2-async.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

static int v4l2_async_notifier_call_bound(struct v4l2_async_notifier *n,
					  struct v4l2_subdev *subdev,
					  struct v4l2_async_subdev *asd)
{
	if (!n->ops || !n->ops->bound)
		return 0;

	return n->ops->bound(n, subdev, asd);
}

static void v4l2_async_notifier_call_unbind(struct v4l2_async_notifier *n,
					    struct v4l2_subdev *subdev,
					    struct v4l2_async_subdev *asd)
{
	if (!n->ops || !n->ops->unbind)
		return;

	n->ops->unbind(n, subdev, asd);
}

static int v4l2_async_notifier_call_complete(struct v4l2_async_notifier *n)
{
	if (!n->ops || !n->ops->complete)
		return 0;

	return n->ops->complete(n);
}

static bool match_i2c(struct v4l2_subdev *sd, struct v4l2_async_subdev *asd)
{
#if IS_ENABLED(CONFIG_I2C)
	struct i2c_client *client = i2c_verify_client(sd->dev);
	return client &&
		asd->match.i2c.adapter_id == client->adapter->nr &&
		asd->match.i2c.address == client->addr;
#else
	return false;
#endif
}

static bool match_devname(struct v4l2_subdev *sd,
			  struct v4l2_async_subdev *asd)
{
	return !strcmp(asd->match.device_name.name, dev_name(sd->dev));
}

static bool match_fwnode(struct v4l2_subdev *sd, struct v4l2_async_subdev *asd)
{
	return sd->fwnode == asd->match.fwnode.fwnode;
}

static bool match_custom(struct v4l2_subdev *sd, struct v4l2_async_subdev *asd)
{
	if (!asd->match.custom.match)
		/* Match always */
		return true;

	return asd->match.custom.match(sd->dev, asd);
}

static LIST_HEAD(subdev_list);
static LIST_HEAD(notifier_list);
static DEFINE_MUTEX(list_lock);

static struct v4l2_async_subdev *v4l2_async_find_match(
	struct v4l2_async_notifier *notifier, struct v4l2_subdev *sd)
{
	bool (*match)(struct v4l2_subdev *, struct v4l2_async_subdev *);
	struct v4l2_async_subdev *asd;

	list_for_each_entry(asd, &notifier->waiting, list) {
		/* bus_type has been verified valid before */
		switch (asd->match_type) {
		case V4L2_ASYNC_MATCH_CUSTOM:
			match = match_custom;
			break;
		case V4L2_ASYNC_MATCH_DEVNAME:
			match = match_devname;
			break;
		case V4L2_ASYNC_MATCH_I2C:
			match = match_i2c;
			break;
		case V4L2_ASYNC_MATCH_FWNODE:
			match = match_fwnode;
			break;
		default:
			/* Cannot happen, unless someone breaks us */
			WARN_ON(true);
			return NULL;
		}

		/* match cannot be NULL here */
		if (match(sd, asd))
			return asd;
	}

	return NULL;
}

static int v4l2_async_match_notify(struct v4l2_async_notifier *notifier,
				   struct v4l2_subdev *sd,
				   struct v4l2_async_subdev *asd)
{
	int ret;

	ret = v4l2_device_register_subdev(notifier->v4l2_dev, sd);
	if (ret < 0)
		return ret;

	ret = v4l2_async_notifier_call_bound(notifier, sd, asd);
	if (ret < 0) {
		v4l2_device_unregister_subdev(sd);
		return ret;
	}

	/* Remove from the waiting list */
	list_del(&asd->list);
	sd->asd = asd;
	sd->notifier = notifier;

	/* Move from the global subdevice list to notifier's done */
	list_move(&sd->async_list, &notifier->done);

	return 0;
}

static void v4l2_async_cleanup(struct v4l2_subdev *sd)
{
	v4l2_device_unregister_subdev(sd);
	/* Subdevice driver will reprobe and put the subdev back onto the list */
	list_del_init(&sd->async_list);
	sd->asd = NULL;
}

static void v4l2_async_notifier_unbind_all_subdevs(
	struct v4l2_async_notifier *notifier)
{
	struct v4l2_subdev *sd, *tmp;

	list_for_each_entry_safe(sd, tmp, &notifier->done, async_list) {
		v4l2_async_notifier_call_unbind(notifier, sd, sd->asd);
		v4l2_async_cleanup(sd);

		list_move(&sd->async_list, &subdev_list);
	}
}

int v4l2_async_notifier_register(struct v4l2_device *v4l2_dev,
				 struct v4l2_async_notifier *notifier)
{
	struct v4l2_subdev *sd, *tmp;
	struct v4l2_async_subdev *asd;
	int ret;
	int i;

	if (!v4l2_dev || !notifier->num_subdevs ||
	    notifier->num_subdevs > V4L2_MAX_SUBDEVS)
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
		case V4L2_ASYNC_MATCH_FWNODE:
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

	list_for_each_entry_safe(sd, tmp, &subdev_list, async_list) {
		int ret;

		asd = v4l2_async_find_match(notifier, sd);
		if (!asd)
			continue;

		ret = v4l2_async_match_notify(notifier, sd, asd);
		if (ret < 0) {
			mutex_unlock(&list_lock);
			return ret;
		}
	}

	if (list_empty(&notifier->waiting)) {
		ret = v4l2_async_notifier_call_complete(notifier);
		if (ret)
			goto err_complete;
	}

	/* Keep also completed notifiers on the list */
	list_add(&notifier->list, &notifier_list);

	mutex_unlock(&list_lock);

	return 0;

err_complete:
	v4l2_async_notifier_unbind_all_subdevs(notifier);

	mutex_unlock(&list_lock);

	return ret;
}
EXPORT_SYMBOL(v4l2_async_notifier_register);

void v4l2_async_notifier_unregister(struct v4l2_async_notifier *notifier)
{
	if (!notifier->v4l2_dev)
		return;

	mutex_lock(&list_lock);

	list_del(&notifier->list);

	v4l2_async_notifier_unbind_all_subdevs(notifier);

	mutex_unlock(&list_lock);

	notifier->v4l2_dev = NULL;
}
EXPORT_SYMBOL(v4l2_async_notifier_unregister);

void v4l2_async_notifier_cleanup(struct v4l2_async_notifier *notifier)
{
	unsigned int i;

	if (!notifier->max_subdevs)
		return;

	for (i = 0; i < notifier->num_subdevs; i++) {
		struct v4l2_async_subdev *asd = notifier->subdevs[i];

		switch (asd->match_type) {
		case V4L2_ASYNC_MATCH_FWNODE:
			fwnode_handle_put(asd->match.fwnode.fwnode);
			break;
		default:
			WARN_ON_ONCE(true);
			break;
		}

		kfree(asd);
	}

	notifier->max_subdevs = 0;
	notifier->num_subdevs = 0;

	kvfree(notifier->subdevs);
	notifier->subdevs = NULL;
}
EXPORT_SYMBOL_GPL(v4l2_async_notifier_cleanup);

int v4l2_async_register_subdev(struct v4l2_subdev *sd)
{
	struct v4l2_async_notifier *notifier;
	int ret;

	/*
	 * No reference taken. The reference is held by the device
	 * (struct v4l2_subdev.dev), and async sub-device does not
	 * exist independently of the device at any point of time.
	 */
	if (!sd->fwnode && sd->dev)
		sd->fwnode = dev_fwnode(sd->dev);

	mutex_lock(&list_lock);

	INIT_LIST_HEAD(&sd->async_list);

	list_for_each_entry(notifier, &notifier_list, list) {
		struct v4l2_async_subdev *asd = v4l2_async_find_match(notifier,
								      sd);
		int ret;

		if (!asd)
			continue;

		ret = v4l2_async_match_notify(notifier, sd, asd);
		if (ret)
			goto err_unlock;

		if (!list_empty(&notifier->waiting))
			goto out_unlock;

		ret = v4l2_async_notifier_call_complete(notifier);
		if (ret)
			goto err_cleanup;

		goto out_unlock;
	}

	/* None matched, wait for hot-plugging */
	list_add(&sd->async_list, &subdev_list);

out_unlock:
	mutex_unlock(&list_lock);

	return 0;

err_cleanup:
	v4l2_async_notifier_call_unbind(notifier, sd, sd->asd);
	v4l2_async_cleanup(sd);

err_unlock:
	mutex_unlock(&list_lock);

	return ret;
}
EXPORT_SYMBOL(v4l2_async_register_subdev);

void v4l2_async_unregister_subdev(struct v4l2_subdev *sd)
{
	mutex_lock(&list_lock);

	if (sd->asd) {
		struct v4l2_async_notifier *notifier = sd->notifier;

		list_add(&sd->asd->list, &notifier->waiting);

		v4l2_async_notifier_call_unbind(notifier, sd, sd->asd);
	}

	v4l2_async_cleanup(sd);

	mutex_unlock(&list_lock);
}
EXPORT_SYMBOL(v4l2_async_unregister_subdev);
