/*
 * Greybus "Core"
 *
 * Copyright 2014 Google Inc.
 *
 * Released under the GPLv2 only.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/types.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/device.h>

#include "greybus.h"

/* Allow greybus to be disabled at boot if needed */
static bool nogreybus;
#ifdef MODULE
module_param(nogreybus, bool, 0444);
#else
core_param(nogreybus, bool, 0444);
#endif
int greybus_disabled(void)
{
	return nogreybus;
}
EXPORT_SYMBOL_GPL(greybus_disabled);

static int greybus_match_one_id(struct greybus_device *gdev,
				const struct greybus_device_id *id)
{
	struct greybus_descriptor *des = &gdev->descriptor;

	if ((id->match_flags & GREYBUS_DEVICE_ID_MATCH_VENDOR) &&
	    (des->wVendor != id->wVendor))
		return 0;

	if ((id->match_flags & GREYBUS_DEVICE_ID_MATCH_PRODUCT) &&
	    (des->wProduct != id->wProduct))
		return 0;

	if ((id->match_flags & GREYBUS_DEVICE_ID_MATCH_SERIAL) &&
	    (des->lSerialNumber != id->lSerialNumber))
		return 0;

	return 1;
}

static const struct greybus_device_id *greybus_match_id(
		struct greybus_device *gdev,
		const struct greybus_device_id *id)
{
	if (id == NULL)
		return NULL;

	for (; id->wVendor || id->wProduct || id->lSerialNumber ||
	       id->driver_info ; id++) {
		if (greybus_match_one_id(gdev, id))
			return id;
	}

	return NULL;
}

static int greybus_device_match(struct device *dev, struct device_driver *drv)
{
	struct greybus_driver *driver = to_greybus_driver(dev->driver);
	struct greybus_device *gdev = to_greybus_device(dev);
	const struct greybus_device_id *id;

	id = greybus_match_id(gdev, driver->id_table);
	if (id)
		return 1;
	/* FIXME - Dyanmic ids? */
	return 0;
}

static int greybus_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	/* struct greybus_device *gdev = to_greybus_device(dev); */

	/* FIXME - add some uevents here... */
	return 0;
}

struct bus_type greybus_bus_type = {
	.name =		"greybus",
	.match =	greybus_device_match,
	.uevent =	greybus_uevent,
};

static int greybus_probe(struct device *dev)
{
	struct greybus_driver *driver = to_greybus_driver(dev->driver);
	struct greybus_device *gdev = to_greybus_device(dev);
	const struct greybus_device_id *id;
	int retval;

	/* match id */
	id = greybus_match_id(gdev, driver->id_table);
	if (!id)
		return -ENODEV;

	retval = driver->probe(gdev, id);
	if (retval)
		return retval;

	return 0;
}

static int greybus_remove(struct device *dev)
{
	struct greybus_driver *driver = to_greybus_driver(dev->driver);
	struct greybus_device *gdev = to_greybus_device(dev);

	driver->disconnect(gdev);
	return 0;
}

int greybus_register_driver(struct greybus_driver *driver, struct module *owner,
		const char *mod_name)
{
	int retval;

	if (greybus_disabled())
		return -ENODEV;

	driver->driver.name = driver->name;
	driver->driver.probe = greybus_probe;
	driver->driver.remove = greybus_remove;
	driver->driver.owner = owner;
	driver->driver.mod_name = mod_name;

	retval = driver_register(&driver->driver);
	if (retval)
		return retval;

	pr_info("registered new driver %s\n", driver->name);
	return 0;
}
EXPORT_SYMBOL_GPL(greybus_register_driver);

void greybus_deregister(struct greybus_driver *driver)
{
	driver_unregister(&driver->driver);
}
EXPORT_SYMBOL_GPL(greybus_deregister);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Greg Kroah-Hartman <gregkh@linuxfoundation.org>");
