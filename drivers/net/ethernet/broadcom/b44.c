/* b44.c: Broadcom 44xx/47xx Fast Ethernet device driver.
 *
 * Copyright (C) 2002 David S. Miller (davem@redhat.com)
 * Copyright (C) 2004 Pekka Pietikainen (pp@ee.oulu.fi)
 * Copyright (C) 2004 Florian Schirmer (jolt@tuxbox.org)
 * Copyright (C) 2006 Felix Fietkau (nbd@openwrt.org)
 * Copyright (C) 2006 Broadcom Corporation.
 * Copyright (C) 2007 Michael Buesch <m@bues.ch>
 * Copyright (C) 2013 Hauke Mehrtens <hauke@hauke-m.de>
 *
 * Distribute under GPL.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/netdevice.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <linux/etherdevice.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/ssb/ssb.h>
#include <linux/slab.h>
#include <linux/phy.h>

#include <linux/uaccess.h>
#include <asm/io.h>
#include <asm/irq.h>


#include "b44.h"

#define DRV_MODULE_NAME		"b44"
#define DRV_MODULE_VERSION	"2.0"
#define DRV_DESCRIPTION		"Broadcom 44xx/47xx 10/100 PCI ethernet driver"

#define B44_DEF_MSG_ENABLE	  \
	(NETIF_MSG_DRV		| \
	 NETIF_MSG_PROBE	| \
	 NETIF_MSG_LINK		| \
	 NETIF_MSG_TIMER	| \
	 NETIF_MSG_IFDOWN	| \
	 NETIF_MSG_IFUP		| \
	 NETIF_MSG_RX_ERR	| \
	 NETIF_MSG_TX_ERR)

/* length of time before we decide the hardware is borked,
 * and dev->tx_timeout() should be called to fix the problem
 */
#define B44_TX_TIMEOUT			(5 * HZ)

/* hardware minimum and maximum for a single frame's data payload */
#define B44_MIN_MTU			ETH_ZLEN
#define B44_MAX_MTU			ETH_DATA_LEN

#define B44_RX_RING_SIZE		512
#define B44_DEF_RX_RING_PENDING		200
#define B44_RX_RING_BYTES	(sizeof(struct dma_desc) * \
				 B44_RX_RING_SIZE)
#define B44_TX_RING_SIZE		512
#define B44_DEF_TX_RING_PENDING		(B44_TX_RING_SIZE - 1)
#define B44_TX_RING_BYTES	(sizeof(struct dma_desc) * \
				 B44_TX_RING_SIZE)

#define TX_RING_GAP(BP)	\
	(B44_TX_RING_SIZE - (BP)->tx_pending)
#define TX_BUFFS_AVAIL(BP)						\
	(((BP)->tx_cons <= (BP)->tx_prod) ?				\
	  (BP)->tx_cons + (BP)->tx_pending - (BP)->tx_prod :		\
	  (BP)->tx_cons - (BP)->tx_prod - TX_RING_GAP(BP))
#define NEXT_TX(N)		(((N) + 1) & (B44_TX_RING_SIZE - 1))

#define RX_PKT_OFFSET		(RX_HEADER_LEN + 2)
#define RX_PKT_BUF_SZ		(1536 + RX_PKT_OFFSET)

/* minimum number of free TX descriptors required to wake up TX process */
#define B44_TX_WAKEUP_THRESH		(B44_TX_RING_SIZE / 4)

/* b44 internal pattern match filter info */
#define B44_PATTERN_BASE	0x400
#define B44_PATTERN_SIZE	0x80
#define B44_PMASK_BASE		0x600
#define B44_PMASK_SIZE		0x10
#define B44_MAX_PATTERNS	16
#define B44_ETHIPV6UDP_HLEN	62
#define B44_ETHIPV4UDP_HLEN	42

MODULE_AUTHOR("Felix Fietkau, Florian Schirmer, Pekka Pietikainen, David S. Miller");
MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_MODULE_VERSION);

static int b44_debug = -1;	/* -1 == use B44_DEF_MSG_ENABLE as value */
module_param(b44_debug, int, 0);
MODULE_PARM_DESC(b44_debug, "B44 bitmapped debugging message enable value");


#ifdef CONFIG_B44_PCI
static const struct pci_device_id b44_pci_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_BCM4401) },
	{ PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_BCM4401B0) },
	{ PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_BCM4401B1) },
	{ 0 } /* terminate list with empty entry */
};
MODULE_DEVICE_TABLE(pci, b44_pci_tbl);

static struct pci_driver b44_pci_driver = {
	.name		= DRV_MODULE_NAME,
	.id_table	= b44_pci_tbl,
};
#endif /* CONFIG_B44_PCI */

static const struct ssb_device_id b44_ssb_tbl[] = {
	SSB_DEVICE(SSB_VENDOR_BROADCOM, SSB_DEV_ETHERNET, SSB_ANY_REV),
	{},
};
MODULE_DEVICE_TABLE(ssb, b44_ssb_tbl);

static void b44_halt(struct b44 *);
static void b44_init_rings(struct b44 *);

#define B44_FULL_RESET		1
#define B44_FULL_RESET_SKIP_PHY	2
#define B44_PARTIAL_RESET	3
#define B44_CHIP_RESET_FULL	4
#define B44_CHIP_RESET_PARTIAL	5

static void b44_init_hw(struct b44 *, int);

static int dma_desc_sync_size;
static int instance;

static const char b44_gstrings[][ETH_GSTRING_LEN] = {
#define _B44(x...)	# x,
B44_STAT_REG_DECLARE
#undef _B44
};

static inline void b44_sync_dma_desc_for_device(struct ssb_device *sdev,
						dma_addr_t dma_base,
						unsigned long offset,
						enum dma_data_direction dir)
{
	dma_sync_single_for_device(sdev->dma_dev, dma_base + offset,
				   dma_desc_sync_size, dir);
}

static inline void b44_sync_dma_desc_for_cpu(struct ssb_device *sdev,
					     dma_addr_t dma_base,
					     unsigned long offset,
					     enum dma_data_direction dir)
{
	dma_sync_single_for_cpu(sdev->dma_dev, dma_base + offset,
				dma_desc_sync_size, dir);
}

static inline unsigned long br32(const struct b44 *bp, unsigned long reg)
{
	return ssb_read32(bp->sdev, reg);
}

static inline void bw32(const struct b44 *bp,
			unsigned long reg, unsigned long val)
{
	ssb_write32(bp->sdev, reg, val);
}

static int b44_wait_bit(struct b44 *bp, unsigned long reg,
			u32 bit, unsigned long timeout, const int clear)
{
	unsigned long i;

	for (i = 0; i < timeout; i++) {
		u32 val = br32(bp, reg);

		if (clear && !(val & bit))
			break;
		if (!clear && (val & bit))
			break;
		udelay(10);
	}
	if (i == timeout) {
		if (net_ratelimit())
			netdev_err(bp->dev, "BUG!  Timeout waiting for bit %08x of register %lx to %s\n",
				   bit, reg, clear ? "clear" : "set");

		return -ENODEV;
	}
	return 0;
}

static inline void __b44_cam_read(struct b44 *bp, unsigned char *data, int index)
{
	u32 val;

	bw32(bp, B44_CAM_CTRL, (CAM_CTRL_READ |
			    (index << CAM_CTRL_INDEX_SHIFT)));

	b44_wait_bit(bp, B44_CAM_CTRL, CAM_CTRL_BUSY, 100, 1);

	val = br32(bp, B44_CAM_DATA_LO);

	data[2] = (val >> 24) & 0xFF;
	data[3] = (val >> 16) & 0xFF;
	data[4] = (val >> 8) & 0xFF;
	data[5] = (val >> 0) & 0xFF;

	val = br32(bp, B44_CAM_DATA_HI);

	data[0] = (val >> 8) & 0xFF;
	data[1] = (val >> 0) & 0xFF;
}

static inline void __b44_cam_write(struct b44 *bp, unsigned char *data, int index)
{
	u32 val;

	val  = ((u32) data[2]) << 24;
	val |= ((u32) data[3]) << 16;
	val |= ((u32) data[4]) <<  8;
	val |= ((u32) data[5]) <<  0;
	bw32(bp, B44_CAM_DATA_LO, val);
	val = (CAM_DATA_HI_VALID |
	       (((u32) data[0]) << 8) |
	       (((u32) data[1]) << 0));
	bw32(bp, B44_CAM_DATA_HI, val);
	bw32(bp, B44_CAM_CTRL, (CAM_CTRL_WRITE |
			    (index << CAM_CTRL_INDEX_SHIFT)));
	b44_wait_bit(bp, B44_CAM_CTRL, CAM_CTRL_BUSY, 100, 1);
}

static inline void __b44_disable_ints(struct b44 *bp)
{
	bw32(bp, B44_IMASK, 0);
}

static void b44_disable_ints(struct b44 *bp)
{
	__b44_disable_ints(bp);

	/* Flush posted writes. */
	br32(bp, B44_IMASK);
}

static void b44_enable_ints(struct b44 *bp)
{
	bw32(bp, B44_IMASK, bp->imask);
}

static int __b44_readphy(struct b44 *bp, int phy_addr, int reg, u32 *val)
{
	int err;

	bw32(bp, B44_EMAC_ISTAT, EMAC_INT_MII);
	bw32(bp, B44_MDIO_DATA, (MDIO_DATA_SB_START |
			     (MDIO_OP_READ << MDIO_DATA_OP_SHIFT) |
			     (phy_addr << MDIO_DATA_PMD_SHIFT) |
			     (reg << MDIO_DATA_RA_SHIFT) |
			     (MDIO_TA_VALID << MDIO_DATA_TA_SHIFT)));
	err = b44_wait_bit(bp, B44_EMAC_ISTAT, EMAC_INT_MII, 100, 0);
	*val = br32(bp, B44_MDIO_DATA) & MDIO_DATA_DATA;

	return err;
}

static int __b44_writephy(struct b44 *bp, int phy_addr, int reg, u32 val)
{
	bw32(bp, B44_EMAC_ISTAT, EMAC_INT_MII);
	bw32(bp, B44_MDIO_DATA, (MDIO_DATA_SB_START |
			     (MDIO_OP_WRITE << MDIO_DATA_OP_SHIFT) |
			     (phy_addr << MDIO_DATA_PMD_SHIFT) |
			     (reg << MDIO_DATA_RA_SHIFT) |
			     (MDIO_TA_VALID << MDIO_DATA_TA_SHIFT) |
			     (val & MDIO_DATA_DATA)));
	return b44_wait_bit(bp, B44_EMAC_ISTAT, EMAC_INT_MII, 100, 0);
}

static inline int b44_readphy(struct b44 *bp, int reg, u32 *val)
{
	if (bp->flags & B44_FLAG_EXTERNAL_PHY)
		return 0;

	return __b44_readphy(bp, bp->phy_addr, reg, val);
}

static inline int b44_writephy(struct b44 *bp, int reg, u32 val)
{
	if (bp->flags & B44_FLAG_EXTERNAL_PHY)
		return 0;

	return __b44_writephy(bp, bp->phy_addr, reg, val);
}

/* miilib interface */
static int b44_mdio_read_mii(struct net_device *dev, int phy_id, int location)
{
	u32 val;
	struct b44 *bp = netdev_priv(dev);
	int rc = __b44_readphy(bp, phy_id, location, &val);
	if (rc)
		return 0xffffffff;
	return val;
}

static void b44_mdio_write_mii(struct net_device *dev, int phy_id, int location,
			       int val)
{
	struct b44 *bp = netdev_priv(dev);
	__b44_writephy(bp, phy_id, location, val);
}

static int b44_mdio_read_phylib(struct mii_bus *bus, int phy_id, int location)
{
	u32 val;
	struct b44 *bp = bus->priv;
	int rc = __b44_readphy(bp, phy_id, location, &val);
	if (rc)
		return 0xffffffff;
	return val;
}

static int b44_mdio_write_phylib(struct mii_bus *bus, int phy_id, int location,
				 u16 val)
{
	struct b44 *bp = bus->priv;
	return __b44_writephy(bp, phy_id, location, val);
}

static int b44_phy_reset(struct b44 *bp)
{
	u32 val;
	int err;

	if (bp->flags & B44_FLAG_EXTERNAL_PHY)
		return 0;
	err = b44_writephy(bp, MII_BMCR, BMCR_RESET);
	if (err)
		return err;
	udelay(100);
	err = b44_readphy(bp, MII_BMCR, &val);
	if (!err) {
		if (val & BMCR_RESET) {
			netdev_err(bp->dev, "PHY Reset would not complete\n");
			err = -ENODEV;
		}
	}

	return err;
}

static void __b44_set_flow_ctrl(struct b44 *bp, u32 pause_flags)
{
	u32 val;

	bp->flags &= ~(B44_FLAG_TX_PAUSE | B44_FLAG_RX_PAUSE);
	bp->flags |= pause_flags;

	val = br32(bp, B44_RXCONFIG);
	if (pause_flags & B44_FLAG_RX_PAUSE)
		val |= RXCONFIG_FLOW;
	else
		val &= ~RXCONFIG_FLOW;
	bw32(bp, B44_RXCONFIG, val);

	val = br32(bp, B44_MAC_FLOW);
	if (pause_flags & B44_FLAG_TX_PAUSE)
		val |= (MAC_FLOW_PAUSE_ENAB |
			(0xc0 & MAC_FLOW_RX_HI_WATER));
	else
		val &= ~MAC_FLOW_PAUSE_ENAB;
	bw32(bp, B44_MAC_FLOW, val);
}

static void b44_set_flow_ctrl(struct b44 *bp, u32 local, u32 remote)
{
	u32 pause_enab = 0;

	/* The driver supports only rx pause by default because
	   the b44 mac tx pause mechanism generates excessive
	   pause frames.
	   Use ethtool to turn on b44 tx pause if necessary.
	 */
	if ((local & ADVERTISE_PAUSE_CAP) &&
	    (local & ADVERTISE_PAUSE_ASYM)){
		if ((remote & LPA_PAUSE_ASYM) &&
		    !(remote & LPA_PAUSE_CAP))
			pause_enab |= B44_FLAG_RX_PAUSE;
	}

	__b44_set_flow_ctrl(bp, pause_enab);
}

#ifdef CONFIG_BCM47XX
#include <linux/bcm47xx_nvram.h>
static void b44_wap54g10_workaround(struct b44 *bp)
{
	char buf[20];
	u32 val;
	int err;

	/*
	 * workaround for bad hardware design in Linksys WAP54G v1.0
	 * see https://dev.openwrt.org/ticket/146
	 * check and reset bit "isolate"
	 */
	if (bcm47xx_nvram_getenv("boardnum", buf, sizeof(buf)) < 0)
		return;
	if (simple_strtoul(buf, NULL, 0) == 2) {
		err = __b44_readphy(bp, 0, MII_BMCR, &val);
		if (err)
			goto error;
		if (!(val & BMCR_ISOLATE))
			return;
		val &= ~BMCR_ISOLATE;
		err = __b44_writephy(bp, 0, MII_BMCR, val);
		if (err)
			goto error;
	}
	return;
error:
	pr_warn("PHY: cannot reset MII transceiver isolate bit\n");
}
#else
static inline void b44_wap54g10_workaround(struct b44 *bp)
{
}
#endif

static int b44_setup_phy(struct b44 *bp)
{
	u32 val;
	int err;

	b44_wap54g10_workaround(bp);

	if (bp->flags & B44_FLAG_EXTERNAL_PHY)
		return 0;
	if ((err = b44_readphy(bp, B44_MII_ALEDCTRL, &val)) != 0)
		goto out;
	if ((err = b44_writephy(bp, B44_MII_ALEDCTRL,
				val & MII_ALEDCTRL_ALLMSK)) != 0)
		goto out;
	if ((err = b44_readphy(bp, B44_MII_TLEDCTRL, &val)) != 0)
		goto out;
	if ((err = b44_writephy(bp, B44_MII_TLEDCTRL,
				val | MII_TLEDCTRL_ENABLE)) != 0)
		goto out;

	if (!(bp->flags & B44_FLAG_FORCE_LINK)) {
		u32 adv = ADVERTISE_CSMA;

		if (bp->flags & B44_FLAG_ADV_10HALF)
			adv |= ADVERTISE_10HALF;
		if (bp->flags & B44_FLAG_ADV_10FULL)
			adv |= ADVERTISE_10FULL;
		if (bp->flags & B44_FLAG_ADV_100HALF)
			adv |= ADVERTISE_100HALF;
		if (bp->flags & B44_FLAG_ADV_100FULL)
			adv |= ADVERTISE_100FULL;

		if (bp->flags & B44_FLAG_PAUSE_AUTO)
			adv |= ADVERTISE_PAUSE_CAP | ADVERTISE_PAUSE_ASYM;

		if ((err = b44_writephy(bp, MII_ADVERTISE, adv)) != 0)
			goto out;
		if ((err = b44_writephy(bp, MII_BMCR, (BMCR_ANENABLE |
						       BMCR_ANRESTART))) != 0)
			goto out;
	} else {
		u32 bmcr;

		if ((err = b44_readphy(bp, MII_BMCR, &bmcr)) != 0)
			goto out;
		bmcr &= ~(BMCR_FULLDPLX | BMCR_ANENABLE | BMCR_SPEED100);
		if (bp->flags & B44_FLAG_100_BASE_T)
			bmcr |= BMCR_SPEED100;
		if (bp->flags & B44_FLAG_FULL_DUPLEX)
			bmcr |= BMCR_FULLDPLX;
		if ((err = b44_writephy(bp, MII_BMCR, bmcr)) != 0)
			goto out;

		/* Since we will not be negotiating there is no safe way
		 * to determine if the link partner supports flow control
		 * or not.  So just disable it completely in this case.
		 */
		b44_set_flow_ctrl(bp, 0, 0);
	}

out:
	return err;
}

static void b44_stats_update(struct b44 *bp)
{
	unsigned long reg;
	u64 *val;

	val = &bp->hw_stats.tx_good_octets;
	u64_stats_update_begin(&bp->hw_stats.syncp);

	for (reg = B44_TX_GOOD_O; reg <= B44_TX_PAUSE; reg += 4UL) {
		*val++ += br32(bp, reg);
	}

	/* Pad */
	reg += 8*4UL;

	for (reg = B44_RX_GOOD_O; reg <= B44_RX_NPAUSE; reg += 4UL) {
		*val++ += br32(bp, reg);
	}

	u64_stats_update_end(&bp->hw_stats.syncp);
}

static void b44_link_report(struct b44 *bp)
{
	if (!netif_carrier_ok(bp->dev)) {
		netdev_info(bp->dev, "Link is down\n");
	} else {
		netdev_info(bp->dev, "Link is up at %d Mbps, %s duplex\n",
			    (bp->flags & B44_FLAG_100_BASE_T) ? 100 : 10,
			    (bp->flags & B44_FLAG_FULL_DUPLEX) ? "full" : "half");

		netdev_info(bp->dev, "Flow control is %s for TX and %s for RX\n",
			    (bp->flags & B44_FLAG_TX_PAUSE) ? "on" : "off",
			    (bp->flags & B44_FLAG_RX_PAUSE) ? "on" : "off");
	}
}

static void b44_check_phy(struct b44 *bp)
{
	u32 bmsr, aux;

	if (bp->flags & B44_FLAG_EXTERNAL_PHY) {
		bp->flags |= B44_FLAG_100_BASE_T;
		if (!netif_carrier_ok(bp->dev)) {
			u32 val = br32(bp, B44_TX_CTRL);
			if (bp->flags & B44_FLAG_FULL_DUPLEX)
				val |= TX_CTRL_DUPLEX;
			else
				val &= ~TX_CTRL_DUPLEX;
			bw32(bp, B44_TX_CTRL, val);
			netif_carrier_on(bp->dev);
			b44_link_report(bp);
		}
		return;
	}

	if (!b44_readphy(bp, MII_BMSR, &bmsr) &&
	    !b44_readphy(bp, B44_MII_AUXCTRL, &aux) &&
	    (bmsr != 0xffff)) {
		if (aux & MII_AUXCTRL_SPEED)
			bp->flags |= B44_FLAG_100_BASE_T;
		else
			bp->flags &= ~B44_FLAG_100_BASE_T;
		if (aux & MII_AUXCTRL_DUPLEX)
			bp->flags |= B44_FLAG_FULL_DUPLEX;
		else
			bp->flags &= ~B44_FLAG_FULL_DUPLEX;

		if (!netif_carrier_ok(bp->dev) &&
		    (bmsr & BMSR_LSTATUS)) {
			u32 val = br32(bp, B44_TX_CTRL);
			u32 local_adv, remote_adv;

			if (bp->flags & B44_FLAG_FULL_DUPLEX)
				val |= TX_CTRL_DUPLEX;
			else
				val &= ~TX_CTRL_DUPLEX;
			bw32(bp, B44_TX_CTRL, val);

			if (!(bp->flags & B44_FLAG_FORCE_LINK) &&
			    !b44_readphy(bp, MII_ADVERTISE, &local_adv) &&
			    !b44_readphy(bp, MII_LPA, &remote_adv))
				b44_set_flow_ctrl(bp, local_adv, remote_adv);

			/* Link now up */
			netif_carrier_on(bp->dev);
			b44_link_report(bp);
		} else if (netif_carrier_ok(bp->dev) && !(bmsr & BMSR_LSTATUS)) {
			/* Link now down */
			netif_carrier_off(bp->dev);
			b44_link_report(bp);
		}

		if (bmsr & BMSR_RFAULT)
			netdev_warn(bp->dev, "Remote fault detected in PHY\n");
		if (bmsr & BMSR_JCD)
			netdev_warn(bp->dev, "Jabber detected in PHY\n");
	}
}

static void b44_timer(struct timer_list *t)
{
	struct b44 *bp = from_timer(bp, t, timer);

	spin_lock_irq(&bp->lock);

	b44_check_phy(bp);

	b44_stats_update(bp);

	spin_unlock_irq(&bp->lock);

	mod_timer(&bp->timer, round_jiffies(jiffies + HZ));
}

static void b44_tx(struct b44 *bp)
{
	u32 cur, cons;
	unsigned bytes_compl = 0, pkts_compl = 0;

	cur  = br32(bp, B44_DMATX_STAT) & DMATX_STAT_CDMASK;
	cur /= sizeof(struct dma_desc);

	/* XXX needs updating when NETIF_F_SG is supported */
	for (cons = bp->tx_cons; cons != cur; cons = NEXT_TX(cons)) {
		struct ring_info *rp = &bp->tx_buffers[cons];
		struct sk_buff *skb = rp->skb;

		BUG_ON(skb == NULL);

		dma_unmap_single(bp->sdev->dma_dev,
				 rp->mapping,
				 skb->len,
				 DMA_TO_DEVICE);
		rp->skb = NULL;

		bytes_compl += skb->len;
		pkts_compl++;

		dev_kfree_skb_irq(skb);
	}

	netdev_completed_queue(bp->dev, pkts_compl, bytes_compl);
	bp->tx_cons = cons;
	if (netif_queue_stopped(bp->dev) &&
	    TX_BUFFS_AVAIL(bp) > B44_TX_WAKEUP_THRESH)
		netif_wake_queue(bp->dev);

	bw32(bp, B44_GPTIMER, 0);
}

/* Works like this.  This chip writes a 'struct rx_header" 30 bytes
 * before the DMA address you give it.  So we allocate 30 more bytes
 * for the RX buffer, DMA map all of it, skb_reserve the 30 bytes, then
 * point the chip at 30 bytes past where the rx_header will go.
 */
static int b44_alloc_rx_skb(struct b44 *bp, int src_idx, u32 dest_idx_unmasked)
{
	struct dma_desc *dp;
	struct ring_info *src_map, *map;
	struct rx_header *rh;
	struct sk_buff *skb;
	dma_addr_t mapping;
	int dest_idx;
	u32 ctrl;

	src_map = NULL;
	if (src_idx >= 0)
		src_map = &bp->rx_buffers[src_idx];
	dest_idx = dest_idx_unmasked & (B44_RX_RING_SIZE - 1);
	map = &bp->rx_buffers[dest_idx];
	skb = netdev_alloc_skb(bp->dev, RX_PKT_BUF_SZ);
	if (skb == NULL)
		return -ENOMEM;

	mapping = dma_map_single(bp->sdev->dma_dev, skb->data,
				 RX_PKT_BUF_SZ,
				 DMA_FROM_DEVICE);

	/* Hardware bug work-around, the chip is unable to do PCI DMA
	   to/from anything above 1GB :-( */
	if (dma_mapping_error(bp->sdev->dma_dev, mapping) ||
		mapping + RX_PKT_BUF_SZ > DMA_BIT_MASK(30)) {
		/* Sigh... */
		if (!dma_mapping_error(bp->sdev->dma_dev, mapping))
			dma_unmap_single(bp->sdev->dma_dev, mapping,
					     RX_PKT_BUF_SZ, DMA_FROM_DEVICE);
		dev_kfree_skb_any(skb);
		skb = alloc_skb(RX_PKT_BUF_SZ, GFP_ATOMIC | GFP_DMA);
		if (skb == NULL)
			return -ENOMEM;
		mapping = dma_map_single(bp->sdev->dma_dev, skb->data,
					 RX_PKT_BUF_SZ,
					 DMA_FROM_DEVICE);
		if (dma_mapping_error(bp->sdev->dma_dev, mapping) ||
		    mapping + RX_PKT_BUF_SZ > DMA_BIT_MASK(30)) {
			if (!dma_mapping_error(bp->sdev->dma_dev, mapping))
				dma_unmap_single(bp->sdev->dma_dev, mapping, RX_PKT_BUF_SZ,DMA_FROM_DEVICE);
			dev_kfree_skb_any(skb);
			return -ENOMEM;
		}
		bp->force_copybreak = 1;
	}

	rh = (struct rx_header *) skb->data;

	rh->len = 0;
	rh->flags = 0;

	map->skb = skb;
	map->mapping = mapping;

	if (src_map != NULL)
		src_map->skb = NULL;

	ctrl = (DESC_CTRL_LEN & RX_PKT_BUF_SZ);
	if (dest_idx == (B44_RX_RING_SIZE - 1))
		ctrl |= DESC_CTRL_EOT;

	dp = &bp->rx_ring[dest_idx];
	dp->ctrl = cpu_to_le32(ctrl);
	dp->addr = cpu_to_le32((u32) mapping + bp->dma_offset);

	if (bp->flags & B44_FLAG_RX_RING_HACK)
		b44_sync_dma_desc_for_device(bp->sdev, bp->rx_ring_dma,
			                    dest_idx * sizeof(*dp),
			                    DMA_BIDIRECTIONAL);

	return RX_PKT_BUF_SZ;
}

static void b44_recycle_rx(struct b44 *bp, int src_idx, u32 dest_idx_unmasked)
{
	struct dma_desc *src_desc, *dest_desc;
	struct ring_info *src_map, *dest_map;
	struct rx_header *rh;
	int dest_idx;
	__le32 ctrl;

	dest_idx = dest_idx_unmasked & (B44_RX_RING_SIZE - 1);
	dest_desc = &bp->rx_ring[dest_idx];
	dest_map = &bp->rx_buffers[dest_idx];
	src_desc = &bp->rx_ring[src_idx];
	src_map = &bp->rx_buffers[src_idx];

	dest_map->skb = src_map->skb;
	rh = (struct rx_header *) src_map->skb->data;
	rh->len = 0;
	rh->flags = 0;
	dest_map->mapping = src_map->mapping;

	if (bp->flags & B44_FLAG_RX_RING_HACK)
		b44_sync_dma_desc_for_cpu(bp->sdev, bp->rx_ring_dma,
			                 src_idx * sizeof(*src_desc),
			                 DMA_BIDIRECTIONAL);

	ctrl = src_desc->ctrl;
	if (dest_idx == (B44_RX_RING_SIZE - 1))
		ctrl |= cpu_to_le32(DESC_CTRL_EOT);
	else
		ctrl &= cpu_to_le32(~DESC_CTRL_EOT);

	dest_desc->ctrl = ctrl;
	dest_desc->addr = src_desc->addr;

	src_map->skb = NULL;

	if (bp->flags & B44_FLAG_RX_RING_HACK)
		b44_sync_dma_desc_for_device(bp->sdev, bp->rx_ring_dma,
					     dest_idx * sizeof(*dest_desc),
					     DMA_BIDIRECTIONAL);

	dma_sync_single_for_device(bp->sdev->dma_dev, dest_map->mapping,
				   RX_PKT_BUF_SZ,
				   DMA_FROM_DEVICE);
}

static int b44_rx(struct b44 *bp, int budget)
{
	int received;
	u32 cons, prod;

	received = 0;
	prod  = br32(bp, B44_DMARX_STAT) & DMARX_STAT_CDMASK;
	prod /= sizeof(struct dma_desc);
	cons = bp->rx_cons;

	while (cons != prod && budget > 0) {
		struct ring_info *rp = &bp->rx_buffers[cons];
		struct sk_buff *skb = rp->skb;
		dma_addr_t map = rp->mapping;
		struct rx_header *rh;
		u16 len;

		dma_sync_single_for_cpu(bp->sdev->dma_dev, map,
					RX_PKT_BUF_SZ,
					DMA_FROM_DEVICE);
		rh = (struct rx_header *) skb->data;
		len = le16_to_cpu(rh->len);
		if ((len > (RX_PKT_BUF_SZ - RX_PKT_OFFSET)) ||
		    (rh->flags & cpu_to_le16(RX_FLAG_ERRORS))) {
		drop_it:
			b44_recycle_rx(bp, cons, bp->rx_prod);
		drop_it_no_recycle:
			bp->dev->stats.rx_dropped++;
			goto next_pkt;
		}

		if (len == 0) {
			int i = 0;

			do {
				udelay(2);
				barrier();
				len = le16_to_cpu(rh->len);
			} while (len == 0 && i++ < 5);
			if (len == 0)
				goto drop_it;
		}

		/* Omit CRC. */
		len -= 4;

		if (!bp->force_copybreak && len > RX_COPY_THRESHOLD) {
			int skb_size;
			skb_size = b44_alloc_rx_skb(bp, cons, bp->rx_prod);
			if (skb_size < 0)
				goto drop_it;
			dma_unmap_single(bp->sdev->dma_dev, map,
					 skb_size, DMA_FROM_DEVICE);
			/* Leave out rx_header */
			skb_put(skb, len + RX_PKT_OFFSET);
			skb_pull(skb, RX_PKT_OFFSET);
		} else {
			struct sk_buff *copy_skb;

			b44_recycle_rx(bp, cons, bp->rx_prod);
			copy_skb = napi_alloc_skb(&bp->napi, len);
			if (copy_skb == NULL)
				goto drop_it_no_recycle;

			skb_put(copy_skb, len);
			/* DMA sync done above, copy just the actual packet */
			skb_copy_from_linear_data_offset(skb, RX_PKT_OFFSET,
							 copy_skb->data, len);
			skb = copy_skb;
		}
		skb_checksum_none_assert(skb);
		skb->protocol = eth_type_trans(skb, bp->dev);
		netif_receive_skb(skb);
		received++;
		budget--;
	next_pkt:
		bp->rx_prod = (bp->rx_prod + 1) &
			(B44_RX_RING_SIZE - 1);
		cons = (cons + 1) & (B44_RX_RING_SIZE - 1);
	}

	bp->rx_cons = cons;
	bw32(bp, B44_DMARX_PTR, cons * sizeof(struct dma_desc));

	return received;
}

static int b44_poll(struct napi_struct *napi, int budget)
{
	struct b44 *bp = container_of(napi, struct b44, napi);
	int work_done;
	unsigned long flags;

	spin_lock_irqsave(&bp->lock, flags);

	if (bp->istat & (ISTAT_TX | ISTAT_TO)) {
		/* spin_lock(&bp->tx_lock); */
		b44_tx(bp);
		/* spin_unlock(&bp->tx_lock); */
	}
	if (bp->istat & ISTAT_RFO) {	/* fast recovery, in ~20msec */
		bp->istat &= ~ISTAT_RFO;
		b44_disable_ints(bp);
		ssb_device_enable(bp->sdev, 0); /* resets ISTAT_RFO */
		b44_init_rings(bp);
		b44_init_hw(bp, B44_FULL_RESET_SKIP_PHY);
		netif_wake_queue(bp->dev);
	}

	spin_unlock_irqrestore(&bp->lock, flags);

	work_done = 0;
	if (bp->istat & ISTAT_RX)
		work_done += b44_rx(bp, budget);

	if (bp->istat & ISTAT_ERRORS) {
		spin_lock_irqsave(&bp->lock, flags);
		b44_halt(bp);
		b44_init_rings(bp);
		b44_init_hw(bp, B44_FULL_RESET_SKIP_PHY);
		netif_wake_queue(bp->dev);
		spin_unlock_irqrestore(&bp->lock, flags);
		work_done = 0;
	}

	if (work_done < budget) {
		napi_complete_done(napi, work_done);
		b44_enable_ints(bp);
	}

	return work_done;
}

static irqreturn_t b44_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct b44 *bp = netdev_priv(dev);
	u32 istat, imask;
	int handled = 0;

	spin_lock(&bp->lock);

	istat = br32(bp, B44_ISTAT);
	imask = br32(bp, B44_IMASK);

	/* The interrupt mask register controls which interrupt bits
	 * will actually raise an interrupt to the CPU when set by hw/firmware,
	 * but doesn't mask off the bits.
	 */
	istat &= imask;
	if (istat) {
		handled = 1;

		if (unlikely(!netif_running(dev))) {
			netdev_info(dev, "late interrupt\n");
			goto irq_ack;
		}

		if (napi_schedule_prep(&bp->napi)) {
			/* NOTE: These writes are posted by the readback of
			 *       the ISTAT register below.
			 */
			bp->istat = istat;
			__b44_disable_ints(bp);
			__napi_schedule(&bp->napi);
		}

irq_ack:
		bw32(bp, B44_ISTAT, istat);
		br32(bp, B44_ISTAT);
	}
	spin_unlock(&bp->lock);
	return IRQ_RETVAL(handled);
}

static void b44_tx_timeout(struct net_device *dev)
{
	struct b44 *bp = netdev_priv(dev);

	netdev_err(dev, "transmit timed out, resetting\n");

	spin_lock_irq(&bp->lock);

	b44_halt(bp);
	b44_init_rings(bp);
	b44_init_hw(bp, B44_FULL_RESET);

	spin_unlock_irq(&bp->lock);

	b44_enable_ints(bp);

	netif_wake_queue(dev);
}

static netdev_tx_t b44_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct b44 *bp = netdev_priv(dev);
	int rc = NETDEV_TX_OK;
	dma_addr_t mapping;
	u32 len, entry, ctrl;
	unsigned long flags;

	len = skb->len;
	spin_lock_irqsave(&bp->lock, flags);

	/* This is a hard error, log it. */
	if (unlikely(TX_BUFFS_AVAIL(bp) < 1)) {
		netif_stop_queue(dev);
		netdev_err(dev, "BUG! Tx Ring full when queue awake!\n");
		goto err_out;
	}

	mapping = dma_map_single(bp->sdev->dma_dev, skb->data, len, DMA_TO_DEVICE);
	if (dma_mapping_error(bp->sdev->dma_dev, mapping) || mapping + len > DMA_BIT_MASK(30)) {
		struct sk_buff *bounce_skb;

		/* Chip can't handle DMA to/from >1GB, use bounce buffer */
		if (!dma_mapping_error(bp->sdev->dma_dev, mapping))
			dma_unmap_single(bp->sdev->dma_dev, mapping, len,
					     DMA_TO_DEVICE);

		bounce_skb = alloc_skb(len, GFP_ATOMIC | GFP_DMA);
		if (!bounce_skb)
			goto err_out;

		mapping = dma_map_single(bp->sdev->dma_dev, bounce_skb->data,
					 len, DMA_TO_DEVICE);
		if (dma_mapping_error(bp->sdev->dma_dev, mapping) || mapping + len > DMA_BIT_MASK(30)) {
			if (!dma_mapping_error(bp->sdev->dma_dev, mapping))
				dma_unmap_single(bp->sdev->dma_dev, mapping,
						     len, DMA_TO_DEVICE);
			dev_kfree_skb_any(bounce_skb);
			goto err_out;
		}

		skb_copy_from_linear_data(skb, skb_put(bounce_skb, len), len);
		dev_kfree_skb_any(skb);
		skb = bounce_skb;
	}

	entry = bp->tx_prod;
	bp->tx_buffers[entry].skb = skb;
	bp->tx_buffers[entry].mapping = mapping;

	ctrl  = (len & DESC_CTRL_LEN);
	ctrl |= DESC_CTRL_IOC | DESC_CTRL_SOF | DESC_CTRL_EOF;
	if (entry == (B44_TX_RING_SIZE - 1))
		ctrl |= DESC_CTRL_EOT;

	bp->tx_ring[entry].ctrl = cpu_to_le32(ctrl);
	bp->tx_ring[entry].addr = cpu_to_le32((u32) mapping+bp->dma_offset);

	if (bp->flags & B44_FLAG_TX_RING_HACK)
		b44_sync_dma_desc_for_device(bp->sdev, bp->tx_ring_dma,
			                    entry * sizeof(bp->tx_ring[0]),
			                    DMA_TO_DEVICE);

	entry = NEXT_TX(entry);

	bp->tx_prod = entry;

	wmb();

	bw32(bp, B44_DMATX_PTR, entry * sizeof(struct dma_desc));
	if (bp->flags & B44_FLAG_BUGGY_TXPTR)
		bw32(bp, B44_DMATX_PTR, entry * sizeof(struct dma_desc));
	if (bp->flags & B44_FLAG_REORDER_BUG)
		br32(bp, B44_DMATX_PTR);

	netdev_sent_queue(dev, skb->len);

	if (TX_BUFFS_AVAIL(bp) < 1)
		netif_stop_queue(dev);

out_unlock:
	spin_unlock_irqrestore(&bp->lock, flags);

	return rc;

err_out:
	rc = NETDEV_TX_BUSY;
	goto out_unlock;
}

static int b44_change_mtu(struct net_device *dev, int new_mtu)
{
	struct b44 *bp = netdev_priv(dev);

	if (!netif_running(dev)) {
		/* We'll just catch it later when the
		 * device is up'd.
		 */
		dev->mtu = new_mtu;
		return 0;
	}

	spin_lock_irq(&bp->lock);
	b44_halt(bp);
	dev->mtu = new_mtu;
	b44_init_rings(bp);
	b44_init_hw(bp, B44_FULL_RESET);
	spin_unlock_irq(&bp->lock);

	b44_enable_ints(bp);

	return 0;
}

/* Free up pending packets in all rx/tx rings.
 *
 * The chip has been shut down and the driver detached from
 * the networking, so no interrupts or new tx packets will
 * end up in the driver.  bp->lock is not held and we are not
 * in an interrupt context and thus may sleep.
 */
static void b44_free_rings(struct b44 *bp)
{
	struct ring_info *rp;
	int i;

	for (i = 0; i < B44_RX_RING_SIZE; i++) {
		rp = &bp->rx_buffers[i];

		if (rp->skb == NULL)
			continue;
		dma_unmap_single(bp->sdev->dma_dev, rp->mapping, RX_PKT_BUF_SZ,
				 DMA_FROM_DEVICE);
		dev_kfree_skb_any(rp->skb);
		rp->skb = NULL;
	}

	/* XXX needs changes once NETIF_F_SG is set... */
	for (i = 0; i < B44_TX_RING_SIZE; i++) {
		rp = &bp->tx_buffers[i];

		if (rp->skb == NULL)
			continue;
		dma_unmap_single(bp->sdev->dma_dev, rp->mapping, rp->skb->len,
				 DMA_TO_DEVICE);
		dev_kfree_skb_any(rp->skb);
		rp->skb = NULL;
	}
}

/* Initialize tx/rx rings for packet processing.
 *
 * The chip has been shut down and the driver detached from
 * the networking, so no interrupts or new tx packets will
 * end up in the driver.
 */
static void b44_init_rings(struct b44 *bp)
{
	int i;

	b44_free_rings(bp);

	memset(bp->rx_ring, 0, B44_RX_RING_BYTES);
	memset(bp->tx_ring, 0, B44_TX_RING_BYTES);

	if (bp->flags & B44_FLAG_RX_RING_HACK)
		dma_sync_single_for_device(bp->sdev->dma_dev, bp->rx_ring_dma,
					   DMA_TABLE_BYTES, DMA_BIDIRECTIONAL);

	if (bp->flags & B44_FLAG_TX_RING_HACK)
		dma_sync_single_for_device(bp->sdev->dma_dev, bp->tx_ring_dma,
					   DMA_TABLE_BYTES, DMA_TO_DEVICE);

	for (i = 0; i < bp->rx_pending; i++) {
		if (b44_alloc_rx_skb(bp, -1, i) < 0)
			break;
	}
}

/*
 * Must not be invoked with interrupt sources disabled and
 * the hardware shutdown down.
 */
static void b44_free_consistent(struct b44 *bp)
{
	kfree(bp->rx_buffers);
	bp->rx_buffers = NULL;
	kfree(bp->tx_buffers);
	bp->tx_buffers = NULL;
	if (bp->rx_ring) {
		if (bp->flags & B44_FLAG_RX_RING_HACK) {
			dma_unmap_single(bp->sdev->dma_dev, bp->rx_ring_dma,
					 DMA_TABLE_BYTES, DMA_BIDIRECTIONAL);
			kfree(bp->rx_ring);
		} else
			dma_free_coherent(bp->sdev->dma_dev, DMA_TABLE_BYTES,
					  bp->rx_ring, bp->rx_ring_dma);
		bp->rx_ring = NULL;
		bp->flags &= ~B44_FLAG_RX_RING_HACK;
	}
	if (bp->tx_ring) {
		if (bp->flags & B44_FLAG_TX_RING_HACK) {
			dma_unmap_single(bp->sdev->dma_dev, bp->tx_ring_dma,
					 DMA_TABLE_BYTES, DMA_TO_DEVICE);
			kfree(bp->tx_ring);
		} else
			dma_free_coherent(bp->sdev->dma_dev, DMA_TABLE_BYTES,
					  bp->tx_ring, bp->tx_ring_dma);
		bp->tx_ring = NULL;
		bp->flags &= ~B44_FLAG_TX_RING_HACK;
	}
}

/*
 * Must not be invoked with interrupt sources disabled and
 * the hardware shutdown down.  Can sleep.
 */
static int b44_alloc_consistent(struct b44 *bp, gfp_t gfp)
{
	int size;

	size  = B44_RX_RING_SIZE * sizeof(struct ring_info);
	bp->rx_buffers = kzalloc(size, gfp);
	if (!bp->rx_buffers)
		goto out_err;

	size = B44_TX_RING_SIZE * sizeof(struct ring_info);
	bp->tx_buffers = kzalloc(size, gfp);
	if (!bp->tx_buffers)
		goto out_err;

	size = DMA_TABLE_BYTES;
	bp->rx_ring = dma_alloc_coherent(bp->sdev->dma_dev, size,
					 &bp->rx_ring_dma, gfp);
	if (!bp->rx_ring) {
		/* Allocation may have failed due to pci_alloc_consistent
		   insisting on use of GFP_DMA, which is more restrictive
		   than necessary...  */
		struct dma_desc *rx_ring;
		dma_addr_t rx_ring_dma;

		rx_ring = kzalloc(size, gfp);
		if (!rx_ring)
			goto out_err;

		rx_ring_dma = dma_map_single(bp->sdev->dma_dev, rx_ring,
					     DMA_TABLE_BYTES,
					     DMA_BIDIRECTIONAL);

		if (dma_mapping_error(bp->sdev->dma_dev, rx_ring_dma) ||
			rx_ring_dma + size > DMA_BIT_MASK(30)) {
			kfree(rx_ring);
			goto out_err;
		}

		bp->rx_ring = rx_ring;
		bp->rx_ring_dma = rx_ring_dma;
		bp->flags |= B44_FLAG_RX_RING_HACK;
	}

	bp->tx_ring = dma_alloc_coherent(bp->sdev->dma_dev, size,
					 &bp->tx_ring_dma, gfp);
	if (!bp->tx_ring) {
		/* Allocation may have failed due to ssb_dma_alloc_consistent
		   insisting on use of GFP_DMA, which is more restrictive
		   than necessary...  */
		struct dma_desc *tx_ring;
		dma_addr_t tx_ring_dma;

		tx_ring = kzalloc(size, gfp);
		if (!tx_ring)
			goto out_err;

		tx_ring_dma = dma_map_single(bp->sdev->dma_dev, tx_ring,
					     DMA_TABLE_BYTES,
					     DMA_TO_DEVICE);

		if (dma_mapping_error(bp->sdev->dma_dev, tx_ring_dma) ||
			tx_ring_dma + size > DMA_BIT_MASK(30)) {
			kfree(tx_ring);
			goto out_err;
		}

		bp->tx_ring = tx_ring;
		bp->tx_ring_dma = tx_ring_dma;
		bp->flags |= B44_FLAG_TX_RING_HACK;
	}

	return 0;

out_err:
	b44_free_consistent(bp);
	return -ENOMEM;
}

/* bp->lock is held. */
static void b44_clear_stats(struct b44 *bp)
{
	unsigned long reg;

	bw32(bp, B44_MIB_CTRL, MIB_CTRL_CLR_ON_READ);
	for (reg = B44_TX_GOOD_O; reg <= B44_TX_PAUSE; reg += 4UL)
		br32(bp, reg);
	for (reg = B44_RX_GOOD_O; reg <= B44_RX_NPAUSE; reg += 4UL)
		br32(bp, reg);
}

/* bp->lock is held. */
static void b44_chip_reset(struct b44 *bp, int reset_kind)
{
	struct ssb_device *sdev = bp->sdev;
	bool was_enabled;

	was_enabled = ssb_device_is_enabled(bp->sdev);

	ssb_device_enable(bp->sdev, 0);
	ssb_pcicore_dev_irqvecs_enable(&sdev->bus->pcicore, sdev);

	if (was_enabled) {
		bw32(bp, B44_RCV_LAZY, 0);
		bw32(bp, B44_ENET_CTRL, ENET_CTRL_DISABLE);
		b44_wait_bit(bp, B44_ENET_CTRL, ENET_CTRL_DISABLE, 200, 1);
		bw32(bp, B44_DMATX_CTRL, 0);
		bp->tx_prod = bp->tx_cons = 0;
		if (br32(bp, B44_DMARX_STAT) & DMARX_STAT_EMASK) {
			b44_wait_bit(bp, B44_DMARX_STAT, DMARX_STAT_SIDLE,
				     100, 0);
		}
		bw32(bp, B44_DMARX_CTRL, 0);
		bp->rx_prod = bp->rx_cons = 0;
	}

	b44_clear_stats(bp);

	/*
	 * Don't enable PHY if we are doing a partial reset
	 * we are probably going to power down
	 */
	if (reset_kind == B44_CHIP_RESET_PARTIAL)
		return;

	switch (sdev->bus->bustype) {
	case SSB_BUSTYPE_SSB:
		bw32(bp, B44_MDIO_CTRL, (MDIO_CTRL_PREAMBLE |
		     (DIV_ROUND_CLOSEST(ssb_clockspeed(sdev->bus),
					B44_MDC_RATIO)
		     & MDIO_CTRL_MAXF_MASK)));
		break;
	case SSB_BUSTYPE_PCI:
		bw32(bp, B44_MDIO_CTRL, (MDIO_CTRL_PREAMBLE |
		     (0x0d & MDIO_CTRL_MAXF_MASK)));
		break;
	case SSB_BUSTYPE_PCMCIA:
	case SSB_BUSTYPE_SDIO:
		WARN_ON(1); /* A device with this bus does not exist. */
		break;
	}

	br32(bp, B44_MDIO_CTRL);

	if (!(br32(bp, B44_DEVCTRL) & DEVCTRL_IPP)) {
		bw32(bp, B44_ENET_CTRL, ENET_CTRL_EPSEL);
		br32(bp, B44_ENET_CTRL);
		bp->flags |= B44_FLAG_EXTERNAL_PHY;
	} else {
		u32 val = br32(bp, B44_DEVCTRL);

		if (val & DEVCTRL_EPR) {
			bw32(bp, B44_DEVCTRL, (val & ~DEVCTRL_EPR));
			br32(bp, B44_DEVCTRL);
			udelay(100);
		}
		bp->flags &= ~B44_FLAG_EXTERNAL_PHY;
	}
}

/* bp->lock is held. */
static void b44_halt(struct b44 *bp)
{
	b44_disable_ints(bp);
	/* reset PHY */
	b44_phy_reset(bp);
	/* power down PHY */
	netdev_info(bp->dev, "powering down PHY\n");
	bw32(bp, B44_MAC_CTRL, MAC_CTRL_PHY_PDOWN);
	/* now reset the chip, but without enabling the MAC&PHY
	 * part of it. This has to be done _after_ we shut down the PHY */
	if (bp->flags & B44_FLAG_EXTERNAL_PHY)
		b44_chip_reset(bp, B44_CHIP_RESET_FULL);
	else
		b44_chip_reset(bp, B44_CHIP_RESET_PARTIAL);
}

/* bp->lock is held. */
static void __b44_set_mac_addr(struct b44 *bp)
{
	bw32(bp, B44_CAM_CTRL, 0);
	if (!(bp->dev->flags & IFF_PROMISC)) {
		u32 val;

		__b44_cam_write(bp, bp->dev->dev_addr, 0);
		val = br32(bp, B44_CAM_CTRL);
		bw32(bp, B44_CAM_CTRL, val | CAM_CTRL_ENABLE);
	}
}

static int b44_set_mac_addr(struct net_device *dev, void *p)
{
	struct b44 *bp = netdev_priv(dev);
	struct sockaddr *addr = p;
	u32 val;

	if (netif_running(dev))
		return -EBUSY;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EINVAL;

	memcpy(dev->dev_addr, addr->sa_data, dev->addr_len);

	spin_lock_irq(&bp->lock);

	val = br32(bp, B44_RXCONFIG);
	if (!(val & RXCONFIG_CAM_ABSENT))
		__b44_set_mac_addr(bp);

	spin_unlock_irq(&bp->lock);

	return 0;
}

/* Called at device open time to get the chip ready for
 * packet processing.  Invoked with bp->lock held.
 */
static void __b44_set_rx_mode(struct net_device *);
static void b44_init_hw(struct b44 *bp, int reset_kind)
{
	u32 val;

	b44_chip_reset(bp, B44_CHIP_RESET_FULL);
	if (reset_kind == B44_FULL_RESET) {
		b44_phy_reset(bp);
		b44_setup_phy(bp);
	}

	/* Enable CRC32, set proper LED modes and power on PHY */
	bw32(bp, B44_MAC_CTRL, MAC_CTRL_CRC32_ENAB | MAC_CTRL_PHY_LEDCTRL);
	bw32(bp, B44_RCV_LAZY, (1 << RCV_LAZY_FC_SHIFT));

	/* This sets the MAC address too.  */
	__b44_set_rx_mode(bp->dev);

	/* MTU + eth header + possible VLAN tag + struct rx_header */
	bw32(bp, B44_RXMAXLEN, bp->dev->mtu + ETH_HLEN + 8 + RX_HEADER_LEN);
	bw32(bp, B44_TXMAXLEN, bp->dev->mtu + ETH_HLEN + 8 + RX_HEADER_LEN);

	bw32(bp, B44_TX_WMARK, 56); /* XXX magic */
	if (reset_kind == B44_PARTIAL_RESET) {
		bw32(bp, B44_DMARX_CTRL, (DMARX_CTRL_ENABLE |
				      (RX_PKT_OFFSET << DMARX_CTRL_ROSHIFT)));
	} else {
		bw32(bp, B44_DMATX_CTRL, DMATX_CTRL_ENABLE);
		bw32(bp, B44_DMATX_ADDR, bp->tx_ring_dma + bp->dma_offset);
		bw32(bp, B44_DMARX_CTRL, (DMARX_CTRL_ENABLE |
				      (RX_PKT_OFFSET << DMARX_CTRL_ROSHIFT)));
		bw32(bp, B44_DMARX_ADDR, bp->rx_ring_dma + bp->dma_offset);

		bw32(bp, B44_DMARX_PTR, bp->rx_pending);
		bp->rx_prod = bp->rx_pending;

		bw32(bp, B44_MIB_CTRL, MIB_CTRL_CLR_ON_READ);
	}

	val = br32(bp, B44_ENET_CTRL);
	bw32(bp, B44_ENET_CTRL, (val | ENET_CTRL_ENABLE));

	netdev_reset_queue(bp->dev);
}

static int b44_open(struct net_device *dev)
{
	struct b44 *bp = netdev_priv(dev);
	int err;

	err = b44_alloc_consistent(bp, GFP_KERNEL);
	if (err)
		goto out;

	napi_enable(&bp->napi);

	b44_init_rings(bp);
	b44_init_hw(bp, B44_FULL_RESET);

	b44_check_phy(bp);

	err = request_irq(dev->irq, b44_interrupt, IRQF_SHARED, dev->name, dev);
	if (unlikely(err < 0)) {
		napi_disable(&bp->napi);
		b44_chip_reset(bp, B44_CHIP_RESET_PARTIAL);
		b44_free_rings(bp);
		b44_free_consistent(bp);
		goto out;
	}

	timer_setup(&bp->timer, b44_timer, 0);
	bp->timer.expires = jiffies + HZ;
	add_timer(&bp->timer);

	b44_enable_ints(bp);

	if (bp->flags & B44_FLAG_EXTERNAL_PHY)
		phy_start(dev->phydev);

	netif_start_queue(dev);
out:
	return err;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
/*
 * Polling receive - used by netconsole and other diagnostic tools
 * to allow network i/o with interrupts disabled.
 */
static void b44_poll_controller(struct net_device *dev)
{
	disable_irq(dev->irq);
	b44_interrupt(dev->irq, dev);
	enable_irq(dev->irq);
}
#endif

static void bwfilter_table(struct b44 *bp, u8 *pp, u32 bytes, u32 table_offset)
{
	u32 i;
	u32 *pattern = (u32 *) pp;

	for (i = 0; i < bytes; i += sizeof(u32)) {
		bw32(bp, B44_FILT_ADDR, table_offset + i);
		bw32(bp, B44_FILT_DATA, pattern[i / sizeof(u32)]);
	}
}

static int b44_magic_pattern(u8 *macaddr, u8 *ppattern, u8 *pmask, int offset)
{
	int magicsync = 6;
	int k, j, len = offset;
	int ethaddr_bytes = ETH_ALEN;

	memset(ppattern + offset, 0xff, magicsync);
	for (j = 0; j < magicsync; j++)
		set_bit(len++, (unsigned long *) pmask);

	for (j = 0; j < B44_MAX_PATTERNS; j++) {
		if ((B44_PATTERN_SIZE - len) >= ETH_ALEN)
			ethaddr_bytes = ETH_ALEN;
		else
			ethaddr_bytes = B44_PATTERN_SIZE - len;
		if (ethaddr_bytes <=0)
			break;
		for (k = 0; k< ethaddr_bytes; k++) {
			ppattern[offset + magicsync +
				(j * ETH_ALEN) + k] = macaddr[k];
			set_bit(len++, (unsigned long *) pmask);
		}
	}
	return len - 1;
}

/* Setup magic packet patterns in the b44 WOL
 * pattern matching filter.
 */
static void b44_setup_pseudo_magicp(struct b44 *bp)
{

	u32 val;
	int plen0, plen1, plen2;
	u8 *pwol_pattern;
	u8 pwol_mask[B44_PMASK_SIZE];

	pwol_pattern = kzalloc(B44_PATTERN_SIZE, GFP_KERNEL);
	if (!pwol_pattern)
		return;

	/* Ipv4 magic packet pattern - pattern 0.*/
	memset(pwol_mask, 0, B44_PMASK_SIZE);
	plen0 = b44_magic_pattern(bp->dev->dev_addr, pwol_pattern, pwol_mask,
				  B44_ETHIPV4UDP_HLEN);

   	bwfilter_table(bp, pwol_pattern, B44_PATTERN_SIZE, B44_PATTERN_BASE);
   	bwfilter_table(bp, pwol_mask, B44_PMASK_SIZE, B44_PMASK_BASE);

	/* Raw ethernet II magic packet pattern - pattern 1 */
	memset(pwol_pattern, 0, B44_PATTERN_SIZE);
	memset(pwol_mask, 0, B44_PMASK_SIZE);
	plen1 = b44_magic_pattern(bp->dev->dev_addr, pwol_pattern, pwol_mask,
				  ETH_HLEN);

   	bwfilter_table(bp, pwol_pattern, B44_PATTERN_SIZE,
		       B44_PATTERN_BASE + B44_PATTERN_SIZE);
  	bwfilter_table(bp, pwol_mask, B44_PMASK_SIZE,
		       B44_PMASK_BASE + B44_PMASK_SIZE);

	/* Ipv6 magic packet pattern - pattern 2 */
	memset(pwol_pattern, 0, B44_PATTERN_SIZE);
	memset(pwol_mask, 0, B44_PMASK_SIZE);
	plen2 = b44_magic_pattern(bp->dev->dev_addr, pwol_pattern, pwol_mask,
				  B44_ETHIPV6UDP_HLEN);

   	bwfilter_table(bp, pwol_pattern, B44_PATTERN_SIZE,
		       B44_PATTERN_BASE + B44_PATTERN_SIZE + B44_PATTERN_SIZE);
  	bwfilter_table(bp, pwol_mask, B44_PMASK_SIZE,
		       B44_PMASK_BASE + B44_PMASK_SIZE + B44_PMASK_SIZE);

	kfree(pwol_pattern);

	/* set these pattern's lengths: one less than each real length */
	val = plen0 | (plen1 << 8) | (plen2 << 16) | WKUP_LEN_ENABLE_THREE;
	bw32(bp, B44_WKUP_LEN, val);

	/* enable wakeup pattern matching */
	val = br32(bp, B44_DEVCTRL);
	bw32(bp, B44_DEVCTRL, val | DEVCTRL_PFE);

}

#ifdef CONFIG_B44_PCI
static void b44_setup_wol_pci(struct b44 *bp)
{
	u16 val;

	if (bp->sdev->bus->bustype != SSB_BUSTYPE_SSB) {
		bw32(bp, SSB_TMSLOW, br32(bp, SSB_TMSLOW) | SSB_TMSLOW_PE);
		pci_read_config_word(bp->sdev->bus->host_pci, SSB_PMCSR, &val);
		pci_write_config_word(bp->sdev->bus->host_pci, SSB_PMCSR, val | SSB_PE);
	}
}
#else
static inline void b44_setup_wol_pci(struct b44 *bp) { }
#endif /* CONFIG_B44_PCI */

static void b44_setup_wol(struct b44 *bp)
{
	u32 val;

	bw32(bp, B44_RXCONFIG, RXCONFIG_ALLMULTI);

	if (bp->flags & B44_FLAG_B0_ANDLATER) {

		bw32(bp, B44_WKUP_LEN, WKUP_LEN_DISABLE);

		val = bp->dev->dev_addr[2] << 24 |
			bp->dev->dev_addr[3] << 16 |
			bp->dev->dev_addr[4] << 8 |
			bp->dev->dev_addr[5];
		bw32(bp, B44_ADDR_LO, val);

		val = bp->dev->dev_addr[0] << 8 |
			bp->dev->dev_addr[1];
		bw32(bp, B44_ADDR_HI, val);

		val = br32(bp, B44_DEVCTRL);
		bw32(bp, B44_DEVCTRL, val | DEVCTRL_MPM | DEVCTRL_PFE);

 	} else {
 		b44_setup_pseudo_magicp(bp);
 	}
	b44_setup_wol_pci(bp);
}

static int b44_close(struct net_device *dev)
{
	struct b44 *bp = netdev_priv(dev);

	netif_stop_queue(dev);

	if (bp->flags & B44_FLAG_EXTERNAL_PHY)
		phy_stop(dev->phydev);

	napi_disable(&bp->napi);

	del_timer_sync(&bp->timer);

	spin_lock_irq(&bp->lock);

	b44_halt(bp);
	b44_free_rings(bp);
	netif_carrier_off(dev);

	spin_unlock_irq(&bp->lock);

	free_irq(dev->irq, dev);

	if (bp->flags & B44_FLAG_WOL_ENABLE) {
		b44_init_hw(bp, B44_PARTIAL_RESET);
		b44_setup_wol(bp);
	}

	b44_free_consistent(bp);

	return 0;
}

static void b44_get_stats64(struct net_device *dev,
			    struct rtnl_link_stats64 *nstat)
{
	struct b44 *bp = netdev_priv(dev);
	struct b44_hw_stats *hwstat = &bp->hw_stats;
	unsigned int start;

	do {
		start = u64_stats_fetch_begin_irq(&hwstat->syncp);

		/* Convert HW stats into rtnl_link_stats64 stats. */
		nstat->rx_packets = hwstat->rx_pkts;
		nstat->tx_packets = hwstat->tx_pkts;
		nstat->rx_bytes   = hwstat->rx_octets;
		nstat->tx_bytes   = hwstat->tx_octets;
		nstat->tx_errors  = (hwstat->tx_jabber_pkts +
				     hwstat->tx_oversize_pkts +
				     hwstat->tx_underruns +
				     hwstat->tx_excessive_cols +
				     hwstat->tx_late_cols);
		nstat->multicast  = hwstat->rx_multicast_pkts;
		nstat->collisions = hwstat->tx_total_cols;

		nstat->rx_length_errors = (hwstat->rx_oversize_pkts +
					   hwstat->rx_undersize);
		nstat->rx_over_errors   = hwstat->rx_missed_pkts;
		nstat->rx_frame_errors  = hwstat->rx_align_errs;
		nstat->rx_crc_errors    = hwstat->rx_crc_errs;
		nstat->rx_errors        = (hwstat->rx_jabber_pkts +
					   hwstat->rx_oversize_pkts +
					   hwstat->rx_missed_pkts +
					   hwstat->rx_crc_align_errs +
					   hwstat->rx_undersize +
					   hwstat->rx_crc_errs +
					   hwstat->rx_align_errs +
					   hwstat->rx_symbol_errs);

		nstat->tx_aborted_errors = hwstat->tx_underruns;
#if 0
		/* Carrier lost counter seems to be broken for some devices */
		nstat->tx_carrier_errors = hwstat->tx_carrier_lost;
#endif
	} while (u64_stats_fetch_retry_irq(&hwstat->syncp, start));

}

static int __b44_load_mcast(struct b44 *bp, struct net_device *dev)
{
	struct netdev_hw_addr *ha;
	int i, num_ents;

	num_ents = min_t(int, netdev_mc_count(dev), B44_MCAST_TABLE_SIZE);
	i = 0;
	netdev_for_each_mc_addr(ha, dev) {
		if (i == num_ents)
			break;
		__b44_cam_write(bp, ha->addr, i++ + 1);
	}
	return i+1;
}

static void __b44_set_rx_mode(struct net_device *dev)
{
	struct b44 *bp = netdev_priv(dev);
	u32 val;

	val = br32(bp, B44_RXCONFIG);
	val &= ~(RXCONFIG_PROMISC | RXCONFIG_ALLMULTI);
	if ((dev->flags & IFF_PROMISC) || (val & RXCONFIG_CAM_ABSENT)) {
		val |= RXCONFIG_PROMISC;
		bw32(bp, B44_RXCONFIG, val);
	} else {
		unsigned char zero[6] = {0, 0, 0, 0, 0, 0};
		int i = 1;

		__b44_set_mac_addr(bp);

		if ((dev->flags & IFF_ALLMULTI) ||
		    (netdev_mc_count(dev) > B44_MCAST_TABLE_SIZE))
			val |= RXCONFIG_ALLMULTI;
		else
			i = __b44_load_mcast(bp, dev);

		for (; i < 64; i++)
			__b44_cam_write(bp, zero, i);

		bw32(bp, B44_RXCONFIG, val);
        	val = br32(bp, B44_CAM_CTRL);
	        bw32(bp, B44_CAM_CTRL, val | CAM_CTRL_ENABLE);
	}
}

static void b44_set_rx_mode(struct net_device *dev)
{
	struct b44 *bp = netdev_priv(dev);

	spin_lock_irq(&bp->lock);
	__b44_set_rx_mode(dev);
	spin_unlock_irq(&bp->lock);
}

static u32 b44_get_msglevel(struct net_device *dev)
{
	struct b44 *bp = netdev_priv(dev);
	return bp->msg_enable;
}

static void b44_set_msglevel(struct net_device *dev, u32 value)
{
	struct b44 *bp = netdev_priv(dev);
	bp->msg_enable = value;
}

static void b44_get_drvinfo (struct net_device *dev, struct ethtool_drvinfo *info)
{
	struct b44 *bp = netdev_priv(dev);
	struct ssb_bus *bus = bp->sdev->bus;

	strlcpy(info->driver, DRV_MODULE_NAME, sizeof(info->driver));
	strlcpy(info->version, DRV_MODULE_VERSION, sizeof(info->version));
	switch (bus->bustype) {
	case SSB_BUSTYPE_PCI:
		strlcpy(info->bus_info, pci_name(bus->host_pci), sizeof(info->bus_info));
		break;
	case SSB_BUSTYPE_SSB:
		strlcpy(info->bus_info, "SSB", sizeof(info->bus_info));
		break;
	case SSB_BUSTYPE_PCMCIA:
	case SSB_BUSTYPE_SDIO:
		WARN_ON(1); /* A device with this bus does not exist. */
		break;
	}
}

static int b44_nway_reset(struct net_device *dev)
{
	struct b44 *bp = netdev_priv(dev);
	u32 bmcr;
	int r;

	spin_lock_irq(&bp->lock);
	b44_readphy(bp, MII_BMCR, &bmcr);
	b44_readphy(bp, MII_BMCR, &bmcr);
	r = -EINVAL;
	if (bmcr & BMCR_ANENABLE) {
		b44_writephy(bp, MII_BMCR,
			     bmcr | BMCR_ANRESTART);
		r = 0;
	}
	spin_unlock_irq(&bp->lock);

	return r;
}

static int b44_get_link_ksettings(struct net_device *dev,
				  struct ethtool_link_ksettings *cmd)
{
	struct b44 *bp = netdev_priv(dev);
	u32 supported, advertising;

	if (bp->flags & B44_FLAG_EXTERNAL_PHY) {
		BUG_ON(!dev->phydev);
		phy_ethtool_ksettings_get(dev->phydev, cmd);

		return 0;
	}

	supported = (SUPPORTED_Autoneg);
	supported |= (SUPPORTED_100baseT_Half |
		      SUPPORTED_100baseT_Full |
		      SUPPORTED_10baseT_Half |
		      SUPPORTED_10baseT_Full |
		      SUPPORTED_MII);

	advertising = 0;
	if (bp->flags & B44_FLAG_ADV_10HALF)
		advertising |= ADVERTISED_10baseT_Half;
	if (bp->flags & B44_FLAG_ADV_10FULL)
		advertising |= ADVERTISED_10baseT_Full;
	if (bp->flags & B44_FLAG_ADV_100HALF)
		advertising |= ADVERTISED_100baseT_Half;
	if (bp->flags & B44_FLAG_ADV_100FULL)
		advertising |= ADVERTISED_100baseT_Full;
	advertising |= ADVERTISED_Pause | ADVERTISED_Asym_Pause;
	cmd->base.speed = (bp->flags & B44_FLAG_100_BASE_T) ?
		SPEED_100 : SPEED_10;
	cmd->base.duplex = (bp->flags & B44_FLAG_FULL_DUPLEX) ?
		DUPLEX_FULL : DUPLEX_HALF;
	cmd->base.port = 0;
	cmd->base.phy_address = bp->phy_addr;
	cmd->base.autoneg = (bp->flags & B44_FLAG_FORCE_LINK) ?
		AUTONEG_DISABLE : AUTONEG_ENABLE;
	if (cmd->base.autoneg == AUTONEG_ENABLE)
		advertising |= ADVERTISED_Autoneg;

	ethtool_convert_legacy_u32_to_link_mode(cmd->link_modes.supported,
						supported);
	ethtool_convert_legacy_u32_to_link_mode(cmd->link_modes.advertising,
						advertising);

	if (!netif_running(dev)){
		cmd->base.speed = 0;
		cmd->base.duplex = 0xff;
	}

	return 0;
}

static int b44_set_link_ksettings(struct net_device *dev,
				  const struct ethtool_link_ksettings *cmd)
{
	struct b44 *bp = netdev_priv(dev);
	u32 speed;
	int ret;
	u32 advertising;

	if (bp->flags & B44_FLAG_EXTERNAL_PHY) {
		BUG_ON(!dev->phydev);
		spin_lock_irq(&bp->lock);
		if (netif_running(dev))
			b44_setup_phy(bp);

		ret = phy_ethtool_ksettings_set(dev->phydev, cmd);

		spin_unlock_irq(&bp->lock);

		return ret;
	}

	speed = cmd->base.speed;

	ethtool_convert_link_mode_to_legacy_u32(&advertising,
						cmd->link_modes.advertising);

	/* We do not support gigabit. */
	if (cmd->base.autoneg == AUTONEG_ENABLE) {
		if (advertising &
		    (ADVERTISED_1000baseT_Half |
		     ADVERTISED_1000baseT_Full))
			return -EINVAL;
	} else if ((speed != SPEED_100 &&
		    speed != SPEED_10) ||
		   (cmd->base.duplex != DUPLEX_HALF &&
		    cmd->base.duplex != DUPLEX_FULL)) {
			return -EINVAL;
	}

	spin_lock_irq(&bp->lock);

	if (cmd->base.autoneg == AUTONEG_ENABLE) {
		bp->flags &= ~(B44_FLAG_FORCE_LINK |
			       B44_FLAG_100_BASE_T |
			       B44_FLAG_FULL_DUPLEX |
			       B44_FLAG_ADV_10HALF |
			       B44_FLAG_ADV_10FULL |
			       B44_FLAG_ADV_100HALF |
			       B44_FLAG_ADV_100FULL);
		if (advertising == 0) {
			bp->flags |= (B44_FLAG_ADV_10HALF |
				      B44_FLAG_ADV_10FULL |
				      B44_FLAG_ADV_100HALF |
				      B44_FLAG_ADV_100FULL);
		} else {
			if (advertising & ADVERTISED_10baseT_Half)
				bp->flags |= B44_FLAG_ADV_10HALF;
			if (advertising & ADVERTISED_10baseT_Full)
				bp->flags |= B44_FLAG_ADV_10FULL;
			if (advertising & ADVERTISED_100baseT_Half)
				bp->flags |= B44_FLAG_ADV_100HALF;
			if (advertising & ADVERTISED_100baseT_Full)
				bp->flags |= B44_FLAG_ADV_100FULL;
		}
	} else {
		bp->flags |= B44_FLAG_FORCE_LINK;
		bp->flags &= ~(B44_FLAG_100_BASE_T | B44_FLAG_FULL_DUPLEX);
		if (speed == SPEED_100)
			bp->flags |= B44_FLAG_100_BASE_T;
		if (cmd->base.duplex == DUPLEX_FULL)
			bp->flags |= B44_FLAG_FULL_DUPLEX;
	}

	if (netif_running(dev))
		b44_setup_phy(bp);

	spin_unlock_irq(&bp->lock);

	return 0;
}

static void b44_get_ringparam(struct net_device *dev,
			      struct ethtool_ringparam *ering)
{
	struct b44 *bp = netdev_priv(dev);

	ering->rx_max_pending = B44_RX_RING_SIZE - 1;
	ering->rx_pending = bp->rx_pending;

	/* XXX ethtool lacks a tx_max_pending, oops... */
}

static int b44_set_ringparam(struct net_device *dev,
			     struct ethtool_ringparam *ering)
{
	struct b44 *bp = netdev_priv(dev);

	if ((ering->rx_pending > B44_RX_RING_SIZE - 1) ||
	    (ering->rx_mini_pending != 0) ||
	    (ering->rx_jumbo_pending != 0) ||
	    (ering->tx_pending > B44_TX_RING_SIZE - 1))
		return -EINVAL;

	spin_lock_irq(&bp->lock);

	bp->rx_pending = ering->rx_pending;
	bp->tx_pending = ering->tx_pending;

	b44_halt(bp);
	b44_init_rings(bp);
	b44_init_hw(bp, B44_FULL_RESET);
	netif_wake_queue(bp->dev);
	spin_unlock_irq(&bp->lock);

	b44_enable_ints(bp);

	return 0;
}

static void b44_get_pauseparam(struct net_device *dev,
				struct ethtool_pauseparam *epause)
{
	struct b44 *bp = netdev_priv(dev);

	epause->autoneg =
		(bp->flags & B44_FLAG_PAUSE_AUTO) != 0;
	epause->rx_pause =
		(bp->flags & B44_FLAG_RX_PAUSE) != 0;
	epause->tx_pause =
		(bp->flags & B44_FLAG_TX_PAUSE) != 0;
}

static int b44_set_pauseparam(struct net_device *dev,
				struct ethtool_pauseparam *epause)
{
	struct b44 *bp = netdev_priv(dev);

	spin_lock_irq(&bp->lock);
	if (epause->autoneg)
		bp->flags |= B44_FLAG_PAUSE_AUTO;
	else
		bp->flags &= ~B44_FLAG_PAUSE_AUTO;
	if (epause->rx_pause)
		bp->flags |= B44_FLAG_RX_PAUSE;
	else
		bp->flags &= ~B44_FLAG_RX_PAUSE;
	if (epause->tx_pause)
		bp->flags |= B44_FLAG_TX_PAUSE;
	else
		bp->flags &= ~B44_FLAG_TX_PAUSE;
	if (bp->flags & B44_FLAG_PAUSE_AUTO) {
		b44_halt(bp);
		b44_init_rings(bp);
		b44_init_hw(bp, B44_FULL_RESET);
	} else {
		__b44_set_flow_ctrl(bp, bp->flags);
	}
	spin_unlock_irq(&bp->lock);

	b44_enable_ints(bp);

	return 0;
}

static void b44_get_strings(struct net_device *dev, u32 stringset, u8 *data)
{
	switch(stringset) {
	case ETH_SS_STATS:
		memcpy(data, *b44_gstrings, sizeof(b44_gstrings));
		break;
	}
}

static int b44_get_sset_count(struct net_device *dev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return ARRAY_SIZE(b44_gstrings);
	default:
		return -EOPNOTSUPP;
	}
}

static void b44_get_ethtool_stats(struct net_device *dev,
				  struct ethtool_stats *stats, u64 *data)
{
	struct b44 *bp = netdev_priv(dev);
	struct b44_hw_stats *hwstat = &bp->hw_stats;
	u64 *data_src, *data_dst;
	unsigned int start;
	u32 i;

	spin_lock_irq(&bp->lock);
	b44_stats_update(bp);
	spin_unlock_irq(&bp->lock);

	do {
		data_src = &hwstat->tx_good_octets;
		data_dst = data;
		start = u64_stats_fetch_begin_irq(&hwstat->syncp);

		for (i = 0; i < ARRAY_SIZE(b44_gstrings); i++)
			*data_dst++ = *data_src++;

	} while (u64_stats_fetch_retry_irq(&hwstat->syncp, start));
}

static void b44_get_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	struct b44 *bp = netdev_priv(dev);

	wol->supported = WAKE_MAGIC;
	if (bp->flags & B44_FLAG_WOL_ENABLE)
		wol->wolopts = WAKE_MAGIC;
	else
		wol->wolopts = 0;
	memset(&wol->sopass, 0, sizeof(wol->sopass));
}

static int b44_set_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	struct b44 *bp = netdev_priv(dev);

	spin_lock_irq(&bp->lock);
	if (wol->wolopts & WAKE_MAGIC)
		bp->flags |= B44_FLAG_WOL_ENABLE;
	else
		bp->flags &= ~B44_FLAG_WOL_ENABLE;
	spin_unlock_irq(&bp->lock);

	device_set_wakeup_enable(bp->sdev->dev, wol->wolopts & WAKE_MAGIC);
	return 0;
}

static const struct ethtool_ops b44_ethtool_ops = {
	.get_drvinfo		= b44_get_drvinfo,
	.nway_reset		= b44_nway_reset,
	.get_link		= ethtool_op_get_link,
	.get_wol		= b44_get_wol,
	.set_wol		= b44_set_wol,
	.get_ringparam		= b44_get_ringparam,
	.set_ringparam		= b44_set_ringparam,
	.get_pauseparam		= b44_get_pauseparam,
	.set_pauseparam		= b44_set_pauseparam,
	.get_msglevel		= b44_get_msglevel,
	.set_msglevel		= b44_set_msglevel,
	.get_strings		= b44_get_strings,
	.get_sset_count		= b44_get_sset_count,
	.get_ethtool_stats	= b44_get_ethtool_stats,
	.get_link_ksettings	= b44_get_link_ksettings,
	.set_link_ksettings	= b44_set_link_ksettings,
};

static int b44_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct b44 *bp = netdev_priv(dev);
	int err = -EINVAL;

	if (!netif_running(dev))
		goto out;

	spin_lock_irq(&bp->lock);
	if (bp->flags & B44_FLAG_EXTERNAL_PHY) {
		BUG_ON(!dev->phydev);
		err = phy_mii_ioctl(dev->phydev, ifr, cmd);
	} else {
		err = generic_mii_ioctl(&bp->mii_if, if_mii(ifr), cmd, NULL);
	}
	spin_unlock_irq(&bp->lock);
out:
	return err;
}

static int b44_get_invariants(struct b44 *bp)
{
	struct ssb_device *sdev = bp->sdev;
	int err = 0;
	u8 *addr;

	bp->dma_offset = ssb_dma_translation(sdev);

	if (sdev->bus->bustype == SSB_BUSTYPE_SSB &&
	    instance > 1) {
		addr = sdev->bus->sprom.et1mac;
		bp->phy_addr = sdev->bus->sprom.et1phyaddr;
	} else {
		addr = sdev->bus->sprom.et0mac;
		bp->phy_addr = sdev->bus->sprom.et0phyaddr;
	}
	/* Some ROMs have buggy PHY addresses with the high
	 * bits set (sign extension?). Truncate them to a
	 * valid PHY address. */
	bp->phy_addr &= 0x1F;

	memcpy(bp->dev->dev_addr, addr, ETH_ALEN);

	if (!is_valid_ether_addr(&bp->dev->dev_addr[0])){
		pr_err("Invalid MAC address found in EEPROM\n");
		return -EINVAL;
	}

	bp->imask = IMASK_DEF;

	/* XXX - really required?
	   bp->flags |= B44_FLAG_BUGGY_TXPTR;
	*/

	if (bp->sdev->id.revision >= 7)
		bp->flags |= B44_FLAG_B0_ANDLATER;

	return err;
}

static const struct net_device_ops b44_netdev_ops = {
	.ndo_open		= b44_open,
	.ndo_stop		= b44_close,
	.ndo_start_xmit		= b44_start_xmit,
	.ndo_get_stats64	= b44_get_stats64,
	.ndo_set_rx_mode	= b44_set_rx_mode,
	.ndo_set_mac_address	= b44_set_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_do_ioctl		= b44_ioctl,
	.ndo_tx_timeout		= b44_tx_timeout,
	.ndo_change_mtu		= b44_change_mtu,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= b44_poll_controller,
#endif
};

static void b44_adjust_link(struct net_device *dev)
{
	struct b44 *bp = netdev_priv(dev);
	struct phy_device *phydev = dev->phydev;
	bool status_changed = 0;

	BUG_ON(!phydev);

	if (bp->old_link != phydev->link) {
		status_changed = 1;
		bp->old_link = phydev->link;
	}

	/* reflect duplex change */
	if (phydev->link) {
		if ((phydev->duplex == DUPLEX_HALF) &&
		    (bp->flags & B44_FLAG_FULL_DUPLEX)) {
			status_changed = 1;
			bp->flags &= ~B44_FLAG_FULL_DUPLEX;
		} else if ((phydev->duplex == DUPLEX_FULL) &&
			   !(bp->flags & B44_FLAG_FULL_DUPLEX)) {
			status_changed = 1;
			bp->flags |= B44_FLAG_FULL_DUPLEX;
		}
	}

	if (status_changed) {
		u32 val = br32(bp, B44_TX_CTRL);
		if (bp->flags & B44_FLAG_FULL_DUPLEX)
			val |= TX_CTRL_DUPLEX;
		else
			val &= ~TX_CTRL_DUPLEX;
		bw32(bp, B44_TX_CTRL, val);
		phy_print_status(phydev);
	}
}

static int b44_register_phy_one(struct b44 *bp)
{
	struct mii_bus *mii_bus;
	struct ssb_device *sdev = bp->sdev;
	struct phy_device *phydev;
	char bus_id[MII_BUS_ID_SIZE + 3];
	struct ssb_sprom *sprom = &sdev->bus->sprom;
	int err;

	mii_bus = mdiobus_alloc();
	if (!mii_bus) {
		dev_err(sdev->dev, "mdiobus_alloc() failed\n");
		err = -ENOMEM;
		goto err_out;
	}

	mii_bus->priv = bp;
	mii_bus->read = b44_mdio_read_phylib;
	mii_bus->write = b44_mdio_write_phylib;
	mii_bus->name = "b44_eth_mii";
	mii_bus->parent = sdev->dev;
	mii_bus->phy_mask = ~(1 << bp->phy_addr);
	snprintf(mii_bus->id, MII_BUS_ID_SIZE, "%x", instance);

	bp->mii_bus = mii_bus;

	err = mdiobus_register(mii_bus);
	if (err) {
		dev_err(sdev->dev, "failed to register MII bus\n");
		goto err_out_mdiobus;
	}

	if (!mdiobus_is_registered_device(bp->mii_bus, bp->phy_addr) &&
	    (sprom->boardflags_lo & (B44_BOARDFLAG_ROBO | B44_BOARDFLAG_ADM))) {

		dev_info(sdev->dev,
			 "could not find PHY at %i, use fixed one\n",
			 bp->phy_addr);

		bp->phy_addr = 0;
		snprintf(bus_id, sizeof(bus_id), PHY_ID_FMT, "fixed-0",
			 bp->phy_addr);
	} else {
		snprintf(bus_id, sizeof(bus_id), PHY_ID_FMT, mii_bus->id,
			 bp->phy_addr);
	}

	phydev = phy_connect(bp->dev, bus_id, &b44_adjust_link,
			     PHY_INTERFACE_MODE_MII);
	if (IS_ERR(phydev)) {
		dev_err(sdev->dev, "could not attach PHY at %i\n",
			bp->phy_addr);
		err = PTR_ERR(phydev);
		goto err_out_mdiobus_unregister;
	}

	/* mask with MAC supported features */
	phydev->supported &= (SUPPORTED_100baseT_Half |
			      SUPPORTED_100baseT_Full |
			      SUPPORTED_Autoneg |
			      SUPPORTED_MII);
	phydev->advertising = phydev->supported;

	bp->old_link = 0;
	bp->phy_addr = phydev->mdio.addr;

	phy_attached_info(phydev);

	return 0;

err_out_mdiobus_unregister:
	mdiobus_unregister(mii_bus);

err_out_mdiobus:
	mdiobus_free(mii_bus);

err_out:
	return err;
}

static void b44_unregister_phy_one(struct b44 *bp)
{
	struct net_device *dev = bp->dev;
	struct mii_bus *mii_bus = bp->mii_bus;

	phy_disconnect(dev->phydev);
	mdiobus_unregister(mii_bus);
	mdiobus_free(mii_bus);
}

static int b44_init_one(struct ssb_device *sdev,
			const struct ssb_device_id *ent)
{
	struct net_device *dev;
	struct b44 *bp;
	int err;

	instance++;

	pr_info_once("%s version %s\n", DRV_DESCRIPTION, DRV_MODULE_VERSION);

	dev = alloc_etherdev(sizeof(*bp));
	if (!dev) {
		err = -ENOMEM;
		goto out;
	}

	SET_NETDEV_DEV(dev, sdev->dev);

	/* No interesting netdevice features in this card... */
	dev->features |= 0;

	bp = netdev_priv(dev);
	bp->sdev = sdev;
	bp->dev = dev;
	bp->force_copybreak = 0;

	bp->msg_enable = netif_msg_init(b44_debug, B44_DEF_MSG_ENABLE);

	spin_lock_init(&bp->lock);
	u64_stats_init(&bp->hw_stats.syncp);

	bp->rx_pending = B44_DEF_RX_RING_PENDING;
	bp->tx_pending = B44_DEF_TX_RING_PENDING;

	dev->netdev_ops = &b44_netdev_ops;
	netif_napi_add(dev, &bp->napi, b44_poll, 64);
	dev->watchdog_timeo = B44_TX_TIMEOUT;
	dev->min_mtu = B44_MIN_MTU;
	dev->max_mtu = B44_MAX_MTU;
	dev->irq = sdev->irq;
	dev->ethtool_ops = &b44_ethtool_ops;

	err = ssb_bus_powerup(sdev->bus, 0);
	if (err) {
		dev_err(sdev->dev,
			"Failed to powerup the bus\n");
		goto err_out_free_dev;
	}

	if (dma_set_mask_and_coherent(sdev->dma_dev, DMA_BIT_MASK(30))) {
		dev_err(sdev->dev,
			"Required 30BIT DMA mask unsupported by the system\n");
		goto err_out_powerdown;
	}

	err = b44_get_invariants(bp);
	if (err) {
		dev_err(sdev->dev,
			"Problem fetching invariants of chip, aborting\n");
		goto err_out_powerdown;
	}

	if (bp->phy_addr == B44_PHY_ADDR_NO_PHY) {
		dev_err(sdev->dev, "No PHY present on this MAC, aborting\n");
		err = -ENODEV;
		goto err_out_powerdown;
	}

	bp->mii_if.dev = dev;
	bp->mii_if.mdio_read = b44_mdio_read_mii;
	bp->mii_if.mdio_write = b44_mdio_write_mii;
	bp->mii_if.phy_id = bp->phy_addr;
	bp->mii_if.phy_id_mask = 0x1f;
	bp->mii_if.reg_num_mask = 0x1f;

	/* By default, advertise all speed/duplex settings. */
	bp->flags |= (B44_FLAG_ADV_10HALF | B44_FLAG_ADV_10FULL |
		      B44_FLAG_ADV_100HALF | B44_FLAG_ADV_100FULL);

	/* By default, auto-negotiate PAUSE. */
	bp->flags |= B44_FLAG_PAUSE_AUTO;

	err = register_netdev(dev);
	if (err) {
		dev_err(sdev->dev, "Cannot register net device, aborting\n");
		goto err_out_powerdown;
	}

	netif_carrier_off(dev);

	ssb_set_drvdata(sdev, dev);

	/* Chip reset provides power to the b44 MAC & PCI cores, which
	 * is necessary for MAC register access.
	 */
	b44_chip_reset(bp, B44_CHIP_RESET_FULL);

	/* do a phy reset to test if there is an active phy */
	err = b44_phy_reset(bp);
	if (err < 0) {
		dev_err(sdev->dev, "phy reset failed\n");
		goto err_out_unregister_netdev;
	}

	if (bp->flags & B44_FLAG_EXTERNAL_PHY) {
		err = b44_register_phy_one(bp);
		if (err) {
			dev_err(sdev->dev, "Cannot register PHY, aborting\n");
			goto err_out_unregister_netdev;
		}
	}

	device_set_wakeup_capable(sdev->dev, true);
	netdev_info(dev, "%s %pM\n", DRV_DESCRIPTION, dev->dev_addr);

	return 0;

err_out_unregister_netdev:
	unregister_netdev(dev);
err_out_powerdown:
	ssb_bus_may_powerdown(sdev->bus);

err_out_free_dev:
	netif_napi_del(&bp->napi);
	free_netdev(dev);

out:
	return err;
}

static void b44_remove_one(struct ssb_device *sdev)
{
	struct net_device *dev = ssb_get_drvdata(sdev);
	struct b44 *bp = netdev_priv(dev);

	unregister_netdev(dev);
	if (bp->flags & B44_FLAG_EXTERNAL_PHY)
		b44_unregister_phy_one(bp);
	ssb_device_disable(sdev, 0);
	ssb_bus_may_powerdown(sdev->bus);
	netif_napi_del(&bp->napi);
	free_netdev(dev);
	ssb_pcihost_set_power_state(sdev, PCI_D3hot);
	ssb_set_drvdata(sdev, NULL);
}

static int b44_suspend(struct ssb_device *sdev, pm_message_t state)
{
	struct net_device *dev = ssb_get_drvdata(sdev);
	struct b44 *bp = netdev_priv(dev);

	if (!netif_running(dev))
		return 0;

	del_timer_sync(&bp->timer);

	spin_lock_irq(&bp->lock);

	b44_halt(bp);
	netif_carrier_off(bp->dev);
	netif_device_detach(bp->dev);
	b44_free_rings(bp);

	spin_unlock_irq(&bp->lock);

	free_irq(dev->irq, dev);
	if (bp->flags & B44_FLAG_WOL_ENABLE) {
		b44_init_hw(bp, B44_PARTIAL_RESET);
		b44_setup_wol(bp);
	}

	ssb_pcihost_set_power_state(sdev, PCI_D3hot);
	return 0;
}

static int b44_resume(struct ssb_device *sdev)
{
	struct net_device *dev = ssb_get_drvdata(sdev);
	struct b44 *bp = netdev_priv(dev);
	int rc = 0;

	rc = ssb_bus_powerup(sdev->bus, 0);
	if (rc) {
		dev_err(sdev->dev,
			"Failed to powerup the bus\n");
		return rc;
	}

	if (!netif_running(dev))
		return 0;

	spin_lock_irq(&bp->lock);
	b44_init_rings(bp);
	b44_init_hw(bp, B44_FULL_RESET);
	spin_unlock_irq(&bp->lock);

	/*
	 * As a shared interrupt, the handler can be called immediately. To be
	 * able to check the interrupt status the hardware must already be
	 * powered back on (b44_init_hw).
	 */
	rc = request_irq(dev->irq, b44_interrupt, IRQF_SHARED, dev->name, dev);
	if (rc) {
		netdev_err(dev, "request_irq failed\n");
		spin_lock_irq(&bp->lock);
		b44_halt(bp);
		b44_free_rings(bp);
		spin_unlock_irq(&bp->lock);
		return rc;
	}

	netif_device_attach(bp->dev);

	b44_enable_ints(bp);
	netif_wake_queue(dev);

	mod_timer(&bp->timer, jiffies + 1);

	return 0;
}

static struct ssb_driver b44_ssb_driver = {
	.name		= DRV_MODULE_NAME,
	.id_table	= b44_ssb_tbl,
	.probe		= b44_init_one,
	.remove		= b44_remove_one,
	.suspend	= b44_suspend,
	.resume		= b44_resume,
};

static inline int __init b44_pci_init(void)
{
	int err = 0;
#ifdef CONFIG_B44_PCI
	err = ssb_pcihost_register(&b44_pci_driver);
#endif
	return err;
}

static inline void b44_pci_exit(void)
{
#ifdef CONFIG_B44_PCI
	ssb_pcihost_unregister(&b44_pci_driver);
#endif
}

static int __init b44_init(void)
{
	unsigned int dma_desc_align_size = dma_get_cache_alignment();
	int err;

	/* Setup paramaters for syncing RX/TX DMA descriptors */
	dma_desc_sync_size = max_t(unsigned int, dma_desc_align_size, sizeof(struct dma_desc));

	err = b44_pci_init();
	if (err)
		return err;
	err = ssb_driver_register(&b44_ssb_driver);
	if (err)
		b44_pci_exit();
	return err;
}

static void __exit b44_cleanup(void)
{
	ssb_driver_unregister(&b44_ssb_driver);
	b44_pci_exit();
}

module_init(b44_init);
module_exit(b44_cleanup);

