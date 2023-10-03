// SPDX-License-Identifier: GPL-2.0-only
/*
 * NXP Wireless LAN device driver: station RX data handling
 *
 * Copyright 2011-2020 NXP
 */

#include <uapi/linux/ipv6.h>
#include <net/ndisc.h>
#include "decl.h"
#include "ioctl.h"
#include "util.h"
#include "fw.h"
#include "main.h"
#include "11n_aggr.h"
#include "11n_rxreorder.h"

/* This function checks if a frame is IPv4 ARP or IPv6 Neighbour advertisement
 * frame. If frame has both source and destination mac address as same, this
 * function drops such gratuitous frames.
 */
static bool
mwifiex_discard_gratuitous_arp(struct mwifiex_private *priv,
			       struct sk_buff *skb)
{
	const struct mwifiex_arp_eth_header *arp;
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
		if (NDISC_NEIGHBOUR_ADVERTISEMENT == icmpv6->icmp6_type) {
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
 * This function processes the received packet and forwards it
 * to kernel/upper layer.
 *
 * This function parses through the received packet and determines
 * if it is a debug packet or normal packet.
 *
 * For non-debug packets, the function chops off unnecessary leading
 * header bytes, reconstructs the packet as an ethernet frame or
 * 802.2/llc/snap frame as required, and sends it to kernel/upper layer.
 *
 * The completion callback is called after processing in complete.
 */
int mwifiex_process_rx_packet(struct mwifiex_private *priv,
			      struct sk_buff *skb)
{
	int ret;
	struct rx_packet_hdr *rx_pkt_hdr;
	struct rxpd *local_rx_pd;
	int hdr_chop;
	struct ethhdr *eth;
	u16 rx_pkt_off, rx_pkt_len;
	u8 *offset;
	u8 adj_rx_rate = 0;

	local_rx_pd = (struct rxpd *) (skb->data);

	rx_pkt_off = le16_to_cpu(local_rx_pd->rx_pkt_offset);
	rx_pkt_len = le16_to_cpu(local_rx_pd->rx_pkt_length);
	rx_pkt_hdr = (void *)local_rx_pd + rx_pkt_off;

	if (sizeof(*rx_pkt_hdr) + rx_pkt_off > skb->len) {
		mwifiex_dbg(priv->adapter, ERROR,
			    "wrong rx packet offset: len=%d, rx_pkt_off=%d\n",
			    skb->len, rx_pkt_off);
		priv->stats.rx_dropped++;
		dev_kfree_skb_any(skb);
		return -1;
	}

	if ((!memcmp(&rx_pkt_hdr->rfc1042_hdr, bridge_tunnel_header,
		     sizeof(bridge_tunnel_header))) ||
	    (!memcmp(&rx_pkt_hdr->rfc1042_hdr, rfc1042_header,
		     sizeof(rfc1042_header)) &&
	     ntohs(rx_pkt_hdr->rfc1042_hdr.snap_type) != ETH_P_AARP &&
	     ntohs(rx_pkt_hdr->rfc1042_hdr.snap_type) != ETH_P_IPX)) {
		/*
		 *  Replace the 803 header and rfc1042 header (llc/snap) with an
		 *    EthernetII header, keep the src/dst and snap_type
		 *    (ethertype).
		 *  The firmware only passes up SNAP frames converting
		 *    all RX Data from 802.11 to 802.2/LLC/SNAP frames.
		 *  To create the Ethernet II, just move the src, dst address
		 *    right before the snap_type.
		 */
		eth = (struct ethhdr *)
			((u8 *) &rx_pkt_hdr->eth803_hdr
			 + sizeof(rx_pkt_hdr->eth803_hdr) +
			 sizeof(rx_pkt_hdr->rfc1042_hdr)
			 - sizeof(rx_pkt_hdr->eth803_hdr.h_dest)
			 - sizeof(rx_pkt_hdr->eth803_hdr.h_source)
			 - sizeof(rx_pkt_hdr->rfc1042_hdr.snap_type));

		memcpy(eth->h_source, rx_pkt_hdr->eth803_hdr.h_source,
		       sizeof(eth->h_source));
		memcpy(eth->h_dest, rx_pkt_hdr->eth803_hdr.h_dest,
		       sizeof(eth->h_dest));

		/* Chop off the rxpd + the excess memory from the 802.2/llc/snap
		   header that was removed. */
		hdr_chop = (u8 *) eth - (u8 *) local_rx_pd;
	} else {
		/* Chop off the rxpd */
		hdr_chop = (u8 *) &rx_pkt_hdr->eth803_hdr -
			(u8 *) local_rx_pd;
	}

	/* Chop off the leading header bytes so the it points to the start of
	   either the reconstructed EthII frame or the 802.2/llc/snap frame */
	skb_pull(skb, hdr_chop);

	if (priv->hs2_enabled &&
	    mwifiex_discard_gratuitous_arp(priv, skb)) {
		mwifiex_dbg(priv->adapter, INFO, "Bypassed Gratuitous ARP\n");
		dev_kfree_skb_any(skb);
		return 0;
	}

	if (ISSUPP_TDLS_ENABLED(priv->adapter->fw_cap_info) &&
	    ntohs(rx_pkt_hdr->eth803_hdr.h_proto) == ETH_P_TDLS) {
		offset = (u8 *)local_rx_pd + rx_pkt_off;
		mwifiex_process_tdls_action_frame(priv, offset, rx_pkt_len);
	}

	/* Only stash RX bitrate for unicast packets. */
	if (likely(!is_multicast_ether_addr(rx_pkt_hdr->eth803_hdr.h_dest))) {
		priv->rxpd_rate = local_rx_pd->rx_rate;
		priv->rxpd_htinfo = local_rx_pd->ht_info;
	}

	if (GET_BSS_ROLE(priv) == MWIFIEX_BSS_ROLE_STA ||
	    GET_BSS_ROLE(priv) == MWIFIEX_BSS_ROLE_UAP) {
		adj_rx_rate = mwifiex_adjust_data_rate(priv,
						       local_rx_pd->rx_rate,
						       local_rx_pd->ht_info);
		mwifiex_hist_data_add(priv, adj_rx_rate, local_rx_pd->snr,
				      local_rx_pd->nf);
	}

	ret = mwifiex_recv_packet(priv, skb);
	if (ret == -1)
		mwifiex_dbg(priv->adapter, ERROR,
			    "recv packet failed\n");

	return ret;
}

/*
 * This function processes the received buffer.
 *
 * The function looks into the RxPD and performs sanity tests on the
 * received buffer to ensure its a valid packet, before processing it
 * further. If the packet is determined to be aggregated, it is
 * de-aggregated accordingly. Non-unicast packets are sent directly to
 * the kernel/upper layers. Unicast packets are handed over to the
 * Rx reordering routine if 11n is enabled.
 *
 * The completion callback is called after processing in complete.
 */
int mwifiex_process_sta_rx_packet(struct mwifiex_private *priv,
				  struct sk_buff *skb)
{
	struct mwifiex_adapter *adapter = priv->adapter;
	int ret = 0;
	struct rxpd *local_rx_pd;
	struct rx_packet_hdr *rx_pkt_hdr;
	u8 ta[ETH_ALEN];
	u16 rx_pkt_type, rx_pkt_offset, rx_pkt_length, seq_num;
	struct mwifiex_sta_node *sta_ptr;

	local_rx_pd = (struct rxpd *) (skb->data);
	rx_pkt_type = le16_to_cpu(local_rx_pd->rx_pkt_type);
	rx_pkt_offset = le16_to_cpu(local_rx_pd->rx_pkt_offset);
	rx_pkt_length = le16_to_cpu(local_rx_pd->rx_pkt_length);
	seq_num = le16_to_cpu(local_rx_pd->seq_num);

	rx_pkt_hdr = (void *)local_rx_pd + rx_pkt_offset;

	if ((rx_pkt_offset + rx_pkt_length) > skb->len ||
	    sizeof(rx_pkt_hdr->eth803_hdr) + rx_pkt_offset > skb->len) {
		mwifiex_dbg(adapter, ERROR,
			    "wrong rx packet: len=%d, rx_pkt_offset=%d, rx_pkt_length=%d\n",
			    skb->len, rx_pkt_offset, rx_pkt_length);
		priv->stats.rx_dropped++;
		dev_kfree_skb_any(skb);
		return ret;
	}

	if (rx_pkt_type == PKT_TYPE_MGMT) {
		ret = mwifiex_process_mgmt_packet(priv, skb);
		if (ret)
			mwifiex_dbg(adapter, DATA, "Rx of mgmt packet failed");
		dev_kfree_skb_any(skb);
		return ret;
	}

	/*
	 * If the packet is not an unicast packet then send the packet
	 * directly to os. Don't pass thru rx reordering
	 */
	if ((!IS_11N_ENABLED(priv) &&
	     !(ISSUPP_TDLS_ENABLED(priv->adapter->fw_cap_info) &&
	       !(local_rx_pd->flags & MWIFIEX_RXPD_FLAGS_TDLS_PACKET))) ||
	    !ether_addr_equal_unaligned(priv->curr_addr, rx_pkt_hdr->eth803_hdr.h_dest)) {
		mwifiex_process_rx_packet(priv, skb);
		return ret;
	}

	if (mwifiex_queuing_ra_based(priv) ||
	    (ISSUPP_TDLS_ENABLED(priv->adapter->fw_cap_info) &&
	     local_rx_pd->flags & MWIFIEX_RXPD_FLAGS_TDLS_PACKET)) {
		memcpy(ta, rx_pkt_hdr->eth803_hdr.h_source, ETH_ALEN);
		if (local_rx_pd->flags & MWIFIEX_RXPD_FLAGS_TDLS_PACKET &&
		    local_rx_pd->priority < MAX_NUM_TID) {
			sta_ptr = mwifiex_get_sta_entry(priv, ta);
			if (sta_ptr)
				sta_ptr->rx_seq[local_rx_pd->priority] =
					      le16_to_cpu(local_rx_pd->seq_num);
			mwifiex_auto_tdls_update_peer_signal(priv, ta,
							     local_rx_pd->snr,
							     local_rx_pd->nf);
		}
	} else {
		if (rx_pkt_type != PKT_TYPE_BAR &&
		    local_rx_pd->priority < MAX_NUM_TID)
			priv->rx_seq[local_rx_pd->priority] = seq_num;
		memcpy(ta, priv->curr_bss_params.bss_descriptor.mac_address,
		       ETH_ALEN);
	}

	/* Reorder and send to OS */
	ret = mwifiex_11n_rx_reorder_pkt(priv, seq_num, local_rx_pd->priority,
					 ta, (u8) rx_pkt_type, skb);

	if (ret || (rx_pkt_type == PKT_TYPE_BAR))
		dev_kfree_skb_any(skb);

	if (ret)
		priv->stats.rx_dropped++;

	return ret;
}
