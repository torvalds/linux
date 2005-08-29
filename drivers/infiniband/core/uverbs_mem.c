/*
 * Copyright (c) 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Cisco Systems.  All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies. All rights reserved.
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
 *
 * $Id: uverbs_mem.c 2743 2005-06-28 22:27:59Z roland $
 */

#include <linux/mm.h>
#include <linux/dma-mapping.h>

#include "uverbs.h"

struct ib_umem_account_work {
	struct work_struct work;
	struct mm_struct  *mm;
	unsigned long      diff;
};


static void __ib_umem_release(struct ib_device *dev, struct ib_umem *umem, int dirty)
{
	struct ib_umem_chunk *chunk, *tmp;
	int i;

	list_for_each_entry_safe(chunk, tmp, &umem->chunk_list, list) {
		dma_unmap_sg(dev->dma_device, chunk->page_list,
			     chunk->nents, DMA_BIDIRECTIONAL);
		for (i = 0; i < chunk->nents; ++i) {
			if (umem->writable && dirty)
				set_page_dirty_lock(chunk->page_list[i].page);
			put_page(chunk->page_list[i].page);
		}

		kfree(chunk);
	}
}

int ib_umem_get(struct ib_device *dev, struct ib_umem *mem,
		void *addr, size_t size, int write)
{
	struct page **page_list;
	struct ib_umem_chunk *chunk;
	unsigned long locked;
	unsigned long lock_limit;
	unsigned long cur_base;
	unsigned long npages;
	int ret = 0;
	int off;
	int i;

	if (!can_do_mlock())
		return -EPERM;

	page_list = (struct page **) __get_free_page(GFP_KERNEL);
	if (!page_list)
		return -ENOMEM;

	mem->user_base = (unsigned long) addr;
	mem->length    = size;
	mem->offset    = (unsigned long) addr & ~PAGE_MASK;
	mem->page_size = PAGE_SIZE;
	mem->writable  = write;

	INIT_LIST_HEAD(&mem->chunk_list);

	npages = PAGE_ALIGN(size + mem->offset) >> PAGE_SHIFT;

	down_write(&current->mm->mmap_sem);

	locked     = npages + current->mm->locked_vm;
	lock_limit = current->signal->rlim[RLIMIT_MEMLOCK].rlim_cur >> PAGE_SHIFT;

	if ((locked > lock_limit) && !capable(CAP_IPC_LOCK)) {
		ret = -ENOMEM;
		goto out;
	}

	cur_base = (unsigned long) addr & PAGE_MASK;

	while (npages) {
		ret = get_user_pages(current, current->mm, cur_base,
				     min_t(int, npages,
					   PAGE_SIZE / sizeof (struct page *)),
				     1, !write, page_list, NULL);

		if (ret < 0)
			goto out;

		cur_base += ret * PAGE_SIZE;
		npages   -= ret;

		off = 0;

		while (ret) {
			chunk = kmalloc(sizeof *chunk + sizeof (struct scatterlist) *
					min_t(int, ret, IB_UMEM_MAX_PAGE_CHUNK),
					GFP_KERNEL);
			if (!chunk) {
				ret = -ENOMEM;
				goto out;
			}

			chunk->nents = min_t(int, ret, IB_UMEM_MAX_PAGE_CHUNK);
			for (i = 0; i < chunk->nents; ++i) {
				chunk->page_list[i].page   = page_list[i + off];
				chunk->page_list[i].offset = 0;
				chunk->page_list[i].length = PAGE_SIZE;
			}

			chunk->nmap = dma_map_sg(dev->dma_device,
						 &chunk->page_list[0],
						 chunk->nents,
						 DMA_BIDIRECTIONAL);
			if (chunk->nmap <= 0) {
				for (i = 0; i < chunk->nents; ++i)
					put_page(chunk->page_list[i].page);
				kfree(chunk);

				ret = -ENOMEM;
				goto out;
			}

			ret -= chunk->nents;
			off += chunk->nents;
			list_add_tail(&chunk->list, &mem->chunk_list);
		}

		ret = 0;
	}

out:
	if (ret < 0)
		__ib_umem_release(dev, mem, 0);
	else
		current->mm->locked_vm = locked;

	up_write(&current->mm->mmap_sem);
	free_page((unsigned long) page_list);

	return ret;
}

void ib_umem_release(struct ib_device *dev, struct ib_umem *umem)
{
	__ib_umem_release(dev, umem, 1);

	down_write(&current->mm->mmap_sem);
	current->mm->locked_vm -=
		PAGE_ALIGN(umem->length + umem->offset) >> PAGE_SHIFT;
	up_write(&current->mm->mmap_sem);
}

static void ib_umem_account(void *work_ptr)
{
	struct ib_umem_account_work *work = work_ptr;

	down_write(&work->mm->mmap_sem);
	work->mm->locked_vm -= work->diff;
	up_write(&work->mm->mmap_sem);
	mmput(work->mm);
	kfree(work);
}

void ib_umem_release_on_close(struct ib_device *dev, struct ib_umem *umem)
{
	struct ib_umem_account_work *work;
	struct mm_struct *mm;

	__ib_umem_release(dev, umem, 1);

	mm = get_task_mm(current);
	if (!mm)
		return;

	/*
	 * We may be called with the mm's mmap_sem already held.  This
	 * can happen when a userspace munmap() is the call that drops
	 * the last reference to our file and calls our release
	 * method.  If there are memory regions to destroy, we'll end
	 * up here and not be able to take the mmap_sem.  Therefore we
	 * defer the vm_locked accounting to the system workqueue.
	 */

	work = kmalloc(sizeof *work, GFP_KERNEL);
	if (!work)
		return;

	INIT_WORK(&work->work, ib_umem_account, work);
	work->mm   = mm;
	work->diff = PAGE_ALIGN(umem->length + umem->offset) >> PAGE_SHIFT;

	schedule_work(&work->work);
}
