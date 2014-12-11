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
#include <linux/pid.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/vmalloc.h>

#include <rdma/ib_verbs.h>
#include <rdma/ib_umem.h>
#include <rdma/ib_umem_odp.h>

int ib_umem_odp_get(struct ib_ucontext *context, struct ib_umem *umem)
{
	int ret_val;
	struct pid *our_pid;

	/* Prevent creating ODP MRs in child processes */
	rcu_read_lock();
	our_pid = get_task_pid(current->group_leader, PIDTYPE_PID);
	rcu_read_unlock();
	put_pid(our_pid);
	if (context->tgid != our_pid)
		return -EINVAL;

	umem->hugetlb = 0;
	umem->odp_data = kzalloc(sizeof(*umem->odp_data), GFP_KERNEL);
	if (!umem->odp_data)
		return -ENOMEM;

	mutex_init(&umem->odp_data->umem_mutex);

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

	return 0;

out_page_list:
	vfree(umem->odp_data->page_list);
out_odp_data:
	kfree(umem->odp_data);
	return ret_val;
}

void ib_umem_odp_release(struct ib_umem *umem)
{
	/*
	 * Ensure that no more pages are mapped in the umem.
	 *
	 * It is the driver's responsibility to ensure, before calling us,
	 * that the hardware will not attempt to access the MR any more.
	 */
	ib_umem_odp_unmap_dma_pages(umem, ib_umem_start(umem),
				    ib_umem_end(umem));

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
 * The function returns -EFAULT if the DMA mapping operation fails.
 *
 * The page is released via put_page even if the operation failed. For
 * on-demand pinning, the page is released whenever it isn't stored in the
 * umem.
 */
static int ib_umem_odp_map_dma_single_page(
		struct ib_umem *umem,
		int page_index,
		struct page *page,
		u64 access_mask,
		unsigned long current_seq)
{
	struct ib_device *dev = umem->context->device;
	dma_addr_t dma_addr;
	int stored_page = 0;
	int ret = 0;

	mutex_lock(&umem->odp_data->umem_mutex);
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
		stored_page = 1;
	} else if (umem->odp_data->page_list[page_index] == page) {
		umem->odp_data->dma_list[page_index] |= access_mask;
	} else {
		pr_err("error: got different pages in IB device and from get_user_pages. IB device page: %p, gup page: %p\n",
		       umem->odp_data->page_list[page_index], page);
	}

out:
	mutex_unlock(&umem->odp_data->umem_mutex);

	if (!stored_page)
		put_page(page);

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
 *
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
	bcnt += off; /* Charge for the first page offset as well. */

	owning_process = get_pid_task(umem->context->tgid, PIDTYPE_PID);
	if (owning_process == NULL) {
		ret = -EINVAL;
		goto out_no_task;
	}

	owning_mm = get_task_mm(owning_process);
	if (owning_mm == NULL) {
		ret = -EINVAL;
		goto out_put_task;
	}

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
		npages = get_user_pages(owning_process, owning_mm, user_virt,
					gup_num_pages,
					access_mask & ODP_WRITE_ALLOWED_BIT, 0,
					local_page_list, NULL);
		up_read(&owning_mm->mmap_sem);

		if (npages < 0)
			break;

		bcnt -= min_t(size_t, npages << PAGE_SHIFT, bcnt);
		user_virt += npages << PAGE_SHIFT;
		for (j = 0; j < npages; ++j) {
			ret = ib_umem_odp_map_dma_single_page(
				umem, k, local_page_list[j], access_mask,
				current_seq);
			if (ret < 0)
				break;
			k++;
		}

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
	for (addr = virt; addr < bound; addr += (u64)umem->page_size) {
		idx = (addr - ib_umem_start(umem)) / PAGE_SIZE;
		mutex_lock(&umem->odp_data->umem_mutex);
		if (umem->odp_data->page_list[idx]) {
			struct page *page = umem->odp_data->page_list[idx];
			struct page *head_page = compound_head(page);
			dma_addr_t dma = umem->odp_data->dma_list[idx];
			dma_addr_t dma_addr = dma & ODP_DMA_ADDR_MASK;

			WARN_ON(!dma_addr);

			ib_dma_unmap_page(dev, dma_addr, PAGE_SIZE,
					  DMA_BIDIRECTIONAL);
			if (dma & ODP_WRITE_ALLOWED_BIT)
				set_page_dirty_lock(head_page);
			put_page(page);
		}
		mutex_unlock(&umem->odp_data->umem_mutex);
	}
}
EXPORT_SYMBOL(ib_umem_odp_unmap_dma_pages);
