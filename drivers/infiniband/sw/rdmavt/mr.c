/*
 * Copyright(c) 2016 Intel Corporation.
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
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

#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <rdma/ib_umem.h>
#include <rdma/rdma_vt.h>
#include "vt.h"
#include "mr.h"
#include "trace.h"

/**
 * rvt_driver_mr_init - Init MR resources per driver
 * @rdi: rvt dev struct
 *
 * Do any intilization needed when a driver registers with rdmavt.
 *
 * Return: 0 on success or errno on failure
 */
int rvt_driver_mr_init(struct rvt_dev_info *rdi)
{
	unsigned int lkey_table_size = rdi->dparms.lkey_table_size;
	unsigned lk_tab_size;
	int i;

	/*
	 * The top hfi1_lkey_table_size bits are used to index the
	 * table.  The lower 8 bits can be owned by the user (copied from
	 * the LKEY).  The remaining bits act as a generation number or tag.
	 */
	if (!lkey_table_size)
		return -EINVAL;

	spin_lock_init(&rdi->lkey_table.lock);

	/* ensure generation is at least 4 bits */
	if (lkey_table_size > RVT_MAX_LKEY_TABLE_BITS) {
		rvt_pr_warn(rdi, "lkey bits %u too large, reduced to %u\n",
			    lkey_table_size, RVT_MAX_LKEY_TABLE_BITS);
		rdi->dparms.lkey_table_size = RVT_MAX_LKEY_TABLE_BITS;
		lkey_table_size = rdi->dparms.lkey_table_size;
	}
	rdi->lkey_table.max = 1 << lkey_table_size;
	rdi->lkey_table.shift = 32 - lkey_table_size;
	lk_tab_size = rdi->lkey_table.max * sizeof(*rdi->lkey_table.table);
	rdi->lkey_table.table = (struct rvt_mregion __rcu **)
			       vmalloc_node(lk_tab_size, rdi->dparms.node);
	if (!rdi->lkey_table.table)
		return -ENOMEM;

	RCU_INIT_POINTER(rdi->dma_mr, NULL);
	for (i = 0; i < rdi->lkey_table.max; i++)
		RCU_INIT_POINTER(rdi->lkey_table.table[i], NULL);

	return 0;
}

/**
 *rvt_mr_exit: clean up MR
 *@rdi: rvt dev structure
 *
 * called when drivers have unregistered or perhaps failed to register with us
 */
void rvt_mr_exit(struct rvt_dev_info *rdi)
{
	if (rdi->dma_mr)
		rvt_pr_err(rdi, "DMA MR not null!\n");

	vfree(rdi->lkey_table.table);
}

static void rvt_deinit_mregion(struct rvt_mregion *mr)
{
	int i = mr->mapsz;

	mr->mapsz = 0;
	while (i)
		kfree(mr->map[--i]);
}

static int rvt_init_mregion(struct rvt_mregion *mr, struct ib_pd *pd,
			    int count)
{
	int m, i = 0;
	struct rvt_dev_info *dev = ib_to_rvt(pd->device);

	mr->mapsz = 0;
	m = (count + RVT_SEGSZ - 1) / RVT_SEGSZ;
	for (; i < m; i++) {
		mr->map[i] = kzalloc_node(sizeof(*mr->map[0]), GFP_KERNEL,
					  dev->dparms.node);
		if (!mr->map[i]) {
			rvt_deinit_mregion(mr);
			return -ENOMEM;
		}
		mr->mapsz++;
	}
	init_completion(&mr->comp);
	/* count returning the ptr to user */
	atomic_set(&mr->refcount, 1);
	atomic_set(&mr->lkey_invalid, 0);
	mr->pd = pd;
	mr->max_segs = count;
	return 0;
}

/**
 * rvt_alloc_lkey - allocate an lkey
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
static int rvt_alloc_lkey(struct rvt_mregion *mr, int dma_region)
{
	unsigned long flags;
	u32 r;
	u32 n;
	int ret = 0;
	struct rvt_dev_info *dev = ib_to_rvt(mr->pd->device);
	struct rvt_lkey_table *rkt = &dev->lkey_table;

	rvt_get_mr(mr);
	spin_lock_irqsave(&rkt->lock, flags);

	/* special case for dma_mr lkey == 0 */
	if (dma_region) {
		struct rvt_mregion *tmr;

		tmr = rcu_access_pointer(dev->dma_mr);
		if (!tmr) {
			rcu_assign_pointer(dev->dma_mr, mr);
			mr->lkey_published = 1;
		} else {
			rvt_put_mr(mr);
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
	 * bits are capped to ensure enough bits for generation number
	 */
	mr->lkey = (r << (32 - dev->dparms.lkey_table_size)) |
		((((1 << (24 - dev->dparms.lkey_table_size)) - 1) & rkt->gen)
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
	rvt_put_mr(mr);
	spin_unlock_irqrestore(&rkt->lock, flags);
	ret = -ENOMEM;
	goto out;
}

/**
 * rvt_free_lkey - free an lkey
 * @mr: mr to free from tables
 */
static void rvt_free_lkey(struct rvt_mregion *mr)
{
	unsigned long flags;
	u32 lkey = mr->lkey;
	u32 r;
	struct rvt_dev_info *dev = ib_to_rvt(mr->pd->device);
	struct rvt_lkey_table *rkt = &dev->lkey_table;
	int freed = 0;

	spin_lock_irqsave(&rkt->lock, flags);
	if (!mr->lkey_published)
		goto out;
	if (lkey == 0) {
		RCU_INIT_POINTER(dev->dma_mr, NULL);
	} else {
		r = lkey >> (32 - dev->dparms.lkey_table_size);
		RCU_INIT_POINTER(rkt->table[r], NULL);
	}
	mr->lkey_published = 0;
	freed++;
out:
	spin_unlock_irqrestore(&rkt->lock, flags);
	if (freed) {
		synchronize_rcu();
		rvt_put_mr(mr);
	}
}

static struct rvt_mr *__rvt_alloc_mr(int count, struct ib_pd *pd)
{
	struct rvt_mr *mr;
	int rval = -ENOMEM;
	int m;

	/* Allocate struct plus pointers to first level page tables. */
	m = (count + RVT_SEGSZ - 1) / RVT_SEGSZ;
	mr = kzalloc(sizeof(*mr) + m * sizeof(mr->mr.map[0]), GFP_KERNEL);
	if (!mr)
		goto bail;

	rval = rvt_init_mregion(&mr->mr, pd, count);
	if (rval)
		goto bail;
	/*
	 * ib_reg_phys_mr() will initialize mr->ibmr except for
	 * lkey and rkey.
	 */
	rval = rvt_alloc_lkey(&mr->mr, 0);
	if (rval)
		goto bail_mregion;
	mr->ibmr.lkey = mr->mr.lkey;
	mr->ibmr.rkey = mr->mr.lkey;
done:
	return mr;

bail_mregion:
	rvt_deinit_mregion(&mr->mr);
bail:
	kfree(mr);
	mr = ERR_PTR(rval);
	goto done;
}

static void __rvt_free_mr(struct rvt_mr *mr)
{
	rvt_deinit_mregion(&mr->mr);
	rvt_free_lkey(&mr->mr);
	kfree(mr);
}

/**
 * rvt_get_dma_mr - get a DMA memory region
 * @pd: protection domain for this memory region
 * @acc: access flags
 *
 * Return: the memory region on success, otherwise returns an errno.
 * Note that all DMA addresses should be created via the
 * struct ib_dma_mapping_ops functions (see dma.c).
 */
struct ib_mr *rvt_get_dma_mr(struct ib_pd *pd, int acc)
{
	struct rvt_mr *mr;
	struct ib_mr *ret;
	int rval;

	if (ibpd_to_rvtpd(pd)->user)
		return ERR_PTR(-EPERM);

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr) {
		ret = ERR_PTR(-ENOMEM);
		goto bail;
	}

	rval = rvt_init_mregion(&mr->mr, pd, 0);
	if (rval) {
		ret = ERR_PTR(rval);
		goto bail;
	}

	rval = rvt_alloc_lkey(&mr->mr, 1);
	if (rval) {
		ret = ERR_PTR(rval);
		goto bail_mregion;
	}

	mr->mr.access_flags = acc;
	ret = &mr->ibmr;
done:
	return ret;

bail_mregion:
	rvt_deinit_mregion(&mr->mr);
bail:
	kfree(mr);
	goto done;
}

/**
 * rvt_reg_user_mr - register a userspace memory region
 * @pd: protection domain for this memory region
 * @start: starting userspace address
 * @length: length of region to register
 * @mr_access_flags: access flags for this memory region
 * @udata: unused by the driver
 *
 * Return: the memory region on success, otherwise returns an errno.
 */
struct ib_mr *rvt_reg_user_mr(struct ib_pd *pd, u64 start, u64 length,
			      u64 virt_addr, int mr_access_flags,
			      struct ib_udata *udata)
{
	struct rvt_mr *mr;
	struct ib_umem *umem;
	struct scatterlist *sg;
	int n, m, entry;
	struct ib_mr *ret;

	if (length == 0)
		return ERR_PTR(-EINVAL);

	umem = ib_umem_get(pd->uobject->context, start, length,
			   mr_access_flags, 0);
	if (IS_ERR(umem))
		return (void *)umem;

	n = umem->nmap;

	mr = __rvt_alloc_mr(n, pd);
	if (IS_ERR(mr)) {
		ret = (struct ib_mr *)mr;
		goto bail_umem;
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
			goto bail_inval;
		}
		mr->mr.map[m]->segs[n].vaddr = vaddr;
		mr->mr.map[m]->segs[n].length = umem->page_size;
		trace_rvt_mr_user_seg(&mr->mr, m, n, vaddr, umem->page_size);
		n++;
		if (n == RVT_SEGSZ) {
			m++;
			n = 0;
		}
	}
	return &mr->ibmr;

bail_inval:
	__rvt_free_mr(mr);

bail_umem:
	ib_umem_release(umem);

	return ret;
}

/**
 * rvt_dereg_mr - unregister and free a memory region
 * @ibmr: the memory region to free
 *
 *
 * Note that this is called to free MRs created by rvt_get_dma_mr()
 * or rvt_reg_user_mr().
 *
 * Returns 0 on success.
 */
int rvt_dereg_mr(struct ib_mr *ibmr)
{
	struct rvt_mr *mr = to_imr(ibmr);
	struct rvt_dev_info *rdi = ib_to_rvt(ibmr->pd->device);
	int ret = 0;
	unsigned long timeout;

	rvt_free_lkey(&mr->mr);

	rvt_put_mr(&mr->mr); /* will set completion if last */
	timeout = wait_for_completion_timeout(&mr->mr.comp, 5 * HZ);
	if (!timeout) {
		rvt_pr_err(rdi,
			   "rvt_dereg_mr timeout mr %p pd %p refcount %u\n",
			   mr, mr->mr.pd, atomic_read(&mr->mr.refcount));
		rvt_get_mr(&mr->mr);
		ret = -EBUSY;
		goto out;
	}
	rvt_deinit_mregion(&mr->mr);
	if (mr->umem)
		ib_umem_release(mr->umem);
	kfree(mr);
out:
	return ret;
}

/**
 * rvt_alloc_mr - Allocate a memory region usable with the
 * @pd: protection domain for this memory region
 * @mr_type: mem region type
 * @max_num_sg: Max number of segments allowed
 *
 * Return: the memory region on success, otherwise return an errno.
 */
struct ib_mr *rvt_alloc_mr(struct ib_pd *pd,
			   enum ib_mr_type mr_type,
			   u32 max_num_sg)
{
	struct rvt_mr *mr;

	if (mr_type != IB_MR_TYPE_MEM_REG)
		return ERR_PTR(-EINVAL);

	mr = __rvt_alloc_mr(max_num_sg, pd);
	if (IS_ERR(mr))
		return (struct ib_mr *)mr;

	return &mr->ibmr;
}

/**
 * rvt_set_page - page assignment function called by ib_sg_to_pages
 * @ibmr: memory region
 * @addr: dma address of mapped page
 *
 * Return: 0 on success
 */
static int rvt_set_page(struct ib_mr *ibmr, u64 addr)
{
	struct rvt_mr *mr = to_imr(ibmr);
	u32 ps = 1 << mr->mr.page_shift;
	u32 mapped_segs = mr->mr.length >> mr->mr.page_shift;
	int m, n;

	if (unlikely(mapped_segs == mr->mr.max_segs))
		return -ENOMEM;

	if (mr->mr.length == 0) {
		mr->mr.user_base = addr;
		mr->mr.iova = addr;
	}

	m = mapped_segs / RVT_SEGSZ;
	n = mapped_segs % RVT_SEGSZ;
	mr->mr.map[m]->segs[n].vaddr = (void *)addr;
	mr->mr.map[m]->segs[n].length = ps;
	trace_rvt_mr_page_seg(&mr->mr, m, n, (void *)addr, ps);
	mr->mr.length += ps;

	return 0;
}

/**
 * rvt_map_mr_sg - map sg list and set it the memory region
 * @ibmr: memory region
 * @sg: dma mapped scatterlist
 * @sg_nents: number of entries in sg
 * @sg_offset: offset in bytes into sg
 *
 * Return: number of sg elements mapped to the memory region
 */
int rvt_map_mr_sg(struct ib_mr *ibmr, struct scatterlist *sg,
		  int sg_nents, unsigned int *sg_offset)
{
	struct rvt_mr *mr = to_imr(ibmr);

	mr->mr.length = 0;
	mr->mr.page_shift = PAGE_SHIFT;
	return ib_sg_to_pages(ibmr, sg, sg_nents, sg_offset,
			      rvt_set_page);
}

/**
 * rvt_fast_reg_mr - fast register physical MR
 * @qp: the queue pair where the work request comes from
 * @ibmr: the memory region to be registered
 * @key: updated key for this memory region
 * @access: access flags for this memory region
 *
 * Returns 0 on success.
 */
int rvt_fast_reg_mr(struct rvt_qp *qp, struct ib_mr *ibmr, u32 key,
		    int access)
{
	struct rvt_mr *mr = to_imr(ibmr);

	if (qp->ibqp.pd != mr->mr.pd)
		return -EACCES;

	/* not applicable to dma MR or user MR */
	if (!mr->mr.lkey || mr->umem)
		return -EINVAL;

	if ((key & 0xFFFFFF00) != (mr->mr.lkey & 0xFFFFFF00))
		return -EINVAL;

	ibmr->lkey = key;
	ibmr->rkey = key;
	mr->mr.lkey = key;
	mr->mr.access_flags = access;
	atomic_set(&mr->mr.lkey_invalid, 0);

	return 0;
}
EXPORT_SYMBOL(rvt_fast_reg_mr);

/**
 * rvt_invalidate_rkey - invalidate an MR rkey
 * @qp: queue pair associated with the invalidate op
 * @rkey: rkey to invalidate
 *
 * Returns 0 on success.
 */
int rvt_invalidate_rkey(struct rvt_qp *qp, u32 rkey)
{
	struct rvt_dev_info *dev = ib_to_rvt(qp->ibqp.device);
	struct rvt_lkey_table *rkt = &dev->lkey_table;
	struct rvt_mregion *mr;

	if (rkey == 0)
		return -EINVAL;

	rcu_read_lock();
	mr = rcu_dereference(
		rkt->table[(rkey >> (32 - dev->dparms.lkey_table_size))]);
	if (unlikely(!mr || mr->lkey != rkey || qp->ibqp.pd != mr->pd))
		goto bail;

	atomic_set(&mr->lkey_invalid, 1);
	rcu_read_unlock();
	return 0;

bail:
	rcu_read_unlock();
	return -EINVAL;
}
EXPORT_SYMBOL(rvt_invalidate_rkey);

/**
 * rvt_alloc_fmr - allocate a fast memory region
 * @pd: the protection domain for this memory region
 * @mr_access_flags: access flags for this memory region
 * @fmr_attr: fast memory region attributes
 *
 * Return: the memory region on success, otherwise returns an errno.
 */
struct ib_fmr *rvt_alloc_fmr(struct ib_pd *pd, int mr_access_flags,
			     struct ib_fmr_attr *fmr_attr)
{
	struct rvt_fmr *fmr;
	int m;
	struct ib_fmr *ret;
	int rval = -ENOMEM;

	/* Allocate struct plus pointers to first level page tables. */
	m = (fmr_attr->max_pages + RVT_SEGSZ - 1) / RVT_SEGSZ;
	fmr = kzalloc(sizeof(*fmr) + m * sizeof(fmr->mr.map[0]), GFP_KERNEL);
	if (!fmr)
		goto bail;

	rval = rvt_init_mregion(&fmr->mr, pd, fmr_attr->max_pages);
	if (rval)
		goto bail;

	/*
	 * ib_alloc_fmr() will initialize fmr->ibfmr except for lkey &
	 * rkey.
	 */
	rval = rvt_alloc_lkey(&fmr->mr, 0);
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
	rvt_deinit_mregion(&fmr->mr);
bail:
	kfree(fmr);
	ret = ERR_PTR(rval);
	goto done;
}

/**
 * rvt_map_phys_fmr - set up a fast memory region
 * @ibmfr: the fast memory region to set up
 * @page_list: the list of pages to associate with the fast memory region
 * @list_len: the number of pages to associate with the fast memory region
 * @iova: the virtual address of the start of the fast memory region
 *
 * This may be called from interrupt context.
 *
 * Return: 0 on success
 */

int rvt_map_phys_fmr(struct ib_fmr *ibfmr, u64 *page_list,
		     int list_len, u64 iova)
{
	struct rvt_fmr *fmr = to_ifmr(ibfmr);
	struct rvt_lkey_table *rkt;
	unsigned long flags;
	int m, n, i;
	u32 ps;
	struct rvt_dev_info *rdi = ib_to_rvt(ibfmr->device);

	i = atomic_read(&fmr->mr.refcount);
	if (i > 2)
		return -EBUSY;

	if (list_len > fmr->mr.max_segs)
		return -EINVAL;

	rkt = &rdi->lkey_table;
	spin_lock_irqsave(&rkt->lock, flags);
	fmr->mr.user_base = iova;
	fmr->mr.iova = iova;
	ps = 1 << fmr->mr.page_shift;
	fmr->mr.length = list_len * ps;
	m = 0;
	n = 0;
	for (i = 0; i < list_len; i++) {
		fmr->mr.map[m]->segs[n].vaddr = (void *)page_list[i];
		fmr->mr.map[m]->segs[n].length = ps;
		trace_rvt_mr_fmr_seg(&fmr->mr, m, n, (void *)page_list[i], ps);
		if (++n == RVT_SEGSZ) {
			m++;
			n = 0;
		}
	}
	spin_unlock_irqrestore(&rkt->lock, flags);
	return 0;
}

/**
 * rvt_unmap_fmr - unmap fast memory regions
 * @fmr_list: the list of fast memory regions to unmap
 *
 * Return: 0 on success.
 */
int rvt_unmap_fmr(struct list_head *fmr_list)
{
	struct rvt_fmr *fmr;
	struct rvt_lkey_table *rkt;
	unsigned long flags;
	struct rvt_dev_info *rdi;

	list_for_each_entry(fmr, fmr_list, ibfmr.list) {
		rdi = ib_to_rvt(fmr->ibfmr.device);
		rkt = &rdi->lkey_table;
		spin_lock_irqsave(&rkt->lock, flags);
		fmr->mr.user_base = 0;
		fmr->mr.iova = 0;
		fmr->mr.length = 0;
		spin_unlock_irqrestore(&rkt->lock, flags);
	}
	return 0;
}

/**
 * rvt_dealloc_fmr - deallocate a fast memory region
 * @ibfmr: the fast memory region to deallocate
 *
 * Return: 0 on success.
 */
int rvt_dealloc_fmr(struct ib_fmr *ibfmr)
{
	struct rvt_fmr *fmr = to_ifmr(ibfmr);
	int ret = 0;
	unsigned long timeout;

	rvt_free_lkey(&fmr->mr);
	rvt_put_mr(&fmr->mr); /* will set completion if last */
	timeout = wait_for_completion_timeout(&fmr->mr.comp, 5 * HZ);
	if (!timeout) {
		rvt_get_mr(&fmr->mr);
		ret = -EBUSY;
		goto out;
	}
	rvt_deinit_mregion(&fmr->mr);
	kfree(fmr);
out:
	return ret;
}

/**
 * rvt_lkey_ok - check IB SGE for validity and initialize
 * @rkt: table containing lkey to check SGE against
 * @pd: protection domain
 * @isge: outgoing internal SGE
 * @sge: SGE to check
 * @acc: access flags
 *
 * Check the IB SGE for validity and initialize our internal version
 * of it.
 *
 * Return: 1 if valid and successful, otherwise returns 0.
 *
 * increments the reference count upon success
 *
 */
int rvt_lkey_ok(struct rvt_lkey_table *rkt, struct rvt_pd *pd,
		struct rvt_sge *isge, struct ib_sge *sge, int acc)
{
	struct rvt_mregion *mr;
	unsigned n, m;
	size_t off;

	/*
	 * We use LKEY == zero for kernel virtual addresses
	 * (see rvt_get_dma_mr and dma.c).
	 */
	rcu_read_lock();
	if (sge->lkey == 0) {
		struct rvt_dev_info *dev = ib_to_rvt(pd->ibpd.device);

		if (pd->user)
			goto bail;
		mr = rcu_dereference(dev->dma_mr);
		if (!mr)
			goto bail;
		rvt_get_mr(mr);
		rcu_read_unlock();

		isge->mr = mr;
		isge->vaddr = (void *)sge->addr;
		isge->length = sge->length;
		isge->sge_length = sge->length;
		isge->m = 0;
		isge->n = 0;
		goto ok;
	}
	mr = rcu_dereference(rkt->table[sge->lkey >> rkt->shift]);
	if (unlikely(!mr || atomic_read(&mr->lkey_invalid) ||
		     mr->lkey != sge->lkey || mr->pd != &pd->ibpd))
		goto bail;

	off = sge->addr - mr->user_base;
	if (unlikely(sge->addr < mr->user_base ||
		     off + sge->length > mr->length ||
		     (mr->access_flags & acc) != acc))
		goto bail;
	rvt_get_mr(mr);
	rcu_read_unlock();

	off += mr->offset;
	if (mr->page_shift) {
		/*
		 * page sizes are uniform power of 2 so no loop is necessary
		 * entries_spanned_by_off is the number of times the loop below
		 * would have executed.
		*/
		size_t entries_spanned_by_off;

		entries_spanned_by_off = off >> mr->page_shift;
		off -= (entries_spanned_by_off << mr->page_shift);
		m = entries_spanned_by_off / RVT_SEGSZ;
		n = entries_spanned_by_off % RVT_SEGSZ;
	} else {
		m = 0;
		n = 0;
		while (off >= mr->map[m]->segs[n].length) {
			off -= mr->map[m]->segs[n].length;
			n++;
			if (n >= RVT_SEGSZ) {
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
EXPORT_SYMBOL(rvt_lkey_ok);

/**
 * rvt_rkey_ok - check the IB virtual address, length, and RKEY
 * @qp: qp for validation
 * @sge: SGE state
 * @len: length of data
 * @vaddr: virtual address to place data
 * @rkey: rkey to check
 * @acc: access flags
 *
 * Return: 1 if successful, otherwise 0.
 *
 * increments the reference count upon success
 */
int rvt_rkey_ok(struct rvt_qp *qp, struct rvt_sge *sge,
		u32 len, u64 vaddr, u32 rkey, int acc)
{
	struct rvt_dev_info *dev = ib_to_rvt(qp->ibqp.device);
	struct rvt_lkey_table *rkt = &dev->lkey_table;
	struct rvt_mregion *mr;
	unsigned n, m;
	size_t off;

	/*
	 * We use RKEY == zero for kernel virtual addresses
	 * (see rvt_get_dma_mr and dma.c).
	 */
	rcu_read_lock();
	if (rkey == 0) {
		struct rvt_pd *pd = ibpd_to_rvtpd(qp->ibqp.pd);
		struct rvt_dev_info *rdi = ib_to_rvt(pd->ibpd.device);

		if (pd->user)
			goto bail;
		mr = rcu_dereference(rdi->dma_mr);
		if (!mr)
			goto bail;
		rvt_get_mr(mr);
		rcu_read_unlock();

		sge->mr = mr;
		sge->vaddr = (void *)vaddr;
		sge->length = len;
		sge->sge_length = len;
		sge->m = 0;
		sge->n = 0;
		goto ok;
	}

	mr = rcu_dereference(rkt->table[rkey >> rkt->shift]);
	if (unlikely(!mr || atomic_read(&mr->lkey_invalid) ||
		     mr->lkey != rkey || qp->ibqp.pd != mr->pd))
		goto bail;

	off = vaddr - mr->iova;
	if (unlikely(vaddr < mr->iova || off + len > mr->length ||
		     (mr->access_flags & acc) == 0))
		goto bail;
	rvt_get_mr(mr);
	rcu_read_unlock();

	off += mr->offset;
	if (mr->page_shift) {
		/*
		 * page sizes are uniform power of 2 so no loop is necessary
		 * entries_spanned_by_off is the number of times the loop below
		 * would have executed.
		*/
		size_t entries_spanned_by_off;

		entries_spanned_by_off = off >> mr->page_shift;
		off -= (entries_spanned_by_off << mr->page_shift);
		m = entries_spanned_by_off / RVT_SEGSZ;
		n = entries_spanned_by_off % RVT_SEGSZ;
	} else {
		m = 0;
		n = 0;
		while (off >= mr->map[m]->segs[n].length) {
			off -= mr->map[m]->segs[n].length;
			n++;
			if (n >= RVT_SEGSZ) {
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
EXPORT_SYMBOL(rvt_rkey_ok);
