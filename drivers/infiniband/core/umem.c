/*
 * Copyright (c) 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Cisco Systems.  All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2020 Intel Corporation. All rights reserved.
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

#include <linux/mm.h>
#include <linux/dma-mapping.h>
#include <linux/sched/signal.h>
#include <linux/sched/mm.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/count_zeros.h>
#include <rdma/ib_umem_odp.h>

#include "uverbs.h"

static void __ib_umem_release(struct ib_device *dev, struct ib_umem *umem, int dirty)
{
	struct sg_page_iter sg_iter;
	struct page *page;

	if (umem->nmap > 0)
		ib_dma_unmap_sg(dev, umem->sg_head.sgl, umem->sg_nents,
				DMA_BIDIRECTIONAL);

	for_each_sg_page(umem->sg_head.sgl, &sg_iter, umem->sg_nents, 0) {
		page = sg_page_iter_page(&sg_iter);
		unpin_user_pages_dirty_lock(&page, 1, umem->writable && dirty);
	}

	sg_free_table(&umem->sg_head);
}

/**
 * ib_umem_find_best_pgsz - Find best HW page size to use for this MR
 *
 * @umem: umem struct
 * @pgsz_bitmap: bitmap of HW supported page sizes
 * @virt: IOVA
 *
 * This helper is intended for HW that support multiple page
 * sizes but can do only a single page size in an MR.
 *
 * Returns 0 if the umem requires page sizes not supported by
 * the driver to be mapped. Drivers always supporting PAGE_SIZE
 * or smaller will never see a 0 result.
 */
unsigned long ib_umem_find_best_pgsz(struct ib_umem *umem,
				     unsigned long pgsz_bitmap,
				     unsigned long virt)
{
	struct scatterlist *sg;
	unsigned long va, pgoff;
	dma_addr_t mask;
	int i;

	if (umem->is_odp) {
		unsigned int page_size = BIT(to_ib_umem_odp(umem)->page_shift);

		/* ODP must always be self consistent. */
		if (!(pgsz_bitmap & page_size))
			return 0;
		return page_size;
	}

	/* rdma_for_each_block() has a bug if the page size is smaller than the
	 * page size used to build the umem. For now prevent smaller page sizes
	 * from being returned.
	 */
	pgsz_bitmap &= GENMASK(BITS_PER_LONG - 1, PAGE_SHIFT);

	/* At minimum, drivers must support PAGE_SIZE or smaller */
	if (WARN_ON(!(pgsz_bitmap & GENMASK(PAGE_SHIFT, 0))))
		return 0;

	umem->iova = va = virt;
	/* The best result is the smallest page size that results in the minimum
	 * number of required pages. Compute the largest page size that could
	 * work based on VA address bits that don't change.
	 */
	mask = pgsz_bitmap &
	       GENMASK(BITS_PER_LONG - 1,
		       bits_per((umem->length - 1 + virt) ^ virt));
	/* offset into first SGL */
	pgoff = umem->address & ~PAGE_MASK;

	for_each_sg(umem->sg_head.sgl, sg, umem->nmap, i) {
		/* Walk SGL and reduce max page size if VA/PA bits differ
		 * for any address.
		 */
		mask |= (sg_dma_address(sg) + pgoff) ^ va;
		va += sg_dma_len(sg) - pgoff;
		/* Except for the last entry, the ending iova alignment sets
		 * the maximum possible page size as the low bits of the iova
		 * must be zero when starting the next chunk.
		 */
		if (i != (umem->nmap - 1))
			mask |= va;
		pgoff = 0;
	}

	/* The mask accumulates 1's in each position where the VA and physical
	 * address differ, thus the length of trailing 0 is the largest page
	 * size that can pass the VA through to the physical.
	 */
	if (mask)
		pgsz_bitmap &= GENMASK(count_trailing_zeros(mask), 0);
	return pgsz_bitmap ? rounddown_pow_of_two(pgsz_bitmap) : 0;
}
EXPORT_SYMBOL(ib_umem_find_best_pgsz);

/**
 * ib_umem_get - Pin and DMA map userspace memory.
 *
 * @device: IB device to connect UMEM
 * @addr: userspace virtual address to start at
 * @size: length of region to pin
 * @access: IB_ACCESS_xxx flags for memory being pinned
 */
struct ib_umem *ib_umem_get(struct ib_device *device, unsigned long addr,
			    size_t size, int access)
{
	struct ib_umem *umem;
	struct page **page_list;
	unsigned long lock_limit;
	unsigned long new_pinned;
	unsigned long cur_base;
	unsigned long dma_attr = 0;
	struct mm_struct *mm;
	unsigned long npages;
	int ret;
	struct scatterlist *sg = NULL;
	unsigned int gup_flags = FOLL_WRITE;

	/*
	 * If the combination of the addr and size requested for this memory
	 * region causes an integer overflow, return error.
	 */
	if (((addr + size) < addr) ||
	    PAGE_ALIGN(addr + size) < (addr + size))
		return ERR_PTR(-EINVAL);

	if (!can_do_mlock())
		return ERR_PTR(-EPERM);

	if (access & IB_ACCESS_ON_DEMAND)
		return ERR_PTR(-EOPNOTSUPP);

	umem = kzalloc(sizeof(*umem), GFP_KERNEL);
	if (!umem)
		return ERR_PTR(-ENOMEM);
	umem->ibdev      = device;
	umem->length     = size;
	umem->address    = addr;
	/*
	 * Drivers should call ib_umem_find_best_pgsz() to set the iova
	 * correctly.
	 */
	umem->iova = addr;
	umem->writable   = ib_access_writable(access);
	umem->owning_mm = mm = current->mm;
	mmgrab(mm);

	page_list = (struct page **) __get_free_page(GFP_KERNEL);
	if (!page_list) {
		ret = -ENOMEM;
		goto umem_kfree;
	}

	npages = ib_umem_num_pages(umem);
	if (npages == 0 || npages > UINT_MAX) {
		ret = -EINVAL;
		goto out;
	}

	lock_limit = rlimit(RLIMIT_MEMLOCK) >> PAGE_SHIFT;

	new_pinned = atomic64_add_return(npages, &mm->pinned_vm);
	if (new_pinned > lock_limit && !capable(CAP_IPC_LOCK)) {
		atomic64_sub(npages, &mm->pinned_vm);
		ret = -ENOMEM;
		goto out;
	}

	cur_base = addr & PAGE_MASK;

	if (!umem->writable)
		gup_flags |= FOLL_FORCE;

	while (npages) {
		cond_resched();
		ret = pin_user_pages_fast(cur_base,
					  min_t(unsigned long, npages,
						PAGE_SIZE /
						sizeof(struct page *)),
					  gup_flags | FOLL_LONGTERM, page_list);
		if (ret < 0)
			goto umem_release;

		cur_base += ret * PAGE_SIZE;
		npages -= ret;
		sg = __sg_alloc_table_from_pages(&umem->sg_head, page_list, ret,
				0, ret << PAGE_SHIFT,
				ib_dma_max_seg_size(device), sg, npages,
				GFP_KERNEL);
		umem->sg_nents = umem->sg_head.nents;
		if (IS_ERR(sg)) {
			unpin_user_pages_dirty_lock(page_list, ret, 0);
			ret = PTR_ERR(sg);
			goto umem_release;
		}
	}

	if (access & IB_ACCESS_RELAXED_ORDERING)
		dma_attr |= DMA_ATTR_WEAK_ORDERING;

	umem->nmap =
		ib_dma_map_sg_attrs(device, umem->sg_head.sgl, umem->sg_nents,
				    DMA_BIDIRECTIONAL, dma_attr);

	if (!umem->nmap) {
		ret = -ENOMEM;
		goto umem_release;
	}

	ret = 0;
	goto out;

umem_release:
	__ib_umem_release(device, umem, 0);
	atomic64_sub(ib_umem_num_pages(umem), &mm->pinned_vm);
out:
	free_page((unsigned long) page_list);
umem_kfree:
	if (ret) {
		mmdrop(umem->owning_mm);
		kfree(umem);
	}
	return ret ? ERR_PTR(ret) : umem;
}
EXPORT_SYMBOL(ib_umem_get);

/**
 * ib_umem_release - release memory pinned with ib_umem_get
 * @umem: umem struct to release
 */
void ib_umem_release(struct ib_umem *umem)
{
	if (!umem)
		return;
	if (umem->is_dmabuf)
		return ib_umem_dmabuf_release(to_ib_umem_dmabuf(umem));
	if (umem->is_odp)
		return ib_umem_odp_release(to_ib_umem_odp(umem));

	__ib_umem_release(umem->ibdev, umem, 1);

	atomic64_sub(ib_umem_num_pages(umem), &umem->owning_mm->pinned_vm);
	mmdrop(umem->owning_mm);
	kfree(umem);
}
EXPORT_SYMBOL(ib_umem_release);

/*
 * Copy from the given ib_umem's pages to the given buffer.
 *
 * umem - the umem to copy from
 * offset - offset to start copying from
 * dst - destination buffer
 * length - buffer length
 *
 * Returns 0 on success, or an error code.
 */
int ib_umem_copy_from(void *dst, struct ib_umem *umem, size_t offset,
		      size_t length)
{
	size_t end = offset + length;
	int ret;

	if (offset > umem->length || length > umem->length - offset) {
		pr_err("ib_umem_copy_from not in range. offset: %zd umem length: %zd end: %zd\n",
		       offset, umem->length, end);
		return -EINVAL;
	}

	ret = sg_pcopy_to_buffer(umem->sg_head.sgl, umem->sg_nents, dst, length,
				 offset + ib_umem_offset(umem));

	if (ret < 0)
		return ret;
	else if (ret != length)
		return -EINVAL;
	else
		return 0;
}
EXPORT_SYMBOL(ib_umem_copy_from);
