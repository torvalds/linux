// SPDX-License-Identifier: GPL-2.0-only
/*
 * vDPA bus.
 *
 * Copyright (c) 2020, Red Hat. All rights reserved.
 *     Author: Jason Wang <jasowang@redhat.com>
 *
 */

#include <linux/module.h>
#include <linux/idr.h>
#include <linux/slab.h>
#include <linux/vdpa.h>

/* A global mutex that protects vdpa management device and device level operations. */
static DEFINE_MUTEX(vdpa_dev_mutex);
static DEFINE_IDA(vdpa_index_ida);

static int vdpa_dev_probe(struct device *d)
{
	struct vdpa_device *vdev = dev_to_vdpa(d);
	struct vdpa_driver *drv = drv_to_vdpa(vdev->dev.driver);
	int ret = 0;

	if (drv && drv->probe)
		ret = drv->probe(vdev);

	return ret;
}

static int vdpa_dev_remove(struct device *d)
{
	struct vdpa_device *vdev = dev_to_vdpa(d);
	struct vdpa_driver *drv = drv_to_vdpa(vdev->dev.driver);

	if (drv && drv->remove)
		drv->remove(vdev);

	return 0;
}

static struct bus_type vdpa_bus = {
	.name  = "vdpa",
	.probe = vdpa_dev_probe,
	.remove = vdpa_dev_remove,
};

static void vdpa_release_dev(struct device *d)
{
	struct vdpa_device *vdev = dev_to_vdpa(d);
	const struct vdpa_config_ops *ops = vdev->config;

	if (ops->free)
		ops->free(vdev);

	ida_simple_remove(&vdpa_index_ida, vdev->index);
	kfree(vdev);
}

/**
 * __vdpa_alloc_device - allocate and initilaize a vDPA device
 * This allows driver to some prepartion after device is
 * initialized but before registered.
 * @parent: the parent device
 * @config: the bus operations that is supported by this device
 * @nvqs: number of virtqueues supported by this device
 * @size: size of the parent structure that contains private data
 * @name: name of the vdpa device; optional.
 *
 * Driver should use vdpa_alloc_device() wrapper macro instead of
 * using this directly.
 *
 * Returns an error when parent/config/dma_dev is not set or fail to get
 * ida.
 */
struct vdpa_device *__vdpa_alloc_device(struct device *parent,
					const struct vdpa_config_ops *config,
					int nvqs, size_t size, const char *name)
{
	struct vdpa_device *vdev;
	int err = -EINVAL;

	if (!config)
		goto err;

	if (!!config->dma_map != !!config->dma_unmap)
		goto err;

	err = -ENOMEM;
	vdev = kzalloc(size, GFP_KERNEL);
	if (!vdev)
		goto err;

	err = ida_alloc(&vdpa_index_ida, GFP_KERNEL);
	if (err < 0)
		goto err_ida;

	vdev->dev.bus = &vdpa_bus;
	vdev->dev.parent = parent;
	vdev->dev.release = vdpa_release_dev;
	vdev->index = err;
	vdev->config = config;
	vdev->features_valid = false;
	vdev->nvqs = nvqs;

	if (name)
		err = dev_set_name(&vdev->dev, "%s", name);
	else
		err = dev_set_name(&vdev->dev, "vdpa%u", vdev->index);
	if (err)
		goto err_name;

	device_initialize(&vdev->dev);

	return vdev;

err_name:
	ida_simple_remove(&vdpa_index_ida, vdev->index);
err_ida:
	kfree(vdev);
err:
	return ERR_PTR(err);
}
EXPORT_SYMBOL_GPL(__vdpa_alloc_device);

static int vdpa_name_match(struct device *dev, const void *data)
{
	struct vdpa_device *vdev = container_of(dev, struct vdpa_device, dev);

	return (strcmp(dev_name(&vdev->dev), data) == 0);
}

/**
 * vdpa_register_device - register a vDPA device
 * Callers must have a succeed call of vdpa_alloc_device() before.
 * @vdev: the vdpa device to be registered to vDPA bus
 *
 * Returns an error when fail to add to vDPA bus
 */
int vdpa_register_device(struct vdpa_device *vdev)
{
	struct device *dev;
	int err;

	mutex_lock(&vdpa_dev_mutex);
	dev = bus_find_device(&vdpa_bus, NULL, dev_name(&vdev->dev), vdpa_name_match);
	if (dev) {
		put_device(dev);
		err = -EEXIST;
		goto name_err;
	}

	err = device_add(&vdev->dev);
name_err:
	mutex_unlock(&vdpa_dev_mutex);
	return err;
}
EXPORT_SYMBOL_GPL(vdpa_register_device);

/**
 * vdpa_unregister_device - unregister a vDPA device
 * @vdev: the vdpa device to be unregisted from vDPA bus
 */
void vdpa_unregister_device(struct vdpa_device *vdev)
{
	mutex_lock(&vdpa_dev_mutex);
	device_unregister(&vdev->dev);
	mutex_unlock(&vdpa_dev_mutex);
}
EXPORT_SYMBOL_GPL(vdpa_unregister_device);

/**
 * __vdpa_register_driver - register a vDPA device driver
 * @drv: the vdpa device driver to be registered
 * @owner: module owner of the driver
 *
 * Returns an err when fail to do the registration
 */
int __vdpa_register_driver(struct vdpa_driver *drv, struct module *owner)
{
	drv->driver.bus = &vdpa_bus;
	drv->driver.owner = owner;

	return driver_register(&drv->driver);
}
EXPORT_SYMBOL_GPL(__vdpa_register_driver);

/**
 * vdpa_unregister_driver - unregister a vDPA device driver
 * @drv: the vdpa device driver to be unregistered
 */
void vdpa_unregister_driver(struct vdpa_driver *drv)
{
	driver_unregister(&drv->driver);
}
EXPORT_SYMBOL_GPL(vdpa_unregister_driver);

static int vdpa_init(void)
{
	return bus_register(&vdpa_bus);
}

static void __exit vdpa_exit(void)
{
	bus_unregister(&vdpa_bus);
	ida_destroy(&vdpa_index_ida);
}
core_initcall(vdpa_init);
module_exit(vdpa_exit);

MODULE_AUTHOR("Jason Wang <jasowang@redhat.com>");
MODULE_LICENSE("GPL v2");
