// SPDX-License-Identifier: GPL-2.0-or-later
/* Broadcom NetXtreme-C/E network driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <net/netdev_queues.h>
#include <net/ip.h>
#include <net/ipv6.h>
#include <net/udp.h>
#include <net/tso.h>
#include <linux/bnxt/hsi.h>

#include "bnxt.h"
#include "bnxt_gso.h"

static u32 bnxt_sw_gso_lhint(unsigned int len)
{
	if (len <= 512)
		return TX_BD_FLAGS_LHINT_512_AND_SMALLER;
	else if (len <= 1023)
		return TX_BD_FLAGS_LHINT_512_TO_1023;
	else if (len <= 2047)
		return TX_BD_FLAGS_LHINT_1024_TO_2047;
	else
		return TX_BD_FLAGS_LHINT_2048_AND_LARGER;
}

netdev_tx_t bnxt_sw_udp_gso_xmit(struct bnxt *bp,
				 struct bnxt_tx_ring_info *txr,
				 struct netdev_queue *txq,
				 struct sk_buff *skb)
{
	unsigned int last_unmap_len __maybe_unused = 0;
	dma_addr_t last_unmap_addr __maybe_unused = 0;
	struct bnxt_sw_tx_bd *last_unmap_buf = NULL;
	unsigned int hdr_len, mss, num_segs;
	struct pci_dev *pdev = bp->pdev;
	unsigned int total_payload;
	struct tso_dma_map map;
	u32 vlan_tag_flags = 0;
	int i, bds_needed;
	struct tso_t tso;
	u16 cfa_action;
	__le32 csum;
	u16 prod;

	hdr_len = tso_start(skb, &tso);
	mss = skb_shinfo(skb)->gso_size;
	total_payload = skb->len - hdr_len;
	num_segs = DIV_ROUND_UP(total_payload, mss);

	if (unlikely(num_segs <= 1))
		goto drop;

	/* Upper bound on the number of descriptors needed.
	 *
	 * Each segment uses 1 long BD + 1 ext BD + payload BDs, which is
	 * at most num_segs + nr_frags (each frag boundary crossing adds at
	 * most 1 extra BD).
	 */
	bds_needed = 3 * num_segs + skb_shinfo(skb)->nr_frags + 1;

	if (unlikely(bnxt_tx_avail(bp, txr) < bds_needed)) {
		netif_txq_try_stop(txq, bnxt_tx_avail(bp, txr),
				   bp->tx_wake_thresh);
		return NETDEV_TX_BUSY;
	}

	/* BD backpressure alone cannot prevent overwriting in-flight
	 * headers in the inline buffer. Check slot availability directly.
	 */
	if (!netif_txq_maybe_stop(txq, bnxt_inline_avail(txr),
				  num_segs, num_segs))
		return NETDEV_TX_BUSY;

	if (unlikely(tso_dma_map_init(&map, &pdev->dev, skb, hdr_len)))
		goto drop;

	cfa_action = bnxt_xmit_get_cfa_action(skb);
	if (skb_vlan_tag_present(skb)) {
		vlan_tag_flags = TX_BD_CFA_META_KEY_VLAN |
				 skb_vlan_tag_get(skb);
		if (skb->vlan_proto == htons(ETH_P_8021Q))
			vlan_tag_flags |= 1 << TX_BD_CFA_META_TPID_SHIFT;
	}

	csum = cpu_to_le32(TX_BD_FLAGS_TCP_UDP_CHKSUM);
	if (!tso.ipv6)
		csum |= cpu_to_le32(TX_BD_FLAGS_IP_CKSUM);

	prod = txr->tx_prod;

	for (i = 0; i < num_segs; i++) {
		unsigned int seg_payload = min_t(unsigned int, mss,
						 total_payload - i * mss);
		u16 slot = (txr->tx_inline_prod + i) &
			   (BNXT_SW_USO_MAX_SEGS - 1);
		struct bnxt_sw_tx_bd *tx_buf;
		unsigned int mapping_len;
		dma_addr_t this_hdr_dma;
		unsigned int chunk_len;
		unsigned int offset;
		dma_addr_t dma_addr;
		struct tx_bd *txbd;
		struct udphdr *uh;
		void *this_hdr;
		int bd_count;
		bool last;
		u32 flags;

		last = (i == num_segs - 1);
		offset = slot * TSO_HEADER_SIZE;
		this_hdr = txr->tx_inline_buf + offset;
		this_hdr_dma = txr->tx_inline_dma + offset;

		tso_build_hdr(skb, this_hdr, &tso, seg_payload, last);

		/* Zero stale csum fields copied from the original skb;
		 * HW offload recomputes from scratch.
		 */
		uh = this_hdr + skb_transport_offset(skb);
		uh->check = 0;
		if (!tso.ipv6) {
			struct iphdr *iph = this_hdr + skb_network_offset(skb);

			iph->check = 0;
		}

		dma_sync_single_for_device(&pdev->dev, this_hdr_dma,
					   hdr_len, DMA_TO_DEVICE);

		bd_count = tso_dma_map_count(&map, seg_payload);

		tx_buf = &txr->tx_buf_ring[RING_TX(bp, prod)];
		txbd = &txr->tx_desc_ring[TX_RING(bp, prod)][TX_IDX(prod)];

		tx_buf->skb = skb;
		tx_buf->nr_frags = bd_count;
		tx_buf->is_push = 0;
		tx_buf->is_ts_pkt = 0;

		dma_unmap_addr_set(tx_buf, mapping, this_hdr_dma);
		dma_unmap_len_set(tx_buf, len, 0);

		if (last) {
			tx_buf->is_sw_gso = BNXT_SW_GSO_LAST;
			tso_dma_map_completion_save(&map, &tx_buf->sw_gso_cstate);
		} else {
			tx_buf->is_sw_gso = BNXT_SW_GSO_MID;
		}

		flags = (hdr_len << TX_BD_LEN_SHIFT) |
			TX_BD_TYPE_LONG_TX_BD |
			TX_BD_CNT(2 + bd_count);

		flags |= bnxt_sw_gso_lhint(hdr_len + seg_payload);

		txbd->tx_bd_len_flags_type = cpu_to_le32(flags);
		txbd->tx_bd_haddr = cpu_to_le64(this_hdr_dma);
		txbd->tx_bd_opaque = SET_TX_OPAQUE(bp, txr, prod,
						   2 + bd_count);

		prod = NEXT_TX(prod);
		bnxt_init_ext_bd(bp, txr, prod, csum,
				 vlan_tag_flags, cfa_action);

		/* set dma_unmap_len on the LAST BD touching each
		 * region. Since completions are in-order, the last segment
		 * completes after all earlier ones, so the unmap is safe.
		 */
		while (tso_dma_map_next(&map, &dma_addr, &chunk_len,
					&mapping_len, seg_payload)) {
			prod = NEXT_TX(prod);
			txbd = &txr->tx_desc_ring[TX_RING(bp, prod)][TX_IDX(prod)];
			tx_buf = &txr->tx_buf_ring[RING_TX(bp, prod)];

			txbd->tx_bd_haddr = cpu_to_le64(dma_addr);
			dma_unmap_addr_set(tx_buf, mapping, dma_addr);
			dma_unmap_len_set(tx_buf, len, 0);
			tx_buf->skb = NULL;
			tx_buf->is_sw_gso = 0;

			if (mapping_len) {
				if (last_unmap_buf) {
					dma_unmap_addr_set(last_unmap_buf,
							   mapping,
							   last_unmap_addr);
					dma_unmap_len_set(last_unmap_buf,
							  len,
							  last_unmap_len);
				}
				last_unmap_addr = dma_addr;
				last_unmap_len = mapping_len;
			}
			last_unmap_buf = tx_buf;

			flags = chunk_len << TX_BD_LEN_SHIFT;
			txbd->tx_bd_len_flags_type = cpu_to_le32(flags);
			txbd->tx_bd_opaque = 0;

			seg_payload -= chunk_len;
		}

		txbd->tx_bd_len_flags_type |=
			cpu_to_le32(TX_BD_FLAGS_PACKET_END);

		prod = NEXT_TX(prod);
	}

	if (last_unmap_buf) {
		dma_unmap_addr_set(last_unmap_buf, mapping, last_unmap_addr);
		dma_unmap_len_set(last_unmap_buf, len, last_unmap_len);
	}

	txr->tx_inline_prod += num_segs;

	netdev_tx_sent_queue(txq, skb->len);

	WRITE_ONCE(txr->tx_prod, prod);
	/* Sync BDs before doorbell */
	wmb();
	bnxt_db_write(bp, &txr->tx_db, prod);

	if (unlikely(bnxt_tx_avail(bp, txr) <= bp->tx_wake_thresh))
		netif_txq_try_stop(txq, bnxt_tx_avail(bp, txr),
				   bp->tx_wake_thresh);

	return NETDEV_TX_OK;

drop:
	dev_kfree_skb_any(skb);
	dev_core_stats_tx_dropped_inc(bp->dev);
	return NETDEV_TX_OK;
}
