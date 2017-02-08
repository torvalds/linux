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
#include <linux/pid.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/vmalloc.h>

#include <rdma/ib_verbs.h>
#include <rdma/ib_umem.h>
#include <rdma/ib_umem_odp.h>

static void ib_umem_notifier_start_account(struct ib_umem *item)
{
	mutex_lock(&item->odp_data->umem_mutex);

	/* Only update private counters for this umem if it has them.
	 * Otherwise skip it. All page faults will be delayed for this umem. */
	if (item->odp_data->mn_counters_active) {
		int notifiers_count = item->odp_data->notifiers_count++;

		if (notifiers_count == 0)
			/* Initialize the completion object for waiting on
			 * notifiers. Since notifier_count is zero, no one
			 * should be waiting right now. */
			reinit_completion(&item->odp_data->notifier_completion);
	}
	mutex_unlock(&item->odp_data->umem_mutex);
}

static void ib_umem_notifier_end_account(struct ib_umem *item)
{
	mutex_lock(&item->odp_data->umem_mutex);

	/* Only update private counters for this umem if it has them.
	 * Otherwise skip it. All page faults will be delayed for this umem. */
	if (item->odp_data->mn_counters_active) {
		/*
		 * This sequence increase will notify the QP page fault that
		 * the page that is going to be mapped in the spte could have
		 * been freed.
		 */
		++item->odp_data->notifiers_seq;
		if (--item->odp_data->notifiers_count == 0)
			complete_all(&item->odp_data->notifier_completion);
	}
	mutex_unlock(&item->odp_data->umem_mutex);
}

/* Account for a new mmu notifier in an ib_ucontext. */
static void ib_ucontext_notifier_start_account(struct ib_ucontext *context)
{
	atomic_inc(&context->notifier_count);
}

/* Account for a terminating mmu notifier in an ib_ucontext.
 *
 * Must be called with the ib_ucontext->umem_rwsem semaphore unlocked, since
 * the function takes the semaphore itself. */
static void ib_ucontext_notifier_end_account(struct ib_ucontext *context)
{
	int zero_notifiers = atomic_dec_and_test(&context->notifier_count);

	if (zero_notifiers &&
	    !list_empty(&context->no_private_counters)) {
		/* No currently running mmu notifiers. Now is the chance to
		 * add private accounting to all previously added umems. */
		struct ib_umem_odp *odp_data, *next;

		/* Prevent concurrent mmu notifiers from working on the
		 * no_private_counters list. */
		down_write(&context->umem_rwsem);

		/* Read the notifier_count again, with the umem_rwsem
		 * semaphore taken for write. */
		if (!atomic_read(&context->notifier_count)) {
			list_for_each_entry_safe(odp_data, next,
						 &context->no_private_counters,
						 no_private_counters) {
				mutex_lock(&odp_data->umem_mutex);
				odp_data->mn_counters_active = true;
				list_del(&odp_data->no_private_counters);
				complete_all(&odp_data->notifier_completion);
				mutex_unlock(&odp_data->umem_mutex);
			}
		}

		up_write(&context->umem_rwsem);
	}
}

static int ib_umem_notifier_release_trampoline(struct ib_umem *item, u64 start,
					       u64 end, void *cookie) {
	/*
	 * Increase the number of notifiers running, to
	 * prevent any further fault handling on this MR.
	 */
	ib_umem_notifier_start_account(item);
	item->odp_data->dying = 1;
	/* Make sure that the fact the umem is dying is out before we release
	 * all pending page faults. */
	smp_wmb();
	complete_all(&item->odp_data->notifier_completion);
	item->context->invalidate_range(item, ib_umem_start(item),
					ib_umem_end(item));
	return 0;
}

static void ib_umem_notifier_release(struct mmu_notifier *mn,
				     struct mm_struct *mm)
{
	struct ib_ucontext *context = container_of(mn, struct ib_ucontext, mn);

	if (!context->invalidate_range)
		return;

	ib_ucontext_notifier_start_account(context);
	down_read(&context->umem_rwsem);
	rbt_ib_umem_for_each_in_range(&context->umem_tree, 0,
				      ULLONG_MAX,
				      ib_umem_notifier_release_trampoline,
				      NULL);
	up_read(&context->umem_rwsem);
}

static int invalidate_page_trampoline(struct ib_umem *item, u64 start,
				      u64 end, void *cookie)
{
	ib_umem_notifier_start_account(item);
	item->context->invalidate_range(item, start, start + PAGE_SIZE);
	ib_umem_notifier_end_account(item);
	return 0;
}

static void ib_umem_notifier_invalidate_page(struct mmu_notifier *mn,
					     struct mm_struct *mm,
					     unsigned long address)
{
	struct ib_ucontext *context = container_of(mn, struct ib_ucontext, mn);

	if (!context->invalidate_range)
		return;

	ib_ucontext_notifier_start_account(context);
	down_read(&context->umem_rwsem);
	rbt_ib_umem_for_each_in_range(&context->umem_tree, address,
				      address + PAGE_SIZE,
				      invalidate_page_trampoline, NULL);
	up_read(&context->umem_rwsem);
	ib_ucontext_notifier_end_account(context);
}

static int invalidate_range_start_trampoline(struct ib_umem *item, u64 start,
					     u64 end, void *cookie)
{
	ib_umem_notifier_start_account(item);
	item->context->invalidate_range(item, start, end);
	return 0;
}

static void ib_umem_notifier_invalidate_range_start(struct mmu_notifier *mn,
						    struct mm_struct *mm,
						    unsigned long start,
						    unsigned long end)
{
	struct ib_ucontext *context = container_of(mn, struct ib_ucontext, mn);

	if (!context->invalidate_range)
		return;

	ib_ucontext_notifier_start_account(context);
	down_read(&context->umem_rwsem);
	rbt_ib_umem_for_each_in_range(&context->umem_tree, start,
				      end,
				      invalidate_range_start_trampoline, NULL);
	up_read(&context->umem_rwsem);
}

static int invalidate_range_end_trampoline(struct ib_umem *item, u64 start,
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
	struct ib_ucontext *context = container_of(mn, struct ib_ucontext, mn);

	if (!context->invalidate_range)
		return;

	down_read(&context->umem_rwsem);
	rbt_ib_umem_for_each_in_range(&context->umem_tree, start,
				      end,
				      invalidate_range_end_trampoline, NULL);
	up_read(&context->umem_rwsem);
	ib_ucontext_notifier_end_account(context);
}

static const struct mmu_notifier_ops ib_umem_notifiers = {
	.release                    = ib_umem_notifier_release,
	.invalidate_page            = ib_umem_notifier_invalidate_page,
	.invalidate_range_start     = ib_umem_notifier_invalidate_range_start,
	.invalidate_range_end       = ib_umem_notifier_invalidate_range_end,
};

struct ib_umem *ib_alloc_odp_umem(struct ib_ucontext *context,
				  unsigned long addr,
				  size_t size)
{
	struct ib_umem *umem;
	struct ib_umem_odp *odp_data;
	int pages = size >> PAGE_SHIFT;
	int ret;

	umem = kzalloc(sizeof(*umem), GFP_KERNEL);
	if (!umem)
		return ERR_PTR(-ENOMEM);

	umem->context   = context;
	umem->length    = size;
	umem->address   = addr;
	umem->page_size = PAGE_SIZE;
	umem->writable  = 1;

	odp_data = kzalloc(sizeof(*odp_data), GFP_KERNEL);
	if (!odp_data) {
		ret = -ENOMEM;
		goto out_umem;
	}
	odp_data->umem = umem;

	mutex_init(&odp_data->umem_mutex);
	init_completion(&odp_data->notifier_completion);

	odp_data->page_list = vzalloc(pages * sizeof(*odp_data->page_list));
	if (!odp_data->page_list) {
		ret = -ENOMEM;
		goto out_odp_data;
	}

	odp_data->dma_list = vzalloc(pages * sizeof(*odp_data->dma_list));
	if (!odp_data->dma_list) {
		ret = -ENOMEM;
		goto out_page_list;
	}

	down_write(&context->umem_rwsem);
	context->odp_mrs_count++;
	rbt_ib_umem_insert(&odp_data->interval_tree, &context->umem_tree);
	if (likely(!atomic_read(&context->notifier_count)))
		odp_data->mn_counters_active = true;
	else
		list_add(&odp_data->no_private_counters,
			 &context->no_private_counters);
	up_write(&context->umem_rwsem);

	umem->odp_data = odp_data;

	return umem;

out_page_list:
	vfree(odp_data->page_list);
out_odp_data:
	kfree(odp_data);
out_umem:
	kfree(umem);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL(ib_alloc_odp_umem);

int ib_umem_odp_get(struct ib_ucontext *context, struct ib_umem *umem)
{
	int ret_val;
	struct pid *our_pid;
	struct mm_struct *mm = get_task_mm(current);

	if (!mm)
		return -EINVAL;

	/* Prevent creating ODP MRs in child processes */
	rcu_read_lock();
	our_pid = get_task_pid(current->group_leader, PIDTYPE_PID);
	rcu_read_unlock();
	put_pid(our_pid);
	if (context->tgid != our_pid) {
		ret_val = -EINVAL;
		goto out_mm;
	}

	umem->hugetlb = 0;
	umem->odp_data = kzalloc(sizeof(*umem->odp_data), GFP_KERNEL);
	if (!umem->odp_data) {
		ret_val = -ENOMEM;
		goto out_mm;
	}
	umem->odp_data->umem = umem;

	mutex_init(&umem->odp_data->umem_mutex);

	init_completion(&umem->odp_data->notifier_completion);

	if (ib_umem_num_pages(umem)) {
		umem->odp_data->page_list = vzalloc(ib_umem_num_pages(umem) *
					    sizeof(*umem->odp_data->page_list));
		if (!umem->odp_data->page_list) {
			ret_val = -ENOMEM;
			goto out_odp_data;
		}

		umem->odp_data->dma_list = vzalloc(ib_umem_num_pages(umem) *
					  sizeof(*umem->odp_data->dma_list));
		if (!umem->odp_data->dma_list) {
			ret_val = -ENOMEM;
			goto out_page_list;
		}
	}

	/*
	 * When using MMU notifiers, we will get a
	 * notification before the "current" task (and MM) is
	 * destroyed. We use the umem_rwsem semaphore to synchronize.
	 */
	down_write(&context->umem_rwsem);
	context->odp_mrs_count++;
	if (likely(ib_umem_start(umem) != ib_umem_end(umem)))
		rbt_ib_umem_insert(&umem->odp_data->interval_tree,
				   &context->umem_tree);
	if (likely(!atomic_read(&context->notifier_count)) ||
	    context->odp_mrs_count == 1)
		umem->odp_data->mn_counters_active = true;
	else
		list_add(&umem->odp_data->no_private_counters,
			 &context->no_private_counters);
	downgrade_write(&context->umem_rwsem);

	if (context->odp_mrs_count == 1) {
		/*
		 * Note that at this point, no MMU notifier is running
		 * for this context!
		 */
		atomic_set(&context->notifier_count, 0);
		INIT_HLIST_NODE(&context->mn.hlist);
		context->mn.ops = &ib_umem_notifiers;
		/*
		 * Lock-dep detects a false positive for mmap_sem vs.
		 * umem_rwsem, due to not grasping downgrade_write correctly.
		 */
		lockdep_off();
		ret_val = mmu_notifier_register(&context->mn, mm);
		lockdep_on();
		if (ret_val) {
			pr_err("Failed to register mmu_notifier %d\n", ret_val);
			ret_val = -EBUSY;
			goto out_mutex;
		}
	}

	up_read(&context->umem_rwsem);

	/*
	 * Note that doing an mmput can cause a notifier for the relevant mm.
	 * If the notifier is called while we hold the umem_rwsem, this will
	 * cause a deadlock. Therefore, we release the reference only after we
	 * released the semaphore.
	 */
	mmput(mm);
	return 0;

out_mutex:
	up_read(&context->umem_rwsem);
	vfree(umem->odp_data->dma_list);
out_page_list:
	vfree(umem->odp_data->page_list);
out_odp_data:
	kfree(umem->odp_data);
out_mm:
	mmput(mm);
	return ret_val;
}

void ib_umem_odp_release(struct ib_umem *umem)
{
	struct ib_ucontext *context = umem->context;

	/*
	 * Ensure that no more pages are mapped in the umem.
	 *
	 * It is the driver's responsibility to ensure, before calling us,
	 * that the hardware will not attempt to access the MR any more.
	 */
	ib_umem_odp_unmap_dma_pages(umem, ib_umem_start(umem),
				    ib_umem_end(umem));

	down_write(&context->umem_rwsem);
	if (likely(ib_umem_start(umem) != ib_umem_end(umem)))
		rbt_ib_umem_remove(&umem->odp_data->interval_tree,
				   &context->umem_tree);
	context->odp_mrs_count--;
	if (!umem->odp_data->mn_counters_active) {
		list_del(&umem->odp_data->no_private_counters);
		complete_all(&umem->odp_data->notifier_completion);
	}

	/*
	 * Downgrade the lock to a read lock. This ensures that the notifiers
	 * (who lock the mutex for reading) will be able to finish, and we
	 * will be able to enventually obtain the mmu notifiers SRCU. Note
	 * that since we are doing it atomically, no other user could register
	 * and unregister while we do the check.
	 */
	downgrade_write(&context->umem_rwsem);
	if (!context->odp_mrs_count) {
		struct task_struct *owning_process = NULL;
		struct mm_struct *owning_mm        = NULL;

		owning_process = get_pid_task(context->tgid,
					      PIDTYPE_PID);
		if (owning_process == NULL)
			/*
			 * The process is already dead, notifier were removed
			 * already.
			 */
			goto out;

		owning_mm = get_task_mm(owning_process);
		if (owning_mm == NULL)
			/*
			 * The process' mm is already dead, notifier were
			 * removed already.
			 */
			goto out_put_task;
		mmu_notifier_unregister(&context->mn, owning_mm);

		mmput(owning_mm);

out_put_task:
		put_task_struct(owning_process);
	}
out:
	up_read(&context->umem_rwsem);

	vfree(umem->odp_data->dma_list);
	vfree(umem->odp_data->page_list);
	kfree(umem->odp_data);
	kfree(umem);
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
 *               umem->odp_data->notifiers_seq.
 *
 * The function returns -EFAULT if the DMA mapping operation fails. It returns
 * -EAGAIN if a concurrent invalidation prevents us from updating the page.
 *
 * The page is released via put_page even if the operation failed. For
 * on-demand pinning, the page is released whenever it isn't stored in the
 * umem.
 */
static int ib_umem_odp_map_dma_single_page(
		struct ib_umem *umem,
		int page_index,
		u64 base_virt_addr,
		struct page *page,
		u64 access_mask,
		unsigned long current_seq)
{
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
	if (ib_umem_mmu_notifier_retry(umem, current_seq)) {
		ret = -EAGAIN;
		goto out;
	}
	if (!(umem->odp_data->dma_list[page_index])) {
		dma_addr = ib_dma_map_page(dev,
					   page,
					   0, PAGE_SIZE,
					   DMA_BIDIRECTIONAL);
		if (ib_dma_mapping_error(dev, dma_addr)) {
			ret = -EFAULT;
			goto out;
		}
		umem->odp_data->dma_list[page_index] = dma_addr | access_mask;
		umem->odp_data->page_list[page_index] = page;
		umem->npages++;
		stored_page = 1;
	} else if (umem->odp_data->page_list[page_index] == page) {
		umem->odp_data->dma_list[page_index] |= access_mask;
	} else {
		pr_err("error: got different pages in IB device and from get_user_pages. IB device page: %p, gup page: %p\n",
		       umem->odp_data->page_list[page_index], page);
		/* Better remove the mapping now, to prevent any further
		 * damage. */
		remove_existing_mapping = 1;
	}

out:
	/* On Demand Paging - avoid pinning the page */
	if (umem->context->invalidate_range || !stored_page)
		put_page(page);

	if (remove_existing_mapping && umem->context->invalidate_range) {
		invalidate_page_trampoline(
			umem,
			base_virt_addr + (page_index * PAGE_SIZE),
			base_virt_addr + ((page_index+1)*PAGE_SIZE),
			NULL);
		ret = -EAGAIN;
	}

	return ret;
}

/**
 * ib_umem_odp_map_dma_pages - Pin and DMA map userspace memory in an ODP MR.
 *
 * Pins the range of pages passed in the argument, and maps them to
 * DMA addresses. The DMA addresses of the mapped pages is updated in
 * umem->odp_data->dma_list.
 *
 * Returns the number of pages mapped in success, negative error code
 * for failure.
 * An -EAGAIN error code is returned when a concurrent mmu notifier prevents
 * the function from completing its task.
 * An -ENOENT error code indicates that userspace process is being terminated
 * and mm was already destroyed.
 * @umem: the umem to map and pin
 * @user_virt: the address from which we need to map.
 * @bcnt: the minimal number of bytes to pin and map. The mapping might be
 *        bigger due to alignment, and may also be smaller in case of an error
 *        pinning or mapping a page. The actual pages mapped is returned in
 *        the return value.
 * @access_mask: bit mask of the requested access permissions for the given
 *               range.
 * @current_seq: the MMU notifiers sequance value for synchronization with
 *               invalidations. the sequance number is read from
 *               umem->odp_data->notifiers_seq before calling this function
 */
int ib_umem_odp_map_dma_pages(struct ib_umem *umem, u64 user_virt, u64 bcnt,
			      u64 access_mask, unsigned long current_seq)
{
	struct task_struct *owning_process  = NULL;
	struct mm_struct   *owning_mm       = NULL;
	struct page       **local_page_list = NULL;
	u64 off;
	int j, k, ret = 0, start_idx, npages = 0;
	u64 base_virt_addr;
	unsigned int flags = 0;

	if (access_mask == 0)
		return -EINVAL;

	if (user_virt < ib_umem_start(umem) ||
	    user_virt + bcnt > ib_umem_end(umem))
		return -EFAULT;

	local_page_list = (struct page **)__get_free_page(GFP_KERNEL);
	if (!local_page_list)
		return -ENOMEM;

	off = user_virt & (~PAGE_MASK);
	user_virt = user_virt & PAGE_MASK;
	base_virt_addr = user_virt;
	bcnt += off; /* Charge for the first page offset as well. */

	owning_process = get_pid_task(umem->context->tgid, PIDTYPE_PID);
	if (owning_process == NULL) {
		ret = -EINVAL;
		goto out_no_task;
	}

	owning_mm = get_task_mm(owning_process);
	if (owning_mm == NULL) {
		ret = -ENOENT;
		goto out_put_task;
	}

	if (access_mask & ODP_WRITE_ALLOWED_BIT)
		flags |= FOLL_WRITE;

	start_idx = (user_virt - ib_umem_start(umem)) >> PAGE_SHIFT;
	k = start_idx;

	while (bcnt > 0) {
		const size_t gup_num_pages =
			min_t(size_t, ALIGN(bcnt, PAGE_SIZE) / PAGE_SIZE,
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
		user_virt += npages << PAGE_SHIFT;
		mutex_lock(&umem->odp_data->umem_mutex);
		for (j = 0; j < npages; ++j) {
			ret = ib_umem_odp_map_dma_single_page(
				umem, k, base_virt_addr, local_page_list[j],
				access_mask, current_seq);
			if (ret < 0)
				break;
			k++;
		}
		mutex_unlock(&umem->odp_data->umem_mutex);

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
	put_task_struct(owning_process);
out_no_task:
	free_page((unsigned long)local_page_list);
	return ret;
}
EXPORT_SYMBOL(ib_umem_odp_map_dma_pages);

void ib_umem_odp_unmap_dma_pages(struct ib_umem *umem, u64 virt,
				 u64 bound)
{
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
	mutex_lock(&umem->odp_data->umem_mutex);
	for (addr = virt; addr < bound; addr += (u64)umem->page_size) {
		idx = (addr - ib_umem_start(umem)) / PAGE_SIZE;
		if (umem->odp_data->page_list[idx]) {
			struct page *page = umem->odp_data->page_list[idx];
			dma_addr_t dma = umem->odp_data->dma_list[idx];
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
			umem->odp_data->page_list[idx] = NULL;
			umem->odp_data->dma_list[idx] = 0;
			umem->npages--;
		}
	}
	mutex_unlock(&umem->odp_data->umem_mutex);
}
EXPORT_SYMBOL(ib_umem_odp_unmap_dma_pages);
