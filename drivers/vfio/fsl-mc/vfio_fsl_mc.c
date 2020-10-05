// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/*
 * Copyright 2013-2016 Freescale Semiconductor Inc.
 * Copyright 2016-2017,2019-2020 NXP
 */

#include <linux/device.h>
#include <linux/iommu.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/vfio.h>
#include <linux/fsl/mc.h>

#include "vfio_fsl_mc_private.h"

static struct fsl_mc_driver vfio_fsl_mc_driver;

static int vfio_fsl_mc_regions_init(struct vfio_fsl_mc_device *vdev)
{
	struct fsl_mc_device *mc_dev = vdev->mc_dev;
	int count = mc_dev->obj_desc.region_count;
	int i;

	vdev->regions = kcalloc(count, sizeof(struct vfio_fsl_mc_region),
				GFP_KERNEL);
	if (!vdev->regions)
		return -ENOMEM;

	for (i = 0; i < count; i++) {
		struct resource *res = &mc_dev->regions[i];
		int no_mmap = is_fsl_mc_bus_dprc(mc_dev);

		vdev->regions[i].addr = res->start;
		vdev->regions[i].size = resource_size(res);
		vdev->regions[i].type = mc_dev->regions[i].flags & IORESOURCE_BITS;
		/*
		 * Only regions addressed with PAGE granularity may be
		 * MMAPed securely.
		 */
		if (!no_mmap && !(vdev->regions[i].addr & ~PAGE_MASK) &&
				!(vdev->regions[i].size & ~PAGE_MASK))
			vdev->regions[i].flags |=
					VFIO_REGION_INFO_FLAG_MMAP;

	}

	return 0;
}

static void vfio_fsl_mc_regions_cleanup(struct vfio_fsl_mc_device *vdev)
{
	kfree(vdev->regions);
}

static int vfio_fsl_mc_open(void *device_data)
{
	struct vfio_fsl_mc_device *vdev = device_data;
	int ret;

	if (!try_module_get(THIS_MODULE))
		return -ENODEV;

	mutex_lock(&vdev->driver_lock);
	if (!vdev->refcnt) {
		ret = vfio_fsl_mc_regions_init(vdev);
		if (ret)
			goto err_reg_init;
	}
	vdev->refcnt++;

	mutex_unlock(&vdev->driver_lock);

	return 0;

err_reg_init:
	mutex_unlock(&vdev->driver_lock);
	module_put(THIS_MODULE);
	return ret;
}

static void vfio_fsl_mc_release(void *device_data)
{
	struct vfio_fsl_mc_device *vdev = device_data;

	mutex_lock(&vdev->driver_lock);

	if (!(--vdev->refcnt))
		vfio_fsl_mc_regions_cleanup(vdev);

	mutex_unlock(&vdev->driver_lock);

	module_put(THIS_MODULE);
}

static long vfio_fsl_mc_ioctl(void *device_data, unsigned int cmd,
			      unsigned long arg)
{
	unsigned long minsz;
	struct vfio_fsl_mc_device *vdev = device_data;
	struct fsl_mc_device *mc_dev = vdev->mc_dev;

	switch (cmd) {
	case VFIO_DEVICE_GET_INFO:
	{
		struct vfio_device_info info;

		minsz = offsetofend(struct vfio_device_info, num_irqs);

		if (copy_from_user(&info, (void __user *)arg, minsz))
			return -EFAULT;

		if (info.argsz < minsz)
			return -EINVAL;

		info.flags = VFIO_DEVICE_FLAGS_FSL_MC;
		info.num_regions = mc_dev->obj_desc.region_count;
		info.num_irqs = mc_dev->obj_desc.irq_count;

		return copy_to_user((void __user *)arg, &info, minsz) ?
			-EFAULT : 0;
	}
	case VFIO_DEVICE_GET_REGION_INFO:
	{
		struct vfio_region_info info;

		minsz = offsetofend(struct vfio_region_info, offset);

		if (copy_from_user(&info, (void __user *)arg, minsz))
			return -EFAULT;

		if (info.argsz < minsz)
			return -EINVAL;

		if (info.index >= mc_dev->obj_desc.region_count)
			return -EINVAL;

		/* map offset to the physical address  */
		info.offset = VFIO_FSL_MC_INDEX_TO_OFFSET(info.index);
		info.size = vdev->regions[info.index].size;
		info.flags = vdev->regions[info.index].flags;

		return copy_to_user((void __user *)arg, &info, minsz);
	}
	case VFIO_DEVICE_GET_IRQ_INFO:
	{
		return -ENOTTY;
	}
	case VFIO_DEVICE_SET_IRQS:
	{
		return -ENOTTY;
	}
	case VFIO_DEVICE_RESET:
	{
		return -ENOTTY;
	}
	default:
		return -ENOTTY;
	}
}

static ssize_t vfio_fsl_mc_read(void *device_data, char __user *buf,
				size_t count, loff_t *ppos)
{
	return -EINVAL;
}

static ssize_t vfio_fsl_mc_write(void *device_data, const char __user *buf,
				 size_t count, loff_t *ppos)
{
	return -EINVAL;
}

static int vfio_fsl_mc_mmap_mmio(struct vfio_fsl_mc_region region,
				 struct vm_area_struct *vma)
{
	u64 size = vma->vm_end - vma->vm_start;
	u64 pgoff, base;
	u8 region_cacheable;

	pgoff = vma->vm_pgoff &
		((1U << (VFIO_FSL_MC_OFFSET_SHIFT - PAGE_SHIFT)) - 1);
	base = pgoff << PAGE_SHIFT;

	if (region.size < PAGE_SIZE || base + size > region.size)
		return -EINVAL;

	region_cacheable = (region.type & FSL_MC_REGION_CACHEABLE) &&
			   (region.type & FSL_MC_REGION_SHAREABLE);
	if (!region_cacheable)
		vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	vma->vm_pgoff = (region.addr >> PAGE_SHIFT) + pgoff;

	return remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
			       size, vma->vm_page_prot);
}

static int vfio_fsl_mc_mmap(void *device_data, struct vm_area_struct *vma)
{
	struct vfio_fsl_mc_device *vdev = device_data;
	struct fsl_mc_device *mc_dev = vdev->mc_dev;
	int index;

	index = vma->vm_pgoff >> (VFIO_FSL_MC_OFFSET_SHIFT - PAGE_SHIFT);

	if (vma->vm_end < vma->vm_start)
		return -EINVAL;
	if (vma->vm_start & ~PAGE_MASK)
		return -EINVAL;
	if (vma->vm_end & ~PAGE_MASK)
		return -EINVAL;
	if (!(vma->vm_flags & VM_SHARED))
		return -EINVAL;
	if (index >= mc_dev->obj_desc.region_count)
		return -EINVAL;

	if (!(vdev->regions[index].flags & VFIO_REGION_INFO_FLAG_MMAP))
		return -EINVAL;

	if (!(vdev->regions[index].flags & VFIO_REGION_INFO_FLAG_READ)
			&& (vma->vm_flags & VM_READ))
		return -EINVAL;

	if (!(vdev->regions[index].flags & VFIO_REGION_INFO_FLAG_WRITE)
			&& (vma->vm_flags & VM_WRITE))
		return -EINVAL;

	vma->vm_private_data = mc_dev;

	return vfio_fsl_mc_mmap_mmio(vdev->regions[index], vma);
}

static const struct vfio_device_ops vfio_fsl_mc_ops = {
	.name		= "vfio-fsl-mc",
	.open		= vfio_fsl_mc_open,
	.release	= vfio_fsl_mc_release,
	.ioctl		= vfio_fsl_mc_ioctl,
	.read		= vfio_fsl_mc_read,
	.write		= vfio_fsl_mc_write,
	.mmap		= vfio_fsl_mc_mmap,
};

static int vfio_fsl_mc_bus_notifier(struct notifier_block *nb,
				    unsigned long action, void *data)
{
	struct vfio_fsl_mc_device *vdev = container_of(nb,
					struct vfio_fsl_mc_device, nb);
	struct device *dev = data;
	struct fsl_mc_device *mc_dev = to_fsl_mc_device(dev);
	struct fsl_mc_device *mc_cont = to_fsl_mc_device(mc_dev->dev.parent);

	if (action == BUS_NOTIFY_ADD_DEVICE &&
	    vdev->mc_dev == mc_cont) {
		mc_dev->driver_override = kasprintf(GFP_KERNEL, "%s",
						    vfio_fsl_mc_ops.name);
		if (!mc_dev->driver_override)
			dev_warn(dev, "VFIO_FSL_MC: Setting driver override for device in dprc %s failed\n",
				 dev_name(&mc_cont->dev));
		else
			dev_info(dev, "VFIO_FSL_MC: Setting driver override for device in dprc %s\n",
				 dev_name(&mc_cont->dev));
	} else if (action == BUS_NOTIFY_BOUND_DRIVER &&
		vdev->mc_dev == mc_cont) {
		struct fsl_mc_driver *mc_drv = to_fsl_mc_driver(dev->driver);

		if (mc_drv && mc_drv != &vfio_fsl_mc_driver)
			dev_warn(dev, "VFIO_FSL_MC: Object %s bound to driver %s while DPRC bound to vfio-fsl-mc\n",
				 dev_name(dev), mc_drv->driver.name);
	}

	return 0;
}

static int vfio_fsl_mc_init_device(struct vfio_fsl_mc_device *vdev)
{
	struct fsl_mc_device *mc_dev = vdev->mc_dev;
	int ret;

	/* Non-dprc devices share mc_io from parent */
	if (!is_fsl_mc_bus_dprc(mc_dev)) {
		struct fsl_mc_device *mc_cont = to_fsl_mc_device(mc_dev->dev.parent);

		mc_dev->mc_io = mc_cont->mc_io;
		return 0;
	}

	vdev->nb.notifier_call = vfio_fsl_mc_bus_notifier;
	ret = bus_register_notifier(&fsl_mc_bus_type, &vdev->nb);
	if (ret)
		return ret;

	/* open DPRC, allocate a MC portal */
	ret = dprc_setup(mc_dev);
	if (ret) {
		dev_err(&mc_dev->dev, "VFIO_FSL_MC: Failed to setup DPRC (%d)\n", ret);
		goto out_nc_unreg;
	}

	ret = dprc_scan_container(mc_dev, false);
	if (ret) {
		dev_err(&mc_dev->dev, "VFIO_FSL_MC: Container scanning failed (%d)\n", ret);
		goto out_dprc_cleanup;
	}

	return 0;

out_dprc_cleanup:
	dprc_remove_devices(mc_dev, NULL, 0);
	dprc_cleanup(mc_dev);
out_nc_unreg:
	bus_unregister_notifier(&fsl_mc_bus_type, &vdev->nb);
	vdev->nb.notifier_call = NULL;

	return ret;
}

static int vfio_fsl_mc_probe(struct fsl_mc_device *mc_dev)
{
	struct iommu_group *group;
	struct vfio_fsl_mc_device *vdev;
	struct device *dev = &mc_dev->dev;
	int ret;

	group = vfio_iommu_group_get(dev);
	if (!group) {
		dev_err(dev, "VFIO_FSL_MC: No IOMMU group\n");
		return -EINVAL;
	}

	vdev = devm_kzalloc(dev, sizeof(*vdev), GFP_KERNEL);
	if (!vdev) {
		ret = -ENOMEM;
		goto out_group_put;
	}

	vdev->mc_dev = mc_dev;

	ret = vfio_add_group_dev(dev, &vfio_fsl_mc_ops, vdev);
	if (ret) {
		dev_err(dev, "VFIO_FSL_MC: Failed to add to vfio group\n");
		goto out_group_put;
	}

	ret = vfio_fsl_mc_init_device(vdev);
	if (ret)
		goto out_group_dev;

	mutex_init(&vdev->driver_lock);

	return 0;

out_group_dev:
	vfio_del_group_dev(dev);
out_group_put:
	vfio_iommu_group_put(group, dev);
	return ret;
}

static int vfio_fsl_mc_remove(struct fsl_mc_device *mc_dev)
{
	struct vfio_fsl_mc_device *vdev;
	struct device *dev = &mc_dev->dev;

	vdev = vfio_del_group_dev(dev);
	if (!vdev)
		return -EINVAL;

	mutex_destroy(&vdev->driver_lock);

	if (is_fsl_mc_bus_dprc(mc_dev)) {
		dprc_remove_devices(mc_dev, NULL, 0);
		dprc_cleanup(mc_dev);
	}

	if (vdev->nb.notifier_call)
		bus_unregister_notifier(&fsl_mc_bus_type, &vdev->nb);

	vfio_iommu_group_put(mc_dev->dev.iommu_group, dev);

	return 0;
}

static struct fsl_mc_driver vfio_fsl_mc_driver = {
	.probe		= vfio_fsl_mc_probe,
	.remove		= vfio_fsl_mc_remove,
	.driver	= {
		.name	= "vfio-fsl-mc",
		.owner	= THIS_MODULE,
	},
};

static int __init vfio_fsl_mc_driver_init(void)
{
	return fsl_mc_driver_register(&vfio_fsl_mc_driver);
}

static void __exit vfio_fsl_mc_driver_exit(void)
{
	fsl_mc_driver_unregister(&vfio_fsl_mc_driver);
}

module_init(vfio_fsl_mc_driver_init);
module_exit(vfio_fsl_mc_driver_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("VFIO for FSL-MC devices - User Level meta-driver");
