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

/**
 * xe_mark_range_accessed() - mark a range is accessed, so core mm
 * have such information for memory eviction or write back to
 * hard disk
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

static int xe_alloc_sg(struct xe_device *xe, struct sg_table *st,
		       struct hmm_range *range, struct rw_semaphore *notifier_sem)
{
	unsigned long i, npages, hmm_pfn;
	unsigned long num_chunks = 0;
	int ret;

	/* HMM docs says this is needed. */
	ret = down_read_interruptible(notifier_sem);
	if (ret)
		return ret;

	if (mmu_interval_read_retry(range->notifier, range->notifier_seq)) {
		up_read(notifier_sem);
		return -EAGAIN;
	}

	npages = xe_npages_in_range(range->start, range->end);
	for (i = 0; i < npages;) {
		unsigned long len;

		hmm_pfn = range->hmm_pfns[i];
		xe_assert(xe, hmm_pfn & HMM_PFN_VALID);

		len = 1UL << hmm_pfn_to_map_order(hmm_pfn);

		/* If order > 0 the page may extend beyond range->start */
		len -= (hmm_pfn & ~HMM_PFN_FLAGS) & (len - 1);
		i += len;
		num_chunks++;
	}
	up_read(notifier_sem);

	return sg_alloc_table(st, num_chunks, GFP_KERNEL);
}

/**
 * xe_build_sg() - build a scatter gather table for all the physical pages/pfn
 * in a hmm_range. dma-map pages if necessary. dma-address is save in sg table
 * and will be used to program GPU page table later.
 * @xe: the xe device who will access the dma-address in sg table
 * @range: the hmm range that we build the sg table from. range->hmm_pfns[]
 * has the pfn numbers of pages that back up this hmm address range.
 * @st: pointer to the sg table.
 * @notifier_sem: The xe notifier lock.
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
		       struct sg_table *st,
		       struct rw_semaphore *notifier_sem,
		       bool write)
{
	unsigned long npages = xe_npages_in_range(range->start, range->end);
	struct device *dev = xe->drm.dev;
	struct scatterlist *sgl;
	struct page *page;
	unsigned long i, j;

	lockdep_assert_held(notifier_sem);

	i = 0;
	for_each_sg(st->sgl, sgl, st->nents, j) {
		unsigned long hmm_pfn, size;

		hmm_pfn = range->hmm_pfns[i];
		page = hmm_pfn_to_page(hmm_pfn);
		xe_assert(xe, !is_device_private_page(page));

		size = 1UL << hmm_pfn_to_map_order(hmm_pfn);
		size -= page_to_pfn(page) & (size - 1);
		i += size;

		if (unlikely(j == st->nents - 1)) {
			xe_assert(xe, i >= npages);
			if (i > npages)
				size -= (i - npages);

			sg_mark_end(sgl);
		} else {
			xe_assert(xe, i < npages);
		}

		sg_set_page(sgl, page, size << PAGE_SHIFT, 0);
	}

	return dma_map_sgtable(dev, st, write ? DMA_BIDIRECTIONAL : DMA_TO_DEVICE,
			       DMA_ATTR_SKIP_CPU_SYNC | DMA_ATTR_NO_KERNEL_MAPPING);
}

static void xe_hmm_userptr_set_mapped(struct xe_userptr_vma *uvma)
{
	struct xe_userptr *userptr = &uvma->userptr;
	struct xe_vm *vm = xe_vma_vm(&uvma->vma);

	lockdep_assert_held_write(&vm->lock);
	lockdep_assert_held(&vm->userptr.notifier_lock);

	mutex_lock(&userptr->unmap_mutex);
	xe_assert(vm->xe, !userptr->mapped);
	userptr->mapped = true;
	mutex_unlock(&userptr->unmap_mutex);
}

void xe_hmm_userptr_unmap(struct xe_userptr_vma *uvma)
{
	struct xe_userptr *userptr = &uvma->userptr;
	struct xe_vma *vma = &uvma->vma;
	bool write = !xe_vma_read_only(vma);
	struct xe_vm *vm = xe_vma_vm(vma);
	struct xe_device *xe = vm->xe;

	if (!lockdep_is_held_type(&vm->userptr.notifier_lock, 0) &&
	    !lockdep_is_held_type(&vm->lock, 0) &&
	    !(vma->gpuva.flags & XE_VMA_DESTROYED)) {
		/* Don't unmap in exec critical section. */
		xe_vm_assert_held(vm);
		/* Don't unmap while mapping the sg. */
		lockdep_assert_held(&vm->lock);
	}

	mutex_lock(&userptr->unmap_mutex);
	if (userptr->sg && userptr->mapped)
		dma_unmap_sgtable(xe->drm.dev, userptr->sg,
				  write ? DMA_BIDIRECTIONAL : DMA_TO_DEVICE, 0);
	userptr->mapped = false;
	mutex_unlock(&userptr->unmap_mutex);
}

/**
 * xe_hmm_userptr_free_sg() - Free the scatter gather table of userptr
 * @uvma: the userptr vma which hold the scatter gather table
 *
 * With function xe_userptr_populate_range, we allocate storage of
 * the userptr sg table. This is a helper function to free this
 * sg table, and dma unmap the address in the table.
 */
void xe_hmm_userptr_free_sg(struct xe_userptr_vma *uvma)
{
	struct xe_userptr *userptr = &uvma->userptr;

	xe_assert(xe_vma_vm(&uvma->vma)->xe, userptr->sg);
	xe_hmm_userptr_unmap(uvma);
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
 * returns: 0 for success; negative error no on failure
 */
int xe_hmm_userptr_populate_range(struct xe_userptr_vma *uvma,
				  bool is_mm_mmap_locked)
{
	unsigned long timeout =
		jiffies + msecs_to_jiffies(HMM_RANGE_DEFAULT_TIMEOUT);
	unsigned long *pfns;
	struct xe_userptr *userptr;
	struct xe_vma *vma = &uvma->vma;
	u64 userptr_start = xe_vma_userptr(vma);
	u64 userptr_end = userptr_start + xe_vma_size(vma);
	struct xe_vm *vm = xe_vma_vm(vma);
	struct hmm_range hmm_range = {
		.pfn_flags_mask = 0, /* ignore pfns */
		.default_flags = HMM_PFN_REQ_FAULT,
		.start = userptr_start,
		.end = userptr_end,
		.notifier = &uvma->userptr.notifier,
		.dev_private_owner = vm->xe,
	};
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
		hmm_range.default_flags |= HMM_PFN_REQ_WRITE;

	if (!mmget_not_zero(userptr->notifier.mm)) {
		ret = -EFAULT;
		goto free_pfns;
	}

	hmm_range.hmm_pfns = pfns;

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

	ret = xe_alloc_sg(vm->xe, &userptr->sgt, &hmm_range, &vm->userptr.notifier_lock);
	if (ret)
		goto free_pfns;

	ret = down_read_interruptible(&vm->userptr.notifier_lock);
	if (ret)
		goto free_st;

	if (mmu_interval_read_retry(hmm_range.notifier, hmm_range.notifier_seq)) {
		ret = -EAGAIN;
		goto out_unlock;
	}

	ret = xe_build_sg(vm->xe, &hmm_range, &userptr->sgt,
			  &vm->userptr.notifier_lock, write);
	if (ret)
		goto out_unlock;

	xe_mark_range_accessed(&hmm_range, write);
	userptr->sg = &userptr->sgt;
	xe_hmm_userptr_set_mapped(uvma);
	userptr->notifier_seq = hmm_range.notifier_seq;
	up_read(&vm->userptr.notifier_lock);
	kvfree(pfns);
	return 0;

out_unlock:
	up_read(&vm->userptr.notifier_lock);
free_st:
	sg_free_table(&userptr->sgt);
free_pfns:
	kvfree(pfns);
	return ret;
}
