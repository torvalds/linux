// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. */

#include "mlx5_ib.h"
#include "umr.h"
#include "wr.h"

static __be64 get_umr_enable_mr_mask(void)
{
	u64 result;

	result = MLX5_MKEY_MASK_KEY |
		 MLX5_MKEY_MASK_FREE;

	return cpu_to_be64(result);
}

static __be64 get_umr_disable_mr_mask(void)
{
	u64 result;

	result = MLX5_MKEY_MASK_FREE;

	return cpu_to_be64(result);
}

static __be64 get_umr_update_translation_mask(void)
{
	u64 result;

	result = MLX5_MKEY_MASK_LEN |
		 MLX5_MKEY_MASK_PAGE_SIZE |
		 MLX5_MKEY_MASK_START_ADDR;

	return cpu_to_be64(result);
}

static __be64 get_umr_update_access_mask(struct mlx5_ib_dev *dev)
{
	u64 result;

	result = MLX5_MKEY_MASK_LR |
		 MLX5_MKEY_MASK_LW |
		 MLX5_MKEY_MASK_RR |
		 MLX5_MKEY_MASK_RW;

	if (MLX5_CAP_GEN(dev->mdev, atomic))
		result |= MLX5_MKEY_MASK_A;

	if (MLX5_CAP_GEN(dev->mdev, relaxed_ordering_write_umr))
		result |= MLX5_MKEY_MASK_RELAXED_ORDERING_WRITE;

	if (MLX5_CAP_GEN(dev->mdev, relaxed_ordering_read_umr))
		result |= MLX5_MKEY_MASK_RELAXED_ORDERING_READ;

	return cpu_to_be64(result);
}

static __be64 get_umr_update_pd_mask(void)
{
	u64 result;

	result = MLX5_MKEY_MASK_PD;

	return cpu_to_be64(result);
}

static int umr_check_mkey_mask(struct mlx5_ib_dev *dev, u64 mask)
{
	if (mask & MLX5_MKEY_MASK_PAGE_SIZE &&
	    MLX5_CAP_GEN(dev->mdev, umr_modify_entity_size_disabled))
		return -EPERM;

	if (mask & MLX5_MKEY_MASK_A &&
	    MLX5_CAP_GEN(dev->mdev, umr_modify_atomic_disabled))
		return -EPERM;

	if (mask & MLX5_MKEY_MASK_RELAXED_ORDERING_WRITE &&
	    !MLX5_CAP_GEN(dev->mdev, relaxed_ordering_write_umr))
		return -EPERM;

	if (mask & MLX5_MKEY_MASK_RELAXED_ORDERING_READ &&
	    !MLX5_CAP_GEN(dev->mdev, relaxed_ordering_read_umr))
		return -EPERM;

	return 0;
}

int mlx5r_umr_set_umr_ctrl_seg(struct mlx5_ib_dev *dev,
			       struct mlx5_wqe_umr_ctrl_seg *umr,
			       const struct ib_send_wr *wr)
{
	const struct mlx5_umr_wr *umrwr = umr_wr(wr);

	memset(umr, 0, sizeof(*umr));

	if (!umrwr->ignore_free_state) {
		if (wr->send_flags & MLX5_IB_SEND_UMR_FAIL_IF_FREE)
			 /* fail if free */
			umr->flags = MLX5_UMR_CHECK_FREE;
		else
			/* fail if not free */
			umr->flags = MLX5_UMR_CHECK_NOT_FREE;
	}

	umr->xlt_octowords =
		cpu_to_be16(mlx5r_umr_get_xlt_octo(umrwr->xlt_size));
	if (wr->send_flags & MLX5_IB_SEND_UMR_UPDATE_XLT) {
		u64 offset = mlx5r_umr_get_xlt_octo(umrwr->offset);

		umr->xlt_offset = cpu_to_be16(offset & 0xffff);
		umr->xlt_offset_47_16 = cpu_to_be32(offset >> 16);
		umr->flags |= MLX5_UMR_TRANSLATION_OFFSET_EN;
	}
	if (wr->send_flags & MLX5_IB_SEND_UMR_UPDATE_TRANSLATION)
		umr->mkey_mask |= get_umr_update_translation_mask();
	if (wr->send_flags & MLX5_IB_SEND_UMR_UPDATE_PD_ACCESS) {
		umr->mkey_mask |= get_umr_update_access_mask(dev);
		umr->mkey_mask |= get_umr_update_pd_mask();
	}
	if (wr->send_flags & MLX5_IB_SEND_UMR_ENABLE_MR)
		umr->mkey_mask |= get_umr_enable_mr_mask();
	if (wr->send_flags & MLX5_IB_SEND_UMR_DISABLE_MR)
		umr->mkey_mask |= get_umr_disable_mr_mask();

	if (!wr->num_sge)
		umr->flags |= MLX5_UMR_INLINE;

	return umr_check_mkey_mask(dev, be64_to_cpu(umr->mkey_mask));
}

enum {
	MAX_UMR_WR = 128,
};

static int mlx5r_umr_qp_rst2rts(struct mlx5_ib_dev *dev, struct ib_qp *qp)
{
	struct ib_qp_attr attr = {};
	int ret;

	attr.qp_state = IB_QPS_INIT;
	attr.port_num = 1;
	ret = ib_modify_qp(qp, &attr,
			   IB_QP_STATE | IB_QP_PKEY_INDEX | IB_QP_PORT);
	if (ret) {
		mlx5_ib_dbg(dev, "Couldn't modify UMR QP\n");
		return ret;
	}

	memset(&attr, 0, sizeof(attr));
	attr.qp_state = IB_QPS_RTR;

	ret = ib_modify_qp(qp, &attr, IB_QP_STATE);
	if (ret) {
		mlx5_ib_dbg(dev, "Couldn't modify umr QP to rtr\n");
		return ret;
	}

	memset(&attr, 0, sizeof(attr));
	attr.qp_state = IB_QPS_RTS;
	ret = ib_modify_qp(qp, &attr, IB_QP_STATE);
	if (ret) {
		mlx5_ib_dbg(dev, "Couldn't modify umr QP to rts\n");
		return ret;
	}

	return 0;
}

int mlx5r_umr_resource_init(struct mlx5_ib_dev *dev)
{
	struct ib_qp_init_attr init_attr = {};
	struct ib_pd *pd;
	struct ib_cq *cq;
	struct ib_qp *qp;
	int ret;

	pd = ib_alloc_pd(&dev->ib_dev, 0);
	if (IS_ERR(pd)) {
		mlx5_ib_dbg(dev, "Couldn't create PD for sync UMR QP\n");
		return PTR_ERR(pd);
	}

	cq = ib_alloc_cq(&dev->ib_dev, NULL, 128, 0, IB_POLL_SOFTIRQ);
	if (IS_ERR(cq)) {
		mlx5_ib_dbg(dev, "Couldn't create CQ for sync UMR QP\n");
		ret = PTR_ERR(cq);
		goto destroy_pd;
	}

	init_attr.send_cq = cq;
	init_attr.recv_cq = cq;
	init_attr.sq_sig_type = IB_SIGNAL_ALL_WR;
	init_attr.cap.max_send_wr = MAX_UMR_WR;
	init_attr.cap.max_send_sge = 1;
	init_attr.qp_type = MLX5_IB_QPT_REG_UMR;
	init_attr.port_num = 1;
	qp = ib_create_qp(pd, &init_attr);
	if (IS_ERR(qp)) {
		mlx5_ib_dbg(dev, "Couldn't create sync UMR QP\n");
		ret = PTR_ERR(qp);
		goto destroy_cq;
	}

	ret = mlx5r_umr_qp_rst2rts(dev, qp);
	if (ret)
		goto destroy_qp;

	dev->umrc.qp = qp;
	dev->umrc.cq = cq;
	dev->umrc.pd = pd;

	sema_init(&dev->umrc.sem, MAX_UMR_WR);

	return 0;

destroy_qp:
	ib_destroy_qp(qp);
destroy_cq:
	ib_free_cq(cq);
destroy_pd:
	ib_dealloc_pd(pd);
	return ret;
}

void mlx5r_umr_resource_cleanup(struct mlx5_ib_dev *dev)
{
	ib_destroy_qp(dev->umrc.qp);
	ib_free_cq(dev->umrc.cq);
	ib_dealloc_pd(dev->umrc.pd);
}

static int mlx5r_umr_post_send(struct ib_qp *ibqp, u32 mkey, struct ib_cqe *cqe,
			       struct mlx5r_umr_wqe *wqe, bool with_data)
{
	unsigned int wqe_size =
		with_data ? sizeof(struct mlx5r_umr_wqe) :
			    sizeof(struct mlx5r_umr_wqe) -
				    sizeof(struct mlx5_wqe_data_seg);
	struct mlx5_ib_dev *dev = to_mdev(ibqp->device);
	struct mlx5_core_dev *mdev = dev->mdev;
	struct mlx5_ib_qp *qp = to_mqp(ibqp);
	struct mlx5_wqe_ctrl_seg *ctrl;
	union {
		struct ib_cqe *ib_cqe;
		u64 wr_id;
	} id;
	void *cur_edge, *seg;
	unsigned long flags;
	unsigned int idx;
	int size, err;

	if (unlikely(mdev->state == MLX5_DEVICE_STATE_INTERNAL_ERROR))
		return -EIO;

	spin_lock_irqsave(&qp->sq.lock, flags);

	err = mlx5r_begin_wqe(qp, &seg, &ctrl, &idx, &size, &cur_edge, 0,
			      cpu_to_be32(mkey), false, false);
	if (WARN_ON(err))
		goto out;

	qp->sq.wr_data[idx] = MLX5_IB_WR_UMR;

	mlx5r_memcpy_send_wqe(&qp->sq, &cur_edge, &seg, &size, wqe, wqe_size);

	id.ib_cqe = cqe;
	mlx5r_finish_wqe(qp, ctrl, seg, size, cur_edge, idx, id.wr_id, 0,
			 MLX5_FENCE_MODE_NONE, MLX5_OPCODE_UMR);

	mlx5r_ring_db(qp, 1, ctrl);

out:
	spin_unlock_irqrestore(&qp->sq.lock, flags);

	return err;
}

static void mlx5r_umr_done(struct ib_cq *cq, struct ib_wc *wc)
{
	struct mlx5_ib_umr_context *context =
		container_of(wc->wr_cqe, struct mlx5_ib_umr_context, cqe);

	context->status = wc->status;
	complete(&context->done);
}

static inline void mlx5r_umr_init_context(struct mlx5r_umr_context *context)
{
	context->cqe.done = mlx5r_umr_done;
	init_completion(&context->done);
}

static int mlx5r_umr_post_send_wait(struct mlx5_ib_dev *dev, u32 mkey,
				   struct mlx5r_umr_wqe *wqe, bool with_data)
{
	struct umr_common *umrc = &dev->umrc;
	struct mlx5r_umr_context umr_context;
	int err;

	err = umr_check_mkey_mask(dev, be64_to_cpu(wqe->ctrl_seg.mkey_mask));
	if (WARN_ON(err))
		return err;

	mlx5r_umr_init_context(&umr_context);

	down(&umrc->sem);
	err = mlx5r_umr_post_send(umrc->qp, mkey, &umr_context.cqe, wqe,
				  with_data);
	if (err)
		mlx5_ib_warn(dev, "UMR post send failed, err %d\n", err);
	else {
		wait_for_completion(&umr_context.done);
		if (umr_context.status != IB_WC_SUCCESS) {
			mlx5_ib_warn(dev, "reg umr failed (%u)\n",
				     umr_context.status);
			err = -EFAULT;
		}
	}
	up(&umrc->sem);
	return err;
}

/**
 * mlx5r_umr_revoke_mr - Fence all DMA on the MR
 * @mr: The MR to fence
 *
 * Upon return the NIC will not be doing any DMA to the pages under the MR,
 * and any DMA in progress will be completed. Failure of this function
 * indicates the HW has failed catastrophically.
 */
int mlx5r_umr_revoke_mr(struct mlx5_ib_mr *mr)
{
	struct mlx5_ib_dev *dev = mr_to_mdev(mr);
	struct mlx5r_umr_wqe wqe = {};

	if (dev->mdev->state == MLX5_DEVICE_STATE_INTERNAL_ERROR)
		return 0;

	wqe.ctrl_seg.mkey_mask |= get_umr_update_pd_mask();
	wqe.ctrl_seg.mkey_mask |= get_umr_disable_mr_mask();
	wqe.ctrl_seg.flags |= MLX5_UMR_INLINE;

	MLX5_SET(mkc, &wqe.mkey_seg, free, 1);
	MLX5_SET(mkc, &wqe.mkey_seg, pd, to_mpd(dev->umrc.pd)->pdn);
	MLX5_SET(mkc, &wqe.mkey_seg, qpn, 0xffffff);
	MLX5_SET(mkc, &wqe.mkey_seg, mkey_7_0,
		 mlx5_mkey_variant(mr->mmkey.key));

	return mlx5r_umr_post_send_wait(dev, mr->mmkey.key, &wqe, false);
}
