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

#define pr_fmt(fmt) KBUILD_MODNAME ":%s: " fmt, __func__

#include <linux/version.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <scsi/scsi_host.h>

#include "common.h"
#include "t3_cpl.h"
#include "t3cdev.h"
#include "cxgb3_defs.h"
#include "cxgb3_ctl_defs.h"
#include "cxgb3_offload.h"
#include "firmware_exports.h"
#include "cxgb3i.h"

static unsigned int dbg_level;
#include "../libcxgbi.h"

#define DRV_MODULE_NAME         "cxgb3i"
#define DRV_MODULE_DESC         "Chelsio T3 iSCSI Driver"
#define DRV_MODULE_VERSION	"2.0.0"
#define DRV_MODULE_RELDATE	"Jun. 2010"

static char version[] =
	DRV_MODULE_DESC " " DRV_MODULE_NAME
	" v" DRV_MODULE_VERSION " (" DRV_MODULE_RELDATE ")\n";

MODULE_AUTHOR("Chelsio Communications, Inc.");
MODULE_DESCRIPTION(DRV_MODULE_DESC);
MODULE_VERSION(DRV_MODULE_VERSION);
MODULE_LICENSE("GPL");

module_param(dbg_level, uint, 0644);
MODULE_PARM_DESC(dbg_level, "debug flag (default=0)");

static int cxgb3i_rcv_win = 256 * 1024;
module_param(cxgb3i_rcv_win, int, 0644);
MODULE_PARM_DESC(cxgb3i_rcv_win, "TCP receive window in bytes (default=256KB)");

static int cxgb3i_snd_win = 128 * 1024;
module_param(cxgb3i_snd_win, int, 0644);
MODULE_PARM_DESC(cxgb3i_snd_win, "TCP send window in bytes (default=128KB)");

static int cxgb3i_rx_credit_thres = 10 * 1024;
module_param(cxgb3i_rx_credit_thres, int, 0644);
MODULE_PARM_DESC(rx_credit_thres,
		 "RX credits return threshold in bytes (default=10KB)");

static unsigned int cxgb3i_max_connect = 8 * 1024;
module_param(cxgb3i_max_connect, uint, 0644);
MODULE_PARM_DESC(cxgb3i_max_connect, "Max. # of connections (default=8092)");

static unsigned int cxgb3i_sport_base = 20000;
module_param(cxgb3i_sport_base, uint, 0644);
MODULE_PARM_DESC(cxgb3i_sport_base, "starting port number (default=20000)");

static void cxgb3i_dev_open(struct t3cdev *);
static void cxgb3i_dev_close(struct t3cdev *);
static void cxgb3i_dev_event_handler(struct t3cdev *, u32, u32);

static struct cxgb3_client t3_client = {
	.name = DRV_MODULE_NAME,
	.handlers = cxgb3i_cpl_handlers,
	.add = cxgb3i_dev_open,
	.remove = cxgb3i_dev_close,
	.event_handler = cxgb3i_dev_event_handler,
};

static struct scsi_host_template cxgb3i_host_template = {
	.module		= THIS_MODULE,
	.name		= DRV_MODULE_NAME,
	.proc_name	= DRV_MODULE_NAME,
	.can_queue	= CXGB3I_SCSI_HOST_QDEPTH,
	.queuecommand	= iscsi_queuecommand,
	.change_queue_depth = iscsi_change_queue_depth,
	.sg_tablesize	= SG_ALL,
	.max_sectors	= 0xFFFF,
	.cmd_per_lun	= ISCSI_DEF_CMD_PER_LUN,
	.eh_abort_handler = iscsi_eh_abort,
	.eh_device_reset_handler = iscsi_eh_device_reset,
	.eh_target_reset_handler = iscsi_eh_recover_target,
	.target_alloc	= iscsi_target_alloc,
	.use_clustering	= DISABLE_CLUSTERING,
	.this_id	= -1,
};

static struct iscsi_transport cxgb3i_iscsi_transport = {
	.owner		= THIS_MODULE,
	.name		= DRV_MODULE_NAME,
	/* owner and name should be set already */
	.caps		= CAP_RECOVERY_L0 | CAP_MULTI_R2T | CAP_HDRDGST
				| CAP_DATADGST | CAP_DIGEST_OFFLOAD |
				CAP_PADDING_OFFLOAD | CAP_TEXT_NEGO,
	.param_mask	= ISCSI_MAX_RECV_DLENGTH | ISCSI_MAX_XMIT_DLENGTH |
				ISCSI_HDRDGST_EN | ISCSI_DATADGST_EN |
				ISCSI_INITIAL_R2T_EN | ISCSI_MAX_R2T |
				ISCSI_IMM_DATA_EN | ISCSI_FIRST_BURST |
				ISCSI_MAX_BURST | ISCSI_PDU_INORDER_EN |
				ISCSI_DATASEQ_INORDER_EN | ISCSI_ERL |
				ISCSI_CONN_PORT | ISCSI_CONN_ADDRESS |
				ISCSI_EXP_STATSN | ISCSI_PERSISTENT_PORT |
				ISCSI_PERSISTENT_ADDRESS |
				ISCSI_TARGET_NAME | ISCSI_TPGT |
				ISCSI_USERNAME | ISCSI_PASSWORD |
				ISCSI_USERNAME_IN | ISCSI_PASSWORD_IN |
				ISCSI_FAST_ABORT | ISCSI_ABORT_TMO |
				ISCSI_LU_RESET_TMO | ISCSI_TGT_RESET_TMO |
				ISCSI_PING_TMO | ISCSI_RECV_TMO |
				ISCSI_IFACE_NAME | ISCSI_INITIATOR_NAME,
	.host_param_mask	= ISCSI_HOST_HWADDRESS | ISCSI_HOST_IPADDRESS |
				ISCSI_HOST_INITIATOR_NAME |
				ISCSI_HOST_NETDEV_NAME,
	.get_host_param	= cxgbi_get_host_param,
	.set_host_param	= cxgbi_set_host_param,
	/* session management */
	.create_session	= cxgbi_create_session,
	.destroy_session	= cxgbi_destroy_session,
	.get_session_param = iscsi_session_get_param,
	/* connection management */
	.create_conn	= cxgbi_create_conn,
	.bind_conn	= cxgbi_bind_conn,
	.destroy_conn	= iscsi_tcp_conn_teardown,
	.start_conn	= iscsi_conn_start,
	.stop_conn	= iscsi_conn_stop,
	.get_conn_param	= iscsi_conn_get_param,
	.set_param	= cxgbi_set_conn_param,
	.get_stats	= cxgbi_get_conn_stats,
	/* pdu xmit req from user space */
	.send_pdu	= iscsi_conn_send_pdu,
	/* task */
	.init_task	= iscsi_tcp_task_init,
	.xmit_task	= iscsi_tcp_task_xmit,
	.cleanup_task	= cxgbi_cleanup_task,
	/* pdu */
	.alloc_pdu	= cxgbi_conn_alloc_pdu,
	.init_pdu	= cxgbi_conn_init_pdu,
	.xmit_pdu	= cxgbi_conn_xmit_pdu,
	.parse_pdu_itt	= cxgbi_parse_pdu_itt,
	/* TCP connect/disconnect */
	.get_ep_param	= cxgbi_get_ep_param,
	.ep_connect	= cxgbi_ep_connect,
	.ep_poll	= cxgbi_ep_poll,
	.ep_disconnect	= cxgbi_ep_disconnect,
	/* Error recovery timeout call */
	.session_recovery_timedout = iscsi_session_recovery_timedout,
};

static struct scsi_transport_template *cxgb3i_stt;

/*
 * CPL (Chelsio Protocol Language) defines a message passing interface between
 * the host driver and Chelsio asic.
 * The section below implments CPLs that related to iscsi tcp connection
 * open/close/abort and data send/receive.
 */

static int push_tx_frames(struct cxgbi_sock *csk, int req_completion);

static void send_act_open_req(struct cxgbi_sock *csk, struct sk_buff *skb,
			      const struct l2t_entry *e)
{
	unsigned int wscale = cxgbi_sock_compute_wscale(cxgb3i_rcv_win);
	struct cpl_act_open_req *req = (struct cpl_act_open_req *)skb->head;

	skb->priority = CPL_PRIORITY_SETUP;

	req->wr.wr_hi = htonl(V_WR_OP(FW_WROPCODE_FORWARD));
	OPCODE_TID(req) = htonl(MK_OPCODE_TID(CPL_ACT_OPEN_REQ, csk->atid));
	req->local_port = csk->saddr.sin_port;
	req->peer_port = csk->daddr.sin_port;
	req->local_ip = csk->saddr.sin_addr.s_addr;
	req->peer_ip = csk->daddr.sin_addr.s_addr;

	req->opt0h = htonl(V_KEEP_ALIVE(1) | F_TCAM_BYPASS |
			V_WND_SCALE(wscale) | V_MSS_IDX(csk->mss_idx) |
			V_L2T_IDX(e->idx) | V_TX_CHANNEL(e->smt_idx));
	req->opt0l = htonl(V_ULP_MODE(ULP2_MODE_ISCSI) |
			V_RCV_BUFSIZ(cxgb3i_rcv_win>>10));

	log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_SOCK,
		"csk 0x%p,%u,0x%lx,%u, %pI4:%u-%pI4:%u, %u,%u,%u.\n",
		csk, csk->state, csk->flags, csk->atid,
		&req->local_ip, ntohs(req->local_port),
		&req->peer_ip, ntohs(req->peer_port),
		csk->mss_idx, e->idx, e->smt_idx);

	l2t_send(csk->cdev->lldev, skb, csk->l2t);
}

static inline void act_open_arp_failure(struct t3cdev *dev, struct sk_buff *skb)
{
	cxgbi_sock_act_open_req_arp_failure(NULL, skb);
}

/*
 * CPL connection close request: host ->
 *
 * Close a connection by sending a CPL_CLOSE_CON_REQ message and queue it to
 * the write queue (i.e., after any unsent txt data).
 */
static void send_close_req(struct cxgbi_sock *csk)
{
	struct sk_buff *skb = csk->cpl_close;
	struct cpl_close_con_req *req = (struct cpl_close_con_req *)skb->head;
	unsigned int tid = csk->tid;

	log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_SOCK,
		"csk 0x%p,%u,0x%lx,%u.\n",
		csk, csk->state, csk->flags, csk->tid);

	csk->cpl_close = NULL;
	req->wr.wr_hi = htonl(V_WR_OP(FW_WROPCODE_OFLD_CLOSE_CON));
	req->wr.wr_lo = htonl(V_WR_TID(tid));
	OPCODE_TID(req) = htonl(MK_OPCODE_TID(CPL_CLOSE_CON_REQ, tid));
	req->rsvd = htonl(csk->write_seq);

	cxgbi_sock_skb_entail(csk, skb);
	if (csk->state >= CTP_ESTABLISHED)
		push_tx_frames(csk, 1);
}

/*
 * CPL connection abort request: host ->
 *
 * Send an ABORT_REQ message. Makes sure we do not send multiple ABORT_REQs
 * for the same connection and also that we do not try to send a message
 * after the connection has closed.
 */
static void abort_arp_failure(struct t3cdev *tdev, struct sk_buff *skb)
{
	struct cpl_abort_req *req = cplhdr(skb);

	log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_SOCK,
		"t3dev 0x%p, tid %u, skb 0x%p.\n",
		tdev, GET_TID(req), skb);
	req->cmd = CPL_ABORT_NO_RST;
	cxgb3_ofld_send(tdev, skb);
}

static void send_abort_req(struct cxgbi_sock *csk)
{
	struct sk_buff *skb = csk->cpl_abort_req;
	struct cpl_abort_req *req;

	if (unlikely(csk->state == CTP_ABORTING || !skb))
		return;
	cxgbi_sock_set_state(csk, CTP_ABORTING);
	cxgbi_sock_set_flag(csk, CTPF_ABORT_RPL_PENDING);
	/* Purge the send queue so we don't send anything after an abort. */
	cxgbi_sock_purge_write_queue(csk);

	csk->cpl_abort_req = NULL;
	req = (struct cpl_abort_req *)skb->head;
	skb->priority = CPL_PRIORITY_DATA;
	set_arp_failure_handler(skb, abort_arp_failure);
	req->wr.wr_hi = htonl(V_WR_OP(FW_WROPCODE_OFLD_HOST_ABORT_CON_REQ));
	req->wr.wr_lo = htonl(V_WR_TID(csk->tid));
	OPCODE_TID(req) = htonl(MK_OPCODE_TID(CPL_ABORT_REQ, csk->tid));
	req->rsvd0 = htonl(csk->snd_nxt);
	req->rsvd1 = !cxgbi_sock_flag(csk, CTPF_TX_DATA_SENT);
	req->cmd = CPL_ABORT_SEND_RST;

	log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_SOCK,
		"csk 0x%p,%u,0x%lx,%u, snd_nxt %u, 0x%x.\n",
		csk, csk->state, csk->flags, csk->tid, csk->snd_nxt,
		req->rsvd1);

	l2t_send(csk->cdev->lldev, skb, csk->l2t);
}

/*
 * CPL connection abort reply: host ->
 *
 * Send an ABORT_RPL message in response of the ABORT_REQ received.
 */
static void send_abort_rpl(struct cxgbi_sock *csk, int rst_status)
{
	struct sk_buff *skb = csk->cpl_abort_rpl;
	struct cpl_abort_rpl *rpl = (struct cpl_abort_rpl *)skb->head;

	log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_SOCK,
		"csk 0x%p,%u,0x%lx,%u, status %d.\n",
		csk, csk->state, csk->flags, csk->tid, rst_status);

	csk->cpl_abort_rpl = NULL;
	skb->priority = CPL_PRIORITY_DATA;
	rpl->wr.wr_hi = htonl(V_WR_OP(FW_WROPCODE_OFLD_HOST_ABORT_CON_RPL));
	rpl->wr.wr_lo = htonl(V_WR_TID(csk->tid));
	OPCODE_TID(rpl) = htonl(MK_OPCODE_TID(CPL_ABORT_RPL, csk->tid));
	rpl->cmd = rst_status;
	cxgb3_ofld_send(csk->cdev->lldev, skb);
}

/*
 * CPL connection rx data ack: host ->
 * Send RX credits through an RX_DATA_ACK CPL message. Returns the number of
 * credits sent.
 */
static u32 send_rx_credits(struct cxgbi_sock *csk, u32 credits)
{
	struct sk_buff *skb;
	struct cpl_rx_data_ack *req;
	u32 dack = F_RX_DACK_CHANGE | V_RX_DACK_MODE(1);

	log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_PDU_RX,
		"csk 0x%p,%u,0x%lx,%u, credit %u, dack %u.\n",
		csk, csk->state, csk->flags, csk->tid, credits, dack);

	skb = alloc_wr(sizeof(*req), 0, GFP_ATOMIC);
	if (!skb) {
		pr_info("csk 0x%p, credit %u, OOM.\n", csk, credits);
		return 0;
	}
	req = (struct cpl_rx_data_ack *)skb->head;
	req->wr.wr_hi = htonl(V_WR_OP(FW_WROPCODE_FORWARD));
	OPCODE_TID(req) = htonl(MK_OPCODE_TID(CPL_RX_DATA_ACK, csk->tid));
	req->credit_dack = htonl(F_RX_DACK_CHANGE | V_RX_DACK_MODE(1) |
				V_RX_CREDITS(credits));
	skb->priority = CPL_PRIORITY_ACK;
	cxgb3_ofld_send(csk->cdev->lldev, skb);
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

static unsigned int wrlen __read_mostly;
static unsigned int skb_wrs[SKB_WR_LIST_SIZE] __read_mostly;

static void init_wr_tab(unsigned int wr_len)
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

static inline void make_tx_data_wr(struct cxgbi_sock *csk, struct sk_buff *skb,
				   int len, int req_completion)
{
	struct tx_data_wr *req;
	struct l2t_entry *l2t = csk->l2t;

	skb_reset_transport_header(skb);
	req = (struct tx_data_wr *)__skb_push(skb, sizeof(*req));
	req->wr_hi = htonl(V_WR_OP(FW_WROPCODE_OFLD_TX_DATA) |
			(req_completion ? F_WR_COMPL : 0));
	req->wr_lo = htonl(V_WR_TID(csk->tid));
	/* len includes the length of any HW ULP additions */
	req->len = htonl(len);
	/* V_TX_ULP_SUBMODE sets both the mode and submode */
	req->flags = htonl(V_TX_ULP_SUBMODE(cxgbi_skcb_ulp_mode(skb)) |
			   V_TX_SHOVE((skb_peek(&csk->write_queue) ? 0 : 1)));
	req->sndseq = htonl(csk->snd_nxt);
	req->param = htonl(V_TX_PORT(l2t->smt_idx));

	if (!cxgbi_sock_flag(csk, CTPF_TX_DATA_SENT)) {
		req->flags |= htonl(V_TX_ACK_PAGES(2) | F_TX_INIT |
				    V_TX_CPU_IDX(csk->rss_qid));
		/* sendbuffer is in units of 32KB. */
		req->param |= htonl(V_TX_SNDBUF(cxgb3i_snd_win >> 15));
		cxgbi_sock_set_flag(csk, CTPF_TX_DATA_SENT);
	}
}

/**
 * push_tx_frames -- start transmit
 * @c3cn: the offloaded connection
 * @req_completion: request wr_ack or not
 *
 * Prepends TX_DATA_WR or CPL_CLOSE_CON_REQ headers to buffers waiting in a
 * connection's send queue and sends them on to T3.  Must be called with the
 * connection's lock held.  Returns the amount of send buffer space that was
 * freed as a result of sending queued data to T3.
 */

static void arp_failure_skb_discard(struct t3cdev *dev, struct sk_buff *skb)
{
	kfree_skb(skb);
}

static int push_tx_frames(struct cxgbi_sock *csk, int req_completion)
{
	int total_size = 0;
	struct sk_buff *skb;

	if (unlikely(csk->state < CTP_ESTABLISHED ||
		csk->state == CTP_CLOSE_WAIT_1 || csk->state >= CTP_ABORTING)) {
			log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_PDU_TX,
				"csk 0x%p,%u,0x%lx,%u, in closing state.\n",
				csk, csk->state, csk->flags, csk->tid);
		return 0;
	}

	while (csk->wr_cred && (skb = skb_peek(&csk->write_queue)) != NULL) {
		int len = skb->len;	/* length before skb_push */
		int frags = skb_shinfo(skb)->nr_frags + (len != skb->data_len);
		int wrs_needed = skb_wrs[frags];

		if (wrs_needed > 1 && len + sizeof(struct tx_data_wr) <= wrlen)
			wrs_needed = 1;

		WARN_ON(frags >= SKB_WR_LIST_SIZE || wrs_needed < 1);

		if (csk->wr_cred < wrs_needed) {
			log_debug(1 << CXGBI_DBG_PDU_TX,
				"csk 0x%p, skb len %u/%u, frag %u, wr %d<%u.\n",
				csk, skb->len, skb->data_len, frags,
				wrs_needed, csk->wr_cred);
			break;
		}

		__skb_unlink(skb, &csk->write_queue);
		skb->priority = CPL_PRIORITY_DATA;
		skb->csum = wrs_needed;	/* remember this until the WR_ACK */
		csk->wr_cred -= wrs_needed;
		csk->wr_una_cred += wrs_needed;
		cxgbi_sock_enqueue_wr(csk, skb);

		log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_PDU_TX,
			"csk 0x%p, enqueue, skb len %u/%u, frag %u, wr %d, "
			"left %u, unack %u.\n",
			csk, skb->len, skb->data_len, frags, skb->csum,
			csk->wr_cred, csk->wr_una_cred);

		if (likely(cxgbi_skcb_test_flag(skb, SKCBF_TX_NEED_HDR))) {
			if ((req_completion &&
				csk->wr_una_cred == wrs_needed) ||
			     csk->wr_una_cred >= csk->wr_max_cred / 2) {
				req_completion = 1;
				csk->wr_una_cred = 0;
			}
			len += cxgbi_ulp_extra_len(cxgbi_skcb_ulp_mode(skb));
			make_tx_data_wr(csk, skb, len, req_completion);
			csk->snd_nxt += len;
			cxgbi_skcb_clear_flag(skb, SKCBF_TX_NEED_HDR);
		}
		total_size += skb->truesize;
		log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_PDU_TX,
			"csk 0x%p, tid 0x%x, send skb 0x%p.\n",
			csk, csk->tid, skb);
		set_arp_failure_handler(skb, arp_failure_skb_discard);
		l2t_send(csk->cdev->lldev, skb, csk->l2t);
	}
	return total_size;
}

/*
 * Process a CPL_ACT_ESTABLISH message: -> host
 * Updates connection state from an active establish CPL message.  Runs with
 * the connection lock held.
 */

static inline void free_atid(struct cxgbi_sock *csk)
{
	if (cxgbi_sock_flag(csk, CTPF_HAS_ATID)) {
		cxgb3_free_atid(csk->cdev->lldev, csk->atid);
		cxgbi_sock_clear_flag(csk, CTPF_HAS_ATID);
		cxgbi_sock_put(csk);
	}
}

static int do_act_establish(struct t3cdev *tdev, struct sk_buff *skb, void *ctx)
{
	struct cxgbi_sock *csk = ctx;
	struct cpl_act_establish *req = cplhdr(skb);
	unsigned int tid = GET_TID(req);
	unsigned int atid = G_PASS_OPEN_TID(ntohl(req->tos_tid));
	u32 rcv_isn = ntohl(req->rcv_isn);	/* real RCV_ISN + 1 */

	log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_SOCK,
		"atid 0x%x,tid 0x%x, csk 0x%p,%u,0x%lx, isn %u.\n",
		atid, atid, csk, csk->state, csk->flags, rcv_isn);

	cxgbi_sock_get(csk);
	cxgbi_sock_set_flag(csk, CTPF_HAS_TID);
	csk->tid = tid;
	cxgb3_insert_tid(csk->cdev->lldev, &t3_client, csk, tid);

	free_atid(csk);

	csk->rss_qid = G_QNUM(ntohs(skb->csum));

	spin_lock_bh(&csk->lock);
	if (csk->retry_timer.function) {
		del_timer(&csk->retry_timer);
		csk->retry_timer.function = NULL;
	}

	if (unlikely(csk->state != CTP_ACTIVE_OPEN))
		pr_info("csk 0x%p,%u,0x%lx,%u, got EST.\n",
			csk, csk->state, csk->flags, csk->tid);

	csk->copied_seq = csk->rcv_wup = csk->rcv_nxt = rcv_isn;
	if (cxgb3i_rcv_win > (M_RCV_BUFSIZ << 10))
		csk->rcv_wup -= cxgb3i_rcv_win - (M_RCV_BUFSIZ << 10);

	cxgbi_sock_established(csk, ntohl(req->snd_isn), ntohs(req->tcp_opt));

	if (unlikely(cxgbi_sock_flag(csk, CTPF_ACTIVE_CLOSE_NEEDED)))
		/* upper layer has requested closing */
		send_abort_req(csk);
	else {
		if (skb_queue_len(&csk->write_queue))
			push_tx_frames(csk, 1);
		cxgbi_conn_tx_open(csk);
	}

	spin_unlock_bh(&csk->lock);
	__kfree_skb(skb);
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
		return -EADDRINUSE;
	default:
		return -EIO;
	}
}

static void act_open_retry_timer(unsigned long data)
{
	struct sk_buff *skb;
	struct cxgbi_sock *csk = (struct cxgbi_sock *)data;

	log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_SOCK,
		"csk 0x%p,%u,0x%lx,%u.\n",
		csk, csk->state, csk->flags, csk->tid);

	cxgbi_sock_get(csk);
	spin_lock_bh(&csk->lock);
	skb = alloc_wr(sizeof(struct cpl_act_open_req), 0, GFP_ATOMIC);
	if (!skb)
		cxgbi_sock_fail_act_open(csk, -ENOMEM);
	else {
		skb->sk = (struct sock *)csk;
		set_arp_failure_handler(skb, act_open_arp_failure);
		send_act_open_req(csk, skb, csk->l2t);
	}
	spin_unlock_bh(&csk->lock);
	cxgbi_sock_put(csk);
}

static int do_act_open_rpl(struct t3cdev *tdev, struct sk_buff *skb, void *ctx)
{
	struct cxgbi_sock *csk = ctx;
	struct cpl_act_open_rpl *rpl = cplhdr(skb);

	pr_info("csk 0x%p,%u,0x%lx,%u, status %u, %pI4:%u-%pI4:%u.\n",
		csk, csk->state, csk->flags, csk->atid, rpl->status,
		&csk->saddr.sin_addr.s_addr, ntohs(csk->saddr.sin_port),
		&csk->daddr.sin_addr.s_addr, ntohs(csk->daddr.sin_port));

	if (rpl->status != CPL_ERR_TCAM_FULL &&
	    rpl->status != CPL_ERR_CONN_EXIST &&
	    rpl->status != CPL_ERR_ARP_MISS)
		cxgb3_queue_tid_release(tdev, GET_TID(rpl));

	cxgbi_sock_get(csk);
	spin_lock_bh(&csk->lock);
	if (rpl->status == CPL_ERR_CONN_EXIST &&
	    csk->retry_timer.function != act_open_retry_timer) {
		csk->retry_timer.function = act_open_retry_timer;
		mod_timer(&csk->retry_timer, jiffies + HZ / 2);
	} else
		cxgbi_sock_fail_act_open(csk,
				act_open_rpl_status_to_errno(rpl->status));

	spin_unlock_bh(&csk->lock);
	cxgbi_sock_put(csk);
	__kfree_skb(skb);
	return 0;
}

/*
 * Process PEER_CLOSE CPL messages: -> host
 * Handle peer FIN.
 */
static int do_peer_close(struct t3cdev *cdev, struct sk_buff *skb, void *ctx)
{
	struct cxgbi_sock *csk = ctx;

	log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_SOCK,
		"csk 0x%p,%u,0x%lx,%u.\n",
		csk, csk->state, csk->flags, csk->tid);

	cxgbi_sock_rcv_peer_close(csk);
	__kfree_skb(skb);
	return 0;
}

/*
 * Process CLOSE_CONN_RPL CPL message: -> host
 * Process a peer ACK to our FIN.
 */
static int do_close_con_rpl(struct t3cdev *cdev, struct sk_buff *skb,
			    void *ctx)
{
	struct cxgbi_sock *csk = ctx;
	struct cpl_close_con_rpl *rpl = cplhdr(skb);

	log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_SOCK,
		"csk 0x%p,%u,0x%lx,%u, snxt %u.\n",
		csk, csk->state, csk->flags, csk->tid, ntohl(rpl->snd_nxt));

	cxgbi_sock_rcv_close_conn_rpl(csk, ntohl(rpl->snd_nxt));
	__kfree_skb(skb);
	return 0;
}

/*
 * Process ABORT_REQ_RSS CPL message: -> host
 * Process abort requests.  If we are waiting for an ABORT_RPL we ignore this
 * request except that we need to reply to it.
 */

static int abort_status_to_errno(struct cxgbi_sock *csk, int abort_reason,
				 int *need_rst)
{
	switch (abort_reason) {
	case CPL_ERR_BAD_SYN: /* fall through */
	case CPL_ERR_CONN_RESET:
		return csk->state > CTP_ESTABLISHED ? -EPIPE : -ECONNRESET;
	case CPL_ERR_XMIT_TIMEDOUT:
	case CPL_ERR_PERSIST_TIMEDOUT:
	case CPL_ERR_FINWAIT2_TIMEDOUT:
	case CPL_ERR_KEEPALIVE_TIMEDOUT:
		return -ETIMEDOUT;
	default:
		return -EIO;
	}
}

static int do_abort_req(struct t3cdev *cdev, struct sk_buff *skb, void *ctx)
{
	const struct cpl_abort_req_rss *req = cplhdr(skb);
	struct cxgbi_sock *csk = ctx;
	int rst_status = CPL_ABORT_NO_RST;

	log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_SOCK,
		"csk 0x%p,%u,0x%lx,%u.\n",
		csk, csk->state, csk->flags, csk->tid);

	if (req->status == CPL_ERR_RTX_NEG_ADVICE ||
	    req->status == CPL_ERR_PERSIST_NEG_ADVICE) {
		goto done;
	}

	cxgbi_sock_get(csk);
	spin_lock_bh(&csk->lock);

	if (!cxgbi_sock_flag(csk, CTPF_ABORT_REQ_RCVD)) {
		cxgbi_sock_set_flag(csk, CTPF_ABORT_REQ_RCVD);
		cxgbi_sock_set_state(csk, CTP_ABORTING);
		goto out;
	}

	cxgbi_sock_clear_flag(csk, CTPF_ABORT_REQ_RCVD);
	send_abort_rpl(csk, rst_status);

	if (!cxgbi_sock_flag(csk, CTPF_ABORT_RPL_PENDING)) {
		csk->err = abort_status_to_errno(csk, req->status, &rst_status);
		cxgbi_sock_closed(csk);
	}

out:
	spin_unlock_bh(&csk->lock);
	cxgbi_sock_put(csk);
done:
	__kfree_skb(skb);
	return 0;
}

/*
 * Process ABORT_RPL_RSS CPL message: -> host
 * Process abort replies.  We only process these messages if we anticipate
 * them as the coordination between SW and HW in this area is somewhat lacking
 * and sometimes we get ABORT_RPLs after we are done with the connection that
 * originated the ABORT_REQ.
 */
static int do_abort_rpl(struct t3cdev *cdev, struct sk_buff *skb, void *ctx)
{
	struct cpl_abort_rpl_rss *rpl = cplhdr(skb);
	struct cxgbi_sock *csk = ctx;

	log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_SOCK,
		"status 0x%x, csk 0x%p, s %u, 0x%lx.\n",
		rpl->status, csk, csk ? csk->state : 0,
		csk ? csk->flags : 0UL);
	/*
	 * Ignore replies to post-close aborts indicating that the abort was
	 * requested too late.  These connections are terminated when we get
	 * PEER_CLOSE or CLOSE_CON_RPL and by the time the abort_rpl_rss
	 * arrives the TID is either no longer used or it has been recycled.
	 */
	if (rpl->status == CPL_ERR_ABORT_FAILED)
		goto rel_skb;
	/*
	 * Sometimes we've already closed the connection, e.g., a post-close
	 * abort races with ABORT_REQ_RSS, the latter frees the connection
	 * expecting the ABORT_REQ will fail with CPL_ERR_ABORT_FAILED,
	 * but FW turns the ABORT_REQ into a regular one and so we get
	 * ABORT_RPL_RSS with status 0 and no connection.
	 */
	if (csk)
		cxgbi_sock_rcv_abort_rpl(csk);
rel_skb:
	__kfree_skb(skb);
	return 0;
}

/*
 * Process RX_ISCSI_HDR CPL message: -> host
 * Handle received PDUs, the payload could be DDP'ed. If not, the payload
 * follow after the bhs.
 */
static int do_iscsi_hdr(struct t3cdev *t3dev, struct sk_buff *skb, void *ctx)
{
	struct cxgbi_sock *csk = ctx;
	struct cpl_iscsi_hdr *hdr_cpl = cplhdr(skb);
	struct cpl_iscsi_hdr_norss data_cpl;
	struct cpl_rx_data_ddp_norss ddp_cpl;
	unsigned int hdr_len, data_len, status;
	unsigned int len;
	int err;

	log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_PDU_RX,
		"csk 0x%p,%u,0x%lx,%u, skb 0x%p,%u.\n",
		csk, csk->state, csk->flags, csk->tid, skb, skb->len);

	spin_lock_bh(&csk->lock);

	if (unlikely(csk->state >= CTP_PASSIVE_CLOSE)) {
		log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_SOCK,
			"csk 0x%p,%u,0x%lx,%u, bad state.\n",
			csk, csk->state, csk->flags, csk->tid);
		if (csk->state != CTP_ABORTING)
			goto abort_conn;
		else
			goto discard;
	}

	cxgbi_skcb_tcp_seq(skb) = ntohl(hdr_cpl->seq);
	cxgbi_skcb_flags(skb) = 0;

	skb_reset_transport_header(skb);
	__skb_pull(skb, sizeof(struct cpl_iscsi_hdr));

	len = hdr_len = ntohs(hdr_cpl->len);
	/* msg coalesce is off or not enough data received */
	if (skb->len <= hdr_len) {
		pr_err("%s: tid %u, CPL_ISCSI_HDR, skb len %u < %u.\n",
			csk->cdev->ports[csk->port_id]->name, csk->tid,
			skb->len, hdr_len);
		goto abort_conn;
	}
	cxgbi_skcb_set_flag(skb, SKCBF_RX_COALESCED);

	err = skb_copy_bits(skb, skb->len - sizeof(ddp_cpl), &ddp_cpl,
			    sizeof(ddp_cpl));
	if (err < 0) {
		pr_err("%s: tid %u, copy cpl_ddp %u-%zu failed %d.\n",
			csk->cdev->ports[csk->port_id]->name, csk->tid,
			skb->len, sizeof(ddp_cpl), err);
		goto abort_conn;
	}

	cxgbi_skcb_set_flag(skb, SKCBF_RX_STATUS);
	cxgbi_skcb_rx_pdulen(skb) = ntohs(ddp_cpl.len);
	cxgbi_skcb_rx_ddigest(skb) = ntohl(ddp_cpl.ulp_crc);
	status = ntohl(ddp_cpl.ddp_status);

	log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_PDU_RX,
		"csk 0x%p, skb 0x%p,%u, pdulen %u, status 0x%x.\n",
		csk, skb, skb->len, cxgbi_skcb_rx_pdulen(skb), status);

	if (status & (1 << CPL_RX_DDP_STATUS_HCRC_SHIFT))
		cxgbi_skcb_set_flag(skb, SKCBF_RX_HCRC_ERR);
	if (status & (1 << CPL_RX_DDP_STATUS_DCRC_SHIFT))
		cxgbi_skcb_set_flag(skb, SKCBF_RX_DCRC_ERR);
	if (status & (1 << CPL_RX_DDP_STATUS_PAD_SHIFT))
		cxgbi_skcb_set_flag(skb, SKCBF_RX_PAD_ERR);

	if (skb->len > (hdr_len + sizeof(ddp_cpl))) {
		err = skb_copy_bits(skb, hdr_len, &data_cpl, sizeof(data_cpl));
		if (err < 0) {
			pr_err("%s: tid %u, cp %zu/%u failed %d.\n",
				csk->cdev->ports[csk->port_id]->name,
				csk->tid, sizeof(data_cpl), skb->len, err);
			goto abort_conn;
		}
		data_len = ntohs(data_cpl.len);
		log_debug(1 << CXGBI_DBG_DDP | 1 << CXGBI_DBG_PDU_RX,
			"skb 0x%p, pdu not ddp'ed %u/%u, status 0x%x.\n",
			skb, data_len, cxgbi_skcb_rx_pdulen(skb), status);
		len += sizeof(data_cpl) + data_len;
	} else if (status & (1 << CPL_RX_DDP_STATUS_DDP_SHIFT))
		cxgbi_skcb_set_flag(skb, SKCBF_RX_DATA_DDPD);

	csk->rcv_nxt = ntohl(ddp_cpl.seq) + cxgbi_skcb_rx_pdulen(skb);
	__pskb_trim(skb, len);
	__skb_queue_tail(&csk->receive_queue, skb);
	cxgbi_conn_pdu_ready(csk);

	spin_unlock_bh(&csk->lock);
	return 0;

abort_conn:
	send_abort_req(csk);
discard:
	spin_unlock_bh(&csk->lock);
	__kfree_skb(skb);
	return 0;
}

/*
 * Process TX_DATA_ACK CPL messages: -> host
 * Process an acknowledgment of WR completion.  Advance snd_una and send the
 * next batch of work requests from the write queue.
 */
static int do_wr_ack(struct t3cdev *cdev, struct sk_buff *skb, void *ctx)
{
	struct cxgbi_sock *csk = ctx;
	struct cpl_wr_ack *hdr = cplhdr(skb);

	log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_PDU_RX,
		"csk 0x%p,%u,0x%lx,%u, cr %u.\n",
		csk, csk->state, csk->flags, csk->tid, ntohs(hdr->credits));

	cxgbi_sock_rcv_wr_ack(csk, ntohs(hdr->credits), ntohl(hdr->snd_una), 1);
	__kfree_skb(skb);
	return 0;
}

/*
 * for each connection, pre-allocate skbs needed for close/abort requests. So
 * that we can service the request right away.
 */
static int alloc_cpls(struct cxgbi_sock *csk)
{
	csk->cpl_close = alloc_wr(sizeof(struct cpl_close_con_req), 0,
					GFP_KERNEL);
	if (!csk->cpl_close)
		return -ENOMEM;
	csk->cpl_abort_req = alloc_wr(sizeof(struct cpl_abort_req), 0,
					GFP_KERNEL);
	if (!csk->cpl_abort_req)
		goto free_cpl_skbs;

	csk->cpl_abort_rpl = alloc_wr(sizeof(struct cpl_abort_rpl), 0,
					GFP_KERNEL);
	if (!csk->cpl_abort_rpl)
		goto free_cpl_skbs;

	return 0;

free_cpl_skbs:
	cxgbi_sock_free_cpl_skbs(csk);
	return -ENOMEM;
}

/**
 * release_offload_resources - release offload resource
 * @c3cn: the offloaded iscsi tcp connection.
 * Release resources held by an offload connection (TID, L2T entry, etc.)
 */
static void l2t_put(struct cxgbi_sock *csk)
{
	struct t3cdev *t3dev = (struct t3cdev *)csk->cdev->lldev;

	if (csk->l2t) {
		l2t_release(t3dev, csk->l2t);
		csk->l2t = NULL;
		cxgbi_sock_put(csk);
	}
}

static void release_offload_resources(struct cxgbi_sock *csk)
{
	struct t3cdev *t3dev = (struct t3cdev *)csk->cdev->lldev;

	log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_SOCK,
		"csk 0x%p,%u,0x%lx,%u.\n",
		csk, csk->state, csk->flags, csk->tid);

	csk->rss_qid = 0;
	cxgbi_sock_free_cpl_skbs(csk);

	if (csk->wr_cred != csk->wr_max_cred) {
		cxgbi_sock_purge_wr_queue(csk);
		cxgbi_sock_reset_wr_list(csk);
	}
	l2t_put(csk);
	if (cxgbi_sock_flag(csk, CTPF_HAS_ATID))
		free_atid(csk);
	else if (cxgbi_sock_flag(csk, CTPF_HAS_TID)) {
		cxgb3_remove_tid(t3dev, (void *)csk, csk->tid);
		cxgbi_sock_clear_flag(csk, CTPF_HAS_TID);
		cxgbi_sock_put(csk);
	}
	csk->dst = NULL;
	csk->cdev = NULL;
}

static void update_address(struct cxgbi_hba *chba)
{
	if (chba->ipv4addr) {
		if (chba->vdev &&
		    chba->ipv4addr != cxgb3i_get_private_ipv4addr(chba->vdev)) {
			cxgb3i_set_private_ipv4addr(chba->vdev, chba->ipv4addr);
			cxgb3i_set_private_ipv4addr(chba->ndev, 0);
			pr_info("%s set %pI4.\n",
				chba->vdev->name, &chba->ipv4addr);
		} else if (chba->ipv4addr !=
				cxgb3i_get_private_ipv4addr(chba->ndev)) {
			cxgb3i_set_private_ipv4addr(chba->ndev, chba->ipv4addr);
			pr_info("%s set %pI4.\n",
				chba->ndev->name, &chba->ipv4addr);
		}
	} else if (cxgb3i_get_private_ipv4addr(chba->ndev)) {
		if (chba->vdev)
			cxgb3i_set_private_ipv4addr(chba->vdev, 0);
		cxgb3i_set_private_ipv4addr(chba->ndev, 0);
	}
}

static int init_act_open(struct cxgbi_sock *csk)
{
	struct dst_entry *dst = csk->dst;
	struct cxgbi_device *cdev = csk->cdev;
	struct t3cdev *t3dev = (struct t3cdev *)cdev->lldev;
	struct net_device *ndev = cdev->ports[csk->port_id];
	struct cxgbi_hba *chba = cdev->hbas[csk->port_id];
	struct sk_buff *skb = NULL;

	log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_SOCK,
		"csk 0x%p,%u,0x%lx.\n", csk, csk->state, csk->flags);

	update_address(chba);
	if (chba->ipv4addr)
		csk->saddr.sin_addr.s_addr = chba->ipv4addr;

	csk->rss_qid = 0;
	csk->l2t = t3_l2t_get(t3dev, dst_get_neighbour(dst), ndev);
	if (!csk->l2t) {
		pr_err("NO l2t available.\n");
		return -EINVAL;
	}
	cxgbi_sock_get(csk);

	csk->atid = cxgb3_alloc_atid(t3dev, &t3_client, csk);
	if (csk->atid < 0) {
		pr_err("NO atid available.\n");
		goto rel_resource;
	}
	cxgbi_sock_set_flag(csk, CTPF_HAS_ATID);
	cxgbi_sock_get(csk);

	skb = alloc_wr(sizeof(struct cpl_act_open_req), 0, GFP_KERNEL);
	if (!skb)
		goto rel_resource;
	skb->sk = (struct sock *)csk;
	set_arp_failure_handler(skb, act_open_arp_failure);

	csk->wr_max_cred = csk->wr_cred = T3C_DATA(t3dev)->max_wrs - 1;
	csk->wr_una_cred = 0;
	csk->mss_idx = cxgbi_sock_select_mss(csk, dst_mtu(dst));
	cxgbi_sock_reset_wr_list(csk);
	csk->err = 0;

	log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_SOCK,
		"csk 0x%p,%u,0x%lx, %pI4:%u-%pI4:%u.\n",
		csk, csk->state, csk->flags,
		&csk->saddr.sin_addr.s_addr, ntohs(csk->saddr.sin_port),
		&csk->daddr.sin_addr.s_addr, ntohs(csk->daddr.sin_port));

	cxgbi_sock_set_state(csk, CTP_ACTIVE_OPEN);
	send_act_open_req(csk, skb, csk->l2t);
	return 0;

rel_resource:
	if (skb)
		__kfree_skb(skb);
	return -EINVAL;
}

cxgb3_cpl_handler_func cxgb3i_cpl_handlers[NUM_CPL_CMDS] = {
	[CPL_ACT_ESTABLISH] = do_act_establish,
	[CPL_ACT_OPEN_RPL] = do_act_open_rpl,
	[CPL_PEER_CLOSE] = do_peer_close,
	[CPL_ABORT_REQ_RSS] = do_abort_req,
	[CPL_ABORT_RPL_RSS] = do_abort_rpl,
	[CPL_CLOSE_CON_RPL] = do_close_con_rpl,
	[CPL_TX_DMA_ACK] = do_wr_ack,
	[CPL_ISCSI_HDR] = do_iscsi_hdr,
};

/**
 * cxgb3i_ofld_init - allocate and initialize resources for each adapter found
 * @cdev:	cxgbi adapter
 */
int cxgb3i_ofld_init(struct cxgbi_device *cdev)
{
	struct t3cdev *t3dev = (struct t3cdev *)cdev->lldev;
	struct adap_ports port;
	struct ofld_page_info rx_page_info;
	unsigned int wr_len;
	int rc;

	if (t3dev->ctl(t3dev, GET_WR_LEN, &wr_len) < 0 ||
	    t3dev->ctl(t3dev, GET_PORTS, &port) < 0 ||
	    t3dev->ctl(t3dev, GET_RX_PAGE_INFO, &rx_page_info) < 0) {
		pr_warn("t3 0x%p, offload up, ioctl failed.\n", t3dev);
		return -EINVAL;
	}

	if (cxgb3i_max_connect > CXGBI_MAX_CONN)
		cxgb3i_max_connect = CXGBI_MAX_CONN;

	rc = cxgbi_device_portmap_create(cdev, cxgb3i_sport_base,
					cxgb3i_max_connect);
	if (rc < 0)
		return rc;

	init_wr_tab(wr_len);
	cdev->csk_release_offload_resources = release_offload_resources;
	cdev->csk_push_tx_frames = push_tx_frames;
	cdev->csk_send_abort_req = send_abort_req;
	cdev->csk_send_close_req = send_close_req;
	cdev->csk_send_rx_credits = send_rx_credits;
	cdev->csk_alloc_cpls = alloc_cpls;
	cdev->csk_init_act_open = init_act_open;

	pr_info("cdev 0x%p, offload up, added.\n", cdev);
	return 0;
}

/*
 * functions to program the pagepod in h/w
 */
static inline void ulp_mem_io_set_hdr(struct sk_buff *skb, unsigned int addr)
{
	struct ulp_mem_io *req = (struct ulp_mem_io *)skb->head;

	memset(req, 0, sizeof(*req));

	req->wr.wr_hi = htonl(V_WR_OP(FW_WROPCODE_BYPASS));
	req->cmd_lock_addr = htonl(V_ULP_MEMIO_ADDR(addr >> 5) |
				   V_ULPTX_CMD(ULP_MEM_WRITE));
	req->len = htonl(V_ULP_MEMIO_DATA_LEN(PPOD_SIZE >> 5) |
			 V_ULPTX_NFLITS((PPOD_SIZE >> 3) + 1));
}

static int ddp_set_map(struct cxgbi_sock *csk, struct cxgbi_pagepod_hdr *hdr,
			unsigned int idx, unsigned int npods,
				struct cxgbi_gather_list *gl)
{
	struct cxgbi_device *cdev = csk->cdev;
	struct cxgbi_ddp_info *ddp = cdev->ddp;
	unsigned int pm_addr = (idx << PPOD_SIZE_SHIFT) + ddp->llimit;
	int i;

	log_debug(1 << CXGBI_DBG_DDP,
		"csk 0x%p, idx %u, npods %u, gl 0x%p.\n",
		csk, idx, npods, gl);

	for (i = 0; i < npods; i++, idx++, pm_addr += PPOD_SIZE) {
		struct sk_buff *skb = alloc_wr(sizeof(struct ulp_mem_io) +
						PPOD_SIZE, 0, GFP_ATOMIC);

		if (!skb)
			return -ENOMEM;

		ulp_mem_io_set_hdr(skb, pm_addr);
		cxgbi_ddp_ppod_set((struct cxgbi_pagepod *)(skb->head +
					sizeof(struct ulp_mem_io)),
				   hdr, gl, i * PPOD_PAGES_MAX);
		skb->priority = CPL_PRIORITY_CONTROL;
		cxgb3_ofld_send(cdev->lldev, skb);
	}
	return 0;
}

static void ddp_clear_map(struct cxgbi_hba *chba, unsigned int tag,
			  unsigned int idx, unsigned int npods)
{
	struct cxgbi_device *cdev = chba->cdev;
	struct cxgbi_ddp_info *ddp = cdev->ddp;
	unsigned int pm_addr = (idx << PPOD_SIZE_SHIFT) + ddp->llimit;
	int i;

	log_debug(1 << CXGBI_DBG_DDP,
		"cdev 0x%p, idx %u, npods %u, tag 0x%x.\n",
		cdev, idx, npods, tag);

	for (i = 0; i < npods; i++, idx++, pm_addr += PPOD_SIZE) {
		struct sk_buff *skb = alloc_wr(sizeof(struct ulp_mem_io) +
						PPOD_SIZE, 0, GFP_ATOMIC);

		if (!skb) {
			pr_err("tag 0x%x, 0x%x, %d/%u, skb OOM.\n",
				tag, idx, i, npods);
			continue;
		}
		ulp_mem_io_set_hdr(skb, pm_addr);
		skb->priority = CPL_PRIORITY_CONTROL;
		cxgb3_ofld_send(cdev->lldev, skb);
	}
}

static int ddp_setup_conn_pgidx(struct cxgbi_sock *csk,
				       unsigned int tid, int pg_idx, bool reply)
{
	struct sk_buff *skb = alloc_wr(sizeof(struct cpl_set_tcb_field), 0,
					GFP_KERNEL);
	struct cpl_set_tcb_field *req;
	u64 val = pg_idx < DDP_PGIDX_MAX ? pg_idx : 0;

	log_debug(1 << CXGBI_DBG_DDP,
		"csk 0x%p, tid %u, pg_idx %d.\n", csk, tid, pg_idx);
	if (!skb)
		return -ENOMEM;

	/* set up ulp submode and page size */
	req = (struct cpl_set_tcb_field *)skb->head;
	req->wr.wr_hi = htonl(V_WR_OP(FW_WROPCODE_FORWARD));
	OPCODE_TID(req) = htonl(MK_OPCODE_TID(CPL_SET_TCB_FIELD, tid));
	req->reply = V_NO_REPLY(reply ? 0 : 1);
	req->cpu_idx = 0;
	req->word = htons(31);
	req->mask = cpu_to_be64(0xF0000000);
	req->val = cpu_to_be64(val << 28);
	skb->priority = CPL_PRIORITY_CONTROL;

	cxgb3_ofld_send(csk->cdev->lldev, skb);
	return 0;
}

/**
 * cxgb3i_setup_conn_digest - setup conn. digest setting
 * @csk: cxgb tcp socket
 * @tid: connection id
 * @hcrc: header digest enabled
 * @dcrc: data digest enabled
 * @reply: request reply from h/w
 * set up the iscsi digest settings for a connection identified by tid
 */
static int ddp_setup_conn_digest(struct cxgbi_sock *csk, unsigned int tid,
			     int hcrc, int dcrc, int reply)
{
	struct sk_buff *skb = alloc_wr(sizeof(struct cpl_set_tcb_field), 0,
					GFP_KERNEL);
	struct cpl_set_tcb_field *req;
	u64 val = (hcrc ? 1 : 0) | (dcrc ? 2 : 0);

	log_debug(1 << CXGBI_DBG_DDP,
		"csk 0x%p, tid %u, crc %d,%d.\n", csk, tid, hcrc, dcrc);
	if (!skb)
		return -ENOMEM;

	/* set up ulp submode and page size */
	req = (struct cpl_set_tcb_field *)skb->head;
	req->wr.wr_hi = htonl(V_WR_OP(FW_WROPCODE_FORWARD));
	OPCODE_TID(req) = htonl(MK_OPCODE_TID(CPL_SET_TCB_FIELD, tid));
	req->reply = V_NO_REPLY(reply ? 0 : 1);
	req->cpu_idx = 0;
	req->word = htons(31);
	req->mask = cpu_to_be64(0x0F000000);
	req->val = cpu_to_be64(val << 24);
	skb->priority = CPL_PRIORITY_CONTROL;

	cxgb3_ofld_send(csk->cdev->lldev, skb);
	return 0;
}

/**
 * t3_ddp_cleanup - release the cxgb3 adapter's ddp resource
 * @cdev: cxgb3i adapter
 * release all the resource held by the ddp pagepod manager for a given
 * adapter if needed
 */

static void t3_ddp_cleanup(struct cxgbi_device *cdev)
{
	struct t3cdev *tdev = (struct t3cdev *)cdev->lldev;

	if (cxgbi_ddp_cleanup(cdev)) {
		pr_info("t3dev 0x%p, ulp_iscsi no more user.\n", tdev);
		tdev->ulp_iscsi = NULL;
	}
}

/**
 * ddp_init - initialize the cxgb3 adapter's ddp resource
 * @cdev: cxgb3i adapter
 * initialize the ddp pagepod manager for a given adapter
 */
static int cxgb3i_ddp_init(struct cxgbi_device *cdev)
{
	struct t3cdev *tdev = (struct t3cdev *)cdev->lldev;
	struct cxgbi_ddp_info *ddp = tdev->ulp_iscsi;
	struct ulp_iscsi_info uinfo;
	unsigned int pgsz_factor[4];
	int i, err;

	if (ddp) {
		kref_get(&ddp->refcnt);
		pr_warn("t3dev 0x%p, ddp 0x%p already set up.\n",
			tdev, tdev->ulp_iscsi);
		cdev->ddp = ddp;
		return -EALREADY;
	}

	err = tdev->ctl(tdev, ULP_ISCSI_GET_PARAMS, &uinfo);
	if (err < 0) {
		pr_err("%s, failed to get iscsi param err=%d.\n",
			 tdev->name, err);
		return err;
	}

	err = cxgbi_ddp_init(cdev, uinfo.llimit, uinfo.ulimit,
			uinfo.max_txsz, uinfo.max_rxsz);
	if (err < 0)
		return err;

	ddp = cdev->ddp;

	uinfo.tagmask = ddp->idx_mask << PPOD_IDX_SHIFT;
	cxgbi_ddp_page_size_factor(pgsz_factor);
	for (i = 0; i < 4; i++)
		uinfo.pgsz_factor[i] = pgsz_factor[i];
	uinfo.ulimit = uinfo.llimit + (ddp->nppods << PPOD_SIZE_SHIFT);

	err = tdev->ctl(tdev, ULP_ISCSI_SET_PARAMS, &uinfo);
	if (err < 0) {
		pr_warn("%s unable to set iscsi param err=%d, ddp disabled.\n",
			tdev->name, err);
		cxgbi_ddp_cleanup(cdev);
		return err;
	}
	tdev->ulp_iscsi = ddp;

	cdev->csk_ddp_setup_digest = ddp_setup_conn_digest;
	cdev->csk_ddp_setup_pgidx = ddp_setup_conn_pgidx;
	cdev->csk_ddp_set = ddp_set_map;
	cdev->csk_ddp_clear = ddp_clear_map;

	pr_info("tdev 0x%p, nppods %u, bits %u, mask 0x%x,0x%x pkt %u/%u, "
		"%u/%u.\n",
		tdev, ddp->nppods, ddp->idx_bits, ddp->idx_mask,
		ddp->rsvd_tag_mask, ddp->max_txsz, uinfo.max_txsz,
		ddp->max_rxsz, uinfo.max_rxsz);
	return 0;
}

static void cxgb3i_dev_close(struct t3cdev *t3dev)
{
	struct cxgbi_device *cdev = cxgbi_device_find_by_lldev(t3dev);

	if (!cdev || cdev->flags & CXGBI_FLAG_ADAPTER_RESET) {
		pr_info("0x%p close, f 0x%x.\n", cdev, cdev ? cdev->flags : 0);
		return;
	}

	cxgbi_device_unregister(cdev);
}

/**
 * cxgb3i_dev_open - init a t3 adapter structure and any h/w settings
 * @t3dev: t3cdev adapter
 */
static void cxgb3i_dev_open(struct t3cdev *t3dev)
{
	struct cxgbi_device *cdev = cxgbi_device_find_by_lldev(t3dev);
	struct adapter *adapter = tdev2adap(t3dev);
	int i, err;

	if (cdev) {
		pr_info("0x%p, updating.\n", cdev);
		return;
	}

	cdev = cxgbi_device_register(0, adapter->params.nports);
	if (!cdev) {
		pr_warn("device 0x%p register failed.\n", t3dev);
		return;
	}

	cdev->flags = CXGBI_FLAG_DEV_T3 | CXGBI_FLAG_IPV4_SET;
	cdev->lldev = t3dev;
	cdev->pdev = adapter->pdev;
	cdev->ports = adapter->port;
	cdev->nports = adapter->params.nports;
	cdev->mtus = adapter->params.mtus;
	cdev->nmtus = NMTUS;
	cdev->snd_win = cxgb3i_snd_win;
	cdev->rcv_win = cxgb3i_rcv_win;
	cdev->rx_credit_thres = cxgb3i_rx_credit_thres;
	cdev->skb_tx_rsvd = CXGB3I_TX_HEADER_LEN;
	cdev->skb_rx_extra = sizeof(struct cpl_iscsi_hdr_norss);
	cdev->dev_ddp_cleanup = t3_ddp_cleanup;
	cdev->itp = &cxgb3i_iscsi_transport;

	err = cxgb3i_ddp_init(cdev);
	if (err) {
		pr_info("0x%p ddp init failed\n", cdev);
		goto err_out;
	}

	err = cxgb3i_ofld_init(cdev);
	if (err) {
		pr_info("0x%p offload init failed\n", cdev);
		goto err_out;
	}

	err = cxgbi_hbas_add(cdev, CXGB3I_MAX_LUN, CXGBI_MAX_CONN,
				&cxgb3i_host_template, cxgb3i_stt);
	if (err)
		goto err_out;

	for (i = 0; i < cdev->nports; i++)
		cdev->hbas[i]->ipv4addr =
			cxgb3i_get_private_ipv4addr(cdev->ports[i]);

	pr_info("cdev 0x%p, f 0x%x, t3dev 0x%p open, err %d.\n",
		cdev, cdev ? cdev->flags : 0, t3dev, err);
	return;

err_out:
	cxgbi_device_unregister(cdev);
}

static void cxgb3i_dev_event_handler(struct t3cdev *t3dev, u32 event, u32 port)
{
	struct cxgbi_device *cdev = cxgbi_device_find_by_lldev(t3dev);

	log_debug(1 << CXGBI_DBG_TOE,
		"0x%p, cdev 0x%p, event 0x%x, port 0x%x.\n",
		t3dev, cdev, event, port);
	if (!cdev)
		return;

	switch (event) {
	case OFFLOAD_STATUS_DOWN:
		cdev->flags |= CXGBI_FLAG_ADAPTER_RESET;
		break;
	case OFFLOAD_STATUS_UP:
		cdev->flags &= ~CXGBI_FLAG_ADAPTER_RESET;
		break;
	}
}

/**
 * cxgb3i_init_module - module init entry point
 *
 * initialize any driver wide global data structures and register itself
 *	with the cxgb3 module
 */
static int __init cxgb3i_init_module(void)
{
	int rc;

	printk(KERN_INFO "%s", version);

	rc = cxgbi_iscsi_init(&cxgb3i_iscsi_transport, &cxgb3i_stt);
	if (rc < 0)
		return rc;

	cxgb3_register_client(&t3_client);
	return 0;
}

/**
 * cxgb3i_exit_module - module cleanup/exit entry point
 *
 * go through the driver hba list and for each hba, release any resource held.
 *	and unregisters iscsi transport and the cxgb3 module
 */
static void __exit cxgb3i_exit_module(void)
{
	cxgb3_unregister_client(&t3_client);
	cxgbi_device_unregister_all(CXGBI_FLAG_DEV_T3);
	cxgbi_iscsi_cleanup(&cxgb3i_iscsi_transport, &cxgb3i_stt);
}

module_init(cxgb3i_init_module);
module_exit(cxgb3i_exit_module);
