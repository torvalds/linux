// SPDX-License-Identifier: GPL-2.0-only
/*
 * V4L2 asynchronous subdevice registration API
 *
 * Copyright (C) 2012-2013, Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 */

#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/types.h>

#include <media/v4l2-async.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

static int v4l2_async_nf_call_bound(struct v4l2_async_notifier *n,
				    struct v4l2_subdev *subdev,
				    struct v4l2_async_subdev *asd)
{
	if (!n->ops || !n->ops->bound)
		return 0;

	return n->ops->bound(n, subdev, asd);
}

static void v4l2_async_nf_call_unbind(struct v4l2_async_notifier *n,
				      struct v4l2_subdev *subdev,
				      struct v4l2_async_subdev *asd)
{
	if (!n->ops || !n->ops->unbind)
		return;

	n->ops->unbind(n, subdev, asd);
}

static int v4l2_async_nf_call_complete(struct v4l2_async_notifier *n)
{
	if (!n->ops || !n->ops->complete)
		return 0;

	return n->ops->complete(n);
}

static bool match_i2c(struct v4l2_async_notifier *notifier,
		      struct v4l2_subdev *sd, struct v4l2_async_subdev *asd)
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

static bool match_fwnode(struct v4l2_async_notifier *notifier,
			 struct v4l2_subdev *sd, struct v4l2_async_subdev *asd)
{
	struct fwnode_handle *other_fwnode;
	struct fwnode_handle *dev_fwnode;
	bool asd_fwnode_is_ep;
	bool sd_fwnode_is_ep;
	struct device *dev;

	/*
	 * Both the subdev and the async subdev can provide either an endpoint
	 * fwnode or a device fwnode. Start with the simple case of direct
	 * fwnode matching.
	 */
	if (sd->fwnode == asd->match.fwnode)
		return true;

	/*
	 * Check the same situation for any possible secondary assigned to the
	 * subdev's fwnode
	 */
	if (!IS_ERR_OR_NULL(sd->fwnode->secondary) &&
	    sd->fwnode->secondary == asd->match.fwnode)
		return true;

	/*
	 * Otherwise, check if the sd fwnode and the asd fwnode refer to an
	 * endpoint or a device. If they're of the same type, there's no match.
	 * Technically speaking this checks if the nodes refer to a connected
	 * endpoint, which is the simplest check that works for both OF and
	 * ACPI. This won't make a difference, as drivers should not try to
	 * match unconnected endpoints.
	 */
	sd_fwnode_is_ep = fwnode_graph_is_endpoint(sd->fwnode);
	asd_fwnode_is_ep = fwnode_graph_is_endpoint(asd->match.fwnode);

	if (sd_fwnode_is_ep == asd_fwnode_is_ep)
		return false;

	/*
	 * The sd and asd fwnodes are of different types. Get the device fwnode
	 * parent of the endpoint fwnode, and compare it with the other fwnode.
	 */
	if (sd_fwnode_is_ep) {
		dev_fwnode = fwnode_graph_get_port_parent(sd->fwnode);
		other_fwnode = asd->match.fwnode;
	} else {
		dev_fwnode = fwnode_graph_get_port_parent(asd->match.fwnode);
		other_fwnode = sd->fwnode;
	}

	fwnode_handle_put(dev_fwnode);

	if (dev_fwnode != other_fwnode)
		return false;

	/*
	 * We have a heterogeneous match. Retrieve the struct device of the side
	 * that matched on a device fwnode to print its driver name.
	 */
	if (sd_fwnode_is_ep)
		dev = notifier->v4l2_dev ? notifier->v4l2_dev->dev
		    : notifier->sd->dev;
	else
		dev = sd->dev;

	if (dev && dev->driver) {
		if (sd_fwnode_is_ep)
			dev_warn(dev, "Driver %s uses device fwnode, incorrect match may occur\n",
				 dev->driver->name);
		dev_notice(dev, "Consider updating driver %s to match on endpoints\n",
			   dev->driver->name);
	}

	return true;
}

static LIST_HEAD(subdev_list);
static LIST_HEAD(notifier_list);
static DEFINE_MUTEX(list_lock);

static struct v4l2_async_subdev *
v4l2_async_find_match(struct v4l2_async_notifier *notifier,
		      struct v4l2_subdev *sd)
{
	bool (*match)(struct v4l2_async_notifier *notifier,
		      struct v4l2_subdev *sd, struct v4l2_async_subdev *asd);
	struct v4l2_async_subdev *asd;

	list_for_each_entry(asd, &notifier->waiting, list) {
		/* bus_type has been verified valid before */
		switch (asd->match_type) {
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
		if (match(notifier, sd, asd))
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
v4l2_async_nf_find_v4l2_dev(struct v4l2_async_notifier *notifier)
{
	while (notifier->parent)
		notifier = notifier->parent;

	return notifier->v4l2_dev;
}

/*
 * Return true if all child sub-device notifiers are complete, false otherwise.
 */
static bool
v4l2_async_nf_can_complete(struct v4l2_async_notifier *notifier)
{
	struct v4l2_subdev *sd;

	if (!list_empty(&notifier->waiting))
		return false;

	list_for_each_entry(sd, &notifier->done, async_list) {
		struct v4l2_async_notifier *subdev_notifier =
			v4l2_async_find_subdev_notifier(sd);

		if (subdev_notifier &&
		    !v4l2_async_nf_can_complete(subdev_notifier))
			return false;
	}

	return true;
}

/*
 * Complete the master notifier if possible. This is done when all async
 * sub-devices have been bound; v4l2_device is also available then.
 */
static int
v4l2_async_nf_try_complete(struct v4l2_async_notifier *notifier)
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
	if (!v4l2_async_nf_can_complete(notifier))
		return 0;

	return v4l2_async_nf_call_complete(notifier);
}

static int
v4l2_async_nf_try_all_subdevs(struct v4l2_async_notifier *notifier);

static int v4l2_async_create_ancillary_links(struct v4l2_async_notifier *n,
					     struct v4l2_subdev *sd)
{
	struct media_link *link = NULL;

#if IS_ENABLED(CONFIG_MEDIA_CONTROLLER)

	if (sd->entity.function != MEDIA_ENT_F_LENS &&
	    sd->entity.function != MEDIA_ENT_F_FLASH)
		return 0;

	link = media_create_ancillary_link(&n->sd->entity, &sd->entity);

#endif

	return IS_ERR(link) ? PTR_ERR(link) : 0;
}

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

	ret = v4l2_async_nf_call_bound(notifier, sd, asd);
	if (ret < 0) {
		v4l2_device_unregister_subdev(sd);
		return ret;
	}

	/*
	 * Depending of the function of the entities involved, we may want to
	 * create links between them (for example between a sensor and its lens
	 * or between a sensor's source pad and the connected device's sink
	 * pad).
	 */
	ret = v4l2_async_create_ancillary_links(notifier, sd);
	if (ret) {
		v4l2_async_nf_call_unbind(notifier, sd, asd);
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

	return v4l2_async_nf_try_all_subdevs(subdev_notifier);
}

/* Test all async sub-devices in a notifier for a match. */
static int
v4l2_async_nf_try_all_subdevs(struct v4l2_async_notifier *notifier)
{
	struct v4l2_device *v4l2_dev =
		v4l2_async_nf_find_v4l2_dev(notifier);
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
v4l2_async_nf_unbind_all_subdevs(struct v4l2_async_notifier *notifier)
{
	struct v4l2_subdev *sd, *tmp;

	list_for_each_entry_safe(sd, tmp, &notifier->done, async_list) {
		struct v4l2_async_notifier *subdev_notifier =
			v4l2_async_find_subdev_notifier(sd);

		if (subdev_notifier)
			v4l2_async_nf_unbind_all_subdevs(subdev_notifier);

		v4l2_async_nf_call_unbind(notifier, sd, sd->asd);
		v4l2_async_cleanup(sd);

		list_move(&sd->async_list, &subdev_list);
	}

	notifier->parent = NULL;
}

/* See if an async sub-device can be found in a notifier's lists. */
static bool
__v4l2_async_nf_has_async_subdev(struct v4l2_async_notifier *notifier,
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
v4l2_async_nf_has_async_subdev(struct v4l2_async_notifier *notifier,
			       struct v4l2_async_subdev *asd, int this_index)
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
		if (__v4l2_async_nf_has_async_subdev(notifier, asd))
			return true;

	return false;
}

static int v4l2_async_nf_asd_valid(struct v4l2_async_notifier *notifier,
				   struct v4l2_async_subdev *asd,
				   int this_index)
{
	struct device *dev =
		notifier->v4l2_dev ? notifier->v4l2_dev->dev : NULL;

	if (!asd)
		return -EINVAL;

	switch (asd->match_type) {
	case V4L2_ASYNC_MATCH_I2C:
	case V4L2_ASYNC_MATCH_FWNODE:
		if (v4l2_async_nf_has_async_subdev(notifier, asd, this_index)) {
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

void v4l2_async_nf_init(struct v4l2_async_notifier *notifier)
{
	INIT_LIST_HEAD(&notifier->asd_list);
}
EXPORT_SYMBOL(v4l2_async_nf_init);

static int __v4l2_async_nf_register(struct v4l2_async_notifier *notifier)
{
	struct v4l2_async_subdev *asd;
	int ret, i = 0;

	INIT_LIST_HEAD(&notifier->waiting);
	INIT_LIST_HEAD(&notifier->done);

	mutex_lock(&list_lock);

	list_for_each_entry(asd, &notifier->asd_list, asd_list) {
		ret = v4l2_async_nf_asd_valid(notifier, asd, i++);
		if (ret)
			goto err_unlock;

		list_add_tail(&asd->list, &notifier->waiting);
	}

	ret = v4l2_async_nf_try_all_subdevs(notifier);
	if (ret < 0)
		goto err_unbind;

	ret = v4l2_async_nf_try_complete(notifier);
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
	v4l2_async_nf_unbind_all_subdevs(notifier);

err_unlock:
	mutex_unlock(&list_lock);

	return ret;
}

int v4l2_async_nf_register(struct v4l2_device *v4l2_dev,
			   struct v4l2_async_notifier *notifier)
{
	int ret;

	if (WARN_ON(!v4l2_dev || notifier->sd))
		return -EINVAL;

	notifier->v4l2_dev = v4l2_dev;

	ret = __v4l2_async_nf_register(notifier);
	if (ret)
		notifier->v4l2_dev = NULL;

	return ret;
}
EXPORT_SYMBOL(v4l2_async_nf_register);

int v4l2_async_subdev_nf_register(struct v4l2_subdev *sd,
				  struct v4l2_async_notifier *notifier)
{
	int ret;

	if (WARN_ON(!sd || notifier->v4l2_dev))
		return -EINVAL;

	notifier->sd = sd;

	ret = __v4l2_async_nf_register(notifier);
	if (ret)
		notifier->sd = NULL;

	return ret;
}
EXPORT_SYMBOL(v4l2_async_subdev_nf_register);

static void
__v4l2_async_nf_unregister(struct v4l2_async_notifier *notifier)
{
	if (!notifier || (!notifier->v4l2_dev && !notifier->sd))
		return;

	v4l2_async_nf_unbind_all_subdevs(notifier);

	notifier->sd = NULL;
	notifier->v4l2_dev = NULL;

	list_del(&notifier->list);
}

void v4l2_async_nf_unregister(struct v4l2_async_notifier *notifier)
{
	mutex_lock(&list_lock);

	__v4l2_async_nf_unregister(notifier);

	mutex_unlock(&list_lock);
}
EXPORT_SYMBOL(v4l2_async_nf_unregister);

static void __v4l2_async_nf_cleanup(struct v4l2_async_notifier *notifier)
{
	struct v4l2_async_subdev *asd, *tmp;

	if (!notifier || !notifier->asd_list.next)
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

void v4l2_async_nf_cleanup(struct v4l2_async_notifier *notifier)
{
	mutex_lock(&list_lock);

	__v4l2_async_nf_cleanup(notifier);

	mutex_unlock(&list_lock);
}
EXPORT_SYMBOL_GPL(v4l2_async_nf_cleanup);

int __v4l2_async_nf_add_subdev(struct v4l2_async_notifier *notifier,
			       struct v4l2_async_subdev *asd)
{
	int ret;

	mutex_lock(&list_lock);

	ret = v4l2_async_nf_asd_valid(notifier, asd, -1);
	if (ret)
		goto unlock;

	list_add_tail(&asd->asd_list, &notifier->asd_list);

unlock:
	mutex_unlock(&list_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(__v4l2_async_nf_add_subdev);

struct v4l2_async_subdev *
__v4l2_async_nf_add_fwnode(struct v4l2_async_notifier *notifier,
			   struct fwnode_handle *fwnode,
			   unsigned int asd_struct_size)
{
	struct v4l2_async_subdev *asd;
	int ret;

	asd = kzalloc(asd_struct_size, GFP_KERNEL);
	if (!asd)
		return ERR_PTR(-ENOMEM);

	asd->match_type = V4L2_ASYNC_MATCH_FWNODE;
	asd->match.fwnode = fwnode_handle_get(fwnode);

	ret = __v4l2_async_nf_add_subdev(notifier, asd);
	if (ret) {
		fwnode_handle_put(fwnode);
		kfree(asd);
		return ERR_PTR(ret);
	}

	return asd;
}
EXPORT_SYMBOL_GPL(__v4l2_async_nf_add_fwnode);

struct v4l2_async_subdev *
__v4l2_async_nf_add_fwnode_remote(struct v4l2_async_notifier *notif,
				  struct fwnode_handle *endpoint,
				  unsigned int asd_struct_size)
{
	struct v4l2_async_subdev *asd;
	struct fwnode_handle *remote;

	remote = fwnode_graph_get_remote_port_parent(endpoint);
	if (!remote)
		return ERR_PTR(-ENOTCONN);

	asd = __v4l2_async_nf_add_fwnode(notif, remote, asd_struct_size);
	/*
	 * Calling __v4l2_async_nf_add_fwnode grabs a refcount,
	 * so drop the one we got in fwnode_graph_get_remote_port_parent.
	 */
	fwnode_handle_put(remote);
	return asd;
}
EXPORT_SYMBOL_GPL(__v4l2_async_nf_add_fwnode_remote);

struct v4l2_async_subdev *
__v4l2_async_nf_add_i2c(struct v4l2_async_notifier *notifier, int adapter_id,
			unsigned short address, unsigned int asd_struct_size)
{
	struct v4l2_async_subdev *asd;
	int ret;

	asd = kzalloc(asd_struct_size, GFP_KERNEL);
	if (!asd)
		return ERR_PTR(-ENOMEM);

	asd->match_type = V4L2_ASYNC_MATCH_I2C;
	asd->match.i2c.adapter_id = adapter_id;
	asd->match.i2c.address = address;

	ret = __v4l2_async_nf_add_subdev(notifier, asd);
	if (ret) {
		kfree(asd);
		return ERR_PTR(ret);
	}

	return asd;
}
EXPORT_SYMBOL_GPL(__v4l2_async_nf_add_i2c);

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
			v4l2_async_nf_find_v4l2_dev(notifier);
		struct v4l2_async_subdev *asd;

		if (!v4l2_dev)
			continue;

		asd = v4l2_async_find_match(notifier, sd);
		if (!asd)
			continue;

		ret = v4l2_async_match_notify(notifier, v4l2_dev, sd, asd);
		if (ret)
			goto err_unbind;

		ret = v4l2_async_nf_try_complete(notifier);
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
		v4l2_async_nf_unbind_all_subdevs(subdev_notifier);

	if (sd->asd)
		v4l2_async_nf_call_unbind(notifier, sd, sd->asd);
	v4l2_async_cleanup(sd);

	mutex_unlock(&list_lock);

	return ret;
}
EXPORT_SYMBOL(v4l2_async_register_subdev);

void v4l2_async_unregister_subdev(struct v4l2_subdev *sd)
{
	if (!sd->async_list.next)
		return;

	mutex_lock(&list_lock);

	__v4l2_async_nf_unregister(sd->subdev_notifier);
	__v4l2_async_nf_cleanup(sd->subdev_notifier);
	kfree(sd->subdev_notifier);
	sd->subdev_notifier = NULL;

	if (sd->asd) {
		struct v4l2_async_notifier *notifier = sd->notifier;

		list_add(&sd->asd->list, &notifier->waiting);

		v4l2_async_nf_call_unbind(notifier, sd, sd->asd);
	}

	v4l2_async_cleanup(sd);

	mutex_unlock(&list_lock);
}
EXPORT_SYMBOL(v4l2_async_unregister_subdev);

static void print_waiting_subdev(struct seq_file *s,
				 struct v4l2_async_subdev *asd)
{
	switch (asd->match_type) {
	case V4L2_ASYNC_MATCH_I2C:
		seq_printf(s, " [i2c] dev=%d-%04x\n", asd->match.i2c.adapter_id,
			   asd->match.i2c.address);
		break;
	case V4L2_ASYNC_MATCH_FWNODE: {
		struct fwnode_handle *devnode, *fwnode = asd->match.fwnode;

		devnode = fwnode_graph_is_endpoint(fwnode) ?
			  fwnode_graph_get_port_parent(fwnode) :
			  fwnode_handle_get(fwnode);

		seq_printf(s, " [fwnode] dev=%s, node=%pfw\n",
			   devnode->dev ? dev_name(devnode->dev) : "nil",
			   fwnode);

		fwnode_handle_put(devnode);
		break;
	}
	}
}

static const char *
v4l2_async_nf_name(struct v4l2_async_notifier *notifier)
{
	if (notifier->v4l2_dev)
		return notifier->v4l2_dev->name;
	else if (notifier->sd)
		return notifier->sd->name;
	else
		return "nil";
}

static int pending_subdevs_show(struct seq_file *s, void *data)
{
	struct v4l2_async_notifier *notif;
	struct v4l2_async_subdev *asd;

	mutex_lock(&list_lock);

	list_for_each_entry(notif, &notifier_list, list) {
		seq_printf(s, "%s:\n", v4l2_async_nf_name(notif));
		list_for_each_entry(asd, &notif->waiting, list)
			print_waiting_subdev(s, asd);
	}

	mutex_unlock(&list_lock);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(pending_subdevs);

static struct dentry *v4l2_async_debugfs_dir;

static int __init v4l2_async_init(void)
{
	v4l2_async_debugfs_dir = debugfs_create_dir("v4l2-async", NULL);
	debugfs_create_file("pending_async_subdevices", 0444,
			    v4l2_async_debugfs_dir, NULL,
			    &pending_subdevs_fops);

	return 0;
}

static void __exit v4l2_async_exit(void)
{
	debugfs_remove_recursive(v4l2_async_debugfs_dir);
}

subsys_initcall(v4l2_async_init);
module_exit(v4l2_async_exit);

MODULE_AUTHOR("Guennadi Liakhovetski <g.liakhovetski@gmx.de>");
MODULE_AUTHOR("Sakari Ailus <sakari.ailus@linux.intel.com>");
MODULE_AUTHOR("Ezequiel Garcia <ezequiel@collabora.com>");
MODULE_LICENSE("GPL");
