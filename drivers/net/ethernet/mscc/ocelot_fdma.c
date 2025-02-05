// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/*
 * Microsemi SoCs FDMA driver
 *
 * Copyright (c) 2021 Microchip
 *
 * Page recycling code is mostly taken from gianfar driver.
 */

#include <linux/align.h>
#include <linux/bitops.h>
#include <linux/dmapool.h>
#include <linux/dsa/ocelot.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>

#include "ocelot_fdma.h"
#include "ocelot_qs.h"

DEFINE_STATIC_KEY_FALSE(ocelot_fdma_enabled);

static void ocelot_fdma_writel(struct ocelot *ocelot, u32 reg, u32 data)
{
	regmap_write(ocelot->targets[FDMA], reg, data);
}

static u32 ocelot_fdma_readl(struct ocelot *ocelot, u32 reg)
{
	u32 retval;

	regmap_read(ocelot->targets[FDMA], reg, &retval);

	return retval;
}

static dma_addr_t ocelot_fdma_idx_dma(dma_addr_t base, u16 idx)
{
	return base + idx * sizeof(struct ocelot_fdma_dcb);
}

static u16 ocelot_fdma_dma_idx(dma_addr_t base, dma_addr_t dma)
{
	return (dma - base) / sizeof(struct ocelot_fdma_dcb);
}

static u16 ocelot_fdma_idx_next(u16 idx, u16 ring_sz)
{
	return unlikely(idx == ring_sz - 1) ? 0 : idx + 1;
}

static u16 ocelot_fdma_idx_prev(u16 idx, u16 ring_sz)
{
	return unlikely(idx == 0) ? ring_sz - 1 : idx - 1;
}

static int ocelot_fdma_rx_ring_free(struct ocelot_fdma *fdma)
{
	struct ocelot_fdma_rx_ring *rx_ring = &fdma->rx_ring;

	if (rx_ring->next_to_use >= rx_ring->next_to_clean)
		return OCELOT_FDMA_RX_RING_SIZE -
		       (rx_ring->next_to_use - rx_ring->next_to_clean) - 1;
	else
		return rx_ring->next_to_clean - rx_ring->next_to_use - 1;
}

static int ocelot_fdma_tx_ring_free(struct ocelot_fdma *fdma)
{
	struct ocelot_fdma_tx_ring *tx_ring = &fdma->tx_ring;

	if (tx_ring->next_to_use >= tx_ring->next_to_clean)
		return OCELOT_FDMA_TX_RING_SIZE -
		       (tx_ring->next_to_use - tx_ring->next_to_clean) - 1;
	else
		return tx_ring->next_to_clean - tx_ring->next_to_use - 1;
}

static bool ocelot_fdma_tx_ring_empty(struct ocelot_fdma *fdma)
{
	struct ocelot_fdma_tx_ring *tx_ring = &fdma->tx_ring;

	return tx_ring->next_to_clean == tx_ring->next_to_use;
}

static void ocelot_fdma_activate_chan(struct ocelot *ocelot, dma_addr_t dma,
				      int chan)
{
	ocelot_fdma_writel(ocelot, MSCC_FDMA_DCB_LLP(chan), dma);
	/* Barrier to force memory writes to DCB to be completed before starting
	 * the channel.
	 */
	wmb();
	ocelot_fdma_writel(ocelot, MSCC_FDMA_CH_ACTIVATE, BIT(chan));
}

static u32 ocelot_fdma_read_ch_safe(struct ocelot *ocelot)
{
	return ocelot_fdma_readl(ocelot, MSCC_FDMA_CH_SAFE);
}

static int ocelot_fdma_wait_chan_safe(struct ocelot *ocelot, int chan)
{
	u32 safe;

	return readx_poll_timeout_atomic(ocelot_fdma_read_ch_safe, ocelot, safe,
					 safe & BIT(chan), 0,
					 OCELOT_FDMA_CH_SAFE_TIMEOUT_US);
}

static void ocelot_fdma_dcb_set_data(struct ocelot_fdma_dcb *dcb,
				     dma_addr_t dma_addr,
				     size_t size)
{
	u32 offset = dma_addr & 0x3;

	dcb->llp = 0;
	dcb->datap = ALIGN_DOWN(dma_addr, 4);
	dcb->datal = ALIGN_DOWN(size, 4);
	dcb->stat = MSCC_FDMA_DCB_STAT_BLOCKO(offset);
}

static bool ocelot_fdma_rx_alloc_page(struct ocelot *ocelot,
				      struct ocelot_fdma_rx_buf *rxb)
{
	dma_addr_t mapping;
	struct page *page;

	page = dev_alloc_page();
	if (unlikely(!page))
		return false;

	mapping = dma_map_page(ocelot->dev, page, 0, PAGE_SIZE,
			       DMA_FROM_DEVICE);
	if (unlikely(dma_mapping_error(ocelot->dev, mapping))) {
		__free_page(page);
		return false;
	}

	rxb->page = page;
	rxb->page_offset = 0;
	rxb->dma_addr = mapping;

	return true;
}

static int ocelot_fdma_alloc_rx_buffs(struct ocelot *ocelot, u16 alloc_cnt)
{
	struct ocelot_fdma *fdma = ocelot->fdma;
	struct ocelot_fdma_rx_ring *rx_ring;
	struct ocelot_fdma_rx_buf *rxb;
	struct ocelot_fdma_dcb *dcb;
	dma_addr_t dma_addr;
	int ret = 0;
	u16 idx;

	rx_ring = &fdma->rx_ring;
	idx = rx_ring->next_to_use;

	while (alloc_cnt--) {
		rxb = &rx_ring->bufs[idx];
		/* try reuse page */
		if (unlikely(!rxb->page)) {
			if (unlikely(!ocelot_fdma_rx_alloc_page(ocelot, rxb))) {
				dev_err_ratelimited(ocelot->dev,
						    "Failed to allocate rx\n");
				ret = -ENOMEM;
				break;
			}
		}

		dcb = &rx_ring->dcbs[idx];
		dma_addr = rxb->dma_addr + rxb->page_offset;
		ocelot_fdma_dcb_set_data(dcb, dma_addr, OCELOT_FDMA_RXB_SIZE);

		idx = ocelot_fdma_idx_next(idx, OCELOT_FDMA_RX_RING_SIZE);
		/* Chain the DCB to the next one */
		dcb->llp = ocelot_fdma_idx_dma(rx_ring->dcbs_dma, idx);
	}

	rx_ring->next_to_use = idx;
	rx_ring->next_to_alloc = idx;

	return ret;
}

static bool ocelot_fdma_tx_dcb_set_skb(struct ocelot *ocelot,
				       struct ocelot_fdma_tx_buf *tx_buf,
				       struct ocelot_fdma_dcb *dcb,
				       struct sk_buff *skb)
{
	dma_addr_t mapping;

	mapping = dma_map_single(ocelot->dev, skb->data, skb->len,
				 DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(ocelot->dev, mapping)))
		return false;

	dma_unmap_addr_set(tx_buf, dma_addr, mapping);

	ocelot_fdma_dcb_set_data(dcb, mapping, OCELOT_FDMA_RX_SIZE);
	tx_buf->skb = skb;
	dcb->stat |= MSCC_FDMA_DCB_STAT_BLOCKL(skb->len);
	dcb->stat |= MSCC_FDMA_DCB_STAT_SOF | MSCC_FDMA_DCB_STAT_EOF;

	return true;
}

static bool ocelot_fdma_check_stop_rx(struct ocelot *ocelot)
{
	u32 llp;

	/* Check if the FDMA hits the DCB with LLP == NULL */
	llp = ocelot_fdma_readl(ocelot, MSCC_FDMA_DCB_LLP(MSCC_FDMA_XTR_CHAN));
	if (unlikely(llp))
		return false;

	ocelot_fdma_writel(ocelot, MSCC_FDMA_CH_DISABLE,
			   BIT(MSCC_FDMA_XTR_CHAN));

	return true;
}

static void ocelot_fdma_rx_set_llp(struct ocelot_fdma_rx_ring *rx_ring)
{
	struct ocelot_fdma_dcb *dcb;
	unsigned int idx;

	idx = ocelot_fdma_idx_prev(rx_ring->next_to_use,
				   OCELOT_FDMA_RX_RING_SIZE);
	dcb = &rx_ring->dcbs[idx];
	dcb->llp = 0;
}

static void ocelot_fdma_rx_restart(struct ocelot *ocelot)
{
	struct ocelot_fdma *fdma = ocelot->fdma;
	struct ocelot_fdma_rx_ring *rx_ring;
	const u8 chan = MSCC_FDMA_XTR_CHAN;
	dma_addr_t new_llp, dma_base;
	unsigned int idx;
	u32 llp_prev;
	int ret;

	rx_ring = &fdma->rx_ring;
	ret = ocelot_fdma_wait_chan_safe(ocelot, chan);
	if (ret) {
		dev_err_ratelimited(ocelot->dev,
				    "Unable to stop RX channel\n");
		return;
	}

	ocelot_fdma_rx_set_llp(rx_ring);

	/* FDMA stopped on the last DCB that contained a NULL LLP, since
	 * we processed some DCBs in RX, there is free space, and  we must set
	 * DCB_LLP to point to the next DCB
	 */
	llp_prev = ocelot_fdma_readl(ocelot, MSCC_FDMA_DCB_LLP_PREV(chan));
	dma_base = rx_ring->dcbs_dma;

	/* Get the next DMA addr located after LLP == NULL DCB */
	idx = ocelot_fdma_dma_idx(dma_base, llp_prev);
	idx = ocelot_fdma_idx_next(idx, OCELOT_FDMA_RX_RING_SIZE);
	new_llp = ocelot_fdma_idx_dma(dma_base, idx);

	/* Finally reactivate the channel */
	ocelot_fdma_activate_chan(ocelot, new_llp, chan);
}

static bool ocelot_fdma_add_rx_frag(struct ocelot_fdma_rx_buf *rxb, u32 stat,
				    struct sk_buff *skb, bool first)
{
	int size = MSCC_FDMA_DCB_STAT_BLOCKL(stat);
	struct page *page = rxb->page;

	if (likely(first)) {
		skb_put(skb, size);
	} else {
		skb_add_rx_frag(skb, skb_shinfo(skb)->nr_frags, page,
				rxb->page_offset, size, OCELOT_FDMA_RX_SIZE);
	}

	/* Try to reuse page */
	if (unlikely(page_ref_count(page) != 1 || page_is_pfmemalloc(page)))
		return false;

	/* Change offset to the other half */
	rxb->page_offset ^= OCELOT_FDMA_RX_SIZE;

	page_ref_inc(page);

	return true;
}

static void ocelot_fdma_reuse_rx_page(struct ocelot *ocelot,
				      struct ocelot_fdma_rx_buf *old_rxb)
{
	struct ocelot_fdma_rx_ring *rx_ring = &ocelot->fdma->rx_ring;
	struct ocelot_fdma_rx_buf *new_rxb;

	new_rxb = &rx_ring->bufs[rx_ring->next_to_alloc];
	rx_ring->next_to_alloc = ocelot_fdma_idx_next(rx_ring->next_to_alloc,
						      OCELOT_FDMA_RX_RING_SIZE);

	/* Copy page reference */
	*new_rxb = *old_rxb;

	/* Sync for use by the device */
	dma_sync_single_range_for_device(ocelot->dev, old_rxb->dma_addr,
					 old_rxb->page_offset,
					 OCELOT_FDMA_RX_SIZE, DMA_FROM_DEVICE);
}

static struct sk_buff *ocelot_fdma_get_skb(struct ocelot *ocelot, u32 stat,
					   struct ocelot_fdma_rx_buf *rxb,
					   struct sk_buff *skb)
{
	bool first = false;

	/* Allocate skb head and data */
	if (likely(!skb)) {
		void *buff_addr = page_address(rxb->page) +
				  rxb->page_offset;

		skb = build_skb(buff_addr, OCELOT_FDMA_SKBFRAG_SIZE);
		if (unlikely(!skb)) {
			dev_err_ratelimited(ocelot->dev,
					    "build_skb failed !\n");
			return NULL;
		}
		first = true;
	}

	dma_sync_single_range_for_cpu(ocelot->dev, rxb->dma_addr,
				      rxb->page_offset, OCELOT_FDMA_RX_SIZE,
				      DMA_FROM_DEVICE);

	if (ocelot_fdma_add_rx_frag(rxb, stat, skb, first)) {
		/* Reuse the free half of the page for the next_to_alloc DCB*/
		ocelot_fdma_reuse_rx_page(ocelot, rxb);
	} else {
		/* page cannot be reused, unmap it */
		dma_unmap_page(ocelot->dev, rxb->dma_addr, PAGE_SIZE,
			       DMA_FROM_DEVICE);
	}

	/* clear rx buff content */
	rxb->page = NULL;

	return skb;
}

static bool ocelot_fdma_receive_skb(struct ocelot *ocelot, struct sk_buff *skb)
{
	struct net_device *ndev;
	void *xfh = skb->data;
	u64 timestamp;
	u64 src_port;

	skb_pull(skb, OCELOT_TAG_LEN);

	ocelot_xfh_get_src_port(xfh, &src_port);
	if (unlikely(src_port >= ocelot->num_phys_ports))
		return false;

	ndev = ocelot_port_to_netdev(ocelot, src_port);
	if (unlikely(!ndev))
		return false;

	if (pskb_trim(skb, skb->len - ETH_FCS_LEN))
		return false;

	skb->dev = ndev;
	skb->protocol = eth_type_trans(skb, skb->dev);
	skb->dev->stats.rx_bytes += skb->len;
	skb->dev->stats.rx_packets++;

	if (ocelot->ptp) {
		ocelot_xfh_get_rew_val(xfh, &timestamp);
		ocelot_ptp_rx_timestamp(ocelot, skb, timestamp);
	}

	if (likely(!skb_defer_rx_timestamp(skb)))
		netif_receive_skb(skb);

	return true;
}

static int ocelot_fdma_rx_get(struct ocelot *ocelot, int budget)
{
	struct ocelot_fdma *fdma = ocelot->fdma;
	struct ocelot_fdma_rx_ring *rx_ring;
	struct ocelot_fdma_rx_buf *rxb;
	struct ocelot_fdma_dcb *dcb;
	struct sk_buff *skb;
	int work_done = 0;
	int cleaned_cnt;
	u32 stat;
	u16 idx;

	cleaned_cnt = ocelot_fdma_rx_ring_free(fdma);
	rx_ring = &fdma->rx_ring;
	skb = rx_ring->skb;

	while (budget--) {
		idx = rx_ring->next_to_clean;
		dcb = &rx_ring->dcbs[idx];
		stat = dcb->stat;
		if (MSCC_FDMA_DCB_STAT_BLOCKL(stat) == 0)
			break;

		/* New packet is a start of frame but we already got a skb set,
		 * we probably lost an EOF packet, free skb
		 */
		if (unlikely(skb && (stat & MSCC_FDMA_DCB_STAT_SOF))) {
			dev_kfree_skb(skb);
			skb = NULL;
		}

		rxb = &rx_ring->bufs[idx];
		/* Fetch next to clean buffer from the rx_ring */
		skb = ocelot_fdma_get_skb(ocelot, stat, rxb, skb);
		if (unlikely(!skb))
			break;

		work_done++;
		cleaned_cnt++;

		idx = ocelot_fdma_idx_next(idx, OCELOT_FDMA_RX_RING_SIZE);
		rx_ring->next_to_clean = idx;

		if (unlikely(stat & MSCC_FDMA_DCB_STAT_ABORT ||
			     stat & MSCC_FDMA_DCB_STAT_PD)) {
			dev_err_ratelimited(ocelot->dev,
					    "DCB aborted or pruned\n");
			dev_kfree_skb(skb);
			skb = NULL;
			continue;
		}

		/* We still need to process the other fragment of the packet
		 * before delivering it to the network stack
		 */
		if (!(stat & MSCC_FDMA_DCB_STAT_EOF))
			continue;

		if (unlikely(!ocelot_fdma_receive_skb(ocelot, skb)))
			dev_kfree_skb(skb);

		skb = NULL;
	}

	rx_ring->skb = skb;

	if (cleaned_cnt)
		ocelot_fdma_alloc_rx_buffs(ocelot, cleaned_cnt);

	return work_done;
}

static void ocelot_fdma_wakeup_netdev(struct ocelot *ocelot)
{
	struct ocelot_port_private *priv;
	struct ocelot_port *ocelot_port;
	struct net_device *dev;
	int port;

	for (port = 0; port < ocelot->num_phys_ports; port++) {
		ocelot_port = ocelot->ports[port];
		if (!ocelot_port)
			continue;
		priv = container_of(ocelot_port, struct ocelot_port_private,
				    port);
		dev = priv->dev;

		if (unlikely(netif_queue_stopped(dev)))
			netif_wake_queue(dev);
	}
}

static void ocelot_fdma_tx_cleanup(struct ocelot *ocelot, int budget)
{
	struct ocelot_fdma *fdma = ocelot->fdma;
	struct ocelot_fdma_tx_ring *tx_ring;
	struct ocelot_fdma_tx_buf *buf;
	unsigned int new_null_llp_idx;
	struct ocelot_fdma_dcb *dcb;
	bool end_of_list = false;
	struct sk_buff *skb;
	dma_addr_t dma;
	u32 dcb_llp;
	u16 ntc;
	int ret;

	tx_ring = &fdma->tx_ring;

	/* Purge the TX packets that have been sent up to the NULL llp or the
	 * end of done list.
	 */
	while (!ocelot_fdma_tx_ring_empty(fdma)) {
		ntc = tx_ring->next_to_clean;
		dcb = &tx_ring->dcbs[ntc];
		if (!(dcb->stat & MSCC_FDMA_DCB_STAT_PD))
			break;

		buf = &tx_ring->bufs[ntc];
		skb = buf->skb;
		dma_unmap_single(ocelot->dev, dma_unmap_addr(buf, dma_addr),
				 skb->len, DMA_TO_DEVICE);
		napi_consume_skb(skb, budget);
		dcb_llp = dcb->llp;

		/* Only update after accessing all dcb fields */
		tx_ring->next_to_clean = ocelot_fdma_idx_next(ntc,
							      OCELOT_FDMA_TX_RING_SIZE);

		/* If we hit the NULL LLP, stop, we might need to reload FDMA */
		if (dcb_llp == 0) {
			end_of_list = true;
			break;
		}
	}

	/* No need to try to wake if there were no TX cleaned_cnt up. */
	if (ocelot_fdma_tx_ring_free(fdma))
		ocelot_fdma_wakeup_netdev(ocelot);

	/* If there is still some DCBs to be processed by the FDMA or if the
	 * pending list is empty, there is no need to restart the FDMA.
	 */
	if (!end_of_list || ocelot_fdma_tx_ring_empty(fdma))
		return;

	ret = ocelot_fdma_wait_chan_safe(ocelot, MSCC_FDMA_INJ_CHAN);
	if (ret) {
		dev_warn(ocelot->dev,
			 "Failed to wait for TX channel to stop\n");
		return;
	}

	/* Set NULL LLP to be the last DCB used */
	new_null_llp_idx = ocelot_fdma_idx_prev(tx_ring->next_to_use,
						OCELOT_FDMA_TX_RING_SIZE);
	dcb = &tx_ring->dcbs[new_null_llp_idx];
	dcb->llp = 0;

	dma = ocelot_fdma_idx_dma(tx_ring->dcbs_dma, tx_ring->next_to_clean);
	ocelot_fdma_activate_chan(ocelot, dma, MSCC_FDMA_INJ_CHAN);
}

static int ocelot_fdma_napi_poll(struct napi_struct *napi, int budget)
{
	struct ocelot_fdma *fdma = container_of(napi, struct ocelot_fdma, napi);
	struct ocelot *ocelot = fdma->ocelot;
	int work_done = 0;
	bool rx_stopped;

	ocelot_fdma_tx_cleanup(ocelot, budget);

	rx_stopped = ocelot_fdma_check_stop_rx(ocelot);

	work_done = ocelot_fdma_rx_get(ocelot, budget);

	if (rx_stopped)
		ocelot_fdma_rx_restart(ocelot);

	if (work_done < budget) {
		napi_complete_done(&fdma->napi, work_done);
		ocelot_fdma_writel(ocelot, MSCC_FDMA_INTR_ENA,
				   BIT(MSCC_FDMA_INJ_CHAN) |
				   BIT(MSCC_FDMA_XTR_CHAN));
	}

	return work_done;
}

static irqreturn_t ocelot_fdma_interrupt(int irq, void *dev_id)
{
	u32 ident, llp, frm, err, err_code;
	struct ocelot *ocelot = dev_id;

	ident = ocelot_fdma_readl(ocelot, MSCC_FDMA_INTR_IDENT);
	frm = ocelot_fdma_readl(ocelot, MSCC_FDMA_INTR_FRM);
	llp = ocelot_fdma_readl(ocelot, MSCC_FDMA_INTR_LLP);

	ocelot_fdma_writel(ocelot, MSCC_FDMA_INTR_LLP, llp & ident);
	ocelot_fdma_writel(ocelot, MSCC_FDMA_INTR_FRM, frm & ident);
	if (frm || llp) {
		ocelot_fdma_writel(ocelot, MSCC_FDMA_INTR_ENA, 0);
		napi_schedule(&ocelot->fdma->napi);
	}

	err = ocelot_fdma_readl(ocelot, MSCC_FDMA_EVT_ERR);
	if (unlikely(err)) {
		err_code = ocelot_fdma_readl(ocelot, MSCC_FDMA_EVT_ERR_CODE);
		dev_err_ratelimited(ocelot->dev,
				    "Error ! chans mask: %#x, code: %#x\n",
				    err, err_code);

		ocelot_fdma_writel(ocelot, MSCC_FDMA_EVT_ERR, err);
		ocelot_fdma_writel(ocelot, MSCC_FDMA_EVT_ERR_CODE, err_code);
	}

	return IRQ_HANDLED;
}

static void ocelot_fdma_send_skb(struct ocelot *ocelot,
				 struct ocelot_fdma *fdma, struct sk_buff *skb)
{
	struct ocelot_fdma_tx_ring *tx_ring = &fdma->tx_ring;
	struct ocelot_fdma_tx_buf *tx_buf;
	struct ocelot_fdma_dcb *dcb;
	dma_addr_t dma;
	u16 next_idx;

	dcb = &tx_ring->dcbs[tx_ring->next_to_use];
	tx_buf = &tx_ring->bufs[tx_ring->next_to_use];
	if (!ocelot_fdma_tx_dcb_set_skb(ocelot, tx_buf, dcb, skb)) {
		dev_kfree_skb_any(skb);
		return;
	}

	next_idx = ocelot_fdma_idx_next(tx_ring->next_to_use,
					OCELOT_FDMA_TX_RING_SIZE);
	skb_tx_timestamp(skb);

	/* If the FDMA TX chan is empty, then enqueue the DCB directly */
	if (ocelot_fdma_tx_ring_empty(fdma)) {
		dma = ocelot_fdma_idx_dma(tx_ring->dcbs_dma,
					  tx_ring->next_to_use);
		ocelot_fdma_activate_chan(ocelot, dma, MSCC_FDMA_INJ_CHAN);
	} else {
		/* Chain the DCBs */
		dcb->llp = ocelot_fdma_idx_dma(tx_ring->dcbs_dma, next_idx);
	}

	tx_ring->next_to_use = next_idx;
}

static int ocelot_fdma_prepare_skb(struct ocelot *ocelot, int port, u32 rew_op,
				   struct sk_buff *skb, struct net_device *dev)
{
	int needed_headroom = max_t(int, OCELOT_TAG_LEN - skb_headroom(skb), 0);
	int needed_tailroom = max_t(int, ETH_FCS_LEN - skb_tailroom(skb), 0);
	void *ifh;
	int err;

	if (unlikely(needed_headroom || needed_tailroom ||
		     skb_header_cloned(skb))) {
		err = pskb_expand_head(skb, needed_headroom, needed_tailroom,
				       GFP_ATOMIC);
		if (unlikely(err)) {
			dev_kfree_skb_any(skb);
			return 1;
		}
	}

	err = skb_linearize(skb);
	if (err) {
		net_err_ratelimited("%s: skb_linearize error (%d)!\n",
				    dev->name, err);
		dev_kfree_skb_any(skb);
		return 1;
	}

	ifh = skb_push(skb, OCELOT_TAG_LEN);
	skb_put(skb, ETH_FCS_LEN);
	ocelot_ifh_set_basic(ifh, ocelot, port, rew_op, skb);

	return 0;
}

int ocelot_fdma_inject_frame(struct ocelot *ocelot, int port, u32 rew_op,
			     struct sk_buff *skb, struct net_device *dev)
{
	struct ocelot_fdma *fdma = ocelot->fdma;
	int ret = NETDEV_TX_OK;

	spin_lock(&fdma->tx_ring.xmit_lock);

	if (ocelot_fdma_tx_ring_free(fdma) == 0) {
		netif_stop_queue(dev);
		ret = NETDEV_TX_BUSY;
		goto out;
	}

	if (ocelot_fdma_prepare_skb(ocelot, port, rew_op, skb, dev))
		goto out;

	ocelot_fdma_send_skb(ocelot, fdma, skb);

out:
	spin_unlock(&fdma->tx_ring.xmit_lock);

	return ret;
}

static void ocelot_fdma_free_rx_ring(struct ocelot *ocelot)
{
	struct ocelot_fdma *fdma = ocelot->fdma;
	struct ocelot_fdma_rx_ring *rx_ring;
	struct ocelot_fdma_rx_buf *rxb;
	u16 idx;

	rx_ring = &fdma->rx_ring;
	idx = rx_ring->next_to_clean;

	/* Free the pages held in the RX ring */
	while (idx != rx_ring->next_to_use) {
		rxb = &rx_ring->bufs[idx];
		dma_unmap_page(ocelot->dev, rxb->dma_addr, PAGE_SIZE,
			       DMA_FROM_DEVICE);
		__free_page(rxb->page);
		idx = ocelot_fdma_idx_next(idx, OCELOT_FDMA_RX_RING_SIZE);
	}

	if (fdma->rx_ring.skb)
		dev_kfree_skb_any(fdma->rx_ring.skb);
}

static void ocelot_fdma_free_tx_ring(struct ocelot *ocelot)
{
	struct ocelot_fdma *fdma = ocelot->fdma;
	struct ocelot_fdma_tx_ring *tx_ring;
	struct ocelot_fdma_tx_buf *txb;
	struct sk_buff *skb;
	u16 idx;

	tx_ring = &fdma->tx_ring;
	idx = tx_ring->next_to_clean;

	while (idx != tx_ring->next_to_use) {
		txb = &tx_ring->bufs[idx];
		skb = txb->skb;
		dma_unmap_single(ocelot->dev, dma_unmap_addr(txb, dma_addr),
				 skb->len, DMA_TO_DEVICE);
		dev_kfree_skb_any(skb);
		idx = ocelot_fdma_idx_next(idx, OCELOT_FDMA_TX_RING_SIZE);
	}
}

static int ocelot_fdma_rings_alloc(struct ocelot *ocelot)
{
	struct ocelot_fdma *fdma = ocelot->fdma;
	struct ocelot_fdma_dcb *dcbs;
	unsigned int adjust;
	dma_addr_t dcbs_dma;
	int ret;

	/* Create a pool of consistent memory blocks for hardware descriptors */
	fdma->dcbs_base = dmam_alloc_coherent(ocelot->dev,
					      OCELOT_DCBS_HW_ALLOC_SIZE,
					      &fdma->dcbs_dma_base, GFP_KERNEL);
	if (!fdma->dcbs_base)
		return -ENOMEM;

	/* DCBs must be aligned on a 32bit boundary */
	dcbs = fdma->dcbs_base;
	dcbs_dma = fdma->dcbs_dma_base;
	if (!IS_ALIGNED(dcbs_dma, 4)) {
		adjust = dcbs_dma & 0x3;
		dcbs_dma = ALIGN(dcbs_dma, 4);
		dcbs = (void *)dcbs + adjust;
	}

	/* TX queue */
	fdma->tx_ring.dcbs = dcbs;
	fdma->tx_ring.dcbs_dma = dcbs_dma;
	spin_lock_init(&fdma->tx_ring.xmit_lock);

	/* RX queue */
	fdma->rx_ring.dcbs = dcbs + OCELOT_FDMA_TX_RING_SIZE;
	fdma->rx_ring.dcbs_dma = dcbs_dma + OCELOT_FDMA_TX_DCB_SIZE;
	ret = ocelot_fdma_alloc_rx_buffs(ocelot,
					 ocelot_fdma_tx_ring_free(fdma));
	if (ret) {
		ocelot_fdma_free_rx_ring(ocelot);
		return ret;
	}

	/* Set the last DCB LLP as NULL, this is normally done when restarting
	 * the RX chan, but this is for the first run
	 */
	ocelot_fdma_rx_set_llp(&fdma->rx_ring);

	return 0;
}

void ocelot_fdma_netdev_init(struct ocelot *ocelot, struct net_device *dev)
{
	struct ocelot_fdma *fdma = ocelot->fdma;

	dev->needed_headroom = OCELOT_TAG_LEN;
	dev->needed_tailroom = ETH_FCS_LEN;

	if (fdma->ndev)
		return;

	fdma->ndev = dev;
	netif_napi_add_weight(dev, &fdma->napi, ocelot_fdma_napi_poll,
			      OCELOT_FDMA_WEIGHT);
}

void ocelot_fdma_netdev_deinit(struct ocelot *ocelot, struct net_device *dev)
{
	struct ocelot_fdma *fdma = ocelot->fdma;

	if (fdma->ndev == dev) {
		netif_napi_del(&fdma->napi);
		fdma->ndev = NULL;
	}
}

void ocelot_fdma_init(struct platform_device *pdev, struct ocelot *ocelot)
{
	struct device *dev = ocelot->dev;
	struct ocelot_fdma *fdma;
	int ret;

	fdma = devm_kzalloc(dev, sizeof(*fdma), GFP_KERNEL);
	if (!fdma)
		return;

	ocelot->fdma = fdma;
	ocelot->dev->coherent_dma_mask = DMA_BIT_MASK(32);

	ocelot_fdma_writel(ocelot, MSCC_FDMA_INTR_ENA, 0);

	fdma->ocelot = ocelot;
	fdma->irq = platform_get_irq_byname(pdev, "fdma");
	ret = devm_request_irq(dev, fdma->irq, ocelot_fdma_interrupt, 0,
			       dev_name(dev), ocelot);
	if (ret)
		goto err_free_fdma;

	ret = ocelot_fdma_rings_alloc(ocelot);
	if (ret)
		goto err_free_irq;

	static_branch_enable(&ocelot_fdma_enabled);

	return;

err_free_irq:
	devm_free_irq(dev, fdma->irq, fdma);
err_free_fdma:
	devm_kfree(dev, fdma);

	ocelot->fdma = NULL;
}

void ocelot_fdma_start(struct ocelot *ocelot)
{
	struct ocelot_fdma *fdma = ocelot->fdma;

	/* Reconfigure for extraction and injection using DMA */
	ocelot_write_rix(ocelot, QS_INJ_GRP_CFG_MODE(2), QS_INJ_GRP_CFG, 0);
	ocelot_write_rix(ocelot, QS_INJ_CTRL_GAP_SIZE(0), QS_INJ_CTRL, 0);

	ocelot_write_rix(ocelot, QS_XTR_GRP_CFG_MODE(2), QS_XTR_GRP_CFG, 0);

	ocelot_fdma_writel(ocelot, MSCC_FDMA_INTR_LLP, 0xffffffff);
	ocelot_fdma_writel(ocelot, MSCC_FDMA_INTR_FRM, 0xffffffff);

	ocelot_fdma_writel(ocelot, MSCC_FDMA_INTR_LLP_ENA,
			   BIT(MSCC_FDMA_INJ_CHAN) | BIT(MSCC_FDMA_XTR_CHAN));
	ocelot_fdma_writel(ocelot, MSCC_FDMA_INTR_FRM_ENA,
			   BIT(MSCC_FDMA_XTR_CHAN));
	ocelot_fdma_writel(ocelot, MSCC_FDMA_INTR_ENA,
			   BIT(MSCC_FDMA_INJ_CHAN) | BIT(MSCC_FDMA_XTR_CHAN));

	napi_enable(&fdma->napi);

	ocelot_fdma_activate_chan(ocelot, ocelot->fdma->rx_ring.dcbs_dma,
				  MSCC_FDMA_XTR_CHAN);
}

void ocelot_fdma_deinit(struct ocelot *ocelot)
{
	struct ocelot_fdma *fdma = ocelot->fdma;

	ocelot_fdma_writel(ocelot, MSCC_FDMA_INTR_ENA, 0);
	ocelot_fdma_writel(ocelot, MSCC_FDMA_CH_FORCEDIS,
			   BIT(MSCC_FDMA_XTR_CHAN));
	ocelot_fdma_writel(ocelot, MSCC_FDMA_CH_FORCEDIS,
			   BIT(MSCC_FDMA_INJ_CHAN));
	napi_synchronize(&fdma->napi);
	napi_disable(&fdma->napi);

	ocelot_fdma_free_rx_ring(ocelot);
	ocelot_fdma_free_tx_ring(ocelot);
}
