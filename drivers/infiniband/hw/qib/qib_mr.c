/*
 * Copyright (c) 2006, 2007, 2008, 2009 QLogic Corporation. All rights reserved.
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

#include <rdma/ib_umem.h>
#include <rdma/ib_smi.h>

#include "qib.h"

/* Fast memory region */
struct qib_fmr {
	struct ib_fmr ibfmr;
	struct qib_mregion mr;        /* must be last */
};

static inline struct qib_fmr *to_ifmr(struct ib_fmr *ibfmr)
{
	return container_of(ibfmr, struct qib_fmr, ibfmr);
}

static int init_qib_mregion(struct qib_mregion *mr, struct ib_pd *pd,
	int count)
{
	int m, i = 0;
	int rval = 0;

	m = (count + QIB_SEGSZ - 1) / QIB_SEGSZ;
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

static void deinit_qib_mregion(struct qib_mregion *mr)
{
	int i = mr->mapsz;

	mr->mapsz = 0;
	while (i)
		kfree(mr->map[--i]);
}


/**
 * qib_get_dma_mr - get a DMA memory region
 * @pd: protection domain for this memory region
 * @acc: access flags
 *
 * Returns the memory region on success, otherwise returns an errno.
 * Note that all DMA addresses should be created via the
 * struct ib_dma_mapping_ops functions (see qib_dma.c).
 */
struct ib_mr *qib_get_dma_mr(struct ib_pd *pd, int acc)
{
	struct qib_mr *mr = NULL;
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

	rval = init_qib_mregion(&mr->mr, pd, 0);
	if (rval) {
		ret = ERR_PTR(rval);
		goto bail;
	}


	rval = qib_alloc_lkey(&mr->mr, 1);
	if (rval) {
		ret = ERR_PTR(rval);
		goto bail_mregion;
	}

	mr->mr.access_flags = acc;
	ret = &mr->ibmr;
done:
	return ret;

bail_mregion:
	deinit_qib_mregion(&mr->mr);
bail:
	kfree(mr);
	goto done;
}

static struct qib_mr *alloc_mr(int count, struct ib_pd *pd)
{
	struct qib_mr *mr;
	int rval = -ENOMEM;
	int m;

	/* Allocate struct plus pointers to first level page tables. */
	m = (count + QIB_SEGSZ - 1) / QIB_SEGSZ;
	mr = kzalloc(sizeof(*mr) + m * sizeof(mr->mr.map[0]), GFP_KERNEL);
	if (!mr)
		goto bail;

	rval = init_qib_mregion(&mr->mr, pd, count);
	if (rval)
		goto bail;
	/*
	 * ib_reg_phys_mr() will initialize mr->ibmr except for
	 * lkey and rkey.
	 */
	rval = qib_alloc_lkey(&mr->mr, 0);
	if (rval)
		goto bail_mregion;
	mr->ibmr.lkey = mr->mr.lkey;
	mr->ibmr.rkey = mr->mr.lkey;
done:
	return mr;

bail_mregion:
	deinit_qib_mregion(&mr->mr);
bail:
	kfree(mr);
	mr = ERR_PTR(rval);
	goto done;
}

/**
 * qib_reg_phys_mr - register a physical memory region
 * @pd: protection domain for this memory region
 * @buffer_list: pointer to the list of physical buffers to register
 * @num_phys_buf: the number of physical buffers to register
 * @iova_start: the starting address passed over IB which maps to this MR
 *
 * Returns the memory region on success, otherwise returns an errno.
 */
struct ib_mr *qib_reg_phys_mr(struct ib_pd *pd,
			      struct ib_phys_buf *buffer_list,
			      int num_phys_buf, int acc, u64 *iova_start)
{
	struct qib_mr *mr;
	int n, m, i;
	struct ib_mr *ret;

	mr = alloc_mr(num_phys_buf, pd);
	if (IS_ERR(mr)) {
		ret = (struct ib_mr *)mr;
		goto bail;
	}

	mr->mr.user_base = *iova_start;
	mr->mr.iova = *iova_start;
	mr->mr.access_flags = acc;

	m = 0;
	n = 0;
	for (i = 0; i < num_phys_buf; i++) {
		mr->mr.map[m]->segs[n].vaddr = (void *) buffer_list[i].addr;
		mr->mr.map[m]->segs[n].length = buffer_list[i].size;
		mr->mr.length += buffer_list[i].size;
		n++;
		if (n == QIB_SEGSZ) {
			m++;
			n = 0;
		}
	}

	ret = &mr->ibmr;

bail:
	return ret;
}

/**
 * qib_reg_user_mr - register a userspace memory region
 * @pd: protection domain for this memory region
 * @start: starting userspace address
 * @length: length of region to register
 * @mr_access_flags: access flags for this memory region
 * @udata: unused by the QLogic_IB driver
 *
 * Returns the memory region on success, otherwise returns an errno.
 */
struct ib_mr *qib_reg_user_mr(struct ib_pd *pd, u64 start, u64 length,
			      u64 virt_addr, int mr_access_flags,
			      struct ib_udata *udata)
{
	struct qib_mr *mr;
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
			if (n == QIB_SEGSZ) {
				m++;
				n = 0;
			}
	}
	ret = &mr->ibmr;

bail:
	return ret;
}

/**
 * qib_dereg_mr - unregister and free a memory region
 * @ibmr: the memory region to free
 *
 * Returns 0 on success.
 *
 * Note that this is called to free MRs created by qib_get_dma_mr()
 * or qib_reg_user_mr().
 */
int qib_dereg_mr(struct ib_mr *ibmr)
{
	struct qib_mr *mr = to_imr(ibmr);
	int ret = 0;
	unsigned long timeout;

	qib_free_lkey(&mr->mr);

	qib_put_mr(&mr->mr); /* will set completion if last */
	timeout = wait_for_completion_timeout(&mr->mr.comp,
		5 * HZ);
	if (!timeout) {
		qib_get_mr(&mr->mr);
		ret = -EBUSY;
		goto out;
	}
	deinit_qib_mregion(&mr->mr);
	if (mr->umem)
		ib_umem_release(mr->umem);
	kfree(mr);
out:
	return ret;
}

/*
 * Allocate a memory region usable with the
 * IB_WR_FAST_REG_MR send work request.
 *
 * Return the memory region on success, otherwise return an errno.
 */
struct ib_mr *qib_alloc_mr(struct ib_pd *pd,
			   enum ib_mr_type mr_type,
			   u32 max_num_sg)
{
	struct qib_mr *mr;

	if (mr_type != IB_MR_TYPE_MEM_REG)
		return ERR_PTR(-EINVAL);

	mr = alloc_mr(max_num_sg, pd);
	if (IS_ERR(mr))
		return (struct ib_mr *)mr;

	return &mr->ibmr;
}

struct ib_fast_reg_page_list *
qib_alloc_fast_reg_page_list(struct ib_device *ibdev, int page_list_len)
{
	unsigned size = page_list_len * sizeof(u64);
	struct ib_fast_reg_page_list *pl;

	if (size > PAGE_SIZE)
		return ERR_PTR(-EINVAL);

	pl = kzalloc(sizeof(*pl), GFP_KERNEL);
	if (!pl)
		return ERR_PTR(-ENOMEM);

	pl->page_list = kzalloc(size, GFP_KERNEL);
	if (!pl->page_list)
		goto err_free;

	return pl;

err_free:
	kfree(pl);
	return ERR_PTR(-ENOMEM);
}

void qib_free_fast_reg_page_list(struct ib_fast_reg_page_list *pl)
{
	kfree(pl->page_list);
	kfree(pl);
}

/**
 * qib_alloc_fmr - allocate a fast memory region
 * @pd: the protection domain for this memory region
 * @mr_access_flags: access flags for this memory region
 * @fmr_attr: fast memory region attributes
 *
 * Returns the memory region on success, otherwise returns an errno.
 */
struct ib_fmr *qib_alloc_fmr(struct ib_pd *pd, int mr_access_flags,
			     struct ib_fmr_attr *fmr_attr)
{
	struct qib_fmr *fmr;
	int m;
	struct ib_fmr *ret;
	int rval = -ENOMEM;

	/* Allocate struct plus pointers to first level page tables. */
	m = (fmr_attr->max_pages + QIB_SEGSZ - 1) / QIB_SEGSZ;
	fmr = kzalloc(sizeof(*fmr) + m * sizeof(fmr->mr.map[0]), GFP_KERNEL);
	if (!fmr)
		goto bail;

	rval = init_qib_mregion(&fmr->mr, pd, fmr_attr->max_pages);
	if (rval)
		goto bail;

	/*
	 * ib_alloc_fmr() will initialize fmr->ibfmr except for lkey &
	 * rkey.
	 */
	rval = qib_alloc_lkey(&fmr->mr, 0);
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
	deinit_qib_mregion(&fmr->mr);
bail:
	kfree(fmr);
	ret = ERR_PTR(rval);
	goto done;
}

/**
 * qib_map_phys_fmr - set up a fast memory region
 * @ibmfr: the fast memory region to set up
 * @page_list: the list of pages to associate with the fast memory region
 * @list_len: the number of pages to associate with the fast memory region
 * @iova: the virtual address of the start of the fast memory region
 *
 * This may be called from interrupt context.
 */

int qib_map_phys_fmr(struct ib_fmr *ibfmr, u64 *page_list,
		     int list_len, u64 iova)
{
	struct qib_fmr *fmr = to_ifmr(ibfmr);
	struct qib_lkey_table *rkt;
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
		if (++n == QIB_SEGSZ) {
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
 * qib_unmap_fmr - unmap fast memory regions
 * @fmr_list: the list of fast memory regions to unmap
 *
 * Returns 0 on success.
 */
int qib_unmap_fmr(struct list_head *fmr_list)
{
	struct qib_fmr *fmr;
	struct qib_lkey_table *rkt;
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
 * qib_dealloc_fmr - deallocate a fast memory region
 * @ibfmr: the fast memory region to deallocate
 *
 * Returns 0 on success.
 */
int qib_dealloc_fmr(struct ib_fmr *ibfmr)
{
	struct qib_fmr *fmr = to_ifmr(ibfmr);
	int ret = 0;
	unsigned long timeout;

	qib_free_lkey(&fmr->mr);
	qib_put_mr(&fmr->mr); /* will set completion if last */
	timeout = wait_for_completion_timeout(&fmr->mr.comp,
		5 * HZ);
	if (!timeout) {
		qib_get_mr(&fmr->mr);
		ret = -EBUSY;
		goto out;
	}
	deinit_qib_mregion(&fmr->mr);
	kfree(fmr);
out:
	return ret;
}

void mr_rcu_callback(struct rcu_head *list)
{
	struct qib_mregion *mr = container_of(list, struct qib_mregion, list);

	complete(&mr->comp);
}
