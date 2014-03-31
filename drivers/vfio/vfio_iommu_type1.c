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
#include <linux/pci.h>		/* pci_bus_type */
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
	struct iommu_domain	*domain;
	struct mutex		lock;
	struct rb_root		dma_list;
	struct list_head	group_list;
	bool			cache;
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

static void vfio_insert_dma(struct vfio_iommu *iommu, struct vfio_dma *new)
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

static void vfio_remove_dma(struct vfio_iommu *iommu, struct vfio_dma *old)
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

	if (!current->mm)
		return -ENODEV;

	ret = vaddr_get_pfn(vaddr, prot, pfn_base);
	if (ret)
		return ret;

	if (is_invalid_reserved_pfn(*pfn_base))
		return 1;

	if (!lock_cap && current->mm->locked_vm + 1 > limit) {
		put_pfn(*pfn_base, prot);
		pr_warn("%s: RLIMIT_MEMLOCK (%ld) exceeded\n", __func__,
			limit << PAGE_SHIFT);
		return -ENOMEM;
	}

	if (unlikely(disable_hugepages)) {
		vfio_lock_acct(1);
		return 1;
	}

	/* Lock all the consecutive pages from pfn_base */
	for (i = 1, vaddr += PAGE_SIZE; i < npage; i++, vaddr += PAGE_SIZE) {
		unsigned long pfn = 0;

		ret = vaddr_get_pfn(vaddr, prot, &pfn);
		if (ret)
			break;

		if (pfn != *pfn_base + i || is_invalid_reserved_pfn(pfn)) {
			put_pfn(pfn, prot);
			break;
		}

		if (!lock_cap && current->mm->locked_vm + i + 1 > limit) {
			put_pfn(pfn, prot);
			pr_warn("%s: RLIMIT_MEMLOCK (%ld) exceeded\n",
				__func__, limit << PAGE_SHIFT);
			break;
		}
	}

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

static int vfio_unmap_unpin(struct vfio_iommu *iommu, struct vfio_dma *dma,
			    dma_addr_t iova, size_t *size)
{
	dma_addr_t start = iova, end = iova + *size;
	long unlocked = 0;

	while (iova < end) {
		size_t unmapped;
		phys_addr_t phys;

		/*
		 * We use the IOMMU to track the physical address.  This
		 * saves us from having a lot more entries in our mapping
		 * tree.  The downside is that we don't track the size
		 * used to do the mapping.  We request unmap of a single
		 * page, but expect IOMMUs that support large pages to
		 * unmap a larger chunk.
		 */
		phys = iommu_iova_to_phys(iommu->domain, iova);
		if (WARN_ON(!phys)) {
			iova += PAGE_SIZE;
			continue;
		}

		unmapped = iommu_unmap(iommu->domain, iova, PAGE_SIZE);
		if (!unmapped)
			break;

		unlocked += vfio_unpin_pages(phys >> PAGE_SHIFT,
					     unmapped >> PAGE_SHIFT,
					     dma->prot, false);
		iova += unmapped;
	}

	vfio_lock_acct(-unlocked);

	*size = iova - start;

	return 0;
}

static int vfio_remove_dma_overlap(struct vfio_iommu *iommu, dma_addr_t start,
				   size_t *size, struct vfio_dma *dma)
{
	size_t offset, overlap, tmp;
	struct vfio_dma *split;
	int ret;

	if (!*size)
		return 0;

	/*
	 * Existing dma region is completely covered, unmap all.  This is
	 * the likely case since userspace tends to map and unmap buffers
	 * in one shot rather than multiple mappings within a buffer.
	 */
	if (likely(start <= dma->iova &&
		   start + *size >= dma->iova + dma->size)) {
		*size = dma->size;
		ret = vfio_unmap_unpin(iommu, dma, dma->iova, size);
		if (ret)
			return ret;

		/*
		 * Did we remove more than we have?  Should never happen
		 * since a vfio_dma is contiguous in iova and vaddr.
		 */
		WARN_ON(*size != dma->size);

		vfio_remove_dma(iommu, dma);
		kfree(dma);
		return 0;
	}

	/* Overlap low address of existing range */
	if (start <= dma->iova) {
		overlap = start + *size - dma->iova;
		ret = vfio_unmap_unpin(iommu, dma, dma->iova, &overlap);
		if (ret)
			return ret;

		vfio_remove_dma(iommu, dma);

		/*
		 * Check, we may have removed to whole vfio_dma.  If not
		 * fixup and re-insert.
		 */
		if (overlap < dma->size) {
			dma->iova += overlap;
			dma->vaddr += overlap;
			dma->size -= overlap;
			vfio_insert_dma(iommu, dma);
		} else
			kfree(dma);

		*size = overlap;
		return 0;
	}

	/* Overlap high address of existing range */
	if (start + *size >= dma->iova + dma->size) {
		offset = start - dma->iova;
		overlap = dma->size - offset;

		ret = vfio_unmap_unpin(iommu, dma, start, &overlap);
		if (ret)
			return ret;

		dma->size -= overlap;
		*size = overlap;
		return 0;
	}

	/* Split existing */

	/*
	 * Allocate our tracking structure early even though it may not
	 * be used.  An Allocation failure later loses track of pages and
	 * is more difficult to unwind.
	 */
	split = kzalloc(sizeof(*split), GFP_KERNEL);
	if (!split)
		return -ENOMEM;

	offset = start - dma->iova;

	ret = vfio_unmap_unpin(iommu, dma, start, size);
	if (ret || !*size) {
		kfree(split);
		return ret;
	}

	tmp = dma->size;

	/* Resize the lower vfio_dma in place, before the below insert */
	dma->size = offset;

	/* Insert new for remainder, assuming it didn't all get unmapped */
	if (likely(offset + *size < tmp)) {
		split->size = tmp - offset - *size;
		split->iova = dma->iova + offset + *size;
		split->vaddr = dma->vaddr + offset + *size;
		split->prot = dma->prot;
		vfio_insert_dma(iommu, split);
	} else
		kfree(split);

	return 0;
}

static int vfio_dma_do_unmap(struct vfio_iommu *iommu,
			     struct vfio_iommu_type1_dma_unmap *unmap)
{
	uint64_t mask;
	struct vfio_dma *dma;
	size_t unmapped = 0, size;
	int ret = 0;

	mask = ((uint64_t)1 << __ffs(iommu->domain->ops->pgsize_bitmap)) - 1;

	if (unmap->iova & mask)
		return -EINVAL;
	if (!unmap->size || unmap->size & mask)
		return -EINVAL;

	WARN_ON(mask & PAGE_MASK);

	mutex_lock(&iommu->lock);

	while ((dma = vfio_find_dma(iommu, unmap->iova, unmap->size))) {
		size = unmap->size;
		ret = vfio_remove_dma_overlap(iommu, unmap->iova, &size, dma);
		if (ret || !size)
			break;
		unmapped += size;
	}

	mutex_unlock(&iommu->lock);

	/*
	 * We may unmap more than requested, update the unmap struct so
	 * userspace can know.
	 */
	unmap->size = unmapped;

	return ret;
}

/*
 * Turns out AMD IOMMU has a page table bug where it won't map large pages
 * to a region that previously mapped smaller pages.  This should be fixed
 * soon, so this is just a temporary workaround to break mappings down into
 * PAGE_SIZE.  Better to map smaller pages than nothing.
 */
static int map_try_harder(struct vfio_iommu *iommu, dma_addr_t iova,
			  unsigned long pfn, long npage, int prot)
{
	long i;
	int ret;

	for (i = 0; i < npage; i++, pfn++, iova += PAGE_SIZE) {
		ret = iommu_map(iommu->domain, iova,
				(phys_addr_t)pfn << PAGE_SHIFT,
				PAGE_SIZE, prot);
		if (ret)
			break;
	}

	for (; i < npage && i > 0; i--, iova -= PAGE_SIZE)
		iommu_unmap(iommu->domain, iova, PAGE_SIZE);

	return ret;
}

static int vfio_dma_do_map(struct vfio_iommu *iommu,
			   struct vfio_iommu_type1_dma_map *map)
{
	dma_addr_t end, iova;
	unsigned long vaddr = map->vaddr;
	size_t size = map->size;
	long npage;
	int ret = 0, prot = 0;
	uint64_t mask;
	struct vfio_dma *dma = NULL;
	unsigned long pfn;

	end = map->iova + map->size;

	mask = ((uint64_t)1 << __ffs(iommu->domain->ops->pgsize_bitmap)) - 1;

	/* READ/WRITE from device perspective */
	if (map->flags & VFIO_DMA_MAP_FLAG_WRITE)
		prot |= IOMMU_WRITE;
	if (map->flags & VFIO_DMA_MAP_FLAG_READ)
		prot |= IOMMU_READ;

	if (!prot)
		return -EINVAL; /* No READ/WRITE? */

	if (iommu->cache)
		prot |= IOMMU_CACHE;

	if (vaddr & mask)
		return -EINVAL;
	if (map->iova & mask)
		return -EINVAL;
	if (!map->size || map->size & mask)
		return -EINVAL;

	WARN_ON(mask & PAGE_MASK);

	/* Don't allow IOVA wrap */
	if (end && end < map->iova)
		return -EINVAL;

	/* Don't allow virtual address wrap */
	if (vaddr + map->size && vaddr + map->size < vaddr)
		return -EINVAL;

	mutex_lock(&iommu->lock);

	if (vfio_find_dma(iommu, map->iova, map->size)) {
		mutex_unlock(&iommu->lock);
		return -EEXIST;
	}

	for (iova = map->iova; iova < end; iova += size, vaddr += size) {
		long i;

		/* Pin a contiguous chunk of memory */
		npage = vfio_pin_pages(vaddr, (end - iova) >> PAGE_SHIFT,
				       prot, &pfn);
		if (npage <= 0) {
			WARN_ON(!npage);
			ret = (int)npage;
			goto out;
		}

		/* Verify pages are not already mapped */
		for (i = 0; i < npage; i++) {
			if (iommu_iova_to_phys(iommu->domain,
					       iova + (i << PAGE_SHIFT))) {
				ret = -EBUSY;
				goto out_unpin;
			}
		}

		ret = iommu_map(iommu->domain, iova,
				(phys_addr_t)pfn << PAGE_SHIFT,
				npage << PAGE_SHIFT, prot);
		if (ret) {
			if (ret != -EBUSY ||
			    map_try_harder(iommu, iova, pfn, npage, prot)) {
				goto out_unpin;
			}
		}

		size = npage << PAGE_SHIFT;

		/*
		 * Check if we abut a region below - nothing below 0.
		 * This is the most likely case when mapping chunks of
		 * physically contiguous regions within a virtual address
		 * range.  Update the abutting entry in place since iova
		 * doesn't change.
		 */
		if (likely(iova)) {
			struct vfio_dma *tmp;
			tmp = vfio_find_dma(iommu, iova - 1, 1);
			if (tmp && tmp->prot == prot &&
			    tmp->vaddr + tmp->size == vaddr) {
				tmp->size += size;
				iova = tmp->iova;
				size = tmp->size;
				vaddr = tmp->vaddr;
				dma = tmp;
			}
		}

		/*
		 * Check if we abut a region above - nothing above ~0 + 1.
		 * If we abut above and below, remove and free.  If only
		 * abut above, remove, modify, reinsert.
		 */
		if (likely(iova + size)) {
			struct vfio_dma *tmp;
			tmp = vfio_find_dma(iommu, iova + size, 1);
			if (tmp && tmp->prot == prot &&
			    tmp->vaddr == vaddr + size) {
				vfio_remove_dma(iommu, tmp);
				if (dma) {
					dma->size += tmp->size;
					kfree(tmp);
				} else {
					size += tmp->size;
					tmp->size = size;
					tmp->iova = iova;
					tmp->vaddr = vaddr;
					vfio_insert_dma(iommu, tmp);
					dma = tmp;
				}
			}
		}

		if (!dma) {
			dma = kzalloc(sizeof(*dma), GFP_KERNEL);
			if (!dma) {
				iommu_unmap(iommu->domain, iova, size);
				ret = -ENOMEM;
				goto out_unpin;
			}

			dma->size = size;
			dma->iova = iova;
			dma->vaddr = vaddr;
			dma->prot = prot;
			vfio_insert_dma(iommu, dma);
		}
	}

	WARN_ON(ret);
	mutex_unlock(&iommu->lock);
	return ret;

out_unpin:
	vfio_unpin_pages(pfn, npage, prot, true);

out:
	iova = map->iova;
	size = map->size;
	while ((dma = vfio_find_dma(iommu, iova, size))) {
		int r = vfio_remove_dma_overlap(iommu, iova,
						&size, dma);
		if (WARN_ON(r || !size))
			break;
	}

	mutex_unlock(&iommu->lock);
	return ret;
}

static int vfio_iommu_type1_attach_group(void *iommu_data,
					 struct iommu_group *iommu_group)
{
	struct vfio_iommu *iommu = iommu_data;
	struct vfio_group *group, *tmp;
	int ret;

	group = kzalloc(sizeof(*group), GFP_KERNEL);
	if (!group)
		return -ENOMEM;

	mutex_lock(&iommu->lock);

	list_for_each_entry(tmp, &iommu->group_list, next) {
		if (tmp->iommu_group == iommu_group) {
			mutex_unlock(&iommu->lock);
			kfree(group);
			return -EINVAL;
		}
	}

	/*
	 * TODO: Domain have capabilities that might change as we add
	 * groups (see iommu->cache, currently never set).  Check for
	 * them and potentially disallow groups to be attached when it
	 * would change capabilities (ugh).
	 */
	ret = iommu_attach_group(iommu->domain, iommu_group);
	if (ret) {
		mutex_unlock(&iommu->lock);
		kfree(group);
		return ret;
	}

	group->iommu_group = iommu_group;
	list_add(&group->next, &iommu->group_list);

	mutex_unlock(&iommu->lock);

	return 0;
}

static void vfio_iommu_type1_detach_group(void *iommu_data,
					  struct iommu_group *iommu_group)
{
	struct vfio_iommu *iommu = iommu_data;
	struct vfio_group *group;

	mutex_lock(&iommu->lock);

	list_for_each_entry(group, &iommu->group_list, next) {
		if (group->iommu_group == iommu_group) {
			iommu_detach_group(iommu->domain, iommu_group);
			list_del(&group->next);
			kfree(group);
			break;
		}
	}

	mutex_unlock(&iommu->lock);
}

static void *vfio_iommu_type1_open(unsigned long arg)
{
	struct vfio_iommu *iommu;

	if (arg != VFIO_TYPE1_IOMMU)
		return ERR_PTR(-EINVAL);

	iommu = kzalloc(sizeof(*iommu), GFP_KERNEL);
	if (!iommu)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&iommu->group_list);
	iommu->dma_list = RB_ROOT;
	mutex_init(&iommu->lock);

	/*
	 * Wish we didn't have to know about bus_type here.
	 */
	iommu->domain = iommu_domain_alloc(&pci_bus_type);
	if (!iommu->domain) {
		kfree(iommu);
		return ERR_PTR(-EIO);
	}

	/*
	 * Wish we could specify required capabilities rather than create
	 * a domain, see what comes out and hope it doesn't change along
	 * the way.  Fortunately we know interrupt remapping is global for
	 * our iommus.
	 */
	if (!allow_unsafe_interrupts &&
	    !iommu_domain_has_cap(iommu->domain, IOMMU_CAP_INTR_REMAP)) {
		pr_warn("%s: No interrupt remapping support.  Use the module param \"allow_unsafe_interrupts\" to enable VFIO IOMMU support on this platform\n",
		       __func__);
		iommu_domain_free(iommu->domain);
		kfree(iommu);
		return ERR_PTR(-EPERM);
	}

	return iommu;
}

static void vfio_iommu_type1_release(void *iommu_data)
{
	struct vfio_iommu *iommu = iommu_data;
	struct vfio_group *group, *group_tmp;
	struct rb_node *node;

	list_for_each_entry_safe(group, group_tmp, &iommu->group_list, next) {
		iommu_detach_group(iommu->domain, group->iommu_group);
		list_del(&group->next);
		kfree(group);
	}

	while ((node = rb_first(&iommu->dma_list))) {
		struct vfio_dma *dma = rb_entry(node, struct vfio_dma, node);
		size_t size = dma->size;
		vfio_remove_dma_overlap(iommu, dma->iova, &size, dma);
		if (WARN_ON(!size))
			break;
	}

	iommu_domain_free(iommu->domain);
	iommu->domain = NULL;
	kfree(iommu);
}

static long vfio_iommu_type1_ioctl(void *iommu_data,
				   unsigned int cmd, unsigned long arg)
{
	struct vfio_iommu *iommu = iommu_data;
	unsigned long minsz;

	if (cmd == VFIO_CHECK_EXTENSION) {
		switch (arg) {
		case VFIO_TYPE1_IOMMU:
			return 1;
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

		info.iova_pgsizes = iommu->domain->ops->pgsize_bitmap;

		return copy_to_user((void __user *)arg, &info, minsz);

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

		return copy_to_user((void __user *)arg, &unmap, minsz);
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
	if (!iommu_present(&pci_bus_type))
		return -ENODEV;

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
