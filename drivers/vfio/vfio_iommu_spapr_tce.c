/*
 * VFIO: IOMMU DMA mapping support for TCE on POWER
 *
 * Copyright (C) 2013 IBM Corp.  All rights reserved.
 *     Author: Alexey Kardashevskiy <aik@ozlabs.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Derived from original vfio_iommu_type1.c:
 * Copyright (C) 2012 Red Hat, Inc.  All rights reserved.
 *     Author: Alex Williamson <alex.williamson@redhat.com>
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/err.h>
#include <linux/vfio.h>
#include <asm/iommu.h>
#include <asm/tce.h>

#define DRIVER_VERSION  "0.1"
#define DRIVER_AUTHOR   "aik@ozlabs.ru"
#define DRIVER_DESC     "VFIO IOMMU SPAPR TCE"

static void tce_iommu_detach_group(void *iommu_data,
		struct iommu_group *iommu_group);

/*
 * VFIO IOMMU fd for SPAPR_TCE IOMMU implementation
 *
 * This code handles mapping and unmapping of user data buffers
 * into DMA'ble space using the IOMMU
 */

/*
 * The container descriptor supports only a single group per container.
 * Required by the API as the container is not supplied with the IOMMU group
 * at the moment of initialization.
 */
struct tce_container {
	struct mutex lock;
	struct iommu_table *tbl;
	bool enabled;
};

static bool tce_page_is_contained(struct page *page, unsigned page_shift)
{
	/*
	 * Check that the TCE table granularity is not bigger than the size of
	 * a page we just found. Otherwise the hardware can get access to
	 * a bigger memory chunk that it should.
	 */
	return (PAGE_SHIFT + compound_order(compound_head(page))) >= page_shift;
}

static int tce_iommu_enable(struct tce_container *container)
{
	int ret = 0;
	unsigned long locked, lock_limit, npages;
	struct iommu_table *tbl = container->tbl;

	if (!container->tbl)
		return -ENXIO;

	if (!current->mm)
		return -ESRCH; /* process exited */

	if (container->enabled)
		return -EBUSY;

	/*
	 * When userspace pages are mapped into the IOMMU, they are effectively
	 * locked memory, so, theoretically, we need to update the accounting
	 * of locked pages on each map and unmap.  For powerpc, the map unmap
	 * paths can be very hot, though, and the accounting would kill
	 * performance, especially since it would be difficult to impossible
	 * to handle the accounting in real mode only.
	 *
	 * To address that, rather than precisely accounting every page, we
	 * instead account for a worst case on locked memory when the iommu is
	 * enabled and disabled.  The worst case upper bound on locked memory
	 * is the size of the whole iommu window, which is usually relatively
	 * small (compared to total memory sizes) on POWER hardware.
	 *
	 * Also we don't have a nice way to fail on H_PUT_TCE due to ulimits,
	 * that would effectively kill the guest at random points, much better
	 * enforcing the limit based on the max that the guest can map.
	 */
	down_write(&current->mm->mmap_sem);
	npages = (tbl->it_size << tbl->it_page_shift) >> PAGE_SHIFT;
	locked = current->mm->locked_vm + npages;
	lock_limit = rlimit(RLIMIT_MEMLOCK) >> PAGE_SHIFT;
	if (locked > lock_limit && !capable(CAP_IPC_LOCK)) {
		pr_warn("RLIMIT_MEMLOCK (%ld) exceeded\n",
				rlimit(RLIMIT_MEMLOCK));
		ret = -ENOMEM;
	} else {

		current->mm->locked_vm += npages;
		container->enabled = true;
	}
	up_write(&current->mm->mmap_sem);

	return ret;
}

static void tce_iommu_disable(struct tce_container *container)
{
	if (!container->enabled)
		return;

	container->enabled = false;

	if (!container->tbl || !current->mm)
		return;

	down_write(&current->mm->mmap_sem);
	current->mm->locked_vm -= (container->tbl->it_size <<
			container->tbl->it_page_shift) >> PAGE_SHIFT;
	up_write(&current->mm->mmap_sem);
}

static void *tce_iommu_open(unsigned long arg)
{
	struct tce_container *container;

	if (arg != VFIO_SPAPR_TCE_IOMMU) {
		pr_err("tce_vfio: Wrong IOMMU type\n");
		return ERR_PTR(-EINVAL);
	}

	container = kzalloc(sizeof(*container), GFP_KERNEL);
	if (!container)
		return ERR_PTR(-ENOMEM);

	mutex_init(&container->lock);

	return container;
}

static void tce_iommu_release(void *iommu_data)
{
	struct tce_container *container = iommu_data;

	WARN_ON(container->tbl && !container->tbl->it_group);
	tce_iommu_disable(container);

	if (container->tbl && container->tbl->it_group)
		tce_iommu_detach_group(iommu_data, container->tbl->it_group);

	mutex_destroy(&container->lock);

	kfree(container);
}

static int tce_iommu_clear(struct tce_container *container,
		struct iommu_table *tbl,
		unsigned long entry, unsigned long pages)
{
	unsigned long oldtce;
	struct page *page;

	for ( ; pages; --pages, ++entry) {
		oldtce = iommu_clear_tce(tbl, entry);
		if (!oldtce)
			continue;

		page = pfn_to_page(oldtce >> PAGE_SHIFT);
		WARN_ON(!page);
		if (page) {
			if (oldtce & TCE_PCI_WRITE)
				SetPageDirty(page);
			put_page(page);
		}
	}

	return 0;
}

static long tce_iommu_build(struct tce_container *container,
		struct iommu_table *tbl,
		unsigned long entry, unsigned long tce, unsigned long pages)
{
	long i, ret = 0;
	struct page *page = NULL;
	unsigned long hva;
	enum dma_data_direction direction = iommu_tce_direction(tce);

	for (i = 0; i < pages; ++i) {
		unsigned long offset = tce & IOMMU_PAGE_MASK(tbl) & ~PAGE_MASK;

		ret = get_user_pages_fast(tce & PAGE_MASK, 1,
				direction != DMA_TO_DEVICE, &page);
		if (unlikely(ret != 1)) {
			ret = -EFAULT;
			break;
		}

		if (!tce_page_is_contained(page, tbl->it_page_shift)) {
			ret = -EPERM;
			break;
		}

		hva = (unsigned long) page_address(page) + offset;

		ret = iommu_tce_build(tbl, entry + i, hva, direction);
		if (ret) {
			put_page(page);
			pr_err("iommu_tce: %s failed ioba=%lx, tce=%lx, ret=%ld\n",
					__func__, entry << tbl->it_page_shift,
					tce, ret);
			break;
		}
		tce += IOMMU_PAGE_SIZE(tbl);
	}

	if (ret)
		tce_iommu_clear(container, tbl, entry, i);

	return ret;
}

static long tce_iommu_ioctl(void *iommu_data,
				 unsigned int cmd, unsigned long arg)
{
	struct tce_container *container = iommu_data;
	unsigned long minsz;
	long ret;

	switch (cmd) {
	case VFIO_CHECK_EXTENSION:
		switch (arg) {
		case VFIO_SPAPR_TCE_IOMMU:
			ret = 1;
			break;
		default:
			ret = vfio_spapr_iommu_eeh_ioctl(NULL, cmd, arg);
			break;
		}

		return (ret < 0) ? 0 : ret;

	case VFIO_IOMMU_SPAPR_TCE_GET_INFO: {
		struct vfio_iommu_spapr_tce_info info;
		struct iommu_table *tbl = container->tbl;

		if (WARN_ON(!tbl))
			return -ENXIO;

		minsz = offsetofend(struct vfio_iommu_spapr_tce_info,
				dma32_window_size);

		if (copy_from_user(&info, (void __user *)arg, minsz))
			return -EFAULT;

		if (info.argsz < minsz)
			return -EINVAL;

		info.dma32_window_start = tbl->it_offset << tbl->it_page_shift;
		info.dma32_window_size = tbl->it_size << tbl->it_page_shift;
		info.flags = 0;

		if (copy_to_user((void __user *)arg, &info, minsz))
			return -EFAULT;

		return 0;
	}
	case VFIO_IOMMU_MAP_DMA: {
		struct vfio_iommu_type1_dma_map param;
		struct iommu_table *tbl = container->tbl;
		unsigned long tce;

		if (!tbl)
			return -ENXIO;

		BUG_ON(!tbl->it_group);

		minsz = offsetofend(struct vfio_iommu_type1_dma_map, size);

		if (copy_from_user(&param, (void __user *)arg, minsz))
			return -EFAULT;

		if (param.argsz < minsz)
			return -EINVAL;

		if (param.flags & ~(VFIO_DMA_MAP_FLAG_READ |
				VFIO_DMA_MAP_FLAG_WRITE))
			return -EINVAL;

		if ((param.size & ~IOMMU_PAGE_MASK(tbl)) ||
				(param.vaddr & ~IOMMU_PAGE_MASK(tbl)))
			return -EINVAL;

		/* iova is checked by the IOMMU API */
		tce = param.vaddr;
		if (param.flags & VFIO_DMA_MAP_FLAG_READ)
			tce |= TCE_PCI_READ;
		if (param.flags & VFIO_DMA_MAP_FLAG_WRITE)
			tce |= TCE_PCI_WRITE;

		ret = iommu_tce_put_param_check(tbl, param.iova, tce);
		if (ret)
			return ret;

		ret = tce_iommu_build(container, tbl,
				param.iova >> tbl->it_page_shift,
				tce, param.size >> tbl->it_page_shift);

		iommu_flush_tce(tbl);

		return ret;
	}
	case VFIO_IOMMU_UNMAP_DMA: {
		struct vfio_iommu_type1_dma_unmap param;
		struct iommu_table *tbl = container->tbl;

		if (WARN_ON(!tbl))
			return -ENXIO;

		minsz = offsetofend(struct vfio_iommu_type1_dma_unmap,
				size);

		if (copy_from_user(&param, (void __user *)arg, minsz))
			return -EFAULT;

		if (param.argsz < minsz)
			return -EINVAL;

		/* No flag is supported now */
		if (param.flags)
			return -EINVAL;

		if (param.size & ~IOMMU_PAGE_MASK(tbl))
			return -EINVAL;

		ret = iommu_tce_clear_param_check(tbl, param.iova, 0,
				param.size >> tbl->it_page_shift);
		if (ret)
			return ret;

		ret = tce_iommu_clear(container, tbl,
				param.iova >> tbl->it_page_shift,
				param.size >> tbl->it_page_shift);
		iommu_flush_tce(tbl);

		return ret;
	}
	case VFIO_IOMMU_ENABLE:
		mutex_lock(&container->lock);
		ret = tce_iommu_enable(container);
		mutex_unlock(&container->lock);
		return ret;


	case VFIO_IOMMU_DISABLE:
		mutex_lock(&container->lock);
		tce_iommu_disable(container);
		mutex_unlock(&container->lock);
		return 0;
	case VFIO_EEH_PE_OP:
		if (!container->tbl || !container->tbl->it_group)
			return -ENODEV;

		return vfio_spapr_iommu_eeh_ioctl(container->tbl->it_group,
						  cmd, arg);
	}

	return -ENOTTY;
}

static int tce_iommu_attach_group(void *iommu_data,
		struct iommu_group *iommu_group)
{
	int ret;
	struct tce_container *container = iommu_data;
	struct iommu_table *tbl = iommu_group_get_iommudata(iommu_group);

	BUG_ON(!tbl);
	mutex_lock(&container->lock);

	/* pr_debug("tce_vfio: Attaching group #%u to iommu %p\n",
			iommu_group_id(iommu_group), iommu_group); */
	if (container->tbl) {
		pr_warn("tce_vfio: Only one group per IOMMU container is allowed, existing id=%d, attaching id=%d\n",
				iommu_group_id(container->tbl->it_group),
				iommu_group_id(iommu_group));
		ret = -EBUSY;
	} else if (container->enabled) {
		pr_err("tce_vfio: attaching group #%u to enabled container\n",
				iommu_group_id(iommu_group));
		ret = -EBUSY;
	} else {
		ret = iommu_take_ownership(tbl);
		if (!ret)
			container->tbl = tbl;
	}

	mutex_unlock(&container->lock);

	return ret;
}

static void tce_iommu_detach_group(void *iommu_data,
		struct iommu_group *iommu_group)
{
	struct tce_container *container = iommu_data;
	struct iommu_table *tbl = iommu_group_get_iommudata(iommu_group);

	BUG_ON(!tbl);
	mutex_lock(&container->lock);
	if (tbl != container->tbl) {
		pr_warn("tce_vfio: detaching group #%u, expected group is #%u\n",
				iommu_group_id(iommu_group),
				iommu_group_id(tbl->it_group));
	} else {
		if (container->enabled) {
			pr_warn("tce_vfio: detaching group #%u from enabled container, forcing disable\n",
					iommu_group_id(tbl->it_group));
			tce_iommu_disable(container);
		}

		/* pr_debug("tce_vfio: detaching group #%u from iommu %p\n",
				iommu_group_id(iommu_group), iommu_group); */
		container->tbl = NULL;
		tce_iommu_clear(container, tbl, tbl->it_offset, tbl->it_size);
		iommu_release_ownership(tbl);
	}
	mutex_unlock(&container->lock);
}

const struct vfio_iommu_driver_ops tce_iommu_driver_ops = {
	.name		= "iommu-vfio-powerpc",
	.owner		= THIS_MODULE,
	.open		= tce_iommu_open,
	.release	= tce_iommu_release,
	.ioctl		= tce_iommu_ioctl,
	.attach_group	= tce_iommu_attach_group,
	.detach_group	= tce_iommu_detach_group,
};

static int __init tce_iommu_init(void)
{
	return vfio_register_iommu_driver(&tce_iommu_driver_ops);
}

static void __exit tce_iommu_cleanup(void)
{
	vfio_unregister_iommu_driver(&tce_iommu_driver_ops);
}

module_init(tce_iommu_init);
module_exit(tce_iommu_cleanup);

MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);

