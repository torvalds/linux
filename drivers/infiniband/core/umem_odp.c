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
#include <linux/pagemap.h>

#include <rdma/ib_verbs.h>
#include <rdma/ib_umem.h>
#include <rdma/ib_umem_odp.h>

#include "uverbs.h"

static void ib_umem_notifier_start_account(struct ib_umem_odp *umem_odp)
{
	mutex_lock(&umem_odp->umem_mutex);
	if (umem_odp->notifiers_count++ == 0)
		/*
		 * Initialize the completion object for waiting on
		 * notifiers. Since notifier_count is zero, no one should be
		 * waiting right now.
		 */
		reinit_completion(&umem_odp->notifier_completion);
	mutex_unlock(&umem_odp->umem_mutex);
}

static void ib_umem_notifier_end_account(struct ib_umem_odp *umem_odp)
{
	mutex_lock(&umem_odp->umem_mutex);
	/*
	 * This sequence increase will notify the QP page fault that the page
	 * that is going to be mapped in the spte could have been freed.
	 */
	++umem_odp->notifiers_seq;
	if (--umem_odp->notifiers_count == 0)
		complete_all(&umem_odp->notifier_completion);
	mutex_unlock(&umem_odp->umem_mutex);
}

static void ib_umem_notifier_release(struct mmu_notifier *mn,
				     struct mm_struct *mm)
{
	struct ib_ucontext_per_mm *per_mm =
		container_of(mn, struct ib_ucontext_per_mm, mn);
	struct rb_node *node;

	down_read(&per_mm->umem_rwsem);
	if (!per_mm->mn.users)
		goto out;

	for (node = rb_first_cached(&per_mm->umem_tree); node;
	     node = rb_next(node)) {
		struct ib_umem_odp *umem_odp =
			rb_entry(node, struct ib_umem_odp, interval_tree.rb);

		/*
		 * Increase the number of notifiers running, to prevent any
		 * further fault handling on this MR.
		 */
		ib_umem_notifier_start_account(umem_odp);
		complete_all(&umem_odp->notifier_completion);
		umem_odp->umem.ibdev->ops.invalidate_range(
			umem_odp, ib_umem_start(umem_odp),
			ib_umem_end(umem_odp));
	}

out:
	up_read(&per_mm->umem_rwsem);
}

static int invalidate_range_start_trampoline(struct ib_umem_odp *item,
					     u64 start, u64 end, void *cookie)
{
	ib_umem_notifier_start_account(item);
	item->umem.ibdev->ops.invalidate_range(item, start, end);
	return 0;
}

static int ib_umem_notifier_invalidate_range_start(struct mmu_notifier *mn,
				const struct mmu_notifier_range *range)
{
	struct ib_ucontext_per_mm *per_mm =
		container_of(mn, struct ib_ucontext_per_mm, mn);
	int rc;

	if (mmu_notifier_range_blockable(range))
		down_read(&per_mm->umem_rwsem);
	else if (!down_read_trylock(&per_mm->umem_rwsem))
		return -EAGAIN;

	if (!per_mm->mn.users) {
		up_read(&per_mm->umem_rwsem);
		/*
		 * At this point users is permanently zero and visible to this
		 * CPU without a lock, that fact is relied on to skip the unlock
		 * in range_end.
		 */
		return 0;
	}

	rc = rbt_ib_umem_for_each_in_range(&per_mm->umem_tree, range->start,
					   range->end,
					   invalidate_range_start_trampoline,
					   mmu_notifier_range_blockable(range),
					   NULL);
	if (rc)
		up_read(&per_mm->umem_rwsem);
	return rc;
}

static int invalidate_range_end_trampoline(struct ib_umem_odp *item, u64 start,
					   u64 end, void *cookie)
{
	ib_umem_notifier_end_account(item);
	return 0;
}

static void ib_umem_notifier_invalidate_range_end(struct mmu_notifier *mn,
				const struct mmu_notifier_range *range)
{
	struct ib_ucontext_per_mm *per_mm =
		container_of(mn, struct ib_ucontext_per_mm, mn);

	if (unlikely(!per_mm->mn.users))
		return;

	rbt_ib_umem_for_each_in_range(&per_mm->umem_tree, range->start,
				      range->end,
				      invalidate_range_end_trampoline, true, NULL);
	up_read(&per_mm->umem_rwsem);
}

static struct mmu_notifier *ib_umem_alloc_notifier(struct mm_struct *mm)
{
	struct ib_ucontext_per_mm *per_mm;

	per_mm = kzalloc(sizeof(*per_mm), GFP_KERNEL);
	if (!per_mm)
		return ERR_PTR(-ENOMEM);

	per_mm->umem_tree = RB_ROOT_CACHED;
	init_rwsem(&per_mm->umem_rwsem);

	WARN_ON(mm != current->mm);
	rcu_read_lock();
	per_mm->tgid = get_task_pid(current->group_leader, PIDTYPE_PID);
	rcu_read_unlock();
	return &per_mm->mn;
}

static void ib_umem_free_notifier(struct mmu_notifier *mn)
{
	struct ib_ucontext_per_mm *per_mm =
		container_of(mn, struct ib_ucontext_per_mm, mn);

	WARN_ON(!RB_EMPTY_ROOT(&per_mm->umem_tree.rb_root));

	put_pid(per_mm->tgid);
	kfree(per_mm);
}

static const struct mmu_notifier_ops ib_umem_notifiers = {
	.release                    = ib_umem_notifier_release,
	.invalidate_range_start     = ib_umem_notifier_invalidate_range_start,
	.invalidate_range_end       = ib_umem_notifier_invalidate_range_end,
	.alloc_notifier		    = ib_umem_alloc_notifier,
	.free_notifier		    = ib_umem_free_notifier,
};

static inline int ib_init_umem_odp(struct ib_umem_odp *umem_odp)
{
	struct ib_ucontext_per_mm *per_mm;
	struct mmu_notifier *mn;
	int ret;

	umem_odp->umem.is_odp = 1;
	if (!umem_odp->is_implicit_odp) {
		size_t page_size = 1UL << umem_odp->page_shift;
		size_t pages;

		umem_odp->interval_tree.start =
			ALIGN_DOWN(umem_odp->umem.address, page_size);
		if (check_add_overflow(umem_odp->umem.address,
				       (unsigned long)umem_odp->umem.length,
				       &umem_odp->interval_tree.last))
			return -EOVERFLOW;
		umem_odp->interval_tree.last =
			ALIGN(umem_odp->interval_tree.last, page_size);
		if (unlikely(umem_odp->interval_tree.last < page_size))
			return -EOVERFLOW;

		pages = (umem_odp->interval_tree.last -
			 umem_odp->interval_tree.start) >>
			umem_odp->page_shift;
		if (!pages)
			return -EINVAL;

		/*
		 * Note that the representation of the intervals in the
		 * interval tree considers the ending point as contained in
		 * the interval.
		 */
		umem_odp->interval_tree.last--;

		umem_odp->page_list = kvcalloc(
			pages, sizeof(*umem_odp->page_list), GFP_KERNEL);
		if (!umem_odp->page_list)
			return -ENOMEM;

		umem_odp->dma_list = kvcalloc(
			pages, sizeof(*umem_odp->dma_list), GFP_KERNEL);
		if (!umem_odp->dma_list) {
			ret = -ENOMEM;
			goto out_page_list;
		}
	}

	mn = mmu_notifier_get(&ib_umem_notifiers, umem_odp->umem.owning_mm);
	if (IS_ERR(mn)) {
		ret = PTR_ERR(mn);
		goto out_dma_list;
	}
	umem_odp->per_mm = per_mm =
		container_of(mn, struct ib_ucontext_per_mm, mn);

	mutex_init(&umem_odp->umem_mutex);
	init_completion(&umem_odp->notifier_completion);

	if (!umem_odp->is_implicit_odp) {
		down_write(&per_mm->umem_rwsem);
		interval_tree_insert(&umem_odp->interval_tree,
				     &per_mm->umem_tree);
		up_write(&per_mm->umem_rwsem);
	}
	mmgrab(umem_odp->umem.owning_mm);

	return 0;

out_dma_list:
	kvfree(umem_odp->dma_list);
out_page_list:
	kvfree(umem_odp->page_list);
	return ret;
}

/**
 * ib_umem_odp_alloc_implicit - Allocate a parent implicit ODP umem
 *
 * Implicit ODP umems do not have a VA range and do not have any page lists.
 * They exist only to hold the per_mm reference to help the driver create
 * children umems.
 *
 * @udata: udata from the syscall being used to create the umem
 * @access: ib_reg_mr access flags
 */
struct ib_umem_odp *ib_umem_odp_alloc_implicit(struct ib_udata *udata,
					       int access)
{
	struct ib_ucontext *context =
		container_of(udata, struct uverbs_attr_bundle, driver_udata)
			->context;
	struct ib_umem *umem;
	struct ib_umem_odp *umem_odp;
	int ret;

	if (access & IB_ACCESS_HUGETLB)
		return ERR_PTR(-EINVAL);

	if (!context)
		return ERR_PTR(-EIO);
	if (WARN_ON_ONCE(!context->device->ops.invalidate_range))
		return ERR_PTR(-EINVAL);

	umem_odp = kzalloc(sizeof(*umem_odp), GFP_KERNEL);
	if (!umem_odp)
		return ERR_PTR(-ENOMEM);
	umem = &umem_odp->umem;
	umem->ibdev = context->device;
	umem->writable = ib_access_writable(access);
	umem->owning_mm = current->mm;
	umem_odp->is_implicit_odp = 1;
	umem_odp->page_shift = PAGE_SHIFT;

	ret = ib_init_umem_odp(umem_odp);
	if (ret) {
		kfree(umem_odp);
		return ERR_PTR(ret);
	}
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
 */
struct ib_umem_odp *ib_umem_odp_alloc_child(struct ib_umem_odp *root,
					    unsigned long addr, size_t size)
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

	ret = ib_init_umem_odp(odp_data);
	if (ret) {
		kfree(odp_data);
		return ERR_PTR(ret);
	}
	return odp_data;
}
EXPORT_SYMBOL(ib_umem_odp_alloc_child);

/**
 * ib_umem_odp_get - Create a umem_odp for a userspace va
 *
 * @udata: userspace context to pin memory for
 * @addr: userspace virtual address to start at
 * @size: length of region to pin
 * @access: IB_ACCESS_xxx flags for memory being pinned
 *
 * The driver should use when the access flags indicate ODP memory. It avoids
 * pinning, instead, stores the mm for future page fault handling in
 * conjunction with MMU notifiers.
 */
struct ib_umem_odp *ib_umem_odp_get(struct ib_udata *udata, unsigned long addr,
				    size_t size, int access)
{
	struct ib_umem_odp *umem_odp;
	struct ib_ucontext *context;
	struct mm_struct *mm;
	int ret;

	if (!udata)
		return ERR_PTR(-EIO);

	context = container_of(udata, struct uverbs_attr_bundle, driver_udata)
			  ->context;
	if (!context)
		return ERR_PTR(-EIO);

	if (WARN_ON_ONCE(!(access & IB_ACCESS_ON_DEMAND)) ||
	    WARN_ON_ONCE(!context->device->ops.invalidate_range))
		return ERR_PTR(-EINVAL);

	umem_odp = kzalloc(sizeof(struct ib_umem_odp), GFP_KERNEL);
	if (!umem_odp)
		return ERR_PTR(-ENOMEM);

	umem_odp->umem.ibdev = context->device;
	umem_odp->umem.length = size;
	umem_odp->umem.address = addr;
	umem_odp->umem.writable = ib_access_writable(access);
	umem_odp->umem.owning_mm = mm = current->mm;

	umem_odp->page_shift = PAGE_SHIFT;
	if (access & IB_ACCESS_HUGETLB) {
		struct vm_area_struct *vma;
		struct hstate *h;

		down_read(&mm->mmap_sem);
		vma = find_vma(mm, ib_umem_start(umem_odp));
		if (!vma || !is_vm_hugetlb_page(vma)) {
			up_read(&mm->mmap_sem);
			ret = -EINVAL;
			goto err_free;
		}
		h = hstate_vma(vma);
		umem_odp->page_shift = huge_page_shift(h);
		up_read(&mm->mmap_sem);
	}

	ret = ib_init_umem_odp(umem_odp);
	if (ret)
		goto err_free;
	return umem_odp;

err_free:
	kfree(umem_odp);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL(ib_umem_odp_get);

void ib_umem_odp_release(struct ib_umem_odp *umem_odp)
{
	struct ib_ucontext_per_mm *per_mm = umem_odp->per_mm;

	/*
	 * Ensure that no more pages are mapped in the umem.
	 *
	 * It is the driver's responsibility to ensure, before calling us,
	 * that the hardware will not attempt to access the MR any more.
	 */
	if (!umem_odp->is_implicit_odp) {
		mutex_lock(&umem_odp->umem_mutex);
		ib_umem_odp_unmap_dma_pages(umem_odp, ib_umem_start(umem_odp),
					    ib_umem_end(umem_odp));
		mutex_unlock(&umem_odp->umem_mutex);
		kvfree(umem_odp->dma_list);
		kvfree(umem_odp->page_list);
	}

	down_write(&per_mm->umem_rwsem);
	if (!umem_odp->is_implicit_odp) {
		interval_tree_remove(&umem_odp->interval_tree,
				     &per_mm->umem_tree);
		complete_all(&umem_odp->notifier_completion);
	}
	/*
	 * NOTE! mmu_notifier_unregister() can happen between a start/end
	 * callback, resulting in a missing end, and thus an unbalanced
	 * lock. This doesn't really matter to us since we are about to kfree
	 * the memory that holds the lock, however LOCKDEP doesn't like this.
	 * Thus we call the mmu_notifier_put under the rwsem and test the
	 * internal users count to reliably see if we are past this point.
	 */
	mmu_notifier_put(&per_mm->mn);
	up_write(&per_mm->umem_rwsem);

	mmdrop(umem_odp->umem.owning_mm);
	kfree(umem_odp);
}
EXPORT_SYMBOL(ib_umem_odp_release);

/*
 * Map for DMA and insert a single page into the on-demand paging page tables.
 *
 * @umem: the umem to insert the page to.
 * @page_index: index in the umem to add the page to.
 * @page: the page struct to map and add.
 * @access_mask: access permissions needed for this page.
 * @current_seq: sequence number for synchronization with invalidations.
 *               the sequence number is taken from
 *               umem_odp->notifiers_seq.
 *
 * The function returns -EFAULT if the DMA mapping operation fails. It returns
 * -EAGAIN if a concurrent invalidation prevents us from updating the page.
 *
 * The page is released via put_user_page even if the operation failed. For
 * on-demand pinning, the page is released whenever it isn't stored in the
 * umem.
 */
static int ib_umem_odp_map_dma_single_page(
		struct ib_umem_odp *umem_odp,
		int page_index,
		struct page *page,
		u64 access_mask,
		unsigned long current_seq)
{
	struct ib_device *dev = umem_odp->umem.ibdev;
	dma_addr_t dma_addr;
	int remove_existing_mapping = 0;
	int ret = 0;

	/*
	 * Note: we avoid writing if seq is different from the initial seq, to
	 * handle case of a racing notifier. This check also allows us to bail
	 * early if we have a notifier running in parallel with us.
	 */
	if (ib_umem_mmu_notifier_retry(umem_odp, current_seq)) {
		ret = -EAGAIN;
		goto out;
	}
	if (!(umem_odp->dma_list[page_index])) {
		dma_addr =
			ib_dma_map_page(dev, page, 0, BIT(umem_odp->page_shift),
					DMA_BIDIRECTIONAL);
		if (ib_dma_mapping_error(dev, dma_addr)) {
			ret = -EFAULT;
			goto out;
		}
		umem_odp->dma_list[page_index] = dma_addr | access_mask;
		umem_odp->page_list[page_index] = page;
		umem_odp->npages++;
	} else if (umem_odp->page_list[page_index] == page) {
		umem_odp->dma_list[page_index] |= access_mask;
	} else {
		pr_err("error: got different pages in IB device and from get_user_pages. IB device page: %p, gup page: %p\n",
		       umem_odp->page_list[page_index], page);
		/* Better remove the mapping now, to prevent any further
		 * damage. */
		remove_existing_mapping = 1;
	}

out:
	put_user_page(page);

	if (remove_existing_mapping) {
		ib_umem_notifier_start_account(umem_odp);
		dev->ops.invalidate_range(
			umem_odp,
			ib_umem_start(umem_odp) +
				(page_index << umem_odp->page_shift),
			ib_umem_start(umem_odp) +
				((page_index + 1) << umem_odp->page_shift));
		ib_umem_notifier_end_account(umem_odp);
		ret = -EAGAIN;
	}

	return ret;
}

/**
 * ib_umem_odp_map_dma_pages - Pin and DMA map userspace memory in an ODP MR.
 *
 * Pins the range of pages passed in the argument, and maps them to
 * DMA addresses. The DMA addresses of the mapped pages is updated in
 * umem_odp->dma_list.
 *
 * Returns the number of pages mapped in success, negative error code
 * for failure.
 * An -EAGAIN error code is returned when a concurrent mmu notifier prevents
 * the function from completing its task.
 * An -ENOENT error code indicates that userspace process is being terminated
 * and mm was already destroyed.
 * @umem_odp: the umem to map and pin
 * @user_virt: the address from which we need to map.
 * @bcnt: the minimal number of bytes to pin and map. The mapping might be
 *        bigger due to alignment, and may also be smaller in case of an error
 *        pinning or mapping a page. The actual pages mapped is returned in
 *        the return value.
 * @access_mask: bit mask of the requested access permissions for the given
 *               range.
 * @current_seq: the MMU notifiers sequance value for synchronization with
 *               invalidations. the sequance number is read from
 *               umem_odp->notifiers_seq before calling this function
 */
int ib_umem_odp_map_dma_pages(struct ib_umem_odp *umem_odp, u64 user_virt,
			      u64 bcnt, u64 access_mask,
			      unsigned long current_seq)
{
	struct task_struct *owning_process  = NULL;
	struct mm_struct *owning_mm = umem_odp->umem.owning_mm;
	struct page       **local_page_list = NULL;
	u64 page_mask, off;
	int j, k, ret = 0, start_idx, npages = 0;
	unsigned int flags = 0, page_shift;
	phys_addr_t p = 0;

	if (access_mask == 0)
		return -EINVAL;

	if (user_virt < ib_umem_start(umem_odp) ||
	    user_virt + bcnt > ib_umem_end(umem_odp))
		return -EFAULT;

	local_page_list = (struct page **)__get_free_page(GFP_KERNEL);
	if (!local_page_list)
		return -ENOMEM;

	page_shift = umem_odp->page_shift;
	page_mask = ~(BIT(page_shift) - 1);
	off = user_virt & (~page_mask);
	user_virt = user_virt & page_mask;
	bcnt += off; /* Charge for the first page offset as well. */

	/*
	 * owning_process is allowed to be NULL, this means somehow the mm is
	 * existing beyond the lifetime of the originating process.. Presumably
	 * mmget_not_zero will fail in this case.
	 */
	owning_process = get_pid_task(umem_odp->per_mm->tgid, PIDTYPE_PID);
	if (!owning_process || !mmget_not_zero(owning_mm)) {
		ret = -EINVAL;
		goto out_put_task;
	}

	if (access_mask & ODP_WRITE_ALLOWED_BIT)
		flags |= FOLL_WRITE;

	start_idx = (user_virt - ib_umem_start(umem_odp)) >> page_shift;
	k = start_idx;

	while (bcnt > 0) {
		const size_t gup_num_pages = min_t(size_t,
				(bcnt + BIT(page_shift) - 1) >> page_shift,
				PAGE_SIZE / sizeof(struct page *));

		down_read(&owning_mm->mmap_sem);
		/*
		 * Note: this might result in redundent page getting. We can
		 * avoid this by checking dma_list to be 0 before calling
		 * get_user_pages. However, this make the code much more
		 * complex (and doesn't gain us much performance in most use
		 * cases).
		 */
		npages = get_user_pages_remote(owning_process, owning_mm,
				user_virt, gup_num_pages,
				flags, local_page_list, NULL, NULL);
		up_read(&owning_mm->mmap_sem);

		if (npages < 0) {
			if (npages != -EAGAIN)
				pr_warn("fail to get %zu user pages with error %d\n", gup_num_pages, npages);
			else
				pr_debug("fail to get %zu user pages with error %d\n", gup_num_pages, npages);
			break;
		}

		bcnt -= min_t(size_t, npages << PAGE_SHIFT, bcnt);
		mutex_lock(&umem_odp->umem_mutex);
		for (j = 0; j < npages; j++, user_virt += PAGE_SIZE) {
			if (user_virt & ~page_mask) {
				p += PAGE_SIZE;
				if (page_to_phys(local_page_list[j]) != p) {
					ret = -EFAULT;
					break;
				}
				put_user_page(local_page_list[j]);
				continue;
			}

			ret = ib_umem_odp_map_dma_single_page(
					umem_odp, k, local_page_list[j],
					access_mask, current_seq);
			if (ret < 0) {
				if (ret != -EAGAIN)
					pr_warn("ib_umem_odp_map_dma_single_page failed with error %d\n", ret);
				else
					pr_debug("ib_umem_odp_map_dma_single_page failed with error %d\n", ret);
				break;
			}

			p = page_to_phys(local_page_list[j]);
			k++;
		}
		mutex_unlock(&umem_odp->umem_mutex);

		if (ret < 0) {
			/*
			 * Release pages, remembering that the first page
			 * to hit an error was already released by
			 * ib_umem_odp_map_dma_single_page().
			 */
			if (npages - (j + 1) > 0)
				put_user_pages(&local_page_list[j+1],
					       npages - (j + 1));
			break;
		}
	}

	if (ret >= 0) {
		if (npages < 0 && k == start_idx)
			ret = npages;
		else
			ret = k - start_idx;
	}

	mmput(owning_mm);
out_put_task:
	if (owning_process)
		put_task_struct(owning_process);
	free_page((unsigned long)local_page_list);
	return ret;
}
EXPORT_SYMBOL(ib_umem_odp_map_dma_pages);

void ib_umem_odp_unmap_dma_pages(struct ib_umem_odp *umem_odp, u64 virt,
				 u64 bound)
{
	int idx;
	u64 addr;
	struct ib_device *dev = umem_odp->umem.ibdev;

	lockdep_assert_held(&umem_odp->umem_mutex);

	virt = max_t(u64, virt, ib_umem_start(umem_odp));
	bound = min_t(u64, bound, ib_umem_end(umem_odp));
	/* Note that during the run of this function, the
	 * notifiers_count of the MR is > 0, preventing any racing
	 * faults from completion. We might be racing with other
	 * invalidations, so we must make sure we free each page only
	 * once. */
	for (addr = virt; addr < bound; addr += BIT(umem_odp->page_shift)) {
		idx = (addr - ib_umem_start(umem_odp)) >> umem_odp->page_shift;
		if (umem_odp->page_list[idx]) {
			struct page *page = umem_odp->page_list[idx];
			dma_addr_t dma = umem_odp->dma_list[idx];
			dma_addr_t dma_addr = dma & ODP_DMA_ADDR_MASK;

			WARN_ON(!dma_addr);

			ib_dma_unmap_page(dev, dma_addr,
					  BIT(umem_odp->page_shift),
					  DMA_BIDIRECTIONAL);
			if (dma & ODP_WRITE_ALLOWED_BIT) {
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
			umem_odp->page_list[idx] = NULL;
			umem_odp->dma_list[idx] = 0;
			umem_odp->npages--;
		}
	}
}
EXPORT_SYMBOL(ib_umem_odp_unmap_dma_pages);

/* @last is not a part of the interval. See comment for function
 * node_last.
 */
int rbt_ib_umem_for_each_in_range(struct rb_root_cached *root,
				  u64 start, u64 last,
				  umem_call_back cb,
				  bool blockable,
				  void *cookie)
{
	int ret_val = 0;
	struct interval_tree_node *node, *next;
	struct ib_umem_odp *umem;

	if (unlikely(start == last))
		return ret_val;

	for (node = interval_tree_iter_first(root, start, last - 1);
			node; node = next) {
		/* TODO move the blockable decision up to the callback */
		if (!blockable)
			return -EAGAIN;
		next = interval_tree_iter_next(node, start, last - 1);
		umem = container_of(node, struct ib_umem_odp, interval_tree);
		ret_val = cb(umem, start, last, cookie) || ret_val;
	}

	return ret_val;
}
