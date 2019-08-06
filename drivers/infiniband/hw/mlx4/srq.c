/*
 * Copyright (c) 2007 Cisco Systems, Inc. All rights reserved.
 * Copyright (c) 2007, 2008 Mellanox Technologies. All rights reserved.
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

#include <linux/mlx4/qp.h>
#include <linux/mlx4/srq.h>
#include <linux/slab.h>

#include "mlx4_ib.h"
#include <rdma/mlx4-abi.h>

static void *get_wqe(struct mlx4_ib_srq *srq, int n)
{
	return mlx4_buf_offset(&srq->buf, n << srq->msrq.wqe_shift);
}

static void mlx4_ib_srq_event(struct mlx4_srq *srq, enum mlx4_event type)
{
	struct ib_event event;
	struct ib_srq *ibsrq = &to_mibsrq(srq)->ibsrq;

	if (ibsrq->event_handler) {
		event.device      = ibsrq->device;
		event.element.srq = ibsrq;
		switch (type) {
		case MLX4_EVENT_TYPE_SRQ_LIMIT:
			event.event = IB_EVENT_SRQ_LIMIT_REACHED;
			break;
		case MLX4_EVENT_TYPE_SRQ_CATAS_ERROR:
			event.event = IB_EVENT_SRQ_ERR;
			break;
		default:
			pr_warn("Unexpected event type %d "
			       "on SRQ %06x\n", type, srq->srqn);
			return;
		}

		ibsrq->event_handler(&event, ibsrq->srq_context);
	}
}

struct ib_srq *mlx4_ib_create_srq(struct ib_pd *pd,
				  struct ib_srq_init_attr *init_attr,
				  struct ib_udata *udata)
{
	struct mlx4_ib_dev *dev = to_mdev(pd->device);
	struct mlx4_ib_srq *srq;
	struct mlx4_wqe_srq_next_seg *next;
	struct mlx4_wqe_data_seg *scatter;
	u32 cqn;
	u16 xrcdn;
	int desc_size;
	int buf_size;
	int err;
	int i;

	/* Sanity check SRQ size before proceeding */
	if (init_attr->attr.max_wr  >= dev->dev->caps.max_srq_wqes ||
	    init_attr->attr.max_sge >  dev->dev->caps.max_srq_sge)
		return ERR_PTR(-EINVAL);

	srq = kmalloc(sizeof *srq, GFP_KERNEL);
	if (!srq)
		return ERR_PTR(-ENOMEM);

	mutex_init(&srq->mutex);
	spin_lock_init(&srq->lock);
	srq->msrq.max    = roundup_pow_of_two(init_attr->attr.max_wr + 1);
	srq->msrq.max_gs = init_attr->attr.max_sge;

	desc_size = max(32UL,
			roundup_pow_of_two(sizeof (struct mlx4_wqe_srq_next_seg) +
					   srq->msrq.max_gs *
					   sizeof (struct mlx4_wqe_data_seg)));
	srq->msrq.wqe_shift = ilog2(desc_size);

	buf_size = srq->msrq.max * desc_size;

	if (udata) {
		struct mlx4_ib_create_srq ucmd;

		if (ib_copy_from_udata(&ucmd, udata, sizeof ucmd)) {
			err = -EFAULT;
			goto err_srq;
		}

		srq->umem = ib_umem_get(pd->uobject->context, ucmd.buf_addr,
					buf_size, 0, 0);
		if (IS_ERR(srq->umem)) {
			err = PTR_ERR(srq->umem);
			goto err_srq;
		}

		err = mlx4_mtt_init(dev->dev, ib_umem_page_count(srq->umem),
				    srq->umem->page_shift, &srq->mtt);
		if (err)
			goto err_buf;

		err = mlx4_ib_umem_write_mtt(dev, &srq->mtt, srq->umem);
		if (err)
			goto err_mtt;

		err = mlx4_ib_db_map_user(to_mucontext(pd->uobject->context),
					  ucmd.db_addr, &srq->db);
		if (err)
			goto err_mtt;
	} else {
		err = mlx4_db_alloc(dev->dev, &srq->db, 0);
		if (err)
			goto err_srq;

		*srq->db.db = 0;

		if (mlx4_buf_alloc(dev->dev, buf_size, PAGE_SIZE * 2,
				   &srq->buf)) {
			err = -ENOMEM;
			goto err_db;
		}

		srq->head    = 0;
		srq->tail    = srq->msrq.max - 1;
		srq->wqe_ctr = 0;

		for (i = 0; i < srq->msrq.max; ++i) {
			next = get_wqe(srq, i);
			next->next_wqe_index =
				cpu_to_be16((i + 1) & (srq->msrq.max - 1));

			for (scatter = (void *) (next + 1);
			     (void *) scatter < (void *) next + desc_size;
			     ++scatter)
				scatter->lkey = cpu_to_be32(MLX4_INVALID_LKEY);
		}

		err = mlx4_mtt_init(dev->dev, srq->buf.npages, srq->buf.page_shift,
				    &srq->mtt);
		if (err)
			goto err_buf;

		err = mlx4_buf_write_mtt(dev->dev, &srq->mtt, &srq->buf);
		if (err)
			goto err_mtt;

		srq->wrid = kvmalloc_array(srq->msrq.max,
					   sizeof(u64), GFP_KERNEL);
		if (!srq->wrid) {
			err = -ENOMEM;
			goto err_mtt;
		}
	}

	cqn = ib_srq_has_cq(init_attr->srq_type) ?
		to_mcq(init_attr->ext.cq)->mcq.cqn : 0;
	xrcdn = (init_attr->srq_type == IB_SRQT_XRC) ?
		to_mxrcd(init_attr->ext.xrc.xrcd)->xrcdn :
		(u16) dev->dev->caps.reserved_xrcds;
	err = mlx4_srq_alloc(dev->dev, to_mpd(pd)->pdn, cqn, xrcdn, &srq->mtt,
			     srq->db.dma, &srq->msrq);
	if (err)
		goto err_wrid;

	srq->msrq.event = mlx4_ib_srq_event;
	srq->ibsrq.ext.xrc.srq_num = srq->msrq.srqn;

	if (udata)
		if (ib_copy_to_udata(udata, &srq->msrq.srqn, sizeof (__u32))) {
			err = -EFAULT;
			goto err_wrid;
		}

	init_attr->attr.max_wr = srq->msrq.max - 1;

	return &srq->ibsrq;

err_wrid:
	if (udata)
		mlx4_ib_db_unmap_user(to_mucontext(pd->uobject->context), &srq->db);
	else
		kvfree(srq->wrid);

err_mtt:
	mlx4_mtt_cleanup(dev->dev, &srq->mtt);

err_buf:
	if (srq->umem)
		ib_umem_release(srq->umem);
	else
		mlx4_buf_free(dev->dev, buf_size, &srq->buf);

err_db:
	if (!udata)
		mlx4_db_free(dev->dev, &srq->db);

err_srq:
	kfree(srq);

	return ERR_PTR(err);
}

int mlx4_ib_modify_srq(struct ib_srq *ibsrq, struct ib_srq_attr *attr,
		       enum ib_srq_attr_mask attr_mask, struct ib_udata *udata)
{
	struct mlx4_ib_dev *dev = to_mdev(ibsrq->device);
	struct mlx4_ib_srq *srq = to_msrq(ibsrq);
	int ret;

	/* We don't support resizing SRQs (yet?) */
	if (attr_mask & IB_SRQ_MAX_WR)
		return -EINVAL;

	if (attr_mask & IB_SRQ_LIMIT) {
		if (attr->srq_limit >= srq->msrq.max)
			return -EINVAL;

		mutex_lock(&srq->mutex);
		ret = mlx4_srq_arm(dev->dev, &srq->msrq, attr->srq_limit);
		mutex_unlock(&srq->mutex);

		if (ret)
			return ret;
	}

	return 0;
}

int mlx4_ib_query_srq(struct ib_srq *ibsrq, struct ib_srq_attr *srq_attr)
{
	struct mlx4_ib_dev *dev = to_mdev(ibsrq->device);
	struct mlx4_ib_srq *srq = to_msrq(ibsrq);
	int ret;
	int limit_watermark;

	ret = mlx4_srq_query(dev->dev, &srq->msrq, &limit_watermark);
	if (ret)
		return ret;

	srq_attr->srq_limit = limit_watermark;
	srq_attr->max_wr    = srq->msrq.max - 1;
	srq_attr->max_sge   = srq->msrq.max_gs;

	return 0;
}

int mlx4_ib_destroy_srq(struct ib_srq *srq)
{
	struct mlx4_ib_dev *dev = to_mdev(srq->device);
	struct mlx4_ib_srq *msrq = to_msrq(srq);

	mlx4_srq_free(dev->dev, &msrq->msrq);
	mlx4_mtt_cleanup(dev->dev, &msrq->mtt);

	if (srq->uobject) {
		mlx4_ib_db_unmap_user(to_mucontext(srq->uobject->context), &msrq->db);
		ib_umem_release(msrq->umem);
	} else {
		kvfree(msrq->wrid);
		mlx4_buf_free(dev->dev, msrq->msrq.max << msrq->msrq.wqe_shift,
			      &msrq->buf);
		mlx4_db_free(dev->dev, &msrq->db);
	}

	kfree(msrq);

	return 0;
}

void mlx4_ib_free_srq_wqe(struct mlx4_ib_srq *srq, int wqe_index)
{
	struct mlx4_wqe_srq_next_seg *next;

	/* always called with interrupts disabled. */
	spin_lock(&srq->lock);

	next = get_wqe(srq, srq->tail);
	next->next_wqe_index = cpu_to_be16(wqe_index);
	srq->tail = wqe_index;

	spin_unlock(&srq->lock);
}

int mlx4_ib_post_srq_recv(struct ib_srq *ibsrq, const struct ib_recv_wr *wr,
			  const struct ib_recv_wr **bad_wr)
{
	struct mlx4_ib_srq *srq = to_msrq(ibsrq);
	struct mlx4_wqe_srq_next_seg *next;
	struct mlx4_wqe_data_seg *scat;
	unsigned long flags;
	int err = 0;
	int nreq;
	int i;
	struct mlx4_ib_dev *mdev = to_mdev(ibsrq->device);

	spin_lock_irqsave(&srq->lock, flags);
	if (mdev->dev->persist->state & MLX4_DEVICE_STATE_INTERNAL_ERROR) {
		err = -EIO;
		*bad_wr = wr;
		nreq = 0;
		goto out;
	}

	for (nreq = 0; wr; ++nreq, wr = wr->next) {
		if (unlikely(wr->num_sge > srq->msrq.max_gs)) {
			err = -EINVAL;
			*bad_wr = wr;
			break;
		}

		if (unlikely(srq->head == srq->tail)) {
			err = -ENOMEM;
			*bad_wr = wr;
			break;
		}

		srq->wrid[srq->head] = wr->wr_id;

		next      = get_wqe(srq, srq->head);
		srq->head = be16_to_cpu(next->next_wqe_index);
		scat      = (struct mlx4_wqe_data_seg *) (next + 1);

		for (i = 0; i < wr->num_sge; ++i) {
			scat[i].byte_count = cpu_to_be32(wr->sg_list[i].length);
			scat[i].lkey       = cpu_to_be32(wr->sg_list[i].lkey);
			scat[i].addr       = cpu_to_be64(wr->sg_list[i].addr);
		}

		if (i < srq->msrq.max_gs) {
			scat[i].byte_count = 0;
			scat[i].lkey       = cpu_to_be32(MLX4_INVALID_LKEY);
			scat[i].addr       = 0;
		}
	}

	if (likely(nreq)) {
		srq->wqe_ctr += nreq;

		/*
		 * Make sure that descriptors are written before
		 * doorbell record.
		 */
		wmb();

		*srq->db.db = cpu_to_be32(srq->wqe_ctr);
	}
out:

	spin_unlock_irqrestore(&srq->lock, flags);

	return err;
}
