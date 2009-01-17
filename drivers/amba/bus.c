/*
 *  linux/arch/arm/common/amba.c
 *
 *  Copyright (C) 2003 Deep Blue Solutions Ltd, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/amba/bus.h>

#include <asm/irq.h>
#include <asm/sizes.h>

#define to_amba_device(d)	container_of(d, struct amba_device, dev)
#define to_amba_driver(d)	container_of(d, struct amba_driver, drv)

static struct amba_id *
amba_lookup(struct amba_id *table, struct amba_device *dev)
{
	int ret = 0;

	while (table->mask) {
		ret = (dev->periphid & table->mask) == table->id;
		if (ret)
			break;
		table++;
	}

	return ret ? table : NULL;
}

static int amba_match(struct device *dev, struct device_driver *drv)
{
	struct amba_device *pcdev = to_amba_device(dev);
	struct amba_driver *pcdrv = to_amba_driver(drv);

	return amba_lookup(pcdrv->id_table, pcdev) != NULL;
}

#ifdef CONFIG_HOTPLUG
static int amba_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct amba_device *pcdev = to_amba_device(dev);
	int retval = 0;

	retval = add_uevent_var(env, "AMBA_ID=%08x", pcdev->periphid);
	return retval;
}
#else
#define amba_uevent NULL
#endif

static int amba_suspend(struct device *dev, pm_message_t state)
{
	struct amba_driver *drv = to_amba_driver(dev->driver);
	int ret = 0;

	if (dev->driver && drv->suspend)
		ret = drv->suspend(to_amba_device(dev), state);
	return ret;
}

static int amba_resume(struct device *dev)
{
	struct amba_driver *drv = to_amba_driver(dev->driver);
	int ret = 0;

	if (dev->driver && drv->resume)
		ret = drv->resume(to_amba_device(dev));
	return ret;
}

#define amba_attr_func(name,fmt,arg...)					\
static ssize_t name##_show(struct device *_dev,				\
			   struct device_attribute *attr, char *buf)	\
{									\
	struct amba_device *dev = to_amba_device(_dev);			\
	return sprintf(buf, fmt, arg);					\
}

#define amba_attr(name,fmt,arg...)	\
amba_attr_func(name,fmt,arg)		\
static DEVICE_ATTR(name, S_IRUGO, name##_show, NULL)

amba_attr_func(id, "%08x\n", dev->periphid);
amba_attr(irq0, "%u\n", dev->irq[0]);
amba_attr(irq1, "%u\n", dev->irq[1]);
amba_attr_func(resource, "\t%016llx\t%016llx\t%016lx\n",
	 (unsigned long long)dev->res.start, (unsigned long long)dev->res.end,
	 dev->res.flags);

static struct device_attribute amba_dev_attrs[] = {
	__ATTR_RO(id),
	__ATTR_RO(resource),
	__ATTR_NULL,
};

/*
 * Primecells are part of the Advanced Microcontroller Bus Architecture,
 * so we call the bus "amba".
 */
static struct bus_type amba_bustype = {
	.name		= "amba",
	.dev_attrs	= amba_dev_attrs,
	.match		= amba_match,
	.uevent		= amba_uevent,
	.suspend	= amba_suspend,
	.resume		= amba_resume,
};

static int __init amba_init(void)
{
	return bus_register(&amba_bustype);
}

postcore_initcall(amba_init);

/*
 * These are the device model conversion veneers; they convert the
 * device model structures to our more specific structures.
 */
static int amba_probe(struct device *dev)
{
	struct amba_device *pcdev = to_amba_device(dev);
	struct amba_driver *pcdrv = to_amba_driver(dev->driver);
	struct amba_id *id;

	id = amba_lookup(pcdrv->id_table, pcdev);

	return pcdrv->probe(pcdev, id);
}

static int amba_remove(struct device *dev)
{
	struct amba_driver *drv = to_amba_driver(dev->driver);
	return drv->remove(to_amba_device(dev));
}

static void amba_shutdown(struct device *dev)
{
	struct amba_driver *drv = to_amba_driver(dev->driver);
	drv->shutdown(to_amba_device(dev));
}

/**
 *	amba_driver_register - register an AMBA device driver
 *	@drv: amba device driver structure
 *
 *	Register an AMBA device driver with the Linux device model
 *	core.  If devices pre-exist, the drivers probe function will
 *	be called.
 */
int amba_driver_register(struct amba_driver *drv)
{
	drv->drv.bus = &amba_bustype;

#define SETFN(fn)	if (drv->fn) drv->drv.fn = amba_##fn
	SETFN(probe);
	SETFN(remove);
	SETFN(shutdown);

	return driver_register(&drv->drv);
}

/**
 *	amba_driver_unregister - remove an AMBA device driver
 *	@drv: AMBA device driver structure to remove
 *
 *	Unregister an AMBA device driver from the Linux device
 *	model.  The device model will call the drivers remove function
 *	for each device the device driver is currently handling.
 */
void amba_driver_unregister(struct amba_driver *drv)
{
	driver_unregister(&drv->drv);
}


static void amba_device_release(struct device *dev)
{
	struct amba_device *d = to_amba_device(dev);

	if (d->res.parent)
		release_resource(&d->res);
	kfree(d);
}

/**
 *	amba_device_register - register an AMBA device
 *	@dev: AMBA device to register
 *	@parent: parent memory resource
 *
 *	Setup the AMBA device, reading the cell ID if present.
 *	Claim the resource, and register the AMBA device with
 *	the Linux device manager.
 */
int amba_device_register(struct amba_device *dev, struct resource *parent)
{
	u32 pid, cid;
	void __iomem *tmp;
	int i, ret;

	dev->dev.release = amba_device_release;
	dev->dev.bus = &amba_bustype;
	dev->dev.dma_mask = &dev->dma_mask;
	dev->res.name = dev->dev.bus_id;

	if (!dev->dev.coherent_dma_mask && dev->dma_mask)
		dev_warn(&dev->dev, "coherent dma mask is unset\n");

	ret = request_resource(parent, &dev->res);
	if (ret)
		goto err_out;

	tmp = ioremap(dev->res.start, SZ_4K);
	if (!tmp) {
		ret = -ENOMEM;
		goto err_release;
	}

	for (pid = 0, i = 0; i < 4; i++)
		pid |= (readl(tmp + 0xfe0 + 4 * i) & 255) << (i * 8);
	for (cid = 0, i = 0; i < 4; i++)
		cid |= (readl(tmp + 0xff0 + 4 * i) & 255) << (i * 8);

	iounmap(tmp);

	if (cid == 0xb105f00d)
		dev->periphid = pid;

	if (!dev->periphid) {
		ret = -ENODEV;
		goto err_release;
	}

	ret = device_register(&dev->dev);
	if (ret)
		goto err_release;

	if (dev->irq[0] != NO_IRQ)
		ret = device_create_file(&dev->dev, &dev_attr_irq0);
	if (ret == 0 && dev->irq[1] != NO_IRQ)
		ret = device_create_file(&dev->dev, &dev_attr_irq1);
	if (ret == 0)
		return ret;

	device_unregister(&dev->dev);

 err_release:
	release_resource(&dev->res);
 err_out:
	return ret;
}

/**
 *	amba_device_unregister - unregister an AMBA device
 *	@dev: AMBA device to remove
 *
 *	Remove the specified AMBA device from the Linux device
 *	manager.  All files associated with this object will be
 *	destroyed, and device drivers notified that the device has
 *	been removed.  The AMBA device's resources including
 *	the amba_device structure will be freed once all
 *	references to it have been dropped.
 */
void amba_device_unregister(struct amba_device *dev)
{
	device_unregister(&dev->dev);
}


struct find_data {
	struct amba_device *dev;
	struct device *parent;
	const char *busid;
	unsigned int id;
	unsigned int mask;
};

static int amba_find_match(struct device *dev, void *data)
{
	struct find_data *d = data;
	struct amba_device *pcdev = to_amba_device(dev);
	int r;

	r = (pcdev->periphid & d->mask) == d->id;
	if (d->parent)
		r &= d->parent == dev->parent;
	if (d->busid)
		r &= strcmp(dev->bus_id, d->busid) == 0;

	if (r) {
		get_device(dev);
		d->dev = pcdev;
	}

	return r;
}

/**
 *	amba_find_device - locate an AMBA device given a bus id
 *	@busid: bus id for device (or NULL)
 *	@parent: parent device (or NULL)
 *	@id: peripheral ID (or 0)
 *	@mask: peripheral ID mask (or 0)
 *
 *	Return the AMBA device corresponding to the supplied parameters.
 *	If no device matches, returns NULL.
 *
 *	NOTE: When a valid device is found, its refcount is
 *	incremented, and must be decremented before the returned
 *	reference.
 */
struct amba_device *
amba_find_device(const char *busid, struct device *parent, unsigned int id,
		 unsigned int mask)
{
	struct find_data data;

	data.dev = NULL;
	data.parent = parent;
	data.busid = busid;
	data.id = id;
	data.mask = mask;

	bus_for_each_dev(&amba_bustype, NULL, &data, amba_find_match);

	return data.dev;
}

/**
 *	amba_request_regions - request all mem regions associated with device
 *	@dev: amba_device structure for device
 *	@name: name, or NULL to use driver name
 */
int amba_request_regions(struct amba_device *dev, const char *name)
{
	int ret = 0;

	if (!name)
		name = dev->dev.driver->name;

	if (!request_mem_region(dev->res.start, SZ_4K, name))
		ret = -EBUSY;

	return ret;
}

/**
 *	amba_release_regions - release mem regions assoicated with device
 *	@dev: amba_device structure for device
 *
 *	Release regions claimed by a successful call to amba_request_regions.
 */
void amba_release_regions(struct amba_device *dev)
{
	release_mem_region(dev->res.start, SZ_4K);
}

EXPORT_SYMBOL(amba_driver_register);
EXPORT_SYMBOL(amba_driver_unregister);
EXPORT_SYMBOL(amba_device_register);
EXPORT_SYMBOL(amba_device_unregister);
EXPORT_SYMBOL(amba_find_device);
EXPORT_SYMBOL(amba_request_regions);
EXPORT_SYMBOL(amba_release_regions);
