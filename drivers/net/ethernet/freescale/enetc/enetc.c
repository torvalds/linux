// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/* Copyright 2017-2019 NXP */

#include "enetc.h"
#include <linux/bpf_trace.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/vmalloc.h>
#include <linux/ptp_classify.h>
#include <net/ip6_checksum.h>
#include <net/pkt_sched.h>
#include <net/tso.h>

u32 enetc_port_mac_rd(struct enetc_si *si, u32 reg)
{
	return enetc_port_rd(&si->hw, reg);
}
EXPORT_SYMBOL_GPL(enetc_port_mac_rd);

void enetc_port_mac_wr(struct enetc_si *si, u32 reg, u32 val)
{
	enetc_port_wr(&si->hw, reg, val);
	if (si->hw_features & ENETC_SI_F_QBU)
		enetc_port_wr(&si->hw, reg + ENETC_PMAC_OFFSET, val);
}
EXPORT_SYMBOL_GPL(enetc_port_mac_wr);

static void enetc_change_preemptible_tcs(struct enetc_ndev_priv *priv,
					 u8 preemptible_tcs)
{
	priv->preemptible_tcs = preemptible_tcs;
	enetc_mm_commit_preemptible_tcs(priv);
}

static int enetc_num_stack_tx_queues(struct enetc_ndev_priv *priv)
{
	int num_tx_rings = priv->num_tx_rings;

	if (priv->xdp_prog)
		return num_tx_rings - num_possible_cpus();

	return num_tx_rings;
}

static struct enetc_bdr *enetc_rx_ring_from_xdp_tx_ring(struct enetc_ndev_priv *priv,
							struct enetc_bdr *tx_ring)
{
	int index = &priv->tx_ring[tx_ring->index] - priv->xdp_tx_ring;

	return priv->rx_ring[index];
}

static struct sk_buff *enetc_tx_swbd_get_skb(struct enetc_tx_swbd *tx_swbd)
{
	if (tx_swbd->is_xdp_tx || tx_swbd->is_xdp_redirect)
		return NULL;

	return tx_swbd->skb;
}

static struct xdp_frame *
enetc_tx_swbd_get_xdp_frame(struct enetc_tx_swbd *tx_swbd)
{
	if (tx_swbd->is_xdp_redirect)
		return tx_swbd->xdp_frame;

	return NULL;
}

static void enetc_unmap_tx_buff(struct enetc_bdr *tx_ring,
				struct enetc_tx_swbd *tx_swbd)
{
	/* For XDP_TX, pages come from RX, whereas for the other contexts where
	 * we have is_dma_page_set, those come from skb_frag_dma_map. We need
	 * to match the DMA mapping length, so we need to differentiate those.
	 */
	if (tx_swbd->is_dma_page)
		dma_unmap_page(tx_ring->dev, tx_swbd->dma,
			       tx_swbd->is_xdp_tx ? PAGE_SIZE : tx_swbd->len,
			       tx_swbd->dir);
	else
		dma_unmap_single(tx_ring->dev, tx_swbd->dma,
				 tx_swbd->len, tx_swbd->dir);
	tx_swbd->dma = 0;
}

static void enetc_free_tx_frame(struct enetc_bdr *tx_ring,
				struct enetc_tx_swbd *tx_swbd)
{
	struct xdp_frame *xdp_frame = enetc_tx_swbd_get_xdp_frame(tx_swbd);
	struct sk_buff *skb = enetc_tx_swbd_get_skb(tx_swbd);

	if (tx_swbd->dma)
		enetc_unmap_tx_buff(tx_ring, tx_swbd);

	if (xdp_frame) {
		xdp_return_frame(tx_swbd->xdp_frame);
		tx_swbd->xdp_frame = NULL;
	} else if (skb) {
		dev_kfree_skb_any(skb);
		tx_swbd->skb = NULL;
	}
}

/* Let H/W know BD ring has been updated */
static void enetc_update_tx_ring_tail(struct enetc_bdr *tx_ring)
{
	/* includes wmb() */
	enetc_wr_reg_hot(tx_ring->tpir, tx_ring->next_to_use);
}

static int enetc_ptp_parse(struct sk_buff *skb, u8 *udp,
			   u8 *msgtype, u8 *twostep,
			   u16 *correction_offset, u16 *body_offset)
{
	unsigned int ptp_class;
	struct ptp_header *hdr;
	unsigned int type;
	u8 *base;

	ptp_class = ptp_classify_raw(skb);
	if (ptp_class == PTP_CLASS_NONE)
		return -EINVAL;

	hdr = ptp_parse_header(skb, ptp_class);
	if (!hdr)
		return -EINVAL;

	type = ptp_class & PTP_CLASS_PMASK;
	if (type == PTP_CLASS_IPV4 || type == PTP_CLASS_IPV6)
		*udp = 1;
	else
		*udp = 0;

	*msgtype = ptp_get_msgtype(hdr, ptp_class);
	*twostep = hdr->flag_field[0] & 0x2;

	base = skb_mac_header(skb);
	*correction_offset = (u8 *)&hdr->correction - base;
	*body_offset = (u8 *)hdr + sizeof(struct ptp_header) - base;

	return 0;
}

static int enetc_map_tx_buffs(struct enetc_bdr *tx_ring, struct sk_buff *skb)
{
	bool do_vlan, do_onestep_tstamp = false, do_twostep_tstamp = false;
	struct enetc_ndev_priv *priv = netdev_priv(tx_ring->ndev);
	struct enetc_hw *hw = &priv->si->hw;
	struct enetc_tx_swbd *tx_swbd;
	int len = skb_headlen(skb);
	union enetc_tx_bd temp_bd;
	u8 msgtype, twostep, udp;
	union enetc_tx_bd *txbd;
	u16 offset1, offset2;
	int i, count = 0;
	skb_frag_t *frag;
	unsigned int f;
	dma_addr_t dma;
	u8 flags = 0;

	i = tx_ring->next_to_use;
	txbd = ENETC_TXBD(*tx_ring, i);
	prefetchw(txbd);

	dma = dma_map_single(tx_ring->dev, skb->data, len, DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(tx_ring->dev, dma)))
		goto dma_err;

	temp_bd.addr = cpu_to_le64(dma);
	temp_bd.buf_len = cpu_to_le16(len);
	temp_bd.lstatus = 0;

	tx_swbd = &tx_ring->tx_swbd[i];
	tx_swbd->dma = dma;
	tx_swbd->len = len;
	tx_swbd->is_dma_page = 0;
	tx_swbd->dir = DMA_TO_DEVICE;
	count++;

	do_vlan = skb_vlan_tag_present(skb);
	if (skb->cb[0] & ENETC_F_TX_ONESTEP_SYNC_TSTAMP) {
		if (enetc_ptp_parse(skb, &udp, &msgtype, &twostep, &offset1,
				    &offset2) ||
		    msgtype != PTP_MSGTYPE_SYNC || twostep)
			WARN_ONCE(1, "Bad packet for one-step timestamping\n");
		else
			do_onestep_tstamp = true;
	} else if (skb->cb[0] & ENETC_F_TX_TSTAMP) {
		do_twostep_tstamp = true;
	}

	tx_swbd->do_twostep_tstamp = do_twostep_tstamp;
	tx_swbd->qbv_en = !!(priv->active_offloads & ENETC_F_QBV);
	tx_swbd->check_wb = tx_swbd->do_twostep_tstamp || tx_swbd->qbv_en;

	if (do_vlan || do_onestep_tstamp || do_twostep_tstamp)
		flags |= ENETC_TXBD_FLAGS_EX;

	if (tx_ring->tsd_enable)
		flags |= ENETC_TXBD_FLAGS_TSE | ENETC_TXBD_FLAGS_TXSTART;

	/* first BD needs frm_len and offload flags set */
	temp_bd.frm_len = cpu_to_le16(skb->len);
	temp_bd.flags = flags;

	if (flags & ENETC_TXBD_FLAGS_TSE)
		temp_bd.txstart = enetc_txbd_set_tx_start(skb->skb_mstamp_ns,
							  flags);

	if (flags & ENETC_TXBD_FLAGS_EX) {
		u8 e_flags = 0;
		*txbd = temp_bd;
		enetc_clear_tx_bd(&temp_bd);

		/* add extension BD for VLAN and/or timestamping */
		flags = 0;
		tx_swbd++;
		txbd++;
		i++;
		if (unlikely(i == tx_ring->bd_count)) {
			i = 0;
			tx_swbd = tx_ring->tx_swbd;
			txbd = ENETC_TXBD(*tx_ring, 0);
		}
		prefetchw(txbd);

		if (do_vlan) {
			temp_bd.ext.vid = cpu_to_le16(skb_vlan_tag_get(skb));
			temp_bd.ext.tpid = 0; /* < C-TAG */
			e_flags |= ENETC_TXBD_E_FLAGS_VLAN_INS;
		}

		if (do_onestep_tstamp) {
			u32 lo, hi, val;
			u64 sec, nsec;
			u8 *data;

			lo = enetc_rd_hot(hw, ENETC_SICTR0);
			hi = enetc_rd_hot(hw, ENETC_SICTR1);
			sec = (u64)hi << 32 | lo;
			nsec = do_div(sec, 1000000000);

			/* Configure extension BD */
			temp_bd.ext.tstamp = cpu_to_le32(lo & 0x3fffffff);
			e_flags |= ENETC_TXBD_E_FLAGS_ONE_STEP_PTP;

			/* Update originTimestamp field of Sync packet
			 * - 48 bits seconds field
			 * - 32 bits nanseconds field
			 */
			data = skb_mac_header(skb);
			*(__be16 *)(data + offset2) =
				htons((sec >> 32) & 0xffff);
			*(__be32 *)(data + offset2 + 2) =
				htonl(sec & 0xffffffff);
			*(__be32 *)(data + offset2 + 6) = htonl(nsec);

			/* Configure single-step register */
			val = ENETC_PM0_SINGLE_STEP_EN;
			val |= ENETC_SET_SINGLE_STEP_OFFSET(offset1);
			if (udp)
				val |= ENETC_PM0_SINGLE_STEP_CH;

			enetc_port_mac_wr(priv->si, ENETC_PM0_SINGLE_STEP,
					  val);
		} else if (do_twostep_tstamp) {
			skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
			e_flags |= ENETC_TXBD_E_FLAGS_TWO_STEP_PTP;
		}

		temp_bd.ext.e_flags = e_flags;
		count++;
	}

	frag = &skb_shinfo(skb)->frags[0];
	for (f = 0; f < skb_shinfo(skb)->nr_frags; f++, frag++) {
		len = skb_frag_size(frag);
		dma = skb_frag_dma_map(tx_ring->dev, frag, 0, len,
				       DMA_TO_DEVICE);
		if (dma_mapping_error(tx_ring->dev, dma))
			goto dma_err;

		*txbd = temp_bd;
		enetc_clear_tx_bd(&temp_bd);

		flags = 0;
		tx_swbd++;
		txbd++;
		i++;
		if (unlikely(i == tx_ring->bd_count)) {
			i = 0;
			tx_swbd = tx_ring->tx_swbd;
			txbd = ENETC_TXBD(*tx_ring, 0);
		}
		prefetchw(txbd);

		temp_bd.addr = cpu_to_le64(dma);
		temp_bd.buf_len = cpu_to_le16(len);

		tx_swbd->dma = dma;
		tx_swbd->len = len;
		tx_swbd->is_dma_page = 1;
		tx_swbd->dir = DMA_TO_DEVICE;
		count++;
	}

	/* last BD needs 'F' bit set */
	flags |= ENETC_TXBD_FLAGS_F;
	temp_bd.flags = flags;
	*txbd = temp_bd;

	tx_ring->tx_swbd[i].is_eof = true;
	tx_ring->tx_swbd[i].skb = skb;

	enetc_bdr_idx_inc(tx_ring, &i);
	tx_ring->next_to_use = i;

	skb_tx_timestamp(skb);

	enetc_update_tx_ring_tail(tx_ring);

	return count;

dma_err:
	dev_err(tx_ring->dev, "DMA map error");

	do {
		tx_swbd = &tx_ring->tx_swbd[i];
		enetc_free_tx_frame(tx_ring, tx_swbd);
		if (i == 0)
			i = tx_ring->bd_count;
		i--;
	} while (count--);

	return 0;
}

static void enetc_map_tx_tso_hdr(struct enetc_bdr *tx_ring, struct sk_buff *skb,
				 struct enetc_tx_swbd *tx_swbd,
				 union enetc_tx_bd *txbd, int *i, int hdr_len,
				 int data_len)
{
	union enetc_tx_bd txbd_tmp;
	u8 flags = 0, e_flags = 0;
	dma_addr_t addr;

	enetc_clear_tx_bd(&txbd_tmp);
	addr = tx_ring->tso_headers_dma + *i * TSO_HEADER_SIZE;

	if (skb_vlan_tag_present(skb))
		flags |= ENETC_TXBD_FLAGS_EX;

	txbd_tmp.addr = cpu_to_le64(addr);
	txbd_tmp.buf_len = cpu_to_le16(hdr_len);

	/* first BD needs frm_len and offload flags set */
	txbd_tmp.frm_len = cpu_to_le16(hdr_len + data_len);
	txbd_tmp.flags = flags;

	/* For the TSO header we do not set the dma address since we do not
	 * want it unmapped when we do cleanup. We still set len so that we
	 * count the bytes sent.
	 */
	tx_swbd->len = hdr_len;
	tx_swbd->do_twostep_tstamp = false;
	tx_swbd->check_wb = false;

	/* Actually write the header in the BD */
	*txbd = txbd_tmp;

	/* Add extension BD for VLAN */
	if (flags & ENETC_TXBD_FLAGS_EX) {
		/* Get the next BD */
		enetc_bdr_idx_inc(tx_ring, i);
		txbd = ENETC_TXBD(*tx_ring, *i);
		tx_swbd = &tx_ring->tx_swbd[*i];
		prefetchw(txbd);

		/* Setup the VLAN fields */
		enetc_clear_tx_bd(&txbd_tmp);
		txbd_tmp.ext.vid = cpu_to_le16(skb_vlan_tag_get(skb));
		txbd_tmp.ext.tpid = 0; /* < C-TAG */
		e_flags |= ENETC_TXBD_E_FLAGS_VLAN_INS;

		/* Write the BD */
		txbd_tmp.ext.e_flags = e_flags;
		*txbd = txbd_tmp;
	}
}

static int enetc_map_tx_tso_data(struct enetc_bdr *tx_ring, struct sk_buff *skb,
				 struct enetc_tx_swbd *tx_swbd,
				 union enetc_tx_bd *txbd, char *data,
				 int size, bool last_bd)
{
	union enetc_tx_bd txbd_tmp;
	dma_addr_t addr;
	u8 flags = 0;

	enetc_clear_tx_bd(&txbd_tmp);

	addr = dma_map_single(tx_ring->dev, data, size, DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(tx_ring->dev, addr))) {
		netdev_err(tx_ring->ndev, "DMA map error\n");
		return -ENOMEM;
	}

	if (last_bd) {
		flags |= ENETC_TXBD_FLAGS_F;
		tx_swbd->is_eof = 1;
	}

	txbd_tmp.addr = cpu_to_le64(addr);
	txbd_tmp.buf_len = cpu_to_le16(size);
	txbd_tmp.flags = flags;

	tx_swbd->dma = addr;
	tx_swbd->len = size;
	tx_swbd->dir = DMA_TO_DEVICE;

	*txbd = txbd_tmp;

	return 0;
}

static __wsum enetc_tso_hdr_csum(struct tso_t *tso, struct sk_buff *skb,
				 char *hdr, int hdr_len, int *l4_hdr_len)
{
	char *l4_hdr = hdr + skb_transport_offset(skb);
	int mac_hdr_len = skb_network_offset(skb);

	if (tso->tlen != sizeof(struct udphdr)) {
		struct tcphdr *tcph = (struct tcphdr *)(l4_hdr);

		tcph->check = 0;
	} else {
		struct udphdr *udph = (struct udphdr *)(l4_hdr);

		udph->check = 0;
	}

	/* Compute the IP checksum. This is necessary since tso_build_hdr()
	 * already incremented the IP ID field.
	 */
	if (!tso->ipv6) {
		struct iphdr *iph = (void *)(hdr + mac_hdr_len);

		iph->check = 0;
		iph->check = ip_fast_csum((unsigned char *)iph, iph->ihl);
	}

	/* Compute the checksum over the L4 header. */
	*l4_hdr_len = hdr_len - skb_transport_offset(skb);
	return csum_partial(l4_hdr, *l4_hdr_len, 0);
}

static void enetc_tso_complete_csum(struct enetc_bdr *tx_ring, struct tso_t *tso,
				    struct sk_buff *skb, char *hdr, int len,
				    __wsum sum)
{
	char *l4_hdr = hdr + skb_transport_offset(skb);
	__sum16 csum_final;

	/* Complete the L4 checksum by appending the pseudo-header to the
	 * already computed checksum.
	 */
	if (!tso->ipv6)
		csum_final = csum_tcpudp_magic(ip_hdr(skb)->saddr,
					       ip_hdr(skb)->daddr,
					       len, ip_hdr(skb)->protocol, sum);
	else
		csum_final = csum_ipv6_magic(&ipv6_hdr(skb)->saddr,
					     &ipv6_hdr(skb)->daddr,
					     len, ipv6_hdr(skb)->nexthdr, sum);

	if (tso->tlen != sizeof(struct udphdr)) {
		struct tcphdr *tcph = (struct tcphdr *)(l4_hdr);

		tcph->check = csum_final;
	} else {
		struct udphdr *udph = (struct udphdr *)(l4_hdr);

		udph->check = csum_final;
	}
}

static int enetc_map_tx_tso_buffs(struct enetc_bdr *tx_ring, struct sk_buff *skb)
{
	int hdr_len, total_len, data_len;
	struct enetc_tx_swbd *tx_swbd;
	union enetc_tx_bd *txbd;
	struct tso_t tso;
	__wsum csum, csum2;
	int count = 0, pos;
	int err, i, bd_data_num;

	/* Initialize the TSO handler, and prepare the first payload */
	hdr_len = tso_start(skb, &tso);
	total_len = skb->len - hdr_len;
	i = tx_ring->next_to_use;

	while (total_len > 0) {
		char *hdr;

		/* Get the BD */
		txbd = ENETC_TXBD(*tx_ring, i);
		tx_swbd = &tx_ring->tx_swbd[i];
		prefetchw(txbd);

		/* Determine the length of this packet */
		data_len = min_t(int, skb_shinfo(skb)->gso_size, total_len);
		total_len -= data_len;

		/* prepare packet headers: MAC + IP + TCP */
		hdr = tx_ring->tso_headers + i * TSO_HEADER_SIZE;
		tso_build_hdr(skb, hdr, &tso, data_len, total_len == 0);

		/* compute the csum over the L4 header */
		csum = enetc_tso_hdr_csum(&tso, skb, hdr, hdr_len, &pos);
		enetc_map_tx_tso_hdr(tx_ring, skb, tx_swbd, txbd, &i, hdr_len, data_len);
		bd_data_num = 0;
		count++;

		while (data_len > 0) {
			int size;

			size = min_t(int, tso.size, data_len);

			/* Advance the index in the BDR */
			enetc_bdr_idx_inc(tx_ring, &i);
			txbd = ENETC_TXBD(*tx_ring, i);
			tx_swbd = &tx_ring->tx_swbd[i];
			prefetchw(txbd);

			/* Compute the checksum over this segment of data and
			 * add it to the csum already computed (over the L4
			 * header and possible other data segments).
			 */
			csum2 = csum_partial(tso.data, size, 0);
			csum = csum_block_add(csum, csum2, pos);
			pos += size;

			err = enetc_map_tx_tso_data(tx_ring, skb, tx_swbd, txbd,
						    tso.data, size,
						    size == data_len);
			if (err)
				goto err_map_data;

			data_len -= size;
			count++;
			bd_data_num++;
			tso_build_data(skb, &tso, size);

			if (unlikely(bd_data_num >= ENETC_MAX_SKB_FRAGS && data_len))
				goto err_chained_bd;
		}

		enetc_tso_complete_csum(tx_ring, &tso, skb, hdr, pos, csum);

		if (total_len == 0)
			tx_swbd->skb = skb;

		/* Go to the next BD */
		enetc_bdr_idx_inc(tx_ring, &i);
	}

	tx_ring->next_to_use = i;
	enetc_update_tx_ring_tail(tx_ring);

	return count;

err_map_data:
	dev_err(tx_ring->dev, "DMA map error");

err_chained_bd:
	do {
		tx_swbd = &tx_ring->tx_swbd[i];
		enetc_free_tx_frame(tx_ring, tx_swbd);
		if (i == 0)
			i = tx_ring->bd_count;
		i--;
	} while (count--);

	return 0;
}

static netdev_tx_t enetc_start_xmit(struct sk_buff *skb,
				    struct net_device *ndev)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	struct enetc_bdr *tx_ring;
	int count, err;

	/* Queue one-step Sync packet if already locked */
	if (skb->cb[0] & ENETC_F_TX_ONESTEP_SYNC_TSTAMP) {
		if (test_and_set_bit_lock(ENETC_TX_ONESTEP_TSTAMP_IN_PROGRESS,
					  &priv->flags)) {
			skb_queue_tail(&priv->tx_skbs, skb);
			return NETDEV_TX_OK;
		}
	}

	tx_ring = priv->tx_ring[skb->queue_mapping];

	if (skb_is_gso(skb)) {
		if (enetc_bd_unused(tx_ring) < tso_count_descs(skb)) {
			netif_stop_subqueue(ndev, tx_ring->index);
			return NETDEV_TX_BUSY;
		}

		enetc_lock_mdio();
		count = enetc_map_tx_tso_buffs(tx_ring, skb);
		enetc_unlock_mdio();
	} else {
		if (unlikely(skb_shinfo(skb)->nr_frags > ENETC_MAX_SKB_FRAGS))
			if (unlikely(skb_linearize(skb)))
				goto drop_packet_err;

		count = skb_shinfo(skb)->nr_frags + 1; /* fragments + head */
		if (enetc_bd_unused(tx_ring) < ENETC_TXBDS_NEEDED(count)) {
			netif_stop_subqueue(ndev, tx_ring->index);
			return NETDEV_TX_BUSY;
		}

		if (skb->ip_summed == CHECKSUM_PARTIAL) {
			err = skb_checksum_help(skb);
			if (err)
				goto drop_packet_err;
		}
		enetc_lock_mdio();
		count = enetc_map_tx_buffs(tx_ring, skb);
		enetc_unlock_mdio();
	}

	if (unlikely(!count))
		goto drop_packet_err;

	if (enetc_bd_unused(tx_ring) < ENETC_TXBDS_MAX_NEEDED)
		netif_stop_subqueue(ndev, tx_ring->index);

	return NETDEV_TX_OK;

drop_packet_err:
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

netdev_tx_t enetc_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	u8 udp, msgtype, twostep;
	u16 offset1, offset2;

	/* Mark tx timestamp type on skb->cb[0] if requires */
	if ((skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP) &&
	    (priv->active_offloads & ENETC_F_TX_TSTAMP_MASK)) {
		skb->cb[0] = priv->active_offloads & ENETC_F_TX_TSTAMP_MASK;
	} else {
		skb->cb[0] = 0;
	}

	/* Fall back to two-step timestamp if not one-step Sync packet */
	if (skb->cb[0] & ENETC_F_TX_ONESTEP_SYNC_TSTAMP) {
		if (enetc_ptp_parse(skb, &udp, &msgtype, &twostep,
				    &offset1, &offset2) ||
		    msgtype != PTP_MSGTYPE_SYNC || twostep != 0)
			skb->cb[0] = ENETC_F_TX_TSTAMP;
	}

	return enetc_start_xmit(skb, ndev);
}
EXPORT_SYMBOL_GPL(enetc_xmit);

static irqreturn_t enetc_msix(int irq, void *data)
{
	struct enetc_int_vector	*v = data;
	int i;

	enetc_lock_mdio();

	/* disable interrupts */
	enetc_wr_reg_hot(v->rbier, 0);
	enetc_wr_reg_hot(v->ricr1, v->rx_ictt);

	for_each_set_bit(i, &v->tx_rings_map, ENETC_MAX_NUM_TXQS)
		enetc_wr_reg_hot(v->tbier_base + ENETC_BDR_OFF(i), 0);

	enetc_unlock_mdio();

	napi_schedule(&v->napi);

	return IRQ_HANDLED;
}

static void enetc_rx_dim_work(struct work_struct *w)
{
	struct dim *dim = container_of(w, struct dim, work);
	struct dim_cq_moder moder =
		net_dim_get_rx_moderation(dim->mode, dim->profile_ix);
	struct enetc_int_vector	*v =
		container_of(dim, struct enetc_int_vector, rx_dim);

	v->rx_ictt = enetc_usecs_to_cycles(moder.usec);
	dim->state = DIM_START_MEASURE;
}

static void enetc_rx_net_dim(struct enetc_int_vector *v)
{
	struct dim_sample dim_sample = {};

	v->comp_cnt++;

	if (!v->rx_napi_work)
		return;

	dim_update_sample(v->comp_cnt,
			  v->rx_ring.stats.packets,
			  v->rx_ring.stats.bytes,
			  &dim_sample);
	net_dim(&v->rx_dim, dim_sample);
}

static int enetc_bd_ready_count(struct enetc_bdr *tx_ring, int ci)
{
	int pi = enetc_rd_reg_hot(tx_ring->tcir) & ENETC_TBCIR_IDX_MASK;

	return pi >= ci ? pi - ci : tx_ring->bd_count - ci + pi;
}

static bool enetc_page_reusable(struct page *page)
{
	return (!page_is_pfmemalloc(page) && page_ref_count(page) == 1);
}

static void enetc_reuse_page(struct enetc_bdr *rx_ring,
			     struct enetc_rx_swbd *old)
{
	struct enetc_rx_swbd *new;

	new = &rx_ring->rx_swbd[rx_ring->next_to_alloc];

	/* next buf that may reuse a page */
	enetc_bdr_idx_inc(rx_ring, &rx_ring->next_to_alloc);

	/* copy page reference */
	*new = *old;
}

static void enetc_get_tx_tstamp(struct enetc_hw *hw, union enetc_tx_bd *txbd,
				u64 *tstamp)
{
	u32 lo, hi, tstamp_lo;

	lo = enetc_rd_hot(hw, ENETC_SICTR0);
	hi = enetc_rd_hot(hw, ENETC_SICTR1);
	tstamp_lo = le32_to_cpu(txbd->wb.tstamp);
	if (lo <= tstamp_lo)
		hi -= 1;
	*tstamp = (u64)hi << 32 | tstamp_lo;
}

static void enetc_tstamp_tx(struct sk_buff *skb, u64 tstamp)
{
	struct skb_shared_hwtstamps shhwtstamps;

	if (skb_shinfo(skb)->tx_flags & SKBTX_IN_PROGRESS) {
		memset(&shhwtstamps, 0, sizeof(shhwtstamps));
		shhwtstamps.hwtstamp = ns_to_ktime(tstamp);
		skb_txtime_consumed(skb);
		skb_tstamp_tx(skb, &shhwtstamps);
	}
}

static void enetc_recycle_xdp_tx_buff(struct enetc_bdr *tx_ring,
				      struct enetc_tx_swbd *tx_swbd)
{
	struct enetc_ndev_priv *priv = netdev_priv(tx_ring->ndev);
	struct enetc_rx_swbd rx_swbd = {
		.dma = tx_swbd->dma,
		.page = tx_swbd->page,
		.page_offset = tx_swbd->page_offset,
		.dir = tx_swbd->dir,
		.len = tx_swbd->len,
	};
	struct enetc_bdr *rx_ring;

	rx_ring = enetc_rx_ring_from_xdp_tx_ring(priv, tx_ring);

	if (likely(enetc_swbd_unused(rx_ring))) {
		enetc_reuse_page(rx_ring, &rx_swbd);

		/* sync for use by the device */
		dma_sync_single_range_for_device(rx_ring->dev, rx_swbd.dma,
						 rx_swbd.page_offset,
						 ENETC_RXB_DMA_SIZE_XDP,
						 rx_swbd.dir);

		rx_ring->stats.recycles++;
	} else {
		/* RX ring is already full, we need to unmap and free the
		 * page, since there's nothing useful we can do with it.
		 */
		rx_ring->stats.recycle_failures++;

		dma_unmap_page(rx_ring->dev, rx_swbd.dma, PAGE_SIZE,
			       rx_swbd.dir);
		__free_page(rx_swbd.page);
	}

	rx_ring->xdp.xdp_tx_in_flight--;
}

static bool enetc_clean_tx_ring(struct enetc_bdr *tx_ring, int napi_budget)
{
	int tx_frm_cnt = 0, tx_byte_cnt = 0, tx_win_drop = 0;
	struct net_device *ndev = tx_ring->ndev;
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	struct enetc_tx_swbd *tx_swbd;
	int i, bds_to_clean;
	bool do_twostep_tstamp;
	u64 tstamp = 0;

	i = tx_ring->next_to_clean;
	tx_swbd = &tx_ring->tx_swbd[i];

	bds_to_clean = enetc_bd_ready_count(tx_ring, i);

	do_twostep_tstamp = false;

	while (bds_to_clean && tx_frm_cnt < ENETC_DEFAULT_TX_WORK) {
		struct xdp_frame *xdp_frame = enetc_tx_swbd_get_xdp_frame(tx_swbd);
		struct sk_buff *skb = enetc_tx_swbd_get_skb(tx_swbd);
		bool is_eof = tx_swbd->is_eof;

		if (unlikely(tx_swbd->check_wb)) {
			union enetc_tx_bd *txbd = ENETC_TXBD(*tx_ring, i);

			if (txbd->flags & ENETC_TXBD_FLAGS_W &&
			    tx_swbd->do_twostep_tstamp) {
				enetc_get_tx_tstamp(&priv->si->hw, txbd,
						    &tstamp);
				do_twostep_tstamp = true;
			}

			if (tx_swbd->qbv_en &&
			    txbd->wb.status & ENETC_TXBD_STATS_WIN)
				tx_win_drop++;
		}

		if (tx_swbd->is_xdp_tx)
			enetc_recycle_xdp_tx_buff(tx_ring, tx_swbd);
		else if (likely(tx_swbd->dma))
			enetc_unmap_tx_buff(tx_ring, tx_swbd);

		if (xdp_frame) {
			xdp_return_frame(xdp_frame);
		} else if (skb) {
			if (unlikely(skb->cb[0] & ENETC_F_TX_ONESTEP_SYNC_TSTAMP)) {
				/* Start work to release lock for next one-step
				 * timestamping packet. And send one skb in
				 * tx_skbs queue if has.
				 */
				schedule_work(&priv->tx_onestep_tstamp);
			} else if (unlikely(do_twostep_tstamp)) {
				enetc_tstamp_tx(skb, tstamp);
				do_twostep_tstamp = false;
			}
			napi_consume_skb(skb, napi_budget);
		}

		tx_byte_cnt += tx_swbd->len;
		/* Scrub the swbd here so we don't have to do that
		 * when we reuse it during xmit
		 */
		memset(tx_swbd, 0, sizeof(*tx_swbd));

		bds_to_clean--;
		tx_swbd++;
		i++;
		if (unlikely(i == tx_ring->bd_count)) {
			i = 0;
			tx_swbd = tx_ring->tx_swbd;
		}

		/* BD iteration loop end */
		if (is_eof) {
			tx_frm_cnt++;
			/* re-arm interrupt source */
			enetc_wr_reg_hot(tx_ring->idr, BIT(tx_ring->index) |
					 BIT(16 + tx_ring->index));
		}

		if (unlikely(!bds_to_clean))
			bds_to_clean = enetc_bd_ready_count(tx_ring, i);
	}

	tx_ring->next_to_clean = i;
	tx_ring->stats.packets += tx_frm_cnt;
	tx_ring->stats.bytes += tx_byte_cnt;
	tx_ring->stats.win_drop += tx_win_drop;

	if (unlikely(tx_frm_cnt && netif_carrier_ok(ndev) &&
		     __netif_subqueue_stopped(ndev, tx_ring->index) &&
		     (enetc_bd_unused(tx_ring) >= ENETC_TXBDS_MAX_NEEDED))) {
		netif_wake_subqueue(ndev, tx_ring->index);
	}

	return tx_frm_cnt != ENETC_DEFAULT_TX_WORK;
}

static bool enetc_new_page(struct enetc_bdr *rx_ring,
			   struct enetc_rx_swbd *rx_swbd)
{
	bool xdp = !!(rx_ring->xdp.prog);
	struct page *page;
	dma_addr_t addr;

	page = dev_alloc_page();
	if (unlikely(!page))
		return false;

	/* For XDP_TX, we forgo dma_unmap -> dma_map */
	rx_swbd->dir = xdp ? DMA_BIDIRECTIONAL : DMA_FROM_DEVICE;

	addr = dma_map_page(rx_ring->dev, page, 0, PAGE_SIZE, rx_swbd->dir);
	if (unlikely(dma_mapping_error(rx_ring->dev, addr))) {
		__free_page(page);

		return false;
	}

	rx_swbd->dma = addr;
	rx_swbd->page = page;
	rx_swbd->page_offset = rx_ring->buffer_offset;

	return true;
}

static int enetc_refill_rx_ring(struct enetc_bdr *rx_ring, const int buff_cnt)
{
	struct enetc_rx_swbd *rx_swbd;
	union enetc_rx_bd *rxbd;
	int i, j;

	i = rx_ring->next_to_use;
	rx_swbd = &rx_ring->rx_swbd[i];
	rxbd = enetc_rxbd(rx_ring, i);

	for (j = 0; j < buff_cnt; j++) {
		/* try reuse page */
		if (unlikely(!rx_swbd->page)) {
			if (unlikely(!enetc_new_page(rx_ring, rx_swbd))) {
				rx_ring->stats.rx_alloc_errs++;
				break;
			}
		}

		/* update RxBD */
		rxbd->w.addr = cpu_to_le64(rx_swbd->dma +
					   rx_swbd->page_offset);
		/* clear 'R" as well */
		rxbd->r.lstatus = 0;

		enetc_rxbd_next(rx_ring, &rxbd, &i);
		rx_swbd = &rx_ring->rx_swbd[i];
	}

	if (likely(j)) {
		rx_ring->next_to_alloc = i; /* keep track from page reuse */
		rx_ring->next_to_use = i;

		/* update ENETC's consumer index */
		enetc_wr_reg_hot(rx_ring->rcir, rx_ring->next_to_use);
	}

	return j;
}

#ifdef CONFIG_FSL_ENETC_PTP_CLOCK
static void enetc_get_rx_tstamp(struct net_device *ndev,
				union enetc_rx_bd *rxbd,
				struct sk_buff *skb)
{
	struct skb_shared_hwtstamps *shhwtstamps = skb_hwtstamps(skb);
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	struct enetc_hw *hw = &priv->si->hw;
	u32 lo, hi, tstamp_lo;
	u64 tstamp;

	if (le16_to_cpu(rxbd->r.flags) & ENETC_RXBD_FLAG_TSTMP) {
		lo = enetc_rd_reg_hot(hw->reg + ENETC_SICTR0);
		hi = enetc_rd_reg_hot(hw->reg + ENETC_SICTR1);
		rxbd = enetc_rxbd_ext(rxbd);
		tstamp_lo = le32_to_cpu(rxbd->ext.tstamp);
		if (lo <= tstamp_lo)
			hi -= 1;

		tstamp = (u64)hi << 32 | tstamp_lo;
		memset(shhwtstamps, 0, sizeof(*shhwtstamps));
		shhwtstamps->hwtstamp = ns_to_ktime(tstamp);
	}
}
#endif

static void enetc_get_offloads(struct enetc_bdr *rx_ring,
			       union enetc_rx_bd *rxbd, struct sk_buff *skb)
{
	struct enetc_ndev_priv *priv = netdev_priv(rx_ring->ndev);

	/* TODO: hashing */
	if (rx_ring->ndev->features & NETIF_F_RXCSUM) {
		u16 inet_csum = le16_to_cpu(rxbd->r.inet_csum);

		skb->csum = csum_unfold((__force __sum16)~htons(inet_csum));
		skb->ip_summed = CHECKSUM_COMPLETE;
	}

	if (le16_to_cpu(rxbd->r.flags) & ENETC_RXBD_FLAG_VLAN) {
		__be16 tpid = 0;

		switch (le16_to_cpu(rxbd->r.flags) & ENETC_RXBD_FLAG_TPID) {
		case 0:
			tpid = htons(ETH_P_8021Q);
			break;
		case 1:
			tpid = htons(ETH_P_8021AD);
			break;
		case 2:
			tpid = htons(enetc_port_rd(&priv->si->hw,
						   ENETC_PCVLANR1));
			break;
		case 3:
			tpid = htons(enetc_port_rd(&priv->si->hw,
						   ENETC_PCVLANR2));
			break;
		default:
			break;
		}

		__vlan_hwaccel_put_tag(skb, tpid, le16_to_cpu(rxbd->r.vlan_opt));
	}

#ifdef CONFIG_FSL_ENETC_PTP_CLOCK
	if (priv->active_offloads & ENETC_F_RX_TSTAMP)
		enetc_get_rx_tstamp(rx_ring->ndev, rxbd, skb);
#endif
}

/* This gets called during the non-XDP NAPI poll cycle as well as on XDP_PASS,
 * so it needs to work with both DMA_FROM_DEVICE as well as DMA_BIDIRECTIONAL
 * mapped buffers.
 */
static struct enetc_rx_swbd *enetc_get_rx_buff(struct enetc_bdr *rx_ring,
					       int i, u16 size)
{
	struct enetc_rx_swbd *rx_swbd = &rx_ring->rx_swbd[i];

	dma_sync_single_range_for_cpu(rx_ring->dev, rx_swbd->dma,
				      rx_swbd->page_offset,
				      size, rx_swbd->dir);
	return rx_swbd;
}

/* Reuse the current page without performing half-page buffer flipping */
static void enetc_put_rx_buff(struct enetc_bdr *rx_ring,
			      struct enetc_rx_swbd *rx_swbd)
{
	size_t buffer_size = ENETC_RXB_TRUESIZE - rx_ring->buffer_offset;

	enetc_reuse_page(rx_ring, rx_swbd);

	dma_sync_single_range_for_device(rx_ring->dev, rx_swbd->dma,
					 rx_swbd->page_offset,
					 buffer_size, rx_swbd->dir);

	rx_swbd->page = NULL;
}

/* Reuse the current page by performing half-page buffer flipping */
static void enetc_flip_rx_buff(struct enetc_bdr *rx_ring,
			       struct enetc_rx_swbd *rx_swbd)
{
	if (likely(enetc_page_reusable(rx_swbd->page))) {
		rx_swbd->page_offset ^= ENETC_RXB_TRUESIZE;
		page_ref_inc(rx_swbd->page);

		enetc_put_rx_buff(rx_ring, rx_swbd);
	} else {
		dma_unmap_page(rx_ring->dev, rx_swbd->dma, PAGE_SIZE,
			       rx_swbd->dir);
		rx_swbd->page = NULL;
	}
}

static struct sk_buff *enetc_map_rx_buff_to_skb(struct enetc_bdr *rx_ring,
						int i, u16 size)
{
	struct enetc_rx_swbd *rx_swbd = enetc_get_rx_buff(rx_ring, i, size);
	struct sk_buff *skb;
	void *ba;

	ba = page_address(rx_swbd->page) + rx_swbd->page_offset;
	skb = build_skb(ba - rx_ring->buffer_offset, ENETC_RXB_TRUESIZE);
	if (unlikely(!skb)) {
		rx_ring->stats.rx_alloc_errs++;
		return NULL;
	}

	skb_reserve(skb, rx_ring->buffer_offset);
	__skb_put(skb, size);

	enetc_flip_rx_buff(rx_ring, rx_swbd);

	return skb;
}

static void enetc_add_rx_buff_to_skb(struct enetc_bdr *rx_ring, int i,
				     u16 size, struct sk_buff *skb)
{
	struct enetc_rx_swbd *rx_swbd = enetc_get_rx_buff(rx_ring, i, size);

	skb_add_rx_frag(skb, skb_shinfo(skb)->nr_frags, rx_swbd->page,
			rx_swbd->page_offset, size, ENETC_RXB_TRUESIZE);

	enetc_flip_rx_buff(rx_ring, rx_swbd);
}

static bool enetc_check_bd_errors_and_consume(struct enetc_bdr *rx_ring,
					      u32 bd_status,
					      union enetc_rx_bd **rxbd, int *i)
{
	if (likely(!(bd_status & ENETC_RXBD_LSTATUS(ENETC_RXBD_ERR_MASK))))
		return false;

	enetc_put_rx_buff(rx_ring, &rx_ring->rx_swbd[*i]);
	enetc_rxbd_next(rx_ring, rxbd, i);

	while (!(bd_status & ENETC_RXBD_LSTATUS_F)) {
		dma_rmb();
		bd_status = le32_to_cpu((*rxbd)->r.lstatus);

		enetc_put_rx_buff(rx_ring, &rx_ring->rx_swbd[*i]);
		enetc_rxbd_next(rx_ring, rxbd, i);
	}

	rx_ring->ndev->stats.rx_dropped++;
	rx_ring->ndev->stats.rx_errors++;

	return true;
}

static struct sk_buff *enetc_build_skb(struct enetc_bdr *rx_ring,
				       u32 bd_status, union enetc_rx_bd **rxbd,
				       int *i, int *cleaned_cnt, int buffer_size)
{
	struct sk_buff *skb;
	u16 size;

	size = le16_to_cpu((*rxbd)->r.buf_len);
	skb = enetc_map_rx_buff_to_skb(rx_ring, *i, size);
	if (!skb)
		return NULL;

	enetc_get_offloads(rx_ring, *rxbd, skb);

	(*cleaned_cnt)++;

	enetc_rxbd_next(rx_ring, rxbd, i);

	/* not last BD in frame? */
	while (!(bd_status & ENETC_RXBD_LSTATUS_F)) {
		bd_status = le32_to_cpu((*rxbd)->r.lstatus);
		size = buffer_size;

		if (bd_status & ENETC_RXBD_LSTATUS_F) {
			dma_rmb();
			size = le16_to_cpu((*rxbd)->r.buf_len);
		}

		enetc_add_rx_buff_to_skb(rx_ring, *i, size, skb);

		(*cleaned_cnt)++;

		enetc_rxbd_next(rx_ring, rxbd, i);
	}

	skb_record_rx_queue(skb, rx_ring->index);
	skb->protocol = eth_type_trans(skb, rx_ring->ndev);

	return skb;
}

#define ENETC_RXBD_BUNDLE 16 /* # of BDs to update at once */

static int enetc_clean_rx_ring(struct enetc_bdr *rx_ring,
			       struct napi_struct *napi, int work_limit)
{
	int rx_frm_cnt = 0, rx_byte_cnt = 0;
	int cleaned_cnt, i;

	cleaned_cnt = enetc_bd_unused(rx_ring);
	/* next descriptor to process */
	i = rx_ring->next_to_clean;

	while (likely(rx_frm_cnt < work_limit)) {
		union enetc_rx_bd *rxbd;
		struct sk_buff *skb;
		u32 bd_status;

		if (cleaned_cnt >= ENETC_RXBD_BUNDLE)
			cleaned_cnt -= enetc_refill_rx_ring(rx_ring,
							    cleaned_cnt);

		rxbd = enetc_rxbd(rx_ring, i);
		bd_status = le32_to_cpu(rxbd->r.lstatus);
		if (!bd_status)
			break;

		enetc_wr_reg_hot(rx_ring->idr, BIT(rx_ring->index));
		dma_rmb(); /* for reading other rxbd fields */

		if (enetc_check_bd_errors_and_consume(rx_ring, bd_status,
						      &rxbd, &i))
			break;

		skb = enetc_build_skb(rx_ring, bd_status, &rxbd, &i,
				      &cleaned_cnt, ENETC_RXB_DMA_SIZE);
		if (!skb)
			break;

		/* When set, the outer VLAN header is extracted and reported
		 * in the receive buffer descriptor. So rx_byte_cnt should
		 * add the length of the extracted VLAN header.
		 */
		if (bd_status & ENETC_RXBD_FLAG_VLAN)
			rx_byte_cnt += VLAN_HLEN;
		rx_byte_cnt += skb->len + ETH_HLEN;
		rx_frm_cnt++;

		napi_gro_receive(napi, skb);
	}

	rx_ring->next_to_clean = i;

	rx_ring->stats.packets += rx_frm_cnt;
	rx_ring->stats.bytes += rx_byte_cnt;

	return rx_frm_cnt;
}

static void enetc_xdp_map_tx_buff(struct enetc_bdr *tx_ring, int i,
				  struct enetc_tx_swbd *tx_swbd,
				  int frm_len)
{
	union enetc_tx_bd *txbd = ENETC_TXBD(*tx_ring, i);

	prefetchw(txbd);

	enetc_clear_tx_bd(txbd);
	txbd->addr = cpu_to_le64(tx_swbd->dma + tx_swbd->page_offset);
	txbd->buf_len = cpu_to_le16(tx_swbd->len);
	txbd->frm_len = cpu_to_le16(frm_len);

	memcpy(&tx_ring->tx_swbd[i], tx_swbd, sizeof(*tx_swbd));
}

/* Puts in the TX ring one XDP frame, mapped as an array of TX software buffer
 * descriptors.
 */
static bool enetc_xdp_tx(struct enetc_bdr *tx_ring,
			 struct enetc_tx_swbd *xdp_tx_arr, int num_tx_swbd)
{
	struct enetc_tx_swbd *tmp_tx_swbd = xdp_tx_arr;
	int i, k, frm_len = tmp_tx_swbd->len;

	if (unlikely(enetc_bd_unused(tx_ring) < ENETC_TXBDS_NEEDED(num_tx_swbd)))
		return false;

	while (unlikely(!tmp_tx_swbd->is_eof)) {
		tmp_tx_swbd++;
		frm_len += tmp_tx_swbd->len;
	}

	i = tx_ring->next_to_use;

	for (k = 0; k < num_tx_swbd; k++) {
		struct enetc_tx_swbd *xdp_tx_swbd = &xdp_tx_arr[k];

		enetc_xdp_map_tx_buff(tx_ring, i, xdp_tx_swbd, frm_len);

		/* last BD needs 'F' bit set */
		if (xdp_tx_swbd->is_eof) {
			union enetc_tx_bd *txbd = ENETC_TXBD(*tx_ring, i);

			txbd->flags = ENETC_TXBD_FLAGS_F;
		}

		enetc_bdr_idx_inc(tx_ring, &i);
	}

	tx_ring->next_to_use = i;

	return true;
}

static int enetc_xdp_frame_to_xdp_tx_swbd(struct enetc_bdr *tx_ring,
					  struct enetc_tx_swbd *xdp_tx_arr,
					  struct xdp_frame *xdp_frame)
{
	struct enetc_tx_swbd *xdp_tx_swbd = &xdp_tx_arr[0];
	struct skb_shared_info *shinfo;
	void *data = xdp_frame->data;
	int len = xdp_frame->len;
	skb_frag_t *frag;
	dma_addr_t dma;
	unsigned int f;
	int n = 0;

	dma = dma_map_single(tx_ring->dev, data, len, DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(tx_ring->dev, dma))) {
		netdev_err(tx_ring->ndev, "DMA map error\n");
		return -1;
	}

	xdp_tx_swbd->dma = dma;
	xdp_tx_swbd->dir = DMA_TO_DEVICE;
	xdp_tx_swbd->len = len;
	xdp_tx_swbd->is_xdp_redirect = true;
	xdp_tx_swbd->is_eof = false;
	xdp_tx_swbd->xdp_frame = NULL;

	n++;

	if (!xdp_frame_has_frags(xdp_frame))
		goto out;

	xdp_tx_swbd = &xdp_tx_arr[n];

	shinfo = xdp_get_shared_info_from_frame(xdp_frame);

	for (f = 0, frag = &shinfo->frags[0]; f < shinfo->nr_frags;
	     f++, frag++) {
		data = skb_frag_address(frag);
		len = skb_frag_size(frag);

		dma = dma_map_single(tx_ring->dev, data, len, DMA_TO_DEVICE);
		if (unlikely(dma_mapping_error(tx_ring->dev, dma))) {
			/* Undo the DMA mapping for all fragments */
			while (--n >= 0)
				enetc_unmap_tx_buff(tx_ring, &xdp_tx_arr[n]);

			netdev_err(tx_ring->ndev, "DMA map error\n");
			return -1;
		}

		xdp_tx_swbd->dma = dma;
		xdp_tx_swbd->dir = DMA_TO_DEVICE;
		xdp_tx_swbd->len = len;
		xdp_tx_swbd->is_xdp_redirect = true;
		xdp_tx_swbd->is_eof = false;
		xdp_tx_swbd->xdp_frame = NULL;

		n++;
		xdp_tx_swbd = &xdp_tx_arr[n];
	}
out:
	xdp_tx_arr[n - 1].is_eof = true;
	xdp_tx_arr[n - 1].xdp_frame = xdp_frame;

	return n;
}

int enetc_xdp_xmit(struct net_device *ndev, int num_frames,
		   struct xdp_frame **frames, u32 flags)
{
	struct enetc_tx_swbd xdp_redirect_arr[ENETC_MAX_SKB_FRAGS] = {0};
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	struct enetc_bdr *tx_ring;
	int xdp_tx_bd_cnt, i, k;
	int xdp_tx_frm_cnt = 0;

	enetc_lock_mdio();

	tx_ring = priv->xdp_tx_ring[smp_processor_id()];

	prefetchw(ENETC_TXBD(*tx_ring, tx_ring->next_to_use));

	for (k = 0; k < num_frames; k++) {
		xdp_tx_bd_cnt = enetc_xdp_frame_to_xdp_tx_swbd(tx_ring,
							       xdp_redirect_arr,
							       frames[k]);
		if (unlikely(xdp_tx_bd_cnt < 0))
			break;

		if (unlikely(!enetc_xdp_tx(tx_ring, xdp_redirect_arr,
					   xdp_tx_bd_cnt))) {
			for (i = 0; i < xdp_tx_bd_cnt; i++)
				enetc_unmap_tx_buff(tx_ring,
						    &xdp_redirect_arr[i]);
			tx_ring->stats.xdp_tx_drops++;
			break;
		}

		xdp_tx_frm_cnt++;
	}

	if (unlikely((flags & XDP_XMIT_FLUSH) || k != xdp_tx_frm_cnt))
		enetc_update_tx_ring_tail(tx_ring);

	tx_ring->stats.xdp_tx += xdp_tx_frm_cnt;

	enetc_unlock_mdio();

	return xdp_tx_frm_cnt;
}
EXPORT_SYMBOL_GPL(enetc_xdp_xmit);

static void enetc_map_rx_buff_to_xdp(struct enetc_bdr *rx_ring, int i,
				     struct xdp_buff *xdp_buff, u16 size)
{
	struct enetc_rx_swbd *rx_swbd = enetc_get_rx_buff(rx_ring, i, size);
	void *hard_start = page_address(rx_swbd->page) + rx_swbd->page_offset;

	/* To be used for XDP_TX */
	rx_swbd->len = size;

	xdp_prepare_buff(xdp_buff, hard_start - rx_ring->buffer_offset,
			 rx_ring->buffer_offset, size, false);
}

static void enetc_add_rx_buff_to_xdp(struct enetc_bdr *rx_ring, int i,
				     u16 size, struct xdp_buff *xdp_buff)
{
	struct skb_shared_info *shinfo = xdp_get_shared_info_from_buff(xdp_buff);
	struct enetc_rx_swbd *rx_swbd = enetc_get_rx_buff(rx_ring, i, size);
	skb_frag_t *frag;

	/* To be used for XDP_TX */
	rx_swbd->len = size;

	if (!xdp_buff_has_frags(xdp_buff)) {
		xdp_buff_set_frags_flag(xdp_buff);
		shinfo->xdp_frags_size = size;
		shinfo->nr_frags = 0;
	} else {
		shinfo->xdp_frags_size += size;
	}

	if (page_is_pfmemalloc(rx_swbd->page))
		xdp_buff_set_frag_pfmemalloc(xdp_buff);

	frag = &shinfo->frags[shinfo->nr_frags];
	skb_frag_fill_page_desc(frag, rx_swbd->page, rx_swbd->page_offset,
				size);

	shinfo->nr_frags++;
}

static void enetc_build_xdp_buff(struct enetc_bdr *rx_ring, u32 bd_status,
				 union enetc_rx_bd **rxbd, int *i,
				 int *cleaned_cnt, struct xdp_buff *xdp_buff)
{
	u16 size = le16_to_cpu((*rxbd)->r.buf_len);

	xdp_init_buff(xdp_buff, ENETC_RXB_TRUESIZE, &rx_ring->xdp.rxq);

	enetc_map_rx_buff_to_xdp(rx_ring, *i, xdp_buff, size);
	(*cleaned_cnt)++;
	enetc_rxbd_next(rx_ring, rxbd, i);

	/* not last BD in frame? */
	while (!(bd_status & ENETC_RXBD_LSTATUS_F)) {
		bd_status = le32_to_cpu((*rxbd)->r.lstatus);
		size = ENETC_RXB_DMA_SIZE_XDP;

		if (bd_status & ENETC_RXBD_LSTATUS_F) {
			dma_rmb();
			size = le16_to_cpu((*rxbd)->r.buf_len);
		}

		enetc_add_rx_buff_to_xdp(rx_ring, *i, size, xdp_buff);
		(*cleaned_cnt)++;
		enetc_rxbd_next(rx_ring, rxbd, i);
	}
}

/* Convert RX buffer descriptors to TX buffer descriptors. These will be
 * recycled back into the RX ring in enetc_clean_tx_ring.
 */
static int enetc_rx_swbd_to_xdp_tx_swbd(struct enetc_tx_swbd *xdp_tx_arr,
					struct enetc_bdr *rx_ring,
					int rx_ring_first, int rx_ring_last)
{
	int n = 0;

	for (; rx_ring_first != rx_ring_last;
	     n++, enetc_bdr_idx_inc(rx_ring, &rx_ring_first)) {
		struct enetc_rx_swbd *rx_swbd = &rx_ring->rx_swbd[rx_ring_first];
		struct enetc_tx_swbd *tx_swbd = &xdp_tx_arr[n];

		/* No need to dma_map, we already have DMA_BIDIRECTIONAL */
		tx_swbd->dma = rx_swbd->dma;
		tx_swbd->dir = rx_swbd->dir;
		tx_swbd->page = rx_swbd->page;
		tx_swbd->page_offset = rx_swbd->page_offset;
		tx_swbd->len = rx_swbd->len;
		tx_swbd->is_dma_page = true;
		tx_swbd->is_xdp_tx = true;
		tx_swbd->is_eof = false;
	}

	/* We rely on caller providing an rx_ring_last > rx_ring_first */
	xdp_tx_arr[n - 1].is_eof = true;

	return n;
}

static void enetc_xdp_drop(struct enetc_bdr *rx_ring, int rx_ring_first,
			   int rx_ring_last)
{
	while (rx_ring_first != rx_ring_last) {
		enetc_put_rx_buff(rx_ring,
				  &rx_ring->rx_swbd[rx_ring_first]);
		enetc_bdr_idx_inc(rx_ring, &rx_ring_first);
	}
	rx_ring->stats.xdp_drops++;
}

static int enetc_clean_rx_ring_xdp(struct enetc_bdr *rx_ring,
				   struct napi_struct *napi, int work_limit,
				   struct bpf_prog *prog)
{
	int xdp_tx_bd_cnt, xdp_tx_frm_cnt = 0, xdp_redirect_frm_cnt = 0;
	struct enetc_tx_swbd xdp_tx_arr[ENETC_MAX_SKB_FRAGS] = {0};
	struct enetc_ndev_priv *priv = netdev_priv(rx_ring->ndev);
	int rx_frm_cnt = 0, rx_byte_cnt = 0;
	struct enetc_bdr *tx_ring;
	int cleaned_cnt, i;
	u32 xdp_act;

	cleaned_cnt = enetc_bd_unused(rx_ring);
	/* next descriptor to process */
	i = rx_ring->next_to_clean;

	while (likely(rx_frm_cnt < work_limit)) {
		union enetc_rx_bd *rxbd, *orig_rxbd;
		int orig_i, orig_cleaned_cnt;
		struct xdp_buff xdp_buff;
		struct sk_buff *skb;
		u32 bd_status;
		int err;

		rxbd = enetc_rxbd(rx_ring, i);
		bd_status = le32_to_cpu(rxbd->r.lstatus);
		if (!bd_status)
			break;

		enetc_wr_reg_hot(rx_ring->idr, BIT(rx_ring->index));
		dma_rmb(); /* for reading other rxbd fields */

		if (enetc_check_bd_errors_and_consume(rx_ring, bd_status,
						      &rxbd, &i))
			break;

		orig_rxbd = rxbd;
		orig_cleaned_cnt = cleaned_cnt;
		orig_i = i;

		enetc_build_xdp_buff(rx_ring, bd_status, &rxbd, &i,
				     &cleaned_cnt, &xdp_buff);

		/* When set, the outer VLAN header is extracted and reported
		 * in the receive buffer descriptor. So rx_byte_cnt should
		 * add the length of the extracted VLAN header.
		 */
		if (bd_status & ENETC_RXBD_FLAG_VLAN)
			rx_byte_cnt += VLAN_HLEN;
		rx_byte_cnt += xdp_get_buff_len(&xdp_buff);

		xdp_act = bpf_prog_run_xdp(prog, &xdp_buff);

		switch (xdp_act) {
		default:
			bpf_warn_invalid_xdp_action(rx_ring->ndev, prog, xdp_act);
			fallthrough;
		case XDP_ABORTED:
			trace_xdp_exception(rx_ring->ndev, prog, xdp_act);
			fallthrough;
		case XDP_DROP:
			enetc_xdp_drop(rx_ring, orig_i, i);
			break;
		case XDP_PASS:
			rxbd = orig_rxbd;
			cleaned_cnt = orig_cleaned_cnt;
			i = orig_i;

			skb = enetc_build_skb(rx_ring, bd_status, &rxbd,
					      &i, &cleaned_cnt,
					      ENETC_RXB_DMA_SIZE_XDP);
			if (unlikely(!skb))
				goto out;

			napi_gro_receive(napi, skb);
			break;
		case XDP_TX:
			tx_ring = priv->xdp_tx_ring[rx_ring->index];
			xdp_tx_bd_cnt = enetc_rx_swbd_to_xdp_tx_swbd(xdp_tx_arr,
								     rx_ring,
								     orig_i, i);

			if (!enetc_xdp_tx(tx_ring, xdp_tx_arr, xdp_tx_bd_cnt)) {
				enetc_xdp_drop(rx_ring, orig_i, i);
				tx_ring->stats.xdp_tx_drops++;
			} else {
				tx_ring->stats.xdp_tx += xdp_tx_bd_cnt;
				rx_ring->xdp.xdp_tx_in_flight += xdp_tx_bd_cnt;
				xdp_tx_frm_cnt++;
				/* The XDP_TX enqueue was successful, so we
				 * need to scrub the RX software BDs because
				 * the ownership of the buffers no longer
				 * belongs to the RX ring, and we must prevent
				 * enetc_refill_rx_ring() from reusing
				 * rx_swbd->page.
				 */
				while (orig_i != i) {
					rx_ring->rx_swbd[orig_i].page = NULL;
					enetc_bdr_idx_inc(rx_ring, &orig_i);
				}
			}
			break;
		case XDP_REDIRECT:
			err = xdp_do_redirect(rx_ring->ndev, &xdp_buff, prog);
			if (unlikely(err)) {
				enetc_xdp_drop(rx_ring, orig_i, i);
				rx_ring->stats.xdp_redirect_failures++;
			} else {
				while (orig_i != i) {
					enetc_flip_rx_buff(rx_ring,
							   &rx_ring->rx_swbd[orig_i]);
					enetc_bdr_idx_inc(rx_ring, &orig_i);
				}
				xdp_redirect_frm_cnt++;
				rx_ring->stats.xdp_redirect++;
			}
		}

		rx_frm_cnt++;
	}

out:
	rx_ring->next_to_clean = i;

	rx_ring->stats.packets += rx_frm_cnt;
	rx_ring->stats.bytes += rx_byte_cnt;

	if (xdp_redirect_frm_cnt)
		xdp_do_flush();

	if (xdp_tx_frm_cnt)
		enetc_update_tx_ring_tail(tx_ring);

	if (cleaned_cnt > rx_ring->xdp.xdp_tx_in_flight)
		enetc_refill_rx_ring(rx_ring, enetc_bd_unused(rx_ring) -
				     rx_ring->xdp.xdp_tx_in_flight);

	return rx_frm_cnt;
}

static int enetc_poll(struct napi_struct *napi, int budget)
{
	struct enetc_int_vector
		*v = container_of(napi, struct enetc_int_vector, napi);
	struct enetc_bdr *rx_ring = &v->rx_ring;
	struct bpf_prog *prog;
	bool complete = true;
	int work_done;
	int i;

	enetc_lock_mdio();

	for (i = 0; i < v->count_tx_rings; i++)
		if (!enetc_clean_tx_ring(&v->tx_ring[i], budget))
			complete = false;

	prog = rx_ring->xdp.prog;
	if (prog)
		work_done = enetc_clean_rx_ring_xdp(rx_ring, napi, budget, prog);
	else
		work_done = enetc_clean_rx_ring(rx_ring, napi, budget);
	if (work_done == budget)
		complete = false;
	if (work_done)
		v->rx_napi_work = true;

	if (!complete) {
		enetc_unlock_mdio();
		return budget;
	}

	napi_complete_done(napi, work_done);

	if (likely(v->rx_dim_en))
		enetc_rx_net_dim(v);

	v->rx_napi_work = false;

	/* enable interrupts */
	enetc_wr_reg_hot(v->rbier, ENETC_RBIER_RXTIE);

	for_each_set_bit(i, &v->tx_rings_map, ENETC_MAX_NUM_TXQS)
		enetc_wr_reg_hot(v->tbier_base + ENETC_BDR_OFF(i),
				 ENETC_TBIER_TXTIE);

	enetc_unlock_mdio();

	return work_done;
}

/* Probing and Init */
#define ENETC_MAX_RFS_SIZE 64
void enetc_get_si_caps(struct enetc_si *si)
{
	struct enetc_hw *hw = &si->hw;
	u32 val;

	/* find out how many of various resources we have to work with */
	val = enetc_rd(hw, ENETC_SICAPR0);
	si->num_rx_rings = (val >> 16) & 0xff;
	si->num_tx_rings = val & 0xff;

	val = enetc_rd(hw, ENETC_SIRFSCAPR);
	si->num_fs_entries = ENETC_SIRFSCAPR_GET_NUM_RFS(val);
	si->num_fs_entries = min(si->num_fs_entries, ENETC_MAX_RFS_SIZE);

	si->num_rss = 0;
	val = enetc_rd(hw, ENETC_SIPCAPR0);
	if (val & ENETC_SIPCAPR0_RSS) {
		u32 rss;

		rss = enetc_rd(hw, ENETC_SIRSSCAPR);
		si->num_rss = ENETC_SIRSSCAPR_GET_NUM_RSS(rss);
	}

	if (val & ENETC_SIPCAPR0_QBV)
		si->hw_features |= ENETC_SI_F_QBV;

	if (val & ENETC_SIPCAPR0_QBU)
		si->hw_features |= ENETC_SI_F_QBU;

	if (val & ENETC_SIPCAPR0_PSFP)
		si->hw_features |= ENETC_SI_F_PSFP;
}
EXPORT_SYMBOL_GPL(enetc_get_si_caps);

static int enetc_dma_alloc_bdr(struct enetc_bdr_resource *res)
{
	size_t bd_base_size = res->bd_count * res->bd_size;

	res->bd_base = dma_alloc_coherent(res->dev, bd_base_size,
					  &res->bd_dma_base, GFP_KERNEL);
	if (!res->bd_base)
		return -ENOMEM;

	/* h/w requires 128B alignment */
	if (!IS_ALIGNED(res->bd_dma_base, 128)) {
		dma_free_coherent(res->dev, bd_base_size, res->bd_base,
				  res->bd_dma_base);
		return -EINVAL;
	}

	return 0;
}

static void enetc_dma_free_bdr(const struct enetc_bdr_resource *res)
{
	size_t bd_base_size = res->bd_count * res->bd_size;

	dma_free_coherent(res->dev, bd_base_size, res->bd_base,
			  res->bd_dma_base);
}

static int enetc_alloc_tx_resource(struct enetc_bdr_resource *res,
				   struct device *dev, size_t bd_count)
{
	int err;

	res->dev = dev;
	res->bd_count = bd_count;
	res->bd_size = sizeof(union enetc_tx_bd);

	res->tx_swbd = vcalloc(bd_count, sizeof(*res->tx_swbd));
	if (!res->tx_swbd)
		return -ENOMEM;

	err = enetc_dma_alloc_bdr(res);
	if (err)
		goto err_alloc_bdr;

	res->tso_headers = dma_alloc_coherent(dev, bd_count * TSO_HEADER_SIZE,
					      &res->tso_headers_dma,
					      GFP_KERNEL);
	if (!res->tso_headers) {
		err = -ENOMEM;
		goto err_alloc_tso;
	}

	return 0;

err_alloc_tso:
	enetc_dma_free_bdr(res);
err_alloc_bdr:
	vfree(res->tx_swbd);
	res->tx_swbd = NULL;

	return err;
}

static void enetc_free_tx_resource(const struct enetc_bdr_resource *res)
{
	dma_free_coherent(res->dev, res->bd_count * TSO_HEADER_SIZE,
			  res->tso_headers, res->tso_headers_dma);
	enetc_dma_free_bdr(res);
	vfree(res->tx_swbd);
}

static struct enetc_bdr_resource *
enetc_alloc_tx_resources(struct enetc_ndev_priv *priv)
{
	struct enetc_bdr_resource *tx_res;
	int i, err;

	tx_res = kcalloc(priv->num_tx_rings, sizeof(*tx_res), GFP_KERNEL);
	if (!tx_res)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < priv->num_tx_rings; i++) {
		struct enetc_bdr *tx_ring = priv->tx_ring[i];

		err = enetc_alloc_tx_resource(&tx_res[i], tx_ring->dev,
					      tx_ring->bd_count);
		if (err)
			goto fail;
	}

	return tx_res;

fail:
	while (i-- > 0)
		enetc_free_tx_resource(&tx_res[i]);

	kfree(tx_res);

	return ERR_PTR(err);
}

static void enetc_free_tx_resources(const struct enetc_bdr_resource *tx_res,
				    size_t num_resources)
{
	size_t i;

	for (i = 0; i < num_resources; i++)
		enetc_free_tx_resource(&tx_res[i]);

	kfree(tx_res);
}

static int enetc_alloc_rx_resource(struct enetc_bdr_resource *res,
				   struct device *dev, size_t bd_count,
				   bool extended)
{
	int err;

	res->dev = dev;
	res->bd_count = bd_count;
	res->bd_size = sizeof(union enetc_rx_bd);
	if (extended)
		res->bd_size *= 2;

	res->rx_swbd = vcalloc(bd_count, sizeof(struct enetc_rx_swbd));
	if (!res->rx_swbd)
		return -ENOMEM;

	err = enetc_dma_alloc_bdr(res);
	if (err) {
		vfree(res->rx_swbd);
		return err;
	}

	return 0;
}

static void enetc_free_rx_resource(const struct enetc_bdr_resource *res)
{
	enetc_dma_free_bdr(res);
	vfree(res->rx_swbd);
}

static struct enetc_bdr_resource *
enetc_alloc_rx_resources(struct enetc_ndev_priv *priv, bool extended)
{
	struct enetc_bdr_resource *rx_res;
	int i, err;

	rx_res = kcalloc(priv->num_rx_rings, sizeof(*rx_res), GFP_KERNEL);
	if (!rx_res)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < priv->num_rx_rings; i++) {
		struct enetc_bdr *rx_ring = priv->rx_ring[i];

		err = enetc_alloc_rx_resource(&rx_res[i], rx_ring->dev,
					      rx_ring->bd_count, extended);
		if (err)
			goto fail;
	}

	return rx_res;

fail:
	while (i-- > 0)
		enetc_free_rx_resource(&rx_res[i]);

	kfree(rx_res);

	return ERR_PTR(err);
}

static void enetc_free_rx_resources(const struct enetc_bdr_resource *rx_res,
				    size_t num_resources)
{
	size_t i;

	for (i = 0; i < num_resources; i++)
		enetc_free_rx_resource(&rx_res[i]);

	kfree(rx_res);
}

static void enetc_assign_tx_resource(struct enetc_bdr *tx_ring,
				     const struct enetc_bdr_resource *res)
{
	tx_ring->bd_base = res ? res->bd_base : NULL;
	tx_ring->bd_dma_base = res ? res->bd_dma_base : 0;
	tx_ring->tx_swbd = res ? res->tx_swbd : NULL;
	tx_ring->tso_headers = res ? res->tso_headers : NULL;
	tx_ring->tso_headers_dma = res ? res->tso_headers_dma : 0;
}

static void enetc_assign_rx_resource(struct enetc_bdr *rx_ring,
				     const struct enetc_bdr_resource *res)
{
	rx_ring->bd_base = res ? res->bd_base : NULL;
	rx_ring->bd_dma_base = res ? res->bd_dma_base : 0;
	rx_ring->rx_swbd = res ? res->rx_swbd : NULL;
}

static void enetc_assign_tx_resources(struct enetc_ndev_priv *priv,
				      const struct enetc_bdr_resource *res)
{
	int i;

	if (priv->tx_res)
		enetc_free_tx_resources(priv->tx_res, priv->num_tx_rings);

	for (i = 0; i < priv->num_tx_rings; i++) {
		enetc_assign_tx_resource(priv->tx_ring[i],
					 res ? &res[i] : NULL);
	}

	priv->tx_res = res;
}

static void enetc_assign_rx_resources(struct enetc_ndev_priv *priv,
				      const struct enetc_bdr_resource *res)
{
	int i;

	if (priv->rx_res)
		enetc_free_rx_resources(priv->rx_res, priv->num_rx_rings);

	for (i = 0; i < priv->num_rx_rings; i++) {
		enetc_assign_rx_resource(priv->rx_ring[i],
					 res ? &res[i] : NULL);
	}

	priv->rx_res = res;
}

static void enetc_free_tx_ring(struct enetc_bdr *tx_ring)
{
	int i;

	for (i = 0; i < tx_ring->bd_count; i++) {
		struct enetc_tx_swbd *tx_swbd = &tx_ring->tx_swbd[i];

		enetc_free_tx_frame(tx_ring, tx_swbd);
	}
}

static void enetc_free_rx_ring(struct enetc_bdr *rx_ring)
{
	int i;

	for (i = 0; i < rx_ring->bd_count; i++) {
		struct enetc_rx_swbd *rx_swbd = &rx_ring->rx_swbd[i];

		if (!rx_swbd->page)
			continue;

		dma_unmap_page(rx_ring->dev, rx_swbd->dma, PAGE_SIZE,
			       rx_swbd->dir);
		__free_page(rx_swbd->page);
		rx_swbd->page = NULL;
	}
}

static void enetc_free_rxtx_rings(struct enetc_ndev_priv *priv)
{
	int i;

	for (i = 0; i < priv->num_rx_rings; i++)
		enetc_free_rx_ring(priv->rx_ring[i]);

	for (i = 0; i < priv->num_tx_rings; i++)
		enetc_free_tx_ring(priv->tx_ring[i]);
}

static int enetc_setup_default_rss_table(struct enetc_si *si, int num_groups)
{
	int *rss_table;
	int i;

	rss_table = kmalloc_array(si->num_rss, sizeof(*rss_table), GFP_KERNEL);
	if (!rss_table)
		return -ENOMEM;

	/* Set up RSS table defaults */
	for (i = 0; i < si->num_rss; i++)
		rss_table[i] = i % num_groups;

	enetc_set_rss_table(si, rss_table, si->num_rss);

	kfree(rss_table);

	return 0;
}

int enetc_configure_si(struct enetc_ndev_priv *priv)
{
	struct enetc_si *si = priv->si;
	struct enetc_hw *hw = &si->hw;
	int err;

	/* set SI cache attributes */
	enetc_wr(hw, ENETC_SICAR0,
		 ENETC_SICAR_RD_COHERENT | ENETC_SICAR_WR_COHERENT);
	enetc_wr(hw, ENETC_SICAR1, ENETC_SICAR_MSI);
	/* enable SI */
	enetc_wr(hw, ENETC_SIMR, ENETC_SIMR_EN);

	if (si->num_rss) {
		err = enetc_setup_default_rss_table(si, priv->num_rx_rings);
		if (err)
			return err;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(enetc_configure_si);

void enetc_init_si_rings_params(struct enetc_ndev_priv *priv)
{
	struct enetc_si *si = priv->si;
	int cpus = num_online_cpus();

	priv->tx_bd_count = ENETC_TX_RING_DEFAULT_SIZE;
	priv->rx_bd_count = ENETC_RX_RING_DEFAULT_SIZE;

	/* Enable all available TX rings in order to configure as many
	 * priorities as possible, when needed.
	 * TODO: Make # of TX rings run-time configurable
	 */
	priv->num_rx_rings = min_t(int, cpus, si->num_rx_rings);
	priv->num_tx_rings = si->num_tx_rings;
	priv->bdr_int_num = cpus;
	priv->ic_mode = ENETC_IC_RX_ADAPTIVE | ENETC_IC_TX_MANUAL;
	priv->tx_ictt = ENETC_TXIC_TIMETHR;
}
EXPORT_SYMBOL_GPL(enetc_init_si_rings_params);

int enetc_alloc_si_resources(struct enetc_ndev_priv *priv)
{
	struct enetc_si *si = priv->si;

	priv->cls_rules = kcalloc(si->num_fs_entries, sizeof(*priv->cls_rules),
				  GFP_KERNEL);
	if (!priv->cls_rules)
		return -ENOMEM;

	return 0;
}
EXPORT_SYMBOL_GPL(enetc_alloc_si_resources);

void enetc_free_si_resources(struct enetc_ndev_priv *priv)
{
	kfree(priv->cls_rules);
}
EXPORT_SYMBOL_GPL(enetc_free_si_resources);

static void enetc_setup_txbdr(struct enetc_hw *hw, struct enetc_bdr *tx_ring)
{
	int idx = tx_ring->index;
	u32 tbmr;

	enetc_txbdr_wr(hw, idx, ENETC_TBBAR0,
		       lower_32_bits(tx_ring->bd_dma_base));

	enetc_txbdr_wr(hw, idx, ENETC_TBBAR1,
		       upper_32_bits(tx_ring->bd_dma_base));

	WARN_ON(!IS_ALIGNED(tx_ring->bd_count, 64)); /* multiple of 64 */
	enetc_txbdr_wr(hw, idx, ENETC_TBLENR,
		       ENETC_RTBLENR_LEN(tx_ring->bd_count));

	/* clearing PI/CI registers for Tx not supported, adjust sw indexes */
	tx_ring->next_to_use = enetc_txbdr_rd(hw, idx, ENETC_TBPIR);
	tx_ring->next_to_clean = enetc_txbdr_rd(hw, idx, ENETC_TBCIR);

	/* enable Tx ints by setting pkt thr to 1 */
	enetc_txbdr_wr(hw, idx, ENETC_TBICR0, ENETC_TBICR0_ICEN | 0x1);

	tbmr = ENETC_TBMR_SET_PRIO(tx_ring->prio);
	if (tx_ring->ndev->features & NETIF_F_HW_VLAN_CTAG_TX)
		tbmr |= ENETC_TBMR_VIH;

	/* enable ring */
	enetc_txbdr_wr(hw, idx, ENETC_TBMR, tbmr);

	tx_ring->tpir = hw->reg + ENETC_BDR(TX, idx, ENETC_TBPIR);
	tx_ring->tcir = hw->reg + ENETC_BDR(TX, idx, ENETC_TBCIR);
	tx_ring->idr = hw->reg + ENETC_SITXIDR;
}

static void enetc_setup_rxbdr(struct enetc_hw *hw, struct enetc_bdr *rx_ring,
			      bool extended)
{
	int idx = rx_ring->index;
	u32 rbmr = 0;

	enetc_rxbdr_wr(hw, idx, ENETC_RBBAR0,
		       lower_32_bits(rx_ring->bd_dma_base));

	enetc_rxbdr_wr(hw, idx, ENETC_RBBAR1,
		       upper_32_bits(rx_ring->bd_dma_base));

	WARN_ON(!IS_ALIGNED(rx_ring->bd_count, 64)); /* multiple of 64 */
	enetc_rxbdr_wr(hw, idx, ENETC_RBLENR,
		       ENETC_RTBLENR_LEN(rx_ring->bd_count));

	if (rx_ring->xdp.prog)
		enetc_rxbdr_wr(hw, idx, ENETC_RBBSR, ENETC_RXB_DMA_SIZE_XDP);
	else
		enetc_rxbdr_wr(hw, idx, ENETC_RBBSR, ENETC_RXB_DMA_SIZE);

	/* Also prepare the consumer index in case page allocation never
	 * succeeds. In that case, hardware will never advance producer index
	 * to match consumer index, and will drop all frames.
	 */
	enetc_rxbdr_wr(hw, idx, ENETC_RBPIR, 0);
	enetc_rxbdr_wr(hw, idx, ENETC_RBCIR, 1);

	/* enable Rx ints by setting pkt thr to 1 */
	enetc_rxbdr_wr(hw, idx, ENETC_RBICR0, ENETC_RBICR0_ICEN | 0x1);

	rx_ring->ext_en = extended;
	if (rx_ring->ext_en)
		rbmr |= ENETC_RBMR_BDS;

	if (rx_ring->ndev->features & NETIF_F_HW_VLAN_CTAG_RX)
		rbmr |= ENETC_RBMR_VTE;

	rx_ring->rcir = hw->reg + ENETC_BDR(RX, idx, ENETC_RBCIR);
	rx_ring->idr = hw->reg + ENETC_SIRXIDR;

	rx_ring->next_to_clean = 0;
	rx_ring->next_to_use = 0;
	rx_ring->next_to_alloc = 0;

	enetc_lock_mdio();
	enetc_refill_rx_ring(rx_ring, enetc_bd_unused(rx_ring));
	enetc_unlock_mdio();

	enetc_rxbdr_wr(hw, idx, ENETC_RBMR, rbmr);
}

static void enetc_setup_bdrs(struct enetc_ndev_priv *priv, bool extended)
{
	struct enetc_hw *hw = &priv->si->hw;
	int i;

	for (i = 0; i < priv->num_tx_rings; i++)
		enetc_setup_txbdr(hw, priv->tx_ring[i]);

	for (i = 0; i < priv->num_rx_rings; i++)
		enetc_setup_rxbdr(hw, priv->rx_ring[i], extended);
}

static void enetc_enable_txbdr(struct enetc_hw *hw, struct enetc_bdr *tx_ring)
{
	int idx = tx_ring->index;
	u32 tbmr;

	tbmr = enetc_txbdr_rd(hw, idx, ENETC_TBMR);
	tbmr |= ENETC_TBMR_EN;
	enetc_txbdr_wr(hw, idx, ENETC_TBMR, tbmr);
}

static void enetc_enable_rxbdr(struct enetc_hw *hw, struct enetc_bdr *rx_ring)
{
	int idx = rx_ring->index;
	u32 rbmr;

	rbmr = enetc_rxbdr_rd(hw, idx, ENETC_RBMR);
	rbmr |= ENETC_RBMR_EN;
	enetc_rxbdr_wr(hw, idx, ENETC_RBMR, rbmr);
}

static void enetc_enable_bdrs(struct enetc_ndev_priv *priv)
{
	struct enetc_hw *hw = &priv->si->hw;
	int i;

	for (i = 0; i < priv->num_tx_rings; i++)
		enetc_enable_txbdr(hw, priv->tx_ring[i]);

	for (i = 0; i < priv->num_rx_rings; i++)
		enetc_enable_rxbdr(hw, priv->rx_ring[i]);
}

static void enetc_disable_rxbdr(struct enetc_hw *hw, struct enetc_bdr *rx_ring)
{
	int idx = rx_ring->index;

	/* disable EN bit on ring */
	enetc_rxbdr_wr(hw, idx, ENETC_RBMR, 0);
}

static void enetc_disable_txbdr(struct enetc_hw *hw, struct enetc_bdr *rx_ring)
{
	int idx = rx_ring->index;

	/* disable EN bit on ring */
	enetc_txbdr_wr(hw, idx, ENETC_TBMR, 0);
}

static void enetc_disable_bdrs(struct enetc_ndev_priv *priv)
{
	struct enetc_hw *hw = &priv->si->hw;
	int i;

	for (i = 0; i < priv->num_tx_rings; i++)
		enetc_disable_txbdr(hw, priv->tx_ring[i]);

	for (i = 0; i < priv->num_rx_rings; i++)
		enetc_disable_rxbdr(hw, priv->rx_ring[i]);
}

static void enetc_wait_txbdr(struct enetc_hw *hw, struct enetc_bdr *tx_ring)
{
	int delay = 8, timeout = 100;
	int idx = tx_ring->index;

	/* wait for busy to clear */
	while (delay < timeout &&
	       enetc_txbdr_rd(hw, idx, ENETC_TBSR) & ENETC_TBSR_BUSY) {
		msleep(delay);
		delay *= 2;
	}

	if (delay >= timeout)
		netdev_warn(tx_ring->ndev, "timeout for tx ring #%d clear\n",
			    idx);
}

static void enetc_wait_bdrs(struct enetc_ndev_priv *priv)
{
	struct enetc_hw *hw = &priv->si->hw;
	int i;

	for (i = 0; i < priv->num_tx_rings; i++)
		enetc_wait_txbdr(hw, priv->tx_ring[i]);
}

static int enetc_setup_irqs(struct enetc_ndev_priv *priv)
{
	struct pci_dev *pdev = priv->si->pdev;
	struct enetc_hw *hw = &priv->si->hw;
	int i, j, err;

	for (i = 0; i < priv->bdr_int_num; i++) {
		int irq = pci_irq_vector(pdev, ENETC_BDR_INT_BASE_IDX + i);
		struct enetc_int_vector *v = priv->int_vector[i];
		int entry = ENETC_BDR_INT_BASE_IDX + i;

		snprintf(v->name, sizeof(v->name), "%s-rxtx%d",
			 priv->ndev->name, i);
		err = request_irq(irq, enetc_msix, 0, v->name, v);
		if (err) {
			dev_err(priv->dev, "request_irq() failed!\n");
			goto irq_err;
		}
		disable_irq(irq);

		v->tbier_base = hw->reg + ENETC_BDR(TX, 0, ENETC_TBIER);
		v->rbier = hw->reg + ENETC_BDR(RX, i, ENETC_RBIER);
		v->ricr1 = hw->reg + ENETC_BDR(RX, i, ENETC_RBICR1);

		enetc_wr(hw, ENETC_SIMSIRRV(i), entry);

		for (j = 0; j < v->count_tx_rings; j++) {
			int idx = v->tx_ring[j].index;

			enetc_wr(hw, ENETC_SIMSITRV(idx), entry);
		}
		irq_set_affinity_hint(irq, get_cpu_mask(i % num_online_cpus()));
	}

	return 0;

irq_err:
	while (i--) {
		int irq = pci_irq_vector(pdev, ENETC_BDR_INT_BASE_IDX + i);

		irq_set_affinity_hint(irq, NULL);
		free_irq(irq, priv->int_vector[i]);
	}

	return err;
}

static void enetc_free_irqs(struct enetc_ndev_priv *priv)
{
	struct pci_dev *pdev = priv->si->pdev;
	int i;

	for (i = 0; i < priv->bdr_int_num; i++) {
		int irq = pci_irq_vector(pdev, ENETC_BDR_INT_BASE_IDX + i);

		irq_set_affinity_hint(irq, NULL);
		free_irq(irq, priv->int_vector[i]);
	}
}

static void enetc_setup_interrupts(struct enetc_ndev_priv *priv)
{
	struct enetc_hw *hw = &priv->si->hw;
	u32 icpt, ictt;
	int i;

	/* enable Tx & Rx event indication */
	if (priv->ic_mode &
	    (ENETC_IC_RX_MANUAL | ENETC_IC_RX_ADAPTIVE)) {
		icpt = ENETC_RBICR0_SET_ICPT(ENETC_RXIC_PKTTHR);
		/* init to non-0 minimum, will be adjusted later */
		ictt = 0x1;
	} else {
		icpt = 0x1; /* enable Rx ints by setting pkt thr to 1 */
		ictt = 0;
	}

	for (i = 0; i < priv->num_rx_rings; i++) {
		enetc_rxbdr_wr(hw, i, ENETC_RBICR1, ictt);
		enetc_rxbdr_wr(hw, i, ENETC_RBICR0, ENETC_RBICR0_ICEN | icpt);
		enetc_rxbdr_wr(hw, i, ENETC_RBIER, ENETC_RBIER_RXTIE);
	}

	if (priv->ic_mode & ENETC_IC_TX_MANUAL)
		icpt = ENETC_TBICR0_SET_ICPT(ENETC_TXIC_PKTTHR);
	else
		icpt = 0x1; /* enable Tx ints by setting pkt thr to 1 */

	for (i = 0; i < priv->num_tx_rings; i++) {
		enetc_txbdr_wr(hw, i, ENETC_TBICR1, priv->tx_ictt);
		enetc_txbdr_wr(hw, i, ENETC_TBICR0, ENETC_TBICR0_ICEN | icpt);
		enetc_txbdr_wr(hw, i, ENETC_TBIER, ENETC_TBIER_TXTIE);
	}
}

static void enetc_clear_interrupts(struct enetc_ndev_priv *priv)
{
	struct enetc_hw *hw = &priv->si->hw;
	int i;

	for (i = 0; i < priv->num_tx_rings; i++)
		enetc_txbdr_wr(hw, i, ENETC_TBIER, 0);

	for (i = 0; i < priv->num_rx_rings; i++)
		enetc_rxbdr_wr(hw, i, ENETC_RBIER, 0);
}

static int enetc_phylink_connect(struct net_device *ndev)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	struct ethtool_eee edata;
	int err;

	if (!priv->phylink) {
		/* phy-less mode */
		netif_carrier_on(ndev);
		return 0;
	}

	err = phylink_of_phy_connect(priv->phylink, priv->dev->of_node, 0);
	if (err) {
		dev_err(&ndev->dev, "could not attach to PHY\n");
		return err;
	}

	/* disable EEE autoneg, until ENETC driver supports it */
	memset(&edata, 0, sizeof(struct ethtool_eee));
	phylink_ethtool_set_eee(priv->phylink, &edata);

	phylink_start(priv->phylink);

	return 0;
}

static void enetc_tx_onestep_tstamp(struct work_struct *work)
{
	struct enetc_ndev_priv *priv;
	struct sk_buff *skb;

	priv = container_of(work, struct enetc_ndev_priv, tx_onestep_tstamp);

	netif_tx_lock_bh(priv->ndev);

	clear_bit_unlock(ENETC_TX_ONESTEP_TSTAMP_IN_PROGRESS, &priv->flags);
	skb = skb_dequeue(&priv->tx_skbs);
	if (skb)
		enetc_start_xmit(skb, priv->ndev);

	netif_tx_unlock_bh(priv->ndev);
}

static void enetc_tx_onestep_tstamp_init(struct enetc_ndev_priv *priv)
{
	INIT_WORK(&priv->tx_onestep_tstamp, enetc_tx_onestep_tstamp);
	skb_queue_head_init(&priv->tx_skbs);
}

void enetc_start(struct net_device *ndev)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	int i;

	enetc_setup_interrupts(priv);

	for (i = 0; i < priv->bdr_int_num; i++) {
		int irq = pci_irq_vector(priv->si->pdev,
					 ENETC_BDR_INT_BASE_IDX + i);

		napi_enable(&priv->int_vector[i]->napi);
		enable_irq(irq);
	}

	enetc_enable_bdrs(priv);

	netif_tx_start_all_queues(ndev);
}
EXPORT_SYMBOL_GPL(enetc_start);

int enetc_open(struct net_device *ndev)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	struct enetc_bdr_resource *tx_res, *rx_res;
	bool extended;
	int err;

	extended = !!(priv->active_offloads & ENETC_F_RX_TSTAMP);

	err = enetc_setup_irqs(priv);
	if (err)
		return err;

	err = enetc_phylink_connect(ndev);
	if (err)
		goto err_phy_connect;

	tx_res = enetc_alloc_tx_resources(priv);
	if (IS_ERR(tx_res)) {
		err = PTR_ERR(tx_res);
		goto err_alloc_tx;
	}

	rx_res = enetc_alloc_rx_resources(priv, extended);
	if (IS_ERR(rx_res)) {
		err = PTR_ERR(rx_res);
		goto err_alloc_rx;
	}

	enetc_tx_onestep_tstamp_init(priv);
	enetc_assign_tx_resources(priv, tx_res);
	enetc_assign_rx_resources(priv, rx_res);
	enetc_setup_bdrs(priv, extended);
	enetc_start(ndev);

	return 0;

err_alloc_rx:
	enetc_free_tx_resources(tx_res, priv->num_tx_rings);
err_alloc_tx:
	if (priv->phylink)
		phylink_disconnect_phy(priv->phylink);
err_phy_connect:
	enetc_free_irqs(priv);

	return err;
}
EXPORT_SYMBOL_GPL(enetc_open);

void enetc_stop(struct net_device *ndev)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	int i;

	netif_tx_stop_all_queues(ndev);

	enetc_disable_bdrs(priv);

	for (i = 0; i < priv->bdr_int_num; i++) {
		int irq = pci_irq_vector(priv->si->pdev,
					 ENETC_BDR_INT_BASE_IDX + i);

		disable_irq(irq);
		napi_synchronize(&priv->int_vector[i]->napi);
		napi_disable(&priv->int_vector[i]->napi);
	}

	enetc_wait_bdrs(priv);

	enetc_clear_interrupts(priv);
}
EXPORT_SYMBOL_GPL(enetc_stop);

int enetc_close(struct net_device *ndev)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);

	enetc_stop(ndev);

	if (priv->phylink) {
		phylink_stop(priv->phylink);
		phylink_disconnect_phy(priv->phylink);
	} else {
		netif_carrier_off(ndev);
	}

	enetc_free_rxtx_rings(priv);

	/* Avoids dangling pointers and also frees old resources */
	enetc_assign_rx_resources(priv, NULL);
	enetc_assign_tx_resources(priv, NULL);

	enetc_free_irqs(priv);

	return 0;
}
EXPORT_SYMBOL_GPL(enetc_close);

static int enetc_reconfigure(struct enetc_ndev_priv *priv, bool extended,
			     int (*cb)(struct enetc_ndev_priv *priv, void *ctx),
			     void *ctx)
{
	struct enetc_bdr_resource *tx_res, *rx_res;
	int err;

	ASSERT_RTNL();

	/* If the interface is down, run the callback right away,
	 * without reconfiguration.
	 */
	if (!netif_running(priv->ndev)) {
		if (cb) {
			err = cb(priv, ctx);
			if (err)
				return err;
		}

		return 0;
	}

	tx_res = enetc_alloc_tx_resources(priv);
	if (IS_ERR(tx_res)) {
		err = PTR_ERR(tx_res);
		goto out;
	}

	rx_res = enetc_alloc_rx_resources(priv, extended);
	if (IS_ERR(rx_res)) {
		err = PTR_ERR(rx_res);
		goto out_free_tx_res;
	}

	enetc_stop(priv->ndev);
	enetc_free_rxtx_rings(priv);

	/* Interface is down, run optional callback now */
	if (cb) {
		err = cb(priv, ctx);
		if (err)
			goto out_restart;
	}

	enetc_assign_tx_resources(priv, tx_res);
	enetc_assign_rx_resources(priv, rx_res);
	enetc_setup_bdrs(priv, extended);
	enetc_start(priv->ndev);

	return 0;

out_restart:
	enetc_setup_bdrs(priv, extended);
	enetc_start(priv->ndev);
	enetc_free_rx_resources(rx_res, priv->num_rx_rings);
out_free_tx_res:
	enetc_free_tx_resources(tx_res, priv->num_tx_rings);
out:
	return err;
}

static void enetc_debug_tx_ring_prios(struct enetc_ndev_priv *priv)
{
	int i;

	for (i = 0; i < priv->num_tx_rings; i++)
		netdev_dbg(priv->ndev, "TX ring %d prio %d\n", i,
			   priv->tx_ring[i]->prio);
}

void enetc_reset_tc_mqprio(struct net_device *ndev)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	struct enetc_hw *hw = &priv->si->hw;
	struct enetc_bdr *tx_ring;
	int num_stack_tx_queues;
	int i;

	num_stack_tx_queues = enetc_num_stack_tx_queues(priv);

	netdev_reset_tc(ndev);
	netif_set_real_num_tx_queues(ndev, num_stack_tx_queues);
	priv->min_num_stack_tx_queues = num_possible_cpus();

	/* Reset all ring priorities to 0 */
	for (i = 0; i < priv->num_tx_rings; i++) {
		tx_ring = priv->tx_ring[i];
		tx_ring->prio = 0;
		enetc_set_bdr_prio(hw, tx_ring->index, tx_ring->prio);
	}

	enetc_debug_tx_ring_prios(priv);

	enetc_change_preemptible_tcs(priv, 0);
}
EXPORT_SYMBOL_GPL(enetc_reset_tc_mqprio);

int enetc_setup_tc_mqprio(struct net_device *ndev, void *type_data)
{
	struct tc_mqprio_qopt_offload *mqprio = type_data;
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	struct tc_mqprio_qopt *qopt = &mqprio->qopt;
	struct enetc_hw *hw = &priv->si->hw;
	int num_stack_tx_queues = 0;
	struct enetc_bdr *tx_ring;
	u8 num_tc = qopt->num_tc;
	int offset, count;
	int err, tc, q;

	if (!num_tc) {
		enetc_reset_tc_mqprio(ndev);
		return 0;
	}

	err = netdev_set_num_tc(ndev, num_tc);
	if (err)
		return err;

	for (tc = 0; tc < num_tc; tc++) {
		offset = qopt->offset[tc];
		count = qopt->count[tc];
		num_stack_tx_queues += count;

		err = netdev_set_tc_queue(ndev, tc, count, offset);
		if (err)
			goto err_reset_tc;

		for (q = offset; q < offset + count; q++) {
			tx_ring = priv->tx_ring[q];
			/* The prio_tc_map is skb_tx_hash()'s way of selecting
			 * between TX queues based on skb->priority. As such,
			 * there's nothing to offload based on it.
			 * Make the mqprio "traffic class" be the priority of
			 * this ring group, and leave the Tx IPV to traffic
			 * class mapping as its default mapping value of 1:1.
			 */
			tx_ring->prio = tc;
			enetc_set_bdr_prio(hw, tx_ring->index, tx_ring->prio);
		}
	}

	err = netif_set_real_num_tx_queues(ndev, num_stack_tx_queues);
	if (err)
		goto err_reset_tc;

	priv->min_num_stack_tx_queues = num_stack_tx_queues;

	enetc_debug_tx_ring_prios(priv);

	enetc_change_preemptible_tcs(priv, mqprio->preemptible_tcs);

	return 0;

err_reset_tc:
	enetc_reset_tc_mqprio(ndev);
	return err;
}
EXPORT_SYMBOL_GPL(enetc_setup_tc_mqprio);

static int enetc_reconfigure_xdp_cb(struct enetc_ndev_priv *priv, void *ctx)
{
	struct bpf_prog *old_prog, *prog = ctx;
	int num_stack_tx_queues;
	int err, i;

	old_prog = xchg(&priv->xdp_prog, prog);

	num_stack_tx_queues = enetc_num_stack_tx_queues(priv);
	err = netif_set_real_num_tx_queues(priv->ndev, num_stack_tx_queues);
	if (err) {
		xchg(&priv->xdp_prog, old_prog);
		return err;
	}

	if (old_prog)
		bpf_prog_put(old_prog);

	for (i = 0; i < priv->num_rx_rings; i++) {
		struct enetc_bdr *rx_ring = priv->rx_ring[i];

		rx_ring->xdp.prog = prog;

		if (prog)
			rx_ring->buffer_offset = XDP_PACKET_HEADROOM;
		else
			rx_ring->buffer_offset = ENETC_RXB_PAD;
	}

	return 0;
}

static int enetc_setup_xdp_prog(struct net_device *ndev, struct bpf_prog *prog,
				struct netlink_ext_ack *extack)
{
	int num_xdp_tx_queues = prog ? num_possible_cpus() : 0;
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	bool extended;

	if (priv->min_num_stack_tx_queues + num_xdp_tx_queues >
	    priv->num_tx_rings) {
		NL_SET_ERR_MSG_FMT_MOD(extack,
				       "Reserving %d XDP TXQs does not leave a minimum of %d for stack (total %d)",
				       num_xdp_tx_queues,
				       priv->min_num_stack_tx_queues,
				       priv->num_tx_rings);
		return -EBUSY;
	}

	extended = !!(priv->active_offloads & ENETC_F_RX_TSTAMP);

	/* The buffer layout is changing, so we need to drain the old
	 * RX buffers and seed new ones.
	 */
	return enetc_reconfigure(priv, extended, enetc_reconfigure_xdp_cb, prog);
}

int enetc_setup_bpf(struct net_device *ndev, struct netdev_bpf *bpf)
{
	switch (bpf->command) {
	case XDP_SETUP_PROG:
		return enetc_setup_xdp_prog(ndev, bpf->prog, bpf->extack);
	default:
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(enetc_setup_bpf);

struct net_device_stats *enetc_get_stats(struct net_device *ndev)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	struct net_device_stats *stats = &ndev->stats;
	unsigned long packets = 0, bytes = 0;
	unsigned long tx_dropped = 0;
	int i;

	for (i = 0; i < priv->num_rx_rings; i++) {
		packets += priv->rx_ring[i]->stats.packets;
		bytes	+= priv->rx_ring[i]->stats.bytes;
	}

	stats->rx_packets = packets;
	stats->rx_bytes = bytes;
	bytes = 0;
	packets = 0;

	for (i = 0; i < priv->num_tx_rings; i++) {
		packets += priv->tx_ring[i]->stats.packets;
		bytes	+= priv->tx_ring[i]->stats.bytes;
		tx_dropped += priv->tx_ring[i]->stats.win_drop;
	}

	stats->tx_packets = packets;
	stats->tx_bytes = bytes;
	stats->tx_dropped = tx_dropped;

	return stats;
}
EXPORT_SYMBOL_GPL(enetc_get_stats);

static int enetc_set_rss(struct net_device *ndev, int en)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	struct enetc_hw *hw = &priv->si->hw;
	u32 reg;

	enetc_wr(hw, ENETC_SIRBGCR, priv->num_rx_rings);

	reg = enetc_rd(hw, ENETC_SIMR);
	reg &= ~ENETC_SIMR_RSSE;
	reg |= (en) ? ENETC_SIMR_RSSE : 0;
	enetc_wr(hw, ENETC_SIMR, reg);

	return 0;
}

static void enetc_enable_rxvlan(struct net_device *ndev, bool en)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	struct enetc_hw *hw = &priv->si->hw;
	int i;

	for (i = 0; i < priv->num_rx_rings; i++)
		enetc_bdr_enable_rxvlan(hw, i, en);
}

static void enetc_enable_txvlan(struct net_device *ndev, bool en)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	struct enetc_hw *hw = &priv->si->hw;
	int i;

	for (i = 0; i < priv->num_tx_rings; i++)
		enetc_bdr_enable_txvlan(hw, i, en);
}

void enetc_set_features(struct net_device *ndev, netdev_features_t features)
{
	netdev_features_t changed = ndev->features ^ features;

	if (changed & NETIF_F_RXHASH)
		enetc_set_rss(ndev, !!(features & NETIF_F_RXHASH));

	if (changed & NETIF_F_HW_VLAN_CTAG_RX)
		enetc_enable_rxvlan(ndev,
				    !!(features & NETIF_F_HW_VLAN_CTAG_RX));

	if (changed & NETIF_F_HW_VLAN_CTAG_TX)
		enetc_enable_txvlan(ndev,
				    !!(features & NETIF_F_HW_VLAN_CTAG_TX));
}
EXPORT_SYMBOL_GPL(enetc_set_features);

#ifdef CONFIG_FSL_ENETC_PTP_CLOCK
static int enetc_hwtstamp_set(struct net_device *ndev, struct ifreq *ifr)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	int err, new_offloads = priv->active_offloads;
	struct hwtstamp_config config;

	if (copy_from_user(&config, ifr->ifr_data, sizeof(config)))
		return -EFAULT;

	switch (config.tx_type) {
	case HWTSTAMP_TX_OFF:
		new_offloads &= ~ENETC_F_TX_TSTAMP_MASK;
		break;
	case HWTSTAMP_TX_ON:
		new_offloads &= ~ENETC_F_TX_TSTAMP_MASK;
		new_offloads |= ENETC_F_TX_TSTAMP;
		break;
	case HWTSTAMP_TX_ONESTEP_SYNC:
		new_offloads &= ~ENETC_F_TX_TSTAMP_MASK;
		new_offloads |= ENETC_F_TX_ONESTEP_SYNC_TSTAMP;
		break;
	default:
		return -ERANGE;
	}

	switch (config.rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		new_offloads &= ~ENETC_F_RX_TSTAMP;
		break;
	default:
		new_offloads |= ENETC_F_RX_TSTAMP;
		config.rx_filter = HWTSTAMP_FILTER_ALL;
	}

	if ((new_offloads ^ priv->active_offloads) & ENETC_F_RX_TSTAMP) {
		bool extended = !!(new_offloads & ENETC_F_RX_TSTAMP);

		err = enetc_reconfigure(priv, extended, NULL, NULL);
		if (err)
			return err;
	}

	priv->active_offloads = new_offloads;

	return copy_to_user(ifr->ifr_data, &config, sizeof(config)) ?
	       -EFAULT : 0;
}

static int enetc_hwtstamp_get(struct net_device *ndev, struct ifreq *ifr)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	struct hwtstamp_config config;

	config.flags = 0;

	if (priv->active_offloads & ENETC_F_TX_ONESTEP_SYNC_TSTAMP)
		config.tx_type = HWTSTAMP_TX_ONESTEP_SYNC;
	else if (priv->active_offloads & ENETC_F_TX_TSTAMP)
		config.tx_type = HWTSTAMP_TX_ON;
	else
		config.tx_type = HWTSTAMP_TX_OFF;

	config.rx_filter = (priv->active_offloads & ENETC_F_RX_TSTAMP) ?
			    HWTSTAMP_FILTER_ALL : HWTSTAMP_FILTER_NONE;

	return copy_to_user(ifr->ifr_data, &config, sizeof(config)) ?
	       -EFAULT : 0;
}
#endif

int enetc_ioctl(struct net_device *ndev, struct ifreq *rq, int cmd)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
#ifdef CONFIG_FSL_ENETC_PTP_CLOCK
	if (cmd == SIOCSHWTSTAMP)
		return enetc_hwtstamp_set(ndev, rq);
	if (cmd == SIOCGHWTSTAMP)
		return enetc_hwtstamp_get(ndev, rq);
#endif

	if (!priv->phylink)
		return -EOPNOTSUPP;

	return phylink_mii_ioctl(priv->phylink, rq, cmd);
}
EXPORT_SYMBOL_GPL(enetc_ioctl);

int enetc_alloc_msix(struct enetc_ndev_priv *priv)
{
	struct pci_dev *pdev = priv->si->pdev;
	int num_stack_tx_queues;
	int first_xdp_tx_ring;
	int i, n, err, nvec;
	int v_tx_rings;

	nvec = ENETC_BDR_INT_BASE_IDX + priv->bdr_int_num;
	/* allocate MSIX for both messaging and Rx/Tx interrupts */
	n = pci_alloc_irq_vectors(pdev, nvec, nvec, PCI_IRQ_MSIX);

	if (n < 0)
		return n;

	if (n != nvec)
		return -EPERM;

	/* # of tx rings per int vector */
	v_tx_rings = priv->num_tx_rings / priv->bdr_int_num;

	for (i = 0; i < priv->bdr_int_num; i++) {
		struct enetc_int_vector *v;
		struct enetc_bdr *bdr;
		int j;

		v = kzalloc(struct_size(v, tx_ring, v_tx_rings), GFP_KERNEL);
		if (!v) {
			err = -ENOMEM;
			goto fail;
		}

		priv->int_vector[i] = v;

		bdr = &v->rx_ring;
		bdr->index = i;
		bdr->ndev = priv->ndev;
		bdr->dev = priv->dev;
		bdr->bd_count = priv->rx_bd_count;
		bdr->buffer_offset = ENETC_RXB_PAD;
		priv->rx_ring[i] = bdr;

		err = xdp_rxq_info_reg(&bdr->xdp.rxq, priv->ndev, i, 0);
		if (err) {
			kfree(v);
			goto fail;
		}

		err = xdp_rxq_info_reg_mem_model(&bdr->xdp.rxq,
						 MEM_TYPE_PAGE_SHARED, NULL);
		if (err) {
			xdp_rxq_info_unreg(&bdr->xdp.rxq);
			kfree(v);
			goto fail;
		}

		/* init defaults for adaptive IC */
		if (priv->ic_mode & ENETC_IC_RX_ADAPTIVE) {
			v->rx_ictt = 0x1;
			v->rx_dim_en = true;
		}
		INIT_WORK(&v->rx_dim.work, enetc_rx_dim_work);
		netif_napi_add(priv->ndev, &v->napi, enetc_poll);
		v->count_tx_rings = v_tx_rings;

		for (j = 0; j < v_tx_rings; j++) {
			int idx;

			/* default tx ring mapping policy */
			idx = priv->bdr_int_num * j + i;
			__set_bit(idx, &v->tx_rings_map);
			bdr = &v->tx_ring[j];
			bdr->index = idx;
			bdr->ndev = priv->ndev;
			bdr->dev = priv->dev;
			bdr->bd_count = priv->tx_bd_count;
			priv->tx_ring[idx] = bdr;
		}
	}

	num_stack_tx_queues = enetc_num_stack_tx_queues(priv);

	err = netif_set_real_num_tx_queues(priv->ndev, num_stack_tx_queues);
	if (err)
		goto fail;

	err = netif_set_real_num_rx_queues(priv->ndev, priv->num_rx_rings);
	if (err)
		goto fail;

	priv->min_num_stack_tx_queues = num_possible_cpus();
	first_xdp_tx_ring = priv->num_tx_rings - num_possible_cpus();
	priv->xdp_tx_ring = &priv->tx_ring[first_xdp_tx_ring];

	return 0;

fail:
	while (i--) {
		struct enetc_int_vector *v = priv->int_vector[i];
		struct enetc_bdr *rx_ring = &v->rx_ring;

		xdp_rxq_info_unreg_mem_model(&rx_ring->xdp.rxq);
		xdp_rxq_info_unreg(&rx_ring->xdp.rxq);
		netif_napi_del(&v->napi);
		cancel_work_sync(&v->rx_dim.work);
		kfree(v);
	}

	pci_free_irq_vectors(pdev);

	return err;
}
EXPORT_SYMBOL_GPL(enetc_alloc_msix);

void enetc_free_msix(struct enetc_ndev_priv *priv)
{
	int i;

	for (i = 0; i < priv->bdr_int_num; i++) {
		struct enetc_int_vector *v = priv->int_vector[i];
		struct enetc_bdr *rx_ring = &v->rx_ring;

		xdp_rxq_info_unreg_mem_model(&rx_ring->xdp.rxq);
		xdp_rxq_info_unreg(&rx_ring->xdp.rxq);
		netif_napi_del(&v->napi);
		cancel_work_sync(&v->rx_dim.work);
	}

	for (i = 0; i < priv->num_rx_rings; i++)
		priv->rx_ring[i] = NULL;

	for (i = 0; i < priv->num_tx_rings; i++)
		priv->tx_ring[i] = NULL;

	for (i = 0; i < priv->bdr_int_num; i++) {
		kfree(priv->int_vector[i]);
		priv->int_vector[i] = NULL;
	}

	/* disable all MSIX for this device */
	pci_free_irq_vectors(priv->si->pdev);
}
EXPORT_SYMBOL_GPL(enetc_free_msix);

static void enetc_kfree_si(struct enetc_si *si)
{
	char *p = (char *)si - si->pad;

	kfree(p);
}

static void enetc_detect_errata(struct enetc_si *si)
{
	if (si->pdev->revision == ENETC_REV1)
		si->errata = ENETC_ERR_VLAN_ISOL | ENETC_ERR_UCMCSWP;
}

int enetc_pci_probe(struct pci_dev *pdev, const char *name, int sizeof_priv)
{
	struct enetc_si *si, *p;
	struct enetc_hw *hw;
	size_t alloc_size;
	int err, len;

	pcie_flr(pdev);
	err = pci_enable_device_mem(pdev);
	if (err)
		return dev_err_probe(&pdev->dev, err, "device enable failed\n");

	/* set up for high or low dma */
	err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (err) {
		dev_err(&pdev->dev, "DMA configuration failed: 0x%x\n", err);
		goto err_dma;
	}

	err = pci_request_mem_regions(pdev, name);
	if (err) {
		dev_err(&pdev->dev, "pci_request_regions failed err=%d\n", err);
		goto err_pci_mem_reg;
	}

	pci_set_master(pdev);

	alloc_size = sizeof(struct enetc_si);
	if (sizeof_priv) {
		/* align priv to 32B */
		alloc_size = ALIGN(alloc_size, ENETC_SI_ALIGN);
		alloc_size += sizeof_priv;
	}
	/* force 32B alignment for enetc_si */
	alloc_size += ENETC_SI_ALIGN - 1;

	p = kzalloc(alloc_size, GFP_KERNEL);
	if (!p) {
		err = -ENOMEM;
		goto err_alloc_si;
	}

	si = PTR_ALIGN(p, ENETC_SI_ALIGN);
	si->pad = (char *)si - (char *)p;

	pci_set_drvdata(pdev, si);
	si->pdev = pdev;
	hw = &si->hw;

	len = pci_resource_len(pdev, ENETC_BAR_REGS);
	hw->reg = ioremap(pci_resource_start(pdev, ENETC_BAR_REGS), len);
	if (!hw->reg) {
		err = -ENXIO;
		dev_err(&pdev->dev, "ioremap() failed\n");
		goto err_ioremap;
	}
	if (len > ENETC_PORT_BASE)
		hw->port = hw->reg + ENETC_PORT_BASE;
	if (len > ENETC_GLOBAL_BASE)
		hw->global = hw->reg + ENETC_GLOBAL_BASE;

	enetc_detect_errata(si);

	return 0;

err_ioremap:
	enetc_kfree_si(si);
err_alloc_si:
	pci_release_mem_regions(pdev);
err_pci_mem_reg:
err_dma:
	pci_disable_device(pdev);

	return err;
}
EXPORT_SYMBOL_GPL(enetc_pci_probe);

void enetc_pci_remove(struct pci_dev *pdev)
{
	struct enetc_si *si = pci_get_drvdata(pdev);
	struct enetc_hw *hw = &si->hw;

	iounmap(hw->reg);
	enetc_kfree_si(si);
	pci_release_mem_regions(pdev);
	pci_disable_device(pdev);
}
EXPORT_SYMBOL_GPL(enetc_pci_remove);

MODULE_LICENSE("Dual BSD/GPL");
