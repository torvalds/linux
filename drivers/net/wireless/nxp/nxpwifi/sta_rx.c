// SPDX-License-Identifier: GPL-2.0-only
/*
 * NXP Wireless LAN device driver: station RX data handling
 *
 * Copyright 2011-2024 NXP
 */

#include <uapi/linux/ipv6.h>
#include <net/ndisc.h>
#include "cfg.h"
#include "util.h"
#include "fw.h"
#include "main.h"
#include "11n_aggr.h"
#include "11n_rxreorder.h"

/*
 * Drop gratuitous IPv4 ARP and IPv6 neighbour advertisements when
 * source and destination addresses are identical.
 */
static bool
nxpwifi_discard_gratuitous_arp(struct nxpwifi_private *priv,
			       struct sk_buff *skb)
{
	const struct nxpwifi_arp_eth_header *arp;
	struct ethhdr *eth;
	struct ipv6hdr *ipv6;
	struct icmp6hdr *icmpv6;

	eth = (struct ethhdr *)skb->data;
	switch (ntohs(eth->h_proto)) {
	case ETH_P_ARP:
		arp = (void *)(skb->data + sizeof(struct ethhdr));
		if (arp->hdr.ar_op == htons(ARPOP_REPLY) ||
		    arp->hdr.ar_op == htons(ARPOP_REQUEST)) {
			if (!memcmp(arp->ar_sip, arp->ar_tip, 4))
				return true;
		}
		break;
	case ETH_P_IPV6:
		ipv6 = (void *)(skb->data + sizeof(struct ethhdr));
		icmpv6 = (void *)(skb->data + sizeof(struct ethhdr) +
				  sizeof(struct ipv6hdr));
		if (icmpv6->icmp6_type == NDISC_NEIGHBOUR_ADVERTISEMENT) {
			if (!memcmp(&ipv6->saddr, &ipv6->daddr,
				    sizeof(struct in6_addr)))
				return true;
		}
		break;
	default:
		break;
	}

	return false;
}

/*
 * Process a received data packet.
 * Convert 802.2/LLC/SNAP to Ethernet II when appropriate, trim the
 * rxpd and extra headers, optionally drop gratuitous ARP/NA, cache
 * RX rate for unicast, then deliver to the stack.
 */
int nxpwifi_process_rx_packet(struct nxpwifi_private *priv,
			      struct sk_buff *skb)
{
	int ret;
	struct rx_packet_hdr *rx_pkt_hdr;
	struct rxpd *local_rx_pd;
	int hdr_chop;
	struct ethhdr *eth;
	u16 rx_pkt_off;
	u8 adj_rx_rate = 0;

	local_rx_pd = (struct rxpd *)(skb->data);

	rx_pkt_off = le16_to_cpu(local_rx_pd->rx_pkt_offset);
	rx_pkt_hdr = (void *)local_rx_pd + rx_pkt_off;

	if (sizeof(rx_pkt_hdr->eth803_hdr) + sizeof(rfc1042_header) +
	    rx_pkt_off > skb->len) {
		priv->stats.rx_dropped++;
		dev_kfree_skb_any(skb);
		return -EINVAL;
	}

	if (sizeof(*rx_pkt_hdr) + rx_pkt_off <= skb->len &&
	    ((!memcmp(&rx_pkt_hdr->rfc1042_hdr, bridge_tunnel_header,
		      sizeof(bridge_tunnel_header))) ||
	     (!memcmp(&rx_pkt_hdr->rfc1042_hdr, rfc1042_header,
		      sizeof(rfc1042_header)) &&
	      rx_pkt_hdr->rfc1042_hdr.snap_type != htons(ETH_P_AARP) &&
	      rx_pkt_hdr->rfc1042_hdr.snap_type != htons(ETH_P_IPX)))) {
		/*
		 * Replace the 803 header and rfc1042 header (llc/snap) with an
		 *    EthernetII header, keep the src/dst and snap_type
		 *    (ethertype).
		 *  The firmware only passes up SNAP frames converting
		 *    all RX Data from 802.11 to 802.2/LLC/SNAP frames.
		 *  To create the Ethernet II, just move the src, dst address
		 *    right before the snap_type.
		 */
		eth = (struct ethhdr *)
			((u8 *)&rx_pkt_hdr->eth803_hdr
			 + sizeof(rx_pkt_hdr->eth803_hdr) +
			 sizeof(rx_pkt_hdr->rfc1042_hdr)
			 - sizeof(rx_pkt_hdr->eth803_hdr.h_dest)
			 - sizeof(rx_pkt_hdr->eth803_hdr.h_source)
			 - sizeof(rx_pkt_hdr->rfc1042_hdr.snap_type));

		memcpy(eth->h_source, rx_pkt_hdr->eth803_hdr.h_source,
		       sizeof(eth->h_source));
		memcpy(eth->h_dest, rx_pkt_hdr->eth803_hdr.h_dest,
		       sizeof(eth->h_dest));

		/*
		 * Chop off the rxpd + the excess memory from the 802.2/llc/snap
		 * header that was removed.
		 */
		hdr_chop = (u8 *)eth - (u8 *)local_rx_pd;
	} else {
		/* Chop off the rxpd */
		hdr_chop = (u8 *)&rx_pkt_hdr->eth803_hdr - (u8 *)local_rx_pd;
	}

	/*
	 * Chop off the leading header bytes so the it points to the start of
	 * either the reconstructed EthII frame or the 802.2/llc/snap frame
	 */
	skb_pull(skb, hdr_chop);

	if (priv->hs2_enabled &&
	    nxpwifi_discard_gratuitous_arp(priv, skb)) {
		nxpwifi_dbg(priv->adapter, INFO, "Bypassed Gratuitous ARP\n");
		dev_kfree_skb_any(skb);
		return 0;
	}

	/* Only stash RX bitrate for unicast packets. */
	if (likely(!is_multicast_ether_addr(rx_pkt_hdr->eth803_hdr.h_dest))) {
		priv->rxpd_rate = local_rx_pd->rx_rate;
		priv->rxpd_htinfo = local_rx_pd->rate_info;
	}

	if (GET_BSS_ROLE(priv) == NXPWIFI_BSS_ROLE_STA ||
	    GET_BSS_ROLE(priv) == NXPWIFI_BSS_ROLE_UAP) {
		adj_rx_rate = nxpwifi_adjust_data_rate(priv,
						       local_rx_pd->rx_rate,
						       local_rx_pd->rate_info);
		nxpwifi_hist_data_add(priv, adj_rx_rate, local_rx_pd->snr,
				      local_rx_pd->nf);
	}

	ret = nxpwifi_recv_packet(priv, skb);
	if (ret)
		nxpwifi_dbg(priv->adapter, ERROR,
			    "recv packet failed\n");

	return ret;
}

/*
 * Process a received buffer on the STA path.
 * Validate RxPD and lengths, handle monitor/mgmt frames, fast-path
 * non-unicast frames, and feed unicast data into 11n reorder/BA logic.
 */
int nxpwifi_process_sta_rx_packet(struct nxpwifi_private *priv,
				  struct sk_buff *skb)
{
	struct nxpwifi_adapter *adapter = priv->adapter;
	int ret = 0;
	struct rxpd *local_rx_pd;
	struct rx_packet_hdr *rx_pkt_hdr;
	u8 ta[ETH_ALEN];
	u16 rx_pkt_type, rx_pkt_offset, rx_pkt_length, seq_num;

	local_rx_pd = (struct rxpd *)(skb->data);
	rx_pkt_type = le16_to_cpu(local_rx_pd->rx_pkt_type);
	rx_pkt_offset = le16_to_cpu(local_rx_pd->rx_pkt_offset);
	rx_pkt_length = le16_to_cpu(local_rx_pd->rx_pkt_length);
	seq_num = le16_to_cpu(local_rx_pd->seq_num);

	rx_pkt_hdr = (void *)local_rx_pd + rx_pkt_offset;

	if ((rx_pkt_offset + rx_pkt_length) > skb->len ||
	    sizeof(rx_pkt_hdr->eth803_hdr) + rx_pkt_offset > skb->len) {
		nxpwifi_dbg(adapter, ERROR,
			    "wrong rx packet: len=%d, rx_pkt_offset=%d, rx_pkt_length=%d\n",
			    skb->len, rx_pkt_offset, rx_pkt_length);
		priv->stats.rx_dropped++;
		dev_kfree_skb_any(skb);
		return ret;
	}

	if (priv->adapter->enable_net_mon && rx_pkt_type == PKT_TYPE_802DOT11) {
		ret = nxpwifi_recv_packet_to_monif(priv, skb);
		if (ret)
			dev_kfree_skb_any(skb);
		return ret;
	}

	if (rx_pkt_type == PKT_TYPE_MGMT) {
		ret = nxpwifi_process_mgmt_packet(priv, skb);
		if (ret && (ret != -EINPROGRESS))
			nxpwifi_dbg(adapter, DATA, "Rx of mgmt packet failed");
		if (ret != -EINPROGRESS)
			dev_kfree_skb_any(skb);
		return ret;
	}

	/*
	 * If the packet is not an unicast packet then send the packet
	 * directly to os. Don't pass thru rx reordering
	 */
	if (!IS_11N_ENABLED(priv) ||
	    !ether_addr_equal_unaligned(priv->curr_addr,
					rx_pkt_hdr->eth803_hdr.h_dest)) {
		nxpwifi_process_rx_packet(priv, skb);
		return ret;
	}

	if (nxpwifi_queuing_ra_based(priv)) {
		memcpy(ta, rx_pkt_hdr->eth803_hdr.h_source, ETH_ALEN);
	} else {
		if (rx_pkt_type != PKT_TYPE_BAR &&
		    local_rx_pd->priority < MAX_NUM_TID)
			priv->rx_seq[local_rx_pd->priority] = seq_num;
		memcpy(ta, priv->curr_bss_params.bss_descriptor.mac_address,
		       ETH_ALEN);
	}

	/* Reorder and send to OS */
	ret = nxpwifi_11n_rx_reorder_pkt(priv, seq_num, local_rx_pd->priority,
					 ta, (u8)rx_pkt_type, skb);

	if (ret || rx_pkt_type == PKT_TYPE_BAR)
		dev_kfree_skb_any(skb);

	if (ret)
		priv->stats.rx_dropped++;

	return ret;
}
