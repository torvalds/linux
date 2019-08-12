// SPDX-License-Identifier: GPL-2.0 or BSD-3-Clause

/* Authors: Bernard Metzler <bmt@zurich.ibm.com> */
/* Copyright (c) 2008-2019, IBM Corporation */

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/net.h>
#include <linux/scatterlist.h>
#include <linux/llist.h>
#include <asm/barrier.h>
#include <net/tcp.h>

#include "siw.h"
#include "siw_verbs.h"
#include "siw_mem.h"

static char siw_qp_state_to_string[SIW_QP_STATE_COUNT][sizeof "TERMINATE"] = {
	[SIW_QP_STATE_IDLE] = "IDLE",
	[SIW_QP_STATE_RTR] = "RTR",
	[SIW_QP_STATE_RTS] = "RTS",
	[SIW_QP_STATE_CLOSING] = "CLOSING",
	[SIW_QP_STATE_TERMINATE] = "TERMINATE",
	[SIW_QP_STATE_ERROR] = "ERROR"
};

/*
 * iWARP (RDMAP, DDP and MPA) parameters as well as Softiwarp settings on a
 * per-RDMAP message basis. Please keep order of initializer. All MPA len
 * is initialized to minimum packet size.
 */
struct iwarp_msg_info iwarp_pktinfo[RDMAP_TERMINATE + 1] = {
	{ /* RDMAP_RDMA_WRITE */
	  .hdr_len = sizeof(struct iwarp_rdma_write),
	  .ctrl.mpa_len = htons(sizeof(struct iwarp_rdma_write) - 2),
	  .ctrl.ddp_rdmap_ctrl = DDP_FLAG_TAGGED | DDP_FLAG_LAST |
				 cpu_to_be16(DDP_VERSION << 8) |
				 cpu_to_be16(RDMAP_VERSION << 6) |
				 cpu_to_be16(RDMAP_RDMA_WRITE),
	  .rx_data = siw_proc_write },
	{ /* RDMAP_RDMA_READ_REQ */
	  .hdr_len = sizeof(struct iwarp_rdma_rreq),
	  .ctrl.mpa_len = htons(sizeof(struct iwarp_rdma_rreq) - 2),
	  .ctrl.ddp_rdmap_ctrl = DDP_FLAG_LAST | cpu_to_be16(DDP_VERSION << 8) |
				 cpu_to_be16(RDMAP_VERSION << 6) |
				 cpu_to_be16(RDMAP_RDMA_READ_REQ),
	  .rx_data = siw_proc_rreq },
	{ /* RDMAP_RDMA_READ_RESP */
	  .hdr_len = sizeof(struct iwarp_rdma_rresp),
	  .ctrl.mpa_len = htons(sizeof(struct iwarp_rdma_rresp) - 2),
	  .ctrl.ddp_rdmap_ctrl = DDP_FLAG_TAGGED | DDP_FLAG_LAST |
				 cpu_to_be16(DDP_VERSION << 8) |
				 cpu_to_be16(RDMAP_VERSION << 6) |
				 cpu_to_be16(RDMAP_RDMA_READ_RESP),
	  .rx_data = siw_proc_rresp },
	{ /* RDMAP_SEND */
	  .hdr_len = sizeof(struct iwarp_send),
	  .ctrl.mpa_len = htons(sizeof(struct iwarp_send) - 2),
	  .ctrl.ddp_rdmap_ctrl = DDP_FLAG_LAST | cpu_to_be16(DDP_VERSION << 8) |
				 cpu_to_be16(RDMAP_VERSION << 6) |
				 cpu_to_be16(RDMAP_SEND),
	  .rx_data = siw_proc_send },
	{ /* RDMAP_SEND_INVAL */
	  .hdr_len = sizeof(struct iwarp_send_inv),
	  .ctrl.mpa_len = htons(sizeof(struct iwarp_send_inv) - 2),
	  .ctrl.ddp_rdmap_ctrl = DDP_FLAG_LAST | cpu_to_be16(DDP_VERSION << 8) |
				 cpu_to_be16(RDMAP_VERSION << 6) |
				 cpu_to_be16(RDMAP_SEND_INVAL),
	  .rx_data = siw_proc_send },
	{ /* RDMAP_SEND_SE */
	  .hdr_len = sizeof(struct iwarp_send),
	  .ctrl.mpa_len = htons(sizeof(struct iwarp_send) - 2),
	  .ctrl.ddp_rdmap_ctrl = DDP_FLAG_LAST | cpu_to_be16(DDP_VERSION << 8) |
				 cpu_to_be16(RDMAP_VERSION << 6) |
				 cpu_to_be16(RDMAP_SEND_SE),
	  .rx_data = siw_proc_send },
	{ /* RDMAP_SEND_SE_INVAL */
	  .hdr_len = sizeof(struct iwarp_send_inv),
	  .ctrl.mpa_len = htons(sizeof(struct iwarp_send_inv) - 2),
	  .ctrl.ddp_rdmap_ctrl = DDP_FLAG_LAST | cpu_to_be16(DDP_VERSION << 8) |
				 cpu_to_be16(RDMAP_VERSION << 6) |
				 cpu_to_be16(RDMAP_SEND_SE_INVAL),
	  .rx_data = siw_proc_send },
	{ /* RDMAP_TERMINATE */
	  .hdr_len = sizeof(struct iwarp_terminate),
	  .ctrl.mpa_len = htons(sizeof(struct iwarp_terminate) - 2),
	  .ctrl.ddp_rdmap_ctrl = DDP_FLAG_LAST | cpu_to_be16(DDP_VERSION << 8) |
				 cpu_to_be16(RDMAP_VERSION << 6) |
				 cpu_to_be16(RDMAP_TERMINATE),
	  .rx_data = siw_proc_terminate }
};

void siw_qp_llp_data_ready(struct sock *sk)
{
	struct siw_qp *qp;

	read_lock(&sk->sk_callback_lock);

	if (unlikely(!sk->sk_user_data || !sk_to_qp(sk)))
		goto done;

	qp = sk_to_qp(sk);

	if (likely(!qp->rx_stream.rx_suspend &&
		   down_read_trylock(&qp->state_lock))) {
		read_descriptor_t rd_desc = { .arg.data = qp, .count = 1 };

		if (likely(qp->attrs.state == SIW_QP_STATE_RTS))
			/*
			 * Implements data receive operation during
			 * socket callback. TCP gracefully catches
			 * the case where there is nothing to receive
			 * (not calling siw_tcp_rx_data() then).
			 */
			tcp_read_sock(sk, &rd_desc, siw_tcp_rx_data);

		up_read(&qp->state_lock);
	} else {
		siw_dbg_qp(qp, "unable to process RX, suspend: %d\n",
			   qp->rx_stream.rx_suspend);
	}
done:
	read_unlock(&sk->sk_callback_lock);
}

void siw_qp_llp_close(struct siw_qp *qp)
{
	siw_dbg_qp(qp, "enter llp close, state = %s\n",
		   siw_qp_state_to_string[qp->attrs.state]);

	down_write(&qp->state_lock);

	qp->rx_stream.rx_suspend = 1;
	qp->tx_ctx.tx_suspend = 1;
	qp->attrs.sk = NULL;

	switch (qp->attrs.state) {
	case SIW_QP_STATE_RTS:
	case SIW_QP_STATE_RTR:
	case SIW_QP_STATE_IDLE:
	case SIW_QP_STATE_TERMINATE:
		qp->attrs.state = SIW_QP_STATE_ERROR;
		break;
	/*
	 * SIW_QP_STATE_CLOSING:
	 *
	 * This is a forced close. shall the QP be moved to
	 * ERROR or IDLE ?
	 */
	case SIW_QP_STATE_CLOSING:
		if (tx_wqe(qp)->wr_status == SIW_WR_IDLE)
			qp->attrs.state = SIW_QP_STATE_ERROR;
		else
			qp->attrs.state = SIW_QP_STATE_IDLE;
		break;

	default:
		siw_dbg_qp(qp, "llp close: no state transition needed: %s\n",
			   siw_qp_state_to_string[qp->attrs.state]);
		break;
	}
	siw_sq_flush(qp);
	siw_rq_flush(qp);

	/*
	 * Dereference closing CEP
	 */
	if (qp->cep) {
		siw_cep_put(qp->cep);
		qp->cep = NULL;
	}

	up_write(&qp->state_lock);

	siw_dbg_qp(qp, "llp close exit: state %s\n",
		   siw_qp_state_to_string[qp->attrs.state]);
}

/*
 * socket callback routine informing about newly available send space.
 * Function schedules SQ work for processing SQ items.
 */
void siw_qp_llp_write_space(struct sock *sk)
{
	struct siw_cep *cep = sk_to_cep(sk);

	cep->sk_write_space(sk);

	if (!test_bit(SOCK_NOSPACE, &sk->sk_socket->flags))
		(void)siw_sq_start(cep->qp);
}

static int siw_qp_readq_init(struct siw_qp *qp, int irq_size, int orq_size)
{
	irq_size = roundup_pow_of_two(irq_size);
	orq_size = roundup_pow_of_two(orq_size);

	qp->attrs.irq_size = irq_size;
	qp->attrs.orq_size = orq_size;

	qp->irq = vzalloc(irq_size * sizeof(struct siw_sqe));
	if (!qp->irq) {
		siw_dbg_qp(qp, "irq malloc for %d failed\n", irq_size);
		qp->attrs.irq_size = 0;
		return -ENOMEM;
	}
	qp->orq = vzalloc(orq_size * sizeof(struct siw_sqe));
	if (!qp->orq) {
		siw_dbg_qp(qp, "orq malloc for %d failed\n", orq_size);
		qp->attrs.orq_size = 0;
		qp->attrs.irq_size = 0;
		vfree(qp->irq);
		return -ENOMEM;
	}
	siw_dbg_qp(qp, "ORD %d, IRD %d\n", orq_size, irq_size);
	return 0;
}

static int siw_qp_enable_crc(struct siw_qp *qp)
{
	struct siw_rx_stream *c_rx = &qp->rx_stream;
	struct siw_iwarp_tx *c_tx = &qp->tx_ctx;
	int size;

	if (siw_crypto_shash == NULL)
		return -ENOENT;

	size = crypto_shash_descsize(siw_crypto_shash) +
		sizeof(struct shash_desc);

	c_tx->mpa_crc_hd = kzalloc(size, GFP_KERNEL);
	c_rx->mpa_crc_hd = kzalloc(size, GFP_KERNEL);
	if (!c_tx->mpa_crc_hd || !c_rx->mpa_crc_hd) {
		kfree(c_tx->mpa_crc_hd);
		kfree(c_rx->mpa_crc_hd);
		c_tx->mpa_crc_hd = NULL;
		c_rx->mpa_crc_hd = NULL;
		return -ENOMEM;
	}
	c_tx->mpa_crc_hd->tfm = siw_crypto_shash;
	c_rx->mpa_crc_hd->tfm = siw_crypto_shash;

	return 0;
}

/*
 * Send a non signalled READ or WRITE to peer side as negotiated
 * with MPAv2 P2P setup protocol. The work request is only created
 * as a current active WR and does not consume Send Queue space.
 *
 * Caller must hold QP state lock.
 */
int siw_qp_mpa_rts(struct siw_qp *qp, enum mpa_v2_ctrl ctrl)
{
	struct siw_wqe *wqe = tx_wqe(qp);
	unsigned long flags;
	int rv = 0;

	spin_lock_irqsave(&qp->sq_lock, flags);

	if (unlikely(wqe->wr_status != SIW_WR_IDLE)) {
		spin_unlock_irqrestore(&qp->sq_lock, flags);
		return -EIO;
	}
	memset(wqe->mem, 0, sizeof(*wqe->mem) * SIW_MAX_SGE);

	wqe->wr_status = SIW_WR_QUEUED;
	wqe->sqe.flags = 0;
	wqe->sqe.num_sge = 1;
	wqe->sqe.sge[0].length = 0;
	wqe->sqe.sge[0].laddr = 0;
	wqe->sqe.sge[0].lkey = 0;
	/*
	 * While it must not be checked for inbound zero length
	 * READ/WRITE, some HW may treat STag 0 special.
	 */
	wqe->sqe.rkey = 1;
	wqe->sqe.raddr = 0;
	wqe->processed = 0;

	if (ctrl & MPA_V2_RDMA_WRITE_RTR)
		wqe->sqe.opcode = SIW_OP_WRITE;
	else if (ctrl & MPA_V2_RDMA_READ_RTR) {
		struct siw_sqe *rreq;

		wqe->sqe.opcode = SIW_OP_READ;

		spin_lock(&qp->orq_lock);

		rreq = orq_get_free(qp);
		if (rreq) {
			siw_read_to_orq(rreq, &wqe->sqe);
			qp->orq_put++;
		} else
			rv = -EIO;

		spin_unlock(&qp->orq_lock);
	} else
		rv = -EINVAL;

	if (rv)
		wqe->wr_status = SIW_WR_IDLE;

	spin_unlock_irqrestore(&qp->sq_lock, flags);

	if (!rv)
		rv = siw_sq_start(qp);

	return rv;
}

/*
 * Map memory access error to DDP tagged error
 */
enum ddp_ecode siw_tagged_error(enum siw_access_state state)
{
	switch (state) {
	case E_STAG_INVALID:
		return DDP_ECODE_T_INVALID_STAG;
	case E_BASE_BOUNDS:
		return DDP_ECODE_T_BASE_BOUNDS;
	case E_PD_MISMATCH:
		return DDP_ECODE_T_STAG_NOT_ASSOC;
	case E_ACCESS_PERM:
		/*
		 * RFC 5041 (DDP) lacks an ecode for insufficient access
		 * permissions. 'Invalid STag' seem to be the closest
		 * match though.
		 */
		return DDP_ECODE_T_INVALID_STAG;
	default:
		WARN_ON(1);
		return DDP_ECODE_T_INVALID_STAG;
	}
}

/*
 * Map memory access error to RDMAP protection error
 */
enum rdmap_ecode siw_rdmap_error(enum siw_access_state state)
{
	switch (state) {
	case E_STAG_INVALID:
		return RDMAP_ECODE_INVALID_STAG;
	case E_BASE_BOUNDS:
		return RDMAP_ECODE_BASE_BOUNDS;
	case E_PD_MISMATCH:
		return RDMAP_ECODE_STAG_NOT_ASSOC;
	case E_ACCESS_PERM:
		return RDMAP_ECODE_ACCESS_RIGHTS;
	default:
		return RDMAP_ECODE_UNSPECIFIED;
	}
}

void siw_init_terminate(struct siw_qp *qp, enum term_elayer layer, u8 etype,
			u8 ecode, int in_tx)
{
	if (!qp->term_info.valid) {
		memset(&qp->term_info, 0, sizeof(qp->term_info));
		qp->term_info.layer = layer;
		qp->term_info.etype = etype;
		qp->term_info.ecode = ecode;
		qp->term_info.in_tx = in_tx;
		qp->term_info.valid = 1;
	}
	siw_dbg_qp(qp, "init TERM: layer %d, type %d, code %d, in tx %s\n",
		   layer, etype, ecode, in_tx ? "yes" : "no");
}

/*
 * Send a TERMINATE message, as defined in RFC's 5040/5041/5044/6581.
 * Sending TERMINATE messages is best effort - such messages
 * can only be send if the QP is still connected and it does
 * not have another outbound message in-progress, i.e. the
 * TERMINATE message must not interfer with an incomplete current
 * transmit operation.
 */
void siw_send_terminate(struct siw_qp *qp)
{
	struct kvec iov[3];
	struct msghdr msg = { .msg_flags = MSG_DONTWAIT | MSG_EOR };
	struct iwarp_terminate *term = NULL;
	union iwarp_hdr *err_hdr = NULL;
	struct socket *s = qp->attrs.sk;
	struct siw_rx_stream *srx = &qp->rx_stream;
	union iwarp_hdr *rx_hdr = &srx->hdr;
	u32 crc = 0;
	int num_frags, len_terminate, rv;

	if (!qp->term_info.valid)
		return;

	qp->term_info.valid = 0;

	if (tx_wqe(qp)->wr_status == SIW_WR_INPROGRESS) {
		siw_dbg_qp(qp, "cannot send TERMINATE: op %d in progress\n",
			   tx_type(tx_wqe(qp)));
		return;
	}
	if (!s && qp->cep)
		/* QP not yet in RTS. Take socket from connection end point */
		s = qp->cep->sock;

	if (!s) {
		siw_dbg_qp(qp, "cannot send TERMINATE: not connected\n");
		return;
	}

	term = kzalloc(sizeof(*term), GFP_KERNEL);
	if (!term)
		return;

	term->ddp_qn = cpu_to_be32(RDMAP_UNTAGGED_QN_TERMINATE);
	term->ddp_mo = 0;
	term->ddp_msn = cpu_to_be32(1);

	iov[0].iov_base = term;
	iov[0].iov_len = sizeof(*term);

	if ((qp->term_info.layer == TERM_ERROR_LAYER_DDP) ||
	    ((qp->term_info.layer == TERM_ERROR_LAYER_RDMAP) &&
	     (qp->term_info.etype != RDMAP_ETYPE_CATASTROPHIC))) {
		err_hdr = kzalloc(sizeof(*err_hdr), GFP_KERNEL);
		if (!err_hdr) {
			kfree(term);
			return;
		}
	}
	memcpy(&term->ctrl, &iwarp_pktinfo[RDMAP_TERMINATE].ctrl,
	       sizeof(struct iwarp_ctrl));

	__rdmap_term_set_layer(term, qp->term_info.layer);
	__rdmap_term_set_etype(term, qp->term_info.etype);
	__rdmap_term_set_ecode(term, qp->term_info.ecode);

	switch (qp->term_info.layer) {
	case TERM_ERROR_LAYER_RDMAP:
		if (qp->term_info.etype == RDMAP_ETYPE_CATASTROPHIC)
			/* No additional DDP/RDMAP header to be included */
			break;

		if (qp->term_info.etype == RDMAP_ETYPE_REMOTE_PROTECTION) {
			/*
			 * Complete RDMAP frame will get attached, and
			 * DDP segment length is valid
			 */
			term->flag_m = 1;
			term->flag_d = 1;
			term->flag_r = 1;

			if (qp->term_info.in_tx) {
				struct iwarp_rdma_rreq *rreq;
				struct siw_wqe *wqe = tx_wqe(qp);

				/* Inbound RREQ error, detected during
				 * RRESP creation. Take state from
				 * current TX work queue element to
				 * reconstruct peers RREQ.
				 */
				rreq = (struct iwarp_rdma_rreq *)err_hdr;

				memcpy(&rreq->ctrl,
				       &iwarp_pktinfo[RDMAP_RDMA_READ_REQ].ctrl,
				       sizeof(struct iwarp_ctrl));

				rreq->rsvd = 0;
				rreq->ddp_qn =
					htonl(RDMAP_UNTAGGED_QN_RDMA_READ);

				/* Provide RREQ's MSN as kept aside */
				rreq->ddp_msn = htonl(wqe->sqe.sge[0].length);

				rreq->ddp_mo = htonl(wqe->processed);
				rreq->sink_stag = htonl(wqe->sqe.rkey);
				rreq->sink_to = cpu_to_be64(wqe->sqe.raddr);
				rreq->read_size = htonl(wqe->sqe.sge[0].length);
				rreq->source_stag = htonl(wqe->sqe.sge[0].lkey);
				rreq->source_to =
					cpu_to_be64(wqe->sqe.sge[0].laddr);

				iov[1].iov_base = rreq;
				iov[1].iov_len = sizeof(*rreq);

				rx_hdr = (union iwarp_hdr *)rreq;
			} else {
				/* Take RDMAP/DDP information from
				 * current (failed) inbound frame.
				 */
				iov[1].iov_base = rx_hdr;

				if (__rdmap_get_opcode(&rx_hdr->ctrl) ==
				    RDMAP_RDMA_READ_REQ)
					iov[1].iov_len =
						sizeof(struct iwarp_rdma_rreq);
				else /* SEND type */
					iov[1].iov_len =
						sizeof(struct iwarp_send);
			}
		} else {
			/* Do not report DDP hdr information if packet
			 * layout is unknown
			 */
			if ((qp->term_info.ecode == RDMAP_ECODE_VERSION) ||
			    (qp->term_info.ecode == RDMAP_ECODE_OPCODE))
				break;

			iov[1].iov_base = rx_hdr;

			/* Only DDP frame will get attached */
			if (rx_hdr->ctrl.ddp_rdmap_ctrl & DDP_FLAG_TAGGED)
				iov[1].iov_len =
					sizeof(struct iwarp_rdma_write);
			else
				iov[1].iov_len = sizeof(struct iwarp_send);

			term->flag_m = 1;
			term->flag_d = 1;
		}
		term->ctrl.mpa_len = cpu_to_be16(iov[1].iov_len);
		break;

	case TERM_ERROR_LAYER_DDP:
		/* Report error encountered while DDP processing.
		 * This can only happen as a result of inbound
		 * DDP processing
		 */

		/* Do not report DDP hdr information if packet
		 * layout is unknown
		 */
		if (((qp->term_info.etype == DDP_ETYPE_TAGGED_BUF) &&
		     (qp->term_info.ecode == DDP_ECODE_T_VERSION)) ||
		    ((qp->term_info.etype == DDP_ETYPE_UNTAGGED_BUF) &&
		     (qp->term_info.ecode == DDP_ECODE_UT_VERSION)))
			break;

		iov[1].iov_base = rx_hdr;

		if (rx_hdr->ctrl.ddp_rdmap_ctrl & DDP_FLAG_TAGGED)
			iov[1].iov_len = sizeof(struct iwarp_ctrl_tagged);
		else
			iov[1].iov_len = sizeof(struct iwarp_ctrl_untagged);

		term->flag_m = 1;
		term->flag_d = 1;
		break;

	default:
		break;
	}
	if (term->flag_m || term->flag_d || term->flag_r) {
		iov[2].iov_base = &crc;
		iov[2].iov_len = sizeof(crc);
		len_terminate = sizeof(*term) + iov[1].iov_len + MPA_CRC_SIZE;
		num_frags = 3;
	} else {
		iov[1].iov_base = &crc;
		iov[1].iov_len = sizeof(crc);
		len_terminate = sizeof(*term) + MPA_CRC_SIZE;
		num_frags = 2;
	}

	/* Adjust DDP Segment Length parameter, if valid */
	if (term->flag_m) {
		u32 real_ddp_len = be16_to_cpu(rx_hdr->ctrl.mpa_len);
		enum rdma_opcode op = __rdmap_get_opcode(&rx_hdr->ctrl);

		real_ddp_len -= iwarp_pktinfo[op].hdr_len - MPA_HDR_SIZE;
		rx_hdr->ctrl.mpa_len = cpu_to_be16(real_ddp_len);
	}

	term->ctrl.mpa_len =
		cpu_to_be16(len_terminate - (MPA_HDR_SIZE + MPA_CRC_SIZE));
	if (qp->tx_ctx.mpa_crc_hd) {
		crypto_shash_init(qp->tx_ctx.mpa_crc_hd);
		if (crypto_shash_update(qp->tx_ctx.mpa_crc_hd,
					(u8 *)iov[0].iov_base,
					iov[0].iov_len))
			goto out;

		if (num_frags == 3) {
			if (crypto_shash_update(qp->tx_ctx.mpa_crc_hd,
						(u8 *)iov[1].iov_base,
						iov[1].iov_len))
				goto out;
		}
		crypto_shash_final(qp->tx_ctx.mpa_crc_hd, (u8 *)&crc);
	}

	rv = kernel_sendmsg(s, &msg, iov, num_frags, len_terminate);
	siw_dbg_qp(qp, "sent TERM: %s, layer %d, type %d, code %d (%d bytes)\n",
		   rv == len_terminate ? "success" : "failure",
		   __rdmap_term_layer(term), __rdmap_term_etype(term),
		   __rdmap_term_ecode(term), rv);
out:
	kfree(term);
	kfree(err_hdr);
}

/*
 * Handle all attrs other than state
 */
static void siw_qp_modify_nonstate(struct siw_qp *qp,
				   struct siw_qp_attrs *attrs,
				   enum siw_qp_attr_mask mask)
{
	if (mask & SIW_QP_ATTR_ACCESS_FLAGS) {
		if (attrs->flags & SIW_RDMA_BIND_ENABLED)
			qp->attrs.flags |= SIW_RDMA_BIND_ENABLED;
		else
			qp->attrs.flags &= ~SIW_RDMA_BIND_ENABLED;

		if (attrs->flags & SIW_RDMA_WRITE_ENABLED)
			qp->attrs.flags |= SIW_RDMA_WRITE_ENABLED;
		else
			qp->attrs.flags &= ~SIW_RDMA_WRITE_ENABLED;

		if (attrs->flags & SIW_RDMA_READ_ENABLED)
			qp->attrs.flags |= SIW_RDMA_READ_ENABLED;
		else
			qp->attrs.flags &= ~SIW_RDMA_READ_ENABLED;
	}
}

static int siw_qp_nextstate_from_idle(struct siw_qp *qp,
				      struct siw_qp_attrs *attrs,
				      enum siw_qp_attr_mask mask)
{
	int rv = 0;

	switch (attrs->state) {
	case SIW_QP_STATE_RTS:
		if (attrs->flags & SIW_MPA_CRC) {
			rv = siw_qp_enable_crc(qp);
			if (rv)
				break;
		}
		if (!(mask & SIW_QP_ATTR_LLP_HANDLE)) {
			siw_dbg_qp(qp, "no socket\n");
			rv = -EINVAL;
			break;
		}
		if (!(mask & SIW_QP_ATTR_MPA)) {
			siw_dbg_qp(qp, "no MPA\n");
			rv = -EINVAL;
			break;
		}
		/*
		 * Initialize iWARP TX state
		 */
		qp->tx_ctx.ddp_msn[RDMAP_UNTAGGED_QN_SEND] = 0;
		qp->tx_ctx.ddp_msn[RDMAP_UNTAGGED_QN_RDMA_READ] = 0;
		qp->tx_ctx.ddp_msn[RDMAP_UNTAGGED_QN_TERMINATE] = 0;

		/*
		 * Initialize iWARP RX state
		 */
		qp->rx_stream.ddp_msn[RDMAP_UNTAGGED_QN_SEND] = 1;
		qp->rx_stream.ddp_msn[RDMAP_UNTAGGED_QN_RDMA_READ] = 1;
		qp->rx_stream.ddp_msn[RDMAP_UNTAGGED_QN_TERMINATE] = 1;

		/*
		 * init IRD free queue, caller has already checked
		 * limits.
		 */
		rv = siw_qp_readq_init(qp, attrs->irq_size,
				       attrs->orq_size);
		if (rv)
			break;

		qp->attrs.sk = attrs->sk;
		qp->attrs.state = SIW_QP_STATE_RTS;

		siw_dbg_qp(qp, "enter RTS: crc=%s, ord=%u, ird=%u\n",
			   attrs->flags & SIW_MPA_CRC ? "y" : "n",
			   qp->attrs.orq_size, qp->attrs.irq_size);
		break;

	case SIW_QP_STATE_ERROR:
		siw_rq_flush(qp);
		qp->attrs.state = SIW_QP_STATE_ERROR;
		if (qp->cep) {
			siw_cep_put(qp->cep);
			qp->cep = NULL;
		}
		break;

	default:
		break;
	}
	return rv;
}

static int siw_qp_nextstate_from_rts(struct siw_qp *qp,
				     struct siw_qp_attrs *attrs)
{
	int drop_conn = 0;

	switch (attrs->state) {
	case SIW_QP_STATE_CLOSING:
		/*
		 * Verbs: move to IDLE if SQ and ORQ are empty.
		 * Move to ERROR otherwise. But first of all we must
		 * close the connection. So we keep CLOSING or ERROR
		 * as a transient state, schedule connection drop work
		 * and wait for the socket state change upcall to
		 * come back closed.
		 */
		if (tx_wqe(qp)->wr_status == SIW_WR_IDLE) {
			qp->attrs.state = SIW_QP_STATE_CLOSING;
		} else {
			qp->attrs.state = SIW_QP_STATE_ERROR;
			siw_sq_flush(qp);
		}
		siw_rq_flush(qp);

		drop_conn = 1;
		break;

	case SIW_QP_STATE_TERMINATE:
		qp->attrs.state = SIW_QP_STATE_TERMINATE;

		siw_init_terminate(qp, TERM_ERROR_LAYER_RDMAP,
				   RDMAP_ETYPE_CATASTROPHIC,
				   RDMAP_ECODE_UNSPECIFIED, 1);
		drop_conn = 1;
		break;

	case SIW_QP_STATE_ERROR:
		/*
		 * This is an emergency close.
		 *
		 * Any in progress transmit operation will get
		 * cancelled.
		 * This will likely result in a protocol failure,
		 * if a TX operation is in transit. The caller
		 * could unconditional wait to give the current
		 * operation a chance to complete.
		 * Esp., how to handle the non-empty IRQ case?
		 * The peer was asking for data transfer at a valid
		 * point in time.
		 */
		siw_sq_flush(qp);
		siw_rq_flush(qp);
		qp->attrs.state = SIW_QP_STATE_ERROR;
		drop_conn = 1;
		break;

	default:
		break;
	}
	return drop_conn;
}

static void siw_qp_nextstate_from_term(struct siw_qp *qp,
				       struct siw_qp_attrs *attrs)
{
	switch (attrs->state) {
	case SIW_QP_STATE_ERROR:
		siw_rq_flush(qp);
		qp->attrs.state = SIW_QP_STATE_ERROR;

		if (tx_wqe(qp)->wr_status != SIW_WR_IDLE)
			siw_sq_flush(qp);
		break;

	default:
		break;
	}
}

static int siw_qp_nextstate_from_close(struct siw_qp *qp,
				       struct siw_qp_attrs *attrs)
{
	int rv = 0;

	switch (attrs->state) {
	case SIW_QP_STATE_IDLE:
		WARN_ON(tx_wqe(qp)->wr_status != SIW_WR_IDLE);
		qp->attrs.state = SIW_QP_STATE_IDLE;
		break;

	case SIW_QP_STATE_CLOSING:
		/*
		 * The LLP may already moved the QP to closing
		 * due to graceful peer close init
		 */
		break;

	case SIW_QP_STATE_ERROR:
		/*
		 * QP was moved to CLOSING by LLP event
		 * not yet seen by user.
		 */
		qp->attrs.state = SIW_QP_STATE_ERROR;

		if (tx_wqe(qp)->wr_status != SIW_WR_IDLE)
			siw_sq_flush(qp);

		siw_rq_flush(qp);
		break;

	default:
		siw_dbg_qp(qp, "state transition undefined: %s => %s\n",
			   siw_qp_state_to_string[qp->attrs.state],
			   siw_qp_state_to_string[attrs->state]);

		rv = -ECONNABORTED;
	}
	return rv;
}

/*
 * Caller must hold qp->state_lock
 */
int siw_qp_modify(struct siw_qp *qp, struct siw_qp_attrs *attrs,
		  enum siw_qp_attr_mask mask)
{
	int drop_conn = 0, rv = 0;

	if (!mask)
		return 0;

	siw_dbg_qp(qp, "state: %s => %s\n",
		   siw_qp_state_to_string[qp->attrs.state],
		   siw_qp_state_to_string[attrs->state]);

	if (mask != SIW_QP_ATTR_STATE)
		siw_qp_modify_nonstate(qp, attrs, mask);

	if (!(mask & SIW_QP_ATTR_STATE))
		return 0;

	switch (qp->attrs.state) {
	case SIW_QP_STATE_IDLE:
	case SIW_QP_STATE_RTR:
		rv = siw_qp_nextstate_from_idle(qp, attrs, mask);
		break;

	case SIW_QP_STATE_RTS:
		drop_conn = siw_qp_nextstate_from_rts(qp, attrs);
		break;

	case SIW_QP_STATE_TERMINATE:
		siw_qp_nextstate_from_term(qp, attrs);
		break;

	case SIW_QP_STATE_CLOSING:
		siw_qp_nextstate_from_close(qp, attrs);
		break;
	default:
		break;
	}
	if (drop_conn)
		siw_qp_cm_drop(qp, 0);

	return rv;
}

void siw_read_to_orq(struct siw_sqe *rreq, struct siw_sqe *sqe)
{
	rreq->id = sqe->id;
	rreq->opcode = sqe->opcode;
	rreq->sge[0].laddr = sqe->sge[0].laddr;
	rreq->sge[0].length = sqe->sge[0].length;
	rreq->sge[0].lkey = sqe->sge[0].lkey;
	rreq->sge[1].lkey = sqe->sge[1].lkey;
	rreq->flags = sqe->flags | SIW_WQE_VALID;
	rreq->num_sge = 1;
}

/*
 * Must be called with SQ locked.
 * To avoid complete SQ starvation by constant inbound READ requests,
 * the active IRQ will not be served after qp->irq_burst, if the
 * SQ has pending work.
 */
int siw_activate_tx(struct siw_qp *qp)
{
	struct siw_sqe *irqe, *sqe;
	struct siw_wqe *wqe = tx_wqe(qp);
	int rv = 1;

	irqe = &qp->irq[qp->irq_get % qp->attrs.irq_size];

	if (irqe->flags & SIW_WQE_VALID) {
		sqe = sq_get_next(qp);

		/*
		 * Avoid local WQE processing starvation in case
		 * of constant inbound READ request stream
		 */
		if (sqe && ++qp->irq_burst >= SIW_IRQ_MAXBURST_SQ_ACTIVE) {
			qp->irq_burst = 0;
			goto skip_irq;
		}
		memset(wqe->mem, 0, sizeof(*wqe->mem) * SIW_MAX_SGE);
		wqe->wr_status = SIW_WR_QUEUED;

		/* start READ RESPONSE */
		wqe->sqe.opcode = SIW_OP_READ_RESPONSE;
		wqe->sqe.flags = 0;
		if (irqe->num_sge) {
			wqe->sqe.num_sge = 1;
			wqe->sqe.sge[0].length = irqe->sge[0].length;
			wqe->sqe.sge[0].laddr = irqe->sge[0].laddr;
			wqe->sqe.sge[0].lkey = irqe->sge[0].lkey;
		} else {
			wqe->sqe.num_sge = 0;
		}

		/* Retain original RREQ's message sequence number for
		 * potential error reporting cases.
		 */
		wqe->sqe.sge[1].length = irqe->sge[1].length;

		wqe->sqe.rkey = irqe->rkey;
		wqe->sqe.raddr = irqe->raddr;

		wqe->processed = 0;
		qp->irq_get++;

		/* mark current IRQ entry free */
		smp_store_mb(irqe->flags, 0);

		goto out;
	}
	sqe = sq_get_next(qp);
	if (sqe) {
skip_irq:
		memset(wqe->mem, 0, sizeof(*wqe->mem) * SIW_MAX_SGE);
		wqe->wr_status = SIW_WR_QUEUED;

		/* First copy SQE to kernel private memory */
		memcpy(&wqe->sqe, sqe, sizeof(*sqe));

		if (wqe->sqe.opcode >= SIW_NUM_OPCODES) {
			rv = -EINVAL;
			goto out;
		}
		if (wqe->sqe.flags & SIW_WQE_INLINE) {
			if (wqe->sqe.opcode != SIW_OP_SEND &&
			    wqe->sqe.opcode != SIW_OP_WRITE) {
				rv = -EINVAL;
				goto out;
			}
			if (wqe->sqe.sge[0].length > SIW_MAX_INLINE) {
				rv = -EINVAL;
				goto out;
			}
			wqe->sqe.sge[0].laddr = (u64)&wqe->sqe.sge[1];
			wqe->sqe.sge[0].lkey = 0;
			wqe->sqe.num_sge = 1;
		}
		if (wqe->sqe.flags & SIW_WQE_READ_FENCE) {
			/* A READ cannot be fenced */
			if (unlikely(wqe->sqe.opcode == SIW_OP_READ ||
				     wqe->sqe.opcode ==
					     SIW_OP_READ_LOCAL_INV)) {
				siw_dbg_qp(qp, "cannot fence read\n");
				rv = -EINVAL;
				goto out;
			}
			spin_lock(&qp->orq_lock);

			if (!siw_orq_empty(qp)) {
				qp->tx_ctx.orq_fence = 1;
				rv = 0;
			}
			spin_unlock(&qp->orq_lock);

		} else if (wqe->sqe.opcode == SIW_OP_READ ||
			   wqe->sqe.opcode == SIW_OP_READ_LOCAL_INV) {
			struct siw_sqe *rreq;

			wqe->sqe.num_sge = 1;

			spin_lock(&qp->orq_lock);

			rreq = orq_get_free(qp);
			if (rreq) {
				/*
				 * Make an immediate copy in ORQ to be ready
				 * to process loopback READ reply
				 */
				siw_read_to_orq(rreq, &wqe->sqe);
				qp->orq_put++;
			} else {
				qp->tx_ctx.orq_fence = 1;
				rv = 0;
			}
			spin_unlock(&qp->orq_lock);
		}

		/* Clear SQE, can be re-used by application */
		smp_store_mb(sqe->flags, 0);
		qp->sq_get++;
	} else {
		rv = 0;
	}
out:
	if (unlikely(rv < 0)) {
		siw_dbg_qp(qp, "error %d\n", rv);
		wqe->wr_status = SIW_WR_IDLE;
	}
	return rv;
}

/*
 * Check if current CQ state qualifies for calling CQ completion
 * handler. Must be called with CQ lock held.
 */
static bool siw_cq_notify_now(struct siw_cq *cq, u32 flags)
{
	u64 cq_notify;

	if (!cq->base_cq.comp_handler)
		return false;

	cq_notify = READ_ONCE(*cq->notify);

	if ((cq_notify & SIW_NOTIFY_NEXT_COMPLETION) ||
	    ((cq_notify & SIW_NOTIFY_SOLICITED) &&
	     (flags & SIW_WQE_SOLICITED))) {
		/* dis-arm CQ */
		smp_store_mb(*cq->notify, SIW_NOTIFY_NOT);

		return true;
	}
	return false;
}

int siw_sqe_complete(struct siw_qp *qp, struct siw_sqe *sqe, u32 bytes,
		     enum siw_wc_status status)
{
	struct siw_cq *cq = qp->scq;
	int rv = 0;

	if (cq) {
		u32 sqe_flags = sqe->flags;
		struct siw_cqe *cqe;
		u32 idx;
		unsigned long flags;

		spin_lock_irqsave(&cq->lock, flags);

		idx = cq->cq_put % cq->num_cqe;
		cqe = &cq->queue[idx];

		if (!READ_ONCE(cqe->flags)) {
			bool notify;

			cqe->id = sqe->id;
			cqe->opcode = sqe->opcode;
			cqe->status = status;
			cqe->imm_data = 0;
			cqe->bytes = bytes;

			if (cq->kernel_verbs)
				cqe->base_qp = qp->ib_qp;
			else
				cqe->qp_id = qp_id(qp);

			/* mark CQE valid for application */
			WRITE_ONCE(cqe->flags, SIW_WQE_VALID);
			/* recycle SQE */
			smp_store_mb(sqe->flags, 0);

			cq->cq_put++;
			notify = siw_cq_notify_now(cq, sqe_flags);

			spin_unlock_irqrestore(&cq->lock, flags);

			if (notify) {
				siw_dbg_cq(cq, "Call completion handler\n");
				cq->base_cq.comp_handler(&cq->base_cq,
						cq->base_cq.cq_context);
			}
		} else {
			spin_unlock_irqrestore(&cq->lock, flags);
			rv = -ENOMEM;
			siw_cq_event(cq, IB_EVENT_CQ_ERR);
		}
	} else {
		/* recycle SQE */
		smp_store_mb(sqe->flags, 0);
	}
	return rv;
}

int siw_rqe_complete(struct siw_qp *qp, struct siw_rqe *rqe, u32 bytes,
		     u32 inval_stag, enum siw_wc_status status)
{
	struct siw_cq *cq = qp->rcq;
	int rv = 0;

	if (cq) {
		struct siw_cqe *cqe;
		u32 idx;
		unsigned long flags;

		spin_lock_irqsave(&cq->lock, flags);

		idx = cq->cq_put % cq->num_cqe;
		cqe = &cq->queue[idx];

		if (!READ_ONCE(cqe->flags)) {
			bool notify;
			u8 cqe_flags = SIW_WQE_VALID;

			cqe->id = rqe->id;
			cqe->opcode = SIW_OP_RECEIVE;
			cqe->status = status;
			cqe->imm_data = 0;
			cqe->bytes = bytes;

			if (cq->kernel_verbs) {
				cqe->base_qp = qp->ib_qp;
				if (inval_stag) {
					cqe_flags |= SIW_WQE_REM_INVAL;
					cqe->inval_stag = inval_stag;
				}
			} else {
				cqe->qp_id = qp_id(qp);
			}
			/* mark CQE valid for application */
			WRITE_ONCE(cqe->flags, cqe_flags);
			/* recycle RQE */
			smp_store_mb(rqe->flags, 0);

			cq->cq_put++;
			notify = siw_cq_notify_now(cq, SIW_WQE_SIGNALLED);

			spin_unlock_irqrestore(&cq->lock, flags);

			if (notify) {
				siw_dbg_cq(cq, "Call completion handler\n");
				cq->base_cq.comp_handler(&cq->base_cq,
						cq->base_cq.cq_context);
			}
		} else {
			spin_unlock_irqrestore(&cq->lock, flags);
			rv = -ENOMEM;
			siw_cq_event(cq, IB_EVENT_CQ_ERR);
		}
	} else {
		/* recycle RQE */
		smp_store_mb(rqe->flags, 0);
	}
	return rv;
}

/*
 * siw_sq_flush()
 *
 * Flush SQ and ORRQ entries to CQ.
 *
 * Must be called with QP state write lock held.
 * Therefore, SQ and ORQ lock must not be taken.
 */
void siw_sq_flush(struct siw_qp *qp)
{
	struct siw_sqe *sqe;
	struct siw_wqe *wqe = tx_wqe(qp);
	int async_event = 0;

	/*
	 * Start with completing any work currently on the ORQ
	 */
	while (qp->attrs.orq_size) {
		sqe = &qp->orq[qp->orq_get % qp->attrs.orq_size];
		if (!READ_ONCE(sqe->flags))
			break;

		if (siw_sqe_complete(qp, sqe, 0, SIW_WC_WR_FLUSH_ERR) != 0)
			break;

		WRITE_ONCE(sqe->flags, 0);
		qp->orq_get++;
	}
	/*
	 * Flush an in-progress WQE if present
	 */
	if (wqe->wr_status != SIW_WR_IDLE) {
		siw_dbg_qp(qp, "flush current SQE, type %d, status %d\n",
			   tx_type(wqe), wqe->wr_status);

		siw_wqe_put_mem(wqe, tx_type(wqe));

		if (tx_type(wqe) != SIW_OP_READ_RESPONSE &&
		    ((tx_type(wqe) != SIW_OP_READ &&
		      tx_type(wqe) != SIW_OP_READ_LOCAL_INV) ||
		     wqe->wr_status == SIW_WR_QUEUED))
			/*
			 * An in-progress Read Request is already in
			 * the ORQ
			 */
			siw_sqe_complete(qp, &wqe->sqe, wqe->bytes,
					 SIW_WC_WR_FLUSH_ERR);

		wqe->wr_status = SIW_WR_IDLE;
	}
	/*
	 * Flush the Send Queue
	 */
	while (qp->attrs.sq_size) {
		sqe = &qp->sendq[qp->sq_get % qp->attrs.sq_size];
		if (!READ_ONCE(sqe->flags))
			break;

		async_event = 1;
		if (siw_sqe_complete(qp, sqe, 0, SIW_WC_WR_FLUSH_ERR) != 0)
			/*
			 * Shall IB_EVENT_SQ_DRAINED be supressed if work
			 * completion fails?
			 */
			break;

		WRITE_ONCE(sqe->flags, 0);
		qp->sq_get++;
	}
	if (async_event)
		siw_qp_event(qp, IB_EVENT_SQ_DRAINED);
}

/*
 * siw_rq_flush()
 *
 * Flush recv queue entries to CQ. Also
 * takes care of pending active tagged and untagged
 * inbound transfers, which have target memory
 * referenced.
 *
 * Must be called with QP state write lock held.
 * Therefore, RQ lock must not be taken.
 */
void siw_rq_flush(struct siw_qp *qp)
{
	struct siw_wqe *wqe = &qp->rx_untagged.wqe_active;

	/*
	 * Flush an in-progress untagged operation if present
	 */
	if (wqe->wr_status != SIW_WR_IDLE) {
		siw_dbg_qp(qp, "flush current rqe, type %d, status %d\n",
			   rx_type(wqe), wqe->wr_status);

		siw_wqe_put_mem(wqe, rx_type(wqe));

		if (rx_type(wqe) == SIW_OP_RECEIVE) {
			siw_rqe_complete(qp, &wqe->rqe, wqe->bytes,
					 0, SIW_WC_WR_FLUSH_ERR);
		} else if (rx_type(wqe) != SIW_OP_READ &&
			   rx_type(wqe) != SIW_OP_READ_RESPONSE &&
			   rx_type(wqe) != SIW_OP_WRITE) {
			siw_sqe_complete(qp, &wqe->sqe, 0, SIW_WC_WR_FLUSH_ERR);
		}
		wqe->wr_status = SIW_WR_IDLE;
	}
	wqe = &qp->rx_tagged.wqe_active;

	if (wqe->wr_status != SIW_WR_IDLE) {
		siw_wqe_put_mem(wqe, rx_type(wqe));
		wqe->wr_status = SIW_WR_IDLE;
	}
	/*
	 * Flush the Receive Queue
	 */
	while (qp->attrs.rq_size) {
		struct siw_rqe *rqe =
			&qp->recvq[qp->rq_get % qp->attrs.rq_size];

		if (!READ_ONCE(rqe->flags))
			break;

		if (siw_rqe_complete(qp, rqe, 0, 0, SIW_WC_WR_FLUSH_ERR) != 0)
			break;

		WRITE_ONCE(rqe->flags, 0);
		qp->rq_get++;
	}
}

int siw_qp_add(struct siw_device *sdev, struct siw_qp *qp)
{
	int rv = xa_alloc(&sdev->qp_xa, &qp->ib_qp->qp_num, qp, xa_limit_32b,
			  GFP_KERNEL);

	if (!rv) {
		kref_init(&qp->ref);
		qp->sdev = sdev;
		qp->qp_num = qp->ib_qp->qp_num;
		siw_dbg_qp(qp, "new QP\n");
	}
	return rv;
}

void siw_free_qp(struct kref *ref)
{
	struct siw_qp *found, *qp = container_of(ref, struct siw_qp, ref);
	struct siw_device *sdev = qp->sdev;
	unsigned long flags;

	if (qp->cep)
		siw_cep_put(qp->cep);

	found = xa_erase(&sdev->qp_xa, qp_id(qp));
	WARN_ON(found != qp);
	spin_lock_irqsave(&sdev->lock, flags);
	list_del(&qp->devq);
	spin_unlock_irqrestore(&sdev->lock, flags);

	vfree(qp->sendq);
	vfree(qp->recvq);
	vfree(qp->irq);
	vfree(qp->orq);

	siw_put_tx_cpu(qp->tx_cpu);

	atomic_dec(&sdev->num_qp);
	siw_dbg_qp(qp, "free QP\n");
	kfree_rcu(qp, rcu);
}
