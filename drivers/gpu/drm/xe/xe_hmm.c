// SPDX-License-Identifier: MIT
/*
 * Copyright © 2024 Intel Corporation
 */

#include <linux/scatterlist.h>
#include <linux/mmu_notifier.h>
#include <linux/dma-mapping.h>
#include <linux/memremap.h>
#include <linux/swap.h>
#include <linux/hmm.h>
#include <linux/mm.h>
#include "xe_hmm.h"
#include "xe_vm.h"
#include "xe_bo.h"

static u64 xe_npages_in_range(unsigned long start, unsigned long end)
{
	return (end - start) >> PAGE_SHIFT;
}

/*
 * xe_mark_range_accessed() - mark a range is accessed, so core mm
 * have such information for memory eviction or write back to
 * hard disk
 *
 * @range: the range to mark
 * @write: if write to this range, we mark pages in this range
 * as dirty
 */
static void xe_mark_range_accessed(struct hmm_range *range, bool write)
{
	struct page *page;
	u64 i, npages;

	npages = xe_npages_in_range(range->start, range->end);
	for (i = 0; i < npages; i++) {
		page = hmm_pfn_to_page(range->hmm_pfns[i]);
		if (write)
			set_page_dirty_lock(page);

		mark_page_accessed(page);
	}
}

/*
 * xe_build_sg() - build a scatter gather table for all the physical pages/pfn
 * in a hmm_range. dma-map pages if necessary. dma-address is save in sg table
 * and will be used to program GPU page table later.
 *
 * @xe: the xe device who will access the dma-address in sg table
 * @range: the hmm range that we build the sg table from. range->hmm_pfns[]
 * has the pfn numbers of pages that back up this hmm address range.
 * @st: pointer to the sg table.
 * @write: whether we write to this range. This decides dma map direction
 * for system pages. If write we map it bi-diretional; otherwise
 * DMA_TO_DEVICE
 *
 * All the contiguous pfns will be collapsed into one entry in
 * the scatter gather table. This is for the purpose of efficiently
 * programming GPU page table.
 *
 * The dma_address in the sg table will later be used by GPU to
 * access memory. So if the memory is system memory, we need to
 * do a dma-mapping so it can be accessed by GPU/DMA.
 *
 * FIXME: This function currently only support pages in system
 * memory. If the memory is GPU local memory (of the GPU who
 * is going to access memory), we need gpu dpa (device physical
 * address), and there is no need of dma-mapping. This is TBD.
 *
 * FIXME: dma-mapping for peer gpu device to access remote gpu's
 * memory. Add this when you support p2p
 *
 * This function allocates the storage of the sg table. It is
 * caller's responsibility to free it calling sg_free_table.
 *
 * Returns 0 if successful; -ENOMEM if fails to allocate memory
 */
static int xe_build_sg(struct xe_device *xe, struct hmm_range *range,
		       struct sg_table *st, bool write)
{
	struct device *dev = xe->drm.dev;
	struct page **pages;
	u64 i, npages;
	int ret;

	npages = xe_npages_in_range(range->start, range->end);
	pages = kvmalloc_array(npages, sizeof(*pages), GFP_KERNEL);
	if (!pages)
		return -ENOMEM;

	for (i = 0; i < npages; i++) {
		pages[i] = hmm_pfn_to_page(range->hmm_pfns[i]);
		xe_assert(xe, !is_device_private_page(pages[i]));
	}

	ret = sg_alloc_table_from_pages_segment(st, pages, npages, 0, npages << PAGE_SHIFT,
						xe_sg_segment_size(dev), GFP_KERNEL);
	if (ret)
		goto free_pages;

	ret = dma_map_sgtable(dev, st, write ? DMA_BIDIRECTIONAL : DMA_TO_DEVICE,
			      DMA_ATTR_SKIP_CPU_SYNC | DMA_ATTR_NO_KERNEL_MAPPING);
	if (ret) {
		sg_free_table(st);
		st = NULL;
	}

free_pages:
	kvfree(pages);
	return ret;
}

/*
 * xe_hmm_userptr_free_sg() - Free the scatter gather table of userptr
 *
 * @uvma: the userptr vma which hold the scatter gather table
 *
 * With function xe_userptr_populate_range, we allocate storage of
 * the userptr sg table. This is a helper function to free this
 * sg table, and dma unmap the address in the table.
 */
void xe_hmm_userptr_free_sg(struct xe_userptr_vma *uvma)
{
	struct xe_userptr *userptr = &uvma->userptr;
	struct xe_vma *vma = &uvma->vma;
	bool write = !xe_vma_read_only(vma);
	struct xe_vm *vm = xe_vma_vm(vma);
	struct xe_device *xe = vm->xe;
	struct device *dev = xe->drm.dev;

	xe_assert(xe, userptr->sg);
	dma_unmap_sgtable(dev, userptr->sg,
			  write ? DMA_BIDIRECTIONAL : DMA_TO_DEVICE, 0);

	sg_free_table(userptr->sg);
	userptr->sg = NULL;
}

/**
 * xe_hmm_userptr_populate_range() - Populate physical pages of a virtual
 * address range
 *
 * @uvma: userptr vma which has information of the range to populate.
 * @is_mm_mmap_locked: True if mmap_read_lock is already acquired by caller.
 *
 * This function populate the physical pages of a virtual
 * address range. The populated physical pages is saved in
 * userptr's sg table. It is similar to get_user_pages but call
 * hmm_range_fault.
 *
 * This function also read mmu notifier sequence # (
 * mmu_interval_read_begin), for the purpose of later
 * comparison (through mmu_interval_read_retry).
 *
 * This must be called with mmap read or write lock held.
 *
 * This function allocates the storage of the userptr sg table.
 * It is caller's responsibility to free it calling sg_free_table.
 *
 * returns: 0 for succuss; negative error no on failure
 */
int xe_hmm_userptr_populate_range(struct xe_userptr_vma *uvma,
				  bool is_mm_mmap_locked)
{
	unsigned long timeout =
		jiffies + msecs_to_jiffies(HMM_RANGE_DEFAULT_TIMEOUT);
	unsigned long *pfns, flags = HMM_PFN_REQ_FAULT;
	struct xe_userptr *userptr;
	struct xe_vma *vma = &uvma->vma;
	u64 userptr_start = xe_vma_userptr(vma);
	u64 userptr_end = userptr_start + xe_vma_size(vma);
	struct xe_vm *vm = xe_vma_vm(vma);
	struct hmm_range hmm_range;
	bool write = !xe_vma_read_only(vma);
	unsigned long notifier_seq;
	u64 npages;
	int ret;

	userptr = &uvma->userptr;

	if (is_mm_mmap_locked)
		mmap_assert_locked(userptr->notifier.mm);

	if (vma->gpuva.flags & XE_VMA_DESTROYED)
		return 0;

	notifier_seq = mmu_interval_read_begin(&userptr->notifier);
	if (notifier_seq == userptr->notifier_seq)
		return 0;

	if (userptr->sg)
		xe_hmm_userptr_free_sg(uvma);

	npages = xe_npages_in_range(userptr_start, userptr_end);
	pfns = kvmalloc_array(npages, sizeof(*pfns), GFP_KERNEL);
	if (unlikely(!pfns))
		return -ENOMEM;

	if (write)
		flags |= HMM_PFN_REQ_WRITE;

	if (!mmget_not_zero(userptr->notifier.mm)) {
		ret = -EFAULT;
		goto free_pfns;
	}

	hmm_range.default_flags = flags;
	hmm_range.hmm_pfns = pfns;
	hmm_range.notifier = &userptr->notifier;
	hmm_range.start = userptr_start;
	hmm_range.end = userptr_end;
	hmm_range.dev_private_owner = vm->xe;

	while (true) {
		hmm_range.notifier_seq = mmu_interval_read_begin(&userptr->notifier);

		if (!is_mm_mmap_locked)
			mmap_read_lock(userptr->notifier.mm);

		ret = hmm_range_fault(&hmm_range);

		if (!is_mm_mmap_locked)
			mmap_read_unlock(userptr->notifier.mm);

		if (ret == -EBUSY) {
			if (time_after(jiffies, timeout))
				break;

			continue;
		}
		break;
	}

	mmput(userptr->notifier.mm);

	if (ret)
		goto free_pfns;

	ret = xe_build_sg(vm->xe, &hmm_range, &userptr->sgt, write);
	if (ret)
		goto free_pfns;

	xe_mark_range_accessed(&hmm_range, write);
	userptr->sg = &userptr->sgt;
	userptr->notifier_seq = hmm_range.notifier_seq;

free_pfns:
	kvfree(pfns);
	return ret;
}

