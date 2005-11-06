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

struct device platform_bus = {
	.bus_id		= "platform",
};

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

/**
 *	platform_get_irq - get an IRQ for a device
 *	@dev: platform device
 *	@num: IRQ number index
 */
int platform_get_irq(struct platform_device *dev, unsigned int num)
{
	struct resource *r = platform_get_resource(dev, IORESOURCE_IRQ, num);

	return r ? r->start : 0;
}

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

/**
 *	platform_get_irq - get an IRQ for a device
 *	@dev: platform device
 *	@name: IRQ name
 */
int platform_get_irq_byname(struct platform_device *dev, char *name)
{
	struct resource *r = platform_get_resource_byname(dev, IORESOURCE_IRQ, name);

	return r ? r->start : 0;
}

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

/**
 *	platform_device_register - add a platform-level device
 *	@pdev:	platform device we're adding
 *
 */
int platform_device_register(struct platform_device * pdev)
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

		if (p && request_resource(p, r)) {
			printk(KERN_ERR
			       "%s: failed to claim resource %d\n",
			       pdev->dev.bus_id, i);
			ret = -EBUSY;
			goto failed;
		}
	}

	pr_debug("Registering platform device '%s'. Parent at %s\n",
		 pdev->dev.bus_id, pdev->dev.parent->bus_id);

	ret = device_register(&pdev->dev);
	if (ret == 0)
		return ret;

 failed:
	while (--i >= 0)
		if (pdev->resource[i].flags & (IORESOURCE_MEM|IORESOURCE_IO))
			release_resource(&pdev->resource[i]);
	return ret;
}

/**
 *	platform_device_unregister - remove a platform-level device
 *	@pdev:	platform device we're removing
 *
 *	Note that this function will also release all memory- and port-based
 *	resources owned by the device (@dev->resource).
 */
void platform_device_unregister(struct platform_device * pdev)
{
	int i;

	if (pdev) {
		for (i = 0; i < pdev->num_resources; i++) {
			struct resource *r = &pdev->resource[i];
			if (r->flags & (IORESOURCE_MEM|IORESOURCE_IO))
				release_resource(r);
		}

		device_unregister(&pdev->dev);
	}
}

struct platform_object {
        struct platform_device pdev;
        struct resource resources[0];
};

static void platform_device_release_simple(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);

	kfree(container_of(pdev, struct platform_object, pdev));
}

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
	struct platform_object *pobj;
	int retval;

	pobj = kzalloc(sizeof(*pobj) + sizeof(struct resource) * num, GFP_KERNEL);
	if (!pobj) {
		retval = -ENOMEM;
		goto error;
	}

	pobj->pdev.name = name;
	pobj->pdev.id = id;
	pobj->pdev.dev.release = platform_device_release_simple;

	if (num) {
		memcpy(pobj->resources, res, sizeof(struct resource) * num);
		pobj->pdev.resource = pobj->resources;
		pobj->pdev.num_resources = num;
	}

	retval = platform_device_register(&pobj->pdev);
	if (retval)
		goto error;

	return &pobj->pdev;

error:
	kfree(pobj);
	return ERR_PTR(retval);
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

static int platform_suspend(struct device * dev, pm_message_t state)
{
	int ret = 0;

	if (dev->driver && dev->driver->suspend)
		ret = dev->driver->suspend(dev, state);

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
	.match		= platform_match,
	.suspend	= platform_suspend,
	.resume		= platform_resume,
};

int __init platform_bus_init(void)
{
	device_register(&platform_bus);
	return bus_register(&platform_bus_type);
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

EXPORT_SYMBOL_GPL(platform_bus);
EXPORT_SYMBOL_GPL(platform_bus_type);
EXPORT_SYMBOL_GPL(platform_add_devices);
EXPORT_SYMBOL_GPL(platform_device_register);
EXPORT_SYMBOL_GPL(platform_device_register_simple);
EXPORT_SYMBOL_GPL(platform_device_unregister);
EXPORT_SYMBOL_GPL(platform_get_irq);
EXPORT_SYMBOL_GPL(platform_get_resource);
EXPORT_SYMBOL_GPL(platform_get_irq_byname);
EXPORT_SYMBOL_GPL(platform_get_resource_byname);
