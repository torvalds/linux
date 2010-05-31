/*
 * linux/drivers/net/ethoc.c
 *
 * Copyright (C) 2007-2008 Avionic Design Development GmbH
 * Copyright (C) 2008-2009 Avionic Design GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Written by Thierry Reding <thierry.reding@avionic-design.de>
 */

#include <linux/etherdevice.h>
#include <linux/crc32.h>
#include <linux/io.h>
#include <linux/mii.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <net/ethoc.h>

static int buffer_size = 0x8000; /* 32 KBytes */
module_param(buffer_size, int, 0);
MODULE_PARM_DESC(buffer_size, "DMA buffer allocation size");

/* register offsets */
#define	MODER		0x00
#define	INT_SOURCE	0x04
#define	INT_MASK	0x08
#define	IPGT		0x0c
#define	IPGR1		0x10
#define	IPGR2		0x14
#define	PACKETLEN	0x18
#define	COLLCONF	0x1c
#define	TX_BD_NUM	0x20
#define	CTRLMODER	0x24
#define	MIIMODER	0x28
#define	MIICOMMAND	0x2c
#define	MIIADDRESS	0x30
#define	MIITX_DATA	0x34
#define	MIIRX_DATA	0x38
#define	MIISTATUS	0x3c
#define	MAC_ADDR0	0x40
#define	MAC_ADDR1	0x44
#define	ETH_HASH0	0x48
#define	ETH_HASH1	0x4c
#define	ETH_TXCTRL	0x50

/* mode register */
#define	MODER_RXEN	(1 <<  0) /* receive enable */
#define	MODER_TXEN	(1 <<  1) /* transmit enable */
#define	MODER_NOPRE	(1 <<  2) /* no preamble */
#define	MODER_BRO	(1 <<  3) /* broadcast address */
#define	MODER_IAM	(1 <<  4) /* individual address mode */
#define	MODER_PRO	(1 <<  5) /* promiscuous mode */
#define	MODER_IFG	(1 <<  6) /* interframe gap for incoming frames */
#define	MODER_LOOP	(1 <<  7) /* loopback */
#define	MODER_NBO	(1 <<  8) /* no back-off */
#define	MODER_EDE	(1 <<  9) /* excess defer enable */
#define	MODER_FULLD	(1 << 10) /* full duplex */
#define	MODER_RESET	(1 << 11) /* FIXME: reset (undocumented) */
#define	MODER_DCRC	(1 << 12) /* delayed CRC enable */
#define	MODER_CRC	(1 << 13) /* CRC enable */
#define	MODER_HUGE	(1 << 14) /* huge packets enable */
#define	MODER_PAD	(1 << 15) /* padding enabled */
#define	MODER_RSM	(1 << 16) /* receive small packets */

/* interrupt source and mask registers */
#define	INT_MASK_TXF	(1 << 0) /* transmit frame */
#define	INT_MASK_TXE	(1 << 1) /* transmit error */
#define	INT_MASK_RXF	(1 << 2) /* receive frame */
#define	INT_MASK_RXE	(1 << 3) /* receive error */
#define	INT_MASK_BUSY	(1 << 4)
#define	INT_MASK_TXC	(1 << 5) /* transmit control frame */
#define	INT_MASK_RXC	(1 << 6) /* receive control frame */

#define	INT_MASK_TX	(INT_MASK_TXF | INT_MASK_TXE)
#define	INT_MASK_RX	(INT_MASK_RXF | INT_MASK_RXE)

#define	INT_MASK_ALL ( \
		INT_MASK_TXF | INT_MASK_TXE | \
		INT_MASK_RXF | INT_MASK_RXE | \
		INT_MASK_TXC | INT_MASK_RXC | \
		INT_MASK_BUSY \
	)

/* packet length register */
#define	PACKETLEN_MIN(min)		(((min) & 0xffff) << 16)
#define	PACKETLEN_MAX(max)		(((max) & 0xffff) <<  0)
#define	PACKETLEN_MIN_MAX(min, max)	(PACKETLEN_MIN(min) | \
					PACKETLEN_MAX(max))

/* transmit buffer number register */
#define	TX_BD_NUM_VAL(x)	(((x) <= 0x80) ? (x) : 0x80)

/* control module mode register */
#define	CTRLMODER_PASSALL	(1 << 0) /* pass all receive frames */
#define	CTRLMODER_RXFLOW	(1 << 1) /* receive control flow */
#define	CTRLMODER_TXFLOW	(1 << 2) /* transmit control flow */

/* MII mode register */
#define	MIIMODER_CLKDIV(x)	((x) & 0xfe) /* needs to be an even number */
#define	MIIMODER_NOPRE		(1 << 8) /* no preamble */

/* MII command register */
#define	MIICOMMAND_SCAN		(1 << 0) /* scan status */
#define	MIICOMMAND_READ		(1 << 1) /* read status */
#define	MIICOMMAND_WRITE	(1 << 2) /* write control data */

/* MII address register */
#define	MIIADDRESS_FIAD(x)		(((x) & 0x1f) << 0)
#define	MIIADDRESS_RGAD(x)		(((x) & 0x1f) << 8)
#define	MIIADDRESS_ADDR(phy, reg)	(MIIADDRESS_FIAD(phy) | \
					MIIADDRESS_RGAD(reg))

/* MII transmit data register */
#define	MIITX_DATA_VAL(x)	((x) & 0xffff)

/* MII receive data register */
#define	MIIRX_DATA_VAL(x)	((x) & 0xffff)

/* MII status register */
#define	MIISTATUS_LINKFAIL	(1 << 0)
#define	MIISTATUS_BUSY		(1 << 1)
#define	MIISTATUS_INVALID	(1 << 2)

/* TX buffer descriptor */
#define	TX_BD_CS		(1 <<  0) /* carrier sense lost */
#define	TX_BD_DF		(1 <<  1) /* defer indication */
#define	TX_BD_LC		(1 <<  2) /* late collision */
#define	TX_BD_RL		(1 <<  3) /* retransmission limit */
#define	TX_BD_RETRY_MASK	(0x00f0)
#define	TX_BD_RETRY(x)		(((x) & 0x00f0) >>  4)
#define	TX_BD_UR		(1 <<  8) /* transmitter underrun */
#define	TX_BD_CRC		(1 << 11) /* TX CRC enable */
#define	TX_BD_PAD		(1 << 12) /* pad enable for short packets */
#define	TX_BD_WRAP		(1 << 13)
#define	TX_BD_IRQ		(1 << 14) /* interrupt request enable */
#define	TX_BD_READY		(1 << 15) /* TX buffer ready */
#define	TX_BD_LEN(x)		(((x) & 0xffff) << 16)
#define	TX_BD_LEN_MASK		(0xffff << 16)

#define	TX_BD_STATS		(TX_BD_CS | TX_BD_DF | TX_BD_LC | \
				TX_BD_RL | TX_BD_RETRY_MASK | TX_BD_UR)

/* RX buffer descriptor */
#define	RX_BD_LC	(1 <<  0) /* late collision */
#define	RX_BD_CRC	(1 <<  1) /* RX CRC error */
#define	RX_BD_SF	(1 <<  2) /* short frame */
#define	RX_BD_TL	(1 <<  3) /* too long */
#define	RX_BD_DN	(1 <<  4) /* dribble nibble */
#define	RX_BD_IS	(1 <<  5) /* invalid symbol */
#define	RX_BD_OR	(1 <<  6) /* receiver overrun */
#define	RX_BD_MISS	(1 <<  7)
#define	RX_BD_CF	(1 <<  8) /* control frame */
#define	RX_BD_WRAP	(1 << 13)
#define	RX_BD_IRQ	(1 << 14) /* interrupt request enable */
#define	RX_BD_EMPTY	(1 << 15)
#define	RX_BD_LEN(x)	(((x) & 0xffff) << 16)

#define	RX_BD_STATS	(RX_BD_LC | RX_BD_CRC | RX_BD_SF | RX_BD_TL | \
			RX_BD_DN | RX_BD_IS | RX_BD_OR | RX_BD_MISS)

#define	ETHOC_BUFSIZ		1536
#define	ETHOC_ZLEN		64
#define	ETHOC_BD_BASE		0x400
#define	ETHOC_TIMEOUT		(HZ / 2)
#define	ETHOC_MII_TIMEOUT	(1 + (HZ / 5))

/**
 * struct ethoc - driver-private device structure
 * @iobase:	pointer to I/O memory region
 * @membase:	pointer to buffer memory region
 * @dma_alloc:	dma allocated buffer size
 * @io_region_size:	I/O memory region size
 * @num_tx:	number of send buffers
 * @cur_tx:	last send buffer written
 * @dty_tx:	last buffer actually sent
 * @num_rx:	number of receive buffers
 * @cur_rx:	current receive buffer
 * @netdev:	pointer to network device structure
 * @napi:	NAPI structure
 * @stats:	network device statistics
 * @msg_enable:	device state flags
 * @rx_lock:	receive lock
 * @lock:	device lock
 * @phy:	attached PHY
 * @mdio:	MDIO bus for PHY access
 * @phy_id:	address of attached PHY
 */
struct ethoc {
	void __iomem *iobase;
	void __iomem *membase;
	int dma_alloc;
	resource_size_t io_region_size;

	unsigned int num_tx;
	unsigned int cur_tx;
	unsigned int dty_tx;

	unsigned int num_rx;
	unsigned int cur_rx;

	struct net_device *netdev;
	struct napi_struct napi;
	struct net_device_stats stats;
	u32 msg_enable;

	spinlock_t rx_lock;
	spinlock_t lock;

	struct phy_device *phy;
	struct mii_bus *mdio;
	s8 phy_id;
};

/**
 * struct ethoc_bd - buffer descriptor
 * @stat:	buffer statistics
 * @addr:	physical memory address
 */
struct ethoc_bd {
	u32 stat;
	u32 addr;
};

static inline u32 ethoc_read(struct ethoc *dev, loff_t offset)
{
	return ioread32(dev->iobase + offset);
}

static inline void ethoc_write(struct ethoc *dev, loff_t offset, u32 data)
{
	iowrite32(data, dev->iobase + offset);
}

static inline void ethoc_read_bd(struct ethoc *dev, int index,
		struct ethoc_bd *bd)
{
	loff_t offset = ETHOC_BD_BASE + (index * sizeof(struct ethoc_bd));
	bd->stat = ethoc_read(dev, offset + 0);
	bd->addr = ethoc_read(dev, offset + 4);
}

static inline void ethoc_write_bd(struct ethoc *dev, int index,
		const struct ethoc_bd *bd)
{
	loff_t offset = ETHOC_BD_BASE + (index * sizeof(struct ethoc_bd));
	ethoc_write(dev, offset + 0, bd->stat);
	ethoc_write(dev, offset + 4, bd->addr);
}

static inline void ethoc_enable_irq(struct ethoc *dev, u32 mask)
{
	u32 imask = ethoc_read(dev, INT_MASK);
	imask |= mask;
	ethoc_write(dev, INT_MASK, imask);
}

static inline void ethoc_disable_irq(struct ethoc *dev, u32 mask)
{
	u32 imask = ethoc_read(dev, INT_MASK);
	imask &= ~mask;
	ethoc_write(dev, INT_MASK, imask);
}

static inline void ethoc_ack_irq(struct ethoc *dev, u32 mask)
{
	ethoc_write(dev, INT_SOURCE, mask);
}

static inline void ethoc_enable_rx_and_tx(struct ethoc *dev)
{
	u32 mode = ethoc_read(dev, MODER);
	mode |= MODER_RXEN | MODER_TXEN;
	ethoc_write(dev, MODER, mode);
}

static inline void ethoc_disable_rx_and_tx(struct ethoc *dev)
{
	u32 mode = ethoc_read(dev, MODER);
	mode &= ~(MODER_RXEN | MODER_TXEN);
	ethoc_write(dev, MODER, mode);
}

static int ethoc_init_ring(struct ethoc *dev)
{
	struct ethoc_bd bd;
	int i;

	dev->cur_tx = 0;
	dev->dty_tx = 0;
	dev->cur_rx = 0;

	/* setup transmission buffers */
	bd.addr = virt_to_phys(dev->membase);
	bd.stat = TX_BD_IRQ | TX_BD_CRC;

	for (i = 0; i < dev->num_tx; i++) {
		if (i == dev->num_tx - 1)
			bd.stat |= TX_BD_WRAP;

		ethoc_write_bd(dev, i, &bd);
		bd.addr += ETHOC_BUFSIZ;
	}

	bd.stat = RX_BD_EMPTY | RX_BD_IRQ;

	for (i = 0; i < dev->num_rx; i++) {
		if (i == dev->num_rx - 1)
			bd.stat |= RX_BD_WRAP;

		ethoc_write_bd(dev, dev->num_tx + i, &bd);
		bd.addr += ETHOC_BUFSIZ;
	}

	return 0;
}

static int ethoc_reset(struct ethoc *dev)
{
	u32 mode;

	/* TODO: reset controller? */

	ethoc_disable_rx_and_tx(dev);

	/* TODO: setup registers */

	/* enable FCS generation and automatic padding */
	mode = ethoc_read(dev, MODER);
	mode |= MODER_CRC | MODER_PAD;
	ethoc_write(dev, MODER, mode);

	/* set full-duplex mode */
	mode = ethoc_read(dev, MODER);
	mode |= MODER_FULLD;
	ethoc_write(dev, MODER, mode);
	ethoc_write(dev, IPGT, 0x15);

	ethoc_ack_irq(dev, INT_MASK_ALL);
	ethoc_enable_irq(dev, INT_MASK_ALL);
	ethoc_enable_rx_and_tx(dev);
	return 0;
}

static unsigned int ethoc_update_rx_stats(struct ethoc *dev,
		struct ethoc_bd *bd)
{
	struct net_device *netdev = dev->netdev;
	unsigned int ret = 0;

	if (bd->stat & RX_BD_TL) {
		dev_err(&netdev->dev, "RX: frame too long\n");
		dev->stats.rx_length_errors++;
		ret++;
	}

	if (bd->stat & RX_BD_SF) {
		dev_err(&netdev->dev, "RX: frame too short\n");
		dev->stats.rx_length_errors++;
		ret++;
	}

	if (bd->stat & RX_BD_DN) {
		dev_err(&netdev->dev, "RX: dribble nibble\n");
		dev->stats.rx_frame_errors++;
	}

	if (bd->stat & RX_BD_CRC) {
		dev_err(&netdev->dev, "RX: wrong CRC\n");
		dev->stats.rx_crc_errors++;
		ret++;
	}

	if (bd->stat & RX_BD_OR) {
		dev_err(&netdev->dev, "RX: overrun\n");
		dev->stats.rx_over_errors++;
		ret++;
	}

	if (bd->stat & RX_BD_MISS)
		dev->stats.rx_missed_errors++;

	if (bd->stat & RX_BD_LC) {
		dev_err(&netdev->dev, "RX: late collision\n");
		dev->stats.collisions++;
		ret++;
	}

	return ret;
}

static int ethoc_rx(struct net_device *dev, int limit)
{
	struct ethoc *priv = netdev_priv(dev);
	int count;

	for (count = 0; count < limit; ++count) {
		unsigned int entry;
		struct ethoc_bd bd;

		entry = priv->num_tx + (priv->cur_rx % priv->num_rx);
		ethoc_read_bd(priv, entry, &bd);
		if (bd.stat & RX_BD_EMPTY)
			break;

		if (ethoc_update_rx_stats(priv, &bd) == 0) {
			int size = bd.stat >> 16;
			struct sk_buff *skb;

			size -= 4; /* strip the CRC */
			skb = netdev_alloc_skb_ip_align(dev, size);

			if (likely(skb)) {
				void *src = phys_to_virt(bd.addr);
				memcpy_fromio(skb_put(skb, size), src, size);
				skb->protocol = eth_type_trans(skb, dev);
				priv->stats.rx_packets++;
				priv->stats.rx_bytes += size;
				netif_receive_skb(skb);
			} else {
				if (net_ratelimit())
					dev_warn(&dev->dev, "low on memory - "
							"packet dropped\n");

				priv->stats.rx_dropped++;
				break;
			}
		}

		/* clear the buffer descriptor so it can be reused */
		bd.stat &= ~RX_BD_STATS;
		bd.stat |=  RX_BD_EMPTY;
		ethoc_write_bd(priv, entry, &bd);
		priv->cur_rx++;
	}

	return count;
}

static int ethoc_update_tx_stats(struct ethoc *dev, struct ethoc_bd *bd)
{
	struct net_device *netdev = dev->netdev;

	if (bd->stat & TX_BD_LC) {
		dev_err(&netdev->dev, "TX: late collision\n");
		dev->stats.tx_window_errors++;
	}

	if (bd->stat & TX_BD_RL) {
		dev_err(&netdev->dev, "TX: retransmit limit\n");
		dev->stats.tx_aborted_errors++;
	}

	if (bd->stat & TX_BD_UR) {
		dev_err(&netdev->dev, "TX: underrun\n");
		dev->stats.tx_fifo_errors++;
	}

	if (bd->stat & TX_BD_CS) {
		dev_err(&netdev->dev, "TX: carrier sense lost\n");
		dev->stats.tx_carrier_errors++;
	}

	if (bd->stat & TX_BD_STATS)
		dev->stats.tx_errors++;

	dev->stats.collisions += (bd->stat >> 4) & 0xf;
	dev->stats.tx_bytes += bd->stat >> 16;
	dev->stats.tx_packets++;
	return 0;
}

static void ethoc_tx(struct net_device *dev)
{
	struct ethoc *priv = netdev_priv(dev);

	spin_lock(&priv->lock);

	while (priv->dty_tx != priv->cur_tx) {
		unsigned int entry = priv->dty_tx % priv->num_tx;
		struct ethoc_bd bd;

		ethoc_read_bd(priv, entry, &bd);
		if (bd.stat & TX_BD_READY)
			break;

		entry = (++priv->dty_tx) % priv->num_tx;
		(void)ethoc_update_tx_stats(priv, &bd);
	}

	if ((priv->cur_tx - priv->dty_tx) <= (priv->num_tx / 2))
		netif_wake_queue(dev);

	ethoc_ack_irq(priv, INT_MASK_TX);
	spin_unlock(&priv->lock);
}

static irqreturn_t ethoc_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = (struct net_device *)dev_id;
	struct ethoc *priv = netdev_priv(dev);
	u32 pending;

	ethoc_disable_irq(priv, INT_MASK_ALL);
	pending = ethoc_read(priv, INT_SOURCE);
	if (unlikely(pending == 0)) {
		ethoc_enable_irq(priv, INT_MASK_ALL);
		return IRQ_NONE;
	}

	ethoc_ack_irq(priv, pending);

	if (pending & INT_MASK_BUSY) {
		dev_err(&dev->dev, "packet dropped\n");
		priv->stats.rx_dropped++;
	}

	if (pending & INT_MASK_RX) {
		if (napi_schedule_prep(&priv->napi))
			__napi_schedule(&priv->napi);
	} else {
		ethoc_enable_irq(priv, INT_MASK_RX);
	}

	if (pending & INT_MASK_TX)
		ethoc_tx(dev);

	ethoc_enable_irq(priv, INT_MASK_ALL & ~INT_MASK_RX);
	return IRQ_HANDLED;
}

static int ethoc_get_mac_address(struct net_device *dev, void *addr)
{
	struct ethoc *priv = netdev_priv(dev);
	u8 *mac = (u8 *)addr;
	u32 reg;

	reg = ethoc_read(priv, MAC_ADDR0);
	mac[2] = (reg >> 24) & 0xff;
	mac[3] = (reg >> 16) & 0xff;
	mac[4] = (reg >>  8) & 0xff;
	mac[5] = (reg >>  0) & 0xff;

	reg = ethoc_read(priv, MAC_ADDR1);
	mac[0] = (reg >>  8) & 0xff;
	mac[1] = (reg >>  0) & 0xff;

	return 0;
}

static int ethoc_poll(struct napi_struct *napi, int budget)
{
	struct ethoc *priv = container_of(napi, struct ethoc, napi);
	int work_done = 0;

	work_done = ethoc_rx(priv->netdev, budget);
	if (work_done < budget) {
		ethoc_enable_irq(priv, INT_MASK_RX);
		napi_complete(napi);
	}

	return work_done;
}

static int ethoc_mdio_read(struct mii_bus *bus, int phy, int reg)
{
	unsigned long timeout = jiffies + ETHOC_MII_TIMEOUT;
	struct ethoc *priv = bus->priv;

	ethoc_write(priv, MIIADDRESS, MIIADDRESS_ADDR(phy, reg));
	ethoc_write(priv, MIICOMMAND, MIICOMMAND_READ);

	while (time_before(jiffies, timeout)) {
		u32 status = ethoc_read(priv, MIISTATUS);
		if (!(status & MIISTATUS_BUSY)) {
			u32 data = ethoc_read(priv, MIIRX_DATA);
			/* reset MII command register */
			ethoc_write(priv, MIICOMMAND, 0);
			return data;
		}

		schedule();
	}

	return -EBUSY;
}

static int ethoc_mdio_write(struct mii_bus *bus, int phy, int reg, u16 val)
{
	unsigned long timeout = jiffies + ETHOC_MII_TIMEOUT;
	struct ethoc *priv = bus->priv;

	ethoc_write(priv, MIIADDRESS, MIIADDRESS_ADDR(phy, reg));
	ethoc_write(priv, MIITX_DATA, val);
	ethoc_write(priv, MIICOMMAND, MIICOMMAND_WRITE);

	while (time_before(jiffies, timeout)) {
		u32 stat = ethoc_read(priv, MIISTATUS);
		if (!(stat & MIISTATUS_BUSY))
			return 0;

		schedule();
	}

	return -EBUSY;
}

static int ethoc_mdio_reset(struct mii_bus *bus)
{
	return 0;
}

static void ethoc_mdio_poll(struct net_device *dev)
{
}

static int ethoc_mdio_probe(struct net_device *dev)
{
	struct ethoc *priv = netdev_priv(dev);
	struct phy_device *phy;
	int i;

	for (i = 0; i < PHY_MAX_ADDR; i++) {
		phy = priv->mdio->phy_map[i];
		if (phy) {
			if (priv->phy_id != -1) {
				/* attach to specified PHY */
				if (priv->phy_id == phy->addr)
					break;
			} else {
				/* autoselect PHY if none was specified */
				if (phy->addr != 0)
					break;
			}
		}
	}

	if (!phy) {
		dev_err(&dev->dev, "no PHY found\n");
		return -ENXIO;
	}

	phy = phy_connect(dev, dev_name(&phy->dev), ethoc_mdio_poll, 0,
			PHY_INTERFACE_MODE_GMII);
	if (IS_ERR(phy)) {
		dev_err(&dev->dev, "could not attach to PHY\n");
		return PTR_ERR(phy);
	}

	priv->phy = phy;
	return 0;
}

static int ethoc_open(struct net_device *dev)
{
	struct ethoc *priv = netdev_priv(dev);
	unsigned int min_tx = 2;
	unsigned int num_bd;
	int ret;

	ret = request_irq(dev->irq, ethoc_interrupt, IRQF_SHARED,
			dev->name, dev);
	if (ret)
		return ret;

	/* calculate the number of TX/RX buffers, maximum 128 supported */
	num_bd = min_t(unsigned int,
		128, (dev->mem_end - dev->mem_start + 1) / ETHOC_BUFSIZ);
	priv->num_tx = max(min_tx, num_bd / 4);
	priv->num_rx = num_bd - priv->num_tx;
	ethoc_write(priv, TX_BD_NUM, priv->num_tx);

	ethoc_init_ring(priv);
	ethoc_reset(priv);

	if (netif_queue_stopped(dev)) {
		dev_dbg(&dev->dev, " resuming queue\n");
		netif_wake_queue(dev);
	} else {
		dev_dbg(&dev->dev, " starting queue\n");
		netif_start_queue(dev);
	}

	phy_start(priv->phy);
	napi_enable(&priv->napi);

	if (netif_msg_ifup(priv)) {
		dev_info(&dev->dev, "I/O: %08lx Memory: %08lx-%08lx\n",
				dev->base_addr, dev->mem_start, dev->mem_end);
	}

	return 0;
}

static int ethoc_stop(struct net_device *dev)
{
	struct ethoc *priv = netdev_priv(dev);

	napi_disable(&priv->napi);

	if (priv->phy)
		phy_stop(priv->phy);

	ethoc_disable_rx_and_tx(priv);
	free_irq(dev->irq, dev);

	if (!netif_queue_stopped(dev))
		netif_stop_queue(dev);

	return 0;
}

static int ethoc_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct ethoc *priv = netdev_priv(dev);
	struct mii_ioctl_data *mdio = if_mii(ifr);
	struct phy_device *phy = NULL;

	if (!netif_running(dev))
		return -EINVAL;

	if (cmd != SIOCGMIIPHY) {
		if (mdio->phy_id >= PHY_MAX_ADDR)
			return -ERANGE;

		phy = priv->mdio->phy_map[mdio->phy_id];
		if (!phy)
			return -ENODEV;
	} else {
		phy = priv->phy;
	}

	return phy_mii_ioctl(phy, mdio, cmd);
}

static int ethoc_config(struct net_device *dev, struct ifmap *map)
{
	return -ENOSYS;
}

static int ethoc_set_mac_address(struct net_device *dev, void *addr)
{
	struct ethoc *priv = netdev_priv(dev);
	u8 *mac = (u8 *)addr;

	ethoc_write(priv, MAC_ADDR0, (mac[2] << 24) | (mac[3] << 16) |
				     (mac[4] <<  8) | (mac[5] <<  0));
	ethoc_write(priv, MAC_ADDR1, (mac[0] <<  8) | (mac[1] <<  0));

	return 0;
}

static void ethoc_set_multicast_list(struct net_device *dev)
{
	struct ethoc *priv = netdev_priv(dev);
	u32 mode = ethoc_read(priv, MODER);
	struct netdev_hw_addr *ha;
	u32 hash[2] = { 0, 0 };

	/* set loopback mode if requested */
	if (dev->flags & IFF_LOOPBACK)
		mode |=  MODER_LOOP;
	else
		mode &= ~MODER_LOOP;

	/* receive broadcast frames if requested */
	if (dev->flags & IFF_BROADCAST)
		mode &= ~MODER_BRO;
	else
		mode |=  MODER_BRO;

	/* enable promiscuous mode if requested */
	if (dev->flags & IFF_PROMISC)
		mode |=  MODER_PRO;
	else
		mode &= ~MODER_PRO;

	ethoc_write(priv, MODER, mode);

	/* receive multicast frames */
	if (dev->flags & IFF_ALLMULTI) {
		hash[0] = 0xffffffff;
		hash[1] = 0xffffffff;
	} else {
		netdev_for_each_mc_addr(ha, dev) {
			u32 crc = ether_crc(ETH_ALEN, ha->addr);
			int bit = (crc >> 26) & 0x3f;
			hash[bit >> 5] |= 1 << (bit & 0x1f);
		}
	}

	ethoc_write(priv, ETH_HASH0, hash[0]);
	ethoc_write(priv, ETH_HASH1, hash[1]);
}

static int ethoc_change_mtu(struct net_device *dev, int new_mtu)
{
	return -ENOSYS;
}

static void ethoc_tx_timeout(struct net_device *dev)
{
	struct ethoc *priv = netdev_priv(dev);
	u32 pending = ethoc_read(priv, INT_SOURCE);
	if (likely(pending))
		ethoc_interrupt(dev->irq, dev);
}

static struct net_device_stats *ethoc_stats(struct net_device *dev)
{
	struct ethoc *priv = netdev_priv(dev);
	return &priv->stats;
}

static netdev_tx_t ethoc_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct ethoc *priv = netdev_priv(dev);
	struct ethoc_bd bd;
	unsigned int entry;
	void *dest;

	if (unlikely(skb->len > ETHOC_BUFSIZ)) {
		priv->stats.tx_errors++;
		goto out;
	}

	entry = priv->cur_tx % priv->num_tx;
	spin_lock_irq(&priv->lock);
	priv->cur_tx++;

	ethoc_read_bd(priv, entry, &bd);
	if (unlikely(skb->len < ETHOC_ZLEN))
		bd.stat |=  TX_BD_PAD;
	else
		bd.stat &= ~TX_BD_PAD;

	dest = phys_to_virt(bd.addr);
	memcpy_toio(dest, skb->data, skb->len);

	bd.stat &= ~(TX_BD_STATS | TX_BD_LEN_MASK);
	bd.stat |= TX_BD_LEN(skb->len);
	ethoc_write_bd(priv, entry, &bd);

	bd.stat |= TX_BD_READY;
	ethoc_write_bd(priv, entry, &bd);

	if (priv->cur_tx == (priv->dty_tx + priv->num_tx)) {
		dev_dbg(&dev->dev, "stopping queue\n");
		netif_stop_queue(dev);
	}

	spin_unlock_irq(&priv->lock);
out:
	dev_kfree_skb(skb);
	return NETDEV_TX_OK;
}

static const struct net_device_ops ethoc_netdev_ops = {
	.ndo_open = ethoc_open,
	.ndo_stop = ethoc_stop,
	.ndo_do_ioctl = ethoc_ioctl,
	.ndo_set_config = ethoc_config,
	.ndo_set_mac_address = ethoc_set_mac_address,
	.ndo_set_multicast_list = ethoc_set_multicast_list,
	.ndo_change_mtu = ethoc_change_mtu,
	.ndo_tx_timeout = ethoc_tx_timeout,
	.ndo_get_stats = ethoc_stats,
	.ndo_start_xmit = ethoc_start_xmit,
};

/**
 * ethoc_probe() - initialize OpenCores ethernet MAC
 * pdev:	platform device
 */
static int ethoc_probe(struct platform_device *pdev)
{
	struct net_device *netdev = NULL;
	struct resource *res = NULL;
	struct resource *mmio = NULL;
	struct resource *mem = NULL;
	struct ethoc *priv = NULL;
	unsigned int phy;
	int ret = 0;

	/* allocate networking device */
	netdev = alloc_etherdev(sizeof(struct ethoc));
	if (!netdev) {
		dev_err(&pdev->dev, "cannot allocate network device\n");
		ret = -ENOMEM;
		goto out;
	}

	SET_NETDEV_DEV(netdev, &pdev->dev);
	platform_set_drvdata(pdev, netdev);

	/* obtain I/O memory space */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "cannot obtain I/O memory space\n");
		ret = -ENXIO;
		goto free;
	}

	mmio = devm_request_mem_region(&pdev->dev, res->start,
			resource_size(res), res->name);
	if (!mmio) {
		dev_err(&pdev->dev, "cannot request I/O memory space\n");
		ret = -ENXIO;
		goto free;
	}

	netdev->base_addr = mmio->start;

	/* obtain buffer memory space */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (res) {
		mem = devm_request_mem_region(&pdev->dev, res->start,
			resource_size(res), res->name);
		if (!mem) {
			dev_err(&pdev->dev, "cannot request memory space\n");
			ret = -ENXIO;
			goto free;
		}

		netdev->mem_start = mem->start;
		netdev->mem_end   = mem->end;
	}


	/* obtain device IRQ number */
	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		dev_err(&pdev->dev, "cannot obtain IRQ\n");
		ret = -ENXIO;
		goto free;
	}

	netdev->irq = res->start;

	/* setup driver-private data */
	priv = netdev_priv(netdev);
	priv->netdev = netdev;
	priv->dma_alloc = 0;
	priv->io_region_size = mmio->end - mmio->start + 1;

	priv->iobase = devm_ioremap_nocache(&pdev->dev, netdev->base_addr,
			resource_size(mmio));
	if (!priv->iobase) {
		dev_err(&pdev->dev, "cannot remap I/O memory space\n");
		ret = -ENXIO;
		goto error;
	}

	if (netdev->mem_end) {
		priv->membase = devm_ioremap_nocache(&pdev->dev,
			netdev->mem_start, resource_size(mem));
		if (!priv->membase) {
			dev_err(&pdev->dev, "cannot remap memory space\n");
			ret = -ENXIO;
			goto error;
		}
	} else {
		/* Allocate buffer memory */
		priv->membase = dma_alloc_coherent(NULL,
			buffer_size, (void *)&netdev->mem_start,
			GFP_KERNEL);
		if (!priv->membase) {
			dev_err(&pdev->dev, "cannot allocate %dB buffer\n",
				buffer_size);
			ret = -ENOMEM;
			goto error;
		}
		netdev->mem_end = netdev->mem_start + buffer_size;
		priv->dma_alloc = buffer_size;
	}

	/* Allow the platform setup code to pass in a MAC address. */
	if (pdev->dev.platform_data) {
		struct ethoc_platform_data *pdata =
			(struct ethoc_platform_data *)pdev->dev.platform_data;
		memcpy(netdev->dev_addr, pdata->hwaddr, IFHWADDRLEN);
		priv->phy_id = pdata->phy_id;
	}

	/* Check that the given MAC address is valid. If it isn't, read the
	 * current MAC from the controller. */
	if (!is_valid_ether_addr(netdev->dev_addr))
		ethoc_get_mac_address(netdev, netdev->dev_addr);

	/* Check the MAC again for validity, if it still isn't choose and
	 * program a random one. */
	if (!is_valid_ether_addr(netdev->dev_addr))
		random_ether_addr(netdev->dev_addr);

	ethoc_set_mac_address(netdev, netdev->dev_addr);

	/* register MII bus */
	priv->mdio = mdiobus_alloc();
	if (!priv->mdio) {
		ret = -ENOMEM;
		goto free;
	}

	priv->mdio->name = "ethoc-mdio";
	snprintf(priv->mdio->id, MII_BUS_ID_SIZE, "%s-%d",
			priv->mdio->name, pdev->id);
	priv->mdio->read = ethoc_mdio_read;
	priv->mdio->write = ethoc_mdio_write;
	priv->mdio->reset = ethoc_mdio_reset;
	priv->mdio->priv = priv;

	priv->mdio->irq = kmalloc(sizeof(int) * PHY_MAX_ADDR, GFP_KERNEL);
	if (!priv->mdio->irq) {
		ret = -ENOMEM;
		goto free_mdio;
	}

	for (phy = 0; phy < PHY_MAX_ADDR; phy++)
		priv->mdio->irq[phy] = PHY_POLL;

	ret = mdiobus_register(priv->mdio);
	if (ret) {
		dev_err(&netdev->dev, "failed to register MDIO bus\n");
		goto free_mdio;
	}

	ret = ethoc_mdio_probe(netdev);
	if (ret) {
		dev_err(&netdev->dev, "failed to probe MDIO bus\n");
		goto error;
	}

	ether_setup(netdev);

	/* setup the net_device structure */
	netdev->netdev_ops = &ethoc_netdev_ops;
	netdev->watchdog_timeo = ETHOC_TIMEOUT;
	netdev->features |= 0;

	/* setup NAPI */
	netif_napi_add(netdev, &priv->napi, ethoc_poll, 64);

	spin_lock_init(&priv->rx_lock);
	spin_lock_init(&priv->lock);

	ret = register_netdev(netdev);
	if (ret < 0) {
		dev_err(&netdev->dev, "failed to register interface\n");
		goto error2;
	}

	goto out;

error2:
	netif_napi_del(&priv->napi);
error:
	mdiobus_unregister(priv->mdio);
free_mdio:
	kfree(priv->mdio->irq);
	mdiobus_free(priv->mdio);
free:
	if (priv) {
		if (priv->dma_alloc)
			dma_free_coherent(NULL, priv->dma_alloc, priv->membase,
					  netdev->mem_start);
		else if (priv->membase)
			devm_iounmap(&pdev->dev, priv->membase);
		if (priv->iobase)
			devm_iounmap(&pdev->dev, priv->iobase);
	}
	if (mem)
		devm_release_mem_region(&pdev->dev, mem->start,
					mem->end - mem->start + 1);
	if (mmio)
		devm_release_mem_region(&pdev->dev, mmio->start,
					mmio->end - mmio->start + 1);
	free_netdev(netdev);
out:
	return ret;
}

/**
 * ethoc_remove() - shutdown OpenCores ethernet MAC
 * @pdev:	platform device
 */
static int ethoc_remove(struct platform_device *pdev)
{
	struct net_device *netdev = platform_get_drvdata(pdev);
	struct ethoc *priv = netdev_priv(netdev);

	platform_set_drvdata(pdev, NULL);

	if (netdev) {
		netif_napi_del(&priv->napi);
		phy_disconnect(priv->phy);
		priv->phy = NULL;

		if (priv->mdio) {
			mdiobus_unregister(priv->mdio);
			kfree(priv->mdio->irq);
			mdiobus_free(priv->mdio);
		}
		if (priv->dma_alloc)
			dma_free_coherent(NULL, priv->dma_alloc, priv->membase,
				netdev->mem_start);
		else {
			devm_iounmap(&pdev->dev, priv->membase);
			devm_release_mem_region(&pdev->dev, netdev->mem_start,
				netdev->mem_end - netdev->mem_start + 1);
		}
		devm_iounmap(&pdev->dev, priv->iobase);
		devm_release_mem_region(&pdev->dev, netdev->base_addr,
			priv->io_region_size);
		unregister_netdev(netdev);
		free_netdev(netdev);
	}

	return 0;
}

#ifdef CONFIG_PM
static int ethoc_suspend(struct platform_device *pdev, pm_message_t state)
{
	return -ENOSYS;
}

static int ethoc_resume(struct platform_device *pdev)
{
	return -ENOSYS;
}
#else
# define ethoc_suspend NULL
# define ethoc_resume  NULL
#endif

static struct platform_driver ethoc_driver = {
	.probe   = ethoc_probe,
	.remove  = ethoc_remove,
	.suspend = ethoc_suspend,
	.resume  = ethoc_resume,
	.driver  = {
		.name = "ethoc",
	},
};

static int __init ethoc_init(void)
{
	return platform_driver_register(&ethoc_driver);
}

static void __exit ethoc_exit(void)
{
	platform_driver_unregister(&ethoc_driver);
}

module_init(ethoc_init);
module_exit(ethoc_exit);

MODULE_AUTHOR("Thierry Reding <thierry.reding@avionic-design.de>");
MODULE_DESCRIPTION("OpenCores Ethernet MAC driver");
MODULE_LICENSE("GPL v2");

