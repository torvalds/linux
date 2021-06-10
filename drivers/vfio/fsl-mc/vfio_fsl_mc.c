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
#include <linux/delay.h>
#include <linux/io-64-nonatomic-hi-lo.h>

#include "vfio_fsl_mc_private.h"

static struct fsl_mc_driver vfio_fsl_mc_driver;

static DEFINE_MUTEX(reflck_lock);

static void vfio_fsl_mc_reflck_get(struct vfio_fsl_mc_reflck *reflck)
{
	kref_get(&reflck->kref);
}

static void vfio_fsl_mc_reflck_release(struct kref *kref)
{
	struct vfio_fsl_mc_reflck *reflck = container_of(kref,
						      struct vfio_fsl_mc_reflck,
						      kref);

	mutex_destroy(&reflck->lock);
	kfree(reflck);
	mutex_unlock(&reflck_lock);
}

static void vfio_fsl_mc_reflck_put(struct vfio_fsl_mc_reflck *reflck)
{
	kref_put_mutex(&reflck->kref, vfio_fsl_mc_reflck_release, &reflck_lock);
}

static struct vfio_fsl_mc_reflck *vfio_fsl_mc_reflck_alloc(void)
{
	struct vfio_fsl_mc_reflck *reflck;

	reflck = kzalloc(sizeof(*reflck), GFP_KERNEL);
	if (!reflck)
		return ERR_PTR(-ENOMEM);

	kref_init(&reflck->kref);
	mutex_init(&reflck->lock);

	return reflck;
}

static int vfio_fsl_mc_reflck_attach(struct vfio_fsl_mc_device *vdev)
{
	int ret = 0;

	mutex_lock(&reflck_lock);
	if (is_fsl_mc_bus_dprc(vdev->mc_dev)) {
		vdev->reflck = vfio_fsl_mc_reflck_alloc();
		ret = PTR_ERR_OR_ZERO(vdev->reflck);
	} else {
		struct device *mc_cont_dev = vdev->mc_dev->dev.parent;
		struct vfio_device *device;
		struct vfio_fsl_mc_device *cont_vdev;

		device = vfio_device_get_from_dev(mc_cont_dev);
		if (!device) {
			ret = -ENODEV;
			goto unlock;
		}

		cont_vdev = vfio_device_data(device);
		if (!cont_vdev || !cont_vdev->reflck) {
			vfio_device_put(device);
			ret = -ENODEV;
			goto unlock;
		}
		vfio_fsl_mc_reflck_get(cont_vdev->reflck);
		vdev->reflck = cont_vdev->reflck;
		vfio_device_put(device);
	}

unlock:
	mutex_unlock(&reflck_lock);
	return ret;
}

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
		vdev->regions[i].flags |= VFIO_REGION_INFO_FLAG_READ;
		if (!(mc_dev->regions[i].flags & IORESOURCE_READONLY))
			vdev->regions[i].flags |= VFIO_REGION_INFO_FLAG_WRITE;
	}

	return 0;
}

static void vfio_fsl_mc_regions_cleanup(struct vfio_fsl_mc_device *vdev)
{
	struct fsl_mc_device *mc_dev = vdev->mc_dev;
	int i;

	for (i = 0; i < mc_dev->obj_desc.region_count; i++)
		iounmap(vdev->regions[i].ioaddr);
	kfree(vdev->regions);
}

static int vfio_fsl_mc_open(void *device_data)
{
	struct vfio_fsl_mc_device *vdev = device_data;
	int ret;

	if (!try_module_get(THIS_MODULE))
		return -ENODEV;

	mutex_lock(&vdev->reflck->lock);
	if (!vdev->refcnt) {
		ret = vfio_fsl_mc_regions_init(vdev);
		if (ret)
			goto err_reg_init;
	}
	vdev->refcnt++;

	mutex_unlock(&vdev->reflck->lock);

	return 0;

err_reg_init:
	mutex_unlock(&vdev->reflck->lock);
	module_put(THIS_MODULE);
	return ret;
}

static void vfio_fsl_mc_release(void *device_data)
{
	struct vfio_fsl_mc_device *vdev = device_data;
	int ret;

	mutex_lock(&vdev->reflck->lock);

	if (!(--vdev->refcnt)) {
		struct fsl_mc_device *mc_dev = vdev->mc_dev;
		struct device *cont_dev = fsl_mc_cont_dev(&mc_dev->dev);
		struct fsl_mc_device *mc_cont = to_fsl_mc_device(cont_dev);

		vfio_fsl_mc_regions_cleanup(vdev);

		/* reset the device before cleaning up the interrupts */
		ret = dprc_reset_container(mc_cont->mc_io, 0,
		      mc_cont->mc_handle,
			  mc_cont->obj_desc.id,
			  DPRC_RESET_OPTION_NON_RECURSIVE);

		if (ret) {
			dev_warn(&mc_cont->dev, "VFIO_FLS_MC: reset device has failed (%d)\n",
				 ret);
			WARN_ON(1);
		}

		vfio_fsl_mc_irqs_cleanup(vdev);

		fsl_mc_cleanup_irq_pool(mc_cont);
	}

	mutex_unlock(&vdev->reflck->lock);

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

		if (is_fsl_mc_bus_dprc(mc_dev))
			info.flags |= VFIO_DEVICE_FLAGS_RESET;

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

		if (copy_to_user((void __user *)arg, &info, minsz))
			return -EFAULT;
		return 0;
	}
	case VFIO_DEVICE_GET_IRQ_INFO:
	{
		struct vfio_irq_info info;

		minsz = offsetofend(struct vfio_irq_info, count);
		if (copy_from_user(&info, (void __user *)arg, minsz))
			return -EFAULT;

		if (info.argsz < minsz)
			return -EINVAL;

		if (info.index >= mc_dev->obj_desc.irq_count)
			return -EINVAL;

		info.flags = VFIO_IRQ_INFO_EVENTFD;
		info.count = 1;

		if (copy_to_user((void __user *)arg, &info, minsz))
			return -EFAULT;
		return 0;
	}
	case VFIO_DEVICE_SET_IRQS:
	{
		struct vfio_irq_set hdr;
		u8 *data = NULL;
		int ret = 0;
		size_t data_size = 0;

		minsz = offsetofend(struct vfio_irq_set, count);

		if (copy_from_user(&hdr, (void __user *)arg, minsz))
			return -EFAULT;

		ret = vfio_set_irqs_validate_and_prepare(&hdr, mc_dev->obj_desc.irq_count,
					mc_dev->obj_desc.irq_count, &data_size);
		if (ret)
			return ret;

		if (data_size) {
			data = memdup_user((void __user *)(arg + minsz),
				   data_size);
			if (IS_ERR(data))
				return PTR_ERR(data);
		}

		mutex_lock(&vdev->igate);
		ret = vfio_fsl_mc_set_irqs_ioctl(vdev, hdr.flags,
						 hdr.index, hdr.start,
						 hdr.count, data);
		mutex_unlock(&vdev->igate);
		kfree(data);

		return ret;
	}
	case VFIO_DEVICE_RESET:
	{
		int ret;
		struct fsl_mc_device *mc_dev = vdev->mc_dev;

		/* reset is supported only for the DPRC */
		if (!is_fsl_mc_bus_dprc(mc_dev))
			return -ENOTTY;

		ret = dprc_reset_container(mc_dev->mc_io, 0,
					   mc_dev->mc_handle,
					   mc_dev->obj_desc.id,
					   DPRC_RESET_OPTION_NON_RECURSIVE);
		return ret;

	}
	default:
		return -ENOTTY;
	}
}

static ssize_t vfio_fsl_mc_read(void *device_data, char __user *buf,
				size_t count, loff_t *ppos)
{
	struct vfio_fsl_mc_device *vdev = device_data;
	unsigned int index = VFIO_FSL_MC_OFFSET_TO_INDEX(*ppos);
	loff_t off = *ppos & VFIO_FSL_MC_OFFSET_MASK;
	struct fsl_mc_device *mc_dev = vdev->mc_dev;
	struct vfio_fsl_mc_region *region;
	u64 data[8];
	int i;

	if (index >= mc_dev->obj_desc.region_count)
		return -EINVAL;

	region = &vdev->regions[index];

	if (!(region->flags & VFIO_REGION_INFO_FLAG_READ))
		return -EINVAL;

	if (!region->ioaddr) {
		region->ioaddr = ioremap(region->addr, region->size);
		if (!region->ioaddr)
			return -ENOMEM;
	}

	if (count != 64 || off != 0)
		return -EINVAL;

	for (i = 7; i >= 0; i--)
		data[i] = readq(region->ioaddr + i * sizeof(uint64_t));

	if (copy_to_user(buf, data, 64))
		return -EFAULT;

	return count;
}

#define MC_CMD_COMPLETION_TIMEOUT_MS    5000
#define MC_CMD_COMPLETION_POLLING_MAX_SLEEP_USECS    500

static int vfio_fsl_mc_send_command(void __iomem *ioaddr, uint64_t *cmd_data)
{
	int i;
	enum mc_cmd_status status;
	unsigned long timeout_usecs = MC_CMD_COMPLETION_TIMEOUT_MS * 1000;

	/* Write at command parameter into portal */
	for (i = 7; i >= 1; i--)
		writeq_relaxed(cmd_data[i], ioaddr + i * sizeof(uint64_t));

	/* Write command header in the end */
	writeq(cmd_data[0], ioaddr);

	/* Wait for response before returning to user-space
	 * This can be optimized in future to even prepare response
	 * before returning to user-space and avoid read ioctl.
	 */
	for (;;) {
		u64 header;
		struct mc_cmd_header *resp_hdr;

		header = cpu_to_le64(readq_relaxed(ioaddr));

		resp_hdr = (struct mc_cmd_header *)&header;
		status = (enum mc_cmd_status)resp_hdr->status;
		if (status != MC_CMD_STATUS_READY)
			break;

		udelay(MC_CMD_COMPLETION_POLLING_MAX_SLEEP_USECS);
		timeout_usecs -= MC_CMD_COMPLETION_POLLING_MAX_SLEEP_USECS;
		if (timeout_usecs == 0)
			return -ETIMEDOUT;
	}

	return 0;
}

static ssize_t vfio_fsl_mc_write(void *device_data, const char __user *buf,
				 size_t count, loff_t *ppos)
{
	struct vfio_fsl_mc_device *vdev = device_data;
	unsigned int index = VFIO_FSL_MC_OFFSET_TO_INDEX(*ppos);
	loff_t off = *ppos & VFIO_FSL_MC_OFFSET_MASK;
	struct fsl_mc_device *mc_dev = vdev->mc_dev;
	struct vfio_fsl_mc_region *region;
	u64 data[8];
	int ret;

	if (index >= mc_dev->obj_desc.region_count)
		return -EINVAL;

	region = &vdev->regions[index];

	if (!(region->flags & VFIO_REGION_INFO_FLAG_WRITE))
		return -EINVAL;

	if (!region->ioaddr) {
		region->ioaddr = ioremap(region->addr, region->size);
		if (!region->ioaddr)
			return -ENOMEM;
	}

	if (count != 64 || off != 0)
		return -EINVAL;

	if (copy_from_user(&data, buf, 64))
		return -EFAULT;

	ret = vfio_fsl_mc_send_command(region->ioaddr, data);
	if (ret)
		return ret;

	return count;

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
	unsigned int index;

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
	return 0;

out_nc_unreg:
	bus_unregister_notifier(&fsl_mc_bus_type, &vdev->nb);
	return ret;
}

static int vfio_fsl_mc_scan_container(struct fsl_mc_device *mc_dev)
{
	int ret;

	/* non dprc devices do not scan for other devices */
	if (!is_fsl_mc_bus_dprc(mc_dev))
		return 0;
	ret = dprc_scan_container(mc_dev, false);
	if (ret) {
		dev_err(&mc_dev->dev,
			"VFIO_FSL_MC: Container scanning failed (%d)\n", ret);
		dprc_remove_devices(mc_dev, NULL, 0);
		return ret;
	}
	return 0;
}

static void vfio_fsl_uninit_device(struct vfio_fsl_mc_device *vdev)
{
	struct fsl_mc_device *mc_dev = vdev->mc_dev;

	if (!is_fsl_mc_bus_dprc(mc_dev))
		return;

	dprc_cleanup(mc_dev);
	bus_unregister_notifier(&fsl_mc_bus_type, &vdev->nb);
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
	mutex_init(&vdev->igate);

	ret = vfio_fsl_mc_reflck_attach(vdev);
	if (ret)
		goto out_group_put;

	ret = vfio_fsl_mc_init_device(vdev);
	if (ret)
		goto out_reflck;

	ret = vfio_add_group_dev(dev, &vfio_fsl_mc_ops, vdev);
	if (ret) {
		dev_err(dev, "VFIO_FSL_MC: Failed to add to vfio group\n");
		goto out_device;
	}

	/*
	 * This triggers recursion into vfio_fsl_mc_probe() on another device
	 * and the vfio_fsl_mc_reflck_attach() must succeed, which relies on the
	 * vfio_add_group_dev() above. It has no impact on this vdev, so it is
	 * safe to be after the vfio device is made live.
	 */
	ret = vfio_fsl_mc_scan_container(mc_dev);
	if (ret)
		goto out_group_dev;
	return 0;

out_group_dev:
	vfio_del_group_dev(dev);
out_device:
	vfio_fsl_uninit_device(vdev);
out_reflck:
	vfio_fsl_mc_reflck_put(vdev->reflck);
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

	mutex_destroy(&vdev->igate);

	dprc_remove_devices(mc_dev, NULL, 0);
	vfio_fsl_uninit_device(vdev);
	vfio_fsl_mc_reflck_put(vdev->reflck);

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
