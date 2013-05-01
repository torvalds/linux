/*
 * Marvell Wireless LAN device driver: station RX data handling
 *
 * Copyright (C) 2011, Marvell International Ltd.
 *
 * This software file (the "File") is distributed by Marvell International
 * Ltd. under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 */

#include "decl.h"
#include "ioctl.h"
#include "util.h"
#include "fw.h"
#include "main.h"
#include "11n_aggr.h"
#include "11n_rxreorder.h"

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
	struct ethhdr *eth_hdr;
	u8 rfc1042_eth_hdr[ETH_ALEN] = { 0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00 };

	local_rx_pd = (struct rxpd *) (skb->data);

	rx_pkt_hdr = (void *)local_rx_pd +
		     le16_to_cpu(local_rx_pd->rx_pkt_offset);

	if (!memcmp(&rx_pkt_hdr->rfc1042_hdr,
		    rfc1042_eth_hdr, sizeof(rfc1042_eth_hdr))) {
		/*
		 *  Replace the 803 header and rfc1042 header (llc/snap) with an
		 *    EthernetII header, keep the src/dst and snap_type
		 *    (ethertype).
		 *  The firmware only passes up SNAP frames converting
		 *    all RX Data from 802.11 to 802.2/LLC/SNAP frames.
		 *  To create the Ethernet II, just move the src, dst address
		 *    right before the snap_type.
		 */
		eth_hdr = (struct ethhdr *)
			((u8 *) &rx_pkt_hdr->eth803_hdr
			 + sizeof(rx_pkt_hdr->eth803_hdr) +
			 sizeof(rx_pkt_hdr->rfc1042_hdr)
			 - sizeof(rx_pkt_hdr->eth803_hdr.h_dest)
			 - sizeof(rx_pkt_hdr->eth803_hdr.h_source)
			 - sizeof(rx_pkt_hdr->rfc1042_hdr.snap_type));

		memcpy(eth_hdr->h_source, rx_pkt_hdr->eth803_hdr.h_source,
		       sizeof(eth_hdr->h_source));
		memcpy(eth_hdr->h_dest, rx_pkt_hdr->eth803_hdr.h_dest,
		       sizeof(eth_hdr->h_dest));

		/* Chop off the rxpd + the excess memory from the 802.2/llc/snap
		   header that was removed. */
		hdr_chop = (u8 *) eth_hdr - (u8 *) local_rx_pd;
	} else {
		/* Chop off the rxpd */
		hdr_chop = (u8 *) &rx_pkt_hdr->eth803_hdr -
			(u8 *) local_rx_pd;
	}

	/* Chop off the leading header bytes so the it points to the start of
	   either the reconstructed EthII frame or the 802.2/llc/snap frame */
	skb_pull(skb, hdr_chop);

	priv->rxpd_rate = local_rx_pd->rx_rate;

	priv->rxpd_htinfo = local_rx_pd->ht_info;

	ret = mwifiex_recv_packet(priv, skb);
	if (ret == -1)
		dev_err(priv->adapter->dev, "recv packet failed\n");

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

	local_rx_pd = (struct rxpd *) (skb->data);
	rx_pkt_type = le16_to_cpu(local_rx_pd->rx_pkt_type);
	rx_pkt_offset = le16_to_cpu(local_rx_pd->rx_pkt_offset);
	rx_pkt_length = le16_to_cpu(local_rx_pd->rx_pkt_length);
	seq_num = le16_to_cpu(local_rx_pd->seq_num);

	rx_pkt_hdr = (void *)local_rx_pd + rx_pkt_offset;

	if ((rx_pkt_offset + rx_pkt_length) > (u16) skb->len) {
		dev_err(adapter->dev,
			"wrong rx packet: len=%d, rx_pkt_offset=%d, rx_pkt_length=%d\n",
			skb->len, rx_pkt_offset, rx_pkt_length);
		priv->stats.rx_dropped++;

		if (adapter->if_ops.data_complete)
			adapter->if_ops.data_complete(adapter, skb);
		else
			dev_kfree_skb_any(skb);

		return ret;
	}

	if (rx_pkt_type == PKT_TYPE_AMSDU) {
		struct sk_buff_head list;
		struct sk_buff *rx_skb;

		__skb_queue_head_init(&list);

		skb_pull(skb, rx_pkt_offset);
		skb_trim(skb, rx_pkt_length);

		ieee80211_amsdu_to_8023s(skb, &list, priv->curr_addr,
					 priv->wdev->iftype, 0, false);

		while (!skb_queue_empty(&list)) {
			rx_skb = __skb_dequeue(&list);
			ret = mwifiex_recv_packet(priv, rx_skb);
			if (ret == -1)
				dev_err(adapter->dev, "Rx of A-MSDU failed");
		}
		return 0;
	} else if (rx_pkt_type == PKT_TYPE_MGMT) {
		ret = mwifiex_process_mgmt_packet(priv, skb);
		if (ret)
			dev_err(adapter->dev, "Rx of mgmt packet failed");
		dev_kfree_skb_any(skb);
		return ret;
	}

	/*
	 * If the packet is not an unicast packet then send the packet
	 * directly to os. Don't pass thru rx reordering
	 */
	if (!IS_11N_ENABLED(priv) ||
	    memcmp(priv->curr_addr, rx_pkt_hdr->eth803_hdr.h_dest, ETH_ALEN)) {
		mwifiex_process_rx_packet(priv, skb);
		return ret;
	}

	if (mwifiex_queuing_ra_based(priv)) {
		memcpy(ta, rx_pkt_hdr->eth803_hdr.h_source, ETH_ALEN);
	} else {
		if (rx_pkt_type != PKT_TYPE_BAR)
			priv->rx_seq[local_rx_pd->priority] = seq_num;
		memcpy(ta, priv->curr_bss_params.bss_descriptor.mac_address,
		       ETH_ALEN);
	}

	/* Reorder and send to OS */
	ret = mwifiex_11n_rx_reorder_pkt(priv, seq_num, local_rx_pd->priority,
					 ta, (u8) rx_pkt_type, skb);

	if (ret || (rx_pkt_type == PKT_TYPE_BAR)) {
		if (adapter->if_ops.data_complete)
			adapter->if_ops.data_complete(adapter, skb);
		else
			dev_kfree_skb_any(skb);
	}

	if (ret)
		priv->stats.rx_dropped++;

	return ret;
}
