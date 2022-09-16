// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/* Copyright 2017-2019 NXP */

#include "enetc.h"
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/vmalloc.h>

/* ENETC overhead: optional extension BD + 1 BD gap */
#define ENETC_TXBDS_NEEDED(val)	((val) + 2)
/* max # of chained Tx BDs is 15, including head and extension BD */
#define ENETC_MAX_SKB_FRAGS	13
#define ENETC_TXBDS_MAX_NEEDED	ENETC_TXBDS_NEEDED(ENETC_MAX_SKB_FRAGS + 1)

static int enetc_map_tx_buffs(struct enetc_bdr *tx_ring, struct sk_buff *skb,
			      int active_offloads);

netdev_tx_t enetc_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	struct enetc_bdr *tx_ring;
	int count;

	tx_ring = priv->tx_ring[skb->queue_mapping];

	if (unlikely(skb_shinfo(skb)->nr_frags > ENETC_MAX_SKB_FRAGS))
		if (unlikely(skb_linearize(skb)))
			goto drop_packet_err;

	count = skb_shinfo(skb)->nr_frags + 1; /* fragments + head */
	if (enetc_bd_unused(tx_ring) < ENETC_TXBDS_NEEDED(count)) {
		netif_stop_subqueue(ndev, tx_ring->index);
		return NETDEV_TX_BUSY;
	}

	enetc_lock_mdio();
	count = enetc_map_tx_buffs(tx_ring, skb, priv->active_offloads);
	enetc_unlock_mdio();

	if (unlikely(!count))
		goto drop_packet_err;

	if (enetc_bd_unused(tx_ring) < ENETC_TXBDS_MAX_NEEDED)
		netif_stop_subqueue(ndev, tx_ring->index);

	return NETDEV_TX_OK;

drop_packet_err:
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

static bool enetc_tx_csum(struct sk_buff *skb, union enetc_tx_bd *txbd)
{
	int l3_start, l3_hsize;
	u16 l3_flags, l4_flags;

	if (skb->ip_summed != CHECKSUM_PARTIAL)
		return false;

	switch (skb->csum_offset) {
	case offsetof(struct tcphdr, check):
		l4_flags = ENETC_TXBD_L4_TCP;
		break;
	case offsetof(struct udphdr, check):
		l4_flags = ENETC_TXBD_L4_UDP;
		break;
	default:
		skb_checksum_help(skb);
		return false;
	}

	l3_start = skb_network_offset(skb);
	l3_hsize = skb_network_header_len(skb);

	l3_flags = 0;
	if (skb->protocol == htons(ETH_P_IPV6))
		l3_flags = ENETC_TXBD_L3_IPV6;

	/* write BD fields */
	txbd->l3_csoff = enetc_txbd_l3_csoff(l3_start, l3_hsize, l3_flags);
	txbd->l4_csoff = l4_flags;

	return true;
}

static void enetc_unmap_tx_buff(struct enetc_bdr *tx_ring,
				struct enetc_tx_swbd *tx_swbd)
{
	if (tx_swbd->is_dma_page)
		dma_unmap_page(tx_ring->dev, tx_swbd->dma,
			       tx_swbd->len, DMA_TO_DEVICE);
	else
		dma_unmap_single(tx_ring->dev, tx_swbd->dma,
				 tx_swbd->len, DMA_TO_DEVICE);
	tx_swbd->dma = 0;
}

static void enetc_free_tx_skb(struct enetc_bdr *tx_ring,
			      struct enetc_tx_swbd *tx_swbd)
{
	if (tx_swbd->dma)
		enetc_unmap_tx_buff(tx_ring, tx_swbd);

	if (tx_swbd->skb) {
		dev_kfree_skb_any(tx_swbd->skb);
		tx_swbd->skb = NULL;
	}
}

static int enetc_map_tx_buffs(struct enetc_bdr *tx_ring, struct sk_buff *skb,
			      int active_offloads)
{
	struct enetc_tx_swbd *tx_swbd;
	skb_frag_t *frag;
	int len = skb_headlen(skb);
	union enetc_tx_bd temp_bd;
	union enetc_tx_bd *txbd;
	bool do_vlan, do_tstamp;
	int i, count = 0;
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
	count++;

	do_vlan = skb_vlan_tag_present(skb);
	do_tstamp = (active_offloads & ENETC_F_TX_TSTAMP) &&
		    (skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP);
	tx_swbd->do_tstamp = do_tstamp;
	tx_swbd->check_wb = tx_swbd->do_tstamp;

	if (do_vlan || do_tstamp)
		flags |= ENETC_TXBD_FLAGS_EX;

	if (enetc_tx_csum(skb, &temp_bd))
		flags |= ENETC_TXBD_FLAGS_CSUM | ENETC_TXBD_FLAGS_L4CS;
	else if (tx_ring->tsd_enable)
		flags |= ENETC_TXBD_FLAGS_TSE | ENETC_TXBD_FLAGS_TXSTART;

	/* first BD needs frm_len and offload flags set */
	temp_bd.frm_len = cpu_to_le16(skb->len);
	temp_bd.flags = flags;

	if (flags & ENETC_TXBD_FLAGS_TSE) {
		u32 temp;

		temp = (skb->skb_mstamp_ns >> 5 & ENETC_TXBD_TXSTART_MASK)
			| (flags << ENETC_TXBD_FLAGS_OFFSET);
		temp_bd.txstart = cpu_to_le32(temp);
	}

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

		if (do_tstamp) {
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
		count++;
	}

	/* last BD needs 'F' bit set */
	flags |= ENETC_TXBD_FLAGS_F;
	temp_bd.flags = flags;
	*txbd = temp_bd;

	tx_ring->tx_swbd[i].skb = skb;

	enetc_bdr_idx_inc(tx_ring, &i);
	tx_ring->next_to_use = i;

	skb_tx_timestamp(skb);

	/* let H/W know BD ring has been updated */
	enetc_wr_reg_hot(tx_ring->tpir, i); /* includes wmb() */

	return count;

dma_err:
	dev_err(tx_ring->dev, "DMA map error");

	do {
		tx_swbd = &tx_ring->tx_swbd[i];
		enetc_free_tx_skb(tx_ring, tx_swbd);
		if (i == 0)
			i = tx_ring->bd_count;
		i--;
	} while (count--);

	return 0;
}

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

static bool enetc_clean_tx_ring(struct enetc_bdr *tx_ring, int napi_budget);
static int enetc_clean_rx_ring(struct enetc_bdr *rx_ring,
			       struct napi_struct *napi, int work_limit);

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

static int enetc_poll(struct napi_struct *napi, int budget)
{
	struct enetc_int_vector
		*v = container_of(napi, struct enetc_int_vector, napi);
	bool complete = true;
	int work_done;
	int i;

	enetc_lock_mdio();

	for (i = 0; i < v->count_tx_rings; i++)
		if (!enetc_clean_tx_ring(&v->tx_ring[i], budget))
			complete = false;

	work_done = enetc_clean_rx_ring(&v->rx_ring, napi, budget);
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

static int enetc_bd_ready_count(struct enetc_bdr *tx_ring, int ci)
{
	int pi = enetc_rd_reg_hot(tx_ring->tcir) & ENETC_TBCIR_IDX_MASK;

	return pi >= ci ? pi - ci : tx_ring->bd_count - ci + pi;
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
		/* Ensure skb_mstamp_ns, which might have been populated with
		 * the txtime, is not mistaken for a software timestamp,
		 * because this will prevent the dispatch of our hardware
		 * timestamp to the socket.
		 */
		skb->tstamp = ktime_set(0, 0);
		skb_tstamp_tx(skb, &shhwtstamps);
	}
}

static bool enetc_clean_tx_ring(struct enetc_bdr *tx_ring, int napi_budget)
{
	struct net_device *ndev = tx_ring->ndev;
	int tx_frm_cnt = 0, tx_byte_cnt = 0;
	struct enetc_tx_swbd *tx_swbd;
	int i, bds_to_clean;
	bool do_tstamp;
	u64 tstamp = 0;

	i = tx_ring->next_to_clean;
	tx_swbd = &tx_ring->tx_swbd[i];

	bds_to_clean = enetc_bd_ready_count(tx_ring, i);

	do_tstamp = false;

	while (bds_to_clean && tx_frm_cnt < ENETC_DEFAULT_TX_WORK) {
		bool is_eof = !!tx_swbd->skb;

		if (unlikely(tx_swbd->check_wb)) {
			struct enetc_ndev_priv *priv = netdev_priv(ndev);
			union enetc_tx_bd *txbd;

			txbd = ENETC_TXBD(*tx_ring, i);

			if (txbd->flags & ENETC_TXBD_FLAGS_W &&
			    tx_swbd->do_tstamp) {
				enetc_get_tx_tstamp(&priv->si->hw, txbd,
						    &tstamp);
				do_tstamp = true;
			}
		}

		if (likely(tx_swbd->dma))
			enetc_unmap_tx_buff(tx_ring, tx_swbd);

		if (is_eof) {
			if (unlikely(do_tstamp)) {
				enetc_tstamp_tx(tx_swbd->skb, tstamp);
				do_tstamp = false;
			}
			napi_consume_skb(tx_swbd->skb, napi_budget);
			tx_swbd->skb = NULL;
		}

		tx_byte_cnt += tx_swbd->len;

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
	struct page *page;
	dma_addr_t addr;

	page = dev_alloc_page();
	if (unlikely(!page))
		return false;

	addr = dma_map_page(rx_ring->dev, page, 0, PAGE_SIZE, DMA_FROM_DEVICE);
	if (unlikely(dma_mapping_error(rx_ring->dev, addr))) {
		__free_page(page);

		return false;
	}

	rx_swbd->dma = addr;
	rx_swbd->page = page;
	rx_swbd->page_offset = ENETC_RXB_PAD;

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

		rxbd = enetc_rxbd_next(rx_ring, rxbd, i);
		rx_swbd++;
		i++;
		if (unlikely(i == rx_ring->bd_count)) {
			i = 0;
			rx_swbd = rx_ring->rx_swbd;
		}
	}

	if (likely(j)) {
		rx_ring->next_to_alloc = i; /* keep track from page reuse */
		rx_ring->next_to_use = i;
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

static void enetc_process_skb(struct enetc_bdr *rx_ring,
			      struct sk_buff *skb)
{
	skb_record_rx_queue(skb, rx_ring->index);
	skb->protocol = eth_type_trans(skb, rx_ring->ndev);
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

static struct enetc_rx_swbd *enetc_get_rx_buff(struct enetc_bdr *rx_ring,
					       int i, u16 size)
{
	struct enetc_rx_swbd *rx_swbd = &rx_ring->rx_swbd[i];

	dma_sync_single_range_for_cpu(rx_ring->dev, rx_swbd->dma,
				      rx_swbd->page_offset,
				      size, DMA_FROM_DEVICE);
	return rx_swbd;
}

static void enetc_put_rx_buff(struct enetc_bdr *rx_ring,
			      struct enetc_rx_swbd *rx_swbd)
{
	if (likely(enetc_page_reusable(rx_swbd->page))) {
		rx_swbd->page_offset ^= ENETC_RXB_TRUESIZE;
		page_ref_inc(rx_swbd->page);

		enetc_reuse_page(rx_ring, rx_swbd);

		/* sync for use by the device */
		dma_sync_single_range_for_device(rx_ring->dev, rx_swbd->dma,
						 rx_swbd->page_offset,
						 ENETC_RXB_DMA_SIZE,
						 DMA_FROM_DEVICE);
	} else {
		dma_unmap_page(rx_ring->dev, rx_swbd->dma,
			       PAGE_SIZE, DMA_FROM_DEVICE);
	}

	rx_swbd->page = NULL;
}

static struct sk_buff *enetc_map_rx_buff_to_skb(struct enetc_bdr *rx_ring,
						int i, u16 size)
{
	struct enetc_rx_swbd *rx_swbd = enetc_get_rx_buff(rx_ring, i, size);
	struct sk_buff *skb;
	void *ba;

	ba = page_address(rx_swbd->page) + rx_swbd->page_offset;
	skb = build_skb(ba - ENETC_RXB_PAD, ENETC_RXB_TRUESIZE);
	if (unlikely(!skb)) {
		rx_ring->stats.rx_alloc_errs++;
		return NULL;
	}

	skb_reserve(skb, ENETC_RXB_PAD);
	__skb_put(skb, size);

	enetc_put_rx_buff(rx_ring, rx_swbd);

	return skb;
}

static void enetc_add_rx_buff_to_skb(struct enetc_bdr *rx_ring, int i,
				     u16 size, struct sk_buff *skb)
{
	struct enetc_rx_swbd *rx_swbd = enetc_get_rx_buff(rx_ring, i, size);

	skb_add_rx_frag(skb, skb_shinfo(skb)->nr_frags, rx_swbd->page,
			rx_swbd->page_offset, size, ENETC_RXB_TRUESIZE);

	enetc_put_rx_buff(rx_ring, rx_swbd);
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
		u16 size;

		if (cleaned_cnt >= ENETC_RXBD_BUNDLE) {
			int count = enetc_refill_rx_ring(rx_ring, cleaned_cnt);

			/* update ENETC's consumer index */
			enetc_wr_reg_hot(rx_ring->rcir, rx_ring->next_to_use);
			cleaned_cnt -= count;
		}

		rxbd = enetc_rxbd(rx_ring, i);
		bd_status = le32_to_cpu(rxbd->r.lstatus);
		if (!bd_status)
			break;

		enetc_wr_reg_hot(rx_ring->idr, BIT(rx_ring->index));
		dma_rmb(); /* for reading other rxbd fields */
		size = le16_to_cpu(rxbd->r.buf_len);
		skb = enetc_map_rx_buff_to_skb(rx_ring, i, size);
		if (!skb)
			break;

		enetc_get_offloads(rx_ring, rxbd, skb);

		cleaned_cnt++;

		rxbd = enetc_rxbd_next(rx_ring, rxbd, i);
		if (unlikely(++i == rx_ring->bd_count))
			i = 0;

		if (unlikely(bd_status &
			     ENETC_RXBD_LSTATUS(ENETC_RXBD_ERR_MASK))) {
			dev_kfree_skb(skb);
			while (!(bd_status & ENETC_RXBD_LSTATUS_F)) {
				dma_rmb();
				bd_status = le32_to_cpu(rxbd->r.lstatus);

				rxbd = enetc_rxbd_next(rx_ring, rxbd, i);
				if (unlikely(++i == rx_ring->bd_count))
					i = 0;
			}

			rx_ring->ndev->stats.rx_dropped++;
			rx_ring->ndev->stats.rx_errors++;

			break;
		}

		/* not last BD in frame? */
		while (!(bd_status & ENETC_RXBD_LSTATUS_F)) {
			bd_status = le32_to_cpu(rxbd->r.lstatus);
			size = ENETC_RXB_DMA_SIZE;

			if (bd_status & ENETC_RXBD_LSTATUS_F) {
				dma_rmb();
				size = le16_to_cpu(rxbd->r.buf_len);
			}

			enetc_add_rx_buff_to_skb(rx_ring, i, size, skb);

			cleaned_cnt++;

			rxbd = enetc_rxbd_next(rx_ring, rxbd, i);
			if (unlikely(++i == rx_ring->bd_count))
				i = 0;
		}

		rx_byte_cnt += skb->len;

		enetc_process_skb(rx_ring, skb);

		napi_gro_receive(napi, skb);

		rx_frm_cnt++;
	}

	rx_ring->next_to_clean = i;

	rx_ring->stats.packets += rx_frm_cnt;
	rx_ring->stats.bytes += rx_byte_cnt;

	return rx_frm_cnt;
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

	if (val & ENETC_SIPCAPR0_PSFP)
		si->hw_features |= ENETC_SI_F_PSFP;
}

static int enetc_dma_alloc_bdr(struct enetc_bdr *r, size_t bd_size)
{
	r->bd_base = dma_alloc_coherent(r->dev, r->bd_count * bd_size,
					&r->bd_dma_base, GFP_KERNEL);
	if (!r->bd_base)
		return -ENOMEM;

	/* h/w requires 128B alignment */
	if (!IS_ALIGNED(r->bd_dma_base, 128)) {
		dma_free_coherent(r->dev, r->bd_count * bd_size, r->bd_base,
				  r->bd_dma_base);
		return -EINVAL;
	}

	return 0;
}

static int enetc_alloc_txbdr(struct enetc_bdr *txr)
{
	int err;

	txr->tx_swbd = vzalloc(txr->bd_count * sizeof(struct enetc_tx_swbd));
	if (!txr->tx_swbd)
		return -ENOMEM;

	err = enetc_dma_alloc_bdr(txr, sizeof(union enetc_tx_bd));
	if (err) {
		vfree(txr->tx_swbd);
		return err;
	}

	txr->next_to_clean = 0;
	txr->next_to_use = 0;

	return 0;
}

static void enetc_free_txbdr(struct enetc_bdr *txr)
{
	int size, i;

	for (i = 0; i < txr->bd_count; i++)
		enetc_free_tx_skb(txr, &txr->tx_swbd[i]);

	size = txr->bd_count * sizeof(union enetc_tx_bd);

	dma_free_coherent(txr->dev, size, txr->bd_base, txr->bd_dma_base);
	txr->bd_base = NULL;

	vfree(txr->tx_swbd);
	txr->tx_swbd = NULL;
}

static int enetc_alloc_tx_resources(struct enetc_ndev_priv *priv)
{
	int i, err;

	for (i = 0; i < priv->num_tx_rings; i++) {
		err = enetc_alloc_txbdr(priv->tx_ring[i]);

		if (err)
			goto fail;
	}

	return 0;

fail:
	while (i-- > 0)
		enetc_free_txbdr(priv->tx_ring[i]);

	return err;
}

static void enetc_free_tx_resources(struct enetc_ndev_priv *priv)
{
	int i;

	for (i = 0; i < priv->num_tx_rings; i++)
		enetc_free_txbdr(priv->tx_ring[i]);
}

static int enetc_alloc_rxbdr(struct enetc_bdr *rxr, bool extended)
{
	size_t size = sizeof(union enetc_rx_bd);
	int err;

	rxr->rx_swbd = vzalloc(rxr->bd_count * sizeof(struct enetc_rx_swbd));
	if (!rxr->rx_swbd)
		return -ENOMEM;

	if (extended)
		size *= 2;

	err = enetc_dma_alloc_bdr(rxr, size);
	if (err) {
		vfree(rxr->rx_swbd);
		return err;
	}

	rxr->next_to_clean = 0;
	rxr->next_to_use = 0;
	rxr->next_to_alloc = 0;
	rxr->ext_en = extended;

	return 0;
}

static void enetc_free_rxbdr(struct enetc_bdr *rxr)
{
	int size;

	size = rxr->bd_count * sizeof(union enetc_rx_bd);

	dma_free_coherent(rxr->dev, size, rxr->bd_base, rxr->bd_dma_base);
	rxr->bd_base = NULL;

	vfree(rxr->rx_swbd);
	rxr->rx_swbd = NULL;
}

static int enetc_alloc_rx_resources(struct enetc_ndev_priv *priv)
{
	bool extended = !!(priv->active_offloads & ENETC_F_RX_TSTAMP);
	int i, err;

	for (i = 0; i < priv->num_rx_rings; i++) {
		err = enetc_alloc_rxbdr(priv->rx_ring[i], extended);

		if (err)
			goto fail;
	}

	return 0;

fail:
	while (i-- > 0)
		enetc_free_rxbdr(priv->rx_ring[i]);

	return err;
}

static void enetc_free_rx_resources(struct enetc_ndev_priv *priv)
{
	int i;

	for (i = 0; i < priv->num_rx_rings; i++)
		enetc_free_rxbdr(priv->rx_ring[i]);
}

static void enetc_free_tx_ring(struct enetc_bdr *tx_ring)
{
	int i;

	if (!tx_ring->tx_swbd)
		return;

	for (i = 0; i < tx_ring->bd_count; i++) {
		struct enetc_tx_swbd *tx_swbd = &tx_ring->tx_swbd[i];

		enetc_free_tx_skb(tx_ring, tx_swbd);
	}

	tx_ring->next_to_clean = 0;
	tx_ring->next_to_use = 0;
}

static void enetc_free_rx_ring(struct enetc_bdr *rx_ring)
{
	int i;

	if (!rx_ring->rx_swbd)
		return;

	for (i = 0; i < rx_ring->bd_count; i++) {
		struct enetc_rx_swbd *rx_swbd = &rx_ring->rx_swbd[i];

		if (!rx_swbd->page)
			continue;

		dma_unmap_page(rx_ring->dev, rx_swbd->dma,
			       PAGE_SIZE, DMA_FROM_DEVICE);
		__free_page(rx_swbd->page);
		rx_swbd->page = NULL;
	}

	rx_ring->next_to_clean = 0;
	rx_ring->next_to_use = 0;
	rx_ring->next_to_alloc = 0;
}

static void enetc_free_rxtx_rings(struct enetc_ndev_priv *priv)
{
	int i;

	for (i = 0; i < priv->num_rx_rings; i++)
		enetc_free_rx_ring(priv->rx_ring[i]);

	for (i = 0; i < priv->num_tx_rings; i++)
		enetc_free_tx_ring(priv->tx_ring[i]);
}

int enetc_alloc_cbdr(struct device *dev, struct enetc_cbdr *cbdr)
{
	int size = cbdr->bd_count * sizeof(struct enetc_cbd);

	cbdr->bd_base = dma_alloc_coherent(dev, size, &cbdr->bd_dma_base,
					   GFP_KERNEL);
	if (!cbdr->bd_base)
		return -ENOMEM;

	/* h/w requires 128B alignment */
	if (!IS_ALIGNED(cbdr->bd_dma_base, 128)) {
		dma_free_coherent(dev, size, cbdr->bd_base, cbdr->bd_dma_base);
		return -EINVAL;
	}

	cbdr->next_to_clean = 0;
	cbdr->next_to_use = 0;

	return 0;
}

void enetc_free_cbdr(struct device *dev, struct enetc_cbdr *cbdr)
{
	int size = cbdr->bd_count * sizeof(struct enetc_cbd);

	dma_free_coherent(dev, size, cbdr->bd_base, cbdr->bd_dma_base);
	cbdr->bd_base = NULL;
}

void enetc_setup_cbdr(struct enetc_hw *hw, struct enetc_cbdr *cbdr)
{
	/* set CBDR cache attributes */
	enetc_wr(hw, ENETC_SICAR2,
		 ENETC_SICAR_RD_COHERENT | ENETC_SICAR_WR_COHERENT);

	enetc_wr(hw, ENETC_SICBDRBAR0, lower_32_bits(cbdr->bd_dma_base));
	enetc_wr(hw, ENETC_SICBDRBAR1, upper_32_bits(cbdr->bd_dma_base));
	enetc_wr(hw, ENETC_SICBDRLENR, ENETC_RTBLENR_LEN(cbdr->bd_count));

	enetc_wr(hw, ENETC_SICBDRPIR, 0);
	enetc_wr(hw, ENETC_SICBDRCIR, 0);

	/* enable ring */
	enetc_wr(hw, ENETC_SICBDRMR, BIT(31));

	cbdr->pir = hw->reg + ENETC_SICBDRPIR;
	cbdr->cir = hw->reg + ENETC_SICBDRCIR;
}

void enetc_clear_cbdr(struct enetc_hw *hw)
{
	enetc_wr(hw, ENETC_SICBDRMR, 0);
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

	/* SI specific */
	si->cbd_ring.bd_count = ENETC_CBDR_DEFAULT_SIZE;
}

int enetc_alloc_si_resources(struct enetc_ndev_priv *priv)
{
	struct enetc_si *si = priv->si;
	int err;

	err = enetc_alloc_cbdr(priv->dev, &si->cbd_ring);
	if (err)
		return err;

	enetc_setup_cbdr(&si->hw, &si->cbd_ring);

	priv->cls_rules = kcalloc(si->num_fs_entries, sizeof(*priv->cls_rules),
				  GFP_KERNEL);
	if (!priv->cls_rules) {
		err = -ENOMEM;
		goto err_alloc_cls;
	}

	return 0;

err_alloc_cls:
	enetc_clear_cbdr(&si->hw);
	enetc_free_cbdr(priv->dev, &si->cbd_ring);

	return err;
}

void enetc_free_si_resources(struct enetc_ndev_priv *priv)
{
	struct enetc_si *si = priv->si;

	enetc_clear_cbdr(&si->hw);
	enetc_free_cbdr(priv->dev, &si->cbd_ring);

	kfree(priv->cls_rules);
}

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

	tbmr = ENETC_TBMR_EN;
	if (tx_ring->ndev->features & NETIF_F_HW_VLAN_CTAG_TX)
		tbmr |= ENETC_TBMR_VIH;

	/* enable ring */
	enetc_txbdr_wr(hw, idx, ENETC_TBMR, tbmr);

	tx_ring->tpir = hw->reg + ENETC_BDR(TX, idx, ENETC_TBPIR);
	tx_ring->tcir = hw->reg + ENETC_BDR(TX, idx, ENETC_TBCIR);
	tx_ring->idr = hw->reg + ENETC_SITXIDR;
}

static void enetc_setup_rxbdr(struct enetc_hw *hw, struct enetc_bdr *rx_ring)
{
	int idx = rx_ring->index;
	u32 rbmr;

	enetc_rxbdr_wr(hw, idx, ENETC_RBBAR0,
		       lower_32_bits(rx_ring->bd_dma_base));

	enetc_rxbdr_wr(hw, idx, ENETC_RBBAR1,
		       upper_32_bits(rx_ring->bd_dma_base));

	WARN_ON(!IS_ALIGNED(rx_ring->bd_count, 64)); /* multiple of 64 */
	enetc_rxbdr_wr(hw, idx, ENETC_RBLENR,
		       ENETC_RTBLENR_LEN(rx_ring->bd_count));

	enetc_rxbdr_wr(hw, idx, ENETC_RBBSR, ENETC_RXB_DMA_SIZE);

	enetc_rxbdr_wr(hw, idx, ENETC_RBPIR, 0);

	/* enable Rx ints by setting pkt thr to 1 */
	enetc_rxbdr_wr(hw, idx, ENETC_RBICR0, ENETC_RBICR0_ICEN | 0x1);

	rbmr = ENETC_RBMR_EN;

	if (rx_ring->ext_en)
		rbmr |= ENETC_RBMR_BDS;

	if (rx_ring->ndev->features & NETIF_F_HW_VLAN_CTAG_RX)
		rbmr |= ENETC_RBMR_VTE;

	rx_ring->rcir = hw->reg + ENETC_BDR(RX, idx, ENETC_RBCIR);
	rx_ring->idr = hw->reg + ENETC_SIRXIDR;

	enetc_refill_rx_ring(rx_ring, enetc_bd_unused(rx_ring));
	/* update ENETC's consumer index */
	enetc_rxbdr_wr(hw, idx, ENETC_RBCIR, rx_ring->next_to_use);

	/* enable ring */
	enetc_rxbdr_wr(hw, idx, ENETC_RBMR, rbmr);
}

static void enetc_setup_bdrs(struct enetc_ndev_priv *priv)
{
	int i;

	for (i = 0; i < priv->num_tx_rings; i++)
		enetc_setup_txbdr(&priv->si->hw, priv->tx_ring[i]);

	for (i = 0; i < priv->num_rx_rings; i++)
		enetc_setup_rxbdr(&priv->si->hw, priv->rx_ring[i]);
}

static void enetc_clear_rxbdr(struct enetc_hw *hw, struct enetc_bdr *rx_ring)
{
	int idx = rx_ring->index;

	/* disable EN bit on ring */
	enetc_rxbdr_wr(hw, idx, ENETC_RBMR, 0);
}

static void enetc_clear_txbdr(struct enetc_hw *hw, struct enetc_bdr *tx_ring)
{
	int delay = 8, timeout = 100;
	int idx = tx_ring->index;

	/* disable EN bit on ring */
	enetc_txbdr_wr(hw, idx, ENETC_TBMR, 0);

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

static void enetc_clear_bdrs(struct enetc_ndev_priv *priv)
{
	int i;

	for (i = 0; i < priv->num_tx_rings; i++)
		enetc_clear_txbdr(&priv->si->hw, priv->tx_ring[i]);

	for (i = 0; i < priv->num_rx_rings; i++)
		enetc_clear_rxbdr(&priv->si->hw, priv->rx_ring[i]);

	udelay(1);
}

static int enetc_setup_irqs(struct enetc_ndev_priv *priv)
{
	struct pci_dev *pdev = priv->si->pdev;
	int i, j, err;

	for (i = 0; i < priv->bdr_int_num; i++) {
		int irq = pci_irq_vector(pdev, ENETC_BDR_INT_BASE_IDX + i);
		struct enetc_int_vector *v = priv->int_vector[i];
		int entry = ENETC_BDR_INT_BASE_IDX + i;
		struct enetc_hw *hw = &priv->si->hw;

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
	int i;

	for (i = 0; i < priv->num_tx_rings; i++)
		enetc_txbdr_wr(&priv->si->hw, i, ENETC_TBIER, 0);

	for (i = 0; i < priv->num_rx_rings; i++)
		enetc_rxbdr_wr(&priv->si->hw, i, ENETC_RBIER, 0);
}

static int enetc_phylink_connect(struct net_device *ndev)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	struct ethtool_eee edata;
	int err;

	if (!priv->phylink)
		return 0; /* phy-less mode */

	err = phylink_of_phy_connect(priv->phylink, priv->dev->of_node, 0);
	if (err) {
		dev_err(&ndev->dev, "could not attach to PHY\n");
		return err;
	}

	/* disable EEE autoneg, until ENETC driver supports it */
	memset(&edata, 0, sizeof(struct ethtool_eee));
	phylink_ethtool_set_eee(priv->phylink, &edata);

	return 0;
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

	if (priv->phylink)
		phylink_start(priv->phylink);
	else
		netif_carrier_on(ndev);

	netif_tx_start_all_queues(ndev);
}

int enetc_open(struct net_device *ndev)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	int err;

	err = enetc_setup_irqs(priv);
	if (err)
		return err;

	err = enetc_phylink_connect(ndev);
	if (err)
		goto err_phy_connect;

	err = enetc_alloc_tx_resources(priv);
	if (err)
		goto err_alloc_tx;

	err = enetc_alloc_rx_resources(priv);
	if (err)
		goto err_alloc_rx;

	err = netif_set_real_num_tx_queues(ndev, priv->num_tx_rings);
	if (err)
		goto err_set_queues;

	err = netif_set_real_num_rx_queues(ndev, priv->num_rx_rings);
	if (err)
		goto err_set_queues;

	enetc_setup_bdrs(priv);
	enetc_start(ndev);

	return 0;

err_set_queues:
	enetc_free_rx_resources(priv);
err_alloc_rx:
	enetc_free_tx_resources(priv);
err_alloc_tx:
	if (priv->phylink)
		phylink_disconnect_phy(priv->phylink);
err_phy_connect:
	enetc_free_irqs(priv);

	return err;
}

void enetc_stop(struct net_device *ndev)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	int i;

	netif_tx_stop_all_queues(ndev);

	for (i = 0; i < priv->bdr_int_num; i++) {
		int irq = pci_irq_vector(priv->si->pdev,
					 ENETC_BDR_INT_BASE_IDX + i);

		disable_irq(irq);
		napi_synchronize(&priv->int_vector[i]->napi);
		napi_disable(&priv->int_vector[i]->napi);
	}

	if (priv->phylink)
		phylink_stop(priv->phylink);
	else
		netif_carrier_off(ndev);

	enetc_clear_interrupts(priv);
}

int enetc_close(struct net_device *ndev)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);

	enetc_stop(ndev);
	enetc_clear_bdrs(priv);

	if (priv->phylink)
		phylink_disconnect_phy(priv->phylink);
	enetc_free_rxtx_rings(priv);
	enetc_free_rx_resources(priv);
	enetc_free_tx_resources(priv);
	enetc_free_irqs(priv);

	return 0;
}

static int enetc_setup_tc_mqprio(struct net_device *ndev, void *type_data)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	struct tc_mqprio_qopt *mqprio = type_data;
	struct enetc_bdr *tx_ring;
	u8 num_tc;
	int i;

	mqprio->hw = TC_MQPRIO_HW_OFFLOAD_TCS;
	num_tc = mqprio->num_tc;

	if (!num_tc) {
		netdev_reset_tc(ndev);
		netif_set_real_num_tx_queues(ndev, priv->num_tx_rings);

		/* Reset all ring priorities to 0 */
		for (i = 0; i < priv->num_tx_rings; i++) {
			tx_ring = priv->tx_ring[i];
			enetc_set_bdr_prio(&priv->si->hw, tx_ring->index, 0);
		}

		return 0;
	}

	/* Check if we have enough BD rings available to accommodate all TCs */
	if (num_tc > priv->num_tx_rings) {
		netdev_err(ndev, "Max %d traffic classes supported\n",
			   priv->num_tx_rings);
		return -EINVAL;
	}

	/* For the moment, we use only one BD ring per TC.
	 *
	 * Configure num_tc BD rings with increasing priorities.
	 */
	for (i = 0; i < num_tc; i++) {
		tx_ring = priv->tx_ring[i];
		enetc_set_bdr_prio(&priv->si->hw, tx_ring->index, i);
	}

	/* Reset the number of netdev queues based on the TC count */
	netif_set_real_num_tx_queues(ndev, num_tc);

	netdev_set_num_tc(ndev, num_tc);

	/* Each TC is associated with one netdev queue */
	for (i = 0; i < num_tc; i++)
		netdev_set_tc_queue(ndev, i, 1, i);

	return 0;
}

int enetc_setup_tc(struct net_device *ndev, enum tc_setup_type type,
		   void *type_data)
{
	switch (type) {
	case TC_SETUP_QDISC_MQPRIO:
		return enetc_setup_tc_mqprio(ndev, type_data);
	case TC_SETUP_QDISC_TAPRIO:
		return enetc_setup_tc_taprio(ndev, type_data);
	case TC_SETUP_QDISC_CBS:
		return enetc_setup_tc_cbs(ndev, type_data);
	case TC_SETUP_QDISC_ETF:
		return enetc_setup_tc_txtime(ndev, type_data);
	case TC_SETUP_BLOCK:
		return enetc_setup_tc_psfp(ndev, type_data);
	default:
		return -EOPNOTSUPP;
	}
}

struct net_device_stats *enetc_get_stats(struct net_device *ndev)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	struct net_device_stats *stats = &ndev->stats;
	unsigned long packets = 0, bytes = 0;
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
	}

	stats->tx_packets = packets;
	stats->tx_bytes = bytes;

	return stats;
}

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
	int i;

	for (i = 0; i < priv->num_rx_rings; i++)
		enetc_bdr_enable_rxvlan(&priv->si->hw, i, en);
}

static void enetc_enable_txvlan(struct net_device *ndev, bool en)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	int i;

	for (i = 0; i < priv->num_tx_rings; i++)
		enetc_bdr_enable_txvlan(&priv->si->hw, i, en);
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

#ifdef CONFIG_FSL_ENETC_PTP_CLOCK
static int enetc_hwtstamp_set(struct net_device *ndev, struct ifreq *ifr)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	struct hwtstamp_config config;
	int ao;

	if (copy_from_user(&config, ifr->ifr_data, sizeof(config)))
		return -EFAULT;

	switch (config.tx_type) {
	case HWTSTAMP_TX_OFF:
		priv->active_offloads &= ~ENETC_F_TX_TSTAMP;
		break;
	case HWTSTAMP_TX_ON:
		priv->active_offloads |= ENETC_F_TX_TSTAMP;
		break;
	default:
		return -ERANGE;
	}

	ao = priv->active_offloads;
	switch (config.rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		priv->active_offloads &= ~ENETC_F_RX_TSTAMP;
		break;
	default:
		priv->active_offloads |= ENETC_F_RX_TSTAMP;
		config.rx_filter = HWTSTAMP_FILTER_ALL;
	}

	if (netif_running(ndev) && ao != priv->active_offloads) {
		enetc_close(ndev);
		enetc_open(ndev);
	}

	return copy_to_user(ifr->ifr_data, &config, sizeof(config)) ?
	       -EFAULT : 0;
}

static int enetc_hwtstamp_get(struct net_device *ndev, struct ifreq *ifr)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	struct hwtstamp_config config;

	config.flags = 0;

	if (priv->active_offloads & ENETC_F_TX_TSTAMP)
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

int enetc_alloc_msix(struct enetc_ndev_priv *priv)
{
	struct pci_dev *pdev = priv->si->pdev;
	int v_tx_rings;
	int i, n, err, nvec;

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

		/* init defaults for adaptive IC */
		if (priv->ic_mode & ENETC_IC_RX_ADAPTIVE) {
			v->rx_ictt = 0x1;
			v->rx_dim_en = true;
		}
		INIT_WORK(&v->rx_dim.work, enetc_rx_dim_work);
		netif_napi_add(priv->ndev, &v->napi, enetc_poll,
			       NAPI_POLL_WEIGHT);
		v->count_tx_rings = v_tx_rings;

		for (j = 0; j < v_tx_rings; j++) {
			int idx;

			/* default tx ring mapping policy */
			if (priv->bdr_int_num == ENETC_MAX_BDR_INT)
				idx = 2 * j + i; /* 2 CPUs */
			else
				idx = j + i * v_tx_rings; /* default */

			__set_bit(idx, &v->tx_rings_map);
			bdr = &v->tx_ring[j];
			bdr->index = idx;
			bdr->ndev = priv->ndev;
			bdr->dev = priv->dev;
			bdr->bd_count = priv->tx_bd_count;
			priv->tx_ring[idx] = bdr;
		}

		bdr = &v->rx_ring;
		bdr->index = i;
		bdr->ndev = priv->ndev;
		bdr->dev = priv->dev;
		bdr->bd_count = priv->rx_bd_count;
		priv->rx_ring[i] = bdr;
	}

	return 0;

fail:
	while (i--) {
		netif_napi_del(&priv->int_vector[i]->napi);
		cancel_work_sync(&priv->int_vector[i]->rx_dim.work);
		kfree(priv->int_vector[i]);
	}

	pci_free_irq_vectors(pdev);

	return err;
}

void enetc_free_msix(struct enetc_ndev_priv *priv)
{
	int i;

	for (i = 0; i < priv->bdr_int_num; i++) {
		struct enetc_int_vector *v = priv->int_vector[i];

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

static void enetc_kfree_si(struct enetc_si *si)
{
	char *p = (char *)si - si->pad;

	kfree(p);
}

static void enetc_detect_errata(struct enetc_si *si)
{
	if (si->pdev->revision == ENETC_REV1)
		si->errata = ENETC_ERR_TXCSUM | ENETC_ERR_VLAN_ISOL |
			     ENETC_ERR_UCMCSWP;
}

int enetc_pci_probe(struct pci_dev *pdev, const char *name, int sizeof_priv)
{
	struct enetc_si *si, *p;
	struct enetc_hw *hw;
	size_t alloc_size;
	int err, len;

	pcie_flr(pdev);
	err = pci_enable_device_mem(pdev);
	if (err) {
		dev_err(&pdev->dev, "device enable failed\n");
		return err;
	}

	/* set up for high or low dma */
	err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (err) {
		err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
		if (err) {
			dev_err(&pdev->dev,
				"DMA configuration failed: 0x%x\n", err);
			goto err_dma;
		}
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

void enetc_pci_remove(struct pci_dev *pdev)
{
	struct enetc_si *si = pci_get_drvdata(pdev);
	struct enetc_hw *hw = &si->hw;

	iounmap(hw->reg);
	enetc_kfree_si(si);
	pci_release_mem_regions(pdev);
	pci_disable_device(pdev);
}
