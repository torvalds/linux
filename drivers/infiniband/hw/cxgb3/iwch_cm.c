/*
 * Copyright (c) 2006 Chelsio, Inc. All rights reserved.
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
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
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
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/skbuff.h>
#include <linux/timer.h>
#include <linux/notifier.h>
#include <linux/inetdevice.h>

#include <net/neighbour.h>
#include <net/netevent.h>
#include <net/route.h>

#include "tcb.h"
#include "cxgb3_offload.h"
#include "iwch.h"
#include "iwch_provider.h"
#include "iwch_cm.h"

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

int peer2peer = 0;
module_param(peer2peer, int, 0644);
MODULE_PARM_DESC(peer2peer, "Support peer2peer ULPs (default=0)");

static int ep_timeout_secs = 60;
module_param(ep_timeout_secs, int, 0644);
MODULE_PARM_DESC(ep_timeout_secs, "CM Endpoint operation timeout "
				   "in seconds (default=60)");

static int mpa_rev = 1;
module_param(mpa_rev, int, 0644);
MODULE_PARM_DESC(mpa_rev, "MPA Revision, 0 supports amso1100, "
		 "1 is spec compliant. (default=1)");

static int markers_enabled = 0;
module_param(markers_enabled, int, 0644);
MODULE_PARM_DESC(markers_enabled, "Enable MPA MARKERS (default(0)=disabled)");

static int crc_enabled = 1;
module_param(crc_enabled, int, 0644);
MODULE_PARM_DESC(crc_enabled, "Enable MPA CRC (default(1)=enabled)");

static int rcv_win = 256 * 1024;
module_param(rcv_win, int, 0644);
MODULE_PARM_DESC(rcv_win, "TCP receive window in bytes (default=256)");

static int snd_win = 32 * 1024;
module_param(snd_win, int, 0644);
MODULE_PARM_DESC(snd_win, "TCP send window in bytes (default=32KB)");

static unsigned int nocong = 0;
module_param(nocong, uint, 0644);
MODULE_PARM_DESC(nocong, "Turn off congestion control (default=0)");

static unsigned int cong_flavor = 1;
module_param(cong_flavor, uint, 0644);
MODULE_PARM_DESC(cong_flavor, "TCP Congestion control flavor (default=1)");

static struct workqueue_struct *workq;

static struct sk_buff_head rxq;

static struct sk_buff *get_skb(struct sk_buff *skb, int len, gfp_t gfp);
static void ep_timeout(unsigned long arg);
static void connect_reply_upcall(struct iwch_ep *ep, int status);

static void start_ep_timer(struct iwch_ep *ep)
{
	PDBG("%s ep %p\n", __func__, ep);
	if (timer_pending(&ep->timer)) {
		PDBG("%s stopped / restarted timer ep %p\n", __func__, ep);
		del_timer_sync(&ep->timer);
	} else
		get_ep(&ep->com);
	ep->timer.expires = jiffies + ep_timeout_secs * HZ;
	ep->timer.data = (unsigned long)ep;
	ep->timer.function = ep_timeout;
	add_timer(&ep->timer);
}

static void stop_ep_timer(struct iwch_ep *ep)
{
	PDBG("%s ep %p\n", __func__, ep);
	if (!timer_pending(&ep->timer)) {
		printk(KERN_ERR "%s timer stopped when its not running!  ep %p state %u\n",
			__func__, ep, ep->com.state);
		WARN_ON(1);
		return;
	}
	del_timer_sync(&ep->timer);
	put_ep(&ep->com);
}

int iwch_l2t_send(struct t3cdev *tdev, struct sk_buff *skb, struct l2t_entry *l2e)
{
	int	error = 0;
	struct cxio_rdev *rdev;

	rdev = (struct cxio_rdev *)tdev->ulp;
	if (cxio_fatal_error(rdev)) {
		kfree_skb(skb);
		return -EIO;
	}
	error = l2t_send(tdev, skb, l2e);
	if (error < 0)
		kfree_skb(skb);
	return error;
}

int iwch_cxgb3_ofld_send(struct t3cdev *tdev, struct sk_buff *skb)
{
	int	error = 0;
	struct cxio_rdev *rdev;

	rdev = (struct cxio_rdev *)tdev->ulp;
	if (cxio_fatal_error(rdev)) {
		kfree_skb(skb);
		return -EIO;
	}
	error = cxgb3_ofld_send(tdev, skb);
	if (error < 0)
		kfree_skb(skb);
	return error;
}

static void release_tid(struct t3cdev *tdev, u32 hwtid, struct sk_buff *skb)
{
	struct cpl_tid_release *req;

	skb = get_skb(skb, sizeof *req, GFP_KERNEL);
	if (!skb)
		return;
	req = (struct cpl_tid_release *) skb_put(skb, sizeof(*req));
	req->wr.wr_hi = htonl(V_WR_OP(FW_WROPCODE_FORWARD));
	OPCODE_TID(req) = htonl(MK_OPCODE_TID(CPL_TID_RELEASE, hwtid));
	skb->priority = CPL_PRIORITY_SETUP;
	iwch_cxgb3_ofld_send(tdev, skb);
	return;
}

int iwch_quiesce_tid(struct iwch_ep *ep)
{
	struct cpl_set_tcb_field *req;
	struct sk_buff *skb = get_skb(NULL, sizeof(*req), GFP_KERNEL);

	if (!skb)
		return -ENOMEM;
	req = (struct cpl_set_tcb_field *) skb_put(skb, sizeof(*req));
	req->wr.wr_hi = htonl(V_WR_OP(FW_WROPCODE_FORWARD));
	req->wr.wr_lo = htonl(V_WR_TID(ep->hwtid));
	OPCODE_TID(req) = htonl(MK_OPCODE_TID(CPL_SET_TCB_FIELD, ep->hwtid));
	req->reply = 0;
	req->cpu_idx = 0;
	req->word = htons(W_TCB_RX_QUIESCE);
	req->mask = cpu_to_be64(1ULL << S_TCB_RX_QUIESCE);
	req->val = cpu_to_be64(1 << S_TCB_RX_QUIESCE);

	skb->priority = CPL_PRIORITY_DATA;
	return iwch_cxgb3_ofld_send(ep->com.tdev, skb);
}

int iwch_resume_tid(struct iwch_ep *ep)
{
	struct cpl_set_tcb_field *req;
	struct sk_buff *skb = get_skb(NULL, sizeof(*req), GFP_KERNEL);

	if (!skb)
		return -ENOMEM;
	req = (struct cpl_set_tcb_field *) skb_put(skb, sizeof(*req));
	req->wr.wr_hi = htonl(V_WR_OP(FW_WROPCODE_FORWARD));
	req->wr.wr_lo = htonl(V_WR_TID(ep->hwtid));
	OPCODE_TID(req) = htonl(MK_OPCODE_TID(CPL_SET_TCB_FIELD, ep->hwtid));
	req->reply = 0;
	req->cpu_idx = 0;
	req->word = htons(W_TCB_RX_QUIESCE);
	req->mask = cpu_to_be64(1ULL << S_TCB_RX_QUIESCE);
	req->val = 0;

	skb->priority = CPL_PRIORITY_DATA;
	return iwch_cxgb3_ofld_send(ep->com.tdev, skb);
}

static void set_emss(struct iwch_ep *ep, u16 opt)
{
	PDBG("%s ep %p opt %u\n", __func__, ep, opt);
	ep->emss = T3C_DATA(ep->com.tdev)->mtus[G_TCPOPT_MSS(opt)] - 40;
	if (G_TCPOPT_TSTAMP(opt))
		ep->emss -= 12;
	if (ep->emss < 128)
		ep->emss = 128;
	PDBG("emss=%d\n", ep->emss);
}

static enum iwch_ep_state state_read(struct iwch_ep_common *epc)
{
	unsigned long flags;
	enum iwch_ep_state state;

	spin_lock_irqsave(&epc->lock, flags);
	state = epc->state;
	spin_unlock_irqrestore(&epc->lock, flags);
	return state;
}

static void __state_set(struct iwch_ep_common *epc, enum iwch_ep_state new)
{
	epc->state = new;
}

static void state_set(struct iwch_ep_common *epc, enum iwch_ep_state new)
{
	unsigned long flags;

	spin_lock_irqsave(&epc->lock, flags);
	PDBG("%s - %s -> %s\n", __func__, states[epc->state], states[new]);
	__state_set(epc, new);
	spin_unlock_irqrestore(&epc->lock, flags);
	return;
}

static void *alloc_ep(int size, gfp_t gfp)
{
	struct iwch_ep_common *epc;

	epc = kzalloc(size, gfp);
	if (epc) {
		kref_init(&epc->kref);
		spin_lock_init(&epc->lock);
		init_waitqueue_head(&epc->waitq);
	}
	PDBG("%s alloc ep %p\n", __func__, epc);
	return epc;
}

void __free_ep(struct kref *kref)
{
	struct iwch_ep *ep;
	ep = container_of(container_of(kref, struct iwch_ep_common, kref),
			  struct iwch_ep, com);
	PDBG("%s ep %p state %s\n", __func__, ep, states[state_read(&ep->com)]);
	if (test_bit(RELEASE_RESOURCES, &ep->com.flags)) {
		cxgb3_remove_tid(ep->com.tdev, (void *)ep, ep->hwtid);
		dst_release(ep->dst);
		l2t_release(L2DATA(ep->com.tdev), ep->l2t);
	}
	kfree(ep);
}

static void release_ep_resources(struct iwch_ep *ep)
{
	PDBG("%s ep %p tid %d\n", __func__, ep, ep->hwtid);
	set_bit(RELEASE_RESOURCES, &ep->com.flags);
	put_ep(&ep->com);
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
	} else {
		skb = alloc_skb(len, gfp);
	}
	return skb;
}

static struct rtable *find_route(struct t3cdev *dev, __be32 local_ip,
				 __be32 peer_ip, __be16 local_port,
				 __be16 peer_port, u8 tos)
{
	struct rtable *rt;
	struct flowi fl = {
		.oif = 0,
		.nl_u = {
			 .ip4_u = {
				   .daddr = peer_ip,
				   .saddr = local_ip,
				   .tos = tos}
			 },
		.proto = IPPROTO_TCP,
		.uli_u = {
			  .ports = {
				    .sport = local_port,
				    .dport = peer_port}
			  }
	};

	if (ip_route_output_flow(&init_net, &rt, &fl, NULL, 0))
		return NULL;
	return rt;
}

static unsigned int find_best_mtu(const struct t3c_data *d, unsigned short mtu)
{
	int i = 0;

	while (i < d->nmtus - 1 && d->mtus[i + 1] <= mtu)
		++i;
	return i;
}

static void arp_failure_discard(struct t3cdev *dev, struct sk_buff *skb)
{
	PDBG("%s t3cdev %p\n", __func__, dev);
	kfree_skb(skb);
}

/*
 * Handle an ARP failure for an active open.
 */
static void act_open_req_arp_failure(struct t3cdev *dev, struct sk_buff *skb)
{
	printk(KERN_ERR MOD "ARP failure duing connect\n");
	kfree_skb(skb);
}

/*
 * Handle an ARP failure for a CPL_ABORT_REQ.  Change it into a no RST variant
 * and send it along.
 */
static void abort_arp_failure(struct t3cdev *dev, struct sk_buff *skb)
{
	struct cpl_abort_req *req = cplhdr(skb);

	PDBG("%s t3cdev %p\n", __func__, dev);
	req->cmd = CPL_ABORT_NO_RST;
	iwch_cxgb3_ofld_send(dev, skb);
}

static int send_halfclose(struct iwch_ep *ep, gfp_t gfp)
{
	struct cpl_close_con_req *req;
	struct sk_buff *skb;

	PDBG("%s ep %p\n", __func__, ep);
	skb = get_skb(NULL, sizeof(*req), gfp);
	if (!skb) {
		printk(KERN_ERR MOD "%s - failed to alloc skb\n", __func__);
		return -ENOMEM;
	}
	skb->priority = CPL_PRIORITY_DATA;
	set_arp_failure_handler(skb, arp_failure_discard);
	req = (struct cpl_close_con_req *) skb_put(skb, sizeof(*req));
	req->wr.wr_hi = htonl(V_WR_OP(FW_WROPCODE_OFLD_CLOSE_CON));
	req->wr.wr_lo = htonl(V_WR_TID(ep->hwtid));
	OPCODE_TID(req) = htonl(MK_OPCODE_TID(CPL_CLOSE_CON_REQ, ep->hwtid));
	return iwch_l2t_send(ep->com.tdev, skb, ep->l2t);
}

static int send_abort(struct iwch_ep *ep, struct sk_buff *skb, gfp_t gfp)
{
	struct cpl_abort_req *req;

	PDBG("%s ep %p\n", __func__, ep);
	skb = get_skb(skb, sizeof(*req), gfp);
	if (!skb) {
		printk(KERN_ERR MOD "%s - failed to alloc skb.\n",
		       __func__);
		return -ENOMEM;
	}
	skb->priority = CPL_PRIORITY_DATA;
	set_arp_failure_handler(skb, abort_arp_failure);
	req = (struct cpl_abort_req *) skb_put(skb, sizeof(*req));
	req->wr.wr_hi = htonl(V_WR_OP(FW_WROPCODE_OFLD_HOST_ABORT_CON_REQ));
	req->wr.wr_lo = htonl(V_WR_TID(ep->hwtid));
	OPCODE_TID(req) = htonl(MK_OPCODE_TID(CPL_ABORT_REQ, ep->hwtid));
	req->cmd = CPL_ABORT_SEND_RST;
	return iwch_l2t_send(ep->com.tdev, skb, ep->l2t);
}

static int send_connect(struct iwch_ep *ep)
{
	struct cpl_act_open_req *req;
	struct sk_buff *skb;
	u32 opt0h, opt0l, opt2;
	unsigned int mtu_idx;
	int wscale;

	PDBG("%s ep %p\n", __func__, ep);

	skb = get_skb(NULL, sizeof(*req), GFP_KERNEL);
	if (!skb) {
		printk(KERN_ERR MOD "%s - failed to alloc skb.\n",
		       __func__);
		return -ENOMEM;
	}
	mtu_idx = find_best_mtu(T3C_DATA(ep->com.tdev), dst_mtu(ep->dst));
	wscale = compute_wscale(rcv_win);
	opt0h = V_NAGLE(0) |
	    V_NO_CONG(nocong) |
	    V_KEEP_ALIVE(1) |
	    F_TCAM_BYPASS |
	    V_WND_SCALE(wscale) |
	    V_MSS_IDX(mtu_idx) |
	    V_L2T_IDX(ep->l2t->idx) | V_TX_CHANNEL(ep->l2t->smt_idx);
	opt0l = V_TOS((ep->tos >> 2) & M_TOS) | V_RCV_BUFSIZ(rcv_win>>10);
	opt2 = V_FLAVORS_VALID(1) | V_CONG_CONTROL_FLAVOR(cong_flavor);
	skb->priority = CPL_PRIORITY_SETUP;
	set_arp_failure_handler(skb, act_open_req_arp_failure);

	req = (struct cpl_act_open_req *) skb_put(skb, sizeof(*req));
	req->wr.wr_hi = htonl(V_WR_OP(FW_WROPCODE_FORWARD));
	OPCODE_TID(req) = htonl(MK_OPCODE_TID(CPL_ACT_OPEN_REQ, ep->atid));
	req->local_port = ep->com.local_addr.sin_port;
	req->peer_port = ep->com.remote_addr.sin_port;
	req->local_ip = ep->com.local_addr.sin_addr.s_addr;
	req->peer_ip = ep->com.remote_addr.sin_addr.s_addr;
	req->opt0h = htonl(opt0h);
	req->opt0l = htonl(opt0l);
	req->params = 0;
	req->opt2 = htonl(opt2);
	return iwch_l2t_send(ep->com.tdev, skb, ep->l2t);
}

static void send_mpa_req(struct iwch_ep *ep, struct sk_buff *skb)
{
	int mpalen;
	struct tx_data_wr *req;
	struct mpa_message *mpa;
	int len;

	PDBG("%s ep %p pd_len %d\n", __func__, ep, ep->plen);

	BUG_ON(skb_cloned(skb));

	mpalen = sizeof(*mpa) + ep->plen;
	if (skb->data + mpalen + sizeof(*req) > skb_end_pointer(skb)) {
		kfree_skb(skb);
		skb=alloc_skb(mpalen + sizeof(*req), GFP_KERNEL);
		if (!skb) {
			connect_reply_upcall(ep, -ENOMEM);
			return;
		}
	}
	skb_trim(skb, 0);
	skb_reserve(skb, sizeof(*req));
	skb_put(skb, mpalen);
	skb->priority = CPL_PRIORITY_DATA;
	mpa = (struct mpa_message *) skb->data;
	memset(mpa, 0, sizeof(*mpa));
	memcpy(mpa->key, MPA_KEY_REQ, sizeof(mpa->key));
	mpa->flags = (crc_enabled ? MPA_CRC : 0) |
		     (markers_enabled ? MPA_MARKERS : 0);
	mpa->private_data_size = htons(ep->plen);
	mpa->revision = mpa_rev;

	if (ep->plen)
		memcpy(mpa->private_data, ep->mpa_pkt + sizeof(*mpa), ep->plen);

	/*
	 * Reference the mpa skb.  This ensures the data area
	 * will remain in memory until the hw acks the tx.
	 * Function tx_ack() will deref it.
	 */
	skb_get(skb);
	set_arp_failure_handler(skb, arp_failure_discard);
	skb_reset_transport_header(skb);
	len = skb->len;
	req = (struct tx_data_wr *) skb_push(skb, sizeof(*req));
	req->wr_hi = htonl(V_WR_OP(FW_WROPCODE_OFLD_TX_DATA)|F_WR_COMPL);
	req->wr_lo = htonl(V_WR_TID(ep->hwtid));
	req->len = htonl(len);
	req->param = htonl(V_TX_PORT(ep->l2t->smt_idx) |
			   V_TX_SNDBUF(snd_win>>15));
	req->flags = htonl(F_TX_INIT);
	req->sndseq = htonl(ep->snd_seq);
	BUG_ON(ep->mpa_skb);
	ep->mpa_skb = skb;
	iwch_l2t_send(ep->com.tdev, skb, ep->l2t);
	start_ep_timer(ep);
	state_set(&ep->com, MPA_REQ_SENT);
	return;
}

static int send_mpa_reject(struct iwch_ep *ep, const void *pdata, u8 plen)
{
	int mpalen;
	struct tx_data_wr *req;
	struct mpa_message *mpa;
	struct sk_buff *skb;

	PDBG("%s ep %p plen %d\n", __func__, ep, plen);

	mpalen = sizeof(*mpa) + plen;

	skb = get_skb(NULL, mpalen + sizeof(*req), GFP_KERNEL);
	if (!skb) {
		printk(KERN_ERR MOD "%s - cannot alloc skb!\n", __func__);
		return -ENOMEM;
	}
	skb_reserve(skb, sizeof(*req));
	mpa = (struct mpa_message *) skb_put(skb, mpalen);
	memset(mpa, 0, sizeof(*mpa));
	memcpy(mpa->key, MPA_KEY_REP, sizeof(mpa->key));
	mpa->flags = MPA_REJECT;
	mpa->revision = mpa_rev;
	mpa->private_data_size = htons(plen);
	if (plen)
		memcpy(mpa->private_data, pdata, plen);

	/*
	 * Reference the mpa skb again.  This ensures the data area
	 * will remain in memory until the hw acks the tx.
	 * Function tx_ack() will deref it.
	 */
	skb_get(skb);
	skb->priority = CPL_PRIORITY_DATA;
	set_arp_failure_handler(skb, arp_failure_discard);
	skb_reset_transport_header(skb);
	req = (struct tx_data_wr *) skb_push(skb, sizeof(*req));
	req->wr_hi = htonl(V_WR_OP(FW_WROPCODE_OFLD_TX_DATA)|F_WR_COMPL);
	req->wr_lo = htonl(V_WR_TID(ep->hwtid));
	req->len = htonl(mpalen);
	req->param = htonl(V_TX_PORT(ep->l2t->smt_idx) |
			   V_TX_SNDBUF(snd_win>>15));
	req->flags = htonl(F_TX_INIT);
	req->sndseq = htonl(ep->snd_seq);
	BUG_ON(ep->mpa_skb);
	ep->mpa_skb = skb;
	return iwch_l2t_send(ep->com.tdev, skb, ep->l2t);
}

static int send_mpa_reply(struct iwch_ep *ep, const void *pdata, u8 plen)
{
	int mpalen;
	struct tx_data_wr *req;
	struct mpa_message *mpa;
	int len;
	struct sk_buff *skb;

	PDBG("%s ep %p plen %d\n", __func__, ep, plen);

	mpalen = sizeof(*mpa) + plen;

	skb = get_skb(NULL, mpalen + sizeof(*req), GFP_KERNEL);
	if (!skb) {
		printk(KERN_ERR MOD "%s - cannot alloc skb!\n", __func__);
		return -ENOMEM;
	}
	skb->priority = CPL_PRIORITY_DATA;
	skb_reserve(skb, sizeof(*req));
	mpa = (struct mpa_message *) skb_put(skb, mpalen);
	memset(mpa, 0, sizeof(*mpa));
	memcpy(mpa->key, MPA_KEY_REP, sizeof(mpa->key));
	mpa->flags = (ep->mpa_attr.crc_enabled ? MPA_CRC : 0) |
		     (markers_enabled ? MPA_MARKERS : 0);
	mpa->revision = mpa_rev;
	mpa->private_data_size = htons(plen);
	if (plen)
		memcpy(mpa->private_data, pdata, plen);

	/*
	 * Reference the mpa skb.  This ensures the data area
	 * will remain in memory until the hw acks the tx.
	 * Function tx_ack() will deref it.
	 */
	skb_get(skb);
	set_arp_failure_handler(skb, arp_failure_discard);
	skb_reset_transport_header(skb);
	len = skb->len;
	req = (struct tx_data_wr *) skb_push(skb, sizeof(*req));
	req->wr_hi = htonl(V_WR_OP(FW_WROPCODE_OFLD_TX_DATA)|F_WR_COMPL);
	req->wr_lo = htonl(V_WR_TID(ep->hwtid));
	req->len = htonl(len);
	req->param = htonl(V_TX_PORT(ep->l2t->smt_idx) |
			   V_TX_SNDBUF(snd_win>>15));
	req->flags = htonl(F_TX_INIT);
	req->sndseq = htonl(ep->snd_seq);
	ep->mpa_skb = skb;
	state_set(&ep->com, MPA_REP_SENT);
	return iwch_l2t_send(ep->com.tdev, skb, ep->l2t);
}

static int act_establish(struct t3cdev *tdev, struct sk_buff *skb, void *ctx)
{
	struct iwch_ep *ep = ctx;
	struct cpl_act_establish *req = cplhdr(skb);
	unsigned int tid = GET_TID(req);

	PDBG("%s ep %p tid %d\n", __func__, ep, tid);

	dst_confirm(ep->dst);

	/* setup the hwtid for this connection */
	ep->hwtid = tid;
	cxgb3_insert_tid(ep->com.tdev, &t3c_client, ep, tid);

	ep->snd_seq = ntohl(req->snd_isn);
	ep->rcv_seq = ntohl(req->rcv_isn);

	set_emss(ep, ntohs(req->tcp_opt));

	/* dealloc the atid */
	cxgb3_free_atid(ep->com.tdev, ep->atid);

	/* start MPA negotiation */
	send_mpa_req(ep, skb);

	return 0;
}

static void abort_connection(struct iwch_ep *ep, struct sk_buff *skb, gfp_t gfp)
{
	PDBG("%s ep %p\n", __FILE__, ep);
	state_set(&ep->com, ABORTING);
	send_abort(ep, skb, gfp);
}

static void close_complete_upcall(struct iwch_ep *ep)
{
	struct iw_cm_event event;

	PDBG("%s ep %p\n", __func__, ep);
	memset(&event, 0, sizeof(event));
	event.event = IW_CM_EVENT_CLOSE;
	if (ep->com.cm_id) {
		PDBG("close complete delivered ep %p cm_id %p tid %d\n",
		     ep, ep->com.cm_id, ep->hwtid);
		ep->com.cm_id->event_handler(ep->com.cm_id, &event);
		ep->com.cm_id->rem_ref(ep->com.cm_id);
		ep->com.cm_id = NULL;
		ep->com.qp = NULL;
	}
}

static void peer_close_upcall(struct iwch_ep *ep)
{
	struct iw_cm_event event;

	PDBG("%s ep %p\n", __func__, ep);
	memset(&event, 0, sizeof(event));
	event.event = IW_CM_EVENT_DISCONNECT;
	if (ep->com.cm_id) {
		PDBG("peer close delivered ep %p cm_id %p tid %d\n",
		     ep, ep->com.cm_id, ep->hwtid);
		ep->com.cm_id->event_handler(ep->com.cm_id, &event);
	}
}

static void peer_abort_upcall(struct iwch_ep *ep)
{
	struct iw_cm_event event;

	PDBG("%s ep %p\n", __func__, ep);
	memset(&event, 0, sizeof(event));
	event.event = IW_CM_EVENT_CLOSE;
	event.status = -ECONNRESET;
	if (ep->com.cm_id) {
		PDBG("abort delivered ep %p cm_id %p tid %d\n", ep,
		     ep->com.cm_id, ep->hwtid);
		ep->com.cm_id->event_handler(ep->com.cm_id, &event);
		ep->com.cm_id->rem_ref(ep->com.cm_id);
		ep->com.cm_id = NULL;
		ep->com.qp = NULL;
	}
}

static void connect_reply_upcall(struct iwch_ep *ep, int status)
{
	struct iw_cm_event event;

	PDBG("%s ep %p status %d\n", __func__, ep, status);
	memset(&event, 0, sizeof(event));
	event.event = IW_CM_EVENT_CONNECT_REPLY;
	event.status = status;
	event.local_addr = ep->com.local_addr;
	event.remote_addr = ep->com.remote_addr;

	if ((status == 0) || (status == -ECONNREFUSED)) {
		event.private_data_len = ep->plen;
		event.private_data = ep->mpa_pkt + sizeof(struct mpa_message);
	}
	if (ep->com.cm_id) {
		PDBG("%s ep %p tid %d status %d\n", __func__, ep,
		     ep->hwtid, status);
		ep->com.cm_id->event_handler(ep->com.cm_id, &event);
	}
	if (status < 0) {
		ep->com.cm_id->rem_ref(ep->com.cm_id);
		ep->com.cm_id = NULL;
		ep->com.qp = NULL;
	}
}

static void connect_request_upcall(struct iwch_ep *ep)
{
	struct iw_cm_event event;

	PDBG("%s ep %p tid %d\n", __func__, ep, ep->hwtid);
	memset(&event, 0, sizeof(event));
	event.event = IW_CM_EVENT_CONNECT_REQUEST;
	event.local_addr = ep->com.local_addr;
	event.remote_addr = ep->com.remote_addr;
	event.private_data_len = ep->plen;
	event.private_data = ep->mpa_pkt + sizeof(struct mpa_message);
	event.provider_data = ep;
	if (state_read(&ep->parent_ep->com) != DEAD) {
		get_ep(&ep->com);
		ep->parent_ep->com.cm_id->event_handler(
						ep->parent_ep->com.cm_id,
						&event);
	}
	put_ep(&ep->parent_ep->com);
	ep->parent_ep = NULL;
}

static void established_upcall(struct iwch_ep *ep)
{
	struct iw_cm_event event;

	PDBG("%s ep %p\n", __func__, ep);
	memset(&event, 0, sizeof(event));
	event.event = IW_CM_EVENT_ESTABLISHED;
	if (ep->com.cm_id) {
		PDBG("%s ep %p tid %d\n", __func__, ep, ep->hwtid);
		ep->com.cm_id->event_handler(ep->com.cm_id, &event);
	}
}

static int update_rx_credits(struct iwch_ep *ep, u32 credits)
{
	struct cpl_rx_data_ack *req;
	struct sk_buff *skb;

	PDBG("%s ep %p credits %u\n", __func__, ep, credits);
	skb = get_skb(NULL, sizeof(*req), GFP_KERNEL);
	if (!skb) {
		printk(KERN_ERR MOD "update_rx_credits - cannot alloc skb!\n");
		return 0;
	}

	req = (struct cpl_rx_data_ack *) skb_put(skb, sizeof(*req));
	req->wr.wr_hi = htonl(V_WR_OP(FW_WROPCODE_FORWARD));
	OPCODE_TID(req) = htonl(MK_OPCODE_TID(CPL_RX_DATA_ACK, ep->hwtid));
	req->credit_dack = htonl(V_RX_CREDITS(credits) | V_RX_FORCE_ACK(1));
	skb->priority = CPL_PRIORITY_ACK;
	iwch_cxgb3_ofld_send(ep->com.tdev, skb);
	return credits;
}

static void process_mpa_reply(struct iwch_ep *ep, struct sk_buff *skb)
{
	struct mpa_message *mpa;
	u16 plen;
	struct iwch_qp_attributes attrs;
	enum iwch_qp_attr_mask mask;
	int err;

	PDBG("%s ep %p\n", __func__, ep);

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
	if (mpa->revision != mpa_rev) {
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
	ep->mpa_attr.initiator = 1;
	ep->mpa_attr.crc_enabled = (mpa->flags & MPA_CRC) | crc_enabled ? 1 : 0;
	ep->mpa_attr.recv_marker_enabled = markers_enabled;
	ep->mpa_attr.xmit_marker_enabled = mpa->flags & MPA_MARKERS ? 1 : 0;
	ep->mpa_attr.version = mpa_rev;
	PDBG("%s - crc_enabled=%d, recv_marker_enabled=%d, "
	     "xmit_marker_enabled=%d, version=%d\n", __func__,
	     ep->mpa_attr.crc_enabled, ep->mpa_attr.recv_marker_enabled,
	     ep->mpa_attr.xmit_marker_enabled, ep->mpa_attr.version);

	attrs.mpa_attr = ep->mpa_attr;
	attrs.max_ird = ep->ird;
	attrs.max_ord = ep->ord;
	attrs.llp_stream_handle = ep;
	attrs.next_state = IWCH_QP_STATE_RTS;

	mask = IWCH_QP_ATTR_NEXT_STATE |
	    IWCH_QP_ATTR_LLP_STREAM_HANDLE | IWCH_QP_ATTR_MPA_ATTR |
	    IWCH_QP_ATTR_MAX_IRD | IWCH_QP_ATTR_MAX_ORD;

	/* bind QP and TID with INIT_WR */
	err = iwch_modify_qp(ep->com.qp->rhp,
			     ep->com.qp, mask, &attrs, 1);
	if (err)
		goto err;

	if (peer2peer && iwch_rqes_posted(ep->com.qp) == 0) {
		iwch_post_zb_read(ep->com.qp);
	}

	goto out;
err:
	abort_connection(ep, skb, GFP_KERNEL);
out:
	connect_reply_upcall(ep, err);
	return;
}

static void process_mpa_request(struct iwch_ep *ep, struct sk_buff *skb)
{
	struct mpa_message *mpa;
	u16 plen;

	PDBG("%s ep %p\n", __func__, ep);

	/*
	 * Stop mpa timer.  If it expired, then the state has
	 * changed and we bail since ep_timeout already aborted
	 * the connection.
	 */
	stop_ep_timer(ep);
	if (state_read(&ep->com) != MPA_REQ_WAIT)
		return;

	/*
	 * If we get more than the supported amount of private data
	 * then we must fail this connection.
	 */
	if (ep->mpa_pkt_len + skb->len > sizeof(ep->mpa_pkt)) {
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
	mpa = (struct mpa_message *) ep->mpa_pkt;

	/*
	 * Validate MPA Header.
	 */
	if (mpa->revision != mpa_rev) {
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
	ep->mpa_attr.version = mpa_rev;
	PDBG("%s - crc_enabled=%d, recv_marker_enabled=%d, "
	     "xmit_marker_enabled=%d, version=%d\n", __func__,
	     ep->mpa_attr.crc_enabled, ep->mpa_attr.recv_marker_enabled,
	     ep->mpa_attr.xmit_marker_enabled, ep->mpa_attr.version);

	state_set(&ep->com, MPA_REQ_RCVD);

	/* drive upcall */
	connect_request_upcall(ep);
	return;
}

static int rx_data(struct t3cdev *tdev, struct sk_buff *skb, void *ctx)
{
	struct iwch_ep *ep = ctx;
	struct cpl_rx_data *hdr = cplhdr(skb);
	unsigned int dlen = ntohs(hdr->len);

	PDBG("%s ep %p dlen %u\n", __func__, ep, dlen);

	skb_pull(skb, sizeof(*hdr));
	skb_trim(skb, dlen);

	ep->rcv_seq += dlen;
	BUG_ON(ep->rcv_seq != (ntohl(hdr->seq) + dlen));

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
		       " ep %p state %d tid %d\n",
		       __func__, ep, state_read(&ep->com), ep->hwtid);

		/*
		 * The ep will timeout and inform the ULP of the failure.
		 * See ep_timeout().
		 */
		break;
	}

	/* update RX credits */
	update_rx_credits(ep, dlen);

	return CPL_RET_BUF_DONE;
}

/*
 * Upcall from the adapter indicating data has been transmitted.
 * For us its just the single MPA request or reply.  We can now free
 * the skb holding the mpa message.
 */
static int tx_ack(struct t3cdev *tdev, struct sk_buff *skb, void *ctx)
{
	struct iwch_ep *ep = ctx;
	struct cpl_wr_ack *hdr = cplhdr(skb);
	unsigned int credits = ntohs(hdr->credits);

	PDBG("%s ep %p credits %u\n", __func__, ep, credits);

	if (credits == 0) {
		PDBG(KERN_ERR "%s 0 credit ack  ep %p state %u\n",
			__func__, ep, state_read(&ep->com));
		return CPL_RET_BUF_DONE;
	}

	BUG_ON(credits != 1);
	dst_confirm(ep->dst);
	if (!ep->mpa_skb) {
		PDBG("%s rdma_init wr_ack ep %p state %u\n",
			__func__, ep, state_read(&ep->com));
		if (ep->mpa_attr.initiator) {
			PDBG("%s initiator ep %p state %u\n",
				__func__, ep, state_read(&ep->com));
			if (peer2peer)
				iwch_post_zb_read(ep->com.qp);
		} else {
			PDBG("%s responder ep %p state %u\n",
				__func__, ep, state_read(&ep->com));
			ep->com.rpl_done = 1;
			wake_up(&ep->com.waitq);
		}
	} else {
		PDBG("%s lsm ack ep %p state %u freeing skb\n",
			__func__, ep, state_read(&ep->com));
		kfree_skb(ep->mpa_skb);
		ep->mpa_skb = NULL;
	}
	return CPL_RET_BUF_DONE;
}

static int abort_rpl(struct t3cdev *tdev, struct sk_buff *skb, void *ctx)
{
	struct iwch_ep *ep = ctx;
	unsigned long flags;
	int release = 0;

	PDBG("%s ep %p\n", __func__, ep);
	BUG_ON(!ep);

	/*
	 * We get 2 abort replies from the HW.  The first one must
	 * be ignored except for scribbling that we need one more.
	 */
	if (!test_and_set_bit(ABORT_REQ_IN_PROGRESS, &ep->com.flags)) {
		return CPL_RET_BUF_DONE;
	}

	spin_lock_irqsave(&ep->com.lock, flags);
	switch (ep->com.state) {
	case ABORTING:
		close_complete_upcall(ep);
		__state_set(&ep->com, DEAD);
		release = 1;
		break;
	default:
		printk(KERN_ERR "%s ep %p state %d\n",
		     __func__, ep, ep->com.state);
		break;
	}
	spin_unlock_irqrestore(&ep->com.lock, flags);

	if (release)
		release_ep_resources(ep);
	return CPL_RET_BUF_DONE;
}

/*
 * Return whether a failed active open has allocated a TID
 */
static inline int act_open_has_tid(int status)
{
	return status != CPL_ERR_TCAM_FULL && status != CPL_ERR_CONN_EXIST &&
	       status != CPL_ERR_ARP_MISS;
}

static int act_open_rpl(struct t3cdev *tdev, struct sk_buff *skb, void *ctx)
{
	struct iwch_ep *ep = ctx;
	struct cpl_act_open_rpl *rpl = cplhdr(skb);

	PDBG("%s ep %p status %u errno %d\n", __func__, ep, rpl->status,
	     status2errno(rpl->status));
	connect_reply_upcall(ep, status2errno(rpl->status));
	state_set(&ep->com, DEAD);
	if (ep->com.tdev->type != T3A && act_open_has_tid(rpl->status))
		release_tid(ep->com.tdev, GET_TID(rpl), NULL);
	cxgb3_free_atid(ep->com.tdev, ep->atid);
	dst_release(ep->dst);
	l2t_release(L2DATA(ep->com.tdev), ep->l2t);
	put_ep(&ep->com);
	return CPL_RET_BUF_DONE;
}

static int listen_start(struct iwch_listen_ep *ep)
{
	struct sk_buff *skb;
	struct cpl_pass_open_req *req;

	PDBG("%s ep %p\n", __func__, ep);
	skb = get_skb(NULL, sizeof(*req), GFP_KERNEL);
	if (!skb) {
		printk(KERN_ERR MOD "t3c_listen_start failed to alloc skb!\n");
		return -ENOMEM;
	}

	req = (struct cpl_pass_open_req *) skb_put(skb, sizeof(*req));
	req->wr.wr_hi = htonl(V_WR_OP(FW_WROPCODE_FORWARD));
	OPCODE_TID(req) = htonl(MK_OPCODE_TID(CPL_PASS_OPEN_REQ, ep->stid));
	req->local_port = ep->com.local_addr.sin_port;
	req->local_ip = ep->com.local_addr.sin_addr.s_addr;
	req->peer_port = 0;
	req->peer_ip = 0;
	req->peer_netmask = 0;
	req->opt0h = htonl(F_DELACK | F_TCAM_BYPASS);
	req->opt0l = htonl(V_RCV_BUFSIZ(rcv_win>>10));
	req->opt1 = htonl(V_CONN_POLICY(CPL_CONN_POLICY_ASK));

	skb->priority = 1;
	return iwch_cxgb3_ofld_send(ep->com.tdev, skb);
}

static int pass_open_rpl(struct t3cdev *tdev, struct sk_buff *skb, void *ctx)
{
	struct iwch_listen_ep *ep = ctx;
	struct cpl_pass_open_rpl *rpl = cplhdr(skb);

	PDBG("%s ep %p status %d error %d\n", __func__, ep,
	     rpl->status, status2errno(rpl->status));
	ep->com.rpl_err = status2errno(rpl->status);
	ep->com.rpl_done = 1;
	wake_up(&ep->com.waitq);

	return CPL_RET_BUF_DONE;
}

static int listen_stop(struct iwch_listen_ep *ep)
{
	struct sk_buff *skb;
	struct cpl_close_listserv_req *req;

	PDBG("%s ep %p\n", __func__, ep);
	skb = get_skb(NULL, sizeof(*req), GFP_KERNEL);
	if (!skb) {
		printk(KERN_ERR MOD "%s - failed to alloc skb\n", __func__);
		return -ENOMEM;
	}
	req = (struct cpl_close_listserv_req *) skb_put(skb, sizeof(*req));
	req->wr.wr_hi = htonl(V_WR_OP(FW_WROPCODE_FORWARD));
	req->cpu_idx = 0;
	OPCODE_TID(req) = htonl(MK_OPCODE_TID(CPL_CLOSE_LISTSRV_REQ, ep->stid));
	skb->priority = 1;
	return iwch_cxgb3_ofld_send(ep->com.tdev, skb);
}

static int close_listsrv_rpl(struct t3cdev *tdev, struct sk_buff *skb,
			     void *ctx)
{
	struct iwch_listen_ep *ep = ctx;
	struct cpl_close_listserv_rpl *rpl = cplhdr(skb);

	PDBG("%s ep %p\n", __func__, ep);
	ep->com.rpl_err = status2errno(rpl->status);
	ep->com.rpl_done = 1;
	wake_up(&ep->com.waitq);
	return CPL_RET_BUF_DONE;
}

static void accept_cr(struct iwch_ep *ep, __be32 peer_ip, struct sk_buff *skb)
{
	struct cpl_pass_accept_rpl *rpl;
	unsigned int mtu_idx;
	u32 opt0h, opt0l, opt2;
	int wscale;

	PDBG("%s ep %p\n", __func__, ep);
	BUG_ON(skb_cloned(skb));
	skb_trim(skb, sizeof(*rpl));
	skb_get(skb);
	mtu_idx = find_best_mtu(T3C_DATA(ep->com.tdev), dst_mtu(ep->dst));
	wscale = compute_wscale(rcv_win);
	opt0h = V_NAGLE(0) |
	    V_NO_CONG(nocong) |
	    V_KEEP_ALIVE(1) |
	    F_TCAM_BYPASS |
	    V_WND_SCALE(wscale) |
	    V_MSS_IDX(mtu_idx) |
	    V_L2T_IDX(ep->l2t->idx) | V_TX_CHANNEL(ep->l2t->smt_idx);
	opt0l = V_TOS((ep->tos >> 2) & M_TOS) | V_RCV_BUFSIZ(rcv_win>>10);
	opt2 = V_FLAVORS_VALID(1) | V_CONG_CONTROL_FLAVOR(cong_flavor);

	rpl = cplhdr(skb);
	rpl->wr.wr_hi = htonl(V_WR_OP(FW_WROPCODE_FORWARD));
	OPCODE_TID(rpl) = htonl(MK_OPCODE_TID(CPL_PASS_ACCEPT_RPL, ep->hwtid));
	rpl->peer_ip = peer_ip;
	rpl->opt0h = htonl(opt0h);
	rpl->opt0l_status = htonl(opt0l | CPL_PASS_OPEN_ACCEPT);
	rpl->opt2 = htonl(opt2);
	rpl->rsvd = rpl->opt2;	/* workaround for HW bug */
	skb->priority = CPL_PRIORITY_SETUP;
	iwch_l2t_send(ep->com.tdev, skb, ep->l2t);

	return;
}

static void reject_cr(struct t3cdev *tdev, u32 hwtid, __be32 peer_ip,
		      struct sk_buff *skb)
{
	PDBG("%s t3cdev %p tid %u peer_ip %x\n", __func__, tdev, hwtid,
	     peer_ip);
	BUG_ON(skb_cloned(skb));
	skb_trim(skb, sizeof(struct cpl_tid_release));
	skb_get(skb);

	if (tdev->type != T3A)
		release_tid(tdev, hwtid, skb);
	else {
		struct cpl_pass_accept_rpl *rpl;

		rpl = cplhdr(skb);
		skb->priority = CPL_PRIORITY_SETUP;
		rpl->wr.wr_hi = htonl(V_WR_OP(FW_WROPCODE_FORWARD));
		OPCODE_TID(rpl) = htonl(MK_OPCODE_TID(CPL_PASS_ACCEPT_RPL,
						      hwtid));
		rpl->peer_ip = peer_ip;
		rpl->opt0h = htonl(F_TCAM_BYPASS);
		rpl->opt0l_status = htonl(CPL_PASS_OPEN_REJECT);
		rpl->opt2 = 0;
		rpl->rsvd = rpl->opt2;
		iwch_cxgb3_ofld_send(tdev, skb);
	}
}

static int pass_accept_req(struct t3cdev *tdev, struct sk_buff *skb, void *ctx)
{
	struct iwch_ep *child_ep, *parent_ep = ctx;
	struct cpl_pass_accept_req *req = cplhdr(skb);
	unsigned int hwtid = GET_TID(req);
	struct dst_entry *dst;
	struct l2t_entry *l2t;
	struct rtable *rt;
	struct iff_mac tim;

	PDBG("%s parent ep %p tid %u\n", __func__, parent_ep, hwtid);

	if (state_read(&parent_ep->com) != LISTEN) {
		printk(KERN_ERR "%s - listening ep not in LISTEN\n",
		       __func__);
		goto reject;
	}

	/*
	 * Find the netdev for this connection request.
	 */
	tim.mac_addr = req->dst_mac;
	tim.vlan_tag = ntohs(req->vlan_tag);
	if (tdev->ctl(tdev, GET_IFF_FROM_MAC, &tim) < 0 || !tim.dev) {
		printk(KERN_ERR "%s bad dst mac %pM\n",
			__func__, req->dst_mac);
		goto reject;
	}

	/* Find output route */
	rt = find_route(tdev,
			req->local_ip,
			req->peer_ip,
			req->local_port,
			req->peer_port, G_PASS_OPEN_TOS(ntohl(req->tos_tid)));
	if (!rt) {
		printk(KERN_ERR MOD "%s - failed to find dst entry!\n",
		       __func__);
		goto reject;
	}
	dst = &rt->u.dst;
	l2t = t3_l2t_get(tdev, dst->neighbour, dst->neighbour->dev);
	if (!l2t) {
		printk(KERN_ERR MOD "%s - failed to allocate l2t entry!\n",
		       __func__);
		dst_release(dst);
		goto reject;
	}
	child_ep = alloc_ep(sizeof(*child_ep), GFP_KERNEL);
	if (!child_ep) {
		printk(KERN_ERR MOD "%s - failed to allocate ep entry!\n",
		       __func__);
		l2t_release(L2DATA(tdev), l2t);
		dst_release(dst);
		goto reject;
	}
	state_set(&child_ep->com, CONNECTING);
	child_ep->com.tdev = tdev;
	child_ep->com.cm_id = NULL;
	child_ep->com.local_addr.sin_family = PF_INET;
	child_ep->com.local_addr.sin_port = req->local_port;
	child_ep->com.local_addr.sin_addr.s_addr = req->local_ip;
	child_ep->com.remote_addr.sin_family = PF_INET;
	child_ep->com.remote_addr.sin_port = req->peer_port;
	child_ep->com.remote_addr.sin_addr.s_addr = req->peer_ip;
	get_ep(&parent_ep->com);
	child_ep->parent_ep = parent_ep;
	child_ep->tos = G_PASS_OPEN_TOS(ntohl(req->tos_tid));
	child_ep->l2t = l2t;
	child_ep->dst = dst;
	child_ep->hwtid = hwtid;
	init_timer(&child_ep->timer);
	cxgb3_insert_tid(tdev, &t3c_client, child_ep, hwtid);
	accept_cr(child_ep, req->peer_ip, skb);
	goto out;
reject:
	reject_cr(tdev, hwtid, req->peer_ip, skb);
out:
	return CPL_RET_BUF_DONE;
}

static int pass_establish(struct t3cdev *tdev, struct sk_buff *skb, void *ctx)
{
	struct iwch_ep *ep = ctx;
	struct cpl_pass_establish *req = cplhdr(skb);

	PDBG("%s ep %p\n", __func__, ep);
	ep->snd_seq = ntohl(req->snd_isn);
	ep->rcv_seq = ntohl(req->rcv_isn);

	set_emss(ep, ntohs(req->tcp_opt));

	dst_confirm(ep->dst);
	state_set(&ep->com, MPA_REQ_WAIT);
	start_ep_timer(ep);

	return CPL_RET_BUF_DONE;
}

static int peer_close(struct t3cdev *tdev, struct sk_buff *skb, void *ctx)
{
	struct iwch_ep *ep = ctx;
	struct iwch_qp_attributes attrs;
	unsigned long flags;
	int disconnect = 1;
	int release = 0;

	PDBG("%s ep %p\n", __func__, ep);
	dst_confirm(ep->dst);

	spin_lock_irqsave(&ep->com.lock, flags);
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
		 * in rdma connection migration (see iwch_accept_cr()).
		 */
		__state_set(&ep->com, CLOSING);
		ep->com.rpl_done = 1;
		ep->com.rpl_err = -ECONNRESET;
		PDBG("waking up ep %p\n", ep);
		wake_up(&ep->com.waitq);
		break;
	case MPA_REP_SENT:
		__state_set(&ep->com, CLOSING);
		ep->com.rpl_done = 1;
		ep->com.rpl_err = -ECONNRESET;
		PDBG("waking up ep %p\n", ep);
		wake_up(&ep->com.waitq);
		break;
	case FPDU_MODE:
		start_ep_timer(ep);
		__state_set(&ep->com, CLOSING);
		attrs.next_state = IWCH_QP_STATE_CLOSING;
		iwch_modify_qp(ep->com.qp->rhp, ep->com.qp,
			       IWCH_QP_ATTR_NEXT_STATE, &attrs, 1);
		peer_close_upcall(ep);
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
			attrs.next_state = IWCH_QP_STATE_IDLE;
			iwch_modify_qp(ep->com.qp->rhp, ep->com.qp,
				       IWCH_QP_ATTR_NEXT_STATE, &attrs, 1);
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
	spin_unlock_irqrestore(&ep->com.lock, flags);
	if (disconnect)
		iwch_ep_disconnect(ep, 0, GFP_KERNEL);
	if (release)
		release_ep_resources(ep);
	return CPL_RET_BUF_DONE;
}

/*
 * Returns whether an ABORT_REQ_RSS message is a negative advice.
 */
static int is_neg_adv_abort(unsigned int status)
{
	return status == CPL_ERR_RTX_NEG_ADVICE ||
	       status == CPL_ERR_PERSIST_NEG_ADVICE;
}

static int peer_abort(struct t3cdev *tdev, struct sk_buff *skb, void *ctx)
{
	struct cpl_abort_req_rss *req = cplhdr(skb);
	struct iwch_ep *ep = ctx;
	struct cpl_abort_rpl *rpl;
	struct sk_buff *rpl_skb;
	struct iwch_qp_attributes attrs;
	int ret;
	int release = 0;
	unsigned long flags;

	if (is_neg_adv_abort(req->status)) {
		PDBG("%s neg_adv_abort ep %p tid %d\n", __func__, ep,
		     ep->hwtid);
		t3_l2t_send_event(ep->com.tdev, ep->l2t);
		return CPL_RET_BUF_DONE;
	}

	/*
	 * We get 2 peer aborts from the HW.  The first one must
	 * be ignored except for scribbling that we need one more.
	 */
	if (!test_and_set_bit(PEER_ABORT_IN_PROGRESS, &ep->com.flags)) {
		return CPL_RET_BUF_DONE;
	}

	spin_lock_irqsave(&ep->com.lock, flags);
	PDBG("%s ep %p state %u\n", __func__, ep, ep->com.state);
	switch (ep->com.state) {
	case CONNECTING:
		break;
	case MPA_REQ_WAIT:
		stop_ep_timer(ep);
		break;
	case MPA_REQ_SENT:
		stop_ep_timer(ep);
		connect_reply_upcall(ep, -ECONNRESET);
		break;
	case MPA_REP_SENT:
		ep->com.rpl_done = 1;
		ep->com.rpl_err = -ECONNRESET;
		PDBG("waking up ep %p\n", ep);
		wake_up(&ep->com.waitq);
		break;
	case MPA_REQ_RCVD:

		/*
		 * We're gonna mark this puppy DEAD, but keep
		 * the reference on it until the ULP accepts or
		 * rejects the CR. Also wake up anyone waiting
		 * in rdma connection migration (see iwch_accept_cr()).
		 */
		ep->com.rpl_done = 1;
		ep->com.rpl_err = -ECONNRESET;
		PDBG("waking up ep %p\n", ep);
		wake_up(&ep->com.waitq);
		break;
	case MORIBUND:
	case CLOSING:
		stop_ep_timer(ep);
		/*FALLTHROUGH*/
	case FPDU_MODE:
		if (ep->com.cm_id && ep->com.qp) {
			attrs.next_state = IWCH_QP_STATE_ERROR;
			ret = iwch_modify_qp(ep->com.qp->rhp,
				     ep->com.qp, IWCH_QP_ATTR_NEXT_STATE,
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
		spin_unlock_irqrestore(&ep->com.lock, flags);
		return CPL_RET_BUF_DONE;
	default:
		BUG_ON(1);
		break;
	}
	dst_confirm(ep->dst);
	if (ep->com.state != ABORTING) {
		__state_set(&ep->com, DEAD);
		release = 1;
	}
	spin_unlock_irqrestore(&ep->com.lock, flags);

	rpl_skb = get_skb(skb, sizeof(*rpl), GFP_KERNEL);
	if (!rpl_skb) {
		printk(KERN_ERR MOD "%s - cannot allocate skb!\n",
		       __func__);
		release = 1;
		goto out;
	}
	rpl_skb->priority = CPL_PRIORITY_DATA;
	rpl = (struct cpl_abort_rpl *) skb_put(rpl_skb, sizeof(*rpl));
	rpl->wr.wr_hi = htonl(V_WR_OP(FW_WROPCODE_OFLD_HOST_ABORT_CON_RPL));
	rpl->wr.wr_lo = htonl(V_WR_TID(ep->hwtid));
	OPCODE_TID(rpl) = htonl(MK_OPCODE_TID(CPL_ABORT_RPL, ep->hwtid));
	rpl->cmd = CPL_ABORT_NO_RST;
	iwch_cxgb3_ofld_send(ep->com.tdev, rpl_skb);
out:
	if (release)
		release_ep_resources(ep);
	return CPL_RET_BUF_DONE;
}

static int close_con_rpl(struct t3cdev *tdev, struct sk_buff *skb, void *ctx)
{
	struct iwch_ep *ep = ctx;
	struct iwch_qp_attributes attrs;
	unsigned long flags;
	int release = 0;

	PDBG("%s ep %p\n", __func__, ep);
	BUG_ON(!ep);

	/* The cm_id may be null if we failed to connect */
	spin_lock_irqsave(&ep->com.lock, flags);
	switch (ep->com.state) {
	case CLOSING:
		__state_set(&ep->com, MORIBUND);
		break;
	case MORIBUND:
		stop_ep_timer(ep);
		if ((ep->com.cm_id) && (ep->com.qp)) {
			attrs.next_state = IWCH_QP_STATE_IDLE;
			iwch_modify_qp(ep->com.qp->rhp,
					     ep->com.qp,
					     IWCH_QP_ATTR_NEXT_STATE,
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
	spin_unlock_irqrestore(&ep->com.lock, flags);
	if (release)
		release_ep_resources(ep);
	return CPL_RET_BUF_DONE;
}

/*
 * T3A does 3 things when a TERM is received:
 * 1) send up a CPL_RDMA_TERMINATE message with the TERM packet
 * 2) generate an async event on the QP with the TERMINATE opcode
 * 3) post a TERMINATE opcde cqe into the associated CQ.
 *
 * For (1), we save the message in the qp for later consumer consumption.
 * For (2), we move the QP into TERMINATE, post a QP event and disconnect.
 * For (3), we toss the CQE in cxio_poll_cq().
 *
 * terminate() handles case (1)...
 */
static int terminate(struct t3cdev *tdev, struct sk_buff *skb, void *ctx)
{
	struct iwch_ep *ep = ctx;

	if (state_read(&ep->com) != FPDU_MODE)
		return CPL_RET_BUF_DONE;

	PDBG("%s ep %p\n", __func__, ep);
	skb_pull(skb, sizeof(struct cpl_rdma_terminate));
	PDBG("%s saving %d bytes of term msg\n", __func__, skb->len);
	skb_copy_from_linear_data(skb, ep->com.qp->attr.terminate_buffer,
				  skb->len);
	ep->com.qp->attr.terminate_msg_len = skb->len;
	ep->com.qp->attr.is_terminate_local = 0;
	return CPL_RET_BUF_DONE;
}

static int ec_status(struct t3cdev *tdev, struct sk_buff *skb, void *ctx)
{
	struct cpl_rdma_ec_status *rep = cplhdr(skb);
	struct iwch_ep *ep = ctx;

	PDBG("%s ep %p tid %u status %d\n", __func__, ep, ep->hwtid,
	     rep->status);
	if (rep->status) {
		struct iwch_qp_attributes attrs;

		printk(KERN_ERR MOD "%s BAD CLOSE - Aborting tid %u\n",
		       __func__, ep->hwtid);
		stop_ep_timer(ep);
		attrs.next_state = IWCH_QP_STATE_ERROR;
		iwch_modify_qp(ep->com.qp->rhp,
			       ep->com.qp, IWCH_QP_ATTR_NEXT_STATE,
			       &attrs, 1);
		abort_connection(ep, NULL, GFP_KERNEL);
	}
	return CPL_RET_BUF_DONE;
}

static void ep_timeout(unsigned long arg)
{
	struct iwch_ep *ep = (struct iwch_ep *)arg;
	struct iwch_qp_attributes attrs;
	unsigned long flags;
	int abort = 1;

	spin_lock_irqsave(&ep->com.lock, flags);
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
			attrs.next_state = IWCH_QP_STATE_ERROR;
			iwch_modify_qp(ep->com.qp->rhp,
				     ep->com.qp, IWCH_QP_ATTR_NEXT_STATE,
				     &attrs, 1);
		}
		__state_set(&ep->com, ABORTING);
		break;
	default:
		printk(KERN_ERR "%s unexpected state ep %p state %u\n",
			__func__, ep, ep->com.state);
		WARN_ON(1);
		abort = 0;
	}
	spin_unlock_irqrestore(&ep->com.lock, flags);
	if (abort)
		abort_connection(ep, NULL, GFP_ATOMIC);
	put_ep(&ep->com);
}

int iwch_reject_cr(struct iw_cm_id *cm_id, const void *pdata, u8 pdata_len)
{
	int err;
	struct iwch_ep *ep = to_ep(cm_id);
	PDBG("%s ep %p tid %u\n", __func__, ep, ep->hwtid);

	if (state_read(&ep->com) == DEAD) {
		put_ep(&ep->com);
		return -ECONNRESET;
	}
	BUG_ON(state_read(&ep->com) != MPA_REQ_RCVD);
	if (mpa_rev == 0)
		abort_connection(ep, NULL, GFP_KERNEL);
	else {
		err = send_mpa_reject(ep, pdata, pdata_len);
		err = iwch_ep_disconnect(ep, 0, GFP_KERNEL);
	}
	put_ep(&ep->com);
	return 0;
}

int iwch_accept_cr(struct iw_cm_id *cm_id, struct iw_cm_conn_param *conn_param)
{
	int err;
	struct iwch_qp_attributes attrs;
	enum iwch_qp_attr_mask mask;
	struct iwch_ep *ep = to_ep(cm_id);
	struct iwch_dev *h = to_iwch_dev(cm_id->device);
	struct iwch_qp *qp = get_qhp(h, conn_param->qpn);

	PDBG("%s ep %p tid %u\n", __func__, ep, ep->hwtid);
	if (state_read(&ep->com) == DEAD) {
		err = -ECONNRESET;
		goto err;
	}

	BUG_ON(state_read(&ep->com) != MPA_REQ_RCVD);
	BUG_ON(!qp);

	if ((conn_param->ord > qp->rhp->attr.max_rdma_read_qp_depth) ||
	    (conn_param->ird > qp->rhp->attr.max_rdma_reads_per_qp)) {
		abort_connection(ep, NULL, GFP_KERNEL);
		err = -EINVAL;
		goto err;
	}

	cm_id->add_ref(cm_id);
	ep->com.cm_id = cm_id;
	ep->com.qp = qp;

	ep->ird = conn_param->ird;
	ep->ord = conn_param->ord;

	if (peer2peer && ep->ird == 0)
		ep->ird = 1;

	PDBG("%s %d ird %d ord %d\n", __func__, __LINE__, ep->ird, ep->ord);

	/* bind QP to EP and move to RTS */
	attrs.mpa_attr = ep->mpa_attr;
	attrs.max_ird = ep->ird;
	attrs.max_ord = ep->ord;
	attrs.llp_stream_handle = ep;
	attrs.next_state = IWCH_QP_STATE_RTS;

	/* bind QP and TID with INIT_WR */
	mask = IWCH_QP_ATTR_NEXT_STATE |
			     IWCH_QP_ATTR_LLP_STREAM_HANDLE |
			     IWCH_QP_ATTR_MPA_ATTR |
			     IWCH_QP_ATTR_MAX_IRD |
			     IWCH_QP_ATTR_MAX_ORD;

	err = iwch_modify_qp(ep->com.qp->rhp,
			     ep->com.qp, mask, &attrs, 1);
	if (err)
		goto err1;

	/* if needed, wait for wr_ack */
	if (iwch_rqes_posted(qp)) {
		wait_event(ep->com.waitq, ep->com.rpl_done);
		err = ep->com.rpl_err;
		if (err)
			goto err1;
	}

	err = send_mpa_reply(ep, conn_param->private_data,
			     conn_param->private_data_len);
	if (err)
		goto err1;


	state_set(&ep->com, FPDU_MODE);
	established_upcall(ep);
	put_ep(&ep->com);
	return 0;
err1:
	ep->com.cm_id = NULL;
	ep->com.qp = NULL;
	cm_id->rem_ref(cm_id);
err:
	put_ep(&ep->com);
	return err;
}

static int is_loopback_dst(struct iw_cm_id *cm_id)
{
	struct net_device *dev;

	dev = ip_dev_find(&init_net, cm_id->remote_addr.sin_addr.s_addr);
	if (!dev)
		return 0;
	dev_put(dev);
	return 1;
}

int iwch_connect(struct iw_cm_id *cm_id, struct iw_cm_conn_param *conn_param)
{
	int err = 0;
	struct iwch_dev *h = to_iwch_dev(cm_id->device);
	struct iwch_ep *ep;
	struct rtable *rt;

	if (is_loopback_dst(cm_id)) {
		err = -ENOSYS;
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

	ep->com.tdev = h->rdev.t3cdev_p;

	cm_id->add_ref(cm_id);
	ep->com.cm_id = cm_id;
	ep->com.qp = get_qhp(h, conn_param->qpn);
	BUG_ON(!ep->com.qp);
	PDBG("%s qpn 0x%x qp %p cm_id %p\n", __func__, conn_param->qpn,
	     ep->com.qp, cm_id);

	/*
	 * Allocate an active TID to initiate a TCP connection.
	 */
	ep->atid = cxgb3_alloc_atid(h->rdev.t3cdev_p, &t3c_client, ep);
	if (ep->atid == -1) {
		printk(KERN_ERR MOD "%s - cannot alloc atid.\n", __func__);
		err = -ENOMEM;
		goto fail2;
	}

	/* find a route */
	rt = find_route(h->rdev.t3cdev_p,
			cm_id->local_addr.sin_addr.s_addr,
			cm_id->remote_addr.sin_addr.s_addr,
			cm_id->local_addr.sin_port,
			cm_id->remote_addr.sin_port, IPTOS_LOWDELAY);
	if (!rt) {
		printk(KERN_ERR MOD "%s - cannot find route.\n", __func__);
		err = -EHOSTUNREACH;
		goto fail3;
	}
	ep->dst = &rt->u.dst;

	/* get a l2t entry */
	ep->l2t = t3_l2t_get(ep->com.tdev, ep->dst->neighbour,
			     ep->dst->neighbour->dev);
	if (!ep->l2t) {
		printk(KERN_ERR MOD "%s - cannot alloc l2e.\n", __func__);
		err = -ENOMEM;
		goto fail4;
	}

	state_set(&ep->com, CONNECTING);
	ep->tos = IPTOS_LOWDELAY;
	ep->com.local_addr = cm_id->local_addr;
	ep->com.remote_addr = cm_id->remote_addr;

	/* send connect request to rnic */
	err = send_connect(ep);
	if (!err)
		goto out;

	l2t_release(L2DATA(h->rdev.t3cdev_p), ep->l2t);
fail4:
	dst_release(ep->dst);
fail3:
	cxgb3_free_atid(ep->com.tdev, ep->atid);
fail2:
	cm_id->rem_ref(cm_id);
	put_ep(&ep->com);
out:
	return err;
}

int iwch_create_listen(struct iw_cm_id *cm_id, int backlog)
{
	int err = 0;
	struct iwch_dev *h = to_iwch_dev(cm_id->device);
	struct iwch_listen_ep *ep;


	might_sleep();

	ep = alloc_ep(sizeof(*ep), GFP_KERNEL);
	if (!ep) {
		printk(KERN_ERR MOD "%s - cannot alloc ep.\n", __func__);
		err = -ENOMEM;
		goto fail1;
	}
	PDBG("%s ep %p\n", __func__, ep);
	ep->com.tdev = h->rdev.t3cdev_p;
	cm_id->add_ref(cm_id);
	ep->com.cm_id = cm_id;
	ep->backlog = backlog;
	ep->com.local_addr = cm_id->local_addr;

	/*
	 * Allocate a server TID.
	 */
	ep->stid = cxgb3_alloc_stid(h->rdev.t3cdev_p, &t3c_client, ep);
	if (ep->stid == -1) {
		printk(KERN_ERR MOD "%s - cannot alloc atid.\n", __func__);
		err = -ENOMEM;
		goto fail2;
	}

	state_set(&ep->com, LISTEN);
	err = listen_start(ep);
	if (err)
		goto fail3;

	/* wait for pass_open_rpl */
	wait_event(ep->com.waitq, ep->com.rpl_done);
	err = ep->com.rpl_err;
	if (!err) {
		cm_id->provider_data = ep;
		goto out;
	}
fail3:
	cxgb3_free_stid(ep->com.tdev, ep->stid);
fail2:
	cm_id->rem_ref(cm_id);
	put_ep(&ep->com);
fail1:
out:
	return err;
}

int iwch_destroy_listen(struct iw_cm_id *cm_id)
{
	int err;
	struct iwch_listen_ep *ep = to_listen_ep(cm_id);

	PDBG("%s ep %p\n", __func__, ep);

	might_sleep();
	state_set(&ep->com, DEAD);
	ep->com.rpl_done = 0;
	ep->com.rpl_err = 0;
	err = listen_stop(ep);
	if (err)
		goto done;
	wait_event(ep->com.waitq, ep->com.rpl_done);
	cxgb3_free_stid(ep->com.tdev, ep->stid);
done:
	err = ep->com.rpl_err;
	cm_id->rem_ref(cm_id);
	put_ep(&ep->com);
	return err;
}

int iwch_ep_disconnect(struct iwch_ep *ep, int abrupt, gfp_t gfp)
{
	int ret=0;
	unsigned long flags;
	int close = 0;
	int fatal = 0;
	struct t3cdev *tdev;
	struct cxio_rdev *rdev;

	spin_lock_irqsave(&ep->com.lock, flags);

	PDBG("%s ep %p state %s, abrupt %d\n", __func__, ep,
	     states[ep->com.state], abrupt);

	tdev = (struct t3cdev *)ep->com.tdev;
	rdev = (struct cxio_rdev *)tdev->ulp;
	if (cxio_fatal_error(rdev)) {
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

	spin_unlock_irqrestore(&ep->com.lock, flags);
	if (close) {
		if (abrupt)
			ret = send_abort(ep, NULL, gfp);
		else
			ret = send_halfclose(ep, gfp);
		if (ret)
			fatal = 1;
	}
	if (fatal)
		release_ep_resources(ep);
	return ret;
}

int iwch_ep_redirect(void *ctx, struct dst_entry *old, struct dst_entry *new,
		     struct l2t_entry *l2t)
{
	struct iwch_ep *ep = ctx;

	if (ep->dst != old)
		return 0;

	PDBG("%s ep %p redirect to dst %p l2t %p\n", __func__, ep, new,
	     l2t);
	dst_hold(new);
	l2t_release(L2DATA(ep->com.tdev), ep->l2t);
	ep->l2t = l2t;
	dst_release(old);
	ep->dst = new;
	return 1;
}

/*
 * All the CM events are handled on a work queue to have a safe context.
 * These are the real handlers that are called from the work queue.
 */
static const cxgb3_cpl_handler_func work_handlers[NUM_CPL_CMDS] = {
	[CPL_ACT_ESTABLISH]	= act_establish,
	[CPL_ACT_OPEN_RPL]	= act_open_rpl,
	[CPL_RX_DATA]		= rx_data,
	[CPL_TX_DMA_ACK]	= tx_ack,
	[CPL_ABORT_RPL_RSS]	= abort_rpl,
	[CPL_ABORT_RPL]		= abort_rpl,
	[CPL_PASS_OPEN_RPL]	= pass_open_rpl,
	[CPL_CLOSE_LISTSRV_RPL]	= close_listsrv_rpl,
	[CPL_PASS_ACCEPT_REQ]	= pass_accept_req,
	[CPL_PASS_ESTABLISH]	= pass_establish,
	[CPL_PEER_CLOSE]	= peer_close,
	[CPL_ABORT_REQ_RSS]	= peer_abort,
	[CPL_CLOSE_CON_RPL]	= close_con_rpl,
	[CPL_RDMA_TERMINATE]	= terminate,
	[CPL_RDMA_EC_STATUS]	= ec_status,
};

static void process_work(struct work_struct *work)
{
	struct sk_buff *skb = NULL;
	void *ep;
	struct t3cdev *tdev;
	int ret;

	while ((skb = skb_dequeue(&rxq))) {
		ep = *((void **) (skb->cb));
		tdev = *((struct t3cdev **) (skb->cb + sizeof(void *)));
		ret = work_handlers[G_OPCODE(ntohl((__force __be32)skb->csum))](tdev, skb, ep);
		if (ret & CPL_RET_BUF_DONE)
			kfree_skb(skb);

		/*
		 * ep was referenced in sched(), and is freed here.
		 */
		put_ep((struct iwch_ep_common *)ep);
	}
}

static DECLARE_WORK(skb_work, process_work);

static int sched(struct t3cdev *tdev, struct sk_buff *skb, void *ctx)
{
	struct iwch_ep_common *epc = ctx;

	get_ep(epc);

	/*
	 * Save ctx and tdev in the skb->cb area.
	 */
	*((void **) skb->cb) = ctx;
	*((struct t3cdev **) (skb->cb + sizeof(void *))) = tdev;

	/*
	 * Queue the skb and schedule the worker thread.
	 */
	skb_queue_tail(&rxq, skb);
	queue_work(workq, &skb_work);
	return 0;
}

static int set_tcb_rpl(struct t3cdev *tdev, struct sk_buff *skb, void *ctx)
{
	struct cpl_set_tcb_rpl *rpl = cplhdr(skb);

	if (rpl->status != CPL_ERR_NONE) {
		printk(KERN_ERR MOD "Unexpected SET_TCB_RPL status %u "
		       "for tid %u\n", rpl->status, GET_TID(rpl));
	}
	return CPL_RET_BUF_DONE;
}

/*
 * All upcalls from the T3 Core go to sched() to schedule the
 * processing on a work queue.
 */
cxgb3_cpl_handler_func t3c_handlers[NUM_CPL_CMDS] = {
	[CPL_ACT_ESTABLISH]	= sched,
	[CPL_ACT_OPEN_RPL]	= sched,
	[CPL_RX_DATA]		= sched,
	[CPL_TX_DMA_ACK]	= sched,
	[CPL_ABORT_RPL_RSS]	= sched,
	[CPL_ABORT_RPL]		= sched,
	[CPL_PASS_OPEN_RPL]	= sched,
	[CPL_CLOSE_LISTSRV_RPL]	= sched,
	[CPL_PASS_ACCEPT_REQ]	= sched,
	[CPL_PASS_ESTABLISH]	= sched,
	[CPL_PEER_CLOSE]	= sched,
	[CPL_CLOSE_CON_RPL]	= sched,
	[CPL_ABORT_REQ_RSS]	= sched,
	[CPL_RDMA_TERMINATE]	= sched,
	[CPL_RDMA_EC_STATUS]	= sched,
	[CPL_SET_TCB_RPL]	= set_tcb_rpl,
};

int __init iwch_cm_init(void)
{
	skb_queue_head_init(&rxq);

	workq = create_singlethread_workqueue("iw_cxgb3");
	if (!workq)
		return -ENOMEM;

	return 0;
}

void __exit iwch_cm_term(void)
{
	flush_workqueue(workq);
	destroy_workqueue(workq);
}
