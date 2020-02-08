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

#include <linux/module.h>
#include <rdma/ib_umem.h>
#include <rdma/ib_umem_odp.h>
#include "mlx5_ib.h"
#include <linux/jiffies.h>

/* @umem: umem object to scan
 * @addr: ib virtual address requested by the user
 * @max_page_shift: high limit for page_shift - 0 means no limit
 * @count: number of PAGE_SIZE pages covered by umem
 * @shift: page shift for the compound pages found in the region
 * @ncont: number of compund pages
 * @order: log2 of the number of compound pages
 */
void mlx5_ib_cont_pages(struct ib_umem *umem, u64 addr,
			unsigned long max_page_shift,
			int *count, int *shift,
			int *ncont, int *order)
{
	unsigned long tmp;
	unsigned long m;
	u64 base = ~0, p = 0;
	u64 len, pfn;
	int i = 0;
	struct scatterlist *sg;
	int entry;

	addr = addr >> PAGE_SHIFT;
	tmp = (unsigned long)addr;
	m = find_first_bit(&tmp, BITS_PER_LONG);
	if (max_page_shift)
		m = min_t(unsigned long, max_page_shift - PAGE_SHIFT, m);

	for_each_sg(umem->sg_head.sgl, sg, umem->nmap, entry) {
		len = sg_dma_len(sg) >> PAGE_SHIFT;
		pfn = sg_dma_address(sg) >> PAGE_SHIFT;
		if (base + p != pfn) {
			/* If either the offset or the new
			 * base are unaligned update m
			 */
			tmp = (unsigned long)(pfn | p);
			if (!IS_ALIGNED(tmp, 1 << m))
				m = find_first_bit(&tmp, BITS_PER_LONG);

			base = pfn;
			p = 0;
		}

		p += len;
		i += len;
	}

	if (i) {
		m = min_t(unsigned long, ilog2(roundup_pow_of_two(i)), m);

		if (order)
			*order = ilog2(roundup_pow_of_two(i) >> m);

		*ncont = DIV_ROUND_UP(i, (1 << m));
	} else {
		m  = 0;

		if (order)
			*order = 0;

		*ncont = 0;
	}
	*shift = PAGE_SHIFT + m;
	*count = i;
}

/*
 * Populate the given array with bus addresses from the umem.
 *
 * dev - mlx5_ib device
 * umem - umem to use to fill the pages
 * page_shift - determines the page size used in the resulting array
 * offset - offset into the umem to start from,
 *          only implemented for ODP umems
 * num_pages - total number of pages to fill
 * pas - bus addresses array to fill
 * access_flags - access flags to set on all present pages.
		  use enum mlx5_ib_mtt_access_flags for this.
 */
void __mlx5_ib_populate_pas(struct mlx5_ib_dev *dev, struct ib_umem *umem,
			    int page_shift, size_t offset, size_t num_pages,
			    __be64 *pas, int access_flags)
{
	int shift = page_shift - PAGE_SHIFT;
	int mask = (1 << shift) - 1;
	int i, k, idx;
	u64 cur = 0;
	u64 base;
	int len;
	struct scatterlist *sg;
	int entry;

	i = 0;
	for_each_sg(umem->sg_head.sgl, sg, umem->nmap, entry) {
		len = sg_dma_len(sg) >> PAGE_SHIFT;
		base = sg_dma_address(sg);

		/* Skip elements below offset */
		if (i + len < offset << shift) {
			i += len;
			continue;
		}

		/* Skip pages below offset */
		if (i < offset << shift) {
			k = (offset << shift) - i;
			i = offset << shift;
		} else {
			k = 0;
		}

		for (; k < len; k++) {
			if (!(i & mask)) {
				cur = base + (k << PAGE_SHIFT);
				cur |= access_flags;
				idx = (i >> shift) - offset;

				pas[idx] = cpu_to_be64(cur);
				mlx5_ib_dbg(dev, "pas[%d] 0x%llx\n",
					    i >> shift, be64_to_cpu(pas[idx]));
			}
			i++;

			/* Stop after num_pages reached */
			if (i >> shift >= offset + num_pages)
				return;
		}
	}
}

void mlx5_ib_populate_pas(struct mlx5_ib_dev *dev, struct ib_umem *umem,
			  int page_shift, __be64 *pas, int access_flags)
{
	return __mlx5_ib_populate_pas(dev, umem, page_shift, 0,
				      ib_umem_num_pages(umem), pas,
				      access_flags);
}
int mlx5_ib_get_buf_offset(u64 addr, int page_shift, u32 *offset)
{
	u64 page_size;
	u64 page_mask;
	u64 off_size;
	u64 off_mask;
	u64 buf_off;

	page_size = (u64)1 << page_shift;
	page_mask = page_size - 1;
	buf_off = addr & page_mask;
	off_size = page_size >> 6;
	off_mask = off_size - 1;

	if (buf_off & off_mask)
		return -EINVAL;

	*offset = buf_off >> ilog2(off_size);
	return 0;
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
	int i;

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
	for (i = 0; i < 8; i++)
		mlx5_write64(&mmio_wqe[i * 2],
			     bf->bfreg->map + bf->offset + i * 8);

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
			dev->wc_support = true;
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
