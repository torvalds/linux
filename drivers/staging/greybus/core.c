/*
 * Greybus "Core"
 *
 * Copyright 2014-2015 Google Inc.
 * Copyright 2014-2015 Linaro Ltd.
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
	struct gb_bundle *bundle = to_gb_bundle(dev);
	const struct greybus_bundle_id *id;

	id = gb_bundle_match_id(bundle, driver->id_table);
	if (id)
		return 1;
	/* FIXME - Dynamic ids? */
	return 0;
}

static int greybus_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct gb_module *module = NULL;
	struct gb_interface *intf = NULL;
	struct gb_bundle *bundle = NULL;
	struct gb_connection *connection = NULL;

	if (is_gb_endo(dev)) {
		/*
		 * Not much to do for an endo, just fall through, as the
		 * "default" attributes are good enough for us.
		 */
		return 0;
	}

	if (is_gb_module(dev)) {
		module = to_gb_module(dev);
	} else if (is_gb_interface(dev)) {
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
	struct gb_bundle *bundle = to_gb_bundle(dev);
	const struct greybus_bundle_id *id;
	int retval;

	/* match id */
	id = gb_bundle_match_id(bundle, driver->id_table);
	if (!id)
		return -ENODEV;

	retval = driver->probe(bundle, id);
	if (retval)
		return retval;

	return 0;
}

static int greybus_remove(struct device *dev)
{
	struct greybus_driver *driver = to_greybus_driver(dev->driver);
	struct gb_bundle *bundle = to_gb_bundle(dev);

	driver->disconnect(bundle);
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

void greybus_deregister_driver(struct greybus_driver *driver)
{
	driver_unregister(&driver->driver);
}
EXPORT_SYMBOL_GPL(greybus_deregister_driver);


static DEFINE_MUTEX(hd_mutex);

static void free_hd(struct kref *kref)
{
	struct greybus_host_device *hd;

	hd = container_of(kref, struct greybus_host_device, kref);

	kfree(hd);
	mutex_unlock(&hd_mutex);
}

struct greybus_host_device *greybus_create_hd(struct greybus_host_driver *driver,
					      struct device *parent,
					      size_t buffer_size_max)
{
	struct greybus_host_device *hd;

	/*
	 * Validate that the driver implements all of the callbacks
	 * so that we don't have to every time we make them.
	 */
	if ((!driver->message_send) || (!driver->message_cancel) ||
	    (!driver->submit_svc)) {
		pr_err("Must implement all greybus_host_driver callbacks!\n");
		return ERR_PTR(-EINVAL);
	}

	if (buffer_size_max < GB_OPERATION_MESSAGE_SIZE_MIN) {
		dev_err(parent, "greybus host-device buffers too small\n");
		return NULL;
	}

	/*
	 * Make sure to never allocate messages larger than what the Greybus
	 * protocol supports.
	 */
	if (buffer_size_max > GB_OPERATION_MESSAGE_SIZE_MAX) {
		dev_warn(parent, "limiting buffer size to %u\n",
			 GB_OPERATION_MESSAGE_SIZE_MAX);
		buffer_size_max = GB_OPERATION_MESSAGE_SIZE_MAX;
	}

	hd = kzalloc(sizeof(*hd) + driver->hd_priv_size, GFP_KERNEL);
	if (!hd)
		return ERR_PTR(-ENOMEM);

	kref_init(&hd->kref);
	hd->parent = parent;
	hd->driver = driver;
	INIT_LIST_HEAD(&hd->interfaces);
	INIT_LIST_HEAD(&hd->connections);
	ida_init(&hd->cport_id_map);
	hd->buffer_size_max = buffer_size_max;

	return hd;
}
EXPORT_SYMBOL_GPL(greybus_create_hd);

int greybus_endo_setup(struct greybus_host_device *hd, u16 endo_id,
			u8 ap_intf_id)
{
	struct gb_endo *endo;

	endo = gb_endo_create(hd, endo_id, ap_intf_id);
	if (IS_ERR(endo))
		return PTR_ERR(endo);
	hd->endo = endo;

	return 0;
}
EXPORT_SYMBOL_GPL(greybus_endo_setup);

void greybus_remove_hd(struct greybus_host_device *hd)
{
	/*
	 * Tear down all interfaces, modules, and the endo that is associated
	 * with this host controller before freeing the memory associated with
	 * the host controller.
	 */
	gb_interfaces_remove(hd);
	gb_endo_remove(hd->endo);
	kref_put_mutex(&hd->kref, free_hd, &hd_mutex);
}
EXPORT_SYMBOL_GPL(greybus_remove_hd);

static int __init gb_init(void)
{
	int retval;

	if (greybus_disabled())
		return -ENODEV;

	BUILD_BUG_ON(HOST_DEV_CPORT_ID_MAX >= (long)CPORT_ID_BAD);

	gb_debugfs_init();

	retval = bus_register(&greybus_bus_type);
	if (retval) {
		pr_err("bus_register failed\n");
		goto error_bus;
	}

	ida_init(&greybus_endo_id_map);

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

	return 0;	/* Success */

error_operation:
	gb_ap_exit();
error_ap:
	bus_unregister(&greybus_bus_type);
error_bus:
	gb_debugfs_cleanup();

	return retval;
}
module_init(gb_init);

static void __exit gb_exit(void)
{
	gb_operation_exit();
	gb_ap_exit();
	bus_unregister(&greybus_bus_type);
	gb_debugfs_cleanup();
}
module_exit(gb_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Greg Kroah-Hartman <gregkh@linuxfoundation.org>");
