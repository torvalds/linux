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
 */

#include <linux/mm.h>
#include <linux/dma-mapping.h>
#include <linux/sched/signal.h>
#include <linux/sched/mm.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
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
		if (!PageDirty(page) && umem->writable && dirty)
			set_page_dirty_lock(page);
		put_page(page);
	}

	sg_free_table(&umem->sg_head);
}

/* ib_umem_add_sg_table - Add N contiguous pages to scatter table
 *
 * sg: current scatterlist entry
 * page_list: array of npage struct page pointers
 * npages: number of pages in page_list
 * max_seg_sz: maximum segment size in bytes
 * nents: [out] number of entries in the scatterlist
 *
 * Return new end of scatterlist
 */
static struct scatterlist *ib_umem_add_sg_table(struct scatterlist *sg,
						struct page **page_list,
						unsigned long npages,
						unsigned int max_seg_sz,
						int *nents)
{
	unsigned long first_pfn;
	unsigned long i = 0;
	bool update_cur_sg = false;
	bool first = !sg_page(sg);

	/* Check if new page_list is contiguous with end of previous page_list.
	 * sg->length here is a multiple of PAGE_SIZE and sg->offset is 0.
	 */
	if (!first && (page_to_pfn(sg_page(sg)) + (sg->length >> PAGE_SHIFT) ==
		       page_to_pfn(page_list[0])))
		update_cur_sg = true;

	while (i != npages) {
		unsigned long len;
		struct page *first_page = page_list[i];

		first_pfn = page_to_pfn(first_page);

		/* Compute the number of contiguous pages we have starting
		 * at i
		 */
		for (len = 0; i != npages &&
			      first_pfn + len == page_to_pfn(page_list[i]) &&
			      len < (max_seg_sz >> PAGE_SHIFT);
		     len++)
			i++;

		/* Squash N contiguous pages from page_list into current sge */
		if (update_cur_sg) {
			if ((max_seg_sz - sg->length) >= (len << PAGE_SHIFT)) {
				sg_set_page(sg, sg_page(sg),
					    sg->length + (len << PAGE_SHIFT),
					    0);
				update_cur_sg = false;
				continue;
			}
			update_cur_sg = false;
		}

		/* Squash N contiguous pages into next sge or first sge */
		if (!first)
			sg = sg_next(sg);

		(*nents)++;
		sg_set_page(sg, first_page, len << PAGE_SHIFT, 0);
		first = false;
	}

	return sg;
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
	unsigned int best_pg_bit;
	unsigned long va, pgoff;
	dma_addr_t mask;
	int i;

	/* At minimum, drivers must support PAGE_SIZE or smaller */
	if (WARN_ON(!(pgsz_bitmap & GENMASK(PAGE_SHIFT, 0))))
		return 0;

	va = virt;
	/* max page size not to exceed MR length */
	mask = roundup_pow_of_two(umem->length);
	/* offset into first SGL */
	pgoff = umem->address & ~PAGE_MASK;

	for_each_sg(umem->sg_head.sgl, sg, umem->nmap, i) {
		/* Walk SGL and reduce max page size if VA/PA bits differ
		 * for any address.
		 */
		mask |= (sg_dma_address(sg) + pgoff) ^ va;
		if (i && i != (umem->nmap - 1))
			/* restrict by length as well for interior SGEs */
			mask |= sg_dma_len(sg);
		va += sg_dma_len(sg) - pgoff;
		pgoff = 0;
	}
	best_pg_bit = rdma_find_pg_bit(mask, pgsz_bitmap);

	return BIT_ULL(best_pg_bit);
}
EXPORT_SYMBOL(ib_umem_find_best_pgsz);

/**
 * ib_umem_get - Pin and DMA map userspace memory.
 *
 * If access flags indicate ODP memory, avoid pinning. Instead, stores
 * the mm for future page fault handling in conjunction with MMU notifiers.
 *
 * @udata: userspace context to pin memory for
 * @addr: userspace virtual address to start at
 * @size: length of region to pin
 * @access: IB_ACCESS_xxx flags for memory being pinned
 * @dmasync: flush in-flight DMA when the memory region is written
 */
struct ib_umem *ib_umem_get(struct ib_udata *udata, unsigned long addr,
			    size_t size, int access, int dmasync)
{
	struct ib_ucontext *context;
	struct ib_umem *umem;
	struct page **page_list;
	unsigned long lock_limit;
	unsigned long new_pinned;
	unsigned long cur_base;
	struct mm_struct *mm;
	unsigned long npages;
	int ret;
	unsigned long dma_attrs = 0;
	struct scatterlist *sg;
	unsigned int gup_flags = FOLL_WRITE;

	if (!udata)
		return ERR_PTR(-EIO);

	context = container_of(udata, struct uverbs_attr_bundle, driver_udata)
			  ->context;
	if (!context)
		return ERR_PTR(-EIO);

	if (dmasync)
		dma_attrs |= DMA_ATTR_WRITE_BARRIER;

	/*
	 * If the combination of the addr and size requested for this memory
	 * region causes an integer overflow, return error.
	 */
	if (((addr + size) < addr) ||
	    PAGE_ALIGN(addr + size) < (addr + size))
		return ERR_PTR(-EINVAL);

	if (!can_do_mlock())
		return ERR_PTR(-EPERM);

	if (access & IB_ACCESS_ON_DEMAND) {
		umem = kzalloc(sizeof(struct ib_umem_odp), GFP_KERNEL);
		if (!umem)
			return ERR_PTR(-ENOMEM);
		umem->is_odp = 1;
	} else {
		umem = kzalloc(sizeof(*umem), GFP_KERNEL);
		if (!umem)
			return ERR_PTR(-ENOMEM);
	}

	umem->context    = context;
	umem->length     = size;
	umem->address    = addr;
	umem->page_shift = PAGE_SHIFT;
	umem->writable   = ib_access_writable(access);
	umem->owning_mm = mm = current->mm;
	mmgrab(mm);

	if (access & IB_ACCESS_ON_DEMAND) {
		if (WARN_ON_ONCE(!context->invalidate_range)) {
			ret = -EINVAL;
			goto umem_kfree;
		}

		ret = ib_umem_odp_get(to_ib_umem_odp(umem), access);
		if (ret)
			goto umem_kfree;
		return umem;
	}

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

	ret = sg_alloc_table(&umem->sg_head, npages, GFP_KERNEL);
	if (ret)
		goto vma;

	if (!umem->writable)
		gup_flags |= FOLL_FORCE;

	sg = umem->sg_head.sgl;

	while (npages) {
		down_read(&mm->mmap_sem);
		ret = get_user_pages_longterm(cur_base,
				     min_t(unsigned long, npages,
					   PAGE_SIZE / sizeof (struct page *)),
				     gup_flags, page_list, NULL);
		if (ret < 0) {
			up_read(&mm->mmap_sem);
			goto umem_release;
		}

		cur_base += ret * PAGE_SIZE;
		npages   -= ret;

		sg = ib_umem_add_sg_table(sg, page_list, ret,
			dma_get_max_seg_size(context->device->dma_device),
			&umem->sg_nents);

		up_read(&mm->mmap_sem);
	}

	sg_mark_end(sg);

	umem->nmap = ib_dma_map_sg_attrs(context->device,
				  umem->sg_head.sgl,
				  umem->sg_nents,
				  DMA_BIDIRECTIONAL,
				  dma_attrs);

	if (!umem->nmap) {
		ret = -ENOMEM;
		goto umem_release;
	}

	ret = 0;
	goto out;

umem_release:
	__ib_umem_release(context->device, umem, 0);
vma:
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

static void __ib_umem_release_tail(struct ib_umem *umem)
{
	mmdrop(umem->owning_mm);
	if (umem->is_odp)
		kfree(to_ib_umem_odp(umem));
	else
		kfree(umem);
}

/**
 * ib_umem_release - release memory pinned with ib_umem_get
 * @umem: umem struct to release
 */
void ib_umem_release(struct ib_umem *umem)
{
	if (umem->is_odp) {
		ib_umem_odp_release(to_ib_umem_odp(umem));
		__ib_umem_release_tail(umem);
		return;
	}

	__ib_umem_release(umem->context->device, umem, 1);

	atomic64_sub(ib_umem_num_pages(umem), &umem->owning_mm->pinned_vm);
	__ib_umem_release_tail(umem);
}
EXPORT_SYMBOL(ib_umem_release);

int ib_umem_page_count(struct ib_umem *umem)
{
	int i;
	int n;
	struct scatterlist *sg;

	if (umem->is_odp)
		return ib_umem_num_pages(umem);

	n = 0;
	for_each_sg(umem->sg_head.sgl, sg, umem->nmap, i)
		n += sg_dma_len(sg) >> umem->page_shift;

	return n;
}
EXPORT_SYMBOL(ib_umem_page_count);

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
