/*
 * Copyright (c) 2018 Chelsio Communications, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Written by: Atul Gupta (atul.gupta@chelsio.com)
 */

#include <linux/module.h>
#include <linux/list.h>
#include <linux/workqueue.h>
#include <linux/skbuff.h>
#include <linux/timer.h>
#include <linux/notifier.h>
#include <linux/inetdevice.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/sched/signal.h>
#include <linux/kallsyms.h>
#include <linux/kprobes.h>
#include <linux/if_vlan.h>
#include <net/tcp.h>
#include <net/dst.h>

#include "chtls.h"
#include "chtls_cm.h"

/*
 * State transitions and actions for close.  Note that if we are in SYN_SENT
 * we remain in that state as we cannot control a connection while it's in
 * SYN_SENT; such connections are allowed to establish and are then aborted.
 */
static unsigned char new_state[16] = {
	/* current state:     new state:      action: */
	/* (Invalid)       */ TCP_CLOSE,
	/* TCP_ESTABLISHED */ TCP_FIN_WAIT1 | TCP_ACTION_FIN,
	/* TCP_SYN_SENT    */ TCP_SYN_SENT,
	/* TCP_SYN_RECV    */ TCP_FIN_WAIT1 | TCP_ACTION_FIN,
	/* TCP_FIN_WAIT1   */ TCP_FIN_WAIT1,
	/* TCP_FIN_WAIT2   */ TCP_FIN_WAIT2,
	/* TCP_TIME_WAIT   */ TCP_CLOSE,
	/* TCP_CLOSE       */ TCP_CLOSE,
	/* TCP_CLOSE_WAIT  */ TCP_LAST_ACK | TCP_ACTION_FIN,
	/* TCP_LAST_ACK    */ TCP_LAST_ACK,
	/* TCP_LISTEN      */ TCP_CLOSE,
	/* TCP_CLOSING     */ TCP_CLOSING,
};

static struct chtls_sock *chtls_sock_create(struct chtls_dev *cdev)
{
	struct chtls_sock *csk = kzalloc(sizeof(*csk), GFP_ATOMIC);

	if (!csk)
		return NULL;

	csk->txdata_skb_cache = alloc_skb(TXDATA_SKB_LEN, GFP_ATOMIC);
	if (!csk->txdata_skb_cache) {
		kfree(csk);
		return NULL;
	}

	kref_init(&csk->kref);
	csk->cdev = cdev;
	skb_queue_head_init(&csk->txq);
	csk->wr_skb_head = NULL;
	csk->wr_skb_tail = NULL;
	csk->mss = MAX_MSS;
	csk->tlshws.ofld = 1;
	csk->tlshws.txkey = -1;
	csk->tlshws.rxkey = -1;
	csk->tlshws.mfs = TLS_MFS;
	skb_queue_head_init(&csk->tlshws.sk_recv_queue);
	return csk;
}

static void chtls_sock_release(struct kref *ref)
{
	struct chtls_sock *csk =
		container_of(ref, struct chtls_sock, kref);

	kfree(csk);
}

static struct net_device *chtls_ipv4_netdev(struct chtls_dev *cdev,
					    struct sock *sk)
{
	struct net_device *ndev = cdev->ports[0];

	if (likely(!inet_sk(sk)->inet_rcv_saddr))
		return ndev;

	ndev = ip_dev_find(&init_net, inet_sk(sk)->inet_rcv_saddr);
	if (!ndev)
		return NULL;

	if (is_vlan_dev(ndev))
		return vlan_dev_real_dev(ndev);
	return ndev;
}

static void assign_rxopt(struct sock *sk, unsigned int opt)
{
	const struct chtls_dev *cdev;
	struct chtls_sock *csk;
	struct tcp_sock *tp;

	csk = rcu_dereference_sk_user_data(sk);
	tp = tcp_sk(sk);

	cdev = csk->cdev;
	tp->tcp_header_len           = sizeof(struct tcphdr);
	tp->rx_opt.mss_clamp         = cdev->mtus[TCPOPT_MSS_G(opt)] - 40;
	tp->mss_cache                = tp->rx_opt.mss_clamp;
	tp->rx_opt.tstamp_ok         = TCPOPT_TSTAMP_G(opt);
	tp->rx_opt.snd_wscale        = TCPOPT_SACK_G(opt);
	tp->rx_opt.wscale_ok         = TCPOPT_WSCALE_OK_G(opt);
	SND_WSCALE(tp)               = TCPOPT_SND_WSCALE_G(opt);
	if (!tp->rx_opt.wscale_ok)
		tp->rx_opt.rcv_wscale = 0;
	if (tp->rx_opt.tstamp_ok) {
		tp->tcp_header_len += TCPOLEN_TSTAMP_ALIGNED;
		tp->rx_opt.mss_clamp -= TCPOLEN_TSTAMP_ALIGNED;
	} else if (csk->opt2 & TSTAMPS_EN_F) {
		csk->opt2 &= ~TSTAMPS_EN_F;
		csk->mtu_idx = TCPOPT_MSS_G(opt);
	}
}

static void chtls_purge_receive_queue(struct sock *sk)
{
	struct sk_buff *skb;

	while ((skb = __skb_dequeue(&sk->sk_receive_queue)) != NULL) {
		skb_dst_set(skb, (void *)NULL);
		kfree_skb(skb);
	}
}

static void chtls_purge_write_queue(struct sock *sk)
{
	struct chtls_sock *csk = rcu_dereference_sk_user_data(sk);
	struct sk_buff *skb;

	while ((skb = __skb_dequeue(&csk->txq))) {
		sk->sk_wmem_queued -= skb->truesize;
		__kfree_skb(skb);
	}
}

static void chtls_purge_recv_queue(struct sock *sk)
{
	struct chtls_sock *csk = rcu_dereference_sk_user_data(sk);
	struct chtls_hws *tlsk = &csk->tlshws;
	struct sk_buff *skb;

	while ((skb = __skb_dequeue(&tlsk->sk_recv_queue)) != NULL) {
		skb_dst_set(skb, NULL);
		kfree_skb(skb);
	}
}

static void abort_arp_failure(void *handle, struct sk_buff *skb)
{
	struct cpl_abort_req *req = cplhdr(skb);
	struct chtls_dev *cdev;

	cdev = (struct chtls_dev *)handle;
	req->cmd = CPL_ABORT_NO_RST;
	cxgb4_ofld_send(cdev->lldi->ports[0], skb);
}

static struct sk_buff *alloc_ctrl_skb(struct sk_buff *skb, int len)
{
	if (likely(skb && !skb_shared(skb) && !skb_cloned(skb))) {
		__skb_trim(skb, 0);
		refcount_inc(&skb->users);
	} else {
		skb = alloc_skb(len, GFP_KERNEL | __GFP_NOFAIL);
	}
	return skb;
}

static void chtls_send_abort(struct sock *sk, int mode, struct sk_buff *skb)
{
	struct cpl_abort_req *req;
	struct chtls_sock *csk;
	struct tcp_sock *tp;

	csk = rcu_dereference_sk_user_data(sk);
	tp = tcp_sk(sk);

	if (!skb)
		skb = alloc_ctrl_skb(csk->txdata_skb_cache, sizeof(*req));

	req = (struct cpl_abort_req *)skb_put(skb, sizeof(*req));
	INIT_TP_WR_CPL(req, CPL_ABORT_REQ, csk->tid);
	skb_set_queue_mapping(skb, (csk->txq_idx << 1) | CPL_PRIORITY_DATA);
	req->rsvd0 = htonl(tp->snd_nxt);
	req->rsvd1 = !csk_flag_nochk(csk, CSK_TX_DATA_SENT);
	req->cmd = mode;
	t4_set_arp_err_handler(skb, csk->cdev, abort_arp_failure);
	send_or_defer(sk, tp, skb, mode == CPL_ABORT_SEND_RST);
}

static void chtls_send_reset(struct sock *sk, int mode, struct sk_buff *skb)
{
	struct chtls_sock *csk = rcu_dereference_sk_user_data(sk);

	if (unlikely(csk_flag_nochk(csk, CSK_ABORT_SHUTDOWN) ||
		     !csk->cdev)) {
		if (sk->sk_state == TCP_SYN_RECV)
			csk_set_flag(csk, CSK_RST_ABORTED);
		goto out;
	}

	if (!csk_flag_nochk(csk, CSK_TX_DATA_SENT)) {
		struct tcp_sock *tp = tcp_sk(sk);

		if (send_tx_flowc_wr(sk, 0, tp->snd_nxt, tp->rcv_nxt) < 0)
			WARN_ONCE(1, "send tx flowc error");
		csk_set_flag(csk, CSK_TX_DATA_SENT);
	}

	csk_set_flag(csk, CSK_ABORT_RPL_PENDING);
	chtls_purge_write_queue(sk);

	csk_set_flag(csk, CSK_ABORT_SHUTDOWN);
	if (sk->sk_state != TCP_SYN_RECV)
		chtls_send_abort(sk, mode, skb);
	else
		goto out;

	return;
out:
	if (skb)
		kfree_skb(skb);
}

static void release_tcp_port(struct sock *sk)
{
	if (inet_csk(sk)->icsk_bind_hash)
		inet_put_port(sk);
}

static void tcp_uncork(struct sock *sk)
{
	struct tcp_sock *tp = tcp_sk(sk);

	if (tp->nonagle & TCP_NAGLE_CORK) {
		tp->nonagle &= ~TCP_NAGLE_CORK;
		chtls_tcp_push(sk, 0);
	}
}

static void chtls_close_conn(struct sock *sk)
{
	struct cpl_close_con_req *req;
	struct chtls_sock *csk;
	struct sk_buff *skb;
	unsigned int tid;
	unsigned int len;

	len = roundup(sizeof(struct cpl_close_con_req), 16);
	csk = rcu_dereference_sk_user_data(sk);
	tid = csk->tid;

	skb = alloc_skb(len, GFP_KERNEL | __GFP_NOFAIL);
	req = (struct cpl_close_con_req *)__skb_put(skb, len);
	memset(req, 0, len);
	req->wr.wr_hi = htonl(FW_WR_OP_V(FW_TP_WR) |
			      FW_WR_IMMDLEN_V(sizeof(*req) -
					      sizeof(req->wr)));
	req->wr.wr_mid = htonl(FW_WR_LEN16_V(DIV_ROUND_UP(sizeof(*req), 16)) |
			       FW_WR_FLOWID_V(tid));

	OPCODE_TID(req) = htonl(MK_OPCODE_TID(CPL_CLOSE_CON_REQ, tid));

	tcp_uncork(sk);
	skb_entail(sk, skb, ULPCB_FLAG_NO_HDR | ULPCB_FLAG_NO_APPEND);
	if (sk->sk_state != TCP_SYN_SENT)
		chtls_push_frames(csk, 1);
}

/*
 * Perform a state transition during close and return the actions indicated
 * for the transition.  Do not make this function inline, the main reason
 * it exists at all is to avoid multiple inlining of tcp_set_state.
 */
static int make_close_transition(struct sock *sk)
{
	int next = (int)new_state[sk->sk_state];

	tcp_set_state(sk, next & TCP_STATE_MASK);
	return next & TCP_ACTION_FIN;
}

void chtls_close(struct sock *sk, long timeout)
{
	int data_lost, prev_state;
	struct chtls_sock *csk;

	csk = rcu_dereference_sk_user_data(sk);

	lock_sock(sk);
	sk->sk_shutdown |= SHUTDOWN_MASK;

	data_lost = skb_queue_len(&sk->sk_receive_queue);
	data_lost |= skb_queue_len(&csk->tlshws.sk_recv_queue);
	chtls_purge_recv_queue(sk);
	chtls_purge_receive_queue(sk);

	if (sk->sk_state == TCP_CLOSE) {
		goto wait;
	} else if (data_lost || sk->sk_state == TCP_SYN_SENT) {
		chtls_send_reset(sk, CPL_ABORT_SEND_RST, NULL);
		release_tcp_port(sk);
		goto unlock;
	} else if (sock_flag(sk, SOCK_LINGER) && !sk->sk_lingertime) {
		sk->sk_prot->disconnect(sk, 0);
	} else if (make_close_transition(sk)) {
		chtls_close_conn(sk);
	}
wait:
	if (timeout)
		sk_stream_wait_close(sk, timeout);

unlock:
	prev_state = sk->sk_state;
	sock_hold(sk);
	sock_orphan(sk);

	release_sock(sk);

	local_bh_disable();
	bh_lock_sock(sk);

	if (prev_state != TCP_CLOSE && sk->sk_state == TCP_CLOSE)
		goto out;

	if (sk->sk_state == TCP_FIN_WAIT2 && tcp_sk(sk)->linger2 < 0 &&
	    !csk_flag(sk, CSK_ABORT_SHUTDOWN)) {
		struct sk_buff *skb;

		skb = alloc_skb(sizeof(struct cpl_abort_req), GFP_ATOMIC);
		if (skb)
			chtls_send_reset(sk, CPL_ABORT_SEND_RST, skb);
	}

	if (sk->sk_state == TCP_CLOSE)
		inet_csk_destroy_sock(sk);

out:
	bh_unlock_sock(sk);
	local_bh_enable();
	sock_put(sk);
}

/*
 * Wait until a socket enters on of the given states.
 */
static int wait_for_states(struct sock *sk, unsigned int states)
{
	DECLARE_WAITQUEUE(wait, current);
	struct socket_wq _sk_wq;
	long current_timeo;
	int err = 0;

	current_timeo = 200;

	/*
	 * We want this to work even when there's no associated struct socket.
	 * In that case we provide a temporary wait_queue_head_t.
	 */
	if (!sk->sk_wq) {
		init_waitqueue_head(&_sk_wq.wait);
		_sk_wq.fasync_list = NULL;
		init_rcu_head_on_stack(&_sk_wq.rcu);
		RCU_INIT_POINTER(sk->sk_wq, &_sk_wq);
	}

	add_wait_queue(sk_sleep(sk), &wait);
	while (!sk_in_state(sk, states)) {
		if (!current_timeo) {
			err = -EBUSY;
			break;
		}
		if (signal_pending(current)) {
			err = sock_intr_errno(current_timeo);
			break;
		}
		set_current_state(TASK_UNINTERRUPTIBLE);
		release_sock(sk);
		if (!sk_in_state(sk, states))
			current_timeo = schedule_timeout(current_timeo);
		__set_current_state(TASK_RUNNING);
		lock_sock(sk);
	}
	remove_wait_queue(sk_sleep(sk), &wait);

	if (rcu_dereference(sk->sk_wq) == &_sk_wq)
		sk->sk_wq = NULL;
	return err;
}

int chtls_disconnect(struct sock *sk, int flags)
{
	struct chtls_sock *csk;
	struct tcp_sock *tp;
	int err;

	tp = tcp_sk(sk);
	csk = rcu_dereference_sk_user_data(sk);
	chtls_purge_recv_queue(sk);
	chtls_purge_receive_queue(sk);
	chtls_purge_write_queue(sk);

	if (sk->sk_state != TCP_CLOSE) {
		sk->sk_err = ECONNRESET;
		chtls_send_reset(sk, CPL_ABORT_SEND_RST, NULL);
		err = wait_for_states(sk, TCPF_CLOSE);
		if (err)
			return err;
	}
	chtls_purge_recv_queue(sk);
	chtls_purge_receive_queue(sk);
	tp->max_window = 0xFFFF << (tp->rx_opt.snd_wscale);
	return tcp_disconnect(sk, flags);
}

#define SHUTDOWN_ELIGIBLE_STATE (TCPF_ESTABLISHED | \
				 TCPF_SYN_RECV | TCPF_CLOSE_WAIT)
void chtls_shutdown(struct sock *sk, int how)
{
	if ((how & SEND_SHUTDOWN) &&
	    sk_in_state(sk, SHUTDOWN_ELIGIBLE_STATE) &&
	    make_close_transition(sk))
		chtls_close_conn(sk);
}

void chtls_destroy_sock(struct sock *sk)
{
	struct chtls_sock *csk;

	csk = rcu_dereference_sk_user_data(sk);
	chtls_purge_recv_queue(sk);
	csk->ulp_mode = ULP_MODE_NONE;
	chtls_purge_write_queue(sk);
	free_tls_keyid(sk);
	kref_put(&csk->kref, chtls_sock_release);
	sk->sk_prot = &tcp_prot;
	sk->sk_prot->destroy(sk);
}

static void reset_listen_child(struct sock *child)
{
	struct chtls_sock *csk = rcu_dereference_sk_user_data(child);
	struct sk_buff *skb;

	skb = alloc_ctrl_skb(csk->txdata_skb_cache,
			     sizeof(struct cpl_abort_req));

	chtls_send_reset(child, CPL_ABORT_SEND_RST, skb);
	sock_orphan(child);
	INC_ORPHAN_COUNT(child);
	if (child->sk_state == TCP_CLOSE)
		inet_csk_destroy_sock(child);
}

static void chtls_disconnect_acceptq(struct sock *listen_sk)
{
	struct request_sock **pprev;

	pprev = ACCEPT_QUEUE(listen_sk);
	while (*pprev) {
		struct request_sock *req = *pprev;

		if (req->rsk_ops == &chtls_rsk_ops) {
			struct sock *child = req->sk;

			*pprev = req->dl_next;
			sk_acceptq_removed(listen_sk);
			reqsk_put(req);
			sock_hold(child);
			local_bh_disable();
			bh_lock_sock(child);
			release_tcp_port(child);
			reset_listen_child(child);
			bh_unlock_sock(child);
			local_bh_enable();
			sock_put(child);
		} else {
			pprev = &req->dl_next;
		}
	}
}

static int listen_hashfn(const struct sock *sk)
{
	return ((unsigned long)sk >> 10) & (LISTEN_INFO_HASH_SIZE - 1);
}

static struct listen_info *listen_hash_add(struct chtls_dev *cdev,
					   struct sock *sk,
					   unsigned int stid)
{
	struct listen_info *p = kmalloc(sizeof(*p), GFP_KERNEL);

	if (p) {
		int key = listen_hashfn(sk);

		p->sk = sk;
		p->stid = stid;
		spin_lock(&cdev->listen_lock);
		p->next = cdev->listen_hash_tab[key];
		cdev->listen_hash_tab[key] = p;
		spin_unlock(&cdev->listen_lock);
	}
	return p;
}

static int listen_hash_find(struct chtls_dev *cdev,
			    struct sock *sk)
{
	struct listen_info *p;
	int stid = -1;
	int key;

	key = listen_hashfn(sk);

	spin_lock(&cdev->listen_lock);
	for (p = cdev->listen_hash_tab[key]; p; p = p->next)
		if (p->sk == sk) {
			stid = p->stid;
			break;
		}
	spin_unlock(&cdev->listen_lock);
	return stid;
}

static int listen_hash_del(struct chtls_dev *cdev,
			   struct sock *sk)
{
	struct listen_info *p, **prev;
	int stid = -1;
	int key;

	key = listen_hashfn(sk);
	prev = &cdev->listen_hash_tab[key];

	spin_lock(&cdev->listen_lock);
	for (p = *prev; p; prev = &p->next, p = p->next)
		if (p->sk == sk) {
			stid = p->stid;
			*prev = p->next;
			kfree(p);
			break;
		}
	spin_unlock(&cdev->listen_lock);
	return stid;
}

static void cleanup_syn_rcv_conn(struct sock *child, struct sock *parent)
{
	struct request_sock *req;
	struct chtls_sock *csk;

	csk = rcu_dereference_sk_user_data(child);
	req = csk->passive_reap_next;

	reqsk_queue_removed(&inet_csk(parent)->icsk_accept_queue, req);
	__skb_unlink((struct sk_buff *)&csk->synq, &csk->listen_ctx->synq);
	chtls_reqsk_free(req);
	csk->passive_reap_next = NULL;
}

static void chtls_reset_synq(struct listen_ctx *listen_ctx)
{
	struct sock *listen_sk = listen_ctx->lsk;

	while (!skb_queue_empty(&listen_ctx->synq)) {
		struct chtls_sock *csk =
			container_of((struct synq *)skb_peek
				(&listen_ctx->synq), struct chtls_sock, synq);
		struct sock *child = csk->sk;

		cleanup_syn_rcv_conn(child, listen_sk);
		sock_hold(child);
		local_bh_disable();
		bh_lock_sock(child);
		release_tcp_port(child);
		reset_listen_child(child);
		bh_unlock_sock(child);
		local_bh_enable();
		sock_put(child);
	}
}

int chtls_listen_start(struct chtls_dev *cdev, struct sock *sk)
{
	struct net_device *ndev;
	struct listen_ctx *ctx;
	struct adapter *adap;
	struct port_info *pi;
	int stid;
	int ret;

	if (sk->sk_family != PF_INET)
		return -EAGAIN;

	rcu_read_lock();
	ndev = chtls_ipv4_netdev(cdev, sk);
	rcu_read_unlock();
	if (!ndev)
		return -EBADF;

	pi = netdev_priv(ndev);
	adap = pi->adapter;
	if (!(adap->flags & FULL_INIT_DONE))
		return -EBADF;

	if (listen_hash_find(cdev, sk) >= 0)   /* already have it */
		return -EADDRINUSE;

	ctx = kmalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	__module_get(THIS_MODULE);
	ctx->lsk = sk;
	ctx->cdev = cdev;
	ctx->state = T4_LISTEN_START_PENDING;
	skb_queue_head_init(&ctx->synq);

	stid = cxgb4_alloc_stid(cdev->tids, sk->sk_family, ctx);
	if (stid < 0)
		goto free_ctx;

	sock_hold(sk);
	if (!listen_hash_add(cdev, sk, stid))
		goto free_stid;

	ret = cxgb4_create_server(ndev, stid,
				  inet_sk(sk)->inet_rcv_saddr,
				  inet_sk(sk)->inet_sport, 0,
				  cdev->lldi->rxq_ids[0]);
	if (ret > 0)
		ret = net_xmit_errno(ret);
	if (ret)
		goto del_hash;
	return 0;
del_hash:
	listen_hash_del(cdev, sk);
free_stid:
	cxgb4_free_stid(cdev->tids, stid, sk->sk_family);
	sock_put(sk);
free_ctx:
	kfree(ctx);
	module_put(THIS_MODULE);
	return -EBADF;
}

void chtls_listen_stop(struct chtls_dev *cdev, struct sock *sk)
{
	struct listen_ctx *listen_ctx;
	int stid;

	stid = listen_hash_del(cdev, sk);
	if (stid < 0)
		return;

	listen_ctx = (struct listen_ctx *)lookup_stid(cdev->tids, stid);
	chtls_reset_synq(listen_ctx);

	cxgb4_remove_server(cdev->lldi->ports[0], stid,
			    cdev->lldi->rxq_ids[0], 0);
	chtls_disconnect_acceptq(sk);
}

static int chtls_pass_open_rpl(struct chtls_dev *cdev, struct sk_buff *skb)
{
	struct cpl_pass_open_rpl *rpl = cplhdr(skb) + RSS_HDR;
	unsigned int stid = GET_TID(rpl);
	struct listen_ctx *listen_ctx;

	listen_ctx = (struct listen_ctx *)lookup_stid(cdev->tids, stid);
	if (!listen_ctx)
		return CPL_RET_BUF_DONE;

	if (listen_ctx->state == T4_LISTEN_START_PENDING) {
		listen_ctx->state = T4_LISTEN_STARTED;
		return CPL_RET_BUF_DONE;
	}

	if (rpl->status != CPL_ERR_NONE) {
		pr_info("Unexpected PASS_OPEN_RPL status %u for STID %u\n",
			rpl->status, stid);
	} else {
		cxgb4_free_stid(cdev->tids, stid, listen_ctx->lsk->sk_family);
		sock_put(listen_ctx->lsk);
		kfree(listen_ctx);
		module_put(THIS_MODULE);
	}
	return CPL_RET_BUF_DONE;
}

static int chtls_close_listsrv_rpl(struct chtls_dev *cdev, struct sk_buff *skb)
{
	struct cpl_close_listsvr_rpl *rpl = cplhdr(skb) + RSS_HDR;
	struct listen_ctx *listen_ctx;
	unsigned int stid;
	void *data;

	stid = GET_TID(rpl);
	data = lookup_stid(cdev->tids, stid);
	listen_ctx = (struct listen_ctx *)data;

	if (rpl->status != CPL_ERR_NONE) {
		pr_info("Unexpected CLOSE_LISTSRV_RPL status %u for STID %u\n",
			rpl->status, stid);
	} else {
		cxgb4_free_stid(cdev->tids, stid, listen_ctx->lsk->sk_family);
		sock_put(listen_ctx->lsk);
		kfree(listen_ctx);
		module_put(THIS_MODULE);
	}
	return CPL_RET_BUF_DONE;
}

static void chtls_purge_wr_queue(struct sock *sk)
{
	struct sk_buff *skb;

	while ((skb = dequeue_wr(sk)) != NULL)
		kfree_skb(skb);
}

static void chtls_release_resources(struct sock *sk)
{
	struct chtls_sock *csk = rcu_dereference_sk_user_data(sk);
	struct chtls_dev *cdev = csk->cdev;
	unsigned int tid = csk->tid;
	struct tid_info *tids;

	if (!cdev)
		return;

	tids = cdev->tids;
	kfree_skb(csk->txdata_skb_cache);
	csk->txdata_skb_cache = NULL;

	if (csk->wr_credits != csk->wr_max_credits) {
		chtls_purge_wr_queue(sk);
		chtls_reset_wr_list(csk);
	}

	if (csk->l2t_entry) {
		cxgb4_l2t_release(csk->l2t_entry);
		csk->l2t_entry = NULL;
	}

	cxgb4_remove_tid(tids, csk->port_id, tid, sk->sk_family);
	sock_put(sk);
}

static void chtls_conn_done(struct sock *sk)
{
	if (sock_flag(sk, SOCK_DEAD))
		chtls_purge_receive_queue(sk);
	sk_wakeup_sleepers(sk, 0);
	tcp_done(sk);
}

static void do_abort_syn_rcv(struct sock *child, struct sock *parent)
{
	/*
	 * If the server is still open we clean up the child connection,
	 * otherwise the server already did the clean up as it was purging
	 * its SYN queue and the skb was just sitting in its backlog.
	 */
	if (likely(parent->sk_state == TCP_LISTEN)) {
		cleanup_syn_rcv_conn(child, parent);
		/* Without the below call to sock_orphan,
		 * we leak the socket resource with syn_flood test
		 * as inet_csk_destroy_sock will not be called
		 * in tcp_done since SOCK_DEAD flag is not set.
		 * Kernel handles this differently where new socket is
		 * created only after 3 way handshake is done.
		 */
		sock_orphan(child);
		percpu_counter_inc((child)->sk_prot->orphan_count);
		chtls_release_resources(child);
		chtls_conn_done(child);
	} else {
		if (csk_flag(child, CSK_RST_ABORTED)) {
			chtls_release_resources(child);
			chtls_conn_done(child);
		}
	}
}

static void pass_open_abort(struct sock *child, struct sock *parent,
			    struct sk_buff *skb)
{
	do_abort_syn_rcv(child, parent);
	kfree_skb(skb);
}

static void bl_pass_open_abort(struct sock *lsk, struct sk_buff *skb)
{
	pass_open_abort(skb->sk, lsk, skb);
}

static void chtls_pass_open_arp_failure(struct sock *sk,
					struct sk_buff *skb)
{
	const struct request_sock *oreq;
	struct chtls_sock *csk;
	struct chtls_dev *cdev;
	struct sock *parent;
	void *data;

	csk = rcu_dereference_sk_user_data(sk);
	cdev = csk->cdev;

	/*
	 * If the connection is being aborted due to the parent listening
	 * socket going away there's nothing to do, the ABORT_REQ will close
	 * the connection.
	 */
	if (csk_flag(sk, CSK_ABORT_RPL_PENDING)) {
		kfree_skb(skb);
		return;
	}

	oreq = csk->passive_reap_next;
	data = lookup_stid(cdev->tids, oreq->ts_recent);
	parent = ((struct listen_ctx *)data)->lsk;

	bh_lock_sock(parent);
	if (!sock_owned_by_user(parent)) {
		pass_open_abort(sk, parent, skb);
	} else {
		BLOG_SKB_CB(skb)->backlog_rcv = bl_pass_open_abort;
		__sk_add_backlog(parent, skb);
	}
	bh_unlock_sock(parent);
}

static void chtls_accept_rpl_arp_failure(void *handle,
					 struct sk_buff *skb)
{
	struct sock *sk = (struct sock *)handle;

	sock_hold(sk);
	process_cpl_msg(chtls_pass_open_arp_failure, sk, skb);
	sock_put(sk);
}

static unsigned int chtls_select_mss(const struct chtls_sock *csk,
				     unsigned int pmtu,
				     struct cpl_pass_accept_req *req)
{
	struct chtls_dev *cdev;
	struct dst_entry *dst;
	unsigned int tcpoptsz;
	unsigned int iphdrsz;
	unsigned int mtu_idx;
	struct tcp_sock *tp;
	unsigned int mss;
	struct sock *sk;

	mss = ntohs(req->tcpopt.mss);
	sk = csk->sk;
	dst = __sk_dst_get(sk);
	cdev = csk->cdev;
	tp = tcp_sk(sk);
	tcpoptsz = 0;

	iphdrsz = sizeof(struct iphdr) + sizeof(struct tcphdr);
	if (req->tcpopt.tstamp)
		tcpoptsz += round_up(TCPOLEN_TIMESTAMP, 4);

	tp->advmss = dst_metric_advmss(dst);
	if (USER_MSS(tp) && tp->advmss > USER_MSS(tp))
		tp->advmss = USER_MSS(tp);
	if (tp->advmss > pmtu - iphdrsz)
		tp->advmss = pmtu - iphdrsz;
	if (mss && tp->advmss > mss)
		tp->advmss = mss;

	tp->advmss = cxgb4_best_aligned_mtu(cdev->lldi->mtus,
					    iphdrsz + tcpoptsz,
					    tp->advmss - tcpoptsz,
					    8, &mtu_idx);
	tp->advmss -= iphdrsz;

	inet_csk(sk)->icsk_pmtu_cookie = pmtu;
	return mtu_idx;
}

static unsigned int select_rcv_wnd(struct chtls_sock *csk)
{
	unsigned int rcvwnd;
	unsigned int wnd;
	struct sock *sk;

	sk = csk->sk;
	wnd = tcp_full_space(sk);

	if (wnd < MIN_RCV_WND)
		wnd = MIN_RCV_WND;

	rcvwnd = MAX_RCV_WND;

	csk_set_flag(csk, CSK_UPDATE_RCV_WND);
	return min(wnd, rcvwnd);
}

static unsigned int select_rcv_wscale(int space, int wscale_ok, int win_clamp)
{
	int wscale = 0;

	if (space > MAX_RCV_WND)
		space = MAX_RCV_WND;
	if (win_clamp && win_clamp < space)
		space = win_clamp;

	if (wscale_ok) {
		while (wscale < 14 && (65535 << wscale) < space)
			wscale++;
	}
	return wscale;
}

static void chtls_pass_accept_rpl(struct sk_buff *skb,
				  struct cpl_pass_accept_req *req,
				  unsigned int tid)

{
	struct cpl_t5_pass_accept_rpl *rpl5;
	struct cxgb4_lld_info *lldi;
	const struct tcphdr *tcph;
	const struct tcp_sock *tp;
	struct chtls_sock *csk;
	unsigned int len;
	struct sock *sk;
	u32 opt2, hlen;
	u64 opt0;

	sk = skb->sk;
	tp = tcp_sk(sk);
	csk = sk->sk_user_data;
	csk->tid = tid;
	lldi = csk->cdev->lldi;
	len = roundup(sizeof(*rpl5), 16);

	rpl5 = __skb_put_zero(skb, len);
	INIT_TP_WR(rpl5, tid);

	OPCODE_TID(rpl5) = cpu_to_be32(MK_OPCODE_TID(CPL_PASS_ACCEPT_RPL,
						     csk->tid));
	csk->mtu_idx = chtls_select_mss(csk, dst_mtu(__sk_dst_get(sk)),
					req);
	opt0 = TCAM_BYPASS_F |
	       WND_SCALE_V((tp)->rx_opt.rcv_wscale) |
	       MSS_IDX_V(csk->mtu_idx) |
	       L2T_IDX_V(csk->l2t_entry->idx) |
	       NAGLE_V(!(tp->nonagle & TCP_NAGLE_OFF)) |
	       TX_CHAN_V(csk->tx_chan) |
	       SMAC_SEL_V(csk->smac_idx) |
	       DSCP_V(csk->tos >> 2) |
	       ULP_MODE_V(ULP_MODE_TLS) |
	       RCV_BUFSIZ_V(min(tp->rcv_wnd >> 10, RCV_BUFSIZ_M));

	opt2 = RX_CHANNEL_V(0) |
		RSS_QUEUE_VALID_F | RSS_QUEUE_V(csk->rss_qid);

	if (!is_t5(lldi->adapter_type))
		opt2 |= RX_FC_DISABLE_F;
	if (req->tcpopt.tstamp)
		opt2 |= TSTAMPS_EN_F;
	if (req->tcpopt.sack)
		opt2 |= SACK_EN_F;
	hlen = ntohl(req->hdr_len);

	tcph = (struct tcphdr *)((u8 *)(req + 1) +
			T6_ETH_HDR_LEN_G(hlen) + T6_IP_HDR_LEN_G(hlen));
	if (tcph->ece && tcph->cwr)
		opt2 |= CCTRL_ECN_V(1);
	opt2 |= CONG_CNTRL_V(CONG_ALG_NEWRENO);
	opt2 |= T5_ISS_F;
	opt2 |= T5_OPT_2_VALID_F;
	rpl5->opt0 = cpu_to_be64(opt0);
	rpl5->opt2 = cpu_to_be32(opt2);
	rpl5->iss = cpu_to_be32((prandom_u32() & ~7UL) - 1);
	set_wr_txq(skb, CPL_PRIORITY_SETUP, csk->port_id);
	t4_set_arp_err_handler(skb, sk, chtls_accept_rpl_arp_failure);
	cxgb4_l2t_send(csk->egress_dev, skb, csk->l2t_entry);
}

static void inet_inherit_port(struct inet_hashinfo *hash_info,
			      struct sock *lsk, struct sock *newsk)
{
	local_bh_disable();
	__inet_inherit_port(lsk, newsk);
	local_bh_enable();
}

static int chtls_backlog_rcv(struct sock *sk, struct sk_buff *skb)
{
	if (skb->protocol) {
		kfree_skb(skb);
		return 0;
	}
	BLOG_SKB_CB(skb)->backlog_rcv(sk, skb);
	return 0;
}

static struct sock *chtls_recv_sock(struct sock *lsk,
				    struct request_sock *oreq,
				    void *network_hdr,
				    const struct cpl_pass_accept_req *req,
				    struct chtls_dev *cdev)
{
	struct adapter *adap = pci_get_drvdata(cdev->pdev);
	const struct tcphdr *tcph;
	struct inet_sock *newinet;
	const struct iphdr *iph;
	struct net_device *ndev;
	struct chtls_sock *csk;
	struct dst_entry *dst;
	struct neighbour *n;
	struct tcp_sock *tp;
	struct sock *newsk;
	bool found = false;
	u16 port_id;
	int rxq_idx;
	int step, i;

	iph = (const struct iphdr *)network_hdr;
	newsk = tcp_create_openreq_child(lsk, oreq, cdev->askb);
	if (!newsk)
		goto free_oreq;

	dst = inet_csk_route_child_sock(lsk, newsk, oreq);
	if (!dst)
		goto free_sk;

	tcph = (struct tcphdr *)(iph + 1);
	n = dst_neigh_lookup(dst, &iph->saddr);
	if (!n || !n->dev)
		goto free_sk;

	ndev = n->dev;
	if (!ndev)
		goto free_dst;
	if (is_vlan_dev(ndev))
		ndev = vlan_dev_real_dev(ndev);

	for_each_port(adap, i)
		if (cdev->ports[i] == ndev)
			found = true;

	if (!found)
		goto free_dst;

	port_id = cxgb4_port_idx(ndev);

	csk = chtls_sock_create(cdev);
	if (!csk)
		goto free_dst;

	csk->l2t_entry = cxgb4_l2t_get(cdev->lldi->l2t, n, ndev, 0);
	if (!csk->l2t_entry)
		goto free_csk;

	newsk->sk_user_data = csk;
	newsk->sk_backlog_rcv = chtls_backlog_rcv;

	tp = tcp_sk(newsk);
	newinet = inet_sk(newsk);

	newinet->inet_daddr = iph->saddr;
	newinet->inet_rcv_saddr = iph->daddr;
	newinet->inet_saddr = iph->daddr;

	oreq->ts_recent = PASS_OPEN_TID_G(ntohl(req->tos_stid));
	sk_setup_caps(newsk, dst);
	newsk->sk_prot_creator = lsk->sk_prot_creator;
	csk->sk = newsk;
	csk->passive_reap_next = oreq;
	csk->tx_chan = cxgb4_port_chan(ndev);
	csk->port_id = port_id;
	csk->egress_dev = ndev;
	csk->tos = PASS_OPEN_TOS_G(ntohl(req->tos_stid));
	csk->ulp_mode = ULP_MODE_TLS;
	step = cdev->lldi->nrxq / cdev->lldi->nchan;
	csk->rss_qid = cdev->lldi->rxq_ids[port_id * step];
	rxq_idx = port_id * step;
	csk->txq_idx = (rxq_idx < cdev->lldi->ntxq) ? rxq_idx :
			port_id * step;
	csk->sndbuf = newsk->sk_sndbuf;
	csk->smac_idx = cxgb4_tp_smt_idx(cdev->lldi->adapter_type,
					 cxgb4_port_viid(ndev));
	tp->rcv_wnd = select_rcv_wnd(csk);
	RCV_WSCALE(tp) = select_rcv_wscale(tcp_full_space(newsk),
					   WSCALE_OK(tp),
					   tp->window_clamp);
	neigh_release(n);
	inet_inherit_port(&tcp_hashinfo, lsk, newsk);
	csk_set_flag(csk, CSK_CONN_INLINE);
	bh_unlock_sock(newsk); /* tcp_create_openreq_child ->sk_clone_lock */

	return newsk;
free_csk:
	chtls_sock_release(&csk->kref);
free_dst:
	neigh_release(n);
	dst_release(dst);
free_sk:
	inet_csk_prepare_forced_close(newsk);
	tcp_done(newsk);
free_oreq:
	chtls_reqsk_free(oreq);
	return NULL;
}

/*
 * Populate a TID_RELEASE WR.  The skb must be already propely sized.
 */
static  void mk_tid_release(struct sk_buff *skb,
			    unsigned int chan, unsigned int tid)
{
	struct cpl_tid_release *req;
	unsigned int len;

	len = roundup(sizeof(struct cpl_tid_release), 16);
	req = (struct cpl_tid_release *)__skb_put(skb, len);
	memset(req, 0, len);
	set_wr_txq(skb, CPL_PRIORITY_SETUP, chan);
	INIT_TP_WR_CPL(req, CPL_TID_RELEASE, tid);
}

static int chtls_get_module(struct sock *sk)
{
	struct inet_connection_sock *icsk = inet_csk(sk);

	if (!try_module_get(icsk->icsk_ulp_ops->owner))
		return -1;

	return 0;
}

static void chtls_pass_accept_request(struct sock *sk,
				      struct sk_buff *skb)
{
	struct cpl_t5_pass_accept_rpl *rpl;
	struct cpl_pass_accept_req *req;
	struct listen_ctx *listen_ctx;
	struct request_sock *oreq;
	struct sk_buff *reply_skb;
	struct chtls_sock *csk;
	struct chtls_dev *cdev;
	struct tcphdr *tcph;
	struct sock *newsk;
	struct ethhdr *eh;
	struct iphdr *iph;
	void *network_hdr;
	unsigned int stid;
	unsigned int len;
	unsigned int tid;

	req = cplhdr(skb) + RSS_HDR;
	tid = GET_TID(req);
	cdev = BLOG_SKB_CB(skb)->cdev;
	newsk = lookup_tid(cdev->tids, tid);
	stid = PASS_OPEN_TID_G(ntohl(req->tos_stid));
	if (newsk) {
		pr_info("tid (%d) already in use\n", tid);
		return;
	}

	len = roundup(sizeof(*rpl), 16);
	reply_skb = alloc_skb(len, GFP_ATOMIC);
	if (!reply_skb) {
		cxgb4_remove_tid(cdev->tids, 0, tid, sk->sk_family);
		kfree_skb(skb);
		return;
	}

	if (sk->sk_state != TCP_LISTEN)
		goto reject;

	if (inet_csk_reqsk_queue_is_full(sk))
		goto reject;

	if (sk_acceptq_is_full(sk))
		goto reject;

	oreq = inet_reqsk_alloc(&chtls_rsk_ops, sk, true);
	if (!oreq)
		goto reject;

	oreq->rsk_rcv_wnd = 0;
	oreq->rsk_window_clamp = 0;
	oreq->cookie_ts = 0;
	oreq->mss = 0;
	oreq->ts_recent = 0;

	eh = (struct ethhdr *)(req + 1);
	iph = (struct iphdr *)(eh + 1);
	if (iph->version != 0x4)
		goto free_oreq;

	network_hdr = (void *)(eh + 1);
	tcph = (struct tcphdr *)(iph + 1);

	tcp_rsk(oreq)->tfo_listener = false;
	tcp_rsk(oreq)->rcv_isn = ntohl(tcph->seq);
	chtls_set_req_port(oreq, tcph->source, tcph->dest);
	inet_rsk(oreq)->ecn_ok = 0;
	chtls_set_req_addr(oreq, iph->daddr, iph->saddr);
	if (req->tcpopt.wsf <= 14) {
		inet_rsk(oreq)->wscale_ok = 1;
		inet_rsk(oreq)->snd_wscale = req->tcpopt.wsf;
	}
	inet_rsk(oreq)->ir_iif = sk->sk_bound_dev_if;

	newsk = chtls_recv_sock(sk, oreq, network_hdr, req, cdev);
	if (!newsk)
		goto reject;

	if (chtls_get_module(newsk))
		goto reject;
	inet_csk_reqsk_queue_added(sk);
	reply_skb->sk = newsk;
	chtls_install_cpl_ops(newsk);
	cxgb4_insert_tid(cdev->tids, newsk, tid, newsk->sk_family);
	csk = rcu_dereference_sk_user_data(newsk);
	listen_ctx = (struct listen_ctx *)lookup_stid(cdev->tids, stid);
	csk->listen_ctx = listen_ctx;
	__skb_queue_tail(&listen_ctx->synq, (struct sk_buff *)&csk->synq);
	chtls_pass_accept_rpl(reply_skb, req, tid);
	kfree_skb(skb);
	return;

free_oreq:
	chtls_reqsk_free(oreq);
reject:
	mk_tid_release(reply_skb, 0, tid);
	cxgb4_ofld_send(cdev->lldi->ports[0], reply_skb);
	kfree_skb(skb);
}

/*
 * Handle a CPL_PASS_ACCEPT_REQ message.
 */
static int chtls_pass_accept_req(struct chtls_dev *cdev, struct sk_buff *skb)
{
	struct cpl_pass_accept_req *req = cplhdr(skb) + RSS_HDR;
	struct listen_ctx *ctx;
	unsigned int stid;
	unsigned int tid;
	struct sock *lsk;
	void *data;

	stid = PASS_OPEN_TID_G(ntohl(req->tos_stid));
	tid = GET_TID(req);

	data = lookup_stid(cdev->tids, stid);
	if (!data)
		return 1;

	ctx = (struct listen_ctx *)data;
	lsk = ctx->lsk;

	if (unlikely(tid >= cdev->tids->ntids)) {
		pr_info("passive open TID %u too large\n", tid);
		return 1;
	}

	BLOG_SKB_CB(skb)->cdev = cdev;
	process_cpl_msg(chtls_pass_accept_request, lsk, skb);
	return 0;
}

/*
 * Completes some final bits of initialization for just established connections
 * and changes their state to TCP_ESTABLISHED.
 *
 * snd_isn here is the ISN after the SYN, i.e., the true ISN + 1.
 */
static void make_established(struct sock *sk, u32 snd_isn, unsigned int opt)
{
	struct tcp_sock *tp = tcp_sk(sk);

	tp->pushed_seq = snd_isn;
	tp->write_seq = snd_isn;
	tp->snd_nxt = snd_isn;
	tp->snd_una = snd_isn;
	inet_sk(sk)->inet_id = prandom_u32();
	assign_rxopt(sk, opt);

	if (tp->rcv_wnd > (RCV_BUFSIZ_M << 10))
		tp->rcv_wup -= tp->rcv_wnd - (RCV_BUFSIZ_M << 10);

	smp_mb();
	tcp_set_state(sk, TCP_ESTABLISHED);
}

static void chtls_abort_conn(struct sock *sk, struct sk_buff *skb)
{
	struct sk_buff *abort_skb;

	abort_skb = alloc_skb(sizeof(struct cpl_abort_req), GFP_ATOMIC);
	if (abort_skb)
		chtls_send_reset(sk, CPL_ABORT_SEND_RST, abort_skb);
}

static struct sock *reap_list;
static DEFINE_SPINLOCK(reap_list_lock);

/*
 * Process the reap list.
 */
DECLARE_TASK_FUNC(process_reap_list, task_param)
{
	spin_lock_bh(&reap_list_lock);
	while (reap_list) {
		struct sock *sk = reap_list;
		struct chtls_sock *csk = rcu_dereference_sk_user_data(sk);

		reap_list = csk->passive_reap_next;
		csk->passive_reap_next = NULL;
		spin_unlock(&reap_list_lock);
		sock_hold(sk);

		bh_lock_sock(sk);
		chtls_abort_conn(sk, NULL);
		sock_orphan(sk);
		if (sk->sk_state == TCP_CLOSE)
			inet_csk_destroy_sock(sk);
		bh_unlock_sock(sk);
		sock_put(sk);
		spin_lock(&reap_list_lock);
	}
	spin_unlock_bh(&reap_list_lock);
}

static DECLARE_WORK(reap_task, process_reap_list);

static void add_to_reap_list(struct sock *sk)
{
	struct chtls_sock *csk = sk->sk_user_data;

	local_bh_disable();
	release_tcp_port(sk); /* release the port immediately */

	spin_lock(&reap_list_lock);
	csk->passive_reap_next = reap_list;
	reap_list = sk;
	if (!csk->passive_reap_next)
		schedule_work(&reap_task);
	spin_unlock(&reap_list_lock);
	local_bh_enable();
}

static void add_pass_open_to_parent(struct sock *child, struct sock *lsk,
				    struct chtls_dev *cdev)
{
	struct request_sock *oreq;
	struct chtls_sock *csk;

	if (lsk->sk_state != TCP_LISTEN)
		return;

	csk = child->sk_user_data;
	oreq = csk->passive_reap_next;
	csk->passive_reap_next = NULL;

	reqsk_queue_removed(&inet_csk(lsk)->icsk_accept_queue, oreq);
	__skb_unlink((struct sk_buff *)&csk->synq, &csk->listen_ctx->synq);

	if (sk_acceptq_is_full(lsk)) {
		chtls_reqsk_free(oreq);
		add_to_reap_list(child);
	} else {
		refcount_set(&oreq->rsk_refcnt, 1);
		inet_csk_reqsk_queue_add(lsk, oreq, child);
		lsk->sk_data_ready(lsk);
	}
}

static void bl_add_pass_open_to_parent(struct sock *lsk, struct sk_buff *skb)
{
	struct sock *child = skb->sk;

	skb->sk = NULL;
	add_pass_open_to_parent(child, lsk, BLOG_SKB_CB(skb)->cdev);
	kfree_skb(skb);
}

static int chtls_pass_establish(struct chtls_dev *cdev, struct sk_buff *skb)
{
	struct cpl_pass_establish *req = cplhdr(skb) + RSS_HDR;
	struct chtls_sock *csk;
	struct sock *lsk, *sk;
	unsigned int hwtid;

	hwtid = GET_TID(req);
	sk = lookup_tid(cdev->tids, hwtid);
	if (!sk)
		return (CPL_RET_UNKNOWN_TID | CPL_RET_BUF_DONE);

	bh_lock_sock(sk);
	if (unlikely(sock_owned_by_user(sk))) {
		kfree_skb(skb);
	} else {
		unsigned int stid;
		void *data;

		csk = sk->sk_user_data;
		csk->wr_max_credits = 64;
		csk->wr_credits = 64;
		csk->wr_unacked = 0;
		make_established(sk, ntohl(req->snd_isn), ntohs(req->tcp_opt));
		stid = PASS_OPEN_TID_G(ntohl(req->tos_stid));
		sk->sk_state_change(sk);
		if (unlikely(sk->sk_socket))
			sk_wake_async(sk, 0, POLL_OUT);

		data = lookup_stid(cdev->tids, stid);
		if (!data) {
			/* listening server close */
			kfree_skb(skb);
			goto unlock;
		}
		lsk = ((struct listen_ctx *)data)->lsk;

		bh_lock_sock(lsk);
		if (unlikely(skb_queue_empty(&csk->listen_ctx->synq))) {
			/* removed from synq */
			bh_unlock_sock(lsk);
			kfree_skb(skb);
			goto unlock;
		}

		if (likely(!sock_owned_by_user(lsk))) {
			kfree_skb(skb);
			add_pass_open_to_parent(sk, lsk, cdev);
		} else {
			skb->sk = sk;
			BLOG_SKB_CB(skb)->cdev = cdev;
			BLOG_SKB_CB(skb)->backlog_rcv =
				bl_add_pass_open_to_parent;
			__sk_add_backlog(lsk, skb);
		}
		bh_unlock_sock(lsk);
	}
unlock:
	bh_unlock_sock(sk);
	return 0;
}

/*
 * Handle receipt of an urgent pointer.
 */
static void handle_urg_ptr(struct sock *sk, u32 urg_seq)
{
	struct tcp_sock *tp = tcp_sk(sk);

	urg_seq--;
	if (tp->urg_data && !after(urg_seq, tp->urg_seq))
		return;	/* duplicate pointer */

	sk_send_sigurg(sk);
	if (tp->urg_seq == tp->copied_seq && tp->urg_data &&
	    !sock_flag(sk, SOCK_URGINLINE) &&
	    tp->copied_seq != tp->rcv_nxt) {
		struct sk_buff *skb = skb_peek(&sk->sk_receive_queue);

		tp->copied_seq++;
		if (skb && tp->copied_seq - ULP_SKB_CB(skb)->seq >= skb->len)
			chtls_free_skb(sk, skb);
	}

	tp->urg_data = TCP_URG_NOTYET;
	tp->urg_seq = urg_seq;
}

static void check_sk_callbacks(struct chtls_sock *csk)
{
	struct sock *sk = csk->sk;

	if (unlikely(sk->sk_user_data &&
		     !csk_flag_nochk(csk, CSK_CALLBACKS_CHKD)))
		csk_set_flag(csk, CSK_CALLBACKS_CHKD);
}

/*
 * Handles Rx data that arrives in a state where the socket isn't accepting
 * new data.
 */
static void handle_excess_rx(struct sock *sk, struct sk_buff *skb)
{
	if (!csk_flag(sk, CSK_ABORT_SHUTDOWN))
		chtls_abort_conn(sk, skb);

	kfree_skb(skb);
}

static void chtls_recv_data(struct sock *sk, struct sk_buff *skb)
{
	struct cpl_rx_data *hdr = cplhdr(skb) + RSS_HDR;
	struct chtls_sock *csk;
	struct tcp_sock *tp;

	csk = rcu_dereference_sk_user_data(sk);
	tp = tcp_sk(sk);

	if (unlikely(sk->sk_shutdown & RCV_SHUTDOWN)) {
		handle_excess_rx(sk, skb);
		return;
	}

	ULP_SKB_CB(skb)->seq = ntohl(hdr->seq);
	ULP_SKB_CB(skb)->psh = hdr->psh;
	skb_ulp_mode(skb) = ULP_MODE_NONE;

	skb_reset_transport_header(skb);
	__skb_pull(skb, sizeof(*hdr) + RSS_HDR);
	if (!skb->data_len)
		__skb_trim(skb, ntohs(hdr->len));

	if (unlikely(hdr->urg))
		handle_urg_ptr(sk, tp->rcv_nxt + ntohs(hdr->urg));
	if (unlikely(tp->urg_data == TCP_URG_NOTYET &&
		     tp->urg_seq - tp->rcv_nxt < skb->len))
		tp->urg_data = TCP_URG_VALID |
			       skb->data[tp->urg_seq - tp->rcv_nxt];

	if (unlikely(hdr->dack_mode != csk->delack_mode)) {
		csk->delack_mode = hdr->dack_mode;
		csk->delack_seq = tp->rcv_nxt;
	}

	tcp_hdr(skb)->fin = 0;
	tp->rcv_nxt += skb->len;

	__skb_queue_tail(&sk->sk_receive_queue, skb);

	if (!sock_flag(sk, SOCK_DEAD)) {
		check_sk_callbacks(csk);
		sk->sk_data_ready(sk);
	}
}

static int chtls_rx_data(struct chtls_dev *cdev, struct sk_buff *skb)
{
	struct cpl_rx_data *req = cplhdr(skb) + RSS_HDR;
	unsigned int hwtid = GET_TID(req);
	struct sock *sk;

	sk = lookup_tid(cdev->tids, hwtid);
	if (unlikely(!sk)) {
		pr_err("can't find conn. for hwtid %u.\n", hwtid);
		return -EINVAL;
	}
	skb_dst_set(skb, NULL);
	process_cpl_msg(chtls_recv_data, sk, skb);
	return 0;
}

static void chtls_recv_pdu(struct sock *sk, struct sk_buff *skb)
{
	struct cpl_tls_data *hdr = cplhdr(skb);
	struct chtls_sock *csk;
	struct chtls_hws *tlsk;
	struct tcp_sock *tp;

	csk = rcu_dereference_sk_user_data(sk);
	tlsk = &csk->tlshws;
	tp = tcp_sk(sk);

	if (unlikely(sk->sk_shutdown & RCV_SHUTDOWN)) {
		handle_excess_rx(sk, skb);
		return;
	}

	ULP_SKB_CB(skb)->seq = ntohl(hdr->seq);
	ULP_SKB_CB(skb)->flags = 0;
	skb_ulp_mode(skb) = ULP_MODE_TLS;

	skb_reset_transport_header(skb);
	__skb_pull(skb, sizeof(*hdr));
	if (!skb->data_len)
		__skb_trim(skb,
			   CPL_TLS_DATA_LENGTH_G(ntohl(hdr->length_pkd)));

	if (unlikely(tp->urg_data == TCP_URG_NOTYET && tp->urg_seq -
		     tp->rcv_nxt < skb->len))
		tp->urg_data = TCP_URG_VALID |
			       skb->data[tp->urg_seq - tp->rcv_nxt];

	tcp_hdr(skb)->fin = 0;
	tlsk->pldlen = CPL_TLS_DATA_LENGTH_G(ntohl(hdr->length_pkd));
	__skb_queue_tail(&tlsk->sk_recv_queue, skb);
}

static int chtls_rx_pdu(struct chtls_dev *cdev, struct sk_buff *skb)
{
	struct cpl_tls_data *req = cplhdr(skb);
	unsigned int hwtid = GET_TID(req);
	struct sock *sk;

	sk = lookup_tid(cdev->tids, hwtid);
	if (unlikely(!sk)) {
		pr_err("can't find conn. for hwtid %u.\n", hwtid);
		return -EINVAL;
	}
	skb_dst_set(skb, NULL);
	process_cpl_msg(chtls_recv_pdu, sk, skb);
	return 0;
}

static void chtls_set_hdrlen(struct sk_buff *skb, unsigned int nlen)
{
	struct tlsrx_cmp_hdr *tls_cmp_hdr = cplhdr(skb);

	skb->hdr_len = ntohs((__force __be16)tls_cmp_hdr->length);
	tls_cmp_hdr->length = ntohs((__force __be16)nlen);
}

static void chtls_rx_hdr(struct sock *sk, struct sk_buff *skb)
{
	struct tlsrx_cmp_hdr *tls_hdr_pkt;
	struct cpl_rx_tls_cmp *cmp_cpl;
	struct sk_buff *skb_rec;
	struct chtls_sock *csk;
	struct chtls_hws *tlsk;
	struct tcp_sock *tp;

	cmp_cpl = cplhdr(skb);
	csk = rcu_dereference_sk_user_data(sk);
	tlsk = &csk->tlshws;
	tp = tcp_sk(sk);

	ULP_SKB_CB(skb)->seq = ntohl(cmp_cpl->seq);
	ULP_SKB_CB(skb)->flags = 0;

	skb_reset_transport_header(skb);
	__skb_pull(skb, sizeof(*cmp_cpl));
	tls_hdr_pkt = (struct tlsrx_cmp_hdr *)skb->data;
	if (tls_hdr_pkt->res_to_mac_error & TLSRX_HDR_PKT_ERROR_M)
		tls_hdr_pkt->type = CONTENT_TYPE_ERROR;
	if (!skb->data_len)
		__skb_trim(skb, TLS_HEADER_LENGTH);

	tp->rcv_nxt +=
		CPL_RX_TLS_CMP_PDULENGTH_G(ntohl(cmp_cpl->pdulength_length));

	ULP_SKB_CB(skb)->flags |= ULPCB_FLAG_TLS_HDR;
	skb_rec = __skb_dequeue(&tlsk->sk_recv_queue);
	if (!skb_rec) {
		__skb_queue_tail(&sk->sk_receive_queue, skb);
	} else {
		chtls_set_hdrlen(skb, tlsk->pldlen);
		tlsk->pldlen = 0;
		__skb_queue_tail(&sk->sk_receive_queue, skb);
		__skb_queue_tail(&sk->sk_receive_queue, skb_rec);
	}

	if (!sock_flag(sk, SOCK_DEAD)) {
		check_sk_callbacks(csk);
		sk->sk_data_ready(sk);
	}
}

static int chtls_rx_cmp(struct chtls_dev *cdev, struct sk_buff *skb)
{
	struct cpl_rx_tls_cmp *req = cplhdr(skb);
	unsigned int hwtid = GET_TID(req);
	struct sock *sk;

	sk = lookup_tid(cdev->tids, hwtid);
	if (unlikely(!sk)) {
		pr_err("can't find conn. for hwtid %u.\n", hwtid);
		return -EINVAL;
	}
	skb_dst_set(skb, NULL);
	process_cpl_msg(chtls_rx_hdr, sk, skb);

	return 0;
}

static void chtls_timewait(struct sock *sk)
{
	struct tcp_sock *tp = tcp_sk(sk);

	tp->rcv_nxt++;
	tp->rx_opt.ts_recent_stamp = ktime_get_seconds();
	tp->srtt_us = 0;
	tcp_time_wait(sk, TCP_TIME_WAIT, 0);
}

static void chtls_peer_close(struct sock *sk, struct sk_buff *skb)
{
	struct chtls_sock *csk = rcu_dereference_sk_user_data(sk);

	sk->sk_shutdown |= RCV_SHUTDOWN;
	sock_set_flag(sk, SOCK_DONE);

	switch (sk->sk_state) {
	case TCP_SYN_RECV:
	case TCP_ESTABLISHED:
		tcp_set_state(sk, TCP_CLOSE_WAIT);
		break;
	case TCP_FIN_WAIT1:
		tcp_set_state(sk, TCP_CLOSING);
		break;
	case TCP_FIN_WAIT2:
		chtls_release_resources(sk);
		if (csk_flag_nochk(csk, CSK_ABORT_RPL_PENDING))
			chtls_conn_done(sk);
		else
			chtls_timewait(sk);
		break;
	default:
		pr_info("cpl_peer_close in bad state %d\n", sk->sk_state);
	}

	if (!sock_flag(sk, SOCK_DEAD)) {
		sk->sk_state_change(sk);
		/* Do not send POLL_HUP for half duplex close. */

		if ((sk->sk_shutdown & SEND_SHUTDOWN) ||
		    sk->sk_state == TCP_CLOSE)
			sk_wake_async(sk, SOCK_WAKE_WAITD, POLL_HUP);
		else
			sk_wake_async(sk, SOCK_WAKE_WAITD, POLL_IN);
	}
	kfree_skb(skb);
}

static void chtls_close_con_rpl(struct sock *sk, struct sk_buff *skb)
{
	struct cpl_close_con_rpl *rpl = cplhdr(skb) + RSS_HDR;
	struct chtls_sock *csk;
	struct tcp_sock *tp;

	csk = rcu_dereference_sk_user_data(sk);
	tp = tcp_sk(sk);

	tp->snd_una = ntohl(rpl->snd_nxt) - 1;  /* exclude FIN */

	switch (sk->sk_state) {
	case TCP_CLOSING:
		chtls_release_resources(sk);
		if (csk_flag_nochk(csk, CSK_ABORT_RPL_PENDING))
			chtls_conn_done(sk);
		else
			chtls_timewait(sk);
		break;
	case TCP_LAST_ACK:
		chtls_release_resources(sk);
		chtls_conn_done(sk);
		break;
	case TCP_FIN_WAIT1:
		tcp_set_state(sk, TCP_FIN_WAIT2);
		sk->sk_shutdown |= SEND_SHUTDOWN;

		if (!sock_flag(sk, SOCK_DEAD))
			sk->sk_state_change(sk);
		else if (tcp_sk(sk)->linger2 < 0 &&
			 !csk_flag_nochk(csk, CSK_ABORT_SHUTDOWN))
			chtls_abort_conn(sk, skb);
		break;
	default:
		pr_info("close_con_rpl in bad state %d\n", sk->sk_state);
	}
	kfree_skb(skb);
}

static struct sk_buff *get_cpl_skb(struct sk_buff *skb,
				   size_t len, gfp_t gfp)
{
	if (likely(!skb_is_nonlinear(skb) && !skb_cloned(skb))) {
		WARN_ONCE(skb->len < len, "skb alloc error");
		__skb_trim(skb, len);
		skb_get(skb);
	} else {
		skb = alloc_skb(len, gfp);
		if (skb)
			__skb_put(skb, len);
	}
	return skb;
}

static void set_abort_rpl_wr(struct sk_buff *skb, unsigned int tid,
			     int cmd)
{
	struct cpl_abort_rpl *rpl = cplhdr(skb);

	INIT_TP_WR_CPL(rpl, CPL_ABORT_RPL, tid);
	rpl->cmd = cmd;
}

static void send_defer_abort_rpl(struct chtls_dev *cdev, struct sk_buff *skb)
{
	struct cpl_abort_req_rss *req = cplhdr(skb);
	struct sk_buff *reply_skb;

	reply_skb = alloc_skb(sizeof(struct cpl_abort_rpl),
			      GFP_KERNEL | __GFP_NOFAIL);
	__skb_put(reply_skb, sizeof(struct cpl_abort_rpl));
	set_abort_rpl_wr(reply_skb, GET_TID(req),
			 (req->status & CPL_ABORT_NO_RST));
	set_wr_txq(reply_skb, CPL_PRIORITY_DATA, req->status >> 1);
	cxgb4_ofld_send(cdev->lldi->ports[0], reply_skb);
	kfree_skb(skb);
}

/*
 * Add an skb to the deferred skb queue for processing from process context.
 */
static void t4_defer_reply(struct sk_buff *skb, struct chtls_dev *cdev,
			   defer_handler_t handler)
{
	DEFERRED_SKB_CB(skb)->handler = handler;
	spin_lock_bh(&cdev->deferq.lock);
	__skb_queue_tail(&cdev->deferq, skb);
	if (skb_queue_len(&cdev->deferq) == 1)
		schedule_work(&cdev->deferq_task);
	spin_unlock_bh(&cdev->deferq.lock);
}

static void chtls_send_abort_rpl(struct sock *sk, struct sk_buff *skb,
				 struct chtls_dev *cdev,
				 int status, int queue)
{
	struct cpl_abort_req_rss *req = cplhdr(skb) + RSS_HDR;
	struct sk_buff *reply_skb;
	struct chtls_sock *csk;
	unsigned int tid;

	csk = rcu_dereference_sk_user_data(sk);
	tid = GET_TID(req);

	reply_skb = get_cpl_skb(skb, sizeof(struct cpl_abort_rpl), gfp_any());
	if (!reply_skb) {
		req->status = (queue << 1) | status;
		t4_defer_reply(skb, cdev, send_defer_abort_rpl);
		return;
	}

	set_abort_rpl_wr(reply_skb, tid, status);
	set_wr_txq(reply_skb, CPL_PRIORITY_DATA, queue);
	if (csk_conn_inline(csk)) {
		struct l2t_entry *e = csk->l2t_entry;

		if (e && sk->sk_state != TCP_SYN_RECV) {
			cxgb4_l2t_send(csk->egress_dev, reply_skb, e);
			return;
		}
	}
	cxgb4_ofld_send(cdev->lldi->ports[0], reply_skb);
	kfree_skb(skb);
}

/*
 * This is run from a listener's backlog to abort a child connection in
 * SYN_RCV state (i.e., one on the listener's SYN queue).
 */
static void bl_abort_syn_rcv(struct sock *lsk, struct sk_buff *skb)
{
	struct chtls_sock *csk;
	struct sock *child;
	int queue;

	child = skb->sk;
	csk = rcu_dereference_sk_user_data(child);
	queue = csk->txq_idx;

	skb->sk	= NULL;
	do_abort_syn_rcv(child, lsk);
	chtls_send_abort_rpl(child, skb, BLOG_SKB_CB(skb)->cdev,
			     CPL_ABORT_NO_RST, queue);
}

static int abort_syn_rcv(struct sock *sk, struct sk_buff *skb)
{
	const struct request_sock *oreq;
	struct listen_ctx *listen_ctx;
	struct chtls_sock *csk;
	struct chtls_dev *cdev;
	struct sock *psk;
	void *ctx;

	csk = sk->sk_user_data;
	oreq = csk->passive_reap_next;
	cdev = csk->cdev;

	if (!oreq)
		return -1;

	ctx = lookup_stid(cdev->tids, oreq->ts_recent);
	if (!ctx)
		return -1;

	listen_ctx = (struct listen_ctx *)ctx;
	psk = listen_ctx->lsk;

	bh_lock_sock(psk);
	if (!sock_owned_by_user(psk)) {
		int queue = csk->txq_idx;

		do_abort_syn_rcv(sk, psk);
		chtls_send_abort_rpl(sk, skb, cdev, CPL_ABORT_NO_RST, queue);
	} else {
		skb->sk = sk;
		BLOG_SKB_CB(skb)->backlog_rcv = bl_abort_syn_rcv;
		__sk_add_backlog(psk, skb);
	}
	bh_unlock_sock(psk);
	return 0;
}

static void chtls_abort_req_rss(struct sock *sk, struct sk_buff *skb)
{
	const struct cpl_abort_req_rss *req = cplhdr(skb) + RSS_HDR;
	struct chtls_sock *csk = sk->sk_user_data;
	int rst_status = CPL_ABORT_NO_RST;
	int queue = csk->txq_idx;

	if (is_neg_adv(req->status)) {
		kfree_skb(skb);
		return;
	}

	csk_reset_flag(csk, CSK_ABORT_REQ_RCVD);

	if (!csk_flag_nochk(csk, CSK_ABORT_SHUTDOWN) &&
	    !csk_flag_nochk(csk, CSK_TX_DATA_SENT)) {
		struct tcp_sock *tp = tcp_sk(sk);

		if (send_tx_flowc_wr(sk, 0, tp->snd_nxt, tp->rcv_nxt) < 0)
			WARN_ONCE(1, "send_tx_flowc error");
		csk_set_flag(csk, CSK_TX_DATA_SENT);
	}

	csk_set_flag(csk, CSK_ABORT_SHUTDOWN);

	if (!csk_flag_nochk(csk, CSK_ABORT_RPL_PENDING)) {
		sk->sk_err = ETIMEDOUT;

		if (!sock_flag(sk, SOCK_DEAD))
			sk->sk_error_report(sk);

		if (sk->sk_state == TCP_SYN_RECV && !abort_syn_rcv(sk, skb))
			return;

		chtls_release_resources(sk);
		chtls_conn_done(sk);
	}

	chtls_send_abort_rpl(sk, skb, csk->cdev, rst_status, queue);
}

static void chtls_abort_rpl_rss(struct sock *sk, struct sk_buff *skb)
{
	struct cpl_abort_rpl_rss *rpl = cplhdr(skb) + RSS_HDR;
	struct chtls_sock *csk;
	struct chtls_dev *cdev;

	csk = rcu_dereference_sk_user_data(sk);
	cdev = csk->cdev;

	if (csk_flag_nochk(csk, CSK_ABORT_RPL_PENDING)) {
		csk_reset_flag(csk, CSK_ABORT_RPL_PENDING);
		if (!csk_flag_nochk(csk, CSK_ABORT_REQ_RCVD)) {
			if (sk->sk_state == TCP_SYN_SENT) {
				cxgb4_remove_tid(cdev->tids,
						 csk->port_id,
						 GET_TID(rpl),
						 sk->sk_family);
				sock_put(sk);
			}
			chtls_release_resources(sk);
			chtls_conn_done(sk);
		}
	}
	kfree_skb(skb);
}

static int chtls_conn_cpl(struct chtls_dev *cdev, struct sk_buff *skb)
{
	struct cpl_peer_close *req = cplhdr(skb) + RSS_HDR;
	void (*fn)(struct sock *sk, struct sk_buff *skb);
	unsigned int hwtid = GET_TID(req);
	struct sock *sk;
	u8 opcode;

	opcode = ((const struct rss_header *)cplhdr(skb))->opcode;

	sk = lookup_tid(cdev->tids, hwtid);
	if (!sk)
		goto rel_skb;

	switch (opcode) {
	case CPL_PEER_CLOSE:
		fn = chtls_peer_close;
		break;
	case CPL_CLOSE_CON_RPL:
		fn = chtls_close_con_rpl;
		break;
	case CPL_ABORT_REQ_RSS:
		fn = chtls_abort_req_rss;
		break;
	case CPL_ABORT_RPL_RSS:
		fn = chtls_abort_rpl_rss;
		break;
	default:
		goto rel_skb;
	}

	process_cpl_msg(fn, sk, skb);
	return 0;

rel_skb:
	kfree_skb(skb);
	return 0;
}

static void chtls_rx_ack(struct sock *sk, struct sk_buff *skb)
{
	struct cpl_fw4_ack *hdr = cplhdr(skb) + RSS_HDR;
	struct chtls_sock *csk = sk->sk_user_data;
	struct tcp_sock *tp = tcp_sk(sk);
	u32 credits = hdr->credits;
	u32 snd_una;

	snd_una = ntohl(hdr->snd_una);
	csk->wr_credits += credits;

	if (csk->wr_unacked > csk->wr_max_credits - csk->wr_credits)
		csk->wr_unacked = csk->wr_max_credits - csk->wr_credits;

	while (credits) {
		struct sk_buff *pskb = csk->wr_skb_head;
		u32 csum;

		if (unlikely(!pskb)) {
			if (csk->wr_nondata)
				csk->wr_nondata -= credits;
			break;
		}
		csum = (__force u32)pskb->csum;
		if (unlikely(credits < csum)) {
			pskb->csum = (__force __wsum)(csum - credits);
			break;
		}
		dequeue_wr(sk);
		credits -= csum;
		kfree_skb(pskb);
	}
	if (hdr->seq_vld & CPL_FW4_ACK_FLAGS_SEQVAL) {
		if (unlikely(before(snd_una, tp->snd_una))) {
			kfree_skb(skb);
			return;
		}

		if (tp->snd_una != snd_una) {
			tp->snd_una = snd_una;
			tp->rcv_tstamp = tcp_time_stamp(tp);
			if (tp->snd_una == tp->snd_nxt &&
			    !csk_flag_nochk(csk, CSK_TX_FAILOVER))
				csk_reset_flag(csk, CSK_TX_WAIT_IDLE);
		}
	}

	if (hdr->seq_vld & CPL_FW4_ACK_FLAGS_CH) {
		unsigned int fclen16 = roundup(failover_flowc_wr_len, 16);

		csk->wr_credits -= fclen16;
		csk_reset_flag(csk, CSK_TX_WAIT_IDLE);
		csk_reset_flag(csk, CSK_TX_FAILOVER);
	}
	if (skb_queue_len(&csk->txq) && chtls_push_frames(csk, 0))
		sk->sk_write_space(sk);

	kfree_skb(skb);
}

static int chtls_wr_ack(struct chtls_dev *cdev, struct sk_buff *skb)
{
	struct cpl_fw4_ack *rpl = cplhdr(skb) + RSS_HDR;
	unsigned int hwtid = GET_TID(rpl);
	struct sock *sk;

	sk = lookup_tid(cdev->tids, hwtid);
	if (unlikely(!sk)) {
		pr_err("can't find conn. for hwtid %u.\n", hwtid);
		return -EINVAL;
	}
	process_cpl_msg(chtls_rx_ack, sk, skb);

	return 0;
}

chtls_handler_func chtls_handlers[NUM_CPL_CMDS] = {
	[CPL_PASS_OPEN_RPL]     = chtls_pass_open_rpl,
	[CPL_CLOSE_LISTSRV_RPL] = chtls_close_listsrv_rpl,
	[CPL_PASS_ACCEPT_REQ]   = chtls_pass_accept_req,
	[CPL_PASS_ESTABLISH]    = chtls_pass_establish,
	[CPL_RX_DATA]           = chtls_rx_data,
	[CPL_TLS_DATA]          = chtls_rx_pdu,
	[CPL_RX_TLS_CMP]        = chtls_rx_cmp,
	[CPL_PEER_CLOSE]        = chtls_conn_cpl,
	[CPL_CLOSE_CON_RPL]     = chtls_conn_cpl,
	[CPL_ABORT_REQ_RSS]     = chtls_conn_cpl,
	[CPL_ABORT_RPL_RSS]     = chtls_conn_cpl,
	[CPL_FW4_ACK]           = chtls_wr_ack,
};
