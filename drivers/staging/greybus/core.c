/*
 * Greybus "Core"
 *
 * Copyright 2014 Google Inc.
 * Copyright 2014 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/types.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/device.h>

#include "greybus.h"

/* Allow greybus to be disabled at boot if needed */
static bool nogreybus;
#ifdef MODULE
module_param(nogreybus, bool, 0444);
#else
core_param(nogreybus, nogreybus, bool, 0444);
#endif
int greybus_disabled(void)
{
	return nogreybus;
}
EXPORT_SYMBOL_GPL(greybus_disabled);

static int greybus_module_match(struct device *dev, struct device_driver *drv)
{
	struct greybus_driver *driver = to_greybus_driver(drv);
	struct gb_interface *intf = to_gb_interface(dev);
	const struct greybus_interface_id *id;

	id = gb_interface_match_id(intf, driver->id_table);
	if (id)
		return 1;
	/* FIXME - Dynamic ids? */
	return 0;
}

static int greybus_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct gb_interface *intf = NULL;
	struct gb_bundle *bundle = NULL;
	struct gb_connection *connection = NULL;

	if (is_gb_interface(dev)) {
		intf = to_gb_interface(dev);
	} else if (is_gb_bundle(dev)) {
		bundle = to_gb_bundle(dev);
		intf = bundle->intf;
	} else if (is_gb_connection(dev)) {
		connection = to_gb_connection(dev);
		bundle = connection->bundle;
		intf = bundle->intf;
	} else {
		dev_WARN(dev, "uevent for unknown greybus device \"type\"!\n");
		return -EINVAL;
	}

	if (connection) {
		// FIXME
		// add a uevent that can "load" a connection type
		return 0;
	}

	if (bundle) {
		// FIXME
		// add a uevent that can "load" a bundle type
		// This is what we need to bind a driver to so use the info
		// in gmod here as well
		return 0;
	}

	// FIXME
	// "just" a module, be vague here, nothing binds to a module except
	// the greybus core, so there's not much, if anything, we need to
	// advertise.
	return 0;
}

struct bus_type greybus_bus_type = {
	.name =		"greybus",
	.match =	greybus_module_match,
	.uevent =	greybus_uevent,
};

static int greybus_probe(struct device *dev)
{
	struct greybus_driver *driver = to_greybus_driver(dev->driver);
	struct gb_interface *intf = to_gb_interface(dev);
	const struct greybus_interface_id *id;
	int retval;

	/* match id */
	id = gb_interface_match_id(intf, driver->id_table);
	if (!id)
		return -ENODEV;

	retval = driver->probe(intf, id);
	if (retval)
		return retval;

	return 0;
}

static int greybus_remove(struct device *dev)
{
	struct greybus_driver *driver = to_greybus_driver(dev->driver);
	struct gb_interface *intf = to_gb_interface(dev);

	driver->disconnect(intf);
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


static DEFINE_MUTEX(hd_mutex);

static void free_hd(struct kref *kref)
{
	struct greybus_host_device *hd;

	hd = container_of(kref, struct greybus_host_device, kref);

	kfree(hd);
	mutex_unlock(&hd_mutex);
}

struct greybus_host_device *greybus_create_hd(struct greybus_host_driver *driver,
					      struct device *parent)
{
	struct greybus_host_device *hd;

	/*
	 * Validate that the driver implements all of the callbacks
	 * so that we don't have to every time we make them.
	 */
	if ((!driver->buffer_send) || (!driver->buffer_cancel) ||
	    (!driver->submit_svc)) {
		pr_err("Must implement all greybus_host_driver callbacks!\n");
		return NULL;
	}

	hd = kzalloc(sizeof(*hd) + driver->hd_priv_size, GFP_KERNEL);
	if (!hd)
		return NULL;

	kref_init(&hd->kref);
	hd->parent = parent;
	hd->driver = driver;
	INIT_LIST_HEAD(&hd->modules);
	INIT_LIST_HEAD(&hd->connections);
	ida_init(&hd->cport_id_map);

	return hd;
}
EXPORT_SYMBOL_GPL(greybus_create_hd);

void greybus_remove_hd(struct greybus_host_device *hd)
{
	/* Tear down all modules that happen to be associated with this host
	 * controller */
	gb_remove_modules(hd);
	kref_put_mutex(&hd->kref, free_hd, &hd_mutex);
}
EXPORT_SYMBOL_GPL(greybus_remove_hd);

static int __init gb_init(void)
{
	int retval;

	BUILD_BUG_ON(HOST_DEV_CPORT_ID_MAX >= (long)CPORT_ID_BAD);

	retval = gb_debugfs_init();
	if (retval) {
		pr_err("debugfs failed\n");
		return retval;
	}

	retval = bus_register(&greybus_bus_type);
	if (retval) {
		pr_err("bus_register failed\n");
		goto error_bus;
	}

	retval = gb_ap_init();
	if (retval) {
		pr_err("gb_ap_init failed\n");
		goto error_ap;
	}

	retval = gb_operation_init();
	if (retval) {
		pr_err("gb_operation_init failed\n");
		goto error_operation;
	}

	if (!gb_protocol_init()) {
		/* This only fails for duplicate protocol registration */
		retval = -EEXIST;
		pr_err("gb_protocol_init failed\n");
		goto error_protocol;
	}

	return 0;	/* Success */

error_protocol:
	gb_operation_exit();
error_operation:
	gb_ap_exit();
error_ap:
	bus_unregister(&greybus_bus_type);
error_bus:
	gb_debugfs_cleanup();

	return retval;
}

static void __exit gb_exit(void)
{
	gb_protocol_exit();
	gb_operation_exit();
	gb_ap_exit();
	bus_unregister(&greybus_bus_type);
	gb_debugfs_cleanup();
}

module_init(gb_init);
module_exit(gb_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Greg Kroah-Hartman <gregkh@linuxfoundation.org>");
