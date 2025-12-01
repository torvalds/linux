// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021-2022, NVIDIA CORPORATION & AFFILIATES.
 *
 * The io_pagetable is the top of datastructure that maps IOVA's to PFNs. The
 * PFNs can be placed into an iommu_domain, or returned to the caller as a page
 * list for access by an in-kernel user.
 *
 * The datastructure uses the iopt_pages to optimize the storage of the PFNs
 * between the domains and xarray.
 */
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/iommu.h>
#include <linux/iommufd.h>
#include <linux/lockdep.h>
#include <linux/sched/mm.h>
#include <linux/slab.h>
#include <uapi/linux/iommufd.h>

#include "double_span.h"
#include "io_pagetable.h"

struct iopt_pages_list {
	struct iopt_pages *pages;
	struct iopt_area *area;
	struct list_head next;
	unsigned long start_byte;
	unsigned long length;
};

struct iopt_area *iopt_area_contig_init(struct iopt_area_contig_iter *iter,
					struct io_pagetable *iopt,
					unsigned long iova,
					unsigned long last_iova)
{
	lockdep_assert_held(&iopt->iova_rwsem);

	iter->cur_iova = iova;
	iter->last_iova = last_iova;
	iter->area = iopt_area_iter_first(iopt, iova, iova);
	if (!iter->area)
		return NULL;
	if (!iter->area->pages) {
		iter->area = NULL;
		return NULL;
	}
	return iter->area;
}

struct iopt_area *iopt_area_contig_next(struct iopt_area_contig_iter *iter)
{
	unsigned long last_iova;

	if (!iter->area)
		return NULL;
	last_iova = iopt_area_last_iova(iter->area);
	if (iter->last_iova <= last_iova)
		return NULL;

	iter->cur_iova = last_iova + 1;
	iter->area = iopt_area_iter_next(iter->area, iter->cur_iova,
					 iter->last_iova);
	if (!iter->area)
		return NULL;
	if (iter->cur_iova != iopt_area_iova(iter->area) ||
	    !iter->area->pages) {
		iter->area = NULL;
		return NULL;
	}
	return iter->area;
}

static bool __alloc_iova_check_range(unsigned long *start, unsigned long last,
				     unsigned long length,
				     unsigned long iova_alignment,
				     unsigned long page_offset)
{
	unsigned long aligned_start;

	/* ALIGN_UP() */
	if (check_add_overflow(*start, iova_alignment - 1, &aligned_start))
		return false;
	aligned_start &= ~(iova_alignment - 1);
	aligned_start |= page_offset;

	if (aligned_start >= last || last - aligned_start < length - 1)
		return false;
	*start = aligned_start;
	return true;
}

static bool __alloc_iova_check_hole(struct interval_tree_double_span_iter *span,
				    unsigned long length,
				    unsigned long iova_alignment,
				    unsigned long page_offset)
{
	if (span->is_used)
		return false;
	return __alloc_iova_check_range(&span->start_hole, span->last_hole,
					length, iova_alignment, page_offset);
}

static bool __alloc_iova_check_used(struct interval_tree_span_iter *span,
				    unsigned long length,
				    unsigned long iova_alignment,
				    unsigned long page_offset)
{
	if (span->is_hole)
		return false;
	return __alloc_iova_check_range(&span->start_used, span->last_used,
					length, iova_alignment, page_offset);
}

/*
 * Automatically find a block of IOVA that is not being used and not reserved.
 * Does not return a 0 IOVA even if it is valid.
 */
static int iopt_alloc_iova(struct io_pagetable *iopt, unsigned long *iova,
			   unsigned long addr, unsigned long length)
{
	unsigned long page_offset = addr % PAGE_SIZE;
	struct interval_tree_double_span_iter used_span;
	struct interval_tree_span_iter allowed_span;
	unsigned long max_alignment = PAGE_SIZE;
	unsigned long iova_alignment;

	lockdep_assert_held(&iopt->iova_rwsem);

	/* Protect roundup_pow-of_two() from overflow */
	if (length == 0 || length >= ULONG_MAX / 2)
		return -EOVERFLOW;

	/*
	 * Keep alignment present in addr when building the IOVA, which
	 * increases the chance we can map a THP.
	 */
	if (!addr)
		iova_alignment = roundup_pow_of_two(length);
	else
		iova_alignment = min_t(unsigned long,
				       roundup_pow_of_two(length),
				       1UL << __ffs64(addr));

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	max_alignment = HPAGE_SIZE;
#endif
	/* Protect against ALIGN() overflow */
	if (iova_alignment >= max_alignment)
		iova_alignment = max_alignment;

	if (iova_alignment < iopt->iova_alignment)
		return -EINVAL;

	interval_tree_for_each_span(&allowed_span, &iopt->allowed_itree,
				    PAGE_SIZE, ULONG_MAX - PAGE_SIZE) {
		if (RB_EMPTY_ROOT(&iopt->allowed_itree.rb_root)) {
			allowed_span.start_used = PAGE_SIZE;
			allowed_span.last_used = ULONG_MAX - PAGE_SIZE;
			allowed_span.is_hole = false;
		}

		if (!__alloc_iova_check_used(&allowed_span, length,
					     iova_alignment, page_offset))
			continue;

		interval_tree_for_each_double_span(
			&used_span, &iopt->reserved_itree, &iopt->area_itree,
			allowed_span.start_used, allowed_span.last_used) {
			if (!__alloc_iova_check_hole(&used_span, length,
						     iova_alignment,
						     page_offset))
				continue;

			*iova = used_span.start_hole;
			return 0;
		}
	}
	return -ENOSPC;
}

static int iopt_check_iova(struct io_pagetable *iopt, unsigned long iova,
			   unsigned long length)
{
	unsigned long last;

	lockdep_assert_held(&iopt->iova_rwsem);

	if ((iova & (iopt->iova_alignment - 1)))
		return -EINVAL;

	if (check_add_overflow(iova, length - 1, &last))
		return -EOVERFLOW;

	/* No reserved IOVA intersects the range */
	if (iopt_reserved_iter_first(iopt, iova, last))
		return -EINVAL;

	/* Check that there is not already a mapping in the range */
	if (iopt_area_iter_first(iopt, iova, last))
		return -EEXIST;
	return 0;
}

/*
 * The area takes a slice of the pages from start_bytes to start_byte + length
 */
static int iopt_insert_area(struct io_pagetable *iopt, struct iopt_area *area,
			    struct iopt_pages *pages, unsigned long iova,
			    unsigned long start_byte, unsigned long length,
			    int iommu_prot)
{
	lockdep_assert_held_write(&iopt->iova_rwsem);

	if ((iommu_prot & IOMMU_WRITE) && !pages->writable)
		return -EPERM;

	area->iommu_prot = iommu_prot;
	area->page_offset = start_byte % PAGE_SIZE;
	if (area->page_offset & (iopt->iova_alignment - 1))
		return -EINVAL;

	area->node.start = iova;
	if (check_add_overflow(iova, length - 1, &area->node.last))
		return -EOVERFLOW;

	area->pages_node.start = start_byte / PAGE_SIZE;
	if (check_add_overflow(start_byte, length - 1, &area->pages_node.last))
		return -EOVERFLOW;
	area->pages_node.last = area->pages_node.last / PAGE_SIZE;
	if (WARN_ON(area->pages_node.last >= pages->npages))
		return -EOVERFLOW;

	/*
	 * The area is inserted with a NULL pages indicating it is not fully
	 * initialized yet.
	 */
	area->iopt = iopt;
	interval_tree_insert(&area->node, &iopt->area_itree);
	return 0;
}

static struct iopt_area *iopt_area_alloc(void)
{
	struct iopt_area *area;

	area = kzalloc(sizeof(*area), GFP_KERNEL_ACCOUNT);
	if (!area)
		return NULL;
	RB_CLEAR_NODE(&area->node.rb);
	RB_CLEAR_NODE(&area->pages_node.rb);
	return area;
}

static int iopt_alloc_area_pages(struct io_pagetable *iopt,
				 struct list_head *pages_list,
				 unsigned long length, unsigned long *dst_iova,
				 int iommu_prot, unsigned int flags)
{
	struct iopt_pages_list *elm;
	unsigned long start;
	unsigned long iova;
	int rc = 0;

	list_for_each_entry(elm, pages_list, next) {
		elm->area = iopt_area_alloc();
		if (!elm->area)
			return -ENOMEM;
	}

	down_write(&iopt->iova_rwsem);
	if ((length & (iopt->iova_alignment - 1)) || !length) {
		rc = -EINVAL;
		goto out_unlock;
	}

	if (flags & IOPT_ALLOC_IOVA) {
		/* Use the first entry to guess the ideal IOVA alignment */
		elm = list_first_entry(pages_list, struct iopt_pages_list,
				       next);
		switch (elm->pages->type) {
		case IOPT_ADDRESS_USER:
			start = elm->start_byte + (uintptr_t)elm->pages->uptr;
			break;
		case IOPT_ADDRESS_FILE:
			start = elm->start_byte + elm->pages->start;
			break;
		}
		rc = iopt_alloc_iova(iopt, dst_iova, start, length);
		if (rc)
			goto out_unlock;
		if (IS_ENABLED(CONFIG_IOMMUFD_TEST) &&
		    WARN_ON(iopt_check_iova(iopt, *dst_iova, length))) {
			rc = -EINVAL;
			goto out_unlock;
		}
	} else {
		rc = iopt_check_iova(iopt, *dst_iova, length);
		if (rc)
			goto out_unlock;
	}

	/*
	 * Areas are created with a NULL pages so that the IOVA space is
	 * reserved and we can unlock the iova_rwsem.
	 */
	iova = *dst_iova;
	list_for_each_entry(elm, pages_list, next) {
		rc = iopt_insert_area(iopt, elm->area, elm->pages, iova,
				      elm->start_byte, elm->length, iommu_prot);
		if (rc)
			goto out_unlock;
		iova += elm->length;
	}

out_unlock:
	up_write(&iopt->iova_rwsem);
	return rc;
}

static void iopt_abort_area(struct iopt_area *area)
{
	if (IS_ENABLED(CONFIG_IOMMUFD_TEST))
		WARN_ON(area->pages);
	if (area->iopt) {
		down_write(&area->iopt->iova_rwsem);
		interval_tree_remove(&area->node, &area->iopt->area_itree);
		up_write(&area->iopt->iova_rwsem);
	}
	kfree(area);
}

void iopt_free_pages_list(struct list_head *pages_list)
{
	struct iopt_pages_list *elm;

	while ((elm = list_first_entry_or_null(pages_list,
					       struct iopt_pages_list, next))) {
		if (elm->area)
			iopt_abort_area(elm->area);
		if (elm->pages)
			iopt_put_pages(elm->pages);
		list_del(&elm->next);
		kfree(elm);
	}
}

static int iopt_fill_domains_pages(struct list_head *pages_list)
{
	struct iopt_pages_list *undo_elm;
	struct iopt_pages_list *elm;
	int rc;

	list_for_each_entry(elm, pages_list, next) {
		rc = iopt_area_fill_domains(elm->area, elm->pages);
		if (rc)
			goto err_undo;
	}
	return 0;

err_undo:
	list_for_each_entry(undo_elm, pages_list, next) {
		if (undo_elm == elm)
			break;
		iopt_area_unfill_domains(undo_elm->area, undo_elm->pages);
	}
	return rc;
}

int iopt_map_pages(struct io_pagetable *iopt, struct list_head *pages_list,
		   unsigned long length, unsigned long *dst_iova,
		   int iommu_prot, unsigned int flags)
{
	struct iopt_pages_list *elm;
	int rc;

	rc = iopt_alloc_area_pages(iopt, pages_list, length, dst_iova,
				   iommu_prot, flags);
	if (rc)
		return rc;

	down_read(&iopt->domains_rwsem);
	rc = iopt_fill_domains_pages(pages_list);
	if (rc)
		goto out_unlock_domains;

	down_write(&iopt->iova_rwsem);
	list_for_each_entry(elm, pages_list, next) {
		/*
		 * area->pages must be set inside the domains_rwsem to ensure
		 * any newly added domains will get filled. Moves the reference
		 * in from the list.
		 */
		elm->area->pages = elm->pages;
		elm->pages = NULL;
		elm->area = NULL;
	}
	up_write(&iopt->iova_rwsem);
out_unlock_domains:
	up_read(&iopt->domains_rwsem);
	return rc;
}

static int iopt_map_common(struct iommufd_ctx *ictx, struct io_pagetable *iopt,
			   struct iopt_pages *pages, unsigned long *iova,
			   unsigned long length, unsigned long start_byte,
			   int iommu_prot, unsigned int flags)
{
	struct iopt_pages_list elm = {};
	LIST_HEAD(pages_list);
	int rc;

	elm.pages = pages;
	elm.start_byte = start_byte;
	if (ictx->account_mode == IOPT_PAGES_ACCOUNT_MM &&
	    elm.pages->account_mode == IOPT_PAGES_ACCOUNT_USER)
		elm.pages->account_mode = IOPT_PAGES_ACCOUNT_MM;
	elm.length = length;
	list_add(&elm.next, &pages_list);

	rc = iopt_map_pages(iopt, &pages_list, length, iova, iommu_prot, flags);
	if (rc) {
		if (elm.area)
			iopt_abort_area(elm.area);
		if (elm.pages)
			iopt_put_pages(elm.pages);
		return rc;
	}
	return 0;
}

/**
 * iopt_map_user_pages() - Map a user VA to an iova in the io page table
 * @ictx: iommufd_ctx the iopt is part of
 * @iopt: io_pagetable to act on
 * @iova: If IOPT_ALLOC_IOVA is set this is unused on input and contains
 *        the chosen iova on output. Otherwise is the iova to map to on input
 * @uptr: User VA to map
 * @length: Number of bytes to map
 * @iommu_prot: Combination of IOMMU_READ/WRITE/etc bits for the mapping
 * @flags: IOPT_ALLOC_IOVA or zero
 *
 * iova, uptr, and length must be aligned to iova_alignment. For domain backed
 * page tables this will pin the pages and load them into the domain at iova.
 * For non-domain page tables this will only setup a lazy reference and the
 * caller must use iopt_access_pages() to touch them.
 *
 * iopt_unmap_iova() must be called to undo this before the io_pagetable can be
 * destroyed.
 */
int iopt_map_user_pages(struct iommufd_ctx *ictx, struct io_pagetable *iopt,
			unsigned long *iova, void __user *uptr,
			unsigned long length, int iommu_prot,
			unsigned int flags)
{
	struct iopt_pages *pages;

	pages = iopt_alloc_user_pages(uptr, length, iommu_prot & IOMMU_WRITE);
	if (IS_ERR(pages))
		return PTR_ERR(pages);

	return iopt_map_common(ictx, iopt, pages, iova, length,
			       uptr - pages->uptr, iommu_prot, flags);
}

/**
 * iopt_map_file_pages() - Like iopt_map_user_pages, but map a file.
 * @ictx: iommufd_ctx the iopt is part of
 * @iopt: io_pagetable to act on
 * @iova: If IOPT_ALLOC_IOVA is set this is unused on input and contains
 *        the chosen iova on output. Otherwise is the iova to map to on input
 * @file: file to map
 * @start: map file starting at this byte offset
 * @length: Number of bytes to map
 * @iommu_prot: Combination of IOMMU_READ/WRITE/etc bits for the mapping
 * @flags: IOPT_ALLOC_IOVA or zero
 */
int iopt_map_file_pages(struct iommufd_ctx *ictx, struct io_pagetable *iopt,
			unsigned long *iova, struct file *file,
			unsigned long start, unsigned long length,
			int iommu_prot, unsigned int flags)
{
	struct iopt_pages *pages;

	pages = iopt_alloc_file_pages(file, start, length,
				      iommu_prot & IOMMU_WRITE);
	if (IS_ERR(pages))
		return PTR_ERR(pages);
	return iopt_map_common(ictx, iopt, pages, iova, length,
			       start - pages->start, iommu_prot, flags);
}

struct iova_bitmap_fn_arg {
	unsigned long flags;
	struct io_pagetable *iopt;
	struct iommu_domain *domain;
	struct iommu_dirty_bitmap *dirty;
};

static int __iommu_read_and_clear_dirty(struct iova_bitmap *bitmap,
					unsigned long iova, size_t length,
					void *opaque)
{
	struct iopt_area *area;
	struct iopt_area_contig_iter iter;
	struct iova_bitmap_fn_arg *arg = opaque;
	struct iommu_domain *domain = arg->domain;
	struct iommu_dirty_bitmap *dirty = arg->dirty;
	const struct iommu_dirty_ops *ops = domain->dirty_ops;
	unsigned long last_iova = iova + length - 1;
	unsigned long flags = arg->flags;
	int ret;

	iopt_for_each_contig_area(&iter, area, arg->iopt, iova, last_iova) {
		unsigned long last = min(last_iova, iopt_area_last_iova(area));

		ret = ops->read_and_clear_dirty(domain, iter.cur_iova,
						last - iter.cur_iova + 1, flags,
						dirty);
		if (ret)
			return ret;
	}

	if (!iopt_area_contig_done(&iter))
		return -EINVAL;
	return 0;
}

static int
iommu_read_and_clear_dirty(struct iommu_domain *domain,
			   struct io_pagetable *iopt, unsigned long flags,
			   struct iommu_hwpt_get_dirty_bitmap *bitmap)
{
	const struct iommu_dirty_ops *ops = domain->dirty_ops;
	struct iommu_iotlb_gather gather;
	struct iommu_dirty_bitmap dirty;
	struct iova_bitmap_fn_arg arg;
	struct iova_bitmap *iter;
	int ret = 0;

	if (!ops || !ops->read_and_clear_dirty)
		return -EOPNOTSUPP;

	iter = iova_bitmap_alloc(bitmap->iova, bitmap->length,
				 bitmap->page_size,
				 u64_to_user_ptr(bitmap->data));
	if (IS_ERR(iter))
		return -ENOMEM;

	iommu_dirty_bitmap_init(&dirty, iter, &gather);

	arg.flags = flags;
	arg.iopt = iopt;
	arg.domain = domain;
	arg.dirty = &dirty;
	iova_bitmap_for_each(iter, &arg, __iommu_read_and_clear_dirty);

	if (!(flags & IOMMU_DIRTY_NO_CLEAR))
		iommu_iotlb_sync(domain, &gather);

	iova_bitmap_free(iter);

	return ret;
}

int iommufd_check_iova_range(struct io_pagetable *iopt,
			     struct iommu_hwpt_get_dirty_bitmap *bitmap)
{
	size_t iommu_pgsize = iopt->iova_alignment;
	u64 last_iova;

	if (check_add_overflow(bitmap->iova, bitmap->length - 1, &last_iova))
		return -EOVERFLOW;

	if (bitmap->iova > ULONG_MAX || last_iova > ULONG_MAX)
		return -EOVERFLOW;

	if ((bitmap->iova & (iommu_pgsize - 1)) ||
	    ((last_iova + 1) & (iommu_pgsize - 1)))
		return -EINVAL;

	if (!bitmap->page_size)
		return -EINVAL;

	if ((bitmap->iova & (bitmap->page_size - 1)) ||
	    ((last_iova + 1) & (bitmap->page_size - 1)))
		return -EINVAL;

	return 0;
}

int iopt_read_and_clear_dirty_data(struct io_pagetable *iopt,
				   struct iommu_domain *domain,
				   unsigned long flags,
				   struct iommu_hwpt_get_dirty_bitmap *bitmap)
{
	int ret;

	ret = iommufd_check_iova_range(iopt, bitmap);
	if (ret)
		return ret;

	down_read(&iopt->iova_rwsem);
	ret = iommu_read_and_clear_dirty(domain, iopt, flags, bitmap);
	up_read(&iopt->iova_rwsem);

	return ret;
}

static int iopt_clear_dirty_data(struct io_pagetable *iopt,
				 struct iommu_domain *domain)
{
	const struct iommu_dirty_ops *ops = domain->dirty_ops;
	struct iommu_iotlb_gather gather;
	struct iommu_dirty_bitmap dirty;
	struct iopt_area *area;
	int ret = 0;

	lockdep_assert_held_read(&iopt->iova_rwsem);

	iommu_dirty_bitmap_init(&dirty, NULL, &gather);

	for (area = iopt_area_iter_first(iopt, 0, ULONG_MAX); area;
	     area = iopt_area_iter_next(area, 0, ULONG_MAX)) {
		if (!area->pages)
			continue;

		ret = ops->read_and_clear_dirty(domain, iopt_area_iova(area),
						iopt_area_length(area), 0,
						&dirty);
		if (ret)
			break;
	}

	iommu_iotlb_sync(domain, &gather);
	return ret;
}

int iopt_set_dirty_tracking(struct io_pagetable *iopt,
			    struct iommu_domain *domain, bool enable)
{
	const struct iommu_dirty_ops *ops = domain->dirty_ops;
	int ret = 0;

	if (!ops)
		return -EOPNOTSUPP;

	down_read(&iopt->iova_rwsem);

	/* Clear dirty bits from PTEs to ensure a clean snapshot */
	if (enable) {
		ret = iopt_clear_dirty_data(iopt, domain);
		if (ret)
			goto out_unlock;
	}

	ret = ops->set_dirty_tracking(domain, enable);

out_unlock:
	up_read(&iopt->iova_rwsem);
	return ret;
}

int iopt_get_pages(struct io_pagetable *iopt, unsigned long iova,
		   unsigned long length, struct list_head *pages_list)
{
	struct iopt_area_contig_iter iter;
	unsigned long last_iova;
	struct iopt_area *area;
	int rc;

	if (!length)
		return -EINVAL;
	if (check_add_overflow(iova, length - 1, &last_iova))
		return -EOVERFLOW;

	down_read(&iopt->iova_rwsem);
	iopt_for_each_contig_area(&iter, area, iopt, iova, last_iova) {
		struct iopt_pages_list *elm;
		unsigned long last = min(last_iova, iopt_area_last_iova(area));

		elm = kzalloc(sizeof(*elm), GFP_KERNEL_ACCOUNT);
		if (!elm) {
			rc = -ENOMEM;
			goto err_free;
		}
		elm->start_byte = iopt_area_start_byte(area, iter.cur_iova);
		elm->pages = area->pages;
		elm->length = (last - iter.cur_iova) + 1;
		kref_get(&elm->pages->kref);
		list_add_tail(&elm->next, pages_list);
	}
	if (!iopt_area_contig_done(&iter)) {
		rc = -ENOENT;
		goto err_free;
	}
	up_read(&iopt->iova_rwsem);
	return 0;
err_free:
	up_read(&iopt->iova_rwsem);
	iopt_free_pages_list(pages_list);
	return rc;
}

static int iopt_unmap_iova_range(struct io_pagetable *iopt, unsigned long start,
				 unsigned long last, unsigned long *unmapped)
{
	struct iopt_area *area;
	unsigned long unmapped_bytes = 0;
	unsigned int tries = 0;
	/* If there are no mapped entries then success */
	int rc = 0;

	/*
	 * The domains_rwsem must be held in read mode any time any area->pages
	 * is NULL. This prevents domain attach/detatch from running
	 * concurrently with cleaning up the area.
	 */
again:
	down_read(&iopt->domains_rwsem);
	down_write(&iopt->iova_rwsem);
	while ((area = iopt_area_iter_first(iopt, start, last))) {
		unsigned long area_last = iopt_area_last_iova(area);
		unsigned long area_first = iopt_area_iova(area);
		struct iopt_pages *pages;

		/* Userspace should not race map/unmap's of the same area */
		if (!area->pages) {
			rc = -EBUSY;
			goto out_unlock_iova;
		}

		/* The area is locked by an object that has not been destroyed */
		if (area->num_locks) {
			rc = -EBUSY;
			goto out_unlock_iova;
		}

		if (area_first < start || area_last > last) {
			rc = -ENOENT;
			goto out_unlock_iova;
		}

		if (area_first != start)
			tries = 0;

		/*
		 * num_accesses writers must hold the iova_rwsem too, so we can
		 * safely read it under the write side of the iovam_rwsem
		 * without the pages->mutex.
		 */
		if (area->num_accesses) {
			size_t length = iopt_area_length(area);

			start = area_first;
			area->prevent_access = true;
			up_write(&iopt->iova_rwsem);
			up_read(&iopt->domains_rwsem);

			iommufd_access_notify_unmap(iopt, area_first, length);
			/* Something is not responding to unmap requests. */
			tries++;
			if (WARN_ON(tries > 100)) {
				rc = -EDEADLOCK;
				goto out_unmapped;
			}
			goto again;
		}

		pages = area->pages;
		area->pages = NULL;
		up_write(&iopt->iova_rwsem);

		iopt_area_unfill_domains(area, pages);
		iopt_abort_area(area);
		iopt_put_pages(pages);

		unmapped_bytes += area_last - area_first + 1;

		down_write(&iopt->iova_rwsem);
	}

out_unlock_iova:
	up_write(&iopt->iova_rwsem);
	up_read(&iopt->domains_rwsem);
out_unmapped:
	if (unmapped)
		*unmapped = unmapped_bytes;
	return rc;
}

/**
 * iopt_unmap_iova() - Remove a range of iova
 * @iopt: io_pagetable to act on
 * @iova: Starting iova to unmap
 * @length: Number of bytes to unmap
 * @unmapped: Return number of bytes unmapped
 *
 * The requested range must be a superset of existing ranges.
 * Splitting/truncating IOVA mappings is not allowed.
 */
int iopt_unmap_iova(struct io_pagetable *iopt, unsigned long iova,
		    unsigned long length, unsigned long *unmapped)
{
	unsigned long iova_last;

	if (!length)
		return -EINVAL;

	if (check_add_overflow(iova, length - 1, &iova_last))
		return -EOVERFLOW;

	return iopt_unmap_iova_range(iopt, iova, iova_last, unmapped);
}

int iopt_unmap_all(struct io_pagetable *iopt, unsigned long *unmapped)
{
	/* If the IOVAs are empty then unmap all succeeds */
	return iopt_unmap_iova_range(iopt, 0, ULONG_MAX, unmapped);
}

/* The caller must always free all the nodes in the allowed_iova rb_root. */
int iopt_set_allow_iova(struct io_pagetable *iopt,
			struct rb_root_cached *allowed_iova)
{
	struct iopt_allowed *allowed;

	down_write(&iopt->iova_rwsem);
	swap(*allowed_iova, iopt->allowed_itree);

	for (allowed = iopt_allowed_iter_first(iopt, 0, ULONG_MAX); allowed;
	     allowed = iopt_allowed_iter_next(allowed, 0, ULONG_MAX)) {
		if (iopt_reserved_iter_first(iopt, allowed->node.start,
					     allowed->node.last)) {
			swap(*allowed_iova, iopt->allowed_itree);
			up_write(&iopt->iova_rwsem);
			return -EADDRINUSE;
		}
	}
	up_write(&iopt->iova_rwsem);
	return 0;
}

int iopt_reserve_iova(struct io_pagetable *iopt, unsigned long start,
		      unsigned long last, void *owner)
{
	struct iopt_reserved *reserved;

	lockdep_assert_held_write(&iopt->iova_rwsem);

	if (iopt_area_iter_first(iopt, start, last) ||
	    iopt_allowed_iter_first(iopt, start, last))
		return -EADDRINUSE;

	reserved = kzalloc(sizeof(*reserved), GFP_KERNEL_ACCOUNT);
	if (!reserved)
		return -ENOMEM;
	reserved->node.start = start;
	reserved->node.last = last;
	reserved->owner = owner;
	interval_tree_insert(&reserved->node, &iopt->reserved_itree);
	return 0;
}

static void __iopt_remove_reserved_iova(struct io_pagetable *iopt, void *owner)
{
	struct iopt_reserved *reserved, *next;

	lockdep_assert_held_write(&iopt->iova_rwsem);

	for (reserved = iopt_reserved_iter_first(iopt, 0, ULONG_MAX); reserved;
	     reserved = next) {
		next = iopt_reserved_iter_next(reserved, 0, ULONG_MAX);

		if (reserved->owner == owner) {
			interval_tree_remove(&reserved->node,
					     &iopt->reserved_itree);
			kfree(reserved);
		}
	}
}

void iopt_remove_reserved_iova(struct io_pagetable *iopt, void *owner)
{
	down_write(&iopt->iova_rwsem);
	__iopt_remove_reserved_iova(iopt, owner);
	up_write(&iopt->iova_rwsem);
}

void iopt_init_table(struct io_pagetable *iopt)
{
	init_rwsem(&iopt->iova_rwsem);
	init_rwsem(&iopt->domains_rwsem);
	iopt->area_itree = RB_ROOT_CACHED;
	iopt->allowed_itree = RB_ROOT_CACHED;
	iopt->reserved_itree = RB_ROOT_CACHED;
	xa_init_flags(&iopt->domains, XA_FLAGS_ACCOUNT);
	xa_init_flags(&iopt->access_list, XA_FLAGS_ALLOC);

	/*
	 * iopt's start as SW tables that can use the entire size_t IOVA space
	 * due to the use of size_t in the APIs. They have no alignment
	 * restriction.
	 */
	iopt->iova_alignment = 1;
}

void iopt_destroy_table(struct io_pagetable *iopt)
{
	struct interval_tree_node *node;

	if (IS_ENABLED(CONFIG_IOMMUFD_TEST))
		iopt_remove_reserved_iova(iopt, NULL);

	while ((node = interval_tree_iter_first(&iopt->allowed_itree, 0,
						ULONG_MAX))) {
		interval_tree_remove(node, &iopt->allowed_itree);
		kfree(container_of(node, struct iopt_allowed, node));
	}

	WARN_ON(!RB_EMPTY_ROOT(&iopt->reserved_itree.rb_root));
	WARN_ON(!xa_empty(&iopt->domains));
	WARN_ON(!xa_empty(&iopt->access_list));
	WARN_ON(!RB_EMPTY_ROOT(&iopt->area_itree.rb_root));
}

/**
 * iopt_unfill_domain() - Unfill a domain with PFNs
 * @iopt: io_pagetable to act on
 * @domain: domain to unfill
 *
 * This is used when removing a domain from the iopt. Every area in the iopt
 * will be unmapped from the domain. The domain must already be removed from the
 * domains xarray.
 */
static void iopt_unfill_domain(struct io_pagetable *iopt,
			       struct iommu_domain *domain)
{
	struct iopt_area *area;

	lockdep_assert_held(&iopt->iova_rwsem);
	lockdep_assert_held_write(&iopt->domains_rwsem);

	/*
	 * Some other domain is holding all the pfns still, rapidly unmap this
	 * domain.
	 */
	if (iopt->next_domain_id != 0) {
		/* Pick an arbitrary remaining domain to act as storage */
		struct iommu_domain *storage_domain =
			xa_load(&iopt->domains, 0);

		for (area = iopt_area_iter_first(iopt, 0, ULONG_MAX); area;
		     area = iopt_area_iter_next(area, 0, ULONG_MAX)) {
			struct iopt_pages *pages = area->pages;

			if (!pages)
				continue;

			mutex_lock(&pages->mutex);
			if (IS_ENABLED(CONFIG_IOMMUFD_TEST))
				WARN_ON(!area->storage_domain);
			if (area->storage_domain == domain)
				area->storage_domain = storage_domain;
			mutex_unlock(&pages->mutex);

			iopt_area_unmap_domain(area, domain);
		}
		return;
	}

	for (area = iopt_area_iter_first(iopt, 0, ULONG_MAX); area;
	     area = iopt_area_iter_next(area, 0, ULONG_MAX)) {
		struct iopt_pages *pages = area->pages;

		if (!pages)
			continue;

		mutex_lock(&pages->mutex);
		interval_tree_remove(&area->pages_node, &pages->domains_itree);
		WARN_ON(area->storage_domain != domain);
		area->storage_domain = NULL;
		iopt_area_unfill_domain(area, pages, domain);
		mutex_unlock(&pages->mutex);
	}
}

/**
 * iopt_fill_domain() - Fill a domain with PFNs
 * @iopt: io_pagetable to act on
 * @domain: domain to fill
 *
 * Fill the domain with PFNs from every area in the iopt. On failure the domain
 * is left unchanged.
 */
static int iopt_fill_domain(struct io_pagetable *iopt,
			    struct iommu_domain *domain)
{
	struct iopt_area *end_area;
	struct iopt_area *area;
	int rc;

	lockdep_assert_held(&iopt->iova_rwsem);
	lockdep_assert_held_write(&iopt->domains_rwsem);

	for (area = iopt_area_iter_first(iopt, 0, ULONG_MAX); area;
	     area = iopt_area_iter_next(area, 0, ULONG_MAX)) {
		struct iopt_pages *pages = area->pages;

		if (!pages)
			continue;

		mutex_lock(&pages->mutex);
		rc = iopt_area_fill_domain(area, domain);
		if (rc) {
			mutex_unlock(&pages->mutex);
			goto out_unfill;
		}
		if (!area->storage_domain) {
			WARN_ON(iopt->next_domain_id != 0);
			area->storage_domain = domain;
			interval_tree_insert(&area->pages_node,
					     &pages->domains_itree);
		}
		mutex_unlock(&pages->mutex);
	}
	return 0;

out_unfill:
	end_area = area;
	for (area = iopt_area_iter_first(iopt, 0, ULONG_MAX); area;
	     area = iopt_area_iter_next(area, 0, ULONG_MAX)) {
		struct iopt_pages *pages = area->pages;

		if (area == end_area)
			break;
		if (!pages)
			continue;
		mutex_lock(&pages->mutex);
		if (iopt->next_domain_id == 0) {
			interval_tree_remove(&area->pages_node,
					     &pages->domains_itree);
			area->storage_domain = NULL;
		}
		iopt_area_unfill_domain(area, pages, domain);
		mutex_unlock(&pages->mutex);
	}
	return rc;
}

/* All existing area's conform to an increased page size */
static int iopt_check_iova_alignment(struct io_pagetable *iopt,
				     unsigned long new_iova_alignment)
{
	unsigned long align_mask = new_iova_alignment - 1;
	struct iopt_area *area;

	lockdep_assert_held(&iopt->iova_rwsem);
	lockdep_assert_held(&iopt->domains_rwsem);

	for (area = iopt_area_iter_first(iopt, 0, ULONG_MAX); area;
	     area = iopt_area_iter_next(area, 0, ULONG_MAX))
		if ((iopt_area_iova(area) & align_mask) ||
		    (iopt_area_length(area) & align_mask) ||
		    (area->page_offset & align_mask))
			return -EADDRINUSE;

	if (IS_ENABLED(CONFIG_IOMMUFD_TEST)) {
		struct iommufd_access *access;
		unsigned long index;

		xa_for_each(&iopt->access_list, index, access)
			if (WARN_ON(access->iova_alignment >
				    new_iova_alignment))
				return -EADDRINUSE;
	}
	return 0;
}

int iopt_table_add_domain(struct io_pagetable *iopt,
			  struct iommu_domain *domain)
{
	const struct iommu_domain_geometry *geometry = &domain->geometry;
	struct iommu_domain *iter_domain;
	unsigned int new_iova_alignment;
	unsigned long index;
	int rc;

	down_write(&iopt->domains_rwsem);
	down_write(&iopt->iova_rwsem);

	xa_for_each(&iopt->domains, index, iter_domain) {
		if (WARN_ON(iter_domain == domain)) {
			rc = -EEXIST;
			goto out_unlock;
		}
	}

	/*
	 * The io page size drives the iova_alignment. Internally the iopt_pages
	 * works in PAGE_SIZE units and we adjust when mapping sub-PAGE_SIZE
	 * objects into the iommu_domain.
	 *
	 * A iommu_domain must always be able to accept PAGE_SIZE to be
	 * compatible as we can't guarantee higher contiguity.
	 */
	new_iova_alignment = max_t(unsigned long,
				   1UL << __ffs(domain->pgsize_bitmap),
				   iopt->iova_alignment);
	if (new_iova_alignment > PAGE_SIZE) {
		rc = -EINVAL;
		goto out_unlock;
	}
	if (new_iova_alignment != iopt->iova_alignment) {
		rc = iopt_check_iova_alignment(iopt, new_iova_alignment);
		if (rc)
			goto out_unlock;
	}

	/* No area exists that is outside the allowed domain aperture */
	if (geometry->aperture_start != 0) {
		rc = iopt_reserve_iova(iopt, 0, geometry->aperture_start - 1,
				       domain);
		if (rc)
			goto out_reserved;
	}
	if (geometry->aperture_end != ULONG_MAX) {
		rc = iopt_reserve_iova(iopt, geometry->aperture_end + 1,
				       ULONG_MAX, domain);
		if (rc)
			goto out_reserved;
	}

	rc = xa_reserve(&iopt->domains, iopt->next_domain_id, GFP_KERNEL);
	if (rc)
		goto out_reserved;

	rc = iopt_fill_domain(iopt, domain);
	if (rc)
		goto out_release;

	iopt->iova_alignment = new_iova_alignment;
	xa_store(&iopt->domains, iopt->next_domain_id, domain, GFP_KERNEL);
	iopt->next_domain_id++;
	up_write(&iopt->iova_rwsem);
	up_write(&iopt->domains_rwsem);
	return 0;
out_release:
	xa_release(&iopt->domains, iopt->next_domain_id);
out_reserved:
	__iopt_remove_reserved_iova(iopt, domain);
out_unlock:
	up_write(&iopt->iova_rwsem);
	up_write(&iopt->domains_rwsem);
	return rc;
}

static int iopt_calculate_iova_alignment(struct io_pagetable *iopt)
{
	unsigned long new_iova_alignment;
	struct iommufd_access *access;
	struct iommu_domain *domain;
	unsigned long index;

	lockdep_assert_held_write(&iopt->iova_rwsem);
	lockdep_assert_held(&iopt->domains_rwsem);

	/* See batch_iommu_map_small() */
	if (iopt->disable_large_pages)
		new_iova_alignment = PAGE_SIZE;
	else
		new_iova_alignment = 1;

	xa_for_each(&iopt->domains, index, domain)
		new_iova_alignment = max_t(unsigned long,
					   1UL << __ffs(domain->pgsize_bitmap),
					   new_iova_alignment);
	xa_for_each(&iopt->access_list, index, access)
		new_iova_alignment = max_t(unsigned long,
					   access->iova_alignment,
					   new_iova_alignment);

	if (new_iova_alignment > iopt->iova_alignment) {
		int rc;

		rc = iopt_check_iova_alignment(iopt, new_iova_alignment);
		if (rc)
			return rc;
	}
	iopt->iova_alignment = new_iova_alignment;
	return 0;
}

void iopt_table_remove_domain(struct io_pagetable *iopt,
			      struct iommu_domain *domain)
{
	struct iommu_domain *iter_domain = NULL;
	unsigned long index;

	down_write(&iopt->domains_rwsem);
	down_write(&iopt->iova_rwsem);

	xa_for_each(&iopt->domains, index, iter_domain)
		if (iter_domain == domain)
			break;
	if (WARN_ON(iter_domain != domain) || index >= iopt->next_domain_id)
		goto out_unlock;

	/*
	 * Compress the xarray to keep it linear by swapping the entry to erase
	 * with the tail entry and shrinking the tail.
	 */
	iopt->next_domain_id--;
	iter_domain = xa_erase(&iopt->domains, iopt->next_domain_id);
	if (index != iopt->next_domain_id)
		xa_store(&iopt->domains, index, iter_domain, GFP_KERNEL);

	iopt_unfill_domain(iopt, domain);
	__iopt_remove_reserved_iova(iopt, domain);

	WARN_ON(iopt_calculate_iova_alignment(iopt));
out_unlock:
	up_write(&iopt->iova_rwsem);
	up_write(&iopt->domains_rwsem);
}

/**
 * iopt_area_split - Split an area into two parts at iova
 * @area: The area to split
 * @iova: Becomes the last of a new area
 *
 * This splits an area into two. It is part of the VFIO compatibility to allow
 * poking a hole in the mapping. The two areas continue to point at the same
 * iopt_pages, just with different starting bytes.
 */
static int iopt_area_split(struct iopt_area *area, unsigned long iova)
{
	unsigned long alignment = area->iopt->iova_alignment;
	unsigned long last_iova = iopt_area_last_iova(area);
	unsigned long start_iova = iopt_area_iova(area);
	unsigned long new_start = iova + 1;
	struct io_pagetable *iopt = area->iopt;
	struct iopt_pages *pages = area->pages;
	struct iopt_area *lhs;
	struct iopt_area *rhs;
	int rc;

	lockdep_assert_held_write(&iopt->iova_rwsem);

	if (iova == start_iova || iova == last_iova)
		return 0;

	if (!pages || area->prevent_access)
		return -EBUSY;

	if (new_start & (alignment - 1) ||
	    iopt_area_start_byte(area, new_start) & (alignment - 1))
		return -EINVAL;

	lhs = iopt_area_alloc();
	if (!lhs)
		return -ENOMEM;

	rhs = iopt_area_alloc();
	if (!rhs) {
		rc = -ENOMEM;
		goto err_free_lhs;
	}

	mutex_lock(&pages->mutex);
	/*
	 * Splitting is not permitted if an access exists, we don't track enough
	 * information to split existing accesses.
	 */
	if (area->num_accesses) {
		rc = -EINVAL;
		goto err_unlock;
	}

	/*
	 * Splitting is not permitted if a domain could have been mapped with
	 * huge pages.
	 */
	if (area->storage_domain && !iopt->disable_large_pages) {
		rc = -EINVAL;
		goto err_unlock;
	}

	interval_tree_remove(&area->node, &iopt->area_itree);
	rc = iopt_insert_area(iopt, lhs, area->pages, start_iova,
			      iopt_area_start_byte(area, start_iova),
			      (new_start - 1) - start_iova + 1,
			      area->iommu_prot);
	if (WARN_ON(rc))
		goto err_insert;

	rc = iopt_insert_area(iopt, rhs, area->pages, new_start,
			      iopt_area_start_byte(area, new_start),
			      last_iova - new_start + 1, area->iommu_prot);
	if (WARN_ON(rc))
		goto err_remove_lhs;

	/*
	 * If the original area has filled a domain, domains_itree has to be
	 * updated.
	 */
	if (area->storage_domain) {
		interval_tree_remove(&area->pages_node, &pages->domains_itree);
		interval_tree_insert(&lhs->pages_node, &pages->domains_itree);
		interval_tree_insert(&rhs->pages_node, &pages->domains_itree);
	}

	lhs->storage_domain = area->storage_domain;
	lhs->pages = area->pages;
	rhs->storage_domain = area->storage_domain;
	rhs->pages = area->pages;
	kref_get(&rhs->pages->kref);
	kfree(area);
	mutex_unlock(&pages->mutex);

	/*
	 * No change to domains or accesses because the pages hasn't been
	 * changed
	 */
	return 0;

err_remove_lhs:
	interval_tree_remove(&lhs->node, &iopt->area_itree);
err_insert:
	interval_tree_insert(&area->node, &iopt->area_itree);
err_unlock:
	mutex_unlock(&pages->mutex);
	kfree(rhs);
err_free_lhs:
	kfree(lhs);
	return rc;
}

int iopt_cut_iova(struct io_pagetable *iopt, unsigned long *iovas,
		  size_t num_iovas)
{
	int rc = 0;
	int i;

	down_write(&iopt->iova_rwsem);
	for (i = 0; i < num_iovas; i++) {
		struct iopt_area *area;

		area = iopt_area_iter_first(iopt, iovas[i], iovas[i]);
		if (!area)
			continue;
		rc = iopt_area_split(area, iovas[i]);
		if (rc)
			break;
	}
	up_write(&iopt->iova_rwsem);
	return rc;
}

void iopt_enable_large_pages(struct io_pagetable *iopt)
{
	int rc;

	down_write(&iopt->domains_rwsem);
	down_write(&iopt->iova_rwsem);
	WRITE_ONCE(iopt->disable_large_pages, false);
	rc = iopt_calculate_iova_alignment(iopt);
	WARN_ON(rc);
	up_write(&iopt->iova_rwsem);
	up_write(&iopt->domains_rwsem);
}

int iopt_disable_large_pages(struct io_pagetable *iopt)
{
	int rc = 0;

	down_write(&iopt->domains_rwsem);
	down_write(&iopt->iova_rwsem);
	if (iopt->disable_large_pages)
		goto out_unlock;

	/* Won't do it if domains already have pages mapped in them */
	if (!xa_empty(&iopt->domains) &&
	    !RB_EMPTY_ROOT(&iopt->area_itree.rb_root)) {
		rc = -EINVAL;
		goto out_unlock;
	}

	WRITE_ONCE(iopt->disable_large_pages, true);
	rc = iopt_calculate_iova_alignment(iopt);
	if (rc)
		WRITE_ONCE(iopt->disable_large_pages, false);
out_unlock:
	up_write(&iopt->iova_rwsem);
	up_write(&iopt->domains_rwsem);
	return rc;
}

int iopt_add_access(struct io_pagetable *iopt, struct iommufd_access *access)
{
	u32 new_id;
	int rc;

	down_write(&iopt->domains_rwsem);
	down_write(&iopt->iova_rwsem);
	rc = xa_alloc(&iopt->access_list, &new_id, access, xa_limit_16b,
		      GFP_KERNEL_ACCOUNT);

	if (rc)
		goto out_unlock;

	rc = iopt_calculate_iova_alignment(iopt);
	if (rc) {
		xa_erase(&iopt->access_list, new_id);
		goto out_unlock;
	}
	access->iopt_access_list_id = new_id;

out_unlock:
	up_write(&iopt->iova_rwsem);
	up_write(&iopt->domains_rwsem);
	return rc;
}

void iopt_remove_access(struct io_pagetable *iopt,
			struct iommufd_access *access, u32 iopt_access_list_id)
{
	down_write(&iopt->domains_rwsem);
	down_write(&iopt->iova_rwsem);
	WARN_ON(xa_erase(&iopt->access_list, iopt_access_list_id) != access);
	WARN_ON(iopt_calculate_iova_alignment(iopt));
	up_write(&iopt->iova_rwsem);
	up_write(&iopt->domains_rwsem);
}

/* Narrow the valid_iova_itree to include reserved ranges from a device. */
int iopt_table_enforce_dev_resv_regions(struct io_pagetable *iopt,
					struct device *dev,
					phys_addr_t *sw_msi_start)
{
	struct iommu_resv_region *resv;
	LIST_HEAD(resv_regions);
	unsigned int num_hw_msi = 0;
	unsigned int num_sw_msi = 0;
	int rc;

	if (iommufd_should_fail())
		return -EINVAL;

	down_write(&iopt->iova_rwsem);
	/* FIXME: drivers allocate memory but there is no failure propogated */
	iommu_get_resv_regions(dev, &resv_regions);

	list_for_each_entry(resv, &resv_regions, list) {
		if (resv->type == IOMMU_RESV_DIRECT_RELAXABLE)
			continue;

		if (sw_msi_start && resv->type == IOMMU_RESV_MSI)
			num_hw_msi++;
		if (sw_msi_start && resv->type == IOMMU_RESV_SW_MSI) {
			*sw_msi_start = resv->start;
			num_sw_msi++;
		}

		rc = iopt_reserve_iova(iopt, resv->start,
				       resv->length - 1 + resv->start, dev);
		if (rc)
			goto out_reserved;
	}

	/* Drivers must offer sane combinations of regions */
	if (WARN_ON(num_sw_msi && num_hw_msi) || WARN_ON(num_sw_msi > 1)) {
		rc = -EINVAL;
		goto out_reserved;
	}

	rc = 0;
	goto out_free_resv;

out_reserved:
	__iopt_remove_reserved_iova(iopt, dev);
out_free_resv:
	iommu_put_resv_regions(dev, &resv_regions);
	up_write(&iopt->iova_rwsem);
	return rc;
}
