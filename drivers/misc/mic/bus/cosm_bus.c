/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * Copyright(c) 2015 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Intel MIC COSM Bus Driver
 */
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/idr.h>
#include "cosm_bus.h"

/* Unique numbering for cosm devices. */
static DEFINE_IDA(cosm_index_ida);

static int cosm_dev_probe(struct device *d)
{
	struct cosm_device *dev = dev_to_cosm(d);
	struct cosm_driver *drv = drv_to_cosm(dev->dev.driver);

	return drv->probe(dev);
}

static int cosm_dev_remove(struct device *d)
{
	struct cosm_device *dev = dev_to_cosm(d);
	struct cosm_driver *drv = drv_to_cosm(dev->dev.driver);

	drv->remove(dev);
	return 0;
}

static struct bus_type cosm_bus = {
	.name  = "cosm_bus",
	.probe = cosm_dev_probe,
	.remove = cosm_dev_remove,
};

int cosm_register_driver(struct cosm_driver *driver)
{
	driver->driver.bus = &cosm_bus;
	return driver_register(&driver->driver);
}
EXPORT_SYMBOL_GPL(cosm_register_driver);

void cosm_unregister_driver(struct cosm_driver *driver)
{
	driver_unregister(&driver->driver);
}
EXPORT_SYMBOL_GPL(cosm_unregister_driver);

static inline void cosm_release_dev(struct device *d)
{
	struct cosm_device *cdev = dev_to_cosm(d);

	kfree(cdev);
}

struct cosm_device *
cosm_register_device(struct device *pdev, struct cosm_hw_ops *hw_ops)
{
	struct cosm_device *cdev;
	int ret;

	cdev = kzalloc(sizeof(*cdev), GFP_KERNEL);
	if (!cdev)
		return ERR_PTR(-ENOMEM);

	cdev->dev.parent = pdev;
	cdev->dev.release = cosm_release_dev;
	cdev->hw_ops = hw_ops;
	dev_set_drvdata(&cdev->dev, cdev);
	cdev->dev.bus = &cosm_bus;

	/* Assign a unique device index and hence name */
	ret = ida_simple_get(&cosm_index_ida, 0, 0, GFP_KERNEL);
	if (ret < 0)
		goto free_cdev;

	cdev->index = ret;
	cdev->dev.id = ret;
	dev_set_name(&cdev->dev, "cosm-dev%u", cdev->index);

	ret = device_register(&cdev->dev);
	if (ret)
		goto ida_remove;
	return cdev;
ida_remove:
	ida_simple_remove(&cosm_index_ida, cdev->index);
free_cdev:
	put_device(&cdev->dev);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(cosm_register_device);

void cosm_unregister_device(struct cosm_device *dev)
{
	int index = dev->index; /* save for after device release */

	device_unregister(&dev->dev);
	ida_simple_remove(&cosm_index_ida, index);
}
EXPORT_SYMBOL_GPL(cosm_unregister_device);

struct cosm_device *cosm_find_cdev_by_id(int id)
{
	struct device *dev = subsys_find_device_by_id(&cosm_bus, id, NULL);

	return dev ? container_of(dev, struct cosm_device, dev) : NULL;
}
EXPORT_SYMBOL_GPL(cosm_find_cdev_by_id);

static int __init cosm_init(void)
{
	return bus_register(&cosm_bus);
}

static void __exit cosm_exit(void)
{
	bus_unregister(&cosm_bus);
	ida_destroy(&cosm_index_ida);
}

core_initcall(cosm_init);
module_exit(cosm_exit);

MODULE_AUTHOR("Intel Corporation");
MODULE_DESCRIPTION("Intel(R) MIC card OS state management bus driver");
MODULE_LICENSE("GPL v2");
