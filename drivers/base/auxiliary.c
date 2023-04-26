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

/**
 * DOC: PURPOSE
 *
 * In some subsystems, the functionality of the core device (PCI/ACPI/other) is
 * too complex for a single device to be managed by a monolithic driver (e.g.
 * Sound Open Firmware), multiple devices might implement a common intersection
 * of functionality (e.g. NICs + RDMA), or a driver may want to export an
 * interface for another subsystem to drive (e.g. SIOV Physical Function export
 * Virtual Function management).  A split of the functionality into child-
 * devices representing sub-domains of functionality makes it possible to
 * compartmentalize, layer, and distribute domain-specific concerns via a Linux
 * device-driver model.
 *
 * An example for this kind of requirement is the audio subsystem where a
 * single IP is handling multiple entities such as HDMI, Soundwire, local
 * devices such as mics/speakers etc. The split for the core's functionality
 * can be arbitrary or be defined by the DSP firmware topology and include
 * hooks for test/debug. This allows for the audio core device to be minimal
 * and focused on hardware-specific control and communication.
 *
 * Each auxiliary_device represents a part of its parent functionality. The
 * generic behavior can be extended and specialized as needed by encapsulating
 * an auxiliary_device within other domain-specific structures and the use of
 * .ops callbacks. Devices on the auxiliary bus do not share any structures and
 * the use of a communication channel with the parent is domain-specific.
 *
 * Note that ops are intended as a way to augment instance behavior within a
 * class of auxiliary devices, it is not the mechanism for exporting common
 * infrastructure from the parent. Consider EXPORT_SYMBOL_NS() to convey
 * infrastructure from the parent module to the auxiliary module(s).
 */

/**
 * DOC: USAGE
 *
 * The auxiliary bus is to be used when a driver and one or more kernel
 * modules, who share a common header file with the driver, need a mechanism to
 * connect and provide access to a shared object allocated by the
 * auxiliary_device's registering driver.  The registering driver for the
 * auxiliary_device(s) and the kernel module(s) registering auxiliary_drivers
 * can be from the same subsystem, or from multiple subsystems.
 *
 * The emphasis here is on a common generic interface that keeps subsystem
 * customization out of the bus infrastructure.
 *
 * One example is a PCI network device that is RDMA-capable and exports a child
 * device to be driven by an auxiliary_driver in the RDMA subsystem.  The PCI
 * driver allocates and registers an auxiliary_device for each physical
 * function on the NIC.  The RDMA driver registers an auxiliary_driver that
 * claims each of these auxiliary_devices.  This conveys data/ops published by
 * the parent PCI device/driver to the RDMA auxiliary_driver.
 *
 * Another use case is for the PCI device to be split out into multiple sub
 * functions.  For each sub function an auxiliary_device is created.  A PCI sub
 * function driver binds to such devices that creates its own one or more class
 * devices.  A PCI sub function auxiliary device is likely to be contained in a
 * struct with additional attributes such as user defined sub function number
 * and optional attributes such as resources and a link to the parent device.
 * These attributes could be used by systemd/udev; and hence should be
 * initialized before a driver binds to an auxiliary_device.
 *
 * A key requirement for utilizing the auxiliary bus is that there is no
 * dependency on a physical bus, device, register accesses or regmap support.
 * These individual devices split from the core cannot live on the platform bus
 * as they are not physical devices that are controlled by DT/ACPI.  The same
 * argument applies for not using MFD in this scenario as MFD relies on
 * individual function devices being physical devices.
 */

/**
 * DOC: EXAMPLE
 *
 * Auxiliary devices are created and registered by a subsystem-level core
 * device that needs to break up its functionality into smaller fragments. One
 * way to extend the scope of an auxiliary_device is to encapsulate it within a
 * domain- pecific structure defined by the parent device. This structure
 * contains the auxiliary_device and any associated shared data/callbacks
 * needed to establish the connection with the parent.
 *
 * An example is:
 *
 * .. code-block:: c
 *
 *         struct foo {
 *		struct auxiliary_device auxdev;
 *		void (*connect)(struct auxiliary_device *auxdev);
 *		void (*disconnect)(struct auxiliary_device *auxdev);
 *		void *data;
 *        };
 *
 * The parent device then registers the auxiliary_device by calling
 * auxiliary_device_init(), and then auxiliary_device_add(), with the pointer
 * to the auxdev member of the above structure. The parent provides a name for
 * the auxiliary_device that, combined with the parent's KBUILD_MODNAME,
 * creates a match_name that is be used for matching and binding with a driver.
 *
 * Whenever an auxiliary_driver is registered, based on the match_name, the
 * auxiliary_driver's probe() is invoked for the matching devices.  The
 * auxiliary_driver can also be encapsulated inside custom drivers that make
 * the core device's functionality extensible by adding additional
 * domain-specific ops as follows:
 *
 * .. code-block:: c
 *
 *	struct my_ops {
 *		void (*send)(struct auxiliary_device *auxdev);
 *		void (*receive)(struct auxiliary_device *auxdev);
 *	};
 *
 *
 *	struct my_driver {
 *		struct auxiliary_driver auxiliary_drv;
 *		const struct my_ops ops;
 *	};
 *
 * An example of this type of usage is:
 *
 * .. code-block:: c
 *
 *	const struct auxiliary_device_id my_auxiliary_id_table[] = {
 *		{ .name = "foo_mod.foo_dev" },
 *		{ },
 *	};
 *
 *	const struct my_ops my_custom_ops = {
 *		.send = my_tx,
 *		.receive = my_rx,
 *	};
 *
 *	const struct my_driver my_drv = {
 *		.auxiliary_drv = {
 *			.name = "myauxiliarydrv",
 *			.id_table = my_auxiliary_id_table,
 *			.probe = my_probe,
 *			.remove = my_remove,
 *			.shutdown = my_shutdown,
 *		},
 *		.ops = my_custom_ops,
 *	};
 */

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

static int auxiliary_uevent(const struct device *dev, struct kobj_uevent_env *env)
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
 * This is the second step in the three-step process to register an
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
 * This is the third step in the three-step process to register an
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
 * The reference returned should be released with put_device().
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
 *
 * The expectation is that users will call the "auxiliary_driver_register"
 * macro so that the caller's KBUILD_MODNAME is automatically inserted for the
 * modname parameter.  Only if a user requires a custom name would this version
 * be called directly.
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
