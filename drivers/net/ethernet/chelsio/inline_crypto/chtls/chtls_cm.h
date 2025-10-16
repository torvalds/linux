/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2018 Chelsio Communications, Inc.
 */

#ifndef __CHTLS_CM_H__
#define __CHTLS_CM_H__

/*
 * TCB settings
 */
/* 3:0 */
#define TCB_ULP_TYPE_W    0
#define TCB_ULP_TYPE_S    0
#define TCB_ULP_TYPE_M    0xfULL
#define TCB_ULP_TYPE_V(x) ((x) << TCB_ULP_TYPE_S)

/* 11:4 */
#define TCB_ULP_RAW_W    0
#define TCB_ULP_RAW_S    4
#define TCB_ULP_RAW_M    0xffULL
#define TCB_ULP_RAW_V(x) ((x) << TCB_ULP_RAW_S)

#define TF_TLS_KEY_SIZE_S    7
#define TF_TLS_KEY_SIZE_V(x) ((x) << TF_TLS_KEY_SIZE_S)

#define TF_TLS_CONTROL_S     2
#define TF_TLS_CONTROL_V(x) ((x) << TF_TLS_CONTROL_S)

#define TF_TLS_ACTIVE_S      1
#define TF_TLS_ACTIVE_V(x) ((x) << TF_TLS_ACTIVE_S)

#define TF_TLS_ENABLE_S      0
#define TF_TLS_ENABLE_V(x) ((x) << TF_TLS_ENABLE_S)

#define TF_RX_QUIESCE_S    15
#define TF_RX_QUIESCE_V(x) ((x) << TF_RX_QUIESCE_S)

/*
 * Max receive window supported by HW in bytes.  Only a small part of it can
 * be set through option0, the rest needs to be set through RX_DATA_ACK.
 */
#define MAX_RCV_WND ((1U << 27) - 1)
#define MAX_MSS     65536

/*
 * Min receive window.  We want it to be large enough to accommodate receive
 * coalescing, handle jumbo frames, and not trigger sender SWS avoidance.
 */
#define MIN_RCV_WND (24 * 1024U)
#define LOOPBACK(x)     (((x) & htonl(0xff000000)) == htonl(0x7f000000))

/* for TX: a skb must have a headroom of at least TX_HEADER_LEN bytes */
#define TX_HEADER_LEN \
	(sizeof(struct fw_ofld_tx_data_wr) + sizeof(struct sge_opaque_hdr))
#define TX_TLSHDR_LEN \
	(sizeof(struct fw_tlstx_data_wr) + sizeof(struct cpl_tx_tls_sfo) + \
	 sizeof(struct sge_opaque_hdr))
#define TXDATA_SKB_LEN 128

enum {
	CPL_TX_TLS_SFO_TYPE_CCS,
	CPL_TX_TLS_SFO_TYPE_ALERT,
	CPL_TX_TLS_SFO_TYPE_HANDSHAKE,
	CPL_TX_TLS_SFO_TYPE_DATA,
	CPL_TX_TLS_SFO_TYPE_HEARTBEAT,
};

enum {
	TLS_HDR_TYPE_CCS = 20,
	TLS_HDR_TYPE_ALERT,
	TLS_HDR_TYPE_HANDSHAKE,
	TLS_HDR_TYPE_RECORD,
	TLS_HDR_TYPE_HEARTBEAT,
};

typedef void (*defer_handler_t)(struct chtls_dev *dev, struct sk_buff *skb);
extern struct request_sock_ops chtls_rsk_ops;
extern struct request_sock_ops chtls_rsk_opsv6;

struct deferred_skb_cb {
	defer_handler_t handler;
	struct chtls_dev *dev;
};

#define DEFERRED_SKB_CB(skb) ((struct deferred_skb_cb *)(skb)->cb)
#define failover_flowc_wr_len offsetof(struct fw_flowc_wr, mnemval[3])
#define WR_SKB_CB(skb) ((struct wr_skb_cb *)(skb)->cb)
#define ACCEPT_QUEUE(sk) (&inet_csk(sk)->icsk_accept_queue.rskq_accept_head)

#define SND_WSCALE(tp) ((tp)->rx_opt.snd_wscale)
#define RCV_WSCALE(tp) ((tp)->rx_opt.rcv_wscale)
#define USER_MSS(tp) (READ_ONCE((tp)->rx_opt.user_mss))
#define TS_RECENT_STAMP(tp) ((tp)->rx_opt.ts_recent_stamp)
#define WSCALE_OK(tp) ((tp)->rx_opt.wscale_ok)
#define TSTAMP_OK(tp) ((tp)->rx_opt.tstamp_ok)
#define SACK_OK(tp) ((tp)->rx_opt.sack_ok)

/* TLS SKB */
#define skb_ulp_tls_inline(skb)      (ULP_SKB_CB(skb)->ulp.tls.ofld)
#define skb_ulp_tls_iv_imm(skb)      (ULP_SKB_CB(skb)->ulp.tls.iv)

void chtls_defer_reply(struct sk_buff *skb, struct chtls_dev *dev,
		       defer_handler_t handler);

/*
 * Returns true if the socket is in one of the supplied states.
 */
static inline unsigned int sk_in_state(const struct sock *sk,
				       unsigned int states)
{
	return states & (1 << sk->sk_state);
}

static void chtls_rsk_destructor(struct request_sock *req)
{
	/* do nothing */
}

static inline void chtls_init_rsk_ops(struct proto *chtls_tcp_prot,
				      struct request_sock_ops *chtls_tcp_ops,
				      struct proto *tcp_prot, int family)
{
	memset(chtls_tcp_ops, 0, sizeof(*chtls_tcp_ops));
	chtls_tcp_ops->family = family;
	chtls_tcp_ops->obj_size = sizeof(struct tcp_request_sock);
	chtls_tcp_ops->destructor = chtls_rsk_destructor;
	chtls_tcp_ops->slab = tcp_prot->rsk_prot->slab;
	chtls_tcp_prot->rsk_prot = chtls_tcp_ops;
}

static inline void chtls_reqsk_free(struct request_sock *req)
{
	if (req->rsk_listener)
		sock_put(req->rsk_listener);
	kmem_cache_free(req->rsk_ops->slab, req);
}

#define DECLARE_TASK_FUNC(task, task_param) \
		static void task(struct work_struct *task_param)

static inline void sk_wakeup_sleepers(struct sock *sk, bool interruptable)
{
	struct socket_wq *wq;

	rcu_read_lock();
	wq = rcu_dereference(sk->sk_wq);
	if (skwq_has_sleeper(wq)) {
		if (interruptable)
			wake_up_interruptible(sk_sleep(sk));
		else
			wake_up_all(sk_sleep(sk));
	}
	rcu_read_unlock();
}

static inline void chtls_set_req_port(struct request_sock *oreq,
				      __be16 source, __be16 dest)
{
	inet_rsk(oreq)->ir_rmt_port = source;
	inet_rsk(oreq)->ir_num = ntohs(dest);
}

static inline void chtls_set_req_addr(struct request_sock *oreq,
				      __be32 local_ip, __be32 peer_ip)
{
	inet_rsk(oreq)->ir_loc_addr = local_ip;
	inet_rsk(oreq)->ir_rmt_addr = peer_ip;
}

static inline void chtls_free_skb(struct sock *sk, struct sk_buff *skb)
{
	skb_dstref_steal(skb);
	__skb_unlink(skb, &sk->sk_receive_queue);
	__kfree_skb(skb);
}

static inline void chtls_kfree_skb(struct sock *sk, struct sk_buff *skb)
{
	skb_dstref_steal(skb);
	__skb_unlink(skb, &sk->sk_receive_queue);
	kfree_skb(skb);
}

static inline void chtls_reset_wr_list(struct chtls_sock *csk)
{
	csk->wr_skb_head = NULL;
	csk->wr_skb_tail = NULL;
}

static inline void enqueue_wr(struct chtls_sock *csk, struct sk_buff *skb)
{
	WR_SKB_CB(skb)->next_wr = NULL;

	skb_get(skb);

	if (!csk->wr_skb_head)
		csk->wr_skb_head = skb;
	else
		WR_SKB_CB(csk->wr_skb_tail)->next_wr = skb;
	csk->wr_skb_tail = skb;
}

static inline struct sk_buff *dequeue_wr(struct sock *sk)
{
	struct chtls_sock *csk = rcu_dereference_sk_user_data(sk);
	struct sk_buff *skb = NULL;

	skb = csk->wr_skb_head;

	if (likely(skb)) {
	 /* Don't bother clearing the tail */
		csk->wr_skb_head = WR_SKB_CB(skb)->next_wr;
		WR_SKB_CB(skb)->next_wr = NULL;
	}
	return skb;
}
#endif
