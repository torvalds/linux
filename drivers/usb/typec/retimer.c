// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2022 Google LLC
 *
 * USB Type-C Retimer support.
 * Author: Prashant Malani <pmalani@chromium.org>
 *
 */

#include <linux/device.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/property.h>
#include <linux/slab.h>

#include "class.h"
#include "retimer.h"

static int retimer_fwnode_match(struct device *dev, const void *fwnode)
{
	return is_typec_retimer(dev) && device_match_fwnode(dev, fwnode);
}

static void *typec_retimer_match(const struct fwnode_handle *fwnode, const char *id, void *data)
{
	struct device *dev;

	if (id && !fwnode_property_present(fwnode, id))
		return NULL;

	dev = class_find_device(&retimer_class, NULL, fwnode,
				retimer_fwnode_match);

	return dev ? to_typec_retimer(dev) : ERR_PTR(-EPROBE_DEFER);
}

/**
 * fwnode_typec_retimer_get - Find USB Type-C retimer.
 * @fwnode: The caller device node.
 *
 * Finds a retimer linked to the caller. This function is primarily meant for the
 * Type-C drivers. Returns a reference to the retimer on success, NULL if no
 * matching connection was found, or ERR_PTR(-EPROBE_DEFER) when a connection
 * was found but the retimer has not been enumerated yet.
 */
struct typec_retimer *fwnode_typec_retimer_get(struct fwnode_handle *fwnode)
{
	struct typec_retimer *retimer;

	retimer = fwnode_connection_find_match(fwnode, "retimer-switch", NULL, typec_retimer_match);
	if (!IS_ERR_OR_NULL(retimer))
		WARN_ON(!try_module_get(retimer->dev.parent->driver->owner));

	return retimer;
}
EXPORT_SYMBOL_GPL(fwnode_typec_retimer_get);

/**
 * typec_retimer_put - Release handle to a retimer.
 * @retimer: USB Type-C Connector Retimer.
 *
 * Decrements reference count for @retimer.
 */
void typec_retimer_put(struct typec_retimer *retimer)
{
	if (!IS_ERR_OR_NULL(retimer)) {
		module_put(retimer->dev.parent->driver->owner);
		put_device(&retimer->dev);
	}
}
EXPORT_SYMBOL_GPL(typec_retimer_put);

int typec_retimer_set(struct typec_retimer *retimer, struct typec_retimer_state *state)
{
	if (IS_ERR_OR_NULL(retimer))
		return 0;

	return retimer->set(retimer, state);
}
EXPORT_SYMBOL_GPL(typec_retimer_set);

static void typec_retimer_release(struct device *dev)
{
	kfree(to_typec_retimer(dev));
}

const struct device_type typec_retimer_dev_type = {
	.name = "typec_retimer",
	.release = typec_retimer_release,
};

/**
 * typec_retimer_register - Register a retimer device.
 * @parent: Parent device.
 * @desc: Retimer description.
 *
 * Some USB Type-C connectors have their physical lines routed through retimers before they
 * reach muxes or host controllers. In some cases (for example: using alternate modes)
 * these retimers need to be reconfigured appropriately. This function registers retimer
 * switches which route and potentially modify the signals on the Type C physical lines
 * enroute to the host controllers.
 */
struct typec_retimer *
typec_retimer_register(struct device *parent, const struct typec_retimer_desc *desc)
{
	struct typec_retimer *retimer;
	int ret;

	if (!desc || !desc->set)
		return ERR_PTR(-EINVAL);

	retimer = kzalloc(sizeof(*retimer), GFP_KERNEL);
	if (!retimer)
		return ERR_PTR(-ENOMEM);

	retimer->set = desc->set;

	device_initialize(&retimer->dev);
	retimer->dev.parent = parent;
	retimer->dev.fwnode = desc->fwnode;
	retimer->dev.class = &retimer_class;
	retimer->dev.type = &typec_retimer_dev_type;
	retimer->dev.driver_data = desc->drvdata;
	dev_set_name(&retimer->dev, "%s-retimer",
		     desc->name ? desc->name : dev_name(parent));

	ret = device_add(&retimer->dev);
	if (ret) {
		dev_err(parent, "failed to register retimer (%d)\n", ret);
		put_device(&retimer->dev);
		return ERR_PTR(ret);
	}

	return retimer;
}
EXPORT_SYMBOL_GPL(typec_retimer_register);

/**
 * typec_retimer_unregister - Unregister retimer device.
 * @retimer: USB Type-C Connector retimer.
 *
 * Unregister retimer that was registered with typec_retimer_register().
 */
void typec_retimer_unregister(struct typec_retimer *retimer)
{
	if (!IS_ERR_OR_NULL(retimer))
		device_unregister(&retimer->dev);
}
EXPORT_SYMBOL_GPL(typec_retimer_unregister);

void *typec_retimer_get_drvdata(struct typec_retimer *retimer)
{
	return dev_get_drvdata(&retimer->dev);
}
EXPORT_SYMBOL_GPL(typec_retimer_get_drvdata);

struct class retimer_class = {
	.name = "retimer",
	.owner = THIS_MODULE,
};
