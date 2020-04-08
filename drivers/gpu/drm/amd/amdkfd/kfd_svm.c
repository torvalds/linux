// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Copyright 2020-2021 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <linux/types.h>
#include "amdgpu_sync.h"
#include "amdgpu_object.h"
#include "amdgpu_vm.h"
#include "amdgpu_mn.h"
#include "kfd_priv.h"
#include "kfd_svm.h"

static bool
svm_range_cpu_invalidate_pagetables(struct mmu_interval_notifier *mni,
				    const struct mmu_notifier_range *range,
				    unsigned long cur_seq);

static const struct mmu_interval_notifier_ops svm_range_mn_ops = {
	.invalidate = svm_range_cpu_invalidate_pagetables,
};

/**
 * svm_range_unlink - unlink svm_range from lists and interval tree
 * @prange: svm range structure to be removed
 *
 * Remove the svm range from svms interval tree and link list
 *
 * Context: The caller must hold svms->lock
 */
static void svm_range_unlink(struct svm_range *prange)
{
	pr_debug("svms 0x%p prange 0x%p [0x%lx 0x%lx]\n", prange->svms,
		 prange, prange->start, prange->last);

	list_del(&prange->list);
	if (prange->it_node.start != 0 && prange->it_node.last != 0)
		interval_tree_remove(&prange->it_node, &prange->svms->objects);
}

static void
svm_range_add_notifier_locked(struct mm_struct *mm, struct svm_range *prange)
{
	pr_debug("svms 0x%p prange 0x%p [0x%lx 0x%lx]\n", prange->svms,
		 prange, prange->start, prange->last);

	mmu_interval_notifier_insert_locked(&prange->notifier, mm,
				     prange->start << PAGE_SHIFT,
				     prange->npages << PAGE_SHIFT,
				     &svm_range_mn_ops);
}

/**
 * svm_range_add_to_svms - add svm range to svms
 * @prange: svm range structure to be added
 *
 * Add the svm range to svms interval tree and link list
 *
 * Context: The caller must hold svms->lock
 */
static void svm_range_add_to_svms(struct svm_range *prange)
{
	pr_debug("svms 0x%p prange 0x%p [0x%lx 0x%lx]\n", prange->svms,
		 prange, prange->start, prange->last);

	list_add_tail(&prange->list, &prange->svms->list);
	prange->it_node.start = prange->start;
	prange->it_node.last = prange->last;
	interval_tree_insert(&prange->it_node, &prange->svms->objects);
}

static void svm_range_remove_notifier(struct svm_range *prange)
{
	pr_debug("remove notifier svms 0x%p prange 0x%p [0x%lx 0x%lx]\n",
		 prange->svms, prange,
		 prange->notifier.interval_tree.start >> PAGE_SHIFT,
		 prange->notifier.interval_tree.last >> PAGE_SHIFT);

	if (prange->notifier.interval_tree.start != 0 &&
	    prange->notifier.interval_tree.last != 0)
		mmu_interval_notifier_remove(&prange->notifier);
}

static void svm_range_free(struct svm_range *prange)
{
	pr_debug("svms 0x%p prange 0x%p [0x%lx 0x%lx]\n", prange->svms, prange,
		 prange->start, prange->last);

	mutex_destroy(&prange->lock);
	kfree(prange);
}

static void
svm_range_set_default_attributes(int32_t *location, int32_t *prefetch_loc,
				 uint8_t *granularity, uint32_t *flags)
{
	*location = KFD_IOCTL_SVM_LOCATION_UNDEFINED;
	*prefetch_loc = KFD_IOCTL_SVM_LOCATION_UNDEFINED;
	*granularity = 9;
	*flags =
		KFD_IOCTL_SVM_FLAG_HOST_ACCESS | KFD_IOCTL_SVM_FLAG_COHERENT;
}

static struct
svm_range *svm_range_new(struct svm_range_list *svms, uint64_t start,
			 uint64_t last)
{
	uint64_t size = last - start + 1;
	struct svm_range *prange;

	prange = kzalloc(sizeof(*prange), GFP_KERNEL);
	if (!prange)
		return NULL;
	prange->npages = size;
	prange->svms = svms;
	prange->start = start;
	prange->last = last;
	INIT_LIST_HEAD(&prange->list);
	INIT_LIST_HEAD(&prange->update_list);
	INIT_LIST_HEAD(&prange->remove_list);
	INIT_LIST_HEAD(&prange->insert_list);
	INIT_LIST_HEAD(&prange->deferred_list);
	INIT_LIST_HEAD(&prange->child_list);
	mutex_init(&prange->lock);
	svm_range_set_default_attributes(&prange->preferred_loc,
					 &prange->prefetch_loc,
					 &prange->granularity, &prange->flags);

	pr_debug("svms 0x%p [0x%llx 0x%llx]\n", svms, start, last);

	return prange;
}

static int
svm_range_check_attr(struct kfd_process *p,
		     uint32_t nattr, struct kfd_ioctl_svm_attribute *attrs)
{
	uint32_t i;
	int gpuidx;

	for (i = 0; i < nattr; i++) {
		switch (attrs[i].type) {
		case KFD_IOCTL_SVM_ATTR_PREFERRED_LOC:
			if (attrs[i].value != KFD_IOCTL_SVM_LOCATION_SYSMEM &&
			    attrs[i].value != KFD_IOCTL_SVM_LOCATION_UNDEFINED &&
			    kfd_process_gpuidx_from_gpuid(p,
							  attrs[i].value) < 0) {
				pr_debug("no GPU 0x%x found\n", attrs[i].value);
				return -EINVAL;
			}
			break;
		case KFD_IOCTL_SVM_ATTR_PREFETCH_LOC:
			if (attrs[i].value != KFD_IOCTL_SVM_LOCATION_SYSMEM &&
			    kfd_process_gpuidx_from_gpuid(p,
							  attrs[i].value) < 0) {
				pr_debug("no GPU 0x%x found\n", attrs[i].value);
				return -EINVAL;
			}
			break;
		case KFD_IOCTL_SVM_ATTR_ACCESS:
		case KFD_IOCTL_SVM_ATTR_ACCESS_IN_PLACE:
		case KFD_IOCTL_SVM_ATTR_NO_ACCESS:
			gpuidx = kfd_process_gpuidx_from_gpuid(p,
							       attrs[i].value);
			if (gpuidx < 0) {
				pr_debug("no GPU 0x%x found\n", attrs[i].value);
				return -EINVAL;
			}
			break;
		case KFD_IOCTL_SVM_ATTR_SET_FLAGS:
			break;
		case KFD_IOCTL_SVM_ATTR_CLR_FLAGS:
			break;
		case KFD_IOCTL_SVM_ATTR_GRANULARITY:
			break;
		default:
			pr_debug("unknown attr type 0x%x\n", attrs[i].type);
			return -EINVAL;
		}
	}

	return 0;
}

static void
svm_range_apply_attrs(struct kfd_process *p, struct svm_range *prange,
		      uint32_t nattr, struct kfd_ioctl_svm_attribute *attrs)
{
	uint32_t i;
	int gpuidx;

	for (i = 0; i < nattr; i++) {
		switch (attrs[i].type) {
		case KFD_IOCTL_SVM_ATTR_PREFERRED_LOC:
			prange->preferred_loc = attrs[i].value;
			break;
		case KFD_IOCTL_SVM_ATTR_PREFETCH_LOC:
			prange->prefetch_loc = attrs[i].value;
			break;
		case KFD_IOCTL_SVM_ATTR_ACCESS:
		case KFD_IOCTL_SVM_ATTR_ACCESS_IN_PLACE:
		case KFD_IOCTL_SVM_ATTR_NO_ACCESS:
			gpuidx = kfd_process_gpuidx_from_gpuid(p,
							       attrs[i].value);
			if (attrs[i].type == KFD_IOCTL_SVM_ATTR_NO_ACCESS) {
				bitmap_clear(prange->bitmap_access, gpuidx, 1);
				bitmap_clear(prange->bitmap_aip, gpuidx, 1);
			} else if (attrs[i].type == KFD_IOCTL_SVM_ATTR_ACCESS) {
				bitmap_set(prange->bitmap_access, gpuidx, 1);
				bitmap_clear(prange->bitmap_aip, gpuidx, 1);
			} else {
				bitmap_clear(prange->bitmap_access, gpuidx, 1);
				bitmap_set(prange->bitmap_aip, gpuidx, 1);
			}
			break;
		case KFD_IOCTL_SVM_ATTR_SET_FLAGS:
			prange->flags |= attrs[i].value;
			break;
		case KFD_IOCTL_SVM_ATTR_CLR_FLAGS:
			prange->flags &= ~attrs[i].value;
			break;
		case KFD_IOCTL_SVM_ATTR_GRANULARITY:
			prange->granularity = attrs[i].value;
			break;
		default:
			WARN_ONCE(1, "svm_range_check_attrs wasn't called?");
		}
	}
}

/**
 * svm_range_debug_dump - print all range information from svms
 * @svms: svm range list header
 *
 * debug output svm range start, end, prefetch location from svms
 * interval tree and link list
 *
 * Context: The caller must hold svms->lock
 */
static void svm_range_debug_dump(struct svm_range_list *svms)
{
	struct interval_tree_node *node;
	struct svm_range *prange;

	pr_debug("dump svms 0x%p list\n", svms);
	pr_debug("range\tstart\tpage\tend\t\tlocation\n");

	list_for_each_entry(prange, &svms->list, list) {
		pr_debug("0x%p 0x%lx\t0x%llx\t0x%llx\t0x%x\n",
			 prange, prange->start, prange->npages,
			 prange->start + prange->npages - 1,
			 prange->actual_loc);
	}

	pr_debug("dump svms 0x%p interval tree\n", svms);
	pr_debug("range\tstart\tpage\tend\t\tlocation\n");
	node = interval_tree_iter_first(&svms->objects, 0, ~0ULL);
	while (node) {
		prange = container_of(node, struct svm_range, it_node);
		pr_debug("0x%p 0x%lx\t0x%llx\t0x%llx\t0x%x\n",
			 prange, prange->start, prange->npages,
			 prange->start + prange->npages - 1,
			 prange->actual_loc);
		node = interval_tree_iter_next(node, 0, ~0ULL);
	}
}

static bool
svm_range_is_same_attrs(struct svm_range *old, struct svm_range *new)
{
	return (old->prefetch_loc == new->prefetch_loc &&
		old->flags == new->flags &&
		old->granularity == new->granularity);
}

/**
 * svm_range_split_adjust - split range and adjust
 *
 * @new: new range
 * @old: the old range
 * @start: the old range adjust to start address in pages
 * @last: the old range adjust to last address in pages
 *
 * Copy attributes in old range to new
 * range from new_start up to size new->npages, the remaining old range is from
 * start to last
 *
 * Return:
 * 0 - OK, -ENOMEM - out of memory
 */
static int
svm_range_split_adjust(struct svm_range *new, struct svm_range *old,
		      uint64_t start, uint64_t last)
{
	pr_debug("svms 0x%p new 0x%lx old [0x%lx 0x%lx] => [0x%llx 0x%llx]\n",
		 new->svms, new->start, old->start, old->last, start, last);

	if (new->start < old->start ||
	    new->last > old->last) {
		WARN_ONCE(1, "invalid new range start or last\n");
		return -EINVAL;
	}

	old->npages = last - start + 1;
	old->start = start;
	old->last = last;
	new->flags = old->flags;
	new->preferred_loc = old->preferred_loc;
	new->prefetch_loc = old->prefetch_loc;
	new->actual_loc = old->actual_loc;
	new->granularity = old->granularity;
	bitmap_copy(new->bitmap_access, old->bitmap_access, MAX_GPU_INSTANCE);
	bitmap_copy(new->bitmap_aip, old->bitmap_aip, MAX_GPU_INSTANCE);

	return 0;
}

/**
 * svm_range_split - split a range in 2 ranges
 *
 * @prange: the svm range to split
 * @start: the remaining range start address in pages
 * @last: the remaining range last address in pages
 * @new: the result new range generated
 *
 * Two cases only:
 * case 1: if start == prange->start
 *         prange ==> prange[start, last]
 *         new range [last + 1, prange->last]
 *
 * case 2: if last == prange->last
 *         prange ==> prange[start, last]
 *         new range [prange->start, start - 1]
 *
 * Return:
 * 0 - OK, -ENOMEM - out of memory, -EINVAL - invalid start, last
 */
static int
svm_range_split(struct svm_range *prange, uint64_t start, uint64_t last,
		struct svm_range **new)
{
	uint64_t old_start = prange->start;
	uint64_t old_last = prange->last;
	struct svm_range_list *svms;
	int r = 0;

	pr_debug("svms 0x%p [0x%llx 0x%llx] to [0x%llx 0x%llx]\n", prange->svms,
		 old_start, old_last, start, last);

	if (old_start != start && old_last != last)
		return -EINVAL;
	if (start < old_start || last > old_last)
		return -EINVAL;

	svms = prange->svms;
	if (old_start == start)
		*new = svm_range_new(svms, last + 1, old_last);
	else
		*new = svm_range_new(svms, old_start, start - 1);
	if (!*new)
		return -ENOMEM;

	r = svm_range_split_adjust(*new, prange, start, last);
	if (r) {
		pr_debug("failed %d split [0x%llx 0x%llx] to [0x%llx 0x%llx]\n",
			 r, old_start, old_last, start, last);
		svm_range_free(*new);
		*new = NULL;
	}

	return r;
}

static int
svm_range_split_tail(struct svm_range *prange, struct svm_range *new,
		     uint64_t new_last, struct list_head *insert_list)
{
	struct svm_range *tail;
	int r = svm_range_split(prange, prange->start, new_last, &tail);

	if (!r)
		list_add(&tail->insert_list, insert_list);
	return r;
}

static int
svm_range_split_head(struct svm_range *prange, struct svm_range *new,
		     uint64_t new_start, struct list_head *insert_list)
{
	struct svm_range *head;
	int r = svm_range_split(prange, new_start, prange->last, &head);

	if (!r)
		list_add(&head->insert_list, insert_list);
	return r;
}

static void
svm_range_add_child(struct svm_range *prange, struct mm_struct *mm,
		    struct svm_range *pchild, enum svm_work_list_ops op)
{
	pr_debug("add child 0x%p [0x%lx 0x%lx] to prange 0x%p child list %d\n",
		 pchild, pchild->start, pchild->last, prange, op);

	pchild->work_item.mm = mm;
	pchild->work_item.op = op;
	list_add_tail(&pchild->child_list, &prange->child_list);
}

/*
 * Validation+GPU mapping with concurrent invalidation (MMU notifiers)
 *
 * To prevent concurrent destruction or change of range attributes, the
 * svm_read_lock must be held. The caller must not hold the svm_write_lock
 * because that would block concurrent evictions and lead to deadlocks. To
 * serialize concurrent migrations or validations of the same range, the
 * prange->migrate_mutex must be held.
 *
 * For VRAM ranges, the SVM BO must be allocated and valid (protected by its
 * eviction fence.
 *
 * The following sequence ensures race-free validation and GPU mapping:
 *
 * 1. Reserve page table (and SVM BO if range is in VRAM)
 * 2. hmm_range_fault to get page addresses (if system memory)
 * 3. DMA-map pages (if system memory)
 * 4-a. Take notifier lock
 * 4-b. Check that pages still valid (mmu_interval_read_retry)
 * 4-c. Check that the range was not split or otherwise invalidated
 * 4-d. Update GPU page table
 * 4.e. Release notifier lock
 * 5. Release page table (and SVM BO) reservation
 */
static int svm_range_validate_and_map(struct mm_struct *mm,
				      struct svm_range *prange,
				      uint32_t gpuidx, bool intr, bool wait)
{
	struct hmm_range *hmm_range;
	int r = 0;

	if (!prange->actual_loc) {
		r = amdgpu_hmm_range_get_pages(&prange->notifier, mm, NULL,
					       prange->start << PAGE_SHIFT,
					       prange->npages, &hmm_range,
					       false, true);
		if (r) {
			pr_debug("failed %d to get svm range pages\n", r);
			goto unreserve_out;
		}
	}

	svm_range_lock(prange);
	if (!prange->actual_loc) {
		if (amdgpu_hmm_range_get_pages_done(hmm_range)) {
			r = -EAGAIN;
			goto unlock_out;
		}
	}

	/* TODO: map to GPU */

unlock_out:
	svm_range_unlock(prange);
unreserve_out:

	return r;
}

/**
 * svm_range_list_lock_and_flush_work - flush pending deferred work
 *
 * @svms: the svm range list
 * @mm: the mm structure
 *
 * Context: Returns with mmap write lock held, pending deferred work flushed
 *
 */
static void
svm_range_list_lock_and_flush_work(struct svm_range_list *svms,
				   struct mm_struct *mm)
{
retry_flush_work:
	flush_work(&svms->deferred_list_work);
	mmap_write_lock(mm);

	if (list_empty(&svms->deferred_range_list))
		return;
	mmap_write_unlock(mm);
	pr_debug("retry flush\n");
	goto retry_flush_work;
}

static struct svm_range *svm_range_clone(struct svm_range *old)
{
	struct svm_range *new;

	new = svm_range_new(old->svms, old->start, old->last);
	if (!new)
		return NULL;

	new->flags = old->flags;
	new->preferred_loc = old->preferred_loc;
	new->prefetch_loc = old->prefetch_loc;
	new->actual_loc = old->actual_loc;
	new->granularity = old->granularity;
	bitmap_copy(new->bitmap_access, old->bitmap_access, MAX_GPU_INSTANCE);
	bitmap_copy(new->bitmap_aip, old->bitmap_aip, MAX_GPU_INSTANCE);

	return new;
}

/**
 * svm_range_handle_overlap - split overlap ranges
 * @svms: svm range list header
 * @new: range added with this attributes
 * @start: range added start address, in pages
 * @last: range last address, in pages
 * @update_list: output, the ranges attributes are updated. For set_attr, this
 *               will do validation and map to GPUs. For unmap, this will be
 *               removed and unmap from GPUs
 * @insert_list: output, the ranges will be inserted into svms, attributes are
 *               not changes. For set_attr, this will add into svms.
 * @remove_list:output, the ranges will be removed from svms
 * @left: the remaining range after overlap, For set_attr, this will be added
 *        as new range.
 *
 * Total have 5 overlap cases.
 *
 * This function handles overlap of an address interval with existing
 * struct svm_ranges for applying new attributes. This may require
 * splitting existing struct svm_ranges. All changes should be applied to
 * the range_list and interval tree transactionally. If any split operation
 * fails, the entire update fails. Therefore the existing overlapping
 * svm_ranges are cloned and the original svm_ranges left unchanged. If the
 * transaction succeeds, the modified clones are added and the originals
 * freed. Otherwise the clones are removed and the old svm_ranges remain.
 *
 * Context: The caller must hold svms->lock
 */
static int
svm_range_handle_overlap(struct svm_range_list *svms, struct svm_range *new,
			 unsigned long start, unsigned long last,
			 struct list_head *update_list,
			 struct list_head *insert_list,
			 struct list_head *remove_list,
			 unsigned long *left)
{
	struct interval_tree_node *node;
	struct svm_range *prange;
	struct svm_range *tmp;
	int r = 0;

	INIT_LIST_HEAD(update_list);
	INIT_LIST_HEAD(insert_list);
	INIT_LIST_HEAD(remove_list);

	node = interval_tree_iter_first(&svms->objects, start, last);
	while (node) {
		struct interval_tree_node *next;
		struct svm_range *old;
		unsigned long next_start;

		pr_debug("found overlap node [0x%lx 0x%lx]\n", node->start,
			 node->last);

		old = container_of(node, struct svm_range, it_node);
		next = interval_tree_iter_next(node, start, last);
		next_start = min(node->last, last) + 1;

		if (node->start < start || node->last > last) {
			/* node intersects the updated range, clone+split it */
			prange = svm_range_clone(old);
			if (!prange) {
				r = -ENOMEM;
				goto out;
			}

			list_add(&old->remove_list, remove_list);
			list_add(&prange->insert_list, insert_list);

			if (node->start < start) {
				pr_debug("change old range start\n");
				r = svm_range_split_head(prange, new, start,
							 insert_list);
				if (r)
					goto out;
			}
			if (node->last > last) {
				pr_debug("change old range last\n");
				r = svm_range_split_tail(prange, new, last,
							 insert_list);
				if (r)
					goto out;
			}
		} else {
			/* The node is contained within start..last,
			 * just update it
			 */
			prange = old;
		}

		if (!svm_range_is_same_attrs(prange, new))
			list_add(&prange->update_list, update_list);

		/* insert a new node if needed */
		if (node->start > start) {
			prange = svm_range_new(prange->svms, start,
					       node->start - 1);
			if (!prange) {
				r = -ENOMEM;
				goto out;
			}

			list_add(&prange->insert_list, insert_list);
			list_add(&prange->update_list, update_list);
		}

		node = next;
		start = next_start;
	}

	if (left && start <= last)
		*left = last - start + 1;

out:
	if (r)
		list_for_each_entry_safe(prange, tmp, insert_list, insert_list)
			svm_range_free(prange);

	return r;
}

static void
svm_range_update_notifier_and_interval_tree(struct mm_struct *mm,
					    struct svm_range *prange)
{
	unsigned long start;
	unsigned long last;

	start = prange->notifier.interval_tree.start >> PAGE_SHIFT;
	last = prange->notifier.interval_tree.last >> PAGE_SHIFT;

	if (prange->start == start && prange->last == last)
		return;

	pr_debug("up notifier 0x%p prange 0x%p [0x%lx 0x%lx] [0x%lx 0x%lx]\n",
		  prange->svms, prange, start, last, prange->start,
		  prange->last);

	if (start != 0 && last != 0) {
		interval_tree_remove(&prange->it_node, &prange->svms->objects);
		svm_range_remove_notifier(prange);
	}
	prange->it_node.start = prange->start;
	prange->it_node.last = prange->last;

	interval_tree_insert(&prange->it_node, &prange->svms->objects);
	svm_range_add_notifier_locked(mm, prange);
}

static void
svm_range_handle_list_op(struct svm_range_list *svms, struct svm_range *prange)
{
	struct mm_struct *mm = prange->work_item.mm;

	switch (prange->work_item.op) {
	case SVM_OP_NULL:
		pr_debug("NULL OP 0x%p prange 0x%p [0x%lx 0x%lx]\n",
			 svms, prange, prange->start, prange->last);
		break;
	case SVM_OP_UNMAP_RANGE:
		pr_debug("remove 0x%p prange 0x%p [0x%lx 0x%lx]\n",
			 svms, prange, prange->start, prange->last);
		svm_range_unlink(prange);
		svm_range_remove_notifier(prange);
		svm_range_free(prange);
		break;
	case SVM_OP_UPDATE_RANGE_NOTIFIER:
		pr_debug("update notifier 0x%p prange 0x%p [0x%lx 0x%lx]\n",
			 svms, prange, prange->start, prange->last);
		svm_range_update_notifier_and_interval_tree(mm, prange);
		break;
	case SVM_OP_ADD_RANGE:
		pr_debug("add 0x%p prange 0x%p [0x%lx 0x%lx]\n", svms, prange,
			 prange->start, prange->last);
		svm_range_add_to_svms(prange);
		svm_range_add_notifier_locked(mm, prange);
		break;
	default:
		WARN_ONCE(1, "Unknown prange 0x%p work op %d\n", prange,
			 prange->work_item.op);
	}
}

static void svm_range_deferred_list_work(struct work_struct *work)
{
	struct svm_range_list *svms;
	struct svm_range *prange;
	struct mm_struct *mm;

	svms = container_of(work, struct svm_range_list, deferred_list_work);
	pr_debug("enter svms 0x%p\n", svms);

	spin_lock(&svms->deferred_list_lock);
	while (!list_empty(&svms->deferred_range_list)) {
		prange = list_first_entry(&svms->deferred_range_list,
					  struct svm_range, deferred_list);
		spin_unlock(&svms->deferred_list_lock);
		pr_debug("prange 0x%p [0x%lx 0x%lx] op %d\n", prange,
			 prange->start, prange->last, prange->work_item.op);

		mm = prange->work_item.mm;
		mmap_write_lock(mm);
		mutex_lock(&svms->lock);

		/* Remove from deferred_list must be inside mmap write lock,
		 * otherwise, svm_range_list_lock_and_flush_work may hold mmap
		 * write lock, and continue because deferred_list is empty, then
		 * deferred_list handle is blocked by mmap write lock.
		 */
		spin_lock(&svms->deferred_list_lock);
		list_del_init(&prange->deferred_list);
		spin_unlock(&svms->deferred_list_lock);

		while (!list_empty(&prange->child_list)) {
			struct svm_range *pchild;

			pchild = list_first_entry(&prange->child_list,
						struct svm_range, child_list);
			pr_debug("child prange 0x%p op %d\n", pchild,
				 pchild->work_item.op);
			list_del_init(&pchild->child_list);
			svm_range_handle_list_op(svms, pchild);
		}

		svm_range_handle_list_op(svms, prange);
		mutex_unlock(&svms->lock);
		mmap_write_unlock(mm);

		spin_lock(&svms->deferred_list_lock);
	}
	spin_unlock(&svms->deferred_list_lock);

	pr_debug("exit svms 0x%p\n", svms);
}

static void
svm_range_add_list_work(struct svm_range_list *svms, struct svm_range *prange,
			struct mm_struct *mm, enum svm_work_list_ops op)
{
	spin_lock(&svms->deferred_list_lock);
	/* if prange is on the deferred list */
	if (!list_empty(&prange->deferred_list)) {
		pr_debug("update exist prange 0x%p work op %d\n", prange, op);
		WARN_ONCE(prange->work_item.mm != mm, "unmatch mm\n");
		if (op != SVM_OP_NULL &&
		    prange->work_item.op != SVM_OP_UNMAP_RANGE)
			prange->work_item.op = op;
	} else {
		prange->work_item.op = op;
		prange->work_item.mm = mm;
		list_add_tail(&prange->deferred_list,
			      &prange->svms->deferred_range_list);
		pr_debug("add prange 0x%p [0x%lx 0x%lx] to work list op %d\n",
			 prange, prange->start, prange->last, op);
	}
	spin_unlock(&svms->deferred_list_lock);
}

static void schedule_deferred_list_work(struct svm_range_list *svms)
{
	spin_lock(&svms->deferred_list_lock);
	if (!list_empty(&svms->deferred_range_list))
		schedule_work(&svms->deferred_list_work);
	spin_unlock(&svms->deferred_list_lock);
}

static void
svm_range_unmap_split(struct mm_struct *mm, struct svm_range *parent,
		      struct svm_range *prange, unsigned long start,
		      unsigned long last)
{
	struct svm_range *head;
	struct svm_range *tail;

	if (prange->work_item.op == SVM_OP_UNMAP_RANGE) {
		pr_debug("prange 0x%p [0x%lx 0x%lx] is already freed\n", prange,
			 prange->start, prange->last);
		return;
	}
	if (start > prange->last || last < prange->start)
		return;

	head = tail = prange;
	if (start > prange->start)
		svm_range_split(prange, prange->start, start - 1, &tail);
	if (last < tail->last)
		svm_range_split(tail, last + 1, tail->last, &head);

	if (head != prange && tail != prange) {
		svm_range_add_child(parent, mm, head, SVM_OP_UNMAP_RANGE);
		svm_range_add_child(parent, mm, tail, SVM_OP_ADD_RANGE);
	} else if (tail != prange) {
		svm_range_add_child(parent, mm, tail, SVM_OP_UNMAP_RANGE);
	} else if (head != prange) {
		svm_range_add_child(parent, mm, head, SVM_OP_UNMAP_RANGE);
	} else if (parent != prange) {
		prange->work_item.op = SVM_OP_UNMAP_RANGE;
	}
}

static void
svm_range_unmap_from_cpu(struct mm_struct *mm, struct svm_range *prange,
			 unsigned long start, unsigned long last)
{
	struct svm_range_list *svms;
	struct svm_range *pchild;
	struct kfd_process *p;
	bool unmap_parent;

	p = kfd_lookup_process_by_mm(mm);
	if (!p)
		return;
	svms = &p->svms;

	pr_debug("svms 0x%p prange 0x%p [0x%lx 0x%lx] [0x%lx 0x%lx]\n", svms,
		 prange, prange->start, prange->last, start, last);

	unmap_parent = start <= prange->start && last >= prange->last;

	list_for_each_entry(pchild, &prange->child_list, child_list)
		svm_range_unmap_split(mm, prange, pchild, start, last);
	svm_range_unmap_split(mm, prange, prange, start, last);

	if (unmap_parent)
		svm_range_add_list_work(svms, prange, mm, SVM_OP_UNMAP_RANGE);
	else
		svm_range_add_list_work(svms, prange, mm,
					SVM_OP_UPDATE_RANGE_NOTIFIER);
	schedule_deferred_list_work(svms);

	kfd_unref_process(p);
}

/**
 * svm_range_cpu_invalidate_pagetables - interval notifier callback
 *
 * MMU range unmap notifier to remove svm ranges
 */
static bool
svm_range_cpu_invalidate_pagetables(struct mmu_interval_notifier *mni,
				    const struct mmu_notifier_range *range,
				    unsigned long cur_seq)
{
	struct svm_range *prange;
	unsigned long start;
	unsigned long last;

	if (range->event == MMU_NOTIFY_RELEASE)
		return true;

	start = mni->interval_tree.start;
	last = mni->interval_tree.last;
	start = (start > range->start ? start : range->start) >> PAGE_SHIFT;
	last = (last < (range->end - 1) ? last : range->end - 1) >> PAGE_SHIFT;
	pr_debug("[0x%lx 0x%lx] range[0x%lx 0x%lx] notifier[0x%lx 0x%lx] %d\n",
		 start, last, range->start >> PAGE_SHIFT,
		 (range->end - 1) >> PAGE_SHIFT,
		 mni->interval_tree.start >> PAGE_SHIFT,
		 mni->interval_tree.last >> PAGE_SHIFT, range->event);

	prange = container_of(mni, struct svm_range, notifier);

	svm_range_lock(prange);
	mmu_interval_set_seq(mni, cur_seq);

	switch (range->event) {
	case MMU_NOTIFY_UNMAP:
		svm_range_unmap_from_cpu(mni->mm, prange, start, last);
		break;
	default:
		break;
	}

	svm_range_unlock(prange);

	return true;
}

void svm_range_list_fini(struct kfd_process *p)
{
	mutex_destroy(&p->svms.lock);

	pr_debug("pasid 0x%x svms 0x%p\n", p->pasid, &p->svms);

	/* Ensure list work is finished before process is destroyed */
	flush_work(&p->svms.deferred_list_work);
}

int svm_range_list_init(struct kfd_process *p)
{
	struct svm_range_list *svms = &p->svms;

	svms->objects = RB_ROOT_CACHED;
	mutex_init(&svms->lock);
	INIT_LIST_HEAD(&svms->list);
	INIT_WORK(&svms->deferred_list_work, svm_range_deferred_list_work);
	INIT_LIST_HEAD(&svms->deferred_range_list);
	spin_lock_init(&svms->deferred_list_lock);

	return 0;
}

/**
 * svm_range_is_valid - check if virtual address range is valid
 * @mm: current process mm_struct
 * @start: range start address, in pages
 * @size: range size, in pages
 *
 * Valid virtual address range means it belongs to one or more VMAs
 *
 * Context: Process context
 *
 * Return:
 *  true - valid svm range
 *  false - invalid svm range
 */
static bool
svm_range_is_valid(struct mm_struct *mm, uint64_t start, uint64_t size)
{
	const unsigned long device_vma = VM_IO | VM_PFNMAP | VM_MIXEDMAP;
	struct vm_area_struct *vma;
	unsigned long end;

	start <<= PAGE_SHIFT;
	end = start + (size << PAGE_SHIFT);

	do {
		vma = find_vma(mm, start);
		if (!vma || start < vma->vm_start ||
		    (vma->vm_flags & device_vma))
			return false;
		start = min(end, vma->vm_end);
	} while (start < end);

	return true;
}

/**
 * svm_range_add - add svm range and handle overlap
 * @p: the range add to this process svms
 * @start: page size aligned
 * @size: page size aligned
 * @nattr: number of attributes
 * @attrs: array of attributes
 * @update_list: output, the ranges need validate and update GPU mapping
 * @insert_list: output, the ranges need insert to svms
 * @remove_list: output, the ranges are replaced and need remove from svms
 *
 * Check if the virtual address range has overlap with the registered ranges,
 * split the overlapped range, copy and adjust pages address and vram nodes in
 * old and new ranges.
 *
 * Context: Process context, caller must hold svms->lock
 *
 * Return:
 * 0 - OK, otherwise error code
 */
static int
svm_range_add(struct kfd_process *p, uint64_t start, uint64_t size,
	      uint32_t nattr, struct kfd_ioctl_svm_attribute *attrs,
	      struct list_head *update_list, struct list_head *insert_list,
	      struct list_head *remove_list)
{
	uint64_t last = start + size - 1UL;
	struct svm_range_list *svms;
	struct svm_range new = {0};
	struct svm_range *prange;
	unsigned long left = 0;
	int r = 0;

	pr_debug("svms 0x%p [0x%llx 0x%llx]\n", &p->svms, start, last);

	svm_range_apply_attrs(p, &new, nattr, attrs);

	svms = &p->svms;

	r = svm_range_handle_overlap(svms, &new, start, last, update_list,
				     insert_list, remove_list, &left);
	if (r)
		return r;

	if (left) {
		prange = svm_range_new(svms, last - left + 1, last);
		list_add(&prange->insert_list, insert_list);
		list_add(&prange->update_list, update_list);
	}

	return 0;
}

static int
svm_range_set_attr(struct kfd_process *p, uint64_t start, uint64_t size,
		   uint32_t nattr, struct kfd_ioctl_svm_attribute *attrs)
{
	struct amdkfd_process_info *process_info = p->kgd_process_info;
	struct mm_struct *mm = current->mm;
	struct list_head update_list;
	struct list_head insert_list;
	struct list_head remove_list;
	struct svm_range_list *svms;
	struct svm_range *prange;
	struct svm_range *next;
	int r = 0;

	pr_debug("pasid 0x%x svms 0x%p [0x%llx 0x%llx] pages 0x%llx\n",
		 p->pasid, &p->svms, start, start + size - 1, size);

	r = svm_range_check_attr(p, nattr, attrs);
	if (r)
		return r;

	svms = &p->svms;

	mutex_lock(&process_info->lock);

	svm_range_list_lock_and_flush_work(svms, mm);

	if (!svm_range_is_valid(mm, start, size)) {
		pr_debug("invalid range\n");
		r = -EFAULT;
		mmap_write_unlock(mm);
		goto out;
	}

	mutex_lock(&svms->lock);

	/* Add new range and split existing ranges as needed */
	r = svm_range_add(p, start, size, nattr, attrs, &update_list,
			  &insert_list, &remove_list);
	if (r) {
		mutex_unlock(&svms->lock);
		mmap_write_unlock(mm);
		goto out;
	}
	/* Apply changes as a transaction */
	list_for_each_entry_safe(prange, next, &insert_list, insert_list) {
		svm_range_add_to_svms(prange);
		svm_range_add_notifier_locked(mm, prange);
	}
	list_for_each_entry(prange, &update_list, update_list) {
		svm_range_apply_attrs(p, prange, nattr, attrs);
		/* TODO: unmap ranges from GPU that lost access */
	}
	list_for_each_entry_safe(prange, next, &remove_list,
				remove_list) {
		pr_debug("unlink old 0x%p prange 0x%p [0x%lx 0x%lx]\n",
			 prange->svms, prange, prange->start,
			 prange->last);
		svm_range_unlink(prange);
		svm_range_remove_notifier(prange);
		svm_range_free(prange);
	}

	mmap_write_downgrade(mm);
	/* Trigger migrations and revalidate and map to GPUs as needed. If
	 * this fails we may be left with partially completed actions. There
	 * is no clean way of rolling back to the previous state in such a
	 * case because the rollback wouldn't be guaranteed to work either.
	 */
	list_for_each_entry(prange, &update_list, update_list) {
		r = svm_range_validate_and_map(mm, prange, MAX_GPU_INSTANCE,
					       true, true);
		if (r) {
			pr_debug("failed %d to map 0x%lx to gpus\n", r,
				 prange->start);
			break;
		}
	}

	svm_range_debug_dump(svms);

	mutex_unlock(&svms->lock);
	mmap_read_unlock(mm);
out:
	mutex_unlock(&process_info->lock);

	pr_debug("pasid 0x%x svms 0x%p [0x%llx 0x%llx] done, r=%d\n", p->pasid,
		 &p->svms, start, start + size - 1, r);

	return r;
}

static int
svm_range_get_attr(struct kfd_process *p, uint64_t start, uint64_t size,
		   uint32_t nattr, struct kfd_ioctl_svm_attribute *attrs)
{
	DECLARE_BITMAP(bitmap_access, MAX_GPU_INSTANCE);
	DECLARE_BITMAP(bitmap_aip, MAX_GPU_INSTANCE);
	bool get_preferred_loc = false;
	bool get_prefetch_loc = false;
	bool get_granularity = false;
	bool get_accessible = false;
	bool get_flags = false;
	uint64_t last = start + size - 1UL;
	struct mm_struct *mm = current->mm;
	uint8_t granularity = 0xff;
	struct interval_tree_node *node;
	struct svm_range_list *svms;
	struct svm_range *prange;
	uint32_t prefetch_loc = KFD_IOCTL_SVM_LOCATION_UNDEFINED;
	uint32_t location = KFD_IOCTL_SVM_LOCATION_UNDEFINED;
	uint32_t flags = 0xffffffff;
	int gpuidx;
	uint32_t i;

	pr_debug("svms 0x%p [0x%llx 0x%llx] nattr 0x%x\n", &p->svms, start,
		 start + size - 1, nattr);

	mmap_read_lock(mm);
	if (!svm_range_is_valid(mm, start, size)) {
		pr_debug("invalid range\n");
		mmap_read_unlock(mm);
		return -EINVAL;
	}
	mmap_read_unlock(mm);

	for (i = 0; i < nattr; i++) {
		switch (attrs[i].type) {
		case KFD_IOCTL_SVM_ATTR_PREFERRED_LOC:
			get_preferred_loc = true;
			break;
		case KFD_IOCTL_SVM_ATTR_PREFETCH_LOC:
			get_prefetch_loc = true;
			break;
		case KFD_IOCTL_SVM_ATTR_ACCESS:
			get_accessible = true;
			break;
		case KFD_IOCTL_SVM_ATTR_SET_FLAGS:
			get_flags = true;
			break;
		case KFD_IOCTL_SVM_ATTR_GRANULARITY:
			get_granularity = true;
			break;
		case KFD_IOCTL_SVM_ATTR_CLR_FLAGS:
		case KFD_IOCTL_SVM_ATTR_ACCESS_IN_PLACE:
		case KFD_IOCTL_SVM_ATTR_NO_ACCESS:
			fallthrough;
		default:
			pr_debug("get invalid attr type 0x%x\n", attrs[i].type);
			return -EINVAL;
		}
	}

	svms = &p->svms;

	mutex_lock(&svms->lock);

	node = interval_tree_iter_first(&svms->objects, start, last);
	if (!node) {
		pr_debug("range attrs not found return default values\n");
		svm_range_set_default_attributes(&location, &prefetch_loc,
						 &granularity, &flags);
		/* TODO: Automatically create SVM ranges and map them on
		 * GPU page faults
		if (p->xnack_enabled)
			bitmap_fill(bitmap_access, MAX_GPU_INSTANCE);
		 */

		goto fill_values;
	}
	bitmap_fill(bitmap_access, MAX_GPU_INSTANCE);
	bitmap_fill(bitmap_aip, MAX_GPU_INSTANCE);

	while (node) {
		struct interval_tree_node *next;

		prange = container_of(node, struct svm_range, it_node);
		next = interval_tree_iter_next(node, start, last);

		if (get_preferred_loc) {
			if (prange->preferred_loc ==
					KFD_IOCTL_SVM_LOCATION_UNDEFINED ||
			    (location != KFD_IOCTL_SVM_LOCATION_UNDEFINED &&
			     location != prange->preferred_loc)) {
				location = KFD_IOCTL_SVM_LOCATION_UNDEFINED;
				get_preferred_loc = false;
			} else {
				location = prange->preferred_loc;
			}
		}
		if (get_prefetch_loc) {
			if (prange->prefetch_loc ==
					KFD_IOCTL_SVM_LOCATION_UNDEFINED ||
			    (prefetch_loc != KFD_IOCTL_SVM_LOCATION_UNDEFINED &&
			     prefetch_loc != prange->prefetch_loc)) {
				prefetch_loc = KFD_IOCTL_SVM_LOCATION_UNDEFINED;
				get_prefetch_loc = false;
			} else {
				prefetch_loc = prange->prefetch_loc;
			}
		}
		if (get_accessible) {
			bitmap_and(bitmap_access, bitmap_access,
				   prange->bitmap_access, MAX_GPU_INSTANCE);
			bitmap_and(bitmap_aip, bitmap_aip,
				   prange->bitmap_aip, MAX_GPU_INSTANCE);
		}
		if (get_flags)
			flags &= prange->flags;

		if (get_granularity && prange->granularity < granularity)
			granularity = prange->granularity;

		node = next;
	}
fill_values:
	mutex_unlock(&svms->lock);

	for (i = 0; i < nattr; i++) {
		switch (attrs[i].type) {
		case KFD_IOCTL_SVM_ATTR_PREFERRED_LOC:
			attrs[i].value = location;
			break;
		case KFD_IOCTL_SVM_ATTR_PREFETCH_LOC:
			attrs[i].value = prefetch_loc;
			break;
		case KFD_IOCTL_SVM_ATTR_ACCESS:
			gpuidx = kfd_process_gpuidx_from_gpuid(p,
							       attrs[i].value);
			if (gpuidx < 0) {
				pr_debug("invalid gpuid %x\n", attrs[i].value);
				return -EINVAL;
			}
			if (test_bit(gpuidx, bitmap_access))
				attrs[i].type = KFD_IOCTL_SVM_ATTR_ACCESS;
			else if (test_bit(gpuidx, bitmap_aip))
				attrs[i].type =
					KFD_IOCTL_SVM_ATTR_ACCESS_IN_PLACE;
			else
				attrs[i].type = KFD_IOCTL_SVM_ATTR_NO_ACCESS;
			break;
		case KFD_IOCTL_SVM_ATTR_SET_FLAGS:
			attrs[i].value = flags;
			break;
		case KFD_IOCTL_SVM_ATTR_GRANULARITY:
			attrs[i].value = (uint32_t)granularity;
			break;
		}
	}

	return 0;
}

int
svm_ioctl(struct kfd_process *p, enum kfd_ioctl_svm_op op, uint64_t start,
	  uint64_t size, uint32_t nattrs, struct kfd_ioctl_svm_attribute *attrs)
{
	int r;

	start >>= PAGE_SHIFT;
	size >>= PAGE_SHIFT;

	switch (op) {
	case KFD_IOCTL_SVM_OP_SET_ATTR:
		r = svm_range_set_attr(p, start, size, nattrs, attrs);
		break;
	case KFD_IOCTL_SVM_OP_GET_ATTR:
		r = svm_range_get_attr(p, start, size, nattrs, attrs);
		break;
	default:
		r = EINVAL;
		break;
	}

	return r;
}
