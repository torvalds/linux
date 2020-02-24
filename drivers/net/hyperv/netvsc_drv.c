// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2009, Microsoft Corporation.
 *
 * Authors:
 *   Haiyang Zhang <haiyangz@microsoft.com>
 *   Hank Janssen  <hjanssen@microsoft.com>
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/atomic.h>
#include <linux/module.h>
#include <linux/highmem.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/etherdevice.h>
#include <linux/pci.h>
#include <linux/skbuff.h>
#include <linux/if_vlan.h>
#include <linux/in.h>
#include <linux/slab.h>
#include <linux/rtnetlink.h>
#include <linux/netpoll.h>
#include <linux/bpf.h>

#include <net/arp.h>
#include <net/route.h>
#include <net/sock.h>
#include <net/pkt_sched.h>
#include <net/checksum.h>
#include <net/ip6_checksum.h>

#include "hyperv_net.h"

#define RING_SIZE_MIN	64
#define RETRY_US_LO	5000
#define RETRY_US_HI	10000
#define RETRY_MAX	2000	/* >10 sec */

#define LINKCHANGE_INT (2 * HZ)
#define VF_TAKEOVER_INT (HZ / 10)

static unsigned int ring_size __ro_after_init = 128;
module_param(ring_size, uint, 0444);
MODULE_PARM_DESC(ring_size, "Ring buffer size (# of pages)");
unsigned int netvsc_ring_bytes __ro_after_init;

static const u32 default_msg = NETIF_MSG_DRV | NETIF_MSG_PROBE |
				NETIF_MSG_LINK | NETIF_MSG_IFUP |
				NETIF_MSG_IFDOWN | NETIF_MSG_RX_ERR |
				NETIF_MSG_TX_ERR;

static int debug = -1;
module_param(debug, int, 0444);
MODULE_PARM_DESC(debug, "Debug level (0=none,...,16=all)");

static LIST_HEAD(netvsc_dev_list);

static void netvsc_change_rx_flags(struct net_device *net, int change)
{
	struct net_device_context *ndev_ctx = netdev_priv(net);
	struct net_device *vf_netdev = rtnl_dereference(ndev_ctx->vf_netdev);
	int inc;

	if (!vf_netdev)
		return;

	if (change & IFF_PROMISC) {
		inc = (net->flags & IFF_PROMISC) ? 1 : -1;
		dev_set_promiscuity(vf_netdev, inc);
	}

	if (change & IFF_ALLMULTI) {
		inc = (net->flags & IFF_ALLMULTI) ? 1 : -1;
		dev_set_allmulti(vf_netdev, inc);
	}
}

static void netvsc_set_rx_mode(struct net_device *net)
{
	struct net_device_context *ndev_ctx = netdev_priv(net);
	struct net_device *vf_netdev;
	struct netvsc_device *nvdev;

	rcu_read_lock();
	vf_netdev = rcu_dereference(ndev_ctx->vf_netdev);
	if (vf_netdev) {
		dev_uc_sync(vf_netdev, net);
		dev_mc_sync(vf_netdev, net);
	}

	nvdev = rcu_dereference(ndev_ctx->nvdev);
	if (nvdev)
		rndis_filter_update(nvdev);
	rcu_read_unlock();
}

static void netvsc_tx_enable(struct netvsc_device *nvscdev,
			     struct net_device *ndev)
{
	nvscdev->tx_disable = false;
	virt_wmb(); /* ensure queue wake up mechanism is on */

	netif_tx_wake_all_queues(ndev);
}

static int netvsc_open(struct net_device *net)
{
	struct net_device_context *ndev_ctx = netdev_priv(net);
	struct net_device *vf_netdev = rtnl_dereference(ndev_ctx->vf_netdev);
	struct netvsc_device *nvdev = rtnl_dereference(ndev_ctx->nvdev);
	struct rndis_device *rdev;
	int ret = 0;

	netif_carrier_off(net);

	/* Open up the device */
	ret = rndis_filter_open(nvdev);
	if (ret != 0) {
		netdev_err(net, "unable to open device (ret %d).\n", ret);
		return ret;
	}

	rdev = nvdev->extension;
	if (!rdev->link_state) {
		netif_carrier_on(net);
		netvsc_tx_enable(nvdev, net);
	}

	if (vf_netdev) {
		/* Setting synthetic device up transparently sets
		 * slave as up. If open fails, then slave will be
		 * still be offline (and not used).
		 */
		ret = dev_open(vf_netdev, NULL);
		if (ret)
			netdev_warn(net,
				    "unable to open slave: %s: %d\n",
				    vf_netdev->name, ret);
	}
	return 0;
}

static int netvsc_wait_until_empty(struct netvsc_device *nvdev)
{
	unsigned int retry = 0;
	int i;

	/* Ensure pending bytes in ring are read */
	for (;;) {
		u32 aread = 0;

		for (i = 0; i < nvdev->num_chn; i++) {
			struct vmbus_channel *chn
				= nvdev->chan_table[i].channel;

			if (!chn)
				continue;

			/* make sure receive not running now */
			napi_synchronize(&nvdev->chan_table[i].napi);

			aread = hv_get_bytes_to_read(&chn->inbound);
			if (aread)
				break;

			aread = hv_get_bytes_to_read(&chn->outbound);
			if (aread)
				break;
		}

		if (aread == 0)
			return 0;

		if (++retry > RETRY_MAX)
			return -ETIMEDOUT;

		usleep_range(RETRY_US_LO, RETRY_US_HI);
	}
}

static void netvsc_tx_disable(struct netvsc_device *nvscdev,
			      struct net_device *ndev)
{
	if (nvscdev) {
		nvscdev->tx_disable = true;
		virt_wmb(); /* ensure txq will not wake up after stop */
	}

	netif_tx_disable(ndev);
}

static int netvsc_close(struct net_device *net)
{
	struct net_device_context *net_device_ctx = netdev_priv(net);
	struct net_device *vf_netdev
		= rtnl_dereference(net_device_ctx->vf_netdev);
	struct netvsc_device *nvdev = rtnl_dereference(net_device_ctx->nvdev);
	int ret;

	netvsc_tx_disable(nvdev, net);

	/* No need to close rndis filter if it is removed already */
	if (!nvdev)
		return 0;

	ret = rndis_filter_close(nvdev);
	if (ret != 0) {
		netdev_err(net, "unable to close device (ret %d).\n", ret);
		return ret;
	}

	ret = netvsc_wait_until_empty(nvdev);
	if (ret)
		netdev_err(net, "Ring buffer not empty after closing rndis\n");

	if (vf_netdev)
		dev_close(vf_netdev);

	return ret;
}

static inline void *init_ppi_data(struct rndis_message *msg,
				  u32 ppi_size, u32 pkt_type)
{
	struct rndis_packet *rndis_pkt = &msg->msg.pkt;
	struct rndis_per_packet_info *ppi;

	rndis_pkt->data_offset += ppi_size;
	ppi = (void *)rndis_pkt + rndis_pkt->per_pkt_info_offset
		+ rndis_pkt->per_pkt_info_len;

	ppi->size = ppi_size;
	ppi->type = pkt_type;
	ppi->internal = 0;
	ppi->ppi_offset = sizeof(struct rndis_per_packet_info);

	rndis_pkt->per_pkt_info_len += ppi_size;

	return ppi + 1;
}

/* Azure hosts don't support non-TCP port numbers in hashing for fragmented
 * packets. We can use ethtool to change UDP hash level when necessary.
 */
static inline u32 netvsc_get_hash(
	struct sk_buff *skb,
	const struct net_device_context *ndc)
{
	struct flow_keys flow;
	u32 hash, pkt_proto = 0;
	static u32 hashrnd __read_mostly;

	net_get_random_once(&hashrnd, sizeof(hashrnd));

	if (!skb_flow_dissect_flow_keys(skb, &flow, 0))
		return 0;

	switch (flow.basic.ip_proto) {
	case IPPROTO_TCP:
		if (flow.basic.n_proto == htons(ETH_P_IP))
			pkt_proto = HV_TCP4_L4HASH;
		else if (flow.basic.n_proto == htons(ETH_P_IPV6))
			pkt_proto = HV_TCP6_L4HASH;

		break;

	case IPPROTO_UDP:
		if (flow.basic.n_proto == htons(ETH_P_IP))
			pkt_proto = HV_UDP4_L4HASH;
		else if (flow.basic.n_proto == htons(ETH_P_IPV6))
			pkt_proto = HV_UDP6_L4HASH;

		break;
	}

	if (pkt_proto & ndc->l4_hash) {
		return skb_get_hash(skb);
	} else {
		if (flow.basic.n_proto == htons(ETH_P_IP))
			hash = jhash2((u32 *)&flow.addrs.v4addrs, 2, hashrnd);
		else if (flow.basic.n_proto == htons(ETH_P_IPV6))
			hash = jhash2((u32 *)&flow.addrs.v6addrs, 8, hashrnd);
		else
			return 0;

		__skb_set_sw_hash(skb, hash, false);
	}

	return hash;
}

static inline int netvsc_get_tx_queue(struct net_device *ndev,
				      struct sk_buff *skb, int old_idx)
{
	const struct net_device_context *ndc = netdev_priv(ndev);
	struct sock *sk = skb->sk;
	int q_idx;

	q_idx = ndc->tx_table[netvsc_get_hash(skb, ndc) &
			      (VRSS_SEND_TAB_SIZE - 1)];

	/* If queue index changed record the new value */
	if (q_idx != old_idx &&
	    sk && sk_fullsock(sk) && rcu_access_pointer(sk->sk_dst_cache))
		sk_tx_queue_set(sk, q_idx);

	return q_idx;
}

/*
 * Select queue for transmit.
 *
 * If a valid queue has already been assigned, then use that.
 * Otherwise compute tx queue based on hash and the send table.
 *
 * This is basically similar to default (netdev_pick_tx) with the added step
 * of using the host send_table when no other queue has been assigned.
 *
 * TODO support XPS - but get_xps_queue not exported
 */
static u16 netvsc_pick_tx(struct net_device *ndev, struct sk_buff *skb)
{
	int q_idx = sk_tx_queue_get(skb->sk);

	if (q_idx < 0 || skb->ooo_okay || q_idx >= ndev->real_num_tx_queues) {
		/* If forwarding a packet, we use the recorded queue when
		 * available for better cache locality.
		 */
		if (skb_rx_queue_recorded(skb))
			q_idx = skb_get_rx_queue(skb);
		else
			q_idx = netvsc_get_tx_queue(ndev, skb, q_idx);
	}

	return q_idx;
}

static u16 netvsc_select_queue(struct net_device *ndev, struct sk_buff *skb,
			       struct net_device *sb_dev)
{
	struct net_device_context *ndc = netdev_priv(ndev);
	struct net_device *vf_netdev;
	u16 txq;

	rcu_read_lock();
	vf_netdev = rcu_dereference(ndc->vf_netdev);
	if (vf_netdev) {
		const struct net_device_ops *vf_ops = vf_netdev->netdev_ops;

		if (vf_ops->ndo_select_queue)
			txq = vf_ops->ndo_select_queue(vf_netdev, skb, sb_dev);
		else
			txq = netdev_pick_tx(vf_netdev, skb, NULL);

		/* Record the queue selected by VF so that it can be
		 * used for common case where VF has more queues than
		 * the synthetic device.
		 */
		qdisc_skb_cb(skb)->slave_dev_queue_mapping = txq;
	} else {
		txq = netvsc_pick_tx(ndev, skb);
	}
	rcu_read_unlock();

	while (unlikely(txq >= ndev->real_num_tx_queues))
		txq -= ndev->real_num_tx_queues;

	return txq;
}

static u32 fill_pg_buf(struct page *page, u32 offset, u32 len,
		       struct hv_page_buffer *pb)
{
	int j = 0;

	/* Deal with compound pages by ignoring unused part
	 * of the page.
	 */
	page += (offset >> PAGE_SHIFT);
	offset &= ~PAGE_MASK;

	while (len > 0) {
		unsigned long bytes;

		bytes = PAGE_SIZE - offset;
		if (bytes > len)
			bytes = len;
		pb[j].pfn = page_to_pfn(page);
		pb[j].offset = offset;
		pb[j].len = bytes;

		offset += bytes;
		len -= bytes;

		if (offset == PAGE_SIZE && len) {
			page++;
			offset = 0;
			j++;
		}
	}

	return j + 1;
}

static u32 init_page_array(void *hdr, u32 len, struct sk_buff *skb,
			   struct hv_netvsc_packet *packet,
			   struct hv_page_buffer *pb)
{
	u32 slots_used = 0;
	char *data = skb->data;
	int frags = skb_shinfo(skb)->nr_frags;
	int i;

	/* The packet is laid out thus:
	 * 1. hdr: RNDIS header and PPI
	 * 2. skb linear data
	 * 3. skb fragment data
	 */
	slots_used += fill_pg_buf(virt_to_page(hdr),
				  offset_in_page(hdr),
				  len, &pb[slots_used]);

	packet->rmsg_size = len;
	packet->rmsg_pgcnt = slots_used;

	slots_used += fill_pg_buf(virt_to_page(data),
				offset_in_page(data),
				skb_headlen(skb), &pb[slots_used]);

	for (i = 0; i < frags; i++) {
		skb_frag_t *frag = skb_shinfo(skb)->frags + i;

		slots_used += fill_pg_buf(skb_frag_page(frag),
					skb_frag_off(frag),
					skb_frag_size(frag), &pb[slots_used]);
	}
	return slots_used;
}

static int count_skb_frag_slots(struct sk_buff *skb)
{
	int i, frags = skb_shinfo(skb)->nr_frags;
	int pages = 0;

	for (i = 0; i < frags; i++) {
		skb_frag_t *frag = skb_shinfo(skb)->frags + i;
		unsigned long size = skb_frag_size(frag);
		unsigned long offset = skb_frag_off(frag);

		/* Skip unused frames from start of page */
		offset &= ~PAGE_MASK;
		pages += PFN_UP(offset + size);
	}
	return pages;
}

static int netvsc_get_slots(struct sk_buff *skb)
{
	char *data = skb->data;
	unsigned int offset = offset_in_page(data);
	unsigned int len = skb_headlen(skb);
	int slots;
	int frag_slots;

	slots = DIV_ROUND_UP(offset + len, PAGE_SIZE);
	frag_slots = count_skb_frag_slots(skb);
	return slots + frag_slots;
}

static u32 net_checksum_info(struct sk_buff *skb)
{
	if (skb->protocol == htons(ETH_P_IP)) {
		struct iphdr *ip = ip_hdr(skb);

		if (ip->protocol == IPPROTO_TCP)
			return TRANSPORT_INFO_IPV4_TCP;
		else if (ip->protocol == IPPROTO_UDP)
			return TRANSPORT_INFO_IPV4_UDP;
	} else {
		struct ipv6hdr *ip6 = ipv6_hdr(skb);

		if (ip6->nexthdr == IPPROTO_TCP)
			return TRANSPORT_INFO_IPV6_TCP;
		else if (ip6->nexthdr == IPPROTO_UDP)
			return TRANSPORT_INFO_IPV6_UDP;
	}

	return TRANSPORT_INFO_NOT_IP;
}

/* Send skb on the slave VF device. */
static int netvsc_vf_xmit(struct net_device *net, struct net_device *vf_netdev,
			  struct sk_buff *skb)
{
	struct net_device_context *ndev_ctx = netdev_priv(net);
	unsigned int len = skb->len;
	int rc;

	skb->dev = vf_netdev;
	skb->queue_mapping = qdisc_skb_cb(skb)->slave_dev_queue_mapping;

	rc = dev_queue_xmit(skb);
	if (likely(rc == NET_XMIT_SUCCESS || rc == NET_XMIT_CN)) {
		struct netvsc_vf_pcpu_stats *pcpu_stats
			= this_cpu_ptr(ndev_ctx->vf_stats);

		u64_stats_update_begin(&pcpu_stats->syncp);
		pcpu_stats->tx_packets++;
		pcpu_stats->tx_bytes += len;
		u64_stats_update_end(&pcpu_stats->syncp);
	} else {
		this_cpu_inc(ndev_ctx->vf_stats->tx_dropped);
	}

	return rc;
}

static int netvsc_xmit(struct sk_buff *skb, struct net_device *net, bool xdp_tx)
{
	struct net_device_context *net_device_ctx = netdev_priv(net);
	struct hv_netvsc_packet *packet = NULL;
	int ret;
	unsigned int num_data_pgs;
	struct rndis_message *rndis_msg;
	struct net_device *vf_netdev;
	u32 rndis_msg_size;
	u32 hash;
	struct hv_page_buffer pb[MAX_PAGE_BUFFER_COUNT];

	/* if VF is present and up then redirect packets
	 * already called with rcu_read_lock_bh
	 */
	vf_netdev = rcu_dereference_bh(net_device_ctx->vf_netdev);
	if (vf_netdev && netif_running(vf_netdev) &&
	    !netpoll_tx_running(net))
		return netvsc_vf_xmit(net, vf_netdev, skb);

	/* We will atmost need two pages to describe the rndis
	 * header. We can only transmit MAX_PAGE_BUFFER_COUNT number
	 * of pages in a single packet. If skb is scattered around
	 * more pages we try linearizing it.
	 */

	num_data_pgs = netvsc_get_slots(skb) + 2;

	if (unlikely(num_data_pgs > MAX_PAGE_BUFFER_COUNT)) {
		++net_device_ctx->eth_stats.tx_scattered;

		if (skb_linearize(skb))
			goto no_memory;

		num_data_pgs = netvsc_get_slots(skb) + 2;
		if (num_data_pgs > MAX_PAGE_BUFFER_COUNT) {
			++net_device_ctx->eth_stats.tx_too_big;
			goto drop;
		}
	}

	/*
	 * Place the rndis header in the skb head room and
	 * the skb->cb will be used for hv_netvsc_packet
	 * structure.
	 */
	ret = skb_cow_head(skb, RNDIS_AND_PPI_SIZE);
	if (ret)
		goto no_memory;

	/* Use the skb control buffer for building up the packet */
	BUILD_BUG_ON(sizeof(struct hv_netvsc_packet) >
			sizeof_field(struct sk_buff, cb));
	packet = (struct hv_netvsc_packet *)skb->cb;

	packet->q_idx = skb_get_queue_mapping(skb);

	packet->total_data_buflen = skb->len;
	packet->total_bytes = skb->len;
	packet->total_packets = 1;

	rndis_msg = (struct rndis_message *)skb->head;

	/* Add the rndis header */
	rndis_msg->ndis_msg_type = RNDIS_MSG_PACKET;
	rndis_msg->msg_len = packet->total_data_buflen;

	rndis_msg->msg.pkt = (struct rndis_packet) {
		.data_offset = sizeof(struct rndis_packet),
		.data_len = packet->total_data_buflen,
		.per_pkt_info_offset = sizeof(struct rndis_packet),
	};

	rndis_msg_size = RNDIS_MESSAGE_SIZE(struct rndis_packet);

	hash = skb_get_hash_raw(skb);
	if (hash != 0 && net->real_num_tx_queues > 1) {
		u32 *hash_info;

		rndis_msg_size += NDIS_HASH_PPI_SIZE;
		hash_info = init_ppi_data(rndis_msg, NDIS_HASH_PPI_SIZE,
					  NBL_HASH_VALUE);
		*hash_info = hash;
	}

	if (skb_vlan_tag_present(skb)) {
		struct ndis_pkt_8021q_info *vlan;

		rndis_msg_size += NDIS_VLAN_PPI_SIZE;
		vlan = init_ppi_data(rndis_msg, NDIS_VLAN_PPI_SIZE,
				     IEEE_8021Q_INFO);

		vlan->value = 0;
		vlan->vlanid = skb_vlan_tag_get_id(skb);
		vlan->cfi = skb_vlan_tag_get_cfi(skb);
		vlan->pri = skb_vlan_tag_get_prio(skb);
	}

	if (skb_is_gso(skb)) {
		struct ndis_tcp_lso_info *lso_info;

		rndis_msg_size += NDIS_LSO_PPI_SIZE;
		lso_info = init_ppi_data(rndis_msg, NDIS_LSO_PPI_SIZE,
					 TCP_LARGESEND_PKTINFO);

		lso_info->value = 0;
		lso_info->lso_v2_transmit.type = NDIS_TCP_LARGE_SEND_OFFLOAD_V2_TYPE;
		if (skb->protocol == htons(ETH_P_IP)) {
			lso_info->lso_v2_transmit.ip_version =
				NDIS_TCP_LARGE_SEND_OFFLOAD_IPV4;
			ip_hdr(skb)->tot_len = 0;
			ip_hdr(skb)->check = 0;
			tcp_hdr(skb)->check =
				~csum_tcpudp_magic(ip_hdr(skb)->saddr,
						   ip_hdr(skb)->daddr, 0, IPPROTO_TCP, 0);
		} else {
			lso_info->lso_v2_transmit.ip_version =
				NDIS_TCP_LARGE_SEND_OFFLOAD_IPV6;
			ipv6_hdr(skb)->payload_len = 0;
			tcp_hdr(skb)->check =
				~csum_ipv6_magic(&ipv6_hdr(skb)->saddr,
						 &ipv6_hdr(skb)->daddr, 0, IPPROTO_TCP, 0);
		}
		lso_info->lso_v2_transmit.tcp_header_offset = skb_transport_offset(skb);
		lso_info->lso_v2_transmit.mss = skb_shinfo(skb)->gso_size;
	} else if (skb->ip_summed == CHECKSUM_PARTIAL) {
		if (net_checksum_info(skb) & net_device_ctx->tx_checksum_mask) {
			struct ndis_tcp_ip_checksum_info *csum_info;

			rndis_msg_size += NDIS_CSUM_PPI_SIZE;
			csum_info = init_ppi_data(rndis_msg, NDIS_CSUM_PPI_SIZE,
						  TCPIP_CHKSUM_PKTINFO);

			csum_info->value = 0;
			csum_info->transmit.tcp_header_offset = skb_transport_offset(skb);

			if (skb->protocol == htons(ETH_P_IP)) {
				csum_info->transmit.is_ipv4 = 1;

				if (ip_hdr(skb)->protocol == IPPROTO_TCP)
					csum_info->transmit.tcp_checksum = 1;
				else
					csum_info->transmit.udp_checksum = 1;
			} else {
				csum_info->transmit.is_ipv6 = 1;

				if (ipv6_hdr(skb)->nexthdr == IPPROTO_TCP)
					csum_info->transmit.tcp_checksum = 1;
				else
					csum_info->transmit.udp_checksum = 1;
			}
		} else {
			/* Can't do offload of this type of checksum */
			if (skb_checksum_help(skb))
				goto drop;
		}
	}

	/* Start filling in the page buffers with the rndis hdr */
	rndis_msg->msg_len += rndis_msg_size;
	packet->total_data_buflen = rndis_msg->msg_len;
	packet->page_buf_cnt = init_page_array(rndis_msg, rndis_msg_size,
					       skb, packet, pb);

	/* timestamp packet in software */
	skb_tx_timestamp(skb);

	ret = netvsc_send(net, packet, rndis_msg, pb, skb, xdp_tx);
	if (likely(ret == 0))
		return NETDEV_TX_OK;

	if (ret == -EAGAIN) {
		++net_device_ctx->eth_stats.tx_busy;
		return NETDEV_TX_BUSY;
	}

	if (ret == -ENOSPC)
		++net_device_ctx->eth_stats.tx_no_space;

drop:
	dev_kfree_skb_any(skb);
	net->stats.tx_dropped++;

	return NETDEV_TX_OK;

no_memory:
	++net_device_ctx->eth_stats.tx_no_memory;
	goto drop;
}

static int netvsc_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	return netvsc_xmit(skb, ndev, false);
}

/*
 * netvsc_linkstatus_callback - Link up/down notification
 */
void netvsc_linkstatus_callback(struct net_device *net,
				struct rndis_message *resp)
{
	struct rndis_indicate_status *indicate = &resp->msg.indicate_status;
	struct net_device_context *ndev_ctx = netdev_priv(net);
	struct netvsc_reconfig *event;
	unsigned long flags;

	/* Update the physical link speed when changing to another vSwitch */
	if (indicate->status == RNDIS_STATUS_LINK_SPEED_CHANGE) {
		u32 speed;

		speed = *(u32 *)((void *)indicate
				 + indicate->status_buf_offset) / 10000;
		ndev_ctx->speed = speed;
		return;
	}

	/* Handle these link change statuses below */
	if (indicate->status != RNDIS_STATUS_NETWORK_CHANGE &&
	    indicate->status != RNDIS_STATUS_MEDIA_CONNECT &&
	    indicate->status != RNDIS_STATUS_MEDIA_DISCONNECT)
		return;

	if (net->reg_state != NETREG_REGISTERED)
		return;

	event = kzalloc(sizeof(*event), GFP_ATOMIC);
	if (!event)
		return;
	event->event = indicate->status;

	spin_lock_irqsave(&ndev_ctx->lock, flags);
	list_add_tail(&event->list, &ndev_ctx->reconfig_events);
	spin_unlock_irqrestore(&ndev_ctx->lock, flags);

	schedule_delayed_work(&ndev_ctx->dwork, 0);
}

static void netvsc_xdp_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	int rc;

	skb->queue_mapping = skb_get_rx_queue(skb);
	__skb_push(skb, ETH_HLEN);

	rc = netvsc_xmit(skb, ndev, true);

	if (dev_xmit_complete(rc))
		return;

	dev_kfree_skb_any(skb);
	ndev->stats.tx_dropped++;
}

static void netvsc_comp_ipcsum(struct sk_buff *skb)
{
	struct iphdr *iph = (struct iphdr *)skb->data;

	iph->check = 0;
	iph->check = ip_fast_csum(iph, iph->ihl);
}

static struct sk_buff *netvsc_alloc_recv_skb(struct net_device *net,
					     struct netvsc_channel *nvchan,
					     struct xdp_buff *xdp)
{
	struct napi_struct *napi = &nvchan->napi;
	const struct ndis_pkt_8021q_info *vlan = nvchan->rsc.vlan;
	const struct ndis_tcp_ip_checksum_info *csum_info =
						nvchan->rsc.csum_info;
	const u32 *hash_info = nvchan->rsc.hash_info;
	struct sk_buff *skb;
	void *xbuf = xdp->data_hard_start;
	int i;

	if (xbuf) {
		unsigned int hdroom = xdp->data - xdp->data_hard_start;
		unsigned int xlen = xdp->data_end - xdp->data;
		unsigned int frag_size = netvsc_xdp_fraglen(hdroom + xlen);

		skb = build_skb(xbuf, frag_size);

		if (!skb) {
			__free_page(virt_to_page(xbuf));
			return NULL;
		}

		skb_reserve(skb, hdroom);
		skb_put(skb, xlen);
		skb->dev = napi->dev;
	} else {
		skb = napi_alloc_skb(napi, nvchan->rsc.pktlen);

		if (!skb)
			return NULL;

		/* Copy to skb. This copy is needed here since the memory
		 * pointed by hv_netvsc_packet cannot be deallocated.
		 */
		for (i = 0; i < nvchan->rsc.cnt; i++)
			skb_put_data(skb, nvchan->rsc.data[i],
				     nvchan->rsc.len[i]);
	}

	skb->protocol = eth_type_trans(skb, net);

	/* skb is already created with CHECKSUM_NONE */
	skb_checksum_none_assert(skb);

	/* Incoming packets may have IP header checksum verified by the host.
	 * They may not have IP header checksum computed after coalescing.
	 * We compute it here if the flags are set, because on Linux, the IP
	 * checksum is always checked.
	 */
	if (csum_info && csum_info->receive.ip_checksum_value_invalid &&
	    csum_info->receive.ip_checksum_succeeded &&
	    skb->protocol == htons(ETH_P_IP))
		netvsc_comp_ipcsum(skb);

	/* Do L4 checksum offload if enabled and present. */
	if (csum_info && (net->features & NETIF_F_RXCSUM)) {
		if (csum_info->receive.tcp_checksum_succeeded ||
		    csum_info->receive.udp_checksum_succeeded)
			skb->ip_summed = CHECKSUM_UNNECESSARY;
	}

	if (hash_info && (net->features & NETIF_F_RXHASH))
		skb_set_hash(skb, *hash_info, PKT_HASH_TYPE_L4);

	if (vlan) {
		u16 vlan_tci = vlan->vlanid | (vlan->pri << VLAN_PRIO_SHIFT) |
			(vlan->cfi ? VLAN_CFI_MASK : 0);

		__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q),
				       vlan_tci);
	}

	return skb;
}

/*
 * netvsc_recv_callback -  Callback when we receive a packet from the
 * "wire" on the specified device.
 */
int netvsc_recv_callback(struct net_device *net,
			 struct netvsc_device *net_device,
			 struct netvsc_channel *nvchan)
{
	struct net_device_context *net_device_ctx = netdev_priv(net);
	struct vmbus_channel *channel = nvchan->channel;
	u16 q_idx = channel->offermsg.offer.sub_channel_index;
	struct sk_buff *skb;
	struct netvsc_stats *rx_stats = &nvchan->rx_stats;
	struct xdp_buff xdp;
	u32 act;

	if (net->reg_state != NETREG_REGISTERED)
		return NVSP_STAT_FAIL;

	act = netvsc_run_xdp(net, nvchan, &xdp);

	if (act != XDP_PASS && act != XDP_TX) {
		u64_stats_update_begin(&rx_stats->syncp);
		rx_stats->xdp_drop++;
		u64_stats_update_end(&rx_stats->syncp);

		return NVSP_STAT_SUCCESS; /* consumed by XDP */
	}

	/* Allocate a skb - TODO direct I/O to pages? */
	skb = netvsc_alloc_recv_skb(net, nvchan, &xdp);

	if (unlikely(!skb)) {
		++net_device_ctx->eth_stats.rx_no_memory;
		return NVSP_STAT_FAIL;
	}

	skb_record_rx_queue(skb, q_idx);

	/*
	 * Even if injecting the packet, record the statistics
	 * on the synthetic device because modifying the VF device
	 * statistics will not work correctly.
	 */
	u64_stats_update_begin(&rx_stats->syncp);
	rx_stats->packets++;
	rx_stats->bytes += nvchan->rsc.pktlen;

	if (skb->pkt_type == PACKET_BROADCAST)
		++rx_stats->broadcast;
	else if (skb->pkt_type == PACKET_MULTICAST)
		++rx_stats->multicast;
	u64_stats_update_end(&rx_stats->syncp);

	if (act == XDP_TX) {
		netvsc_xdp_xmit(skb, net);
		return NVSP_STAT_SUCCESS;
	}

	napi_gro_receive(&nvchan->napi, skb);
	return NVSP_STAT_SUCCESS;
}

static void netvsc_get_drvinfo(struct net_device *net,
			       struct ethtool_drvinfo *info)
{
	strlcpy(info->driver, KBUILD_MODNAME, sizeof(info->driver));
	strlcpy(info->fw_version, "N/A", sizeof(info->fw_version));
}

static void netvsc_get_channels(struct net_device *net,
				struct ethtool_channels *channel)
{
	struct net_device_context *net_device_ctx = netdev_priv(net);
	struct netvsc_device *nvdev = rtnl_dereference(net_device_ctx->nvdev);

	if (nvdev) {
		channel->max_combined	= nvdev->max_chn;
		channel->combined_count = nvdev->num_chn;
	}
}

/* Alloc struct netvsc_device_info, and initialize it from either existing
 * struct netvsc_device, or from default values.
 */
static
struct netvsc_device_info *netvsc_devinfo_get(struct netvsc_device *nvdev)
{
	struct netvsc_device_info *dev_info;
	struct bpf_prog *prog;

	dev_info = kzalloc(sizeof(*dev_info), GFP_ATOMIC);

	if (!dev_info)
		return NULL;

	if (nvdev) {
		ASSERT_RTNL();

		dev_info->num_chn = nvdev->num_chn;
		dev_info->send_sections = nvdev->send_section_cnt;
		dev_info->send_section_size = nvdev->send_section_size;
		dev_info->recv_sections = nvdev->recv_section_cnt;
		dev_info->recv_section_size = nvdev->recv_section_size;

		memcpy(dev_info->rss_key, nvdev->extension->rss_key,
		       NETVSC_HASH_KEYLEN);

		prog = netvsc_xdp_get(nvdev);
		if (prog) {
			bpf_prog_inc(prog);
			dev_info->bprog = prog;
		}
	} else {
		dev_info->num_chn = VRSS_CHANNEL_DEFAULT;
		dev_info->send_sections = NETVSC_DEFAULT_TX;
		dev_info->send_section_size = NETVSC_SEND_SECTION_SIZE;
		dev_info->recv_sections = NETVSC_DEFAULT_RX;
		dev_info->recv_section_size = NETVSC_RECV_SECTION_SIZE;
	}

	return dev_info;
}

/* Free struct netvsc_device_info */
static void netvsc_devinfo_put(struct netvsc_device_info *dev_info)
{
	if (dev_info->bprog) {
		ASSERT_RTNL();
		bpf_prog_put(dev_info->bprog);
	}

	kfree(dev_info);
}

static int netvsc_detach(struct net_device *ndev,
			 struct netvsc_device *nvdev)
{
	struct net_device_context *ndev_ctx = netdev_priv(ndev);
	struct hv_device *hdev = ndev_ctx->device_ctx;
	int ret;

	/* Don't try continuing to try and setup sub channels */
	if (cancel_work_sync(&nvdev->subchan_work))
		nvdev->num_chn = 1;

	netvsc_xdp_set(ndev, NULL, NULL, nvdev);

	/* If device was up (receiving) then shutdown */
	if (netif_running(ndev)) {
		netvsc_tx_disable(nvdev, ndev);

		ret = rndis_filter_close(nvdev);
		if (ret) {
			netdev_err(ndev,
				   "unable to close device (ret %d).\n", ret);
			return ret;
		}

		ret = netvsc_wait_until_empty(nvdev);
		if (ret) {
			netdev_err(ndev,
				   "Ring buffer not empty after closing rndis\n");
			return ret;
		}
	}

	netif_device_detach(ndev);

	rndis_filter_device_remove(hdev, nvdev);

	return 0;
}

static int netvsc_attach(struct net_device *ndev,
			 struct netvsc_device_info *dev_info)
{
	struct net_device_context *ndev_ctx = netdev_priv(ndev);
	struct hv_device *hdev = ndev_ctx->device_ctx;
	struct netvsc_device *nvdev;
	struct rndis_device *rdev;
	struct bpf_prog *prog;
	int ret = 0;

	nvdev = rndis_filter_device_add(hdev, dev_info);
	if (IS_ERR(nvdev))
		return PTR_ERR(nvdev);

	if (nvdev->num_chn > 1) {
		ret = rndis_set_subchannel(ndev, nvdev, dev_info);

		/* if unavailable, just proceed with one queue */
		if (ret) {
			nvdev->max_chn = 1;
			nvdev->num_chn = 1;
		}
	}

	prog = dev_info->bprog;
	if (prog) {
		bpf_prog_inc(prog);
		ret = netvsc_xdp_set(ndev, prog, NULL, nvdev);
		if (ret) {
			bpf_prog_put(prog);
			goto err1;
		}
	}

	/* In any case device is now ready */
	netif_device_attach(ndev);

	/* Note: enable and attach happen when sub-channels setup */
	netif_carrier_off(ndev);

	if (netif_running(ndev)) {
		ret = rndis_filter_open(nvdev);
		if (ret)
			goto err2;

		rdev = nvdev->extension;
		if (!rdev->link_state)
			netif_carrier_on(ndev);
	}

	return 0;

err2:
	netif_device_detach(ndev);

err1:
	rndis_filter_device_remove(hdev, nvdev);

	return ret;
}

static int netvsc_set_channels(struct net_device *net,
			       struct ethtool_channels *channels)
{
	struct net_device_context *net_device_ctx = netdev_priv(net);
	struct netvsc_device *nvdev = rtnl_dereference(net_device_ctx->nvdev);
	unsigned int orig, count = channels->combined_count;
	struct netvsc_device_info *device_info;
	int ret;

	/* We do not support separate count for rx, tx, or other */
	if (count == 0 ||
	    channels->rx_count || channels->tx_count || channels->other_count)
		return -EINVAL;

	if (!nvdev || nvdev->destroy)
		return -ENODEV;

	if (nvdev->nvsp_version < NVSP_PROTOCOL_VERSION_5)
		return -EINVAL;

	if (count > nvdev->max_chn)
		return -EINVAL;

	orig = nvdev->num_chn;

	device_info = netvsc_devinfo_get(nvdev);

	if (!device_info)
		return -ENOMEM;

	device_info->num_chn = count;

	ret = netvsc_detach(net, nvdev);
	if (ret)
		goto out;

	ret = netvsc_attach(net, device_info);
	if (ret) {
		device_info->num_chn = orig;
		if (netvsc_attach(net, device_info))
			netdev_err(net, "restoring channel setting failed\n");
	}

out:
	netvsc_devinfo_put(device_info);
	return ret;
}

static bool
netvsc_validate_ethtool_ss_cmd(const struct ethtool_link_ksettings *cmd)
{
	struct ethtool_link_ksettings diff1 = *cmd;
	struct ethtool_link_ksettings diff2 = {};

	diff1.base.speed = 0;
	diff1.base.duplex = 0;
	/* advertising and cmd are usually set */
	ethtool_link_ksettings_zero_link_mode(&diff1, advertising);
	diff1.base.cmd = 0;
	/* We set port to PORT_OTHER */
	diff2.base.port = PORT_OTHER;

	return !memcmp(&diff1, &diff2, sizeof(diff1));
}

static void netvsc_init_settings(struct net_device *dev)
{
	struct net_device_context *ndc = netdev_priv(dev);

	ndc->l4_hash = HV_DEFAULT_L4HASH;

	ndc->speed = SPEED_UNKNOWN;
	ndc->duplex = DUPLEX_FULL;

	dev->features = NETIF_F_LRO;
}

static int netvsc_get_link_ksettings(struct net_device *dev,
				     struct ethtool_link_ksettings *cmd)
{
	struct net_device_context *ndc = netdev_priv(dev);

	cmd->base.speed = ndc->speed;
	cmd->base.duplex = ndc->duplex;
	cmd->base.port = PORT_OTHER;

	return 0;
}

static int netvsc_set_link_ksettings(struct net_device *dev,
				     const struct ethtool_link_ksettings *cmd)
{
	struct net_device_context *ndc = netdev_priv(dev);
	u32 speed;

	speed = cmd->base.speed;
	if (!ethtool_validate_speed(speed) ||
	    !ethtool_validate_duplex(cmd->base.duplex) ||
	    !netvsc_validate_ethtool_ss_cmd(cmd))
		return -EINVAL;

	ndc->speed = speed;
	ndc->duplex = cmd->base.duplex;

	return 0;
}

static int netvsc_change_mtu(struct net_device *ndev, int mtu)
{
	struct net_device_context *ndevctx = netdev_priv(ndev);
	struct net_device *vf_netdev = rtnl_dereference(ndevctx->vf_netdev);
	struct netvsc_device *nvdev = rtnl_dereference(ndevctx->nvdev);
	int orig_mtu = ndev->mtu;
	struct netvsc_device_info *device_info;
	int ret = 0;

	if (!nvdev || nvdev->destroy)
		return -ENODEV;

	device_info = netvsc_devinfo_get(nvdev);

	if (!device_info)
		return -ENOMEM;

	/* Change MTU of underlying VF netdev first. */
	if (vf_netdev) {
		ret = dev_set_mtu(vf_netdev, mtu);
		if (ret)
			goto out;
	}

	ret = netvsc_detach(ndev, nvdev);
	if (ret)
		goto rollback_vf;

	ndev->mtu = mtu;

	ret = netvsc_attach(ndev, device_info);
	if (!ret)
		goto out;

	/* Attempt rollback to original MTU */
	ndev->mtu = orig_mtu;

	if (netvsc_attach(ndev, device_info))
		netdev_err(ndev, "restoring mtu failed\n");
rollback_vf:
	if (vf_netdev)
		dev_set_mtu(vf_netdev, orig_mtu);

out:
	netvsc_devinfo_put(device_info);
	return ret;
}

static void netvsc_get_vf_stats(struct net_device *net,
				struct netvsc_vf_pcpu_stats *tot)
{
	struct net_device_context *ndev_ctx = netdev_priv(net);
	int i;

	memset(tot, 0, sizeof(*tot));

	for_each_possible_cpu(i) {
		const struct netvsc_vf_pcpu_stats *stats
			= per_cpu_ptr(ndev_ctx->vf_stats, i);
		u64 rx_packets, rx_bytes, tx_packets, tx_bytes;
		unsigned int start;

		do {
			start = u64_stats_fetch_begin_irq(&stats->syncp);
			rx_packets = stats->rx_packets;
			tx_packets = stats->tx_packets;
			rx_bytes = stats->rx_bytes;
			tx_bytes = stats->tx_bytes;
		} while (u64_stats_fetch_retry_irq(&stats->syncp, start));

		tot->rx_packets += rx_packets;
		tot->tx_packets += tx_packets;
		tot->rx_bytes   += rx_bytes;
		tot->tx_bytes   += tx_bytes;
		tot->tx_dropped += stats->tx_dropped;
	}
}

static void netvsc_get_pcpu_stats(struct net_device *net,
				  struct netvsc_ethtool_pcpu_stats *pcpu_tot)
{
	struct net_device_context *ndev_ctx = netdev_priv(net);
	struct netvsc_device *nvdev = rcu_dereference_rtnl(ndev_ctx->nvdev);
	int i;

	/* fetch percpu stats of vf */
	for_each_possible_cpu(i) {
		const struct netvsc_vf_pcpu_stats *stats =
			per_cpu_ptr(ndev_ctx->vf_stats, i);
		struct netvsc_ethtool_pcpu_stats *this_tot = &pcpu_tot[i];
		unsigned int start;

		do {
			start = u64_stats_fetch_begin_irq(&stats->syncp);
			this_tot->vf_rx_packets = stats->rx_packets;
			this_tot->vf_tx_packets = stats->tx_packets;
			this_tot->vf_rx_bytes = stats->rx_bytes;
			this_tot->vf_tx_bytes = stats->tx_bytes;
		} while (u64_stats_fetch_retry_irq(&stats->syncp, start));
		this_tot->rx_packets = this_tot->vf_rx_packets;
		this_tot->tx_packets = this_tot->vf_tx_packets;
		this_tot->rx_bytes   = this_tot->vf_rx_bytes;
		this_tot->tx_bytes   = this_tot->vf_tx_bytes;
	}

	/* fetch percpu stats of netvsc */
	for (i = 0; i < nvdev->num_chn; i++) {
		const struct netvsc_channel *nvchan = &nvdev->chan_table[i];
		const struct netvsc_stats *stats;
		struct netvsc_ethtool_pcpu_stats *this_tot =
			&pcpu_tot[nvchan->channel->target_cpu];
		u64 packets, bytes;
		unsigned int start;

		stats = &nvchan->tx_stats;
		do {
			start = u64_stats_fetch_begin_irq(&stats->syncp);
			packets = stats->packets;
			bytes = stats->bytes;
		} while (u64_stats_fetch_retry_irq(&stats->syncp, start));

		this_tot->tx_bytes	+= bytes;
		this_tot->tx_packets	+= packets;

		stats = &nvchan->rx_stats;
		do {
			start = u64_stats_fetch_begin_irq(&stats->syncp);
			packets = stats->packets;
			bytes = stats->bytes;
		} while (u64_stats_fetch_retry_irq(&stats->syncp, start));

		this_tot->rx_bytes	+= bytes;
		this_tot->rx_packets	+= packets;
	}
}

static void netvsc_get_stats64(struct net_device *net,
			       struct rtnl_link_stats64 *t)
{
	struct net_device_context *ndev_ctx = netdev_priv(net);
	struct netvsc_device *nvdev;
	struct netvsc_vf_pcpu_stats vf_tot;
	int i;

	rcu_read_lock();

	nvdev = rcu_dereference(ndev_ctx->nvdev);
	if (!nvdev)
		goto out;

	netdev_stats_to_stats64(t, &net->stats);

	netvsc_get_vf_stats(net, &vf_tot);
	t->rx_packets += vf_tot.rx_packets;
	t->tx_packets += vf_tot.tx_packets;
	t->rx_bytes   += vf_tot.rx_bytes;
	t->tx_bytes   += vf_tot.tx_bytes;
	t->tx_dropped += vf_tot.tx_dropped;

	for (i = 0; i < nvdev->num_chn; i++) {
		const struct netvsc_channel *nvchan = &nvdev->chan_table[i];
		const struct netvsc_stats *stats;
		u64 packets, bytes, multicast;
		unsigned int start;

		stats = &nvchan->tx_stats;
		do {
			start = u64_stats_fetch_begin_irq(&stats->syncp);
			packets = stats->packets;
			bytes = stats->bytes;
		} while (u64_stats_fetch_retry_irq(&stats->syncp, start));

		t->tx_bytes	+= bytes;
		t->tx_packets	+= packets;

		stats = &nvchan->rx_stats;
		do {
			start = u64_stats_fetch_begin_irq(&stats->syncp);
			packets = stats->packets;
			bytes = stats->bytes;
			multicast = stats->multicast + stats->broadcast;
		} while (u64_stats_fetch_retry_irq(&stats->syncp, start));

		t->rx_bytes	+= bytes;
		t->rx_packets	+= packets;
		t->multicast	+= multicast;
	}
out:
	rcu_read_unlock();
}

static int netvsc_set_mac_addr(struct net_device *ndev, void *p)
{
	struct net_device_context *ndc = netdev_priv(ndev);
	struct net_device *vf_netdev = rtnl_dereference(ndc->vf_netdev);
	struct netvsc_device *nvdev = rtnl_dereference(ndc->nvdev);
	struct sockaddr *addr = p;
	int err;

	err = eth_prepare_mac_addr_change(ndev, p);
	if (err)
		return err;

	if (!nvdev)
		return -ENODEV;

	if (vf_netdev) {
		err = dev_set_mac_address(vf_netdev, addr, NULL);
		if (err)
			return err;
	}

	err = rndis_filter_set_device_mac(nvdev, addr->sa_data);
	if (!err) {
		eth_commit_mac_addr_change(ndev, p);
	} else if (vf_netdev) {
		/* rollback change on VF */
		memcpy(addr->sa_data, ndev->dev_addr, ETH_ALEN);
		dev_set_mac_address(vf_netdev, addr, NULL);
	}

	return err;
}

static const struct {
	char name[ETH_GSTRING_LEN];
	u16 offset;
} netvsc_stats[] = {
	{ "tx_scattered", offsetof(struct netvsc_ethtool_stats, tx_scattered) },
	{ "tx_no_memory", offsetof(struct netvsc_ethtool_stats, tx_no_memory) },
	{ "tx_no_space",  offsetof(struct netvsc_ethtool_stats, tx_no_space) },
	{ "tx_too_big",	  offsetof(struct netvsc_ethtool_stats, tx_too_big) },
	{ "tx_busy",	  offsetof(struct netvsc_ethtool_stats, tx_busy) },
	{ "tx_send_full", offsetof(struct netvsc_ethtool_stats, tx_send_full) },
	{ "rx_comp_busy", offsetof(struct netvsc_ethtool_stats, rx_comp_busy) },
	{ "rx_no_memory", offsetof(struct netvsc_ethtool_stats, rx_no_memory) },
	{ "stop_queue", offsetof(struct netvsc_ethtool_stats, stop_queue) },
	{ "wake_queue", offsetof(struct netvsc_ethtool_stats, wake_queue) },
}, pcpu_stats[] = {
	{ "cpu%u_rx_packets",
		offsetof(struct netvsc_ethtool_pcpu_stats, rx_packets) },
	{ "cpu%u_rx_bytes",
		offsetof(struct netvsc_ethtool_pcpu_stats, rx_bytes) },
	{ "cpu%u_tx_packets",
		offsetof(struct netvsc_ethtool_pcpu_stats, tx_packets) },
	{ "cpu%u_tx_bytes",
		offsetof(struct netvsc_ethtool_pcpu_stats, tx_bytes) },
	{ "cpu%u_vf_rx_packets",
		offsetof(struct netvsc_ethtool_pcpu_stats, vf_rx_packets) },
	{ "cpu%u_vf_rx_bytes",
		offsetof(struct netvsc_ethtool_pcpu_stats, vf_rx_bytes) },
	{ "cpu%u_vf_tx_packets",
		offsetof(struct netvsc_ethtool_pcpu_stats, vf_tx_packets) },
	{ "cpu%u_vf_tx_bytes",
		offsetof(struct netvsc_ethtool_pcpu_stats, vf_tx_bytes) },
}, vf_stats[] = {
	{ "vf_rx_packets", offsetof(struct netvsc_vf_pcpu_stats, rx_packets) },
	{ "vf_rx_bytes",   offsetof(struct netvsc_vf_pcpu_stats, rx_bytes) },
	{ "vf_tx_packets", offsetof(struct netvsc_vf_pcpu_stats, tx_packets) },
	{ "vf_tx_bytes",   offsetof(struct netvsc_vf_pcpu_stats, tx_bytes) },
	{ "vf_tx_dropped", offsetof(struct netvsc_vf_pcpu_stats, tx_dropped) },
};

#define NETVSC_GLOBAL_STATS_LEN	ARRAY_SIZE(netvsc_stats)
#define NETVSC_VF_STATS_LEN	ARRAY_SIZE(vf_stats)

/* statistics per queue (rx/tx packets/bytes) */
#define NETVSC_PCPU_STATS_LEN (num_present_cpus() * ARRAY_SIZE(pcpu_stats))

/* 5 statistics per queue (rx/tx packets/bytes, rx xdp_drop) */
#define NETVSC_QUEUE_STATS_LEN(dev) ((dev)->num_chn * 5)

static int netvsc_get_sset_count(struct net_device *dev, int string_set)
{
	struct net_device_context *ndc = netdev_priv(dev);
	struct netvsc_device *nvdev = rtnl_dereference(ndc->nvdev);

	if (!nvdev)
		return -ENODEV;

	switch (string_set) {
	case ETH_SS_STATS:
		return NETVSC_GLOBAL_STATS_LEN
			+ NETVSC_VF_STATS_LEN
			+ NETVSC_QUEUE_STATS_LEN(nvdev)
			+ NETVSC_PCPU_STATS_LEN;
	default:
		return -EINVAL;
	}
}

static void netvsc_get_ethtool_stats(struct net_device *dev,
				     struct ethtool_stats *stats, u64 *data)
{
	struct net_device_context *ndc = netdev_priv(dev);
	struct netvsc_device *nvdev = rtnl_dereference(ndc->nvdev);
	const void *nds = &ndc->eth_stats;
	const struct netvsc_stats *qstats;
	struct netvsc_vf_pcpu_stats sum;
	struct netvsc_ethtool_pcpu_stats *pcpu_sum;
	unsigned int start;
	u64 packets, bytes;
	u64 xdp_drop;
	int i, j, cpu;

	if (!nvdev)
		return;

	for (i = 0; i < NETVSC_GLOBAL_STATS_LEN; i++)
		data[i] = *(unsigned long *)(nds + netvsc_stats[i].offset);

	netvsc_get_vf_stats(dev, &sum);
	for (j = 0; j < NETVSC_VF_STATS_LEN; j++)
		data[i++] = *(u64 *)((void *)&sum + vf_stats[j].offset);

	for (j = 0; j < nvdev->num_chn; j++) {
		qstats = &nvdev->chan_table[j].tx_stats;

		do {
			start = u64_stats_fetch_begin_irq(&qstats->syncp);
			packets = qstats->packets;
			bytes = qstats->bytes;
		} while (u64_stats_fetch_retry_irq(&qstats->syncp, start));
		data[i++] = packets;
		data[i++] = bytes;

		qstats = &nvdev->chan_table[j].rx_stats;
		do {
			start = u64_stats_fetch_begin_irq(&qstats->syncp);
			packets = qstats->packets;
			bytes = qstats->bytes;
			xdp_drop = qstats->xdp_drop;
		} while (u64_stats_fetch_retry_irq(&qstats->syncp, start));
		data[i++] = packets;
		data[i++] = bytes;
		data[i++] = xdp_drop;
	}

	pcpu_sum = kvmalloc_array(num_possible_cpus(),
				  sizeof(struct netvsc_ethtool_pcpu_stats),
				  GFP_KERNEL);
	netvsc_get_pcpu_stats(dev, pcpu_sum);
	for_each_present_cpu(cpu) {
		struct netvsc_ethtool_pcpu_stats *this_sum = &pcpu_sum[cpu];

		for (j = 0; j < ARRAY_SIZE(pcpu_stats); j++)
			data[i++] = *(u64 *)((void *)this_sum
					     + pcpu_stats[j].offset);
	}
	kvfree(pcpu_sum);
}

static void netvsc_get_strings(struct net_device *dev, u32 stringset, u8 *data)
{
	struct net_device_context *ndc = netdev_priv(dev);
	struct netvsc_device *nvdev = rtnl_dereference(ndc->nvdev);
	u8 *p = data;
	int i, cpu;

	if (!nvdev)
		return;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < ARRAY_SIZE(netvsc_stats); i++) {
			memcpy(p, netvsc_stats[i].name, ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}

		for (i = 0; i < ARRAY_SIZE(vf_stats); i++) {
			memcpy(p, vf_stats[i].name, ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}

		for (i = 0; i < nvdev->num_chn; i++) {
			sprintf(p, "tx_queue_%u_packets", i);
			p += ETH_GSTRING_LEN;
			sprintf(p, "tx_queue_%u_bytes", i);
			p += ETH_GSTRING_LEN;
			sprintf(p, "rx_queue_%u_packets", i);
			p += ETH_GSTRING_LEN;
			sprintf(p, "rx_queue_%u_bytes", i);
			p += ETH_GSTRING_LEN;
			sprintf(p, "rx_queue_%u_xdp_drop", i);
			p += ETH_GSTRING_LEN;
		}

		for_each_present_cpu(cpu) {
			for (i = 0; i < ARRAY_SIZE(pcpu_stats); i++) {
				sprintf(p, pcpu_stats[i].name, cpu);
				p += ETH_GSTRING_LEN;
			}
		}

		break;
	}
}

static int
netvsc_get_rss_hash_opts(struct net_device_context *ndc,
			 struct ethtool_rxnfc *info)
{
	const u32 l4_flag = RXH_L4_B_0_1 | RXH_L4_B_2_3;

	info->data = RXH_IP_SRC | RXH_IP_DST;

	switch (info->flow_type) {
	case TCP_V4_FLOW:
		if (ndc->l4_hash & HV_TCP4_L4HASH)
			info->data |= l4_flag;

		break;

	case TCP_V6_FLOW:
		if (ndc->l4_hash & HV_TCP6_L4HASH)
			info->data |= l4_flag;

		break;

	case UDP_V4_FLOW:
		if (ndc->l4_hash & HV_UDP4_L4HASH)
			info->data |= l4_flag;

		break;

	case UDP_V6_FLOW:
		if (ndc->l4_hash & HV_UDP6_L4HASH)
			info->data |= l4_flag;

		break;

	case IPV4_FLOW:
	case IPV6_FLOW:
		break;
	default:
		info->data = 0;
		break;
	}

	return 0;
}

static int
netvsc_get_rxnfc(struct net_device *dev, struct ethtool_rxnfc *info,
		 u32 *rules)
{
	struct net_device_context *ndc = netdev_priv(dev);
	struct netvsc_device *nvdev = rtnl_dereference(ndc->nvdev);

	if (!nvdev)
		return -ENODEV;

	switch (info->cmd) {
	case ETHTOOL_GRXRINGS:
		info->data = nvdev->num_chn;
		return 0;

	case ETHTOOL_GRXFH:
		return netvsc_get_rss_hash_opts(ndc, info);
	}
	return -EOPNOTSUPP;
}

static int netvsc_set_rss_hash_opts(struct net_device_context *ndc,
				    struct ethtool_rxnfc *info)
{
	if (info->data == (RXH_IP_SRC | RXH_IP_DST |
			   RXH_L4_B_0_1 | RXH_L4_B_2_3)) {
		switch (info->flow_type) {
		case TCP_V4_FLOW:
			ndc->l4_hash |= HV_TCP4_L4HASH;
			break;

		case TCP_V6_FLOW:
			ndc->l4_hash |= HV_TCP6_L4HASH;
			break;

		case UDP_V4_FLOW:
			ndc->l4_hash |= HV_UDP4_L4HASH;
			break;

		case UDP_V6_FLOW:
			ndc->l4_hash |= HV_UDP6_L4HASH;
			break;

		default:
			return -EOPNOTSUPP;
		}

		return 0;
	}

	if (info->data == (RXH_IP_SRC | RXH_IP_DST)) {
		switch (info->flow_type) {
		case TCP_V4_FLOW:
			ndc->l4_hash &= ~HV_TCP4_L4HASH;
			break;

		case TCP_V6_FLOW:
			ndc->l4_hash &= ~HV_TCP6_L4HASH;
			break;

		case UDP_V4_FLOW:
			ndc->l4_hash &= ~HV_UDP4_L4HASH;
			break;

		case UDP_V6_FLOW:
			ndc->l4_hash &= ~HV_UDP6_L4HASH;
			break;

		default:
			return -EOPNOTSUPP;
		}

		return 0;
	}

	return -EOPNOTSUPP;
}

static int
netvsc_set_rxnfc(struct net_device *ndev, struct ethtool_rxnfc *info)
{
	struct net_device_context *ndc = netdev_priv(ndev);

	if (info->cmd == ETHTOOL_SRXFH)
		return netvsc_set_rss_hash_opts(ndc, info);

	return -EOPNOTSUPP;
}

static u32 netvsc_get_rxfh_key_size(struct net_device *dev)
{
	return NETVSC_HASH_KEYLEN;
}

static u32 netvsc_rss_indir_size(struct net_device *dev)
{
	return ITAB_NUM;
}

static int netvsc_get_rxfh(struct net_device *dev, u32 *indir, u8 *key,
			   u8 *hfunc)
{
	struct net_device_context *ndc = netdev_priv(dev);
	struct netvsc_device *ndev = rtnl_dereference(ndc->nvdev);
	struct rndis_device *rndis_dev;
	int i;

	if (!ndev)
		return -ENODEV;

	if (hfunc)
		*hfunc = ETH_RSS_HASH_TOP;	/* Toeplitz */

	rndis_dev = ndev->extension;
	if (indir) {
		for (i = 0; i < ITAB_NUM; i++)
			indir[i] = ndc->rx_table[i];
	}

	if (key)
		memcpy(key, rndis_dev->rss_key, NETVSC_HASH_KEYLEN);

	return 0;
}

static int netvsc_set_rxfh(struct net_device *dev, const u32 *indir,
			   const u8 *key, const u8 hfunc)
{
	struct net_device_context *ndc = netdev_priv(dev);
	struct netvsc_device *ndev = rtnl_dereference(ndc->nvdev);
	struct rndis_device *rndis_dev;
	int i;

	if (!ndev)
		return -ENODEV;

	if (hfunc != ETH_RSS_HASH_NO_CHANGE && hfunc != ETH_RSS_HASH_TOP)
		return -EOPNOTSUPP;

	rndis_dev = ndev->extension;
	if (indir) {
		for (i = 0; i < ITAB_NUM; i++)
			if (indir[i] >= ndev->num_chn)
				return -EINVAL;

		for (i = 0; i < ITAB_NUM; i++)
			ndc->rx_table[i] = indir[i];
	}

	if (!key) {
		if (!indir)
			return 0;

		key = rndis_dev->rss_key;
	}

	return rndis_filter_set_rss_param(rndis_dev, key);
}

/* Hyper-V RNDIS protocol does not have ring in the HW sense.
 * It does have pre-allocated receive area which is divided into sections.
 */
static void __netvsc_get_ringparam(struct netvsc_device *nvdev,
				   struct ethtool_ringparam *ring)
{
	u32 max_buf_size;

	ring->rx_pending = nvdev->recv_section_cnt;
	ring->tx_pending = nvdev->send_section_cnt;

	if (nvdev->nvsp_version <= NVSP_PROTOCOL_VERSION_2)
		max_buf_size = NETVSC_RECEIVE_BUFFER_SIZE_LEGACY;
	else
		max_buf_size = NETVSC_RECEIVE_BUFFER_SIZE;

	ring->rx_max_pending = max_buf_size / nvdev->recv_section_size;
	ring->tx_max_pending = NETVSC_SEND_BUFFER_SIZE
		/ nvdev->send_section_size;
}

static void netvsc_get_ringparam(struct net_device *ndev,
				 struct ethtool_ringparam *ring)
{
	struct net_device_context *ndevctx = netdev_priv(ndev);
	struct netvsc_device *nvdev = rtnl_dereference(ndevctx->nvdev);

	if (!nvdev)
		return;

	__netvsc_get_ringparam(nvdev, ring);
}

static int netvsc_set_ringparam(struct net_device *ndev,
				struct ethtool_ringparam *ring)
{
	struct net_device_context *ndevctx = netdev_priv(ndev);
	struct netvsc_device *nvdev = rtnl_dereference(ndevctx->nvdev);
	struct netvsc_device_info *device_info;
	struct ethtool_ringparam orig;
	u32 new_tx, new_rx;
	int ret = 0;

	if (!nvdev || nvdev->destroy)
		return -ENODEV;

	memset(&orig, 0, sizeof(orig));
	__netvsc_get_ringparam(nvdev, &orig);

	new_tx = clamp_t(u32, ring->tx_pending,
			 NETVSC_MIN_TX_SECTIONS, orig.tx_max_pending);
	new_rx = clamp_t(u32, ring->rx_pending,
			 NETVSC_MIN_RX_SECTIONS, orig.rx_max_pending);

	if (new_tx == orig.tx_pending &&
	    new_rx == orig.rx_pending)
		return 0;	 /* no change */

	device_info = netvsc_devinfo_get(nvdev);

	if (!device_info)
		return -ENOMEM;

	device_info->send_sections = new_tx;
	device_info->recv_sections = new_rx;

	ret = netvsc_detach(ndev, nvdev);
	if (ret)
		goto out;

	ret = netvsc_attach(ndev, device_info);
	if (ret) {
		device_info->send_sections = orig.tx_pending;
		device_info->recv_sections = orig.rx_pending;

		if (netvsc_attach(ndev, device_info))
			netdev_err(ndev, "restoring ringparam failed");
	}

out:
	netvsc_devinfo_put(device_info);
	return ret;
}

static netdev_features_t netvsc_fix_features(struct net_device *ndev,
					     netdev_features_t features)
{
	struct net_device_context *ndevctx = netdev_priv(ndev);
	struct netvsc_device *nvdev = rtnl_dereference(ndevctx->nvdev);

	if (!nvdev || nvdev->destroy)
		return features;

	if ((features & NETIF_F_LRO) && netvsc_xdp_get(nvdev)) {
		features ^= NETIF_F_LRO;
		netdev_info(ndev, "Skip LRO - unsupported with XDP\n");
	}

	return features;
}

static int netvsc_set_features(struct net_device *ndev,
			       netdev_features_t features)
{
	netdev_features_t change = features ^ ndev->features;
	struct net_device_context *ndevctx = netdev_priv(ndev);
	struct netvsc_device *nvdev = rtnl_dereference(ndevctx->nvdev);
	struct net_device *vf_netdev = rtnl_dereference(ndevctx->vf_netdev);
	struct ndis_offload_params offloads;
	int ret = 0;

	if (!nvdev || nvdev->destroy)
		return -ENODEV;

	if (!(change & NETIF_F_LRO))
		goto syncvf;

	memset(&offloads, 0, sizeof(struct ndis_offload_params));

	if (features & NETIF_F_LRO) {
		offloads.rsc_ip_v4 = NDIS_OFFLOAD_PARAMETERS_RSC_ENABLED;
		offloads.rsc_ip_v6 = NDIS_OFFLOAD_PARAMETERS_RSC_ENABLED;
	} else {
		offloads.rsc_ip_v4 = NDIS_OFFLOAD_PARAMETERS_RSC_DISABLED;
		offloads.rsc_ip_v6 = NDIS_OFFLOAD_PARAMETERS_RSC_DISABLED;
	}

	ret = rndis_filter_set_offload_params(ndev, nvdev, &offloads);

	if (ret) {
		features ^= NETIF_F_LRO;
		ndev->features = features;
	}

syncvf:
	if (!vf_netdev)
		return ret;

	vf_netdev->wanted_features = features;
	netdev_update_features(vf_netdev);

	return ret;
}

static u32 netvsc_get_msglevel(struct net_device *ndev)
{
	struct net_device_context *ndev_ctx = netdev_priv(ndev);

	return ndev_ctx->msg_enable;
}

static void netvsc_set_msglevel(struct net_device *ndev, u32 val)
{
	struct net_device_context *ndev_ctx = netdev_priv(ndev);

	ndev_ctx->msg_enable = val;
}

static const struct ethtool_ops ethtool_ops = {
	.get_drvinfo	= netvsc_get_drvinfo,
	.get_msglevel	= netvsc_get_msglevel,
	.set_msglevel	= netvsc_set_msglevel,
	.get_link	= ethtool_op_get_link,
	.get_ethtool_stats = netvsc_get_ethtool_stats,
	.get_sset_count = netvsc_get_sset_count,
	.get_strings	= netvsc_get_strings,
	.get_channels   = netvsc_get_channels,
	.set_channels   = netvsc_set_channels,
	.get_ts_info	= ethtool_op_get_ts_info,
	.get_rxnfc	= netvsc_get_rxnfc,
	.set_rxnfc	= netvsc_set_rxnfc,
	.get_rxfh_key_size = netvsc_get_rxfh_key_size,
	.get_rxfh_indir_size = netvsc_rss_indir_size,
	.get_rxfh	= netvsc_get_rxfh,
	.set_rxfh	= netvsc_set_rxfh,
	.get_link_ksettings = netvsc_get_link_ksettings,
	.set_link_ksettings = netvsc_set_link_ksettings,
	.get_ringparam	= netvsc_get_ringparam,
	.set_ringparam	= netvsc_set_ringparam,
};

static const struct net_device_ops device_ops = {
	.ndo_open =			netvsc_open,
	.ndo_stop =			netvsc_close,
	.ndo_start_xmit =		netvsc_start_xmit,
	.ndo_change_rx_flags =		netvsc_change_rx_flags,
	.ndo_set_rx_mode =		netvsc_set_rx_mode,
	.ndo_fix_features =		netvsc_fix_features,
	.ndo_set_features =		netvsc_set_features,
	.ndo_change_mtu =		netvsc_change_mtu,
	.ndo_validate_addr =		eth_validate_addr,
	.ndo_set_mac_address =		netvsc_set_mac_addr,
	.ndo_select_queue =		netvsc_select_queue,
	.ndo_get_stats64 =		netvsc_get_stats64,
	.ndo_bpf =			netvsc_bpf,
};

/*
 * Handle link status changes. For RNDIS_STATUS_NETWORK_CHANGE emulate link
 * down/up sequence. In case of RNDIS_STATUS_MEDIA_CONNECT when carrier is
 * present send GARP packet to network peers with netif_notify_peers().
 */
static void netvsc_link_change(struct work_struct *w)
{
	struct net_device_context *ndev_ctx =
		container_of(w, struct net_device_context, dwork.work);
	struct hv_device *device_obj = ndev_ctx->device_ctx;
	struct net_device *net = hv_get_drvdata(device_obj);
	struct netvsc_device *net_device;
	struct rndis_device *rdev;
	struct netvsc_reconfig *event = NULL;
	bool notify = false, reschedule = false;
	unsigned long flags, next_reconfig, delay;

	/* if changes are happening, comeback later */
	if (!rtnl_trylock()) {
		schedule_delayed_work(&ndev_ctx->dwork, LINKCHANGE_INT);
		return;
	}

	net_device = rtnl_dereference(ndev_ctx->nvdev);
	if (!net_device)
		goto out_unlock;

	rdev = net_device->extension;

	next_reconfig = ndev_ctx->last_reconfig + LINKCHANGE_INT;
	if (time_is_after_jiffies(next_reconfig)) {
		/* link_watch only sends one notification with current state
		 * per second, avoid doing reconfig more frequently. Handle
		 * wrap around.
		 */
		delay = next_reconfig - jiffies;
		delay = delay < LINKCHANGE_INT ? delay : LINKCHANGE_INT;
		schedule_delayed_work(&ndev_ctx->dwork, delay);
		goto out_unlock;
	}
	ndev_ctx->last_reconfig = jiffies;

	spin_lock_irqsave(&ndev_ctx->lock, flags);
	if (!list_empty(&ndev_ctx->reconfig_events)) {
		event = list_first_entry(&ndev_ctx->reconfig_events,
					 struct netvsc_reconfig, list);
		list_del(&event->list);
		reschedule = !list_empty(&ndev_ctx->reconfig_events);
	}
	spin_unlock_irqrestore(&ndev_ctx->lock, flags);

	if (!event)
		goto out_unlock;

	switch (event->event) {
		/* Only the following events are possible due to the check in
		 * netvsc_linkstatus_callback()
		 */
	case RNDIS_STATUS_MEDIA_CONNECT:
		if (rdev->link_state) {
			rdev->link_state = false;
			netif_carrier_on(net);
			netvsc_tx_enable(net_device, net);
		} else {
			notify = true;
		}
		kfree(event);
		break;
	case RNDIS_STATUS_MEDIA_DISCONNECT:
		if (!rdev->link_state) {
			rdev->link_state = true;
			netif_carrier_off(net);
			netvsc_tx_disable(net_device, net);
		}
		kfree(event);
		break;
	case RNDIS_STATUS_NETWORK_CHANGE:
		/* Only makes sense if carrier is present */
		if (!rdev->link_state) {
			rdev->link_state = true;
			netif_carrier_off(net);
			netvsc_tx_disable(net_device, net);
			event->event = RNDIS_STATUS_MEDIA_CONNECT;
			spin_lock_irqsave(&ndev_ctx->lock, flags);
			list_add(&event->list, &ndev_ctx->reconfig_events);
			spin_unlock_irqrestore(&ndev_ctx->lock, flags);
			reschedule = true;
		}
		break;
	}

	rtnl_unlock();

	if (notify)
		netdev_notify_peers(net);

	/* link_watch only sends one notification with current state per
	 * second, handle next reconfig event in 2 seconds.
	 */
	if (reschedule)
		schedule_delayed_work(&ndev_ctx->dwork, LINKCHANGE_INT);

	return;

out_unlock:
	rtnl_unlock();
}

static struct net_device *get_netvsc_byref(struct net_device *vf_netdev)
{
	struct net_device_context *net_device_ctx;
	struct net_device *dev;

	dev = netdev_master_upper_dev_get(vf_netdev);
	if (!dev || dev->netdev_ops != &device_ops)
		return NULL;	/* not a netvsc device */

	net_device_ctx = netdev_priv(dev);
	if (!rtnl_dereference(net_device_ctx->nvdev))
		return NULL;	/* device is removed */

	return dev;
}

/* Called when VF is injecting data into network stack.
 * Change the associated network device from VF to netvsc.
 * note: already called with rcu_read_lock
 */
static rx_handler_result_t netvsc_vf_handle_frame(struct sk_buff **pskb)
{
	struct sk_buff *skb = *pskb;
	struct net_device *ndev = rcu_dereference(skb->dev->rx_handler_data);
	struct net_device_context *ndev_ctx = netdev_priv(ndev);
	struct netvsc_vf_pcpu_stats *pcpu_stats
		 = this_cpu_ptr(ndev_ctx->vf_stats);

	skb = skb_share_check(skb, GFP_ATOMIC);
	if (unlikely(!skb))
		return RX_HANDLER_CONSUMED;

	*pskb = skb;

	skb->dev = ndev;

	u64_stats_update_begin(&pcpu_stats->syncp);
	pcpu_stats->rx_packets++;
	pcpu_stats->rx_bytes += skb->len;
	u64_stats_update_end(&pcpu_stats->syncp);

	return RX_HANDLER_ANOTHER;
}

static int netvsc_vf_join(struct net_device *vf_netdev,
			  struct net_device *ndev)
{
	struct net_device_context *ndev_ctx = netdev_priv(ndev);
	int ret;

	ret = netdev_rx_handler_register(vf_netdev,
					 netvsc_vf_handle_frame, ndev);
	if (ret != 0) {
		netdev_err(vf_netdev,
			   "can not register netvsc VF receive handler (err = %d)\n",
			   ret);
		goto rx_handler_failed;
	}

	ret = netdev_master_upper_dev_link(vf_netdev, ndev,
					   NULL, NULL, NULL);
	if (ret != 0) {
		netdev_err(vf_netdev,
			   "can not set master device %s (err = %d)\n",
			   ndev->name, ret);
		goto upper_link_failed;
	}

	/* set slave flag before open to prevent IPv6 addrconf */
	vf_netdev->flags |= IFF_SLAVE;

	schedule_delayed_work(&ndev_ctx->vf_takeover, VF_TAKEOVER_INT);

	call_netdevice_notifiers(NETDEV_JOIN, vf_netdev);

	netdev_info(vf_netdev, "joined to %s\n", ndev->name);
	return 0;

upper_link_failed:
	netdev_rx_handler_unregister(vf_netdev);
rx_handler_failed:
	return ret;
}

static void __netvsc_vf_setup(struct net_device *ndev,
			      struct net_device *vf_netdev)
{
	int ret;

	/* Align MTU of VF with master */
	ret = dev_set_mtu(vf_netdev, ndev->mtu);
	if (ret)
		netdev_warn(vf_netdev,
			    "unable to change mtu to %u\n", ndev->mtu);

	/* set multicast etc flags on VF */
	dev_change_flags(vf_netdev, ndev->flags | IFF_SLAVE, NULL);

	/* sync address list from ndev to VF */
	netif_addr_lock_bh(ndev);
	dev_uc_sync(vf_netdev, ndev);
	dev_mc_sync(vf_netdev, ndev);
	netif_addr_unlock_bh(ndev);

	if (netif_running(ndev)) {
		ret = dev_open(vf_netdev, NULL);
		if (ret)
			netdev_warn(vf_netdev,
				    "unable to open: %d\n", ret);
	}
}

/* Setup VF as slave of the synthetic device.
 * Runs in workqueue to avoid recursion in netlink callbacks.
 */
static void netvsc_vf_setup(struct work_struct *w)
{
	struct net_device_context *ndev_ctx
		= container_of(w, struct net_device_context, vf_takeover.work);
	struct net_device *ndev = hv_get_drvdata(ndev_ctx->device_ctx);
	struct net_device *vf_netdev;

	if (!rtnl_trylock()) {
		schedule_delayed_work(&ndev_ctx->vf_takeover, 0);
		return;
	}

	vf_netdev = rtnl_dereference(ndev_ctx->vf_netdev);
	if (vf_netdev)
		__netvsc_vf_setup(ndev, vf_netdev);

	rtnl_unlock();
}

/* Find netvsc by VF serial number.
 * The PCI hyperv controller records the serial number as the slot kobj name.
 */
static struct net_device *get_netvsc_byslot(const struct net_device *vf_netdev)
{
	struct device *parent = vf_netdev->dev.parent;
	struct net_device_context *ndev_ctx;
	struct pci_dev *pdev;
	u32 serial;

	if (!parent || !dev_is_pci(parent))
		return NULL; /* not a PCI device */

	pdev = to_pci_dev(parent);
	if (!pdev->slot) {
		netdev_notice(vf_netdev, "no PCI slot information\n");
		return NULL;
	}

	if (kstrtou32(pci_slot_name(pdev->slot), 10, &serial)) {
		netdev_notice(vf_netdev, "Invalid vf serial:%s\n",
			      pci_slot_name(pdev->slot));
		return NULL;
	}

	list_for_each_entry(ndev_ctx, &netvsc_dev_list, list) {
		if (!ndev_ctx->vf_alloc)
			continue;

		if (ndev_ctx->vf_serial == serial)
			return hv_get_drvdata(ndev_ctx->device_ctx);
	}

	netdev_notice(vf_netdev,
		      "no netdev found for vf serial:%u\n", serial);
	return NULL;
}

static int netvsc_register_vf(struct net_device *vf_netdev)
{
	struct net_device_context *net_device_ctx;
	struct netvsc_device *netvsc_dev;
	struct bpf_prog *prog;
	struct net_device *ndev;
	int ret;

	if (vf_netdev->addr_len != ETH_ALEN)
		return NOTIFY_DONE;

	ndev = get_netvsc_byslot(vf_netdev);
	if (!ndev)
		return NOTIFY_DONE;

	net_device_ctx = netdev_priv(ndev);
	netvsc_dev = rtnl_dereference(net_device_ctx->nvdev);
	if (!netvsc_dev || rtnl_dereference(net_device_ctx->vf_netdev))
		return NOTIFY_DONE;

	/* if synthetic interface is a different namespace,
	 * then move the VF to that namespace; join will be
	 * done again in that context.
	 */
	if (!net_eq(dev_net(ndev), dev_net(vf_netdev))) {
		ret = dev_change_net_namespace(vf_netdev,
					       dev_net(ndev), "eth%d");
		if (ret)
			netdev_err(vf_netdev,
				   "could not move to same namespace as %s: %d\n",
				   ndev->name, ret);
		else
			netdev_info(vf_netdev,
				    "VF moved to namespace with: %s\n",
				    ndev->name);
		return NOTIFY_DONE;
	}

	netdev_info(ndev, "VF registering: %s\n", vf_netdev->name);

	if (netvsc_vf_join(vf_netdev, ndev) != 0)
		return NOTIFY_DONE;

	dev_hold(vf_netdev);
	rcu_assign_pointer(net_device_ctx->vf_netdev, vf_netdev);

	vf_netdev->wanted_features = ndev->features;
	netdev_update_features(vf_netdev);

	prog = netvsc_xdp_get(netvsc_dev);
	netvsc_vf_setxdp(vf_netdev, prog);

	return NOTIFY_OK;
}

/* VF up/down change detected, schedule to change data path */
static int netvsc_vf_changed(struct net_device *vf_netdev)
{
	struct net_device_context *net_device_ctx;
	struct netvsc_device *netvsc_dev;
	struct net_device *ndev;
	bool vf_is_up = netif_running(vf_netdev);

	ndev = get_netvsc_byref(vf_netdev);
	if (!ndev)
		return NOTIFY_DONE;

	net_device_ctx = netdev_priv(ndev);
	netvsc_dev = rtnl_dereference(net_device_ctx->nvdev);
	if (!netvsc_dev)
		return NOTIFY_DONE;

	netvsc_switch_datapath(ndev, vf_is_up);
	netdev_info(ndev, "Data path switched %s VF: %s\n",
		    vf_is_up ? "to" : "from", vf_netdev->name);

	return NOTIFY_OK;
}

static int netvsc_unregister_vf(struct net_device *vf_netdev)
{
	struct net_device *ndev;
	struct net_device_context *net_device_ctx;

	ndev = get_netvsc_byref(vf_netdev);
	if (!ndev)
		return NOTIFY_DONE;

	net_device_ctx = netdev_priv(ndev);
	cancel_delayed_work_sync(&net_device_ctx->vf_takeover);

	netdev_info(ndev, "VF unregistering: %s\n", vf_netdev->name);

	netvsc_vf_setxdp(vf_netdev, NULL);

	netdev_rx_handler_unregister(vf_netdev);
	netdev_upper_dev_unlink(vf_netdev, ndev);
	RCU_INIT_POINTER(net_device_ctx->vf_netdev, NULL);
	dev_put(vf_netdev);

	return NOTIFY_OK;
}

static int netvsc_probe(struct hv_device *dev,
			const struct hv_vmbus_device_id *dev_id)
{
	struct net_device *net = NULL;
	struct net_device_context *net_device_ctx;
	struct netvsc_device_info *device_info = NULL;
	struct netvsc_device *nvdev;
	int ret = -ENOMEM;

	net = alloc_etherdev_mq(sizeof(struct net_device_context),
				VRSS_CHANNEL_MAX);
	if (!net)
		goto no_net;

	netif_carrier_off(net);

	netvsc_init_settings(net);

	net_device_ctx = netdev_priv(net);
	net_device_ctx->device_ctx = dev;
	net_device_ctx->msg_enable = netif_msg_init(debug, default_msg);
	if (netif_msg_probe(net_device_ctx))
		netdev_dbg(net, "netvsc msg_enable: %d\n",
			   net_device_ctx->msg_enable);

	hv_set_drvdata(dev, net);

	INIT_DELAYED_WORK(&net_device_ctx->dwork, netvsc_link_change);

	spin_lock_init(&net_device_ctx->lock);
	INIT_LIST_HEAD(&net_device_ctx->reconfig_events);
	INIT_DELAYED_WORK(&net_device_ctx->vf_takeover, netvsc_vf_setup);

	net_device_ctx->vf_stats
		= netdev_alloc_pcpu_stats(struct netvsc_vf_pcpu_stats);
	if (!net_device_ctx->vf_stats)
		goto no_stats;

	net->netdev_ops = &device_ops;
	net->ethtool_ops = &ethtool_ops;
	SET_NETDEV_DEV(net, &dev->device);

	/* We always need headroom for rndis header */
	net->needed_headroom = RNDIS_AND_PPI_SIZE;

	/* Initialize the number of queues to be 1, we may change it if more
	 * channels are offered later.
	 */
	netif_set_real_num_tx_queues(net, 1);
	netif_set_real_num_rx_queues(net, 1);

	/* Notify the netvsc driver of the new device */
	device_info = netvsc_devinfo_get(NULL);

	if (!device_info) {
		ret = -ENOMEM;
		goto devinfo_failed;
	}

	nvdev = rndis_filter_device_add(dev, device_info);
	if (IS_ERR(nvdev)) {
		ret = PTR_ERR(nvdev);
		netdev_err(net, "unable to add netvsc device (ret %d)\n", ret);
		goto rndis_failed;
	}

	memcpy(net->dev_addr, device_info->mac_adr, ETH_ALEN);

	/* We must get rtnl lock before scheduling nvdev->subchan_work,
	 * otherwise netvsc_subchan_work() can get rtnl lock first and wait
	 * all subchannels to show up, but that may not happen because
	 * netvsc_probe() can't get rtnl lock and as a result vmbus_onoffer()
	 * -> ... -> device_add() -> ... -> __device_attach() can't get
	 * the device lock, so all the subchannels can't be processed --
	 * finally netvsc_subchan_work() hangs forever.
	 */
	rtnl_lock();

	if (nvdev->num_chn > 1)
		schedule_work(&nvdev->subchan_work);

	/* hw_features computed in rndis_netdev_set_hwcaps() */
	net->features = net->hw_features |
		NETIF_F_HIGHDMA | NETIF_F_HW_VLAN_CTAG_TX |
		NETIF_F_HW_VLAN_CTAG_RX;
	net->vlan_features = net->features;

	/* MTU range: 68 - 1500 or 65521 */
	net->min_mtu = NETVSC_MTU_MIN;
	if (nvdev->nvsp_version >= NVSP_PROTOCOL_VERSION_2)
		net->max_mtu = NETVSC_MTU - ETH_HLEN;
	else
		net->max_mtu = ETH_DATA_LEN;

	ret = register_netdevice(net);
	if (ret != 0) {
		pr_err("Unable to register netdev.\n");
		goto register_failed;
	}

	list_add(&net_device_ctx->list, &netvsc_dev_list);
	rtnl_unlock();

	netvsc_devinfo_put(device_info);
	return 0;

register_failed:
	rtnl_unlock();
	rndis_filter_device_remove(dev, nvdev);
rndis_failed:
	netvsc_devinfo_put(device_info);
devinfo_failed:
	free_percpu(net_device_ctx->vf_stats);
no_stats:
	hv_set_drvdata(dev, NULL);
	free_netdev(net);
no_net:
	return ret;
}

static int netvsc_remove(struct hv_device *dev)
{
	struct net_device_context *ndev_ctx;
	struct net_device *vf_netdev, *net;
	struct netvsc_device *nvdev;

	net = hv_get_drvdata(dev);
	if (net == NULL) {
		dev_err(&dev->device, "No net device to remove\n");
		return 0;
	}

	ndev_ctx = netdev_priv(net);

	cancel_delayed_work_sync(&ndev_ctx->dwork);

	rtnl_lock();
	nvdev = rtnl_dereference(ndev_ctx->nvdev);
	if (nvdev) {
		cancel_work_sync(&nvdev->subchan_work);
		netvsc_xdp_set(net, NULL, NULL, nvdev);
	}

	/*
	 * Call to the vsc driver to let it know that the device is being
	 * removed. Also blocks mtu and channel changes.
	 */
	vf_netdev = rtnl_dereference(ndev_ctx->vf_netdev);
	if (vf_netdev)
		netvsc_unregister_vf(vf_netdev);

	if (nvdev)
		rndis_filter_device_remove(dev, nvdev);

	unregister_netdevice(net);
	list_del(&ndev_ctx->list);

	rtnl_unlock();

	hv_set_drvdata(dev, NULL);

	free_percpu(ndev_ctx->vf_stats);
	free_netdev(net);
	return 0;
}

static int netvsc_suspend(struct hv_device *dev)
{
	struct net_device_context *ndev_ctx;
	struct net_device *vf_netdev, *net;
	struct netvsc_device *nvdev;
	int ret;

	net = hv_get_drvdata(dev);

	ndev_ctx = netdev_priv(net);
	cancel_delayed_work_sync(&ndev_ctx->dwork);

	rtnl_lock();

	nvdev = rtnl_dereference(ndev_ctx->nvdev);
	if (nvdev == NULL) {
		ret = -ENODEV;
		goto out;
	}

	vf_netdev = rtnl_dereference(ndev_ctx->vf_netdev);
	if (vf_netdev)
		netvsc_unregister_vf(vf_netdev);

	/* Save the current config info */
	ndev_ctx->saved_netvsc_dev_info = netvsc_devinfo_get(nvdev);

	ret = netvsc_detach(net, nvdev);
out:
	rtnl_unlock();

	return ret;
}

static int netvsc_resume(struct hv_device *dev)
{
	struct net_device *net = hv_get_drvdata(dev);
	struct net_device_context *net_device_ctx;
	struct netvsc_device_info *device_info;
	int ret;

	rtnl_lock();

	net_device_ctx = netdev_priv(net);
	device_info = net_device_ctx->saved_netvsc_dev_info;

	ret = netvsc_attach(net, device_info);

	netvsc_devinfo_put(device_info);
	net_device_ctx->saved_netvsc_dev_info = NULL;

	rtnl_unlock();

	return ret;
}
static const struct hv_vmbus_device_id id_table[] = {
	/* Network guid */
	{ HV_NIC_GUID, },
	{ },
};

MODULE_DEVICE_TABLE(vmbus, id_table);

/* The one and only one */
static struct  hv_driver netvsc_drv = {
	.name = KBUILD_MODNAME,
	.id_table = id_table,
	.probe = netvsc_probe,
	.remove = netvsc_remove,
	.suspend = netvsc_suspend,
	.resume = netvsc_resume,
	.driver = {
		.probe_type = PROBE_FORCE_SYNCHRONOUS,
	},
};

/*
 * On Hyper-V, every VF interface is matched with a corresponding
 * synthetic interface. The synthetic interface is presented first
 * to the guest. When the corresponding VF instance is registered,
 * we will take care of switching the data path.
 */
static int netvsc_netdev_event(struct notifier_block *this,
			       unsigned long event, void *ptr)
{
	struct net_device *event_dev = netdev_notifier_info_to_dev(ptr);

	/* Skip our own events */
	if (event_dev->netdev_ops == &device_ops)
		return NOTIFY_DONE;

	/* Avoid non-Ethernet type devices */
	if (event_dev->type != ARPHRD_ETHER)
		return NOTIFY_DONE;

	/* Avoid Vlan dev with same MAC registering as VF */
	if (is_vlan_dev(event_dev))
		return NOTIFY_DONE;

	/* Avoid Bonding master dev with same MAC registering as VF */
	if ((event_dev->priv_flags & IFF_BONDING) &&
	    (event_dev->flags & IFF_MASTER))
		return NOTIFY_DONE;

	switch (event) {
	case NETDEV_REGISTER:
		return netvsc_register_vf(event_dev);
	case NETDEV_UNREGISTER:
		return netvsc_unregister_vf(event_dev);
	case NETDEV_UP:
	case NETDEV_DOWN:
		return netvsc_vf_changed(event_dev);
	default:
		return NOTIFY_DONE;
	}
}

static struct notifier_block netvsc_netdev_notifier = {
	.notifier_call = netvsc_netdev_event,
};

static void __exit netvsc_drv_exit(void)
{
	unregister_netdevice_notifier(&netvsc_netdev_notifier);
	vmbus_driver_unregister(&netvsc_drv);
}

static int __init netvsc_drv_init(void)
{
	int ret;

	if (ring_size < RING_SIZE_MIN) {
		ring_size = RING_SIZE_MIN;
		pr_info("Increased ring_size to %u (min allowed)\n",
			ring_size);
	}
	netvsc_ring_bytes = ring_size * PAGE_SIZE;

	ret = vmbus_driver_register(&netvsc_drv);
	if (ret)
		return ret;

	register_netdevice_notifier(&netvsc_netdev_notifier);
	return 0;
}

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Microsoft Hyper-V network driver");

module_init(netvsc_drv_init);
module_exit(netvsc_drv_exit);
