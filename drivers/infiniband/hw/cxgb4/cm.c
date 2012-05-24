/*
 * Copyright (c) 2009-2010 Chelsio, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer in the documentation and/or other materials
 *	  provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
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

#include <net/neighbour.h>
#include <net/netevent.h>
#include <net/route.h>

#include "iw_cxgb4.h"

static char *states[] = {
	"idle",
	"listen",
	"connecting",
	"mpa_wait_req",
	"mpa_req_sent",
	"mpa_req_rcvd",
	"mpa_rep_sent",
	"fpdu_mode",
	"aborting",
	"closing",
	"moribund",
	"dead",
	NULL,
};

static int dack_mode = 1;
module_param(dack_mode, int, 0644);
MODULE_PARM_DESC(dack_mode, "Delayed ack mode (default=1)");

int c4iw_max_read_depth = 8;
module_param(c4iw_max_read_depth, int, 0644);
MODULE_PARM_DESC(c4iw_max_read_depth, "Per-connection max ORD/IRD (default=8)");

static int enable_tcp_timestamps;
module_param(enable_tcp_timestamps, int, 0644);
MODULE_PARM_DESC(enable_tcp_timestamps, "Enable tcp timestamps (default=0)");

static int enable_tcp_sack;
module_param(enable_tcp_sack, int, 0644);
MODULE_PARM_DESC(enable_tcp_sack, "Enable tcp SACK (default=0)");

static int enable_tcp_window_scaling = 1;
module_param(enable_tcp_window_scaling, int, 0644);
MODULE_PARM_DESC(enable_tcp_window_scaling,
		 "Enable tcp window scaling (default=1)");

int c4iw_debug;
module_param(c4iw_debug, int, 0644);
MODULE_PARM_DESC(c4iw_debug, "Enable debug logging (default=0)");

static int peer2peer;
module_param(peer2peer, int, 0644);
MODULE_PARM_DESC(peer2peer, "Support peer2peer ULPs (default=0)");

static int p2p_type = FW_RI_INIT_P2PTYPE_READ_REQ;
module_param(p2p_type, int, 0644);
MODULE_PARM_DESC(p2p_type, "RDMAP opcode to use for the RTR message: "
			   "1=RDMA_READ 0=RDMA_WRITE (default 1)");

static int ep_timeout_secs = 60;
module_param(ep_timeout_secs, int, 0644);
MODULE_PARM_DESC(ep_timeout_secs, "CM Endpoint operation timeout "
				   "in seconds (default=60)");

static int mpa_rev = 1;
module_param(mpa_rev, int, 0644);
MODULE_PARM_DESC(mpa_rev, "MPA Revision, 0 supports amso1100, "
		"1 is RFC0544 spec compliant, 2 is IETF MPA Peer Connect Draft"
		" compliant (default=1)");

static int markers_enabled;
module_param(markers_enabled, int, 0644);
MODULE_PARM_DESC(markers_enabled, "Enable MPA MARKERS (default(0)=disabled)");

static int crc_enabled = 1;
module_param(crc_enabled, int, 0644);
MODULE_PARM_DESC(crc_enabled, "Enable MPA CRC (default(1)=enabled)");

static int rcv_win = 256 * 1024;
module_param(rcv_win, int, 0644);
MODULE_PARM_DESC(rcv_win, "TCP receive window in bytes (default=256KB)");

static int snd_win = 128 * 1024;
module_param(snd_win, int, 0644);
MODULE_PARM_DESC(snd_win, "TCP send window in bytes (default=128KB)");

static struct workqueue_struct *workq;

static struct sk_buff_head rxq;

static struct sk_buff *get_skb(struct sk_buff *skb, int len, gfp_t gfp);
static void ep_timeout(unsigned long arg);
static void connect_reply_upcall(struct c4iw_ep *ep, int status);

static LIST_HEAD(timeout_list);
static spinlock_t timeout_lock;

static void start_ep_timer(struct c4iw_ep *ep)
{
	PDBG("%s ep %p\n", __func__, ep);
	if (timer_pending(&ep->timer)) {
		PDBG("%s stopped / restarted timer ep %p\n", __func__, ep);
		del_timer_sync(&ep->timer);
	} else
		c4iw_get_ep(&ep->com);
	ep->timer.expires = jiffies + ep_timeout_secs * HZ;
	ep->timer.data = (unsigned long)ep;
	ep->timer.function = ep_timeout;
	add_timer(&ep->timer);
}

static void stop_ep_timer(struct c4iw_ep *ep)
{
	PDBG("%s ep %p\n", __func__, ep);
	if (!timer_pending(&ep->timer)) {
		printk(KERN_ERR "%s timer stopped when its not running! "
		       "ep %p state %u\n", __func__, ep, ep->com.state);
		WARN_ON(1);
		return;
	}
	del_timer_sync(&ep->timer);
	c4iw_put_ep(&ep->com);
}

static int c4iw_l2t_send(struct c4iw_rdev *rdev, struct sk_buff *skb,
		  struct l2t_entry *l2e)
{
	int	error = 0;

	if (c4iw_fatal_error(rdev)) {
		kfree_skb(skb);
		PDBG("%s - device in error state - dropping\n", __func__);
		return -EIO;
	}
	error = cxgb4_l2t_send(rdev->lldi.ports[0], skb, l2e);
	if (error < 0)
		kfree_skb(skb);
	return error < 0 ? error : 0;
}

int c4iw_ofld_send(struct c4iw_rdev *rdev, struct sk_buff *skb)
{
	int	error = 0;

	if (c4iw_fatal_error(rdev)) {
		kfree_skb(skb);
		PDBG("%s - device in error state - dropping\n", __func__);
		return -EIO;
	}
	error = cxgb4_ofld_send(rdev->lldi.ports[0], skb);
	if (error < 0)
		kfree_skb(skb);
	return error < 0 ? error : 0;
}

static void release_tid(struct c4iw_rdev *rdev, u32 hwtid, struct sk_buff *skb)
{
	struct cpl_tid_release *req;

	skb = get_skb(skb, sizeof *req, GFP_KERNEL);
	if (!skb)
		return;
	req = (struct cpl_tid_release *) skb_put(skb, sizeof(*req));
	INIT_TP_WR(req, hwtid);
	OPCODE_TID(req) = cpu_to_be32(MK_OPCODE_TID(CPL_TID_RELEASE, hwtid));
	set_wr_txq(skb, CPL_PRIORITY_SETUP, 0);
	c4iw_ofld_send(rdev, skb);
	return;
}

static void set_emss(struct c4iw_ep *ep, u16 opt)
{
	ep->emss = ep->com.dev->rdev.lldi.mtus[GET_TCPOPT_MSS(opt)] - 40;
	ep->mss = ep->emss;
	if (GET_TCPOPT_TSTAMP(opt))
		ep->emss -= 12;
	if (ep->emss < 128)
		ep->emss = 128;
	PDBG("%s mss_idx %u mss %u emss=%u\n", __func__, GET_TCPOPT_MSS(opt),
	     ep->mss, ep->emss);
}

static enum c4iw_ep_state state_read(struct c4iw_ep_common *epc)
{
	enum c4iw_ep_state state;

	mutex_lock(&epc->mutex);
	state = epc->state;
	mutex_unlock(&epc->mutex);
	return state;
}

static void __state_set(struct c4iw_ep_common *epc, enum c4iw_ep_state new)
{
	epc->state = new;
}

static void state_set(struct c4iw_ep_common *epc, enum c4iw_ep_state new)
{
	mutex_lock(&epc->mutex);
	PDBG("%s - %s -> %s\n", __func__, states[epc->state], states[new]);
	__state_set(epc, new);
	mutex_unlock(&epc->mutex);
	return;
}

static void *alloc_ep(int size, gfp_t gfp)
{
	struct c4iw_ep_common *epc;

	epc = kzalloc(size, gfp);
	if (epc) {
		kref_init(&epc->kref);
		mutex_init(&epc->mutex);
		c4iw_init_wr_wait(&epc->wr_wait);
	}
	PDBG("%s alloc ep %p\n", __func__, epc);
	return epc;
}

void _c4iw_free_ep(struct kref *kref)
{
	struct c4iw_ep *ep;

	ep = container_of(kref, struct c4iw_ep, com.kref);
	PDBG("%s ep %p state %s\n", __func__, ep, states[state_read(&ep->com)]);
	if (test_bit(RELEASE_RESOURCES, &ep->com.flags)) {
		cxgb4_remove_tid(ep->com.dev->rdev.lldi.tids, 0, ep->hwtid);
		dst_release(ep->dst);
		cxgb4_l2t_release(ep->l2t);
	}
	kfree(ep);
}

static void release_ep_resources(struct c4iw_ep *ep)
{
	set_bit(RELEASE_RESOURCES, &ep->com.flags);
	c4iw_put_ep(&ep->com);
}

static int status2errno(int status)
{
	switch (status) {
	case CPL_ERR_NONE:
		return 0;
	case CPL_ERR_CONN_RESET:
		return -ECONNRESET;
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

/*
 * Try and reuse skbs already allocated...
 */
static struct sk_buff *get_skb(struct sk_buff *skb, int len, gfp_t gfp)
{
	if (skb && !skb_is_nonlinear(skb) && !skb_cloned(skb)) {
		skb_trim(skb, 0);
		skb_get(skb);
		skb_reset_transport_header(skb);
	} else {
		skb = alloc_skb(len, gfp);
	}
	return skb;
}

static struct rtable *find_route(struct c4iw_dev *dev, __be32 local_ip,
				 __be32 peer_ip, __be16 local_port,
				 __be16 peer_port, u8 tos)
{
	struct rtable *rt;
	struct flowi4 fl4;

	rt = ip_route_output_ports(&init_net, &fl4, NULL, peer_ip, local_ip,
				   peer_port, local_port, IPPROTO_TCP,
				   tos, 0);
	if (IS_ERR(rt))
		return NULL;
	return rt;
}

static void arp_failure_discard(void *handle, struct sk_buff *skb)
{
	PDBG("%s c4iw_dev %p\n", __func__, handle);
	kfree_skb(skb);
}

/*
 * Handle an ARP failure for an active open.
 */
static void act_open_req_arp_failure(void *handle, struct sk_buff *skb)
{
	printk(KERN_ERR MOD "ARP failure duing connect\n");
	kfree_skb(skb);
}

/*
 * Handle an ARP failure for a CPL_ABORT_REQ.  Change it into a no RST variant
 * and send it along.
 */
static void abort_arp_failure(void *handle, struct sk_buff *skb)
{
	struct c4iw_rdev *rdev = handle;
	struct cpl_abort_req *req = cplhdr(skb);

	PDBG("%s rdev %p\n", __func__, rdev);
	req->cmd = CPL_ABORT_NO_RST;
	c4iw_ofld_send(rdev, skb);
}

static void send_flowc(struct c4iw_ep *ep, struct sk_buff *skb)
{
	unsigned int flowclen = 80;
	struct fw_flowc_wr *flowc;
	int i;

	skb = get_skb(skb, flowclen, GFP_KERNEL);
	flowc = (struct fw_flowc_wr *)__skb_put(skb, flowclen);

	flowc->op_to_nparams = cpu_to_be32(FW_WR_OP(FW_FLOWC_WR) |
					   FW_FLOWC_WR_NPARAMS(8));
	flowc->flowid_len16 = cpu_to_be32(FW_WR_LEN16(DIV_ROUND_UP(flowclen,
					  16)) | FW_WR_FLOWID(ep->hwtid));

	flowc->mnemval[0].mnemonic = FW_FLOWC_MNEM_PFNVFN;
	flowc->mnemval[0].val = cpu_to_be32(PCI_FUNC(ep->com.dev->rdev.lldi.pdev->devfn) << 8);
	flowc->mnemval[1].mnemonic = FW_FLOWC_MNEM_CH;
	flowc->mnemval[1].val = cpu_to_be32(ep->tx_chan);
	flowc->mnemval[2].mnemonic = FW_FLOWC_MNEM_PORT;
	flowc->mnemval[2].val = cpu_to_be32(ep->tx_chan);
	flowc->mnemval[3].mnemonic = FW_FLOWC_MNEM_IQID;
	flowc->mnemval[3].val = cpu_to_be32(ep->rss_qid);
	flowc->mnemval[4].mnemonic = FW_FLOWC_MNEM_SNDNXT;
	flowc->mnemval[4].val = cpu_to_be32(ep->snd_seq);
	flowc->mnemval[5].mnemonic = FW_FLOWC_MNEM_RCVNXT;
	flowc->mnemval[5].val = cpu_to_be32(ep->rcv_seq);
	flowc->mnemval[6].mnemonic = FW_FLOWC_MNEM_SNDBUF;
	flowc->mnemval[6].val = cpu_to_be32(snd_win);
	flowc->mnemval[7].mnemonic = FW_FLOWC_MNEM_MSS;
	flowc->mnemval[7].val = cpu_to_be32(ep->emss);
	/* Pad WR to 16 byte boundary */
	flowc->mnemval[8].mnemonic = 0;
	flowc->mnemval[8].val = 0;
	for (i = 0; i < 9; i++) {
		flowc->mnemval[i].r4[0] = 0;
		flowc->mnemval[i].r4[1] = 0;
		flowc->mnemval[i].r4[2] = 0;
	}

	set_wr_txq(skb, CPL_PRIORITY_DATA, ep->txq_idx);
	c4iw_ofld_send(&ep->com.dev->rdev, skb);
}

static int send_halfclose(struct c4iw_ep *ep, gfp_t gfp)
{
	struct cpl_close_con_req *req;
	struct sk_buff *skb;
	int wrlen = roundup(sizeof *req, 16);

	PDBG("%s ep %p tid %u\n", __func__, ep, ep->hwtid);
	skb = get_skb(NULL, wrlen, gfp);
	if (!skb) {
		printk(KERN_ERR MOD "%s - failed to alloc skb\n", __func__);
		return -ENOMEM;
	}
	set_wr_txq(skb, CPL_PRIORITY_DATA, ep->txq_idx);
	t4_set_arp_err_handler(skb, NULL, arp_failure_discard);
	req = (struct cpl_close_con_req *) skb_put(skb, wrlen);
	memset(req, 0, wrlen);
	INIT_TP_WR(req, ep->hwtid);
	OPCODE_TID(req) = cpu_to_be32(MK_OPCODE_TID(CPL_CLOSE_CON_REQ,
						    ep->hwtid));
	return c4iw_l2t_send(&ep->com.dev->rdev, skb, ep->l2t);
}

static int send_abort(struct c4iw_ep *ep, struct sk_buff *skb, gfp_t gfp)
{
	struct cpl_abort_req *req;
	int wrlen = roundup(sizeof *req, 16);

	PDBG("%s ep %p tid %u\n", __func__, ep, ep->hwtid);
	skb = get_skb(skb, wrlen, gfp);
	if (!skb) {
		printk(KERN_ERR MOD "%s - failed to alloc skb.\n",
		       __func__);
		return -ENOMEM;
	}
	set_wr_txq(skb, CPL_PRIORITY_DATA, ep->txq_idx);
	t4_set_arp_err_handler(skb, &ep->com.dev->rdev, abort_arp_failure);
	req = (struct cpl_abort_req *) skb_put(skb, wrlen);
	memset(req, 0, wrlen);
	INIT_TP_WR(req, ep->hwtid);
	OPCODE_TID(req) = cpu_to_be32(MK_OPCODE_TID(CPL_ABORT_REQ, ep->hwtid));
	req->cmd = CPL_ABORT_SEND_RST;
	return c4iw_l2t_send(&ep->com.dev->rdev, skb, ep->l2t);
}

static int send_connect(struct c4iw_ep *ep)
{
	struct cpl_act_open_req *req;
	struct sk_buff *skb;
	u64 opt0;
	u32 opt2;
	unsigned int mtu_idx;
	int wscale;
	int wrlen = roundup(sizeof *req, 16);

	PDBG("%s ep %p atid %u\n", __func__, ep, ep->atid);

	skb = get_skb(NULL, wrlen, GFP_KERNEL);
	if (!skb) {
		printk(KERN_ERR MOD "%s - failed to alloc skb.\n",
		       __func__);
		return -ENOMEM;
	}
	set_wr_txq(skb, CPL_PRIORITY_SETUP, ep->ctrlq_idx);

	cxgb4_best_mtu(ep->com.dev->rdev.lldi.mtus, ep->mtu, &mtu_idx);
	wscale = compute_wscale(rcv_win);
	opt0 = KEEP_ALIVE(1) |
	       DELACK(1) |
	       WND_SCALE(wscale) |
	       MSS_IDX(mtu_idx) |
	       L2T_IDX(ep->l2t->idx) |
	       TX_CHAN(ep->tx_chan) |
	       SMAC_SEL(ep->smac_idx) |
	       DSCP(ep->tos) |
	       ULP_MODE(ULP_MODE_TCPDDP) |
	       RCV_BUFSIZ(rcv_win>>10);
	opt2 = RX_CHANNEL(0) |
	       RSS_QUEUE_VALID | RSS_QUEUE(ep->rss_qid);
	if (enable_tcp_timestamps)
		opt2 |= TSTAMPS_EN(1);
	if (enable_tcp_sack)
		opt2 |= SACK_EN(1);
	if (wscale && enable_tcp_window_scaling)
		opt2 |= WND_SCALE_EN(1);
	t4_set_arp_err_handler(skb, NULL, act_open_req_arp_failure);

	req = (struct cpl_act_open_req *) skb_put(skb, wrlen);
	INIT_TP_WR(req, 0);
	OPCODE_TID(req) = cpu_to_be32(
		MK_OPCODE_TID(CPL_ACT_OPEN_REQ, ((ep->rss_qid<<14)|ep->atid)));
	req->local_port = ep->com.local_addr.sin_port;
	req->peer_port = ep->com.remote_addr.sin_port;
	req->local_ip = ep->com.local_addr.sin_addr.s_addr;
	req->peer_ip = ep->com.remote_addr.sin_addr.s_addr;
	req->opt0 = cpu_to_be64(opt0);
	req->params = 0;
	req->opt2 = cpu_to_be32(opt2);
	return c4iw_l2t_send(&ep->com.dev->rdev, skb, ep->l2t);
}

static void send_mpa_req(struct c4iw_ep *ep, struct sk_buff *skb,
		u8 mpa_rev_to_use)
{
	int mpalen, wrlen;
	struct fw_ofld_tx_data_wr *req;
	struct mpa_message *mpa;
	struct mpa_v2_conn_params mpa_v2_params;

	PDBG("%s ep %p tid %u pd_len %d\n", __func__, ep, ep->hwtid, ep->plen);

	BUG_ON(skb_cloned(skb));

	mpalen = sizeof(*mpa) + ep->plen;
	if (mpa_rev_to_use == 2)
		mpalen += sizeof(struct mpa_v2_conn_params);
	wrlen = roundup(mpalen + sizeof *req, 16);
	skb = get_skb(skb, wrlen, GFP_KERNEL);
	if (!skb) {
		connect_reply_upcall(ep, -ENOMEM);
		return;
	}
	set_wr_txq(skb, CPL_PRIORITY_DATA, ep->txq_idx);

	req = (struct fw_ofld_tx_data_wr *)skb_put(skb, wrlen);
	memset(req, 0, wrlen);
	req->op_to_immdlen = cpu_to_be32(
		FW_WR_OP(FW_OFLD_TX_DATA_WR) |
		FW_WR_COMPL(1) |
		FW_WR_IMMDLEN(mpalen));
	req->flowid_len16 = cpu_to_be32(
		FW_WR_FLOWID(ep->hwtid) |
		FW_WR_LEN16(wrlen >> 4));
	req->plen = cpu_to_be32(mpalen);
	req->tunnel_to_proxy = cpu_to_be32(
		FW_OFLD_TX_DATA_WR_FLUSH(1) |
		FW_OFLD_TX_DATA_WR_SHOVE(1));

	mpa = (struct mpa_message *)(req + 1);
	memcpy(mpa->key, MPA_KEY_REQ, sizeof(mpa->key));
	mpa->flags = (crc_enabled ? MPA_CRC : 0) |
		     (markers_enabled ? MPA_MARKERS : 0) |
		     (mpa_rev_to_use == 2 ? MPA_ENHANCED_RDMA_CONN : 0);
	mpa->private_data_size = htons(ep->plen);
	mpa->revision = mpa_rev_to_use;
	if (mpa_rev_to_use == 1) {
		ep->tried_with_mpa_v1 = 1;
		ep->retry_with_mpa_v1 = 0;
	}

	if (mpa_rev_to_use == 2) {
		mpa->private_data_size +=
			htons(sizeof(struct mpa_v2_conn_params));
		mpa_v2_params.ird = htons((u16)ep->ird);
		mpa_v2_params.ord = htons((u16)ep->ord);

		if (peer2peer) {
			mpa_v2_params.ird |= htons(MPA_V2_PEER2PEER_MODEL);
			if (p2p_type == FW_RI_INIT_P2PTYPE_RDMA_WRITE)
				mpa_v2_params.ord |=
					htons(MPA_V2_RDMA_WRITE_RTR);
			else if (p2p_type == FW_RI_INIT_P2PTYPE_READ_REQ)
				mpa_v2_params.ord |=
					htons(MPA_V2_RDMA_READ_RTR);
		}
		memcpy(mpa->private_data, &mpa_v2_params,
		       sizeof(struct mpa_v2_conn_params));

		if (ep->plen)
			memcpy(mpa->private_data +
			       sizeof(struct mpa_v2_conn_params),
			       ep->mpa_pkt + sizeof(*mpa), ep->plen);
	} else
		if (ep->plen)
			memcpy(mpa->private_data,
					ep->mpa_pkt + sizeof(*mpa), ep->plen);

	/*
	 * Reference the mpa skb.  This ensures the data area
	 * will remain in memory until the hw acks the tx.
	 * Function fw4_ack() will deref it.
	 */
	skb_get(skb);
	t4_set_arp_err_handler(skb, NULL, arp_failure_discard);
	BUG_ON(ep->mpa_skb);
	ep->mpa_skb = skb;
	c4iw_l2t_send(&ep->com.dev->rdev, skb, ep->l2t);
	start_ep_timer(ep);
	state_set(&ep->com, MPA_REQ_SENT);
	ep->mpa_attr.initiator = 1;
	return;
}

static int send_mpa_reject(struct c4iw_ep *ep, const void *pdata, u8 plen)
{
	int mpalen, wrlen;
	struct fw_ofld_tx_data_wr *req;
	struct mpa_message *mpa;
	struct sk_buff *skb;
	struct mpa_v2_conn_params mpa_v2_params;

	PDBG("%s ep %p tid %u pd_len %d\n", __func__, ep, ep->hwtid, ep->plen);

	mpalen = sizeof(*mpa) + plen;
	if (ep->mpa_attr.version == 2 && ep->mpa_attr.enhanced_rdma_conn)
		mpalen += sizeof(struct mpa_v2_conn_params);
	wrlen = roundup(mpalen + sizeof *req, 16);

	skb = get_skb(NULL, wrlen, GFP_KERNEL);
	if (!skb) {
		printk(KERN_ERR MOD "%s - cannot alloc skb!\n", __func__);
		return -ENOMEM;
	}
	set_wr_txq(skb, CPL_PRIORITY_DATA, ep->txq_idx);

	req = (struct fw_ofld_tx_data_wr *)skb_put(skb, wrlen);
	memset(req, 0, wrlen);
	req->op_to_immdlen = cpu_to_be32(
		FW_WR_OP(FW_OFLD_TX_DATA_WR) |
		FW_WR_COMPL(1) |
		FW_WR_IMMDLEN(mpalen));
	req->flowid_len16 = cpu_to_be32(
		FW_WR_FLOWID(ep->hwtid) |
		FW_WR_LEN16(wrlen >> 4));
	req->plen = cpu_to_be32(mpalen);
	req->tunnel_to_proxy = cpu_to_be32(
		FW_OFLD_TX_DATA_WR_FLUSH(1) |
		FW_OFLD_TX_DATA_WR_SHOVE(1));

	mpa = (struct mpa_message *)(req + 1);
	memset(mpa, 0, sizeof(*mpa));
	memcpy(mpa->key, MPA_KEY_REP, sizeof(mpa->key));
	mpa->flags = MPA_REJECT;
	mpa->revision = mpa_rev;
	mpa->private_data_size = htons(plen);

	if (ep->mpa_attr.version == 2 && ep->mpa_attr.enhanced_rdma_conn) {
		mpa->flags |= MPA_ENHANCED_RDMA_CONN;
		mpa->private_data_size +=
			htons(sizeof(struct mpa_v2_conn_params));
		mpa_v2_params.ird = htons(((u16)ep->ird) |
					  (peer2peer ? MPA_V2_PEER2PEER_MODEL :
					   0));
		mpa_v2_params.ord = htons(((u16)ep->ord) | (peer2peer ?
					  (p2p_type ==
					   FW_RI_INIT_P2PTYPE_RDMA_WRITE ?
					   MPA_V2_RDMA_WRITE_RTR : p2p_type ==
					   FW_RI_INIT_P2PTYPE_READ_REQ ?
					   MPA_V2_RDMA_READ_RTR : 0) : 0));
		memcpy(mpa->private_data, &mpa_v2_params,
		       sizeof(struct mpa_v2_conn_params));

		if (ep->plen)
			memcpy(mpa->private_data +
			       sizeof(struct mpa_v2_conn_params), pdata, plen);
	} else
		if (plen)
			memcpy(mpa->private_data, pdata, plen);

	/*
	 * Reference the mpa skb again.  This ensures the data area
	 * will remain in memory until the hw acks the tx.
	 * Function fw4_ack() will deref it.
	 */
	skb_get(skb);
	set_wr_txq(skb, CPL_PRIORITY_DATA, ep->txq_idx);
	t4_set_arp_err_handler(skb, NULL, arp_failure_discard);
	BUG_ON(ep->mpa_skb);
	ep->mpa_skb = skb;
	return c4iw_l2t_send(&ep->com.dev->rdev, skb, ep->l2t);
}

static int send_mpa_reply(struct c4iw_ep *ep, const void *pdata, u8 plen)
{
	int mpalen, wrlen;
	struct fw_ofld_tx_data_wr *req;
	struct mpa_message *mpa;
	struct sk_buff *skb;
	struct mpa_v2_conn_params mpa_v2_params;

	PDBG("%s ep %p tid %u pd_len %d\n", __func__, ep, ep->hwtid, ep->plen);

	mpalen = sizeof(*mpa) + plen;
	if (ep->mpa_attr.version == 2 && ep->mpa_attr.enhanced_rdma_conn)
		mpalen += sizeof(struct mpa_v2_conn_params);
	wrlen = roundup(mpalen + sizeof *req, 16);

	skb = get_skb(NULL, wrlen, GFP_KERNEL);
	if (!skb) {
		printk(KERN_ERR MOD "%s - cannot alloc skb!\n", __func__);
		return -ENOMEM;
	}
	set_wr_txq(skb, CPL_PRIORITY_DATA, ep->txq_idx);

	req = (struct fw_ofld_tx_data_wr *) skb_put(skb, wrlen);
	memset(req, 0, wrlen);
	req->op_to_immdlen = cpu_to_be32(
		FW_WR_OP(FW_OFLD_TX_DATA_WR) |
		FW_WR_COMPL(1) |
		FW_WR_IMMDLEN(mpalen));
	req->flowid_len16 = cpu_to_be32(
		FW_WR_FLOWID(ep->hwtid) |
		FW_WR_LEN16(wrlen >> 4));
	req->plen = cpu_to_be32(mpalen);
	req->tunnel_to_proxy = cpu_to_be32(
		FW_OFLD_TX_DATA_WR_FLUSH(1) |
		FW_OFLD_TX_DATA_WR_SHOVE(1));

	mpa = (struct mpa_message *)(req + 1);
	memset(mpa, 0, sizeof(*mpa));
	memcpy(mpa->key, MPA_KEY_REP, sizeof(mpa->key));
	mpa->flags = (ep->mpa_attr.crc_enabled ? MPA_CRC : 0) |
		     (markers_enabled ? MPA_MARKERS : 0);
	mpa->revision = ep->mpa_attr.version;
	mpa->private_data_size = htons(plen);

	if (ep->mpa_attr.version == 2 && ep->mpa_attr.enhanced_rdma_conn) {
		mpa->flags |= MPA_ENHANCED_RDMA_CONN;
		mpa->private_data_size +=
			htons(sizeof(struct mpa_v2_conn_params));
		mpa_v2_params.ird = htons((u16)ep->ird);
		mpa_v2_params.ord = htons((u16)ep->ord);
		if (peer2peer && (ep->mpa_attr.p2p_type !=
					FW_RI_INIT_P2PTYPE_DISABLED)) {
			mpa_v2_params.ird |= htons(MPA_V2_PEER2PEER_MODEL);

			if (p2p_type == FW_RI_INIT_P2PTYPE_RDMA_WRITE)
				mpa_v2_params.ord |=
					htons(MPA_V2_RDMA_WRITE_RTR);
			else if (p2p_type == FW_RI_INIT_P2PTYPE_READ_REQ)
				mpa_v2_params.ord |=
					htons(MPA_V2_RDMA_READ_RTR);
		}

		memcpy(mpa->private_data, &mpa_v2_params,
		       sizeof(struct mpa_v2_conn_params));

		if (ep->plen)
			memcpy(mpa->private_data +
			       sizeof(struct mpa_v2_conn_params), pdata, plen);
	} else
		if (plen)
			memcpy(mpa->private_data, pdata, plen);

	/*
	 * Reference the mpa skb.  This ensures the data area
	 * will remain in memory until the hw acks the tx.
	 * Function fw4_ack() will deref it.
	 */
	skb_get(skb);
	t4_set_arp_err_handler(skb, NULL, arp_failure_discard);
	ep->mpa_skb = skb;
	state_set(&ep->com, MPA_REP_SENT);
	return c4iw_l2t_send(&ep->com.dev->rdev, skb, ep->l2t);
}

static int act_establish(struct c4iw_dev *dev, struct sk_buff *skb)
{
	struct c4iw_ep *ep;
	struct cpl_act_establish *req = cplhdr(skb);
	unsigned int tid = GET_TID(req);
	unsigned int atid = GET_TID_TID(ntohl(req->tos_atid));
	struct tid_info *t = dev->rdev.lldi.tids;

	ep = lookup_atid(t, atid);

	PDBG("%s ep %p tid %u snd_isn %u rcv_isn %u\n", __func__, ep, tid,
	     be32_to_cpu(req->snd_isn), be32_to_cpu(req->rcv_isn));

	dst_confirm(ep->dst);

	/* setup the hwtid for this connection */
	ep->hwtid = tid;
	cxgb4_insert_tid(t, ep, tid);

	ep->snd_seq = be32_to_cpu(req->snd_isn);
	ep->rcv_seq = be32_to_cpu(req->rcv_isn);

	set_emss(ep, ntohs(req->tcp_opt));

	/* dealloc the atid */
	cxgb4_free_atid(t, atid);

	/* start MPA negotiation */
	send_flowc(ep, NULL);
	if (ep->retry_with_mpa_v1)
		send_mpa_req(ep, skb, 1);
	else
		send_mpa_req(ep, skb, mpa_rev);

	return 0;
}

static void close_complete_upcall(struct c4iw_ep *ep)
{
	struct iw_cm_event event;

	PDBG("%s ep %p tid %u\n", __func__, ep, ep->hwtid);
	memset(&event, 0, sizeof(event));
	event.event = IW_CM_EVENT_CLOSE;
	if (ep->com.cm_id) {
		PDBG("close complete delivered ep %p cm_id %p tid %u\n",
		     ep, ep->com.cm_id, ep->hwtid);
		ep->com.cm_id->event_handler(ep->com.cm_id, &event);
		ep->com.cm_id->rem_ref(ep->com.cm_id);
		ep->com.cm_id = NULL;
		ep->com.qp = NULL;
	}
}

static int abort_connection(struct c4iw_ep *ep, struct sk_buff *skb, gfp_t gfp)
{
	PDBG("%s ep %p tid %u\n", __func__, ep, ep->hwtid);
	close_complete_upcall(ep);
	state_set(&ep->com, ABORTING);
	return send_abort(ep, skb, gfp);
}

static void peer_close_upcall(struct c4iw_ep *ep)
{
	struct iw_cm_event event;

	PDBG("%s ep %p tid %u\n", __func__, ep, ep->hwtid);
	memset(&event, 0, sizeof(event));
	event.event = IW_CM_EVENT_DISCONNECT;
	if (ep->com.cm_id) {
		PDBG("peer close delivered ep %p cm_id %p tid %u\n",
		     ep, ep->com.cm_id, ep->hwtid);
		ep->com.cm_id->event_handler(ep->com.cm_id, &event);
	}
}

static void peer_abort_upcall(struct c4iw_ep *ep)
{
	struct iw_cm_event event;

	PDBG("%s ep %p tid %u\n", __func__, ep, ep->hwtid);
	memset(&event, 0, sizeof(event));
	event.event = IW_CM_EVENT_CLOSE;
	event.status = -ECONNRESET;
	if (ep->com.cm_id) {
		PDBG("abort delivered ep %p cm_id %p tid %u\n", ep,
		     ep->com.cm_id, ep->hwtid);
		ep->com.cm_id->event_handler(ep->com.cm_id, &event);
		ep->com.cm_id->rem_ref(ep->com.cm_id);
		ep->com.cm_id = NULL;
		ep->com.qp = NULL;
	}
}

static void connect_reply_upcall(struct c4iw_ep *ep, int status)
{
	struct iw_cm_event event;

	PDBG("%s ep %p tid %u status %d\n", __func__, ep, ep->hwtid, status);
	memset(&event, 0, sizeof(event));
	event.event = IW_CM_EVENT_CONNECT_REPLY;
	event.status = status;
	event.local_addr = ep->com.local_addr;
	event.remote_addr = ep->com.remote_addr;

	if ((status == 0) || (status == -ECONNREFUSED)) {
		if (!ep->tried_with_mpa_v1) {
			/* this means MPA_v2 is used */
			event.private_data_len = ep->plen -
				sizeof(struct mpa_v2_conn_params);
			event.private_data = ep->mpa_pkt +
				sizeof(struct mpa_message) +
				sizeof(struct mpa_v2_conn_params);
		} else {
			/* this means MPA_v1 is used */
			event.private_data_len = ep->plen;
			event.private_data = ep->mpa_pkt +
				sizeof(struct mpa_message);
		}
	}

	PDBG("%s ep %p tid %u status %d\n", __func__, ep,
	     ep->hwtid, status);
	ep->com.cm_id->event_handler(ep->com.cm_id, &event);

	if (status < 0) {
		ep->com.cm_id->rem_ref(ep->com.cm_id);
		ep->com.cm_id = NULL;
		ep->com.qp = NULL;
	}
}

static void connect_request_upcall(struct c4iw_ep *ep)
{
	struct iw_cm_event event;

	PDBG("%s ep %p tid %u\n", __func__, ep, ep->hwtid);
	memset(&event, 0, sizeof(event));
	event.event = IW_CM_EVENT_CONNECT_REQUEST;
	event.local_addr = ep->com.local_addr;
	event.remote_addr = ep->com.remote_addr;
	event.provider_data = ep;
	if (!ep->tried_with_mpa_v1) {
		/* this means MPA_v2 is used */
		event.ord = ep->ord;
		event.ird = ep->ird;
		event.private_data_len = ep->plen -
			sizeof(struct mpa_v2_conn_params);
		event.private_data = ep->mpa_pkt + sizeof(struct mpa_message) +
			sizeof(struct mpa_v2_conn_params);
	} else {
		/* this means MPA_v1 is used. Send max supported */
		event.ord = c4iw_max_read_depth;
		event.ird = c4iw_max_read_depth;
		event.private_data_len = ep->plen;
		event.private_data = ep->mpa_pkt + sizeof(struct mpa_message);
	}
	if (state_read(&ep->parent_ep->com) != DEAD) {
		c4iw_get_ep(&ep->com);
		ep->parent_ep->com.cm_id->event_handler(
						ep->parent_ep->com.cm_id,
						&event);
	}
	c4iw_put_ep(&ep->parent_ep->com);
	ep->parent_ep = NULL;
}

static void established_upcall(struct c4iw_ep *ep)
{
	struct iw_cm_event event;

	PDBG("%s ep %p tid %u\n", __func__, ep, ep->hwtid);
	memset(&event, 0, sizeof(event));
	event.event = IW_CM_EVENT_ESTABLISHED;
	event.ird = ep->ird;
	event.ord = ep->ord;
	if (ep->com.cm_id) {
		PDBG("%s ep %p tid %u\n", __func__, ep, ep->hwtid);
		ep->com.cm_id->event_handler(ep->com.cm_id, &event);
	}
}

static int update_rx_credits(struct c4iw_ep *ep, u32 credits)
{
	struct cpl_rx_data_ack *req;
	struct sk_buff *skb;
	int wrlen = roundup(sizeof *req, 16);

	PDBG("%s ep %p tid %u credits %u\n", __func__, ep, ep->hwtid, credits);
	skb = get_skb(NULL, wrlen, GFP_KERNEL);
	if (!skb) {
		printk(KERN_ERR MOD "update_rx_credits - cannot alloc skb!\n");
		return 0;
	}

	req = (struct cpl_rx_data_ack *) skb_put(skb, wrlen);
	memset(req, 0, wrlen);
	INIT_TP_WR(req, ep->hwtid);
	OPCODE_TID(req) = cpu_to_be32(MK_OPCODE_TID(CPL_RX_DATA_ACK,
						    ep->hwtid));
	req->credit_dack = cpu_to_be32(credits | RX_FORCE_ACK(1) |
				       F_RX_DACK_CHANGE |
				       V_RX_DACK_MODE(dack_mode));
	set_wr_txq(skb, CPL_PRIORITY_ACK, ep->ctrlq_idx);
	c4iw_ofld_send(&ep->com.dev->rdev, skb);
	return credits;
}

static void process_mpa_reply(struct c4iw_ep *ep, struct sk_buff *skb)
{
	struct mpa_message *mpa;
	struct mpa_v2_conn_params *mpa_v2_params;
	u16 plen;
	u16 resp_ird, resp_ord;
	u8 rtr_mismatch = 0, insuff_ird = 0;
	struct c4iw_qp_attributes attrs;
	enum c4iw_qp_attr_mask mask;
	int err;

	PDBG("%s ep %p tid %u\n", __func__, ep, ep->hwtid);

	/*
	 * Stop mpa timer.  If it expired, then the state has
	 * changed and we bail since ep_timeout already aborted
	 * the connection.
	 */
	stop_ep_timer(ep);
	if (state_read(&ep->com) != MPA_REQ_SENT)
		return;

	/*
	 * If we get more than the supported amount of private data
	 * then we must fail this connection.
	 */
	if (ep->mpa_pkt_len + skb->len > sizeof(ep->mpa_pkt)) {
		err = -EINVAL;
		goto err;
	}

	/*
	 * copy the new data into our accumulation buffer.
	 */
	skb_copy_from_linear_data(skb, &(ep->mpa_pkt[ep->mpa_pkt_len]),
				  skb->len);
	ep->mpa_pkt_len += skb->len;

	/*
	 * if we don't even have the mpa message, then bail.
	 */
	if (ep->mpa_pkt_len < sizeof(*mpa))
		return;
	mpa = (struct mpa_message *) ep->mpa_pkt;

	/* Validate MPA header. */
	if (mpa->revision > mpa_rev) {
		printk(KERN_ERR MOD "%s MPA version mismatch. Local = %d,"
		       " Received = %d\n", __func__, mpa_rev, mpa->revision);
		err = -EPROTO;
		goto err;
	}
	if (memcmp(mpa->key, MPA_KEY_REP, sizeof(mpa->key))) {
		err = -EPROTO;
		goto err;
	}

	plen = ntohs(mpa->private_data_size);

	/*
	 * Fail if there's too much private data.
	 */
	if (plen > MPA_MAX_PRIVATE_DATA) {
		err = -EPROTO;
		goto err;
	}

	/*
	 * If plen does not account for pkt size
	 */
	if (ep->mpa_pkt_len > (sizeof(*mpa) + plen)) {
		err = -EPROTO;
		goto err;
	}

	ep->plen = (u8) plen;

	/*
	 * If we don't have all the pdata yet, then bail.
	 * We'll continue process when more data arrives.
	 */
	if (ep->mpa_pkt_len < (sizeof(*mpa) + plen))
		return;

	if (mpa->flags & MPA_REJECT) {
		err = -ECONNREFUSED;
		goto err;
	}

	/*
	 * If we get here we have accumulated the entire mpa
	 * start reply message including private data. And
	 * the MPA header is valid.
	 */
	state_set(&ep->com, FPDU_MODE);
	ep->mpa_attr.crc_enabled = (mpa->flags & MPA_CRC) | crc_enabled ? 1 : 0;
	ep->mpa_attr.recv_marker_enabled = markers_enabled;
	ep->mpa_attr.xmit_marker_enabled = mpa->flags & MPA_MARKERS ? 1 : 0;
	ep->mpa_attr.version = mpa->revision;
	ep->mpa_attr.p2p_type = FW_RI_INIT_P2PTYPE_DISABLED;

	if (mpa->revision == 2) {
		ep->mpa_attr.enhanced_rdma_conn =
			mpa->flags & MPA_ENHANCED_RDMA_CONN ? 1 : 0;
		if (ep->mpa_attr.enhanced_rdma_conn) {
			mpa_v2_params = (struct mpa_v2_conn_params *)
				(ep->mpa_pkt + sizeof(*mpa));
			resp_ird = ntohs(mpa_v2_params->ird) &
				MPA_V2_IRD_ORD_MASK;
			resp_ord = ntohs(mpa_v2_params->ord) &
				MPA_V2_IRD_ORD_MASK;

			/*
			 * This is a double-check. Ideally, below checks are
			 * not required since ird/ord stuff has been taken
			 * care of in c4iw_accept_cr
			 */
			if ((ep->ird < resp_ord) || (ep->ord > resp_ird)) {
				err = -ENOMEM;
				ep->ird = resp_ord;
				ep->ord = resp_ird;
				insuff_ird = 1;
			}

			if (ntohs(mpa_v2_params->ird) &
					MPA_V2_PEER2PEER_MODEL) {
				if (ntohs(mpa_v2_params->ord) &
						MPA_V2_RDMA_WRITE_RTR)
					ep->mpa_attr.p2p_type =
						FW_RI_INIT_P2PTYPE_RDMA_WRITE;
				else if (ntohs(mpa_v2_params->ord) &
						MPA_V2_RDMA_READ_RTR)
					ep->mpa_attr.p2p_type =
						FW_RI_INIT_P2PTYPE_READ_REQ;
			}
		}
	} else if (mpa->revision == 1)
		if (peer2peer)
			ep->mpa_attr.p2p_type = p2p_type;

	PDBG("%s - crc_enabled=%d, recv_marker_enabled=%d, "
	     "xmit_marker_enabled=%d, version=%d p2p_type=%d local-p2p_type = "
	     "%d\n", __func__, ep->mpa_attr.crc_enabled,
	     ep->mpa_attr.recv_marker_enabled,
	     ep->mpa_attr.xmit_marker_enabled, ep->mpa_attr.version,
	     ep->mpa_attr.p2p_type, p2p_type);

	/*
	 * If responder's RTR does not match with that of initiator, assign
	 * FW_RI_INIT_P2PTYPE_DISABLED in mpa attributes so that RTR is not
	 * generated when moving QP to RTS state.
	 * A TERM message will be sent after QP has moved to RTS state
	 */
	if ((ep->mpa_attr.version == 2) && peer2peer &&
			(ep->mpa_attr.p2p_type != p2p_type)) {
		ep->mpa_attr.p2p_type = FW_RI_INIT_P2PTYPE_DISABLED;
		rtr_mismatch = 1;
	}

	attrs.mpa_attr = ep->mpa_attr;
	attrs.max_ird = ep->ird;
	attrs.max_ord = ep->ord;
	attrs.llp_stream_handle = ep;
	attrs.next_state = C4IW_QP_STATE_RTS;

	mask = C4IW_QP_ATTR_NEXT_STATE |
	    C4IW_QP_ATTR_LLP_STREAM_HANDLE | C4IW_QP_ATTR_MPA_ATTR |
	    C4IW_QP_ATTR_MAX_IRD | C4IW_QP_ATTR_MAX_ORD;

	/* bind QP and TID with INIT_WR */
	err = c4iw_modify_qp(ep->com.qp->rhp,
			     ep->com.qp, mask, &attrs, 1);
	if (err)
		goto err;

	/*
	 * If responder's RTR requirement did not match with what initiator
	 * supports, generate TERM message
	 */
	if (rtr_mismatch) {
		printk(KERN_ERR "%s: RTR mismatch, sending TERM\n", __func__);
		attrs.layer_etype = LAYER_MPA | DDP_LLP;
		attrs.ecode = MPA_NOMATCH_RTR;
		attrs.next_state = C4IW_QP_STATE_TERMINATE;
		err = c4iw_modify_qp(ep->com.qp->rhp, ep->com.qp,
				C4IW_QP_ATTR_NEXT_STATE, &attrs, 0);
		err = -ENOMEM;
		goto out;
	}

	/*
	 * Generate TERM if initiator IRD is not sufficient for responder
	 * provided ORD. Currently, we do the same behaviour even when
	 * responder provided IRD is also not sufficient as regards to
	 * initiator ORD.
	 */
	if (insuff_ird) {
		printk(KERN_ERR "%s: Insufficient IRD, sending TERM\n",
				__func__);
		attrs.layer_etype = LAYER_MPA | DDP_LLP;
		attrs.ecode = MPA_INSUFF_IRD;
		attrs.next_state = C4IW_QP_STATE_TERMINATE;
		err = c4iw_modify_qp(ep->com.qp->rhp, ep->com.qp,
				C4IW_QP_ATTR_NEXT_STATE, &attrs, 0);
		err = -ENOMEM;
		goto out;
	}
	goto out;
err:
	state_set(&ep->com, ABORTING);
	send_abort(ep, skb, GFP_KERNEL);
out:
	connect_reply_upcall(ep, err);
	return;
}

static void process_mpa_request(struct c4iw_ep *ep, struct sk_buff *skb)
{
	struct mpa_message *mpa;
	struct mpa_v2_conn_params *mpa_v2_params;
	u16 plen;

	PDBG("%s ep %p tid %u\n", __func__, ep, ep->hwtid);

	if (state_read(&ep->com) != MPA_REQ_WAIT)
		return;

	/*
	 * If we get more than the supported amount of private data
	 * then we must fail this connection.
	 */
	if (ep->mpa_pkt_len + skb->len > sizeof(ep->mpa_pkt)) {
		stop_ep_timer(ep);
		abort_connection(ep, skb, GFP_KERNEL);
		return;
	}

	PDBG("%s enter (%s line %u)\n", __func__, __FILE__, __LINE__);

	/*
	 * Copy the new data into our accumulation buffer.
	 */
	skb_copy_from_linear_data(skb, &(ep->mpa_pkt[ep->mpa_pkt_len]),
				  skb->len);
	ep->mpa_pkt_len += skb->len;

	/*
	 * If we don't even have the mpa message, then bail.
	 * We'll continue process when more data arrives.
	 */
	if (ep->mpa_pkt_len < sizeof(*mpa))
		return;

	PDBG("%s enter (%s line %u)\n", __func__, __FILE__, __LINE__);
	stop_ep_timer(ep);
	mpa = (struct mpa_message *) ep->mpa_pkt;

	/*
	 * Validate MPA Header.
	 */
	if (mpa->revision > mpa_rev) {
		printk(KERN_ERR MOD "%s MPA version mismatch. Local = %d,"
		       " Received = %d\n", __func__, mpa_rev, mpa->revision);
		abort_connection(ep, skb, GFP_KERNEL);
		return;
	}

	if (memcmp(mpa->key, MPA_KEY_REQ, sizeof(mpa->key))) {
		abort_connection(ep, skb, GFP_KERNEL);
		return;
	}

	plen = ntohs(mpa->private_data_size);

	/*
	 * Fail if there's too much private data.
	 */
	if (plen > MPA_MAX_PRIVATE_DATA) {
		abort_connection(ep, skb, GFP_KERNEL);
		return;
	}

	/*
	 * If plen does not account for pkt size
	 */
	if (ep->mpa_pkt_len > (sizeof(*mpa) + plen)) {
		abort_connection(ep, skb, GFP_KERNEL);
		return;
	}
	ep->plen = (u8) plen;

	/*
	 * If we don't have all the pdata yet, then bail.
	 */
	if (ep->mpa_pkt_len < (sizeof(*mpa) + plen))
		return;

	/*
	 * If we get here we have accumulated the entire mpa
	 * start reply message including private data.
	 */
	ep->mpa_attr.initiator = 0;
	ep->mpa_attr.crc_enabled = (mpa->flags & MPA_CRC) | crc_enabled ? 1 : 0;
	ep->mpa_attr.recv_marker_enabled = markers_enabled;
	ep->mpa_attr.xmit_marker_enabled = mpa->flags & MPA_MARKERS ? 1 : 0;
	ep->mpa_attr.version = mpa->revision;
	if (mpa->revision == 1)
		ep->tried_with_mpa_v1 = 1;
	ep->mpa_attr.p2p_type = FW_RI_INIT_P2PTYPE_DISABLED;

	if (mpa->revision == 2) {
		ep->mpa_attr.enhanced_rdma_conn =
			mpa->flags & MPA_ENHANCED_RDMA_CONN ? 1 : 0;
		if (ep->mpa_attr.enhanced_rdma_conn) {
			mpa_v2_params = (struct mpa_v2_conn_params *)
				(ep->mpa_pkt + sizeof(*mpa));
			ep->ird = ntohs(mpa_v2_params->ird) &
				MPA_V2_IRD_ORD_MASK;
			ep->ord = ntohs(mpa_v2_params->ord) &
				MPA_V2_IRD_ORD_MASK;
			if (ntohs(mpa_v2_params->ird) & MPA_V2_PEER2PEER_MODEL)
				if (peer2peer) {
					if (ntohs(mpa_v2_params->ord) &
							MPA_V2_RDMA_WRITE_RTR)
						ep->mpa_attr.p2p_type =
						FW_RI_INIT_P2PTYPE_RDMA_WRITE;
					else if (ntohs(mpa_v2_params->ord) &
							MPA_V2_RDMA_READ_RTR)
						ep->mpa_attr.p2p_type =
						FW_RI_INIT_P2PTYPE_READ_REQ;
				}
		}
	} else if (mpa->revision == 1)
		if (peer2peer)
			ep->mpa_attr.p2p_type = p2p_type;

	PDBG("%s - crc_enabled=%d, recv_marker_enabled=%d, "
	     "xmit_marker_enabled=%d, version=%d p2p_type=%d\n", __func__,
	     ep->mpa_attr.crc_enabled, ep->mpa_attr.recv_marker_enabled,
	     ep->mpa_attr.xmit_marker_enabled, ep->mpa_attr.version,
	     ep->mpa_attr.p2p_type);

	state_set(&ep->com, MPA_REQ_RCVD);

	/* drive upcall */
	connect_request_upcall(ep);
	return;
}

static int rx_data(struct c4iw_dev *dev, struct sk_buff *skb)
{
	struct c4iw_ep *ep;
	struct cpl_rx_data *hdr = cplhdr(skb);
	unsigned int dlen = ntohs(hdr->len);
	unsigned int tid = GET_TID(hdr);
	struct tid_info *t = dev->rdev.lldi.tids;

	ep = lookup_tid(t, tid);
	PDBG("%s ep %p tid %u dlen %u\n", __func__, ep, ep->hwtid, dlen);
	skb_pull(skb, sizeof(*hdr));
	skb_trim(skb, dlen);

	ep->rcv_seq += dlen;
	BUG_ON(ep->rcv_seq != (ntohl(hdr->seq) + dlen));

	/* update RX credits */
	update_rx_credits(ep, dlen);

	switch (state_read(&ep->com)) {
	case MPA_REQ_SENT:
		process_mpa_reply(ep, skb);
		break;
	case MPA_REQ_WAIT:
		process_mpa_request(ep, skb);
		break;
	case MPA_REP_SENT:
		break;
	default:
		printk(KERN_ERR MOD "%s Unexpected streaming data."
		       " ep %p state %d tid %u\n",
		       __func__, ep, state_read(&ep->com), ep->hwtid);

		/*
		 * The ep will timeout and inform the ULP of the failure.
		 * See ep_timeout().
		 */
		break;
	}
	return 0;
}

static int abort_rpl(struct c4iw_dev *dev, struct sk_buff *skb)
{
	struct c4iw_ep *ep;
	struct cpl_abort_rpl_rss *rpl = cplhdr(skb);
	int release = 0;
	unsigned int tid = GET_TID(rpl);
	struct tid_info *t = dev->rdev.lldi.tids;

	ep = lookup_tid(t, tid);
	PDBG("%s ep %p tid %u\n", __func__, ep, ep->hwtid);
	BUG_ON(!ep);
	mutex_lock(&ep->com.mutex);
	switch (ep->com.state) {
	case ABORTING:
		__state_set(&ep->com, DEAD);
		release = 1;
		break;
	default:
		printk(KERN_ERR "%s ep %p state %d\n",
		     __func__, ep, ep->com.state);
		break;
	}
	mutex_unlock(&ep->com.mutex);

	if (release)
		release_ep_resources(ep);
	return 0;
}

/*
 * Return whether a failed active open has allocated a TID
 */
static inline int act_open_has_tid(int status)
{
	return status != CPL_ERR_TCAM_FULL && status != CPL_ERR_CONN_EXIST &&
	       status != CPL_ERR_ARP_MISS;
}

static int act_open_rpl(struct c4iw_dev *dev, struct sk_buff *skb)
{
	struct c4iw_ep *ep;
	struct cpl_act_open_rpl *rpl = cplhdr(skb);
	unsigned int atid = GET_TID_TID(GET_AOPEN_ATID(
					ntohl(rpl->atid_status)));
	struct tid_info *t = dev->rdev.lldi.tids;
	int status = GET_AOPEN_STATUS(ntohl(rpl->atid_status));

	ep = lookup_atid(t, atid);

	PDBG("%s ep %p atid %u status %u errno %d\n", __func__, ep, atid,
	     status, status2errno(status));

	if (status == CPL_ERR_RTX_NEG_ADVICE) {
		printk(KERN_WARNING MOD "Connection problems for atid %u\n",
			atid);
		return 0;
	}

	connect_reply_upcall(ep, status2errno(status));
	state_set(&ep->com, DEAD);

	if (status && act_open_has_tid(status))
		cxgb4_remove_tid(ep->com.dev->rdev.lldi.tids, 0, GET_TID(rpl));

	cxgb4_free_atid(t, atid);
	dst_release(ep->dst);
	cxgb4_l2t_release(ep->l2t);
	c4iw_put_ep(&ep->com);

	return 0;
}

static int pass_open_rpl(struct c4iw_dev *dev, struct sk_buff *skb)
{
	struct cpl_pass_open_rpl *rpl = cplhdr(skb);
	struct tid_info *t = dev->rdev.lldi.tids;
	unsigned int stid = GET_TID(rpl);
	struct c4iw_listen_ep *ep = lookup_stid(t, stid);

	if (!ep) {
		printk(KERN_ERR MOD "stid %d lookup failure!\n", stid);
		return 0;
	}
	PDBG("%s ep %p status %d error %d\n", __func__, ep,
	     rpl->status, status2errno(rpl->status));
	c4iw_wake_up(&ep->com.wr_wait, status2errno(rpl->status));

	return 0;
}

static int listen_stop(struct c4iw_listen_ep *ep)
{
	struct sk_buff *skb;
	struct cpl_close_listsvr_req *req;

	PDBG("%s ep %p\n", __func__, ep);
	skb = get_skb(NULL, sizeof(*req), GFP_KERNEL);
	if (!skb) {
		printk(KERN_ERR MOD "%s - failed to alloc skb\n", __func__);
		return -ENOMEM;
	}
	req = (struct cpl_close_listsvr_req *) skb_put(skb, sizeof(*req));
	INIT_TP_WR(req, 0);
	OPCODE_TID(req) = cpu_to_be32(MK_OPCODE_TID(CPL_CLOSE_LISTSRV_REQ,
						    ep->stid));
	req->reply_ctrl = cpu_to_be16(
			  QUEUENO(ep->com.dev->rdev.lldi.rxq_ids[0]));
	set_wr_txq(skb, CPL_PRIORITY_SETUP, 0);
	return c4iw_ofld_send(&ep->com.dev->rdev, skb);
}

static int close_listsrv_rpl(struct c4iw_dev *dev, struct sk_buff *skb)
{
	struct cpl_close_listsvr_rpl *rpl = cplhdr(skb);
	struct tid_info *t = dev->rdev.lldi.tids;
	unsigned int stid = GET_TID(rpl);
	struct c4iw_listen_ep *ep = lookup_stid(t, stid);

	PDBG("%s ep %p\n", __func__, ep);
	c4iw_wake_up(&ep->com.wr_wait, status2errno(rpl->status));
	return 0;
}

static void accept_cr(struct c4iw_ep *ep, __be32 peer_ip, struct sk_buff *skb,
		      struct cpl_pass_accept_req *req)
{
	struct cpl_pass_accept_rpl *rpl;
	unsigned int mtu_idx;
	u64 opt0;
	u32 opt2;
	int wscale;

	PDBG("%s ep %p tid %u\n", __func__, ep, ep->hwtid);
	BUG_ON(skb_cloned(skb));
	skb_trim(skb, sizeof(*rpl));
	skb_get(skb);
	cxgb4_best_mtu(ep->com.dev->rdev.lldi.mtus, ep->mtu, &mtu_idx);
	wscale = compute_wscale(rcv_win);
	opt0 = KEEP_ALIVE(1) |
	       DELACK(1) |
	       WND_SCALE(wscale) |
	       MSS_IDX(mtu_idx) |
	       L2T_IDX(ep->l2t->idx) |
	       TX_CHAN(ep->tx_chan) |
	       SMAC_SEL(ep->smac_idx) |
	       DSCP(ep->tos) |
	       ULP_MODE(ULP_MODE_TCPDDP) |
	       RCV_BUFSIZ(rcv_win>>10);
	opt2 = RX_CHANNEL(0) |
	       RSS_QUEUE_VALID | RSS_QUEUE(ep->rss_qid);

	if (enable_tcp_timestamps && req->tcpopt.tstamp)
		opt2 |= TSTAMPS_EN(1);
	if (enable_tcp_sack && req->tcpopt.sack)
		opt2 |= SACK_EN(1);
	if (wscale && enable_tcp_window_scaling)
		opt2 |= WND_SCALE_EN(1);

	rpl = cplhdr(skb);
	INIT_TP_WR(rpl, ep->hwtid);
	OPCODE_TID(rpl) = cpu_to_be32(MK_OPCODE_TID(CPL_PASS_ACCEPT_RPL,
				      ep->hwtid));
	rpl->opt0 = cpu_to_be64(opt0);
	rpl->opt2 = cpu_to_be32(opt2);
	set_wr_txq(skb, CPL_PRIORITY_SETUP, ep->ctrlq_idx);
	c4iw_l2t_send(&ep->com.dev->rdev, skb, ep->l2t);

	return;
}

static void reject_cr(struct c4iw_dev *dev, u32 hwtid, __be32 peer_ip,
		      struct sk_buff *skb)
{
	PDBG("%s c4iw_dev %p tid %u peer_ip %x\n", __func__, dev, hwtid,
	     peer_ip);
	BUG_ON(skb_cloned(skb));
	skb_trim(skb, sizeof(struct cpl_tid_release));
	skb_get(skb);
	release_tid(&dev->rdev, hwtid, skb);
	return;
}

static void get_4tuple(struct cpl_pass_accept_req *req,
		       __be32 *local_ip, __be32 *peer_ip,
		       __be16 *local_port, __be16 *peer_port)
{
	int eth_len = G_ETH_HDR_LEN(be32_to_cpu(req->hdr_len));
	int ip_len = G_IP_HDR_LEN(be32_to_cpu(req->hdr_len));
	struct iphdr *ip = (struct iphdr *)((u8 *)(req + 1) + eth_len);
	struct tcphdr *tcp = (struct tcphdr *)
			     ((u8 *)(req + 1) + eth_len + ip_len);

	PDBG("%s saddr 0x%x daddr 0x%x sport %u dport %u\n", __func__,
	     ntohl(ip->saddr), ntohl(ip->daddr), ntohs(tcp->source),
	     ntohs(tcp->dest));

	*peer_ip = ip->saddr;
	*local_ip = ip->daddr;
	*peer_port = tcp->source;
	*local_port = tcp->dest;

	return;
}

static int import_ep(struct c4iw_ep *ep, __be32 peer_ip, struct dst_entry *dst,
		     struct c4iw_dev *cdev, bool clear_mpa_v1)
{
	struct neighbour *n;
	int err, step;

	n = dst_neigh_lookup(dst, &peer_ip);
	if (!n)
		return -ENODEV;

	rcu_read_lock();
	err = -ENOMEM;
	if (n->dev->flags & IFF_LOOPBACK) {
		struct net_device *pdev;

		pdev = ip_dev_find(&init_net, peer_ip);
		ep->l2t = cxgb4_l2t_get(cdev->rdev.lldi.l2t,
					n, pdev, 0);
		if (!ep->l2t)
			goto out;
		ep->mtu = pdev->mtu;
		ep->tx_chan = cxgb4_port_chan(pdev);
		ep->smac_idx = (cxgb4_port_viid(pdev) & 0x7F) << 1;
		step = cdev->rdev.lldi.ntxq /
			cdev->rdev.lldi.nchan;
		ep->txq_idx = cxgb4_port_idx(pdev) * step;
		step = cdev->rdev.lldi.nrxq /
			cdev->rdev.lldi.nchan;
		ep->ctrlq_idx = cxgb4_port_idx(pdev);
		ep->rss_qid = cdev->rdev.lldi.rxq_ids[
			cxgb4_port_idx(pdev) * step];
		dev_put(pdev);
	} else {
		ep->l2t = cxgb4_l2t_get(cdev->rdev.lldi.l2t,
					n, n->dev, 0);
		if (!ep->l2t)
			goto out;
		ep->mtu = dst_mtu(ep->dst);
		ep->tx_chan = cxgb4_port_chan(n->dev);
		ep->smac_idx = (cxgb4_port_viid(n->dev) & 0x7F) << 1;
		step = cdev->rdev.lldi.ntxq /
			cdev->rdev.lldi.nchan;
		ep->txq_idx = cxgb4_port_idx(n->dev) * step;
		ep->ctrlq_idx = cxgb4_port_idx(n->dev);
		step = cdev->rdev.lldi.nrxq /
			cdev->rdev.lldi.nchan;
		ep->rss_qid = cdev->rdev.lldi.rxq_ids[
			cxgb4_port_idx(n->dev) * step];

		if (clear_mpa_v1) {
			ep->retry_with_mpa_v1 = 0;
			ep->tried_with_mpa_v1 = 0;
		}
	}
	err = 0;
out:
	rcu_read_unlock();

	neigh_release(n);

	return err;
}

static int pass_accept_req(struct c4iw_dev *dev, struct sk_buff *skb)
{
	struct c4iw_ep *child_ep, *parent_ep;
	struct cpl_pass_accept_req *req = cplhdr(skb);
	unsigned int stid = GET_POPEN_TID(ntohl(req->tos_stid));
	struct tid_info *t = dev->rdev.lldi.tids;
	unsigned int hwtid = GET_TID(req);
	struct dst_entry *dst;
	struct rtable *rt;
	__be32 local_ip, peer_ip;
	__be16 local_port, peer_port;
	int err;

	parent_ep = lookup_stid(t, stid);
	PDBG("%s parent ep %p tid %u\n", __func__, parent_ep, hwtid);

	get_4tuple(req, &local_ip, &peer_ip, &local_port, &peer_port);

	if (state_read(&parent_ep->com) != LISTEN) {
		printk(KERN_ERR "%s - listening ep not in LISTEN\n",
		       __func__);
		goto reject;
	}

	/* Find output route */
	rt = find_route(dev, local_ip, peer_ip, local_port, peer_port,
			GET_POPEN_TOS(ntohl(req->tos_stid)));
	if (!rt) {
		printk(KERN_ERR MOD "%s - failed to find dst entry!\n",
		       __func__);
		goto reject;
	}
	dst = &rt->dst;

	child_ep = alloc_ep(sizeof(*child_ep), GFP_KERNEL);
	if (!child_ep) {
		printk(KERN_ERR MOD "%s - failed to allocate ep entry!\n",
		       __func__);
		dst_release(dst);
		goto reject;
	}

	err = import_ep(child_ep, peer_ip, dst, dev, false);
	if (err) {
		printk(KERN_ERR MOD "%s - failed to allocate l2t entry!\n",
		       __func__);
		dst_release(dst);
		kfree(child_ep);
		goto reject;
	}

	state_set(&child_ep->com, CONNECTING);
	child_ep->com.dev = dev;
	child_ep->com.cm_id = NULL;
	child_ep->com.local_addr.sin_family = PF_INET;
	child_ep->com.local_addr.sin_port = local_port;
	child_ep->com.local_addr.sin_addr.s_addr = local_ip;
	child_ep->com.remote_addr.sin_family = PF_INET;
	child_ep->com.remote_addr.sin_port = peer_port;
	child_ep->com.remote_addr.sin_addr.s_addr = peer_ip;
	c4iw_get_ep(&parent_ep->com);
	child_ep->parent_ep = parent_ep;
	child_ep->tos = GET_POPEN_TOS(ntohl(req->tos_stid));
	child_ep->dst = dst;
	child_ep->hwtid = hwtid;

	PDBG("%s tx_chan %u smac_idx %u rss_qid %u\n", __func__,
	     child_ep->tx_chan, child_ep->smac_idx, child_ep->rss_qid);

	init_timer(&child_ep->timer);
	cxgb4_insert_tid(t, child_ep, hwtid);
	accept_cr(child_ep, peer_ip, skb, req);
	goto out;
reject:
	reject_cr(dev, hwtid, peer_ip, skb);
out:
	return 0;
}

static int pass_establish(struct c4iw_dev *dev, struct sk_buff *skb)
{
	struct c4iw_ep *ep;
	struct cpl_pass_establish *req = cplhdr(skb);
	struct tid_info *t = dev->rdev.lldi.tids;
	unsigned int tid = GET_TID(req);

	ep = lookup_tid(t, tid);
	PDBG("%s ep %p tid %u\n", __func__, ep, ep->hwtid);
	ep->snd_seq = be32_to_cpu(req->snd_isn);
	ep->rcv_seq = be32_to_cpu(req->rcv_isn);

	set_emss(ep, ntohs(req->tcp_opt));

	dst_confirm(ep->dst);
	state_set(&ep->com, MPA_REQ_WAIT);
	start_ep_timer(ep);
	send_flowc(ep, skb);

	return 0;
}

static int peer_close(struct c4iw_dev *dev, struct sk_buff *skb)
{
	struct cpl_peer_close *hdr = cplhdr(skb);
	struct c4iw_ep *ep;
	struct c4iw_qp_attributes attrs;
	int disconnect = 1;
	int release = 0;
	struct tid_info *t = dev->rdev.lldi.tids;
	unsigned int tid = GET_TID(hdr);
	int ret;

	ep = lookup_tid(t, tid);
	PDBG("%s ep %p tid %u\n", __func__, ep, ep->hwtid);
	dst_confirm(ep->dst);

	mutex_lock(&ep->com.mutex);
	switch (ep->com.state) {
	case MPA_REQ_WAIT:
		__state_set(&ep->com, CLOSING);
		break;
	case MPA_REQ_SENT:
		__state_set(&ep->com, CLOSING);
		connect_reply_upcall(ep, -ECONNRESET);
		break;
	case MPA_REQ_RCVD:

		/*
		 * We're gonna mark this puppy DEAD, but keep
		 * the reference on it until the ULP accepts or
		 * rejects the CR. Also wake up anyone waiting
		 * in rdma connection migration (see c4iw_accept_cr()).
		 */
		__state_set(&ep->com, CLOSING);
		PDBG("waking up ep %p tid %u\n", ep, ep->hwtid);
		c4iw_wake_up(&ep->com.wr_wait, -ECONNRESET);
		break;
	case MPA_REP_SENT:
		__state_set(&ep->com, CLOSING);
		PDBG("waking up ep %p tid %u\n", ep, ep->hwtid);
		c4iw_wake_up(&ep->com.wr_wait, -ECONNRESET);
		break;
	case FPDU_MODE:
		start_ep_timer(ep);
		__state_set(&ep->com, CLOSING);
		attrs.next_state = C4IW_QP_STATE_CLOSING;
		ret = c4iw_modify_qp(ep->com.qp->rhp, ep->com.qp,
				       C4IW_QP_ATTR_NEXT_STATE, &attrs, 1);
		if (ret != -ECONNRESET) {
			peer_close_upcall(ep);
			disconnect = 1;
		}
		break;
	case ABORTING:
		disconnect = 0;
		break;
	case CLOSING:
		__state_set(&ep->com, MORIBUND);
		disconnect = 0;
		break;
	case MORIBUND:
		stop_ep_timer(ep);
		if (ep->com.cm_id && ep->com.qp) {
			attrs.next_state = C4IW_QP_STATE_IDLE;
			c4iw_modify_qp(ep->com.qp->rhp, ep->com.qp,
				       C4IW_QP_ATTR_NEXT_STATE, &attrs, 1);
		}
		close_complete_upcall(ep);
		__state_set(&ep->com, DEAD);
		release = 1;
		disconnect = 0;
		break;
	case DEAD:
		disconnect = 0;
		break;
	default:
		BUG_ON(1);
	}
	mutex_unlock(&ep->com.mutex);
	if (disconnect)
		c4iw_ep_disconnect(ep, 0, GFP_KERNEL);
	if (release)
		release_ep_resources(ep);
	return 0;
}

/*
 * Returns whether an ABORT_REQ_RSS message is a negative advice.
 */
static int is_neg_adv_abort(unsigned int status)
{
	return status == CPL_ERR_RTX_NEG_ADVICE ||
	       status == CPL_ERR_PERSIST_NEG_ADVICE;
}

static int c4iw_reconnect(struct c4iw_ep *ep)
{
	struct rtable *rt;
	int err = 0;

	PDBG("%s qp %p cm_id %p\n", __func__, ep->com.qp, ep->com.cm_id);
	init_timer(&ep->timer);

	/*
	 * Allocate an active TID to initiate a TCP connection.
	 */
	ep->atid = cxgb4_alloc_atid(ep->com.dev->rdev.lldi.tids, ep);
	if (ep->atid == -1) {
		printk(KERN_ERR MOD "%s - cannot alloc atid.\n", __func__);
		err = -ENOMEM;
		goto fail2;
	}

	/* find a route */
	rt = find_route(ep->com.dev,
			ep->com.cm_id->local_addr.sin_addr.s_addr,
			ep->com.cm_id->remote_addr.sin_addr.s_addr,
			ep->com.cm_id->local_addr.sin_port,
			ep->com.cm_id->remote_addr.sin_port, 0);
	if (!rt) {
		printk(KERN_ERR MOD "%s - cannot find route.\n", __func__);
		err = -EHOSTUNREACH;
		goto fail3;
	}
	ep->dst = &rt->dst;

	err = import_ep(ep, ep->com.cm_id->remote_addr.sin_addr.s_addr,
			ep->dst, ep->com.dev, false);
	if (err) {
		printk(KERN_ERR MOD "%s - cannot alloc l2e.\n", __func__);
		goto fail4;
	}

	PDBG("%s txq_idx %u tx_chan %u smac_idx %u rss_qid %u l2t_idx %u\n",
	     __func__, ep->txq_idx, ep->tx_chan, ep->smac_idx, ep->rss_qid,
	     ep->l2t->idx);

	state_set(&ep->com, CONNECTING);
	ep->tos = 0;

	/* send connect request to rnic */
	err = send_connect(ep);
	if (!err)
		goto out;

	cxgb4_l2t_release(ep->l2t);
fail4:
	dst_release(ep->dst);
fail3:
	cxgb4_free_atid(ep->com.dev->rdev.lldi.tids, ep->atid);
fail2:
	/*
	 * remember to send notification to upper layer.
	 * We are in here so the upper layer is not aware that this is
	 * re-connect attempt and so, upper layer is still waiting for
	 * response of 1st connect request.
	 */
	connect_reply_upcall(ep, -ECONNRESET);
	c4iw_put_ep(&ep->com);
out:
	return err;
}

static int peer_abort(struct c4iw_dev *dev, struct sk_buff *skb)
{
	struct cpl_abort_req_rss *req = cplhdr(skb);
	struct c4iw_ep *ep;
	struct cpl_abort_rpl *rpl;
	struct sk_buff *rpl_skb;
	struct c4iw_qp_attributes attrs;
	int ret;
	int release = 0;
	struct tid_info *t = dev->rdev.lldi.tids;
	unsigned int tid = GET_TID(req);

	ep = lookup_tid(t, tid);
	if (is_neg_adv_abort(req->status)) {
		PDBG("%s neg_adv_abort ep %p tid %u\n", __func__, ep,
		     ep->hwtid);
		return 0;
	}
	PDBG("%s ep %p tid %u state %u\n", __func__, ep, ep->hwtid,
	     ep->com.state);

	/*
	 * Wake up any threads in rdma_init() or rdma_fini().
	 * However, this is not needed if com state is just
	 * MPA_REQ_SENT
	 */
	if (ep->com.state != MPA_REQ_SENT)
		c4iw_wake_up(&ep->com.wr_wait, -ECONNRESET);

	mutex_lock(&ep->com.mutex);
	switch (ep->com.state) {
	case CONNECTING:
		break;
	case MPA_REQ_WAIT:
		stop_ep_timer(ep);
		break;
	case MPA_REQ_SENT:
		stop_ep_timer(ep);
		if (mpa_rev == 2 && ep->tried_with_mpa_v1)
			connect_reply_upcall(ep, -ECONNRESET);
		else {
			/*
			 * we just don't send notification upwards because we
			 * want to retry with mpa_v1 without upper layers even
			 * knowing it.
			 *
			 * do some housekeeping so as to re-initiate the
			 * connection
			 */
			PDBG("%s: mpa_rev=%d. Retrying with mpav1\n", __func__,
			     mpa_rev);
			ep->retry_with_mpa_v1 = 1;
		}
		break;
	case MPA_REP_SENT:
		break;
	case MPA_REQ_RCVD:
		break;
	case MORIBUND:
	case CLOSING:
		stop_ep_timer(ep);
		/*FALLTHROUGH*/
	case FPDU_MODE:
		if (ep->com.cm_id && ep->com.qp) {
			attrs.next_state = C4IW_QP_STATE_ERROR;
			ret = c4iw_modify_qp(ep->com.qp->rhp,
				     ep->com.qp, C4IW_QP_ATTR_NEXT_STATE,
				     &attrs, 1);
			if (ret)
				printk(KERN_ERR MOD
				       "%s - qp <- error failed!\n",
				       __func__);
		}
		peer_abort_upcall(ep);
		break;
	case ABORTING:
		break;
	case DEAD:
		PDBG("%s PEER_ABORT IN DEAD STATE!!!!\n", __func__);
		mutex_unlock(&ep->com.mutex);
		return 0;
	default:
		BUG_ON(1);
		break;
	}
	dst_confirm(ep->dst);
	if (ep->com.state != ABORTING) {
		__state_set(&ep->com, DEAD);
		/* we don't release if we want to retry with mpa_v1 */
		if (!ep->retry_with_mpa_v1)
			release = 1;
	}
	mutex_unlock(&ep->com.mutex);

	rpl_skb = get_skb(skb, sizeof(*rpl), GFP_KERNEL);
	if (!rpl_skb) {
		printk(KERN_ERR MOD "%s - cannot allocate skb!\n",
		       __func__);
		release = 1;
		goto out;
	}
	set_wr_txq(skb, CPL_PRIORITY_DATA, ep->txq_idx);
	rpl = (struct cpl_abort_rpl *) skb_put(rpl_skb, sizeof(*rpl));
	INIT_TP_WR(rpl, ep->hwtid);
	OPCODE_TID(rpl) = cpu_to_be32(MK_OPCODE_TID(CPL_ABORT_RPL, ep->hwtid));
	rpl->cmd = CPL_ABORT_NO_RST;
	c4iw_ofld_send(&ep->com.dev->rdev, rpl_skb);
out:
	if (release)
		release_ep_resources(ep);

	/* retry with mpa-v1 */
	if (ep && ep->retry_with_mpa_v1) {
		cxgb4_remove_tid(ep->com.dev->rdev.lldi.tids, 0, ep->hwtid);
		dst_release(ep->dst);
		cxgb4_l2t_release(ep->l2t);
		c4iw_reconnect(ep);
	}

	return 0;
}

static int close_con_rpl(struct c4iw_dev *dev, struct sk_buff *skb)
{
	struct c4iw_ep *ep;
	struct c4iw_qp_attributes attrs;
	struct cpl_close_con_rpl *rpl = cplhdr(skb);
	int release = 0;
	struct tid_info *t = dev->rdev.lldi.tids;
	unsigned int tid = GET_TID(rpl);

	ep = lookup_tid(t, tid);

	PDBG("%s ep %p tid %u\n", __func__, ep, ep->hwtid);
	BUG_ON(!ep);

	/* The cm_id may be null if we failed to connect */
	mutex_lock(&ep->com.mutex);
	switch (ep->com.state) {
	case CLOSING:
		__state_set(&ep->com, MORIBUND);
		break;
	case MORIBUND:
		stop_ep_timer(ep);
		if ((ep->com.cm_id) && (ep->com.qp)) {
			attrs.next_state = C4IW_QP_STATE_IDLE;
			c4iw_modify_qp(ep->com.qp->rhp,
					     ep->com.qp,
					     C4IW_QP_ATTR_NEXT_STATE,
					     &attrs, 1);
		}
		close_complete_upcall(ep);
		__state_set(&ep->com, DEAD);
		release = 1;
		break;
	case ABORTING:
	case DEAD:
		break;
	default:
		BUG_ON(1);
		break;
	}
	mutex_unlock(&ep->com.mutex);
	if (release)
		release_ep_resources(ep);
	return 0;
}

static int terminate(struct c4iw_dev *dev, struct sk_buff *skb)
{
	struct cpl_rdma_terminate *rpl = cplhdr(skb);
	struct tid_info *t = dev->rdev.lldi.tids;
	unsigned int tid = GET_TID(rpl);
	struct c4iw_ep *ep;
	struct c4iw_qp_attributes attrs;

	ep = lookup_tid(t, tid);
	BUG_ON(!ep);

	if (ep && ep->com.qp) {
		printk(KERN_WARNING MOD "TERM received tid %u qpid %u\n", tid,
		       ep->com.qp->wq.sq.qid);
		attrs.next_state = C4IW_QP_STATE_TERMINATE;
		c4iw_modify_qp(ep->com.qp->rhp, ep->com.qp,
			       C4IW_QP_ATTR_NEXT_STATE, &attrs, 1);
	} else
		printk(KERN_WARNING MOD "TERM received tid %u no ep/qp\n", tid);

	return 0;
}

/*
 * Upcall from the adapter indicating data has been transmitted.
 * For us its just the single MPA request or reply.  We can now free
 * the skb holding the mpa message.
 */
static int fw4_ack(struct c4iw_dev *dev, struct sk_buff *skb)
{
	struct c4iw_ep *ep;
	struct cpl_fw4_ack *hdr = cplhdr(skb);
	u8 credits = hdr->credits;
	unsigned int tid = GET_TID(hdr);
	struct tid_info *t = dev->rdev.lldi.tids;


	ep = lookup_tid(t, tid);
	PDBG("%s ep %p tid %u credits %u\n", __func__, ep, ep->hwtid, credits);
	if (credits == 0) {
		PDBG("%s 0 credit ack ep %p tid %u state %u\n",
		     __func__, ep, ep->hwtid, state_read(&ep->com));
		return 0;
	}

	dst_confirm(ep->dst);
	if (ep->mpa_skb) {
		PDBG("%s last streaming msg ack ep %p tid %u state %u "
		     "initiator %u freeing skb\n", __func__, ep, ep->hwtid,
		     state_read(&ep->com), ep->mpa_attr.initiator ? 1 : 0);
		kfree_skb(ep->mpa_skb);
		ep->mpa_skb = NULL;
	}
	return 0;
}

int c4iw_reject_cr(struct iw_cm_id *cm_id, const void *pdata, u8 pdata_len)
{
	int err;
	struct c4iw_ep *ep = to_ep(cm_id);
	PDBG("%s ep %p tid %u\n", __func__, ep, ep->hwtid);

	if (state_read(&ep->com) == DEAD) {
		c4iw_put_ep(&ep->com);
		return -ECONNRESET;
	}
	BUG_ON(state_read(&ep->com) != MPA_REQ_RCVD);
	if (mpa_rev == 0)
		abort_connection(ep, NULL, GFP_KERNEL);
	else {
		err = send_mpa_reject(ep, pdata, pdata_len);
		err = c4iw_ep_disconnect(ep, 0, GFP_KERNEL);
	}
	c4iw_put_ep(&ep->com);
	return 0;
}

int c4iw_accept_cr(struct iw_cm_id *cm_id, struct iw_cm_conn_param *conn_param)
{
	int err;
	struct c4iw_qp_attributes attrs;
	enum c4iw_qp_attr_mask mask;
	struct c4iw_ep *ep = to_ep(cm_id);
	struct c4iw_dev *h = to_c4iw_dev(cm_id->device);
	struct c4iw_qp *qp = get_qhp(h, conn_param->qpn);

	PDBG("%s ep %p tid %u\n", __func__, ep, ep->hwtid);
	if (state_read(&ep->com) == DEAD) {
		err = -ECONNRESET;
		goto err;
	}

	BUG_ON(state_read(&ep->com) != MPA_REQ_RCVD);
	BUG_ON(!qp);

	if ((conn_param->ord > c4iw_max_read_depth) ||
	    (conn_param->ird > c4iw_max_read_depth)) {
		abort_connection(ep, NULL, GFP_KERNEL);
		err = -EINVAL;
		goto err;
	}

	if (ep->mpa_attr.version == 2 && ep->mpa_attr.enhanced_rdma_conn) {
		if (conn_param->ord > ep->ird) {
			ep->ird = conn_param->ird;
			ep->ord = conn_param->ord;
			send_mpa_reject(ep, conn_param->private_data,
					conn_param->private_data_len);
			abort_connection(ep, NULL, GFP_KERNEL);
			err = -ENOMEM;
			goto err;
		}
		if (conn_param->ird > ep->ord) {
			if (!ep->ord)
				conn_param->ird = 1;
			else {
				abort_connection(ep, NULL, GFP_KERNEL);
				err = -ENOMEM;
				goto err;
			}
		}

	}
	ep->ird = conn_param->ird;
	ep->ord = conn_param->ord;

	if (ep->mpa_attr.version != 2)
		if (peer2peer && ep->ird == 0)
			ep->ird = 1;

	PDBG("%s %d ird %d ord %d\n", __func__, __LINE__, ep->ird, ep->ord);

	cm_id->add_ref(cm_id);
	ep->com.cm_id = cm_id;
	ep->com.qp = qp;

	/* bind QP to EP and move to RTS */
	attrs.mpa_attr = ep->mpa_attr;
	attrs.max_ird = ep->ird;
	attrs.max_ord = ep->ord;
	attrs.llp_stream_handle = ep;
	attrs.next_state = C4IW_QP_STATE_RTS;

	/* bind QP and TID with INIT_WR */
	mask = C4IW_QP_ATTR_NEXT_STATE |
			     C4IW_QP_ATTR_LLP_STREAM_HANDLE |
			     C4IW_QP_ATTR_MPA_ATTR |
			     C4IW_QP_ATTR_MAX_IRD |
			     C4IW_QP_ATTR_MAX_ORD;

	err = c4iw_modify_qp(ep->com.qp->rhp,
			     ep->com.qp, mask, &attrs, 1);
	if (err)
		goto err1;
	err = send_mpa_reply(ep, conn_param->private_data,
			     conn_param->private_data_len);
	if (err)
		goto err1;

	state_set(&ep->com, FPDU_MODE);
	established_upcall(ep);
	c4iw_put_ep(&ep->com);
	return 0;
err1:
	ep->com.cm_id = NULL;
	ep->com.qp = NULL;
	cm_id->rem_ref(cm_id);
err:
	c4iw_put_ep(&ep->com);
	return err;
}

int c4iw_connect(struct iw_cm_id *cm_id, struct iw_cm_conn_param *conn_param)
{
	struct c4iw_dev *dev = to_c4iw_dev(cm_id->device);
	struct c4iw_ep *ep;
	struct rtable *rt;
	int err = 0;

	if ((conn_param->ord > c4iw_max_read_depth) ||
	    (conn_param->ird > c4iw_max_read_depth)) {
		err = -EINVAL;
		goto out;
	}
	ep = alloc_ep(sizeof(*ep), GFP_KERNEL);
	if (!ep) {
		printk(KERN_ERR MOD "%s - cannot alloc ep.\n", __func__);
		err = -ENOMEM;
		goto out;
	}
	init_timer(&ep->timer);
	ep->plen = conn_param->private_data_len;
	if (ep->plen)
		memcpy(ep->mpa_pkt + sizeof(struct mpa_message),
		       conn_param->private_data, ep->plen);
	ep->ird = conn_param->ird;
	ep->ord = conn_param->ord;

	if (peer2peer && ep->ord == 0)
		ep->ord = 1;

	cm_id->add_ref(cm_id);
	ep->com.dev = dev;
	ep->com.cm_id = cm_id;
	ep->com.qp = get_qhp(dev, conn_param->qpn);
	BUG_ON(!ep->com.qp);
	PDBG("%s qpn 0x%x qp %p cm_id %p\n", __func__, conn_param->qpn,
	     ep->com.qp, cm_id);

	/*
	 * Allocate an active TID to initiate a TCP connection.
	 */
	ep->atid = cxgb4_alloc_atid(dev->rdev.lldi.tids, ep);
	if (ep->atid == -1) {
		printk(KERN_ERR MOD "%s - cannot alloc atid.\n", __func__);
		err = -ENOMEM;
		goto fail2;
	}

	PDBG("%s saddr 0x%x sport 0x%x raddr 0x%x rport 0x%x\n", __func__,
	     ntohl(cm_id->local_addr.sin_addr.s_addr),
	     ntohs(cm_id->local_addr.sin_port),
	     ntohl(cm_id->remote_addr.sin_addr.s_addr),
	     ntohs(cm_id->remote_addr.sin_port));

	/* find a route */
	rt = find_route(dev,
			cm_id->local_addr.sin_addr.s_addr,
			cm_id->remote_addr.sin_addr.s_addr,
			cm_id->local_addr.sin_port,
			cm_id->remote_addr.sin_port, 0);
	if (!rt) {
		printk(KERN_ERR MOD "%s - cannot find route.\n", __func__);
		err = -EHOSTUNREACH;
		goto fail3;
	}
	ep->dst = &rt->dst;

	err = import_ep(ep, cm_id->remote_addr.sin_addr.s_addr,
			ep->dst, ep->com.dev, true);
	if (err) {
		printk(KERN_ERR MOD "%s - cannot alloc l2e.\n", __func__);
		goto fail4;
	}

	PDBG("%s txq_idx %u tx_chan %u smac_idx %u rss_qid %u l2t_idx %u\n",
		__func__, ep->txq_idx, ep->tx_chan, ep->smac_idx, ep->rss_qid,
		ep->l2t->idx);

	state_set(&ep->com, CONNECTING);
	ep->tos = 0;
	ep->com.local_addr = cm_id->local_addr;
	ep->com.remote_addr = cm_id->remote_addr;

	/* send connect request to rnic */
	err = send_connect(ep);
	if (!err)
		goto out;

	cxgb4_l2t_release(ep->l2t);
fail4:
	dst_release(ep->dst);
fail3:
	cxgb4_free_atid(ep->com.dev->rdev.lldi.tids, ep->atid);
fail2:
	cm_id->rem_ref(cm_id);
	c4iw_put_ep(&ep->com);
out:
	return err;
}

int c4iw_create_listen(struct iw_cm_id *cm_id, int backlog)
{
	int err = 0;
	struct c4iw_dev *dev = to_c4iw_dev(cm_id->device);
	struct c4iw_listen_ep *ep;


	might_sleep();

	ep = alloc_ep(sizeof(*ep), GFP_KERNEL);
	if (!ep) {
		printk(KERN_ERR MOD "%s - cannot alloc ep.\n", __func__);
		err = -ENOMEM;
		goto fail1;
	}
	PDBG("%s ep %p\n", __func__, ep);
	cm_id->add_ref(cm_id);
	ep->com.cm_id = cm_id;
	ep->com.dev = dev;
	ep->backlog = backlog;
	ep->com.local_addr = cm_id->local_addr;

	/*
	 * Allocate a server TID.
	 */
	ep->stid = cxgb4_alloc_stid(dev->rdev.lldi.tids, PF_INET, ep);
	if (ep->stid == -1) {
		printk(KERN_ERR MOD "%s - cannot alloc stid.\n", __func__);
		err = -ENOMEM;
		goto fail2;
	}

	state_set(&ep->com, LISTEN);
	c4iw_init_wr_wait(&ep->com.wr_wait);
	err = cxgb4_create_server(ep->com.dev->rdev.lldi.ports[0], ep->stid,
				  ep->com.local_addr.sin_addr.s_addr,
				  ep->com.local_addr.sin_port,
				  ep->com.dev->rdev.lldi.rxq_ids[0]);
	if (err)
		goto fail3;

	/* wait for pass_open_rpl */
	err = c4iw_wait_for_reply(&ep->com.dev->rdev, &ep->com.wr_wait, 0, 0,
				  __func__);
	if (!err) {
		cm_id->provider_data = ep;
		goto out;
	}
fail3:
	cxgb4_free_stid(ep->com.dev->rdev.lldi.tids, ep->stid, PF_INET);
fail2:
	cm_id->rem_ref(cm_id);
	c4iw_put_ep(&ep->com);
fail1:
out:
	return err;
}

int c4iw_destroy_listen(struct iw_cm_id *cm_id)
{
	int err;
	struct c4iw_listen_ep *ep = to_listen_ep(cm_id);

	PDBG("%s ep %p\n", __func__, ep);

	might_sleep();
	state_set(&ep->com, DEAD);
	c4iw_init_wr_wait(&ep->com.wr_wait);
	err = listen_stop(ep);
	if (err)
		goto done;
	err = c4iw_wait_for_reply(&ep->com.dev->rdev, &ep->com.wr_wait, 0, 0,
				  __func__);
	cxgb4_free_stid(ep->com.dev->rdev.lldi.tids, ep->stid, PF_INET);
done:
	cm_id->rem_ref(cm_id);
	c4iw_put_ep(&ep->com);
	return err;
}

int c4iw_ep_disconnect(struct c4iw_ep *ep, int abrupt, gfp_t gfp)
{
	int ret = 0;
	int close = 0;
	int fatal = 0;
	struct c4iw_rdev *rdev;

	mutex_lock(&ep->com.mutex);

	PDBG("%s ep %p state %s, abrupt %d\n", __func__, ep,
	     states[ep->com.state], abrupt);

	rdev = &ep->com.dev->rdev;
	if (c4iw_fatal_error(rdev)) {
		fatal = 1;
		close_complete_upcall(ep);
		ep->com.state = DEAD;
	}
	switch (ep->com.state) {
	case MPA_REQ_WAIT:
	case MPA_REQ_SENT:
	case MPA_REQ_RCVD:
	case MPA_REP_SENT:
	case FPDU_MODE:
		close = 1;
		if (abrupt)
			ep->com.state = ABORTING;
		else {
			ep->com.state = CLOSING;
			start_ep_timer(ep);
		}
		set_bit(CLOSE_SENT, &ep->com.flags);
		break;
	case CLOSING:
		if (!test_and_set_bit(CLOSE_SENT, &ep->com.flags)) {
			close = 1;
			if (abrupt) {
				stop_ep_timer(ep);
				ep->com.state = ABORTING;
			} else
				ep->com.state = MORIBUND;
		}
		break;
	case MORIBUND:
	case ABORTING:
	case DEAD:
		PDBG("%s ignoring disconnect ep %p state %u\n",
		     __func__, ep, ep->com.state);
		break;
	default:
		BUG();
		break;
	}

	if (close) {
		if (abrupt) {
			close_complete_upcall(ep);
			ret = send_abort(ep, NULL, gfp);
		} else
			ret = send_halfclose(ep, gfp);
		if (ret)
			fatal = 1;
	}
	mutex_unlock(&ep->com.mutex);
	if (fatal)
		release_ep_resources(ep);
	return ret;
}

static int async_event(struct c4iw_dev *dev, struct sk_buff *skb)
{
	struct cpl_fw6_msg *rpl = cplhdr(skb);
	c4iw_ev_dispatch(dev, (struct t4_cqe *)&rpl->data[0]);
	return 0;
}

/*
 * These are the real handlers that are called from a
 * work queue.
 */
static c4iw_handler_func work_handlers[NUM_CPL_CMDS] = {
	[CPL_ACT_ESTABLISH] = act_establish,
	[CPL_ACT_OPEN_RPL] = act_open_rpl,
	[CPL_RX_DATA] = rx_data,
	[CPL_ABORT_RPL_RSS] = abort_rpl,
	[CPL_ABORT_RPL] = abort_rpl,
	[CPL_PASS_OPEN_RPL] = pass_open_rpl,
	[CPL_CLOSE_LISTSRV_RPL] = close_listsrv_rpl,
	[CPL_PASS_ACCEPT_REQ] = pass_accept_req,
	[CPL_PASS_ESTABLISH] = pass_establish,
	[CPL_PEER_CLOSE] = peer_close,
	[CPL_ABORT_REQ_RSS] = peer_abort,
	[CPL_CLOSE_CON_RPL] = close_con_rpl,
	[CPL_RDMA_TERMINATE] = terminate,
	[CPL_FW4_ACK] = fw4_ack,
	[CPL_FW6_MSG] = async_event
};

static void process_timeout(struct c4iw_ep *ep)
{
	struct c4iw_qp_attributes attrs;
	int abort = 1;

	mutex_lock(&ep->com.mutex);
	PDBG("%s ep %p tid %u state %d\n", __func__, ep, ep->hwtid,
	     ep->com.state);
	switch (ep->com.state) {
	case MPA_REQ_SENT:
		__state_set(&ep->com, ABORTING);
		connect_reply_upcall(ep, -ETIMEDOUT);
		break;
	case MPA_REQ_WAIT:
		__state_set(&ep->com, ABORTING);
		break;
	case CLOSING:
	case MORIBUND:
		if (ep->com.cm_id && ep->com.qp) {
			attrs.next_state = C4IW_QP_STATE_ERROR;
			c4iw_modify_qp(ep->com.qp->rhp,
				     ep->com.qp, C4IW_QP_ATTR_NEXT_STATE,
				     &attrs, 1);
		}
		__state_set(&ep->com, ABORTING);
		break;
	default:
		printk(KERN_ERR "%s unexpected state ep %p tid %u state %u\n",
			__func__, ep, ep->hwtid, ep->com.state);
		WARN_ON(1);
		abort = 0;
	}
	mutex_unlock(&ep->com.mutex);
	if (abort)
		abort_connection(ep, NULL, GFP_KERNEL);
	c4iw_put_ep(&ep->com);
}

static void process_timedout_eps(void)
{
	struct c4iw_ep *ep;

	spin_lock_irq(&timeout_lock);
	while (!list_empty(&timeout_list)) {
		struct list_head *tmp;

		tmp = timeout_list.next;
		list_del(tmp);
		spin_unlock_irq(&timeout_lock);
		ep = list_entry(tmp, struct c4iw_ep, entry);
		process_timeout(ep);
		spin_lock_irq(&timeout_lock);
	}
	spin_unlock_irq(&timeout_lock);
}

static void process_work(struct work_struct *work)
{
	struct sk_buff *skb = NULL;
	struct c4iw_dev *dev;
	struct cpl_act_establish *rpl;
	unsigned int opcode;
	int ret;

	while ((skb = skb_dequeue(&rxq))) {
		rpl = cplhdr(skb);
		dev = *((struct c4iw_dev **) (skb->cb + sizeof(void *)));
		opcode = rpl->ot.opcode;

		BUG_ON(!work_handlers[opcode]);
		ret = work_handlers[opcode](dev, skb);
		if (!ret)
			kfree_skb(skb);
	}
	process_timedout_eps();
}

static DECLARE_WORK(skb_work, process_work);

static void ep_timeout(unsigned long arg)
{
	struct c4iw_ep *ep = (struct c4iw_ep *)arg;

	spin_lock(&timeout_lock);
	list_add_tail(&ep->entry, &timeout_list);
	spin_unlock(&timeout_lock);
	queue_work(workq, &skb_work);
}

/*
 * All the CM events are handled on a work queue to have a safe context.
 */
static int sched(struct c4iw_dev *dev, struct sk_buff *skb)
{

	/*
	 * Save dev in the skb->cb area.
	 */
	*((struct c4iw_dev **) (skb->cb + sizeof(void *))) = dev;

	/*
	 * Queue the skb and schedule the worker thread.
	 */
	skb_queue_tail(&rxq, skb);
	queue_work(workq, &skb_work);
	return 0;
}

static int set_tcb_rpl(struct c4iw_dev *dev, struct sk_buff *skb)
{
	struct cpl_set_tcb_rpl *rpl = cplhdr(skb);

	if (rpl->status != CPL_ERR_NONE) {
		printk(KERN_ERR MOD "Unexpected SET_TCB_RPL status %u "
		       "for tid %u\n", rpl->status, GET_TID(rpl));
	}
	kfree_skb(skb);
	return 0;
}

static int fw6_msg(struct c4iw_dev *dev, struct sk_buff *skb)
{
	struct cpl_fw6_msg *rpl = cplhdr(skb);
	struct c4iw_wr_wait *wr_waitp;
	int ret;

	PDBG("%s type %u\n", __func__, rpl->type);

	switch (rpl->type) {
	case 1:
		ret = (int)((be64_to_cpu(rpl->data[0]) >> 8) & 0xff);
		wr_waitp = (struct c4iw_wr_wait *)(__force unsigned long) rpl->data[1];
		PDBG("%s wr_waitp %p ret %u\n", __func__, wr_waitp, ret);
		if (wr_waitp)
			c4iw_wake_up(wr_waitp, ret ? -ret : 0);
		kfree_skb(skb);
		break;
	case 2:
		sched(dev, skb);
		break;
	default:
		printk(KERN_ERR MOD "%s unexpected fw6 msg type %u\n", __func__,
		       rpl->type);
		kfree_skb(skb);
		break;
	}
	return 0;
}

static int peer_abort_intr(struct c4iw_dev *dev, struct sk_buff *skb)
{
	struct cpl_abort_req_rss *req = cplhdr(skb);
	struct c4iw_ep *ep;
	struct tid_info *t = dev->rdev.lldi.tids;
	unsigned int tid = GET_TID(req);

	ep = lookup_tid(t, tid);
	if (is_neg_adv_abort(req->status)) {
		PDBG("%s neg_adv_abort ep %p tid %u\n", __func__, ep,
		     ep->hwtid);
		kfree_skb(skb);
		return 0;
	}
	PDBG("%s ep %p tid %u state %u\n", __func__, ep, ep->hwtid,
	     ep->com.state);

	/*
	 * Wake up any threads in rdma_init() or rdma_fini().
	 * However, this is not needed if com state is just
	 * MPA_REQ_SENT
	 */
	if (ep->com.state != MPA_REQ_SENT)
		c4iw_wake_up(&ep->com.wr_wait, -ECONNRESET);
	sched(dev, skb);
	return 0;
}

/*
 * Most upcalls from the T4 Core go to sched() to
 * schedule the processing on a work queue.
 */
c4iw_handler_func c4iw_handlers[NUM_CPL_CMDS] = {
	[CPL_ACT_ESTABLISH] = sched,
	[CPL_ACT_OPEN_RPL] = sched,
	[CPL_RX_DATA] = sched,
	[CPL_ABORT_RPL_RSS] = sched,
	[CPL_ABORT_RPL] = sched,
	[CPL_PASS_OPEN_RPL] = sched,
	[CPL_CLOSE_LISTSRV_RPL] = sched,
	[CPL_PASS_ACCEPT_REQ] = sched,
	[CPL_PASS_ESTABLISH] = sched,
	[CPL_PEER_CLOSE] = sched,
	[CPL_CLOSE_CON_RPL] = sched,
	[CPL_ABORT_REQ_RSS] = peer_abort_intr,
	[CPL_RDMA_TERMINATE] = sched,
	[CPL_FW4_ACK] = sched,
	[CPL_SET_TCB_RPL] = set_tcb_rpl,
	[CPL_FW6_MSG] = fw6_msg
};

int __init c4iw_cm_init(void)
{
	spin_lock_init(&timeout_lock);
	skb_queue_head_init(&rxq);

	workq = create_singlethread_workqueue("iw_cxgb4");
	if (!workq)
		return -ENOMEM;

	return 0;
}

void __exit c4iw_cm_term(void)
{
	WARN_ON(!list_empty(&timeout_list));
	flush_workqueue(workq);
	destroy_workqueue(workq);
}
