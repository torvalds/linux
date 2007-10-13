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


#include <asm-powerpc/system.h>
#include "ehca_classes.h"
#include "ehca_tools.h"
#include "ehca_qes.h"
#include "ehca_iverbs.h"
#include "hcp_if.h"
#include "hipz_fns.h"

static inline int ehca_write_rwqe(struct ipz_queue *ipz_rqueue,
				  struct ehca_wqe *wqe_p,
				  struct ib_recv_wr *recv_wr)
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

	wqe_p->work_request_id = recv_wr->wr_id;
	wqe_p->nr_of_data_seg = recv_wr->num_sge;

	for (cnt_ds = 0; cnt_ds < recv_wr->num_sge; cnt_ds++) {
		wqe_p->u.all_rcv.sg_list[cnt_ds].vaddr =
			recv_wr->sg_list[cnt_ds].addr;
		wqe_p->u.all_rcv.sg_list[cnt_ds].lkey =
			recv_wr->sg_list[cnt_ds].lkey;
		wqe_p->u.all_rcv.sg_list[cnt_ds].length =
			recv_wr->sg_list[cnt_ds].length;
	}

	if (ehca_debug_level) {
		ehca_gen_dbg("RECEIVE WQE written into ipz_rqueue=%p",
			     ipz_rqueue);
		ehca_dmp( wqe_p, 16*(6 + wqe_p->nr_of_data_seg), "recv wqe");
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
				  const struct ib_send_wr *send_wr)
{
	u32 idx;
	u64 dma_length;
	struct ehca_av *my_av;
	u32 remote_qkey = send_wr->wr.ud.remote_qkey;

	if (unlikely((send_wr->num_sge < 0) ||
		     (send_wr->num_sge > qp->ipz_squeue.act_nr_of_sg))) {
		ehca_gen_err("Invalid number of WQE SGE. "
			 "num_sqe=%x max_nr_of_sg=%x",
			 send_wr->num_sge, qp->ipz_squeue.act_nr_of_sg);
		return -EINVAL; /* invalid SG list length */
	}

	/* clear wqe header until sglist */
	memset(wqe_p, 0, offsetof(struct ehca_wqe, u.ud_av.sg_list));

	wqe_p->work_request_id = send_wr->wr_id;

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

	if (send_wr->send_flags & IB_SEND_SIGNALED)
		wqe_p->wr_flag |= WQE_WRFLAG_REQ_SIGNAL_COM;

	if (send_wr->opcode == IB_WR_SEND_WITH_IMM ||
	    send_wr->opcode == IB_WR_RDMA_WRITE_WITH_IMM) {
		/* this might not work as long as HW does not support it */
		wqe_p->immediate_data = be32_to_cpu(send_wr->imm_data);
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
		if (!send_wr->wr.ud.ah) {
			ehca_gen_err("wr.ud.ah is NULL. qp=%p", qp);
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
		wqe_p->u.nud.remote_virtual_adress =
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

		break;

	default:
		ehca_gen_err("Invalid qptype=%x", qp->qp_type);
		return -EINVAL;
	}

	if (ehca_debug_level) {
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

int ehca_post_send(struct ib_qp *qp,
		   struct ib_send_wr *send_wr,
		   struct ib_send_wr **bad_send_wr)
{
	struct ehca_qp *my_qp = container_of(qp, struct ehca_qp, ib_qp);
	struct ib_send_wr *cur_send_wr;
	struct ehca_wqe *wqe_p;
	int wqe_cnt = 0;
	int ret = 0;
	unsigned long flags;

	/* LOCK the QUEUE */
	spin_lock_irqsave(&my_qp->spinlock_s, flags);

	/* loop processes list of send reqs */
	for (cur_send_wr = send_wr; cur_send_wr != NULL;
	     cur_send_wr = cur_send_wr->next) {
		u64 start_offset = my_qp->ipz_squeue.current_q_offset;
		/* get pointer next to free WQE */
		wqe_p = ipz_qeit_get_inc(&my_qp->ipz_squeue);
		if (unlikely(!wqe_p)) {
			/* too many posted work requests: queue overflow */
			if (bad_send_wr)
				*bad_send_wr = cur_send_wr;
			if (wqe_cnt == 0) {
				ret = -ENOMEM;
				ehca_err(qp->device, "Too many posted WQEs "
					 "qp_num=%x", qp->qp_num);
			}
			goto post_send_exit0;
		}
		/* write a SEND WQE into the QUEUE */
		ret = ehca_write_swqe(my_qp, wqe_p, cur_send_wr);
		/*
		 * if something failed,
		 * reset the free entry pointer to the start value
		 */
		if (unlikely(ret)) {
			my_qp->ipz_squeue.current_q_offset = start_offset;
			*bad_send_wr = cur_send_wr;
			if (wqe_cnt == 0) {
				ret = -EINVAL;
				ehca_err(qp->device, "Could not write WQE "
					 "qp_num=%x", qp->qp_num);
			}
			goto post_send_exit0;
		}
		wqe_cnt++;
		ehca_dbg(qp->device, "ehca_qp=%p qp_num=%x wqe_cnt=%d",
			 my_qp, qp->qp_num, wqe_cnt);
	} /* eof for cur_send_wr */

post_send_exit0:
	iosync(); /* serialize GAL register access */
	hipz_update_sqa(my_qp, wqe_cnt);
	spin_unlock_irqrestore(&my_qp->spinlock_s, flags);
	return ret;
}

static int internal_post_recv(struct ehca_qp *my_qp,
			      struct ib_device *dev,
			      struct ib_recv_wr *recv_wr,
			      struct ib_recv_wr **bad_recv_wr)
{
	struct ib_recv_wr *cur_recv_wr;
	struct ehca_wqe *wqe_p;
	int wqe_cnt = 0;
	int ret = 0;
	unsigned long flags;

	if (unlikely(!HAS_RQ(my_qp))) {
		ehca_err(dev, "QP has no RQ  ehca_qp=%p qp_num=%x ext_type=%d",
			 my_qp, my_qp->real_qp_num, my_qp->ext_type);
		return -ENODEV;
	}

	/* LOCK the QUEUE */
	spin_lock_irqsave(&my_qp->spinlock_r, flags);

	/* loop processes list of send reqs */
	for (cur_recv_wr = recv_wr; cur_recv_wr != NULL;
	     cur_recv_wr = cur_recv_wr->next) {
		u64 start_offset = my_qp->ipz_rqueue.current_q_offset;
		/* get pointer next to free WQE */
		wqe_p = ipz_qeit_get_inc(&my_qp->ipz_rqueue);
		if (unlikely(!wqe_p)) {
			/* too many posted work requests: queue overflow */
			if (bad_recv_wr)
				*bad_recv_wr = cur_recv_wr;
			if (wqe_cnt == 0) {
				ret = -ENOMEM;
				ehca_err(dev, "Too many posted WQEs "
					 "qp_num=%x", my_qp->real_qp_num);
			}
			goto post_recv_exit0;
		}
		/* write a RECV WQE into the QUEUE */
		ret = ehca_write_rwqe(&my_qp->ipz_rqueue, wqe_p, cur_recv_wr);
		/*
		 * if something failed,
		 * reset the free entry pointer to the start value
		 */
		if (unlikely(ret)) {
			my_qp->ipz_rqueue.current_q_offset = start_offset;
			*bad_recv_wr = cur_recv_wr;
			if (wqe_cnt == 0) {
				ret = -EINVAL;
				ehca_err(dev, "Could not write WQE "
					 "qp_num=%x", my_qp->real_qp_num);
			}
			goto post_recv_exit0;
		}
		wqe_cnt++;
		ehca_dbg(dev, "ehca_qp=%p qp_num=%x wqe_cnt=%d",
			 my_qp, my_qp->real_qp_num, wqe_cnt);
	} /* eof for cur_recv_wr */

post_recv_exit0:
	iosync(); /* serialize GAL register access */
	hipz_update_rqa(my_qp, wqe_cnt);
	spin_unlock_irqrestore(&my_qp->spinlock_r, flags);
	return ret;
}

int ehca_post_recv(struct ib_qp *qp,
		   struct ib_recv_wr *recv_wr,
		   struct ib_recv_wr **bad_recv_wr)
{
	return internal_post_recv(container_of(qp, struct ehca_qp, ib_qp),
				  qp->device, recv_wr, bad_recv_wr);
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
	int ret = 0;
	struct ehca_cq *my_cq = container_of(cq, struct ehca_cq, ib_cq);
	struct ehca_cqe *cqe;
	struct ehca_qp *my_qp;
	int cqe_count = 0;

poll_cq_one_read_cqe:
	cqe = (struct ehca_cqe *)
		ipz_qeit_get_inc_valid(&my_cq->ipz_queue);
	if (!cqe) {
		ret = -EAGAIN;
		ehca_dbg(cq->device, "Completion queue is empty ehca_cq=%p "
			 "cq_num=%x ret=%i", my_cq, my_cq->cq_number, ret);
		goto  poll_cq_one_exit0;
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
			goto poll_cq_one_read_cqe;
		}
		spin_lock_irqsave(&qp->spinlock_s, flags);
		purgeflag = qp->sqerr_purgeflag;
		spin_unlock_irqrestore(&qp->spinlock_s, flags);

		if (purgeflag) {
			ehca_dbg(cq->device,
				 "Got CQE with purged bit qp_num=%x src_qp=%x",
				 cqe->local_qp_number, cqe->remote_qp_number);
			if (ehca_debug_level)
				ehca_dmp(cqe, 64, "qp_num=%x src_qp=%x",
					 cqe->local_qp_number,
					 cqe->remote_qp_number);
			/*
			 * ignore this to avoid double cqes of bad wqe
			 * that caused sqe and turn off purge flag
			 */
			qp->sqerr_purgeflag = 0;
			goto poll_cq_one_read_cqe;
		}
	}

	/* tracing cqe */
	if (unlikely(ehca_debug_level)) {
		ehca_dbg(cq->device,
			 "Received COMPLETION ehca_cq=%p cq_num=%x -----",
			 my_cq, my_cq->cq_number);
		ehca_dmp(cqe, 64, "ehca_cq=%p cq_num=%x",
			 my_cq, my_cq->cq_number);
		ehca_dbg(cq->device,
			 "ehca_cq=%p cq_num=%x -------------------------",
			 my_cq, my_cq->cq_number);
	}

	/* we got a completion! */
	wc->wr_id = cqe->work_request_id;

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
		goto poll_cq_one_exit0;
	}
	/* eval ib_wc_status */
	if (unlikely(cqe->status & WC_STATUS_ERROR_BIT)) {
		/* complete with errors */
		map_ib_wc_status(cqe->status, &wc->status);
		wc->vendor_err = wc->status;
	} else
		wc->status = IB_WC_SUCCESS;

	read_lock(&ehca_qp_idr_lock);
	my_qp = idr_find(&ehca_qp_idr, cqe->qp_token);
	wc->qp = &my_qp->ib_qp;
	read_unlock(&ehca_qp_idr_lock);

	wc->byte_len = cqe->nr_bytes_transferred;
	wc->pkey_index = cqe->pkey_index;
	wc->slid = cqe->rlid;
	wc->dlid_path_bits = cqe->dlid;
	wc->src_qp = cqe->remote_qp_number;
	wc->wc_flags = cqe->w_completion_flags;
	wc->imm_data = cpu_to_be32(cqe->immediate_data);
	wc->sl = cqe->service_level;

	if (unlikely(wc->status != IB_WC_SUCCESS))
		ehca_dbg(cq->device,
			 "ehca_cq=%p cq_num=%x WARNING unsuccessful cqe "
			 "OPType=%x status=%x qp_num=%x src_qp=%x wr_id=%lx "
			 "cqe=%p", my_cq, my_cq->cq_number, cqe->optype,
			 cqe->status, cqe->local_qp_number,
			 cqe->remote_qp_number, cqe->work_request_id, cqe);

poll_cq_one_exit0:
	if (cqe_count > 0)
		hipz_update_feca(my_cq, cqe_count);

	return ret;
}

int ehca_poll_cq(struct ib_cq *cq, int num_entries, struct ib_wc *wc)
{
	struct ehca_cq *my_cq = container_of(cq, struct ehca_cq, ib_cq);
	int nr;
	struct ib_wc *current_wc = wc;
	int ret = 0;
	unsigned long flags;

	if (num_entries < 1) {
		ehca_err(cq->device, "Invalid num_entries=%d ehca_cq=%p "
			 "cq_num=%x", num_entries, my_cq, my_cq->cq_number);
		ret = -EINVAL;
		goto poll_cq_exit0;
	}

	spin_lock_irqsave(&my_cq->spinlock, flags);
	for (nr = 0; nr < num_entries; nr++) {
		ret = ehca_poll_cq_one(cq, current_wc);
		if (ret)
			break;
		current_wc++;
	} /* eof for nr */
	spin_unlock_irqrestore(&my_cq->spinlock, flags);
	if (ret == -EAGAIN  || !ret)
		ret = nr;

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
