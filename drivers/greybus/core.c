// SPDX-License-Identifier: GPL-2.0
/*
 * Greybus "Core"
 *
 * Copyright 2014-2015 Google Inc.
 * Copyright 2014-2015 Linaro Ltd.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#define CREATE_TRACE_POINTS
#include <linux/greybus.h>
#include "greybus_trace.h"

#define GB_BUNDLE_AUTOSUSPEND_MS	3000

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

static int is_gb_host_device(const struct device *dev)
{
	return dev->type == &greybus_hd_type;
}

static int is_gb_module(const struct device *dev)
{
	return dev->type == &greybus_module_type;
}

static int is_gb_interface(const struct device *dev)
{
	return dev->type == &greybus_interface_type;
}

static int is_gb_control(const struct device *dev)
{
	return dev->type == &greybus_control_type;
}

static int is_gb_bundle(const struct device *dev)
{
	return dev->type == &greybus_bundle_type;
}

static int is_gb_svc(const struct device *dev)
{
	return dev->type == &greybus_svc_type;
}

static bool greybus_match_one_id(struct gb_bundle *bundle,
				 const struct greybus_bundle_id *id)
{
	if ((id->match_flags & GREYBUS_ID_MATCH_VENDOR) &&
	    (id->vendor != bundle->intf->vendor_id))
		return false;

	if ((id->match_flags & GREYBUS_ID_MATCH_PRODUCT) &&
	    (id->product != bundle->intf->product_id))
		return false;

	if ((id->match_flags & GREYBUS_ID_MATCH_CLASS) &&
	    (id->class != bundle->class))
		return false;

	return true;
}

static const struct greybus_bundle_id *
greybus_match_id(struct gb_bundle *bundle, const struct greybus_bundle_id *id)
{
	if (!id)
		return NULL;

	for (; id->vendor || id->product || id->class || id->driver_info;
									id++) {
		if (greybus_match_one_id(bundle, id))
			return id;
	}

	return NULL;
}

static int greybus_match_device(struct device *dev, struct device_driver *drv)
{
	struct greybus_driver *driver = to_greybus_driver(drv);
	struct gb_bundle *bundle;
	const struct greybus_bundle_id *id;

	if (!is_gb_bundle(dev))
		return 0;

	bundle = to_gb_bundle(dev);

	id = greybus_match_id(bundle, driver->id_table);
	if (id)
		return 1;
	/* FIXME - Dynamic ids? */
	return 0;
}

static int greybus_uevent(const struct device *dev, struct kobj_uevent_env *env)
{
	const struct gb_host_device *hd;
	const struct gb_module *module = NULL;
	const struct gb_interface *intf = NULL;
	const struct gb_control *control = NULL;
	const struct gb_bundle *bundle = NULL;
	const struct gb_svc *svc = NULL;

	if (is_gb_host_device(dev)) {
		hd = to_gb_host_device(dev);
	} else if (is_gb_module(dev)) {
		module = to_gb_module(dev);
		hd = module->hd;
	} else if (is_gb_interface(dev)) {
		intf = to_gb_interface(dev);
		module = intf->module;
		hd = intf->hd;
	} else if (is_gb_control(dev)) {
		control = to_gb_control(dev);
		intf = control->intf;
		module = intf->module;
		hd = intf->hd;
	} else if (is_gb_bundle(dev)) {
		bundle = to_gb_bundle(dev);
		intf = bundle->intf;
		module = intf->module;
		hd = intf->hd;
	} else if (is_gb_svc(dev)) {
		svc = to_gb_svc(dev);
		hd = svc->hd;
	} else {
		dev_WARN(dev, "uevent for unknown greybus device \"type\"!\n");
		return -EINVAL;
	}

	if (add_uevent_var(env, "BUS=%u", hd->bus_id))
		return -ENOMEM;

	if (module) {
		if (add_uevent_var(env, "MODULE=%u", module->module_id))
			return -ENOMEM;
	}

	if (intf) {
		if (add_uevent_var(env, "INTERFACE=%u", intf->interface_id))
			return -ENOMEM;
		if (add_uevent_var(env, "GREYBUS_ID=%08x/%08x",
				   intf->vendor_id, intf->product_id))
			return -ENOMEM;
	}

	if (bundle) {
		// FIXME
		// add a uevent that can "load" a bundle type
		// This is what we need to bind a driver to so use the info
		// in gmod here as well

		if (add_uevent_var(env, "BUNDLE=%u", bundle->id))
			return -ENOMEM;
		if (add_uevent_var(env, "BUNDLE_CLASS=%02x", bundle->class))
			return -ENOMEM;
	}

	return 0;
}

static void greybus_shutdown(struct device *dev)
{
	if (is_gb_host_device(dev)) {
		struct gb_host_device *hd;

		hd = to_gb_host_device(dev);
		gb_hd_shutdown(hd);
	}
}

const struct bus_type greybus_bus_type = {
	.name =		"greybus",
	.match =	greybus_match_device,
	.uevent =	greybus_uevent,
	.shutdown =	greybus_shutdown,
};

static int greybus_probe(struct device *dev)
{
	struct greybus_driver *driver = to_greybus_driver(dev->driver);
	struct gb_bundle *bundle = to_gb_bundle(dev);
	const struct greybus_bundle_id *id;
	int retval;

	/* match id */
	id = greybus_match_id(bundle, driver->id_table);
	if (!id)
		return -ENODEV;

	retval = pm_runtime_get_sync(&bundle->intf->dev);
	if (retval < 0) {
		pm_runtime_put_noidle(&bundle->intf->dev);
		return retval;
	}

	retval = gb_control_bundle_activate(bundle->intf->control, bundle->id);
	if (retval) {
		pm_runtime_put(&bundle->intf->dev);
		return retval;
	}

	/*
	 * Unbound bundle devices are always deactivated. During probe, the
	 * Runtime PM is set to enabled and active and the usage count is
	 * incremented. If the driver supports runtime PM, it should call
	 * pm_runtime_put() in its probe routine and pm_runtime_get_sync()
	 * in remove routine.
	 */
	pm_runtime_set_autosuspend_delay(dev, GB_BUNDLE_AUTOSUSPEND_MS);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_get_noresume(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	retval = driver->probe(bundle, id);
	if (retval) {
		/*
		 * Catch buggy drivers that fail to destroy their connections.
		 */
		WARN_ON(!list_empty(&bundle->connections));

		gb_control_bundle_deactivate(bundle->intf->control, bundle->id);

		pm_runtime_disable(dev);
		pm_runtime_set_suspended(dev);
		pm_runtime_put_noidle(dev);
		pm_runtime_dont_use_autosuspend(dev);
		pm_runtime_put(&bundle->intf->dev);

		return retval;
	}

	pm_runtime_put(&bundle->intf->dev);

	return 0;
}

static int greybus_remove(struct device *dev)
{
	struct greybus_driver *driver = to_greybus_driver(dev->driver);
	struct gb_bundle *bundle = to_gb_bundle(dev);
	struct gb_connection *connection;
	int retval;

	retval = pm_runtime_get_sync(dev);
	if (retval < 0)
		dev_err(dev, "failed to resume bundle: %d\n", retval);

	/*
	 * Disable (non-offloaded) connections early in case the interface is
	 * already gone to avoid unceccessary operation timeouts during
	 * driver disconnect. Otherwise, only disable incoming requests.
	 */
	list_for_each_entry(connection, &bundle->connections, bundle_links) {
		if (gb_connection_is_offloaded(connection))
			continue;

		if (bundle->intf->disconnected)
			gb_connection_disable_forced(connection);
		else
			gb_connection_disable_rx(connection);
	}

	driver->disconnect(bundle);

	/* Catch buggy drivers that fail to destroy their connections. */
	WARN_ON(!list_empty(&bundle->connections));

	if (!bundle->intf->disconnected)
		gb_control_bundle_deactivate(bundle->intf->control, bundle->id);

	pm_runtime_put_noidle(dev);
	pm_runtime_disable(dev);
	pm_runtime_set_suspended(dev);
	pm_runtime_dont_use_autosuspend(dev);
	pm_runtime_put_noidle(dev);

	return 0;
}

int greybus_register_driver(struct greybus_driver *driver, struct module *owner,
			    const char *mod_name)
{
	int retval;

	if (greybus_disabled())
		return -ENODEV;

	driver->driver.bus = &greybus_bus_type;
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

static int __init gb_init(void)
{
	int retval;

	if (greybus_disabled())
		return -ENODEV;

	BUILD_BUG_ON(CPORT_ID_MAX >= (long)CPORT_ID_BAD);

	gb_debugfs_init();

	retval = bus_register(&greybus_bus_type);
	if (retval) {
		pr_err("bus_register failed (%d)\n", retval);
		goto error_bus;
	}

	retval = gb_hd_init();
	if (retval) {
		pr_err("gb_hd_init failed (%d)\n", retval);
		goto error_hd;
	}

	retval = gb_operation_init();
	if (retval) {
		pr_err("gb_operation_init failed (%d)\n", retval);
		goto error_operation;
	}
	return 0;	/* Success */

error_operation:
	gb_hd_exit();
error_hd:
	bus_unregister(&greybus_bus_type);
error_bus:
	gb_debugfs_cleanup();

	return retval;
}
module_init(gb_init);

static void __exit gb_exit(void)
{
	gb_operation_exit();
	gb_hd_exit();
	bus_unregister(&greybus_bus_type);
	gb_debugfs_cleanup();
	tracepoint_synchronize_unregister();
}
module_exit(gb_exit);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Greg Kroah-Hartman <gregkh@linuxfoundation.org>");
