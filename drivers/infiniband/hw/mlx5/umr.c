// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. */

#include <rdma/ib_umem_odp.h>
#include "mlx5_ib.h"
#include "umr.h"
#include "wr.h"

/*
 * We can't use an array for xlt_emergency_page because dma_map_single doesn't
 * work on kernel modules memory
 */
void *xlt_emergency_page;
static DEFINE_MUTEX(xlt_emergency_page_mutex);

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

static __be64 get_umr_update_translation_mask(struct mlx5_ib_dev *dev)
{
	u64 result;

	result = MLX5_MKEY_MASK_LEN |
		 MLX5_MKEY_MASK_PAGE_SIZE |
		 MLX5_MKEY_MASK_START_ADDR;
	if (MLX5_CAP_GEN_2(dev->mdev, umr_log_entity_size_5))
		result |= MLX5_MKEY_MASK_PAGE_SIZE_5;

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
	struct ib_cq *cq;
	struct ib_qp *qp;
	int ret = 0;


	/*
	 * UMR qp is set once, never changed until device unload.
	 * Avoid taking the mutex if initialization is already done.
	 */
	if (dev->umrc.qp)
		return 0;

	mutex_lock(&dev->umrc.init_lock);
	/* First user allocates the UMR resources. Skip if already allocated. */
	if (dev->umrc.qp)
		goto unlock;

	cq = ib_alloc_cq(&dev->ib_dev, NULL, 128, 0, IB_POLL_SOFTIRQ);
	if (IS_ERR(cq)) {
		mlx5_ib_dbg(dev, "Couldn't create CQ for sync UMR QP\n");
		ret = PTR_ERR(cq);
		goto unlock;
	}

	init_attr.send_cq = cq;
	init_attr.recv_cq = cq;
	init_attr.sq_sig_type = IB_SIGNAL_ALL_WR;
	init_attr.cap.max_send_wr = MAX_UMR_WR;
	init_attr.cap.max_send_sge = 1;
	init_attr.qp_type = MLX5_IB_QPT_REG_UMR;
	init_attr.port_num = 1;
	qp = ib_create_qp(dev->umrc.pd, &init_attr);
	if (IS_ERR(qp)) {
		mlx5_ib_dbg(dev, "Couldn't create sync UMR QP\n");
		ret = PTR_ERR(qp);
		goto destroy_cq;
	}

	ret = mlx5r_umr_qp_rst2rts(dev, qp);
	if (ret)
		goto destroy_qp;

	dev->umrc.cq = cq;

	sema_init(&dev->umrc.sem, MAX_UMR_WR);
	mutex_init(&dev->umrc.lock);
	dev->umrc.state = MLX5_UMR_STATE_ACTIVE;
	dev->umrc.qp = qp;

	mutex_unlock(&dev->umrc.init_lock);
	return 0;

destroy_qp:
	ib_destroy_qp(qp);
destroy_cq:
	ib_free_cq(cq);
unlock:
	mutex_unlock(&dev->umrc.init_lock);
	return ret;
}

void mlx5r_umr_resource_cleanup(struct mlx5_ib_dev *dev)
{
	if (dev->umrc.state == MLX5_UMR_STATE_UNINIT)
		return;
	mutex_destroy(&dev->umrc.lock);
	/* After device init, UMR cp/qp are not unset during the lifetime. */
	ib_destroy_qp(dev->umrc.qp);
	ib_free_cq(dev->umrc.cq);
}

int mlx5r_umr_init(struct mlx5_ib_dev *dev)
{
	struct ib_pd *pd;

	pd = ib_alloc_pd(&dev->ib_dev, 0);
	if (IS_ERR(pd)) {
		mlx5_ib_dbg(dev, "Couldn't create PD for sync UMR QP\n");
		return PTR_ERR(pd);
	}
	dev->umrc.pd = pd;

	mutex_init(&dev->umrc.init_lock);

	return 0;
}

void mlx5r_umr_cleanup(struct mlx5_ib_dev *dev)
{
	if (!dev->umrc.pd)
		return;

	mutex_destroy(&dev->umrc.init_lock);
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
			 MLX5_FENCE_MODE_INITIATOR_SMALL, MLX5_OPCODE_UMR);

	mlx5r_ring_db(qp, 1, ctrl);

out:
	spin_unlock_irqrestore(&qp->sq.lock, flags);

	return err;
}

static int mlx5r_umr_recover(struct mlx5_ib_dev *dev, u32 mkey,
			     struct mlx5r_umr_context *umr_context,
			     struct mlx5r_umr_wqe *wqe, bool with_data)
{
	struct umr_common *umrc = &dev->umrc;
	struct ib_qp_attr attr;
	int err;

	mutex_lock(&umrc->lock);
	/* Preventing any further WRs to be sent now */
	if (umrc->state != MLX5_UMR_STATE_RECOVER) {
		mlx5_ib_warn(dev, "UMR recovery encountered an unexpected state=%d\n",
			     umrc->state);
		umrc->state = MLX5_UMR_STATE_RECOVER;
	}
	mutex_unlock(&umrc->lock);

	/* Sending a final/barrier WR (the failed one) and wait for its completion.
	 * This will ensure that all the previous WRs got a completion before
	 * we set the QP state to RESET.
	 */
	err = mlx5r_umr_post_send(umrc->qp, mkey, &umr_context->cqe, wqe,
				  with_data);
	if (err) {
		mlx5_ib_warn(dev, "UMR recovery post send failed, err %d\n", err);
		goto err;
	}

	/* Since the QP is in an error state, it will only receive
	 * IB_WC_WR_FLUSH_ERR. However, as it serves only as a barrier
	 * we don't care about its status.
	 */
	wait_for_completion(&umr_context->done);

	attr.qp_state = IB_QPS_RESET;
	err = ib_modify_qp(umrc->qp, &attr, IB_QP_STATE);
	if (err) {
		mlx5_ib_warn(dev, "Couldn't modify UMR QP to RESET, err=%d\n", err);
		goto err;
	}

	err = mlx5r_umr_qp_rst2rts(dev, umrc->qp);
	if (err) {
		mlx5_ib_warn(dev, "Couldn't modify UMR QP to RTS, err=%d\n", err);
		goto err;
	}

	umrc->state = MLX5_UMR_STATE_ACTIVE;
	return 0;

err:
	umrc->state = MLX5_UMR_STATE_ERR;
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
	while (true) {
		mutex_lock(&umrc->lock);
		if (umrc->state == MLX5_UMR_STATE_ERR) {
			mutex_unlock(&umrc->lock);
			err = -EFAULT;
			break;
		}

		if (umrc->state == MLX5_UMR_STATE_RECOVER) {
			mutex_unlock(&umrc->lock);
			usleep_range(3000, 5000);
			continue;
		}

		err = mlx5r_umr_post_send(umrc->qp, mkey, &umr_context.cqe, wqe,
					  with_data);
		mutex_unlock(&umrc->lock);
		if (err) {
			mlx5_ib_warn(dev, "UMR post send failed, err %d\n",
				     err);
			break;
		}

		wait_for_completion(&umr_context.done);

		if (umr_context.status == IB_WC_SUCCESS)
			break;

		if (umr_context.status == IB_WC_WR_FLUSH_ERR)
			continue;

		WARN_ON_ONCE(1);
		mlx5_ib_warn(dev,
			"reg umr failed (%u). Trying to recover and resubmit the flushed WQEs, mkey = %u\n",
			umr_context.status, mkey);
		err = mlx5r_umr_recover(dev, mkey, &umr_context, wqe, with_data);
		if (err)
			mlx5_ib_warn(dev, "couldn't recover UMR, err %d\n",
				     err);
		err = -EFAULT;
		break;
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

static void mlx5r_umr_set_access_flags(struct mlx5_ib_dev *dev,
				       struct mlx5_mkey_seg *seg,
				       unsigned int access_flags)
{
	bool ro_read = (access_flags & IB_ACCESS_RELAXED_ORDERING) &&
		       (MLX5_CAP_GEN(dev->mdev, relaxed_ordering_read) ||
			pcie_relaxed_ordering_enabled(dev->mdev->pdev));

	MLX5_SET(mkc, seg, a, !!(access_flags & IB_ACCESS_REMOTE_ATOMIC));
	MLX5_SET(mkc, seg, rw, !!(access_flags & IB_ACCESS_REMOTE_WRITE));
	MLX5_SET(mkc, seg, rr, !!(access_flags & IB_ACCESS_REMOTE_READ));
	MLX5_SET(mkc, seg, lw, !!(access_flags & IB_ACCESS_LOCAL_WRITE));
	MLX5_SET(mkc, seg, lr, 1);
	MLX5_SET(mkc, seg, relaxed_ordering_write,
		 !!(access_flags & IB_ACCESS_RELAXED_ORDERING));
	MLX5_SET(mkc, seg, relaxed_ordering_read, ro_read);
}

int mlx5r_umr_rereg_pd_access(struct mlx5_ib_mr *mr, struct ib_pd *pd,
			      int access_flags)
{
	struct mlx5_ib_dev *dev = mr_to_mdev(mr);
	struct mlx5r_umr_wqe wqe = {};
	int err;

	wqe.ctrl_seg.mkey_mask = get_umr_update_access_mask(dev);
	wqe.ctrl_seg.mkey_mask |= get_umr_update_pd_mask();
	wqe.ctrl_seg.flags = MLX5_UMR_CHECK_FREE;
	wqe.ctrl_seg.flags |= MLX5_UMR_INLINE;

	mlx5r_umr_set_access_flags(dev, &wqe.mkey_seg, access_flags);
	MLX5_SET(mkc, &wqe.mkey_seg, pd, to_mpd(pd)->pdn);
	MLX5_SET(mkc, &wqe.mkey_seg, qpn, 0xffffff);
	MLX5_SET(mkc, &wqe.mkey_seg, mkey_7_0,
		 mlx5_mkey_variant(mr->mmkey.key));

	err = mlx5r_umr_post_send_wait(dev, mr->mmkey.key, &wqe, false);
	if (err)
		return err;

	mr->access_flags = access_flags;
	return 0;
}

#define MLX5_MAX_UMR_CHUNK                                                     \
	((1 << (MLX5_MAX_UMR_SHIFT + 4)) - MLX5_UMR_FLEX_ALIGNMENT)
#define MLX5_SPARE_UMR_CHUNK 0x10000

/*
 * Allocate a temporary buffer to hold the per-page information to transfer to
 * HW. For efficiency this should be as large as it can be, but buffer
 * allocation failure is not allowed, so try smaller sizes.
 */
static void *mlx5r_umr_alloc_xlt(size_t *nents, size_t ent_size, gfp_t gfp_mask)
{
	const size_t xlt_chunk_align = MLX5_UMR_FLEX_ALIGNMENT / ent_size;
	size_t size;
	void *res = NULL;

	static_assert(PAGE_SIZE % MLX5_UMR_FLEX_ALIGNMENT == 0);

	/*
	 * MLX5_IB_UPD_XLT_ATOMIC doesn't signal an atomic context just that the
	 * allocation can't trigger any kind of reclaim.
	 */
	might_sleep();

	gfp_mask |= __GFP_ZERO | __GFP_NORETRY;

	/*
	 * If the system already has a suitable high order page then just use
	 * that, but don't try hard to create one. This max is about 1M, so a
	 * free x86 huge page will satisfy it.
	 */
	size = min_t(size_t, ent_size * ALIGN(*nents, xlt_chunk_align),
		     MLX5_MAX_UMR_CHUNK);
	*nents = size / ent_size;
	res = (void *)__get_free_pages(gfp_mask | __GFP_NOWARN,
				       get_order(size));
	if (res)
		return res;

	if (size > MLX5_SPARE_UMR_CHUNK) {
		size = MLX5_SPARE_UMR_CHUNK;
		*nents = size / ent_size;
		res = (void *)__get_free_pages(gfp_mask | __GFP_NOWARN,
					       get_order(size));
		if (res)
			return res;
	}

	*nents = PAGE_SIZE / ent_size;
	res = (void *)__get_free_page(gfp_mask);
	if (res)
		return res;

	mutex_lock(&xlt_emergency_page_mutex);
	memset(xlt_emergency_page, 0, PAGE_SIZE);
	return xlt_emergency_page;
}

static void mlx5r_umr_free_xlt(void *xlt, size_t length)
{
	if (xlt == xlt_emergency_page) {
		mutex_unlock(&xlt_emergency_page_mutex);
		return;
	}

	free_pages((unsigned long)xlt, get_order(length));
}

static void mlx5r_umr_unmap_free_xlt(struct mlx5_ib_dev *dev, void *xlt,
				     struct ib_sge *sg)
{
	struct device *ddev = &dev->mdev->pdev->dev;

	dma_unmap_single(ddev, sg->addr, sg->length, DMA_TO_DEVICE);
	mlx5r_umr_free_xlt(xlt, sg->length);
}

/*
 * Create an XLT buffer ready for submission.
 */
static void *mlx5r_umr_create_xlt(struct mlx5_ib_dev *dev, struct ib_sge *sg,
				  size_t nents, size_t ent_size,
				  unsigned int flags)
{
	struct device *ddev = &dev->mdev->pdev->dev;
	dma_addr_t dma;
	void *xlt;

	xlt = mlx5r_umr_alloc_xlt(&nents, ent_size,
				 flags & MLX5_IB_UPD_XLT_ATOMIC ? GFP_ATOMIC :
								  GFP_KERNEL);
	sg->length = nents * ent_size;
	dma = dma_map_single(ddev, xlt, sg->length, DMA_TO_DEVICE);
	if (dma_mapping_error(ddev, dma)) {
		mlx5_ib_err(dev, "unable to map DMA during XLT update.\n");
		mlx5r_umr_free_xlt(xlt, sg->length);
		return NULL;
	}
	sg->addr = dma;
	sg->lkey = dev->umrc.pd->local_dma_lkey;

	return xlt;
}

static void
mlx5r_umr_set_update_xlt_ctrl_seg(struct mlx5_wqe_umr_ctrl_seg *ctrl_seg,
				  unsigned int flags, struct ib_sge *sg)
{
	if (!(flags & MLX5_IB_UPD_XLT_ENABLE))
		/* fail if free */
		ctrl_seg->flags = MLX5_UMR_CHECK_FREE;
	else
		/* fail if not free */
		ctrl_seg->flags = MLX5_UMR_CHECK_NOT_FREE;
	ctrl_seg->xlt_octowords =
		cpu_to_be16(mlx5r_umr_get_xlt_octo(sg->length));
}

static void mlx5r_umr_set_update_xlt_mkey_seg(struct mlx5_ib_dev *dev,
					      struct mlx5_mkey_seg *mkey_seg,
					      struct mlx5_ib_mr *mr,
					      unsigned int page_shift)
{
	mlx5r_umr_set_access_flags(dev, mkey_seg, mr->access_flags);
	MLX5_SET(mkc, mkey_seg, pd, to_mpd(mr->ibmr.pd)->pdn);
	MLX5_SET64(mkc, mkey_seg, start_addr, mr->ibmr.iova);
	MLX5_SET64(mkc, mkey_seg, len, mr->ibmr.length);
	MLX5_SET(mkc, mkey_seg, log_page_size, page_shift);
	MLX5_SET(mkc, mkey_seg, qpn, 0xffffff);
	MLX5_SET(mkc, mkey_seg, mkey_7_0, mlx5_mkey_variant(mr->mmkey.key));
}

static void
mlx5r_umr_set_update_xlt_data_seg(struct mlx5_wqe_data_seg *data_seg,
				  struct ib_sge *sg)
{
	data_seg->byte_count = cpu_to_be32(sg->length);
	data_seg->lkey = cpu_to_be32(sg->lkey);
	data_seg->addr = cpu_to_be64(sg->addr);
}

static void mlx5r_umr_update_offset(struct mlx5_wqe_umr_ctrl_seg *ctrl_seg,
				    u64 offset)
{
	u64 octo_offset = mlx5r_umr_get_xlt_octo(offset);

	ctrl_seg->xlt_offset = cpu_to_be16(octo_offset & 0xffff);
	ctrl_seg->xlt_offset_47_16 = cpu_to_be32(octo_offset >> 16);
	ctrl_seg->flags |= MLX5_UMR_TRANSLATION_OFFSET_EN;
}

static void mlx5r_umr_final_update_xlt(struct mlx5_ib_dev *dev,
				       struct mlx5r_umr_wqe *wqe,
				       struct mlx5_ib_mr *mr, struct ib_sge *sg,
				       unsigned int flags)
{
	bool update_pd_access, update_translation;

	if (flags & MLX5_IB_UPD_XLT_ENABLE)
		wqe->ctrl_seg.mkey_mask |= get_umr_enable_mr_mask();

	update_pd_access = flags & MLX5_IB_UPD_XLT_ENABLE ||
			   flags & MLX5_IB_UPD_XLT_PD ||
			   flags & MLX5_IB_UPD_XLT_ACCESS;

	if (update_pd_access) {
		wqe->ctrl_seg.mkey_mask |= get_umr_update_access_mask(dev);
		wqe->ctrl_seg.mkey_mask |= get_umr_update_pd_mask();
	}

	update_translation =
		flags & MLX5_IB_UPD_XLT_ENABLE || flags & MLX5_IB_UPD_XLT_ADDR;

	if (update_translation) {
		wqe->ctrl_seg.mkey_mask |= get_umr_update_translation_mask(dev);
		if (!mr->ibmr.length)
			MLX5_SET(mkc, &wqe->mkey_seg, length64, 1);
		if (flags & MLX5_IB_UPD_XLT_KEEP_PGSZ)
			wqe->ctrl_seg.mkey_mask &=
				cpu_to_be64(~MLX5_MKEY_MASK_PAGE_SIZE);
	}

	wqe->ctrl_seg.xlt_octowords =
		cpu_to_be16(mlx5r_umr_get_xlt_octo(sg->length));
	wqe->data_seg.byte_count = cpu_to_be32(sg->length);
}

static void
_mlx5r_umr_init_wqe(struct mlx5_ib_mr *mr, struct mlx5r_umr_wqe *wqe,
		    struct ib_sge *sg, unsigned int flags,
		    unsigned int page_shift, bool dd)
{
	struct mlx5_ib_dev *dev = mr_to_mdev(mr);

	mlx5r_umr_set_update_xlt_ctrl_seg(&wqe->ctrl_seg, flags, sg);
	mlx5r_umr_set_update_xlt_mkey_seg(dev, &wqe->mkey_seg, mr, page_shift);
	if (dd) /* Use the data direct internal kernel PD */
		MLX5_SET(mkc, &wqe->mkey_seg, pd, dev->ddr.pdn);
	mlx5r_umr_set_update_xlt_data_seg(&wqe->data_seg, sg);
}

static int
_mlx5r_umr_update_mr_pas(struct mlx5_ib_mr *mr, unsigned int flags, bool dd,
			 size_t start_block, size_t nblocks)
{
	size_t ent_size = dd ? sizeof(struct mlx5_ksm) : sizeof(struct mlx5_mtt);
	struct mlx5_ib_dev *dev = mr_to_mdev(mr);
	struct device *ddev = &dev->mdev->pdev->dev;
	struct mlx5r_umr_wqe wqe = {};
	size_t processed_blocks = 0;
	struct ib_block_iter biter;
	size_t cur_block_idx = 0;
	struct mlx5_ksm *cur_ksm;
	struct mlx5_mtt *cur_mtt;
	size_t orig_sg_length;
	size_t total_blocks;
	size_t final_size;
	void *curr_entry;
	struct ib_sge sg;
	void *entry;
	u64 offset;
	int err = 0;

	total_blocks = ib_umem_num_dma_blocks(mr->umem, 1UL << mr->page_shift);
	if (start_block > total_blocks)
		return -EINVAL;

	/* nblocks 0 means update all blocks starting from start_block */
	if (nblocks)
		total_blocks = nblocks;

	entry = mlx5r_umr_create_xlt(dev, &sg, total_blocks, ent_size, flags);
	if (!entry)
		return -ENOMEM;

	orig_sg_length = sg.length;

	_mlx5r_umr_init_wqe(mr, &wqe, &sg, flags, mr->page_shift, dd);

	/* Set initial translation offset to start_block */
	offset = (u64)start_block * ent_size;
	mlx5r_umr_update_offset(&wqe.ctrl_seg, offset);

	if (dd)
		cur_ksm = entry;
	else
		cur_mtt = entry;

	curr_entry = entry;

	rdma_umem_for_each_dma_block(mr->umem, &biter, BIT(mr->page_shift)) {
		if (cur_block_idx < start_block) {
			cur_block_idx++;
			continue;
		}

		if (nblocks && processed_blocks >= nblocks)
			break;

		if (curr_entry == entry + sg.length) {
			dma_sync_single_for_device(ddev, sg.addr, sg.length,
						   DMA_TO_DEVICE);

			err = mlx5r_umr_post_send_wait(dev, mr->mmkey.key, &wqe,
						       true);
			if (err)
				goto err;
			dma_sync_single_for_cpu(ddev, sg.addr, sg.length,
						DMA_TO_DEVICE);
			offset += sg.length;
			mlx5r_umr_update_offset(&wqe.ctrl_seg, offset);
			if (dd)
				cur_ksm = entry;
			else
				cur_mtt = entry;
		}

		if (dd) {
			cur_ksm->va = cpu_to_be64(rdma_block_iter_dma_address(&biter));
			if (mr->access_flags & IB_ACCESS_RELAXED_ORDERING &&
			    dev->ddr.mkey_ro_valid)
				cur_ksm->key = cpu_to_be32(dev->ddr.mkey_ro);
			else
				cur_ksm->key = cpu_to_be32(dev->ddr.mkey);
			if (mr->umem->is_dmabuf &&
			    (flags & MLX5_IB_UPD_XLT_ZAP)) {
				cur_ksm->va = 0;
				cur_ksm->key = 0;
			}
			cur_ksm++;
			curr_entry = cur_ksm;
		} else {
			cur_mtt->ptag =
				cpu_to_be64(rdma_block_iter_dma_address(&biter) |
					    MLX5_IB_MTT_PRESENT);
			if (mr->umem->is_dmabuf && (flags & MLX5_IB_UPD_XLT_ZAP))
				cur_mtt->ptag = 0;
			cur_mtt++;
			curr_entry = cur_mtt;
		}

		processed_blocks++;
	}

	final_size = curr_entry - entry;
	sg.length = ALIGN(final_size, MLX5_UMR_FLEX_ALIGNMENT);
	memset(curr_entry, 0, sg.length - final_size);
	mlx5r_umr_final_update_xlt(dev, &wqe, mr, &sg, flags);

	dma_sync_single_for_device(ddev, sg.addr, sg.length, DMA_TO_DEVICE);
	err = mlx5r_umr_post_send_wait(dev, mr->mmkey.key, &wqe, true);

err:
	sg.length = orig_sg_length;
	mlx5r_umr_unmap_free_xlt(dev, entry, &sg);
	return err;
}

int mlx5r_umr_update_data_direct_ksm_pas_range(struct mlx5_ib_mr *mr,
					       unsigned int flags,
					       size_t start_block,
					       size_t nblocks)
{
	/* No invalidation flow is expected */
	if (WARN_ON(!mr->umem->is_dmabuf) || ((flags & MLX5_IB_UPD_XLT_ZAP) &&
	    !(flags & MLX5_IB_UPD_XLT_KEEP_PGSZ)))
		return -EINVAL;

	return _mlx5r_umr_update_mr_pas(mr, flags, true, start_block, nblocks);
}

int mlx5r_umr_update_data_direct_ksm_pas(struct mlx5_ib_mr *mr,
					 unsigned int flags)
{
	return mlx5r_umr_update_data_direct_ksm_pas_range(mr, flags, 0, 0);
}

int mlx5r_umr_update_mr_pas_range(struct mlx5_ib_mr *mr, unsigned int flags,
				  size_t start_block, size_t nblocks)
{
	if (WARN_ON(mr->umem->is_odp))
		return -EINVAL;

	return _mlx5r_umr_update_mr_pas(mr, flags, false, start_block, nblocks);
}

/*
 * Send the DMA list to the HW for a normal MR using UMR.
 * Dmabuf MR is handled in a similar way, except that the MLX5_IB_UPD_XLT_ZAP
 * flag may be used.
 */
int mlx5r_umr_update_mr_pas(struct mlx5_ib_mr *mr, unsigned int flags)
{
	return mlx5r_umr_update_mr_pas_range(mr, flags, 0, 0);
}

static bool umr_can_use_indirect_mkey(struct mlx5_ib_dev *dev)
{
	return !MLX5_CAP_GEN(dev->mdev, umr_indirect_mkey_disabled);
}

int mlx5r_umr_update_xlt(struct mlx5_ib_mr *mr, u64 idx, int npages,
			 int page_shift, int flags)
{
	int desc_size = (flags & MLX5_IB_UPD_XLT_INDIRECT)
			       ? sizeof(struct mlx5_klm)
			       : sizeof(struct mlx5_mtt);
	const int page_align = MLX5_UMR_FLEX_ALIGNMENT / desc_size;
	struct mlx5_ib_dev *dev = mr_to_mdev(mr);
	struct device *ddev = &dev->mdev->pdev->dev;
	const int page_mask = page_align - 1;
	struct mlx5r_umr_wqe wqe = {};
	size_t pages_mapped = 0;
	size_t pages_to_map = 0;
	size_t size_to_map = 0;
	size_t orig_sg_length;
	size_t pages_iter;
	struct ib_sge sg;
	int err = 0;
	void *xlt;

	if ((flags & MLX5_IB_UPD_XLT_INDIRECT) &&
	    !umr_can_use_indirect_mkey(dev))
		return -EPERM;

	if (WARN_ON(!mr->umem->is_odp))
		return -EINVAL;

	/* UMR copies MTTs in units of MLX5_UMR_FLEX_ALIGNMENT bytes,
	 * so we need to align the offset and length accordingly
	 */
	if (idx & page_mask) {
		npages += idx & page_mask;
		idx &= ~page_mask;
	}
	pages_to_map = ALIGN(npages, page_align);

	xlt = mlx5r_umr_create_xlt(dev, &sg, npages, desc_size, flags);
	if (!xlt)
		return -ENOMEM;

	pages_iter = sg.length / desc_size;
	orig_sg_length = sg.length;

	if (!(flags & MLX5_IB_UPD_XLT_INDIRECT)) {
		struct ib_umem_odp *odp = to_ib_umem_odp(mr->umem);
		size_t max_pages = ib_umem_odp_num_pages(odp) - idx;

		pages_to_map = min_t(size_t, pages_to_map, max_pages);
	}

	mlx5r_umr_set_update_xlt_ctrl_seg(&wqe.ctrl_seg, flags, &sg);
	mlx5r_umr_set_update_xlt_mkey_seg(dev, &wqe.mkey_seg, mr, page_shift);
	mlx5r_umr_set_update_xlt_data_seg(&wqe.data_seg, &sg);

	for (pages_mapped = 0;
	     pages_mapped < pages_to_map && !err;
	     pages_mapped += pages_iter, idx += pages_iter) {
		npages = min_t(int, pages_iter, pages_to_map - pages_mapped);
		size_to_map = npages * desc_size;
		dma_sync_single_for_cpu(ddev, sg.addr, sg.length,
					DMA_TO_DEVICE);
		/*
		 * npages is the maximum number of pages to map, but we
		 * can't guarantee that all pages are actually mapped.
		 *
		 * For example, if page is p2p of type which is not supported
		 * for mapping, the number of pages mapped will be less than
		 * requested.
		 */
		err = mlx5_odp_populate_xlt(xlt, idx, npages, mr, flags);
		if (err)
			return err;
		dma_sync_single_for_device(ddev, sg.addr, sg.length,
					   DMA_TO_DEVICE);
		sg.length = ALIGN(size_to_map, MLX5_UMR_FLEX_ALIGNMENT);

		if (pages_mapped + pages_iter >= pages_to_map)
			mlx5r_umr_final_update_xlt(dev, &wqe, mr, &sg, flags);
		mlx5r_umr_update_offset(&wqe.ctrl_seg, idx * desc_size);
		err = mlx5r_umr_post_send_wait(dev, mr->mmkey.key, &wqe, true);
	}
	sg.length = orig_sg_length;
	mlx5r_umr_unmap_free_xlt(dev, xlt, &sg);
	return err;
}

/*
 * Update only the page-size (log_page_size) field of an existing memory key
 * using UMR.  This is useful when the MR's physical layout stays the same
 * but the optimal page shift has changed (e.g. dmabuf after pages are
 * pinned and the HW can switch from 4K to huge-page alignment).
 */
int mlx5r_umr_update_mr_page_shift(struct mlx5_ib_mr *mr,
				   unsigned int page_shift,
				   bool dd)
{
	struct mlx5_ib_dev *dev = mr_to_mdev(mr);
	struct mlx5r_umr_wqe wqe = {};
	int err;

	/* Build UMR wqe: we touch only PAGE_SIZE, so use the dedicated mask */
	wqe.ctrl_seg.mkey_mask = get_umr_update_translation_mask(dev);

	/* MR must be free while page size is modified */
	wqe.ctrl_seg.flags = MLX5_UMR_CHECK_FREE | MLX5_UMR_INLINE;

	/* Fill mkey segment with the new page size, keep the rest unchanged */
	MLX5_SET(mkc, &wqe.mkey_seg, log_page_size, page_shift);

	if (dd)
		MLX5_SET(mkc, &wqe.mkey_seg, pd, dev->ddr.pdn);
	else
		MLX5_SET(mkc, &wqe.mkey_seg, pd, to_mpd(mr->ibmr.pd)->pdn);

	MLX5_SET64(mkc, &wqe.mkey_seg, start_addr, mr->ibmr.iova);
	MLX5_SET64(mkc, &wqe.mkey_seg, len, mr->ibmr.length);
	MLX5_SET(mkc, &wqe.mkey_seg, qpn, 0xffffff);
	MLX5_SET(mkc, &wqe.mkey_seg, mkey_7_0,
		 mlx5_mkey_variant(mr->mmkey.key));

	err = mlx5r_umr_post_send_wait(dev, mr->mmkey.key, &wqe, false);
	if (!err)
		mr->page_shift = page_shift;

	return err;
}

static inline int
_mlx5r_dmabuf_umr_update_pas(struct mlx5_ib_mr *mr, unsigned int flags,
			     size_t start_block, size_t nblocks, bool dd)
{
	if (dd)
		return mlx5r_umr_update_data_direct_ksm_pas_range(mr, flags,
								  start_block,
								  nblocks);
	else
		return mlx5r_umr_update_mr_pas_range(mr, flags, start_block,
						     nblocks);
}

/**
 * This function makes an mkey non-present by zapping the translation entries of
 * the mkey by zapping (zeroing out) the first N entries, where N is determined
 * by the largest page size supported by the device and the MR length.
 * It then updates the mkey's page size to the largest possible value, ensuring
 * the MR is completely non-present and safe for further updates.
 * It is useful to update the page size of a dmabuf MR on a page fault.
 *
 * Return: On success, returns the number of entries that were zapped.
 *         On error, returns a negative error code.
 */
static int _mlx5r_umr_zap_mkey(struct mlx5_ib_mr *mr,
			       unsigned int flags,
			       unsigned int page_shift,
			       size_t *nblocks,
			       bool dd)
{
	unsigned int old_page_shift = mr->page_shift;
	struct mlx5_ib_dev *dev = mr_to_mdev(mr);
	unsigned int max_page_shift;
	size_t page_shift_nblocks;
	unsigned int max_log_size;
	int access_mode;
	int err;

	access_mode = dd ? MLX5_MKC_ACCESS_MODE_KSM : MLX5_MKC_ACCESS_MODE_MTT;
	flags |= MLX5_IB_UPD_XLT_KEEP_PGSZ | MLX5_IB_UPD_XLT_ZAP |
		 MLX5_IB_UPD_XLT_ATOMIC;
	max_log_size = get_max_log_entity_size_cap(dev, access_mode);
	max_page_shift = order_base_2(mr->ibmr.length);
	max_page_shift = min(max(max_page_shift, page_shift), max_log_size);
	/* Count blocks in units of max_page_shift, we will zap exactly this
	 * many to make the whole MR non-present.
	 * Block size must be aligned to MLX5_UMR_FLEX_ALIGNMENT since it may
	 * be used as offset into the XLT later on.
	 */
	*nblocks = ib_umem_num_dma_blocks(mr->umem, 1UL << max_page_shift);
	if (dd)
		*nblocks = ALIGN(*nblocks, MLX5_UMR_KSM_NUM_ENTRIES_ALIGNMENT);
	else
		*nblocks = ALIGN(*nblocks, MLX5_UMR_MTT_NUM_ENTRIES_ALIGNMENT);
	page_shift_nblocks = ib_umem_num_dma_blocks(mr->umem,
						    1UL << page_shift);
	/* If the number of blocks at max possible page shift is greater than
	 * the number of blocks at the new page size, we should just go over the
	 * whole mkey entries.
	 */
	if (*nblocks >= page_shift_nblocks)
		*nblocks = 0;

	/* Make the first nblocks entries non-present without changing
	 * page size yet.
	 */
	if (*nblocks)
		mr->page_shift = max_page_shift;
	err = _mlx5r_dmabuf_umr_update_pas(mr, flags, 0, *nblocks, dd);
	if (err) {
		mr->page_shift = old_page_shift;
		return err;
	}

	/* Change page size to the max page size now that the MR is completely
	 * non-present.
	 */
	if (*nblocks) {
		err = mlx5r_umr_update_mr_page_shift(mr, max_page_shift, dd);
		if (err) {
			mr->page_shift = old_page_shift;
			return err;
		}
	}

	return 0;
}

/**
 * mlx5r_umr_dmabuf_update_pgsz - Safely update DMABUF MR page size and its
 * entries accordingly
 * @mr:        The memory region to update
 * @xlt_flags: Translation table update flags
 * @page_shift: The new (optimized) page shift to use
 *
 * This function updates the page size and mkey translation entries for a DMABUF
 * MR in a safe, multi-step process to avoid exposing partially updated mappings
 * The update is performed in 5 steps:
 *   1. Make the first X entries non-present, while X is calculated to be
 *        minimal according to a large page shift that can be used to cover the
 *        MR length.
 *   2. Update the page size to the large supported page size
 *   3. Load the remaining N-X entries according to the (optimized) page_shift
 *   4. Update the page size according to the (optimized) page_shift
 *   5. Load the first X entries with the correct translations
 *
 * This ensures that at no point is the MR accessible with a partially updated
 * translation table, maintaining correctness and preventing access to stale or
 * inconsistent mappings.
 *
 * Returns 0 on success or a negative error code on failure.
 */
int mlx5r_umr_dmabuf_update_pgsz(struct mlx5_ib_mr *mr, u32 xlt_flags,
				 unsigned int page_shift)
{
	unsigned int old_page_shift = mr->page_shift;
	size_t zapped_blocks;
	size_t total_blocks;
	int err;

	err = _mlx5r_umr_zap_mkey(mr, xlt_flags, page_shift, &zapped_blocks,
				  mr->data_direct);
	if (err)
		return err;

	/* _mlx5r_umr_zap_mkey already enables the mkey */
	xlt_flags &= ~MLX5_IB_UPD_XLT_ENABLE;
	mr->page_shift = page_shift;
	total_blocks = ib_umem_num_dma_blocks(mr->umem, 1UL << mr->page_shift);
	if (zapped_blocks && zapped_blocks < total_blocks) {
		/* Update PAS according to the new page size but don't update
		 * the page size in the mkey yet.
		 */
		err = _mlx5r_dmabuf_umr_update_pas(
			mr,
			xlt_flags | MLX5_IB_UPD_XLT_KEEP_PGSZ,
			zapped_blocks,
			total_blocks - zapped_blocks,
			mr->data_direct);
		if (err)
			goto err;
	}

	err = mlx5r_umr_update_mr_page_shift(mr, mr->page_shift,
					     mr->data_direct);
	if (err)
		goto err;
	err = _mlx5r_dmabuf_umr_update_pas(mr, xlt_flags, 0, zapped_blocks,
					   mr->data_direct);
	if (err)
		goto err;

	return 0;
err:
	mr->page_shift = old_page_shift;
	return err;
}
