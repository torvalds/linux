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
#include <linux/of_irq.h>
#include <linux/of_device.h>
#include <linux/acpi.h>
#include <linux/iommu.h>
#include <linux/dma-map-ops.h>

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
	int ret;

	ret = driver_set_override(_dev, &dev->driver_override, buf, count);
	if (ret)
		return ret;

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

static int amba_read_periphid(struct amba_device *dev)
{
	struct reset_control *rstc;
	u32 size, pid, cid;
	void __iomem *tmp;
	int i, ret;

	ret = dev_pm_domain_attach(&dev->dev, true);
	if (ret) {
		dev_dbg(&dev->dev, "can't get PM domain: %d\n", ret);
		goto err_out;
	}

	ret = amba_get_enable_pclk(dev);
	if (ret) {
		dev_dbg(&dev->dev, "can't get pclk: %d\n", ret);
		goto err_pm;
	}

	/*
	 * Find reset control(s) of the amba bus and de-assert them.
	 */
	rstc = of_reset_control_array_get_optional_shared(dev->dev.of_node);
	if (IS_ERR(rstc)) {
		ret = PTR_ERR(rstc);
		if (ret != -EPROBE_DEFER)
			dev_err(&dev->dev, "can't get reset: %d\n", ret);
		goto err_clk;
	}
	reset_control_deassert(rstc);
	reset_control_put(rstc);

	size = resource_size(&dev->res);
	tmp = ioremap(dev->res.start, size);
	if (!tmp) {
		ret = -ENOMEM;
		goto err_clk;
	}

	/*
	 * Read pid and cid based on size of resource
	 * they are located at end of region
	 */
	for (pid = 0, i = 0; i < 4; i++)
		pid |= (readl(tmp + size - 0x20 + 4 * i) & 255) << (i * 8);
	for (cid = 0, i = 0; i < 4; i++)
		cid |= (readl(tmp + size - 0x10 + 4 * i) & 255) << (i * 8);

	if (cid == CORESIGHT_CID) {
		/* set the base to the start of the last 4k block */
		void __iomem *csbase = tmp + size - 4096;

		dev->uci.devarch = readl(csbase + UCI_REG_DEVARCH_OFFSET);
		dev->uci.devtype = readl(csbase + UCI_REG_DEVTYPE_OFFSET) & 0xff;
	}

	if (cid == AMBA_CID || cid == CORESIGHT_CID) {
		dev->periphid = pid;
		dev->cid = cid;
	}

	if (!dev->periphid)
		ret = -ENODEV;

	iounmap(tmp);

err_clk:
	amba_put_disable_pclk(dev);
err_pm:
	dev_pm_domain_detach(&dev->dev, true);
err_out:
	return ret;
}

static int amba_match(struct device *dev, struct device_driver *drv)
{
	struct amba_device *pcdev = to_amba_device(dev);
	struct amba_driver *pcdrv = to_amba_driver(drv);

	mutex_lock(&pcdev->periphid_lock);
	if (!pcdev->periphid) {
		int ret = amba_read_periphid(pcdev);

		/*
		 * Returning any error other than -EPROBE_DEFER from bus match
		 * can cause driver registration failure. So, if there's a
		 * permanent failure in reading pid and cid, simply map it to
		 * -EPROBE_DEFER.
		 */
		if (ret) {
			mutex_unlock(&pcdev->periphid_lock);
			return -EPROBE_DEFER;
		}
		dev_set_uevent_suppress(dev, false);
		kobject_uevent(&dev->kobj, KOBJ_ADD);
	}
	mutex_unlock(&pcdev->periphid_lock);

	/* When driver_override is set, only bind to the matching driver */
	if (pcdev->driver_override)
		return !strcmp(pcdev->driver_override, drv->name);

	return amba_lookup(pcdrv->id_table, pcdev) != NULL;
}

static int amba_uevent(const struct device *dev, struct kobj_uevent_env *env)
{
	const struct amba_device *pcdev = to_amba_device(dev);
	int retval = 0;

	retval = add_uevent_var(env, "AMBA_ID=%08x", pcdev->periphid);
	if (retval)
		return retval;

	retval = add_uevent_var(env, "MODALIAS=amba:d%08X", pcdev->periphid);
	return retval;
}

static int of_amba_device_decode_irq(struct amba_device *dev)
{
	struct device_node *node = dev->dev.of_node;
	int i, irq = 0;

	if (IS_ENABLED(CONFIG_OF_IRQ) && node) {
		/* Decode the IRQs and address ranges */
		for (i = 0; i < AMBA_NR_IRQS; i++) {
			irq = of_irq_get(node, i);
			if (irq < 0) {
				if (irq == -EPROBE_DEFER)
					return irq;
				irq = 0;
			}

			dev->irq[i] = irq;
		}
	}

	return 0;
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
		ret = of_amba_device_decode_irq(pcdev);
		if (ret)
			break;

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

static void amba_remove(struct device *dev)
{
	struct amba_device *pcdev = to_amba_device(dev);
	struct amba_driver *drv = to_amba_driver(dev->driver);

	pm_runtime_get_sync(dev);
	if (drv->remove)
		drv->remove(pcdev);
	pm_runtime_put_noidle(dev);

	/* Undo the runtime PM settings in amba_probe() */
	pm_runtime_disable(dev);
	pm_runtime_set_suspended(dev);
	pm_runtime_put_noidle(dev);

	amba_put_disable_pclk(pcdev);
	dev_pm_domain_detach(dev, true);
}

static void amba_shutdown(struct device *dev)
{
	struct amba_driver *drv;

	if (!dev->driver)
		return;

	drv = to_amba_driver(dev->driver);
	if (drv->shutdown)
		drv->shutdown(to_amba_device(dev));
}

static int amba_dma_configure(struct device *dev)
{
	struct amba_driver *drv = to_amba_driver(dev->driver);
	enum dev_dma_attr attr;
	int ret = 0;

	if (dev->of_node) {
		ret = of_dma_configure(dev, dev->of_node, true);
	} else if (has_acpi_companion(dev)) {
		attr = acpi_get_dma_attr(to_acpi_device_node(dev->fwnode));
		ret = acpi_dma_configure(dev, attr);
	}

	if (!ret && !drv->driver_managed_dma) {
		ret = iommu_device_use_default_domain(dev);
		if (ret)
			arch_teardown_dma_ops(dev);
	}

	return ret;
}

static void amba_dma_cleanup(struct device *dev)
{
	struct amba_driver *drv = to_amba_driver(dev->driver);

	if (!drv->driver_managed_dma)
		iommu_device_unuse_default_domain(dev);
}

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
	.probe		= amba_probe,
	.remove		= amba_remove,
	.shutdown	= amba_shutdown,
	.dma_configure	= amba_dma_configure,
	.dma_cleanup	= amba_dma_cleanup,
	.pm		= &amba_pm,
};
EXPORT_SYMBOL_GPL(amba_bustype);

static int __init amba_init(void)
{
	return bus_register(&amba_bustype);
}

postcore_initcall(amba_init);

static int amba_proxy_probe(struct amba_device *adev,
			    const struct amba_id *id)
{
	WARN(1, "Stub driver should never match any device.\n");
	return -ENODEV;
}

static const struct amba_id amba_stub_drv_ids[] = {
	{ 0, 0 },
};

static struct amba_driver amba_proxy_drv = {
	.drv = {
		.name = "amba-proxy",
	},
	.probe = amba_proxy_probe,
	.id_table = amba_stub_drv_ids,
};

static int __init amba_stub_drv_init(void)
{
	if (!IS_ENABLED(CONFIG_MODULES))
		return 0;

	/*
	 * The amba_match() function will get called only if there is at least
	 * one amba driver registered. If all amba drivers are modules and are
	 * only loaded based on uevents, then we'll hit a chicken-and-egg
	 * situation where amba_match() is waiting on drivers and drivers are
	 * waiting on amba_match(). So, register a stub driver to make sure
	 * amba_match() is called even if no amba driver has been registered.
	 */
	return amba_driver_register(&amba_proxy_drv);
}
late_initcall_sync(amba_stub_drv_init);

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
	if (!drv->probe)
		return -EINVAL;

	drv->drv.bus = &amba_bustype;

	return driver_register(&drv->drv);
}
EXPORT_SYMBOL(amba_driver_register);

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
EXPORT_SYMBOL(amba_driver_unregister);

static void amba_device_release(struct device *dev)
{
	struct amba_device *d = to_amba_device(dev);

	if (d->res.parent)
		release_resource(&d->res);
	mutex_destroy(&d->periphid_lock);
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
	int ret;

	ret = request_resource(parent, &dev->res);
	if (ret)
		return ret;

	/* If primecell ID isn't hard-coded, figure it out */
	if (!dev->periphid) {
		/*
		 * AMBA device uevents require reading its pid and cid
		 * registers.  To do this, the device must be on, clocked and
		 * out of reset.  However in some cases those resources might
		 * not yet be available.  If that's the case, we suppress the
		 * generation of uevents until we can read the pid and cid
		 * registers.  See also amba_match().
		 */
		if (amba_read_periphid(dev))
			dev_set_uevent_suppress(&dev->dev, true);
	}

	ret = device_add(&dev->dev);
	if (ret)
		release_resource(&dev->res);

	return ret;
}
EXPORT_SYMBOL_GPL(amba_device_add);

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
	mutex_init(&dev->periphid_lock);
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
EXPORT_SYMBOL(amba_device_register);

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
EXPORT_SYMBOL(amba_device_unregister);

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
EXPORT_SYMBOL(amba_request_regions);

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
EXPORT_SYMBOL(amba_release_regions);
