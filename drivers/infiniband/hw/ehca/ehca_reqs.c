/*
 *  IBM eServer eHCA Infiniband device driver for Linux on POWER
 *
 *  post_send/recv, poll_cq, req_notify
 *
 *  Authors: Hoang-Nam Nguyen <hnguyen@de.ibm.com>
 *           Waleri Fomin <fomin@de.ibm.com>
 *           Joachim Fenkes <fenkes@de.ibm.com>
 *           Reinhard Ernst <rernst@de.ibm.com>
 *
 *  Copyright (c) 2005 IBM Corporation
 *
 *  All rights reserved.
 *
 *  This source code is distributed under a dual license of GPL v2.0 and OpenIB
 *  BSD.
 *
 * OpenIB BSD License
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials
 * provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */


#include <asm/system.h>
#include "ehca_classes.h"
#include "ehca_tools.h"
#include "ehca_qes.h"
#include "ehca_iverbs.h"
#include "hcp_if.h"
#include "hipz_fns.h"

/* in RC traffic, insert an empty RDMA READ every this many packets */
#define ACK_CIRC_THRESHOLD 2000000

static u64 replace_wr_id(u64 wr_id, u16 idx)
{
	u64 ret;

	ret = wr_id & ~QMAP_IDX_MASK;
	ret |= idx & QMAP_IDX_MASK;

	return ret;
}

static u16 get_app_wr_id(u64 wr_id)
{
	return wr_id & QMAP_IDX_MASK;
}

static inline int ehca_write_rwqe(struct ipz_queue *ipz_rqueue,
				  struct ehca_wqe *wqe_p,
				  struct ib_recv_wr *recv_wr,
				  u32 rq_map_idx)
{
	u8 cnt_ds;
	if (unlikely((recv_wr->num_sge < 0) ||
		     (recv_wr->num_sge > ipz_rqueue->act_nr_of_sg))) {
		ehca_gen_err("Invalid number of WQE SGE. "
			 "num_sqe=%x max_nr_of_sg=%x",
			 recv_wr->num_sge, ipz_rqueue->act_nr_of_sg);
		return -EINVAL; /* invalid SG list length */
	}

	/* clear wqe header until sglist */
	memset(wqe_p, 0, offsetof(struct ehca_wqe, u.ud_av.sg_list));

	wqe_p->work_request_id = replace_wr_id(recv_wr->wr_id, rq_map_idx);
	wqe_p->nr_of_data_seg = recv_wr->num_sge;

	for (cnt_ds = 0; cnt_ds < recv_wr->num_sge; cnt_ds++) {
		wqe_p->u.all_rcv.sg_list[cnt_ds].vaddr =
			recv_wr->sg_list[cnt_ds].addr;
		wqe_p->u.all_rcv.sg_list[cnt_ds].lkey =
			recv_wr->sg_list[cnt_ds].lkey;
		wqe_p->u.all_rcv.sg_list[cnt_ds].length =
			recv_wr->sg_list[cnt_ds].length;
	}

	if (ehca_debug_level >= 3) {
		ehca_gen_dbg("RECEIVE WQE written into ipz_rqueue=%p",
			     ipz_rqueue);
		ehca_dmp(wqe_p, 16*(6 + wqe_p->nr_of_data_seg), "recv wqe");
	}

	return 0;
}

#if defined(DEBUG_GSI_SEND_WR)

/* need ib_mad struct */
#include <rdma/ib_mad.h>

static void trace_send_wr_ud(const struct ib_send_wr *send_wr)
{
	int idx;
	int j;
	while (send_wr) {
		struct ib_mad_hdr *mad_hdr = send_wr->wr.ud.mad_hdr;
		struct ib_sge *sge = send_wr->sg_list;
		ehca_gen_dbg("send_wr#%x wr_id=%lx num_sge=%x "
			     "send_flags=%x opcode=%x", idx, send_wr->wr_id,
			     send_wr->num_sge, send_wr->send_flags,
			     send_wr->opcode);
		if (mad_hdr) {
			ehca_gen_dbg("send_wr#%x mad_hdr base_version=%x "
				     "mgmt_class=%x class_version=%x method=%x "
				     "status=%x class_specific=%x tid=%lx "
				     "attr_id=%x resv=%x attr_mod=%x",
				     idx, mad_hdr->base_version,
				     mad_hdr->mgmt_class,
				     mad_hdr->class_version, mad_hdr->method,
				     mad_hdr->status, mad_hdr->class_specific,
				     mad_hdr->tid, mad_hdr->attr_id,
				     mad_hdr->resv,
				     mad_hdr->attr_mod);
		}
		for (j = 0; j < send_wr->num_sge; j++) {
			u8 *data = (u8 *)abs_to_virt(sge->addr);
			ehca_gen_dbg("send_wr#%x sge#%x addr=%p length=%x "
				     "lkey=%x",
				     idx, j, data, sge->length, sge->lkey);
			/* assume length is n*16 */
			ehca_dmp(data, sge->length, "send_wr#%x sge#%x",
				 idx, j);
			sge++;
		} /* eof for j */
		idx++;
		send_wr = send_wr->next;
	} /* eof while send_wr */
}

#endif /* DEBUG_GSI_SEND_WR */

static inline int ehca_write_swqe(struct ehca_qp *qp,
				  struct ehca_wqe *wqe_p,
				  const struct ib_send_wr *send_wr,
				  u32 sq_map_idx,
				  int hidden)
{
	u32 idx;
	u64 dma_length;
	struct ehca_av *my_av;
	u32 remote_qkey = send_wr->wr.ud.remote_qkey;
	struct ehca_qmap_entry *qmap_entry = &qp->sq_map.map[sq_map_idx];

	if (unlikely((send_wr->num_sge < 0) ||
		     (send_wr->num_sge > qp->ipz_squeue.act_nr_of_sg))) {
		ehca_gen_err("Invalid number of WQE SGE. "
			 "num_sqe=%x max_nr_of_sg=%x",
			 send_wr->num_sge, qp->ipz_squeue.act_nr_of_sg);
		return -EINVAL; /* invalid SG list length */
	}

	/* clear wqe header until sglist */
	memset(wqe_p, 0, offsetof(struct ehca_wqe, u.ud_av.sg_list));

	wqe_p->work_request_id = replace_wr_id(send_wr->wr_id, sq_map_idx);

	qmap_entry->app_wr_id = get_app_wr_id(send_wr->wr_id);
	qmap_entry->reported = 0;
	qmap_entry->cqe_req = 0;

	switch (send_wr->opcode) {
	case IB_WR_SEND:
	case IB_WR_SEND_WITH_IMM:
		wqe_p->optype = WQE_OPTYPE_SEND;
		break;
	case IB_WR_RDMA_WRITE:
	case IB_WR_RDMA_WRITE_WITH_IMM:
		wqe_p->optype = WQE_OPTYPE_RDMAWRITE;
		break;
	case IB_WR_RDMA_READ:
		wqe_p->optype = WQE_OPTYPE_RDMAREAD;
		break;
	default:
		ehca_gen_err("Invalid opcode=%x", send_wr->opcode);
		return -EINVAL; /* invalid opcode */
	}

	wqe_p->wqef = (send_wr->opcode) & WQEF_HIGH_NIBBLE;

	wqe_p->wr_flag = 0;

	if ((send_wr->send_flags & IB_SEND_SIGNALED ||
	    qp->init_attr.sq_sig_type == IB_SIGNAL_ALL_WR)
	    && !hidden) {
		wqe_p->wr_flag |= WQE_WRFLAG_REQ_SIGNAL_COM;
		qmap_entry->cqe_req = 1;
	}

	if (send_wr->opcode == IB_WR_SEND_WITH_IMM ||
	    send_wr->opcode == IB_WR_RDMA_WRITE_WITH_IMM) {
		/* this might not work as long as HW does not support it */
		wqe_p->immediate_data = be32_to_cpu(send_wr->ex.imm_data);
		wqe_p->wr_flag |= WQE_WRFLAG_IMM_DATA_PRESENT;
	}

	wqe_p->nr_of_data_seg = send_wr->num_sge;

	switch (qp->qp_type) {
	case IB_QPT_SMI:
	case IB_QPT_GSI:
		/* no break is intential here */
	case IB_QPT_UD:
		/* IB 1.2 spec C10-15 compliance */
		if (send_wr->wr.ud.remote_qkey & 0x80000000)
			remote_qkey = qp->qkey;

		wqe_p->destination_qp_number = send_wr->wr.ud.remote_qpn << 8;
		wqe_p->local_ee_context_qkey = remote_qkey;
		if (unlikely(!send_wr->wr.ud.ah)) {
			ehca_gen_err("wr.ud.ah is NULL. qp=%p", qp);
			return -EINVAL;
		}
		if (unlikely(send_wr->wr.ud.remote_qpn == 0)) {
			ehca_gen_err("dest QP# is 0. qp=%x", qp->real_qp_num);
			return -EINVAL;
		}
		my_av = container_of(send_wr->wr.ud.ah, struct ehca_av, ib_ah);
		wqe_p->u.ud_av.ud_av = my_av->av;

		/*
		 * omitted check of IB_SEND_INLINE
		 * since HW does not support it
		 */
		for (idx = 0; idx < send_wr->num_sge; idx++) {
			wqe_p->u.ud_av.sg_list[idx].vaddr =
				send_wr->sg_list[idx].addr;
			wqe_p->u.ud_av.sg_list[idx].lkey =
				send_wr->sg_list[idx].lkey;
			wqe_p->u.ud_av.sg_list[idx].length =
				send_wr->sg_list[idx].length;
		} /* eof for idx */
		if (qp->qp_type == IB_QPT_SMI ||
		    qp->qp_type == IB_QPT_GSI)
			wqe_p->u.ud_av.ud_av.pmtu = 1;
		if (qp->qp_type == IB_QPT_GSI) {
			wqe_p->pkeyi = send_wr->wr.ud.pkey_index;
#ifdef DEBUG_GSI_SEND_WR
			trace_send_wr_ud(send_wr);
#endif /* DEBUG_GSI_SEND_WR */
		}
		break;

	case IB_QPT_UC:
		if (send_wr->send_flags & IB_SEND_FENCE)
			wqe_p->wr_flag |= WQE_WRFLAG_FENCE;
		/* no break is intentional here */
	case IB_QPT_RC:
		/* TODO: atomic not implemented */
		wqe_p->u.nud.remote_virtual_address =
			send_wr->wr.rdma.remote_addr;
		wqe_p->u.nud.rkey = send_wr->wr.rdma.rkey;

		/*
		 * omitted checking of IB_SEND_INLINE
		 * since HW does not support it
		 */
		dma_length = 0;
		for (idx = 0; idx < send_wr->num_sge; idx++) {
			wqe_p->u.nud.sg_list[idx].vaddr =
				send_wr->sg_list[idx].addr;
			wqe_p->u.nud.sg_list[idx].lkey =
				send_wr->sg_list[idx].lkey;
			wqe_p->u.nud.sg_list[idx].length =
				send_wr->sg_list[idx].length;
			dma_length += send_wr->sg_list[idx].length;
		} /* eof idx */
		wqe_p->u.nud.atomic_1st_op_dma_len = dma_length;

		/* unsolicited ack circumvention */
		if (send_wr->opcode == IB_WR_RDMA_READ) {
			/* on RDMA read, switch on and reset counters */
			qp->message_count = qp->packet_count = 0;
			qp->unsol_ack_circ = 1;
		} else
			/* else estimate #packets */
			qp->packet_count += (dma_length >> qp->mtu_shift) + 1;

		break;

	default:
		ehca_gen_err("Invalid qptype=%x", qp->qp_type);
		return -EINVAL;
	}

	if (ehca_debug_level >= 3) {
		ehca_gen_dbg("SEND WQE written into queue qp=%p ", qp);
		ehca_dmp( wqe_p, 16*(6 + wqe_p->nr_of_data_seg), "send wqe");
	}
	return 0;
}

/* map_ib_wc_status converts raw cqe_status to ib_wc_status */
static inline void map_ib_wc_status(u32 cqe_status,
				    enum ib_wc_status *wc_status)
{
	if (unlikely(cqe_status & WC_STATUS_ERROR_BIT)) {
		switch (cqe_status & 0x3F) {
		case 0x01:
		case 0x21:
			*wc_status = IB_WC_LOC_LEN_ERR;
			break;
		case 0x02:
		case 0x22:
			*wc_status = IB_WC_LOC_QP_OP_ERR;
			break;
		case 0x03:
		case 0x23:
			*wc_status = IB_WC_LOC_EEC_OP_ERR;
			break;
		case 0x04:
		case 0x24:
			*wc_status = IB_WC_LOC_PROT_ERR;
			break;
		case 0x05:
		case 0x25:
			*wc_status = IB_WC_WR_FLUSH_ERR;
			break;
		case 0x06:
			*wc_status = IB_WC_MW_BIND_ERR;
			break;
		case 0x07: /* remote error - look into bits 20:24 */
			switch ((cqe_status
				 & WC_STATUS_REMOTE_ERROR_FLAGS) >> 11) {
			case 0x0:
				/*
				 * PSN Sequence Error!
				 * couldn't find a matching status!
				 */
				*wc_status = IB_WC_GENERAL_ERR;
				break;
			case 0x1:
				*wc_status = IB_WC_REM_INV_REQ_ERR;
				break;
			case 0x2:
				*wc_status = IB_WC_REM_ACCESS_ERR;
				break;
			case 0x3:
				*wc_status = IB_WC_REM_OP_ERR;
				break;
			case 0x4:
				*wc_status = IB_WC_REM_INV_RD_REQ_ERR;
				break;
			}
			break;
		case 0x08:
			*wc_status = IB_WC_RETRY_EXC_ERR;
			break;
		case 0x09:
			*wc_status = IB_WC_RNR_RETRY_EXC_ERR;
			break;
		case 0x0A:
		case 0x2D:
			*wc_status = IB_WC_REM_ABORT_ERR;
			break;
		case 0x0B:
		case 0x2E:
			*wc_status = IB_WC_INV_EECN_ERR;
			break;
		case 0x0C:
		case 0x2F:
			*wc_status = IB_WC_INV_EEC_STATE_ERR;
			break;
		case 0x0D:
			*wc_status = IB_WC_BAD_RESP_ERR;
			break;
		case 0x10:
			/* WQE purged */
			*wc_status = IB_WC_WR_FLUSH_ERR;
			break;
		default:
			*wc_status = IB_WC_FATAL_ERR;

		}
	} else
		*wc_status = IB_WC_SUCCESS;
}

static inline int post_one_send(struct ehca_qp *my_qp,
			 struct ib_send_wr *cur_send_wr,
			 int hidden)
{
	struct ehca_wqe *wqe_p;
	int ret;
	u32 sq_map_idx;
	u64 start_offset = my_qp->ipz_squeue.current_q_offset;

	/* get pointer next to free WQE */
	wqe_p = ipz_qeit_get_inc(&my_qp->ipz_squeue);
	if (unlikely(!wqe_p)) {
		/* too many posted work requests: queue overflow */
		ehca_err(my_qp->ib_qp.device, "Too many posted WQEs "
			 "qp_num=%x", my_qp->ib_qp.qp_num);
		return -ENOMEM;
	}

	/*
	 * Get the index of the WQE in the send queue. The same index is used
	 * for writing into the sq_map.
	 */
	sq_map_idx = start_offset / my_qp->ipz_squeue.qe_size;

	/* write a SEND WQE into the QUEUE */
	ret = ehca_write_swqe(my_qp, wqe_p, cur_send_wr, sq_map_idx, hidden);
	/*
	 * if something failed,
	 * reset the free entry pointer to the start value
	 */
	if (unlikely(ret)) {
		my_qp->ipz_squeue.current_q_offset = start_offset;
		ehca_err(my_qp->ib_qp.device, "Could not write WQE "
			 "qp_num=%x", my_qp->ib_qp.qp_num);
		return -EINVAL;
	}

	return 0;
}

int ehca_post_send(struct ib_qp *qp,
		   struct ib_send_wr *send_wr,
		   struct ib_send_wr **bad_send_wr)
{
	struct ehca_qp *my_qp = container_of(qp, struct ehca_qp, ib_qp);
	int wqe_cnt = 0;
	int ret = 0;
	unsigned long flags;

	/* Reject WR if QP is in RESET, INIT or RTR state */
	if (unlikely(my_qp->state < IB_QPS_RTS)) {
		ehca_err(qp->device, "Invalid QP state  qp_state=%d qpn=%x",
			 my_qp->state, qp->qp_num);
		ret = -EINVAL;
		goto out;
	}

	/* LOCK the QUEUE */
	spin_lock_irqsave(&my_qp->spinlock_s, flags);

	/* Send an empty extra RDMA read if:
	 *  1) there has been an RDMA read on this connection before
	 *  2) no RDMA read occurred for ACK_CIRC_THRESHOLD link packets
	 *  3) we can be sure that any previous extra RDMA read has been
	 *     processed so we don't overflow the SQ
	 */
	if (unlikely(my_qp->unsol_ack_circ &&
		     my_qp->packet_count > ACK_CIRC_THRESHOLD &&
		     my_qp->message_count > my_qp->init_attr.cap.max_send_wr)) {
		/* insert an empty RDMA READ to fix up the remote QP state */
		struct ib_send_wr circ_wr;
		memset(&circ_wr, 0, sizeof(circ_wr));
		circ_wr.opcode = IB_WR_RDMA_READ;
		post_one_send(my_qp, &circ_wr, 1); /* ignore retcode */
		wqe_cnt++;
		ehca_dbg(qp->device, "posted circ wr  qp_num=%x", qp->qp_num);
		my_qp->message_count = my_qp->packet_count = 0;
	}

	/* loop processes list of send reqs */
	while (send_wr) {
		ret = post_one_send(my_qp, send_wr, 0);
		if (unlikely(ret)) {
			goto post_send_exit0;
		}
		wqe_cnt++;
		send_wr = send_wr->next;
	}

post_send_exit0:
	iosync(); /* serialize GAL register access */
	hipz_update_sqa(my_qp, wqe_cnt);
	if (unlikely(ret || ehca_debug_level >= 2))
		ehca_dbg(qp->device, "ehca_qp=%p qp_num=%x wqe_cnt=%d ret=%i",
			 my_qp, qp->qp_num, wqe_cnt, ret);
	my_qp->message_count += wqe_cnt;
	spin_unlock_irqrestore(&my_qp->spinlock_s, flags);

out:
	if (ret)
		*bad_send_wr = send_wr;
	return ret;
}

static int internal_post_recv(struct ehca_qp *my_qp,
			      struct ib_device *dev,
			      struct ib_recv_wr *recv_wr,
			      struct ib_recv_wr **bad_recv_wr)
{
	struct ehca_wqe *wqe_p;
	int wqe_cnt = 0;
	int ret = 0;
	u32 rq_map_idx;
	unsigned long flags;
	struct ehca_qmap_entry *qmap_entry;

	if (unlikely(!HAS_RQ(my_qp))) {
		ehca_err(dev, "QP has no RQ  ehca_qp=%p qp_num=%x ext_type=%d",
			 my_qp, my_qp->real_qp_num, my_qp->ext_type);
		ret = -ENODEV;
		goto out;
	}

	/* LOCK the QUEUE */
	spin_lock_irqsave(&my_qp->spinlock_r, flags);

	/* loop processes list of recv reqs */
	while (recv_wr) {
		u64 start_offset = my_qp->ipz_rqueue.current_q_offset;
		/* get pointer next to free WQE */
		wqe_p = ipz_qeit_get_inc(&my_qp->ipz_rqueue);
		if (unlikely(!wqe_p)) {
			/* too many posted work requests: queue overflow */
			ret = -ENOMEM;
			ehca_err(dev, "Too many posted WQEs "
				"qp_num=%x", my_qp->real_qp_num);
			goto post_recv_exit0;
		}
		/*
		 * Get the index of the WQE in the recv queue. The same index
		 * is used for writing into the rq_map.
		 */
		rq_map_idx = start_offset / my_qp->ipz_rqueue.qe_size;

		/* write a RECV WQE into the QUEUE */
		ret = ehca_write_rwqe(&my_qp->ipz_rqueue, wqe_p, recv_wr,
				rq_map_idx);
		/*
		 * if something failed,
		 * reset the free entry pointer to the start value
		 */
		if (unlikely(ret)) {
			my_qp->ipz_rqueue.current_q_offset = start_offset;
			ret = -EINVAL;
			ehca_err(dev, "Could not write WQE "
				"qp_num=%x", my_qp->real_qp_num);
			goto post_recv_exit0;
		}

		qmap_entry = &my_qp->rq_map.map[rq_map_idx];
		qmap_entry->app_wr_id = get_app_wr_id(recv_wr->wr_id);
		qmap_entry->reported = 0;
		qmap_entry->cqe_req = 1;

		wqe_cnt++;
		recv_wr = recv_wr->next;
	} /* eof for recv_wr */

post_recv_exit0:
	iosync(); /* serialize GAL register access */
	hipz_update_rqa(my_qp, wqe_cnt);
	if (unlikely(ret || ehca_debug_level >= 2))
	    ehca_dbg(dev, "ehca_qp=%p qp_num=%x wqe_cnt=%d ret=%i",
		     my_qp, my_qp->real_qp_num, wqe_cnt, ret);
	spin_unlock_irqrestore(&my_qp->spinlock_r, flags);

out:
	if (ret)
		*bad_recv_wr = recv_wr;

	return ret;
}

int ehca_post_recv(struct ib_qp *qp,
		   struct ib_recv_wr *recv_wr,
		   struct ib_recv_wr **bad_recv_wr)
{
	struct ehca_qp *my_qp = container_of(qp, struct ehca_qp, ib_qp);

	/* Reject WR if QP is in RESET state */
	if (unlikely(my_qp->state == IB_QPS_RESET)) {
		ehca_err(qp->device, "Invalid QP state  qp_state=%d qpn=%x",
			 my_qp->state, qp->qp_num);
		*bad_recv_wr = recv_wr;
		return -EINVAL;
	}

	return internal_post_recv(my_qp, qp->device, recv_wr, bad_recv_wr);
}

int ehca_post_srq_recv(struct ib_srq *srq,
		       struct ib_recv_wr *recv_wr,
		       struct ib_recv_wr **bad_recv_wr)
{
	return internal_post_recv(container_of(srq, struct ehca_qp, ib_srq),
				  srq->device, recv_wr, bad_recv_wr);
}

/*
 * ib_wc_opcode table converts ehca wc opcode to ib
 * Since we use zero to indicate invalid opcode, the actual ib opcode must
 * be decremented!!!
 */
static const u8 ib_wc_opcode[255] = {
	[0x01] = IB_WC_RECV+1,
	[0x02] = IB_WC_RECV_RDMA_WITH_IMM+1,
	[0x04] = IB_WC_BIND_MW+1,
	[0x08] = IB_WC_FETCH_ADD+1,
	[0x10] = IB_WC_COMP_SWAP+1,
	[0x20] = IB_WC_RDMA_WRITE+1,
	[0x40] = IB_WC_RDMA_READ+1,
	[0x80] = IB_WC_SEND+1
};

/* internal function to poll one entry of cq */
static inline int ehca_poll_cq_one(struct ib_cq *cq, struct ib_wc *wc)
{
	int ret = 0, qmap_tail_idx;
	struct ehca_cq *my_cq = container_of(cq, struct ehca_cq, ib_cq);
	struct ehca_cqe *cqe;
	struct ehca_qp *my_qp;
	struct ehca_qmap_entry *qmap_entry;
	struct ehca_queue_map *qmap;
	int cqe_count = 0, is_error;

repoll:
	cqe = (struct ehca_cqe *)
		ipz_qeit_get_inc_valid(&my_cq->ipz_queue);
	if (!cqe) {
		ret = -EAGAIN;
		if (ehca_debug_level >= 3)
			ehca_dbg(cq->device, "Completion queue is empty  "
				 "my_cq=%p cq_num=%x", my_cq, my_cq->cq_number);
		goto poll_cq_one_exit0;
	}

	/* prevents loads being reordered across this point */
	rmb();

	cqe_count++;
	if (unlikely(cqe->status & WC_STATUS_PURGE_BIT)) {
		struct ehca_qp *qp;
		int purgeflag;
		unsigned long flags;

		qp = ehca_cq_get_qp(my_cq, cqe->local_qp_number);
		if (!qp) {
			ehca_err(cq->device, "cq_num=%x qp_num=%x "
				 "could not find qp -> ignore cqe",
				 my_cq->cq_number, cqe->local_qp_number);
			ehca_dmp(cqe, 64, "cq_num=%x qp_num=%x",
				 my_cq->cq_number, cqe->local_qp_number);
			/* ignore this purged cqe */
			goto repoll;
		}
		spin_lock_irqsave(&qp->spinlock_s, flags);
		purgeflag = qp->sqerr_purgeflag;
		spin_unlock_irqrestore(&qp->spinlock_s, flags);

		if (purgeflag) {
			ehca_dbg(cq->device,
				 "Got CQE with purged bit qp_num=%x src_qp=%x",
				 cqe->local_qp_number, cqe->remote_qp_number);
			if (ehca_debug_level >= 2)
				ehca_dmp(cqe, 64, "qp_num=%x src_qp=%x",
					 cqe->local_qp_number,
					 cqe->remote_qp_number);
			/*
			 * ignore this to avoid double cqes of bad wqe
			 * that caused sqe and turn off purge flag
			 */
			qp->sqerr_purgeflag = 0;
			goto repoll;
		}
	}

	is_error = cqe->status & WC_STATUS_ERROR_BIT;

	/* trace error CQEs if debug_level >= 1, trace all CQEs if >= 3 */
	if (unlikely(ehca_debug_level >= 3 || (ehca_debug_level && is_error))) {
		ehca_dbg(cq->device,
			 "Received %sCOMPLETION ehca_cq=%p cq_num=%x -----",
			 is_error ? "ERROR " : "", my_cq, my_cq->cq_number);
		ehca_dmp(cqe, 64, "ehca_cq=%p cq_num=%x",
			 my_cq, my_cq->cq_number);
		ehca_dbg(cq->device,
			 "ehca_cq=%p cq_num=%x -------------------------",
			 my_cq, my_cq->cq_number);
	}

	read_lock(&ehca_qp_idr_lock);
	my_qp = idr_find(&ehca_qp_idr, cqe->qp_token);
	read_unlock(&ehca_qp_idr_lock);
	if (!my_qp)
		goto repoll;
	wc->qp = &my_qp->ib_qp;

	qmap_tail_idx = get_app_wr_id(cqe->work_request_id);
	if (!(cqe->w_completion_flags & WC_SEND_RECEIVE_BIT))
		/* We got a send completion. */
		qmap = &my_qp->sq_map;
	else
		/* We got a receive completion. */
		qmap = &my_qp->rq_map;

	/* advance the tail pointer */
	qmap->tail = qmap_tail_idx;

	if (is_error) {
		/*
		 * set left_to_poll to 0 because in error state, we will not
		 * get any additional CQEs
		 */
		my_qp->sq_map.next_wqe_idx = next_index(my_qp->sq_map.tail,
							my_qp->sq_map.entries);
		my_qp->sq_map.left_to_poll = 0;
		ehca_add_to_err_list(my_qp, 1);

		my_qp->rq_map.next_wqe_idx = next_index(my_qp->rq_map.tail,
							my_qp->rq_map.entries);
		my_qp->rq_map.left_to_poll = 0;
		if (HAS_RQ(my_qp))
			ehca_add_to_err_list(my_qp, 0);
	}

	qmap_entry = &qmap->map[qmap_tail_idx];
	if (qmap_entry->reported) {
		ehca_warn(cq->device, "Double cqe on qp_num=%#x",
				my_qp->real_qp_num);
		/* found a double cqe, discard it and read next one */
		goto repoll;
	}

	wc->wr_id = replace_wr_id(cqe->work_request_id, qmap_entry->app_wr_id);
	qmap_entry->reported = 1;

	/* if left_to_poll is decremented to 0, add the QP to the error list */
	if (qmap->left_to_poll > 0) {
		qmap->left_to_poll--;
		if ((my_qp->sq_map.left_to_poll == 0) &&
				(my_qp->rq_map.left_to_poll == 0)) {
			ehca_add_to_err_list(my_qp, 1);
			if (HAS_RQ(my_qp))
				ehca_add_to_err_list(my_qp, 0);
		}
	}

	/* eval ib_wc_opcode */
	wc->opcode = ib_wc_opcode[cqe->optype]-1;
	if (unlikely(wc->opcode == -1)) {
		ehca_err(cq->device, "Invalid cqe->OPType=%x cqe->status=%x "
			 "ehca_cq=%p cq_num=%x",
			 cqe->optype, cqe->status, my_cq, my_cq->cq_number);
		/* dump cqe for other infos */
		ehca_dmp(cqe, 64, "ehca_cq=%p cq_num=%x",
			 my_cq, my_cq->cq_number);
		/* update also queue adder to throw away this entry!!! */
		goto repoll;
	}

	/* eval ib_wc_status */
	if (unlikely(is_error)) {
		/* complete with errors */
		map_ib_wc_status(cqe->status, &wc->status);
		wc->vendor_err = wc->status;
	} else
		wc->status = IB_WC_SUCCESS;

	wc->byte_len = cqe->nr_bytes_transferred;
	wc->pkey_index = cqe->pkey_index;
	wc->slid = cqe->rlid;
	wc->dlid_path_bits = cqe->dlid;
	wc->src_qp = cqe->remote_qp_number;
	/*
	 * HW has "Immed data present" and "GRH present" in bits 6 and 5.
	 * SW defines those in bits 1 and 0, so we can just shift and mask.
	 */
	wc->wc_flags = (cqe->w_completion_flags >> 5) & 3;
	wc->ex.imm_data = cpu_to_be32(cqe->immediate_data);
	wc->sl = cqe->service_level;

poll_cq_one_exit0:
	if (cqe_count > 0)
		hipz_update_feca(my_cq, cqe_count);

	return ret;
}

static int generate_flush_cqes(struct ehca_qp *my_qp, struct ib_cq *cq,
			       struct ib_wc *wc, int num_entries,
			       struct ipz_queue *ipz_queue, int on_sq)
{
	int nr = 0;
	struct ehca_wqe *wqe;
	u64 offset;
	struct ehca_queue_map *qmap;
	struct ehca_qmap_entry *qmap_entry;

	if (on_sq)
		qmap = &my_qp->sq_map;
	else
		qmap = &my_qp->rq_map;

	qmap_entry = &qmap->map[qmap->next_wqe_idx];

	while ((nr < num_entries) && (qmap_entry->reported == 0)) {
		/* generate flush CQE */

		memset(wc, 0, sizeof(*wc));

		offset = qmap->next_wqe_idx * ipz_queue->qe_size;
		wqe = (struct ehca_wqe *)ipz_qeit_calc(ipz_queue, offset);
		if (!wqe) {
			ehca_err(cq->device, "Invalid wqe offset=%#llx on "
				 "qp_num=%#x", offset, my_qp->real_qp_num);
			return nr;
		}

		wc->wr_id = replace_wr_id(wqe->work_request_id,
					  qmap_entry->app_wr_id);

		if (on_sq) {
			switch (wqe->optype) {
			case WQE_OPTYPE_SEND:
				wc->opcode = IB_WC_SEND;
				break;
			case WQE_OPTYPE_RDMAWRITE:
				wc->opcode = IB_WC_RDMA_WRITE;
				break;
			case WQE_OPTYPE_RDMAREAD:
				wc->opcode = IB_WC_RDMA_READ;
				break;
			default:
				ehca_err(cq->device, "Invalid optype=%x",
						wqe->optype);
				return nr;
			}
		} else
			wc->opcode = IB_WC_RECV;

		if (wqe->wr_flag & WQE_WRFLAG_IMM_DATA_PRESENT) {
			wc->ex.imm_data = wqe->immediate_data;
			wc->wc_flags |= IB_WC_WITH_IMM;
		}

		wc->status = IB_WC_WR_FLUSH_ERR;

		wc->qp = &my_qp->ib_qp;

		/* mark as reported and advance next_wqe pointer */
		qmap_entry->reported = 1;
		qmap->next_wqe_idx = next_index(qmap->next_wqe_idx,
						qmap->entries);
		qmap_entry = &qmap->map[qmap->next_wqe_idx];

		wc++; nr++;
	}

	return nr;

}

int ehca_poll_cq(struct ib_cq *cq, int num_entries, struct ib_wc *wc)
{
	struct ehca_cq *my_cq = container_of(cq, struct ehca_cq, ib_cq);
	int nr;
	struct ehca_qp *err_qp;
	struct ib_wc *current_wc = wc;
	int ret = 0;
	unsigned long flags;
	int entries_left = num_entries;

	if (num_entries < 1) {
		ehca_err(cq->device, "Invalid num_entries=%d ehca_cq=%p "
			 "cq_num=%x", num_entries, my_cq, my_cq->cq_number);
		ret = -EINVAL;
		goto poll_cq_exit0;
	}

	spin_lock_irqsave(&my_cq->spinlock, flags);

	/* generate flush cqes for send queues */
	list_for_each_entry(err_qp, &my_cq->sqp_err_list, sq_err_node) {
		nr = generate_flush_cqes(err_qp, cq, current_wc, entries_left,
				&err_qp->ipz_squeue, 1);
		entries_left -= nr;
		current_wc += nr;

		if (entries_left == 0)
			break;
	}

	/* generate flush cqes for receive queues */
	list_for_each_entry(err_qp, &my_cq->rqp_err_list, rq_err_node) {
		nr = generate_flush_cqes(err_qp, cq, current_wc, entries_left,
				&err_qp->ipz_rqueue, 0);
		entries_left -= nr;
		current_wc += nr;

		if (entries_left == 0)
			break;
	}

	for (nr = 0; nr < entries_left; nr++) {
		ret = ehca_poll_cq_one(cq, current_wc);
		if (ret)
			break;
		current_wc++;
	} /* eof for nr */
	entries_left -= nr;

	spin_unlock_irqrestore(&my_cq->spinlock, flags);
	if (ret == -EAGAIN  || !ret)
		ret = num_entries - entries_left;

poll_cq_exit0:
	return ret;
}

int ehca_req_notify_cq(struct ib_cq *cq, enum ib_cq_notify_flags notify_flags)
{
	struct ehca_cq *my_cq = container_of(cq, struct ehca_cq, ib_cq);
	int ret = 0;

	switch (notify_flags & IB_CQ_SOLICITED_MASK) {
	case IB_CQ_SOLICITED:
		hipz_set_cqx_n0(my_cq, 1);
		break;
	case IB_CQ_NEXT_COMP:
		hipz_set_cqx_n1(my_cq, 1);
		break;
	default:
		return -EINVAL;
	}

	if (notify_flags & IB_CQ_REPORT_MISSED_EVENTS) {
		unsigned long spl_flags;
		spin_lock_irqsave(&my_cq->spinlock, spl_flags);
		ret = ipz_qeit_is_valid(&my_cq->ipz_queue);
		spin_unlock_irqrestore(&my_cq->spinlock, spl_flags);
	}

	return ret;
}
