/*
 * Copyright (C) 2015 Mans Rullgard <mans@mansr.com>
 *
 * Mostly rewritten, based on driver from Sigma Designs.  Original
 * copyright notice below.
 *
 *
 * Driver for tangox SMP864x/SMP865x/SMP867x/SMP868x builtin Ethernet Mac.
 *
 * Copyright (C) 2005 Maxime Bizon <mbizon@freebox.fr>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>
#include <linux/ethtool.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/dma-mapping.h>
#include <linux/phy.h>
#include <linux/cache.h>
#include <linux/jiffies.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <asm/barrier.h>

#include "nb8800.h"

static void nb8800_tx_done(struct net_device *dev);
static int nb8800_dma_stop(struct net_device *dev);

static inline u8 nb8800_readb(struct nb8800_priv *priv, int reg)
{
	return readb_relaxed(priv->base + reg);
}

static inline u32 nb8800_readl(struct nb8800_priv *priv, int reg)
{
	return readl_relaxed(priv->base + reg);
}

static inline void nb8800_writeb(struct nb8800_priv *priv, int reg, u8 val)
{
	writeb_relaxed(val, priv->base + reg);
}

static inline void nb8800_writew(struct nb8800_priv *priv, int reg, u16 val)
{
	writew_relaxed(val, priv->base + reg);
}

static inline void nb8800_writel(struct nb8800_priv *priv, int reg, u32 val)
{
	writel_relaxed(val, priv->base + reg);
}

static inline void nb8800_maskb(struct nb8800_priv *priv, int reg,
				u32 mask, u32 val)
{
	u32 old = nb8800_readb(priv, reg);
	u32 new = (old & ~mask) | (val & mask);

	if (new != old)
		nb8800_writeb(priv, reg, new);
}

static inline void nb8800_maskl(struct nb8800_priv *priv, int reg,
				u32 mask, u32 val)
{
	u32 old = nb8800_readl(priv, reg);
	u32 new = (old & ~mask) | (val & mask);

	if (new != old)
		nb8800_writel(priv, reg, new);
}

static inline void nb8800_modb(struct nb8800_priv *priv, int reg, u8 bits,
			       bool set)
{
	nb8800_maskb(priv, reg, bits, set ? bits : 0);
}

static inline void nb8800_setb(struct nb8800_priv *priv, int reg, u8 bits)
{
	nb8800_maskb(priv, reg, bits, bits);
}

static inline void nb8800_clearb(struct nb8800_priv *priv, int reg, u8 bits)
{
	nb8800_maskb(priv, reg, bits, 0);
}

static inline void nb8800_modl(struct nb8800_priv *priv, int reg, u32 bits,
			       bool set)
{
	nb8800_maskl(priv, reg, bits, set ? bits : 0);
}

static inline void nb8800_setl(struct nb8800_priv *priv, int reg, u32 bits)
{
	nb8800_maskl(priv, reg, bits, bits);
}

static inline void nb8800_clearl(struct nb8800_priv *priv, int reg, u32 bits)
{
	nb8800_maskl(priv, reg, bits, 0);
}

static int nb8800_mdio_wait(struct mii_bus *bus)
{
	struct nb8800_priv *priv = bus->priv;
	u32 val;

	return readl_poll_timeout_atomic(priv->base + NB8800_MDIO_CMD,
					 val, !(val & MDIO_CMD_GO), 1, 1000);
}

static int nb8800_mdio_cmd(struct mii_bus *bus, u32 cmd)
{
	struct nb8800_priv *priv = bus->priv;
	int err;

	err = nb8800_mdio_wait(bus);
	if (err)
		return err;

	nb8800_writel(priv, NB8800_MDIO_CMD, cmd);
	udelay(10);
	nb8800_writel(priv, NB8800_MDIO_CMD, cmd | MDIO_CMD_GO);

	return nb8800_mdio_wait(bus);
}

static int nb8800_mdio_read(struct mii_bus *bus, int phy_id, int reg)
{
	struct nb8800_priv *priv = bus->priv;
	u32 val;
	int err;

	err = nb8800_mdio_cmd(bus, MDIO_CMD_ADDR(phy_id) | MDIO_CMD_REG(reg));
	if (err)
		return err;

	val = nb8800_readl(priv, NB8800_MDIO_STS);
	if (val & MDIO_STS_ERR)
		return 0xffff;

	return val & 0xffff;
}

static int nb8800_mdio_write(struct mii_bus *bus, int phy_id, int reg, u16 val)
{
	u32 cmd = MDIO_CMD_ADDR(phy_id) | MDIO_CMD_REG(reg) |
		MDIO_CMD_DATA(val) | MDIO_CMD_WR;

	return nb8800_mdio_cmd(bus, cmd);
}

static void nb8800_mac_tx(struct net_device *dev, bool enable)
{
	struct nb8800_priv *priv = netdev_priv(dev);

	while (nb8800_readl(priv, NB8800_TXC_CR) & TCR_EN)
		cpu_relax();

	nb8800_modb(priv, NB8800_TX_CTL1, TX_EN, enable);
}

static void nb8800_mac_rx(struct net_device *dev, bool enable)
{
	nb8800_modb(netdev_priv(dev), NB8800_RX_CTL, RX_EN, enable);
}

static void nb8800_mac_af(struct net_device *dev, bool enable)
{
	nb8800_modb(netdev_priv(dev), NB8800_RX_CTL, RX_AF_EN, enable);
}

static void nb8800_start_rx(struct net_device *dev)
{
	nb8800_setl(netdev_priv(dev), NB8800_RXC_CR, RCR_EN);
}

static int nb8800_alloc_rx(struct net_device *dev, unsigned int i, bool napi)
{
	struct nb8800_priv *priv = netdev_priv(dev);
	struct nb8800_rx_desc *rxd = &priv->rx_descs[i];
	struct nb8800_rx_buf *rxb = &priv->rx_bufs[i];
	int size = L1_CACHE_ALIGN(RX_BUF_SIZE);
	dma_addr_t dma_addr;
	struct page *page;
	unsigned long offset;
	void *data;

	data = napi ? napi_alloc_frag(size) : netdev_alloc_frag(size);
	if (!data)
		return -ENOMEM;

	page = virt_to_head_page(data);
	offset = data - page_address(page);

	dma_addr = dma_map_page(&dev->dev, page, offset, RX_BUF_SIZE,
				DMA_FROM_DEVICE);

	if (dma_mapping_error(&dev->dev, dma_addr)) {
		skb_free_frag(data);
		return -ENOMEM;
	}

	rxb->page = page;
	rxb->offset = offset;
	rxd->desc.s_addr = dma_addr;

	return 0;
}

static void nb8800_receive(struct net_device *dev, unsigned int i,
			   unsigned int len)
{
	struct nb8800_priv *priv = netdev_priv(dev);
	struct nb8800_rx_desc *rxd = &priv->rx_descs[i];
	struct page *page = priv->rx_bufs[i].page;
	int offset = priv->rx_bufs[i].offset;
	void *data = page_address(page) + offset;
	dma_addr_t dma = rxd->desc.s_addr;
	struct sk_buff *skb;
	unsigned int size;
	int err;

	size = len <= RX_COPYBREAK ? len : RX_COPYHDR;

	skb = napi_alloc_skb(&priv->napi, size);
	if (!skb) {
		netdev_err(dev, "rx skb allocation failed\n");
		dev->stats.rx_dropped++;
		return;
	}

	if (len <= RX_COPYBREAK) {
		dma_sync_single_for_cpu(&dev->dev, dma, len, DMA_FROM_DEVICE);
		skb_put_data(skb, data, len);
		dma_sync_single_for_device(&dev->dev, dma, len,
					   DMA_FROM_DEVICE);
	} else {
		err = nb8800_alloc_rx(dev, i, true);
		if (err) {
			netdev_err(dev, "rx buffer allocation failed\n");
			dev->stats.rx_dropped++;
			dev_kfree_skb(skb);
			return;
		}

		dma_unmap_page(&dev->dev, dma, RX_BUF_SIZE, DMA_FROM_DEVICE);
		skb_put_data(skb, data, RX_COPYHDR);
		skb_add_rx_frag(skb, skb_shinfo(skb)->nr_frags, page,
				offset + RX_COPYHDR, len - RX_COPYHDR,
				RX_BUF_SIZE);
	}

	skb->protocol = eth_type_trans(skb, dev);
	napi_gro_receive(&priv->napi, skb);
}

static void nb8800_rx_error(struct net_device *dev, u32 report)
{
	if (report & RX_LENGTH_ERR)
		dev->stats.rx_length_errors++;

	if (report & RX_FCS_ERR)
		dev->stats.rx_crc_errors++;

	if (report & RX_FIFO_OVERRUN)
		dev->stats.rx_fifo_errors++;

	if (report & RX_ALIGNMENT_ERROR)
		dev->stats.rx_frame_errors++;

	dev->stats.rx_errors++;
}

static int nb8800_poll(struct napi_struct *napi, int budget)
{
	struct net_device *dev = napi->dev;
	struct nb8800_priv *priv = netdev_priv(dev);
	struct nb8800_rx_desc *rxd;
	unsigned int last = priv->rx_eoc;
	unsigned int next;
	int work = 0;

	nb8800_tx_done(dev);

again:
	do {
		unsigned int len;

		next = (last + 1) % RX_DESC_COUNT;

		rxd = &priv->rx_descs[next];

		if (!rxd->report)
			break;

		len = RX_BYTES_TRANSFERRED(rxd->report);

		if (IS_RX_ERROR(rxd->report))
			nb8800_rx_error(dev, rxd->report);
		else
			nb8800_receive(dev, next, len);

		dev->stats.rx_packets++;
		dev->stats.rx_bytes += len;

		if (rxd->report & RX_MULTICAST_PKT)
			dev->stats.multicast++;

		rxd->report = 0;
		last = next;
		work++;
	} while (work < budget);

	if (work) {
		priv->rx_descs[last].desc.config |= DESC_EOC;
		wmb();	/* ensure new EOC is written before clearing old */
		priv->rx_descs[priv->rx_eoc].desc.config &= ~DESC_EOC;
		priv->rx_eoc = last;
		nb8800_start_rx(dev);
	}

	if (work < budget) {
		nb8800_writel(priv, NB8800_RX_ITR, priv->rx_itr_irq);

		/* If a packet arrived after we last checked but
		 * before writing RX_ITR, the interrupt will be
		 * delayed, so we retrieve it now.
		 */
		if (priv->rx_descs[next].report)
			goto again;

		napi_complete_done(napi, work);
	}

	return work;
}

static void __nb8800_tx_dma_start(struct net_device *dev)
{
	struct nb8800_priv *priv = netdev_priv(dev);
	struct nb8800_tx_buf *txb;
	u32 txc_cr;

	txb = &priv->tx_bufs[priv->tx_queue];
	if (!txb->ready)
		return;

	txc_cr = nb8800_readl(priv, NB8800_TXC_CR);
	if (txc_cr & TCR_EN)
		return;

	nb8800_writel(priv, NB8800_TX_DESC_ADDR, txb->dma_desc);
	wmb();		/* ensure desc addr is written before starting DMA */
	nb8800_writel(priv, NB8800_TXC_CR, txc_cr | TCR_EN);

	priv->tx_queue = (priv->tx_queue + txb->chain_len) % TX_DESC_COUNT;
}

static void nb8800_tx_dma_start(struct net_device *dev)
{
	struct nb8800_priv *priv = netdev_priv(dev);

	spin_lock_irq(&priv->tx_lock);
	__nb8800_tx_dma_start(dev);
	spin_unlock_irq(&priv->tx_lock);
}

static void nb8800_tx_dma_start_irq(struct net_device *dev)
{
	struct nb8800_priv *priv = netdev_priv(dev);

	spin_lock(&priv->tx_lock);
	__nb8800_tx_dma_start(dev);
	spin_unlock(&priv->tx_lock);
}

static int nb8800_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct nb8800_priv *priv = netdev_priv(dev);
	struct nb8800_tx_desc *txd;
	struct nb8800_tx_buf *txb;
	struct nb8800_dma_desc *desc;
	dma_addr_t dma_addr;
	unsigned int dma_len;
	unsigned int align;
	unsigned int next;
	bool xmit_more;

	if (atomic_read(&priv->tx_free) <= NB8800_DESC_LOW) {
		netif_stop_queue(dev);
		return NETDEV_TX_BUSY;
	}

	align = (8 - (uintptr_t)skb->data) & 7;

	dma_len = skb->len - align;
	dma_addr = dma_map_single(&dev->dev, skb->data + align,
				  dma_len, DMA_TO_DEVICE);

	if (dma_mapping_error(&dev->dev, dma_addr)) {
		netdev_err(dev, "tx dma mapping error\n");
		kfree_skb(skb);
		dev->stats.tx_dropped++;
		return NETDEV_TX_OK;
	}

	xmit_more = netdev_xmit_more();
	if (atomic_dec_return(&priv->tx_free) <= NB8800_DESC_LOW) {
		netif_stop_queue(dev);
		xmit_more = false;
	}

	next = priv->tx_next;
	txb = &priv->tx_bufs[next];
	txd = &priv->tx_descs[next];
	desc = &txd->desc[0];

	next = (next + 1) % TX_DESC_COUNT;

	if (align) {
		memcpy(txd->buf, skb->data, align);

		desc->s_addr =
			txb->dma_desc + offsetof(struct nb8800_tx_desc, buf);
		desc->n_addr = txb->dma_desc + sizeof(txd->desc[0]);
		desc->config = DESC_BTS(2) | DESC_DS | align;

		desc++;
	}

	desc->s_addr = dma_addr;
	desc->n_addr = priv->tx_bufs[next].dma_desc;
	desc->config = DESC_BTS(2) | DESC_DS | DESC_EOF | dma_len;

	if (!xmit_more)
		desc->config |= DESC_EOC;

	txb->skb = skb;
	txb->dma_addr = dma_addr;
	txb->dma_len = dma_len;

	if (!priv->tx_chain) {
		txb->chain_len = 1;
		priv->tx_chain = txb;
	} else {
		priv->tx_chain->chain_len++;
	}

	netdev_sent_queue(dev, skb->len);

	priv->tx_next = next;

	if (!xmit_more) {
		smp_wmb();
		priv->tx_chain->ready = true;
		priv->tx_chain = NULL;
		nb8800_tx_dma_start(dev);
	}

	return NETDEV_TX_OK;
}

static void nb8800_tx_error(struct net_device *dev, u32 report)
{
	if (report & TX_LATE_COLLISION)
		dev->stats.collisions++;

	if (report & TX_PACKET_DROPPED)
		dev->stats.tx_dropped++;

	if (report & TX_FIFO_UNDERRUN)
		dev->stats.tx_fifo_errors++;

	dev->stats.tx_errors++;
}

static void nb8800_tx_done(struct net_device *dev)
{
	struct nb8800_priv *priv = netdev_priv(dev);
	unsigned int limit = priv->tx_next;
	unsigned int done = priv->tx_done;
	unsigned int packets = 0;
	unsigned int len = 0;

	while (done != limit) {
		struct nb8800_tx_desc *txd = &priv->tx_descs[done];
		struct nb8800_tx_buf *txb = &priv->tx_bufs[done];
		struct sk_buff *skb;

		if (!txd->report)
			break;

		skb = txb->skb;
		len += skb->len;

		dma_unmap_single(&dev->dev, txb->dma_addr, txb->dma_len,
				 DMA_TO_DEVICE);

		if (IS_TX_ERROR(txd->report)) {
			nb8800_tx_error(dev, txd->report);
			kfree_skb(skb);
		} else {
			consume_skb(skb);
		}

		dev->stats.tx_packets++;
		dev->stats.tx_bytes += TX_BYTES_TRANSFERRED(txd->report);
		dev->stats.collisions += TX_EARLY_COLLISIONS(txd->report);

		txb->skb = NULL;
		txb->ready = false;
		txd->report = 0;

		done = (done + 1) % TX_DESC_COUNT;
		packets++;
	}

	if (packets) {
		smp_mb__before_atomic();
		atomic_add(packets, &priv->tx_free);
		netdev_completed_queue(dev, packets, len);
		netif_wake_queue(dev);
		priv->tx_done = done;
	}
}

static irqreturn_t nb8800_irq(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct nb8800_priv *priv = netdev_priv(dev);
	irqreturn_t ret = IRQ_NONE;
	u32 val;

	/* tx interrupt */
	val = nb8800_readl(priv, NB8800_TXC_SR);
	if (val) {
		nb8800_writel(priv, NB8800_TXC_SR, val);

		if (val & TSR_DI)
			nb8800_tx_dma_start_irq(dev);

		if (val & TSR_TI)
			napi_schedule_irqoff(&priv->napi);

		if (unlikely(val & TSR_DE))
			netdev_err(dev, "TX DMA error\n");

		/* should never happen with automatic status retrieval */
		if (unlikely(val & TSR_TO))
			netdev_err(dev, "TX Status FIFO overflow\n");

		ret = IRQ_HANDLED;
	}

	/* rx interrupt */
	val = nb8800_readl(priv, NB8800_RXC_SR);
	if (val) {
		nb8800_writel(priv, NB8800_RXC_SR, val);

		if (likely(val & (RSR_RI | RSR_DI))) {
			nb8800_writel(priv, NB8800_RX_ITR, priv->rx_itr_poll);
			napi_schedule_irqoff(&priv->napi);
		}

		if (unlikely(val & RSR_DE))
			netdev_err(dev, "RX DMA error\n");

		/* should never happen with automatic status retrieval */
		if (unlikely(val & RSR_RO))
			netdev_err(dev, "RX Status FIFO overflow\n");

		ret = IRQ_HANDLED;
	}

	return ret;
}

static void nb8800_mac_config(struct net_device *dev)
{
	struct nb8800_priv *priv = netdev_priv(dev);
	bool gigabit = priv->speed == SPEED_1000;
	u32 mac_mode_mask = RGMII_MODE | HALF_DUPLEX | GMAC_MODE;
	u32 mac_mode = 0;
	u32 slot_time;
	u32 phy_clk;
	u32 ict;

	if (!priv->duplex)
		mac_mode |= HALF_DUPLEX;

	if (gigabit) {
		if (phy_interface_is_rgmii(dev->phydev))
			mac_mode |= RGMII_MODE;

		mac_mode |= GMAC_MODE;
		phy_clk = 125000000;

		/* Should be 512 but register is only 8 bits */
		slot_time = 255;
	} else {
		phy_clk = 25000000;
		slot_time = 128;
	}

	ict = DIV_ROUND_UP(phy_clk, clk_get_rate(priv->clk));

	nb8800_writeb(priv, NB8800_IC_THRESHOLD, ict);
	nb8800_writeb(priv, NB8800_SLOT_TIME, slot_time);
	nb8800_maskb(priv, NB8800_MAC_MODE, mac_mode_mask, mac_mode);
}

static void nb8800_pause_config(struct net_device *dev)
{
	struct nb8800_priv *priv = netdev_priv(dev);
	struct phy_device *phydev = dev->phydev;
	u32 rxcr;

	if (priv->pause_aneg) {
		if (!phydev || !phydev->link)
			return;

		priv->pause_rx = phydev->pause;
		priv->pause_tx = phydev->pause ^ phydev->asym_pause;
	}

	nb8800_modb(priv, NB8800_RX_CTL, RX_PAUSE_EN, priv->pause_rx);

	rxcr = nb8800_readl(priv, NB8800_RXC_CR);
	if (!!(rxcr & RCR_FL) == priv->pause_tx)
		return;

	if (netif_running(dev)) {
		napi_disable(&priv->napi);
		netif_tx_lock_bh(dev);
		nb8800_dma_stop(dev);
		nb8800_modl(priv, NB8800_RXC_CR, RCR_FL, priv->pause_tx);
		nb8800_start_rx(dev);
		netif_tx_unlock_bh(dev);
		napi_enable(&priv->napi);
	} else {
		nb8800_modl(priv, NB8800_RXC_CR, RCR_FL, priv->pause_tx);
	}
}

static void nb8800_link_reconfigure(struct net_device *dev)
{
	struct nb8800_priv *priv = netdev_priv(dev);
	struct phy_device *phydev = dev->phydev;
	int change = 0;

	if (phydev->link) {
		if (phydev->speed != priv->speed) {
			priv->speed = phydev->speed;
			change = 1;
		}

		if (phydev->duplex != priv->duplex) {
			priv->duplex = phydev->duplex;
			change = 1;
		}

		if (change)
			nb8800_mac_config(dev);

		nb8800_pause_config(dev);
	}

	if (phydev->link != priv->link) {
		priv->link = phydev->link;
		change = 1;
	}

	if (change)
		phy_print_status(phydev);
}

static void nb8800_update_mac_addr(struct net_device *dev)
{
	struct nb8800_priv *priv = netdev_priv(dev);
	int i;

	for (i = 0; i < ETH_ALEN; i++)
		nb8800_writeb(priv, NB8800_SRC_ADDR(i), dev->dev_addr[i]);

	for (i = 0; i < ETH_ALEN; i++)
		nb8800_writeb(priv, NB8800_UC_ADDR(i), dev->dev_addr[i]);
}

static int nb8800_set_mac_address(struct net_device *dev, void *addr)
{
	struct sockaddr *sock = addr;

	if (netif_running(dev))
		return -EBUSY;

	ether_addr_copy(dev->dev_addr, sock->sa_data);
	nb8800_update_mac_addr(dev);

	return 0;
}

static void nb8800_mc_init(struct net_device *dev, int val)
{
	struct nb8800_priv *priv = netdev_priv(dev);

	nb8800_writeb(priv, NB8800_MC_INIT, val);
	readb_poll_timeout_atomic(priv->base + NB8800_MC_INIT, val, !val,
				  1, 1000);
}

static void nb8800_set_rx_mode(struct net_device *dev)
{
	struct nb8800_priv *priv = netdev_priv(dev);
	struct netdev_hw_addr *ha;
	int i;

	if (dev->flags & (IFF_PROMISC | IFF_ALLMULTI)) {
		nb8800_mac_af(dev, false);
		return;
	}

	nb8800_mac_af(dev, true);
	nb8800_mc_init(dev, 0);

	netdev_for_each_mc_addr(ha, dev) {
		for (i = 0; i < ETH_ALEN; i++)
			nb8800_writeb(priv, NB8800_MC_ADDR(i), ha->addr[i]);

		nb8800_mc_init(dev, 0xff);
	}
}

#define RX_DESC_SIZE (RX_DESC_COUNT * sizeof(struct nb8800_rx_desc))
#define TX_DESC_SIZE (TX_DESC_COUNT * sizeof(struct nb8800_tx_desc))

static void nb8800_dma_free(struct net_device *dev)
{
	struct nb8800_priv *priv = netdev_priv(dev);
	unsigned int i;

	if (priv->rx_bufs) {
		for (i = 0; i < RX_DESC_COUNT; i++)
			if (priv->rx_bufs[i].page)
				put_page(priv->rx_bufs[i].page);

		kfree(priv->rx_bufs);
		priv->rx_bufs = NULL;
	}

	if (priv->tx_bufs) {
		for (i = 0; i < TX_DESC_COUNT; i++)
			kfree_skb(priv->tx_bufs[i].skb);

		kfree(priv->tx_bufs);
		priv->tx_bufs = NULL;
	}

	if (priv->rx_descs) {
		dma_free_coherent(dev->dev.parent, RX_DESC_SIZE, priv->rx_descs,
				  priv->rx_desc_dma);
		priv->rx_descs = NULL;
	}

	if (priv->tx_descs) {
		dma_free_coherent(dev->dev.parent, TX_DESC_SIZE, priv->tx_descs,
				  priv->tx_desc_dma);
		priv->tx_descs = NULL;
	}
}

static void nb8800_dma_reset(struct net_device *dev)
{
	struct nb8800_priv *priv = netdev_priv(dev);
	struct nb8800_rx_desc *rxd;
	struct nb8800_tx_desc *txd;
	unsigned int i;

	for (i = 0; i < RX_DESC_COUNT; i++) {
		dma_addr_t rx_dma = priv->rx_desc_dma + i * sizeof(*rxd);

		rxd = &priv->rx_descs[i];
		rxd->desc.n_addr = rx_dma + sizeof(*rxd);
		rxd->desc.r_addr =
			rx_dma + offsetof(struct nb8800_rx_desc, report);
		rxd->desc.config = priv->rx_dma_config;
		rxd->report = 0;
	}

	rxd->desc.n_addr = priv->rx_desc_dma;
	rxd->desc.config |= DESC_EOC;

	priv->rx_eoc = RX_DESC_COUNT - 1;

	for (i = 0; i < TX_DESC_COUNT; i++) {
		struct nb8800_tx_buf *txb = &priv->tx_bufs[i];
		dma_addr_t r_dma = txb->dma_desc +
			offsetof(struct nb8800_tx_desc, report);

		txd = &priv->tx_descs[i];
		txd->desc[0].r_addr = r_dma;
		txd->desc[1].r_addr = r_dma;
		txd->report = 0;
	}

	priv->tx_next = 0;
	priv->tx_queue = 0;
	priv->tx_done = 0;
	atomic_set(&priv->tx_free, TX_DESC_COUNT);

	nb8800_writel(priv, NB8800_RX_DESC_ADDR, priv->rx_desc_dma);

	wmb();		/* ensure all setup is written before starting */
}

static int nb8800_dma_init(struct net_device *dev)
{
	struct nb8800_priv *priv = netdev_priv(dev);
	unsigned int n_rx = RX_DESC_COUNT;
	unsigned int n_tx = TX_DESC_COUNT;
	unsigned int i;
	int err;

	priv->rx_descs = dma_alloc_coherent(dev->dev.parent, RX_DESC_SIZE,
					    &priv->rx_desc_dma, GFP_KERNEL);
	if (!priv->rx_descs)
		goto err_out;

	priv->rx_bufs = kcalloc(n_rx, sizeof(*priv->rx_bufs), GFP_KERNEL);
	if (!priv->rx_bufs)
		goto err_out;

	for (i = 0; i < n_rx; i++) {
		err = nb8800_alloc_rx(dev, i, false);
		if (err)
			goto err_out;
	}

	priv->tx_descs = dma_alloc_coherent(dev->dev.parent, TX_DESC_SIZE,
					    &priv->tx_desc_dma, GFP_KERNEL);
	if (!priv->tx_descs)
		goto err_out;

	priv->tx_bufs = kcalloc(n_tx, sizeof(*priv->tx_bufs), GFP_KERNEL);
	if (!priv->tx_bufs)
		goto err_out;

	for (i = 0; i < n_tx; i++)
		priv->tx_bufs[i].dma_desc =
			priv->tx_desc_dma + i * sizeof(struct nb8800_tx_desc);

	nb8800_dma_reset(dev);

	return 0;

err_out:
	nb8800_dma_free(dev);

	return -ENOMEM;
}

static int nb8800_dma_stop(struct net_device *dev)
{
	struct nb8800_priv *priv = netdev_priv(dev);
	struct nb8800_tx_buf *txb = &priv->tx_bufs[0];
	struct nb8800_tx_desc *txd = &priv->tx_descs[0];
	int retry = 5;
	u32 txcr;
	u32 rxcr;
	int err;
	unsigned int i;

	/* wait for tx to finish */
	err = readl_poll_timeout_atomic(priv->base + NB8800_TXC_CR, txcr,
					!(txcr & TCR_EN) &&
					priv->tx_done == priv->tx_next,
					1000, 1000000);
	if (err)
		return err;

	/* The rx DMA only stops if it reaches the end of chain.
	 * To make this happen, we set the EOC flag on all rx
	 * descriptors, put the device in loopback mode, and send
	 * a few dummy frames.  The interrupt handler will ignore
	 * these since NAPI is disabled and no real frames are in
	 * the tx queue.
	 */

	for (i = 0; i < RX_DESC_COUNT; i++)
		priv->rx_descs[i].desc.config |= DESC_EOC;

	txd->desc[0].s_addr =
		txb->dma_desc + offsetof(struct nb8800_tx_desc, buf);
	txd->desc[0].config = DESC_BTS(2) | DESC_DS | DESC_EOF | DESC_EOC | 8;
	memset(txd->buf, 0, sizeof(txd->buf));

	nb8800_mac_af(dev, false);
	nb8800_setb(priv, NB8800_MAC_MODE, LOOPBACK_EN);

	do {
		nb8800_writel(priv, NB8800_TX_DESC_ADDR, txb->dma_desc);
		wmb();
		nb8800_writel(priv, NB8800_TXC_CR, txcr | TCR_EN);

		err = readl_poll_timeout_atomic(priv->base + NB8800_RXC_CR,
						rxcr, !(rxcr & RCR_EN),
						1000, 100000);
	} while (err && --retry);

	nb8800_mac_af(dev, true);
	nb8800_clearb(priv, NB8800_MAC_MODE, LOOPBACK_EN);
	nb8800_dma_reset(dev);

	return retry ? 0 : -ETIMEDOUT;
}

static void nb8800_pause_adv(struct net_device *dev)
{
	struct nb8800_priv *priv = netdev_priv(dev);
	struct phy_device *phydev = dev->phydev;

	if (!phydev)
		return;

	phy_set_asym_pause(phydev, priv->pause_rx, priv->pause_tx);
}

static int nb8800_open(struct net_device *dev)
{
	struct nb8800_priv *priv = netdev_priv(dev);
	struct phy_device *phydev;
	int err;

	/* clear any pending interrupts */
	nb8800_writel(priv, NB8800_RXC_SR, 0xf);
	nb8800_writel(priv, NB8800_TXC_SR, 0xf);

	err = nb8800_dma_init(dev);
	if (err)
		return err;

	err = request_irq(dev->irq, nb8800_irq, 0, dev_name(&dev->dev), dev);
	if (err)
		goto err_free_dma;

	nb8800_mac_rx(dev, true);
	nb8800_mac_tx(dev, true);

	phydev = of_phy_connect(dev, priv->phy_node,
				nb8800_link_reconfigure, 0,
				priv->phy_mode);
	if (!phydev) {
		err = -ENODEV;
		goto err_free_irq;
	}

	nb8800_pause_adv(dev);

	netdev_reset_queue(dev);
	napi_enable(&priv->napi);
	netif_start_queue(dev);

	nb8800_start_rx(dev);
	phy_start(phydev);

	return 0;

err_free_irq:
	free_irq(dev->irq, dev);
err_free_dma:
	nb8800_dma_free(dev);

	return err;
}

static int nb8800_stop(struct net_device *dev)
{
	struct nb8800_priv *priv = netdev_priv(dev);
	struct phy_device *phydev = dev->phydev;

	phy_stop(phydev);

	netif_stop_queue(dev);
	napi_disable(&priv->napi);

	nb8800_dma_stop(dev);
	nb8800_mac_rx(dev, false);
	nb8800_mac_tx(dev, false);

	phy_disconnect(phydev);

	free_irq(dev->irq, dev);

	nb8800_dma_free(dev);

	return 0;
}

static int nb8800_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	return phy_mii_ioctl(dev->phydev, rq, cmd);
}

static const struct net_device_ops nb8800_netdev_ops = {
	.ndo_open		= nb8800_open,
	.ndo_stop		= nb8800_stop,
	.ndo_start_xmit		= nb8800_xmit,
	.ndo_set_mac_address	= nb8800_set_mac_address,
	.ndo_set_rx_mode	= nb8800_set_rx_mode,
	.ndo_do_ioctl		= nb8800_ioctl,
	.ndo_validate_addr	= eth_validate_addr,
};

static void nb8800_get_pauseparam(struct net_device *dev,
				  struct ethtool_pauseparam *pp)
{
	struct nb8800_priv *priv = netdev_priv(dev);

	pp->autoneg = priv->pause_aneg;
	pp->rx_pause = priv->pause_rx;
	pp->tx_pause = priv->pause_tx;
}

static int nb8800_set_pauseparam(struct net_device *dev,
				 struct ethtool_pauseparam *pp)
{
	struct nb8800_priv *priv = netdev_priv(dev);
	struct phy_device *phydev = dev->phydev;

	priv->pause_aneg = pp->autoneg;
	priv->pause_rx = pp->rx_pause;
	priv->pause_tx = pp->tx_pause;

	nb8800_pause_adv(dev);

	if (!priv->pause_aneg)
		nb8800_pause_config(dev);
	else if (phydev)
		phy_start_aneg(phydev);

	return 0;
}

static const char nb8800_stats_names[][ETH_GSTRING_LEN] = {
	"rx_bytes_ok",
	"rx_frames_ok",
	"rx_undersize_frames",
	"rx_fragment_frames",
	"rx_64_byte_frames",
	"rx_127_byte_frames",
	"rx_255_byte_frames",
	"rx_511_byte_frames",
	"rx_1023_byte_frames",
	"rx_max_size_frames",
	"rx_oversize_frames",
	"rx_bad_fcs_frames",
	"rx_broadcast_frames",
	"rx_multicast_frames",
	"rx_control_frames",
	"rx_pause_frames",
	"rx_unsup_control_frames",
	"rx_align_error_frames",
	"rx_overrun_frames",
	"rx_jabber_frames",
	"rx_bytes",
	"rx_frames",

	"tx_bytes_ok",
	"tx_frames_ok",
	"tx_64_byte_frames",
	"tx_127_byte_frames",
	"tx_255_byte_frames",
	"tx_511_byte_frames",
	"tx_1023_byte_frames",
	"tx_max_size_frames",
	"tx_oversize_frames",
	"tx_broadcast_frames",
	"tx_multicast_frames",
	"tx_control_frames",
	"tx_pause_frames",
	"tx_underrun_frames",
	"tx_single_collision_frames",
	"tx_multi_collision_frames",
	"tx_deferred_collision_frames",
	"tx_late_collision_frames",
	"tx_excessive_collision_frames",
	"tx_bytes",
	"tx_frames",
	"tx_collisions",
};

#define NB8800_NUM_STATS ARRAY_SIZE(nb8800_stats_names)

static int nb8800_get_sset_count(struct net_device *dev, int sset)
{
	if (sset == ETH_SS_STATS)
		return NB8800_NUM_STATS;

	return -EOPNOTSUPP;
}

static void nb8800_get_strings(struct net_device *dev, u32 sset, u8 *buf)
{
	if (sset == ETH_SS_STATS)
		memcpy(buf, &nb8800_stats_names, sizeof(nb8800_stats_names));
}

static u32 nb8800_read_stat(struct net_device *dev, int index)
{
	struct nb8800_priv *priv = netdev_priv(dev);

	nb8800_writeb(priv, NB8800_STAT_INDEX, index);

	return nb8800_readl(priv, NB8800_STAT_DATA);
}

static void nb8800_get_ethtool_stats(struct net_device *dev,
				     struct ethtool_stats *estats, u64 *st)
{
	unsigned int i;
	u32 rx, tx;

	for (i = 0; i < NB8800_NUM_STATS / 2; i++) {
		rx = nb8800_read_stat(dev, i);
		tx = nb8800_read_stat(dev, i | 0x80);
		st[i] = rx;
		st[i + NB8800_NUM_STATS / 2] = tx;
	}
}

static const struct ethtool_ops nb8800_ethtool_ops = {
	.nway_reset		= phy_ethtool_nway_reset,
	.get_link		= ethtool_op_get_link,
	.get_pauseparam		= nb8800_get_pauseparam,
	.set_pauseparam		= nb8800_set_pauseparam,
	.get_sset_count		= nb8800_get_sset_count,
	.get_strings		= nb8800_get_strings,
	.get_ethtool_stats	= nb8800_get_ethtool_stats,
	.get_link_ksettings	= phy_ethtool_get_link_ksettings,
	.set_link_ksettings	= phy_ethtool_set_link_ksettings,
};

static int nb8800_hw_init(struct net_device *dev)
{
	struct nb8800_priv *priv = netdev_priv(dev);
	u32 val;

	val = TX_RETRY_EN | TX_PAD_EN | TX_APPEND_FCS;
	nb8800_writeb(priv, NB8800_TX_CTL1, val);

	/* Collision retry count */
	nb8800_writeb(priv, NB8800_TX_CTL2, 5);

	val = RX_PAD_STRIP | RX_AF_EN;
	nb8800_writeb(priv, NB8800_RX_CTL, val);

	/* Chosen by fair dice roll */
	nb8800_writeb(priv, NB8800_RANDOM_SEED, 4);

	/* TX cycles per deferral period */
	nb8800_writeb(priv, NB8800_TX_SDP, 12);

	/* The following three threshold values have been
	 * experimentally determined for good results.
	 */

	/* RX/TX FIFO threshold for partial empty (64-bit entries) */
	nb8800_writeb(priv, NB8800_PE_THRESHOLD, 0);

	/* RX/TX FIFO threshold for partial full (64-bit entries) */
	nb8800_writeb(priv, NB8800_PF_THRESHOLD, 255);

	/* Buffer size for transmit (64-bit entries) */
	nb8800_writeb(priv, NB8800_TX_BUFSIZE, 64);

	/* Configure tx DMA */

	val = nb8800_readl(priv, NB8800_TXC_CR);
	val &= TCR_LE;		/* keep endian setting */
	val |= TCR_DM;		/* DMA descriptor mode */
	val |= TCR_RS;		/* automatically store tx status  */
	val |= TCR_DIE;		/* interrupt on DMA chain completion */
	val |= TCR_TFI(7);	/* interrupt after 7 frames transmitted */
	val |= TCR_BTS(2);	/* 32-byte bus transaction size */
	nb8800_writel(priv, NB8800_TXC_CR, val);

	/* TX complete interrupt after 10 ms or 7 frames (see above) */
	val = clk_get_rate(priv->clk) / 100;
	nb8800_writel(priv, NB8800_TX_ITR, val);

	/* Configure rx DMA */

	val = nb8800_readl(priv, NB8800_RXC_CR);
	val &= RCR_LE;		/* keep endian setting */
	val |= RCR_DM;		/* DMA descriptor mode */
	val |= RCR_RS;		/* automatically store rx status */
	val |= RCR_DIE;		/* interrupt at end of DMA chain */
	val |= RCR_RFI(7);	/* interrupt after 7 frames received */
	val |= RCR_BTS(2);	/* 32-byte bus transaction size */
	nb8800_writel(priv, NB8800_RXC_CR, val);

	/* The rx interrupt can fire before the DMA has completed
	 * unless a small delay is added.  50 us is hopefully enough.
	 */
	priv->rx_itr_irq = clk_get_rate(priv->clk) / 20000;

	/* In NAPI poll mode we want to disable interrupts, but the
	 * hardware does not permit this.  Delay 10 ms instead.
	 */
	priv->rx_itr_poll = clk_get_rate(priv->clk) / 100;

	nb8800_writel(priv, NB8800_RX_ITR, priv->rx_itr_irq);

	priv->rx_dma_config = RX_BUF_SIZE | DESC_BTS(2) | DESC_DS | DESC_EOF;

	/* Flow control settings */

	/* Pause time of 0.1 ms */
	val = 100000 / 512;
	nb8800_writeb(priv, NB8800_PQ1, val >> 8);
	nb8800_writeb(priv, NB8800_PQ2, val & 0xff);

	/* Auto-negotiate by default */
	priv->pause_aneg = true;
	priv->pause_rx = true;
	priv->pause_tx = true;

	nb8800_mc_init(dev, 0);

	return 0;
}

static int nb8800_tangox_init(struct net_device *dev)
{
	struct nb8800_priv *priv = netdev_priv(dev);
	u32 pad_mode = PAD_MODE_MII;

	switch (priv->phy_mode) {
	case PHY_INTERFACE_MODE_MII:
	case PHY_INTERFACE_MODE_GMII:
		pad_mode = PAD_MODE_MII;
		break;

	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
		pad_mode = PAD_MODE_RGMII;
		break;

	default:
		dev_err(dev->dev.parent, "unsupported phy mode %s\n",
			phy_modes(priv->phy_mode));
		return -EINVAL;
	}

	nb8800_writeb(priv, NB8800_TANGOX_PAD_MODE, pad_mode);

	return 0;
}

static int nb8800_tangox_reset(struct net_device *dev)
{
	struct nb8800_priv *priv = netdev_priv(dev);
	int clk_div;

	nb8800_writeb(priv, NB8800_TANGOX_RESET, 0);
	usleep_range(1000, 10000);
	nb8800_writeb(priv, NB8800_TANGOX_RESET, 1);

	wmb();		/* ensure reset is cleared before proceeding */

	clk_div = DIV_ROUND_UP(clk_get_rate(priv->clk), 2 * MAX_MDC_CLOCK);
	nb8800_writew(priv, NB8800_TANGOX_MDIO_CLKDIV, clk_div);

	return 0;
}

static const struct nb8800_ops nb8800_tangox_ops = {
	.init	= nb8800_tangox_init,
	.reset	= nb8800_tangox_reset,
};

static int nb8800_tango4_init(struct net_device *dev)
{
	struct nb8800_priv *priv = netdev_priv(dev);
	int err;

	err = nb8800_tangox_init(dev);
	if (err)
		return err;

	/* On tango4 interrupt on DMA completion per frame works and gives
	 * better performance despite generating more rx interrupts.
	 */

	/* Disable unnecessary interrupt on rx completion */
	nb8800_clearl(priv, NB8800_RXC_CR, RCR_RFI(7));

	/* Request interrupt on descriptor DMA completion */
	priv->rx_dma_config |= DESC_ID;

	return 0;
}

static const struct nb8800_ops nb8800_tango4_ops = {
	.init	= nb8800_tango4_init,
	.reset	= nb8800_tangox_reset,
};

static const struct of_device_id nb8800_dt_ids[] = {
	{
		.compatible = "aurora,nb8800",
	},
	{
		.compatible = "sigma,smp8642-ethernet",
		.data = &nb8800_tangox_ops,
	},
	{
		.compatible = "sigma,smp8734-ethernet",
		.data = &nb8800_tango4_ops,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, nb8800_dt_ids);

static int nb8800_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	const struct nb8800_ops *ops = NULL;
	struct nb8800_priv *priv;
	struct resource *res;
	struct net_device *dev;
	struct mii_bus *bus;
	const unsigned char *mac;
	void __iomem *base;
	int irq;
	int ret;

	match = of_match_device(nb8800_dt_ids, &pdev->dev);
	if (match)
		ops = match->data;

	irq = platform_get_irq(pdev, 0);
	if (irq <= 0) {
		dev_err(&pdev->dev, "No IRQ\n");
		return -EINVAL;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	dev_dbg(&pdev->dev, "AU-NB8800 Ethernet at %pa\n", &res->start);

	dev = alloc_etherdev(sizeof(*priv));
	if (!dev)
		return -ENOMEM;

	platform_set_drvdata(pdev, dev);
	SET_NETDEV_DEV(dev, &pdev->dev);

	priv = netdev_priv(dev);
	priv->base = base;

	priv->phy_mode = of_get_phy_mode(pdev->dev.of_node);
	if (priv->phy_mode < 0)
		priv->phy_mode = PHY_INTERFACE_MODE_RGMII;

	priv->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(priv->clk)) {
		dev_err(&pdev->dev, "failed to get clock\n");
		ret = PTR_ERR(priv->clk);
		goto err_free_dev;
	}

	ret = clk_prepare_enable(priv->clk);
	if (ret)
		goto err_free_dev;

	spin_lock_init(&priv->tx_lock);

	if (ops && ops->reset) {
		ret = ops->reset(dev);
		if (ret)
			goto err_disable_clk;
	}

	bus = devm_mdiobus_alloc(&pdev->dev);
	if (!bus) {
		ret = -ENOMEM;
		goto err_disable_clk;
	}

	bus->name = "nb8800-mii";
	bus->read = nb8800_mdio_read;
	bus->write = nb8800_mdio_write;
	bus->parent = &pdev->dev;
	snprintf(bus->id, MII_BUS_ID_SIZE, "%lx.nb8800-mii",
		 (unsigned long)res->start);
	bus->priv = priv;

	ret = of_mdiobus_register(bus, pdev->dev.of_node);
	if (ret) {
		dev_err(&pdev->dev, "failed to register MII bus\n");
		goto err_disable_clk;
	}

	if (of_phy_is_fixed_link(pdev->dev.of_node)) {
		ret = of_phy_register_fixed_link(pdev->dev.of_node);
		if (ret < 0) {
			dev_err(&pdev->dev, "bad fixed-link spec\n");
			goto err_free_bus;
		}
		priv->phy_node = of_node_get(pdev->dev.of_node);
	}

	if (!priv->phy_node)
		priv->phy_node = of_parse_phandle(pdev->dev.of_node,
						  "phy-handle", 0);

	if (!priv->phy_node) {
		dev_err(&pdev->dev, "no PHY specified\n");
		ret = -ENODEV;
		goto err_free_bus;
	}

	priv->mii_bus = bus;

	ret = nb8800_hw_init(dev);
	if (ret)
		goto err_deregister_fixed_link;

	if (ops && ops->init) {
		ret = ops->init(dev);
		if (ret)
			goto err_deregister_fixed_link;
	}

	dev->netdev_ops = &nb8800_netdev_ops;
	dev->ethtool_ops = &nb8800_ethtool_ops;
	dev->flags |= IFF_MULTICAST;
	dev->irq = irq;

	mac = of_get_mac_address(pdev->dev.of_node);
	if (mac)
		ether_addr_copy(dev->dev_addr, mac);

	if (!is_valid_ether_addr(dev->dev_addr))
		eth_hw_addr_random(dev);

	nb8800_update_mac_addr(dev);

	netif_carrier_off(dev);

	ret = register_netdev(dev);
	if (ret) {
		netdev_err(dev, "failed to register netdev\n");
		goto err_free_dma;
	}

	netif_napi_add(dev, &priv->napi, nb8800_poll, NAPI_POLL_WEIGHT);

	netdev_info(dev, "MAC address %pM\n", dev->dev_addr);

	return 0;

err_free_dma:
	nb8800_dma_free(dev);
err_deregister_fixed_link:
	if (of_phy_is_fixed_link(pdev->dev.of_node))
		of_phy_deregister_fixed_link(pdev->dev.of_node);
err_free_bus:
	of_node_put(priv->phy_node);
	mdiobus_unregister(bus);
err_disable_clk:
	clk_disable_unprepare(priv->clk);
err_free_dev:
	free_netdev(dev);

	return ret;
}

static int nb8800_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct nb8800_priv *priv = netdev_priv(ndev);

	unregister_netdev(ndev);
	if (of_phy_is_fixed_link(pdev->dev.of_node))
		of_phy_deregister_fixed_link(pdev->dev.of_node);
	of_node_put(priv->phy_node);

	mdiobus_unregister(priv->mii_bus);

	clk_disable_unprepare(priv->clk);

	nb8800_dma_free(ndev);
	free_netdev(ndev);

	return 0;
}

static struct platform_driver nb8800_driver = {
	.driver = {
		.name		= "nb8800",
		.of_match_table	= nb8800_dt_ids,
	},
	.probe	= nb8800_probe,
	.remove	= nb8800_remove,
};

module_platform_driver(nb8800_driver);

MODULE_DESCRIPTION("Aurora AU-NB8800 Ethernet driver");
MODULE_AUTHOR("Mans Rullgard <mans@mansr.com>");
MODULE_LICENSE("GPL");
