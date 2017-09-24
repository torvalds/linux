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
	if (!is_of_node(sd->fwnode) || !is_of_node(asd->match.fwnode.fwnode))
		return sd->fwnode == asd->match.fwnode.fwnode;

	return !of_node_cmp(of_node_full_name(to_of_node(sd->fwnode)),
			    of_node_full_name(
				    to_of_node(asd->match.fwnode.fwnode)));
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

/* Find the sub-device notifier registered by a sub-device driver. */
static struct v4l2_async_notifier *v4l2_async_find_subdev_notifier(
	struct v4l2_subdev *sd)
{
	struct v4l2_async_notifier *n;

	list_for_each_entry(n, &notifier_list, list)
		if (n->sd == sd)
			return n;

	return NULL;
}

/* Get v4l2_device related to the notifier if one can be found. */
static struct v4l2_device *v4l2_async_notifier_find_v4l2_dev(
	struct v4l2_async_notifier *notifier)
{
	while (notifier->parent)
		notifier = notifier->parent;

	return notifier->v4l2_dev;
}

/*
 * Return true if all child sub-device notifiers are complete, false otherwise.
 */
static bool v4l2_async_notifier_can_complete(
	struct v4l2_async_notifier *notifier)
{
	struct v4l2_subdev *sd;

	if (!list_empty(&notifier->waiting))
		return false;

	list_for_each_entry(sd, &notifier->done, async_list) {
		struct v4l2_async_notifier *subdev_notifier =
			v4l2_async_find_subdev_notifier(sd);

		if (subdev_notifier &&
		    !v4l2_async_notifier_can_complete(subdev_notifier))
			return false;
	}

	return true;
}

/* Complete all notifiers. Call on the root notifier. */
static int v4l2_async_notifier_complete(
	struct v4l2_async_notifier *notifier)
{
	struct v4l2_subdev *sd;

	list_for_each_entry(sd, &notifier->done, async_list) {
		struct v4l2_async_notifier *subdev_notifier =
			v4l2_async_find_subdev_notifier(sd);
		int ret;

		if (!subdev_notifier)
			continue;

		ret = v4l2_async_notifier_complete(subdev_notifier);
		if (ret)
			return ret;
	}

	return v4l2_async_notifier_call_complete(notifier);
}

/*
 * Complete notifiers if possible. This is done when all async sub-devices have
 * been bound; v4l2_device is also available then.
 */
static int v4l2_async_notifier_try_complete(
	struct v4l2_async_notifier *notifier)
{
	/* Quick check whether there are still more sub-devices here. */
	if (!list_empty(&notifier->waiting))
		return 0;

	/* Check the entire notifier tree; find the root notifier first. */
	while (notifier->parent)
		notifier = notifier->parent;

	/* This is root if it has v4l2_dev. */
	if (!notifier->v4l2_dev)
		return 0;

	/* Is everything ready? */
	if (!v4l2_async_notifier_can_complete(notifier))
		return 0;

	return v4l2_async_notifier_complete(notifier);
}

static int v4l2_async_notifier_try_all_subdevs(
	struct v4l2_async_notifier *notifier);

static int v4l2_async_match_notify(struct v4l2_async_notifier *notifier,
				   struct v4l2_device *v4l2_dev,
				   struct v4l2_subdev *sd,
				   struct v4l2_async_subdev *asd)
{
	struct v4l2_async_notifier *subdev_notifier;
	int ret;

	ret = v4l2_device_register_subdev(v4l2_dev, sd);
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

	/*
	 * See if the sub-device has a notifier. If it does, proceed
	 * with checking for its async sub-devices.
	 */
	subdev_notifier = v4l2_async_find_subdev_notifier(sd);
	if (subdev_notifier && !subdev_notifier->parent) {
		subdev_notifier->parent = notifier;
		ret = v4l2_async_notifier_try_all_subdevs(subdev_notifier);
		if (ret)
			return ret;
	}

	return 0;
}

/* Test all async sub-devices in a notifier for a match. */
static int v4l2_async_notifier_try_all_subdevs(
	struct v4l2_async_notifier *notifier)
{
	struct v4l2_device *v4l2_dev =
		v4l2_async_notifier_find_v4l2_dev(notifier);
	struct v4l2_subdev *sd;

	if (!v4l2_dev)
		return 0;

again:
	list_for_each_entry(sd, &subdev_list, async_list) {
		struct v4l2_async_subdev *asd;
		int ret;

		asd = v4l2_async_find_match(notifier, sd);
		if (!asd)
			continue;

		ret = v4l2_async_match_notify(notifier, v4l2_dev, sd, asd);
		if (ret < 0)
			return ret;

		/*
		 * v4l2_async_match_notify() may lead to registering a
		 * new notifier and thus changing the async subdevs
		 * list. In order to proceed safely from here, restart
		 * parsing the list from the beginning.
		 */
		goto again;
	}

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

/* See if an fwnode can be found in a notifier's lists. */
static bool __v4l2_async_notifier_fwnode_has_async_subdev(
	struct v4l2_async_notifier *notifier, struct fwnode_handle *fwnode)
{
	struct v4l2_async_subdev *asd;
	struct v4l2_subdev *sd;

	list_for_each_entry(asd, &notifier->waiting, list) {
		if (asd->match_type != V4L2_ASYNC_MATCH_FWNODE)
			continue;

		if (asd->match.fwnode.fwnode == fwnode)
			return true;
	}

	list_for_each_entry(sd, &notifier->done, async_list) {
		if (WARN_ON(!sd->asd))
			continue;

		if (sd->asd->match_type != V4L2_ASYNC_MATCH_FWNODE)
			continue;

		if (sd->asd->match.fwnode.fwnode == fwnode)
			return true;
	}

	return false;
}

/*
 * Find out whether an async sub-device was set up for an fwnode already or
 * whether it exists in a given notifier before @this_index.
 */
static bool v4l2_async_notifier_fwnode_has_async_subdev(
	struct v4l2_async_notifier *notifier, struct fwnode_handle *fwnode,
	unsigned int this_index)
{
	unsigned int j;

	lockdep_assert_held(&list_lock);

	/* Check that an fwnode is not being added more than once. */
	for (j = 0; j < this_index; j++) {
		struct v4l2_async_subdev *asd = notifier->subdevs[this_index];
		struct v4l2_async_subdev *other_asd = notifier->subdevs[j];

		if (other_asd->match_type == V4L2_ASYNC_MATCH_FWNODE &&
		    asd->match.fwnode.fwnode ==
		    other_asd->match.fwnode.fwnode)
			return true;
	}

	/* Check than an fwnode did not exist in other notifiers. */
	list_for_each_entry(notifier, &notifier_list, list)
		if (__v4l2_async_notifier_fwnode_has_async_subdev(
			    notifier, fwnode))
			return true;

	return false;
}

static int __v4l2_async_notifier_register(struct v4l2_async_notifier *notifier)
{
	struct device *dev =
		notifier->v4l2_dev ? notifier->v4l2_dev->dev : NULL;
	struct v4l2_async_subdev *asd;
	int ret;
	int i;

	if (notifier->num_subdevs > V4L2_MAX_SUBDEVS)
		return -EINVAL;

	INIT_LIST_HEAD(&notifier->waiting);
	INIT_LIST_HEAD(&notifier->done);

	if (!notifier->num_subdevs) {
		int ret;

		ret = v4l2_async_notifier_call_complete(notifier);
		notifier->v4l2_dev = NULL;
		notifier->sd = NULL;

		return ret;
	}

	mutex_lock(&list_lock);

	for (i = 0; i < notifier->num_subdevs; i++) {
		asd = notifier->subdevs[i];

		switch (asd->match_type) {
		case V4L2_ASYNC_MATCH_CUSTOM:
		case V4L2_ASYNC_MATCH_DEVNAME:
		case V4L2_ASYNC_MATCH_I2C:
			break;
		case V4L2_ASYNC_MATCH_FWNODE:
			if (v4l2_async_notifier_fwnode_has_async_subdev(
				    notifier, asd->match.fwnode.fwnode, i)) {
				dev_err(dev,
					"fwnode has already been registered or in notifier's subdev list\n");
				ret = -EEXIST;
				goto out_unlock;
			}
			break;
		default:
			dev_err(dev, "Invalid match type %u on %p\n",
				asd->match_type, asd);
			ret = -EINVAL;
			goto out_unlock;
		}
		list_add_tail(&asd->list, &notifier->waiting);
	}

	ret = v4l2_async_notifier_try_all_subdevs(notifier);
	if (ret)
		goto out_unlock;

	ret = v4l2_async_notifier_try_complete(notifier);

	/* Keep also completed notifiers on the list */
	list_add(&notifier->list, &notifier_list);

out_unlock:
	mutex_unlock(&list_lock);

	return ret;
}

int v4l2_async_notifier_register(struct v4l2_device *v4l2_dev,
				 struct v4l2_async_notifier *notifier)
{
	if (WARN_ON(!v4l2_dev || notifier->sd))
		return -EINVAL;

	notifier->v4l2_dev = v4l2_dev;

	return __v4l2_async_notifier_register(notifier);
}
EXPORT_SYMBOL(v4l2_async_notifier_register);

int v4l2_async_subdev_notifier_register(struct v4l2_subdev *sd,
					struct v4l2_async_notifier *notifier)
{
	if (WARN_ON(!sd || notifier->v4l2_dev))
		return -EINVAL;

	notifier->sd = sd;

	return __v4l2_async_notifier_register(notifier);
}
EXPORT_SYMBOL(v4l2_async_subdev_notifier_register);

/* Unbind all sub-devices in the notifier tree. */
static void v4l2_async_notifier_unbind_all_subdevs(
	struct v4l2_async_notifier *notifier)
{
	struct v4l2_subdev *sd, *tmp;

	list_for_each_entry_safe(sd, tmp, &notifier->done, async_list) {
		struct v4l2_async_notifier *subdev_notifier =
			v4l2_async_find_subdev_notifier(sd);

		if (subdev_notifier)
			v4l2_async_notifier_unbind_all_subdevs(subdev_notifier);

		v4l2_async_cleanup(sd);

		v4l2_async_notifier_call_unbind(notifier, sd, sd->asd);

		list_move(&sd->async_list, &subdev_list);
	}

	notifier->parent = NULL;
}

void v4l2_async_notifier_unregister(struct v4l2_async_notifier *notifier)
{
	if (!notifier->v4l2_dev && !notifier->sd)
		return;

	mutex_lock(&list_lock);

	v4l2_async_notifier_unbind_all_subdevs(notifier);

	notifier->sd = NULL;
	notifier->v4l2_dev = NULL;

	list_del(&notifier->list);

	mutex_unlock(&list_lock);
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
		struct v4l2_device *v4l2_dev =
			v4l2_async_notifier_find_v4l2_dev(notifier);
		struct v4l2_async_subdev *asd;
		int ret;

		if (!v4l2_dev)
			continue;

		asd = v4l2_async_find_match(notifier, sd);
		if (!asd)
			continue;

		ret = v4l2_async_match_notify(notifier, v4l2_dev, sd, asd);

		if (!ret)
			ret = v4l2_async_notifier_try_complete(notifier);

		mutex_unlock(&list_lock);
		return ret;
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

	if (sd->subdev_notifier)
		v4l2_async_notifier_unregister(sd->subdev_notifier);
	v4l2_async_notifier_cleanup(sd->subdev_notifier);
	kfree(sd->subdev_notifier);

	if (!sd->asd) {
		if (!list_empty(&sd->async_list))
			v4l2_async_cleanup(sd);
		return;
	}

	mutex_lock(&list_lock);

	list_add(&sd->asd->list, &notifier->waiting);

	v4l2_async_cleanup(sd);

	v4l2_async_notifier_call_unbind(notifier, sd, sd->asd);

	mutex_unlock(&list_lock);
}
EXPORT_SYMBOL(v4l2_async_unregister_subdev);
