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

#include <linux/etherdevice.h>
#include <rdma/ib_umem.h>
#include <rdma/ib_cache.h>
#include <rdma/ib_user_verbs.h>
#include <rdma/rdma_counter.h>
#include <linux/mlx5/fs.h>
#include "mlx5_ib.h"
#include "ib_rep.h"
#include "counters.h"
#include "cmd.h"
#include "umr.h"
#include "qp.h"
#include "wr.h"

enum {
	MLX5_IB_ACK_REQ_FREQ	= 8,
};

enum {
	MLX5_IB_DEFAULT_SCHED_QUEUE	= 0x83,
	MLX5_IB_DEFAULT_QP0_SCHED_QUEUE	= 0x3f,
	MLX5_IB_LINK_TYPE_IB		= 0,
	MLX5_IB_LINK_TYPE_ETH		= 1
};

enum raw_qp_set_mask_map {
	MLX5_RAW_QP_MOD_SET_RQ_Q_CTR_ID		= 1UL << 0,
	MLX5_RAW_QP_RATE_LIMIT			= 1UL << 1,
};

enum {
	MLX5_QP_RM_GO_BACK_N			= 0x1,
};

struct mlx5_modify_raw_qp_param {
	u16 operation;

	u32 set_mask; /* raw_qp_set_mask_map */

	struct mlx5_rate_limit rl;

	u8 rq_q_ctr_id;
	u32 port;
};

struct mlx5_ib_qp_event_work {
	struct work_struct work;
	struct mlx5_core_qp *qp;
	int type;
};

static struct workqueue_struct *mlx5_ib_qp_event_wq;

static void get_cqs(enum ib_qp_type qp_type,
		    struct ib_cq *ib_send_cq, struct ib_cq *ib_recv_cq,
		    struct mlx5_ib_cq **send_cq, struct mlx5_ib_cq **recv_cq);

static int is_qp0(enum ib_qp_type qp_type)
{
	return qp_type == IB_QPT_SMI;
}

static int is_sqp(enum ib_qp_type qp_type)
{
	return is_qp0(qp_type) || is_qp1(qp_type);
}

/**
 * mlx5_ib_read_user_wqe_common() - Copy a WQE (or part of) from user WQ
 * to kernel buffer
 *
 * @umem: User space memory where the WQ is
 * @buffer: buffer to copy to
 * @buflen: buffer length
 * @wqe_index: index of WQE to copy from
 * @wq_offset: offset to start of WQ
 * @wq_wqe_cnt: number of WQEs in WQ
 * @wq_wqe_shift: log2 of WQE size
 * @bcnt: number of bytes to copy
 * @bytes_copied: number of bytes to copy (return value)
 *
 * Copies from start of WQE bcnt or less bytes.
 * Does not gurantee to copy the entire WQE.
 *
 * Return: zero on success, or an error code.
 */
static int mlx5_ib_read_user_wqe_common(struct ib_umem *umem, void *buffer,
					size_t buflen, int wqe_index,
					int wq_offset, int wq_wqe_cnt,
					int wq_wqe_shift, int bcnt,
					size_t *bytes_copied)
{
	size_t offset = wq_offset + ((wqe_index % wq_wqe_cnt) << wq_wqe_shift);
	size_t wq_end = wq_offset + (wq_wqe_cnt << wq_wqe_shift);
	size_t copy_length;
	int ret;

	/* don't copy more than requested, more than buffer length or
	 * beyond WQ end
	 */
	copy_length = min_t(u32, buflen, wq_end - offset);
	copy_length = min_t(u32, copy_length, bcnt);

	ret = ib_umem_copy_from(buffer, umem, offset, copy_length);
	if (ret)
		return ret;

	if (!ret && bytes_copied)
		*bytes_copied = copy_length;

	return 0;
}

static int mlx5_ib_read_kernel_wqe_sq(struct mlx5_ib_qp *qp, int wqe_index,
				      void *buffer, size_t buflen, size_t *bc)
{
	struct mlx5_wqe_ctrl_seg *ctrl;
	size_t bytes_copied = 0;
	size_t wqe_length;
	void *p;
	int ds;

	wqe_index = wqe_index & qp->sq.fbc.sz_m1;

	/* read the control segment first */
	p = mlx5_frag_buf_get_wqe(&qp->sq.fbc, wqe_index);
	ctrl = p;
	ds = be32_to_cpu(ctrl->qpn_ds) & MLX5_WQE_CTRL_DS_MASK;
	wqe_length = ds * MLX5_WQE_DS_UNITS;

	/* read rest of WQE if it spreads over more than one stride */
	while (bytes_copied < wqe_length) {
		size_t copy_length =
			min_t(size_t, buflen - bytes_copied, MLX5_SEND_WQE_BB);

		if (!copy_length)
			break;

		memcpy(buffer + bytes_copied, p, copy_length);
		bytes_copied += copy_length;

		wqe_index = (wqe_index + 1) & qp->sq.fbc.sz_m1;
		p = mlx5_frag_buf_get_wqe(&qp->sq.fbc, wqe_index);
	}
	*bc = bytes_copied;
	return 0;
}

static int mlx5_ib_read_user_wqe_sq(struct mlx5_ib_qp *qp, int wqe_index,
				    void *buffer, size_t buflen, size_t *bc)
{
	struct mlx5_ib_qp_base *base = &qp->trans_qp.base;
	struct ib_umem *umem = base->ubuffer.umem;
	struct mlx5_ib_wq *wq = &qp->sq;
	struct mlx5_wqe_ctrl_seg *ctrl;
	size_t bytes_copied;
	size_t bytes_copied2;
	size_t wqe_length;
	int ret;
	int ds;

	/* at first read as much as possible */
	ret = mlx5_ib_read_user_wqe_common(umem, buffer, buflen, wqe_index,
					   wq->offset, wq->wqe_cnt,
					   wq->wqe_shift, buflen,
					   &bytes_copied);
	if (ret)
		return ret;

	/* we need at least control segment size to proceed */
	if (bytes_copied < sizeof(*ctrl))
		return -EINVAL;

	ctrl = buffer;
	ds = be32_to_cpu(ctrl->qpn_ds) & MLX5_WQE_CTRL_DS_MASK;
	wqe_length = ds * MLX5_WQE_DS_UNITS;

	/* if we copied enough then we are done */
	if (bytes_copied >= wqe_length) {
		*bc = bytes_copied;
		return 0;
	}

	/* otherwise this a wrapped around wqe
	 * so read the remaining bytes starting
	 * from  wqe_index 0
	 */
	ret = mlx5_ib_read_user_wqe_common(umem, buffer + bytes_copied,
					   buflen - bytes_copied, 0, wq->offset,
					   wq->wqe_cnt, wq->wqe_shift,
					   wqe_length - bytes_copied,
					   &bytes_copied2);

	if (ret)
		return ret;
	*bc = bytes_copied + bytes_copied2;
	return 0;
}

int mlx5_ib_read_wqe_sq(struct mlx5_ib_qp *qp, int wqe_index, void *buffer,
			size_t buflen, size_t *bc)
{
	struct mlx5_ib_qp_base *base = &qp->trans_qp.base;
	struct ib_umem *umem = base->ubuffer.umem;

	if (buflen < sizeof(struct mlx5_wqe_ctrl_seg))
		return -EINVAL;

	if (!umem)
		return mlx5_ib_read_kernel_wqe_sq(qp, wqe_index, buffer,
						  buflen, bc);

	return mlx5_ib_read_user_wqe_sq(qp, wqe_index, buffer, buflen, bc);
}

static int mlx5_ib_read_user_wqe_rq(struct mlx5_ib_qp *qp, int wqe_index,
				    void *buffer, size_t buflen, size_t *bc)
{
	struct mlx5_ib_qp_base *base = &qp->trans_qp.base;
	struct ib_umem *umem = base->ubuffer.umem;
	struct mlx5_ib_wq *wq = &qp->rq;
	size_t bytes_copied;
	int ret;

	ret = mlx5_ib_read_user_wqe_common(umem, buffer, buflen, wqe_index,
					   wq->offset, wq->wqe_cnt,
					   wq->wqe_shift, buflen,
					   &bytes_copied);

	if (ret)
		return ret;
	*bc = bytes_copied;
	return 0;
}

int mlx5_ib_read_wqe_rq(struct mlx5_ib_qp *qp, int wqe_index, void *buffer,
			size_t buflen, size_t *bc)
{
	struct mlx5_ib_qp_base *base = &qp->trans_qp.base;
	struct ib_umem *umem = base->ubuffer.umem;
	struct mlx5_ib_wq *wq = &qp->rq;
	size_t wqe_size = 1 << wq->wqe_shift;

	if (buflen < wqe_size)
		return -EINVAL;

	if (!umem)
		return -EOPNOTSUPP;

	return mlx5_ib_read_user_wqe_rq(qp, wqe_index, buffer, buflen, bc);
}

static int mlx5_ib_read_user_wqe_srq(struct mlx5_ib_srq *srq, int wqe_index,
				     void *buffer, size_t buflen, size_t *bc)
{
	struct ib_umem *umem = srq->umem;
	size_t bytes_copied;
	int ret;

	ret = mlx5_ib_read_user_wqe_common(umem, buffer, buflen, wqe_index, 0,
					   srq->msrq.max, srq->msrq.wqe_shift,
					   buflen, &bytes_copied);

	if (ret)
		return ret;
	*bc = bytes_copied;
	return 0;
}

int mlx5_ib_read_wqe_srq(struct mlx5_ib_srq *srq, int wqe_index, void *buffer,
			 size_t buflen, size_t *bc)
{
	struct ib_umem *umem = srq->umem;
	size_t wqe_size = 1 << srq->msrq.wqe_shift;

	if (buflen < wqe_size)
		return -EINVAL;

	if (!umem)
		return -EOPNOTSUPP;

	return mlx5_ib_read_user_wqe_srq(srq, wqe_index, buffer, buflen, bc);
}

static void mlx5_ib_qp_err_syndrome(struct ib_qp *ibqp)
{
	struct mlx5_ib_dev *dev = to_mdev(ibqp->device);
	int outlen = MLX5_ST_SZ_BYTES(query_qp_out);
	struct mlx5_ib_qp *qp = to_mqp(ibqp);
	void *pas_ext_union, *err_syn;
	u32 *outb;
	int err;

	if (!MLX5_CAP_GEN(dev->mdev, qpc_extension) ||
	    !MLX5_CAP_GEN(dev->mdev, qp_error_syndrome))
		return;

	outb = kzalloc(outlen, GFP_KERNEL);
	if (!outb)
		return;

	err = mlx5_core_qp_query(dev, &qp->trans_qp.base.mqp, outb, outlen,
				 true);
	if (err)
		goto out;

	pas_ext_union =
		MLX5_ADDR_OF(query_qp_out, outb, qp_pas_or_qpc_ext_and_pas);
	err_syn = MLX5_ADDR_OF(qpc_extension_and_pas_list_in, pas_ext_union,
			       qpc_data_extension.error_syndrome);

	pr_err("%s/%d: QP %d error: %s (0x%x 0x%x 0x%x)\n",
	       ibqp->device->name, ibqp->port, ibqp->qp_num,
	       ib_wc_status_msg(
		       MLX5_GET(cqe_error_syndrome, err_syn, syndrome)),
	       MLX5_GET(cqe_error_syndrome, err_syn, vendor_error_syndrome),
	       MLX5_GET(cqe_error_syndrome, err_syn, hw_syndrome_type),
	       MLX5_GET(cqe_error_syndrome, err_syn, hw_error_syndrome));
out:
	kfree(outb);
}

static void mlx5_ib_handle_qp_event(struct work_struct *_work)
{
	struct mlx5_ib_qp_event_work *qpe_work =
		container_of(_work, struct mlx5_ib_qp_event_work, work);
	struct ib_qp *ibqp = &to_mibqp(qpe_work->qp)->ibqp;
	struct ib_event event = {};

	event.device = ibqp->device;
	event.element.qp = ibqp;
	switch (qpe_work->type) {
	case MLX5_EVENT_TYPE_PATH_MIG:
		event.event = IB_EVENT_PATH_MIG;
		break;
	case MLX5_EVENT_TYPE_COMM_EST:
		event.event = IB_EVENT_COMM_EST;
		break;
	case MLX5_EVENT_TYPE_SQ_DRAINED:
		event.event = IB_EVENT_SQ_DRAINED;
		break;
	case MLX5_EVENT_TYPE_SRQ_LAST_WQE:
		event.event = IB_EVENT_QP_LAST_WQE_REACHED;
		break;
	case MLX5_EVENT_TYPE_WQ_CATAS_ERROR:
		event.event = IB_EVENT_QP_FATAL;
		break;
	case MLX5_EVENT_TYPE_PATH_MIG_FAILED:
		event.event = IB_EVENT_PATH_MIG_ERR;
		break;
	case MLX5_EVENT_TYPE_WQ_INVAL_REQ_ERROR:
		event.event = IB_EVENT_QP_REQ_ERR;
		break;
	case MLX5_EVENT_TYPE_WQ_ACCESS_ERROR:
		event.event = IB_EVENT_QP_ACCESS_ERR;
		break;
	default:
		pr_warn("mlx5_ib: Unexpected event type %d on QP %06x\n",
			qpe_work->type, qpe_work->qp->qpn);
		goto out;
	}

	if ((event.event == IB_EVENT_QP_FATAL) ||
	    (event.event == IB_EVENT_QP_ACCESS_ERR))
		mlx5_ib_qp_err_syndrome(ibqp);

	ibqp->event_handler(&event, ibqp->qp_context);

out:
	mlx5_core_res_put(&qpe_work->qp->common);
	kfree(qpe_work);
}

static void mlx5_ib_qp_event(struct mlx5_core_qp *qp, int type)
{
	struct ib_qp *ibqp = &to_mibqp(qp)->ibqp;
	struct mlx5_ib_qp_event_work *qpe_work;

	if (type == MLX5_EVENT_TYPE_PATH_MIG) {
		/* This event is only valid for trans_qps */
		to_mibqp(qp)->port = to_mibqp(qp)->trans_qp.alt_port;
	}

	if (!ibqp->event_handler)
		goto out_no_handler;

	qpe_work = kzalloc(sizeof(*qpe_work), GFP_ATOMIC);
	if (!qpe_work)
		goto out_no_handler;

	qpe_work->qp = qp;
	qpe_work->type = type;
	INIT_WORK(&qpe_work->work, mlx5_ib_handle_qp_event);
	queue_work(mlx5_ib_qp_event_wq, &qpe_work->work);
	return;

out_no_handler:
	mlx5_core_res_put(&qp->common);
}

static int set_rq_size(struct mlx5_ib_dev *dev, struct ib_qp_cap *cap,
		       int has_rq, struct mlx5_ib_qp *qp, struct mlx5_ib_create_qp *ucmd)
{
	int wqe_size;
	int wq_size;

	/* Sanity check RQ size before proceeding */
	if (cap->max_recv_wr > (1 << MLX5_CAP_GEN(dev->mdev, log_max_qp_sz)))
		return -EINVAL;

	if (!has_rq) {
		qp->rq.max_gs = 0;
		qp->rq.wqe_cnt = 0;
		qp->rq.wqe_shift = 0;
		cap->max_recv_wr = 0;
		cap->max_recv_sge = 0;
	} else {
		int wq_sig = !!(qp->flags_en & MLX5_QP_FLAG_SIGNATURE);

		if (ucmd) {
			qp->rq.wqe_cnt = ucmd->rq_wqe_count;
			if (ucmd->rq_wqe_shift > BITS_PER_BYTE * sizeof(ucmd->rq_wqe_shift))
				return -EINVAL;
			qp->rq.wqe_shift = ucmd->rq_wqe_shift;
			if ((1 << qp->rq.wqe_shift) /
				    sizeof(struct mlx5_wqe_data_seg) <
			    wq_sig)
				return -EINVAL;
			qp->rq.max_gs =
				(1 << qp->rq.wqe_shift) /
					sizeof(struct mlx5_wqe_data_seg) -
				wq_sig;
			qp->rq.max_post = qp->rq.wqe_cnt;
		} else {
			wqe_size =
				wq_sig ? sizeof(struct mlx5_wqe_signature_seg) :
					 0;
			wqe_size += cap->max_recv_sge * sizeof(struct mlx5_wqe_data_seg);
			wqe_size = roundup_pow_of_two(wqe_size);
			wq_size = roundup_pow_of_two(cap->max_recv_wr) * wqe_size;
			wq_size = max_t(int, wq_size, MLX5_SEND_WQE_BB);
			qp->rq.wqe_cnt = wq_size / wqe_size;
			if (wqe_size > MLX5_CAP_GEN(dev->mdev, max_wqe_sz_rq)) {
				mlx5_ib_dbg(dev, "wqe_size %d, max %d\n",
					    wqe_size,
					    MLX5_CAP_GEN(dev->mdev,
							 max_wqe_sz_rq));
				return -EINVAL;
			}
			qp->rq.wqe_shift = ilog2(wqe_size);
			qp->rq.max_gs =
				(1 << qp->rq.wqe_shift) /
					sizeof(struct mlx5_wqe_data_seg) -
				wq_sig;
			qp->rq.max_post = qp->rq.wqe_cnt;
		}
	}

	return 0;
}

static int sq_overhead(struct ib_qp_init_attr *attr)
{
	int size = 0;

	switch (attr->qp_type) {
	case IB_QPT_XRC_INI:
		size += sizeof(struct mlx5_wqe_xrc_seg);
		fallthrough;
	case IB_QPT_RC:
		size += sizeof(struct mlx5_wqe_ctrl_seg) +
			max(sizeof(struct mlx5_wqe_atomic_seg) +
			    sizeof(struct mlx5_wqe_raddr_seg),
			    sizeof(struct mlx5_wqe_umr_ctrl_seg) +
			    sizeof(struct mlx5_mkey_seg) +
			    MLX5_IB_SQ_UMR_INLINE_THRESHOLD /
			    MLX5_IB_UMR_OCTOWORD);
		break;

	case IB_QPT_XRC_TGT:
		return 0;

	case IB_QPT_UC:
		size += sizeof(struct mlx5_wqe_ctrl_seg) +
			max(sizeof(struct mlx5_wqe_raddr_seg),
			    sizeof(struct mlx5_wqe_umr_ctrl_seg) +
			    sizeof(struct mlx5_mkey_seg));
		break;

	case IB_QPT_UD:
		if (attr->create_flags & IB_QP_CREATE_IPOIB_UD_LSO)
			size += sizeof(struct mlx5_wqe_eth_pad) +
				sizeof(struct mlx5_wqe_eth_seg);
		fallthrough;
	case IB_QPT_SMI:
	case MLX5_IB_QPT_HW_GSI:
		size += sizeof(struct mlx5_wqe_ctrl_seg) +
			sizeof(struct mlx5_wqe_datagram_seg);
		break;

	case MLX5_IB_QPT_REG_UMR:
		size += sizeof(struct mlx5_wqe_ctrl_seg) +
			sizeof(struct mlx5_wqe_umr_ctrl_seg) +
			sizeof(struct mlx5_mkey_seg);
		break;

	default:
		return -EINVAL;
	}

	return size;
}

static int calc_send_wqe(struct ib_qp_init_attr *attr)
{
	int inl_size = 0;
	int size;

	size = sq_overhead(attr);
	if (size < 0)
		return size;

	if (attr->cap.max_inline_data) {
		inl_size = size + sizeof(struct mlx5_wqe_inline_seg) +
			attr->cap.max_inline_data;
	}

	size += attr->cap.max_send_sge * sizeof(struct mlx5_wqe_data_seg);
	if (attr->create_flags & IB_QP_CREATE_INTEGRITY_EN &&
	    ALIGN(max_t(int, inl_size, size), MLX5_SEND_WQE_BB) < MLX5_SIG_WQE_SIZE)
		return MLX5_SIG_WQE_SIZE;
	else
		return ALIGN(max_t(int, inl_size, size), MLX5_SEND_WQE_BB);
}

static int get_send_sge(struct ib_qp_init_attr *attr, int wqe_size)
{
	int max_sge;

	if (attr->qp_type == IB_QPT_RC)
		max_sge = (min_t(int, wqe_size, 512) -
			   sizeof(struct mlx5_wqe_ctrl_seg) -
			   sizeof(struct mlx5_wqe_raddr_seg)) /
			sizeof(struct mlx5_wqe_data_seg);
	else if (attr->qp_type == IB_QPT_XRC_INI)
		max_sge = (min_t(int, wqe_size, 512) -
			   sizeof(struct mlx5_wqe_ctrl_seg) -
			   sizeof(struct mlx5_wqe_xrc_seg) -
			   sizeof(struct mlx5_wqe_raddr_seg)) /
			sizeof(struct mlx5_wqe_data_seg);
	else
		max_sge = (wqe_size - sq_overhead(attr)) /
			sizeof(struct mlx5_wqe_data_seg);

	return min_t(int, max_sge, wqe_size - sq_overhead(attr) /
		     sizeof(struct mlx5_wqe_data_seg));
}

static int calc_sq_size(struct mlx5_ib_dev *dev, struct ib_qp_init_attr *attr,
			struct mlx5_ib_qp *qp)
{
	int wqe_size;
	int wq_size;

	if (!attr->cap.max_send_wr)
		return 0;

	wqe_size = calc_send_wqe(attr);
	mlx5_ib_dbg(dev, "wqe_size %d\n", wqe_size);
	if (wqe_size < 0)
		return wqe_size;

	if (wqe_size > MLX5_CAP_GEN(dev->mdev, max_wqe_sz_sq)) {
		mlx5_ib_dbg(dev, "wqe_size(%d) > max_sq_desc_sz(%d)\n",
			    wqe_size, MLX5_CAP_GEN(dev->mdev, max_wqe_sz_sq));
		return -EINVAL;
	}

	qp->max_inline_data = wqe_size - sq_overhead(attr) -
			      sizeof(struct mlx5_wqe_inline_seg);
	attr->cap.max_inline_data = qp->max_inline_data;

	wq_size = roundup_pow_of_two(attr->cap.max_send_wr * wqe_size);
	qp->sq.wqe_cnt = wq_size / MLX5_SEND_WQE_BB;
	if (qp->sq.wqe_cnt > (1 << MLX5_CAP_GEN(dev->mdev, log_max_qp_sz))) {
		mlx5_ib_dbg(dev, "send queue size (%d * %d / %d -> %d) exceeds limits(%d)\n",
			    attr->cap.max_send_wr, wqe_size, MLX5_SEND_WQE_BB,
			    qp->sq.wqe_cnt,
			    1 << MLX5_CAP_GEN(dev->mdev, log_max_qp_sz));
		return -ENOMEM;
	}
	qp->sq.wqe_shift = ilog2(MLX5_SEND_WQE_BB);
	qp->sq.max_gs = get_send_sge(attr, wqe_size);
	if (qp->sq.max_gs < attr->cap.max_send_sge)
		return -ENOMEM;

	attr->cap.max_send_sge = qp->sq.max_gs;
	qp->sq.max_post = wq_size / wqe_size;
	attr->cap.max_send_wr = qp->sq.max_post;

	return wq_size;
}

static int set_user_buf_size(struct mlx5_ib_dev *dev,
			    struct mlx5_ib_qp *qp,
			    struct mlx5_ib_create_qp *ucmd,
			    struct mlx5_ib_qp_base *base,
			    struct ib_qp_init_attr *attr)
{
	int desc_sz = 1 << qp->sq.wqe_shift;

	if (desc_sz > MLX5_CAP_GEN(dev->mdev, max_wqe_sz_sq)) {
		mlx5_ib_warn(dev, "desc_sz %d, max_sq_desc_sz %d\n",
			     desc_sz, MLX5_CAP_GEN(dev->mdev, max_wqe_sz_sq));
		return -EINVAL;
	}

	if (ucmd->sq_wqe_count && !is_power_of_2(ucmd->sq_wqe_count)) {
		mlx5_ib_warn(dev, "sq_wqe_count %d is not a power of two\n",
			     ucmd->sq_wqe_count);
		return -EINVAL;
	}

	qp->sq.wqe_cnt = ucmd->sq_wqe_count;

	if (qp->sq.wqe_cnt > (1 << MLX5_CAP_GEN(dev->mdev, log_max_qp_sz))) {
		mlx5_ib_warn(dev, "wqe_cnt %d, max_wqes %d\n",
			     qp->sq.wqe_cnt,
			     1 << MLX5_CAP_GEN(dev->mdev, log_max_qp_sz));
		return -EINVAL;
	}

	if (attr->qp_type == IB_QPT_RAW_PACKET ||
	    qp->flags & IB_QP_CREATE_SOURCE_QPN) {
		base->ubuffer.buf_size = qp->rq.wqe_cnt << qp->rq.wqe_shift;
		qp->raw_packet_qp.sq.ubuffer.buf_size = qp->sq.wqe_cnt << 6;
	} else {
		base->ubuffer.buf_size = (qp->rq.wqe_cnt << qp->rq.wqe_shift) +
					 (qp->sq.wqe_cnt << 6);
	}

	return 0;
}

static int qp_has_rq(struct ib_qp_init_attr *attr)
{
	if (attr->qp_type == IB_QPT_XRC_INI ||
	    attr->qp_type == IB_QPT_XRC_TGT || attr->srq ||
	    attr->qp_type == MLX5_IB_QPT_REG_UMR ||
	    !attr->cap.max_recv_wr)
		return 0;

	return 1;
}

enum {
	/* this is the first blue flame register in the array of bfregs assigned
	 * to a processes. Since we do not use it for blue flame but rather
	 * regular 64 bit doorbells, we do not need a lock for maintaiing
	 * "odd/even" order
	 */
	NUM_NON_BLUE_FLAME_BFREGS = 1,
};

static int max_bfregs(struct mlx5_ib_dev *dev, struct mlx5_bfreg_info *bfregi)
{
	return get_uars_per_sys_page(dev, bfregi->lib_uar_4k) *
	       bfregi->num_static_sys_pages * MLX5_NON_FP_BFREGS_PER_UAR;
}

static int num_med_bfreg(struct mlx5_ib_dev *dev,
			 struct mlx5_bfreg_info *bfregi)
{
	int n;

	n = max_bfregs(dev, bfregi) - bfregi->num_low_latency_bfregs -
	    NUM_NON_BLUE_FLAME_BFREGS;

	return n >= 0 ? n : 0;
}

static int first_med_bfreg(struct mlx5_ib_dev *dev,
			   struct mlx5_bfreg_info *bfregi)
{
	return num_med_bfreg(dev, bfregi) ? 1 : -ENOMEM;
}

static int first_hi_bfreg(struct mlx5_ib_dev *dev,
			  struct mlx5_bfreg_info *bfregi)
{
	int med;

	med = num_med_bfreg(dev, bfregi);
	return ++med;
}

static int alloc_high_class_bfreg(struct mlx5_ib_dev *dev,
				  struct mlx5_bfreg_info *bfregi)
{
	int i;

	for (i = first_hi_bfreg(dev, bfregi); i < max_bfregs(dev, bfregi); i++) {
		if (!bfregi->count[i]) {
			bfregi->count[i]++;
			return i;
		}
	}

	return -ENOMEM;
}

static int alloc_med_class_bfreg(struct mlx5_ib_dev *dev,
				 struct mlx5_bfreg_info *bfregi)
{
	int minidx = first_med_bfreg(dev, bfregi);
	int i;

	if (minidx < 0)
		return minidx;

	for (i = minidx; i < first_hi_bfreg(dev, bfregi); i++) {
		if (bfregi->count[i] < bfregi->count[minidx])
			minidx = i;
		if (!bfregi->count[minidx])
			break;
	}

	bfregi->count[minidx]++;
	return minidx;
}

static int alloc_bfreg(struct mlx5_ib_dev *dev,
		       struct mlx5_bfreg_info *bfregi)
{
	int bfregn = -ENOMEM;

	if (bfregi->lib_uar_dyn)
		return -EINVAL;

	mutex_lock(&bfregi->lock);
	if (bfregi->ver >= 2) {
		bfregn = alloc_high_class_bfreg(dev, bfregi);
		if (bfregn < 0)
			bfregn = alloc_med_class_bfreg(dev, bfregi);
	}

	if (bfregn < 0) {
		BUILD_BUG_ON(NUM_NON_BLUE_FLAME_BFREGS != 1);
		bfregn = 0;
		bfregi->count[bfregn]++;
	}
	mutex_unlock(&bfregi->lock);

	return bfregn;
}

void mlx5_ib_free_bfreg(struct mlx5_ib_dev *dev, struct mlx5_bfreg_info *bfregi, int bfregn)
{
	mutex_lock(&bfregi->lock);
	bfregi->count[bfregn]--;
	mutex_unlock(&bfregi->lock);
}

static enum mlx5_qp_state to_mlx5_state(enum ib_qp_state state)
{
	switch (state) {
	case IB_QPS_RESET:	return MLX5_QP_STATE_RST;
	case IB_QPS_INIT:	return MLX5_QP_STATE_INIT;
	case IB_QPS_RTR:	return MLX5_QP_STATE_RTR;
	case IB_QPS_RTS:	return MLX5_QP_STATE_RTS;
	case IB_QPS_SQD:	return MLX5_QP_STATE_SQD;
	case IB_QPS_SQE:	return MLX5_QP_STATE_SQER;
	case IB_QPS_ERR:	return MLX5_QP_STATE_ERR;
	default:		return -1;
	}
}

static int to_mlx5_st(enum ib_qp_type type)
{
	switch (type) {
	case IB_QPT_RC:			return MLX5_QP_ST_RC;
	case IB_QPT_UC:			return MLX5_QP_ST_UC;
	case IB_QPT_UD:			return MLX5_QP_ST_UD;
	case MLX5_IB_QPT_REG_UMR:	return MLX5_QP_ST_REG_UMR;
	case IB_QPT_XRC_INI:
	case IB_QPT_XRC_TGT:		return MLX5_QP_ST_XRC;
	case IB_QPT_SMI:		return MLX5_QP_ST_QP0;
	case MLX5_IB_QPT_HW_GSI:	return MLX5_QP_ST_QP1;
	case MLX5_IB_QPT_DCI:		return MLX5_QP_ST_DCI;
	case IB_QPT_RAW_PACKET:		return MLX5_QP_ST_RAW_ETHERTYPE;
	default:		return -EINVAL;
	}
}

static void mlx5_ib_lock_cqs(struct mlx5_ib_cq *send_cq,
			     struct mlx5_ib_cq *recv_cq);
static void mlx5_ib_unlock_cqs(struct mlx5_ib_cq *send_cq,
			       struct mlx5_ib_cq *recv_cq);

int bfregn_to_uar_index(struct mlx5_ib_dev *dev,
			struct mlx5_bfreg_info *bfregi, u32 bfregn,
			bool dyn_bfreg)
{
	unsigned int bfregs_per_sys_page;
	u32 index_of_sys_page;
	u32 offset;

	if (bfregi->lib_uar_dyn)
		return -EINVAL;

	bfregs_per_sys_page = get_uars_per_sys_page(dev, bfregi->lib_uar_4k) *
				MLX5_NON_FP_BFREGS_PER_UAR;
	index_of_sys_page = bfregn / bfregs_per_sys_page;

	if (dyn_bfreg) {
		index_of_sys_page += bfregi->num_static_sys_pages;

		if (index_of_sys_page >= bfregi->num_sys_pages)
			return -EINVAL;

		if (bfregn > bfregi->num_dyn_bfregs ||
		    bfregi->sys_pages[index_of_sys_page] == MLX5_IB_INVALID_UAR_INDEX) {
			mlx5_ib_dbg(dev, "Invalid dynamic uar index\n");
			return -EINVAL;
		}
	}

	offset = bfregn % bfregs_per_sys_page / MLX5_NON_FP_BFREGS_PER_UAR;
	return bfregi->sys_pages[index_of_sys_page] + offset;
}

static void destroy_user_rq(struct mlx5_ib_dev *dev, struct ib_pd *pd,
			    struct mlx5_ib_rwq *rwq, struct ib_udata *udata)
{
	struct mlx5_ib_ucontext *context =
		rdma_udata_to_drv_context(
			udata,
			struct mlx5_ib_ucontext,
			ibucontext);

	if (rwq->create_flags & MLX5_IB_WQ_FLAGS_DELAY_DROP)
		atomic_dec(&dev->delay_drop.rqs_cnt);

	mlx5_ib_db_unmap_user(context, &rwq->db);
	ib_umem_release(rwq->umem);
}

static int create_user_rq(struct mlx5_ib_dev *dev, struct ib_pd *pd,
			  struct ib_udata *udata, struct mlx5_ib_rwq *rwq,
			  struct mlx5_ib_create_wq *ucmd)
{
	struct mlx5_ib_ucontext *ucontext = rdma_udata_to_drv_context(
		udata, struct mlx5_ib_ucontext, ibucontext);
	unsigned long page_size = 0;
	u32 offset = 0;
	int err;

	if (!ucmd->buf_addr)
		return -EINVAL;

	rwq->umem = ib_umem_get(&dev->ib_dev, ucmd->buf_addr, rwq->buf_size, 0);
	if (IS_ERR(rwq->umem)) {
		mlx5_ib_dbg(dev, "umem_get failed\n");
		err = PTR_ERR(rwq->umem);
		return err;
	}

	page_size = mlx5_umem_find_best_quantized_pgoff(
		rwq->umem, wq, log_wq_pg_sz, MLX5_ADAPTER_PAGE_SHIFT,
		page_offset, 64, &rwq->rq_page_offset);
	if (!page_size) {
		mlx5_ib_warn(dev, "bad offset\n");
		err = -EINVAL;
		goto err_umem;
	}

	rwq->rq_num_pas = ib_umem_num_dma_blocks(rwq->umem, page_size);
	rwq->page_shift = order_base_2(page_size);
	rwq->log_page_size =  rwq->page_shift - MLX5_ADAPTER_PAGE_SHIFT;
	rwq->wq_sig = !!(ucmd->flags & MLX5_WQ_FLAG_SIGNATURE);

	mlx5_ib_dbg(
		dev,
		"addr 0x%llx, size %zd, npages %zu, page_size %ld, ncont %d, offset %d\n",
		(unsigned long long)ucmd->buf_addr, rwq->buf_size,
		ib_umem_num_pages(rwq->umem), page_size, rwq->rq_num_pas,
		offset);

	err = mlx5_ib_db_map_user(ucontext, ucmd->db_addr, &rwq->db);
	if (err) {
		mlx5_ib_dbg(dev, "map failed\n");
		goto err_umem;
	}

	return 0;

err_umem:
	ib_umem_release(rwq->umem);
	return err;
}

static int adjust_bfregn(struct mlx5_ib_dev *dev,
			 struct mlx5_bfreg_info *bfregi, int bfregn)
{
	return bfregn / MLX5_NON_FP_BFREGS_PER_UAR * MLX5_BFREGS_PER_UAR +
				bfregn % MLX5_NON_FP_BFREGS_PER_UAR;
}

static int _create_user_qp(struct mlx5_ib_dev *dev, struct ib_pd *pd,
			   struct mlx5_ib_qp *qp, struct ib_udata *udata,
			   struct ib_qp_init_attr *attr, u32 **in,
			   struct mlx5_ib_create_qp_resp *resp, int *inlen,
			   struct mlx5_ib_qp_base *base,
			   struct mlx5_ib_create_qp *ucmd)
{
	struct mlx5_ib_ucontext *context;
	struct mlx5_ib_ubuffer *ubuffer = &base->ubuffer;
	unsigned int page_offset_quantized = 0;
	unsigned long page_size = 0;
	int uar_index = 0;
	int bfregn;
	int ncont = 0;
	__be64 *pas;
	void *qpc;
	int err;
	u16 uid;
	u32 uar_flags;

	context = rdma_udata_to_drv_context(udata, struct mlx5_ib_ucontext,
					    ibucontext);
	uar_flags = qp->flags_en &
		    (MLX5_QP_FLAG_UAR_PAGE_INDEX | MLX5_QP_FLAG_BFREG_INDEX);
	switch (uar_flags) {
	case MLX5_QP_FLAG_UAR_PAGE_INDEX:
		uar_index = ucmd->bfreg_index;
		bfregn = MLX5_IB_INVALID_BFREG;
		break;
	case MLX5_QP_FLAG_BFREG_INDEX:
		uar_index = bfregn_to_uar_index(dev, &context->bfregi,
						ucmd->bfreg_index, true);
		if (uar_index < 0)
			return uar_index;
		bfregn = MLX5_IB_INVALID_BFREG;
		break;
	case 0:
		if (qp->flags & IB_QP_CREATE_CROSS_CHANNEL)
			return -EINVAL;
		bfregn = alloc_bfreg(dev, &context->bfregi);
		if (bfregn < 0)
			return bfregn;
		break;
	default:
		return -EINVAL;
	}

	mlx5_ib_dbg(dev, "bfregn 0x%x, uar_index 0x%x\n", bfregn, uar_index);
	if (bfregn != MLX5_IB_INVALID_BFREG)
		uar_index = bfregn_to_uar_index(dev, &context->bfregi, bfregn,
						false);

	qp->rq.offset = 0;
	qp->sq.wqe_shift = ilog2(MLX5_SEND_WQE_BB);
	qp->sq.offset = qp->rq.wqe_cnt << qp->rq.wqe_shift;

	err = set_user_buf_size(dev, qp, ucmd, base, attr);
	if (err)
		goto err_bfreg;

	if (ucmd->buf_addr && ubuffer->buf_size) {
		ubuffer->buf_addr = ucmd->buf_addr;
		ubuffer->umem = ib_umem_get(&dev->ib_dev, ubuffer->buf_addr,
					    ubuffer->buf_size, 0);
		if (IS_ERR(ubuffer->umem)) {
			err = PTR_ERR(ubuffer->umem);
			goto err_bfreg;
		}
		page_size = mlx5_umem_find_best_quantized_pgoff(
			ubuffer->umem, qpc, log_page_size,
			MLX5_ADAPTER_PAGE_SHIFT, page_offset, 64,
			&page_offset_quantized);
		if (!page_size) {
			err = -EINVAL;
			goto err_umem;
		}
		ncont = ib_umem_num_dma_blocks(ubuffer->umem, page_size);
	} else {
		ubuffer->umem = NULL;
	}

	*inlen = MLX5_ST_SZ_BYTES(create_qp_in) +
		 MLX5_FLD_SZ_BYTES(create_qp_in, pas[0]) * ncont;
	*in = kvzalloc(*inlen, GFP_KERNEL);
	if (!*in) {
		err = -ENOMEM;
		goto err_umem;
	}

	uid = (attr->qp_type != IB_QPT_XRC_INI) ? to_mpd(pd)->uid : 0;
	MLX5_SET(create_qp_in, *in, uid, uid);
	qpc = MLX5_ADDR_OF(create_qp_in, *in, qpc);
	pas = (__be64 *)MLX5_ADDR_OF(create_qp_in, *in, pas);
	if (ubuffer->umem) {
		mlx5_ib_populate_pas(ubuffer->umem, page_size, pas, 0);
		MLX5_SET(qpc, qpc, log_page_size,
			 order_base_2(page_size) - MLX5_ADAPTER_PAGE_SHIFT);
		MLX5_SET(qpc, qpc, page_offset, page_offset_quantized);
	}
	MLX5_SET(qpc, qpc, uar_page, uar_index);
	if (bfregn != MLX5_IB_INVALID_BFREG)
		resp->bfreg_index = adjust_bfregn(dev, &context->bfregi, bfregn);
	else
		resp->bfreg_index = MLX5_IB_INVALID_BFREG;
	qp->bfregn = bfregn;

	err = mlx5_ib_db_map_user(context, ucmd->db_addr, &qp->db);
	if (err) {
		mlx5_ib_dbg(dev, "map failed\n");
		goto err_free;
	}

	return 0;

err_free:
	kvfree(*in);

err_umem:
	ib_umem_release(ubuffer->umem);

err_bfreg:
	if (bfregn != MLX5_IB_INVALID_BFREG)
		mlx5_ib_free_bfreg(dev, &context->bfregi, bfregn);
	return err;
}

static void destroy_qp(struct mlx5_ib_dev *dev, struct mlx5_ib_qp *qp,
		       struct mlx5_ib_qp_base *base, struct ib_udata *udata)
{
	struct mlx5_ib_ucontext *context = rdma_udata_to_drv_context(
		udata, struct mlx5_ib_ucontext, ibucontext);

	if (udata) {
		/* User QP */
		mlx5_ib_db_unmap_user(context, &qp->db);
		ib_umem_release(base->ubuffer.umem);

		/*
		 * Free only the BFREGs which are handled by the kernel.
		 * BFREGs of UARs allocated dynamically are handled by user.
		 */
		if (qp->bfregn != MLX5_IB_INVALID_BFREG)
			mlx5_ib_free_bfreg(dev, &context->bfregi, qp->bfregn);
		return;
	}

	/* Kernel QP */
	kvfree(qp->sq.wqe_head);
	kvfree(qp->sq.w_list);
	kvfree(qp->sq.wrid);
	kvfree(qp->sq.wr_data);
	kvfree(qp->rq.wrid);
	if (qp->db.db)
		mlx5_db_free(dev->mdev, &qp->db);
	if (qp->buf.frags)
		mlx5_frag_buf_free(dev->mdev, &qp->buf);
}

static int _create_kernel_qp(struct mlx5_ib_dev *dev,
			     struct ib_qp_init_attr *init_attr,
			     struct mlx5_ib_qp *qp, u32 **in, int *inlen,
			     struct mlx5_ib_qp_base *base)
{
	int uar_index;
	void *qpc;
	int err;

	if (init_attr->qp_type == MLX5_IB_QPT_REG_UMR)
		qp->bf.bfreg = &dev->fp_bfreg;
	else if (qp->flags & MLX5_IB_QP_CREATE_WC_TEST)
		qp->bf.bfreg = &dev->wc_bfreg;
	else
		qp->bf.bfreg = &dev->bfreg;

	/* We need to divide by two since each register is comprised of
	 * two buffers of identical size, namely odd and even
	 */
	qp->bf.buf_size = (1 << MLX5_CAP_GEN(dev->mdev, log_bf_reg_size)) / 2;
	uar_index = qp->bf.bfreg->index;

	err = calc_sq_size(dev, init_attr, qp);
	if (err < 0) {
		mlx5_ib_dbg(dev, "err %d\n", err);
		return err;
	}

	qp->rq.offset = 0;
	qp->sq.offset = qp->rq.wqe_cnt << qp->rq.wqe_shift;
	base->ubuffer.buf_size = err + (qp->rq.wqe_cnt << qp->rq.wqe_shift);

	err = mlx5_frag_buf_alloc_node(dev->mdev, base->ubuffer.buf_size,
				       &qp->buf, dev->mdev->priv.numa_node);
	if (err) {
		mlx5_ib_dbg(dev, "err %d\n", err);
		return err;
	}

	if (qp->rq.wqe_cnt)
		mlx5_init_fbc(qp->buf.frags, qp->rq.wqe_shift,
			      ilog2(qp->rq.wqe_cnt), &qp->rq.fbc);

	if (qp->sq.wqe_cnt) {
		int sq_strides_offset = (qp->sq.offset  & (PAGE_SIZE - 1)) /
					MLX5_SEND_WQE_BB;
		mlx5_init_fbc_offset(qp->buf.frags +
				     (qp->sq.offset / PAGE_SIZE),
				     ilog2(MLX5_SEND_WQE_BB),
				     ilog2(qp->sq.wqe_cnt),
				     sq_strides_offset, &qp->sq.fbc);

		qp->sq.cur_edge = get_sq_edge(&qp->sq, 0);
	}

	*inlen = MLX5_ST_SZ_BYTES(create_qp_in) +
		 MLX5_FLD_SZ_BYTES(create_qp_in, pas[0]) * qp->buf.npages;
	*in = kvzalloc(*inlen, GFP_KERNEL);
	if (!*in) {
		err = -ENOMEM;
		goto err_buf;
	}

	qpc = MLX5_ADDR_OF(create_qp_in, *in, qpc);
	MLX5_SET(qpc, qpc, uar_page, uar_index);
	MLX5_SET(qpc, qpc, ts_format, mlx5_get_qp_default_ts(dev->mdev));
	MLX5_SET(qpc, qpc, log_page_size, qp->buf.page_shift - MLX5_ADAPTER_PAGE_SHIFT);

	/* Set "fast registration enabled" for all kernel QPs */
	MLX5_SET(qpc, qpc, fre, 1);
	MLX5_SET(qpc, qpc, rlky, 1);

	if (qp->flags & MLX5_IB_QP_CREATE_SQPN_QP1)
		MLX5_SET(qpc, qpc, deth_sqpn, 1);

	mlx5_fill_page_frag_array(&qp->buf,
				  (__be64 *)MLX5_ADDR_OF(create_qp_in,
							 *in, pas));

	err = mlx5_db_alloc(dev->mdev, &qp->db);
	if (err) {
		mlx5_ib_dbg(dev, "err %d\n", err);
		goto err_free;
	}

	qp->sq.wrid = kvmalloc_array(qp->sq.wqe_cnt,
				     sizeof(*qp->sq.wrid), GFP_KERNEL);
	qp->sq.wr_data = kvmalloc_array(qp->sq.wqe_cnt,
					sizeof(*qp->sq.wr_data), GFP_KERNEL);
	qp->rq.wrid = kvmalloc_array(qp->rq.wqe_cnt,
				     sizeof(*qp->rq.wrid), GFP_KERNEL);
	qp->sq.w_list = kvmalloc_array(qp->sq.wqe_cnt,
				       sizeof(*qp->sq.w_list), GFP_KERNEL);
	qp->sq.wqe_head = kvmalloc_array(qp->sq.wqe_cnt,
					 sizeof(*qp->sq.wqe_head), GFP_KERNEL);

	if (!qp->sq.wrid || !qp->sq.wr_data || !qp->rq.wrid ||
	    !qp->sq.w_list || !qp->sq.wqe_head) {
		err = -ENOMEM;
		goto err_wrid;
	}

	return 0;

err_wrid:
	kvfree(qp->sq.wqe_head);
	kvfree(qp->sq.w_list);
	kvfree(qp->sq.wrid);
	kvfree(qp->sq.wr_data);
	kvfree(qp->rq.wrid);
	mlx5_db_free(dev->mdev, &qp->db);

err_free:
	kvfree(*in);

err_buf:
	mlx5_frag_buf_free(dev->mdev, &qp->buf);
	return err;
}

static u32 get_rx_type(struct mlx5_ib_qp *qp, struct ib_qp_init_attr *attr)
{
	if (attr->srq || (qp->type == IB_QPT_XRC_TGT) ||
	    (qp->type == MLX5_IB_QPT_DCI) || (qp->type == IB_QPT_XRC_INI))
		return MLX5_SRQ_RQ;
	else if (!qp->has_rq)
		return MLX5_ZERO_LEN_RQ;

	return MLX5_NON_ZERO_RQ;
}

static int create_raw_packet_qp_tis(struct mlx5_ib_dev *dev,
				    struct mlx5_ib_qp *qp,
				    struct mlx5_ib_sq *sq, u32 tdn,
				    struct ib_pd *pd)
{
	u32 in[MLX5_ST_SZ_DW(create_tis_in)] = {};
	void *tisc = MLX5_ADDR_OF(create_tis_in, in, ctx);

	MLX5_SET(create_tis_in, in, uid, to_mpd(pd)->uid);
	MLX5_SET(tisc, tisc, transport_domain, tdn);
	if (!mlx5_ib_lag_should_assign_affinity(dev) &&
	    mlx5_lag_is_lacp_owner(dev->mdev))
		MLX5_SET(tisc, tisc, strict_lag_tx_port_affinity, 1);
	if (qp->flags & IB_QP_CREATE_SOURCE_QPN)
		MLX5_SET(tisc, tisc, underlay_qpn, qp->underlay_qpn);

	return mlx5_core_create_tis(dev->mdev, in, &sq->tisn);
}

static void destroy_raw_packet_qp_tis(struct mlx5_ib_dev *dev,
				      struct mlx5_ib_sq *sq, struct ib_pd *pd)
{
	mlx5_cmd_destroy_tis(dev->mdev, sq->tisn, to_mpd(pd)->uid);
}

static void destroy_flow_rule_vport_sq(struct mlx5_ib_sq *sq)
{
	if (sq->flow_rule)
		mlx5_del_flow_rules(sq->flow_rule);
	sq->flow_rule = NULL;
}

static bool fr_supported(int ts_cap)
{
	return ts_cap == MLX5_TIMESTAMP_FORMAT_CAP_FREE_RUNNING ||
	       ts_cap == MLX5_TIMESTAMP_FORMAT_CAP_FREE_RUNNING_AND_REAL_TIME;
}

static int get_ts_format(struct mlx5_ib_dev *dev, struct mlx5_ib_cq *cq,
			 bool fr_sup, bool rt_sup)
{
	if (cq->private_flags & MLX5_IB_CQ_PR_FLAGS_REAL_TIME_TS) {
		if (!rt_sup) {
			mlx5_ib_dbg(dev,
				    "Real time TS format is not supported\n");
			return -EOPNOTSUPP;
		}
		return MLX5_TIMESTAMP_FORMAT_REAL_TIME;
	}
	if (cq->create_flags & IB_UVERBS_CQ_FLAGS_TIMESTAMP_COMPLETION) {
		if (!fr_sup) {
			mlx5_ib_dbg(dev,
				    "Free running TS format is not supported\n");
			return -EOPNOTSUPP;
		}
		return MLX5_TIMESTAMP_FORMAT_FREE_RUNNING;
	}
	return fr_sup ? MLX5_TIMESTAMP_FORMAT_FREE_RUNNING :
			MLX5_TIMESTAMP_FORMAT_DEFAULT;
}

static int get_rq_ts_format(struct mlx5_ib_dev *dev, struct mlx5_ib_cq *recv_cq)
{
	u8 ts_cap = MLX5_CAP_GEN(dev->mdev, rq_ts_format);

	return get_ts_format(dev, recv_cq, fr_supported(ts_cap),
			     rt_supported(ts_cap));
}

static int get_sq_ts_format(struct mlx5_ib_dev *dev, struct mlx5_ib_cq *send_cq)
{
	u8 ts_cap = MLX5_CAP_GEN(dev->mdev, sq_ts_format);

	return get_ts_format(dev, send_cq, fr_supported(ts_cap),
			     rt_supported(ts_cap));
}

static int get_qp_ts_format(struct mlx5_ib_dev *dev, struct mlx5_ib_cq *send_cq,
			    struct mlx5_ib_cq *recv_cq)
{
	u8 ts_cap = MLX5_CAP_ROCE(dev->mdev, qp_ts_format);
	bool fr_sup = fr_supported(ts_cap);
	bool rt_sup = rt_supported(ts_cap);
	u8 default_ts = fr_sup ? MLX5_TIMESTAMP_FORMAT_FREE_RUNNING :
				 MLX5_TIMESTAMP_FORMAT_DEFAULT;
	int send_ts_format =
		send_cq ? get_ts_format(dev, send_cq, fr_sup, rt_sup) :
			  default_ts;
	int recv_ts_format =
		recv_cq ? get_ts_format(dev, recv_cq, fr_sup, rt_sup) :
			  default_ts;

	if (send_ts_format < 0 || recv_ts_format < 0)
		return -EOPNOTSUPP;

	if (send_ts_format != MLX5_TIMESTAMP_FORMAT_DEFAULT &&
	    recv_ts_format != MLX5_TIMESTAMP_FORMAT_DEFAULT &&
	    send_ts_format != recv_ts_format) {
		mlx5_ib_dbg(
			dev,
			"The send ts_format does not match the receive ts_format\n");
		return -EOPNOTSUPP;
	}

	return send_ts_format == default_ts ? recv_ts_format : send_ts_format;
}

static int create_raw_packet_qp_sq(struct mlx5_ib_dev *dev,
				   struct ib_udata *udata,
				   struct mlx5_ib_sq *sq, void *qpin,
				   struct ib_pd *pd, struct mlx5_ib_cq *cq)
{
	struct mlx5_ib_ubuffer *ubuffer = &sq->ubuffer;
	__be64 *pas;
	void *in;
	void *sqc;
	void *qpc = MLX5_ADDR_OF(create_qp_in, qpin, qpc);
	void *wq;
	int inlen;
	int err;
	unsigned int page_offset_quantized;
	unsigned long page_size;
	int ts_format;

	ts_format = get_sq_ts_format(dev, cq);
	if (ts_format < 0)
		return ts_format;

	sq->ubuffer.umem = ib_umem_get(&dev->ib_dev, ubuffer->buf_addr,
				       ubuffer->buf_size, 0);
	if (IS_ERR(sq->ubuffer.umem))
		return PTR_ERR(sq->ubuffer.umem);
	page_size = mlx5_umem_find_best_quantized_pgoff(
		ubuffer->umem, wq, log_wq_pg_sz, MLX5_ADAPTER_PAGE_SHIFT,
		page_offset, 64, &page_offset_quantized);
	if (!page_size) {
		err = -EINVAL;
		goto err_umem;
	}

	inlen = MLX5_ST_SZ_BYTES(create_sq_in) +
		sizeof(u64) *
			ib_umem_num_dma_blocks(sq->ubuffer.umem, page_size);
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in) {
		err = -ENOMEM;
		goto err_umem;
	}

	MLX5_SET(create_sq_in, in, uid, to_mpd(pd)->uid);
	sqc = MLX5_ADDR_OF(create_sq_in, in, ctx);
	MLX5_SET(sqc, sqc, flush_in_error_en, 1);
	if (MLX5_CAP_ETH(dev->mdev, multi_pkt_send_wqe))
		MLX5_SET(sqc, sqc, allow_multi_pkt_send_wqe, 1);
	MLX5_SET(sqc, sqc, state, MLX5_SQC_STATE_RST);
	MLX5_SET(sqc, sqc, ts_format, ts_format);
	MLX5_SET(sqc, sqc, user_index, MLX5_GET(qpc, qpc, user_index));
	MLX5_SET(sqc, sqc, cqn, MLX5_GET(qpc, qpc, cqn_snd));
	MLX5_SET(sqc, sqc, tis_lst_sz, 1);
	MLX5_SET(sqc, sqc, tis_num_0, sq->tisn);
	if (MLX5_CAP_GEN(dev->mdev, eth_net_offloads) &&
	    MLX5_CAP_ETH(dev->mdev, swp))
		MLX5_SET(sqc, sqc, allow_swp, 1);

	wq = MLX5_ADDR_OF(sqc, sqc, wq);
	MLX5_SET(wq, wq, wq_type, MLX5_WQ_TYPE_CYCLIC);
	MLX5_SET(wq, wq, pd, MLX5_GET(qpc, qpc, pd));
	MLX5_SET(wq, wq, uar_page, MLX5_GET(qpc, qpc, uar_page));
	MLX5_SET64(wq, wq, dbr_addr, MLX5_GET64(qpc, qpc, dbr_addr));
	MLX5_SET(wq, wq, log_wq_stride, ilog2(MLX5_SEND_WQE_BB));
	MLX5_SET(wq, wq, log_wq_sz, MLX5_GET(qpc, qpc, log_sq_size));
	MLX5_SET(wq, wq, log_wq_pg_sz,
		 order_base_2(page_size) - MLX5_ADAPTER_PAGE_SHIFT);
	MLX5_SET(wq, wq, page_offset, page_offset_quantized);

	pas = (__be64 *)MLX5_ADDR_OF(wq, wq, pas);
	mlx5_ib_populate_pas(sq->ubuffer.umem, page_size, pas, 0);

	err = mlx5_core_create_sq_tracked(dev, in, inlen, &sq->base.mqp);

	kvfree(in);

	if (err)
		goto err_umem;

	return 0;

err_umem:
	ib_umem_release(sq->ubuffer.umem);
	sq->ubuffer.umem = NULL;

	return err;
}

static void destroy_raw_packet_qp_sq(struct mlx5_ib_dev *dev,
				     struct mlx5_ib_sq *sq)
{
	destroy_flow_rule_vport_sq(sq);
	mlx5_core_destroy_sq_tracked(dev, &sq->base.mqp);
	ib_umem_release(sq->ubuffer.umem);
}

static int create_raw_packet_qp_rq(struct mlx5_ib_dev *dev,
				   struct mlx5_ib_rq *rq, void *qpin,
				   struct ib_pd *pd, struct mlx5_ib_cq *cq)
{
	struct mlx5_ib_qp *mqp = rq->base.container_mibqp;
	__be64 *pas;
	void *in;
	void *rqc;
	void *wq;
	void *qpc = MLX5_ADDR_OF(create_qp_in, qpin, qpc);
	struct ib_umem *umem = rq->base.ubuffer.umem;
	unsigned int page_offset_quantized;
	unsigned long page_size = 0;
	int ts_format;
	size_t inlen;
	int err;

	ts_format = get_rq_ts_format(dev, cq);
	if (ts_format < 0)
		return ts_format;

	page_size = mlx5_umem_find_best_quantized_pgoff(umem, wq, log_wq_pg_sz,
							MLX5_ADAPTER_PAGE_SHIFT,
							page_offset, 64,
							&page_offset_quantized);
	if (!page_size)
		return -EINVAL;

	inlen = MLX5_ST_SZ_BYTES(create_rq_in) +
		sizeof(u64) * ib_umem_num_dma_blocks(umem, page_size);
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	MLX5_SET(create_rq_in, in, uid, to_mpd(pd)->uid);
	rqc = MLX5_ADDR_OF(create_rq_in, in, ctx);
	if (!(rq->flags & MLX5_IB_RQ_CVLAN_STRIPPING))
		MLX5_SET(rqc, rqc, vsd, 1);
	MLX5_SET(rqc, rqc, mem_rq_type, MLX5_RQC_MEM_RQ_TYPE_MEMORY_RQ_INLINE);
	MLX5_SET(rqc, rqc, state, MLX5_RQC_STATE_RST);
	MLX5_SET(rqc, rqc, ts_format, ts_format);
	MLX5_SET(rqc, rqc, flush_in_error_en, 1);
	MLX5_SET(rqc, rqc, user_index, MLX5_GET(qpc, qpc, user_index));
	MLX5_SET(rqc, rqc, cqn, MLX5_GET(qpc, qpc, cqn_rcv));

	if (mqp->flags & IB_QP_CREATE_SCATTER_FCS)
		MLX5_SET(rqc, rqc, scatter_fcs, 1);

	wq = MLX5_ADDR_OF(rqc, rqc, wq);
	MLX5_SET(wq, wq, wq_type, MLX5_WQ_TYPE_CYCLIC);
	if (rq->flags & MLX5_IB_RQ_PCI_WRITE_END_PADDING)
		MLX5_SET(wq, wq, end_padding_mode, MLX5_WQ_END_PAD_MODE_ALIGN);
	MLX5_SET(wq, wq, page_offset, page_offset_quantized);
	MLX5_SET(wq, wq, pd, MLX5_GET(qpc, qpc, pd));
	MLX5_SET64(wq, wq, dbr_addr, MLX5_GET64(qpc, qpc, dbr_addr));
	MLX5_SET(wq, wq, log_wq_stride, MLX5_GET(qpc, qpc, log_rq_stride) + 4);
	MLX5_SET(wq, wq, log_wq_pg_sz,
		 order_base_2(page_size) - MLX5_ADAPTER_PAGE_SHIFT);
	MLX5_SET(wq, wq, log_wq_sz, MLX5_GET(qpc, qpc, log_rq_size));

	pas = (__be64 *)MLX5_ADDR_OF(wq, wq, pas);
	mlx5_ib_populate_pas(umem, page_size, pas, 0);

	err = mlx5_core_create_rq_tracked(dev, in, inlen, &rq->base.mqp);

	kvfree(in);

	return err;
}

static void destroy_raw_packet_qp_rq(struct mlx5_ib_dev *dev,
				     struct mlx5_ib_rq *rq)
{
	mlx5_core_destroy_rq_tracked(dev, &rq->base.mqp);
}

static void destroy_raw_packet_qp_tir(struct mlx5_ib_dev *dev,
				      struct mlx5_ib_rq *rq,
				      u32 qp_flags_en,
				      struct ib_pd *pd)
{
	if (qp_flags_en & (MLX5_QP_FLAG_TIR_ALLOW_SELF_LB_UC |
			   MLX5_QP_FLAG_TIR_ALLOW_SELF_LB_MC))
		mlx5_ib_disable_lb(dev, false, true);
	mlx5_cmd_destroy_tir(dev->mdev, rq->tirn, to_mpd(pd)->uid);
}

static int create_raw_packet_qp_tir(struct mlx5_ib_dev *dev,
				    struct mlx5_ib_rq *rq, u32 tdn,
				    u32 *qp_flags_en, struct ib_pd *pd,
				    u32 *out)
{
	u8 lb_flag = 0;
	u32 *in;
	void *tirc;
	int inlen;
	int err;

	inlen = MLX5_ST_SZ_BYTES(create_tir_in);
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	MLX5_SET(create_tir_in, in, uid, to_mpd(pd)->uid);
	tirc = MLX5_ADDR_OF(create_tir_in, in, ctx);
	MLX5_SET(tirc, tirc, disp_type, MLX5_TIRC_DISP_TYPE_DIRECT);
	MLX5_SET(tirc, tirc, inline_rqn, rq->base.mqp.qpn);
	MLX5_SET(tirc, tirc, transport_domain, tdn);
	if (*qp_flags_en & MLX5_QP_FLAG_TUNNEL_OFFLOADS)
		MLX5_SET(tirc, tirc, tunneled_offload_en, 1);

	if (*qp_flags_en & MLX5_QP_FLAG_TIR_ALLOW_SELF_LB_UC)
		lb_flag |= MLX5_TIRC_SELF_LB_BLOCK_BLOCK_UNICAST;

	if (*qp_flags_en & MLX5_QP_FLAG_TIR_ALLOW_SELF_LB_MC)
		lb_flag |= MLX5_TIRC_SELF_LB_BLOCK_BLOCK_MULTICAST;

	if (dev->is_rep) {
		lb_flag |= MLX5_TIRC_SELF_LB_BLOCK_BLOCK_UNICAST;
		*qp_flags_en |= MLX5_QP_FLAG_TIR_ALLOW_SELF_LB_UC;
	}

	MLX5_SET(tirc, tirc, self_lb_block, lb_flag);
	MLX5_SET(create_tir_in, in, opcode, MLX5_CMD_OP_CREATE_TIR);
	err = mlx5_cmd_exec_inout(dev->mdev, create_tir, in, out);
	rq->tirn = MLX5_GET(create_tir_out, out, tirn);
	if (!err && MLX5_GET(tirc, tirc, self_lb_block)) {
		err = mlx5_ib_enable_lb(dev, false, true);

		if (err)
			destroy_raw_packet_qp_tir(dev, rq, 0, pd);
	}
	kvfree(in);

	return err;
}

static int create_raw_packet_qp(struct mlx5_ib_dev *dev, struct mlx5_ib_qp *qp,
				u32 *in, size_t inlen, struct ib_pd *pd,
				struct ib_udata *udata,
				struct mlx5_ib_create_qp_resp *resp,
				struct ib_qp_init_attr *init_attr)
{
	struct mlx5_ib_raw_packet_qp *raw_packet_qp = &qp->raw_packet_qp;
	struct mlx5_ib_sq *sq = &raw_packet_qp->sq;
	struct mlx5_ib_rq *rq = &raw_packet_qp->rq;
	struct mlx5_ib_ucontext *mucontext = rdma_udata_to_drv_context(
		udata, struct mlx5_ib_ucontext, ibucontext);
	int err;
	u32 tdn = mucontext->tdn;
	u16 uid = to_mpd(pd)->uid;
	u32 out[MLX5_ST_SZ_DW(create_tir_out)] = {};

	if (!qp->sq.wqe_cnt && !qp->rq.wqe_cnt)
		return -EINVAL;
	if (qp->sq.wqe_cnt) {
		err = create_raw_packet_qp_tis(dev, qp, sq, tdn, pd);
		if (err)
			return err;

		err = create_raw_packet_qp_sq(dev, udata, sq, in, pd,
					      to_mcq(init_attr->send_cq));
		if (err)
			goto err_destroy_tis;

		if (uid) {
			resp->tisn = sq->tisn;
			resp->comp_mask |= MLX5_IB_CREATE_QP_RESP_MASK_TISN;
			resp->sqn = sq->base.mqp.qpn;
			resp->comp_mask |= MLX5_IB_CREATE_QP_RESP_MASK_SQN;
		}

		sq->base.container_mibqp = qp;
		sq->base.mqp.event = mlx5_ib_qp_event;
	}

	if (qp->rq.wqe_cnt) {
		rq->base.container_mibqp = qp;

		if (qp->flags & IB_QP_CREATE_CVLAN_STRIPPING)
			rq->flags |= MLX5_IB_RQ_CVLAN_STRIPPING;
		if (qp->flags & IB_QP_CREATE_PCI_WRITE_END_PADDING)
			rq->flags |= MLX5_IB_RQ_PCI_WRITE_END_PADDING;
		err = create_raw_packet_qp_rq(dev, rq, in, pd,
					      to_mcq(init_attr->recv_cq));
		if (err)
			goto err_destroy_sq;

		err = create_raw_packet_qp_tir(dev, rq, tdn, &qp->flags_en, pd,
					       out);
		if (err)
			goto err_destroy_rq;

		if (uid) {
			resp->rqn = rq->base.mqp.qpn;
			resp->comp_mask |= MLX5_IB_CREATE_QP_RESP_MASK_RQN;
			resp->tirn = rq->tirn;
			resp->comp_mask |= MLX5_IB_CREATE_QP_RESP_MASK_TIRN;
			if (MLX5_CAP_FLOWTABLE_NIC_RX(dev->mdev, sw_owner) ||
			    MLX5_CAP_FLOWTABLE_NIC_RX(dev->mdev, sw_owner_v2)) {
				resp->tir_icm_addr = MLX5_GET(
					create_tir_out, out, icm_address_31_0);
				resp->tir_icm_addr |=
					(u64)MLX5_GET(create_tir_out, out,
						      icm_address_39_32)
					<< 32;
				resp->tir_icm_addr |=
					(u64)MLX5_GET(create_tir_out, out,
						      icm_address_63_40)
					<< 40;
				resp->comp_mask |=
					MLX5_IB_CREATE_QP_RESP_MASK_TIR_ICM_ADDR;
			}
		}
	}

	qp->trans_qp.base.mqp.qpn = qp->sq.wqe_cnt ? sq->base.mqp.qpn :
						     rq->base.mqp.qpn;
	return 0;

err_destroy_rq:
	destroy_raw_packet_qp_rq(dev, rq);
err_destroy_sq:
	if (!qp->sq.wqe_cnt)
		return err;
	destroy_raw_packet_qp_sq(dev, sq);
err_destroy_tis:
	destroy_raw_packet_qp_tis(dev, sq, pd);

	return err;
}

static void destroy_raw_packet_qp(struct mlx5_ib_dev *dev,
				  struct mlx5_ib_qp *qp)
{
	struct mlx5_ib_raw_packet_qp *raw_packet_qp = &qp->raw_packet_qp;
	struct mlx5_ib_sq *sq = &raw_packet_qp->sq;
	struct mlx5_ib_rq *rq = &raw_packet_qp->rq;

	if (qp->rq.wqe_cnt) {
		destroy_raw_packet_qp_tir(dev, rq, qp->flags_en, qp->ibqp.pd);
		destroy_raw_packet_qp_rq(dev, rq);
	}

	if (qp->sq.wqe_cnt) {
		destroy_raw_packet_qp_sq(dev, sq);
		destroy_raw_packet_qp_tis(dev, sq, qp->ibqp.pd);
	}
}

static void raw_packet_qp_copy_info(struct mlx5_ib_qp *qp,
				    struct mlx5_ib_raw_packet_qp *raw_packet_qp)
{
	struct mlx5_ib_sq *sq = &raw_packet_qp->sq;
	struct mlx5_ib_rq *rq = &raw_packet_qp->rq;

	sq->sq = &qp->sq;
	rq->rq = &qp->rq;
	sq->doorbell = &qp->db;
	rq->doorbell = &qp->db;
}

static void destroy_rss_raw_qp_tir(struct mlx5_ib_dev *dev, struct mlx5_ib_qp *qp)
{
	if (qp->flags_en & (MLX5_QP_FLAG_TIR_ALLOW_SELF_LB_UC |
			    MLX5_QP_FLAG_TIR_ALLOW_SELF_LB_MC))
		mlx5_ib_disable_lb(dev, false, true);
	mlx5_cmd_destroy_tir(dev->mdev, qp->rss_qp.tirn,
			     to_mpd(qp->ibqp.pd)->uid);
}

struct mlx5_create_qp_params {
	struct ib_udata *udata;
	size_t inlen;
	size_t outlen;
	size_t ucmd_size;
	void *ucmd;
	u8 is_rss_raw : 1;
	struct ib_qp_init_attr *attr;
	u32 uidx;
	struct mlx5_ib_create_qp_resp resp;
};

static int create_rss_raw_qp_tir(struct mlx5_ib_dev *dev, struct ib_pd *pd,
				 struct mlx5_ib_qp *qp,
				 struct mlx5_create_qp_params *params)
{
	struct ib_qp_init_attr *init_attr = params->attr;
	struct mlx5_ib_create_qp_rss *ucmd = params->ucmd;
	struct ib_udata *udata = params->udata;
	struct mlx5_ib_ucontext *mucontext = rdma_udata_to_drv_context(
		udata, struct mlx5_ib_ucontext, ibucontext);
	int inlen;
	int outlen;
	int err;
	u32 *in;
	u32 *out;
	void *tirc;
	void *hfso;
	u32 selected_fields = 0;
	u32 outer_l4;
	u32 tdn = mucontext->tdn;
	u8 lb_flag = 0;

	if (ucmd->comp_mask) {
		mlx5_ib_dbg(dev, "invalid comp mask\n");
		return -EOPNOTSUPP;
	}

	if (ucmd->rx_hash_fields_mask & MLX5_RX_HASH_INNER &&
	    !(ucmd->flags & MLX5_QP_FLAG_TUNNEL_OFFLOADS)) {
		mlx5_ib_dbg(dev, "Tunnel offloads must be set for inner RSS\n");
		return -EOPNOTSUPP;
	}

	if (dev->is_rep)
		qp->flags_en |= MLX5_QP_FLAG_TIR_ALLOW_SELF_LB_UC;

	if (qp->flags_en & MLX5_QP_FLAG_TIR_ALLOW_SELF_LB_UC)
		lb_flag |= MLX5_TIRC_SELF_LB_BLOCK_BLOCK_UNICAST;

	if (qp->flags_en & MLX5_QP_FLAG_TIR_ALLOW_SELF_LB_MC)
		lb_flag |= MLX5_TIRC_SELF_LB_BLOCK_BLOCK_MULTICAST;

	inlen = MLX5_ST_SZ_BYTES(create_tir_in);
	outlen = MLX5_ST_SZ_BYTES(create_tir_out);
	in = kvzalloc(inlen + outlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	out = in + MLX5_ST_SZ_DW(create_tir_in);
	MLX5_SET(create_tir_in, in, uid, to_mpd(pd)->uid);
	tirc = MLX5_ADDR_OF(create_tir_in, in, ctx);
	MLX5_SET(tirc, tirc, disp_type,
		 MLX5_TIRC_DISP_TYPE_INDIRECT);
	MLX5_SET(tirc, tirc, indirect_table,
		 init_attr->rwq_ind_tbl->ind_tbl_num);
	MLX5_SET(tirc, tirc, transport_domain, tdn);

	hfso = MLX5_ADDR_OF(tirc, tirc, rx_hash_field_selector_outer);

	if (ucmd->flags & MLX5_QP_FLAG_TUNNEL_OFFLOADS)
		MLX5_SET(tirc, tirc, tunneled_offload_en, 1);

	MLX5_SET(tirc, tirc, self_lb_block, lb_flag);

	if (ucmd->rx_hash_fields_mask & MLX5_RX_HASH_INNER)
		hfso = MLX5_ADDR_OF(tirc, tirc, rx_hash_field_selector_inner);
	else
		hfso = MLX5_ADDR_OF(tirc, tirc, rx_hash_field_selector_outer);

	switch (ucmd->rx_hash_function) {
	case MLX5_RX_HASH_FUNC_TOEPLITZ:
	{
		void *rss_key = MLX5_ADDR_OF(tirc, tirc, rx_hash_toeplitz_key);
		size_t len = MLX5_FLD_SZ_BYTES(tirc, rx_hash_toeplitz_key);

		if (len != ucmd->rx_key_len) {
			err = -EINVAL;
			goto err;
		}

		MLX5_SET(tirc, tirc, rx_hash_fn, MLX5_RX_HASH_FN_TOEPLITZ);
		memcpy(rss_key, ucmd->rx_hash_key, len);
		break;
	}
	default:
		err = -EOPNOTSUPP;
		goto err;
	}

	if (!ucmd->rx_hash_fields_mask) {
		/* special case when this TIR serves as steering entry without hashing */
		if (!init_attr->rwq_ind_tbl->log_ind_tbl_size)
			goto create_tir;
		err = -EINVAL;
		goto err;
	}

	if (((ucmd->rx_hash_fields_mask & MLX5_RX_HASH_SRC_IPV4) ||
	     (ucmd->rx_hash_fields_mask & MLX5_RX_HASH_DST_IPV4)) &&
	     ((ucmd->rx_hash_fields_mask & MLX5_RX_HASH_SRC_IPV6) ||
	     (ucmd->rx_hash_fields_mask & MLX5_RX_HASH_DST_IPV6))) {
		err = -EINVAL;
		goto err;
	}

	/* If none of IPV4 & IPV6 SRC/DST was set - this bit field is ignored */
	if ((ucmd->rx_hash_fields_mask & MLX5_RX_HASH_SRC_IPV4) ||
	    (ucmd->rx_hash_fields_mask & MLX5_RX_HASH_DST_IPV4))
		MLX5_SET(rx_hash_field_select, hfso, l3_prot_type,
			 MLX5_L3_PROT_TYPE_IPV4);
	else if ((ucmd->rx_hash_fields_mask & MLX5_RX_HASH_SRC_IPV6) ||
		 (ucmd->rx_hash_fields_mask & MLX5_RX_HASH_DST_IPV6))
		MLX5_SET(rx_hash_field_select, hfso, l3_prot_type,
			 MLX5_L3_PROT_TYPE_IPV6);

	outer_l4 = ((ucmd->rx_hash_fields_mask & MLX5_RX_HASH_SRC_PORT_TCP) ||
		    (ucmd->rx_hash_fields_mask & MLX5_RX_HASH_DST_PORT_TCP))
			   << 0 |
		   ((ucmd->rx_hash_fields_mask & MLX5_RX_HASH_SRC_PORT_UDP) ||
		    (ucmd->rx_hash_fields_mask & MLX5_RX_HASH_DST_PORT_UDP))
			   << 1 |
		   (ucmd->rx_hash_fields_mask & MLX5_RX_HASH_IPSEC_SPI) << 2;

	/* Check that only one l4 protocol is set */
	if (outer_l4 & (outer_l4 - 1)) {
		err = -EINVAL;
		goto err;
	}

	/* If none of TCP & UDP SRC/DST was set - this bit field is ignored */
	if ((ucmd->rx_hash_fields_mask & MLX5_RX_HASH_SRC_PORT_TCP) ||
	    (ucmd->rx_hash_fields_mask & MLX5_RX_HASH_DST_PORT_TCP))
		MLX5_SET(rx_hash_field_select, hfso, l4_prot_type,
			 MLX5_L4_PROT_TYPE_TCP);
	else if ((ucmd->rx_hash_fields_mask & MLX5_RX_HASH_SRC_PORT_UDP) ||
		 (ucmd->rx_hash_fields_mask & MLX5_RX_HASH_DST_PORT_UDP))
		MLX5_SET(rx_hash_field_select, hfso, l4_prot_type,
			 MLX5_L4_PROT_TYPE_UDP);

	if ((ucmd->rx_hash_fields_mask & MLX5_RX_HASH_SRC_IPV4) ||
	    (ucmd->rx_hash_fields_mask & MLX5_RX_HASH_SRC_IPV6))
		selected_fields |= MLX5_HASH_FIELD_SEL_SRC_IP;

	if ((ucmd->rx_hash_fields_mask & MLX5_RX_HASH_DST_IPV4) ||
	    (ucmd->rx_hash_fields_mask & MLX5_RX_HASH_DST_IPV6))
		selected_fields |= MLX5_HASH_FIELD_SEL_DST_IP;

	if ((ucmd->rx_hash_fields_mask & MLX5_RX_HASH_SRC_PORT_TCP) ||
	    (ucmd->rx_hash_fields_mask & MLX5_RX_HASH_SRC_PORT_UDP))
		selected_fields |= MLX5_HASH_FIELD_SEL_L4_SPORT;

	if ((ucmd->rx_hash_fields_mask & MLX5_RX_HASH_DST_PORT_TCP) ||
	    (ucmd->rx_hash_fields_mask & MLX5_RX_HASH_DST_PORT_UDP))
		selected_fields |= MLX5_HASH_FIELD_SEL_L4_DPORT;

	if (ucmd->rx_hash_fields_mask & MLX5_RX_HASH_IPSEC_SPI)
		selected_fields |= MLX5_HASH_FIELD_SEL_IPSEC_SPI;

	MLX5_SET(rx_hash_field_select, hfso, selected_fields, selected_fields);

create_tir:
	MLX5_SET(create_tir_in, in, opcode, MLX5_CMD_OP_CREATE_TIR);
	err = mlx5_cmd_exec_inout(dev->mdev, create_tir, in, out);

	qp->rss_qp.tirn = MLX5_GET(create_tir_out, out, tirn);
	if (!err && MLX5_GET(tirc, tirc, self_lb_block)) {
		err = mlx5_ib_enable_lb(dev, false, true);

		if (err)
			mlx5_cmd_destroy_tir(dev->mdev, qp->rss_qp.tirn,
					     to_mpd(pd)->uid);
	}

	if (err)
		goto err;

	if (mucontext->devx_uid) {
		params->resp.comp_mask |= MLX5_IB_CREATE_QP_RESP_MASK_TIRN;
		params->resp.tirn = qp->rss_qp.tirn;
		if (MLX5_CAP_FLOWTABLE_NIC_RX(dev->mdev, sw_owner) ||
		    MLX5_CAP_FLOWTABLE_NIC_RX(dev->mdev, sw_owner_v2)) {
			params->resp.tir_icm_addr =
				MLX5_GET(create_tir_out, out, icm_address_31_0);
			params->resp.tir_icm_addr |=
				(u64)MLX5_GET(create_tir_out, out,
					      icm_address_39_32)
				<< 32;
			params->resp.tir_icm_addr |=
				(u64)MLX5_GET(create_tir_out, out,
					      icm_address_63_40)
				<< 40;
			params->resp.comp_mask |=
				MLX5_IB_CREATE_QP_RESP_MASK_TIR_ICM_ADDR;
		}
	}

	kvfree(in);
	/* qpn is reserved for that QP */
	qp->trans_qp.base.mqp.qpn = 0;
	qp->is_rss = true;
	return 0;

err:
	kvfree(in);
	return err;
}

static void configure_requester_scat_cqe(struct mlx5_ib_dev *dev,
					 struct mlx5_ib_qp *qp,
					 struct ib_qp_init_attr *init_attr,
					 void *qpc)
{
	int scqe_sz;
	bool allow_scat_cqe = false;

	allow_scat_cqe = qp->flags_en & MLX5_QP_FLAG_ALLOW_SCATTER_CQE;

	if (!allow_scat_cqe && init_attr->sq_sig_type != IB_SIGNAL_ALL_WR)
		return;

	scqe_sz = mlx5_ib_get_cqe_size(init_attr->send_cq);
	if (scqe_sz == 128) {
		MLX5_SET(qpc, qpc, cs_req, MLX5_REQ_SCAT_DATA64_CQE);
		return;
	}

	if (init_attr->qp_type != MLX5_IB_QPT_DCI ||
	    MLX5_CAP_GEN(dev->mdev, dc_req_scat_data_cqe))
		MLX5_SET(qpc, qpc, cs_req, MLX5_REQ_SCAT_DATA32_CQE);
}

static int atomic_size_to_mode(int size_mask)
{
	/* driver does not support atomic_size > 256B
	 * and does not know how to translate bigger sizes
	 */
	int supported_size_mask = size_mask & 0x1ff;
	int log_max_size;

	if (!supported_size_mask)
		return -EOPNOTSUPP;

	log_max_size = __fls(supported_size_mask);

	if (log_max_size > 3)
		return log_max_size;

	return MLX5_ATOMIC_MODE_8B;
}

static int get_atomic_mode(struct mlx5_ib_dev *dev,
			   enum ib_qp_type qp_type)
{
	u8 atomic_operations = MLX5_CAP_ATOMIC(dev->mdev, atomic_operations);
	u8 atomic = MLX5_CAP_GEN(dev->mdev, atomic);
	int atomic_mode = -EOPNOTSUPP;
	int atomic_size_mask;

	if (!atomic)
		return -EOPNOTSUPP;

	if (qp_type == MLX5_IB_QPT_DCT)
		atomic_size_mask = MLX5_CAP_ATOMIC(dev->mdev, atomic_size_dc);
	else
		atomic_size_mask = MLX5_CAP_ATOMIC(dev->mdev, atomic_size_qp);

	if ((atomic_operations & MLX5_ATOMIC_OPS_EXTENDED_CMP_SWAP) ||
	    (atomic_operations & MLX5_ATOMIC_OPS_EXTENDED_FETCH_ADD))
		atomic_mode = atomic_size_to_mode(atomic_size_mask);

	if (atomic_mode <= 0 &&
	    (atomic_operations & MLX5_ATOMIC_OPS_CMP_SWAP &&
	     atomic_operations & MLX5_ATOMIC_OPS_FETCH_ADD))
		atomic_mode = MLX5_ATOMIC_MODE_IB_COMP;

	return atomic_mode;
}

static int create_xrc_tgt_qp(struct mlx5_ib_dev *dev, struct mlx5_ib_qp *qp,
			     struct mlx5_create_qp_params *params)
{
	struct ib_qp_init_attr *attr = params->attr;
	u32 uidx = params->uidx;
	struct mlx5_ib_resources *devr = &dev->devr;
	u32 out[MLX5_ST_SZ_DW(create_qp_out)] = {};
	int inlen = MLX5_ST_SZ_BYTES(create_qp_in);
	struct mlx5_core_dev *mdev = dev->mdev;
	struct mlx5_ib_qp_base *base;
	unsigned long flags;
	void *qpc;
	u32 *in;
	int err;

	if (attr->sq_sig_type == IB_SIGNAL_ALL_WR)
		qp->sq_signal_bits = MLX5_WQE_CTRL_CQ_UPDATE;

	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	qpc = MLX5_ADDR_OF(create_qp_in, in, qpc);

	MLX5_SET(qpc, qpc, st, MLX5_QP_ST_XRC);
	MLX5_SET(qpc, qpc, pm_state, MLX5_QP_PM_MIGRATED);
	MLX5_SET(qpc, qpc, pd, to_mpd(devr->p0)->pdn);

	if (qp->flags & IB_QP_CREATE_BLOCK_MULTICAST_LOOPBACK)
		MLX5_SET(qpc, qpc, block_lb_mc, 1);
	if (qp->flags & IB_QP_CREATE_CROSS_CHANNEL)
		MLX5_SET(qpc, qpc, cd_master, 1);
	if (qp->flags & IB_QP_CREATE_MANAGED_SEND)
		MLX5_SET(qpc, qpc, cd_slave_send, 1);
	if (qp->flags & IB_QP_CREATE_MANAGED_RECV)
		MLX5_SET(qpc, qpc, cd_slave_receive, 1);

	MLX5_SET(qpc, qpc, ts_format, mlx5_get_qp_default_ts(dev->mdev));
	MLX5_SET(qpc, qpc, rq_type, MLX5_SRQ_RQ);
	MLX5_SET(qpc, qpc, no_sq, 1);
	MLX5_SET(qpc, qpc, cqn_rcv, to_mcq(devr->c0)->mcq.cqn);
	MLX5_SET(qpc, qpc, cqn_snd, to_mcq(devr->c0)->mcq.cqn);
	MLX5_SET(qpc, qpc, srqn_rmpn_xrqn, to_msrq(devr->s0)->msrq.srqn);
	MLX5_SET(qpc, qpc, xrcd, to_mxrcd(attr->xrcd)->xrcdn);
	MLX5_SET64(qpc, qpc, dbr_addr, qp->db.dma);

	/* 0xffffff means we ask to work with cqe version 0 */
	if (MLX5_CAP_GEN(mdev, cqe_version) == MLX5_CQE_VERSION_V1)
		MLX5_SET(qpc, qpc, user_index, uidx);

	if (qp->flags & IB_QP_CREATE_PCI_WRITE_END_PADDING) {
		MLX5_SET(qpc, qpc, end_padding_mode,
			 MLX5_WQ_END_PAD_MODE_ALIGN);
		/* Special case to clean flag */
		qp->flags &= ~IB_QP_CREATE_PCI_WRITE_END_PADDING;
	}

	base = &qp->trans_qp.base;
	err = mlx5_qpc_create_qp(dev, &base->mqp, in, inlen, out);
	kvfree(in);
	if (err)
		return err;

	base->container_mibqp = qp;
	base->mqp.event = mlx5_ib_qp_event;
	if (MLX5_CAP_GEN(mdev, ece_support))
		params->resp.ece_options = MLX5_GET(create_qp_out, out, ece);

	spin_lock_irqsave(&dev->reset_flow_resource_lock, flags);
	list_add_tail(&qp->qps_list, &dev->qp_list);
	spin_unlock_irqrestore(&dev->reset_flow_resource_lock, flags);

	qp->trans_qp.xrcdn = to_mxrcd(attr->xrcd)->xrcdn;
	return 0;
}

static int create_dci(struct mlx5_ib_dev *dev, struct ib_pd *pd,
		      struct mlx5_ib_qp *qp,
		      struct mlx5_create_qp_params *params)
{
	struct ib_qp_init_attr *init_attr = params->attr;
	struct mlx5_ib_create_qp *ucmd = params->ucmd;
	u32 out[MLX5_ST_SZ_DW(create_qp_out)] = {};
	struct ib_udata *udata = params->udata;
	u32 uidx = params->uidx;
	struct mlx5_ib_resources *devr = &dev->devr;
	int inlen = MLX5_ST_SZ_BYTES(create_qp_in);
	struct mlx5_core_dev *mdev = dev->mdev;
	struct mlx5_ib_cq *send_cq;
	struct mlx5_ib_cq *recv_cq;
	unsigned long flags;
	struct mlx5_ib_qp_base *base;
	int ts_format;
	int mlx5_st;
	void *qpc;
	u32 *in;
	int err;

	spin_lock_init(&qp->sq.lock);
	spin_lock_init(&qp->rq.lock);

	mlx5_st = to_mlx5_st(qp->type);
	if (mlx5_st < 0)
		return -EINVAL;

	if (init_attr->sq_sig_type == IB_SIGNAL_ALL_WR)
		qp->sq_signal_bits = MLX5_WQE_CTRL_CQ_UPDATE;

	base = &qp->trans_qp.base;

	qp->has_rq = qp_has_rq(init_attr);
	err = set_rq_size(dev, &init_attr->cap, qp->has_rq, qp, ucmd);
	if (err) {
		mlx5_ib_dbg(dev, "err %d\n", err);
		return err;
	}

	if (ucmd->rq_wqe_shift != qp->rq.wqe_shift ||
	    ucmd->rq_wqe_count != qp->rq.wqe_cnt)
		return -EINVAL;

	if (ucmd->sq_wqe_count > (1 << MLX5_CAP_GEN(mdev, log_max_qp_sz)))
		return -EINVAL;

	ts_format = get_qp_ts_format(dev, to_mcq(init_attr->send_cq),
				     to_mcq(init_attr->recv_cq));

	if (ts_format < 0)
		return ts_format;

	err = _create_user_qp(dev, pd, qp, udata, init_attr, &in, &params->resp,
			      &inlen, base, ucmd);
	if (err)
		return err;

	if (MLX5_CAP_GEN(mdev, ece_support))
		MLX5_SET(create_qp_in, in, ece, ucmd->ece_options);
	qpc = MLX5_ADDR_OF(create_qp_in, in, qpc);

	MLX5_SET(qpc, qpc, st, mlx5_st);
	MLX5_SET(qpc, qpc, pm_state, MLX5_QP_PM_MIGRATED);
	MLX5_SET(qpc, qpc, pd, to_mpd(pd)->pdn);

	if (qp->flags_en & MLX5_QP_FLAG_SIGNATURE)
		MLX5_SET(qpc, qpc, wq_signature, 1);

	if (qp->flags & IB_QP_CREATE_CROSS_CHANNEL)
		MLX5_SET(qpc, qpc, cd_master, 1);
	if (qp->flags & IB_QP_CREATE_MANAGED_SEND)
		MLX5_SET(qpc, qpc, cd_slave_send, 1);
	if (qp->flags_en & MLX5_QP_FLAG_SCATTER_CQE)
		configure_requester_scat_cqe(dev, qp, init_attr, qpc);

	if (qp->rq.wqe_cnt) {
		MLX5_SET(qpc, qpc, log_rq_stride, qp->rq.wqe_shift - 4);
		MLX5_SET(qpc, qpc, log_rq_size, ilog2(qp->rq.wqe_cnt));
	}

	if (qp->flags_en & MLX5_QP_FLAG_DCI_STREAM) {
		MLX5_SET(qpc, qpc, log_num_dci_stream_channels,
			 ucmd->dci_streams.log_num_concurent);
		MLX5_SET(qpc, qpc, log_num_dci_errored_streams,
			 ucmd->dci_streams.log_num_errored);
	}

	MLX5_SET(qpc, qpc, ts_format, ts_format);
	MLX5_SET(qpc, qpc, rq_type, get_rx_type(qp, init_attr));

	MLX5_SET(qpc, qpc, log_sq_size, ilog2(qp->sq.wqe_cnt));

	/* Set default resources */
	if (init_attr->srq) {
		MLX5_SET(qpc, qpc, xrcd, devr->xrcdn0);
		MLX5_SET(qpc, qpc, srqn_rmpn_xrqn,
			 to_msrq(init_attr->srq)->msrq.srqn);
	} else {
		MLX5_SET(qpc, qpc, xrcd, devr->xrcdn1);
		MLX5_SET(qpc, qpc, srqn_rmpn_xrqn,
			 to_msrq(devr->s1)->msrq.srqn);
	}

	if (init_attr->send_cq)
		MLX5_SET(qpc, qpc, cqn_snd,
			 to_mcq(init_attr->send_cq)->mcq.cqn);

	if (init_attr->recv_cq)
		MLX5_SET(qpc, qpc, cqn_rcv,
			 to_mcq(init_attr->recv_cq)->mcq.cqn);

	MLX5_SET64(qpc, qpc, dbr_addr, qp->db.dma);

	/* 0xffffff means we ask to work with cqe version 0 */
	if (MLX5_CAP_GEN(mdev, cqe_version) == MLX5_CQE_VERSION_V1)
		MLX5_SET(qpc, qpc, user_index, uidx);

	if (qp->flags & IB_QP_CREATE_PCI_WRITE_END_PADDING) {
		MLX5_SET(qpc, qpc, end_padding_mode,
			 MLX5_WQ_END_PAD_MODE_ALIGN);
		/* Special case to clean flag */
		qp->flags &= ~IB_QP_CREATE_PCI_WRITE_END_PADDING;
	}

	err = mlx5_qpc_create_qp(dev, &base->mqp, in, inlen, out);

	kvfree(in);
	if (err)
		goto err_create;

	base->container_mibqp = qp;
	base->mqp.event = mlx5_ib_qp_event;
	if (MLX5_CAP_GEN(mdev, ece_support))
		params->resp.ece_options = MLX5_GET(create_qp_out, out, ece);

	get_cqs(qp->type, init_attr->send_cq, init_attr->recv_cq,
		&send_cq, &recv_cq);
	spin_lock_irqsave(&dev->reset_flow_resource_lock, flags);
	mlx5_ib_lock_cqs(send_cq, recv_cq);
	/* Maintain device to QPs access, needed for further handling via reset
	 * flow
	 */
	list_add_tail(&qp->qps_list, &dev->qp_list);
	/* Maintain CQ to QPs access, needed for further handling via reset flow
	 */
	if (send_cq)
		list_add_tail(&qp->cq_send_list, &send_cq->list_send_qp);
	if (recv_cq)
		list_add_tail(&qp->cq_recv_list, &recv_cq->list_recv_qp);
	mlx5_ib_unlock_cqs(send_cq, recv_cq);
	spin_unlock_irqrestore(&dev->reset_flow_resource_lock, flags);

	return 0;

err_create:
	destroy_qp(dev, qp, base, udata);
	return err;
}

static int create_user_qp(struct mlx5_ib_dev *dev, struct ib_pd *pd,
			  struct mlx5_ib_qp *qp,
			  struct mlx5_create_qp_params *params)
{
	struct ib_qp_init_attr *init_attr = params->attr;
	struct mlx5_ib_create_qp *ucmd = params->ucmd;
	u32 out[MLX5_ST_SZ_DW(create_qp_out)] = {};
	struct ib_udata *udata = params->udata;
	u32 uidx = params->uidx;
	struct mlx5_ib_resources *devr = &dev->devr;
	int inlen = MLX5_ST_SZ_BYTES(create_qp_in);
	struct mlx5_core_dev *mdev = dev->mdev;
	struct mlx5_ib_cq *send_cq;
	struct mlx5_ib_cq *recv_cq;
	unsigned long flags;
	struct mlx5_ib_qp_base *base;
	int ts_format;
	int mlx5_st;
	void *qpc;
	u32 *in;
	int err;

	spin_lock_init(&qp->sq.lock);
	spin_lock_init(&qp->rq.lock);

	mlx5_st = to_mlx5_st(qp->type);
	if (mlx5_st < 0)
		return -EINVAL;

	if (init_attr->sq_sig_type == IB_SIGNAL_ALL_WR)
		qp->sq_signal_bits = MLX5_WQE_CTRL_CQ_UPDATE;

	if (qp->flags & IB_QP_CREATE_SOURCE_QPN)
		qp->underlay_qpn = init_attr->source_qpn;

	base = (init_attr->qp_type == IB_QPT_RAW_PACKET ||
		qp->flags & IB_QP_CREATE_SOURCE_QPN) ?
	       &qp->raw_packet_qp.rq.base :
	       &qp->trans_qp.base;

	qp->has_rq = qp_has_rq(init_attr);
	err = set_rq_size(dev, &init_attr->cap, qp->has_rq, qp, ucmd);
	if (err) {
		mlx5_ib_dbg(dev, "err %d\n", err);
		return err;
	}

	if (ucmd->rq_wqe_shift != qp->rq.wqe_shift ||
	    ucmd->rq_wqe_count != qp->rq.wqe_cnt)
		return -EINVAL;

	if (ucmd->sq_wqe_count > (1 << MLX5_CAP_GEN(mdev, log_max_qp_sz)))
		return -EINVAL;

	if (init_attr->qp_type != IB_QPT_RAW_PACKET) {
		ts_format = get_qp_ts_format(dev, to_mcq(init_attr->send_cq),
					     to_mcq(init_attr->recv_cq));
		if (ts_format < 0)
			return ts_format;
	}

	err = _create_user_qp(dev, pd, qp, udata, init_attr, &in, &params->resp,
			      &inlen, base, ucmd);
	if (err)
		return err;

	if (is_sqp(init_attr->qp_type))
		qp->port = init_attr->port_num;

	if (MLX5_CAP_GEN(mdev, ece_support))
		MLX5_SET(create_qp_in, in, ece, ucmd->ece_options);
	qpc = MLX5_ADDR_OF(create_qp_in, in, qpc);

	MLX5_SET(qpc, qpc, st, mlx5_st);
	MLX5_SET(qpc, qpc, pm_state, MLX5_QP_PM_MIGRATED);
	MLX5_SET(qpc, qpc, pd, to_mpd(pd)->pdn);

	if (qp->flags_en & MLX5_QP_FLAG_SIGNATURE)
		MLX5_SET(qpc, qpc, wq_signature, 1);

	if (qp->flags & IB_QP_CREATE_BLOCK_MULTICAST_LOOPBACK)
		MLX5_SET(qpc, qpc, block_lb_mc, 1);

	if (qp->flags & IB_QP_CREATE_CROSS_CHANNEL)
		MLX5_SET(qpc, qpc, cd_master, 1);
	if (qp->flags & IB_QP_CREATE_MANAGED_SEND)
		MLX5_SET(qpc, qpc, cd_slave_send, 1);
	if (qp->flags & IB_QP_CREATE_MANAGED_RECV)
		MLX5_SET(qpc, qpc, cd_slave_receive, 1);
	if (qp->flags_en & MLX5_QP_FLAG_PACKET_BASED_CREDIT_MODE)
		MLX5_SET(qpc, qpc, req_e2e_credit_mode, 1);
	if ((qp->flags_en & MLX5_QP_FLAG_SCATTER_CQE) &&
	    (init_attr->qp_type == IB_QPT_RC ||
	     init_attr->qp_type == IB_QPT_UC)) {
		int rcqe_sz = mlx5_ib_get_cqe_size(init_attr->recv_cq);

		MLX5_SET(qpc, qpc, cs_res,
			 rcqe_sz == 128 ? MLX5_RES_SCAT_DATA64_CQE :
					  MLX5_RES_SCAT_DATA32_CQE);
	}
	if ((qp->flags_en & MLX5_QP_FLAG_SCATTER_CQE) &&
	    (qp->type == MLX5_IB_QPT_DCI || qp->type == IB_QPT_RC))
		configure_requester_scat_cqe(dev, qp, init_attr, qpc);

	if (qp->rq.wqe_cnt) {
		MLX5_SET(qpc, qpc, log_rq_stride, qp->rq.wqe_shift - 4);
		MLX5_SET(qpc, qpc, log_rq_size, ilog2(qp->rq.wqe_cnt));
	}

	if (init_attr->qp_type != IB_QPT_RAW_PACKET)
		MLX5_SET(qpc, qpc, ts_format, ts_format);

	MLX5_SET(qpc, qpc, rq_type, get_rx_type(qp, init_attr));

	if (qp->sq.wqe_cnt) {
		MLX5_SET(qpc, qpc, log_sq_size, ilog2(qp->sq.wqe_cnt));
	} else {
		MLX5_SET(qpc, qpc, no_sq, 1);
		if (init_attr->srq &&
		    init_attr->srq->srq_type == IB_SRQT_TM)
			MLX5_SET(qpc, qpc, offload_type,
				 MLX5_QPC_OFFLOAD_TYPE_RNDV);
	}

	/* Set default resources */
	switch (init_attr->qp_type) {
	case IB_QPT_XRC_INI:
		MLX5_SET(qpc, qpc, cqn_rcv, to_mcq(devr->c0)->mcq.cqn);
		MLX5_SET(qpc, qpc, xrcd, devr->xrcdn1);
		MLX5_SET(qpc, qpc, srqn_rmpn_xrqn, to_msrq(devr->s0)->msrq.srqn);
		break;
	default:
		if (init_attr->srq) {
			MLX5_SET(qpc, qpc, xrcd, devr->xrcdn0);
			MLX5_SET(qpc, qpc, srqn_rmpn_xrqn, to_msrq(init_attr->srq)->msrq.srqn);
		} else {
			MLX5_SET(qpc, qpc, xrcd, devr->xrcdn1);
			MLX5_SET(qpc, qpc, srqn_rmpn_xrqn, to_msrq(devr->s1)->msrq.srqn);
		}
	}

	if (init_attr->send_cq)
		MLX5_SET(qpc, qpc, cqn_snd, to_mcq(init_attr->send_cq)->mcq.cqn);

	if (init_attr->recv_cq)
		MLX5_SET(qpc, qpc, cqn_rcv, to_mcq(init_attr->recv_cq)->mcq.cqn);

	MLX5_SET64(qpc, qpc, dbr_addr, qp->db.dma);

	/* 0xffffff means we ask to work with cqe version 0 */
	if (MLX5_CAP_GEN(mdev, cqe_version) == MLX5_CQE_VERSION_V1)
		MLX5_SET(qpc, qpc, user_index, uidx);

	if (qp->flags & IB_QP_CREATE_PCI_WRITE_END_PADDING &&
	    init_attr->qp_type != IB_QPT_RAW_PACKET) {
		MLX5_SET(qpc, qpc, end_padding_mode,
			 MLX5_WQ_END_PAD_MODE_ALIGN);
		/* Special case to clean flag */
		qp->flags &= ~IB_QP_CREATE_PCI_WRITE_END_PADDING;
	}

	if (init_attr->qp_type == IB_QPT_RAW_PACKET ||
	    qp->flags & IB_QP_CREATE_SOURCE_QPN) {
		qp->raw_packet_qp.sq.ubuffer.buf_addr = ucmd->sq_buf_addr;
		raw_packet_qp_copy_info(qp, &qp->raw_packet_qp);
		err = create_raw_packet_qp(dev, qp, in, inlen, pd, udata,
					   &params->resp, init_attr);
	} else
		err = mlx5_qpc_create_qp(dev, &base->mqp, in, inlen, out);

	kvfree(in);
	if (err)
		goto err_create;

	base->container_mibqp = qp;
	base->mqp.event = mlx5_ib_qp_event;
	if (MLX5_CAP_GEN(mdev, ece_support))
		params->resp.ece_options = MLX5_GET(create_qp_out, out, ece);

	get_cqs(qp->type, init_attr->send_cq, init_attr->recv_cq,
		&send_cq, &recv_cq);
	spin_lock_irqsave(&dev->reset_flow_resource_lock, flags);
	mlx5_ib_lock_cqs(send_cq, recv_cq);
	/* Maintain device to QPs access, needed for further handling via reset
	 * flow
	 */
	list_add_tail(&qp->qps_list, &dev->qp_list);
	/* Maintain CQ to QPs access, needed for further handling via reset flow
	 */
	if (send_cq)
		list_add_tail(&qp->cq_send_list, &send_cq->list_send_qp);
	if (recv_cq)
		list_add_tail(&qp->cq_recv_list, &recv_cq->list_recv_qp);
	mlx5_ib_unlock_cqs(send_cq, recv_cq);
	spin_unlock_irqrestore(&dev->reset_flow_resource_lock, flags);

	return 0;

err_create:
	destroy_qp(dev, qp, base, udata);
	return err;
}

static int create_kernel_qp(struct mlx5_ib_dev *dev, struct ib_pd *pd,
			    struct mlx5_ib_qp *qp,
			    struct mlx5_create_qp_params *params)
{
	struct ib_qp_init_attr *attr = params->attr;
	u32 uidx = params->uidx;
	struct mlx5_ib_resources *devr = &dev->devr;
	u32 out[MLX5_ST_SZ_DW(create_qp_out)] = {};
	int inlen = MLX5_ST_SZ_BYTES(create_qp_in);
	struct mlx5_core_dev *mdev = dev->mdev;
	struct mlx5_ib_cq *send_cq;
	struct mlx5_ib_cq *recv_cq;
	unsigned long flags;
	struct mlx5_ib_qp_base *base;
	int mlx5_st;
	void *qpc;
	u32 *in;
	int err;

	spin_lock_init(&qp->sq.lock);
	spin_lock_init(&qp->rq.lock);

	mlx5_st = to_mlx5_st(qp->type);
	if (mlx5_st < 0)
		return -EINVAL;

	if (attr->sq_sig_type == IB_SIGNAL_ALL_WR)
		qp->sq_signal_bits = MLX5_WQE_CTRL_CQ_UPDATE;

	base = &qp->trans_qp.base;

	qp->has_rq = qp_has_rq(attr);
	err = set_rq_size(dev, &attr->cap, qp->has_rq, qp, NULL);
	if (err) {
		mlx5_ib_dbg(dev, "err %d\n", err);
		return err;
	}

	err = _create_kernel_qp(dev, attr, qp, &in, &inlen, base);
	if (err)
		return err;

	if (is_sqp(attr->qp_type))
		qp->port = attr->port_num;

	qpc = MLX5_ADDR_OF(create_qp_in, in, qpc);

	MLX5_SET(qpc, qpc, st, mlx5_st);
	MLX5_SET(qpc, qpc, pm_state, MLX5_QP_PM_MIGRATED);

	if (attr->qp_type != MLX5_IB_QPT_REG_UMR)
		MLX5_SET(qpc, qpc, pd, to_mpd(pd ? pd : devr->p0)->pdn);
	else
		MLX5_SET(qpc, qpc, latency_sensitive, 1);


	if (qp->flags & IB_QP_CREATE_BLOCK_MULTICAST_LOOPBACK)
		MLX5_SET(qpc, qpc, block_lb_mc, 1);

	if (qp->rq.wqe_cnt) {
		MLX5_SET(qpc, qpc, log_rq_stride, qp->rq.wqe_shift - 4);
		MLX5_SET(qpc, qpc, log_rq_size, ilog2(qp->rq.wqe_cnt));
	}

	MLX5_SET(qpc, qpc, rq_type, get_rx_type(qp, attr));

	if (qp->sq.wqe_cnt)
		MLX5_SET(qpc, qpc, log_sq_size, ilog2(qp->sq.wqe_cnt));
	else
		MLX5_SET(qpc, qpc, no_sq, 1);

	if (attr->srq) {
		MLX5_SET(qpc, qpc, xrcd, devr->xrcdn0);
		MLX5_SET(qpc, qpc, srqn_rmpn_xrqn,
			 to_msrq(attr->srq)->msrq.srqn);
	} else {
		MLX5_SET(qpc, qpc, xrcd, devr->xrcdn1);
		MLX5_SET(qpc, qpc, srqn_rmpn_xrqn,
			 to_msrq(devr->s1)->msrq.srqn);
	}

	if (attr->send_cq)
		MLX5_SET(qpc, qpc, cqn_snd, to_mcq(attr->send_cq)->mcq.cqn);

	if (attr->recv_cq)
		MLX5_SET(qpc, qpc, cqn_rcv, to_mcq(attr->recv_cq)->mcq.cqn);

	MLX5_SET64(qpc, qpc, dbr_addr, qp->db.dma);

	/* 0xffffff means we ask to work with cqe version 0 */
	if (MLX5_CAP_GEN(mdev, cqe_version) == MLX5_CQE_VERSION_V1)
		MLX5_SET(qpc, qpc, user_index, uidx);

	/* we use IB_QP_CREATE_IPOIB_UD_LSO to indicates ipoib qp */
	if (qp->flags & IB_QP_CREATE_IPOIB_UD_LSO)
		MLX5_SET(qpc, qpc, ulp_stateless_offload_mode, 1);

	if (qp->flags & IB_QP_CREATE_INTEGRITY_EN &&
	    MLX5_CAP_GEN(mdev, go_back_n))
		MLX5_SET(qpc, qpc, retry_mode, MLX5_QP_RM_GO_BACK_N);

	err = mlx5_qpc_create_qp(dev, &base->mqp, in, inlen, out);
	kvfree(in);
	if (err)
		goto err_create;

	base->container_mibqp = qp;
	base->mqp.event = mlx5_ib_qp_event;

	get_cqs(qp->type, attr->send_cq, attr->recv_cq,
		&send_cq, &recv_cq);
	spin_lock_irqsave(&dev->reset_flow_resource_lock, flags);
	mlx5_ib_lock_cqs(send_cq, recv_cq);
	/* Maintain device to QPs access, needed for further handling via reset
	 * flow
	 */
	list_add_tail(&qp->qps_list, &dev->qp_list);
	/* Maintain CQ to QPs access, needed for further handling via reset flow
	 */
	if (send_cq)
		list_add_tail(&qp->cq_send_list, &send_cq->list_send_qp);
	if (recv_cq)
		list_add_tail(&qp->cq_recv_list, &recv_cq->list_recv_qp);
	mlx5_ib_unlock_cqs(send_cq, recv_cq);
	spin_unlock_irqrestore(&dev->reset_flow_resource_lock, flags);

	return 0;

err_create:
	destroy_qp(dev, qp, base, NULL);
	return err;
}

static void mlx5_ib_lock_cqs(struct mlx5_ib_cq *send_cq, struct mlx5_ib_cq *recv_cq)
	__acquires(&send_cq->lock) __acquires(&recv_cq->lock)
{
	if (send_cq) {
		if (recv_cq) {
			if (send_cq->mcq.cqn < recv_cq->mcq.cqn)  {
				spin_lock(&send_cq->lock);
				spin_lock_nested(&recv_cq->lock,
						 SINGLE_DEPTH_NESTING);
			} else if (send_cq->mcq.cqn == recv_cq->mcq.cqn) {
				spin_lock(&send_cq->lock);
				__acquire(&recv_cq->lock);
			} else {
				spin_lock(&recv_cq->lock);
				spin_lock_nested(&send_cq->lock,
						 SINGLE_DEPTH_NESTING);
			}
		} else {
			spin_lock(&send_cq->lock);
			__acquire(&recv_cq->lock);
		}
	} else if (recv_cq) {
		spin_lock(&recv_cq->lock);
		__acquire(&send_cq->lock);
	} else {
		__acquire(&send_cq->lock);
		__acquire(&recv_cq->lock);
	}
}

static void mlx5_ib_unlock_cqs(struct mlx5_ib_cq *send_cq, struct mlx5_ib_cq *recv_cq)
	__releases(&send_cq->lock) __releases(&recv_cq->lock)
{
	if (send_cq) {
		if (recv_cq) {
			if (send_cq->mcq.cqn < recv_cq->mcq.cqn)  {
				spin_unlock(&recv_cq->lock);
				spin_unlock(&send_cq->lock);
			} else if (send_cq->mcq.cqn == recv_cq->mcq.cqn) {
				__release(&recv_cq->lock);
				spin_unlock(&send_cq->lock);
			} else {
				spin_unlock(&send_cq->lock);
				spin_unlock(&recv_cq->lock);
			}
		} else {
			__release(&recv_cq->lock);
			spin_unlock(&send_cq->lock);
		}
	} else if (recv_cq) {
		__release(&send_cq->lock);
		spin_unlock(&recv_cq->lock);
	} else {
		__release(&recv_cq->lock);
		__release(&send_cq->lock);
	}
}

static void get_cqs(enum ib_qp_type qp_type,
		    struct ib_cq *ib_send_cq, struct ib_cq *ib_recv_cq,
		    struct mlx5_ib_cq **send_cq, struct mlx5_ib_cq **recv_cq)
{
	switch (qp_type) {
	case IB_QPT_XRC_TGT:
		*send_cq = NULL;
		*recv_cq = NULL;
		break;
	case MLX5_IB_QPT_REG_UMR:
	case IB_QPT_XRC_INI:
		*send_cq = ib_send_cq ? to_mcq(ib_send_cq) : NULL;
		*recv_cq = NULL;
		break;

	case IB_QPT_SMI:
	case MLX5_IB_QPT_HW_GSI:
	case IB_QPT_RC:
	case IB_QPT_UC:
	case IB_QPT_UD:
	case IB_QPT_RAW_PACKET:
		*send_cq = ib_send_cq ? to_mcq(ib_send_cq) : NULL;
		*recv_cq = ib_recv_cq ? to_mcq(ib_recv_cq) : NULL;
		break;
	default:
		*send_cq = NULL;
		*recv_cq = NULL;
		break;
	}
}

static int modify_raw_packet_qp(struct mlx5_ib_dev *dev, struct mlx5_ib_qp *qp,
				const struct mlx5_modify_raw_qp_param *raw_qp_param,
				u8 lag_tx_affinity);

static void destroy_qp_common(struct mlx5_ib_dev *dev, struct mlx5_ib_qp *qp,
			      struct ib_udata *udata)
{
	struct mlx5_ib_cq *send_cq, *recv_cq;
	struct mlx5_ib_qp_base *base;
	unsigned long flags;
	int err;

	if (qp->is_rss) {
		destroy_rss_raw_qp_tir(dev, qp);
		return;
	}

	base = (qp->type == IB_QPT_RAW_PACKET ||
		qp->flags & IB_QP_CREATE_SOURCE_QPN) ?
		       &qp->raw_packet_qp.rq.base :
		       &qp->trans_qp.base;

	if (qp->state != IB_QPS_RESET) {
		if (qp->type != IB_QPT_RAW_PACKET &&
		    !(qp->flags & IB_QP_CREATE_SOURCE_QPN)) {
			err = mlx5_core_qp_modify(dev, MLX5_CMD_OP_2RST_QP, 0,
						  NULL, &base->mqp, NULL);
		} else {
			struct mlx5_modify_raw_qp_param raw_qp_param = {
				.operation = MLX5_CMD_OP_2RST_QP
			};

			err = modify_raw_packet_qp(dev, qp, &raw_qp_param, 0);
		}
		if (err)
			mlx5_ib_warn(dev, "mlx5_ib: modify QP 0x%06x to RESET failed\n",
				     base->mqp.qpn);
	}

	get_cqs(qp->type, qp->ibqp.send_cq, qp->ibqp.recv_cq, &send_cq,
		&recv_cq);

	spin_lock_irqsave(&dev->reset_flow_resource_lock, flags);
	mlx5_ib_lock_cqs(send_cq, recv_cq);
	/* del from lists under both locks above to protect reset flow paths */
	list_del(&qp->qps_list);
	if (send_cq)
		list_del(&qp->cq_send_list);

	if (recv_cq)
		list_del(&qp->cq_recv_list);

	if (!udata) {
		__mlx5_ib_cq_clean(recv_cq, base->mqp.qpn,
				   qp->ibqp.srq ? to_msrq(qp->ibqp.srq) : NULL);
		if (send_cq != recv_cq)
			__mlx5_ib_cq_clean(send_cq, base->mqp.qpn,
					   NULL);
	}
	mlx5_ib_unlock_cqs(send_cq, recv_cq);
	spin_unlock_irqrestore(&dev->reset_flow_resource_lock, flags);

	if (qp->type == IB_QPT_RAW_PACKET ||
	    qp->flags & IB_QP_CREATE_SOURCE_QPN) {
		destroy_raw_packet_qp(dev, qp);
	} else {
		err = mlx5_core_destroy_qp(dev, &base->mqp);
		if (err)
			mlx5_ib_warn(dev, "failed to destroy QP 0x%x\n",
				     base->mqp.qpn);
	}

	destroy_qp(dev, qp, base, udata);
}

static int create_dct(struct mlx5_ib_dev *dev, struct ib_pd *pd,
		      struct mlx5_ib_qp *qp,
		      struct mlx5_create_qp_params *params)
{
	struct ib_qp_init_attr *attr = params->attr;
	struct mlx5_ib_create_qp *ucmd = params->ucmd;
	u32 uidx = params->uidx;
	void *dctc;

	if (mlx5_lag_is_active(dev->mdev) && !MLX5_CAP_GEN(dev->mdev, lag_dct))
		return -EOPNOTSUPP;

	qp->dct.in = kzalloc(MLX5_ST_SZ_BYTES(create_dct_in), GFP_KERNEL);
	if (!qp->dct.in)
		return -ENOMEM;

	MLX5_SET(create_dct_in, qp->dct.in, uid, to_mpd(pd)->uid);
	dctc = MLX5_ADDR_OF(create_dct_in, qp->dct.in, dct_context_entry);
	MLX5_SET(dctc, dctc, pd, to_mpd(pd)->pdn);
	MLX5_SET(dctc, dctc, srqn_xrqn, to_msrq(attr->srq)->msrq.srqn);
	MLX5_SET(dctc, dctc, cqn, to_mcq(attr->recv_cq)->mcq.cqn);
	MLX5_SET64(dctc, dctc, dc_access_key, ucmd->access_key);
	MLX5_SET(dctc, dctc, user_index, uidx);
	if (MLX5_CAP_GEN(dev->mdev, ece_support))
		MLX5_SET(dctc, dctc, ece, ucmd->ece_options);

	if (qp->flags_en & MLX5_QP_FLAG_SCATTER_CQE) {
		int rcqe_sz = mlx5_ib_get_cqe_size(attr->recv_cq);

		if (rcqe_sz == 128)
			MLX5_SET(dctc, dctc, cs_res, MLX5_RES_SCAT_DATA64_CQE);
	}

	qp->state = IB_QPS_RESET;
	return 0;
}

static int check_qp_type(struct mlx5_ib_dev *dev, struct ib_qp_init_attr *attr,
			 enum ib_qp_type *type)
{
	if (attr->qp_type == IB_QPT_DRIVER && !MLX5_CAP_GEN(dev->mdev, dct))
		goto out;

	switch (attr->qp_type) {
	case IB_QPT_XRC_TGT:
	case IB_QPT_XRC_INI:
		if (!MLX5_CAP_GEN(dev->mdev, xrc))
			goto out;
		fallthrough;
	case IB_QPT_RC:
	case IB_QPT_UC:
	case IB_QPT_SMI:
	case MLX5_IB_QPT_HW_GSI:
	case IB_QPT_DRIVER:
	case IB_QPT_GSI:
	case IB_QPT_RAW_PACKET:
	case IB_QPT_UD:
	case MLX5_IB_QPT_REG_UMR:
		break;
	default:
		goto out;
	}

	*type = attr->qp_type;
	return 0;

out:
	mlx5_ib_dbg(dev, "Unsupported QP type %d\n", attr->qp_type);
	return -EOPNOTSUPP;
}

static int check_valid_flow(struct mlx5_ib_dev *dev, struct ib_pd *pd,
			    struct ib_qp_init_attr *attr,
			    struct ib_udata *udata)
{
	struct mlx5_ib_ucontext *ucontext = rdma_udata_to_drv_context(
		udata, struct mlx5_ib_ucontext, ibucontext);

	if (!udata) {
		/* Kernel create_qp callers */
		if (attr->rwq_ind_tbl)
			return -EOPNOTSUPP;

		switch (attr->qp_type) {
		case IB_QPT_RAW_PACKET:
		case IB_QPT_DRIVER:
			return -EOPNOTSUPP;
		default:
			return 0;
		}
	}

	/* Userspace create_qp callers */
	if (attr->qp_type == IB_QPT_RAW_PACKET && !ucontext->cqe_version) {
		mlx5_ib_dbg(dev,
			"Raw Packet QP is only supported for CQE version > 0\n");
		return -EINVAL;
	}

	if (attr->qp_type != IB_QPT_RAW_PACKET && attr->rwq_ind_tbl) {
		mlx5_ib_dbg(dev,
			    "Wrong QP type %d for the RWQ indirect table\n",
			    attr->qp_type);
		return -EINVAL;
	}

	/*
	 * We don't need to see this warning, it means that kernel code
	 * missing ib_pd. Placed here to catch developer's mistakes.
	 */
	WARN_ONCE(!pd && attr->qp_type != IB_QPT_XRC_TGT,
		  "There is a missing PD pointer assignment\n");
	return 0;
}

static void process_vendor_flag(struct mlx5_ib_dev *dev, int *flags, int flag,
				bool cond, struct mlx5_ib_qp *qp)
{
	if (!(*flags & flag))
		return;

	if (cond) {
		qp->flags_en |= flag;
		*flags &= ~flag;
		return;
	}

	switch (flag) {
	case MLX5_QP_FLAG_SCATTER_CQE:
	case MLX5_QP_FLAG_ALLOW_SCATTER_CQE:
		/*
		 * We don't return error if these flags were provided,
		 * and mlx5 doesn't have right capability.
		 */
		*flags &= ~(MLX5_QP_FLAG_SCATTER_CQE |
			    MLX5_QP_FLAG_ALLOW_SCATTER_CQE);
		return;
	default:
		break;
	}
	mlx5_ib_dbg(dev, "Vendor create QP flag 0x%X is not supported\n", flag);
}

static int process_vendor_flags(struct mlx5_ib_dev *dev, struct mlx5_ib_qp *qp,
				void *ucmd, struct ib_qp_init_attr *attr)
{
	struct mlx5_core_dev *mdev = dev->mdev;
	bool cond;
	int flags;

	if (attr->rwq_ind_tbl)
		flags = ((struct mlx5_ib_create_qp_rss *)ucmd)->flags;
	else
		flags = ((struct mlx5_ib_create_qp *)ucmd)->flags;

	switch (flags & (MLX5_QP_FLAG_TYPE_DCT | MLX5_QP_FLAG_TYPE_DCI)) {
	case MLX5_QP_FLAG_TYPE_DCI:
		qp->type = MLX5_IB_QPT_DCI;
		break;
	case MLX5_QP_FLAG_TYPE_DCT:
		qp->type = MLX5_IB_QPT_DCT;
		break;
	default:
		if (qp->type != IB_QPT_DRIVER)
			break;
		/*
		 * It is IB_QPT_DRIVER and or no subtype or
		 * wrong subtype were provided.
		 */
		return -EINVAL;
	}

	process_vendor_flag(dev, &flags, MLX5_QP_FLAG_TYPE_DCI, true, qp);
	process_vendor_flag(dev, &flags, MLX5_QP_FLAG_TYPE_DCT, true, qp);
	process_vendor_flag(dev, &flags, MLX5_QP_FLAG_DCI_STREAM,
			    MLX5_CAP_GEN(mdev, log_max_dci_stream_channels),
			    qp);

	process_vendor_flag(dev, &flags, MLX5_QP_FLAG_SIGNATURE, true, qp);
	process_vendor_flag(dev, &flags, MLX5_QP_FLAG_SCATTER_CQE,
			    MLX5_CAP_GEN(mdev, sctr_data_cqe), qp);
	process_vendor_flag(dev, &flags, MLX5_QP_FLAG_ALLOW_SCATTER_CQE,
			    MLX5_CAP_GEN(mdev, sctr_data_cqe), qp);

	if (qp->type == IB_QPT_RAW_PACKET) {
		cond = MLX5_CAP_ETH(mdev, tunnel_stateless_vxlan) ||
		       MLX5_CAP_ETH(mdev, tunnel_stateless_gre) ||
		       MLX5_CAP_ETH(mdev, tunnel_stateless_geneve_rx);
		process_vendor_flag(dev, &flags, MLX5_QP_FLAG_TUNNEL_OFFLOADS,
				    cond, qp);
		process_vendor_flag(dev, &flags,
				    MLX5_QP_FLAG_TIR_ALLOW_SELF_LB_UC, true,
				    qp);
		process_vendor_flag(dev, &flags,
				    MLX5_QP_FLAG_TIR_ALLOW_SELF_LB_MC, true,
				    qp);
	}

	if (qp->type == IB_QPT_RC)
		process_vendor_flag(dev, &flags,
				    MLX5_QP_FLAG_PACKET_BASED_CREDIT_MODE,
				    MLX5_CAP_GEN(mdev, qp_packet_based), qp);

	process_vendor_flag(dev, &flags, MLX5_QP_FLAG_BFREG_INDEX, true, qp);
	process_vendor_flag(dev, &flags, MLX5_QP_FLAG_UAR_PAGE_INDEX, true, qp);

	cond = qp->flags_en & ~(MLX5_QP_FLAG_TUNNEL_OFFLOADS |
				MLX5_QP_FLAG_TIR_ALLOW_SELF_LB_UC |
				MLX5_QP_FLAG_TIR_ALLOW_SELF_LB_MC);
	if (attr->rwq_ind_tbl && cond) {
		mlx5_ib_dbg(dev, "RSS RAW QP has unsupported flags 0x%X\n",
			    cond);
		return -EINVAL;
	}

	if (flags)
		mlx5_ib_dbg(dev, "udata has unsupported flags 0x%X\n", flags);

	return (flags) ? -EINVAL : 0;
	}

static void process_create_flag(struct mlx5_ib_dev *dev, int *flags, int flag,
				bool cond, struct mlx5_ib_qp *qp)
{
	if (!(*flags & flag))
		return;

	if (cond) {
		qp->flags |= flag;
		*flags &= ~flag;
		return;
	}

	if (flag == MLX5_IB_QP_CREATE_WC_TEST) {
		/*
		 * Special case, if condition didn't meet, it won't be error,
		 * just different in-kernel flow.
		 */
		*flags &= ~MLX5_IB_QP_CREATE_WC_TEST;
		return;
	}
	mlx5_ib_dbg(dev, "Verbs create QP flag 0x%X is not supported\n", flag);
}

static int process_create_flags(struct mlx5_ib_dev *dev, struct mlx5_ib_qp *qp,
				struct ib_qp_init_attr *attr)
{
	enum ib_qp_type qp_type = qp->type;
	struct mlx5_core_dev *mdev = dev->mdev;
	int create_flags = attr->create_flags;
	bool cond;

	if (qp_type == MLX5_IB_QPT_DCT)
		return (create_flags) ? -EINVAL : 0;

	if (qp_type == IB_QPT_RAW_PACKET && attr->rwq_ind_tbl)
		return (create_flags) ? -EINVAL : 0;

	process_create_flag(dev, &create_flags, IB_QP_CREATE_NETIF_QP,
			    mlx5_get_flow_namespace(dev->mdev,
						    MLX5_FLOW_NAMESPACE_BYPASS),
			    qp);
	process_create_flag(dev, &create_flags,
			    IB_QP_CREATE_INTEGRITY_EN,
			    MLX5_CAP_GEN(mdev, sho), qp);
	process_create_flag(dev, &create_flags,
			    IB_QP_CREATE_BLOCK_MULTICAST_LOOPBACK,
			    MLX5_CAP_GEN(mdev, block_lb_mc), qp);
	process_create_flag(dev, &create_flags, IB_QP_CREATE_CROSS_CHANNEL,
			    MLX5_CAP_GEN(mdev, cd), qp);
	process_create_flag(dev, &create_flags, IB_QP_CREATE_MANAGED_SEND,
			    MLX5_CAP_GEN(mdev, cd), qp);
	process_create_flag(dev, &create_flags, IB_QP_CREATE_MANAGED_RECV,
			    MLX5_CAP_GEN(mdev, cd), qp);

	if (qp_type == IB_QPT_UD) {
		process_create_flag(dev, &create_flags,
				    IB_QP_CREATE_IPOIB_UD_LSO,
				    MLX5_CAP_GEN(mdev, ipoib_basic_offloads),
				    qp);
		cond = MLX5_CAP_GEN(mdev, port_type) == MLX5_CAP_PORT_TYPE_IB;
		process_create_flag(dev, &create_flags, IB_QP_CREATE_SOURCE_QPN,
				    cond, qp);
	}

	if (qp_type == IB_QPT_RAW_PACKET) {
		cond = MLX5_CAP_GEN(mdev, eth_net_offloads) &&
		       MLX5_CAP_ETH(mdev, scatter_fcs);
		process_create_flag(dev, &create_flags,
				    IB_QP_CREATE_SCATTER_FCS, cond, qp);

		cond = MLX5_CAP_GEN(mdev, eth_net_offloads) &&
		       MLX5_CAP_ETH(mdev, vlan_cap);
		process_create_flag(dev, &create_flags,
				    IB_QP_CREATE_CVLAN_STRIPPING, cond, qp);
	}

	process_create_flag(dev, &create_flags,
			    IB_QP_CREATE_PCI_WRITE_END_PADDING,
			    MLX5_CAP_GEN(mdev, end_pad), qp);

	process_create_flag(dev, &create_flags, MLX5_IB_QP_CREATE_WC_TEST,
			    qp_type != MLX5_IB_QPT_REG_UMR, qp);
	process_create_flag(dev, &create_flags, MLX5_IB_QP_CREATE_SQPN_QP1,
			    true, qp);

	if (create_flags) {
		mlx5_ib_dbg(dev, "Create QP has unsupported flags 0x%X\n",
			    create_flags);
		return -EOPNOTSUPP;
	}
	return 0;
}

static int process_udata_size(struct mlx5_ib_dev *dev,
			      struct mlx5_create_qp_params *params)
{
	size_t ucmd = sizeof(struct mlx5_ib_create_qp);
	struct ib_udata *udata = params->udata;
	size_t outlen = udata->outlen;
	size_t inlen = udata->inlen;

	params->outlen = min(outlen, sizeof(struct mlx5_ib_create_qp_resp));
	params->ucmd_size = ucmd;
	if (!params->is_rss_raw) {
		/* User has old rdma-core, which doesn't support ECE */
		size_t min_inlen =
			offsetof(struct mlx5_ib_create_qp, ece_options);

		/*
		 * We will check in check_ucmd_data() that user
		 * cleared everything after inlen.
		 */
		params->inlen = (inlen < min_inlen) ? 0 : min(inlen, ucmd);
		goto out;
	}

	/* RSS RAW QP */
	if (inlen < offsetofend(struct mlx5_ib_create_qp_rss, flags))
		return -EINVAL;

	if (outlen < offsetofend(struct mlx5_ib_create_qp_resp, bfreg_index))
		return -EINVAL;

	ucmd = sizeof(struct mlx5_ib_create_qp_rss);
	params->ucmd_size = ucmd;
	if (inlen > ucmd && !ib_is_udata_cleared(udata, ucmd, inlen - ucmd))
		return -EINVAL;

	params->inlen = min(ucmd, inlen);
out:
	if (!params->inlen)
		mlx5_ib_dbg(dev, "udata is too small\n");

	return (params->inlen) ? 0 : -EINVAL;
}

static int create_qp(struct mlx5_ib_dev *dev, struct ib_pd *pd,
		     struct mlx5_ib_qp *qp,
		     struct mlx5_create_qp_params *params)
{
	int err;

	if (params->is_rss_raw) {
		err = create_rss_raw_qp_tir(dev, pd, qp, params);
		goto out;
	}

	switch (qp->type) {
	case MLX5_IB_QPT_DCT:
		err = create_dct(dev, pd, qp, params);
		break;
	case MLX5_IB_QPT_DCI:
		err = create_dci(dev, pd, qp, params);
		break;
	case IB_QPT_XRC_TGT:
		err = create_xrc_tgt_qp(dev, qp, params);
		break;
	case IB_QPT_GSI:
		err = mlx5_ib_create_gsi(pd, qp, params->attr);
		break;
	case MLX5_IB_QPT_HW_GSI:
		rdma_restrack_no_track(&qp->ibqp.res);
		fallthrough;
	case MLX5_IB_QPT_REG_UMR:
	default:
		if (params->udata)
			err = create_user_qp(dev, pd, qp, params);
		else
			err = create_kernel_qp(dev, pd, qp, params);
	}

out:
	if (err) {
		mlx5_ib_err(dev, "Create QP type %d failed\n", qp->type);
		return err;
	}

	if (is_qp0(qp->type))
		qp->ibqp.qp_num = 0;
	else if (is_qp1(qp->type))
		qp->ibqp.qp_num = 1;
	else
		qp->ibqp.qp_num = qp->trans_qp.base.mqp.qpn;

	mlx5_ib_dbg(dev,
		"QP type %d, ib qpn 0x%X, mlx qpn 0x%x, rcqn 0x%x, scqn 0x%x, ece 0x%x\n",
		qp->type, qp->ibqp.qp_num, qp->trans_qp.base.mqp.qpn,
		params->attr->recv_cq ? to_mcq(params->attr->recv_cq)->mcq.cqn :
					-1,
		params->attr->send_cq ? to_mcq(params->attr->send_cq)->mcq.cqn :
					-1,
		params->resp.ece_options);

	return 0;
}

static int check_qp_attr(struct mlx5_ib_dev *dev, struct mlx5_ib_qp *qp,
			 struct ib_qp_init_attr *attr)
{
	int ret = 0;

	switch (qp->type) {
	case MLX5_IB_QPT_DCT:
		ret = (!attr->srq || !attr->recv_cq) ? -EINVAL : 0;
		break;
	case MLX5_IB_QPT_DCI:
		ret = (attr->cap.max_recv_wr || attr->cap.max_recv_sge) ?
			      -EINVAL :
			      0;
		break;
	case IB_QPT_RAW_PACKET:
		ret = (attr->rwq_ind_tbl && attr->send_cq) ? -EINVAL : 0;
		break;
	default:
		break;
	}

	if (ret)
		mlx5_ib_dbg(dev, "QP type %d has wrong attributes\n", qp->type);

	return ret;
}

static int get_qp_uidx(struct mlx5_ib_qp *qp,
		       struct mlx5_create_qp_params *params)
{
	struct mlx5_ib_create_qp *ucmd = params->ucmd;
	struct ib_udata *udata = params->udata;
	struct mlx5_ib_ucontext *ucontext = rdma_udata_to_drv_context(
		udata, struct mlx5_ib_ucontext, ibucontext);

	if (params->is_rss_raw)
		return 0;

	return get_qp_user_index(ucontext, ucmd, sizeof(*ucmd), &params->uidx);
}

static int mlx5_ib_destroy_dct(struct mlx5_ib_qp *mqp)
{
	struct mlx5_ib_dev *dev = to_mdev(mqp->ibqp.device);

	if (mqp->state == IB_QPS_RTR) {
		int err;

		err = mlx5_core_destroy_dct(dev, &mqp->dct.mdct);
		if (err) {
			mlx5_ib_warn(dev, "failed to destroy DCT %d\n", err);
			return err;
		}
	}

	kfree(mqp->dct.in);
	return 0;
}

static int check_ucmd_data(struct mlx5_ib_dev *dev,
			   struct mlx5_create_qp_params *params)
{
	struct ib_udata *udata = params->udata;
	size_t size, last;
	int ret;

	if (params->is_rss_raw)
		/*
		 * These QPs don't have "reserved" field in their
		 * create_qp input struct, so their data is always valid.
		 */
		last = sizeof(struct mlx5_ib_create_qp_rss);
	else
		last = offsetof(struct mlx5_ib_create_qp, reserved);

	if (udata->inlen <= last)
		return 0;

	/*
	 * User provides different create_qp structures based on the
	 * flow and we need to know if he cleared memory after our
	 * struct create_qp ends.
	 */
	size = udata->inlen - last;
	ret = ib_is_udata_cleared(params->udata, last, size);
	if (!ret)
		mlx5_ib_dbg(
			dev,
			"udata is not cleared, inlen = %zu, ucmd = %zu, last = %zu, size = %zu\n",
			udata->inlen, params->ucmd_size, last, size);
	return ret ? 0 : -EINVAL;
}

int mlx5_ib_create_qp(struct ib_qp *ibqp, struct ib_qp_init_attr *attr,
		      struct ib_udata *udata)
{
	struct mlx5_create_qp_params params = {};
	struct mlx5_ib_dev *dev = to_mdev(ibqp->device);
	struct mlx5_ib_qp *qp = to_mqp(ibqp);
	struct ib_pd *pd = ibqp->pd;
	enum ib_qp_type type;
	int err;

	err = check_qp_type(dev, attr, &type);
	if (err)
		return err;

	err = check_valid_flow(dev, pd, attr, udata);
	if (err)
		return err;

	params.udata = udata;
	params.uidx = MLX5_IB_DEFAULT_UIDX;
	params.attr = attr;
	params.is_rss_raw = !!attr->rwq_ind_tbl;

	if (udata) {
		err = process_udata_size(dev, &params);
		if (err)
			return err;

		err = check_ucmd_data(dev, &params);
		if (err)
			return err;

		params.ucmd = kzalloc(params.ucmd_size, GFP_KERNEL);
		if (!params.ucmd)
			return -ENOMEM;

		err = ib_copy_from_udata(params.ucmd, udata, params.inlen);
		if (err)
			goto free_ucmd;
	}

	mutex_init(&qp->mutex);
	qp->type = type;
	if (udata) {
		err = process_vendor_flags(dev, qp, params.ucmd, attr);
		if (err)
			goto free_ucmd;

		err = get_qp_uidx(qp, &params);
		if (err)
			goto free_ucmd;
	}
	err = process_create_flags(dev, qp, attr);
	if (err)
		goto free_ucmd;

	err = check_qp_attr(dev, qp, attr);
	if (err)
		goto free_ucmd;

	err = create_qp(dev, pd, qp, &params);
	if (err)
		goto free_ucmd;

	kfree(params.ucmd);
	params.ucmd = NULL;

	if (udata)
		/*
		 * It is safe to copy response for all user create QP flows,
		 * including MLX5_IB_QPT_DCT, which doesn't need it.
		 * In that case, resp will be filled with zeros.
		 */
		err = ib_copy_to_udata(udata, &params.resp, params.outlen);
	if (err)
		goto destroy_qp;

	return 0;

destroy_qp:
	switch (qp->type) {
	case MLX5_IB_QPT_DCT:
		mlx5_ib_destroy_dct(qp);
		break;
	case IB_QPT_GSI:
		mlx5_ib_destroy_gsi(qp);
		break;
	default:
		destroy_qp_common(dev, qp, udata);
	}

free_ucmd:
	kfree(params.ucmd);
	return err;
}

int mlx5_ib_destroy_qp(struct ib_qp *qp, struct ib_udata *udata)
{
	struct mlx5_ib_dev *dev = to_mdev(qp->device);
	struct mlx5_ib_qp *mqp = to_mqp(qp);

	if (mqp->type == IB_QPT_GSI)
		return mlx5_ib_destroy_gsi(mqp);

	if (mqp->type == MLX5_IB_QPT_DCT)
		return mlx5_ib_destroy_dct(mqp);

	destroy_qp_common(dev, mqp, udata);
	return 0;
}

static int set_qpc_atomic_flags(struct mlx5_ib_qp *qp,
				const struct ib_qp_attr *attr, int attr_mask,
				void *qpc)
{
	struct mlx5_ib_dev *dev = to_mdev(qp->ibqp.device);
	u8 dest_rd_atomic;
	u32 access_flags;

	if (attr_mask & IB_QP_MAX_DEST_RD_ATOMIC)
		dest_rd_atomic = attr->max_dest_rd_atomic;
	else
		dest_rd_atomic = qp->trans_qp.resp_depth;

	if (attr_mask & IB_QP_ACCESS_FLAGS)
		access_flags = attr->qp_access_flags;
	else
		access_flags = qp->trans_qp.atomic_rd_en;

	if (!dest_rd_atomic)
		access_flags &= IB_ACCESS_REMOTE_WRITE;

	MLX5_SET(qpc, qpc, rre, !!(access_flags & IB_ACCESS_REMOTE_READ));

	if (access_flags & IB_ACCESS_REMOTE_ATOMIC) {
		int atomic_mode;

		atomic_mode = get_atomic_mode(dev, qp->type);
		if (atomic_mode < 0)
			return -EOPNOTSUPP;

		MLX5_SET(qpc, qpc, rae, 1);
		MLX5_SET(qpc, qpc, atomic_mode, atomic_mode);
	}

	MLX5_SET(qpc, qpc, rwe, !!(access_flags & IB_ACCESS_REMOTE_WRITE));
	return 0;
}

enum {
	MLX5_PATH_FLAG_FL	= 1 << 0,
	MLX5_PATH_FLAG_FREE_AR	= 1 << 1,
	MLX5_PATH_FLAG_COUNTER	= 1 << 2,
};

static int mlx5_to_ib_rate_map(u8 rate)
{
	static const int rates[] = { IB_RATE_PORT_CURRENT, IB_RATE_56_GBPS,
				     IB_RATE_25_GBPS,	   IB_RATE_100_GBPS,
				     IB_RATE_200_GBPS,	   IB_RATE_50_GBPS,
				     IB_RATE_400_GBPS };

	if (rate < ARRAY_SIZE(rates))
		return rates[rate];

	return rate - MLX5_STAT_RATE_OFFSET;
}

static int ib_to_mlx5_rate_map(u8 rate)
{
	switch (rate) {
	case IB_RATE_PORT_CURRENT:
		return 0;
	case IB_RATE_56_GBPS:
		return 1;
	case IB_RATE_25_GBPS:
		return 2;
	case IB_RATE_100_GBPS:
		return 3;
	case IB_RATE_200_GBPS:
		return 4;
	case IB_RATE_50_GBPS:
		return 5;
	case IB_RATE_400_GBPS:
		return 6;
	default:
		return rate + MLX5_STAT_RATE_OFFSET;
	}

	return 0;
}

static int ib_rate_to_mlx5(struct mlx5_ib_dev *dev, u8 rate)
{
	u32 stat_rate_support;

	if (rate == IB_RATE_PORT_CURRENT)
		return 0;

	if (rate < IB_RATE_2_5_GBPS || rate > IB_RATE_800_GBPS)
		return -EINVAL;

	stat_rate_support = MLX5_CAP_GEN(dev->mdev, stat_rate_support);
	while (rate != IB_RATE_PORT_CURRENT &&
	       !(1 << ib_to_mlx5_rate_map(rate) & stat_rate_support))
		--rate;

	return ib_to_mlx5_rate_map(rate);
}

static int modify_raw_packet_eth_prio(struct mlx5_core_dev *dev,
				      struct mlx5_ib_sq *sq, u8 sl,
				      struct ib_pd *pd)
{
	void *in;
	void *tisc;
	int inlen;
	int err;

	inlen = MLX5_ST_SZ_BYTES(modify_tis_in);
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	MLX5_SET(modify_tis_in, in, bitmask.prio, 1);
	MLX5_SET(modify_tis_in, in, uid, to_mpd(pd)->uid);

	tisc = MLX5_ADDR_OF(modify_tis_in, in, ctx);
	MLX5_SET(tisc, tisc, prio, ((sl & 0x7) << 1));

	err = mlx5_core_modify_tis(dev, sq->tisn, in);

	kvfree(in);

	return err;
}

static int modify_raw_packet_tx_affinity(struct mlx5_core_dev *dev,
					 struct mlx5_ib_sq *sq, u8 tx_affinity,
					 struct ib_pd *pd)
{
	void *in;
	void *tisc;
	int inlen;
	int err;

	inlen = MLX5_ST_SZ_BYTES(modify_tis_in);
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	MLX5_SET(modify_tis_in, in, bitmask.lag_tx_port_affinity, 1);
	MLX5_SET(modify_tis_in, in, uid, to_mpd(pd)->uid);

	tisc = MLX5_ADDR_OF(modify_tis_in, in, ctx);
	MLX5_SET(tisc, tisc, lag_tx_port_affinity, tx_affinity);

	err = mlx5_core_modify_tis(dev, sq->tisn, in);

	kvfree(in);

	return err;
}

static void mlx5_set_path_udp_sport(void *path, const struct rdma_ah_attr *ah,
				    u32 lqpn, u32 rqpn)

{
	u32 fl = ah->grh.flow_label;

	if (!fl)
		fl = rdma_calc_flow_label(lqpn, rqpn);

	MLX5_SET(ads, path, udp_sport, rdma_flow_label_to_udp_sport(fl));
}

static int mlx5_set_path(struct mlx5_ib_dev *dev, struct mlx5_ib_qp *qp,
			 const struct rdma_ah_attr *ah, void *path, u8 port,
			 int attr_mask, u32 path_flags,
			 const struct ib_qp_attr *attr, bool alt)
{
	const struct ib_global_route *grh = rdma_ah_read_grh(ah);
	int err;
	enum ib_gid_type gid_type;
	u8 ah_flags = rdma_ah_get_ah_flags(ah);
	u8 sl = rdma_ah_get_sl(ah);

	if (attr_mask & IB_QP_PKEY_INDEX)
		MLX5_SET(ads, path, pkey_index,
			 alt ? attr->alt_pkey_index : attr->pkey_index);

	if (ah_flags & IB_AH_GRH) {
		const struct ib_port_immutable *immutable;

		immutable = ib_port_immutable_read(&dev->ib_dev, port);
		if (grh->sgid_index >= immutable->gid_tbl_len) {
			pr_err("sgid_index (%u) too large. max is %d\n",
			       grh->sgid_index,
			       immutable->gid_tbl_len);
			return -EINVAL;
		}
	}

	if (ah->type == RDMA_AH_ATTR_TYPE_ROCE) {
		if (!(ah_flags & IB_AH_GRH))
			return -EINVAL;

		ether_addr_copy(MLX5_ADDR_OF(ads, path, rmac_47_32),
				ah->roce.dmac);
		if ((qp->type == IB_QPT_RC ||
		     qp->type == IB_QPT_UC ||
		     qp->type == IB_QPT_XRC_INI ||
		     qp->type == IB_QPT_XRC_TGT) &&
		    (grh->sgid_attr->gid_type == IB_GID_TYPE_ROCE_UDP_ENCAP) &&
		    (attr_mask & IB_QP_DEST_QPN))
			mlx5_set_path_udp_sport(path, ah,
						qp->ibqp.qp_num,
						attr->dest_qp_num);
		MLX5_SET(ads, path, eth_prio, sl & 0x7);
		gid_type = ah->grh.sgid_attr->gid_type;
		if (gid_type == IB_GID_TYPE_ROCE_UDP_ENCAP)
			MLX5_SET(ads, path, dscp, grh->traffic_class >> 2);
	} else {
		MLX5_SET(ads, path, fl, !!(path_flags & MLX5_PATH_FLAG_FL));
		MLX5_SET(ads, path, free_ar,
			 !!(path_flags & MLX5_PATH_FLAG_FREE_AR));
		MLX5_SET(ads, path, rlid, rdma_ah_get_dlid(ah));
		MLX5_SET(ads, path, mlid, rdma_ah_get_path_bits(ah));
		MLX5_SET(ads, path, grh, !!(ah_flags & IB_AH_GRH));
		MLX5_SET(ads, path, sl, sl);
	}

	if (ah_flags & IB_AH_GRH) {
		MLX5_SET(ads, path, src_addr_index, grh->sgid_index);
		MLX5_SET(ads, path, hop_limit, grh->hop_limit);
		MLX5_SET(ads, path, tclass, grh->traffic_class);
		MLX5_SET(ads, path, flow_label, grh->flow_label);
		memcpy(MLX5_ADDR_OF(ads, path, rgid_rip), grh->dgid.raw,
		       sizeof(grh->dgid.raw));
	}

	err = ib_rate_to_mlx5(dev, rdma_ah_get_static_rate(ah));
	if (err < 0)
		return err;
	MLX5_SET(ads, path, stat_rate, err);
	MLX5_SET(ads, path, vhca_port_num, port);

	if (attr_mask & IB_QP_TIMEOUT)
		MLX5_SET(ads, path, ack_timeout,
			 alt ? attr->alt_timeout : attr->timeout);

	if ((qp->type == IB_QPT_RAW_PACKET) && qp->sq.wqe_cnt)
		return modify_raw_packet_eth_prio(dev->mdev,
						  &qp->raw_packet_qp.sq,
						  sl & 0xf, qp->ibqp.pd);

	return 0;
}

static enum mlx5_qp_optpar opt_mask[MLX5_QP_NUM_STATE][MLX5_QP_NUM_STATE][MLX5_QP_ST_MAX] = {
	[MLX5_QP_STATE_INIT] = {
		[MLX5_QP_STATE_INIT] = {
			[MLX5_QP_ST_RC] = MLX5_QP_OPTPAR_RRE		|
					  MLX5_QP_OPTPAR_RAE		|
					  MLX5_QP_OPTPAR_RWE		|
					  MLX5_QP_OPTPAR_PKEY_INDEX	|
					  MLX5_QP_OPTPAR_PRI_PORT	|
					  MLX5_QP_OPTPAR_LAG_TX_AFF,
			[MLX5_QP_ST_UC] = MLX5_QP_OPTPAR_RWE		|
					  MLX5_QP_OPTPAR_PKEY_INDEX	|
					  MLX5_QP_OPTPAR_PRI_PORT	|
					  MLX5_QP_OPTPAR_LAG_TX_AFF,
			[MLX5_QP_ST_UD] = MLX5_QP_OPTPAR_PKEY_INDEX	|
					  MLX5_QP_OPTPAR_Q_KEY		|
					  MLX5_QP_OPTPAR_PRI_PORT,
			[MLX5_QP_ST_XRC] = MLX5_QP_OPTPAR_RRE		|
					  MLX5_QP_OPTPAR_RAE		|
					  MLX5_QP_OPTPAR_RWE		|
					  MLX5_QP_OPTPAR_PKEY_INDEX	|
					  MLX5_QP_OPTPAR_PRI_PORT	|
					  MLX5_QP_OPTPAR_LAG_TX_AFF,
		},
		[MLX5_QP_STATE_RTR] = {
			[MLX5_QP_ST_RC] = MLX5_QP_OPTPAR_ALT_ADDR_PATH  |
					  MLX5_QP_OPTPAR_RRE            |
					  MLX5_QP_OPTPAR_RAE            |
					  MLX5_QP_OPTPAR_RWE            |
					  MLX5_QP_OPTPAR_PKEY_INDEX	|
					  MLX5_QP_OPTPAR_LAG_TX_AFF,
			[MLX5_QP_ST_UC] = MLX5_QP_OPTPAR_ALT_ADDR_PATH  |
					  MLX5_QP_OPTPAR_RWE            |
					  MLX5_QP_OPTPAR_PKEY_INDEX	|
					  MLX5_QP_OPTPAR_LAG_TX_AFF,
			[MLX5_QP_ST_UD] = MLX5_QP_OPTPAR_PKEY_INDEX     |
					  MLX5_QP_OPTPAR_Q_KEY,
			[MLX5_QP_ST_MLX] = MLX5_QP_OPTPAR_PKEY_INDEX	|
					   MLX5_QP_OPTPAR_Q_KEY,
			[MLX5_QP_ST_XRC] = MLX5_QP_OPTPAR_ALT_ADDR_PATH |
					  MLX5_QP_OPTPAR_RRE            |
					  MLX5_QP_OPTPAR_RAE            |
					  MLX5_QP_OPTPAR_RWE            |
					  MLX5_QP_OPTPAR_PKEY_INDEX	|
					  MLX5_QP_OPTPAR_LAG_TX_AFF,
		},
	},
	[MLX5_QP_STATE_RTR] = {
		[MLX5_QP_STATE_RTS] = {
			[MLX5_QP_ST_RC] = MLX5_QP_OPTPAR_ALT_ADDR_PATH	|
					  MLX5_QP_OPTPAR_RRE		|
					  MLX5_QP_OPTPAR_RAE		|
					  MLX5_QP_OPTPAR_RWE		|
					  MLX5_QP_OPTPAR_PM_STATE	|
					  MLX5_QP_OPTPAR_RNR_TIMEOUT,
			[MLX5_QP_ST_UC] = MLX5_QP_OPTPAR_ALT_ADDR_PATH	|
					  MLX5_QP_OPTPAR_RWE		|
					  MLX5_QP_OPTPAR_PM_STATE,
			[MLX5_QP_ST_UD] = MLX5_QP_OPTPAR_Q_KEY,
			[MLX5_QP_ST_XRC] = MLX5_QP_OPTPAR_ALT_ADDR_PATH	|
					  MLX5_QP_OPTPAR_RRE		|
					  MLX5_QP_OPTPAR_RAE		|
					  MLX5_QP_OPTPAR_RWE		|
					  MLX5_QP_OPTPAR_PM_STATE	|
					  MLX5_QP_OPTPAR_RNR_TIMEOUT,
		},
	},
	[MLX5_QP_STATE_RTS] = {
		[MLX5_QP_STATE_RTS] = {
			[MLX5_QP_ST_RC] = MLX5_QP_OPTPAR_RRE		|
					  MLX5_QP_OPTPAR_RAE		|
					  MLX5_QP_OPTPAR_RWE		|
					  MLX5_QP_OPTPAR_RNR_TIMEOUT	|
					  MLX5_QP_OPTPAR_PM_STATE	|
					  MLX5_QP_OPTPAR_ALT_ADDR_PATH,
			[MLX5_QP_ST_UC] = MLX5_QP_OPTPAR_RWE		|
					  MLX5_QP_OPTPAR_PM_STATE	|
					  MLX5_QP_OPTPAR_ALT_ADDR_PATH,
			[MLX5_QP_ST_UD] = MLX5_QP_OPTPAR_Q_KEY		|
					  MLX5_QP_OPTPAR_SRQN		|
					  MLX5_QP_OPTPAR_CQN_RCV,
			[MLX5_QP_ST_XRC] = MLX5_QP_OPTPAR_RRE		|
					  MLX5_QP_OPTPAR_RAE		|
					  MLX5_QP_OPTPAR_RWE		|
					  MLX5_QP_OPTPAR_RNR_TIMEOUT	|
					  MLX5_QP_OPTPAR_PM_STATE	|
					  MLX5_QP_OPTPAR_ALT_ADDR_PATH,
		},
	},
	[MLX5_QP_STATE_SQER] = {
		[MLX5_QP_STATE_RTS] = {
			[MLX5_QP_ST_UD]	 = MLX5_QP_OPTPAR_Q_KEY,
			[MLX5_QP_ST_MLX] = MLX5_QP_OPTPAR_Q_KEY,
			[MLX5_QP_ST_UC]	 = MLX5_QP_OPTPAR_RWE,
			[MLX5_QP_ST_RC]	 = MLX5_QP_OPTPAR_RNR_TIMEOUT	|
					   MLX5_QP_OPTPAR_RWE		|
					   MLX5_QP_OPTPAR_RAE		|
					   MLX5_QP_OPTPAR_RRE,
			[MLX5_QP_ST_XRC]  = MLX5_QP_OPTPAR_RNR_TIMEOUT	|
					   MLX5_QP_OPTPAR_RWE		|
					   MLX5_QP_OPTPAR_RAE		|
					   MLX5_QP_OPTPAR_RRE,
		},
	},
	[MLX5_QP_STATE_SQD] = {
		[MLX5_QP_STATE_RTS] = {
			[MLX5_QP_ST_UD] = MLX5_QP_OPTPAR_Q_KEY,
			[MLX5_QP_ST_MLX] = MLX5_QP_OPTPAR_Q_KEY,
			[MLX5_QP_ST_UC] = MLX5_QP_OPTPAR_RWE,
			[MLX5_QP_ST_RC] = MLX5_QP_OPTPAR_RNR_TIMEOUT	|
					  MLX5_QP_OPTPAR_RWE		|
					  MLX5_QP_OPTPAR_RAE		|
					  MLX5_QP_OPTPAR_RRE,
		},
	},
};

static int ib_nr_to_mlx5_nr(int ib_mask)
{
	switch (ib_mask) {
	case IB_QP_STATE:
		return 0;
	case IB_QP_CUR_STATE:
		return 0;
	case IB_QP_EN_SQD_ASYNC_NOTIFY:
		return 0;
	case IB_QP_ACCESS_FLAGS:
		return MLX5_QP_OPTPAR_RWE | MLX5_QP_OPTPAR_RRE |
			MLX5_QP_OPTPAR_RAE;
	case IB_QP_PKEY_INDEX:
		return MLX5_QP_OPTPAR_PKEY_INDEX;
	case IB_QP_PORT:
		return MLX5_QP_OPTPAR_PRI_PORT;
	case IB_QP_QKEY:
		return MLX5_QP_OPTPAR_Q_KEY;
	case IB_QP_AV:
		return MLX5_QP_OPTPAR_PRIMARY_ADDR_PATH |
			MLX5_QP_OPTPAR_PRI_PORT;
	case IB_QP_PATH_MTU:
		return 0;
	case IB_QP_TIMEOUT:
		return MLX5_QP_OPTPAR_ACK_TIMEOUT;
	case IB_QP_RETRY_CNT:
		return MLX5_QP_OPTPAR_RETRY_COUNT;
	case IB_QP_RNR_RETRY:
		return MLX5_QP_OPTPAR_RNR_RETRY;
	case IB_QP_RQ_PSN:
		return 0;
	case IB_QP_MAX_QP_RD_ATOMIC:
		return MLX5_QP_OPTPAR_SRA_MAX;
	case IB_QP_ALT_PATH:
		return MLX5_QP_OPTPAR_ALT_ADDR_PATH;
	case IB_QP_MIN_RNR_TIMER:
		return MLX5_QP_OPTPAR_RNR_TIMEOUT;
	case IB_QP_SQ_PSN:
		return 0;
	case IB_QP_MAX_DEST_RD_ATOMIC:
		return MLX5_QP_OPTPAR_RRA_MAX | MLX5_QP_OPTPAR_RWE |
			MLX5_QP_OPTPAR_RRE | MLX5_QP_OPTPAR_RAE;
	case IB_QP_PATH_MIG_STATE:
		return MLX5_QP_OPTPAR_PM_STATE;
	case IB_QP_CAP:
		return 0;
	case IB_QP_DEST_QPN:
		return 0;
	}
	return 0;
}

static int ib_mask_to_mlx5_opt(int ib_mask)
{
	int result = 0;
	int i;

	for (i = 0; i < 8 * sizeof(int); i++) {
		if ((1 << i) & ib_mask)
			result |= ib_nr_to_mlx5_nr(1 << i);
	}

	return result;
}

static int modify_raw_packet_qp_rq(
	struct mlx5_ib_dev *dev, struct mlx5_ib_rq *rq, int new_state,
	const struct mlx5_modify_raw_qp_param *raw_qp_param, struct ib_pd *pd)
{
	void *in;
	void *rqc;
	int inlen;
	int err;

	inlen = MLX5_ST_SZ_BYTES(modify_rq_in);
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	MLX5_SET(modify_rq_in, in, rq_state, rq->state);
	MLX5_SET(modify_rq_in, in, uid, to_mpd(pd)->uid);

	rqc = MLX5_ADDR_OF(modify_rq_in, in, ctx);
	MLX5_SET(rqc, rqc, state, new_state);

	if (raw_qp_param->set_mask & MLX5_RAW_QP_MOD_SET_RQ_Q_CTR_ID) {
		if (MLX5_CAP_GEN(dev->mdev, modify_rq_counter_set_id)) {
			MLX5_SET64(modify_rq_in, in, modify_bitmask,
				   MLX5_MODIFY_RQ_IN_MODIFY_BITMASK_RQ_COUNTER_SET_ID);
			MLX5_SET(rqc, rqc, counter_set_id, raw_qp_param->rq_q_ctr_id);
		} else
			dev_info_once(
				&dev->ib_dev.dev,
				"RAW PACKET QP counters are not supported on current FW\n");
	}

	err = mlx5_core_modify_rq(dev->mdev, rq->base.mqp.qpn, in);
	if (err)
		goto out;

	rq->state = new_state;

out:
	kvfree(in);
	return err;
}

static int modify_raw_packet_qp_sq(
	struct mlx5_core_dev *dev, struct mlx5_ib_sq *sq, int new_state,
	const struct mlx5_modify_raw_qp_param *raw_qp_param, struct ib_pd *pd)
{
	struct mlx5_ib_qp *ibqp = sq->base.container_mibqp;
	struct mlx5_rate_limit old_rl = ibqp->rl;
	struct mlx5_rate_limit new_rl = old_rl;
	bool new_rate_added = false;
	u16 rl_index = 0;
	void *in;
	void *sqc;
	int inlen;
	int err;

	inlen = MLX5_ST_SZ_BYTES(modify_sq_in);
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	MLX5_SET(modify_sq_in, in, uid, to_mpd(pd)->uid);
	MLX5_SET(modify_sq_in, in, sq_state, sq->state);

	sqc = MLX5_ADDR_OF(modify_sq_in, in, ctx);
	MLX5_SET(sqc, sqc, state, new_state);

	if (raw_qp_param->set_mask & MLX5_RAW_QP_RATE_LIMIT) {
		if (new_state != MLX5_SQC_STATE_RDY)
			pr_warn("%s: Rate limit can only be changed when SQ is moving to RDY\n",
				__func__);
		else
			new_rl = raw_qp_param->rl;
	}

	if (!mlx5_rl_are_equal(&old_rl, &new_rl)) {
		if (new_rl.rate) {
			err = mlx5_rl_add_rate(dev, &rl_index, &new_rl);
			if (err) {
				pr_err("Failed configuring rate limit(err %d): \
				       rate %u, max_burst_sz %u, typical_pkt_sz %u\n",
				       err, new_rl.rate, new_rl.max_burst_sz,
				       new_rl.typical_pkt_sz);

				goto out;
			}
			new_rate_added = true;
		}

		MLX5_SET64(modify_sq_in, in, modify_bitmask, 1);
		/* index 0 means no limit */
		MLX5_SET(sqc, sqc, packet_pacing_rate_limit_index, rl_index);
	}

	err = mlx5_core_modify_sq(dev, sq->base.mqp.qpn, in);
	if (err) {
		/* Remove new rate from table if failed */
		if (new_rate_added)
			mlx5_rl_remove_rate(dev, &new_rl);
		goto out;
	}

	/* Only remove the old rate after new rate was set */
	if ((old_rl.rate && !mlx5_rl_are_equal(&old_rl, &new_rl)) ||
	    (new_state != MLX5_SQC_STATE_RDY)) {
		mlx5_rl_remove_rate(dev, &old_rl);
		if (new_state != MLX5_SQC_STATE_RDY)
			memset(&new_rl, 0, sizeof(new_rl));
	}

	ibqp->rl = new_rl;
	sq->state = new_state;

out:
	kvfree(in);
	return err;
}

static int modify_raw_packet_qp(struct mlx5_ib_dev *dev, struct mlx5_ib_qp *qp,
				const struct mlx5_modify_raw_qp_param *raw_qp_param,
				u8 tx_affinity)
{
	struct mlx5_ib_raw_packet_qp *raw_packet_qp = &qp->raw_packet_qp;
	struct mlx5_ib_rq *rq = &raw_packet_qp->rq;
	struct mlx5_ib_sq *sq = &raw_packet_qp->sq;
	int modify_rq = !!qp->rq.wqe_cnt;
	int modify_sq = !!qp->sq.wqe_cnt;
	int rq_state;
	int sq_state;
	int err;

	switch (raw_qp_param->operation) {
	case MLX5_CMD_OP_RST2INIT_QP:
		rq_state = MLX5_RQC_STATE_RDY;
		sq_state = MLX5_SQC_STATE_RST;
		break;
	case MLX5_CMD_OP_2ERR_QP:
		rq_state = MLX5_RQC_STATE_ERR;
		sq_state = MLX5_SQC_STATE_ERR;
		break;
	case MLX5_CMD_OP_2RST_QP:
		rq_state = MLX5_RQC_STATE_RST;
		sq_state = MLX5_SQC_STATE_RST;
		break;
	case MLX5_CMD_OP_RTR2RTS_QP:
	case MLX5_CMD_OP_RTS2RTS_QP:
		if (raw_qp_param->set_mask & ~MLX5_RAW_QP_RATE_LIMIT)
			return -EINVAL;

		modify_rq = 0;
		sq_state = MLX5_SQC_STATE_RDY;
		break;
	case MLX5_CMD_OP_INIT2INIT_QP:
	case MLX5_CMD_OP_INIT2RTR_QP:
		if (raw_qp_param->set_mask)
			return -EINVAL;
		else
			return 0;
	default:
		WARN_ON(1);
		return -EINVAL;
	}

	if (modify_rq) {
		err =  modify_raw_packet_qp_rq(dev, rq, rq_state, raw_qp_param,
					       qp->ibqp.pd);
		if (err)
			return err;
	}

	if (modify_sq) {
		struct mlx5_flow_handle *flow_rule;

		if (tx_affinity) {
			err = modify_raw_packet_tx_affinity(dev->mdev, sq,
							    tx_affinity,
							    qp->ibqp.pd);
			if (err)
				return err;
		}

		flow_rule = create_flow_rule_vport_sq(dev, sq,
						      raw_qp_param->port);
		if (IS_ERR(flow_rule))
			return PTR_ERR(flow_rule);

		err = modify_raw_packet_qp_sq(dev->mdev, sq, sq_state,
					      raw_qp_param, qp->ibqp.pd);
		if (err) {
			if (flow_rule)
				mlx5_del_flow_rules(flow_rule);
			return err;
		}

		if (flow_rule) {
			destroy_flow_rule_vport_sq(sq);
			sq->flow_rule = flow_rule;
		}

		return err;
	}

	return 0;
}

static unsigned int get_tx_affinity_rr(struct mlx5_ib_dev *dev,
				       struct ib_udata *udata)
{
	struct mlx5_ib_ucontext *ucontext = rdma_udata_to_drv_context(
		udata, struct mlx5_ib_ucontext, ibucontext);
	u8 port_num = mlx5_core_native_port_num(dev->mdev) - 1;
	atomic_t *tx_port_affinity;

	if (ucontext)
		tx_port_affinity = &ucontext->tx_port_affinity;
	else
		tx_port_affinity = &dev->port[port_num].roce.tx_port_affinity;

	return (unsigned int)atomic_add_return(1, tx_port_affinity) %
		(dev->lag_active ? dev->lag_ports : MLX5_CAP_GEN(dev->mdev, num_lag_ports)) + 1;
}

static bool qp_supports_affinity(struct mlx5_ib_qp *qp)
{
	if ((qp->type == IB_QPT_RC) || (qp->type == IB_QPT_UD) ||
	    (qp->type == IB_QPT_UC) || (qp->type == IB_QPT_RAW_PACKET) ||
	    (qp->type == IB_QPT_XRC_INI) || (qp->type == IB_QPT_XRC_TGT) ||
	    (qp->type == MLX5_IB_QPT_DCI))
		return true;
	return false;
}

static unsigned int get_tx_affinity(struct ib_qp *qp,
				    const struct ib_qp_attr *attr,
				    int attr_mask, u8 init,
				    struct ib_udata *udata)
{
	struct mlx5_ib_ucontext *ucontext = rdma_udata_to_drv_context(
		udata, struct mlx5_ib_ucontext, ibucontext);
	struct mlx5_ib_dev *dev = to_mdev(qp->device);
	struct mlx5_ib_qp *mqp = to_mqp(qp);
	struct mlx5_ib_qp_base *qp_base;
	unsigned int tx_affinity;

	if (!(mlx5_ib_lag_should_assign_affinity(dev) &&
	      qp_supports_affinity(mqp)))
		return 0;

	if (mqp->flags & MLX5_IB_QP_CREATE_SQPN_QP1)
		tx_affinity = mqp->gsi_lag_port;
	else if (init)
		tx_affinity = get_tx_affinity_rr(dev, udata);
	else if ((attr_mask & IB_QP_AV) && attr->xmit_slave)
		tx_affinity =
			mlx5_lag_get_slave_port(dev->mdev, attr->xmit_slave);
	else
		return 0;

	qp_base = &mqp->trans_qp.base;
	if (ucontext)
		mlx5_ib_dbg(dev, "Set tx affinity 0x%x to qpn 0x%x ucontext %p\n",
			    tx_affinity, qp_base->mqp.qpn, ucontext);
	else
		mlx5_ib_dbg(dev, "Set tx affinity 0x%x to qpn 0x%x\n",
			    tx_affinity, qp_base->mqp.qpn);
	return tx_affinity;
}

static int __mlx5_ib_qp_set_raw_qp_counter(struct mlx5_ib_qp *qp, u32 set_id,
					   struct mlx5_core_dev *mdev)
{
	struct mlx5_ib_raw_packet_qp *raw_packet_qp = &qp->raw_packet_qp;
	struct mlx5_ib_rq *rq = &raw_packet_qp->rq;
	u32 in[MLX5_ST_SZ_DW(modify_rq_in)] = {};
	void *rqc;

	if (!qp->rq.wqe_cnt)
		return 0;

	MLX5_SET(modify_rq_in, in, rq_state, rq->state);
	MLX5_SET(modify_rq_in, in, uid, to_mpd(qp->ibqp.pd)->uid);

	rqc = MLX5_ADDR_OF(modify_rq_in, in, ctx);
	MLX5_SET(rqc, rqc, state, MLX5_RQC_STATE_RDY);

	MLX5_SET64(modify_rq_in, in, modify_bitmask,
		   MLX5_MODIFY_RQ_IN_MODIFY_BITMASK_RQ_COUNTER_SET_ID);
	MLX5_SET(rqc, rqc, counter_set_id, set_id);

	return mlx5_core_modify_rq(mdev, rq->base.mqp.qpn, in);
}

static int __mlx5_ib_qp_set_counter(struct ib_qp *qp,
				    struct rdma_counter *counter)
{
	struct mlx5_ib_dev *dev = to_mdev(qp->device);
	u32 in[MLX5_ST_SZ_DW(rts2rts_qp_in)] = {};
	struct mlx5_ib_qp *mqp = to_mqp(qp);
	struct mlx5_ib_qp_base *base;
	u32 set_id;
	u32 *qpc;

	if (counter)
		set_id = counter->id;
	else
		set_id = mlx5_ib_get_counters_id(dev, mqp->port - 1);

	if (mqp->type == IB_QPT_RAW_PACKET)
		return __mlx5_ib_qp_set_raw_qp_counter(mqp, set_id, dev->mdev);

	base = &mqp->trans_qp.base;
	MLX5_SET(rts2rts_qp_in, in, opcode, MLX5_CMD_OP_RTS2RTS_QP);
	MLX5_SET(rts2rts_qp_in, in, qpn, base->mqp.qpn);
	MLX5_SET(rts2rts_qp_in, in, uid, base->mqp.uid);
	MLX5_SET(rts2rts_qp_in, in, opt_param_mask,
		 MLX5_QP_OPTPAR_COUNTER_SET_ID);

	qpc = MLX5_ADDR_OF(rts2rts_qp_in, in, qpc);
	MLX5_SET(qpc, qpc, counter_set_id, set_id);
	return mlx5_cmd_exec_in(dev->mdev, rts2rts_qp, in);
}

static int __mlx5_ib_modify_qp(struct ib_qp *ibqp,
			       const struct ib_qp_attr *attr, int attr_mask,
			       enum ib_qp_state cur_state,
			       enum ib_qp_state new_state,
			       const struct mlx5_ib_modify_qp *ucmd,
			       struct mlx5_ib_modify_qp_resp *resp,
			       struct ib_udata *udata)
{
	static const u16 optab[MLX5_QP_NUM_STATE][MLX5_QP_NUM_STATE] = {
		[MLX5_QP_STATE_RST] = {
			[MLX5_QP_STATE_RST]	= MLX5_CMD_OP_2RST_QP,
			[MLX5_QP_STATE_ERR]	= MLX5_CMD_OP_2ERR_QP,
			[MLX5_QP_STATE_INIT]	= MLX5_CMD_OP_RST2INIT_QP,
		},
		[MLX5_QP_STATE_INIT]  = {
			[MLX5_QP_STATE_RST]	= MLX5_CMD_OP_2RST_QP,
			[MLX5_QP_STATE_ERR]	= MLX5_CMD_OP_2ERR_QP,
			[MLX5_QP_STATE_INIT]	= MLX5_CMD_OP_INIT2INIT_QP,
			[MLX5_QP_STATE_RTR]	= MLX5_CMD_OP_INIT2RTR_QP,
		},
		[MLX5_QP_STATE_RTR]   = {
			[MLX5_QP_STATE_RST]	= MLX5_CMD_OP_2RST_QP,
			[MLX5_QP_STATE_ERR]	= MLX5_CMD_OP_2ERR_QP,
			[MLX5_QP_STATE_RTS]	= MLX5_CMD_OP_RTR2RTS_QP,
		},
		[MLX5_QP_STATE_RTS]   = {
			[MLX5_QP_STATE_RST]	= MLX5_CMD_OP_2RST_QP,
			[MLX5_QP_STATE_ERR]	= MLX5_CMD_OP_2ERR_QP,
			[MLX5_QP_STATE_RTS]	= MLX5_CMD_OP_RTS2RTS_QP,
		},
		[MLX5_QP_STATE_SQD] = {
			[MLX5_QP_STATE_RST]	= MLX5_CMD_OP_2RST_QP,
			[MLX5_QP_STATE_ERR]	= MLX5_CMD_OP_2ERR_QP,
			[MLX5_QP_STATE_RTS]	= MLX5_CMD_OP_SQD_RTS_QP,
		},
		[MLX5_QP_STATE_SQER] = {
			[MLX5_QP_STATE_RST]	= MLX5_CMD_OP_2RST_QP,
			[MLX5_QP_STATE_ERR]	= MLX5_CMD_OP_2ERR_QP,
			[MLX5_QP_STATE_RTS]	= MLX5_CMD_OP_SQERR2RTS_QP,
		},
		[MLX5_QP_STATE_ERR] = {
			[MLX5_QP_STATE_RST]	= MLX5_CMD_OP_2RST_QP,
			[MLX5_QP_STATE_ERR]	= MLX5_CMD_OP_2ERR_QP,
		}
	};

	struct mlx5_ib_dev *dev = to_mdev(ibqp->device);
	struct mlx5_ib_qp *qp = to_mqp(ibqp);
	struct mlx5_ib_qp_base *base = &qp->trans_qp.base;
	struct mlx5_ib_cq *send_cq, *recv_cq;
	struct mlx5_ib_pd *pd;
	enum mlx5_qp_state mlx5_cur, mlx5_new;
	void *qpc, *pri_path, *alt_path;
	enum mlx5_qp_optpar optpar = 0;
	u32 set_id = 0;
	int mlx5_st;
	int err;
	u16 op;
	u8 tx_affinity = 0;

	mlx5_st = to_mlx5_st(qp->type);
	if (mlx5_st < 0)
		return -EINVAL;

	qpc = kzalloc(MLX5_ST_SZ_BYTES(qpc), GFP_KERNEL);
	if (!qpc)
		return -ENOMEM;

	pd = to_mpd(qp->ibqp.pd);
	MLX5_SET(qpc, qpc, st, mlx5_st);

	if (!(attr_mask & IB_QP_PATH_MIG_STATE)) {
		MLX5_SET(qpc, qpc, pm_state, MLX5_QP_PM_MIGRATED);
	} else {
		switch (attr->path_mig_state) {
		case IB_MIG_MIGRATED:
			MLX5_SET(qpc, qpc, pm_state, MLX5_QP_PM_MIGRATED);
			break;
		case IB_MIG_REARM:
			MLX5_SET(qpc, qpc, pm_state, MLX5_QP_PM_REARM);
			break;
		case IB_MIG_ARMED:
			MLX5_SET(qpc, qpc, pm_state, MLX5_QP_PM_ARMED);
			break;
		}
	}

	tx_affinity = get_tx_affinity(ibqp, attr, attr_mask,
				      cur_state == IB_QPS_RESET &&
				      new_state == IB_QPS_INIT, udata);

	MLX5_SET(qpc, qpc, lag_tx_port_affinity, tx_affinity);
	if (tx_affinity && new_state == IB_QPS_RTR &&
	    MLX5_CAP_GEN(dev->mdev, init2_lag_tx_port_affinity))
		optpar |= MLX5_QP_OPTPAR_LAG_TX_AFF;

	if (is_sqp(qp->type)) {
		MLX5_SET(qpc, qpc, mtu, IB_MTU_256);
		MLX5_SET(qpc, qpc, log_msg_max, 8);
	} else if ((qp->type == IB_QPT_UD &&
		    !(qp->flags & IB_QP_CREATE_SOURCE_QPN)) ||
		   qp->type == MLX5_IB_QPT_REG_UMR) {
		MLX5_SET(qpc, qpc, mtu, IB_MTU_4096);
		MLX5_SET(qpc, qpc, log_msg_max, 12);
	} else if (attr_mask & IB_QP_PATH_MTU) {
		if (attr->path_mtu < IB_MTU_256 ||
		    attr->path_mtu > IB_MTU_4096) {
			mlx5_ib_warn(dev, "invalid mtu %d\n", attr->path_mtu);
			err = -EINVAL;
			goto out;
		}
		MLX5_SET(qpc, qpc, mtu, attr->path_mtu);
		MLX5_SET(qpc, qpc, log_msg_max,
			 MLX5_CAP_GEN(dev->mdev, log_max_msg));
	}

	if (attr_mask & IB_QP_DEST_QPN)
		MLX5_SET(qpc, qpc, remote_qpn, attr->dest_qp_num);

	pri_path = MLX5_ADDR_OF(qpc, qpc, primary_address_path);
	alt_path = MLX5_ADDR_OF(qpc, qpc, secondary_address_path);

	if (attr_mask & IB_QP_PKEY_INDEX)
		MLX5_SET(ads, pri_path, pkey_index, attr->pkey_index);

	/* todo implement counter_index functionality */

	if (is_sqp(qp->type))
		MLX5_SET(ads, pri_path, vhca_port_num, qp->port);

	if (attr_mask & IB_QP_PORT)
		MLX5_SET(ads, pri_path, vhca_port_num, attr->port_num);

	if (attr_mask & IB_QP_AV) {
		err = mlx5_set_path(dev, qp, &attr->ah_attr, pri_path,
				    attr_mask & IB_QP_PORT ? attr->port_num :
							     qp->port,
				    attr_mask, 0, attr, false);
		if (err)
			goto out;
	}

	if (attr_mask & IB_QP_TIMEOUT)
		MLX5_SET(ads, pri_path, ack_timeout, attr->timeout);

	if (attr_mask & IB_QP_ALT_PATH) {
		err = mlx5_set_path(dev, qp, &attr->alt_ah_attr, alt_path,
				    attr->alt_port_num,
				    attr_mask | IB_QP_PKEY_INDEX |
					    IB_QP_TIMEOUT,
				    0, attr, true);
		if (err)
			goto out;
	}

	get_cqs(qp->type, qp->ibqp.send_cq, qp->ibqp.recv_cq,
		&send_cq, &recv_cq);

	MLX5_SET(qpc, qpc, pd, pd ? pd->pdn : to_mpd(dev->devr.p0)->pdn);
	if (send_cq)
		MLX5_SET(qpc, qpc, cqn_snd, send_cq->mcq.cqn);
	if (recv_cq)
		MLX5_SET(qpc, qpc, cqn_rcv, recv_cq->mcq.cqn);

	MLX5_SET(qpc, qpc, log_ack_req_freq, MLX5_IB_ACK_REQ_FREQ);

	if (attr_mask & IB_QP_RNR_RETRY)
		MLX5_SET(qpc, qpc, rnr_retry, attr->rnr_retry);

	if (attr_mask & IB_QP_RETRY_CNT)
		MLX5_SET(qpc, qpc, retry_count, attr->retry_cnt);

	if (attr_mask & IB_QP_MAX_QP_RD_ATOMIC && attr->max_rd_atomic)
		MLX5_SET(qpc, qpc, log_sra_max, ilog2(attr->max_rd_atomic));

	if (attr_mask & IB_QP_SQ_PSN)
		MLX5_SET(qpc, qpc, next_send_psn, attr->sq_psn);

	if (attr_mask & IB_QP_MAX_DEST_RD_ATOMIC && attr->max_dest_rd_atomic)
		MLX5_SET(qpc, qpc, log_rra_max,
			 ilog2(attr->max_dest_rd_atomic));

	if (attr_mask & (IB_QP_ACCESS_FLAGS | IB_QP_MAX_DEST_RD_ATOMIC)) {
		err = set_qpc_atomic_flags(qp, attr, attr_mask, qpc);
		if (err)
			goto out;
	}

	if (attr_mask & IB_QP_MIN_RNR_TIMER)
		MLX5_SET(qpc, qpc, min_rnr_nak, attr->min_rnr_timer);

	if (attr_mask & IB_QP_RQ_PSN)
		MLX5_SET(qpc, qpc, next_rcv_psn, attr->rq_psn);

	if (attr_mask & IB_QP_QKEY)
		MLX5_SET(qpc, qpc, q_key, attr->qkey);

	if (qp->rq.wqe_cnt && cur_state == IB_QPS_RESET && new_state == IB_QPS_INIT)
		MLX5_SET64(qpc, qpc, dbr_addr, qp->db.dma);

	if (cur_state == IB_QPS_RESET && new_state == IB_QPS_INIT) {
		u8 port_num = (attr_mask & IB_QP_PORT ? attr->port_num :
			       qp->port) - 1;

		/* Underlay port should be used - index 0 function per port */
		if (qp->flags & IB_QP_CREATE_SOURCE_QPN)
			port_num = 0;

		if (ibqp->counter)
			set_id = ibqp->counter->id;
		else
			set_id = mlx5_ib_get_counters_id(dev, port_num);
		MLX5_SET(qpc, qpc, counter_set_id, set_id);
	}

	if (!ibqp->uobject && cur_state == IB_QPS_RESET && new_state == IB_QPS_INIT)
		MLX5_SET(qpc, qpc, rlky, 1);

	if (qp->flags & MLX5_IB_QP_CREATE_SQPN_QP1)
		MLX5_SET(qpc, qpc, deth_sqpn, 1);

	mlx5_cur = to_mlx5_state(cur_state);
	mlx5_new = to_mlx5_state(new_state);

	if (mlx5_cur >= MLX5_QP_NUM_STATE || mlx5_new >= MLX5_QP_NUM_STATE ||
	    !optab[mlx5_cur][mlx5_new]) {
		err = -EINVAL;
		goto out;
	}

	op = optab[mlx5_cur][mlx5_new];
	optpar |= ib_mask_to_mlx5_opt(attr_mask);
	optpar &= opt_mask[mlx5_cur][mlx5_new][mlx5_st];

	if (qp->type == IB_QPT_RAW_PACKET ||
	    qp->flags & IB_QP_CREATE_SOURCE_QPN) {
		struct mlx5_modify_raw_qp_param raw_qp_param = {};

		raw_qp_param.operation = op;
		if (cur_state == IB_QPS_RESET && new_state == IB_QPS_INIT) {
			raw_qp_param.rq_q_ctr_id = set_id;
			raw_qp_param.set_mask |= MLX5_RAW_QP_MOD_SET_RQ_Q_CTR_ID;
		}

		if (attr_mask & IB_QP_PORT)
			raw_qp_param.port = attr->port_num;

		if (attr_mask & IB_QP_RATE_LIMIT) {
			raw_qp_param.rl.rate = attr->rate_limit;

			if (ucmd->burst_info.max_burst_sz) {
				if (attr->rate_limit &&
				    MLX5_CAP_QOS(dev->mdev, packet_pacing_burst_bound)) {
					raw_qp_param.rl.max_burst_sz =
						ucmd->burst_info.max_burst_sz;
				} else {
					err = -EINVAL;
					goto out;
				}
			}

			if (ucmd->burst_info.typical_pkt_sz) {
				if (attr->rate_limit &&
				    MLX5_CAP_QOS(dev->mdev, packet_pacing_typical_size)) {
					raw_qp_param.rl.typical_pkt_sz =
						ucmd->burst_info.typical_pkt_sz;
				} else {
					err = -EINVAL;
					goto out;
				}
			}

			raw_qp_param.set_mask |= MLX5_RAW_QP_RATE_LIMIT;
		}

		err = modify_raw_packet_qp(dev, qp, &raw_qp_param, tx_affinity);
	} else {
		if (udata) {
			/* For the kernel flows, the resp will stay zero */
			resp->ece_options =
				MLX5_CAP_GEN(dev->mdev, ece_support) ?
					ucmd->ece_options : 0;
			resp->response_length = sizeof(*resp);
		}
		err = mlx5_core_qp_modify(dev, op, optpar, qpc, &base->mqp,
					  &resp->ece_options);
	}

	if (err)
		goto out;

	qp->state = new_state;

	if (attr_mask & IB_QP_ACCESS_FLAGS)
		qp->trans_qp.atomic_rd_en = attr->qp_access_flags;
	if (attr_mask & IB_QP_MAX_DEST_RD_ATOMIC)
		qp->trans_qp.resp_depth = attr->max_dest_rd_atomic;
	if (attr_mask & IB_QP_PORT)
		qp->port = attr->port_num;
	if (attr_mask & IB_QP_ALT_PATH)
		qp->trans_qp.alt_port = attr->alt_port_num;

	/*
	 * If we moved a kernel QP to RESET, clean up all old CQ
	 * entries and reinitialize the QP.
	 */
	if (new_state == IB_QPS_RESET &&
	    !ibqp->uobject && qp->type != IB_QPT_XRC_TGT) {
		mlx5_ib_cq_clean(recv_cq, base->mqp.qpn,
				 ibqp->srq ? to_msrq(ibqp->srq) : NULL);
		if (send_cq != recv_cq)
			mlx5_ib_cq_clean(send_cq, base->mqp.qpn, NULL);

		qp->rq.head = 0;
		qp->rq.tail = 0;
		qp->sq.head = 0;
		qp->sq.tail = 0;
		qp->sq.cur_post = 0;
		if (qp->sq.wqe_cnt)
			qp->sq.cur_edge = get_sq_edge(&qp->sq, 0);
		qp->sq.last_poll = 0;
		qp->db.db[MLX5_RCV_DBR] = 0;
		qp->db.db[MLX5_SND_DBR] = 0;
	}

	if ((new_state == IB_QPS_RTS) && qp->counter_pending) {
		err = __mlx5_ib_qp_set_counter(ibqp, ibqp->counter);
		if (!err)
			qp->counter_pending = 0;
	}

out:
	kfree(qpc);
	return err;
}

static inline bool is_valid_mask(int mask, int req, int opt)
{
	if ((mask & req) != req)
		return false;

	if (mask & ~(req | opt))
		return false;

	return true;
}

/* check valid transition for driver QP types
 * for now the only QP type that this function supports is DCI
 */
static bool modify_dci_qp_is_ok(enum ib_qp_state cur_state, enum ib_qp_state new_state,
				enum ib_qp_attr_mask attr_mask)
{
	int req = IB_QP_STATE;
	int opt = 0;

	if (new_state == IB_QPS_RESET) {
		return is_valid_mask(attr_mask, req, opt);
	} else if (cur_state == IB_QPS_RESET && new_state == IB_QPS_INIT) {
		req |= IB_QP_PKEY_INDEX | IB_QP_PORT;
		return is_valid_mask(attr_mask, req, opt);
	} else if (cur_state == IB_QPS_INIT && new_state == IB_QPS_INIT) {
		opt = IB_QP_PKEY_INDEX | IB_QP_PORT;
		return is_valid_mask(attr_mask, req, opt);
	} else if (cur_state == IB_QPS_INIT && new_state == IB_QPS_RTR) {
		req |= IB_QP_PATH_MTU;
		opt = IB_QP_PKEY_INDEX | IB_QP_AV;
		return is_valid_mask(attr_mask, req, opt);
	} else if (cur_state == IB_QPS_RTR && new_state == IB_QPS_RTS) {
		req |= IB_QP_TIMEOUT | IB_QP_RETRY_CNT | IB_QP_RNR_RETRY |
		       IB_QP_MAX_QP_RD_ATOMIC | IB_QP_SQ_PSN;
		opt = IB_QP_MIN_RNR_TIMER;
		return is_valid_mask(attr_mask, req, opt);
	} else if (cur_state == IB_QPS_RTS && new_state == IB_QPS_RTS) {
		opt = IB_QP_MIN_RNR_TIMER;
		return is_valid_mask(attr_mask, req, opt);
	} else if (cur_state != IB_QPS_RESET && new_state == IB_QPS_ERR) {
		return is_valid_mask(attr_mask, req, opt);
	}
	return false;
}

/* mlx5_ib_modify_dct: modify a DCT QP
 * valid transitions are:
 * RESET to INIT: must set access_flags, pkey_index and port
 * INIT  to RTR : must set min_rnr_timer, tclass, flow_label,
 *			   mtu, gid_index and hop_limit
 * Other transitions and attributes are illegal
 */
static int mlx5_ib_modify_dct(struct ib_qp *ibqp, struct ib_qp_attr *attr,
			      int attr_mask, struct mlx5_ib_modify_qp *ucmd,
			      struct ib_udata *udata)
{
	struct mlx5_ib_qp *qp = to_mqp(ibqp);
	struct mlx5_ib_dev *dev = to_mdev(ibqp->device);
	enum ib_qp_state cur_state, new_state;
	int required = IB_QP_STATE;
	void *dctc;
	int err;

	if (!(attr_mask & IB_QP_STATE))
		return -EINVAL;

	cur_state = qp->state;
	new_state = attr->qp_state;

	dctc = MLX5_ADDR_OF(create_dct_in, qp->dct.in, dct_context_entry);
	if (MLX5_CAP_GEN(dev->mdev, ece_support) && ucmd->ece_options)
		/*
		 * DCT doesn't initialize QP till modify command is executed,
		 * so we need to overwrite previously set ECE field if user
		 * provided any value except zero, which means not set/not
		 * valid.
		 */
		MLX5_SET(dctc, dctc, ece, ucmd->ece_options);

	if (cur_state == IB_QPS_RESET && new_state == IB_QPS_INIT) {
		u16 set_id;

		required |= IB_QP_ACCESS_FLAGS | IB_QP_PKEY_INDEX | IB_QP_PORT;
		if (!is_valid_mask(attr_mask, required, 0))
			return -EINVAL;

		if (attr->port_num == 0 ||
		    attr->port_num > dev->num_ports) {
			mlx5_ib_dbg(dev, "invalid port number %d. number of ports is %d\n",
				    attr->port_num, dev->num_ports);
			return -EINVAL;
		}
		if (attr->qp_access_flags & IB_ACCESS_REMOTE_READ)
			MLX5_SET(dctc, dctc, rre, 1);
		if (attr->qp_access_flags & IB_ACCESS_REMOTE_WRITE)
			MLX5_SET(dctc, dctc, rwe, 1);
		if (attr->qp_access_flags & IB_ACCESS_REMOTE_ATOMIC) {
			int atomic_mode;

			atomic_mode = get_atomic_mode(dev, MLX5_IB_QPT_DCT);
			if (atomic_mode < 0)
				return -EOPNOTSUPP;

			MLX5_SET(dctc, dctc, atomic_mode, atomic_mode);
			MLX5_SET(dctc, dctc, rae, 1);
		}
		MLX5_SET(dctc, dctc, pkey_index, attr->pkey_index);
		if (mlx5_lag_is_active(dev->mdev))
			MLX5_SET(dctc, dctc, port,
				 get_tx_affinity_rr(dev, udata));
		else
			MLX5_SET(dctc, dctc, port, attr->port_num);

		set_id = mlx5_ib_get_counters_id(dev, attr->port_num - 1);
		MLX5_SET(dctc, dctc, counter_set_id, set_id);
	} else if (cur_state == IB_QPS_INIT && new_state == IB_QPS_RTR) {
		struct mlx5_ib_modify_qp_resp resp = {};
		u32 out[MLX5_ST_SZ_DW(create_dct_out)] = {};
		u32 min_resp_len = offsetofend(typeof(resp), dctn);

		if (udata->outlen < min_resp_len)
			return -EINVAL;
		/*
		 * If we don't have enough space for the ECE options,
		 * simply indicate it with resp.response_length.
		 */
		resp.response_length = (udata->outlen < sizeof(resp)) ?
					       min_resp_len :
					       sizeof(resp);

		required |= IB_QP_MIN_RNR_TIMER | IB_QP_AV | IB_QP_PATH_MTU;
		if (!is_valid_mask(attr_mask, required, 0))
			return -EINVAL;
		MLX5_SET(dctc, dctc, min_rnr_nak, attr->min_rnr_timer);
		MLX5_SET(dctc, dctc, tclass, attr->ah_attr.grh.traffic_class);
		MLX5_SET(dctc, dctc, flow_label, attr->ah_attr.grh.flow_label);
		MLX5_SET(dctc, dctc, mtu, attr->path_mtu);
		MLX5_SET(dctc, dctc, my_addr_index, attr->ah_attr.grh.sgid_index);
		MLX5_SET(dctc, dctc, hop_limit, attr->ah_attr.grh.hop_limit);
		if (attr->ah_attr.type == RDMA_AH_ATTR_TYPE_ROCE)
			MLX5_SET(dctc, dctc, eth_prio, attr->ah_attr.sl & 0x7);

		err = mlx5_core_create_dct(dev, &qp->dct.mdct, qp->dct.in,
					   MLX5_ST_SZ_BYTES(create_dct_in), out,
					   sizeof(out));
		err = mlx5_cmd_check(dev->mdev, err, qp->dct.in, out);
		if (err)
			return err;
		resp.dctn = qp->dct.mdct.mqp.qpn;
		if (MLX5_CAP_GEN(dev->mdev, ece_support))
			resp.ece_options = MLX5_GET(create_dct_out, out, ece);
		err = ib_copy_to_udata(udata, &resp, resp.response_length);
		if (err) {
			mlx5_core_destroy_dct(dev, &qp->dct.mdct);
			return err;
		}
	} else {
		mlx5_ib_warn(dev, "Modify DCT: Invalid transition from %d to %d\n", cur_state, new_state);
		return -EINVAL;
	}

	qp->state = new_state;
	return 0;
}

static bool mlx5_ib_modify_qp_allowed(struct mlx5_ib_dev *dev,
				      struct mlx5_ib_qp *qp)
{
	if (dev->profile != &raw_eth_profile)
		return true;

	if (qp->type == IB_QPT_RAW_PACKET || qp->type == MLX5_IB_QPT_REG_UMR)
		return true;

	/* Internal QP used for wc testing, with NOPs in wq */
	if (qp->flags & MLX5_IB_QP_CREATE_WC_TEST)
		return true;

	return false;
}

static int validate_rd_atomic(struct mlx5_ib_dev *dev, struct ib_qp_attr *attr,
			      int attr_mask, enum ib_qp_type qp_type)
{
	int log_max_ra_res;
	int log_max_ra_req;

	if (qp_type == MLX5_IB_QPT_DCI) {
		log_max_ra_res = 1 << MLX5_CAP_GEN(dev->mdev,
						   log_max_ra_res_dc);
		log_max_ra_req = 1 << MLX5_CAP_GEN(dev->mdev,
						   log_max_ra_req_dc);
	} else {
		log_max_ra_res = 1 << MLX5_CAP_GEN(dev->mdev,
						   log_max_ra_res_qp);
		log_max_ra_req = 1 << MLX5_CAP_GEN(dev->mdev,
						   log_max_ra_req_qp);
	}

	if (attr_mask & IB_QP_MAX_QP_RD_ATOMIC &&
	    attr->max_rd_atomic > log_max_ra_res) {
		mlx5_ib_dbg(dev, "invalid max_rd_atomic value %d\n",
			    attr->max_rd_atomic);
		return false;
	}

	if (attr_mask & IB_QP_MAX_DEST_RD_ATOMIC &&
	    attr->max_dest_rd_atomic > log_max_ra_req) {
		mlx5_ib_dbg(dev, "invalid max_dest_rd_atomic value %d\n",
			    attr->max_dest_rd_atomic);
		return false;
	}
	return true;
}

int mlx5_ib_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr,
		      int attr_mask, struct ib_udata *udata)
{
	struct mlx5_ib_dev *dev = to_mdev(ibqp->device);
	struct mlx5_ib_modify_qp_resp resp = {};
	struct mlx5_ib_qp *qp = to_mqp(ibqp);
	struct mlx5_ib_modify_qp ucmd = {};
	enum ib_qp_type qp_type;
	enum ib_qp_state cur_state, new_state;
	int err = -EINVAL;

	if (!mlx5_ib_modify_qp_allowed(dev, qp))
		return -EOPNOTSUPP;

	if (attr_mask & ~(IB_QP_ATTR_STANDARD_BITS | IB_QP_RATE_LIMIT))
		return -EOPNOTSUPP;

	if (ibqp->rwq_ind_tbl)
		return -ENOSYS;

	if (udata && udata->inlen) {
		if (udata->inlen < offsetofend(typeof(ucmd), ece_options))
			return -EINVAL;

		if (udata->inlen > sizeof(ucmd) &&
		    !ib_is_udata_cleared(udata, sizeof(ucmd),
					 udata->inlen - sizeof(ucmd)))
			return -EOPNOTSUPP;

		if (ib_copy_from_udata(&ucmd, udata,
				       min(udata->inlen, sizeof(ucmd))))
			return -EFAULT;

		if (ucmd.comp_mask ||
		    memchr_inv(&ucmd.burst_info.reserved, 0,
			       sizeof(ucmd.burst_info.reserved)))
			return -EOPNOTSUPP;

	}

	if (qp->type == IB_QPT_GSI)
		return mlx5_ib_gsi_modify_qp(ibqp, attr, attr_mask);

	qp_type = (qp->type == MLX5_IB_QPT_HW_GSI) ? IB_QPT_GSI : qp->type;

	if (qp_type == MLX5_IB_QPT_DCT)
		return mlx5_ib_modify_dct(ibqp, attr, attr_mask, &ucmd, udata);

	mutex_lock(&qp->mutex);

	cur_state = attr_mask & IB_QP_CUR_STATE ? attr->cur_qp_state : qp->state;
	new_state = attr_mask & IB_QP_STATE ? attr->qp_state : cur_state;

	if (qp->flags & IB_QP_CREATE_SOURCE_QPN) {
		if (attr_mask & ~(IB_QP_STATE | IB_QP_CUR_STATE)) {
			mlx5_ib_dbg(dev, "invalid attr_mask 0x%x when underlay QP is used\n",
				    attr_mask);
			goto out;
		}
	} else if (qp_type != MLX5_IB_QPT_REG_UMR &&
		   qp_type != MLX5_IB_QPT_DCI &&
		   !ib_modify_qp_is_ok(cur_state, new_state, qp_type,
				       attr_mask)) {
		mlx5_ib_dbg(dev, "invalid QP state transition from %d to %d, qp_type %d, attr_mask 0x%x\n",
			    cur_state, new_state, qp->type, attr_mask);
		goto out;
	} else if (qp_type == MLX5_IB_QPT_DCI &&
		   !modify_dci_qp_is_ok(cur_state, new_state, attr_mask)) {
		mlx5_ib_dbg(dev, "invalid QP state transition from %d to %d, qp_type %d, attr_mask 0x%x\n",
			    cur_state, new_state, qp_type, attr_mask);
		goto out;
	}

	if ((attr_mask & IB_QP_PORT) &&
	    (attr->port_num == 0 ||
	     attr->port_num > dev->num_ports)) {
		mlx5_ib_dbg(dev, "invalid port number %d. number of ports is %d\n",
			    attr->port_num, dev->num_ports);
		goto out;
	}

	if ((attr_mask & IB_QP_PKEY_INDEX) &&
	    attr->pkey_index >= dev->pkey_table_len) {
		mlx5_ib_dbg(dev, "invalid pkey index %d\n", attr->pkey_index);
		goto out;
	}

	if (!validate_rd_atomic(dev, attr, attr_mask, qp_type))
		goto out;

	if (cur_state == new_state && cur_state == IB_QPS_RESET) {
		err = 0;
		goto out;
	}

	err = __mlx5_ib_modify_qp(ibqp, attr, attr_mask, cur_state,
				  new_state, &ucmd, &resp, udata);

	/* resp.response_length is set in ECE supported flows only */
	if (!err && resp.response_length &&
	    udata->outlen >= resp.response_length)
		/* Return -EFAULT to the user and expect him to destroy QP. */
		err = ib_copy_to_udata(udata, &resp, resp.response_length);

out:
	mutex_unlock(&qp->mutex);
	return err;
}

static inline enum ib_qp_state to_ib_qp_state(enum mlx5_qp_state mlx5_state)
{
	switch (mlx5_state) {
	case MLX5_QP_STATE_RST:      return IB_QPS_RESET;
	case MLX5_QP_STATE_INIT:     return IB_QPS_INIT;
	case MLX5_QP_STATE_RTR:      return IB_QPS_RTR;
	case MLX5_QP_STATE_RTS:      return IB_QPS_RTS;
	case MLX5_QP_STATE_SQ_DRAINING:
	case MLX5_QP_STATE_SQD:      return IB_QPS_SQD;
	case MLX5_QP_STATE_SQER:     return IB_QPS_SQE;
	case MLX5_QP_STATE_ERR:      return IB_QPS_ERR;
	default:		     return -1;
	}
}

static inline enum ib_mig_state to_ib_mig_state(int mlx5_mig_state)
{
	switch (mlx5_mig_state) {
	case MLX5_QP_PM_ARMED:		return IB_MIG_ARMED;
	case MLX5_QP_PM_REARM:		return IB_MIG_REARM;
	case MLX5_QP_PM_MIGRATED:	return IB_MIG_MIGRATED;
	default: return -1;
	}
}

static void to_rdma_ah_attr(struct mlx5_ib_dev *ibdev,
			    struct rdma_ah_attr *ah_attr, void *path)
{
	int port = MLX5_GET(ads, path, vhca_port_num);
	int static_rate;

	memset(ah_attr, 0, sizeof(*ah_attr));

	if (!port || port > ibdev->num_ports)
		return;

	ah_attr->type = rdma_ah_find_type(&ibdev->ib_dev, port);

	rdma_ah_set_port_num(ah_attr, port);
	rdma_ah_set_sl(ah_attr, MLX5_GET(ads, path, sl));

	rdma_ah_set_dlid(ah_attr, MLX5_GET(ads, path, rlid));
	rdma_ah_set_path_bits(ah_attr, MLX5_GET(ads, path, mlid));

	static_rate = MLX5_GET(ads, path, stat_rate);
	rdma_ah_set_static_rate(ah_attr, mlx5_to_ib_rate_map(static_rate));
	if (MLX5_GET(ads, path, grh) ||
	    ah_attr->type == RDMA_AH_ATTR_TYPE_ROCE) {
		rdma_ah_set_grh(ah_attr, NULL, MLX5_GET(ads, path, flow_label),
				MLX5_GET(ads, path, src_addr_index),
				MLX5_GET(ads, path, hop_limit),
				MLX5_GET(ads, path, tclass));
		rdma_ah_set_dgid_raw(ah_attr, MLX5_ADDR_OF(ads, path, rgid_rip));
	}
}

static int query_raw_packet_qp_sq_state(struct mlx5_ib_dev *dev,
					struct mlx5_ib_sq *sq,
					u8 *sq_state)
{
	int err;

	err = mlx5_core_query_sq_state(dev->mdev, sq->base.mqp.qpn, sq_state);
	if (err)
		goto out;
	sq->state = *sq_state;

out:
	return err;
}

static int query_raw_packet_qp_rq_state(struct mlx5_ib_dev *dev,
					struct mlx5_ib_rq *rq,
					u8 *rq_state)
{
	void *out;
	void *rqc;
	int inlen;
	int err;

	inlen = MLX5_ST_SZ_BYTES(query_rq_out);
	out = kvzalloc(inlen, GFP_KERNEL);
	if (!out)
		return -ENOMEM;

	err = mlx5_core_query_rq(dev->mdev, rq->base.mqp.qpn, out);
	if (err)
		goto out;

	rqc = MLX5_ADDR_OF(query_rq_out, out, rq_context);
	*rq_state = MLX5_GET(rqc, rqc, state);
	rq->state = *rq_state;

out:
	kvfree(out);
	return err;
}

static int sqrq_state_to_qp_state(u8 sq_state, u8 rq_state,
				  struct mlx5_ib_qp *qp, u8 *qp_state)
{
	static const u8 sqrq_trans[MLX5_RQ_NUM_STATE][MLX5_SQ_NUM_STATE] = {
		[MLX5_RQC_STATE_RST] = {
			[MLX5_SQC_STATE_RST]	= IB_QPS_RESET,
			[MLX5_SQC_STATE_RDY]	= MLX5_QP_STATE_BAD,
			[MLX5_SQC_STATE_ERR]	= MLX5_QP_STATE_BAD,
			[MLX5_SQ_STATE_NA]	= IB_QPS_RESET,
		},
		[MLX5_RQC_STATE_RDY] = {
			[MLX5_SQC_STATE_RST]	= MLX5_QP_STATE,
			[MLX5_SQC_STATE_RDY]	= MLX5_QP_STATE,
			[MLX5_SQC_STATE_ERR]	= IB_QPS_SQE,
			[MLX5_SQ_STATE_NA]	= MLX5_QP_STATE,
		},
		[MLX5_RQC_STATE_ERR] = {
			[MLX5_SQC_STATE_RST]    = MLX5_QP_STATE_BAD,
			[MLX5_SQC_STATE_RDY]	= MLX5_QP_STATE_BAD,
			[MLX5_SQC_STATE_ERR]	= IB_QPS_ERR,
			[MLX5_SQ_STATE_NA]	= IB_QPS_ERR,
		},
		[MLX5_RQ_STATE_NA] = {
			[MLX5_SQC_STATE_RST]    = MLX5_QP_STATE,
			[MLX5_SQC_STATE_RDY]	= MLX5_QP_STATE,
			[MLX5_SQC_STATE_ERR]	= MLX5_QP_STATE,
			[MLX5_SQ_STATE_NA]	= MLX5_QP_STATE_BAD,
		},
	};

	*qp_state = sqrq_trans[rq_state][sq_state];

	if (*qp_state == MLX5_QP_STATE_BAD) {
		WARN(1, "Buggy Raw Packet QP state, SQ 0x%x state: 0x%x, RQ 0x%x state: 0x%x",
		     qp->raw_packet_qp.sq.base.mqp.qpn, sq_state,
		     qp->raw_packet_qp.rq.base.mqp.qpn, rq_state);
		return -EINVAL;
	}

	if (*qp_state == MLX5_QP_STATE)
		*qp_state = qp->state;

	return 0;
}

static int query_raw_packet_qp_state(struct mlx5_ib_dev *dev,
				     struct mlx5_ib_qp *qp,
				     u8 *raw_packet_qp_state)
{
	struct mlx5_ib_raw_packet_qp *raw_packet_qp = &qp->raw_packet_qp;
	struct mlx5_ib_sq *sq = &raw_packet_qp->sq;
	struct mlx5_ib_rq *rq = &raw_packet_qp->rq;
	int err;
	u8 sq_state = MLX5_SQ_STATE_NA;
	u8 rq_state = MLX5_RQ_STATE_NA;

	if (qp->sq.wqe_cnt) {
		err = query_raw_packet_qp_sq_state(dev, sq, &sq_state);
		if (err)
			return err;
	}

	if (qp->rq.wqe_cnt) {
		err = query_raw_packet_qp_rq_state(dev, rq, &rq_state);
		if (err)
			return err;
	}

	return sqrq_state_to_qp_state(sq_state, rq_state, qp,
				      raw_packet_qp_state);
}

static int query_qp_attr(struct mlx5_ib_dev *dev, struct mlx5_ib_qp *qp,
			 struct ib_qp_attr *qp_attr)
{
	int outlen = MLX5_ST_SZ_BYTES(query_qp_out);
	void *qpc, *pri_path, *alt_path;
	u32 *outb;
	int err;

	outb = kzalloc(outlen, GFP_KERNEL);
	if (!outb)
		return -ENOMEM;

	err = mlx5_core_qp_query(dev, &qp->trans_qp.base.mqp, outb, outlen,
				 false);
	if (err)
		goto out;

	qpc = MLX5_ADDR_OF(query_qp_out, outb, qpc);

	qp->state = to_ib_qp_state(MLX5_GET(qpc, qpc, state));
	if (MLX5_GET(qpc, qpc, state) == MLX5_QP_STATE_SQ_DRAINING)
		qp_attr->sq_draining = 1;

	qp_attr->path_mtu = MLX5_GET(qpc, qpc, mtu);
	qp_attr->path_mig_state = to_ib_mig_state(MLX5_GET(qpc, qpc, pm_state));
	qp_attr->qkey = MLX5_GET(qpc, qpc, q_key);
	qp_attr->rq_psn = MLX5_GET(qpc, qpc, next_rcv_psn);
	qp_attr->sq_psn = MLX5_GET(qpc, qpc, next_send_psn);
	qp_attr->dest_qp_num = MLX5_GET(qpc, qpc, remote_qpn);

	if (MLX5_GET(qpc, qpc, rre))
		qp_attr->qp_access_flags |= IB_ACCESS_REMOTE_READ;
	if (MLX5_GET(qpc, qpc, rwe))
		qp_attr->qp_access_flags |= IB_ACCESS_REMOTE_WRITE;
	if (MLX5_GET(qpc, qpc, rae))
		qp_attr->qp_access_flags |= IB_ACCESS_REMOTE_ATOMIC;

	qp_attr->max_rd_atomic = 1 << MLX5_GET(qpc, qpc, log_sra_max);
	qp_attr->max_dest_rd_atomic = 1 << MLX5_GET(qpc, qpc, log_rra_max);
	qp_attr->min_rnr_timer = MLX5_GET(qpc, qpc, min_rnr_nak);
	qp_attr->retry_cnt = MLX5_GET(qpc, qpc, retry_count);
	qp_attr->rnr_retry = MLX5_GET(qpc, qpc, rnr_retry);

	pri_path = MLX5_ADDR_OF(qpc, qpc, primary_address_path);
	alt_path = MLX5_ADDR_OF(qpc, qpc, secondary_address_path);

	if (qp->type == IB_QPT_RC || qp->type == IB_QPT_UC ||
	    qp->type == IB_QPT_XRC_INI || qp->type == IB_QPT_XRC_TGT) {
		to_rdma_ah_attr(dev, &qp_attr->ah_attr, pri_path);
		to_rdma_ah_attr(dev, &qp_attr->alt_ah_attr, alt_path);
		qp_attr->alt_pkey_index = MLX5_GET(ads, alt_path, pkey_index);
		qp_attr->alt_port_num = MLX5_GET(ads, alt_path, vhca_port_num);
	}

	qp_attr->pkey_index = MLX5_GET(ads, pri_path, pkey_index);
	qp_attr->port_num = MLX5_GET(ads, pri_path, vhca_port_num);
	qp_attr->timeout = MLX5_GET(ads, pri_path, ack_timeout);
	qp_attr->alt_timeout = MLX5_GET(ads, alt_path, ack_timeout);

out:
	kfree(outb);
	return err;
}

static int mlx5_ib_dct_query_qp(struct mlx5_ib_dev *dev, struct mlx5_ib_qp *mqp,
				struct ib_qp_attr *qp_attr, int qp_attr_mask,
				struct ib_qp_init_attr *qp_init_attr)
{
	struct mlx5_core_dct	*dct = &mqp->dct.mdct;
	u32 *out;
	u32 access_flags = 0;
	int outlen = MLX5_ST_SZ_BYTES(query_dct_out);
	void *dctc;
	int err;
	int supported_mask = IB_QP_STATE |
			     IB_QP_ACCESS_FLAGS |
			     IB_QP_PORT |
			     IB_QP_MIN_RNR_TIMER |
			     IB_QP_AV |
			     IB_QP_PATH_MTU |
			     IB_QP_PKEY_INDEX;

	if (qp_attr_mask & ~supported_mask)
		return -EINVAL;
	if (mqp->state != IB_QPS_RTR)
		return -EINVAL;

	out = kzalloc(outlen, GFP_KERNEL);
	if (!out)
		return -ENOMEM;

	err = mlx5_core_dct_query(dev, dct, out, outlen);
	if (err)
		goto out;

	dctc = MLX5_ADDR_OF(query_dct_out, out, dct_context_entry);

	if (qp_attr_mask & IB_QP_STATE)
		qp_attr->qp_state = IB_QPS_RTR;

	if (qp_attr_mask & IB_QP_ACCESS_FLAGS) {
		if (MLX5_GET(dctc, dctc, rre))
			access_flags |= IB_ACCESS_REMOTE_READ;
		if (MLX5_GET(dctc, dctc, rwe))
			access_flags |= IB_ACCESS_REMOTE_WRITE;
		if (MLX5_GET(dctc, dctc, rae))
			access_flags |= IB_ACCESS_REMOTE_ATOMIC;
		qp_attr->qp_access_flags = access_flags;
	}

	if (qp_attr_mask & IB_QP_PORT)
		qp_attr->port_num = MLX5_GET(dctc, dctc, port);
	if (qp_attr_mask & IB_QP_MIN_RNR_TIMER)
		qp_attr->min_rnr_timer = MLX5_GET(dctc, dctc, min_rnr_nak);
	if (qp_attr_mask & IB_QP_AV) {
		qp_attr->ah_attr.grh.traffic_class = MLX5_GET(dctc, dctc, tclass);
		qp_attr->ah_attr.grh.flow_label = MLX5_GET(dctc, dctc, flow_label);
		qp_attr->ah_attr.grh.sgid_index = MLX5_GET(dctc, dctc, my_addr_index);
		qp_attr->ah_attr.grh.hop_limit = MLX5_GET(dctc, dctc, hop_limit);
	}
	if (qp_attr_mask & IB_QP_PATH_MTU)
		qp_attr->path_mtu = MLX5_GET(dctc, dctc, mtu);
	if (qp_attr_mask & IB_QP_PKEY_INDEX)
		qp_attr->pkey_index = MLX5_GET(dctc, dctc, pkey_index);
out:
	kfree(out);
	return err;
}

int mlx5_ib_query_qp(struct ib_qp *ibqp, struct ib_qp_attr *qp_attr,
		     int qp_attr_mask, struct ib_qp_init_attr *qp_init_attr)
{
	struct mlx5_ib_dev *dev = to_mdev(ibqp->device);
	struct mlx5_ib_qp *qp = to_mqp(ibqp);
	int err = 0;
	u8 raw_packet_qp_state;

	if (ibqp->rwq_ind_tbl)
		return -ENOSYS;

	if (qp->type == IB_QPT_GSI)
		return mlx5_ib_gsi_query_qp(ibqp, qp_attr, qp_attr_mask,
					    qp_init_attr);

	/* Not all of output fields are applicable, make sure to zero them */
	memset(qp_init_attr, 0, sizeof(*qp_init_attr));
	memset(qp_attr, 0, sizeof(*qp_attr));

	if (unlikely(qp->type == MLX5_IB_QPT_DCT))
		return mlx5_ib_dct_query_qp(dev, qp, qp_attr,
					    qp_attr_mask, qp_init_attr);

	mutex_lock(&qp->mutex);

	if (qp->type == IB_QPT_RAW_PACKET ||
	    qp->flags & IB_QP_CREATE_SOURCE_QPN) {
		err = query_raw_packet_qp_state(dev, qp, &raw_packet_qp_state);
		if (err)
			goto out;
		qp->state = raw_packet_qp_state;
		qp_attr->port_num = 1;
	} else {
		err = query_qp_attr(dev, qp, qp_attr);
		if (err)
			goto out;
	}

	qp_attr->qp_state	     = qp->state;
	qp_attr->cur_qp_state	     = qp_attr->qp_state;
	qp_attr->cap.max_recv_wr     = qp->rq.wqe_cnt;
	qp_attr->cap.max_recv_sge    = qp->rq.max_gs;

	if (!ibqp->uobject) {
		qp_attr->cap.max_send_wr  = qp->sq.max_post;
		qp_attr->cap.max_send_sge = qp->sq.max_gs;
		qp_init_attr->qp_context = ibqp->qp_context;
	} else {
		qp_attr->cap.max_send_wr  = 0;
		qp_attr->cap.max_send_sge = 0;
	}

	qp_init_attr->qp_type = qp->type;
	qp_init_attr->recv_cq = ibqp->recv_cq;
	qp_init_attr->send_cq = ibqp->send_cq;
	qp_init_attr->srq = ibqp->srq;
	qp_attr->cap.max_inline_data = qp->max_inline_data;

	qp_init_attr->cap	     = qp_attr->cap;

	qp_init_attr->create_flags = qp->flags;

	qp_init_attr->sq_sig_type = qp->sq_signal_bits & MLX5_WQE_CTRL_CQ_UPDATE ?
		IB_SIGNAL_ALL_WR : IB_SIGNAL_REQ_WR;

out:
	mutex_unlock(&qp->mutex);
	return err;
}

int mlx5_ib_alloc_xrcd(struct ib_xrcd *ibxrcd, struct ib_udata *udata)
{
	struct mlx5_ib_dev *dev = to_mdev(ibxrcd->device);
	struct mlx5_ib_xrcd *xrcd = to_mxrcd(ibxrcd);

	if (!MLX5_CAP_GEN(dev->mdev, xrc))
		return -EOPNOTSUPP;

	return mlx5_cmd_xrcd_alloc(dev->mdev, &xrcd->xrcdn, 0);
}

int mlx5_ib_dealloc_xrcd(struct ib_xrcd *xrcd, struct ib_udata *udata)
{
	struct mlx5_ib_dev *dev = to_mdev(xrcd->device);
	u32 xrcdn = to_mxrcd(xrcd)->xrcdn;

	return mlx5_cmd_xrcd_dealloc(dev->mdev, xrcdn, 0);
}

static void mlx5_ib_wq_event(struct mlx5_core_qp *core_qp, int type)
{
	struct mlx5_ib_rwq *rwq = to_mibrwq(core_qp);
	struct mlx5_ib_dev *dev = to_mdev(rwq->ibwq.device);
	struct ib_event event;

	if (rwq->ibwq.event_handler) {
		event.device     = rwq->ibwq.device;
		event.element.wq = &rwq->ibwq;
		switch (type) {
		case MLX5_EVENT_TYPE_WQ_CATAS_ERROR:
			event.event = IB_EVENT_WQ_FATAL;
			break;
		default:
			mlx5_ib_warn(dev, "Unexpected event type %d on WQ %06x\n", type, core_qp->qpn);
			return;
		}

		rwq->ibwq.event_handler(&event, rwq->ibwq.wq_context);
	}
}

static int set_delay_drop(struct mlx5_ib_dev *dev)
{
	int err = 0;

	mutex_lock(&dev->delay_drop.lock);
	if (dev->delay_drop.activate)
		goto out;

	err = mlx5_core_set_delay_drop(dev, dev->delay_drop.timeout);
	if (err)
		goto out;

	dev->delay_drop.activate = true;
out:
	mutex_unlock(&dev->delay_drop.lock);

	if (!err)
		atomic_inc(&dev->delay_drop.rqs_cnt);
	return err;
}

static int  create_rq(struct mlx5_ib_rwq *rwq, struct ib_pd *pd,
		      struct ib_wq_init_attr *init_attr)
{
	struct mlx5_ib_dev *dev;
	int has_net_offloads;
	__be64 *rq_pas0;
	int ts_format;
	void *in;
	void *rqc;
	void *wq;
	int inlen;
	int err;

	dev = to_mdev(pd->device);

	ts_format = get_rq_ts_format(dev, to_mcq(init_attr->cq));
	if (ts_format < 0)
		return ts_format;

	inlen = MLX5_ST_SZ_BYTES(create_rq_in) + sizeof(u64) * rwq->rq_num_pas;
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	MLX5_SET(create_rq_in, in, uid, to_mpd(pd)->uid);
	rqc = MLX5_ADDR_OF(create_rq_in, in, ctx);
	MLX5_SET(rqc,  rqc, mem_rq_type,
		 MLX5_RQC_MEM_RQ_TYPE_MEMORY_RQ_INLINE);
	MLX5_SET(rqc, rqc, ts_format, ts_format);
	MLX5_SET(rqc, rqc, user_index, rwq->user_index);
	MLX5_SET(rqc,  rqc, cqn, to_mcq(init_attr->cq)->mcq.cqn);
	MLX5_SET(rqc,  rqc, state, MLX5_RQC_STATE_RST);
	MLX5_SET(rqc,  rqc, flush_in_error_en, 1);
	wq = MLX5_ADDR_OF(rqc, rqc, wq);
	MLX5_SET(wq, wq, wq_type,
		 rwq->create_flags & MLX5_IB_WQ_FLAGS_STRIDING_RQ ?
		 MLX5_WQ_TYPE_CYCLIC_STRIDING_RQ : MLX5_WQ_TYPE_CYCLIC);
	if (init_attr->create_flags & IB_WQ_FLAGS_PCI_WRITE_END_PADDING) {
		if (!MLX5_CAP_GEN(dev->mdev, end_pad)) {
			mlx5_ib_dbg(dev, "Scatter end padding is not supported\n");
			err = -EOPNOTSUPP;
			goto out;
		} else {
			MLX5_SET(wq, wq, end_padding_mode, MLX5_WQ_END_PAD_MODE_ALIGN);
		}
	}
	MLX5_SET(wq, wq, log_wq_stride, rwq->log_rq_stride);
	if (rwq->create_flags & MLX5_IB_WQ_FLAGS_STRIDING_RQ) {
		/*
		 * In Firmware number of strides in each WQE is:
		 *   "512 * 2^single_wqe_log_num_of_strides"
		 * Values 3 to 8 are accepted as 10 to 15, 9 to 18 are
		 * accepted as 0 to 9
		 */
		static const u8 fw_map[] = { 10, 11, 12, 13, 14, 15, 0, 1,
					     2,  3,  4,  5,  6,  7,  8, 9 };
		MLX5_SET(wq, wq, two_byte_shift_en, rwq->two_byte_shift_en);
		MLX5_SET(wq, wq, log_wqe_stride_size,
			 rwq->single_stride_log_num_of_bytes -
			 MLX5_MIN_SINGLE_STRIDE_LOG_NUM_BYTES);
		MLX5_SET(wq, wq, log_wqe_num_of_strides,
			 fw_map[rwq->log_num_strides -
				MLX5_EXT_MIN_SINGLE_WQE_LOG_NUM_STRIDES]);
	}
	MLX5_SET(wq, wq, log_wq_sz, rwq->log_rq_size);
	MLX5_SET(wq, wq, pd, to_mpd(pd)->pdn);
	MLX5_SET(wq, wq, page_offset, rwq->rq_page_offset);
	MLX5_SET(wq, wq, log_wq_pg_sz, rwq->log_page_size);
	MLX5_SET(wq, wq, wq_signature, rwq->wq_sig);
	MLX5_SET64(wq, wq, dbr_addr, rwq->db.dma);
	has_net_offloads = MLX5_CAP_GEN(dev->mdev, eth_net_offloads);
	if (init_attr->create_flags & IB_WQ_FLAGS_CVLAN_STRIPPING) {
		if (!(has_net_offloads && MLX5_CAP_ETH(dev->mdev, vlan_cap))) {
			mlx5_ib_dbg(dev, "VLAN offloads are not supported\n");
			err = -EOPNOTSUPP;
			goto out;
		}
	} else {
		MLX5_SET(rqc, rqc, vsd, 1);
	}
	if (init_attr->create_flags & IB_WQ_FLAGS_SCATTER_FCS) {
		if (!(has_net_offloads && MLX5_CAP_ETH(dev->mdev, scatter_fcs))) {
			mlx5_ib_dbg(dev, "Scatter FCS is not supported\n");
			err = -EOPNOTSUPP;
			goto out;
		}
		MLX5_SET(rqc, rqc, scatter_fcs, 1);
	}
	if (init_attr->create_flags & IB_WQ_FLAGS_DELAY_DROP) {
		if (!(dev->ib_dev.attrs.raw_packet_caps &
		      IB_RAW_PACKET_CAP_DELAY_DROP)) {
			mlx5_ib_dbg(dev, "Delay drop is not supported\n");
			err = -EOPNOTSUPP;
			goto out;
		}
		MLX5_SET(rqc, rqc, delay_drop_en, 1);
	}
	rq_pas0 = (__be64 *)MLX5_ADDR_OF(wq, wq, pas);
	mlx5_ib_populate_pas(rwq->umem, 1UL << rwq->page_shift, rq_pas0, 0);
	err = mlx5_core_create_rq_tracked(dev, in, inlen, &rwq->core_qp);
	if (!err && init_attr->create_flags & IB_WQ_FLAGS_DELAY_DROP) {
		err = set_delay_drop(dev);
		if (err) {
			mlx5_ib_warn(dev, "Failed to enable delay drop err=%d\n",
				     err);
			mlx5_core_destroy_rq_tracked(dev, &rwq->core_qp);
		} else {
			rwq->create_flags |= MLX5_IB_WQ_FLAGS_DELAY_DROP;
		}
	}
out:
	kvfree(in);
	return err;
}

static int set_user_rq_size(struct mlx5_ib_dev *dev,
			    struct ib_wq_init_attr *wq_init_attr,
			    struct mlx5_ib_create_wq *ucmd,
			    struct mlx5_ib_rwq *rwq)
{
	/* Sanity check RQ size before proceeding */
	if (wq_init_attr->max_wr > (1 << MLX5_CAP_GEN(dev->mdev, log_max_wq_sz)))
		return -EINVAL;

	if (!ucmd->rq_wqe_count)
		return -EINVAL;

	rwq->wqe_count = ucmd->rq_wqe_count;
	rwq->wqe_shift = ucmd->rq_wqe_shift;
	if (check_shl_overflow(rwq->wqe_count, rwq->wqe_shift, &rwq->buf_size))
		return -EINVAL;

	rwq->log_rq_stride = rwq->wqe_shift;
	rwq->log_rq_size = ilog2(rwq->wqe_count);
	return 0;
}

static bool log_of_strides_valid(struct mlx5_ib_dev *dev, u32 log_num_strides)
{
	if ((log_num_strides > MLX5_MAX_SINGLE_WQE_LOG_NUM_STRIDES) ||
	    (log_num_strides < MLX5_EXT_MIN_SINGLE_WQE_LOG_NUM_STRIDES))
		return false;

	if (!MLX5_CAP_GEN(dev->mdev, ext_stride_num_range) &&
	    (log_num_strides < MLX5_MIN_SINGLE_WQE_LOG_NUM_STRIDES))
		return false;

	return true;
}

static int prepare_user_rq(struct ib_pd *pd,
			   struct ib_wq_init_attr *init_attr,
			   struct ib_udata *udata,
			   struct mlx5_ib_rwq *rwq)
{
	struct mlx5_ib_dev *dev = to_mdev(pd->device);
	struct mlx5_ib_create_wq ucmd = {};
	int err;
	size_t required_cmd_sz;

	required_cmd_sz = offsetofend(struct mlx5_ib_create_wq,
				      single_stride_log_num_of_bytes);
	if (udata->inlen < required_cmd_sz) {
		mlx5_ib_dbg(dev, "invalid inlen\n");
		return -EINVAL;
	}

	if (udata->inlen > sizeof(ucmd) &&
	    !ib_is_udata_cleared(udata, sizeof(ucmd),
				 udata->inlen - sizeof(ucmd))) {
		mlx5_ib_dbg(dev, "inlen is not supported\n");
		return -EOPNOTSUPP;
	}

	if (ib_copy_from_udata(&ucmd, udata, min(sizeof(ucmd), udata->inlen))) {
		mlx5_ib_dbg(dev, "copy failed\n");
		return -EFAULT;
	}

	if (ucmd.comp_mask & (~MLX5_IB_CREATE_WQ_STRIDING_RQ)) {
		mlx5_ib_dbg(dev, "invalid comp mask\n");
		return -EOPNOTSUPP;
	} else if (ucmd.comp_mask & MLX5_IB_CREATE_WQ_STRIDING_RQ) {
		if (!MLX5_CAP_GEN(dev->mdev, striding_rq)) {
			mlx5_ib_dbg(dev, "Striding RQ is not supported\n");
			return -EOPNOTSUPP;
		}
		if ((ucmd.single_stride_log_num_of_bytes <
		    MLX5_MIN_SINGLE_STRIDE_LOG_NUM_BYTES) ||
		    (ucmd.single_stride_log_num_of_bytes >
		     MLX5_MAX_SINGLE_STRIDE_LOG_NUM_BYTES)) {
			mlx5_ib_dbg(dev, "Invalid log stride size (%u. Range is %u - %u)\n",
				    ucmd.single_stride_log_num_of_bytes,
				    MLX5_MIN_SINGLE_STRIDE_LOG_NUM_BYTES,
				    MLX5_MAX_SINGLE_STRIDE_LOG_NUM_BYTES);
			return -EINVAL;
		}
		if (!log_of_strides_valid(dev,
					  ucmd.single_wqe_log_num_of_strides)) {
			mlx5_ib_dbg(
				dev,
				"Invalid log num strides (%u. Range is %u - %u)\n",
				ucmd.single_wqe_log_num_of_strides,
				MLX5_CAP_GEN(dev->mdev, ext_stride_num_range) ?
					MLX5_EXT_MIN_SINGLE_WQE_LOG_NUM_STRIDES :
					MLX5_MIN_SINGLE_WQE_LOG_NUM_STRIDES,
				MLX5_MAX_SINGLE_WQE_LOG_NUM_STRIDES);
			return -EINVAL;
		}
		rwq->single_stride_log_num_of_bytes =
			ucmd.single_stride_log_num_of_bytes;
		rwq->log_num_strides = ucmd.single_wqe_log_num_of_strides;
		rwq->two_byte_shift_en = !!ucmd.two_byte_shift_en;
		rwq->create_flags |= MLX5_IB_WQ_FLAGS_STRIDING_RQ;
	}

	err = set_user_rq_size(dev, init_attr, &ucmd, rwq);
	if (err) {
		mlx5_ib_dbg(dev, "err %d\n", err);
		return err;
	}

	err = create_user_rq(dev, pd, udata, rwq, &ucmd);
	if (err) {
		mlx5_ib_dbg(dev, "err %d\n", err);
		return err;
	}

	rwq->user_index = ucmd.user_index;
	return 0;
}

struct ib_wq *mlx5_ib_create_wq(struct ib_pd *pd,
				struct ib_wq_init_attr *init_attr,
				struct ib_udata *udata)
{
	struct mlx5_ib_dev *dev;
	struct mlx5_ib_rwq *rwq;
	struct mlx5_ib_create_wq_resp resp = {};
	size_t min_resp_len;
	int err;

	if (!udata)
		return ERR_PTR(-ENOSYS);

	min_resp_len = offsetofend(struct mlx5_ib_create_wq_resp, reserved);
	if (udata->outlen && udata->outlen < min_resp_len)
		return ERR_PTR(-EINVAL);

	if (!capable(CAP_SYS_RAWIO) &&
	    init_attr->create_flags & IB_WQ_FLAGS_DELAY_DROP)
		return ERR_PTR(-EPERM);

	dev = to_mdev(pd->device);
	switch (init_attr->wq_type) {
	case IB_WQT_RQ:
		rwq = kzalloc(sizeof(*rwq), GFP_KERNEL);
		if (!rwq)
			return ERR_PTR(-ENOMEM);
		err = prepare_user_rq(pd, init_attr, udata, rwq);
		if (err)
			goto err;
		err = create_rq(rwq, pd, init_attr);
		if (err)
			goto err_user_rq;
		break;
	default:
		mlx5_ib_dbg(dev, "unsupported wq type %d\n",
			    init_attr->wq_type);
		return ERR_PTR(-EINVAL);
	}

	rwq->ibwq.wq_num = rwq->core_qp.qpn;
	rwq->ibwq.state = IB_WQS_RESET;
	if (udata->outlen) {
		resp.response_length = offsetofend(
			struct mlx5_ib_create_wq_resp, response_length);
		err = ib_copy_to_udata(udata, &resp, resp.response_length);
		if (err)
			goto err_copy;
	}

	rwq->core_qp.event = mlx5_ib_wq_event;
	rwq->ibwq.event_handler = init_attr->event_handler;
	return &rwq->ibwq;

err_copy:
	mlx5_core_destroy_rq_tracked(dev, &rwq->core_qp);
err_user_rq:
	destroy_user_rq(dev, pd, rwq, udata);
err:
	kfree(rwq);
	return ERR_PTR(err);
}

int mlx5_ib_destroy_wq(struct ib_wq *wq, struct ib_udata *udata)
{
	struct mlx5_ib_dev *dev = to_mdev(wq->device);
	struct mlx5_ib_rwq *rwq = to_mrwq(wq);
	int ret;

	ret = mlx5_core_destroy_rq_tracked(dev, &rwq->core_qp);
	if (ret)
		return ret;
	destroy_user_rq(dev, wq->pd, rwq, udata);
	kfree(rwq);
	return 0;
}

int mlx5_ib_create_rwq_ind_table(struct ib_rwq_ind_table *ib_rwq_ind_table,
				 struct ib_rwq_ind_table_init_attr *init_attr,
				 struct ib_udata *udata)
{
	struct mlx5_ib_rwq_ind_table *rwq_ind_tbl =
		to_mrwq_ind_table(ib_rwq_ind_table);
	struct mlx5_ib_dev *dev = to_mdev(ib_rwq_ind_table->device);
	int sz = 1 << init_attr->log_ind_tbl_size;
	struct mlx5_ib_create_rwq_ind_tbl_resp resp = {};
	size_t min_resp_len;
	int inlen;
	int err;
	int i;
	u32 *in;
	void *rqtc;

	if (udata->inlen > 0 &&
	    !ib_is_udata_cleared(udata, 0,
				 udata->inlen))
		return -EOPNOTSUPP;

	if (init_attr->log_ind_tbl_size >
	    MLX5_CAP_GEN(dev->mdev, log_max_rqt_size)) {
		mlx5_ib_dbg(dev, "log_ind_tbl_size = %d is bigger than supported = %d\n",
			    init_attr->log_ind_tbl_size,
			    MLX5_CAP_GEN(dev->mdev, log_max_rqt_size));
		return -EINVAL;
	}

	min_resp_len =
		offsetofend(struct mlx5_ib_create_rwq_ind_tbl_resp, reserved);
	if (udata->outlen && udata->outlen < min_resp_len)
		return -EINVAL;

	inlen = MLX5_ST_SZ_BYTES(create_rqt_in) + sizeof(u32) * sz;
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	rqtc = MLX5_ADDR_OF(create_rqt_in, in, rqt_context);

	MLX5_SET(rqtc, rqtc, rqt_actual_size, sz);
	MLX5_SET(rqtc, rqtc, rqt_max_size, sz);

	for (i = 0; i < sz; i++)
		MLX5_SET(rqtc, rqtc, rq_num[i], init_attr->ind_tbl[i]->wq_num);

	rwq_ind_tbl->uid = to_mpd(init_attr->ind_tbl[0]->pd)->uid;
	MLX5_SET(create_rqt_in, in, uid, rwq_ind_tbl->uid);

	err = mlx5_core_create_rqt(dev->mdev, in, inlen, &rwq_ind_tbl->rqtn);
	kvfree(in);
	if (err)
		return err;

	rwq_ind_tbl->ib_rwq_ind_tbl.ind_tbl_num = rwq_ind_tbl->rqtn;
	if (udata->outlen) {
		resp.response_length =
			offsetofend(struct mlx5_ib_create_rwq_ind_tbl_resp,
				    response_length);
		err = ib_copy_to_udata(udata, &resp, resp.response_length);
		if (err)
			goto err_copy;
	}

	return 0;

err_copy:
	mlx5_cmd_destroy_rqt(dev->mdev, rwq_ind_tbl->rqtn, rwq_ind_tbl->uid);
	return err;
}

int mlx5_ib_destroy_rwq_ind_table(struct ib_rwq_ind_table *ib_rwq_ind_tbl)
{
	struct mlx5_ib_rwq_ind_table *rwq_ind_tbl = to_mrwq_ind_table(ib_rwq_ind_tbl);
	struct mlx5_ib_dev *dev = to_mdev(ib_rwq_ind_tbl->device);

	return mlx5_cmd_destroy_rqt(dev->mdev, rwq_ind_tbl->rqtn, rwq_ind_tbl->uid);
}

int mlx5_ib_modify_wq(struct ib_wq *wq, struct ib_wq_attr *wq_attr,
		      u32 wq_attr_mask, struct ib_udata *udata)
{
	struct mlx5_ib_dev *dev = to_mdev(wq->device);
	struct mlx5_ib_rwq *rwq = to_mrwq(wq);
	struct mlx5_ib_modify_wq ucmd = {};
	size_t required_cmd_sz;
	int curr_wq_state;
	int wq_state;
	int inlen;
	int err;
	void *rqc;
	void *in;

	required_cmd_sz = offsetofend(struct mlx5_ib_modify_wq, reserved);
	if (udata->inlen < required_cmd_sz)
		return -EINVAL;

	if (udata->inlen > sizeof(ucmd) &&
	    !ib_is_udata_cleared(udata, sizeof(ucmd),
				 udata->inlen - sizeof(ucmd)))
		return -EOPNOTSUPP;

	if (ib_copy_from_udata(&ucmd, udata, min(sizeof(ucmd), udata->inlen)))
		return -EFAULT;

	if (ucmd.comp_mask || ucmd.reserved)
		return -EOPNOTSUPP;

	inlen = MLX5_ST_SZ_BYTES(modify_rq_in);
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	rqc = MLX5_ADDR_OF(modify_rq_in, in, ctx);

	curr_wq_state = wq_attr->curr_wq_state;
	wq_state = wq_attr->wq_state;
	if (curr_wq_state == IB_WQS_ERR)
		curr_wq_state = MLX5_RQC_STATE_ERR;
	if (wq_state == IB_WQS_ERR)
		wq_state = MLX5_RQC_STATE_ERR;
	MLX5_SET(modify_rq_in, in, rq_state, curr_wq_state);
	MLX5_SET(modify_rq_in, in, uid, to_mpd(wq->pd)->uid);
	MLX5_SET(rqc, rqc, state, wq_state);

	if (wq_attr_mask & IB_WQ_FLAGS) {
		if (wq_attr->flags_mask & IB_WQ_FLAGS_CVLAN_STRIPPING) {
			if (!(MLX5_CAP_GEN(dev->mdev, eth_net_offloads) &&
			      MLX5_CAP_ETH(dev->mdev, vlan_cap))) {
				mlx5_ib_dbg(dev, "VLAN offloads are not supported\n");
				err = -EOPNOTSUPP;
				goto out;
			}
			MLX5_SET64(modify_rq_in, in, modify_bitmask,
				   MLX5_MODIFY_RQ_IN_MODIFY_BITMASK_VSD);
			MLX5_SET(rqc, rqc, vsd,
				 (wq_attr->flags & IB_WQ_FLAGS_CVLAN_STRIPPING) ? 0 : 1);
		}

		if (wq_attr->flags_mask & IB_WQ_FLAGS_PCI_WRITE_END_PADDING) {
			mlx5_ib_dbg(dev, "Modifying scatter end padding is not supported\n");
			err = -EOPNOTSUPP;
			goto out;
		}
	}

	if (curr_wq_state == IB_WQS_RESET && wq_state == IB_WQS_RDY) {
		u16 set_id;

		set_id = mlx5_ib_get_counters_id(dev, 0);
		if (MLX5_CAP_GEN(dev->mdev, modify_rq_counter_set_id)) {
			MLX5_SET64(modify_rq_in, in, modify_bitmask,
				   MLX5_MODIFY_RQ_IN_MODIFY_BITMASK_RQ_COUNTER_SET_ID);
			MLX5_SET(rqc, rqc, counter_set_id, set_id);
		} else
			dev_info_once(
				&dev->ib_dev.dev,
				"Receive WQ counters are not supported on current FW\n");
	}

	err = mlx5_core_modify_rq(dev->mdev, rwq->core_qp.qpn, in);
	if (!err)
		rwq->ibwq.state = (wq_state == MLX5_RQC_STATE_ERR) ? IB_WQS_ERR : wq_state;

out:
	kvfree(in);
	return err;
}

struct mlx5_ib_drain_cqe {
	struct ib_cqe cqe;
	struct completion done;
};

static void mlx5_ib_drain_qp_done(struct ib_cq *cq, struct ib_wc *wc)
{
	struct mlx5_ib_drain_cqe *cqe = container_of(wc->wr_cqe,
						     struct mlx5_ib_drain_cqe,
						     cqe);

	complete(&cqe->done);
}

/* This function returns only once the drained WR was completed */
static void handle_drain_completion(struct ib_cq *cq,
				    struct mlx5_ib_drain_cqe *sdrain,
				    struct mlx5_ib_dev *dev)
{
	struct mlx5_core_dev *mdev = dev->mdev;

	if (cq->poll_ctx == IB_POLL_DIRECT) {
		while (wait_for_completion_timeout(&sdrain->done, HZ / 10) <= 0)
			ib_process_cq_direct(cq, -1);
		return;
	}

	if (mdev->state == MLX5_DEVICE_STATE_INTERNAL_ERROR) {
		struct mlx5_ib_cq *mcq = to_mcq(cq);
		bool triggered = false;
		unsigned long flags;

		spin_lock_irqsave(&dev->reset_flow_resource_lock, flags);
		/* Make sure that the CQ handler won't run if wasn't run yet */
		if (!mcq->mcq.reset_notify_added)
			mcq->mcq.reset_notify_added = 1;
		else
			triggered = true;
		spin_unlock_irqrestore(&dev->reset_flow_resource_lock, flags);

		if (triggered) {
			/* Wait for any scheduled/running task to be ended */
			switch (cq->poll_ctx) {
			case IB_POLL_SOFTIRQ:
				irq_poll_disable(&cq->iop);
				irq_poll_enable(&cq->iop);
				break;
			case IB_POLL_WORKQUEUE:
				cancel_work_sync(&cq->work);
				break;
			default:
				WARN_ON_ONCE(1);
			}
		}

		/* Run the CQ handler - this makes sure that the drain WR will
		 * be processed if wasn't processed yet.
		 */
		mcq->mcq.comp(&mcq->mcq, NULL);
	}

	wait_for_completion(&sdrain->done);
}

void mlx5_ib_drain_sq(struct ib_qp *qp)
{
	struct ib_cq *cq = qp->send_cq;
	struct ib_qp_attr attr = { .qp_state = IB_QPS_ERR };
	struct mlx5_ib_drain_cqe sdrain;
	const struct ib_send_wr *bad_swr;
	struct ib_rdma_wr swr = {
		.wr = {
			.next = NULL,
			{ .wr_cqe	= &sdrain.cqe, },
			.opcode	= IB_WR_RDMA_WRITE,
		},
	};
	int ret;
	struct mlx5_ib_dev *dev = to_mdev(qp->device);
	struct mlx5_core_dev *mdev = dev->mdev;

	ret = ib_modify_qp(qp, &attr, IB_QP_STATE);
	if (ret && mdev->state != MLX5_DEVICE_STATE_INTERNAL_ERROR) {
		WARN_ONCE(ret, "failed to drain send queue: %d\n", ret);
		return;
	}

	sdrain.cqe.done = mlx5_ib_drain_qp_done;
	init_completion(&sdrain.done);

	ret = mlx5_ib_post_send_drain(qp, &swr.wr, &bad_swr);
	if (ret) {
		WARN_ONCE(ret, "failed to drain send queue: %d\n", ret);
		return;
	}

	handle_drain_completion(cq, &sdrain, dev);
}

void mlx5_ib_drain_rq(struct ib_qp *qp)
{
	struct ib_cq *cq = qp->recv_cq;
	struct ib_qp_attr attr = { .qp_state = IB_QPS_ERR };
	struct mlx5_ib_drain_cqe rdrain;
	struct ib_recv_wr rwr = {};
	const struct ib_recv_wr *bad_rwr;
	int ret;
	struct mlx5_ib_dev *dev = to_mdev(qp->device);
	struct mlx5_core_dev *mdev = dev->mdev;

	ret = ib_modify_qp(qp, &attr, IB_QP_STATE);
	if (ret && mdev->state != MLX5_DEVICE_STATE_INTERNAL_ERROR) {
		WARN_ONCE(ret, "failed to drain recv queue: %d\n", ret);
		return;
	}

	rwr.wr_cqe = &rdrain.cqe;
	rdrain.cqe.done = mlx5_ib_drain_qp_done;
	init_completion(&rdrain.done);

	ret = mlx5_ib_post_recv_drain(qp, &rwr, &bad_rwr);
	if (ret) {
		WARN_ONCE(ret, "failed to drain recv queue: %d\n", ret);
		return;
	}

	handle_drain_completion(cq, &rdrain, dev);
}

/*
 * Bind a qp to a counter. If @counter is NULL then bind the qp to
 * the default counter
 */
int mlx5_ib_qp_set_counter(struct ib_qp *qp, struct rdma_counter *counter)
{
	struct mlx5_ib_dev *dev = to_mdev(qp->device);
	struct mlx5_ib_qp *mqp = to_mqp(qp);
	int err = 0;

	mutex_lock(&mqp->mutex);
	if (mqp->state == IB_QPS_RESET) {
		qp->counter = counter;
		goto out;
	}

	if (!MLX5_CAP_GEN(dev->mdev, rts2rts_qp_counters_set_id)) {
		err = -EOPNOTSUPP;
		goto out;
	}

	if (mqp->state == IB_QPS_RTS) {
		err = __mlx5_ib_qp_set_counter(qp, counter);
		if (!err)
			qp->counter = counter;

		goto out;
	}

	mqp->counter_pending = 1;
	qp->counter = counter;

out:
	mutex_unlock(&mqp->mutex);
	return err;
}

int mlx5_ib_qp_event_init(void)
{
	mlx5_ib_qp_event_wq = alloc_ordered_workqueue("mlx5_ib_qp_event_wq", 0);
	if (!mlx5_ib_qp_event_wq)
		return -ENOMEM;

	return 0;
}

void mlx5_ib_qp_event_cleanup(void)
{
	destroy_workqueue(mlx5_ib_qp_event_wq);
}
