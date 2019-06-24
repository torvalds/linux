// SPDX-License-Identifier: GPL-2.0-only
/*
 * V4L2 asynchronous subdevice registration API
 *
 * Copyright (C) 2012-2013, Guennadi Liakhovetski <g.liakhovetski@gmx.de>
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
	return !strcmp(asd->match.device_name, dev_name(sd->dev));
}

static bool match_fwnode(struct v4l2_subdev *sd, struct v4l2_async_subdev *asd)
{
	return sd->fwnode == asd->match.fwnode;
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

static struct v4l2_async_subdev *
v4l2_async_find_match(struct v4l2_async_notifier *notifier,
		      struct v4l2_subdev *sd)
{
	bool (*match)(struct v4l2_subdev *sd, struct v4l2_async_subdev *asd);
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

/* Compare two async sub-device descriptors for equivalence */
static bool asd_equal(struct v4l2_async_subdev *asd_x,
		      struct v4l2_async_subdev *asd_y)
{
	if (asd_x->match_type != asd_y->match_type)
		return false;

	switch (asd_x->match_type) {
	case V4L2_ASYNC_MATCH_DEVNAME:
		return strcmp(asd_x->match.device_name,
			      asd_y->match.device_name) == 0;
	case V4L2_ASYNC_MATCH_I2C:
		return asd_x->match.i2c.adapter_id ==
			asd_y->match.i2c.adapter_id &&
			asd_x->match.i2c.address ==
			asd_y->match.i2c.address;
	case V4L2_ASYNC_MATCH_FWNODE:
		return asd_x->match.fwnode == asd_y->match.fwnode;
	default:
		break;
	}

	return false;
}

/* Find the sub-device notifier registered by a sub-device driver. */
static struct v4l2_async_notifier *
v4l2_async_find_subdev_notifier(struct v4l2_subdev *sd)
{
	struct v4l2_async_notifier *n;

	list_for_each_entry(n, &notifier_list, list)
		if (n->sd == sd)
			return n;

	return NULL;
}

/* Get v4l2_device related to the notifier if one can be found. */
static struct v4l2_device *
v4l2_async_notifier_find_v4l2_dev(struct v4l2_async_notifier *notifier)
{
	while (notifier->parent)
		notifier = notifier->parent;

	return notifier->v4l2_dev;
}

/*
 * Return true if all child sub-device notifiers are complete, false otherwise.
 */
static bool
v4l2_async_notifier_can_complete(struct v4l2_async_notifier *notifier)
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

/*
 * Complete the master notifier if possible. This is done when all async
 * sub-devices have been bound; v4l2_device is also available then.
 */
static int
v4l2_async_notifier_try_complete(struct v4l2_async_notifier *notifier)
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

	return v4l2_async_notifier_call_complete(notifier);
}

static int
v4l2_async_notifier_try_all_subdevs(struct v4l2_async_notifier *notifier);

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
	 * See if the sub-device has a notifier. If not, return here.
	 */
	subdev_notifier = v4l2_async_find_subdev_notifier(sd);
	if (!subdev_notifier || subdev_notifier->parent)
		return 0;

	/*
	 * Proceed with checking for the sub-device notifier's async
	 * sub-devices, and return the result. The error will be handled by the
	 * caller.
	 */
	subdev_notifier->parent = notifier;

	return v4l2_async_notifier_try_all_subdevs(subdev_notifier);
}

/* Test all async sub-devices in a notifier for a match. */
static int
v4l2_async_notifier_try_all_subdevs(struct v4l2_async_notifier *notifier)
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
	/*
	 * Subdevice driver will reprobe and put the subdev back
	 * onto the list
	 */
	list_del_init(&sd->async_list);
	sd->asd = NULL;
}

/* Unbind all sub-devices in the notifier tree. */
static void
v4l2_async_notifier_unbind_all_subdevs(struct v4l2_async_notifier *notifier)
{
	struct v4l2_subdev *sd, *tmp;

	list_for_each_entry_safe(sd, tmp, &notifier->done, async_list) {
		struct v4l2_async_notifier *subdev_notifier =
			v4l2_async_find_subdev_notifier(sd);

		if (subdev_notifier)
			v4l2_async_notifier_unbind_all_subdevs(subdev_notifier);

		v4l2_async_notifier_call_unbind(notifier, sd, sd->asd);
		v4l2_async_cleanup(sd);

		list_move(&sd->async_list, &subdev_list);
	}

	notifier->parent = NULL;
}

/* See if an async sub-device can be found in a notifier's lists. */
static bool
__v4l2_async_notifier_has_async_subdev(struct v4l2_async_notifier *notifier,
				       struct v4l2_async_subdev *asd)
{
	struct v4l2_async_subdev *asd_y;
	struct v4l2_subdev *sd;

	list_for_each_entry(asd_y, &notifier->waiting, list)
		if (asd_equal(asd, asd_y))
			return true;

	list_for_each_entry(sd, &notifier->done, async_list) {
		if (WARN_ON(!sd->asd))
			continue;

		if (asd_equal(asd, sd->asd))
			return true;
	}

	return false;
}

/*
 * Find out whether an async sub-device was set up already or
 * whether it exists in a given notifier before @this_index.
 * If @this_index < 0, search the notifier's entire @asd_list.
 */
static bool
v4l2_async_notifier_has_async_subdev(struct v4l2_async_notifier *notifier,
				     struct v4l2_async_subdev *asd,
				     int this_index)
{
	struct v4l2_async_subdev *asd_y;
	int j = 0;

	lockdep_assert_held(&list_lock);

	/* Check that an asd is not being added more than once. */
	list_for_each_entry(asd_y, &notifier->asd_list, asd_list) {
		if (this_index >= 0 && j++ >= this_index)
			break;
		if (asd_equal(asd, asd_y))
			return true;
	}

	/* Check that an asd does not exist in other notifiers. */
	list_for_each_entry(notifier, &notifier_list, list)
		if (__v4l2_async_notifier_has_async_subdev(notifier, asd))
			return true;

	return false;
}

static int v4l2_async_notifier_asd_valid(struct v4l2_async_notifier *notifier,
					 struct v4l2_async_subdev *asd,
					 int this_index)
{
	struct device *dev =
		notifier->v4l2_dev ? notifier->v4l2_dev->dev : NULL;

	if (!asd)
		return -EINVAL;

	switch (asd->match_type) {
	case V4L2_ASYNC_MATCH_CUSTOM:
	case V4L2_ASYNC_MATCH_DEVNAME:
	case V4L2_ASYNC_MATCH_I2C:
	case V4L2_ASYNC_MATCH_FWNODE:
		if (v4l2_async_notifier_has_async_subdev(notifier, asd,
							 this_index)) {
			dev_dbg(dev, "subdev descriptor already listed in this or other notifiers\n");
			return -EEXIST;
		}
		break;
	default:
		dev_err(dev, "Invalid match type %u on %p\n",
			asd->match_type, asd);
		return -EINVAL;
	}

	return 0;
}

void v4l2_async_notifier_init(struct v4l2_async_notifier *notifier)
{
	INIT_LIST_HEAD(&notifier->asd_list);
}
EXPORT_SYMBOL(v4l2_async_notifier_init);

static int __v4l2_async_notifier_register(struct v4l2_async_notifier *notifier)
{
	struct v4l2_async_subdev *asd;
	int ret, i = 0;

	INIT_LIST_HEAD(&notifier->waiting);
	INIT_LIST_HEAD(&notifier->done);

	mutex_lock(&list_lock);

	list_for_each_entry(asd, &notifier->asd_list, asd_list) {
		ret = v4l2_async_notifier_asd_valid(notifier, asd, i++);
		if (ret)
			goto err_unlock;

		list_add_tail(&asd->list, &notifier->waiting);
	}

	ret = v4l2_async_notifier_try_all_subdevs(notifier);
	if (ret < 0)
		goto err_unbind;

	ret = v4l2_async_notifier_try_complete(notifier);
	if (ret < 0)
		goto err_unbind;

	/* Keep also completed notifiers on the list */
	list_add(&notifier->list, &notifier_list);

	mutex_unlock(&list_lock);

	return 0;

err_unbind:
	/*
	 * On failure, unbind all sub-devices registered through this notifier.
	 */
	v4l2_async_notifier_unbind_all_subdevs(notifier);

err_unlock:
	mutex_unlock(&list_lock);

	return ret;
}

int v4l2_async_notifier_register(struct v4l2_device *v4l2_dev,
				 struct v4l2_async_notifier *notifier)
{
	int ret;

	if (WARN_ON(!v4l2_dev || notifier->sd))
		return -EINVAL;

	notifier->v4l2_dev = v4l2_dev;

	ret = __v4l2_async_notifier_register(notifier);
	if (ret)
		notifier->v4l2_dev = NULL;

	return ret;
}
EXPORT_SYMBOL(v4l2_async_notifier_register);

int v4l2_async_subdev_notifier_register(struct v4l2_subdev *sd,
					struct v4l2_async_notifier *notifier)
{
	int ret;

	if (WARN_ON(!sd || notifier->v4l2_dev))
		return -EINVAL;

	notifier->sd = sd;

	ret = __v4l2_async_notifier_register(notifier);
	if (ret)
		notifier->sd = NULL;

	return ret;
}
EXPORT_SYMBOL(v4l2_async_subdev_notifier_register);

static void
__v4l2_async_notifier_unregister(struct v4l2_async_notifier *notifier)
{
	if (!notifier || (!notifier->v4l2_dev && !notifier->sd))
		return;

	v4l2_async_notifier_unbind_all_subdevs(notifier);

	notifier->sd = NULL;
	notifier->v4l2_dev = NULL;

	list_del(&notifier->list);
}

void v4l2_async_notifier_unregister(struct v4l2_async_notifier *notifier)
{
	mutex_lock(&list_lock);

	__v4l2_async_notifier_unregister(notifier);

	mutex_unlock(&list_lock);
}
EXPORT_SYMBOL(v4l2_async_notifier_unregister);

static void __v4l2_async_notifier_cleanup(struct v4l2_async_notifier *notifier)
{
	struct v4l2_async_subdev *asd, *tmp;

	if (!notifier)
		return;

	list_for_each_entry_safe(asd, tmp, &notifier->asd_list, asd_list) {
		switch (asd->match_type) {
		case V4L2_ASYNC_MATCH_FWNODE:
			fwnode_handle_put(asd->match.fwnode);
			break;
		default:
			break;
		}

		list_del(&asd->asd_list);
		kfree(asd);
	}
}

void v4l2_async_notifier_cleanup(struct v4l2_async_notifier *notifier)
{
	mutex_lock(&list_lock);

	__v4l2_async_notifier_cleanup(notifier);

	mutex_unlock(&list_lock);
}
EXPORT_SYMBOL_GPL(v4l2_async_notifier_cleanup);

int v4l2_async_notifier_add_subdev(struct v4l2_async_notifier *notifier,
				   struct v4l2_async_subdev *asd)
{
	int ret;

	mutex_lock(&list_lock);

	ret = v4l2_async_notifier_asd_valid(notifier, asd, -1);
	if (ret)
		goto unlock;

	list_add_tail(&asd->asd_list, &notifier->asd_list);

unlock:
	mutex_unlock(&list_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(v4l2_async_notifier_add_subdev);

struct v4l2_async_subdev *
v4l2_async_notifier_add_fwnode_subdev(struct v4l2_async_notifier *notifier,
				      struct fwnode_handle *fwnode,
				      unsigned int asd_struct_size)
{
	struct v4l2_async_subdev *asd;
	int ret;

	asd = kzalloc(asd_struct_size, GFP_KERNEL);
	if (!asd)
		return ERR_PTR(-ENOMEM);

	asd->match_type = V4L2_ASYNC_MATCH_FWNODE;
	asd->match.fwnode = fwnode;

	ret = v4l2_async_notifier_add_subdev(notifier, asd);
	if (ret) {
		kfree(asd);
		return ERR_PTR(ret);
	}

	return asd;
}
EXPORT_SYMBOL_GPL(v4l2_async_notifier_add_fwnode_subdev);

struct v4l2_async_subdev *
v4l2_async_notifier_add_i2c_subdev(struct v4l2_async_notifier *notifier,
				   int adapter_id, unsigned short address,
				   unsigned int asd_struct_size)
{
	struct v4l2_async_subdev *asd;
	int ret;

	asd = kzalloc(asd_struct_size, GFP_KERNEL);
	if (!asd)
		return ERR_PTR(-ENOMEM);

	asd->match_type = V4L2_ASYNC_MATCH_I2C;
	asd->match.i2c.adapter_id = adapter_id;
	asd->match.i2c.address = address;

	ret = v4l2_async_notifier_add_subdev(notifier, asd);
	if (ret) {
		kfree(asd);
		return ERR_PTR(ret);
	}

	return asd;
}
EXPORT_SYMBOL_GPL(v4l2_async_notifier_add_i2c_subdev);

struct v4l2_async_subdev *
v4l2_async_notifier_add_devname_subdev(struct v4l2_async_notifier *notifier,
				       const char *device_name,
				       unsigned int asd_struct_size)
{
	struct v4l2_async_subdev *asd;
	int ret;

	asd = kzalloc(asd_struct_size, GFP_KERNEL);
	if (!asd)
		return ERR_PTR(-ENOMEM);

	asd->match_type = V4L2_ASYNC_MATCH_DEVNAME;
	asd->match.device_name = device_name;

	ret = v4l2_async_notifier_add_subdev(notifier, asd);
	if (ret) {
		kfree(asd);
		return ERR_PTR(ret);
	}

	return asd;
}
EXPORT_SYMBOL_GPL(v4l2_async_notifier_add_devname_subdev);

int v4l2_async_register_subdev(struct v4l2_subdev *sd)
{
	struct v4l2_async_notifier *subdev_notifier;
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
		struct v4l2_device *v4l2_dev =
			v4l2_async_notifier_find_v4l2_dev(notifier);
		struct v4l2_async_subdev *asd;

		if (!v4l2_dev)
			continue;

		asd = v4l2_async_find_match(notifier, sd);
		if (!asd)
			continue;

		ret = v4l2_async_match_notify(notifier, v4l2_dev, sd, asd);
		if (ret)
			goto err_unbind;

		ret = v4l2_async_notifier_try_complete(notifier);
		if (ret)
			goto err_unbind;

		goto out_unlock;
	}

	/* None matched, wait for hot-plugging */
	list_add(&sd->async_list, &subdev_list);

out_unlock:
	mutex_unlock(&list_lock);

	return 0;

err_unbind:
	/*
	 * Complete failed. Unbind the sub-devices bound through registering
	 * this async sub-device.
	 */
	subdev_notifier = v4l2_async_find_subdev_notifier(sd);
	if (subdev_notifier)
		v4l2_async_notifier_unbind_all_subdevs(subdev_notifier);

	if (sd->asd)
		v4l2_async_notifier_call_unbind(notifier, sd, sd->asd);
	v4l2_async_cleanup(sd);

	mutex_unlock(&list_lock);

	return ret;
}
EXPORT_SYMBOL(v4l2_async_register_subdev);

void v4l2_async_unregister_subdev(struct v4l2_subdev *sd)
{
	mutex_lock(&list_lock);

	__v4l2_async_notifier_unregister(sd->subdev_notifier);
	__v4l2_async_notifier_cleanup(sd->subdev_notifier);
	kfree(sd->subdev_notifier);
	sd->subdev_notifier = NULL;

	if (sd->asd) {
		struct v4l2_async_notifier *notifier = sd->notifier;

		list_add(&sd->asd->list, &notifier->waiting);

		v4l2_async_notifier_call_unbind(notifier, sd, sd->asd);
	}

	v4l2_async_cleanup(sd);

	mutex_unlock(&list_lock);
}
EXPORT_SYMBOL(v4l2_async_unregister_subdev);
