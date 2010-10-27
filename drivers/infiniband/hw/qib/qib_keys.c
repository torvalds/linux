/*
 * Copyright (c) 2006, 2007, 2009 QLogic Corporation. All rights reserved.
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

#include "qib.h"

/**
 * qib_alloc_lkey - allocate an lkey
 * @rkt: lkey table in which to allocate the lkey
 * @mr: memory region that this lkey protects
 *
 * Returns 1 if successful, otherwise returns 0.
 */

int qib_alloc_lkey(struct qib_lkey_table *rkt, struct qib_mregion *mr)
{
	unsigned long flags;
	u32 r;
	u32 n;
	int ret;

	spin_lock_irqsave(&rkt->lock, flags);

	/* Find the next available LKEY */
	r = rkt->next;
	n = r;
	for (;;) {
		if (rkt->table[r] == NULL)
			break;
		r = (r + 1) & (rkt->max - 1);
		if (r == n) {
			spin_unlock_irqrestore(&rkt->lock, flags);
			ret = 0;
			goto bail;
		}
	}
	rkt->next = (r + 1) & (rkt->max - 1);
	/*
	 * Make sure lkey is never zero which is reserved to indicate an
	 * unrestricted LKEY.
	 */
	rkt->gen++;
	mr->lkey = (r << (32 - ib_qib_lkey_table_size)) |
		((((1 << (24 - ib_qib_lkey_table_size)) - 1) & rkt->gen)
		 << 8);
	if (mr->lkey == 0) {
		mr->lkey |= 1 << 8;
		rkt->gen++;
	}
	rkt->table[r] = mr;
	spin_unlock_irqrestore(&rkt->lock, flags);

	ret = 1;

bail:
	return ret;
}

/**
 * qib_free_lkey - free an lkey
 * @rkt: table from which to free the lkey
 * @lkey: lkey id to free
 */
int qib_free_lkey(struct qib_ibdev *dev, struct qib_mregion *mr)
{
	unsigned long flags;
	u32 lkey = mr->lkey;
	u32 r;
	int ret;

	spin_lock_irqsave(&dev->lk_table.lock, flags);
	if (lkey == 0) {
		if (dev->dma_mr && dev->dma_mr == mr) {
			ret = atomic_read(&dev->dma_mr->refcount);
			if (!ret)
				dev->dma_mr = NULL;
		} else
			ret = 0;
	} else {
		r = lkey >> (32 - ib_qib_lkey_table_size);
		ret = atomic_read(&dev->lk_table.table[r]->refcount);
		if (!ret)
			dev->lk_table.table[r] = NULL;
	}
	spin_unlock_irqrestore(&dev->lk_table.lock, flags);

	if (ret)
		ret = -EBUSY;
	return ret;
}

/**
 * qib_lkey_ok - check IB SGE for validity and initialize
 * @rkt: table containing lkey to check SGE against
 * @isge: outgoing internal SGE
 * @sge: SGE to check
 * @acc: access flags
 *
 * Return 1 if valid and successful, otherwise returns 0.
 *
 * Check the IB SGE for validity and initialize our internal version
 * of it.
 */
int qib_lkey_ok(struct qib_lkey_table *rkt, struct qib_pd *pd,
		struct qib_sge *isge, struct ib_sge *sge, int acc)
{
	struct qib_mregion *mr;
	unsigned n, m;
	size_t off;
	int ret = 0;
	unsigned long flags;

	/*
	 * We use LKEY == zero for kernel virtual addresses
	 * (see qib_get_dma_mr and qib_dma.c).
	 */
	spin_lock_irqsave(&rkt->lock, flags);
	if (sge->lkey == 0) {
		struct qib_ibdev *dev = to_idev(pd->ibpd.device);

		if (pd->user)
			goto bail;
		if (!dev->dma_mr)
			goto bail;
		atomic_inc(&dev->dma_mr->refcount);
		isge->mr = dev->dma_mr;
		isge->vaddr = (void *) sge->addr;
		isge->length = sge->length;
		isge->sge_length = sge->length;
		isge->m = 0;
		isge->n = 0;
		goto ok;
	}
	mr = rkt->table[(sge->lkey >> (32 - ib_qib_lkey_table_size))];
	if (unlikely(mr == NULL || mr->lkey != sge->lkey ||
		     mr->pd != &pd->ibpd))
		goto bail;

	off = sge->addr - mr->user_base;
	if (unlikely(sge->addr < mr->user_base ||
		     off + sge->length > mr->length ||
		     (mr->access_flags & acc) != acc))
		goto bail;

	off += mr->offset;
	m = 0;
	n = 0;
	while (off >= mr->map[m]->segs[n].length) {
		off -= mr->map[m]->segs[n].length;
		n++;
		if (n >= QIB_SEGSZ) {
			m++;
			n = 0;
		}
	}
	atomic_inc(&mr->refcount);
	isge->mr = mr;
	isge->vaddr = mr->map[m]->segs[n].vaddr + off;
	isge->length = mr->map[m]->segs[n].length - off;
	isge->sge_length = sge->length;
	isge->m = m;
	isge->n = n;
ok:
	ret = 1;
bail:
	spin_unlock_irqrestore(&rkt->lock, flags);
	return ret;
}

/**
 * qib_rkey_ok - check the IB virtual address, length, and RKEY
 * @dev: infiniband device
 * @ss: SGE state
 * @len: length of data
 * @vaddr: virtual address to place data
 * @rkey: rkey to check
 * @acc: access flags
 *
 * Return 1 if successful, otherwise 0.
 */
int qib_rkey_ok(struct qib_qp *qp, struct qib_sge *sge,
		u32 len, u64 vaddr, u32 rkey, int acc)
{
	struct qib_lkey_table *rkt = &to_idev(qp->ibqp.device)->lk_table;
	struct qib_mregion *mr;
	unsigned n, m;
	size_t off;
	int ret = 0;
	unsigned long flags;

	/*
	 * We use RKEY == zero for kernel virtual addresses
	 * (see qib_get_dma_mr and qib_dma.c).
	 */
	spin_lock_irqsave(&rkt->lock, flags);
	if (rkey == 0) {
		struct qib_pd *pd = to_ipd(qp->ibqp.pd);
		struct qib_ibdev *dev = to_idev(pd->ibpd.device);

		if (pd->user)
			goto bail;
		if (!dev->dma_mr)
			goto bail;
		atomic_inc(&dev->dma_mr->refcount);
		sge->mr = dev->dma_mr;
		sge->vaddr = (void *) vaddr;
		sge->length = len;
		sge->sge_length = len;
		sge->m = 0;
		sge->n = 0;
		goto ok;
	}

	mr = rkt->table[(rkey >> (32 - ib_qib_lkey_table_size))];
	if (unlikely(mr == NULL || mr->lkey != rkey || qp->ibqp.pd != mr->pd))
		goto bail;

	off = vaddr - mr->iova;
	if (unlikely(vaddr < mr->iova || off + len > mr->length ||
		     (mr->access_flags & acc) == 0))
		goto bail;

	off += mr->offset;
	m = 0;
	n = 0;
	while (off >= mr->map[m]->segs[n].length) {
		off -= mr->map[m]->segs[n].length;
		n++;
		if (n >= QIB_SEGSZ) {
			m++;
			n = 0;
		}
	}
	atomic_inc(&mr->refcount);
	sge->mr = mr;
	sge->vaddr = mr->map[m]->segs[n].vaddr + off;
	sge->length = mr->map[m]->segs[n].length - off;
	sge->sge_length = len;
	sge->m = m;
	sge->n = n;
ok:
	ret = 1;
bail:
	spin_unlock_irqrestore(&rkt->lock, flags);
	return ret;
}

/*
 * Initialize the memory region specified by the work reqeust.
 */
int qib_fast_reg_mr(struct qib_qp *qp, struct ib_send_wr *wr)
{
	struct qib_lkey_table *rkt = &to_idev(qp->ibqp.device)->lk_table;
	struct qib_pd *pd = to_ipd(qp->ibqp.pd);
	struct qib_mregion *mr;
	u32 rkey = wr->wr.fast_reg.rkey;
	unsigned i, n, m;
	int ret = -EINVAL;
	unsigned long flags;
	u64 *page_list;
	size_t ps;

	spin_lock_irqsave(&rkt->lock, flags);
	if (pd->user || rkey == 0)
		goto bail;

	mr = rkt->table[(rkey >> (32 - ib_qib_lkey_table_size))];
	if (unlikely(mr == NULL || qp->ibqp.pd != mr->pd))
		goto bail;

	if (wr->wr.fast_reg.page_list_len > mr->max_segs)
		goto bail;

	ps = 1UL << wr->wr.fast_reg.page_shift;
	if (wr->wr.fast_reg.length > ps * wr->wr.fast_reg.page_list_len)
		goto bail;

	mr->user_base = wr->wr.fast_reg.iova_start;
	mr->iova = wr->wr.fast_reg.iova_start;
	mr->lkey = rkey;
	mr->length = wr->wr.fast_reg.length;
	mr->access_flags = wr->wr.fast_reg.access_flags;
	page_list = wr->wr.fast_reg.page_list->page_list;
	m = 0;
	n = 0;
	for (i = 0; i < wr->wr.fast_reg.page_list_len; i++) {
		mr->map[m]->segs[n].vaddr = (void *) page_list[i];
		mr->map[m]->segs[n].length = ps;
		if (++n == QIB_SEGSZ) {
			m++;
			n = 0;
		}
	}

	ret = 0;
bail:
	spin_unlock_irqrestore(&rkt->lock, flags);
	return ret;
}
