// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022-2023, Advanced Micro Devices, Inc.
 */

#include <linux/vfio.h>
#include <linux/cdx/cdx_bus.h>

#include "private.h"

static int vfio_cdx_open_device(struct vfio_device *core_vdev)
{
	struct vfio_cdx_device *vdev =
		container_of(core_vdev, struct vfio_cdx_device, vdev);
	struct cdx_device *cdx_dev = to_cdx_device(core_vdev->dev);
	int count = cdx_dev->res_count;
	int i, ret;

	vdev->regions = kcalloc(count, sizeof(struct vfio_cdx_region),
				GFP_KERNEL_ACCOUNT);
	if (!vdev->regions)
		return -ENOMEM;

	for (i = 0; i < count; i++) {
		struct resource *res = &cdx_dev->res[i];

		vdev->regions[i].addr = res->start;
		vdev->regions[i].size = resource_size(res);
		vdev->regions[i].type = res->flags;
		/*
		 * Only regions addressed with PAGE granularity may be
		 * MMAP'ed securely.
		 */
		if (!(vdev->regions[i].addr & ~PAGE_MASK) &&
		    !(vdev->regions[i].size & ~PAGE_MASK))
			vdev->regions[i].flags |=
					VFIO_REGION_INFO_FLAG_MMAP;
		vdev->regions[i].flags |= VFIO_REGION_INFO_FLAG_READ;
		if (!(cdx_dev->res[i].flags & IORESOURCE_READONLY))
			vdev->regions[i].flags |= VFIO_REGION_INFO_FLAG_WRITE;
	}
	ret = cdx_dev_reset(core_vdev->dev);
	if (ret) {
		kfree(vdev->regions);
		vdev->regions = NULL;
		return ret;
	}
	ret = cdx_clear_master(cdx_dev);
	if (ret)
		vdev->flags &= ~BME_SUPPORT;
	else
		vdev->flags |= BME_SUPPORT;

	return 0;
}

static void vfio_cdx_close_device(struct vfio_device *core_vdev)
{
	struct vfio_cdx_device *vdev =
		container_of(core_vdev, struct vfio_cdx_device, vdev);

	kfree(vdev->regions);
	cdx_dev_reset(core_vdev->dev);
	vfio_cdx_irqs_cleanup(vdev);
}

static int vfio_cdx_bm_ctrl(struct vfio_device *core_vdev, u32 flags,
			    void __user *arg, size_t argsz)
{
	size_t minsz =
		offsetofend(struct vfio_device_feature_bus_master, op);
	struct vfio_cdx_device *vdev =
		container_of(core_vdev, struct vfio_cdx_device, vdev);
	struct cdx_device *cdx_dev = to_cdx_device(core_vdev->dev);
	struct vfio_device_feature_bus_master ops;
	int ret;

	if (!(vdev->flags & BME_SUPPORT))
		return -ENOTTY;

	ret = vfio_check_feature(flags, argsz, VFIO_DEVICE_FEATURE_SET,
				 sizeof(ops));
	if (ret != 1)
		return ret;

	if (copy_from_user(&ops, arg, minsz))
		return -EFAULT;

	switch (ops.op) {
	case VFIO_DEVICE_FEATURE_CLEAR_MASTER:
		return cdx_clear_master(cdx_dev);
	case VFIO_DEVICE_FEATURE_SET_MASTER:
		return cdx_set_master(cdx_dev);
	default:
		return -EINVAL;
	}
}

static int vfio_cdx_ioctl_feature(struct vfio_device *device, u32 flags,
				  void __user *arg, size_t argsz)
{
	switch (flags & VFIO_DEVICE_FEATURE_MASK) {
	case VFIO_DEVICE_FEATURE_BUS_MASTER:
		return vfio_cdx_bm_ctrl(device, flags, arg, argsz);
	default:
		return -ENOTTY;
	}
}

static int vfio_cdx_ioctl_get_info(struct vfio_cdx_device *vdev,
				   struct vfio_device_info __user *arg)
{
	unsigned long minsz = offsetofend(struct vfio_device_info, num_irqs);
	struct cdx_device *cdx_dev = to_cdx_device(vdev->vdev.dev);
	struct vfio_device_info info;

	if (copy_from_user(&info, arg, minsz))
		return -EFAULT;

	if (info.argsz < minsz)
		return -EINVAL;

	info.flags = VFIO_DEVICE_FLAGS_CDX;
	info.flags |= VFIO_DEVICE_FLAGS_RESET;

	info.num_regions = cdx_dev->res_count;
	info.num_irqs = cdx_dev->num_msi ? 1 : 0;

	return copy_to_user(arg, &info, minsz) ? -EFAULT : 0;
}

static int vfio_cdx_ioctl_get_region_info(struct vfio_cdx_device *vdev,
					  struct vfio_region_info __user *arg)
{
	unsigned long minsz = offsetofend(struct vfio_region_info, offset);
	struct cdx_device *cdx_dev = to_cdx_device(vdev->vdev.dev);
	struct vfio_region_info info;

	if (copy_from_user(&info, arg, minsz))
		return -EFAULT;

	if (info.argsz < minsz)
		return -EINVAL;

	if (info.index >= cdx_dev->res_count)
		return -EINVAL;

	/* map offset to the physical address */
	info.offset = vfio_cdx_index_to_offset(info.index);
	info.size = vdev->regions[info.index].size;
	info.flags = vdev->regions[info.index].flags;

	return copy_to_user(arg, &info, minsz) ? -EFAULT : 0;
}

static int vfio_cdx_ioctl_get_irq_info(struct vfio_cdx_device *vdev,
				       struct vfio_irq_info __user *arg)
{
	unsigned long minsz = offsetofend(struct vfio_irq_info, count);
	struct cdx_device *cdx_dev = to_cdx_device(vdev->vdev.dev);
	struct vfio_irq_info info;

	if (copy_from_user(&info, arg, minsz))
		return -EFAULT;

	if (info.argsz < minsz)
		return -EINVAL;

	if (info.index >= 1)
		return -EINVAL;

	if (!cdx_dev->num_msi)
		return -EINVAL;

	info.flags = VFIO_IRQ_INFO_EVENTFD | VFIO_IRQ_INFO_NORESIZE;
	info.count = cdx_dev->num_msi;

	return copy_to_user(arg, &info, minsz) ? -EFAULT : 0;
}

static int vfio_cdx_ioctl_set_irqs(struct vfio_cdx_device *vdev,
				   struct vfio_irq_set __user *arg)
{
	unsigned long minsz = offsetofend(struct vfio_irq_set, count);
	struct cdx_device *cdx_dev = to_cdx_device(vdev->vdev.dev);
	struct vfio_irq_set hdr;
	size_t data_size = 0;
	u8 *data = NULL;
	int ret = 0;

	if (copy_from_user(&hdr, arg, minsz))
		return -EFAULT;

	ret = vfio_set_irqs_validate_and_prepare(&hdr, cdx_dev->num_msi,
						 1, &data_size);
	if (ret)
		return ret;

	if (data_size) {
		data = memdup_user(arg->data, data_size);
		if (IS_ERR(data))
			return PTR_ERR(data);
	}

	ret = vfio_cdx_set_irqs_ioctl(vdev, hdr.flags, hdr.index,
				      hdr.start, hdr.count, data);
	kfree(data);

	return ret;
}

static long vfio_cdx_ioctl(struct vfio_device *core_vdev,
			   unsigned int cmd, unsigned long arg)
{
	struct vfio_cdx_device *vdev =
		container_of(core_vdev, struct vfio_cdx_device, vdev);
	void __user *uarg = (void __user *)arg;

	switch (cmd) {
	case VFIO_DEVICE_GET_INFO:
		return vfio_cdx_ioctl_get_info(vdev, uarg);
	case VFIO_DEVICE_GET_REGION_INFO:
		return vfio_cdx_ioctl_get_region_info(vdev, uarg);
	case VFIO_DEVICE_GET_IRQ_INFO:
		return vfio_cdx_ioctl_get_irq_info(vdev, uarg);
	case VFIO_DEVICE_SET_IRQS:
		return vfio_cdx_ioctl_set_irqs(vdev, uarg);
	case VFIO_DEVICE_RESET:
		return cdx_dev_reset(core_vdev->dev);
	default:
		return -ENOTTY;
	}
}

static int vfio_cdx_mmap_mmio(struct vfio_cdx_region region,
			      struct vm_area_struct *vma)
{
	u64 size = vma->vm_end - vma->vm_start;
	u64 pgoff, base;

	pgoff = vma->vm_pgoff &
		((1U << (VFIO_CDX_OFFSET_SHIFT - PAGE_SHIFT)) - 1);
	base = pgoff << PAGE_SHIFT;

	if (base + size > region.size)
		return -EINVAL;

	vma->vm_pgoff = (region.addr >> PAGE_SHIFT) + pgoff;
	vma->vm_page_prot = pgprot_device(vma->vm_page_prot);

	return io_remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
				  size, vma->vm_page_prot);
}

static int vfio_cdx_mmap(struct vfio_device *core_vdev,
			 struct vm_area_struct *vma)
{
	struct vfio_cdx_device *vdev =
		container_of(core_vdev, struct vfio_cdx_device, vdev);
	struct cdx_device *cdx_dev = to_cdx_device(core_vdev->dev);
	unsigned int index;

	index = vma->vm_pgoff >> (VFIO_CDX_OFFSET_SHIFT - PAGE_SHIFT);

	if (index >= cdx_dev->res_count)
		return -EINVAL;

	if (!(vdev->regions[index].flags & VFIO_REGION_INFO_FLAG_MMAP))
		return -EINVAL;

	if (!(vdev->regions[index].flags & VFIO_REGION_INFO_FLAG_READ) &&
	    (vma->vm_flags & VM_READ))
		return -EPERM;

	if (!(vdev->regions[index].flags & VFIO_REGION_INFO_FLAG_WRITE) &&
	    (vma->vm_flags & VM_WRITE))
		return -EPERM;

	return vfio_cdx_mmap_mmio(vdev->regions[index], vma);
}

static const struct vfio_device_ops vfio_cdx_ops = {
	.name		= "vfio-cdx",
	.open_device	= vfio_cdx_open_device,
	.close_device	= vfio_cdx_close_device,
	.ioctl		= vfio_cdx_ioctl,
	.device_feature = vfio_cdx_ioctl_feature,
	.mmap		= vfio_cdx_mmap,
	.bind_iommufd	= vfio_iommufd_physical_bind,
	.unbind_iommufd	= vfio_iommufd_physical_unbind,
	.attach_ioas	= vfio_iommufd_physical_attach_ioas,
};

static int vfio_cdx_probe(struct cdx_device *cdx_dev)
{
	struct vfio_cdx_device *vdev;
	struct device *dev = &cdx_dev->dev;
	int ret;

	vdev = vfio_alloc_device(vfio_cdx_device, vdev, dev,
				 &vfio_cdx_ops);
	if (IS_ERR(vdev))
		return PTR_ERR(vdev);

	ret = vfio_register_group_dev(&vdev->vdev);
	if (ret)
		goto out_uninit;

	dev_set_drvdata(dev, vdev);
	return 0;

out_uninit:
	vfio_put_device(&vdev->vdev);
	return ret;
}

static int vfio_cdx_remove(struct cdx_device *cdx_dev)
{
	struct device *dev = &cdx_dev->dev;
	struct vfio_cdx_device *vdev = dev_get_drvdata(dev);

	vfio_unregister_group_dev(&vdev->vdev);
	vfio_put_device(&vdev->vdev);

	return 0;
}

static const struct cdx_device_id vfio_cdx_table[] = {
	{ CDX_DEVICE_DRIVER_OVERRIDE(CDX_ANY_ID, CDX_ANY_ID,
				     CDX_ID_F_VFIO_DRIVER_OVERRIDE) }, /* match all by default */
	{}
};

MODULE_DEVICE_TABLE(cdx, vfio_cdx_table);

static struct cdx_driver vfio_cdx_driver = {
	.probe		= vfio_cdx_probe,
	.remove		= vfio_cdx_remove,
	.match_id_table	= vfio_cdx_table,
	.driver	= {
		.name	= "vfio-cdx",
	},
	.driver_managed_dma = true,
};

module_driver(vfio_cdx_driver, cdx_driver_register, cdx_driver_unregister);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("VFIO for CDX devices - User Level meta-driver");
MODULE_IMPORT_NS("CDX_BUS");
