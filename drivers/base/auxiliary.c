// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019-2020 Intel Corporation
 *
 * Please see Documentation/driver-api/auxiliary_bus.rst for more information.
 */

#define pr_fmt(fmt) "%s:%s: " fmt, KBUILD_MODNAME, __func__

#include <linux/device.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/string.h>
#include <linux/auxiliary_bus.h>
#include "base.h"

static const struct auxiliary_device_id *auxiliary_match_id(const struct auxiliary_device_id *id,
							    const struct auxiliary_device *auxdev)
{
	for (; id->name[0]; id++) {
		const char *p = strrchr(dev_name(&auxdev->dev), '.');
		int match_size;

		if (!p)
			continue;
		match_size = p - dev_name(&auxdev->dev);

		/* use dev_name(&auxdev->dev) prefix before last '.' char to match to */
		if (strlen(id->name) == match_size &&
		    !strncmp(dev_name(&auxdev->dev), id->name, match_size))
			return id;
	}
	return NULL;
}

static int auxiliary_match(struct device *dev, struct device_driver *drv)
{
	struct auxiliary_device *auxdev = to_auxiliary_dev(dev);
	struct auxiliary_driver *auxdrv = to_auxiliary_drv(drv);

	return !!auxiliary_match_id(auxdrv->id_table, auxdev);
}

static int auxiliary_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	const char *name, *p;

	name = dev_name(dev);
	p = strrchr(name, '.');

	return add_uevent_var(env, "MODALIAS=%s%.*s", AUXILIARY_MODULE_PREFIX,
			      (int)(p - name), name);
}

static const struct dev_pm_ops auxiliary_dev_pm_ops = {
	SET_RUNTIME_PM_OPS(pm_generic_runtime_suspend, pm_generic_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(pm_generic_suspend, pm_generic_resume)
};

static int auxiliary_bus_probe(struct device *dev)
{
	struct auxiliary_driver *auxdrv = to_auxiliary_drv(dev->driver);
	struct auxiliary_device *auxdev = to_auxiliary_dev(dev);
	int ret;

	ret = dev_pm_domain_attach(dev, true);
	if (ret) {
		dev_warn(dev, "Failed to attach to PM Domain : %d\n", ret);
		return ret;
	}

	ret = auxdrv->probe(auxdev, auxiliary_match_id(auxdrv->id_table, auxdev));
	if (ret)
		dev_pm_domain_detach(dev, true);

	return ret;
}

static void auxiliary_bus_remove(struct device *dev)
{
	struct auxiliary_driver *auxdrv = to_auxiliary_drv(dev->driver);
	struct auxiliary_device *auxdev = to_auxiliary_dev(dev);

	if (auxdrv->remove)
		auxdrv->remove(auxdev);
	dev_pm_domain_detach(dev, true);
}

static void auxiliary_bus_shutdown(struct device *dev)
{
	struct auxiliary_driver *auxdrv = NULL;
	struct auxiliary_device *auxdev;

	if (dev->driver) {
		auxdrv = to_auxiliary_drv(dev->driver);
		auxdev = to_auxiliary_dev(dev);
	}

	if (auxdrv && auxdrv->shutdown)
		auxdrv->shutdown(auxdev);
}

static struct bus_type auxiliary_bus_type = {
	.name = "auxiliary",
	.probe = auxiliary_bus_probe,
	.remove = auxiliary_bus_remove,
	.shutdown = auxiliary_bus_shutdown,
	.match = auxiliary_match,
	.uevent = auxiliary_uevent,
	.pm = &auxiliary_dev_pm_ops,
};

/**
 * auxiliary_device_init - check auxiliary_device and initialize
 * @auxdev: auxiliary device struct
 *
 * This is the first step in the two-step process to register an
 * auxiliary_device.
 *
 * When this function returns an error code, then the device_initialize will
 * *not* have been performed, and the caller will be responsible to free any
 * memory allocated for the auxiliary_device in the error path directly.
 *
 * It returns 0 on success.  On success, the device_initialize has been
 * performed.  After this point any error unwinding will need to include a call
 * to auxiliary_device_uninit().  In this post-initialize error scenario, a call
 * to the device's .release callback will be triggered, and all memory clean-up
 * is expected to be handled there.
 */
int auxiliary_device_init(struct auxiliary_device *auxdev)
{
	struct device *dev = &auxdev->dev;

	if (!dev->parent) {
		pr_err("auxiliary_device has a NULL dev->parent\n");
		return -EINVAL;
	}

	if (!auxdev->name) {
		pr_err("auxiliary_device has a NULL name\n");
		return -EINVAL;
	}

	dev->bus = &auxiliary_bus_type;
	device_initialize(&auxdev->dev);
	return 0;
}
EXPORT_SYMBOL_GPL(auxiliary_device_init);

/**
 * __auxiliary_device_add - add an auxiliary bus device
 * @auxdev: auxiliary bus device to add to the bus
 * @modname: name of the parent device's driver module
 *
 * This is the second step in the two-step process to register an
 * auxiliary_device.
 *
 * This function must be called after a successful call to
 * auxiliary_device_init(), which will perform the device_initialize.  This
 * means that if this returns an error code, then a call to
 * auxiliary_device_uninit() must be performed so that the .release callback
 * will be triggered to free the memory associated with the auxiliary_device.
 *
 * The expectation is that users will call the "auxiliary_device_add" macro so
 * that the caller's KBUILD_MODNAME is automatically inserted for the modname
 * parameter.  Only if a user requires a custom name would this version be
 * called directly.
 */
int __auxiliary_device_add(struct auxiliary_device *auxdev, const char *modname)
{
	struct device *dev = &auxdev->dev;
	int ret;

	if (!modname) {
		dev_err(dev, "auxiliary device modname is NULL\n");
		return -EINVAL;
	}

	ret = dev_set_name(dev, "%s.%s.%d", modname, auxdev->name, auxdev->id);
	if (ret) {
		dev_err(dev, "auxiliary device dev_set_name failed: %d\n", ret);
		return ret;
	}

	ret = device_add(dev);
	if (ret)
		dev_err(dev, "adding auxiliary device failed!: %d\n", ret);

	return ret;
}
EXPORT_SYMBOL_GPL(__auxiliary_device_add);

/**
 * auxiliary_find_device - auxiliary device iterator for locating a particular device.
 * @start: Device to begin with
 * @data: Data to pass to match function
 * @match: Callback function to check device
 *
 * This function returns a reference to a device that is 'found'
 * for later use, as determined by the @match callback.
 *
 * The callback should return 0 if the device doesn't match and non-zero
 * if it does.  If the callback returns non-zero, this function will
 * return to the caller and not iterate over any more devices.
 */
struct auxiliary_device *auxiliary_find_device(struct device *start,
					       const void *data,
					       int (*match)(struct device *dev, const void *data))
{
	struct device *dev;

	dev = bus_find_device(&auxiliary_bus_type, start, data, match);
	if (!dev)
		return NULL;

	return to_auxiliary_dev(dev);
}
EXPORT_SYMBOL_GPL(auxiliary_find_device);

/**
 * __auxiliary_driver_register - register a driver for auxiliary bus devices
 * @auxdrv: auxiliary_driver structure
 * @owner: owning module/driver
 * @modname: KBUILD_MODNAME for parent driver
 */
int __auxiliary_driver_register(struct auxiliary_driver *auxdrv,
				struct module *owner, const char *modname)
{
	int ret;

	if (WARN_ON(!auxdrv->probe) || WARN_ON(!auxdrv->id_table))
		return -EINVAL;

	if (auxdrv->name)
		auxdrv->driver.name = kasprintf(GFP_KERNEL, "%s.%s", modname,
						auxdrv->name);
	else
		auxdrv->driver.name = kasprintf(GFP_KERNEL, "%s", modname);
	if (!auxdrv->driver.name)
		return -ENOMEM;

	auxdrv->driver.owner = owner;
	auxdrv->driver.bus = &auxiliary_bus_type;
	auxdrv->driver.mod_name = modname;

	ret = driver_register(&auxdrv->driver);
	if (ret)
		kfree(auxdrv->driver.name);

	return ret;
}
EXPORT_SYMBOL_GPL(__auxiliary_driver_register);

/**
 * auxiliary_driver_unregister - unregister a driver
 * @auxdrv: auxiliary_driver structure
 */
void auxiliary_driver_unregister(struct auxiliary_driver *auxdrv)
{
	driver_unregister(&auxdrv->driver);
	kfree(auxdrv->driver.name);
}
EXPORT_SYMBOL_GPL(auxiliary_driver_unregister);

void __init auxiliary_bus_init(void)
{
	WARN_ON(bus_register(&auxiliary_bus_type));
}
