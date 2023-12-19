// SPDX-License-Identifier: GPL-2.0
/* Driver for SGI's IOC3 based Ethernet cards as found in the PCI card.
 *
 * Copyright (C) 1999, 2000, 01, 03, 06 Ralf Baechle
 * Copyright (C) 1995, 1999, 2000, 2001 by Silicon Graphics, Inc.
 *
 * References:
 *  o IOC3 ASIC specification 4.51, 1996-04-18
 *  o IEEE 802.3 specification, 2000 edition
 *  o DP38840A Specification, National Semiconductor, March 1997
 *
 * To do:
 *
 *  o Use prefetching for large packets.  What is a good lower limit for
 *    prefetching?
 *  o Use hardware checksums.
 *  o Which PHYs might possibly be attached to the IOC3 in real live,
 *    which workarounds are required for them?  Do we ever have Lucent's?
 *  o For the 2.5 branch kill the mii-tool ioctls.
 */

#define IOC3_NAME	"ioc3-eth"
#define IOC3_VERSION	"2.6.3-4"

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/crc16.h>
#include <linux/crc32.h>
#include <linux/mii.h>
#include <linux/in.h>
#include <linux/io.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/gfp.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/skbuff.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/nvmem-consumer.h>

#include <net/ip.h>

#include <asm/sn/ioc3.h>
#include <asm/pci/bridge.h>

#define CRC16_INIT	0
#define CRC16_VALID	0xb001

/* Number of RX buffers.  This is tunable in the range of 16 <= x < 512.
 * The value must be a power of two.
 */
#define RX_BUFFS		64
#define RX_RING_ENTRIES		512		/* fixed in hardware */
#define RX_RING_MASK		(RX_RING_ENTRIES - 1)
#define RX_RING_SIZE		(RX_RING_ENTRIES * sizeof(u64))

/* 128 TX buffers (not tunable) */
#define TX_RING_ENTRIES		128
#define TX_RING_MASK		(TX_RING_ENTRIES - 1)
#define TX_RING_SIZE		(TX_RING_ENTRIES * sizeof(struct ioc3_etxd))

/* IOC3 does dma transfers in 128 byte blocks */
#define IOC3_DMA_XFER_LEN	128UL

/* Every RX buffer starts with 8 byte descriptor data */
#define RX_OFFSET		(sizeof(struct ioc3_erxbuf) + NET_IP_ALIGN)
#define RX_BUF_SIZE		(13 * IOC3_DMA_XFER_LEN)

#define ETCSR_FD   ((21 << ETCSR_IPGR2_SHIFT) | (21 << ETCSR_IPGR1_SHIFT) | 21)
#define ETCSR_HD   ((17 << ETCSR_IPGR2_SHIFT) | (11 << ETCSR_IPGR1_SHIFT) | 21)

/* Private per NIC data of the driver.  */
struct ioc3_private {
	struct ioc3_ethregs *regs;
	struct device *dma_dev;
	u32 *ssram;
	unsigned long *rxr;		/* pointer to receiver ring */
	void *tx_ring;
	struct ioc3_etxd *txr;
	dma_addr_t rxr_dma;
	dma_addr_t txr_dma;
	struct sk_buff *rx_skbs[RX_RING_ENTRIES];
	struct sk_buff *tx_skbs[TX_RING_ENTRIES];
	int rx_ci;			/* RX consumer index */
	int rx_pi;			/* RX producer index */
	int tx_ci;			/* TX consumer index */
	int tx_pi;			/* TX producer index */
	int txqlen;
	u32 emcr, ehar_h, ehar_l;
	spinlock_t ioc3_lock;
	struct mii_if_info mii;

	/* Members used by autonegotiation  */
	struct timer_list ioc3_timer;
};

static int ioc3_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
static void ioc3_set_multicast_list(struct net_device *dev);
static netdev_tx_t ioc3_start_xmit(struct sk_buff *skb, struct net_device *dev);
static void ioc3_timeout(struct net_device *dev, unsigned int txqueue);
static inline unsigned int ioc3_hash(const unsigned char *addr);
static void ioc3_start(struct ioc3_private *ip);
static inline void ioc3_stop(struct ioc3_private *ip);
static void ioc3_init(struct net_device *dev);
static int ioc3_alloc_rx_bufs(struct net_device *dev);
static void ioc3_free_rx_bufs(struct ioc3_private *ip);
static inline void ioc3_clean_tx_ring(struct ioc3_private *ip);

static const struct ethtool_ops ioc3_ethtool_ops;

static inline unsigned long aligned_rx_skb_addr(unsigned long addr)
{
	return (~addr + 1) & (IOC3_DMA_XFER_LEN - 1UL);
}

static inline int ioc3_alloc_skb(struct ioc3_private *ip, struct sk_buff **skb,
				 struct ioc3_erxbuf **rxb, dma_addr_t *rxb_dma)
{
	struct sk_buff *new_skb;
	dma_addr_t d;
	int offset;

	new_skb = alloc_skb(RX_BUF_SIZE + IOC3_DMA_XFER_LEN - 1, GFP_ATOMIC);
	if (!new_skb)
		return -ENOMEM;

	/* ensure buffer is aligned to IOC3_DMA_XFER_LEN */
	offset = aligned_rx_skb_addr((unsigned long)new_skb->data);
	if (offset)
		skb_reserve(new_skb, offset);

	d = dma_map_single(ip->dma_dev, new_skb->data,
			   RX_BUF_SIZE, DMA_FROM_DEVICE);

	if (dma_mapping_error(ip->dma_dev, d)) {
		dev_kfree_skb_any(new_skb);
		return -ENOMEM;
	}
	*rxb_dma = d;
	*rxb = (struct ioc3_erxbuf *)new_skb->data;
	skb_reserve(new_skb, RX_OFFSET);
	*skb = new_skb;

	return 0;
}

#ifdef CONFIG_PCI_XTALK_BRIDGE
static inline unsigned long ioc3_map(dma_addr_t addr, unsigned long attr)
{
	return (addr & ~PCI64_ATTR_BAR) | attr;
}

#define ERBAR_VAL	(ERBAR_BARRIER_BIT << ERBAR_RXBARR_SHIFT)
#else
static inline unsigned long ioc3_map(dma_addr_t addr, unsigned long attr)
{
	return addr;
}

#define ERBAR_VAL	0
#endif

static int ioc3eth_nvmem_match(struct device *dev, const void *data)
{
	const char *name = dev_name(dev);
	const char *prefix = data;
	int prefix_len;

	prefix_len = strlen(prefix);
	if (strlen(name) < (prefix_len + 3))
		return 0;

	if (memcmp(prefix, name, prefix_len) != 0)
		return 0;

	/* found nvmem device which is attached to our ioc3
	 * now check for one wire family code 09, 89 and 91
	 */
	if (memcmp(name + prefix_len, "09-", 3) == 0)
		return 1;
	if (memcmp(name + prefix_len, "89-", 3) == 0)
		return 1;
	if (memcmp(name + prefix_len, "91-", 3) == 0)
		return 1;

	return 0;
}

static int ioc3eth_get_mac_addr(struct resource *res, u8 mac_addr[6])
{
	struct nvmem_device *nvmem;
	char prefix[24];
	u8 prom[16];
	int ret;
	int i;

	snprintf(prefix, sizeof(prefix), "ioc3-%012llx-",
		 res->start & ~0xffff);

	nvmem = nvmem_device_find(prefix, ioc3eth_nvmem_match);
	if (IS_ERR(nvmem))
		return PTR_ERR(nvmem);

	ret = nvmem_device_read(nvmem, 0, 16, prom);
	nvmem_device_put(nvmem);
	if (ret < 0)
		return ret;

	/* check, if content is valid */
	if (prom[0] != 0x0a ||
	    crc16(CRC16_INIT, prom, 13) != CRC16_VALID)
		return -EINVAL;

	for (i = 0; i < 6; i++)
		mac_addr[i] = prom[10 - i];

	return 0;
}

static void __ioc3_set_mac_address(struct net_device *dev)
{
	struct ioc3_private *ip = netdev_priv(dev);

	writel((dev->dev_addr[5] <<  8) |
	       dev->dev_addr[4],
	       &ip->regs->emar_h);
	writel((dev->dev_addr[3] << 24) |
	       (dev->dev_addr[2] << 16) |
	       (dev->dev_addr[1] <<  8) |
	       dev->dev_addr[0],
	       &ip->regs->emar_l);
}

static int ioc3_set_mac_address(struct net_device *dev, void *addr)
{
	struct ioc3_private *ip = netdev_priv(dev);
	struct sockaddr *sa = addr;

	eth_hw_addr_set(dev, sa->sa_data);

	spin_lock_irq(&ip->ioc3_lock);
	__ioc3_set_mac_address(dev);
	spin_unlock_irq(&ip->ioc3_lock);

	return 0;
}

/* Caller must hold the ioc3_lock ever for MII readers.  This is also
 * used to protect the transmitter side but it's low contention.
 */
static int ioc3_mdio_read(struct net_device *dev, int phy, int reg)
{
	struct ioc3_private *ip = netdev_priv(dev);
	struct ioc3_ethregs *regs = ip->regs;

	while (readl(&regs->micr) & MICR_BUSY)
		;
	writel((phy << MICR_PHYADDR_SHIFT) | reg | MICR_READTRIG,
	       &regs->micr);
	while (readl(&regs->micr) & MICR_BUSY)
		;

	return readl(&regs->midr_r) & MIDR_DATA_MASK;
}

static void ioc3_mdio_write(struct net_device *dev, int phy, int reg, int data)
{
	struct ioc3_private *ip = netdev_priv(dev);
	struct ioc3_ethregs *regs = ip->regs;

	while (readl(&regs->micr) & MICR_BUSY)
		;
	writel(data, &regs->midr_w);
	writel((phy << MICR_PHYADDR_SHIFT) | reg, &regs->micr);
	while (readl(&regs->micr) & MICR_BUSY)
		;
}

static int ioc3_mii_init(struct ioc3_private *ip);

static struct net_device_stats *ioc3_get_stats(struct net_device *dev)
{
	struct ioc3_private *ip = netdev_priv(dev);
	struct ioc3_ethregs *regs = ip->regs;

	dev->stats.collisions += readl(&regs->etcdc) & ETCDC_COLLCNT_MASK;
	return &dev->stats;
}

static void ioc3_tcpudp_checksum(struct sk_buff *skb, u32 hwsum, int len)
{
	struct ethhdr *eh = eth_hdr(skb);
	unsigned int proto;
	unsigned char *cp;
	struct iphdr *ih;
	u32 csum, ehsum;
	u16 *ew;

	/* Did hardware handle the checksum at all?  The cases we can handle
	 * are:
	 *
	 * - TCP and UDP checksums of IPv4 only.
	 * - IPv6 would be doable but we keep that for later ...
	 * - Only unfragmented packets.  Did somebody already tell you
	 *   fragmentation is evil?
	 * - don't care about packet size.  Worst case when processing a
	 *   malformed packet we'll try to access the packet at ip header +
	 *   64 bytes which is still inside the skb.  Even in the unlikely
	 *   case where the checksum is right the higher layers will still
	 *   drop the packet as appropriate.
	 */
	if (eh->h_proto != htons(ETH_P_IP))
		return;

	ih = (struct iphdr *)((char *)eh + ETH_HLEN);
	if (ip_is_fragment(ih))
		return;

	proto = ih->protocol;
	if (proto != IPPROTO_TCP && proto != IPPROTO_UDP)
		return;

	/* Same as tx - compute csum of pseudo header  */
	csum = hwsum +
	       (ih->tot_len - (ih->ihl << 2)) +
	       htons((u16)ih->protocol) +
	       (ih->saddr >> 16) + (ih->saddr & 0xffff) +
	       (ih->daddr >> 16) + (ih->daddr & 0xffff);

	/* Sum up ethernet dest addr, src addr and protocol  */
	ew = (u16 *)eh;
	ehsum = ew[0] + ew[1] + ew[2] + ew[3] + ew[4] + ew[5] + ew[6];

	ehsum = (ehsum & 0xffff) + (ehsum >> 16);
	ehsum = (ehsum & 0xffff) + (ehsum >> 16);

	csum += 0xffff ^ ehsum;

	/* In the next step we also subtract the 1's complement
	 * checksum of the trailing ethernet CRC.
	 */
	cp = (char *)eh + len;	/* points at trailing CRC */
	if (len & 1) {
		csum += 0xffff ^ (u16)((cp[1] << 8) | cp[0]);
		csum += 0xffff ^ (u16)((cp[3] << 8) | cp[2]);
	} else {
		csum += 0xffff ^ (u16)((cp[0] << 8) | cp[1]);
		csum += 0xffff ^ (u16)((cp[2] << 8) | cp[3]);
	}

	csum = (csum & 0xffff) + (csum >> 16);
	csum = (csum & 0xffff) + (csum >> 16);

	if (csum == 0xffff)
		skb->ip_summed = CHECKSUM_UNNECESSARY;
}

static inline void ioc3_rx(struct net_device *dev)
{
	struct ioc3_private *ip = netdev_priv(dev);
	struct sk_buff *skb, *new_skb;
	int rx_entry, n_entry, len;
	struct ioc3_erxbuf *rxb;
	unsigned long *rxr;
	dma_addr_t d;
	u32 w0, err;

	rxr = ip->rxr;		/* Ring base */
	rx_entry = ip->rx_ci;				/* RX consume index */
	n_entry = ip->rx_pi;

	skb = ip->rx_skbs[rx_entry];
	rxb = (struct ioc3_erxbuf *)(skb->data - RX_OFFSET);
	w0 = be32_to_cpu(rxb->w0);

	while (w0 & ERXBUF_V) {
		err = be32_to_cpu(rxb->err);		/* It's valid ...  */
		if (err & ERXBUF_GOODPKT) {
			len = ((w0 >> ERXBUF_BYTECNT_SHIFT) & 0x7ff) - 4;
			skb_put(skb, len);
			skb->protocol = eth_type_trans(skb, dev);

			if (ioc3_alloc_skb(ip, &new_skb, &rxb, &d)) {
				/* Ouch, drop packet and just recycle packet
				 * to keep the ring filled.
				 */
				dev->stats.rx_dropped++;
				new_skb = skb;
				d = rxr[rx_entry];
				goto next;
			}

			if (likely(dev->features & NETIF_F_RXCSUM))
				ioc3_tcpudp_checksum(skb,
						     w0 & ERXBUF_IPCKSUM_MASK,
						     len);

			dma_unmap_single(ip->dma_dev, rxr[rx_entry],
					 RX_BUF_SIZE, DMA_FROM_DEVICE);

			netif_rx(skb);

			ip->rx_skbs[rx_entry] = NULL;	/* Poison  */

			dev->stats.rx_packets++;		/* Statistics */
			dev->stats.rx_bytes += len;
		} else {
			/* The frame is invalid and the skb never
			 * reached the network layer so we can just
			 * recycle it.
			 */
			new_skb = skb;
			d = rxr[rx_entry];
			dev->stats.rx_errors++;
		}
		if (err & ERXBUF_CRCERR)	/* Statistics */
			dev->stats.rx_crc_errors++;
		if (err & ERXBUF_FRAMERR)
			dev->stats.rx_frame_errors++;

next:
		ip->rx_skbs[n_entry] = new_skb;
		rxr[n_entry] = cpu_to_be64(ioc3_map(d, PCI64_ATTR_BAR));
		rxb->w0 = 0;				/* Clear valid flag */
		n_entry = (n_entry + 1) & RX_RING_MASK;	/* Update erpir */

		/* Now go on to the next ring entry.  */
		rx_entry = (rx_entry + 1) & RX_RING_MASK;
		skb = ip->rx_skbs[rx_entry];
		rxb = (struct ioc3_erxbuf *)(skb->data - RX_OFFSET);
		w0 = be32_to_cpu(rxb->w0);
	}
	writel((n_entry << 3) | ERPIR_ARM, &ip->regs->erpir);
	ip->rx_pi = n_entry;
	ip->rx_ci = rx_entry;
}

static inline void ioc3_tx(struct net_device *dev)
{
	struct ioc3_private *ip = netdev_priv(dev);
	struct ioc3_ethregs *regs = ip->regs;
	unsigned long packets, bytes;
	int tx_entry, o_entry;
	struct sk_buff *skb;
	u32 etcir;

	spin_lock(&ip->ioc3_lock);
	etcir = readl(&regs->etcir);

	tx_entry = (etcir >> 7) & TX_RING_MASK;
	o_entry = ip->tx_ci;
	packets = 0;
	bytes = 0;

	while (o_entry != tx_entry) {
		packets++;
		skb = ip->tx_skbs[o_entry];
		bytes += skb->len;
		dev_consume_skb_irq(skb);
		ip->tx_skbs[o_entry] = NULL;

		o_entry = (o_entry + 1) & TX_RING_MASK;	/* Next */

		etcir = readl(&regs->etcir);		/* More pkts sent?  */
		tx_entry = (etcir >> 7) & TX_RING_MASK;
	}

	dev->stats.tx_packets += packets;
	dev->stats.tx_bytes += bytes;
	ip->txqlen -= packets;

	if (netif_queue_stopped(dev) && ip->txqlen < TX_RING_ENTRIES)
		netif_wake_queue(dev);

	ip->tx_ci = o_entry;
	spin_unlock(&ip->ioc3_lock);
}

/* Deal with fatal IOC3 errors.  This condition might be caused by a hard or
 * software problems, so we should try to recover
 * more gracefully if this ever happens.  In theory we might be flooded
 * with such error interrupts if something really goes wrong, so we might
 * also consider to take the interface down.
 */
static void ioc3_error(struct net_device *dev, u32 eisr)
{
	struct ioc3_private *ip = netdev_priv(dev);

	spin_lock(&ip->ioc3_lock);

	if (eisr & EISR_RXOFLO)
		net_err_ratelimited("%s: RX overflow.\n", dev->name);
	if (eisr & EISR_RXBUFOFLO)
		net_err_ratelimited("%s: RX buffer overflow.\n", dev->name);
	if (eisr & EISR_RXMEMERR)
		net_err_ratelimited("%s: RX PCI error.\n", dev->name);
	if (eisr & EISR_RXPARERR)
		net_err_ratelimited("%s: RX SSRAM parity error.\n", dev->name);
	if (eisr & EISR_TXBUFUFLO)
		net_err_ratelimited("%s: TX buffer underflow.\n", dev->name);
	if (eisr & EISR_TXMEMERR)
		net_err_ratelimited("%s: TX PCI error.\n", dev->name);

	ioc3_stop(ip);
	ioc3_free_rx_bufs(ip);
	ioc3_clean_tx_ring(ip);

	ioc3_init(dev);
	if (ioc3_alloc_rx_bufs(dev)) {
		netdev_err(dev, "%s: rx buffer allocation failed\n", __func__);
		spin_unlock(&ip->ioc3_lock);
		return;
	}
	ioc3_start(ip);
	ioc3_mii_init(ip);

	netif_wake_queue(dev);

	spin_unlock(&ip->ioc3_lock);
}

/* The interrupt handler does all of the Rx thread work and cleans up
 * after the Tx thread.
 */
static irqreturn_t ioc3_interrupt(int irq, void *dev_id)
{
	struct ioc3_private *ip = netdev_priv(dev_id);
	struct ioc3_ethregs *regs = ip->regs;
	u32 eisr;

	eisr = readl(&regs->eisr);
	writel(eisr, &regs->eisr);
	readl(&regs->eisr);				/* Flush */

	if (eisr & (EISR_RXOFLO | EISR_RXBUFOFLO | EISR_RXMEMERR |
		    EISR_RXPARERR | EISR_TXBUFUFLO | EISR_TXMEMERR))
		ioc3_error(dev_id, eisr);
	if (eisr & EISR_RXTIMERINT)
		ioc3_rx(dev_id);
	if (eisr & EISR_TXEXPLICIT)
		ioc3_tx(dev_id);

	return IRQ_HANDLED;
}

static inline void ioc3_setup_duplex(struct ioc3_private *ip)
{
	struct ioc3_ethregs *regs = ip->regs;

	spin_lock_irq(&ip->ioc3_lock);

	if (ip->mii.full_duplex) {
		writel(ETCSR_FD, &regs->etcsr);
		ip->emcr |= EMCR_DUPLEX;
	} else {
		writel(ETCSR_HD, &regs->etcsr);
		ip->emcr &= ~EMCR_DUPLEX;
	}
	writel(ip->emcr, &regs->emcr);

	spin_unlock_irq(&ip->ioc3_lock);
}

static void ioc3_timer(struct timer_list *t)
{
	struct ioc3_private *ip = from_timer(ip, t, ioc3_timer);

	/* Print the link status if it has changed */
	mii_check_media(&ip->mii, 1, 0);
	ioc3_setup_duplex(ip);

	ip->ioc3_timer.expires = jiffies + ((12 * HZ) / 10); /* 1.2s */
	add_timer(&ip->ioc3_timer);
}

/* Try to find a PHY.  There is no apparent relation between the MII addresses
 * in the SGI documentation and what we find in reality, so we simply probe
 * for the PHY.
 */
static int ioc3_mii_init(struct ioc3_private *ip)
{
	u16 word;
	int i;

	for (i = 0; i < 32; i++) {
		word = ioc3_mdio_read(ip->mii.dev, i, MII_PHYSID1);

		if (word != 0xffff && word != 0x0000) {
			ip->mii.phy_id = i;
			return 0;
		}
	}
	ip->mii.phy_id = -1;
	return -ENODEV;
}

static void ioc3_mii_start(struct ioc3_private *ip)
{
	ip->ioc3_timer.expires = jiffies + (12 * HZ) / 10;  /* 1.2 sec. */
	add_timer(&ip->ioc3_timer);
}

static inline void ioc3_tx_unmap(struct ioc3_private *ip, int entry)
{
	struct ioc3_etxd *desc;
	u32 cmd, bufcnt, len;

	desc = &ip->txr[entry];
	cmd = be32_to_cpu(desc->cmd);
	bufcnt = be32_to_cpu(desc->bufcnt);
	if (cmd & ETXD_B1V) {
		len = (bufcnt & ETXD_B1CNT_MASK) >> ETXD_B1CNT_SHIFT;
		dma_unmap_single(ip->dma_dev, be64_to_cpu(desc->p1),
				 len, DMA_TO_DEVICE);
	}
	if (cmd & ETXD_B2V) {
		len = (bufcnt & ETXD_B2CNT_MASK) >> ETXD_B2CNT_SHIFT;
		dma_unmap_single(ip->dma_dev, be64_to_cpu(desc->p2),
				 len, DMA_TO_DEVICE);
	}
}

static inline void ioc3_clean_tx_ring(struct ioc3_private *ip)
{
	struct sk_buff *skb;
	int i;

	for (i = 0; i < TX_RING_ENTRIES; i++) {
		skb = ip->tx_skbs[i];
		if (skb) {
			ioc3_tx_unmap(ip, i);
			ip->tx_skbs[i] = NULL;
			dev_kfree_skb_any(skb);
		}
		ip->txr[i].cmd = 0;
	}
	ip->tx_pi = 0;
	ip->tx_ci = 0;
}

static void ioc3_free_rx_bufs(struct ioc3_private *ip)
{
	int rx_entry, n_entry;
	struct sk_buff *skb;

	n_entry = ip->rx_ci;
	rx_entry = ip->rx_pi;

	while (n_entry != rx_entry) {
		skb = ip->rx_skbs[n_entry];
		if (skb) {
			dma_unmap_single(ip->dma_dev,
					 be64_to_cpu(ip->rxr[n_entry]),
					 RX_BUF_SIZE, DMA_FROM_DEVICE);
			dev_kfree_skb_any(skb);
		}
		n_entry = (n_entry + 1) & RX_RING_MASK;
	}
}

static int ioc3_alloc_rx_bufs(struct net_device *dev)
{
	struct ioc3_private *ip = netdev_priv(dev);
	struct ioc3_erxbuf *rxb;
	dma_addr_t d;
	int i;

	/* Now the rx buffers.  The RX ring may be larger but
	 * we only allocate 16 buffers for now.  Need to tune
	 * this for performance and memory later.
	 */
	for (i = 0; i < RX_BUFFS; i++) {
		if (ioc3_alloc_skb(ip, &ip->rx_skbs[i], &rxb, &d))
			return -ENOMEM;

		rxb->w0 = 0;	/* Clear valid flag */
		ip->rxr[i] = cpu_to_be64(ioc3_map(d, PCI64_ATTR_BAR));
	}
	ip->rx_ci = 0;
	ip->rx_pi = RX_BUFFS;

	return 0;
}

static inline void ioc3_ssram_disc(struct ioc3_private *ip)
{
	struct ioc3_ethregs *regs = ip->regs;
	u32 *ssram0 = &ip->ssram[0x0000];
	u32 *ssram1 = &ip->ssram[0x4000];
	u32 pattern = 0x5555;

	/* Assume the larger size SSRAM and enable parity checking */
	writel(readl(&regs->emcr) | (EMCR_BUFSIZ | EMCR_RAMPAR), &regs->emcr);
	readl(&regs->emcr); /* Flush */

	writel(pattern, ssram0);
	writel(~pattern & IOC3_SSRAM_DM, ssram1);

	if ((readl(ssram0) & IOC3_SSRAM_DM) != pattern ||
	    (readl(ssram1) & IOC3_SSRAM_DM) != (~pattern & IOC3_SSRAM_DM)) {
		/* set ssram size to 64 KB */
		ip->emcr |= EMCR_RAMPAR;
		writel(readl(&regs->emcr) & ~EMCR_BUFSIZ, &regs->emcr);
	} else {
		ip->emcr |= EMCR_BUFSIZ | EMCR_RAMPAR;
	}
}

static void ioc3_init(struct net_device *dev)
{
	struct ioc3_private *ip = netdev_priv(dev);
	struct ioc3_ethregs *regs = ip->regs;

	del_timer_sync(&ip->ioc3_timer);	/* Kill if running	*/

	writel(EMCR_RST, &regs->emcr);		/* Reset		*/
	readl(&regs->emcr);			/* Flush WB		*/
	udelay(4);				/* Give it time ...	*/
	writel(0, &regs->emcr);
	readl(&regs->emcr);

	/* Misc registers  */
	writel(ERBAR_VAL, &regs->erbar);
	readl(&regs->etcdc);			/* Clear on read */
	writel(15, &regs->ercsr);		/* RX low watermark  */
	writel(0, &regs->ertr);			/* Interrupt immediately */
	__ioc3_set_mac_address(dev);
	writel(ip->ehar_h, &regs->ehar_h);
	writel(ip->ehar_l, &regs->ehar_l);
	writel(42, &regs->ersr);		/* XXX should be random */
}

static void ioc3_start(struct ioc3_private *ip)
{
	struct ioc3_ethregs *regs = ip->regs;
	unsigned long ring;

	/* Now the rx ring base, consume & produce registers.  */
	ring = ioc3_map(ip->rxr_dma, PCI64_ATTR_PREC);
	writel(ring >> 32, &regs->erbr_h);
	writel(ring & 0xffffffff, &regs->erbr_l);
	writel(ip->rx_ci << 3, &regs->ercir);
	writel((ip->rx_pi << 3) | ERPIR_ARM, &regs->erpir);

	ring = ioc3_map(ip->txr_dma, PCI64_ATTR_PREC);

	ip->txqlen = 0;					/* nothing queued  */

	/* Now the tx ring base, consume & produce registers.  */
	writel(ring >> 32, &regs->etbr_h);
	writel(ring & 0xffffffff, &regs->etbr_l);
	writel(ip->tx_pi << 7, &regs->etpir);
	writel(ip->tx_ci << 7, &regs->etcir);
	readl(&regs->etcir);				/* Flush */

	ip->emcr |= ((RX_OFFSET / 2) << EMCR_RXOFF_SHIFT) | EMCR_TXDMAEN |
		    EMCR_TXEN | EMCR_RXDMAEN | EMCR_RXEN | EMCR_PADEN;
	writel(ip->emcr, &regs->emcr);
	writel(EISR_RXTIMERINT | EISR_RXOFLO | EISR_RXBUFOFLO |
	       EISR_RXMEMERR | EISR_RXPARERR | EISR_TXBUFUFLO |
	       EISR_TXEXPLICIT | EISR_TXMEMERR, &regs->eier);
	readl(&regs->eier);
}

static inline void ioc3_stop(struct ioc3_private *ip)
{
	struct ioc3_ethregs *regs = ip->regs;

	writel(0, &regs->emcr);			/* Shutup */
	writel(0, &regs->eier);			/* Disable interrupts */
	readl(&regs->eier);			/* Flush */
}

static int ioc3_open(struct net_device *dev)
{
	struct ioc3_private *ip = netdev_priv(dev);

	ip->ehar_h = 0;
	ip->ehar_l = 0;

	ioc3_init(dev);
	if (ioc3_alloc_rx_bufs(dev)) {
		netdev_err(dev, "%s: rx buffer allocation failed\n", __func__);
		return -ENOMEM;
	}
	ioc3_start(ip);
	ioc3_mii_start(ip);

	netif_start_queue(dev);
	return 0;
}

static int ioc3_close(struct net_device *dev)
{
	struct ioc3_private *ip = netdev_priv(dev);

	del_timer_sync(&ip->ioc3_timer);

	netif_stop_queue(dev);

	ioc3_stop(ip);

	ioc3_free_rx_bufs(ip);
	ioc3_clean_tx_ring(ip);

	return 0;
}

static const struct net_device_ops ioc3_netdev_ops = {
	.ndo_open		= ioc3_open,
	.ndo_stop		= ioc3_close,
	.ndo_start_xmit		= ioc3_start_xmit,
	.ndo_tx_timeout		= ioc3_timeout,
	.ndo_get_stats		= ioc3_get_stats,
	.ndo_set_rx_mode	= ioc3_set_multicast_list,
	.ndo_eth_ioctl		= ioc3_ioctl,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address	= ioc3_set_mac_address,
};

static int ioc3eth_probe(struct platform_device *pdev)
{
	u32 sw_physid1, sw_physid2, vendor, model, rev;
	struct ioc3_private *ip;
	struct net_device *dev;
	struct resource *regs;
	u8 mac_addr[6];
	int err;

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!regs) {
		dev_err(&pdev->dev, "Invalid resource\n");
		return -EINVAL;
	}
	/* get mac addr from one wire prom */
	if (ioc3eth_get_mac_addr(regs, mac_addr))
		return -EPROBE_DEFER; /* not available yet */

	dev = alloc_etherdev(sizeof(struct ioc3_private));
	if (!dev)
		return -ENOMEM;

	SET_NETDEV_DEV(dev, &pdev->dev);

	ip = netdev_priv(dev);
	ip->dma_dev = pdev->dev.parent;
	ip->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(ip->regs)) {
		err = PTR_ERR(ip->regs);
		goto out_free;
	}

	ip->ssram = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(ip->ssram)) {
		err = PTR_ERR(ip->ssram);
		goto out_free;
	}

	dev->irq = platform_get_irq(pdev, 0);
	if (dev->irq < 0) {
		err = dev->irq;
		goto out_free;
	}

	if (devm_request_irq(&pdev->dev, dev->irq, ioc3_interrupt,
			     IRQF_SHARED, "ioc3-eth", dev)) {
		dev_err(&pdev->dev, "Can't get irq %d\n", dev->irq);
		err = -ENODEV;
		goto out_free;
	}

	spin_lock_init(&ip->ioc3_lock);
	timer_setup(&ip->ioc3_timer, ioc3_timer, 0);

	ioc3_stop(ip);

	/* Allocate rx ring.  4kb = 512 entries, must be 4kb aligned */
	ip->rxr = dma_alloc_coherent(ip->dma_dev, RX_RING_SIZE, &ip->rxr_dma,
				     GFP_KERNEL);
	if (!ip->rxr) {
		pr_err("ioc3-eth: rx ring allocation failed\n");
		err = -ENOMEM;
		goto out_stop;
	}

	/* Allocate tx rings.  16kb = 128 bufs, must be 16kb aligned  */
	ip->tx_ring = dma_alloc_coherent(ip->dma_dev, TX_RING_SIZE + SZ_16K - 1,
					 &ip->txr_dma, GFP_KERNEL);
	if (!ip->tx_ring) {
		pr_err("ioc3-eth: tx ring allocation failed\n");
		err = -ENOMEM;
		goto out_stop;
	}
	/* Align TX ring */
	ip->txr = PTR_ALIGN(ip->tx_ring, SZ_16K);
	ip->txr_dma = ALIGN(ip->txr_dma, SZ_16K);

	ioc3_init(dev);

	ip->mii.phy_id_mask = 0x1f;
	ip->mii.reg_num_mask = 0x1f;
	ip->mii.dev = dev;
	ip->mii.mdio_read = ioc3_mdio_read;
	ip->mii.mdio_write = ioc3_mdio_write;

	ioc3_mii_init(ip);

	if (ip->mii.phy_id == -1) {
		netdev_err(dev, "Didn't find a PHY, goodbye.\n");
		err = -ENODEV;
		goto out_stop;
	}

	ioc3_mii_start(ip);
	ioc3_ssram_disc(ip);
	eth_hw_addr_set(dev, mac_addr);

	/* The IOC3-specific entries in the device structure. */
	dev->watchdog_timeo	= 5 * HZ;
	dev->netdev_ops		= &ioc3_netdev_ops;
	dev->ethtool_ops	= &ioc3_ethtool_ops;
	dev->hw_features	= NETIF_F_IP_CSUM | NETIF_F_RXCSUM;
	dev->features		= NETIF_F_IP_CSUM | NETIF_F_HIGHDMA;

	sw_physid1 = ioc3_mdio_read(dev, ip->mii.phy_id, MII_PHYSID1);
	sw_physid2 = ioc3_mdio_read(dev, ip->mii.phy_id, MII_PHYSID2);

	err = register_netdev(dev);
	if (err)
		goto out_stop;

	mii_check_media(&ip->mii, 1, 1);
	ioc3_setup_duplex(ip);

	vendor = (sw_physid1 << 12) | (sw_physid2 >> 4);
	model  = (sw_physid2 >> 4) & 0x3f;
	rev    = sw_physid2 & 0xf;
	netdev_info(dev, "Using PHY %d, vendor 0x%x, model %d, rev %d.\n",
		    ip->mii.phy_id, vendor, model, rev);
	netdev_info(dev, "IOC3 SSRAM has %d kbyte.\n",
		    ip->emcr & EMCR_BUFSIZ ? 128 : 64);

	return 0;

out_stop:
	del_timer_sync(&ip->ioc3_timer);
	if (ip->rxr)
		dma_free_coherent(ip->dma_dev, RX_RING_SIZE, ip->rxr,
				  ip->rxr_dma);
	if (ip->tx_ring)
		dma_free_coherent(ip->dma_dev, TX_RING_SIZE + SZ_16K - 1, ip->tx_ring,
				  ip->txr_dma);
out_free:
	free_netdev(dev);
	return err;
}

static void ioc3eth_remove(struct platform_device *pdev)
{
	struct net_device *dev = platform_get_drvdata(pdev);
	struct ioc3_private *ip = netdev_priv(dev);

	dma_free_coherent(ip->dma_dev, RX_RING_SIZE, ip->rxr, ip->rxr_dma);
	dma_free_coherent(ip->dma_dev, TX_RING_SIZE + SZ_16K - 1, ip->tx_ring, ip->txr_dma);

	unregister_netdev(dev);
	del_timer_sync(&ip->ioc3_timer);
	free_netdev(dev);
}


static netdev_tx_t ioc3_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct ioc3_private *ip = netdev_priv(dev);
	struct ioc3_etxd *desc;
	unsigned long data;
	unsigned int len;
	int produce;
	u32 w0 = 0;

	/* IOC3 has a fairly simple minded checksumming hardware which simply
	 * adds up the 1's complement checksum for the entire packet and
	 * inserts it at an offset which can be specified in the descriptor
	 * into the transmit packet.  This means we have to compensate for the
	 * MAC header which should not be summed and the TCP/UDP pseudo headers
	 * manually.
	 */
	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		const struct iphdr *ih = ip_hdr(skb);
		const int proto = ntohs(ih->protocol);
		unsigned int csoff;
		u32 csum, ehsum;
		u16 *eh;

		/* The MAC header.  skb->mac seem the logic approach
		 * to find the MAC header - except it's a NULL pointer ...
		 */
		eh = (u16 *)skb->data;

		/* Sum up dest addr, src addr and protocol  */
		ehsum = eh[0] + eh[1] + eh[2] + eh[3] + eh[4] + eh[5] + eh[6];

		/* Skip IP header; it's sum is always zero and was
		 * already filled in by ip_output.c
		 */
		csum = csum_tcpudp_nofold(ih->saddr, ih->daddr,
					  ih->tot_len - (ih->ihl << 2),
					  proto, csum_fold(ehsum));

		csum = (csum & 0xffff) + (csum >> 16);	/* Fold again */
		csum = (csum & 0xffff) + (csum >> 16);

		csoff = ETH_HLEN + (ih->ihl << 2);
		if (proto == IPPROTO_UDP) {
			csoff += offsetof(struct udphdr, check);
			udp_hdr(skb)->check = csum;
		}
		if (proto == IPPROTO_TCP) {
			csoff += offsetof(struct tcphdr, check);
			tcp_hdr(skb)->check = csum;
		}

		w0 = ETXD_DOCHECKSUM | (csoff << ETXD_CHKOFF_SHIFT);
	}

	spin_lock_irq(&ip->ioc3_lock);

	data = (unsigned long)skb->data;
	len = skb->len;

	produce = ip->tx_pi;
	desc = &ip->txr[produce];

	if (len <= 104) {
		/* Short packet, let's copy it directly into the ring.  */
		skb_copy_from_linear_data(skb, desc->data, skb->len);
		if (len < ETH_ZLEN) {
			/* Very short packet, pad with zeros at the end. */
			memset(desc->data + len, 0, ETH_ZLEN - len);
			len = ETH_ZLEN;
		}
		desc->cmd = cpu_to_be32(len | ETXD_INTWHENDONE | ETXD_D0V | w0);
		desc->bufcnt = cpu_to_be32(len);
	} else if ((data ^ (data + len - 1)) & 0x4000) {
		unsigned long b2 = (data | 0x3fffUL) + 1UL;
		unsigned long s1 = b2 - data;
		unsigned long s2 = data + len - b2;
		dma_addr_t d1, d2;

		desc->cmd    = cpu_to_be32(len | ETXD_INTWHENDONE |
					   ETXD_B1V | ETXD_B2V | w0);
		desc->bufcnt = cpu_to_be32((s1 << ETXD_B1CNT_SHIFT) |
					   (s2 << ETXD_B2CNT_SHIFT));
		d1 = dma_map_single(ip->dma_dev, skb->data, s1, DMA_TO_DEVICE);
		if (dma_mapping_error(ip->dma_dev, d1))
			goto drop_packet;
		d2 = dma_map_single(ip->dma_dev, (void *)b2, s1, DMA_TO_DEVICE);
		if (dma_mapping_error(ip->dma_dev, d2)) {
			dma_unmap_single(ip->dma_dev, d1, len, DMA_TO_DEVICE);
			goto drop_packet;
		}
		desc->p1     = cpu_to_be64(ioc3_map(d1, PCI64_ATTR_PREF));
		desc->p2     = cpu_to_be64(ioc3_map(d2, PCI64_ATTR_PREF));
	} else {
		dma_addr_t d;

		/* Normal sized packet that doesn't cross a page boundary. */
		desc->cmd = cpu_to_be32(len | ETXD_INTWHENDONE | ETXD_B1V | w0);
		desc->bufcnt = cpu_to_be32(len << ETXD_B1CNT_SHIFT);
		d = dma_map_single(ip->dma_dev, skb->data, len, DMA_TO_DEVICE);
		if (dma_mapping_error(ip->dma_dev, d))
			goto drop_packet;
		desc->p1     = cpu_to_be64(ioc3_map(d, PCI64_ATTR_PREF));
	}

	mb(); /* make sure all descriptor changes are visible */

	ip->tx_skbs[produce] = skb;			/* Remember skb */
	produce = (produce + 1) & TX_RING_MASK;
	ip->tx_pi = produce;
	writel(produce << 7, &ip->regs->etpir);		/* Fire ... */

	ip->txqlen++;

	if (ip->txqlen >= (TX_RING_ENTRIES - 1))
		netif_stop_queue(dev);

	spin_unlock_irq(&ip->ioc3_lock);

	return NETDEV_TX_OK;

drop_packet:
	dev_kfree_skb_any(skb);
	dev->stats.tx_dropped++;

	spin_unlock_irq(&ip->ioc3_lock);

	return NETDEV_TX_OK;
}

static void ioc3_timeout(struct net_device *dev, unsigned int txqueue)
{
	struct ioc3_private *ip = netdev_priv(dev);

	netdev_err(dev, "transmit timed out, resetting\n");

	spin_lock_irq(&ip->ioc3_lock);

	ioc3_stop(ip);
	ioc3_free_rx_bufs(ip);
	ioc3_clean_tx_ring(ip);

	ioc3_init(dev);
	if (ioc3_alloc_rx_bufs(dev)) {
		netdev_err(dev, "%s: rx buffer allocation failed\n", __func__);
		spin_unlock_irq(&ip->ioc3_lock);
		return;
	}
	ioc3_start(ip);
	ioc3_mii_init(ip);
	ioc3_mii_start(ip);

	spin_unlock_irq(&ip->ioc3_lock);

	netif_wake_queue(dev);
}

/* Given a multicast ethernet address, this routine calculates the
 * address's bit index in the logical address filter mask
 */
static inline unsigned int ioc3_hash(const unsigned char *addr)
{
	unsigned int temp = 0;
	int bits;
	u32 crc;

	crc = ether_crc_le(ETH_ALEN, addr);

	crc &= 0x3f;    /* bit reverse lowest 6 bits for hash index */
	for (bits = 6; --bits >= 0; ) {
		temp <<= 1;
		temp |= (crc & 0x1);
		crc >>= 1;
	}

	return temp;
}

static void ioc3_get_drvinfo(struct net_device *dev,
			     struct ethtool_drvinfo *info)
{
	strscpy(info->driver, IOC3_NAME, sizeof(info->driver));
	strscpy(info->version, IOC3_VERSION, sizeof(info->version));
	strscpy(info->bus_info, pci_name(to_pci_dev(dev->dev.parent)),
		sizeof(info->bus_info));
}

static int ioc3_get_link_ksettings(struct net_device *dev,
				   struct ethtool_link_ksettings *cmd)
{
	struct ioc3_private *ip = netdev_priv(dev);

	spin_lock_irq(&ip->ioc3_lock);
	mii_ethtool_get_link_ksettings(&ip->mii, cmd);
	spin_unlock_irq(&ip->ioc3_lock);

	return 0;
}

static int ioc3_set_link_ksettings(struct net_device *dev,
				   const struct ethtool_link_ksettings *cmd)
{
	struct ioc3_private *ip = netdev_priv(dev);
	int rc;

	spin_lock_irq(&ip->ioc3_lock);
	rc = mii_ethtool_set_link_ksettings(&ip->mii, cmd);
	spin_unlock_irq(&ip->ioc3_lock);

	return rc;
}

static int ioc3_nway_reset(struct net_device *dev)
{
	struct ioc3_private *ip = netdev_priv(dev);
	int rc;

	spin_lock_irq(&ip->ioc3_lock);
	rc = mii_nway_restart(&ip->mii);
	spin_unlock_irq(&ip->ioc3_lock);

	return rc;
}

static u32 ioc3_get_link(struct net_device *dev)
{
	struct ioc3_private *ip = netdev_priv(dev);
	int rc;

	spin_lock_irq(&ip->ioc3_lock);
	rc = mii_link_ok(&ip->mii);
	spin_unlock_irq(&ip->ioc3_lock);

	return rc;
}

static const struct ethtool_ops ioc3_ethtool_ops = {
	.get_drvinfo		= ioc3_get_drvinfo,
	.nway_reset		= ioc3_nway_reset,
	.get_link		= ioc3_get_link,
	.get_link_ksettings	= ioc3_get_link_ksettings,
	.set_link_ksettings	= ioc3_set_link_ksettings,
};

static int ioc3_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct ioc3_private *ip = netdev_priv(dev);
	int rc;

	spin_lock_irq(&ip->ioc3_lock);
	rc = generic_mii_ioctl(&ip->mii, if_mii(rq), cmd, NULL);
	spin_unlock_irq(&ip->ioc3_lock);

	return rc;
}

static void ioc3_set_multicast_list(struct net_device *dev)
{
	struct ioc3_private *ip = netdev_priv(dev);
	struct ioc3_ethregs *regs = ip->regs;
	struct netdev_hw_addr *ha;
	u64 ehar = 0;

	spin_lock_irq(&ip->ioc3_lock);

	if (dev->flags & IFF_PROMISC) {			/* Set promiscuous.  */
		ip->emcr |= EMCR_PROMISC;
		writel(ip->emcr, &regs->emcr);
		readl(&regs->emcr);
	} else {
		ip->emcr &= ~EMCR_PROMISC;
		writel(ip->emcr, &regs->emcr);		/* Clear promiscuous. */
		readl(&regs->emcr);

		if ((dev->flags & IFF_ALLMULTI) ||
		    (netdev_mc_count(dev) > 64)) {
			/* Too many for hashing to make sense or we want all
			 * multicast packets anyway,  so skip computing all the
			 * hashes and just accept all packets.
			 */
			ip->ehar_h = 0xffffffff;
			ip->ehar_l = 0xffffffff;
		} else {
			netdev_for_each_mc_addr(ha, dev) {
				ehar |= (1UL << ioc3_hash(ha->addr));
			}
			ip->ehar_h = ehar >> 32;
			ip->ehar_l = ehar & 0xffffffff;
		}
		writel(ip->ehar_h, &regs->ehar_h);
		writel(ip->ehar_l, &regs->ehar_l);
	}

	spin_unlock_irq(&ip->ioc3_lock);
}

static struct platform_driver ioc3eth_driver = {
	.probe  = ioc3eth_probe,
	.remove_new = ioc3eth_remove,
	.driver = {
		.name = "ioc3-eth",
	}
};

module_platform_driver(ioc3eth_driver);

MODULE_AUTHOR("Ralf Baechle <ralf@linux-mips.org>");
MODULE_DESCRIPTION("SGI IOC3 Ethernet driver");
MODULE_LICENSE("GPL");
