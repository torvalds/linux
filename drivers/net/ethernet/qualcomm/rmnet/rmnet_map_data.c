// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2013-2018, The Linux Foundation. All rights reserved.
 *
 * RMNET Data MAP protocol
 */

#include <linux/netdevice.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <net/ip6_checksum.h>
#include "rmnet_config.h"
#include "rmnet_map.h"
#include "rmnet_private.h"

#define RMNET_MAP_DEAGGR_SPACING  64
#define RMNET_MAP_DEAGGR_HEADROOM (RMNET_MAP_DEAGGR_SPACING / 2)

static __sum16 *rmnet_map_get_csum_field(unsigned char protocol,
					 const void *txporthdr)
{
	__sum16 *check = NULL;

	switch (protocol) {
	case IPPROTO_TCP:
		check = &(((struct tcphdr *)txporthdr)->check);
		break;

	case IPPROTO_UDP:
		check = &(((struct udphdr *)txporthdr)->check);
		break;

	default:
		check = NULL;
		break;
	}

	return check;
}

static int
rmnet_map_ipv4_dl_csum_trailer(struct sk_buff *skb,
			       struct rmnet_map_dl_csum_trailer *csum_trailer,
			       struct rmnet_priv *priv)
{
	__sum16 *csum_field, csum_temp, pseudo_csum, hdr_csum, ip_payload_csum;
	u16 csum_value, csum_value_final;
	struct iphdr *ip4h;
	void *txporthdr;
	__be16 addend;

	ip4h = (struct iphdr *)(skb->data);
	if ((ntohs(ip4h->frag_off) & IP_MF) ||
	    ((ntohs(ip4h->frag_off) & IP_OFFSET) > 0)) {
		priv->stats.csum_fragmented_pkt++;
		return -EOPNOTSUPP;
	}

	txporthdr = skb->data + ip4h->ihl * 4;

	csum_field = rmnet_map_get_csum_field(ip4h->protocol, txporthdr);

	if (!csum_field) {
		priv->stats.csum_err_invalid_transport++;
		return -EPROTONOSUPPORT;
	}

	/* RFC 768 - Skip IPv4 UDP packets where sender checksum field is 0 */
	if (*csum_field == 0 && ip4h->protocol == IPPROTO_UDP) {
		priv->stats.csum_skipped++;
		return 0;
	}

	csum_value = ~ntohs(csum_trailer->csum_value);
	hdr_csum = ~ip_fast_csum(ip4h, (int)ip4h->ihl);
	ip_payload_csum = csum16_sub((__force __sum16)csum_value,
				     (__force __be16)hdr_csum);

	pseudo_csum = ~csum_tcpudp_magic(ip4h->saddr, ip4h->daddr,
					 ntohs(ip4h->tot_len) - ip4h->ihl * 4,
					 ip4h->protocol, 0);
	addend = (__force __be16)ntohs((__force __be16)pseudo_csum);
	pseudo_csum = csum16_add(ip_payload_csum, addend);

	addend = (__force __be16)ntohs((__force __be16)*csum_field);
	csum_temp = ~csum16_sub(pseudo_csum, addend);
	csum_value_final = (__force u16)csum_temp;

	if (unlikely(csum_value_final == 0)) {
		switch (ip4h->protocol) {
		case IPPROTO_UDP:
			/* RFC 768 - DL4 1's complement rule for UDP csum 0 */
			csum_value_final = ~csum_value_final;
			break;

		case IPPROTO_TCP:
			/* DL4 Non-RFC compliant TCP checksum found */
			if (*csum_field == (__force __sum16)0xFFFF)
				csum_value_final = ~csum_value_final;
			break;
		}
	}

	if (csum_value_final == ntohs((__force __be16)*csum_field)) {
		priv->stats.csum_ok++;
		return 0;
	} else {
		priv->stats.csum_validation_failed++;
		return -EINVAL;
	}
}

#if IS_ENABLED(CONFIG_IPV6)
static int
rmnet_map_ipv6_dl_csum_trailer(struct sk_buff *skb,
			       struct rmnet_map_dl_csum_trailer *csum_trailer,
			       struct rmnet_priv *priv)
{
	__sum16 *csum_field, ip6_payload_csum, pseudo_csum, csum_temp;
	u16 csum_value, csum_value_final;
	__be16 ip6_hdr_csum, addend;
	struct ipv6hdr *ip6h;
	void *txporthdr;
	u32 length;

	ip6h = (struct ipv6hdr *)(skb->data);

	txporthdr = skb->data + sizeof(struct ipv6hdr);
	csum_field = rmnet_map_get_csum_field(ip6h->nexthdr, txporthdr);

	if (!csum_field) {
		priv->stats.csum_err_invalid_transport++;
		return -EPROTONOSUPPORT;
	}

	csum_value = ~ntohs(csum_trailer->csum_value);
	ip6_hdr_csum = (__force __be16)
			~ntohs((__force __be16)ip_compute_csum(ip6h,
			       (int)(txporthdr - (void *)(skb->data))));
	ip6_payload_csum = csum16_sub((__force __sum16)csum_value,
				      ip6_hdr_csum);

	length = (ip6h->nexthdr == IPPROTO_UDP) ?
		 ntohs(((struct udphdr *)txporthdr)->len) :
		 ntohs(ip6h->payload_len);
	pseudo_csum = ~(csum_ipv6_magic(&ip6h->saddr, &ip6h->daddr,
			     length, ip6h->nexthdr, 0));
	addend = (__force __be16)ntohs((__force __be16)pseudo_csum);
	pseudo_csum = csum16_add(ip6_payload_csum, addend);

	addend = (__force __be16)ntohs((__force __be16)*csum_field);
	csum_temp = ~csum16_sub(pseudo_csum, addend);
	csum_value_final = (__force u16)csum_temp;

	if (unlikely(csum_value_final == 0)) {
		switch (ip6h->nexthdr) {
		case IPPROTO_UDP:
			/* RFC 2460 section 8.1
			 * DL6 One's complement rule for UDP checksum 0
			 */
			csum_value_final = ~csum_value_final;
			break;

		case IPPROTO_TCP:
			/* DL6 Non-RFC compliant TCP checksum found */
			if (*csum_field == (__force __sum16)0xFFFF)
				csum_value_final = ~csum_value_final;
			break;
		}
	}

	if (csum_value_final == ntohs((__force __be16)*csum_field)) {
		priv->stats.csum_ok++;
		return 0;
	} else {
		priv->stats.csum_validation_failed++;
		return -EINVAL;
	}
}
#endif

static void rmnet_map_complement_ipv4_txporthdr_csum_field(void *iphdr)
{
	struct iphdr *ip4h = (struct iphdr *)iphdr;
	void *txphdr;
	u16 *csum;

	txphdr = iphdr + ip4h->ihl * 4;

	if (ip4h->protocol == IPPROTO_TCP || ip4h->protocol == IPPROTO_UDP) {
		csum = (u16 *)rmnet_map_get_csum_field(ip4h->protocol, txphdr);
		*csum = ~(*csum);
	}
}

static void
rmnet_map_ipv4_ul_csum_header(void *iphdr,
			      struct rmnet_map_ul_csum_header *ul_header,
			      struct sk_buff *skb)
{
	__be16 *hdr = (__be16 *)ul_header;
	struct iphdr *ip4h = iphdr;

	ul_header->csum_start_offset = htons(skb_network_header_len(skb));
	ul_header->csum_insert_offset = skb->csum_offset;
	ul_header->csum_enabled = 1;
	if (ip4h->protocol == IPPROTO_UDP)
		ul_header->udp_ind = 1;
	else
		ul_header->udp_ind = 0;

	/* Changing remaining fields to network order */
	hdr++;
	*hdr = htons((__force u16)*hdr);

	skb->ip_summed = CHECKSUM_NONE;

	rmnet_map_complement_ipv4_txporthdr_csum_field(iphdr);
}

#if IS_ENABLED(CONFIG_IPV6)
static void rmnet_map_complement_ipv6_txporthdr_csum_field(void *ip6hdr)
{
	struct ipv6hdr *ip6h = (struct ipv6hdr *)ip6hdr;
	void *txphdr;
	u16 *csum;

	txphdr = ip6hdr + sizeof(struct ipv6hdr);

	if (ip6h->nexthdr == IPPROTO_TCP || ip6h->nexthdr == IPPROTO_UDP) {
		csum = (u16 *)rmnet_map_get_csum_field(ip6h->nexthdr, txphdr);
		*csum = ~(*csum);
	}
}

static void
rmnet_map_ipv6_ul_csum_header(void *ip6hdr,
			      struct rmnet_map_ul_csum_header *ul_header,
			      struct sk_buff *skb)
{
	__be16 *hdr = (__be16 *)ul_header;
	struct ipv6hdr *ip6h = ip6hdr;

	ul_header->csum_start_offset = htons(skb_network_header_len(skb));
	ul_header->csum_insert_offset = skb->csum_offset;
	ul_header->csum_enabled = 1;

	if (ip6h->nexthdr == IPPROTO_UDP)
		ul_header->udp_ind = 1;
	else
		ul_header->udp_ind = 0;

	/* Changing remaining fields to network order */
	hdr++;
	*hdr = htons((__force u16)*hdr);

	skb->ip_summed = CHECKSUM_NONE;

	rmnet_map_complement_ipv6_txporthdr_csum_field(ip6hdr);
}
#endif

/* Adds MAP header to front of skb->data
 * Padding is calculated and set appropriately in MAP header. Mux ID is
 * initialized to 0.
 */
struct rmnet_map_header *rmnet_map_add_map_header(struct sk_buff *skb,
						  int hdrlen, int pad)
{
	struct rmnet_map_header *map_header;
	u32 padding, map_datalen;
	u8 *padbytes;

	map_datalen = skb->len - hdrlen;
	map_header = (struct rmnet_map_header *)
			skb_push(skb, sizeof(struct rmnet_map_header));
	memset(map_header, 0, sizeof(struct rmnet_map_header));

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

	padbytes = (u8 *)skb_put(skb, padding);
	memset(padbytes, 0, padding);

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
	struct rmnet_map_header *maph;
	struct sk_buff *skbn;
	u32 packet_len;

	if (skb->len == 0)
		return NULL;

	maph = (struct rmnet_map_header *)skb->data;
	packet_len = ntohs(maph->pkt_len) + sizeof(*maph);

	if (port->data_format & RMNET_FLAGS_INGRESS_MAP_CKSUMV4)
		packet_len += sizeof(struct rmnet_map_dl_csum_trailer);

	if (((int)skb->len - (int)packet_len) < 0)
		return NULL;

	/* Some hardware can send us empty frames. Catch them */
	if (!maph->pkt_len)
		return NULL;

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

	if (skb->protocol == htons(ETH_P_IP)) {
		return rmnet_map_ipv4_dl_csum_trailer(skb, csum_trailer, priv);
	} else if (skb->protocol == htons(ETH_P_IPV6)) {
#if IS_ENABLED(CONFIG_IPV6)
		return rmnet_map_ipv6_dl_csum_trailer(skb, csum_trailer, priv);
#else
		priv->stats.csum_err_invalid_ip_version++;
		return -EPROTONOSUPPORT;
#endif
	} else {
		priv->stats.csum_err_invalid_ip_version++;
		return -EPROTONOSUPPORT;
	}

	return 0;
}

/* Generates UL checksum meta info header for IPv4 and IPv6 over TCP and UDP
 * packets that are supported for UL checksum offload.
 */
void rmnet_map_checksum_uplink_packet(struct sk_buff *skb,
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

	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		iphdr = (char *)ul_header +
			sizeof(struct rmnet_map_ul_csum_header);

		if (skb->protocol == htons(ETH_P_IP)) {
			rmnet_map_ipv4_ul_csum_header(iphdr, ul_header, skb);
			return;
		} else if (skb->protocol == htons(ETH_P_IPV6)) {
#if IS_ENABLED(CONFIG_IPV6)
			rmnet_map_ipv6_ul_csum_header(iphdr, ul_header, skb);
			return;
#else
			priv->stats.csum_err_invalid_ip_version++;
			goto sw_csum;
#endif
		} else {
			priv->stats.csum_err_invalid_ip_version++;
		}
	}

sw_csum:
	ul_header->csum_start_offset = 0;
	ul_header->csum_insert_offset = 0;
	ul_header->csum_enabled = 0;
	ul_header->udp_ind = 0;

	priv->stats.csum_sw++;
}
