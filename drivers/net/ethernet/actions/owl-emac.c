// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Actions Semi Owl SoCs Ethernet MAC driver
 *
 * Copyright (c) 2012 Actions Semi Inc.
 * Copyright (c) 2021 Cristian Ciocaltea <cristian.ciocaltea@gmail.com>
 */

#include <linux/circ_buf.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/etherdevice.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/reset.h>

#include "owl-emac.h"

#define OWL_EMAC_DEFAULT_MSG_ENABLE	(NETIF_MSG_DRV | \
					 NETIF_MSG_PROBE | \
					 NETIF_MSG_LINK)

static u32 owl_emac_reg_read(struct owl_emac_priv *priv, u32 reg)
{
	return readl(priv->base + reg);
}

static void owl_emac_reg_write(struct owl_emac_priv *priv, u32 reg, u32 data)
{
	writel(data, priv->base + reg);
}

static u32 owl_emac_reg_update(struct owl_emac_priv *priv,
			       u32 reg, u32 mask, u32 val)
{
	u32 data, old_val;

	data = owl_emac_reg_read(priv, reg);
	old_val = data & mask;

	data &= ~mask;
	data |= val & mask;

	owl_emac_reg_write(priv, reg, data);

	return old_val;
}

static void owl_emac_reg_set(struct owl_emac_priv *priv, u32 reg, u32 bits)
{
	owl_emac_reg_update(priv, reg, bits, bits);
}

static void owl_emac_reg_clear(struct owl_emac_priv *priv, u32 reg, u32 bits)
{
	owl_emac_reg_update(priv, reg, bits, 0);
}

static struct device *owl_emac_get_dev(struct owl_emac_priv *priv)
{
	return priv->netdev->dev.parent;
}

static void owl_emac_irq_enable(struct owl_emac_priv *priv)
{
	/* Enable all interrupts except TU.
	 *
	 * Note the NIE and AIE bits shall also be set in order to actually
	 * enable the selected interrupts.
	 */
	owl_emac_reg_write(priv, OWL_EMAC_REG_MAC_CSR7,
			   OWL_EMAC_BIT_MAC_CSR7_NIE |
			   OWL_EMAC_BIT_MAC_CSR7_AIE |
			   OWL_EMAC_BIT_MAC_CSR7_ALL_NOT_TUE);
}

static void owl_emac_irq_disable(struct owl_emac_priv *priv)
{
	/* Disable all interrupts.
	 *
	 * WARNING: Unset only the NIE and AIE bits in CSR7 to workaround an
	 * unexpected side effect (MAC hardware bug?!) where some bits in the
	 * status register (CSR5) are cleared automatically before being able
	 * to read them via owl_emac_irq_clear().
	 */
	owl_emac_reg_write(priv, OWL_EMAC_REG_MAC_CSR7,
			   OWL_EMAC_BIT_MAC_CSR7_ALL_NOT_TUE);
}

static u32 owl_emac_irq_status(struct owl_emac_priv *priv)
{
	return owl_emac_reg_read(priv, OWL_EMAC_REG_MAC_CSR5);
}

static u32 owl_emac_irq_clear(struct owl_emac_priv *priv)
{
	u32 val = owl_emac_irq_status(priv);

	owl_emac_reg_write(priv, OWL_EMAC_REG_MAC_CSR5, val);

	return val;
}

static dma_addr_t owl_emac_dma_map_rx(struct owl_emac_priv *priv,
				      struct sk_buff *skb)
{
	struct device *dev = owl_emac_get_dev(priv);

	/* Buffer pointer for the RX DMA descriptor must be word aligned. */
	return dma_map_single(dev, skb_tail_pointer(skb),
			      skb_tailroom(skb), DMA_FROM_DEVICE);
}

static void owl_emac_dma_unmap_rx(struct owl_emac_priv *priv,
				  struct sk_buff *skb, dma_addr_t dma_addr)
{
	struct device *dev = owl_emac_get_dev(priv);

	dma_unmap_single(dev, dma_addr, skb_tailroom(skb), DMA_FROM_DEVICE);
}

static dma_addr_t owl_emac_dma_map_tx(struct owl_emac_priv *priv,
				      struct sk_buff *skb)
{
	struct device *dev = owl_emac_get_dev(priv);

	return dma_map_single(dev, skb->data, skb_headlen(skb), DMA_TO_DEVICE);
}

static void owl_emac_dma_unmap_tx(struct owl_emac_priv *priv,
				  struct sk_buff *skb, dma_addr_t dma_addr)
{
	struct device *dev = owl_emac_get_dev(priv);

	dma_unmap_single(dev, dma_addr, skb_headlen(skb), DMA_TO_DEVICE);
}

static unsigned int owl_emac_ring_num_unused(struct owl_emac_ring *ring)
{
	return CIRC_SPACE(ring->head, ring->tail, ring->size);
}

static unsigned int owl_emac_ring_get_next(struct owl_emac_ring *ring,
					   unsigned int cur)
{
	return (cur + 1) & (ring->size - 1);
}

static void owl_emac_ring_push_head(struct owl_emac_ring *ring)
{
	ring->head = owl_emac_ring_get_next(ring, ring->head);
}

static void owl_emac_ring_pop_tail(struct owl_emac_ring *ring)
{
	ring->tail = owl_emac_ring_get_next(ring, ring->tail);
}

static struct sk_buff *owl_emac_alloc_skb(struct net_device *netdev)
{
	struct sk_buff *skb;
	int offset;

	skb = netdev_alloc_skb(netdev, OWL_EMAC_RX_FRAME_MAX_LEN +
			       OWL_EMAC_SKB_RESERVE);
	if (unlikely(!skb))
		return NULL;

	/* Ensure 4 bytes DMA alignment. */
	offset = ((uintptr_t)skb->data) & (OWL_EMAC_SKB_ALIGN - 1);
	if (unlikely(offset))
		skb_reserve(skb, OWL_EMAC_SKB_ALIGN - offset);

	return skb;
}

static int owl_emac_ring_prepare_rx(struct owl_emac_priv *priv)
{
	struct owl_emac_ring *ring = &priv->rx_ring;
	struct device *dev = owl_emac_get_dev(priv);
	struct net_device *netdev = priv->netdev;
	struct owl_emac_ring_desc *desc;
	struct sk_buff *skb;
	dma_addr_t dma_addr;
	int i;

	for (i = 0; i < ring->size; i++) {
		skb = owl_emac_alloc_skb(netdev);
		if (!skb)
			return -ENOMEM;

		dma_addr = owl_emac_dma_map_rx(priv, skb);
		if (dma_mapping_error(dev, dma_addr)) {
			dev_kfree_skb(skb);
			return -ENOMEM;
		}

		desc = &ring->descs[i];
		desc->status = OWL_EMAC_BIT_RDES0_OWN;
		desc->control = skb_tailroom(skb) & OWL_EMAC_MSK_RDES1_RBS1;
		desc->buf_addr = dma_addr;
		desc->reserved = 0;

		ring->skbs[i] = skb;
		ring->skbs_dma[i] = dma_addr;
	}

	desc->control |= OWL_EMAC_BIT_RDES1_RER;

	ring->head = 0;
	ring->tail = 0;

	return 0;
}

static void owl_emac_ring_prepare_tx(struct owl_emac_priv *priv)
{
	struct owl_emac_ring *ring = &priv->tx_ring;
	struct owl_emac_ring_desc *desc;
	int i;

	for (i = 0; i < ring->size; i++) {
		desc = &ring->descs[i];

		desc->status = 0;
		desc->control = OWL_EMAC_BIT_TDES1_IC;
		desc->buf_addr = 0;
		desc->reserved = 0;
	}

	desc->control |= OWL_EMAC_BIT_TDES1_TER;

	memset(ring->skbs_dma, 0, sizeof(dma_addr_t) * ring->size);

	ring->head = 0;
	ring->tail = 0;
}

static void owl_emac_ring_unprepare_rx(struct owl_emac_priv *priv)
{
	struct owl_emac_ring *ring = &priv->rx_ring;
	int i;

	for (i = 0; i < ring->size; i++) {
		ring->descs[i].status = 0;

		if (!ring->skbs_dma[i])
			continue;

		owl_emac_dma_unmap_rx(priv, ring->skbs[i], ring->skbs_dma[i]);
		ring->skbs_dma[i] = 0;

		dev_kfree_skb(ring->skbs[i]);
		ring->skbs[i] = NULL;
	}
}

static void owl_emac_ring_unprepare_tx(struct owl_emac_priv *priv)
{
	struct owl_emac_ring *ring = &priv->tx_ring;
	int i;

	for (i = 0; i < ring->size; i++) {
		ring->descs[i].status = 0;

		if (!ring->skbs_dma[i])
			continue;

		owl_emac_dma_unmap_tx(priv, ring->skbs[i], ring->skbs_dma[i]);
		ring->skbs_dma[i] = 0;

		dev_kfree_skb(ring->skbs[i]);
		ring->skbs[i] = NULL;
	}
}

static int owl_emac_ring_alloc(struct device *dev, struct owl_emac_ring *ring,
			       unsigned int size)
{
	ring->descs = dmam_alloc_coherent(dev,
					  sizeof(struct owl_emac_ring_desc) * size,
					  &ring->descs_dma, GFP_KERNEL);
	if (!ring->descs)
		return -ENOMEM;

	ring->skbs = devm_kcalloc(dev, size, sizeof(struct sk_buff *),
				  GFP_KERNEL);
	if (!ring->skbs)
		return -ENOMEM;

	ring->skbs_dma = devm_kcalloc(dev, size, sizeof(dma_addr_t),
				      GFP_KERNEL);
	if (!ring->skbs_dma)
		return -ENOMEM;

	ring->size = size;

	return 0;
}

static void owl_emac_dma_cmd_resume_rx(struct owl_emac_priv *priv)
{
	owl_emac_reg_write(priv, OWL_EMAC_REG_MAC_CSR2,
			   OWL_EMAC_VAL_MAC_CSR2_RPD);
}

static void owl_emac_dma_cmd_resume_tx(struct owl_emac_priv *priv)
{
	owl_emac_reg_write(priv, OWL_EMAC_REG_MAC_CSR1,
			   OWL_EMAC_VAL_MAC_CSR1_TPD);
}

static u32 owl_emac_dma_cmd_set_tx(struct owl_emac_priv *priv, u32 status)
{
	return owl_emac_reg_update(priv, OWL_EMAC_REG_MAC_CSR6,
				   OWL_EMAC_BIT_MAC_CSR6_ST, status);
}

static u32 owl_emac_dma_cmd_start_tx(struct owl_emac_priv *priv)
{
	return owl_emac_dma_cmd_set_tx(priv, ~0);
}

static u32 owl_emac_dma_cmd_set(struct owl_emac_priv *priv, u32 status)
{
	return owl_emac_reg_update(priv, OWL_EMAC_REG_MAC_CSR6,
				   OWL_EMAC_MSK_MAC_CSR6_STSR, status);
}

static u32 owl_emac_dma_cmd_start(struct owl_emac_priv *priv)
{
	return owl_emac_dma_cmd_set(priv, ~0);
}

static u32 owl_emac_dma_cmd_stop(struct owl_emac_priv *priv)
{
	return owl_emac_dma_cmd_set(priv, 0);
}

static void owl_emac_set_hw_mac_addr(struct net_device *netdev)
{
	struct owl_emac_priv *priv = netdev_priv(netdev);
	const u8 *mac_addr = netdev->dev_addr;
	u32 addr_high, addr_low;

	addr_high = mac_addr[0] << 8 | mac_addr[1];
	addr_low = mac_addr[2] << 24 | mac_addr[3] << 16 |
		   mac_addr[4] << 8 | mac_addr[5];

	owl_emac_reg_write(priv, OWL_EMAC_REG_MAC_CSR17, addr_high);
	owl_emac_reg_write(priv, OWL_EMAC_REG_MAC_CSR16, addr_low);
}

static void owl_emac_update_link_state(struct owl_emac_priv *priv)
{
	u32 val, status;

	if (priv->pause) {
		val = OWL_EMAC_BIT_MAC_CSR20_FCE | OWL_EMAC_BIT_MAC_CSR20_TUE;
		val |= OWL_EMAC_BIT_MAC_CSR20_TPE | OWL_EMAC_BIT_MAC_CSR20_RPE;
		val |= OWL_EMAC_BIT_MAC_CSR20_BPE;
	} else {
		val = 0;
	}

	/* Update flow control. */
	owl_emac_reg_write(priv, OWL_EMAC_REG_MAC_CSR20, val);

	val = (priv->speed == SPEED_100) ? OWL_EMAC_VAL_MAC_CSR6_SPEED_100M :
					   OWL_EMAC_VAL_MAC_CSR6_SPEED_10M;
	val <<= OWL_EMAC_OFF_MAC_CSR6_SPEED;

	if (priv->duplex == DUPLEX_FULL)
		val |= OWL_EMAC_BIT_MAC_CSR6_FD;

	spin_lock_bh(&priv->lock);

	/* Temporarily stop DMA TX & RX. */
	status = owl_emac_dma_cmd_stop(priv);

	/* Update operation modes. */
	owl_emac_reg_update(priv, OWL_EMAC_REG_MAC_CSR6,
			    OWL_EMAC_MSK_MAC_CSR6_SPEED |
			    OWL_EMAC_BIT_MAC_CSR6_FD, val);

	/* Restore DMA TX & RX status. */
	owl_emac_dma_cmd_set(priv, status);

	spin_unlock_bh(&priv->lock);
}

static void owl_emac_adjust_link(struct net_device *netdev)
{
	struct owl_emac_priv *priv = netdev_priv(netdev);
	struct phy_device *phydev = netdev->phydev;
	bool state_changed = false;

	if (phydev->link) {
		if (!priv->link) {
			priv->link = phydev->link;
			state_changed = true;
		}

		if (priv->speed != phydev->speed) {
			priv->speed = phydev->speed;
			state_changed = true;
		}

		if (priv->duplex != phydev->duplex) {
			priv->duplex = phydev->duplex;
			state_changed = true;
		}

		if (priv->pause != phydev->pause) {
			priv->pause = phydev->pause;
			state_changed = true;
		}
	} else {
		if (priv->link) {
			priv->link = phydev->link;
			state_changed = true;
		}
	}

	if (state_changed) {
		if (phydev->link)
			owl_emac_update_link_state(priv);

		if (netif_msg_link(priv))
			phy_print_status(phydev);
	}
}

static irqreturn_t owl_emac_handle_irq(int irq, void *data)
{
	struct net_device *netdev = data;
	struct owl_emac_priv *priv = netdev_priv(netdev);

	if (netif_running(netdev)) {
		owl_emac_irq_disable(priv);
		napi_schedule(&priv->napi);
	}

	return IRQ_HANDLED;
}

static void owl_emac_ether_addr_push(u8 **dst, const u8 *src)
{
	u32 *a = (u32 *)(*dst);
	const u16 *b = (const u16 *)src;

	a[0] = b[0];
	a[1] = b[1];
	a[2] = b[2];

	*dst += 12;
}

static void
owl_emac_setup_frame_prepare(struct owl_emac_priv *priv, struct sk_buff *skb)
{
	const u8 bcast_addr[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
	const u8 *mac_addr = priv->netdev->dev_addr;
	u8 *frame;
	int i;

	skb_put(skb, OWL_EMAC_SETUP_FRAME_LEN);

	frame = skb->data;
	memset(frame, 0, skb->len);

	owl_emac_ether_addr_push(&frame, mac_addr);
	owl_emac_ether_addr_push(&frame, bcast_addr);

	/* Fill multicast addresses. */
	WARN_ON(priv->mcaddr_list.count >= OWL_EMAC_MAX_MULTICAST_ADDRS);
	for (i = 0; i < priv->mcaddr_list.count; i++) {
		mac_addr = priv->mcaddr_list.addrs[i];
		owl_emac_ether_addr_push(&frame, mac_addr);
	}
}

/* The setup frame is a special descriptor which is used to provide physical
 * addresses (i.e. mac, broadcast and multicast) to the MAC hardware for
 * filtering purposes. To be recognized as a setup frame, the TDES1_SET bit
 * must be set in the TX descriptor control field.
 */
static int owl_emac_setup_frame_xmit(struct owl_emac_priv *priv)
{
	struct owl_emac_ring *ring = &priv->tx_ring;
	struct net_device *netdev = priv->netdev;
	struct owl_emac_ring_desc *desc;
	struct sk_buff *skb;
	unsigned int tx_head;
	u32 status, control;
	dma_addr_t dma_addr;
	int ret;

	skb = owl_emac_alloc_skb(netdev);
	if (!skb)
		return -ENOMEM;

	owl_emac_setup_frame_prepare(priv, skb);

	dma_addr = owl_emac_dma_map_tx(priv, skb);
	if (dma_mapping_error(owl_emac_get_dev(priv), dma_addr)) {
		ret = -ENOMEM;
		goto err_free_skb;
	}

	spin_lock_bh(&priv->lock);

	tx_head = ring->head;
	desc = &ring->descs[tx_head];

	status = READ_ONCE(desc->status);
	control = READ_ONCE(desc->control);
	dma_rmb(); /* Ensure data has been read before used. */

	if (unlikely(status & OWL_EMAC_BIT_TDES0_OWN) ||
	    !owl_emac_ring_num_unused(ring)) {
		spin_unlock_bh(&priv->lock);
		owl_emac_dma_unmap_tx(priv, skb, dma_addr);
		ret = -EBUSY;
		goto err_free_skb;
	}

	ring->skbs[tx_head] = skb;
	ring->skbs_dma[tx_head] = dma_addr;

	control &= OWL_EMAC_BIT_TDES1_IC | OWL_EMAC_BIT_TDES1_TER; /* Maintain bits */
	control |= OWL_EMAC_BIT_TDES1_SET;
	control |= OWL_EMAC_MSK_TDES1_TBS1 & skb->len;

	WRITE_ONCE(desc->control, control);
	WRITE_ONCE(desc->buf_addr, dma_addr);
	dma_wmb(); /* Flush descriptor before changing ownership. */
	WRITE_ONCE(desc->status, OWL_EMAC_BIT_TDES0_OWN);

	owl_emac_ring_push_head(ring);

	/* Temporarily enable DMA TX. */
	status = owl_emac_dma_cmd_start_tx(priv);

	/* Trigger setup frame processing. */
	owl_emac_dma_cmd_resume_tx(priv);

	/* Restore DMA TX status. */
	owl_emac_dma_cmd_set_tx(priv, status);

	/* Stop regular TX until setup frame is processed. */
	netif_stop_queue(netdev);

	spin_unlock_bh(&priv->lock);

	return 0;

err_free_skb:
	dev_kfree_skb(skb);
	return ret;
}

static netdev_tx_t owl_emac_ndo_start_xmit(struct sk_buff *skb,
					   struct net_device *netdev)
{
	struct owl_emac_priv *priv = netdev_priv(netdev);
	struct device *dev = owl_emac_get_dev(priv);
	struct owl_emac_ring *ring = &priv->tx_ring;
	struct owl_emac_ring_desc *desc;
	unsigned int tx_head;
	u32 status, control;
	dma_addr_t dma_addr;

	dma_addr = owl_emac_dma_map_tx(priv, skb);
	if (dma_mapping_error(dev, dma_addr)) {
		dev_err_ratelimited(&netdev->dev, "TX DMA mapping failed\n");
		dev_kfree_skb(skb);
		netdev->stats.tx_dropped++;
		return NETDEV_TX_OK;
	}

	spin_lock_bh(&priv->lock);

	tx_head = ring->head;
	desc = &ring->descs[tx_head];

	status = READ_ONCE(desc->status);
	control = READ_ONCE(desc->control);
	dma_rmb(); /* Ensure data has been read before used. */

	if (!owl_emac_ring_num_unused(ring) ||
	    unlikely(status & OWL_EMAC_BIT_TDES0_OWN)) {
		netif_stop_queue(netdev);
		spin_unlock_bh(&priv->lock);

		dev_dbg_ratelimited(&netdev->dev, "TX buffer full, status=0x%08x\n",
				    owl_emac_irq_status(priv));
		owl_emac_dma_unmap_tx(priv, skb, dma_addr);
		netdev->stats.tx_dropped++;
		return NETDEV_TX_BUSY;
	}

	ring->skbs[tx_head] = skb;
	ring->skbs_dma[tx_head] = dma_addr;

	control &= OWL_EMAC_BIT_TDES1_IC | OWL_EMAC_BIT_TDES1_TER; /* Maintain bits */
	control |= OWL_EMAC_BIT_TDES1_FS | OWL_EMAC_BIT_TDES1_LS;
	control |= OWL_EMAC_MSK_TDES1_TBS1 & skb->len;

	WRITE_ONCE(desc->control, control);
	WRITE_ONCE(desc->buf_addr, dma_addr);
	dma_wmb(); /* Flush descriptor before changing ownership. */
	WRITE_ONCE(desc->status, OWL_EMAC_BIT_TDES0_OWN);

	owl_emac_dma_cmd_resume_tx(priv);
	owl_emac_ring_push_head(ring);

	/* FIXME: The transmission is currently restricted to a single frame
	 * at a time as a workaround for a MAC hardware bug that causes random
	 * freeze of the TX queue processor.
	 */
	netif_stop_queue(netdev);

	spin_unlock_bh(&priv->lock);

	return NETDEV_TX_OK;
}

static bool owl_emac_tx_complete_tail(struct owl_emac_priv *priv)
{
	struct owl_emac_ring *ring = &priv->tx_ring;
	struct net_device *netdev = priv->netdev;
	struct owl_emac_ring_desc *desc;
	struct sk_buff *skb;
	unsigned int tx_tail;
	u32 status;

	tx_tail = ring->tail;
	desc = &ring->descs[tx_tail];

	status = READ_ONCE(desc->status);
	dma_rmb(); /* Ensure data has been read before used. */

	if (status & OWL_EMAC_BIT_TDES0_OWN)
		return false;

	/* Check for errors. */
	if (status & OWL_EMAC_BIT_TDES0_ES) {
		dev_dbg_ratelimited(&netdev->dev,
				    "TX complete error status: 0x%08x\n",
				    status);

		netdev->stats.tx_errors++;

		if (status & OWL_EMAC_BIT_TDES0_UF)
			netdev->stats.tx_fifo_errors++;

		if (status & OWL_EMAC_BIT_TDES0_EC)
			netdev->stats.tx_aborted_errors++;

		if (status & OWL_EMAC_BIT_TDES0_LC)
			netdev->stats.tx_window_errors++;

		if (status & OWL_EMAC_BIT_TDES0_NC)
			netdev->stats.tx_heartbeat_errors++;

		if (status & OWL_EMAC_BIT_TDES0_LO)
			netdev->stats.tx_carrier_errors++;
	} else {
		netdev->stats.tx_packets++;
		netdev->stats.tx_bytes += ring->skbs[tx_tail]->len;
	}

	/* Some collisions occurred, but pkt has been transmitted. */
	if (status & OWL_EMAC_BIT_TDES0_DE)
		netdev->stats.collisions++;

	skb = ring->skbs[tx_tail];
	owl_emac_dma_unmap_tx(priv, skb, ring->skbs_dma[tx_tail]);
	dev_kfree_skb(skb);

	ring->skbs[tx_tail] = NULL;
	ring->skbs_dma[tx_tail] = 0;

	owl_emac_ring_pop_tail(ring);

	if (unlikely(netif_queue_stopped(netdev)))
		netif_wake_queue(netdev);

	return true;
}

static void owl_emac_tx_complete(struct owl_emac_priv *priv)
{
	struct owl_emac_ring *ring = &priv->tx_ring;
	struct net_device *netdev = priv->netdev;
	unsigned int tx_next;
	u32 status;

	spin_lock(&priv->lock);

	while (ring->tail != ring->head) {
		if (!owl_emac_tx_complete_tail(priv))
			break;
	}

	/* FIXME: This is a workaround for a MAC hardware bug not clearing
	 * (sometimes) the OWN bit for a transmitted frame descriptor.
	 *
	 * At this point, when TX queue is full, the tail descriptor has the
	 * OWN bit set, which normally means the frame has not been processed
	 * or transmitted yet. But if there is at least one descriptor in the
	 * queue having the OWN bit cleared, we can safely assume the tail
	 * frame has been also processed by the MAC hardware.
	 *
	 * If that's the case, let's force the frame completion by manually
	 * clearing the OWN bit.
	 */
	if (unlikely(!owl_emac_ring_num_unused(ring))) {
		tx_next = ring->tail;

		while ((tx_next = owl_emac_ring_get_next(ring, tx_next)) != ring->head) {
			status = READ_ONCE(ring->descs[tx_next].status);
			dma_rmb(); /* Ensure data has been read before used. */

			if (status & OWL_EMAC_BIT_TDES0_OWN)
				continue;

			netdev_dbg(netdev, "Found uncleared TX desc OWN bit\n");

			status = READ_ONCE(ring->descs[ring->tail].status);
			dma_rmb(); /* Ensure data has been read before used. */
			status &= ~OWL_EMAC_BIT_TDES0_OWN;
			WRITE_ONCE(ring->descs[ring->tail].status, status);

			owl_emac_tx_complete_tail(priv);
			break;
		}
	}

	spin_unlock(&priv->lock);
}

static int owl_emac_rx_process(struct owl_emac_priv *priv, int budget)
{
	struct owl_emac_ring *ring = &priv->rx_ring;
	struct device *dev = owl_emac_get_dev(priv);
	struct net_device *netdev = priv->netdev;
	struct owl_emac_ring_desc *desc;
	struct sk_buff *curr_skb, *new_skb;
	dma_addr_t curr_dma, new_dma;
	unsigned int rx_tail, len;
	u32 status;
	int recv = 0;

	while (recv < budget) {
		spin_lock(&priv->lock);

		rx_tail = ring->tail;
		desc = &ring->descs[rx_tail];

		status = READ_ONCE(desc->status);
		dma_rmb(); /* Ensure data has been read before used. */

		if (status & OWL_EMAC_BIT_RDES0_OWN) {
			spin_unlock(&priv->lock);
			break;
		}

		curr_skb = ring->skbs[rx_tail];
		curr_dma = ring->skbs_dma[rx_tail];
		owl_emac_ring_pop_tail(ring);

		spin_unlock(&priv->lock);

		if (status & (OWL_EMAC_BIT_RDES0_DE | OWL_EMAC_BIT_RDES0_RF |
		    OWL_EMAC_BIT_RDES0_TL | OWL_EMAC_BIT_RDES0_CS |
		    OWL_EMAC_BIT_RDES0_DB | OWL_EMAC_BIT_RDES0_CE |
		    OWL_EMAC_BIT_RDES0_ZERO)) {
			dev_dbg_ratelimited(&netdev->dev,
					    "RX desc error status: 0x%08x\n",
					    status);

			if (status & OWL_EMAC_BIT_RDES0_DE)
				netdev->stats.rx_over_errors++;

			if (status & (OWL_EMAC_BIT_RDES0_RF | OWL_EMAC_BIT_RDES0_DB))
				netdev->stats.rx_frame_errors++;

			if (status & OWL_EMAC_BIT_RDES0_TL)
				netdev->stats.rx_length_errors++;

			if (status & OWL_EMAC_BIT_RDES0_CS)
				netdev->stats.collisions++;

			if (status & OWL_EMAC_BIT_RDES0_CE)
				netdev->stats.rx_crc_errors++;

			if (status & OWL_EMAC_BIT_RDES0_ZERO)
				netdev->stats.rx_fifo_errors++;

			goto drop_skb;
		}

		len = (status & OWL_EMAC_MSK_RDES0_FL) >> OWL_EMAC_OFF_RDES0_FL;
		if (unlikely(len > OWL_EMAC_RX_FRAME_MAX_LEN)) {
			netdev->stats.rx_length_errors++;
			netdev_err(netdev, "invalid RX frame len: %u\n", len);
			goto drop_skb;
		}

		/* Prepare new skb before receiving the current one. */
		new_skb = owl_emac_alloc_skb(netdev);
		if (unlikely(!new_skb))
			goto drop_skb;

		new_dma = owl_emac_dma_map_rx(priv, new_skb);
		if (dma_mapping_error(dev, new_dma)) {
			dev_kfree_skb(new_skb);
			netdev_err(netdev, "RX DMA mapping failed\n");
			goto drop_skb;
		}

		owl_emac_dma_unmap_rx(priv, curr_skb, curr_dma);

		skb_put(curr_skb, len - ETH_FCS_LEN);
		curr_skb->ip_summed = CHECKSUM_NONE;
		curr_skb->protocol = eth_type_trans(curr_skb, netdev);
		curr_skb->dev = netdev;

		netif_receive_skb(curr_skb);

		netdev->stats.rx_packets++;
		netdev->stats.rx_bytes += len;
		recv++;
		goto push_skb;

drop_skb:
		netdev->stats.rx_dropped++;
		netdev->stats.rx_errors++;
		/* Reuse the current skb. */
		new_skb = curr_skb;
		new_dma = curr_dma;

push_skb:
		spin_lock(&priv->lock);

		ring->skbs[ring->head] = new_skb;
		ring->skbs_dma[ring->head] = new_dma;

		WRITE_ONCE(desc->buf_addr, new_dma);
		dma_wmb(); /* Flush descriptor before changing ownership. */
		WRITE_ONCE(desc->status, OWL_EMAC_BIT_RDES0_OWN);

		owl_emac_ring_push_head(ring);

		spin_unlock(&priv->lock);
	}

	return recv;
}

static int owl_emac_poll(struct napi_struct *napi, int budget)
{
	int work_done = 0, ru_cnt = 0, recv;
	static int tx_err_cnt, rx_err_cnt;
	struct owl_emac_priv *priv;
	u32 status, proc_status;

	priv = container_of(napi, struct owl_emac_priv, napi);

	while ((status = owl_emac_irq_clear(priv)) &
	       (OWL_EMAC_BIT_MAC_CSR5_NIS | OWL_EMAC_BIT_MAC_CSR5_AIS)) {
		recv = 0;

		/* TX setup frame raises ETI instead of TI. */
		if (status & (OWL_EMAC_BIT_MAC_CSR5_TI | OWL_EMAC_BIT_MAC_CSR5_ETI)) {
			owl_emac_tx_complete(priv);
			tx_err_cnt = 0;

			/* Count MAC internal RX errors. */
			proc_status = status & OWL_EMAC_MSK_MAC_CSR5_RS;
			proc_status >>= OWL_EMAC_OFF_MAC_CSR5_RS;
			if (proc_status == OWL_EMAC_VAL_MAC_CSR5_RS_DATA ||
			    proc_status == OWL_EMAC_VAL_MAC_CSR5_RS_CDES ||
			    proc_status == OWL_EMAC_VAL_MAC_CSR5_RS_FDES)
				rx_err_cnt++;
		}

		if (status & OWL_EMAC_BIT_MAC_CSR5_RI) {
			recv = owl_emac_rx_process(priv, budget - work_done);
			rx_err_cnt = 0;

			/* Count MAC internal TX errors. */
			proc_status = status & OWL_EMAC_MSK_MAC_CSR5_TS;
			proc_status >>= OWL_EMAC_OFF_MAC_CSR5_TS;
			if (proc_status == OWL_EMAC_VAL_MAC_CSR5_TS_DATA ||
			    proc_status == OWL_EMAC_VAL_MAC_CSR5_TS_CDES)
				tx_err_cnt++;
		} else if (status & OWL_EMAC_BIT_MAC_CSR5_RU) {
			/* MAC AHB is in suspended state, will return to RX
			 * descriptor processing when the host changes ownership
			 * of the descriptor and either an RX poll demand CMD is
			 * issued or a new frame is recognized by the MAC AHB.
			 */
			if (++ru_cnt == 2)
				owl_emac_dma_cmd_resume_rx(priv);

			recv = owl_emac_rx_process(priv, budget - work_done);

			/* Guard against too many RU interrupts. */
			if (ru_cnt > 3)
				break;
		}

		work_done += recv;
		if (work_done >= budget)
			break;
	}

	if (work_done < budget) {
		napi_complete_done(napi, work_done);
		owl_emac_irq_enable(priv);
	}

	/* Reset MAC when getting too many internal TX or RX errors. */
	if (tx_err_cnt > 10 || rx_err_cnt > 10) {
		netdev_dbg(priv->netdev, "%s error status: 0x%08x\n",
			   tx_err_cnt > 10 ? "TX" : "RX", status);
		rx_err_cnt = 0;
		tx_err_cnt = 0;
		schedule_work(&priv->mac_reset_task);
	}

	return work_done;
}

static void owl_emac_mdio_clock_enable(struct owl_emac_priv *priv)
{
	u32 val;

	/* Enable MDC clock generation by adjusting CLKDIV according to
	 * the vendor implementation of the original driver.
	 */
	val = owl_emac_reg_read(priv, OWL_EMAC_REG_MAC_CSR10);
	val &= OWL_EMAC_MSK_MAC_CSR10_CLKDIV;
	val |= OWL_EMAC_VAL_MAC_CSR10_CLKDIV_128 << OWL_EMAC_OFF_MAC_CSR10_CLKDIV;

	val |= OWL_EMAC_BIT_MAC_CSR10_SB;
	val |= OWL_EMAC_VAL_MAC_CSR10_OPCODE_CDS << OWL_EMAC_OFF_MAC_CSR10_OPCODE;
	owl_emac_reg_write(priv, OWL_EMAC_REG_MAC_CSR10, val);
}

static void owl_emac_core_hw_reset(struct owl_emac_priv *priv)
{
	/* Trigger hardware reset. */
	reset_control_assert(priv->reset);
	usleep_range(10, 20);
	reset_control_deassert(priv->reset);
	usleep_range(100, 200);
}

static int owl_emac_core_sw_reset(struct owl_emac_priv *priv)
{
	u32 val;
	int ret;

	/* Trigger software reset. */
	owl_emac_reg_set(priv, OWL_EMAC_REG_MAC_CSR0, OWL_EMAC_BIT_MAC_CSR0_SWR);
	ret = readl_poll_timeout(priv->base + OWL_EMAC_REG_MAC_CSR0,
				 val, !(val & OWL_EMAC_BIT_MAC_CSR0_SWR),
				 OWL_EMAC_POLL_DELAY_USEC,
				 OWL_EMAC_RESET_POLL_TIMEOUT_USEC);
	if (ret)
		return ret;

	if (priv->phy_mode == PHY_INTERFACE_MODE_RMII) {
		/* Enable RMII and use the 50MHz rmii clk as output to PHY. */
		val = 0;
	} else {
		/* Enable SMII and use the 125MHz rmii clk as output to PHY.
		 * Additionally set SMII SYNC delay to 4 half cycle.
		 */
		val = 0x04 << OWL_EMAC_OFF_MAC_CTRL_SSDC;
		val |= OWL_EMAC_BIT_MAC_CTRL_RSIS;
	}
	owl_emac_reg_write(priv, OWL_EMAC_REG_MAC_CTRL, val);

	/* MDC is disabled after reset. */
	owl_emac_mdio_clock_enable(priv);

	/* Set FIFO pause & restart threshold levels. */
	val = 0x40 << OWL_EMAC_OFF_MAC_CSR19_FPTL;
	val |= 0x10 << OWL_EMAC_OFF_MAC_CSR19_FRTL;
	owl_emac_reg_write(priv, OWL_EMAC_REG_MAC_CSR19, val);

	/* Set flow control pause quanta time to ~100 ms. */
	val = 0x4FFF << OWL_EMAC_OFF_MAC_CSR18_PQT;
	owl_emac_reg_write(priv, OWL_EMAC_REG_MAC_CSR18, val);

	/* Setup interrupt mitigation. */
	val = 7 << OWL_EMAC_OFF_MAC_CSR11_NRP;
	val |= 4 << OWL_EMAC_OFF_MAC_CSR11_RT;
	owl_emac_reg_write(priv, OWL_EMAC_REG_MAC_CSR11, val);

	/* Set RX/TX rings base addresses. */
	owl_emac_reg_write(priv, OWL_EMAC_REG_MAC_CSR3,
			   (u32)(priv->rx_ring.descs_dma));
	owl_emac_reg_write(priv, OWL_EMAC_REG_MAC_CSR4,
			   (u32)(priv->tx_ring.descs_dma));

	/* Setup initial operation mode. */
	val = OWL_EMAC_VAL_MAC_CSR6_SPEED_100M << OWL_EMAC_OFF_MAC_CSR6_SPEED;
	val |= OWL_EMAC_BIT_MAC_CSR6_FD;
	owl_emac_reg_update(priv, OWL_EMAC_REG_MAC_CSR6,
			    OWL_EMAC_MSK_MAC_CSR6_SPEED |
			    OWL_EMAC_BIT_MAC_CSR6_FD, val);
	owl_emac_reg_clear(priv, OWL_EMAC_REG_MAC_CSR6,
			   OWL_EMAC_BIT_MAC_CSR6_PR | OWL_EMAC_BIT_MAC_CSR6_PM);

	priv->link = 0;
	priv->speed = SPEED_UNKNOWN;
	priv->duplex = DUPLEX_UNKNOWN;
	priv->pause = 0;
	priv->mcaddr_list.count = 0;

	return 0;
}

static int owl_emac_enable(struct net_device *netdev, bool start_phy)
{
	struct owl_emac_priv *priv = netdev_priv(netdev);
	int ret;

	owl_emac_dma_cmd_stop(priv);
	owl_emac_irq_disable(priv);
	owl_emac_irq_clear(priv);

	owl_emac_ring_prepare_tx(priv);
	ret = owl_emac_ring_prepare_rx(priv);
	if (ret)
		goto err_unprep;

	ret = owl_emac_core_sw_reset(priv);
	if (ret) {
		netdev_err(netdev, "failed to soft reset MAC core: %d\n", ret);
		goto err_unprep;
	}

	owl_emac_set_hw_mac_addr(netdev);
	owl_emac_setup_frame_xmit(priv);

	netdev_reset_queue(netdev);
	napi_enable(&priv->napi);

	owl_emac_irq_enable(priv);
	owl_emac_dma_cmd_start(priv);

	if (start_phy)
		phy_start(netdev->phydev);

	netif_start_queue(netdev);

	return 0;

err_unprep:
	owl_emac_ring_unprepare_rx(priv);
	owl_emac_ring_unprepare_tx(priv);

	return ret;
}

static void owl_emac_disable(struct net_device *netdev, bool stop_phy)
{
	struct owl_emac_priv *priv = netdev_priv(netdev);

	owl_emac_dma_cmd_stop(priv);
	owl_emac_irq_disable(priv);

	netif_stop_queue(netdev);
	napi_disable(&priv->napi);

	if (stop_phy)
		phy_stop(netdev->phydev);

	owl_emac_ring_unprepare_rx(priv);
	owl_emac_ring_unprepare_tx(priv);
}

static int owl_emac_ndo_open(struct net_device *netdev)
{
	return owl_emac_enable(netdev, true);
}

static int owl_emac_ndo_stop(struct net_device *netdev)
{
	owl_emac_disable(netdev, true);

	return 0;
}

static void owl_emac_set_multicast(struct net_device *netdev, int count)
{
	struct owl_emac_priv *priv = netdev_priv(netdev);
	struct netdev_hw_addr *ha;
	int index = 0;

	if (count <= 0) {
		priv->mcaddr_list.count = 0;
		return;
	}

	netdev_for_each_mc_addr(ha, netdev) {
		if (!is_multicast_ether_addr(ha->addr))
			continue;

		WARN_ON(index >= OWL_EMAC_MAX_MULTICAST_ADDRS);
		ether_addr_copy(priv->mcaddr_list.addrs[index++], ha->addr);
	}

	priv->mcaddr_list.count = index;

	owl_emac_setup_frame_xmit(priv);
}

static void owl_emac_ndo_set_rx_mode(struct net_device *netdev)
{
	struct owl_emac_priv *priv = netdev_priv(netdev);
	u32 status, val = 0;
	int mcast_count = 0;

	if (netdev->flags & IFF_PROMISC) {
		val = OWL_EMAC_BIT_MAC_CSR6_PR;
	} else if (netdev->flags & IFF_ALLMULTI) {
		val = OWL_EMAC_BIT_MAC_CSR6_PM;
	} else if (netdev->flags & IFF_MULTICAST) {
		mcast_count = netdev_mc_count(netdev);

		if (mcast_count > OWL_EMAC_MAX_MULTICAST_ADDRS) {
			val = OWL_EMAC_BIT_MAC_CSR6_PM;
			mcast_count = 0;
		}
	}

	spin_lock_bh(&priv->lock);

	/* Temporarily stop DMA TX & RX. */
	status = owl_emac_dma_cmd_stop(priv);

	/* Update operation modes. */
	owl_emac_reg_update(priv, OWL_EMAC_REG_MAC_CSR6,
			    OWL_EMAC_BIT_MAC_CSR6_PR | OWL_EMAC_BIT_MAC_CSR6_PM,
			    val);

	/* Restore DMA TX & RX status. */
	owl_emac_dma_cmd_set(priv, status);

	spin_unlock_bh(&priv->lock);

	/* Set/reset multicast addr list. */
	owl_emac_set_multicast(netdev, mcast_count);
}

static int owl_emac_ndo_set_mac_addr(struct net_device *netdev, void *addr)
{
	struct sockaddr *skaddr = addr;

	if (!is_valid_ether_addr(skaddr->sa_data))
		return -EADDRNOTAVAIL;

	if (netif_running(netdev))
		return -EBUSY;

	eth_hw_addr_set(netdev, skaddr->sa_data);
	owl_emac_set_hw_mac_addr(netdev);

	return owl_emac_setup_frame_xmit(netdev_priv(netdev));
}

static int owl_emac_ndo_eth_ioctl(struct net_device *netdev,
				  struct ifreq *req, int cmd)
{
	if (!netif_running(netdev))
		return -EINVAL;

	return phy_mii_ioctl(netdev->phydev, req, cmd);
}

static void owl_emac_ndo_tx_timeout(struct net_device *netdev,
				    unsigned int txqueue)
{
	struct owl_emac_priv *priv = netdev_priv(netdev);

	schedule_work(&priv->mac_reset_task);
}

static void owl_emac_reset_task(struct work_struct *work)
{
	struct owl_emac_priv *priv;

	priv = container_of(work, struct owl_emac_priv, mac_reset_task);

	netdev_dbg(priv->netdev, "resetting MAC\n");
	owl_emac_disable(priv->netdev, false);
	owl_emac_enable(priv->netdev, false);
}

static struct net_device_stats *
owl_emac_ndo_get_stats(struct net_device *netdev)
{
	/* FIXME: If possible, try to get stats from MAC hardware registers
	 * instead of tracking them manually in the driver.
	 */

	return &netdev->stats;
}

static const struct net_device_ops owl_emac_netdev_ops = {
	.ndo_open		= owl_emac_ndo_open,
	.ndo_stop		= owl_emac_ndo_stop,
	.ndo_start_xmit		= owl_emac_ndo_start_xmit,
	.ndo_set_rx_mode	= owl_emac_ndo_set_rx_mode,
	.ndo_set_mac_address	= owl_emac_ndo_set_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_eth_ioctl		= owl_emac_ndo_eth_ioctl,
	.ndo_tx_timeout         = owl_emac_ndo_tx_timeout,
	.ndo_get_stats		= owl_emac_ndo_get_stats,
};

static void owl_emac_ethtool_get_drvinfo(struct net_device *dev,
					 struct ethtool_drvinfo *info)
{
	strscpy(info->driver, OWL_EMAC_DRVNAME, sizeof(info->driver));
}

static u32 owl_emac_ethtool_get_msglevel(struct net_device *netdev)
{
	struct owl_emac_priv *priv = netdev_priv(netdev);

	return priv->msg_enable;
}

static void owl_emac_ethtool_set_msglevel(struct net_device *ndev, u32 val)
{
	struct owl_emac_priv *priv = netdev_priv(ndev);

	priv->msg_enable = val;
}

static const struct ethtool_ops owl_emac_ethtool_ops = {
	.get_drvinfo		= owl_emac_ethtool_get_drvinfo,
	.get_link		= ethtool_op_get_link,
	.get_link_ksettings	= phy_ethtool_get_link_ksettings,
	.set_link_ksettings	= phy_ethtool_set_link_ksettings,
	.get_msglevel		= owl_emac_ethtool_get_msglevel,
	.set_msglevel		= owl_emac_ethtool_set_msglevel,
};

static int owl_emac_mdio_wait(struct owl_emac_priv *priv)
{
	u32 val;

	/* Wait while data transfer is in progress. */
	return readl_poll_timeout(priv->base + OWL_EMAC_REG_MAC_CSR10,
				  val, !(val & OWL_EMAC_BIT_MAC_CSR10_SB),
				  OWL_EMAC_POLL_DELAY_USEC,
				  OWL_EMAC_MDIO_POLL_TIMEOUT_USEC);
}

static int owl_emac_mdio_read(struct mii_bus *bus, int addr, int regnum)
{
	struct owl_emac_priv *priv = bus->priv;
	u32 data, tmp;
	int ret;

	data = OWL_EMAC_BIT_MAC_CSR10_SB;
	data |= OWL_EMAC_VAL_MAC_CSR10_OPCODE_RD << OWL_EMAC_OFF_MAC_CSR10_OPCODE;

	tmp = addr << OWL_EMAC_OFF_MAC_CSR10_PHYADD;
	data |= tmp & OWL_EMAC_MSK_MAC_CSR10_PHYADD;

	tmp = regnum << OWL_EMAC_OFF_MAC_CSR10_REGADD;
	data |= tmp & OWL_EMAC_MSK_MAC_CSR10_REGADD;

	owl_emac_reg_write(priv, OWL_EMAC_REG_MAC_CSR10, data);

	ret = owl_emac_mdio_wait(priv);
	if (ret)
		return ret;

	data = owl_emac_reg_read(priv, OWL_EMAC_REG_MAC_CSR10);
	data &= OWL_EMAC_MSK_MAC_CSR10_DATA;

	return data;
}

static int
owl_emac_mdio_write(struct mii_bus *bus, int addr, int regnum, u16 val)
{
	struct owl_emac_priv *priv = bus->priv;
	u32 data, tmp;

	data = OWL_EMAC_BIT_MAC_CSR10_SB;
	data |= OWL_EMAC_VAL_MAC_CSR10_OPCODE_WR << OWL_EMAC_OFF_MAC_CSR10_OPCODE;

	tmp = addr << OWL_EMAC_OFF_MAC_CSR10_PHYADD;
	data |= tmp & OWL_EMAC_MSK_MAC_CSR10_PHYADD;

	tmp = regnum << OWL_EMAC_OFF_MAC_CSR10_REGADD;
	data |= tmp & OWL_EMAC_MSK_MAC_CSR10_REGADD;

	data |= val & OWL_EMAC_MSK_MAC_CSR10_DATA;

	owl_emac_reg_write(priv, OWL_EMAC_REG_MAC_CSR10, data);

	return owl_emac_mdio_wait(priv);
}

static int owl_emac_mdio_init(struct net_device *netdev)
{
	struct owl_emac_priv *priv = netdev_priv(netdev);
	struct device *dev = owl_emac_get_dev(priv);
	struct device_node *mdio_node;
	int ret;

	mdio_node = of_get_child_by_name(dev->of_node, "mdio");
	if (!mdio_node)
		return -ENODEV;

	if (!of_device_is_available(mdio_node)) {
		ret = -ENODEV;
		goto err_put_node;
	}

	priv->mii = devm_mdiobus_alloc(dev);
	if (!priv->mii) {
		ret = -ENOMEM;
		goto err_put_node;
	}

	snprintf(priv->mii->id, MII_BUS_ID_SIZE, "%s", dev_name(dev));
	priv->mii->name = "owl-emac-mdio";
	priv->mii->parent = dev;
	priv->mii->read = owl_emac_mdio_read;
	priv->mii->write = owl_emac_mdio_write;
	priv->mii->phy_mask = ~0; /* Mask out all PHYs from auto probing. */
	priv->mii->priv = priv;

	ret = devm_of_mdiobus_register(dev, priv->mii, mdio_node);

err_put_node:
	of_node_put(mdio_node);
	return ret;
}

static int owl_emac_phy_init(struct net_device *netdev)
{
	struct owl_emac_priv *priv = netdev_priv(netdev);
	struct device *dev = owl_emac_get_dev(priv);
	struct phy_device *phy;

	phy = of_phy_get_and_connect(netdev, dev->of_node,
				     owl_emac_adjust_link);
	if (!phy)
		return -ENODEV;

	phy_set_sym_pause(phy, true, true, true);

	if (netif_msg_link(priv))
		phy_attached_info(phy);

	return 0;
}

static void owl_emac_get_mac_addr(struct net_device *netdev)
{
	struct device *dev = netdev->dev.parent;
	int ret;

	ret = platform_get_ethdev_address(dev, netdev);
	if (!ret && is_valid_ether_addr(netdev->dev_addr))
		return;

	eth_hw_addr_random(netdev);
	dev_warn(dev, "using random MAC address %pM\n", netdev->dev_addr);
}

static __maybe_unused int owl_emac_suspend(struct device *dev)
{
	struct net_device *netdev = dev_get_drvdata(dev);
	struct owl_emac_priv *priv = netdev_priv(netdev);

	disable_irq(netdev->irq);

	if (netif_running(netdev)) {
		owl_emac_disable(netdev, true);
		netif_device_detach(netdev);
	}

	clk_bulk_disable_unprepare(OWL_EMAC_NCLKS, priv->clks);

	return 0;
}

static __maybe_unused int owl_emac_resume(struct device *dev)
{
	struct net_device *netdev = dev_get_drvdata(dev);
	struct owl_emac_priv *priv = netdev_priv(netdev);
	int ret;

	ret = clk_bulk_prepare_enable(OWL_EMAC_NCLKS, priv->clks);
	if (ret)
		return ret;

	if (netif_running(netdev)) {
		owl_emac_core_hw_reset(priv);
		owl_emac_core_sw_reset(priv);

		ret = owl_emac_enable(netdev, true);
		if (ret) {
			clk_bulk_disable_unprepare(OWL_EMAC_NCLKS, priv->clks);
			return ret;
		}

		netif_device_attach(netdev);
	}

	enable_irq(netdev->irq);

	return 0;
}

static void owl_emac_clk_disable_unprepare(void *data)
{
	struct owl_emac_priv *priv = data;

	clk_bulk_disable_unprepare(OWL_EMAC_NCLKS, priv->clks);
}

static int owl_emac_clk_set_rate(struct owl_emac_priv *priv)
{
	struct device *dev = owl_emac_get_dev(priv);
	unsigned long rate;
	int ret;

	switch (priv->phy_mode) {
	case PHY_INTERFACE_MODE_RMII:
		rate = 50000000;
		break;

	case PHY_INTERFACE_MODE_SMII:
		rate = 125000000;
		break;

	default:
		dev_err(dev, "unsupported phy interface mode %d\n",
			priv->phy_mode);
		return -EOPNOTSUPP;
	}

	ret = clk_set_rate(priv->clks[OWL_EMAC_CLK_RMII].clk, rate);
	if (ret)
		dev_err(dev, "failed to set RMII clock rate: %d\n", ret);

	return ret;
}

static int owl_emac_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct net_device *netdev;
	struct owl_emac_priv *priv;
	int ret, i;

	netdev = devm_alloc_etherdev(dev, sizeof(*priv));
	if (!netdev)
		return -ENOMEM;

	platform_set_drvdata(pdev, netdev);
	SET_NETDEV_DEV(netdev, dev);

	priv = netdev_priv(netdev);
	priv->netdev = netdev;
	priv->msg_enable = netif_msg_init(-1, OWL_EMAC_DEFAULT_MSG_ENABLE);

	ret = of_get_phy_mode(dev->of_node, &priv->phy_mode);
	if (ret) {
		dev_err(dev, "failed to get phy mode: %d\n", ret);
		return ret;
	}

	spin_lock_init(&priv->lock);

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(dev, "unsupported DMA mask\n");
		return ret;
	}

	ret = owl_emac_ring_alloc(dev, &priv->rx_ring, OWL_EMAC_RX_RING_SIZE);
	if (ret)
		return ret;

	ret = owl_emac_ring_alloc(dev, &priv->tx_ring, OWL_EMAC_TX_RING_SIZE);
	if (ret)
		return ret;

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	netdev->irq = platform_get_irq(pdev, 0);
	if (netdev->irq < 0)
		return netdev->irq;

	ret = devm_request_irq(dev, netdev->irq, owl_emac_handle_irq,
			       IRQF_SHARED, netdev->name, netdev);
	if (ret) {
		dev_err(dev, "failed to request irq: %d\n", netdev->irq);
		return ret;
	}

	for (i = 0; i < OWL_EMAC_NCLKS; i++)
		priv->clks[i].id = owl_emac_clk_names[i];

	ret = devm_clk_bulk_get(dev, OWL_EMAC_NCLKS, priv->clks);
	if (ret)
		return ret;

	ret = clk_bulk_prepare_enable(OWL_EMAC_NCLKS, priv->clks);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(dev, owl_emac_clk_disable_unprepare, priv);
	if (ret)
		return ret;

	ret = owl_emac_clk_set_rate(priv);
	if (ret)
		return ret;

	priv->reset = devm_reset_control_get_exclusive(dev, NULL);
	if (IS_ERR(priv->reset))
		return dev_err_probe(dev, PTR_ERR(priv->reset),
				     "failed to get reset control");

	owl_emac_get_mac_addr(netdev);

	owl_emac_core_hw_reset(priv);
	owl_emac_mdio_clock_enable(priv);

	ret = owl_emac_mdio_init(netdev);
	if (ret) {
		dev_err(dev, "failed to initialize MDIO bus\n");
		return ret;
	}

	ret = owl_emac_phy_init(netdev);
	if (ret) {
		dev_err(dev, "failed to initialize PHY\n");
		return ret;
	}

	INIT_WORK(&priv->mac_reset_task, owl_emac_reset_task);

	netdev->min_mtu = OWL_EMAC_MTU_MIN;
	netdev->max_mtu = OWL_EMAC_MTU_MAX;
	netdev->watchdog_timeo = OWL_EMAC_TX_TIMEOUT;
	netdev->netdev_ops = &owl_emac_netdev_ops;
	netdev->ethtool_ops = &owl_emac_ethtool_ops;
	netif_napi_add(netdev, &priv->napi, owl_emac_poll);

	ret = devm_register_netdev(dev, netdev);
	if (ret) {
		netif_napi_del(&priv->napi);
		phy_disconnect(netdev->phydev);
		return ret;
	}

	return 0;
}

static int owl_emac_remove(struct platform_device *pdev)
{
	struct owl_emac_priv *priv = platform_get_drvdata(pdev);

	netif_napi_del(&priv->napi);
	phy_disconnect(priv->netdev->phydev);
	cancel_work_sync(&priv->mac_reset_task);

	return 0;
}

static const struct of_device_id owl_emac_of_match[] = {
	{ .compatible = "actions,owl-emac", },
	{ }
};
MODULE_DEVICE_TABLE(of, owl_emac_of_match);

static SIMPLE_DEV_PM_OPS(owl_emac_pm_ops,
			 owl_emac_suspend, owl_emac_resume);

static struct platform_driver owl_emac_driver = {
	.driver = {
		.name = OWL_EMAC_DRVNAME,
		.of_match_table = owl_emac_of_match,
		.pm = &owl_emac_pm_ops,
	},
	.probe = owl_emac_probe,
	.remove = owl_emac_remove,
};
module_platform_driver(owl_emac_driver);

MODULE_DESCRIPTION("Actions Semi Owl SoCs Ethernet MAC Driver");
MODULE_AUTHOR("Actions Semi Inc.");
MODULE_AUTHOR("Cristian Ciocaltea <cristian.ciocaltea@gmail.com>");
MODULE_LICENSE("GPL");
