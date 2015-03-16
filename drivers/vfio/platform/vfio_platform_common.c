/*
 * Copyright (C) 2013 - Virtual Open Systems
 * Author: Antonios Motakis <a.motakis@virtualopensystems.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/device.h>
#include <linux/iommu.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/vfio.h>

#include "vfio_platform_private.h"

static DEFINE_MUTEX(driver_lock);

static int vfio_platform_regions_init(struct vfio_platform_device *vdev)
{
	int cnt = 0, i;

	while (vdev->get_resource(vdev, cnt))
		cnt++;

	vdev->regions = kcalloc(cnt, sizeof(struct vfio_platform_region),
				GFP_KERNEL);
	if (!vdev->regions)
		return -ENOMEM;

	for (i = 0; i < cnt;  i++) {
		struct resource *res =
			vdev->get_resource(vdev, i);

		if (!res)
			goto err;

		vdev->regions[i].addr = res->start;
		vdev->regions[i].size = resource_size(res);
		vdev->regions[i].flags = 0;

		switch (resource_type(res)) {
		case IORESOURCE_MEM:
			vdev->regions[i].type = VFIO_PLATFORM_REGION_TYPE_MMIO;
			vdev->regions[i].flags |= VFIO_REGION_INFO_FLAG_READ;
			if (!(res->flags & IORESOURCE_READONLY))
				vdev->regions[i].flags |=
					VFIO_REGION_INFO_FLAG_WRITE;

			/*
			 * Only regions addressed with PAGE granularity may be
			 * MMAPed securely.
			 */
			if (!(vdev->regions[i].addr & ~PAGE_MASK) &&
					!(vdev->regions[i].size & ~PAGE_MASK))
				vdev->regions[i].flags |=
					VFIO_REGION_INFO_FLAG_MMAP;

			break;
		case IORESOURCE_IO:
			vdev->regions[i].type = VFIO_PLATFORM_REGION_TYPE_PIO;
			break;
		default:
			goto err;
		}
	}

	vdev->num_regions = cnt;

	return 0;
err:
	kfree(vdev->regions);
	return -EINVAL;
}

static void vfio_platform_regions_cleanup(struct vfio_platform_device *vdev)
{
	int i;

	for (i = 0; i < vdev->num_regions; i++)
		iounmap(vdev->regions[i].ioaddr);

	vdev->num_regions = 0;
	kfree(vdev->regions);
}

static void vfio_platform_release(void *device_data)
{
	struct vfio_platform_device *vdev = device_data;

	mutex_lock(&driver_lock);

	if (!(--vdev->refcnt)) {
		vfio_platform_regions_cleanup(vdev);
	}

	mutex_unlock(&driver_lock);

	module_put(THIS_MODULE);
}

static int vfio_platform_open(void *device_data)
{
	struct vfio_platform_device *vdev = device_data;
	int ret;

	if (!try_module_get(THIS_MODULE))
		return -ENODEV;

	mutex_lock(&driver_lock);

	if (!vdev->refcnt) {
		ret = vfio_platform_regions_init(vdev);
		if (ret)
			goto err_reg;
	}

	vdev->refcnt++;

	mutex_unlock(&driver_lock);
	return 0;

err_reg:
	mutex_unlock(&driver_lock);
	module_put(THIS_MODULE);
	return ret;
}

static long vfio_platform_ioctl(void *device_data,
				unsigned int cmd, unsigned long arg)
{
	struct vfio_platform_device *vdev = device_data;
	unsigned long minsz;

	if (cmd == VFIO_DEVICE_GET_INFO) {
		struct vfio_device_info info;

		minsz = offsetofend(struct vfio_device_info, num_irqs);

		if (copy_from_user(&info, (void __user *)arg, minsz))
			return -EFAULT;

		if (info.argsz < minsz)
			return -EINVAL;

		info.flags = vdev->flags;
		info.num_regions = vdev->num_regions;
		info.num_irqs = 0;

		return copy_to_user((void __user *)arg, &info, minsz);

	} else if (cmd == VFIO_DEVICE_GET_REGION_INFO) {
		struct vfio_region_info info;

		minsz = offsetofend(struct vfio_region_info, offset);

		if (copy_from_user(&info, (void __user *)arg, minsz))
			return -EFAULT;

		if (info.argsz < minsz)
			return -EINVAL;

		if (info.index >= vdev->num_regions)
			return -EINVAL;

		/* map offset to the physical address  */
		info.offset = VFIO_PLATFORM_INDEX_TO_OFFSET(info.index);
		info.size = vdev->regions[info.index].size;
		info.flags = vdev->regions[info.index].flags;

		return copy_to_user((void __user *)arg, &info, minsz);

	} else if (cmd == VFIO_DEVICE_GET_IRQ_INFO)
		return -EINVAL;

	else if (cmd == VFIO_DEVICE_SET_IRQS)
		return -EINVAL;

	else if (cmd == VFIO_DEVICE_RESET)
		return -EINVAL;

	return -ENOTTY;
}

static ssize_t vfio_platform_read_mmio(struct vfio_platform_region reg,
				       char __user *buf, size_t count,
				       loff_t off)
{
	unsigned int done = 0;

	if (!reg.ioaddr) {
		reg.ioaddr =
			ioremap_nocache(reg.addr, reg.size);

		if (!reg.ioaddr)
			return -ENOMEM;
	}

	while (count) {
		size_t filled;

		if (count >= 4 && !(off % 4)) {
			u32 val;

			val = ioread32(reg.ioaddr + off);
			if (copy_to_user(buf, &val, 4))
				goto err;

			filled = 4;
		} else if (count >= 2 && !(off % 2)) {
			u16 val;

			val = ioread16(reg.ioaddr + off);
			if (copy_to_user(buf, &val, 2))
				goto err;

			filled = 2;
		} else {
			u8 val;

			val = ioread8(reg.ioaddr + off);
			if (copy_to_user(buf, &val, 1))
				goto err;

			filled = 1;
		}


		count -= filled;
		done += filled;
		off += filled;
		buf += filled;
	}

	return done;
err:
	return -EFAULT;
}

static ssize_t vfio_platform_read(void *device_data, char __user *buf,
				  size_t count, loff_t *ppos)
{
	struct vfio_platform_device *vdev = device_data;
	unsigned int index = VFIO_PLATFORM_OFFSET_TO_INDEX(*ppos);
	loff_t off = *ppos & VFIO_PLATFORM_OFFSET_MASK;

	if (index >= vdev->num_regions)
		return -EINVAL;

	if (!(vdev->regions[index].flags & VFIO_REGION_INFO_FLAG_READ))
		return -EINVAL;

	if (vdev->regions[index].type & VFIO_PLATFORM_REGION_TYPE_MMIO)
		return vfio_platform_read_mmio(vdev->regions[index],
							buf, count, off);
	else if (vdev->regions[index].type & VFIO_PLATFORM_REGION_TYPE_PIO)
		return -EINVAL; /* not implemented */

	return -EINVAL;
}

static ssize_t vfio_platform_write_mmio(struct vfio_platform_region reg,
					const char __user *buf, size_t count,
					loff_t off)
{
	unsigned int done = 0;

	if (!reg.ioaddr) {
		reg.ioaddr =
			ioremap_nocache(reg.addr, reg.size);

		if (!reg.ioaddr)
			return -ENOMEM;
	}

	while (count) {
		size_t filled;

		if (count >= 4 && !(off % 4)) {
			u32 val;

			if (copy_from_user(&val, buf, 4))
				goto err;
			iowrite32(val, reg.ioaddr + off);

			filled = 4;
		} else if (count >= 2 && !(off % 2)) {
			u16 val;

			if (copy_from_user(&val, buf, 2))
				goto err;
			iowrite16(val, reg.ioaddr + off);

			filled = 2;
		} else {
			u8 val;

			if (copy_from_user(&val, buf, 1))
				goto err;
			iowrite8(val, reg.ioaddr + off);

			filled = 1;
		}

		count -= filled;
		done += filled;
		off += filled;
		buf += filled;
	}

	return done;
err:
	return -EFAULT;
}

static ssize_t vfio_platform_write(void *device_data, const char __user *buf,
				   size_t count, loff_t *ppos)
{
	struct vfio_platform_device *vdev = device_data;
	unsigned int index = VFIO_PLATFORM_OFFSET_TO_INDEX(*ppos);
	loff_t off = *ppos & VFIO_PLATFORM_OFFSET_MASK;

	if (index >= vdev->num_regions)
		return -EINVAL;

	if (!(vdev->regions[index].flags & VFIO_REGION_INFO_FLAG_WRITE))
		return -EINVAL;

	if (vdev->regions[index].type & VFIO_PLATFORM_REGION_TYPE_MMIO)
		return vfio_platform_write_mmio(vdev->regions[index],
							buf, count, off);
	else if (vdev->regions[index].type & VFIO_PLATFORM_REGION_TYPE_PIO)
		return -EINVAL; /* not implemented */

	return -EINVAL;
}

static int vfio_platform_mmap_mmio(struct vfio_platform_region region,
				   struct vm_area_struct *vma)
{
	u64 req_len, pgoff, req_start;

	req_len = vma->vm_end - vma->vm_start;
	pgoff = vma->vm_pgoff &
		((1U << (VFIO_PLATFORM_OFFSET_SHIFT - PAGE_SHIFT)) - 1);
	req_start = pgoff << PAGE_SHIFT;

	if (region.size < PAGE_SIZE || req_start + req_len > region.size)
		return -EINVAL;

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	vma->vm_pgoff = (region.addr >> PAGE_SHIFT) + pgoff;

	return remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
			       req_len, vma->vm_page_prot);
}

static int vfio_platform_mmap(void *device_data, struct vm_area_struct *vma)
{
	struct vfio_platform_device *vdev = device_data;
	unsigned int index;

	index = vma->vm_pgoff >> (VFIO_PLATFORM_OFFSET_SHIFT - PAGE_SHIFT);

	if (vma->vm_end < vma->vm_start)
		return -EINVAL;
	if (!(vma->vm_flags & VM_SHARED))
		return -EINVAL;
	if (index >= vdev->num_regions)
		return -EINVAL;
	if (vma->vm_start & ~PAGE_MASK)
		return -EINVAL;
	if (vma->vm_end & ~PAGE_MASK)
		return -EINVAL;

	if (!(vdev->regions[index].flags & VFIO_REGION_INFO_FLAG_MMAP))
		return -EINVAL;

	if (!(vdev->regions[index].flags & VFIO_REGION_INFO_FLAG_READ)
			&& (vma->vm_flags & VM_READ))
		return -EINVAL;

	if (!(vdev->regions[index].flags & VFIO_REGION_INFO_FLAG_WRITE)
			&& (vma->vm_flags & VM_WRITE))
		return -EINVAL;

	vma->vm_private_data = vdev;

	if (vdev->regions[index].type & VFIO_PLATFORM_REGION_TYPE_MMIO)
		return vfio_platform_mmap_mmio(vdev->regions[index], vma);

	else if (vdev->regions[index].type & VFIO_PLATFORM_REGION_TYPE_PIO)
		return -EINVAL; /* not implemented */

	return -EINVAL;
}

static const struct vfio_device_ops vfio_platform_ops = {
	.name		= "vfio-platform",
	.open		= vfio_platform_open,
	.release	= vfio_platform_release,
	.ioctl		= vfio_platform_ioctl,
	.read		= vfio_platform_read,
	.write		= vfio_platform_write,
	.mmap		= vfio_platform_mmap,
};

int vfio_platform_probe_common(struct vfio_platform_device *vdev,
			       struct device *dev)
{
	struct iommu_group *group;
	int ret;

	if (!vdev)
		return -EINVAL;

	group = iommu_group_get(dev);
	if (!group) {
		pr_err("VFIO: No IOMMU group for device %s\n", vdev->name);
		return -EINVAL;
	}

	ret = vfio_add_group_dev(dev, &vfio_platform_ops, vdev);
	if (ret) {
		iommu_group_put(group);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(vfio_platform_probe_common);

struct vfio_platform_device *vfio_platform_remove_common(struct device *dev)
{
	struct vfio_platform_device *vdev;

	vdev = vfio_del_group_dev(dev);
	if (vdev)
		iommu_group_put(dev->iommu_group);

	return vdev;
}
EXPORT_SYMBOL_GPL(vfio_platform_remove_common);
