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

#define DRV_NAME	"veth"
#define DRV_VERSION	"1.0"

#define VETH_XDP_FLAG		BIT(0)
#define VETH_RING_SIZE		256
#define VETH_XDP_HEADROOM	(XDP_PACKET_HEADROOM + NET_IP_ALIGN)

/* Separating two types of XDP xmit */
#define VETH_XDP_TX		BIT(0)
#define VETH_XDP_REDIR		BIT(1)

struct pcpu_vstats {
	u64			packets;
	u64			bytes;
	struct u64_stats_sync	syncp;
};

struct veth_rq {
	struct napi_struct	xdp_napi;
	struct net_device	*dev;
	struct bpf_prog __rcu	*xdp_prog;
	struct xdp_mem_info	xdp_mem;
	bool			rx_notify_masked;
	struct ptr_ring		xdp_ring;
	struct xdp_rxq_info	xdp_rxq;
};

struct veth_priv {
	struct net_device __rcu	*peer;
	atomic64_t		dropped;
	struct bpf_prog		*_xdp_prog;
	struct veth_rq		*rq;
	unsigned int		requested_headroom;
};

/*
 * ethtool interface
 */

static struct {
	const char string[ETH_GSTRING_LEN];
} ethtool_stats_keys[] = {
	{ "peer_ifindex" },
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
	strlcpy(info->driver, DRV_NAME, sizeof(info->driver));
	strlcpy(info->version, DRV_VERSION, sizeof(info->version));
}

static void veth_get_strings(struct net_device *dev, u32 stringset, u8 *buf)
{
	switch(stringset) {
	case ETH_SS_STATS:
		memcpy(buf, &ethtool_stats_keys, sizeof(ethtool_stats_keys));
		break;
	}
}

static int veth_get_sset_count(struct net_device *dev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return ARRAY_SIZE(ethtool_stats_keys);
	default:
		return -EOPNOTSUPP;
	}
}

static void veth_get_ethtool_stats(struct net_device *dev,
		struct ethtool_stats *stats, u64 *data)
{
	struct veth_priv *priv = netdev_priv(dev);
	struct net_device *peer = rtnl_dereference(priv->peer);

	data[0] = peer ? peer->ifindex : 0;
}

static const struct ethtool_ops veth_ethtool_ops = {
	.get_drvinfo		= veth_get_drvinfo,
	.get_link		= ethtool_op_get_link,
	.get_strings		= veth_get_strings,
	.get_sset_count		= veth_get_sset_count,
	.get_ethtool_stats	= veth_get_ethtool_stats,
	.get_link_ksettings	= veth_get_link_ksettings,
};

/* general routines */

static bool veth_is_xdp_frame(void *ptr)
{
	return (unsigned long)ptr & VETH_XDP_FLAG;
}

static void *veth_ptr_to_xdp(void *ptr)
{
	return (void *)((unsigned long)ptr & ~VETH_XDP_FLAG);
}

static void *veth_xdp_to_ptr(void *ptr)
{
	return (void *)((unsigned long)ptr | VETH_XDP_FLAG);
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
	if (!rq->rx_notify_masked) {
		rq->rx_notify_masked = true;
		napi_schedule(&rq->xdp_napi);
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
		netif_rx(skb);
}

static netdev_tx_t veth_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct veth_priv *rcv_priv, *priv = netdev_priv(dev);
	struct veth_rq *rq = NULL;
	struct net_device *rcv;
	int length = skb->len;
	bool rcv_xdp = false;
	int rxq;

	rcu_read_lock();
	rcv = rcu_dereference(priv->peer);
	if (unlikely(!rcv)) {
		kfree_skb(skb);
		goto drop;
	}

	rcv_priv = netdev_priv(rcv);
	rxq = skb_get_queue_mapping(skb);
	if (rxq < rcv->real_num_rx_queues) {
		rq = &rcv_priv->rq[rxq];
		rcv_xdp = rcu_access_pointer(rq->xdp_prog);
		if (rcv_xdp)
			skb_record_rx_queue(skb, rxq);
	}

	if (likely(veth_forward_skb(rcv, skb, rq, rcv_xdp) == NET_RX_SUCCESS)) {
		struct pcpu_vstats *stats = this_cpu_ptr(dev->vstats);

		u64_stats_update_begin(&stats->syncp);
		stats->bytes += length;
		stats->packets++;
		u64_stats_update_end(&stats->syncp);
	} else {
drop:
		atomic64_inc(&priv->dropped);
	}

	if (rcv_xdp)
		__veth_xdp_flush(rq);

	rcu_read_unlock();

	return NETDEV_TX_OK;
}

static u64 veth_stats_one(struct pcpu_vstats *result, struct net_device *dev)
{
	struct veth_priv *priv = netdev_priv(dev);
	int cpu;

	result->packets = 0;
	result->bytes = 0;
	for_each_possible_cpu(cpu) {
		struct pcpu_vstats *stats = per_cpu_ptr(dev->vstats, cpu);
		u64 packets, bytes;
		unsigned int start;

		do {
			start = u64_stats_fetch_begin_irq(&stats->syncp);
			packets = stats->packets;
			bytes = stats->bytes;
		} while (u64_stats_fetch_retry_irq(&stats->syncp, start));
		result->packets += packets;
		result->bytes += bytes;
	}
	return atomic64_read(&priv->dropped);
}

static void veth_get_stats64(struct net_device *dev,
			     struct rtnl_link_stats64 *tot)
{
	struct veth_priv *priv = netdev_priv(dev);
	struct net_device *peer;
	struct pcpu_vstats one;

	tot->tx_dropped = veth_stats_one(&one, dev);
	tot->tx_bytes = one.bytes;
	tot->tx_packets = one.packets;

	rcu_read_lock();
	peer = rcu_dereference(priv->peer);
	if (peer) {
		tot->rx_dropped = veth_stats_one(&one, peer);
		tot->rx_bytes = one.bytes;
		tot->rx_packets = one.packets;
	}
	rcu_read_unlock();
}

/* fake multicast ability */
static void veth_set_multicast_list(struct net_device *dev)
{
}

static struct sk_buff *veth_build_skb(void *head, int headroom, int len,
				      int buflen)
{
	struct sk_buff *skb;

	if (!buflen) {
		buflen = SKB_DATA_ALIGN(headroom + len) +
			 SKB_DATA_ALIGN(sizeof(struct skb_shared_info));
	}
	skb = build_skb(head, buflen);
	if (!skb)
		return NULL;

	skb_reserve(skb, headroom);
	skb_put(skb, len);

	return skb;
}

static int veth_select_rxq(struct net_device *dev)
{
	return smp_processor_id() % dev->real_num_rx_queues;
}

static int veth_xdp_xmit(struct net_device *dev, int n,
			 struct xdp_frame **frames, u32 flags)
{
	struct veth_priv *rcv_priv, *priv = netdev_priv(dev);
	struct net_device *rcv;
	unsigned int max_len;
	struct veth_rq *rq;
	int i, drops = 0;

	if (unlikely(flags & ~XDP_XMIT_FLAGS_MASK))
		return -EINVAL;

	rcv = rcu_dereference(priv->peer);
	if (unlikely(!rcv))
		return -ENXIO;

	rcv_priv = netdev_priv(rcv);
	rq = &rcv_priv->rq[veth_select_rxq(rcv)];
	/* Non-NULL xdp_prog ensures that xdp_ring is initialized on receive
	 * side. This means an XDP program is loaded on the peer and the peer
	 * device is up.
	 */
	if (!rcu_access_pointer(rq->xdp_prog))
		return -ENXIO;

	max_len = rcv->mtu + rcv->hard_header_len + VLAN_HLEN;

	spin_lock(&rq->xdp_ring.producer_lock);
	for (i = 0; i < n; i++) {
		struct xdp_frame *frame = frames[i];
		void *ptr = veth_xdp_to_ptr(frame);

		if (unlikely(frame->len > max_len ||
			     __ptr_ring_produce(&rq->xdp_ring, ptr))) {
			xdp_return_frame_rx_napi(frame);
			drops++;
		}
	}
	spin_unlock(&rq->xdp_ring.producer_lock);

	if (flags & XDP_XMIT_FLUSH)
		__veth_xdp_flush(rq);

	return n - drops;
}

static void veth_xdp_flush(struct net_device *dev)
{
	struct veth_priv *rcv_priv, *priv = netdev_priv(dev);
	struct net_device *rcv;
	struct veth_rq *rq;

	rcu_read_lock();
	rcv = rcu_dereference(priv->peer);
	if (unlikely(!rcv))
		goto out;

	rcv_priv = netdev_priv(rcv);
	rq = &rcv_priv->rq[veth_select_rxq(rcv)];
	/* xdp_ring is initialized on receive side? */
	if (unlikely(!rcu_access_pointer(rq->xdp_prog)))
		goto out;

	__veth_xdp_flush(rq);
out:
	rcu_read_unlock();
}

static int veth_xdp_tx(struct net_device *dev, struct xdp_buff *xdp)
{
	struct xdp_frame *frame = convert_to_xdp_frame(xdp);

	if (unlikely(!frame))
		return -EOVERFLOW;

	return veth_xdp_xmit(dev, 1, &frame, 0);
}

static struct sk_buff *veth_xdp_rcv_one(struct veth_rq *rq,
					struct xdp_frame *frame,
					unsigned int *xdp_xmit)
{
	void *hard_start = frame->data - frame->headroom;
	void *head = hard_start - sizeof(struct xdp_frame);
	int len = frame->len, delta = 0;
	struct xdp_frame orig_frame;
	struct bpf_prog *xdp_prog;
	unsigned int headroom;
	struct sk_buff *skb;

	rcu_read_lock();
	xdp_prog = rcu_dereference(rq->xdp_prog);
	if (likely(xdp_prog)) {
		struct xdp_buff xdp;
		u32 act;

		xdp.data_hard_start = hard_start;
		xdp.data = frame->data;
		xdp.data_end = frame->data + frame->len;
		xdp.data_meta = frame->data - frame->metasize;
		xdp.rxq = &rq->xdp_rxq;

		act = bpf_prog_run_xdp(xdp_prog, &xdp);

		switch (act) {
		case XDP_PASS:
			delta = frame->data - xdp.data;
			len = xdp.data_end - xdp.data;
			break;
		case XDP_TX:
			orig_frame = *frame;
			xdp.data_hard_start = head;
			xdp.rxq->mem = frame->mem;
			if (unlikely(veth_xdp_tx(rq->dev, &xdp) < 0)) {
				trace_xdp_exception(rq->dev, xdp_prog, act);
				frame = &orig_frame;
				goto err_xdp;
			}
			*xdp_xmit |= VETH_XDP_TX;
			rcu_read_unlock();
			goto xdp_xmit;
		case XDP_REDIRECT:
			orig_frame = *frame;
			xdp.data_hard_start = head;
			xdp.rxq->mem = frame->mem;
			if (xdp_do_redirect(rq->dev, &xdp, xdp_prog)) {
				frame = &orig_frame;
				goto err_xdp;
			}
			*xdp_xmit |= VETH_XDP_REDIR;
			rcu_read_unlock();
			goto xdp_xmit;
		default:
			bpf_warn_invalid_xdp_action(act);
		case XDP_ABORTED:
			trace_xdp_exception(rq->dev, xdp_prog, act);
		case XDP_DROP:
			goto err_xdp;
		}
	}
	rcu_read_unlock();

	headroom = sizeof(struct xdp_frame) + frame->headroom - delta;
	skb = veth_build_skb(head, headroom, len, 0);
	if (!skb) {
		xdp_return_frame(frame);
		goto err;
	}

	xdp_scrub_frame(frame);
	skb->protocol = eth_type_trans(skb, rq->dev);
err:
	return skb;
err_xdp:
	rcu_read_unlock();
	xdp_return_frame(frame);
xdp_xmit:
	return NULL;
}

static struct sk_buff *veth_xdp_rcv_skb(struct veth_rq *rq, struct sk_buff *skb,
					unsigned int *xdp_xmit)
{
	u32 pktlen, headroom, act, metalen;
	void *orig_data, *orig_data_end;
	struct bpf_prog *xdp_prog;
	int mac_len, delta, off;
	struct xdp_buff xdp;

	skb_orphan(skb);

	rcu_read_lock();
	xdp_prog = rcu_dereference(rq->xdp_prog);
	if (unlikely(!xdp_prog)) {
		rcu_read_unlock();
		goto out;
	}

	mac_len = skb->data - skb_mac_header(skb);
	pktlen = skb->len + mac_len;
	headroom = skb_headroom(skb) - mac_len;

	if (skb_shared(skb) || skb_head_is_locked(skb) ||
	    skb_is_nonlinear(skb) || headroom < XDP_PACKET_HEADROOM) {
		struct sk_buff *nskb;
		int size, head_off;
		void *head, *start;
		struct page *page;

		size = SKB_DATA_ALIGN(VETH_XDP_HEADROOM + pktlen) +
		       SKB_DATA_ALIGN(sizeof(struct skb_shared_info));
		if (size > PAGE_SIZE)
			goto drop;

		page = alloc_page(GFP_ATOMIC | __GFP_NOWARN);
		if (!page)
			goto drop;

		head = page_address(page);
		start = head + VETH_XDP_HEADROOM;
		if (skb_copy_bits(skb, -mac_len, start, pktlen)) {
			page_frag_free(head);
			goto drop;
		}

		nskb = veth_build_skb(head,
				      VETH_XDP_HEADROOM + mac_len, skb->len,
				      PAGE_SIZE);
		if (!nskb) {
			page_frag_free(head);
			goto drop;
		}

		skb_copy_header(nskb, skb);
		head_off = skb_headroom(nskb) - skb_headroom(skb);
		skb_headers_offset_update(nskb, head_off);
		consume_skb(skb);
		skb = nskb;
	}

	xdp.data_hard_start = skb->head;
	xdp.data = skb_mac_header(skb);
	xdp.data_end = xdp.data + pktlen;
	xdp.data_meta = xdp.data;
	xdp.rxq = &rq->xdp_rxq;
	orig_data = xdp.data;
	orig_data_end = xdp.data_end;

	act = bpf_prog_run_xdp(xdp_prog, &xdp);

	switch (act) {
	case XDP_PASS:
		break;
	case XDP_TX:
		get_page(virt_to_page(xdp.data));
		consume_skb(skb);
		xdp.rxq->mem = rq->xdp_mem;
		if (unlikely(veth_xdp_tx(rq->dev, &xdp) < 0)) {
			trace_xdp_exception(rq->dev, xdp_prog, act);
			goto err_xdp;
		}
		*xdp_xmit |= VETH_XDP_TX;
		rcu_read_unlock();
		goto xdp_xmit;
	case XDP_REDIRECT:
		get_page(virt_to_page(xdp.data));
		consume_skb(skb);
		xdp.rxq->mem = rq->xdp_mem;
		if (xdp_do_redirect(rq->dev, &xdp, xdp_prog))
			goto err_xdp;
		*xdp_xmit |= VETH_XDP_REDIR;
		rcu_read_unlock();
		goto xdp_xmit;
	default:
		bpf_warn_invalid_xdp_action(act);
	case XDP_ABORTED:
		trace_xdp_exception(rq->dev, xdp_prog, act);
	case XDP_DROP:
		goto drop;
	}
	rcu_read_unlock();

	delta = orig_data - xdp.data;
	off = mac_len + delta;
	if (off > 0)
		__skb_push(skb, off);
	else if (off < 0)
		__skb_pull(skb, -off);
	skb->mac_header -= delta;
	off = xdp.data_end - orig_data_end;
	if (off != 0)
		__skb_put(skb, off);
	skb->protocol = eth_type_trans(skb, rq->dev);

	metalen = xdp.data - xdp.data_meta;
	if (metalen)
		skb_metadata_set(skb, metalen);
out:
	return skb;
drop:
	rcu_read_unlock();
	kfree_skb(skb);
	return NULL;
err_xdp:
	rcu_read_unlock();
	page_frag_free(xdp.data);
xdp_xmit:
	return NULL;
}

static int veth_xdp_rcv(struct veth_rq *rq, int budget, unsigned int *xdp_xmit)
{
	int i, done = 0;

	for (i = 0; i < budget; i++) {
		void *ptr = __ptr_ring_consume(&rq->xdp_ring);
		struct sk_buff *skb;

		if (!ptr)
			break;

		if (veth_is_xdp_frame(ptr)) {
			skb = veth_xdp_rcv_one(rq, veth_ptr_to_xdp(ptr),
					       xdp_xmit);
		} else {
			skb = veth_xdp_rcv_skb(rq, ptr, xdp_xmit);
		}

		if (skb)
			napi_gro_receive(&rq->xdp_napi, skb);

		done++;
	}

	return done;
}

static int veth_poll(struct napi_struct *napi, int budget)
{
	struct veth_rq *rq =
		container_of(napi, struct veth_rq, xdp_napi);
	unsigned int xdp_xmit = 0;
	int done;

	xdp_set_return_frame_no_direct();
	done = veth_xdp_rcv(rq, budget, &xdp_xmit);

	if (done < budget && napi_complete_done(napi, done)) {
		/* Write rx_notify_masked before reading ptr_ring */
		smp_store_mb(rq->rx_notify_masked, false);
		if (unlikely(!__ptr_ring_empty(&rq->xdp_ring))) {
			rq->rx_notify_masked = true;
			napi_schedule(&rq->xdp_napi);
		}
	}

	if (xdp_xmit & VETH_XDP_TX)
		veth_xdp_flush(rq->dev);
	if (xdp_xmit & VETH_XDP_REDIR)
		xdp_do_flush_map();
	xdp_clear_return_frame_no_direct();

	return done;
}

static int veth_napi_add(struct net_device *dev)
{
	struct veth_priv *priv = netdev_priv(dev);
	int err, i;

	for (i = 0; i < dev->real_num_rx_queues; i++) {
		struct veth_rq *rq = &priv->rq[i];

		err = ptr_ring_init(&rq->xdp_ring, VETH_RING_SIZE, GFP_KERNEL);
		if (err)
			goto err_xdp_ring;
	}

	for (i = 0; i < dev->real_num_rx_queues; i++) {
		struct veth_rq *rq = &priv->rq[i];

		netif_napi_add(dev, &rq->xdp_napi, veth_poll, NAPI_POLL_WEIGHT);
		napi_enable(&rq->xdp_napi);
	}

	return 0;
err_xdp_ring:
	for (i--; i >= 0; i--)
		ptr_ring_cleanup(&priv->rq[i].xdp_ring, veth_ptr_free);

	return err;
}

static void veth_napi_del(struct net_device *dev)
{
	struct veth_priv *priv = netdev_priv(dev);
	int i;

	for (i = 0; i < dev->real_num_rx_queues; i++) {
		struct veth_rq *rq = &priv->rq[i];

		napi_disable(&rq->xdp_napi);
		napi_hash_del(&rq->xdp_napi);
	}
	synchronize_net();

	for (i = 0; i < dev->real_num_rx_queues; i++) {
		struct veth_rq *rq = &priv->rq[i];

		netif_napi_del(&rq->xdp_napi);
		rq->rx_notify_masked = false;
		ptr_ring_cleanup(&rq->xdp_ring, veth_ptr_free);
	}
}

static int veth_enable_xdp(struct net_device *dev)
{
	struct veth_priv *priv = netdev_priv(dev);
	int err, i;

	if (!xdp_rxq_info_is_reg(&priv->rq[0].xdp_rxq)) {
		for (i = 0; i < dev->real_num_rx_queues; i++) {
			struct veth_rq *rq = &priv->rq[i];

			err = xdp_rxq_info_reg(&rq->xdp_rxq, dev, i);
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

		err = veth_napi_add(dev);
		if (err)
			goto err_rxq_reg;
	}

	for (i = 0; i < dev->real_num_rx_queues; i++)
		rcu_assign_pointer(priv->rq[i].xdp_prog, priv->_xdp_prog);

	return 0;
err_reg_mem:
	xdp_rxq_info_unreg(&priv->rq[i].xdp_rxq);
err_rxq_reg:
	for (i--; i >= 0; i--)
		xdp_rxq_info_unreg(&priv->rq[i].xdp_rxq);

	return err;
}

static void veth_disable_xdp(struct net_device *dev)
{
	struct veth_priv *priv = netdev_priv(dev);
	int i;

	for (i = 0; i < dev->real_num_rx_queues; i++)
		rcu_assign_pointer(priv->rq[i].xdp_prog, NULL);
	veth_napi_del(dev);
	for (i = 0; i < dev->real_num_rx_queues; i++) {
		struct veth_rq *rq = &priv->rq[i];

		rq->xdp_rxq.mem = rq->xdp_mem;
		xdp_rxq_info_unreg(&rq->xdp_rxq);
	}
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

	priv->rq = kcalloc(dev->num_rx_queues, sizeof(*priv->rq), GFP_KERNEL);
	if (!priv->rq)
		return -ENOMEM;

	for (i = 0; i < dev->num_rx_queues; i++)
		priv->rq[i].dev = dev;

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

	dev->vstats = netdev_alloc_pcpu_stats(struct pcpu_vstats);
	if (!dev->vstats)
		return -ENOMEM;

	err = veth_alloc_queues(dev);
	if (err) {
		free_percpu(dev->vstats);
		return err;
	}

	return 0;
}

static void veth_dev_free(struct net_device *dev)
{
	veth_free_queues(dev);
	free_percpu(dev->vstats);
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

	return features;
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

		max_mtu = PAGE_SIZE - VETH_XDP_HEADROOM -
			  peer->hard_header_len -
			  SKB_DATA_ALIGN(sizeof(struct skb_shared_info));
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
	}

	if (old_prog) {
		if (!prog) {
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

static u32 veth_xdp_query(struct net_device *dev)
{
	struct veth_priv *priv = netdev_priv(dev);
	const struct bpf_prog *xdp_prog;

	xdp_prog = priv->_xdp_prog;
	if (xdp_prog)
		return xdp_prog->aux->id;

	return 0;
}

static int veth_xdp(struct net_device *dev, struct netdev_bpf *xdp)
{
	switch (xdp->command) {
	case XDP_SETUP_PROG:
		return veth_xdp_set(dev, xdp->prog, xdp->extack);
	case XDP_QUERY_PROG:
		xdp->prog_id = veth_xdp_query(dev);
		return 0;
	default:
		return -EINVAL;
	}
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
	.ndo_features_check	= passthru_features_check,
	.ndo_set_rx_headroom	= veth_set_rx_headroom,
	.ndo_bpf		= veth_xdp,
	.ndo_xdp_xmit		= veth_xdp_xmit,
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
		nla_strlcpy(ifname, tbp[IFLA_IFNAME], IFNAMSIZ);
		name_assign_type = NET_NAME_USER;
	} else {
		snprintf(ifname, IFNAMSIZ, DRV_NAME "%%d");
		name_assign_type = NET_NAME_ENUM;
	}

	net = rtnl_link_get_net(src_net, tbp);
	if (IS_ERR(net))
		return PTR_ERR(net);

	peer = rtnl_create_link(net, ifname, name_assign_type,
				&veth_link_ops, tbp);
	if (IS_ERR(peer)) {
		put_net(net);
		return PTR_ERR(peer);
	}

	if (!ifmp || !tbp[IFLA_ADDRESS])
		eth_hw_addr_random(peer);

	if (ifmp && (dev->ifindex != 0))
		peer->ifindex = ifmp->ifi_index;

	peer->gso_max_size = dev->gso_max_size;
	peer->gso_max_segs = dev->gso_max_segs;

	err = register_netdevice(peer);
	put_net(net);
	net = NULL;
	if (err < 0)
		goto err_register_peer;

	netif_carrier_off(peer);

	err = rtnl_configure_link(peer, ifmp);
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
		nla_strlcpy(dev->name, tb[IFLA_IFNAME], IFNAMSIZ);
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

	priv = netdev_priv(peer);
	rcu_assign_pointer(priv->peer, dev);

	return 0;

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
