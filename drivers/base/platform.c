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

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/bootmem.h>
#include <linux/err.h>
#include <linux/slab.h>

#include "base.h"

#define to_platform_driver(drv)	(container_of((drv), struct platform_driver, driver))

struct device platform_bus = {
	.bus_id		= "platform",
};
EXPORT_SYMBOL_GPL(platform_bus);

/**
 *	platform_get_resource - get a resource for a device
 *	@dev: platform device
 *	@type: resource type
 *	@num: resource index
 */
struct resource *
platform_get_resource(struct platform_device *dev, unsigned int type,
		      unsigned int num)
{
	int i;

	for (i = 0; i < dev->num_resources; i++) {
		struct resource *r = &dev->resource[i];

		if ((r->flags & (IORESOURCE_IO|IORESOURCE_MEM|
				 IORESOURCE_IRQ|IORESOURCE_DMA))
		    == type)
			if (num-- == 0)
				return r;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(platform_get_resource);

/**
 *	platform_get_irq - get an IRQ for a device
 *	@dev: platform device
 *	@num: IRQ number index
 */
int platform_get_irq(struct platform_device *dev, unsigned int num)
{
	struct resource *r = platform_get_resource(dev, IORESOURCE_IRQ, num);

	return r ? r->start : -ENXIO;
}
EXPORT_SYMBOL_GPL(platform_get_irq);

/**
 *	platform_get_resource_byname - get a resource for a device by name
 *	@dev: platform device
 *	@type: resource type
 *	@name: resource name
 */
struct resource *
platform_get_resource_byname(struct platform_device *dev, unsigned int type,
		      char *name)
{
	int i;

	for (i = 0; i < dev->num_resources; i++) {
		struct resource *r = &dev->resource[i];

		if ((r->flags & (IORESOURCE_IO|IORESOURCE_MEM|
				 IORESOURCE_IRQ|IORESOURCE_DMA)) == type)
			if (!strcmp(r->name, name))
				return r;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(platform_get_resource_byname);

/**
 *	platform_get_irq - get an IRQ for a device
 *	@dev: platform device
 *	@name: IRQ name
 */
int platform_get_irq_byname(struct platform_device *dev, char *name)
{
	struct resource *r = platform_get_resource_byname(dev, IORESOURCE_IRQ, name);

	return r ? r->start : -ENXIO;
}
EXPORT_SYMBOL_GPL(platform_get_irq_byname);

/**
 *	platform_add_devices - add a numbers of platform devices
 *	@devs: array of platform devices to add
 *	@num: number of platform devices in array
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
 *	platform_device_put
 *	@pdev:	platform device to free
 *
 *	Free all memory associated with a platform device.  This function
 *	must _only_ be externally called in error cases.  All other usage
 *	is a bug.
 */
void platform_device_put(struct platform_device *pdev)
{
	if (pdev)
		put_device(&pdev->dev);
}
EXPORT_SYMBOL_GPL(platform_device_put);

static void platform_device_release(struct device *dev)
{
	struct platform_object *pa = container_of(dev, struct platform_object, pdev.dev);

	kfree(pa->pdev.dev.platform_data);
	kfree(pa->pdev.resource);
	kfree(pa);
}

/**
 *	platform_device_alloc
 *	@name:	base name of the device we're adding
 *	@id:    instance id
 *
 *	Create a platform device object which can have other objects attached
 *	to it, and which will have attached objects freed when it is released.
 */
struct platform_device *platform_device_alloc(const char *name, unsigned int id)
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
 *	platform_device_add_resources
 *	@pdev:	platform device allocated by platform_device_alloc to add resources to
 *	@res:   set of resources that needs to be allocated for the device
 *	@num:	number of resources
 *
 *	Add a copy of the resources to the platform device.  The memory
 *	associated with the resources will be freed when the platform
 *	device is released.
 */
int platform_device_add_resources(struct platform_device *pdev, struct resource *res, unsigned int num)
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
 *	platform_device_add_data
 *	@pdev:	platform device allocated by platform_device_alloc to add resources to
 *	@data:	platform specific data for this platform device
 *	@size:	size of platform specific data
 *
 *	Add a copy of platform specific data to the platform device's platform_data
 *	pointer.  The memory associated with the platform data will be freed
 *	when the platform device is released.
 */
int platform_device_add_data(struct platform_device *pdev, const void *data, size_t size)
{
	void *d;

	d = kmalloc(size, GFP_KERNEL);
	if (d) {
		memcpy(d, data, size);
		pdev->dev.platform_data = d;
	}
	return d ? 0 : -ENOMEM;
}
EXPORT_SYMBOL_GPL(platform_device_add_data);

/**
 *	platform_device_add - add a platform device to device hierarchy
 *	@pdev:	platform device we're adding
 *
 *	This is part 2 of platform_device_register(), though may be called
 *	separately _iff_ pdev was allocated by platform_device_alloc().
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
		snprintf(pdev->dev.bus_id, BUS_ID_SIZE, "%s.%u", pdev->name, pdev->id);
	else
		strlcpy(pdev->dev.bus_id, pdev->name, BUS_ID_SIZE);

	for (i = 0; i < pdev->num_resources; i++) {
		struct resource *p, *r = &pdev->resource[i];

		if (r->name == NULL)
			r->name = pdev->dev.bus_id;

		p = r->parent;
		if (!p) {
			if (r->flags & IORESOURCE_MEM)
				p = &iomem_resource;
			else if (r->flags & IORESOURCE_IO)
				p = &ioport_resource;
		}

		if (p && insert_resource(p, r)) {
			printk(KERN_ERR
			       "%s: failed to claim resource %d\n",
			       pdev->dev.bus_id, i);
			ret = -EBUSY;
			goto failed;
		}
	}

	pr_debug("Registering platform device '%s'. Parent at %s\n",
		 pdev->dev.bus_id, pdev->dev.parent->bus_id);

	ret = device_add(&pdev->dev);
	if (ret == 0)
		return ret;

 failed:
	while (--i >= 0)
		if (pdev->resource[i].flags & (IORESOURCE_MEM|IORESOURCE_IO))
			release_resource(&pdev->resource[i]);
	return ret;
}
EXPORT_SYMBOL_GPL(platform_device_add);

/**
 *	platform_device_del - remove a platform-level device
 *	@pdev:	platform device we're removing
 *
 *	Note that this function will also release all memory- and port-based
 *	resources owned by the device (@dev->resource).
 */
void platform_device_del(struct platform_device *pdev)
{
	int i;

	if (pdev) {
		for (i = 0; i < pdev->num_resources; i++) {
			struct resource *r = &pdev->resource[i];
			if (r->flags & (IORESOURCE_MEM|IORESOURCE_IO))
				release_resource(r);
		}

		device_del(&pdev->dev);
	}
}
EXPORT_SYMBOL_GPL(platform_device_del);

/**
 *	platform_device_register - add a platform-level device
 *	@pdev:	platform device we're adding
 *
 */
int platform_device_register(struct platform_device * pdev)
{
	device_initialize(&pdev->dev);
	return platform_device_add(pdev);
}
EXPORT_SYMBOL_GPL(platform_device_register);

/**
 *	platform_device_unregister - unregister a platform-level device
 *	@pdev:	platform device we're unregistering
 *
 *	Unregistration is done in 2 steps. First we release all resources
 *	and remove it from the subsystem, then we drop reference count by
 *	calling platform_device_put().
 */
void platform_device_unregister(struct platform_device * pdev)
{
	platform_device_del(pdev);
	platform_device_put(pdev);
}
EXPORT_SYMBOL_GPL(platform_device_unregister);

/**
 *	platform_device_register_simple
 *	@name:  base name of the device we're adding
 *	@id:    instance id
 *	@res:   set of resources that needs to be allocated for the device
 *	@num:	number of resources
 *
 *	This function creates a simple platform device that requires minimal
 *	resource and memory management. Canned release function freeing
 *	memory allocated for the device allows drivers using such devices
 *	to be unloaded iwithout waiting for the last reference to the device
 *	to be dropped.
 */
struct platform_device *platform_device_register_simple(char *name, unsigned int id,
							struct resource *res, unsigned int num)
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

static int platform_drv_suspend(struct device *_dev, pm_message_t state)
{
	struct platform_driver *drv = to_platform_driver(_dev->driver);
	struct platform_device *dev = to_platform_device(_dev);

	return drv->suspend(dev, state);
}

static int platform_drv_resume(struct device *_dev)
{
	struct platform_driver *drv = to_platform_driver(_dev->driver);
	struct platform_device *dev = to_platform_device(_dev);

	return drv->resume(dev);
}

/**
 *	platform_driver_register
 *	@drv: platform driver structure
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
	if (drv->suspend)
		drv->driver.suspend = platform_drv_suspend;
	if (drv->resume)
		drv->driver.resume = platform_drv_resume;
	return driver_register(&drv->driver);
}
EXPORT_SYMBOL_GPL(platform_driver_register);

/**
 *	platform_driver_unregister
 *	@drv: platform driver structure
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

	/* temporary section violation during probe() */
	drv->probe = probe;
	retval = code = platform_driver_register(drv);

	/* Fixup that section violation, being paranoid about code scanning
	 * the list of drivers in order to probe new devices.  Check to see
	 * if the probe was successful, and make sure any forced probes of
	 * new devices fail.
	 */
	spin_lock(&platform_bus_type.klist_drivers.k_lock);
	drv->probe = NULL;
	if (code == 0 && list_empty(&drv->driver.klist_devices.k_list))
		retval = -ENODEV;
	drv->driver.probe = platform_drv_probe_fail;
	spin_unlock(&platform_bus_type.klist_drivers.k_lock);

	if (code != retval)
		platform_driver_unregister(drv);
	return retval;
}
EXPORT_SYMBOL_GPL(platform_driver_probe);

/* modalias support enables more hands-off userspace setup:
 * (a) environment variable lets new-style hotplug events work once system is
 *     fully running:  "modprobe $MODALIAS"
 * (b) sysfs attribute lets new-style coldplug recover from hotplug events
 *     mishandled before system is fully running:  "modprobe $(cat modalias)"
 */
static ssize_t
modalias_show(struct device *dev, struct device_attribute *a, char *buf)
{
	struct platform_device	*pdev = to_platform_device(dev);
	int len = snprintf(buf, PAGE_SIZE, "%s\n", pdev->name);

	return (len >= PAGE_SIZE) ? (PAGE_SIZE - 1) : len;
}

static struct device_attribute platform_dev_attrs[] = {
	__ATTR_RO(modalias),
	__ATTR_NULL,
};

static int platform_uevent(struct device *dev, char **envp, int num_envp,
		char *buffer, int buffer_size)
{
	struct platform_device	*pdev = to_platform_device(dev);

	envp[0] = buffer;
	snprintf(buffer, buffer_size, "MODALIAS=%s", pdev->name);
	return 0;
}


/**
 *	platform_match - bind platform device to platform driver.
 *	@dev:	device.
 *	@drv:	driver.
 *
 *	Platform device IDs are assumed to be encoded like this:
 *	"<name><instance>", where <name> is a short description of the
 *	type of device, like "pci" or "floppy", and <instance> is the
 *	enumerated instance of the device, like '0' or '42'.
 *	Driver IDs are simply "<name>".
 *	So, extract the <name> from the platform_device structure,
 *	and compare it against the name of the driver. Return whether
 *	they match or not.
 */

static int platform_match(struct device * dev, struct device_driver * drv)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);

	return (strncmp(pdev->name, drv->name, BUS_ID_SIZE) == 0);
}

static int platform_suspend(struct device *dev, pm_message_t mesg)
{
	int ret = 0;

	if (dev->driver && dev->driver->suspend)
		ret = dev->driver->suspend(dev, mesg);

	return ret;
}

static int platform_suspend_late(struct device *dev, pm_message_t mesg)
{
	struct platform_driver *drv = to_platform_driver(dev->driver);
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	int ret = 0;

	if (dev->driver && drv->suspend_late)
		ret = drv->suspend_late(pdev, mesg);

	return ret;
}

static int platform_resume_early(struct device *dev)
{
	struct platform_driver *drv = to_platform_driver(dev->driver);
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	int ret = 0;

	if (dev->driver && drv->resume_early)
		ret = drv->resume_early(pdev);

	return ret;
}

static int platform_resume(struct device * dev)
{
	int ret = 0;

	if (dev->driver && dev->driver->resume)
		ret = dev->driver->resume(dev);

	return ret;
}

struct bus_type platform_bus_type = {
	.name		= "platform",
	.dev_attrs	= platform_dev_attrs,
	.match		= platform_match,
	.uevent		= platform_uevent,
	.suspend	= platform_suspend,
	.suspend_late	= platform_suspend_late,
	.resume_early	= platform_resume_early,
	.resume		= platform_resume,
};
EXPORT_SYMBOL_GPL(platform_bus_type);

int __init platform_bus_init(void)
{
	int error;

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
	return mask & *dev->dma_mask;
}
EXPORT_SYMBOL_GPL(dma_get_required_mask);
#endif
