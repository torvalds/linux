/*
 * Copyright (c) 2006, 2007 QLogic Corporation. All rights reserved.
 * Copyright (c) 2005, 2006 PathScale, Inc. All rights reserved.
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

#include <linux/slab.h>

#include <rdma/ib_umem.h>
#include <rdma/ib_pack.h>
#include <rdma/ib_smi.h>

#include "ipath_verbs.h"

/* Fast memory region */
struct ipath_fmr {
	struct ib_fmr ibfmr;
	u8 page_shift;
	struct ipath_mregion mr;        /* must be last */
};

static inline struct ipath_fmr *to_ifmr(struct ib_fmr *ibfmr)
{
	return container_of(ibfmr, struct ipath_fmr, ibfmr);
}

/**
 * ipath_get_dma_mr - get a DMA memory region
 * @pd: protection domain for this memory region
 * @acc: access flags
 *
 * Returns the memory region on success, otherwise returns an errno.
 * Note that all DMA addresses should be created via the
 * struct ib_dma_mapping_ops functions (see ipath_dma.c).
 */
struct ib_mr *ipath_get_dma_mr(struct ib_pd *pd, int acc)
{
	struct ipath_mr *mr;
	struct ib_mr *ret;

	mr = kzalloc(sizeof *mr, GFP_KERNEL);
	if (!mr) {
		ret = ERR_PTR(-ENOMEM);
		goto bail;
	}

	mr->mr.access_flags = acc;
	ret = &mr->ibmr;

bail:
	return ret;
}

static struct ipath_mr *alloc_mr(int count,
				 struct ipath_lkey_table *lk_table)
{
	struct ipath_mr *mr;
	int m, i = 0;

	/* Allocate struct plus pointers to first level page tables. */
	m = (count + IPATH_SEGSZ - 1) / IPATH_SEGSZ;
	mr = kmalloc(sizeof *mr + m * sizeof mr->mr.map[0], GFP_KERNEL);
	if (!mr)
		goto done;

	/* Allocate first level page tables. */
	for (; i < m; i++) {
		mr->mr.map[i] = kmalloc(sizeof *mr->mr.map[0], GFP_KERNEL);
		if (!mr->mr.map[i])
			goto bail;
	}
	mr->mr.mapsz = m;

	/*
	 * ib_reg_phys_mr() will initialize mr->ibmr except for
	 * lkey and rkey.
	 */
	if (!ipath_alloc_lkey(lk_table, &mr->mr))
		goto bail;
	mr->ibmr.rkey = mr->ibmr.lkey = mr->mr.lkey;

	goto done;

bail:
	while (i) {
		i--;
		kfree(mr->mr.map[i]);
	}
	kfree(mr);
	mr = NULL;

done:
	return mr;
}

/**
 * ipath_reg_phys_mr - register a physical memory region
 * @pd: protection domain for this memory region
 * @buffer_list: pointer to the list of physical buffers to register
 * @num_phys_buf: the number of physical buffers to register
 * @iova_start: the starting address passed over IB which maps to this MR
 *
 * Returns the memory region on success, otherwise returns an errno.
 */
struct ib_mr *ipath_reg_phys_mr(struct ib_pd *pd,
				struct ib_phys_buf *buffer_list,
				int num_phys_buf, int acc, u64 *iova_start)
{
	struct ipath_mr *mr;
	int n, m, i;
	struct ib_mr *ret;

	mr = alloc_mr(num_phys_buf, &to_idev(pd->device)->lk_table);
	if (mr == NULL) {
		ret = ERR_PTR(-ENOMEM);
		goto bail;
	}

	mr->mr.pd = pd;
	mr->mr.user_base = *iova_start;
	mr->mr.iova = *iova_start;
	mr->mr.length = 0;
	mr->mr.offset = 0;
	mr->mr.access_flags = acc;
	mr->mr.max_segs = num_phys_buf;
	mr->umem = NULL;

	m = 0;
	n = 0;
	for (i = 0; i < num_phys_buf; i++) {
		mr->mr.map[m]->segs[n].vaddr = (void *) buffer_list[i].addr;
		mr->mr.map[m]->segs[n].length = buffer_list[i].size;
		mr->mr.length += buffer_list[i].size;
		n++;
		if (n == IPATH_SEGSZ) {
			m++;
			n = 0;
		}
	}

	ret = &mr->ibmr;

bail:
	return ret;
}

/**
 * ipath_reg_user_mr - register a userspace memory region
 * @pd: protection domain for this memory region
 * @start: starting userspace address
 * @length: length of region to register
 * @virt_addr: virtual address to use (from HCA's point of view)
 * @mr_access_flags: access flags for this memory region
 * @udata: unused by the InfiniPath driver
 *
 * Returns the memory region on success, otherwise returns an errno.
 */
struct ib_mr *ipath_reg_user_mr(struct ib_pd *pd, u64 start, u64 length,
				u64 virt_addr, int mr_access_flags,
				struct ib_udata *udata)
{
	struct ipath_mr *mr;
	struct ib_umem *umem;
	int n, m, entry;
	struct scatterlist *sg;
	struct ib_mr *ret;

	if (length == 0) {
		ret = ERR_PTR(-EINVAL);
		goto bail;
	}

	umem = ib_umem_get(pd->uobject->context, start, length,
			   mr_access_flags, 0);
	if (IS_ERR(umem))
		return (void *) umem;

	n = umem->nmap;
	mr = alloc_mr(n, &to_idev(pd->device)->lk_table);
	if (!mr) {
		ret = ERR_PTR(-ENOMEM);
		ib_umem_release(umem);
		goto bail;
	}

	mr->mr.pd = pd;
	mr->mr.user_base = start;
	mr->mr.iova = virt_addr;
	mr->mr.length = length;
	mr->mr.offset = ib_umem_offset(umem);
	mr->mr.access_flags = mr_access_flags;
	mr->mr.max_segs = n;
	mr->umem = umem;

	m = 0;
	n = 0;
	for_each_sg(umem->sg_head.sgl, sg, umem->nmap, entry) {
		void *vaddr;

		vaddr = page_address(sg_page(sg));
		if (!vaddr) {
			ret = ERR_PTR(-EINVAL);
			goto bail;
		}
		mr->mr.map[m]->segs[n].vaddr = vaddr;
		mr->mr.map[m]->segs[n].length = umem->page_size;
		n++;
		if (n == IPATH_SEGSZ) {
			m++;
			n = 0;
		}
	}
	ret = &mr->ibmr;

bail:
	return ret;
}

/**
 * ipath_dereg_mr - unregister and free a memory region
 * @ibmr: the memory region to free
 *
 * Returns 0 on success.
 *
 * Note that this is called to free MRs created by ipath_get_dma_mr()
 * or ipath_reg_user_mr().
 */
int ipath_dereg_mr(struct ib_mr *ibmr)
{
	struct ipath_mr *mr = to_imr(ibmr);
	int i;

	ipath_free_lkey(&to_idev(ibmr->device)->lk_table, ibmr->lkey);
	i = mr->mr.mapsz;
	while (i) {
		i--;
		kfree(mr->mr.map[i]);
	}

	if (mr->umem)
		ib_umem_release(mr->umem);

	kfree(mr);
	return 0;
}

/**
 * ipath_alloc_fmr - allocate a fast memory region
 * @pd: the protection domain for this memory region
 * @mr_access_flags: access flags for this memory region
 * @fmr_attr: fast memory region attributes
 *
 * Returns the memory region on success, otherwise returns an errno.
 */
struct ib_fmr *ipath_alloc_fmr(struct ib_pd *pd, int mr_access_flags,
			       struct ib_fmr_attr *fmr_attr)
{
	struct ipath_fmr *fmr;
	int m, i = 0;
	struct ib_fmr *ret;

	/* Allocate struct plus pointers to first level page tables. */
	m = (fmr_attr->max_pages + IPATH_SEGSZ - 1) / IPATH_SEGSZ;
	fmr = kmalloc(sizeof *fmr + m * sizeof fmr->mr.map[0], GFP_KERNEL);
	if (!fmr)
		goto bail;

	/* Allocate first level page tables. */
	for (; i < m; i++) {
		fmr->mr.map[i] = kmalloc(sizeof *fmr->mr.map[0],
					 GFP_KERNEL);
		if (!fmr->mr.map[i])
			goto bail;
	}
	fmr->mr.mapsz = m;

	/*
	 * ib_alloc_fmr() will initialize fmr->ibfmr except for lkey &
	 * rkey.
	 */
	if (!ipath_alloc_lkey(&to_idev(pd->device)->lk_table, &fmr->mr))
		goto bail;
	fmr->ibfmr.rkey = fmr->ibfmr.lkey = fmr->mr.lkey;
	/*
	 * Resources are allocated but no valid mapping (RKEY can't be
	 * used).
	 */
	fmr->mr.pd = pd;
	fmr->mr.user_base = 0;
	fmr->mr.iova = 0;
	fmr->mr.length = 0;
	fmr->mr.offset = 0;
	fmr->mr.access_flags = mr_access_flags;
	fmr->mr.max_segs = fmr_attr->max_pages;
	fmr->page_shift = fmr_attr->page_shift;

	ret = &fmr->ibfmr;
	goto done;

bail:
	while (i)
		kfree(fmr->mr.map[--i]);
	kfree(fmr);
	ret = ERR_PTR(-ENOMEM);

done:
	return ret;
}

/**
 * ipath_map_phys_fmr - set up a fast memory region
 * @ibmfr: the fast memory region to set up
 * @page_list: the list of pages to associate with the fast memory region
 * @list_len: the number of pages to associate with the fast memory region
 * @iova: the virtual address of the start of the fast memory region
 *
 * This may be called from interrupt context.
 */

int ipath_map_phys_fmr(struct ib_fmr *ibfmr, u64 * page_list,
		       int list_len, u64 iova)
{
	struct ipath_fmr *fmr = to_ifmr(ibfmr);
	struct ipath_lkey_table *rkt;
	unsigned long flags;
	int m, n, i;
	u32 ps;
	int ret;

	if (list_len > fmr->mr.max_segs) {
		ret = -EINVAL;
		goto bail;
	}
	rkt = &to_idev(ibfmr->device)->lk_table;
	spin_lock_irqsave(&rkt->lock, flags);
	fmr->mr.user_base = iova;
	fmr->mr.iova = iova;
	ps = 1 << fmr->page_shift;
	fmr->mr.length = list_len * ps;
	m = 0;
	n = 0;
	ps = 1 << fmr->page_shift;
	for (i = 0; i < list_len; i++) {
		fmr->mr.map[m]->segs[n].vaddr = (void *) page_list[i];
		fmr->mr.map[m]->segs[n].length = ps;
		if (++n == IPATH_SEGSZ) {
			m++;
			n = 0;
		}
	}
	spin_unlock_irqrestore(&rkt->lock, flags);
	ret = 0;

bail:
	return ret;
}

/**
 * ipath_unmap_fmr - unmap fast memory regions
 * @fmr_list: the list of fast memory regions to unmap
 *
 * Returns 0 on success.
 */
int ipath_unmap_fmr(struct list_head *fmr_list)
{
	struct ipath_fmr *fmr;
	struct ipath_lkey_table *rkt;
	unsigned long flags;

	list_for_each_entry(fmr, fmr_list, ibfmr.list) {
		rkt = &to_idev(fmr->ibfmr.device)->lk_table;
		spin_lock_irqsave(&rkt->lock, flags);
		fmr->mr.user_base = 0;
		fmr->mr.iova = 0;
		fmr->mr.length = 0;
		spin_unlock_irqrestore(&rkt->lock, flags);
	}
	return 0;
}

/**
 * ipath_dealloc_fmr - deallocate a fast memory region
 * @ibfmr: the fast memory region to deallocate
 *
 * Returns 0 on success.
 */
int ipath_dealloc_fmr(struct ib_fmr *ibfmr)
{
	struct ipath_fmr *fmr = to_ifmr(ibfmr);
	int i;

	ipath_free_lkey(&to_idev(ibfmr->device)->lk_table, ibfmr->lkey);
	i = fmr->mr.mapsz;
	while (i)
		kfree(fmr->mr.map[--i]);
	kfree(fmr);
	return 0;
}
