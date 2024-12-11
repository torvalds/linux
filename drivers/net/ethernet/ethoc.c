// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/drivers/net/ethernet/ethoc.c
 *
 * Copyright (C) 2007-2008 Avionic Design Development GmbH
 * Copyright (C) 2008-2009 Avionic Design GmbH
 *
 * Written by Thierry Reding <thierry.reding@avionic-design.de>
 */

#include <linux/dma-mapping.h>
#include <linux/etherdevice.h>
#include <linux/clk.h>
#include <linux/crc32.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mii.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_net.h>
#include <linux/module.h>
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
#define	ETH_END		0x54

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
 * @big_endian: just big or little (endian)
 * @num_bd:	number of buffer descriptors
 * @num_tx:	number of send buffers
 * @cur_tx:	last send buffer written
 * @dty_tx:	last buffer actually sent
 * @num_rx:	number of receive buffers
 * @cur_rx:	current receive buffer
 * @vma:        pointer to array of virtual memory addresses for buffers
 * @netdev:	pointer to network device structure
 * @napi:	NAPI structure
 * @msg_enable:	device state flags
 * @lock:	device lock
 * @mdio:	MDIO bus for PHY access
 * @clk:	clock
 * @phy_id:	address of attached PHY
 * @old_link:	previous link info
 * @old_duplex: previous duplex info
 */
struct ethoc {
	void __iomem *iobase;
	void __iomem *membase;
	bool big_endian;

	unsigned int num_bd;
	unsigned int num_tx;
	unsigned int cur_tx;
	unsigned int dty_tx;

	unsigned int num_rx;
	unsigned int cur_rx;

	void **vma;

	struct net_device *netdev;
	struct napi_struct napi;
	u32 msg_enable;

	spinlock_t lock;

	struct mii_bus *mdio;
	struct clk *clk;
	s8 phy_id;

	int old_link;
	int old_duplex;
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
	if (dev->big_endian)
		return ioread32be(dev->iobase + offset);
	else
		return ioread32(dev->iobase + offset);
}

static inline void ethoc_write(struct ethoc *dev, loff_t offset, u32 data)
{
	if (dev->big_endian)
		iowrite32be(data, dev->iobase + offset);
	else
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

static int ethoc_init_ring(struct ethoc *dev, unsigned long mem_start)
{
	struct ethoc_bd bd;
	int i;
	void *vma;

	dev->cur_tx = 0;
	dev->dty_tx = 0;
	dev->cur_rx = 0;

	ethoc_write(dev, TX_BD_NUM, dev->num_tx);

	/* setup transmission buffers */
	bd.addr = mem_start;
	bd.stat = TX_BD_IRQ | TX_BD_CRC;
	vma = dev->membase;

	for (i = 0; i < dev->num_tx; i++) {
		if (i == dev->num_tx - 1)
			bd.stat |= TX_BD_WRAP;

		ethoc_write_bd(dev, i, &bd);
		bd.addr += ETHOC_BUFSIZ;

		dev->vma[i] = vma;
		vma += ETHOC_BUFSIZ;
	}

	bd.stat = RX_BD_EMPTY | RX_BD_IRQ;

	for (i = 0; i < dev->num_rx; i++) {
		if (i == dev->num_rx - 1)
			bd.stat |= RX_BD_WRAP;

		ethoc_write_bd(dev, dev->num_tx + i, &bd);
		bd.addr += ETHOC_BUFSIZ;

		dev->vma[dev->num_tx + i] = vma;
		vma += ETHOC_BUFSIZ;
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
		netdev->stats.rx_length_errors++;
		ret++;
	}

	if (bd->stat & RX_BD_SF) {
		dev_err(&netdev->dev, "RX: frame too short\n");
		netdev->stats.rx_length_errors++;
		ret++;
	}

	if (bd->stat & RX_BD_DN) {
		dev_err(&netdev->dev, "RX: dribble nibble\n");
		netdev->stats.rx_frame_errors++;
	}

	if (bd->stat & RX_BD_CRC) {
		dev_err(&netdev->dev, "RX: wrong CRC\n");
		netdev->stats.rx_crc_errors++;
		ret++;
	}

	if (bd->stat & RX_BD_OR) {
		dev_err(&netdev->dev, "RX: overrun\n");
		netdev->stats.rx_over_errors++;
		ret++;
	}

	if (bd->stat & RX_BD_MISS)
		netdev->stats.rx_missed_errors++;

	if (bd->stat & RX_BD_LC) {
		dev_err(&netdev->dev, "RX: late collision\n");
		netdev->stats.collisions++;
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

		entry = priv->num_tx + priv->cur_rx;
		ethoc_read_bd(priv, entry, &bd);
		if (bd.stat & RX_BD_EMPTY) {
			ethoc_ack_irq(priv, INT_MASK_RX);
			/* If packet (interrupt) came in between checking
			 * BD_EMTPY and clearing the interrupt source, then we
			 * risk missing the packet as the RX interrupt won't
			 * trigger right away when we reenable it; hence, check
			 * BD_EMTPY here again to make sure there isn't such a
			 * packet waiting for us...
			 */
			ethoc_read_bd(priv, entry, &bd);
			if (bd.stat & RX_BD_EMPTY)
				break;
		}

		if (ethoc_update_rx_stats(priv, &bd) == 0) {
			int size = bd.stat >> 16;
			struct sk_buff *skb;

			size -= 4; /* strip the CRC */
			skb = netdev_alloc_skb_ip_align(dev, size);

			if (likely(skb)) {
				void *src = priv->vma[entry];
				memcpy_fromio(skb_put(skb, size), src, size);
				skb->protocol = eth_type_trans(skb, dev);
				dev->stats.rx_packets++;
				dev->stats.rx_bytes += size;
				netif_receive_skb(skb);
			} else {
				if (net_ratelimit())
					dev_warn(&dev->dev,
					    "low on memory - packet dropped\n");

				dev->stats.rx_dropped++;
				break;
			}
		}

		/* clear the buffer descriptor so it can be reused */
		bd.stat &= ~RX_BD_STATS;
		bd.stat |=  RX_BD_EMPTY;
		ethoc_write_bd(priv, entry, &bd);
		if (++priv->cur_rx == priv->num_rx)
			priv->cur_rx = 0;
	}

	return count;
}

static void ethoc_update_tx_stats(struct ethoc *dev, struct ethoc_bd *bd)
{
	struct net_device *netdev = dev->netdev;

	if (bd->stat & TX_BD_LC) {
		dev_err(&netdev->dev, "TX: late collision\n");
		netdev->stats.tx_window_errors++;
	}

	if (bd->stat & TX_BD_RL) {
		dev_err(&netdev->dev, "TX: retransmit limit\n");
		netdev->stats.tx_aborted_errors++;
	}

	if (bd->stat & TX_BD_UR) {
		dev_err(&netdev->dev, "TX: underrun\n");
		netdev->stats.tx_fifo_errors++;
	}

	if (bd->stat & TX_BD_CS) {
		dev_err(&netdev->dev, "TX: carrier sense lost\n");
		netdev->stats.tx_carrier_errors++;
	}

	if (bd->stat & TX_BD_STATS)
		netdev->stats.tx_errors++;

	netdev->stats.collisions += (bd->stat >> 4) & 0xf;
	netdev->stats.tx_bytes += bd->stat >> 16;
	netdev->stats.tx_packets++;
}

static int ethoc_tx(struct net_device *dev, int limit)
{
	struct ethoc *priv = netdev_priv(dev);
	int count;
	struct ethoc_bd bd;

	for (count = 0; count < limit; ++count) {
		unsigned int entry;

		entry = priv->dty_tx & (priv->num_tx-1);

		ethoc_read_bd(priv, entry, &bd);

		if (bd.stat & TX_BD_READY || (priv->dty_tx == priv->cur_tx)) {
			ethoc_ack_irq(priv, INT_MASK_TX);
			/* If interrupt came in between reading in the BD
			 * and clearing the interrupt source, then we risk
			 * missing the event as the TX interrupt won't trigger
			 * right away when we reenable it; hence, check
			 * BD_EMPTY here again to make sure there isn't such an
			 * event pending...
			 */
			ethoc_read_bd(priv, entry, &bd);
			if (bd.stat & TX_BD_READY ||
			    (priv->dty_tx == priv->cur_tx))
				break;
		}

		ethoc_update_tx_stats(priv, &bd);
		priv->dty_tx++;
	}

	if ((priv->cur_tx - priv->dty_tx) <= (priv->num_tx / 2))
		netif_wake_queue(dev);

	return count;
}

static irqreturn_t ethoc_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct ethoc *priv = netdev_priv(dev);
	u32 pending;
	u32 mask;

	/* Figure out what triggered the interrupt...
	 * The tricky bit here is that the interrupt source bits get
	 * set in INT_SOURCE for an event regardless of whether that
	 * event is masked or not.  Thus, in order to figure out what
	 * triggered the interrupt, we need to remove the sources
	 * for all events that are currently masked.  This behaviour
	 * is not particularly well documented but reasonable...
	 */
	mask = ethoc_read(priv, INT_MASK);
	pending = ethoc_read(priv, INT_SOURCE);
	pending &= mask;

	if (unlikely(pending == 0))
		return IRQ_NONE;

	ethoc_ack_irq(priv, pending);

	/* We always handle the dropped packet interrupt */
	if (pending & INT_MASK_BUSY) {
		dev_dbg(&dev->dev, "packet dropped\n");
		dev->stats.rx_dropped++;
	}

	/* Handle receive/transmit event by switching to polling */
	if (pending & (INT_MASK_TX | INT_MASK_RX)) {
		ethoc_disable_irq(priv, INT_MASK_TX | INT_MASK_RX);
		napi_schedule(&priv->napi);
	}

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
	int rx_work_done = 0;
	int tx_work_done = 0;

	rx_work_done = ethoc_rx(priv->netdev, budget);
	tx_work_done = ethoc_tx(priv->netdev, budget);

	if (rx_work_done < budget && tx_work_done < budget) {
		napi_complete_done(napi, rx_work_done);
		ethoc_enable_irq(priv, INT_MASK_TX | INT_MASK_RX);
	}

	return rx_work_done;
}

static int ethoc_mdio_read(struct mii_bus *bus, int phy, int reg)
{
	struct ethoc *priv = bus->priv;
	int i;

	ethoc_write(priv, MIIADDRESS, MIIADDRESS_ADDR(phy, reg));
	ethoc_write(priv, MIICOMMAND, MIICOMMAND_READ);

	for (i = 0; i < 5; i++) {
		u32 status = ethoc_read(priv, MIISTATUS);
		if (!(status & MIISTATUS_BUSY)) {
			u32 data = ethoc_read(priv, MIIRX_DATA);
			/* reset MII command register */
			ethoc_write(priv, MIICOMMAND, 0);
			return data;
		}
		usleep_range(100, 200);
	}

	return -EBUSY;
}

static int ethoc_mdio_write(struct mii_bus *bus, int phy, int reg, u16 val)
{
	struct ethoc *priv = bus->priv;
	int i;

	ethoc_write(priv, MIIADDRESS, MIIADDRESS_ADDR(phy, reg));
	ethoc_write(priv, MIITX_DATA, val);
	ethoc_write(priv, MIICOMMAND, MIICOMMAND_WRITE);

	for (i = 0; i < 5; i++) {
		u32 stat = ethoc_read(priv, MIISTATUS);
		if (!(stat & MIISTATUS_BUSY)) {
			/* reset MII command register */
			ethoc_write(priv, MIICOMMAND, 0);
			return 0;
		}
		usleep_range(100, 200);
	}

	return -EBUSY;
}

static void ethoc_mdio_poll(struct net_device *dev)
{
	struct ethoc *priv = netdev_priv(dev);
	struct phy_device *phydev = dev->phydev;
	bool changed = false;
	u32 mode;

	if (priv->old_link != phydev->link) {
		changed = true;
		priv->old_link = phydev->link;
	}

	if (priv->old_duplex != phydev->duplex) {
		changed = true;
		priv->old_duplex = phydev->duplex;
	}

	if (!changed)
		return;

	mode = ethoc_read(priv, MODER);
	if (phydev->duplex == DUPLEX_FULL)
		mode |= MODER_FULLD;
	else
		mode &= ~MODER_FULLD;
	ethoc_write(priv, MODER, mode);

	phy_print_status(phydev);
}

static int ethoc_mdio_probe(struct net_device *dev)
{
	struct ethoc *priv = netdev_priv(dev);
	struct phy_device *phy;
	int err;

	if (priv->phy_id != -1)
		phy = mdiobus_get_phy(priv->mdio, priv->phy_id);
	else
		phy = phy_find_first(priv->mdio);

	if (!phy)
		return dev_err_probe(&dev->dev, -ENXIO, "no PHY found\n");

	priv->old_duplex = -1;
	priv->old_link = -1;

	err = phy_connect_direct(dev, phy, ethoc_mdio_poll,
				 PHY_INTERFACE_MODE_GMII);
	if (err)
		return dev_err_probe(&dev->dev, err, "could not attach to PHY\n");

	phy_set_max_speed(phy, SPEED_100);

	return 0;
}

static int ethoc_open(struct net_device *dev)
{
	struct ethoc *priv = netdev_priv(dev);
	int ret;

	ret = request_irq(dev->irq, ethoc_interrupt, IRQF_SHARED,
			dev->name, dev);
	if (ret)
		return ret;

	napi_enable(&priv->napi);

	ethoc_init_ring(priv, dev->mem_start);
	ethoc_reset(priv);

	if (netif_queue_stopped(dev)) {
		dev_dbg(&dev->dev, " resuming queue\n");
		netif_wake_queue(dev);
	} else {
		dev_dbg(&dev->dev, " starting queue\n");
		netif_start_queue(dev);
	}

	priv->old_link = -1;
	priv->old_duplex = -1;

	phy_start(dev->phydev);

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

	if (dev->phydev)
		phy_stop(dev->phydev);

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

		phy = mdiobus_get_phy(priv->mdio, mdio->phy_id);
		if (!phy)
			return -ENODEV;
	} else {
		phy = dev->phydev;
	}

	return phy_mii_ioctl(phy, ifr, cmd);
}

static void ethoc_do_set_mac_address(struct net_device *dev)
{
	const unsigned char *mac = dev->dev_addr;
	struct ethoc *priv = netdev_priv(dev);

	ethoc_write(priv, MAC_ADDR0, (mac[2] << 24) | (mac[3] << 16) |
				     (mac[4] <<  8) | (mac[5] <<  0));
	ethoc_write(priv, MAC_ADDR1, (mac[0] <<  8) | (mac[1] <<  0));
}

static int ethoc_set_mac_address(struct net_device *dev, void *p)
{
	const struct sockaddr *addr = p;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;
	eth_hw_addr_set(dev, addr->sa_data);
	ethoc_do_set_mac_address(dev);
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

static void ethoc_tx_timeout(struct net_device *dev, unsigned int txqueue)
{
	struct ethoc *priv = netdev_priv(dev);
	u32 pending = ethoc_read(priv, INT_SOURCE);
	if (likely(pending))
		ethoc_interrupt(dev->irq, dev);
}

static netdev_tx_t ethoc_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct ethoc *priv = netdev_priv(dev);
	struct ethoc_bd bd;
	unsigned int entry;
	void *dest;

	if (skb_put_padto(skb, ETHOC_ZLEN)) {
		dev->stats.tx_errors++;
		goto out_no_free;
	}

	if (unlikely(skb->len > ETHOC_BUFSIZ)) {
		dev->stats.tx_errors++;
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

	dest = priv->vma[entry];
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
	skb_tx_timestamp(skb);
out:
	dev_kfree_skb(skb);
out_no_free:
	return NETDEV_TX_OK;
}

static int ethoc_get_regs_len(struct net_device *netdev)
{
	return ETH_END;
}

static void ethoc_get_regs(struct net_device *dev, struct ethtool_regs *regs,
			   void *p)
{
	struct ethoc *priv = netdev_priv(dev);
	u32 *regs_buff = p;
	unsigned i;

	regs->version = 0;
	for (i = 0; i < ETH_END / sizeof(u32); ++i)
		regs_buff[i] = ethoc_read(priv, i * sizeof(u32));
}

static void ethoc_get_ringparam(struct net_device *dev,
				struct ethtool_ringparam *ring,
				struct kernel_ethtool_ringparam *kernel_ring,
				struct netlink_ext_ack *extack)
{
	struct ethoc *priv = netdev_priv(dev);

	ring->rx_max_pending = priv->num_bd - 1;
	ring->rx_mini_max_pending = 0;
	ring->rx_jumbo_max_pending = 0;
	ring->tx_max_pending = priv->num_bd - 1;

	ring->rx_pending = priv->num_rx;
	ring->rx_mini_pending = 0;
	ring->rx_jumbo_pending = 0;
	ring->tx_pending = priv->num_tx;
}

static int ethoc_set_ringparam(struct net_device *dev,
			       struct ethtool_ringparam *ring,
			       struct kernel_ethtool_ringparam *kernel_ring,
			       struct netlink_ext_ack *extack)
{
	struct ethoc *priv = netdev_priv(dev);

	if (ring->tx_pending < 1 || ring->rx_pending < 1 ||
	    ring->tx_pending + ring->rx_pending > priv->num_bd)
		return -EINVAL;
	if (ring->rx_mini_pending || ring->rx_jumbo_pending)
		return -EINVAL;

	if (netif_running(dev)) {
		netif_tx_disable(dev);
		ethoc_disable_rx_and_tx(priv);
		ethoc_disable_irq(priv, INT_MASK_TX | INT_MASK_RX);
		synchronize_irq(dev->irq);
	}

	priv->num_tx = rounddown_pow_of_two(ring->tx_pending);
	priv->num_rx = ring->rx_pending;
	ethoc_init_ring(priv, dev->mem_start);

	if (netif_running(dev)) {
		ethoc_enable_irq(priv, INT_MASK_TX | INT_MASK_RX);
		ethoc_enable_rx_and_tx(priv);
		netif_wake_queue(dev);
	}
	return 0;
}

static const struct ethtool_ops ethoc_ethtool_ops = {
	.get_regs_len = ethoc_get_regs_len,
	.get_regs = ethoc_get_regs,
	.nway_reset = phy_ethtool_nway_reset,
	.get_link = ethtool_op_get_link,
	.get_ringparam = ethoc_get_ringparam,
	.set_ringparam = ethoc_set_ringparam,
	.get_ts_info = ethtool_op_get_ts_info,
	.get_link_ksettings = phy_ethtool_get_link_ksettings,
	.set_link_ksettings = phy_ethtool_set_link_ksettings,
};

static const struct net_device_ops ethoc_netdev_ops = {
	.ndo_open = ethoc_open,
	.ndo_stop = ethoc_stop,
	.ndo_eth_ioctl = ethoc_ioctl,
	.ndo_set_mac_address = ethoc_set_mac_address,
	.ndo_set_rx_mode = ethoc_set_multicast_list,
	.ndo_change_mtu = ethoc_change_mtu,
	.ndo_tx_timeout = ethoc_tx_timeout,
	.ndo_start_xmit = ethoc_start_xmit,
};

/**
 * ethoc_probe - initialize OpenCores ethernet MAC
 * @pdev:	platform device
 */
static int ethoc_probe(struct platform_device *pdev)
{
	struct net_device *netdev = NULL;
	struct resource *res = NULL;
	struct resource *mmio = NULL;
	struct resource *mem = NULL;
	struct ethoc *priv = NULL;
	int num_bd;
	int ret = 0;
	struct ethoc_platform_data *pdata = dev_get_platdata(&pdev->dev);
	u32 eth_clkfreq = pdata ? pdata->eth_clkfreq : 0;

	/* allocate networking device */
	netdev = alloc_etherdev(sizeof(struct ethoc));
	if (!netdev) {
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
	ret = platform_get_irq(pdev, 0);
	if (ret < 0)
		goto free;

	netdev->irq = ret;

	/* setup driver-private data */
	priv = netdev_priv(netdev);
	priv->netdev = netdev;

	priv->iobase = devm_ioremap(&pdev->dev, netdev->base_addr,
			resource_size(mmio));
	if (!priv->iobase) {
		dev_err(&pdev->dev, "cannot remap I/O memory space\n");
		ret = -ENXIO;
		goto free;
	}

	if (netdev->mem_end) {
		priv->membase = devm_ioremap(&pdev->dev,
			netdev->mem_start, resource_size(mem));
		if (!priv->membase) {
			dev_err(&pdev->dev, "cannot remap memory space\n");
			ret = -ENXIO;
			goto free;
		}
	} else {
		/* Allocate buffer memory */
		priv->membase = dmam_alloc_coherent(&pdev->dev,
			buffer_size, (void *)&netdev->mem_start,
			GFP_KERNEL);
		if (!priv->membase) {
			dev_err(&pdev->dev, "cannot allocate %dB buffer\n",
				buffer_size);
			ret = -ENOMEM;
			goto free;
		}
		netdev->mem_end = netdev->mem_start + buffer_size;
	}

	priv->big_endian = pdata ? pdata->big_endian :
		of_device_is_big_endian(pdev->dev.of_node);

	/* calculate the number of TX/RX buffers, maximum 128 supported */
	num_bd = min_t(unsigned int,
		128, (netdev->mem_end - netdev->mem_start + 1) / ETHOC_BUFSIZ);
	if (num_bd < 4) {
		ret = -ENODEV;
		goto free;
	}
	priv->num_bd = num_bd;
	/* num_tx must be a power of two */
	priv->num_tx = rounddown_pow_of_two(num_bd >> 1);
	priv->num_rx = num_bd - priv->num_tx;

	dev_dbg(&pdev->dev, "ethoc: num_tx: %d num_rx: %d\n",
		priv->num_tx, priv->num_rx);

	priv->vma = devm_kcalloc(&pdev->dev, num_bd, sizeof(void *),
				 GFP_KERNEL);
	if (!priv->vma) {
		ret = -ENOMEM;
		goto free;
	}

	/* Allow the platform setup code to pass in a MAC address. */
	if (pdata) {
		eth_hw_addr_set(netdev, pdata->hwaddr);
		priv->phy_id = pdata->phy_id;
	} else {
		of_get_ethdev_address(pdev->dev.of_node, netdev);
		priv->phy_id = -1;
	}

	/* Check that the given MAC address is valid. If it isn't, read the
	 * current MAC from the controller.
	 */
	if (!is_valid_ether_addr(netdev->dev_addr)) {
		u8 addr[ETH_ALEN];

		ethoc_get_mac_address(netdev, addr);
		eth_hw_addr_set(netdev, addr);
	}

	/* Check the MAC again for validity, if it still isn't choose and
	 * program a random one.
	 */
	if (!is_valid_ether_addr(netdev->dev_addr))
		eth_hw_addr_random(netdev);

	ethoc_do_set_mac_address(netdev);

	/* Allow the platform setup code to adjust MII management bus clock. */
	if (!eth_clkfreq) {
		struct clk *clk = devm_clk_get(&pdev->dev, NULL);

		if (!IS_ERR(clk)) {
			priv->clk = clk;
			clk_prepare_enable(clk);
			eth_clkfreq = clk_get_rate(clk);
		}
	}
	if (eth_clkfreq) {
		u32 clkdiv = MIIMODER_CLKDIV(eth_clkfreq / 2500000 + 1);

		if (!clkdiv)
			clkdiv = 2;
		dev_dbg(&pdev->dev, "setting MII clkdiv to %u\n", clkdiv);
		ethoc_write(priv, MIIMODER,
			    (ethoc_read(priv, MIIMODER) & MIIMODER_NOPRE) |
			    clkdiv);
	}

	/* register MII bus */
	priv->mdio = mdiobus_alloc();
	if (!priv->mdio) {
		ret = -ENOMEM;
		goto free2;
	}

	priv->mdio->name = "ethoc-mdio";
	snprintf(priv->mdio->id, MII_BUS_ID_SIZE, "%s-%d",
			priv->mdio->name, pdev->id);
	priv->mdio->read = ethoc_mdio_read;
	priv->mdio->write = ethoc_mdio_write;
	priv->mdio->priv = priv;

	ret = mdiobus_register(priv->mdio);
	if (ret) {
		dev_err(&netdev->dev, "failed to register MDIO bus\n");
		goto free3;
	}

	ret = ethoc_mdio_probe(netdev);
	if (ret) {
		dev_err(&netdev->dev, "failed to probe MDIO bus\n");
		goto error;
	}

	/* setup the net_device structure */
	netdev->netdev_ops = &ethoc_netdev_ops;
	netdev->watchdog_timeo = ETHOC_TIMEOUT;
	netdev->features |= 0;
	netdev->ethtool_ops = &ethoc_ethtool_ops;

	/* setup NAPI */
	netif_napi_add(netdev, &priv->napi, ethoc_poll);

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
free3:
	mdiobus_free(priv->mdio);
free2:
	clk_disable_unprepare(priv->clk);
free:
	free_netdev(netdev);
out:
	return ret;
}

/**
 * ethoc_remove - shutdown OpenCores ethernet MAC
 * @pdev:	platform device
 */
static void ethoc_remove(struct platform_device *pdev)
{
	struct net_device *netdev = platform_get_drvdata(pdev);
	struct ethoc *priv = netdev_priv(netdev);

	if (netdev) {
		netif_napi_del(&priv->napi);
		phy_disconnect(netdev->phydev);

		if (priv->mdio) {
			mdiobus_unregister(priv->mdio);
			mdiobus_free(priv->mdio);
		}
		clk_disable_unprepare(priv->clk);
		unregister_netdev(netdev);
		free_netdev(netdev);
	}
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

static const struct of_device_id ethoc_match[] = {
	{ .compatible = "opencores,ethoc", },
	{},
};
MODULE_DEVICE_TABLE(of, ethoc_match);

static struct platform_driver ethoc_driver = {
	.probe   = ethoc_probe,
	.remove = ethoc_remove,
	.suspend = ethoc_suspend,
	.resume  = ethoc_resume,
	.driver  = {
		.name = "ethoc",
		.of_match_table = ethoc_match,
	},
};

module_platform_driver(ethoc_driver);

MODULE_AUTHOR("Thierry Reding <thierry.reding@avionic-design.de>");
MODULE_DESCRIPTION("OpenCores Ethernet MAC driver");
MODULE_LICENSE("GPL v2");

