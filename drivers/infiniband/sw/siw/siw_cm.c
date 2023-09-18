// SPDX-License-Identifier: GPL-2.0 or BSD-3-Clause

/* Authors: Bernard Metzler <bmt@zurich.ibm.com> */
/*          Fredy Neeser */
/*          Greg Joyce <greg@opengridcomputing.com> */
/* Copyright (c) 2008-2019, IBM Corporation */
/* Copyright (c) 2017, Open Grid Computing, Inc. */

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/net.h>
#include <linux/inetdevice.h>
#include <net/addrconf.h>
#include <linux/workqueue.h>
#include <net/sock.h>
#include <net/tcp.h>
#include <linux/inet.h>
#include <linux/tcp.h>
#include <trace/events/sock.h>

#include <rdma/iw_cm.h>
#include <rdma/ib_verbs.h>
#include <rdma/ib_user_verbs.h>

#include "siw.h"
#include "siw_cm.h"

/*
 * Set to any combination of
 * MPA_V2_RDMA_NO_RTR, MPA_V2_RDMA_READ_RTR, MPA_V2_RDMA_WRITE_RTR
 */
static __be16 rtr_type = MPA_V2_RDMA_READ_RTR | MPA_V2_RDMA_WRITE_RTR;
static const bool relaxed_ird_negotiation = true;

static void siw_cm_llp_state_change(struct sock *s);
static void siw_cm_llp_data_ready(struct sock *s);
static void siw_cm_llp_write_space(struct sock *s);
static void siw_cm_llp_error_report(struct sock *s);
static int siw_cm_upcall(struct siw_cep *cep, enum iw_cm_event_type reason,
			 int status);

static void siw_sk_assign_cm_upcalls(struct sock *sk)
{
	write_lock_bh(&sk->sk_callback_lock);
	sk->sk_state_change = siw_cm_llp_state_change;
	sk->sk_data_ready = siw_cm_llp_data_ready;
	sk->sk_write_space = siw_cm_llp_write_space;
	sk->sk_error_report = siw_cm_llp_error_report;
	write_unlock_bh(&sk->sk_callback_lock);
}

static void siw_sk_save_upcalls(struct sock *sk)
{
	struct siw_cep *cep = sk_to_cep(sk);

	write_lock_bh(&sk->sk_callback_lock);
	cep->sk_state_change = sk->sk_state_change;
	cep->sk_data_ready = sk->sk_data_ready;
	cep->sk_write_space = sk->sk_write_space;
	cep->sk_error_report = sk->sk_error_report;
	write_unlock_bh(&sk->sk_callback_lock);
}

static void siw_sk_restore_upcalls(struct sock *sk, struct siw_cep *cep)
{
	sk->sk_state_change = cep->sk_state_change;
	sk->sk_data_ready = cep->sk_data_ready;
	sk->sk_write_space = cep->sk_write_space;
	sk->sk_error_report = cep->sk_error_report;
	sk->sk_user_data = NULL;
}

static void siw_qp_socket_assoc(struct siw_cep *cep, struct siw_qp *qp)
{
	struct socket *s = cep->sock;
	struct sock *sk = s->sk;

	write_lock_bh(&sk->sk_callback_lock);

	qp->attrs.sk = s;
	sk->sk_data_ready = siw_qp_llp_data_ready;
	sk->sk_write_space = siw_qp_llp_write_space;

	write_unlock_bh(&sk->sk_callback_lock);
}

static void siw_socket_disassoc(struct socket *s)
{
	struct sock *sk = s->sk;
	struct siw_cep *cep;

	if (sk) {
		write_lock_bh(&sk->sk_callback_lock);
		cep = sk_to_cep(sk);
		if (cep) {
			siw_sk_restore_upcalls(sk, cep);
			siw_cep_put(cep);
		} else {
			pr_warn("siw: cannot restore sk callbacks: no ep\n");
		}
		write_unlock_bh(&sk->sk_callback_lock);
	} else {
		pr_warn("siw: cannot restore sk callbacks: no sk\n");
	}
}

static void siw_rtr_data_ready(struct sock *sk)
{
	struct siw_cep *cep;
	struct siw_qp *qp = NULL;
	read_descriptor_t rd_desc;

	trace_sk_data_ready(sk);

	read_lock(&sk->sk_callback_lock);

	cep = sk_to_cep(sk);
	if (!cep) {
		WARN(1, "No connection endpoint\n");
		goto out;
	}
	qp = sk_to_qp(sk);

	memset(&rd_desc, 0, sizeof(rd_desc));
	rd_desc.arg.data = qp;
	rd_desc.count = 1;

	tcp_read_sock(sk, &rd_desc, siw_tcp_rx_data);
	/*
	 * Check if first frame was successfully processed.
	 * Signal connection full establishment if yes.
	 * Failed data processing would have already scheduled
	 * connection drop.
	 */
	if (!qp->rx_stream.rx_suspend)
		siw_cm_upcall(cep, IW_CM_EVENT_ESTABLISHED, 0);
out:
	read_unlock(&sk->sk_callback_lock);
	if (qp)
		siw_qp_socket_assoc(cep, qp);
}

static void siw_sk_assign_rtr_upcalls(struct siw_cep *cep)
{
	struct sock *sk = cep->sock->sk;

	write_lock_bh(&sk->sk_callback_lock);
	sk->sk_data_ready = siw_rtr_data_ready;
	sk->sk_write_space = siw_qp_llp_write_space;
	write_unlock_bh(&sk->sk_callback_lock);
}

static void siw_cep_socket_assoc(struct siw_cep *cep, struct socket *s)
{
	cep->sock = s;
	siw_cep_get(cep);
	s->sk->sk_user_data = cep;

	siw_sk_save_upcalls(s->sk);
	siw_sk_assign_cm_upcalls(s->sk);
}

static struct siw_cep *siw_cep_alloc(struct siw_device *sdev)
{
	struct siw_cep *cep = kzalloc(sizeof(*cep), GFP_KERNEL);
	unsigned long flags;

	if (!cep)
		return NULL;

	INIT_LIST_HEAD(&cep->listenq);
	INIT_LIST_HEAD(&cep->devq);
	INIT_LIST_HEAD(&cep->work_freelist);

	kref_init(&cep->ref);
	cep->state = SIW_EPSTATE_IDLE;
	init_waitqueue_head(&cep->waitq);
	spin_lock_init(&cep->lock);
	cep->sdev = sdev;
	cep->enhanced_rdma_conn_est = false;

	spin_lock_irqsave(&sdev->lock, flags);
	list_add_tail(&cep->devq, &sdev->cep_list);
	spin_unlock_irqrestore(&sdev->lock, flags);

	siw_dbg_cep(cep, "new endpoint\n");
	return cep;
}

static void siw_cm_free_work(struct siw_cep *cep)
{
	struct list_head *w, *tmp;
	struct siw_cm_work *work;

	list_for_each_safe(w, tmp, &cep->work_freelist) {
		work = list_entry(w, struct siw_cm_work, list);
		list_del(&work->list);
		kfree(work);
	}
}

static void siw_cancel_mpatimer(struct siw_cep *cep)
{
	spin_lock_bh(&cep->lock);
	if (cep->mpa_timer) {
		if (cancel_delayed_work(&cep->mpa_timer->work)) {
			siw_cep_put(cep);
			kfree(cep->mpa_timer); /* not needed again */
		}
		cep->mpa_timer = NULL;
	}
	spin_unlock_bh(&cep->lock);
}

static void siw_put_work(struct siw_cm_work *work)
{
	INIT_LIST_HEAD(&work->list);
	spin_lock_bh(&work->cep->lock);
	list_add(&work->list, &work->cep->work_freelist);
	spin_unlock_bh(&work->cep->lock);
}

static void siw_cep_set_inuse(struct siw_cep *cep)
{
	unsigned long flags;
retry:
	spin_lock_irqsave(&cep->lock, flags);

	if (cep->in_use) {
		spin_unlock_irqrestore(&cep->lock, flags);
		wait_event_interruptible(cep->waitq, !cep->in_use);
		if (signal_pending(current))
			flush_signals(current);
		goto retry;
	} else {
		cep->in_use = 1;
		spin_unlock_irqrestore(&cep->lock, flags);
	}
}

static void siw_cep_set_free(struct siw_cep *cep)
{
	unsigned long flags;

	spin_lock_irqsave(&cep->lock, flags);
	cep->in_use = 0;
	spin_unlock_irqrestore(&cep->lock, flags);

	wake_up(&cep->waitq);
}

static void __siw_cep_dealloc(struct kref *ref)
{
	struct siw_cep *cep = container_of(ref, struct siw_cep, ref);
	struct siw_device *sdev = cep->sdev;
	unsigned long flags;

	WARN_ON(cep->listen_cep);

	/* kfree(NULL) is safe */
	kfree(cep->mpa.pdata);
	spin_lock_bh(&cep->lock);
	if (!list_empty(&cep->work_freelist))
		siw_cm_free_work(cep);
	spin_unlock_bh(&cep->lock);

	spin_lock_irqsave(&sdev->lock, flags);
	list_del(&cep->devq);
	spin_unlock_irqrestore(&sdev->lock, flags);

	siw_dbg_cep(cep, "free endpoint\n");
	kfree(cep);
}

static struct siw_cm_work *siw_get_work(struct siw_cep *cep)
{
	struct siw_cm_work *work = NULL;

	spin_lock_bh(&cep->lock);
	if (!list_empty(&cep->work_freelist)) {
		work = list_entry(cep->work_freelist.next, struct siw_cm_work,
				  list);
		list_del_init(&work->list);
	}
	spin_unlock_bh(&cep->lock);
	return work;
}

static int siw_cm_alloc_work(struct siw_cep *cep, int num)
{
	struct siw_cm_work *work;

	while (num--) {
		work = kmalloc(sizeof(*work), GFP_KERNEL);
		if (!work) {
			if (!(list_empty(&cep->work_freelist)))
				siw_cm_free_work(cep);
			return -ENOMEM;
		}
		work->cep = cep;
		INIT_LIST_HEAD(&work->list);
		list_add(&work->list, &cep->work_freelist);
	}
	return 0;
}

/*
 * siw_cm_upcall()
 *
 * Upcall to IWCM to inform about async connection events
 */
static int siw_cm_upcall(struct siw_cep *cep, enum iw_cm_event_type reason,
			 int status)
{
	struct iw_cm_event event;
	struct iw_cm_id *id;

	memset(&event, 0, sizeof(event));
	event.status = status;
	event.event = reason;

	if (reason == IW_CM_EVENT_CONNECT_REQUEST) {
		event.provider_data = cep;
		id = cep->listen_cep->cm_id;
	} else {
		id = cep->cm_id;
	}
	/* Signal IRD and ORD */
	if (reason == IW_CM_EVENT_ESTABLISHED ||
	    reason == IW_CM_EVENT_CONNECT_REPLY) {
		/* Signal negotiated IRD/ORD values we will use */
		event.ird = cep->ird;
		event.ord = cep->ord;
	} else if (reason == IW_CM_EVENT_CONNECT_REQUEST) {
		event.ird = cep->ord;
		event.ord = cep->ird;
	}
	/* Signal private data and address information */
	if (reason == IW_CM_EVENT_CONNECT_REQUEST ||
	    reason == IW_CM_EVENT_CONNECT_REPLY) {
		u16 pd_len = be16_to_cpu(cep->mpa.hdr.params.pd_len);

		if (pd_len) {
			/*
			 * hand over MPA private data
			 */
			event.private_data_len = pd_len;
			event.private_data = cep->mpa.pdata;

			/* Hide MPA V2 IRD/ORD control */
			if (cep->enhanced_rdma_conn_est) {
				event.private_data_len -=
					sizeof(struct mpa_v2_data);
				event.private_data +=
					sizeof(struct mpa_v2_data);
			}
		}
		getname_local(cep->sock, &event.local_addr);
		getname_peer(cep->sock, &event.remote_addr);
	}
	siw_dbg_cep(cep, "[QP %u]: reason=%d, status=%d\n",
		    cep->qp ? qp_id(cep->qp) : UINT_MAX, reason, status);

	return id->event_handler(id, &event);
}

/*
 * siw_qp_cm_drop()
 *
 * Drops established LLP connection if present and not already
 * scheduled for dropping. Called from user context, SQ workqueue
 * or receive IRQ. Caller signals if socket can be immediately
 * closed (basically, if not in IRQ).
 */
void siw_qp_cm_drop(struct siw_qp *qp, int schedule)
{
	struct siw_cep *cep = qp->cep;

	qp->rx_stream.rx_suspend = 1;
	qp->tx_ctx.tx_suspend = 1;

	if (!qp->cep)
		return;

	if (schedule) {
		siw_cm_queue_work(cep, SIW_CM_WORK_CLOSE_LLP);
	} else {
		siw_cep_set_inuse(cep);

		if (cep->state == SIW_EPSTATE_CLOSED) {
			siw_dbg_cep(cep, "already closed\n");
			goto out;
		}
		siw_dbg_cep(cep, "immediate close, state %d\n", cep->state);

		if (qp->term_info.valid)
			siw_send_terminate(qp);

		if (cep->cm_id) {
			switch (cep->state) {
			case SIW_EPSTATE_AWAIT_MPAREP:
				siw_cm_upcall(cep, IW_CM_EVENT_CONNECT_REPLY,
					      -EINVAL);
				break;

			case SIW_EPSTATE_RDMA_MODE:
				siw_cm_upcall(cep, IW_CM_EVENT_CLOSE, 0);
				break;

			case SIW_EPSTATE_IDLE:
			case SIW_EPSTATE_LISTENING:
			case SIW_EPSTATE_CONNECTING:
			case SIW_EPSTATE_AWAIT_MPAREQ:
			case SIW_EPSTATE_RECVD_MPAREQ:
			case SIW_EPSTATE_CLOSED:
			default:
				break;
			}
			cep->cm_id->rem_ref(cep->cm_id);
			cep->cm_id = NULL;
			siw_cep_put(cep);
		}
		cep->state = SIW_EPSTATE_CLOSED;

		if (cep->sock) {
			siw_socket_disassoc(cep->sock);
			/*
			 * Immediately close socket
			 */
			sock_release(cep->sock);
			cep->sock = NULL;
		}
		if (cep->qp) {
			cep->qp = NULL;
			siw_qp_put(qp);
		}
out:
		siw_cep_set_free(cep);
	}
}

void siw_cep_put(struct siw_cep *cep)
{
	WARN_ON(kref_read(&cep->ref) < 1);
	kref_put(&cep->ref, __siw_cep_dealloc);
}

void siw_cep_get(struct siw_cep *cep)
{
	kref_get(&cep->ref);
}

/*
 * Expects params->pd_len in host byte order
 */
static int siw_send_mpareqrep(struct siw_cep *cep, const void *pdata, u8 pd_len)
{
	struct socket *s = cep->sock;
	struct mpa_rr *rr = &cep->mpa.hdr;
	struct kvec iov[3];
	struct msghdr msg;
	int rv;
	int iovec_num = 0;
	int mpa_len;

	memset(&msg, 0, sizeof(msg));

	iov[iovec_num].iov_base = rr;
	iov[iovec_num].iov_len = sizeof(*rr);
	mpa_len = sizeof(*rr);

	if (cep->enhanced_rdma_conn_est) {
		iovec_num++;
		iov[iovec_num].iov_base = &cep->mpa.v2_ctrl;
		iov[iovec_num].iov_len = sizeof(cep->mpa.v2_ctrl);
		mpa_len += sizeof(cep->mpa.v2_ctrl);
	}
	if (pd_len) {
		iovec_num++;
		iov[iovec_num].iov_base = (char *)pdata;
		iov[iovec_num].iov_len = pd_len;
		mpa_len += pd_len;
	}
	if (cep->enhanced_rdma_conn_est)
		pd_len += sizeof(cep->mpa.v2_ctrl);

	rr->params.pd_len = cpu_to_be16(pd_len);

	rv = kernel_sendmsg(s, &msg, iov, iovec_num + 1, mpa_len);

	return rv < 0 ? rv : 0;
}

/*
 * Receive MPA Request/Reply header.
 *
 * Returns 0 if complete MPA Request/Reply header including
 * eventual private data was received. Returns -EAGAIN if
 * header was partially received or negative error code otherwise.
 *
 * Context: May be called in process context only
 */
static int siw_recv_mpa_rr(struct siw_cep *cep)
{
	struct mpa_rr *hdr = &cep->mpa.hdr;
	struct socket *s = cep->sock;
	u16 pd_len;
	int rcvd, to_rcv;

	if (cep->mpa.bytes_rcvd < sizeof(struct mpa_rr)) {
		rcvd = ksock_recv(s, (char *)hdr + cep->mpa.bytes_rcvd,
				  sizeof(struct mpa_rr) - cep->mpa.bytes_rcvd,
				  0);
		if (rcvd <= 0)
			return -ECONNABORTED;

		cep->mpa.bytes_rcvd += rcvd;

		if (cep->mpa.bytes_rcvd < sizeof(struct mpa_rr))
			return -EAGAIN;

		if (be16_to_cpu(hdr->params.pd_len) > MPA_MAX_PRIVDATA)
			return -EPROTO;
	}
	pd_len = be16_to_cpu(hdr->params.pd_len);

	/*
	 * At least the MPA Request/Reply header (frame not including
	 * private data) has been received.
	 * Receive (or continue receiving) any private data.
	 */
	to_rcv = pd_len - (cep->mpa.bytes_rcvd - sizeof(struct mpa_rr));

	if (!to_rcv) {
		/*
		 * We must have hdr->params.pd_len == 0 and thus received a
		 * complete MPA Request/Reply frame.
		 * Check against peer protocol violation.
		 */
		u32 word;

		rcvd = ksock_recv(s, (char *)&word, sizeof(word), MSG_DONTWAIT);
		if (rcvd == -EAGAIN)
			return 0;

		if (rcvd == 0) {
			siw_dbg_cep(cep, "peer EOF\n");
			return -EPIPE;
		}
		if (rcvd < 0) {
			siw_dbg_cep(cep, "error: %d\n", rcvd);
			return rcvd;
		}
		siw_dbg_cep(cep, "peer sent extra data: %d\n", rcvd);

		return -EPROTO;
	}

	/*
	 * At this point, we must have hdr->params.pd_len != 0.
	 * A private data buffer gets allocated if hdr->params.pd_len != 0.
	 */
	if (!cep->mpa.pdata) {
		cep->mpa.pdata = kmalloc(pd_len + 4, GFP_KERNEL);
		if (!cep->mpa.pdata)
			return -ENOMEM;
	}
	rcvd = ksock_recv(
		s, cep->mpa.pdata + cep->mpa.bytes_rcvd - sizeof(struct mpa_rr),
		to_rcv + 4, MSG_DONTWAIT);

	if (rcvd < 0)
		return rcvd;

	if (rcvd > to_rcv)
		return -EPROTO;

	cep->mpa.bytes_rcvd += rcvd;

	if (to_rcv == rcvd) {
		siw_dbg_cep(cep, "%d bytes private data received\n", pd_len);
		return 0;
	}
	return -EAGAIN;
}

/*
 * siw_proc_mpareq()
 *
 * Read MPA Request from socket and signal new connection to IWCM
 * if success. Caller must hold lock on corresponding listening CEP.
 */
static int siw_proc_mpareq(struct siw_cep *cep)
{
	struct mpa_rr *req;
	int version, rv;
	u16 pd_len;

	rv = siw_recv_mpa_rr(cep);
	if (rv)
		return rv;

	req = &cep->mpa.hdr;

	version = __mpa_rr_revision(req->params.bits);
	pd_len = be16_to_cpu(req->params.pd_len);

	if (version > MPA_REVISION_2)
		/* allow for 0, 1, and 2 only */
		return -EPROTO;

	if (memcmp(req->key, MPA_KEY_REQ, 16))
		return -EPROTO;

	/* Prepare for sending MPA reply */
	memcpy(req->key, MPA_KEY_REP, 16);

	if (version == MPA_REVISION_2 &&
	    (req->params.bits & MPA_RR_FLAG_ENHANCED)) {
		/*
		 * MPA version 2 must signal IRD/ORD values and P2P mode
		 * in private data if header flag MPA_RR_FLAG_ENHANCED
		 * is set.
		 */
		if (pd_len < sizeof(struct mpa_v2_data))
			goto reject_conn;

		cep->enhanced_rdma_conn_est = true;
	}

	/* MPA Markers: currently not supported. Marker TX to be added. */
	if (req->params.bits & MPA_RR_FLAG_MARKERS)
		goto reject_conn;

	if (req->params.bits & MPA_RR_FLAG_CRC) {
		/*
		 * RFC 5044, page 27: CRC MUST be used if peer requests it.
		 * siw specific: 'mpa_crc_strict' parameter to reject
		 * connection with CRC if local CRC off enforced by
		 * 'mpa_crc_strict' module parameter.
		 */
		if (!mpa_crc_required && mpa_crc_strict)
			goto reject_conn;

		/* Enable CRC if requested by module parameter */
		if (mpa_crc_required)
			req->params.bits |= MPA_RR_FLAG_CRC;
	}
	if (cep->enhanced_rdma_conn_est) {
		struct mpa_v2_data *v2 = (struct mpa_v2_data *)cep->mpa.pdata;

		/*
		 * Peer requested ORD becomes requested local IRD,
		 * peer requested IRD becomes requested local ORD.
		 * IRD and ORD get limited by global maximum values.
		 */
		cep->ord = ntohs(v2->ird) & MPA_IRD_ORD_MASK;
		cep->ord = min(cep->ord, SIW_MAX_ORD_QP);
		cep->ird = ntohs(v2->ord) & MPA_IRD_ORD_MASK;
		cep->ird = min(cep->ird, SIW_MAX_IRD_QP);

		/* May get overwritten by locally negotiated values */
		cep->mpa.v2_ctrl.ird = htons(cep->ird);
		cep->mpa.v2_ctrl.ord = htons(cep->ord);

		/*
		 * Support for peer sent zero length Write or Read to
		 * let local side enter RTS. Writes are preferred.
		 * Sends would require pre-posting a Receive and are
		 * not supported.
		 * Propose zero length Write if none of Read and Write
		 * is indicated.
		 */
		if (v2->ird & MPA_V2_PEER_TO_PEER) {
			cep->mpa.v2_ctrl.ird |= MPA_V2_PEER_TO_PEER;

			if (v2->ord & MPA_V2_RDMA_WRITE_RTR)
				cep->mpa.v2_ctrl.ord |= MPA_V2_RDMA_WRITE_RTR;
			else if (v2->ord & MPA_V2_RDMA_READ_RTR)
				cep->mpa.v2_ctrl.ord |= MPA_V2_RDMA_READ_RTR;
			else
				cep->mpa.v2_ctrl.ord |= MPA_V2_RDMA_WRITE_RTR;
		}
	}

	cep->state = SIW_EPSTATE_RECVD_MPAREQ;

	/* Keep reference until IWCM accepts/rejects */
	siw_cep_get(cep);
	rv = siw_cm_upcall(cep, IW_CM_EVENT_CONNECT_REQUEST, 0);
	if (rv)
		siw_cep_put(cep);

	return rv;

reject_conn:
	siw_dbg_cep(cep, "reject: crc %d:%d:%d, m %d:%d\n",
		    req->params.bits & MPA_RR_FLAG_CRC ? 1 : 0,
		    mpa_crc_required, mpa_crc_strict,
		    req->params.bits & MPA_RR_FLAG_MARKERS ? 1 : 0, 0);

	req->params.bits &= ~MPA_RR_FLAG_MARKERS;
	req->params.bits |= MPA_RR_FLAG_REJECT;

	if (!mpa_crc_required && mpa_crc_strict)
		req->params.bits &= ~MPA_RR_FLAG_CRC;

	if (pd_len)
		kfree(cep->mpa.pdata);

	cep->mpa.pdata = NULL;

	siw_send_mpareqrep(cep, NULL, 0);

	return -EOPNOTSUPP;
}

static int siw_proc_mpareply(struct siw_cep *cep)
{
	struct siw_qp_attrs qp_attrs;
	enum siw_qp_attr_mask qp_attr_mask;
	struct siw_qp *qp = cep->qp;
	struct mpa_rr *rep;
	int rv;
	u16 rep_ord;
	u16 rep_ird;
	bool ird_insufficient = false;
	enum mpa_v2_ctrl mpa_p2p_mode = MPA_V2_RDMA_NO_RTR;

	rv = siw_recv_mpa_rr(cep);
	if (rv)
		goto out_err;

	siw_cancel_mpatimer(cep);

	rep = &cep->mpa.hdr;

	if (__mpa_rr_revision(rep->params.bits) > MPA_REVISION_2) {
		/* allow for 0, 1,  and 2 only */
		rv = -EPROTO;
		goto out_err;
	}
	if (memcmp(rep->key, MPA_KEY_REP, 16)) {
		siw_init_terminate(qp, TERM_ERROR_LAYER_LLP, LLP_ETYPE_MPA,
				   LLP_ECODE_INVALID_REQ_RESP, 0);
		siw_send_terminate(qp);
		rv = -EPROTO;
		goto out_err;
	}
	if (rep->params.bits & MPA_RR_FLAG_REJECT) {
		siw_dbg_cep(cep, "got mpa reject\n");
		siw_cm_upcall(cep, IW_CM_EVENT_CONNECT_REPLY, -ECONNRESET);

		return -ECONNRESET;
	}
	if (try_gso && rep->params.bits & MPA_RR_FLAG_GSO_EXP) {
		siw_dbg_cep(cep, "peer allows GSO on TX\n");
		qp->tx_ctx.gso_seg_limit = 0;
	}
	if ((rep->params.bits & MPA_RR_FLAG_MARKERS) ||
	    (mpa_crc_required && !(rep->params.bits & MPA_RR_FLAG_CRC)) ||
	    (mpa_crc_strict && !mpa_crc_required &&
	     (rep->params.bits & MPA_RR_FLAG_CRC))) {
		siw_dbg_cep(cep, "reply unsupp: crc %d:%d:%d, m %d:%d\n",
			    rep->params.bits & MPA_RR_FLAG_CRC ? 1 : 0,
			    mpa_crc_required, mpa_crc_strict,
			    rep->params.bits & MPA_RR_FLAG_MARKERS ? 1 : 0, 0);

		siw_cm_upcall(cep, IW_CM_EVENT_CONNECT_REPLY, -ECONNREFUSED);

		return -EINVAL;
	}
	if (cep->enhanced_rdma_conn_est) {
		struct mpa_v2_data *v2;

		if (__mpa_rr_revision(rep->params.bits) < MPA_REVISION_2 ||
		    !(rep->params.bits & MPA_RR_FLAG_ENHANCED)) {
			/*
			 * Protocol failure: The responder MUST reply with
			 * MPA version 2 and MUST set MPA_RR_FLAG_ENHANCED.
			 */
			siw_dbg_cep(cep, "mpa reply error: vers %d, enhcd %d\n",
				    __mpa_rr_revision(rep->params.bits),
				    rep->params.bits & MPA_RR_FLAG_ENHANCED ?
					    1 :
					    0);

			siw_cm_upcall(cep, IW_CM_EVENT_CONNECT_REPLY,
				      -ECONNRESET);
			return -EINVAL;
		}
		v2 = (struct mpa_v2_data *)cep->mpa.pdata;
		rep_ird = ntohs(v2->ird) & MPA_IRD_ORD_MASK;
		rep_ord = ntohs(v2->ord) & MPA_IRD_ORD_MASK;

		if (cep->ird < rep_ord &&
		    (relaxed_ird_negotiation == false ||
		     rep_ord > cep->sdev->attrs.max_ird)) {
			siw_dbg_cep(cep, "ird %d, rep_ord %d, max_ord %d\n",
				    cep->ird, rep_ord,
				    cep->sdev->attrs.max_ord);
			ird_insufficient = true;
		}
		if (cep->ord > rep_ird && relaxed_ird_negotiation == false) {
			siw_dbg_cep(cep, "ord %d, rep_ird %d\n", cep->ord,
				    rep_ird);
			ird_insufficient = true;
		}
		/*
		 * Always report negotiated peer values to user,
		 * even if IRD/ORD negotiation failed
		 */
		cep->ird = rep_ord;
		cep->ord = rep_ird;

		if (ird_insufficient) {
			/*
			 * If the initiator IRD is insuffient for the
			 * responder ORD, send a TERM.
			 */
			siw_init_terminate(qp, TERM_ERROR_LAYER_LLP,
					   LLP_ETYPE_MPA,
					   LLP_ECODE_INSUFFICIENT_IRD, 0);
			siw_send_terminate(qp);
			rv = -ENOMEM;
			goto out_err;
		}
		if (cep->mpa.v2_ctrl_req.ird & MPA_V2_PEER_TO_PEER)
			mpa_p2p_mode =
				cep->mpa.v2_ctrl_req.ord &
				(MPA_V2_RDMA_WRITE_RTR | MPA_V2_RDMA_READ_RTR);

		/*
		 * Check if we requested P2P mode, and if peer agrees
		 */
		if (mpa_p2p_mode != MPA_V2_RDMA_NO_RTR) {
			if ((mpa_p2p_mode & v2->ord) == 0) {
				/*
				 * We requested RTR mode(s), but the peer
				 * did not pick any mode we support.
				 */
				siw_dbg_cep(cep,
					    "rtr mode:  req %2x, got %2x\n",
					    mpa_p2p_mode,
					    v2->ord & (MPA_V2_RDMA_WRITE_RTR |
						       MPA_V2_RDMA_READ_RTR));

				siw_init_terminate(qp, TERM_ERROR_LAYER_LLP,
						   LLP_ETYPE_MPA,
						   LLP_ECODE_NO_MATCHING_RTR,
						   0);
				siw_send_terminate(qp);
				rv = -EPROTO;
				goto out_err;
			}
			mpa_p2p_mode = v2->ord & (MPA_V2_RDMA_WRITE_RTR |
						  MPA_V2_RDMA_READ_RTR);
		}
	}
	memset(&qp_attrs, 0, sizeof(qp_attrs));

	if (rep->params.bits & MPA_RR_FLAG_CRC)
		qp_attrs.flags = SIW_MPA_CRC;

	qp_attrs.irq_size = cep->ird;
	qp_attrs.orq_size = cep->ord;
	qp_attrs.sk = cep->sock;
	qp_attrs.state = SIW_QP_STATE_RTS;

	qp_attr_mask = SIW_QP_ATTR_STATE | SIW_QP_ATTR_LLP_HANDLE |
		       SIW_QP_ATTR_ORD | SIW_QP_ATTR_IRD | SIW_QP_ATTR_MPA;

	/* Move socket RX/TX under QP control */
	down_write(&qp->state_lock);
	if (qp->attrs.state > SIW_QP_STATE_RTR) {
		rv = -EINVAL;
		up_write(&qp->state_lock);
		goto out_err;
	}
	rv = siw_qp_modify(qp, &qp_attrs, qp_attr_mask);

	siw_qp_socket_assoc(cep, qp);

	up_write(&qp->state_lock);

	/* Send extra RDMA frame to trigger peer RTS if negotiated */
	if (mpa_p2p_mode != MPA_V2_RDMA_NO_RTR) {
		rv = siw_qp_mpa_rts(qp, mpa_p2p_mode);
		if (rv)
			goto out_err;
	}
	if (!rv) {
		rv = siw_cm_upcall(cep, IW_CM_EVENT_CONNECT_REPLY, 0);
		if (!rv)
			cep->state = SIW_EPSTATE_RDMA_MODE;

		return 0;
	}

out_err:
	if (rv != -EAGAIN)
		siw_cm_upcall(cep, IW_CM_EVENT_CONNECT_REPLY, -EINVAL);

	return rv;
}

/*
 * siw_accept_newconn - accept an incoming pending connection
 *
 */
static void siw_accept_newconn(struct siw_cep *cep)
{
	struct socket *s = cep->sock;
	struct socket *new_s = NULL;
	struct siw_cep *new_cep = NULL;
	int rv = 0; /* debug only. should disappear */

	if (cep->state != SIW_EPSTATE_LISTENING)
		goto error;

	new_cep = siw_cep_alloc(cep->sdev);
	if (!new_cep)
		goto error;

	/*
	 * 4: Allocate a sufficient number of work elements
	 * to allow concurrent handling of local + peer close
	 * events, MPA header processing + MPA timeout.
	 */
	if (siw_cm_alloc_work(new_cep, 4) != 0)
		goto error;

	/*
	 * Copy saved socket callbacks from listening CEP
	 * and assign new socket with new CEP
	 */
	new_cep->sk_state_change = cep->sk_state_change;
	new_cep->sk_data_ready = cep->sk_data_ready;
	new_cep->sk_write_space = cep->sk_write_space;
	new_cep->sk_error_report = cep->sk_error_report;

	rv = kernel_accept(s, &new_s, O_NONBLOCK);
	if (rv != 0) {
		/*
		 * Connection already aborted by peer..?
		 */
		siw_dbg_cep(cep, "kernel_accept() error: %d\n", rv);
		goto error;
	}
	new_cep->sock = new_s;
	siw_cep_get(new_cep);
	new_s->sk->sk_user_data = new_cep;

	if (siw_tcp_nagle == false)
		tcp_sock_set_nodelay(new_s->sk);
	new_cep->state = SIW_EPSTATE_AWAIT_MPAREQ;

	rv = siw_cm_queue_work(new_cep, SIW_CM_WORK_MPATIMEOUT);
	if (rv)
		goto error;
	/*
	 * See siw_proc_mpareq() etc. for the use of new_cep->listen_cep.
	 */
	new_cep->listen_cep = cep;
	siw_cep_get(cep);

	if (atomic_read(&new_s->sk->sk_rmem_alloc)) {
		/*
		 * MPA REQ already queued
		 */
		siw_dbg_cep(cep, "immediate mpa request\n");

		siw_cep_set_inuse(new_cep);
		rv = siw_proc_mpareq(new_cep);
		if (rv != -EAGAIN) {
			siw_cep_put(cep);
			new_cep->listen_cep = NULL;
			if (rv) {
				siw_cep_set_free(new_cep);
				goto error;
			}
		}
		siw_cep_set_free(new_cep);
	}
	return;

error:
	if (new_cep)
		siw_cep_put(new_cep);

	if (new_s) {
		siw_socket_disassoc(new_s);
		sock_release(new_s);
		new_cep->sock = NULL;
	}
	siw_dbg_cep(cep, "error %d\n", rv);
}

static void siw_cm_work_handler(struct work_struct *w)
{
	struct siw_cm_work *work;
	struct siw_cep *cep;
	int release_cep = 0, rv = 0;

	work = container_of(w, struct siw_cm_work, work.work);
	cep = work->cep;

	siw_dbg_cep(cep, "[QP %u]: work type: %d, state %d\n",
		    cep->qp ? qp_id(cep->qp) : UINT_MAX,
		    work->type, cep->state);

	siw_cep_set_inuse(cep);

	switch (work->type) {
	case SIW_CM_WORK_ACCEPT:
		siw_accept_newconn(cep);
		break;

	case SIW_CM_WORK_READ_MPAHDR:
		if (cep->state == SIW_EPSTATE_AWAIT_MPAREQ) {
			if (cep->listen_cep) {
				siw_cep_set_inuse(cep->listen_cep);

				if (cep->listen_cep->state ==
				    SIW_EPSTATE_LISTENING)
					rv = siw_proc_mpareq(cep);
				else
					rv = -EFAULT;

				siw_cep_set_free(cep->listen_cep);

				if (rv != -EAGAIN) {
					siw_cep_put(cep->listen_cep);
					cep->listen_cep = NULL;
					if (rv)
						siw_cep_put(cep);
				}
			}
		} else if (cep->state == SIW_EPSTATE_AWAIT_MPAREP) {
			rv = siw_proc_mpareply(cep);
		} else {
			/*
			 * CEP already moved out of MPA handshake.
			 * any connection management already done.
			 * silently ignore the mpa packet.
			 */
			if (cep->state == SIW_EPSTATE_RDMA_MODE) {
				cep->sock->sk->sk_data_ready(cep->sock->sk);
				siw_dbg_cep(cep, "already in RDMA mode");
			} else {
				siw_dbg_cep(cep, "out of state: %d\n",
					    cep->state);
			}
		}
		if (rv && rv != -EAGAIN)
			release_cep = 1;
		break;

	case SIW_CM_WORK_CLOSE_LLP:
		/*
		 * QP scheduled LLP close
		 */
		if (cep->qp && cep->qp->term_info.valid)
			siw_send_terminate(cep->qp);

		if (cep->cm_id)
			siw_cm_upcall(cep, IW_CM_EVENT_CLOSE, 0);

		release_cep = 1;
		break;

	case SIW_CM_WORK_PEER_CLOSE:
		if (cep->cm_id) {
			if (cep->state == SIW_EPSTATE_AWAIT_MPAREP) {
				/*
				 * MPA reply not received, but connection drop
				 */
				siw_cm_upcall(cep, IW_CM_EVENT_CONNECT_REPLY,
					      -ECONNRESET);
			} else if (cep->state == SIW_EPSTATE_RDMA_MODE) {
				/*
				 * NOTE: IW_CM_EVENT_DISCONNECT is given just
				 *       to transition IWCM into CLOSING.
				 */
				siw_cm_upcall(cep, IW_CM_EVENT_DISCONNECT, 0);
				siw_cm_upcall(cep, IW_CM_EVENT_CLOSE, 0);
			}
			/*
			 * for other states there is no connection
			 * known to the IWCM.
			 */
		} else {
			if (cep->state == SIW_EPSTATE_RECVD_MPAREQ) {
				/*
				 * Wait for the ulp/CM to call accept/reject
				 */
				siw_dbg_cep(cep,
					    "mpa req recvd, wait for ULP\n");
			} else if (cep->state == SIW_EPSTATE_AWAIT_MPAREQ) {
				/*
				 * Socket close before MPA request received.
				 */
				siw_dbg_cep(cep, "no mpareq: drop listener\n");
				siw_cep_put(cep->listen_cep);
				cep->listen_cep = NULL;
			}
		}
		release_cep = 1;
		break;

	case SIW_CM_WORK_MPATIMEOUT:
		cep->mpa_timer = NULL;

		if (cep->state == SIW_EPSTATE_AWAIT_MPAREP) {
			/*
			 * MPA request timed out:
			 * Hide any partially received private data and signal
			 * timeout
			 */
			cep->mpa.hdr.params.pd_len = 0;

			if (cep->cm_id)
				siw_cm_upcall(cep, IW_CM_EVENT_CONNECT_REPLY,
					      -ETIMEDOUT);
			release_cep = 1;

		} else if (cep->state == SIW_EPSTATE_AWAIT_MPAREQ) {
			/*
			 * No MPA request received after peer TCP stream setup.
			 */
			if (cep->listen_cep) {
				siw_cep_put(cep->listen_cep);
				cep->listen_cep = NULL;
			}
			release_cep = 1;
		}
		break;

	default:
		WARN(1, "Undefined CM work type: %d\n", work->type);
	}
	if (release_cep) {
		siw_dbg_cep(cep,
			    "release: timer=%s, QP[%u]\n",
			    cep->mpa_timer ? "y" : "n",
			    cep->qp ? qp_id(cep->qp) : UINT_MAX);

		siw_cancel_mpatimer(cep);

		cep->state = SIW_EPSTATE_CLOSED;

		if (cep->qp) {
			struct siw_qp *qp = cep->qp;
			/*
			 * Serialize a potential race with application
			 * closing the QP and calling siw_qp_cm_drop()
			 */
			siw_qp_get(qp);
			siw_cep_set_free(cep);

			siw_qp_llp_close(qp);
			siw_qp_put(qp);

			siw_cep_set_inuse(cep);
			cep->qp = NULL;
			siw_qp_put(qp);
		}
		if (cep->sock) {
			siw_socket_disassoc(cep->sock);
			sock_release(cep->sock);
			cep->sock = NULL;
		}
		if (cep->cm_id) {
			cep->cm_id->rem_ref(cep->cm_id);
			cep->cm_id = NULL;
			siw_cep_put(cep);
		}
	}
	siw_cep_set_free(cep);
	siw_put_work(work);
	siw_cep_put(cep);
}

static struct workqueue_struct *siw_cm_wq;

int siw_cm_queue_work(struct siw_cep *cep, enum siw_work_type type)
{
	struct siw_cm_work *work = siw_get_work(cep);
	unsigned long delay = 0;

	if (!work) {
		siw_dbg_cep(cep, "failed with no work available\n");
		return -ENOMEM;
	}
	work->type = type;
	work->cep = cep;

	siw_cep_get(cep);

	INIT_DELAYED_WORK(&work->work, siw_cm_work_handler);

	if (type == SIW_CM_WORK_MPATIMEOUT) {
		cep->mpa_timer = work;

		if (cep->state == SIW_EPSTATE_AWAIT_MPAREP)
			delay = MPAREQ_TIMEOUT;
		else
			delay = MPAREP_TIMEOUT;
	}
	siw_dbg_cep(cep, "[QP %u]: work type: %d, timeout %lu\n",
		    cep->qp ? qp_id(cep->qp) : -1, type, delay);

	queue_delayed_work(siw_cm_wq, &work->work, delay);

	return 0;
}

static void siw_cm_llp_data_ready(struct sock *sk)
{
	struct siw_cep *cep;

	trace_sk_data_ready(sk);

	read_lock(&sk->sk_callback_lock);

	cep = sk_to_cep(sk);
	if (!cep)
		goto out;

	siw_dbg_cep(cep, "state: %d\n", cep->state);

	switch (cep->state) {
	case SIW_EPSTATE_RDMA_MODE:
	case SIW_EPSTATE_LISTENING:
		break;

	case SIW_EPSTATE_AWAIT_MPAREQ:
	case SIW_EPSTATE_AWAIT_MPAREP:
		siw_cm_queue_work(cep, SIW_CM_WORK_READ_MPAHDR);
		break;

	default:
		siw_dbg_cep(cep, "unexpected data, state %d\n", cep->state);
		break;
	}
out:
	read_unlock(&sk->sk_callback_lock);
}

static void siw_cm_llp_write_space(struct sock *sk)
{
	struct siw_cep *cep = sk_to_cep(sk);

	if (cep)
		siw_dbg_cep(cep, "state: %d\n", cep->state);
}

static void siw_cm_llp_error_report(struct sock *sk)
{
	struct siw_cep *cep = sk_to_cep(sk);

	if (cep) {
		siw_dbg_cep(cep, "error %d, socket state: %d, cep state: %d\n",
			    sk->sk_err, sk->sk_state, cep->state);
		cep->sk_error_report(sk);
	}
}

static void siw_cm_llp_state_change(struct sock *sk)
{
	struct siw_cep *cep;
	void (*orig_state_change)(struct sock *s);

	read_lock(&sk->sk_callback_lock);

	cep = sk_to_cep(sk);
	if (!cep) {
		/* endpoint already disassociated */
		read_unlock(&sk->sk_callback_lock);
		return;
	}
	orig_state_change = cep->sk_state_change;

	siw_dbg_cep(cep, "state: %d\n", cep->state);

	switch (sk->sk_state) {
	case TCP_ESTABLISHED:
		/*
		 * handle accepting socket as special case where only
		 * new connection is possible
		 */
		siw_cm_queue_work(cep, SIW_CM_WORK_ACCEPT);
		break;

	case TCP_CLOSE:
	case TCP_CLOSE_WAIT:
		if (cep->qp)
			cep->qp->tx_ctx.tx_suspend = 1;
		siw_cm_queue_work(cep, SIW_CM_WORK_PEER_CLOSE);
		break;

	default:
		siw_dbg_cep(cep, "unexpected socket state %d\n", sk->sk_state);
	}
	read_unlock(&sk->sk_callback_lock);
	orig_state_change(sk);
}

static int kernel_bindconnect(struct socket *s, struct sockaddr *laddr,
			      struct sockaddr *raddr, bool afonly)
{
	int rv, flags = 0;
	size_t size = laddr->sa_family == AF_INET ?
		sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);

	/*
	 * Make address available again asap.
	 */
	sock_set_reuseaddr(s->sk);

	if (afonly) {
		rv = ip6_sock_set_v6only(s->sk);
		if (rv)
			return rv;
	}

	rv = s->ops->bind(s, laddr, size);
	if (rv < 0)
		return rv;

	rv = s->ops->connect(s, raddr, size, flags);

	return rv < 0 ? rv : 0;
}

int siw_connect(struct iw_cm_id *id, struct iw_cm_conn_param *params)
{
	struct siw_device *sdev = to_siw_dev(id->device);
	struct siw_qp *qp;
	struct siw_cep *cep = NULL;
	struct socket *s = NULL;
	struct sockaddr *laddr = (struct sockaddr *)&id->local_addr,
			*raddr = (struct sockaddr *)&id->remote_addr;
	bool p2p_mode = peer_to_peer, v4 = true;
	u16 pd_len = params->private_data_len;
	int version = mpa_version, rv;

	if (pd_len > MPA_MAX_PRIVDATA)
		return -EINVAL;

	if (params->ird > sdev->attrs.max_ird ||
	    params->ord > sdev->attrs.max_ord)
		return -ENOMEM;

	if (laddr->sa_family == AF_INET6)
		v4 = false;
	else if (laddr->sa_family != AF_INET)
		return -EAFNOSUPPORT;

	/*
	 * Respect any iwarp port mapping: Use mapped remote address
	 * if valid. Local address must not be mapped, since siw
	 * uses kernel TCP stack.
	 */
	if ((v4 && to_sockaddr_in(id->remote_addr).sin_port != 0) ||
	     to_sockaddr_in6(id->remote_addr).sin6_port != 0)
		raddr = (struct sockaddr *)&id->m_remote_addr;

	qp = siw_qp_id2obj(sdev, params->qpn);
	if (!qp) {
		WARN(1, "[QP %u] does not exist\n", params->qpn);
		rv = -EINVAL;
		goto error;
	}
	siw_dbg_qp(qp, "pd_len %d, laddr %pISp, raddr %pISp\n", pd_len, laddr,
		   raddr);

	rv = sock_create(v4 ? AF_INET : AF_INET6, SOCK_STREAM, IPPROTO_TCP, &s);
	if (rv < 0)
		goto error;

	/*
	 * NOTE: For simplification, connect() is called in blocking
	 * mode. Might be reconsidered for async connection setup at
	 * TCP level.
	 */
	rv = kernel_bindconnect(s, laddr, raddr, id->afonly);
	if (rv != 0) {
		siw_dbg_qp(qp, "kernel_bindconnect: error %d\n", rv);
		goto error;
	}
	if (siw_tcp_nagle == false)
		tcp_sock_set_nodelay(s->sk);
	cep = siw_cep_alloc(sdev);
	if (!cep) {
		rv = -ENOMEM;
		goto error;
	}
	siw_cep_set_inuse(cep);

	/* Associate QP with CEP */
	siw_cep_get(cep);
	qp->cep = cep;

	/* siw_qp_get(qp) already done by QP lookup */
	cep->qp = qp;

	id->add_ref(id);
	cep->cm_id = id;

	/*
	 * 4: Allocate a sufficient number of work elements
	 * to allow concurrent handling of local + peer close
	 * events, MPA header processing + MPA timeout.
	 */
	rv = siw_cm_alloc_work(cep, 4);
	if (rv != 0) {
		rv = -ENOMEM;
		goto error;
	}
	cep->ird = params->ird;
	cep->ord = params->ord;

	if (p2p_mode && cep->ord == 0)
		cep->ord = 1;

	cep->state = SIW_EPSTATE_CONNECTING;

	/*
	 * Associate CEP with socket
	 */
	siw_cep_socket_assoc(cep, s);

	cep->state = SIW_EPSTATE_AWAIT_MPAREP;

	/*
	 * Set MPA Request bits: CRC if required, no MPA Markers,
	 * MPA Rev. according to module parameter 'mpa_version', Key 'Request'.
	 */
	cep->mpa.hdr.params.bits = 0;
	if (version > MPA_REVISION_2) {
		pr_warn("Setting MPA version to %u\n", MPA_REVISION_2);
		version = MPA_REVISION_2;
		/* Adjust also module parameter */
		mpa_version = MPA_REVISION_2;
	}
	__mpa_rr_set_revision(&cep->mpa.hdr.params.bits, version);

	if (try_gso)
		cep->mpa.hdr.params.bits |= MPA_RR_FLAG_GSO_EXP;

	if (mpa_crc_required)
		cep->mpa.hdr.params.bits |= MPA_RR_FLAG_CRC;

	/*
	 * If MPA version == 2:
	 * o Include ORD and IRD.
	 * o Indicate peer-to-peer mode, if required by module
	 *   parameter 'peer_to_peer'.
	 */
	if (version == MPA_REVISION_2) {
		cep->enhanced_rdma_conn_est = true;
		cep->mpa.hdr.params.bits |= MPA_RR_FLAG_ENHANCED;

		cep->mpa.v2_ctrl.ird = htons(cep->ird);
		cep->mpa.v2_ctrl.ord = htons(cep->ord);

		if (p2p_mode) {
			cep->mpa.v2_ctrl.ird |= MPA_V2_PEER_TO_PEER;
			cep->mpa.v2_ctrl.ord |= rtr_type;
		}
		/* Remember own P2P mode requested */
		cep->mpa.v2_ctrl_req.ird = cep->mpa.v2_ctrl.ird;
		cep->mpa.v2_ctrl_req.ord = cep->mpa.v2_ctrl.ord;
	}
	memcpy(cep->mpa.hdr.key, MPA_KEY_REQ, 16);

	rv = siw_send_mpareqrep(cep, params->private_data, pd_len);
	/*
	 * Reset private data.
	 */
	cep->mpa.hdr.params.pd_len = 0;

	if (rv >= 0) {
		rv = siw_cm_queue_work(cep, SIW_CM_WORK_MPATIMEOUT);
		if (!rv) {
			siw_dbg_cep(cep, "[QP %u]: exit\n", qp_id(qp));
			siw_cep_set_free(cep);
			return 0;
		}
	}
error:
	siw_dbg(id->device, "failed: %d\n", rv);

	if (cep) {
		siw_socket_disassoc(s);
		sock_release(s);
		cep->sock = NULL;

		cep->qp = NULL;

		cep->cm_id = NULL;
		id->rem_ref(id);

		qp->cep = NULL;
		siw_cep_put(cep);

		cep->state = SIW_EPSTATE_CLOSED;

		siw_cep_set_free(cep);

		siw_cep_put(cep);

	} else if (s) {
		sock_release(s);
	}
	if (qp)
		siw_qp_put(qp);

	return rv;
}

/*
 * siw_accept - Let SoftiWARP accept an RDMA connection request
 *
 * @id:		New connection management id to be used for accepted
 *		connection request
 * @params:	Connection parameters provided by ULP for accepting connection
 *
 * Transition QP to RTS state, associate new CM id @id with accepted CEP
 * and get prepared for TCP input by installing socket callbacks.
 * Then send MPA Reply and generate the "connection established" event.
 * Socket callbacks must be installed before sending MPA Reply, because
 * the latter may cause a first RDMA message to arrive from the RDMA Initiator
 * side very quickly, at which time the socket callbacks must be ready.
 */
int siw_accept(struct iw_cm_id *id, struct iw_cm_conn_param *params)
{
	struct siw_device *sdev = to_siw_dev(id->device);
	struct siw_cep *cep = (struct siw_cep *)id->provider_data;
	struct siw_qp *qp;
	struct siw_qp_attrs qp_attrs;
	int rv, max_priv_data = MPA_MAX_PRIVDATA;
	bool wait_for_peer_rts = false;

	siw_cep_set_inuse(cep);
	siw_cep_put(cep);

	/* Free lingering inbound private data */
	if (cep->mpa.hdr.params.pd_len) {
		cep->mpa.hdr.params.pd_len = 0;
		kfree(cep->mpa.pdata);
		cep->mpa.pdata = NULL;
	}
	siw_cancel_mpatimer(cep);

	if (cep->state != SIW_EPSTATE_RECVD_MPAREQ) {
		siw_dbg_cep(cep, "out of state\n");

		siw_cep_set_free(cep);
		siw_cep_put(cep);

		return -ECONNRESET;
	}
	qp = siw_qp_id2obj(sdev, params->qpn);
	if (!qp) {
		WARN(1, "[QP %d] does not exist\n", params->qpn);
		siw_cep_set_free(cep);
		siw_cep_put(cep);

		return -EINVAL;
	}
	down_write(&qp->state_lock);
	if (qp->attrs.state > SIW_QP_STATE_RTR) {
		rv = -EINVAL;
		up_write(&qp->state_lock);
		goto error;
	}
	siw_dbg_cep(cep, "[QP %d]\n", params->qpn);

	if (try_gso && cep->mpa.hdr.params.bits & MPA_RR_FLAG_GSO_EXP) {
		siw_dbg_cep(cep, "peer allows GSO on TX\n");
		qp->tx_ctx.gso_seg_limit = 0;
	}
	if (params->ord > sdev->attrs.max_ord ||
	    params->ird > sdev->attrs.max_ird) {
		siw_dbg_cep(
			cep,
			"[QP %u]: ord %d (max %d), ird %d (max %d)\n",
			qp_id(qp), params->ord, sdev->attrs.max_ord,
			params->ird, sdev->attrs.max_ird);
		rv = -EINVAL;
		up_write(&qp->state_lock);
		goto error;
	}
	if (cep->enhanced_rdma_conn_est)
		max_priv_data -= sizeof(struct mpa_v2_data);

	if (params->private_data_len > max_priv_data) {
		siw_dbg_cep(
			cep,
			"[QP %u]: private data length: %d (max %d)\n",
			qp_id(qp), params->private_data_len, max_priv_data);
		rv = -EINVAL;
		up_write(&qp->state_lock);
		goto error;
	}
	if (cep->enhanced_rdma_conn_est) {
		if (params->ord > cep->ord) {
			if (relaxed_ird_negotiation) {
				params->ord = cep->ord;
			} else {
				cep->ird = params->ird;
				cep->ord = params->ord;
				rv = -EINVAL;
				up_write(&qp->state_lock);
				goto error;
			}
		}
		if (params->ird < cep->ird) {
			if (relaxed_ird_negotiation &&
			    cep->ird <= sdev->attrs.max_ird)
				params->ird = cep->ird;
			else {
				rv = -ENOMEM;
				up_write(&qp->state_lock);
				goto error;
			}
		}
		if (cep->mpa.v2_ctrl.ord &
		    (MPA_V2_RDMA_WRITE_RTR | MPA_V2_RDMA_READ_RTR))
			wait_for_peer_rts = true;
		/*
		 * Signal back negotiated IRD and ORD values
		 */
		cep->mpa.v2_ctrl.ord =
			htons(params->ord & MPA_IRD_ORD_MASK) |
			(cep->mpa.v2_ctrl.ord & ~MPA_V2_MASK_IRD_ORD);
		cep->mpa.v2_ctrl.ird =
			htons(params->ird & MPA_IRD_ORD_MASK) |
			(cep->mpa.v2_ctrl.ird & ~MPA_V2_MASK_IRD_ORD);
	}
	cep->ird = params->ird;
	cep->ord = params->ord;

	cep->cm_id = id;
	id->add_ref(id);

	memset(&qp_attrs, 0, sizeof(qp_attrs));
	qp_attrs.orq_size = cep->ord;
	qp_attrs.irq_size = cep->ird;
	qp_attrs.sk = cep->sock;
	if (cep->mpa.hdr.params.bits & MPA_RR_FLAG_CRC)
		qp_attrs.flags = SIW_MPA_CRC;
	qp_attrs.state = SIW_QP_STATE_RTS;

	siw_dbg_cep(cep, "[QP%u]: moving to rts\n", qp_id(qp));

	/* Associate QP with CEP */
	siw_cep_get(cep);
	qp->cep = cep;

	/* siw_qp_get(qp) already done by QP lookup */
	cep->qp = qp;

	cep->state = SIW_EPSTATE_RDMA_MODE;

	/* Move socket RX/TX under QP control */
	rv = siw_qp_modify(qp, &qp_attrs,
			   SIW_QP_ATTR_STATE | SIW_QP_ATTR_LLP_HANDLE |
				   SIW_QP_ATTR_ORD | SIW_QP_ATTR_IRD |
				   SIW_QP_ATTR_MPA);
	up_write(&qp->state_lock);

	if (rv)
		goto error;

	siw_dbg_cep(cep, "[QP %u]: send mpa reply, %d byte pdata\n",
		    qp_id(qp), params->private_data_len);

	rv = siw_send_mpareqrep(cep, params->private_data,
				params->private_data_len);
	if (rv != 0)
		goto error;

	if (wait_for_peer_rts) {
		siw_sk_assign_rtr_upcalls(cep);
	} else {
		siw_qp_socket_assoc(cep, qp);
		rv = siw_cm_upcall(cep, IW_CM_EVENT_ESTABLISHED, 0);
		if (rv)
			goto error;
	}
	siw_cep_set_free(cep);

	return 0;
error:
	siw_socket_disassoc(cep->sock);
	sock_release(cep->sock);
	cep->sock = NULL;

	cep->state = SIW_EPSTATE_CLOSED;

	if (cep->cm_id) {
		cep->cm_id->rem_ref(id);
		cep->cm_id = NULL;
	}
	if (qp->cep) {
		siw_cep_put(cep);
		qp->cep = NULL;
	}
	cep->qp = NULL;
	siw_qp_put(qp);

	siw_cep_set_free(cep);
	siw_cep_put(cep);

	return rv;
}

/*
 * siw_reject()
 *
 * Local connection reject case. Send private data back to peer,
 * close connection and dereference connection id.
 */
int siw_reject(struct iw_cm_id *id, const void *pdata, u8 pd_len)
{
	struct siw_cep *cep = (struct siw_cep *)id->provider_data;

	siw_cep_set_inuse(cep);
	siw_cep_put(cep);

	siw_cancel_mpatimer(cep);

	if (cep->state != SIW_EPSTATE_RECVD_MPAREQ) {
		siw_dbg_cep(cep, "out of state\n");

		siw_cep_set_free(cep);
		siw_cep_put(cep); /* put last reference */

		return -ECONNRESET;
	}
	siw_dbg_cep(cep, "cep->state %d, pd_len %d\n", cep->state,
		    pd_len);

	if (__mpa_rr_revision(cep->mpa.hdr.params.bits) >= MPA_REVISION_1) {
		cep->mpa.hdr.params.bits |= MPA_RR_FLAG_REJECT; /* reject */
		siw_send_mpareqrep(cep, pdata, pd_len);
	}
	siw_socket_disassoc(cep->sock);
	sock_release(cep->sock);
	cep->sock = NULL;

	cep->state = SIW_EPSTATE_CLOSED;

	siw_cep_set_free(cep);
	siw_cep_put(cep);

	return 0;
}

/*
 * siw_create_listen - Create resources for a listener's IWCM ID @id
 *
 * Starts listen on the socket address id->local_addr.
 *
 */
int siw_create_listen(struct iw_cm_id *id, int backlog)
{
	struct socket *s;
	struct siw_cep *cep = NULL;
	struct siw_device *sdev = to_siw_dev(id->device);
	int addr_family = id->local_addr.ss_family;
	int rv = 0;

	if (addr_family != AF_INET && addr_family != AF_INET6)
		return -EAFNOSUPPORT;

	rv = sock_create(addr_family, SOCK_STREAM, IPPROTO_TCP, &s);
	if (rv < 0)
		return rv;

	/*
	 * Allow binding local port when still in TIME_WAIT from last close.
	 */
	sock_set_reuseaddr(s->sk);

	if (addr_family == AF_INET) {
		struct sockaddr_in *laddr = &to_sockaddr_in(id->local_addr);

		/* For wildcard addr, limit binding to current device only */
		if (ipv4_is_zeronet(laddr->sin_addr.s_addr))
			s->sk->sk_bound_dev_if = sdev->netdev->ifindex;

		rv = s->ops->bind(s, (struct sockaddr *)laddr,
				  sizeof(struct sockaddr_in));
	} else {
		struct sockaddr_in6 *laddr = &to_sockaddr_in6(id->local_addr);

		if (id->afonly) {
			rv = ip6_sock_set_v6only(s->sk);
			if (rv) {
				siw_dbg(id->device,
					"ip6_sock_set_v6only erro: %d\n", rv);
				goto error;
			}
		}

		/* For wildcard addr, limit binding to current device only */
		if (ipv6_addr_any(&laddr->sin6_addr))
			s->sk->sk_bound_dev_if = sdev->netdev->ifindex;

		rv = s->ops->bind(s, (struct sockaddr *)laddr,
				  sizeof(struct sockaddr_in6));
	}
	if (rv) {
		siw_dbg(id->device, "socket bind error: %d\n", rv);
		goto error;
	}
	cep = siw_cep_alloc(sdev);
	if (!cep) {
		rv = -ENOMEM;
		goto error;
	}
	siw_cep_socket_assoc(cep, s);

	rv = siw_cm_alloc_work(cep, backlog);
	if (rv) {
		siw_dbg(id->device,
			"alloc_work error %d, backlog %d\n",
			rv, backlog);
		goto error;
	}
	rv = s->ops->listen(s, backlog);
	if (rv) {
		siw_dbg(id->device, "listen error %d\n", rv);
		goto error;
	}
	cep->cm_id = id;
	id->add_ref(id);

	/*
	 * In case of a wildcard rdma_listen on a multi-homed device,
	 * a listener's IWCM id is associated with more than one listening CEP.
	 *
	 * We currently use id->provider_data in three different ways:
	 *
	 * o For a listener's IWCM id, id->provider_data points to
	 *   the list_head of the list of listening CEPs.
	 *   Uses: siw_create_listen(), siw_destroy_listen()
	 *
	 * o For each accepted passive-side IWCM id, id->provider_data
	 *   points to the CEP itself. This is a consequence of
	 *   - siw_cm_upcall() setting event.provider_data = cep and
	 *   - the IWCM's cm_conn_req_handler() setting provider_data of the
	 *     new passive-side IWCM id equal to event.provider_data
	 *   Uses: siw_accept(), siw_reject()
	 *
	 * o For an active-side IWCM id, id->provider_data is not used at all.
	 *
	 */
	if (!id->provider_data) {
		id->provider_data =
			kmalloc(sizeof(struct list_head), GFP_KERNEL);
		if (!id->provider_data) {
			rv = -ENOMEM;
			goto error;
		}
		INIT_LIST_HEAD((struct list_head *)id->provider_data);
	}
	list_add_tail(&cep->listenq, (struct list_head *)id->provider_data);
	cep->state = SIW_EPSTATE_LISTENING;

	siw_dbg(id->device, "Listen at laddr %pISp\n", &id->local_addr);

	return 0;

error:
	siw_dbg(id->device, "failed: %d\n", rv);

	if (cep) {
		siw_cep_set_inuse(cep);

		if (cep->cm_id) {
			cep->cm_id->rem_ref(cep->cm_id);
			cep->cm_id = NULL;
		}
		cep->sock = NULL;
		siw_socket_disassoc(s);
		cep->state = SIW_EPSTATE_CLOSED;

		siw_cep_set_free(cep);
		siw_cep_put(cep);
	}
	sock_release(s);

	return rv;
}

static void siw_drop_listeners(struct iw_cm_id *id)
{
	struct list_head *p, *tmp;

	/*
	 * In case of a wildcard rdma_listen on a multi-homed device,
	 * a listener's IWCM id is associated with more than one listening CEP.
	 */
	list_for_each_safe(p, tmp, (struct list_head *)id->provider_data) {
		struct siw_cep *cep = list_entry(p, struct siw_cep, listenq);

		list_del(p);

		siw_dbg_cep(cep, "drop cep, state %d\n", cep->state);

		siw_cep_set_inuse(cep);

		if (cep->cm_id) {
			cep->cm_id->rem_ref(cep->cm_id);
			cep->cm_id = NULL;
		}
		if (cep->sock) {
			siw_socket_disassoc(cep->sock);
			sock_release(cep->sock);
			cep->sock = NULL;
		}
		cep->state = SIW_EPSTATE_CLOSED;
		siw_cep_set_free(cep);
		siw_cep_put(cep);
	}
}

int siw_destroy_listen(struct iw_cm_id *id)
{
	if (!id->provider_data) {
		siw_dbg(id->device, "no cep(s)\n");
		return 0;
	}
	siw_drop_listeners(id);
	kfree(id->provider_data);
	id->provider_data = NULL;

	return 0;
}

int siw_cm_init(void)
{
	/*
	 * create_single_workqueue for strict ordering
	 */
	siw_cm_wq = create_singlethread_workqueue("siw_cm_wq");
	if (!siw_cm_wq)
		return -ENOMEM;

	return 0;
}

void siw_cm_exit(void)
{
	if (siw_cm_wq)
		destroy_workqueue(siw_cm_wq);
}
