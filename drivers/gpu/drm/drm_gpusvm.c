// SPDX-License-Identifier: GPL-2.0-only OR MIT
/*
 * Copyright © 2024 Intel Corporation
 *
 * Authors:
 *     Matthew Brost <matthew.brost@intel.com>
 */

#include <linux/dma-mapping.h>
#include <linux/export.h>
#include <linux/hmm.h>
#include <linux/hugetlb_inline.h>
#include <linux/memremap.h>
#include <linux/mm_types.h>
#include <linux/slab.h>

#include <drm/drm_device.h>
#include <drm/drm_gpusvm.h>
#include <drm/drm_pagemap.h>
#include <drm/drm_print.h>

/**
 * DOC: Overview
 *
 * GPU Shared Virtual Memory (GPU SVM) layer for the Direct Rendering Manager (DRM)
 * is a component of the DRM framework designed to manage shared virtual memory
 * between the CPU and GPU. It enables efficient data exchange and processing
 * for GPU-accelerated applications by allowing memory sharing and
 * synchronization between the CPU's and GPU's virtual address spaces.
 *
 * Key GPU SVM Components:
 *
 * - Notifiers:
 *	Used for tracking memory intervals and notifying the GPU of changes,
 *	notifiers are sized based on a GPU SVM initialization parameter, with a
 *	recommendation of 512M or larger. They maintain a Red-BlacK tree and a
 *	list of ranges that fall within the notifier interval.  Notifiers are
 *	tracked within a GPU SVM Red-BlacK tree and list and are dynamically
 *	inserted or removed as ranges within the interval are created or
 *	destroyed.
 * - Ranges:
 *	Represent memory ranges mapped in a DRM device and managed by GPU SVM.
 *	They are sized based on an array of chunk sizes, which is a GPU SVM
 *	initialization parameter, and the CPU address space.  Upon GPU fault,
 *	the largest aligned chunk that fits within the faulting CPU address
 *	space is chosen for the range size. Ranges are expected to be
 *	dynamically allocated on GPU fault and removed on an MMU notifier UNMAP
 *	event. As mentioned above, ranges are tracked in a notifier's Red-Black
 *	tree.
 *
 * - Operations:
 *	Define the interface for driver-specific GPU SVM operations such as
 *	range allocation, notifier allocation, and invalidations.
 *
 * - Device Memory Allocations:
 *	Embedded structure containing enough information for GPU SVM to migrate
 *	to / from device memory.
 *
 * - Device Memory Operations:
 *	Define the interface for driver-specific device memory operations
 *	release memory, populate pfns, and copy to / from device memory.
 *
 * This layer provides interfaces for allocating, mapping, migrating, and
 * releasing memory ranges between the CPU and GPU. It handles all core memory
 * management interactions (DMA mapping, HMM, and migration) and provides
 * driver-specific virtual functions (vfuncs). This infrastructure is sufficient
 * to build the expected driver components for an SVM implementation as detailed
 * below.
 *
 * Expected Driver Components:
 *
 * - GPU page fault handler:
 *	Used to create ranges and notifiers based on the fault address,
 *	optionally migrate the range to device memory, and create GPU bindings.
 *
 * - Garbage collector:
 *	Used to unmap and destroy GPU bindings for ranges.  Ranges are expected
 *	to be added to the garbage collector upon a MMU_NOTIFY_UNMAP event in
 *	notifier callback.
 *
 * - Notifier callback:
 *	Used to invalidate and DMA unmap GPU bindings for ranges.
 */

/**
 * DOC: Locking
 *
 * GPU SVM handles locking for core MM interactions, i.e., it locks/unlocks the
 * mmap lock as needed.
 *
 * GPU SVM introduces a global notifier lock, which safeguards the notifier's
 * range RB tree and list, as well as the range's DMA mappings and sequence
 * number. GPU SVM manages all necessary locking and unlocking operations,
 * except for the recheck range's pages being valid
 * (drm_gpusvm_range_pages_valid) when the driver is committing GPU bindings.
 * This lock corresponds to the ``driver->update`` lock mentioned in
 * Documentation/mm/hmm.rst. Future revisions may transition from a GPU SVM
 * global lock to a per-notifier lock if finer-grained locking is deemed
 * necessary.
 *
 * In addition to the locking mentioned above, the driver should implement a
 * lock to safeguard core GPU SVM function calls that modify state, such as
 * drm_gpusvm_range_find_or_insert and drm_gpusvm_range_remove. This lock is
 * denoted as 'driver_svm_lock' in code examples. Finer grained driver side
 * locking should also be possible for concurrent GPU fault processing within a
 * single GPU SVM. The 'driver_svm_lock' can be via drm_gpusvm_driver_set_lock
 * to add annotations to GPU SVM.
 */

/**
 * DOC: Partial Unmapping of Ranges
 *
 * Partial unmapping of ranges (e.g., 1M out of 2M is unmapped by CPU resulting
 * in MMU_NOTIFY_UNMAP event) presents several challenges, with the main one
 * being that a subset of the range still has CPU and GPU mappings. If the
 * backing store for the range is in device memory, a subset of the backing
 * store has references. One option would be to split the range and device
 * memory backing store, but the implementation for this would be quite
 * complicated. Given that partial unmappings are rare and driver-defined range
 * sizes are relatively small, GPU SVM does not support splitting of ranges.
 *
 * With no support for range splitting, upon partial unmapping of a range, the
 * driver is expected to invalidate and destroy the entire range. If the range
 * has device memory as its backing, the driver is also expected to migrate any
 * remaining pages back to RAM.
 */

/**
 * DOC: Examples
 *
 * This section provides three examples of how to build the expected driver
 * components: the GPU page fault handler, the garbage collector, and the
 * notifier callback.
 *
 * The generic code provided does not include logic for complex migration
 * policies, optimized invalidations, fined grained driver locking, or other
 * potentially required driver locking (e.g., DMA-resv locks).
 *
 * 1) GPU page fault handler
 *
 * .. code-block:: c
 *
 *	int driver_bind_range(struct drm_gpusvm *gpusvm, struct drm_gpusvm_range *range)
 *	{
 *		int err = 0;
 *
 *		driver_alloc_and_setup_memory_for_bind(gpusvm, range);
 *
 *		drm_gpusvm_notifier_lock(gpusvm);
 *		if (drm_gpusvm_range_pages_valid(range))
 *			driver_commit_bind(gpusvm, range);
 *		else
 *			err = -EAGAIN;
 *		drm_gpusvm_notifier_unlock(gpusvm);
 *
 *		return err;
 *	}
 *
 *	int driver_gpu_fault(struct drm_gpusvm *gpusvm, unsigned long fault_addr,
 *			     unsigned long gpuva_start, unsigned long gpuva_end)
 *	{
 *		struct drm_gpusvm_ctx ctx = {};
 *		int err;
 *
 *		driver_svm_lock();
 *	retry:
 *		// Always process UNMAPs first so view of GPU SVM ranges is current
 *		driver_garbage_collector(gpusvm);
 *
 *		range = drm_gpusvm_range_find_or_insert(gpusvm, fault_addr,
 *							gpuva_start, gpuva_end,
 *						        &ctx);
 *		if (IS_ERR(range)) {
 *			err = PTR_ERR(range);
 *			goto unlock;
 *		}
 *
 *		if (driver_migration_policy(range)) {
 *			err = drm_pagemap_populate_mm(driver_choose_drm_pagemap(),
 *						      gpuva_start, gpuva_end, gpusvm->mm,
 *						      ctx->timeslice_ms);
 *			if (err)	// CPU mappings may have changed
 *				goto retry;
 *		}
 *
 *		err = drm_gpusvm_range_get_pages(gpusvm, range, &ctx);
 *		if (err == -EOPNOTSUPP || err == -EFAULT || err == -EPERM) {	// CPU mappings changed
 *			if (err == -EOPNOTSUPP)
 *				drm_gpusvm_range_evict(gpusvm, range);
 *			goto retry;
 *		} else if (err) {
 *			goto unlock;
 *		}
 *
 *		err = driver_bind_range(gpusvm, range);
 *		if (err == -EAGAIN)	// CPU mappings changed
 *			goto retry
 *
 *	unlock:
 *		driver_svm_unlock();
 *		return err;
 *	}
 *
 * 2) Garbage Collector
 *
 * .. code-block:: c
 *
 *	void __driver_garbage_collector(struct drm_gpusvm *gpusvm,
 *					struct drm_gpusvm_range *range)
 *	{
 *		assert_driver_svm_locked(gpusvm);
 *
 *		// Partial unmap, migrate any remaining device memory pages back to RAM
 *		if (range->flags.partial_unmap)
 *			drm_gpusvm_range_evict(gpusvm, range);
 *
 *		driver_unbind_range(range);
 *		drm_gpusvm_range_remove(gpusvm, range);
 *	}
 *
 *	void driver_garbage_collector(struct drm_gpusvm *gpusvm)
 *	{
 *		assert_driver_svm_locked(gpusvm);
 *
 *		for_each_range_in_garbage_collector(gpusvm, range)
 *			__driver_garbage_collector(gpusvm, range);
 *	}
 *
 * 3) Notifier callback
 *
 * .. code-block:: c
 *
 *	void driver_invalidation(struct drm_gpusvm *gpusvm,
 *				 struct drm_gpusvm_notifier *notifier,
 *				 const struct mmu_notifier_range *mmu_range)
 *	{
 *		struct drm_gpusvm_ctx ctx = { .in_notifier = true, };
 *		struct drm_gpusvm_range *range = NULL;
 *
 *		driver_invalidate_device_pages(gpusvm, mmu_range->start, mmu_range->end);
 *
 *		drm_gpusvm_for_each_range(range, notifier, mmu_range->start,
 *					  mmu_range->end) {
 *			drm_gpusvm_range_unmap_pages(gpusvm, range, &ctx);
 *
 *			if (mmu_range->event != MMU_NOTIFY_UNMAP)
 *				continue;
 *
 *			drm_gpusvm_range_set_unmapped(range, mmu_range);
 *			driver_garbage_collector_add(gpusvm, range);
 *		}
 *	}
 */

/**
 * npages_in_range() - Calculate the number of pages in a given range
 * @start: The start address of the range
 * @end: The end address of the range
 *
 * This macro calculates the number of pages in a given memory range,
 * specified by the start and end addresses. It divides the difference
 * between the end and start addresses by the page size (PAGE_SIZE) to
 * determine the number of pages in the range.
 *
 * Return: The number of pages in the specified range.
 */
static unsigned long
npages_in_range(unsigned long start, unsigned long end)
{
	return (end - start) >> PAGE_SHIFT;
}

/**
 * drm_gpusvm_notifier_find() - Find GPU SVM notifier from GPU SVM
 * @gpusvm: Pointer to the GPU SVM structure.
 * @start: Start address of the notifier
 * @end: End address of the notifier
 *
 * Return: A pointer to the drm_gpusvm_notifier if found or NULL
 */
struct drm_gpusvm_notifier *
drm_gpusvm_notifier_find(struct drm_gpusvm *gpusvm, unsigned long start,
			 unsigned long end)
{
	struct interval_tree_node *itree;

	itree = interval_tree_iter_first(&gpusvm->root, start, end - 1);

	if (itree)
		return container_of(itree, struct drm_gpusvm_notifier, itree);
	else
		return NULL;
}
EXPORT_SYMBOL_GPL(drm_gpusvm_notifier_find);

/**
 * drm_gpusvm_range_find() - Find GPU SVM range from GPU SVM notifier
 * @notifier: Pointer to the GPU SVM notifier structure.
 * @start: Start address of the range
 * @end: End address of the range
 *
 * Return: A pointer to the drm_gpusvm_range if found or NULL
 */
struct drm_gpusvm_range *
drm_gpusvm_range_find(struct drm_gpusvm_notifier *notifier, unsigned long start,
		      unsigned long end)
{
	struct interval_tree_node *itree;

	itree = interval_tree_iter_first(&notifier->root, start, end - 1);

	if (itree)
		return container_of(itree, struct drm_gpusvm_range, itree);
	else
		return NULL;
}
EXPORT_SYMBOL_GPL(drm_gpusvm_range_find);

/**
 * drm_gpusvm_notifier_invalidate() - Invalidate a GPU SVM notifier.
 * @mni: Pointer to the mmu_interval_notifier structure.
 * @mmu_range: Pointer to the mmu_notifier_range structure.
 * @cur_seq: Current sequence number.
 *
 * This function serves as a generic MMU notifier for GPU SVM. It sets the MMU
 * notifier sequence number and calls the driver invalidate vfunc under
 * gpusvm->notifier_lock.
 *
 * Return: true if the operation succeeds, false otherwise.
 */
static bool
drm_gpusvm_notifier_invalidate(struct mmu_interval_notifier *mni,
			       const struct mmu_notifier_range *mmu_range,
			       unsigned long cur_seq)
{
	struct drm_gpusvm_notifier *notifier =
		container_of(mni, typeof(*notifier), notifier);
	struct drm_gpusvm *gpusvm = notifier->gpusvm;

	if (!mmu_notifier_range_blockable(mmu_range))
		return false;

	down_write(&gpusvm->notifier_lock);
	mmu_interval_set_seq(mni, cur_seq);
	gpusvm->ops->invalidate(gpusvm, notifier, mmu_range);
	up_write(&gpusvm->notifier_lock);

	return true;
}

/*
 * drm_gpusvm_notifier_ops - MMU interval notifier operations for GPU SVM
 */
static const struct mmu_interval_notifier_ops drm_gpusvm_notifier_ops = {
	.invalidate = drm_gpusvm_notifier_invalidate,
};

/**
 * drm_gpusvm_init() - Initialize the GPU SVM.
 * @gpusvm: Pointer to the GPU SVM structure.
 * @name: Name of the GPU SVM.
 * @drm: Pointer to the DRM device structure.
 * @mm: Pointer to the mm_struct for the address space.
 * @mm_start: Start address of GPU SVM.
 * @mm_range: Range of the GPU SVM.
 * @notifier_size: Size of individual notifiers.
 * @ops: Pointer to the operations structure for GPU SVM.
 * @chunk_sizes: Pointer to the array of chunk sizes used in range allocation.
 *               Entries should be powers of 2 in descending order with last
 *               entry being SZ_4K.
 * @num_chunks: Number of chunks.
 *
 * This function initializes the GPU SVM.
 *
 * Note: If only using the simple drm_gpusvm_pages API (get/unmap/free),
 * then only @gpusvm, @name, and @drm are expected. However, the same base
 * @gpusvm can also be used with both modes together in which case the full
 * setup is needed, where the core drm_gpusvm_pages API will simply never use
 * the other fields.
 *
 * Return: 0 on success, a negative error code on failure.
 */
int drm_gpusvm_init(struct drm_gpusvm *gpusvm,
		    const char *name, struct drm_device *drm,
		    struct mm_struct *mm,
		    unsigned long mm_start, unsigned long mm_range,
		    unsigned long notifier_size,
		    const struct drm_gpusvm_ops *ops,
		    const unsigned long *chunk_sizes, int num_chunks)
{
	if (mm) {
		if (!ops->invalidate || !num_chunks)
			return -EINVAL;
		mmgrab(mm);
	} else {
		/* No full SVM mode, only core drm_gpusvm_pages API. */
		if (ops || num_chunks || mm_range || notifier_size)
			return -EINVAL;
	}

	gpusvm->name = name;
	gpusvm->drm = drm;
	gpusvm->mm = mm;
	gpusvm->mm_start = mm_start;
	gpusvm->mm_range = mm_range;
	gpusvm->notifier_size = notifier_size;
	gpusvm->ops = ops;
	gpusvm->chunk_sizes = chunk_sizes;
	gpusvm->num_chunks = num_chunks;

	gpusvm->root = RB_ROOT_CACHED;
	INIT_LIST_HEAD(&gpusvm->notifier_list);

	init_rwsem(&gpusvm->notifier_lock);

	fs_reclaim_acquire(GFP_KERNEL);
	might_lock(&gpusvm->notifier_lock);
	fs_reclaim_release(GFP_KERNEL);

#ifdef CONFIG_LOCKDEP
	gpusvm->lock_dep_map = NULL;
#endif

	return 0;
}
EXPORT_SYMBOL_GPL(drm_gpusvm_init);

/**
 * to_drm_gpusvm_notifier() - retrieve the container struct for a given rbtree node
 * @node: a pointer to the rbtree node embedded within a drm_gpusvm_notifier struct
 *
 * Return: A pointer to the containing drm_gpusvm_notifier structure.
 */
static struct drm_gpusvm_notifier *to_drm_gpusvm_notifier(struct rb_node *node)
{
	return container_of(node, struct drm_gpusvm_notifier, itree.rb);
}

/**
 * drm_gpusvm_notifier_insert() - Insert GPU SVM notifier
 * @gpusvm: Pointer to the GPU SVM structure
 * @notifier: Pointer to the GPU SVM notifier structure
 *
 * This function inserts the GPU SVM notifier into the GPU SVM RB tree and list.
 */
static void drm_gpusvm_notifier_insert(struct drm_gpusvm *gpusvm,
				       struct drm_gpusvm_notifier *notifier)
{
	struct rb_node *node;
	struct list_head *head;

	interval_tree_insert(&notifier->itree, &gpusvm->root);

	node = rb_prev(&notifier->itree.rb);
	if (node)
		head = &(to_drm_gpusvm_notifier(node))->entry;
	else
		head = &gpusvm->notifier_list;

	list_add(&notifier->entry, head);
}

/**
 * drm_gpusvm_notifier_remove() - Remove GPU SVM notifier
 * @gpusvm: Pointer to the GPU SVM tructure
 * @notifier: Pointer to the GPU SVM notifier structure
 *
 * This function removes the GPU SVM notifier from the GPU SVM RB tree and list.
 */
static void drm_gpusvm_notifier_remove(struct drm_gpusvm *gpusvm,
				       struct drm_gpusvm_notifier *notifier)
{
	interval_tree_remove(&notifier->itree, &gpusvm->root);
	list_del(&notifier->entry);
}

/**
 * drm_gpusvm_fini() - Finalize the GPU SVM.
 * @gpusvm: Pointer to the GPU SVM structure.
 *
 * This function finalizes the GPU SVM by cleaning up any remaining ranges and
 * notifiers, and dropping a reference to struct MM.
 */
void drm_gpusvm_fini(struct drm_gpusvm *gpusvm)
{
	struct drm_gpusvm_notifier *notifier, *next;

	drm_gpusvm_for_each_notifier_safe(notifier, next, gpusvm, 0, LONG_MAX) {
		struct drm_gpusvm_range *range, *__next;

		/*
		 * Remove notifier first to avoid racing with any invalidation
		 */
		mmu_interval_notifier_remove(&notifier->notifier);
		notifier->flags.removed = true;

		drm_gpusvm_for_each_range_safe(range, __next, notifier, 0,
					       LONG_MAX)
			drm_gpusvm_range_remove(gpusvm, range);
	}

	if (gpusvm->mm)
		mmdrop(gpusvm->mm);
	WARN_ON(!RB_EMPTY_ROOT(&gpusvm->root.rb_root));
}
EXPORT_SYMBOL_GPL(drm_gpusvm_fini);

/**
 * drm_gpusvm_notifier_alloc() - Allocate GPU SVM notifier
 * @gpusvm: Pointer to the GPU SVM structure
 * @fault_addr: Fault address
 *
 * This function allocates and initializes the GPU SVM notifier structure.
 *
 * Return: Pointer to the allocated GPU SVM notifier on success, ERR_PTR() on failure.
 */
static struct drm_gpusvm_notifier *
drm_gpusvm_notifier_alloc(struct drm_gpusvm *gpusvm, unsigned long fault_addr)
{
	struct drm_gpusvm_notifier *notifier;

	if (gpusvm->ops->notifier_alloc)
		notifier = gpusvm->ops->notifier_alloc();
	else
		notifier = kzalloc(sizeof(*notifier), GFP_KERNEL);

	if (!notifier)
		return ERR_PTR(-ENOMEM);

	notifier->gpusvm = gpusvm;
	notifier->itree.start = ALIGN_DOWN(fault_addr, gpusvm->notifier_size);
	notifier->itree.last = ALIGN(fault_addr + 1, gpusvm->notifier_size) - 1;
	INIT_LIST_HEAD(&notifier->entry);
	notifier->root = RB_ROOT_CACHED;
	INIT_LIST_HEAD(&notifier->range_list);

	return notifier;
}

/**
 * drm_gpusvm_notifier_free() - Free GPU SVM notifier
 * @gpusvm: Pointer to the GPU SVM structure
 * @notifier: Pointer to the GPU SVM notifier structure
 *
 * This function frees the GPU SVM notifier structure.
 */
static void drm_gpusvm_notifier_free(struct drm_gpusvm *gpusvm,
				     struct drm_gpusvm_notifier *notifier)
{
	WARN_ON(!RB_EMPTY_ROOT(&notifier->root.rb_root));

	if (gpusvm->ops->notifier_free)
		gpusvm->ops->notifier_free(notifier);
	else
		kfree(notifier);
}

/**
 * to_drm_gpusvm_range() - retrieve the container struct for a given rbtree node
 * @node: a pointer to the rbtree node embedded within a drm_gpusvm_range struct
 *
 * Return: A pointer to the containing drm_gpusvm_range structure.
 */
static struct drm_gpusvm_range *to_drm_gpusvm_range(struct rb_node *node)
{
	return container_of(node, struct drm_gpusvm_range, itree.rb);
}

/**
 * drm_gpusvm_range_insert() - Insert GPU SVM range
 * @notifier: Pointer to the GPU SVM notifier structure
 * @range: Pointer to the GPU SVM range structure
 *
 * This function inserts the GPU SVM range into the notifier RB tree and list.
 */
static void drm_gpusvm_range_insert(struct drm_gpusvm_notifier *notifier,
				    struct drm_gpusvm_range *range)
{
	struct rb_node *node;
	struct list_head *head;

	drm_gpusvm_notifier_lock(notifier->gpusvm);
	interval_tree_insert(&range->itree, &notifier->root);

	node = rb_prev(&range->itree.rb);
	if (node)
		head = &(to_drm_gpusvm_range(node))->entry;
	else
		head = &notifier->range_list;

	list_add(&range->entry, head);
	drm_gpusvm_notifier_unlock(notifier->gpusvm);
}

/**
 * __drm_gpusvm_range_remove() - Remove GPU SVM range
 * @notifier: Pointer to the GPU SVM notifier structure
 * @range: Pointer to the GPU SVM range structure
 *
 * This macro removes the GPU SVM range from the notifier RB tree and list.
 */
static void __drm_gpusvm_range_remove(struct drm_gpusvm_notifier *notifier,
				      struct drm_gpusvm_range *range)
{
	interval_tree_remove(&range->itree, &notifier->root);
	list_del(&range->entry);
}

/**
 * drm_gpusvm_range_alloc() - Allocate GPU SVM range
 * @gpusvm: Pointer to the GPU SVM structure
 * @notifier: Pointer to the GPU SVM notifier structure
 * @fault_addr: Fault address
 * @chunk_size: Chunk size
 * @migrate_devmem: Flag indicating whether to migrate device memory
 *
 * This function allocates and initializes the GPU SVM range structure.
 *
 * Return: Pointer to the allocated GPU SVM range on success, ERR_PTR() on failure.
 */
static struct drm_gpusvm_range *
drm_gpusvm_range_alloc(struct drm_gpusvm *gpusvm,
		       struct drm_gpusvm_notifier *notifier,
		       unsigned long fault_addr, unsigned long chunk_size,
		       bool migrate_devmem)
{
	struct drm_gpusvm_range *range;

	if (gpusvm->ops->range_alloc)
		range = gpusvm->ops->range_alloc(gpusvm);
	else
		range = kzalloc(sizeof(*range), GFP_KERNEL);

	if (!range)
		return ERR_PTR(-ENOMEM);

	kref_init(&range->refcount);
	range->gpusvm = gpusvm;
	range->notifier = notifier;
	range->itree.start = ALIGN_DOWN(fault_addr, chunk_size);
	range->itree.last = ALIGN(fault_addr + 1, chunk_size) - 1;
	INIT_LIST_HEAD(&range->entry);
	range->pages.notifier_seq = LONG_MAX;
	range->pages.flags.migrate_devmem = migrate_devmem ? 1 : 0;

	return range;
}

/**
 * drm_gpusvm_hmm_pfn_to_order() - Get the largest CPU mapping order.
 * @hmm_pfn: The current hmm_pfn.
 * @hmm_pfn_index: Index of the @hmm_pfn within the pfn array.
 * @npages: Number of pages within the pfn array i.e the hmm range size.
 *
 * To allow skipping PFNs with the same flags (like when they belong to
 * the same huge PTE) when looping over the pfn array, take a given a hmm_pfn,
 * and return the largest order that will fit inside the CPU PTE, but also
 * crucially accounting for the original hmm range boundaries.
 *
 * Return: The largest order that will safely fit within the size of the hmm_pfn
 * CPU PTE.
 */
static unsigned int drm_gpusvm_hmm_pfn_to_order(unsigned long hmm_pfn,
						unsigned long hmm_pfn_index,
						unsigned long npages)
{
	unsigned long size;

	size = 1UL << hmm_pfn_to_map_order(hmm_pfn);
	size -= (hmm_pfn & ~HMM_PFN_FLAGS) & (size - 1);
	hmm_pfn_index += size;
	if (hmm_pfn_index > npages)
		size -= (hmm_pfn_index - npages);

	return ilog2(size);
}

/**
 * drm_gpusvm_check_pages() - Check pages
 * @gpusvm: Pointer to the GPU SVM structure
 * @notifier: Pointer to the GPU SVM notifier structure
 * @start: Start address
 * @end: End address
 * @dev_private_owner: The device private page owner
 *
 * Check if pages between start and end have been faulted in on the CPU. Use to
 * prevent migration of pages without CPU backing store.
 *
 * Return: True if pages have been faulted into CPU, False otherwise
 */
static bool drm_gpusvm_check_pages(struct drm_gpusvm *gpusvm,
				   struct drm_gpusvm_notifier *notifier,
				   unsigned long start, unsigned long end,
				   void *dev_private_owner)
{
	struct hmm_range hmm_range = {
		.default_flags = 0,
		.notifier = &notifier->notifier,
		.start = start,
		.end = end,
		.dev_private_owner = dev_private_owner,
	};
	unsigned long timeout =
		jiffies + msecs_to_jiffies(HMM_RANGE_DEFAULT_TIMEOUT);
	unsigned long *pfns;
	unsigned long npages = npages_in_range(start, end);
	int err, i;

	mmap_assert_locked(gpusvm->mm);

	pfns = kvmalloc_array(npages, sizeof(*pfns), GFP_KERNEL);
	if (!pfns)
		return false;

	hmm_range.notifier_seq = mmu_interval_read_begin(&notifier->notifier);
	hmm_range.hmm_pfns = pfns;

	while (true) {
		err = hmm_range_fault(&hmm_range);
		if (err == -EBUSY) {
			if (time_after(jiffies, timeout))
				break;

			hmm_range.notifier_seq =
				mmu_interval_read_begin(&notifier->notifier);
			continue;
		}
		break;
	}
	if (err)
		goto err_free;

	for (i = 0; i < npages;) {
		if (!(pfns[i] & HMM_PFN_VALID)) {
			err = -EFAULT;
			goto err_free;
		}
		i += 0x1 << drm_gpusvm_hmm_pfn_to_order(pfns[i], i, npages);
	}

err_free:
	kvfree(pfns);
	return err ? false : true;
}

/**
 * drm_gpusvm_range_chunk_size() - Determine chunk size for GPU SVM range
 * @gpusvm: Pointer to the GPU SVM structure
 * @notifier: Pointer to the GPU SVM notifier structure
 * @vas: Pointer to the virtual memory area structure
 * @fault_addr: Fault address
 * @gpuva_start: Start address of GPUVA which mirrors CPU
 * @gpuva_end: End address of GPUVA which mirrors CPU
 * @check_pages_threshold: Check CPU pages for present threshold
 * @dev_private_owner: The device private page owner
 *
 * This function determines the chunk size for the GPU SVM range based on the
 * fault address, GPU SVM chunk sizes, existing GPU SVM ranges, and the virtual
 * memory area boundaries.
 *
 * Return: Chunk size on success, LONG_MAX on failure.
 */
static unsigned long
drm_gpusvm_range_chunk_size(struct drm_gpusvm *gpusvm,
			    struct drm_gpusvm_notifier *notifier,
			    struct vm_area_struct *vas,
			    unsigned long fault_addr,
			    unsigned long gpuva_start,
			    unsigned long gpuva_end,
			    unsigned long check_pages_threshold,
			    void *dev_private_owner)
{
	unsigned long start, end;
	int i = 0;

retry:
	for (; i < gpusvm->num_chunks; ++i) {
		start = ALIGN_DOWN(fault_addr, gpusvm->chunk_sizes[i]);
		end = ALIGN(fault_addr + 1, gpusvm->chunk_sizes[i]);

		if (start >= vas->vm_start && end <= vas->vm_end &&
		    start >= drm_gpusvm_notifier_start(notifier) &&
		    end <= drm_gpusvm_notifier_end(notifier) &&
		    start >= gpuva_start && end <= gpuva_end)
			break;
	}

	if (i == gpusvm->num_chunks)
		return LONG_MAX;

	/*
	 * If allocation more than page, ensure not to overlap with existing
	 * ranges.
	 */
	if (end - start != SZ_4K) {
		struct drm_gpusvm_range *range;

		range = drm_gpusvm_range_find(notifier, start, end);
		if (range) {
			++i;
			goto retry;
		}

		/*
		 * XXX: Only create range on pages CPU has faulted in. Without
		 * this check, or prefault, on BMG 'xe_exec_system_allocator --r
		 * process-many-malloc' fails. In the failure case, each process
		 * mallocs 16k but the CPU VMA is ~128k which results in 64k SVM
		 * ranges. When migrating the SVM ranges, some processes fail in
		 * drm_pagemap_migrate_to_devmem with 'migrate.cpages != npages'
		 * and then upon drm_gpusvm_range_get_pages device pages from
		 * other processes are collected + faulted in which creates all
		 * sorts of problems. Unsure exactly how this happening, also
		 * problem goes away if 'xe_exec_system_allocator --r
		 * process-many-malloc' mallocs at least 64k at a time.
		 */
		if (end - start <= check_pages_threshold &&
		    !drm_gpusvm_check_pages(gpusvm, notifier, start, end, dev_private_owner)) {
			++i;
			goto retry;
		}
	}

	return end - start;
}

#ifdef CONFIG_LOCKDEP
/**
 * drm_gpusvm_driver_lock_held() - Assert GPU SVM driver lock is held
 * @gpusvm: Pointer to the GPU SVM structure.
 *
 * Ensure driver lock is held.
 */
static void drm_gpusvm_driver_lock_held(struct drm_gpusvm *gpusvm)
{
	if ((gpusvm)->lock_dep_map)
		lockdep_assert(lock_is_held_type((gpusvm)->lock_dep_map, 0));
}
#else
static void drm_gpusvm_driver_lock_held(struct drm_gpusvm *gpusvm)
{
}
#endif

/**
 * drm_gpusvm_find_vma_start() - Find start address for first VMA in range
 * @gpusvm: Pointer to the GPU SVM structure
 * @start: The inclusive start user address.
 * @end: The exclusive end user address.
 *
 * Returns: The start address of first VMA within the provided range,
 * ULONG_MAX otherwise. Assumes start_addr < end_addr.
 */
unsigned long
drm_gpusvm_find_vma_start(struct drm_gpusvm *gpusvm,
			  unsigned long start,
			  unsigned long end)
{
	struct mm_struct *mm = gpusvm->mm;
	struct vm_area_struct *vma;
	unsigned long addr = ULONG_MAX;

	if (!mmget_not_zero(mm))
		return addr;

	mmap_read_lock(mm);

	vma = find_vma_intersection(mm, start, end);
	if (vma)
		addr =  vma->vm_start;

	mmap_read_unlock(mm);
	mmput(mm);

	return addr;
}
EXPORT_SYMBOL_GPL(drm_gpusvm_find_vma_start);

/**
 * drm_gpusvm_range_find_or_insert() - Find or insert GPU SVM range
 * @gpusvm: Pointer to the GPU SVM structure
 * @fault_addr: Fault address
 * @gpuva_start: Start address of GPUVA which mirrors CPU
 * @gpuva_end: End address of GPUVA which mirrors CPU
 * @ctx: GPU SVM context
 *
 * This function finds or inserts a newly allocated a GPU SVM range based on the
 * fault address. Caller must hold a lock to protect range lookup and insertion.
 *
 * Return: Pointer to the GPU SVM range on success, ERR_PTR() on failure.
 */
struct drm_gpusvm_range *
drm_gpusvm_range_find_or_insert(struct drm_gpusvm *gpusvm,
				unsigned long fault_addr,
				unsigned long gpuva_start,
				unsigned long gpuva_end,
				const struct drm_gpusvm_ctx *ctx)
{
	struct drm_gpusvm_notifier *notifier;
	struct drm_gpusvm_range *range;
	struct mm_struct *mm = gpusvm->mm;
	struct vm_area_struct *vas;
	bool notifier_alloc = false;
	unsigned long chunk_size;
	int err;
	bool migrate_devmem;

	drm_gpusvm_driver_lock_held(gpusvm);

	if (fault_addr < gpusvm->mm_start ||
	    fault_addr > gpusvm->mm_start + gpusvm->mm_range)
		return ERR_PTR(-EINVAL);

	if (!mmget_not_zero(mm))
		return ERR_PTR(-EFAULT);

	notifier = drm_gpusvm_notifier_find(gpusvm, fault_addr, fault_addr + 1);
	if (!notifier) {
		notifier = drm_gpusvm_notifier_alloc(gpusvm, fault_addr);
		if (IS_ERR(notifier)) {
			err = PTR_ERR(notifier);
			goto err_mmunlock;
		}
		notifier_alloc = true;
		err = mmu_interval_notifier_insert(&notifier->notifier,
						   mm,
						   drm_gpusvm_notifier_start(notifier),
						   drm_gpusvm_notifier_size(notifier),
						   &drm_gpusvm_notifier_ops);
		if (err)
			goto err_notifier;
	}

	mmap_read_lock(mm);

	vas = vma_lookup(mm, fault_addr);
	if (!vas) {
		err = -ENOENT;
		goto err_notifier_remove;
	}

	if (!ctx->read_only && !(vas->vm_flags & VM_WRITE)) {
		err = -EPERM;
		goto err_notifier_remove;
	}

	range = drm_gpusvm_range_find(notifier, fault_addr, fault_addr + 1);
	if (range)
		goto out_mmunlock;
	/*
	 * XXX: Short-circuiting migration based on migrate_vma_* current
	 * limitations. If/when migrate_vma_* add more support, this logic will
	 * have to change.
	 */
	migrate_devmem = ctx->devmem_possible &&
		vma_is_anonymous(vas) && !is_vm_hugetlb_page(vas);

	chunk_size = drm_gpusvm_range_chunk_size(gpusvm, notifier, vas,
						 fault_addr, gpuva_start,
						 gpuva_end,
						 ctx->check_pages_threshold,
						 ctx->device_private_page_owner);
	if (chunk_size == LONG_MAX) {
		err = -EINVAL;
		goto err_notifier_remove;
	}

	range = drm_gpusvm_range_alloc(gpusvm, notifier, fault_addr, chunk_size,
				       migrate_devmem);
	if (IS_ERR(range)) {
		err = PTR_ERR(range);
		goto err_notifier_remove;
	}

	drm_gpusvm_range_insert(notifier, range);
	if (notifier_alloc)
		drm_gpusvm_notifier_insert(gpusvm, notifier);

out_mmunlock:
	mmap_read_unlock(mm);
	mmput(mm);

	return range;

err_notifier_remove:
	mmap_read_unlock(mm);
	if (notifier_alloc)
		mmu_interval_notifier_remove(&notifier->notifier);
err_notifier:
	if (notifier_alloc)
		drm_gpusvm_notifier_free(gpusvm, notifier);
err_mmunlock:
	mmput(mm);
	return ERR_PTR(err);
}
EXPORT_SYMBOL_GPL(drm_gpusvm_range_find_or_insert);

/**
 * __drm_gpusvm_unmap_pages() - Unmap pages associated with GPU SVM pages (internal)
 * @gpusvm: Pointer to the GPU SVM structure
 * @svm_pages: Pointer to the GPU SVM pages structure
 * @npages: Number of pages to unmap
 *
 * This function unmap pages associated with a GPU SVM pages struct. Assumes and
 * asserts correct locking is in place when called.
 */
static void __drm_gpusvm_unmap_pages(struct drm_gpusvm *gpusvm,
				     struct drm_gpusvm_pages *svm_pages,
				     unsigned long npages)
{
	struct drm_pagemap *dpagemap = svm_pages->dpagemap;
	struct device *dev = gpusvm->drm->dev;
	unsigned long i, j;

	lockdep_assert_held(&gpusvm->notifier_lock);

	if (svm_pages->flags.has_dma_mapping) {
		struct drm_gpusvm_pages_flags flags = {
			.__flags = svm_pages->flags.__flags,
		};

		for (i = 0, j = 0; i < npages; j++) {
			struct drm_pagemap_addr *addr = &svm_pages->dma_addr[j];

			if (addr->proto == DRM_INTERCONNECT_SYSTEM)
				dma_unmap_page(dev,
					       addr->addr,
					       PAGE_SIZE << addr->order,
					       addr->dir);
			else if (dpagemap && dpagemap->ops->device_unmap)
				dpagemap->ops->device_unmap(dpagemap,
							    dev, *addr);
			i += 1 << addr->order;
		}

		/* WRITE_ONCE pairs with READ_ONCE for opportunistic checks */
		flags.has_devmem_pages = false;
		flags.has_dma_mapping = false;
		WRITE_ONCE(svm_pages->flags.__flags, flags.__flags);

		svm_pages->dpagemap = NULL;
	}
}

/**
 * __drm_gpusvm_free_pages() - Free dma array associated with GPU SVM pages
 * @gpusvm: Pointer to the GPU SVM structure
 * @svm_pages: Pointer to the GPU SVM pages structure
 *
 * This function frees the dma address array associated with a GPU SVM range.
 */
static void __drm_gpusvm_free_pages(struct drm_gpusvm *gpusvm,
				    struct drm_gpusvm_pages *svm_pages)
{
	lockdep_assert_held(&gpusvm->notifier_lock);

	if (svm_pages->dma_addr) {
		kvfree(svm_pages->dma_addr);
		svm_pages->dma_addr = NULL;
	}
}

/**
 * drm_gpusvm_free_pages() - Free dma-mapping associated with GPU SVM pages
 * struct
 * @gpusvm: Pointer to the GPU SVM structure
 * @svm_pages: Pointer to the GPU SVM pages structure
 * @npages: Number of mapped pages
 *
 * This function unmaps and frees the dma address array associated with a GPU
 * SVM pages struct.
 */
void drm_gpusvm_free_pages(struct drm_gpusvm *gpusvm,
			   struct drm_gpusvm_pages *svm_pages,
			   unsigned long npages)
{
	drm_gpusvm_notifier_lock(gpusvm);
	__drm_gpusvm_unmap_pages(gpusvm, svm_pages, npages);
	__drm_gpusvm_free_pages(gpusvm, svm_pages);
	drm_gpusvm_notifier_unlock(gpusvm);
}
EXPORT_SYMBOL_GPL(drm_gpusvm_free_pages);

/**
 * drm_gpusvm_range_remove() - Remove GPU SVM range
 * @gpusvm: Pointer to the GPU SVM structure
 * @range: Pointer to the GPU SVM range to be removed
 *
 * This function removes the specified GPU SVM range and also removes the parent
 * GPU SVM notifier if no more ranges remain in the notifier. The caller must
 * hold a lock to protect range and notifier removal.
 */
void drm_gpusvm_range_remove(struct drm_gpusvm *gpusvm,
			     struct drm_gpusvm_range *range)
{
	unsigned long npages = npages_in_range(drm_gpusvm_range_start(range),
					       drm_gpusvm_range_end(range));
	struct drm_gpusvm_notifier *notifier;

	drm_gpusvm_driver_lock_held(gpusvm);

	notifier = drm_gpusvm_notifier_find(gpusvm,
					    drm_gpusvm_range_start(range),
					    drm_gpusvm_range_start(range) + 1);
	if (WARN_ON_ONCE(!notifier))
		return;

	drm_gpusvm_notifier_lock(gpusvm);
	__drm_gpusvm_unmap_pages(gpusvm, &range->pages, npages);
	__drm_gpusvm_free_pages(gpusvm, &range->pages);
	__drm_gpusvm_range_remove(notifier, range);
	drm_gpusvm_notifier_unlock(gpusvm);

	drm_gpusvm_range_put(range);

	if (RB_EMPTY_ROOT(&notifier->root.rb_root)) {
		if (!notifier->flags.removed)
			mmu_interval_notifier_remove(&notifier->notifier);
		drm_gpusvm_notifier_remove(gpusvm, notifier);
		drm_gpusvm_notifier_free(gpusvm, notifier);
	}
}
EXPORT_SYMBOL_GPL(drm_gpusvm_range_remove);

/**
 * drm_gpusvm_range_get() - Get a reference to GPU SVM range
 * @range: Pointer to the GPU SVM range
 *
 * This function increments the reference count of the specified GPU SVM range.
 *
 * Return: Pointer to the GPU SVM range.
 */
struct drm_gpusvm_range *
drm_gpusvm_range_get(struct drm_gpusvm_range *range)
{
	kref_get(&range->refcount);

	return range;
}
EXPORT_SYMBOL_GPL(drm_gpusvm_range_get);

/**
 * drm_gpusvm_range_destroy() - Destroy GPU SVM range
 * @refcount: Pointer to the reference counter embedded in the GPU SVM range
 *
 * This function destroys the specified GPU SVM range when its reference count
 * reaches zero. If a custom range-free function is provided, it is invoked to
 * free the range; otherwise, the range is deallocated using kfree().
 */
static void drm_gpusvm_range_destroy(struct kref *refcount)
{
	struct drm_gpusvm_range *range =
		container_of(refcount, struct drm_gpusvm_range, refcount);
	struct drm_gpusvm *gpusvm = range->gpusvm;

	if (gpusvm->ops->range_free)
		gpusvm->ops->range_free(range);
	else
		kfree(range);
}

/**
 * drm_gpusvm_range_put() - Put a reference to GPU SVM range
 * @range: Pointer to the GPU SVM range
 *
 * This function decrements the reference count of the specified GPU SVM range
 * and frees it when the count reaches zero.
 */
void drm_gpusvm_range_put(struct drm_gpusvm_range *range)
{
	kref_put(&range->refcount, drm_gpusvm_range_destroy);
}
EXPORT_SYMBOL_GPL(drm_gpusvm_range_put);

/**
 * drm_gpusvm_pages_valid() - GPU SVM range pages valid
 * @gpusvm: Pointer to the GPU SVM structure
 * @svm_pages: Pointer to the GPU SVM pages structure
 *
 * This function determines if a GPU SVM range pages are valid. Expected be
 * called holding gpusvm->notifier_lock and as the last step before committing a
 * GPU binding. This is akin to a notifier seqno check in the HMM documentation
 * but due to wider notifiers (i.e., notifiers which span multiple ranges) this
 * function is required for finer grained checking (i.e., per range) if pages
 * are valid.
 *
 * Return: True if GPU SVM range has valid pages, False otherwise
 */
static bool drm_gpusvm_pages_valid(struct drm_gpusvm *gpusvm,
				   struct drm_gpusvm_pages *svm_pages)
{
	lockdep_assert_held(&gpusvm->notifier_lock);

	return svm_pages->flags.has_devmem_pages || svm_pages->flags.has_dma_mapping;
}

/**
 * drm_gpusvm_range_pages_valid() - GPU SVM range pages valid
 * @gpusvm: Pointer to the GPU SVM structure
 * @range: Pointer to the GPU SVM range structure
 *
 * This function determines if a GPU SVM range pages are valid. Expected be
 * called holding gpusvm->notifier_lock and as the last step before committing a
 * GPU binding. This is akin to a notifier seqno check in the HMM documentation
 * but due to wider notifiers (i.e., notifiers which span multiple ranges) this
 * function is required for finer grained checking (i.e., per range) if pages
 * are valid.
 *
 * Return: True if GPU SVM range has valid pages, False otherwise
 */
bool drm_gpusvm_range_pages_valid(struct drm_gpusvm *gpusvm,
				  struct drm_gpusvm_range *range)
{
	return drm_gpusvm_pages_valid(gpusvm, &range->pages);
}
EXPORT_SYMBOL_GPL(drm_gpusvm_range_pages_valid);

/**
 * drm_gpusvm_range_pages_valid_unlocked() - GPU SVM range pages valid unlocked
 * @gpusvm: Pointer to the GPU SVM structure
 * @range: Pointer to the GPU SVM range structure
 *
 * This function determines if a GPU SVM range pages are valid. Expected be
 * called without holding gpusvm->notifier_lock.
 *
 * Return: True if GPU SVM range has valid pages, False otherwise
 */
static bool drm_gpusvm_pages_valid_unlocked(struct drm_gpusvm *gpusvm,
					    struct drm_gpusvm_pages *svm_pages)
{
	bool pages_valid;

	if (!svm_pages->dma_addr)
		return false;

	drm_gpusvm_notifier_lock(gpusvm);
	pages_valid = drm_gpusvm_pages_valid(gpusvm, svm_pages);
	if (!pages_valid)
		__drm_gpusvm_free_pages(gpusvm, svm_pages);
	drm_gpusvm_notifier_unlock(gpusvm);

	return pages_valid;
}

/**
 * drm_gpusvm_get_pages() - Get pages and populate GPU SVM pages struct
 * @gpusvm: Pointer to the GPU SVM structure
 * @svm_pages: The SVM pages to populate. This will contain the dma-addresses
 * @mm: The mm corresponding to the CPU range
 * @notifier: The corresponding notifier for the given CPU range
 * @pages_start: Start CPU address for the pages
 * @pages_end: End CPU address for the pages (exclusive)
 * @ctx: GPU SVM context
 *
 * This function gets and maps pages for CPU range and ensures they are
 * mapped for DMA access.
 *
 * Return: 0 on success, negative error code on failure.
 */
int drm_gpusvm_get_pages(struct drm_gpusvm *gpusvm,
			 struct drm_gpusvm_pages *svm_pages,
			 struct mm_struct *mm,
			 struct mmu_interval_notifier *notifier,
			 unsigned long pages_start, unsigned long pages_end,
			 const struct drm_gpusvm_ctx *ctx)
{
	struct hmm_range hmm_range = {
		.default_flags = HMM_PFN_REQ_FAULT | (ctx->read_only ? 0 :
			HMM_PFN_REQ_WRITE),
		.notifier = notifier,
		.start = pages_start,
		.end = pages_end,
		.dev_private_owner = ctx->device_private_page_owner,
	};
	void *zdd;
	unsigned long timeout =
		jiffies + msecs_to_jiffies(HMM_RANGE_DEFAULT_TIMEOUT);
	unsigned long i, j;
	unsigned long npages = npages_in_range(pages_start, pages_end);
	unsigned long num_dma_mapped;
	unsigned int order = 0;
	unsigned long *pfns;
	int err = 0;
	struct dev_pagemap *pagemap;
	struct drm_pagemap *dpagemap;
	struct drm_gpusvm_pages_flags flags;
	enum dma_data_direction dma_dir = ctx->read_only ? DMA_TO_DEVICE :
							   DMA_BIDIRECTIONAL;

retry:
	hmm_range.notifier_seq = mmu_interval_read_begin(notifier);
	if (drm_gpusvm_pages_valid_unlocked(gpusvm, svm_pages))
		goto set_seqno;

	pfns = kvmalloc_array(npages, sizeof(*pfns), GFP_KERNEL);
	if (!pfns)
		return -ENOMEM;

	if (!mmget_not_zero(mm)) {
		err = -EFAULT;
		goto err_free;
	}

	hmm_range.hmm_pfns = pfns;
	while (true) {
		mmap_read_lock(mm);
		err = hmm_range_fault(&hmm_range);
		mmap_read_unlock(mm);

		if (err == -EBUSY) {
			if (time_after(jiffies, timeout))
				break;

			hmm_range.notifier_seq =
				mmu_interval_read_begin(notifier);
			continue;
		}
		break;
	}
	mmput(mm);
	if (err)
		goto err_free;

map_pages:
	/*
	 * Perform all dma mappings under the notifier lock to not
	 * access freed pages. A notifier will either block on
	 * the notifier lock or unmap dma.
	 */
	drm_gpusvm_notifier_lock(gpusvm);

	flags.__flags = svm_pages->flags.__flags;
	if (flags.unmapped) {
		drm_gpusvm_notifier_unlock(gpusvm);
		err = -EFAULT;
		goto err_free;
	}

	if (mmu_interval_read_retry(notifier, hmm_range.notifier_seq)) {
		drm_gpusvm_notifier_unlock(gpusvm);
		kvfree(pfns);
		goto retry;
	}

	if (!svm_pages->dma_addr) {
		/* Unlock and restart mapping to allocate memory. */
		drm_gpusvm_notifier_unlock(gpusvm);
		svm_pages->dma_addr =
			kvmalloc_array(npages, sizeof(*svm_pages->dma_addr), GFP_KERNEL);
		if (!svm_pages->dma_addr) {
			err = -ENOMEM;
			goto err_free;
		}
		goto map_pages;
	}

	zdd = NULL;
	pagemap = NULL;
	num_dma_mapped = 0;
	for (i = 0, j = 0; i < npages; ++j) {
		struct page *page = hmm_pfn_to_page(pfns[i]);

		order = drm_gpusvm_hmm_pfn_to_order(pfns[i], i, npages);
		if (is_device_private_page(page) ||
		    is_device_coherent_page(page)) {
			if (zdd != page->zone_device_data && i > 0) {
				err = -EOPNOTSUPP;
				goto err_unmap;
			}
			zdd = page->zone_device_data;
			if (pagemap != page_pgmap(page)) {
				if (i > 0) {
					err = -EOPNOTSUPP;
					goto err_unmap;
				}

				pagemap = page_pgmap(page);
				dpagemap = drm_pagemap_page_to_dpagemap(page);
				if (drm_WARN_ON(gpusvm->drm, !dpagemap)) {
					/*
					 * Raced. This is not supposed to happen
					 * since hmm_range_fault() should've migrated
					 * this page to system.
					 */
					err = -EAGAIN;
					goto err_unmap;
				}
			}
			svm_pages->dma_addr[j] =
				dpagemap->ops->device_map(dpagemap,
							  gpusvm->drm->dev,
							  page, order,
							  dma_dir);
			if (dma_mapping_error(gpusvm->drm->dev,
					      svm_pages->dma_addr[j].addr)) {
				err = -EFAULT;
				goto err_unmap;
			}
		} else {
			dma_addr_t addr;

			if (is_zone_device_page(page) || pagemap) {
				err = -EOPNOTSUPP;
				goto err_unmap;
			}

			if (ctx->devmem_only) {
				err = -EFAULT;
				goto err_unmap;
			}

			addr = dma_map_page(gpusvm->drm->dev,
					    page, 0,
					    PAGE_SIZE << order,
					    dma_dir);
			if (dma_mapping_error(gpusvm->drm->dev, addr)) {
				err = -EFAULT;
				goto err_unmap;
			}

			svm_pages->dma_addr[j] = drm_pagemap_addr_encode
				(addr, DRM_INTERCONNECT_SYSTEM, order,
				 dma_dir);
		}
		i += 1 << order;
		num_dma_mapped = i;
		flags.has_dma_mapping = true;
	}

	if (pagemap) {
		flags.has_devmem_pages = true;
		svm_pages->dpagemap = dpagemap;
	}

	/* WRITE_ONCE pairs with READ_ONCE for opportunistic checks */
	WRITE_ONCE(svm_pages->flags.__flags, flags.__flags);

	drm_gpusvm_notifier_unlock(gpusvm);
	kvfree(pfns);
set_seqno:
	svm_pages->notifier_seq = hmm_range.notifier_seq;

	return 0;

err_unmap:
	__drm_gpusvm_unmap_pages(gpusvm, svm_pages, num_dma_mapped);
	drm_gpusvm_notifier_unlock(gpusvm);
err_free:
	kvfree(pfns);
	if (err == -EAGAIN)
		goto retry;
	return err;
}
EXPORT_SYMBOL_GPL(drm_gpusvm_get_pages);

/**
 * drm_gpusvm_range_get_pages() - Get pages for a GPU SVM range
 * @gpusvm: Pointer to the GPU SVM structure
 * @range: Pointer to the GPU SVM range structure
 * @ctx: GPU SVM context
 *
 * This function gets pages for a GPU SVM range and ensures they are mapped for
 * DMA access.
 *
 * Return: 0 on success, negative error code on failure.
 */
int drm_gpusvm_range_get_pages(struct drm_gpusvm *gpusvm,
			       struct drm_gpusvm_range *range,
			       const struct drm_gpusvm_ctx *ctx)
{
	return drm_gpusvm_get_pages(gpusvm, &range->pages, gpusvm->mm,
				    &range->notifier->notifier,
				    drm_gpusvm_range_start(range),
				    drm_gpusvm_range_end(range), ctx);
}
EXPORT_SYMBOL_GPL(drm_gpusvm_range_get_pages);

/**
 * drm_gpusvm_unmap_pages() - Unmap GPU svm pages
 * @gpusvm: Pointer to the GPU SVM structure
 * @svm_pages: Pointer to the GPU SVM pages structure
 * @npages: Number of pages in @svm_pages.
 * @ctx: GPU SVM context
 *
 * This function unmaps pages associated with a GPU SVM pages struct. If
 * @in_notifier is set, it is assumed that gpusvm->notifier_lock is held in
 * write mode; if it is clear, it acquires gpusvm->notifier_lock in read mode.
 * Must be called in the invalidate() callback of the corresponding notifier for
 * IOMMU security model.
 */
void drm_gpusvm_unmap_pages(struct drm_gpusvm *gpusvm,
			    struct drm_gpusvm_pages *svm_pages,
			    unsigned long npages,
			    const struct drm_gpusvm_ctx *ctx)
{
	if (ctx->in_notifier)
		lockdep_assert_held_write(&gpusvm->notifier_lock);
	else
		drm_gpusvm_notifier_lock(gpusvm);

	__drm_gpusvm_unmap_pages(gpusvm, svm_pages, npages);

	if (!ctx->in_notifier)
		drm_gpusvm_notifier_unlock(gpusvm);
}
EXPORT_SYMBOL_GPL(drm_gpusvm_unmap_pages);

/**
 * drm_gpusvm_range_unmap_pages() - Unmap pages associated with a GPU SVM range
 * @gpusvm: Pointer to the GPU SVM structure
 * @range: Pointer to the GPU SVM range structure
 * @ctx: GPU SVM context
 *
 * This function unmaps pages associated with a GPU SVM range. If @in_notifier
 * is set, it is assumed that gpusvm->notifier_lock is held in write mode; if it
 * is clear, it acquires gpusvm->notifier_lock in read mode. Must be called on
 * each GPU SVM range attached to notifier in gpusvm->ops->invalidate for IOMMU
 * security model.
 */
void drm_gpusvm_range_unmap_pages(struct drm_gpusvm *gpusvm,
				  struct drm_gpusvm_range *range,
				  const struct drm_gpusvm_ctx *ctx)
{
	unsigned long npages = npages_in_range(drm_gpusvm_range_start(range),
					       drm_gpusvm_range_end(range));

	return drm_gpusvm_unmap_pages(gpusvm, &range->pages, npages, ctx);
}
EXPORT_SYMBOL_GPL(drm_gpusvm_range_unmap_pages);

/**
 * drm_gpusvm_range_evict() - Evict GPU SVM range
 * @gpusvm: Pointer to the GPU SVM structure
 * @range: Pointer to the GPU SVM range to be removed
 *
 * This function evicts the specified GPU SVM range.
 *
 * Return: 0 on success, a negative error code on failure.
 */
int drm_gpusvm_range_evict(struct drm_gpusvm *gpusvm,
			   struct drm_gpusvm_range *range)
{
	struct mmu_interval_notifier *notifier = &range->notifier->notifier;
	struct hmm_range hmm_range = {
		.default_flags = HMM_PFN_REQ_FAULT,
		.notifier = notifier,
		.start = drm_gpusvm_range_start(range),
		.end = drm_gpusvm_range_end(range),
		.dev_private_owner = NULL,
	};
	unsigned long timeout =
		jiffies + msecs_to_jiffies(HMM_RANGE_DEFAULT_TIMEOUT);
	unsigned long *pfns;
	unsigned long npages = npages_in_range(drm_gpusvm_range_start(range),
					       drm_gpusvm_range_end(range));
	int err = 0;
	struct mm_struct *mm = gpusvm->mm;

	if (!mmget_not_zero(mm))
		return -EFAULT;

	pfns = kvmalloc_array(npages, sizeof(*pfns), GFP_KERNEL);
	if (!pfns)
		return -ENOMEM;

	hmm_range.hmm_pfns = pfns;
	while (!time_after(jiffies, timeout)) {
		hmm_range.notifier_seq = mmu_interval_read_begin(notifier);
		if (time_after(jiffies, timeout)) {
			err = -ETIME;
			break;
		}

		mmap_read_lock(mm);
		err = hmm_range_fault(&hmm_range);
		mmap_read_unlock(mm);
		if (err != -EBUSY)
			break;
	}

	kvfree(pfns);
	mmput(mm);

	return err;
}
EXPORT_SYMBOL_GPL(drm_gpusvm_range_evict);

/**
 * drm_gpusvm_has_mapping() - Check if GPU SVM has mapping for the given address range
 * @gpusvm: Pointer to the GPU SVM structure.
 * @start: Start address
 * @end: End address
 *
 * Return: True if GPU SVM has mapping, False otherwise
 */
bool drm_gpusvm_has_mapping(struct drm_gpusvm *gpusvm, unsigned long start,
			    unsigned long end)
{
	struct drm_gpusvm_notifier *notifier;

	drm_gpusvm_for_each_notifier(notifier, gpusvm, start, end) {
		struct drm_gpusvm_range *range = NULL;

		drm_gpusvm_for_each_range(range, notifier, start, end)
			return true;
	}

	return false;
}
EXPORT_SYMBOL_GPL(drm_gpusvm_has_mapping);

/**
 * drm_gpusvm_range_set_unmapped() - Mark a GPU SVM range as unmapped
 * @range: Pointer to the GPU SVM range structure.
 * @mmu_range: Pointer to the MMU notifier range structure.
 *
 * This function marks a GPU SVM range as unmapped and sets the partial_unmap flag
 * if the range partially falls within the provided MMU notifier range.
 */
void drm_gpusvm_range_set_unmapped(struct drm_gpusvm_range *range,
				   const struct mmu_notifier_range *mmu_range)
{
	lockdep_assert_held_write(&range->gpusvm->notifier_lock);

	range->pages.flags.unmapped = true;
	if (drm_gpusvm_range_start(range) < mmu_range->start ||
	    drm_gpusvm_range_end(range) > mmu_range->end)
		range->pages.flags.partial_unmap = true;
}
EXPORT_SYMBOL_GPL(drm_gpusvm_range_set_unmapped);

MODULE_DESCRIPTION("DRM GPUSVM");
MODULE_LICENSE("GPL");
