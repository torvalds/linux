/*
 * IBM PowerPC Virtual I/O Infrastructure Support.
 *
 *    Copyright (c) 2003-2005 IBM Corp.
 *     Dave Engebretsen engebret@us.ibm.com
 *     Santiago Leon santil@us.ibm.com
 *     Hollis Blanchard <hollisb@us.ibm.com>
 *     Stephen Rothwell
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/init.h>
#include <linux/console.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/dma-mapping.h>
#include <asm/iommu.h>
#include <asm/dma.h>
#include <asm/vio.h>
#include <asm/prom.h>

static const struct vio_device_id *vio_match_device(
		const struct vio_device_id *, const struct vio_dev *);

struct vio_dev vio_bus_device  = { /* fake "parent" device */
	.name = vio_bus_device.dev.bus_id,
	.type = "",
	.dev.bus_id = "vio",
	.dev.bus = &vio_bus_type,
};

static struct vio_bus_ops vio_bus_ops;

/*
 * Convert from struct device to struct vio_dev and pass to driver.
 * dev->driver has already been set by generic code because vio_bus_match
 * succeeded.
 */
static int vio_bus_probe(struct device *dev)
{
	struct vio_dev *viodev = to_vio_dev(dev);
	struct vio_driver *viodrv = to_vio_driver(dev->driver);
	const struct vio_device_id *id;
	int error = -ENODEV;

	if (!viodrv->probe)
		return error;

	id = vio_match_device(viodrv->id_table, viodev);
	if (id)
		error = viodrv->probe(viodev, id);

	return error;
}

/* convert from struct device to struct vio_dev and pass to driver. */
static int vio_bus_remove(struct device *dev)
{
	struct vio_dev *viodev = to_vio_dev(dev);
	struct vio_driver *viodrv = to_vio_driver(dev->driver);

	if (viodrv->remove)
		return viodrv->remove(viodev);

	/* driver can't remove */
	return 1;
}

/* convert from struct device to struct vio_dev and pass to driver. */
static void vio_bus_shutdown(struct device *dev)
{
	struct vio_dev *viodev = to_vio_dev(dev);
	struct vio_driver *viodrv = to_vio_driver(dev->driver);

	if (dev->driver && viodrv->shutdown)
		viodrv->shutdown(viodev);
}

/**
 * vio_register_driver: - Register a new vio driver
 * @drv:	The vio_driver structure to be registered.
 */
int vio_register_driver(struct vio_driver *viodrv)
{
	printk(KERN_DEBUG "%s: driver %s registering\n", __FUNCTION__,
		viodrv->driver.name);

	/* fill in 'struct driver' fields */
	viodrv->driver.bus = &vio_bus_type;

	return driver_register(&viodrv->driver);
}
EXPORT_SYMBOL(vio_register_driver);

/**
 * vio_unregister_driver - Remove registration of vio driver.
 * @driver:	The vio_driver struct to be removed form registration
 */
void vio_unregister_driver(struct vio_driver *viodrv)
{
	driver_unregister(&viodrv->driver);
}
EXPORT_SYMBOL(vio_unregister_driver);

/**
 * vio_match_device: - Tell if a VIO device has a matching
 *			VIO device id structure.
 * @ids:	array of VIO device id structures to search in
 * @dev:	the VIO device structure to match against
 *
 * Used by a driver to check whether a VIO device present in the
 * system is in its list of supported devices. Returns the matching
 * vio_device_id structure or NULL if there is no match.
 */
static const struct vio_device_id *vio_match_device(
		const struct vio_device_id *ids, const struct vio_dev *dev)
{
	while (ids->type[0] != '\0') {
		if (vio_bus_ops.match(ids, dev))
			return ids;
		ids++;
	}
	return NULL;
}

/**
 * vio_bus_init: - Initialize the virtual IO bus
 */
int __init vio_bus_init(struct vio_bus_ops *ops)
{
	int err;

	vio_bus_ops = *ops;

	err = bus_register(&vio_bus_type);
	if (err) {
		printk(KERN_ERR "failed to register VIO bus\n");
		return err;
	}

	/*
	 * The fake parent of all vio devices, just to give us
	 * a nice directory
	 */
	err = device_register(&vio_bus_device.dev);
	if (err) {
		printk(KERN_WARNING "%s: device_register returned %i\n",
				__FUNCTION__, err);
		return err;
	}

	return 0;
}

/* vio_dev refcount hit 0 */
static void __devinit vio_dev_release(struct device *dev)
{
	if (vio_bus_ops.release_device)
		vio_bus_ops.release_device(dev);
	kfree(to_vio_dev(dev));
}

static ssize_t viodev_show_name(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", to_vio_dev(dev)->name);
}
DEVICE_ATTR(name, S_IRUSR | S_IRGRP | S_IROTH, viodev_show_name, NULL);

struct vio_dev * __devinit vio_register_device(struct vio_dev *viodev)
{
	/* init generic 'struct device' fields: */
	viodev->dev.parent = &vio_bus_device.dev;
	viodev->dev.bus = &vio_bus_type;
	viodev->dev.release = vio_dev_release;

	/* register with generic device framework */
	if (device_register(&viodev->dev)) {
		printk(KERN_ERR "%s: failed to register device %s\n",
				__FUNCTION__, viodev->dev.bus_id);
		return NULL;
	}
	device_create_file(&viodev->dev, &dev_attr_name);

	return viodev;
}

void __devinit vio_unregister_device(struct vio_dev *viodev)
{
	if (vio_bus_ops.unregister_device)
		vio_bus_ops.unregister_device(viodev);
	device_remove_file(&viodev->dev, &dev_attr_name);
	device_unregister(&viodev->dev);
}
EXPORT_SYMBOL(vio_unregister_device);

static dma_addr_t vio_map_single(struct device *dev, void *vaddr,
			  size_t size, enum dma_data_direction direction)
{
	return iommu_map_single(to_vio_dev(dev)->iommu_table, vaddr, size,
			~0ul, direction);
}

static void vio_unmap_single(struct device *dev, dma_addr_t dma_handle,
		      size_t size, enum dma_data_direction direction)
{
	iommu_unmap_single(to_vio_dev(dev)->iommu_table, dma_handle, size,
			direction);
}

static int vio_map_sg(struct device *dev, struct scatterlist *sglist,
		int nelems, enum dma_data_direction direction)
{
	return iommu_map_sg(dev, to_vio_dev(dev)->iommu_table, sglist,
			nelems, ~0ul, direction);
}

static void vio_unmap_sg(struct device *dev, struct scatterlist *sglist,
		int nelems, enum dma_data_direction direction)
{
	iommu_unmap_sg(to_vio_dev(dev)->iommu_table, sglist, nelems, direction);
}

static void *vio_alloc_coherent(struct device *dev, size_t size,
			   dma_addr_t *dma_handle, gfp_t flag)
{
	return iommu_alloc_coherent(to_vio_dev(dev)->iommu_table, size,
			dma_handle, ~0ul, flag);
}

static void vio_free_coherent(struct device *dev, size_t size,
			 void *vaddr, dma_addr_t dma_handle)
{
	iommu_free_coherent(to_vio_dev(dev)->iommu_table, size, vaddr,
			dma_handle);
}

static int vio_dma_supported(struct device *dev, u64 mask)
{
	return 1;
}

struct dma_mapping_ops vio_dma_ops = {
	.alloc_coherent = vio_alloc_coherent,
	.free_coherent = vio_free_coherent,
	.map_single = vio_map_single,
	.unmap_single = vio_unmap_single,
	.map_sg = vio_map_sg,
	.unmap_sg = vio_unmap_sg,
	.dma_supported = vio_dma_supported,
};

static int vio_bus_match(struct device *dev, struct device_driver *drv)
{
	const struct vio_dev *vio_dev = to_vio_dev(dev);
	struct vio_driver *vio_drv = to_vio_driver(drv);
	const struct vio_device_id *ids = vio_drv->id_table;

	return (ids != NULL) && (vio_match_device(ids, vio_dev) != NULL);
}

static int vio_hotplug(struct device *dev, char **envp, int num_envp,
			char *buffer, int buffer_size)
{
	const struct vio_dev *vio_dev = to_vio_dev(dev);
	char *cp;
	int length;

	if (!num_envp)
		return -ENOMEM;

	if (!vio_dev->dev.platform_data)
		return -ENODEV;
	cp = (char *)get_property(vio_dev->dev.platform_data, "compatible", &length);
	if (!cp)
		return -ENODEV;

	envp[0] = buffer;
	length = scnprintf(buffer, buffer_size, "MODALIAS=vio:T%sS%s",
				vio_dev->type, cp);
	if (buffer_size - length <= 0)
		return -ENOMEM;
	envp[1] = NULL;
	return 0;
}

struct bus_type vio_bus_type = {
	.name = "vio",
	.uevent = vio_hotplug,
	.match = vio_bus_match,
	.probe = vio_bus_probe,
	.remove = vio_bus_remove,
	.shutdown = vio_bus_shutdown,
};
