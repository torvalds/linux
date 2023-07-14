// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2016 Mellanox Technologies Ltd. All rights reserved.
 * Copyright (c) 2015 System Fabric Works, Inc. All rights reserved.
 */

#include <linux/libnvdimm.h>

#include "rxe.h"
#include "rxe_loc.h"

/* Return a random 8 bit key value that is
 * different than the last_key. Set last_key to -1
 * if this is the first key for an MR or MW
 */
u8 rxe_get_next_key(u32 last_key)
{
	u8 key;

	do {
		get_random_bytes(&key, 1);
	} while (key == last_key);

	return key;
}

int mr_check_range(struct rxe_mr *mr, u64 iova, size_t length)
{
	switch (mr->ibmr.type) {
	case IB_MR_TYPE_DMA:
		return 0;

	case IB_MR_TYPE_USER:
	case IB_MR_TYPE_MEM_REG:
		if (iova < mr->ibmr.iova ||
		    iova + length > mr->ibmr.iova + mr->ibmr.length) {
			rxe_dbg_mr(mr, "iova/length out of range");
			return -EINVAL;
		}
		return 0;

	default:
		rxe_dbg_mr(mr, "mr type not supported\n");
		return -EINVAL;
	}
}

static void rxe_mr_init(int access, struct rxe_mr *mr)
{
	u32 key = mr->elem.index << 8 | rxe_get_next_key(-1);

	/* set ibmr->l/rkey and also copy into private l/rkey
	 * for user MRs these will always be the same
	 * for cases where caller 'owns' the key portion
	 * they may be different until REG_MR WQE is executed.
	 */
	mr->lkey = mr->ibmr.lkey = key;
	mr->rkey = mr->ibmr.rkey = key;

	mr->access = access;
	mr->ibmr.page_size = PAGE_SIZE;
	mr->page_mask = PAGE_MASK;
	mr->page_shift = PAGE_SHIFT;
	mr->state = RXE_MR_STATE_INVALID;
}

void rxe_mr_init_dma(int access, struct rxe_mr *mr)
{
	rxe_mr_init(access, mr);

	mr->state = RXE_MR_STATE_VALID;
	mr->ibmr.type = IB_MR_TYPE_DMA;
}

static unsigned long rxe_mr_iova_to_index(struct rxe_mr *mr, u64 iova)
{
	return (iova >> mr->page_shift) - (mr->ibmr.iova >> mr->page_shift);
}

static unsigned long rxe_mr_iova_to_page_offset(struct rxe_mr *mr, u64 iova)
{
	return iova & (mr_page_size(mr) - 1);
}

static bool is_pmem_page(struct page *pg)
{
	unsigned long paddr = page_to_phys(pg);

	return REGION_INTERSECTS ==
	       region_intersects(paddr, PAGE_SIZE, IORESOURCE_MEM,
				 IORES_DESC_PERSISTENT_MEMORY);
}

static int rxe_mr_fill_pages_from_sgt(struct rxe_mr *mr, struct sg_table *sgt)
{
	XA_STATE(xas, &mr->page_list, 0);
	struct sg_page_iter sg_iter;
	struct page *page;
	bool persistent = !!(mr->access & IB_ACCESS_FLUSH_PERSISTENT);

	__sg_page_iter_start(&sg_iter, sgt->sgl, sgt->orig_nents, 0);
	if (!__sg_page_iter_next(&sg_iter))
		return 0;

	do {
		xas_lock(&xas);
		while (true) {
			page = sg_page_iter_page(&sg_iter);

			if (persistent && !is_pmem_page(page)) {
				rxe_dbg_mr(mr, "Page can't be persistent\n");
				xas_set_err(&xas, -EINVAL);
				break;
			}

			xas_store(&xas, page);
			if (xas_error(&xas))
				break;
			xas_next(&xas);
			if (!__sg_page_iter_next(&sg_iter))
				break;
		}
		xas_unlock(&xas);
	} while (xas_nomem(&xas, GFP_KERNEL));

	return xas_error(&xas);
}

int rxe_mr_init_user(struct rxe_dev *rxe, u64 start, u64 length, u64 iova,
		     int access, struct rxe_mr *mr)
{
	struct ib_umem *umem;
	int err;

	rxe_mr_init(access, mr);

	xa_init(&mr->page_list);

	umem = ib_umem_get(&rxe->ib_dev, start, length, access);
	if (IS_ERR(umem)) {
		rxe_dbg_mr(mr, "Unable to pin memory region err = %d\n",
			(int)PTR_ERR(umem));
		return PTR_ERR(umem);
	}

	err = rxe_mr_fill_pages_from_sgt(mr, &umem->sgt_append.sgt);
	if (err) {
		ib_umem_release(umem);
		return err;
	}

	mr->umem = umem;
	mr->ibmr.type = IB_MR_TYPE_USER;
	mr->state = RXE_MR_STATE_VALID;

	return 0;
}

static int rxe_mr_alloc(struct rxe_mr *mr, int num_buf)
{
	XA_STATE(xas, &mr->page_list, 0);
	int i = 0;
	int err;

	xa_init(&mr->page_list);

	do {
		xas_lock(&xas);
		while (i != num_buf) {
			xas_store(&xas, XA_ZERO_ENTRY);
			if (xas_error(&xas))
				break;
			xas_next(&xas);
			i++;
		}
		xas_unlock(&xas);
	} while (xas_nomem(&xas, GFP_KERNEL));

	err = xas_error(&xas);
	if (err)
		return err;

	mr->num_buf = num_buf;

	return 0;
}

int rxe_mr_init_fast(int max_pages, struct rxe_mr *mr)
{
	int err;

	/* always allow remote access for FMRs */
	rxe_mr_init(RXE_ACCESS_REMOTE, mr);

	err = rxe_mr_alloc(mr, max_pages);
	if (err)
		goto err1;

	mr->state = RXE_MR_STATE_FREE;
	mr->ibmr.type = IB_MR_TYPE_MEM_REG;

	return 0;

err1:
	return err;
}

static int rxe_set_page(struct ib_mr *ibmr, u64 dma_addr)
{
	struct rxe_mr *mr = to_rmr(ibmr);
	struct page *page = ib_virt_dma_to_page(dma_addr);
	bool persistent = !!(mr->access & IB_ACCESS_FLUSH_PERSISTENT);
	int err;

	if (persistent && !is_pmem_page(page)) {
		rxe_dbg_mr(mr, "Page cannot be persistent\n");
		return -EINVAL;
	}

	if (unlikely(mr->nbuf == mr->num_buf))
		return -ENOMEM;

	err = xa_err(xa_store(&mr->page_list, mr->nbuf, page, GFP_KERNEL));
	if (err)
		return err;

	mr->nbuf++;
	return 0;
}

int rxe_map_mr_sg(struct ib_mr *ibmr, struct scatterlist *sgl,
		  int sg_nents, unsigned int *sg_offset)
{
	struct rxe_mr *mr = to_rmr(ibmr);
	unsigned int page_size = mr_page_size(mr);

	mr->nbuf = 0;
	mr->page_shift = ilog2(page_size);
	mr->page_mask = ~((u64)page_size - 1);
	mr->page_offset = mr->ibmr.iova & (page_size - 1);

	return ib_sg_to_pages(ibmr, sgl, sg_nents, sg_offset, rxe_set_page);
}

static int rxe_mr_copy_xarray(struct rxe_mr *mr, u64 iova, void *addr,
			      unsigned int length, enum rxe_mr_copy_dir dir)
{
	unsigned int page_offset = rxe_mr_iova_to_page_offset(mr, iova);
	unsigned long index = rxe_mr_iova_to_index(mr, iova);
	unsigned int bytes;
	struct page *page;
	void *va;

	while (length) {
		page = xa_load(&mr->page_list, index);
		if (!page)
			return -EFAULT;

		bytes = min_t(unsigned int, length,
				mr_page_size(mr) - page_offset);
		va = kmap_local_page(page);
		if (dir == RXE_FROM_MR_OBJ)
			memcpy(addr, va + page_offset, bytes);
		else
			memcpy(va + page_offset, addr, bytes);
		kunmap_local(va);

		page_offset = 0;
		addr += bytes;
		length -= bytes;
		index++;
	}

	return 0;
}

static void rxe_mr_copy_dma(struct rxe_mr *mr, u64 dma_addr, void *addr,
			    unsigned int length, enum rxe_mr_copy_dir dir)
{
	unsigned int page_offset = dma_addr & (PAGE_SIZE - 1);
	unsigned int bytes;
	struct page *page;
	u8 *va;

	while (length) {
		page = ib_virt_dma_to_page(dma_addr);
		bytes = min_t(unsigned int, length,
				PAGE_SIZE - page_offset);
		va = kmap_local_page(page);

		if (dir == RXE_TO_MR_OBJ)
			memcpy(va + page_offset, addr, bytes);
		else
			memcpy(addr, va + page_offset, bytes);

		kunmap_local(va);
		page_offset = 0;
		dma_addr += bytes;
		addr += bytes;
		length -= bytes;
	}
}

int rxe_mr_copy(struct rxe_mr *mr, u64 iova, void *addr,
		unsigned int length, enum rxe_mr_copy_dir dir)
{
	int err;

	if (length == 0)
		return 0;

	if (WARN_ON(!mr))
		return -EINVAL;

	if (mr->ibmr.type == IB_MR_TYPE_DMA) {
		rxe_mr_copy_dma(mr, iova, addr, length, dir);
		return 0;
	}

	err = mr_check_range(mr, iova, length);
	if (unlikely(err)) {
		rxe_dbg_mr(mr, "iova out of range");
		return err;
	}

	return rxe_mr_copy_xarray(mr, iova, addr, length, dir);
}

/* copy data in or out of a wqe, i.e. sg list
 * under the control of a dma descriptor
 */
int copy_data(
	struct rxe_pd		*pd,
	int			access,
	struct rxe_dma_info	*dma,
	void			*addr,
	int			length,
	enum rxe_mr_copy_dir	dir)
{
	int			bytes;
	struct rxe_sge		*sge	= &dma->sge[dma->cur_sge];
	int			offset	= dma->sge_offset;
	int			resid	= dma->resid;
	struct rxe_mr		*mr	= NULL;
	u64			iova;
	int			err;

	if (length == 0)
		return 0;

	if (length > resid) {
		err = -EINVAL;
		goto err2;
	}

	if (sge->length && (offset < sge->length)) {
		mr = lookup_mr(pd, access, sge->lkey, RXE_LOOKUP_LOCAL);
		if (!mr) {
			err = -EINVAL;
			goto err1;
		}
	}

	while (length > 0) {
		bytes = length;

		if (offset >= sge->length) {
			if (mr) {
				rxe_put(mr);
				mr = NULL;
			}
			sge++;
			dma->cur_sge++;
			offset = 0;

			if (dma->cur_sge >= dma->num_sge) {
				err = -ENOSPC;
				goto err2;
			}

			if (sge->length) {
				mr = lookup_mr(pd, access, sge->lkey,
					       RXE_LOOKUP_LOCAL);
				if (!mr) {
					err = -EINVAL;
					goto err1;
				}
			} else {
				continue;
			}
		}

		if (bytes > sge->length - offset)
			bytes = sge->length - offset;

		if (bytes > 0) {
			iova = sge->addr + offset;
			err = rxe_mr_copy(mr, iova, addr, bytes, dir);
			if (err)
				goto err2;

			offset	+= bytes;
			resid	-= bytes;
			length	-= bytes;
			addr	+= bytes;
		}
	}

	dma->sge_offset = offset;
	dma->resid	= resid;

	if (mr)
		rxe_put(mr);

	return 0;

err2:
	if (mr)
		rxe_put(mr);
err1:
	return err;
}

int rxe_flush_pmem_iova(struct rxe_mr *mr, u64 iova, unsigned int length)
{
	unsigned int page_offset;
	unsigned long index;
	struct page *page;
	unsigned int bytes;
	int err;
	u8 *va;

	/* mr must be valid even if length is zero */
	if (WARN_ON(!mr))
		return -EINVAL;

	if (length == 0)
		return 0;

	if (mr->ibmr.type == IB_MR_TYPE_DMA)
		return -EFAULT;

	err = mr_check_range(mr, iova, length);
	if (err)
		return err;

	while (length > 0) {
		index = rxe_mr_iova_to_index(mr, iova);
		page = xa_load(&mr->page_list, index);
		page_offset = rxe_mr_iova_to_page_offset(mr, iova);
		if (!page)
			return -EFAULT;
		bytes = min_t(unsigned int, length,
				mr_page_size(mr) - page_offset);

		va = kmap_local_page(page);
		arch_wb_cache_pmem(va + page_offset, bytes);
		kunmap_local(va);

		length -= bytes;
		iova += bytes;
		page_offset = 0;
	}

	return 0;
}

/* Guarantee atomicity of atomic operations at the machine level. */
static DEFINE_SPINLOCK(atomic_ops_lock);

int rxe_mr_do_atomic_op(struct rxe_mr *mr, u64 iova, int opcode,
			u64 compare, u64 swap_add, u64 *orig_val)
{
	unsigned int page_offset;
	struct page *page;
	u64 value;
	u64 *va;

	if (unlikely(mr->state != RXE_MR_STATE_VALID)) {
		rxe_dbg_mr(mr, "mr not in valid state");
		return RESPST_ERR_RKEY_VIOLATION;
	}

	if (mr->ibmr.type == IB_MR_TYPE_DMA) {
		page_offset = iova & (PAGE_SIZE - 1);
		page = ib_virt_dma_to_page(iova);
	} else {
		unsigned long index;
		int err;

		err = mr_check_range(mr, iova, sizeof(value));
		if (err) {
			rxe_dbg_mr(mr, "iova out of range");
			return RESPST_ERR_RKEY_VIOLATION;
		}
		page_offset = rxe_mr_iova_to_page_offset(mr, iova);
		index = rxe_mr_iova_to_index(mr, iova);
		page = xa_load(&mr->page_list, index);
		if (!page)
			return RESPST_ERR_RKEY_VIOLATION;
	}

	if (unlikely(page_offset & 0x7)) {
		rxe_dbg_mr(mr, "iova not aligned");
		return RESPST_ERR_MISALIGNED_ATOMIC;
	}

	va = kmap_local_page(page);

	spin_lock_bh(&atomic_ops_lock);
	value = *orig_val = va[page_offset >> 3];

	if (opcode == IB_OPCODE_RC_COMPARE_SWAP) {
		if (value == compare)
			va[page_offset >> 3] = swap_add;
	} else {
		value += swap_add;
		va[page_offset >> 3] = value;
	}
	spin_unlock_bh(&atomic_ops_lock);

	kunmap_local(va);

	return 0;
}

#if defined CONFIG_64BIT
/* only implemented or called for 64 bit architectures */
int rxe_mr_do_atomic_write(struct rxe_mr *mr, u64 iova, u64 value)
{
	unsigned int page_offset;
	struct page *page;
	u64 *va;

	/* See IBA oA19-28 */
	if (unlikely(mr->state != RXE_MR_STATE_VALID)) {
		rxe_dbg_mr(mr, "mr not in valid state");
		return RESPST_ERR_RKEY_VIOLATION;
	}

	if (mr->ibmr.type == IB_MR_TYPE_DMA) {
		page_offset = iova & (PAGE_SIZE - 1);
		page = ib_virt_dma_to_page(iova);
	} else {
		unsigned long index;
		int err;

		/* See IBA oA19-28 */
		err = mr_check_range(mr, iova, sizeof(value));
		if (unlikely(err)) {
			rxe_dbg_mr(mr, "iova out of range");
			return RESPST_ERR_RKEY_VIOLATION;
		}
		page_offset = rxe_mr_iova_to_page_offset(mr, iova);
		index = rxe_mr_iova_to_index(mr, iova);
		page = xa_load(&mr->page_list, index);
		if (!page)
			return RESPST_ERR_RKEY_VIOLATION;
	}

	/* See IBA A19.4.2 */
	if (unlikely(page_offset & 0x7)) {
		rxe_dbg_mr(mr, "misaligned address");
		return RESPST_ERR_MISALIGNED_ATOMIC;
	}

	va = kmap_local_page(page);

	/* Do atomic write after all prior operations have completed */
	smp_store_release(&va[page_offset >> 3], value);

	kunmap_local(va);

	return 0;
}
#else
int rxe_mr_do_atomic_write(struct rxe_mr *mr, u64 iova, u64 value)
{
	return RESPST_ERR_UNSUPPORTED_OPCODE;
}
#endif

int advance_dma_data(struct rxe_dma_info *dma, unsigned int length)
{
	struct rxe_sge		*sge	= &dma->sge[dma->cur_sge];
	int			offset	= dma->sge_offset;
	int			resid	= dma->resid;

	while (length) {
		unsigned int bytes;

		if (offset >= sge->length) {
			sge++;
			dma->cur_sge++;
			offset = 0;
			if (dma->cur_sge >= dma->num_sge)
				return -ENOSPC;
		}

		bytes = length;

		if (bytes > sge->length - offset)
			bytes = sge->length - offset;

		offset	+= bytes;
		resid	-= bytes;
		length	-= bytes;
	}

	dma->sge_offset = offset;
	dma->resid	= resid;

	return 0;
}

struct rxe_mr *lookup_mr(struct rxe_pd *pd, int access, u32 key,
			 enum rxe_mr_lookup_type type)
{
	struct rxe_mr *mr;
	struct rxe_dev *rxe = to_rdev(pd->ibpd.device);
	int index = key >> 8;

	mr = rxe_pool_get_index(&rxe->mr_pool, index);
	if (!mr)
		return NULL;

	if (unlikely((type == RXE_LOOKUP_LOCAL && mr->lkey != key) ||
		     (type == RXE_LOOKUP_REMOTE && mr->rkey != key) ||
		     mr_pd(mr) != pd || ((access & mr->access) != access) ||
		     mr->state != RXE_MR_STATE_VALID)) {
		rxe_put(mr);
		mr = NULL;
	}

	return mr;
}

int rxe_invalidate_mr(struct rxe_qp *qp, u32 key)
{
	struct rxe_dev *rxe = to_rdev(qp->ibqp.device);
	struct rxe_mr *mr;
	int remote;
	int ret;

	mr = rxe_pool_get_index(&rxe->mr_pool, key >> 8);
	if (!mr) {
		rxe_dbg_qp(qp, "No MR for key %#x\n", key);
		ret = -EINVAL;
		goto err;
	}

	remote = mr->access & RXE_ACCESS_REMOTE;
	if (remote ? (key != mr->rkey) : (key != mr->lkey)) {
		rxe_dbg_mr(mr, "wr key (%#x) doesn't match mr key (%#x)\n",
			key, (remote ? mr->rkey : mr->lkey));
		ret = -EINVAL;
		goto err_drop_ref;
	}

	if (atomic_read(&mr->num_mw) > 0) {
		rxe_dbg_mr(mr, "Attempt to invalidate an MR while bound to MWs\n");
		ret = -EINVAL;
		goto err_drop_ref;
	}

	if (unlikely(mr->ibmr.type != IB_MR_TYPE_MEM_REG)) {
		rxe_dbg_mr(mr, "Type (%d) is wrong\n", mr->ibmr.type);
		ret = -EINVAL;
		goto err_drop_ref;
	}

	mr->state = RXE_MR_STATE_FREE;
	ret = 0;

err_drop_ref:
	rxe_put(mr);
err:
	return ret;
}

/* user can (re)register fast MR by executing a REG_MR WQE.
 * user is expected to hold a reference on the ib mr until the
 * WQE completes.
 * Once a fast MR is created this is the only way to change the
 * private keys. It is the responsibility of the user to maintain
 * the ib mr keys in sync with rxe mr keys.
 */
int rxe_reg_fast_mr(struct rxe_qp *qp, struct rxe_send_wqe *wqe)
{
	struct rxe_mr *mr = to_rmr(wqe->wr.wr.reg.mr);
	u32 key = wqe->wr.wr.reg.key;
	u32 access = wqe->wr.wr.reg.access;

	/* user can only register MR in free state */
	if (unlikely(mr->state != RXE_MR_STATE_FREE)) {
		rxe_dbg_mr(mr, "mr->lkey = 0x%x not free\n", mr->lkey);
		return -EINVAL;
	}

	/* user can only register mr with qp in same protection domain */
	if (unlikely(qp->ibqp.pd != mr->ibmr.pd)) {
		rxe_dbg_mr(mr, "qp->pd and mr->pd don't match\n");
		return -EINVAL;
	}

	/* user is only allowed to change key portion of l/rkey */
	if (unlikely((mr->lkey & ~0xff) != (key & ~0xff))) {
		rxe_dbg_mr(mr, "key = 0x%x has wrong index mr->lkey = 0x%x\n",
			key, mr->lkey);
		return -EINVAL;
	}

	mr->access = access;
	mr->lkey = key;
	mr->rkey = key;
	mr->ibmr.iova = wqe->wr.wr.reg.mr->iova;
	mr->state = RXE_MR_STATE_VALID;

	return 0;
}

void rxe_mr_cleanup(struct rxe_pool_elem *elem)
{
	struct rxe_mr *mr = container_of(elem, typeof(*mr), elem);

	rxe_put(mr_pd(mr));
	ib_umem_release(mr->umem);

	if (mr->ibmr.type != IB_MR_TYPE_DMA)
		xa_destroy(&mr->page_list);
}
