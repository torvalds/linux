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

static inline int ib_init_umem_odp(struct ib_umem_odp *umem_odp,
				   const struct mmu_interval_notifier_ops *ops)
{
	int ret;

	umem_odp->umem.is_odp = 1;
	mutex_init(&umem_odp->umem_mutex);

	if (!umem_odp->is_implicit_odp) {
		size_t page_size = 1UL << umem_odp->page_shift;
		unsigned long start;
		unsigned long end;
		size_t pages;

		start = ALIGN_DOWN(umem_odp->umem.address, page_size);
		if (check_add_overflow(umem_odp->umem.address,
				       (unsigned long)umem_odp->umem.length,
				       &end))
			return -EOVERFLOW;
		end = ALIGN(end, page_size);
		if (unlikely(end < page_size))
			return -EOVERFLOW;

		pages = (end - start) >> umem_odp->page_shift;
		if (!pages)
			return -EINVAL;

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

		ret = mmu_interval_notifier_insert(&umem_odp->notifier,
						   umem_odp->umem.owning_mm,
						   start, end - start, ops);
		if (ret)
			goto out_dma_list;
	}

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
 * @device: IB device to create UMEM
 * @access: ib_reg_mr access flags
 */
struct ib_umem_odp *ib_umem_odp_alloc_implicit(struct ib_device *device,
					       int access)
{
	struct ib_umem *umem;
	struct ib_umem_odp *umem_odp;
	int ret;

	if (access & IB_ACCESS_HUGETLB)
		return ERR_PTR(-EINVAL);

	umem_odp = kzalloc(sizeof(*umem_odp), GFP_KERNEL);
	if (!umem_odp)
		return ERR_PTR(-ENOMEM);
	umem = &umem_odp->umem;
	umem->ibdev = device;
	umem->writable = ib_access_writable(access);
	umem->owning_mm = current->mm;
	umem_odp->is_implicit_odp = 1;
	umem_odp->page_shift = PAGE_SHIFT;

	umem_odp->tgid = get_task_pid(current->group_leader, PIDTYPE_PID);
	ret = ib_init_umem_odp(umem_odp, NULL);
	if (ret) {
		put_pid(umem_odp->tgid);
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
	struct mm_struct *mm;
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
	umem_odp->umem.owning_mm = mm = current->mm;
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

void ib_umem_odp_release(struct ib_umem_odp *umem_odp)
{
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
		mmu_interval_notifier_remove(&umem_odp->notifier);
		kvfree(umem_odp->dma_list);
		kvfree(umem_odp->page_list);
	}
	put_pid(umem_odp->tgid);
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
 * The page is released via put_page even if the operation failed. For on-demand
 * pinning, the page is released whenever it isn't stored in the umem.
 */
static int ib_umem_odp_map_dma_single_page(
		struct ib_umem_odp *umem_odp,
		unsigned int page_index,
		struct page *page,
		u64 access_mask,
		unsigned long current_seq)
{
	struct ib_device *dev = umem_odp->umem.ibdev;
	dma_addr_t dma_addr;
	int ret = 0;

	if (mmu_interval_check_retry(&umem_odp->notifier, current_seq)) {
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
		/*
		 * This is a race here where we could have done:
		 *
		 *         CPU0                             CPU1
		 *   get_user_pages()
		 *                                       invalidate()
		 *                                       page_fault()
		 *   mutex_lock(umem_mutex)
		 *    page from GUP != page in ODP
		 *
		 * It should be prevented by the retry test above as reading
		 * the seq number should be reliable under the
		 * umem_mutex. Thus something is really not working right if
		 * things get here.
		 */
		WARN(true,
		     "Got different pages in IB device and from get_user_pages. IB device page: %p, gup page: %p\n",
		     umem_odp->page_list[page_index], page);
		ret = -EAGAIN;
	}

out:
	put_page(page);
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
	owning_process = get_pid_task(umem_odp->tgid, PIDTYPE_PID);
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
				ALIGN(bcnt, PAGE_SIZE) / PAGE_SIZE,
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
				put_page(local_page_list[j]);
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
				release_pages(&local_page_list[j+1],
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
