/*
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

#include <asm/io.h>

#include "ipath_verbs.h"

/**
 * ipath_alloc_lkey - allocate an lkey
 * @rkt: lkey table in which to allocate the lkey
 * @mr: memory region that this lkey protects
 *
 * Returns 1 if successful, otherwise returns 0.
 */

int ipath_alloc_lkey(struct ipath_lkey_table *rkt, struct ipath_mregion *mr)
{
	unsigned long flags;
	u32 r;
	u32 n;
	int ret;

	spin_lock_irqsave(&rkt->lock, flags);

	/* Find the next available LKEY */
	r = n = rkt->next;
	for (;;) {
		if (rkt->table[r] == NULL)
			break;
		r = (r + 1) & (rkt->max - 1);
		if (r == n) {
			spin_unlock_irqrestore(&rkt->lock, flags);
			_VERBS_INFO("LKEY table full\n");
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
	mr->lkey = (r << (32 - ib_ipath_lkey_table_size)) |
		((((1 << (24 - ib_ipath_lkey_table_size)) - 1) & rkt->gen)
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
 * ipath_free_lkey - free an lkey
 * @rkt: table from which to free the lkey
 * @lkey: lkey id to free
 */
void ipath_free_lkey(struct ipath_lkey_table *rkt, u32 lkey)
{
	unsigned long flags;
	u32 r;

	if (lkey == 0)
		return;
	r = lkey >> (32 - ib_ipath_lkey_table_size);
	spin_lock_irqsave(&rkt->lock, flags);
	rkt->table[r] = NULL;
	spin_unlock_irqrestore(&rkt->lock, flags);
}

/**
 * ipath_lkey_ok - check IB SGE for validity and initialize
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
int ipath_lkey_ok(struct ipath_lkey_table *rkt, struct ipath_sge *isge,
		  struct ib_sge *sge, int acc)
{
	struct ipath_mregion *mr;
	size_t off;
	int ret;

	/*
	 * We use LKEY == zero to mean a physical kmalloc() address.
	 * This is a bit of a hack since we rely on dma_map_single()
	 * being reversible by calling bus_to_virt().
	 */
	if (sge->lkey == 0) {
		isge->mr = NULL;
		isge->vaddr = bus_to_virt(sge->addr);
		isge->length = sge->length;
		isge->sge_length = sge->length;
		ret = 1;
		goto bail;
	}
	mr = rkt->table[(sge->lkey >> (32 - ib_ipath_lkey_table_size))];
	if (unlikely(mr == NULL || mr->lkey != sge->lkey)) {
		ret = 0;
		goto bail;
	}

	off = sge->addr - mr->user_base;
	if (unlikely(sge->addr < mr->user_base ||
		     off + sge->length > mr->length ||
		     (mr->access_flags & acc) != acc)) {
		ret = 0;
		goto bail;
	}

	off += mr->offset;
	isge->mr = mr;
	isge->m = 0;
	isge->n = 0;
	while (off >= mr->map[isge->m]->segs[isge->n].length) {
		off -= mr->map[isge->m]->segs[isge->n].length;
		isge->n++;
		if (isge->n >= IPATH_SEGSZ) {
			isge->m++;
			isge->n = 0;
		}
	}
	isge->vaddr = mr->map[isge->m]->segs[isge->n].vaddr + off;
	isge->length = mr->map[isge->m]->segs[isge->n].length - off;
	isge->sge_length = sge->length;

	ret = 1;

bail:
	return ret;
}

/**
 * ipath_rkey_ok - check the IB virtual address, length, and RKEY
 * @dev: infiniband device
 * @ss: SGE state
 * @len: length of data
 * @vaddr: virtual address to place data
 * @rkey: rkey to check
 * @acc: access flags
 *
 * Return 1 if successful, otherwise 0.
 */
int ipath_rkey_ok(struct ipath_ibdev *dev, struct ipath_sge_state *ss,
		  u32 len, u64 vaddr, u32 rkey, int acc)
{
	struct ipath_lkey_table *rkt = &dev->lk_table;
	struct ipath_sge *sge = &ss->sge;
	struct ipath_mregion *mr;
	size_t off;
	int ret;

	mr = rkt->table[(rkey >> (32 - ib_ipath_lkey_table_size))];
	if (unlikely(mr == NULL || mr->lkey != rkey)) {
		ret = 0;
		goto bail;
	}

	off = vaddr - mr->iova;
	if (unlikely(vaddr < mr->iova || off + len > mr->length ||
		     (mr->access_flags & acc) == 0)) {
		ret = 0;
		goto bail;
	}

	off += mr->offset;
	sge->mr = mr;
	sge->m = 0;
	sge->n = 0;
	while (off >= mr->map[sge->m]->segs[sge->n].length) {
		off -= mr->map[sge->m]->segs[sge->n].length;
		sge->n++;
		if (sge->n >= IPATH_SEGSZ) {
			sge->m++;
			sge->n = 0;
		}
	}
	sge->vaddr = mr->map[sge->m]->segs[sge->n].vaddr + off;
	sge->length = mr->map[sge->m]->segs[sge->n].length - off;
	sge->sge_length = len;
	ss->sg_list = NULL;
	ss->num_sge = 1;

	ret = 1;

bail:
	return ret;
}
