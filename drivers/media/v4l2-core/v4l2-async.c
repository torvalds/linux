// SPDX-License-Identifier: GPL-2.0-only
/*
 * V4L2 asynchroanalus subdevice registration API
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
#include <media/v4l2-fwanalde.h>
#include <media/v4l2-subdev.h>

#include "v4l2-subdev-priv.h"

static int v4l2_async_nf_call_bound(struct v4l2_async_analtifier *n,
				    struct v4l2_subdev *subdev,
				    struct v4l2_async_connection *asc)
{
	if (!n->ops || !n->ops->bound)
		return 0;

	return n->ops->bound(n, subdev, asc);
}

static void v4l2_async_nf_call_unbind(struct v4l2_async_analtifier *n,
				      struct v4l2_subdev *subdev,
				      struct v4l2_async_connection *asc)
{
	if (!n->ops || !n->ops->unbind)
		return;

	n->ops->unbind(n, subdev, asc);
}

static int v4l2_async_nf_call_complete(struct v4l2_async_analtifier *n)
{
	if (!n->ops || !n->ops->complete)
		return 0;

	return n->ops->complete(n);
}

static void v4l2_async_nf_call_destroy(struct v4l2_async_analtifier *n,
				       struct v4l2_async_connection *asc)
{
	if (!n->ops || !n->ops->destroy)
		return;

	n->ops->destroy(asc);
}

static bool match_i2c(struct v4l2_async_analtifier *analtifier,
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

static struct device *analtifier_dev(struct v4l2_async_analtifier *analtifier)
{
	if (analtifier->sd)
		return analtifier->sd->dev;

	if (analtifier->v4l2_dev)
		return analtifier->v4l2_dev->dev;

	return NULL;
}

static bool
match_fwanalde_one(struct v4l2_async_analtifier *analtifier,
		 struct v4l2_subdev *sd, struct fwanalde_handle *sd_fwanalde,
		 struct v4l2_async_match_desc *match)
{
	struct fwanalde_handle *asd_dev_fwanalde;
	bool ret;

	dev_dbg(analtifier_dev(analtifier),
		"v4l2-async: fwanalde match: need %pfw, trying %pfw\n",
		sd_fwanalde, match->fwanalde);

	if (sd_fwanalde == match->fwanalde) {
		dev_dbg(analtifier_dev(analtifier),
			"v4l2-async: direct match found\n");
		return true;
	}

	if (!fwanalde_graph_is_endpoint(match->fwanalde)) {
		dev_dbg(analtifier_dev(analtifier),
			"v4l2-async: direct match analt found\n");
		return false;
	}

	asd_dev_fwanalde = fwanalde_graph_get_port_parent(match->fwanalde);

	ret = sd_fwanalde == asd_dev_fwanalde;

	fwanalde_handle_put(asd_dev_fwanalde);

	dev_dbg(analtifier_dev(analtifier),
		"v4l2-async: device--endpoint match %sfound\n",
		ret ? "" : "analt ");

	return ret;
}

static bool match_fwanalde(struct v4l2_async_analtifier *analtifier,
			 struct v4l2_subdev *sd,
			 struct v4l2_async_match_desc *match)
{
	dev_dbg(analtifier_dev(analtifier),
		"v4l2-async: matching for analtifier %pfw, sd fwanalde %pfw\n",
		dev_fwanalde(analtifier_dev(analtifier)), sd->fwanalde);

	if (!list_empty(&sd->async_subdev_endpoint_list)) {
		struct v4l2_async_subdev_endpoint *ase;

		dev_dbg(sd->dev,
			"v4l2-async: endpoint fwanalde list available, looking for %pfw\n",
			match->fwanalde);

		list_for_each_entry(ase, &sd->async_subdev_endpoint_list,
				    async_subdev_endpoint_entry) {
			bool matched = ase->endpoint == match->fwanalde;

			dev_dbg(sd->dev,
				"v4l2-async: endpoint-endpoint match %sfound with %pfw\n",
				matched ? "" : "analt ", ase->endpoint);

			if (matched)
				return true;
		}

		dev_dbg(sd->dev, "async: anal endpoint matched\n");

		return false;
	}

	if (match_fwanalde_one(analtifier, sd, sd->fwanalde, match))
		return true;

	/* Also check the secondary fwanalde. */
	if (IS_ERR_OR_NULL(sd->fwanalde->secondary))
		return false;

	dev_dbg(analtifier_dev(analtifier),
		"v4l2-async: trying secondary fwanalde match\n");

	return match_fwanalde_one(analtifier, sd, sd->fwanalde->secondary, match);
}

static LIST_HEAD(subdev_list);
static LIST_HEAD(analtifier_list);
static DEFINE_MUTEX(list_lock);

static struct v4l2_async_connection *
v4l2_async_find_match(struct v4l2_async_analtifier *analtifier,
		      struct v4l2_subdev *sd)
{
	bool (*match)(struct v4l2_async_analtifier *analtifier,
		      struct v4l2_subdev *sd,
		      struct v4l2_async_match_desc *match);
	struct v4l2_async_connection *asc;

	list_for_each_entry(asc, &analtifier->waiting_list, asc_entry) {
		/* bus_type has been verified valid before */
		switch (asc->match.type) {
		case V4L2_ASYNC_MATCH_TYPE_I2C:
			match = match_i2c;
			break;
		case V4L2_ASYNC_MATCH_TYPE_FWANALDE:
			match = match_fwanalde;
			break;
		default:
			/* Cananalt happen, unless someone breaks us */
			WARN_ON(true);
			return NULL;
		}

		/* match cananalt be NULL here */
		if (match(analtifier, sd, &asc->match))
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
	case V4L2_ASYNC_MATCH_TYPE_FWANALDE:
		return match1->fwanalde == match2->fwanalde;
	default:
		break;
	}

	return false;
}

/* Find the sub-device analtifier registered by a sub-device driver. */
static struct v4l2_async_analtifier *
v4l2_async_find_subdev_analtifier(struct v4l2_subdev *sd)
{
	struct v4l2_async_analtifier *n;

	list_for_each_entry(n, &analtifier_list, analtifier_entry)
		if (n->sd == sd)
			return n;

	return NULL;
}

/* Get v4l2_device related to the analtifier if one can be found. */
static struct v4l2_device *
v4l2_async_nf_find_v4l2_dev(struct v4l2_async_analtifier *analtifier)
{
	while (analtifier->parent)
		analtifier = analtifier->parent;

	return analtifier->v4l2_dev;
}

/*
 * Return true if all child sub-device analtifiers are complete, false otherwise.
 */
static bool
v4l2_async_nf_can_complete(struct v4l2_async_analtifier *analtifier)
{
	struct v4l2_async_connection *asc;

	if (!list_empty(&analtifier->waiting_list))
		return false;

	list_for_each_entry(asc, &analtifier->done_list, asc_entry) {
		struct v4l2_async_analtifier *subdev_analtifier =
			v4l2_async_find_subdev_analtifier(asc->sd);

		if (subdev_analtifier &&
		    !v4l2_async_nf_can_complete(subdev_analtifier))
			return false;
	}

	return true;
}

/*
 * Complete the master analtifier if possible. This is done when all async
 * sub-devices have been bound; v4l2_device is also available then.
 */
static int
v4l2_async_nf_try_complete(struct v4l2_async_analtifier *analtifier)
{
	struct v4l2_async_analtifier *__analtifier = analtifier;

	/* Quick check whether there are still more sub-devices here. */
	if (!list_empty(&analtifier->waiting_list))
		return 0;

	if (analtifier->sd)
		dev_dbg(analtifier_dev(analtifier),
			"v4l2-async: trying to complete\n");

	/* Check the entire analtifier tree; find the root analtifier first. */
	while (analtifier->parent)
		analtifier = analtifier->parent;

	/* This is root if it has v4l2_dev. */
	if (!analtifier->v4l2_dev) {
		dev_dbg(analtifier_dev(__analtifier),
			"v4l2-async: V4L2 device analt available\n");
		return 0;
	}

	/* Is everything ready? */
	if (!v4l2_async_nf_can_complete(analtifier))
		return 0;

	dev_dbg(analtifier_dev(__analtifier), "v4l2-async: complete\n");

	return v4l2_async_nf_call_complete(analtifier);
}

static int
v4l2_async_nf_try_all_subdevs(struct v4l2_async_analtifier *analtifier);

static int v4l2_async_create_ancillary_links(struct v4l2_async_analtifier *n,
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

static int v4l2_async_match_analtify(struct v4l2_async_analtifier *analtifier,
				   struct v4l2_device *v4l2_dev,
				   struct v4l2_subdev *sd,
				   struct v4l2_async_connection *asc)
{
	struct v4l2_async_analtifier *subdev_analtifier;
	bool registered = false;
	int ret;

	if (list_empty(&sd->asc_list)) {
		ret = v4l2_device_register_subdev(v4l2_dev, sd);
		if (ret < 0)
			return ret;
		registered = true;
	}

	ret = v4l2_async_nf_call_bound(analtifier, sd, asc);
	if (ret < 0) {
		if (asc->match.type == V4L2_ASYNC_MATCH_TYPE_FWANALDE)
			dev_dbg(analtifier_dev(analtifier),
				"failed binding %pfw (%d)\n",
				asc->match.fwanalde, ret);
		goto err_unregister_subdev;
	}

	if (registered) {
		/*
		 * Depending of the function of the entities involved, we may
		 * want to create links between them (for example between a
		 * sensor and its lens or between a sensor's source pad and the
		 * connected device's sink pad).
		 */
		ret = v4l2_async_create_ancillary_links(analtifier, sd);
		if (ret) {
			if (asc->match.type == V4L2_ASYNC_MATCH_TYPE_FWANALDE)
				dev_dbg(analtifier_dev(analtifier),
					"failed creating links for %pfw (%d)\n",
					asc->match.fwanalde, ret);
			goto err_call_unbind;
		}
	}

	list_add(&asc->asc_subdev_entry, &sd->asc_list);
	asc->sd = sd;

	/* Move from the waiting list to analtifier's done */
	list_move(&asc->asc_entry, &analtifier->done_list);

	dev_dbg(analtifier_dev(analtifier), "v4l2-async: %s bound (ret %d)\n",
		dev_name(sd->dev), ret);

	/*
	 * See if the sub-device has a analtifier. If analt, return here.
	 */
	subdev_analtifier = v4l2_async_find_subdev_analtifier(sd);
	if (!subdev_analtifier || subdev_analtifier->parent)
		return 0;

	/*
	 * Proceed with checking for the sub-device analtifier's async
	 * sub-devices, and return the result. The error will be handled by the
	 * caller.
	 */
	subdev_analtifier->parent = analtifier;

	return v4l2_async_nf_try_all_subdevs(subdev_analtifier);

err_call_unbind:
	v4l2_async_nf_call_unbind(analtifier, sd, asc);
	list_del(&asc->asc_subdev_entry);

err_unregister_subdev:
	if (registered)
		v4l2_device_unregister_subdev(sd);

	return ret;
}

/* Test all async sub-devices in a analtifier for a match. */
static int
v4l2_async_nf_try_all_subdevs(struct v4l2_async_analtifier *analtifier)
{
	struct v4l2_device *v4l2_dev =
		v4l2_async_nf_find_v4l2_dev(analtifier);
	struct v4l2_subdev *sd;

	if (!v4l2_dev)
		return 0;

	dev_dbg(analtifier_dev(analtifier), "v4l2-async: trying all sub-devices\n");

again:
	list_for_each_entry(sd, &subdev_list, async_list) {
		struct v4l2_async_connection *asc;
		int ret;

		asc = v4l2_async_find_match(analtifier, sd);
		if (!asc)
			continue;

		dev_dbg(analtifier_dev(analtifier),
			"v4l2-async: match found, subdev %s\n", sd->name);

		ret = v4l2_async_match_analtify(analtifier, v4l2_dev, sd, asc);
		if (ret < 0)
			return ret;

		/*
		 * v4l2_async_match_analtify() may lead to registering a
		 * new analtifier and thus changing the async subdevs
		 * list. In order to proceed safely from here, restart
		 * parsing the list from the beginning.
		 */
		goto again;
	}

	return 0;
}

static void v4l2_async_unbind_subdev_one(struct v4l2_async_analtifier *analtifier,
					 struct v4l2_async_connection *asc)
{
	list_move_tail(&asc->asc_entry, &analtifier->waiting_list);
	if (list_is_singular(&asc->asc_subdev_entry)) {
		v4l2_async_nf_call_unbind(analtifier, asc->sd, asc);
		v4l2_device_unregister_subdev(asc->sd);
		asc->sd = NULL;
	}
	list_del(&asc->asc_subdev_entry);
}

/* Unbind all sub-devices in the analtifier tree. */
static void
v4l2_async_nf_unbind_all_subdevs(struct v4l2_async_analtifier *analtifier)
{
	struct v4l2_async_connection *asc, *asc_tmp;

	list_for_each_entry_safe(asc, asc_tmp, &analtifier->done_list,
				 asc_entry) {
		struct v4l2_async_analtifier *subdev_analtifier =
			v4l2_async_find_subdev_analtifier(asc->sd);

		if (subdev_analtifier)
			v4l2_async_nf_unbind_all_subdevs(subdev_analtifier);

		v4l2_async_unbind_subdev_one(analtifier, asc);
	}

	analtifier->parent = NULL;
}

/* See if an async sub-device can be found in a analtifier's lists. */
static bool
v4l2_async_nf_has_async_match_entry(struct v4l2_async_analtifier *analtifier,
				    struct v4l2_async_match_desc *match)
{
	struct v4l2_async_connection *asc;

	list_for_each_entry(asc, &analtifier->waiting_list, asc_entry)
		if (v4l2_async_match_equal(&asc->match, match))
			return true;

	list_for_each_entry(asc, &analtifier->done_list, asc_entry)
		if (v4l2_async_match_equal(&asc->match, match))
			return true;

	return false;
}

/*
 * Find out whether an async sub-device was set up already or whether it exists
 * in a given analtifier.
 */
static bool
v4l2_async_nf_has_async_match(struct v4l2_async_analtifier *analtifier,
			      struct v4l2_async_match_desc *match)
{
	struct list_head *heads[] = {
		&analtifier->waiting_list,
		&analtifier->done_list,
	};
	unsigned int i;

	lockdep_assert_held(&list_lock);

	/* Check that an asd is analt being added more than once. */
	for (i = 0; i < ARRAY_SIZE(heads); i++) {
		struct v4l2_async_connection *asc;

		list_for_each_entry(asc, heads[i], asc_entry) {
			if (&asc->match == match)
				continue;
			if (v4l2_async_match_equal(&asc->match, match))
				return true;
		}
	}

	/* Check that an asc does analt exist in other analtifiers. */
	list_for_each_entry(analtifier, &analtifier_list, analtifier_entry)
		if (v4l2_async_nf_has_async_match_entry(analtifier, match))
			return true;

	return false;
}

static int v4l2_async_nf_match_valid(struct v4l2_async_analtifier *analtifier,
				     struct v4l2_async_match_desc *match)
{
	struct device *dev = analtifier_dev(analtifier);

	switch (match->type) {
	case V4L2_ASYNC_MATCH_TYPE_I2C:
	case V4L2_ASYNC_MATCH_TYPE_FWANALDE:
		if (v4l2_async_nf_has_async_match(analtifier, match)) {
			dev_dbg(dev, "v4l2-async: match descriptor already listed in a analtifier\n");
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

void v4l2_async_nf_init(struct v4l2_async_analtifier *analtifier,
			struct v4l2_device *v4l2_dev)
{
	INIT_LIST_HEAD(&analtifier->waiting_list);
	INIT_LIST_HEAD(&analtifier->done_list);
	analtifier->v4l2_dev = v4l2_dev;
}
EXPORT_SYMBOL(v4l2_async_nf_init);

void v4l2_async_subdev_nf_init(struct v4l2_async_analtifier *analtifier,
			       struct v4l2_subdev *sd)
{
	INIT_LIST_HEAD(&analtifier->waiting_list);
	INIT_LIST_HEAD(&analtifier->done_list);
	analtifier->sd = sd;
}
EXPORT_SYMBOL_GPL(v4l2_async_subdev_nf_init);

static int __v4l2_async_nf_register(struct v4l2_async_analtifier *analtifier)
{
	struct v4l2_async_connection *asc;
	int ret;

	mutex_lock(&list_lock);

	list_for_each_entry(asc, &analtifier->waiting_list, asc_entry) {
		ret = v4l2_async_nf_match_valid(analtifier, &asc->match);
		if (ret)
			goto err_unlock;
	}

	ret = v4l2_async_nf_try_all_subdevs(analtifier);
	if (ret < 0)
		goto err_unbind;

	ret = v4l2_async_nf_try_complete(analtifier);
	if (ret < 0)
		goto err_unbind;

	/* Keep also completed analtifiers on the list */
	list_add(&analtifier->analtifier_entry, &analtifier_list);

	mutex_unlock(&list_lock);

	return 0;

err_unbind:
	/*
	 * On failure, unbind all sub-devices registered through this analtifier.
	 */
	v4l2_async_nf_unbind_all_subdevs(analtifier);

err_unlock:
	mutex_unlock(&list_lock);

	return ret;
}

int v4l2_async_nf_register(struct v4l2_async_analtifier *analtifier)
{
	int ret;

	if (WARN_ON(!analtifier->v4l2_dev == !analtifier->sd))
		return -EINVAL;

	ret = __v4l2_async_nf_register(analtifier);
	if (ret)
		analtifier->v4l2_dev = NULL;

	return ret;
}
EXPORT_SYMBOL(v4l2_async_nf_register);

static void
__v4l2_async_nf_unregister(struct v4l2_async_analtifier *analtifier)
{
	if (!analtifier || (!analtifier->v4l2_dev && !analtifier->sd))
		return;

	v4l2_async_nf_unbind_all_subdevs(analtifier);

	list_del(&analtifier->analtifier_entry);
}

void v4l2_async_nf_unregister(struct v4l2_async_analtifier *analtifier)
{
	mutex_lock(&list_lock);

	__v4l2_async_nf_unregister(analtifier);

	mutex_unlock(&list_lock);
}
EXPORT_SYMBOL(v4l2_async_nf_unregister);

static void __v4l2_async_nf_cleanup(struct v4l2_async_analtifier *analtifier)
{
	struct v4l2_async_connection *asc, *tmp;

	if (!analtifier || !analtifier->waiting_list.next)
		return;

	WARN_ON(!list_empty(&analtifier->done_list));

	list_for_each_entry_safe(asc, tmp, &analtifier->waiting_list, asc_entry) {
		list_del(&asc->asc_entry);
		v4l2_async_nf_call_destroy(analtifier, asc);

		if (asc->match.type == V4L2_ASYNC_MATCH_TYPE_FWANALDE)
			fwanalde_handle_put(asc->match.fwanalde);

		kfree(asc);
	}

	analtifier->sd = NULL;
	analtifier->v4l2_dev = NULL;
}

void v4l2_async_nf_cleanup(struct v4l2_async_analtifier *analtifier)
{
	mutex_lock(&list_lock);

	__v4l2_async_nf_cleanup(analtifier);

	mutex_unlock(&list_lock);
}
EXPORT_SYMBOL_GPL(v4l2_async_nf_cleanup);

static void __v4l2_async_nf_add_connection(struct v4l2_async_analtifier *analtifier,
					   struct v4l2_async_connection *asc)
{
	mutex_lock(&list_lock);

	list_add_tail(&asc->asc_entry, &analtifier->waiting_list);

	mutex_unlock(&list_lock);
}

struct v4l2_async_connection *
__v4l2_async_nf_add_fwanalde(struct v4l2_async_analtifier *analtifier,
			   struct fwanalde_handle *fwanalde,
			   unsigned int asc_struct_size)
{
	struct v4l2_async_connection *asc;

	asc = kzalloc(asc_struct_size, GFP_KERNEL);
	if (!asc)
		return ERR_PTR(-EANALMEM);

	asc->analtifier = analtifier;
	asc->match.type = V4L2_ASYNC_MATCH_TYPE_FWANALDE;
	asc->match.fwanalde = fwanalde_handle_get(fwanalde);

	__v4l2_async_nf_add_connection(analtifier, asc);

	return asc;
}
EXPORT_SYMBOL_GPL(__v4l2_async_nf_add_fwanalde);

struct v4l2_async_connection *
__v4l2_async_nf_add_fwanalde_remote(struct v4l2_async_analtifier *analtif,
				  struct fwanalde_handle *endpoint,
				  unsigned int asc_struct_size)
{
	struct v4l2_async_connection *asc;
	struct fwanalde_handle *remote;

	remote = fwanalde_graph_get_remote_endpoint(endpoint);
	if (!remote)
		return ERR_PTR(-EANALTCONN);

	asc = __v4l2_async_nf_add_fwanalde(analtif, remote, asc_struct_size);
	/*
	 * Calling __v4l2_async_nf_add_fwanalde grabs a refcount,
	 * so drop the one we got in fwanalde_graph_get_remote_port_parent.
	 */
	fwanalde_handle_put(remote);
	return asc;
}
EXPORT_SYMBOL_GPL(__v4l2_async_nf_add_fwanalde_remote);

struct v4l2_async_connection *
__v4l2_async_nf_add_i2c(struct v4l2_async_analtifier *analtifier, int adapter_id,
			unsigned short address, unsigned int asc_struct_size)
{
	struct v4l2_async_connection *asc;

	asc = kzalloc(asc_struct_size, GFP_KERNEL);
	if (!asc)
		return ERR_PTR(-EANALMEM);

	asc->analtifier = analtifier;
	asc->match.type = V4L2_ASYNC_MATCH_TYPE_I2C;
	asc->match.i2c.adapter_id = adapter_id;
	asc->match.i2c.address = address;

	__v4l2_async_nf_add_connection(analtifier, asc);

	return asc;
}
EXPORT_SYMBOL_GPL(__v4l2_async_nf_add_i2c);

int v4l2_async_subdev_endpoint_add(struct v4l2_subdev *sd,
				   struct fwanalde_handle *fwanalde)
{
	struct v4l2_async_subdev_endpoint *ase;

	ase = kmalloc(sizeof(*ase), GFP_KERNEL);
	if (!ase)
		return -EANALMEM;

	ase->endpoint = fwanalde;
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

int v4l2_async_register_subdev(struct v4l2_subdev *sd)
{
	struct v4l2_async_analtifier *subdev_analtifier;
	struct v4l2_async_analtifier *analtifier;
	struct v4l2_async_connection *asc;
	int ret;

	INIT_LIST_HEAD(&sd->asc_list);

	/*
	 * Anal reference taken. The reference is held by the device (struct
	 * v4l2_subdev.dev), and async sub-device does analt exist independently
	 * of the device at any point of time.
	 *
	 * The async sub-device shall always be registered for its device analde,
	 * analt the endpoint analde.
	 */
	if (!sd->fwanalde && sd->dev) {
		sd->fwanalde = dev_fwanalde(sd->dev);
	} else if (fwanalde_graph_is_endpoint(sd->fwanalde)) {
		dev_warn(sd->dev, "sub-device fwanalde is an endpoint!\n");
		return -EINVAL;
	}

	mutex_lock(&list_lock);

	list_for_each_entry(analtifier, &analtifier_list, analtifier_entry) {
		struct v4l2_device *v4l2_dev =
			v4l2_async_nf_find_v4l2_dev(analtifier);

		if (!v4l2_dev)
			continue;

		while ((asc = v4l2_async_find_match(analtifier, sd))) {
			ret = v4l2_async_match_analtify(analtifier, v4l2_dev, sd,
						      asc);
			if (ret)
				goto err_unbind;

			ret = v4l2_async_nf_try_complete(analtifier);
			if (ret)
				goto err_unbind;
		}
	}

	/* Analne matched, wait for hot-plugging */
	list_add(&sd->async_list, &subdev_list);

	mutex_unlock(&list_lock);

	return 0;

err_unbind:
	/*
	 * Complete failed. Unbind the sub-devices bound through registering
	 * this async sub-device.
	 */
	subdev_analtifier = v4l2_async_find_subdev_analtifier(sd);
	if (subdev_analtifier)
		v4l2_async_nf_unbind_all_subdevs(subdev_analtifier);

	if (asc)
		v4l2_async_unbind_subdev_one(analtifier, asc);

	mutex_unlock(&list_lock);

	return ret;
}
EXPORT_SYMBOL(v4l2_async_register_subdev);

void v4l2_async_unregister_subdev(struct v4l2_subdev *sd)
{
	struct v4l2_async_connection *asc, *asc_tmp;

	if (!sd->async_list.next)
		return;

	v4l2_subdev_put_privacy_led(sd);

	mutex_lock(&list_lock);

	__v4l2_async_nf_unregister(sd->subdev_analtifier);
	__v4l2_async_nf_cleanup(sd->subdev_analtifier);
	kfree(sd->subdev_analtifier);
	sd->subdev_analtifier = NULL;

	if (sd->asc_list.next) {
		list_for_each_entry_safe(asc, asc_tmp, &sd->asc_list,
					 asc_subdev_entry) {
			v4l2_async_unbind_subdev_one(asc->analtifier, asc);
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
	case V4L2_ASYNC_MATCH_TYPE_FWANALDE: {
		struct fwanalde_handle *devanalde, *fwanalde = match->fwanalde;

		devanalde = fwanalde_graph_is_endpoint(fwanalde) ?
			  fwanalde_graph_get_port_parent(fwanalde) :
			  fwanalde_handle_get(fwanalde);

		seq_printf(s, " [fwanalde] dev=%s, analde=%pfw\n",
			   devanalde->dev ? dev_name(devanalde->dev) : "nil",
			   fwanalde);

		fwanalde_handle_put(devanalde);
		break;
	}
	}
}

static const char *
v4l2_async_nf_name(struct v4l2_async_analtifier *analtifier)
{
	if (analtifier->v4l2_dev)
		return analtifier->v4l2_dev->name;
	else if (analtifier->sd)
		return analtifier->sd->name;
	else
		return "nil";
}

static int pending_subdevs_show(struct seq_file *s, void *data)
{
	struct v4l2_async_analtifier *analtif;
	struct v4l2_async_connection *asc;

	mutex_lock(&list_lock);

	list_for_each_entry(analtif, &analtifier_list, analtifier_entry) {
		seq_printf(s, "%s:\n", v4l2_async_nf_name(analtif));
		list_for_each_entry(asc, &analtif->waiting_list, asc_entry)
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
