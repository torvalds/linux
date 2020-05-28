// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/arch/arm/common/amba.c
 *
 *  Copyright (C) 2003 Deep Blue Solutions Ltd, All Rights Reserved.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/pm_domain.h>
#include <linux/amba/bus.h>
#include <linux/sizes.h>
#include <linux/limits.h>
#include <linux/clk/clk-conf.h>
#include <linux/platform_device.h>
#include <linux/reset.h>

#include <asm/irq.h>

#define to_amba_driver(d)	container_of(d, struct amba_driver, drv)

/* called on periphid match and class 0x9 coresight device. */
static int
amba_cs_uci_id_match(const struct amba_id *table, struct amba_device *dev)
{
	int ret = 0;
	struct amba_cs_uci_id *uci;

	uci = table->data;

	/* no table data or zero mask - return match on periphid */
	if (!uci || (uci->devarch_mask == 0))
		return 1;

	/* test against read devtype and masked devarch value */
	ret = (dev->uci.devtype == uci->devtype) &&
		((dev->uci.devarch & uci->devarch_mask) == uci->devarch);
	return ret;
}

static const struct amba_id *
amba_lookup(const struct amba_id *table, struct amba_device *dev)
{
	while (table->mask) {
		if (((dev->periphid & table->mask) == table->id) &&
			((dev->cid != CORESIGHT_CID) ||
			 (amba_cs_uci_id_match(table, dev))))
			return table;
		table++;
	}
	return NULL;
}

static int amba_match(struct device *dev, struct device_driver *drv)
{
	struct amba_device *pcdev = to_amba_device(dev);
	struct amba_driver *pcdrv = to_amba_driver(drv);

	/* When driver_override is set, only bind to the matching driver */
	if (pcdev->driver_override)
		return !strcmp(pcdev->driver_override, drv->name);

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

static ssize_t driver_override_show(struct device *_dev,
				    struct device_attribute *attr, char *buf)
{
	struct amba_device *dev = to_amba_device(_dev);
	ssize_t len;

	device_lock(_dev);
	len = sprintf(buf, "%s\n", dev->driver_override);
	device_unlock(_dev);
	return len;
}

static ssize_t driver_override_store(struct device *_dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct amba_device *dev = to_amba_device(_dev);
	char *driver_override, *old, *cp;

	/* We need to keep extra room for a newline */
	if (count >= (PAGE_SIZE - 1))
		return -EINVAL;

	driver_override = kstrndup(buf, count, GFP_KERNEL);
	if (!driver_override)
		return -ENOMEM;

	cp = strchr(driver_override, '\n');
	if (cp)
		*cp = '\0';

	device_lock(_dev);
	old = dev->driver_override;
	if (strlen(driver_override)) {
		dev->driver_override = driver_override;
	} else {
		kfree(driver_override);
		dev->driver_override = NULL;
	}
	device_unlock(_dev);

	kfree(old);

	return count;
}
static DEVICE_ATTR_RW(driver_override);

#define amba_attr_func(name,fmt,arg...)					\
static ssize_t name##_show(struct device *_dev,				\
			   struct device_attribute *attr, char *buf)	\
{									\
	struct amba_device *dev = to_amba_device(_dev);			\
	return sprintf(buf, fmt, arg);					\
}									\
static DEVICE_ATTR_RO(name)

amba_attr_func(id, "%08x\n", dev->periphid);
amba_attr_func(irq0, "%u\n", dev->irq[0]);
amba_attr_func(irq1, "%u\n", dev->irq[1]);
amba_attr_func(resource, "\t%016llx\t%016llx\t%016lx\n",
	 (unsigned long long)dev->res.start, (unsigned long long)dev->res.end,
	 dev->res.flags);

static struct attribute *amba_dev_attrs[] = {
	&dev_attr_id.attr,
	&dev_attr_resource.attr,
	&dev_attr_driver_override.attr,
	NULL,
};
ATTRIBUTE_GROUPS(amba_dev);

#ifdef CONFIG_PM
/*
 * Hooks to provide runtime PM of the pclk (bus clock).  It is safe to
 * enable/disable the bus clock at runtime PM suspend/resume as this
 * does not result in loss of context.
 */
static int amba_pm_runtime_suspend(struct device *dev)
{
	struct amba_device *pcdev = to_amba_device(dev);
	int ret = pm_generic_runtime_suspend(dev);

	if (ret == 0 && dev->driver) {
		if (pm_runtime_is_irq_safe(dev))
			clk_disable(pcdev->pclk);
		else
			clk_disable_unprepare(pcdev->pclk);
	}

	return ret;
}

static int amba_pm_runtime_resume(struct device *dev)
{
	struct amba_device *pcdev = to_amba_device(dev);
	int ret;

	if (dev->driver) {
		if (pm_runtime_is_irq_safe(dev))
			ret = clk_enable(pcdev->pclk);
		else
			ret = clk_prepare_enable(pcdev->pclk);
		/* Failure is probably fatal to the system, but... */
		if (ret)
			return ret;
	}

	return pm_generic_runtime_resume(dev);
}
#endif /* CONFIG_PM */

static const struct dev_pm_ops amba_pm = {
	.suspend	= pm_generic_suspend,
	.resume		= pm_generic_resume,
	.freeze		= pm_generic_freeze,
	.thaw		= pm_generic_thaw,
	.poweroff	= pm_generic_poweroff,
	.restore	= pm_generic_restore,
	SET_RUNTIME_PM_OPS(
		amba_pm_runtime_suspend,
		amba_pm_runtime_resume,
		NULL
	)
};

/*
 * Primecells are part of the Advanced Microcontroller Bus Architecture,
 * so we call the bus "amba".
 * DMA configuration for platform and AMBA bus is same. So here we reuse
 * platform's DMA config routine.
 */
struct bus_type amba_bustype = {
	.name		= "amba",
	.dev_groups	= amba_dev_groups,
	.match		= amba_match,
	.uevent		= amba_uevent,
	.dma_configure	= platform_dma_configure,
	.pm		= &amba_pm,
};
EXPORT_SYMBOL_GPL(amba_bustype);

static int __init amba_init(void)
{
	return bus_register(&amba_bustype);
}

postcore_initcall(amba_init);

static int amba_get_enable_pclk(struct amba_device *pcdev)
{
	int ret;

	pcdev->pclk = clk_get(&pcdev->dev, "apb_pclk");
	if (IS_ERR(pcdev->pclk))
		return PTR_ERR(pcdev->pclk);

	ret = clk_prepare_enable(pcdev->pclk);
	if (ret)
		clk_put(pcdev->pclk);

	return ret;
}

static void amba_put_disable_pclk(struct amba_device *pcdev)
{
	clk_disable_unprepare(pcdev->pclk);
	clk_put(pcdev->pclk);
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
		ret = of_clk_set_defaults(dev->of_node, false);
		if (ret < 0)
			break;

		ret = dev_pm_domain_attach(dev, true);
		if (ret)
			break;

		ret = amba_get_enable_pclk(pcdev);
		if (ret) {
			dev_pm_domain_detach(dev, true);
			break;
		}

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
		dev_pm_domain_detach(dev, true);
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
	dev_pm_domain_detach(dev, true);

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

static int amba_device_try_add(struct amba_device *dev, struct resource *parent)
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

	ret = dev_pm_domain_attach(&dev->dev, true);
	if (ret) {
		iounmap(tmp);
		goto err_release;
	}

	ret = amba_get_enable_pclk(dev);
	if (ret == 0) {
		u32 pid, cid;
		struct reset_control *rstc;

		/*
		 * Find reset control(s) of the amba bus and de-assert them.
		 */
		rstc = of_reset_control_array_get_optional_shared(dev->dev.of_node);
		if (IS_ERR(rstc)) {
			ret = PTR_ERR(rstc);
			if (ret != -EPROBE_DEFER)
				dev_err(&dev->dev, "can't get reset: %d\n",
					ret);
			goto err_reset;
		}
		reset_control_deassert(rstc);
		reset_control_put(rstc);

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

		if (cid == CORESIGHT_CID) {
			/* set the base to the start of the last 4k block */
			void __iomem *csbase = tmp + size - 4096;

			dev->uci.devarch =
				readl(csbase + UCI_REG_DEVARCH_OFFSET);
			dev->uci.devtype =
				readl(csbase + UCI_REG_DEVTYPE_OFFSET) & 0xff;
		}

		amba_put_disable_pclk(dev);

		if (cid == AMBA_CID || cid == CORESIGHT_CID) {
			dev->periphid = pid;
			dev->cid = cid;
		}

		if (!dev->periphid)
			ret = -ENODEV;
	}

	iounmap(tmp);
	dev_pm_domain_detach(&dev->dev, true);

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

 err_reset:
	amba_put_disable_pclk(dev);
	iounmap(tmp);
	dev_pm_domain_detach(&dev->dev, true);
	goto err_release;
}

/*
 * Registration of AMBA device require reading its pid and cid registers.
 * To do this, the device must be turned on (if it is a part of power domain)
 * and have clocks enabled. However in some cases those resources might not be
 * yet available. Returning EPROBE_DEFER is not a solution in such case,
 * because callers don't handle this special error code. Instead such devices
 * are added to the special list and their registration is retried from
 * periodic worker, until all resources are available and registration succeeds.
 */
struct deferred_device {
	struct amba_device *dev;
	struct resource *parent;
	struct list_head node;
};

static LIST_HEAD(deferred_devices);
static DEFINE_MUTEX(deferred_devices_lock);

static void amba_deferred_retry_func(struct work_struct *dummy);
static DECLARE_DELAYED_WORK(deferred_retry_work, amba_deferred_retry_func);

#define DEFERRED_DEVICE_TIMEOUT (msecs_to_jiffies(5 * 1000))

static void amba_deferred_retry_func(struct work_struct *dummy)
{
	struct deferred_device *ddev, *tmp;

	mutex_lock(&deferred_devices_lock);

	list_for_each_entry_safe(ddev, tmp, &deferred_devices, node) {
		int ret = amba_device_try_add(ddev->dev, ddev->parent);

		if (ret == -EPROBE_DEFER)
			continue;

		list_del_init(&ddev->node);
		kfree(ddev);
	}

	if (!list_empty(&deferred_devices))
		schedule_delayed_work(&deferred_retry_work,
				      DEFERRED_DEVICE_TIMEOUT);

	mutex_unlock(&deferred_devices_lock);
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
	int ret = amba_device_try_add(dev, parent);

	if (ret == -EPROBE_DEFER) {
		struct deferred_device *ddev;

		ddev = kmalloc(sizeof(*ddev), GFP_KERNEL);
		if (!ddev)
			return -ENOMEM;

		ddev->dev = dev;
		ddev->parent = parent;
		ret = 0;

		mutex_lock(&deferred_devices_lock);

		if (list_empty(&deferred_devices))
			schedule_delayed_work(&deferred_retry_work,
					      DEFERRED_DEVICE_TIMEOUT);
		list_add_tail(&ddev->node, &deferred_devices);

		mutex_unlock(&deferred_devices_lock);
	}
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
	dev->dev.dma_parms = &dev->dma_parms;
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
