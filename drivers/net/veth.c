// SPDX-License-Identifier: GPL-2.0-only
/*
 *  drivers/net/veth.c
 *
 *  Copyright (C) 2007 OpenVZ http://openvz.org, SWsoft Inc
 *
 * Author: Pavel Emelianov <xemul@openvz.org>
 * Ethtool interface from: Eric W. Biederman <ebiederm@xmission.com>
 *
 */

#include <linux/netdevice.h>
#include <linux/slab.h>
#include <linux/ethtool.h>
#include <linux/etherdevice.h>
#include <linux/u64_stats_sync.h>

#include <net/rtnetlink.h>
#include <net/dst.h>
#include <net/xfrm.h>
#include <net/xdp.h>
#include <linux/veth.h>
#include <linux/module.h>
#include <linux/bpf.h>
#include <linux/filter.h>
#include <linux/ptr_ring.h>
#include <linux/bpf_trace.h>
#include <linux/net_tstamp.h>
#include <net/page_pool.h>

#define DRV_NAME	"veth"
#define DRV_VERSION	"1.0"

#define VETH_XDP_FLAG		BIT(0)
#define VETH_RING_SIZE		256
#define VETH_XDP_HEADROOM	(XDP_PACKET_HEADROOM + NET_IP_ALIGN)

#define VETH_XDP_TX_BULK_SIZE	16
#define VETH_XDP_BATCH		16

struct veth_stats {
	u64	rx_drops;
	/* xdp */
	u64	xdp_packets;
	u64	xdp_bytes;
	u64	xdp_redirect;
	u64	xdp_drops;
	u64	xdp_tx;
	u64	xdp_tx_err;
	u64	peer_tq_xdp_xmit;
	u64	peer_tq_xdp_xmit_err;
};

struct veth_rq_stats {
	struct veth_stats	vs;
	struct u64_stats_sync	syncp;
};

struct veth_rq {
	struct napi_struct	xdp_napi;
	struct napi_struct __rcu *napi; /* points to xdp_napi when the latter is initialized */
	struct net_device	*dev;
	struct bpf_prog __rcu	*xdp_prog;
	struct xdp_mem_info	xdp_mem;
	struct veth_rq_stats	stats;
	bool			rx_notify_masked;
	struct ptr_ring		xdp_ring;
	struct xdp_rxq_info	xdp_rxq;
	struct page_pool	*page_pool;
};

struct veth_priv {
	struct net_device __rcu	*peer;
	atomic64_t		dropped;
	struct bpf_prog		*_xdp_prog;
	struct veth_rq		*rq;
	unsigned int		requested_headroom;
};

struct veth_xdp_tx_bq {
	struct xdp_frame *q[VETH_XDP_TX_BULK_SIZE];
	unsigned int count;
};

/*
 * ethtool interface
 */

struct veth_q_stat_desc {
	char	desc[ETH_GSTRING_LEN];
	size_t	offset;
};

#define VETH_RQ_STAT(m)	offsetof(struct veth_stats, m)

static const struct veth_q_stat_desc veth_rq_stats_desc[] = {
	{ "xdp_packets",	VETH_RQ_STAT(xdp_packets) },
	{ "xdp_bytes",		VETH_RQ_STAT(xdp_bytes) },
	{ "drops",		VETH_RQ_STAT(rx_drops) },
	{ "xdp_redirect",	VETH_RQ_STAT(xdp_redirect) },
	{ "xdp_drops",		VETH_RQ_STAT(xdp_drops) },
	{ "xdp_tx",		VETH_RQ_STAT(xdp_tx) },
	{ "xdp_tx_errors",	VETH_RQ_STAT(xdp_tx_err) },
};

#define VETH_RQ_STATS_LEN	ARRAY_SIZE(veth_rq_stats_desc)

static const struct veth_q_stat_desc veth_tq_stats_desc[] = {
	{ "xdp_xmit",		VETH_RQ_STAT(peer_tq_xdp_xmit) },
	{ "xdp_xmit_errors",	VETH_RQ_STAT(peer_tq_xdp_xmit_err) },
};

#define VETH_TQ_STATS_LEN	ARRAY_SIZE(veth_tq_stats_desc)

static struct {
	const char string[ETH_GSTRING_LEN];
} ethtool_stats_keys[] = {
	{ "peer_ifindex" },
};

struct veth_xdp_buff {
	struct xdp_buff xdp;
	struct sk_buff *skb;
};

static int veth_get_link_ksettings(struct net_device *dev,
				   struct ethtool_link_ksettings *cmd)
{
	cmd->base.speed		= SPEED_10000;
	cmd->base.duplex	= DUPLEX_FULL;
	cmd->base.port		= PORT_TP;
	cmd->base.autoneg	= AUTONEG_DISABLE;
	return 0;
}

static void veth_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *info)
{
	strscpy(info->driver, DRV_NAME, sizeof(info->driver));
	strscpy(info->version, DRV_VERSION, sizeof(info->version));
}

static void veth_get_strings(struct net_device *dev, u32 stringset, u8 *buf)
{
	u8 *p = buf;
	int i, j;

	switch(stringset) {
	case ETH_SS_STATS:
		memcpy(p, &ethtool_stats_keys, sizeof(ethtool_stats_keys));
		p += sizeof(ethtool_stats_keys);
		for (i = 0; i < dev->real_num_rx_queues; i++)
			for (j = 0; j < VETH_RQ_STATS_LEN; j++)
				ethtool_sprintf(&p, "rx_queue_%u_%.18s",
						i, veth_rq_stats_desc[j].desc);

		for (i = 0; i < dev->real_num_tx_queues; i++)
			for (j = 0; j < VETH_TQ_STATS_LEN; j++)
				ethtool_sprintf(&p, "tx_queue_%u_%.18s",
						i, veth_tq_stats_desc[j].desc);

		page_pool_ethtool_stats_get_strings(p);
		break;
	}
}

static int veth_get_sset_count(struct net_device *dev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return ARRAY_SIZE(ethtool_stats_keys) +
		       VETH_RQ_STATS_LEN * dev->real_num_rx_queues +
		       VETH_TQ_STATS_LEN * dev->real_num_tx_queues +
		       page_pool_ethtool_stats_get_count();
	default:
		return -EOPNOTSUPP;
	}
}

static void veth_get_ethtool_stats(struct net_device *dev,
		struct ethtool_stats *stats, u64 *data)
{
	struct veth_priv *rcv_priv, *priv = netdev_priv(dev);
	struct net_device *peer = rtnl_dereference(priv->peer);
	struct page_pool_stats pp_stats = {};
	int i, j, idx, pp_idx;

	data[0] = peer ? peer->ifindex : 0;
	idx = 1;
	for (i = 0; i < dev->real_num_rx_queues; i++) {
		const struct veth_rq_stats *rq_stats = &priv->rq[i].stats;
		const void *stats_base = (void *)&rq_stats->vs;
		unsigned int start;
		size_t offset;

		do {
			start = u64_stats_fetch_begin(&rq_stats->syncp);
			for (j = 0; j < VETH_RQ_STATS_LEN; j++) {
				offset = veth_rq_stats_desc[j].offset;
				data[idx + j] = *(u64 *)(stats_base + offset);
			}
		} while (u64_stats_fetch_retry(&rq_stats->syncp, start));
		idx += VETH_RQ_STATS_LEN;
	}
	pp_idx = idx;

	if (!peer)
		goto page_pool_stats;

	rcv_priv = netdev_priv(peer);
	for (i = 0; i < peer->real_num_rx_queues; i++) {
		const struct veth_rq_stats *rq_stats = &rcv_priv->rq[i].stats;
		const void *base = (void *)&rq_stats->vs;
		unsigned int start, tx_idx = idx;
		size_t offset;

		tx_idx += (i % dev->real_num_tx_queues) * VETH_TQ_STATS_LEN;
		do {
			start = u64_stats_fetch_begin(&rq_stats->syncp);
			for (j = 0; j < VETH_TQ_STATS_LEN; j++) {
				offset = veth_tq_stats_desc[j].offset;
				data[tx_idx + j] += *(u64 *)(base + offset);
			}
		} while (u64_stats_fetch_retry(&rq_stats->syncp, start));
		pp_idx = tx_idx + VETH_TQ_STATS_LEN;
	}

page_pool_stats:
	for (i = 0; i < dev->real_num_rx_queues; i++) {
		if (!priv->rq[i].page_pool)
			continue;
		page_pool_get_stats(priv->rq[i].page_pool, &pp_stats);
	}
	page_pool_ethtool_stats_get(&data[pp_idx], &pp_stats);
}

static void veth_get_channels(struct net_device *dev,
			      struct ethtool_channels *channels)
{
	channels->tx_count = dev->real_num_tx_queues;
	channels->rx_count = dev->real_num_rx_queues;
	channels->max_tx = dev->num_tx_queues;
	channels->max_rx = dev->num_rx_queues;
}

static int veth_set_channels(struct net_device *dev,
			     struct ethtool_channels *ch);

static const struct ethtool_ops veth_ethtool_ops = {
	.get_drvinfo		= veth_get_drvinfo,
	.get_link		= ethtool_op_get_link,
	.get_strings		= veth_get_strings,
	.get_sset_count		= veth_get_sset_count,
	.get_ethtool_stats	= veth_get_ethtool_stats,
	.get_link_ksettings	= veth_get_link_ksettings,
	.get_ts_info		= ethtool_op_get_ts_info,
	.get_channels		= veth_get_channels,
	.set_channels		= veth_set_channels,
};

/* general routines */

static bool veth_is_xdp_frame(void *ptr)
{
	return (unsigned long)ptr & VETH_XDP_FLAG;
}

static struct xdp_frame *veth_ptr_to_xdp(void *ptr)
{
	return (void *)((unsigned long)ptr & ~VETH_XDP_FLAG);
}

static void *veth_xdp_to_ptr(struct xdp_frame *xdp)
{
	return (void *)((unsigned long)xdp | VETH_XDP_FLAG);
}

static void veth_ptr_free(void *ptr)
{
	if (veth_is_xdp_frame(ptr))
		xdp_return_frame(veth_ptr_to_xdp(ptr));
	else
		kfree_skb(ptr);
}

static void __veth_xdp_flush(struct veth_rq *rq)
{
	/* Write ptr_ring before reading rx_notify_masked */
	smp_mb();
	if (!READ_ONCE(rq->rx_notify_masked) &&
	    napi_schedule_prep(&rq->xdp_napi)) {
		WRITE_ONCE(rq->rx_notify_masked, true);
		__napi_schedule(&rq->xdp_napi);
	}
}

static int veth_xdp_rx(struct veth_rq *rq, struct sk_buff *skb)
{
	if (unlikely(ptr_ring_produce(&rq->xdp_ring, skb))) {
		dev_kfree_skb_any(skb);
		return NET_RX_DROP;
	}

	return NET_RX_SUCCESS;
}

static int veth_forward_skb(struct net_device *dev, struct sk_buff *skb,
			    struct veth_rq *rq, bool xdp)
{
	return __dev_forward_skb(dev, skb) ?: xdp ?
		veth_xdp_rx(rq, skb) :
		__netif_rx(skb);
}

/* return true if the specified skb has chances of GRO aggregation
 * Don't strive for accuracy, but try to avoid GRO overhead in the most
 * common scenarios.
 * When XDP is enabled, all traffic is considered eligible, as the xmit
 * device has TSO off.
 * When TSO is enabled on the xmit device, we are likely interested only
 * in UDP aggregation, explicitly check for that if the skb is suspected
 * - the sock_wfree destructor is used by UDP, ICMP and XDP sockets -
 * to belong to locally generated UDP traffic.
 */
static bool veth_skb_is_eligible_for_gro(const struct net_device *dev,
					 const struct net_device *rcv,
					 const struct sk_buff *skb)
{
	return !(dev->features & NETIF_F_ALL_TSO) ||
		(skb->destructor == sock_wfree &&
		 rcv->features & (NETIF_F_GRO_FRAGLIST | NETIF_F_GRO_UDP_FWD));
}

static netdev_tx_t veth_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct veth_priv *rcv_priv, *priv = netdev_priv(dev);
	struct veth_rq *rq = NULL;
	struct net_device *rcv;
	int length = skb->len;
	bool use_napi = false;
	int rxq;

	rcu_read_lock();
	rcv = rcu_dereference(priv->peer);
	if (unlikely(!rcv) || !pskb_may_pull(skb, ETH_HLEN)) {
		kfree_skb(skb);
		goto drop;
	}

	rcv_priv = netdev_priv(rcv);
	rxq = skb_get_queue_mapping(skb);
	if (rxq < rcv->real_num_rx_queues) {
		rq = &rcv_priv->rq[rxq];

		/* The napi pointer is available when an XDP program is
		 * attached or when GRO is enabled
		 * Don't bother with napi/GRO if the skb can't be aggregated
		 */
		use_napi = rcu_access_pointer(rq->napi) &&
			   veth_skb_is_eligible_for_gro(dev, rcv, skb);
	}

	skb_tx_timestamp(skb);
	if (likely(veth_forward_skb(rcv, skb, rq, use_napi) == NET_RX_SUCCESS)) {
		if (!use_napi)
			dev_lstats_add(dev, length);
	} else {
drop:
		atomic64_inc(&priv->dropped);
	}

	if (use_napi)
		__veth_xdp_flush(rq);

	rcu_read_unlock();

	return NETDEV_TX_OK;
}

static u64 veth_stats_tx(struct net_device *dev, u64 *packets, u64 *bytes)
{
	struct veth_priv *priv = netdev_priv(dev);

	dev_lstats_read(dev, packets, bytes);
	return atomic64_read(&priv->dropped);
}

static void veth_stats_rx(struct veth_stats *result, struct net_device *dev)
{
	struct veth_priv *priv = netdev_priv(dev);
	int i;

	result->peer_tq_xdp_xmit_err = 0;
	result->xdp_packets = 0;
	result->xdp_tx_err = 0;
	result->xdp_bytes = 0;
	result->rx_drops = 0;
	for (i = 0; i < dev->num_rx_queues; i++) {
		u64 packets, bytes, drops, xdp_tx_err, peer_tq_xdp_xmit_err;
		struct veth_rq_stats *stats = &priv->rq[i].stats;
		unsigned int start;

		do {
			start = u64_stats_fetch_begin(&stats->syncp);
			peer_tq_xdp_xmit_err = stats->vs.peer_tq_xdp_xmit_err;
			xdp_tx_err = stats->vs.xdp_tx_err;
			packets = stats->vs.xdp_packets;
			bytes = stats->vs.xdp_bytes;
			drops = stats->vs.rx_drops;
		} while (u64_stats_fetch_retry(&stats->syncp, start));
		result->peer_tq_xdp_xmit_err += peer_tq_xdp_xmit_err;
		result->xdp_tx_err += xdp_tx_err;
		result->xdp_packets += packets;
		result->xdp_bytes += bytes;
		result->rx_drops += drops;
	}
}

static void veth_get_stats64(struct net_device *dev,
			     struct rtnl_link_stats64 *tot)
{
	struct veth_priv *priv = netdev_priv(dev);
	struct net_device *peer;
	struct veth_stats rx;
	u64 packets, bytes;

	tot->tx_dropped = veth_stats_tx(dev, &packets, &bytes);
	tot->tx_bytes = bytes;
	tot->tx_packets = packets;

	veth_stats_rx(&rx, dev);
	tot->tx_dropped += rx.xdp_tx_err;
	tot->rx_dropped = rx.rx_drops + rx.peer_tq_xdp_xmit_err;
	tot->rx_bytes = rx.xdp_bytes;
	tot->rx_packets = rx.xdp_packets;

	rcu_read_lock();
	peer = rcu_dereference(priv->peer);
	if (peer) {
		veth_stats_tx(peer, &packets, &bytes);
		tot->rx_bytes += bytes;
		tot->rx_packets += packets;

		veth_stats_rx(&rx, peer);
		tot->tx_dropped += rx.peer_tq_xdp_xmit_err;
		tot->rx_dropped += rx.xdp_tx_err;
		tot->tx_bytes += rx.xdp_bytes;
		tot->tx_packets += rx.xdp_packets;
	}
	rcu_read_unlock();
}

/* fake multicast ability */
static void veth_set_multicast_list(struct net_device *dev)
{
}

static int veth_select_rxq(struct net_device *dev)
{
	return smp_processor_id() % dev->real_num_rx_queues;
}

static struct net_device *veth_peer_dev(struct net_device *dev)
{
	struct veth_priv *priv = netdev_priv(dev);

	/* Callers must be under RCU read side. */
	return rcu_dereference(priv->peer);
}

static int veth_xdp_xmit(struct net_device *dev, int n,
			 struct xdp_frame **frames,
			 u32 flags, bool ndo_xmit)
{
	struct veth_priv *rcv_priv, *priv = netdev_priv(dev);
	int i, ret = -ENXIO, nxmit = 0;
	struct net_device *rcv;
	unsigned int max_len;
	struct veth_rq *rq;

	if (unlikely(flags & ~XDP_XMIT_FLAGS_MASK))
		return -EINVAL;

	rcu_read_lock();
	rcv = rcu_dereference(priv->peer);
	if (unlikely(!rcv))
		goto out;

	rcv_priv = netdev_priv(rcv);
	rq = &rcv_priv->rq[veth_select_rxq(rcv)];
	/* The napi pointer is set if NAPI is enabled, which ensures that
	 * xdp_ring is initialized on receive side and the peer device is up.
	 */
	if (!rcu_access_pointer(rq->napi))
		goto out;

	max_len = rcv->mtu + rcv->hard_header_len + VLAN_HLEN;

	spin_lock(&rq->xdp_ring.producer_lock);
	for (i = 0; i < n; i++) {
		struct xdp_frame *frame = frames[i];
		void *ptr = veth_xdp_to_ptr(frame);

		if (unlikely(xdp_get_frame_len(frame) > max_len ||
			     __ptr_ring_produce(&rq->xdp_ring, ptr)))
			break;
		nxmit++;
	}
	spin_unlock(&rq->xdp_ring.producer_lock);

	if (flags & XDP_XMIT_FLUSH)
		__veth_xdp_flush(rq);

	ret = nxmit;
	if (ndo_xmit) {
		u64_stats_update_begin(&rq->stats.syncp);
		rq->stats.vs.peer_tq_xdp_xmit += nxmit;
		rq->stats.vs.peer_tq_xdp_xmit_err += n - nxmit;
		u64_stats_update_end(&rq->stats.syncp);
	}

out:
	rcu_read_unlock();

	return ret;
}

static int veth_ndo_xdp_xmit(struct net_device *dev, int n,
			     struct xdp_frame **frames, u32 flags)
{
	int err;

	err = veth_xdp_xmit(dev, n, frames, flags, true);
	if (err < 0) {
		struct veth_priv *priv = netdev_priv(dev);

		atomic64_add(n, &priv->dropped);
	}

	return err;
}

static void veth_xdp_flush_bq(struct veth_rq *rq, struct veth_xdp_tx_bq *bq)
{
	int sent, i, err = 0, drops;

	sent = veth_xdp_xmit(rq->dev, bq->count, bq->q, 0, false);
	if (sent < 0) {
		err = sent;
		sent = 0;
	}

	for (i = sent; unlikely(i < bq->count); i++)
		xdp_return_frame(bq->q[i]);

	drops = bq->count - sent;
	trace_xdp_bulk_tx(rq->dev, sent, drops, err);

	u64_stats_update_begin(&rq->stats.syncp);
	rq->stats.vs.xdp_tx += sent;
	rq->stats.vs.xdp_tx_err += drops;
	u64_stats_update_end(&rq->stats.syncp);

	bq->count = 0;
}

static void veth_xdp_flush(struct veth_rq *rq, struct veth_xdp_tx_bq *bq)
{
	struct veth_priv *rcv_priv, *priv = netdev_priv(rq->dev);
	struct net_device *rcv;
	struct veth_rq *rcv_rq;

	rcu_read_lock();
	veth_xdp_flush_bq(rq, bq);
	rcv = rcu_dereference(priv->peer);
	if (unlikely(!rcv))
		goto out;

	rcv_priv = netdev_priv(rcv);
	rcv_rq = &rcv_priv->rq[veth_select_rxq(rcv)];
	/* xdp_ring is initialized on receive side? */
	if (unlikely(!rcu_access_pointer(rcv_rq->xdp_prog)))
		goto out;

	__veth_xdp_flush(rcv_rq);
out:
	rcu_read_unlock();
}

static int veth_xdp_tx(struct veth_rq *rq, struct xdp_buff *xdp,
		       struct veth_xdp_tx_bq *bq)
{
	struct xdp_frame *frame = xdp_convert_buff_to_frame(xdp);

	if (unlikely(!frame))
		return -EOVERFLOW;

	if (unlikely(bq->count == VETH_XDP_TX_BULK_SIZE))
		veth_xdp_flush_bq(rq, bq);

	bq->q[bq->count++] = frame;

	return 0;
}

static struct xdp_frame *veth_xdp_rcv_one(struct veth_rq *rq,
					  struct xdp_frame *frame,
					  struct veth_xdp_tx_bq *bq,
					  struct veth_stats *stats)
{
	struct xdp_frame orig_frame;
	struct bpf_prog *xdp_prog;

	rcu_read_lock();
	xdp_prog = rcu_dereference(rq->xdp_prog);
	if (likely(xdp_prog)) {
		struct veth_xdp_buff vxbuf;
		struct xdp_buff *xdp = &vxbuf.xdp;
		u32 act;

		xdp_convert_frame_to_buff(frame, xdp);
		xdp->rxq = &rq->xdp_rxq;
		vxbuf.skb = NULL;

		act = bpf_prog_run_xdp(xdp_prog, xdp);

		switch (act) {
		case XDP_PASS:
			if (xdp_update_frame_from_buff(xdp, frame))
				goto err_xdp;
			break;
		case XDP_TX:
			orig_frame = *frame;
			xdp->rxq->mem = frame->mem;
			if (unlikely(veth_xdp_tx(rq, xdp, bq) < 0)) {
				trace_xdp_exception(rq->dev, xdp_prog, act);
				frame = &orig_frame;
				stats->rx_drops++;
				goto err_xdp;
			}
			stats->xdp_tx++;
			rcu_read_unlock();
			goto xdp_xmit;
		case XDP_REDIRECT:
			orig_frame = *frame;
			xdp->rxq->mem = frame->mem;
			if (xdp_do_redirect(rq->dev, xdp, xdp_prog)) {
				frame = &orig_frame;
				stats->rx_drops++;
				goto err_xdp;
			}
			stats->xdp_redirect++;
			rcu_read_unlock();
			goto xdp_xmit;
		default:
			bpf_warn_invalid_xdp_action(rq->dev, xdp_prog, act);
			fallthrough;
		case XDP_ABORTED:
			trace_xdp_exception(rq->dev, xdp_prog, act);
			fallthrough;
		case XDP_DROP:
			stats->xdp_drops++;
			goto err_xdp;
		}
	}
	rcu_read_unlock();

	return frame;
err_xdp:
	rcu_read_unlock();
	xdp_return_frame(frame);
xdp_xmit:
	return NULL;
}

/* frames array contains VETH_XDP_BATCH at most */
static void veth_xdp_rcv_bulk_skb(struct veth_rq *rq, void **frames,
				  int n_xdpf, struct veth_xdp_tx_bq *bq,
				  struct veth_stats *stats)
{
	void *skbs[VETH_XDP_BATCH];
	int i;

	if (xdp_alloc_skb_bulk(skbs, n_xdpf,
			       GFP_ATOMIC | __GFP_ZERO) < 0) {
		for (i = 0; i < n_xdpf; i++)
			xdp_return_frame(frames[i]);
		stats->rx_drops += n_xdpf;

		return;
	}

	for (i = 0; i < n_xdpf; i++) {
		struct sk_buff *skb = skbs[i];

		skb = __xdp_build_skb_from_frame(frames[i], skb,
						 rq->dev);
		if (!skb) {
			xdp_return_frame(frames[i]);
			stats->rx_drops++;
			continue;
		}
		napi_gro_receive(&rq->xdp_napi, skb);
	}
}

static void veth_xdp_get(struct xdp_buff *xdp)
{
	struct skb_shared_info *sinfo = xdp_get_shared_info_from_buff(xdp);
	int i;

	get_page(virt_to_page(xdp->data));
	if (likely(!xdp_buff_has_frags(xdp)))
		return;

	for (i = 0; i < sinfo->nr_frags; i++)
		__skb_frag_ref(&sinfo->frags[i]);
}

static int veth_convert_skb_to_xdp_buff(struct veth_rq *rq,
					struct xdp_buff *xdp,
					struct sk_buff **pskb)
{
	struct sk_buff *skb = *pskb;
	u32 frame_sz;

	if (skb_shared(skb) || skb_head_is_locked(skb) ||
	    skb_shinfo(skb)->nr_frags ||
	    skb_headroom(skb) < XDP_PACKET_HEADROOM) {
		u32 size, len, max_head_size, off;
		struct sk_buff *nskb;
		struct page *page;
		int i, head_off;

		/* We need a private copy of the skb and data buffers since
		 * the ebpf program can modify it. We segment the original skb
		 * into order-0 pages without linearize it.
		 *
		 * Make sure we have enough space for linear and paged area
		 */
		max_head_size = SKB_WITH_OVERHEAD(PAGE_SIZE -
						  VETH_XDP_HEADROOM);
		if (skb->len > PAGE_SIZE * MAX_SKB_FRAGS + max_head_size)
			goto drop;

		/* Allocate skb head */
		page = page_pool_dev_alloc_pages(rq->page_pool);
		if (!page)
			goto drop;

		nskb = build_skb(page_address(page), PAGE_SIZE);
		if (!nskb) {
			page_pool_put_full_page(rq->page_pool, page, true);
			goto drop;
		}

		skb_reserve(nskb, VETH_XDP_HEADROOM);
		skb_copy_header(nskb, skb);
		skb_mark_for_recycle(nskb);

		size = min_t(u32, skb->len, max_head_size);
		if (skb_copy_bits(skb, 0, nskb->data, size)) {
			consume_skb(nskb);
			goto drop;
		}
		skb_put(nskb, size);

		head_off = skb_headroom(nskb) - skb_headroom(skb);
		skb_headers_offset_update(nskb, head_off);

		/* Allocate paged area of new skb */
		off = size;
		len = skb->len - off;

		for (i = 0; i < MAX_SKB_FRAGS && off < skb->len; i++) {
			page = page_pool_dev_alloc_pages(rq->page_pool);
			if (!page) {
				consume_skb(nskb);
				goto drop;
			}

			size = min_t(u32, len, PAGE_SIZE);
			skb_add_rx_frag(nskb, i, page, 0, size, PAGE_SIZE);
			if (skb_copy_bits(skb, off, page_address(page),
					  size)) {
				consume_skb(nskb);
				goto drop;
			}

			len -= size;
			off += size;
		}

		consume_skb(skb);
		skb = nskb;
	}

	/* SKB "head" area always have tailroom for skb_shared_info */
	frame_sz = skb_end_pointer(skb) - skb->head;
	frame_sz += SKB_DATA_ALIGN(sizeof(struct skb_shared_info));
	xdp_init_buff(xdp, frame_sz, &rq->xdp_rxq);
	xdp_prepare_buff(xdp, skb->head, skb_headroom(skb),
			 skb_headlen(skb), true);

	if (skb_is_nonlinear(skb)) {
		skb_shinfo(skb)->xdp_frags_size = skb->data_len;
		xdp_buff_set_frags_flag(xdp);
	} else {
		xdp_buff_clear_frags_flag(xdp);
	}
	*pskb = skb;

	return 0;
drop:
	consume_skb(skb);
	*pskb = NULL;

	return -ENOMEM;
}

static struct sk_buff *veth_xdp_rcv_skb(struct veth_rq *rq,
					struct sk_buff *skb,
					struct veth_xdp_tx_bq *bq,
					struct veth_stats *stats)
{
	void *orig_data, *orig_data_end;
	struct bpf_prog *xdp_prog;
	struct veth_xdp_buff vxbuf;
	struct xdp_buff *xdp = &vxbuf.xdp;
	u32 act, metalen;
	int off;

	skb_prepare_for_gro(skb);

	rcu_read_lock();
	xdp_prog = rcu_dereference(rq->xdp_prog);
	if (unlikely(!xdp_prog)) {
		rcu_read_unlock();
		goto out;
	}

	__skb_push(skb, skb->data - skb_mac_header(skb));
	if (veth_convert_skb_to_xdp_buff(rq, xdp, &skb))
		goto drop;
	vxbuf.skb = skb;

	orig_data = xdp->data;
	orig_data_end = xdp->data_end;

	act = bpf_prog_run_xdp(xdp_prog, xdp);

	switch (act) {
	case XDP_PASS:
		break;
	case XDP_TX:
		veth_xdp_get(xdp);
		consume_skb(skb);
		xdp->rxq->mem = rq->xdp_mem;
		if (unlikely(veth_xdp_tx(rq, xdp, bq) < 0)) {
			trace_xdp_exception(rq->dev, xdp_prog, act);
			stats->rx_drops++;
			goto err_xdp;
		}
		stats->xdp_tx++;
		rcu_read_unlock();
		goto xdp_xmit;
	case XDP_REDIRECT:
		veth_xdp_get(xdp);
		consume_skb(skb);
		xdp->rxq->mem = rq->xdp_mem;
		if (xdp_do_redirect(rq->dev, xdp, xdp_prog)) {
			stats->rx_drops++;
			goto err_xdp;
		}
		stats->xdp_redirect++;
		rcu_read_unlock();
		goto xdp_xmit;
	default:
		bpf_warn_invalid_xdp_action(rq->dev, xdp_prog, act);
		fallthrough;
	case XDP_ABORTED:
		trace_xdp_exception(rq->dev, xdp_prog, act);
		fallthrough;
	case XDP_DROP:
		stats->xdp_drops++;
		goto xdp_drop;
	}
	rcu_read_unlock();

	/* check if bpf_xdp_adjust_head was used */
	off = orig_data - xdp->data;
	if (off > 0)
		__skb_push(skb, off);
	else if (off < 0)
		__skb_pull(skb, -off);

	skb_reset_mac_header(skb);

	/* check if bpf_xdp_adjust_tail was used */
	off = xdp->data_end - orig_data_end;
	if (off != 0)
		__skb_put(skb, off); /* positive on grow, negative on shrink */

	/* XDP frag metadata (e.g. nr_frags) are updated in eBPF helpers
	 * (e.g. bpf_xdp_adjust_tail), we need to update data_len here.
	 */
	if (xdp_buff_has_frags(xdp))
		skb->data_len = skb_shinfo(skb)->xdp_frags_size;
	else
		skb->data_len = 0;

	skb->protocol = eth_type_trans(skb, rq->dev);

	metalen = xdp->data - xdp->data_meta;
	if (metalen)
		skb_metadata_set(skb, metalen);
out:
	return skb;
drop:
	stats->rx_drops++;
xdp_drop:
	rcu_read_unlock();
	kfree_skb(skb);
	return NULL;
err_xdp:
	rcu_read_unlock();
	xdp_return_buff(xdp);
xdp_xmit:
	return NULL;
}

static int veth_xdp_rcv(struct veth_rq *rq, int budget,
			struct veth_xdp_tx_bq *bq,
			struct veth_stats *stats)
{
	int i, done = 0, n_xdpf = 0;
	void *xdpf[VETH_XDP_BATCH];

	for (i = 0; i < budget; i++) {
		void *ptr = __ptr_ring_consume(&rq->xdp_ring);

		if (!ptr)
			break;

		if (veth_is_xdp_frame(ptr)) {
			/* ndo_xdp_xmit */
			struct xdp_frame *frame = veth_ptr_to_xdp(ptr);

			stats->xdp_bytes += xdp_get_frame_len(frame);
			frame = veth_xdp_rcv_one(rq, frame, bq, stats);
			if (frame) {
				/* XDP_PASS */
				xdpf[n_xdpf++] = frame;
				if (n_xdpf == VETH_XDP_BATCH) {
					veth_xdp_rcv_bulk_skb(rq, xdpf, n_xdpf,
							      bq, stats);
					n_xdpf = 0;
				}
			}
		} else {
			/* ndo_start_xmit */
			struct sk_buff *skb = ptr;

			stats->xdp_bytes += skb->len;
			skb = veth_xdp_rcv_skb(rq, skb, bq, stats);
			if (skb) {
				if (skb_shared(skb) || skb_unclone(skb, GFP_ATOMIC))
					netif_receive_skb(skb);
				else
					napi_gro_receive(&rq->xdp_napi, skb);
			}
		}
		done++;
	}

	if (n_xdpf)
		veth_xdp_rcv_bulk_skb(rq, xdpf, n_xdpf, bq, stats);

	u64_stats_update_begin(&rq->stats.syncp);
	rq->stats.vs.xdp_redirect += stats->xdp_redirect;
	rq->stats.vs.xdp_bytes += stats->xdp_bytes;
	rq->stats.vs.xdp_drops += stats->xdp_drops;
	rq->stats.vs.rx_drops += stats->rx_drops;
	rq->stats.vs.xdp_packets += done;
	u64_stats_update_end(&rq->stats.syncp);

	return done;
}

static int veth_poll(struct napi_struct *napi, int budget)
{
	struct veth_rq *rq =
		container_of(napi, struct veth_rq, xdp_napi);
	struct veth_stats stats = {};
	struct veth_xdp_tx_bq bq;
	int done;

	bq.count = 0;

	xdp_set_return_frame_no_direct();
	done = veth_xdp_rcv(rq, budget, &bq, &stats);

	if (stats.xdp_redirect > 0)
		xdp_do_flush();

	if (done < budget && napi_complete_done(napi, done)) {
		/* Write rx_notify_masked before reading ptr_ring */
		smp_store_mb(rq->rx_notify_masked, false);
		if (unlikely(!__ptr_ring_empty(&rq->xdp_ring))) {
			if (napi_schedule_prep(&rq->xdp_napi)) {
				WRITE_ONCE(rq->rx_notify_masked, true);
				__napi_schedule(&rq->xdp_napi);
			}
		}
	}

	if (stats.xdp_tx > 0)
		veth_xdp_flush(rq, &bq);
	xdp_clear_return_frame_no_direct();

	return done;
}

static int veth_create_page_pool(struct veth_rq *rq)
{
	struct page_pool_params pp_params = {
		.order = 0,
		.pool_size = VETH_RING_SIZE,
		.nid = NUMA_NO_NODE,
		.dev = &rq->dev->dev,
	};

	rq->page_pool = page_pool_create(&pp_params);
	if (IS_ERR(rq->page_pool)) {
		int err = PTR_ERR(rq->page_pool);

		rq->page_pool = NULL;
		return err;
	}

	return 0;
}

static int __veth_napi_enable_range(struct net_device *dev, int start, int end)
{
	struct veth_priv *priv = netdev_priv(dev);
	int err, i;

	for (i = start; i < end; i++) {
		err = veth_create_page_pool(&priv->rq[i]);
		if (err)
			goto err_page_pool;
	}

	for (i = start; i < end; i++) {
		struct veth_rq *rq = &priv->rq[i];

		err = ptr_ring_init(&rq->xdp_ring, VETH_RING_SIZE, GFP_KERNEL);
		if (err)
			goto err_xdp_ring;
	}

	for (i = start; i < end; i++) {
		struct veth_rq *rq = &priv->rq[i];

		napi_enable(&rq->xdp_napi);
		rcu_assign_pointer(priv->rq[i].napi, &priv->rq[i].xdp_napi);
	}

	return 0;

err_xdp_ring:
	for (i--; i >= start; i--)
		ptr_ring_cleanup(&priv->rq[i].xdp_ring, veth_ptr_free);
err_page_pool:
	for (i = start; i < end; i++) {
		page_pool_destroy(priv->rq[i].page_pool);
		priv->rq[i].page_pool = NULL;
	}

	return err;
}

static int __veth_napi_enable(struct net_device *dev)
{
	return __veth_napi_enable_range(dev, 0, dev->real_num_rx_queues);
}

static void veth_napi_del_range(struct net_device *dev, int start, int end)
{
	struct veth_priv *priv = netdev_priv(dev);
	int i;

	for (i = start; i < end; i++) {
		struct veth_rq *rq = &priv->rq[i];

		rcu_assign_pointer(priv->rq[i].napi, NULL);
		napi_disable(&rq->xdp_napi);
		__netif_napi_del(&rq->xdp_napi);
	}
	synchronize_net();

	for (i = start; i < end; i++) {
		struct veth_rq *rq = &priv->rq[i];

		rq->rx_notify_masked = false;
		ptr_ring_cleanup(&rq->xdp_ring, veth_ptr_free);
	}

	for (i = start; i < end; i++) {
		page_pool_destroy(priv->rq[i].page_pool);
		priv->rq[i].page_pool = NULL;
	}
}

static void veth_napi_del(struct net_device *dev)
{
	veth_napi_del_range(dev, 0, dev->real_num_rx_queues);
}

static bool veth_gro_requested(const struct net_device *dev)
{
	return !!(dev->wanted_features & NETIF_F_GRO);
}

static int veth_enable_xdp_range(struct net_device *dev, int start, int end,
				 bool napi_already_on)
{
	struct veth_priv *priv = netdev_priv(dev);
	int err, i;

	for (i = start; i < end; i++) {
		struct veth_rq *rq = &priv->rq[i];

		if (!napi_already_on)
			netif_napi_add(dev, &rq->xdp_napi, veth_poll);
		err = xdp_rxq_info_reg(&rq->xdp_rxq, dev, i, rq->xdp_napi.napi_id);
		if (err < 0)
			goto err_rxq_reg;

		err = xdp_rxq_info_reg_mem_model(&rq->xdp_rxq,
						 MEM_TYPE_PAGE_SHARED,
						 NULL);
		if (err < 0)
			goto err_reg_mem;

		/* Save original mem info as it can be overwritten */
		rq->xdp_mem = rq->xdp_rxq.mem;
	}
	return 0;

err_reg_mem:
	xdp_rxq_info_unreg(&priv->rq[i].xdp_rxq);
err_rxq_reg:
	for (i--; i >= start; i--) {
		struct veth_rq *rq = &priv->rq[i];

		xdp_rxq_info_unreg(&rq->xdp_rxq);
		if (!napi_already_on)
			netif_napi_del(&rq->xdp_napi);
	}

	return err;
}

static void veth_disable_xdp_range(struct net_device *dev, int start, int end,
				   bool delete_napi)
{
	struct veth_priv *priv = netdev_priv(dev);
	int i;

	for (i = start; i < end; i++) {
		struct veth_rq *rq = &priv->rq[i];

		rq->xdp_rxq.mem = rq->xdp_mem;
		xdp_rxq_info_unreg(&rq->xdp_rxq);

		if (delete_napi)
			netif_napi_del(&rq->xdp_napi);
	}
}

static int veth_enable_xdp(struct net_device *dev)
{
	bool napi_already_on = veth_gro_requested(dev) && (dev->flags & IFF_UP);
	struct veth_priv *priv = netdev_priv(dev);
	int err, i;

	if (!xdp_rxq_info_is_reg(&priv->rq[0].xdp_rxq)) {
		err = veth_enable_xdp_range(dev, 0, dev->real_num_rx_queues, napi_already_on);
		if (err)
			return err;

		if (!napi_already_on) {
			err = __veth_napi_enable(dev);
			if (err) {
				veth_disable_xdp_range(dev, 0, dev->real_num_rx_queues, true);
				return err;
			}

			if (!veth_gro_requested(dev)) {
				/* user-space did not require GRO, but adding XDP
				 * is supposed to get GRO working
				 */
				dev->features |= NETIF_F_GRO;
				netdev_features_change(dev);
			}
		}
	}

	for (i = 0; i < dev->real_num_rx_queues; i++) {
		rcu_assign_pointer(priv->rq[i].xdp_prog, priv->_xdp_prog);
		rcu_assign_pointer(priv->rq[i].napi, &priv->rq[i].xdp_napi);
	}

	return 0;
}

static void veth_disable_xdp(struct net_device *dev)
{
	struct veth_priv *priv = netdev_priv(dev);
	int i;

	for (i = 0; i < dev->real_num_rx_queues; i++)
		rcu_assign_pointer(priv->rq[i].xdp_prog, NULL);

	if (!netif_running(dev) || !veth_gro_requested(dev)) {
		veth_napi_del(dev);

		/* if user-space did not require GRO, since adding XDP
		 * enabled it, clear it now
		 */
		if (!veth_gro_requested(dev) && netif_running(dev)) {
			dev->features &= ~NETIF_F_GRO;
			netdev_features_change(dev);
		}
	}

	veth_disable_xdp_range(dev, 0, dev->real_num_rx_queues, false);
}

static int veth_napi_enable_range(struct net_device *dev, int start, int end)
{
	struct veth_priv *priv = netdev_priv(dev);
	int err, i;

	for (i = start; i < end; i++) {
		struct veth_rq *rq = &priv->rq[i];

		netif_napi_add(dev, &rq->xdp_napi, veth_poll);
	}

	err = __veth_napi_enable_range(dev, start, end);
	if (err) {
		for (i = start; i < end; i++) {
			struct veth_rq *rq = &priv->rq[i];

			netif_napi_del(&rq->xdp_napi);
		}
		return err;
	}
	return err;
}

static int veth_napi_enable(struct net_device *dev)
{
	return veth_napi_enable_range(dev, 0, dev->real_num_rx_queues);
}

static void veth_disable_range_safe(struct net_device *dev, int start, int end)
{
	struct veth_priv *priv = netdev_priv(dev);

	if (start >= end)
		return;

	if (priv->_xdp_prog) {
		veth_napi_del_range(dev, start, end);
		veth_disable_xdp_range(dev, start, end, false);
	} else if (veth_gro_requested(dev)) {
		veth_napi_del_range(dev, start, end);
	}
}

static int veth_enable_range_safe(struct net_device *dev, int start, int end)
{
	struct veth_priv *priv = netdev_priv(dev);
	int err;

	if (start >= end)
		return 0;

	if (priv->_xdp_prog) {
		/* these channels are freshly initialized, napi is not on there even
		 * when GRO is requeste
		 */
		err = veth_enable_xdp_range(dev, start, end, false);
		if (err)
			return err;

		err = __veth_napi_enable_range(dev, start, end);
		if (err) {
			/* on error always delete the newly added napis */
			veth_disable_xdp_range(dev, start, end, true);
			return err;
		}
	} else if (veth_gro_requested(dev)) {
		return veth_napi_enable_range(dev, start, end);
	}
	return 0;
}

static void veth_set_xdp_features(struct net_device *dev)
{
	struct veth_priv *priv = netdev_priv(dev);
	struct net_device *peer;

	peer = rtnl_dereference(priv->peer);
	if (peer && peer->real_num_tx_queues <= dev->real_num_rx_queues) {
		struct veth_priv *priv_peer = netdev_priv(peer);
		xdp_features_t val = NETDEV_XDP_ACT_BASIC |
				     NETDEV_XDP_ACT_REDIRECT |
				     NETDEV_XDP_ACT_RX_SG;

		if (priv_peer->_xdp_prog || veth_gro_requested(peer))
			val |= NETDEV_XDP_ACT_NDO_XMIT |
			       NETDEV_XDP_ACT_NDO_XMIT_SG;
		xdp_set_features_flag(dev, val);
	} else {
		xdp_clear_features_flag(dev);
	}
}

static int veth_set_channels(struct net_device *dev,
			     struct ethtool_channels *ch)
{
	struct veth_priv *priv = netdev_priv(dev);
	unsigned int old_rx_count, new_rx_count;
	struct veth_priv *peer_priv;
	struct net_device *peer;
	int err;

	/* sanity check. Upper bounds are already enforced by the caller */
	if (!ch->rx_count || !ch->tx_count)
		return -EINVAL;

	/* avoid braking XDP, if that is enabled */
	peer = rtnl_dereference(priv->peer);
	peer_priv = peer ? netdev_priv(peer) : NULL;
	if (priv->_xdp_prog && peer && ch->rx_count < peer->real_num_tx_queues)
		return -EINVAL;

	if (peer && peer_priv && peer_priv->_xdp_prog && ch->tx_count > peer->real_num_rx_queues)
		return -EINVAL;

	old_rx_count = dev->real_num_rx_queues;
	new_rx_count = ch->rx_count;
	if (netif_running(dev)) {
		/* turn device off */
		netif_carrier_off(dev);
		if (peer)
			netif_carrier_off(peer);

		/* try to allocate new resurces, as needed*/
		err = veth_enable_range_safe(dev, old_rx_count, new_rx_count);
		if (err)
			goto out;
	}

	err = netif_set_real_num_rx_queues(dev, ch->rx_count);
	if (err)
		goto revert;

	err = netif_set_real_num_tx_queues(dev, ch->tx_count);
	if (err) {
		int err2 = netif_set_real_num_rx_queues(dev, old_rx_count);

		/* this error condition could happen only if rx and tx change
		 * in opposite directions (e.g. tx nr raises, rx nr decreases)
		 * and we can't do anything to fully restore the original
		 * status
		 */
		if (err2)
			pr_warn("Can't restore rx queues config %d -> %d %d",
				new_rx_count, old_rx_count, err2);
		else
			goto revert;
	}

out:
	if (netif_running(dev)) {
		/* note that we need to swap the arguments WRT the enable part
		 * to identify the range we have to disable
		 */
		veth_disable_range_safe(dev, new_rx_count, old_rx_count);
		netif_carrier_on(dev);
		if (peer)
			netif_carrier_on(peer);
	}

	/* update XDP supported features */
	veth_set_xdp_features(dev);
	if (peer)
		veth_set_xdp_features(peer);

	return err;

revert:
	new_rx_count = old_rx_count;
	old_rx_count = ch->rx_count;
	goto out;
}

static int veth_open(struct net_device *dev)
{
	struct veth_priv *priv = netdev_priv(dev);
	struct net_device *peer = rtnl_dereference(priv->peer);
	int err;

	if (!peer)
		return -ENOTCONN;

	if (priv->_xdp_prog) {
		err = veth_enable_xdp(dev);
		if (err)
			return err;
	} else if (veth_gro_requested(dev)) {
		err = veth_napi_enable(dev);
		if (err)
			return err;
	}

	if (peer->flags & IFF_UP) {
		netif_carrier_on(dev);
		netif_carrier_on(peer);
	}

	return 0;
}

static int veth_close(struct net_device *dev)
{
	struct veth_priv *priv = netdev_priv(dev);
	struct net_device *peer = rtnl_dereference(priv->peer);

	netif_carrier_off(dev);
	if (peer)
		netif_carrier_off(peer);

	if (priv->_xdp_prog)
		veth_disable_xdp(dev);
	else if (veth_gro_requested(dev))
		veth_napi_del(dev);

	return 0;
}

static int is_valid_veth_mtu(int mtu)
{
	return mtu >= ETH_MIN_MTU && mtu <= ETH_MAX_MTU;
}

static int veth_alloc_queues(struct net_device *dev)
{
	struct veth_priv *priv = netdev_priv(dev);
	int i;

	priv->rq = kcalloc(dev->num_rx_queues, sizeof(*priv->rq), GFP_KERNEL_ACCOUNT);
	if (!priv->rq)
		return -ENOMEM;

	for (i = 0; i < dev->num_rx_queues; i++) {
		priv->rq[i].dev = dev;
		u64_stats_init(&priv->rq[i].stats.syncp);
	}

	return 0;
}

static void veth_free_queues(struct net_device *dev)
{
	struct veth_priv *priv = netdev_priv(dev);

	kfree(priv->rq);
}

static int veth_dev_init(struct net_device *dev)
{
	int err;

	dev->lstats = netdev_alloc_pcpu_stats(struct pcpu_lstats);
	if (!dev->lstats)
		return -ENOMEM;

	err = veth_alloc_queues(dev);
	if (err) {
		free_percpu(dev->lstats);
		return err;
	}

	return 0;
}

static void veth_dev_free(struct net_device *dev)
{
	veth_free_queues(dev);
	free_percpu(dev->lstats);
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void veth_poll_controller(struct net_device *dev)
{
	/* veth only receives frames when its peer sends one
	 * Since it has nothing to do with disabling irqs, we are guaranteed
	 * never to have pending data when we poll for it so
	 * there is nothing to do here.
	 *
	 * We need this though so netpoll recognizes us as an interface that
	 * supports polling, which enables bridge devices in virt setups to
	 * still use netconsole
	 */
}
#endif	/* CONFIG_NET_POLL_CONTROLLER */

static int veth_get_iflink(const struct net_device *dev)
{
	struct veth_priv *priv = netdev_priv(dev);
	struct net_device *peer;
	int iflink;

	rcu_read_lock();
	peer = rcu_dereference(priv->peer);
	iflink = peer ? peer->ifindex : 0;
	rcu_read_unlock();

	return iflink;
}

static netdev_features_t veth_fix_features(struct net_device *dev,
					   netdev_features_t features)
{
	struct veth_priv *priv = netdev_priv(dev);
	struct net_device *peer;

	peer = rtnl_dereference(priv->peer);
	if (peer) {
		struct veth_priv *peer_priv = netdev_priv(peer);

		if (peer_priv->_xdp_prog)
			features &= ~NETIF_F_GSO_SOFTWARE;
	}
	if (priv->_xdp_prog)
		features |= NETIF_F_GRO;

	return features;
}

static int veth_set_features(struct net_device *dev,
			     netdev_features_t features)
{
	netdev_features_t changed = features ^ dev->features;
	struct veth_priv *priv = netdev_priv(dev);
	struct net_device *peer;
	int err;

	if (!(changed & NETIF_F_GRO) || !(dev->flags & IFF_UP) || priv->_xdp_prog)
		return 0;

	peer = rtnl_dereference(priv->peer);
	if (features & NETIF_F_GRO) {
		err = veth_napi_enable(dev);
		if (err)
			return err;

		if (peer)
			xdp_features_set_redirect_target(peer, true);
	} else {
		if (peer)
			xdp_features_clear_redirect_target(peer);
		veth_napi_del(dev);
	}
	return 0;
}

static void veth_set_rx_headroom(struct net_device *dev, int new_hr)
{
	struct veth_priv *peer_priv, *priv = netdev_priv(dev);
	struct net_device *peer;

	if (new_hr < 0)
		new_hr = 0;

	rcu_read_lock();
	peer = rcu_dereference(priv->peer);
	if (unlikely(!peer))
		goto out;

	peer_priv = netdev_priv(peer);
	priv->requested_headroom = new_hr;
	new_hr = max(priv->requested_headroom, peer_priv->requested_headroom);
	dev->needed_headroom = new_hr;
	peer->needed_headroom = new_hr;

out:
	rcu_read_unlock();
}

static int veth_xdp_set(struct net_device *dev, struct bpf_prog *prog,
			struct netlink_ext_ack *extack)
{
	struct veth_priv *priv = netdev_priv(dev);
	struct bpf_prog *old_prog;
	struct net_device *peer;
	unsigned int max_mtu;
	int err;

	old_prog = priv->_xdp_prog;
	priv->_xdp_prog = prog;
	peer = rtnl_dereference(priv->peer);

	if (prog) {
		if (!peer) {
			NL_SET_ERR_MSG_MOD(extack, "Cannot set XDP when peer is detached");
			err = -ENOTCONN;
			goto err;
		}

		max_mtu = SKB_WITH_OVERHEAD(PAGE_SIZE - VETH_XDP_HEADROOM) -
			  peer->hard_header_len;
		/* Allow increasing the max_mtu if the program supports
		 * XDP fragments.
		 */
		if (prog->aux->xdp_has_frags)
			max_mtu += PAGE_SIZE * MAX_SKB_FRAGS;

		if (peer->mtu > max_mtu) {
			NL_SET_ERR_MSG_MOD(extack, "Peer MTU is too large to set XDP");
			err = -ERANGE;
			goto err;
		}

		if (dev->real_num_rx_queues < peer->real_num_tx_queues) {
			NL_SET_ERR_MSG_MOD(extack, "XDP expects number of rx queues not less than peer tx queues");
			err = -ENOSPC;
			goto err;
		}

		if (dev->flags & IFF_UP) {
			err = veth_enable_xdp(dev);
			if (err) {
				NL_SET_ERR_MSG_MOD(extack, "Setup for XDP failed");
				goto err;
			}
		}

		if (!old_prog) {
			peer->hw_features &= ~NETIF_F_GSO_SOFTWARE;
			peer->max_mtu = max_mtu;
		}

		xdp_features_set_redirect_target(peer, true);
	}

	if (old_prog) {
		if (!prog) {
			if (peer && !veth_gro_requested(dev))
				xdp_features_clear_redirect_target(peer);

			if (dev->flags & IFF_UP)
				veth_disable_xdp(dev);

			if (peer) {
				peer->hw_features |= NETIF_F_GSO_SOFTWARE;
				peer->max_mtu = ETH_MAX_MTU;
			}
		}
		bpf_prog_put(old_prog);
	}

	if ((!!old_prog ^ !!prog) && peer)
		netdev_update_features(peer);

	return 0;
err:
	priv->_xdp_prog = old_prog;

	return err;
}

static int veth_xdp(struct net_device *dev, struct netdev_bpf *xdp)
{
	switch (xdp->command) {
	case XDP_SETUP_PROG:
		return veth_xdp_set(dev, xdp->prog, xdp->extack);
	default:
		return -EINVAL;
	}
}

static int veth_xdp_rx_timestamp(const struct xdp_md *ctx, u64 *timestamp)
{
	struct veth_xdp_buff *_ctx = (void *)ctx;

	if (!_ctx->skb)
		return -ENODATA;

	*timestamp = skb_hwtstamps(_ctx->skb)->hwtstamp;
	return 0;
}

static int veth_xdp_rx_hash(const struct xdp_md *ctx, u32 *hash,
			    enum xdp_rss_hash_type *rss_type)
{
	struct veth_xdp_buff *_ctx = (void *)ctx;
	struct sk_buff *skb = _ctx->skb;

	if (!skb)
		return -ENODATA;

	*hash = skb_get_hash(skb);
	*rss_type = skb->l4_hash ? XDP_RSS_TYPE_L4_ANY : XDP_RSS_TYPE_NONE;

	return 0;
}

static const struct net_device_ops veth_netdev_ops = {
	.ndo_init            = veth_dev_init,
	.ndo_open            = veth_open,
	.ndo_stop            = veth_close,
	.ndo_start_xmit      = veth_xmit,
	.ndo_get_stats64     = veth_get_stats64,
	.ndo_set_rx_mode     = veth_set_multicast_list,
	.ndo_set_mac_address = eth_mac_addr,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= veth_poll_controller,
#endif
	.ndo_get_iflink		= veth_get_iflink,
	.ndo_fix_features	= veth_fix_features,
	.ndo_set_features	= veth_set_features,
	.ndo_features_check	= passthru_features_check,
	.ndo_set_rx_headroom	= veth_set_rx_headroom,
	.ndo_bpf		= veth_xdp,
	.ndo_xdp_xmit		= veth_ndo_xdp_xmit,
	.ndo_get_peer_dev	= veth_peer_dev,
};

static const struct xdp_metadata_ops veth_xdp_metadata_ops = {
	.xmo_rx_timestamp		= veth_xdp_rx_timestamp,
	.xmo_rx_hash			= veth_xdp_rx_hash,
};

#define VETH_FEATURES (NETIF_F_SG | NETIF_F_FRAGLIST | NETIF_F_HW_CSUM | \
		       NETIF_F_RXCSUM | NETIF_F_SCTP_CRC | NETIF_F_HIGHDMA | \
		       NETIF_F_GSO_SOFTWARE | NETIF_F_GSO_ENCAP_ALL | \
		       NETIF_F_HW_VLAN_CTAG_TX | NETIF_F_HW_VLAN_CTAG_RX | \
		       NETIF_F_HW_VLAN_STAG_TX | NETIF_F_HW_VLAN_STAG_RX )

static void veth_setup(struct net_device *dev)
{
	ether_setup(dev);

	dev->priv_flags &= ~IFF_TX_SKB_SHARING;
	dev->priv_flags |= IFF_LIVE_ADDR_CHANGE;
	dev->priv_flags |= IFF_NO_QUEUE;
	dev->priv_flags |= IFF_PHONY_HEADROOM;

	dev->netdev_ops = &veth_netdev_ops;
	dev->xdp_metadata_ops = &veth_xdp_metadata_ops;
	dev->ethtool_ops = &veth_ethtool_ops;
	dev->features |= NETIF_F_LLTX;
	dev->features |= VETH_FEATURES;
	dev->vlan_features = dev->features &
			     ~(NETIF_F_HW_VLAN_CTAG_TX |
			       NETIF_F_HW_VLAN_STAG_TX |
			       NETIF_F_HW_VLAN_CTAG_RX |
			       NETIF_F_HW_VLAN_STAG_RX);
	dev->needs_free_netdev = true;
	dev->priv_destructor = veth_dev_free;
	dev->max_mtu = ETH_MAX_MTU;

	dev->hw_features = VETH_FEATURES;
	dev->hw_enc_features = VETH_FEATURES;
	dev->mpls_features = NETIF_F_HW_CSUM | NETIF_F_GSO_SOFTWARE;
	netif_set_tso_max_size(dev, GSO_MAX_SIZE);
}

/*
 * netlink interface
 */

static int veth_validate(struct nlattr *tb[], struct nlattr *data[],
			 struct netlink_ext_ack *extack)
{
	if (tb[IFLA_ADDRESS]) {
		if (nla_len(tb[IFLA_ADDRESS]) != ETH_ALEN)
			return -EINVAL;
		if (!is_valid_ether_addr(nla_data(tb[IFLA_ADDRESS])))
			return -EADDRNOTAVAIL;
	}
	if (tb[IFLA_MTU]) {
		if (!is_valid_veth_mtu(nla_get_u32(tb[IFLA_MTU])))
			return -EINVAL;
	}
	return 0;
}

static struct rtnl_link_ops veth_link_ops;

static void veth_disable_gro(struct net_device *dev)
{
	dev->features &= ~NETIF_F_GRO;
	dev->wanted_features &= ~NETIF_F_GRO;
	netdev_update_features(dev);
}

static int veth_init_queues(struct net_device *dev, struct nlattr *tb[])
{
	int err;

	if (!tb[IFLA_NUM_TX_QUEUES] && dev->num_tx_queues > 1) {
		err = netif_set_real_num_tx_queues(dev, 1);
		if (err)
			return err;
	}
	if (!tb[IFLA_NUM_RX_QUEUES] && dev->num_rx_queues > 1) {
		err = netif_set_real_num_rx_queues(dev, 1);
		if (err)
			return err;
	}
	return 0;
}

static int veth_newlink(struct net *src_net, struct net_device *dev,
			struct nlattr *tb[], struct nlattr *data[],
			struct netlink_ext_ack *extack)
{
	int err;
	struct net_device *peer;
	struct veth_priv *priv;
	char ifname[IFNAMSIZ];
	struct nlattr *peer_tb[IFLA_MAX + 1], **tbp;
	unsigned char name_assign_type;
	struct ifinfomsg *ifmp;
	struct net *net;

	/*
	 * create and register peer first
	 */
	if (data != NULL && data[VETH_INFO_PEER] != NULL) {
		struct nlattr *nla_peer;

		nla_peer = data[VETH_INFO_PEER];
		ifmp = nla_data(nla_peer);
		err = rtnl_nla_parse_ifla(peer_tb,
					  nla_data(nla_peer) + sizeof(struct ifinfomsg),
					  nla_len(nla_peer) - sizeof(struct ifinfomsg),
					  NULL);
		if (err < 0)
			return err;

		err = veth_validate(peer_tb, NULL, extack);
		if (err < 0)
			return err;

		tbp = peer_tb;
	} else {
		ifmp = NULL;
		tbp = tb;
	}

	if (ifmp && tbp[IFLA_IFNAME]) {
		nla_strscpy(ifname, tbp[IFLA_IFNAME], IFNAMSIZ);
		name_assign_type = NET_NAME_USER;
	} else {
		snprintf(ifname, IFNAMSIZ, DRV_NAME "%%d");
		name_assign_type = NET_NAME_ENUM;
	}

	net = rtnl_link_get_net(src_net, tbp);
	if (IS_ERR(net))
		return PTR_ERR(net);

	peer = rtnl_create_link(net, ifname, name_assign_type,
				&veth_link_ops, tbp, extack);
	if (IS_ERR(peer)) {
		put_net(net);
		return PTR_ERR(peer);
	}

	if (!ifmp || !tbp[IFLA_ADDRESS])
		eth_hw_addr_random(peer);

	if (ifmp && (dev->ifindex != 0))
		peer->ifindex = ifmp->ifi_index;

	netif_inherit_tso_max(peer, dev);

	err = register_netdevice(peer);
	put_net(net);
	net = NULL;
	if (err < 0)
		goto err_register_peer;

	/* keep GRO disabled by default to be consistent with the established
	 * veth behavior
	 */
	veth_disable_gro(peer);
	netif_carrier_off(peer);

	err = rtnl_configure_link(peer, ifmp, 0, NULL);
	if (err < 0)
		goto err_configure_peer;

	/*
	 * register dev last
	 *
	 * note, that since we've registered new device the dev's name
	 * should be re-allocated
	 */

	if (tb[IFLA_ADDRESS] == NULL)
		eth_hw_addr_random(dev);

	if (tb[IFLA_IFNAME])
		nla_strscpy(dev->name, tb[IFLA_IFNAME], IFNAMSIZ);
	else
		snprintf(dev->name, IFNAMSIZ, DRV_NAME "%%d");

	err = register_netdevice(dev);
	if (err < 0)
		goto err_register_dev;

	netif_carrier_off(dev);

	/*
	 * tie the deviced together
	 */

	priv = netdev_priv(dev);
	rcu_assign_pointer(priv->peer, peer);
	err = veth_init_queues(dev, tb);
	if (err)
		goto err_queues;

	priv = netdev_priv(peer);
	rcu_assign_pointer(priv->peer, dev);
	err = veth_init_queues(peer, tb);
	if (err)
		goto err_queues;

	veth_disable_gro(dev);
	/* update XDP supported features */
	veth_set_xdp_features(dev);
	veth_set_xdp_features(peer);

	return 0;

err_queues:
	unregister_netdevice(dev);
err_register_dev:
	/* nothing to do */
err_configure_peer:
	unregister_netdevice(peer);
	return err;

err_register_peer:
	free_netdev(peer);
	return err;
}

static void veth_dellink(struct net_device *dev, struct list_head *head)
{
	struct veth_priv *priv;
	struct net_device *peer;

	priv = netdev_priv(dev);
	peer = rtnl_dereference(priv->peer);

	/* Note : dellink() is called from default_device_exit_batch(),
	 * before a rcu_synchronize() point. The devices are guaranteed
	 * not being freed before one RCU grace period.
	 */
	RCU_INIT_POINTER(priv->peer, NULL);
	unregister_netdevice_queue(dev, head);

	if (peer) {
		priv = netdev_priv(peer);
		RCU_INIT_POINTER(priv->peer, NULL);
		unregister_netdevice_queue(peer, head);
	}
}

static const struct nla_policy veth_policy[VETH_INFO_MAX + 1] = {
	[VETH_INFO_PEER]	= { .len = sizeof(struct ifinfomsg) },
};

static struct net *veth_get_link_net(const struct net_device *dev)
{
	struct veth_priv *priv = netdev_priv(dev);
	struct net_device *peer = rtnl_dereference(priv->peer);

	return peer ? dev_net(peer) : dev_net(dev);
}

static unsigned int veth_get_num_queues(void)
{
	/* enforce the same queue limit as rtnl_create_link */
	int queues = num_possible_cpus();

	if (queues > 4096)
		queues = 4096;
	return queues;
}

static struct rtnl_link_ops veth_link_ops = {
	.kind		= DRV_NAME,
	.priv_size	= sizeof(struct veth_priv),
	.setup		= veth_setup,
	.validate	= veth_validate,
	.newlink	= veth_newlink,
	.dellink	= veth_dellink,
	.policy		= veth_policy,
	.maxtype	= VETH_INFO_MAX,
	.get_link_net	= veth_get_link_net,
	.get_num_tx_queues	= veth_get_num_queues,
	.get_num_rx_queues	= veth_get_num_queues,
};

/*
 * init/fini
 */

static __init int veth_init(void)
{
	return rtnl_link_register(&veth_link_ops);
}

static __exit void veth_exit(void)
{
	rtnl_link_unregister(&veth_link_ops);
}

module_init(veth_init);
module_exit(veth_exit);

MODULE_DESCRIPTION("Virtual Ethernet Tunnel");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS_RTNL_LINK(DRV_NAME);
