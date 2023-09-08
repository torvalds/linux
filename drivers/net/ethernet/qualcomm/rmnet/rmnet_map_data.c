// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2013-2018, 2021, The Linux Foundation. All rights reserved.
 *
 * RMNET Data MAP protocol
 */

#include <linux/netdevice.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <net/ip6_checksum.h>
#include <linux/bitfield.h>
#include "rmnet_config.h"
#include "rmnet_map.h"
#include "rmnet_private.h"
#include "rmnet_vnd.h"

#define RMNET_MAP_DEAGGR_SPACING  64
#define RMNET_MAP_DEAGGR_HEADROOM (RMNET_MAP_DEAGGR_SPACING / 2)

static __sum16 *rmnet_map_get_csum_field(unsigned char protocol,
					 const void *txporthdr)
{
	if (protocol == IPPROTO_TCP)
		return &((struct tcphdr *)txporthdr)->check;

	if (protocol == IPPROTO_UDP)
		return &((struct udphdr *)txporthdr)->check;

	return NULL;
}

static int
rmnet_map_ipv4_dl_csum_trailer(struct sk_buff *skb,
			       struct rmnet_map_dl_csum_trailer *csum_trailer,
			       struct rmnet_priv *priv)
{
	struct iphdr *ip4h = (struct iphdr *)skb->data;
	void *txporthdr = skb->data + ip4h->ihl * 4;
	__sum16 *csum_field, pseudo_csum;
	__sum16 ip_payload_csum;

	/* Computing the checksum over just the IPv4 header--including its
	 * checksum field--should yield 0.  If it doesn't, the IP header
	 * is bad, so return an error and let the IP layer drop it.
	 */
	if (ip_fast_csum(ip4h, ip4h->ihl)) {
		priv->stats.csum_ip4_header_bad++;
		return -EINVAL;
	}

	/* We don't support checksum offload on IPv4 fragments */
	if (ip_is_fragment(ip4h)) {
		priv->stats.csum_fragmented_pkt++;
		return -EOPNOTSUPP;
	}

	/* Checksum offload is only supported for UDP and TCP protocols */
	csum_field = rmnet_map_get_csum_field(ip4h->protocol, txporthdr);
	if (!csum_field) {
		priv->stats.csum_err_invalid_transport++;
		return -EPROTONOSUPPORT;
	}

	/* RFC 768: UDP checksum is optional for IPv4, and is 0 if unused */
	if (!*csum_field && ip4h->protocol == IPPROTO_UDP) {
		priv->stats.csum_skipped++;
		return 0;
	}

	/* The checksum value in the trailer is computed over the entire
	 * IP packet, including the IP header and payload.  To derive the
	 * transport checksum from this, we first subract the contribution
	 * of the IP header from the trailer checksum.  We then add the
	 * checksum computed over the pseudo header.
	 *
	 * We verified above that the IP header contributes zero to the
	 * trailer checksum.  Therefore the checksum in the trailer is
	 * just the checksum computed over the IP payload.

	 * If the IP payload arrives intact, adding the pseudo header
	 * checksum to the IP payload checksum will yield 0xffff (negative
	 * zero).  This means the trailer checksum and the pseudo checksum
	 * are additive inverses of each other.  Put another way, the
	 * message passes the checksum test if the trailer checksum value
	 * is the negated pseudo header checksum.
	 *
	 * Knowing this, we don't even need to examine the transport
	 * header checksum value; it is already accounted for in the
	 * checksum value found in the trailer.
	 */
	ip_payload_csum = csum_trailer->csum_value;

	pseudo_csum = csum_tcpudp_magic(ip4h->saddr, ip4h->daddr,
					ntohs(ip4h->tot_len) - ip4h->ihl * 4,
					ip4h->protocol, 0);

	/* The cast is required to ensure only the low 16 bits are examined */
	if (ip_payload_csum != (__sum16)~pseudo_csum) {
		priv->stats.csum_validation_failed++;
		return -EINVAL;
	}

	priv->stats.csum_ok++;
	return 0;
}

#if IS_ENABLED(CONFIG_IPV6)
static int
rmnet_map_ipv6_dl_csum_trailer(struct sk_buff *skb,
			       struct rmnet_map_dl_csum_trailer *csum_trailer,
			       struct rmnet_priv *priv)
{
	struct ipv6hdr *ip6h = (struct ipv6hdr *)skb->data;
	void *txporthdr = skb->data + sizeof(*ip6h);
	__sum16 *csum_field, pseudo_csum;
	__sum16 ip6_payload_csum;
	__be16 ip_header_csum;

	/* Checksum offload is only supported for UDP and TCP protocols;
	 * the packet cannot include any IPv6 extension headers
	 */
	csum_field = rmnet_map_get_csum_field(ip6h->nexthdr, txporthdr);
	if (!csum_field) {
		priv->stats.csum_err_invalid_transport++;
		return -EPROTONOSUPPORT;
	}

	/* The checksum value in the trailer is computed over the entire
	 * IP packet, including the IP header and payload.  To derive the
	 * transport checksum from this, we first subract the contribution
	 * of the IP header from the trailer checksum.  We then add the
	 * checksum computed over the pseudo header.
	 */
	ip_header_csum = (__force __be16)ip_fast_csum(ip6h, sizeof(*ip6h) / 4);
	ip6_payload_csum = csum16_sub(csum_trailer->csum_value, ip_header_csum);

	pseudo_csum = csum_ipv6_magic(&ip6h->saddr, &ip6h->daddr,
				      ntohs(ip6h->payload_len),
				      ip6h->nexthdr, 0);

	/* It's sufficient to compare the IP payload checksum with the
	 * negated pseudo checksum to determine whether the packet
	 * checksum was good.  (See further explanation in comments
	 * in rmnet_map_ipv4_dl_csum_trailer()).
	 *
	 * The cast is required to ensure only the low 16 bits are
	 * examined.
	 */
	if (ip6_payload_csum != (__sum16)~pseudo_csum) {
		priv->stats.csum_validation_failed++;
		return -EINVAL;
	}

	priv->stats.csum_ok++;
	return 0;
}
#else
static int
rmnet_map_ipv6_dl_csum_trailer(struct sk_buff *skb,
			       struct rmnet_map_dl_csum_trailer *csum_trailer,
			       struct rmnet_priv *priv)
{
	return 0;
}
#endif

static void rmnet_map_complement_ipv4_txporthdr_csum_field(struct iphdr *ip4h)
{
	void *txphdr;
	u16 *csum;

	txphdr = (void *)ip4h + ip4h->ihl * 4;

	if (ip4h->protocol == IPPROTO_TCP || ip4h->protocol == IPPROTO_UDP) {
		csum = (u16 *)rmnet_map_get_csum_field(ip4h->protocol, txphdr);
		*csum = ~(*csum);
	}
}

static void
rmnet_map_ipv4_ul_csum_header(struct iphdr *iphdr,
			      struct rmnet_map_ul_csum_header *ul_header,
			      struct sk_buff *skb)
{
	u16 val;

	val = MAP_CSUM_UL_ENABLED_FLAG;
	if (iphdr->protocol == IPPROTO_UDP)
		val |= MAP_CSUM_UL_UDP_FLAG;
	val |= skb->csum_offset & MAP_CSUM_UL_OFFSET_MASK;

	ul_header->csum_start_offset = htons(skb_network_header_len(skb));
	ul_header->csum_info = htons(val);

	skb->ip_summed = CHECKSUM_NONE;

	rmnet_map_complement_ipv4_txporthdr_csum_field(iphdr);
}

#if IS_ENABLED(CONFIG_IPV6)
static void
rmnet_map_complement_ipv6_txporthdr_csum_field(struct ipv6hdr *ip6h)
{
	void *txphdr;
	u16 *csum;

	txphdr = ip6h + 1;

	if (ip6h->nexthdr == IPPROTO_TCP || ip6h->nexthdr == IPPROTO_UDP) {
		csum = (u16 *)rmnet_map_get_csum_field(ip6h->nexthdr, txphdr);
		*csum = ~(*csum);
	}
}

static void
rmnet_map_ipv6_ul_csum_header(struct ipv6hdr *ipv6hdr,
			      struct rmnet_map_ul_csum_header *ul_header,
			      struct sk_buff *skb)
{
	u16 val;

	val = MAP_CSUM_UL_ENABLED_FLAG;
	if (ipv6hdr->nexthdr == IPPROTO_UDP)
		val |= MAP_CSUM_UL_UDP_FLAG;
	val |= skb->csum_offset & MAP_CSUM_UL_OFFSET_MASK;

	ul_header->csum_start_offset = htons(skb_network_header_len(skb));
	ul_header->csum_info = htons(val);

	skb->ip_summed = CHECKSUM_NONE;

	rmnet_map_complement_ipv6_txporthdr_csum_field(ipv6hdr);
}
#else
static void
rmnet_map_ipv6_ul_csum_header(void *ip6hdr,
			      struct rmnet_map_ul_csum_header *ul_header,
			      struct sk_buff *skb)
{
}
#endif

static void rmnet_map_v5_checksum_uplink_packet(struct sk_buff *skb,
						struct rmnet_port *port,
						struct net_device *orig_dev)
{
	struct rmnet_priv *priv = netdev_priv(orig_dev);
	struct rmnet_map_v5_csum_header *ul_header;

	ul_header = skb_push(skb, sizeof(*ul_header));
	memset(ul_header, 0, sizeof(*ul_header));
	ul_header->header_info = u8_encode_bits(RMNET_MAP_HEADER_TYPE_CSUM_OFFLOAD,
						MAPV5_HDRINFO_HDR_TYPE_FMASK);

	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		void *iph = ip_hdr(skb);
		__sum16 *check;
		void *trans;
		u8 proto;

		if (skb->protocol == htons(ETH_P_IP)) {
			u16 ip_len = ((struct iphdr *)iph)->ihl * 4;

			proto = ((struct iphdr *)iph)->protocol;
			trans = iph + ip_len;
		} else if (IS_ENABLED(CONFIG_IPV6) &&
			   skb->protocol == htons(ETH_P_IPV6)) {
			u16 ip_len = sizeof(struct ipv6hdr);

			proto = ((struct ipv6hdr *)iph)->nexthdr;
			trans = iph + ip_len;
		} else {
			priv->stats.csum_err_invalid_ip_version++;
			goto sw_csum;
		}

		check = rmnet_map_get_csum_field(proto, trans);
		if (check) {
			skb->ip_summed = CHECKSUM_NONE;
			/* Ask for checksum offloading */
			ul_header->csum_info |= MAPV5_CSUMINFO_VALID_FLAG;
			priv->stats.csum_hw++;
			return;
		}
	}

sw_csum:
	priv->stats.csum_sw++;
}

/* Adds MAP header to front of skb->data
 * Padding is calculated and set appropriately in MAP header. Mux ID is
 * initialized to 0.
 */
struct rmnet_map_header *rmnet_map_add_map_header(struct sk_buff *skb,
						  int hdrlen,
						  struct rmnet_port *port,
						  int pad)
{
	struct rmnet_map_header *map_header;
	u32 padding, map_datalen;

	map_datalen = skb->len - hdrlen;
	map_header = (struct rmnet_map_header *)
			skb_push(skb, sizeof(struct rmnet_map_header));
	memset(map_header, 0, sizeof(struct rmnet_map_header));

	/* Set next_hdr bit for csum offload packets */
	if (port->data_format & RMNET_FLAGS_EGRESS_MAP_CKSUMV5)
		map_header->flags |= MAP_NEXT_HEADER_FLAG;

	if (pad == RMNET_MAP_NO_PAD_BYTES) {
		map_header->pkt_len = htons(map_datalen);
		return map_header;
	}

	BUILD_BUG_ON(MAP_PAD_LEN_MASK < 3);
	padding = ALIGN(map_datalen, 4) - map_datalen;

	if (padding == 0)
		goto done;

	if (skb_tailroom(skb) < padding)
		return NULL;

	skb_put_zero(skb, padding);

done:
	map_header->pkt_len = htons(map_datalen + padding);
	/* This is a data packet, so the CMD bit is 0 */
	map_header->flags = padding & MAP_PAD_LEN_MASK;

	return map_header;
}

/* Deaggregates a single packet
 * A whole new buffer is allocated for each portion of an aggregated frame.
 * Caller should keep calling deaggregate() on the source skb until 0 is
 * returned, indicating that there are no more packets to deaggregate. Caller
 * is responsible for freeing the original skb.
 */
struct sk_buff *rmnet_map_deaggregate(struct sk_buff *skb,
				      struct rmnet_port *port)
{
	struct rmnet_map_v5_csum_header *next_hdr = NULL;
	struct rmnet_map_header *maph;
	void *data = skb->data;
	struct sk_buff *skbn;
	u8 nexthdr_type;
	u32 packet_len;

	if (skb->len == 0)
		return NULL;

	maph = (struct rmnet_map_header *)skb->data;
	packet_len = ntohs(maph->pkt_len) + sizeof(*maph);

	if (port->data_format & RMNET_FLAGS_INGRESS_MAP_CKSUMV4) {
		packet_len += sizeof(struct rmnet_map_dl_csum_trailer);
	} else if (port->data_format & RMNET_FLAGS_INGRESS_MAP_CKSUMV5) {
		if (!(maph->flags & MAP_CMD_FLAG)) {
			packet_len += sizeof(*next_hdr);
			if (maph->flags & MAP_NEXT_HEADER_FLAG)
				next_hdr = data + sizeof(*maph);
			else
				/* Mapv5 data pkt without csum hdr is invalid */
				return NULL;
		}
	}

	if (((int)skb->len - (int)packet_len) < 0)
		return NULL;

	/* Some hardware can send us empty frames. Catch them */
	if (!maph->pkt_len)
		return NULL;

	if (next_hdr) {
		nexthdr_type = u8_get_bits(next_hdr->header_info,
					   MAPV5_HDRINFO_HDR_TYPE_FMASK);
		if (nexthdr_type != RMNET_MAP_HEADER_TYPE_CSUM_OFFLOAD)
			return NULL;
	}

	skbn = alloc_skb(packet_len + RMNET_MAP_DEAGGR_SPACING, GFP_ATOMIC);
	if (!skbn)
		return NULL;

	skb_reserve(skbn, RMNET_MAP_DEAGGR_HEADROOM);
	skb_put(skbn, packet_len);
	memcpy(skbn->data, skb->data, packet_len);
	skb_pull(skb, packet_len);

	return skbn;
}

/* Validates packet checksums. Function takes a pointer to
 * the beginning of a buffer which contains the IP payload +
 * padding + checksum trailer.
 * Only IPv4 and IPv6 are supported along with TCP & UDP.
 * Fragmented or tunneled packets are not supported.
 */
int rmnet_map_checksum_downlink_packet(struct sk_buff *skb, u16 len)
{
	struct rmnet_priv *priv = netdev_priv(skb->dev);
	struct rmnet_map_dl_csum_trailer *csum_trailer;

	if (unlikely(!(skb->dev->features & NETIF_F_RXCSUM))) {
		priv->stats.csum_sw++;
		return -EOPNOTSUPP;
	}

	csum_trailer = (struct rmnet_map_dl_csum_trailer *)(skb->data + len);

	if (!(csum_trailer->flags & MAP_CSUM_DL_VALID_FLAG)) {
		priv->stats.csum_valid_unset++;
		return -EINVAL;
	}

	if (skb->protocol == htons(ETH_P_IP))
		return rmnet_map_ipv4_dl_csum_trailer(skb, csum_trailer, priv);

	if (IS_ENABLED(CONFIG_IPV6) && skb->protocol == htons(ETH_P_IPV6))
		return rmnet_map_ipv6_dl_csum_trailer(skb, csum_trailer, priv);

	priv->stats.csum_err_invalid_ip_version++;

	return -EPROTONOSUPPORT;
}

static void rmnet_map_v4_checksum_uplink_packet(struct sk_buff *skb,
						struct net_device *orig_dev)
{
	struct rmnet_priv *priv = netdev_priv(orig_dev);
	struct rmnet_map_ul_csum_header *ul_header;
	void *iphdr;

	ul_header = (struct rmnet_map_ul_csum_header *)
		    skb_push(skb, sizeof(struct rmnet_map_ul_csum_header));

	if (unlikely(!(orig_dev->features &
		     (NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM))))
		goto sw_csum;

	if (skb->ip_summed != CHECKSUM_PARTIAL)
		goto sw_csum;

	iphdr = (char *)ul_header +
		sizeof(struct rmnet_map_ul_csum_header);

	if (skb->protocol == htons(ETH_P_IP)) {
		rmnet_map_ipv4_ul_csum_header(iphdr, ul_header, skb);
		priv->stats.csum_hw++;
		return;
	}

	if (IS_ENABLED(CONFIG_IPV6) && skb->protocol == htons(ETH_P_IPV6)) {
		rmnet_map_ipv6_ul_csum_header(iphdr, ul_header, skb);
		priv->stats.csum_hw++;
		return;
	}

	priv->stats.csum_err_invalid_ip_version++;

sw_csum:
	memset(ul_header, 0, sizeof(*ul_header));

	priv->stats.csum_sw++;
}

/* Generates UL checksum meta info header for IPv4 and IPv6 over TCP and UDP
 * packets that are supported for UL checksum offload.
 */
void rmnet_map_checksum_uplink_packet(struct sk_buff *skb,
				      struct rmnet_port *port,
				      struct net_device *orig_dev,
				      int csum_type)
{
	switch (csum_type) {
	case RMNET_FLAGS_EGRESS_MAP_CKSUMV4:
		rmnet_map_v4_checksum_uplink_packet(skb, orig_dev);
		break;
	case RMNET_FLAGS_EGRESS_MAP_CKSUMV5:
		rmnet_map_v5_checksum_uplink_packet(skb, port, orig_dev);
		break;
	default:
		break;
	}
}

/* Process a MAPv5 packet header */
int rmnet_map_process_next_hdr_packet(struct sk_buff *skb,
				      u16 len)
{
	struct rmnet_priv *priv = netdev_priv(skb->dev);
	struct rmnet_map_v5_csum_header *next_hdr;
	u8 nexthdr_type;

	next_hdr = (struct rmnet_map_v5_csum_header *)(skb->data +
			sizeof(struct rmnet_map_header));

	nexthdr_type = u8_get_bits(next_hdr->header_info,
				   MAPV5_HDRINFO_HDR_TYPE_FMASK);

	if (nexthdr_type != RMNET_MAP_HEADER_TYPE_CSUM_OFFLOAD)
		return -EINVAL;

	if (unlikely(!(skb->dev->features & NETIF_F_RXCSUM))) {
		priv->stats.csum_sw++;
	} else if (next_hdr->csum_info & MAPV5_CSUMINFO_VALID_FLAG) {
		priv->stats.csum_ok++;
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	} else {
		priv->stats.csum_valid_unset++;
	}

	/* Pull csum v5 header */
	skb_pull(skb, sizeof(*next_hdr));

	return 0;
}

#define RMNET_AGG_BYPASS_TIME_NSEC 10000000L

static void reset_aggr_params(struct rmnet_port *port)
{
	port->skbagg_head = NULL;
	port->agg_count = 0;
	port->agg_state = 0;
	memset(&port->agg_time, 0, sizeof(struct timespec64));
}

static void rmnet_send_skb(struct rmnet_port *port, struct sk_buff *skb)
{
	if (skb_needs_linearize(skb, port->dev->features)) {
		if (unlikely(__skb_linearize(skb))) {
			struct rmnet_priv *priv;

			priv = netdev_priv(port->rmnet_dev);
			this_cpu_inc(priv->pcpu_stats->stats.tx_drops);
			dev_kfree_skb_any(skb);
			return;
		}
	}

	dev_queue_xmit(skb);
}

static void rmnet_map_flush_tx_packet_work(struct work_struct *work)
{
	struct sk_buff *skb = NULL;
	struct rmnet_port *port;

	port = container_of(work, struct rmnet_port, agg_wq);

	spin_lock_bh(&port->agg_lock);
	if (likely(port->agg_state == -EINPROGRESS)) {
		/* Buffer may have already been shipped out */
		if (likely(port->skbagg_head)) {
			skb = port->skbagg_head;
			reset_aggr_params(port);
		}
		port->agg_state = 0;
	}

	spin_unlock_bh(&port->agg_lock);
	if (skb)
		rmnet_send_skb(port, skb);
}

static enum hrtimer_restart rmnet_map_flush_tx_packet_queue(struct hrtimer *t)
{
	struct rmnet_port *port;

	port = container_of(t, struct rmnet_port, hrtimer);

	schedule_work(&port->agg_wq);

	return HRTIMER_NORESTART;
}

unsigned int rmnet_map_tx_aggregate(struct sk_buff *skb, struct rmnet_port *port,
				    struct net_device *orig_dev)
{
	struct timespec64 diff, last;
	unsigned int len = skb->len;
	struct sk_buff *agg_skb;
	int size;

	spin_lock_bh(&port->agg_lock);
	memcpy(&last, &port->agg_last, sizeof(struct timespec64));
	ktime_get_real_ts64(&port->agg_last);

	if (!port->skbagg_head) {
		/* Check to see if we should agg first. If the traffic is very
		 * sparse, don't aggregate.
		 */
new_packet:
		diff = timespec64_sub(port->agg_last, last);
		size = port->egress_agg_params.bytes - skb->len;

		if (size < 0) {
			/* dropped */
			spin_unlock_bh(&port->agg_lock);
			return 0;
		}

		if (diff.tv_sec > 0 || diff.tv_nsec > RMNET_AGG_BYPASS_TIME_NSEC ||
		    size == 0)
			goto no_aggr;

		port->skbagg_head = skb_copy_expand(skb, 0, size, GFP_ATOMIC);
		if (!port->skbagg_head)
			goto no_aggr;

		dev_kfree_skb_any(skb);
		port->skbagg_head->protocol = htons(ETH_P_MAP);
		port->agg_count = 1;
		ktime_get_real_ts64(&port->agg_time);
		skb_frag_list_init(port->skbagg_head);
		goto schedule;
	}
	diff = timespec64_sub(port->agg_last, port->agg_time);
	size = port->egress_agg_params.bytes - port->skbagg_head->len;

	if (skb->len > size) {
		agg_skb = port->skbagg_head;
		reset_aggr_params(port);
		spin_unlock_bh(&port->agg_lock);
		hrtimer_cancel(&port->hrtimer);
		rmnet_send_skb(port, agg_skb);
		spin_lock_bh(&port->agg_lock);
		goto new_packet;
	}

	if (skb_has_frag_list(port->skbagg_head))
		port->skbagg_tail->next = skb;
	else
		skb_shinfo(port->skbagg_head)->frag_list = skb;

	port->skbagg_head->len += skb->len;
	port->skbagg_head->data_len += skb->len;
	port->skbagg_head->truesize += skb->truesize;
	port->skbagg_tail = skb;
	port->agg_count++;

	if (diff.tv_sec > 0 || diff.tv_nsec > port->egress_agg_params.time_nsec ||
	    port->agg_count >= port->egress_agg_params.count ||
	    port->skbagg_head->len == port->egress_agg_params.bytes) {
		agg_skb = port->skbagg_head;
		reset_aggr_params(port);
		spin_unlock_bh(&port->agg_lock);
		hrtimer_cancel(&port->hrtimer);
		rmnet_send_skb(port, agg_skb);
		return len;
	}

schedule:
	if (!hrtimer_active(&port->hrtimer) && port->agg_state != -EINPROGRESS) {
		port->agg_state = -EINPROGRESS;
		hrtimer_start(&port->hrtimer,
			      ns_to_ktime(port->egress_agg_params.time_nsec),
			      HRTIMER_MODE_REL);
	}
	spin_unlock_bh(&port->agg_lock);

	return len;

no_aggr:
	spin_unlock_bh(&port->agg_lock);
	skb->protocol = htons(ETH_P_MAP);
	dev_queue_xmit(skb);

	return len;
}

void rmnet_map_update_ul_agg_config(struct rmnet_port *port, u32 size,
				    u32 count, u32 time)
{
	spin_lock_bh(&port->agg_lock);
	port->egress_agg_params.bytes = size;
	WRITE_ONCE(port->egress_agg_params.count, count);
	port->egress_agg_params.time_nsec = time * NSEC_PER_USEC;
	spin_unlock_bh(&port->agg_lock);
}

void rmnet_map_tx_aggregate_init(struct rmnet_port *port)
{
	hrtimer_init(&port->hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	port->hrtimer.function = rmnet_map_flush_tx_packet_queue;
	spin_lock_init(&port->agg_lock);
	rmnet_map_update_ul_agg_config(port, 4096, 1, 800);
	INIT_WORK(&port->agg_wq, rmnet_map_flush_tx_packet_work);
}

void rmnet_map_tx_aggregate_exit(struct rmnet_port *port)
{
	hrtimer_cancel(&port->hrtimer);
	cancel_work_sync(&port->agg_wq);

	spin_lock_bh(&port->agg_lock);
	if (port->agg_state == -EINPROGRESS) {
		if (port->skbagg_head) {
			dev_kfree_skb_any(port->skbagg_head);
			reset_aggr_params(port);
		}

		port->agg_state = 0;
	}
	spin_unlock_bh(&port->agg_lock);
}
