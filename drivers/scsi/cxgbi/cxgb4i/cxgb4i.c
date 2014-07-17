/*
 * cxgb4i.c: Chelsio T4 iSCSI driver.
 *
 * Copyright (c) 2010 Chelsio Communications, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * Written by:	Karen Xie (kxie@chelsio.com)
 *		Rakesh Ranjan (rranjan@chelsio.com)
 */

#define pr_fmt(fmt) KBUILD_MODNAME ":%s: " fmt, __func__

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <scsi/scsi_host.h>
#include <net/tcp.h>
#include <net/dst.h>
#include <linux/netdevice.h>
#include <net/addrconf.h>

#include "t4_regs.h"
#include "t4_msg.h"
#include "cxgb4.h"
#include "cxgb4_uld.h"
#include "t4fw_api.h"
#include "l2t.h"
#include "cxgb4i.h"

static unsigned int dbg_level;

#include "../libcxgbi.h"

#define	DRV_MODULE_NAME		"cxgb4i"
#define DRV_MODULE_DESC		"Chelsio T4/T5 iSCSI Driver"
#define	DRV_MODULE_VERSION	"0.9.4"

static char version[] =
	DRV_MODULE_DESC " " DRV_MODULE_NAME
	" v" DRV_MODULE_VERSION "\n";

MODULE_AUTHOR("Chelsio Communications, Inc.");
MODULE_DESCRIPTION(DRV_MODULE_DESC);
MODULE_VERSION(DRV_MODULE_VERSION);
MODULE_LICENSE("GPL");

module_param(dbg_level, uint, 0644);
MODULE_PARM_DESC(dbg_level, "Debug flag (default=0)");

static int cxgb4i_rcv_win = 256 * 1024;
module_param(cxgb4i_rcv_win, int, 0644);
MODULE_PARM_DESC(cxgb4i_rcv_win, "TCP reveive window in bytes");

static int cxgb4i_snd_win = 128 * 1024;
module_param(cxgb4i_snd_win, int, 0644);
MODULE_PARM_DESC(cxgb4i_snd_win, "TCP send window in bytes");

static int cxgb4i_rx_credit_thres = 10 * 1024;
module_param(cxgb4i_rx_credit_thres, int, 0644);
MODULE_PARM_DESC(cxgb4i_rx_credit_thres,
		"RX credits return threshold in bytes (default=10KB)");

static unsigned int cxgb4i_max_connect = (8 * 1024);
module_param(cxgb4i_max_connect, uint, 0644);
MODULE_PARM_DESC(cxgb4i_max_connect, "Maximum number of connections");

static unsigned short cxgb4i_sport_base = 20000;
module_param(cxgb4i_sport_base, ushort, 0644);
MODULE_PARM_DESC(cxgb4i_sport_base, "Starting port number (default 20000)");

typedef void (*cxgb4i_cplhandler_func)(struct cxgbi_device *, struct sk_buff *);

static void *t4_uld_add(const struct cxgb4_lld_info *);
static int t4_uld_rx_handler(void *, const __be64 *, const struct pkt_gl *);
static int t4_uld_state_change(void *, enum cxgb4_state state);

static const struct cxgb4_uld_info cxgb4i_uld_info = {
	.name = DRV_MODULE_NAME,
	.add = t4_uld_add,
	.rx_handler = t4_uld_rx_handler,
	.state_change = t4_uld_state_change,
};

static struct scsi_host_template cxgb4i_host_template = {
	.module		= THIS_MODULE,
	.name		= DRV_MODULE_NAME,
	.proc_name	= DRV_MODULE_NAME,
	.can_queue	= CXGB4I_SCSI_HOST_QDEPTH,
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

static struct iscsi_transport cxgb4i_iscsi_transport = {
	.owner		= THIS_MODULE,
	.name		= DRV_MODULE_NAME,
	.caps		= CAP_RECOVERY_L0 | CAP_MULTI_R2T | CAP_HDRDGST |
				CAP_DATADGST | CAP_DIGEST_OFFLOAD |
				CAP_PADDING_OFFLOAD | CAP_TEXT_NEGO,
	.attr_is_visible	= cxgbi_attr_is_visible,
	.get_host_param	= cxgbi_get_host_param,
	.set_host_param	= cxgbi_set_host_param,
	/* session management */
	.create_session	= cxgbi_create_session,
	.destroy_session	= cxgbi_destroy_session,
	.get_session_param = iscsi_session_get_param,
	/* connection management */
	.create_conn	= cxgbi_create_conn,
	.bind_conn		= cxgbi_bind_conn,
	.destroy_conn	= iscsi_tcp_conn_teardown,
	.start_conn		= iscsi_conn_start,
	.stop_conn		= iscsi_conn_stop,
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

static struct scsi_transport_template *cxgb4i_stt;

/*
 * CPL (Chelsio Protocol Language) defines a message passing interface between
 * the host driver and Chelsio asic.
 * The section below implments CPLs that related to iscsi tcp connection
 * open/close/abort and data send/receive.
 */

#define DIV_ROUND_UP(n, d)	(((n) + (d) - 1) / (d))
#define RCV_BUFSIZ_MASK		0x3FFU
#define MAX_IMM_TX_PKT_LEN	128

static inline void set_queue(struct sk_buff *skb, unsigned int queue,
				const struct cxgbi_sock *csk)
{
	skb->queue_mapping = queue;
}

static int push_tx_frames(struct cxgbi_sock *, int);

/*
 * is_ofld_imm - check whether a packet can be sent as immediate data
 * @skb: the packet
 *
 * Returns true if a packet can be sent as an offload WR with immediate
 * data.  We currently use the same limit as for Ethernet packets.
 */
static inline int is_ofld_imm(const struct sk_buff *skb)
{
	return skb->len <= (MAX_IMM_TX_PKT_LEN -
			sizeof(struct fw_ofld_tx_data_wr));
}

static void send_act_open_req(struct cxgbi_sock *csk, struct sk_buff *skb,
				struct l2t_entry *e)
{
	struct cxgb4_lld_info *lldi = cxgbi_cdev_priv(csk->cdev);
	int t4 = is_t4(lldi->adapter_type);
	int wscale = cxgbi_sock_compute_wscale(csk->mss_idx);
	unsigned long long opt0;
	unsigned int opt2;
	unsigned int qid_atid = ((unsigned int)csk->atid) |
				 (((unsigned int)csk->rss_qid) << 14);

	opt0 = KEEP_ALIVE(1) |
		WND_SCALE(wscale) |
		MSS_IDX(csk->mss_idx) |
		L2T_IDX(((struct l2t_entry *)csk->l2t)->idx) |
		TX_CHAN(csk->tx_chan) |
		SMAC_SEL(csk->smac_idx) |
		ULP_MODE(ULP_MODE_ISCSI) |
		RCV_BUFSIZ(cxgb4i_rcv_win >> 10);
	opt2 = RX_CHANNEL(0) |
		RSS_QUEUE_VALID |
		(1 << 20) |
		RSS_QUEUE(csk->rss_qid);

	if (is_t4(lldi->adapter_type)) {
		struct cpl_act_open_req *req =
				(struct cpl_act_open_req *)skb->head;

		INIT_TP_WR(req, 0);
		OPCODE_TID(req) = cpu_to_be32(MK_OPCODE_TID(CPL_ACT_OPEN_REQ,
					qid_atid));
		req->local_port = csk->saddr.sin_port;
		req->peer_port = csk->daddr.sin_port;
		req->local_ip = csk->saddr.sin_addr.s_addr;
		req->peer_ip = csk->daddr.sin_addr.s_addr;
		req->opt0 = cpu_to_be64(opt0);
		req->params = cpu_to_be32(cxgb4_select_ntuple(
					csk->cdev->ports[csk->port_id],
					csk->l2t));
		opt2 |= 1 << 22;
		req->opt2 = cpu_to_be32(opt2);

		log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_SOCK,
			"csk t4 0x%p, %pI4:%u-%pI4:%u, atid %d, qid %u.\n",
			csk, &req->local_ip, ntohs(req->local_port),
			&req->peer_ip, ntohs(req->peer_port),
			csk->atid, csk->rss_qid);
	} else {
		struct cpl_t5_act_open_req *req =
				(struct cpl_t5_act_open_req *)skb->head;

		INIT_TP_WR(req, 0);
		OPCODE_TID(req) = cpu_to_be32(MK_OPCODE_TID(CPL_ACT_OPEN_REQ,
					qid_atid));
		req->local_port = csk->saddr.sin_port;
		req->peer_port = csk->daddr.sin_port;
		req->local_ip = csk->saddr.sin_addr.s_addr;
		req->peer_ip = csk->daddr.sin_addr.s_addr;
		req->opt0 = cpu_to_be64(opt0);
		req->params = cpu_to_be64(V_FILTER_TUPLE(
				cxgb4_select_ntuple(
					csk->cdev->ports[csk->port_id],
					csk->l2t)));
		opt2 |= 1 << 31;
		req->opt2 = cpu_to_be32(opt2);

		log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_SOCK,
			"csk t5 0x%p, %pI4:%u-%pI4:%u, atid %d, qid %u.\n",
			csk, &req->local_ip, ntohs(req->local_port),
			&req->peer_ip, ntohs(req->peer_port),
			csk->atid, csk->rss_qid);
	}

	set_wr_txq(skb, CPL_PRIORITY_SETUP, csk->port_id);

	pr_info_ipaddr("t%d csk 0x%p,%u,0x%lx,%u, rss_qid %u.\n",
		       (&csk->saddr), (&csk->daddr), t4 ? 4 : 5, csk,
		       csk->state, csk->flags, csk->atid, csk->rss_qid);

	cxgb4_l2t_send(csk->cdev->ports[csk->port_id], skb, csk->l2t);
}

static void send_act_open_req6(struct cxgbi_sock *csk, struct sk_buff *skb,
			       struct l2t_entry *e)
{
	struct cxgb4_lld_info *lldi = cxgbi_cdev_priv(csk->cdev);
	int t4 = is_t4(lldi->adapter_type);
	int wscale = cxgbi_sock_compute_wscale(csk->mss_idx);
	unsigned long long opt0;
	unsigned int opt2;
	unsigned int qid_atid = ((unsigned int)csk->atid) |
				 (((unsigned int)csk->rss_qid) << 14);

	opt0 = KEEP_ALIVE(1) |
		WND_SCALE(wscale) |
		MSS_IDX(csk->mss_idx) |
		L2T_IDX(((struct l2t_entry *)csk->l2t)->idx) |
		TX_CHAN(csk->tx_chan) |
		SMAC_SEL(csk->smac_idx) |
		ULP_MODE(ULP_MODE_ISCSI) |
		RCV_BUFSIZ(cxgb4i_rcv_win >> 10);

	opt2 = RX_CHANNEL(0) |
		RSS_QUEUE_VALID |
		RX_FC_DISABLE |
		RSS_QUEUE(csk->rss_qid);

	if (t4) {
		struct cpl_act_open_req6 *req =
			    (struct cpl_act_open_req6 *)skb->head;

		INIT_TP_WR(req, 0);
		OPCODE_TID(req) = cpu_to_be32(MK_OPCODE_TID(CPL_ACT_OPEN_REQ6,
							    qid_atid));
		req->local_port = csk->saddr6.sin6_port;
		req->peer_port = csk->daddr6.sin6_port;

		req->local_ip_hi = *(__be64 *)(csk->saddr6.sin6_addr.s6_addr);
		req->local_ip_lo = *(__be64 *)(csk->saddr6.sin6_addr.s6_addr +
								    8);
		req->peer_ip_hi = *(__be64 *)(csk->daddr6.sin6_addr.s6_addr);
		req->peer_ip_lo = *(__be64 *)(csk->daddr6.sin6_addr.s6_addr +
								    8);

		req->opt0 = cpu_to_be64(opt0);

		opt2 |= RX_FC_VALID;
		req->opt2 = cpu_to_be32(opt2);

		req->params = cpu_to_be32(cxgb4_select_ntuple(
					  csk->cdev->ports[csk->port_id],
					  csk->l2t));
	} else {
		struct cpl_t5_act_open_req6 *req =
				(struct cpl_t5_act_open_req6 *)skb->head;

		INIT_TP_WR(req, 0);
		OPCODE_TID(req) = cpu_to_be32(MK_OPCODE_TID(CPL_ACT_OPEN_REQ6,
							    qid_atid));
		req->local_port = csk->saddr6.sin6_port;
		req->peer_port = csk->daddr6.sin6_port;
		req->local_ip_hi = *(__be64 *)(csk->saddr6.sin6_addr.s6_addr);
		req->local_ip_lo = *(__be64 *)(csk->saddr6.sin6_addr.s6_addr +
									8);
		req->peer_ip_hi = *(__be64 *)(csk->daddr6.sin6_addr.s6_addr);
		req->peer_ip_lo = *(__be64 *)(csk->daddr6.sin6_addr.s6_addr +
									8);
		req->opt0 = cpu_to_be64(opt0);

		opt2 |= T5_OPT_2_VALID;
		req->opt2 = cpu_to_be32(opt2);

		req->params = cpu_to_be64(V_FILTER_TUPLE(cxgb4_select_ntuple(
					  csk->cdev->ports[csk->port_id],
					  csk->l2t)));
	}

	set_wr_txq(skb, CPL_PRIORITY_SETUP, csk->port_id);

	pr_info("t%d csk 0x%p,%u,0x%lx,%u, [%pI6]:%u-[%pI6]:%u, rss_qid %u.\n",
		t4 ? 4 : 5, csk, csk->state, csk->flags, csk->atid,
		&csk->saddr6.sin6_addr, ntohs(csk->saddr.sin_port),
		&csk->daddr6.sin6_addr, ntohs(csk->daddr.sin_port),
		csk->rss_qid);

	cxgb4_l2t_send(csk->cdev->ports[csk->port_id], skb, csk->l2t);
}

static void send_close_req(struct cxgbi_sock *csk)
{
	struct sk_buff *skb = csk->cpl_close;
	struct cpl_close_con_req *req = (struct cpl_close_con_req *)skb->head;
	unsigned int tid = csk->tid;

	log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_SOCK,
		"csk 0x%p,%u,0x%lx, tid %u.\n",
		csk, csk->state, csk->flags, csk->tid);
	csk->cpl_close = NULL;
	set_wr_txq(skb, CPL_PRIORITY_DATA, csk->port_id);
	INIT_TP_WR(req, tid);
	OPCODE_TID(req) = cpu_to_be32(MK_OPCODE_TID(CPL_CLOSE_CON_REQ, tid));
	req->rsvd = 0;

	cxgbi_sock_skb_entail(csk, skb);
	if (csk->state >= CTP_ESTABLISHED)
		push_tx_frames(csk, 1);
}

static void abort_arp_failure(void *handle, struct sk_buff *skb)
{
	struct cxgbi_sock *csk = (struct cxgbi_sock *)handle;
	struct cpl_abort_req *req;

	log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_SOCK,
		"csk 0x%p,%u,0x%lx, tid %u, abort.\n",
		csk, csk->state, csk->flags, csk->tid);
	req = (struct cpl_abort_req *)skb->data;
	req->cmd = CPL_ABORT_NO_RST;
	cxgb4_ofld_send(csk->cdev->ports[csk->port_id], skb);
}

static void send_abort_req(struct cxgbi_sock *csk)
{
	struct cpl_abort_req *req;
	struct sk_buff *skb = csk->cpl_abort_req;

	if (unlikely(csk->state == CTP_ABORTING) || !skb || !csk->cdev)
		return;
	cxgbi_sock_set_state(csk, CTP_ABORTING);
	cxgbi_sock_set_flag(csk, CTPF_ABORT_RPL_PENDING);
	cxgbi_sock_purge_write_queue(csk);

	csk->cpl_abort_req = NULL;
	req = (struct cpl_abort_req *)skb->head;
	set_queue(skb, CPL_PRIORITY_DATA, csk);
	req->cmd = CPL_ABORT_SEND_RST;
	t4_set_arp_err_handler(skb, csk, abort_arp_failure);
	INIT_TP_WR(req, csk->tid);
	OPCODE_TID(req) = cpu_to_be32(MK_OPCODE_TID(CPL_ABORT_REQ, csk->tid));
	req->rsvd0 = htonl(csk->snd_nxt);
	req->rsvd1 = !cxgbi_sock_flag(csk, CTPF_TX_DATA_SENT);

	log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_SOCK,
		"csk 0x%p,%u,0x%lx,%u, snd_nxt %u, 0x%x.\n",
		csk, csk->state, csk->flags, csk->tid, csk->snd_nxt,
		req->rsvd1);

	cxgb4_l2t_send(csk->cdev->ports[csk->port_id], skb, csk->l2t);
}

static void send_abort_rpl(struct cxgbi_sock *csk, int rst_status)
{
	struct sk_buff *skb = csk->cpl_abort_rpl;
	struct cpl_abort_rpl *rpl = (struct cpl_abort_rpl *)skb->head;

	log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_SOCK,
		"csk 0x%p,%u,0x%lx,%u, status %d.\n",
		csk, csk->state, csk->flags, csk->tid, rst_status);

	csk->cpl_abort_rpl = NULL;
	set_queue(skb, CPL_PRIORITY_DATA, csk);
	INIT_TP_WR(rpl, csk->tid);
	OPCODE_TID(rpl) = cpu_to_be32(MK_OPCODE_TID(CPL_ABORT_RPL, csk->tid));
	rpl->cmd = rst_status;
	cxgb4_ofld_send(csk->cdev->ports[csk->port_id], skb);
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

	log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_PDU_RX,
		"csk 0x%p,%u,0x%lx,%u, credit %u.\n",
		csk, csk->state, csk->flags, csk->tid, credits);

	skb = alloc_wr(sizeof(*req), 0, GFP_ATOMIC);
	if (!skb) {
		pr_info("csk 0x%p, credit %u, OOM.\n", csk, credits);
		return 0;
	}
	req = (struct cpl_rx_data_ack *)skb->head;

	set_wr_txq(skb, CPL_PRIORITY_ACK, csk->port_id);
	INIT_TP_WR(req, csk->tid);
	OPCODE_TID(req) = cpu_to_be32(MK_OPCODE_TID(CPL_RX_DATA_ACK,
				      csk->tid));
	req->credit_dack = cpu_to_be32(RX_CREDITS(credits) | RX_FORCE_ACK(1));
	cxgb4_ofld_send(csk->cdev->ports[csk->port_id], skb);
	return credits;
}

/*
 * sgl_len - calculates the size of an SGL of the given capacity
 * @n: the number of SGL entries
 * Calculates the number of flits needed for a scatter/gather list that
 * can hold the given number of entries.
 */
static inline unsigned int sgl_len(unsigned int n)
{
	n--;
	return (3 * n) / 2 + (n & 1) + 2;
}

/*
 * calc_tx_flits_ofld - calculate # of flits for an offload packet
 * @skb: the packet
 *
 * Returns the number of flits needed for the given offload packet.
 * These packets are already fully constructed and no additional headers
 * will be added.
 */
static inline unsigned int calc_tx_flits_ofld(const struct sk_buff *skb)
{
	unsigned int flits, cnt;

	if (is_ofld_imm(skb))
		return DIV_ROUND_UP(skb->len, 8);
	flits = skb_transport_offset(skb) / 8;
	cnt = skb_shinfo(skb)->nr_frags;
	if (skb_tail_pointer(skb) != skb_transport_header(skb))
		cnt++;
	return flits + sgl_len(cnt);
}

static inline void send_tx_flowc_wr(struct cxgbi_sock *csk)
{
	struct sk_buff *skb;
	struct fw_flowc_wr *flowc;
	int flowclen, i;

	flowclen = 80;
	skb = alloc_wr(flowclen, 0, GFP_ATOMIC);
	flowc = (struct fw_flowc_wr *)skb->head;
	flowc->op_to_nparams =
		htonl(FW_WR_OP(FW_FLOWC_WR) | FW_FLOWC_WR_NPARAMS(8));
	flowc->flowid_len16 =
		htonl(FW_WR_LEN16(DIV_ROUND_UP(72, 16)) |
				FW_WR_FLOWID(csk->tid));
	flowc->mnemval[0].mnemonic = FW_FLOWC_MNEM_PFNVFN;
	flowc->mnemval[0].val = htonl(csk->cdev->pfvf);
	flowc->mnemval[1].mnemonic = FW_FLOWC_MNEM_CH;
	flowc->mnemval[1].val = htonl(csk->tx_chan);
	flowc->mnemval[2].mnemonic = FW_FLOWC_MNEM_PORT;
	flowc->mnemval[2].val = htonl(csk->tx_chan);
	flowc->mnemval[3].mnemonic = FW_FLOWC_MNEM_IQID;
	flowc->mnemval[3].val = htonl(csk->rss_qid);
	flowc->mnemval[4].mnemonic = FW_FLOWC_MNEM_SNDNXT;
	flowc->mnemval[4].val = htonl(csk->snd_nxt);
	flowc->mnemval[5].mnemonic = FW_FLOWC_MNEM_RCVNXT;
	flowc->mnemval[5].val = htonl(csk->rcv_nxt);
	flowc->mnemval[6].mnemonic = FW_FLOWC_MNEM_SNDBUF;
	flowc->mnemval[6].val = htonl(cxgb4i_snd_win);
	flowc->mnemval[7].mnemonic = FW_FLOWC_MNEM_MSS;
	flowc->mnemval[7].val = htonl(csk->advmss);
	flowc->mnemval[8].mnemonic = 0;
	flowc->mnemval[8].val = 0;
	for (i = 0; i < 9; i++) {
		flowc->mnemval[i].r4[0] = 0;
		flowc->mnemval[i].r4[1] = 0;
		flowc->mnemval[i].r4[2] = 0;
	}
	set_queue(skb, CPL_PRIORITY_DATA, csk);

	log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_SOCK,
		"csk 0x%p, tid 0x%x, %u,%u,%u,%u,%u,%u,%u.\n",
		csk, csk->tid, 0, csk->tx_chan, csk->rss_qid,
		csk->snd_nxt, csk->rcv_nxt, cxgb4i_snd_win,
		csk->advmss);

	cxgb4_ofld_send(csk->cdev->ports[csk->port_id], skb);
}

static inline void make_tx_data_wr(struct cxgbi_sock *csk, struct sk_buff *skb,
				   int dlen, int len, u32 credits, int compl)
{
	struct fw_ofld_tx_data_wr *req;
	unsigned int submode = cxgbi_skcb_ulp_mode(skb) & 3;
	unsigned int wr_ulp_mode = 0;

	req = (struct fw_ofld_tx_data_wr *)__skb_push(skb, sizeof(*req));

	if (is_ofld_imm(skb)) {
		req->op_to_immdlen = htonl(FW_WR_OP(FW_OFLD_TX_DATA_WR) |
					FW_WR_COMPL(1) |
					FW_WR_IMMDLEN(dlen));
		req->flowid_len16 = htonl(FW_WR_FLOWID(csk->tid) |
						FW_WR_LEN16(credits));
	} else {
		req->op_to_immdlen =
			cpu_to_be32(FW_WR_OP(FW_OFLD_TX_DATA_WR) |
					FW_WR_COMPL(1) |
					FW_WR_IMMDLEN(0));
		req->flowid_len16 =
			cpu_to_be32(FW_WR_FLOWID(csk->tid) |
					FW_WR_LEN16(credits));
	}
	if (submode)
		wr_ulp_mode = FW_OFLD_TX_DATA_WR_ULPMODE(ULP2_MODE_ISCSI) |
				FW_OFLD_TX_DATA_WR_ULPSUBMODE(submode);
	req->tunnel_to_proxy = htonl(wr_ulp_mode |
		 FW_OFLD_TX_DATA_WR_SHOVE(skb_peek(&csk->write_queue) ? 0 : 1));
	req->plen = htonl(len);
	if (!cxgbi_sock_flag(csk, CTPF_TX_DATA_SENT))
		cxgbi_sock_set_flag(csk, CTPF_TX_DATA_SENT);
}

static void arp_failure_skb_discard(void *handle, struct sk_buff *skb)
{
	kfree_skb(skb);
}

static int push_tx_frames(struct cxgbi_sock *csk, int req_completion)
{
	int total_size = 0;
	struct sk_buff *skb;

	if (unlikely(csk->state < CTP_ESTABLISHED ||
		csk->state == CTP_CLOSE_WAIT_1 || csk->state >= CTP_ABORTING)) {
		log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_SOCK |
			 1 << CXGBI_DBG_PDU_TX,
			"csk 0x%p,%u,0x%lx,%u, in closing state.\n",
			csk, csk->state, csk->flags, csk->tid);
		return 0;
	}

	while (csk->wr_cred && (skb = skb_peek(&csk->write_queue)) != NULL) {
		int dlen = skb->len;
		int len = skb->len;
		unsigned int credits_needed;

		skb_reset_transport_header(skb);
		if (is_ofld_imm(skb))
			credits_needed = DIV_ROUND_UP(dlen +
					sizeof(struct fw_ofld_tx_data_wr), 16);
		else
			credits_needed = DIV_ROUND_UP(8*calc_tx_flits_ofld(skb)
					+ sizeof(struct fw_ofld_tx_data_wr),
					16);

		if (csk->wr_cred < credits_needed) {
			log_debug(1 << CXGBI_DBG_PDU_TX,
				"csk 0x%p, skb %u/%u, wr %d < %u.\n",
				csk, skb->len, skb->data_len,
				credits_needed, csk->wr_cred);
			break;
		}
		__skb_unlink(skb, &csk->write_queue);
		set_queue(skb, CPL_PRIORITY_DATA, csk);
		skb->csum = credits_needed;
		csk->wr_cred -= credits_needed;
		csk->wr_una_cred += credits_needed;
		cxgbi_sock_enqueue_wr(csk, skb);

		log_debug(1 << CXGBI_DBG_PDU_TX,
			"csk 0x%p, skb %u/%u, wr %d, left %u, unack %u.\n",
			csk, skb->len, skb->data_len, credits_needed,
			csk->wr_cred, csk->wr_una_cred);

		if (likely(cxgbi_skcb_test_flag(skb, SKCBF_TX_NEED_HDR))) {
			if (!cxgbi_sock_flag(csk, CTPF_TX_DATA_SENT)) {
				send_tx_flowc_wr(csk);
				skb->csum += 5;
				csk->wr_cred -= 5;
				csk->wr_una_cred += 5;
			}
			len += cxgbi_ulp_extra_len(cxgbi_skcb_ulp_mode(skb));
			make_tx_data_wr(csk, skb, dlen, len, credits_needed,
					req_completion);
			csk->snd_nxt += len;
			cxgbi_skcb_clear_flag(skb, SKCBF_TX_NEED_HDR);
		}
		total_size += skb->truesize;
		t4_set_arp_err_handler(skb, csk, arp_failure_skb_discard);

		log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_PDU_TX,
			"csk 0x%p,%u,0x%lx,%u, skb 0x%p, %u.\n",
			csk, csk->state, csk->flags, csk->tid, skb, len);

		cxgb4_l2t_send(csk->cdev->ports[csk->port_id], skb, csk->l2t);
	}
	return total_size;
}

static inline void free_atid(struct cxgbi_sock *csk)
{
	struct cxgb4_lld_info *lldi = cxgbi_cdev_priv(csk->cdev);

	if (cxgbi_sock_flag(csk, CTPF_HAS_ATID)) {
		cxgb4_free_atid(lldi->tids, csk->atid);
		cxgbi_sock_clear_flag(csk, CTPF_HAS_ATID);
		cxgbi_sock_put(csk);
	}
}

static void do_act_establish(struct cxgbi_device *cdev, struct sk_buff *skb)
{
	struct cxgbi_sock *csk;
	struct cpl_act_establish *req = (struct cpl_act_establish *)skb->data;
	unsigned short tcp_opt = ntohs(req->tcp_opt);
	unsigned int tid = GET_TID(req);
	unsigned int atid = GET_TID_TID(ntohl(req->tos_atid));
	struct cxgb4_lld_info *lldi = cxgbi_cdev_priv(cdev);
	struct tid_info *t = lldi->tids;
	u32 rcv_isn = be32_to_cpu(req->rcv_isn);

	csk = lookup_atid(t, atid);
	if (unlikely(!csk)) {
		pr_err("NO conn. for atid %u, cdev 0x%p.\n", atid, cdev);
		goto rel_skb;
	}

	if (csk->atid != atid) {
		pr_err("bad conn atid %u, csk 0x%p,%u,0x%lx,tid %u, atid %u.\n",
			atid, csk, csk->state, csk->flags, csk->tid, csk->atid);
		goto rel_skb;
	}

	pr_info_ipaddr("atid 0x%x, tid 0x%x, csk 0x%p,%u,0x%lx, isn %u.\n",
		       (&csk->saddr), (&csk->daddr),
		       atid, tid, csk, csk->state, csk->flags, rcv_isn);

	module_put(THIS_MODULE);

	cxgbi_sock_get(csk);
	csk->tid = tid;
	cxgb4_insert_tid(lldi->tids, csk, tid);
	cxgbi_sock_set_flag(csk, CTPF_HAS_TID);

	free_atid(csk);

	spin_lock_bh(&csk->lock);
	if (unlikely(csk->state != CTP_ACTIVE_OPEN))
		pr_info("csk 0x%p,%u,0x%lx,%u, got EST.\n",
			csk, csk->state, csk->flags, csk->tid);

	if (csk->retry_timer.function) {
		del_timer(&csk->retry_timer);
		csk->retry_timer.function = NULL;
	}

	csk->copied_seq = csk->rcv_wup = csk->rcv_nxt = rcv_isn;
	/*
	 * Causes the first RX_DATA_ACK to supply any Rx credits we couldn't
	 * pass through opt0.
	 */
	if (cxgb4i_rcv_win > (RCV_BUFSIZ_MASK << 10))
		csk->rcv_wup -= cxgb4i_rcv_win - (RCV_BUFSIZ_MASK << 10);

	csk->advmss = lldi->mtus[GET_TCPOPT_MSS(tcp_opt)] - 40;
	if (GET_TCPOPT_TSTAMP(tcp_opt))
		csk->advmss -= 12;
	if (csk->advmss < 128)
		csk->advmss = 128;

	log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_SOCK,
		"csk 0x%p, mss_idx %u, advmss %u.\n",
			csk, GET_TCPOPT_MSS(tcp_opt), csk->advmss);

	cxgbi_sock_established(csk, ntohl(req->snd_isn), ntohs(req->tcp_opt));

	if (unlikely(cxgbi_sock_flag(csk, CTPF_ACTIVE_CLOSE_NEEDED)))
		send_abort_req(csk);
	else {
		if (skb_queue_len(&csk->write_queue))
			push_tx_frames(csk, 0);
		cxgbi_conn_tx_open(csk);
	}
	spin_unlock_bh(&csk->lock);

rel_skb:
	__kfree_skb(skb);
}

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

static void csk_act_open_retry_timer(unsigned long data)
{
	struct sk_buff *skb;
	struct cxgbi_sock *csk = (struct cxgbi_sock *)data;
	struct cxgb4_lld_info *lldi = cxgbi_cdev_priv(csk->cdev);
	void (*send_act_open_func)(struct cxgbi_sock *, struct sk_buff *,
				   struct l2t_entry *);
	int t4 = is_t4(lldi->adapter_type), size, size6;

	log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_SOCK,
		"csk 0x%p,%u,0x%lx,%u.\n",
		csk, csk->state, csk->flags, csk->tid);

	cxgbi_sock_get(csk);
	spin_lock_bh(&csk->lock);

	if (t4) {
		size = sizeof(struct cpl_act_open_req);
		size6 = sizeof(struct cpl_act_open_req6);
	} else {
		size = sizeof(struct cpl_t5_act_open_req);
		size6 = sizeof(struct cpl_t5_act_open_req6);
	}

	if (csk->csk_family == AF_INET) {
		send_act_open_func = send_act_open_req;
		skb = alloc_wr(size, 0, GFP_ATOMIC);
	} else {
		send_act_open_func = send_act_open_req6;
		skb = alloc_wr(size6, 0, GFP_ATOMIC);
	}

	if (!skb)
		cxgbi_sock_fail_act_open(csk, -ENOMEM);
	else {
		skb->sk = (struct sock *)csk;
		t4_set_arp_err_handler(skb, csk,
				       cxgbi_sock_act_open_req_arp_failure);
		send_act_open_func(csk, skb, csk->l2t);
	}

	spin_unlock_bh(&csk->lock);
	cxgbi_sock_put(csk);

}

static void do_act_open_rpl(struct cxgbi_device *cdev, struct sk_buff *skb)
{
	struct cxgbi_sock *csk;
	struct cpl_act_open_rpl *rpl = (struct cpl_act_open_rpl *)skb->data;
	unsigned int tid = GET_TID(rpl);
	unsigned int atid =
		GET_TID_TID(GET_AOPEN_ATID(be32_to_cpu(rpl->atid_status)));
	unsigned int status = GET_AOPEN_STATUS(be32_to_cpu(rpl->atid_status));
	struct cxgb4_lld_info *lldi = cxgbi_cdev_priv(cdev);
	struct tid_info *t = lldi->tids;

	csk = lookup_atid(t, atid);
	if (unlikely(!csk)) {
		pr_err("NO matching conn. atid %u, tid %u.\n", atid, tid);
		goto rel_skb;
	}

	pr_info_ipaddr("tid %u/%u, status %u.\n"
		       "csk 0x%p,%u,0x%lx. ", (&csk->saddr), (&csk->daddr),
		       atid, tid, status, csk, csk->state, csk->flags);

	if (status == CPL_ERR_RTX_NEG_ADVICE)
		goto rel_skb;

	if (status && status != CPL_ERR_TCAM_FULL &&
	    status != CPL_ERR_CONN_EXIST &&
	    status != CPL_ERR_ARP_MISS)
		cxgb4_remove_tid(lldi->tids, csk->port_id, GET_TID(rpl));

	cxgbi_sock_get(csk);
	spin_lock_bh(&csk->lock);

	if (status == CPL_ERR_CONN_EXIST &&
	    csk->retry_timer.function != csk_act_open_retry_timer) {
		csk->retry_timer.function = csk_act_open_retry_timer;
		mod_timer(&csk->retry_timer, jiffies + HZ / 2);
	} else
		cxgbi_sock_fail_act_open(csk,
					act_open_rpl_status_to_errno(status));

	spin_unlock_bh(&csk->lock);
	cxgbi_sock_put(csk);
rel_skb:
	__kfree_skb(skb);
}

static void do_peer_close(struct cxgbi_device *cdev, struct sk_buff *skb)
{
	struct cxgbi_sock *csk;
	struct cpl_peer_close *req = (struct cpl_peer_close *)skb->data;
	unsigned int tid = GET_TID(req);
	struct cxgb4_lld_info *lldi = cxgbi_cdev_priv(cdev);
	struct tid_info *t = lldi->tids;

	csk = lookup_tid(t, tid);
	if (unlikely(!csk)) {
		pr_err("can't find connection for tid %u.\n", tid);
		goto rel_skb;
	}
	pr_info_ipaddr("csk 0x%p,%u,0x%lx,%u.\n",
		       (&csk->saddr), (&csk->daddr),
		       csk, csk->state, csk->flags, csk->tid);
	cxgbi_sock_rcv_peer_close(csk);
rel_skb:
	__kfree_skb(skb);
}

static void do_close_con_rpl(struct cxgbi_device *cdev, struct sk_buff *skb)
{
	struct cxgbi_sock *csk;
	struct cpl_close_con_rpl *rpl = (struct cpl_close_con_rpl *)skb->data;
	unsigned int tid = GET_TID(rpl);
	struct cxgb4_lld_info *lldi = cxgbi_cdev_priv(cdev);
	struct tid_info *t = lldi->tids;

	csk = lookup_tid(t, tid);
	if (unlikely(!csk)) {
		pr_err("can't find connection for tid %u.\n", tid);
		goto rel_skb;
	}
	pr_info_ipaddr("csk 0x%p,%u,0x%lx,%u.\n",
		       (&csk->saddr), (&csk->daddr),
		       csk, csk->state, csk->flags, csk->tid);
	cxgbi_sock_rcv_close_conn_rpl(csk, ntohl(rpl->snd_nxt));
rel_skb:
	__kfree_skb(skb);
}

static int abort_status_to_errno(struct cxgbi_sock *csk, int abort_reason,
								int *need_rst)
{
	switch (abort_reason) {
	case CPL_ERR_BAD_SYN: /* fall through */
	case CPL_ERR_CONN_RESET:
		return csk->state > CTP_ESTABLISHED ?
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

static void do_abort_req_rss(struct cxgbi_device *cdev, struct sk_buff *skb)
{
	struct cxgbi_sock *csk;
	struct cpl_abort_req_rss *req = (struct cpl_abort_req_rss *)skb->data;
	unsigned int tid = GET_TID(req);
	struct cxgb4_lld_info *lldi = cxgbi_cdev_priv(cdev);
	struct tid_info *t = lldi->tids;
	int rst_status = CPL_ABORT_NO_RST;

	csk = lookup_tid(t, tid);
	if (unlikely(!csk)) {
		pr_err("can't find connection for tid %u.\n", tid);
		goto rel_skb;
	}

	pr_info_ipaddr("csk 0x%p,%u,0x%lx,%u, status %u.\n",
		       (&csk->saddr), (&csk->daddr),
		       csk, csk->state, csk->flags, csk->tid, req->status);

	if (req->status == CPL_ERR_RTX_NEG_ADVICE ||
	    req->status == CPL_ERR_PERSIST_NEG_ADVICE)
		goto rel_skb;

	cxgbi_sock_get(csk);
	spin_lock_bh(&csk->lock);

	if (!cxgbi_sock_flag(csk, CTPF_ABORT_REQ_RCVD)) {
		cxgbi_sock_set_flag(csk, CTPF_ABORT_REQ_RCVD);
		cxgbi_sock_set_state(csk, CTP_ABORTING);
		goto done;
	}

	cxgbi_sock_clear_flag(csk, CTPF_ABORT_REQ_RCVD);
	send_abort_rpl(csk, rst_status);

	if (!cxgbi_sock_flag(csk, CTPF_ABORT_RPL_PENDING)) {
		csk->err = abort_status_to_errno(csk, req->status, &rst_status);
		cxgbi_sock_closed(csk);
	}
done:
	spin_unlock_bh(&csk->lock);
	cxgbi_sock_put(csk);
rel_skb:
	__kfree_skb(skb);
}

static void do_abort_rpl_rss(struct cxgbi_device *cdev, struct sk_buff *skb)
{
	struct cxgbi_sock *csk;
	struct cpl_abort_rpl_rss *rpl = (struct cpl_abort_rpl_rss *)skb->data;
	unsigned int tid = GET_TID(rpl);
	struct cxgb4_lld_info *lldi = cxgbi_cdev_priv(cdev);
	struct tid_info *t = lldi->tids;

	csk = lookup_tid(t, tid);
	if (!csk)
		goto rel_skb;

	if (csk)
		pr_info_ipaddr("csk 0x%p,%u,0x%lx,%u, status %u.\n",
			       (&csk->saddr), (&csk->daddr), csk,
			       csk->state, csk->flags, csk->tid, rpl->status);

	if (rpl->status == CPL_ERR_ABORT_FAILED)
		goto rel_skb;

	cxgbi_sock_rcv_abort_rpl(csk);
rel_skb:
	__kfree_skb(skb);
}

static void do_rx_iscsi_hdr(struct cxgbi_device *cdev, struct sk_buff *skb)
{
	struct cxgbi_sock *csk;
	struct cpl_iscsi_hdr *cpl = (struct cpl_iscsi_hdr *)skb->data;
	unsigned short pdu_len_ddp = be16_to_cpu(cpl->pdu_len_ddp);
	unsigned int tid = GET_TID(cpl);
	struct cxgb4_lld_info *lldi = cxgbi_cdev_priv(cdev);
	struct tid_info *t = lldi->tids;

	csk = lookup_tid(t, tid);
	if (unlikely(!csk)) {
		pr_err("can't find conn. for tid %u.\n", tid);
		goto rel_skb;
	}

	log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_PDU_RX,
		"csk 0x%p,%u,0x%lx, tid %u, skb 0x%p,%u, 0x%x.\n",
		csk, csk->state, csk->flags, csk->tid, skb, skb->len,
		pdu_len_ddp);

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

	cxgbi_skcb_tcp_seq(skb) = ntohl(cpl->seq);
	cxgbi_skcb_flags(skb) = 0;

	skb_reset_transport_header(skb);
	__skb_pull(skb, sizeof(*cpl));
	__pskb_trim(skb, ntohs(cpl->len));

	if (!csk->skb_ulp_lhdr) {
		unsigned char *bhs;
		unsigned int hlen, dlen, plen;

		log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_PDU_RX,
			"csk 0x%p,%u,0x%lx, tid %u, skb 0x%p header.\n",
			csk, csk->state, csk->flags, csk->tid, skb);
		csk->skb_ulp_lhdr = skb;
		cxgbi_skcb_set_flag(skb, SKCBF_RX_HDR);

		if (cxgbi_skcb_tcp_seq(skb) != csk->rcv_nxt) {
			pr_info("tid %u, CPL_ISCSI_HDR, bad seq, 0x%x/0x%x.\n",
				csk->tid, cxgbi_skcb_tcp_seq(skb),
				csk->rcv_nxt);
			goto abort_conn;
		}

		bhs = skb->data;
		hlen = ntohs(cpl->len);
		dlen = ntohl(*(unsigned int *)(bhs + 4)) & 0xFFFFFF;

		plen = ISCSI_PDU_LEN(pdu_len_ddp);
		if (is_t4(lldi->adapter_type))
			plen -= 40;

		if ((hlen + dlen) != plen) {
			pr_info("tid 0x%x, CPL_ISCSI_HDR, pdu len "
				"mismatch %u != %u + %u, seq 0x%x.\n",
				csk->tid, plen, hlen, dlen,
				cxgbi_skcb_tcp_seq(skb));
			goto abort_conn;
		}

		cxgbi_skcb_rx_pdulen(skb) = (hlen + dlen + 3) & (~0x3);
		if (dlen)
			cxgbi_skcb_rx_pdulen(skb) += csk->dcrc_len;
		csk->rcv_nxt += cxgbi_skcb_rx_pdulen(skb);

		log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_PDU_RX,
			"csk 0x%p, skb 0x%p, 0x%x,%u+%u,0x%x,0x%x.\n",
			csk, skb, *bhs, hlen, dlen,
			ntohl(*((unsigned int *)(bhs + 16))),
			ntohl(*((unsigned int *)(bhs + 24))));

	} else {
		struct sk_buff *lskb = csk->skb_ulp_lhdr;

		cxgbi_skcb_set_flag(lskb, SKCBF_RX_DATA);
		log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_PDU_RX,
			"csk 0x%p,%u,0x%lx, skb 0x%p data, 0x%p.\n",
			csk, csk->state, csk->flags, skb, lskb);
	}

	__skb_queue_tail(&csk->receive_queue, skb);
	spin_unlock_bh(&csk->lock);
	return;

abort_conn:
	send_abort_req(csk);
discard:
	spin_unlock_bh(&csk->lock);
rel_skb:
	__kfree_skb(skb);
}

static void do_rx_data_ddp(struct cxgbi_device *cdev,
				  struct sk_buff *skb)
{
	struct cxgbi_sock *csk;
	struct sk_buff *lskb;
	struct cpl_rx_data_ddp *rpl = (struct cpl_rx_data_ddp *)skb->data;
	unsigned int tid = GET_TID(rpl);
	struct cxgb4_lld_info *lldi = cxgbi_cdev_priv(cdev);
	struct tid_info *t = lldi->tids;
	unsigned int status = ntohl(rpl->ddpvld);

	csk = lookup_tid(t, tid);
	if (unlikely(!csk)) {
		pr_err("can't find connection for tid %u.\n", tid);
		goto rel_skb;
	}

	log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_PDU_RX,
		"csk 0x%p,%u,0x%lx, skb 0x%p,0x%x, lhdr 0x%p.\n",
		csk, csk->state, csk->flags, skb, status, csk->skb_ulp_lhdr);

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

	if (!csk->skb_ulp_lhdr) {
		pr_err("tid 0x%x, rcv RX_DATA_DDP w/o pdu bhs.\n", csk->tid);
		goto abort_conn;
	}

	lskb = csk->skb_ulp_lhdr;
	csk->skb_ulp_lhdr = NULL;

	cxgbi_skcb_rx_ddigest(lskb) = ntohl(rpl->ulp_crc);

	if (ntohs(rpl->len) != cxgbi_skcb_rx_pdulen(lskb))
		pr_info("tid 0x%x, RX_DATA_DDP pdulen %u != %u.\n",
			csk->tid, ntohs(rpl->len), cxgbi_skcb_rx_pdulen(lskb));

	if (status & (1 << CPL_RX_DDP_STATUS_HCRC_SHIFT)) {
		pr_info("csk 0x%p, lhdr 0x%p, status 0x%x, hcrc bad 0x%lx.\n",
			csk, lskb, status, cxgbi_skcb_flags(lskb));
		cxgbi_skcb_set_flag(lskb, SKCBF_RX_HCRC_ERR);
	}
	if (status & (1 << CPL_RX_DDP_STATUS_DCRC_SHIFT)) {
		pr_info("csk 0x%p, lhdr 0x%p, status 0x%x, dcrc bad 0x%lx.\n",
			csk, lskb, status, cxgbi_skcb_flags(lskb));
		cxgbi_skcb_set_flag(lskb, SKCBF_RX_DCRC_ERR);
	}
	if (status & (1 << CPL_RX_DDP_STATUS_PAD_SHIFT)) {
		log_debug(1 << CXGBI_DBG_PDU_RX,
			"csk 0x%p, lhdr 0x%p, status 0x%x, pad bad.\n",
			csk, lskb, status);
		cxgbi_skcb_set_flag(lskb, SKCBF_RX_PAD_ERR);
	}
	if ((status & (1 << CPL_RX_DDP_STATUS_DDP_SHIFT)) &&
		!cxgbi_skcb_test_flag(lskb, SKCBF_RX_DATA)) {
		log_debug(1 << CXGBI_DBG_PDU_RX,
			"csk 0x%p, lhdr 0x%p, 0x%x, data ddp'ed.\n",
			csk, lskb, status);
		cxgbi_skcb_set_flag(lskb, SKCBF_RX_DATA_DDPD);
	}
	log_debug(1 << CXGBI_DBG_PDU_RX,
		"csk 0x%p, lskb 0x%p, f 0x%lx.\n",
		csk, lskb, cxgbi_skcb_flags(lskb));

	cxgbi_skcb_set_flag(lskb, SKCBF_RX_STATUS);
	cxgbi_conn_pdu_ready(csk);
	spin_unlock_bh(&csk->lock);
	goto rel_skb;

abort_conn:
	send_abort_req(csk);
discard:
	spin_unlock_bh(&csk->lock);
rel_skb:
	__kfree_skb(skb);
}

static void do_fw4_ack(struct cxgbi_device *cdev, struct sk_buff *skb)
{
	struct cxgbi_sock *csk;
	struct cpl_fw4_ack *rpl = (struct cpl_fw4_ack *)skb->data;
	unsigned int tid = GET_TID(rpl);
	struct cxgb4_lld_info *lldi = cxgbi_cdev_priv(cdev);
	struct tid_info *t = lldi->tids;

	csk = lookup_tid(t, tid);
	if (unlikely(!csk))
		pr_err("can't find connection for tid %u.\n", tid);
	else {
		log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_SOCK,
			"csk 0x%p,%u,0x%lx,%u.\n",
			csk, csk->state, csk->flags, csk->tid);
		cxgbi_sock_rcv_wr_ack(csk, rpl->credits, ntohl(rpl->snd_una),
					rpl->seq_vld);
	}
	__kfree_skb(skb);
}

static void do_set_tcb_rpl(struct cxgbi_device *cdev, struct sk_buff *skb)
{
	struct cpl_set_tcb_rpl *rpl = (struct cpl_set_tcb_rpl *)skb->data;
	unsigned int tid = GET_TID(rpl);
	struct cxgb4_lld_info *lldi = cxgbi_cdev_priv(cdev);
	struct tid_info *t = lldi->tids;
	struct cxgbi_sock *csk;

	csk = lookup_tid(t, tid);
	if (!csk)
		pr_err("can't find conn. for tid %u.\n", tid);

	log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_SOCK,
		"csk 0x%p,%u,%lx,%u, status 0x%x.\n",
		csk, csk->state, csk->flags, csk->tid, rpl->status);

	if (rpl->status != CPL_ERR_NONE)
		pr_err("csk 0x%p,%u, SET_TCB_RPL status %u.\n",
			csk, tid, rpl->status);

	__kfree_skb(skb);
}

static int alloc_cpls(struct cxgbi_sock *csk)
{
	csk->cpl_close = alloc_wr(sizeof(struct cpl_close_con_req),
					0, GFP_KERNEL);
	if (!csk->cpl_close)
		return -ENOMEM;

	csk->cpl_abort_req = alloc_wr(sizeof(struct cpl_abort_req),
					0, GFP_KERNEL);
	if (!csk->cpl_abort_req)
		goto free_cpls;

	csk->cpl_abort_rpl = alloc_wr(sizeof(struct cpl_abort_rpl),
					0, GFP_KERNEL);
	if (!csk->cpl_abort_rpl)
		goto free_cpls;
	return 0;

free_cpls:
	cxgbi_sock_free_cpl_skbs(csk);
	return -ENOMEM;
}

static inline void l2t_put(struct cxgbi_sock *csk)
{
	if (csk->l2t) {
		cxgb4_l2t_release(csk->l2t);
		csk->l2t = NULL;
		cxgbi_sock_put(csk);
	}
}

static void release_offload_resources(struct cxgbi_sock *csk)
{
	struct cxgb4_lld_info *lldi;

	log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_SOCK,
		"csk 0x%p,%u,0x%lx,%u.\n",
		csk, csk->state, csk->flags, csk->tid);

	cxgbi_sock_free_cpl_skbs(csk);
	if (csk->wr_cred != csk->wr_max_cred) {
		cxgbi_sock_purge_wr_queue(csk);
		cxgbi_sock_reset_wr_list(csk);
	}

	l2t_put(csk);
	if (cxgbi_sock_flag(csk, CTPF_HAS_ATID))
		free_atid(csk);
	else if (cxgbi_sock_flag(csk, CTPF_HAS_TID)) {
		lldi = cxgbi_cdev_priv(csk->cdev);
		cxgb4_remove_tid(lldi->tids, 0, csk->tid);
		cxgbi_sock_clear_flag(csk, CTPF_HAS_TID);
		cxgbi_sock_put(csk);
	}
	csk->dst = NULL;
	csk->cdev = NULL;
}

static int init_act_open(struct cxgbi_sock *csk)
{
	struct cxgbi_device *cdev = csk->cdev;
	struct cxgb4_lld_info *lldi = cxgbi_cdev_priv(cdev);
	struct net_device *ndev = cdev->ports[csk->port_id];
	struct sk_buff *skb = NULL;
	struct neighbour *n = NULL;
	void *daddr;
	unsigned int step;
	unsigned int size, size6;
	int t4 = is_t4(lldi->adapter_type);

	log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_SOCK,
		"csk 0x%p,%u,0x%lx,%u.\n",
		csk, csk->state, csk->flags, csk->tid);

	if (csk->csk_family == AF_INET)
		daddr = &csk->daddr.sin_addr.s_addr;
	else
		daddr = &csk->daddr6.sin6_addr;

	n = dst_neigh_lookup(csk->dst, daddr);

	if (!n) {
		pr_err("%s, can't get neighbour of csk->dst.\n", ndev->name);
		goto rel_resource;
	}

	csk->atid = cxgb4_alloc_atid(lldi->tids, csk);
	if (csk->atid < 0) {
		pr_err("%s, NO atid available.\n", ndev->name);
		return -EINVAL;
	}
	cxgbi_sock_set_flag(csk, CTPF_HAS_ATID);
	cxgbi_sock_get(csk);

	n = dst_neigh_lookup(csk->dst, &csk->daddr.sin_addr.s_addr);
	if (!n) {
		pr_err("%s, can't get neighbour of csk->dst.\n", ndev->name);
		goto rel_resource;
	}
	csk->l2t = cxgb4_l2t_get(lldi->l2t, n, ndev, 0);
	if (!csk->l2t) {
		pr_err("%s, cannot alloc l2t.\n", ndev->name);
		goto rel_resource;
	}
	cxgbi_sock_get(csk);

	if (t4) {
		size = sizeof(struct cpl_act_open_req);
		size6 = sizeof(struct cpl_act_open_req6);
	} else {
		size = sizeof(struct cpl_t5_act_open_req);
		size6 = sizeof(struct cpl_t5_act_open_req6);
	}

	if (csk->csk_family == AF_INET)
		skb = alloc_wr(size, 0, GFP_NOIO);
	else
		skb = alloc_wr(size6, 0, GFP_NOIO);

	if (!skb)
		goto rel_resource;
	skb->sk = (struct sock *)csk;
	t4_set_arp_err_handler(skb, csk, cxgbi_sock_act_open_req_arp_failure);

	if (!csk->mtu)
		csk->mtu = dst_mtu(csk->dst);
	cxgb4_best_mtu(lldi->mtus, csk->mtu, &csk->mss_idx);
	csk->tx_chan = cxgb4_port_chan(ndev);
	/* SMT two entries per row */
	csk->smac_idx = ((cxgb4_port_viid(ndev) & 0x7F)) << 1;
	step = lldi->ntxq / lldi->nchan;
	csk->txq_idx = cxgb4_port_idx(ndev) * step;
	step = lldi->nrxq / lldi->nchan;
	csk->rss_qid = lldi->rxq_ids[cxgb4_port_idx(ndev) * step];
	csk->wr_cred = lldi->wr_cred -
		       DIV_ROUND_UP(sizeof(struct cpl_abort_req), 16);
	csk->wr_max_cred = csk->wr_cred;
	csk->wr_una_cred = 0;
	cxgbi_sock_reset_wr_list(csk);
	csk->err = 0;

	pr_info_ipaddr("csk 0x%p,%u,0x%lx,%u,%u,%u, mtu %u,%u, smac %u.\n",
		       (&csk->saddr), (&csk->daddr), csk, csk->state,
		       csk->flags, csk->tx_chan, csk->txq_idx, csk->rss_qid,
		       csk->mtu, csk->mss_idx, csk->smac_idx);

	/* must wait for either a act_open_rpl or act_open_establish */
	try_module_get(THIS_MODULE);
	cxgbi_sock_set_state(csk, CTP_ACTIVE_OPEN);
	if (csk->csk_family == AF_INET)
		send_act_open_req(csk, skb, csk->l2t);
	else
		send_act_open_req6(csk, skb, csk->l2t);
	neigh_release(n);

	return 0;

rel_resource:
	if (n)
		neigh_release(n);
	if (skb)
		__kfree_skb(skb);
	return -EINVAL;
}

#define CPL_ISCSI_DATA		0xB2
#define CPL_RX_ISCSI_DDP	0x49
cxgb4i_cplhandler_func cxgb4i_cplhandlers[NUM_CPL_CMDS] = {
	[CPL_ACT_ESTABLISH] = do_act_establish,
	[CPL_ACT_OPEN_RPL] = do_act_open_rpl,
	[CPL_PEER_CLOSE] = do_peer_close,
	[CPL_ABORT_REQ_RSS] = do_abort_req_rss,
	[CPL_ABORT_RPL_RSS] = do_abort_rpl_rss,
	[CPL_CLOSE_CON_RPL] = do_close_con_rpl,
	[CPL_FW4_ACK] = do_fw4_ack,
	[CPL_ISCSI_HDR] = do_rx_iscsi_hdr,
	[CPL_ISCSI_DATA] = do_rx_iscsi_hdr,
	[CPL_SET_TCB_RPL] = do_set_tcb_rpl,
	[CPL_RX_DATA_DDP] = do_rx_data_ddp,
	[CPL_RX_ISCSI_DDP] = do_rx_data_ddp,
};

int cxgb4i_ofld_init(struct cxgbi_device *cdev)
{
	int rc;

	if (cxgb4i_max_connect > CXGB4I_MAX_CONN)
		cxgb4i_max_connect = CXGB4I_MAX_CONN;

	rc = cxgbi_device_portmap_create(cdev, cxgb4i_sport_base,
					cxgb4i_max_connect);
	if (rc < 0)
		return rc;

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
#define ULPMEM_IDATA_MAX_NPPODS	4 /* 256/PPOD_SIZE */
static inline void ulp_mem_io_set_hdr(struct cxgb4_lld_info *lldi,
				struct ulp_mem_io *req,
				unsigned int wr_len, unsigned int dlen,
				unsigned int pm_addr)
{
	struct ulptx_idata *idata = (struct ulptx_idata *)(req + 1);

	INIT_ULPTX_WR(req, wr_len, 0, 0);
	if (is_t4(lldi->adapter_type))
		req->cmd = htonl(ULPTX_CMD(ULP_TX_MEM_WRITE) |
					(ULP_MEMIO_ORDER(1)));
	else
		req->cmd = htonl(ULPTX_CMD(ULP_TX_MEM_WRITE) |
					(V_T5_ULP_MEMIO_IMM(1)));
	req->dlen = htonl(ULP_MEMIO_DATA_LEN(dlen >> 5));
	req->lock_addr = htonl(ULP_MEMIO_ADDR(pm_addr >> 5));
	req->len16 = htonl(DIV_ROUND_UP(wr_len - sizeof(req->wr), 16));

	idata->cmd_more = htonl(ULPTX_CMD(ULP_TX_SC_IMM));
	idata->len = htonl(dlen);
}

static int ddp_ppod_write_idata(struct cxgbi_device *cdev, unsigned int port_id,
				struct cxgbi_pagepod_hdr *hdr, unsigned int idx,
				unsigned int npods,
				struct cxgbi_gather_list *gl,
				unsigned int gl_pidx)
{
	struct cxgbi_ddp_info *ddp = cdev->ddp;
	struct cxgb4_lld_info *lldi = cxgbi_cdev_priv(cdev);
	struct sk_buff *skb;
	struct ulp_mem_io *req;
	struct ulptx_idata *idata;
	struct cxgbi_pagepod *ppod;
	unsigned int pm_addr = idx * PPOD_SIZE + ddp->llimit;
	unsigned int dlen = PPOD_SIZE * npods;
	unsigned int wr_len = roundup(sizeof(struct ulp_mem_io) +
				sizeof(struct ulptx_idata) + dlen, 16);
	unsigned int i;

	skb = alloc_wr(wr_len, 0, GFP_ATOMIC);
	if (!skb) {
		pr_err("cdev 0x%p, idx %u, npods %u, OOM.\n",
			cdev, idx, npods);
		return -ENOMEM;
	}
	req = (struct ulp_mem_io *)skb->head;
	set_queue(skb, CPL_PRIORITY_CONTROL, NULL);

	ulp_mem_io_set_hdr(lldi, req, wr_len, dlen, pm_addr);
	idata = (struct ulptx_idata *)(req + 1);
	ppod = (struct cxgbi_pagepod *)(idata + 1);

	for (i = 0; i < npods; i++, ppod++, gl_pidx += PPOD_PAGES_MAX) {
		if (!hdr && !gl)
			cxgbi_ddp_ppod_clear(ppod);
		else
			cxgbi_ddp_ppod_set(ppod, hdr, gl, gl_pidx);
	}

	cxgb4_ofld_send(cdev->ports[port_id], skb);
	return 0;
}

static int ddp_set_map(struct cxgbi_sock *csk, struct cxgbi_pagepod_hdr *hdr,
			unsigned int idx, unsigned int npods,
			struct cxgbi_gather_list *gl)
{
	unsigned int i, cnt;
	int err = 0;

	for (i = 0; i < npods; i += cnt, idx += cnt) {
		cnt = npods - i;
		if (cnt > ULPMEM_IDATA_MAX_NPPODS)
			cnt = ULPMEM_IDATA_MAX_NPPODS;
		err = ddp_ppod_write_idata(csk->cdev, csk->port_id, hdr,
					idx, cnt, gl, 4 * i);
		if (err < 0)
			break;
	}
	return err;
}

static void ddp_clear_map(struct cxgbi_hba *chba, unsigned int tag,
			  unsigned int idx, unsigned int npods)
{
	unsigned int i, cnt;
	int err;

	for (i = 0; i < npods; i += cnt, idx += cnt) {
		cnt = npods - i;
		if (cnt > ULPMEM_IDATA_MAX_NPPODS)
			cnt = ULPMEM_IDATA_MAX_NPPODS;
		err = ddp_ppod_write_idata(chba->cdev, chba->port_id, NULL,
					idx, cnt, NULL, 0);
		if (err < 0)
			break;
	}
}

static int ddp_setup_conn_pgidx(struct cxgbi_sock *csk, unsigned int tid,
				int pg_idx, bool reply)
{
	struct sk_buff *skb;
	struct cpl_set_tcb_field *req;

	if (!pg_idx || pg_idx >= DDP_PGIDX_MAX)
		return 0;

	skb = alloc_wr(sizeof(*req), 0, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	/*  set up ulp page size */
	req = (struct cpl_set_tcb_field *)skb->head;
	INIT_TP_WR(req, csk->tid);
	OPCODE_TID(req) = htonl(MK_OPCODE_TID(CPL_SET_TCB_FIELD, csk->tid));
	req->reply_ctrl = htons(NO_REPLY(reply) | QUEUENO(csk->rss_qid));
	req->word_cookie = htons(0);
	req->mask = cpu_to_be64(0x3 << 8);
	req->val = cpu_to_be64(pg_idx << 8);
	set_wr_txq(skb, CPL_PRIORITY_CONTROL, csk->port_id);

	log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_SOCK,
		"csk 0x%p, tid 0x%x, pg_idx %u.\n", csk, csk->tid, pg_idx);

	cxgb4_ofld_send(csk->cdev->ports[csk->port_id], skb);
	return 0;
}

static int ddp_setup_conn_digest(struct cxgbi_sock *csk, unsigned int tid,
				 int hcrc, int dcrc, int reply)
{
	struct sk_buff *skb;
	struct cpl_set_tcb_field *req;

	if (!hcrc && !dcrc)
		return 0;

	skb = alloc_wr(sizeof(*req), 0, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	csk->hcrc_len = (hcrc ? 4 : 0);
	csk->dcrc_len = (dcrc ? 4 : 0);
	/*  set up ulp submode */
	req = (struct cpl_set_tcb_field *)skb->head;
	INIT_TP_WR(req, tid);
	OPCODE_TID(req) = htonl(MK_OPCODE_TID(CPL_SET_TCB_FIELD, tid));
	req->reply_ctrl = htons(NO_REPLY(reply) | QUEUENO(csk->rss_qid));
	req->word_cookie = htons(0);
	req->mask = cpu_to_be64(0x3 << 4);
	req->val = cpu_to_be64(((hcrc ? ULP_CRC_HEADER : 0) |
				(dcrc ? ULP_CRC_DATA : 0)) << 4);
	set_wr_txq(skb, CPL_PRIORITY_CONTROL, csk->port_id);

	log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_SOCK,
		"csk 0x%p, tid 0x%x, crc %d,%d.\n", csk, csk->tid, hcrc, dcrc);

	cxgb4_ofld_send(csk->cdev->ports[csk->port_id], skb);
	return 0;
}

static int cxgb4i_ddp_init(struct cxgbi_device *cdev)
{
	struct cxgb4_lld_info *lldi = cxgbi_cdev_priv(cdev);
	struct cxgbi_ddp_info *ddp = cdev->ddp;
	unsigned int tagmask, pgsz_factor[4];
	int err;

	if (ddp) {
		kref_get(&ddp->refcnt);
		pr_warn("cdev 0x%p, ddp 0x%p already set up.\n",
			cdev, cdev->ddp);
		return -EALREADY;
	}

	err = cxgbi_ddp_init(cdev, lldi->vr->iscsi.start,
			lldi->vr->iscsi.start + lldi->vr->iscsi.size - 1,
			lldi->iscsi_iolen, lldi->iscsi_iolen);
	if (err < 0)
		return err;

	ddp = cdev->ddp;

	tagmask = ddp->idx_mask << PPOD_IDX_SHIFT;
	cxgbi_ddp_page_size_factor(pgsz_factor);
	cxgb4_iscsi_init(lldi->ports[0], tagmask, pgsz_factor);

	cdev->csk_ddp_setup_digest = ddp_setup_conn_digest;
	cdev->csk_ddp_setup_pgidx = ddp_setup_conn_pgidx;
	cdev->csk_ddp_set = ddp_set_map;
	cdev->csk_ddp_clear = ddp_clear_map;

	pr_info("cxgb4i 0x%p tag: sw %u, rsvd %u,%u, mask 0x%x.\n",
		cdev, cdev->tag_format.sw_bits, cdev->tag_format.rsvd_bits,
		cdev->tag_format.rsvd_shift, cdev->tag_format.rsvd_mask);
	pr_info("cxgb4i 0x%p, nppods %u, bits %u, mask 0x%x,0x%x pkt %u/%u, "
		" %u/%u.\n",
		cdev, ddp->nppods, ddp->idx_bits, ddp->idx_mask,
		ddp->rsvd_tag_mask, ddp->max_txsz, lldi->iscsi_iolen,
		ddp->max_rxsz, lldi->iscsi_iolen);
	pr_info("cxgb4i 0x%p max payload size: %u/%u, %u/%u.\n",
		cdev, cdev->tx_max_size, ddp->max_txsz, cdev->rx_max_size,
		ddp->max_rxsz);
	return 0;
}

static int cxgbi_inet6addr_handler(struct notifier_block *this,
				   unsigned long event, void *data)
{
	struct inet6_ifaddr *ifa = data;
	struct net_device *event_dev = ifa->idev->dev;
	struct cxgbi_device *cdev;
	int ret = NOTIFY_DONE;

	rcu_read_lock();

	if (event_dev->priv_flags & IFF_802_1Q_VLAN)
		event_dev = vlan_dev_real_dev(event_dev);

	cdev = cxgbi_device_find_by_netdev(event_dev, NULL);
	if (!cdev) {
		rcu_read_unlock();
		return ret;
	}
	switch (event) {
	case NETDEV_UP:
		ret = cxgb4_clip_get(event_dev,
				     (const struct in6_addr *)
				     ((ifa)->addr.s6_addr));
		if (ret < 0) {
			rcu_read_unlock();
			return ret;
		}
		ret = NOTIFY_OK;
		break;

	case NETDEV_DOWN:
		cxgb4_clip_release(event_dev,
				   (const struct in6_addr *)
				   ((ifa)->addr.s6_addr));
		ret = NOTIFY_OK;
		break;

	default:
		break;
	}

	rcu_read_unlock();
	return ret;
}

static struct notifier_block cxgbi_inet6addr_notifier = {
	.notifier_call = cxgbi_inet6addr_handler
};

/* Retrieve IPv6 addresses from a root device (bond, vlan) associated with
 * a physical device.
 * The physical device reference is needed to send the actual CLIP command.
 */
static int update_dev_clip(struct net_device *root_dev, struct net_device *dev)
{
	struct inet6_dev *idev = NULL;
	struct inet6_ifaddr *ifa;
	int ret = 0;

	idev = __in6_dev_get(root_dev);
	if (!idev)
		return ret;

	read_lock_bh(&idev->lock);
	list_for_each_entry(ifa, &idev->addr_list, if_list) {
		pr_info("updating the clip for addr %pI6\n",
			ifa->addr.s6_addr);
		ret = cxgb4_clip_get(dev, (const struct in6_addr *)
				     ifa->addr.s6_addr);
		if (ret < 0)
			break;
	}

	read_unlock_bh(&idev->lock);
	return ret;
}

static int update_root_dev_clip(struct net_device *dev)
{
	struct net_device *root_dev = NULL;
	int i, ret = 0;

	/* First populate the real net device's IPv6 address */
	ret = update_dev_clip(dev, dev);
	if (ret)
		return ret;

	/* Parse all bond and vlan devices layered on top of the physical dev */
	root_dev = netdev_master_upper_dev_get(dev);
	if (root_dev) {
		ret = update_dev_clip(root_dev, dev);
		if (ret)
			return ret;
	}

	for (i = 0; i < VLAN_N_VID; i++) {
		root_dev = __vlan_find_dev_deep_rcu(dev, htons(ETH_P_8021Q), i);
		if (!root_dev)
			continue;

		ret = update_dev_clip(root_dev, dev);
		if (ret)
			break;
	}
	return ret;
}

static void cxgbi_update_clip(struct cxgbi_device *cdev)
{
	int i;

	rcu_read_lock();

	for (i = 0; i < cdev->nports; i++) {
		struct net_device *dev = cdev->ports[i];
		int ret = 0;

		if (dev)
			ret = update_root_dev_clip(dev);
		if (ret < 0)
			break;
	}
	rcu_read_unlock();
}

static void *t4_uld_add(const struct cxgb4_lld_info *lldi)
{
	struct cxgbi_device *cdev;
	struct port_info *pi;
	int i, rc;

	cdev = cxgbi_device_register(sizeof(*lldi), lldi->nports);
	if (!cdev) {
		pr_info("t4 device 0x%p, register failed.\n", lldi);
		return NULL;
	}
	pr_info("0x%p,0x%x, ports %u,%s, chan %u, q %u,%u, wr %u.\n",
		cdev, lldi->adapter_type, lldi->nports,
		lldi->ports[0]->name, lldi->nchan, lldi->ntxq,
		lldi->nrxq, lldi->wr_cred);
	for (i = 0; i < lldi->nrxq; i++)
		log_debug(1 << CXGBI_DBG_DEV,
			"t4 0x%p, rxq id #%d: %u.\n",
			cdev, i, lldi->rxq_ids[i]);

	memcpy(cxgbi_cdev_priv(cdev), lldi, sizeof(*lldi));
	cdev->flags = CXGBI_FLAG_DEV_T4;
	cdev->pdev = lldi->pdev;
	cdev->ports = lldi->ports;
	cdev->nports = lldi->nports;
	cdev->mtus = lldi->mtus;
	cdev->nmtus = NMTUS;
	cdev->snd_win = cxgb4i_snd_win;
	cdev->rcv_win = cxgb4i_rcv_win;
	cdev->rx_credit_thres = cxgb4i_rx_credit_thres;
	cdev->skb_tx_rsvd = CXGB4I_TX_HEADER_LEN;
	cdev->skb_rx_extra = sizeof(struct cpl_iscsi_hdr);
	cdev->itp = &cxgb4i_iscsi_transport;

	cdev->pfvf = FW_VIID_PFN_GET(cxgb4_port_viid(lldi->ports[0])) << 8;
	pr_info("cdev 0x%p,%s, pfvf %u.\n",
		cdev, lldi->ports[0]->name, cdev->pfvf);

	rc = cxgb4i_ddp_init(cdev);
	if (rc) {
		pr_info("t4 0x%p ddp init failed.\n", cdev);
		goto err_out;
	}
	rc = cxgb4i_ofld_init(cdev);
	if (rc) {
		pr_info("t4 0x%p ofld init failed.\n", cdev);
		goto err_out;
	}

	rc = cxgbi_hbas_add(cdev, CXGB4I_MAX_LUN, CXGBI_MAX_CONN,
				&cxgb4i_host_template, cxgb4i_stt);
	if (rc)
		goto err_out;

	for (i = 0; i < cdev->nports; i++) {
		pi = netdev_priv(lldi->ports[i]);
		cdev->hbas[i]->port_id = pi->port_id;
	}
	return cdev;

err_out:
	cxgbi_device_unregister(cdev);
	return ERR_PTR(-ENOMEM);
}

#define RX_PULL_LEN	128
static int t4_uld_rx_handler(void *handle, const __be64 *rsp,
				const struct pkt_gl *pgl)
{
	const struct cpl_act_establish *rpl;
	struct sk_buff *skb;
	unsigned int opc;
	struct cxgbi_device *cdev = handle;

	if (pgl == NULL) {
		unsigned int len = 64 - sizeof(struct rsp_ctrl) - 8;

		skb = alloc_wr(len, 0, GFP_ATOMIC);
		if (!skb)
			goto nomem;
		skb_copy_to_linear_data(skb, &rsp[1], len);
	} else {
		if (unlikely(*(u8 *)rsp != *(u8 *)pgl->va)) {
			pr_info("? FL 0x%p,RSS%#llx,FL %#llx,len %u.\n",
				pgl->va, be64_to_cpu(*rsp),
				be64_to_cpu(*(u64 *)pgl->va),
				pgl->tot_len);
			return 0;
		}
		skb = cxgb4_pktgl_to_skb(pgl, RX_PULL_LEN, RX_PULL_LEN);
		if (unlikely(!skb))
			goto nomem;
	}

	rpl = (struct cpl_act_establish *)skb->data;
	opc = rpl->ot.opcode;
	log_debug(1 << CXGBI_DBG_TOE,
		"cdev %p, opcode 0x%x(0x%x,0x%x), skb %p.\n",
		 cdev, opc, rpl->ot.opcode_tid, ntohl(rpl->ot.opcode_tid), skb);
	if (cxgb4i_cplhandlers[opc])
		cxgb4i_cplhandlers[opc](cdev, skb);
	else {
		pr_err("No handler for opcode 0x%x.\n", opc);
		__kfree_skb(skb);
	}
	return 0;
nomem:
	log_debug(1 << CXGBI_DBG_TOE, "OOM bailing out.\n");
	return 1;
}

static int t4_uld_state_change(void *handle, enum cxgb4_state state)
{
	struct cxgbi_device *cdev = handle;

	switch (state) {
	case CXGB4_STATE_UP:
		pr_info("cdev 0x%p, UP.\n", cdev);
		cxgbi_update_clip(cdev);
		/* re-initialize */
		break;
	case CXGB4_STATE_START_RECOVERY:
		pr_info("cdev 0x%p, RECOVERY.\n", cdev);
		/* close all connections */
		break;
	case CXGB4_STATE_DOWN:
		pr_info("cdev 0x%p, DOWN.\n", cdev);
		break;
	case CXGB4_STATE_DETACH:
		pr_info("cdev 0x%p, DETACH.\n", cdev);
		cxgbi_device_unregister(cdev);
		break;
	default:
		pr_info("cdev 0x%p, unknown state %d.\n", cdev, state);
		break;
	}
	return 0;
}

static int __init cxgb4i_init_module(void)
{
	int rc;

	printk(KERN_INFO "%s", version);

	rc = cxgbi_iscsi_init(&cxgb4i_iscsi_transport, &cxgb4i_stt);
	if (rc < 0)
		return rc;
	cxgb4_register_uld(CXGB4_ULD_ISCSI, &cxgb4i_uld_info);

	register_inet6addr_notifier(&cxgbi_inet6addr_notifier);

	return 0;
}

static void __exit cxgb4i_exit_module(void)
{
	unregister_inet6addr_notifier(&cxgbi_inet6addr_notifier);

	cxgb4_unregister_uld(CXGB4_ULD_ISCSI);
	cxgbi_device_unregister_all(CXGBI_FLAG_DEV_T4);
	cxgbi_iscsi_cleanup(&cxgb4i_iscsi_transport, &cxgb4i_stt);
}

module_init(cxgb4i_init_module);
module_exit(cxgb4i_exit_module);
