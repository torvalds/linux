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

static long try_increment_locked_vm(long npages)
{
	long ret = 0, locked, lock_limit;

	if (!current || !current->mm)
		return -ESRCH; /* process exited */

	if (!npages)
		return 0;

	down_write(&current->mm->mmap_sem);
	locked = current->mm->locked_vm + npages;
	lock_limit = rlimit(RLIMIT_MEMLOCK) >> PAGE_SHIFT;
	if (locked > lock_limit && !capable(CAP_IPC_LOCK))
		ret = -ENOMEM;
	else
		current->mm->locked_vm += npages;

	pr_debug("[%d] RLIMIT_MEMLOCK +%ld %ld/%ld%s\n", current->pid,
			npages << PAGE_SHIFT,
			current->mm->locked_vm << PAGE_SHIFT,
			rlimit(RLIMIT_MEMLOCK),
			ret ? " - exceeded" : "");

	up_write(&current->mm->mmap_sem);

	return ret;
}

static void decrement_locked_vm(long npages)
{
	if (!current || !current->mm || !npages)
		return; /* process exited */

	down_write(&current->mm->mmap_sem);
	if (WARN_ON_ONCE(npages > current->mm->locked_vm))
		npages = current->mm->locked_vm;
	current->mm->locked_vm -= npages;
	pr_debug("[%d] RLIMIT_MEMLOCK -%ld %ld/%ld\n", current->pid,
			npages << PAGE_SHIFT,
			current->mm->locked_vm << PAGE_SHIFT,
			rlimit(RLIMIT_MEMLOCK));
	up_write(&current->mm->mmap_sem);
}

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
	struct iommu_group *grp;
	bool enabled;
	unsigned long locked_pages;
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

static long tce_iommu_find_table(struct tce_container *container,
		phys_addr_t ioba, struct iommu_table **ptbl)
{
	long i;
	struct iommu_table_group *table_group;

	table_group = iommu_group_get_iommudata(container->grp);
	if (!table_group)
		return -1;

	for (i = 0; i < IOMMU_TABLE_GROUP_MAX_TABLES; ++i) {
		struct iommu_table *tbl = table_group->tables[i];

		if (tbl) {
			unsigned long entry = ioba >> tbl->it_page_shift;
			unsigned long start = tbl->it_offset;
			unsigned long end = start + tbl->it_size;

			if ((start <= entry) && (entry < end)) {
				*ptbl = tbl;
				return i;
			}
		}
	}

	return -1;
}

static int tce_iommu_enable(struct tce_container *container)
{
	int ret = 0;
	unsigned long locked;
	struct iommu_table *tbl;
	struct iommu_table_group *table_group;

	if (!container->grp)
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
	 *
	 * Unfortunately at the moment it counts whole tables, no matter how
	 * much memory the guest has. I.e. for 4GB guest and 4 IOMMU groups
	 * each with 2GB DMA window, 8GB will be counted here. The reason for
	 * this is that we cannot tell here the amount of RAM used by the guest
	 * as this information is only available from KVM and VFIO is
	 * KVM agnostic.
	 */
	table_group = iommu_group_get_iommudata(container->grp);
	if (!table_group)
		return -ENODEV;

	tbl = table_group->tables[0];
	locked = (tbl->it_size << tbl->it_page_shift) >> PAGE_SHIFT;
	ret = try_increment_locked_vm(locked);
	if (ret)
		return ret;

	container->locked_pages = locked;

	container->enabled = true;

	return ret;
}

static void tce_iommu_disable(struct tce_container *container)
{
	if (!container->enabled)
		return;

	container->enabled = false;

	if (!current->mm)
		return;

	decrement_locked_vm(container->locked_pages);
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

	WARN_ON(container->grp);

	if (container->grp)
		tce_iommu_detach_group(iommu_data, container->grp);

	tce_iommu_disable(container);
	mutex_destroy(&container->lock);

	kfree(container);
}

static void tce_iommu_unuse_page(struct tce_container *container,
		unsigned long oldtce)
{
	struct page *page;

	if (!(oldtce & (TCE_PCI_READ | TCE_PCI_WRITE)))
		return;

	page = pfn_to_page(oldtce >> PAGE_SHIFT);

	if (oldtce & TCE_PCI_WRITE)
		SetPageDirty(page);

	put_page(page);
}

static int tce_iommu_clear(struct tce_container *container,
		struct iommu_table *tbl,
		unsigned long entry, unsigned long pages)
{
	unsigned long oldtce;

	for ( ; pages; --pages, ++entry) {
		oldtce = iommu_clear_tce(tbl, entry);
		if (!oldtce)
			continue;

		tce_iommu_unuse_page(container, oldtce);
	}

	return 0;
}

static int tce_iommu_use_page(unsigned long tce, unsigned long *hpa)
{
	struct page *page = NULL;
	enum dma_data_direction direction = iommu_tce_direction(tce);

	if (get_user_pages_fast(tce & PAGE_MASK, 1,
			direction != DMA_TO_DEVICE, &page) != 1)
		return -EFAULT;

	*hpa = __pa((unsigned long) page_address(page));

	return 0;
}

static long tce_iommu_build(struct tce_container *container,
		struct iommu_table *tbl,
		unsigned long entry, unsigned long tce, unsigned long pages)
{
	long i, ret = 0;
	struct page *page;
	unsigned long hpa;
	enum dma_data_direction direction = iommu_tce_direction(tce);

	for (i = 0; i < pages; ++i) {
		unsigned long offset = tce & IOMMU_PAGE_MASK(tbl) & ~PAGE_MASK;

		ret = tce_iommu_use_page(tce, &hpa);
		if (ret)
			break;

		page = pfn_to_page(hpa >> PAGE_SHIFT);
		if (!tce_page_is_contained(page, tbl->it_page_shift)) {
			ret = -EPERM;
			break;
		}

		hpa |= offset;
		ret = iommu_tce_build(tbl, entry + i, (unsigned long) __va(hpa),
				direction);
		if (ret) {
			tce_iommu_unuse_page(container, hpa);
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
		struct iommu_table *tbl;
		struct iommu_table_group *table_group;

		if (WARN_ON(!container->grp))
			return -ENXIO;

		table_group = iommu_group_get_iommudata(container->grp);

		tbl = table_group->tables[0];
		if (WARN_ON_ONCE(!tbl))
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
		struct iommu_table *tbl = NULL;
		unsigned long tce;
		long num;

		if (!container->enabled)
			return -EPERM;

		minsz = offsetofend(struct vfio_iommu_type1_dma_map, size);

		if (copy_from_user(&param, (void __user *)arg, minsz))
			return -EFAULT;

		if (param.argsz < minsz)
			return -EINVAL;

		if (param.flags & ~(VFIO_DMA_MAP_FLAG_READ |
				VFIO_DMA_MAP_FLAG_WRITE))
			return -EINVAL;

		num = tce_iommu_find_table(container, param.iova, &tbl);
		if (num < 0)
			return -ENXIO;

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
		struct iommu_table *tbl = NULL;
		long num;

		if (!container->enabled)
			return -EPERM;

		minsz = offsetofend(struct vfio_iommu_type1_dma_unmap,
				size);

		if (copy_from_user(&param, (void __user *)arg, minsz))
			return -EFAULT;

		if (param.argsz < minsz)
			return -EINVAL;

		/* No flag is supported now */
		if (param.flags)
			return -EINVAL;

		num = tce_iommu_find_table(container, param.iova, &tbl);
		if (num < 0)
			return -ENXIO;

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
		if (!container->grp)
			return -ENODEV;

		return vfio_spapr_iommu_eeh_ioctl(container->grp,
						  cmd, arg);
	}

	return -ENOTTY;
}

static void tce_iommu_release_ownership(struct tce_container *container,
		struct iommu_table_group *table_group)
{
	int i;

	for (i = 0; i < IOMMU_TABLE_GROUP_MAX_TABLES; ++i) {
		struct iommu_table *tbl = table_group->tables[i];

		if (!tbl)
			continue;

		tce_iommu_clear(container, tbl, tbl->it_offset, tbl->it_size);
		if (tbl->it_map)
			iommu_release_ownership(tbl);
	}
}

static int tce_iommu_take_ownership(struct tce_container *container,
		struct iommu_table_group *table_group)
{
	int i, j, rc = 0;

	for (i = 0; i < IOMMU_TABLE_GROUP_MAX_TABLES; ++i) {
		struct iommu_table *tbl = table_group->tables[i];

		if (!tbl || !tbl->it_map)
			continue;

		rc = iommu_take_ownership(tbl);
		if (rc) {
			for (j = 0; j < i; ++j)
				iommu_release_ownership(
						table_group->tables[j]);

			return rc;
		}
	}

	return 0;
}

static void tce_iommu_release_ownership_ddw(struct tce_container *container,
		struct iommu_table_group *table_group)
{
	table_group->ops->release_ownership(table_group);
}

static long tce_iommu_take_ownership_ddw(struct tce_container *container,
		struct iommu_table_group *table_group)
{
	table_group->ops->take_ownership(table_group);

	return 0;
}

static int tce_iommu_attach_group(void *iommu_data,
		struct iommu_group *iommu_group)
{
	int ret;
	struct tce_container *container = iommu_data;
	struct iommu_table_group *table_group;

	mutex_lock(&container->lock);

	/* pr_debug("tce_vfio: Attaching group #%u to iommu %p\n",
			iommu_group_id(iommu_group), iommu_group); */
	if (container->grp) {
		pr_warn("tce_vfio: Only one group per IOMMU container is allowed, existing id=%d, attaching id=%d\n",
				iommu_group_id(container->grp),
				iommu_group_id(iommu_group));
		ret = -EBUSY;
		goto unlock_exit;
	}

	if (container->enabled) {
		pr_err("tce_vfio: attaching group #%u to enabled container\n",
				iommu_group_id(iommu_group));
		ret = -EBUSY;
		goto unlock_exit;
	}

	table_group = iommu_group_get_iommudata(iommu_group);
	if (!table_group) {
		ret = -ENXIO;
		goto unlock_exit;
	}

	if (!table_group->ops || !table_group->ops->take_ownership ||
			!table_group->ops->release_ownership)
		ret = tce_iommu_take_ownership(container, table_group);
	else
		ret = tce_iommu_take_ownership_ddw(container, table_group);

	if (!ret)
		container->grp = iommu_group;

unlock_exit:
	mutex_unlock(&container->lock);

	return ret;
}

static void tce_iommu_detach_group(void *iommu_data,
		struct iommu_group *iommu_group)
{
	struct tce_container *container = iommu_data;
	struct iommu_table_group *table_group;

	mutex_lock(&container->lock);
	if (iommu_group != container->grp) {
		pr_warn("tce_vfio: detaching group #%u, expected group is #%u\n",
				iommu_group_id(iommu_group),
				iommu_group_id(container->grp));
		goto unlock_exit;
	}

	if (container->enabled) {
		pr_warn("tce_vfio: detaching group #%u from enabled container, forcing disable\n",
				iommu_group_id(container->grp));
		tce_iommu_disable(container);
	}

	/* pr_debug("tce_vfio: detaching group #%u from iommu %p\n",
	   iommu_group_id(iommu_group), iommu_group); */
	container->grp = NULL;

	table_group = iommu_group_get_iommudata(iommu_group);
	BUG_ON(!table_group);

	if (!table_group->ops || !table_group->ops->release_ownership)
		tce_iommu_release_ownership(container, table_group);
	else
		tce_iommu_release_ownership_ddw(container, table_group);

unlock_exit:
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

