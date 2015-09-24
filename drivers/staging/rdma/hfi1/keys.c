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

#include "hfi.h"

/**
 * hfi1_alloc_lkey - allocate an lkey
 * @mr: memory region that this lkey protects
 * @dma_region: 0->normal key, 1->restricted DMA key
 *
 * Returns 0 if successful, otherwise returns -errno.
 *
 * Increments mr reference count as required.
 *
 * Sets the lkey field mr for non-dma regions.
 *
 */

int hfi1_alloc_lkey(struct hfi1_mregion *mr, int dma_region)
{
	unsigned long flags;
	u32 r;
	u32 n;
	int ret = 0;
	struct hfi1_ibdev *dev = to_idev(mr->pd->device);
	struct hfi1_lkey_table *rkt = &dev->lk_table;

	hfi1_get_mr(mr);
	spin_lock_irqsave(&rkt->lock, flags);

	/* special case for dma_mr lkey == 0 */
	if (dma_region) {
		struct hfi1_mregion *tmr;

		tmr = rcu_access_pointer(dev->dma_mr);
		if (!tmr) {
			rcu_assign_pointer(dev->dma_mr, mr);
			mr->lkey_published = 1;
		} else {
			hfi1_put_mr(mr);
		}
		goto success;
	}

	/* Find the next available LKEY */
	r = rkt->next;
	n = r;
	for (;;) {
		if (!rcu_access_pointer(rkt->table[r]))
			break;
		r = (r + 1) & (rkt->max - 1);
		if (r == n)
			goto bail;
	}
	rkt->next = (r + 1) & (rkt->max - 1);
	/*
	 * Make sure lkey is never zero which is reserved to indicate an
	 * unrestricted LKEY.
	 */
	rkt->gen++;
	/*
	 * bits are capped in verbs.c to ensure enough bits for
	 * generation number
	 */
	mr->lkey = (r << (32 - hfi1_lkey_table_size)) |
		((((1 << (24 - hfi1_lkey_table_size)) - 1) & rkt->gen)
		 << 8);
	if (mr->lkey == 0) {
		mr->lkey |= 1 << 8;
		rkt->gen++;
	}
	rcu_assign_pointer(rkt->table[r], mr);
	mr->lkey_published = 1;
success:
	spin_unlock_irqrestore(&rkt->lock, flags);
out:
	return ret;
bail:
	hfi1_put_mr(mr);
	spin_unlock_irqrestore(&rkt->lock, flags);
	ret = -ENOMEM;
	goto out;
}

/**
 * hfi1_free_lkey - free an lkey
 * @mr: mr to free from tables
 */
void hfi1_free_lkey(struct hfi1_mregion *mr)
{
	unsigned long flags;
	u32 lkey = mr->lkey;
	u32 r;
	struct hfi1_ibdev *dev = to_idev(mr->pd->device);
	struct hfi1_lkey_table *rkt = &dev->lk_table;
	int freed = 0;

	spin_lock_irqsave(&rkt->lock, flags);
	if (!mr->lkey_published)
		goto out;
	if (lkey == 0)
		RCU_INIT_POINTER(dev->dma_mr, NULL);
	else {
		r = lkey >> (32 - hfi1_lkey_table_size);
		RCU_INIT_POINTER(rkt->table[r], NULL);
	}
	mr->lkey_published = 0;
	freed++;
out:
	spin_unlock_irqrestore(&rkt->lock, flags);
	if (freed) {
		synchronize_rcu();
		hfi1_put_mr(mr);
	}
}

/**
 * hfi1_lkey_ok - check IB SGE for validity and initialize
 * @rkt: table containing lkey to check SGE against
 * @pd: protection domain
 * @isge: outgoing internal SGE
 * @sge: SGE to check
 * @acc: access flags
 *
 * Return 1 if valid and successful, otherwise returns 0.
 *
 * increments the reference count upon success
 *
 * Check the IB SGE for validity and initialize our internal version
 * of it.
 */
int hfi1_lkey_ok(struct hfi1_lkey_table *rkt, struct hfi1_pd *pd,
		 struct hfi1_sge *isge, struct ib_sge *sge, int acc)
{
	struct hfi1_mregion *mr;
	unsigned n, m;
	size_t off;

	/*
	 * We use LKEY == zero for kernel virtual addresses
	 * (see hfi1_get_dma_mr and dma.c).
	 */
	rcu_read_lock();
	if (sge->lkey == 0) {
		struct hfi1_ibdev *dev = to_idev(pd->ibpd.device);

		if (pd->user)
			goto bail;
		mr = rcu_dereference(dev->dma_mr);
		if (!mr)
			goto bail;
		atomic_inc(&mr->refcount);
		rcu_read_unlock();

		isge->mr = mr;
		isge->vaddr = (void *) sge->addr;
		isge->length = sge->length;
		isge->sge_length = sge->length;
		isge->m = 0;
		isge->n = 0;
		goto ok;
	}
	mr = rcu_dereference(
		rkt->table[(sge->lkey >> (32 - hfi1_lkey_table_size))]);
	if (unlikely(!mr || mr->lkey != sge->lkey || mr->pd != &pd->ibpd))
		goto bail;

	off = sge->addr - mr->user_base;
	if (unlikely(sge->addr < mr->user_base ||
		     off + sge->length > mr->length ||
		     (mr->access_flags & acc) != acc))
		goto bail;
	atomic_inc(&mr->refcount);
	rcu_read_unlock();

	off += mr->offset;
	if (mr->page_shift) {
		/*
		page sizes are uniform power of 2 so no loop is necessary
		entries_spanned_by_off is the number of times the loop below
		would have executed.
		*/
		size_t entries_spanned_by_off;

		entries_spanned_by_off = off >> mr->page_shift;
		off -= (entries_spanned_by_off << mr->page_shift);
		m = entries_spanned_by_off / HFI1_SEGSZ;
		n = entries_spanned_by_off % HFI1_SEGSZ;
	} else {
		m = 0;
		n = 0;
		while (off >= mr->map[m]->segs[n].length) {
			off -= mr->map[m]->segs[n].length;
			n++;
			if (n >= HFI1_SEGSZ) {
				m++;
				n = 0;
			}
		}
	}
	isge->mr = mr;
	isge->vaddr = mr->map[m]->segs[n].vaddr + off;
	isge->length = mr->map[m]->segs[n].length - off;
	isge->sge_length = sge->length;
	isge->m = m;
	isge->n = n;
ok:
	return 1;
bail:
	rcu_read_unlock();
	return 0;
}

/**
 * hfi1_rkey_ok - check the IB virtual address, length, and RKEY
 * @qp: qp for validation
 * @sge: SGE state
 * @len: length of data
 * @vaddr: virtual address to place data
 * @rkey: rkey to check
 * @acc: access flags
 *
 * Return 1 if successful, otherwise 0.
 *
 * increments the reference count upon success
 */
int hfi1_rkey_ok(struct hfi1_qp *qp, struct hfi1_sge *sge,
		 u32 len, u64 vaddr, u32 rkey, int acc)
{
	struct hfi1_lkey_table *rkt = &to_idev(qp->ibqp.device)->lk_table;
	struct hfi1_mregion *mr;
	unsigned n, m;
	size_t off;

	/*
	 * We use RKEY == zero for kernel virtual addresses
	 * (see hfi1_get_dma_mr and dma.c).
	 */
	rcu_read_lock();
	if (rkey == 0) {
		struct hfi1_pd *pd = to_ipd(qp->ibqp.pd);
		struct hfi1_ibdev *dev = to_idev(pd->ibpd.device);

		if (pd->user)
			goto bail;
		mr = rcu_dereference(dev->dma_mr);
		if (!mr)
			goto bail;
		atomic_inc(&mr->refcount);
		rcu_read_unlock();

		sge->mr = mr;
		sge->vaddr = (void *) vaddr;
		sge->length = len;
		sge->sge_length = len;
		sge->m = 0;
		sge->n = 0;
		goto ok;
	}

	mr = rcu_dereference(
		rkt->table[(rkey >> (32 - hfi1_lkey_table_size))]);
	if (unlikely(!mr || mr->lkey != rkey || qp->ibqp.pd != mr->pd))
		goto bail;

	off = vaddr - mr->iova;
	if (unlikely(vaddr < mr->iova || off + len > mr->length ||
		     (mr->access_flags & acc) == 0))
		goto bail;
	atomic_inc(&mr->refcount);
	rcu_read_unlock();

	off += mr->offset;
	if (mr->page_shift) {
		/*
		page sizes are uniform power of 2 so no loop is necessary
		entries_spanned_by_off is the number of times the loop below
		would have executed.
		*/
		size_t entries_spanned_by_off;

		entries_spanned_by_off = off >> mr->page_shift;
		off -= (entries_spanned_by_off << mr->page_shift);
		m = entries_spanned_by_off / HFI1_SEGSZ;
		n = entries_spanned_by_off % HFI1_SEGSZ;
	} else {
		m = 0;
		n = 0;
		while (off >= mr->map[m]->segs[n].length) {
			off -= mr->map[m]->segs[n].length;
			n++;
			if (n >= HFI1_SEGSZ) {
				m++;
				n = 0;
			}
		}
	}
	sge->mr = mr;
	sge->vaddr = mr->map[m]->segs[n].vaddr + off;
	sge->length = mr->map[m]->segs[n].length - off;
	sge->sge_length = len;
	sge->m = m;
	sge->n = n;
ok:
	return 1;
bail:
	rcu_read_unlock();
	return 0;
}

/*
 * Initialize the memory region specified by the work request.
 */
int hfi1_fast_reg_mr(struct hfi1_qp *qp, struct ib_send_wr *wr)
{
	struct hfi1_lkey_table *rkt = &to_idev(qp->ibqp.device)->lk_table;
	struct hfi1_pd *pd = to_ipd(qp->ibqp.pd);
	struct hfi1_mregion *mr;
	u32 rkey = wr->wr.fast_reg.rkey;
	unsigned i, n, m;
	int ret = -EINVAL;
	unsigned long flags;
	u64 *page_list;
	size_t ps;

	spin_lock_irqsave(&rkt->lock, flags);
	if (pd->user || rkey == 0)
		goto bail;

	mr = rcu_dereference_protected(
		rkt->table[(rkey >> (32 - hfi1_lkey_table_size))],
		lockdep_is_held(&rkt->lock));
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
		if (++n == HFI1_SEGSZ) {
			m++;
			n = 0;
		}
	}

	ret = 0;
bail:
	spin_unlock_irqrestore(&rkt->lock, flags);
	return ret;
}
