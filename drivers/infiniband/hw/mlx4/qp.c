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

#include <linux/log2.h>
#include <linux/etherdevice.h>
#include <net/ip.h>
#include <linux/slab.h>
#include <linux/netdevice.h>

#include <rdma/ib_cache.h>
#include <rdma/ib_pack.h>
#include <rdma/ib_addr.h>
#include <rdma/ib_mad.h>

#include <linux/mlx4/driver.h>
#include <linux/mlx4/qp.h>

#include "mlx4_ib.h"
#include <rdma/mlx4-abi.h>

static void mlx4_ib_lock_cqs(struct mlx4_ib_cq *send_cq,
			     struct mlx4_ib_cq *recv_cq);
static void mlx4_ib_unlock_cqs(struct mlx4_ib_cq *send_cq,
			       struct mlx4_ib_cq *recv_cq);
static int _mlx4_ib_modify_wq(struct ib_wq *ibwq, enum ib_wq_state new_state);

enum {
	MLX4_IB_ACK_REQ_FREQ	= 8,
};

enum {
	MLX4_IB_DEFAULT_SCHED_QUEUE	= 0x83,
	MLX4_IB_DEFAULT_QP0_SCHED_QUEUE	= 0x3f,
	MLX4_IB_LINK_TYPE_IB		= 0,
	MLX4_IB_LINK_TYPE_ETH		= 1
};

enum {
	/*
	 * Largest possible UD header: send with GRH and immediate
	 * data plus 18 bytes for an Ethernet header with VLAN/802.1Q
	 * tag.  (LRH would only use 8 bytes, so Ethernet is the
	 * biggest case)
	 */
	MLX4_IB_UD_HEADER_SIZE		= 82,
	MLX4_IB_LSO_HEADER_SPARE	= 128,
};

struct mlx4_ib_sqp {
	struct mlx4_ib_qp	qp;
	int			pkey_index;
	u32			qkey;
	u32			send_psn;
	struct ib_ud_header	ud_header;
	u8			header_buf[MLX4_IB_UD_HEADER_SIZE];
	struct ib_qp		*roce_v2_gsi;
};

enum {
	MLX4_IB_MIN_SQ_STRIDE	= 6,
	MLX4_IB_CACHE_LINE_SIZE	= 64,
};

enum {
	MLX4_RAW_QP_MTU		= 7,
	MLX4_RAW_QP_MSGMAX	= 31,
};

#ifndef ETH_ALEN
#define ETH_ALEN        6
#endif

static const __be32 mlx4_ib_opcode[] = {
	[IB_WR_SEND]				= cpu_to_be32(MLX4_OPCODE_SEND),
	[IB_WR_LSO]				= cpu_to_be32(MLX4_OPCODE_LSO),
	[IB_WR_SEND_WITH_IMM]			= cpu_to_be32(MLX4_OPCODE_SEND_IMM),
	[IB_WR_RDMA_WRITE]			= cpu_to_be32(MLX4_OPCODE_RDMA_WRITE),
	[IB_WR_RDMA_WRITE_WITH_IMM]		= cpu_to_be32(MLX4_OPCODE_RDMA_WRITE_IMM),
	[IB_WR_RDMA_READ]			= cpu_to_be32(MLX4_OPCODE_RDMA_READ),
	[IB_WR_ATOMIC_CMP_AND_SWP]		= cpu_to_be32(MLX4_OPCODE_ATOMIC_CS),
	[IB_WR_ATOMIC_FETCH_AND_ADD]		= cpu_to_be32(MLX4_OPCODE_ATOMIC_FA),
	[IB_WR_SEND_WITH_INV]			= cpu_to_be32(MLX4_OPCODE_SEND_INVAL),
	[IB_WR_LOCAL_INV]			= cpu_to_be32(MLX4_OPCODE_LOCAL_INVAL),
	[IB_WR_REG_MR]				= cpu_to_be32(MLX4_OPCODE_FMR),
	[IB_WR_MASKED_ATOMIC_CMP_AND_SWP]	= cpu_to_be32(MLX4_OPCODE_MASKED_ATOMIC_CS),
	[IB_WR_MASKED_ATOMIC_FETCH_AND_ADD]	= cpu_to_be32(MLX4_OPCODE_MASKED_ATOMIC_FA),
};

enum mlx4_ib_source_type {
	MLX4_IB_QP_SRC	= 0,
	MLX4_IB_RWQ_SRC	= 1,
};

static struct mlx4_ib_sqp *to_msqp(struct mlx4_ib_qp *mqp)
{
	return container_of(mqp, struct mlx4_ib_sqp, qp);
}

static int is_tunnel_qp(struct mlx4_ib_dev *dev, struct mlx4_ib_qp *qp)
{
	if (!mlx4_is_master(dev->dev))
		return 0;

	return qp->mqp.qpn >= dev->dev->phys_caps.base_tunnel_sqpn &&
	       qp->mqp.qpn < dev->dev->phys_caps.base_tunnel_sqpn +
		8 * MLX4_MFUNC_MAX;
}

static int is_sqp(struct mlx4_ib_dev *dev, struct mlx4_ib_qp *qp)
{
	int proxy_sqp = 0;
	int real_sqp = 0;
	int i;
	/* PPF or Native -- real SQP */
	real_sqp = ((mlx4_is_master(dev->dev) || !mlx4_is_mfunc(dev->dev)) &&
		    qp->mqp.qpn >= dev->dev->phys_caps.base_sqpn &&
		    qp->mqp.qpn <= dev->dev->phys_caps.base_sqpn + 3);
	if (real_sqp)
		return 1;
	/* VF or PF -- proxy SQP */
	if (mlx4_is_mfunc(dev->dev)) {
		for (i = 0; i < dev->dev->caps.num_ports; i++) {
			if (qp->mqp.qpn == dev->dev->caps.spec_qps[i].qp0_proxy ||
			    qp->mqp.qpn == dev->dev->caps.spec_qps[i].qp1_proxy) {
				proxy_sqp = 1;
				break;
			}
		}
	}
	if (proxy_sqp)
		return 1;

	return !!(qp->flags & MLX4_IB_ROCE_V2_GSI_QP);
}

/* used for INIT/CLOSE port logic */
static int is_qp0(struct mlx4_ib_dev *dev, struct mlx4_ib_qp *qp)
{
	int proxy_qp0 = 0;
	int real_qp0 = 0;
	int i;
	/* PPF or Native -- real QP0 */
	real_qp0 = ((mlx4_is_master(dev->dev) || !mlx4_is_mfunc(dev->dev)) &&
		    qp->mqp.qpn >= dev->dev->phys_caps.base_sqpn &&
		    qp->mqp.qpn <= dev->dev->phys_caps.base_sqpn + 1);
	if (real_qp0)
		return 1;
	/* VF or PF -- proxy QP0 */
	if (mlx4_is_mfunc(dev->dev)) {
		for (i = 0; i < dev->dev->caps.num_ports; i++) {
			if (qp->mqp.qpn == dev->dev->caps.spec_qps[i].qp0_proxy) {
				proxy_qp0 = 1;
				break;
			}
		}
	}
	return proxy_qp0;
}

static void *get_wqe(struct mlx4_ib_qp *qp, int offset)
{
	return mlx4_buf_offset(&qp->buf, offset);
}

static void *get_recv_wqe(struct mlx4_ib_qp *qp, int n)
{
	return get_wqe(qp, qp->rq.offset + (n << qp->rq.wqe_shift));
}

static void *get_send_wqe(struct mlx4_ib_qp *qp, int n)
{
	return get_wqe(qp, qp->sq.offset + (n << qp->sq.wqe_shift));
}

/*
 * Stamp a SQ WQE so that it is invalid if prefetched by marking the
 * first four bytes of every 64 byte chunk with
 *     0x7FFFFFF | (invalid_ownership_value << 31).
 *
 * When the max work request size is less than or equal to the WQE
 * basic block size, as an optimization, we can stamp all WQEs with
 * 0xffffffff, and skip the very first chunk of each WQE.
 */
static void stamp_send_wqe(struct mlx4_ib_qp *qp, int n, int size)
{
	__be32 *wqe;
	int i;
	int s;
	int ind;
	void *buf;
	__be32 stamp;
	struct mlx4_wqe_ctrl_seg *ctrl;

	if (qp->sq_max_wqes_per_wr > 1) {
		s = roundup(size, 1U << qp->sq.wqe_shift);
		for (i = 0; i < s; i += 64) {
			ind = (i >> qp->sq.wqe_shift) + n;
			stamp = ind & qp->sq.wqe_cnt ? cpu_to_be32(0x7fffffff) :
						       cpu_to_be32(0xffffffff);
			buf = get_send_wqe(qp, ind & (qp->sq.wqe_cnt - 1));
			wqe = buf + (i & ((1 << qp->sq.wqe_shift) - 1));
			*wqe = stamp;
		}
	} else {
		ctrl = buf = get_send_wqe(qp, n & (qp->sq.wqe_cnt - 1));
		s = (ctrl->qpn_vlan.fence_size & 0x3f) << 4;
		for (i = 64; i < s; i += 64) {
			wqe = buf + i;
			*wqe = cpu_to_be32(0xffffffff);
		}
	}
}

static void post_nop_wqe(struct mlx4_ib_qp *qp, int n, int size)
{
	struct mlx4_wqe_ctrl_seg *ctrl;
	struct mlx4_wqe_inline_seg *inl;
	void *wqe;
	int s;

	ctrl = wqe = get_send_wqe(qp, n & (qp->sq.wqe_cnt - 1));
	s = sizeof(struct mlx4_wqe_ctrl_seg);

	if (qp->ibqp.qp_type == IB_QPT_UD) {
		struct mlx4_wqe_datagram_seg *dgram = wqe + sizeof *ctrl;
		struct mlx4_av *av = (struct mlx4_av *)dgram->av;
		memset(dgram, 0, sizeof *dgram);
		av->port_pd = cpu_to_be32((qp->port << 24) | to_mpd(qp->ibqp.pd)->pdn);
		s += sizeof(struct mlx4_wqe_datagram_seg);
	}

	/* Pad the remainder of the WQE with an inline data segment. */
	if (size > s) {
		inl = wqe + s;
		inl->byte_count = cpu_to_be32(1 << 31 | (size - s - sizeof *inl));
	}
	ctrl->srcrb_flags = 0;
	ctrl->qpn_vlan.fence_size = size / 16;
	/*
	 * Make sure descriptor is fully written before setting ownership bit
	 * (because HW can start executing as soon as we do).
	 */
	wmb();

	ctrl->owner_opcode = cpu_to_be32(MLX4_OPCODE_NOP | MLX4_WQE_CTRL_NEC) |
		(n & qp->sq.wqe_cnt ? cpu_to_be32(1 << 31) : 0);

	stamp_send_wqe(qp, n + qp->sq_spare_wqes, size);
}

/* Post NOP WQE to prevent wrap-around in the middle of WR */
static inline unsigned pad_wraparound(struct mlx4_ib_qp *qp, int ind)
{
	unsigned s = qp->sq.wqe_cnt - (ind & (qp->sq.wqe_cnt - 1));
	if (unlikely(s < qp->sq_max_wqes_per_wr)) {
		post_nop_wqe(qp, ind, s << qp->sq.wqe_shift);
		ind += s;
	}
	return ind;
}

static void mlx4_ib_qp_event(struct mlx4_qp *qp, enum mlx4_event type)
{
	struct ib_event event;
	struct ib_qp *ibqp = &to_mibqp(qp)->ibqp;

	if (type == MLX4_EVENT_TYPE_PATH_MIG)
		to_mibqp(qp)->port = to_mibqp(qp)->alt_port;

	if (ibqp->event_handler) {
		event.device     = ibqp->device;
		event.element.qp = ibqp;
		switch (type) {
		case MLX4_EVENT_TYPE_PATH_MIG:
			event.event = IB_EVENT_PATH_MIG;
			break;
		case MLX4_EVENT_TYPE_COMM_EST:
			event.event = IB_EVENT_COMM_EST;
			break;
		case MLX4_EVENT_TYPE_SQ_DRAINED:
			event.event = IB_EVENT_SQ_DRAINED;
			break;
		case MLX4_EVENT_TYPE_SRQ_QP_LAST_WQE:
			event.event = IB_EVENT_QP_LAST_WQE_REACHED;
			break;
		case MLX4_EVENT_TYPE_WQ_CATAS_ERROR:
			event.event = IB_EVENT_QP_FATAL;
			break;
		case MLX4_EVENT_TYPE_PATH_MIG_FAILED:
			event.event = IB_EVENT_PATH_MIG_ERR;
			break;
		case MLX4_EVENT_TYPE_WQ_INVAL_REQ_ERROR:
			event.event = IB_EVENT_QP_REQ_ERR;
			break;
		case MLX4_EVENT_TYPE_WQ_ACCESS_ERROR:
			event.event = IB_EVENT_QP_ACCESS_ERR;
			break;
		default:
			pr_warn("Unexpected event type %d "
			       "on QP %06x\n", type, qp->qpn);
			return;
		}

		ibqp->event_handler(&event, ibqp->qp_context);
	}
}

static void mlx4_ib_wq_event(struct mlx4_qp *qp, enum mlx4_event type)
{
	pr_warn_ratelimited("Unexpected event type %d on WQ 0x%06x. Events are not supported for WQs\n",
			    type, qp->qpn);
}

static int send_wqe_overhead(enum mlx4_ib_qp_type type, u32 flags)
{
	/*
	 * UD WQEs must have a datagram segment.
	 * RC and UC WQEs might have a remote address segment.
	 * MLX WQEs need two extra inline data segments (for the UD
	 * header and space for the ICRC).
	 */
	switch (type) {
	case MLX4_IB_QPT_UD:
		return sizeof (struct mlx4_wqe_ctrl_seg) +
			sizeof (struct mlx4_wqe_datagram_seg) +
			((flags & MLX4_IB_QP_LSO) ? MLX4_IB_LSO_HEADER_SPARE : 0);
	case MLX4_IB_QPT_PROXY_SMI_OWNER:
	case MLX4_IB_QPT_PROXY_SMI:
	case MLX4_IB_QPT_PROXY_GSI:
		return sizeof (struct mlx4_wqe_ctrl_seg) +
			sizeof (struct mlx4_wqe_datagram_seg) + 64;
	case MLX4_IB_QPT_TUN_SMI_OWNER:
	case MLX4_IB_QPT_TUN_GSI:
		return sizeof (struct mlx4_wqe_ctrl_seg) +
			sizeof (struct mlx4_wqe_datagram_seg);

	case MLX4_IB_QPT_UC:
		return sizeof (struct mlx4_wqe_ctrl_seg) +
			sizeof (struct mlx4_wqe_raddr_seg);
	case MLX4_IB_QPT_RC:
		return sizeof (struct mlx4_wqe_ctrl_seg) +
			sizeof (struct mlx4_wqe_masked_atomic_seg) +
			sizeof (struct mlx4_wqe_raddr_seg);
	case MLX4_IB_QPT_SMI:
	case MLX4_IB_QPT_GSI:
		return sizeof (struct mlx4_wqe_ctrl_seg) +
			ALIGN(MLX4_IB_UD_HEADER_SIZE +
			      DIV_ROUND_UP(MLX4_IB_UD_HEADER_SIZE,
					   MLX4_INLINE_ALIGN) *
			      sizeof (struct mlx4_wqe_inline_seg),
			      sizeof (struct mlx4_wqe_data_seg)) +
			ALIGN(4 +
			      sizeof (struct mlx4_wqe_inline_seg),
			      sizeof (struct mlx4_wqe_data_seg));
	default:
		return sizeof (struct mlx4_wqe_ctrl_seg);
	}
}

static int set_rq_size(struct mlx4_ib_dev *dev, struct ib_qp_cap *cap,
		       int is_user, int has_rq, struct mlx4_ib_qp *qp,
		       u32 inl_recv_sz)
{
	/* Sanity check RQ size before proceeding */
	if (cap->max_recv_wr > dev->dev->caps.max_wqes - MLX4_IB_SQ_MAX_SPARE ||
	    cap->max_recv_sge > min(dev->dev->caps.max_sq_sg, dev->dev->caps.max_rq_sg))
		return -EINVAL;

	if (!has_rq) {
		if (cap->max_recv_wr || inl_recv_sz)
			return -EINVAL;

		qp->rq.wqe_cnt = qp->rq.max_gs = 0;
	} else {
		u32 max_inl_recv_sz = dev->dev->caps.max_rq_sg *
			sizeof(struct mlx4_wqe_data_seg);
		u32 wqe_size;

		/* HW requires >= 1 RQ entry with >= 1 gather entry */
		if (is_user && (!cap->max_recv_wr || !cap->max_recv_sge ||
				inl_recv_sz > max_inl_recv_sz))
			return -EINVAL;

		qp->rq.wqe_cnt	 = roundup_pow_of_two(max(1U, cap->max_recv_wr));
		qp->rq.max_gs	 = roundup_pow_of_two(max(1U, cap->max_recv_sge));
		wqe_size = qp->rq.max_gs * sizeof(struct mlx4_wqe_data_seg);
		qp->rq.wqe_shift = ilog2(max_t(u32, wqe_size, inl_recv_sz));
	}

	/* leave userspace return values as they were, so as not to break ABI */
	if (is_user) {
		cap->max_recv_wr  = qp->rq.max_post = qp->rq.wqe_cnt;
		cap->max_recv_sge = qp->rq.max_gs;
	} else {
		cap->max_recv_wr  = qp->rq.max_post =
			min(dev->dev->caps.max_wqes - MLX4_IB_SQ_MAX_SPARE, qp->rq.wqe_cnt);
		cap->max_recv_sge = min(qp->rq.max_gs,
					min(dev->dev->caps.max_sq_sg,
					    dev->dev->caps.max_rq_sg));
	}

	return 0;
}

static int set_kernel_sq_size(struct mlx4_ib_dev *dev, struct ib_qp_cap *cap,
			      enum mlx4_ib_qp_type type, struct mlx4_ib_qp *qp,
			      bool shrink_wqe)
{
	int s;

	/* Sanity check SQ size before proceeding */
	if (cap->max_send_wr  > (dev->dev->caps.max_wqes - MLX4_IB_SQ_MAX_SPARE) ||
	    cap->max_send_sge > min(dev->dev->caps.max_sq_sg, dev->dev->caps.max_rq_sg) ||
	    cap->max_inline_data + send_wqe_overhead(type, qp->flags) +
	    sizeof (struct mlx4_wqe_inline_seg) > dev->dev->caps.max_sq_desc_sz)
		return -EINVAL;

	/*
	 * For MLX transport we need 2 extra S/G entries:
	 * one for the header and one for the checksum at the end
	 */
	if ((type == MLX4_IB_QPT_SMI || type == MLX4_IB_QPT_GSI ||
	     type & (MLX4_IB_QPT_PROXY_SMI_OWNER | MLX4_IB_QPT_TUN_SMI_OWNER)) &&
	    cap->max_send_sge + 2 > dev->dev->caps.max_sq_sg)
		return -EINVAL;

	s = max(cap->max_send_sge * sizeof (struct mlx4_wqe_data_seg),
		cap->max_inline_data + sizeof (struct mlx4_wqe_inline_seg)) +
		send_wqe_overhead(type, qp->flags);

	if (s > dev->dev->caps.max_sq_desc_sz)
		return -EINVAL;

	/*
	 * Hermon supports shrinking WQEs, such that a single work
	 * request can include multiple units of 1 << wqe_shift.  This
	 * way, work requests can differ in size, and do not have to
	 * be a power of 2 in size, saving memory and speeding up send
	 * WR posting.  Unfortunately, if we do this then the
	 * wqe_index field in CQEs can't be used to look up the WR ID
	 * anymore, so we do this only if selective signaling is off.
	 *
	 * Further, on 32-bit platforms, we can't use vmap() to make
	 * the QP buffer virtually contiguous.  Thus we have to use
	 * constant-sized WRs to make sure a WR is always fully within
	 * a single page-sized chunk.
	 *
	 * Finally, we use NOP work requests to pad the end of the
	 * work queue, to avoid wrap-around in the middle of WR.  We
	 * set NEC bit to avoid getting completions with error for
	 * these NOP WRs, but since NEC is only supported starting
	 * with firmware 2.2.232, we use constant-sized WRs for older
	 * firmware.
	 *
	 * And, since MLX QPs only support SEND, we use constant-sized
	 * WRs in this case.
	 *
	 * We look for the smallest value of wqe_shift such that the
	 * resulting number of wqes does not exceed device
	 * capabilities.
	 *
	 * We set WQE size to at least 64 bytes, this way stamping
	 * invalidates each WQE.
	 */
	if (shrink_wqe && dev->dev->caps.fw_ver >= MLX4_FW_VER_WQE_CTRL_NEC &&
	    qp->sq_signal_bits && BITS_PER_LONG == 64 &&
	    type != MLX4_IB_QPT_SMI && type != MLX4_IB_QPT_GSI &&
	    !(type & (MLX4_IB_QPT_PROXY_SMI_OWNER | MLX4_IB_QPT_PROXY_SMI |
		      MLX4_IB_QPT_PROXY_GSI | MLX4_IB_QPT_TUN_SMI_OWNER)))
		qp->sq.wqe_shift = ilog2(64);
	else
		qp->sq.wqe_shift = ilog2(roundup_pow_of_two(s));

	for (;;) {
		qp->sq_max_wqes_per_wr = DIV_ROUND_UP(s, 1U << qp->sq.wqe_shift);

		/*
		 * We need to leave 2 KB + 1 WR of headroom in the SQ to
		 * allow HW to prefetch.
		 */
		qp->sq_spare_wqes = (2048 >> qp->sq.wqe_shift) + qp->sq_max_wqes_per_wr;
		qp->sq.wqe_cnt = roundup_pow_of_two(cap->max_send_wr *
						    qp->sq_max_wqes_per_wr +
						    qp->sq_spare_wqes);

		if (qp->sq.wqe_cnt <= dev->dev->caps.max_wqes)
			break;

		if (qp->sq_max_wqes_per_wr <= 1)
			return -EINVAL;

		++qp->sq.wqe_shift;
	}

	qp->sq.max_gs = (min(dev->dev->caps.max_sq_desc_sz,
			     (qp->sq_max_wqes_per_wr << qp->sq.wqe_shift)) -
			 send_wqe_overhead(type, qp->flags)) /
		sizeof (struct mlx4_wqe_data_seg);

	qp->buf_size = (qp->rq.wqe_cnt << qp->rq.wqe_shift) +
		(qp->sq.wqe_cnt << qp->sq.wqe_shift);
	if (qp->rq.wqe_shift > qp->sq.wqe_shift) {
		qp->rq.offset = 0;
		qp->sq.offset = qp->rq.wqe_cnt << qp->rq.wqe_shift;
	} else {
		qp->rq.offset = qp->sq.wqe_cnt << qp->sq.wqe_shift;
		qp->sq.offset = 0;
	}

	cap->max_send_wr  = qp->sq.max_post =
		(qp->sq.wqe_cnt - qp->sq_spare_wqes) / qp->sq_max_wqes_per_wr;
	cap->max_send_sge = min(qp->sq.max_gs,
				min(dev->dev->caps.max_sq_sg,
				    dev->dev->caps.max_rq_sg));
	/* We don't support inline sends for kernel QPs (yet) */
	cap->max_inline_data = 0;

	return 0;
}

static int set_user_sq_size(struct mlx4_ib_dev *dev,
			    struct mlx4_ib_qp *qp,
			    struct mlx4_ib_create_qp *ucmd)
{
	/* Sanity check SQ size before proceeding */
	if ((1 << ucmd->log_sq_bb_count) > dev->dev->caps.max_wqes	 ||
	    ucmd->log_sq_stride >
		ilog2(roundup_pow_of_two(dev->dev->caps.max_sq_desc_sz)) ||
	    ucmd->log_sq_stride < MLX4_IB_MIN_SQ_STRIDE)
		return -EINVAL;

	qp->sq.wqe_cnt   = 1 << ucmd->log_sq_bb_count;
	qp->sq.wqe_shift = ucmd->log_sq_stride;

	qp->buf_size = (qp->rq.wqe_cnt << qp->rq.wqe_shift) +
		(qp->sq.wqe_cnt << qp->sq.wqe_shift);

	return 0;
}

static int alloc_proxy_bufs(struct ib_device *dev, struct mlx4_ib_qp *qp)
{
	int i;

	qp->sqp_proxy_rcv =
		kmalloc(sizeof (struct mlx4_ib_buf) * qp->rq.wqe_cnt,
			GFP_KERNEL);
	if (!qp->sqp_proxy_rcv)
		return -ENOMEM;
	for (i = 0; i < qp->rq.wqe_cnt; i++) {
		qp->sqp_proxy_rcv[i].addr =
			kmalloc(sizeof (struct mlx4_ib_proxy_sqp_hdr),
				GFP_KERNEL);
		if (!qp->sqp_proxy_rcv[i].addr)
			goto err;
		qp->sqp_proxy_rcv[i].map =
			ib_dma_map_single(dev, qp->sqp_proxy_rcv[i].addr,
					  sizeof (struct mlx4_ib_proxy_sqp_hdr),
					  DMA_FROM_DEVICE);
		if (ib_dma_mapping_error(dev, qp->sqp_proxy_rcv[i].map)) {
			kfree(qp->sqp_proxy_rcv[i].addr);
			goto err;
		}
	}
	return 0;

err:
	while (i > 0) {
		--i;
		ib_dma_unmap_single(dev, qp->sqp_proxy_rcv[i].map,
				    sizeof (struct mlx4_ib_proxy_sqp_hdr),
				    DMA_FROM_DEVICE);
		kfree(qp->sqp_proxy_rcv[i].addr);
	}
	kfree(qp->sqp_proxy_rcv);
	qp->sqp_proxy_rcv = NULL;
	return -ENOMEM;
}

static void free_proxy_bufs(struct ib_device *dev, struct mlx4_ib_qp *qp)
{
	int i;

	for (i = 0; i < qp->rq.wqe_cnt; i++) {
		ib_dma_unmap_single(dev, qp->sqp_proxy_rcv[i].map,
				    sizeof (struct mlx4_ib_proxy_sqp_hdr),
				    DMA_FROM_DEVICE);
		kfree(qp->sqp_proxy_rcv[i].addr);
	}
	kfree(qp->sqp_proxy_rcv);
}

static int qp_has_rq(struct ib_qp_init_attr *attr)
{
	if (attr->qp_type == IB_QPT_XRC_INI || attr->qp_type == IB_QPT_XRC_TGT)
		return 0;

	return !attr->srq;
}

static int qp0_enabled_vf(struct mlx4_dev *dev, int qpn)
{
	int i;
	for (i = 0; i < dev->caps.num_ports; i++) {
		if (qpn == dev->caps.spec_qps[i].qp0_proxy)
			return !!dev->caps.spec_qps[i].qp0_qkey;
	}
	return 0;
}

static void mlx4_ib_free_qp_counter(struct mlx4_ib_dev *dev,
				    struct mlx4_ib_qp *qp)
{
	mutex_lock(&dev->counters_table[qp->port - 1].mutex);
	mlx4_counter_free(dev->dev, qp->counter_index->index);
	list_del(&qp->counter_index->list);
	mutex_unlock(&dev->counters_table[qp->port - 1].mutex);

	kfree(qp->counter_index);
	qp->counter_index = NULL;
}

static int set_qp_rss(struct mlx4_ib_dev *dev, struct mlx4_ib_rss *rss_ctx,
		      struct ib_qp_init_attr *init_attr,
		      struct mlx4_ib_create_qp_rss *ucmd)
{
	rss_ctx->base_qpn_tbl_sz = init_attr->rwq_ind_tbl->ind_tbl[0]->wq_num |
		(init_attr->rwq_ind_tbl->log_ind_tbl_size << 24);

	if ((ucmd->rx_hash_function == MLX4_IB_RX_HASH_FUNC_TOEPLITZ) &&
	    (dev->dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_RSS_TOP)) {
		memcpy(rss_ctx->rss_key, ucmd->rx_hash_key,
		       MLX4_EN_RSS_KEY_SIZE);
	} else {
		pr_debug("RX Hash function is not supported\n");
		return (-EOPNOTSUPP);
	}

	if (ucmd->rx_hash_fields_mask & ~(MLX4_IB_RX_HASH_SRC_IPV4	|
					  MLX4_IB_RX_HASH_DST_IPV4	|
					  MLX4_IB_RX_HASH_SRC_IPV6	|
					  MLX4_IB_RX_HASH_DST_IPV6	|
					  MLX4_IB_RX_HASH_SRC_PORT_TCP	|
					  MLX4_IB_RX_HASH_DST_PORT_TCP	|
					  MLX4_IB_RX_HASH_SRC_PORT_UDP	|
					  MLX4_IB_RX_HASH_DST_PORT_UDP)) {
		pr_debug("RX Hash fields_mask has unsupported mask (0x%llx)\n",
			 ucmd->rx_hash_fields_mask);
		return (-EOPNOTSUPP);
	}

	if ((ucmd->rx_hash_fields_mask & MLX4_IB_RX_HASH_SRC_IPV4) &&
	    (ucmd->rx_hash_fields_mask & MLX4_IB_RX_HASH_DST_IPV4)) {
		rss_ctx->flags = MLX4_RSS_IPV4;
	} else if ((ucmd->rx_hash_fields_mask & MLX4_IB_RX_HASH_SRC_IPV4) ||
		   (ucmd->rx_hash_fields_mask & MLX4_IB_RX_HASH_DST_IPV4)) {
		pr_debug("RX Hash fields_mask is not supported - both IPv4 SRC and DST must be set\n");
		return (-EOPNOTSUPP);
	}

	if ((ucmd->rx_hash_fields_mask & MLX4_IB_RX_HASH_SRC_IPV6) &&
	    (ucmd->rx_hash_fields_mask & MLX4_IB_RX_HASH_DST_IPV6)) {
		rss_ctx->flags |= MLX4_RSS_IPV6;
	} else if ((ucmd->rx_hash_fields_mask & MLX4_IB_RX_HASH_SRC_IPV6) ||
		   (ucmd->rx_hash_fields_mask & MLX4_IB_RX_HASH_DST_IPV6)) {
		pr_debug("RX Hash fields_mask is not supported - both IPv6 SRC and DST must be set\n");
		return (-EOPNOTSUPP);
	}

	if ((ucmd->rx_hash_fields_mask & MLX4_IB_RX_HASH_SRC_PORT_UDP) &&
	    (ucmd->rx_hash_fields_mask & MLX4_IB_RX_HASH_DST_PORT_UDP)) {
		if (!(dev->dev->caps.flags & MLX4_DEV_CAP_FLAG_UDP_RSS)) {
			pr_debug("RX Hash fields_mask for UDP is not supported\n");
			return (-EOPNOTSUPP);
		}

		if (rss_ctx->flags & MLX4_RSS_IPV4)
			rss_ctx->flags |= MLX4_RSS_UDP_IPV4;
		if (rss_ctx->flags & MLX4_RSS_IPV6)
			rss_ctx->flags |= MLX4_RSS_UDP_IPV6;
		if (!(rss_ctx->flags & (MLX4_RSS_IPV6 | MLX4_RSS_IPV4))) {
			pr_debug("RX Hash fields_mask is not supported - UDP must be set with IPv4 or IPv6\n");
			return (-EOPNOTSUPP);
		}
	} else if ((ucmd->rx_hash_fields_mask & MLX4_IB_RX_HASH_SRC_PORT_UDP) ||
		   (ucmd->rx_hash_fields_mask & MLX4_IB_RX_HASH_DST_PORT_UDP)) {
		pr_debug("RX Hash fields_mask is not supported - both UDP SRC and DST must be set\n");
		return (-EOPNOTSUPP);
	}

	if ((ucmd->rx_hash_fields_mask & MLX4_IB_RX_HASH_SRC_PORT_TCP) &&
	    (ucmd->rx_hash_fields_mask & MLX4_IB_RX_HASH_DST_PORT_TCP)) {
		if (rss_ctx->flags & MLX4_RSS_IPV4)
			rss_ctx->flags |= MLX4_RSS_TCP_IPV4;
		if (rss_ctx->flags & MLX4_RSS_IPV6)
			rss_ctx->flags |= MLX4_RSS_TCP_IPV6;
		if (!(rss_ctx->flags & (MLX4_RSS_IPV6 | MLX4_RSS_IPV4))) {
			pr_debug("RX Hash fields_mask is not supported - TCP must be set with IPv4 or IPv6\n");
			return (-EOPNOTSUPP);
		}
	} else if ((ucmd->rx_hash_fields_mask & MLX4_IB_RX_HASH_SRC_PORT_TCP) ||
		   (ucmd->rx_hash_fields_mask & MLX4_IB_RX_HASH_DST_PORT_TCP)) {
		pr_debug("RX Hash fields_mask is not supported - both TCP SRC and DST must be set\n");
		return (-EOPNOTSUPP);
	}

	if (ucmd->rx_hash_fields_mask & MLX4_IB_RX_HASH_INNER) {
		if (dev->dev->caps.tunnel_offload_mode ==
		    MLX4_TUNNEL_OFFLOAD_MODE_VXLAN) {
			/*
			 * Hash according to inner headers if exist, otherwise
			 * according to outer headers.
			 */
			rss_ctx->flags |= MLX4_RSS_BY_INNER_HEADERS_IPONLY;
		} else {
			pr_debug("RSS Hash for inner headers isn't supported\n");
			return (-EOPNOTSUPP);
		}
	}

	return 0;
}

static int create_qp_rss(struct mlx4_ib_dev *dev,
			 struct ib_qp_init_attr *init_attr,
			 struct mlx4_ib_create_qp_rss *ucmd,
			 struct mlx4_ib_qp *qp)
{
	int qpn;
	int err;

	qp->mqp.usage = MLX4_RES_USAGE_USER_VERBS;

	err = mlx4_qp_reserve_range(dev->dev, 1, 1, &qpn, 0, qp->mqp.usage);
	if (err)
		return err;

	err = mlx4_qp_alloc(dev->dev, qpn, &qp->mqp);
	if (err)
		goto err_qpn;

	mutex_init(&qp->mutex);

	INIT_LIST_HEAD(&qp->gid_list);
	INIT_LIST_HEAD(&qp->steering_rules);

	qp->mlx4_ib_qp_type = MLX4_IB_QPT_RAW_PACKET;
	qp->state = IB_QPS_RESET;

	/* Set dummy send resources to be compatible with HV and PRM */
	qp->sq_no_prefetch = 1;
	qp->sq.wqe_cnt = 1;
	qp->sq.wqe_shift = MLX4_IB_MIN_SQ_STRIDE;
	qp->buf_size = qp->sq.wqe_cnt << MLX4_IB_MIN_SQ_STRIDE;
	qp->mtt = (to_mqp(
		   (struct ib_qp *)init_attr->rwq_ind_tbl->ind_tbl[0]))->mtt;

	qp->rss_ctx = kzalloc(sizeof(*qp->rss_ctx), GFP_KERNEL);
	if (!qp->rss_ctx) {
		err = -ENOMEM;
		goto err_qp_alloc;
	}

	err = set_qp_rss(dev, qp->rss_ctx, init_attr, ucmd);
	if (err)
		goto err;

	return 0;

err:
	kfree(qp->rss_ctx);

err_qp_alloc:
	mlx4_qp_remove(dev->dev, &qp->mqp);
	mlx4_qp_free(dev->dev, &qp->mqp);

err_qpn:
	mlx4_qp_release_range(dev->dev, qpn, 1);
	return err;
}

static struct ib_qp *_mlx4_ib_create_qp_rss(struct ib_pd *pd,
					    struct ib_qp_init_attr *init_attr,
					    struct ib_udata *udata)
{
	struct mlx4_ib_qp *qp;
	struct mlx4_ib_create_qp_rss ucmd = {};
	size_t required_cmd_sz;
	int err;

	if (!udata) {
		pr_debug("RSS QP with NULL udata\n");
		return ERR_PTR(-EINVAL);
	}

	if (udata->outlen)
		return ERR_PTR(-EOPNOTSUPP);

	required_cmd_sz = offsetof(typeof(ucmd), reserved1) +
					sizeof(ucmd.reserved1);
	if (udata->inlen < required_cmd_sz) {
		pr_debug("invalid inlen\n");
		return ERR_PTR(-EINVAL);
	}

	if (ib_copy_from_udata(&ucmd, udata, min(sizeof(ucmd), udata->inlen))) {
		pr_debug("copy failed\n");
		return ERR_PTR(-EFAULT);
	}

	if (memchr_inv(ucmd.reserved, 0, sizeof(ucmd.reserved)))
		return ERR_PTR(-EOPNOTSUPP);

	if (ucmd.comp_mask || ucmd.reserved1)
		return ERR_PTR(-EOPNOTSUPP);

	if (udata->inlen > sizeof(ucmd) &&
	    !ib_is_udata_cleared(udata, sizeof(ucmd),
				 udata->inlen - sizeof(ucmd))) {
		pr_debug("inlen is not supported\n");
		return ERR_PTR(-EOPNOTSUPP);
	}

	if (init_attr->qp_type != IB_QPT_RAW_PACKET) {
		pr_debug("RSS QP with unsupported QP type %d\n",
			 init_attr->qp_type);
		return ERR_PTR(-EOPNOTSUPP);
	}

	if (init_attr->create_flags) {
		pr_debug("RSS QP doesn't support create flags\n");
		return ERR_PTR(-EOPNOTSUPP);
	}

	if (init_attr->send_cq || init_attr->cap.max_send_wr) {
		pr_debug("RSS QP with unsupported send attributes\n");
		return ERR_PTR(-EOPNOTSUPP);
	}

	qp = kzalloc(sizeof(*qp), GFP_KERNEL);
	if (!qp)
		return ERR_PTR(-ENOMEM);

	qp->pri.vid = 0xFFFF;
	qp->alt.vid = 0xFFFF;

	err = create_qp_rss(to_mdev(pd->device), init_attr, &ucmd, qp);
	if (err) {
		kfree(qp);
		return ERR_PTR(err);
	}

	qp->ibqp.qp_num = qp->mqp.qpn;

	return &qp->ibqp;
}

/*
 * This function allocates a WQN from a range which is consecutive and aligned
 * to its size. In case the range is full, then it creates a new range and
 * allocates WQN from it. The new range will be used for following allocations.
 */
static int mlx4_ib_alloc_wqn(struct mlx4_ib_ucontext *context,
			     struct mlx4_ib_qp *qp, int range_size, int *wqn)
{
	struct mlx4_ib_dev *dev = to_mdev(context->ibucontext.device);
	struct mlx4_wqn_range *range;
	int err = 0;

	mutex_lock(&context->wqn_ranges_mutex);

	range = list_first_entry_or_null(&context->wqn_ranges_list,
					 struct mlx4_wqn_range, list);

	if (!range || (range->refcount == range->size) || range->dirty) {
		range = kzalloc(sizeof(*range), GFP_KERNEL);
		if (!range) {
			err = -ENOMEM;
			goto out;
		}

		err = mlx4_qp_reserve_range(dev->dev, range_size,
					    range_size, &range->base_wqn, 0,
					    qp->mqp.usage);
		if (err) {
			kfree(range);
			goto out;
		}

		range->size = range_size;
		list_add(&range->list, &context->wqn_ranges_list);
	} else if (range_size != 1) {
		/*
		 * Requesting a new range (>1) when last range is still open, is
		 * not valid.
		 */
		err = -EINVAL;
		goto out;
	}

	qp->wqn_range = range;

	*wqn = range->base_wqn + range->refcount;

	range->refcount++;

out:
	mutex_unlock(&context->wqn_ranges_mutex);

	return err;
}

static void mlx4_ib_release_wqn(struct mlx4_ib_ucontext *context,
				struct mlx4_ib_qp *qp, bool dirty_release)
{
	struct mlx4_ib_dev *dev = to_mdev(context->ibucontext.device);
	struct mlx4_wqn_range *range;

	mutex_lock(&context->wqn_ranges_mutex);

	range = qp->wqn_range;

	range->refcount--;
	if (!range->refcount) {
		mlx4_qp_release_range(dev->dev, range->base_wqn,
				      range->size);
		list_del(&range->list);
		kfree(range);
	} else if (dirty_release) {
	/*
	 * A range which one of its WQNs is destroyed, won't be able to be
	 * reused for further WQN allocations.
	 * The next created WQ will allocate a new range.
	 */
		range->dirty = 1;
	}

	mutex_unlock(&context->wqn_ranges_mutex);
}

static int create_qp_common(struct mlx4_ib_dev *dev, struct ib_pd *pd,
			    enum mlx4_ib_source_type src,
			    struct ib_qp_init_attr *init_attr,
			    struct ib_udata *udata, int sqpn,
			    struct mlx4_ib_qp **caller_qp)
{
	int qpn;
	int err;
	struct ib_qp_cap backup_cap;
	struct mlx4_ib_sqp *sqp = NULL;
	struct mlx4_ib_qp *qp;
	enum mlx4_ib_qp_type qp_type = (enum mlx4_ib_qp_type) init_attr->qp_type;
	struct mlx4_ib_cq *mcq;
	unsigned long flags;
	int range_size = 0;

	/* When tunneling special qps, we use a plain UD qp */
	if (sqpn) {
		if (mlx4_is_mfunc(dev->dev) &&
		    (!mlx4_is_master(dev->dev) ||
		     !(init_attr->create_flags & MLX4_IB_SRIOV_SQP))) {
			if (init_attr->qp_type == IB_QPT_GSI)
				qp_type = MLX4_IB_QPT_PROXY_GSI;
			else {
				if (mlx4_is_master(dev->dev) ||
				    qp0_enabled_vf(dev->dev, sqpn))
					qp_type = MLX4_IB_QPT_PROXY_SMI_OWNER;
				else
					qp_type = MLX4_IB_QPT_PROXY_SMI;
			}
		}
		qpn = sqpn;
		/* add extra sg entry for tunneling */
		init_attr->cap.max_recv_sge++;
	} else if (init_attr->create_flags & MLX4_IB_SRIOV_TUNNEL_QP) {
		struct mlx4_ib_qp_tunnel_init_attr *tnl_init =
			container_of(init_attr,
				     struct mlx4_ib_qp_tunnel_init_attr, init_attr);
		if ((tnl_init->proxy_qp_type != IB_QPT_SMI &&
		     tnl_init->proxy_qp_type != IB_QPT_GSI)   ||
		    !mlx4_is_master(dev->dev))
			return -EINVAL;
		if (tnl_init->proxy_qp_type == IB_QPT_GSI)
			qp_type = MLX4_IB_QPT_TUN_GSI;
		else if (tnl_init->slave == mlx4_master_func_num(dev->dev) ||
			 mlx4_vf_smi_enabled(dev->dev, tnl_init->slave,
					     tnl_init->port))
			qp_type = MLX4_IB_QPT_TUN_SMI_OWNER;
		else
			qp_type = MLX4_IB_QPT_TUN_SMI;
		/* we are definitely in the PPF here, since we are creating
		 * tunnel QPs. base_tunnel_sqpn is therefore valid. */
		qpn = dev->dev->phys_caps.base_tunnel_sqpn + 8 * tnl_init->slave
			+ tnl_init->proxy_qp_type * 2 + tnl_init->port - 1;
		sqpn = qpn;
	}

	if (!*caller_qp) {
		if (qp_type == MLX4_IB_QPT_SMI || qp_type == MLX4_IB_QPT_GSI ||
		    (qp_type & (MLX4_IB_QPT_PROXY_SMI | MLX4_IB_QPT_PROXY_SMI_OWNER |
				MLX4_IB_QPT_PROXY_GSI | MLX4_IB_QPT_TUN_SMI_OWNER))) {
			sqp = kzalloc(sizeof(struct mlx4_ib_sqp), GFP_KERNEL);
			if (!sqp)
				return -ENOMEM;
			qp = &sqp->qp;
			qp->pri.vid = 0xFFFF;
			qp->alt.vid = 0xFFFF;
		} else {
			qp = kzalloc(sizeof(struct mlx4_ib_qp), GFP_KERNEL);
			if (!qp)
				return -ENOMEM;
			qp->pri.vid = 0xFFFF;
			qp->alt.vid = 0xFFFF;
		}
	} else
		qp = *caller_qp;

	qp->mlx4_ib_qp_type = qp_type;

	mutex_init(&qp->mutex);
	spin_lock_init(&qp->sq.lock);
	spin_lock_init(&qp->rq.lock);
	INIT_LIST_HEAD(&qp->gid_list);
	INIT_LIST_HEAD(&qp->steering_rules);

	qp->state	 = IB_QPS_RESET;
	if (init_attr->sq_sig_type == IB_SIGNAL_ALL_WR)
		qp->sq_signal_bits = cpu_to_be32(MLX4_WQE_CTRL_CQ_UPDATE);


	if (pd->uobject) {
		union {
			struct mlx4_ib_create_qp qp;
			struct mlx4_ib_create_wq wq;
		} ucmd;
		size_t copy_len;
		int shift;
		int n;

		copy_len = (src == MLX4_IB_QP_SRC) ?
			   sizeof(struct mlx4_ib_create_qp) :
			   min(sizeof(struct mlx4_ib_create_wq), udata->inlen);

		if (ib_copy_from_udata(&ucmd, udata, copy_len)) {
			err = -EFAULT;
			goto err;
		}

		if (src == MLX4_IB_RWQ_SRC) {
			if (ucmd.wq.comp_mask || ucmd.wq.reserved[0] ||
			    ucmd.wq.reserved[1] || ucmd.wq.reserved[2]) {
				pr_debug("user command isn't supported\n");
				err = -EOPNOTSUPP;
				goto err;
			}

			if (ucmd.wq.log_range_size >
			    ilog2(dev->dev->caps.max_rss_tbl_sz)) {
				pr_debug("WQN range size must be equal or smaller than %d\n",
					 dev->dev->caps.max_rss_tbl_sz);
				err = -EOPNOTSUPP;
				goto err;
			}
			range_size = 1 << ucmd.wq.log_range_size;
		} else {
			qp->inl_recv_sz = ucmd.qp.inl_recv_sz;
		}

		if (init_attr->create_flags & IB_QP_CREATE_SCATTER_FCS) {
			if (!(dev->dev->caps.flags &
			      MLX4_DEV_CAP_FLAG_FCS_KEEP)) {
				pr_debug("scatter FCS is unsupported\n");
				err = -EOPNOTSUPP;
				goto err;
			}

			qp->flags |= MLX4_IB_QP_SCATTER_FCS;
		}

		err = set_rq_size(dev, &init_attr->cap, !!pd->uobject,
				  qp_has_rq(init_attr), qp, qp->inl_recv_sz);
		if (err)
			goto err;

		if (src == MLX4_IB_QP_SRC) {
			qp->sq_no_prefetch = ucmd.qp.sq_no_prefetch;

			err = set_user_sq_size(dev, qp,
					       (struct mlx4_ib_create_qp *)
					       &ucmd);
			if (err)
				goto err;
		} else {
			qp->sq_no_prefetch = 1;
			qp->sq.wqe_cnt = 1;
			qp->sq.wqe_shift = MLX4_IB_MIN_SQ_STRIDE;
			/* Allocated buffer expects to have at least that SQ
			 * size.
			 */
			qp->buf_size = (qp->rq.wqe_cnt << qp->rq.wqe_shift) +
				(qp->sq.wqe_cnt << qp->sq.wqe_shift);
		}

		qp->umem = ib_umem_get(pd->uobject->context,
				(src == MLX4_IB_QP_SRC) ? ucmd.qp.buf_addr :
				ucmd.wq.buf_addr, qp->buf_size, 0, 0);
		if (IS_ERR(qp->umem)) {
			err = PTR_ERR(qp->umem);
			goto err;
		}

		n = ib_umem_page_count(qp->umem);
		shift = mlx4_ib_umem_calc_optimal_mtt_size(qp->umem, 0, &n);
		err = mlx4_mtt_init(dev->dev, n, shift, &qp->mtt);

		if (err)
			goto err_buf;

		err = mlx4_ib_umem_write_mtt(dev, &qp->mtt, qp->umem);
		if (err)
			goto err_mtt;

		if (qp_has_rq(init_attr)) {
			err = mlx4_ib_db_map_user(to_mucontext(pd->uobject->context),
				(src == MLX4_IB_QP_SRC) ? ucmd.qp.db_addr :
				ucmd.wq.db_addr, &qp->db);
			if (err)
				goto err_mtt;
		}
		qp->mqp.usage = MLX4_RES_USAGE_USER_VERBS;
	} else {
		err = set_rq_size(dev, &init_attr->cap, !!pd->uobject,
				  qp_has_rq(init_attr), qp, 0);
		if (err)
			goto err;

		qp->sq_no_prefetch = 0;

		if (init_attr->create_flags & IB_QP_CREATE_IPOIB_UD_LSO)
			qp->flags |= MLX4_IB_QP_LSO;

		if (init_attr->create_flags & IB_QP_CREATE_NETIF_QP) {
			if (dev->steering_support ==
			    MLX4_STEERING_MODE_DEVICE_MANAGED)
				qp->flags |= MLX4_IB_QP_NETIF;
			else
				goto err;
		}

		memcpy(&backup_cap, &init_attr->cap, sizeof(backup_cap));
		err = set_kernel_sq_size(dev, &init_attr->cap,
					 qp_type, qp, true);
		if (err)
			goto err;

		if (qp_has_rq(init_attr)) {
			err = mlx4_db_alloc(dev->dev, &qp->db, 0);
			if (err)
				goto err;

			*qp->db.db = 0;
		}

		if (mlx4_buf_alloc(dev->dev, qp->buf_size, qp->buf_size,
				   &qp->buf)) {
			memcpy(&init_attr->cap, &backup_cap,
			       sizeof(backup_cap));
			err = set_kernel_sq_size(dev, &init_attr->cap, qp_type,
						 qp, false);
			if (err)
				goto err_db;

			if (mlx4_buf_alloc(dev->dev, qp->buf_size,
					   PAGE_SIZE * 2, &qp->buf)) {
				err = -ENOMEM;
				goto err_db;
			}
		}

		err = mlx4_mtt_init(dev->dev, qp->buf.npages, qp->buf.page_shift,
				    &qp->mtt);
		if (err)
			goto err_buf;

		err = mlx4_buf_write_mtt(dev->dev, &qp->mtt, &qp->buf);
		if (err)
			goto err_mtt;

		qp->sq.wrid = kvmalloc_array(qp->sq.wqe_cnt,
					     sizeof(u64), GFP_KERNEL);
		qp->rq.wrid = kvmalloc_array(qp->rq.wqe_cnt,
					     sizeof(u64), GFP_KERNEL);
		if (!qp->sq.wrid || !qp->rq.wrid) {
			err = -ENOMEM;
			goto err_wrid;
		}
		qp->mqp.usage = MLX4_RES_USAGE_DRIVER;
	}

	if (sqpn) {
		if (qp->mlx4_ib_qp_type & (MLX4_IB_QPT_PROXY_SMI_OWNER |
		    MLX4_IB_QPT_PROXY_SMI | MLX4_IB_QPT_PROXY_GSI)) {
			if (alloc_proxy_bufs(pd->device, qp)) {
				err = -ENOMEM;
				goto err_wrid;
			}
		}
	} else if (src == MLX4_IB_RWQ_SRC) {
		err = mlx4_ib_alloc_wqn(to_mucontext(pd->uobject->context), qp,
					range_size, &qpn);
		if (err)
			goto err_wrid;
	} else {
		/* Raw packet QPNs may not have bits 6,7 set in their qp_num;
		 * otherwise, the WQE BlueFlame setup flow wrongly causes
		 * VLAN insertion. */
		if (init_attr->qp_type == IB_QPT_RAW_PACKET)
			err = mlx4_qp_reserve_range(dev->dev, 1, 1, &qpn,
						    (init_attr->cap.max_send_wr ?
						     MLX4_RESERVE_ETH_BF_QP : 0) |
						    (init_attr->cap.max_recv_wr ?
						     MLX4_RESERVE_A0_QP : 0),
						    qp->mqp.usage);
		else
			if (qp->flags & MLX4_IB_QP_NETIF)
				err = mlx4_ib_steer_qp_alloc(dev, 1, &qpn);
			else
				err = mlx4_qp_reserve_range(dev->dev, 1, 1,
							    &qpn, 0, qp->mqp.usage);
		if (err)
			goto err_proxy;
	}

	if (init_attr->create_flags & IB_QP_CREATE_BLOCK_MULTICAST_LOOPBACK)
		qp->flags |= MLX4_IB_QP_BLOCK_MULTICAST_LOOPBACK;

	err = mlx4_qp_alloc(dev->dev, qpn, &qp->mqp);
	if (err)
		goto err_qpn;

	if (init_attr->qp_type == IB_QPT_XRC_TGT)
		qp->mqp.qpn |= (1 << 23);

	/*
	 * Hardware wants QPN written in big-endian order (after
	 * shifting) for send doorbell.  Precompute this value to save
	 * a little bit when posting sends.
	 */
	qp->doorbell_qpn = swab32(qp->mqp.qpn << 8);

	qp->mqp.event = (src == MLX4_IB_QP_SRC) ? mlx4_ib_qp_event :
						  mlx4_ib_wq_event;

	if (!*caller_qp)
		*caller_qp = qp;

	spin_lock_irqsave(&dev->reset_flow_resource_lock, flags);
	mlx4_ib_lock_cqs(to_mcq(init_attr->send_cq),
			 to_mcq(init_attr->recv_cq));
	/* Maintain device to QPs access, needed for further handling
	 * via reset flow
	 */
	list_add_tail(&qp->qps_list, &dev->qp_list);
	/* Maintain CQ to QPs access, needed for further handling
	 * via reset flow
	 */
	mcq = to_mcq(init_attr->send_cq);
	list_add_tail(&qp->cq_send_list, &mcq->send_qp_list);
	mcq = to_mcq(init_attr->recv_cq);
	list_add_tail(&qp->cq_recv_list, &mcq->recv_qp_list);
	mlx4_ib_unlock_cqs(to_mcq(init_attr->send_cq),
			   to_mcq(init_attr->recv_cq));
	spin_unlock_irqrestore(&dev->reset_flow_resource_lock, flags);
	return 0;

err_qpn:
	if (!sqpn) {
		if (qp->flags & MLX4_IB_QP_NETIF)
			mlx4_ib_steer_qp_free(dev, qpn, 1);
		else if (src == MLX4_IB_RWQ_SRC)
			mlx4_ib_release_wqn(to_mucontext(pd->uobject->context),
					    qp, 0);
		else
			mlx4_qp_release_range(dev->dev, qpn, 1);
	}
err_proxy:
	if (qp->mlx4_ib_qp_type == MLX4_IB_QPT_PROXY_GSI)
		free_proxy_bufs(pd->device, qp);
err_wrid:
	if (pd->uobject) {
		if (qp_has_rq(init_attr))
			mlx4_ib_db_unmap_user(to_mucontext(pd->uobject->context), &qp->db);
	} else {
		kvfree(qp->sq.wrid);
		kvfree(qp->rq.wrid);
	}

err_mtt:
	mlx4_mtt_cleanup(dev->dev, &qp->mtt);

err_buf:
	if (pd->uobject)
		ib_umem_release(qp->umem);
	else
		mlx4_buf_free(dev->dev, qp->buf_size, &qp->buf);

err_db:
	if (!pd->uobject && qp_has_rq(init_attr))
		mlx4_db_free(dev->dev, &qp->db);

err:
	if (sqp)
		kfree(sqp);
	else if (!*caller_qp)
		kfree(qp);
	return err;
}

static enum mlx4_qp_state to_mlx4_state(enum ib_qp_state state)
{
	switch (state) {
	case IB_QPS_RESET:	return MLX4_QP_STATE_RST;
	case IB_QPS_INIT:	return MLX4_QP_STATE_INIT;
	case IB_QPS_RTR:	return MLX4_QP_STATE_RTR;
	case IB_QPS_RTS:	return MLX4_QP_STATE_RTS;
	case IB_QPS_SQD:	return MLX4_QP_STATE_SQD;
	case IB_QPS_SQE:	return MLX4_QP_STATE_SQER;
	case IB_QPS_ERR:	return MLX4_QP_STATE_ERR;
	default:		return -1;
	}
}

static void mlx4_ib_lock_cqs(struct mlx4_ib_cq *send_cq, struct mlx4_ib_cq *recv_cq)
	__acquires(&send_cq->lock) __acquires(&recv_cq->lock)
{
	if (send_cq == recv_cq) {
		spin_lock(&send_cq->lock);
		__acquire(&recv_cq->lock);
	} else if (send_cq->mcq.cqn < recv_cq->mcq.cqn) {
		spin_lock(&send_cq->lock);
		spin_lock_nested(&recv_cq->lock, SINGLE_DEPTH_NESTING);
	} else {
		spin_lock(&recv_cq->lock);
		spin_lock_nested(&send_cq->lock, SINGLE_DEPTH_NESTING);
	}
}

static void mlx4_ib_unlock_cqs(struct mlx4_ib_cq *send_cq, struct mlx4_ib_cq *recv_cq)
	__releases(&send_cq->lock) __releases(&recv_cq->lock)
{
	if (send_cq == recv_cq) {
		__release(&recv_cq->lock);
		spin_unlock(&send_cq->lock);
	} else if (send_cq->mcq.cqn < recv_cq->mcq.cqn) {
		spin_unlock(&recv_cq->lock);
		spin_unlock(&send_cq->lock);
	} else {
		spin_unlock(&send_cq->lock);
		spin_unlock(&recv_cq->lock);
	}
}

static void del_gid_entries(struct mlx4_ib_qp *qp)
{
	struct mlx4_ib_gid_entry *ge, *tmp;

	list_for_each_entry_safe(ge, tmp, &qp->gid_list, list) {
		list_del(&ge->list);
		kfree(ge);
	}
}

static struct mlx4_ib_pd *get_pd(struct mlx4_ib_qp *qp)
{
	if (qp->ibqp.qp_type == IB_QPT_XRC_TGT)
		return to_mpd(to_mxrcd(qp->ibqp.xrcd)->pd);
	else
		return to_mpd(qp->ibqp.pd);
}

static void get_cqs(struct mlx4_ib_qp *qp, enum mlx4_ib_source_type src,
		    struct mlx4_ib_cq **send_cq, struct mlx4_ib_cq **recv_cq)
{
	switch (qp->ibqp.qp_type) {
	case IB_QPT_XRC_TGT:
		*send_cq = to_mcq(to_mxrcd(qp->ibqp.xrcd)->cq);
		*recv_cq = *send_cq;
		break;
	case IB_QPT_XRC_INI:
		*send_cq = to_mcq(qp->ibqp.send_cq);
		*recv_cq = *send_cq;
		break;
	default:
		*recv_cq = (src == MLX4_IB_QP_SRC) ? to_mcq(qp->ibqp.recv_cq) :
						     to_mcq(qp->ibwq.cq);
		*send_cq = (src == MLX4_IB_QP_SRC) ? to_mcq(qp->ibqp.send_cq) :
						     *recv_cq;
		break;
	}
}

static void destroy_qp_rss(struct mlx4_ib_dev *dev, struct mlx4_ib_qp *qp)
{
	if (qp->state != IB_QPS_RESET) {
		int i;

		for (i = 0; i < (1 << qp->ibqp.rwq_ind_tbl->log_ind_tbl_size);
		     i++) {
			struct ib_wq *ibwq = qp->ibqp.rwq_ind_tbl->ind_tbl[i];
			struct mlx4_ib_qp *wq =	to_mqp((struct ib_qp *)ibwq);

			mutex_lock(&wq->mutex);

			wq->rss_usecnt--;

			mutex_unlock(&wq->mutex);
		}

		if (mlx4_qp_modify(dev->dev, NULL, to_mlx4_state(qp->state),
				   MLX4_QP_STATE_RST, NULL, 0, 0, &qp->mqp))
			pr_warn("modify QP %06x to RESET failed.\n",
				qp->mqp.qpn);
	}

	mlx4_qp_remove(dev->dev, &qp->mqp);
	mlx4_qp_free(dev->dev, &qp->mqp);
	mlx4_qp_release_range(dev->dev, qp->mqp.qpn, 1);
	del_gid_entries(qp);
	kfree(qp->rss_ctx);
}

static void destroy_qp_common(struct mlx4_ib_dev *dev, struct mlx4_ib_qp *qp,
			      enum mlx4_ib_source_type src, int is_user)
{
	struct mlx4_ib_cq *send_cq, *recv_cq;
	unsigned long flags;

	if (qp->state != IB_QPS_RESET) {
		if (mlx4_qp_modify(dev->dev, NULL, to_mlx4_state(qp->state),
				   MLX4_QP_STATE_RST, NULL, 0, 0, &qp->mqp))
			pr_warn("modify QP %06x to RESET failed.\n",
			       qp->mqp.qpn);
		if (qp->pri.smac || (!qp->pri.smac && qp->pri.smac_port)) {
			mlx4_unregister_mac(dev->dev, qp->pri.smac_port, qp->pri.smac);
			qp->pri.smac = 0;
			qp->pri.smac_port = 0;
		}
		if (qp->alt.smac) {
			mlx4_unregister_mac(dev->dev, qp->alt.smac_port, qp->alt.smac);
			qp->alt.smac = 0;
		}
		if (qp->pri.vid < 0x1000) {
			mlx4_unregister_vlan(dev->dev, qp->pri.vlan_port, qp->pri.vid);
			qp->pri.vid = 0xFFFF;
			qp->pri.candidate_vid = 0xFFFF;
			qp->pri.update_vid = 0;
		}
		if (qp->alt.vid < 0x1000) {
			mlx4_unregister_vlan(dev->dev, qp->alt.vlan_port, qp->alt.vid);
			qp->alt.vid = 0xFFFF;
			qp->alt.candidate_vid = 0xFFFF;
			qp->alt.update_vid = 0;
		}
	}

	get_cqs(qp, src, &send_cq, &recv_cq);

	spin_lock_irqsave(&dev->reset_flow_resource_lock, flags);
	mlx4_ib_lock_cqs(send_cq, recv_cq);

	/* del from lists under both locks above to protect reset flow paths */
	list_del(&qp->qps_list);
	list_del(&qp->cq_send_list);
	list_del(&qp->cq_recv_list);
	if (!is_user) {
		__mlx4_ib_cq_clean(recv_cq, qp->mqp.qpn,
				 qp->ibqp.srq ? to_msrq(qp->ibqp.srq): NULL);
		if (send_cq != recv_cq)
			__mlx4_ib_cq_clean(send_cq, qp->mqp.qpn, NULL);
	}

	mlx4_qp_remove(dev->dev, &qp->mqp);

	mlx4_ib_unlock_cqs(send_cq, recv_cq);
	spin_unlock_irqrestore(&dev->reset_flow_resource_lock, flags);

	mlx4_qp_free(dev->dev, &qp->mqp);

	if (!is_sqp(dev, qp) && !is_tunnel_qp(dev, qp)) {
		if (qp->flags & MLX4_IB_QP_NETIF)
			mlx4_ib_steer_qp_free(dev, qp->mqp.qpn, 1);
		else if (src == MLX4_IB_RWQ_SRC)
			mlx4_ib_release_wqn(to_mucontext(
					    qp->ibwq.uobject->context), qp, 1);
		else
			mlx4_qp_release_range(dev->dev, qp->mqp.qpn, 1);
	}

	mlx4_mtt_cleanup(dev->dev, &qp->mtt);

	if (is_user) {
		if (qp->rq.wqe_cnt) {
			struct mlx4_ib_ucontext *mcontext = !src ?
				to_mucontext(qp->ibqp.uobject->context) :
				to_mucontext(qp->ibwq.uobject->context);
			mlx4_ib_db_unmap_user(mcontext, &qp->db);
		}
		ib_umem_release(qp->umem);
	} else {
		kvfree(qp->sq.wrid);
		kvfree(qp->rq.wrid);
		if (qp->mlx4_ib_qp_type & (MLX4_IB_QPT_PROXY_SMI_OWNER |
		    MLX4_IB_QPT_PROXY_SMI | MLX4_IB_QPT_PROXY_GSI))
			free_proxy_bufs(&dev->ib_dev, qp);
		mlx4_buf_free(dev->dev, qp->buf_size, &qp->buf);
		if (qp->rq.wqe_cnt)
			mlx4_db_free(dev->dev, &qp->db);
	}

	del_gid_entries(qp);
}

static u32 get_sqp_num(struct mlx4_ib_dev *dev, struct ib_qp_init_attr *attr)
{
	/* Native or PPF */
	if (!mlx4_is_mfunc(dev->dev) ||
	    (mlx4_is_master(dev->dev) &&
	     attr->create_flags & MLX4_IB_SRIOV_SQP)) {
		return  dev->dev->phys_caps.base_sqpn +
			(attr->qp_type == IB_QPT_SMI ? 0 : 2) +
			attr->port_num - 1;
	}
	/* PF or VF -- creating proxies */
	if (attr->qp_type == IB_QPT_SMI)
		return dev->dev->caps.spec_qps[attr->port_num - 1].qp0_proxy;
	else
		return dev->dev->caps.spec_qps[attr->port_num - 1].qp1_proxy;
}

static struct ib_qp *_mlx4_ib_create_qp(struct ib_pd *pd,
					struct ib_qp_init_attr *init_attr,
					struct ib_udata *udata)
{
	struct mlx4_ib_qp *qp = NULL;
	int err;
	int sup_u_create_flags = MLX4_IB_QP_BLOCK_MULTICAST_LOOPBACK;
	u16 xrcdn = 0;

	if (init_attr->rwq_ind_tbl)
		return _mlx4_ib_create_qp_rss(pd, init_attr, udata);

	/*
	 * We only support LSO, vendor flag1, and multicast loopback blocking,
	 * and only for kernel UD QPs.
	 */
	if (init_attr->create_flags & ~(MLX4_IB_QP_LSO |
					MLX4_IB_QP_BLOCK_MULTICAST_LOOPBACK |
					MLX4_IB_SRIOV_TUNNEL_QP |
					MLX4_IB_SRIOV_SQP |
					MLX4_IB_QP_NETIF |
					MLX4_IB_QP_CREATE_ROCE_V2_GSI))
		return ERR_PTR(-EINVAL);

	if (init_attr->create_flags & IB_QP_CREATE_NETIF_QP) {
		if (init_attr->qp_type != IB_QPT_UD)
			return ERR_PTR(-EINVAL);
	}

	if (init_attr->create_flags) {
		if (udata && init_attr->create_flags & ~(sup_u_create_flags))
			return ERR_PTR(-EINVAL);

		if ((init_attr->create_flags & ~(MLX4_IB_SRIOV_SQP |
						 MLX4_IB_QP_CREATE_ROCE_V2_GSI  |
						 MLX4_IB_QP_BLOCK_MULTICAST_LOOPBACK) &&
		     init_attr->qp_type != IB_QPT_UD) ||
		    (init_attr->create_flags & MLX4_IB_SRIOV_SQP &&
		     init_attr->qp_type > IB_QPT_GSI) ||
		    (init_attr->create_flags & MLX4_IB_QP_CREATE_ROCE_V2_GSI &&
		     init_attr->qp_type != IB_QPT_GSI))
			return ERR_PTR(-EINVAL);
	}

	switch (init_attr->qp_type) {
	case IB_QPT_XRC_TGT:
		pd = to_mxrcd(init_attr->xrcd)->pd;
		xrcdn = to_mxrcd(init_attr->xrcd)->xrcdn;
		init_attr->send_cq = to_mxrcd(init_attr->xrcd)->cq;
		/* fall through */
	case IB_QPT_XRC_INI:
		if (!(to_mdev(pd->device)->dev->caps.flags & MLX4_DEV_CAP_FLAG_XRC))
			return ERR_PTR(-ENOSYS);
		init_attr->recv_cq = init_attr->send_cq;
		/* fall through */
	case IB_QPT_RC:
	case IB_QPT_UC:
	case IB_QPT_RAW_PACKET:
		qp = kzalloc(sizeof(*qp), GFP_KERNEL);
		if (!qp)
			return ERR_PTR(-ENOMEM);
		qp->pri.vid = 0xFFFF;
		qp->alt.vid = 0xFFFF;
		/* fall through */
	case IB_QPT_UD:
	{
		err = create_qp_common(to_mdev(pd->device), pd,	MLX4_IB_QP_SRC,
				       init_attr, udata, 0, &qp);
		if (err) {
			kfree(qp);
			return ERR_PTR(err);
		}

		qp->ibqp.qp_num = qp->mqp.qpn;
		qp->xrcdn = xrcdn;

		break;
	}
	case IB_QPT_SMI:
	case IB_QPT_GSI:
	{
		int sqpn;

		/* Userspace is not allowed to create special QPs: */
		if (udata)
			return ERR_PTR(-EINVAL);
		if (init_attr->create_flags & MLX4_IB_QP_CREATE_ROCE_V2_GSI) {
			int res = mlx4_qp_reserve_range(to_mdev(pd->device)->dev,
							1, 1, &sqpn, 0,
							MLX4_RES_USAGE_DRIVER);

			if (res)
				return ERR_PTR(res);
		} else {
			sqpn = get_sqp_num(to_mdev(pd->device), init_attr);
		}

		err = create_qp_common(to_mdev(pd->device), pd, MLX4_IB_QP_SRC,
				       init_attr, udata, sqpn, &qp);
		if (err)
			return ERR_PTR(err);

		qp->port	= init_attr->port_num;
		qp->ibqp.qp_num = init_attr->qp_type == IB_QPT_SMI ? 0 :
			init_attr->create_flags & MLX4_IB_QP_CREATE_ROCE_V2_GSI ? sqpn : 1;
		break;
	}
	default:
		/* Don't support raw QPs */
		return ERR_PTR(-EINVAL);
	}

	return &qp->ibqp;
}

struct ib_qp *mlx4_ib_create_qp(struct ib_pd *pd,
				struct ib_qp_init_attr *init_attr,
				struct ib_udata *udata) {
	struct ib_device *device = pd ? pd->device : init_attr->xrcd->device;
	struct ib_qp *ibqp;
	struct mlx4_ib_dev *dev = to_mdev(device);

	ibqp = _mlx4_ib_create_qp(pd, init_attr, udata);

	if (!IS_ERR(ibqp) &&
	    (init_attr->qp_type == IB_QPT_GSI) &&
	    !(init_attr->create_flags & MLX4_IB_QP_CREATE_ROCE_V2_GSI)) {
		struct mlx4_ib_sqp *sqp = to_msqp((to_mqp(ibqp)));
		int is_eth = rdma_cap_eth_ah(&dev->ib_dev, init_attr->port_num);

		if (is_eth &&
		    dev->dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_ROCE_V1_V2) {
			init_attr->create_flags |= MLX4_IB_QP_CREATE_ROCE_V2_GSI;
			sqp->roce_v2_gsi = ib_create_qp(pd, init_attr);

			if (IS_ERR(sqp->roce_v2_gsi)) {
				pr_err("Failed to create GSI QP for RoCEv2 (%ld)\n", PTR_ERR(sqp->roce_v2_gsi));
				sqp->roce_v2_gsi = NULL;
			} else {
				sqp = to_msqp(to_mqp(sqp->roce_v2_gsi));
				sqp->qp.flags |= MLX4_IB_ROCE_V2_GSI_QP;
			}

			init_attr->create_flags &= ~MLX4_IB_QP_CREATE_ROCE_V2_GSI;
		}
	}
	return ibqp;
}

static int _mlx4_ib_destroy_qp(struct ib_qp *qp)
{
	struct mlx4_ib_dev *dev = to_mdev(qp->device);
	struct mlx4_ib_qp *mqp = to_mqp(qp);

	if (is_qp0(dev, mqp))
		mlx4_CLOSE_PORT(dev->dev, mqp->port);

	if (mqp->mlx4_ib_qp_type == MLX4_IB_QPT_PROXY_GSI &&
	    dev->qp1_proxy[mqp->port - 1] == mqp) {
		mutex_lock(&dev->qp1_proxy_lock[mqp->port - 1]);
		dev->qp1_proxy[mqp->port - 1] = NULL;
		mutex_unlock(&dev->qp1_proxy_lock[mqp->port - 1]);
	}

	if (mqp->counter_index)
		mlx4_ib_free_qp_counter(dev, mqp);

	if (qp->rwq_ind_tbl) {
		destroy_qp_rss(dev, mqp);
	} else {
		struct mlx4_ib_pd *pd;

		pd = get_pd(mqp);
		destroy_qp_common(dev, mqp, MLX4_IB_QP_SRC, !!pd->ibpd.uobject);
	}

	if (is_sqp(dev, mqp))
		kfree(to_msqp(mqp));
	else
		kfree(mqp);

	return 0;
}

int mlx4_ib_destroy_qp(struct ib_qp *qp)
{
	struct mlx4_ib_qp *mqp = to_mqp(qp);

	if (mqp->mlx4_ib_qp_type == MLX4_IB_QPT_GSI) {
		struct mlx4_ib_sqp *sqp = to_msqp(mqp);

		if (sqp->roce_v2_gsi)
			ib_destroy_qp(sqp->roce_v2_gsi);
	}

	return _mlx4_ib_destroy_qp(qp);
}

static int to_mlx4_st(struct mlx4_ib_dev *dev, enum mlx4_ib_qp_type type)
{
	switch (type) {
	case MLX4_IB_QPT_RC:		return MLX4_QP_ST_RC;
	case MLX4_IB_QPT_UC:		return MLX4_QP_ST_UC;
	case MLX4_IB_QPT_UD:		return MLX4_QP_ST_UD;
	case MLX4_IB_QPT_XRC_INI:
	case MLX4_IB_QPT_XRC_TGT:	return MLX4_QP_ST_XRC;
	case MLX4_IB_QPT_SMI:
	case MLX4_IB_QPT_GSI:
	case MLX4_IB_QPT_RAW_PACKET:	return MLX4_QP_ST_MLX;

	case MLX4_IB_QPT_PROXY_SMI_OWNER:
	case MLX4_IB_QPT_TUN_SMI_OWNER:	return (mlx4_is_mfunc(dev->dev) ?
						MLX4_QP_ST_MLX : -1);
	case MLX4_IB_QPT_PROXY_SMI:
	case MLX4_IB_QPT_TUN_SMI:
	case MLX4_IB_QPT_PROXY_GSI:
	case MLX4_IB_QPT_TUN_GSI:	return (mlx4_is_mfunc(dev->dev) ?
						MLX4_QP_ST_UD : -1);
	default:			return -1;
	}
}

static __be32 to_mlx4_access_flags(struct mlx4_ib_qp *qp, const struct ib_qp_attr *attr,
				   int attr_mask)
{
	u8 dest_rd_atomic;
	u32 access_flags;
	u32 hw_access_flags = 0;

	if (attr_mask & IB_QP_MAX_DEST_RD_ATOMIC)
		dest_rd_atomic = attr->max_dest_rd_atomic;
	else
		dest_rd_atomic = qp->resp_depth;

	if (attr_mask & IB_QP_ACCESS_FLAGS)
		access_flags = attr->qp_access_flags;
	else
		access_flags = qp->atomic_rd_en;

	if (!dest_rd_atomic)
		access_flags &= IB_ACCESS_REMOTE_WRITE;

	if (access_flags & IB_ACCESS_REMOTE_READ)
		hw_access_flags |= MLX4_QP_BIT_RRE;
	if (access_flags & IB_ACCESS_REMOTE_ATOMIC)
		hw_access_flags |= MLX4_QP_BIT_RAE;
	if (access_flags & IB_ACCESS_REMOTE_WRITE)
		hw_access_flags |= MLX4_QP_BIT_RWE;

	return cpu_to_be32(hw_access_flags);
}

static void store_sqp_attrs(struct mlx4_ib_sqp *sqp, const struct ib_qp_attr *attr,
			    int attr_mask)
{
	if (attr_mask & IB_QP_PKEY_INDEX)
		sqp->pkey_index = attr->pkey_index;
	if (attr_mask & IB_QP_QKEY)
		sqp->qkey = attr->qkey;
	if (attr_mask & IB_QP_SQ_PSN)
		sqp->send_psn = attr->sq_psn;
}

static void mlx4_set_sched(struct mlx4_qp_path *path, u8 port)
{
	path->sched_queue = (path->sched_queue & 0xbf) | ((port - 1) << 6);
}

static int _mlx4_set_path(struct mlx4_ib_dev *dev,
			  const struct rdma_ah_attr *ah,
			  u64 smac, u16 vlan_tag, struct mlx4_qp_path *path,
			  struct mlx4_roce_smac_vlan_info *smac_info, u8 port)
{
	int vidx;
	int smac_index;
	int err;

	path->grh_mylmc = rdma_ah_get_path_bits(ah) & 0x7f;
	path->rlid = cpu_to_be16(rdma_ah_get_dlid(ah));
	if (rdma_ah_get_static_rate(ah)) {
		path->static_rate = rdma_ah_get_static_rate(ah) +
				    MLX4_STAT_RATE_OFFSET;
		while (path->static_rate > IB_RATE_2_5_GBPS + MLX4_STAT_RATE_OFFSET &&
		       !(1 << path->static_rate & dev->dev->caps.stat_rate_support))
			--path->static_rate;
	} else
		path->static_rate = 0;

	if (rdma_ah_get_ah_flags(ah) & IB_AH_GRH) {
		const struct ib_global_route *grh = rdma_ah_read_grh(ah);
		int real_sgid_index =
			mlx4_ib_gid_index_to_real_index(dev, port,
							grh->sgid_index);

		if (real_sgid_index < 0)
			return real_sgid_index;
		if (real_sgid_index >= dev->dev->caps.gid_table_len[port]) {
			pr_err("sgid_index (%u) too large. max is %d\n",
			       real_sgid_index, dev->dev->caps.gid_table_len[port] - 1);
			return -1;
		}

		path->grh_mylmc |= 1 << 7;
		path->mgid_index = real_sgid_index;
		path->hop_limit  = grh->hop_limit;
		path->tclass_flowlabel =
			cpu_to_be32((grh->traffic_class << 20) |
				    (grh->flow_label));
		memcpy(path->rgid, grh->dgid.raw, 16);
	}

	if (ah->type == RDMA_AH_ATTR_TYPE_ROCE) {
		if (!(rdma_ah_get_ah_flags(ah) & IB_AH_GRH))
			return -1;

		path->sched_queue = MLX4_IB_DEFAULT_SCHED_QUEUE |
			((port - 1) << 6) | ((rdma_ah_get_sl(ah) & 7) << 3);

		path->feup |= MLX4_FEUP_FORCE_ETH_UP;
		if (vlan_tag < 0x1000) {
			if (smac_info->vid < 0x1000) {
				/* both valid vlan ids */
				if (smac_info->vid != vlan_tag) {
					/* different VIDs.  unreg old and reg new */
					err = mlx4_register_vlan(dev->dev, port, vlan_tag, &vidx);
					if (err)
						return err;
					smac_info->candidate_vid = vlan_tag;
					smac_info->candidate_vlan_index = vidx;
					smac_info->candidate_vlan_port = port;
					smac_info->update_vid = 1;
					path->vlan_index = vidx;
				} else {
					path->vlan_index = smac_info->vlan_index;
				}
			} else {
				/* no current vlan tag in qp */
				err = mlx4_register_vlan(dev->dev, port, vlan_tag, &vidx);
				if (err)
					return err;
				smac_info->candidate_vid = vlan_tag;
				smac_info->candidate_vlan_index = vidx;
				smac_info->candidate_vlan_port = port;
				smac_info->update_vid = 1;
				path->vlan_index = vidx;
			}
			path->feup |= MLX4_FVL_FORCE_ETH_VLAN;
			path->fl = 1 << 6;
		} else {
			/* have current vlan tag. unregister it at modify-qp success */
			if (smac_info->vid < 0x1000) {
				smac_info->candidate_vid = 0xFFFF;
				smac_info->update_vid = 1;
			}
		}

		/* get smac_index for RoCE use.
		 * If no smac was yet assigned, register one.
		 * If one was already assigned, but the new mac differs,
		 * unregister the old one and register the new one.
		*/
		if ((!smac_info->smac && !smac_info->smac_port) ||
		    smac_info->smac != smac) {
			/* register candidate now, unreg if needed, after success */
			smac_index = mlx4_register_mac(dev->dev, port, smac);
			if (smac_index >= 0) {
				smac_info->candidate_smac_index = smac_index;
				smac_info->candidate_smac = smac;
				smac_info->candidate_smac_port = port;
			} else {
				return -EINVAL;
			}
		} else {
			smac_index = smac_info->smac_index;
		}
		memcpy(path->dmac, ah->roce.dmac, 6);
		path->ackto = MLX4_IB_LINK_TYPE_ETH;
		/* put MAC table smac index for IBoE */
		path->grh_mylmc = (u8) (smac_index) | 0x80;
	} else {
		path->sched_queue = MLX4_IB_DEFAULT_SCHED_QUEUE |
			((port - 1) << 6) | ((rdma_ah_get_sl(ah) & 0xf) << 2);
	}

	return 0;
}

static int mlx4_set_path(struct mlx4_ib_dev *dev, const struct ib_qp_attr *qp,
			 enum ib_qp_attr_mask qp_attr_mask,
			 struct mlx4_ib_qp *mqp,
			 struct mlx4_qp_path *path, u8 port,
			 u16 vlan_id, u8 *smac)
{
	return _mlx4_set_path(dev, &qp->ah_attr,
			      mlx4_mac_to_u64(smac),
			      vlan_id,
			      path, &mqp->pri, port);
}

static int mlx4_set_alt_path(struct mlx4_ib_dev *dev,
			     const struct ib_qp_attr *qp,
			     enum ib_qp_attr_mask qp_attr_mask,
			     struct mlx4_ib_qp *mqp,
			     struct mlx4_qp_path *path, u8 port)
{
	return _mlx4_set_path(dev, &qp->alt_ah_attr,
			      0,
			      0xffff,
			      path, &mqp->alt, port);
}

static void update_mcg_macs(struct mlx4_ib_dev *dev, struct mlx4_ib_qp *qp)
{
	struct mlx4_ib_gid_entry *ge, *tmp;

	list_for_each_entry_safe(ge, tmp, &qp->gid_list, list) {
		if (!ge->added && mlx4_ib_add_mc(dev, qp, &ge->gid)) {
			ge->added = 1;
			ge->port = qp->port;
		}
	}
}

static int handle_eth_ud_smac_index(struct mlx4_ib_dev *dev,
				    struct mlx4_ib_qp *qp,
				    struct mlx4_qp_context *context)
{
	u64 u64_mac;
	int smac_index;

	u64_mac = atomic64_read(&dev->iboe.mac[qp->port - 1]);

	context->pri_path.sched_queue = MLX4_IB_DEFAULT_SCHED_QUEUE | ((qp->port - 1) << 6);
	if (!qp->pri.smac && !qp->pri.smac_port) {
		smac_index = mlx4_register_mac(dev->dev, qp->port, u64_mac);
		if (smac_index >= 0) {
			qp->pri.candidate_smac_index = smac_index;
			qp->pri.candidate_smac = u64_mac;
			qp->pri.candidate_smac_port = qp->port;
			context->pri_path.grh_mylmc = 0x80 | (u8) smac_index;
		} else {
			return -ENOENT;
		}
	}
	return 0;
}

static int create_qp_lb_counter(struct mlx4_ib_dev *dev, struct mlx4_ib_qp *qp)
{
	struct counter_index *new_counter_index;
	int err;
	u32 tmp_idx;

	if (rdma_port_get_link_layer(&dev->ib_dev, qp->port) !=
	    IB_LINK_LAYER_ETHERNET ||
	    !(qp->flags & MLX4_IB_QP_BLOCK_MULTICAST_LOOPBACK) ||
	    !(dev->dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_LB_SRC_CHK))
		return 0;

	err = mlx4_counter_alloc(dev->dev, &tmp_idx, MLX4_RES_USAGE_DRIVER);
	if (err)
		return err;

	new_counter_index = kmalloc(sizeof(*new_counter_index), GFP_KERNEL);
	if (!new_counter_index) {
		mlx4_counter_free(dev->dev, tmp_idx);
		return -ENOMEM;
	}

	new_counter_index->index = tmp_idx;
	new_counter_index->allocated = 1;
	qp->counter_index = new_counter_index;

	mutex_lock(&dev->counters_table[qp->port - 1].mutex);
	list_add_tail(&new_counter_index->list,
		      &dev->counters_table[qp->port - 1].counters_list);
	mutex_unlock(&dev->counters_table[qp->port - 1].mutex);

	return 0;
}

enum {
	MLX4_QPC_ROCE_MODE_1 = 0,
	MLX4_QPC_ROCE_MODE_2 = 2,
	MLX4_QPC_ROCE_MODE_UNDEFINED = 0xff
};

static u8 gid_type_to_qpc(enum ib_gid_type gid_type)
{
	switch (gid_type) {
	case IB_GID_TYPE_ROCE:
		return MLX4_QPC_ROCE_MODE_1;
	case IB_GID_TYPE_ROCE_UDP_ENCAP:
		return MLX4_QPC_ROCE_MODE_2;
	default:
		return MLX4_QPC_ROCE_MODE_UNDEFINED;
	}
}

/*
 * Go over all RSS QP's childes (WQs) and apply their HW state according to
 * their logic state if the RSS QP is the first RSS QP associated for the WQ.
 */
static int bringup_rss_rwqs(struct ib_rwq_ind_table *ind_tbl, u8 port_num)
{
	int err = 0;
	int i;

	for (i = 0; i < (1 << ind_tbl->log_ind_tbl_size); i++) {
		struct ib_wq *ibwq = ind_tbl->ind_tbl[i];
		struct mlx4_ib_qp *wq = to_mqp((struct ib_qp *)ibwq);

		mutex_lock(&wq->mutex);

		/* Mlx4_ib restrictions:
		 * WQ's is associated to a port according to the RSS QP it is
		 * associates to.
		 * In case the WQ is associated to a different port by another
		 * RSS QP, return a failure.
		 */
		if ((wq->rss_usecnt > 0) && (wq->port != port_num)) {
			err = -EINVAL;
			mutex_unlock(&wq->mutex);
			break;
		}
		wq->port = port_num;
		if ((wq->rss_usecnt == 0) && (ibwq->state == IB_WQS_RDY)) {
			err = _mlx4_ib_modify_wq(ibwq, IB_WQS_RDY);
			if (err) {
				mutex_unlock(&wq->mutex);
				break;
			}
		}
		wq->rss_usecnt++;

		mutex_unlock(&wq->mutex);
	}

	if (i && err) {
		int j;

		for (j = (i - 1); j >= 0; j--) {
			struct ib_wq *ibwq = ind_tbl->ind_tbl[j];
			struct mlx4_ib_qp *wq = to_mqp((struct ib_qp *)ibwq);

			mutex_lock(&wq->mutex);

			if ((wq->rss_usecnt == 1) &&
			    (ibwq->state == IB_WQS_RDY))
				if (_mlx4_ib_modify_wq(ibwq, IB_WQS_RESET))
					pr_warn("failed to reverse WQN=0x%06x\n",
						ibwq->wq_num);
			wq->rss_usecnt--;

			mutex_unlock(&wq->mutex);
		}
	}

	return err;
}

static void bring_down_rss_rwqs(struct ib_rwq_ind_table *ind_tbl)
{
	int i;

	for (i = 0; i < (1 << ind_tbl->log_ind_tbl_size); i++) {
		struct ib_wq *ibwq = ind_tbl->ind_tbl[i];
		struct mlx4_ib_qp *wq = to_mqp((struct ib_qp *)ibwq);

		mutex_lock(&wq->mutex);

		if ((wq->rss_usecnt == 1) && (ibwq->state == IB_WQS_RDY))
			if (_mlx4_ib_modify_wq(ibwq, IB_WQS_RESET))
				pr_warn("failed to reverse WQN=%x\n",
					ibwq->wq_num);
		wq->rss_usecnt--;

		mutex_unlock(&wq->mutex);
	}
}

static void fill_qp_rss_context(struct mlx4_qp_context *context,
				struct mlx4_ib_qp *qp)
{
	struct mlx4_rss_context *rss_context;

	rss_context = (void *)context + offsetof(struct mlx4_qp_context,
			pri_path) + MLX4_RSS_OFFSET_IN_QPC_PRI_PATH;

	rss_context->base_qpn = cpu_to_be32(qp->rss_ctx->base_qpn_tbl_sz);
	rss_context->default_qpn =
		cpu_to_be32(qp->rss_ctx->base_qpn_tbl_sz & 0xffffff);
	if (qp->rss_ctx->flags & (MLX4_RSS_UDP_IPV4 | MLX4_RSS_UDP_IPV6))
		rss_context->base_qpn_udp = rss_context->default_qpn;
	rss_context->flags = qp->rss_ctx->flags;
	/* Currently support just toeplitz */
	rss_context->hash_fn = MLX4_RSS_HASH_TOP;

	memcpy(rss_context->rss_key, qp->rss_ctx->rss_key,
	       MLX4_EN_RSS_KEY_SIZE);
}

static int __mlx4_ib_modify_qp(void *src, enum mlx4_ib_source_type src_type,
			       const struct ib_qp_attr *attr, int attr_mask,
			       enum ib_qp_state cur_state, enum ib_qp_state new_state)
{
	struct ib_uobject *ibuobject;
	struct ib_srq  *ibsrq;
	struct ib_rwq_ind_table *rwq_ind_tbl;
	enum ib_qp_type qp_type;
	struct mlx4_ib_dev *dev;
	struct mlx4_ib_qp *qp;
	struct mlx4_ib_pd *pd;
	struct mlx4_ib_cq *send_cq, *recv_cq;
	struct mlx4_qp_context *context;
	enum mlx4_qp_optpar optpar = 0;
	int sqd_event;
	int steer_qp = 0;
	int err = -EINVAL;
	int counter_index;

	if (src_type == MLX4_IB_RWQ_SRC) {
		struct ib_wq *ibwq;

		ibwq	    = (struct ib_wq *)src;
		ibuobject   = ibwq->uobject;
		ibsrq	    = NULL;
		rwq_ind_tbl = NULL;
		qp_type     = IB_QPT_RAW_PACKET;
		qp	    = to_mqp((struct ib_qp *)ibwq);
		dev	    = to_mdev(ibwq->device);
		pd	    = to_mpd(ibwq->pd);
	} else {
		struct ib_qp *ibqp;

		ibqp	    = (struct ib_qp *)src;
		ibuobject   = ibqp->uobject;
		ibsrq	    = ibqp->srq;
		rwq_ind_tbl = ibqp->rwq_ind_tbl;
		qp_type     = ibqp->qp_type;
		qp	    = to_mqp(ibqp);
		dev	    = to_mdev(ibqp->device);
		pd	    = get_pd(qp);
	}

	/* APM is not supported under RoCE */
	if (attr_mask & IB_QP_ALT_PATH &&
	    rdma_port_get_link_layer(&dev->ib_dev, qp->port) ==
	    IB_LINK_LAYER_ETHERNET)
		return -ENOTSUPP;

	context = kzalloc(sizeof *context, GFP_KERNEL);
	if (!context)
		return -ENOMEM;

	context->flags = cpu_to_be32((to_mlx4_state(new_state) << 28) |
				     (to_mlx4_st(dev, qp->mlx4_ib_qp_type) << 16));

	if (!(attr_mask & IB_QP_PATH_MIG_STATE))
		context->flags |= cpu_to_be32(MLX4_QP_PM_MIGRATED << 11);
	else {
		optpar |= MLX4_QP_OPTPAR_PM_STATE;
		switch (attr->path_mig_state) {
		case IB_MIG_MIGRATED:
			context->flags |= cpu_to_be32(MLX4_QP_PM_MIGRATED << 11);
			break;
		case IB_MIG_REARM:
			context->flags |= cpu_to_be32(MLX4_QP_PM_REARM << 11);
			break;
		case IB_MIG_ARMED:
			context->flags |= cpu_to_be32(MLX4_QP_PM_ARMED << 11);
			break;
		}
	}

	if (qp->inl_recv_sz)
		context->param3 |= cpu_to_be32(1 << 25);

	if (qp->flags & MLX4_IB_QP_SCATTER_FCS)
		context->param3 |= cpu_to_be32(1 << 29);

	if (qp_type == IB_QPT_GSI || qp_type == IB_QPT_SMI)
		context->mtu_msgmax = (IB_MTU_4096 << 5) | 11;
	else if (qp_type == IB_QPT_RAW_PACKET)
		context->mtu_msgmax = (MLX4_RAW_QP_MTU << 5) | MLX4_RAW_QP_MSGMAX;
	else if (qp_type == IB_QPT_UD) {
		if (qp->flags & MLX4_IB_QP_LSO)
			context->mtu_msgmax = (IB_MTU_4096 << 5) |
					      ilog2(dev->dev->caps.max_gso_sz);
		else
			context->mtu_msgmax = (IB_MTU_4096 << 5) | 13;
	} else if (attr_mask & IB_QP_PATH_MTU) {
		if (attr->path_mtu < IB_MTU_256 || attr->path_mtu > IB_MTU_4096) {
			pr_err("path MTU (%u) is invalid\n",
			       attr->path_mtu);
			goto out;
		}
		context->mtu_msgmax = (attr->path_mtu << 5) |
			ilog2(dev->dev->caps.max_msg_sz);
	}

	if (!rwq_ind_tbl) { /* PRM RSS receive side should be left zeros */
		if (qp->rq.wqe_cnt)
			context->rq_size_stride = ilog2(qp->rq.wqe_cnt) << 3;
		context->rq_size_stride |= qp->rq.wqe_shift - 4;
	}

	if (qp->sq.wqe_cnt)
		context->sq_size_stride = ilog2(qp->sq.wqe_cnt) << 3;
	context->sq_size_stride |= qp->sq.wqe_shift - 4;

	if (new_state == IB_QPS_RESET && qp->counter_index)
		mlx4_ib_free_qp_counter(dev, qp);

	if (cur_state == IB_QPS_RESET && new_state == IB_QPS_INIT) {
		context->sq_size_stride |= !!qp->sq_no_prefetch << 7;
		context->xrcd = cpu_to_be32((u32) qp->xrcdn);
		if (qp_type == IB_QPT_RAW_PACKET)
			context->param3 |= cpu_to_be32(1 << 30);
	}

	if (ibuobject)
		context->usr_page = cpu_to_be32(
			mlx4_to_hw_uar_index(dev->dev,
					     to_mucontext(ibuobject->context)
					     ->uar.index));
	else
		context->usr_page = cpu_to_be32(
			mlx4_to_hw_uar_index(dev->dev, dev->priv_uar.index));

	if (attr_mask & IB_QP_DEST_QPN)
		context->remote_qpn = cpu_to_be32(attr->dest_qp_num);

	if (attr_mask & IB_QP_PORT) {
		if (cur_state == IB_QPS_SQD && new_state == IB_QPS_SQD &&
		    !(attr_mask & IB_QP_AV)) {
			mlx4_set_sched(&context->pri_path, attr->port_num);
			optpar |= MLX4_QP_OPTPAR_SCHED_QUEUE;
		}
	}

	if (cur_state == IB_QPS_INIT && new_state == IB_QPS_RTR) {
		err = create_qp_lb_counter(dev, qp);
		if (err)
			goto out;

		counter_index =
			dev->counters_table[qp->port - 1].default_counter;
		if (qp->counter_index)
			counter_index = qp->counter_index->index;

		if (counter_index != -1) {
			context->pri_path.counter_index = counter_index;
			optpar |= MLX4_QP_OPTPAR_COUNTER_INDEX;
			if (qp->counter_index) {
				context->pri_path.fl |=
					MLX4_FL_ETH_SRC_CHECK_MC_LB;
				context->pri_path.vlan_control |=
					MLX4_CTRL_ETH_SRC_CHECK_IF_COUNTER;
			}
		} else
			context->pri_path.counter_index =
				MLX4_SINK_COUNTER_INDEX(dev->dev);

		if (qp->flags & MLX4_IB_QP_NETIF) {
			mlx4_ib_steer_qp_reg(dev, qp, 1);
			steer_qp = 1;
		}

		if (qp_type == IB_QPT_GSI) {
			enum ib_gid_type gid_type = qp->flags & MLX4_IB_ROCE_V2_GSI_QP ?
				IB_GID_TYPE_ROCE_UDP_ENCAP : IB_GID_TYPE_ROCE;
			u8 qpc_roce_mode = gid_type_to_qpc(gid_type);

			context->rlkey_roce_mode |= (qpc_roce_mode << 6);
		}
	}

	if (attr_mask & IB_QP_PKEY_INDEX) {
		if (qp->mlx4_ib_qp_type & MLX4_IB_QPT_ANY_SRIOV)
			context->pri_path.disable_pkey_check = 0x40;
		context->pri_path.pkey_index = attr->pkey_index;
		optpar |= MLX4_QP_OPTPAR_PKEY_INDEX;
	}

	if (attr_mask & IB_QP_AV) {
		u8 port_num = mlx4_is_bonded(dev->dev) ? 1 :
			attr_mask & IB_QP_PORT ? attr->port_num : qp->port;
		union ib_gid gid;
		struct ib_gid_attr gid_attr = {.gid_type = IB_GID_TYPE_IB};
		u16 vlan = 0xffff;
		u8 smac[ETH_ALEN];
		int status = 0;
		int is_eth =
			rdma_cap_eth_ah(&dev->ib_dev, port_num) &&
			rdma_ah_get_ah_flags(&attr->ah_attr) & IB_AH_GRH;

		if (is_eth) {
			int index =
				rdma_ah_read_grh(&attr->ah_attr)->sgid_index;

			status = ib_get_cached_gid(&dev->ib_dev, port_num,
						   index, &gid, &gid_attr);
			if (!status && gid_attr.ndev) {
				vlan = rdma_vlan_dev_vlan_id(gid_attr.ndev);
				memcpy(smac, gid_attr.ndev->dev_addr, ETH_ALEN);
				dev_put(gid_attr.ndev);
			}
		}
		if (status)
			goto out;

		if (mlx4_set_path(dev, attr, attr_mask, qp, &context->pri_path,
				  port_num, vlan, smac))
			goto out;

		optpar |= (MLX4_QP_OPTPAR_PRIMARY_ADDR_PATH |
			   MLX4_QP_OPTPAR_SCHED_QUEUE);

		if (is_eth &&
		    (cur_state == IB_QPS_INIT && new_state == IB_QPS_RTR)) {
			u8 qpc_roce_mode = gid_type_to_qpc(gid_attr.gid_type);

			if (qpc_roce_mode == MLX4_QPC_ROCE_MODE_UNDEFINED) {
				err = -EINVAL;
				goto out;
			}
			context->rlkey_roce_mode |= (qpc_roce_mode << 6);
		}

	}

	if (attr_mask & IB_QP_TIMEOUT) {
		context->pri_path.ackto |= attr->timeout << 3;
		optpar |= MLX4_QP_OPTPAR_ACK_TIMEOUT;
	}

	if (attr_mask & IB_QP_ALT_PATH) {
		if (attr->alt_port_num == 0 ||
		    attr->alt_port_num > dev->dev->caps.num_ports)
			goto out;

		if (attr->alt_pkey_index >=
		    dev->dev->caps.pkey_table_len[attr->alt_port_num])
			goto out;

		if (mlx4_set_alt_path(dev, attr, attr_mask, qp,
				      &context->alt_path,
				      attr->alt_port_num))
			goto out;

		context->alt_path.pkey_index = attr->alt_pkey_index;
		context->alt_path.ackto = attr->alt_timeout << 3;
		optpar |= MLX4_QP_OPTPAR_ALT_ADDR_PATH;
	}

	context->pd = cpu_to_be32(pd->pdn);

	if (!rwq_ind_tbl) {
		context->params1 = cpu_to_be32(MLX4_IB_ACK_REQ_FREQ << 28);
		get_cqs(qp, src_type, &send_cq, &recv_cq);
	} else { /* Set dummy CQs to be compatible with HV and PRM */
		send_cq = to_mcq(rwq_ind_tbl->ind_tbl[0]->cq);
		recv_cq = send_cq;
	}
	context->cqn_send = cpu_to_be32(send_cq->mcq.cqn);
	context->cqn_recv = cpu_to_be32(recv_cq->mcq.cqn);

	/* Set "fast registration enabled" for all kernel QPs */
	if (!ibuobject)
		context->params1 |= cpu_to_be32(1 << 11);

	if (attr_mask & IB_QP_RNR_RETRY) {
		context->params1 |= cpu_to_be32(attr->rnr_retry << 13);
		optpar |= MLX4_QP_OPTPAR_RNR_RETRY;
	}

	if (attr_mask & IB_QP_RETRY_CNT) {
		context->params1 |= cpu_to_be32(attr->retry_cnt << 16);
		optpar |= MLX4_QP_OPTPAR_RETRY_COUNT;
	}

	if (attr_mask & IB_QP_MAX_QP_RD_ATOMIC) {
		if (attr->max_rd_atomic)
			context->params1 |=
				cpu_to_be32(fls(attr->max_rd_atomic - 1) << 21);
		optpar |= MLX4_QP_OPTPAR_SRA_MAX;
	}

	if (attr_mask & IB_QP_SQ_PSN)
		context->next_send_psn = cpu_to_be32(attr->sq_psn);

	if (attr_mask & IB_QP_MAX_DEST_RD_ATOMIC) {
		if (attr->max_dest_rd_atomic)
			context->params2 |=
				cpu_to_be32(fls(attr->max_dest_rd_atomic - 1) << 21);
		optpar |= MLX4_QP_OPTPAR_RRA_MAX;
	}

	if (attr_mask & (IB_QP_ACCESS_FLAGS | IB_QP_MAX_DEST_RD_ATOMIC)) {
		context->params2 |= to_mlx4_access_flags(qp, attr, attr_mask);
		optpar |= MLX4_QP_OPTPAR_RWE | MLX4_QP_OPTPAR_RRE | MLX4_QP_OPTPAR_RAE;
	}

	if (ibsrq)
		context->params2 |= cpu_to_be32(MLX4_QP_BIT_RIC);

	if (attr_mask & IB_QP_MIN_RNR_TIMER) {
		context->rnr_nextrecvpsn |= cpu_to_be32(attr->min_rnr_timer << 24);
		optpar |= MLX4_QP_OPTPAR_RNR_TIMEOUT;
	}
	if (attr_mask & IB_QP_RQ_PSN)
		context->rnr_nextrecvpsn |= cpu_to_be32(attr->rq_psn);

	/* proxy and tunnel qp qkeys will be changed in modify-qp wrappers */
	if (attr_mask & IB_QP_QKEY) {
		if (qp->mlx4_ib_qp_type &
		    (MLX4_IB_QPT_PROXY_SMI_OWNER | MLX4_IB_QPT_TUN_SMI_OWNER))
			context->qkey = cpu_to_be32(IB_QP_SET_QKEY);
		else {
			if (mlx4_is_mfunc(dev->dev) &&
			    !(qp->mlx4_ib_qp_type & MLX4_IB_QPT_ANY_SRIOV) &&
			    (attr->qkey & MLX4_RESERVED_QKEY_MASK) ==
			    MLX4_RESERVED_QKEY_BASE) {
				pr_err("Cannot use reserved QKEY"
				       " 0x%x (range 0xffff0000..0xffffffff"
				       " is reserved)\n", attr->qkey);
				err = -EINVAL;
				goto out;
			}
			context->qkey = cpu_to_be32(attr->qkey);
		}
		optpar |= MLX4_QP_OPTPAR_Q_KEY;
	}

	if (ibsrq)
		context->srqn = cpu_to_be32(1 << 24 |
					    to_msrq(ibsrq)->msrq.srqn);

	if (qp->rq.wqe_cnt &&
	    cur_state == IB_QPS_RESET &&
	    new_state == IB_QPS_INIT)
		context->db_rec_addr = cpu_to_be64(qp->db.dma);

	if (cur_state == IB_QPS_INIT &&
	    new_state == IB_QPS_RTR  &&
	    (qp_type == IB_QPT_GSI || qp_type == IB_QPT_SMI ||
	     qp_type == IB_QPT_UD || qp_type == IB_QPT_RAW_PACKET)) {
		context->pri_path.sched_queue = (qp->port - 1) << 6;
		if (qp->mlx4_ib_qp_type == MLX4_IB_QPT_SMI ||
		    qp->mlx4_ib_qp_type &
		    (MLX4_IB_QPT_PROXY_SMI_OWNER | MLX4_IB_QPT_TUN_SMI_OWNER)) {
			context->pri_path.sched_queue |= MLX4_IB_DEFAULT_QP0_SCHED_QUEUE;
			if (qp->mlx4_ib_qp_type != MLX4_IB_QPT_SMI)
				context->pri_path.fl = 0x80;
		} else {
			if (qp->mlx4_ib_qp_type & MLX4_IB_QPT_ANY_SRIOV)
				context->pri_path.fl = 0x80;
			context->pri_path.sched_queue |= MLX4_IB_DEFAULT_SCHED_QUEUE;
		}
		if (rdma_port_get_link_layer(&dev->ib_dev, qp->port) ==
		    IB_LINK_LAYER_ETHERNET) {
			if (qp->mlx4_ib_qp_type == MLX4_IB_QPT_TUN_GSI ||
			    qp->mlx4_ib_qp_type == MLX4_IB_QPT_GSI)
				context->pri_path.feup = 1 << 7; /* don't fsm */
			/* handle smac_index */
			if (qp->mlx4_ib_qp_type == MLX4_IB_QPT_UD ||
			    qp->mlx4_ib_qp_type == MLX4_IB_QPT_PROXY_GSI ||
			    qp->mlx4_ib_qp_type == MLX4_IB_QPT_TUN_GSI) {
				err = handle_eth_ud_smac_index(dev, qp, context);
				if (err) {
					err = -EINVAL;
					goto out;
				}
				if (qp->mlx4_ib_qp_type == MLX4_IB_QPT_PROXY_GSI)
					dev->qp1_proxy[qp->port - 1] = qp;
			}
		}
	}

	if (qp_type == IB_QPT_RAW_PACKET) {
		context->pri_path.ackto = (context->pri_path.ackto & 0xf8) |
					MLX4_IB_LINK_TYPE_ETH;
		if (dev->dev->caps.tunnel_offload_mode ==  MLX4_TUNNEL_OFFLOAD_MODE_VXLAN) {
			/* set QP to receive both tunneled & non-tunneled packets */
			if (!rwq_ind_tbl)
				context->srqn = cpu_to_be32(7 << 28);
		}
	}

	if (qp_type == IB_QPT_UD && (new_state == IB_QPS_RTR)) {
		int is_eth = rdma_port_get_link_layer(
				&dev->ib_dev, qp->port) ==
				IB_LINK_LAYER_ETHERNET;
		if (is_eth) {
			context->pri_path.ackto = MLX4_IB_LINK_TYPE_ETH;
			optpar |= MLX4_QP_OPTPAR_PRIMARY_ADDR_PATH;
		}
	}

	if (cur_state == IB_QPS_RTS && new_state == IB_QPS_SQD	&&
	    attr_mask & IB_QP_EN_SQD_ASYNC_NOTIFY && attr->en_sqd_async_notify)
		sqd_event = 1;
	else
		sqd_event = 0;

	if (!ibuobject &&
	    cur_state == IB_QPS_RESET &&
	    new_state == IB_QPS_INIT)
		context->rlkey_roce_mode |= (1 << 4);

	/*
	 * Before passing a kernel QP to the HW, make sure that the
	 * ownership bits of the send queue are set and the SQ
	 * headroom is stamped so that the hardware doesn't start
	 * processing stale work requests.
	 */
	if (!ibuobject &&
	    cur_state == IB_QPS_RESET &&
	    new_state == IB_QPS_INIT) {
		struct mlx4_wqe_ctrl_seg *ctrl;
		int i;

		for (i = 0; i < qp->sq.wqe_cnt; ++i) {
			ctrl = get_send_wqe(qp, i);
			ctrl->owner_opcode = cpu_to_be32(1 << 31);
			if (qp->sq_max_wqes_per_wr == 1)
				ctrl->qpn_vlan.fence_size =
						1 << (qp->sq.wqe_shift - 4);

			stamp_send_wqe(qp, i, 1 << qp->sq.wqe_shift);
		}
	}

	if (rwq_ind_tbl	&&
	    cur_state == IB_QPS_RESET &&
	    new_state == IB_QPS_INIT) {
		fill_qp_rss_context(context, qp);
		context->flags |= cpu_to_be32(1 << MLX4_RSS_QPC_FLAG_OFFSET);
	}

	err = mlx4_qp_modify(dev->dev, &qp->mtt, to_mlx4_state(cur_state),
			     to_mlx4_state(new_state), context, optpar,
			     sqd_event, &qp->mqp);
	if (err)
		goto out;

	qp->state = new_state;

	if (attr_mask & IB_QP_ACCESS_FLAGS)
		qp->atomic_rd_en = attr->qp_access_flags;
	if (attr_mask & IB_QP_MAX_DEST_RD_ATOMIC)
		qp->resp_depth = attr->max_dest_rd_atomic;
	if (attr_mask & IB_QP_PORT) {
		qp->port = attr->port_num;
		update_mcg_macs(dev, qp);
	}
	if (attr_mask & IB_QP_ALT_PATH)
		qp->alt_port = attr->alt_port_num;

	if (is_sqp(dev, qp))
		store_sqp_attrs(to_msqp(qp), attr, attr_mask);

	/*
	 * If we moved QP0 to RTR, bring the IB link up; if we moved
	 * QP0 to RESET or ERROR, bring the link back down.
	 */
	if (is_qp0(dev, qp)) {
		if (cur_state != IB_QPS_RTR && new_state == IB_QPS_RTR)
			if (mlx4_INIT_PORT(dev->dev, qp->port))
				pr_warn("INIT_PORT failed for port %d\n",
				       qp->port);

		if (cur_state != IB_QPS_RESET && cur_state != IB_QPS_ERR &&
		    (new_state == IB_QPS_RESET || new_state == IB_QPS_ERR))
			mlx4_CLOSE_PORT(dev->dev, qp->port);
	}

	/*
	 * If we moved a kernel QP to RESET, clean up all old CQ
	 * entries and reinitialize the QP.
	 */
	if (new_state == IB_QPS_RESET) {
		if (!ibuobject) {
			mlx4_ib_cq_clean(recv_cq, qp->mqp.qpn,
					 ibsrq ? to_msrq(ibsrq) : NULL);
			if (send_cq != recv_cq)
				mlx4_ib_cq_clean(send_cq, qp->mqp.qpn, NULL);

			qp->rq.head = 0;
			qp->rq.tail = 0;
			qp->sq.head = 0;
			qp->sq.tail = 0;
			qp->sq_next_wqe = 0;
			if (qp->rq.wqe_cnt)
				*qp->db.db  = 0;

			if (qp->flags & MLX4_IB_QP_NETIF)
				mlx4_ib_steer_qp_reg(dev, qp, 0);
		}
		if (qp->pri.smac || (!qp->pri.smac && qp->pri.smac_port)) {
			mlx4_unregister_mac(dev->dev, qp->pri.smac_port, qp->pri.smac);
			qp->pri.smac = 0;
			qp->pri.smac_port = 0;
		}
		if (qp->alt.smac) {
			mlx4_unregister_mac(dev->dev, qp->alt.smac_port, qp->alt.smac);
			qp->alt.smac = 0;
		}
		if (qp->pri.vid < 0x1000) {
			mlx4_unregister_vlan(dev->dev, qp->pri.vlan_port, qp->pri.vid);
			qp->pri.vid = 0xFFFF;
			qp->pri.candidate_vid = 0xFFFF;
			qp->pri.update_vid = 0;
		}

		if (qp->alt.vid < 0x1000) {
			mlx4_unregister_vlan(dev->dev, qp->alt.vlan_port, qp->alt.vid);
			qp->alt.vid = 0xFFFF;
			qp->alt.candidate_vid = 0xFFFF;
			qp->alt.update_vid = 0;
		}
	}
out:
	if (err && qp->counter_index)
		mlx4_ib_free_qp_counter(dev, qp);
	if (err && steer_qp)
		mlx4_ib_steer_qp_reg(dev, qp, 0);
	kfree(context);
	if (qp->pri.candidate_smac ||
	    (!qp->pri.candidate_smac && qp->pri.candidate_smac_port)) {
		if (err) {
			mlx4_unregister_mac(dev->dev, qp->pri.candidate_smac_port, qp->pri.candidate_smac);
		} else {
			if (qp->pri.smac || (!qp->pri.smac && qp->pri.smac_port))
				mlx4_unregister_mac(dev->dev, qp->pri.smac_port, qp->pri.smac);
			qp->pri.smac = qp->pri.candidate_smac;
			qp->pri.smac_index = qp->pri.candidate_smac_index;
			qp->pri.smac_port = qp->pri.candidate_smac_port;
		}
		qp->pri.candidate_smac = 0;
		qp->pri.candidate_smac_index = 0;
		qp->pri.candidate_smac_port = 0;
	}
	if (qp->alt.candidate_smac) {
		if (err) {
			mlx4_unregister_mac(dev->dev, qp->alt.candidate_smac_port, qp->alt.candidate_smac);
		} else {
			if (qp->alt.smac)
				mlx4_unregister_mac(dev->dev, qp->alt.smac_port, qp->alt.smac);
			qp->alt.smac = qp->alt.candidate_smac;
			qp->alt.smac_index = qp->alt.candidate_smac_index;
			qp->alt.smac_port = qp->alt.candidate_smac_port;
		}
		qp->alt.candidate_smac = 0;
		qp->alt.candidate_smac_index = 0;
		qp->alt.candidate_smac_port = 0;
	}

	if (qp->pri.update_vid) {
		if (err) {
			if (qp->pri.candidate_vid < 0x1000)
				mlx4_unregister_vlan(dev->dev, qp->pri.candidate_vlan_port,
						     qp->pri.candidate_vid);
		} else {
			if (qp->pri.vid < 0x1000)
				mlx4_unregister_vlan(dev->dev, qp->pri.vlan_port,
						     qp->pri.vid);
			qp->pri.vid = qp->pri.candidate_vid;
			qp->pri.vlan_port = qp->pri.candidate_vlan_port;
			qp->pri.vlan_index =  qp->pri.candidate_vlan_index;
		}
		qp->pri.candidate_vid = 0xFFFF;
		qp->pri.update_vid = 0;
	}

	if (qp->alt.update_vid) {
		if (err) {
			if (qp->alt.candidate_vid < 0x1000)
				mlx4_unregister_vlan(dev->dev, qp->alt.candidate_vlan_port,
						     qp->alt.candidate_vid);
		} else {
			if (qp->alt.vid < 0x1000)
				mlx4_unregister_vlan(dev->dev, qp->alt.vlan_port,
						     qp->alt.vid);
			qp->alt.vid = qp->alt.candidate_vid;
			qp->alt.vlan_port = qp->alt.candidate_vlan_port;
			qp->alt.vlan_index =  qp->alt.candidate_vlan_index;
		}
		qp->alt.candidate_vid = 0xFFFF;
		qp->alt.update_vid = 0;
	}

	return err;
}

enum {
	MLX4_IB_MODIFY_QP_RSS_SUP_ATTR_MSK = (IB_QP_STATE	|
					      IB_QP_PORT),
};

static int _mlx4_ib_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr,
			      int attr_mask, struct ib_udata *udata)
{
	enum rdma_link_layer ll = IB_LINK_LAYER_UNSPECIFIED;
	struct mlx4_ib_dev *dev = to_mdev(ibqp->device);
	struct mlx4_ib_qp *qp = to_mqp(ibqp);
	enum ib_qp_state cur_state, new_state;
	int err = -EINVAL;
	mutex_lock(&qp->mutex);

	cur_state = attr_mask & IB_QP_CUR_STATE ? attr->cur_qp_state : qp->state;
	new_state = attr_mask & IB_QP_STATE ? attr->qp_state : cur_state;

	if (cur_state != new_state || cur_state != IB_QPS_RESET) {
		int port = attr_mask & IB_QP_PORT ? attr->port_num : qp->port;
		ll = rdma_port_get_link_layer(&dev->ib_dev, port);
	}

	if (!ib_modify_qp_is_ok(cur_state, new_state, ibqp->qp_type,
				attr_mask, ll)) {
		pr_debug("qpn 0x%x: invalid attribute mask specified "
			 "for transition %d to %d. qp_type %d,"
			 " attr_mask 0x%x\n",
			 ibqp->qp_num, cur_state, new_state,
			 ibqp->qp_type, attr_mask);
		goto out;
	}

	if (ibqp->rwq_ind_tbl) {
		if (!(((cur_state == IB_QPS_RESET) &&
		       (new_state == IB_QPS_INIT)) ||
		      ((cur_state == IB_QPS_INIT)  &&
		       (new_state == IB_QPS_RTR)))) {
			pr_debug("qpn 0x%x: RSS QP unsupported transition %d to %d\n",
				 ibqp->qp_num, cur_state, new_state);

			err = -EOPNOTSUPP;
			goto out;
		}

		if (attr_mask & ~MLX4_IB_MODIFY_QP_RSS_SUP_ATTR_MSK) {
			pr_debug("qpn 0x%x: RSS QP unsupported attribute mask 0x%x for transition %d to %d\n",
				 ibqp->qp_num, attr_mask, cur_state, new_state);

			err = -EOPNOTSUPP;
			goto out;
		}
	}

	if (mlx4_is_bonded(dev->dev) && (attr_mask & IB_QP_PORT)) {
		if ((cur_state == IB_QPS_RESET) && (new_state == IB_QPS_INIT)) {
			if ((ibqp->qp_type == IB_QPT_RC) ||
			    (ibqp->qp_type == IB_QPT_UD) ||
			    (ibqp->qp_type == IB_QPT_UC) ||
			    (ibqp->qp_type == IB_QPT_RAW_PACKET) ||
			    (ibqp->qp_type == IB_QPT_XRC_INI)) {
				attr->port_num = mlx4_ib_bond_next_port(dev);
			}
		} else {
			/* no sense in changing port_num
			 * when ports are bonded */
			attr_mask &= ~IB_QP_PORT;
		}
	}

	if ((attr_mask & IB_QP_PORT) &&
	    (attr->port_num == 0 || attr->port_num > dev->num_ports)) {
		pr_debug("qpn 0x%x: invalid port number (%d) specified "
			 "for transition %d to %d. qp_type %d\n",
			 ibqp->qp_num, attr->port_num, cur_state,
			 new_state, ibqp->qp_type);
		goto out;
	}

	if ((attr_mask & IB_QP_PORT) && (ibqp->qp_type == IB_QPT_RAW_PACKET) &&
	    (rdma_port_get_link_layer(&dev->ib_dev, attr->port_num) !=
	     IB_LINK_LAYER_ETHERNET))
		goto out;

	if (attr_mask & IB_QP_PKEY_INDEX) {
		int p = attr_mask & IB_QP_PORT ? attr->port_num : qp->port;
		if (attr->pkey_index >= dev->dev->caps.pkey_table_len[p]) {
			pr_debug("qpn 0x%x: invalid pkey index (%d) specified "
				 "for transition %d to %d. qp_type %d\n",
				 ibqp->qp_num, attr->pkey_index, cur_state,
				 new_state, ibqp->qp_type);
			goto out;
		}
	}

	if (attr_mask & IB_QP_MAX_QP_RD_ATOMIC &&
	    attr->max_rd_atomic > dev->dev->caps.max_qp_init_rdma) {
		pr_debug("qpn 0x%x: max_rd_atomic (%d) too large. "
			 "Transition %d to %d. qp_type %d\n",
			 ibqp->qp_num, attr->max_rd_atomic, cur_state,
			 new_state, ibqp->qp_type);
		goto out;
	}

	if (attr_mask & IB_QP_MAX_DEST_RD_ATOMIC &&
	    attr->max_dest_rd_atomic > dev->dev->caps.max_qp_dest_rdma) {
		pr_debug("qpn 0x%x: max_dest_rd_atomic (%d) too large. "
			 "Transition %d to %d. qp_type %d\n",
			 ibqp->qp_num, attr->max_dest_rd_atomic, cur_state,
			 new_state, ibqp->qp_type);
		goto out;
	}

	if (cur_state == new_state && cur_state == IB_QPS_RESET) {
		err = 0;
		goto out;
	}

	if (ibqp->rwq_ind_tbl && (new_state == IB_QPS_INIT)) {
		err = bringup_rss_rwqs(ibqp->rwq_ind_tbl, attr->port_num);
		if (err)
			goto out;
	}

	err = __mlx4_ib_modify_qp(ibqp, MLX4_IB_QP_SRC, attr, attr_mask,
				  cur_state, new_state);

	if (ibqp->rwq_ind_tbl && err)
		bring_down_rss_rwqs(ibqp->rwq_ind_tbl);

	if (mlx4_is_bonded(dev->dev) && (attr_mask & IB_QP_PORT))
		attr->port_num = 1;

out:
	mutex_unlock(&qp->mutex);
	return err;
}

int mlx4_ib_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr,
		      int attr_mask, struct ib_udata *udata)
{
	struct mlx4_ib_qp *mqp = to_mqp(ibqp);
	int ret;

	ret = _mlx4_ib_modify_qp(ibqp, attr, attr_mask, udata);

	if (mqp->mlx4_ib_qp_type == MLX4_IB_QPT_GSI) {
		struct mlx4_ib_sqp *sqp = to_msqp(mqp);
		int err = 0;

		if (sqp->roce_v2_gsi)
			err = ib_modify_qp(sqp->roce_v2_gsi, attr, attr_mask);
		if (err)
			pr_err("Failed to modify GSI QP for RoCEv2 (%d)\n",
			       err);
	}
	return ret;
}

static int vf_get_qp0_qkey(struct mlx4_dev *dev, int qpn, u32 *qkey)
{
	int i;
	for (i = 0; i < dev->caps.num_ports; i++) {
		if (qpn == dev->caps.spec_qps[i].qp0_proxy ||
		    qpn == dev->caps.spec_qps[i].qp0_tunnel) {
			*qkey = dev->caps.spec_qps[i].qp0_qkey;
			return 0;
		}
	}
	return -EINVAL;
}

static int build_sriov_qp0_header(struct mlx4_ib_sqp *sqp,
				  struct ib_ud_wr *wr,
				  void *wqe, unsigned *mlx_seg_len)
{
	struct mlx4_ib_dev *mdev = to_mdev(sqp->qp.ibqp.device);
	struct ib_device *ib_dev = &mdev->ib_dev;
	struct mlx4_wqe_mlx_seg *mlx = wqe;
	struct mlx4_wqe_inline_seg *inl = wqe + sizeof *mlx;
	struct mlx4_ib_ah *ah = to_mah(wr->ah);
	u16 pkey;
	u32 qkey;
	int send_size;
	int header_size;
	int spc;
	int i;

	if (wr->wr.opcode != IB_WR_SEND)
		return -EINVAL;

	send_size = 0;

	for (i = 0; i < wr->wr.num_sge; ++i)
		send_size += wr->wr.sg_list[i].length;

	/* for proxy-qp0 sends, need to add in size of tunnel header */
	/* for tunnel-qp0 sends, tunnel header is already in s/g list */
	if (sqp->qp.mlx4_ib_qp_type == MLX4_IB_QPT_PROXY_SMI_OWNER)
		send_size += sizeof (struct mlx4_ib_tunnel_header);

	ib_ud_header_init(send_size, 1, 0, 0, 0, 0, 0, 0, &sqp->ud_header);

	if (sqp->qp.mlx4_ib_qp_type == MLX4_IB_QPT_PROXY_SMI_OWNER) {
		sqp->ud_header.lrh.service_level =
			be32_to_cpu(ah->av.ib.sl_tclass_flowlabel) >> 28;
		sqp->ud_header.lrh.destination_lid =
			cpu_to_be16(ah->av.ib.g_slid & 0x7f);
		sqp->ud_header.lrh.source_lid =
			cpu_to_be16(ah->av.ib.g_slid & 0x7f);
	}

	mlx->flags &= cpu_to_be32(MLX4_WQE_CTRL_CQ_UPDATE);

	/* force loopback */
	mlx->flags |= cpu_to_be32(MLX4_WQE_MLX_VL15 | 0x1 | MLX4_WQE_MLX_SLR);
	mlx->rlid = sqp->ud_header.lrh.destination_lid;

	sqp->ud_header.lrh.virtual_lane    = 0;
	sqp->ud_header.bth.solicited_event = !!(wr->wr.send_flags & IB_SEND_SOLICITED);
	ib_get_cached_pkey(ib_dev, sqp->qp.port, 0, &pkey);
	sqp->ud_header.bth.pkey = cpu_to_be16(pkey);
	if (sqp->qp.mlx4_ib_qp_type == MLX4_IB_QPT_TUN_SMI_OWNER)
		sqp->ud_header.bth.destination_qpn = cpu_to_be32(wr->remote_qpn);
	else
		sqp->ud_header.bth.destination_qpn =
			cpu_to_be32(mdev->dev->caps.spec_qps[sqp->qp.port - 1].qp0_tunnel);

	sqp->ud_header.bth.psn = cpu_to_be32((sqp->send_psn++) & ((1 << 24) - 1));
	if (mlx4_is_master(mdev->dev)) {
		if (mlx4_get_parav_qkey(mdev->dev, sqp->qp.mqp.qpn, &qkey))
			return -EINVAL;
	} else {
		if (vf_get_qp0_qkey(mdev->dev, sqp->qp.mqp.qpn, &qkey))
			return -EINVAL;
	}
	sqp->ud_header.deth.qkey = cpu_to_be32(qkey);
	sqp->ud_header.deth.source_qpn = cpu_to_be32(sqp->qp.mqp.qpn);

	sqp->ud_header.bth.opcode        = IB_OPCODE_UD_SEND_ONLY;
	sqp->ud_header.immediate_present = 0;

	header_size = ib_ud_header_pack(&sqp->ud_header, sqp->header_buf);

	/*
	 * Inline data segments may not cross a 64 byte boundary.  If
	 * our UD header is bigger than the space available up to the
	 * next 64 byte boundary in the WQE, use two inline data
	 * segments to hold the UD header.
	 */
	spc = MLX4_INLINE_ALIGN -
	      ((unsigned long) (inl + 1) & (MLX4_INLINE_ALIGN - 1));
	if (header_size <= spc) {
		inl->byte_count = cpu_to_be32(1 << 31 | header_size);
		memcpy(inl + 1, sqp->header_buf, header_size);
		i = 1;
	} else {
		inl->byte_count = cpu_to_be32(1 << 31 | spc);
		memcpy(inl + 1, sqp->header_buf, spc);

		inl = (void *) (inl + 1) + spc;
		memcpy(inl + 1, sqp->header_buf + spc, header_size - spc);
		/*
		 * Need a barrier here to make sure all the data is
		 * visible before the byte_count field is set.
		 * Otherwise the HCA prefetcher could grab the 64-byte
		 * chunk with this inline segment and get a valid (!=
		 * 0xffffffff) byte count but stale data, and end up
		 * generating a packet with bad headers.
		 *
		 * The first inline segment's byte_count field doesn't
		 * need a barrier, because it comes after a
		 * control/MLX segment and therefore is at an offset
		 * of 16 mod 64.
		 */
		wmb();
		inl->byte_count = cpu_to_be32(1 << 31 | (header_size - spc));
		i = 2;
	}

	*mlx_seg_len =
	ALIGN(i * sizeof (struct mlx4_wqe_inline_seg) + header_size, 16);
	return 0;
}

static u8 sl_to_vl(struct mlx4_ib_dev *dev, u8 sl, int port_num)
{
	union sl2vl_tbl_to_u64 tmp_vltab;
	u8 vl;

	if (sl > 15)
		return 0xf;
	tmp_vltab.sl64 = atomic64_read(&dev->sl2vl[port_num - 1]);
	vl = tmp_vltab.sl8[sl >> 1];
	if (sl & 1)
		vl &= 0x0f;
	else
		vl >>= 4;
	return vl;
}

static int fill_gid_by_hw_index(struct mlx4_ib_dev *ibdev, u8 port_num,
				int index, union ib_gid *gid,
				enum ib_gid_type *gid_type)
{
	struct mlx4_ib_iboe *iboe = &ibdev->iboe;
	struct mlx4_port_gid_table *port_gid_table;
	unsigned long flags;

	port_gid_table = &iboe->gids[port_num - 1];
	spin_lock_irqsave(&iboe->lock, flags);
	memcpy(gid, &port_gid_table->gids[index].gid, sizeof(*gid));
	*gid_type = port_gid_table->gids[index].gid_type;
	spin_unlock_irqrestore(&iboe->lock, flags);
	if (!memcmp(gid, &zgid, sizeof(*gid)))
		return -ENOENT;

	return 0;
}

#define MLX4_ROCEV2_QP1_SPORT 0xC000
static int build_mlx_header(struct mlx4_ib_sqp *sqp, struct ib_ud_wr *wr,
			    void *wqe, unsigned *mlx_seg_len)
{
	struct ib_device *ib_dev = sqp->qp.ibqp.device;
	struct mlx4_ib_dev *ibdev = to_mdev(ib_dev);
	struct mlx4_wqe_mlx_seg *mlx = wqe;
	struct mlx4_wqe_ctrl_seg *ctrl = wqe;
	struct mlx4_wqe_inline_seg *inl = wqe + sizeof *mlx;
	struct mlx4_ib_ah *ah = to_mah(wr->ah);
	union ib_gid sgid;
	u16 pkey;
	int send_size;
	int header_size;
	int spc;
	int i;
	int err = 0;
	u16 vlan = 0xffff;
	bool is_eth;
	bool is_vlan = false;
	bool is_grh;
	bool is_udp = false;
	int ip_version = 0;

	send_size = 0;
	for (i = 0; i < wr->wr.num_sge; ++i)
		send_size += wr->wr.sg_list[i].length;

	is_eth = rdma_port_get_link_layer(sqp->qp.ibqp.device, sqp->qp.port) == IB_LINK_LAYER_ETHERNET;
	is_grh = mlx4_ib_ah_grh_present(ah);
	if (is_eth) {
		enum ib_gid_type gid_type;
		if (mlx4_is_mfunc(to_mdev(ib_dev)->dev)) {
			/* When multi-function is enabled, the ib_core gid
			 * indexes don't necessarily match the hw ones, so
			 * we must use our own cache */
			err = mlx4_get_roce_gid_from_slave(to_mdev(ib_dev)->dev,
							   be32_to_cpu(ah->av.ib.port_pd) >> 24,
							   ah->av.ib.gid_index, &sgid.raw[0]);
			if (err)
				return err;
		} else  {
			err = fill_gid_by_hw_index(ibdev, sqp->qp.port,
					    ah->av.ib.gid_index,
					    &sgid, &gid_type);
			if (!err) {
				is_udp = gid_type == IB_GID_TYPE_ROCE_UDP_ENCAP;
				if (is_udp) {
					if (ipv6_addr_v4mapped((struct in6_addr *)&sgid))
						ip_version = 4;
					else
						ip_version = 6;
					is_grh = false;
				}
			} else {
				return err;
			}
		}
		if (ah->av.eth.vlan != cpu_to_be16(0xffff)) {
			vlan = be16_to_cpu(ah->av.eth.vlan) & 0x0fff;
			is_vlan = 1;
		}
	}
	err = ib_ud_header_init(send_size, !is_eth, is_eth, is_vlan, is_grh,
			  ip_version, is_udp, 0, &sqp->ud_header);
	if (err)
		return err;

	if (!is_eth) {
		sqp->ud_header.lrh.service_level =
			be32_to_cpu(ah->av.ib.sl_tclass_flowlabel) >> 28;
		sqp->ud_header.lrh.destination_lid = ah->av.ib.dlid;
		sqp->ud_header.lrh.source_lid = cpu_to_be16(ah->av.ib.g_slid & 0x7f);
	}

	if (is_grh || (ip_version == 6)) {
		sqp->ud_header.grh.traffic_class =
			(be32_to_cpu(ah->av.ib.sl_tclass_flowlabel) >> 20) & 0xff;
		sqp->ud_header.grh.flow_label    =
			ah->av.ib.sl_tclass_flowlabel & cpu_to_be32(0xfffff);
		sqp->ud_header.grh.hop_limit     = ah->av.ib.hop_limit;
		if (is_eth) {
			memcpy(sqp->ud_header.grh.source_gid.raw, sgid.raw, 16);
		} else {
			if (mlx4_is_mfunc(to_mdev(ib_dev)->dev)) {
				/* When multi-function is enabled, the ib_core gid
				 * indexes don't necessarily match the hw ones, so
				 * we must use our own cache
				 */
				sqp->ud_header.grh.source_gid.global.subnet_prefix =
					cpu_to_be64(atomic64_read(&(to_mdev(ib_dev)->sriov.
								    demux[sqp->qp.port - 1].
								    subnet_prefix)));
				sqp->ud_header.grh.source_gid.global.interface_id =
					to_mdev(ib_dev)->sriov.demux[sqp->qp.port - 1].
						       guid_cache[ah->av.ib.gid_index];
			} else {
				ib_get_cached_gid(ib_dev,
						  be32_to_cpu(ah->av.ib.port_pd) >> 24,
						  ah->av.ib.gid_index,
						  &sqp->ud_header.grh.source_gid, NULL);
			}
		}
		memcpy(sqp->ud_header.grh.destination_gid.raw,
		       ah->av.ib.dgid, 16);
	}

	if (ip_version == 4) {
		sqp->ud_header.ip4.tos =
			(be32_to_cpu(ah->av.ib.sl_tclass_flowlabel) >> 20) & 0xff;
		sqp->ud_header.ip4.id = 0;
		sqp->ud_header.ip4.frag_off = htons(IP_DF);
		sqp->ud_header.ip4.ttl = ah->av.eth.hop_limit;

		memcpy(&sqp->ud_header.ip4.saddr,
		       sgid.raw + 12, 4);
		memcpy(&sqp->ud_header.ip4.daddr, ah->av.ib.dgid + 12, 4);
		sqp->ud_header.ip4.check = ib_ud_ip4_csum(&sqp->ud_header);
	}

	if (is_udp) {
		sqp->ud_header.udp.dport = htons(ROCE_V2_UDP_DPORT);
		sqp->ud_header.udp.sport = htons(MLX4_ROCEV2_QP1_SPORT);
		sqp->ud_header.udp.csum = 0;
	}

	mlx->flags &= cpu_to_be32(MLX4_WQE_CTRL_CQ_UPDATE);

	if (!is_eth) {
		mlx->flags |= cpu_to_be32((!sqp->qp.ibqp.qp_num ? MLX4_WQE_MLX_VL15 : 0) |
					  (sqp->ud_header.lrh.destination_lid ==
					   IB_LID_PERMISSIVE ? MLX4_WQE_MLX_SLR : 0) |
					  (sqp->ud_header.lrh.service_level << 8));
		if (ah->av.ib.port_pd & cpu_to_be32(0x80000000))
			mlx->flags |= cpu_to_be32(0x1); /* force loopback */
		mlx->rlid = sqp->ud_header.lrh.destination_lid;
	}

	switch (wr->wr.opcode) {
	case IB_WR_SEND:
		sqp->ud_header.bth.opcode	 = IB_OPCODE_UD_SEND_ONLY;
		sqp->ud_header.immediate_present = 0;
		break;
	case IB_WR_SEND_WITH_IMM:
		sqp->ud_header.bth.opcode	 = IB_OPCODE_UD_SEND_ONLY_WITH_IMMEDIATE;
		sqp->ud_header.immediate_present = 1;
		sqp->ud_header.immediate_data    = wr->wr.ex.imm_data;
		break;
	default:
		return -EINVAL;
	}

	if (is_eth) {
		struct in6_addr in6;
		u16 ether_type;
		u16 pcp = (be32_to_cpu(ah->av.ib.sl_tclass_flowlabel) >> 29) << 13;

		ether_type = (!is_udp) ? ETH_P_IBOE:
			(ip_version == 4 ? ETH_P_IP : ETH_P_IPV6);

		mlx->sched_prio = cpu_to_be16(pcp);

		ether_addr_copy(sqp->ud_header.eth.smac_h, ah->av.eth.s_mac);
		memcpy(sqp->ud_header.eth.dmac_h, ah->av.eth.mac, 6);
		memcpy(&ctrl->srcrb_flags16[0], ah->av.eth.mac, 2);
		memcpy(&ctrl->imm, ah->av.eth.mac + 2, 4);
		memcpy(&in6, sgid.raw, sizeof(in6));


		if (!memcmp(sqp->ud_header.eth.smac_h, sqp->ud_header.eth.dmac_h, 6))
			mlx->flags |= cpu_to_be32(MLX4_WQE_CTRL_FORCE_LOOPBACK);
		if (!is_vlan) {
			sqp->ud_header.eth.type = cpu_to_be16(ether_type);
		} else {
			sqp->ud_header.vlan.type = cpu_to_be16(ether_type);
			sqp->ud_header.vlan.tag = cpu_to_be16(vlan | pcp);
		}
	} else {
		sqp->ud_header.lrh.virtual_lane    = !sqp->qp.ibqp.qp_num ? 15 :
							sl_to_vl(to_mdev(ib_dev),
								 sqp->ud_header.lrh.service_level,
								 sqp->qp.port);
		if (sqp->qp.ibqp.qp_num && sqp->ud_header.lrh.virtual_lane == 15)
			return -EINVAL;
		if (sqp->ud_header.lrh.destination_lid == IB_LID_PERMISSIVE)
			sqp->ud_header.lrh.source_lid = IB_LID_PERMISSIVE;
	}
	sqp->ud_header.bth.solicited_event = !!(wr->wr.send_flags & IB_SEND_SOLICITED);
	if (!sqp->qp.ibqp.qp_num)
		ib_get_cached_pkey(ib_dev, sqp->qp.port, sqp->pkey_index, &pkey);
	else
		ib_get_cached_pkey(ib_dev, sqp->qp.port, wr->pkey_index, &pkey);
	sqp->ud_header.bth.pkey = cpu_to_be16(pkey);
	sqp->ud_header.bth.destination_qpn = cpu_to_be32(wr->remote_qpn);
	sqp->ud_header.bth.psn = cpu_to_be32((sqp->send_psn++) & ((1 << 24) - 1));
	sqp->ud_header.deth.qkey = cpu_to_be32(wr->remote_qkey & 0x80000000 ?
					       sqp->qkey : wr->remote_qkey);
	sqp->ud_header.deth.source_qpn = cpu_to_be32(sqp->qp.ibqp.qp_num);

	header_size = ib_ud_header_pack(&sqp->ud_header, sqp->header_buf);

	if (0) {
		pr_err("built UD header of size %d:\n", header_size);
		for (i = 0; i < header_size / 4; ++i) {
			if (i % 8 == 0)
				pr_err("  [%02x] ", i * 4);
			pr_cont(" %08x",
				be32_to_cpu(((__be32 *) sqp->header_buf)[i]));
			if ((i + 1) % 8 == 0)
				pr_cont("\n");
		}
		pr_err("\n");
	}

	/*
	 * Inline data segments may not cross a 64 byte boundary.  If
	 * our UD header is bigger than the space available up to the
	 * next 64 byte boundary in the WQE, use two inline data
	 * segments to hold the UD header.
	 */
	spc = MLX4_INLINE_ALIGN -
		((unsigned long) (inl + 1) & (MLX4_INLINE_ALIGN - 1));
	if (header_size <= spc) {
		inl->byte_count = cpu_to_be32(1 << 31 | header_size);
		memcpy(inl + 1, sqp->header_buf, header_size);
		i = 1;
	} else {
		inl->byte_count = cpu_to_be32(1 << 31 | spc);
		memcpy(inl + 1, sqp->header_buf, spc);

		inl = (void *) (inl + 1) + spc;
		memcpy(inl + 1, sqp->header_buf + spc, header_size - spc);
		/*
		 * Need a barrier here to make sure all the data is
		 * visible before the byte_count field is set.
		 * Otherwise the HCA prefetcher could grab the 64-byte
		 * chunk with this inline segment and get a valid (!=
		 * 0xffffffff) byte count but stale data, and end up
		 * generating a packet with bad headers.
		 *
		 * The first inline segment's byte_count field doesn't
		 * need a barrier, because it comes after a
		 * control/MLX segment and therefore is at an offset
		 * of 16 mod 64.
		 */
		wmb();
		inl->byte_count = cpu_to_be32(1 << 31 | (header_size - spc));
		i = 2;
	}

	*mlx_seg_len =
		ALIGN(i * sizeof (struct mlx4_wqe_inline_seg) + header_size, 16);
	return 0;
}

static int mlx4_wq_overflow(struct mlx4_ib_wq *wq, int nreq, struct ib_cq *ib_cq)
{
	unsigned cur;
	struct mlx4_ib_cq *cq;

	cur = wq->head - wq->tail;
	if (likely(cur + nreq < wq->max_post))
		return 0;

	cq = to_mcq(ib_cq);
	spin_lock(&cq->lock);
	cur = wq->head - wq->tail;
	spin_unlock(&cq->lock);

	return cur + nreq >= wq->max_post;
}

static __be32 convert_access(int acc)
{
	return (acc & IB_ACCESS_REMOTE_ATOMIC ?
		cpu_to_be32(MLX4_WQE_FMR_AND_BIND_PERM_ATOMIC)       : 0) |
	       (acc & IB_ACCESS_REMOTE_WRITE  ?
		cpu_to_be32(MLX4_WQE_FMR_AND_BIND_PERM_REMOTE_WRITE) : 0) |
	       (acc & IB_ACCESS_REMOTE_READ   ?
		cpu_to_be32(MLX4_WQE_FMR_AND_BIND_PERM_REMOTE_READ)  : 0) |
	       (acc & IB_ACCESS_LOCAL_WRITE   ? cpu_to_be32(MLX4_WQE_FMR_PERM_LOCAL_WRITE)  : 0) |
		cpu_to_be32(MLX4_WQE_FMR_PERM_LOCAL_READ);
}

static void set_reg_seg(struct mlx4_wqe_fmr_seg *fseg,
			struct ib_reg_wr *wr)
{
	struct mlx4_ib_mr *mr = to_mmr(wr->mr);

	fseg->flags		= convert_access(wr->access);
	fseg->mem_key		= cpu_to_be32(wr->key);
	fseg->buf_list		= cpu_to_be64(mr->page_map);
	fseg->start_addr	= cpu_to_be64(mr->ibmr.iova);
	fseg->reg_len		= cpu_to_be64(mr->ibmr.length);
	fseg->offset		= 0; /* XXX -- is this just for ZBVA? */
	fseg->page_size		= cpu_to_be32(ilog2(mr->ibmr.page_size));
	fseg->reserved[0]	= 0;
	fseg->reserved[1]	= 0;
}

static void set_local_inv_seg(struct mlx4_wqe_local_inval_seg *iseg, u32 rkey)
{
	memset(iseg, 0, sizeof(*iseg));
	iseg->mem_key = cpu_to_be32(rkey);
}

static __always_inline void set_raddr_seg(struct mlx4_wqe_raddr_seg *rseg,
					  u64 remote_addr, u32 rkey)
{
	rseg->raddr    = cpu_to_be64(remote_addr);
	rseg->rkey     = cpu_to_be32(rkey);
	rseg->reserved = 0;
}

static void set_atomic_seg(struct mlx4_wqe_atomic_seg *aseg,
		struct ib_atomic_wr *wr)
{
	if (wr->wr.opcode == IB_WR_ATOMIC_CMP_AND_SWP) {
		aseg->swap_add = cpu_to_be64(wr->swap);
		aseg->compare  = cpu_to_be64(wr->compare_add);
	} else if (wr->wr.opcode == IB_WR_MASKED_ATOMIC_FETCH_AND_ADD) {
		aseg->swap_add = cpu_to_be64(wr->compare_add);
		aseg->compare  = cpu_to_be64(wr->compare_add_mask);
	} else {
		aseg->swap_add = cpu_to_be64(wr->compare_add);
		aseg->compare  = 0;
	}

}

static void set_masked_atomic_seg(struct mlx4_wqe_masked_atomic_seg *aseg,
				  struct ib_atomic_wr *wr)
{
	aseg->swap_add		= cpu_to_be64(wr->swap);
	aseg->swap_add_mask	= cpu_to_be64(wr->swap_mask);
	aseg->compare		= cpu_to_be64(wr->compare_add);
	aseg->compare_mask	= cpu_to_be64(wr->compare_add_mask);
}

static void set_datagram_seg(struct mlx4_wqe_datagram_seg *dseg,
			     struct ib_ud_wr *wr)
{
	memcpy(dseg->av, &to_mah(wr->ah)->av, sizeof (struct mlx4_av));
	dseg->dqpn = cpu_to_be32(wr->remote_qpn);
	dseg->qkey = cpu_to_be32(wr->remote_qkey);
	dseg->vlan = to_mah(wr->ah)->av.eth.vlan;
	memcpy(dseg->mac, to_mah(wr->ah)->av.eth.mac, 6);
}

static void set_tunnel_datagram_seg(struct mlx4_ib_dev *dev,
				    struct mlx4_wqe_datagram_seg *dseg,
				    struct ib_ud_wr *wr,
				    enum mlx4_ib_qp_type qpt)
{
	union mlx4_ext_av *av = &to_mah(wr->ah)->av;
	struct mlx4_av sqp_av = {0};
	int port = *((u8 *) &av->ib.port_pd) & 0x3;

	/* force loopback */
	sqp_av.port_pd = av->ib.port_pd | cpu_to_be32(0x80000000);
	sqp_av.g_slid = av->ib.g_slid & 0x7f; /* no GRH */
	sqp_av.sl_tclass_flowlabel = av->ib.sl_tclass_flowlabel &
			cpu_to_be32(0xf0000000);

	memcpy(dseg->av, &sqp_av, sizeof (struct mlx4_av));
	if (qpt == MLX4_IB_QPT_PROXY_GSI)
		dseg->dqpn = cpu_to_be32(dev->dev->caps.spec_qps[port - 1].qp1_tunnel);
	else
		dseg->dqpn = cpu_to_be32(dev->dev->caps.spec_qps[port - 1].qp0_tunnel);
	/* Use QKEY from the QP context, which is set by master */
	dseg->qkey = cpu_to_be32(IB_QP_SET_QKEY);
}

static void build_tunnel_header(struct ib_ud_wr *wr, void *wqe, unsigned *mlx_seg_len)
{
	struct mlx4_wqe_inline_seg *inl = wqe;
	struct mlx4_ib_tunnel_header hdr;
	struct mlx4_ib_ah *ah = to_mah(wr->ah);
	int spc;
	int i;

	memcpy(&hdr.av, &ah->av, sizeof hdr.av);
	hdr.remote_qpn = cpu_to_be32(wr->remote_qpn);
	hdr.pkey_index = cpu_to_be16(wr->pkey_index);
	hdr.qkey = cpu_to_be32(wr->remote_qkey);
	memcpy(hdr.mac, ah->av.eth.mac, 6);
	hdr.vlan = ah->av.eth.vlan;

	spc = MLX4_INLINE_ALIGN -
		((unsigned long) (inl + 1) & (MLX4_INLINE_ALIGN - 1));
	if (sizeof (hdr) <= spc) {
		memcpy(inl + 1, &hdr, sizeof (hdr));
		wmb();
		inl->byte_count = cpu_to_be32(1 << 31 | sizeof (hdr));
		i = 1;
	} else {
		memcpy(inl + 1, &hdr, spc);
		wmb();
		inl->byte_count = cpu_to_be32(1 << 31 | spc);

		inl = (void *) (inl + 1) + spc;
		memcpy(inl + 1, (void *) &hdr + spc, sizeof (hdr) - spc);
		wmb();
		inl->byte_count = cpu_to_be32(1 << 31 | (sizeof (hdr) - spc));
		i = 2;
	}

	*mlx_seg_len =
		ALIGN(i * sizeof (struct mlx4_wqe_inline_seg) + sizeof (hdr), 16);
}

static void set_mlx_icrc_seg(void *dseg)
{
	u32 *t = dseg;
	struct mlx4_wqe_inline_seg *iseg = dseg;

	t[1] = 0;

	/*
	 * Need a barrier here before writing the byte_count field to
	 * make sure that all the data is visible before the
	 * byte_count field is set.  Otherwise, if the segment begins
	 * a new cacheline, the HCA prefetcher could grab the 64-byte
	 * chunk and get a valid (!= * 0xffffffff) byte count but
	 * stale data, and end up sending the wrong data.
	 */
	wmb();

	iseg->byte_count = cpu_to_be32((1 << 31) | 4);
}

static void set_data_seg(struct mlx4_wqe_data_seg *dseg, struct ib_sge *sg)
{
	dseg->lkey       = cpu_to_be32(sg->lkey);
	dseg->addr       = cpu_to_be64(sg->addr);

	/*
	 * Need a barrier here before writing the byte_count field to
	 * make sure that all the data is visible before the
	 * byte_count field is set.  Otherwise, if the segment begins
	 * a new cacheline, the HCA prefetcher could grab the 64-byte
	 * chunk and get a valid (!= * 0xffffffff) byte count but
	 * stale data, and end up sending the wrong data.
	 */
	wmb();

	dseg->byte_count = cpu_to_be32(sg->length);
}

static void __set_data_seg(struct mlx4_wqe_data_seg *dseg, struct ib_sge *sg)
{
	dseg->byte_count = cpu_to_be32(sg->length);
	dseg->lkey       = cpu_to_be32(sg->lkey);
	dseg->addr       = cpu_to_be64(sg->addr);
}

static int build_lso_seg(struct mlx4_wqe_lso_seg *wqe, struct ib_ud_wr *wr,
			 struct mlx4_ib_qp *qp, unsigned *lso_seg_len,
			 __be32 *lso_hdr_sz, __be32 *blh)
{
	unsigned halign = ALIGN(sizeof *wqe + wr->hlen, 16);

	if (unlikely(halign > MLX4_IB_CACHE_LINE_SIZE))
		*blh = cpu_to_be32(1 << 6);

	if (unlikely(!(qp->flags & MLX4_IB_QP_LSO) &&
		     wr->wr.num_sge > qp->sq.max_gs - (halign >> 4)))
		return -EINVAL;

	memcpy(wqe->header, wr->header, wr->hlen);

	*lso_hdr_sz  = cpu_to_be32(wr->mss << 16 | wr->hlen);
	*lso_seg_len = halign;
	return 0;
}

static __be32 send_ieth(struct ib_send_wr *wr)
{
	switch (wr->opcode) {
	case IB_WR_SEND_WITH_IMM:
	case IB_WR_RDMA_WRITE_WITH_IMM:
		return wr->ex.imm_data;

	case IB_WR_SEND_WITH_INV:
		return cpu_to_be32(wr->ex.invalidate_rkey);

	default:
		return 0;
	}
}

static void add_zero_len_inline(void *wqe)
{
	struct mlx4_wqe_inline_seg *inl = wqe;
	memset(wqe, 0, 16);
	inl->byte_count = cpu_to_be32(1 << 31);
}

int mlx4_ib_post_send(struct ib_qp *ibqp, struct ib_send_wr *wr,
		      struct ib_send_wr **bad_wr)
{
	struct mlx4_ib_qp *qp = to_mqp(ibqp);
	void *wqe;
	struct mlx4_wqe_ctrl_seg *ctrl;
	struct mlx4_wqe_data_seg *dseg;
	unsigned long flags;
	int nreq;
	int err = 0;
	unsigned ind;
	int uninitialized_var(stamp);
	int uninitialized_var(size);
	unsigned uninitialized_var(seglen);
	__be32 dummy;
	__be32 *lso_wqe;
	__be32 uninitialized_var(lso_hdr_sz);
	__be32 blh;
	int i;
	struct mlx4_ib_dev *mdev = to_mdev(ibqp->device);

	if (qp->mlx4_ib_qp_type == MLX4_IB_QPT_GSI) {
		struct mlx4_ib_sqp *sqp = to_msqp(qp);

		if (sqp->roce_v2_gsi) {
			struct mlx4_ib_ah *ah = to_mah(ud_wr(wr)->ah);
			enum ib_gid_type gid_type;
			union ib_gid gid;

			if (!fill_gid_by_hw_index(mdev, sqp->qp.port,
					   ah->av.ib.gid_index,
					   &gid, &gid_type))
				qp = (gid_type == IB_GID_TYPE_ROCE_UDP_ENCAP) ?
						to_mqp(sqp->roce_v2_gsi) : qp;
			else
				pr_err("Failed to get gid at index %d. RoCEv2 will not work properly\n",
				       ah->av.ib.gid_index);
		}
	}

	spin_lock_irqsave(&qp->sq.lock, flags);
	if (mdev->dev->persist->state & MLX4_DEVICE_STATE_INTERNAL_ERROR) {
		err = -EIO;
		*bad_wr = wr;
		nreq = 0;
		goto out;
	}

	ind = qp->sq_next_wqe;

	for (nreq = 0; wr; ++nreq, wr = wr->next) {
		lso_wqe = &dummy;
		blh = 0;

		if (mlx4_wq_overflow(&qp->sq, nreq, qp->ibqp.send_cq)) {
			err = -ENOMEM;
			*bad_wr = wr;
			goto out;
		}

		if (unlikely(wr->num_sge > qp->sq.max_gs)) {
			err = -EINVAL;
			*bad_wr = wr;
			goto out;
		}

		ctrl = wqe = get_send_wqe(qp, ind & (qp->sq.wqe_cnt - 1));
		qp->sq.wrid[(qp->sq.head + nreq) & (qp->sq.wqe_cnt - 1)] = wr->wr_id;

		ctrl->srcrb_flags =
			(wr->send_flags & IB_SEND_SIGNALED ?
			 cpu_to_be32(MLX4_WQE_CTRL_CQ_UPDATE) : 0) |
			(wr->send_flags & IB_SEND_SOLICITED ?
			 cpu_to_be32(MLX4_WQE_CTRL_SOLICITED) : 0) |
			((wr->send_flags & IB_SEND_IP_CSUM) ?
			 cpu_to_be32(MLX4_WQE_CTRL_IP_CSUM |
				     MLX4_WQE_CTRL_TCP_UDP_CSUM) : 0) |
			qp->sq_signal_bits;

		ctrl->imm = send_ieth(wr);

		wqe += sizeof *ctrl;
		size = sizeof *ctrl / 16;

		switch (qp->mlx4_ib_qp_type) {
		case MLX4_IB_QPT_RC:
		case MLX4_IB_QPT_UC:
			switch (wr->opcode) {
			case IB_WR_ATOMIC_CMP_AND_SWP:
			case IB_WR_ATOMIC_FETCH_AND_ADD:
			case IB_WR_MASKED_ATOMIC_FETCH_AND_ADD:
				set_raddr_seg(wqe, atomic_wr(wr)->remote_addr,
					      atomic_wr(wr)->rkey);
				wqe  += sizeof (struct mlx4_wqe_raddr_seg);

				set_atomic_seg(wqe, atomic_wr(wr));
				wqe  += sizeof (struct mlx4_wqe_atomic_seg);

				size += (sizeof (struct mlx4_wqe_raddr_seg) +
					 sizeof (struct mlx4_wqe_atomic_seg)) / 16;

				break;

			case IB_WR_MASKED_ATOMIC_CMP_AND_SWP:
				set_raddr_seg(wqe, atomic_wr(wr)->remote_addr,
					      atomic_wr(wr)->rkey);
				wqe  += sizeof (struct mlx4_wqe_raddr_seg);

				set_masked_atomic_seg(wqe, atomic_wr(wr));
				wqe  += sizeof (struct mlx4_wqe_masked_atomic_seg);

				size += (sizeof (struct mlx4_wqe_raddr_seg) +
					 sizeof (struct mlx4_wqe_masked_atomic_seg)) / 16;

				break;

			case IB_WR_RDMA_READ:
			case IB_WR_RDMA_WRITE:
			case IB_WR_RDMA_WRITE_WITH_IMM:
				set_raddr_seg(wqe, rdma_wr(wr)->remote_addr,
					      rdma_wr(wr)->rkey);
				wqe  += sizeof (struct mlx4_wqe_raddr_seg);
				size += sizeof (struct mlx4_wqe_raddr_seg) / 16;
				break;

			case IB_WR_LOCAL_INV:
				ctrl->srcrb_flags |=
					cpu_to_be32(MLX4_WQE_CTRL_STRONG_ORDER);
				set_local_inv_seg(wqe, wr->ex.invalidate_rkey);
				wqe  += sizeof (struct mlx4_wqe_local_inval_seg);
				size += sizeof (struct mlx4_wqe_local_inval_seg) / 16;
				break;

			case IB_WR_REG_MR:
				ctrl->srcrb_flags |=
					cpu_to_be32(MLX4_WQE_CTRL_STRONG_ORDER);
				set_reg_seg(wqe, reg_wr(wr));
				wqe  += sizeof(struct mlx4_wqe_fmr_seg);
				size += sizeof(struct mlx4_wqe_fmr_seg) / 16;
				break;

			default:
				/* No extra segments required for sends */
				break;
			}
			break;

		case MLX4_IB_QPT_TUN_SMI_OWNER:
			err =  build_sriov_qp0_header(to_msqp(qp), ud_wr(wr),
					ctrl, &seglen);
			if (unlikely(err)) {
				*bad_wr = wr;
				goto out;
			}
			wqe  += seglen;
			size += seglen / 16;
			break;
		case MLX4_IB_QPT_TUN_SMI:
		case MLX4_IB_QPT_TUN_GSI:
			/* this is a UD qp used in MAD responses to slaves. */
			set_datagram_seg(wqe, ud_wr(wr));
			/* set the forced-loopback bit in the data seg av */
			*(__be32 *) wqe |= cpu_to_be32(0x80000000);
			wqe  += sizeof (struct mlx4_wqe_datagram_seg);
			size += sizeof (struct mlx4_wqe_datagram_seg) / 16;
			break;
		case MLX4_IB_QPT_UD:
			set_datagram_seg(wqe, ud_wr(wr));
			wqe  += sizeof (struct mlx4_wqe_datagram_seg);
			size += sizeof (struct mlx4_wqe_datagram_seg) / 16;

			if (wr->opcode == IB_WR_LSO) {
				err = build_lso_seg(wqe, ud_wr(wr), qp, &seglen,
						&lso_hdr_sz, &blh);
				if (unlikely(err)) {
					*bad_wr = wr;
					goto out;
				}
				lso_wqe = (__be32 *) wqe;
				wqe  += seglen;
				size += seglen / 16;
			}
			break;

		case MLX4_IB_QPT_PROXY_SMI_OWNER:
			err = build_sriov_qp0_header(to_msqp(qp), ud_wr(wr),
					ctrl, &seglen);
			if (unlikely(err)) {
				*bad_wr = wr;
				goto out;
			}
			wqe  += seglen;
			size += seglen / 16;
			/* to start tunnel header on a cache-line boundary */
			add_zero_len_inline(wqe);
			wqe += 16;
			size++;
			build_tunnel_header(ud_wr(wr), wqe, &seglen);
			wqe  += seglen;
			size += seglen / 16;
			break;
		case MLX4_IB_QPT_PROXY_SMI:
		case MLX4_IB_QPT_PROXY_GSI:
			/* If we are tunneling special qps, this is a UD qp.
			 * In this case we first add a UD segment targeting
			 * the tunnel qp, and then add a header with address
			 * information */
			set_tunnel_datagram_seg(to_mdev(ibqp->device), wqe,
						ud_wr(wr),
						qp->mlx4_ib_qp_type);
			wqe  += sizeof (struct mlx4_wqe_datagram_seg);
			size += sizeof (struct mlx4_wqe_datagram_seg) / 16;
			build_tunnel_header(ud_wr(wr), wqe, &seglen);
			wqe  += seglen;
			size += seglen / 16;
			break;

		case MLX4_IB_QPT_SMI:
		case MLX4_IB_QPT_GSI:
			err = build_mlx_header(to_msqp(qp), ud_wr(wr), ctrl,
					&seglen);
			if (unlikely(err)) {
				*bad_wr = wr;
				goto out;
			}
			wqe  += seglen;
			size += seglen / 16;
			break;

		default:
			break;
		}

		/*
		 * Write data segments in reverse order, so as to
		 * overwrite cacheline stamp last within each
		 * cacheline.  This avoids issues with WQE
		 * prefetching.
		 */

		dseg = wqe;
		dseg += wr->num_sge - 1;
		size += wr->num_sge * (sizeof (struct mlx4_wqe_data_seg) / 16);

		/* Add one more inline data segment for ICRC for MLX sends */
		if (unlikely(qp->mlx4_ib_qp_type == MLX4_IB_QPT_SMI ||
			     qp->mlx4_ib_qp_type == MLX4_IB_QPT_GSI ||
			     qp->mlx4_ib_qp_type &
			     (MLX4_IB_QPT_PROXY_SMI_OWNER | MLX4_IB_QPT_TUN_SMI_OWNER))) {
			set_mlx_icrc_seg(dseg + 1);
			size += sizeof (struct mlx4_wqe_data_seg) / 16;
		}

		for (i = wr->num_sge - 1; i >= 0; --i, --dseg)
			set_data_seg(dseg, wr->sg_list + i);

		/*
		 * Possibly overwrite stamping in cacheline with LSO
		 * segment only after making sure all data segments
		 * are written.
		 */
		wmb();
		*lso_wqe = lso_hdr_sz;

		ctrl->qpn_vlan.fence_size = (wr->send_flags & IB_SEND_FENCE ?
					     MLX4_WQE_CTRL_FENCE : 0) | size;

		/*
		 * Make sure descriptor is fully written before
		 * setting ownership bit (because HW can start
		 * executing as soon as we do).
		 */
		wmb();

		if (wr->opcode < 0 || wr->opcode >= ARRAY_SIZE(mlx4_ib_opcode)) {
			*bad_wr = wr;
			err = -EINVAL;
			goto out;
		}

		ctrl->owner_opcode = mlx4_ib_opcode[wr->opcode] |
			(ind & qp->sq.wqe_cnt ? cpu_to_be32(1 << 31) : 0) | blh;

		stamp = ind + qp->sq_spare_wqes;
		ind += DIV_ROUND_UP(size * 16, 1U << qp->sq.wqe_shift);

		/*
		 * We can improve latency by not stamping the last
		 * send queue WQE until after ringing the doorbell, so
		 * only stamp here if there are still more WQEs to post.
		 *
		 * Same optimization applies to padding with NOP wqe
		 * in case of WQE shrinking (used to prevent wrap-around
		 * in the middle of WR).
		 */
		if (wr->next) {
			stamp_send_wqe(qp, stamp, size * 16);
			ind = pad_wraparound(qp, ind);
		}
	}

out:
	if (likely(nreq)) {
		qp->sq.head += nreq;

		/*
		 * Make sure that descriptors are written before
		 * doorbell record.
		 */
		wmb();

		writel_relaxed(qp->doorbell_qpn,
			to_mdev(ibqp->device)->uar_map + MLX4_SEND_DOORBELL);

		/*
		 * Make sure doorbells don't leak out of SQ spinlock
		 * and reach the HCA out of order.
		 */
		mmiowb();

		stamp_send_wqe(qp, stamp, size * 16);

		ind = pad_wraparound(qp, ind);
		qp->sq_next_wqe = ind;
	}

	spin_unlock_irqrestore(&qp->sq.lock, flags);

	return err;
}

int mlx4_ib_post_recv(struct ib_qp *ibqp, struct ib_recv_wr *wr,
		      struct ib_recv_wr **bad_wr)
{
	struct mlx4_ib_qp *qp = to_mqp(ibqp);
	struct mlx4_wqe_data_seg *scat;
	unsigned long flags;
	int err = 0;
	int nreq;
	int ind;
	int max_gs;
	int i;
	struct mlx4_ib_dev *mdev = to_mdev(ibqp->device);

	max_gs = qp->rq.max_gs;
	spin_lock_irqsave(&qp->rq.lock, flags);

	if (mdev->dev->persist->state & MLX4_DEVICE_STATE_INTERNAL_ERROR) {
		err = -EIO;
		*bad_wr = wr;
		nreq = 0;
		goto out;
	}

	ind = qp->rq.head & (qp->rq.wqe_cnt - 1);

	for (nreq = 0; wr; ++nreq, wr = wr->next) {
		if (mlx4_wq_overflow(&qp->rq, nreq, qp->ibqp.recv_cq)) {
			err = -ENOMEM;
			*bad_wr = wr;
			goto out;
		}

		if (unlikely(wr->num_sge > qp->rq.max_gs)) {
			err = -EINVAL;
			*bad_wr = wr;
			goto out;
		}

		scat = get_recv_wqe(qp, ind);

		if (qp->mlx4_ib_qp_type & (MLX4_IB_QPT_PROXY_SMI_OWNER |
		    MLX4_IB_QPT_PROXY_SMI | MLX4_IB_QPT_PROXY_GSI)) {
			ib_dma_sync_single_for_device(ibqp->device,
						      qp->sqp_proxy_rcv[ind].map,
						      sizeof (struct mlx4_ib_proxy_sqp_hdr),
						      DMA_FROM_DEVICE);
			scat->byte_count =
				cpu_to_be32(sizeof (struct mlx4_ib_proxy_sqp_hdr));
			/* use dma lkey from upper layer entry */
			scat->lkey = cpu_to_be32(wr->sg_list->lkey);
			scat->addr = cpu_to_be64(qp->sqp_proxy_rcv[ind].map);
			scat++;
			max_gs--;
		}

		for (i = 0; i < wr->num_sge; ++i)
			__set_data_seg(scat + i, wr->sg_list + i);

		if (i < max_gs) {
			scat[i].byte_count = 0;
			scat[i].lkey       = cpu_to_be32(MLX4_INVALID_LKEY);
			scat[i].addr       = 0;
		}

		qp->rq.wrid[ind] = wr->wr_id;

		ind = (ind + 1) & (qp->rq.wqe_cnt - 1);
	}

out:
	if (likely(nreq)) {
		qp->rq.head += nreq;

		/*
		 * Make sure that descriptors are written before
		 * doorbell record.
		 */
		wmb();

		*qp->db.db = cpu_to_be32(qp->rq.head & 0xffff);
	}

	spin_unlock_irqrestore(&qp->rq.lock, flags);

	return err;
}

static inline enum ib_qp_state to_ib_qp_state(enum mlx4_qp_state mlx4_state)
{
	switch (mlx4_state) {
	case MLX4_QP_STATE_RST:      return IB_QPS_RESET;
	case MLX4_QP_STATE_INIT:     return IB_QPS_INIT;
	case MLX4_QP_STATE_RTR:      return IB_QPS_RTR;
	case MLX4_QP_STATE_RTS:      return IB_QPS_RTS;
	case MLX4_QP_STATE_SQ_DRAINING:
	case MLX4_QP_STATE_SQD:      return IB_QPS_SQD;
	case MLX4_QP_STATE_SQER:     return IB_QPS_SQE;
	case MLX4_QP_STATE_ERR:      return IB_QPS_ERR;
	default:		     return -1;
	}
}

static inline enum ib_mig_state to_ib_mig_state(int mlx4_mig_state)
{
	switch (mlx4_mig_state) {
	case MLX4_QP_PM_ARMED:		return IB_MIG_ARMED;
	case MLX4_QP_PM_REARM:		return IB_MIG_REARM;
	case MLX4_QP_PM_MIGRATED:	return IB_MIG_MIGRATED;
	default: return -1;
	}
}

static int to_ib_qp_access_flags(int mlx4_flags)
{
	int ib_flags = 0;

	if (mlx4_flags & MLX4_QP_BIT_RRE)
		ib_flags |= IB_ACCESS_REMOTE_READ;
	if (mlx4_flags & MLX4_QP_BIT_RWE)
		ib_flags |= IB_ACCESS_REMOTE_WRITE;
	if (mlx4_flags & MLX4_QP_BIT_RAE)
		ib_flags |= IB_ACCESS_REMOTE_ATOMIC;

	return ib_flags;
}

static void to_rdma_ah_attr(struct mlx4_ib_dev *ibdev,
			    struct rdma_ah_attr *ah_attr,
			    struct mlx4_qp_path *path)
{
	struct mlx4_dev *dev = ibdev->dev;
	u8 port_num = path->sched_queue & 0x40 ? 2 : 1;

	memset(ah_attr, 0, sizeof(*ah_attr));
	ah_attr->type = rdma_ah_find_type(&ibdev->ib_dev, port_num);
	if (port_num == 0 || port_num > dev->caps.num_ports)
		return;

	if (ah_attr->type == RDMA_AH_ATTR_TYPE_ROCE)
		rdma_ah_set_sl(ah_attr, ((path->sched_queue >> 3) & 0x7) |
			       ((path->sched_queue & 4) << 1));
	else
		rdma_ah_set_sl(ah_attr, (path->sched_queue >> 2) & 0xf);
	rdma_ah_set_port_num(ah_attr, port_num);

	rdma_ah_set_dlid(ah_attr, be16_to_cpu(path->rlid));
	rdma_ah_set_path_bits(ah_attr, path->grh_mylmc & 0x7f);
	rdma_ah_set_static_rate(ah_attr,
				path->static_rate ? path->static_rate - 5 : 0);
	if (path->grh_mylmc & (1 << 7)) {
		rdma_ah_set_grh(ah_attr, NULL,
				be32_to_cpu(path->tclass_flowlabel) & 0xfffff,
				path->mgid_index,
				path->hop_limit,
				(be32_to_cpu(path->tclass_flowlabel)
				 >> 20) & 0xff);
		rdma_ah_set_dgid_raw(ah_attr, path->rgid);
	}
}

int mlx4_ib_query_qp(struct ib_qp *ibqp, struct ib_qp_attr *qp_attr, int qp_attr_mask,
		     struct ib_qp_init_attr *qp_init_attr)
{
	struct mlx4_ib_dev *dev = to_mdev(ibqp->device);
	struct mlx4_ib_qp *qp = to_mqp(ibqp);
	struct mlx4_qp_context context;
	int mlx4_state;
	int err = 0;

	if (ibqp->rwq_ind_tbl)
		return -EOPNOTSUPP;

	mutex_lock(&qp->mutex);

	if (qp->state == IB_QPS_RESET) {
		qp_attr->qp_state = IB_QPS_RESET;
		goto done;
	}

	err = mlx4_qp_query(dev->dev, &qp->mqp, &context);
	if (err) {
		err = -EINVAL;
		goto out;
	}

	mlx4_state = be32_to_cpu(context.flags) >> 28;

	qp->state		     = to_ib_qp_state(mlx4_state);
	qp_attr->qp_state	     = qp->state;
	qp_attr->path_mtu	     = context.mtu_msgmax >> 5;
	qp_attr->path_mig_state	     =
		to_ib_mig_state((be32_to_cpu(context.flags) >> 11) & 0x3);
	qp_attr->qkey		     = be32_to_cpu(context.qkey);
	qp_attr->rq_psn		     = be32_to_cpu(context.rnr_nextrecvpsn) & 0xffffff;
	qp_attr->sq_psn		     = be32_to_cpu(context.next_send_psn) & 0xffffff;
	qp_attr->dest_qp_num	     = be32_to_cpu(context.remote_qpn) & 0xffffff;
	qp_attr->qp_access_flags     =
		to_ib_qp_access_flags(be32_to_cpu(context.params2));

	if (qp->ibqp.qp_type == IB_QPT_RC || qp->ibqp.qp_type == IB_QPT_UC) {
		to_rdma_ah_attr(dev, &qp_attr->ah_attr, &context.pri_path);
		to_rdma_ah_attr(dev, &qp_attr->alt_ah_attr, &context.alt_path);
		qp_attr->alt_pkey_index = context.alt_path.pkey_index & 0x7f;
		qp_attr->alt_port_num	=
			rdma_ah_get_port_num(&qp_attr->alt_ah_attr);
	}

	qp_attr->pkey_index = context.pri_path.pkey_index & 0x7f;
	if (qp_attr->qp_state == IB_QPS_INIT)
		qp_attr->port_num = qp->port;
	else
		qp_attr->port_num = context.pri_path.sched_queue & 0x40 ? 2 : 1;

	/* qp_attr->en_sqd_async_notify is only applicable in modify qp */
	qp_attr->sq_draining = mlx4_state == MLX4_QP_STATE_SQ_DRAINING;

	qp_attr->max_rd_atomic = 1 << ((be32_to_cpu(context.params1) >> 21) & 0x7);

	qp_attr->max_dest_rd_atomic =
		1 << ((be32_to_cpu(context.params2) >> 21) & 0x7);
	qp_attr->min_rnr_timer	    =
		(be32_to_cpu(context.rnr_nextrecvpsn) >> 24) & 0x1f;
	qp_attr->timeout	    = context.pri_path.ackto >> 3;
	qp_attr->retry_cnt	    = (be32_to_cpu(context.params1) >> 16) & 0x7;
	qp_attr->rnr_retry	    = (be32_to_cpu(context.params1) >> 13) & 0x7;
	qp_attr->alt_timeout	    = context.alt_path.ackto >> 3;

done:
	qp_attr->cur_qp_state	     = qp_attr->qp_state;
	qp_attr->cap.max_recv_wr     = qp->rq.wqe_cnt;
	qp_attr->cap.max_recv_sge    = qp->rq.max_gs;

	if (!ibqp->uobject) {
		qp_attr->cap.max_send_wr  = qp->sq.wqe_cnt;
		qp_attr->cap.max_send_sge = qp->sq.max_gs;
	} else {
		qp_attr->cap.max_send_wr  = 0;
		qp_attr->cap.max_send_sge = 0;
	}

	/*
	 * We don't support inline sends for kernel QPs (yet), and we
	 * don't know what userspace's value should be.
	 */
	qp_attr->cap.max_inline_data = 0;

	qp_init_attr->cap	     = qp_attr->cap;

	qp_init_attr->create_flags = 0;
	if (qp->flags & MLX4_IB_QP_BLOCK_MULTICAST_LOOPBACK)
		qp_init_attr->create_flags |= IB_QP_CREATE_BLOCK_MULTICAST_LOOPBACK;

	if (qp->flags & MLX4_IB_QP_LSO)
		qp_init_attr->create_flags |= IB_QP_CREATE_IPOIB_UD_LSO;

	if (qp->flags & MLX4_IB_QP_NETIF)
		qp_init_attr->create_flags |= IB_QP_CREATE_NETIF_QP;

	qp_init_attr->sq_sig_type =
		qp->sq_signal_bits == cpu_to_be32(MLX4_WQE_CTRL_CQ_UPDATE) ?
		IB_SIGNAL_ALL_WR : IB_SIGNAL_REQ_WR;

out:
	mutex_unlock(&qp->mutex);
	return err;
}

struct ib_wq *mlx4_ib_create_wq(struct ib_pd *pd,
				struct ib_wq_init_attr *init_attr,
				struct ib_udata *udata)
{
	struct mlx4_ib_dev *dev;
	struct ib_qp_init_attr ib_qp_init_attr;
	struct mlx4_ib_qp *qp;
	struct mlx4_ib_create_wq ucmd;
	int err, required_cmd_sz;

	if (!(udata && pd->uobject))
		return ERR_PTR(-EINVAL);

	required_cmd_sz = offsetof(typeof(ucmd), comp_mask) +
			  sizeof(ucmd.comp_mask);
	if (udata->inlen < required_cmd_sz) {
		pr_debug("invalid inlen\n");
		return ERR_PTR(-EINVAL);
	}

	if (udata->inlen > sizeof(ucmd) &&
	    !ib_is_udata_cleared(udata, sizeof(ucmd),
				 udata->inlen - sizeof(ucmd))) {
		pr_debug("inlen is not supported\n");
		return ERR_PTR(-EOPNOTSUPP);
	}

	if (udata->outlen)
		return ERR_PTR(-EOPNOTSUPP);

	dev = to_mdev(pd->device);

	if (init_attr->wq_type != IB_WQT_RQ) {
		pr_debug("unsupported wq type %d\n", init_attr->wq_type);
		return ERR_PTR(-EOPNOTSUPP);
	}

	if (init_attr->create_flags & ~IB_WQ_FLAGS_SCATTER_FCS) {
		pr_debug("unsupported create_flags %u\n",
			 init_attr->create_flags);
		return ERR_PTR(-EOPNOTSUPP);
	}

	qp = kzalloc(sizeof(*qp), GFP_KERNEL);
	if (!qp)
		return ERR_PTR(-ENOMEM);

	qp->pri.vid = 0xFFFF;
	qp->alt.vid = 0xFFFF;

	memset(&ib_qp_init_attr, 0, sizeof(ib_qp_init_attr));
	ib_qp_init_attr.qp_context = init_attr->wq_context;
	ib_qp_init_attr.qp_type = IB_QPT_RAW_PACKET;
	ib_qp_init_attr.cap.max_recv_wr = init_attr->max_wr;
	ib_qp_init_attr.cap.max_recv_sge = init_attr->max_sge;
	ib_qp_init_attr.recv_cq = init_attr->cq;
	ib_qp_init_attr.send_cq = ib_qp_init_attr.recv_cq; /* Dummy CQ */

	if (init_attr->create_flags & IB_WQ_FLAGS_SCATTER_FCS)
		ib_qp_init_attr.create_flags |= IB_QP_CREATE_SCATTER_FCS;

	err = create_qp_common(dev, pd, MLX4_IB_RWQ_SRC, &ib_qp_init_attr,
			       udata, 0, &qp);
	if (err) {
		kfree(qp);
		return ERR_PTR(err);
	}

	qp->ibwq.event_handler = init_attr->event_handler;
	qp->ibwq.wq_num = qp->mqp.qpn;
	qp->ibwq.state = IB_WQS_RESET;

	return &qp->ibwq;
}

static int ib_wq2qp_state(enum ib_wq_state state)
{
	switch (state) {
	case IB_WQS_RESET:
		return IB_QPS_RESET;
	case IB_WQS_RDY:
		return IB_QPS_RTR;
	default:
		return IB_QPS_ERR;
	}
}

static int _mlx4_ib_modify_wq(struct ib_wq *ibwq, enum ib_wq_state new_state)
{
	struct mlx4_ib_qp *qp = to_mqp((struct ib_qp *)ibwq);
	enum ib_qp_state qp_cur_state;
	enum ib_qp_state qp_new_state;
	int attr_mask;
	int err;

	/* ib_qp.state represents the WQ HW state while ib_wq.state represents
	 * the WQ logic state.
	 */
	qp_cur_state = qp->state;
	qp_new_state = ib_wq2qp_state(new_state);

	if (ib_wq2qp_state(new_state) == qp_cur_state)
		return 0;

	if (new_state == IB_WQS_RDY) {
		struct ib_qp_attr attr = {};

		attr.port_num = qp->port;
		attr_mask = IB_QP_PORT;

		err = __mlx4_ib_modify_qp(ibwq, MLX4_IB_RWQ_SRC, &attr,
					  attr_mask, IB_QPS_RESET, IB_QPS_INIT);
		if (err) {
			pr_debug("WQN=0x%06x failed to apply RST->INIT on the HW QP\n",
				 ibwq->wq_num);
			return err;
		}

		qp_cur_state = IB_QPS_INIT;
	}

	attr_mask = 0;
	err = __mlx4_ib_modify_qp(ibwq, MLX4_IB_RWQ_SRC, NULL, attr_mask,
				  qp_cur_state,  qp_new_state);

	if (err && (qp_cur_state == IB_QPS_INIT)) {
		qp_new_state = IB_QPS_RESET;
		if (__mlx4_ib_modify_qp(ibwq, MLX4_IB_RWQ_SRC, NULL,
					attr_mask, IB_QPS_INIT, IB_QPS_RESET)) {
			pr_warn("WQN=0x%06x failed with reverting HW's resources failure\n",
				ibwq->wq_num);
			qp_new_state = IB_QPS_INIT;
		}
	}

	qp->state = qp_new_state;

	return err;
}

int mlx4_ib_modify_wq(struct ib_wq *ibwq, struct ib_wq_attr *wq_attr,
		      u32 wq_attr_mask, struct ib_udata *udata)
{
	struct mlx4_ib_qp *qp = to_mqp((struct ib_qp *)ibwq);
	struct mlx4_ib_modify_wq ucmd = {};
	size_t required_cmd_sz;
	enum ib_wq_state cur_state, new_state;
	int err = 0;

	required_cmd_sz = offsetof(typeof(ucmd), reserved) +
				   sizeof(ucmd.reserved);
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

	if (wq_attr_mask & IB_WQ_FLAGS)
		return -EOPNOTSUPP;

	cur_state = wq_attr_mask & IB_WQ_CUR_STATE ? wq_attr->curr_wq_state :
						     ibwq->state;
	new_state = wq_attr_mask & IB_WQ_STATE ? wq_attr->wq_state : cur_state;

	if (cur_state  < IB_WQS_RESET || cur_state  > IB_WQS_ERR ||
	    new_state < IB_WQS_RESET || new_state > IB_WQS_ERR)
		return -EINVAL;

	if ((new_state == IB_WQS_RDY) && (cur_state == IB_WQS_ERR))
		return -EINVAL;

	if ((new_state == IB_WQS_ERR) && (cur_state == IB_WQS_RESET))
		return -EINVAL;

	/* Need to protect against the parent RSS which also may modify WQ
	 * state.
	 */
	mutex_lock(&qp->mutex);

	/* Can update HW state only if a RSS QP has already associated to this
	 * WQ, so we can apply its port on the WQ.
	 */
	if (qp->rss_usecnt)
		err = _mlx4_ib_modify_wq(ibwq, new_state);

	if (!err)
		ibwq->state = new_state;

	mutex_unlock(&qp->mutex);

	return err;
}

int mlx4_ib_destroy_wq(struct ib_wq *ibwq)
{
	struct mlx4_ib_dev *dev = to_mdev(ibwq->device);
	struct mlx4_ib_qp *qp = to_mqp((struct ib_qp *)ibwq);

	if (qp->counter_index)
		mlx4_ib_free_qp_counter(dev, qp);

	destroy_qp_common(dev, qp, MLX4_IB_RWQ_SRC, 1);

	kfree(qp);

	return 0;
}

struct ib_rwq_ind_table
*mlx4_ib_create_rwq_ind_table(struct ib_device *device,
			      struct ib_rwq_ind_table_init_attr *init_attr,
			      struct ib_udata *udata)
{
	struct ib_rwq_ind_table *rwq_ind_table;
	struct mlx4_ib_create_rwq_ind_tbl_resp resp = {};
	unsigned int ind_tbl_size = 1 << init_attr->log_ind_tbl_size;
	unsigned int base_wqn;
	size_t min_resp_len;
	int i;
	int err;

	if (udata->inlen > 0 &&
	    !ib_is_udata_cleared(udata, 0,
				 udata->inlen))
		return ERR_PTR(-EOPNOTSUPP);

	min_resp_len = offsetof(typeof(resp), reserved) + sizeof(resp.reserved);
	if (udata->outlen && udata->outlen < min_resp_len)
		return ERR_PTR(-EINVAL);

	if (ind_tbl_size >
	    device->attrs.rss_caps.max_rwq_indirection_table_size) {
		pr_debug("log_ind_tbl_size = %d is bigger than supported = %d\n",
			 ind_tbl_size,
			 device->attrs.rss_caps.max_rwq_indirection_table_size);
		return ERR_PTR(-EINVAL);
	}

	base_wqn = init_attr->ind_tbl[0]->wq_num;

	if (base_wqn % ind_tbl_size) {
		pr_debug("WQN=0x%x isn't aligned with indirection table size\n",
			 base_wqn);
		return ERR_PTR(-EINVAL);
	}

	for (i = 1; i < ind_tbl_size; i++) {
		if (++base_wqn != init_attr->ind_tbl[i]->wq_num) {
			pr_debug("indirection table's WQNs aren't consecutive\n");
			return ERR_PTR(-EINVAL);
		}
	}

	rwq_ind_table = kzalloc(sizeof(*rwq_ind_table), GFP_KERNEL);
	if (!rwq_ind_table)
		return ERR_PTR(-ENOMEM);

	if (udata->outlen) {
		resp.response_length = offsetof(typeof(resp), response_length) +
					sizeof(resp.response_length);
		err = ib_copy_to_udata(udata, &resp, resp.response_length);
		if (err)
			goto err;
	}

	return rwq_ind_table;

err:
	kfree(rwq_ind_table);
	return ERR_PTR(err);
}

int mlx4_ib_destroy_rwq_ind_table(struct ib_rwq_ind_table *ib_rwq_ind_tbl)
{
	kfree(ib_rwq_ind_tbl);
	return 0;
}
