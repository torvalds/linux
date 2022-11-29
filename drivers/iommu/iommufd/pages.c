// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021-2022, NVIDIA CORPORATION & AFFILIATES.
 *
 * The iopt_pages is the center of the storage and motion of PFNs. Each
 * iopt_pages represents a logical linear array of full PFNs. The array is 0
 * based and has npages in it. Accessors use 'index' to refer to the entry in
 * this logical array, regardless of its storage location.
 *
 * PFNs are stored in a tiered scheme:
 *  1) iopt_pages::pinned_pfns xarray
 *  2) An iommu_domain
 *  3) The origin of the PFNs, i.e. the userspace pointer
 *
 * PFN have to be copied between all combinations of tiers, depending on the
 * configuration.
 *
 * When a PFN is taken out of the userspace pointer it is pinned exactly once.
 * The storage locations of the PFN's index are tracked in the two interval
 * trees. If no interval includes the index then it is not pinned.
 *
 * If access_itree includes the PFN's index then an in-kernel access has
 * requested the page. The PFN is stored in the xarray so other requestors can
 * continue to find it.
 *
 * If the domains_itree includes the PFN's index then an iommu_domain is storing
 * the PFN and it can be read back using iommu_iova_to_phys(). To avoid
 * duplicating storage the xarray is not used if only iommu_domains are using
 * the PFN's index.
 *
 * As a general principle this is designed so that destroy never fails. This
 * means removing an iommu_domain or releasing a in-kernel access will not fail
 * due to insufficient memory. In practice this means some cases have to hold
 * PFNs in the xarray even though they are also being stored in an iommu_domain.
 *
 * While the iopt_pages can use an iommu_domain as storage, it does not have an
 * IOVA itself. Instead the iopt_area represents a range of IOVA and uses the
 * iopt_pages as the PFN provider. Multiple iopt_areas can share the iopt_pages
 * and reference their own slice of the PFN array, with sub page granularity.
 *
 * In this file the term 'last' indicates an inclusive and closed interval, eg
 * [0,0] refers to a single PFN. 'end' means an open range, eg [0,0) refers to
 * no PFNs.
 *
 * Be cautious of overflow. An IOVA can go all the way up to U64_MAX, so
 * last_iova + 1 can overflow. An iopt_pages index will always be much less than
 * ULONG_MAX so last_index + 1 cannot overflow.
 */
#include <linux/overflow.h>
#include <linux/slab.h>
#include <linux/iommu.h>
#include <linux/sched/mm.h>
#include <linux/highmem.h>
#include <linux/kthread.h>
#include <linux/iommufd.h>

#include "io_pagetable.h"
#include "double_span.h"

#define TEMP_MEMORY_LIMIT 65536
#define BATCH_BACKUP_SIZE 32

/*
 * More memory makes pin_user_pages() and the batching more efficient, but as
 * this is only a performance optimization don't try too hard to get it. A 64k
 * allocation can hold about 26M of 4k pages and 13G of 2M pages in an
 * pfn_batch. Various destroy paths cannot fail and provide a small amount of
 * stack memory as a backup contingency. If backup_len is given this cannot
 * fail.
 */
static void *temp_kmalloc(size_t *size, void *backup, size_t backup_len)
{
	void *res;

	if (WARN_ON(*size == 0))
		return NULL;

	if (*size < backup_len)
		return backup;
	*size = min_t(size_t, *size, TEMP_MEMORY_LIMIT);
	res = kmalloc(*size, GFP_KERNEL | __GFP_NOWARN | __GFP_NORETRY);
	if (res)
		return res;
	*size = PAGE_SIZE;
	if (backup_len) {
		res = kmalloc(*size, GFP_KERNEL | __GFP_NOWARN | __GFP_NORETRY);
		if (res)
			return res;
		*size = backup_len;
		return backup;
	}
	return kmalloc(*size, GFP_KERNEL);
}

void interval_tree_double_span_iter_update(
	struct interval_tree_double_span_iter *iter)
{
	unsigned long last_hole = ULONG_MAX;
	unsigned int i;

	for (i = 0; i != ARRAY_SIZE(iter->spans); i++) {
		if (interval_tree_span_iter_done(&iter->spans[i])) {
			iter->is_used = -1;
			return;
		}

		if (iter->spans[i].is_hole) {
			last_hole = min(last_hole, iter->spans[i].last_hole);
			continue;
		}

		iter->is_used = i + 1;
		iter->start_used = iter->spans[i].start_used;
		iter->last_used = min(iter->spans[i].last_used, last_hole);
		return;
	}

	iter->is_used = 0;
	iter->start_hole = iter->spans[0].start_hole;
	iter->last_hole =
		min(iter->spans[0].last_hole, iter->spans[1].last_hole);
}

void interval_tree_double_span_iter_first(
	struct interval_tree_double_span_iter *iter,
	struct rb_root_cached *itree1, struct rb_root_cached *itree2,
	unsigned long first_index, unsigned long last_index)
{
	unsigned int i;

	iter->itrees[0] = itree1;
	iter->itrees[1] = itree2;
	for (i = 0; i != ARRAY_SIZE(iter->spans); i++)
		interval_tree_span_iter_first(&iter->spans[i], iter->itrees[i],
					      first_index, last_index);
	interval_tree_double_span_iter_update(iter);
}

void interval_tree_double_span_iter_next(
	struct interval_tree_double_span_iter *iter)
{
	unsigned int i;

	if (iter->is_used == -1 ||
	    iter->last_hole == iter->spans[0].last_index) {
		iter->is_used = -1;
		return;
	}

	for (i = 0; i != ARRAY_SIZE(iter->spans); i++)
		interval_tree_span_iter_advance(
			&iter->spans[i], iter->itrees[i], iter->last_hole + 1);
	interval_tree_double_span_iter_update(iter);
}

static void iopt_pages_add_npinned(struct iopt_pages *pages, size_t npages)
{
	pages->npinned += npages;
}

static void iopt_pages_sub_npinned(struct iopt_pages *pages, size_t npages)
{
	pages->npinned -= npages;
}

static void iopt_pages_err_unpin(struct iopt_pages *pages,
				 unsigned long start_index,
				 unsigned long last_index,
				 struct page **page_list)
{
	unsigned long npages = last_index - start_index + 1;

	unpin_user_pages(page_list, npages);
	iopt_pages_sub_npinned(pages, npages);
}

/*
 * index is the number of PAGE_SIZE units from the start of the area's
 * iopt_pages. If the iova is sub page-size then the area has an iova that
 * covers a portion of the first and last pages in the range.
 */
static unsigned long iopt_area_index_to_iova(struct iopt_area *area,
					     unsigned long index)
{
	index -= iopt_area_index(area);
	if (index == 0)
		return iopt_area_iova(area);
	return iopt_area_iova(area) - area->page_offset + index * PAGE_SIZE;
}

static unsigned long iopt_area_index_to_iova_last(struct iopt_area *area,
						  unsigned long index)
{
	if (index == iopt_area_last_index(area))
		return iopt_area_last_iova(area);
	return iopt_area_iova(area) - area->page_offset +
	       (index - iopt_area_index(area) + 1) * PAGE_SIZE - 1;
}

static void iommu_unmap_nofail(struct iommu_domain *domain, unsigned long iova,
			       size_t size)
{
	size_t ret;

	ret = iommu_unmap(domain, iova, size);
	/*
	 * It is a logic error in this code or a driver bug if the IOMMU unmaps
	 * something other than exactly as requested. This implies that the
	 * iommu driver may not fail unmap for reasons beyond bad agruments.
	 * Particularly, the iommu driver may not do a memory allocation on the
	 * unmap path.
	 */
	WARN_ON(ret != size);
}

static struct iopt_area *iopt_pages_find_domain_area(struct iopt_pages *pages,
						     unsigned long index)
{
	struct interval_tree_node *node;

	node = interval_tree_iter_first(&pages->domains_itree, index, index);
	if (!node)
		return NULL;
	return container_of(node, struct iopt_area, pages_node);
}

/*
 * A simple datastructure to hold a vector of PFNs, optimized for contiguous
 * PFNs. This is used as a temporary holding memory for shuttling pfns from one
 * place to another. Generally everything is made more efficient if operations
 * work on the largest possible grouping of pfns. eg fewer lock/unlock cycles,
 * better cache locality, etc
 */
struct pfn_batch {
	unsigned long *pfns;
	u32 *npfns;
	unsigned int array_size;
	unsigned int end;
	unsigned int total_pfns;
};

static void batch_clear(struct pfn_batch *batch)
{
	batch->total_pfns = 0;
	batch->end = 0;
	batch->pfns[0] = 0;
	batch->npfns[0] = 0;
}

/*
 * Carry means we carry a portion of the final hugepage over to the front of the
 * batch
 */
static void batch_clear_carry(struct pfn_batch *batch, unsigned int keep_pfns)
{
	if (!keep_pfns)
		return batch_clear(batch);

	batch->total_pfns = keep_pfns;
	batch->npfns[0] = keep_pfns;
	batch->pfns[0] = batch->pfns[batch->end - 1] +
			 (batch->npfns[batch->end - 1] - keep_pfns);
	batch->end = 0;
}

static void batch_skip_carry(struct pfn_batch *batch, unsigned int skip_pfns)
{
	if (!batch->total_pfns)
		return;
	skip_pfns = min(batch->total_pfns, skip_pfns);
	batch->pfns[0] += skip_pfns;
	batch->npfns[0] -= skip_pfns;
	batch->total_pfns -= skip_pfns;
}

static int __batch_init(struct pfn_batch *batch, size_t max_pages, void *backup,
			size_t backup_len)
{
	const size_t elmsz = sizeof(*batch->pfns) + sizeof(*batch->npfns);
	size_t size = max_pages * elmsz;

	batch->pfns = temp_kmalloc(&size, backup, backup_len);
	if (!batch->pfns)
		return -ENOMEM;
	batch->array_size = size / elmsz;
	batch->npfns = (u32 *)(batch->pfns + batch->array_size);
	batch_clear(batch);
	return 0;
}

static int batch_init(struct pfn_batch *batch, size_t max_pages)
{
	return __batch_init(batch, max_pages, NULL, 0);
}

static void batch_init_backup(struct pfn_batch *batch, size_t max_pages,
			      void *backup, size_t backup_len)
{
	__batch_init(batch, max_pages, backup, backup_len);
}

static void batch_destroy(struct pfn_batch *batch, void *backup)
{
	if (batch->pfns != backup)
		kfree(batch->pfns);
}

/* true if the pfn could be added, false otherwise */
static bool batch_add_pfn(struct pfn_batch *batch, unsigned long pfn)
{
	const unsigned int MAX_NPFNS = type_max(typeof(*batch->npfns));

	if (batch->end &&
	    pfn == batch->pfns[batch->end - 1] + batch->npfns[batch->end - 1] &&
	    batch->npfns[batch->end - 1] != MAX_NPFNS) {
		batch->npfns[batch->end - 1]++;
		batch->total_pfns++;
		return true;
	}
	if (batch->end == batch->array_size)
		return false;
	batch->total_pfns++;
	batch->pfns[batch->end] = pfn;
	batch->npfns[batch->end] = 1;
	batch->end++;
	return true;
}

/*
 * Fill the batch with pfns from the domain. When the batch is full, or it
 * reaches last_index, the function will return. The caller should use
 * batch->total_pfns to determine the starting point for the next iteration.
 */
static void batch_from_domain(struct pfn_batch *batch,
			      struct iommu_domain *domain,
			      struct iopt_area *area, unsigned long start_index,
			      unsigned long last_index)
{
	unsigned int page_offset = 0;
	unsigned long iova;
	phys_addr_t phys;

	iova = iopt_area_index_to_iova(area, start_index);
	if (start_index == iopt_area_index(area))
		page_offset = area->page_offset;
	while (start_index <= last_index) {
		/*
		 * This is pretty slow, it would be nice to get the page size
		 * back from the driver, or have the driver directly fill the
		 * batch.
		 */
		phys = iommu_iova_to_phys(domain, iova) - page_offset;
		if (!batch_add_pfn(batch, PHYS_PFN(phys)))
			return;
		iova += PAGE_SIZE - page_offset;
		page_offset = 0;
		start_index++;
	}
}

static struct page **raw_pages_from_domain(struct iommu_domain *domain,
					   struct iopt_area *area,
					   unsigned long start_index,
					   unsigned long last_index,
					   struct page **out_pages)
{
	unsigned int page_offset = 0;
	unsigned long iova;
	phys_addr_t phys;

	iova = iopt_area_index_to_iova(area, start_index);
	if (start_index == iopt_area_index(area))
		page_offset = area->page_offset;
	while (start_index <= last_index) {
		phys = iommu_iova_to_phys(domain, iova) - page_offset;
		*(out_pages++) = pfn_to_page(PHYS_PFN(phys));
		iova += PAGE_SIZE - page_offset;
		page_offset = 0;
		start_index++;
	}
	return out_pages;
}

/* Continues reading a domain until we reach a discontiguity in the pfns. */
static void batch_from_domain_continue(struct pfn_batch *batch,
				       struct iommu_domain *domain,
				       struct iopt_area *area,
				       unsigned long start_index,
				       unsigned long last_index)
{
	unsigned int array_size = batch->array_size;

	batch->array_size = batch->end;
	batch_from_domain(batch, domain, area, start_index, last_index);
	batch->array_size = array_size;
}

/*
 * This is part of the VFIO compatibility support for VFIO_TYPE1_IOMMU. That
 * mode permits splitting a mapped area up, and then one of the splits is
 * unmapped. Doing this normally would cause us to violate our invariant of
 * pairing map/unmap. Thus, to support old VFIO compatibility disable support
 * for batching consecutive PFNs. All PFNs mapped into the iommu are done in
 * PAGE_SIZE units, not larger or smaller.
 */
static int batch_iommu_map_small(struct iommu_domain *domain,
				 unsigned long iova, phys_addr_t paddr,
				 size_t size, int prot)
{
	unsigned long start_iova = iova;
	int rc;

	while (size) {
		rc = iommu_map(domain, iova, paddr, PAGE_SIZE, prot);
		if (rc)
			goto err_unmap;
		iova += PAGE_SIZE;
		paddr += PAGE_SIZE;
		size -= PAGE_SIZE;
	}
	return 0;

err_unmap:
	if (start_iova != iova)
		iommu_unmap_nofail(domain, start_iova, iova - start_iova);
	return rc;
}

static int batch_to_domain(struct pfn_batch *batch, struct iommu_domain *domain,
			   struct iopt_area *area, unsigned long start_index)
{
	bool disable_large_pages = area->iopt->disable_large_pages;
	unsigned long last_iova = iopt_area_last_iova(area);
	unsigned int page_offset = 0;
	unsigned long start_iova;
	unsigned long next_iova;
	unsigned int cur = 0;
	unsigned long iova;
	int rc;

	/* The first index might be a partial page */
	if (start_index == iopt_area_index(area))
		page_offset = area->page_offset;
	next_iova = iova = start_iova =
		iopt_area_index_to_iova(area, start_index);
	while (cur < batch->end) {
		next_iova = min(last_iova + 1,
				next_iova + batch->npfns[cur] * PAGE_SIZE -
					page_offset);
		if (disable_large_pages)
			rc = batch_iommu_map_small(
				domain, iova,
				PFN_PHYS(batch->pfns[cur]) + page_offset,
				next_iova - iova, area->iommu_prot);
		else
			rc = iommu_map(domain, iova,
				       PFN_PHYS(batch->pfns[cur]) + page_offset,
				       next_iova - iova, area->iommu_prot);
		if (rc)
			goto err_unmap;
		iova = next_iova;
		page_offset = 0;
		cur++;
	}
	return 0;
err_unmap:
	if (start_iova != iova)
		iommu_unmap_nofail(domain, start_iova, iova - start_iova);
	return rc;
}

static void batch_from_xarray(struct pfn_batch *batch, struct xarray *xa,
			      unsigned long start_index,
			      unsigned long last_index)
{
	XA_STATE(xas, xa, start_index);
	void *entry;

	rcu_read_lock();
	while (true) {
		entry = xas_next(&xas);
		if (xas_retry(&xas, entry))
			continue;
		WARN_ON(!xa_is_value(entry));
		if (!batch_add_pfn(batch, xa_to_value(entry)) ||
		    start_index == last_index)
			break;
		start_index++;
	}
	rcu_read_unlock();
}

static void batch_from_xarray_clear(struct pfn_batch *batch, struct xarray *xa,
				    unsigned long start_index,
				    unsigned long last_index)
{
	XA_STATE(xas, xa, start_index);
	void *entry;

	xas_lock(&xas);
	while (true) {
		entry = xas_next(&xas);
		if (xas_retry(&xas, entry))
			continue;
		WARN_ON(!xa_is_value(entry));
		if (!batch_add_pfn(batch, xa_to_value(entry)))
			break;
		xas_store(&xas, NULL);
		if (start_index == last_index)
			break;
		start_index++;
	}
	xas_unlock(&xas);
}

static void clear_xarray(struct xarray *xa, unsigned long start_index,
			 unsigned long last_index)
{
	XA_STATE(xas, xa, start_index);
	void *entry;

	xas_lock(&xas);
	xas_for_each(&xas, entry, last_index)
		xas_store(&xas, NULL);
	xas_unlock(&xas);
}

static int pages_to_xarray(struct xarray *xa, unsigned long start_index,
			   unsigned long last_index, struct page **pages)
{
	struct page **end_pages = pages + (last_index - start_index) + 1;
	XA_STATE(xas, xa, start_index);

	do {
		void *old;

		xas_lock(&xas);
		while (pages != end_pages) {
			old = xas_store(&xas, xa_mk_value(page_to_pfn(*pages)));
			if (xas_error(&xas))
				break;
			WARN_ON(old);
			pages++;
			xas_next(&xas);
		}
		xas_unlock(&xas);
	} while (xas_nomem(&xas, GFP_KERNEL));

	if (xas_error(&xas)) {
		if (xas.xa_index != start_index)
			clear_xarray(xa, start_index, xas.xa_index - 1);
		return xas_error(&xas);
	}
	return 0;
}

static void batch_from_pages(struct pfn_batch *batch, struct page **pages,
			     size_t npages)
{
	struct page **end = pages + npages;

	for (; pages != end; pages++)
		if (!batch_add_pfn(batch, page_to_pfn(*pages)))
			break;
}

static void batch_unpin(struct pfn_batch *batch, struct iopt_pages *pages,
			unsigned int first_page_off, size_t npages)
{
	unsigned int cur = 0;

	while (first_page_off) {
		if (batch->npfns[cur] > first_page_off)
			break;
		first_page_off -= batch->npfns[cur];
		cur++;
	}

	while (npages) {
		size_t to_unpin = min_t(size_t, npages,
					batch->npfns[cur] - first_page_off);

		unpin_user_page_range_dirty_lock(
			pfn_to_page(batch->pfns[cur] + first_page_off),
			to_unpin, pages->writable);
		iopt_pages_sub_npinned(pages, to_unpin);
		cur++;
		first_page_off = 0;
		npages -= to_unpin;
	}
}

static void copy_data_page(struct page *page, void *data, unsigned long offset,
			   size_t length, unsigned int flags)
{
	void *mem;

	mem = kmap_local_page(page);
	if (flags & IOMMUFD_ACCESS_RW_WRITE) {
		memcpy(mem + offset, data, length);
		set_page_dirty_lock(page);
	} else {
		memcpy(data, mem + offset, length);
	}
	kunmap_local(mem);
}

static unsigned long batch_rw(struct pfn_batch *batch, void *data,
			      unsigned long offset, unsigned long length,
			      unsigned int flags)
{
	unsigned long copied = 0;
	unsigned int npage = 0;
	unsigned int cur = 0;

	while (cur < batch->end) {
		unsigned long bytes = min(length, PAGE_SIZE - offset);

		copy_data_page(pfn_to_page(batch->pfns[cur] + npage), data,
			       offset, bytes, flags);
		offset = 0;
		length -= bytes;
		data += bytes;
		copied += bytes;
		npage++;
		if (npage == batch->npfns[cur]) {
			npage = 0;
			cur++;
		}
		if (!length)
			break;
	}
	return copied;
}

/* pfn_reader_user is just the pin_user_pages() path */
struct pfn_reader_user {
	struct page **upages;
	size_t upages_len;
	unsigned long upages_start;
	unsigned long upages_end;
	unsigned int gup_flags;
	/*
	 * 1 means mmget() and mmap_read_lock(), 0 means only mmget(), -1 is
	 * neither
	 */
	int locked;
};

static void pfn_reader_user_init(struct pfn_reader_user *user,
				 struct iopt_pages *pages)
{
	user->upages = NULL;
	user->upages_start = 0;
	user->upages_end = 0;
	user->locked = -1;

	if (pages->writable) {
		user->gup_flags = FOLL_LONGTERM | FOLL_WRITE;
	} else {
		/* Still need to break COWs on read */
		user->gup_flags = FOLL_LONGTERM | FOLL_FORCE | FOLL_WRITE;
	}
}

static void pfn_reader_user_destroy(struct pfn_reader_user *user,
				    struct iopt_pages *pages)
{
	if (user->locked != -1) {
		if (user->locked)
			mmap_read_unlock(pages->source_mm);
		if (pages->source_mm != current->mm)
			mmput(pages->source_mm);
		user->locked = 0;
	}

	kfree(user->upages);
	user->upages = NULL;
}

static int pfn_reader_user_pin(struct pfn_reader_user *user,
			       struct iopt_pages *pages,
			       unsigned long start_index,
			       unsigned long last_index)
{
	bool remote_mm = pages->source_mm != current->mm;
	unsigned long npages;
	uintptr_t uptr;
	long rc;

	if (!user->upages) {
		/* All undone in pfn_reader_destroy() */
		user->upages_len =
			(last_index - start_index + 1) * sizeof(*user->upages);
		user->upages = temp_kmalloc(&user->upages_len, NULL, 0);
		if (!user->upages)
			return -ENOMEM;
	}

	if (user->locked == -1) {
		/*
		 * The majority of usages will run the map task within the mm
		 * providing the pages, so we can optimize into
		 * get_user_pages_fast()
		 */
		if (remote_mm) {
			if (!mmget_not_zero(pages->source_mm))
				return -EFAULT;
		}
		user->locked = 0;
	}

	npages = min_t(unsigned long, last_index - start_index + 1,
		       user->upages_len / sizeof(*user->upages));

	uptr = (uintptr_t)(pages->uptr + start_index * PAGE_SIZE);
	if (!remote_mm)
		rc = pin_user_pages_fast(uptr, npages, user->gup_flags,
					 user->upages);
	else {
		if (!user->locked) {
			mmap_read_lock(pages->source_mm);
			user->locked = 1;
		}
		/*
		 * FIXME: last NULL can be &pfns->locked once the GUP patch
		 * is merged.
		 */
		rc = pin_user_pages_remote(pages->source_mm, uptr, npages,
					   user->gup_flags, user->upages, NULL,
					   NULL);
	}
	if (rc <= 0) {
		if (WARN_ON(!rc))
			return -EFAULT;
		return rc;
	}
	iopt_pages_add_npinned(pages, rc);
	user->upages_start = start_index;
	user->upages_end = start_index + rc;
	return 0;
}

/* This is the "modern" and faster accounting method used by io_uring */
static int incr_user_locked_vm(struct iopt_pages *pages, unsigned long npages)
{
	unsigned long lock_limit;
	unsigned long cur_pages;
	unsigned long new_pages;

	lock_limit = task_rlimit(pages->source_task, RLIMIT_MEMLOCK) >>
		     PAGE_SHIFT;
	npages = pages->npinned - pages->last_npinned;
	do {
		cur_pages = atomic_long_read(&pages->source_user->locked_vm);
		new_pages = cur_pages + npages;
		if (new_pages > lock_limit)
			return -ENOMEM;
	} while (atomic_long_cmpxchg(&pages->source_user->locked_vm, cur_pages,
				     new_pages) != cur_pages);
	return 0;
}

static void decr_user_locked_vm(struct iopt_pages *pages, unsigned long npages)
{
	if (WARN_ON(atomic_long_read(&pages->source_user->locked_vm) < npages))
		return;
	atomic_long_sub(npages, &pages->source_user->locked_vm);
}

/* This is the accounting method used for compatibility with VFIO */
static int update_mm_locked_vm(struct iopt_pages *pages, unsigned long npages,
			       bool inc, struct pfn_reader_user *user)
{
	bool do_put = false;
	int rc;

	if (user && user->locked) {
		mmap_read_unlock(pages->source_mm);
		user->locked = 0;
		/* If we had the lock then we also have a get */
	} else if ((!user || !user->upages) &&
		   pages->source_mm != current->mm) {
		if (!mmget_not_zero(pages->source_mm))
			return -EINVAL;
		do_put = true;
	}

	mmap_write_lock(pages->source_mm);
	rc = __account_locked_vm(pages->source_mm, npages, inc,
				 pages->source_task, false);
	mmap_write_unlock(pages->source_mm);

	if (do_put)
		mmput(pages->source_mm);
	return rc;
}

static int do_update_pinned(struct iopt_pages *pages, unsigned long npages,
			    bool inc, struct pfn_reader_user *user)
{
	int rc = 0;

	switch (pages->account_mode) {
	case IOPT_PAGES_ACCOUNT_NONE:
		break;
	case IOPT_PAGES_ACCOUNT_USER:
		if (inc)
			rc = incr_user_locked_vm(pages, npages);
		else
			decr_user_locked_vm(pages, npages);
		break;
	case IOPT_PAGES_ACCOUNT_MM:
		rc = update_mm_locked_vm(pages, npages, inc, user);
		break;
	}
	if (rc)
		return rc;

	pages->last_npinned = pages->npinned;
	if (inc)
		atomic64_add(npages, &pages->source_mm->pinned_vm);
	else
		atomic64_sub(npages, &pages->source_mm->pinned_vm);
	return 0;
}

static void update_unpinned(struct iopt_pages *pages)
{
	if (WARN_ON(pages->npinned > pages->last_npinned))
		return;
	if (pages->npinned == pages->last_npinned)
		return;
	do_update_pinned(pages, pages->last_npinned - pages->npinned, false,
			 NULL);
}

/*
 * Changes in the number of pages pinned is done after the pages have been read
 * and processed. If the user lacked the limit then the error unwind will unpin
 * everything that was just pinned. This is because it is expensive to calculate
 * how many pages we have already pinned within a range to generate an accurate
 * prediction in advance of doing the work to actually pin them.
 */
static int pfn_reader_user_update_pinned(struct pfn_reader_user *user,
					 struct iopt_pages *pages)
{
	unsigned long npages;
	bool inc;

	lockdep_assert_held(&pages->mutex);

	if (pages->npinned == pages->last_npinned)
		return 0;

	if (pages->npinned < pages->last_npinned) {
		npages = pages->last_npinned - pages->npinned;
		inc = false;
	} else {
		npages = pages->npinned - pages->last_npinned;
		inc = true;
	}
	return do_update_pinned(pages, npages, inc, user);
}

/*
 * PFNs are stored in three places, in order of preference:
 * - The iopt_pages xarray. This is only populated if there is a
 *   iopt_pages_access
 * - The iommu_domain under an area
 * - The original PFN source, ie pages->source_mm
 *
 * This iterator reads the pfns optimizing to load according to the
 * above order.
 */
struct pfn_reader {
	struct iopt_pages *pages;
	struct interval_tree_double_span_iter span;
	struct pfn_batch batch;
	unsigned long batch_start_index;
	unsigned long batch_end_index;
	unsigned long last_index;

	struct pfn_reader_user user;
};

static int pfn_reader_update_pinned(struct pfn_reader *pfns)
{
	return pfn_reader_user_update_pinned(&pfns->user, pfns->pages);
}

/*
 * The batch can contain a mixture of pages that are still in use and pages that
 * need to be unpinned. Unpin only pages that are not held anywhere else.
 */
static void pfn_reader_unpin(struct pfn_reader *pfns)
{
	unsigned long last = pfns->batch_end_index - 1;
	unsigned long start = pfns->batch_start_index;
	struct interval_tree_double_span_iter span;
	struct iopt_pages *pages = pfns->pages;

	lockdep_assert_held(&pages->mutex);

	interval_tree_for_each_double_span(&span, &pages->access_itree,
					   &pages->domains_itree, start, last) {
		if (span.is_used)
			continue;

		batch_unpin(&pfns->batch, pages, span.start_hole - start,
			    span.last_hole - span.start_hole + 1);
	}
}

/* Process a single span to load it from the proper storage */
static int pfn_reader_fill_span(struct pfn_reader *pfns)
{
	struct interval_tree_double_span_iter *span = &pfns->span;
	unsigned long start_index = pfns->batch_end_index;
	struct iopt_area *area;
	int rc;

	if (span->is_used == 1) {
		batch_from_xarray(&pfns->batch, &pfns->pages->pinned_pfns,
				  start_index, span->last_used);
		return 0;
	}

	if (span->is_used == 2) {
		/*
		 * Pull as many pages from the first domain we find in the
		 * target span. If it is too small then we will be called again
		 * and we'll find another area.
		 */
		area = iopt_pages_find_domain_area(pfns->pages, start_index);
		if (WARN_ON(!area))
			return -EINVAL;

		/* The storage_domain cannot change without the pages mutex */
		batch_from_domain(
			&pfns->batch, area->storage_domain, area, start_index,
			min(iopt_area_last_index(area), span->last_used));
		return 0;
	}

	if (start_index >= pfns->user.upages_end) {
		rc = pfn_reader_user_pin(&pfns->user, pfns->pages, start_index,
					 span->last_hole);
		if (rc)
			return rc;
	}

	batch_from_pages(&pfns->batch,
			 pfns->user.upages +
				 (start_index - pfns->user.upages_start),
			 pfns->user.upages_end - start_index);
	return 0;
}

static bool pfn_reader_done(struct pfn_reader *pfns)
{
	return pfns->batch_start_index == pfns->last_index + 1;
}

static int pfn_reader_next(struct pfn_reader *pfns)
{
	int rc;

	batch_clear(&pfns->batch);
	pfns->batch_start_index = pfns->batch_end_index;

	while (pfns->batch_end_index != pfns->last_index + 1) {
		unsigned int npfns = pfns->batch.total_pfns;

		rc = pfn_reader_fill_span(pfns);
		if (rc)
			return rc;

		if (WARN_ON(!pfns->batch.total_pfns))
			return -EINVAL;

		pfns->batch_end_index =
			pfns->batch_start_index + pfns->batch.total_pfns;
		if (pfns->batch_end_index == pfns->span.last_used + 1)
			interval_tree_double_span_iter_next(&pfns->span);

		/* Batch is full */
		if (npfns == pfns->batch.total_pfns)
			return 0;
	}
	return 0;
}

static int pfn_reader_init(struct pfn_reader *pfns, struct iopt_pages *pages,
			   unsigned long start_index, unsigned long last_index)
{
	int rc;

	lockdep_assert_held(&pages->mutex);

	pfns->pages = pages;
	pfns->batch_start_index = start_index;
	pfns->batch_end_index = start_index;
	pfns->last_index = last_index;
	pfn_reader_user_init(&pfns->user, pages);
	rc = batch_init(&pfns->batch, last_index - start_index + 1);
	if (rc)
		return rc;
	interval_tree_double_span_iter_first(&pfns->span, &pages->access_itree,
					     &pages->domains_itree, start_index,
					     last_index);
	return 0;
}

/*
 * There are many assertions regarding the state of pages->npinned vs
 * pages->last_pinned, for instance something like unmapping a domain must only
 * decrement the npinned, and pfn_reader_destroy() must be called only after all
 * the pins are updated. This is fine for success flows, but error flows
 * sometimes need to release the pins held inside the pfn_reader before going on
 * to complete unmapping and releasing pins held in domains.
 */
static void pfn_reader_release_pins(struct pfn_reader *pfns)
{
	struct iopt_pages *pages = pfns->pages;

	if (pfns->user.upages_end > pfns->batch_end_index) {
		size_t npages = pfns->user.upages_end - pfns->batch_end_index;

		/* Any pages not transferred to the batch are just unpinned */
		unpin_user_pages(pfns->user.upages + (pfns->batch_end_index -
						      pfns->user.upages_start),
				 npages);
		iopt_pages_sub_npinned(pages, npages);
		pfns->user.upages_end = pfns->batch_end_index;
	}
	if (pfns->batch_start_index != pfns->batch_end_index) {
		pfn_reader_unpin(pfns);
		pfns->batch_start_index = pfns->batch_end_index;
	}
}

static void pfn_reader_destroy(struct pfn_reader *pfns)
{
	struct iopt_pages *pages = pfns->pages;

	pfn_reader_release_pins(pfns);
	pfn_reader_user_destroy(&pfns->user, pfns->pages);
	batch_destroy(&pfns->batch, NULL);
	WARN_ON(pages->last_npinned != pages->npinned);
}

static int pfn_reader_first(struct pfn_reader *pfns, struct iopt_pages *pages,
			    unsigned long start_index, unsigned long last_index)
{
	int rc;

	rc = pfn_reader_init(pfns, pages, start_index, last_index);
	if (rc)
		return rc;
	rc = pfn_reader_next(pfns);
	if (rc) {
		pfn_reader_destroy(pfns);
		return rc;
	}
	return 0;
}
