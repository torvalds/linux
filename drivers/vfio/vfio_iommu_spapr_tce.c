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
#include <linux/vmalloc.h>
#include <asm/iommu.h>
#include <asm/tce.h>
#include <asm/mmu_context.h>

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

struct tce_iommu_group {
	struct list_head next;
	struct iommu_group *grp;
};

/*
 * The container descriptor supports only a single group per container.
 * Required by the API as the container is not supplied with the IOMMU group
 * at the moment of initialization.
 */
struct tce_container {
	struct mutex lock;
	bool enabled;
	bool v2;
	unsigned long locked_pages;
	struct iommu_table *tables[IOMMU_TABLE_GROUP_MAX_TABLES];
	struct list_head group_list;
};

static long tce_iommu_unregister_pages(struct tce_container *container,
		__u64 vaddr, __u64 size)
{
	struct mm_iommu_table_group_mem_t *mem;

	if ((vaddr & ~PAGE_MASK) || (size & ~PAGE_MASK))
		return -EINVAL;

	mem = mm_iommu_find(vaddr, size >> PAGE_SHIFT);
	if (!mem)
		return -ENOENT;

	return mm_iommu_put(mem);
}

static long tce_iommu_register_pages(struct tce_container *container,
		__u64 vaddr, __u64 size)
{
	long ret = 0;
	struct mm_iommu_table_group_mem_t *mem = NULL;
	unsigned long entries = size >> PAGE_SHIFT;

	if ((vaddr & ~PAGE_MASK) || (size & ~PAGE_MASK) ||
			((vaddr + size) < vaddr))
		return -EINVAL;

	ret = mm_iommu_get(vaddr, entries, &mem);
	if (ret)
		return ret;

	container->enabled = true;

	return 0;
}

static long tce_iommu_userspace_view_alloc(struct iommu_table *tbl)
{
	unsigned long cb = _ALIGN_UP(sizeof(tbl->it_userspace[0]) *
			tbl->it_size, PAGE_SIZE);
	unsigned long *uas;
	long ret;

	BUG_ON(tbl->it_userspace);

	ret = try_increment_locked_vm(cb >> PAGE_SHIFT);
	if (ret)
		return ret;

	uas = vzalloc(cb);
	if (!uas) {
		decrement_locked_vm(cb >> PAGE_SHIFT);
		return -ENOMEM;
	}
	tbl->it_userspace = uas;

	return 0;
}

static void tce_iommu_userspace_view_free(struct iommu_table *tbl)
{
	unsigned long cb = _ALIGN_UP(sizeof(tbl->it_userspace[0]) *
			tbl->it_size, PAGE_SIZE);

	if (!tbl->it_userspace)
		return;

	vfree(tbl->it_userspace);
	tbl->it_userspace = NULL;
	decrement_locked_vm(cb >> PAGE_SHIFT);
}

static bool tce_page_is_contained(struct page *page, unsigned page_shift)
{
	/*
	 * Check that the TCE table granularity is not bigger than the size of
	 * a page we just found. Otherwise the hardware can get access to
	 * a bigger memory chunk that it should.
	 */
	return (PAGE_SHIFT + compound_order(compound_head(page))) >= page_shift;
}

static inline bool tce_groups_attached(struct tce_container *container)
{
	return !list_empty(&container->group_list);
}

static long tce_iommu_find_table(struct tce_container *container,
		phys_addr_t ioba, struct iommu_table **ptbl)
{
	long i;

	for (i = 0; i < IOMMU_TABLE_GROUP_MAX_TABLES; ++i) {
		struct iommu_table *tbl = container->tables[i];

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

static int tce_iommu_find_free_table(struct tce_container *container)
{
	int i;

	for (i = 0; i < IOMMU_TABLE_GROUP_MAX_TABLES; ++i) {
		if (!container->tables[i])
			return i;
	}

	return -ENOSPC;
}

static int tce_iommu_enable(struct tce_container *container)
{
	int ret = 0;
	unsigned long locked;
	struct iommu_table_group *table_group;
	struct tce_iommu_group *tcegrp;

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
	 *
	 * So we do not allow enabling a container without a group attached
	 * as there is no way to know how much we should increment
	 * the locked_vm counter.
	 */
	if (!tce_groups_attached(container))
		return -ENODEV;

	tcegrp = list_first_entry(&container->group_list,
			struct tce_iommu_group, next);
	table_group = iommu_group_get_iommudata(tcegrp->grp);
	if (!table_group)
		return -ENODEV;

	if (!table_group->tce32_size)
		return -EPERM;

	locked = table_group->tce32_size >> PAGE_SHIFT;
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

	if ((arg != VFIO_SPAPR_TCE_IOMMU) && (arg != VFIO_SPAPR_TCE_v2_IOMMU)) {
		pr_err("tce_vfio: Wrong IOMMU type\n");
		return ERR_PTR(-EINVAL);
	}

	container = kzalloc(sizeof(*container), GFP_KERNEL);
	if (!container)
		return ERR_PTR(-ENOMEM);

	mutex_init(&container->lock);
	INIT_LIST_HEAD_RCU(&container->group_list);

	container->v2 = arg == VFIO_SPAPR_TCE_v2_IOMMU;

	return container;
}

static int tce_iommu_clear(struct tce_container *container,
		struct iommu_table *tbl,
		unsigned long entry, unsigned long pages);
static void tce_iommu_free_table(struct iommu_table *tbl);

static void tce_iommu_release(void *iommu_data)
{
	struct tce_container *container = iommu_data;
	struct iommu_table_group *table_group;
	struct tce_iommu_group *tcegrp;
	long i;

	while (tce_groups_attached(container)) {
		tcegrp = list_first_entry(&container->group_list,
				struct tce_iommu_group, next);
		table_group = iommu_group_get_iommudata(tcegrp->grp);
		tce_iommu_detach_group(iommu_data, tcegrp->grp);
	}

	/*
	 * If VFIO created a table, it was not disposed
	 * by tce_iommu_detach_group() so do it now.
	 */
	for (i = 0; i < IOMMU_TABLE_GROUP_MAX_TABLES; ++i) {
		struct iommu_table *tbl = container->tables[i];

		if (!tbl)
			continue;

		tce_iommu_clear(container, tbl, tbl->it_offset, tbl->it_size);
		tce_iommu_free_table(tbl);
	}

	tce_iommu_disable(container);
	mutex_destroy(&container->lock);

	kfree(container);
}

static void tce_iommu_unuse_page(struct tce_container *container,
		unsigned long hpa)
{
	struct page *page;

	page = pfn_to_page(hpa >> PAGE_SHIFT);
	put_page(page);
}

static int tce_iommu_prereg_ua_to_hpa(unsigned long tce, unsigned long size,
		unsigned long *phpa, struct mm_iommu_table_group_mem_t **pmem)
{
	long ret = 0;
	struct mm_iommu_table_group_mem_t *mem;

	mem = mm_iommu_lookup(tce, size);
	if (!mem)
		return -EINVAL;

	ret = mm_iommu_ua_to_hpa(mem, tce, phpa);
	if (ret)
		return -EINVAL;

	*pmem = mem;

	return 0;
}

static void tce_iommu_unuse_page_v2(struct iommu_table *tbl,
		unsigned long entry)
{
	struct mm_iommu_table_group_mem_t *mem = NULL;
	int ret;
	unsigned long hpa = 0;
	unsigned long *pua = IOMMU_TABLE_USERSPACE_ENTRY(tbl, entry);

	if (!pua || !current || !current->mm)
		return;

	ret = tce_iommu_prereg_ua_to_hpa(*pua, IOMMU_PAGE_SIZE(tbl),
			&hpa, &mem);
	if (ret)
		pr_debug("%s: tce %lx at #%lx was not cached, ret=%d\n",
				__func__, *pua, entry, ret);
	if (mem)
		mm_iommu_mapped_dec(mem);

	*pua = 0;
}

static int tce_iommu_clear(struct tce_container *container,
		struct iommu_table *tbl,
		unsigned long entry, unsigned long pages)
{
	unsigned long oldhpa;
	long ret;
	enum dma_data_direction direction;

	for ( ; pages; --pages, ++entry) {
		direction = DMA_NONE;
		oldhpa = 0;
		ret = iommu_tce_xchg(tbl, entry, &oldhpa, &direction);
		if (ret)
			continue;

		if (direction == DMA_NONE)
			continue;

		if (container->v2) {
			tce_iommu_unuse_page_v2(tbl, entry);
			continue;
		}

		tce_iommu_unuse_page(container, oldhpa);
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
		unsigned long entry, unsigned long tce, unsigned long pages,
		enum dma_data_direction direction)
{
	long i, ret = 0;
	struct page *page;
	unsigned long hpa;
	enum dma_data_direction dirtmp;

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
		dirtmp = direction;
		ret = iommu_tce_xchg(tbl, entry + i, &hpa, &dirtmp);
		if (ret) {
			tce_iommu_unuse_page(container, hpa);
			pr_err("iommu_tce: %s failed ioba=%lx, tce=%lx, ret=%ld\n",
					__func__, entry << tbl->it_page_shift,
					tce, ret);
			break;
		}

		if (dirtmp != DMA_NONE)
			tce_iommu_unuse_page(container, hpa);

		tce += IOMMU_PAGE_SIZE(tbl);
	}

	if (ret)
		tce_iommu_clear(container, tbl, entry, i);

	return ret;
}

static long tce_iommu_build_v2(struct tce_container *container,
		struct iommu_table *tbl,
		unsigned long entry, unsigned long tce, unsigned long pages,
		enum dma_data_direction direction)
{
	long i, ret = 0;
	struct page *page;
	unsigned long hpa;
	enum dma_data_direction dirtmp;

	for (i = 0; i < pages; ++i) {
		struct mm_iommu_table_group_mem_t *mem = NULL;
		unsigned long *pua = IOMMU_TABLE_USERSPACE_ENTRY(tbl,
				entry + i);

		ret = tce_iommu_prereg_ua_to_hpa(tce, IOMMU_PAGE_SIZE(tbl),
				&hpa, &mem);
		if (ret)
			break;

		page = pfn_to_page(hpa >> PAGE_SHIFT);
		if (!tce_page_is_contained(page, tbl->it_page_shift)) {
			ret = -EPERM;
			break;
		}

		/* Preserve offset within IOMMU page */
		hpa |= tce & IOMMU_PAGE_MASK(tbl) & ~PAGE_MASK;
		dirtmp = direction;

		/* The registered region is being unregistered */
		if (mm_iommu_mapped_inc(mem))
			break;

		ret = iommu_tce_xchg(tbl, entry + i, &hpa, &dirtmp);
		if (ret) {
			/* dirtmp cannot be DMA_NONE here */
			tce_iommu_unuse_page_v2(tbl, entry + i);
			pr_err("iommu_tce: %s failed ioba=%lx, tce=%lx, ret=%ld\n",
					__func__, entry << tbl->it_page_shift,
					tce, ret);
			break;
		}

		if (dirtmp != DMA_NONE)
			tce_iommu_unuse_page_v2(tbl, entry + i);

		*pua = tce;

		tce += IOMMU_PAGE_SIZE(tbl);
	}

	if (ret)
		tce_iommu_clear(container, tbl, entry, i);

	return ret;
}

static long tce_iommu_create_table(struct tce_container *container,
			struct iommu_table_group *table_group,
			int num,
			__u32 page_shift,
			__u64 window_size,
			__u32 levels,
			struct iommu_table **ptbl)
{
	long ret, table_size;

	table_size = table_group->ops->get_table_size(page_shift, window_size,
			levels);
	if (!table_size)
		return -EINVAL;

	ret = try_increment_locked_vm(table_size >> PAGE_SHIFT);
	if (ret)
		return ret;

	ret = table_group->ops->create_table(table_group, num,
			page_shift, window_size, levels, ptbl);

	WARN_ON(!ret && !(*ptbl)->it_ops->free);
	WARN_ON(!ret && ((*ptbl)->it_allocated_size != table_size));

	if (!ret && container->v2) {
		ret = tce_iommu_userspace_view_alloc(*ptbl);
		if (ret)
			(*ptbl)->it_ops->free(*ptbl);
	}

	if (ret)
		decrement_locked_vm(table_size >> PAGE_SHIFT);

	return ret;
}

static void tce_iommu_free_table(struct iommu_table *tbl)
{
	unsigned long pages = tbl->it_allocated_size >> PAGE_SHIFT;

	tce_iommu_userspace_view_free(tbl);
	tbl->it_ops->free(tbl);
	decrement_locked_vm(pages);
}

static long tce_iommu_create_window(struct tce_container *container,
		__u32 page_shift, __u64 window_size, __u32 levels,
		__u64 *start_addr)
{
	struct tce_iommu_group *tcegrp;
	struct iommu_table_group *table_group;
	struct iommu_table *tbl = NULL;
	long ret, num;

	num = tce_iommu_find_free_table(container);
	if (num < 0)
		return num;

	/* Get the first group for ops::create_table */
	tcegrp = list_first_entry(&container->group_list,
			struct tce_iommu_group, next);
	table_group = iommu_group_get_iommudata(tcegrp->grp);
	if (!table_group)
		return -EFAULT;

	if (!(table_group->pgsizes & (1ULL << page_shift)))
		return -EINVAL;

	if (!table_group->ops->set_window || !table_group->ops->unset_window ||
			!table_group->ops->get_table_size ||
			!table_group->ops->create_table)
		return -EPERM;

	/* Create TCE table */
	ret = tce_iommu_create_table(container, table_group, num,
			page_shift, window_size, levels, &tbl);
	if (ret)
		return ret;

	BUG_ON(!tbl->it_ops->free);

	/*
	 * Program the table to every group.
	 * Groups have been tested for compatibility at the attach time.
	 */
	list_for_each_entry(tcegrp, &container->group_list, next) {
		table_group = iommu_group_get_iommudata(tcegrp->grp);

		ret = table_group->ops->set_window(table_group, num, tbl);
		if (ret)
			goto unset_exit;
	}

	container->tables[num] = tbl;

	/* Return start address assigned by platform in create_table() */
	*start_addr = tbl->it_offset << tbl->it_page_shift;

	return 0;

unset_exit:
	list_for_each_entry(tcegrp, &container->group_list, next) {
		table_group = iommu_group_get_iommudata(tcegrp->grp);
		table_group->ops->unset_window(table_group, num);
	}
	tce_iommu_free_table(tbl);

	return ret;
}

static long tce_iommu_remove_window(struct tce_container *container,
		__u64 start_addr)
{
	struct iommu_table_group *table_group = NULL;
	struct iommu_table *tbl;
	struct tce_iommu_group *tcegrp;
	int num;

	num = tce_iommu_find_table(container, start_addr, &tbl);
	if (num < 0)
		return -EINVAL;

	BUG_ON(!tbl->it_size);

	/* Detach groups from IOMMUs */
	list_for_each_entry(tcegrp, &container->group_list, next) {
		table_group = iommu_group_get_iommudata(tcegrp->grp);

		/*
		 * SPAPR TCE IOMMU exposes the default DMA window to
		 * the guest via dma32_window_start/size of
		 * VFIO_IOMMU_SPAPR_TCE_GET_INFO. Some platforms allow
		 * the userspace to remove this window, some do not so
		 * here we check for the platform capability.
		 */
		if (!table_group->ops || !table_group->ops->unset_window)
			return -EPERM;

		table_group->ops->unset_window(table_group, num);
	}

	/* Free table */
	tce_iommu_clear(container, tbl, tbl->it_offset, tbl->it_size);
	tce_iommu_free_table(tbl);
	container->tables[num] = NULL;

	return 0;
}

static long tce_iommu_ioctl(void *iommu_data,
				 unsigned int cmd, unsigned long arg)
{
	struct tce_container *container = iommu_data;
	unsigned long minsz, ddwsz;
	long ret;

	switch (cmd) {
	case VFIO_CHECK_EXTENSION:
		switch (arg) {
		case VFIO_SPAPR_TCE_IOMMU:
		case VFIO_SPAPR_TCE_v2_IOMMU:
			ret = 1;
			break;
		default:
			ret = vfio_spapr_iommu_eeh_ioctl(NULL, cmd, arg);
			break;
		}

		return (ret < 0) ? 0 : ret;

	case VFIO_IOMMU_SPAPR_TCE_GET_INFO: {
		struct vfio_iommu_spapr_tce_info info;
		struct tce_iommu_group *tcegrp;
		struct iommu_table_group *table_group;

		if (!tce_groups_attached(container))
			return -ENXIO;

		tcegrp = list_first_entry(&container->group_list,
				struct tce_iommu_group, next);
		table_group = iommu_group_get_iommudata(tcegrp->grp);

		if (!table_group)
			return -ENXIO;

		minsz = offsetofend(struct vfio_iommu_spapr_tce_info,
				dma32_window_size);

		if (copy_from_user(&info, (void __user *)arg, minsz))
			return -EFAULT;

		if (info.argsz < minsz)
			return -EINVAL;

		info.dma32_window_start = table_group->tce32_start;
		info.dma32_window_size = table_group->tce32_size;
		info.flags = 0;
		memset(&info.ddw, 0, sizeof(info.ddw));

		if (table_group->max_dynamic_windows_supported &&
				container->v2) {
			info.flags |= VFIO_IOMMU_SPAPR_INFO_DDW;
			info.ddw.pgsizes = table_group->pgsizes;
			info.ddw.max_dynamic_windows_supported =
				table_group->max_dynamic_windows_supported;
			info.ddw.levels = table_group->max_levels;
		}

		ddwsz = offsetofend(struct vfio_iommu_spapr_tce_info, ddw);

		if (info.argsz >= ddwsz)
			minsz = ddwsz;

		if (copy_to_user((void __user *)arg, &info, minsz))
			return -EFAULT;

		return 0;
	}
	case VFIO_IOMMU_MAP_DMA: {
		struct vfio_iommu_type1_dma_map param;
		struct iommu_table *tbl = NULL;
		long num;
		enum dma_data_direction direction;

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
		if (param.flags & VFIO_DMA_MAP_FLAG_READ) {
			if (param.flags & VFIO_DMA_MAP_FLAG_WRITE)
				direction = DMA_BIDIRECTIONAL;
			else
				direction = DMA_TO_DEVICE;
		} else {
			if (param.flags & VFIO_DMA_MAP_FLAG_WRITE)
				direction = DMA_FROM_DEVICE;
			else
				return -EINVAL;
		}

		ret = iommu_tce_put_param_check(tbl, param.iova, param.vaddr);
		if (ret)
			return ret;

		if (container->v2)
			ret = tce_iommu_build_v2(container, tbl,
					param.iova >> tbl->it_page_shift,
					param.vaddr,
					param.size >> tbl->it_page_shift,
					direction);
		else
			ret = tce_iommu_build(container, tbl,
					param.iova >> tbl->it_page_shift,
					param.vaddr,
					param.size >> tbl->it_page_shift,
					direction);

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
	case VFIO_IOMMU_SPAPR_REGISTER_MEMORY: {
		struct vfio_iommu_spapr_register_memory param;

		if (!container->v2)
			break;

		minsz = offsetofend(struct vfio_iommu_spapr_register_memory,
				size);

		if (copy_from_user(&param, (void __user *)arg, minsz))
			return -EFAULT;

		if (param.argsz < minsz)
			return -EINVAL;

		/* No flag is supported now */
		if (param.flags)
			return -EINVAL;

		mutex_lock(&container->lock);
		ret = tce_iommu_register_pages(container, param.vaddr,
				param.size);
		mutex_unlock(&container->lock);

		return ret;
	}
	case VFIO_IOMMU_SPAPR_UNREGISTER_MEMORY: {
		struct vfio_iommu_spapr_register_memory param;

		if (!container->v2)
			break;

		minsz = offsetofend(struct vfio_iommu_spapr_register_memory,
				size);

		if (copy_from_user(&param, (void __user *)arg, minsz))
			return -EFAULT;

		if (param.argsz < minsz)
			return -EINVAL;

		/* No flag is supported now */
		if (param.flags)
			return -EINVAL;

		mutex_lock(&container->lock);
		ret = tce_iommu_unregister_pages(container, param.vaddr,
				param.size);
		mutex_unlock(&container->lock);

		return ret;
	}
	case VFIO_IOMMU_ENABLE:
		if (container->v2)
			break;

		mutex_lock(&container->lock);
		ret = tce_iommu_enable(container);
		mutex_unlock(&container->lock);
		return ret;


	case VFIO_IOMMU_DISABLE:
		if (container->v2)
			break;

		mutex_lock(&container->lock);
		tce_iommu_disable(container);
		mutex_unlock(&container->lock);
		return 0;

	case VFIO_EEH_PE_OP: {
		struct tce_iommu_group *tcegrp;

		ret = 0;
		list_for_each_entry(tcegrp, &container->group_list, next) {
			ret = vfio_spapr_iommu_eeh_ioctl(tcegrp->grp,
					cmd, arg);
			if (ret)
				return ret;
		}
		return ret;
	}

	case VFIO_IOMMU_SPAPR_TCE_CREATE: {
		struct vfio_iommu_spapr_tce_create create;

		if (!container->v2)
			break;

		if (!tce_groups_attached(container))
			return -ENXIO;

		minsz = offsetofend(struct vfio_iommu_spapr_tce_create,
				start_addr);

		if (copy_from_user(&create, (void __user *)arg, minsz))
			return -EFAULT;

		if (create.argsz < minsz)
			return -EINVAL;

		if (create.flags)
			return -EINVAL;

		mutex_lock(&container->lock);

		ret = tce_iommu_create_window(container, create.page_shift,
				create.window_size, create.levels,
				&create.start_addr);

		mutex_unlock(&container->lock);

		if (!ret && copy_to_user((void __user *)arg, &create, minsz))
			ret = -EFAULT;

		return ret;
	}
	case VFIO_IOMMU_SPAPR_TCE_REMOVE: {
		struct vfio_iommu_spapr_tce_remove remove;

		if (!container->v2)
			break;

		if (!tce_groups_attached(container))
			return -ENXIO;

		minsz = offsetofend(struct vfio_iommu_spapr_tce_remove,
				start_addr);

		if (copy_from_user(&remove, (void __user *)arg, minsz))
			return -EFAULT;

		if (remove.argsz < minsz)
			return -EINVAL;

		if (remove.flags)
			return -EINVAL;

		mutex_lock(&container->lock);

		ret = tce_iommu_remove_window(container, remove.start_addr);

		mutex_unlock(&container->lock);

		return ret;
	}
	}

	return -ENOTTY;
}

static void tce_iommu_release_ownership(struct tce_container *container,
		struct iommu_table_group *table_group)
{
	int i;

	for (i = 0; i < IOMMU_TABLE_GROUP_MAX_TABLES; ++i) {
		struct iommu_table *tbl = container->tables[i];

		if (!tbl)
			continue;

		tce_iommu_clear(container, tbl, tbl->it_offset, tbl->it_size);
		tce_iommu_userspace_view_free(tbl);
		if (tbl->it_map)
			iommu_release_ownership(tbl);

		container->tables[i] = NULL;
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

		rc = tce_iommu_userspace_view_alloc(tbl);
		if (!rc)
			rc = iommu_take_ownership(tbl);

		if (rc) {
			for (j = 0; j < i; ++j)
				iommu_release_ownership(
						table_group->tables[j]);

			return rc;
		}
	}

	for (i = 0; i < IOMMU_TABLE_GROUP_MAX_TABLES; ++i)
		container->tables[i] = table_group->tables[i];

	return 0;
}

static void tce_iommu_release_ownership_ddw(struct tce_container *container,
		struct iommu_table_group *table_group)
{
	long i;

	if (!table_group->ops->unset_window) {
		WARN_ON_ONCE(1);
		return;
	}

	for (i = 0; i < IOMMU_TABLE_GROUP_MAX_TABLES; ++i)
		table_group->ops->unset_window(table_group, i);

	table_group->ops->release_ownership(table_group);
}

static long tce_iommu_take_ownership_ddw(struct tce_container *container,
		struct iommu_table_group *table_group)
{
	long i, ret = 0;
	struct iommu_table *tbl = NULL;

	if (!table_group->ops->create_table || !table_group->ops->set_window ||
			!table_group->ops->release_ownership) {
		WARN_ON_ONCE(1);
		return -EFAULT;
	}

	table_group->ops->take_ownership(table_group);

	/*
	 * If it the first group attached, check if there is
	 * a default DMA window and create one if none as
	 * the userspace expects it to exist.
	 */
	if (!tce_groups_attached(container) && !container->tables[0]) {
		ret = tce_iommu_create_table(container,
				table_group,
				0, /* window number */
				IOMMU_PAGE_SHIFT_4K,
				table_group->tce32_size,
				1, /* default levels */
				&tbl);
		if (ret)
			goto release_exit;
		else
			container->tables[0] = tbl;
	}

	/* Set all windows to the new group */
	for (i = 0; i < IOMMU_TABLE_GROUP_MAX_TABLES; ++i) {
		tbl = container->tables[i];

		if (!tbl)
			continue;

		/* Set the default window to a new group */
		ret = table_group->ops->set_window(table_group, i, tbl);
		if (ret)
			goto release_exit;
	}

	return 0;

release_exit:
	for (i = 0; i < IOMMU_TABLE_GROUP_MAX_TABLES; ++i)
		table_group->ops->unset_window(table_group, i);

	table_group->ops->release_ownership(table_group);

	return ret;
}

static int tce_iommu_attach_group(void *iommu_data,
		struct iommu_group *iommu_group)
{
	int ret;
	struct tce_container *container = iommu_data;
	struct iommu_table_group *table_group;
	struct tce_iommu_group *tcegrp = NULL;

	mutex_lock(&container->lock);

	/* pr_debug("tce_vfio: Attaching group #%u to iommu %p\n",
			iommu_group_id(iommu_group), iommu_group); */
	table_group = iommu_group_get_iommudata(iommu_group);

	if (tce_groups_attached(container) && (!table_group->ops ||
			!table_group->ops->take_ownership ||
			!table_group->ops->release_ownership)) {
		ret = -EBUSY;
		goto unlock_exit;
	}

	/* Check if new group has the same iommu_ops (i.e. compatible) */
	list_for_each_entry(tcegrp, &container->group_list, next) {
		struct iommu_table_group *table_group_tmp;

		if (tcegrp->grp == iommu_group) {
			pr_warn("tce_vfio: Group %d is already attached\n",
					iommu_group_id(iommu_group));
			ret = -EBUSY;
			goto unlock_exit;
		}
		table_group_tmp = iommu_group_get_iommudata(tcegrp->grp);
		if (table_group_tmp->ops->create_table !=
				table_group->ops->create_table) {
			pr_warn("tce_vfio: Group %d is incompatible with group %d\n",
					iommu_group_id(iommu_group),
					iommu_group_id(tcegrp->grp));
			ret = -EPERM;
			goto unlock_exit;
		}
	}

	tcegrp = kzalloc(sizeof(*tcegrp), GFP_KERNEL);
	if (!tcegrp) {
		ret = -ENOMEM;
		goto unlock_exit;
	}

	if (!table_group->ops || !table_group->ops->take_ownership ||
			!table_group->ops->release_ownership)
		ret = tce_iommu_take_ownership(container, table_group);
	else
		ret = tce_iommu_take_ownership_ddw(container, table_group);

	if (!ret) {
		tcegrp->grp = iommu_group;
		list_add(&tcegrp->next, &container->group_list);
	}

unlock_exit:
	if (ret && tcegrp)
		kfree(tcegrp);

	mutex_unlock(&container->lock);

	return ret;
}

static void tce_iommu_detach_group(void *iommu_data,
		struct iommu_group *iommu_group)
{
	struct tce_container *container = iommu_data;
	struct iommu_table_group *table_group;
	bool found = false;
	struct tce_iommu_group *tcegrp;

	mutex_lock(&container->lock);

	list_for_each_entry(tcegrp, &container->group_list, next) {
		if (tcegrp->grp == iommu_group) {
			found = true;
			break;
		}
	}

	if (!found) {
		pr_warn("tce_vfio: detaching unattached group #%u\n",
				iommu_group_id(iommu_group));
		goto unlock_exit;
	}

	list_del(&tcegrp->next);
	kfree(tcegrp);

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

