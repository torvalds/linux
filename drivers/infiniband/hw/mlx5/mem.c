/*
 * Copyright (c) 2013-2015, Mellanox Technologies. All rights reserved.
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

#include <linux/io.h>
#include <rdma/ib_umem_odp.h>
#include "mlx5_ib.h"
#include <linux/jiffies.h>

/*
 * Fill in a physical address list. ib_umem_num_dma_blocks() entries will be
 * filled in the pas array.
 */
void mlx5_ib_populate_pas(struct ib_umem *umem, size_t page_size, __be64 *pas,
			  u64 access_flags)
{
	struct ib_block_iter biter;

	rdma_umem_for_each_dma_block (umem, &biter, page_size) {
		*pas = cpu_to_be64(rdma_block_iter_dma_address(&biter) |
				   access_flags);
		pas++;
	}
}

/*
 * Compute the page shift and page_offset for mailboxes that use a quantized
 * page_offset. The granulatity of the page offset scales according to page
 * size.
 */
unsigned long __mlx5_umem_find_best_quantized_pgoff(
	struct ib_umem *umem, unsigned long pgsz_bitmap,
	unsigned int page_offset_bits, u64 pgoff_bitmask, unsigned int scale,
	unsigned int *page_offset_quantized)
{
	const u64 page_offset_mask = (1UL << page_offset_bits) - 1;
	unsigned long page_size;
	u64 page_offset;

	page_size = ib_umem_find_best_pgoff(umem, pgsz_bitmap, pgoff_bitmask);
	if (!page_size)
		return 0;

	/*
	 * page size is the largest possible page size.
	 *
	 * Reduce the page_size, and thus the page_offset and quanta, until the
	 * page_offset fits into the mailbox field. Once page_size < scale this
	 * loop is guaranteed to terminate.
	 */
	page_offset = ib_umem_dma_offset(umem, page_size);
	while (page_offset & ~(u64)(page_offset_mask * (page_size / scale))) {
		page_size /= 2;
		page_offset = ib_umem_dma_offset(umem, page_size);
	}

	/*
	 * The address is not aligned, or otherwise cannot be represented by the
	 * page_offset.
	 */
	if (!(pgsz_bitmap & page_size))
		return 0;

	*page_offset_quantized =
		(unsigned long)page_offset / (page_size / scale);
	if (WARN_ON(*page_offset_quantized > page_offset_mask))
		return 0;
	return page_size;
}

#define WR_ID_BF 0xBF
#define WR_ID_END 0xBAD
#define TEST_WC_NUM_WQES 255
#define TEST_WC_POLLING_MAX_TIME_JIFFIES msecs_to_jiffies(100)
static int post_send_nop(struct mlx5_ib_dev *dev, struct ib_qp *ibqp, u64 wr_id,
			 bool signaled)
{
	struct mlx5_ib_qp *qp = to_mqp(ibqp);
	struct mlx5_wqe_ctrl_seg *ctrl;
	struct mlx5_bf *bf = &qp->bf;
	__be32 mmio_wqe[16] = {};
	unsigned long flags;
	unsigned int idx;

	if (unlikely(dev->mdev->state == MLX5_DEVICE_STATE_INTERNAL_ERROR))
		return -EIO;

	spin_lock_irqsave(&qp->sq.lock, flags);

	idx = qp->sq.cur_post & (qp->sq.wqe_cnt - 1);
	ctrl = mlx5_frag_buf_get_wqe(&qp->sq.fbc, idx);

	memset(ctrl, 0, sizeof(struct mlx5_wqe_ctrl_seg));
	ctrl->fm_ce_se = signaled ? MLX5_WQE_CTRL_CQ_UPDATE : 0;
	ctrl->opmod_idx_opcode =
		cpu_to_be32(((u32)(qp->sq.cur_post) << 8) | MLX5_OPCODE_NOP);
	ctrl->qpn_ds = cpu_to_be32((sizeof(struct mlx5_wqe_ctrl_seg) / 16) |
				   (qp->trans_qp.base.mqp.qpn << 8));

	qp->sq.wrid[idx] = wr_id;
	qp->sq.w_list[idx].opcode = MLX5_OPCODE_NOP;
	qp->sq.wqe_head[idx] = qp->sq.head + 1;
	qp->sq.cur_post += DIV_ROUND_UP(sizeof(struct mlx5_wqe_ctrl_seg),
					MLX5_SEND_WQE_BB);
	qp->sq.w_list[idx].next = qp->sq.cur_post;
	qp->sq.head++;

	memcpy(mmio_wqe, ctrl, sizeof(*ctrl));
	((struct mlx5_wqe_ctrl_seg *)&mmio_wqe)->fm_ce_se |=
		MLX5_WQE_CTRL_CQ_UPDATE;

	/* Make sure that descriptors are written before
	 * updating doorbell record and ringing the doorbell
	 */
	wmb();

	qp->db.db[MLX5_SND_DBR] = cpu_to_be32(qp->sq.cur_post);

	/* Make sure doorbell record is visible to the HCA before
	 * we hit doorbell
	 */
	wmb();
	__iowrite64_copy(bf->bfreg->map + bf->offset, mmio_wqe,
			 sizeof(mmio_wqe) / 8);

	bf->offset ^= bf->buf_size;

	spin_unlock_irqrestore(&qp->sq.lock, flags);

	return 0;
}

static int test_wc_poll_cq_result(struct mlx5_ib_dev *dev, struct ib_cq *cq)
{
	int ret;
	struct ib_wc wc = {};
	unsigned long end = jiffies + TEST_WC_POLLING_MAX_TIME_JIFFIES;

	do {
		ret = ib_poll_cq(cq, 1, &wc);
		if (ret < 0 || wc.status)
			return ret < 0 ? ret : -EINVAL;
		if (ret)
			break;
	} while (!time_after(jiffies, end));

	if (!ret)
		return -ETIMEDOUT;

	if (wc.wr_id != WR_ID_BF)
		ret = 0;

	return ret;
}

static int test_wc_do_send(struct mlx5_ib_dev *dev, struct ib_qp *qp)
{
	int err, i;

	for (i = 0; i < TEST_WC_NUM_WQES; i++) {
		err = post_send_nop(dev, qp, WR_ID_BF, false);
		if (err)
			return err;
	}

	return post_send_nop(dev, qp, WR_ID_END, true);
}

int mlx5_ib_test_wc(struct mlx5_ib_dev *dev)
{
	struct ib_cq_init_attr cq_attr = { .cqe = TEST_WC_NUM_WQES + 1 };
	int port_type_cap = MLX5_CAP_GEN(dev->mdev, port_type);
	struct ib_qp_init_attr qp_init_attr = {
		.cap = { .max_send_wr = TEST_WC_NUM_WQES },
		.qp_type = IB_QPT_UD,
		.sq_sig_type = IB_SIGNAL_REQ_WR,
		.create_flags = MLX5_IB_QP_CREATE_WC_TEST,
	};
	struct ib_qp_attr qp_attr = { .port_num = 1 };
	struct ib_device *ibdev = &dev->ib_dev;
	struct ib_qp *qp;
	struct ib_cq *cq;
	struct ib_pd *pd;
	int ret;

	if (!MLX5_CAP_GEN(dev->mdev, bf))
		return 0;

	if (!dev->mdev->roce.roce_en &&
	    port_type_cap == MLX5_CAP_PORT_TYPE_ETH) {
		if (mlx5_core_is_pf(dev->mdev))
			dev->wc_support = arch_can_pci_mmap_wc();
		return 0;
	}

	ret = mlx5_alloc_bfreg(dev->mdev, &dev->wc_bfreg, true, false);
	if (ret)
		goto print_err;

	if (!dev->wc_bfreg.wc)
		goto out1;

	pd = ib_alloc_pd(ibdev, 0);
	if (IS_ERR(pd)) {
		ret = PTR_ERR(pd);
		goto out1;
	}

	cq = ib_create_cq(ibdev, NULL, NULL, NULL, &cq_attr);
	if (IS_ERR(cq)) {
		ret = PTR_ERR(cq);
		goto out2;
	}

	qp_init_attr.recv_cq = cq;
	qp_init_attr.send_cq = cq;
	qp = ib_create_qp(pd, &qp_init_attr);
	if (IS_ERR(qp)) {
		ret = PTR_ERR(qp);
		goto out3;
	}

	qp_attr.qp_state = IB_QPS_INIT;
	ret = ib_modify_qp(qp, &qp_attr,
			   IB_QP_STATE | IB_QP_PORT | IB_QP_PKEY_INDEX |
				   IB_QP_QKEY);
	if (ret)
		goto out4;

	qp_attr.qp_state = IB_QPS_RTR;
	ret = ib_modify_qp(qp, &qp_attr, IB_QP_STATE);
	if (ret)
		goto out4;

	qp_attr.qp_state = IB_QPS_RTS;
	ret = ib_modify_qp(qp, &qp_attr, IB_QP_STATE | IB_QP_SQ_PSN);
	if (ret)
		goto out4;

	ret = test_wc_do_send(dev, qp);
	if (ret < 0)
		goto out4;

	ret = test_wc_poll_cq_result(dev, cq);
	if (ret > 0) {
		dev->wc_support = true;
		ret = 0;
	}

out4:
	ib_destroy_qp(qp);
out3:
	ib_destroy_cq(cq);
out2:
	ib_dealloc_pd(pd);
out1:
	mlx5_free_bfreg(dev->mdev, &dev->wc_bfreg);
print_err:
	if (ret)
		mlx5_ib_err(
			dev,
			"Error %d while trying to test write-combining support\n",
			ret);
	return ret;
}
