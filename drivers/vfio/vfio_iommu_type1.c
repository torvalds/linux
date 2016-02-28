/*
 * VFIO: IOMMU DMA mapping support for Type1 IOMMU
 *
 * Copyright (C) 2012 Red Hat, Inc.  All rights reserved.
 *     Author: Alex Williamson <alex.williamson@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Derived from original vfio:
 * Copyright 2010 Cisco Systems, Inc.  All rights reserved.
 * Author: Tom Lyon, pugs@cisco.com
 *
 * We arbitrarily define a Type1 IOMMU as one matching the below code.
 * It could be called the x86 IOMMU as it's designed for AMD-Vi & Intel
 * VT-d, but that makes it harder to re-use as theoretically anyone
 * implementing a similar IOMMU could make use of this.  We expect the
 * IOMMU to support the IOMMU API and have few to no restrictions around
 * the IOVA range that can be mapped.  The Type1 IOMMU is currently
 * optimized for relatively static mappings of a userspace process with
 * userpsace pages pinned into memory.  We also assume devices and IOMMU
 * domains are PCI based as the IOMMU API is still centered around a
 * device/bus interface rather than a group interface.
 */

#include <linux/compat.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/iommu.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/rbtree.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/vfio.h>
#include <linux/workqueue.h>

#define DRIVER_VERSION  "0.2"
#define DRIVER_AUTHOR   "Alex Williamson <alex.williamson@redhat.com>"
#define DRIVER_DESC     "Type1 IOMMU driver for VFIO"

static bool allow_unsafe_interrupts;
module_param_named(allow_unsafe_interrupts,
		   allow_unsafe_interrupts, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(allow_unsafe_interrupts,
		 "Enable VFIO IOMMU support for on platforms without interrupt remapping support.");

static bool disable_hugepages;
module_param_named(disable_hugepages,
		   disable_hugepages, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(disable_hugepages,
		 "Disable VFIO IOMMU support for IOMMU hugepages.");

struct vfio_iommu {
	struct list_head	domain_list;
	struct mutex		lock;
	struct rb_root		dma_list;
	bool			v2;
	bool			nesting;
};

struct vfio_domain {
	struct iommu_domain	*domain;
	struct list_head	next;
	struct list_head	group_list;
	int			prot;		/* IOMMU_CACHE */
	bool			fgsp;		/* Fine-grained super pages */
};

struct vfio_dma {
	struct rb_node		node;
	dma_addr_t		iova;		/* Device address */
	unsigned long		vaddr;		/* Process virtual addr */
	size_t			size;		/* Map size (bytes) */
	int			prot;		/* IOMMU_READ/WRITE */
};

struct vfio_group {
	struct iommu_group	*iommu_group;
	struct list_head	next;
};

/*
 * This code handles mapping and unmapping of user data buffers
 * into DMA'ble space using the IOMMU
 */

static struct vfio_dma *vfio_find_dma(struct vfio_iommu *iommu,
				      dma_addr_t start, size_t size)
{
	struct rb_node *node = iommu->dma_list.rb_node;

	while (node) {
		struct vfio_dma *dma = rb_entry(node, struct vfio_dma, node);

		if (start + size <= dma->iova)
			node = node->rb_left;
		else if (start >= dma->iova + dma->size)
			node = node->rb_right;
		else
			return dma;
	}

	return NULL;
}

static void vfio_link_dma(struct vfio_iommu *iommu, struct vfio_dma *new)
{
	struct rb_node **link = &iommu->dma_list.rb_node, *parent = NULL;
	struct vfio_dma *dma;

	while (*link) {
		parent = *link;
		dma = rb_entry(parent, struct vfio_dma, node);

		if (new->iova + new->size <= dma->iova)
			link = &(*link)->rb_left;
		else
			link = &(*link)->rb_right;
	}

	rb_link_node(&new->node, parent, link);
	rb_insert_color(&new->node, &iommu->dma_list);
}

static void vfio_unlink_dma(struct vfio_iommu *iommu, struct vfio_dma *old)
{
	rb_erase(&old->node, &iommu->dma_list);
}

struct vwork {
	struct mm_struct	*mm;
	long			npage;
	struct work_struct	work;
};

/* delayed decrement/increment for locked_vm */
static void vfio_lock_acct_bg(struct work_struct *work)
{
	struct vwork *vwork = container_of(work, struct vwork, work);
	struct mm_struct *mm;

	mm = vwork->mm;
	down_write(&mm->mmap_sem);
	mm->locked_vm += vwork->npage;
	up_write(&mm->mmap_sem);
	mmput(mm);
	kfree(vwork);
}

static void vfio_lock_acct(long npage)
{
	struct vwork *vwork;
	struct mm_struct *mm;

	if (!current->mm || !npage)
		return; /* process exited or nothing to do */

	if (down_write_trylock(&current->mm->mmap_sem)) {
		current->mm->locked_vm += npage;
		up_write(&current->mm->mmap_sem);
		return;
	}

	/*
	 * Couldn't get mmap_sem lock, so must setup to update
	 * mm->locked_vm later. If locked_vm were atomic, we
	 * wouldn't need this silliness
	 */
	vwork = kmalloc(sizeof(struct vwork), GFP_KERNEL);
	if (!vwork)
		return;
	mm = get_task_mm(current);
	if (!mm) {
		kfree(vwork);
		return;
	}
	INIT_WORK(&vwork->work, vfio_lock_acct_bg);
	vwork->mm = mm;
	vwork->npage = npage;
	schedule_work(&vwork->work);
}

/*
 * Some mappings aren't backed by a struct page, for example an mmap'd
 * MMIO range for our own or another device.  These use a different
 * pfn conversion and shouldn't be tracked as locked pages.
 */
static bool is_invalid_reserved_pfn(unsigned long pfn)
{
	if (pfn_valid(pfn)) {
		bool reserved;
		struct page *tail = pfn_to_page(pfn);
		struct page *head = compound_head(tail);
		reserved = !!(PageReserved(head));
		if (head != tail) {
			/*
			 * "head" is not a dangling pointer
			 * (compound_head takes care of that)
			 * but the hugepage may have been split
			 * from under us (and we may not hold a
			 * reference count on the head page so it can
			 * be reused before we run PageReferenced), so
			 * we've to check PageTail before returning
			 * what we just read.
			 */
			smp_rmb();
			if (PageTail(tail))
				return reserved;
		}
		return PageReserved(tail);
	}

	return true;
}

static int put_pfn(unsigned long pfn, int prot)
{
	if (!is_invalid_reserved_pfn(pfn)) {
		struct page *page = pfn_to_page(pfn);
		if (prot & IOMMU_WRITE)
			SetPageDirty(page);
		put_page(page);
		return 1;
	}
	return 0;
}

static int vaddr_get_pfn(unsigned long vaddr, int prot, unsigned long *pfn)
{
	struct page *page[1];
	struct vm_area_struct *vma;
	int ret = -EFAULT;

	if (get_user_pages_fast(vaddr, 1, !!(prot & IOMMU_WRITE), page) == 1) {
		*pfn = page_to_pfn(page[0]);
		return 0;
	}

	down_read(&current->mm->mmap_sem);

	vma = find_vma_intersection(current->mm, vaddr, vaddr + 1);

	if (vma && vma->vm_flags & VM_PFNMAP) {
		*pfn = ((vaddr - vma->vm_start) >> PAGE_SHIFT) + vma->vm_pgoff;
		if (is_invalid_reserved_pfn(*pfn))
			ret = 0;
	}

	up_read(&current->mm->mmap_sem);

	return ret;
}

/*
 * Attempt to pin pages.  We really don't want to track all the pfns and
 * the iommu can only map chunks of consecutive pfns anyway, so get the
 * first page and all consecutive pages with the same locking.
 */
static long vfio_pin_pages(unsigned long vaddr, long npage,
			   int prot, unsigned long *pfn_base)
{
	unsigned long limit = rlimit(RLIMIT_MEMLOCK) >> PAGE_SHIFT;
	bool lock_cap = capable(CAP_IPC_LOCK);
	long ret, i;
	bool rsvd;

	if (!current->mm)
		return -ENODEV;

	ret = vaddr_get_pfn(vaddr, prot, pfn_base);
	if (ret)
		return ret;

	rsvd = is_invalid_reserved_pfn(*pfn_base);

	if (!rsvd && !lock_cap && current->mm->locked_vm + 1 > limit) {
		put_pfn(*pfn_base, prot);
		pr_warn("%s: RLIMIT_MEMLOCK (%ld) exceeded\n", __func__,
			limit << PAGE_SHIFT);
		return -ENOMEM;
	}

	if (unlikely(disable_hugepages)) {
		if (!rsvd)
			vfio_lock_acct(1);
		return 1;
	}

	/* Lock all the consecutive pages from pfn_base */
	for (i = 1, vaddr += PAGE_SIZE; i < npage; i++, vaddr += PAGE_SIZE) {
		unsigned long pfn = 0;

		ret = vaddr_get_pfn(vaddr, prot, &pfn);
		if (ret)
			break;

		if (pfn != *pfn_base + i ||
		    rsvd != is_invalid_reserved_pfn(pfn)) {
			put_pfn(pfn, prot);
			break;
		}

		if (!rsvd && !lock_cap &&
		    current->mm->locked_vm + i + 1 > limit) {
			put_pfn(pfn, prot);
			pr_warn("%s: RLIMIT_MEMLOCK (%ld) exceeded\n",
				__func__, limit << PAGE_SHIFT);
			break;
		}
	}

	if (!rsvd)
		vfio_lock_acct(i);

	return i;
}

static long vfio_unpin_pages(unsigned long pfn, long npage,
			     int prot, bool do_accounting)
{
	unsigned long unlocked = 0;
	long i;

	for (i = 0; i < npage; i++)
		unlocked += put_pfn(pfn++, prot);

	if (do_accounting)
		vfio_lock_acct(-unlocked);

	return unlocked;
}

static void vfio_unmap_unpin(struct vfio_iommu *iommu, struct vfio_dma *dma)
{
	dma_addr_t iova = dma->iova, end = dma->iova + dma->size;
	struct vfio_domain *domain, *d;
	long unlocked = 0;

	if (!dma->size)
		return;
	/*
	 * We use the IOMMU to track the physical addresses, otherwise we'd
	 * need a much more complicated tracking system.  Unfortunately that
	 * means we need to use one of the iommu domains to figure out the
	 * pfns to unpin.  The rest need to be unmapped in advance so we have
	 * no iommu translations remaining when the pages are unpinned.
	 */
	domain = d = list_first_entry(&iommu->domain_list,
				      struct vfio_domain, next);

	list_for_each_entry_continue(d, &iommu->domain_list, next) {
		iommu_unmap(d->domain, dma->iova, dma->size);
		cond_resched();
	}

	while (iova < end) {
		size_t unmapped, len;
		phys_addr_t phys, next;

		phys = iommu_iova_to_phys(domain->domain, iova);
		if (WARN_ON(!phys)) {
			iova += PAGE_SIZE;
			continue;
		}

		/*
		 * To optimize for fewer iommu_unmap() calls, each of which
		 * may require hardware cache flushing, try to find the
		 * largest contiguous physical memory chunk to unmap.
		 */
		for (len = PAGE_SIZE;
		     !domain->fgsp && iova + len < end; len += PAGE_SIZE) {
			next = iommu_iova_to_phys(domain->domain, iova + len);
			if (next != phys + len)
				break;
		}

		unmapped = iommu_unmap(domain->domain, iova, len);
		if (WARN_ON(!unmapped))
			break;

		unlocked += vfio_unpin_pages(phys >> PAGE_SHIFT,
					     unmapped >> PAGE_SHIFT,
					     dma->prot, false);
		iova += unmapped;

		cond_resched();
	}

	vfio_lock_acct(-unlocked);
}

static void vfio_remove_dma(struct vfio_iommu *iommu, struct vfio_dma *dma)
{
	vfio_unmap_unpin(iommu, dma);
	vfio_unlink_dma(iommu, dma);
	kfree(dma);
}

static unsigned long vfio_pgsize_bitmap(struct vfio_iommu *iommu)
{
	struct vfio_domain *domain;
	unsigned long bitmap = ULONG_MAX;

	mutex_lock(&iommu->lock);
	list_for_each_entry(domain, &iommu->domain_list, next)
		bitmap &= domain->domain->ops->pgsize_bitmap;
	mutex_unlock(&iommu->lock);

	/*
	 * In case the IOMMU supports page sizes smaller than PAGE_SIZE
	 * we pretend PAGE_SIZE is supported and hide sub-PAGE_SIZE sizes.
	 * That way the user will be able to map/unmap buffers whose size/
	 * start address is aligned with PAGE_SIZE. Pinning code uses that
	 * granularity while iommu driver can use the sub-PAGE_SIZE size
	 * to map the buffer.
	 */
	if (bitmap & ~PAGE_MASK) {
		bitmap &= PAGE_MASK;
		bitmap |= PAGE_SIZE;
	}

	return bitmap;
}

static int vfio_dma_do_unmap(struct vfio_iommu *iommu,
			     struct vfio_iommu_type1_dma_unmap *unmap)
{
	uint64_t mask;
	struct vfio_dma *dma;
	size_t unmapped = 0;
	int ret = 0;

	mask = ((uint64_t)1 << __ffs(vfio_pgsize_bitmap(iommu))) - 1;

	if (unmap->iova & mask)
		return -EINVAL;
	if (!unmap->size || unmap->size & mask)
		return -EINVAL;

	WARN_ON(mask & PAGE_MASK);

	mutex_lock(&iommu->lock);

	/*
	 * vfio-iommu-type1 (v1) - User mappings were coalesced together to
	 * avoid tracking individual mappings.  This means that the granularity
	 * of the original mapping was lost and the user was allowed to attempt
	 * to unmap any range.  Depending on the contiguousness of physical
	 * memory and page sizes supported by the IOMMU, arbitrary unmaps may
	 * or may not have worked.  We only guaranteed unmap granularity
	 * matching the original mapping; even though it was untracked here,
	 * the original mappings are reflected in IOMMU mappings.  This
	 * resulted in a couple unusual behaviors.  First, if a range is not
	 * able to be unmapped, ex. a set of 4k pages that was mapped as a
	 * 2M hugepage into the IOMMU, the unmap ioctl returns success but with
	 * a zero sized unmap.  Also, if an unmap request overlaps the first
	 * address of a hugepage, the IOMMU will unmap the entire hugepage.
	 * This also returns success and the returned unmap size reflects the
	 * actual size unmapped.
	 *
	 * We attempt to maintain compatibility with this "v1" interface, but
	 * we take control out of the hands of the IOMMU.  Therefore, an unmap
	 * request offset from the beginning of the original mapping will
	 * return success with zero sized unmap.  And an unmap request covering
	 * the first iova of mapping will unmap the entire range.
	 *
	 * The v2 version of this interface intends to be more deterministic.
	 * Unmap requests must fully cover previous mappings.  Multiple
	 * mappings may still be unmaped by specifying large ranges, but there
	 * must not be any previous mappings bisected by the range.  An error
	 * will be returned if these conditions are not met.  The v2 interface
	 * will only return success and a size of zero if there were no
	 * mappings within the range.
	 */
	if (iommu->v2) {
		dma = vfio_find_dma(iommu, unmap->iova, 0);
		if (dma && dma->iova != unmap->iova) {
			ret = -EINVAL;
			goto unlock;
		}
		dma = vfio_find_dma(iommu, unmap->iova + unmap->size - 1, 0);
		if (dma && dma->iova + dma->size != unmap->iova + unmap->size) {
			ret = -EINVAL;
			goto unlock;
		}
	}

	while ((dma = vfio_find_dma(iommu, unmap->iova, unmap->size))) {
		if (!iommu->v2 && unmap->iova > dma->iova)
			break;
		unmapped += dma->size;
		vfio_remove_dma(iommu, dma);
	}

unlock:
	mutex_unlock(&iommu->lock);

	/* Report how much was unmapped */
	unmap->size = unmapped;

	return ret;
}

/*
 * Turns out AMD IOMMU has a page table bug where it won't map large pages
 * to a region that previously mapped smaller pages.  This should be fixed
 * soon, so this is just a temporary workaround to break mappings down into
 * PAGE_SIZE.  Better to map smaller pages than nothing.
 */
static int map_try_harder(struct vfio_domain *domain, dma_addr_t iova,
			  unsigned long pfn, long npage, int prot)
{
	long i;
	int ret;

	for (i = 0; i < npage; i++, pfn++, iova += PAGE_SIZE) {
		ret = iommu_map(domain->domain, iova,
				(phys_addr_t)pfn << PAGE_SHIFT,
				PAGE_SIZE, prot | domain->prot);
		if (ret)
			break;
	}

	for (; i < npage && i > 0; i--, iova -= PAGE_SIZE)
		iommu_unmap(domain->domain, iova, PAGE_SIZE);

	return ret;
}

static int vfio_iommu_map(struct vfio_iommu *iommu, dma_addr_t iova,
			  unsigned long pfn, long npage, int prot)
{
	struct vfio_domain *d;
	int ret;

	list_for_each_entry(d, &iommu->domain_list, next) {
		ret = iommu_map(d->domain, iova, (phys_addr_t)pfn << PAGE_SHIFT,
				npage << PAGE_SHIFT, prot | d->prot);
		if (ret) {
			if (ret != -EBUSY ||
			    map_try_harder(d, iova, pfn, npage, prot))
				goto unwind;
		}

		cond_resched();
	}

	return 0;

unwind:
	list_for_each_entry_continue_reverse(d, &iommu->domain_list, next)
		iommu_unmap(d->domain, iova, npage << PAGE_SHIFT);

	return ret;
}

static int vfio_dma_do_map(struct vfio_iommu *iommu,
			   struct vfio_iommu_type1_dma_map *map)
{
	dma_addr_t iova = map->iova;
	unsigned long vaddr = map->vaddr;
	size_t size = map->size;
	long npage;
	int ret = 0, prot = 0;
	uint64_t mask;
	struct vfio_dma *dma;
	unsigned long pfn;

	/* Verify that none of our __u64 fields overflow */
	if (map->size != size || map->vaddr != vaddr || map->iova != iova)
		return -EINVAL;

	mask = ((uint64_t)1 << __ffs(vfio_pgsize_bitmap(iommu))) - 1;

	WARN_ON(mask & PAGE_MASK);

	/* READ/WRITE from device perspective */
	if (map->flags & VFIO_DMA_MAP_FLAG_WRITE)
		prot |= IOMMU_WRITE;
	if (map->flags & VFIO_DMA_MAP_FLAG_READ)
		prot |= IOMMU_READ;

	if (!prot || !size || (size | iova | vaddr) & mask)
		return -EINVAL;

	/* Don't allow IOVA or virtual address wrap */
	if (iova + size - 1 < iova || vaddr + size - 1 < vaddr)
		return -EINVAL;

	mutex_lock(&iommu->lock);

	if (vfio_find_dma(iommu, iova, size)) {
		mutex_unlock(&iommu->lock);
		return -EEXIST;
	}

	dma = kzalloc(sizeof(*dma), GFP_KERNEL);
	if (!dma) {
		mutex_unlock(&iommu->lock);
		return -ENOMEM;
	}

	dma->iova = iova;
	dma->vaddr = vaddr;
	dma->prot = prot;

	/* Insert zero-sized and grow as we map chunks of it */
	vfio_link_dma(iommu, dma);

	while (size) {
		/* Pin a contiguous chunk of memory */
		npage = vfio_pin_pages(vaddr + dma->size,
				       size >> PAGE_SHIFT, prot, &pfn);
		if (npage <= 0) {
			WARN_ON(!npage);
			ret = (int)npage;
			break;
		}

		/* Map it! */
		ret = vfio_iommu_map(iommu, iova + dma->size, pfn, npage, prot);
		if (ret) {
			vfio_unpin_pages(pfn, npage, prot, true);
			break;
		}

		size -= npage << PAGE_SHIFT;
		dma->size += npage << PAGE_SHIFT;
	}

	if (ret)
		vfio_remove_dma(iommu, dma);

	mutex_unlock(&iommu->lock);
	return ret;
}

static int vfio_bus_type(struct device *dev, void *data)
{
	struct bus_type **bus = data;

	if (*bus && *bus != dev->bus)
		return -EINVAL;

	*bus = dev->bus;

	return 0;
}

static int vfio_iommu_replay(struct vfio_iommu *iommu,
			     struct vfio_domain *domain)
{
	struct vfio_domain *d;
	struct rb_node *n;
	int ret;

	/* Arbitrarily pick the first domain in the list for lookups */
	d = list_first_entry(&iommu->domain_list, struct vfio_domain, next);
	n = rb_first(&iommu->dma_list);

	/* If there's not a domain, there better not be any mappings */
	if (WARN_ON(n && !d))
		return -EINVAL;

	for (; n; n = rb_next(n)) {
		struct vfio_dma *dma;
		dma_addr_t iova;

		dma = rb_entry(n, struct vfio_dma, node);
		iova = dma->iova;

		while (iova < dma->iova + dma->size) {
			phys_addr_t phys = iommu_iova_to_phys(d->domain, iova);
			size_t size;

			if (WARN_ON(!phys)) {
				iova += PAGE_SIZE;
				continue;
			}

			size = PAGE_SIZE;

			while (iova + size < dma->iova + dma->size &&
			       phys + size == iommu_iova_to_phys(d->domain,
								 iova + size))
				size += PAGE_SIZE;

			ret = iommu_map(domain->domain, iova, phys,
					size, dma->prot | domain->prot);
			if (ret)
				return ret;

			iova += size;
		}
	}

	return 0;
}

/*
 * We change our unmap behavior slightly depending on whether the IOMMU
 * supports fine-grained superpages.  IOMMUs like AMD-Vi will use a superpage
 * for practically any contiguous power-of-two mapping we give it.  This means
 * we don't need to look for contiguous chunks ourselves to make unmapping
 * more efficient.  On IOMMUs with coarse-grained super pages, like Intel VT-d
 * with discrete 2M/1G/512G/1T superpages, identifying contiguous chunks
 * significantly boosts non-hugetlbfs mappings and doesn't seem to hurt when
 * hugetlbfs is in use.
 */
static void vfio_test_domain_fgsp(struct vfio_domain *domain)
{
	struct page *pages;
	int ret, order = get_order(PAGE_SIZE * 2);

	pages = alloc_pages(GFP_KERNEL | __GFP_ZERO, order);
	if (!pages)
		return;

	ret = iommu_map(domain->domain, 0, page_to_phys(pages), PAGE_SIZE * 2,
			IOMMU_READ | IOMMU_WRITE | domain->prot);
	if (!ret) {
		size_t unmapped = iommu_unmap(domain->domain, 0, PAGE_SIZE);

		if (unmapped == PAGE_SIZE)
			iommu_unmap(domain->domain, PAGE_SIZE, PAGE_SIZE);
		else
			domain->fgsp = true;
	}

	__free_pages(pages, order);
}

static int vfio_iommu_type1_attach_group(void *iommu_data,
					 struct iommu_group *iommu_group)
{
	struct vfio_iommu *iommu = iommu_data;
	struct vfio_group *group, *g;
	struct vfio_domain *domain, *d;
	struct bus_type *bus = NULL;
	int ret;

	mutex_lock(&iommu->lock);

	list_for_each_entry(d, &iommu->domain_list, next) {
		list_for_each_entry(g, &d->group_list, next) {
			if (g->iommu_group != iommu_group)
				continue;

			mutex_unlock(&iommu->lock);
			return -EINVAL;
		}
	}

	group = kzalloc(sizeof(*group), GFP_KERNEL);
	domain = kzalloc(sizeof(*domain), GFP_KERNEL);
	if (!group || !domain) {
		ret = -ENOMEM;
		goto out_free;
	}

	group->iommu_group = iommu_group;

	/* Determine bus_type in order to allocate a domain */
	ret = iommu_group_for_each_dev(iommu_group, &bus, vfio_bus_type);
	if (ret)
		goto out_free;

	domain->domain = iommu_domain_alloc(bus);
	if (!domain->domain) {
		ret = -EIO;
		goto out_free;
	}

	if (iommu->nesting) {
		int attr = 1;

		ret = iommu_domain_set_attr(domain->domain, DOMAIN_ATTR_NESTING,
					    &attr);
		if (ret)
			goto out_domain;
	}

	ret = iommu_attach_group(domain->domain, iommu_group);
	if (ret)
		goto out_domain;

	INIT_LIST_HEAD(&domain->group_list);
	list_add(&group->next, &domain->group_list);

	if (!allow_unsafe_interrupts &&
	    !iommu_capable(bus, IOMMU_CAP_INTR_REMAP)) {
		pr_warn("%s: No interrupt remapping support.  Use the module param \"allow_unsafe_interrupts\" to enable VFIO IOMMU support on this platform\n",
		       __func__);
		ret = -EPERM;
		goto out_detach;
	}

	if (iommu_capable(bus, IOMMU_CAP_CACHE_COHERENCY))
		domain->prot |= IOMMU_CACHE;

	/*
	 * Try to match an existing compatible domain.  We don't want to
	 * preclude an IOMMU driver supporting multiple bus_types and being
	 * able to include different bus_types in the same IOMMU domain, so
	 * we test whether the domains use the same iommu_ops rather than
	 * testing if they're on the same bus_type.
	 */
	list_for_each_entry(d, &iommu->domain_list, next) {
		if (d->domain->ops == domain->domain->ops &&
		    d->prot == domain->prot) {
			iommu_detach_group(domain->domain, iommu_group);
			if (!iommu_attach_group(d->domain, iommu_group)) {
				list_add(&group->next, &d->group_list);
				iommu_domain_free(domain->domain);
				kfree(domain);
				mutex_unlock(&iommu->lock);
				return 0;
			}

			ret = iommu_attach_group(domain->domain, iommu_group);
			if (ret)
				goto out_domain;
		}
	}

	vfio_test_domain_fgsp(domain);

	/* replay mappings on new domains */
	ret = vfio_iommu_replay(iommu, domain);
	if (ret)
		goto out_detach;

	list_add(&domain->next, &iommu->domain_list);

	mutex_unlock(&iommu->lock);

	return 0;

out_detach:
	iommu_detach_group(domain->domain, iommu_group);
out_domain:
	iommu_domain_free(domain->domain);
out_free:
	kfree(domain);
	kfree(group);
	mutex_unlock(&iommu->lock);
	return ret;
}

static void vfio_iommu_unmap_unpin_all(struct vfio_iommu *iommu)
{
	struct rb_node *node;

	while ((node = rb_first(&iommu->dma_list)))
		vfio_remove_dma(iommu, rb_entry(node, struct vfio_dma, node));
}

static void vfio_iommu_type1_detach_group(void *iommu_data,
					  struct iommu_group *iommu_group)
{
	struct vfio_iommu *iommu = iommu_data;
	struct vfio_domain *domain;
	struct vfio_group *group;

	mutex_lock(&iommu->lock);

	list_for_each_entry(domain, &iommu->domain_list, next) {
		list_for_each_entry(group, &domain->group_list, next) {
			if (group->iommu_group != iommu_group)
				continue;

			iommu_detach_group(domain->domain, iommu_group);
			list_del(&group->next);
			kfree(group);
			/*
			 * Group ownership provides privilege, if the group
			 * list is empty, the domain goes away.  If it's the
			 * last domain, then all the mappings go away too.
			 */
			if (list_empty(&domain->group_list)) {
				if (list_is_singular(&iommu->domain_list))
					vfio_iommu_unmap_unpin_all(iommu);
				iommu_domain_free(domain->domain);
				list_del(&domain->next);
				kfree(domain);
			}
			goto done;
		}
	}

done:
	mutex_unlock(&iommu->lock);
}

static void *vfio_iommu_type1_open(unsigned long arg)
{
	struct vfio_iommu *iommu;

	iommu = kzalloc(sizeof(*iommu), GFP_KERNEL);
	if (!iommu)
		return ERR_PTR(-ENOMEM);

	switch (arg) {
	case VFIO_TYPE1_IOMMU:
		break;
	case VFIO_TYPE1_NESTING_IOMMU:
		iommu->nesting = true;
	case VFIO_TYPE1v2_IOMMU:
		iommu->v2 = true;
		break;
	default:
		kfree(iommu);
		return ERR_PTR(-EINVAL);
	}

	INIT_LIST_HEAD(&iommu->domain_list);
	iommu->dma_list = RB_ROOT;
	mutex_init(&iommu->lock);

	return iommu;
}

static void vfio_iommu_type1_release(void *iommu_data)
{
	struct vfio_iommu *iommu = iommu_data;
	struct vfio_domain *domain, *domain_tmp;
	struct vfio_group *group, *group_tmp;

	vfio_iommu_unmap_unpin_all(iommu);

	list_for_each_entry_safe(domain, domain_tmp,
				 &iommu->domain_list, next) {
		list_for_each_entry_safe(group, group_tmp,
					 &domain->group_list, next) {
			iommu_detach_group(domain->domain, group->iommu_group);
			list_del(&group->next);
			kfree(group);
		}
		iommu_domain_free(domain->domain);
		list_del(&domain->next);
		kfree(domain);
	}

	kfree(iommu);
}

static int vfio_domains_have_iommu_cache(struct vfio_iommu *iommu)
{
	struct vfio_domain *domain;
	int ret = 1;

	mutex_lock(&iommu->lock);
	list_for_each_entry(domain, &iommu->domain_list, next) {
		if (!(domain->prot & IOMMU_CACHE)) {
			ret = 0;
			break;
		}
	}
	mutex_unlock(&iommu->lock);

	return ret;
}

static long vfio_iommu_type1_ioctl(void *iommu_data,
				   unsigned int cmd, unsigned long arg)
{
	struct vfio_iommu *iommu = iommu_data;
	unsigned long minsz;

	if (cmd == VFIO_CHECK_EXTENSION) {
		switch (arg) {
		case VFIO_TYPE1_IOMMU:
		case VFIO_TYPE1v2_IOMMU:
		case VFIO_TYPE1_NESTING_IOMMU:
			return 1;
		case VFIO_DMA_CC_IOMMU:
			if (!iommu)
				return 0;
			return vfio_domains_have_iommu_cache(iommu);
		default:
			return 0;
		}
	} else if (cmd == VFIO_IOMMU_GET_INFO) {
		struct vfio_iommu_type1_info info;

		minsz = offsetofend(struct vfio_iommu_type1_info, iova_pgsizes);

		if (copy_from_user(&info, (void __user *)arg, minsz))
			return -EFAULT;

		if (info.argsz < minsz)
			return -EINVAL;

		info.flags = 0;

		info.iova_pgsizes = vfio_pgsize_bitmap(iommu);

		return copy_to_user((void __user *)arg, &info, minsz) ?
			-EFAULT : 0;

	} else if (cmd == VFIO_IOMMU_MAP_DMA) {
		struct vfio_iommu_type1_dma_map map;
		uint32_t mask = VFIO_DMA_MAP_FLAG_READ |
				VFIO_DMA_MAP_FLAG_WRITE;

		minsz = offsetofend(struct vfio_iommu_type1_dma_map, size);

		if (copy_from_user(&map, (void __user *)arg, minsz))
			return -EFAULT;

		if (map.argsz < minsz || map.flags & ~mask)
			return -EINVAL;

		return vfio_dma_do_map(iommu, &map);

	} else if (cmd == VFIO_IOMMU_UNMAP_DMA) {
		struct vfio_iommu_type1_dma_unmap unmap;
		long ret;

		minsz = offsetofend(struct vfio_iommu_type1_dma_unmap, size);

		if (copy_from_user(&unmap, (void __user *)arg, minsz))
			return -EFAULT;

		if (unmap.argsz < minsz || unmap.flags)
			return -EINVAL;

		ret = vfio_dma_do_unmap(iommu, &unmap);
		if (ret)
			return ret;

		return copy_to_user((void __user *)arg, &unmap, minsz) ?
			-EFAULT : 0;
	}

	return -ENOTTY;
}

static const struct vfio_iommu_driver_ops vfio_iommu_driver_ops_type1 = {
	.name		= "vfio-iommu-type1",
	.owner		= THIS_MODULE,
	.open		= vfio_iommu_type1_open,
	.release	= vfio_iommu_type1_release,
	.ioctl		= vfio_iommu_type1_ioctl,
	.attach_group	= vfio_iommu_type1_attach_group,
	.detach_group	= vfio_iommu_type1_detach_group,
};

static int __init vfio_iommu_type1_init(void)
{
	return vfio_register_iommu_driver(&vfio_iommu_driver_ops_type1);
}

static void __exit vfio_iommu_type1_cleanup(void)
{
	vfio_unregister_iommu_driver(&vfio_iommu_driver_ops_type1);
}

module_init(vfio_iommu_type1_init);
module_exit(vfio_iommu_type1_cleanup);

MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
