/*
 * cxgb4i.c: Chelsio T4 iSCSI driver.
 *
 * Copyright (c) 2010-2015 Chelsio Communications, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * Written by:	Karen Xie (kxie@chelsio.com)
 *		Rakesh Ranjan (rranjan@chelsio.com)
 */

#define pr_fmt(fmt) KBUILD_MODNAME ":%s: " fmt, __func__

#include <linux/kernel.h>
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
#include "clip_tbl.h"

static unsigned int dbg_level;

#include "../libcxgbi.h"

#ifdef CONFIG_CHELSIO_T4_DCB
#include <net/dcbevent.h>
#include "cxgb4_dcb.h"
#endif

#define	DRV_MODULE_NAME		"cxgb4i"
#define DRV_MODULE_DESC		"Chelsio T4-T6 iSCSI Driver"
#define	DRV_MODULE_VERSION	"0.9.5-ko"
#define DRV_MODULE_RELDATE	"Apr. 2015"

static char version[] =
	DRV_MODULE_DESC " " DRV_MODULE_NAME
	" v" DRV_MODULE_VERSION " (" DRV_MODULE_RELDATE ")\n";

MODULE_AUTHOR("Chelsio Communications, Inc.");
MODULE_DESCRIPTION(DRV_MODULE_DESC);
MODULE_VERSION(DRV_MODULE_VERSION);
MODULE_LICENSE("GPL");

module_param(dbg_level, uint, 0644);
MODULE_PARM_DESC(dbg_level, "Debug flag (default=0)");

#define CXGB4I_DEFAULT_10G_RCV_WIN (256 * 1024)
static int cxgb4i_rcv_win = -1;
module_param(cxgb4i_rcv_win, int, 0644);
MODULE_PARM_DESC(cxgb4i_rcv_win, "TCP receive window in bytes");

#define CXGB4I_DEFAULT_10G_SND_WIN (128 * 1024)
static int cxgb4i_snd_win = -1;
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
static inline int send_tx_flowc_wr(struct cxgbi_sock *);

static const struct cxgb4_uld_info cxgb4i_uld_info = {
	.name = DRV_MODULE_NAME,
	.nrxq = MAX_ULD_QSETS,
	.ntxq = MAX_ULD_QSETS,
	.rxq_size = 1024,
	.lro = false,
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
	.change_queue_depth = scsi_change_queue_depth,
	.sg_tablesize	= SG_ALL,
	.max_sectors	= 0xFFFF,
	.cmd_per_lun	= ISCSI_DEF_CMD_PER_LUN,
	.eh_timed_out	= iscsi_eh_cmd_timed_out,
	.eh_abort_handler = iscsi_eh_abort,
	.eh_device_reset_handler = iscsi_eh_device_reset,
	.eh_target_reset_handler = iscsi_eh_recover_target,
	.target_alloc	= iscsi_target_alloc,
	.dma_boundary	= PAGE_SIZE - 1,
	.this_id	= -1,
	.track_queue_depth = 1,
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

#ifdef CONFIG_CHELSIO_T4_DCB
static int
cxgb4_dcb_change_notify(struct notifier_block *, unsigned long, void *);

static struct notifier_block cxgb4_dcb_change = {
	.notifier_call = cxgb4_dcb_change_notify,
};
#endif

static struct scsi_transport_template *cxgb4i_stt;

/*
 * CPL (Chelsio Protocol Language) defines a message passing interface between
 * the host driver and Chelsio asic.
 * The section below implments CPLs that related to iscsi tcp connection
 * open/close/abort and data send/receive.
 */

#define RCV_BUFSIZ_MASK		0x3FFU
#define MAX_IMM_TX_PKT_LEN	256

static int push_tx_frames(struct cxgbi_sock *, int);

/*
 * is_ofld_imm - check whether a packet can be sent as immediate data
 * @skb: the packet
 *
 * Returns true if a packet can be sent as an offload WR with immediate
 * data.  We currently use the same limit as for Ethernet packets.
 */
static inline bool is_ofld_imm(const struct sk_buff *skb)
{
	int len = skb->len;

	if (likely(cxgbi_skcb_test_flag(skb, SKCBF_TX_NEED_HDR)))
		len += sizeof(struct fw_ofld_tx_data_wr);

	return len <= MAX_IMM_TX_PKT_LEN;
}

static void send_act_open_req(struct cxgbi_sock *csk, struct sk_buff *skb,
				struct l2t_entry *e)
{
	struct cxgb4_lld_info *lldi = cxgbi_cdev_priv(csk->cdev);
	int wscale = cxgbi_sock_compute_wscale(csk->mss_idx);
	unsigned long long opt0;
	unsigned int opt2;
	unsigned int qid_atid = ((unsigned int)csk->atid) |
				 (((unsigned int)csk->rss_qid) << 14);

	opt0 = KEEP_ALIVE_F |
		WND_SCALE_V(wscale) |
		MSS_IDX_V(csk->mss_idx) |
		L2T_IDX_V(((struct l2t_entry *)csk->l2t)->idx) |
		TX_CHAN_V(csk->tx_chan) |
		SMAC_SEL_V(csk->smac_idx) |
		ULP_MODE_V(ULP_MODE_ISCSI) |
		RCV_BUFSIZ_V(csk->rcv_win >> 10);

	opt2 = RX_CHANNEL_V(0) |
		RSS_QUEUE_VALID_F |
		RSS_QUEUE_V(csk->rss_qid);

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
		opt2 |= RX_FC_VALID_F;
		req->opt2 = cpu_to_be32(opt2);

		log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_SOCK,
			"csk t4 0x%p, %pI4:%u-%pI4:%u, atid %d, qid %u.\n",
			csk, &req->local_ip, ntohs(req->local_port),
			&req->peer_ip, ntohs(req->peer_port),
			csk->atid, csk->rss_qid);
	} else if (is_t5(lldi->adapter_type)) {
		struct cpl_t5_act_open_req *req =
				(struct cpl_t5_act_open_req *)skb->head;
		u32 isn = (prandom_u32() & ~7UL) - 1;

		INIT_TP_WR(req, 0);
		OPCODE_TID(req) = cpu_to_be32(MK_OPCODE_TID(CPL_ACT_OPEN_REQ,
					qid_atid));
		req->local_port = csk->saddr.sin_port;
		req->peer_port = csk->daddr.sin_port;
		req->local_ip = csk->saddr.sin_addr.s_addr;
		req->peer_ip = csk->daddr.sin_addr.s_addr;
		req->opt0 = cpu_to_be64(opt0);
		req->params = cpu_to_be64(FILTER_TUPLE_V(
				cxgb4_select_ntuple(
					csk->cdev->ports[csk->port_id],
					csk->l2t)));
		req->rsvd = cpu_to_be32(isn);
		opt2 |= T5_ISS_VALID;
		opt2 |= T5_OPT_2_VALID_F;

		req->opt2 = cpu_to_be32(opt2);

		log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_SOCK,
			"csk t5 0x%p, %pI4:%u-%pI4:%u, atid %d, qid %u.\n",
			csk, &req->local_ip, ntohs(req->local_port),
			&req->peer_ip, ntohs(req->peer_port),
			csk->atid, csk->rss_qid);
	} else {
		struct cpl_t6_act_open_req *req =
				(struct cpl_t6_act_open_req *)skb->head;
		u32 isn = (prandom_u32() & ~7UL) - 1;

		INIT_TP_WR(req, 0);
		OPCODE_TID(req) = cpu_to_be32(MK_OPCODE_TID(CPL_ACT_OPEN_REQ,
							    qid_atid));
		req->local_port = csk->saddr.sin_port;
		req->peer_port = csk->daddr.sin_port;
		req->local_ip = csk->saddr.sin_addr.s_addr;
		req->peer_ip = csk->daddr.sin_addr.s_addr;
		req->opt0 = cpu_to_be64(opt0);
		req->params = cpu_to_be64(FILTER_TUPLE_V(
				cxgb4_select_ntuple(
					csk->cdev->ports[csk->port_id],
					csk->l2t)));
		req->rsvd = cpu_to_be32(isn);

		opt2 |= T5_ISS_VALID;
		opt2 |= RX_FC_DISABLE_F;
		opt2 |= T5_OPT_2_VALID_F;

		req->opt2 = cpu_to_be32(opt2);
		req->rsvd2 = cpu_to_be32(0);
		req->opt3 = cpu_to_be32(0);

		log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_SOCK,
			  "csk t6 0x%p, %pI4:%u-%pI4:%u, atid %d, qid %u.\n",
			  csk, &req->local_ip, ntohs(req->local_port),
			  &req->peer_ip, ntohs(req->peer_port),
			  csk->atid, csk->rss_qid);
	}

	set_wr_txq(skb, CPL_PRIORITY_SETUP, csk->port_id);

	pr_info_ipaddr("t%d csk 0x%p,%u,0x%lx,%u, rss_qid %u.\n",
		       (&csk->saddr), (&csk->daddr),
		       CHELSIO_CHIP_VERSION(lldi->adapter_type), csk,
		       csk->state, csk->flags, csk->atid, csk->rss_qid);

	cxgb4_l2t_send(csk->cdev->ports[csk->port_id], skb, csk->l2t);
}

#if IS_ENABLED(CONFIG_IPV6)
static void send_act_open_req6(struct cxgbi_sock *csk, struct sk_buff *skb,
			       struct l2t_entry *e)
{
	struct cxgb4_lld_info *lldi = cxgbi_cdev_priv(csk->cdev);
	int wscale = cxgbi_sock_compute_wscale(csk->mss_idx);
	unsigned long long opt0;
	unsigned int opt2;
	unsigned int qid_atid = ((unsigned int)csk->atid) |
				 (((unsigned int)csk->rss_qid) << 14);

	opt0 = KEEP_ALIVE_F |
		WND_SCALE_V(wscale) |
		MSS_IDX_V(csk->mss_idx) |
		L2T_IDX_V(((struct l2t_entry *)csk->l2t)->idx) |
		TX_CHAN_V(csk->tx_chan) |
		SMAC_SEL_V(csk->smac_idx) |
		ULP_MODE_V(ULP_MODE_ISCSI) |
		RCV_BUFSIZ_V(csk->rcv_win >> 10);

	opt2 = RX_CHANNEL_V(0) |
		RSS_QUEUE_VALID_F |
		RSS_QUEUE_V(csk->rss_qid);

	if (is_t4(lldi->adapter_type)) {
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

		opt2 |= RX_FC_VALID_F;
		req->opt2 = cpu_to_be32(opt2);

		req->params = cpu_to_be32(cxgb4_select_ntuple(
					  csk->cdev->ports[csk->port_id],
					  csk->l2t));
	} else if (is_t5(lldi->adapter_type)) {
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

		opt2 |= T5_OPT_2_VALID_F;
		req->opt2 = cpu_to_be32(opt2);

		req->params = cpu_to_be64(FILTER_TUPLE_V(cxgb4_select_ntuple(
					  csk->cdev->ports[csk->port_id],
					  csk->l2t)));
	} else {
		struct cpl_t6_act_open_req6 *req =
				(struct cpl_t6_act_open_req6 *)skb->head;

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

		opt2 |= RX_FC_DISABLE_F;
		opt2 |= T5_OPT_2_VALID_F;

		req->opt2 = cpu_to_be32(opt2);

		req->params = cpu_to_be64(FILTER_TUPLE_V(cxgb4_select_ntuple(
					  csk->cdev->ports[csk->port_id],
					  csk->l2t)));

		req->rsvd2 = cpu_to_be32(0);
		req->opt3 = cpu_to_be32(0);
	}

	set_wr_txq(skb, CPL_PRIORITY_SETUP, csk->port_id);

	pr_info("t%d csk 0x%p,%u,0x%lx,%u, [%pI6]:%u-[%pI6]:%u, rss_qid %u.\n",
		CHELSIO_CHIP_VERSION(lldi->adapter_type), csk, csk->state,
		csk->flags, csk->atid,
		&csk->saddr6.sin6_addr, ntohs(csk->saddr.sin_port),
		&csk->daddr6.sin6_addr, ntohs(csk->daddr.sin_port),
		csk->rss_qid);

	cxgb4_l2t_send(csk->cdev->ports[csk->port_id], skb, csk->l2t);
}
#endif

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

	if (!cxgbi_sock_flag(csk, CTPF_TX_DATA_SENT)) {
		send_tx_flowc_wr(csk);
		cxgbi_sock_set_flag(csk, CTPF_TX_DATA_SENT);
	}

	cxgbi_sock_set_state(csk, CTP_ABORTING);
	cxgbi_sock_set_flag(csk, CTPF_ABORT_RPL_PENDING);
	cxgbi_sock_purge_write_queue(csk);

	csk->cpl_abort_req = NULL;
	req = (struct cpl_abort_req *)skb->head;
	set_wr_txq(skb, CPL_PRIORITY_DATA, csk->port_id);
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
	set_wr_txq(skb, CPL_PRIORITY_DATA, csk->port_id);
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
	req->credit_dack = cpu_to_be32(RX_CREDITS_V(credits)
				       | RX_FORCE_ACK_F);
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

#define FLOWC_WR_NPARAMS_MIN	9
static inline int tx_flowc_wr_credits(int *nparamsp, int *flowclenp)
{
	int nparams, flowclen16, flowclen;

	nparams = FLOWC_WR_NPARAMS_MIN;
#ifdef CONFIG_CHELSIO_T4_DCB
	nparams++;
#endif
	flowclen = offsetof(struct fw_flowc_wr, mnemval[nparams]);
	flowclen16 = DIV_ROUND_UP(flowclen, 16);
	flowclen = flowclen16 * 16;
	/*
	 * Return the number of 16-byte credits used by the FlowC request.
	 * Pass back the nparams and actual FlowC length if requested.
	 */
	if (nparamsp)
		*nparamsp = nparams;
	if (flowclenp)
		*flowclenp = flowclen;

	return flowclen16;
}

static inline int send_tx_flowc_wr(struct cxgbi_sock *csk)
{
	struct sk_buff *skb;
	struct fw_flowc_wr *flowc;
	int nparams, flowclen16, flowclen;

#ifdef CONFIG_CHELSIO_T4_DCB
	u16 vlan = ((struct l2t_entry *)csk->l2t)->vlan;
#endif
	flowclen16 = tx_flowc_wr_credits(&nparams, &flowclen);
	skb = alloc_wr(flowclen, 0, GFP_ATOMIC);
	flowc = (struct fw_flowc_wr *)skb->head;
	flowc->op_to_nparams =
		htonl(FW_WR_OP_V(FW_FLOWC_WR) | FW_FLOWC_WR_NPARAMS_V(nparams));
	flowc->flowid_len16 =
		htonl(FW_WR_LEN16_V(flowclen16) | FW_WR_FLOWID_V(csk->tid));
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
	flowc->mnemval[6].val = htonl(csk->snd_win);
	flowc->mnemval[7].mnemonic = FW_FLOWC_MNEM_MSS;
	flowc->mnemval[7].val = htonl(csk->advmss);
	flowc->mnemval[8].mnemonic = 0;
	flowc->mnemval[8].val = 0;
	flowc->mnemval[8].mnemonic = FW_FLOWC_MNEM_TXDATAPLEN_MAX;
	flowc->mnemval[8].val = 16384;
#ifdef CONFIG_CHELSIO_T4_DCB
	flowc->mnemval[9].mnemonic = FW_FLOWC_MNEM_DCBPRIO;
	if (vlan == CPL_L2T_VLAN_NONE) {
		pr_warn_ratelimited("csk %u without VLAN Tag on DCB Link\n",
				    csk->tid);
		flowc->mnemval[9].val = cpu_to_be32(0);
	} else {
		flowc->mnemval[9].val = cpu_to_be32((vlan & VLAN_PRIO_MASK) >>
					VLAN_PRIO_SHIFT);
	}
#endif

	set_wr_txq(skb, CPL_PRIORITY_DATA, csk->port_id);

	log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_SOCK,
		"csk 0x%p, tid 0x%x, %u,%u,%u,%u,%u,%u,%u.\n",
		csk, csk->tid, 0, csk->tx_chan, csk->rss_qid,
		csk->snd_nxt, csk->rcv_nxt, csk->snd_win,
		csk->advmss);

	cxgb4_ofld_send(csk->cdev->ports[csk->port_id], skb);

	return flowclen16;
}

static inline void make_tx_data_wr(struct cxgbi_sock *csk, struct sk_buff *skb,
				   int dlen, int len, u32 credits, int compl)
{
	struct fw_ofld_tx_data_wr *req;
	unsigned int submode = cxgbi_skcb_ulp_mode(skb) & 3;
	unsigned int wr_ulp_mode = 0, val;
	bool imm = is_ofld_imm(skb);

	req = __skb_push(skb, sizeof(*req));

	if (imm) {
		req->op_to_immdlen = htonl(FW_WR_OP_V(FW_OFLD_TX_DATA_WR) |
					FW_WR_COMPL_F |
					FW_WR_IMMDLEN_V(dlen));
		req->flowid_len16 = htonl(FW_WR_FLOWID_V(csk->tid) |
						FW_WR_LEN16_V(credits));
	} else {
		req->op_to_immdlen =
			cpu_to_be32(FW_WR_OP_V(FW_OFLD_TX_DATA_WR) |
					FW_WR_COMPL_F |
					FW_WR_IMMDLEN_V(0));
		req->flowid_len16 =
			cpu_to_be32(FW_WR_FLOWID_V(csk->tid) |
					FW_WR_LEN16_V(credits));
	}
	if (submode)
		wr_ulp_mode = FW_OFLD_TX_DATA_WR_ULPMODE_V(ULP2_MODE_ISCSI) |
				FW_OFLD_TX_DATA_WR_ULPSUBMODE_V(submode);
	val = skb_peek(&csk->write_queue) ? 0 : 1;
	req->tunnel_to_proxy = htonl(wr_ulp_mode |
				     FW_OFLD_TX_DATA_WR_SHOVE_V(val));
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
		int flowclen16 = 0;

		skb_reset_transport_header(skb);
		if (is_ofld_imm(skb))
			credits_needed = DIV_ROUND_UP(dlen, 16);
		else
			credits_needed = DIV_ROUND_UP(
						8 * calc_tx_flits_ofld(skb),
						16);

		if (likely(cxgbi_skcb_test_flag(skb, SKCBF_TX_NEED_HDR)))
			credits_needed += DIV_ROUND_UP(
					sizeof(struct fw_ofld_tx_data_wr),
					16);

		/*
		 * Assumes the initial credits is large enough to support
		 * fw_flowc_wr plus largest possible first payload
		 */
		if (!cxgbi_sock_flag(csk, CTPF_TX_DATA_SENT)) {
			flowclen16 = send_tx_flowc_wr(csk);
			csk->wr_cred -= flowclen16;
			csk->wr_una_cred += flowclen16;
			cxgbi_sock_set_flag(csk, CTPF_TX_DATA_SENT);
		}

		if (csk->wr_cred < credits_needed) {
			log_debug(1 << CXGBI_DBG_PDU_TX,
				"csk 0x%p, skb %u/%u, wr %d < %u.\n",
				csk, skb->len, skb->data_len,
				credits_needed, csk->wr_cred);
			break;
		}
		__skb_unlink(skb, &csk->write_queue);
		set_wr_txq(skb, CPL_PRIORITY_DATA, csk->port_id);
		skb->csum = credits_needed + flowclen16;
		csk->wr_cred -= credits_needed;
		csk->wr_una_cred += credits_needed;
		cxgbi_sock_enqueue_wr(csk, skb);

		log_debug(1 << CXGBI_DBG_PDU_TX,
			"csk 0x%p, skb %u/%u, wr %d, left %u, unack %u.\n",
			csk, skb->len, skb->data_len, credits_needed,
			csk->wr_cred, csk->wr_una_cred);

		if (likely(cxgbi_skcb_test_flag(skb, SKCBF_TX_NEED_HDR))) {
			len += cxgbi_ulp_extra_len(cxgbi_skcb_ulp_mode(skb));
			make_tx_data_wr(csk, skb, dlen, len, credits_needed,
					req_completion);
			csk->snd_nxt += len;
			cxgbi_skcb_clear_flag(skb, SKCBF_TX_NEED_HDR);
		} else if (cxgbi_skcb_test_flag(skb, SKCBF_TX_FLAG_COMPL) &&
			   (csk->wr_una_cred >= (csk->wr_max_cred / 2))) {
			struct cpl_close_con_req *req =
				(struct cpl_close_con_req *)skb->data;
			req->wr.wr_hi |= htonl(FW_WR_COMPL_F);
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
	unsigned int atid = TID_TID_G(ntohl(req->tos_atid));
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

	module_put(cdev->owner);

	cxgbi_sock_get(csk);
	csk->tid = tid;
	cxgb4_insert_tid(lldi->tids, csk, tid, csk->csk_family);
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
	if (csk->rcv_win > (RCV_BUFSIZ_MASK << 10))
		csk->rcv_wup -= csk->rcv_win - (RCV_BUFSIZ_MASK << 10);

	csk->advmss = lldi->mtus[TCPOPT_MSS_G(tcp_opt)] - 40;
	if (TCPOPT_TSTAMP_G(tcp_opt))
		csk->advmss -= 12;
	if (csk->advmss < 128)
		csk->advmss = 128;

	log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_SOCK,
		"csk 0x%p, mss_idx %u, advmss %u.\n",
			csk, TCPOPT_MSS_G(tcp_opt), csk->advmss);

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

static void csk_act_open_retry_timer(struct timer_list *t)
{
	struct sk_buff *skb = NULL;
	struct cxgbi_sock *csk = from_timer(csk, t, retry_timer);
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
#if IS_ENABLED(CONFIG_IPV6)
	} else {
		send_act_open_func = send_act_open_req6;
		skb = alloc_wr(size6, 0, GFP_ATOMIC);
#endif
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

static inline bool is_neg_adv(unsigned int status)
{
	return status == CPL_ERR_RTX_NEG_ADVICE ||
		status == CPL_ERR_KEEPALV_NEG_ADVICE ||
		status == CPL_ERR_PERSIST_NEG_ADVICE;
}

static void do_act_open_rpl(struct cxgbi_device *cdev, struct sk_buff *skb)
{
	struct cxgbi_sock *csk;
	struct cpl_act_open_rpl *rpl = (struct cpl_act_open_rpl *)skb->data;
	unsigned int tid = GET_TID(rpl);
	unsigned int atid =
		TID_TID_G(AOPEN_ATID_G(be32_to_cpu(rpl->atid_status)));
	unsigned int status = AOPEN_STATUS_G(be32_to_cpu(rpl->atid_status));
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

	if (is_neg_adv(status))
		goto rel_skb;

	module_put(cdev->owner);

	if (status && status != CPL_ERR_TCAM_FULL &&
	    status != CPL_ERR_CONN_EXIST &&
	    status != CPL_ERR_ARP_MISS)
		cxgb4_remove_tid(lldi->tids, csk->port_id, GET_TID(rpl),
				 csk->csk_family);

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

	if (is_neg_adv(req->status))
		goto rel_skb;

	cxgbi_sock_get(csk);
	spin_lock_bh(&csk->lock);

	cxgbi_sock_clear_flag(csk, CTPF_ABORT_REQ_RCVD);

	if (!cxgbi_sock_flag(csk, CTPF_TX_DATA_SENT)) {
		send_tx_flowc_wr(csk);
		cxgbi_sock_set_flag(csk, CTPF_TX_DATA_SENT);
	}

	cxgbi_sock_set_flag(csk, CTPF_ABORT_REQ_RCVD);
	cxgbi_sock_set_state(csk, CTP_ABORTING);

	send_abort_rpl(csk, rst_status);

	if (!cxgbi_sock_flag(csk, CTPF_ABORT_RPL_PENDING)) {
		csk->err = abort_status_to_errno(csk, req->status, &rst_status);
		cxgbi_sock_closed(csk);
	}

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

	pr_info_ipaddr("csk 0x%p,%u,0x%lx,%u, status %u.\n",
		       (&csk->saddr), (&csk->daddr), csk,
		       csk->state, csk->flags, csk->tid, rpl->status);

	if (rpl->status == CPL_ERR_ABORT_FAILED)
		goto rel_skb;

	cxgbi_sock_rcv_abort_rpl(csk);
rel_skb:
	__kfree_skb(skb);
}

static void do_rx_data(struct cxgbi_device *cdev, struct sk_buff *skb)
{
	struct cxgbi_sock *csk;
	struct cpl_rx_data *cpl = (struct cpl_rx_data *)skb->data;
	unsigned int tid = GET_TID(cpl);
	struct cxgb4_lld_info *lldi = cxgbi_cdev_priv(cdev);
	struct tid_info *t = lldi->tids;

	csk = lookup_tid(t, tid);
	if (!csk) {
		pr_err("can't find connection for tid %u.\n", tid);
	} else {
		/* not expecting this, reset the connection. */
		pr_err("csk 0x%p, tid %u, rcv cpl_rx_data.\n", csk, tid);
		spin_lock_bh(&csk->lock);
		send_abort_req(csk);
		spin_unlock_bh(&csk->lock);
	}
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

		if ((CHELSIO_CHIP_VERSION(lldi->adapter_type) <= CHELSIO_T5) &&
		    (cxgbi_skcb_tcp_seq(skb) != csk->rcv_nxt)) {
			pr_info("tid %u, CPL_ISCSI_HDR, bad seq, 0x%x/0x%x.\n",
				csk->tid, cxgbi_skcb_tcp_seq(skb),
				csk->rcv_nxt);
			goto abort_conn;
		}

		bhs = skb->data;
		hlen = ntohs(cpl->len);
		dlen = ntohl(*(unsigned int *)(bhs + 4)) & 0xFFFFFF;

		plen = ISCSI_PDU_LEN_G(pdu_len_ddp);
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

static void do_rx_iscsi_data(struct cxgbi_device *cdev, struct sk_buff *skb)
{
	struct cxgbi_sock *csk;
	struct cpl_iscsi_hdr *cpl = (struct cpl_iscsi_hdr *)skb->data;
	struct cxgb4_lld_info *lldi = cxgbi_cdev_priv(cdev);
	struct tid_info *t = lldi->tids;
	struct sk_buff *lskb;
	u32 tid = GET_TID(cpl);
	u16 pdu_len_ddp = be16_to_cpu(cpl->pdu_len_ddp);

	csk = lookup_tid(t, tid);
	if (unlikely(!csk)) {
		pr_err("can't find conn. for tid %u.\n", tid);
		goto rel_skb;
	}

	log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_PDU_RX,
		  "csk 0x%p,%u,0x%lx, tid %u, skb 0x%p,%u, 0x%x.\n",
		  csk, csk->state, csk->flags, csk->tid, skb,
		  skb->len, pdu_len_ddp);

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

	cxgbi_skcb_tcp_seq(skb) = be32_to_cpu(cpl->seq);
	cxgbi_skcb_flags(skb) = 0;

	skb_reset_transport_header(skb);
	__skb_pull(skb, sizeof(*cpl));
	__pskb_trim(skb, ntohs(cpl->len));

	if (!csk->skb_ulp_lhdr)
		csk->skb_ulp_lhdr = skb;

	lskb = csk->skb_ulp_lhdr;
	cxgbi_skcb_set_flag(lskb, SKCBF_RX_DATA);

	log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_PDU_RX,
		  "csk 0x%p,%u,0x%lx, skb 0x%p data, 0x%p.\n",
		  csk, csk->state, csk->flags, skb, lskb);

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

static void
cxgb4i_process_ddpvld(struct cxgbi_sock *csk,
		      struct sk_buff *skb, u32 ddpvld)
{
	if (ddpvld & (1 << CPL_RX_DDP_STATUS_HCRC_SHIFT)) {
		pr_info("csk 0x%p, lhdr 0x%p, status 0x%x, hcrc bad 0x%lx.\n",
			csk, skb, ddpvld, cxgbi_skcb_flags(skb));
		cxgbi_skcb_set_flag(skb, SKCBF_RX_HCRC_ERR);
	}

	if (ddpvld & (1 << CPL_RX_DDP_STATUS_DCRC_SHIFT)) {
		pr_info("csk 0x%p, lhdr 0x%p, status 0x%x, dcrc bad 0x%lx.\n",
			csk, skb, ddpvld, cxgbi_skcb_flags(skb));
		cxgbi_skcb_set_flag(skb, SKCBF_RX_DCRC_ERR);
	}

	if (ddpvld & (1 << CPL_RX_DDP_STATUS_PAD_SHIFT)) {
		log_debug(1 << CXGBI_DBG_PDU_RX,
			  "csk 0x%p, lhdr 0x%p, status 0x%x, pad bad.\n",
			  csk, skb, ddpvld);
		cxgbi_skcb_set_flag(skb, SKCBF_RX_PAD_ERR);
	}

	if ((ddpvld & (1 << CPL_RX_DDP_STATUS_DDP_SHIFT)) &&
	    !cxgbi_skcb_test_flag(skb, SKCBF_RX_DATA)) {
		log_debug(1 << CXGBI_DBG_PDU_RX,
			  "csk 0x%p, lhdr 0x%p, 0x%x, data ddp'ed.\n",
			  csk, skb, ddpvld);
		cxgbi_skcb_set_flag(skb, SKCBF_RX_DATA_DDPD);
	}
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
	u32 ddpvld = be32_to_cpu(rpl->ddpvld);

	csk = lookup_tid(t, tid);
	if (unlikely(!csk)) {
		pr_err("can't find connection for tid %u.\n", tid);
		goto rel_skb;
	}

	log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_PDU_RX,
		"csk 0x%p,%u,0x%lx, skb 0x%p,0x%x, lhdr 0x%p.\n",
		csk, csk->state, csk->flags, skb, ddpvld, csk->skb_ulp_lhdr);

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

	cxgb4i_process_ddpvld(csk, lskb, ddpvld);

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

static void
do_rx_iscsi_cmp(struct cxgbi_device *cdev, struct sk_buff *skb)
{
	struct cxgbi_sock *csk;
	struct cpl_rx_iscsi_cmp *rpl = (struct cpl_rx_iscsi_cmp *)skb->data;
	struct cxgb4_lld_info *lldi = cxgbi_cdev_priv(cdev);
	struct tid_info *t = lldi->tids;
	struct sk_buff *data_skb = NULL;
	u32 tid = GET_TID(rpl);
	u32 ddpvld = be32_to_cpu(rpl->ddpvld);
	u32 seq = be32_to_cpu(rpl->seq);
	u16 pdu_len_ddp = be16_to_cpu(rpl->pdu_len_ddp);

	csk = lookup_tid(t, tid);
	if (unlikely(!csk)) {
		pr_err("can't find connection for tid %u.\n", tid);
		goto rel_skb;
	}

	log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_PDU_RX,
		  "csk 0x%p,%u,0x%lx, skb 0x%p,0x%x, lhdr 0x%p, len %u, "
		  "pdu_len_ddp %u, status %u.\n",
		  csk, csk->state, csk->flags, skb, ddpvld, csk->skb_ulp_lhdr,
		  ntohs(rpl->len), pdu_len_ddp,  rpl->status);

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

	cxgbi_skcb_tcp_seq(skb) = seq;
	cxgbi_skcb_flags(skb) = 0;
	cxgbi_skcb_rx_pdulen(skb) = 0;

	skb_reset_transport_header(skb);
	__skb_pull(skb, sizeof(*rpl));
	__pskb_trim(skb, be16_to_cpu(rpl->len));

	csk->rcv_nxt = seq + pdu_len_ddp;

	if (csk->skb_ulp_lhdr) {
		data_skb = skb_peek(&csk->receive_queue);
		if (!data_skb ||
		    !cxgbi_skcb_test_flag(data_skb, SKCBF_RX_DATA)) {
			pr_err("Error! freelist data not found 0x%p, tid %u\n",
			       data_skb, tid);

			goto abort_conn;
		}
		__skb_unlink(data_skb, &csk->receive_queue);

		cxgbi_skcb_set_flag(skb, SKCBF_RX_DATA);

		__skb_queue_tail(&csk->receive_queue, skb);
		__skb_queue_tail(&csk->receive_queue, data_skb);
	} else {
		 __skb_queue_tail(&csk->receive_queue, skb);
	}

	csk->skb_ulp_lhdr = NULL;

	cxgbi_skcb_set_flag(skb, SKCBF_RX_HDR);
	cxgbi_skcb_set_flag(skb, SKCBF_RX_STATUS);
	cxgbi_skcb_set_flag(skb, SKCBF_RX_ISCSI_COMPL);
	cxgbi_skcb_rx_ddigest(skb) = be32_to_cpu(rpl->ulp_crc);

	cxgb4i_process_ddpvld(csk, skb, ddpvld);

	log_debug(1 << CXGBI_DBG_PDU_RX, "csk 0x%p, skb 0x%p, f 0x%lx.\n",
		  csk, skb, cxgbi_skcb_flags(skb));

	cxgbi_conn_pdu_ready(csk);
	spin_unlock_bh(&csk->lock);

	return;

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
	if (!csk) {
		pr_err("can't find conn. for tid %u.\n", tid);
		return;
	}

	log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_SOCK,
		"csk 0x%p,%u,%lx,%u, status 0x%x.\n",
		csk, csk->state, csk->flags, csk->tid, rpl->status);

	if (rpl->status != CPL_ERR_NONE) {
		pr_err("csk 0x%p,%u, SET_TCB_RPL status %u.\n",
			csk, tid, rpl->status);
		csk->err = -EINVAL;
	}

	complete(&csk->cmpl);

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
#if IS_ENABLED(CONFIG_IPV6)
	struct net_device *ndev = csk->cdev->ports[csk->port_id];
#endif

	log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_SOCK,
		"csk 0x%p,%u,0x%lx,%u.\n",
		csk, csk->state, csk->flags, csk->tid);

	cxgbi_sock_free_cpl_skbs(csk);
	cxgbi_sock_purge_write_queue(csk);
	if (csk->wr_cred != csk->wr_max_cred) {
		cxgbi_sock_purge_wr_queue(csk);
		cxgbi_sock_reset_wr_list(csk);
	}

	l2t_put(csk);
#if IS_ENABLED(CONFIG_IPV6)
	if (csk->csk_family == AF_INET6)
		cxgb4_clip_release(ndev,
				   (const u32 *)&csk->saddr6.sin6_addr, 1);
#endif

	if (cxgbi_sock_flag(csk, CTPF_HAS_ATID))
		free_atid(csk);
	else if (cxgbi_sock_flag(csk, CTPF_HAS_TID)) {
		lldi = cxgbi_cdev_priv(csk->cdev);
		cxgb4_remove_tid(lldi->tids, 0, csk->tid,
				 csk->csk_family);
		cxgbi_sock_clear_flag(csk, CTPF_HAS_TID);
		cxgbi_sock_put(csk);
	}
	csk->dst = NULL;
}

#ifdef CONFIG_CHELSIO_T4_DCB
static inline u8 get_iscsi_dcb_state(struct net_device *ndev)
{
	return ndev->dcbnl_ops->getstate(ndev);
}

static int select_priority(int pri_mask)
{
	if (!pri_mask)
		return 0;
	return (ffs(pri_mask) - 1);
}

static u8 get_iscsi_dcb_priority(struct net_device *ndev)
{
	int rv;
	u8 caps;

	struct dcb_app iscsi_dcb_app = {
		.protocol = 3260
	};

	rv = (int)ndev->dcbnl_ops->getcap(ndev, DCB_CAP_ATTR_DCBX, &caps);
	if (rv)
		return 0;

	if (caps & DCB_CAP_DCBX_VER_IEEE) {
		iscsi_dcb_app.selector = IEEE_8021QAZ_APP_SEL_STREAM;
		rv = dcb_ieee_getapp_mask(ndev, &iscsi_dcb_app);
		if (!rv) {
			iscsi_dcb_app.selector = IEEE_8021QAZ_APP_SEL_ANY;
			rv = dcb_ieee_getapp_mask(ndev, &iscsi_dcb_app);
		}
	} else if (caps & DCB_CAP_DCBX_VER_CEE) {
		iscsi_dcb_app.selector = DCB_APP_IDTYPE_PORTNUM;
		rv = dcb_getapp(ndev, &iscsi_dcb_app);
	}

	log_debug(1 << CXGBI_DBG_ISCSI,
		  "iSCSI priority is set to %u\n", select_priority(rv));
	return select_priority(rv);
}
#endif

static int init_act_open(struct cxgbi_sock *csk)
{
	struct cxgbi_device *cdev = csk->cdev;
	struct cxgb4_lld_info *lldi = cxgbi_cdev_priv(cdev);
	struct net_device *ndev = cdev->ports[csk->port_id];
	struct sk_buff *skb = NULL;
	struct neighbour *n = NULL;
	void *daddr;
	unsigned int step;
	unsigned int rxq_idx;
	unsigned int size, size6;
	unsigned int linkspeed;
	unsigned int rcv_winf, snd_winf;
#ifdef CONFIG_CHELSIO_T4_DCB
	u8 priority = 0;
#endif
	log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_SOCK,
		"csk 0x%p,%u,0x%lx,%u.\n",
		csk, csk->state, csk->flags, csk->tid);

	if (csk->csk_family == AF_INET)
		daddr = &csk->daddr.sin_addr.s_addr;
#if IS_ENABLED(CONFIG_IPV6)
	else if (csk->csk_family == AF_INET6)
		daddr = &csk->daddr6.sin6_addr;
#endif
	else {
		pr_err("address family 0x%x not supported\n", csk->csk_family);
		goto rel_resource;
	}

	n = dst_neigh_lookup(csk->dst, daddr);

	if (!n) {
		pr_err("%s, can't get neighbour of csk->dst.\n", ndev->name);
		goto rel_resource;
	}

	if (!(n->nud_state & NUD_VALID))
		neigh_event_send(n, NULL);

	csk->atid = cxgb4_alloc_atid(lldi->tids, csk);
	if (csk->atid < 0) {
		pr_err("%s, NO atid available.\n", ndev->name);
		goto rel_resource_without_clip;
	}
	cxgbi_sock_set_flag(csk, CTPF_HAS_ATID);
	cxgbi_sock_get(csk);

#ifdef CONFIG_CHELSIO_T4_DCB
	if (get_iscsi_dcb_state(ndev))
		priority = get_iscsi_dcb_priority(ndev);

	csk->dcb_priority = priority;
	csk->l2t = cxgb4_l2t_get(lldi->l2t, n, ndev, priority);
#else
	csk->l2t = cxgb4_l2t_get(lldi->l2t, n, ndev, 0);
#endif
	if (!csk->l2t) {
		pr_err("%s, cannot alloc l2t.\n", ndev->name);
		goto rel_resource_without_clip;
	}
	cxgbi_sock_get(csk);

#if IS_ENABLED(CONFIG_IPV6)
	if (csk->csk_family == AF_INET6)
		cxgb4_clip_get(ndev, (const u32 *)&csk->saddr6.sin6_addr, 1);
#endif

	if (is_t4(lldi->adapter_type)) {
		size = sizeof(struct cpl_act_open_req);
		size6 = sizeof(struct cpl_act_open_req6);
	} else if (is_t5(lldi->adapter_type)) {
		size = sizeof(struct cpl_t5_act_open_req);
		size6 = sizeof(struct cpl_t5_act_open_req6);
	} else {
		size = sizeof(struct cpl_t6_act_open_req);
		size6 = sizeof(struct cpl_t6_act_open_req6);
	}

	if (csk->csk_family == AF_INET)
		skb = alloc_wr(size, 0, GFP_NOIO);
#if IS_ENABLED(CONFIG_IPV6)
	else
		skb = alloc_wr(size6, 0, GFP_NOIO);
#endif

	if (!skb)
		goto rel_resource;
	skb->sk = (struct sock *)csk;
	t4_set_arp_err_handler(skb, csk, cxgbi_sock_act_open_req_arp_failure);

	if (!csk->mtu)
		csk->mtu = dst_mtu(csk->dst);
	cxgb4_best_mtu(lldi->mtus, csk->mtu, &csk->mss_idx);
	csk->tx_chan = cxgb4_port_chan(ndev);
	csk->smac_idx = ((struct port_info *)netdev_priv(ndev))->smt_idx;
	step = lldi->ntxq / lldi->nchan;
	csk->txq_idx = cxgb4_port_idx(ndev) * step;
	step = lldi->nrxq / lldi->nchan;
	rxq_idx = (cxgb4_port_idx(ndev) * step) + (cdev->rxq_idx_cntr % step);
	cdev->rxq_idx_cntr++;
	csk->rss_qid = lldi->rxq_ids[rxq_idx];
	linkspeed = ((struct port_info *)netdev_priv(ndev))->link_cfg.speed;
	csk->snd_win = cxgb4i_snd_win;
	csk->rcv_win = cxgb4i_rcv_win;
	if (cxgb4i_rcv_win <= 0) {
		csk->rcv_win = CXGB4I_DEFAULT_10G_RCV_WIN;
		rcv_winf = linkspeed / SPEED_10000;
		if (rcv_winf)
			csk->rcv_win *= rcv_winf;
	}
	if (cxgb4i_snd_win <= 0) {
		csk->snd_win = CXGB4I_DEFAULT_10G_SND_WIN;
		snd_winf = linkspeed / SPEED_10000;
		if (snd_winf)
			csk->snd_win *= snd_winf;
	}
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
	if (!try_module_get(cdev->owner)) {
		pr_err("%s, try_module_get failed.\n", ndev->name);
		goto rel_resource;
	}

	cxgbi_sock_set_state(csk, CTP_ACTIVE_OPEN);
	if (csk->csk_family == AF_INET)
		send_act_open_req(csk, skb, csk->l2t);
#if IS_ENABLED(CONFIG_IPV6)
	else
		send_act_open_req6(csk, skb, csk->l2t);
#endif
	neigh_release(n);

	return 0;

rel_resource:
#if IS_ENABLED(CONFIG_IPV6)
	if (csk->csk_family == AF_INET6)
		cxgb4_clip_release(ndev,
				   (const u32 *)&csk->saddr6.sin6_addr, 1);
#endif
rel_resource_without_clip:
	if (n)
		neigh_release(n);
	if (skb)
		__kfree_skb(skb);
	return -EINVAL;
}

static cxgb4i_cplhandler_func cxgb4i_cplhandlers[NUM_CPL_CMDS] = {
	[CPL_ACT_ESTABLISH] = do_act_establish,
	[CPL_ACT_OPEN_RPL] = do_act_open_rpl,
	[CPL_PEER_CLOSE] = do_peer_close,
	[CPL_ABORT_REQ_RSS] = do_abort_req_rss,
	[CPL_ABORT_RPL_RSS] = do_abort_rpl_rss,
	[CPL_CLOSE_CON_RPL] = do_close_con_rpl,
	[CPL_FW4_ACK] = do_fw4_ack,
	[CPL_ISCSI_HDR] = do_rx_iscsi_hdr,
	[CPL_ISCSI_DATA] = do_rx_iscsi_data,
	[CPL_SET_TCB_RPL] = do_set_tcb_rpl,
	[CPL_RX_DATA_DDP] = do_rx_data_ddp,
	[CPL_RX_ISCSI_DDP] = do_rx_data_ddp,
	[CPL_RX_ISCSI_CMP] = do_rx_iscsi_cmp,
	[CPL_RX_DATA] = do_rx_data,
};

static int cxgb4i_ofld_init(struct cxgbi_device *cdev)
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

static inline void
ulp_mem_io_set_hdr(struct cxgbi_device *cdev,
		   struct ulp_mem_io *req,
		   unsigned int wr_len, unsigned int dlen,
		   unsigned int pm_addr,
		   int tid)
{
	struct cxgb4_lld_info *lldi = cxgbi_cdev_priv(cdev);
	struct ulptx_idata *idata = (struct ulptx_idata *)(req + 1);

	INIT_ULPTX_WR(req, wr_len, 0, tid);
	req->wr.wr_hi = htonl(FW_WR_OP_V(FW_ULPTX_WR) |
		FW_WR_ATOMIC_V(0));
	req->cmd = htonl(ULPTX_CMD_V(ULP_TX_MEM_WRITE) |
		ULP_MEMIO_ORDER_V(is_t4(lldi->adapter_type)) |
		T5_ULP_MEMIO_IMM_V(!is_t4(lldi->adapter_type)));
	req->dlen = htonl(ULP_MEMIO_DATA_LEN_V(dlen >> 5));
	req->lock_addr = htonl(ULP_MEMIO_ADDR_V(pm_addr >> 5));
	req->len16 = htonl(DIV_ROUND_UP(wr_len - sizeof(req->wr), 16));

	idata->cmd_more = htonl(ULPTX_CMD_V(ULP_TX_SC_IMM));
	idata->len = htonl(dlen);
}

static struct sk_buff *
ddp_ppod_init_idata(struct cxgbi_device *cdev,
		    struct cxgbi_ppm *ppm,
		    unsigned int idx, unsigned int npods,
		    unsigned int tid)
{
	unsigned int pm_addr = (idx << PPOD_SIZE_SHIFT) + ppm->llimit;
	unsigned int dlen = npods << PPOD_SIZE_SHIFT;
	unsigned int wr_len = roundup(sizeof(struct ulp_mem_io) +
				sizeof(struct ulptx_idata) + dlen, 16);
	struct sk_buff *skb = alloc_wr(wr_len, 0, GFP_ATOMIC);

	if (!skb) {
		pr_err("%s: %s idx %u, npods %u, OOM.\n",
		       __func__, ppm->ndev->name, idx, npods);
		return NULL;
	}

	ulp_mem_io_set_hdr(cdev, (struct ulp_mem_io *)skb->head, wr_len, dlen,
			   pm_addr, tid);

	return skb;
}

static int ddp_ppod_write_idata(struct cxgbi_ppm *ppm, struct cxgbi_sock *csk,
				struct cxgbi_task_tag_info *ttinfo,
				unsigned int idx, unsigned int npods,
				struct scatterlist **sg_pp,
				unsigned int *sg_off)
{
	struct cxgbi_device *cdev = csk->cdev;
	struct sk_buff *skb = ddp_ppod_init_idata(cdev, ppm, idx, npods,
						  csk->tid);
	struct ulp_mem_io *req;
	struct ulptx_idata *idata;
	struct cxgbi_pagepod *ppod;
	int i;

	if (!skb)
		return -ENOMEM;

	req = (struct ulp_mem_io *)skb->head;
	idata = (struct ulptx_idata *)(req + 1);
	ppod = (struct cxgbi_pagepod *)(idata + 1);

	for (i = 0; i < npods; i++, ppod++)
		cxgbi_ddp_set_one_ppod(ppod, ttinfo, sg_pp, sg_off);

	cxgbi_skcb_set_flag(skb, SKCBF_TX_MEM_WRITE);
	cxgbi_skcb_set_flag(skb, SKCBF_TX_FLAG_COMPL);
	set_wr_txq(skb, CPL_PRIORITY_DATA, csk->port_id);

	spin_lock_bh(&csk->lock);
	cxgbi_sock_skb_entail(csk, skb);
	spin_unlock_bh(&csk->lock);

	return 0;
}

static int ddp_set_map(struct cxgbi_ppm *ppm, struct cxgbi_sock *csk,
		       struct cxgbi_task_tag_info *ttinfo)
{
	unsigned int pidx = ttinfo->idx;
	unsigned int npods = ttinfo->npods;
	unsigned int i, cnt;
	int err = 0;
	struct scatterlist *sg = ttinfo->sgl;
	unsigned int offset = 0;

	ttinfo->cid = csk->port_id;

	for (i = 0; i < npods; i += cnt, pidx += cnt) {
		cnt = npods - i;

		if (cnt > ULPMEM_IDATA_MAX_NPPODS)
			cnt = ULPMEM_IDATA_MAX_NPPODS;
		err = ddp_ppod_write_idata(ppm, csk, ttinfo, pidx, cnt,
					   &sg, &offset);
		if (err < 0)
			break;
	}

	return err;
}

static int ddp_setup_conn_pgidx(struct cxgbi_sock *csk, unsigned int tid,
				int pg_idx)
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
	req->reply_ctrl = htons(NO_REPLY_V(0) | QUEUENO_V(csk->rss_qid));
	req->word_cookie = htons(0);
	req->mask = cpu_to_be64(0x3 << 8);
	req->val = cpu_to_be64(pg_idx << 8);
	set_wr_txq(skb, CPL_PRIORITY_CONTROL, csk->port_id);

	log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_SOCK,
		"csk 0x%p, tid 0x%x, pg_idx %u.\n", csk, csk->tid, pg_idx);

	reinit_completion(&csk->cmpl);
	cxgb4_ofld_send(csk->cdev->ports[csk->port_id], skb);
	wait_for_completion(&csk->cmpl);

	return csk->err;
}

static int ddp_setup_conn_digest(struct cxgbi_sock *csk, unsigned int tid,
				 int hcrc, int dcrc)
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
	req->reply_ctrl = htons(NO_REPLY_V(0) | QUEUENO_V(csk->rss_qid));
	req->word_cookie = htons(0);
	req->mask = cpu_to_be64(0x3 << 4);
	req->val = cpu_to_be64(((hcrc ? ULP_CRC_HEADER : 0) |
				(dcrc ? ULP_CRC_DATA : 0)) << 4);
	set_wr_txq(skb, CPL_PRIORITY_CONTROL, csk->port_id);

	log_debug(1 << CXGBI_DBG_TOE | 1 << CXGBI_DBG_SOCK,
		"csk 0x%p, tid 0x%x, crc %d,%d.\n", csk, csk->tid, hcrc, dcrc);

	reinit_completion(&csk->cmpl);
	cxgb4_ofld_send(csk->cdev->ports[csk->port_id], skb);
	wait_for_completion(&csk->cmpl);

	return csk->err;
}

static struct cxgbi_ppm *cdev2ppm(struct cxgbi_device *cdev)
{
	return (struct cxgbi_ppm *)(*((struct cxgb4_lld_info *)
				       (cxgbi_cdev_priv(cdev)))->iscsi_ppm);
}

static int cxgb4i_ddp_init(struct cxgbi_device *cdev)
{
	struct cxgb4_lld_info *lldi = cxgbi_cdev_priv(cdev);
	struct net_device *ndev = cdev->ports[0];
	struct cxgbi_tag_format tformat;
	int i, err;

	if (!lldi->vr->iscsi.size) {
		pr_warn("%s, iscsi NOT enabled, check config!\n", ndev->name);
		return -EACCES;
	}

	cdev->flags |= CXGBI_FLAG_USE_PPOD_OFLDQ;

	memset(&tformat, 0, sizeof(struct cxgbi_tag_format));
	for (i = 0; i < 4; i++)
		tformat.pgsz_order[i] = (lldi->iscsi_pgsz_order >> (i << 3))
					 & 0xF;
	cxgbi_tagmask_check(lldi->iscsi_tagmask, &tformat);

	pr_info("iscsi_edram.start 0x%x iscsi_edram.size 0x%x",
		lldi->vr->ppod_edram.start, lldi->vr->ppod_edram.size);

	err = cxgbi_ddp_ppm_setup(lldi->iscsi_ppm, cdev, &tformat,
				  lldi->vr->iscsi.size, lldi->iscsi_llimit,
				  lldi->vr->iscsi.start, 2,
				  lldi->vr->ppod_edram.start,
				  lldi->vr->ppod_edram.size);

	if (err < 0)
		return err;

	cdev->csk_ddp_setup_digest = ddp_setup_conn_digest;
	cdev->csk_ddp_setup_pgidx = ddp_setup_conn_pgidx;
	cdev->csk_ddp_set_map = ddp_set_map;
	cdev->tx_max_size = min_t(unsigned int, ULP2_MAX_PDU_PAYLOAD,
				  lldi->iscsi_iolen - ISCSI_PDU_NONPAYLOAD_LEN);
	cdev->rx_max_size = min_t(unsigned int, ULP2_MAX_PDU_PAYLOAD,
				  lldi->iscsi_iolen - ISCSI_PDU_NONPAYLOAD_LEN);
	cdev->cdev2ppm = cdev2ppm;

	return 0;
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
	cdev->rx_credit_thres = (CHELSIO_CHIP_VERSION(lldi->adapter_type) <=
				 CHELSIO_T5) ? cxgb4i_rx_credit_thres : 0;
	cdev->skb_tx_rsvd = CXGB4I_TX_HEADER_LEN;
	cdev->skb_rx_extra = sizeof(struct cpl_iscsi_hdr);
	cdev->itp = &cxgb4i_iscsi_transport;
	cdev->owner = THIS_MODULE;

	cdev->pfvf = FW_PFVF_CMD_PFN_V(lldi->pf);
	pr_info("cdev 0x%p,%s, pfvf %u.\n",
		cdev, lldi->ports[0]->name, cdev->pfvf);

	rc = cxgb4i_ddp_init(cdev);
	if (rc) {
		pr_info("t4 0x%p ddp init failed %d.\n", cdev, rc);
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
	if (opc >= ARRAY_SIZE(cxgb4i_cplhandlers) || !cxgb4i_cplhandlers[opc]) {
		pr_err("No handler for opcode 0x%x.\n", opc);
		__kfree_skb(skb);
	} else
		cxgb4i_cplhandlers[opc](cdev, skb);

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

#ifdef CONFIG_CHELSIO_T4_DCB
static int
cxgb4_dcb_change_notify(struct notifier_block *self, unsigned long val,
			void *data)
{
	int i, port = 0xFF;
	struct net_device *ndev;
	struct cxgbi_device *cdev = NULL;
	struct dcb_app_type *iscsi_app = data;
	struct cxgbi_ports_map *pmap;
	u8 priority;

	if (iscsi_app->dcbx & DCB_CAP_DCBX_VER_IEEE) {
		if ((iscsi_app->app.selector != IEEE_8021QAZ_APP_SEL_STREAM) &&
		    (iscsi_app->app.selector != IEEE_8021QAZ_APP_SEL_ANY))
			return NOTIFY_DONE;

		priority = iscsi_app->app.priority;
	} else if (iscsi_app->dcbx & DCB_CAP_DCBX_VER_CEE) {
		if (iscsi_app->app.selector != DCB_APP_IDTYPE_PORTNUM)
			return NOTIFY_DONE;

		if (!iscsi_app->app.priority)
			return NOTIFY_DONE;

		priority = ffs(iscsi_app->app.priority) - 1;
	} else {
		return NOTIFY_DONE;
	}

	if (iscsi_app->app.protocol != 3260)
		return NOTIFY_DONE;

	log_debug(1 << CXGBI_DBG_ISCSI, "iSCSI priority for ifid %d is %u\n",
		  iscsi_app->ifindex, priority);

	ndev = dev_get_by_index(&init_net, iscsi_app->ifindex);
	if (!ndev)
		return NOTIFY_DONE;

	cdev = cxgbi_device_find_by_netdev_rcu(ndev, &port);

	dev_put(ndev);
	if (!cdev)
		return NOTIFY_DONE;

	pmap = &cdev->pmap;

	for (i = 0; i < pmap->used; i++) {
		if (pmap->port_csk[i]) {
			struct cxgbi_sock *csk = pmap->port_csk[i];

			if (csk->dcb_priority != priority) {
				iscsi_conn_failure(csk->user_data,
						   ISCSI_ERR_CONN_FAILED);
				pr_info("Restarting iSCSI connection %p with "
					"priority %u->%u.\n", csk,
					csk->dcb_priority, priority);
			}
		}
	}
	return NOTIFY_OK;
}
#endif

static int __init cxgb4i_init_module(void)
{
	int rc;

	printk(KERN_INFO "%s", version);

	rc = cxgbi_iscsi_init(&cxgb4i_iscsi_transport, &cxgb4i_stt);
	if (rc < 0)
		return rc;
	cxgb4_register_uld(CXGB4_ULD_ISCSI, &cxgb4i_uld_info);

#ifdef CONFIG_CHELSIO_T4_DCB
	pr_info("%s dcb enabled.\n", DRV_MODULE_NAME);
	register_dcbevent_notifier(&cxgb4_dcb_change);
#endif
	return 0;
}

static void __exit cxgb4i_exit_module(void)
{
#ifdef CONFIG_CHELSIO_T4_DCB
	unregister_dcbevent_notifier(&cxgb4_dcb_change);
#endif
	cxgb4_unregister_uld(CXGB4_ULD_ISCSI);
	cxgbi_device_unregister_all(CXGBI_FLAG_DEV_T4);
	cxgbi_iscsi_cleanup(&cxgb4i_iscsi_transport, &cxgb4i_stt);
}

module_init(cxgb4i_init_module);
module_exit(cxgb4i_exit_module);
