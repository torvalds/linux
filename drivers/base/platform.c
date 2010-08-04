/*
 * platform.c - platform 'pseudo' bus for legacy devices
 *
 * Copyright (c) 2002-3 Patrick Mochel
 * Copyright (c) 2002-3 Open Source Development Labs
 *
 * This file is released under the GPLv2
 *
 * Please see Documentation/driver-model/platform.txt for more
 * information.
 */

#include <linux/string.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/bootmem.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>

#include "base.h"

#define to_platform_driver(drv)	(container_of((drv), struct platform_driver, \
				 driver))

struct device platform_bus = {
	.init_name	= "platform",
};
EXPORT_SYMBOL_GPL(platform_bus);

/**
 * platform_get_resource - get a resource for a device
 * @dev: platform device
 * @type: resource type
 * @num: resource index
 */
struct resource *platform_get_resource(struct platform_device *dev,
				       unsigned int type, unsigned int num)
{
	int i;

	for (i = 0; i < dev->num_resources; i++) {
		struct resource *r = &dev->resource[i];

		if (type == resource_type(r) && num-- == 0)
			return r;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(platform_get_resource);

/**
 * platform_get_irq - get an IRQ for a device
 * @dev: platform device
 * @num: IRQ number index
 */
int platform_get_irq(struct platform_device *dev, unsigned int num)
{
	struct resource *r = platform_get_resource(dev, IORESOURCE_IRQ, num);

	return r ? r->start : -ENXIO;
}
EXPORT_SYMBOL_GPL(platform_get_irq);

/**
 * platform_get_resource_byname - get a resource for a device by name
 * @dev: platform device
 * @type: resource type
 * @name: resource name
 */
struct resource *platform_get_resource_byname(struct platform_device *dev,
					      unsigned int type,
					      const char *name)
{
	int i;

	for (i = 0; i < dev->num_resources; i++) {
		struct resource *r = &dev->resource[i];

		if (type == resource_type(r) && !strcmp(r->name, name))
			return r;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(platform_get_resource_byname);

/**
 * platform_get_irq - get an IRQ for a device
 * @dev: platform device
 * @name: IRQ name
 */
int platform_get_irq_byname(struct platform_device *dev, const char *name)
{
	struct resource *r = platform_get_resource_byname(dev, IORESOURCE_IRQ,
							  name);

	return r ? r->start : -ENXIO;
}
EXPORT_SYMBOL_GPL(platform_get_irq_byname);

/**
 * platform_add_devices - add a numbers of platform devices
 * @devs: array of platform devices to add
 * @num: number of platform devices in array
 */
int platform_add_devices(struct platform_device **devs, int num)
{
	int i, ret = 0;

	for (i = 0; i < num; i++) {
		ret = platform_device_register(devs[i]);
		if (ret) {
			while (--i >= 0)
				platform_device_unregister(devs[i]);
			break;
		}
	}

	return ret;
}
EXPORT_SYMBOL_GPL(platform_add_devices);

struct platform_object {
	struct platform_device pdev;
	char name[1];
};

/**
 * platform_device_put - destroy a platform device
 * @pdev: platform device to free
 *
 * Free all memory associated with a platform device.  This function must
 * _only_ be externally called in error cases.  All other usage is a bug.
 */
void platform_device_put(struct platform_device *pdev)
{
	if (pdev)
		put_device(&pdev->dev);
}
EXPORT_SYMBOL_GPL(platform_device_put);

static void platform_device_release(struct device *dev)
{
	struct platform_object *pa = container_of(dev, struct platform_object,
						  pdev.dev);

	kfree(pa->pdev.dev.platform_data);
	kfree(pa->pdev.resource);
	kfree(pa);
}

/**
 * platform_device_alloc - create a platform device
 * @name: base name of the device we're adding
 * @id: instance id
 *
 * Create a platform device object which can have other objects attached
 * to it, and which will have attached objects freed when it is released.
 */
struct platform_device *platform_device_alloc(const char *name, int id)
{
	struct platform_object *pa;

	pa = kzalloc(sizeof(struct platform_object) + strlen(name), GFP_KERNEL);
	if (pa) {
		strcpy(pa->name, name);
		pa->pdev.name = pa->name;
		pa->pdev.id = id;
		device_initialize(&pa->pdev.dev);
		pa->pdev.dev.release = platform_device_release;
	}

	return pa ? &pa->pdev : NULL;
}
EXPORT_SYMBOL_GPL(platform_device_alloc);

/**
 * platform_device_add_resources - add resources to a platform device
 * @pdev: platform device allocated by platform_device_alloc to add resources to
 * @res: set of resources that needs to be allocated for the device
 * @num: number of resources
 *
 * Add a copy of the resources to the platform device.  The memory
 * associated with the resources will be freed when the platform device is
 * released.
 */
int platform_device_add_resources(struct platform_device *pdev,
				  const struct resource *res, unsigned int num)
{
	struct resource *r;

	r = kmalloc(sizeof(struct resource) * num, GFP_KERNEL);
	if (r) {
		memcpy(r, res, sizeof(struct resource) * num);
		pdev->resource = r;
		pdev->num_resources = num;
	}
	return r ? 0 : -ENOMEM;
}
EXPORT_SYMBOL_GPL(platform_device_add_resources);

/**
 * platform_device_add_data - add platform-specific data to a platform device
 * @pdev: platform device allocated by platform_device_alloc to add resources to
 * @data: platform specific data for this platform device
 * @size: size of platform specific data
 *
 * Add a copy of platform specific data to the platform device's
 * platform_data pointer.  The memory associated with the platform data
 * will be freed when the platform device is released.
 */
int platform_device_add_data(struct platform_device *pdev, const void *data,
			     size_t size)
{
	void *d = kmemdup(data, size, GFP_KERNEL);

	if (d) {
		pdev->dev.platform_data = d;
		return 0;
	}
	return -ENOMEM;
}
EXPORT_SYMBOL_GPL(platform_device_add_data);

/**
 * platform_device_add - add a platform device to device hierarchy
 * @pdev: platform device we're adding
 *
 * This is part 2 of platform_device_register(), though may be called
 * separately _iff_ pdev was allocated by platform_device_alloc().
 */
int platform_device_add(struct platform_device *pdev)
{
	int i, ret = 0;

	if (!pdev)
		return -EINVAL;

	if (!pdev->dev.parent)
		pdev->dev.parent = &platform_bus;

	pdev->dev.bus = &platform_bus_type;

	if (pdev->id != -1)
		dev_set_name(&pdev->dev, "%s.%d", pdev->name,  pdev->id);
	else
		dev_set_name(&pdev->dev, "%s", pdev->name);

	for (i = 0; i < pdev->num_resources; i++) {
		struct resource *p, *r = &pdev->resource[i];

		if (r->name == NULL)
			r->name = dev_name(&pdev->dev);

		p = r->parent;
		if (!p) {
			if (resource_type(r) == IORESOURCE_MEM)
				p = &iomem_resource;
			else if (resource_type(r) == IORESOURCE_IO)
				p = &ioport_resource;
		}

		if (p && insert_resource(p, r)) {
			printk(KERN_ERR
			       "%s: failed to claim resource %d\n",
			       dev_name(&pdev->dev), i);
			ret = -EBUSY;
			goto failed;
		}
	}

	pr_debug("Registering platform device '%s'. Parent at %s\n",
		 dev_name(&pdev->dev), dev_name(pdev->dev.parent));

	ret = device_add(&pdev->dev);
	if (ret == 0)
		return ret;

 failed:
	while (--i >= 0) {
		struct resource *r = &pdev->resource[i];
		unsigned long type = resource_type(r);

		if (type == IORESOURCE_MEM || type == IORESOURCE_IO)
			release_resource(r);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(platform_device_add);

/**
 * platform_device_del - remove a platform-level device
 * @pdev: platform device we're removing
 *
 * Note that this function will also release all memory- and port-based
 * resources owned by the device (@dev->resource).  This function must
 * _only_ be externally called in error cases.  All other usage is a bug.
 */
void platform_device_del(struct platform_device *pdev)
{
	int i;

	if (pdev) {
		device_del(&pdev->dev);

		for (i = 0; i < pdev->num_resources; i++) {
			struct resource *r = &pdev->resource[i];
			unsigned long type = resource_type(r);

			if (type == IORESOURCE_MEM || type == IORESOURCE_IO)
				release_resource(r);
		}
	}
}
EXPORT_SYMBOL_GPL(platform_device_del);

/**
 * platform_device_register - add a platform-level device
 * @pdev: platform device we're adding
 */
int platform_device_register(struct platform_device *pdev)
{
	device_initialize(&pdev->dev);
	return platform_device_add(pdev);
}
EXPORT_SYMBOL_GPL(platform_device_register);

/**
 * platform_device_unregister - unregister a platform-level device
 * @pdev: platform device we're unregistering
 *
 * Unregistration is done in 2 steps. First we release all resources
 * and remove it from the subsystem, then we drop reference count by
 * calling platform_device_put().
 */
void platform_device_unregister(struct platform_device *pdev)
{
	platform_device_del(pdev);
	platform_device_put(pdev);
}
EXPORT_SYMBOL_GPL(platform_device_unregister);

/**
 * platform_device_register_simple - add a platform-level device and its resources
 * @name: base name of the device we're adding
 * @id: instance id
 * @res: set of resources that needs to be allocated for the device
 * @num: number of resources
 *
 * This function creates a simple platform device that requires minimal
 * resource and memory management. Canned release function freeing memory
 * allocated for the device allows drivers using such devices to be
 * unloaded without waiting for the last reference to the device to be
 * dropped.
 *
 * This interface is primarily intended for use with legacy drivers which
 * probe hardware directly.  Because such drivers create sysfs device nodes
 * themselves, rather than letting system infrastructure handle such device
 * enumeration tasks, they don't fully conform to the Linux driver model.
 * In particular, when such drivers are built as modules, they can't be
 * "hotplugged".
 *
 * Returns &struct platform_device pointer on success, or ERR_PTR() on error.
 */
struct platform_device *platform_device_register_simple(const char *name,
							int id,
							const struct resource *res,
							unsigned int num)
{
	struct platform_device *pdev;
	int retval;

	pdev = platform_device_alloc(name, id);
	if (!pdev) {
		retval = -ENOMEM;
		goto error;
	}

	if (num) {
		retval = platform_device_add_resources(pdev, res, num);
		if (retval)
			goto error;
	}

	retval = platform_device_add(pdev);
	if (retval)
		goto error;

	return pdev;

error:
	platform_device_put(pdev);
	return ERR_PTR(retval);
}
EXPORT_SYMBOL_GPL(platform_device_register_simple);

/**
 * platform_device_register_data - add a platform-level device with platform-specific data
 * @parent: parent device for the device we're adding
 * @name: base name of the device we're adding
 * @id: instance id
 * @data: platform specific data for this platform device
 * @size: size of platform specific data
 *
 * This function creates a simple platform device that requires minimal
 * resource and memory management. Canned release function freeing memory
 * allocated for the device allows drivers using such devices to be
 * unloaded without waiting for the last reference to the device to be
 * dropped.
 *
 * Returns &struct platform_device pointer on success, or ERR_PTR() on error.
 */
struct platform_device *platform_device_register_data(
		struct device *parent,
		const char *name, int id,
		const void *data, size_t size)
{
	struct platform_device *pdev;
	int retval;

	pdev = platform_device_alloc(name, id);
	if (!pdev) {
		retval = -ENOMEM;
		goto error;
	}

	pdev->dev.parent = parent;

	if (size) {
		retval = platform_device_add_data(pdev, data, size);
		if (retval)
			goto error;
	}

	retval = platform_device_add(pdev);
	if (retval)
		goto error;

	return pdev;

error:
	platform_device_put(pdev);
	return ERR_PTR(retval);
}
EXPORT_SYMBOL_GPL(platform_device_register_data);

static int platform_drv_probe(struct device *_dev)
{
	struct platform_driver *drv = to_platform_driver(_dev->driver);
	struct platform_device *dev = to_platform_device(_dev);

	return drv->probe(dev);
}

static int platform_drv_probe_fail(struct device *_dev)
{
	return -ENXIO;
}

static int platform_drv_remove(struct device *_dev)
{
	struct platform_driver *drv = to_platform_driver(_dev->driver);
	struct platform_device *dev = to_platform_device(_dev);

	return drv->remove(dev);
}

static void platform_drv_shutdown(struct device *_dev)
{
	struct platform_driver *drv = to_platform_driver(_dev->driver);
	struct platform_device *dev = to_platform_device(_dev);

	drv->shutdown(dev);
}

/**
 * platform_driver_register - register a driver for platform-level devices
 * @drv: platform driver structure
 */
int platform_driver_register(struct platform_driver *drv)
{
	drv->driver.bus = &platform_bus_type;
	if (drv->probe)
		drv->driver.probe = platform_drv_probe;
	if (drv->remove)
		drv->driver.remove = platform_drv_remove;
	if (drv->shutdown)
		drv->driver.shutdown = platform_drv_shutdown;

	return driver_register(&drv->driver);
}
EXPORT_SYMBOL_GPL(platform_driver_register);

/**
 * platform_driver_unregister - unregister a driver for platform-level devices
 * @drv: platform driver structure
 */
void platform_driver_unregister(struct platform_driver *drv)
{
	driver_unregister(&drv->driver);
}
EXPORT_SYMBOL_GPL(platform_driver_unregister);

/**
 * platform_driver_probe - register driver for non-hotpluggable device
 * @drv: platform driver structure
 * @probe: the driver probe routine, probably from an __init section
 *
 * Use this instead of platform_driver_register() when you know the device
 * is not hotpluggable and has already been registered, and you want to
 * remove its run-once probe() infrastructure from memory after the driver
 * has bound to the device.
 *
 * One typical use for this would be with drivers for controllers integrated
 * into system-on-chip processors, where the controller devices have been
 * configured as part of board setup.
 *
 * Returns zero if the driver registered and bound to a device, else returns
 * a negative error code and with the driver not registered.
 */
int __init_or_module platform_driver_probe(struct platform_driver *drv,
		int (*probe)(struct platform_device *))
{
	int retval, code;

	/* make sure driver won't have bind/unbind attributes */
	drv->driver.suppress_bind_attrs = true;

	/* temporary section violation during probe() */
	drv->probe = probe;
	retval = code = platform_driver_register(drv);

	/*
	 * Fixup that section violation, being paranoid about code scanning
	 * the list of drivers in order to probe new devices.  Check to see
	 * if the probe was successful, and make sure any forced probes of
	 * new devices fail.
	 */
	spin_lock(&platform_bus_type.p->klist_drivers.k_lock);
	drv->probe = NULL;
	if (code == 0 && list_empty(&drv->driver.p->klist_devices.k_list))
		retval = -ENODEV;
	drv->driver.probe = platform_drv_probe_fail;
	spin_unlock(&platform_bus_type.p->klist_drivers.k_lock);

	if (code != retval)
		platform_driver_unregister(drv);
	return retval;
}
EXPORT_SYMBOL_GPL(platform_driver_probe);

/**
 * platform_create_bundle - register driver and create corresponding device
 * @driver: platform driver structure
 * @probe: the driver probe routine, probably from an __init section
 * @res: set of resources that needs to be allocated for the device
 * @n_res: number of resources
 * @data: platform specific data for this platform device
 * @size: size of platform specific data
 *
 * Use this in legacy-style modules that probe hardware directly and
 * register a single platform device and corresponding platform driver.
 *
 * Returns &struct platform_device pointer on success, or ERR_PTR() on error.
 */
struct platform_device * __init_or_module platform_create_bundle(
			struct platform_driver *driver,
			int (*probe)(struct platform_device *),
			struct resource *res, unsigned int n_res,
			const void *data, size_t size)
{
	struct platform_device *pdev;
	int error;

	pdev = platform_device_alloc(driver->driver.name, -1);
	if (!pdev) {
		error = -ENOMEM;
		goto err_out;
	}

	if (res) {
		error = platform_device_add_resources(pdev, res, n_res);
		if (error)
			goto err_pdev_put;
	}

	if (data) {
		error = platform_device_add_data(pdev, data, size);
		if (error)
			goto err_pdev_put;
	}

	error = platform_device_add(pdev);
	if (error)
		goto err_pdev_put;

	error = platform_driver_probe(driver, probe);
	if (error)
		goto err_pdev_del;

	return pdev;

err_pdev_del:
	platform_device_del(pdev);
err_pdev_put:
	platform_device_put(pdev);
err_out:
	return ERR_PTR(error);
}
EXPORT_SYMBOL_GPL(platform_create_bundle);

/* modalias support enables more hands-off userspace setup:
 * (a) environment variable lets new-style hotplug events work once system is
 *     fully running:  "modprobe $MODALIAS"
 * (b) sysfs attribute lets new-style coldplug recover from hotplug events
 *     mishandled before system is fully running:  "modprobe $(cat modalias)"
 */
static ssize_t modalias_show(struct device *dev, struct device_attribute *a,
			     char *buf)
{
	struct platform_device	*pdev = to_platform_device(dev);
	int len = snprintf(buf, PAGE_SIZE, "platform:%s\n", pdev->name);

	return (len >= PAGE_SIZE) ? (PAGE_SIZE - 1) : len;
}

static struct device_attribute platform_dev_attrs[] = {
	__ATTR_RO(modalias),
	__ATTR_NULL,
};

static int platform_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct platform_device	*pdev = to_platform_device(dev);

	add_uevent_var(env, "MODALIAS=%s%s", PLATFORM_MODULE_PREFIX,
		(pdev->id_entry) ? pdev->id_entry->name : pdev->name);
	return 0;
}

static const struct platform_device_id *platform_match_id(
			const struct platform_device_id *id,
			struct platform_device *pdev)
{
	while (id->name[0]) {
		if (strcmp(pdev->name, id->name) == 0) {
			pdev->id_entry = id;
			return id;
		}
		id++;
	}
	return NULL;
}

/**
 * platform_match - bind platform device to platform driver.
 * @dev: device.
 * @drv: driver.
 *
 * Platform device IDs are assumed to be encoded like this:
 * "<name><instance>", where <name> is a short description of the type of
 * device, like "pci" or "floppy", and <instance> is the enumerated
 * instance of the device, like '0' or '42'.  Driver IDs are simply
 * "<name>".  So, extract the <name> from the platform_device structure,
 * and compare it against the name of the driver. Return whether they match
 * or not.
 */
static int platform_match(struct device *dev, struct device_driver *drv)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct platform_driver *pdrv = to_platform_driver(drv);

	/* match against the id table first */
	if (pdrv->id_table)
		return platform_match_id(pdrv->id_table, pdev) != NULL;

	/* fall-back to driver name match */
	return (strcmp(pdev->name, drv->name) == 0);
}

#ifdef CONFIG_PM_SLEEP

static int platform_legacy_suspend(struct device *dev, pm_message_t mesg)
{
	struct platform_driver *pdrv = to_platform_driver(dev->driver);
	struct platform_device *pdev = to_platform_device(dev);
	int ret = 0;

	if (dev->driver && pdrv->suspend)
		ret = pdrv->suspend(pdev, mesg);

	return ret;
}

static int platform_legacy_resume(struct device *dev)
{
	struct platform_driver *pdrv = to_platform_driver(dev->driver);
	struct platform_device *pdev = to_platform_device(dev);
	int ret = 0;

	if (dev->driver && pdrv->resume)
		ret = pdrv->resume(pdev);

	return ret;
}

static int platform_pm_prepare(struct device *dev)
{
	struct device_driver *drv = dev->driver;
	int ret = 0;

	if (drv && drv->pm && drv->pm->prepare)
		ret = drv->pm->prepare(dev);

	return ret;
}

static void platform_pm_complete(struct device *dev)
{
	struct device_driver *drv = dev->driver;

	if (drv && drv->pm && drv->pm->complete)
		drv->pm->complete(dev);
}

#else /* !CONFIG_PM_SLEEP */

#define platform_pm_prepare		NULL
#define platform_pm_complete		NULL

#endif /* !CONFIG_PM_SLEEP */

#ifdef CONFIG_SUSPEND

int __weak platform_pm_suspend(struct device *dev)
{
	struct device_driver *drv = dev->driver;
	int ret = 0;

	if (!drv)
		return 0;

	if (drv->pm) {
		if (drv->pm->suspend)
			ret = drv->pm->suspend(dev);
	} else {
		ret = platform_legacy_suspend(dev, PMSG_SUSPEND);
	}

	return ret;
}

int __weak platform_pm_suspend_noirq(struct device *dev)
{
	struct device_driver *drv = dev->driver;
	int ret = 0;

	if (!drv)
		return 0;

	if (drv->pm) {
		if (drv->pm->suspend_noirq)
			ret = drv->pm->suspend_noirq(dev);
	}

	return ret;
}

int __weak platform_pm_resume(struct device *dev)
{
	struct device_driver *drv = dev->driver;
	int ret = 0;

	if (!drv)
		return 0;

	if (drv->pm) {
		if (drv->pm->resume)
			ret = drv->pm->resume(dev);
	} else {
		ret = platform_legacy_resume(dev);
	}

	return ret;
}

int __weak platform_pm_resume_noirq(struct device *dev)
{
	struct device_driver *drv = dev->driver;
	int ret = 0;

	if (!drv)
		return 0;

	if (drv->pm) {
		if (drv->pm->resume_noirq)
			ret = drv->pm->resume_noirq(dev);
	}

	return ret;
}

#else /* !CONFIG_SUSPEND */

#define platform_pm_suspend		NULL
#define platform_pm_resume		NULL
#define platform_pm_suspend_noirq	NULL
#define platform_pm_resume_noirq	NULL

#endif /* !CONFIG_SUSPEND */

#ifdef CONFIG_HIBERNATION

static int platform_pm_freeze(struct device *dev)
{
	struct device_driver *drv = dev->driver;
	int ret = 0;

	if (!drv)
		return 0;

	if (drv->pm) {
		if (drv->pm->freeze)
			ret = drv->pm->freeze(dev);
	} else {
		ret = platform_legacy_suspend(dev, PMSG_FREEZE);
	}

	return ret;
}

static int platform_pm_freeze_noirq(struct device *dev)
{
	struct device_driver *drv = dev->driver;
	int ret = 0;

	if (!drv)
		return 0;

	if (drv->pm) {
		if (drv->pm->freeze_noirq)
			ret = drv->pm->freeze_noirq(dev);
	}

	return ret;
}

static int platform_pm_thaw(struct device *dev)
{
	struct device_driver *drv = dev->driver;
	int ret = 0;

	if (!drv)
		return 0;

	if (drv->pm) {
		if (drv->pm->thaw)
			ret = drv->pm->thaw(dev);
	} else {
		ret = platform_legacy_resume(dev);
	}

	return ret;
}

static int platform_pm_thaw_noirq(struct device *dev)
{
	struct device_driver *drv = dev->driver;
	int ret = 0;

	if (!drv)
		return 0;

	if (drv->pm) {
		if (drv->pm->thaw_noirq)
			ret = drv->pm->thaw_noirq(dev);
	}

	return ret;
}

static int platform_pm_poweroff(struct device *dev)
{
	struct device_driver *drv = dev->driver;
	int ret = 0;

	if (!drv)
		return 0;

	if (drv->pm) {
		if (drv->pm->poweroff)
			ret = drv->pm->poweroff(dev);
	} else {
		ret = platform_legacy_suspend(dev, PMSG_HIBERNATE);
	}

	return ret;
}

static int platform_pm_poweroff_noirq(struct device *dev)
{
	struct device_driver *drv = dev->driver;
	int ret = 0;

	if (!drv)
		return 0;

	if (drv->pm) {
		if (drv->pm->poweroff_noirq)
			ret = drv->pm->poweroff_noirq(dev);
	}

	return ret;
}

static int platform_pm_restore(struct device *dev)
{
	struct device_driver *drv = dev->driver;
	int ret = 0;

	if (!drv)
		return 0;

	if (drv->pm) {
		if (drv->pm->restore)
			ret = drv->pm->restore(dev);
	} else {
		ret = platform_legacy_resume(dev);
	}

	return ret;
}

static int platform_pm_restore_noirq(struct device *dev)
{
	struct device_driver *drv = dev->driver;
	int ret = 0;

	if (!drv)
		return 0;

	if (drv->pm) {
		if (drv->pm->restore_noirq)
			ret = drv->pm->restore_noirq(dev);
	}

	return ret;
}

#else /* !CONFIG_HIBERNATION */

#define platform_pm_freeze		NULL
#define platform_pm_thaw		NULL
#define platform_pm_poweroff		NULL
#define platform_pm_restore		NULL
#define platform_pm_freeze_noirq	NULL
#define platform_pm_thaw_noirq		NULL
#define platform_pm_poweroff_noirq	NULL
#define platform_pm_restore_noirq	NULL

#endif /* !CONFIG_HIBERNATION */

#ifdef CONFIG_PM_RUNTIME

int __weak platform_pm_runtime_suspend(struct device *dev)
{
	return pm_generic_runtime_suspend(dev);
};

int __weak platform_pm_runtime_resume(struct device *dev)
{
	return pm_generic_runtime_resume(dev);
};

int __weak platform_pm_runtime_idle(struct device *dev)
{
	return pm_generic_runtime_idle(dev);
};

#else /* !CONFIG_PM_RUNTIME */

#define platform_pm_runtime_suspend NULL
#define platform_pm_runtime_resume NULL
#define platform_pm_runtime_idle NULL

#endif /* !CONFIG_PM_RUNTIME */

static const struct dev_pm_ops platform_dev_pm_ops = {
	.prepare = platform_pm_prepare,
	.complete = platform_pm_complete,
	.suspend = platform_pm_suspend,
	.resume = platform_pm_resume,
	.freeze = platform_pm_freeze,
	.thaw = platform_pm_thaw,
	.poweroff = platform_pm_poweroff,
	.restore = platform_pm_restore,
	.suspend_noirq = platform_pm_suspend_noirq,
	.resume_noirq = platform_pm_resume_noirq,
	.freeze_noirq = platform_pm_freeze_noirq,
	.thaw_noirq = platform_pm_thaw_noirq,
	.poweroff_noirq = platform_pm_poweroff_noirq,
	.restore_noirq = platform_pm_restore_noirq,
	.runtime_suspend = platform_pm_runtime_suspend,
	.runtime_resume = platform_pm_runtime_resume,
	.runtime_idle = platform_pm_runtime_idle,
};

struct bus_type platform_bus_type = {
	.name		= "platform",
	.dev_attrs	= platform_dev_attrs,
	.match		= platform_match,
	.uevent		= platform_uevent,
	.pm		= &platform_dev_pm_ops,
};
EXPORT_SYMBOL_GPL(platform_bus_type);

int __init platform_bus_init(void)
{
	int error;

	early_platform_cleanup();

	error = device_register(&platform_bus);
	if (error)
		return error;
	error =  bus_register(&platform_bus_type);
	if (error)
		device_unregister(&platform_bus);
	return error;
}

#ifndef ARCH_HAS_DMA_GET_REQUIRED_MASK
u64 dma_get_required_mask(struct device *dev)
{
	u32 low_totalram = ((max_pfn - 1) << PAGE_SHIFT);
	u32 high_totalram = ((max_pfn - 1) >> (32 - PAGE_SHIFT));
	u64 mask;

	if (!high_totalram) {
		/* convert to mask just covering totalram */
		low_totalram = (1 << (fls(low_totalram) - 1));
		low_totalram += low_totalram - 1;
		mask = low_totalram;
	} else {
		high_totalram = (1 << (fls(high_totalram) - 1));
		high_totalram += high_totalram - 1;
		mask = (((u64)high_totalram) << 32) + 0xffffffff;
	}
	return mask;
}
EXPORT_SYMBOL_GPL(dma_get_required_mask);
#endif

static __initdata LIST_HEAD(early_platform_driver_list);
static __initdata LIST_HEAD(early_platform_device_list);

/**
 * early_platform_driver_register - register early platform driver
 * @epdrv: early_platform driver structure
 * @buf: string passed from early_param()
 *
 * Helper function for early_platform_init() / early_platform_init_buffer()
 */
int __init early_platform_driver_register(struct early_platform_driver *epdrv,
					  char *buf)
{
	char *tmp;
	int n;

	/* Simply add the driver to the end of the global list.
	 * Drivers will by default be put on the list in compiled-in order.
	 */
	if (!epdrv->list.next) {
		INIT_LIST_HEAD(&epdrv->list);
		list_add_tail(&epdrv->list, &early_platform_driver_list);
	}

	/* If the user has specified device then make sure the driver
	 * gets prioritized. The driver of the last device specified on
	 * command line will be put first on the list.
	 */
	n = strlen(epdrv->pdrv->driver.name);
	if (buf && !strncmp(buf, epdrv->pdrv->driver.name, n)) {
		list_move(&epdrv->list, &early_platform_driver_list);

		/* Allow passing parameters after device name */
		if (buf[n] == '\0' || buf[n] == ',')
			epdrv->requested_id = -1;
		else {
			epdrv->requested_id = simple_strtoul(&buf[n + 1],
							     &tmp, 10);

			if (buf[n] != '.' || (tmp == &buf[n + 1])) {
				epdrv->requested_id = EARLY_PLATFORM_ID_ERROR;
				n = 0;
			} else
				n += strcspn(&buf[n + 1], ",") + 1;
		}

		if (buf[n] == ',')
			n++;

		if (epdrv->bufsize) {
			memcpy(epdrv->buffer, &buf[n],
			       min_t(int, epdrv->bufsize, strlen(&buf[n]) + 1));
			epdrv->buffer[epdrv->bufsize - 1] = '\0';
		}
	}

	return 0;
}

/**
 * early_platform_add_devices - adds a number of early platform devices
 * @devs: array of early platform devices to add
 * @num: number of early platform devices in array
 *
 * Used by early architecture code to register early platform devices and
 * their platform data.
 */
void __init early_platform_add_devices(struct platform_device **devs, int num)
{
	struct device *dev;
	int i;

	/* simply add the devices to list */
	for (i = 0; i < num; i++) {
		dev = &devs[i]->dev;

		if (!dev->devres_head.next) {
			INIT_LIST_HEAD(&dev->devres_head);
			list_add_tail(&dev->devres_head,
				      &early_platform_device_list);
		}
	}
}

/**
 * early_platform_driver_register_all - register early platform drivers
 * @class_str: string to identify early platform driver class
 *
 * Used by architecture code to register all early platform drivers
 * for a certain class. If omitted then only early platform drivers
 * with matching kernel command line class parameters will be registered.
 */
void __init early_platform_driver_register_all(char *class_str)
{
	/* The "class_str" parameter may or may not be present on the kernel
	 * command line. If it is present then there may be more than one
	 * matching parameter.
	 *
	 * Since we register our early platform drivers using early_param()
	 * we need to make sure that they also get registered in the case
	 * when the parameter is missing from the kernel command line.
	 *
	 * We use parse_early_options() to make sure the early_param() gets
	 * called at least once. The early_param() may be called more than
	 * once since the name of the preferred device may be specified on
	 * the kernel command line. early_platform_driver_register() handles
	 * this case for us.
	 */
	parse_early_options(class_str);
}

/**
 * early_platform_match - find early platform device matching driver
 * @epdrv: early platform driver structure
 * @id: id to match against
 */
static  __init struct platform_device *
early_platform_match(struct early_platform_driver *epdrv, int id)
{
	struct platform_device *pd;

	list_for_each_entry(pd, &early_platform_device_list, dev.devres_head)
		if (platform_match(&pd->dev, &epdrv->pdrv->driver))
			if (pd->id == id)
				return pd;

	return NULL;
}

/**
 * early_platform_left - check if early platform driver has matching devices
 * @epdrv: early platform driver structure
 * @id: return true if id or above exists
 */
static  __init int early_platform_left(struct early_platform_driver *epdrv,
				       int id)
{
	struct platform_device *pd;

	list_for_each_entry(pd, &early_platform_device_list, dev.devres_head)
		if (platform_match(&pd->dev, &epdrv->pdrv->driver))
			if (pd->id >= id)
				return 1;

	return 0;
}

/**
 * early_platform_driver_probe_id - probe drivers matching class_str and id
 * @class_str: string to identify early platform driver class
 * @id: id to match against
 * @nr_probe: number of platform devices to successfully probe before exiting
 */
static int __init early_platform_driver_probe_id(char *class_str,
						 int id,
						 int nr_probe)
{
	struct early_platform_driver *epdrv;
	struct platform_device *match;
	int match_id;
	int n = 0;
	int left = 0;

	list_for_each_entry(epdrv, &early_platform_driver_list, list) {
		/* only use drivers matching our class_str */
		if (strcmp(class_str, epdrv->class_str))
			continue;

		if (id == -2) {
			match_id = epdrv->requested_id;
			left = 1;

		} else {
			match_id = id;
			left += early_platform_left(epdrv, id);

			/* skip requested id */
			switch (epdrv->requested_id) {
			case EARLY_PLATFORM_ID_ERROR:
			case EARLY_PLATFORM_ID_UNSET:
				break;
			default:
				if (epdrv->requested_id == id)
					match_id = EARLY_PLATFORM_ID_UNSET;
			}
		}

		switch (match_id) {
		case EARLY_PLATFORM_ID_ERROR:
			pr_warning("%s: unable to parse %s parameter\n",
				   class_str, epdrv->pdrv->driver.name);
			/* fall-through */
		case EARLY_PLATFORM_ID_UNSET:
			match = NULL;
			break;
		default:
			match = early_platform_match(epdrv, match_id);
		}

		if (match) {
			/*
			 * Set up a sensible init_name to enable
			 * dev_name() and others to be used before the
			 * rest of the driver core is initialized.
			 */
			if (!match->dev.init_name && slab_is_available()) {
				if (match->id != -1)
					match->dev.init_name =
						kasprintf(GFP_KERNEL, "%s.%d",
							  match->name,
							  match->id);
				else
					match->dev.init_name =
						kasprintf(GFP_KERNEL, "%s",
							  match->name);

				if (!match->dev.init_name)
					return -ENOMEM;
			}

			if (epdrv->pdrv->probe(match))
				pr_warning("%s: unable to probe %s early.\n",
					   class_str, match->name);
			else
				n++;
		}

		if (n >= nr_probe)
			break;
	}

	if (left)
		return n;
	else
		return -ENODEV;
}

/**
 * early_platform_driver_probe - probe a class of registered drivers
 * @class_str: string to identify early platform driver class
 * @nr_probe: number of platform devices to successfully probe before exiting
 * @user_only: only probe user specified early platform devices
 *
 * Used by architecture code to probe registered early platform drivers
 * within a certain class. For probe to happen a registered early platform
 * device matching a registered early platform driver is needed.
 */
int __init early_platform_driver_probe(char *class_str,
				       int nr_probe,
				       int user_only)
{
	int k, n, i;

	n = 0;
	for (i = -2; n < nr_probe; i++) {
		k = early_platform_driver_probe_id(class_str, i, nr_probe - n);

		if (k < 0)
			break;

		n += k;

		if (user_only)
			break;
	}

	return n;
}

/**
 * early_platform_cleanup - clean up early platform code
 */
void __init early_platform_cleanup(void)
{
	struct platform_device *pd, *pd2;

	/* clean up the devres list used to chain devices */
	list_for_each_entry_safe(pd, pd2, &early_platform_device_list,
				 dev.devres_head) {
		list_del(&pd->dev.devres_head);
		memset(&pd->dev.devres_head, 0, sizeof(pd->dev.devres_head));
	}
}

