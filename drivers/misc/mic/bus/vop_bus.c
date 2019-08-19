// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * Copyright(c) 2016 Intel Corporation.
 *
 * Intel Virtio Over PCIe (VOP) Bus driver.
 */
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/idr.h>
#include <linux/dma-mapping.h>

#include "vop_bus.h"

static ssize_t device_show(struct device *d,
			   struct device_attribute *attr, char *buf)
{
	struct vop_device *dev = dev_to_vop(d);

	return sprintf(buf, "0x%04x\n", dev->id.device);
}
static DEVICE_ATTR_RO(device);

static ssize_t vendor_show(struct device *d,
			   struct device_attribute *attr, char *buf)
{
	struct vop_device *dev = dev_to_vop(d);

	return sprintf(buf, "0x%04x\n", dev->id.vendor);
}
static DEVICE_ATTR_RO(vendor);

static ssize_t modalias_show(struct device *d,
			     struct device_attribute *attr, char *buf)
{
	struct vop_device *dev = dev_to_vop(d);

	return sprintf(buf, "vop:d%08Xv%08X\n",
		       dev->id.device, dev->id.vendor);
}
static DEVICE_ATTR_RO(modalias);

static struct attribute *vop_dev_attrs[] = {
	&dev_attr_device.attr,
	&dev_attr_vendor.attr,
	&dev_attr_modalias.attr,
	NULL,
};
ATTRIBUTE_GROUPS(vop_dev);

static inline int vop_id_match(const struct vop_device *dev,
			       const struct vop_device_id *id)
{
	if (id->device != dev->id.device && id->device != VOP_DEV_ANY_ID)
		return 0;

	return id->vendor == VOP_DEV_ANY_ID || id->vendor == dev->id.vendor;
}

/*
 * This looks through all the IDs a driver claims to support.  If any of them
 * match, we return 1 and the kernel will call vop_dev_probe().
 */
static int vop_dev_match(struct device *dv, struct device_driver *dr)
{
	unsigned int i;
	struct vop_device *dev = dev_to_vop(dv);
	const struct vop_device_id *ids;

	ids = drv_to_vop(dr)->id_table;
	for (i = 0; ids[i].device; i++)
		if (vop_id_match(dev, &ids[i]))
			return 1;
	return 0;
}

static int vop_uevent(struct device *dv, struct kobj_uevent_env *env)
{
	struct vop_device *dev = dev_to_vop(dv);

	return add_uevent_var(env, "MODALIAS=vop:d%08Xv%08X",
			      dev->id.device, dev->id.vendor);
}

static int vop_dev_probe(struct device *d)
{
	struct vop_device *dev = dev_to_vop(d);
	struct vop_driver *drv = drv_to_vop(dev->dev.driver);

	return drv->probe(dev);
}

static int vop_dev_remove(struct device *d)
{
	struct vop_device *dev = dev_to_vop(d);
	struct vop_driver *drv = drv_to_vop(dev->dev.driver);

	drv->remove(dev);
	return 0;
}

static struct bus_type vop_bus = {
	.name  = "vop_bus",
	.match = vop_dev_match,
	.dev_groups = vop_dev_groups,
	.uevent = vop_uevent,
	.probe = vop_dev_probe,
	.remove = vop_dev_remove,
};

int vop_register_driver(struct vop_driver *driver)
{
	driver->driver.bus = &vop_bus;
	return driver_register(&driver->driver);
}
EXPORT_SYMBOL_GPL(vop_register_driver);

void vop_unregister_driver(struct vop_driver *driver)
{
	driver_unregister(&driver->driver);
}
EXPORT_SYMBOL_GPL(vop_unregister_driver);

static void vop_release_dev(struct device *d)
{
	struct vop_device *dev = dev_to_vop(d);

	kfree(dev);
}

struct vop_device *
vop_register_device(struct device *pdev, int id,
		    const struct dma_map_ops *dma_ops,
		    struct vop_hw_ops *hw_ops, u8 dnode, struct mic_mw *aper,
		    struct dma_chan *chan)
{
	int ret;
	struct vop_device *vdev;

	vdev = kzalloc(sizeof(*vdev), GFP_KERNEL);
	if (!vdev)
		return ERR_PTR(-ENOMEM);

	vdev->dev.parent = pdev;
	vdev->id.device = id;
	vdev->id.vendor = VOP_DEV_ANY_ID;
	vdev->dev.dma_ops = dma_ops;
	vdev->dev.dma_mask = &vdev->dev.coherent_dma_mask;
	dma_set_mask(&vdev->dev, DMA_BIT_MASK(64));
	vdev->dev.release = vop_release_dev;
	vdev->hw_ops = hw_ops;
	vdev->dev.bus = &vop_bus;
	vdev->dnode = dnode;
	vdev->aper = aper;
	vdev->dma_ch = chan;
	vdev->index = dnode - 1;
	dev_set_name(&vdev->dev, "vop-dev%u", vdev->index);
	/*
	 * device_register() causes the bus infrastructure to look for a
	 * matching driver.
	 */
	ret = device_register(&vdev->dev);
	if (ret)
		goto free_vdev;
	return vdev;
free_vdev:
	put_device(&vdev->dev);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(vop_register_device);

void vop_unregister_device(struct vop_device *dev)
{
	device_unregister(&dev->dev);
}
EXPORT_SYMBOL_GPL(vop_unregister_device);

static int __init vop_init(void)
{
	return bus_register(&vop_bus);
}

static void __exit vop_exit(void)
{
	bus_unregister(&vop_bus);
}

core_initcall(vop_init);
module_exit(vop_exit);

MODULE_AUTHOR("Intel Corporation");
MODULE_DESCRIPTION("Intel(R) VOP Bus driver");
MODULE_LICENSE("GPL v2");
