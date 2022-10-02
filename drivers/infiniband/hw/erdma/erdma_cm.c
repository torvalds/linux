// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause

/* Authors: Cheng Xu <chengyou@linux.alibaba.com> */
/*          Kai Shen <kaishen@linux.alibaba.com> */
/* Copyright (c) 2020-2022, Alibaba Group. */

/* Authors: Bernard Metzler <bmt@zurich.ibm.com> */
/*          Fredy Neeser */
/*          Greg Joyce <greg@opengridcomputing.com> */
/* Copyright (c) 2008-2019, IBM Corporation */
/* Copyright (c) 2017, Open Grid Computing, Inc. */

#include <linux/errno.h>
#include <linux/inetdevice.h>
#include <linux/net.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <net/addrconf.h>

#include <rdma/ib_user_verbs.h>
#include <rdma/ib_verbs.h>

#include "erdma.h"
#include "erdma_cm.h"
#include "erdma_verbs.h"

static struct workqueue_struct *erdma_cm_wq;

static void erdma_cm_llp_state_change(struct sock *sk);
static void erdma_cm_llp_data_ready(struct sock *sk);
static void erdma_cm_llp_error_report(struct sock *sk);

static void erdma_sk_assign_cm_upcalls(struct sock *sk)
{
	write_lock_bh(&sk->sk_callback_lock);
	sk->sk_state_change = erdma_cm_llp_state_change;
	sk->sk_data_ready = erdma_cm_llp_data_ready;
	sk->sk_error_report = erdma_cm_llp_error_report;
	write_unlock_bh(&sk->sk_callback_lock);
}

static void erdma_sk_save_upcalls(struct sock *sk)
{
	struct erdma_cep *cep = sk_to_cep(sk);

	write_lock_bh(&sk->sk_callback_lock);
	cep->sk_state_change = sk->sk_state_change;
	cep->sk_data_ready = sk->sk_data_ready;
	cep->sk_error_report = sk->sk_error_report;
	write_unlock_bh(&sk->sk_callback_lock);
}

static void erdma_sk_restore_upcalls(struct sock *sk, struct erdma_cep *cep)
{
	sk->sk_state_change = cep->sk_state_change;
	sk->sk_data_ready = cep->sk_data_ready;
	sk->sk_error_report = cep->sk_error_report;
	sk->sk_user_data = NULL;
}

static void erdma_socket_disassoc(struct socket *s)
{
	struct sock *sk = s->sk;
	struct erdma_cep *cep;

	if (sk) {
		write_lock_bh(&sk->sk_callback_lock);
		cep = sk_to_cep(sk);
		if (cep) {
			erdma_sk_restore_upcalls(sk, cep);
			erdma_cep_put(cep);
		} else {
			WARN_ON_ONCE(1);
		}
		write_unlock_bh(&sk->sk_callback_lock);
	} else {
		WARN_ON_ONCE(1);
	}
}

static void erdma_cep_socket_assoc(struct erdma_cep *cep, struct socket *s)
{
	cep->sock = s;
	erdma_cep_get(cep);
	s->sk->sk_user_data = cep;

	erdma_sk_save_upcalls(s->sk);
	erdma_sk_assign_cm_upcalls(s->sk);
}

static void erdma_disassoc_listen_cep(struct erdma_cep *cep)
{
	if (cep->listen_cep) {
		erdma_cep_put(cep->listen_cep);
		cep->listen_cep = NULL;
	}
}

static struct erdma_cep *erdma_cep_alloc(struct erdma_dev *dev)
{
	struct erdma_cep *cep = kzalloc(sizeof(*cep), GFP_KERNEL);
	unsigned long flags;

	if (!cep)
		return NULL;

	INIT_LIST_HEAD(&cep->listenq);
	INIT_LIST_HEAD(&cep->devq);
	INIT_LIST_HEAD(&cep->work_freelist);

	kref_init(&cep->ref);
	cep->state = ERDMA_EPSTATE_IDLE;
	init_waitqueue_head(&cep->waitq);
	spin_lock_init(&cep->lock);
	cep->dev = dev;

	spin_lock_irqsave(&dev->lock, flags);
	list_add_tail(&cep->devq, &dev->cep_list);
	spin_unlock_irqrestore(&dev->lock, flags);

	return cep;
}

static void erdma_cm_free_work(struct erdma_cep *cep)
{
	struct list_head *w, *tmp;
	struct erdma_cm_work *work;

	list_for_each_safe(w, tmp, &cep->work_freelist) {
		work = list_entry(w, struct erdma_cm_work, list);
		list_del(&work->list);
		kfree(work);
	}
}

static void erdma_cancel_mpatimer(struct erdma_cep *cep)
{
	spin_lock_bh(&cep->lock);
	if (cep->mpa_timer) {
		if (cancel_delayed_work(&cep->mpa_timer->work)) {
			erdma_cep_put(cep);
			kfree(cep->mpa_timer);
		}
		cep->mpa_timer = NULL;
	}
	spin_unlock_bh(&cep->lock);
}

static void erdma_put_work(struct erdma_cm_work *work)
{
	INIT_LIST_HEAD(&work->list);
	spin_lock_bh(&work->cep->lock);
	list_add(&work->list, &work->cep->work_freelist);
	spin_unlock_bh(&work->cep->lock);
}

static void erdma_cep_set_inuse(struct erdma_cep *cep)
{
	unsigned long flags;

	spin_lock_irqsave(&cep->lock, flags);
	while (cep->in_use) {
		spin_unlock_irqrestore(&cep->lock, flags);
		wait_event_interruptible(cep->waitq, !cep->in_use);
		if (signal_pending(current))
			flush_signals(current);

		spin_lock_irqsave(&cep->lock, flags);
	}

	cep->in_use = 1;
	spin_unlock_irqrestore(&cep->lock, flags);
}

static void erdma_cep_set_free(struct erdma_cep *cep)
{
	unsigned long flags;

	spin_lock_irqsave(&cep->lock, flags);
	cep->in_use = 0;
	spin_unlock_irqrestore(&cep->lock, flags);

	wake_up(&cep->waitq);
}

static void __erdma_cep_dealloc(struct kref *ref)
{
	struct erdma_cep *cep = container_of(ref, struct erdma_cep, ref);
	struct erdma_dev *dev = cep->dev;
	unsigned long flags;

	WARN_ON(cep->listen_cep);

	kfree(cep->private_data);
	kfree(cep->mpa.pdata);
	spin_lock_bh(&cep->lock);
	if (!list_empty(&cep->work_freelist))
		erdma_cm_free_work(cep);
	spin_unlock_bh(&cep->lock);

	spin_lock_irqsave(&dev->lock, flags);
	list_del(&cep->devq);
	spin_unlock_irqrestore(&dev->lock, flags);
	kfree(cep);
}

static struct erdma_cm_work *erdma_get_work(struct erdma_cep *cep)
{
	struct erdma_cm_work *work = NULL;

	spin_lock_bh(&cep->lock);
	if (!list_empty(&cep->work_freelist)) {
		work = list_entry(cep->work_freelist.next, struct erdma_cm_work,
				  list);
		list_del_init(&work->list);
	}

	spin_unlock_bh(&cep->lock);
	return work;
}

static int erdma_cm_alloc_work(struct erdma_cep *cep, int num)
{
	struct erdma_cm_work *work;

	while (num--) {
		work = kmalloc(sizeof(*work), GFP_KERNEL);
		if (!work) {
			if (!(list_empty(&cep->work_freelist)))
				erdma_cm_free_work(cep);
			return -ENOMEM;
		}
		work->cep = cep;
		INIT_LIST_HEAD(&work->list);
		list_add(&work->list, &cep->work_freelist);
	}

	return 0;
}

static int erdma_cm_upcall(struct erdma_cep *cep, enum iw_cm_event_type reason,
			   int status)
{
	struct iw_cm_event event;
	struct iw_cm_id *cm_id;

	memset(&event, 0, sizeof(event));
	event.status = status;
	event.event = reason;

	if (reason == IW_CM_EVENT_CONNECT_REQUEST) {
		event.provider_data = cep;
		cm_id = cep->listen_cep->cm_id;

		event.ird = cep->dev->attrs.max_ird;
		event.ord = cep->dev->attrs.max_ord;
	} else {
		cm_id = cep->cm_id;
	}

	if (reason == IW_CM_EVENT_CONNECT_REQUEST ||
	    reason == IW_CM_EVENT_CONNECT_REPLY) {
		u16 pd_len = be16_to_cpu(cep->mpa.hdr.params.pd_len);

		if (pd_len && cep->mpa.pdata) {
			event.private_data_len = pd_len;
			event.private_data = cep->mpa.pdata;
		}

		getname_local(cep->sock, &event.local_addr);
		getname_peer(cep->sock, &event.remote_addr);
	}

	return cm_id->event_handler(cm_id, &event);
}

void erdma_qp_cm_drop(struct erdma_qp *qp)
{
	struct erdma_cep *cep = qp->cep;

	if (!qp->cep)
		return;

	erdma_cep_set_inuse(cep);

	/* already closed. */
	if (cep->state == ERDMA_EPSTATE_CLOSED)
		goto out;

	if (cep->cm_id) {
		switch (cep->state) {
		case ERDMA_EPSTATE_AWAIT_MPAREP:
			erdma_cm_upcall(cep, IW_CM_EVENT_CONNECT_REPLY,
					-EINVAL);
			break;
		case ERDMA_EPSTATE_RDMA_MODE:
			erdma_cm_upcall(cep, IW_CM_EVENT_CLOSE, 0);
			break;
		case ERDMA_EPSTATE_IDLE:
		case ERDMA_EPSTATE_LISTENING:
		case ERDMA_EPSTATE_CONNECTING:
		case ERDMA_EPSTATE_AWAIT_MPAREQ:
		case ERDMA_EPSTATE_RECVD_MPAREQ:
		case ERDMA_EPSTATE_CLOSED:
		default:
			break;
		}
		cep->cm_id->rem_ref(cep->cm_id);
		cep->cm_id = NULL;
		erdma_cep_put(cep);
	}
	cep->state = ERDMA_EPSTATE_CLOSED;

	if (cep->sock) {
		erdma_socket_disassoc(cep->sock);
		sock_release(cep->sock);
		cep->sock = NULL;
	}

	if (cep->qp) {
		cep->qp = NULL;
		erdma_qp_put(qp);
	}
out:
	erdma_cep_set_free(cep);
}

void erdma_cep_put(struct erdma_cep *cep)
{
	WARN_ON(kref_read(&cep->ref) < 1);
	kref_put(&cep->ref, __erdma_cep_dealloc);
}

void erdma_cep_get(struct erdma_cep *cep)
{
	kref_get(&cep->ref);
}

static int erdma_send_mpareqrep(struct erdma_cep *cep, const void *pdata,
				u8 pd_len)
{
	struct socket *s = cep->sock;
	struct mpa_rr *rr = &cep->mpa.hdr;
	struct kvec iov[3];
	struct msghdr msg;
	int iovec_num = 0;
	int ret;
	int mpa_len;

	memset(&msg, 0, sizeof(msg));

	rr->params.pd_len = cpu_to_be16(pd_len);

	iov[iovec_num].iov_base = rr;
	iov[iovec_num].iov_len = sizeof(*rr);
	iovec_num++;
	mpa_len = sizeof(*rr);

	iov[iovec_num].iov_base = &cep->mpa.ext_data;
	iov[iovec_num].iov_len = sizeof(cep->mpa.ext_data);
	iovec_num++;
	mpa_len += sizeof(cep->mpa.ext_data);

	if (pd_len) {
		iov[iovec_num].iov_base = (char *)pdata;
		iov[iovec_num].iov_len = pd_len;
		mpa_len += pd_len;
		iovec_num++;
	}

	ret = kernel_sendmsg(s, &msg, iov, iovec_num, mpa_len);

	return ret < 0 ? ret : 0;
}

static inline int ksock_recv(struct socket *sock, char *buf, size_t size,
			     int flags)
{
	struct kvec iov = { buf, size };
	struct msghdr msg = { .msg_name = NULL, .msg_flags = flags };

	return kernel_recvmsg(sock, &msg, &iov, 1, size, flags);
}

static int __recv_mpa_hdr(struct erdma_cep *cep, int hdr_rcvd, char *hdr,
			  int hdr_size, int *rcvd_out)
{
	struct socket *s = cep->sock;
	int rcvd;

	*rcvd_out = 0;
	if (hdr_rcvd < hdr_size) {
		rcvd = ksock_recv(s, hdr + hdr_rcvd, hdr_size - hdr_rcvd,
				  MSG_DONTWAIT);
		if (rcvd == -EAGAIN)
			return -EAGAIN;

		if (rcvd <= 0)
			return -ECONNABORTED;

		hdr_rcvd += rcvd;
		*rcvd_out = rcvd;

		if (hdr_rcvd < hdr_size)
			return -EAGAIN;
	}

	return 0;
}

static void __mpa_rr_set_revision(__be16 *bits, u8 rev)
{
	*bits = (*bits & ~MPA_RR_MASK_REVISION) |
		(cpu_to_be16(rev) & MPA_RR_MASK_REVISION);
}

static u8 __mpa_rr_revision(__be16 mpa_rr_bits)
{
	__be16 rev = mpa_rr_bits & MPA_RR_MASK_REVISION;

	return (u8)be16_to_cpu(rev);
}

static void __mpa_ext_set_cc(__be32 *bits, u32 cc)
{
	*bits = (*bits & ~MPA_EXT_FLAG_CC) |
		(cpu_to_be32(cc) & MPA_EXT_FLAG_CC);
}

static u8 __mpa_ext_cc(__be32 mpa_ext_bits)
{
	__be32 cc = mpa_ext_bits & MPA_EXT_FLAG_CC;

	return (u8)be32_to_cpu(cc);
}

/*
 * Receive MPA Request/Reply header.
 *
 * Returns 0 if complete MPA Request/Reply haeder including
 * eventual private data was received. Returns -EAGAIN if
 * header was partially received or negative error code otherwise.
 *
 * Context: May be called in process context only
 */
static int erdma_recv_mpa_rr(struct erdma_cep *cep)
{
	struct mpa_rr *hdr = &cep->mpa.hdr;
	struct socket *s = cep->sock;
	u16 pd_len;
	int rcvd, to_rcv, ret, pd_rcvd;

	if (cep->mpa.bytes_rcvd < sizeof(struct mpa_rr)) {
		ret = __recv_mpa_hdr(cep, cep->mpa.bytes_rcvd,
				     (char *)&cep->mpa.hdr,
				     sizeof(struct mpa_rr), &rcvd);
		cep->mpa.bytes_rcvd += rcvd;
		if (ret)
			return ret;
	}

	if (be16_to_cpu(hdr->params.pd_len) > MPA_MAX_PRIVDATA ||
	    __mpa_rr_revision(hdr->params.bits) != MPA_REVISION_EXT_1)
		return -EPROTO;

	if (cep->mpa.bytes_rcvd - sizeof(struct mpa_rr) <
	    sizeof(struct erdma_mpa_ext)) {
		ret = __recv_mpa_hdr(
			cep, cep->mpa.bytes_rcvd - sizeof(struct mpa_rr),
			(char *)&cep->mpa.ext_data,
			sizeof(struct erdma_mpa_ext), &rcvd);
		cep->mpa.bytes_rcvd += rcvd;
		if (ret)
			return ret;
	}

	pd_len = be16_to_cpu(hdr->params.pd_len);
	pd_rcvd = cep->mpa.bytes_rcvd - sizeof(struct mpa_rr) -
		  sizeof(struct erdma_mpa_ext);
	to_rcv = pd_len - pd_rcvd;

	if (!to_rcv) {
		/*
		 * We have received the whole MPA Request/Reply message.
		 * Check against peer protocol violation.
		 */
		u32 word;

		ret = __recv_mpa_hdr(cep, 0, (char *)&word, sizeof(word),
				     &rcvd);
		if (ret == -EAGAIN && rcvd == 0)
			return 0;

		if (ret)
			return ret;

		return -EPROTO;
	}

	/*
	 * At this point, MPA header has been fully received, and pd_len != 0.
	 * So, begin to receive private data.
	 */
	if (!cep->mpa.pdata) {
		cep->mpa.pdata = kmalloc(pd_len + 4, GFP_KERNEL);
		if (!cep->mpa.pdata)
			return -ENOMEM;
	}

	rcvd = ksock_recv(s, cep->mpa.pdata + pd_rcvd, to_rcv + 4,
			  MSG_DONTWAIT);
	if (rcvd < 0)
		return rcvd;

	if (rcvd > to_rcv)
		return -EPROTO;

	cep->mpa.bytes_rcvd += rcvd;

	if (to_rcv == rcvd)
		return 0;

	return -EAGAIN;
}

/*
 * erdma_proc_mpareq()
 *
 * Read MPA Request from socket and signal new connection to IWCM
 * if success. Caller must hold lock on corresponding listening CEP.
 */
static int erdma_proc_mpareq(struct erdma_cep *cep)
{
	struct mpa_rr *req;
	int ret;

	ret = erdma_recv_mpa_rr(cep);
	if (ret)
		return ret;

	req = &cep->mpa.hdr;

	if (memcmp(req->key, MPA_KEY_REQ, MPA_KEY_SIZE))
		return -EPROTO;

	memcpy(req->key, MPA_KEY_REP, MPA_KEY_SIZE);

	/* Currently does not support marker and crc. */
	if (req->params.bits & MPA_RR_FLAG_MARKERS ||
	    req->params.bits & MPA_RR_FLAG_CRC)
		goto reject_conn;

	cep->state = ERDMA_EPSTATE_RECVD_MPAREQ;

	/* Keep reference until IWCM accepts/rejects */
	erdma_cep_get(cep);
	ret = erdma_cm_upcall(cep, IW_CM_EVENT_CONNECT_REQUEST, 0);
	if (ret)
		erdma_cep_put(cep);

	return ret;

reject_conn:
	req->params.bits &= ~MPA_RR_FLAG_MARKERS;
	req->params.bits |= MPA_RR_FLAG_REJECT;
	req->params.bits &= ~MPA_RR_FLAG_CRC;

	kfree(cep->mpa.pdata);
	cep->mpa.pdata = NULL;
	erdma_send_mpareqrep(cep, NULL, 0);

	return -EOPNOTSUPP;
}

static int erdma_proc_mpareply(struct erdma_cep *cep)
{
	struct erdma_qp_attrs qp_attrs;
	struct erdma_qp *qp = cep->qp;
	struct mpa_rr *rep;
	int ret;

	ret = erdma_recv_mpa_rr(cep);
	if (ret)
		goto out_err;

	erdma_cancel_mpatimer(cep);

	rep = &cep->mpa.hdr;

	if (memcmp(rep->key, MPA_KEY_REP, MPA_KEY_SIZE)) {
		ret = -EPROTO;
		goto out_err;
	}

	if (rep->params.bits & MPA_RR_FLAG_REJECT) {
		erdma_cm_upcall(cep, IW_CM_EVENT_CONNECT_REPLY, -ECONNRESET);
		return -ECONNRESET;
	}

	/* Currently does not support marker and crc. */
	if ((rep->params.bits & MPA_RR_FLAG_MARKERS) ||
	    (rep->params.bits & MPA_RR_FLAG_CRC)) {
		erdma_cm_upcall(cep, IW_CM_EVENT_CONNECT_REPLY, -ECONNREFUSED);
		return -EINVAL;
	}

	memset(&qp_attrs, 0, sizeof(qp_attrs));
	qp_attrs.irq_size = cep->ird;
	qp_attrs.orq_size = cep->ord;
	qp_attrs.state = ERDMA_QP_STATE_RTS;

	down_write(&qp->state_lock);
	if (qp->attrs.state > ERDMA_QP_STATE_RTR) {
		ret = -EINVAL;
		up_write(&qp->state_lock);
		goto out_err;
	}

	qp->attrs.qp_type = ERDMA_QP_ACTIVE;
	if (__mpa_ext_cc(cep->mpa.ext_data.bits) != qp->attrs.cc)
		qp->attrs.cc = COMPROMISE_CC;

	ret = erdma_modify_qp_internal(qp, &qp_attrs,
				       ERDMA_QP_ATTR_STATE |
				       ERDMA_QP_ATTR_LLP_HANDLE |
				       ERDMA_QP_ATTR_MPA);

	up_write(&qp->state_lock);

	if (!ret) {
		ret = erdma_cm_upcall(cep, IW_CM_EVENT_CONNECT_REPLY, 0);
		if (!ret)
			cep->state = ERDMA_EPSTATE_RDMA_MODE;

		return 0;
	}

out_err:
	if (ret != -EAGAIN)
		erdma_cm_upcall(cep, IW_CM_EVENT_CONNECT_REPLY, -EINVAL);

	return ret;
}

static void erdma_accept_newconn(struct erdma_cep *cep)
{
	struct socket *s = cep->sock;
	struct socket *new_s = NULL;
	struct erdma_cep *new_cep = NULL;
	int ret = 0;

	if (cep->state != ERDMA_EPSTATE_LISTENING)
		goto error;

	new_cep = erdma_cep_alloc(cep->dev);
	if (!new_cep)
		goto error;

	/*
	 * 4: Allocate a sufficient number of work elements
	 * to allow concurrent handling of local + peer close
	 * events, MPA header processing + MPA timeout.
	 */
	if (erdma_cm_alloc_work(new_cep, 4) != 0)
		goto error;

	/*
	 * Copy saved socket callbacks from listening CEP
	 * and assign new socket with new CEP
	 */
	new_cep->sk_state_change = cep->sk_state_change;
	new_cep->sk_data_ready = cep->sk_data_ready;
	new_cep->sk_error_report = cep->sk_error_report;

	ret = kernel_accept(s, &new_s, O_NONBLOCK);
	if (ret != 0)
		goto error;

	new_cep->sock = new_s;
	erdma_cep_get(new_cep);
	new_s->sk->sk_user_data = new_cep;

	tcp_sock_set_nodelay(new_s->sk);
	new_cep->state = ERDMA_EPSTATE_AWAIT_MPAREQ;

	ret = erdma_cm_queue_work(new_cep, ERDMA_CM_WORK_MPATIMEOUT);
	if (ret)
		goto error;

	new_cep->listen_cep = cep;
	erdma_cep_get(cep);

	if (atomic_read(&new_s->sk->sk_rmem_alloc)) {
		/* MPA REQ already queued */
		erdma_cep_set_inuse(new_cep);
		ret = erdma_proc_mpareq(new_cep);
		if (ret != -EAGAIN) {
			erdma_cep_put(cep);
			new_cep->listen_cep = NULL;
			if (ret) {
				erdma_cep_set_free(new_cep);
				goto error;
			}
		}
		erdma_cep_set_free(new_cep);
	}
	return;

error:
	if (new_cep) {
		new_cep->state = ERDMA_EPSTATE_CLOSED;
		erdma_cancel_mpatimer(new_cep);

		erdma_cep_put(new_cep);
		new_cep->sock = NULL;
	}

	if (new_s) {
		erdma_socket_disassoc(new_s);
		sock_release(new_s);
	}
}

static int erdma_newconn_connected(struct erdma_cep *cep)
{
	int ret = 0;

	cep->mpa.hdr.params.bits = 0;
	__mpa_rr_set_revision(&cep->mpa.hdr.params.bits, MPA_REVISION_EXT_1);

	memcpy(cep->mpa.hdr.key, MPA_KEY_REQ, MPA_KEY_SIZE);
	cep->mpa.ext_data.cookie = cpu_to_be32(cep->qp->attrs.cookie);
	__mpa_ext_set_cc(&cep->mpa.ext_data.bits, cep->qp->attrs.cc);

	ret = erdma_send_mpareqrep(cep, cep->private_data, cep->pd_len);
	cep->state = ERDMA_EPSTATE_AWAIT_MPAREP;
	cep->mpa.hdr.params.pd_len = 0;

	if (ret >= 0)
		ret = erdma_cm_queue_work(cep, ERDMA_CM_WORK_MPATIMEOUT);

	return ret;
}

static void erdma_cm_work_handler(struct work_struct *w)
{
	struct erdma_cm_work *work;
	struct erdma_cep *cep;
	int release_cep = 0, ret = 0;

	work = container_of(w, struct erdma_cm_work, work.work);
	cep = work->cep;

	erdma_cep_set_inuse(cep);

	switch (work->type) {
	case ERDMA_CM_WORK_CONNECTED:
		erdma_cancel_mpatimer(cep);
		if (cep->state == ERDMA_EPSTATE_CONNECTING) {
			ret = erdma_newconn_connected(cep);
			if (ret) {
				erdma_cm_upcall(cep, IW_CM_EVENT_CONNECT_REPLY,
						-EIO);
				release_cep = 1;
			}
		}
		break;
	case ERDMA_CM_WORK_CONNECTTIMEOUT:
		if (cep->state == ERDMA_EPSTATE_CONNECTING) {
			cep->mpa_timer = NULL;
			erdma_cm_upcall(cep, IW_CM_EVENT_CONNECT_REPLY,
					-ETIMEDOUT);
			release_cep = 1;
		}
		break;
	case ERDMA_CM_WORK_ACCEPT:
		erdma_accept_newconn(cep);
		break;
	case ERDMA_CM_WORK_READ_MPAHDR:
		if (cep->state == ERDMA_EPSTATE_AWAIT_MPAREQ) {
			if (cep->listen_cep) {
				erdma_cep_set_inuse(cep->listen_cep);

				if (cep->listen_cep->state ==
				    ERDMA_EPSTATE_LISTENING)
					ret = erdma_proc_mpareq(cep);
				else
					ret = -EFAULT;

				erdma_cep_set_free(cep->listen_cep);

				if (ret != -EAGAIN) {
					erdma_cep_put(cep->listen_cep);
					cep->listen_cep = NULL;
					if (ret)
						erdma_cep_put(cep);
				}
			}
		} else if (cep->state == ERDMA_EPSTATE_AWAIT_MPAREP) {
			ret = erdma_proc_mpareply(cep);
		}

		if (ret && ret != -EAGAIN)
			release_cep = 1;
		break;
	case ERDMA_CM_WORK_CLOSE_LLP:
		if (cep->cm_id)
			erdma_cm_upcall(cep, IW_CM_EVENT_CLOSE, 0);
		release_cep = 1;
		break;
	case ERDMA_CM_WORK_PEER_CLOSE:
		if (cep->cm_id) {
			if (cep->state == ERDMA_EPSTATE_CONNECTING ||
			    cep->state == ERDMA_EPSTATE_AWAIT_MPAREP) {
				/*
				 * MPA reply not received, but connection drop
				 */
				erdma_cm_upcall(cep, IW_CM_EVENT_CONNECT_REPLY,
						-ECONNRESET);
			} else if (cep->state == ERDMA_EPSTATE_RDMA_MODE) {
				/*
				 * NOTE: IW_CM_EVENT_DISCONNECT is given just
				 *       to transition IWCM into CLOSING.
				 */
				erdma_cm_upcall(cep, IW_CM_EVENT_DISCONNECT, 0);
				erdma_cm_upcall(cep, IW_CM_EVENT_CLOSE, 0);
			}
		} else if (cep->state == ERDMA_EPSTATE_AWAIT_MPAREQ) {
			/* Socket close before MPA request received. */
			erdma_disassoc_listen_cep(cep);
			erdma_cep_put(cep);
		}
		release_cep = 1;
		break;
	case ERDMA_CM_WORK_MPATIMEOUT:
		cep->mpa_timer = NULL;
		if (cep->state == ERDMA_EPSTATE_AWAIT_MPAREP) {
			/*
			 * MPA request timed out:
			 * Hide any partially received private data and signal
			 * timeout
			 */
			cep->mpa.hdr.params.pd_len = 0;

			if (cep->cm_id)
				erdma_cm_upcall(cep, IW_CM_EVENT_CONNECT_REPLY,
						-ETIMEDOUT);
			release_cep = 1;
		} else if (cep->state == ERDMA_EPSTATE_AWAIT_MPAREQ) {
			/* No MPA req received after peer TCP stream setup. */
			erdma_disassoc_listen_cep(cep);

			erdma_cep_put(cep);
			release_cep = 1;
		}
		break;
	default:
		WARN(1, "Undefined CM work type: %d\n", work->type);
	}

	if (release_cep) {
		erdma_cancel_mpatimer(cep);
		cep->state = ERDMA_EPSTATE_CLOSED;
		if (cep->qp) {
			struct erdma_qp *qp = cep->qp;
			/*
			 * Serialize a potential race with application
			 * closing the QP and calling erdma_qp_cm_drop()
			 */
			erdma_qp_get(qp);
			erdma_cep_set_free(cep);

			erdma_qp_llp_close(qp);
			erdma_qp_put(qp);

			erdma_cep_set_inuse(cep);
			cep->qp = NULL;
			erdma_qp_put(qp);
		}

		if (cep->sock) {
			erdma_socket_disassoc(cep->sock);
			sock_release(cep->sock);
			cep->sock = NULL;
		}

		if (cep->cm_id) {
			cep->cm_id->rem_ref(cep->cm_id);
			cep->cm_id = NULL;
			if (cep->state != ERDMA_EPSTATE_LISTENING)
				erdma_cep_put(cep);
		}
	}
	erdma_cep_set_free(cep);
	erdma_put_work(work);
	erdma_cep_put(cep);
}

int erdma_cm_queue_work(struct erdma_cep *cep, enum erdma_work_type type)
{
	struct erdma_cm_work *work = erdma_get_work(cep);
	unsigned long delay = 0;

	if (!work)
		return -ENOMEM;

	work->type = type;
	work->cep = cep;

	erdma_cep_get(cep);

	INIT_DELAYED_WORK(&work->work, erdma_cm_work_handler);

	if (type == ERDMA_CM_WORK_MPATIMEOUT) {
		cep->mpa_timer = work;

		if (cep->state == ERDMA_EPSTATE_AWAIT_MPAREP)
			delay = MPAREP_TIMEOUT;
		else
			delay = MPAREQ_TIMEOUT;
	} else if (type == ERDMA_CM_WORK_CONNECTTIMEOUT) {
		cep->mpa_timer = work;

		delay = CONNECT_TIMEOUT;
	}

	queue_delayed_work(erdma_cm_wq, &work->work, delay);

	return 0;
}

static void erdma_cm_llp_data_ready(struct sock *sk)
{
	struct erdma_cep *cep;

	read_lock(&sk->sk_callback_lock);

	cep = sk_to_cep(sk);
	if (!cep)
		goto out;

	if (cep->state == ERDMA_EPSTATE_AWAIT_MPAREQ ||
	    cep->state == ERDMA_EPSTATE_AWAIT_MPAREP)
		erdma_cm_queue_work(cep, ERDMA_CM_WORK_READ_MPAHDR);

out:
	read_unlock(&sk->sk_callback_lock);
}

static void erdma_cm_llp_error_report(struct sock *sk)
{
	struct erdma_cep *cep = sk_to_cep(sk);

	if (cep)
		cep->sk_error_report(sk);
}

static void erdma_cm_llp_state_change(struct sock *sk)
{
	struct erdma_cep *cep;
	void (*orig_state_change)(struct sock *sk);

	read_lock(&sk->sk_callback_lock);

	cep = sk_to_cep(sk);
	if (!cep) {
		read_unlock(&sk->sk_callback_lock);
		return;
	}
	orig_state_change = cep->sk_state_change;

	switch (sk->sk_state) {
	case TCP_ESTABLISHED:
		if (cep->state == ERDMA_EPSTATE_CONNECTING)
			erdma_cm_queue_work(cep, ERDMA_CM_WORK_CONNECTED);
		else
			erdma_cm_queue_work(cep, ERDMA_CM_WORK_ACCEPT);
		break;
	case TCP_CLOSE:
	case TCP_CLOSE_WAIT:
		if (cep->state != ERDMA_EPSTATE_LISTENING)
			erdma_cm_queue_work(cep, ERDMA_CM_WORK_PEER_CLOSE);
		break;
	default:
		break;
	}
	read_unlock(&sk->sk_callback_lock);
	orig_state_change(sk);
}

static int kernel_bindconnect(struct socket *s, struct sockaddr *laddr,
			      int laddrlen, struct sockaddr *raddr,
			      int raddrlen, int flags)
{
	int ret;

	sock_set_reuseaddr(s->sk);
	ret = s->ops->bind(s, laddr, laddrlen);
	if (ret)
		return ret;
	ret = s->ops->connect(s, raddr, raddrlen, flags);
	return ret < 0 ? ret : 0;
}

int erdma_connect(struct iw_cm_id *id, struct iw_cm_conn_param *params)
{
	struct erdma_dev *dev = to_edev(id->device);
	struct erdma_qp *qp;
	struct erdma_cep *cep = NULL;
	struct socket *s = NULL;
	struct sockaddr *laddr = (struct sockaddr *)&id->m_local_addr;
	struct sockaddr *raddr = (struct sockaddr *)&id->m_remote_addr;
	u16 pd_len = params->private_data_len;
	int ret;

	if (pd_len > MPA_MAX_PRIVDATA)
		return -EINVAL;

	if (params->ird > dev->attrs.max_ird ||
	    params->ord > dev->attrs.max_ord)
		return -EINVAL;

	if (laddr->sa_family != AF_INET || raddr->sa_family != AF_INET)
		return -EAFNOSUPPORT;

	qp = find_qp_by_qpn(dev, params->qpn);
	if (!qp)
		return -ENOENT;
	erdma_qp_get(qp);

	ret = sock_create(AF_INET, SOCK_STREAM, IPPROTO_TCP, &s);
	if (ret < 0)
		goto error_put_qp;

	cep = erdma_cep_alloc(dev);
	if (!cep) {
		ret = -ENOMEM;
		goto error_release_sock;
	}

	erdma_cep_set_inuse(cep);

	/* Associate QP with CEP */
	erdma_cep_get(cep);
	qp->cep = cep;
	cep->qp = qp;

	/* Associate cm_id with CEP */
	id->add_ref(id);
	cep->cm_id = id;

	/*
	 * 6: Allocate a sufficient number of work elements
	 * to allow concurrent handling of local + peer close
	 * events, MPA header processing + MPA timeout, connected event
	 * and connect timeout.
	 */
	ret = erdma_cm_alloc_work(cep, 6);
	if (ret != 0) {
		ret = -ENOMEM;
		goto error_release_cep;
	}

	cep->ird = params->ird;
	cep->ord = params->ord;
	cep->state = ERDMA_EPSTATE_CONNECTING;

	erdma_cep_socket_assoc(cep, s);

	if (pd_len) {
		cep->pd_len = pd_len;
		cep->private_data = kmalloc(pd_len, GFP_KERNEL);
		if (!cep->private_data) {
			ret = -ENOMEM;
			goto error_disassoc;
		}

		memcpy(cep->private_data, params->private_data,
		       params->private_data_len);
	}

	ret = kernel_bindconnect(s, laddr, sizeof(*laddr), raddr,
				 sizeof(*raddr), O_NONBLOCK);
	if (ret != -EINPROGRESS && ret != 0) {
		goto error_disassoc;
	} else if (ret == 0) {
		ret = erdma_cm_queue_work(cep, ERDMA_CM_WORK_CONNECTED);
		if (ret)
			goto error_disassoc;
	} else {
		ret = erdma_cm_queue_work(cep, ERDMA_CM_WORK_CONNECTTIMEOUT);
		if (ret)
			goto error_disassoc;
	}

	erdma_cep_set_free(cep);
	return 0;

error_disassoc:
	kfree(cep->private_data);
	cep->private_data = NULL;
	cep->pd_len = 0;

	erdma_socket_disassoc(s);

error_release_cep:
	/* disassoc with cm_id */
	cep->cm_id = NULL;
	id->rem_ref(id);

	/* disassoc with qp */
	qp->cep = NULL;
	erdma_cep_put(cep);
	cep->qp = NULL;

	cep->state = ERDMA_EPSTATE_CLOSED;

	erdma_cep_set_free(cep);

	/* release the cep. */
	erdma_cep_put(cep);

error_release_sock:
	if (s)
		sock_release(s);
error_put_qp:
	erdma_qp_put(qp);

	return ret;
}

int erdma_accept(struct iw_cm_id *id, struct iw_cm_conn_param *params)
{
	struct erdma_dev *dev = to_edev(id->device);
	struct erdma_cep *cep = (struct erdma_cep *)id->provider_data;
	struct erdma_qp *qp;
	struct erdma_qp_attrs qp_attrs;
	int ret;

	erdma_cep_set_inuse(cep);
	erdma_cep_put(cep);

	/* Free lingering inbound private data */
	if (cep->mpa.hdr.params.pd_len) {
		cep->mpa.hdr.params.pd_len = 0;
		kfree(cep->mpa.pdata);
		cep->mpa.pdata = NULL;
	}
	erdma_cancel_mpatimer(cep);

	if (cep->state != ERDMA_EPSTATE_RECVD_MPAREQ) {
		erdma_cep_set_free(cep);
		erdma_cep_put(cep);

		return -ECONNRESET;
	}

	qp = find_qp_by_qpn(dev, params->qpn);
	if (!qp)
		return -ENOENT;
	erdma_qp_get(qp);

	down_write(&qp->state_lock);
	if (qp->attrs.state > ERDMA_QP_STATE_RTR) {
		ret = -EINVAL;
		up_write(&qp->state_lock);
		goto error;
	}

	if (params->ord > dev->attrs.max_ord ||
	    params->ird > dev->attrs.max_ord) {
		ret = -EINVAL;
		up_write(&qp->state_lock);
		goto error;
	}

	if (params->private_data_len > MPA_MAX_PRIVDATA) {
		ret = -EINVAL;
		up_write(&qp->state_lock);
		goto error;
	}

	cep->ird = params->ird;
	cep->ord = params->ord;

	cep->cm_id = id;
	id->add_ref(id);

	memset(&qp_attrs, 0, sizeof(qp_attrs));
	qp_attrs.orq_size = params->ord;
	qp_attrs.irq_size = params->ird;

	qp_attrs.state = ERDMA_QP_STATE_RTS;

	/* Associate QP with CEP */
	erdma_cep_get(cep);
	qp->cep = cep;
	cep->qp = qp;

	cep->state = ERDMA_EPSTATE_RDMA_MODE;

	qp->attrs.qp_type = ERDMA_QP_PASSIVE;
	qp->attrs.pd_len = params->private_data_len;

	if (qp->attrs.cc != __mpa_ext_cc(cep->mpa.ext_data.bits))
		qp->attrs.cc = COMPROMISE_CC;

	/* move to rts */
	ret = erdma_modify_qp_internal(qp, &qp_attrs,
				       ERDMA_QP_ATTR_STATE |
				       ERDMA_QP_ATTR_ORD |
				       ERDMA_QP_ATTR_LLP_HANDLE |
				       ERDMA_QP_ATTR_IRD |
				       ERDMA_QP_ATTR_MPA);
	up_write(&qp->state_lock);

	if (ret)
		goto error;

	cep->mpa.ext_data.bits = 0;
	__mpa_ext_set_cc(&cep->mpa.ext_data.bits, qp->attrs.cc);
	cep->mpa.ext_data.cookie = cpu_to_be32(cep->qp->attrs.cookie);

	ret = erdma_send_mpareqrep(cep, params->private_data,
				   params->private_data_len);
	if (!ret) {
		ret = erdma_cm_upcall(cep, IW_CM_EVENT_ESTABLISHED, 0);
		if (ret)
			goto error;

		erdma_cep_set_free(cep);

		return 0;
	}

error:
	erdma_socket_disassoc(cep->sock);
	sock_release(cep->sock);
	cep->sock = NULL;

	cep->state = ERDMA_EPSTATE_CLOSED;

	if (cep->cm_id) {
		cep->cm_id->rem_ref(id);
		cep->cm_id = NULL;
	}

	if (qp->cep) {
		erdma_cep_put(cep);
		qp->cep = NULL;
	}

	cep->qp = NULL;
	erdma_qp_put(qp);

	erdma_cep_set_free(cep);
	erdma_cep_put(cep);

	return ret;
}

int erdma_reject(struct iw_cm_id *id, const void *pdata, u8 plen)
{
	struct erdma_cep *cep = (struct erdma_cep *)id->provider_data;

	erdma_cep_set_inuse(cep);
	erdma_cep_put(cep);

	erdma_cancel_mpatimer(cep);

	if (cep->state != ERDMA_EPSTATE_RECVD_MPAREQ) {
		erdma_cep_set_free(cep);
		erdma_cep_put(cep);

		return -ECONNRESET;
	}

	if (__mpa_rr_revision(cep->mpa.hdr.params.bits) == MPA_REVISION_EXT_1) {
		cep->mpa.hdr.params.bits |= MPA_RR_FLAG_REJECT; /* reject */
		erdma_send_mpareqrep(cep, pdata, plen);
	}

	erdma_socket_disassoc(cep->sock);
	sock_release(cep->sock);
	cep->sock = NULL;

	cep->state = ERDMA_EPSTATE_CLOSED;

	erdma_cep_set_free(cep);
	erdma_cep_put(cep);

	return 0;
}

int erdma_create_listen(struct iw_cm_id *id, int backlog)
{
	struct socket *s;
	struct erdma_cep *cep = NULL;
	int ret = 0;
	struct erdma_dev *dev = to_edev(id->device);
	int addr_family = id->local_addr.ss_family;
	struct sockaddr_in *laddr = &to_sockaddr_in(id->local_addr);

	if (addr_family != AF_INET)
		return -EAFNOSUPPORT;

	ret = sock_create(addr_family, SOCK_STREAM, IPPROTO_TCP, &s);
	if (ret < 0)
		return ret;

	sock_set_reuseaddr(s->sk);

	/* For wildcard addr, limit binding to current device only */
	if (ipv4_is_zeronet(laddr->sin_addr.s_addr))
		s->sk->sk_bound_dev_if = dev->netdev->ifindex;

	ret = s->ops->bind(s, (struct sockaddr *)laddr,
			   sizeof(struct sockaddr_in));
	if (ret)
		goto error;

	cep = erdma_cep_alloc(dev);
	if (!cep) {
		ret = -ENOMEM;
		goto error;
	}
	erdma_cep_socket_assoc(cep, s);

	ret = erdma_cm_alloc_work(cep, backlog);
	if (ret)
		goto error;

	ret = s->ops->listen(s, backlog);
	if (ret)
		goto error;

	cep->cm_id = id;
	id->add_ref(id);

	if (!id->provider_data) {
		id->provider_data =
			kmalloc(sizeof(struct list_head), GFP_KERNEL);
		if (!id->provider_data) {
			ret = -ENOMEM;
			goto error;
		}
		INIT_LIST_HEAD((struct list_head *)id->provider_data);
	}

	list_add_tail(&cep->listenq, (struct list_head *)id->provider_data);
	cep->state = ERDMA_EPSTATE_LISTENING;

	return 0;

error:
	if (cep) {
		erdma_cep_set_inuse(cep);

		if (cep->cm_id) {
			cep->cm_id->rem_ref(cep->cm_id);
			cep->cm_id = NULL;
		}
		cep->sock = NULL;
		erdma_socket_disassoc(s);
		cep->state = ERDMA_EPSTATE_CLOSED;

		erdma_cep_set_free(cep);
		erdma_cep_put(cep);
	}
	sock_release(s);

	return ret;
}

static void erdma_drop_listeners(struct iw_cm_id *id)
{
	struct list_head *p, *tmp;
	/*
	 * In case of a wildcard rdma_listen on a multi-homed device,
	 * a listener's IWCM id is associated with more than one listening CEP.
	 */
	list_for_each_safe(p, tmp, (struct list_head *)id->provider_data) {
		struct erdma_cep *cep =
			list_entry(p, struct erdma_cep, listenq);

		list_del(p);

		erdma_cep_set_inuse(cep);

		if (cep->cm_id) {
			cep->cm_id->rem_ref(cep->cm_id);
			cep->cm_id = NULL;
		}
		if (cep->sock) {
			erdma_socket_disassoc(cep->sock);
			sock_release(cep->sock);
			cep->sock = NULL;
		}
		cep->state = ERDMA_EPSTATE_CLOSED;
		erdma_cep_set_free(cep);
		erdma_cep_put(cep);
	}
}

int erdma_destroy_listen(struct iw_cm_id *id)
{
	if (!id->provider_data)
		return 0;

	erdma_drop_listeners(id);
	kfree(id->provider_data);
	id->provider_data = NULL;

	return 0;
}

int erdma_cm_init(void)
{
	erdma_cm_wq = create_singlethread_workqueue("erdma_cm_wq");
	if (!erdma_cm_wq)
		return -ENOMEM;

	return 0;
}

void erdma_cm_exit(void)
{
	if (erdma_cm_wq)
		destroy_workqueue(erdma_cm_wq);
}
