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

#define dev_fmt(fmt)	"VFIO: " fmt

#include <linux/device.h>
#include <linux/acpi.h>
#include <linux/iommu.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/vfio.h>

#include "vfio_platform_private.h"

#define DRIVER_VERSION  "0.10"
#define DRIVER_AUTHOR   "Antonios Motakis <a.motakis@virtualopensystems.com>"
#define DRIVER_DESC     "VFIO platform base module"

#define VFIO_PLATFORM_IS_ACPI(vdev) ((vdev)->acpihid != NULL)

static LIST_HEAD(reset_list);
static DEFINE_MUTEX(driver_lock);

static vfio_platform_reset_fn_t vfio_platform_lookup_reset(const char *compat,
					struct module **module)
{
	struct vfio_platform_reset_node *iter;
	vfio_platform_reset_fn_t reset_fn = NULL;

	mutex_lock(&driver_lock);
	list_for_each_entry(iter, &reset_list, link) {
		if (!strcmp(iter->compat, compat) &&
			try_module_get(iter->owner)) {
			*module = iter->owner;
			reset_fn = iter->of_reset;
			break;
		}
	}
	mutex_unlock(&driver_lock);
	return reset_fn;
}

static int vfio_platform_acpi_probe(struct vfio_platform_device *vdev,
				    struct device *dev)
{
	struct acpi_device *adev;

	if (acpi_disabled)
		return -ENOENT;

	adev = ACPI_COMPANION(dev);
	if (!adev) {
		dev_err(dev, "ACPI companion device not found for %s\n",
			vdev->name);
		return -ENODEV;
	}

#ifdef CONFIG_ACPI
	vdev->acpihid = acpi_device_hid(adev);
#endif
	return WARN_ON(!vdev->acpihid) ? -EINVAL : 0;
}

static int vfio_platform_acpi_call_reset(struct vfio_platform_device *vdev,
				  const char **extra_dbg)
{
#ifdef CONFIG_ACPI
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	struct device *dev = vdev->device;
	acpi_handle handle = ACPI_HANDLE(dev);
	acpi_status acpi_ret;

	acpi_ret = acpi_evaluate_object(handle, "_RST", NULL, &buffer);
	if (ACPI_FAILURE(acpi_ret)) {
		if (extra_dbg)
			*extra_dbg = acpi_format_exception(acpi_ret);
		return -EINVAL;
	}

	return 0;
#else
	return -ENOENT;
#endif
}

static bool vfio_platform_acpi_has_reset(struct vfio_platform_device *vdev)
{
#ifdef CONFIG_ACPI
	struct device *dev = vdev->device;
	acpi_handle handle = ACPI_HANDLE(dev);

	return acpi_has_method(handle, "_RST");
#else
	return false;
#endif
}

static bool vfio_platform_has_reset(struct vfio_platform_device *vdev)
{
	if (VFIO_PLATFORM_IS_ACPI(vdev))
		return vfio_platform_acpi_has_reset(vdev);

	return vdev->of_reset ? true : false;
}

static int vfio_platform_get_reset(struct vfio_platform_device *vdev)
{
	if (VFIO_PLATFORM_IS_ACPI(vdev))
		return vfio_platform_acpi_has_reset(vdev) ? 0 : -ENOENT;

	vdev->of_reset = vfio_platform_lookup_reset(vdev->compat,
						    &vdev->reset_module);
	if (!vdev->of_reset) {
		request_module("vfio-reset:%s", vdev->compat);
		vdev->of_reset = vfio_platform_lookup_reset(vdev->compat,
							&vdev->reset_module);
	}

	return vdev->of_reset ? 0 : -ENOENT;
}

static void vfio_platform_put_reset(struct vfio_platform_device *vdev)
{
	if (VFIO_PLATFORM_IS_ACPI(vdev))
		return;

	if (vdev->of_reset)
		module_put(vdev->reset_module);
}

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

static int vfio_platform_call_reset(struct vfio_platform_device *vdev,
				    const char **extra_dbg)
{
	if (VFIO_PLATFORM_IS_ACPI(vdev)) {
		dev_info(vdev->device, "reset\n");
		return vfio_platform_acpi_call_reset(vdev, extra_dbg);
	} else if (vdev->of_reset) {
		dev_info(vdev->device, "reset\n");
		return vdev->of_reset(vdev);
	}

	dev_warn(vdev->device, "no reset function found!\n");
	return -EINVAL;
}

static void vfio_platform_release(void *device_data)
{
	struct vfio_platform_device *vdev = device_data;

	mutex_lock(&driver_lock);

	if (!(--vdev->refcnt)) {
		const char *extra_dbg = NULL;
		int ret;

		ret = vfio_platform_call_reset(vdev, &extra_dbg);
		if (ret && vdev->reset_required) {
			dev_warn(vdev->device, "reset driver is required and reset call failed in release (%d) %s\n",
				 ret, extra_dbg ? extra_dbg : "");
			WARN_ON(1);
		}
		pm_runtime_put(vdev->device);
		vfio_platform_regions_cleanup(vdev);
		vfio_platform_irq_cleanup(vdev);
	}

	mutex_unlock(&driver_lock);

	module_put(vdev->parent_module);
}

static int vfio_platform_open(void *device_data)
{
	struct vfio_platform_device *vdev = device_data;
	int ret;

	if (!try_module_get(vdev->parent_module))
		return -ENODEV;

	mutex_lock(&driver_lock);

	if (!vdev->refcnt) {
		const char *extra_dbg = NULL;

		ret = vfio_platform_regions_init(vdev);
		if (ret)
			goto err_reg;

		ret = vfio_platform_irq_init(vdev);
		if (ret)
			goto err_irq;

		ret = pm_runtime_get_sync(vdev->device);
		if (ret < 0)
			goto err_pm;

		ret = vfio_platform_call_reset(vdev, &extra_dbg);
		if (ret && vdev->reset_required) {
			dev_warn(vdev->device, "reset driver is required and reset call failed in open (%d) %s\n",
				 ret, extra_dbg ? extra_dbg : "");
			goto err_rst;
		}
	}

	vdev->refcnt++;

	mutex_unlock(&driver_lock);
	return 0;

err_rst:
	pm_runtime_put(vdev->device);
err_pm:
	vfio_platform_irq_cleanup(vdev);
err_irq:
	vfio_platform_regions_cleanup(vdev);
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

		if (vfio_platform_has_reset(vdev))
			vdev->flags |= VFIO_DEVICE_FLAGS_RESET;
		info.flags = vdev->flags;
		info.num_regions = vdev->num_regions;
		info.num_irqs = vdev->num_irqs;

		return copy_to_user((void __user *)arg, &info, minsz) ?
			-EFAULT : 0;

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

		return copy_to_user((void __user *)arg, &info, minsz) ?
			-EFAULT : 0;

	} else if (cmd == VFIO_DEVICE_GET_IRQ_INFO) {
		struct vfio_irq_info info;

		minsz = offsetofend(struct vfio_irq_info, count);

		if (copy_from_user(&info, (void __user *)arg, minsz))
			return -EFAULT;

		if (info.argsz < minsz)
			return -EINVAL;

		if (info.index >= vdev->num_irqs)
			return -EINVAL;

		info.flags = vdev->irqs[info.index].flags;
		info.count = vdev->irqs[info.index].count;

		return copy_to_user((void __user *)arg, &info, minsz) ?
			-EFAULT : 0;

	} else if (cmd == VFIO_DEVICE_SET_IRQS) {
		struct vfio_irq_set hdr;
		u8 *data = NULL;
		int ret = 0;
		size_t data_size = 0;

		minsz = offsetofend(struct vfio_irq_set, count);

		if (copy_from_user(&hdr, (void __user *)arg, minsz))
			return -EFAULT;

		ret = vfio_set_irqs_validate_and_prepare(&hdr, vdev->num_irqs,
						 vdev->num_irqs, &data_size);
		if (ret)
			return ret;

		if (data_size) {
			data = memdup_user((void __user *)(arg + minsz),
					    data_size);
			if (IS_ERR(data))
				return PTR_ERR(data);
		}

		mutex_lock(&vdev->igate);

		ret = vfio_platform_set_irqs_ioctl(vdev, hdr.flags, hdr.index,
						   hdr.start, hdr.count, data);
		mutex_unlock(&vdev->igate);
		kfree(data);

		return ret;

	} else if (cmd == VFIO_DEVICE_RESET) {
		return vfio_platform_call_reset(vdev, NULL);
	}

	return -ENOTTY;
}

static ssize_t vfio_platform_read_mmio(struct vfio_platform_region *reg,
				       char __user *buf, size_t count,
				       loff_t off)
{
	unsigned int done = 0;

	if (!reg->ioaddr) {
		reg->ioaddr =
			ioremap_nocache(reg->addr, reg->size);

		if (!reg->ioaddr)
			return -ENOMEM;
	}

	while (count) {
		size_t filled;

		if (count >= 4 && !(off % 4)) {
			u32 val;

			val = ioread32(reg->ioaddr + off);
			if (copy_to_user(buf, &val, 4))
				goto err;

			filled = 4;
		} else if (count >= 2 && !(off % 2)) {
			u16 val;

			val = ioread16(reg->ioaddr + off);
			if (copy_to_user(buf, &val, 2))
				goto err;

			filled = 2;
		} else {
			u8 val;

			val = ioread8(reg->ioaddr + off);
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
		return vfio_platform_read_mmio(&vdev->regions[index],
							buf, count, off);
	else if (vdev->regions[index].type & VFIO_PLATFORM_REGION_TYPE_PIO)
		return -EINVAL; /* not implemented */

	return -EINVAL;
}

static ssize_t vfio_platform_write_mmio(struct vfio_platform_region *reg,
					const char __user *buf, size_t count,
					loff_t off)
{
	unsigned int done = 0;

	if (!reg->ioaddr) {
		reg->ioaddr =
			ioremap_nocache(reg->addr, reg->size);

		if (!reg->ioaddr)
			return -ENOMEM;
	}

	while (count) {
		size_t filled;

		if (count >= 4 && !(off % 4)) {
			u32 val;

			if (copy_from_user(&val, buf, 4))
				goto err;
			iowrite32(val, reg->ioaddr + off);

			filled = 4;
		} else if (count >= 2 && !(off % 2)) {
			u16 val;

			if (copy_from_user(&val, buf, 2))
				goto err;
			iowrite16(val, reg->ioaddr + off);

			filled = 2;
		} else {
			u8 val;

			if (copy_from_user(&val, buf, 1))
				goto err;
			iowrite8(val, reg->ioaddr + off);

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
		return vfio_platform_write_mmio(&vdev->regions[index],
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

static int vfio_platform_of_probe(struct vfio_platform_device *vdev,
			   struct device *dev)
{
	int ret;

	ret = device_property_read_string(dev, "compatible",
					  &vdev->compat);
	if (ret)
		dev_err(dev, "Cannot retrieve compat for %s\n", vdev->name);

	return ret;
}

/*
 * There can be two kernel build combinations. One build where
 * ACPI is not selected in Kconfig and another one with the ACPI Kconfig.
 *
 * In the first case, vfio_platform_acpi_probe will return since
 * acpi_disabled is 1. DT user will not see any kind of messages from
 * ACPI.
 *
 * In the second case, both DT and ACPI is compiled in but the system is
 * booting with any of these combinations.
 *
 * If the firmware is DT type, then acpi_disabled is 1. The ACPI probe routine
 * terminates immediately without any messages.
 *
 * If the firmware is ACPI type, then acpi_disabled is 0. All other checks are
 * valid checks. We cannot claim that this system is DT.
 */
int vfio_platform_probe_common(struct vfio_platform_device *vdev,
			       struct device *dev)
{
	struct iommu_group *group;
	int ret;

	if (!vdev)
		return -EINVAL;

	ret = vfio_platform_acpi_probe(vdev, dev);
	if (ret)
		ret = vfio_platform_of_probe(vdev, dev);

	if (ret)
		return ret;

	vdev->device = dev;

	ret = vfio_platform_get_reset(vdev);
	if (ret && vdev->reset_required) {
		dev_err(dev, "No reset function found for device %s\n",
			vdev->name);
		return ret;
	}

	group = vfio_iommu_group_get(dev);
	if (!group) {
		dev_err(dev, "No IOMMU group for device %s\n", vdev->name);
		ret = -EINVAL;
		goto put_reset;
	}

	ret = vfio_add_group_dev(dev, &vfio_platform_ops, vdev);
	if (ret)
		goto put_iommu;

	mutex_init(&vdev->igate);

	pm_runtime_enable(vdev->device);
	return 0;

put_iommu:
	vfio_iommu_group_put(group, dev);
put_reset:
	vfio_platform_put_reset(vdev);
	return ret;
}
EXPORT_SYMBOL_GPL(vfio_platform_probe_common);

struct vfio_platform_device *vfio_platform_remove_common(struct device *dev)
{
	struct vfio_platform_device *vdev;

	vdev = vfio_del_group_dev(dev);

	if (vdev) {
		pm_runtime_disable(vdev->device);
		vfio_platform_put_reset(vdev);
		vfio_iommu_group_put(dev->iommu_group, dev);
	}

	return vdev;
}
EXPORT_SYMBOL_GPL(vfio_platform_remove_common);

void __vfio_platform_register_reset(struct vfio_platform_reset_node *node)
{
	mutex_lock(&driver_lock);
	list_add(&node->link, &reset_list);
	mutex_unlock(&driver_lock);
}
EXPORT_SYMBOL_GPL(__vfio_platform_register_reset);

void vfio_platform_unregister_reset(const char *compat,
				    vfio_platform_reset_fn_t fn)
{
	struct vfio_platform_reset_node *iter, *temp;

	mutex_lock(&driver_lock);
	list_for_each_entry_safe(iter, temp, &reset_list, link) {
		if (!strcmp(iter->compat, compat) && (iter->of_reset == fn)) {
			list_del(&iter->link);
			break;
		}
	}

	mutex_unlock(&driver_lock);

}
EXPORT_SYMBOL_GPL(vfio_platform_unregister_reset);

MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
