/*
 * drivers/video/tegra/host/bus.c
 *
 * Copyright (C) 2010 Google, Inc.
 *
 * Author:
 *	Erik Gilling <konkers@google.com>
 *
 * based heavily on drivers/base/platform.c
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/pm_runtime.h>

#include <mach/nvhost.h>

#include "dev.h"

struct nvhost_master *nvhost;
struct device nvhost_bus = {
	.init_name	= "nvhost",
};

struct resource *nvhost_get_resource(struct nvhost_device *dev,
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
EXPORT_SYMBOL_GPL(nvhost_get_resource);

int nvhost_get_irq(struct nvhost_device *dev, unsigned int num)
{
	struct resource *r = nvhost_get_resource(dev, IORESOURCE_IRQ, num);

	return r ? r->start : -ENXIO;
}
EXPORT_SYMBOL_GPL(nvhost_get_irq);

struct resource *nvhost_get_resource_byname(struct nvhost_device *dev,
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
EXPORT_SYMBOL_GPL(nvhost_get_resource_byname);

int nvhost_get_irq_byname(struct nvhost_device *dev, const char *name)
{
	struct resource *r = nvhost_get_resource_byname(dev, IORESOURCE_IRQ,
							  name);

	return r ? r->start : -ENXIO;
}
EXPORT_SYMBOL_GPL(nvhost_get_irq_byname);

static int nvhost_drv_probe(struct device *_dev)
{
	struct nvhost_driver *drv = to_nvhost_driver(_dev->driver);
	struct nvhost_device *dev = to_nvhost_device(_dev);

	dev->host = nvhost;

	return drv->probe(dev);
}

static int nvhost_drv_remove(struct device *_dev)
{
	struct nvhost_driver *drv = to_nvhost_driver(_dev->driver);
	struct nvhost_device *dev = to_nvhost_device(_dev);

	return drv->remove(dev);
}

static void nvhost_drv_shutdown(struct device *_dev)
{
	struct nvhost_driver *drv = to_nvhost_driver(_dev->driver);
	struct nvhost_device *dev = to_nvhost_device(_dev);

	drv->shutdown(dev);
}

int nvhost_driver_register(struct nvhost_driver *drv)
{
	drv->driver.bus = &nvhost_bus_type;
	if (drv->probe)
		drv->driver.probe = nvhost_drv_probe;
	if (drv->remove)
		drv->driver.remove = nvhost_drv_remove;
	if (drv->shutdown)
		drv->driver.shutdown = nvhost_drv_shutdown;

	return driver_register(&drv->driver);
}
EXPORT_SYMBOL(nvhost_driver_register);

void nvhost_driver_unregister(struct nvhost_driver *drv)
{
	driver_unregister(&drv->driver);
}
EXPORT_SYMBOL_GPL(nvhost_driver_unregister);

int nvhost_device_register(struct nvhost_device *dev)
{
	int i, ret = 0;

	if (!dev)
		return -EINVAL;

	device_initialize(&dev->dev);

	if (!dev->dev.parent)
		dev->dev.parent = &nvhost_bus;

	dev->dev.bus = &nvhost_bus_type;

	if (dev->id != -1)
		dev_set_name(&dev->dev, "%s.%d", dev->name,  dev->id);
	else
		dev_set_name(&dev->dev, "%s", dev->name);

	for (i = 0; i < dev->num_resources; i++) {
		struct resource *p, *r = &dev->resource[i];

		if (r->name == NULL)
			r->name = dev_name(&dev->dev);

		p = r->parent;
		if (!p) {
			if (resource_type(r) == IORESOURCE_MEM)
				p = &iomem_resource;
			else if (resource_type(r) == IORESOURCE_IO)
				p = &ioport_resource;
		}

		if (p && insert_resource(p, r)) {
			pr_err("%s: failed to claim resource %d\n",
			       dev_name(&dev->dev), i);
			ret = -EBUSY;
			goto failed;
		}
	}

	ret = device_add(&dev->dev);
	if (ret == 0)
		return ret;

failed:
	while (--i >= 0) {
		struct resource *r = &dev->resource[i];
		unsigned long type = resource_type(r);

		if (type == IORESOURCE_MEM || type == IORESOURCE_IO)
			release_resource(r);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(nvhost_device_register);

void nvhost_device_unregister(struct nvhost_device *dev)
{
	int i;
	if (dev) {
		device_del(&dev->dev);

		for (i = 0; i < dev->num_resources; i++) {
			struct resource *r = &dev->resource[i];
			unsigned long type = resource_type(r);

			if (type == IORESOURCE_MEM || type == IORESOURCE_IO)
				release_resource(r);
		}

		put_device(&dev->dev);
	}
}
EXPORT_SYMBOL_GPL(nvhost_device_unregister);


static int nvhost_bus_match(struct device *_dev, struct device_driver *drv)
{
	struct nvhost_device *dev = to_nvhost_device(_dev);

	pr_info("host1x: %s %s\n", dev->name, drv->name);
	return !strncmp(dev->name, drv->name, strlen(drv->name));
}

#ifdef CONFIG_PM_SLEEP

static int nvhost_legacy_suspend(struct device *dev, pm_message_t mesg)
{
	struct nvhost_driver *pdrv = to_nvhost_driver(dev->driver);
	struct nvhost_device *pdev = to_nvhost_device(dev);
	int ret = 0;

	if (dev->driver && pdrv->suspend)
		ret = pdrv->suspend(pdev, mesg);

	return ret;
}

static int nvhost_legacy_resume(struct device *dev)
{
	struct nvhost_driver *pdrv = to_nvhost_driver(dev->driver);
	struct nvhost_device *pdev = to_nvhost_device(dev);
	int ret = 0;

	if (dev->driver && pdrv->resume)
		ret = pdrv->resume(pdev);

	return ret;
}

static int nvhost_pm_prepare(struct device *dev)
{
	struct device_driver *drv = dev->driver;
	int ret = 0;

	if (drv && drv->pm && drv->pm->prepare)
		ret = drv->pm->prepare(dev);

	return ret;
}

static void nvhost_pm_complete(struct device *dev)
{
	struct device_driver *drv = dev->driver;

	if (drv && drv->pm && drv->pm->complete)
		drv->pm->complete(dev);
}

#else /* !CONFIG_PM_SLEEP */

#define nvhost_pm_prepare		NULL
#define nvhost_pm_complete		NULL

#endif /* !CONFIG_PM_SLEEP */

#ifdef CONFIG_SUSPEND

int __weak nvhost_pm_suspend(struct device *dev)
{
	struct device_driver *drv = dev->driver;
	int ret = 0;

	if (!drv)
		return 0;

	if (drv->pm) {
		if (drv->pm->suspend)
			ret = drv->pm->suspend(dev);
	} else {
		ret = nvhost_legacy_suspend(dev, PMSG_SUSPEND);
	}

	return ret;
}

int __weak nvhost_pm_suspend_noirq(struct device *dev)
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

int __weak nvhost_pm_resume(struct device *dev)
{
	struct device_driver *drv = dev->driver;
	int ret = 0;

	if (!drv)
		return 0;

	if (drv->pm) {
		if (drv->pm->resume)
			ret = drv->pm->resume(dev);
	} else {
		ret = nvhost_legacy_resume(dev);
	}

	return ret;
}

int __weak nvhost_pm_resume_noirq(struct device *dev)
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

#define nvhost_pm_suspend		NULL
#define nvhost_pm_resume		NULL
#define nvhost_pm_suspend_noirq	NULL
#define nvhost_pm_resume_noirq	NULL

#endif /* !CONFIG_SUSPEND */

#ifdef CONFIG_HIBERNATION

static int nvhost_pm_freeze(struct device *dev)
{
	struct device_driver *drv = dev->driver;
	int ret = 0;

	if (!drv)
		return 0;

	if (drv->pm) {
		if (drv->pm->freeze)
			ret = drv->pm->freeze(dev);
	} else {
		ret = nvhost_legacy_suspend(dev, PMSG_FREEZE);
	}

	return ret;
}

static int nvhost_pm_freeze_noirq(struct device *dev)
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

static int nvhost_pm_thaw(struct device *dev)
{
	struct device_driver *drv = dev->driver;
	int ret = 0;

	if (!drv)
		return 0;

	if (drv->pm) {
		if (drv->pm->thaw)
			ret = drv->pm->thaw(dev);
	} else {
		ret = nvhost_legacy_resume(dev);
	}

	return ret;
}

static int nvhost_pm_thaw_noirq(struct device *dev)
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

static int nvhost_pm_poweroff(struct device *dev)
{
	struct device_driver *drv = dev->driver;
	int ret = 0;

	if (!drv)
		return 0;

	if (drv->pm) {
		if (drv->pm->poweroff)
			ret = drv->pm->poweroff(dev);
	} else {
		ret = nvhost_legacy_suspend(dev, PMSG_HIBERNATE);
	}

	return ret;
}

static int nvhost_pm_poweroff_noirq(struct device *dev)
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

static int nvhost_pm_restore(struct device *dev)
{
	struct device_driver *drv = dev->driver;
	int ret = 0;

	if (!drv)
		return 0;

	if (drv->pm) {
		if (drv->pm->restore)
			ret = drv->pm->restore(dev);
	} else {
		ret = nvhost_legacy_resume(dev);
	}

	return ret;
}

static int nvhost_pm_restore_noirq(struct device *dev)
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

#define nvhost_pm_freeze		NULL
#define nvhost_pm_thaw		NULL
#define nvhost_pm_poweroff		NULL
#define nvhost_pm_restore		NULL
#define nvhost_pm_freeze_noirq	NULL
#define nvhost_pm_thaw_noirq		NULL
#define nvhost_pm_poweroff_noirq	NULL
#define nvhost_pm_restore_noirq	NULL

#endif /* !CONFIG_HIBERNATION */

#ifdef CONFIG_PM_RUNTIME

int __weak nvhost_pm_runtime_suspend(struct device *dev)
{
	return pm_generic_runtime_suspend(dev);
};

int __weak nvhost_pm_runtime_resume(struct device *dev)
{
	return pm_generic_runtime_resume(dev);
};

int __weak nvhost_pm_runtime_idle(struct device *dev)
{
	return pm_generic_runtime_idle(dev);
};

#else /* !CONFIG_PM_RUNTIME */

#define nvhost_pm_runtime_suspend NULL
#define nvhost_pm_runtime_resume NULL
#define nvhost_pm_runtime_idle NULL

#endif /* !CONFIG_PM_RUNTIME */

static const struct dev_pm_ops nvhost_dev_pm_ops = {
	.prepare = nvhost_pm_prepare,
	.complete = nvhost_pm_complete,
	.suspend = nvhost_pm_suspend,
	.resume = nvhost_pm_resume,
	.freeze = nvhost_pm_freeze,
	.thaw = nvhost_pm_thaw,
	.poweroff = nvhost_pm_poweroff,
	.restore = nvhost_pm_restore,
	.suspend_noirq = nvhost_pm_suspend_noirq,
	.resume_noirq = nvhost_pm_resume_noirq,
	.freeze_noirq = nvhost_pm_freeze_noirq,
	.thaw_noirq = nvhost_pm_thaw_noirq,
	.poweroff_noirq = nvhost_pm_poweroff_noirq,
	.restore_noirq = nvhost_pm_restore_noirq,
	.runtime_suspend = nvhost_pm_runtime_suspend,
	.runtime_resume = nvhost_pm_runtime_resume,
	.runtime_idle = nvhost_pm_runtime_idle,
};

struct bus_type nvhost_bus_type = {
	.name		= "nvhost",
	.match		= nvhost_bus_match,
	.pm		= &nvhost_dev_pm_ops,
};
EXPORT_SYMBOL(nvhost_bus_type);

int nvhost_bus_register(struct nvhost_master *host)
{
	nvhost = host;

	return 0;
}


int nvhost_bus_init(void)
{
	int err;

	pr_info("host1x bus init\n");
	err = device_register(&nvhost_bus);
	if (err)
		return err;

	err = bus_register(&nvhost_bus_type);
	if (err)
		device_unregister(&nvhost_bus);

	return err;
}
postcore_initcall(nvhost_bus_init);

