/*
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2015 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Copyright(c) 2015 Intel Corporation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <rdma/ib_umem.h>
#include <rdma/ib_smi.h>

#include "hfi.h"

/* Fast memory region */
struct hfi1_fmr {
	struct ib_fmr ibfmr;
	struct hfi1_mregion mr;        /* must be last */
};

static inline struct hfi1_fmr *to_ifmr(struct ib_fmr *ibfmr)
{
	return container_of(ibfmr, struct hfi1_fmr, ibfmr);
}

static int init_mregion(struct hfi1_mregion *mr, struct ib_pd *pd,
			int count)
{
	int m, i = 0;
	int rval = 0;

	m = (count + HFI1_SEGSZ - 1) / HFI1_SEGSZ;
	for (; i < m; i++) {
		mr->map[i] = kzalloc(sizeof(*mr->map[0]), GFP_KERNEL);
		if (!mr->map[i])
			goto bail;
	}
	mr->mapsz = m;
	init_completion(&mr->comp);
	/* count returning the ptr to user */
	atomic_set(&mr->refcount, 1);
	mr->pd = pd;
	mr->max_segs = count;
out:
	return rval;
bail:
	while (i)
		kfree(mr->map[--i]);
	rval = -ENOMEM;
	goto out;
}

static void deinit_mregion(struct hfi1_mregion *mr)
{
	int i = mr->mapsz;

	mr->mapsz = 0;
	while (i)
		kfree(mr->map[--i]);
}


/**
 * hfi1_get_dma_mr - get a DMA memory region
 * @pd: protection domain for this memory region
 * @acc: access flags
 *
 * Returns the memory region on success, otherwise returns an errno.
 * Note that all DMA addresses should be created via the
 * struct ib_dma_mapping_ops functions (see dma.c).
 */
struct ib_mr *hfi1_get_dma_mr(struct ib_pd *pd, int acc)
{
	struct hfi1_mr *mr = NULL;
	struct ib_mr *ret;
	int rval;

	if (to_ipd(pd)->user) {
		ret = ERR_PTR(-EPERM);
		goto bail;
	}

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr) {
		ret = ERR_PTR(-ENOMEM);
		goto bail;
	}

	rval = init_mregion(&mr->mr, pd, 0);
	if (rval) {
		ret = ERR_PTR(rval);
		goto bail;
	}


	rval = hfi1_alloc_lkey(&mr->mr, 1);
	if (rval) {
		ret = ERR_PTR(rval);
		goto bail_mregion;
	}

	mr->mr.access_flags = acc;
	ret = &mr->ibmr;
done:
	return ret;

bail_mregion:
	deinit_mregion(&mr->mr);
bail:
	kfree(mr);
	goto done;
}

static struct hfi1_mr *alloc_mr(int count, struct ib_pd *pd)
{
	struct hfi1_mr *mr;
	int rval = -ENOMEM;
	int m;

	/* Allocate struct plus pointers to first level page tables. */
	m = (count + HFI1_SEGSZ - 1) / HFI1_SEGSZ;
	mr = kzalloc(sizeof(*mr) + m * sizeof(mr->mr.map[0]), GFP_KERNEL);
	if (!mr)
		goto bail;

	rval = init_mregion(&mr->mr, pd, count);
	if (rval)
		goto bail;

	rval = hfi1_alloc_lkey(&mr->mr, 0);
	if (rval)
		goto bail_mregion;
	mr->ibmr.lkey = mr->mr.lkey;
	mr->ibmr.rkey = mr->mr.lkey;
done:
	return mr;

bail_mregion:
	deinit_mregion(&mr->mr);
bail:
	kfree(mr);
	mr = ERR_PTR(rval);
	goto done;
}

/**
 * hfi1_reg_user_mr - register a userspace memory region
 * @pd: protection domain for this memory region
 * @start: starting userspace address
 * @length: length of region to register
 * @mr_access_flags: access flags for this memory region
 * @udata: unused by the driver
 *
 * Returns the memory region on success, otherwise returns an errno.
 */
struct ib_mr *hfi1_reg_user_mr(struct ib_pd *pd, u64 start, u64 length,
			       u64 virt_addr, int mr_access_flags,
			       struct ib_udata *udata)
{
	struct hfi1_mr *mr;
	struct ib_umem *umem;
	struct scatterlist *sg;
	int n, m, entry;
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

	mr = alloc_mr(n, pd);
	if (IS_ERR(mr)) {
		ret = (struct ib_mr *)mr;
		ib_umem_release(umem);
		goto bail;
	}

	mr->mr.user_base = start;
	mr->mr.iova = virt_addr;
	mr->mr.length = length;
	mr->mr.offset = ib_umem_offset(umem);
	mr->mr.access_flags = mr_access_flags;
	mr->umem = umem;

	if (is_power_of_2(umem->page_size))
		mr->mr.page_shift = ilog2(umem->page_size);
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
		if (n == HFI1_SEGSZ) {
			m++;
			n = 0;
		}
	}
	ret = &mr->ibmr;

bail:
	return ret;
}

/**
 * hfi1_dereg_mr - unregister and free a memory region
 * @ibmr: the memory region to free
 *
 * Returns 0 on success.
 *
 * Note that this is called to free MRs created by hfi1_get_dma_mr()
 * or hfi1_reg_user_mr().
 */
int hfi1_dereg_mr(struct ib_mr *ibmr)
{
	struct hfi1_mr *mr = to_imr(ibmr);
	int ret = 0;
	unsigned long timeout;

	hfi1_free_lkey(&mr->mr);

	hfi1_put_mr(&mr->mr); /* will set completion if last */
	timeout = wait_for_completion_timeout(&mr->mr.comp,
		5 * HZ);
	if (!timeout) {
		dd_dev_err(
			dd_from_ibdev(mr->mr.pd->device),
			"hfi1_dereg_mr timeout mr %p pd %p refcount %u\n",
			mr, mr->mr.pd, atomic_read(&mr->mr.refcount));
		hfi1_get_mr(&mr->mr);
		ret = -EBUSY;
		goto out;
	}
	deinit_mregion(&mr->mr);
	if (mr->umem)
		ib_umem_release(mr->umem);
	kfree(mr);
out:
	return ret;
}

/*
 * Allocate a memory region usable with the
 * IB_WR_REG_MR send work request.
 *
 * Return the memory region on success, otherwise return an errno.
 * FIXME: IB_WR_REG_MR is not supported
 */
struct ib_mr *hfi1_alloc_mr(struct ib_pd *pd,
			    enum ib_mr_type mr_type,
			    u32 max_num_sg)
{
	struct hfi1_mr *mr;

	if (mr_type != IB_MR_TYPE_MEM_REG)
		return ERR_PTR(-EINVAL);

	mr = alloc_mr(max_num_sg, pd);
	if (IS_ERR(mr))
		return (struct ib_mr *)mr;

	return &mr->ibmr;
}

/**
 * hfi1_alloc_fmr - allocate a fast memory region
 * @pd: the protection domain for this memory region
 * @mr_access_flags: access flags for this memory region
 * @fmr_attr: fast memory region attributes
 *
 * Returns the memory region on success, otherwise returns an errno.
 */
struct ib_fmr *hfi1_alloc_fmr(struct ib_pd *pd, int mr_access_flags,
			      struct ib_fmr_attr *fmr_attr)
{
	struct hfi1_fmr *fmr;
	int m;
	struct ib_fmr *ret;
	int rval = -ENOMEM;

	/* Allocate struct plus pointers to first level page tables. */
	m = (fmr_attr->max_pages + HFI1_SEGSZ - 1) / HFI1_SEGSZ;
	fmr = kzalloc(sizeof(*fmr) + m * sizeof(fmr->mr.map[0]), GFP_KERNEL);
	if (!fmr)
		goto bail;

	rval = init_mregion(&fmr->mr, pd, fmr_attr->max_pages);
	if (rval)
		goto bail;

	/*
	 * ib_alloc_fmr() will initialize fmr->ibfmr except for lkey &
	 * rkey.
	 */
	rval = hfi1_alloc_lkey(&fmr->mr, 0);
	if (rval)
		goto bail_mregion;
	fmr->ibfmr.rkey = fmr->mr.lkey;
	fmr->ibfmr.lkey = fmr->mr.lkey;
	/*
	 * Resources are allocated but no valid mapping (RKEY can't be
	 * used).
	 */
	fmr->mr.access_flags = mr_access_flags;
	fmr->mr.max_segs = fmr_attr->max_pages;
	fmr->mr.page_shift = fmr_attr->page_shift;

	ret = &fmr->ibfmr;
done:
	return ret;

bail_mregion:
	deinit_mregion(&fmr->mr);
bail:
	kfree(fmr);
	ret = ERR_PTR(rval);
	goto done;
}

/**
 * hfi1_map_phys_fmr - set up a fast memory region
 * @ibmfr: the fast memory region to set up
 * @page_list: the list of pages to associate with the fast memory region
 * @list_len: the number of pages to associate with the fast memory region
 * @iova: the virtual address of the start of the fast memory region
 *
 * This may be called from interrupt context.
 */

int hfi1_map_phys_fmr(struct ib_fmr *ibfmr, u64 *page_list,
		      int list_len, u64 iova)
{
	struct hfi1_fmr *fmr = to_ifmr(ibfmr);
	struct hfi1_lkey_table *rkt;
	unsigned long flags;
	int m, n, i;
	u32 ps;
	int ret;

	i = atomic_read(&fmr->mr.refcount);
	if (i > 2)
		return -EBUSY;

	if (list_len > fmr->mr.max_segs) {
		ret = -EINVAL;
		goto bail;
	}
	rkt = &to_idev(ibfmr->device)->lk_table;
	spin_lock_irqsave(&rkt->lock, flags);
	fmr->mr.user_base = iova;
	fmr->mr.iova = iova;
	ps = 1 << fmr->mr.page_shift;
	fmr->mr.length = list_len * ps;
	m = 0;
	n = 0;
	for (i = 0; i < list_len; i++) {
		fmr->mr.map[m]->segs[n].vaddr = (void *) page_list[i];
		fmr->mr.map[m]->segs[n].length = ps;
		if (++n == HFI1_SEGSZ) {
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
 * hfi1_unmap_fmr - unmap fast memory regions
 * @fmr_list: the list of fast memory regions to unmap
 *
 * Returns 0 on success.
 */
int hfi1_unmap_fmr(struct list_head *fmr_list)
{
	struct hfi1_fmr *fmr;
	struct hfi1_lkey_table *rkt;
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
 * hfi1_dealloc_fmr - deallocate a fast memory region
 * @ibfmr: the fast memory region to deallocate
 *
 * Returns 0 on success.
 */
int hfi1_dealloc_fmr(struct ib_fmr *ibfmr)
{
	struct hfi1_fmr *fmr = to_ifmr(ibfmr);
	int ret = 0;
	unsigned long timeout;

	hfi1_free_lkey(&fmr->mr);
	hfi1_put_mr(&fmr->mr); /* will set completion if last */
	timeout = wait_for_completion_timeout(&fmr->mr.comp,
		5 * HZ);
	if (!timeout) {
		hfi1_get_mr(&fmr->mr);
		ret = -EBUSY;
		goto out;
	}
	deinit_mregion(&fmr->mr);
	kfree(fmr);
out:
	return ret;
}
