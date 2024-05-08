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

#include "v4l2-subdev-priv.h"

static int v4l2_async_nf_call_bound(struct v4l2_async_notifier *n,
				    struct v4l2_subdev *subdev,
				    struct v4l2_async_connection *asc)
{
	if (!n->ops || !n->ops->bound)
		return 0;

	return n->ops->bound(n, subdev, asc);
}

static void v4l2_async_nf_call_unbind(struct v4l2_async_notifier *n,
				      struct v4l2_subdev *subdev,
				      struct v4l2_async_connection *asc)
{
	if (!n->ops || !n->ops->unbind)
		return;

	n->ops->unbind(n, subdev, asc);
}

static int v4l2_async_nf_call_complete(struct v4l2_async_notifier *n)
{
	if (!n->ops || !n->ops->complete)
		return 0;

	return n->ops->complete(n);
}

static void v4l2_async_nf_call_destroy(struct v4l2_async_notifier *n,
				       struct v4l2_async_connection *asc)
{
	if (!n->ops || !n->ops->destroy)
		return;

	n->ops->destroy(asc);
}

static bool match_i2c(struct v4l2_async_notifier *notifier,
		      struct v4l2_subdev *sd,
		      struct v4l2_async_match_desc *match)
{
#if IS_ENABLED(CONFIG_I2C)
	struct i2c_client *client = i2c_verify_client(sd->dev);

	return client &&
		match->i2c.adapter_id == client->adapter->nr &&
		match->i2c.address == client->addr;
#else
	return false;
#endif
}

static struct device *notifier_dev(struct v4l2_async_notifier *notifier)
{
	if (notifier->sd)
		return notifier->sd->dev;

	if (notifier->v4l2_dev)
		return notifier->v4l2_dev->dev;

	return NULL;
}

static bool
match_fwnode_one(struct v4l2_async_notifier *notifier,
		 struct v4l2_subdev *sd, struct fwnode_handle *sd_fwnode,
		 struct v4l2_async_match_desc *match)
{
	struct fwnode_handle *asd_dev_fwnode;
	bool ret;

	dev_dbg(notifier_dev(notifier),
		"v4l2-async: fwnode match: need %pfw, trying %pfw\n",
		sd_fwnode, match->fwnode);

	if (sd_fwnode == match->fwnode) {
		dev_dbg(notifier_dev(notifier),
			"v4l2-async: direct match found\n");
		return true;
	}

	if (!fwnode_graph_is_endpoint(match->fwnode)) {
		dev_dbg(notifier_dev(notifier),
			"v4l2-async: direct match not found\n");
		return false;
	}

	asd_dev_fwnode = fwnode_graph_get_port_parent(match->fwnode);

	ret = sd_fwnode == asd_dev_fwnode;

	fwnode_handle_put(asd_dev_fwnode);

	dev_dbg(notifier_dev(notifier),
		"v4l2-async: device--endpoint match %sfound\n",
		ret ? "" : "not ");

	return ret;
}

static bool match_fwnode(struct v4l2_async_notifier *notifier,
			 struct v4l2_subdev *sd,
			 struct v4l2_async_match_desc *match)
{
	dev_dbg(notifier_dev(notifier),
		"v4l2-async: matching for notifier %pfw, sd fwnode %pfw\n",
		dev_fwnode(notifier_dev(notifier)), sd->fwnode);

	if (!list_empty(&sd->async_subdev_endpoint_list)) {
		struct v4l2_async_subdev_endpoint *ase;

		dev_dbg(sd->dev,
			"v4l2-async: endpoint fwnode list available, looking for %pfw\n",
			match->fwnode);

		list_for_each_entry(ase, &sd->async_subdev_endpoint_list,
				    async_subdev_endpoint_entry) {
			bool matched = ase->endpoint == match->fwnode;

			dev_dbg(sd->dev,
				"v4l2-async: endpoint-endpoint match %sfound with %pfw\n",
				matched ? "" : "not ", ase->endpoint);

			if (matched)
				return true;
		}

		dev_dbg(sd->dev, "async: no endpoint matched\n");

		return false;
	}

	if (match_fwnode_one(notifier, sd, sd->fwnode, match))
		return true;

	/* Also check the secondary fwnode. */
	if (IS_ERR_OR_NULL(sd->fwnode->secondary))
		return false;

	dev_dbg(notifier_dev(notifier),
		"v4l2-async: trying secondary fwnode match\n");

	return match_fwnode_one(notifier, sd, sd->fwnode->secondary, match);
}

static LIST_HEAD(subdev_list);
static LIST_HEAD(notifier_list);
static DEFINE_MUTEX(list_lock);

static struct v4l2_async_connection *
v4l2_async_find_match(struct v4l2_async_notifier *notifier,
		      struct v4l2_subdev *sd)
{
	bool (*match)(struct v4l2_async_notifier *notifier,
		      struct v4l2_subdev *sd,
		      struct v4l2_async_match_desc *match);
	struct v4l2_async_connection *asc;

	list_for_each_entry(asc, &notifier->waiting_list, asc_entry) {
		/* bus_type has been verified valid before */
		switch (asc->match.type) {
		case V4L2_ASYNC_MATCH_TYPE_I2C:
			match = match_i2c;
			break;
		case V4L2_ASYNC_MATCH_TYPE_FWNODE:
			match = match_fwnode;
			break;
		default:
			/* Cannot happen, unless someone breaks us */
			WARN_ON(true);
			return NULL;
		}

		/* match cannot be NULL here */
		if (match(notifier, sd, &asc->match))
			return asc;
	}

	return NULL;
}

/* Compare two async match descriptors for equivalence */
static bool v4l2_async_match_equal(struct v4l2_async_match_desc *match1,
				   struct v4l2_async_match_desc *match2)
{
	if (match1->type != match2->type)
		return false;

	switch (match1->type) {
	case V4L2_ASYNC_MATCH_TYPE_I2C:
		return match1->i2c.adapter_id == match2->i2c.adapter_id &&
			match1->i2c.address == match2->i2c.address;
	case V4L2_ASYNC_MATCH_TYPE_FWNODE:
		return match1->fwnode == match2->fwnode;
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

	list_for_each_entry(n, &notifier_list, notifier_entry)
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
	struct v4l2_async_connection *asc;

	if (!list_empty(&notifier->waiting_list))
		return false;

	list_for_each_entry(asc, &notifier->done_list, asc_entry) {
		struct v4l2_async_notifier *subdev_notifier =
			v4l2_async_find_subdev_notifier(asc->sd);

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
	struct v4l2_async_notifier *__notifier = notifier;

	/* Quick check whether there are still more sub-devices here. */
	if (!list_empty(&notifier->waiting_list))
		return 0;

	if (notifier->sd)
		dev_dbg(notifier_dev(notifier),
			"v4l2-async: trying to complete\n");

	/* Check the entire notifier tree; find the root notifier first. */
	while (notifier->parent)
		notifier = notifier->parent;

	/* This is root if it has v4l2_dev. */
	if (!notifier->v4l2_dev) {
		dev_dbg(notifier_dev(__notifier),
			"v4l2-async: V4L2 device not available\n");
		return 0;
	}

	/* Is everything ready? */
	if (!v4l2_async_nf_can_complete(notifier))
		return 0;

	dev_dbg(notifier_dev(__notifier), "v4l2-async: complete\n");

	return v4l2_async_nf_call_complete(notifier);
}

static int
v4l2_async_nf_try_all_subdevs(struct v4l2_async_notifier *notifier);

static int v4l2_async_create_ancillary_links(struct v4l2_async_notifier *n,
					     struct v4l2_subdev *sd)
{
#if IS_ENABLED(CONFIG_MEDIA_CONTROLLER)
	struct media_link *link;

	if (sd->entity.function != MEDIA_ENT_F_LENS &&
	    sd->entity.function != MEDIA_ENT_F_FLASH)
		return 0;

	if (!n->sd)
		return 0;

	link = media_create_ancillary_link(&n->sd->entity, &sd->entity);

	return IS_ERR(link) ? PTR_ERR(link) : 0;
#else
	return 0;
#endif
}

static int v4l2_async_match_notify(struct v4l2_async_notifier *notifier,
				   struct v4l2_device *v4l2_dev,
				   struct v4l2_subdev *sd,
				   struct v4l2_async_connection *asc)
{
	struct v4l2_async_notifier *subdev_notifier;
	bool registered = false;
	int ret;

	if (list_empty(&sd->asc_list)) {
		ret = __v4l2_device_register_subdev(v4l2_dev, sd, sd->owner);
		if (ret < 0)
			return ret;
		registered = true;
	}

	ret = v4l2_async_nf_call_bound(notifier, sd, asc);
	if (ret < 0) {
		if (asc->match.type == V4L2_ASYNC_MATCH_TYPE_FWNODE)
			dev_dbg(notifier_dev(notifier),
				"failed binding %pfw (%d)\n",
				asc->match.fwnode, ret);
		goto err_unregister_subdev;
	}

	if (registered) {
		/*
		 * Depending of the function of the entities involved, we may
		 * want to create links between them (for example between a
		 * sensor and its lens or between a sensor's source pad and the
		 * connected device's sink pad).
		 */
		ret = v4l2_async_create_ancillary_links(notifier, sd);
		if (ret) {
			if (asc->match.type == V4L2_ASYNC_MATCH_TYPE_FWNODE)
				dev_dbg(notifier_dev(notifier),
					"failed creating links for %pfw (%d)\n",
					asc->match.fwnode, ret);
			goto err_call_unbind;
		}
	}

	list_add(&asc->asc_subdev_entry, &sd->asc_list);
	asc->sd = sd;

	/* Move from the waiting list to notifier's done */
	list_move(&asc->asc_entry, &notifier->done_list);

	dev_dbg(notifier_dev(notifier), "v4l2-async: %s bound (ret %d)\n",
		dev_name(sd->dev), ret);

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

err_call_unbind:
	v4l2_async_nf_call_unbind(notifier, sd, asc);
	list_del(&asc->asc_subdev_entry);

err_unregister_subdev:
	if (registered)
		v4l2_device_unregister_subdev(sd);

	return ret;
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

	dev_dbg(notifier_dev(notifier), "v4l2-async: trying all sub-devices\n");

again:
	list_for_each_entry(sd, &subdev_list, async_list) {
		struct v4l2_async_connection *asc;
		int ret;

		asc = v4l2_async_find_match(notifier, sd);
		if (!asc)
			continue;

		dev_dbg(notifier_dev(notifier),
			"v4l2-async: match found, subdev %s\n", sd->name);

		ret = v4l2_async_match_notify(notifier, v4l2_dev, sd, asc);
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

static void v4l2_async_unbind_subdev_one(struct v4l2_async_notifier *notifier,
					 struct v4l2_async_connection *asc)
{
	list_move_tail(&asc->asc_entry, &notifier->waiting_list);
	if (list_is_singular(&asc->asc_subdev_entry)) {
		v4l2_async_nf_call_unbind(notifier, asc->sd, asc);
		v4l2_device_unregister_subdev(asc->sd);
		asc->sd = NULL;
	}
	list_del(&asc->asc_subdev_entry);
}

/* Unbind all sub-devices in the notifier tree. */
static void
v4l2_async_nf_unbind_all_subdevs(struct v4l2_async_notifier *notifier)
{
	struct v4l2_async_connection *asc, *asc_tmp;

	list_for_each_entry_safe(asc, asc_tmp, &notifier->done_list,
				 asc_entry) {
		struct v4l2_async_notifier *subdev_notifier =
			v4l2_async_find_subdev_notifier(asc->sd);

		if (subdev_notifier)
			v4l2_async_nf_unbind_all_subdevs(subdev_notifier);

		v4l2_async_unbind_subdev_one(notifier, asc);
	}

	notifier->parent = NULL;
}

/* See if an async sub-device can be found in a notifier's lists. */
static bool
v4l2_async_nf_has_async_match_entry(struct v4l2_async_notifier *notifier,
				    struct v4l2_async_match_desc *match)
{
	struct v4l2_async_connection *asc;

	list_for_each_entry(asc, &notifier->waiting_list, asc_entry)
		if (v4l2_async_match_equal(&asc->match, match))
			return true;

	list_for_each_entry(asc, &notifier->done_list, asc_entry)
		if (v4l2_async_match_equal(&asc->match, match))
			return true;

	return false;
}

/*
 * Find out whether an async sub-device was set up already or whether it exists
 * in a given notifier.
 */
static bool
v4l2_async_nf_has_async_match(struct v4l2_async_notifier *notifier,
			      struct v4l2_async_match_desc *match)
{
	struct list_head *heads[] = {
		&notifier->waiting_list,
		&notifier->done_list,
	};
	unsigned int i;

	lockdep_assert_held(&list_lock);

	/* Check that an asd is not being added more than once. */
	for (i = 0; i < ARRAY_SIZE(heads); i++) {
		struct v4l2_async_connection *asc;

		list_for_each_entry(asc, heads[i], asc_entry) {
			if (&asc->match == match)
				continue;
			if (v4l2_async_match_equal(&asc->match, match))
				return true;
		}
	}

	/* Check that an asc does not exist in other notifiers. */
	list_for_each_entry(notifier, &notifier_list, notifier_entry)
		if (v4l2_async_nf_has_async_match_entry(notifier, match))
			return true;

	return false;
}

static int v4l2_async_nf_match_valid(struct v4l2_async_notifier *notifier,
				     struct v4l2_async_match_desc *match)
{
	struct device *dev = notifier_dev(notifier);

	switch (match->type) {
	case V4L2_ASYNC_MATCH_TYPE_I2C:
	case V4L2_ASYNC_MATCH_TYPE_FWNODE:
		if (v4l2_async_nf_has_async_match(notifier, match)) {
			dev_dbg(dev, "v4l2-async: match descriptor already listed in a notifier\n");
			return -EEXIST;
		}
		break;
	default:
		dev_err(dev, "v4l2-async: Invalid match type %u on %p\n",
			match->type, match);
		return -EINVAL;
	}

	return 0;
}

void v4l2_async_nf_init(struct v4l2_async_notifier *notifier,
			struct v4l2_device *v4l2_dev)
{
	INIT_LIST_HEAD(&notifier->waiting_list);
	INIT_LIST_HEAD(&notifier->done_list);
	INIT_LIST_HEAD(&notifier->notifier_entry);
	notifier->v4l2_dev = v4l2_dev;
}
EXPORT_SYMBOL(v4l2_async_nf_init);

void v4l2_async_subdev_nf_init(struct v4l2_async_notifier *notifier,
			       struct v4l2_subdev *sd)
{
	INIT_LIST_HEAD(&notifier->waiting_list);
	INIT_LIST_HEAD(&notifier->done_list);
	INIT_LIST_HEAD(&notifier->notifier_entry);
	notifier->sd = sd;
}
EXPORT_SYMBOL_GPL(v4l2_async_subdev_nf_init);

static int __v4l2_async_nf_register(struct v4l2_async_notifier *notifier)
{
	struct v4l2_async_connection *asc;
	int ret;

	mutex_lock(&list_lock);

	list_for_each_entry(asc, &notifier->waiting_list, asc_entry) {
		ret = v4l2_async_nf_match_valid(notifier, &asc->match);
		if (ret)
			goto err_unlock;
	}

	ret = v4l2_async_nf_try_all_subdevs(notifier);
	if (ret < 0)
		goto err_unbind;

	ret = v4l2_async_nf_try_complete(notifier);
	if (ret < 0)
		goto err_unbind;

	/* Keep also completed notifiers on the list */
	list_add(&notifier->notifier_entry, &notifier_list);

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

int v4l2_async_nf_register(struct v4l2_async_notifier *notifier)
{
	if (WARN_ON(!notifier->v4l2_dev == !notifier->sd))
		return -EINVAL;

	return __v4l2_async_nf_register(notifier);
}
EXPORT_SYMBOL(v4l2_async_nf_register);

static void
__v4l2_async_nf_unregister(struct v4l2_async_notifier *notifier)
{
	if (!notifier || (!notifier->v4l2_dev && !notifier->sd))
		return;

	v4l2_async_nf_unbind_all_subdevs(notifier);

	list_del_init(&notifier->notifier_entry);
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
	struct v4l2_async_connection *asc, *tmp;

	if (!notifier || !notifier->waiting_list.next)
		return;

	WARN_ON(!list_empty(&notifier->done_list));

	list_for_each_entry_safe(asc, tmp, &notifier->waiting_list, asc_entry) {
		list_del(&asc->asc_entry);
		v4l2_async_nf_call_destroy(notifier, asc);

		if (asc->match.type == V4L2_ASYNC_MATCH_TYPE_FWNODE)
			fwnode_handle_put(asc->match.fwnode);

		kfree(asc);
	}

	notifier->sd = NULL;
	notifier->v4l2_dev = NULL;
}

void v4l2_async_nf_cleanup(struct v4l2_async_notifier *notifier)
{
	mutex_lock(&list_lock);

	__v4l2_async_nf_cleanup(notifier);

	mutex_unlock(&list_lock);
}
EXPORT_SYMBOL_GPL(v4l2_async_nf_cleanup);

static void __v4l2_async_nf_add_connection(struct v4l2_async_notifier *notifier,
					   struct v4l2_async_connection *asc)
{
	mutex_lock(&list_lock);

	list_add_tail(&asc->asc_entry, &notifier->waiting_list);

	mutex_unlock(&list_lock);
}

struct v4l2_async_connection *
__v4l2_async_nf_add_fwnode(struct v4l2_async_notifier *notifier,
			   struct fwnode_handle *fwnode,
			   unsigned int asc_struct_size)
{
	struct v4l2_async_connection *asc;

	asc = kzalloc(asc_struct_size, GFP_KERNEL);
	if (!asc)
		return ERR_PTR(-ENOMEM);

	asc->notifier = notifier;
	asc->match.type = V4L2_ASYNC_MATCH_TYPE_FWNODE;
	asc->match.fwnode = fwnode_handle_get(fwnode);

	__v4l2_async_nf_add_connection(notifier, asc);

	return asc;
}
EXPORT_SYMBOL_GPL(__v4l2_async_nf_add_fwnode);

struct v4l2_async_connection *
__v4l2_async_nf_add_fwnode_remote(struct v4l2_async_notifier *notif,
				  struct fwnode_handle *endpoint,
				  unsigned int asc_struct_size)
{
	struct v4l2_async_connection *asc;
	struct fwnode_handle *remote;

	remote = fwnode_graph_get_remote_endpoint(endpoint);
	if (!remote)
		return ERR_PTR(-ENOTCONN);

	asc = __v4l2_async_nf_add_fwnode(notif, remote, asc_struct_size);
	/*
	 * Calling __v4l2_async_nf_add_fwnode grabs a refcount,
	 * so drop the one we got in fwnode_graph_get_remote_port_parent.
	 */
	fwnode_handle_put(remote);
	return asc;
}
EXPORT_SYMBOL_GPL(__v4l2_async_nf_add_fwnode_remote);

struct v4l2_async_connection *
__v4l2_async_nf_add_i2c(struct v4l2_async_notifier *notifier, int adapter_id,
			unsigned short address, unsigned int asc_struct_size)
{
	struct v4l2_async_connection *asc;

	asc = kzalloc(asc_struct_size, GFP_KERNEL);
	if (!asc)
		return ERR_PTR(-ENOMEM);

	asc->notifier = notifier;
	asc->match.type = V4L2_ASYNC_MATCH_TYPE_I2C;
	asc->match.i2c.adapter_id = adapter_id;
	asc->match.i2c.address = address;

	__v4l2_async_nf_add_connection(notifier, asc);

	return asc;
}
EXPORT_SYMBOL_GPL(__v4l2_async_nf_add_i2c);

int v4l2_async_subdev_endpoint_add(struct v4l2_subdev *sd,
				   struct fwnode_handle *fwnode)
{
	struct v4l2_async_subdev_endpoint *ase;

	ase = kmalloc(sizeof(*ase), GFP_KERNEL);
	if (!ase)
		return -ENOMEM;

	ase->endpoint = fwnode;
	list_add(&ase->async_subdev_endpoint_entry,
		 &sd->async_subdev_endpoint_list);

	return 0;
}
EXPORT_SYMBOL_GPL(v4l2_async_subdev_endpoint_add);

struct v4l2_async_connection *
v4l2_async_connection_unique(struct v4l2_subdev *sd)
{
	if (!list_is_singular(&sd->asc_list))
		return NULL;

	return list_first_entry(&sd->asc_list,
				struct v4l2_async_connection, asc_subdev_entry);
}
EXPORT_SYMBOL_GPL(v4l2_async_connection_unique);

int __v4l2_async_register_subdev(struct v4l2_subdev *sd, struct module *module)
{
	struct v4l2_async_notifier *subdev_notifier;
	struct v4l2_async_notifier *notifier;
	struct v4l2_async_connection *asc;
	int ret;

	INIT_LIST_HEAD(&sd->asc_list);

	/*
	 * No reference taken. The reference is held by the device (struct
	 * v4l2_subdev.dev), and async sub-device does not exist independently
	 * of the device at any point of time.
	 *
	 * The async sub-device shall always be registered for its device node,
	 * not the endpoint node.
	 */
	if (!sd->fwnode && sd->dev) {
		sd->fwnode = dev_fwnode(sd->dev);
	} else if (fwnode_graph_is_endpoint(sd->fwnode)) {
		dev_warn(sd->dev, "sub-device fwnode is an endpoint!\n");
		return -EINVAL;
	}

	sd->owner = module;

	mutex_lock(&list_lock);

	list_for_each_entry(notifier, &notifier_list, notifier_entry) {
		struct v4l2_device *v4l2_dev =
			v4l2_async_nf_find_v4l2_dev(notifier);

		if (!v4l2_dev)
			continue;

		while ((asc = v4l2_async_find_match(notifier, sd))) {
			ret = v4l2_async_match_notify(notifier, v4l2_dev, sd,
						      asc);
			if (ret)
				goto err_unbind;

			ret = v4l2_async_nf_try_complete(notifier);
			if (ret)
				goto err_unbind;
		}
	}

	/* None matched, wait for hot-plugging */
	list_add(&sd->async_list, &subdev_list);

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

	if (asc)
		v4l2_async_unbind_subdev_one(notifier, asc);

	mutex_unlock(&list_lock);

	sd->owner = NULL;

	return ret;
}
EXPORT_SYMBOL(__v4l2_async_register_subdev);

void v4l2_async_unregister_subdev(struct v4l2_subdev *sd)
{
	struct v4l2_async_connection *asc, *asc_tmp;

	if (!sd->async_list.next)
		return;

	v4l2_subdev_put_privacy_led(sd);

	mutex_lock(&list_lock);

	__v4l2_async_nf_unregister(sd->subdev_notifier);
	__v4l2_async_nf_cleanup(sd->subdev_notifier);
	kfree(sd->subdev_notifier);
	sd->subdev_notifier = NULL;

	if (sd->asc_list.next) {
		list_for_each_entry_safe(asc, asc_tmp, &sd->asc_list,
					 asc_subdev_entry) {
			v4l2_async_unbind_subdev_one(asc->notifier, asc);
		}
	}

	list_del(&sd->async_list);
	sd->async_list.next = NULL;

	mutex_unlock(&list_lock);
}
EXPORT_SYMBOL(v4l2_async_unregister_subdev);

static void print_waiting_match(struct seq_file *s,
				struct v4l2_async_match_desc *match)
{
	switch (match->type) {
	case V4L2_ASYNC_MATCH_TYPE_I2C:
		seq_printf(s, " [i2c] dev=%d-%04x\n", match->i2c.adapter_id,
			   match->i2c.address);
		break;
	case V4L2_ASYNC_MATCH_TYPE_FWNODE: {
		struct fwnode_handle *devnode, *fwnode = match->fwnode;

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
	struct v4l2_async_connection *asc;

	mutex_lock(&list_lock);

	list_for_each_entry(notif, &notifier_list, notifier_entry) {
		seq_printf(s, "%s:\n", v4l2_async_nf_name(notif));
		list_for_each_entry(asc, &notif->waiting_list, asc_entry)
			print_waiting_match(s, &asc->match);
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
