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
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/amba/bus.h>
#include <linux/sizes.h>

#include <asm/irq.h>

#define to_amba_driver(d)	container_of(d, struct amba_driver, drv)

static const struct amba_id *
amba_lookup(const struct amba_id *table, struct amba_device *dev)
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

static int amba_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct amba_device *pcdev = to_amba_device(dev);
	int retval = 0;

	retval = add_uevent_var(env, "AMBA_ID=%08x", pcdev->periphid);
	if (retval)
		return retval;

	retval = add_uevent_var(env, "MODALIAS=amba:d%08X", pcdev->periphid);
	return retval;
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

#ifdef CONFIG_PM_SLEEP

static int amba_legacy_suspend(struct device *dev, pm_message_t mesg)
{
	struct amba_driver *adrv = to_amba_driver(dev->driver);
	struct amba_device *adev = to_amba_device(dev);
	int ret = 0;

	if (dev->driver && adrv->suspend)
		ret = adrv->suspend(adev, mesg);

	return ret;
}

static int amba_legacy_resume(struct device *dev)
{
	struct amba_driver *adrv = to_amba_driver(dev->driver);
	struct amba_device *adev = to_amba_device(dev);
	int ret = 0;

	if (dev->driver && adrv->resume)
		ret = adrv->resume(adev);

	return ret;
}

#endif /* CONFIG_PM_SLEEP */

#ifdef CONFIG_SUSPEND

static int amba_pm_suspend(struct device *dev)
{
	struct device_driver *drv = dev->driver;
	int ret = 0;

	if (!drv)
		return 0;

	if (drv->pm) {
		if (drv->pm->suspend)
			ret = drv->pm->suspend(dev);
	} else {
		ret = amba_legacy_suspend(dev, PMSG_SUSPEND);
	}

	return ret;
}

static int amba_pm_resume(struct device *dev)
{
	struct device_driver *drv = dev->driver;
	int ret = 0;

	if (!drv)
		return 0;

	if (drv->pm) {
		if (drv->pm->resume)
			ret = drv->pm->resume(dev);
	} else {
		ret = amba_legacy_resume(dev);
	}

	return ret;
}

#else /* !CONFIG_SUSPEND */

#define amba_pm_suspend		NULL
#define amba_pm_resume		NULL

#endif /* !CONFIG_SUSPEND */

#ifdef CONFIG_HIBERNATE_CALLBACKS

static int amba_pm_freeze(struct device *dev)
{
	struct device_driver *drv = dev->driver;
	int ret = 0;

	if (!drv)
		return 0;

	if (drv->pm) {
		if (drv->pm->freeze)
			ret = drv->pm->freeze(dev);
	} else {
		ret = amba_legacy_suspend(dev, PMSG_FREEZE);
	}

	return ret;
}

static int amba_pm_thaw(struct device *dev)
{
	struct device_driver *drv = dev->driver;
	int ret = 0;

	if (!drv)
		return 0;

	if (drv->pm) {
		if (drv->pm->thaw)
			ret = drv->pm->thaw(dev);
	} else {
		ret = amba_legacy_resume(dev);
	}

	return ret;
}

static int amba_pm_poweroff(struct device *dev)
{
	struct device_driver *drv = dev->driver;
	int ret = 0;

	if (!drv)
		return 0;

	if (drv->pm) {
		if (drv->pm->poweroff)
			ret = drv->pm->poweroff(dev);
	} else {
		ret = amba_legacy_suspend(dev, PMSG_HIBERNATE);
	}

	return ret;
}

static int amba_pm_restore(struct device *dev)
{
	struct device_driver *drv = dev->driver;
	int ret = 0;

	if (!drv)
		return 0;

	if (drv->pm) {
		if (drv->pm->restore)
			ret = drv->pm->restore(dev);
	} else {
		ret = amba_legacy_resume(dev);
	}

	return ret;
}

#else /* !CONFIG_HIBERNATE_CALLBACKS */

#define amba_pm_freeze		NULL
#define amba_pm_thaw		NULL
#define amba_pm_poweroff		NULL
#define amba_pm_restore		NULL

#endif /* !CONFIG_HIBERNATE_CALLBACKS */

#ifdef CONFIG_PM_RUNTIME
/*
 * Hooks to provide runtime PM of the pclk (bus clock).  It is safe to
 * enable/disable the bus clock at runtime PM suspend/resume as this
 * does not result in loss of context.
 */
static int amba_pm_runtime_suspend(struct device *dev)
{
	struct amba_device *pcdev = to_amba_device(dev);
	int ret = pm_generic_runtime_suspend(dev);

	if (ret == 0 && dev->driver)
		clk_disable(pcdev->pclk);

	return ret;
}

static int amba_pm_runtime_resume(struct device *dev)
{
	struct amba_device *pcdev = to_amba_device(dev);
	int ret;

	if (dev->driver) {
		ret = clk_enable(pcdev->pclk);
		/* Failure is probably fatal to the system, but... */
		if (ret)
			return ret;
	}

	return pm_generic_runtime_resume(dev);
}
#endif

#ifdef CONFIG_PM

static const struct dev_pm_ops amba_pm = {
	.suspend	= amba_pm_suspend,
	.resume		= amba_pm_resume,
	.freeze		= amba_pm_freeze,
	.thaw		= amba_pm_thaw,
	.poweroff	= amba_pm_poweroff,
	.restore	= amba_pm_restore,
	SET_RUNTIME_PM_OPS(
		amba_pm_runtime_suspend,
		amba_pm_runtime_resume,
		NULL
	)
};

#define AMBA_PM (&amba_pm)

#else /* !CONFIG_PM */

#define AMBA_PM	NULL

#endif /* !CONFIG_PM */

/*
 * Primecells are part of the Advanced Microcontroller Bus Architecture,
 * so we call the bus "amba".
 */
struct bus_type amba_bustype = {
	.name		= "amba",
	.dev_attrs	= amba_dev_attrs,
	.match		= amba_match,
	.uevent		= amba_uevent,
	.pm		= AMBA_PM,
};

static int __init amba_init(void)
{
	return bus_register(&amba_bustype);
}

postcore_initcall(amba_init);

static int amba_get_enable_pclk(struct amba_device *pcdev)
{
	struct clk *pclk = clk_get(&pcdev->dev, "apb_pclk");
	int ret;

	pcdev->pclk = pclk;

	if (IS_ERR(pclk))
		return PTR_ERR(pclk);

	ret = clk_prepare(pclk);
	if (ret) {
		clk_put(pclk);
		return ret;
	}

	ret = clk_enable(pclk);
	if (ret) {
		clk_unprepare(pclk);
		clk_put(pclk);
	}

	return ret;
}

static void amba_put_disable_pclk(struct amba_device *pcdev)
{
	struct clk *pclk = pcdev->pclk;

	clk_disable(pclk);
	clk_unprepare(pclk);
	clk_put(pclk);
}

/*
 * These are the device model conversion veneers; they convert the
 * device model structures to our more specific structures.
 */
static int amba_probe(struct device *dev)
{
	struct amba_device *pcdev = to_amba_device(dev);
	struct amba_driver *pcdrv = to_amba_driver(dev->driver);
	const struct amba_id *id = amba_lookup(pcdrv->id_table, pcdev);
	int ret;

	do {
		ret = amba_get_enable_pclk(pcdev);
		if (ret)
			break;

		pm_runtime_get_noresume(dev);
		pm_runtime_set_active(dev);
		pm_runtime_enable(dev);

		ret = pcdrv->probe(pcdev, id);
		if (ret == 0)
			break;

		pm_runtime_disable(dev);
		pm_runtime_set_suspended(dev);
		pm_runtime_put_noidle(dev);

		amba_put_disable_pclk(pcdev);
	} while (0);

	return ret;
}

static int amba_remove(struct device *dev)
{
	struct amba_device *pcdev = to_amba_device(dev);
	struct amba_driver *drv = to_amba_driver(dev->driver);
	int ret;

	pm_runtime_get_sync(dev);
	ret = drv->remove(pcdev);
	pm_runtime_put_noidle(dev);

	/* Undo the runtime PM settings in amba_probe() */
	pm_runtime_disable(dev);
	pm_runtime_set_suspended(dev);
	pm_runtime_put_noidle(dev);

	amba_put_disable_pclk(pcdev);

	return ret;
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
 *	amba_device_add - add a previously allocated AMBA device structure
 *	@dev: AMBA device allocated by amba_device_alloc
 *	@parent: resource parent for this devices resources
 *
 *	Claim the resource, and read the device cell ID if not already
 *	initialized.  Register the AMBA device with the Linux device
 *	manager.
 */
int amba_device_add(struct amba_device *dev, struct resource *parent)
{
	u32 size;
	void __iomem *tmp;
	int i, ret;

	WARN_ON(dev->irq[0] == (unsigned int)-1);
	WARN_ON(dev->irq[1] == (unsigned int)-1);

	ret = request_resource(parent, &dev->res);
	if (ret)
		goto err_out;

	/* Hard-coded primecell ID instead of plug-n-play */
	if (dev->periphid != 0)
		goto skip_probe;

	/*
	 * Dynamically calculate the size of the resource
	 * and use this for iomap
	 */
	size = resource_size(&dev->res);
	tmp = ioremap(dev->res.start, size);
	if (!tmp) {
		ret = -ENOMEM;
		goto err_release;
	}

	ret = amba_get_enable_pclk(dev);
	if (ret == 0) {
		u32 pid, cid;

		/*
		 * Read pid and cid based on size of resource
		 * they are located at end of region
		 */
		for (pid = 0, i = 0; i < 4; i++)
			pid |= (readl(tmp + size - 0x20 + 4 * i) & 255) <<
				(i * 8);
		for (cid = 0, i = 0; i < 4; i++)
			cid |= (readl(tmp + size - 0x10 + 4 * i) & 255) <<
				(i * 8);

		amba_put_disable_pclk(dev);

		if (cid == AMBA_CID)
			dev->periphid = pid;

		if (!dev->periphid)
			ret = -ENODEV;
	}

	iounmap(tmp);

	if (ret)
		goto err_release;

 skip_probe:
	ret = device_add(&dev->dev);
	if (ret)
		goto err_release;

	if (dev->irq[0])
		ret = device_create_file(&dev->dev, &dev_attr_irq0);
	if (ret == 0 && dev->irq[1])
		ret = device_create_file(&dev->dev, &dev_attr_irq1);
	if (ret == 0)
		return ret;

	device_unregister(&dev->dev);

 err_release:
	release_resource(&dev->res);
 err_out:
	return ret;
}
EXPORT_SYMBOL_GPL(amba_device_add);

static struct amba_device *
amba_aphb_device_add(struct device *parent, const char *name,
		     resource_size_t base, size_t size, int irq1, int irq2,
		     void *pdata, unsigned int periphid, u64 dma_mask,
		     struct resource *resbase)
{
	struct amba_device *dev;
	int ret;

	dev = amba_device_alloc(name, base, size);
	if (!dev)
		return ERR_PTR(-ENOMEM);

	dev->dev.coherent_dma_mask = dma_mask;
	dev->irq[0] = irq1;
	dev->irq[1] = irq2;
	dev->periphid = periphid;
	dev->dev.platform_data = pdata;
	dev->dev.parent = parent;

	ret = amba_device_add(dev, resbase);
	if (ret) {
		amba_device_put(dev);
		return ERR_PTR(ret);
	}

	return dev;
}

struct amba_device *
amba_apb_device_add(struct device *parent, const char *name,
		    resource_size_t base, size_t size, int irq1, int irq2,
		    void *pdata, unsigned int periphid)
{
	return amba_aphb_device_add(parent, name, base, size, irq1, irq2, pdata,
				    periphid, 0, &iomem_resource);
}
EXPORT_SYMBOL_GPL(amba_apb_device_add);

struct amba_device *
amba_ahb_device_add(struct device *parent, const char *name,
		    resource_size_t base, size_t size, int irq1, int irq2,
		    void *pdata, unsigned int periphid)
{
	return amba_aphb_device_add(parent, name, base, size, irq1, irq2, pdata,
				    periphid, ~0ULL, &iomem_resource);
}
EXPORT_SYMBOL_GPL(amba_ahb_device_add);

struct amba_device *
amba_apb_device_add_res(struct device *parent, const char *name,
			resource_size_t base, size_t size, int irq1,
			int irq2, void *pdata, unsigned int periphid,
			struct resource *resbase)
{
	return amba_aphb_device_add(parent, name, base, size, irq1, irq2, pdata,
				    periphid, 0, resbase);
}
EXPORT_SYMBOL_GPL(amba_apb_device_add_res);

struct amba_device *
amba_ahb_device_add_res(struct device *parent, const char *name,
			resource_size_t base, size_t size, int irq1,
			int irq2, void *pdata, unsigned int periphid,
			struct resource *resbase)
{
	return amba_aphb_device_add(parent, name, base, size, irq1, irq2, pdata,
				    periphid, ~0ULL, resbase);
}
EXPORT_SYMBOL_GPL(amba_ahb_device_add_res);


static void amba_device_initialize(struct amba_device *dev, const char *name)
{
	device_initialize(&dev->dev);
	if (name)
		dev_set_name(&dev->dev, "%s", name);
	dev->dev.release = amba_device_release;
	dev->dev.bus = &amba_bustype;
	dev->dev.dma_mask = &dev->dev.coherent_dma_mask;
	dev->res.name = dev_name(&dev->dev);
}

/**
 *	amba_device_alloc - allocate an AMBA device
 *	@name: sysfs name of the AMBA device
 *	@base: base of AMBA device
 *	@size: size of AMBA device
 *
 *	Allocate and initialize an AMBA device structure.  Returns %NULL
 *	on failure.
 */
struct amba_device *amba_device_alloc(const char *name, resource_size_t base,
	size_t size)
{
	struct amba_device *dev;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (dev) {
		amba_device_initialize(dev, name);
		dev->res.start = base;
		dev->res.end = base + size - 1;
		dev->res.flags = IORESOURCE_MEM;
	}

	return dev;
}
EXPORT_SYMBOL_GPL(amba_device_alloc);

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
	amba_device_initialize(dev, dev->dev.init_name);
	dev->dev.init_name = NULL;

	return amba_device_add(dev, parent);
}

/**
 *	amba_device_put - put an AMBA device
 *	@dev: AMBA device to put
 */
void amba_device_put(struct amba_device *dev)
{
	put_device(&dev->dev);
}
EXPORT_SYMBOL_GPL(amba_device_put);

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
		r &= strcmp(dev_name(dev), d->busid) == 0;

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
	u32 size;

	if (!name)
		name = dev->dev.driver->name;

	size = resource_size(&dev->res);

	if (!request_mem_region(dev->res.start, size, name))
		ret = -EBUSY;

	return ret;
}

/**
 *	amba_release_regions - release mem regions associated with device
 *	@dev: amba_device structure for device
 *
 *	Release regions claimed by a successful call to amba_request_regions.
 */
void amba_release_regions(struct amba_device *dev)
{
	u32 size;

	size = resource_size(&dev->res);
	release_mem_region(dev->res.start, size);
}

EXPORT_SYMBOL(amba_driver_register);
EXPORT_SYMBOL(amba_driver_unregister);
EXPORT_SYMBOL(amba_device_register);
EXPORT_SYMBOL(amba_device_unregister);
EXPORT_SYMBOL(amba_find_device);
EXPORT_SYMBOL(amba_request_regions);
EXPORT_SYMBOL(amba_release_regions);
