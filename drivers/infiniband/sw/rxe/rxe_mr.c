// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2016 Mellanox Technologies Ltd. All rights reserved.
 * Copyright (c) 2015 System Fabric Works, Inc. All rights reserved.
 */

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


	switch (mr->type) {
	case IB_MR_TYPE_DMA:
		return 0;

	case IB_MR_TYPE_USER:
	case IB_MR_TYPE_MEM_REG:
		if (iova < mr->ibmr.iova || length > mr->ibmr.length ||
		    iova > mr->ibmr.iova + mr->ibmr.length - length)
			return -EFAULT;
		return 0;

	default:
		pr_warn("%s: mr type (%d) not supported\n",
			__func__, mr->type);
		return -EFAULT;
	}
}

#define IB_ACCESS_REMOTE	(IB_ACCESS_REMOTE_READ		\
				| IB_ACCESS_REMOTE_WRITE	\
				| IB_ACCESS_REMOTE_ATOMIC)

static void rxe_mr_init(int access, struct rxe_mr *mr)
{
	u32 lkey = mr->elem.index << 8 | rxe_get_next_key(-1);
	u32 rkey = (access & IB_ACCESS_REMOTE) ? lkey : 0;

	/* set ibmr->l/rkey and also copy into private l/rkey
	 * for user MRs these will always be the same
	 * for cases where caller 'owns' the key portion
	 * they may be different until REG_MR WQE is executed.
	 */
	mr->lkey = mr->ibmr.lkey = lkey;
	mr->rkey = mr->ibmr.rkey = rkey;

	mr->state = RXE_MR_STATE_INVALID;
	mr->map_shift = ilog2(RXE_BUF_PER_MAP);
}

static int rxe_mr_alloc(struct rxe_mr *mr, int num_buf)
{
	int i;
	int num_map;
	struct rxe_map **map = mr->map;

	num_map = (num_buf + RXE_BUF_PER_MAP - 1) / RXE_BUF_PER_MAP;

	mr->map = kmalloc_array(num_map, sizeof(*map), GFP_KERNEL);
	if (!mr->map)
		goto err1;

	for (i = 0; i < num_map; i++) {
		mr->map[i] = kmalloc(sizeof(**map), GFP_KERNEL);
		if (!mr->map[i])
			goto err2;
	}

	BUILD_BUG_ON(!is_power_of_2(RXE_BUF_PER_MAP));

	mr->map_shift = ilog2(RXE_BUF_PER_MAP);
	mr->map_mask = RXE_BUF_PER_MAP - 1;

	mr->num_buf = num_buf;
	mr->num_map = num_map;
	mr->max_buf = num_map * RXE_BUF_PER_MAP;

	return 0;

err2:
	for (i--; i >= 0; i--)
		kfree(mr->map[i]);

	kfree(mr->map);
err1:
	return -ENOMEM;
}

void rxe_mr_init_dma(int access, struct rxe_mr *mr)
{
	rxe_mr_init(access, mr);

	mr->access = access;
	mr->state = RXE_MR_STATE_VALID;
	mr->type = IB_MR_TYPE_DMA;
}

int rxe_mr_init_user(struct rxe_dev *rxe, u64 start, u64 length, u64 iova,
		     int access, struct rxe_mr *mr)
{
	struct rxe_map		**map;
	struct rxe_phys_buf	*buf = NULL;
	struct ib_umem		*umem;
	struct sg_page_iter	sg_iter;
	int			num_buf;
	void			*vaddr;
	int err;
	int i;

	umem = ib_umem_get(&rxe->ib_dev, start, length, access);
	if (IS_ERR(umem)) {
		pr_warn("%s: Unable to pin memory region err = %d\n",
			__func__, (int)PTR_ERR(umem));
		err = PTR_ERR(umem);
		goto err_out;
	}

	num_buf = ib_umem_num_pages(umem);

	rxe_mr_init(access, mr);

	err = rxe_mr_alloc(mr, num_buf);
	if (err) {
		pr_warn("%s: Unable to allocate memory for map\n",
				__func__);
		goto err_release_umem;
	}

	mr->page_shift = PAGE_SHIFT;
	mr->page_mask = PAGE_SIZE - 1;

	num_buf			= 0;
	map = mr->map;
	if (length > 0) {
		buf = map[0]->buf;

		for_each_sgtable_page (&umem->sgt_append.sgt, &sg_iter, 0) {
			if (num_buf >= RXE_BUF_PER_MAP) {
				map++;
				buf = map[0]->buf;
				num_buf = 0;
			}

			vaddr = page_address(sg_page_iter_page(&sg_iter));
			if (!vaddr) {
				pr_warn("%s: Unable to get virtual address\n",
						__func__);
				err = -ENOMEM;
				goto err_cleanup_map;
			}

			buf->addr = (uintptr_t)vaddr;
			buf->size = PAGE_SIZE;
			num_buf++;
			buf++;

		}
	}

	mr->umem = umem;
	mr->access = access;
	mr->offset = ib_umem_offset(umem);
	mr->state = RXE_MR_STATE_VALID;
	mr->type = IB_MR_TYPE_USER;

	return 0;

err_cleanup_map:
	for (i = 0; i < mr->num_map; i++)
		kfree(mr->map[i]);
	kfree(mr->map);
err_release_umem:
	ib_umem_release(umem);
err_out:
	return err;
}

int rxe_mr_init_fast(int max_pages, struct rxe_mr *mr)
{
	int err;

	/* always allow remote access for FMRs */
	rxe_mr_init(IB_ACCESS_REMOTE, mr);

	err = rxe_mr_alloc(mr, max_pages);
	if (err)
		goto err1;

	mr->max_buf = max_pages;
	mr->state = RXE_MR_STATE_FREE;
	mr->type = IB_MR_TYPE_MEM_REG;

	return 0;

err1:
	return err;
}

static void lookup_iova(struct rxe_mr *mr, u64 iova, int *m_out, int *n_out,
			size_t *offset_out)
{
	size_t offset = iova - mr->ibmr.iova + mr->offset;
	int			map_index;
	int			buf_index;
	u64			length;

	if (likely(mr->page_shift)) {
		*offset_out = offset & mr->page_mask;
		offset >>= mr->page_shift;
		*n_out = offset & mr->map_mask;
		*m_out = offset >> mr->map_shift;
	} else {
		map_index = 0;
		buf_index = 0;

		length = mr->map[map_index]->buf[buf_index].size;

		while (offset >= length) {
			offset -= length;
			buf_index++;

			if (buf_index == RXE_BUF_PER_MAP) {
				map_index++;
				buf_index = 0;
			}
			length = mr->map[map_index]->buf[buf_index].size;
		}

		*m_out = map_index;
		*n_out = buf_index;
		*offset_out = offset;
	}
}

void *iova_to_vaddr(struct rxe_mr *mr, u64 iova, int length)
{
	size_t offset;
	int m, n;
	void *addr;

	if (mr->state != RXE_MR_STATE_VALID) {
		pr_warn("mr not in valid state\n");
		addr = NULL;
		goto out;
	}

	if (!mr->map) {
		addr = (void *)(uintptr_t)iova;
		goto out;
	}

	if (mr_check_range(mr, iova, length)) {
		pr_warn("range violation\n");
		addr = NULL;
		goto out;
	}

	lookup_iova(mr, iova, &m, &n, &offset);

	if (offset + length > mr->map[m]->buf[n].size) {
		pr_warn("crosses page boundary\n");
		addr = NULL;
		goto out;
	}

	addr = (void *)(uintptr_t)mr->map[m]->buf[n].addr + offset;

out:
	return addr;
}

/* copy data from a range (vaddr, vaddr+length-1) to or from
 * a mr object starting at iova.
 */
int rxe_mr_copy(struct rxe_mr *mr, u64 iova, void *addr, int length,
		enum rxe_mr_copy_dir dir)
{
	int			err;
	int			bytes;
	u8			*va;
	struct rxe_map		**map;
	struct rxe_phys_buf	*buf;
	int			m;
	int			i;
	size_t			offset;

	if (length == 0)
		return 0;

	if (mr->type == IB_MR_TYPE_DMA) {
		u8 *src, *dest;

		src = (dir == RXE_TO_MR_OBJ) ? addr : ((void *)(uintptr_t)iova);

		dest = (dir == RXE_TO_MR_OBJ) ? ((void *)(uintptr_t)iova) : addr;

		memcpy(dest, src, length);

		return 0;
	}

	WARN_ON_ONCE(!mr->map);

	err = mr_check_range(mr, iova, length);
	if (err) {
		err = -EFAULT;
		goto err1;
	}

	lookup_iova(mr, iova, &m, &i, &offset);

	map = mr->map + m;
	buf	= map[0]->buf + i;

	while (length > 0) {
		u8 *src, *dest;

		va	= (u8 *)(uintptr_t)buf->addr + offset;
		src = (dir == RXE_TO_MR_OBJ) ? addr : va;
		dest = (dir == RXE_TO_MR_OBJ) ? va : addr;

		bytes	= buf->size - offset;

		if (bytes > length)
			bytes = length;

		memcpy(dest, src, bytes);

		length	-= bytes;
		addr	+= bytes;

		offset	= 0;
		buf++;
		i++;

		if (i == RXE_BUF_PER_MAP) {
			i = 0;
			map++;
			buf = map[0]->buf;
		}
	}

	return 0;

err1:
	return err;
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

/* (1) find the mr corresponding to lkey/rkey
 *     depending on lookup_type
 * (2) verify that the (qp) pd matches the mr pd
 * (3) verify that the mr can support the requested access
 * (4) verify that mr state is valid
 */
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
		     mr_pd(mr) != pd || (access && !(access & mr->access)) ||
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
	int ret;

	mr = rxe_pool_get_index(&rxe->mr_pool, key >> 8);
	if (!mr) {
		pr_err("%s: No MR for key %#x\n", __func__, key);
		ret = -EINVAL;
		goto err;
	}

	if (mr->rkey ? (key != mr->rkey) : (key != mr->lkey)) {
		pr_err("%s: wr key (%#x) doesn't match mr key (%#x)\n",
			__func__, key, (mr->rkey ? mr->rkey : mr->lkey));
		ret = -EINVAL;
		goto err_drop_ref;
	}

	if (atomic_read(&mr->num_mw) > 0) {
		pr_warn("%s: Attempt to invalidate an MR while bound to MWs\n",
			__func__);
		ret = -EINVAL;
		goto err_drop_ref;
	}

	if (unlikely(mr->type != IB_MR_TYPE_MEM_REG)) {
		pr_warn("%s: mr->type (%d) is wrong type\n", __func__, mr->type);
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
		pr_warn("%s: mr->lkey = 0x%x not free\n",
			__func__, mr->lkey);
		return -EINVAL;
	}

	/* user can only register mr with qp in same protection domain */
	if (unlikely(qp->ibqp.pd != mr->ibmr.pd)) {
		pr_warn("%s: qp->pd and mr->pd don't match\n",
			__func__);
		return -EINVAL;
	}

	/* user is only allowed to change key portion of l/rkey */
	if (unlikely((mr->lkey & ~0xff) != (key & ~0xff))) {
		pr_warn("%s: key = 0x%x has wrong index mr->lkey = 0x%x\n",
			__func__, key, mr->lkey);
		return -EINVAL;
	}

	mr->access = access;
	mr->lkey = key;
	mr->rkey = (access & IB_ACCESS_REMOTE) ? key : 0;
	mr->ibmr.iova = wqe->wr.wr.reg.mr->iova;
	mr->state = RXE_MR_STATE_VALID;

	return 0;
}

int rxe_dereg_mr(struct ib_mr *ibmr, struct ib_udata *udata)
{
	struct rxe_mr *mr = to_rmr(ibmr);

	/* See IBA 10.6.7.2.6 */
	if (atomic_read(&mr->num_mw) > 0)
		return -EINVAL;

	rxe_cleanup(mr);

	return 0;
}

void rxe_mr_cleanup(struct rxe_pool_elem *elem)
{
	struct rxe_mr *mr = container_of(elem, typeof(*mr), elem);
	int i;

	rxe_put(mr_pd(mr));
	ib_umem_release(mr->umem);

	if (mr->map) {
		for (i = 0; i < mr->num_map; i++)
			kfree(mr->map[i]);

		kfree(mr->map);
	}
}
