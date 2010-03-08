/*
 * cxgb3i_offload.c: Chelsio S3xx iscsi offloaded tcp connection management
 *
 * Copyright (C) 2003-2008 Chelsio Communications.  All rights reserved.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the LICENSE file included in this
 * release for licensing terms and conditions.
 *
 * Written by:	Dimitris Michailidis (dm@chelsio.com)
 *		Karen Xie (kxie@chelsio.com)
 */

#include <linux/if_vlan.h>
#include <linux/version.h>

#include "cxgb3_defs.h"
#include "cxgb3_ctl_defs.h"
#include "firmware_exports.h"
#include "cxgb3i_offload.h"
#include "cxgb3i_pdu.h"
#include "cxgb3i_ddp.h"

#ifdef __DEBUG_C3CN_CONN__
#define c3cn_conn_debug		cxgb3i_log_debug
#else
#define c3cn_conn_debug(fmt...)
#endif

#ifdef __DEBUG_C3CN_TX__
#define c3cn_tx_debug		cxgb3i_log_debug
#else
#define c3cn_tx_debug(fmt...)
#endif

#ifdef __DEBUG_C3CN_RX__
#define c3cn_rx_debug		cxgb3i_log_debug
#else
#define c3cn_rx_debug(fmt...)
#endif

/*
 * module parameters releated to offloaded iscsi connection
 */
static int cxgb3_rcv_win = 256 * 1024;
module_param(cxgb3_rcv_win, int, 0644);
MODULE_PARM_DESC(cxgb3_rcv_win, "TCP receive window in bytes (default=256KB)");

static int cxgb3_snd_win = 128 * 1024;
module_param(cxgb3_snd_win, int, 0644);
MODULE_PARM_DESC(cxgb3_snd_win, "TCP send window in bytes (default=128KB)");

static int cxgb3_rx_credit_thres = 10 * 1024;
module_param(cxgb3_rx_credit_thres, int, 0644);
MODULE_PARM_DESC(rx_credit_thres,
		 "RX credits return threshold in bytes (default=10KB)");

static unsigned int cxgb3_max_connect = 8 * 1024;
module_param(cxgb3_max_connect, uint, 0644);
MODULE_PARM_DESC(cxgb3_max_connect, "Max. # of connections (default=8092)");

static unsigned int cxgb3_sport_base = 20000;
module_param(cxgb3_sport_base, uint, 0644);
MODULE_PARM_DESC(cxgb3_sport_base, "starting port number (default=20000)");

/*
 * cxgb3i tcp connection data(per adapter) list
 */
static LIST_HEAD(cdata_list);
static DEFINE_RWLOCK(cdata_rwlock);

static int c3cn_push_tx_frames(struct s3_conn *c3cn, int req_completion);
static void c3cn_release_offload_resources(struct s3_conn *c3cn);

/*
 * iscsi source port management
 *
 * Find a free source port in the port allocation map. We use a very simple
 * rotor scheme to look for the next free port.
 *
 * If a source port has been specified make sure that it doesn't collide with
 * our normal source port allocation map.  If it's outside the range of our
 * allocation/deallocation scheme just let them use it.
 *
 * If the source port is outside our allocation range, the caller is
 * responsible for keeping track of their port usage.
 */
static int c3cn_get_port(struct s3_conn *c3cn, struct cxgb3i_sdev_data *cdata)
{
	unsigned int start;
	int idx;

	if (!cdata)
		goto error_out;

	if (c3cn->saddr.sin_port) {
		cxgb3i_log_error("connect, sin_port NON-ZERO %u.\n",
				 c3cn->saddr.sin_port);
		return -EADDRINUSE;
	}

	spin_lock_bh(&cdata->lock);
	start = idx = cdata->sport_next;
	do {
		if (++idx >= cxgb3_max_connect)
			idx = 0;
		if (!cdata->sport_conn[idx]) {
			c3cn->saddr.sin_port = htons(cxgb3_sport_base + idx);
			cdata->sport_next = idx;
			cdata->sport_conn[idx] = c3cn;
			spin_unlock_bh(&cdata->lock);

			c3cn_conn_debug("%s reserve port %u.\n",
					cdata->cdev->name,
					cxgb3_sport_base + idx);
			return 0;
		}
	} while (idx != start);
	spin_unlock_bh(&cdata->lock);

error_out:
	return -EADDRNOTAVAIL;
}

static void c3cn_put_port(struct s3_conn *c3cn)
{
	if (!c3cn->cdev)
		return;

	if (c3cn->saddr.sin_port) {
		struct cxgb3i_sdev_data *cdata = CXGB3_SDEV_DATA(c3cn->cdev);
		int idx = ntohs(c3cn->saddr.sin_port) - cxgb3_sport_base;

		c3cn->saddr.sin_port = 0;
		if (idx < 0 || idx >= cxgb3_max_connect)
			return;
		spin_lock_bh(&cdata->lock);
		cdata->sport_conn[idx] = NULL;
		spin_unlock_bh(&cdata->lock);
		c3cn_conn_debug("%s, release port %u.\n",
				cdata->cdev->name, cxgb3_sport_base + idx);
	}
}

static inline void c3cn_set_flag(struct s3_conn *c3cn, enum c3cn_flags flag)
{
	__set_bit(flag, &c3cn->flags);
	c3cn_conn_debug("c3cn 0x%p, set %d, s %u, f 0x%lx.\n",
			c3cn, flag, c3cn->state, c3cn->flags);
}

static inline void c3cn_clear_flag(struct s3_conn *c3cn, enum c3cn_flags flag)
{
	__clear_bit(flag, &c3cn->flags);
	c3cn_conn_debug("c3cn 0x%p, clear %d, s %u, f 0x%lx.\n",
			c3cn, flag, c3cn->state, c3cn->flags);
}

static inline int c3cn_flag(struct s3_conn *c3cn, enum c3cn_flags flag)
{
	if (c3cn == NULL)
		return 0;
	return test_bit(flag, &c3cn->flags);
}

static void c3cn_set_state(struct s3_conn *c3cn, int state)
{
	c3cn_conn_debug("c3cn 0x%p state -> %u.\n", c3cn, state);
	c3cn->state = state;
}

static inline void c3cn_hold(struct s3_conn *c3cn)
{
	atomic_inc(&c3cn->refcnt);
}

static inline void c3cn_put(struct s3_conn *c3cn)
{
	if (atomic_dec_and_test(&c3cn->refcnt)) {
		c3cn_conn_debug("free c3cn 0x%p, s %u, f 0x%lx.\n",
				c3cn, c3cn->state, c3cn->flags);
		kfree(c3cn);
	}
}

static void c3cn_closed(struct s3_conn *c3cn)
{
	c3cn_conn_debug("c3cn 0x%p, state %u, flag 0x%lx.\n",
			 c3cn, c3cn->state, c3cn->flags);

	c3cn_put_port(c3cn);
	c3cn_release_offload_resources(c3cn);
	c3cn_set_state(c3cn, C3CN_STATE_CLOSED);
	cxgb3i_conn_closing(c3cn);
}

/*
 * CPL (Chelsio Protocol Language) defines a message passing interface between
 * the host driver and T3 asic.
 * The section below implments CPLs that related to iscsi tcp connection
 * open/close/abort and data send/receive.
 */

/*
 * CPL connection active open request: host ->
 */
static unsigned int find_best_mtu(const struct t3c_data *d, unsigned short mtu)
{
	int i = 0;

	while (i < d->nmtus - 1 && d->mtus[i + 1] <= mtu)
		++i;
	return i;
}

static unsigned int select_mss(struct s3_conn *c3cn, unsigned int pmtu)
{
	unsigned int idx;
	struct dst_entry *dst = c3cn->dst_cache;
	struct t3cdev *cdev = c3cn->cdev;
	const struct t3c_data *td = T3C_DATA(cdev);
	u16 advmss = dst_metric(dst, RTAX_ADVMSS);

	if (advmss > pmtu - 40)
		advmss = pmtu - 40;
	if (advmss < td->mtus[0] - 40)
		advmss = td->mtus[0] - 40;
	idx = find_best_mtu(td, advmss + 40);
	return idx;
}

static inline int compute_wscale(int win)
{
	int wscale = 0;
	while (wscale < 14 && (65535<<wscale) < win)
		wscale++;
	return wscale;
}

static inline unsigned int calc_opt0h(struct s3_conn *c3cn)
{
	int wscale = compute_wscale(cxgb3_rcv_win);
	return  V_KEEP_ALIVE(1) |
		F_TCAM_BYPASS |
		V_WND_SCALE(wscale) |
		V_MSS_IDX(c3cn->mss_idx);
}

static inline unsigned int calc_opt0l(struct s3_conn *c3cn)
{
	return  V_ULP_MODE(ULP_MODE_ISCSI) |
		V_RCV_BUFSIZ(cxgb3_rcv_win>>10);
}

static void make_act_open_req(struct s3_conn *c3cn, struct sk_buff *skb,
			      unsigned int atid, const struct l2t_entry *e)
{
	struct cpl_act_open_req *req;

	c3cn_conn_debug("c3cn 0x%p, atid 0x%x.\n", c3cn, atid);

	skb->priority = CPL_PRIORITY_SETUP;
	req = (struct cpl_act_open_req *)__skb_put(skb, sizeof(*req));
	req->wr.wr_hi = htonl(V_WR_OP(FW_WROPCODE_FORWARD));
	OPCODE_TID(req) = htonl(MK_OPCODE_TID(CPL_ACT_OPEN_REQ, atid));
	req->local_port = c3cn->saddr.sin_port;
	req->peer_port = c3cn->daddr.sin_port;
	req->local_ip = c3cn->saddr.sin_addr.s_addr;
	req->peer_ip = c3cn->daddr.sin_addr.s_addr;
	req->opt0h = htonl(calc_opt0h(c3cn) | V_L2T_IDX(e->idx) |
			   V_TX_CHANNEL(e->smt_idx));
	req->opt0l = htonl(calc_opt0l(c3cn));
	req->params = 0;
}

static void fail_act_open(struct s3_conn *c3cn, int errno)
{
	c3cn_conn_debug("c3cn 0x%p, state %u, flag 0x%lx.\n",
			c3cn, c3cn->state, c3cn->flags);
	c3cn->err = errno;
	c3cn_closed(c3cn);
}

static void act_open_req_arp_failure(struct t3cdev *dev, struct sk_buff *skb)
{
	struct s3_conn *c3cn = (struct s3_conn *)skb->sk;

	c3cn_conn_debug("c3cn 0x%p, state %u.\n", c3cn, c3cn->state);

	c3cn_hold(c3cn);
	spin_lock_bh(&c3cn->lock);
	if (c3cn->state == C3CN_STATE_CONNECTING)
		fail_act_open(c3cn, -EHOSTUNREACH);
	spin_unlock_bh(&c3cn->lock);
	c3cn_put(c3cn);
	__kfree_skb(skb);
}

/*
 * CPL connection close request: host ->
 *
 * Close a connection by sending a CPL_CLOSE_CON_REQ message and queue it to
 * the write queue (i.e., after any unsent txt data).
 */
static void skb_entail(struct s3_conn *c3cn, struct sk_buff *skb,
		       int flags)
{
	skb_tcp_seq(skb) = c3cn->write_seq;
	skb_flags(skb) = flags;
	__skb_queue_tail(&c3cn->write_queue, skb);
}

static void send_close_req(struct s3_conn *c3cn)
{
	struct sk_buff *skb = c3cn->cpl_close;
	struct cpl_close_con_req *req = (struct cpl_close_con_req *)skb->head;
	unsigned int tid = c3cn->tid;

	c3cn_conn_debug("c3cn 0x%p, state 0x%x, flag 0x%lx.\n",
			c3cn, c3cn->state, c3cn->flags);

	c3cn->cpl_close = NULL;

	req->wr.wr_hi = htonl(V_WR_OP(FW_WROPCODE_OFLD_CLOSE_CON));
	req->wr.wr_lo = htonl(V_WR_TID(tid));
	OPCODE_TID(req) = htonl(MK_OPCODE_TID(CPL_CLOSE_CON_REQ, tid));
	req->rsvd = htonl(c3cn->write_seq);

	skb_entail(c3cn, skb, C3CB_FLAG_NO_APPEND);
	if (c3cn->state != C3CN_STATE_CONNECTING)
		c3cn_push_tx_frames(c3cn, 1);
}

/*
 * CPL connection abort request: host ->
 *
 * Send an ABORT_REQ message. Makes sure we do not send multiple ABORT_REQs
 * for the same connection and also that we do not try to send a message
 * after the connection has closed.
 */
static void abort_arp_failure(struct t3cdev *cdev, struct sk_buff *skb)
{
	struct cpl_abort_req *req = cplhdr(skb);

	c3cn_conn_debug("tdev 0x%p.\n", cdev);

	req->cmd = CPL_ABORT_NO_RST;
	cxgb3_ofld_send(cdev, skb);
}

static inline void c3cn_purge_write_queue(struct s3_conn *c3cn)
{
	struct sk_buff *skb;

	while ((skb = __skb_dequeue(&c3cn->write_queue)))
		__kfree_skb(skb);
}

static void send_abort_req(struct s3_conn *c3cn)
{
	struct sk_buff *skb = c3cn->cpl_abort_req;
	struct cpl_abort_req *req;
	unsigned int tid = c3cn->tid;

	if (unlikely(c3cn->state == C3CN_STATE_ABORTING) || !skb ||
		     !c3cn->cdev)
		return;

	c3cn_set_state(c3cn, C3CN_STATE_ABORTING);

	c3cn_conn_debug("c3cn 0x%p, flag ABORT_RPL + ABORT_SHUT.\n", c3cn);

	c3cn_set_flag(c3cn, C3CN_ABORT_RPL_PENDING);

	/* Purge the send queue so we don't send anything after an abort. */
	c3cn_purge_write_queue(c3cn);

	c3cn->cpl_abort_req = NULL;
	req = (struct cpl_abort_req *)skb->head;

	skb->priority = CPL_PRIORITY_DATA;
	set_arp_failure_handler(skb, abort_arp_failure);

	req->wr.wr_hi = htonl(V_WR_OP(FW_WROPCODE_OFLD_HOST_ABORT_CON_REQ));
	req->wr.wr_lo = htonl(V_WR_TID(tid));
	OPCODE_TID(req) = htonl(MK_OPCODE_TID(CPL_ABORT_REQ, tid));
	req->rsvd0 = htonl(c3cn->snd_nxt);
	req->rsvd1 = !c3cn_flag(c3cn, C3CN_TX_DATA_SENT);
	req->cmd = CPL_ABORT_SEND_RST;

	l2t_send(c3cn->cdev, skb, c3cn->l2t);
}

/*
 * CPL connection abort reply: host ->
 *
 * Send an ABORT_RPL message in response of the ABORT_REQ received.
 */
static void send_abort_rpl(struct s3_conn *c3cn, int rst_status)
{
	struct sk_buff *skb = c3cn->cpl_abort_rpl;
	struct cpl_abort_rpl *rpl = (struct cpl_abort_rpl *)skb->head;

	c3cn->cpl_abort_rpl = NULL;

	skb->priority = CPL_PRIORITY_DATA;
	rpl->wr.wr_hi = htonl(V_WR_OP(FW_WROPCODE_OFLD_HOST_ABORT_CON_RPL));
	rpl->wr.wr_lo = htonl(V_WR_TID(c3cn->tid));
	OPCODE_TID(rpl) = htonl(MK_OPCODE_TID(CPL_ABORT_RPL, c3cn->tid));
	rpl->cmd = rst_status;

	cxgb3_ofld_send(c3cn->cdev, skb);
}

/*
 * CPL connection rx data ack: host ->
 * Send RX credits through an RX_DATA_ACK CPL message. Returns the number of
 * credits sent.
 */
static u32 send_rx_credits(struct s3_conn *c3cn, u32 credits, u32 dack)
{
	struct sk_buff *skb;
	struct cpl_rx_data_ack *req;

	skb = alloc_skb(sizeof(*req), GFP_ATOMIC);
	if (!skb)
		return 0;

	req = (struct cpl_rx_data_ack *)__skb_put(skb, sizeof(*req));
	req->wr.wr_hi = htonl(V_WR_OP(FW_WROPCODE_FORWARD));
	OPCODE_TID(req) = htonl(MK_OPCODE_TID(CPL_RX_DATA_ACK, c3cn->tid));
	req->credit_dack = htonl(dack | V_RX_CREDITS(credits));
	skb->priority = CPL_PRIORITY_ACK;
	cxgb3_ofld_send(c3cn->cdev, skb);
	return credits;
}

/*
 * CPL connection tx data: host ->
 *
 * Send iscsi PDU via TX_DATA CPL message. Returns the number of
 * credits sent.
 * Each TX_DATA consumes work request credit (wrs), so we need to keep track of
 * how many we've used so far and how many are pending (i.e., yet ack'ed by T3).
 */

/*
 * For ULP connections HW may inserts digest bytes into the pdu. Those digest
 * bytes are not sent by the host but are part of the TCP payload and therefore
 * consume TCP sequence space.
 */
static const unsigned int cxgb3_ulp_extra_len[] = { 0, 4, 4, 8 };
static inline unsigned int ulp_extra_len(const struct sk_buff *skb)
{
	return cxgb3_ulp_extra_len[skb_ulp_mode(skb) & 3];
}

static unsigned int wrlen __read_mostly;

/*
 * The number of WRs needed for an skb depends on the number of fragments
 * in the skb and whether it has any payload in its main body.  This maps the
 * length of the gather list represented by an skb into the # of necessary WRs.
 * The extra two fragments are for iscsi bhs and payload padding.
 */
#define SKB_WR_LIST_SIZE	(MAX_SKB_FRAGS + 2)
static unsigned int skb_wrs[SKB_WR_LIST_SIZE] __read_mostly;

static void s3_init_wr_tab(unsigned int wr_len)
{
	int i;

	if (skb_wrs[1])		/* already initialized */
		return;

	for (i = 1; i < SKB_WR_LIST_SIZE; i++) {
		int sgl_len = (3 * i) / 2 + (i & 1);

		sgl_len += 3;
		skb_wrs[i] = (sgl_len <= wr_len
			      ? 1 : 1 + (sgl_len - 2) / (wr_len - 1));
	}

	wrlen = wr_len * 8;
}

static inline void reset_wr_list(struct s3_conn *c3cn)
{
	c3cn->wr_pending_head = c3cn->wr_pending_tail = NULL;
}

/*
 * Add a WR to a connections's list of pending WRs.  This is a singly-linked
 * list of sk_buffs operating as a FIFO.  The head is kept in wr_pending_head
 * and the tail in wr_pending_tail.
 */
static inline void enqueue_wr(struct s3_conn *c3cn,
			      struct sk_buff *skb)
{
	skb_tx_wr_next(skb) = NULL;

	/*
	 * We want to take an extra reference since both us and the driver
	 * need to free the packet before it's really freed. We know there's
	 * just one user currently so we use atomic_set rather than skb_get
	 * to avoid the atomic op.
	 */
	atomic_set(&skb->users, 2);

	if (!c3cn->wr_pending_head)
		c3cn->wr_pending_head = skb;
	else
		skb_tx_wr_next(c3cn->wr_pending_tail) = skb;
	c3cn->wr_pending_tail = skb;
}

static int count_pending_wrs(struct s3_conn *c3cn)
{
	int n = 0;
	const struct sk_buff *skb = c3cn->wr_pending_head;

	while (skb) {
		n += skb->csum;
		skb = skb_tx_wr_next(skb);
	}
	return n;
}

static inline struct sk_buff *peek_wr(const struct s3_conn *c3cn)
{
	return c3cn->wr_pending_head;
}

static inline void free_wr_skb(struct sk_buff *skb)
{
	kfree_skb(skb);
}

static inline struct sk_buff *dequeue_wr(struct s3_conn *c3cn)
{
	struct sk_buff *skb = c3cn->wr_pending_head;

	if (likely(skb)) {
		/* Don't bother clearing the tail */
		c3cn->wr_pending_head = skb_tx_wr_next(skb);
		skb_tx_wr_next(skb) = NULL;
	}
	return skb;
}

static void purge_wr_queue(struct s3_conn *c3cn)
{
	struct sk_buff *skb;
	while ((skb = dequeue_wr(c3cn)) != NULL)
		free_wr_skb(skb);
}

static inline void make_tx_data_wr(struct s3_conn *c3cn, struct sk_buff *skb,
				   int len, int req_completion)
{
	struct tx_data_wr *req;

	skb_reset_transport_header(skb);
	req = (struct tx_data_wr *)__skb_push(skb, sizeof(*req));
	req->wr_hi = htonl(V_WR_OP(FW_WROPCODE_OFLD_TX_DATA) |
			(req_completion ? F_WR_COMPL : 0));
	req->wr_lo = htonl(V_WR_TID(c3cn->tid));
	req->sndseq = htonl(c3cn->snd_nxt);
	/* len includes the length of any HW ULP additions */
	req->len = htonl(len);
	req->param = htonl(V_TX_PORT(c3cn->l2t->smt_idx));
	/* V_TX_ULP_SUBMODE sets both the mode and submode */
	req->flags = htonl(V_TX_ULP_SUBMODE(skb_ulp_mode(skb)) |
			   V_TX_SHOVE((skb_peek(&c3cn->write_queue) ? 0 : 1)));

	if (!c3cn_flag(c3cn, C3CN_TX_DATA_SENT)) {
		req->flags |= htonl(V_TX_ACK_PAGES(2) | F_TX_INIT |
				    V_TX_CPU_IDX(c3cn->qset));
		/* Sendbuffer is in units of 32KB. */
		req->param |= htonl(V_TX_SNDBUF(cxgb3_snd_win >> 15));
		c3cn_set_flag(c3cn, C3CN_TX_DATA_SENT);
	}
}

/**
 * c3cn_push_tx_frames -- start transmit
 * @c3cn: the offloaded connection
 * @req_completion: request wr_ack or not
 *
 * Prepends TX_DATA_WR or CPL_CLOSE_CON_REQ headers to buffers waiting in a
 * connection's send queue and sends them on to T3.  Must be called with the
 * connection's lock held.  Returns the amount of send buffer space that was
 * freed as a result of sending queued data to T3.
 */
static void arp_failure_discard(struct t3cdev *cdev, struct sk_buff *skb)
{
	kfree_skb(skb);
}

static int c3cn_push_tx_frames(struct s3_conn *c3cn, int req_completion)
{
	int total_size = 0;
	struct sk_buff *skb;
	struct t3cdev *cdev;
	struct cxgb3i_sdev_data *cdata;

	if (unlikely(c3cn->state == C3CN_STATE_CONNECTING ||
		     c3cn->state == C3CN_STATE_CLOSE_WAIT_1 ||
		     c3cn->state >= C3CN_STATE_ABORTING)) {
		c3cn_tx_debug("c3cn 0x%p, in closing state %u.\n",
			      c3cn, c3cn->state);
		return 0;
	}

	cdev = c3cn->cdev;
	cdata = CXGB3_SDEV_DATA(cdev);

	while (c3cn->wr_avail
	       && (skb = skb_peek(&c3cn->write_queue)) != NULL) {
		int len = skb->len;	/* length before skb_push */
		int frags = skb_shinfo(skb)->nr_frags + (len != skb->data_len);
		int wrs_needed = skb_wrs[frags];

		if (wrs_needed > 1 && len + sizeof(struct tx_data_wr) <= wrlen)
			wrs_needed = 1;

		WARN_ON(frags >= SKB_WR_LIST_SIZE || wrs_needed < 1);

		if (c3cn->wr_avail < wrs_needed) {
			c3cn_tx_debug("c3cn 0x%p, skb len %u/%u, frag %u, "
				      "wr %d < %u.\n",
				      c3cn, skb->len, skb->data_len, frags,
				      wrs_needed, c3cn->wr_avail);
			break;
		}

		__skb_unlink(skb, &c3cn->write_queue);
		skb->priority = CPL_PRIORITY_DATA;
		skb->csum = wrs_needed;	/* remember this until the WR_ACK */
		c3cn->wr_avail -= wrs_needed;
		c3cn->wr_unacked += wrs_needed;
		enqueue_wr(c3cn, skb);

		c3cn_tx_debug("c3cn 0x%p, enqueue, skb len %u/%u, frag %u, "
				"wr %d, left %u, unack %u.\n",
				c3cn, skb->len, skb->data_len, frags,
				wrs_needed, c3cn->wr_avail, c3cn->wr_unacked);


		if (likely(skb_flags(skb) & C3CB_FLAG_NEED_HDR)) {
			if ((req_completion &&
				c3cn->wr_unacked == wrs_needed) ||
			    (skb_flags(skb) & C3CB_FLAG_COMPL) ||
			    c3cn->wr_unacked >= c3cn->wr_max / 2) {
				req_completion = 1;
				c3cn->wr_unacked = 0;
			}
			len += ulp_extra_len(skb);
			make_tx_data_wr(c3cn, skb, len, req_completion);
			c3cn->snd_nxt += len;
			skb_flags(skb) &= ~C3CB_FLAG_NEED_HDR;
		}

		total_size += skb->truesize;
		set_arp_failure_handler(skb, arp_failure_discard);
		l2t_send(cdev, skb, c3cn->l2t);
	}
	return total_size;
}

/*
 * process_cpl_msg: -> host
 * Top-level CPL message processing used by most CPL messages that
 * pertain to connections.
 */
static inline void process_cpl_msg(void (*fn)(struct s3_conn *,
					      struct sk_buff *),
				   struct s3_conn *c3cn,
				   struct sk_buff *skb)
{
	spin_lock_bh(&c3cn->lock);
	fn(c3cn, skb);
	spin_unlock_bh(&c3cn->lock);
}

/*
 * process_cpl_msg_ref: -> host
 * Similar to process_cpl_msg() but takes an extra connection reference around
 * the call to the handler.  Should be used if the handler may drop a
 * connection reference.
 */
static inline void process_cpl_msg_ref(void (*fn) (struct s3_conn *,
						   struct sk_buff *),
				       struct s3_conn *c3cn,
				       struct sk_buff *skb)
{
	c3cn_hold(c3cn);
	process_cpl_msg(fn, c3cn, skb);
	c3cn_put(c3cn);
}

/*
 * Process a CPL_ACT_ESTABLISH message: -> host
 * Updates connection state from an active establish CPL message.  Runs with
 * the connection lock held.
 */

static inline void s3_free_atid(struct t3cdev *cdev, unsigned int tid)
{
	struct s3_conn *c3cn = cxgb3_free_atid(cdev, tid);
	if (c3cn)
		c3cn_put(c3cn);
}

static void c3cn_established(struct s3_conn *c3cn, u32 snd_isn,
			     unsigned int opt)
{
	c3cn_conn_debug("c3cn 0x%p, state %u.\n", c3cn, c3cn->state);

	c3cn->write_seq = c3cn->snd_nxt = c3cn->snd_una = snd_isn;

	/*
	 * Causes the first RX_DATA_ACK to supply any Rx credits we couldn't
	 * pass through opt0.
	 */
	if (cxgb3_rcv_win > (M_RCV_BUFSIZ << 10))
		c3cn->rcv_wup -= cxgb3_rcv_win - (M_RCV_BUFSIZ << 10);

	dst_confirm(c3cn->dst_cache);

	smp_mb();

	c3cn_set_state(c3cn, C3CN_STATE_ESTABLISHED);
}

static void process_act_establish(struct s3_conn *c3cn, struct sk_buff *skb)
{
	struct cpl_act_establish *req = cplhdr(skb);
	u32 rcv_isn = ntohl(req->rcv_isn);	/* real RCV_ISN + 1 */

	c3cn_conn_debug("c3cn 0x%p, state %u, flag 0x%lx.\n",
			c3cn, c3cn->state, c3cn->flags);

	if (unlikely(c3cn->state != C3CN_STATE_CONNECTING))
		cxgb3i_log_error("TID %u expected SYN_SENT, got EST., s %u\n",
				 c3cn->tid, c3cn->state);

	c3cn->copied_seq = c3cn->rcv_wup = c3cn->rcv_nxt = rcv_isn;
	c3cn_established(c3cn, ntohl(req->snd_isn), ntohs(req->tcp_opt));

	__kfree_skb(skb);

	if (unlikely(c3cn_flag(c3cn, C3CN_ACTIVE_CLOSE_NEEDED)))
		/* upper layer has requested closing */
		send_abort_req(c3cn);
	else {
		if (skb_queue_len(&c3cn->write_queue))
			c3cn_push_tx_frames(c3cn, 1);
		cxgb3i_conn_tx_open(c3cn);
	}
}

static int do_act_establish(struct t3cdev *cdev, struct sk_buff *skb,
			    void *ctx)
{
	struct cpl_act_establish *req = cplhdr(skb);
	unsigned int tid = GET_TID(req);
	unsigned int atid = G_PASS_OPEN_TID(ntohl(req->tos_tid));
	struct s3_conn *c3cn = ctx;
	struct cxgb3i_sdev_data *cdata = CXGB3_SDEV_DATA(cdev);

	c3cn_conn_debug("rcv, tid 0x%x, c3cn 0x%p, s %u, f 0x%lx.\n",
			tid, c3cn, c3cn->state, c3cn->flags);

	c3cn->tid = tid;
	c3cn_hold(c3cn);
	cxgb3_insert_tid(cdata->cdev, cdata->client, c3cn, tid);
	s3_free_atid(cdev, atid);

	c3cn->qset = G_QNUM(ntohl(skb->csum));

	process_cpl_msg(process_act_establish, c3cn, skb);
	return 0;
}

/*
 * Process a CPL_ACT_OPEN_RPL message: -> host
 * Handle active open failures.
 */
static int act_open_rpl_status_to_errno(int status)
{
	switch (status) {
	case CPL_ERR_CONN_RESET:
		return -ECONNREFUSED;
	case CPL_ERR_ARP_MISS:
		return -EHOSTUNREACH;
	case CPL_ERR_CONN_TIMEDOUT:
		return -ETIMEDOUT;
	case CPL_ERR_TCAM_FULL:
		return -ENOMEM;
	case CPL_ERR_CONN_EXIST:
		cxgb3i_log_error("ACTIVE_OPEN_RPL: 4-tuple in use\n");
		return -EADDRINUSE;
	default:
		return -EIO;
	}
}

static void act_open_retry_timer(unsigned long data)
{
	struct sk_buff *skb;
	struct s3_conn *c3cn = (struct s3_conn *)data;

	c3cn_conn_debug("c3cn 0x%p, state %u.\n", c3cn, c3cn->state);

	spin_lock_bh(&c3cn->lock);
	skb = alloc_skb(sizeof(struct cpl_act_open_req), GFP_ATOMIC);
	if (!skb)
		fail_act_open(c3cn, -ENOMEM);
	else {
		skb->sk = (struct sock *)c3cn;
		set_arp_failure_handler(skb, act_open_req_arp_failure);
		make_act_open_req(c3cn, skb, c3cn->tid, c3cn->l2t);
		l2t_send(c3cn->cdev, skb, c3cn->l2t);
	}
	spin_unlock_bh(&c3cn->lock);
	c3cn_put(c3cn);
}

static void process_act_open_rpl(struct s3_conn *c3cn, struct sk_buff *skb)
{
	struct cpl_act_open_rpl *rpl = cplhdr(skb);

	c3cn_conn_debug("c3cn 0x%p, state %u, flag 0x%lx.\n",
			c3cn, c3cn->state, c3cn->flags);

	if (rpl->status == CPL_ERR_CONN_EXIST &&
	    c3cn->retry_timer.function != act_open_retry_timer) {
		c3cn->retry_timer.function = act_open_retry_timer;
		if (!mod_timer(&c3cn->retry_timer, jiffies + HZ / 2))
			c3cn_hold(c3cn);
	} else
		fail_act_open(c3cn, act_open_rpl_status_to_errno(rpl->status));
	__kfree_skb(skb);
}

static int do_act_open_rpl(struct t3cdev *cdev, struct sk_buff *skb, void *ctx)
{
	struct s3_conn *c3cn = ctx;
	struct cpl_act_open_rpl *rpl = cplhdr(skb);

	c3cn_conn_debug("rcv, status 0x%x, c3cn 0x%p, s %u, f 0x%lx.\n",
			rpl->status, c3cn, c3cn->state, c3cn->flags);

	if (rpl->status != CPL_ERR_TCAM_FULL &&
	    rpl->status != CPL_ERR_CONN_EXIST &&
	    rpl->status != CPL_ERR_ARP_MISS)
		cxgb3_queue_tid_release(cdev, GET_TID(rpl));

	process_cpl_msg_ref(process_act_open_rpl, c3cn, skb);
	return 0;
}

/*
 * Process PEER_CLOSE CPL messages: -> host
 * Handle peer FIN.
 */
static void process_peer_close(struct s3_conn *c3cn, struct sk_buff *skb)
{
	c3cn_conn_debug("c3cn 0x%p, state %u, flag 0x%lx.\n",
			c3cn, c3cn->state, c3cn->flags);

	if (c3cn_flag(c3cn, C3CN_ABORT_RPL_PENDING))
		goto out;

	switch (c3cn->state) {
	case C3CN_STATE_ESTABLISHED:
		c3cn_set_state(c3cn, C3CN_STATE_PASSIVE_CLOSE);
		break;
	case C3CN_STATE_ACTIVE_CLOSE:
		c3cn_set_state(c3cn, C3CN_STATE_CLOSE_WAIT_2);
		break;
	case C3CN_STATE_CLOSE_WAIT_1:
		c3cn_closed(c3cn);
		break;
	case C3CN_STATE_ABORTING:
		break;
	default:
		cxgb3i_log_error("%s: peer close, TID %u in bad state %u\n",
				 c3cn->cdev->name, c3cn->tid, c3cn->state);
	}

	cxgb3i_conn_closing(c3cn);
out:
	__kfree_skb(skb);
}

static int do_peer_close(struct t3cdev *cdev, struct sk_buff *skb, void *ctx)
{
	struct s3_conn *c3cn = ctx;

	c3cn_conn_debug("rcv, c3cn 0x%p, s %u, f 0x%lx.\n",
			c3cn, c3cn->state, c3cn->flags);
	process_cpl_msg_ref(process_peer_close, c3cn, skb);
	return 0;
}

/*
 * Process CLOSE_CONN_RPL CPL message: -> host
 * Process a peer ACK to our FIN.
 */
static void process_close_con_rpl(struct s3_conn *c3cn, struct sk_buff *skb)
{
	struct cpl_close_con_rpl *rpl = cplhdr(skb);

	c3cn_conn_debug("c3cn 0x%p, state %u, flag 0x%lx.\n",
			c3cn, c3cn->state, c3cn->flags);

	c3cn->snd_una = ntohl(rpl->snd_nxt) - 1;	/* exclude FIN */

	if (c3cn_flag(c3cn, C3CN_ABORT_RPL_PENDING))
		goto out;

	switch (c3cn->state) {
	case C3CN_STATE_ACTIVE_CLOSE:
		c3cn_set_state(c3cn, C3CN_STATE_CLOSE_WAIT_1);
		break;
	case C3CN_STATE_CLOSE_WAIT_1:
	case C3CN_STATE_CLOSE_WAIT_2:
		c3cn_closed(c3cn);
		break;
	case C3CN_STATE_ABORTING:
		break;
	default:
		cxgb3i_log_error("%s: close_rpl, TID %u in bad state %u\n",
				 c3cn->cdev->name, c3cn->tid, c3cn->state);
	}

out:
	kfree_skb(skb);
}

static int do_close_con_rpl(struct t3cdev *cdev, struct sk_buff *skb,
			    void *ctx)
{
	struct s3_conn *c3cn = ctx;

	c3cn_conn_debug("rcv, c3cn 0x%p, s %u, f 0x%lx.\n",
			 c3cn, c3cn->state, c3cn->flags);

	process_cpl_msg_ref(process_close_con_rpl, c3cn, skb);
	return 0;
}

/*
 * Process ABORT_REQ_RSS CPL message: -> host
 * Process abort requests.  If we are waiting for an ABORT_RPL we ignore this
 * request except that we need to reply to it.
 */

static int abort_status_to_errno(struct s3_conn *c3cn, int abort_reason,
				 int *need_rst)
{
	switch (abort_reason) {
	case CPL_ERR_BAD_SYN: /* fall through */
	case CPL_ERR_CONN_RESET:
		return c3cn->state > C3CN_STATE_ESTABLISHED ?
			-EPIPE : -ECONNRESET;
	case CPL_ERR_XMIT_TIMEDOUT:
	case CPL_ERR_PERSIST_TIMEDOUT:
	case CPL_ERR_FINWAIT2_TIMEDOUT:
	case CPL_ERR_KEEPALIVE_TIMEDOUT:
		return -ETIMEDOUT;
	default:
		return -EIO;
	}
}

static void process_abort_req(struct s3_conn *c3cn, struct sk_buff *skb)
{
	int rst_status = CPL_ABORT_NO_RST;
	const struct cpl_abort_req_rss *req = cplhdr(skb);

	c3cn_conn_debug("c3cn 0x%p, state %u, flag 0x%lx.\n",
			c3cn, c3cn->state, c3cn->flags);

	if (!c3cn_flag(c3cn, C3CN_ABORT_REQ_RCVD)) {
		c3cn_set_flag(c3cn, C3CN_ABORT_REQ_RCVD);
		c3cn_set_state(c3cn, C3CN_STATE_ABORTING);
		__kfree_skb(skb);
		return;
	}

	c3cn_clear_flag(c3cn, C3CN_ABORT_REQ_RCVD);
	send_abort_rpl(c3cn, rst_status);

	if (!c3cn_flag(c3cn, C3CN_ABORT_RPL_PENDING)) {
		c3cn->err =
		    abort_status_to_errno(c3cn, req->status, &rst_status);
		c3cn_closed(c3cn);
	}
}

static int do_abort_req(struct t3cdev *cdev, struct sk_buff *skb, void *ctx)
{
	const struct cpl_abort_req_rss *req = cplhdr(skb);
	struct s3_conn *c3cn = ctx;

	c3cn_conn_debug("rcv, c3cn 0x%p, s 0x%x, f 0x%lx.\n",
			c3cn, c3cn->state, c3cn->flags);

	if (req->status == CPL_ERR_RTX_NEG_ADVICE ||
	    req->status == CPL_ERR_PERSIST_NEG_ADVICE) {
		__kfree_skb(skb);
		return 0;
	}

	process_cpl_msg_ref(process_abort_req, c3cn, skb);
	return 0;
}

/*
 * Process ABORT_RPL_RSS CPL message: -> host
 * Process abort replies.  We only process these messages if we anticipate
 * them as the coordination between SW and HW in this area is somewhat lacking
 * and sometimes we get ABORT_RPLs after we are done with the connection that
 * originated the ABORT_REQ.
 */
static void process_abort_rpl(struct s3_conn *c3cn, struct sk_buff *skb)
{
	c3cn_conn_debug("c3cn 0x%p, state %u, flag 0x%lx.\n",
			c3cn, c3cn->state, c3cn->flags);

	if (c3cn_flag(c3cn, C3CN_ABORT_RPL_PENDING)) {
		if (!c3cn_flag(c3cn, C3CN_ABORT_RPL_RCVD))
			c3cn_set_flag(c3cn, C3CN_ABORT_RPL_RCVD);
		else {
			c3cn_clear_flag(c3cn, C3CN_ABORT_RPL_RCVD);
			c3cn_clear_flag(c3cn, C3CN_ABORT_RPL_PENDING);
			if (c3cn_flag(c3cn, C3CN_ABORT_REQ_RCVD))
				cxgb3i_log_error("%s tid %u, ABORT_RPL_RSS\n",
						 c3cn->cdev->name, c3cn->tid);
			c3cn_closed(c3cn);
		}
	}
	__kfree_skb(skb);
}

static int do_abort_rpl(struct t3cdev *cdev, struct sk_buff *skb, void *ctx)
{
	struct cpl_abort_rpl_rss *rpl = cplhdr(skb);
	struct s3_conn *c3cn = ctx;

	c3cn_conn_debug("rcv, status 0x%x, c3cn 0x%p, s %u, 0x%lx.\n",
			rpl->status, c3cn, c3cn ? c3cn->state : 0,
			c3cn ? c3cn->flags : 0UL);

	/*
	 * Ignore replies to post-close aborts indicating that the abort was
	 * requested too late.  These connections are terminated when we get
	 * PEER_CLOSE or CLOSE_CON_RPL and by the time the abort_rpl_rss
	 * arrives the TID is either no longer used or it has been recycled.
	 */
	if (rpl->status == CPL_ERR_ABORT_FAILED)
		goto discard;

	/*
	 * Sometimes we've already closed the connection, e.g., a post-close
	 * abort races with ABORT_REQ_RSS, the latter frees the connection
	 * expecting the ABORT_REQ will fail with CPL_ERR_ABORT_FAILED,
	 * but FW turns the ABORT_REQ into a regular one and so we get
	 * ABORT_RPL_RSS with status 0 and no connection.
	 */
	if (!c3cn)
		goto discard;

	process_cpl_msg_ref(process_abort_rpl, c3cn, skb);
	return 0;

discard:
	__kfree_skb(skb);
	return 0;
}

/*
 * Process RX_ISCSI_HDR CPL message: -> host
 * Handle received PDUs, the payload could be DDP'ed. If not, the payload
 * follow after the bhs.
 */
static void process_rx_iscsi_hdr(struct s3_conn *c3cn, struct sk_buff *skb)
{
	struct cpl_iscsi_hdr *hdr_cpl = cplhdr(skb);
	struct cpl_iscsi_hdr_norss data_cpl;
	struct cpl_rx_data_ddp_norss ddp_cpl;
	unsigned int hdr_len, data_len, status;
	unsigned int len;
	int err;

	if (unlikely(c3cn->state >= C3CN_STATE_PASSIVE_CLOSE)) {
		if (c3cn->state != C3CN_STATE_ABORTING)
			send_abort_req(c3cn);
		__kfree_skb(skb);
		return;
	}

	skb_tcp_seq(skb) = ntohl(hdr_cpl->seq);
	skb_flags(skb) = 0;

	skb_reset_transport_header(skb);
	__skb_pull(skb, sizeof(struct cpl_iscsi_hdr));

	len = hdr_len = ntohs(hdr_cpl->len);
	/* msg coalesce is off or not enough data received */
	if (skb->len <= hdr_len) {
		cxgb3i_log_error("%s: TID %u, ISCSI_HDR, skb len %u < %u.\n",
				 c3cn->cdev->name, c3cn->tid,
				 skb->len, hdr_len);
		goto abort_conn;
	}

	err = skb_copy_bits(skb, skb->len - sizeof(ddp_cpl), &ddp_cpl,
			    sizeof(ddp_cpl));
	if (err < 0)
		goto abort_conn;

	skb_ulp_mode(skb) = ULP2_FLAG_DATA_READY;
	skb_rx_pdulen(skb) = ntohs(ddp_cpl.len);
	skb_rx_ddigest(skb) = ntohl(ddp_cpl.ulp_crc);
	status = ntohl(ddp_cpl.ddp_status);

	c3cn_rx_debug("rx skb 0x%p, len %u, pdulen %u, ddp status 0x%x.\n",
		      skb, skb->len, skb_rx_pdulen(skb), status);

	if (status & (1 << RX_DDP_STATUS_HCRC_SHIFT))
		skb_ulp_mode(skb) |= ULP2_FLAG_HCRC_ERROR;
	if (status & (1 << RX_DDP_STATUS_DCRC_SHIFT))
		skb_ulp_mode(skb) |= ULP2_FLAG_DCRC_ERROR;
	if (status & (1 << RX_DDP_STATUS_PAD_SHIFT))
		skb_ulp_mode(skb) |= ULP2_FLAG_PAD_ERROR;

	if (skb->len > (hdr_len + sizeof(ddp_cpl))) {
		err = skb_copy_bits(skb, hdr_len, &data_cpl, sizeof(data_cpl));
		if (err < 0)
			goto abort_conn;
		data_len = ntohs(data_cpl.len);
		len += sizeof(data_cpl) + data_len;
	} else if (status & (1 << RX_DDP_STATUS_DDP_SHIFT))
		skb_ulp_mode(skb) |= ULP2_FLAG_DATA_DDPED;

	c3cn->rcv_nxt = ntohl(ddp_cpl.seq) + skb_rx_pdulen(skb);
	__pskb_trim(skb, len);
	__skb_queue_tail(&c3cn->receive_queue, skb);
	cxgb3i_conn_pdu_ready(c3cn);

	return;

abort_conn:
	send_abort_req(c3cn);
	__kfree_skb(skb);
}

static int do_iscsi_hdr(struct t3cdev *t3dev, struct sk_buff *skb, void *ctx)
{
	struct s3_conn *c3cn = ctx;

	process_cpl_msg(process_rx_iscsi_hdr, c3cn, skb);
	return 0;
}

/*
 * Process TX_DATA_ACK CPL messages: -> host
 * Process an acknowledgment of WR completion.  Advance snd_una and send the
 * next batch of work requests from the write queue.
 */
static void check_wr_invariants(struct s3_conn *c3cn)
{
	int pending = count_pending_wrs(c3cn);

	if (unlikely(c3cn->wr_avail + pending != c3cn->wr_max))
		cxgb3i_log_error("TID %u: credit imbalance: avail %u, "
				"pending %u, total should be %u\n",
				c3cn->tid, c3cn->wr_avail, pending,
				c3cn->wr_max);
}

static void process_wr_ack(struct s3_conn *c3cn, struct sk_buff *skb)
{
	struct cpl_wr_ack *hdr = cplhdr(skb);
	unsigned int credits = ntohs(hdr->credits);
	u32 snd_una = ntohl(hdr->snd_una);

	c3cn_tx_debug("%u WR credits, avail %u, unack %u, TID %u, state %u.\n",
			credits, c3cn->wr_avail, c3cn->wr_unacked,
			c3cn->tid, c3cn->state);

	c3cn->wr_avail += credits;
	if (c3cn->wr_unacked > c3cn->wr_max - c3cn->wr_avail)
		c3cn->wr_unacked = c3cn->wr_max - c3cn->wr_avail;

	while (credits) {
		struct sk_buff *p = peek_wr(c3cn);

		if (unlikely(!p)) {
			cxgb3i_log_error("%u WR_ACK credits for TID %u with "
					 "nothing pending, state %u\n",
					 credits, c3cn->tid, c3cn->state);
			break;
		}
		if (unlikely(credits < p->csum)) {
			struct tx_data_wr *w = cplhdr(p);
			cxgb3i_log_error("TID %u got %u WR credits need %u, "
					 "len %u, main body %u, frags %u, "
					 "seq # %u, ACK una %u, ACK nxt %u, "
					 "WR_AVAIL %u, WRs pending %u\n",
					 c3cn->tid, credits, p->csum, p->len,
					 p->len - p->data_len,
					 skb_shinfo(p)->nr_frags,
					 ntohl(w->sndseq), snd_una,
					 ntohl(hdr->snd_nxt), c3cn->wr_avail,
					 count_pending_wrs(c3cn) - credits);
			p->csum -= credits;
			break;
		} else {
			dequeue_wr(c3cn);
			credits -= p->csum;
			free_wr_skb(p);
		}
	}

	check_wr_invariants(c3cn);

	if (unlikely(before(snd_una, c3cn->snd_una))) {
		cxgb3i_log_error("TID %u, unexpected sequence # %u in WR_ACK "
				 "snd_una %u\n",
				 c3cn->tid, snd_una, c3cn->snd_una);
		goto out_free;
	}

	if (c3cn->snd_una != snd_una) {
		c3cn->snd_una = snd_una;
		dst_confirm(c3cn->dst_cache);
	}

	if (skb_queue_len(&c3cn->write_queue)) {
		if (c3cn_push_tx_frames(c3cn, 0))
			cxgb3i_conn_tx_open(c3cn);
	} else
		cxgb3i_conn_tx_open(c3cn);
out_free:
	__kfree_skb(skb);
}

static int do_wr_ack(struct t3cdev *cdev, struct sk_buff *skb, void *ctx)
{
	struct s3_conn *c3cn = ctx;

	process_cpl_msg(process_wr_ack, c3cn, skb);
	return 0;
}

/*
 * for each connection, pre-allocate skbs needed for close/abort requests. So
 * that we can service the request right away.
 */
static void c3cn_free_cpl_skbs(struct s3_conn *c3cn)
{
	if (c3cn->cpl_close)
		kfree_skb(c3cn->cpl_close);
	if (c3cn->cpl_abort_req)
		kfree_skb(c3cn->cpl_abort_req);
	if (c3cn->cpl_abort_rpl)
		kfree_skb(c3cn->cpl_abort_rpl);
}

static int c3cn_alloc_cpl_skbs(struct s3_conn *c3cn)
{
	c3cn->cpl_close = alloc_skb(sizeof(struct cpl_close_con_req),
				   GFP_KERNEL);
	if (!c3cn->cpl_close)
		return -ENOMEM;
	skb_put(c3cn->cpl_close, sizeof(struct cpl_close_con_req));

	c3cn->cpl_abort_req = alloc_skb(sizeof(struct cpl_abort_req),
					GFP_KERNEL);
	if (!c3cn->cpl_abort_req)
		goto free_cpl_skbs;
	skb_put(c3cn->cpl_abort_req, sizeof(struct cpl_abort_req));

	c3cn->cpl_abort_rpl = alloc_skb(sizeof(struct cpl_abort_rpl),
					GFP_KERNEL);
	if (!c3cn->cpl_abort_rpl)
		goto free_cpl_skbs;
	skb_put(c3cn->cpl_abort_rpl, sizeof(struct cpl_abort_rpl));

	return 0;

free_cpl_skbs:
	c3cn_free_cpl_skbs(c3cn);
	return -ENOMEM;
}

/**
 * c3cn_release_offload_resources - release offload resource
 * @c3cn: the offloaded iscsi tcp connection.
 * Release resources held by an offload connection (TID, L2T entry, etc.)
 */
static void c3cn_release_offload_resources(struct s3_conn *c3cn)
{
	struct t3cdev *cdev = c3cn->cdev;
	unsigned int tid = c3cn->tid;

	c3cn->qset = 0;
	c3cn_free_cpl_skbs(c3cn);

	if (c3cn->wr_avail != c3cn->wr_max) {
		purge_wr_queue(c3cn);
		reset_wr_list(c3cn);
	}

	if (cdev) {
		if (c3cn->l2t) {
			l2t_release(L2DATA(cdev), c3cn->l2t);
			c3cn->l2t = NULL;
		}
		if (c3cn->state == C3CN_STATE_CONNECTING)
			/* we have ATID */
			s3_free_atid(cdev, tid);
		else {
			/* we have TID */
			cxgb3_remove_tid(cdev, (void *)c3cn, tid);
			c3cn_put(c3cn);
		}
	}

	c3cn->dst_cache = NULL;
	c3cn->cdev = NULL;
}

/**
 * cxgb3i_c3cn_create - allocate and initialize an s3_conn structure
 * returns the s3_conn structure allocated.
 */
struct s3_conn *cxgb3i_c3cn_create(void)
{
	struct s3_conn *c3cn;

	c3cn = kzalloc(sizeof(*c3cn), GFP_KERNEL);
	if (!c3cn)
		return NULL;

	/* pre-allocate close/abort cpl, so we don't need to wait for memory
	   when close/abort is requested. */
	if (c3cn_alloc_cpl_skbs(c3cn) < 0)
		goto free_c3cn;

	c3cn_conn_debug("alloc c3cn 0x%p.\n", c3cn);

	c3cn->flags = 0;
	spin_lock_init(&c3cn->lock);
	atomic_set(&c3cn->refcnt, 1);
	skb_queue_head_init(&c3cn->receive_queue);
	skb_queue_head_init(&c3cn->write_queue);
	setup_timer(&c3cn->retry_timer, NULL, (unsigned long)c3cn);
	rwlock_init(&c3cn->callback_lock);

	return c3cn;

free_c3cn:
	kfree(c3cn);
	return NULL;
}

static void c3cn_active_close(struct s3_conn *c3cn)
{
	int data_lost;
	int close_req = 0;

	c3cn_conn_debug("c3cn 0x%p, state %u, flag 0x%lx.\n",
			 c3cn, c3cn->state, c3cn->flags);

	dst_confirm(c3cn->dst_cache);

	c3cn_hold(c3cn);
	spin_lock_bh(&c3cn->lock);

	data_lost = skb_queue_len(&c3cn->receive_queue);
	__skb_queue_purge(&c3cn->receive_queue);

	switch (c3cn->state) {
	case C3CN_STATE_CLOSED:
	case C3CN_STATE_ACTIVE_CLOSE:
	case C3CN_STATE_CLOSE_WAIT_1:
	case C3CN_STATE_CLOSE_WAIT_2:
	case C3CN_STATE_ABORTING:
		/* nothing need to be done */
		break;
	case C3CN_STATE_CONNECTING:
		/* defer until cpl_act_open_rpl or cpl_act_establish */
		c3cn_set_flag(c3cn, C3CN_ACTIVE_CLOSE_NEEDED);
		break;
	case C3CN_STATE_ESTABLISHED:
		close_req = 1;
		c3cn_set_state(c3cn, C3CN_STATE_ACTIVE_CLOSE);
		break;
	case C3CN_STATE_PASSIVE_CLOSE:
		close_req = 1;
		c3cn_set_state(c3cn, C3CN_STATE_CLOSE_WAIT_2);
		break;
	}

	if (close_req) {
		if (data_lost)
			/* Unread data was tossed, zap the connection. */
			send_abort_req(c3cn);
		else
			send_close_req(c3cn);
	}

	spin_unlock_bh(&c3cn->lock);
	c3cn_put(c3cn);
}

/**
 * cxgb3i_c3cn_release - close and release an iscsi tcp connection and any
 * 	resource held
 * @c3cn: the iscsi tcp connection
 */
void cxgb3i_c3cn_release(struct s3_conn *c3cn)
{
	c3cn_conn_debug("c3cn 0x%p, s %u, f 0x%lx.\n",
			c3cn, c3cn->state, c3cn->flags);
	if (unlikely(c3cn->state == C3CN_STATE_CONNECTING))
		c3cn_set_flag(c3cn, C3CN_ACTIVE_CLOSE_NEEDED);
	else if (likely(c3cn->state != C3CN_STATE_CLOSED))
		c3cn_active_close(c3cn);
	c3cn_put(c3cn);
}

static int is_cxgb3_dev(struct net_device *dev)
{
	struct cxgb3i_sdev_data *cdata;
	struct net_device *ndev = dev;

	if (dev->priv_flags & IFF_802_1Q_VLAN)
		ndev = vlan_dev_real_dev(dev);

	write_lock(&cdata_rwlock);
	list_for_each_entry(cdata, &cdata_list, list) {
		struct adap_ports *ports = &cdata->ports;
		int i;

		for (i = 0; i < ports->nports; i++)
			if (ndev == ports->lldevs[i]) {
				write_unlock(&cdata_rwlock);
				return 1;
			}
	}
	write_unlock(&cdata_rwlock);
	return 0;
}

/**
 * cxgb3_egress_dev - return the cxgb3 egress device
 * @root_dev: the root device anchoring the search
 * @c3cn: the connection used to determine egress port in bonding mode
 * @context: in bonding mode, indicates a connection set up or failover
 *
 * Return egress device or NULL if the egress device isn't one of our ports.
 */
static struct net_device *cxgb3_egress_dev(struct net_device *root_dev,
					   struct s3_conn *c3cn,
					   int context)
{
	while (root_dev) {
		if (root_dev->priv_flags & IFF_802_1Q_VLAN)
			root_dev = vlan_dev_real_dev(root_dev);
		else if (is_cxgb3_dev(root_dev))
			return root_dev;
		else
			return NULL;
	}
	return NULL;
}

static struct rtable *find_route(struct net_device *dev,
				 __be32 saddr, __be32 daddr,
				 __be16 sport, __be16 dport)
{
	struct rtable *rt;
	struct flowi fl = {
		.oif = dev ? dev->ifindex : 0,
		.nl_u = {
			 .ip4_u = {
				   .daddr = daddr,
				   .saddr = saddr,
				   .tos = 0 } },
		.proto = IPPROTO_TCP,
		.uli_u = {
			  .ports = {
				    .sport = sport,
				    .dport = dport } } };

	if (ip_route_output_flow(&init_net, &rt, &fl, NULL, 0))
		return NULL;
	return rt;
}

/*
 * Assign offload parameters to some connection fields.
 */
static void init_offload_conn(struct s3_conn *c3cn,
			      struct t3cdev *cdev,
			      struct dst_entry *dst)
{
	BUG_ON(c3cn->cdev != cdev);
	c3cn->wr_max = c3cn->wr_avail = T3C_DATA(cdev)->max_wrs - 1;
	c3cn->wr_unacked = 0;
	c3cn->mss_idx = select_mss(c3cn, dst_mtu(dst));

	reset_wr_list(c3cn);
}

static int initiate_act_open(struct s3_conn *c3cn, struct net_device *dev)
{
	struct cxgb3i_sdev_data *cdata = NDEV2CDATA(dev);
	struct t3cdev *cdev = cdata->cdev;
	struct dst_entry *dst = c3cn->dst_cache;
	struct sk_buff *skb;

	c3cn_conn_debug("c3cn 0x%p, state %u, flag 0x%lx.\n",
			c3cn, c3cn->state, c3cn->flags);
	/*
	 * Initialize connection data.  Note that the flags and ULP mode are
	 * initialized higher up ...
	 */
	c3cn->dev = dev;
	c3cn->cdev = cdev;
	c3cn->tid = cxgb3_alloc_atid(cdev, cdata->client, c3cn);
	if (c3cn->tid < 0)
		goto out_err;

	c3cn->qset = 0;
	c3cn->l2t = t3_l2t_get(cdev, dst->neighbour, dev);
	if (!c3cn->l2t)
		goto free_tid;

	skb = alloc_skb(sizeof(struct cpl_act_open_req), GFP_KERNEL);
	if (!skb)
		goto free_l2t;

	skb->sk = (struct sock *)c3cn;
	set_arp_failure_handler(skb, act_open_req_arp_failure);

	c3cn_hold(c3cn);

	init_offload_conn(c3cn, cdev, dst);
	c3cn->err = 0;

	make_act_open_req(c3cn, skb, c3cn->tid, c3cn->l2t);
	l2t_send(cdev, skb, c3cn->l2t);
	return 0;

free_l2t:
	l2t_release(L2DATA(cdev), c3cn->l2t);
free_tid:
	s3_free_atid(cdev, c3cn->tid);
	c3cn->tid = 0;
out_err:
	return -EINVAL;
}

/**
 * cxgb3i_find_dev - find the interface associated with the given address
 * @ipaddr: ip address
 */
static struct net_device *
cxgb3i_find_dev(struct net_device *dev, __be32 ipaddr)
{
	struct flowi fl;
	int err;
	struct rtable *rt;

	memset(&fl, 0, sizeof(fl));
	fl.nl_u.ip4_u.daddr = ipaddr;

	err = ip_route_output_key(dev ? dev_net(dev) : &init_net, &rt, &fl);
	if (!err)
		return (&rt->u.dst)->dev;

	return NULL;
}

/**
 * cxgb3i_c3cn_connect - initiates an iscsi tcp connection to a given address
 * @c3cn: the iscsi tcp connection
 * @usin: destination address
 *
 * return 0 if active open request is sent, < 0 otherwise.
 */
int cxgb3i_c3cn_connect(struct net_device *dev, struct s3_conn *c3cn,
			struct sockaddr_in *usin)
{
	struct rtable *rt;
	struct cxgb3i_sdev_data *cdata;
	struct t3cdev *cdev;
	__be32 sipv4;
	struct net_device *dstdev;
	int err;

	c3cn_conn_debug("c3cn 0x%p, dev 0x%p.\n", c3cn, dev);

	if (usin->sin_family != AF_INET)
		return -EAFNOSUPPORT;

	c3cn->daddr.sin_port = usin->sin_port;
	c3cn->daddr.sin_addr.s_addr = usin->sin_addr.s_addr;

	dstdev = cxgb3i_find_dev(dev, usin->sin_addr.s_addr);
	if (!dstdev || !is_cxgb3_dev(dstdev))
		return -ENETUNREACH;

	if (dstdev->priv_flags & IFF_802_1Q_VLAN)
		dev = dstdev;

	rt = find_route(dev, c3cn->saddr.sin_addr.s_addr,
			c3cn->daddr.sin_addr.s_addr,
			c3cn->saddr.sin_port,
			c3cn->daddr.sin_port);
	if (rt == NULL) {
		c3cn_conn_debug("NO route to 0x%x, port %u, dev %s.\n",
				c3cn->daddr.sin_addr.s_addr,
				ntohs(c3cn->daddr.sin_port),
				dev ? dev->name : "any");
		return -ENETUNREACH;
	}

	if (rt->rt_flags & (RTCF_MULTICAST | RTCF_BROADCAST)) {
		c3cn_conn_debug("multi-cast route to 0x%x, port %u, dev %s.\n",
				c3cn->daddr.sin_addr.s_addr,
				ntohs(c3cn->daddr.sin_port),
				dev ? dev->name : "any");
		ip_rt_put(rt);
		return -ENETUNREACH;
	}

	if (!c3cn->saddr.sin_addr.s_addr)
		c3cn->saddr.sin_addr.s_addr = rt->rt_src;

	/* now commit destination to connection */
	c3cn->dst_cache = &rt->u.dst;

	/* try to establish an offloaded connection */
	dev = cxgb3_egress_dev(c3cn->dst_cache->dev, c3cn, 0);
	if (dev == NULL) {
		c3cn_conn_debug("c3cn 0x%p, egress dev NULL.\n", c3cn);
		return -ENETUNREACH;
	}
	cdata = NDEV2CDATA(dev);
	cdev = cdata->cdev;

	/* get a source port if one hasn't been provided */
	err = c3cn_get_port(c3cn, cdata);
	if (err)
		return err;

	c3cn_conn_debug("c3cn 0x%p get port %u.\n",
			c3cn, ntohs(c3cn->saddr.sin_port));

	sipv4 = cxgb3i_get_private_ipv4addr(dev);
	if (!sipv4) {
		c3cn_conn_debug("c3cn 0x%p, iscsi ip not configured.\n", c3cn);
		sipv4 = c3cn->saddr.sin_addr.s_addr;
		cxgb3i_set_private_ipv4addr(dev, sipv4);
	} else
		c3cn->saddr.sin_addr.s_addr = sipv4;

	c3cn_conn_debug("c3cn 0x%p, %pI4,%u-%pI4,%u SYN_SENT.\n",
			c3cn,
			&c3cn->saddr.sin_addr.s_addr,
			ntohs(c3cn->saddr.sin_port),
			&c3cn->daddr.sin_addr.s_addr,
			ntohs(c3cn->daddr.sin_port));

	c3cn_set_state(c3cn, C3CN_STATE_CONNECTING);
	if (!initiate_act_open(c3cn, dev))
		return 0;

	/*
	 * If we get here, we don't have an offload connection so simply
	 * return a failure.
	 */
	err = -ENOTSUPP;

	/*
	 * This trashes the connection and releases the local port,
	 * if necessary.
	 */
	c3cn_conn_debug("c3cn 0x%p -> CLOSED.\n", c3cn);
	c3cn_set_state(c3cn, C3CN_STATE_CLOSED);
	ip_rt_put(rt);
	c3cn_put_port(c3cn);
	return err;
}

/**
 * cxgb3i_c3cn_rx_credits - ack received tcp data.
 * @c3cn: iscsi tcp connection
 * @copied: # of bytes processed
 *
 * Called after some received data has been read.  It returns RX credits
 * to the HW for the amount of data processed.
 */
void cxgb3i_c3cn_rx_credits(struct s3_conn *c3cn, int copied)
{
	struct t3cdev *cdev;
	int must_send;
	u32 credits, dack = 0;

	if (c3cn->state != C3CN_STATE_ESTABLISHED)
		return;

	credits = c3cn->copied_seq - c3cn->rcv_wup;
	if (unlikely(!credits))
		return;

	cdev = c3cn->cdev;

	if (unlikely(cxgb3_rx_credit_thres == 0))
		return;

	dack = F_RX_DACK_CHANGE | V_RX_DACK_MODE(1);

	/*
	 * For coalescing to work effectively ensure the receive window has
	 * at least 16KB left.
	 */
	must_send = credits + 16384 >= cxgb3_rcv_win;

	if (must_send || credits >= cxgb3_rx_credit_thres)
		c3cn->rcv_wup += send_rx_credits(c3cn, credits, dack);
}

/**
 * cxgb3i_c3cn_send_pdus - send the skbs containing iscsi pdus
 * @c3cn: iscsi tcp connection
 * @skb: skb contains the iscsi pdu
 *
 * Add a list of skbs to a connection send queue. The skbs must comply with
 * the max size limit of the device and have a headroom of at least
 * TX_HEADER_LEN bytes.
 * Return # of bytes queued.
 */
int cxgb3i_c3cn_send_pdus(struct s3_conn *c3cn, struct sk_buff *skb)
{
	struct sk_buff *next;
	int err, copied = 0;

	spin_lock_bh(&c3cn->lock);

	if (c3cn->state != C3CN_STATE_ESTABLISHED) {
		c3cn_tx_debug("c3cn 0x%p, not in est. state %u.\n",
			      c3cn, c3cn->state);
		err = -EAGAIN;
		goto out_err;
	}

	if (c3cn->err) {
		c3cn_tx_debug("c3cn 0x%p, err %d.\n", c3cn, c3cn->err);
		err = -EPIPE;
		goto out_err;
	}

	if (c3cn->write_seq - c3cn->snd_una >= cxgb3_snd_win) {
		c3cn_tx_debug("c3cn 0x%p, snd %u - %u > %u.\n",
				c3cn, c3cn->write_seq, c3cn->snd_una,
				cxgb3_snd_win);
		err = -ENOBUFS;
		goto out_err;
	}

	while (skb) {
		int frags = skb_shinfo(skb)->nr_frags +
				(skb->len != skb->data_len);

		if (unlikely(skb_headroom(skb) < TX_HEADER_LEN)) {
			c3cn_tx_debug("c3cn 0x%p, skb head.\n", c3cn);
			err = -EINVAL;
			goto out_err;
		}

		if (frags >= SKB_WR_LIST_SIZE) {
			cxgb3i_log_error("c3cn 0x%p, tx frags %d, len %u,%u.\n",
					 c3cn, skb_shinfo(skb)->nr_frags,
					 skb->len, skb->data_len);
			err = -EINVAL;
			goto out_err;
		}

		next = skb->next;
		skb->next = NULL;
		skb_entail(c3cn, skb, C3CB_FLAG_NO_APPEND | C3CB_FLAG_NEED_HDR);
		copied += skb->len;
		c3cn->write_seq += skb->len + ulp_extra_len(skb);
		skb = next;
	}
done:
	if (likely(skb_queue_len(&c3cn->write_queue)))
		c3cn_push_tx_frames(c3cn, 1);
	spin_unlock_bh(&c3cn->lock);
	return copied;

out_err:
	if (copied == 0 && err == -EPIPE)
		copied = c3cn->err ? c3cn->err : -EPIPE;
	else
		copied = err;
	goto done;
}

static void sdev_data_cleanup(struct cxgb3i_sdev_data *cdata)
{
	struct adap_ports *ports = &cdata->ports;
	struct s3_conn *c3cn;
	int i;

	for (i = 0; i < cxgb3_max_connect; i++) {
		if (cdata->sport_conn[i]) {
			c3cn = cdata->sport_conn[i];
			cdata->sport_conn[i] = NULL;

			spin_lock_bh(&c3cn->lock);
			c3cn->cdev = NULL;
			c3cn_set_flag(c3cn, C3CN_OFFLOAD_DOWN);
			c3cn_closed(c3cn);
			spin_unlock_bh(&c3cn->lock);
		}
	}

	for (i = 0; i < ports->nports; i++)
		NDEV2CDATA(ports->lldevs[i]) = NULL;

	cxgb3i_free_big_mem(cdata);
}

void cxgb3i_sdev_cleanup(void)
{
	struct cxgb3i_sdev_data *cdata;

	write_lock(&cdata_rwlock);
	list_for_each_entry(cdata, &cdata_list, list) {
		list_del(&cdata->list);
		sdev_data_cleanup(cdata);
	}
	write_unlock(&cdata_rwlock);
}

int cxgb3i_sdev_init(cxgb3_cpl_handler_func *cpl_handlers)
{
	cpl_handlers[CPL_ACT_ESTABLISH] = do_act_establish;
	cpl_handlers[CPL_ACT_OPEN_RPL] = do_act_open_rpl;
	cpl_handlers[CPL_PEER_CLOSE] = do_peer_close;
	cpl_handlers[CPL_ABORT_REQ_RSS] = do_abort_req;
	cpl_handlers[CPL_ABORT_RPL_RSS] = do_abort_rpl;
	cpl_handlers[CPL_CLOSE_CON_RPL] = do_close_con_rpl;
	cpl_handlers[CPL_TX_DMA_ACK] = do_wr_ack;
	cpl_handlers[CPL_ISCSI_HDR] = do_iscsi_hdr;

	if (cxgb3_max_connect > CXGB3I_MAX_CONN)
		cxgb3_max_connect = CXGB3I_MAX_CONN;
	return 0;
}

/**
 * cxgb3i_sdev_add - allocate and initialize resources for each adapter found
 * @cdev:	t3cdev adapter
 * @client:	cxgb3 driver client
 */
void cxgb3i_sdev_add(struct t3cdev *cdev, struct cxgb3_client *client)
{
	struct cxgb3i_sdev_data *cdata;
	struct ofld_page_info rx_page_info;
	unsigned int wr_len;
	int mapsize = cxgb3_max_connect * sizeof(struct s3_conn *);
	int i;

	cdata =  cxgb3i_alloc_big_mem(sizeof(*cdata) + mapsize, GFP_KERNEL);
	if (!cdata) {
		cxgb3i_log_warn("t3dev 0x%p, offload up, OOM %d.\n",
				cdev, mapsize);
		return;
	}

	if (cdev->ctl(cdev, GET_WR_LEN, &wr_len) < 0 ||
	    cdev->ctl(cdev, GET_PORTS, &cdata->ports) < 0 ||
	    cdev->ctl(cdev, GET_RX_PAGE_INFO, &rx_page_info) < 0) {
		cxgb3i_log_warn("t3dev 0x%p, offload up, ioctl failed.\n",
				cdev);
		goto free_cdata;
	}

	s3_init_wr_tab(wr_len);

	spin_lock_init(&cdata->lock);
	INIT_LIST_HEAD(&cdata->list);
	cdata->cdev = cdev;
	cdata->client = client;

	for (i = 0; i < cdata->ports.nports; i++)
		NDEV2CDATA(cdata->ports.lldevs[i]) = cdata;

	write_lock(&cdata_rwlock);
	list_add_tail(&cdata->list, &cdata_list);
	write_unlock(&cdata_rwlock);

	cxgb3i_log_info("t3dev 0x%p, offload up, added.\n", cdev);
	return;

free_cdata:
	cxgb3i_free_big_mem(cdata);
}

/**
 * cxgb3i_sdev_remove - free the allocated resources for the adapter
 * @cdev:	t3cdev adapter
 */
void cxgb3i_sdev_remove(struct t3cdev *cdev)
{
	struct cxgb3i_sdev_data *cdata = CXGB3_SDEV_DATA(cdev);

	cxgb3i_log_info("t3dev 0x%p, offload down, remove.\n", cdev);

	write_lock(&cdata_rwlock);
	list_del(&cdata->list);
	write_unlock(&cdata_rwlock);

	sdev_data_cleanup(cdata);
}
