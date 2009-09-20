/*
 * Intel IXP4xx Ethernet driver for Linux
 *
 * Copyright (C) 2007 Krzysztof Halasa <khc@pm.waw.pl>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 *
 * Ethernet port config (0x00 is not present on IXP42X):
 *
 * logical port		0x00		0x10		0x20
 * NPE			0 (NPE-A)	1 (NPE-B)	2 (NPE-C)
 * physical PortId	2		0		1
 * TX queue		23		24		25
 * RX-free queue	26		27		28
 * TX-done queue is always 31, per-port RX and TX-ready queues are configurable
 *
 *
 * Queue entries:
 * bits 0 -> 1	- NPE ID (RX and TX-done)
 * bits 0 -> 2	- priority (TX, per 802.1D)
 * bits 3 -> 4	- port ID (user-set?)
 * bits 5 -> 31	- physical descriptor address
 */

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/etherdevice.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <mach/npe.h>
#include <mach/qmgr.h>

#define DEBUG_DESC		0
#define DEBUG_RX		0
#define DEBUG_TX		0
#define DEBUG_PKT_BYTES		0
#define DEBUG_MDIO		0
#define DEBUG_CLOSE		0

#define DRV_NAME		"ixp4xx_eth"

#define MAX_NPES		3

#define RX_DESCS		64 /* also length of all RX queues */
#define TX_DESCS		16 /* also length of all TX queues */
#define TXDONE_QUEUE_LEN	64 /* dwords */

#define POOL_ALLOC_SIZE		(sizeof(struct desc) * (RX_DESCS + TX_DESCS))
#define REGS_SIZE		0x1000
#define MAX_MRU			1536 /* 0x600 */
#define RX_BUFF_SIZE		ALIGN((NET_IP_ALIGN) + MAX_MRU, 4)

#define NAPI_WEIGHT		16
#define MDIO_INTERVAL		(3 * HZ)
#define MAX_MDIO_RETRIES	100 /* microseconds, typically 30 cycles */
#define MAX_CLOSE_WAIT		1000 /* microseconds, typically 2-3 cycles */

#define NPE_ID(port_id)		((port_id) >> 4)
#define PHYSICAL_ID(port_id)	((NPE_ID(port_id) + 2) % 3)
#define TX_QUEUE(port_id)	(NPE_ID(port_id) + 23)
#define RXFREE_QUEUE(port_id)	(NPE_ID(port_id) + 26)
#define TXDONE_QUEUE		31

/* TX Control Registers */
#define TX_CNTRL0_TX_EN		0x01
#define TX_CNTRL0_HALFDUPLEX	0x02
#define TX_CNTRL0_RETRY		0x04
#define TX_CNTRL0_PAD_EN	0x08
#define TX_CNTRL0_APPEND_FCS	0x10
#define TX_CNTRL0_2DEFER	0x20
#define TX_CNTRL0_RMII		0x40 /* reduced MII */
#define TX_CNTRL1_RETRIES	0x0F /* 4 bits */

/* RX Control Registers */
#define RX_CNTRL0_RX_EN		0x01
#define RX_CNTRL0_PADSTRIP_EN	0x02
#define RX_CNTRL0_SEND_FCS	0x04
#define RX_CNTRL0_PAUSE_EN	0x08
#define RX_CNTRL0_LOOP_EN	0x10
#define RX_CNTRL0_ADDR_FLTR_EN	0x20
#define RX_CNTRL0_RX_RUNT_EN	0x40
#define RX_CNTRL0_BCAST_DIS	0x80
#define RX_CNTRL1_DEFER_EN	0x01

/* Core Control Register */
#define CORE_RESET		0x01
#define CORE_RX_FIFO_FLUSH	0x02
#define CORE_TX_FIFO_FLUSH	0x04
#define CORE_SEND_JAM		0x08
#define CORE_MDC_EN		0x10 /* MDIO using NPE-B ETH-0 only */

#define DEFAULT_TX_CNTRL0	(TX_CNTRL0_TX_EN | TX_CNTRL0_RETRY |	\
				 TX_CNTRL0_PAD_EN | TX_CNTRL0_APPEND_FCS | \
				 TX_CNTRL0_2DEFER)
#define DEFAULT_RX_CNTRL0	RX_CNTRL0_RX_EN
#define DEFAULT_CORE_CNTRL	CORE_MDC_EN


/* NPE message codes */
#define NPE_GETSTATUS			0x00
#define NPE_EDB_SETPORTADDRESS		0x01
#define NPE_EDB_GETMACADDRESSDATABASE	0x02
#define NPE_EDB_SETMACADDRESSSDATABASE	0x03
#define NPE_GETSTATS			0x04
#define NPE_RESETSTATS			0x05
#define NPE_SETMAXFRAMELENGTHS		0x06
#define NPE_VLAN_SETRXTAGMODE		0x07
#define NPE_VLAN_SETDEFAULTRXVID	0x08
#define NPE_VLAN_SETPORTVLANTABLEENTRY	0x09
#define NPE_VLAN_SETPORTVLANTABLERANGE	0x0A
#define NPE_VLAN_SETRXQOSENTRY		0x0B
#define NPE_VLAN_SETPORTIDEXTRACTIONMODE 0x0C
#define NPE_STP_SETBLOCKINGSTATE	0x0D
#define NPE_FW_SETFIREWALLMODE		0x0E
#define NPE_PC_SETFRAMECONTROLDURATIONID 0x0F
#define NPE_PC_SETAPMACTABLE		0x11
#define NPE_SETLOOPBACK_MODE		0x12
#define NPE_PC_SETBSSIDTABLE		0x13
#define NPE_ADDRESS_FILTER_CONFIG	0x14
#define NPE_APPENDFCSCONFIG		0x15
#define NPE_NOTIFY_MAC_RECOVERY_DONE	0x16
#define NPE_MAC_RECOVERY_START		0x17


#ifdef __ARMEB__
typedef struct sk_buff buffer_t;
#define free_buffer dev_kfree_skb
#define free_buffer_irq dev_kfree_skb_irq
#else
typedef void buffer_t;
#define free_buffer kfree
#define free_buffer_irq kfree
#endif

struct eth_regs {
	u32 tx_control[2], __res1[2];		/* 000 */
	u32 rx_control[2], __res2[2];		/* 010 */
	u32 random_seed, __res3[3];		/* 020 */
	u32 partial_empty_threshold, __res4;	/* 030 */
	u32 partial_full_threshold, __res5;	/* 038 */
	u32 tx_start_bytes, __res6[3];		/* 040 */
	u32 tx_deferral, rx_deferral, __res7[2];/* 050 */
	u32 tx_2part_deferral[2], __res8[2];	/* 060 */
	u32 slot_time, __res9[3];		/* 070 */
	u32 mdio_command[4];			/* 080 */
	u32 mdio_status[4];			/* 090 */
	u32 mcast_mask[6], __res10[2];		/* 0A0 */
	u32 mcast_addr[6], __res11[2];		/* 0C0 */
	u32 int_clock_threshold, __res12[3];	/* 0E0 */
	u32 hw_addr[6], __res13[61];		/* 0F0 */
	u32 core_control;			/* 1FC */
};

struct port {
	struct resource *mem_res;
	struct eth_regs __iomem *regs;
	struct npe *npe;
	struct net_device *netdev;
	struct napi_struct napi;
	struct phy_device *phydev;
	struct eth_plat_info *plat;
	buffer_t *rx_buff_tab[RX_DESCS], *tx_buff_tab[TX_DESCS];
	struct desc *desc_tab;	/* coherent */
	u32 desc_tab_phys;
	int id;			/* logical port ID */
	int speed, duplex;
	u8 firmware[4];
};

/* NPE message structure */
struct msg {
#ifdef __ARMEB__
	u8 cmd, eth_id, byte2, byte3;
	u8 byte4, byte5, byte6, byte7;
#else
	u8 byte3, byte2, eth_id, cmd;
	u8 byte7, byte6, byte5, byte4;
#endif
};

/* Ethernet packet descriptor */
struct desc {
	u32 next;		/* pointer to next buffer, unused */

#ifdef __ARMEB__
	u16 buf_len;		/* buffer length */
	u16 pkt_len;		/* packet length */
	u32 data;		/* pointer to data buffer in RAM */
	u8 dest_id;
	u8 src_id;
	u16 flags;
	u8 qos;
	u8 padlen;
	u16 vlan_tci;
#else
	u16 pkt_len;		/* packet length */
	u16 buf_len;		/* buffer length */
	u32 data;		/* pointer to data buffer in RAM */
	u16 flags;
	u8 src_id;
	u8 dest_id;
	u16 vlan_tci;
	u8 padlen;
	u8 qos;
#endif

#ifdef __ARMEB__
	u8 dst_mac_0, dst_mac_1, dst_mac_2, dst_mac_3;
	u8 dst_mac_4, dst_mac_5, src_mac_0, src_mac_1;
	u8 src_mac_2, src_mac_3, src_mac_4, src_mac_5;
#else
	u8 dst_mac_3, dst_mac_2, dst_mac_1, dst_mac_0;
	u8 src_mac_1, src_mac_0, dst_mac_5, dst_mac_4;
	u8 src_mac_5, src_mac_4, src_mac_3, src_mac_2;
#endif
};


#define rx_desc_phys(port, n)	((port)->desc_tab_phys +		\
				 (n) * sizeof(struct desc))
#define rx_desc_ptr(port, n)	(&(port)->desc_tab[n])

#define tx_desc_phys(port, n)	((port)->desc_tab_phys +		\
				 ((n) + RX_DESCS) * sizeof(struct desc))
#define tx_desc_ptr(port, n)	(&(port)->desc_tab[(n) + RX_DESCS])

#ifndef __ARMEB__
static inline void memcpy_swab32(u32 *dest, u32 *src, int cnt)
{
	int i;
	for (i = 0; i < cnt; i++)
		dest[i] = swab32(src[i]);
}
#endif

static spinlock_t mdio_lock;
static struct eth_regs __iomem *mdio_regs; /* mdio command and status only */
struct mii_bus *mdio_bus;
static int ports_open;
static struct port *npe_port_tab[MAX_NPES];
static struct dma_pool *dma_pool;


static int ixp4xx_mdio_cmd(struct mii_bus *bus, int phy_id, int location,
			   int write, u16 cmd)
{
	int cycles = 0;

	if (__raw_readl(&mdio_regs->mdio_command[3]) & 0x80) {
		printk(KERN_ERR "%s: MII not ready to transmit\n", bus->name);
		return -1;
	}

	if (write) {
		__raw_writel(cmd & 0xFF, &mdio_regs->mdio_command[0]);
		__raw_writel(cmd >> 8, &mdio_regs->mdio_command[1]);
	}
	__raw_writel(((phy_id << 5) | location) & 0xFF,
		     &mdio_regs->mdio_command[2]);
	__raw_writel((phy_id >> 3) | (write << 2) | 0x80 /* GO */,
		     &mdio_regs->mdio_command[3]);

	while ((cycles < MAX_MDIO_RETRIES) &&
	       (__raw_readl(&mdio_regs->mdio_command[3]) & 0x80)) {
		udelay(1);
		cycles++;
	}

	if (cycles == MAX_MDIO_RETRIES) {
		printk(KERN_ERR "%s #%i: MII write failed\n", bus->name,
		       phy_id);
		return -1;
	}

#if DEBUG_MDIO
	printk(KERN_DEBUG "%s #%i: mdio_%s() took %i cycles\n", bus->name,
	       phy_id, write ? "write" : "read", cycles);
#endif

	if (write)
		return 0;

	if (__raw_readl(&mdio_regs->mdio_status[3]) & 0x80) {
#if DEBUG_MDIO
		printk(KERN_DEBUG "%s #%i: MII read failed\n", bus->name,
		       phy_id);
#endif
		return 0xFFFF; /* don't return error */
	}

	return (__raw_readl(&mdio_regs->mdio_status[0]) & 0xFF) |
		((__raw_readl(&mdio_regs->mdio_status[1]) & 0xFF) << 8);
}

static int ixp4xx_mdio_read(struct mii_bus *bus, int phy_id, int location)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&mdio_lock, flags);
	ret = ixp4xx_mdio_cmd(bus, phy_id, location, 0, 0);
	spin_unlock_irqrestore(&mdio_lock, flags);
#if DEBUG_MDIO
	printk(KERN_DEBUG "%s #%i: MII read [%i] -> 0x%X\n", bus->name,
	       phy_id, location, ret);
#endif
	return ret;
}

static int ixp4xx_mdio_write(struct mii_bus *bus, int phy_id, int location,
			     u16 val)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&mdio_lock, flags);
	ret = ixp4xx_mdio_cmd(bus, phy_id, location, 1, val);
	spin_unlock_irqrestore(&mdio_lock, flags);
#if DEBUG_MDIO
	printk(KERN_DEBUG "%s #%i: MII read [%i] <- 0x%X, err = %i\n",
	       bus->name, phy_id, location, val, ret);
#endif
	return ret;
}

static int ixp4xx_mdio_register(void)
{
	int err;

	if (!(mdio_bus = mdiobus_alloc()))
		return -ENOMEM;

	if (cpu_is_ixp43x()) {
		/* IXP43x lacks NPE-B and uses NPE-C for MII PHY access */
		if (!(ixp4xx_read_feature_bits() & IXP4XX_FEATURE_NPEC_ETH))
			return -ENODEV;
		mdio_regs = (struct eth_regs __iomem *)IXP4XX_EthC_BASE_VIRT;
	} else {
		/* All MII PHY accesses use NPE-B Ethernet registers */
		if (!(ixp4xx_read_feature_bits() & IXP4XX_FEATURE_NPEB_ETH0))
			return -ENODEV;
		mdio_regs = (struct eth_regs __iomem *)IXP4XX_EthB_BASE_VIRT;
	}

	__raw_writel(DEFAULT_CORE_CNTRL, &mdio_regs->core_control);
	spin_lock_init(&mdio_lock);
	mdio_bus->name = "IXP4xx MII Bus";
	mdio_bus->read = &ixp4xx_mdio_read;
	mdio_bus->write = &ixp4xx_mdio_write;
	strcpy(mdio_bus->id, "0");

	if ((err = mdiobus_register(mdio_bus)))
		mdiobus_free(mdio_bus);
	return err;
}

static void ixp4xx_mdio_remove(void)
{
	mdiobus_unregister(mdio_bus);
	mdiobus_free(mdio_bus);
}


static void ixp4xx_adjust_link(struct net_device *dev)
{
	struct port *port = netdev_priv(dev);
	struct phy_device *phydev = port->phydev;

	if (!phydev->link) {
		if (port->speed) {
			port->speed = 0;
			printk(KERN_INFO "%s: link down\n", dev->name);
		}
		return;
	}

	if (port->speed == phydev->speed && port->duplex == phydev->duplex)
		return;

	port->speed = phydev->speed;
	port->duplex = phydev->duplex;

	if (port->duplex)
		__raw_writel(DEFAULT_TX_CNTRL0 & ~TX_CNTRL0_HALFDUPLEX,
			     &port->regs->tx_control[0]);
	else
		__raw_writel(DEFAULT_TX_CNTRL0 | TX_CNTRL0_HALFDUPLEX,
			     &port->regs->tx_control[0]);

	printk(KERN_INFO "%s: link up, speed %u Mb/s, %s duplex\n",
	       dev->name, port->speed, port->duplex ? "full" : "half");
}


static inline void debug_pkt(struct net_device *dev, const char *func,
			     u8 *data, int len)
{
#if DEBUG_PKT_BYTES
	int i;

	printk(KERN_DEBUG "%s: %s(%i) ", dev->name, func, len);
	for (i = 0; i < len; i++) {
		if (i >= DEBUG_PKT_BYTES)
			break;
		printk("%s%02X",
		       ((i == 6) || (i == 12) || (i >= 14)) ? " " : "",
		       data[i]);
	}
	printk("\n");
#endif
}


static inline void debug_desc(u32 phys, struct desc *desc)
{
#if DEBUG_DESC
	printk(KERN_DEBUG "%X: %X %3X %3X %08X %2X < %2X %4X %X"
	       " %X %X %02X%02X%02X%02X%02X%02X < %02X%02X%02X%02X%02X%02X\n",
	       phys, desc->next, desc->buf_len, desc->pkt_len,
	       desc->data, desc->dest_id, desc->src_id, desc->flags,
	       desc->qos, desc->padlen, desc->vlan_tci,
	       desc->dst_mac_0, desc->dst_mac_1, desc->dst_mac_2,
	       desc->dst_mac_3, desc->dst_mac_4, desc->dst_mac_5,
	       desc->src_mac_0, desc->src_mac_1, desc->src_mac_2,
	       desc->src_mac_3, desc->src_mac_4, desc->src_mac_5);
#endif
}

static inline int queue_get_desc(unsigned int queue, struct port *port,
				 int is_tx)
{
	u32 phys, tab_phys, n_desc;
	struct desc *tab;

	if (!(phys = qmgr_get_entry(queue)))
		return -1;

	phys &= ~0x1F; /* mask out non-address bits */
	tab_phys = is_tx ? tx_desc_phys(port, 0) : rx_desc_phys(port, 0);
	tab = is_tx ? tx_desc_ptr(port, 0) : rx_desc_ptr(port, 0);
	n_desc = (phys - tab_phys) / sizeof(struct desc);
	BUG_ON(n_desc >= (is_tx ? TX_DESCS : RX_DESCS));
	debug_desc(phys, &tab[n_desc]);
	BUG_ON(tab[n_desc].next);
	return n_desc;
}

static inline void queue_put_desc(unsigned int queue, u32 phys,
				  struct desc *desc)
{
	debug_desc(phys, desc);
	BUG_ON(phys & 0x1F);
	qmgr_put_entry(queue, phys);
	/* Don't check for queue overflow here, we've allocated sufficient
	   length and queues >= 32 don't support this check anyway. */
}


static inline void dma_unmap_tx(struct port *port, struct desc *desc)
{
#ifdef __ARMEB__
	dma_unmap_single(&port->netdev->dev, desc->data,
			 desc->buf_len, DMA_TO_DEVICE);
#else
	dma_unmap_single(&port->netdev->dev, desc->data & ~3,
			 ALIGN((desc->data & 3) + desc->buf_len, 4),
			 DMA_TO_DEVICE);
#endif
}


static void eth_rx_irq(void *pdev)
{
	struct net_device *dev = pdev;
	struct port *port = netdev_priv(dev);

#if DEBUG_RX
	printk(KERN_DEBUG "%s: eth_rx_irq\n", dev->name);
#endif
	qmgr_disable_irq(port->plat->rxq);
	napi_schedule(&port->napi);
}

static int eth_poll(struct napi_struct *napi, int budget)
{
	struct port *port = container_of(napi, struct port, napi);
	struct net_device *dev = port->netdev;
	unsigned int rxq = port->plat->rxq, rxfreeq = RXFREE_QUEUE(port->id);
	int received = 0;

#if DEBUG_RX
	printk(KERN_DEBUG "%s: eth_poll\n", dev->name);
#endif

	while (received < budget) {
		struct sk_buff *skb;
		struct desc *desc;
		int n;
#ifdef __ARMEB__
		struct sk_buff *temp;
		u32 phys;
#endif

		if ((n = queue_get_desc(rxq, port, 0)) < 0) {
#if DEBUG_RX
			printk(KERN_DEBUG "%s: eth_poll napi_complete\n",
			       dev->name);
#endif
			napi_complete(napi);
			qmgr_enable_irq(rxq);
			if (!qmgr_stat_below_low_watermark(rxq) &&
			    napi_reschedule(napi)) { /* not empty again */
#if DEBUG_RX
				printk(KERN_DEBUG "%s: eth_poll"
				       " napi_reschedule successed\n",
				       dev->name);
#endif
				qmgr_disable_irq(rxq);
				continue;
			}
#if DEBUG_RX
			printk(KERN_DEBUG "%s: eth_poll all done\n",
			       dev->name);
#endif
			return received; /* all work done */
		}

		desc = rx_desc_ptr(port, n);

#ifdef __ARMEB__
		if ((skb = netdev_alloc_skb(dev, RX_BUFF_SIZE))) {
			phys = dma_map_single(&dev->dev, skb->data,
					      RX_BUFF_SIZE, DMA_FROM_DEVICE);
			if (dma_mapping_error(&dev->dev, phys)) {
				dev_kfree_skb(skb);
				skb = NULL;
			}
		}
#else
		skb = netdev_alloc_skb(dev,
				       ALIGN(NET_IP_ALIGN + desc->pkt_len, 4));
#endif

		if (!skb) {
			dev->stats.rx_dropped++;
			/* put the desc back on RX-ready queue */
			desc->buf_len = MAX_MRU;
			desc->pkt_len = 0;
			queue_put_desc(rxfreeq, rx_desc_phys(port, n), desc);
			continue;
		}

		/* process received frame */
#ifdef __ARMEB__
		temp = skb;
		skb = port->rx_buff_tab[n];
		dma_unmap_single(&dev->dev, desc->data - NET_IP_ALIGN,
				 RX_BUFF_SIZE, DMA_FROM_DEVICE);
#else
		dma_sync_single_for_cpu(&dev->dev, desc->data - NET_IP_ALIGN,
					RX_BUFF_SIZE, DMA_FROM_DEVICE);
		memcpy_swab32((u32 *)skb->data, (u32 *)port->rx_buff_tab[n],
			      ALIGN(NET_IP_ALIGN + desc->pkt_len, 4) / 4);
#endif
		skb_reserve(skb, NET_IP_ALIGN);
		skb_put(skb, desc->pkt_len);

		debug_pkt(dev, "eth_poll", skb->data, skb->len);

		skb->protocol = eth_type_trans(skb, dev);
		dev->stats.rx_packets++;
		dev->stats.rx_bytes += skb->len;
		netif_receive_skb(skb);

		/* put the new buffer on RX-free queue */
#ifdef __ARMEB__
		port->rx_buff_tab[n] = temp;
		desc->data = phys + NET_IP_ALIGN;
#endif
		desc->buf_len = MAX_MRU;
		desc->pkt_len = 0;
		queue_put_desc(rxfreeq, rx_desc_phys(port, n), desc);
		received++;
	}

#if DEBUG_RX
	printk(KERN_DEBUG "eth_poll(): end, not all work done\n");
#endif
	return received;		/* not all work done */
}


static void eth_txdone_irq(void *unused)
{
	u32 phys;

#if DEBUG_TX
	printk(KERN_DEBUG DRV_NAME ": eth_txdone_irq\n");
#endif
	while ((phys = qmgr_get_entry(TXDONE_QUEUE)) != 0) {
		u32 npe_id, n_desc;
		struct port *port;
		struct desc *desc;
		int start;

		npe_id = phys & 3;
		BUG_ON(npe_id >= MAX_NPES);
		port = npe_port_tab[npe_id];
		BUG_ON(!port);
		phys &= ~0x1F; /* mask out non-address bits */
		n_desc = (phys - tx_desc_phys(port, 0)) / sizeof(struct desc);
		BUG_ON(n_desc >= TX_DESCS);
		desc = tx_desc_ptr(port, n_desc);
		debug_desc(phys, desc);

		if (port->tx_buff_tab[n_desc]) { /* not the draining packet */
			port->netdev->stats.tx_packets++;
			port->netdev->stats.tx_bytes += desc->pkt_len;

			dma_unmap_tx(port, desc);
#if DEBUG_TX
			printk(KERN_DEBUG "%s: eth_txdone_irq free %p\n",
			       port->netdev->name, port->tx_buff_tab[n_desc]);
#endif
			free_buffer_irq(port->tx_buff_tab[n_desc]);
			port->tx_buff_tab[n_desc] = NULL;
		}

		start = qmgr_stat_below_low_watermark(port->plat->txreadyq);
		queue_put_desc(port->plat->txreadyq, phys, desc);
		if (start) { /* TX-ready queue was empty */
#if DEBUG_TX
			printk(KERN_DEBUG "%s: eth_txdone_irq xmit ready\n",
			       port->netdev->name);
#endif
			netif_wake_queue(port->netdev);
		}
	}
}

static int eth_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct port *port = netdev_priv(dev);
	unsigned int txreadyq = port->plat->txreadyq;
	int len, offset, bytes, n;
	void *mem;
	u32 phys;
	struct desc *desc;

#if DEBUG_TX
	printk(KERN_DEBUG "%s: eth_xmit\n", dev->name);
#endif

	if (unlikely(skb->len > MAX_MRU)) {
		dev_kfree_skb(skb);
		dev->stats.tx_errors++;
		return NETDEV_TX_OK;
	}

	debug_pkt(dev, "eth_xmit", skb->data, skb->len);

	len = skb->len;
#ifdef __ARMEB__
	offset = 0; /* no need to keep alignment */
	bytes = len;
	mem = skb->data;
#else
	offset = (int)skb->data & 3; /* keep 32-bit alignment */
	bytes = ALIGN(offset + len, 4);
	if (!(mem = kmalloc(bytes, GFP_ATOMIC))) {
		dev_kfree_skb(skb);
		dev->stats.tx_dropped++;
		return NETDEV_TX_OK;
	}
	memcpy_swab32(mem, (u32 *)((int)skb->data & ~3), bytes / 4);
	dev_kfree_skb(skb);
#endif

	phys = dma_map_single(&dev->dev, mem, bytes, DMA_TO_DEVICE);
	if (dma_mapping_error(&dev->dev, phys)) {
#ifdef __ARMEB__
		dev_kfree_skb(skb);
#else
		kfree(mem);
#endif
		dev->stats.tx_dropped++;
		return NETDEV_TX_OK;
	}

	n = queue_get_desc(txreadyq, port, 1);
	BUG_ON(n < 0);
	desc = tx_desc_ptr(port, n);

#ifdef __ARMEB__
	port->tx_buff_tab[n] = skb;
#else
	port->tx_buff_tab[n] = mem;
#endif
	desc->data = phys + offset;
	desc->buf_len = desc->pkt_len = len;

	/* NPE firmware pads short frames with zeros internally */
	wmb();
	queue_put_desc(TX_QUEUE(port->id), tx_desc_phys(port, n), desc);
	dev->trans_start = jiffies;

	if (qmgr_stat_below_low_watermark(txreadyq)) { /* empty */
#if DEBUG_TX
		printk(KERN_DEBUG "%s: eth_xmit queue full\n", dev->name);
#endif
		netif_stop_queue(dev);
		/* we could miss TX ready interrupt */
		/* really empty in fact */
		if (!qmgr_stat_below_low_watermark(txreadyq)) {
#if DEBUG_TX
			printk(KERN_DEBUG "%s: eth_xmit ready again\n",
			       dev->name);
#endif
			netif_wake_queue(dev);
		}
	}

#if DEBUG_TX
	printk(KERN_DEBUG "%s: eth_xmit end\n", dev->name);
#endif
	return NETDEV_TX_OK;
}


static void eth_set_mcast_list(struct net_device *dev)
{
	struct port *port = netdev_priv(dev);
	struct dev_mc_list *mclist = dev->mc_list;
	u8 diffs[ETH_ALEN], *addr;
	int cnt = dev->mc_count, i;

	if ((dev->flags & IFF_PROMISC) || !mclist || !cnt) {
		__raw_writel(DEFAULT_RX_CNTRL0 & ~RX_CNTRL0_ADDR_FLTR_EN,
			     &port->regs->rx_control[0]);
		return;
	}

	memset(diffs, 0, ETH_ALEN);
	addr = mclist->dmi_addr; /* first MAC address */

	while (--cnt && (mclist = mclist->next))
		for (i = 0; i < ETH_ALEN; i++)
			diffs[i] |= addr[i] ^ mclist->dmi_addr[i];

	for (i = 0; i < ETH_ALEN; i++) {
		__raw_writel(addr[i], &port->regs->mcast_addr[i]);
		__raw_writel(~diffs[i], &port->regs->mcast_mask[i]);
	}

	__raw_writel(DEFAULT_RX_CNTRL0 | RX_CNTRL0_ADDR_FLTR_EN,
		     &port->regs->rx_control[0]);
}


static int eth_ioctl(struct net_device *dev, struct ifreq *req, int cmd)
{
	struct port *port = netdev_priv(dev);

	if (!netif_running(dev))
		return -EINVAL;
	return phy_mii_ioctl(port->phydev, if_mii(req), cmd);
}

/* ethtool support */

static void ixp4xx_get_drvinfo(struct net_device *dev,
			       struct ethtool_drvinfo *info)
{
	struct port *port = netdev_priv(dev);
	strcpy(info->driver, DRV_NAME);
	snprintf(info->fw_version, sizeof(info->fw_version), "%u:%u:%u:%u",
		 port->firmware[0], port->firmware[1],
		 port->firmware[2], port->firmware[3]);
	strcpy(info->bus_info, "internal");
}

static int ixp4xx_get_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct port *port = netdev_priv(dev);
	return phy_ethtool_gset(port->phydev, cmd);
}

static int ixp4xx_set_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct port *port = netdev_priv(dev);
	return phy_ethtool_sset(port->phydev, cmd);
}

static int ixp4xx_nway_reset(struct net_device *dev)
{
	struct port *port = netdev_priv(dev);
	return phy_start_aneg(port->phydev);
}

static const struct ethtool_ops ixp4xx_ethtool_ops = {
	.get_drvinfo = ixp4xx_get_drvinfo,
	.get_settings = ixp4xx_get_settings,
	.set_settings = ixp4xx_set_settings,
	.nway_reset = ixp4xx_nway_reset,
	.get_link = ethtool_op_get_link,
};


static int request_queues(struct port *port)
{
	int err;

	err = qmgr_request_queue(RXFREE_QUEUE(port->id), RX_DESCS, 0, 0,
				 "%s:RX-free", port->netdev->name);
	if (err)
		return err;

	err = qmgr_request_queue(port->plat->rxq, RX_DESCS, 0, 0,
				 "%s:RX", port->netdev->name);
	if (err)
		goto rel_rxfree;

	err = qmgr_request_queue(TX_QUEUE(port->id), TX_DESCS, 0, 0,
				 "%s:TX", port->netdev->name);
	if (err)
		goto rel_rx;

	err = qmgr_request_queue(port->plat->txreadyq, TX_DESCS, 0, 0,
				 "%s:TX-ready", port->netdev->name);
	if (err)
		goto rel_tx;

	/* TX-done queue handles skbs sent out by the NPEs */
	if (!ports_open) {
		err = qmgr_request_queue(TXDONE_QUEUE, TXDONE_QUEUE_LEN, 0, 0,
					 "%s:TX-done", DRV_NAME);
		if (err)
			goto rel_txready;
	}
	return 0;

rel_txready:
	qmgr_release_queue(port->plat->txreadyq);
rel_tx:
	qmgr_release_queue(TX_QUEUE(port->id));
rel_rx:
	qmgr_release_queue(port->plat->rxq);
rel_rxfree:
	qmgr_release_queue(RXFREE_QUEUE(port->id));
	printk(KERN_DEBUG "%s: unable to request hardware queues\n",
	       port->netdev->name);
	return err;
}

static void release_queues(struct port *port)
{
	qmgr_release_queue(RXFREE_QUEUE(port->id));
	qmgr_release_queue(port->plat->rxq);
	qmgr_release_queue(TX_QUEUE(port->id));
	qmgr_release_queue(port->plat->txreadyq);

	if (!ports_open)
		qmgr_release_queue(TXDONE_QUEUE);
}

static int init_queues(struct port *port)
{
	int i;

	if (!ports_open)
		if (!(dma_pool = dma_pool_create(DRV_NAME, NULL,
						 POOL_ALLOC_SIZE, 32, 0)))
			return -ENOMEM;

	if (!(port->desc_tab = dma_pool_alloc(dma_pool, GFP_KERNEL,
					      &port->desc_tab_phys)))
		return -ENOMEM;
	memset(port->desc_tab, 0, POOL_ALLOC_SIZE);
	memset(port->rx_buff_tab, 0, sizeof(port->rx_buff_tab)); /* tables */
	memset(port->tx_buff_tab, 0, sizeof(port->tx_buff_tab));

	/* Setup RX buffers */
	for (i = 0; i < RX_DESCS; i++) {
		struct desc *desc = rx_desc_ptr(port, i);
		buffer_t *buff; /* skb or kmalloc()ated memory */
		void *data;
#ifdef __ARMEB__
		if (!(buff = netdev_alloc_skb(port->netdev, RX_BUFF_SIZE)))
			return -ENOMEM;
		data = buff->data;
#else
		if (!(buff = kmalloc(RX_BUFF_SIZE, GFP_KERNEL)))
			return -ENOMEM;
		data = buff;
#endif
		desc->buf_len = MAX_MRU;
		desc->data = dma_map_single(&port->netdev->dev, data,
					    RX_BUFF_SIZE, DMA_FROM_DEVICE);
		if (dma_mapping_error(&port->netdev->dev, desc->data)) {
			free_buffer(buff);
			return -EIO;
		}
		desc->data += NET_IP_ALIGN;
		port->rx_buff_tab[i] = buff;
	}

	return 0;
}

static void destroy_queues(struct port *port)
{
	int i;

	if (port->desc_tab) {
		for (i = 0; i < RX_DESCS; i++) {
			struct desc *desc = rx_desc_ptr(port, i);
			buffer_t *buff = port->rx_buff_tab[i];
			if (buff) {
				dma_unmap_single(&port->netdev->dev,
						 desc->data - NET_IP_ALIGN,
						 RX_BUFF_SIZE, DMA_FROM_DEVICE);
				free_buffer(buff);
			}
		}
		for (i = 0; i < TX_DESCS; i++) {
			struct desc *desc = tx_desc_ptr(port, i);
			buffer_t *buff = port->tx_buff_tab[i];
			if (buff) {
				dma_unmap_tx(port, desc);
				free_buffer(buff);
			}
		}
		dma_pool_free(dma_pool, port->desc_tab, port->desc_tab_phys);
		port->desc_tab = NULL;
	}

	if (!ports_open && dma_pool) {
		dma_pool_destroy(dma_pool);
		dma_pool = NULL;
	}
}

static int eth_open(struct net_device *dev)
{
	struct port *port = netdev_priv(dev);
	struct npe *npe = port->npe;
	struct msg msg;
	int i, err;

	if (!npe_running(npe)) {
		err = npe_load_firmware(npe, npe_name(npe), &dev->dev);
		if (err)
			return err;

		if (npe_recv_message(npe, &msg, "ETH_GET_STATUS")) {
			printk(KERN_ERR "%s: %s not responding\n", dev->name,
			       npe_name(npe));
			return -EIO;
		}
		port->firmware[0] = msg.byte4;
		port->firmware[1] = msg.byte5;
		port->firmware[2] = msg.byte6;
		port->firmware[3] = msg.byte7;
	}

	memset(&msg, 0, sizeof(msg));
	msg.cmd = NPE_VLAN_SETRXQOSENTRY;
	msg.eth_id = port->id;
	msg.byte5 = port->plat->rxq | 0x80;
	msg.byte7 = port->plat->rxq << 4;
	for (i = 0; i < 8; i++) {
		msg.byte3 = i;
		if (npe_send_recv_message(port->npe, &msg, "ETH_SET_RXQ"))
			return -EIO;
	}

	msg.cmd = NPE_EDB_SETPORTADDRESS;
	msg.eth_id = PHYSICAL_ID(port->id);
	msg.byte2 = dev->dev_addr[0];
	msg.byte3 = dev->dev_addr[1];
	msg.byte4 = dev->dev_addr[2];
	msg.byte5 = dev->dev_addr[3];
	msg.byte6 = dev->dev_addr[4];
	msg.byte7 = dev->dev_addr[5];
	if (npe_send_recv_message(port->npe, &msg, "ETH_SET_MAC"))
		return -EIO;

	memset(&msg, 0, sizeof(msg));
	msg.cmd = NPE_FW_SETFIREWALLMODE;
	msg.eth_id = port->id;
	if (npe_send_recv_message(port->npe, &msg, "ETH_SET_FIREWALL_MODE"))
		return -EIO;

	if ((err = request_queues(port)) != 0)
		return err;

	if ((err = init_queues(port)) != 0) {
		destroy_queues(port);
		release_queues(port);
		return err;
	}

	port->speed = 0;	/* force "link up" message */
	phy_start(port->phydev);

	for (i = 0; i < ETH_ALEN; i++)
		__raw_writel(dev->dev_addr[i], &port->regs->hw_addr[i]);
	__raw_writel(0x08, &port->regs->random_seed);
	__raw_writel(0x12, &port->regs->partial_empty_threshold);
	__raw_writel(0x30, &port->regs->partial_full_threshold);
	__raw_writel(0x08, &port->regs->tx_start_bytes);
	__raw_writel(0x15, &port->regs->tx_deferral);
	__raw_writel(0x08, &port->regs->tx_2part_deferral[0]);
	__raw_writel(0x07, &port->regs->tx_2part_deferral[1]);
	__raw_writel(0x80, &port->regs->slot_time);
	__raw_writel(0x01, &port->regs->int_clock_threshold);

	/* Populate queues with buffers, no failure after this point */
	for (i = 0; i < TX_DESCS; i++)
		queue_put_desc(port->plat->txreadyq,
			       tx_desc_phys(port, i), tx_desc_ptr(port, i));

	for (i = 0; i < RX_DESCS; i++)
		queue_put_desc(RXFREE_QUEUE(port->id),
			       rx_desc_phys(port, i), rx_desc_ptr(port, i));

	__raw_writel(TX_CNTRL1_RETRIES, &port->regs->tx_control[1]);
	__raw_writel(DEFAULT_TX_CNTRL0, &port->regs->tx_control[0]);
	__raw_writel(0, &port->regs->rx_control[1]);
	__raw_writel(DEFAULT_RX_CNTRL0, &port->regs->rx_control[0]);

	napi_enable(&port->napi);
	eth_set_mcast_list(dev);
	netif_start_queue(dev);

	qmgr_set_irq(port->plat->rxq, QUEUE_IRQ_SRC_NOT_EMPTY,
		     eth_rx_irq, dev);
	if (!ports_open) {
		qmgr_set_irq(TXDONE_QUEUE, QUEUE_IRQ_SRC_NOT_EMPTY,
			     eth_txdone_irq, NULL);
		qmgr_enable_irq(TXDONE_QUEUE);
	}
	ports_open++;
	/* we may already have RX data, enables IRQ */
	napi_schedule(&port->napi);
	return 0;
}

static int eth_close(struct net_device *dev)
{
	struct port *port = netdev_priv(dev);
	struct msg msg;
	int buffs = RX_DESCS; /* allocated RX buffers */
	int i;

	ports_open--;
	qmgr_disable_irq(port->plat->rxq);
	napi_disable(&port->napi);
	netif_stop_queue(dev);

	while (queue_get_desc(RXFREE_QUEUE(port->id), port, 0) >= 0)
		buffs--;

	memset(&msg, 0, sizeof(msg));
	msg.cmd = NPE_SETLOOPBACK_MODE;
	msg.eth_id = port->id;
	msg.byte3 = 1;
	if (npe_send_recv_message(port->npe, &msg, "ETH_ENABLE_LOOPBACK"))
		printk(KERN_CRIT "%s: unable to enable loopback\n", dev->name);

	i = 0;
	do {			/* drain RX buffers */
		while (queue_get_desc(port->plat->rxq, port, 0) >= 0)
			buffs--;
		if (!buffs)
			break;
		if (qmgr_stat_empty(TX_QUEUE(port->id))) {
			/* we have to inject some packet */
			struct desc *desc;
			u32 phys;
			int n = queue_get_desc(port->plat->txreadyq, port, 1);
			BUG_ON(n < 0);
			desc = tx_desc_ptr(port, n);
			phys = tx_desc_phys(port, n);
			desc->buf_len = desc->pkt_len = 1;
			wmb();
			queue_put_desc(TX_QUEUE(port->id), phys, desc);
		}
		udelay(1);
	} while (++i < MAX_CLOSE_WAIT);

	if (buffs)
		printk(KERN_CRIT "%s: unable to drain RX queue, %i buffer(s)"
		       " left in NPE\n", dev->name, buffs);
#if DEBUG_CLOSE
	if (!buffs)
		printk(KERN_DEBUG "Draining RX queue took %i cycles\n", i);
#endif

	buffs = TX_DESCS;
	while (queue_get_desc(TX_QUEUE(port->id), port, 1) >= 0)
		buffs--; /* cancel TX */

	i = 0;
	do {
		while (queue_get_desc(port->plat->txreadyq, port, 1) >= 0)
			buffs--;
		if (!buffs)
			break;
	} while (++i < MAX_CLOSE_WAIT);

	if (buffs)
		printk(KERN_CRIT "%s: unable to drain TX queue, %i buffer(s) "
		       "left in NPE\n", dev->name, buffs);
#if DEBUG_CLOSE
	if (!buffs)
		printk(KERN_DEBUG "Draining TX queues took %i cycles\n", i);
#endif

	msg.byte3 = 0;
	if (npe_send_recv_message(port->npe, &msg, "ETH_DISABLE_LOOPBACK"))
		printk(KERN_CRIT "%s: unable to disable loopback\n",
		       dev->name);

	phy_stop(port->phydev);

	if (!ports_open)
		qmgr_disable_irq(TXDONE_QUEUE);
	destroy_queues(port);
	release_queues(port);
	return 0;
}

static const struct net_device_ops ixp4xx_netdev_ops = {
	.ndo_open = eth_open,
	.ndo_stop = eth_close,
	.ndo_start_xmit = eth_xmit,
	.ndo_set_multicast_list = eth_set_mcast_list,
	.ndo_do_ioctl = eth_ioctl,
	.ndo_change_mtu = eth_change_mtu,
	.ndo_set_mac_address = eth_mac_addr,
	.ndo_validate_addr = eth_validate_addr,
};

static int __devinit eth_init_one(struct platform_device *pdev)
{
	struct port *port;
	struct net_device *dev;
	struct eth_plat_info *plat = pdev->dev.platform_data;
	u32 regs_phys;
	char phy_id[MII_BUS_ID_SIZE + 3];
	int err;

	if (!(dev = alloc_etherdev(sizeof(struct port))))
		return -ENOMEM;

	SET_NETDEV_DEV(dev, &pdev->dev);
	port = netdev_priv(dev);
	port->netdev = dev;
	port->id = pdev->id;

	switch (port->id) {
	case IXP4XX_ETH_NPEA:
		port->regs = (struct eth_regs __iomem *)IXP4XX_EthA_BASE_VIRT;
		regs_phys  = IXP4XX_EthA_BASE_PHYS;
		break;
	case IXP4XX_ETH_NPEB:
		port->regs = (struct eth_regs __iomem *)IXP4XX_EthB_BASE_VIRT;
		regs_phys  = IXP4XX_EthB_BASE_PHYS;
		break;
	case IXP4XX_ETH_NPEC:
		port->regs = (struct eth_regs __iomem *)IXP4XX_EthC_BASE_VIRT;
		regs_phys  = IXP4XX_EthC_BASE_PHYS;
		break;
	default:
		err = -ENODEV;
		goto err_free;
	}

	dev->netdev_ops = &ixp4xx_netdev_ops;
	dev->ethtool_ops = &ixp4xx_ethtool_ops;
	dev->tx_queue_len = 100;

	netif_napi_add(dev, &port->napi, eth_poll, NAPI_WEIGHT);

	if (!(port->npe = npe_request(NPE_ID(port->id)))) {
		err = -EIO;
		goto err_free;
	}

	port->mem_res = request_mem_region(regs_phys, REGS_SIZE, dev->name);
	if (!port->mem_res) {
		err = -EBUSY;
		goto err_npe_rel;
	}

	port->plat = plat;
	npe_port_tab[NPE_ID(port->id)] = port;
	memcpy(dev->dev_addr, plat->hwaddr, ETH_ALEN);

	platform_set_drvdata(pdev, dev);

	__raw_writel(DEFAULT_CORE_CNTRL | CORE_RESET,
		     &port->regs->core_control);
	udelay(50);
	__raw_writel(DEFAULT_CORE_CNTRL, &port->regs->core_control);
	udelay(50);

	snprintf(phy_id, MII_BUS_ID_SIZE + 3, PHY_ID_FMT, "0", plat->phy);
	port->phydev = phy_connect(dev, phy_id, &ixp4xx_adjust_link, 0,
				   PHY_INTERFACE_MODE_MII);
	if ((err = IS_ERR(port->phydev)))
		goto err_free_mem;

	port->phydev->irq = PHY_POLL;

	if ((err = register_netdev(dev)))
		goto err_phy_dis;

	printk(KERN_INFO "%s: MII PHY %i on %s\n", dev->name, plat->phy,
	       npe_name(port->npe));

	return 0;

err_phy_dis:
	phy_disconnect(port->phydev);
err_free_mem:
	npe_port_tab[NPE_ID(port->id)] = NULL;
	platform_set_drvdata(pdev, NULL);
	release_resource(port->mem_res);
err_npe_rel:
	npe_release(port->npe);
err_free:
	free_netdev(dev);
	return err;
}

static int __devexit eth_remove_one(struct platform_device *pdev)
{
	struct net_device *dev = platform_get_drvdata(pdev);
	struct port *port = netdev_priv(dev);

	unregister_netdev(dev);
	phy_disconnect(port->phydev);
	npe_port_tab[NPE_ID(port->id)] = NULL;
	platform_set_drvdata(pdev, NULL);
	npe_release(port->npe);
	release_resource(port->mem_res);
	free_netdev(dev);
	return 0;
}

static struct platform_driver ixp4xx_eth_driver = {
	.driver.name	= DRV_NAME,
	.probe		= eth_init_one,
	.remove		= eth_remove_one,
};

static int __init eth_init_module(void)
{
	int err;
	if ((err = ixp4xx_mdio_register()))
		return err;
	return platform_driver_register(&ixp4xx_eth_driver);
}

static void __exit eth_cleanup_module(void)
{
	platform_driver_unregister(&ixp4xx_eth_driver);
	ixp4xx_mdio_remove();
}

MODULE_AUTHOR("Krzysztof Halasa");
MODULE_DESCRIPTION("Intel IXP4xx Ethernet driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:ixp4xx_eth");
module_init(eth_init_module);
module_exit(eth_cleanup_module);
