// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 Intel Corporation
 */

#include <uapi/linux/if_ether.h>
#include <uapi/linux/if_arp.h>
#include <uapi/linux/icmp.h>

#include <linux/etherdevice.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/ieee80211.h>

#include <net/cfg80211.h>
#include <net/ip.h>

#include <linux/if_arp.h>
#include <linux/icmp.h>
#include <linux/udp.h>
#include <linux/ip.h>
#include <linux/mm.h>

#include "internal.h"
#include "sap.h"
#include "iwl-mei.h"

/*
 * Returns true if further filtering should be stopped. Only in that case
 * pass_to_csme and rx_handler_res are set. Otherwise, next level of filters
 * should be checked.
 */
static bool iwl_mei_rx_filter_eth(const struct ethhdr *ethhdr,
				  const struct iwl_sap_oob_filters *filters,
				  bool *pass_to_csme,
				  rx_handler_result_t *rx_handler_res)
{
	const struct iwl_sap_eth_filter *filt;

	/* This filter is not relevant for UCAST packet */
	if (!is_multicast_ether_addr(ethhdr->h_dest) ||
	    is_broadcast_ether_addr(ethhdr->h_dest))
		return false;

	for (filt = &filters->eth_filters[0];
	     filt < &filters->eth_filters[0] + ARRAY_SIZE(filters->eth_filters);
	     filt++) {
		/* Assume there are no enabled filter after a disabled one */
		if (!(filt->flags & SAP_ETH_FILTER_ENABLED))
			break;

		if (compare_ether_header(filt->mac_address, ethhdr->h_dest))
			continue;

		/* Packet needs to reach the host's stack */
		if (filt->flags & SAP_ETH_FILTER_COPY)
			*rx_handler_res = RX_HANDLER_PASS;
		else
			*rx_handler_res = RX_HANDLER_CONSUMED;

		/* We have an authoritative answer, stop filtering */
		if (filt->flags & SAP_ETH_FILTER_STOP) {
			*pass_to_csme = true;
			return true;
		}

		return false;
	}

	 /* MCAST frames that don't match layer 2 filters are not sent to ME */
	*pass_to_csme  = false;

	return true;
}

/*
 * Returns true iff the frame should be passed to CSME in which case
 * rx_handler_res is set.
 */
static bool iwl_mei_rx_filter_arp(struct sk_buff *skb,
				  const struct iwl_sap_oob_filters *filters,
				  rx_handler_result_t *rx_handler_res)
{
	const struct iwl_sap_ipv4_filter *filt = &filters->ipv4_filter;
	const struct arphdr *arp;
	const __be32 *target_ip;
	u32 flags = le32_to_cpu(filt->flags);

	if (!pskb_may_pull(skb, arp_hdr_len(skb->dev)))
		return false;

	arp = arp_hdr(skb);

	/* Handle only IPv4 over ethernet ARP frames */
	if (arp->ar_hrd != htons(ARPHRD_ETHER) ||
	    arp->ar_pro != htons(ETH_P_IP))
		return false;

	/*
	 * After the ARP header, we have:
	 * src MAC address   - 6 bytes
	 * src IP address    - 4 bytes
	 * target MAC addess - 6 bytes
	 */
	target_ip = (void *)((u8 *)(arp + 1) +
			     ETH_ALEN + sizeof(__be32) + ETH_ALEN);

	/*
	 * ARP request is forwarded to ME only if IP address match in the
	 * ARP request's target ip field.
	 */
	if (arp->ar_op == htons(ARPOP_REQUEST) &&
	    (filt->flags & cpu_to_le32(SAP_IPV4_FILTER_ARP_REQ_PASS)) &&
	    (filt->ipv4_addr == 0 || filt->ipv4_addr == *target_ip)) {
		if (flags & SAP_IPV4_FILTER_ARP_REQ_COPY)
			*rx_handler_res = RX_HANDLER_PASS;
		else
			*rx_handler_res = RX_HANDLER_CONSUMED;

		return true;
	}

	/* ARP reply is always forwarded to ME regardless of the IP */
	if (flags & SAP_IPV4_FILTER_ARP_RESP_PASS &&
	    arp->ar_op == htons(ARPOP_REPLY)) {
		if (flags & SAP_IPV4_FILTER_ARP_RESP_COPY)
			*rx_handler_res = RX_HANDLER_PASS;
		else
			*rx_handler_res = RX_HANDLER_CONSUMED;

		return true;
	}

	return false;
}

static bool
iwl_mei_rx_filter_tcp_udp(struct sk_buff *skb, bool  ip_match,
			  const struct iwl_sap_oob_filters *filters,
			  rx_handler_result_t *rx_handler_res)
{
	const struct iwl_sap_flex_filter *filt;

	for (filt = &filters->flex_filters[0];
	     filt < &filters->flex_filters[0] + ARRAY_SIZE(filters->flex_filters);
	     filt++) {
		if (!(filt->flags & SAP_FLEX_FILTER_ENABLED))
			break;

		/*
		 * We are required to have a match on the IP level and we didn't
		 * have such match.
		 */
		if ((filt->flags &
		     (SAP_FLEX_FILTER_IPV4 | SAP_FLEX_FILTER_IPV6)) &&
		    !ip_match)
			continue;

		if ((filt->flags & SAP_FLEX_FILTER_UDP) &&
		    ip_hdr(skb)->protocol != IPPROTO_UDP)
			continue;

		if ((filt->flags & SAP_FLEX_FILTER_TCP) &&
		    ip_hdr(skb)->protocol != IPPROTO_TCP)
			continue;

		/*
		 * We must have either a TCP header or a UDP header, both
		 * starts with a source port and then a destination port.
		 * Both are big endian words.
		 * Use a UDP header and that will work for TCP as well.
		 */
		if ((filt->src_port && filt->src_port != udp_hdr(skb)->source) ||
		    (filt->dst_port && filt->dst_port != udp_hdr(skb)->dest))
			continue;

		if (filt->flags & SAP_FLEX_FILTER_COPY)
			*rx_handler_res = RX_HANDLER_PASS;
		else
			*rx_handler_res = RX_HANDLER_CONSUMED;

		return true;
	}

	return false;
}

static bool iwl_mei_rx_filter_ipv4(struct sk_buff *skb,
				   const struct iwl_sap_oob_filters *filters,
				   rx_handler_result_t *rx_handler_res)
{
	const struct iwl_sap_ipv4_filter *filt = &filters->ipv4_filter;
	const struct iphdr *iphdr;
	unsigned int iphdrlen;
	bool match;

	if (!pskb_may_pull(skb, skb_network_offset(skb) + sizeof(*iphdr)) ||
	    !pskb_may_pull(skb, skb_network_offset(skb) +
			   sizeof(ip_hdrlen(skb) - sizeof(*iphdr))))
		return false;

	iphdrlen = ip_hdrlen(skb);
	iphdr = ip_hdr(skb);
	match = !filters->ipv4_filter.ipv4_addr ||
		filters->ipv4_filter.ipv4_addr == iphdr->daddr;

	skb_set_transport_header(skb, skb_network_offset(skb) + iphdrlen);

	switch (ip_hdr(skb)->protocol) {
	case IPPROTO_UDP:
	case IPPROTO_TCP:
		/*
		 * UDP header is shorter than TCP header and we look at the first bytes
		 * of the header anyway (see below).
		 * If we have a truncated TCP packet, let CSME handle this.
		 */
		if (!pskb_may_pull(skb, skb_transport_offset(skb) +
				   sizeof(struct udphdr)))
			return false;

		return iwl_mei_rx_filter_tcp_udp(skb, match,
						 filters, rx_handler_res);

	case IPPROTO_ICMP: {
		struct icmphdr *icmp;

		if (!pskb_may_pull(skb, skb_transport_offset(skb) + sizeof(*icmp)))
			return false;

		icmp = icmp_hdr(skb);

		/*
		 * Don't pass echo requests to ME even if it wants it as we
		 * want the host to answer.
		 */
		if ((filt->flags & cpu_to_le32(SAP_IPV4_FILTER_ICMP_PASS)) &&
		    match && (icmp->type != ICMP_ECHO || icmp->code != 0)) {
			if (filt->flags & cpu_to_le32(SAP_IPV4_FILTER_ICMP_COPY))
				*rx_handler_res = RX_HANDLER_PASS;
			else
				*rx_handler_res = RX_HANDLER_CONSUMED;

			return true;
		}
		break;
		}
	case IPPROTO_ICMPV6:
		/* TODO: Should we have the same ICMP request logic here too? */
		if ((filters->icmpv6_flags & cpu_to_le32(SAP_ICMPV6_FILTER_ENABLED) &&
		     match)) {
			if (filters->icmpv6_flags &
			    cpu_to_le32(SAP_ICMPV6_FILTER_COPY))
				*rx_handler_res = RX_HANDLER_PASS;
			else
				*rx_handler_res = RX_HANDLER_CONSUMED;

			return true;
		}
		break;
	default:
		return false;
	}

	return false;
}

static bool iwl_mei_rx_filter_ipv6(struct sk_buff *skb,
				   const struct iwl_sap_oob_filters *filters,
				   rx_handler_result_t *rx_handler_res)
{
	*rx_handler_res = RX_HANDLER_PASS;

	/* TODO */

	return false;
}

static rx_handler_result_t
iwl_mei_rx_pass_to_csme(struct sk_buff *skb,
			const struct iwl_sap_oob_filters *filters,
			bool *pass_to_csme)
{
	const struct ethhdr *ethhdr = (void *)skb_mac_header(skb);
	rx_handler_result_t rx_handler_res = RX_HANDLER_PASS;
	bool (*filt_handler)(struct sk_buff *skb,
			     const struct iwl_sap_oob_filters *filters,
			     rx_handler_result_t *rx_handler_res);

	/*
	 * skb->data points the IP header / ARP header and the ETH header
	 * is in the headroom.
	 */
	skb_reset_network_header(skb);

	/*
	 * MCAST IP packets sent by us are received again here without
	 * an ETH header. Drop them here.
	 */
	if (!skb_mac_offset(skb))
		return RX_HANDLER_PASS;

	if (skb_headroom(skb) < sizeof(*ethhdr))
		return RX_HANDLER_PASS;

	if (iwl_mei_rx_filter_eth(ethhdr, filters,
				  pass_to_csme, &rx_handler_res))
		return rx_handler_res;

	switch (skb->protocol) {
	case htons(ETH_P_IP):
		filt_handler = iwl_mei_rx_filter_ipv4;
		break;
	case htons(ETH_P_ARP):
		filt_handler = iwl_mei_rx_filter_arp;
		break;
	case htons(ETH_P_IPV6):
		filt_handler = iwl_mei_rx_filter_ipv6;
		break;
	default:
		*pass_to_csme = false;
		return rx_handler_res;
	}

	*pass_to_csme = filt_handler(skb, filters, &rx_handler_res);

	return rx_handler_res;
}

rx_handler_result_t iwl_mei_rx_filter(struct sk_buff *orig_skb,
				      const struct iwl_sap_oob_filters *filters,
				      bool *pass_to_csme)
{
	rx_handler_result_t ret;
	struct sk_buff *skb;

	ret = iwl_mei_rx_pass_to_csme(orig_skb, filters, pass_to_csme);

	if (!*pass_to_csme)
		return RX_HANDLER_PASS;

	if (ret == RX_HANDLER_PASS)
		skb = skb_copy(orig_skb, GFP_ATOMIC);
	else
		skb = orig_skb;

	/* CSME wants the MAC header as well, push it back */
	skb_push(skb, skb->data - skb_mac_header(skb));

	/*
	 * Add the packet that CSME wants to get to the ring. Don't send the
	 * Check Shared Area HECI message since this is not possible from the
	 * Rx context. The caller will schedule a worker to do just that.
	 */
	iwl_mei_add_data_to_ring(skb, false);

	/*
	 * In case we drop the packet, don't free it, the caller will do that
	 * for us
	 */
	if (ret == RX_HANDLER_PASS)
		dev_kfree_skb(skb);

	return ret;
}

#define DHCP_SERVER_PORT 67
#define DHCP_CLIENT_PORT 68
void iwl_mei_tx_copy_to_csme(struct sk_buff *origskb, unsigned int ivlen)
{
	struct ieee80211_hdr *hdr;
	struct sk_buff *skb;
	struct ethhdr ethhdr;
	struct ethhdr *eth;

	/* Catch DHCP packets */
	if (origskb->protocol != htons(ETH_P_IP) ||
	    ip_hdr(origskb)->protocol != IPPROTO_UDP ||
	    udp_hdr(origskb)->source != htons(DHCP_CLIENT_PORT) ||
	    udp_hdr(origskb)->dest != htons(DHCP_SERVER_PORT))
		return;

	/*
	 * We could be a bit less aggressive here and not copy everything, but
	 * this is very rare anyway, do don't bother much.
	 */
	skb = skb_copy(origskb, GFP_ATOMIC);
	if (!skb)
		return;

	skb->protocol = origskb->protocol;

	hdr = (void *)skb->data;

	memcpy(ethhdr.h_dest, ieee80211_get_DA(hdr), ETH_ALEN);
	memcpy(ethhdr.h_source, ieee80211_get_SA(hdr), ETH_ALEN);

	/*
	 * Remove the ieee80211 header + IV + SNAP but leave the ethertype
	 * We still have enough headroom for the sap header.
	 */
	pskb_pull(skb, ieee80211_hdrlen(hdr->frame_control) + ivlen + 6);
	eth = skb_push(skb, sizeof(ethhdr.h_dest) + sizeof(ethhdr.h_source));
	memcpy(eth, &ethhdr, sizeof(ethhdr.h_dest) + sizeof(ethhdr.h_source));

	iwl_mei_add_data_to_ring(skb, true);

	dev_kfree_skb(skb);
}
EXPORT_SYMBOL_GPL(iwl_mei_tx_copy_to_csme);
