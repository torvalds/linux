// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright(c) 2016 Intel Corporation.
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

	rdi->dparms.props.max_mr = rdi->lkey_table.max;
	return 0;
}

/**
 * rvt_mr_exit - clean up MR
 * @rdi: rvt dev structure
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
	percpu_ref_exit(&mr->refcount);
}

static void __rvt_mregion_complete(struct percpu_ref *ref)
{
	struct rvt_mregion *mr = container_of(ref, struct rvt_mregion,
					      refcount);

	complete(&mr->comp);
}

static int rvt_init_mregion(struct rvt_mregion *mr, struct ib_pd *pd,
			    int count, unsigned int percpu_flags)
{
	int m, i = 0;
	struct rvt_dev_info *dev = ib_to_rvt(pd->device);

	mr->mapsz = 0;
	m = (count + RVT_SEGSZ - 1) / RVT_SEGSZ;
	for (; i < m; i++) {
		mr->map[i] = kzalloc_node(sizeof(*mr->map[0]), GFP_KERNEL,
					  dev->dparms.node);
		if (!mr->map[i])
			goto bail;
		mr->mapsz++;
	}
	init_completion(&mr->comp);
	/* count returning the ptr to user */
	if (percpu_ref_init(&mr->refcount, &__rvt_mregion_complete,
			    percpu_flags, GFP_KERNEL))
		goto bail;

	atomic_set(&mr->lkey_invalid, 0);
	mr->pd = pd;
	mr->max_segs = count;
	return 0;
bail:
	rvt_deinit_mregion(mr);
	return -ENOMEM;
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
			mr->lkey_published = 1;
			/* Insure published written first */
			rcu_assign_pointer(dev->dma_mr, mr);
			rvt_get_mr(mr);
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
	mr->lkey_published = 1;
	/* Insure published written first */
	rcu_assign_pointer(rkt->table[r], mr);
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
	if (!lkey) {
		if (mr->lkey_published) {
			mr->lkey_published = 0;
			/* insure published is written before pointer */
			rcu_assign_pointer(dev->dma_mr, NULL);
			rvt_put_mr(mr);
		}
	} else {
		if (!mr->lkey_published)
			goto out;
		r = lkey >> (32 - dev->dparms.lkey_table_size);
		mr->lkey_published = 0;
		/* insure published is written before pointer */
		rcu_assign_pointer(rkt->table[r], NULL);
	}
	freed++;
out:
	spin_unlock_irqrestore(&rkt->lock, flags);
	if (freed)
		percpu_ref_kill(&mr->refcount);
}

static struct rvt_mr *__rvt_alloc_mr(int count, struct ib_pd *pd)
{
	struct rvt_mr *mr;
	int rval = -ENOMEM;
	int m;

	/* Allocate struct plus pointers to first level page tables. */
	m = (count + RVT_SEGSZ - 1) / RVT_SEGSZ;
	mr = kzalloc(struct_size(mr, mr.map, m), GFP_KERNEL);
	if (!mr)
		goto bail;

	rval = rvt_init_mregion(&mr->mr, pd, count, 0);
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
	rvt_free_lkey(&mr->mr);
	rvt_deinit_mregion(&mr->mr);
	kfree(mr);
}

/**
 * rvt_get_dma_mr - get a DMA memory region
 * @pd: protection domain for this memory region
 * @acc: access flags
 *
 * Return: the memory region on success, otherwise returns an errno.
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

	rval = rvt_init_mregion(&mr->mr, pd, 0, 0);
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
 * @virt_addr: associated virtual address
 * @mr_access_flags: access flags for this memory region
 * @dmah: dma handle
 * @udata: unused by the driver
 *
 * Return: the memory region on success, otherwise returns an errno.
 */
struct ib_mr *rvt_reg_user_mr(struct ib_pd *pd, u64 start, u64 length,
			      u64 virt_addr, int mr_access_flags,
			      struct ib_dmah *dmah,
			      struct ib_udata *udata)
{
	struct rvt_mr *mr;
	struct ib_umem *umem;
	struct sg_page_iter sg_iter;
	int n, m;
	struct ib_mr *ret;

	if (dmah)
		return ERR_PTR(-EOPNOTSUPP);

	if (length == 0)
		return ERR_PTR(-EINVAL);

	umem = ib_umem_get(pd->device, start, length, mr_access_flags);
	if (IS_ERR(umem))
		return ERR_CAST(umem);

	n = ib_umem_num_pages(umem);

	mr = __rvt_alloc_mr(n, pd);
	if (IS_ERR(mr)) {
		ret = ERR_CAST(mr);
		goto bail_umem;
	}

	mr->mr.user_base = start;
	mr->mr.iova = virt_addr;
	mr->mr.length = length;
	mr->mr.offset = ib_umem_offset(umem);
	mr->mr.access_flags = mr_access_flags;
	mr->umem = umem;

	mr->mr.page_shift = PAGE_SHIFT;
	m = 0;
	n = 0;
	for_each_sgtable_page (&umem->sgt_append.sgt, &sg_iter, 0) {
		void *vaddr;

		vaddr = page_address(sg_page_iter_page(&sg_iter));
		if (!vaddr) {
			ret = ERR_PTR(-EINVAL);
			goto bail_inval;
		}
		mr->mr.map[m]->segs[n].vaddr = vaddr;
		mr->mr.map[m]->segs[n].length = PAGE_SIZE;
		trace_rvt_mr_user_seg(&mr->mr, m, n, vaddr, PAGE_SIZE);
		if (++n == RVT_SEGSZ) {
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
 * rvt_dereg_clean_qp_cb - callback from iterator
 * @qp: the qp
 * @v: the mregion (as u64)
 *
 * This routine fields the callback for all QPs and
 * for QPs in the same PD as the MR will call the
 * rvt_qp_mr_clean() to potentially cleanup references.
 */
static void rvt_dereg_clean_qp_cb(struct rvt_qp *qp, u64 v)
{
	struct rvt_mregion *mr = (struct rvt_mregion *)v;

	/* skip PDs that are not ours */
	if (mr->pd != qp->ibqp.pd)
		return;
	rvt_qp_mr_clean(qp, mr->lkey);
}

/**
 * rvt_dereg_clean_qps - find QPs for reference cleanup
 * @mr: the MR that is being deregistered
 *
 * This routine iterates RC QPs looking for references
 * to the lkey noted in mr.
 */
static void rvt_dereg_clean_qps(struct rvt_mregion *mr)
{
	struct rvt_dev_info *rdi = ib_to_rvt(mr->pd->device);

	rvt_qp_iter(rdi, (u64)mr, rvt_dereg_clean_qp_cb);
}

/**
 * rvt_check_refs - check references
 * @mr: the megion
 * @t: the caller identification
 *
 * This routine checks MRs holding a reference during
 * when being de-registered.
 *
 * If the count is non-zero, the code calls a clean routine then
 * waits for the timeout for the count to zero.
 */
static int rvt_check_refs(struct rvt_mregion *mr, const char *t)
{
	unsigned long timeout;
	struct rvt_dev_info *rdi = ib_to_rvt(mr->pd->device);

	if (mr->lkey) {
		/* avoid dma mr */
		rvt_dereg_clean_qps(mr);
		/* @mr was indexed on rcu protected @lkey_table */
		synchronize_rcu();
	}

	timeout = wait_for_completion_timeout(&mr->comp, 5 * HZ);
	if (!timeout) {
		rvt_pr_err(rdi,
			   "%s timeout mr %p pd %p lkey %x refcount %ld\n",
			   t, mr, mr->pd, mr->lkey,
			   atomic_long_read(&mr->refcount.data->count));
		rvt_get_mr(mr);
		return -EBUSY;
	}
	return 0;
}

/**
 * rvt_mr_has_lkey - is MR
 * @mr: the mregion
 * @lkey: the lkey
 */
bool rvt_mr_has_lkey(struct rvt_mregion *mr, u32 lkey)
{
	return mr && lkey == mr->lkey;
}

/**
 * rvt_ss_has_lkey - is mr in sge tests
 * @ss: the sge state
 * @lkey: the lkey
 *
 * This code tests for an MR in the indicated
 * sge state.
 */
bool rvt_ss_has_lkey(struct rvt_sge_state *ss, u32 lkey)
{
	int i;
	bool rval = false;

	if (!ss->num_sge)
		return rval;
	/* first one */
	rval = rvt_mr_has_lkey(ss->sge.mr, lkey);
	/* any others */
	for (i = 0; !rval && i < ss->num_sge - 1; i++)
		rval = rvt_mr_has_lkey(ss->sg_list[i].mr, lkey);
	return rval;
}

/**
 * rvt_dereg_mr - unregister and free a memory region
 * @ibmr: the memory region to free
 * @udata: unused by the driver
 *
 * Note that this is called to free MRs created by rvt_get_dma_mr()
 * or rvt_reg_user_mr().
 *
 * Returns 0 on success.
 */
int rvt_dereg_mr(struct ib_mr *ibmr, struct ib_udata *udata)
{
	struct rvt_mr *mr = to_imr(ibmr);
	int ret;

	rvt_free_lkey(&mr->mr);

	rvt_put_mr(&mr->mr); /* will set completion if last */
	ret = rvt_check_refs(&mr->mr, __func__);
	if (ret)
		goto out;
	rvt_deinit_mregion(&mr->mr);
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
struct ib_mr *rvt_alloc_mr(struct ib_pd *pd, enum ib_mr_type mr_type,
			   u32 max_num_sg)
{
	struct rvt_mr *mr;

	if (mr_type != IB_MR_TYPE_MEM_REG)
		return ERR_PTR(-EINVAL);

	mr = __rvt_alloc_mr(max_num_sg, pd);
	if (IS_ERR(mr))
		return ERR_CAST(mr);

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

	m = mapped_segs / RVT_SEGSZ;
	n = mapped_segs % RVT_SEGSZ;
	mr->mr.map[m]->segs[n].vaddr = (void *)addr;
	mr->mr.map[m]->segs[n].length = ps;
	mr->mr.length += ps;
	trace_rvt_mr_page_seg(&mr->mr, m, n, (void *)addr, ps);

	return 0;
}

/**
 * rvt_map_mr_sg - map sg list and set it the memory region
 * @ibmr: memory region
 * @sg: dma mapped scatterlist
 * @sg_nents: number of entries in sg
 * @sg_offset: offset in bytes into sg
 *
 * Overwrite rvt_mr length with mr length calculated by ib_sg_to_pages.
 *
 * Return: number of sg elements mapped to the memory region
 */
int rvt_map_mr_sg(struct ib_mr *ibmr, struct scatterlist *sg,
		  int sg_nents, unsigned int *sg_offset)
{
	struct rvt_mr *mr = to_imr(ibmr);
	int ret;

	mr->mr.length = 0;
	mr->mr.page_shift = PAGE_SHIFT;
	ret = ib_sg_to_pages(ibmr, sg, sg_nents, sg_offset, rvt_set_page);
	mr->mr.user_base = ibmr->iova;
	mr->mr.iova = ibmr->iova;
	mr->mr.offset = ibmr->iova - (u64)mr->mr.map[0]->segs[0].vaddr;
	mr->mr.length = (size_t)ibmr->length;
	trace_rvt_map_mr_sg(ibmr, sg_nents, sg_offset);
	return ret;
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
	mr->mr.iova = ibmr->iova;
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
 * rvt_sge_adjacent - is isge compressible
 * @last_sge: last outgoing SGE written
 * @sge: SGE to check
 *
 * If adjacent will update last_sge to add length.
 *
 * Return: true if isge is adjacent to last sge
 */
static inline bool rvt_sge_adjacent(struct rvt_sge *last_sge,
				    struct ib_sge *sge)
{
	if (last_sge && sge->lkey == last_sge->mr->lkey &&
	    ((uint64_t)(last_sge->vaddr + last_sge->length) == sge->addr)) {
		if (sge->lkey) {
			if (unlikely((sge->addr - last_sge->mr->user_base +
			      sge->length > last_sge->mr->length)))
				return false; /* overrun, caller will catch */
		} else {
			last_sge->length += sge->length;
		}
		last_sge->sge_length += sge->length;
		trace_rvt_sge_adjacent(last_sge, sge);
		return true;
	}
	return false;
}

/**
 * rvt_lkey_ok - check IB SGE for validity and initialize
 * @rkt: table containing lkey to check SGE against
 * @pd: protection domain
 * @isge: outgoing internal SGE
 * @last_sge: last outgoing SGE written
 * @sge: SGE to check
 * @acc: access flags
 *
 * Check the IB SGE for validity and initialize our internal version
 * of it.
 *
 * Increments the reference count when a new sge is stored.
 *
 * Return: 0 if compressed, 1 if added , otherwise returns -errno.
 */
int rvt_lkey_ok(struct rvt_lkey_table *rkt, struct rvt_pd *pd,
		struct rvt_sge *isge, struct rvt_sge *last_sge,
		struct ib_sge *sge, int acc)
{
	struct rvt_mregion *mr;
	unsigned n, m;
	size_t off;

	/*
	 * We use LKEY == zero for kernel virtual addresses
	 * (see rvt_get_dma_mr()).
	 */
	if (sge->lkey == 0) {
		struct rvt_dev_info *dev = ib_to_rvt(pd->ibpd.device);

		if (pd->user)
			return -EINVAL;
		if (rvt_sge_adjacent(last_sge, sge))
			return 0;
		rcu_read_lock();
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
	if (rvt_sge_adjacent(last_sge, sge))
		return 0;
	rcu_read_lock();
	mr = rcu_dereference(rkt->table[sge->lkey >> rkt->shift]);
	if (!mr)
		goto bail;
	rvt_get_mr(mr);
	if (!READ_ONCE(mr->lkey_published))
		goto bail_unref;

	if (unlikely(atomic_read(&mr->lkey_invalid) ||
		     mr->lkey != sge->lkey || mr->pd != &pd->ibpd))
		goto bail_unref;

	off = sge->addr - mr->user_base;
	if (unlikely(sge->addr < mr->user_base ||
		     off + sge->length > mr->length ||
		     (mr->access_flags & acc) != acc))
		goto bail_unref;
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
	trace_rvt_sge_new(isge, sge);
	return 1;
bail_unref:
	rvt_put_mr(mr);
bail:
	rcu_read_unlock();
	return -EINVAL;
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
	 * (see rvt_get_dma_mr()).
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
	if (!mr)
		goto bail;
	rvt_get_mr(mr);
	/* insure mr read is before test */
	if (!READ_ONCE(mr->lkey_published))
		goto bail_unref;
	if (unlikely(atomic_read(&mr->lkey_invalid) ||
		     mr->lkey != rkey || qp->ibqp.pd != mr->pd))
		goto bail_unref;

	off = vaddr - mr->iova;
	if (unlikely(vaddr < mr->iova || off + len > mr->length ||
		     (mr->access_flags & acc) == 0))
		goto bail_unref;
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
bail_unref:
	rvt_put_mr(mr);
bail:
	rcu_read_unlock();
	return 0;
}
EXPORT_SYMBOL(rvt_rkey_ok);
