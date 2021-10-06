/* Synopsys DesignWare Core Enterprise Ethernet (XLGMAC) Driver
 *
 * Copyright (c) 2017 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is dual-licensed; you may select either version 2 of
 * the GNU General Public License ("GPL") or BSD license ("BSD").
 *
 * This Synopsys DWC XLGMAC software driver and associated documentation
 * (hereinafter the "Software") is an unsupported proprietary work of
 * Synopsys, Inc. unless otherwise expressly agreed to in writing between
 * Synopsys and you. The Software IS NOT an item of Licensed Software or a
 * Licensed Product under any End User Software License Agreement or
 * Agreement for Licensed Products with Synopsys or any supplement thereto.
 * Synopsys is a registered trademark of Synopsys, Inc. Other names included
 * in the SOFTWARE may be the trademarks of their respective owners.
 */

#include <linux/netdevice.h>
#include <linux/tcp.h>
#include <linux/interrupt.h>

#include "dwc-xlgmac.h"
#include "dwc-xlgmac-reg.h"

static int xlgmac_one_poll(struct napi_struct *, int);
static int xlgmac_all_poll(struct napi_struct *, int);

static inline unsigned int xlgmac_tx_avail_desc(struct xlgmac_ring *ring)
{
	return (ring->dma_desc_count - (ring->cur - ring->dirty));
}

static inline unsigned int xlgmac_rx_dirty_desc(struct xlgmac_ring *ring)
{
	return (ring->cur - ring->dirty);
}

static int xlgmac_maybe_stop_tx_queue(
			struct xlgmac_channel *channel,
			struct xlgmac_ring *ring,
			unsigned int count)
{
	struct xlgmac_pdata *pdata = channel->pdata;

	if (count > xlgmac_tx_avail_desc(ring)) {
		netif_info(pdata, drv, pdata->netdev,
			   "Tx queue stopped, not enough descriptors available\n");
		netif_stop_subqueue(pdata->netdev, channel->queue_index);
		ring->tx.queue_stopped = 1;

		/* If we haven't notified the hardware because of xmit_more
		 * support, tell it now
		 */
		if (ring->tx.xmit_more)
			pdata->hw_ops.tx_start_xmit(channel, ring);

		return NETDEV_TX_BUSY;
	}

	return 0;
}

static void xlgmac_prep_vlan(struct sk_buff *skb,
			     struct xlgmac_pkt_info *pkt_info)
{
	if (skb_vlan_tag_present(skb))
		pkt_info->vlan_ctag = skb_vlan_tag_get(skb);
}

static int xlgmac_prep_tso(struct sk_buff *skb,
			   struct xlgmac_pkt_info *pkt_info)
{
	int ret;

	if (!XLGMAC_GET_REG_BITS(pkt_info->attributes,
				 TX_PACKET_ATTRIBUTES_TSO_ENABLE_POS,
				 TX_PACKET_ATTRIBUTES_TSO_ENABLE_LEN))
		return 0;

	ret = skb_cow_head(skb, 0);
	if (ret)
		return ret;

	pkt_info->header_len = skb_transport_offset(skb) + tcp_hdrlen(skb);
	pkt_info->tcp_header_len = tcp_hdrlen(skb);
	pkt_info->tcp_payload_len = skb->len - pkt_info->header_len;
	pkt_info->mss = skb_shinfo(skb)->gso_size;

	XLGMAC_PR("header_len=%u\n", pkt_info->header_len);
	XLGMAC_PR("tcp_header_len=%u, tcp_payload_len=%u\n",
		  pkt_info->tcp_header_len, pkt_info->tcp_payload_len);
	XLGMAC_PR("mss=%u\n", pkt_info->mss);

	/* Update the number of packets that will ultimately be transmitted
	 * along with the extra bytes for each extra packet
	 */
	pkt_info->tx_packets = skb_shinfo(skb)->gso_segs;
	pkt_info->tx_bytes += (pkt_info->tx_packets - 1) * pkt_info->header_len;

	return 0;
}

static int xlgmac_is_tso(struct sk_buff *skb)
{
	if (skb->ip_summed != CHECKSUM_PARTIAL)
		return 0;

	if (!skb_is_gso(skb))
		return 0;

	return 1;
}

static void xlgmac_prep_tx_pkt(struct xlgmac_pdata *pdata,
			       struct xlgmac_ring *ring,
			       struct sk_buff *skb,
			       struct xlgmac_pkt_info *pkt_info)
{
	skb_frag_t *frag;
	unsigned int context_desc;
	unsigned int len;
	unsigned int i;

	pkt_info->skb = skb;

	context_desc = 0;
	pkt_info->desc_count = 0;

	pkt_info->tx_packets = 1;
	pkt_info->tx_bytes = skb->len;

	if (xlgmac_is_tso(skb)) {
		/* TSO requires an extra descriptor if mss is different */
		if (skb_shinfo(skb)->gso_size != ring->tx.cur_mss) {
			context_desc = 1;
			pkt_info->desc_count++;
		}

		/* TSO requires an extra descriptor for TSO header */
		pkt_info->desc_count++;

		pkt_info->attributes = XLGMAC_SET_REG_BITS(
					pkt_info->attributes,
					TX_PACKET_ATTRIBUTES_TSO_ENABLE_POS,
					TX_PACKET_ATTRIBUTES_TSO_ENABLE_LEN,
					1);
		pkt_info->attributes = XLGMAC_SET_REG_BITS(
					pkt_info->attributes,
					TX_PACKET_ATTRIBUTES_CSUM_ENABLE_POS,
					TX_PACKET_ATTRIBUTES_CSUM_ENABLE_LEN,
					1);
	} else if (skb->ip_summed == CHECKSUM_PARTIAL)
		pkt_info->attributes = XLGMAC_SET_REG_BITS(
					pkt_info->attributes,
					TX_PACKET_ATTRIBUTES_CSUM_ENABLE_POS,
					TX_PACKET_ATTRIBUTES_CSUM_ENABLE_LEN,
					1);

	if (skb_vlan_tag_present(skb)) {
		/* VLAN requires an extra descriptor if tag is different */
		if (skb_vlan_tag_get(skb) != ring->tx.cur_vlan_ctag)
			/* We can share with the TSO context descriptor */
			if (!context_desc) {
				context_desc = 1;
				pkt_info->desc_count++;
			}

		pkt_info->attributes = XLGMAC_SET_REG_BITS(
					pkt_info->attributes,
					TX_PACKET_ATTRIBUTES_VLAN_CTAG_POS,
					TX_PACKET_ATTRIBUTES_VLAN_CTAG_LEN,
					1);
	}

	for (len = skb_headlen(skb); len;) {
		pkt_info->desc_count++;
		len -= min_t(unsigned int, len, XLGMAC_TX_MAX_BUF_SIZE);
	}

	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		frag = &skb_shinfo(skb)->frags[i];
		for (len = skb_frag_size(frag); len; ) {
			pkt_info->desc_count++;
			len -= min_t(unsigned int, len, XLGMAC_TX_MAX_BUF_SIZE);
		}
	}
}

static int xlgmac_calc_rx_buf_size(struct net_device *netdev, unsigned int mtu)
{
	unsigned int rx_buf_size;

	if (mtu > XLGMAC_JUMBO_PACKET_MTU) {
		netdev_alert(netdev, "MTU exceeds maximum supported value\n");
		return -EINVAL;
	}

	rx_buf_size = mtu + ETH_HLEN + ETH_FCS_LEN + VLAN_HLEN;
	rx_buf_size = clamp_val(rx_buf_size, XLGMAC_RX_MIN_BUF_SIZE, PAGE_SIZE);

	rx_buf_size = (rx_buf_size + XLGMAC_RX_BUF_ALIGN - 1) &
		      ~(XLGMAC_RX_BUF_ALIGN - 1);

	return rx_buf_size;
}

static void xlgmac_enable_rx_tx_ints(struct xlgmac_pdata *pdata)
{
	struct xlgmac_hw_ops *hw_ops = &pdata->hw_ops;
	struct xlgmac_channel *channel;
	enum xlgmac_int int_id;
	unsigned int i;

	channel = pdata->channel_head;
	for (i = 0; i < pdata->channel_count; i++, channel++) {
		if (channel->tx_ring && channel->rx_ring)
			int_id = XLGMAC_INT_DMA_CH_SR_TI_RI;
		else if (channel->tx_ring)
			int_id = XLGMAC_INT_DMA_CH_SR_TI;
		else if (channel->rx_ring)
			int_id = XLGMAC_INT_DMA_CH_SR_RI;
		else
			continue;

		hw_ops->enable_int(channel, int_id);
	}
}

static void xlgmac_disable_rx_tx_ints(struct xlgmac_pdata *pdata)
{
	struct xlgmac_hw_ops *hw_ops = &pdata->hw_ops;
	struct xlgmac_channel *channel;
	enum xlgmac_int int_id;
	unsigned int i;

	channel = pdata->channel_head;
	for (i = 0; i < pdata->channel_count; i++, channel++) {
		if (channel->tx_ring && channel->rx_ring)
			int_id = XLGMAC_INT_DMA_CH_SR_TI_RI;
		else if (channel->tx_ring)
			int_id = XLGMAC_INT_DMA_CH_SR_TI;
		else if (channel->rx_ring)
			int_id = XLGMAC_INT_DMA_CH_SR_RI;
		else
			continue;

		hw_ops->disable_int(channel, int_id);
	}
}

static irqreturn_t xlgmac_isr(int irq, void *data)
{
	unsigned int dma_isr, dma_ch_isr, mac_isr;
	struct xlgmac_pdata *pdata = data;
	struct xlgmac_channel *channel;
	struct xlgmac_hw_ops *hw_ops;
	unsigned int i, ti, ri;

	hw_ops = &pdata->hw_ops;

	/* The DMA interrupt status register also reports MAC and MTL
	 * interrupts. So for polling mode, we just need to check for
	 * this register to be non-zero
	 */
	dma_isr = readl(pdata->mac_regs + DMA_ISR);
	if (!dma_isr)
		return IRQ_HANDLED;

	netif_dbg(pdata, intr, pdata->netdev, "DMA_ISR=%#010x\n", dma_isr);

	for (i = 0; i < pdata->channel_count; i++) {
		if (!(dma_isr & (1 << i)))
			continue;

		channel = pdata->channel_head + i;

		dma_ch_isr = readl(XLGMAC_DMA_REG(channel, DMA_CH_SR));
		netif_dbg(pdata, intr, pdata->netdev, "DMA_CH%u_ISR=%#010x\n",
			  i, dma_ch_isr);

		/* The TI or RI interrupt bits may still be set even if using
		 * per channel DMA interrupts. Check to be sure those are not
		 * enabled before using the private data napi structure.
		 */
		ti = XLGMAC_GET_REG_BITS(dma_ch_isr, DMA_CH_SR_TI_POS,
					 DMA_CH_SR_TI_LEN);
		ri = XLGMAC_GET_REG_BITS(dma_ch_isr, DMA_CH_SR_RI_POS,
					 DMA_CH_SR_RI_LEN);
		if (!pdata->per_channel_irq && (ti || ri)) {
			if (napi_schedule_prep(&pdata->napi)) {
				/* Disable Tx and Rx interrupts */
				xlgmac_disable_rx_tx_ints(pdata);

				pdata->stats.napi_poll_isr++;
				/* Turn on polling */
				__napi_schedule_irqoff(&pdata->napi);
			}
		}

		if (XLGMAC_GET_REG_BITS(dma_ch_isr, DMA_CH_SR_TPS_POS,
					DMA_CH_SR_TPS_LEN))
			pdata->stats.tx_process_stopped++;

		if (XLGMAC_GET_REG_BITS(dma_ch_isr, DMA_CH_SR_RPS_POS,
					DMA_CH_SR_RPS_LEN))
			pdata->stats.rx_process_stopped++;

		if (XLGMAC_GET_REG_BITS(dma_ch_isr, DMA_CH_SR_TBU_POS,
					DMA_CH_SR_TBU_LEN))
			pdata->stats.tx_buffer_unavailable++;

		if (XLGMAC_GET_REG_BITS(dma_ch_isr, DMA_CH_SR_RBU_POS,
					DMA_CH_SR_RBU_LEN))
			pdata->stats.rx_buffer_unavailable++;

		/* Restart the device on a Fatal Bus Error */
		if (XLGMAC_GET_REG_BITS(dma_ch_isr, DMA_CH_SR_FBE_POS,
					DMA_CH_SR_FBE_LEN)) {
			pdata->stats.fatal_bus_error++;
			schedule_work(&pdata->restart_work);
		}

		/* Clear all interrupt signals */
		writel(dma_ch_isr, XLGMAC_DMA_REG(channel, DMA_CH_SR));
	}

	if (XLGMAC_GET_REG_BITS(dma_isr, DMA_ISR_MACIS_POS,
				DMA_ISR_MACIS_LEN)) {
		mac_isr = readl(pdata->mac_regs + MAC_ISR);

		if (XLGMAC_GET_REG_BITS(mac_isr, MAC_ISR_MMCTXIS_POS,
					MAC_ISR_MMCTXIS_LEN))
			hw_ops->tx_mmc_int(pdata);

		if (XLGMAC_GET_REG_BITS(mac_isr, MAC_ISR_MMCRXIS_POS,
					MAC_ISR_MMCRXIS_LEN))
			hw_ops->rx_mmc_int(pdata);
	}

	return IRQ_HANDLED;
}

static irqreturn_t xlgmac_dma_isr(int irq, void *data)
{
	struct xlgmac_channel *channel = data;

	/* Per channel DMA interrupts are enabled, so we use the per
	 * channel napi structure and not the private data napi structure
	 */
	if (napi_schedule_prep(&channel->napi)) {
		/* Disable Tx and Rx interrupts */
		disable_irq_nosync(channel->dma_irq);

		/* Turn on polling */
		__napi_schedule_irqoff(&channel->napi);
	}

	return IRQ_HANDLED;
}

static void xlgmac_tx_timer(struct timer_list *t)
{
	struct xlgmac_channel *channel = from_timer(channel, t, tx_timer);
	struct xlgmac_pdata *pdata = channel->pdata;
	struct napi_struct *napi;

	napi = (pdata->per_channel_irq) ? &channel->napi : &pdata->napi;

	if (napi_schedule_prep(napi)) {
		/* Disable Tx and Rx interrupts */
		if (pdata->per_channel_irq)
			disable_irq_nosync(channel->dma_irq);
		else
			xlgmac_disable_rx_tx_ints(pdata);

		pdata->stats.napi_poll_txtimer++;
		/* Turn on polling */
		__napi_schedule(napi);
	}

	channel->tx_timer_active = 0;
}

static void xlgmac_init_timers(struct xlgmac_pdata *pdata)
{
	struct xlgmac_channel *channel;
	unsigned int i;

	channel = pdata->channel_head;
	for (i = 0; i < pdata->channel_count; i++, channel++) {
		if (!channel->tx_ring)
			break;

		timer_setup(&channel->tx_timer, xlgmac_tx_timer, 0);
	}
}

static void xlgmac_stop_timers(struct xlgmac_pdata *pdata)
{
	struct xlgmac_channel *channel;
	unsigned int i;

	channel = pdata->channel_head;
	for (i = 0; i < pdata->channel_count; i++, channel++) {
		if (!channel->tx_ring)
			break;

		del_timer_sync(&channel->tx_timer);
	}
}

static void xlgmac_napi_enable(struct xlgmac_pdata *pdata, unsigned int add)
{
	struct xlgmac_channel *channel;
	unsigned int i;

	if (pdata->per_channel_irq) {
		channel = pdata->channel_head;
		for (i = 0; i < pdata->channel_count; i++, channel++) {
			if (add)
				netif_napi_add(pdata->netdev, &channel->napi,
					       xlgmac_one_poll,
					       NAPI_POLL_WEIGHT);

			napi_enable(&channel->napi);
		}
	} else {
		if (add)
			netif_napi_add(pdata->netdev, &pdata->napi,
				       xlgmac_all_poll, NAPI_POLL_WEIGHT);

		napi_enable(&pdata->napi);
	}
}

static void xlgmac_napi_disable(struct xlgmac_pdata *pdata, unsigned int del)
{
	struct xlgmac_channel *channel;
	unsigned int i;

	if (pdata->per_channel_irq) {
		channel = pdata->channel_head;
		for (i = 0; i < pdata->channel_count; i++, channel++) {
			napi_disable(&channel->napi);

			if (del)
				netif_napi_del(&channel->napi);
		}
	} else {
		napi_disable(&pdata->napi);

		if (del)
			netif_napi_del(&pdata->napi);
	}
}

static int xlgmac_request_irqs(struct xlgmac_pdata *pdata)
{
	struct net_device *netdev = pdata->netdev;
	struct xlgmac_channel *channel;
	unsigned int i;
	int ret;

	ret = devm_request_irq(pdata->dev, pdata->dev_irq, xlgmac_isr,
			       IRQF_SHARED, netdev->name, pdata);
	if (ret) {
		netdev_alert(netdev, "error requesting irq %d\n",
			     pdata->dev_irq);
		return ret;
	}

	if (!pdata->per_channel_irq)
		return 0;

	channel = pdata->channel_head;
	for (i = 0; i < pdata->channel_count; i++, channel++) {
		snprintf(channel->dma_irq_name,
			 sizeof(channel->dma_irq_name) - 1,
			 "%s-TxRx-%u", netdev_name(netdev),
			 channel->queue_index);

		ret = devm_request_irq(pdata->dev, channel->dma_irq,
				       xlgmac_dma_isr, 0,
				       channel->dma_irq_name, channel);
		if (ret) {
			netdev_alert(netdev, "error requesting irq %d\n",
				     channel->dma_irq);
			goto err_irq;
		}
	}

	return 0;

err_irq:
	/* Using an unsigned int, 'i' will go to UINT_MAX and exit */
	for (i--, channel--; i < pdata->channel_count; i--, channel--)
		devm_free_irq(pdata->dev, channel->dma_irq, channel);

	devm_free_irq(pdata->dev, pdata->dev_irq, pdata);

	return ret;
}

static void xlgmac_free_irqs(struct xlgmac_pdata *pdata)
{
	struct xlgmac_channel *channel;
	unsigned int i;

	devm_free_irq(pdata->dev, pdata->dev_irq, pdata);

	if (!pdata->per_channel_irq)
		return;

	channel = pdata->channel_head;
	for (i = 0; i < pdata->channel_count; i++, channel++)
		devm_free_irq(pdata->dev, channel->dma_irq, channel);
}

static void xlgmac_free_tx_data(struct xlgmac_pdata *pdata)
{
	struct xlgmac_desc_ops *desc_ops = &pdata->desc_ops;
	struct xlgmac_desc_data *desc_data;
	struct xlgmac_channel *channel;
	struct xlgmac_ring *ring;
	unsigned int i, j;

	channel = pdata->channel_head;
	for (i = 0; i < pdata->channel_count; i++, channel++) {
		ring = channel->tx_ring;
		if (!ring)
			break;

		for (j = 0; j < ring->dma_desc_count; j++) {
			desc_data = XLGMAC_GET_DESC_DATA(ring, j);
			desc_ops->unmap_desc_data(pdata, desc_data);
		}
	}
}

static void xlgmac_free_rx_data(struct xlgmac_pdata *pdata)
{
	struct xlgmac_desc_ops *desc_ops = &pdata->desc_ops;
	struct xlgmac_desc_data *desc_data;
	struct xlgmac_channel *channel;
	struct xlgmac_ring *ring;
	unsigned int i, j;

	channel = pdata->channel_head;
	for (i = 0; i < pdata->channel_count; i++, channel++) {
		ring = channel->rx_ring;
		if (!ring)
			break;

		for (j = 0; j < ring->dma_desc_count; j++) {
			desc_data = XLGMAC_GET_DESC_DATA(ring, j);
			desc_ops->unmap_desc_data(pdata, desc_data);
		}
	}
}

static int xlgmac_start(struct xlgmac_pdata *pdata)
{
	struct xlgmac_hw_ops *hw_ops = &pdata->hw_ops;
	struct net_device *netdev = pdata->netdev;
	int ret;

	hw_ops->init(pdata);
	xlgmac_napi_enable(pdata, 1);

	ret = xlgmac_request_irqs(pdata);
	if (ret)
		goto err_napi;

	hw_ops->enable_tx(pdata);
	hw_ops->enable_rx(pdata);
	netif_tx_start_all_queues(netdev);

	return 0;

err_napi:
	xlgmac_napi_disable(pdata, 1);
	hw_ops->exit(pdata);

	return ret;
}

static void xlgmac_stop(struct xlgmac_pdata *pdata)
{
	struct xlgmac_hw_ops *hw_ops = &pdata->hw_ops;
	struct net_device *netdev = pdata->netdev;
	struct xlgmac_channel *channel;
	struct netdev_queue *txq;
	unsigned int i;

	netif_tx_stop_all_queues(netdev);
	xlgmac_stop_timers(pdata);
	hw_ops->disable_tx(pdata);
	hw_ops->disable_rx(pdata);
	xlgmac_free_irqs(pdata);
	xlgmac_napi_disable(pdata, 1);
	hw_ops->exit(pdata);

	channel = pdata->channel_head;
	for (i = 0; i < pdata->channel_count; i++, channel++) {
		if (!channel->tx_ring)
			continue;

		txq = netdev_get_tx_queue(netdev, channel->queue_index);
		netdev_tx_reset_queue(txq);
	}
}

static void xlgmac_restart_dev(struct xlgmac_pdata *pdata)
{
	/* If not running, "restart" will happen on open */
	if (!netif_running(pdata->netdev))
		return;

	xlgmac_stop(pdata);

	xlgmac_free_tx_data(pdata);
	xlgmac_free_rx_data(pdata);

	xlgmac_start(pdata);
}

static void xlgmac_restart(struct work_struct *work)
{
	struct xlgmac_pdata *pdata = container_of(work,
						   struct xlgmac_pdata,
						   restart_work);

	rtnl_lock();

	xlgmac_restart_dev(pdata);

	rtnl_unlock();
}

static int xlgmac_open(struct net_device *netdev)
{
	struct xlgmac_pdata *pdata = netdev_priv(netdev);
	struct xlgmac_desc_ops *desc_ops;
	int ret;

	desc_ops = &pdata->desc_ops;

	/* TODO: Initialize the phy */

	/* Calculate the Rx buffer size before allocating rings */
	ret = xlgmac_calc_rx_buf_size(netdev, netdev->mtu);
	if (ret < 0)
		return ret;
	pdata->rx_buf_size = ret;

	/* Allocate the channels and rings */
	ret = desc_ops->alloc_channels_and_rings(pdata);
	if (ret)
		return ret;

	INIT_WORK(&pdata->restart_work, xlgmac_restart);
	xlgmac_init_timers(pdata);

	ret = xlgmac_start(pdata);
	if (ret)
		goto err_channels_and_rings;

	return 0;

err_channels_and_rings:
	desc_ops->free_channels_and_rings(pdata);

	return ret;
}

static int xlgmac_close(struct net_device *netdev)
{
	struct xlgmac_pdata *pdata = netdev_priv(netdev);
	struct xlgmac_desc_ops *desc_ops;

	desc_ops = &pdata->desc_ops;

	/* Stop the device */
	xlgmac_stop(pdata);

	/* Free the channels and rings */
	desc_ops->free_channels_and_rings(pdata);

	return 0;
}

static void xlgmac_tx_timeout(struct net_device *netdev, unsigned int txqueue)
{
	struct xlgmac_pdata *pdata = netdev_priv(netdev);

	netdev_warn(netdev, "tx timeout, device restarting\n");
	schedule_work(&pdata->restart_work);
}

static netdev_tx_t xlgmac_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	struct xlgmac_pdata *pdata = netdev_priv(netdev);
	struct xlgmac_pkt_info *tx_pkt_info;
	struct xlgmac_desc_ops *desc_ops;
	struct xlgmac_channel *channel;
	struct xlgmac_hw_ops *hw_ops;
	struct netdev_queue *txq;
	struct xlgmac_ring *ring;
	int ret;

	desc_ops = &pdata->desc_ops;
	hw_ops = &pdata->hw_ops;

	XLGMAC_PR("skb->len = %d\n", skb->len);

	channel = pdata->channel_head + skb->queue_mapping;
	txq = netdev_get_tx_queue(netdev, channel->queue_index);
	ring = channel->tx_ring;
	tx_pkt_info = &ring->pkt_info;

	if (skb->len == 0) {
		netif_err(pdata, tx_err, netdev,
			  "empty skb received from stack\n");
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	/* Prepare preliminary packet info for TX */
	memset(tx_pkt_info, 0, sizeof(*tx_pkt_info));
	xlgmac_prep_tx_pkt(pdata, ring, skb, tx_pkt_info);

	/* Check that there are enough descriptors available */
	ret = xlgmac_maybe_stop_tx_queue(channel, ring,
					 tx_pkt_info->desc_count);
	if (ret)
		return ret;

	ret = xlgmac_prep_tso(skb, tx_pkt_info);
	if (ret) {
		netif_err(pdata, tx_err, netdev,
			  "error processing TSO packet\n");
		dev_kfree_skb_any(skb);
		return ret;
	}
	xlgmac_prep_vlan(skb, tx_pkt_info);

	if (!desc_ops->map_tx_skb(channel, skb)) {
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	/* Report on the actual number of bytes (to be) sent */
	netdev_tx_sent_queue(txq, tx_pkt_info->tx_bytes);

	/* Configure required descriptor fields for transmission */
	hw_ops->dev_xmit(channel);

	if (netif_msg_pktdata(pdata))
		xlgmac_print_pkt(netdev, skb, true);

	/* Stop the queue in advance if there may not be enough descriptors */
	xlgmac_maybe_stop_tx_queue(channel, ring, XLGMAC_TX_MAX_DESC_NR);

	return NETDEV_TX_OK;
}

static void xlgmac_get_stats64(struct net_device *netdev,
			       struct rtnl_link_stats64 *s)
{
	struct xlgmac_pdata *pdata = netdev_priv(netdev);
	struct xlgmac_stats *pstats = &pdata->stats;

	pdata->hw_ops.read_mmc_stats(pdata);

	s->rx_packets = pstats->rxframecount_gb;
	s->rx_bytes = pstats->rxoctetcount_gb;
	s->rx_errors = pstats->rxframecount_gb -
		       pstats->rxbroadcastframes_g -
		       pstats->rxmulticastframes_g -
		       pstats->rxunicastframes_g;
	s->multicast = pstats->rxmulticastframes_g;
	s->rx_length_errors = pstats->rxlengtherror;
	s->rx_crc_errors = pstats->rxcrcerror;
	s->rx_fifo_errors = pstats->rxfifooverflow;

	s->tx_packets = pstats->txframecount_gb;
	s->tx_bytes = pstats->txoctetcount_gb;
	s->tx_errors = pstats->txframecount_gb - pstats->txframecount_g;
	s->tx_dropped = netdev->stats.tx_dropped;
}

static int xlgmac_set_mac_address(struct net_device *netdev, void *addr)
{
	struct xlgmac_pdata *pdata = netdev_priv(netdev);
	struct xlgmac_hw_ops *hw_ops = &pdata->hw_ops;
	struct sockaddr *saddr = addr;

	if (!is_valid_ether_addr(saddr->sa_data))
		return -EADDRNOTAVAIL;

	memcpy(netdev->dev_addr, saddr->sa_data, netdev->addr_len);

	hw_ops->set_mac_address(pdata, netdev->dev_addr);

	return 0;
}

static int xlgmac_ioctl(struct net_device *netdev,
			struct ifreq *ifreq, int cmd)
{
	if (!netif_running(netdev))
		return -ENODEV;

	return 0;
}

static int xlgmac_change_mtu(struct net_device *netdev, int mtu)
{
	struct xlgmac_pdata *pdata = netdev_priv(netdev);
	int ret;

	ret = xlgmac_calc_rx_buf_size(netdev, mtu);
	if (ret < 0)
		return ret;

	pdata->rx_buf_size = ret;
	netdev->mtu = mtu;

	xlgmac_restart_dev(pdata);

	return 0;
}

static int xlgmac_vlan_rx_add_vid(struct net_device *netdev,
				  __be16 proto,
				  u16 vid)
{
	struct xlgmac_pdata *pdata = netdev_priv(netdev);
	struct xlgmac_hw_ops *hw_ops = &pdata->hw_ops;

	set_bit(vid, pdata->active_vlans);
	hw_ops->update_vlan_hash_table(pdata);

	return 0;
}

static int xlgmac_vlan_rx_kill_vid(struct net_device *netdev,
				   __be16 proto,
				   u16 vid)
{
	struct xlgmac_pdata *pdata = netdev_priv(netdev);
	struct xlgmac_hw_ops *hw_ops = &pdata->hw_ops;

	clear_bit(vid, pdata->active_vlans);
	hw_ops->update_vlan_hash_table(pdata);

	return 0;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void xlgmac_poll_controller(struct net_device *netdev)
{
	struct xlgmac_pdata *pdata = netdev_priv(netdev);
	struct xlgmac_channel *channel;
	unsigned int i;

	if (pdata->per_channel_irq) {
		channel = pdata->channel_head;
		for (i = 0; i < pdata->channel_count; i++, channel++)
			xlgmac_dma_isr(channel->dma_irq, channel);
	} else {
		disable_irq(pdata->dev_irq);
		xlgmac_isr(pdata->dev_irq, pdata);
		enable_irq(pdata->dev_irq);
	}
}
#endif /* CONFIG_NET_POLL_CONTROLLER */

static int xlgmac_set_features(struct net_device *netdev,
			       netdev_features_t features)
{
	netdev_features_t rxhash, rxcsum, rxvlan, rxvlan_filter;
	struct xlgmac_pdata *pdata = netdev_priv(netdev);
	struct xlgmac_hw_ops *hw_ops = &pdata->hw_ops;
	int ret = 0;

	rxhash = pdata->netdev_features & NETIF_F_RXHASH;
	rxcsum = pdata->netdev_features & NETIF_F_RXCSUM;
	rxvlan = pdata->netdev_features & NETIF_F_HW_VLAN_CTAG_RX;
	rxvlan_filter = pdata->netdev_features & NETIF_F_HW_VLAN_CTAG_FILTER;

	if ((features & NETIF_F_RXHASH) && !rxhash)
		ret = hw_ops->enable_rss(pdata);
	else if (!(features & NETIF_F_RXHASH) && rxhash)
		ret = hw_ops->disable_rss(pdata);
	if (ret)
		return ret;

	if ((features & NETIF_F_RXCSUM) && !rxcsum)
		hw_ops->enable_rx_csum(pdata);
	else if (!(features & NETIF_F_RXCSUM) && rxcsum)
		hw_ops->disable_rx_csum(pdata);

	if ((features & NETIF_F_HW_VLAN_CTAG_RX) && !rxvlan)
		hw_ops->enable_rx_vlan_stripping(pdata);
	else if (!(features & NETIF_F_HW_VLAN_CTAG_RX) && rxvlan)
		hw_ops->disable_rx_vlan_stripping(pdata);

	if ((features & NETIF_F_HW_VLAN_CTAG_FILTER) && !rxvlan_filter)
		hw_ops->enable_rx_vlan_filtering(pdata);
	else if (!(features & NETIF_F_HW_VLAN_CTAG_FILTER) && rxvlan_filter)
		hw_ops->disable_rx_vlan_filtering(pdata);

	pdata->netdev_features = features;

	return 0;
}

static void xlgmac_set_rx_mode(struct net_device *netdev)
{
	struct xlgmac_pdata *pdata = netdev_priv(netdev);
	struct xlgmac_hw_ops *hw_ops = &pdata->hw_ops;

	hw_ops->config_rx_mode(pdata);
}

static const struct net_device_ops xlgmac_netdev_ops = {
	.ndo_open		= xlgmac_open,
	.ndo_stop		= xlgmac_close,
	.ndo_start_xmit		= xlgmac_xmit,
	.ndo_tx_timeout		= xlgmac_tx_timeout,
	.ndo_get_stats64	= xlgmac_get_stats64,
	.ndo_change_mtu		= xlgmac_change_mtu,
	.ndo_set_mac_address	= xlgmac_set_mac_address,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_eth_ioctl		= xlgmac_ioctl,
	.ndo_vlan_rx_add_vid	= xlgmac_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid	= xlgmac_vlan_rx_kill_vid,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= xlgmac_poll_controller,
#endif
	.ndo_set_features	= xlgmac_set_features,
	.ndo_set_rx_mode	= xlgmac_set_rx_mode,
};

const struct net_device_ops *xlgmac_get_netdev_ops(void)
{
	return &xlgmac_netdev_ops;
}

static void xlgmac_rx_refresh(struct xlgmac_channel *channel)
{
	struct xlgmac_pdata *pdata = channel->pdata;
	struct xlgmac_ring *ring = channel->rx_ring;
	struct xlgmac_desc_data *desc_data;
	struct xlgmac_desc_ops *desc_ops;
	struct xlgmac_hw_ops *hw_ops;

	desc_ops = &pdata->desc_ops;
	hw_ops = &pdata->hw_ops;

	while (ring->dirty != ring->cur) {
		desc_data = XLGMAC_GET_DESC_DATA(ring, ring->dirty);

		/* Reset desc_data values */
		desc_ops->unmap_desc_data(pdata, desc_data);

		if (desc_ops->map_rx_buffer(pdata, ring, desc_data))
			break;

		hw_ops->rx_desc_reset(pdata, desc_data, ring->dirty);

		ring->dirty++;
	}

	/* Make sure everything is written before the register write */
	wmb();

	/* Update the Rx Tail Pointer Register with address of
	 * the last cleaned entry
	 */
	desc_data = XLGMAC_GET_DESC_DATA(ring, ring->dirty - 1);
	writel(lower_32_bits(desc_data->dma_desc_addr),
	       XLGMAC_DMA_REG(channel, DMA_CH_RDTR_LO));
}

static struct sk_buff *xlgmac_create_skb(struct xlgmac_pdata *pdata,
					 struct napi_struct *napi,
					 struct xlgmac_desc_data *desc_data,
					 unsigned int len)
{
	unsigned int copy_len;
	struct sk_buff *skb;
	u8 *packet;

	skb = napi_alloc_skb(napi, desc_data->rx.hdr.dma_len);
	if (!skb)
		return NULL;

	/* Start with the header buffer which may contain just the header
	 * or the header plus data
	 */
	dma_sync_single_range_for_cpu(pdata->dev, desc_data->rx.hdr.dma_base,
				      desc_data->rx.hdr.dma_off,
				      desc_data->rx.hdr.dma_len,
				      DMA_FROM_DEVICE);

	packet = page_address(desc_data->rx.hdr.pa.pages) +
		 desc_data->rx.hdr.pa.pages_offset;
	copy_len = (desc_data->rx.hdr_len) ? desc_data->rx.hdr_len : len;
	copy_len = min(desc_data->rx.hdr.dma_len, copy_len);
	skb_copy_to_linear_data(skb, packet, copy_len);
	skb_put(skb, copy_len);

	len -= copy_len;
	if (len) {
		/* Add the remaining data as a frag */
		dma_sync_single_range_for_cpu(pdata->dev,
					      desc_data->rx.buf.dma_base,
					      desc_data->rx.buf.dma_off,
					      desc_data->rx.buf.dma_len,
					      DMA_FROM_DEVICE);

		skb_add_rx_frag(skb, skb_shinfo(skb)->nr_frags,
				desc_data->rx.buf.pa.pages,
				desc_data->rx.buf.pa.pages_offset,
				len, desc_data->rx.buf.dma_len);
		desc_data->rx.buf.pa.pages = NULL;
	}

	return skb;
}

static int xlgmac_tx_poll(struct xlgmac_channel *channel)
{
	struct xlgmac_pdata *pdata = channel->pdata;
	struct xlgmac_ring *ring = channel->tx_ring;
	struct net_device *netdev = pdata->netdev;
	unsigned int tx_packets = 0, tx_bytes = 0;
	struct xlgmac_desc_data *desc_data;
	struct xlgmac_dma_desc *dma_desc;
	struct xlgmac_desc_ops *desc_ops;
	struct xlgmac_hw_ops *hw_ops;
	struct netdev_queue *txq;
	int processed = 0;
	unsigned int cur;

	desc_ops = &pdata->desc_ops;
	hw_ops = &pdata->hw_ops;

	/* Nothing to do if there isn't a Tx ring for this channel */
	if (!ring)
		return 0;

	cur = ring->cur;

	/* Be sure we get ring->cur before accessing descriptor data */
	smp_rmb();

	txq = netdev_get_tx_queue(netdev, channel->queue_index);

	while ((processed < XLGMAC_TX_DESC_MAX_PROC) &&
	       (ring->dirty != cur)) {
		desc_data = XLGMAC_GET_DESC_DATA(ring, ring->dirty);
		dma_desc = desc_data->dma_desc;

		if (!hw_ops->tx_complete(dma_desc))
			break;

		/* Make sure descriptor fields are read after reading
		 * the OWN bit
		 */
		dma_rmb();

		if (netif_msg_tx_done(pdata))
			xlgmac_dump_tx_desc(pdata, ring, ring->dirty, 1, 0);

		if (hw_ops->is_last_desc(dma_desc)) {
			tx_packets += desc_data->tx.packets;
			tx_bytes += desc_data->tx.bytes;
		}

		/* Free the SKB and reset the descriptor for re-use */
		desc_ops->unmap_desc_data(pdata, desc_data);
		hw_ops->tx_desc_reset(desc_data);

		processed++;
		ring->dirty++;
	}

	if (!processed)
		return 0;

	netdev_tx_completed_queue(txq, tx_packets, tx_bytes);

	if ((ring->tx.queue_stopped == 1) &&
	    (xlgmac_tx_avail_desc(ring) > XLGMAC_TX_DESC_MIN_FREE)) {
		ring->tx.queue_stopped = 0;
		netif_tx_wake_queue(txq);
	}

	XLGMAC_PR("processed=%d\n", processed);

	return processed;
}

static int xlgmac_rx_poll(struct xlgmac_channel *channel, int budget)
{
	struct xlgmac_pdata *pdata = channel->pdata;
	struct xlgmac_ring *ring = channel->rx_ring;
	struct net_device *netdev = pdata->netdev;
	unsigned int len, dma_desc_len, max_len;
	unsigned int context_next, context;
	struct xlgmac_desc_data *desc_data;
	struct xlgmac_pkt_info *pkt_info;
	unsigned int incomplete, error;
	struct xlgmac_hw_ops *hw_ops;
	unsigned int received = 0;
	struct napi_struct *napi;
	struct sk_buff *skb;
	int packet_count = 0;

	hw_ops = &pdata->hw_ops;

	/* Nothing to do if there isn't a Rx ring for this channel */
	if (!ring)
		return 0;

	incomplete = 0;
	context_next = 0;

	napi = (pdata->per_channel_irq) ? &channel->napi : &pdata->napi;

	desc_data = XLGMAC_GET_DESC_DATA(ring, ring->cur);
	pkt_info = &ring->pkt_info;
	while (packet_count < budget) {
		/* First time in loop see if we need to restore state */
		if (!received && desc_data->state_saved) {
			skb = desc_data->state.skb;
			error = desc_data->state.error;
			len = desc_data->state.len;
		} else {
			memset(pkt_info, 0, sizeof(*pkt_info));
			skb = NULL;
			error = 0;
			len = 0;
		}

read_again:
		desc_data = XLGMAC_GET_DESC_DATA(ring, ring->cur);

		if (xlgmac_rx_dirty_desc(ring) > XLGMAC_RX_DESC_MAX_DIRTY)
			xlgmac_rx_refresh(channel);

		if (hw_ops->dev_read(channel))
			break;

		received++;
		ring->cur++;

		incomplete = XLGMAC_GET_REG_BITS(
					pkt_info->attributes,
					RX_PACKET_ATTRIBUTES_INCOMPLETE_POS,
					RX_PACKET_ATTRIBUTES_INCOMPLETE_LEN);
		context_next = XLGMAC_GET_REG_BITS(
					pkt_info->attributes,
					RX_PACKET_ATTRIBUTES_CONTEXT_NEXT_POS,
					RX_PACKET_ATTRIBUTES_CONTEXT_NEXT_LEN);
		context = XLGMAC_GET_REG_BITS(
					pkt_info->attributes,
					RX_PACKET_ATTRIBUTES_CONTEXT_POS,
					RX_PACKET_ATTRIBUTES_CONTEXT_LEN);

		/* Earlier error, just drain the remaining data */
		if ((incomplete || context_next) && error)
			goto read_again;

		if (error || pkt_info->errors) {
			if (pkt_info->errors)
				netif_err(pdata, rx_err, netdev,
					  "error in received packet\n");
			dev_kfree_skb(skb);
			goto next_packet;
		}

		if (!context) {
			/* Length is cumulative, get this descriptor's length */
			dma_desc_len = desc_data->rx.len - len;
			len += dma_desc_len;

			if (dma_desc_len && !skb) {
				skb = xlgmac_create_skb(pdata, napi, desc_data,
							dma_desc_len);
				if (!skb)
					error = 1;
			} else if (dma_desc_len) {
				dma_sync_single_range_for_cpu(
						pdata->dev,
						desc_data->rx.buf.dma_base,
						desc_data->rx.buf.dma_off,
						desc_data->rx.buf.dma_len,
						DMA_FROM_DEVICE);

				skb_add_rx_frag(
					skb, skb_shinfo(skb)->nr_frags,
					desc_data->rx.buf.pa.pages,
					desc_data->rx.buf.pa.pages_offset,
					dma_desc_len,
					desc_data->rx.buf.dma_len);
				desc_data->rx.buf.pa.pages = NULL;
			}
		}

		if (incomplete || context_next)
			goto read_again;

		if (!skb)
			goto next_packet;

		/* Be sure we don't exceed the configured MTU */
		max_len = netdev->mtu + ETH_HLEN;
		if (!(netdev->features & NETIF_F_HW_VLAN_CTAG_RX) &&
		    (skb->protocol == htons(ETH_P_8021Q)))
			max_len += VLAN_HLEN;

		if (skb->len > max_len) {
			netif_err(pdata, rx_err, netdev,
				  "packet length exceeds configured MTU\n");
			dev_kfree_skb(skb);
			goto next_packet;
		}

		if (netif_msg_pktdata(pdata))
			xlgmac_print_pkt(netdev, skb, false);

		skb_checksum_none_assert(skb);
		if (XLGMAC_GET_REG_BITS(pkt_info->attributes,
					RX_PACKET_ATTRIBUTES_CSUM_DONE_POS,
				    RX_PACKET_ATTRIBUTES_CSUM_DONE_LEN))
			skb->ip_summed = CHECKSUM_UNNECESSARY;

		if (XLGMAC_GET_REG_BITS(pkt_info->attributes,
					RX_PACKET_ATTRIBUTES_VLAN_CTAG_POS,
				    RX_PACKET_ATTRIBUTES_VLAN_CTAG_LEN)) {
			__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q),
					       pkt_info->vlan_ctag);
			pdata->stats.rx_vlan_packets++;
		}

		if (XLGMAC_GET_REG_BITS(pkt_info->attributes,
					RX_PACKET_ATTRIBUTES_RSS_HASH_POS,
				    RX_PACKET_ATTRIBUTES_RSS_HASH_LEN))
			skb_set_hash(skb, pkt_info->rss_hash,
				     pkt_info->rss_hash_type);

		skb->dev = netdev;
		skb->protocol = eth_type_trans(skb, netdev);
		skb_record_rx_queue(skb, channel->queue_index);

		napi_gro_receive(napi, skb);

next_packet:
		packet_count++;
	}

	/* Check if we need to save state before leaving */
	if (received && (incomplete || context_next)) {
		desc_data = XLGMAC_GET_DESC_DATA(ring, ring->cur);
		desc_data->state_saved = 1;
		desc_data->state.skb = skb;
		desc_data->state.len = len;
		desc_data->state.error = error;
	}

	XLGMAC_PR("packet_count = %d\n", packet_count);

	return packet_count;
}

static int xlgmac_one_poll(struct napi_struct *napi, int budget)
{
	struct xlgmac_channel *channel = container_of(napi,
						struct xlgmac_channel,
						napi);
	int processed = 0;

	XLGMAC_PR("budget=%d\n", budget);

	/* Cleanup Tx ring first */
	xlgmac_tx_poll(channel);

	/* Process Rx ring next */
	processed = xlgmac_rx_poll(channel, budget);

	/* If we processed everything, we are done */
	if (processed < budget) {
		/* Turn off polling */
		napi_complete_done(napi, processed);

		/* Enable Tx and Rx interrupts */
		enable_irq(channel->dma_irq);
	}

	XLGMAC_PR("received = %d\n", processed);

	return processed;
}

static int xlgmac_all_poll(struct napi_struct *napi, int budget)
{
	struct xlgmac_pdata *pdata = container_of(napi,
						   struct xlgmac_pdata,
						   napi);
	struct xlgmac_channel *channel;
	int processed, last_processed;
	int ring_budget;
	unsigned int i;

	XLGMAC_PR("budget=%d\n", budget);

	processed = 0;
	ring_budget = budget / pdata->rx_ring_count;
	do {
		last_processed = processed;

		channel = pdata->channel_head;
		for (i = 0; i < pdata->channel_count; i++, channel++) {
			/* Cleanup Tx ring first */
			xlgmac_tx_poll(channel);

			/* Process Rx ring next */
			if (ring_budget > (budget - processed))
				ring_budget = budget - processed;
			processed += xlgmac_rx_poll(channel, ring_budget);
		}
	} while ((processed < budget) && (processed != last_processed));

	/* If we processed everything, we are done */
	if (processed < budget) {
		/* Turn off polling */
		napi_complete_done(napi, processed);

		/* Enable Tx and Rx interrupts */
		xlgmac_enable_rx_tx_ints(pdata);
	}

	XLGMAC_PR("received = %d\n", processed);

	return processed;
}
