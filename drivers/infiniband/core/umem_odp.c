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
#include <linux/interval_tree_generic.h>

#include <rdma/ib_verbs.h>
#include <rdma/ib_umem.h>
#include <rdma/ib_umem_odp.h>

/*
 * The ib_umem list keeps track of memory regions for which the HW
 * device request to receive notification when the related memory
 * mapping is changed.
 *
 * ib_umem_lock protects the list.
 */

static u64 node_start(struct umem_odp_node *n)
{
	struct ib_umem_odp *umem_odp =
			container_of(n, struct ib_umem_odp, interval_tree);

	return ib_umem_start(&umem_odp->umem);
}

/* Note that the representation of the intervals in the interval tree
 * considers the ending point as contained in the interval, while the
 * function ib_umem_end returns the first address which is not contained
 * in the umem.
 */
static u64 node_last(struct umem_odp_node *n)
{
	struct ib_umem_odp *umem_odp =
			container_of(n, struct ib_umem_odp, interval_tree);

	return ib_umem_end(&umem_odp->umem) - 1;
}

INTERVAL_TREE_DEFINE(struct umem_odp_node, rb, u64, __subtree_last,
		     node_start, node_last, static, rbt_ib_umem)

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

static int ib_umem_notifier_release_trampoline(struct ib_umem_odp *umem_odp,
					       u64 start, u64 end, void *cookie)
{
	struct ib_umem *umem = &umem_odp->umem;

	/*
	 * Increase the number of notifiers running, to
	 * prevent any further fault handling on this MR.
	 */
	ib_umem_notifier_start_account(umem_odp);
	umem_odp->dying = 1;
	/* Make sure that the fact the umem is dying is out before we release
	 * all pending page faults. */
	smp_wmb();
	complete_all(&umem_odp->notifier_completion);
	umem->context->invalidate_range(umem_odp, ib_umem_start(umem),
					ib_umem_end(umem));
	return 0;
}

static void ib_umem_notifier_release(struct mmu_notifier *mn,
				     struct mm_struct *mm)
{
	struct ib_ucontext_per_mm *per_mm =
		container_of(mn, struct ib_ucontext_per_mm, mn);

	down_read(&per_mm->umem_rwsem);
	if (per_mm->active)
		rbt_ib_umem_for_each_in_range(
			&per_mm->umem_tree, 0, ULLONG_MAX,
			ib_umem_notifier_release_trampoline, true, NULL);
	up_read(&per_mm->umem_rwsem);
}

static int invalidate_range_start_trampoline(struct ib_umem_odp *item,
					     u64 start, u64 end, void *cookie)
{
	ib_umem_notifier_start_account(item);
	item->umem.context->invalidate_range(item, start, end);
	return 0;
}

static int ib_umem_notifier_invalidate_range_start(struct mmu_notifier *mn,
						    struct mm_struct *mm,
						    unsigned long start,
						    unsigned long end,
						    bool blockable)
{
	struct ib_ucontext_per_mm *per_mm =
		container_of(mn, struct ib_ucontext_per_mm, mn);

	if (blockable)
		down_read(&per_mm->umem_rwsem);
	else if (!down_read_trylock(&per_mm->umem_rwsem))
		return -EAGAIN;

	if (!per_mm->active) {
		up_read(&per_mm->umem_rwsem);
		/*
		 * At this point active is permanently set and visible to this
		 * CPU without a lock, that fact is relied on to skip the unlock
		 * in range_end.
		 */
		return 0;
	}

	return rbt_ib_umem_for_each_in_range(&per_mm->umem_tree, start, end,
					     invalidate_range_start_trampoline,
					     blockable, NULL);
}

static int invalidate_range_end_trampoline(struct ib_umem_odp *item, u64 start,
					   u64 end, void *cookie)
{
	ib_umem_notifier_end_account(item);
	return 0;
}

static void ib_umem_notifier_invalidate_range_end(struct mmu_notifier *mn,
						  struct mm_struct *mm,
						  unsigned long start,
						  unsigned long end)
{
	struct ib_ucontext_per_mm *per_mm =
		container_of(mn, struct ib_ucontext_per_mm, mn);

	if (unlikely(!per_mm->active))
		return;

	rbt_ib_umem_for_each_in_range(&per_mm->umem_tree, start,
				      end,
				      invalidate_range_end_trampoline, true, NULL);
	up_read(&per_mm->umem_rwsem);
}

static const struct mmu_notifier_ops ib_umem_notifiers = {
	.release                    = ib_umem_notifier_release,
	.invalidate_range_start     = ib_umem_notifier_invalidate_range_start,
	.invalidate_range_end       = ib_umem_notifier_invalidate_range_end,
};

static void add_umem_to_per_mm(struct ib_umem_odp *umem_odp)
{
	struct ib_ucontext_per_mm *per_mm = umem_odp->per_mm;
	struct ib_umem *umem = &umem_odp->umem;

	down_write(&per_mm->umem_rwsem);
	if (likely(ib_umem_start(umem) != ib_umem_end(umem)))
		rbt_ib_umem_insert(&umem_odp->interval_tree,
				   &per_mm->umem_tree);
	up_write(&per_mm->umem_rwsem);
}

static void remove_umem_from_per_mm(struct ib_umem_odp *umem_odp)
{
	struct ib_ucontext_per_mm *per_mm = umem_odp->per_mm;
	struct ib_umem *umem = &umem_odp->umem;

	down_write(&per_mm->umem_rwsem);
	if (likely(ib_umem_start(umem) != ib_umem_end(umem)))
		rbt_ib_umem_remove(&umem_odp->interval_tree,
				   &per_mm->umem_tree);
	complete_all(&umem_odp->notifier_completion);

	up_write(&per_mm->umem_rwsem);
}

static struct ib_ucontext_per_mm *alloc_per_mm(struct ib_ucontext *ctx,
					       struct mm_struct *mm)
{
	struct ib_ucontext_per_mm *per_mm;
	int ret;

	per_mm = kzalloc(sizeof(*per_mm), GFP_KERNEL);
	if (!per_mm)
		return ERR_PTR(-ENOMEM);

	per_mm->context = ctx;
	per_mm->mm = mm;
	per_mm->umem_tree = RB_ROOT_CACHED;
	init_rwsem(&per_mm->umem_rwsem);
	per_mm->active = ctx->invalidate_range;

	rcu_read_lock();
	per_mm->tgid = get_task_pid(current->group_leader, PIDTYPE_PID);
	rcu_read_unlock();

	WARN_ON(mm != current->mm);

	per_mm->mn.ops = &ib_umem_notifiers;
	ret = mmu_notifier_register(&per_mm->mn, per_mm->mm);
	if (ret) {
		dev_err(&ctx->device->dev,
			"Failed to register mmu_notifier %d\n", ret);
		goto out_pid;
	}

	list_add(&per_mm->ucontext_list, &ctx->per_mm_list);
	return per_mm;

out_pid:
	put_pid(per_mm->tgid);
	kfree(per_mm);
	return ERR_PTR(ret);
}

static int get_per_mm(struct ib_umem_odp *umem_odp)
{
	struct ib_ucontext *ctx = umem_odp->umem.context;
	struct ib_ucontext_per_mm *per_mm;

	/*
	 * Generally speaking we expect only one or two per_mm in this list,
	 * so no reason to optimize this search today.
	 */
	mutex_lock(&ctx->per_mm_list_lock);
	list_for_each_entry(per_mm, &ctx->per_mm_list, ucontext_list) {
		if (per_mm->mm == umem_odp->umem.owning_mm)
			goto found;
	}

	per_mm = alloc_per_mm(ctx, umem_odp->umem.owning_mm);
	if (IS_ERR(per_mm)) {
		mutex_unlock(&ctx->per_mm_list_lock);
		return PTR_ERR(per_mm);
	}

found:
	umem_odp->per_mm = per_mm;
	per_mm->odp_mrs_count++;
	mutex_unlock(&ctx->per_mm_list_lock);

	return 0;
}

static void free_per_mm(struct rcu_head *rcu)
{
	kfree(container_of(rcu, struct ib_ucontext_per_mm, rcu));
}

void put_per_mm(struct ib_umem_odp *umem_odp)
{
	struct ib_ucontext_per_mm *per_mm = umem_odp->per_mm;
	struct ib_ucontext *ctx = umem_odp->umem.context;
	bool need_free;

	mutex_lock(&ctx->per_mm_list_lock);
	umem_odp->per_mm = NULL;
	per_mm->odp_mrs_count--;
	need_free = per_mm->odp_mrs_count == 0;
	if (need_free)
		list_del(&per_mm->ucontext_list);
	mutex_unlock(&ctx->per_mm_list_lock);

	if (!need_free)
		return;

	/*
	 * NOTE! mmu_notifier_unregister() can happen between a start/end
	 * callback, resulting in an start/end, and thus an unbalanced
	 * lock. This doesn't really matter to us since we are about to kfree
	 * the memory that holds the lock, however LOCKDEP doesn't like this.
	 */
	down_write(&per_mm->umem_rwsem);
	per_mm->active = false;
	up_write(&per_mm->umem_rwsem);

	WARN_ON(!RB_EMPTY_ROOT(&per_mm->umem_tree.rb_root));
	mmu_notifier_unregister_no_release(&per_mm->mn, per_mm->mm);
	put_pid(per_mm->tgid);
	mmu_notifier_call_srcu(&per_mm->rcu, free_per_mm);
}

struct ib_umem_odp *ib_alloc_odp_umem(struct ib_ucontext_per_mm *per_mm,
				      unsigned long addr, size_t size)
{
	struct ib_ucontext *ctx = per_mm->context;
	struct ib_umem_odp *odp_data;
	struct ib_umem *umem;
	int pages = size >> PAGE_SHIFT;
	int ret;

	odp_data = kzalloc(sizeof(*odp_data), GFP_KERNEL);
	if (!odp_data)
		return ERR_PTR(-ENOMEM);
	umem = &odp_data->umem;
	umem->context    = ctx;
	umem->length     = size;
	umem->address    = addr;
	umem->page_shift = PAGE_SHIFT;
	umem->writable   = 1;
	umem->is_odp = 1;
	odp_data->per_mm = per_mm;

	mutex_init(&odp_data->umem_mutex);
	init_completion(&odp_data->notifier_completion);

	odp_data->page_list =
		vzalloc(array_size(pages, sizeof(*odp_data->page_list)));
	if (!odp_data->page_list) {
		ret = -ENOMEM;
		goto out_odp_data;
	}

	odp_data->dma_list =
		vzalloc(array_size(pages, sizeof(*odp_data->dma_list)));
	if (!odp_data->dma_list) {
		ret = -ENOMEM;
		goto out_page_list;
	}

	/*
	 * Caller must ensure that the umem_odp that the per_mm came from
	 * cannot be freed during the call to ib_alloc_odp_umem.
	 */
	mutex_lock(&ctx->per_mm_list_lock);
	per_mm->odp_mrs_count++;
	mutex_unlock(&ctx->per_mm_list_lock);
	add_umem_to_per_mm(odp_data);

	return odp_data;

out_page_list:
	vfree(odp_data->page_list);
out_odp_data:
	kfree(odp_data);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL(ib_alloc_odp_umem);

int ib_umem_odp_get(struct ib_umem_odp *umem_odp, int access)
{
	struct ib_umem *umem = &umem_odp->umem;
	/*
	 * NOTE: This must called in a process context where umem->owning_mm
	 * == current->mm
	 */
	struct mm_struct *mm = umem->owning_mm;
	int ret_val;

	if (access & IB_ACCESS_HUGETLB) {
		struct vm_area_struct *vma;
		struct hstate *h;

		down_read(&mm->mmap_sem);
		vma = find_vma(mm, ib_umem_start(umem));
		if (!vma || !is_vm_hugetlb_page(vma)) {
			up_read(&mm->mmap_sem);
			return -EINVAL;
		}
		h = hstate_vma(vma);
		umem->page_shift = huge_page_shift(h);
		up_read(&mm->mmap_sem);
		umem->hugetlb = 1;
	} else {
		umem->hugetlb = 0;
	}

	mutex_init(&umem_odp->umem_mutex);

	init_completion(&umem_odp->notifier_completion);

	if (ib_umem_num_pages(umem)) {
		umem_odp->page_list =
			vzalloc(array_size(sizeof(*umem_odp->page_list),
					   ib_umem_num_pages(umem)));
		if (!umem_odp->page_list)
			return -ENOMEM;

		umem_odp->dma_list =
			vzalloc(array_size(sizeof(*umem_odp->dma_list),
					   ib_umem_num_pages(umem)));
		if (!umem_odp->dma_list) {
			ret_val = -ENOMEM;
			goto out_page_list;
		}
	}

	ret_val = get_per_mm(umem_odp);
	if (ret_val)
		goto out_dma_list;
	add_umem_to_per_mm(umem_odp);

	return 0;

out_dma_list:
	vfree(umem_odp->dma_list);
out_page_list:
	vfree(umem_odp->page_list);
	return ret_val;
}

void ib_umem_odp_release(struct ib_umem_odp *umem_odp)
{
	struct ib_umem *umem = &umem_odp->umem;

	/*
	 * Ensure that no more pages are mapped in the umem.
	 *
	 * It is the driver's responsibility to ensure, before calling us,
	 * that the hardware will not attempt to access the MR any more.
	 */
	ib_umem_odp_unmap_dma_pages(umem_odp, ib_umem_start(umem),
				    ib_umem_end(umem));

	remove_umem_from_per_mm(umem_odp);
	put_per_mm(umem_odp);
	vfree(umem_odp->dma_list);
	vfree(umem_odp->page_list);
}

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
 * The page is released via put_page even if the operation failed. For
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
	struct ib_umem *umem = &umem_odp->umem;
	struct ib_device *dev = umem->context->device;
	dma_addr_t dma_addr;
	int stored_page = 0;
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
		dma_addr = ib_dma_map_page(dev,
					   page,
					   0, BIT(umem->page_shift),
					   DMA_BIDIRECTIONAL);
		if (ib_dma_mapping_error(dev, dma_addr)) {
			ret = -EFAULT;
			goto out;
		}
		umem_odp->dma_list[page_index] = dma_addr | access_mask;
		umem_odp->page_list[page_index] = page;
		umem->npages++;
		stored_page = 1;
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
	/* On Demand Paging - avoid pinning the page */
	if (umem->context->invalidate_range || !stored_page)
		put_page(page);

	if (remove_existing_mapping && umem->context->invalidate_range) {
		ib_umem_notifier_start_account(umem_odp);
		umem->context->invalidate_range(
			umem_odp,
			ib_umem_start(umem) + (page_index << umem->page_shift),
			ib_umem_start(umem) +
				((page_index + 1) << umem->page_shift));
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
	struct ib_umem *umem = &umem_odp->umem;
	struct task_struct *owning_process  = NULL;
	struct mm_struct *owning_mm = umem_odp->umem.owning_mm;
	struct page       **local_page_list = NULL;
	u64 page_mask, off;
	int j, k, ret = 0, start_idx, npages = 0, page_shift;
	unsigned int flags = 0;
	phys_addr_t p = 0;

	if (access_mask == 0)
		return -EINVAL;

	if (user_virt < ib_umem_start(umem) ||
	    user_virt + bcnt > ib_umem_end(umem))
		return -EFAULT;

	local_page_list = (struct page **)__get_free_page(GFP_KERNEL);
	if (!local_page_list)
		return -ENOMEM;

	page_shift = umem->page_shift;
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
	if (WARN_ON(!mmget_not_zero(umem_odp->umem.owning_mm))) {
		ret = -EINVAL;
		goto out_put_task;
	}

	if (access_mask & ODP_WRITE_ALLOWED_BIT)
		flags |= FOLL_WRITE;

	start_idx = (user_virt - ib_umem_start(umem)) >> page_shift;
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

		if (npages < 0)
			break;

		bcnt -= min_t(size_t, npages << PAGE_SHIFT, bcnt);
		mutex_lock(&umem_odp->umem_mutex);
		for (j = 0; j < npages; j++, user_virt += PAGE_SIZE) {
			if (user_virt & ~page_mask) {
				p += PAGE_SIZE;
				if (page_to_phys(local_page_list[j]) != p) {
					ret = -EFAULT;
					break;
				}
				put_page(local_page_list[j]);
				continue;
			}

			ret = ib_umem_odp_map_dma_single_page(
					umem_odp, k, local_page_list[j],
					access_mask, current_seq);
			if (ret < 0)
				break;

			p = page_to_phys(local_page_list[j]);
			k++;
		}
		mutex_unlock(&umem_odp->umem_mutex);

		if (ret < 0) {
			/* Release left over pages when handling errors. */
			for (++j; j < npages; ++j)
				put_page(local_page_list[j]);
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
	struct ib_umem *umem = &umem_odp->umem;
	int idx;
	u64 addr;
	struct ib_device *dev = umem->context->device;

	virt  = max_t(u64, virt,  ib_umem_start(umem));
	bound = min_t(u64, bound, ib_umem_end(umem));
	/* Note that during the run of this function, the
	 * notifiers_count of the MR is > 0, preventing any racing
	 * faults from completion. We might be racing with other
	 * invalidations, so we must make sure we free each page only
	 * once. */
	mutex_lock(&umem_odp->umem_mutex);
	for (addr = virt; addr < bound; addr += BIT(umem->page_shift)) {
		idx = (addr - ib_umem_start(umem)) >> umem->page_shift;
		if (umem_odp->page_list[idx]) {
			struct page *page = umem_odp->page_list[idx];
			dma_addr_t dma = umem_odp->dma_list[idx];
			dma_addr_t dma_addr = dma & ODP_DMA_ADDR_MASK;

			WARN_ON(!dma_addr);

			ib_dma_unmap_page(dev, dma_addr, PAGE_SIZE,
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
			/* on demand pinning support */
			if (!umem->context->invalidate_range)
				put_page(page);
			umem_odp->page_list[idx] = NULL;
			umem_odp->dma_list[idx] = 0;
			umem->npages--;
		}
	}
	mutex_unlock(&umem_odp->umem_mutex);
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
	struct umem_odp_node *node, *next;
	struct ib_umem_odp *umem;

	if (unlikely(start == last))
		return ret_val;

	for (node = rbt_ib_umem_iter_first(root, start, last - 1);
			node; node = next) {
		/* TODO move the blockable decision up to the callback */
		if (!blockable)
			return -EAGAIN;
		next = rbt_ib_umem_iter_next(node, start, last - 1);
		umem = container_of(node, struct ib_umem_odp, interval_tree);
		ret_val = cb(umem, start, last, cookie) || ret_val;
	}

	return ret_val;
}
EXPORT_SYMBOL(rbt_ib_umem_for_each_in_range);

struct ib_umem_odp *rbt_ib_umem_lookup(struct rb_root_cached *root,
				       u64 addr, u64 length)
{
	struct umem_odp_node *node;

	node = rbt_ib_umem_iter_first(root, addr, addr + length - 1);
	if (node)
		return container_of(node, struct ib_umem_odp, interval_tree);
	return NULL;

}
EXPORT_SYMBOL(rbt_ib_umem_lookup);
