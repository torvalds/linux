/*
 * Copyright (c) 2009, Microsoft Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, see <http://www.gnu.org/licenses/>.
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
#include <linux/skbuff.h>
#include <linux/if_vlan.h>
#include <linux/in.h>
#include <linux/slab.h>
#include <net/arp.h>
#include <net/route.h>
#include <net/sock.h>
#include <net/pkt_sched.h>

#include "hyperv_net.h"

struct net_device_context {
	/* point back to our device context */
	struct hv_device *device_ctx;
	struct delayed_work dwork;
	struct work_struct work;
};

#define RING_SIZE_MIN 64
static int ring_size = 128;
module_param(ring_size, int, S_IRUGO);
MODULE_PARM_DESC(ring_size, "Ring buffer size (# of pages)");

static void do_set_multicast(struct work_struct *w)
{
	struct net_device_context *ndevctx =
		container_of(w, struct net_device_context, work);
	struct netvsc_device *nvdev;
	struct rndis_device *rdev;

	nvdev = hv_get_drvdata(ndevctx->device_ctx);
	if (nvdev == NULL || nvdev->ndev == NULL)
		return;

	rdev = nvdev->extension;
	if (rdev == NULL)
		return;

	if (nvdev->ndev->flags & IFF_PROMISC)
		rndis_filter_set_packet_filter(rdev,
			NDIS_PACKET_TYPE_PROMISCUOUS);
	else
		rndis_filter_set_packet_filter(rdev,
			NDIS_PACKET_TYPE_BROADCAST |
			NDIS_PACKET_TYPE_ALL_MULTICAST |
			NDIS_PACKET_TYPE_DIRECTED);
}

static void netvsc_set_multicast_list(struct net_device *net)
{
	struct net_device_context *net_device_ctx = netdev_priv(net);

	schedule_work(&net_device_ctx->work);
}

static int netvsc_open(struct net_device *net)
{
	struct net_device_context *net_device_ctx = netdev_priv(net);
	struct hv_device *device_obj = net_device_ctx->device_ctx;
	struct netvsc_device *nvdev;
	struct rndis_device *rdev;
	int ret = 0;

	netif_carrier_off(net);

	/* Open up the device */
	ret = rndis_filter_open(device_obj);
	if (ret != 0) {
		netdev_err(net, "unable to open device (ret %d).\n", ret);
		return ret;
	}

	netif_tx_start_all_queues(net);

	nvdev = hv_get_drvdata(device_obj);
	rdev = nvdev->extension;
	if (!rdev->link_state)
		netif_carrier_on(net);

	return ret;
}

static int netvsc_close(struct net_device *net)
{
	struct net_device_context *net_device_ctx = netdev_priv(net);
	struct hv_device *device_obj = net_device_ctx->device_ctx;
	int ret;

	netif_tx_disable(net);

	/* Make sure netvsc_set_multicast_list doesn't re-enable filter! */
	cancel_work_sync(&net_device_ctx->work);
	ret = rndis_filter_close(device_obj);
	if (ret != 0)
		netdev_err(net, "unable to close device (ret %d).\n", ret);

	return ret;
}

static void *init_ppi_data(struct rndis_message *msg, u32 ppi_size,
				int pkt_type)
{
	struct rndis_packet *rndis_pkt;
	struct rndis_per_packet_info *ppi;

	rndis_pkt = &msg->msg.pkt;
	rndis_pkt->data_offset += ppi_size;

	ppi = (struct rndis_per_packet_info *)((void *)rndis_pkt +
		rndis_pkt->per_pkt_info_offset + rndis_pkt->per_pkt_info_len);

	ppi->size = ppi_size;
	ppi->type = pkt_type;
	ppi->ppi_offset = sizeof(struct rndis_per_packet_info);

	rndis_pkt->per_pkt_info_len += ppi_size;

	return ppi;
}

union sub_key {
	u64 k;
	struct {
		u8 pad[3];
		u8 kb;
		u32 ka;
	};
};

/* Toeplitz hash function
 * data: network byte order
 * return: host byte order
 */
static u32 comp_hash(u8 *key, int klen, void *data, int dlen)
{
	union sub_key subk;
	int k_next = 4;
	u8 dt;
	int i, j;
	u32 ret = 0;

	subk.k = 0;
	subk.ka = ntohl(*(u32 *)key);

	for (i = 0; i < dlen; i++) {
		subk.kb = key[k_next];
		k_next = (k_next + 1) % klen;
		dt = ((u8 *)data)[i];
		for (j = 0; j < 8; j++) {
			if (dt & 0x80)
				ret ^= subk.ka;
			dt <<= 1;
			subk.k <<= 1;
		}
	}

	return ret;
}

static bool netvsc_set_hash(u32 *hash, struct sk_buff *skb)
{
	struct flow_keys flow;
	int data_len;

	if (!skb_flow_dissect(skb, &flow) || flow.n_proto != htons(ETH_P_IP))
		return false;

	if (flow.ip_proto == IPPROTO_TCP)
		data_len = 12;
	else
		data_len = 8;

	*hash = comp_hash(netvsc_hash_key, HASH_KEYLEN, &flow, data_len);

	return true;
}

static u16 netvsc_select_queue(struct net_device *ndev, struct sk_buff *skb,
			void *accel_priv, select_queue_fallback_t fallback)
{
	struct net_device_context *net_device_ctx = netdev_priv(ndev);
	struct hv_device *hdev =  net_device_ctx->device_ctx;
	struct netvsc_device *nvsc_dev = hv_get_drvdata(hdev);
	u32 hash;
	u16 q_idx = 0;

	if (nvsc_dev == NULL || ndev->real_num_tx_queues <= 1)
		return 0;

	if (netvsc_set_hash(&hash, skb)) {
		q_idx = nvsc_dev->send_table[hash % VRSS_SEND_TAB_SIZE] %
			ndev->real_num_tx_queues;
		skb_set_hash(skb, hash, PKT_HASH_TYPE_L3);
	}

	return q_idx;
}

static void netvsc_xmit_completion(void *context)
{
	struct hv_netvsc_packet *packet = (struct hv_netvsc_packet *)context;
	struct sk_buff *skb = (struct sk_buff *)
		(unsigned long)packet->send_completion_tid;
	u32 index = packet->send_buf_index;

	kfree(packet);

	if (skb && (index == NETVSC_INVALID_INDEX))
		dev_kfree_skb_any(skb);
}

static u32 fill_pg_buf(struct page *page, u32 offset, u32 len,
			struct hv_page_buffer *pb)
{
	int j = 0;

	/* Deal with compund pages by ignoring unused part
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
			   struct hv_page_buffer *pb)
{
	u32 slots_used = 0;
	char *data = skb->data;
	int frags = skb_shinfo(skb)->nr_frags;
	int i;

	/* The packet is laid out thus:
	 * 1. hdr
	 * 2. skb linear data
	 * 3. skb fragment data
	 */
	if (hdr != NULL)
		slots_used += fill_pg_buf(virt_to_page(hdr),
					offset_in_page(hdr),
					len, &pb[slots_used]);

	slots_used += fill_pg_buf(virt_to_page(data),
				offset_in_page(data),
				skb_headlen(skb), &pb[slots_used]);

	for (i = 0; i < frags; i++) {
		skb_frag_t *frag = skb_shinfo(skb)->frags + i;

		slots_used += fill_pg_buf(skb_frag_page(frag),
					frag->page_offset,
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
		unsigned long offset = frag->page_offset;

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

static u32 get_net_transport_info(struct sk_buff *skb, u32 *trans_off)
{
	u32 ret_val = TRANSPORT_INFO_NOT_IP;

	if ((eth_hdr(skb)->h_proto != htons(ETH_P_IP)) &&
		(eth_hdr(skb)->h_proto != htons(ETH_P_IPV6))) {
		goto not_ip;
	}

	*trans_off = skb_transport_offset(skb);

	if ((eth_hdr(skb)->h_proto == htons(ETH_P_IP))) {
		struct iphdr *iphdr = ip_hdr(skb);

		if (iphdr->protocol == IPPROTO_TCP)
			ret_val = TRANSPORT_INFO_IPV4_TCP;
		else if (iphdr->protocol == IPPROTO_UDP)
			ret_val = TRANSPORT_INFO_IPV4_UDP;
	} else {
		if (ipv6_hdr(skb)->nexthdr == IPPROTO_TCP)
			ret_val = TRANSPORT_INFO_IPV6_TCP;
		else if (ipv6_hdr(skb)->nexthdr == IPPROTO_UDP)
			ret_val = TRANSPORT_INFO_IPV6_UDP;
	}

not_ip:
	return ret_val;
}

static int netvsc_start_xmit(struct sk_buff *skb, struct net_device *net)
{
	struct net_device_context *net_device_ctx = netdev_priv(net);
	struct hv_netvsc_packet *packet;
	int ret;
	unsigned int num_data_pgs;
	struct rndis_message *rndis_msg;
	struct rndis_packet *rndis_pkt;
	u32 rndis_msg_size;
	bool isvlan;
	struct rndis_per_packet_info *ppi;
	struct ndis_tcp_ip_checksum_info *csum_info;
	struct ndis_tcp_lso_info *lso_info;
	int  hdr_offset;
	u32 net_trans_info;
	u32 hash;
	u32 skb_length = skb->len;


	/* We will atmost need two pages to describe the rndis
	 * header. We can only transmit MAX_PAGE_BUFFER_COUNT number
	 * of pages in a single packet.
	 */
	num_data_pgs = netvsc_get_slots(skb) + 2;
	if (num_data_pgs > MAX_PAGE_BUFFER_COUNT) {
		netdev_err(net, "Packet too big: %u\n", skb->len);
		dev_kfree_skb(skb);
		net->stats.tx_dropped++;
		return NETDEV_TX_OK;
	}

	/* Allocate a netvsc packet based on # of frags. */
	packet = kzalloc(sizeof(struct hv_netvsc_packet) +
			 (num_data_pgs * sizeof(struct hv_page_buffer)) +
			 sizeof(struct rndis_message) +
			 NDIS_VLAN_PPI_SIZE + NDIS_CSUM_PPI_SIZE +
			 NDIS_LSO_PPI_SIZE + NDIS_HASH_PPI_SIZE, GFP_ATOMIC);
	if (!packet) {
		/* out of memory, drop packet */
		netdev_err(net, "unable to allocate hv_netvsc_packet\n");

		dev_kfree_skb(skb);
		net->stats.tx_dropped++;
		return NETDEV_TX_OK;
	}

	packet->vlan_tci = skb->vlan_tci;

	packet->q_idx = skb_get_queue_mapping(skb);

	packet->is_data_pkt = true;
	packet->total_data_buflen = skb->len;

	packet->rndis_msg = (struct rndis_message *)((unsigned long)packet +
				sizeof(struct hv_netvsc_packet) +
				(num_data_pgs * sizeof(struct hv_page_buffer)));

	/* Set the completion routine */
	packet->send_completion = netvsc_xmit_completion;
	packet->send_completion_ctx = packet;
	packet->send_completion_tid = (unsigned long)skb;

	isvlan = packet->vlan_tci & VLAN_TAG_PRESENT;

	/* Add the rndis header */
	rndis_msg = packet->rndis_msg;
	rndis_msg->ndis_msg_type = RNDIS_MSG_PACKET;
	rndis_msg->msg_len = packet->total_data_buflen;
	rndis_pkt = &rndis_msg->msg.pkt;
	rndis_pkt->data_offset = sizeof(struct rndis_packet);
	rndis_pkt->data_len = packet->total_data_buflen;
	rndis_pkt->per_pkt_info_offset = sizeof(struct rndis_packet);

	rndis_msg_size = RNDIS_MESSAGE_SIZE(struct rndis_packet);

	hash = skb_get_hash_raw(skb);
	if (hash != 0 && net->real_num_tx_queues > 1) {
		rndis_msg_size += NDIS_HASH_PPI_SIZE;
		ppi = init_ppi_data(rndis_msg, NDIS_HASH_PPI_SIZE,
				    NBL_HASH_VALUE);
		*(u32 *)((void *)ppi + ppi->ppi_offset) = hash;
	}

	if (isvlan) {
		struct ndis_pkt_8021q_info *vlan;

		rndis_msg_size += NDIS_VLAN_PPI_SIZE;
		ppi = init_ppi_data(rndis_msg, NDIS_VLAN_PPI_SIZE,
					IEEE_8021Q_INFO);
		vlan = (struct ndis_pkt_8021q_info *)((void *)ppi +
						ppi->ppi_offset);
		vlan->vlanid = packet->vlan_tci & VLAN_VID_MASK;
		vlan->pri = (packet->vlan_tci & VLAN_PRIO_MASK) >>
				VLAN_PRIO_SHIFT;
	}

	net_trans_info = get_net_transport_info(skb, &hdr_offset);
	if (net_trans_info == TRANSPORT_INFO_NOT_IP)
		goto do_send;

	/*
	 * Setup the sendside checksum offload only if this is not a
	 * GSO packet.
	 */
	if (skb_is_gso(skb))
		goto do_lso;

	if ((skb->ip_summed == CHECKSUM_NONE) ||
	    (skb->ip_summed == CHECKSUM_UNNECESSARY))
		goto do_send;

	rndis_msg_size += NDIS_CSUM_PPI_SIZE;
	ppi = init_ppi_data(rndis_msg, NDIS_CSUM_PPI_SIZE,
			    TCPIP_CHKSUM_PKTINFO);

	csum_info = (struct ndis_tcp_ip_checksum_info *)((void *)ppi +
			ppi->ppi_offset);

	if (net_trans_info & (INFO_IPV4 << 16))
		csum_info->transmit.is_ipv4 = 1;
	else
		csum_info->transmit.is_ipv6 = 1;

	if (net_trans_info & INFO_TCP) {
		csum_info->transmit.tcp_checksum = 1;
		csum_info->transmit.tcp_header_offset = hdr_offset;
	} else if (net_trans_info & INFO_UDP) {
		/* UDP checksum offload is not supported on ws2008r2.
		 * Furthermore, on ws2012 and ws2012r2, there are some
		 * issues with udp checksum offload from Linux guests.
		 * (these are host issues).
		 * For now compute the checksum here.
		 */
		struct udphdr *uh;
		u16 udp_len;

		ret = skb_cow_head(skb, 0);
		if (ret)
			goto drop;

		uh = udp_hdr(skb);
		udp_len = ntohs(uh->len);
		uh->check = 0;
		uh->check = csum_tcpudp_magic(ip_hdr(skb)->saddr,
					      ip_hdr(skb)->daddr,
					      udp_len, IPPROTO_UDP,
					      csum_partial(uh, udp_len, 0));
		if (uh->check == 0)
			uh->check = CSUM_MANGLED_0;

		csum_info->transmit.udp_checksum = 0;
	}
	goto do_send;

do_lso:
	rndis_msg_size += NDIS_LSO_PPI_SIZE;
	ppi = init_ppi_data(rndis_msg, NDIS_LSO_PPI_SIZE,
			    TCP_LARGESEND_PKTINFO);

	lso_info = (struct ndis_tcp_lso_info *)((void *)ppi +
			ppi->ppi_offset);

	lso_info->lso_v2_transmit.type = NDIS_TCP_LARGE_SEND_OFFLOAD_V2_TYPE;
	if (net_trans_info & (INFO_IPV4 << 16)) {
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
	lso_info->lso_v2_transmit.tcp_header_offset = hdr_offset;
	lso_info->lso_v2_transmit.mss = skb_shinfo(skb)->gso_size;

do_send:
	/* Start filling in the page buffers with the rndis hdr */
	rndis_msg->msg_len += rndis_msg_size;
	packet->total_data_buflen = rndis_msg->msg_len;
	packet->page_buf_cnt = init_page_array(rndis_msg, rndis_msg_size,
					skb, &packet->page_buf[0]);

	ret = netvsc_send(net_device_ctx->device_ctx, packet);

drop:
	if (ret == 0) {
		net->stats.tx_bytes += skb_length;
		net->stats.tx_packets++;
	} else {
		kfree(packet);
		if (ret != -EAGAIN) {
			dev_kfree_skb_any(skb);
			net->stats.tx_dropped++;
		}
	}

	return (ret == -EAGAIN) ? NETDEV_TX_BUSY : NETDEV_TX_OK;
}

/*
 * netvsc_linkstatus_callback - Link up/down notification
 */
void netvsc_linkstatus_callback(struct hv_device *device_obj,
				struct rndis_message *resp)
{
	struct rndis_indicate_status *indicate = &resp->msg.indicate_status;
	struct net_device *net;
	struct net_device_context *ndev_ctx;
	struct netvsc_device *net_device;
	struct rndis_device *rdev;

	net_device = hv_get_drvdata(device_obj);
	rdev = net_device->extension;

	switch (indicate->status) {
	case RNDIS_STATUS_MEDIA_CONNECT:
		rdev->link_state = false;
		break;
	case RNDIS_STATUS_MEDIA_DISCONNECT:
		rdev->link_state = true;
		break;
	case RNDIS_STATUS_NETWORK_CHANGE:
		rdev->link_change = true;
		break;
	default:
		return;
	}

	net = net_device->ndev;

	if (!net || net->reg_state != NETREG_REGISTERED)
		return;

	ndev_ctx = netdev_priv(net);
	if (!rdev->link_state) {
		schedule_delayed_work(&ndev_ctx->dwork, 0);
		schedule_delayed_work(&ndev_ctx->dwork, msecs_to_jiffies(20));
	} else {
		schedule_delayed_work(&ndev_ctx->dwork, 0);
	}
}

/*
 * netvsc_recv_callback -  Callback when we receive a packet from the
 * "wire" on the specified device.
 */
int netvsc_recv_callback(struct hv_device *device_obj,
				struct hv_netvsc_packet *packet,
				struct ndis_tcp_ip_checksum_info *csum_info)
{
	struct net_device *net;
	struct sk_buff *skb;

	net = ((struct netvsc_device *)hv_get_drvdata(device_obj))->ndev;
	if (!net || net->reg_state != NETREG_REGISTERED) {
		packet->status = NVSP_STAT_FAIL;
		return 0;
	}

	/* Allocate a skb - TODO direct I/O to pages? */
	skb = netdev_alloc_skb_ip_align(net, packet->total_data_buflen);
	if (unlikely(!skb)) {
		++net->stats.rx_dropped;
		packet->status = NVSP_STAT_FAIL;
		return 0;
	}

	/*
	 * Copy to skb. This copy is needed here since the memory pointed by
	 * hv_netvsc_packet cannot be deallocated
	 */
	memcpy(skb_put(skb, packet->total_data_buflen), packet->data,
		packet->total_data_buflen);

	skb->protocol = eth_type_trans(skb, net);
	if (csum_info) {
		/* We only look at the IP checksum here.
		 * Should we be dropping the packet if checksum
		 * failed? How do we deal with other checksums - TCP/UDP?
		 */
		if (csum_info->receive.ip_checksum_succeeded)
			skb->ip_summed = CHECKSUM_UNNECESSARY;
		else
			skb->ip_summed = CHECKSUM_NONE;
	}

	if (packet->vlan_tci & VLAN_TAG_PRESENT)
		__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q),
				       packet->vlan_tci);

	skb_record_rx_queue(skb, packet->channel->
			    offermsg.offer.sub_channel_index);

	net->stats.rx_packets++;
	net->stats.rx_bytes += packet->total_data_buflen;

	/*
	 * Pass the skb back up. Network stack will deallocate the skb when it
	 * is done.
	 * TODO - use NAPI?
	 */
	netif_rx(skb);

	return 0;
}

static void netvsc_get_drvinfo(struct net_device *net,
			       struct ethtool_drvinfo *info)
{
	strlcpy(info->driver, KBUILD_MODNAME, sizeof(info->driver));
	strlcpy(info->fw_version, "N/A", sizeof(info->fw_version));
}

static int netvsc_change_mtu(struct net_device *ndev, int mtu)
{
	struct net_device_context *ndevctx = netdev_priv(ndev);
	struct hv_device *hdev =  ndevctx->device_ctx;
	struct netvsc_device *nvdev = hv_get_drvdata(hdev);
	struct netvsc_device_info device_info;
	int limit = ETH_DATA_LEN;

	if (nvdev == NULL || nvdev->destroy)
		return -ENODEV;

	if (nvdev->nvsp_version >= NVSP_PROTOCOL_VERSION_2)
		limit = NETVSC_MTU;

	if (mtu < 68 || mtu > limit)
		return -EINVAL;

	nvdev->start_remove = true;
	cancel_work_sync(&ndevctx->work);
	netif_tx_disable(ndev);
	rndis_filter_device_remove(hdev);

	ndev->mtu = mtu;

	ndevctx->device_ctx = hdev;
	hv_set_drvdata(hdev, ndev);
	device_info.ring_size = ring_size;
	rndis_filter_device_add(hdev, &device_info);
	netif_tx_wake_all_queues(ndev);

	return 0;
}


static int netvsc_set_mac_addr(struct net_device *ndev, void *p)
{
	struct net_device_context *ndevctx = netdev_priv(ndev);
	struct hv_device *hdev =  ndevctx->device_ctx;
	struct sockaddr *addr = p;
	char save_adr[ETH_ALEN];
	unsigned char save_aatype;
	int err;

	memcpy(save_adr, ndev->dev_addr, ETH_ALEN);
	save_aatype = ndev->addr_assign_type;

	err = eth_mac_addr(ndev, p);
	if (err != 0)
		return err;

	err = rndis_filter_set_device_mac(hdev, addr->sa_data);
	if (err != 0) {
		/* roll back to saved MAC */
		memcpy(ndev->dev_addr, save_adr, ETH_ALEN);
		ndev->addr_assign_type = save_aatype;
	}

	return err;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void netvsc_poll_controller(struct net_device *net)
{
	/* As netvsc_start_xmit() works synchronous we don't have to
	 * trigger anything here.
	 */
}
#endif

static const struct ethtool_ops ethtool_ops = {
	.get_drvinfo	= netvsc_get_drvinfo,
	.get_link	= ethtool_op_get_link,
};

static const struct net_device_ops device_ops = {
	.ndo_open =			netvsc_open,
	.ndo_stop =			netvsc_close,
	.ndo_start_xmit =		netvsc_start_xmit,
	.ndo_set_rx_mode =		netvsc_set_multicast_list,
	.ndo_change_mtu =		netvsc_change_mtu,
	.ndo_validate_addr =		eth_validate_addr,
	.ndo_set_mac_address =		netvsc_set_mac_addr,
	.ndo_select_queue =		netvsc_select_queue,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller =		netvsc_poll_controller,
#endif
};

/*
 * Send GARP packet to network peers after migrations.
 * After Quick Migration, the network is not immediately operational in the
 * current context when receiving RNDIS_STATUS_MEDIA_CONNECT event. So, add
 * another netif_notify_peers() into a delayed work, otherwise GARP packet
 * will not be sent after quick migration, and cause network disconnection.
 * Also, we update the carrier status here.
 */
static void netvsc_link_change(struct work_struct *w)
{
	struct net_device_context *ndev_ctx;
	struct net_device *net;
	struct netvsc_device *net_device;
	struct rndis_device *rdev;
	bool notify, refresh = false;
	char *argv[] = { "/etc/init.d/network", "restart", NULL };
	char *envp[] = { "HOME=/", "PATH=/sbin:/usr/sbin:/bin:/usr/bin", NULL };

	rtnl_lock();

	ndev_ctx = container_of(w, struct net_device_context, dwork.work);
	net_device = hv_get_drvdata(ndev_ctx->device_ctx);
	rdev = net_device->extension;
	net = net_device->ndev;

	if (rdev->link_state) {
		netif_carrier_off(net);
		notify = false;
	} else {
		netif_carrier_on(net);
		notify = true;
		if (rdev->link_change) {
			rdev->link_change = false;
			refresh = true;
		}
	}

	rtnl_unlock();

	if (refresh)
		call_usermodehelper(argv[0], argv, envp, UMH_WAIT_EXEC);

	if (notify)
		netdev_notify_peers(net);
}


static int netvsc_probe(struct hv_device *dev,
			const struct hv_vmbus_device_id *dev_id)
{
	struct net_device *net = NULL;
	struct net_device_context *net_device_ctx;
	struct netvsc_device_info device_info;
	struct netvsc_device *nvdev;
	int ret;

	net = alloc_etherdev_mq(sizeof(struct net_device_context),
				num_online_cpus());
	if (!net)
		return -ENOMEM;

	netif_carrier_off(net);

	net_device_ctx = netdev_priv(net);
	net_device_ctx->device_ctx = dev;
	hv_set_drvdata(dev, net);
	INIT_DELAYED_WORK(&net_device_ctx->dwork, netvsc_link_change);
	INIT_WORK(&net_device_ctx->work, do_set_multicast);

	net->netdev_ops = &device_ops;

	net->hw_features = NETIF_F_RXCSUM | NETIF_F_SG | NETIF_F_IP_CSUM |
				NETIF_F_TSO;
	net->features = NETIF_F_HW_VLAN_CTAG_TX | NETIF_F_SG | NETIF_F_RXCSUM |
			NETIF_F_IP_CSUM | NETIF_F_TSO;

	net->ethtool_ops = &ethtool_ops;
	SET_NETDEV_DEV(net, &dev->device);

	/* Notify the netvsc driver of the new device */
	device_info.ring_size = ring_size;
	ret = rndis_filter_device_add(dev, &device_info);
	if (ret != 0) {
		netdev_err(net, "unable to add netvsc device (ret %d)\n", ret);
		free_netdev(net);
		hv_set_drvdata(dev, NULL);
		return ret;
	}
	memcpy(net->dev_addr, device_info.mac_adr, ETH_ALEN);

	nvdev = hv_get_drvdata(dev);
	netif_set_real_num_tx_queues(net, nvdev->num_chn);
	netif_set_real_num_rx_queues(net, nvdev->num_chn);

	ret = register_netdev(net);
	if (ret != 0) {
		pr_err("Unable to register netdev.\n");
		rndis_filter_device_remove(dev);
		free_netdev(net);
	} else {
		schedule_delayed_work(&net_device_ctx->dwork, 0);
	}

	return ret;
}

static int netvsc_remove(struct hv_device *dev)
{
	struct net_device *net;
	struct net_device_context *ndev_ctx;
	struct netvsc_device *net_device;

	net_device = hv_get_drvdata(dev);
	net = net_device->ndev;

	if (net == NULL) {
		dev_err(&dev->device, "No net device to remove\n");
		return 0;
	}

	net_device->start_remove = true;

	ndev_ctx = netdev_priv(net);
	cancel_delayed_work_sync(&ndev_ctx->dwork);
	cancel_work_sync(&ndev_ctx->work);

	/* Stop outbound asap */
	netif_tx_disable(net);

	unregister_netdev(net);

	/*
	 * Call to the vsc driver to let it know that the device is being
	 * removed
	 */
	rndis_filter_device_remove(dev);

	free_netdev(net);
	return 0;
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
};

static void __exit netvsc_drv_exit(void)
{
	vmbus_driver_unregister(&netvsc_drv);
}

static int __init netvsc_drv_init(void)
{
	if (ring_size < RING_SIZE_MIN) {
		ring_size = RING_SIZE_MIN;
		pr_info("Increased ring_size to %d (min allowed)\n",
			ring_size);
	}
	return vmbus_driver_register(&netvsc_drv);
}

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Microsoft Hyper-V network driver");

module_init(netvsc_drv_init);
module_exit(netvsc_drv_exit);
