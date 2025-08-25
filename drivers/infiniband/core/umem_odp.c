/*
 * Copyright (c) 2014 Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/sched/task.h>
#include <linux/pid.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/vmalloc.h>
#include <linux/hugetlb.h>
#include <linux/interval_tree.h>
#include <linux/hmm.h>
#include <linux/hmm-dma.h>
#include <linux/pagemap.h>

#include <rdma/ib_umem_odp.h>

#include "uverbs.h"

static void ib_init_umem_implicit_odp(struct ib_umem_odp *umem_odp)
{
	umem_odp->is_implicit_odp = 1;
	umem_odp->umem.is_odp = 1;
	mutex_init(&umem_odp->umem_mutex);
}

static int ib_init_umem_odp(struct ib_umem_odp *umem_odp,
			    const struct mmu_interval_notifier_ops *ops)
{
	struct ib_device *dev = umem_odp->umem.ibdev;
	size_t page_size = 1UL << umem_odp->page_shift;
	struct hmm_dma_map *map;
	unsigned long start;
	unsigned long end;
	size_t nr_entries;
	int ret = 0;

	umem_odp->umem.is_odp = 1;
	mutex_init(&umem_odp->umem_mutex);

	start = ALIGN_DOWN(umem_odp->umem.address, page_size);
	if (check_add_overflow(umem_odp->umem.address,
			       (unsigned long)umem_odp->umem.length, &end))
		return -EOVERFLOW;
	end = ALIGN(end, page_size);
	if (unlikely(end < page_size))
		return -EOVERFLOW;
	/*
	 * The mmu notifier can be called within reclaim contexts and takes the
	 * umem_mutex. This is rare to trigger in testing, teach lockdep about
	 * it.
	 */
	if (IS_ENABLED(CONFIG_LOCKDEP)) {
		fs_reclaim_acquire(GFP_KERNEL);
		mutex_lock(&umem_odp->umem_mutex);
		mutex_unlock(&umem_odp->umem_mutex);
		fs_reclaim_release(GFP_KERNEL);
	}

	nr_entries = (end - start) >> PAGE_SHIFT;
	if (!(nr_entries * PAGE_SIZE / page_size))
		return -EINVAL;

	map = &umem_odp->map;
	if (ib_uses_virt_dma(dev)) {
		map->pfn_list = kvcalloc(nr_entries, sizeof(*map->pfn_list),
					 GFP_KERNEL | __GFP_NOWARN);
		if (!map->pfn_list)
			ret = -ENOMEM;
	} else
		ret = hmm_dma_map_alloc(dev->dma_device, map,
					(end - start) >> PAGE_SHIFT,
					1 << umem_odp->page_shift);
	if (ret)
		return ret;

	ret = mmu_interval_notifier_insert(&umem_odp->notifier,
					   umem_odp->umem.owning_mm, start,
					   end - start, ops);
	if (ret)
		goto out_free_map;

	return 0;

out_free_map:
	if (ib_uses_virt_dma(dev))
		kvfree(map->pfn_list);
	else
		hmm_dma_map_free(dev->dma_device, map);
	return ret;
}

/**
 * ib_umem_odp_alloc_implicit - Allocate a parent implicit ODP umem
 *
 * Implicit ODP umems do not have a VA range and do not have any page lists.
 * They exist only to hold the per_mm reference to help the driver create
 * children umems.
 *
 * @device: IB device to create UMEM
 * @access: ib_reg_mr access flags
 */
struct ib_umem_odp *ib_umem_odp_alloc_implicit(struct ib_device *device,
					       int access)
{
	struct ib_umem *umem;
	struct ib_umem_odp *umem_odp;

	if (access & IB_ACCESS_HUGETLB)
		return ERR_PTR(-EINVAL);

	umem_odp = kzalloc(sizeof(*umem_odp), GFP_KERNEL);
	if (!umem_odp)
		return ERR_PTR(-ENOMEM);
	umem = &umem_odp->umem;
	umem->ibdev = device;
	umem->writable = ib_access_writable(access);
	umem->owning_mm = current->mm;
	umem_odp->page_shift = PAGE_SHIFT;

	umem_odp->tgid = get_task_pid(current->group_leader, PIDTYPE_PID);
	ib_init_umem_implicit_odp(umem_odp);
	return umem_odp;
}
EXPORT_SYMBOL(ib_umem_odp_alloc_implicit);

/**
 * ib_umem_odp_alloc_child - Allocate a child ODP umem under an implicit
 *                           parent ODP umem
 *
 * @root: The parent umem enclosing the child. This must be allocated using
 *        ib_alloc_implicit_odp_umem()
 * @addr: The starting userspace VA
 * @size: The length of the userspace VA
 * @ops: MMU interval ops, currently only @invalidate
 */
struct ib_umem_odp *
ib_umem_odp_alloc_child(struct ib_umem_odp *root, unsigned long addr,
			size_t size,
			const struct mmu_interval_notifier_ops *ops)
{
	/*
	 * Caller must ensure that root cannot be freed during the call to
	 * ib_alloc_odp_umem.
	 */
	struct ib_umem_odp *odp_data;
	struct ib_umem *umem;
	int ret;

	if (WARN_ON(!root->is_implicit_odp))
		return ERR_PTR(-EINVAL);

	odp_data = kzalloc(sizeof(*odp_data), GFP_KERNEL);
	if (!odp_data)
		return ERR_PTR(-ENOMEM);
	umem = &odp_data->umem;
	umem->ibdev = root->umem.ibdev;
	umem->length     = size;
	umem->address    = addr;
	umem->writable   = root->umem.writable;
	umem->owning_mm  = root->umem.owning_mm;
	odp_data->page_shift = PAGE_SHIFT;
	odp_data->notifier.ops = ops;

	/*
	 * A mmget must be held when registering a notifier, the owming_mm only
	 * has a mm_grab at this point.
	 */
	if (!mmget_not_zero(umem->owning_mm)) {
		ret = -EFAULT;
		goto out_free;
	}

	odp_data->tgid = get_pid(root->tgid);
	ret = ib_init_umem_odp(odp_data, ops);
	if (ret)
		goto out_tgid;
	mmput(umem->owning_mm);
	return odp_data;

out_tgid:
	put_pid(odp_data->tgid);
	mmput(umem->owning_mm);
out_free:
	kfree(odp_data);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL(ib_umem_odp_alloc_child);

/**
 * ib_umem_odp_get - Create a umem_odp for a userspace va
 *
 * @device: IB device struct to get UMEM
 * @addr: userspace virtual address to start at
 * @size: length of region to pin
 * @access: IB_ACCESS_xxx flags for memory being pinned
 * @ops: MMU interval ops, currently only @invalidate
 *
 * The driver should use when the access flags indicate ODP memory. It avoids
 * pinning, instead, stores the mm for future page fault handling in
 * conjunction with MMU notifiers.
 */
struct ib_umem_odp *ib_umem_odp_get(struct ib_device *device,
				    unsigned long addr, size_t size, int access,
				    const struct mmu_interval_notifier_ops *ops)
{
	struct ib_umem_odp *umem_odp;
	int ret;

	if (WARN_ON_ONCE(!(access & IB_ACCESS_ON_DEMAND)))
		return ERR_PTR(-EINVAL);

	umem_odp = kzalloc(sizeof(struct ib_umem_odp), GFP_KERNEL);
	if (!umem_odp)
		return ERR_PTR(-ENOMEM);

	umem_odp->umem.ibdev = device;
	umem_odp->umem.length = size;
	umem_odp->umem.address = addr;
	umem_odp->umem.writable = ib_access_writable(access);
	umem_odp->umem.owning_mm = current->mm;
	umem_odp->notifier.ops = ops;

	umem_odp->page_shift = PAGE_SHIFT;
#ifdef CONFIG_HUGETLB_PAGE
	if (access & IB_ACCESS_HUGETLB)
		umem_odp->page_shift = HPAGE_SHIFT;
#endif

	umem_odp->tgid = get_task_pid(current->group_leader, PIDTYPE_PID);
	ret = ib_init_umem_odp(umem_odp, ops);
	if (ret)
		goto err_put_pid;
	return umem_odp;

err_put_pid:
	put_pid(umem_odp->tgid);
	kfree(umem_odp);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL(ib_umem_odp_get);

static void ib_umem_odp_free(struct ib_umem_odp *umem_odp)
{
	struct ib_device *dev = umem_odp->umem.ibdev;

	/*
	 * Ensure that no more pages are mapped in the umem.
	 *
	 * It is the driver's responsibility to ensure, before calling us,
	 * that the hardware will not attempt to access the MR any more.
	 */
	mutex_lock(&umem_odp->umem_mutex);
	ib_umem_odp_unmap_dma_pages(umem_odp, ib_umem_start(umem_odp),
				    ib_umem_end(umem_odp));
	mutex_unlock(&umem_odp->umem_mutex);
	mmu_interval_notifier_remove(&umem_odp->notifier);
	if (ib_uses_virt_dma(dev))
		kvfree(umem_odp->map.pfn_list);
	else
		hmm_dma_map_free(dev->dma_device, &umem_odp->map);
}

void ib_umem_odp_release(struct ib_umem_odp *umem_odp)
{
	if (!umem_odp->is_implicit_odp)
		ib_umem_odp_free(umem_odp);

	put_pid(umem_odp->tgid);
	kfree(umem_odp);
}
EXPORT_SYMBOL(ib_umem_odp_release);

/**
 * ib_umem_odp_map_dma_and_lock - DMA map userspace memory in an ODP MR and lock it.
 *
 * Maps the range passed in the argument to DMA addresses.
 * Upon success the ODP MR will be locked to let caller complete its device
 * page table update.
 *
 * Returns the number of pages mapped in success, negative error code
 * for failure.
 * @umem_odp: the umem to map and pin
 * @user_virt: the address from which we need to map.
 * @bcnt: the minimal number of bytes to pin and map. The mapping might be
 *        bigger due to alignment, and may also be smaller in case of an error
 *        pinning or mapping a page. The actual pages mapped is returned in
 *        the return value.
 * @access_mask: bit mask of the requested access permissions for the given
 *               range.
 * @fault: is faulting required for the given range
 */
int ib_umem_odp_map_dma_and_lock(struct ib_umem_odp *umem_odp, u64 user_virt,
				 u64 bcnt, u64 access_mask, bool fault)
			__acquires(&umem_odp->umem_mutex)
{
	struct task_struct *owning_process  = NULL;
	struct mm_struct *owning_mm = umem_odp->umem.owning_mm;
	int pfn_index, dma_index, ret = 0, start_idx;
	unsigned int page_shift, hmm_order, pfn_start_idx;
	unsigned long num_pfns, current_seq;
	struct hmm_range range = {};
	unsigned long timeout;

	if (user_virt < ib_umem_start(umem_odp) ||
	    user_virt + bcnt > ib_umem_end(umem_odp))
		return -EFAULT;

	page_shift = umem_odp->page_shift;

	/*
	 * owning_process is allowed to be NULL, this means somehow the mm is
	 * existing beyond the lifetime of the originating process.. Presumably
	 * mmget_not_zero will fail in this case.
	 */
	owning_process = get_pid_task(umem_odp->tgid, PIDTYPE_PID);
	if (!owning_process || !mmget_not_zero(owning_mm)) {
		ret = -EINVAL;
		goto out_put_task;
	}

	range.notifier = &umem_odp->notifier;
	range.start = ALIGN_DOWN(user_virt, 1UL << page_shift);
	range.end = ALIGN(user_virt + bcnt, 1UL << page_shift);
	pfn_start_idx = (range.start - ib_umem_start(umem_odp)) >> PAGE_SHIFT;
	num_pfns = (range.end - range.start) >> PAGE_SHIFT;
	if (fault) {
		range.default_flags = HMM_PFN_REQ_FAULT;

		if (access_mask & HMM_PFN_WRITE)
			range.default_flags |= HMM_PFN_REQ_WRITE;
	}

	range.hmm_pfns = &(umem_odp->map.pfn_list[pfn_start_idx]);
	timeout = jiffies + msecs_to_jiffies(HMM_RANGE_DEFAULT_TIMEOUT);

retry:
	current_seq = range.notifier_seq =
		mmu_interval_read_begin(&umem_odp->notifier);

	mmap_read_lock(owning_mm);
	ret = hmm_range_fault(&range);
	mmap_read_unlock(owning_mm);
	if (unlikely(ret)) {
		if (ret == -EBUSY && !time_after(jiffies, timeout))
			goto retry;
		goto out_put_mm;
	}

	start_idx = (range.start - ib_umem_start(umem_odp)) >> page_shift;
	dma_index = start_idx;

	mutex_lock(&umem_odp->umem_mutex);
	if (mmu_interval_read_retry(&umem_odp->notifier, current_seq)) {
		mutex_unlock(&umem_odp->umem_mutex);
		goto retry;
	}

	for (pfn_index = 0; pfn_index < num_pfns;
		pfn_index += 1 << (page_shift - PAGE_SHIFT), dma_index++) {

		/*
		 * Since we asked for hmm_range_fault() to populate
		 * pages it shouldn't return an error entry on success.
		 */
		WARN_ON(fault && range.hmm_pfns[pfn_index] & HMM_PFN_ERROR);
		WARN_ON(fault && !(range.hmm_pfns[pfn_index] & HMM_PFN_VALID));
		if (!(range.hmm_pfns[pfn_index] & HMM_PFN_VALID))
			continue;

		if (range.hmm_pfns[pfn_index] & HMM_PFN_DMA_MAPPED)
			continue;

		hmm_order = hmm_pfn_to_map_order(range.hmm_pfns[pfn_index]);
		/* If a hugepage was detected and ODP wasn't set for, the umem
		 * page_shift will be used, the opposite case is an error.
		 */
		if (hmm_order + PAGE_SHIFT < page_shift) {
			ret = -EINVAL;
			ibdev_dbg(umem_odp->umem.ibdev,
				  "%s: un-expected hmm_order %u, page_shift %u\n",
				  __func__, hmm_order, page_shift);
			break;
		}
	}
	/* upon success lock should stay on hold for the callee */
	if (!ret)
		ret = dma_index - start_idx;
	else
		mutex_unlock(&umem_odp->umem_mutex);

out_put_mm:
	mmput_async(owning_mm);
out_put_task:
	if (owning_process)
		put_task_struct(owning_process);
	return ret;
}
EXPORT_SYMBOL(ib_umem_odp_map_dma_and_lock);

void ib_umem_odp_unmap_dma_pages(struct ib_umem_odp *umem_odp, u64 virt,
				 u64 bound)
{
	struct ib_device *dev = umem_odp->umem.ibdev;
	u64 addr;

	lockdep_assert_held(&umem_odp->umem_mutex);

	virt = max_t(u64, virt, ib_umem_start(umem_odp));
	bound = min_t(u64, bound, ib_umem_end(umem_odp));
	for (addr = virt; addr < bound; addr += BIT(umem_odp->page_shift)) {
		u64 offset = addr - ib_umem_start(umem_odp);
		size_t idx = offset >> umem_odp->page_shift;
		unsigned long pfn = umem_odp->map.pfn_list[idx];

		if (!hmm_dma_unmap_pfn(dev->dma_device, &umem_odp->map, idx))
			goto clear;

		if (pfn & HMM_PFN_WRITE) {
			struct page *page = hmm_pfn_to_page(pfn);
			struct page *head_page = compound_head(page);
			/*
			 * set_page_dirty prefers being called with
			 * the page lock. However, MMU notifiers are
			 * called sometimes with and sometimes without
			 * the lock. We rely on the umem_mutex instead
			 * to prevent other mmu notifiers from
			 * continuing and allowing the page mapping to
			 * be removed.
			 */
			set_page_dirty(head_page);
		}
		umem_odp->npages--;
clear:
		umem_odp->map.pfn_list[idx] &= ~HMM_PFN_FLAGS;
	}
}
EXPORT_SYMBOL(ib_umem_odp_unmap_dma_pages);
