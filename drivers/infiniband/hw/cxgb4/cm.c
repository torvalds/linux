/*
 * Copyright (c) 2009-2014 Chelsio, Inc. All rights reserved.
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
#include <linux/if_vlan.h>

#include <net/neighbour.h>
#include <net/netevent.h>
#include <net/route.h>
#include <net/tcp.h>
#include <net/ip6_route.h>
#include <net/addrconf.h>

#include <rdma/ib_addr.h>

#include "iw_cxgb4.h"
#include "clip_tbl.h"

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

static int nocong;
module_param(nocong, int, 0644);
MODULE_PARM_DESC(nocong, "Turn of congestion control (default=0)");

static int enable_ecn;
module_param(enable_ecn, int, 0644);
MODULE_PARM_DESC(enable_ecn, "Enable ECN (default=0/disabled)");

static int dack_mode = 1;
module_param(dack_mode, int, 0644);
MODULE_PARM_DESC(dack_mode, "Delayed ack mode (default=1)");

uint c4iw_max_read_depth = 32;
module_param(c4iw_max_read_depth, int, 0644);
MODULE_PARM_DESC(c4iw_max_read_depth,
		 "Per-connection max ORD/IRD (default=32)");

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

static int peer2peer = 1;
module_param(peer2peer, int, 0644);
MODULE_PARM_DESC(peer2peer, "Support peer2peer ULPs (default=1)");

static int p2p_type = FW_RI_INIT_P2PTYPE_READ_REQ;
module_param(p2p_type, int, 0644);
MODULE_PARM_DESC(p2p_type, "RDMAP opcode to use for the RTR message: "
			   "1=RDMA_READ 0=RDMA_WRITE (default 1)");

static int ep_timeout_secs = 60;
module_param(ep_timeout_secs, int, 0644);
MODULE_PARM_DESC(ep_timeout_secs, "CM Endpoint operation timeout "
				   "in seconds (default=60)");

static int mpa_rev = 2;
module_param(mpa_rev, int, 0644);
MODULE_PARM_DESC(mpa_rev, "MPA Revision, 0 supports amso1100, "
		"1 is RFC5044 spec compliant, 2 is IETF MPA Peer Connect Draft"
		" compliant (default=2)");

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
static int sched(struct c4iw_dev *dev, struct sk_buff *skb);

static LIST_HEAD(timeout_list);
static spinlock_t timeout_lock;

static void deref_cm_id(struct c4iw_ep_common *epc)
{
	epc->cm_id->rem_ref(epc->cm_id);
	epc->cm_id = NULL;
	set_bit(CM_ID_DEREFED, &epc->history);
}

static void ref_cm_id(struct c4iw_ep_common *epc)
{
	set_bit(CM_ID_REFED, &epc->history);
	epc->cm_id->add_ref(epc->cm_id);
}

static void deref_qp(struct c4iw_ep *ep)
{
	c4iw_qp_rem_ref(&ep->com.qp->ibqp);
	clear_bit(QP_REFERENCED, &ep->com.flags);
	set_bit(QP_DEREFED, &ep->com.history);
}

static void ref_qp(struct c4iw_ep *ep)
{
	set_bit(QP_REFERENCED, &ep->com.flags);
	set_bit(QP_REFED, &ep->com.history);
	c4iw_qp_add_ref(&ep->com.qp->ibqp);
}

static void start_ep_timer(struct c4iw_ep *ep)
{
	PDBG("%s ep %p\n", __func__, ep);
	if (timer_pending(&ep->timer)) {
		pr_err("%s timer already started! ep %p\n",
		       __func__, ep);
		return;
	}
	clear_bit(TIMEOUT, &ep->com.flags);
	c4iw_get_ep(&ep->com);
	ep->timer.expires = jiffies + ep_timeout_secs * HZ;
	ep->timer.data = (unsigned long)ep;
	ep->timer.function = ep_timeout;
	add_timer(&ep->timer);
}

static int stop_ep_timer(struct c4iw_ep *ep)
{
	PDBG("%s ep %p stopping\n", __func__, ep);
	del_timer_sync(&ep->timer);
	if (!test_and_set_bit(TIMEOUT, &ep->com.flags)) {
		c4iw_put_ep(&ep->com);
		return 0;
	}
	return 1;
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
	else if (error == NET_XMIT_DROP)
		return -ENOMEM;
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
	ep->emss = ep->com.dev->rdev.lldi.mtus[TCPOPT_MSS_G(opt)] -
		   ((AF_INET == ep->com.remote_addr.ss_family) ?
		    sizeof(struct iphdr) : sizeof(struct ipv6hdr)) -
		   sizeof(struct tcphdr);
	ep->mss = ep->emss;
	if (TCPOPT_TSTAMP_G(opt))
		ep->emss -= round_up(TCPOLEN_TIMESTAMP, 4);
	if (ep->emss < 128)
		ep->emss = 128;
	if (ep->emss & 7)
		PDBG("Warning: misaligned mtu idx %u mss %u emss=%u\n",
		     TCPOPT_MSS_G(opt), ep->mss, ep->emss);
	PDBG("%s mss_idx %u mss %u emss=%u\n", __func__, TCPOPT_MSS_G(opt),
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

static int alloc_ep_skb_list(struct sk_buff_head *ep_skb_list, int size)
{
	struct sk_buff *skb;
	unsigned int i;
	size_t len;

	len = roundup(sizeof(union cpl_wr_size), 16);
	for (i = 0; i < size; i++) {
		skb = alloc_skb(len, GFP_KERNEL);
		if (!skb)
			goto fail;
		skb_queue_tail(ep_skb_list, skb);
	}
	return 0;
fail:
	skb_queue_purge(ep_skb_list);
	return -ENOMEM;
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

static void remove_ep_tid(struct c4iw_ep *ep)
{
	unsigned long flags;

	spin_lock_irqsave(&ep->com.dev->lock, flags);
	_remove_handle(ep->com.dev, &ep->com.dev->hwtid_idr, ep->hwtid, 0);
	spin_unlock_irqrestore(&ep->com.dev->lock, flags);
}

static void insert_ep_tid(struct c4iw_ep *ep)
{
	unsigned long flags;

	spin_lock_irqsave(&ep->com.dev->lock, flags);
	_insert_handle(ep->com.dev, &ep->com.dev->hwtid_idr, ep, ep->hwtid, 0);
	spin_unlock_irqrestore(&ep->com.dev->lock, flags);
}

/*
 * Atomically lookup the ep ptr given the tid and grab a reference on the ep.
 */
static struct c4iw_ep *get_ep_from_tid(struct c4iw_dev *dev, unsigned int tid)
{
	struct c4iw_ep *ep;
	unsigned long flags;

	spin_lock_irqsave(&dev->lock, flags);
	ep = idr_find(&dev->hwtid_idr, tid);
	if (ep)
		c4iw_get_ep(&ep->com);
	spin_unlock_irqrestore(&dev->lock, flags);
	return ep;
}

/*
 * Atomically lookup the ep ptr given the stid and grab a reference on the ep.
 */
static struct c4iw_listen_ep *get_ep_from_stid(struct c4iw_dev *dev,
					       unsigned int stid)
{
	struct c4iw_listen_ep *ep;
	unsigned long flags;

	spin_lock_irqsave(&dev->lock, flags);
	ep = idr_find(&dev->stid_idr, stid);
	if (ep)
		c4iw_get_ep(&ep->com);
	spin_unlock_irqrestore(&dev->lock, flags);
	return ep;
}

void _c4iw_free_ep(struct kref *kref)
{
	struct c4iw_ep *ep;

	ep = container_of(kref, struct c4iw_ep, com.kref);
	PDBG("%s ep %p state %s\n", __func__, ep, states[ep->com.state]);
	if (test_bit(QP_REFERENCED, &ep->com.flags))
		deref_qp(ep);
	if (test_bit(RELEASE_RESOURCES, &ep->com.flags)) {
		if (ep->com.remote_addr.ss_family == AF_INET6) {
			struct sockaddr_in6 *sin6 =
					(struct sockaddr_in6 *)
					&ep->com.local_addr;

			cxgb4_clip_release(
					ep->com.dev->rdev.lldi.ports[0],
					(const u32 *)&sin6->sin6_addr.s6_addr,
					1);
		}
		cxgb4_remove_tid(ep->com.dev->rdev.lldi.tids, 0, ep->hwtid);
		dst_release(ep->dst);
		cxgb4_l2t_release(ep->l2t);
		if (ep->mpa_skb)
			kfree_skb(ep->mpa_skb);
	}
	if (!skb_queue_empty(&ep->com.ep_skb_list))
		skb_queue_purge(&ep->com.ep_skb_list);
	kfree(ep);
}

static void release_ep_resources(struct c4iw_ep *ep)
{
	set_bit(RELEASE_RESOURCES, &ep->com.flags);

	/*
	 * If we have a hwtid, then remove it from the idr table
	 * so lookups will no longer find this endpoint.  Otherwise
	 * we have a race where one thread finds the ep ptr just
	 * before the other thread is freeing the ep memory.
	 */
	if (ep->hwtid != -1)
		remove_ep_tid(ep);
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
	t4_set_arp_err_handler(skb, NULL, NULL);
	return skb;
}

static struct net_device *get_real_dev(struct net_device *egress_dev)
{
	return rdma_vlan_dev_real_dev(egress_dev) ? : egress_dev;
}

static int our_interface(struct c4iw_dev *dev, struct net_device *egress_dev)
{
	int i;

	egress_dev = get_real_dev(egress_dev);
	for (i = 0; i < dev->rdev.lldi.nports; i++)
		if (dev->rdev.lldi.ports[i] == egress_dev)
			return 1;
	return 0;
}

static struct dst_entry *find_route6(struct c4iw_dev *dev, __u8 *local_ip,
				     __u8 *peer_ip, __be16 local_port,
				     __be16 peer_port, u8 tos,
				     __u32 sin6_scope_id)
{
	struct dst_entry *dst = NULL;

	if (IS_ENABLED(CONFIG_IPV6)) {
		struct flowi6 fl6;

		memset(&fl6, 0, sizeof(fl6));
		memcpy(&fl6.daddr, peer_ip, 16);
		memcpy(&fl6.saddr, local_ip, 16);
		if (ipv6_addr_type(&fl6.daddr) & IPV6_ADDR_LINKLOCAL)
			fl6.flowi6_oif = sin6_scope_id;
		dst = ip6_route_output(&init_net, NULL, &fl6);
		if (!dst)
			goto out;
		if (!our_interface(dev, ip6_dst_idev(dst)->dev) &&
		    !(ip6_dst_idev(dst)->dev->flags & IFF_LOOPBACK)) {
			dst_release(dst);
			dst = NULL;
		}
	}

out:
	return dst;
}

static struct dst_entry *find_route(struct c4iw_dev *dev, __be32 local_ip,
				 __be32 peer_ip, __be16 local_port,
				 __be16 peer_port, u8 tos)
{
	struct rtable *rt;
	struct flowi4 fl4;
	struct neighbour *n;

	rt = ip_route_output_ports(&init_net, &fl4, NULL, peer_ip, local_ip,
				   peer_port, local_port, IPPROTO_TCP,
				   tos, 0);
	if (IS_ERR(rt))
		return NULL;
	n = dst_neigh_lookup(&rt->dst, &peer_ip);
	if (!n)
		return NULL;
	if (!our_interface(dev, n->dev) &&
	    !(n->dev->flags & IFF_LOOPBACK)) {
		neigh_release(n);
		dst_release(&rt->dst);
		return NULL;
	}
	neigh_release(n);
	return &rt->dst;
}

static void arp_failure_discard(void *handle, struct sk_buff *skb)
{
	pr_err(MOD "ARP failure\n");
	kfree_skb(skb);
}

static void mpa_start_arp_failure(void *handle, struct sk_buff *skb)
{
	pr_err("ARP failure during MPA Negotiation - Closing Connection\n");
}

enum {
	NUM_FAKE_CPLS = 2,
	FAKE_CPL_PUT_EP_SAFE = NUM_CPL_CMDS + 0,
	FAKE_CPL_PASS_PUT_EP_SAFE = NUM_CPL_CMDS + 1,
};

static int _put_ep_safe(struct c4iw_dev *dev, struct sk_buff *skb)
{
	struct c4iw_ep *ep;

	ep = *((struct c4iw_ep **)(skb->cb + 2 * sizeof(void *)));
	release_ep_resources(ep);
	return 0;
}

static int _put_pass_ep_safe(struct c4iw_dev *dev, struct sk_buff *skb)
{
	struct c4iw_ep *ep;

	ep = *((struct c4iw_ep **)(skb->cb + 2 * sizeof(void *)));
	c4iw_put_ep(&ep->parent_ep->com);
	release_ep_resources(ep);
	return 0;
}

/*
 * Fake up a special CPL opcode and call sched() so process_work() will call
 * _put_ep_safe() in a safe context to free the ep resources.  This is needed
 * because ARP error handlers are called in an ATOMIC context, and
 * _c4iw_free_ep() needs to block.
 */
static void queue_arp_failure_cpl(struct c4iw_ep *ep, struct sk_buff *skb,
				  int cpl)
{
	struct cpl_act_establish *rpl = cplhdr(skb);

	/* Set our special ARP_FAILURE opcode */
	rpl->ot.opcode = cpl;

	/*
	 * Save ep in the skb->cb area, after where sched() will save the dev
	 * ptr.
	 */
	*((struct c4iw_ep **)(skb->cb + 2 * sizeof(void *))) = ep;
	sched(ep->com.dev, skb);
}

/* Handle an ARP failure for an accept */
static void pass_accept_rpl_arp_failure(void *handle, struct sk_buff *skb)
{
	struct c4iw_ep *ep = handle;

	pr_err(MOD "ARP failure during accept - tid %u -dropping connection\n",
	       ep->hwtid);

	__state_set(&ep->com, DEAD);
	queue_arp_failure_cpl(ep, skb, FAKE_CPL_PASS_PUT_EP_SAFE);
}

/*
 * Handle an ARP failure for an active open.
 */
static void act_open_req_arp_failure(void *handle, struct sk_buff *skb)
{
	struct c4iw_ep *ep = handle;

	printk(KERN_ERR MOD "ARP failure during connect\n");
	connect_reply_upcall(ep, -EHOSTUNREACH);
	__state_set(&ep->com, DEAD);
	if (ep->com.remote_addr.ss_family == AF_INET6) {
		struct sockaddr_in6 *sin6 =
			(struct sockaddr_in6 *)&ep->com.local_addr;
		cxgb4_clip_release(ep->com.dev->rdev.lldi.ports[0],
				   (const u32 *)&sin6->sin6_addr.s6_addr, 1);
	}
	remove_handle(ep->com.dev, &ep->com.dev->atid_idr, ep->atid);
	cxgb4_free_atid(ep->com.dev->rdev.lldi.tids, ep->atid);
	queue_arp_failure_cpl(ep, skb, FAKE_CPL_PUT_EP_SAFE);
}

/*
 * Handle an ARP failure for a CPL_ABORT_REQ.  Change it into a no RST variant
 * and send it along.
 */
static void abort_arp_failure(void *handle, struct sk_buff *skb)
{
	int ret;
	struct c4iw_ep *ep = handle;
	struct c4iw_rdev *rdev = &ep->com.dev->rdev;
	struct cpl_abort_req *req = cplhdr(skb);

	PDBG("%s rdev %p\n", __func__, rdev);
	req->cmd = CPL_ABORT_NO_RST;
	ret = c4iw_ofld_send(rdev, skb);
	if (ret) {
		__state_set(&ep->com, DEAD);
		queue_arp_failure_cpl(ep, skb, FAKE_CPL_PUT_EP_SAFE);
	}
}

static int send_flowc(struct c4iw_ep *ep)
{
	struct fw_flowc_wr *flowc;
	struct sk_buff *skb = skb_dequeue(&ep->com.ep_skb_list);
	int i;
	u16 vlan = ep->l2t->vlan;
	int nparams;

	if (WARN_ON(!skb))
		return -ENOMEM;

	if (vlan == CPL_L2T_VLAN_NONE)
		nparams = 8;
	else
		nparams = 9;

	flowc = (struct fw_flowc_wr *)__skb_put(skb, FLOWC_LEN);

	flowc->op_to_nparams = cpu_to_be32(FW_WR_OP_V(FW_FLOWC_WR) |
					   FW_FLOWC_WR_NPARAMS_V(nparams));
	flowc->flowid_len16 = cpu_to_be32(FW_WR_LEN16_V(DIV_ROUND_UP(FLOWC_LEN,
					  16)) | FW_WR_FLOWID_V(ep->hwtid));

	flowc->mnemval[0].mnemonic = FW_FLOWC_MNEM_PFNVFN;
	flowc->mnemval[0].val = cpu_to_be32(FW_PFVF_CMD_PFN_V
					    (ep->com.dev->rdev.lldi.pf));
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
	flowc->mnemval[6].val = cpu_to_be32(ep->snd_win);
	flowc->mnemval[7].mnemonic = FW_FLOWC_MNEM_MSS;
	flowc->mnemval[7].val = cpu_to_be32(ep->emss);
	if (nparams == 9) {
		u16 pri;

		pri = (vlan & VLAN_PRIO_MASK) >> VLAN_PRIO_SHIFT;
		flowc->mnemval[8].mnemonic = FW_FLOWC_MNEM_SCHEDCLASS;
		flowc->mnemval[8].val = cpu_to_be32(pri);
	} else {
		/* Pad WR to 16 byte boundary */
		flowc->mnemval[8].mnemonic = 0;
		flowc->mnemval[8].val = 0;
	}
	for (i = 0; i < 9; i++) {
		flowc->mnemval[i].r4[0] = 0;
		flowc->mnemval[i].r4[1] = 0;
		flowc->mnemval[i].r4[2] = 0;
	}

	set_wr_txq(skb, CPL_PRIORITY_DATA, ep->txq_idx);
	return c4iw_ofld_send(&ep->com.dev->rdev, skb);
}

static int send_halfclose(struct c4iw_ep *ep)
{
	struct cpl_close_con_req *req;
	struct sk_buff *skb = skb_dequeue(&ep->com.ep_skb_list);
	int wrlen = roundup(sizeof *req, 16);

	PDBG("%s ep %p tid %u\n", __func__, ep, ep->hwtid);
	if (WARN_ON(!skb))
		return -ENOMEM;

	set_wr_txq(skb, CPL_PRIORITY_DATA, ep->txq_idx);
	t4_set_arp_err_handler(skb, NULL, arp_failure_discard);
	req = (struct cpl_close_con_req *) skb_put(skb, wrlen);
	memset(req, 0, wrlen);
	INIT_TP_WR(req, ep->hwtid);
	OPCODE_TID(req) = cpu_to_be32(MK_OPCODE_TID(CPL_CLOSE_CON_REQ,
						    ep->hwtid));
	return c4iw_l2t_send(&ep->com.dev->rdev, skb, ep->l2t);
}

static int send_abort(struct c4iw_ep *ep)
{
	struct cpl_abort_req *req;
	int wrlen = roundup(sizeof *req, 16);
	struct sk_buff *req_skb = skb_dequeue(&ep->com.ep_skb_list);

	PDBG("%s ep %p tid %u\n", __func__, ep, ep->hwtid);
	if (WARN_ON(!req_skb))
		return -ENOMEM;

	set_wr_txq(req_skb, CPL_PRIORITY_DATA, ep->txq_idx);
	t4_set_arp_err_handler(req_skb, ep, abort_arp_failure);
	req = (struct cpl_abort_req *)skb_put(req_skb, wrlen);
	memset(req, 0, wrlen);
	INIT_TP_WR(req, ep->hwtid);
	OPCODE_TID(req) = cpu_to_be32(MK_OPCODE_TID(CPL_ABORT_REQ, ep->hwtid));
	req->cmd = CPL_ABORT_SEND_RST;
	return c4iw_l2t_send(&ep->com.dev->rdev, req_skb, ep->l2t);
}

static void best_mtu(const unsigned short *mtus, unsigned short mtu,
		     unsigned int *idx, int use_ts, int ipv6)
{
	unsigned short hdr_size = (ipv6 ?
				   sizeof(struct ipv6hdr) :
				   sizeof(struct iphdr)) +
				  sizeof(struct tcphdr) +
				  (use_ts ?
				   round_up(TCPOLEN_TIMESTAMP, 4) : 0);
	unsigned short data_size = mtu - hdr_size;

	cxgb4_best_aligned_mtu(mtus, hdr_size, data_size, 8, idx);
}

static int send_connect(struct c4iw_ep *ep)
{
	struct cpl_act_open_req *req = NULL;
	struct cpl_t5_act_open_req *t5req = NULL;
	struct cpl_t6_act_open_req *t6req = NULL;
	struct cpl_act_open_req6 *req6 = NULL;
	struct cpl_t5_act_open_req6 *t5req6 = NULL;
	struct cpl_t6_act_open_req6 *t6req6 = NULL;
	struct sk_buff *skb;
	u64 opt0;
	u32 opt2;
	unsigned int mtu_idx;
	int wscale;
	int win, sizev4, sizev6, wrlen;
	struct sockaddr_in *la = (struct sockaddr_in *)
				 &ep->com.local_addr;
	struct sockaddr_in *ra = (struct sockaddr_in *)
				 &ep->com.remote_addr;
	struct sockaddr_in6 *la6 = (struct sockaddr_in6 *)
				   &ep->com.local_addr;
	struct sockaddr_in6 *ra6 = (struct sockaddr_in6 *)
				   &ep->com.remote_addr;
	int ret;
	enum chip_type adapter_type = ep->com.dev->rdev.lldi.adapter_type;
	u32 isn = (prandom_u32() & ~7UL) - 1;

	switch (CHELSIO_CHIP_VERSION(adapter_type)) {
	case CHELSIO_T4:
		sizev4 = sizeof(struct cpl_act_open_req);
		sizev6 = sizeof(struct cpl_act_open_req6);
		break;
	case CHELSIO_T5:
		sizev4 = sizeof(struct cpl_t5_act_open_req);
		sizev6 = sizeof(struct cpl_t5_act_open_req6);
		break;
	case CHELSIO_T6:
		sizev4 = sizeof(struct cpl_t6_act_open_req);
		sizev6 = sizeof(struct cpl_t6_act_open_req6);
		break;
	default:
		pr_err("T%d Chip is not supported\n",
		       CHELSIO_CHIP_VERSION(adapter_type));
		return -EINVAL;
	}

	wrlen = (ep->com.remote_addr.ss_family == AF_INET) ?
			roundup(sizev4, 16) :
			roundup(sizev6, 16);

	PDBG("%s ep %p atid %u\n", __func__, ep, ep->atid);

	skb = get_skb(NULL, wrlen, GFP_KERNEL);
	if (!skb) {
		printk(KERN_ERR MOD "%s - failed to alloc skb.\n",
		       __func__);
		return -ENOMEM;
	}
	set_wr_txq(skb, CPL_PRIORITY_SETUP, ep->ctrlq_idx);

	best_mtu(ep->com.dev->rdev.lldi.mtus, ep->mtu, &mtu_idx,
		 enable_tcp_timestamps,
		 (AF_INET == ep->com.remote_addr.ss_family) ? 0 : 1);
	wscale = compute_wscale(rcv_win);

	/*
	 * Specify the largest window that will fit in opt0. The
	 * remainder will be specified in the rx_data_ack.
	 */
	win = ep->rcv_win >> 10;
	if (win > RCV_BUFSIZ_M)
		win = RCV_BUFSIZ_M;

	opt0 = (nocong ? NO_CONG_F : 0) |
	       KEEP_ALIVE_F |
	       DELACK_F |
	       WND_SCALE_V(wscale) |
	       MSS_IDX_V(mtu_idx) |
	       L2T_IDX_V(ep->l2t->idx) |
	       TX_CHAN_V(ep->tx_chan) |
	       SMAC_SEL_V(ep->smac_idx) |
	       DSCP_V(ep->tos >> 2) |
	       ULP_MODE_V(ULP_MODE_TCPDDP) |
	       RCV_BUFSIZ_V(win);
	opt2 = RX_CHANNEL_V(0) |
	       CCTRL_ECN_V(enable_ecn) |
	       RSS_QUEUE_VALID_F | RSS_QUEUE_V(ep->rss_qid);
	if (enable_tcp_timestamps)
		opt2 |= TSTAMPS_EN_F;
	if (enable_tcp_sack)
		opt2 |= SACK_EN_F;
	if (wscale && enable_tcp_window_scaling)
		opt2 |= WND_SCALE_EN_F;
	if (CHELSIO_CHIP_VERSION(adapter_type) > CHELSIO_T4) {
		if (peer2peer)
			isn += 4;

		opt2 |= T5_OPT_2_VALID_F;
		opt2 |= CONG_CNTRL_V(CONG_ALG_TAHOE);
		opt2 |= T5_ISS_F;
	}

	if (ep->com.remote_addr.ss_family == AF_INET6)
		cxgb4_clip_get(ep->com.dev->rdev.lldi.ports[0],
			       (const u32 *)&la6->sin6_addr.s6_addr, 1);

	t4_set_arp_err_handler(skb, ep, act_open_req_arp_failure);

	if (ep->com.remote_addr.ss_family == AF_INET) {
		switch (CHELSIO_CHIP_VERSION(adapter_type)) {
		case CHELSIO_T4:
			req = (struct cpl_act_open_req *)skb_put(skb, wrlen);
			INIT_TP_WR(req, 0);
			break;
		case CHELSIO_T5:
			t5req = (struct cpl_t5_act_open_req *)skb_put(skb,
					wrlen);
			INIT_TP_WR(t5req, 0);
			req = (struct cpl_act_open_req *)t5req;
			break;
		case CHELSIO_T6:
			t6req = (struct cpl_t6_act_open_req *)skb_put(skb,
					wrlen);
			INIT_TP_WR(t6req, 0);
			req = (struct cpl_act_open_req *)t6req;
			t5req = (struct cpl_t5_act_open_req *)t6req;
			break;
		default:
			pr_err("T%d Chip is not supported\n",
			       CHELSIO_CHIP_VERSION(adapter_type));
			ret = -EINVAL;
			goto clip_release;
		}

		OPCODE_TID(req) = cpu_to_be32(MK_OPCODE_TID(CPL_ACT_OPEN_REQ,
					((ep->rss_qid<<14) | ep->atid)));
		req->local_port = la->sin_port;
		req->peer_port = ra->sin_port;
		req->local_ip = la->sin_addr.s_addr;
		req->peer_ip = ra->sin_addr.s_addr;
		req->opt0 = cpu_to_be64(opt0);

		if (is_t4(ep->com.dev->rdev.lldi.adapter_type)) {
			req->params = cpu_to_be32(cxgb4_select_ntuple(
						ep->com.dev->rdev.lldi.ports[0],
						ep->l2t));
			req->opt2 = cpu_to_be32(opt2);
		} else {
			t5req->params = cpu_to_be64(FILTER_TUPLE_V(
						cxgb4_select_ntuple(
						ep->com.dev->rdev.lldi.ports[0],
						ep->l2t)));
			t5req->rsvd = cpu_to_be32(isn);
			PDBG("%s snd_isn %u\n", __func__, t5req->rsvd);
			t5req->opt2 = cpu_to_be32(opt2);
		}
	} else {
		switch (CHELSIO_CHIP_VERSION(adapter_type)) {
		case CHELSIO_T4:
			req6 = (struct cpl_act_open_req6 *)skb_put(skb, wrlen);
			INIT_TP_WR(req6, 0);
			break;
		case CHELSIO_T5:
			t5req6 = (struct cpl_t5_act_open_req6 *)skb_put(skb,
					wrlen);
			INIT_TP_WR(t5req6, 0);
			req6 = (struct cpl_act_open_req6 *)t5req6;
			break;
		case CHELSIO_T6:
			t6req6 = (struct cpl_t6_act_open_req6 *)skb_put(skb,
					wrlen);
			INIT_TP_WR(t6req6, 0);
			req6 = (struct cpl_act_open_req6 *)t6req6;
			t5req6 = (struct cpl_t5_act_open_req6 *)t6req6;
			break;
		default:
			pr_err("T%d Chip is not supported\n",
			       CHELSIO_CHIP_VERSION(adapter_type));
			ret = -EINVAL;
			goto clip_release;
		}

		OPCODE_TID(req6) = cpu_to_be32(MK_OPCODE_TID(CPL_ACT_OPEN_REQ6,
					((ep->rss_qid<<14)|ep->atid)));
		req6->local_port = la6->sin6_port;
		req6->peer_port = ra6->sin6_port;
		req6->local_ip_hi = *((__be64 *)(la6->sin6_addr.s6_addr));
		req6->local_ip_lo = *((__be64 *)(la6->sin6_addr.s6_addr + 8));
		req6->peer_ip_hi = *((__be64 *)(ra6->sin6_addr.s6_addr));
		req6->peer_ip_lo = *((__be64 *)(ra6->sin6_addr.s6_addr + 8));
		req6->opt0 = cpu_to_be64(opt0);

		if (is_t4(ep->com.dev->rdev.lldi.adapter_type)) {
			req6->params = cpu_to_be32(cxgb4_select_ntuple(
						ep->com.dev->rdev.lldi.ports[0],
						ep->l2t));
			req6->opt2 = cpu_to_be32(opt2);
		} else {
			t5req6->params = cpu_to_be64(FILTER_TUPLE_V(
						cxgb4_select_ntuple(
						ep->com.dev->rdev.lldi.ports[0],
						ep->l2t)));
			t5req6->rsvd = cpu_to_be32(isn);
			PDBG("%s snd_isn %u\n", __func__, t5req6->rsvd);
			t5req6->opt2 = cpu_to_be32(opt2);
		}
	}

	set_bit(ACT_OPEN_REQ, &ep->com.history);
	ret = c4iw_l2t_send(&ep->com.dev->rdev, skb, ep->l2t);
clip_release:
	if (ret && ep->com.remote_addr.ss_family == AF_INET6)
		cxgb4_clip_release(ep->com.dev->rdev.lldi.ports[0],
				   (const u32 *)&la6->sin6_addr.s6_addr, 1);
	return ret;
}

static int send_mpa_req(struct c4iw_ep *ep, struct sk_buff *skb,
			u8 mpa_rev_to_use)
{
	int mpalen, wrlen, ret;
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
		return -ENOMEM;
	}
	set_wr_txq(skb, CPL_PRIORITY_DATA, ep->txq_idx);

	req = (struct fw_ofld_tx_data_wr *)skb_put(skb, wrlen);
	memset(req, 0, wrlen);
	req->op_to_immdlen = cpu_to_be32(
		FW_WR_OP_V(FW_OFLD_TX_DATA_WR) |
		FW_WR_COMPL_F |
		FW_WR_IMMDLEN_V(mpalen));
	req->flowid_len16 = cpu_to_be32(
		FW_WR_FLOWID_V(ep->hwtid) |
		FW_WR_LEN16_V(wrlen >> 4));
	req->plen = cpu_to_be32(mpalen);
	req->tunnel_to_proxy = cpu_to_be32(
		FW_OFLD_TX_DATA_WR_FLUSH_F |
		FW_OFLD_TX_DATA_WR_SHOVE_F);

	mpa = (struct mpa_message *)(req + 1);
	memcpy(mpa->key, MPA_KEY_REQ, sizeof(mpa->key));

	mpa->flags = 0;
	if (crc_enabled)
		mpa->flags |= MPA_CRC;
	if (markers_enabled) {
		mpa->flags |= MPA_MARKERS;
		ep->mpa_attr.recv_marker_enabled = 1;
	} else {
		ep->mpa_attr.recv_marker_enabled = 0;
	}
	if (mpa_rev_to_use == 2)
		mpa->flags |= MPA_ENHANCED_RDMA_CONN;

	mpa->private_data_size = htons(ep->plen);
	mpa->revision = mpa_rev_to_use;
	if (mpa_rev_to_use == 1) {
		ep->tried_with_mpa_v1 = 1;
		ep->retry_with_mpa_v1 = 0;
	}

	if (mpa_rev_to_use == 2) {
		mpa->private_data_size = htons(ntohs(mpa->private_data_size) +
					       sizeof (struct mpa_v2_conn_params));
		PDBG("%s initiator ird %u ord %u\n", __func__, ep->ird,
		     ep->ord);
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
	ret = c4iw_l2t_send(&ep->com.dev->rdev, skb, ep->l2t);
	if (ret)
		return ret;
	start_ep_timer(ep);
	__state_set(&ep->com, MPA_REQ_SENT);
	ep->mpa_attr.initiator = 1;
	ep->snd_seq += mpalen;
	return ret;
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
		FW_WR_OP_V(FW_OFLD_TX_DATA_WR) |
		FW_WR_COMPL_F |
		FW_WR_IMMDLEN_V(mpalen));
	req->flowid_len16 = cpu_to_be32(
		FW_WR_FLOWID_V(ep->hwtid) |
		FW_WR_LEN16_V(wrlen >> 4));
	req->plen = cpu_to_be32(mpalen);
	req->tunnel_to_proxy = cpu_to_be32(
		FW_OFLD_TX_DATA_WR_FLUSH_F |
		FW_OFLD_TX_DATA_WR_SHOVE_F);

	mpa = (struct mpa_message *)(req + 1);
	memset(mpa, 0, sizeof(*mpa));
	memcpy(mpa->key, MPA_KEY_REP, sizeof(mpa->key));
	mpa->flags = MPA_REJECT;
	mpa->revision = ep->mpa_attr.version;
	mpa->private_data_size = htons(plen);

	if (ep->mpa_attr.version == 2 && ep->mpa_attr.enhanced_rdma_conn) {
		mpa->flags |= MPA_ENHANCED_RDMA_CONN;
		mpa->private_data_size = htons(ntohs(mpa->private_data_size) +
					       sizeof (struct mpa_v2_conn_params));
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
	t4_set_arp_err_handler(skb, NULL, mpa_start_arp_failure);
	BUG_ON(ep->mpa_skb);
	ep->mpa_skb = skb;
	ep->snd_seq += mpalen;
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
		FW_WR_OP_V(FW_OFLD_TX_DATA_WR) |
		FW_WR_COMPL_F |
		FW_WR_IMMDLEN_V(mpalen));
	req->flowid_len16 = cpu_to_be32(
		FW_WR_FLOWID_V(ep->hwtid) |
		FW_WR_LEN16_V(wrlen >> 4));
	req->plen = cpu_to_be32(mpalen);
	req->tunnel_to_proxy = cpu_to_be32(
		FW_OFLD_TX_DATA_WR_FLUSH_F |
		FW_OFLD_TX_DATA_WR_SHOVE_F);

	mpa = (struct mpa_message *)(req + 1);
	memset(mpa, 0, sizeof(*mpa));
	memcpy(mpa->key, MPA_KEY_REP, sizeof(mpa->key));
	mpa->flags = 0;
	if (ep->mpa_attr.crc_enabled)
		mpa->flags |= MPA_CRC;
	if (ep->mpa_attr.recv_marker_enabled)
		mpa->flags |= MPA_MARKERS;
	mpa->revision = ep->mpa_attr.version;
	mpa->private_data_size = htons(plen);

	if (ep->mpa_attr.version == 2 && ep->mpa_attr.enhanced_rdma_conn) {
		mpa->flags |= MPA_ENHANCED_RDMA_CONN;
		mpa->private_data_size = htons(ntohs(mpa->private_data_size) +
					       sizeof (struct mpa_v2_conn_params));
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
	t4_set_arp_err_handler(skb, NULL, mpa_start_arp_failure);
	ep->mpa_skb = skb;
	__state_set(&ep->com, MPA_REP_SENT);
	ep->snd_seq += mpalen;
	return c4iw_l2t_send(&ep->com.dev->rdev, skb, ep->l2t);
}

static int act_establish(struct c4iw_dev *dev, struct sk_buff *skb)
{
	struct c4iw_ep *ep;
	struct cpl_act_establish *req = cplhdr(skb);
	unsigned int tid = GET_TID(req);
	unsigned int atid = TID_TID_G(ntohl(req->tos_atid));
	struct tid_info *t = dev->rdev.lldi.tids;
	int ret;

	ep = lookup_atid(t, atid);

	PDBG("%s ep %p tid %u snd_isn %u rcv_isn %u\n", __func__, ep, tid,
	     be32_to_cpu(req->snd_isn), be32_to_cpu(req->rcv_isn));

	mutex_lock(&ep->com.mutex);
	dst_confirm(ep->dst);

	/* setup the hwtid for this connection */
	ep->hwtid = tid;
	cxgb4_insert_tid(t, ep, tid);
	insert_ep_tid(ep);

	ep->snd_seq = be32_to_cpu(req->snd_isn);
	ep->rcv_seq = be32_to_cpu(req->rcv_isn);

	set_emss(ep, ntohs(req->tcp_opt));

	/* dealloc the atid */
	remove_handle(ep->com.dev, &ep->com.dev->atid_idr, atid);
	cxgb4_free_atid(t, atid);
	set_bit(ACT_ESTAB, &ep->com.history);

	/* start MPA negotiation */
	ret = send_flowc(ep);
	if (ret)
		goto err;
	if (ep->retry_with_mpa_v1)
		ret = send_mpa_req(ep, skb, 1);
	else
		ret = send_mpa_req(ep, skb, mpa_rev);
	if (ret)
		goto err;
	mutex_unlock(&ep->com.mutex);
	return 0;
err:
	mutex_unlock(&ep->com.mutex);
	connect_reply_upcall(ep, -ENOMEM);
	c4iw_ep_disconnect(ep, 0, GFP_KERNEL);
	return 0;
}

static void close_complete_upcall(struct c4iw_ep *ep, int status)
{
	struct iw_cm_event event;

	PDBG("%s ep %p tid %u\n", __func__, ep, ep->hwtid);
	memset(&event, 0, sizeof(event));
	event.event = IW_CM_EVENT_CLOSE;
	event.status = status;
	if (ep->com.cm_id) {
		PDBG("close complete delivered ep %p cm_id %p tid %u\n",
		     ep, ep->com.cm_id, ep->hwtid);
		ep->com.cm_id->event_handler(ep->com.cm_id, &event);
		deref_cm_id(&ep->com);
		set_bit(CLOSE_UPCALL, &ep->com.history);
	}
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
		set_bit(DISCONN_UPCALL, &ep->com.history);
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
		deref_cm_id(&ep->com);
		set_bit(ABORT_UPCALL, &ep->com.history);
	}
}

static void connect_reply_upcall(struct c4iw_ep *ep, int status)
{
	struct iw_cm_event event;

	PDBG("%s ep %p tid %u status %d\n", __func__, ep, ep->hwtid, status);
	memset(&event, 0, sizeof(event));
	event.event = IW_CM_EVENT_CONNECT_REPLY;
	event.status = status;
	memcpy(&event.local_addr, &ep->com.local_addr,
	       sizeof(ep->com.local_addr));
	memcpy(&event.remote_addr, &ep->com.remote_addr,
	       sizeof(ep->com.remote_addr));

	if ((status == 0) || (status == -ECONNREFUSED)) {
		if (!ep->tried_with_mpa_v1) {
			/* this means MPA_v2 is used */
			event.ord = ep->ird;
			event.ird = ep->ord;
			event.private_data_len = ep->plen -
				sizeof(struct mpa_v2_conn_params);
			event.private_data = ep->mpa_pkt +
				sizeof(struct mpa_message) +
				sizeof(struct mpa_v2_conn_params);
		} else {
			/* this means MPA_v1 is used */
			event.ord = cur_max_read_depth(ep->com.dev);
			event.ird = cur_max_read_depth(ep->com.dev);
			event.private_data_len = ep->plen;
			event.private_data = ep->mpa_pkt +
				sizeof(struct mpa_message);
		}
	}

	PDBG("%s ep %p tid %u status %d\n", __func__, ep,
	     ep->hwtid, status);
	set_bit(CONN_RPL_UPCALL, &ep->com.history);
	ep->com.cm_id->event_handler(ep->com.cm_id, &event);

	if (status < 0)
		deref_cm_id(&ep->com);
}

static int connect_request_upcall(struct c4iw_ep *ep)
{
	struct iw_cm_event event;
	int ret;

	PDBG("%s ep %p tid %u\n", __func__, ep, ep->hwtid);
	memset(&event, 0, sizeof(event));
	event.event = IW_CM_EVENT_CONNECT_REQUEST;
	memcpy(&event.local_addr, &ep->com.local_addr,
	       sizeof(ep->com.local_addr));
	memcpy(&event.remote_addr, &ep->com.remote_addr,
	       sizeof(ep->com.remote_addr));
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
		event.ord = cur_max_read_depth(ep->com.dev);
		event.ird = cur_max_read_depth(ep->com.dev);
		event.private_data_len = ep->plen;
		event.private_data = ep->mpa_pkt + sizeof(struct mpa_message);
	}
	c4iw_get_ep(&ep->com);
	ret = ep->parent_ep->com.cm_id->event_handler(ep->parent_ep->com.cm_id,
						      &event);
	if (ret)
		c4iw_put_ep(&ep->com);
	set_bit(CONNREQ_UPCALL, &ep->com.history);
	c4iw_put_ep(&ep->parent_ep->com);
	return ret;
}

static void established_upcall(struct c4iw_ep *ep)
{
	struct iw_cm_event event;

	PDBG("%s ep %p tid %u\n", __func__, ep, ep->hwtid);
	memset(&event, 0, sizeof(event));
	event.event = IW_CM_EVENT_ESTABLISHED;
	event.ird = ep->ord;
	event.ord = ep->ird;
	if (ep->com.cm_id) {
		PDBG("%s ep %p tid %u\n", __func__, ep, ep->hwtid);
		ep->com.cm_id->event_handler(ep->com.cm_id, &event);
		set_bit(ESTAB_UPCALL, &ep->com.history);
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

	/*
	 * If we couldn't specify the entire rcv window at connection setup
	 * due to the limit in the number of bits in the RCV_BUFSIZ field,
	 * then add the overage in to the credits returned.
	 */
	if (ep->rcv_win > RCV_BUFSIZ_M * 1024)
		credits += ep->rcv_win - RCV_BUFSIZ_M * 1024;

	req = (struct cpl_rx_data_ack *) skb_put(skb, wrlen);
	memset(req, 0, wrlen);
	INIT_TP_WR(req, ep->hwtid);
	OPCODE_TID(req) = cpu_to_be32(MK_OPCODE_TID(CPL_RX_DATA_ACK,
						    ep->hwtid));
	req->credit_dack = cpu_to_be32(credits | RX_FORCE_ACK_F |
				       RX_DACK_CHANGE_F |
				       RX_DACK_MODE_V(dack_mode));
	set_wr_txq(skb, CPL_PRIORITY_ACK, ep->ctrlq_idx);
	c4iw_ofld_send(&ep->com.dev->rdev, skb);
	return credits;
}

#define RELAXED_IRD_NEGOTIATION 1

/*
 * process_mpa_reply - process streaming mode MPA reply
 *
 * Returns:
 *
 * 0 upon success indicating a connect request was delivered to the ULP
 * or the mpa request is incomplete but valid so far.
 *
 * 1 if a failure requires the caller to close the connection.
 *
 * 2 if a failure requires the caller to abort the connection.
 */
static int process_mpa_reply(struct c4iw_ep *ep, struct sk_buff *skb)
{
	struct mpa_message *mpa;
	struct mpa_v2_conn_params *mpa_v2_params;
	u16 plen;
	u16 resp_ird, resp_ord;
	u8 rtr_mismatch = 0, insuff_ird = 0;
	struct c4iw_qp_attributes attrs;
	enum c4iw_qp_attr_mask mask;
	int err;
	int disconnect = 0;

	PDBG("%s ep %p tid %u\n", __func__, ep, ep->hwtid);

	/*
	 * If we get more than the supported amount of private data
	 * then we must fail this connection.
	 */
	if (ep->mpa_pkt_len + skb->len > sizeof(ep->mpa_pkt)) {
		err = -EINVAL;
		goto err_stop_timer;
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
		return 0;
	mpa = (struct mpa_message *) ep->mpa_pkt;

	/* Validate MPA header. */
	if (mpa->revision > mpa_rev) {
		printk(KERN_ERR MOD "%s MPA version mismatch. Local = %d,"
		       " Received = %d\n", __func__, mpa_rev, mpa->revision);
		err = -EPROTO;
		goto err_stop_timer;
	}
	if (memcmp(mpa->key, MPA_KEY_REP, sizeof(mpa->key))) {
		err = -EPROTO;
		goto err_stop_timer;
	}

	plen = ntohs(mpa->private_data_size);

	/*
	 * Fail if there's too much private data.
	 */
	if (plen > MPA_MAX_PRIVATE_DATA) {
		err = -EPROTO;
		goto err_stop_timer;
	}

	/*
	 * If plen does not account for pkt size
	 */
	if (ep->mpa_pkt_len > (sizeof(*mpa) + plen)) {
		err = -EPROTO;
		goto err_stop_timer;
	}

	ep->plen = (u8) plen;

	/*
	 * If we don't have all the pdata yet, then bail.
	 * We'll continue process when more data arrives.
	 */
	if (ep->mpa_pkt_len < (sizeof(*mpa) + plen))
		return 0;

	if (mpa->flags & MPA_REJECT) {
		err = -ECONNREFUSED;
		goto err_stop_timer;
	}

	/*
	 * Stop mpa timer.  If it expired, then
	 * we ignore the MPA reply.  process_timeout()
	 * will abort the connection.
	 */
	if (stop_ep_timer(ep))
		return 0;

	/*
	 * If we get here we have accumulated the entire mpa
	 * start reply message including private data. And
	 * the MPA header is valid.
	 */
	__state_set(&ep->com, FPDU_MODE);
	ep->mpa_attr.crc_enabled = (mpa->flags & MPA_CRC) | crc_enabled ? 1 : 0;
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
			PDBG("%s responder ird %u ord %u ep ird %u ord %u\n",
			     __func__, resp_ird, resp_ord, ep->ird, ep->ord);

			/*
			 * This is a double-check. Ideally, below checks are
			 * not required since ird/ord stuff has been taken
			 * care of in c4iw_accept_cr
			 */
			if (ep->ird < resp_ord) {
				if (RELAXED_IRD_NEGOTIATION && resp_ord <=
				    ep->com.dev->rdev.lldi.max_ordird_qp)
					ep->ird = resp_ord;
				else
					insuff_ird = 1;
			} else if (ep->ird > resp_ord) {
				ep->ird = resp_ord;
			}
			if (ep->ord > resp_ird) {
				if (RELAXED_IRD_NEGOTIATION)
					ep->ord = resp_ird;
				else
					insuff_ird = 1;
			}
			if (insuff_ird) {
				err = -ENOMEM;
				ep->ird = resp_ord;
				ep->ord = resp_ird;
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
		attrs.send_term = 1;
		err = c4iw_modify_qp(ep->com.qp->rhp, ep->com.qp,
				C4IW_QP_ATTR_NEXT_STATE, &attrs, 1);
		err = -ENOMEM;
		disconnect = 1;
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
		attrs.send_term = 1;
		err = c4iw_modify_qp(ep->com.qp->rhp, ep->com.qp,
				C4IW_QP_ATTR_NEXT_STATE, &attrs, 1);
		err = -ENOMEM;
		disconnect = 1;
		goto out;
	}
	goto out;
err_stop_timer:
	stop_ep_timer(ep);
err:
	disconnect = 2;
out:
	connect_reply_upcall(ep, err);
	return disconnect;
}

/*
 * process_mpa_request - process streaming mode MPA request
 *
 * Returns:
 *
 * 0 upon success indicating a connect request was delivered to the ULP
 * or the mpa request is incomplete but valid so far.
 *
 * 1 if a failure requires the caller to close the connection.
 *
 * 2 if a failure requires the caller to abort the connection.
 */
static int process_mpa_request(struct c4iw_ep *ep, struct sk_buff *skb)
{
	struct mpa_message *mpa;
	struct mpa_v2_conn_params *mpa_v2_params;
	u16 plen;

	PDBG("%s ep %p tid %u\n", __func__, ep, ep->hwtid);

	/*
	 * If we get more than the supported amount of private data
	 * then we must fail this connection.
	 */
	if (ep->mpa_pkt_len + skb->len > sizeof(ep->mpa_pkt))
		goto err_stop_timer;

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
		return 0;

	PDBG("%s enter (%s line %u)\n", __func__, __FILE__, __LINE__);
	mpa = (struct mpa_message *) ep->mpa_pkt;

	/*
	 * Validate MPA Header.
	 */
	if (mpa->revision > mpa_rev) {
		printk(KERN_ERR MOD "%s MPA version mismatch. Local = %d,"
		       " Received = %d\n", __func__, mpa_rev, mpa->revision);
		goto err_stop_timer;
	}

	if (memcmp(mpa->key, MPA_KEY_REQ, sizeof(mpa->key)))
		goto err_stop_timer;

	plen = ntohs(mpa->private_data_size);

	/*
	 * Fail if there's too much private data.
	 */
	if (plen > MPA_MAX_PRIVATE_DATA)
		goto err_stop_timer;

	/*
	 * If plen does not account for pkt size
	 */
	if (ep->mpa_pkt_len > (sizeof(*mpa) + plen))
		goto err_stop_timer;
	ep->plen = (u8) plen;

	/*
	 * If we don't have all the pdata yet, then bail.
	 */
	if (ep->mpa_pkt_len < (sizeof(*mpa) + plen))
		return 0;

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
			ep->ird = min_t(u32, ep->ird,
					cur_max_read_depth(ep->com.dev));
			ep->ord = ntohs(mpa_v2_params->ord) &
				MPA_V2_IRD_ORD_MASK;
			ep->ord = min_t(u32, ep->ord,
					cur_max_read_depth(ep->com.dev));
			PDBG("%s initiator ird %u ord %u\n", __func__, ep->ird,
			     ep->ord);
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

	__state_set(&ep->com, MPA_REQ_RCVD);

	/* drive upcall */
	mutex_lock_nested(&ep->parent_ep->com.mutex, SINGLE_DEPTH_NESTING);
	if (ep->parent_ep->com.state != DEAD) {
		if (connect_request_upcall(ep))
			goto err_unlock_parent;
	} else {
		goto err_unlock_parent;
	}
	mutex_unlock(&ep->parent_ep->com.mutex);
	return 0;

err_unlock_parent:
	mutex_unlock(&ep->parent_ep->com.mutex);
	goto err_out;
err_stop_timer:
	(void)stop_ep_timer(ep);
err_out:
	return 2;
}

static int rx_data(struct c4iw_dev *dev, struct sk_buff *skb)
{
	struct c4iw_ep *ep;
	struct cpl_rx_data *hdr = cplhdr(skb);
	unsigned int dlen = ntohs(hdr->len);
	unsigned int tid = GET_TID(hdr);
	__u8 status = hdr->status;
	int disconnect = 0;

	ep = get_ep_from_tid(dev, tid);
	if (!ep)
		return 0;
	PDBG("%s ep %p tid %u dlen %u\n", __func__, ep, ep->hwtid, dlen);
	skb_pull(skb, sizeof(*hdr));
	skb_trim(skb, dlen);
	mutex_lock(&ep->com.mutex);

	/* update RX credits */
	update_rx_credits(ep, dlen);

	switch (ep->com.state) {
	case MPA_REQ_SENT:
		ep->rcv_seq += dlen;
		disconnect = process_mpa_reply(ep, skb);
		break;
	case MPA_REQ_WAIT:
		ep->rcv_seq += dlen;
		disconnect = process_mpa_request(ep, skb);
		break;
	case FPDU_MODE: {
		struct c4iw_qp_attributes attrs;
		BUG_ON(!ep->com.qp);
		if (status)
			pr_err("%s Unexpected streaming data." \
			       " qpid %u ep %p state %d tid %u status %d\n",
			       __func__, ep->com.qp->wq.sq.qid, ep,
			       ep->com.state, ep->hwtid, status);
		attrs.next_state = C4IW_QP_STATE_TERMINATE;
		c4iw_modify_qp(ep->com.qp->rhp, ep->com.qp,
			       C4IW_QP_ATTR_NEXT_STATE, &attrs, 1);
		disconnect = 1;
		break;
	}
	default:
		break;
	}
	mutex_unlock(&ep->com.mutex);
	if (disconnect)
		c4iw_ep_disconnect(ep, disconnect == 2, GFP_KERNEL);
	c4iw_put_ep(&ep->com);
	return 0;
}

static int abort_rpl(struct c4iw_dev *dev, struct sk_buff *skb)
{
	struct c4iw_ep *ep;
	struct cpl_abort_rpl_rss *rpl = cplhdr(skb);
	int release = 0;
	unsigned int tid = GET_TID(rpl);

	ep = get_ep_from_tid(dev, tid);
	if (!ep) {
		printk(KERN_WARNING MOD "Abort rpl to freed endpoint\n");
		return 0;
	}
	PDBG("%s ep %p tid %u\n", __func__, ep, ep->hwtid);
	mutex_lock(&ep->com.mutex);
	switch (ep->com.state) {
	case ABORTING:
		c4iw_wake_up(&ep->com.wr_wait, -ECONNRESET);
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
	c4iw_put_ep(&ep->com);
	return 0;
}

static int send_fw_act_open_req(struct c4iw_ep *ep, unsigned int atid)
{
	struct sk_buff *skb;
	struct fw_ofld_connection_wr *req;
	unsigned int mtu_idx;
	int wscale;
	struct sockaddr_in *sin;
	int win;

	skb = get_skb(NULL, sizeof(*req), GFP_KERNEL);
	req = (struct fw_ofld_connection_wr *)__skb_put(skb, sizeof(*req));
	memset(req, 0, sizeof(*req));
	req->op_compl = htonl(WR_OP_V(FW_OFLD_CONNECTION_WR));
	req->len16_pkd = htonl(FW_WR_LEN16_V(DIV_ROUND_UP(sizeof(*req), 16)));
	req->le.filter = cpu_to_be32(cxgb4_select_ntuple(
				     ep->com.dev->rdev.lldi.ports[0],
				     ep->l2t));
	sin = (struct sockaddr_in *)&ep->com.local_addr;
	req->le.lport = sin->sin_port;
	req->le.u.ipv4.lip = sin->sin_addr.s_addr;
	sin = (struct sockaddr_in *)&ep->com.remote_addr;
	req->le.pport = sin->sin_port;
	req->le.u.ipv4.pip = sin->sin_addr.s_addr;
	req->tcb.t_state_to_astid =
			htonl(FW_OFLD_CONNECTION_WR_T_STATE_V(TCP_SYN_SENT) |
			FW_OFLD_CONNECTION_WR_ASTID_V(atid));
	req->tcb.cplrxdataack_cplpassacceptrpl =
			htons(FW_OFLD_CONNECTION_WR_CPLRXDATAACK_F);
	req->tcb.tx_max = (__force __be32) jiffies;
	req->tcb.rcv_adv = htons(1);
	best_mtu(ep->com.dev->rdev.lldi.mtus, ep->mtu, &mtu_idx,
		 enable_tcp_timestamps,
		 (AF_INET == ep->com.remote_addr.ss_family) ? 0 : 1);
	wscale = compute_wscale(rcv_win);

	/*
	 * Specify the largest window that will fit in opt0. The
	 * remainder will be specified in the rx_data_ack.
	 */
	win = ep->rcv_win >> 10;
	if (win > RCV_BUFSIZ_M)
		win = RCV_BUFSIZ_M;

	req->tcb.opt0 = (__force __be64) (TCAM_BYPASS_F |
		(nocong ? NO_CONG_F : 0) |
		KEEP_ALIVE_F |
		DELACK_F |
		WND_SCALE_V(wscale) |
		MSS_IDX_V(mtu_idx) |
		L2T_IDX_V(ep->l2t->idx) |
		TX_CHAN_V(ep->tx_chan) |
		SMAC_SEL_V(ep->smac_idx) |
		DSCP_V(ep->tos >> 2) |
		ULP_MODE_V(ULP_MODE_TCPDDP) |
		RCV_BUFSIZ_V(win));
	req->tcb.opt2 = (__force __be32) (PACE_V(1) |
		TX_QUEUE_V(ep->com.dev->rdev.lldi.tx_modq[ep->tx_chan]) |
		RX_CHANNEL_V(0) |
		CCTRL_ECN_V(enable_ecn) |
		RSS_QUEUE_VALID_F | RSS_QUEUE_V(ep->rss_qid));
	if (enable_tcp_timestamps)
		req->tcb.opt2 |= (__force __be32)TSTAMPS_EN_F;
	if (enable_tcp_sack)
		req->tcb.opt2 |= (__force __be32)SACK_EN_F;
	if (wscale && enable_tcp_window_scaling)
		req->tcb.opt2 |= (__force __be32)WND_SCALE_EN_F;
	req->tcb.opt0 = cpu_to_be64((__force u64)req->tcb.opt0);
	req->tcb.opt2 = cpu_to_be32((__force u32)req->tcb.opt2);
	set_wr_txq(skb, CPL_PRIORITY_CONTROL, ep->ctrlq_idx);
	set_bit(ACT_OFLD_CONN, &ep->com.history);
	return c4iw_l2t_send(&ep->com.dev->rdev, skb, ep->l2t);
}

/*
 * Some of the error codes above implicitly indicate that there is no TID
 * allocated with the result of an ACT_OPEN.  We use this predicate to make
 * that explicit.
 */
static inline int act_open_has_tid(int status)
{
	return (status != CPL_ERR_TCAM_PARITY &&
		status != CPL_ERR_TCAM_MISS &&
		status != CPL_ERR_TCAM_FULL &&
		status != CPL_ERR_CONN_EXIST_SYNRECV &&
		status != CPL_ERR_CONN_EXIST);
}

/* Returns whether a CPL status conveys negative advice.
 */
static int is_neg_adv(unsigned int status)
{
	return status == CPL_ERR_RTX_NEG_ADVICE ||
	       status == CPL_ERR_PERSIST_NEG_ADVICE ||
	       status == CPL_ERR_KEEPALV_NEG_ADVICE;
}

static char *neg_adv_str(unsigned int status)
{
	switch (status) {
	case CPL_ERR_RTX_NEG_ADVICE:
		return "Retransmit timeout";
	case CPL_ERR_PERSIST_NEG_ADVICE:
		return "Persist timeout";
	case CPL_ERR_KEEPALV_NEG_ADVICE:
		return "Keepalive timeout";
	default:
		return "Unknown";
	}
}

static void set_tcp_window(struct c4iw_ep *ep, struct port_info *pi)
{
	ep->snd_win = snd_win;
	ep->rcv_win = rcv_win;
	PDBG("%s snd_win %d rcv_win %d\n", __func__, ep->snd_win, ep->rcv_win);
}

#define ACT_OPEN_RETRY_COUNT 2

static int import_ep(struct c4iw_ep *ep, int iptype, __u8 *peer_ip,
		     struct dst_entry *dst, struct c4iw_dev *cdev,
		     bool clear_mpa_v1, enum chip_type adapter_type, u8 tos)
{
	struct neighbour *n;
	int err, step;
	struct net_device *pdev;

	n = dst_neigh_lookup(dst, peer_ip);
	if (!n)
		return -ENODEV;

	rcu_read_lock();
	err = -ENOMEM;
	if (n->dev->flags & IFF_LOOPBACK) {
		if (iptype == 4)
			pdev = ip_dev_find(&init_net, *(__be32 *)peer_ip);
		else if (IS_ENABLED(CONFIG_IPV6))
			for_each_netdev(&init_net, pdev) {
				if (ipv6_chk_addr(&init_net,
						  (struct in6_addr *)peer_ip,
						  pdev, 1))
					break;
			}
		else
			pdev = NULL;

		if (!pdev) {
			err = -ENODEV;
			goto out;
		}
		ep->l2t = cxgb4_l2t_get(cdev->rdev.lldi.l2t,
					n, pdev, rt_tos2priority(tos));
		if (!ep->l2t)
			goto out;
		ep->mtu = pdev->mtu;
		ep->tx_chan = cxgb4_port_chan(pdev);
		ep->smac_idx = cxgb4_tp_smt_idx(adapter_type,
						cxgb4_port_viid(pdev));
		step = cdev->rdev.lldi.ntxq /
			cdev->rdev.lldi.nchan;
		ep->txq_idx = cxgb4_port_idx(pdev) * step;
		step = cdev->rdev.lldi.nrxq /
			cdev->rdev.lldi.nchan;
		ep->ctrlq_idx = cxgb4_port_idx(pdev);
		ep->rss_qid = cdev->rdev.lldi.rxq_ids[
			cxgb4_port_idx(pdev) * step];
		set_tcp_window(ep, (struct port_info *)netdev_priv(pdev));
		dev_put(pdev);
	} else {
		pdev = get_real_dev(n->dev);
		ep->l2t = cxgb4_l2t_get(cdev->rdev.lldi.l2t,
					n, pdev, 0);
		if (!ep->l2t)
			goto out;
		ep->mtu = dst_mtu(dst);
		ep->tx_chan = cxgb4_port_chan(pdev);
		ep->smac_idx = cxgb4_tp_smt_idx(adapter_type,
						cxgb4_port_viid(pdev));
		step = cdev->rdev.lldi.ntxq /
			cdev->rdev.lldi.nchan;
		ep->txq_idx = cxgb4_port_idx(pdev) * step;
		ep->ctrlq_idx = cxgb4_port_idx(pdev);
		step = cdev->rdev.lldi.nrxq /
			cdev->rdev.lldi.nchan;
		ep->rss_qid = cdev->rdev.lldi.rxq_ids[
			cxgb4_port_idx(pdev) * step];
		set_tcp_window(ep, (struct port_info *)netdev_priv(pdev));

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

static int c4iw_reconnect(struct c4iw_ep *ep)
{
	int err = 0;
	int size = 0;
	struct sockaddr_in *laddr = (struct sockaddr_in *)
				    &ep->com.cm_id->m_local_addr;
	struct sockaddr_in *raddr = (struct sockaddr_in *)
				    &ep->com.cm_id->m_remote_addr;
	struct sockaddr_in6 *laddr6 = (struct sockaddr_in6 *)
				      &ep->com.cm_id->m_local_addr;
	struct sockaddr_in6 *raddr6 = (struct sockaddr_in6 *)
				      &ep->com.cm_id->m_remote_addr;
	int iptype;
	__u8 *ra;

	PDBG("%s qp %p cm_id %p\n", __func__, ep->com.qp, ep->com.cm_id);
	init_timer(&ep->timer);
	c4iw_init_wr_wait(&ep->com.wr_wait);

	/* When MPA revision is different on nodes, the node with MPA_rev=2
	 * tries to reconnect with MPA_rev 1 for the same EP through
	 * c4iw_reconnect(), where the same EP is assigned with new tid for
	 * further connection establishment. As we are using the same EP pointer
	 * for reconnect, few skbs are used during the previous c4iw_connect(),
	 * which leaves the EP with inadequate skbs for further
	 * c4iw_reconnect(), Further causing an assert BUG_ON() due to empty
	 * skb_list() during peer_abort(). Allocate skbs which is already used.
	 */
	size = (CN_MAX_CON_BUF - skb_queue_len(&ep->com.ep_skb_list));
	if (alloc_ep_skb_list(&ep->com.ep_skb_list, size)) {
		err = -ENOMEM;
		goto fail1;
	}

	/*
	 * Allocate an active TID to initiate a TCP connection.
	 */
	ep->atid = cxgb4_alloc_atid(ep->com.dev->rdev.lldi.tids, ep);
	if (ep->atid == -1) {
		pr_err("%s - cannot alloc atid.\n", __func__);
		err = -ENOMEM;
		goto fail2;
	}
	insert_handle(ep->com.dev, &ep->com.dev->atid_idr, ep, ep->atid);

	/* find a route */
	if (ep->com.cm_id->m_local_addr.ss_family == AF_INET) {
		ep->dst = find_route(ep->com.dev, laddr->sin_addr.s_addr,
				     raddr->sin_addr.s_addr, laddr->sin_port,
				     raddr->sin_port, ep->com.cm_id->tos);
		iptype = 4;
		ra = (__u8 *)&raddr->sin_addr;
	} else {
		ep->dst = find_route6(ep->com.dev, laddr6->sin6_addr.s6_addr,
				      raddr6->sin6_addr.s6_addr,
				      laddr6->sin6_port, raddr6->sin6_port, 0,
				      raddr6->sin6_scope_id);
		iptype = 6;
		ra = (__u8 *)&raddr6->sin6_addr;
	}
	if (!ep->dst) {
		pr_err("%s - cannot find route.\n", __func__);
		err = -EHOSTUNREACH;
		goto fail3;
	}
	err = import_ep(ep, iptype, ra, ep->dst, ep->com.dev, false,
			ep->com.dev->rdev.lldi.adapter_type,
			ep->com.cm_id->tos);
	if (err) {
		pr_err("%s - cannot alloc l2e.\n", __func__);
		goto fail4;
	}

	PDBG("%s txq_idx %u tx_chan %u smac_idx %u rss_qid %u l2t_idx %u\n",
	     __func__, ep->txq_idx, ep->tx_chan, ep->smac_idx, ep->rss_qid,
	     ep->l2t->idx);

	state_set(&ep->com, CONNECTING);
	ep->tos = ep->com.cm_id->tos;

	/* send connect request to rnic */
	err = send_connect(ep);
	if (!err)
		goto out;

	cxgb4_l2t_release(ep->l2t);
fail4:
	dst_release(ep->dst);
fail3:
	remove_handle(ep->com.dev, &ep->com.dev->atid_idr, ep->atid);
	cxgb4_free_atid(ep->com.dev->rdev.lldi.tids, ep->atid);
fail2:
	/*
	 * remember to send notification to upper layer.
	 * We are in here so the upper layer is not aware that this is
	 * re-connect attempt and so, upper layer is still waiting for
	 * response of 1st connect request.
	 */
	connect_reply_upcall(ep, -ECONNRESET);
fail1:
	c4iw_put_ep(&ep->com);
out:
	return err;
}

static int act_open_rpl(struct c4iw_dev *dev, struct sk_buff *skb)
{
	struct c4iw_ep *ep;
	struct cpl_act_open_rpl *rpl = cplhdr(skb);
	unsigned int atid = TID_TID_G(AOPEN_ATID_G(
				      ntohl(rpl->atid_status)));
	struct tid_info *t = dev->rdev.lldi.tids;
	int status = AOPEN_STATUS_G(ntohl(rpl->atid_status));
	struct sockaddr_in *la;
	struct sockaddr_in *ra;
	struct sockaddr_in6 *la6;
	struct sockaddr_in6 *ra6;
	int ret = 0;

	ep = lookup_atid(t, atid);
	la = (struct sockaddr_in *)&ep->com.local_addr;
	ra = (struct sockaddr_in *)&ep->com.remote_addr;
	la6 = (struct sockaddr_in6 *)&ep->com.local_addr;
	ra6 = (struct sockaddr_in6 *)&ep->com.remote_addr;

	PDBG("%s ep %p atid %u status %u errno %d\n", __func__, ep, atid,
	     status, status2errno(status));

	if (is_neg_adv(status)) {
		PDBG("%s Connection problems for atid %u status %u (%s)\n",
		     __func__, atid, status, neg_adv_str(status));
		ep->stats.connect_neg_adv++;
		mutex_lock(&dev->rdev.stats.lock);
		dev->rdev.stats.neg_adv++;
		mutex_unlock(&dev->rdev.stats.lock);
		return 0;
	}

	set_bit(ACT_OPEN_RPL, &ep->com.history);

	/*
	 * Log interesting failures.
	 */
	switch (status) {
	case CPL_ERR_CONN_RESET:
	case CPL_ERR_CONN_TIMEDOUT:
		break;
	case CPL_ERR_TCAM_FULL:
		mutex_lock(&dev->rdev.stats.lock);
		dev->rdev.stats.tcam_full++;
		mutex_unlock(&dev->rdev.stats.lock);
		if (ep->com.local_addr.ss_family == AF_INET &&
		    dev->rdev.lldi.enable_fw_ofld_conn) {
			ret = send_fw_act_open_req(ep, TID_TID_G(AOPEN_ATID_G(
						   ntohl(rpl->atid_status))));
			if (ret)
				goto fail;
			return 0;
		}
		break;
	case CPL_ERR_CONN_EXIST:
		if (ep->retry_count++ < ACT_OPEN_RETRY_COUNT) {
			set_bit(ACT_RETRY_INUSE, &ep->com.history);
			if (ep->com.remote_addr.ss_family == AF_INET6) {
				struct sockaddr_in6 *sin6 =
						(struct sockaddr_in6 *)
						&ep->com.local_addr;
				cxgb4_clip_release(
						ep->com.dev->rdev.lldi.ports[0],
						(const u32 *)
						&sin6->sin6_addr.s6_addr, 1);
			}
			remove_handle(ep->com.dev, &ep->com.dev->atid_idr,
					atid);
			cxgb4_free_atid(t, atid);
			dst_release(ep->dst);
			cxgb4_l2t_release(ep->l2t);
			c4iw_reconnect(ep);
			return 0;
		}
		break;
	default:
		if (ep->com.local_addr.ss_family == AF_INET) {
			pr_info("Active open failure - atid %u status %u errno %d %pI4:%u->%pI4:%u\n",
				atid, status, status2errno(status),
				&la->sin_addr.s_addr, ntohs(la->sin_port),
				&ra->sin_addr.s_addr, ntohs(ra->sin_port));
		} else {
			pr_info("Active open failure - atid %u status %u errno %d %pI6:%u->%pI6:%u\n",
				atid, status, status2errno(status),
				la6->sin6_addr.s6_addr, ntohs(la6->sin6_port),
				ra6->sin6_addr.s6_addr, ntohs(ra6->sin6_port));
		}
		break;
	}

fail:
	connect_reply_upcall(ep, status2errno(status));
	state_set(&ep->com, DEAD);

	if (ep->com.remote_addr.ss_family == AF_INET6) {
		struct sockaddr_in6 *sin6 =
			(struct sockaddr_in6 *)&ep->com.local_addr;
		cxgb4_clip_release(ep->com.dev->rdev.lldi.ports[0],
				   (const u32 *)&sin6->sin6_addr.s6_addr, 1);
	}
	if (status && act_open_has_tid(status))
		cxgb4_remove_tid(ep->com.dev->rdev.lldi.tids, 0, GET_TID(rpl));

	remove_handle(ep->com.dev, &ep->com.dev->atid_idr, atid);
	cxgb4_free_atid(t, atid);
	dst_release(ep->dst);
	cxgb4_l2t_release(ep->l2t);
	c4iw_put_ep(&ep->com);

	return 0;
}

static int pass_open_rpl(struct c4iw_dev *dev, struct sk_buff *skb)
{
	struct cpl_pass_open_rpl *rpl = cplhdr(skb);
	unsigned int stid = GET_TID(rpl);
	struct c4iw_listen_ep *ep = get_ep_from_stid(dev, stid);

	if (!ep) {
		PDBG("%s stid %d lookup failure!\n", __func__, stid);
		goto out;
	}
	PDBG("%s ep %p status %d error %d\n", __func__, ep,
	     rpl->status, status2errno(rpl->status));
	c4iw_wake_up(&ep->com.wr_wait, status2errno(rpl->status));
	c4iw_put_ep(&ep->com);
out:
	return 0;
}

static int close_listsrv_rpl(struct c4iw_dev *dev, struct sk_buff *skb)
{
	struct cpl_close_listsvr_rpl *rpl = cplhdr(skb);
	unsigned int stid = GET_TID(rpl);
	struct c4iw_listen_ep *ep = get_ep_from_stid(dev, stid);

	PDBG("%s ep %p\n", __func__, ep);
	c4iw_wake_up(&ep->com.wr_wait, status2errno(rpl->status));
	c4iw_put_ep(&ep->com);
	return 0;
}

static int accept_cr(struct c4iw_ep *ep, struct sk_buff *skb,
		     struct cpl_pass_accept_req *req)
{
	struct cpl_pass_accept_rpl *rpl;
	unsigned int mtu_idx;
	u64 opt0;
	u32 opt2;
	int wscale;
	struct cpl_t5_pass_accept_rpl *rpl5 = NULL;
	int win;
	enum chip_type adapter_type = ep->com.dev->rdev.lldi.adapter_type;

	PDBG("%s ep %p tid %u\n", __func__, ep, ep->hwtid);
	BUG_ON(skb_cloned(skb));

	skb_get(skb);
	rpl = cplhdr(skb);
	if (!is_t4(adapter_type)) {
		skb_trim(skb, roundup(sizeof(*rpl5), 16));
		rpl5 = (void *)rpl;
		INIT_TP_WR(rpl5, ep->hwtid);
	} else {
		skb_trim(skb, sizeof(*rpl));
		INIT_TP_WR(rpl, ep->hwtid);
	}
	OPCODE_TID(rpl) = cpu_to_be32(MK_OPCODE_TID(CPL_PASS_ACCEPT_RPL,
						    ep->hwtid));

	best_mtu(ep->com.dev->rdev.lldi.mtus, ep->mtu, &mtu_idx,
		 enable_tcp_timestamps && req->tcpopt.tstamp,
		 (AF_INET == ep->com.remote_addr.ss_family) ? 0 : 1);
	wscale = compute_wscale(rcv_win);

	/*
	 * Specify the largest window that will fit in opt0. The
	 * remainder will be specified in the rx_data_ack.
	 */
	win = ep->rcv_win >> 10;
	if (win > RCV_BUFSIZ_M)
		win = RCV_BUFSIZ_M;
	opt0 = (nocong ? NO_CONG_F : 0) |
	       KEEP_ALIVE_F |
	       DELACK_F |
	       WND_SCALE_V(wscale) |
	       MSS_IDX_V(mtu_idx) |
	       L2T_IDX_V(ep->l2t->idx) |
	       TX_CHAN_V(ep->tx_chan) |
	       SMAC_SEL_V(ep->smac_idx) |
	       DSCP_V(ep->tos >> 2) |
	       ULP_MODE_V(ULP_MODE_TCPDDP) |
	       RCV_BUFSIZ_V(win);
	opt2 = RX_CHANNEL_V(0) |
	       RSS_QUEUE_VALID_F | RSS_QUEUE_V(ep->rss_qid);

	if (enable_tcp_timestamps && req->tcpopt.tstamp)
		opt2 |= TSTAMPS_EN_F;
	if (enable_tcp_sack && req->tcpopt.sack)
		opt2 |= SACK_EN_F;
	if (wscale && enable_tcp_window_scaling)
		opt2 |= WND_SCALE_EN_F;
	if (enable_ecn) {
		const struct tcphdr *tcph;
		u32 hlen = ntohl(req->hdr_len);

		if (CHELSIO_CHIP_VERSION(adapter_type) <= CHELSIO_T5)
			tcph = (const void *)(req + 1) + ETH_HDR_LEN_G(hlen) +
				IP_HDR_LEN_G(hlen);
		else
			tcph = (const void *)(req + 1) +
				T6_ETH_HDR_LEN_G(hlen) + T6_IP_HDR_LEN_G(hlen);
		if (tcph->ece && tcph->cwr)
			opt2 |= CCTRL_ECN_V(1);
	}
	if (CHELSIO_CHIP_VERSION(adapter_type) > CHELSIO_T4) {
		u32 isn = (prandom_u32() & ~7UL) - 1;
		opt2 |= T5_OPT_2_VALID_F;
		opt2 |= CONG_CNTRL_V(CONG_ALG_TAHOE);
		opt2 |= T5_ISS_F;
		rpl5 = (void *)rpl;
		memset(&rpl5->iss, 0, roundup(sizeof(*rpl5)-sizeof(*rpl), 16));
		if (peer2peer)
			isn += 4;
		rpl5->iss = cpu_to_be32(isn);
		PDBG("%s iss %u\n", __func__, be32_to_cpu(rpl5->iss));
	}

	rpl->opt0 = cpu_to_be64(opt0);
	rpl->opt2 = cpu_to_be32(opt2);
	set_wr_txq(skb, CPL_PRIORITY_SETUP, ep->ctrlq_idx);
	t4_set_arp_err_handler(skb, ep, pass_accept_rpl_arp_failure);

	return c4iw_l2t_send(&ep->com.dev->rdev, skb, ep->l2t);
}

static void reject_cr(struct c4iw_dev *dev, u32 hwtid, struct sk_buff *skb)
{
	PDBG("%s c4iw_dev %p tid %u\n", __func__, dev, hwtid);
	BUG_ON(skb_cloned(skb));
	skb_trim(skb, sizeof(struct cpl_tid_release));
	release_tid(&dev->rdev, hwtid, skb);
	return;
}

static void get_4tuple(struct cpl_pass_accept_req *req, enum chip_type type,
		       int *iptype, __u8 *local_ip, __u8 *peer_ip,
		       __be16 *local_port, __be16 *peer_port)
{
	int eth_len = (CHELSIO_CHIP_VERSION(type) <= CHELSIO_T5) ?
		      ETH_HDR_LEN_G(be32_to_cpu(req->hdr_len)) :
		      T6_ETH_HDR_LEN_G(be32_to_cpu(req->hdr_len));
	int ip_len = (CHELSIO_CHIP_VERSION(type) <= CHELSIO_T5) ?
		     IP_HDR_LEN_G(be32_to_cpu(req->hdr_len)) :
		     T6_IP_HDR_LEN_G(be32_to_cpu(req->hdr_len));
	struct iphdr *ip = (struct iphdr *)((u8 *)(req + 1) + eth_len);
	struct ipv6hdr *ip6 = (struct ipv6hdr *)((u8 *)(req + 1) + eth_len);
	struct tcphdr *tcp = (struct tcphdr *)
			     ((u8 *)(req + 1) + eth_len + ip_len);

	if (ip->version == 4) {
		PDBG("%s saddr 0x%x daddr 0x%x sport %u dport %u\n", __func__,
		     ntohl(ip->saddr), ntohl(ip->daddr), ntohs(tcp->source),
		     ntohs(tcp->dest));
		*iptype = 4;
		memcpy(peer_ip, &ip->saddr, 4);
		memcpy(local_ip, &ip->daddr, 4);
	} else {
		PDBG("%s saddr %pI6 daddr %pI6 sport %u dport %u\n", __func__,
		     ip6->saddr.s6_addr, ip6->daddr.s6_addr, ntohs(tcp->source),
		     ntohs(tcp->dest));
		*iptype = 6;
		memcpy(peer_ip, ip6->saddr.s6_addr, 16);
		memcpy(local_ip, ip6->daddr.s6_addr, 16);
	}
	*peer_port = tcp->source;
	*local_port = tcp->dest;

	return;
}

static int pass_accept_req(struct c4iw_dev *dev, struct sk_buff *skb)
{
	struct c4iw_ep *child_ep = NULL, *parent_ep;
	struct cpl_pass_accept_req *req = cplhdr(skb);
	unsigned int stid = PASS_OPEN_TID_G(ntohl(req->tos_stid));
	struct tid_info *t = dev->rdev.lldi.tids;
	unsigned int hwtid = GET_TID(req);
	struct dst_entry *dst;
	__u8 local_ip[16], peer_ip[16];
	__be16 local_port, peer_port;
	struct sockaddr_in6 *sin6;
	int err;
	u16 peer_mss = ntohs(req->tcpopt.mss);
	int iptype;
	unsigned short hdrs;
	u8 tos = PASS_OPEN_TOS_G(ntohl(req->tos_stid));

	parent_ep = (struct c4iw_ep *)get_ep_from_stid(dev, stid);
	if (!parent_ep) {
		PDBG("%s connect request on invalid stid %d\n", __func__, stid);
		goto reject;
	}

	if (state_read(&parent_ep->com) != LISTEN) {
		PDBG("%s - listening ep not in LISTEN\n", __func__);
		goto reject;
	}

	get_4tuple(req, parent_ep->com.dev->rdev.lldi.adapter_type, &iptype,
		   local_ip, peer_ip, &local_port, &peer_port);

	/* Find output route */
	if (iptype == 4)  {
		PDBG("%s parent ep %p hwtid %u laddr %pI4 raddr %pI4 lport %d rport %d peer_mss %d\n"
		     , __func__, parent_ep, hwtid,
		     local_ip, peer_ip, ntohs(local_port),
		     ntohs(peer_port), peer_mss);
		dst = find_route(dev, *(__be32 *)local_ip, *(__be32 *)peer_ip,
				 local_port, peer_port,
				 tos);
	} else {
		PDBG("%s parent ep %p hwtid %u laddr %pI6 raddr %pI6 lport %d rport %d peer_mss %d\n"
		     , __func__, parent_ep, hwtid,
		     local_ip, peer_ip, ntohs(local_port),
		     ntohs(peer_port), peer_mss);
		dst = find_route6(dev, local_ip, peer_ip, local_port, peer_port,
				  PASS_OPEN_TOS_G(ntohl(req->tos_stid)),
				  ((struct sockaddr_in6 *)
				  &parent_ep->com.local_addr)->sin6_scope_id);
	}
	if (!dst) {
		printk(KERN_ERR MOD "%s - failed to find dst entry!\n",
		       __func__);
		goto reject;
	}

	child_ep = alloc_ep(sizeof(*child_ep), GFP_KERNEL);
	if (!child_ep) {
		printk(KERN_ERR MOD "%s - failed to allocate ep entry!\n",
		       __func__);
		dst_release(dst);
		goto reject;
	}

	err = import_ep(child_ep, iptype, peer_ip, dst, dev, false,
			parent_ep->com.dev->rdev.lldi.adapter_type, tos);
	if (err) {
		printk(KERN_ERR MOD "%s - failed to allocate l2t entry!\n",
		       __func__);
		dst_release(dst);
		kfree(child_ep);
		goto reject;
	}

	hdrs = sizeof(struct iphdr) + sizeof(struct tcphdr) +
	       ((enable_tcp_timestamps && req->tcpopt.tstamp) ? 12 : 0);
	if (peer_mss && child_ep->mtu > (peer_mss + hdrs))
		child_ep->mtu = peer_mss + hdrs;

	skb_queue_head_init(&child_ep->com.ep_skb_list);
	if (alloc_ep_skb_list(&child_ep->com.ep_skb_list, CN_MAX_CON_BUF))
		goto fail;

	state_set(&child_ep->com, CONNECTING);
	child_ep->com.dev = dev;
	child_ep->com.cm_id = NULL;

	if (iptype == 4) {
		struct sockaddr_in *sin = (struct sockaddr_in *)
			&child_ep->com.local_addr;

		sin->sin_family = PF_INET;
		sin->sin_port = local_port;
		sin->sin_addr.s_addr = *(__be32 *)local_ip;

		sin = (struct sockaddr_in *)&child_ep->com.local_addr;
		sin->sin_family = PF_INET;
		sin->sin_port = ((struct sockaddr_in *)
				 &parent_ep->com.local_addr)->sin_port;
		sin->sin_addr.s_addr = *(__be32 *)local_ip;

		sin = (struct sockaddr_in *)&child_ep->com.remote_addr;
		sin->sin_family = PF_INET;
		sin->sin_port = peer_port;
		sin->sin_addr.s_addr = *(__be32 *)peer_ip;
	} else {
		sin6 = (struct sockaddr_in6 *)&child_ep->com.local_addr;
		sin6->sin6_family = PF_INET6;
		sin6->sin6_port = local_port;
		memcpy(sin6->sin6_addr.s6_addr, local_ip, 16);

		sin6 = (struct sockaddr_in6 *)&child_ep->com.local_addr;
		sin6->sin6_family = PF_INET6;
		sin6->sin6_port = ((struct sockaddr_in6 *)
				   &parent_ep->com.local_addr)->sin6_port;
		memcpy(sin6->sin6_addr.s6_addr, local_ip, 16);

		sin6 = (struct sockaddr_in6 *)&child_ep->com.remote_addr;
		sin6->sin6_family = PF_INET6;
		sin6->sin6_port = peer_port;
		memcpy(sin6->sin6_addr.s6_addr, peer_ip, 16);
	}

	c4iw_get_ep(&parent_ep->com);
	child_ep->parent_ep = parent_ep;
	child_ep->tos = tos;
	child_ep->dst = dst;
	child_ep->hwtid = hwtid;

	PDBG("%s tx_chan %u smac_idx %u rss_qid %u\n", __func__,
	     child_ep->tx_chan, child_ep->smac_idx, child_ep->rss_qid);

	init_timer(&child_ep->timer);
	cxgb4_insert_tid(t, child_ep, hwtid);
	insert_ep_tid(child_ep);
	if (accept_cr(child_ep, skb, req)) {
		c4iw_put_ep(&parent_ep->com);
		release_ep_resources(child_ep);
	} else {
		set_bit(PASS_ACCEPT_REQ, &child_ep->com.history);
	}
	if (iptype == 6) {
		sin6 = (struct sockaddr_in6 *)&child_ep->com.local_addr;
		cxgb4_clip_get(child_ep->com.dev->rdev.lldi.ports[0],
			       (const u32 *)&sin6->sin6_addr.s6_addr, 1);
	}
	goto out;
fail:
	c4iw_put_ep(&child_ep->com);
reject:
	reject_cr(dev, hwtid, skb);
	if (parent_ep)
		c4iw_put_ep(&parent_ep->com);
out:
	return 0;
}

static int pass_establish(struct c4iw_dev *dev, struct sk_buff *skb)
{
	struct c4iw_ep *ep;
	struct cpl_pass_establish *req = cplhdr(skb);
	unsigned int tid = GET_TID(req);
	int ret;

	ep = get_ep_from_tid(dev, tid);
	PDBG("%s ep %p tid %u\n", __func__, ep, ep->hwtid);
	ep->snd_seq = be32_to_cpu(req->snd_isn);
	ep->rcv_seq = be32_to_cpu(req->rcv_isn);

	PDBG("%s ep %p hwtid %u tcp_opt 0x%02x\n", __func__, ep, tid,
	     ntohs(req->tcp_opt));

	set_emss(ep, ntohs(req->tcp_opt));

	dst_confirm(ep->dst);
	mutex_lock(&ep->com.mutex);
	ep->com.state = MPA_REQ_WAIT;
	start_ep_timer(ep);
	set_bit(PASS_ESTAB, &ep->com.history);
	ret = send_flowc(ep);
	mutex_unlock(&ep->com.mutex);
	if (ret)
		c4iw_ep_disconnect(ep, 1, GFP_KERNEL);
	c4iw_put_ep(&ep->com);

	return 0;
}

static int peer_close(struct c4iw_dev *dev, struct sk_buff *skb)
{
	struct cpl_peer_close *hdr = cplhdr(skb);
	struct c4iw_ep *ep;
	struct c4iw_qp_attributes attrs;
	int disconnect = 1;
	int release = 0;
	unsigned int tid = GET_TID(hdr);
	int ret;

	ep = get_ep_from_tid(dev, tid);
	if (!ep)
		return 0;

	PDBG("%s ep %p tid %u\n", __func__, ep, ep->hwtid);
	dst_confirm(ep->dst);

	set_bit(PEER_CLOSE, &ep->com.history);
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
		(void)stop_ep_timer(ep);
		if (ep->com.cm_id && ep->com.qp) {
			attrs.next_state = C4IW_QP_STATE_IDLE;
			c4iw_modify_qp(ep->com.qp->rhp, ep->com.qp,
				       C4IW_QP_ATTR_NEXT_STATE, &attrs, 1);
		}
		close_complete_upcall(ep, 0);
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
	c4iw_put_ep(&ep->com);
	return 0;
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
	unsigned int tid = GET_TID(req);

	ep = get_ep_from_tid(dev, tid);
	if (!ep)
		return 0;

	if (is_neg_adv(req->status)) {
		PDBG("%s Negative advice on abort- tid %u status %d (%s)\n",
		     __func__, ep->hwtid, req->status,
		     neg_adv_str(req->status));
		ep->stats.abort_neg_adv++;
		mutex_lock(&dev->rdev.stats.lock);
		dev->rdev.stats.neg_adv++;
		mutex_unlock(&dev->rdev.stats.lock);
		goto deref_ep;
	}
	PDBG("%s ep %p tid %u state %u\n", __func__, ep, ep->hwtid,
	     ep->com.state);
	set_bit(PEER_ABORT, &ep->com.history);

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
		c4iw_put_ep(&ep->parent_ep->com);
		break;
	case MPA_REQ_WAIT:
		(void)stop_ep_timer(ep);
		break;
	case MPA_REQ_SENT:
		(void)stop_ep_timer(ep);
		if (mpa_rev == 1 || (mpa_rev == 2 && ep->tried_with_mpa_v1))
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
		goto deref_ep;
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

	rpl_skb = skb_dequeue(&ep->com.ep_skb_list);
	if (WARN_ON(!rpl_skb)) {
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
	else if (ep->retry_with_mpa_v1) {
		if (ep->com.remote_addr.ss_family == AF_INET6) {
			struct sockaddr_in6 *sin6 =
					(struct sockaddr_in6 *)
					&ep->com.local_addr;
			cxgb4_clip_release(
					ep->com.dev->rdev.lldi.ports[0],
					(const u32 *)&sin6->sin6_addr.s6_addr,
					1);
		}
		remove_handle(ep->com.dev, &ep->com.dev->hwtid_idr, ep->hwtid);
		cxgb4_remove_tid(ep->com.dev->rdev.lldi.tids, 0, ep->hwtid);
		dst_release(ep->dst);
		cxgb4_l2t_release(ep->l2t);
		c4iw_reconnect(ep);
	}

deref_ep:
	c4iw_put_ep(&ep->com);
	/* Dereferencing ep, referenced in peer_abort_intr() */
	c4iw_put_ep(&ep->com);
	return 0;
}

static int close_con_rpl(struct c4iw_dev *dev, struct sk_buff *skb)
{
	struct c4iw_ep *ep;
	struct c4iw_qp_attributes attrs;
	struct cpl_close_con_rpl *rpl = cplhdr(skb);
	int release = 0;
	unsigned int tid = GET_TID(rpl);

	ep = get_ep_from_tid(dev, tid);
	if (!ep)
		return 0;

	PDBG("%s ep %p tid %u\n", __func__, ep, ep->hwtid);
	BUG_ON(!ep);

	/* The cm_id may be null if we failed to connect */
	mutex_lock(&ep->com.mutex);
	set_bit(CLOSE_CON_RPL, &ep->com.history);
	switch (ep->com.state) {
	case CLOSING:
		__state_set(&ep->com, MORIBUND);
		break;
	case MORIBUND:
		(void)stop_ep_timer(ep);
		if ((ep->com.cm_id) && (ep->com.qp)) {
			attrs.next_state = C4IW_QP_STATE_IDLE;
			c4iw_modify_qp(ep->com.qp->rhp,
					     ep->com.qp,
					     C4IW_QP_ATTR_NEXT_STATE,
					     &attrs, 1);
		}
		close_complete_upcall(ep, 0);
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
	c4iw_put_ep(&ep->com);
	return 0;
}

static int terminate(struct c4iw_dev *dev, struct sk_buff *skb)
{
	struct cpl_rdma_terminate *rpl = cplhdr(skb);
	unsigned int tid = GET_TID(rpl);
	struct c4iw_ep *ep;
	struct c4iw_qp_attributes attrs;

	ep = get_ep_from_tid(dev, tid);
	BUG_ON(!ep);

	if (ep && ep->com.qp) {
		printk(KERN_WARNING MOD "TERM received tid %u qpid %u\n", tid,
		       ep->com.qp->wq.sq.qid);
		attrs.next_state = C4IW_QP_STATE_TERMINATE;
		c4iw_modify_qp(ep->com.qp->rhp, ep->com.qp,
			       C4IW_QP_ATTR_NEXT_STATE, &attrs, 1);
	} else
		printk(KERN_WARNING MOD "TERM received tid %u no ep/qp\n", tid);
	c4iw_put_ep(&ep->com);

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


	ep = get_ep_from_tid(dev, tid);
	if (!ep)
		return 0;
	PDBG("%s ep %p tid %u credits %u\n", __func__, ep, ep->hwtid, credits);
	if (credits == 0) {
		PDBG("%s 0 credit ack ep %p tid %u state %u\n",
		     __func__, ep, ep->hwtid, state_read(&ep->com));
		goto out;
	}

	dst_confirm(ep->dst);
	if (ep->mpa_skb) {
		PDBG("%s last streaming msg ack ep %p tid %u state %u "
		     "initiator %u freeing skb\n", __func__, ep, ep->hwtid,
		     state_read(&ep->com), ep->mpa_attr.initiator ? 1 : 0);
		mutex_lock(&ep->com.mutex);
		kfree_skb(ep->mpa_skb);
		ep->mpa_skb = NULL;
		if (test_bit(STOP_MPA_TIMER, &ep->com.flags))
			stop_ep_timer(ep);
		mutex_unlock(&ep->com.mutex);
	}
out:
	c4iw_put_ep(&ep->com);
	return 0;
}

int c4iw_reject_cr(struct iw_cm_id *cm_id, const void *pdata, u8 pdata_len)
{
	int abort;
	struct c4iw_ep *ep = to_ep(cm_id);

	PDBG("%s ep %p tid %u\n", __func__, ep, ep->hwtid);

	mutex_lock(&ep->com.mutex);
	if (ep->com.state != MPA_REQ_RCVD) {
		mutex_unlock(&ep->com.mutex);
		c4iw_put_ep(&ep->com);
		return -ECONNRESET;
	}
	set_bit(ULP_REJECT, &ep->com.history);
	if (mpa_rev == 0)
		abort = 1;
	else
		abort = send_mpa_reject(ep, pdata, pdata_len);
	mutex_unlock(&ep->com.mutex);

	stop_ep_timer(ep);
	c4iw_ep_disconnect(ep, abort != 0, GFP_KERNEL);
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
	int abort = 0;

	PDBG("%s ep %p tid %u\n", __func__, ep, ep->hwtid);

	mutex_lock(&ep->com.mutex);
	if (ep->com.state != MPA_REQ_RCVD) {
		err = -ECONNRESET;
		goto err_out;
	}

	BUG_ON(!qp);

	set_bit(ULP_ACCEPT, &ep->com.history);
	if ((conn_param->ord > cur_max_read_depth(ep->com.dev)) ||
	    (conn_param->ird > cur_max_read_depth(ep->com.dev))) {
		err = -EINVAL;
		goto err_abort;
	}

	if (ep->mpa_attr.version == 2 && ep->mpa_attr.enhanced_rdma_conn) {
		if (conn_param->ord > ep->ird) {
			if (RELAXED_IRD_NEGOTIATION) {
				conn_param->ord = ep->ird;
			} else {
				ep->ird = conn_param->ird;
				ep->ord = conn_param->ord;
				send_mpa_reject(ep, conn_param->private_data,
						conn_param->private_data_len);
				err = -ENOMEM;
				goto err_abort;
			}
		}
		if (conn_param->ird < ep->ord) {
			if (RELAXED_IRD_NEGOTIATION &&
			    ep->ord <= h->rdev.lldi.max_ordird_qp) {
				conn_param->ird = ep->ord;
			} else {
				err = -ENOMEM;
				goto err_abort;
			}
		}
	}
	ep->ird = conn_param->ird;
	ep->ord = conn_param->ord;

	if (ep->mpa_attr.version == 1) {
		if (peer2peer && ep->ird == 0)
			ep->ird = 1;
	} else {
		if (peer2peer &&
		    (ep->mpa_attr.p2p_type != FW_RI_INIT_P2PTYPE_DISABLED) &&
		    (p2p_type == FW_RI_INIT_P2PTYPE_READ_REQ) && ep->ird == 0)
			ep->ird = 1;
	}

	PDBG("%s %d ird %d ord %d\n", __func__, __LINE__, ep->ird, ep->ord);

	ep->com.cm_id = cm_id;
	ref_cm_id(&ep->com);
	ep->com.qp = qp;
	ref_qp(ep);

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
		goto err_deref_cm_id;

	set_bit(STOP_MPA_TIMER, &ep->com.flags);
	err = send_mpa_reply(ep, conn_param->private_data,
			     conn_param->private_data_len);
	if (err)
		goto err_deref_cm_id;

	__state_set(&ep->com, FPDU_MODE);
	established_upcall(ep);
	mutex_unlock(&ep->com.mutex);
	c4iw_put_ep(&ep->com);
	return 0;
err_deref_cm_id:
	deref_cm_id(&ep->com);
err_abort:
	abort = 1;
err_out:
	mutex_unlock(&ep->com.mutex);
	if (abort)
		c4iw_ep_disconnect(ep, 1, GFP_KERNEL);
	c4iw_put_ep(&ep->com);
	return err;
}

static int pick_local_ipaddrs(struct c4iw_dev *dev, struct iw_cm_id *cm_id)
{
	struct in_device *ind;
	int found = 0;
	struct sockaddr_in *laddr = (struct sockaddr_in *)&cm_id->m_local_addr;
	struct sockaddr_in *raddr = (struct sockaddr_in *)&cm_id->m_remote_addr;

	ind = in_dev_get(dev->rdev.lldi.ports[0]);
	if (!ind)
		return -EADDRNOTAVAIL;
	for_primary_ifa(ind) {
		laddr->sin_addr.s_addr = ifa->ifa_address;
		raddr->sin_addr.s_addr = ifa->ifa_address;
		found = 1;
		break;
	}
	endfor_ifa(ind);
	in_dev_put(ind);
	return found ? 0 : -EADDRNOTAVAIL;
}

static int get_lladdr(struct net_device *dev, struct in6_addr *addr,
		      unsigned char banned_flags)
{
	struct inet6_dev *idev;
	int err = -EADDRNOTAVAIL;

	rcu_read_lock();
	idev = __in6_dev_get(dev);
	if (idev != NULL) {
		struct inet6_ifaddr *ifp;

		read_lock_bh(&idev->lock);
		list_for_each_entry(ifp, &idev->addr_list, if_list) {
			if (ifp->scope == IFA_LINK &&
			    !(ifp->flags & banned_flags)) {
				memcpy(addr, &ifp->addr, 16);
				err = 0;
				break;
			}
		}
		read_unlock_bh(&idev->lock);
	}
	rcu_read_unlock();
	return err;
}

static int pick_local_ip6addrs(struct c4iw_dev *dev, struct iw_cm_id *cm_id)
{
	struct in6_addr uninitialized_var(addr);
	struct sockaddr_in6 *la6 = (struct sockaddr_in6 *)&cm_id->m_local_addr;
	struct sockaddr_in6 *ra6 = (struct sockaddr_in6 *)&cm_id->m_remote_addr;

	if (!get_lladdr(dev->rdev.lldi.ports[0], &addr, IFA_F_TENTATIVE)) {
		memcpy(la6->sin6_addr.s6_addr, &addr, 16);
		memcpy(ra6->sin6_addr.s6_addr, &addr, 16);
		return 0;
	}
	return -EADDRNOTAVAIL;
}

int c4iw_connect(struct iw_cm_id *cm_id, struct iw_cm_conn_param *conn_param)
{
	struct c4iw_dev *dev = to_c4iw_dev(cm_id->device);
	struct c4iw_ep *ep;
	int err = 0;
	struct sockaddr_in *laddr;
	struct sockaddr_in *raddr;
	struct sockaddr_in6 *laddr6;
	struct sockaddr_in6 *raddr6;
	__u8 *ra;
	int iptype;

	if ((conn_param->ord > cur_max_read_depth(dev)) ||
	    (conn_param->ird > cur_max_read_depth(dev))) {
		err = -EINVAL;
		goto out;
	}
	ep = alloc_ep(sizeof(*ep), GFP_KERNEL);
	if (!ep) {
		printk(KERN_ERR MOD "%s - cannot alloc ep.\n", __func__);
		err = -ENOMEM;
		goto out;
	}

	skb_queue_head_init(&ep->com.ep_skb_list);
	if (alloc_ep_skb_list(&ep->com.ep_skb_list, CN_MAX_CON_BUF)) {
		err = -ENOMEM;
		goto fail1;
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

	ep->com.cm_id = cm_id;
	ref_cm_id(&ep->com);
	ep->com.dev = dev;
	ep->com.qp = get_qhp(dev, conn_param->qpn);
	if (!ep->com.qp) {
		PDBG("%s qpn 0x%x not found!\n", __func__, conn_param->qpn);
		err = -EINVAL;
		goto fail2;
	}
	ref_qp(ep);
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
	insert_handle(dev, &dev->atid_idr, ep, ep->atid);

	memcpy(&ep->com.local_addr, &cm_id->m_local_addr,
	       sizeof(ep->com.local_addr));
	memcpy(&ep->com.remote_addr, &cm_id->m_remote_addr,
	       sizeof(ep->com.remote_addr));

	laddr = (struct sockaddr_in *)&ep->com.local_addr;
	raddr = (struct sockaddr_in *)&ep->com.remote_addr;
	laddr6 = (struct sockaddr_in6 *)&ep->com.local_addr;
	raddr6 = (struct sockaddr_in6 *) &ep->com.remote_addr;

	if (cm_id->m_remote_addr.ss_family == AF_INET) {
		iptype = 4;
		ra = (__u8 *)&raddr->sin_addr;

		/*
		 * Handle loopback requests to INADDR_ANY.
		 */
		if (raddr->sin_addr.s_addr == htonl(INADDR_ANY)) {
			err = pick_local_ipaddrs(dev, cm_id);
			if (err)
				goto fail2;
		}

		/* find a route */
		PDBG("%s saddr %pI4 sport 0x%x raddr %pI4 rport 0x%x\n",
		     __func__, &laddr->sin_addr, ntohs(laddr->sin_port),
		     ra, ntohs(raddr->sin_port));
		ep->dst = find_route(dev, laddr->sin_addr.s_addr,
				     raddr->sin_addr.s_addr, laddr->sin_port,
				     raddr->sin_port, cm_id->tos);
	} else {
		iptype = 6;
		ra = (__u8 *)&raddr6->sin6_addr;

		/*
		 * Handle loopback requests to INADDR_ANY.
		 */
		if (ipv6_addr_type(&raddr6->sin6_addr) == IPV6_ADDR_ANY) {
			err = pick_local_ip6addrs(dev, cm_id);
			if (err)
				goto fail2;
		}

		/* find a route */
		PDBG("%s saddr %pI6 sport 0x%x raddr %pI6 rport 0x%x\n",
		     __func__, laddr6->sin6_addr.s6_addr,
		     ntohs(laddr6->sin6_port),
		     raddr6->sin6_addr.s6_addr, ntohs(raddr6->sin6_port));
		ep->dst = find_route6(dev, laddr6->sin6_addr.s6_addr,
				      raddr6->sin6_addr.s6_addr,
				      laddr6->sin6_port, raddr6->sin6_port, 0,
				      raddr6->sin6_scope_id);
	}
	if (!ep->dst) {
		printk(KERN_ERR MOD "%s - cannot find route.\n", __func__);
		err = -EHOSTUNREACH;
		goto fail3;
	}

	err = import_ep(ep, iptype, ra, ep->dst, ep->com.dev, true,
			ep->com.dev->rdev.lldi.adapter_type, cm_id->tos);
	if (err) {
		printk(KERN_ERR MOD "%s - cannot alloc l2e.\n", __func__);
		goto fail4;
	}

	PDBG("%s txq_idx %u tx_chan %u smac_idx %u rss_qid %u l2t_idx %u\n",
		__func__, ep->txq_idx, ep->tx_chan, ep->smac_idx, ep->rss_qid,
		ep->l2t->idx);

	state_set(&ep->com, CONNECTING);
	ep->tos = cm_id->tos;

	/* send connect request to rnic */
	err = send_connect(ep);
	if (!err)
		goto out;

	cxgb4_l2t_release(ep->l2t);
fail4:
	dst_release(ep->dst);
fail3:
	remove_handle(ep->com.dev, &ep->com.dev->atid_idr, ep->atid);
	cxgb4_free_atid(ep->com.dev->rdev.lldi.tids, ep->atid);
fail2:
	skb_queue_purge(&ep->com.ep_skb_list);
	deref_cm_id(&ep->com);
fail1:
	c4iw_put_ep(&ep->com);
out:
	return err;
}

static int create_server6(struct c4iw_dev *dev, struct c4iw_listen_ep *ep)
{
	int err;
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)
				    &ep->com.local_addr;

	if (ipv6_addr_type(&sin6->sin6_addr) != IPV6_ADDR_ANY) {
		err = cxgb4_clip_get(ep->com.dev->rdev.lldi.ports[0],
				     (const u32 *)&sin6->sin6_addr.s6_addr, 1);
		if (err)
			return err;
	}
	c4iw_init_wr_wait(&ep->com.wr_wait);
	err = cxgb4_create_server6(ep->com.dev->rdev.lldi.ports[0],
				   ep->stid, &sin6->sin6_addr,
				   sin6->sin6_port,
				   ep->com.dev->rdev.lldi.rxq_ids[0]);
	if (!err)
		err = c4iw_wait_for_reply(&ep->com.dev->rdev,
					  &ep->com.wr_wait,
					  0, 0, __func__);
	else if (err > 0)
		err = net_xmit_errno(err);
	if (err) {
		cxgb4_clip_release(ep->com.dev->rdev.lldi.ports[0],
				   (const u32 *)&sin6->sin6_addr.s6_addr, 1);
		pr_err("cxgb4_create_server6/filter failed err %d stid %d laddr %pI6 lport %d\n",
		       err, ep->stid,
		       sin6->sin6_addr.s6_addr, ntohs(sin6->sin6_port));
	}
	return err;
}

static int create_server4(struct c4iw_dev *dev, struct c4iw_listen_ep *ep)
{
	int err;
	struct sockaddr_in *sin = (struct sockaddr_in *)
				  &ep->com.local_addr;

	if (dev->rdev.lldi.enable_fw_ofld_conn) {
		do {
			err = cxgb4_create_server_filter(
				ep->com.dev->rdev.lldi.ports[0], ep->stid,
				sin->sin_addr.s_addr, sin->sin_port, 0,
				ep->com.dev->rdev.lldi.rxq_ids[0], 0, 0);
			if (err == -EBUSY) {
				if (c4iw_fatal_error(&ep->com.dev->rdev)) {
					err = -EIO;
					break;
				}
				set_current_state(TASK_UNINTERRUPTIBLE);
				schedule_timeout(usecs_to_jiffies(100));
			}
		} while (err == -EBUSY);
	} else {
		c4iw_init_wr_wait(&ep->com.wr_wait);
		err = cxgb4_create_server(ep->com.dev->rdev.lldi.ports[0],
				ep->stid, sin->sin_addr.s_addr, sin->sin_port,
				0, ep->com.dev->rdev.lldi.rxq_ids[0]);
		if (!err)
			err = c4iw_wait_for_reply(&ep->com.dev->rdev,
						  &ep->com.wr_wait,
						  0, 0, __func__);
		else if (err > 0)
			err = net_xmit_errno(err);
	}
	if (err)
		pr_err("cxgb4_create_server/filter failed err %d stid %d laddr %pI4 lport %d\n"
		       , err, ep->stid,
		       &sin->sin_addr, ntohs(sin->sin_port));
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
	skb_queue_head_init(&ep->com.ep_skb_list);
	PDBG("%s ep %p\n", __func__, ep);
	ep->com.cm_id = cm_id;
	ref_cm_id(&ep->com);
	ep->com.dev = dev;
	ep->backlog = backlog;
	memcpy(&ep->com.local_addr, &cm_id->m_local_addr,
	       sizeof(ep->com.local_addr));

	/*
	 * Allocate a server TID.
	 */
	if (dev->rdev.lldi.enable_fw_ofld_conn &&
	    ep->com.local_addr.ss_family == AF_INET)
		ep->stid = cxgb4_alloc_sftid(dev->rdev.lldi.tids,
					     cm_id->m_local_addr.ss_family, ep);
	else
		ep->stid = cxgb4_alloc_stid(dev->rdev.lldi.tids,
					    cm_id->m_local_addr.ss_family, ep);

	if (ep->stid == -1) {
		printk(KERN_ERR MOD "%s - cannot alloc stid.\n", __func__);
		err = -ENOMEM;
		goto fail2;
	}
	insert_handle(dev, &dev->stid_idr, ep, ep->stid);

	memcpy(&ep->com.local_addr, &cm_id->m_local_addr,
	       sizeof(ep->com.local_addr));

	state_set(&ep->com, LISTEN);
	if (ep->com.local_addr.ss_family == AF_INET)
		err = create_server4(dev, ep);
	else
		err = create_server6(dev, ep);
	if (!err) {
		cm_id->provider_data = ep;
		goto out;
	}

	cxgb4_free_stid(ep->com.dev->rdev.lldi.tids, ep->stid,
			ep->com.local_addr.ss_family);
fail2:
	deref_cm_id(&ep->com);
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
	if (ep->com.dev->rdev.lldi.enable_fw_ofld_conn &&
	    ep->com.local_addr.ss_family == AF_INET) {
		err = cxgb4_remove_server_filter(
			ep->com.dev->rdev.lldi.ports[0], ep->stid,
			ep->com.dev->rdev.lldi.rxq_ids[0], 0);
	} else {
		struct sockaddr_in6 *sin6;
		c4iw_init_wr_wait(&ep->com.wr_wait);
		err = cxgb4_remove_server(
				ep->com.dev->rdev.lldi.ports[0], ep->stid,
				ep->com.dev->rdev.lldi.rxq_ids[0], 0);
		if (err)
			goto done;
		err = c4iw_wait_for_reply(&ep->com.dev->rdev, &ep->com.wr_wait,
					  0, 0, __func__);
		sin6 = (struct sockaddr_in6 *)&ep->com.local_addr;
		cxgb4_clip_release(ep->com.dev->rdev.lldi.ports[0],
				   (const u32 *)&sin6->sin6_addr.s6_addr, 1);
	}
	remove_handle(ep->com.dev, &ep->com.dev->stid_idr, ep->stid);
	cxgb4_free_stid(ep->com.dev->rdev.lldi.tids, ep->stid,
			ep->com.local_addr.ss_family);
done:
	deref_cm_id(&ep->com);
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

	/*
	 * Ref the ep here in case we have fatal errors causing the
	 * ep to be released and freed.
	 */
	c4iw_get_ep(&ep->com);

	rdev = &ep->com.dev->rdev;
	if (c4iw_fatal_error(rdev)) {
		fatal = 1;
		close_complete_upcall(ep, -EIO);
		ep->com.state = DEAD;
	}
	switch (ep->com.state) {
	case MPA_REQ_WAIT:
	case MPA_REQ_SENT:
	case MPA_REQ_RCVD:
	case MPA_REP_SENT:
	case FPDU_MODE:
	case CONNECTING:
		close = 1;
		if (abrupt)
			ep->com.state = ABORTING;
		else {
			ep->com.state = CLOSING;

			/*
			 * if we close before we see the fw4_ack() then we fix
			 * up the timer state since we're reusing it.
			 */
			if (ep->mpa_skb &&
			    test_bit(STOP_MPA_TIMER, &ep->com.flags)) {
				clear_bit(STOP_MPA_TIMER, &ep->com.flags);
				stop_ep_timer(ep);
			}
			start_ep_timer(ep);
		}
		set_bit(CLOSE_SENT, &ep->com.flags);
		break;
	case CLOSING:
		if (!test_and_set_bit(CLOSE_SENT, &ep->com.flags)) {
			close = 1;
			if (abrupt) {
				(void)stop_ep_timer(ep);
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
			set_bit(EP_DISC_ABORT, &ep->com.history);
			close_complete_upcall(ep, -ECONNRESET);
			ret = send_abort(ep);
		} else {
			set_bit(EP_DISC_CLOSE, &ep->com.history);
			ret = send_halfclose(ep);
		}
		if (ret) {
			set_bit(EP_DISC_FAIL, &ep->com.history);
			if (!abrupt) {
				stop_ep_timer(ep);
				close_complete_upcall(ep, -EIO);
			}
			if (ep->com.qp) {
				struct c4iw_qp_attributes attrs;

				attrs.next_state = C4IW_QP_STATE_ERROR;
				ret = c4iw_modify_qp(ep->com.qp->rhp,
						     ep->com.qp,
						     C4IW_QP_ATTR_NEXT_STATE,
						     &attrs, 1);
				if (ret)
					pr_err(MOD
					       "%s - qp <- error failed!\n",
					       __func__);
			}
			fatal = 1;
		}
	}
	mutex_unlock(&ep->com.mutex);
	c4iw_put_ep(&ep->com);
	if (fatal)
		release_ep_resources(ep);
	return ret;
}

static void active_ofld_conn_reply(struct c4iw_dev *dev, struct sk_buff *skb,
			struct cpl_fw6_msg_ofld_connection_wr_rpl *req)
{
	struct c4iw_ep *ep;
	int atid = be32_to_cpu(req->tid);

	ep = (struct c4iw_ep *)lookup_atid(dev->rdev.lldi.tids,
					   (__force u32) req->tid);
	if (!ep)
		return;

	switch (req->retval) {
	case FW_ENOMEM:
		set_bit(ACT_RETRY_NOMEM, &ep->com.history);
		if (ep->retry_count++ < ACT_OPEN_RETRY_COUNT) {
			send_fw_act_open_req(ep, atid);
			return;
		}
	case FW_EADDRINUSE:
		set_bit(ACT_RETRY_INUSE, &ep->com.history);
		if (ep->retry_count++ < ACT_OPEN_RETRY_COUNT) {
			send_fw_act_open_req(ep, atid);
			return;
		}
		break;
	default:
		pr_info("%s unexpected ofld conn wr retval %d\n",
		       __func__, req->retval);
		break;
	}
	pr_err("active ofld_connect_wr failure %d atid %d\n",
	       req->retval, atid);
	mutex_lock(&dev->rdev.stats.lock);
	dev->rdev.stats.act_ofld_conn_fails++;
	mutex_unlock(&dev->rdev.stats.lock);
	connect_reply_upcall(ep, status2errno(req->retval));
	state_set(&ep->com, DEAD);
	if (ep->com.remote_addr.ss_family == AF_INET6) {
		struct sockaddr_in6 *sin6 =
			(struct sockaddr_in6 *)&ep->com.local_addr;
		cxgb4_clip_release(ep->com.dev->rdev.lldi.ports[0],
				   (const u32 *)&sin6->sin6_addr.s6_addr, 1);
	}
	remove_handle(dev, &dev->atid_idr, atid);
	cxgb4_free_atid(dev->rdev.lldi.tids, atid);
	dst_release(ep->dst);
	cxgb4_l2t_release(ep->l2t);
	c4iw_put_ep(&ep->com);
}

static void passive_ofld_conn_reply(struct c4iw_dev *dev, struct sk_buff *skb,
			struct cpl_fw6_msg_ofld_connection_wr_rpl *req)
{
	struct sk_buff *rpl_skb;
	struct cpl_pass_accept_req *cpl;
	int ret;

	rpl_skb = (struct sk_buff *)(unsigned long)req->cookie;
	BUG_ON(!rpl_skb);
	if (req->retval) {
		PDBG("%s passive open failure %d\n", __func__, req->retval);
		mutex_lock(&dev->rdev.stats.lock);
		dev->rdev.stats.pas_ofld_conn_fails++;
		mutex_unlock(&dev->rdev.stats.lock);
		kfree_skb(rpl_skb);
	} else {
		cpl = (struct cpl_pass_accept_req *)cplhdr(rpl_skb);
		OPCODE_TID(cpl) = htonl(MK_OPCODE_TID(CPL_PASS_ACCEPT_REQ,
					(__force u32) htonl(
					(__force u32) req->tid)));
		ret = pass_accept_req(dev, rpl_skb);
		if (!ret)
			kfree_skb(rpl_skb);
	}
	return;
}

static int deferred_fw6_msg(struct c4iw_dev *dev, struct sk_buff *skb)
{
	struct cpl_fw6_msg *rpl = cplhdr(skb);
	struct cpl_fw6_msg_ofld_connection_wr_rpl *req;

	switch (rpl->type) {
	case FW6_TYPE_CQE:
		c4iw_ev_dispatch(dev, (struct t4_cqe *)&rpl->data[0]);
		break;
	case FW6_TYPE_OFLD_CONNECTION_WR_RPL:
		req = (struct cpl_fw6_msg_ofld_connection_wr_rpl *)rpl->data;
		switch (req->t_state) {
		case TCP_SYN_SENT:
			active_ofld_conn_reply(dev, skb, req);
			break;
		case TCP_SYN_RECV:
			passive_ofld_conn_reply(dev, skb, req);
			break;
		default:
			pr_err("%s unexpected ofld conn wr state %d\n",
			       __func__, req->t_state);
			break;
		}
		break;
	}
	return 0;
}

static void build_cpl_pass_accept_req(struct sk_buff *skb, int stid , u8 tos)
{
	__be32 l2info;
	__be16 hdr_len, vlantag, len;
	u16 eth_hdr_len;
	int tcp_hdr_len, ip_hdr_len;
	u8 intf;
	struct cpl_rx_pkt *cpl = cplhdr(skb);
	struct cpl_pass_accept_req *req;
	struct tcp_options_received tmp_opt;
	struct c4iw_dev *dev;
	enum chip_type type;

	dev = *((struct c4iw_dev **) (skb->cb + sizeof(void *)));
	/* Store values from cpl_rx_pkt in temporary location. */
	vlantag = cpl->vlan;
	len = cpl->len;
	l2info  = cpl->l2info;
	hdr_len = cpl->hdr_len;
	intf = cpl->iff;

	__skb_pull(skb, sizeof(*req) + sizeof(struct rss_header));

	/*
	 * We need to parse the TCP options from SYN packet.
	 * to generate cpl_pass_accept_req.
	 */
	memset(&tmp_opt, 0, sizeof(tmp_opt));
	tcp_clear_options(&tmp_opt);
	tcp_parse_options(skb, &tmp_opt, 0, NULL);

	req = (struct cpl_pass_accept_req *)__skb_push(skb, sizeof(*req));
	memset(req, 0, sizeof(*req));
	req->l2info = cpu_to_be16(SYN_INTF_V(intf) |
			 SYN_MAC_IDX_V(RX_MACIDX_G(
			 be32_to_cpu(l2info))) |
			 SYN_XACT_MATCH_F);
	type = dev->rdev.lldi.adapter_type;
	tcp_hdr_len = RX_TCPHDR_LEN_G(be16_to_cpu(hdr_len));
	ip_hdr_len = RX_IPHDR_LEN_G(be16_to_cpu(hdr_len));
	req->hdr_len =
		cpu_to_be32(SYN_RX_CHAN_V(RX_CHAN_G(be32_to_cpu(l2info))));
	if (CHELSIO_CHIP_VERSION(type) <= CHELSIO_T5) {
		eth_hdr_len = is_t4(type) ?
				RX_ETHHDR_LEN_G(be32_to_cpu(l2info)) :
				RX_T5_ETHHDR_LEN_G(be32_to_cpu(l2info));
		req->hdr_len |= cpu_to_be32(TCP_HDR_LEN_V(tcp_hdr_len) |
					    IP_HDR_LEN_V(ip_hdr_len) |
					    ETH_HDR_LEN_V(eth_hdr_len));
	} else { /* T6 and later */
		eth_hdr_len = RX_T6_ETHHDR_LEN_G(be32_to_cpu(l2info));
		req->hdr_len |= cpu_to_be32(T6_TCP_HDR_LEN_V(tcp_hdr_len) |
					    T6_IP_HDR_LEN_V(ip_hdr_len) |
					    T6_ETH_HDR_LEN_V(eth_hdr_len));
	}
	req->vlan = vlantag;
	req->len = len;
	req->tos_stid = cpu_to_be32(PASS_OPEN_TID_V(stid) |
				    PASS_OPEN_TOS_V(tos));
	req->tcpopt.mss = htons(tmp_opt.mss_clamp);
	if (tmp_opt.wscale_ok)
		req->tcpopt.wsf = tmp_opt.snd_wscale;
	req->tcpopt.tstamp = tmp_opt.saw_tstamp;
	if (tmp_opt.sack_ok)
		req->tcpopt.sack = 1;
	OPCODE_TID(req) = htonl(MK_OPCODE_TID(CPL_PASS_ACCEPT_REQ, 0));
	return;
}

static void send_fw_pass_open_req(struct c4iw_dev *dev, struct sk_buff *skb,
				  __be32 laddr, __be16 lport,
				  __be32 raddr, __be16 rport,
				  u32 rcv_isn, u32 filter, u16 window,
				  u32 rss_qid, u8 port_id)
{
	struct sk_buff *req_skb;
	struct fw_ofld_connection_wr *req;
	struct cpl_pass_accept_req *cpl = cplhdr(skb);
	int ret;

	req_skb = alloc_skb(sizeof(struct fw_ofld_connection_wr), GFP_KERNEL);
	req = (struct fw_ofld_connection_wr *)__skb_put(req_skb, sizeof(*req));
	memset(req, 0, sizeof(*req));
	req->op_compl = htonl(WR_OP_V(FW_OFLD_CONNECTION_WR) | FW_WR_COMPL_F);
	req->len16_pkd = htonl(FW_WR_LEN16_V(DIV_ROUND_UP(sizeof(*req), 16)));
	req->le.version_cpl = htonl(FW_OFLD_CONNECTION_WR_CPL_F);
	req->le.filter = (__force __be32) filter;
	req->le.lport = lport;
	req->le.pport = rport;
	req->le.u.ipv4.lip = laddr;
	req->le.u.ipv4.pip = raddr;
	req->tcb.rcv_nxt = htonl(rcv_isn + 1);
	req->tcb.rcv_adv = htons(window);
	req->tcb.t_state_to_astid =
		 htonl(FW_OFLD_CONNECTION_WR_T_STATE_V(TCP_SYN_RECV) |
			FW_OFLD_CONNECTION_WR_RCV_SCALE_V(cpl->tcpopt.wsf) |
			FW_OFLD_CONNECTION_WR_ASTID_V(
			PASS_OPEN_TID_G(ntohl(cpl->tos_stid))));

	/*
	 * We store the qid in opt2 which will be used by the firmware
	 * to send us the wr response.
	 */
	req->tcb.opt2 = htonl(RSS_QUEUE_V(rss_qid));

	/*
	 * We initialize the MSS index in TCB to 0xF.
	 * So that when driver sends cpl_pass_accept_rpl
	 * TCB picks up the correct value. If this was 0
	 * TP will ignore any value > 0 for MSS index.
	 */
	req->tcb.opt0 = cpu_to_be64(MSS_IDX_V(0xF));
	req->cookie = (uintptr_t)skb;

	set_wr_txq(req_skb, CPL_PRIORITY_CONTROL, port_id);
	ret = cxgb4_ofld_send(dev->rdev.lldi.ports[0], req_skb);
	if (ret < 0) {
		pr_err("%s - cxgb4_ofld_send error %d - dropping\n", __func__,
		       ret);
		kfree_skb(skb);
		kfree_skb(req_skb);
	}
}

/*
 * Handler for CPL_RX_PKT message. Need to handle cpl_rx_pkt
 * messages when a filter is being used instead of server to
 * redirect a syn packet. When packets hit filter they are redirected
 * to the offload queue and driver tries to establish the connection
 * using firmware work request.
 */
static int rx_pkt(struct c4iw_dev *dev, struct sk_buff *skb)
{
	int stid;
	unsigned int filter;
	struct ethhdr *eh = NULL;
	struct vlan_ethhdr *vlan_eh = NULL;
	struct iphdr *iph;
	struct tcphdr *tcph;
	struct rss_header *rss = (void *)skb->data;
	struct cpl_rx_pkt *cpl = (void *)skb->data;
	struct cpl_pass_accept_req *req = (void *)(rss + 1);
	struct l2t_entry *e;
	struct dst_entry *dst;
	struct c4iw_ep *lep = NULL;
	u16 window;
	struct port_info *pi;
	struct net_device *pdev;
	u16 rss_qid, eth_hdr_len;
	int step;
	u32 tx_chan;
	struct neighbour *neigh;

	/* Drop all non-SYN packets */
	if (!(cpl->l2info & cpu_to_be32(RXF_SYN_F)))
		goto reject;

	/*
	 * Drop all packets which did not hit the filter.
	 * Unlikely to happen.
	 */
	if (!(rss->filter_hit && rss->filter_tid))
		goto reject;

	/*
	 * Calculate the server tid from filter hit index from cpl_rx_pkt.
	 */
	stid = (__force int) cpu_to_be32((__force u32) rss->hash_val);

	lep = (struct c4iw_ep *)get_ep_from_stid(dev, stid);
	if (!lep) {
		PDBG("%s connect request on invalid stid %d\n", __func__, stid);
		goto reject;
	}

	switch (CHELSIO_CHIP_VERSION(dev->rdev.lldi.adapter_type)) {
	case CHELSIO_T4:
		eth_hdr_len = RX_ETHHDR_LEN_G(be32_to_cpu(cpl->l2info));
		break;
	case CHELSIO_T5:
		eth_hdr_len = RX_T5_ETHHDR_LEN_G(be32_to_cpu(cpl->l2info));
		break;
	case CHELSIO_T6:
		eth_hdr_len = RX_T6_ETHHDR_LEN_G(be32_to_cpu(cpl->l2info));
		break;
	default:
		pr_err("T%d Chip is not supported\n",
		       CHELSIO_CHIP_VERSION(dev->rdev.lldi.adapter_type));
		goto reject;
	}

	if (eth_hdr_len == ETH_HLEN) {
		eh = (struct ethhdr *)(req + 1);
		iph = (struct iphdr *)(eh + 1);
	} else {
		vlan_eh = (struct vlan_ethhdr *)(req + 1);
		iph = (struct iphdr *)(vlan_eh + 1);
		skb->vlan_tci = ntohs(cpl->vlan);
	}

	if (iph->version != 0x4)
		goto reject;

	tcph = (struct tcphdr *)(iph + 1);
	skb_set_network_header(skb, (void *)iph - (void *)rss);
	skb_set_transport_header(skb, (void *)tcph - (void *)rss);
	skb_get(skb);

	PDBG("%s lip 0x%x lport %u pip 0x%x pport %u tos %d\n", __func__,
	     ntohl(iph->daddr), ntohs(tcph->dest), ntohl(iph->saddr),
	     ntohs(tcph->source), iph->tos);

	dst = find_route(dev, iph->daddr, iph->saddr, tcph->dest, tcph->source,
			 iph->tos);
	if (!dst) {
		pr_err("%s - failed to find dst entry!\n",
		       __func__);
		goto reject;
	}
	neigh = dst_neigh_lookup_skb(dst, skb);

	if (!neigh) {
		pr_err("%s - failed to allocate neigh!\n",
		       __func__);
		goto free_dst;
	}

	if (neigh->dev->flags & IFF_LOOPBACK) {
		pdev = ip_dev_find(&init_net, iph->daddr);
		e = cxgb4_l2t_get(dev->rdev.lldi.l2t, neigh,
				    pdev, 0);
		pi = (struct port_info *)netdev_priv(pdev);
		tx_chan = cxgb4_port_chan(pdev);
		dev_put(pdev);
	} else {
		pdev = get_real_dev(neigh->dev);
		e = cxgb4_l2t_get(dev->rdev.lldi.l2t, neigh,
					pdev, 0);
		pi = (struct port_info *)netdev_priv(pdev);
		tx_chan = cxgb4_port_chan(pdev);
	}
	neigh_release(neigh);
	if (!e) {
		pr_err("%s - failed to allocate l2t entry!\n",
		       __func__);
		goto free_dst;
	}

	step = dev->rdev.lldi.nrxq / dev->rdev.lldi.nchan;
	rss_qid = dev->rdev.lldi.rxq_ids[pi->port_id * step];
	window = (__force u16) htons((__force u16)tcph->window);

	/* Calcuate filter portion for LE region. */
	filter = (__force unsigned int) cpu_to_be32(cxgb4_select_ntuple(
						    dev->rdev.lldi.ports[0],
						    e));

	/*
	 * Synthesize the cpl_pass_accept_req. We have everything except the
	 * TID. Once firmware sends a reply with TID we update the TID field
	 * in cpl and pass it through the regular cpl_pass_accept_req path.
	 */
	build_cpl_pass_accept_req(skb, stid, iph->tos);
	send_fw_pass_open_req(dev, skb, iph->daddr, tcph->dest, iph->saddr,
			      tcph->source, ntohl(tcph->seq), filter, window,
			      rss_qid, pi->port_id);
	cxgb4_l2t_release(e);
free_dst:
	dst_release(dst);
reject:
	if (lep)
		c4iw_put_ep(&lep->com);
	return 0;
}

/*
 * These are the real handlers that are called from a
 * work queue.
 */
static c4iw_handler_func work_handlers[NUM_CPL_CMDS + NUM_FAKE_CPLS] = {
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
	[CPL_FW6_MSG] = deferred_fw6_msg,
	[CPL_RX_PKT] = rx_pkt,
	[FAKE_CPL_PUT_EP_SAFE] = _put_ep_safe,
	[FAKE_CPL_PASS_PUT_EP_SAFE] = _put_pass_ep_safe
};

static void process_timeout(struct c4iw_ep *ep)
{
	struct c4iw_qp_attributes attrs;
	int abort = 1;

	mutex_lock(&ep->com.mutex);
	PDBG("%s ep %p tid %u state %d\n", __func__, ep, ep->hwtid,
	     ep->com.state);
	set_bit(TIMEDOUT, &ep->com.history);
	switch (ep->com.state) {
	case MPA_REQ_SENT:
		connect_reply_upcall(ep, -ETIMEDOUT);
		break;
	case MPA_REQ_WAIT:
	case MPA_REQ_RCVD:
	case MPA_REP_SENT:
	case FPDU_MODE:
		break;
	case CLOSING:
	case MORIBUND:
		if (ep->com.cm_id && ep->com.qp) {
			attrs.next_state = C4IW_QP_STATE_ERROR;
			c4iw_modify_qp(ep->com.qp->rhp,
				     ep->com.qp, C4IW_QP_ATTR_NEXT_STATE,
				     &attrs, 1);
		}
		close_complete_upcall(ep, -ETIMEDOUT);
		break;
	case ABORTING:
	case DEAD:

		/*
		 * These states are expected if the ep timed out at the same
		 * time as another thread was calling stop_ep_timer().
		 * So we silently do nothing for these states.
		 */
		abort = 0;
		break;
	default:
		WARN(1, "%s unexpected state ep %p tid %u state %u\n",
			__func__, ep, ep->hwtid, ep->com.state);
		abort = 0;
	}
	mutex_unlock(&ep->com.mutex);
	if (abort)
		c4iw_ep_disconnect(ep, 1, GFP_KERNEL);
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
		tmp->next = NULL;
		tmp->prev = NULL;
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

	process_timedout_eps();
	while ((skb = skb_dequeue(&rxq))) {
		rpl = cplhdr(skb);
		dev = *((struct c4iw_dev **) (skb->cb + sizeof(void *)));
		opcode = rpl->ot.opcode;

		BUG_ON(!work_handlers[opcode]);
		ret = work_handlers[opcode](dev, skb);
		if (!ret)
			kfree_skb(skb);
		process_timedout_eps();
	}
}

static DECLARE_WORK(skb_work, process_work);

static void ep_timeout(unsigned long arg)
{
	struct c4iw_ep *ep = (struct c4iw_ep *)arg;
	int kickit = 0;

	spin_lock(&timeout_lock);
	if (!test_and_set_bit(TIMEOUT, &ep->com.flags)) {
		/*
		 * Only insert if it is not already on the list.
		 */
		if (!ep->entry.next) {
			list_add_tail(&ep->entry, &timeout_list);
			kickit = 1;
		}
	}
	spin_unlock(&timeout_lock);
	if (kickit)
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
	case FW6_TYPE_WR_RPL:
		ret = (int)((be64_to_cpu(rpl->data[0]) >> 8) & 0xff);
		wr_waitp = (struct c4iw_wr_wait *)(__force unsigned long) rpl->data[1];
		PDBG("%s wr_waitp %p ret %u\n", __func__, wr_waitp, ret);
		if (wr_waitp)
			c4iw_wake_up(wr_waitp, ret ? -ret : 0);
		kfree_skb(skb);
		break;
	case FW6_TYPE_CQE:
	case FW6_TYPE_OFLD_CONNECTION_WR_RPL:
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
	unsigned int tid = GET_TID(req);

	ep = get_ep_from_tid(dev, tid);
	/* This EP will be dereferenced in peer_abort() */
	if (!ep) {
		printk(KERN_WARNING MOD
		       "Abort on non-existent endpoint, tid %d\n", tid);
		kfree_skb(skb);
		return 0;
	}
	if (is_neg_adv(req->status)) {
		PDBG("%s Negative advice on abort- tid %u status %d (%s)\n",
		     __func__, ep->hwtid, req->status,
		     neg_adv_str(req->status));
		goto out;
	}
	PDBG("%s ep %p tid %u state %u\n", __func__, ep, ep->hwtid,
	     ep->com.state);

	c4iw_wake_up(&ep->com.wr_wait, -ECONNRESET);
out:
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
	[CPL_FW6_MSG] = fw6_msg,
	[CPL_RX_PKT] = sched
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

void c4iw_cm_term(void)
{
	WARN_ON(!list_empty(&timeout_list));
	flush_workqueue(workq);
	destroy_workqueue(workq);
}
