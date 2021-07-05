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
	case RXE_MR_TYPE_DMA:
		return 0;

	case RXE_MR_TYPE_MR:
		if (iova < mr->iova || length > mr->length ||
		    iova > mr->iova + mr->length - length)
			return -EFAULT;
		return 0;

	default:
		return -EFAULT;
	}
}

#define IB_ACCESS_REMOTE	(IB_ACCESS_REMOTE_READ		\
				| IB_ACCESS_REMOTE_WRITE	\
				| IB_ACCESS_REMOTE_ATOMIC)

static void rxe_mr_init(int access, struct rxe_mr *mr)
{
	u32 lkey = mr->pelem.index << 8 | rxe_get_next_key(-1);
	u32 rkey = (access & IB_ACCESS_REMOTE) ? lkey : 0;

	mr->ibmr.lkey = lkey;
	mr->ibmr.rkey = rkey;
	mr->state = RXE_MR_STATE_INVALID;
	mr->type = RXE_MR_TYPE_NONE;
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

void rxe_mr_init_dma(struct rxe_pd *pd, int access, struct rxe_mr *mr)
{
	rxe_mr_init(access, mr);

	mr->ibmr.pd = &pd->ibpd;
	mr->access = access;
	mr->state = RXE_MR_STATE_VALID;
	mr->type = RXE_MR_TYPE_DMA;
}

int rxe_mr_init_user(struct rxe_pd *pd, u64 start, u64 length, u64 iova,
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

	umem = ib_umem_get(pd->ibpd.device, start, length, access);
	if (IS_ERR(umem)) {
		pr_warn("%s: Unable to pin memory region err = %d\n",
			__func__, (int)PTR_ERR(umem));
		err = PTR_ERR(umem);
		goto err_out;
	}

	mr->umem = umem;
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

		for_each_sg_page(umem->sg_head.sgl, &sg_iter, umem->nmap, 0) {
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

	mr->ibmr.pd = &pd->ibpd;
	mr->umem = umem;
	mr->access = access;
	mr->length = length;
	mr->iova = iova;
	mr->va = start;
	mr->offset = ib_umem_offset(umem);
	mr->state = RXE_MR_STATE_VALID;
	mr->type = RXE_MR_TYPE_MR;

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

int rxe_mr_init_fast(struct rxe_pd *pd, int max_pages, struct rxe_mr *mr)
{
	int err;

	rxe_mr_init(0, mr);

	/* In fastreg, we also set the rkey */
	mr->ibmr.rkey = mr->ibmr.lkey;

	err = rxe_mr_alloc(mr, max_pages);
	if (err)
		goto err1;

	mr->ibmr.pd = &pd->ibpd;
	mr->max_buf = max_pages;
	mr->state = RXE_MR_STATE_FREE;
	mr->type = RXE_MR_TYPE_MR;

	return 0;

err1:
	return err;
}

static void lookup_iova(struct rxe_mr *mr, u64 iova, int *m_out, int *n_out,
			size_t *offset_out)
{
	size_t offset = iova - mr->iova + mr->offset;
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
 * a mr object starting at iova. Compute incremental value of
 * crc32 if crcp is not zero. caller must hold a reference to mr
 */
int rxe_mr_copy(struct rxe_mr *mr, u64 iova, void *addr, int length,
		enum rxe_mr_copy_dir dir, u32 *crcp)
{
	int			err;
	int			bytes;
	u8			*va;
	struct rxe_map		**map;
	struct rxe_phys_buf	*buf;
	int			m;
	int			i;
	size_t			offset;
	u32			crc = crcp ? (*crcp) : 0;

	if (length == 0)
		return 0;

	if (mr->type == RXE_MR_TYPE_DMA) {
		u8 *src, *dest;

		src = (dir == RXE_TO_MR_OBJ) ? addr : ((void *)(uintptr_t)iova);

		dest = (dir == RXE_TO_MR_OBJ) ? ((void *)(uintptr_t)iova) : addr;

		memcpy(dest, src, length);

		if (crcp)
			*crcp = rxe_crc32(to_rdev(mr->ibmr.device), *crcp, dest,
					  length);

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

		if (crcp)
			crc = rxe_crc32(to_rdev(mr->ibmr.device), crc, dest,
					bytes);

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

	if (crcp)
		*crcp = crc;

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
	enum rxe_mr_copy_dir	dir,
	u32			*crcp)
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
				rxe_drop_ref(mr);
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

			err = rxe_mr_copy(mr, iova, addr, bytes, dir, crcp);
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
		rxe_drop_ref(mr);

	return 0;

err2:
	if (mr)
		rxe_drop_ref(mr);
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

	if (unlikely((type == RXE_LOOKUP_LOCAL && mr_lkey(mr) != key) ||
		     (type == RXE_LOOKUP_REMOTE && mr_rkey(mr) != key) ||
		     mr_pd(mr) != pd || (access && !(access & mr->access)) ||
		     mr->state != RXE_MR_STATE_VALID)) {
		rxe_drop_ref(mr);
		mr = NULL;
	}

	return mr;
}

int rxe_invalidate_mr(struct rxe_qp *qp, u32 rkey)
{
	struct rxe_dev *rxe = to_rdev(qp->ibqp.device);
	struct rxe_mr *mr;
	int ret;

	mr = rxe_pool_get_index(&rxe->mr_pool, rkey >> 8);
	if (!mr) {
		pr_err("%s: No MR for rkey %#x\n", __func__, rkey);
		ret = -EINVAL;
		goto err;
	}

	if (rkey != mr->ibmr.rkey) {
		pr_err("%s: rkey (%#x) doesn't match mr->ibmr.rkey (%#x)\n",
			__func__, rkey, mr->ibmr.rkey);
		ret = -EINVAL;
		goto err_drop_ref;
	}

	if (atomic_read(&mr->num_mw) > 0) {
		pr_warn("%s: Attempt to invalidate an MR while bound to MWs\n",
			__func__);
		ret = -EINVAL;
		goto err_drop_ref;
	}

	mr->state = RXE_MR_STATE_FREE;
	ret = 0;

err_drop_ref:
	rxe_drop_ref(mr);
err:
	return ret;
}

int rxe_dereg_mr(struct ib_mr *ibmr, struct ib_udata *udata)
{
	struct rxe_mr *mr = to_rmr(ibmr);

	if (atomic_read(&mr->num_mw) > 0) {
		pr_warn("%s: Attempt to deregister an MR while bound to MWs\n",
			__func__);
		return -EINVAL;
	}

	mr->state = RXE_MR_STATE_ZOMBIE;
	rxe_drop_ref(mr_pd(mr));
	rxe_drop_index(mr);
	rxe_drop_ref(mr);

	return 0;
}

void rxe_mr_cleanup(struct rxe_pool_entry *arg)
{
	struct rxe_mr *mr = container_of(arg, typeof(*mr), pelem);
	int i;

	ib_umem_release(mr->umem);

	if (mr->map) {
		for (i = 0; i < mr->num_map; i++)
			kfree(mr->map[i]);

		kfree(mr->map);
	}
}
