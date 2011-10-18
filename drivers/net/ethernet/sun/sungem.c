/* $Id: sungem.c,v 1.44.2.22 2002/03/13 01:18:12 davem Exp $
 * sungem.c: Sun GEM ethernet driver.
 *
 * Copyright (C) 2000, 2001, 2002, 2003 David S. Miller (davem@redhat.com)
 *
 * Support for Apple GMAC and assorted PHYs, WOL, Power Management
 * (C) 2001,2002,2003 Benjamin Herrenscmidt (benh@kernel.crashing.org)
 * (C) 2004,2005 Benjamin Herrenscmidt, IBM Corp.
 *
 * NAPI and NETPOLL support
 * (C) 2004 by Eric Lemoine (eric.lemoine@gmail.com)
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/crc32.h>
#include <linux/random.h>
#include <linux/workqueue.h>
#include <linux/if_vlan.h>
#include <linux/bitops.h>
#include <linux/mm.h>
#include <linux/gfp.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/byteorder.h>
#include <asm/uaccess.h>
#include <asm/irq.h>

#ifdef CONFIG_SPARC
#include <asm/idprom.h>
#include <asm/prom.h>
#endif

#ifdef CONFIG_PPC_PMAC
#include <asm/pci-bridge.h>
#include <asm/prom.h>
#include <asm/machdep.h>
#include <asm/pmac_feature.h>
#endif

#include <linux/sungem_phy.h>
#include "sungem.h"

/* Stripping FCS is causing problems, disabled for now */
#undef STRIP_FCS

#define DEFAULT_MSG	(NETIF_MSG_DRV		| \
			 NETIF_MSG_PROBE	| \
			 NETIF_MSG_LINK)

#define ADVERTISE_MASK	(SUPPORTED_10baseT_Half | SUPPORTED_10baseT_Full | \
			 SUPPORTED_100baseT_Half | SUPPORTED_100baseT_Full | \
			 SUPPORTED_1000baseT_Half | SUPPORTED_1000baseT_Full | \
			 SUPPORTED_Pause | SUPPORTED_Autoneg)

#define DRV_NAME	"sungem"
#define DRV_VERSION	"1.0"
#define DRV_AUTHOR	"David S. Miller <davem@redhat.com>"

static char version[] __devinitdata =
        DRV_NAME ".c:v" DRV_VERSION " " DRV_AUTHOR "\n";

MODULE_AUTHOR(DRV_AUTHOR);
MODULE_DESCRIPTION("Sun GEM Gbit ethernet driver");
MODULE_LICENSE("GPL");

#define GEM_MODULE_NAME	"gem"

static DEFINE_PCI_DEVICE_TABLE(gem_pci_tbl) = {
	{ PCI_VENDOR_ID_SUN, PCI_DEVICE_ID_SUN_GEM,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0UL },

	/* These models only differ from the original GEM in
	 * that their tx/rx fifos are of a different size and
	 * they only support 10/100 speeds. -DaveM
	 *
	 * Apple's GMAC does support gigabit on machines with
	 * the BCM54xx PHYs. -BenH
	 */
	{ PCI_VENDOR_ID_SUN, PCI_DEVICE_ID_SUN_RIO_GEM,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0UL },
	{ PCI_VENDOR_ID_APPLE, PCI_DEVICE_ID_APPLE_UNI_N_GMAC,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0UL },
	{ PCI_VENDOR_ID_APPLE, PCI_DEVICE_ID_APPLE_UNI_N_GMACP,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0UL },
	{ PCI_VENDOR_ID_APPLE, PCI_DEVICE_ID_APPLE_UNI_N_GMAC2,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0UL },
	{ PCI_VENDOR_ID_APPLE, PCI_DEVICE_ID_APPLE_K2_GMAC,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0UL },
	{ PCI_VENDOR_ID_APPLE, PCI_DEVICE_ID_APPLE_SH_SUNGEM,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0UL },
	{ PCI_VENDOR_ID_APPLE, PCI_DEVICE_ID_APPLE_IPID2_GMAC,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0UL },
	{0, }
};

MODULE_DEVICE_TABLE(pci, gem_pci_tbl);

static u16 __phy_read(struct gem *gp, int phy_addr, int reg)
{
	u32 cmd;
	int limit = 10000;

	cmd  = (1 << 30);
	cmd |= (2 << 28);
	cmd |= (phy_addr << 23) & MIF_FRAME_PHYAD;
	cmd |= (reg << 18) & MIF_FRAME_REGAD;
	cmd |= (MIF_FRAME_TAMSB);
	writel(cmd, gp->regs + MIF_FRAME);

	while (--limit) {
		cmd = readl(gp->regs + MIF_FRAME);
		if (cmd & MIF_FRAME_TALSB)
			break;

		udelay(10);
	}

	if (!limit)
		cmd = 0xffff;

	return cmd & MIF_FRAME_DATA;
}

static inline int _phy_read(struct net_device *dev, int mii_id, int reg)
{
	struct gem *gp = netdev_priv(dev);
	return __phy_read(gp, mii_id, reg);
}

static inline u16 phy_read(struct gem *gp, int reg)
{
	return __phy_read(gp, gp->mii_phy_addr, reg);
}

static void __phy_write(struct gem *gp, int phy_addr, int reg, u16 val)
{
	u32 cmd;
	int limit = 10000;

	cmd  = (1 << 30);
	cmd |= (1 << 28);
	cmd |= (phy_addr << 23) & MIF_FRAME_PHYAD;
	cmd |= (reg << 18) & MIF_FRAME_REGAD;
	cmd |= (MIF_FRAME_TAMSB);
	cmd |= (val & MIF_FRAME_DATA);
	writel(cmd, gp->regs + MIF_FRAME);

	while (limit--) {
		cmd = readl(gp->regs + MIF_FRAME);
		if (cmd & MIF_FRAME_TALSB)
			break;

		udelay(10);
	}
}

static inline void _phy_write(struct net_device *dev, int mii_id, int reg, int val)
{
	struct gem *gp = netdev_priv(dev);
	__phy_write(gp, mii_id, reg, val & 0xffff);
}

static inline void phy_write(struct gem *gp, int reg, u16 val)
{
	__phy_write(gp, gp->mii_phy_addr, reg, val);
}

static inline void gem_enable_ints(struct gem *gp)
{
	/* Enable all interrupts but TXDONE */
	writel(GREG_STAT_TXDONE, gp->regs + GREG_IMASK);
}

static inline void gem_disable_ints(struct gem *gp)
{
	/* Disable all interrupts, including TXDONE */
	writel(GREG_STAT_NAPI | GREG_STAT_TXDONE, gp->regs + GREG_IMASK);
	(void)readl(gp->regs + GREG_IMASK); /* write posting */
}

static void gem_get_cell(struct gem *gp)
{
	BUG_ON(gp->cell_enabled < 0);
	gp->cell_enabled++;
#ifdef CONFIG_PPC_PMAC
	if (gp->cell_enabled == 1) {
		mb();
		pmac_call_feature(PMAC_FTR_GMAC_ENABLE, gp->of_node, 0, 1);
		udelay(10);
	}
#endif /* CONFIG_PPC_PMAC */
}

/* Turn off the chip's clock */
static void gem_put_cell(struct gem *gp)
{
	BUG_ON(gp->cell_enabled <= 0);
	gp->cell_enabled--;
#ifdef CONFIG_PPC_PMAC
	if (gp->cell_enabled == 0) {
		mb();
		pmac_call_feature(PMAC_FTR_GMAC_ENABLE, gp->of_node, 0, 0);
		udelay(10);
	}
#endif /* CONFIG_PPC_PMAC */
}

static inline void gem_netif_stop(struct gem *gp)
{
	gp->dev->trans_start = jiffies;	/* prevent tx timeout */
	napi_disable(&gp->napi);
	netif_tx_disable(gp->dev);
}

static inline void gem_netif_start(struct gem *gp)
{
	/* NOTE: unconditional netif_wake_queue is only
	 * appropriate so long as all callers are assured to
	 * have free tx slots.
	 */
	netif_wake_queue(gp->dev);
	napi_enable(&gp->napi);
}

static void gem_schedule_reset(struct gem *gp)
{
	gp->reset_task_pending = 1;
	schedule_work(&gp->reset_task);
}

static void gem_handle_mif_event(struct gem *gp, u32 reg_val, u32 changed_bits)
{
	if (netif_msg_intr(gp))
		printk(KERN_DEBUG "%s: mif interrupt\n", gp->dev->name);
}

static int gem_pcs_interrupt(struct net_device *dev, struct gem *gp, u32 gem_status)
{
	u32 pcs_istat = readl(gp->regs + PCS_ISTAT);
	u32 pcs_miistat;

	if (netif_msg_intr(gp))
		printk(KERN_DEBUG "%s: pcs interrupt, pcs_istat: 0x%x\n",
			gp->dev->name, pcs_istat);

	if (!(pcs_istat & PCS_ISTAT_LSC)) {
		netdev_err(dev, "PCS irq but no link status change???\n");
		return 0;
	}

	/* The link status bit latches on zero, so you must
	 * read it twice in such a case to see a transition
	 * to the link being up.
	 */
	pcs_miistat = readl(gp->regs + PCS_MIISTAT);
	if (!(pcs_miistat & PCS_MIISTAT_LS))
		pcs_miistat |=
			(readl(gp->regs + PCS_MIISTAT) &
			 PCS_MIISTAT_LS);

	if (pcs_miistat & PCS_MIISTAT_ANC) {
		/* The remote-fault indication is only valid
		 * when autoneg has completed.
		 */
		if (pcs_miistat & PCS_MIISTAT_RF)
			netdev_info(dev, "PCS AutoNEG complete, RemoteFault\n");
		else
			netdev_info(dev, "PCS AutoNEG complete\n");
	}

	if (pcs_miistat & PCS_MIISTAT_LS) {
		netdev_info(dev, "PCS link is now up\n");
		netif_carrier_on(gp->dev);
	} else {
		netdev_info(dev, "PCS link is now down\n");
		netif_carrier_off(gp->dev);
		/* If this happens and the link timer is not running,
		 * reset so we re-negotiate.
		 */
		if (!timer_pending(&gp->link_timer))
			return 1;
	}

	return 0;
}

static int gem_txmac_interrupt(struct net_device *dev, struct gem *gp, u32 gem_status)
{
	u32 txmac_stat = readl(gp->regs + MAC_TXSTAT);

	if (netif_msg_intr(gp))
		printk(KERN_DEBUG "%s: txmac interrupt, txmac_stat: 0x%x\n",
			gp->dev->name, txmac_stat);

	/* Defer timer expiration is quite normal,
	 * don't even log the event.
	 */
	if ((txmac_stat & MAC_TXSTAT_DTE) &&
	    !(txmac_stat & ~MAC_TXSTAT_DTE))
		return 0;

	if (txmac_stat & MAC_TXSTAT_URUN) {
		netdev_err(dev, "TX MAC xmit underrun\n");
		dev->stats.tx_fifo_errors++;
	}

	if (txmac_stat & MAC_TXSTAT_MPE) {
		netdev_err(dev, "TX MAC max packet size error\n");
		dev->stats.tx_errors++;
	}

	/* The rest are all cases of one of the 16-bit TX
	 * counters expiring.
	 */
	if (txmac_stat & MAC_TXSTAT_NCE)
		dev->stats.collisions += 0x10000;

	if (txmac_stat & MAC_TXSTAT_ECE) {
		dev->stats.tx_aborted_errors += 0x10000;
		dev->stats.collisions += 0x10000;
	}

	if (txmac_stat & MAC_TXSTAT_LCE) {
		dev->stats.tx_aborted_errors += 0x10000;
		dev->stats.collisions += 0x10000;
	}

	/* We do not keep track of MAC_TXSTAT_FCE and
	 * MAC_TXSTAT_PCE events.
	 */
	return 0;
}

/* When we get a RX fifo overflow, the RX unit in GEM is probably hung
 * so we do the following.
 *
 * If any part of the reset goes wrong, we return 1 and that causes the
 * whole chip to be reset.
 */
static int gem_rxmac_reset(struct gem *gp)
{
	struct net_device *dev = gp->dev;
	int limit, i;
	u64 desc_dma;
	u32 val;

	/* First, reset & disable MAC RX. */
	writel(MAC_RXRST_CMD, gp->regs + MAC_RXRST);
	for (limit = 0; limit < 5000; limit++) {
		if (!(readl(gp->regs + MAC_RXRST) & MAC_RXRST_CMD))
			break;
		udelay(10);
	}
	if (limit == 5000) {
		netdev_err(dev, "RX MAC will not reset, resetting whole chip\n");
		return 1;
	}

	writel(gp->mac_rx_cfg & ~MAC_RXCFG_ENAB,
	       gp->regs + MAC_RXCFG);
	for (limit = 0; limit < 5000; limit++) {
		if (!(readl(gp->regs + MAC_RXCFG) & MAC_RXCFG_ENAB))
			break;
		udelay(10);
	}
	if (limit == 5000) {
		netdev_err(dev, "RX MAC will not disable, resetting whole chip\n");
		return 1;
	}

	/* Second, disable RX DMA. */
	writel(0, gp->regs + RXDMA_CFG);
	for (limit = 0; limit < 5000; limit++) {
		if (!(readl(gp->regs + RXDMA_CFG) & RXDMA_CFG_ENABLE))
			break;
		udelay(10);
	}
	if (limit == 5000) {
		netdev_err(dev, "RX DMA will not disable, resetting whole chip\n");
		return 1;
	}

	udelay(5000);

	/* Execute RX reset command. */
	writel(gp->swrst_base | GREG_SWRST_RXRST,
	       gp->regs + GREG_SWRST);
	for (limit = 0; limit < 5000; limit++) {
		if (!(readl(gp->regs + GREG_SWRST) & GREG_SWRST_RXRST))
			break;
		udelay(10);
	}
	if (limit == 5000) {
		netdev_err(dev, "RX reset command will not execute, resetting whole chip\n");
		return 1;
	}

	/* Refresh the RX ring. */
	for (i = 0; i < RX_RING_SIZE; i++) {
		struct gem_rxd *rxd = &gp->init_block->rxd[i];

		if (gp->rx_skbs[i] == NULL) {
			netdev_err(dev, "Parts of RX ring empty, resetting whole chip\n");
			return 1;
		}

		rxd->status_word = cpu_to_le64(RXDCTRL_FRESH(gp));
	}
	gp->rx_new = gp->rx_old = 0;

	/* Now we must reprogram the rest of RX unit. */
	desc_dma = (u64) gp->gblock_dvma;
	desc_dma += (INIT_BLOCK_TX_RING_SIZE * sizeof(struct gem_txd));
	writel(desc_dma >> 32, gp->regs + RXDMA_DBHI);
	writel(desc_dma & 0xffffffff, gp->regs + RXDMA_DBLOW);
	writel(RX_RING_SIZE - 4, gp->regs + RXDMA_KICK);
	val = (RXDMA_CFG_BASE | (RX_OFFSET << 10) |
	       ((14 / 2) << 13) | RXDMA_CFG_FTHRESH_128);
	writel(val, gp->regs + RXDMA_CFG);
	if (readl(gp->regs + GREG_BIFCFG) & GREG_BIFCFG_M66EN)
		writel(((5 & RXDMA_BLANK_IPKTS) |
			((8 << 12) & RXDMA_BLANK_ITIME)),
		       gp->regs + RXDMA_BLANK);
	else
		writel(((5 & RXDMA_BLANK_IPKTS) |
			((4 << 12) & RXDMA_BLANK_ITIME)),
		       gp->regs + RXDMA_BLANK);
	val  = (((gp->rx_pause_off / 64) << 0) & RXDMA_PTHRESH_OFF);
	val |= (((gp->rx_pause_on / 64) << 12) & RXDMA_PTHRESH_ON);
	writel(val, gp->regs + RXDMA_PTHRESH);
	val = readl(gp->regs + RXDMA_CFG);
	writel(val | RXDMA_CFG_ENABLE, gp->regs + RXDMA_CFG);
	writel(MAC_RXSTAT_RCV, gp->regs + MAC_RXMASK);
	val = readl(gp->regs + MAC_RXCFG);
	writel(val | MAC_RXCFG_ENAB, gp->regs + MAC_RXCFG);

	return 0;
}

static int gem_rxmac_interrupt(struct net_device *dev, struct gem *gp, u32 gem_status)
{
	u32 rxmac_stat = readl(gp->regs + MAC_RXSTAT);
	int ret = 0;

	if (netif_msg_intr(gp))
		printk(KERN_DEBUG "%s: rxmac interrupt, rxmac_stat: 0x%x\n",
			gp->dev->name, rxmac_stat);

	if (rxmac_stat & MAC_RXSTAT_OFLW) {
		u32 smac = readl(gp->regs + MAC_SMACHINE);

		netdev_err(dev, "RX MAC fifo overflow smac[%08x]\n", smac);
		dev->stats.rx_over_errors++;
		dev->stats.rx_fifo_errors++;

		ret = gem_rxmac_reset(gp);
	}

	if (rxmac_stat & MAC_RXSTAT_ACE)
		dev->stats.rx_frame_errors += 0x10000;

	if (rxmac_stat & MAC_RXSTAT_CCE)
		dev->stats.rx_crc_errors += 0x10000;

	if (rxmac_stat & MAC_RXSTAT_LCE)
		dev->stats.rx_length_errors += 0x10000;

	/* We do not track MAC_RXSTAT_FCE and MAC_RXSTAT_VCE
	 * events.
	 */
	return ret;
}

static int gem_mac_interrupt(struct net_device *dev, struct gem *gp, u32 gem_status)
{
	u32 mac_cstat = readl(gp->regs + MAC_CSTAT);

	if (netif_msg_intr(gp))
		printk(KERN_DEBUG "%s: mac interrupt, mac_cstat: 0x%x\n",
			gp->dev->name, mac_cstat);

	/* This interrupt is just for pause frame and pause
	 * tracking.  It is useful for diagnostics and debug
	 * but probably by default we will mask these events.
	 */
	if (mac_cstat & MAC_CSTAT_PS)
		gp->pause_entered++;

	if (mac_cstat & MAC_CSTAT_PRCV)
		gp->pause_last_time_recvd = (mac_cstat >> 16);

	return 0;
}

static int gem_mif_interrupt(struct net_device *dev, struct gem *gp, u32 gem_status)
{
	u32 mif_status = readl(gp->regs + MIF_STATUS);
	u32 reg_val, changed_bits;

	reg_val = (mif_status & MIF_STATUS_DATA) >> 16;
	changed_bits = (mif_status & MIF_STATUS_STAT);

	gem_handle_mif_event(gp, reg_val, changed_bits);

	return 0;
}

static int gem_pci_interrupt(struct net_device *dev, struct gem *gp, u32 gem_status)
{
	u32 pci_estat = readl(gp->regs + GREG_PCIESTAT);

	if (gp->pdev->vendor == PCI_VENDOR_ID_SUN &&
	    gp->pdev->device == PCI_DEVICE_ID_SUN_GEM) {
		netdev_err(dev, "PCI error [%04x]", pci_estat);

		if (pci_estat & GREG_PCIESTAT_BADACK)
			pr_cont(" <No ACK64# during ABS64 cycle>");
		if (pci_estat & GREG_PCIESTAT_DTRTO)
			pr_cont(" <Delayed transaction timeout>");
		if (pci_estat & GREG_PCIESTAT_OTHER)
			pr_cont(" <other>");
		pr_cont("\n");
	} else {
		pci_estat |= GREG_PCIESTAT_OTHER;
		netdev_err(dev, "PCI error\n");
	}

	if (pci_estat & GREG_PCIESTAT_OTHER) {
		u16 pci_cfg_stat;

		/* Interrogate PCI config space for the
		 * true cause.
		 */
		pci_read_config_word(gp->pdev, PCI_STATUS,
				     &pci_cfg_stat);
		netdev_err(dev, "Read PCI cfg space status [%04x]\n",
			   pci_cfg_stat);
		if (pci_cfg_stat & PCI_STATUS_PARITY)
			netdev_err(dev, "PCI parity error detected\n");
		if (pci_cfg_stat & PCI_STATUS_SIG_TARGET_ABORT)
			netdev_err(dev, "PCI target abort\n");
		if (pci_cfg_stat & PCI_STATUS_REC_TARGET_ABORT)
			netdev_err(dev, "PCI master acks target abort\n");
		if (pci_cfg_stat & PCI_STATUS_REC_MASTER_ABORT)
			netdev_err(dev, "PCI master abort\n");
		if (pci_cfg_stat & PCI_STATUS_SIG_SYSTEM_ERROR)
			netdev_err(dev, "PCI system error SERR#\n");
		if (pci_cfg_stat & PCI_STATUS_DETECTED_PARITY)
			netdev_err(dev, "PCI parity error\n");

		/* Write the error bits back to clear them. */
		pci_cfg_stat &= (PCI_STATUS_PARITY |
				 PCI_STATUS_SIG_TARGET_ABORT |
				 PCI_STATUS_REC_TARGET_ABORT |
				 PCI_STATUS_REC_MASTER_ABORT |
				 PCI_STATUS_SIG_SYSTEM_ERROR |
				 PCI_STATUS_DETECTED_PARITY);
		pci_write_config_word(gp->pdev,
				      PCI_STATUS, pci_cfg_stat);
	}

	/* For all PCI errors, we should reset the chip. */
	return 1;
}

/* All non-normal interrupt conditions get serviced here.
 * Returns non-zero if we should just exit the interrupt
 * handler right now (ie. if we reset the card which invalidates
 * all of the other original irq status bits).
 */
static int gem_abnormal_irq(struct net_device *dev, struct gem *gp, u32 gem_status)
{
	if (gem_status & GREG_STAT_RXNOBUF) {
		/* Frame arrived, no free RX buffers available. */
		if (netif_msg_rx_err(gp))
			printk(KERN_DEBUG "%s: no buffer for rx frame\n",
				gp->dev->name);
		dev->stats.rx_dropped++;
	}

	if (gem_status & GREG_STAT_RXTAGERR) {
		/* corrupt RX tag framing */
		if (netif_msg_rx_err(gp))
			printk(KERN_DEBUG "%s: corrupt rx tag framing\n",
				gp->dev->name);
		dev->stats.rx_errors++;

		return 1;
	}

	if (gem_status & GREG_STAT_PCS) {
		if (gem_pcs_interrupt(dev, gp, gem_status))
			return 1;
	}

	if (gem_status & GREG_STAT_TXMAC) {
		if (gem_txmac_interrupt(dev, gp, gem_status))
			return 1;
	}

	if (gem_status & GREG_STAT_RXMAC) {
		if (gem_rxmac_interrupt(dev, gp, gem_status))
			return 1;
	}

	if (gem_status & GREG_STAT_MAC) {
		if (gem_mac_interrupt(dev, gp, gem_status))
			return 1;
	}

	if (gem_status & GREG_STAT_MIF) {
		if (gem_mif_interrupt(dev, gp, gem_status))
			return 1;
	}

	if (gem_status & GREG_STAT_PCIERR) {
		if (gem_pci_interrupt(dev, gp, gem_status))
			return 1;
	}

	return 0;
}

static __inline__ void gem_tx(struct net_device *dev, struct gem *gp, u32 gem_status)
{
	int entry, limit;

	entry = gp->tx_old;
	limit = ((gem_status & GREG_STAT_TXNR) >> GREG_STAT_TXNR_SHIFT);
	while (entry != limit) {
		struct sk_buff *skb;
		struct gem_txd *txd;
		dma_addr_t dma_addr;
		u32 dma_len;
		int frag;

		if (netif_msg_tx_done(gp))
			printk(KERN_DEBUG "%s: tx done, slot %d\n",
				gp->dev->name, entry);
		skb = gp->tx_skbs[entry];
		if (skb_shinfo(skb)->nr_frags) {
			int last = entry + skb_shinfo(skb)->nr_frags;
			int walk = entry;
			int incomplete = 0;

			last &= (TX_RING_SIZE - 1);
			for (;;) {
				walk = NEXT_TX(walk);
				if (walk == limit)
					incomplete = 1;
				if (walk == last)
					break;
			}
			if (incomplete)
				break;
		}
		gp->tx_skbs[entry] = NULL;
		dev->stats.tx_bytes += skb->len;

		for (frag = 0; frag <= skb_shinfo(skb)->nr_frags; frag++) {
			txd = &gp->init_block->txd[entry];

			dma_addr = le64_to_cpu(txd->buffer);
			dma_len = le64_to_cpu(txd->control_word) & TXDCTRL_BUFSZ;

			pci_unmap_page(gp->pdev, dma_addr, dma_len, PCI_DMA_TODEVICE);
			entry = NEXT_TX(entry);
		}

		dev->stats.tx_packets++;
		dev_kfree_skb(skb);
	}
	gp->tx_old = entry;

	/* Need to make the tx_old update visible to gem_start_xmit()
	 * before checking for netif_queue_stopped().  Without the
	 * memory barrier, there is a small possibility that gem_start_xmit()
	 * will miss it and cause the queue to be stopped forever.
	 */
	smp_mb();

	if (unlikely(netif_queue_stopped(dev) &&
		     TX_BUFFS_AVAIL(gp) > (MAX_SKB_FRAGS + 1))) {
		struct netdev_queue *txq = netdev_get_tx_queue(dev, 0);

		__netif_tx_lock(txq, smp_processor_id());
		if (netif_queue_stopped(dev) &&
		    TX_BUFFS_AVAIL(gp) > (MAX_SKB_FRAGS + 1))
			netif_wake_queue(dev);
		__netif_tx_unlock(txq);
	}
}

static __inline__ void gem_post_rxds(struct gem *gp, int limit)
{
	int cluster_start, curr, count, kick;

	cluster_start = curr = (gp->rx_new & ~(4 - 1));
	count = 0;
	kick = -1;
	wmb();
	while (curr != limit) {
		curr = NEXT_RX(curr);
		if (++count == 4) {
			struct gem_rxd *rxd =
				&gp->init_block->rxd[cluster_start];
			for (;;) {
				rxd->status_word = cpu_to_le64(RXDCTRL_FRESH(gp));
				rxd++;
				cluster_start = NEXT_RX(cluster_start);
				if (cluster_start == curr)
					break;
			}
			kick = curr;
			count = 0;
		}
	}
	if (kick >= 0) {
		mb();
		writel(kick, gp->regs + RXDMA_KICK);
	}
}

#define ALIGNED_RX_SKB_ADDR(addr) \
        ((((unsigned long)(addr) + (64UL - 1UL)) & ~(64UL - 1UL)) - (unsigned long)(addr))
static __inline__ struct sk_buff *gem_alloc_skb(struct net_device *dev, int size,
						gfp_t gfp_flags)
{
	struct sk_buff *skb = alloc_skb(size + 64, gfp_flags);

	if (likely(skb)) {
		unsigned long offset = ALIGNED_RX_SKB_ADDR(skb->data);
		skb_reserve(skb, offset);
		skb->dev = dev;
	}
	return skb;
}

static int gem_rx(struct gem *gp, int work_to_do)
{
	struct net_device *dev = gp->dev;
	int entry, drops, work_done = 0;
	u32 done;
	__sum16 csum;

	if (netif_msg_rx_status(gp))
		printk(KERN_DEBUG "%s: rx interrupt, done: %d, rx_new: %d\n",
			gp->dev->name, readl(gp->regs + RXDMA_DONE), gp->rx_new);

	entry = gp->rx_new;
	drops = 0;
	done = readl(gp->regs + RXDMA_DONE);
	for (;;) {
		struct gem_rxd *rxd = &gp->init_block->rxd[entry];
		struct sk_buff *skb;
		u64 status = le64_to_cpu(rxd->status_word);
		dma_addr_t dma_addr;
		int len;

		if ((status & RXDCTRL_OWN) != 0)
			break;

		if (work_done >= RX_RING_SIZE || work_done >= work_to_do)
			break;

		/* When writing back RX descriptor, GEM writes status
		 * then buffer address, possibly in separate transactions.
		 * If we don't wait for the chip to write both, we could
		 * post a new buffer to this descriptor then have GEM spam
		 * on the buffer address.  We sync on the RX completion
		 * register to prevent this from happening.
		 */
		if (entry == done) {
			done = readl(gp->regs + RXDMA_DONE);
			if (entry == done)
				break;
		}

		/* We can now account for the work we're about to do */
		work_done++;

		skb = gp->rx_skbs[entry];

		len = (status & RXDCTRL_BUFSZ) >> 16;
		if ((len < ETH_ZLEN) || (status & RXDCTRL_BAD)) {
			dev->stats.rx_errors++;
			if (len < ETH_ZLEN)
				dev->stats.rx_length_errors++;
			if (len & RXDCTRL_BAD)
				dev->stats.rx_crc_errors++;

			/* We'll just return it to GEM. */
		drop_it:
			dev->stats.rx_dropped++;
			goto next;
		}

		dma_addr = le64_to_cpu(rxd->buffer);
		if (len > RX_COPY_THRESHOLD) {
			struct sk_buff *new_skb;

			new_skb = gem_alloc_skb(dev, RX_BUF_ALLOC_SIZE(gp), GFP_ATOMIC);
			if (new_skb == NULL) {
				drops++;
				goto drop_it;
			}
			pci_unmap_page(gp->pdev, dma_addr,
				       RX_BUF_ALLOC_SIZE(gp),
				       PCI_DMA_FROMDEVICE);
			gp->rx_skbs[entry] = new_skb;
			skb_put(new_skb, (gp->rx_buf_sz + RX_OFFSET));
			rxd->buffer = cpu_to_le64(pci_map_page(gp->pdev,
							       virt_to_page(new_skb->data),
							       offset_in_page(new_skb->data),
							       RX_BUF_ALLOC_SIZE(gp),
							       PCI_DMA_FROMDEVICE));
			skb_reserve(new_skb, RX_OFFSET);

			/* Trim the original skb for the netif. */
			skb_trim(skb, len);
		} else {
			struct sk_buff *copy_skb = netdev_alloc_skb(dev, len + 2);

			if (copy_skb == NULL) {
				drops++;
				goto drop_it;
			}

			skb_reserve(copy_skb, 2);
			skb_put(copy_skb, len);
			pci_dma_sync_single_for_cpu(gp->pdev, dma_addr, len, PCI_DMA_FROMDEVICE);
			skb_copy_from_linear_data(skb, copy_skb->data, len);
			pci_dma_sync_single_for_device(gp->pdev, dma_addr, len, PCI_DMA_FROMDEVICE);

			/* We'll reuse the original ring buffer. */
			skb = copy_skb;
		}

		csum = (__force __sum16)htons((status & RXDCTRL_TCPCSUM) ^ 0xffff);
		skb->csum = csum_unfold(csum);
		skb->ip_summed = CHECKSUM_COMPLETE;
		skb->protocol = eth_type_trans(skb, gp->dev);

		napi_gro_receive(&gp->napi, skb);

		dev->stats.rx_packets++;
		dev->stats.rx_bytes += len;

	next:
		entry = NEXT_RX(entry);
	}

	gem_post_rxds(gp, entry);

	gp->rx_new = entry;

	if (drops)
		netdev_info(gp->dev, "Memory squeeze, deferring packet\n");

	return work_done;
}

static int gem_poll(struct napi_struct *napi, int budget)
{
	struct gem *gp = container_of(napi, struct gem, napi);
	struct net_device *dev = gp->dev;
	int work_done;

	work_done = 0;
	do {
		/* Handle anomalies */
		if (unlikely(gp->status & GREG_STAT_ABNORMAL)) {
			struct netdev_queue *txq = netdev_get_tx_queue(dev, 0);
			int reset;

			/* We run the abnormal interrupt handling code with
			 * the Tx lock. It only resets the Rx portion of the
			 * chip, but we need to guard it against DMA being
			 * restarted by the link poll timer
			 */
			__netif_tx_lock(txq, smp_processor_id());
			reset = gem_abnormal_irq(dev, gp, gp->status);
			__netif_tx_unlock(txq);
			if (reset) {
				gem_schedule_reset(gp);
				napi_complete(napi);
				return work_done;
			}
		}

		/* Run TX completion thread */
		gem_tx(dev, gp, gp->status);

		/* Run RX thread. We don't use any locking here,
		 * code willing to do bad things - like cleaning the
		 * rx ring - must call napi_disable(), which
		 * schedule_timeout()'s if polling is already disabled.
		 */
		work_done += gem_rx(gp, budget - work_done);

		if (work_done >= budget)
			return work_done;

		gp->status = readl(gp->regs + GREG_STAT);
	} while (gp->status & GREG_STAT_NAPI);

	napi_complete(napi);
	gem_enable_ints(gp);

	return work_done;
}

static irqreturn_t gem_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct gem *gp = netdev_priv(dev);

	if (napi_schedule_prep(&gp->napi)) {
		u32 gem_status = readl(gp->regs + GREG_STAT);

		if (unlikely(gem_status == 0)) {
			napi_enable(&gp->napi);
			return IRQ_NONE;
		}
		if (netif_msg_intr(gp))
			printk(KERN_DEBUG "%s: gem_interrupt() gem_status: 0x%x\n",
			       gp->dev->name, gem_status);

		gp->status = gem_status;
		gem_disable_ints(gp);
		__napi_schedule(&gp->napi);
	}

	/* If polling was disabled at the time we received that
	 * interrupt, we may return IRQ_HANDLED here while we
	 * should return IRQ_NONE. No big deal...
	 */
	return IRQ_HANDLED;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void gem_poll_controller(struct net_device *dev)
{
	struct gem *gp = netdev_priv(dev);

	disable_irq(gp->pdev->irq);
	gem_interrupt(gp->pdev->irq, dev);
	enable_irq(gp->pdev->irq);
}
#endif

static void gem_tx_timeout(struct net_device *dev)
{
	struct gem *gp = netdev_priv(dev);

	netdev_err(dev, "transmit timed out, resetting\n");

	netdev_err(dev, "TX_STATE[%08x:%08x:%08x]\n",
		   readl(gp->regs + TXDMA_CFG),
		   readl(gp->regs + MAC_TXSTAT),
		   readl(gp->regs + MAC_TXCFG));
	netdev_err(dev, "RX_STATE[%08x:%08x:%08x]\n",
		   readl(gp->regs + RXDMA_CFG),
		   readl(gp->regs + MAC_RXSTAT),
		   readl(gp->regs + MAC_RXCFG));

	gem_schedule_reset(gp);
}

static __inline__ int gem_intme(int entry)
{
	/* Algorithm: IRQ every 1/2 of descriptors. */
	if (!(entry & ((TX_RING_SIZE>>1)-1)))
		return 1;

	return 0;
}

static netdev_tx_t gem_start_xmit(struct sk_buff *skb,
				  struct net_device *dev)
{
	struct gem *gp = netdev_priv(dev);
	int entry;
	u64 ctrl;

	ctrl = 0;
	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		const u64 csum_start_off = skb_checksum_start_offset(skb);
		const u64 csum_stuff_off = csum_start_off + skb->csum_offset;

		ctrl = (TXDCTRL_CENAB |
			(csum_start_off << 15) |
			(csum_stuff_off << 21));
	}

	if (unlikely(TX_BUFFS_AVAIL(gp) <= (skb_shinfo(skb)->nr_frags + 1))) {
		/* This is a hard error, log it. */
		if (!netif_queue_stopped(dev)) {
			netif_stop_queue(dev);
			netdev_err(dev, "BUG! Tx Ring full when queue awake!\n");
		}
		return NETDEV_TX_BUSY;
	}

	entry = gp->tx_new;
	gp->tx_skbs[entry] = skb;

	if (skb_shinfo(skb)->nr_frags == 0) {
		struct gem_txd *txd = &gp->init_block->txd[entry];
		dma_addr_t mapping;
		u32 len;

		len = skb->len;
		mapping = pci_map_page(gp->pdev,
				       virt_to_page(skb->data),
				       offset_in_page(skb->data),
				       len, PCI_DMA_TODEVICE);
		ctrl |= TXDCTRL_SOF | TXDCTRL_EOF | len;
		if (gem_intme(entry))
			ctrl |= TXDCTRL_INTME;
		txd->buffer = cpu_to_le64(mapping);
		wmb();
		txd->control_word = cpu_to_le64(ctrl);
		entry = NEXT_TX(entry);
	} else {
		struct gem_txd *txd;
		u32 first_len;
		u64 intme;
		dma_addr_t first_mapping;
		int frag, first_entry = entry;

		intme = 0;
		if (gem_intme(entry))
			intme |= TXDCTRL_INTME;

		/* We must give this initial chunk to the device last.
		 * Otherwise we could race with the device.
		 */
		first_len = skb_headlen(skb);
		first_mapping = pci_map_page(gp->pdev, virt_to_page(skb->data),
					     offset_in_page(skb->data),
					     first_len, PCI_DMA_TODEVICE);
		entry = NEXT_TX(entry);

		for (frag = 0; frag < skb_shinfo(skb)->nr_frags; frag++) {
			const skb_frag_t *this_frag = &skb_shinfo(skb)->frags[frag];
			u32 len;
			dma_addr_t mapping;
			u64 this_ctrl;

			len = skb_frag_size(this_frag);
			mapping = skb_frag_dma_map(&gp->pdev->dev, this_frag,
						   0, len, DMA_TO_DEVICE);
			this_ctrl = ctrl;
			if (frag == skb_shinfo(skb)->nr_frags - 1)
				this_ctrl |= TXDCTRL_EOF;

			txd = &gp->init_block->txd[entry];
			txd->buffer = cpu_to_le64(mapping);
			wmb();
			txd->control_word = cpu_to_le64(this_ctrl | len);

			if (gem_intme(entry))
				intme |= TXDCTRL_INTME;

			entry = NEXT_TX(entry);
		}
		txd = &gp->init_block->txd[first_entry];
		txd->buffer = cpu_to_le64(first_mapping);
		wmb();
		txd->control_word =
			cpu_to_le64(ctrl | TXDCTRL_SOF | intme | first_len);
	}

	gp->tx_new = entry;
	if (unlikely(TX_BUFFS_AVAIL(gp) <= (MAX_SKB_FRAGS + 1))) {
		netif_stop_queue(dev);

		/* netif_stop_queue() must be done before checking
		 * checking tx index in TX_BUFFS_AVAIL() below, because
		 * in gem_tx(), we update tx_old before checking for
		 * netif_queue_stopped().
		 */
		smp_mb();
		if (TX_BUFFS_AVAIL(gp) > (MAX_SKB_FRAGS + 1))
			netif_wake_queue(dev);
	}
	if (netif_msg_tx_queued(gp))
		printk(KERN_DEBUG "%s: tx queued, slot %d, skblen %d\n",
		       dev->name, entry, skb->len);
	mb();
	writel(gp->tx_new, gp->regs + TXDMA_KICK);

	return NETDEV_TX_OK;
}

static void gem_pcs_reset(struct gem *gp)
{
	int limit;
	u32 val;

	/* Reset PCS unit. */
	val = readl(gp->regs + PCS_MIICTRL);
	val |= PCS_MIICTRL_RST;
	writel(val, gp->regs + PCS_MIICTRL);

	limit = 32;
	while (readl(gp->regs + PCS_MIICTRL) & PCS_MIICTRL_RST) {
		udelay(100);
		if (limit-- <= 0)
			break;
	}
	if (limit < 0)
		netdev_warn(gp->dev, "PCS reset bit would not clear\n");
}

static void gem_pcs_reinit_adv(struct gem *gp)
{
	u32 val;

	/* Make sure PCS is disabled while changing advertisement
	 * configuration.
	 */
	val = readl(gp->regs + PCS_CFG);
	val &= ~(PCS_CFG_ENABLE | PCS_CFG_TO);
	writel(val, gp->regs + PCS_CFG);

	/* Advertise all capabilities except asymmetric
	 * pause.
	 */
	val = readl(gp->regs + PCS_MIIADV);
	val |= (PCS_MIIADV_FD | PCS_MIIADV_HD |
		PCS_MIIADV_SP | PCS_MIIADV_AP);
	writel(val, gp->regs + PCS_MIIADV);

	/* Enable and restart auto-negotiation, disable wrapback/loopback,
	 * and re-enable PCS.
	 */
	val = readl(gp->regs + PCS_MIICTRL);
	val |= (PCS_MIICTRL_RAN | PCS_MIICTRL_ANE);
	val &= ~PCS_MIICTRL_WB;
	writel(val, gp->regs + PCS_MIICTRL);

	val = readl(gp->regs + PCS_CFG);
	val |= PCS_CFG_ENABLE;
	writel(val, gp->regs + PCS_CFG);

	/* Make sure serialink loopback is off.  The meaning
	 * of this bit is logically inverted based upon whether
	 * you are in Serialink or SERDES mode.
	 */
	val = readl(gp->regs + PCS_SCTRL);
	if (gp->phy_type == phy_serialink)
		val &= ~PCS_SCTRL_LOOP;
	else
		val |= PCS_SCTRL_LOOP;
	writel(val, gp->regs + PCS_SCTRL);
}

#define STOP_TRIES 32

static void gem_reset(struct gem *gp)
{
	int limit;
	u32 val;

	/* Make sure we won't get any more interrupts */
	writel(0xffffffff, gp->regs + GREG_IMASK);

	/* Reset the chip */
	writel(gp->swrst_base | GREG_SWRST_TXRST | GREG_SWRST_RXRST,
	       gp->regs + GREG_SWRST);

	limit = STOP_TRIES;

	do {
		udelay(20);
		val = readl(gp->regs + GREG_SWRST);
		if (limit-- <= 0)
			break;
	} while (val & (GREG_SWRST_TXRST | GREG_SWRST_RXRST));

	if (limit < 0)
		netdev_err(gp->dev, "SW reset is ghetto\n");

	if (gp->phy_type == phy_serialink || gp->phy_type == phy_serdes)
		gem_pcs_reinit_adv(gp);
}

static void gem_start_dma(struct gem *gp)
{
	u32 val;

	/* We are ready to rock, turn everything on. */
	val = readl(gp->regs + TXDMA_CFG);
	writel(val | TXDMA_CFG_ENABLE, gp->regs + TXDMA_CFG);
	val = readl(gp->regs + RXDMA_CFG);
	writel(val | RXDMA_CFG_ENABLE, gp->regs + RXDMA_CFG);
	val = readl(gp->regs + MAC_TXCFG);
	writel(val | MAC_TXCFG_ENAB, gp->regs + MAC_TXCFG);
	val = readl(gp->regs + MAC_RXCFG);
	writel(val | MAC_RXCFG_ENAB, gp->regs + MAC_RXCFG);

	(void) readl(gp->regs + MAC_RXCFG);
	udelay(100);

	gem_enable_ints(gp);

	writel(RX_RING_SIZE - 4, gp->regs + RXDMA_KICK);
}

/* DMA won't be actually stopped before about 4ms tho ...
 */
static void gem_stop_dma(struct gem *gp)
{
	u32 val;

	/* We are done rocking, turn everything off. */
	val = readl(gp->regs + TXDMA_CFG);
	writel(val & ~TXDMA_CFG_ENABLE, gp->regs + TXDMA_CFG);
	val = readl(gp->regs + RXDMA_CFG);
	writel(val & ~RXDMA_CFG_ENABLE, gp->regs + RXDMA_CFG);
	val = readl(gp->regs + MAC_TXCFG);
	writel(val & ~MAC_TXCFG_ENAB, gp->regs + MAC_TXCFG);
	val = readl(gp->regs + MAC_RXCFG);
	writel(val & ~MAC_RXCFG_ENAB, gp->regs + MAC_RXCFG);

	(void) readl(gp->regs + MAC_RXCFG);

	/* Need to wait a bit ... done by the caller */
}


// XXX dbl check what that function should do when called on PCS PHY
static void gem_begin_auto_negotiation(struct gem *gp, struct ethtool_cmd *ep)
{
	u32 advertise, features;
	int autoneg;
	int speed;
	int duplex;

	if (gp->phy_type != phy_mii_mdio0 &&
     	    gp->phy_type != phy_mii_mdio1)
     	    	goto non_mii;

	/* Setup advertise */
	if (found_mii_phy(gp))
		features = gp->phy_mii.def->features;
	else
		features = 0;

	advertise = features & ADVERTISE_MASK;
	if (gp->phy_mii.advertising != 0)
		advertise &= gp->phy_mii.advertising;

	autoneg = gp->want_autoneg;
	speed = gp->phy_mii.speed;
	duplex = gp->phy_mii.duplex;

	/* Setup link parameters */
	if (!ep)
		goto start_aneg;
	if (ep->autoneg == AUTONEG_ENABLE) {
		advertise = ep->advertising;
		autoneg = 1;
	} else {
		autoneg = 0;
		speed = ethtool_cmd_speed(ep);
		duplex = ep->duplex;
	}

start_aneg:
	/* Sanitize settings based on PHY capabilities */
	if ((features & SUPPORTED_Autoneg) == 0)
		autoneg = 0;
	if (speed == SPEED_1000 &&
	    !(features & (SUPPORTED_1000baseT_Half | SUPPORTED_1000baseT_Full)))
		speed = SPEED_100;
	if (speed == SPEED_100 &&
	    !(features & (SUPPORTED_100baseT_Half | SUPPORTED_100baseT_Full)))
		speed = SPEED_10;
	if (duplex == DUPLEX_FULL &&
	    !(features & (SUPPORTED_1000baseT_Full |
	    		  SUPPORTED_100baseT_Full |
	    		  SUPPORTED_10baseT_Full)))
	    	duplex = DUPLEX_HALF;
	if (speed == 0)
		speed = SPEED_10;

	/* If we are asleep, we don't try to actually setup the PHY, we
	 * just store the settings
	 */
	if (!netif_device_present(gp->dev)) {
		gp->phy_mii.autoneg = gp->want_autoneg = autoneg;
		gp->phy_mii.speed = speed;
		gp->phy_mii.duplex = duplex;
		return;
	}

	/* Configure PHY & start aneg */
	gp->want_autoneg = autoneg;
	if (autoneg) {
		if (found_mii_phy(gp))
			gp->phy_mii.def->ops->setup_aneg(&gp->phy_mii, advertise);
		gp->lstate = link_aneg;
	} else {
		if (found_mii_phy(gp))
			gp->phy_mii.def->ops->setup_forced(&gp->phy_mii, speed, duplex);
		gp->lstate = link_force_ok;
	}

non_mii:
	gp->timer_ticks = 0;
	mod_timer(&gp->link_timer, jiffies + ((12 * HZ) / 10));
}

/* A link-up condition has occurred, initialize and enable the
 * rest of the chip.
 */
static int gem_set_link_modes(struct gem *gp)
{
	struct netdev_queue *txq = netdev_get_tx_queue(gp->dev, 0);
	int full_duplex, speed, pause;
	u32 val;

	full_duplex = 0;
	speed = SPEED_10;
	pause = 0;

	if (found_mii_phy(gp)) {
	    	if (gp->phy_mii.def->ops->read_link(&gp->phy_mii))
	    		return 1;
		full_duplex = (gp->phy_mii.duplex == DUPLEX_FULL);
		speed = gp->phy_mii.speed;
		pause = gp->phy_mii.pause;
	} else if (gp->phy_type == phy_serialink ||
	    	   gp->phy_type == phy_serdes) {
		u32 pcs_lpa = readl(gp->regs + PCS_MIILP);

		if ((pcs_lpa & PCS_MIIADV_FD) || gp->phy_type == phy_serdes)
			full_duplex = 1;
		speed = SPEED_1000;
	}

	netif_info(gp, link, gp->dev, "Link is up at %d Mbps, %s-duplex\n",
		   speed, (full_duplex ? "full" : "half"));


	/* We take the tx queue lock to avoid collisions between
	 * this code, the tx path and the NAPI-driven error path
	 */
	__netif_tx_lock(txq, smp_processor_id());

	val = (MAC_TXCFG_EIPG0 | MAC_TXCFG_NGU);
	if (full_duplex) {
		val |= (MAC_TXCFG_ICS | MAC_TXCFG_ICOLL);
	} else {
		/* MAC_TXCFG_NBO must be zero. */
	}
	writel(val, gp->regs + MAC_TXCFG);

	val = (MAC_XIFCFG_OE | MAC_XIFCFG_LLED);
	if (!full_duplex &&
	    (gp->phy_type == phy_mii_mdio0 ||
	     gp->phy_type == phy_mii_mdio1)) {
		val |= MAC_XIFCFG_DISE;
	} else if (full_duplex) {
		val |= MAC_XIFCFG_FLED;
	}

	if (speed == SPEED_1000)
		val |= (MAC_XIFCFG_GMII);

	writel(val, gp->regs + MAC_XIFCFG);

	/* If gigabit and half-duplex, enable carrier extension
	 * mode.  Else, disable it.
	 */
	if (speed == SPEED_1000 && !full_duplex) {
		val = readl(gp->regs + MAC_TXCFG);
		writel(val | MAC_TXCFG_TCE, gp->regs + MAC_TXCFG);

		val = readl(gp->regs + MAC_RXCFG);
		writel(val | MAC_RXCFG_RCE, gp->regs + MAC_RXCFG);
	} else {
		val = readl(gp->regs + MAC_TXCFG);
		writel(val & ~MAC_TXCFG_TCE, gp->regs + MAC_TXCFG);

		val = readl(gp->regs + MAC_RXCFG);
		writel(val & ~MAC_RXCFG_RCE, gp->regs + MAC_RXCFG);
	}

	if (gp->phy_type == phy_serialink ||
	    gp->phy_type == phy_serdes) {
 		u32 pcs_lpa = readl(gp->regs + PCS_MIILP);

		if (pcs_lpa & (PCS_MIIADV_SP | PCS_MIIADV_AP))
			pause = 1;
	}

	if (!full_duplex)
		writel(512, gp->regs + MAC_STIME);
	else
		writel(64, gp->regs + MAC_STIME);
	val = readl(gp->regs + MAC_MCCFG);
	if (pause)
		val |= (MAC_MCCFG_SPE | MAC_MCCFG_RPE);
	else
		val &= ~(MAC_MCCFG_SPE | MAC_MCCFG_RPE);
	writel(val, gp->regs + MAC_MCCFG);

	gem_start_dma(gp);

	__netif_tx_unlock(txq);

	if (netif_msg_link(gp)) {
		if (pause) {
			netdev_info(gp->dev,
				    "Pause is enabled (rxfifo: %d off: %d on: %d)\n",
				    gp->rx_fifo_sz,
				    gp->rx_pause_off,
				    gp->rx_pause_on);
		} else {
			netdev_info(gp->dev, "Pause is disabled\n");
		}
	}

	return 0;
}

static int gem_mdio_link_not_up(struct gem *gp)
{
	switch (gp->lstate) {
	case link_force_ret:
		netif_info(gp, link, gp->dev,
			   "Autoneg failed again, keeping forced mode\n");
		gp->phy_mii.def->ops->setup_forced(&gp->phy_mii,
			gp->last_forced_speed, DUPLEX_HALF);
		gp->timer_ticks = 5;
		gp->lstate = link_force_ok;
		return 0;
	case link_aneg:
		/* We try forced modes after a failed aneg only on PHYs that don't
		 * have "magic_aneg" bit set, which means they internally do the
		 * while forced-mode thingy. On these, we just restart aneg
		 */
		if (gp->phy_mii.def->magic_aneg)
			return 1;
		netif_info(gp, link, gp->dev, "switching to forced 100bt\n");
		/* Try forced modes. */
		gp->phy_mii.def->ops->setup_forced(&gp->phy_mii, SPEED_100,
			DUPLEX_HALF);
		gp->timer_ticks = 5;
		gp->lstate = link_force_try;
		return 0;
	case link_force_try:
		/* Downgrade from 100 to 10 Mbps if necessary.
		 * If already at 10Mbps, warn user about the
		 * situation every 10 ticks.
		 */
		if (gp->phy_mii.speed == SPEED_100) {
			gp->phy_mii.def->ops->setup_forced(&gp->phy_mii, SPEED_10,
				DUPLEX_HALF);
			gp->timer_ticks = 5;
			netif_info(gp, link, gp->dev,
				   "switching to forced 10bt\n");
			return 0;
		} else
			return 1;
	default:
		return 0;
	}
}

static void gem_link_timer(unsigned long data)
{
	struct gem *gp = (struct gem *) data;
	struct net_device *dev = gp->dev;
	int restart_aneg = 0;

	/* There's no point doing anything if we're going to be reset */
	if (gp->reset_task_pending)
		return;

	if (gp->phy_type == phy_serialink ||
	    gp->phy_type == phy_serdes) {
		u32 val = readl(gp->regs + PCS_MIISTAT);

		if (!(val & PCS_MIISTAT_LS))
			val = readl(gp->regs + PCS_MIISTAT);

		if ((val & PCS_MIISTAT_LS) != 0) {
			if (gp->lstate == link_up)
				goto restart;

			gp->lstate = link_up;
			netif_carrier_on(dev);
			(void)gem_set_link_modes(gp);
		}
		goto restart;
	}
	if (found_mii_phy(gp) && gp->phy_mii.def->ops->poll_link(&gp->phy_mii)) {
		/* Ok, here we got a link. If we had it due to a forced
		 * fallback, and we were configured for autoneg, we do
		 * retry a short autoneg pass. If you know your hub is
		 * broken, use ethtool ;)
		 */
		if (gp->lstate == link_force_try && gp->want_autoneg) {
			gp->lstate = link_force_ret;
			gp->last_forced_speed = gp->phy_mii.speed;
			gp->timer_ticks = 5;
			if (netif_msg_link(gp))
				netdev_info(dev,
					    "Got link after fallback, retrying autoneg once...\n");
			gp->phy_mii.def->ops->setup_aneg(&gp->phy_mii, gp->phy_mii.advertising);
		} else if (gp->lstate != link_up) {
			gp->lstate = link_up;
			netif_carrier_on(dev);
			if (gem_set_link_modes(gp))
				restart_aneg = 1;
		}
	} else {
		/* If the link was previously up, we restart the
		 * whole process
		 */
		if (gp->lstate == link_up) {
			gp->lstate = link_down;
			netif_info(gp, link, dev, "Link down\n");
			netif_carrier_off(dev);
			gem_schedule_reset(gp);
			/* The reset task will restart the timer */
			return;
		} else if (++gp->timer_ticks > 10) {
			if (found_mii_phy(gp))
				restart_aneg = gem_mdio_link_not_up(gp);
			else
				restart_aneg = 1;
		}
	}
	if (restart_aneg) {
		gem_begin_auto_negotiation(gp, NULL);
		return;
	}
restart:
	mod_timer(&gp->link_timer, jiffies + ((12 * HZ) / 10));
}

static void gem_clean_rings(struct gem *gp)
{
	struct gem_init_block *gb = gp->init_block;
	struct sk_buff *skb;
	int i;
	dma_addr_t dma_addr;

	for (i = 0; i < RX_RING_SIZE; i++) {
		struct gem_rxd *rxd;

		rxd = &gb->rxd[i];
		if (gp->rx_skbs[i] != NULL) {
			skb = gp->rx_skbs[i];
			dma_addr = le64_to_cpu(rxd->buffer);
			pci_unmap_page(gp->pdev, dma_addr,
				       RX_BUF_ALLOC_SIZE(gp),
				       PCI_DMA_FROMDEVICE);
			dev_kfree_skb_any(skb);
			gp->rx_skbs[i] = NULL;
		}
		rxd->status_word = 0;
		wmb();
		rxd->buffer = 0;
	}

	for (i = 0; i < TX_RING_SIZE; i++) {
		if (gp->tx_skbs[i] != NULL) {
			struct gem_txd *txd;
			int frag;

			skb = gp->tx_skbs[i];
			gp->tx_skbs[i] = NULL;

			for (frag = 0; frag <= skb_shinfo(skb)->nr_frags; frag++) {
				int ent = i & (TX_RING_SIZE - 1);

				txd = &gb->txd[ent];
				dma_addr = le64_to_cpu(txd->buffer);
				pci_unmap_page(gp->pdev, dma_addr,
					       le64_to_cpu(txd->control_word) &
					       TXDCTRL_BUFSZ, PCI_DMA_TODEVICE);

				if (frag != skb_shinfo(skb)->nr_frags)
					i++;
			}
			dev_kfree_skb_any(skb);
		}
	}
}

static void gem_init_rings(struct gem *gp)
{
	struct gem_init_block *gb = gp->init_block;
	struct net_device *dev = gp->dev;
	int i;
	dma_addr_t dma_addr;

	gp->rx_new = gp->rx_old = gp->tx_new = gp->tx_old = 0;

	gem_clean_rings(gp);

	gp->rx_buf_sz = max(dev->mtu + ETH_HLEN + VLAN_HLEN,
			    (unsigned)VLAN_ETH_FRAME_LEN);

	for (i = 0; i < RX_RING_SIZE; i++) {
		struct sk_buff *skb;
		struct gem_rxd *rxd = &gb->rxd[i];

		skb = gem_alloc_skb(dev, RX_BUF_ALLOC_SIZE(gp), GFP_KERNEL);
		if (!skb) {
			rxd->buffer = 0;
			rxd->status_word = 0;
			continue;
		}

		gp->rx_skbs[i] = skb;
		skb_put(skb, (gp->rx_buf_sz + RX_OFFSET));
		dma_addr = pci_map_page(gp->pdev,
					virt_to_page(skb->data),
					offset_in_page(skb->data),
					RX_BUF_ALLOC_SIZE(gp),
					PCI_DMA_FROMDEVICE);
		rxd->buffer = cpu_to_le64(dma_addr);
		wmb();
		rxd->status_word = cpu_to_le64(RXDCTRL_FRESH(gp));
		skb_reserve(skb, RX_OFFSET);
	}

	for (i = 0; i < TX_RING_SIZE; i++) {
		struct gem_txd *txd = &gb->txd[i];

		txd->control_word = 0;
		wmb();
		txd->buffer = 0;
	}
	wmb();
}

/* Init PHY interface and start link poll state machine */
static void gem_init_phy(struct gem *gp)
{
	u32 mifcfg;

	/* Revert MIF CFG setting done on stop_phy */
	mifcfg = readl(gp->regs + MIF_CFG);
	mifcfg &= ~MIF_CFG_BBMODE;
	writel(mifcfg, gp->regs + MIF_CFG);

	if (gp->pdev->vendor == PCI_VENDOR_ID_APPLE) {
		int i;

		/* Those delay sucks, the HW seem to love them though, I'll
		 * serisouly consider breaking some locks here to be able
		 * to schedule instead
		 */
		for (i = 0; i < 3; i++) {
#ifdef CONFIG_PPC_PMAC
			pmac_call_feature(PMAC_FTR_GMAC_PHY_RESET, gp->of_node, 0, 0);
			msleep(20);
#endif
			/* Some PHYs used by apple have problem getting back to us,
			 * we do an additional reset here
			 */
			phy_write(gp, MII_BMCR, BMCR_RESET);
			msleep(20);
			if (phy_read(gp, MII_BMCR) != 0xffff)
				break;
			if (i == 2)
				netdev_warn(gp->dev, "GMAC PHY not responding !\n");
		}
	}

	if (gp->pdev->vendor == PCI_VENDOR_ID_SUN &&
	    gp->pdev->device == PCI_DEVICE_ID_SUN_GEM) {
		u32 val;

		/* Init datapath mode register. */
		if (gp->phy_type == phy_mii_mdio0 ||
		    gp->phy_type == phy_mii_mdio1) {
			val = PCS_DMODE_MGM;
		} else if (gp->phy_type == phy_serialink) {
			val = PCS_DMODE_SM | PCS_DMODE_GMOE;
		} else {
			val = PCS_DMODE_ESM;
		}

		writel(val, gp->regs + PCS_DMODE);
	}

	if (gp->phy_type == phy_mii_mdio0 ||
	    gp->phy_type == phy_mii_mdio1) {
		/* Reset and detect MII PHY */
		sungem_phy_probe(&gp->phy_mii, gp->mii_phy_addr);

		/* Init PHY */
		if (gp->phy_mii.def && gp->phy_mii.def->ops->init)
			gp->phy_mii.def->ops->init(&gp->phy_mii);
	} else {
		gem_pcs_reset(gp);
		gem_pcs_reinit_adv(gp);
	}

	/* Default aneg parameters */
	gp->timer_ticks = 0;
	gp->lstate = link_down;
	netif_carrier_off(gp->dev);

	/* Print things out */
	if (gp->phy_type == phy_mii_mdio0 ||
	    gp->phy_type == phy_mii_mdio1)
		netdev_info(gp->dev, "Found %s PHY\n",
			    gp->phy_mii.def ? gp->phy_mii.def->name : "no");

	gem_begin_auto_negotiation(gp, NULL);
}

static void gem_init_dma(struct gem *gp)
{
	u64 desc_dma = (u64) gp->gblock_dvma;
	u32 val;

	val = (TXDMA_CFG_BASE | (0x7ff << 10) | TXDMA_CFG_PMODE);
	writel(val, gp->regs + TXDMA_CFG);

	writel(desc_dma >> 32, gp->regs + TXDMA_DBHI);
	writel(desc_dma & 0xffffffff, gp->regs + TXDMA_DBLOW);
	desc_dma += (INIT_BLOCK_TX_RING_SIZE * sizeof(struct gem_txd));

	writel(0, gp->regs + TXDMA_KICK);

	val = (RXDMA_CFG_BASE | (RX_OFFSET << 10) |
	       ((14 / 2) << 13) | RXDMA_CFG_FTHRESH_128);
	writel(val, gp->regs + RXDMA_CFG);

	writel(desc_dma >> 32, gp->regs + RXDMA_DBHI);
	writel(desc_dma & 0xffffffff, gp->regs + RXDMA_DBLOW);

	writel(RX_RING_SIZE - 4, gp->regs + RXDMA_KICK);

	val  = (((gp->rx_pause_off / 64) << 0) & RXDMA_PTHRESH_OFF);
	val |= (((gp->rx_pause_on / 64) << 12) & RXDMA_PTHRESH_ON);
	writel(val, gp->regs + RXDMA_PTHRESH);

	if (readl(gp->regs + GREG_BIFCFG) & GREG_BIFCFG_M66EN)
		writel(((5 & RXDMA_BLANK_IPKTS) |
			((8 << 12) & RXDMA_BLANK_ITIME)),
		       gp->regs + RXDMA_BLANK);
	else
		writel(((5 & RXDMA_BLANK_IPKTS) |
			((4 << 12) & RXDMA_BLANK_ITIME)),
		       gp->regs + RXDMA_BLANK);
}

static u32 gem_setup_multicast(struct gem *gp)
{
	u32 rxcfg = 0;
	int i;

	if ((gp->dev->flags & IFF_ALLMULTI) ||
	    (netdev_mc_count(gp->dev) > 256)) {
	    	for (i=0; i<16; i++)
			writel(0xffff, gp->regs + MAC_HASH0 + (i << 2));
		rxcfg |= MAC_RXCFG_HFE;
	} else if (gp->dev->flags & IFF_PROMISC) {
		rxcfg |= MAC_RXCFG_PROM;
	} else {
		u16 hash_table[16];
		u32 crc;
		struct netdev_hw_addr *ha;
		int i;

		memset(hash_table, 0, sizeof(hash_table));
		netdev_for_each_mc_addr(ha, gp->dev) {
			crc = ether_crc_le(6, ha->addr);
			crc >>= 24;
			hash_table[crc >> 4] |= 1 << (15 - (crc & 0xf));
		}
	    	for (i=0; i<16; i++)
			writel(hash_table[i], gp->regs + MAC_HASH0 + (i << 2));
		rxcfg |= MAC_RXCFG_HFE;
	}

	return rxcfg;
}

static void gem_init_mac(struct gem *gp)
{
	unsigned char *e = &gp->dev->dev_addr[0];

	writel(0x1bf0, gp->regs + MAC_SNDPAUSE);

	writel(0x00, gp->regs + MAC_IPG0);
	writel(0x08, gp->regs + MAC_IPG1);
	writel(0x04, gp->regs + MAC_IPG2);
	writel(0x40, gp->regs + MAC_STIME);
	writel(0x40, gp->regs + MAC_MINFSZ);

	/* Ethernet payload + header + FCS + optional VLAN tag. */
	writel(0x20000000 | (gp->rx_buf_sz + 4), gp->regs + MAC_MAXFSZ);

	writel(0x07, gp->regs + MAC_PASIZE);
	writel(0x04, gp->regs + MAC_JAMSIZE);
	writel(0x10, gp->regs + MAC_ATTLIM);
	writel(0x8808, gp->regs + MAC_MCTYPE);

	writel((e[5] | (e[4] << 8)) & 0x3ff, gp->regs + MAC_RANDSEED);

	writel((e[4] << 8) | e[5], gp->regs + MAC_ADDR0);
	writel((e[2] << 8) | e[3], gp->regs + MAC_ADDR1);
	writel((e[0] << 8) | e[1], gp->regs + MAC_ADDR2);

	writel(0, gp->regs + MAC_ADDR3);
	writel(0, gp->regs + MAC_ADDR4);
	writel(0, gp->regs + MAC_ADDR5);

	writel(0x0001, gp->regs + MAC_ADDR6);
	writel(0xc200, gp->regs + MAC_ADDR7);
	writel(0x0180, gp->regs + MAC_ADDR8);

	writel(0, gp->regs + MAC_AFILT0);
	writel(0, gp->regs + MAC_AFILT1);
	writel(0, gp->regs + MAC_AFILT2);
	writel(0, gp->regs + MAC_AF21MSK);
	writel(0, gp->regs + MAC_AF0MSK);

	gp->mac_rx_cfg = gem_setup_multicast(gp);
#ifdef STRIP_FCS
	gp->mac_rx_cfg |= MAC_RXCFG_SFCS;
#endif
	writel(0, gp->regs + MAC_NCOLL);
	writel(0, gp->regs + MAC_FASUCC);
	writel(0, gp->regs + MAC_ECOLL);
	writel(0, gp->regs + MAC_LCOLL);
	writel(0, gp->regs + MAC_DTIMER);
	writel(0, gp->regs + MAC_PATMPS);
	writel(0, gp->regs + MAC_RFCTR);
	writel(0, gp->regs + MAC_LERR);
	writel(0, gp->regs + MAC_AERR);
	writel(0, gp->regs + MAC_FCSERR);
	writel(0, gp->regs + MAC_RXCVERR);

	/* Clear RX/TX/MAC/XIF config, we will set these up and enable
	 * them once a link is established.
	 */
	writel(0, gp->regs + MAC_TXCFG);
	writel(gp->mac_rx_cfg, gp->regs + MAC_RXCFG);
	writel(0, gp->regs + MAC_MCCFG);
	writel(0, gp->regs + MAC_XIFCFG);

	/* Setup MAC interrupts.  We want to get all of the interesting
	 * counter expiration events, but we do not want to hear about
	 * normal rx/tx as the DMA engine tells us that.
	 */
	writel(MAC_TXSTAT_XMIT, gp->regs + MAC_TXMASK);
	writel(MAC_RXSTAT_RCV, gp->regs + MAC_RXMASK);

	/* Don't enable even the PAUSE interrupts for now, we
	 * make no use of those events other than to record them.
	 */
	writel(0xffffffff, gp->regs + MAC_MCMASK);

	/* Don't enable GEM's WOL in normal operations
	 */
	if (gp->has_wol)
		writel(0, gp->regs + WOL_WAKECSR);
}

static void gem_init_pause_thresholds(struct gem *gp)
{
       	u32 cfg;

	/* Calculate pause thresholds.  Setting the OFF threshold to the
	 * full RX fifo size effectively disables PAUSE generation which
	 * is what we do for 10/100 only GEMs which have FIFOs too small
	 * to make real gains from PAUSE.
	 */
	if (gp->rx_fifo_sz <= (2 * 1024)) {
		gp->rx_pause_off = gp->rx_pause_on = gp->rx_fifo_sz;
	} else {
		int max_frame = (gp->rx_buf_sz + 4 + 64) & ~63;
		int off = (gp->rx_fifo_sz - (max_frame * 2));
		int on = off - max_frame;

		gp->rx_pause_off = off;
		gp->rx_pause_on = on;
	}


	/* Configure the chip "burst" DMA mode & enable some
	 * HW bug fixes on Apple version
	 */
       	cfg  = 0;
       	if (gp->pdev->vendor == PCI_VENDOR_ID_APPLE)
		cfg |= GREG_CFG_RONPAULBIT | GREG_CFG_ENBUG2FIX;
#if !defined(CONFIG_SPARC64) && !defined(CONFIG_ALPHA)
       	cfg |= GREG_CFG_IBURST;
#endif
       	cfg |= ((31 << 1) & GREG_CFG_TXDMALIM);
       	cfg |= ((31 << 6) & GREG_CFG_RXDMALIM);
       	writel(cfg, gp->regs + GREG_CFG);

	/* If Infinite Burst didn't stick, then use different
	 * thresholds (and Apple bug fixes don't exist)
	 */
	if (!(readl(gp->regs + GREG_CFG) & GREG_CFG_IBURST)) {
		cfg = ((2 << 1) & GREG_CFG_TXDMALIM);
		cfg |= ((8 << 6) & GREG_CFG_RXDMALIM);
		writel(cfg, gp->regs + GREG_CFG);
	}
}

static int gem_check_invariants(struct gem *gp)
{
	struct pci_dev *pdev = gp->pdev;
	u32 mif_cfg;

	/* On Apple's sungem, we can't rely on registers as the chip
	 * was been powered down by the firmware. The PHY is looked
	 * up later on.
	 */
	if (pdev->vendor == PCI_VENDOR_ID_APPLE) {
		gp->phy_type = phy_mii_mdio0;
		gp->tx_fifo_sz = readl(gp->regs + TXDMA_FSZ) * 64;
		gp->rx_fifo_sz = readl(gp->regs + RXDMA_FSZ) * 64;
		gp->swrst_base = 0;

		mif_cfg = readl(gp->regs + MIF_CFG);
		mif_cfg &= ~(MIF_CFG_PSELECT|MIF_CFG_POLL|MIF_CFG_BBMODE|MIF_CFG_MDI1);
		mif_cfg |= MIF_CFG_MDI0;
		writel(mif_cfg, gp->regs + MIF_CFG);
		writel(PCS_DMODE_MGM, gp->regs + PCS_DMODE);
		writel(MAC_XIFCFG_OE, gp->regs + MAC_XIFCFG);

		/* We hard-code the PHY address so we can properly bring it out of
		 * reset later on, we can't really probe it at this point, though
		 * that isn't an issue.
		 */
		if (gp->pdev->device == PCI_DEVICE_ID_APPLE_K2_GMAC)
			gp->mii_phy_addr = 1;
		else
			gp->mii_phy_addr = 0;

		return 0;
	}

	mif_cfg = readl(gp->regs + MIF_CFG);

	if (pdev->vendor == PCI_VENDOR_ID_SUN &&
	    pdev->device == PCI_DEVICE_ID_SUN_RIO_GEM) {
		/* One of the MII PHYs _must_ be present
		 * as this chip has no gigabit PHY.
		 */
		if ((mif_cfg & (MIF_CFG_MDI0 | MIF_CFG_MDI1)) == 0) {
			pr_err("RIO GEM lacks MII phy, mif_cfg[%08x]\n",
			       mif_cfg);
			return -1;
		}
	}

	/* Determine initial PHY interface type guess.  MDIO1 is the
	 * external PHY and thus takes precedence over MDIO0.
	 */

	if (mif_cfg & MIF_CFG_MDI1) {
		gp->phy_type = phy_mii_mdio1;
		mif_cfg |= MIF_CFG_PSELECT;
		writel(mif_cfg, gp->regs + MIF_CFG);
	} else if (mif_cfg & MIF_CFG_MDI0) {
		gp->phy_type = phy_mii_mdio0;
		mif_cfg &= ~MIF_CFG_PSELECT;
		writel(mif_cfg, gp->regs + MIF_CFG);
	} else {
#ifdef CONFIG_SPARC
		const char *p;

		p = of_get_property(gp->of_node, "shared-pins", NULL);
		if (p && !strcmp(p, "serdes"))
			gp->phy_type = phy_serdes;
		else
#endif
			gp->phy_type = phy_serialink;
	}
	if (gp->phy_type == phy_mii_mdio1 ||
	    gp->phy_type == phy_mii_mdio0) {
		int i;

		for (i = 0; i < 32; i++) {
			gp->mii_phy_addr = i;
			if (phy_read(gp, MII_BMCR) != 0xffff)
				break;
		}
		if (i == 32) {
			if (pdev->device != PCI_DEVICE_ID_SUN_GEM) {
				pr_err("RIO MII phy will not respond\n");
				return -1;
			}
			gp->phy_type = phy_serdes;
		}
	}

	/* Fetch the FIFO configurations now too. */
	gp->tx_fifo_sz = readl(gp->regs + TXDMA_FSZ) * 64;
	gp->rx_fifo_sz = readl(gp->regs + RXDMA_FSZ) * 64;

	if (pdev->vendor == PCI_VENDOR_ID_SUN) {
		if (pdev->device == PCI_DEVICE_ID_SUN_GEM) {
			if (gp->tx_fifo_sz != (9 * 1024) ||
			    gp->rx_fifo_sz != (20 * 1024)) {
				pr_err("GEM has bogus fifo sizes tx(%d) rx(%d)\n",
				       gp->tx_fifo_sz, gp->rx_fifo_sz);
				return -1;
			}
			gp->swrst_base = 0;
		} else {
			if (gp->tx_fifo_sz != (2 * 1024) ||
			    gp->rx_fifo_sz != (2 * 1024)) {
				pr_err("RIO GEM has bogus fifo sizes tx(%d) rx(%d)\n",
				       gp->tx_fifo_sz, gp->rx_fifo_sz);
				return -1;
			}
			gp->swrst_base = (64 / 4) << GREG_SWRST_CACHE_SHIFT;
		}
	}

	return 0;
}

static void gem_reinit_chip(struct gem *gp)
{
	/* Reset the chip */
	gem_reset(gp);

	/* Make sure ints are disabled */
	gem_disable_ints(gp);

	/* Allocate & setup ring buffers */
	gem_init_rings(gp);

	/* Configure pause thresholds */
	gem_init_pause_thresholds(gp);

	/* Init DMA & MAC engines */
	gem_init_dma(gp);
	gem_init_mac(gp);
}


static void gem_stop_phy(struct gem *gp, int wol)
{
	u32 mifcfg;

	/* Let the chip settle down a bit, it seems that helps
	 * for sleep mode on some models
	 */
	msleep(10);

	/* Make sure we aren't polling PHY status change. We
	 * don't currently use that feature though
	 */
	mifcfg = readl(gp->regs + MIF_CFG);
	mifcfg &= ~MIF_CFG_POLL;
	writel(mifcfg, gp->regs + MIF_CFG);

	if (wol && gp->has_wol) {
		unsigned char *e = &gp->dev->dev_addr[0];
		u32 csr;

		/* Setup wake-on-lan for MAGIC packet */
		writel(MAC_RXCFG_HFE | MAC_RXCFG_SFCS | MAC_RXCFG_ENAB,
		       gp->regs + MAC_RXCFG);
		writel((e[4] << 8) | e[5], gp->regs + WOL_MATCH0);
		writel((e[2] << 8) | e[3], gp->regs + WOL_MATCH1);
		writel((e[0] << 8) | e[1], gp->regs + WOL_MATCH2);

		writel(WOL_MCOUNT_N | WOL_MCOUNT_M, gp->regs + WOL_MCOUNT);
		csr = WOL_WAKECSR_ENABLE;
		if ((readl(gp->regs + MAC_XIFCFG) & MAC_XIFCFG_GMII) == 0)
			csr |= WOL_WAKECSR_MII;
		writel(csr, gp->regs + WOL_WAKECSR);
	} else {
		writel(0, gp->regs + MAC_RXCFG);
		(void)readl(gp->regs + MAC_RXCFG);
		/* Machine sleep will die in strange ways if we
		 * dont wait a bit here, looks like the chip takes
		 * some time to really shut down
		 */
		msleep(10);
	}

	writel(0, gp->regs + MAC_TXCFG);
	writel(0, gp->regs + MAC_XIFCFG);
	writel(0, gp->regs + TXDMA_CFG);
	writel(0, gp->regs + RXDMA_CFG);

	if (!wol) {
		gem_reset(gp);
		writel(MAC_TXRST_CMD, gp->regs + MAC_TXRST);
		writel(MAC_RXRST_CMD, gp->regs + MAC_RXRST);

		if (found_mii_phy(gp) && gp->phy_mii.def->ops->suspend)
			gp->phy_mii.def->ops->suspend(&gp->phy_mii);

		/* According to Apple, we must set the MDIO pins to this begnign
		 * state or we may 1) eat more current, 2) damage some PHYs
		 */
		writel(mifcfg | MIF_CFG_BBMODE, gp->regs + MIF_CFG);
		writel(0, gp->regs + MIF_BBCLK);
		writel(0, gp->regs + MIF_BBDATA);
		writel(0, gp->regs + MIF_BBOENAB);
		writel(MAC_XIFCFG_GMII | MAC_XIFCFG_LBCK, gp->regs + MAC_XIFCFG);
		(void) readl(gp->regs + MAC_XIFCFG);
	}
}

static int gem_do_start(struct net_device *dev)
{
	struct gem *gp = netdev_priv(dev);
	int rc;

	/* Enable the cell */
	gem_get_cell(gp);

	/* Make sure PCI access and bus master are enabled */
	rc = pci_enable_device(gp->pdev);
	if (rc) {
		netdev_err(dev, "Failed to enable chip on PCI bus !\n");

		/* Put cell and forget it for now, it will be considered as
		 * still asleep, a new sleep cycle may bring it back
		 */
		gem_put_cell(gp);
		return -ENXIO;
	}
	pci_set_master(gp->pdev);

	/* Init & setup chip hardware */
	gem_reinit_chip(gp);

	/* An interrupt might come in handy */
	rc = request_irq(gp->pdev->irq, gem_interrupt,
			 IRQF_SHARED, dev->name, (void *)dev);
	if (rc) {
		netdev_err(dev, "failed to request irq !\n");

		gem_reset(gp);
		gem_clean_rings(gp);
		gem_put_cell(gp);
		return rc;
	}

	/* Mark us as attached again if we come from resume(), this has
	 * no effect if we weren't detatched and needs to be done now.
	 */
	netif_device_attach(dev);

	/* Restart NAPI & queues */
	gem_netif_start(gp);

	/* Detect & init PHY, start autoneg etc... this will
	 * eventually result in starting DMA operations when
	 * the link is up
	 */
	gem_init_phy(gp);

	return 0;
}

static void gem_do_stop(struct net_device *dev, int wol)
{
	struct gem *gp = netdev_priv(dev);

	/* Stop NAPI and stop tx queue */
	gem_netif_stop(gp);

	/* Make sure ints are disabled. We don't care about
	 * synchronizing as NAPI is disabled, thus a stray
	 * interrupt will do nothing bad (our irq handler
	 * just schedules NAPI)
	 */
	gem_disable_ints(gp);

	/* Stop the link timer */
	del_timer_sync(&gp->link_timer);

	/* We cannot cancel the reset task while holding the
	 * rtnl lock, we'd get an A->B / B->A deadlock stituation
	 * if we did. This is not an issue however as the reset
	 * task is synchronized vs. us (rtnl_lock) and will do
	 * nothing if the device is down or suspended. We do
	 * still clear reset_task_pending to avoid a spurrious
	 * reset later on in case we do resume before it gets
	 * scheduled.
	 */
	gp->reset_task_pending = 0;

	/* If we are going to sleep with WOL */
	gem_stop_dma(gp);
	msleep(10);
	if (!wol)
		gem_reset(gp);
	msleep(10);

	/* Get rid of rings */
	gem_clean_rings(gp);

	/* No irq needed anymore */
	free_irq(gp->pdev->irq, (void *) dev);

	/* Shut the PHY down eventually and setup WOL */
	gem_stop_phy(gp, wol);

	/* Make sure bus master is disabled */
	pci_disable_device(gp->pdev);

	/* Cell not needed neither if no WOL */
	if (!wol)
		gem_put_cell(gp);
}

static void gem_reset_task(struct work_struct *work)
{
	struct gem *gp = container_of(work, struct gem, reset_task);

	/* Lock out the network stack (essentially shield ourselves
	 * against a racing open, close, control call, or suspend
	 */
	rtnl_lock();

	/* Skip the reset task if suspended or closed, or if it's
	 * been cancelled by gem_do_stop (see comment there)
	 */
	if (!netif_device_present(gp->dev) ||
	    !netif_running(gp->dev) ||
	    !gp->reset_task_pending) {
		rtnl_unlock();
		return;
	}

	/* Stop the link timer */
	del_timer_sync(&gp->link_timer);

	/* Stop NAPI and tx */
	gem_netif_stop(gp);

	/* Reset the chip & rings */
	gem_reinit_chip(gp);
	if (gp->lstate == link_up)
		gem_set_link_modes(gp);

	/* Restart NAPI and Tx */
	gem_netif_start(gp);

	/* We are back ! */
	gp->reset_task_pending = 0;

	/* If the link is not up, restart autoneg, else restart the
	 * polling timer
	 */
	if (gp->lstate != link_up)
		gem_begin_auto_negotiation(gp, NULL);
	else
		mod_timer(&gp->link_timer, jiffies + ((12 * HZ) / 10));

	rtnl_unlock();
}

static int gem_open(struct net_device *dev)
{
	/* We allow open while suspended, we just do nothing,
	 * the chip will be initialized in resume()
	 */
	if (netif_device_present(dev))
		return gem_do_start(dev);
	return 0;
}

static int gem_close(struct net_device *dev)
{
	if (netif_device_present(dev))
		gem_do_stop(dev, 0);

	return 0;
}

#ifdef CONFIG_PM
static int gem_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct gem *gp = netdev_priv(dev);

	/* Lock the network stack first to avoid racing with open/close,
	 * reset task and setting calls
	 */
	rtnl_lock();

	/* Not running, mark ourselves non-present, no need for
	 * a lock here
	 */
	if (!netif_running(dev)) {
		netif_device_detach(dev);
		rtnl_unlock();
		return 0;
	}
	netdev_info(dev, "suspending, WakeOnLan %s\n",
		    (gp->wake_on_lan && netif_running(dev)) ?
		    "enabled" : "disabled");

	/* Tell the network stack we're gone. gem_do_stop() below will
	 * synchronize with TX, stop NAPI etc...
	 */
	netif_device_detach(dev);

	/* Switch off chip, remember WOL setting */
	gp->asleep_wol = gp->wake_on_lan;
	gem_do_stop(dev, gp->asleep_wol);

	/* Unlock the network stack */
	rtnl_unlock();

	return 0;
}

static int gem_resume(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct gem *gp = netdev_priv(dev);

	/* See locking comment in gem_suspend */
	rtnl_lock();

	/* Not running, mark ourselves present, no need for
	 * a lock here
	 */
	if (!netif_running(dev)) {
		netif_device_attach(dev);
		rtnl_unlock();
		return 0;
	}

	/* Restart chip. If that fails there isn't much we can do, we
	 * leave things stopped.
	 */
	gem_do_start(dev);

	/* If we had WOL enabled, the cell clock was never turned off during
	 * sleep, so we end up beeing unbalanced. Fix that here
	 */
	if (gp->asleep_wol)
		gem_put_cell(gp);

	/* Unlock the network stack */
	rtnl_unlock();

	return 0;
}
#endif /* CONFIG_PM */

static struct net_device_stats *gem_get_stats(struct net_device *dev)
{
	struct gem *gp = netdev_priv(dev);

	/* I have seen this being called while the PM was in progress,
	 * so we shield against this. Let's also not poke at registers
	 * while the reset task is going on.
	 *
	 * TODO: Move stats collection elsewhere (link timer ?) and
	 * make this a nop to avoid all those synchro issues
	 */
	if (!netif_device_present(dev) || !netif_running(dev))
		goto bail;

	/* Better safe than sorry... */
	if (WARN_ON(!gp->cell_enabled))
		goto bail;

	dev->stats.rx_crc_errors += readl(gp->regs + MAC_FCSERR);
	writel(0, gp->regs + MAC_FCSERR);

	dev->stats.rx_frame_errors += readl(gp->regs + MAC_AERR);
	writel(0, gp->regs + MAC_AERR);

	dev->stats.rx_length_errors += readl(gp->regs + MAC_LERR);
	writel(0, gp->regs + MAC_LERR);

	dev->stats.tx_aborted_errors += readl(gp->regs + MAC_ECOLL);
	dev->stats.collisions +=
		(readl(gp->regs + MAC_ECOLL) + readl(gp->regs + MAC_LCOLL));
	writel(0, gp->regs + MAC_ECOLL);
	writel(0, gp->regs + MAC_LCOLL);
 bail:
	return &dev->stats;
}

static int gem_set_mac_address(struct net_device *dev, void *addr)
{
	struct sockaddr *macaddr = (struct sockaddr *) addr;
	struct gem *gp = netdev_priv(dev);
	unsigned char *e = &dev->dev_addr[0];

	if (!is_valid_ether_addr(macaddr->sa_data))
		return -EADDRNOTAVAIL;

	memcpy(dev->dev_addr, macaddr->sa_data, dev->addr_len);

	/* We'll just catch it later when the device is up'd or resumed */
	if (!netif_running(dev) || !netif_device_present(dev))
		return 0;

	/* Better safe than sorry... */
	if (WARN_ON(!gp->cell_enabled))
		return 0;

	writel((e[4] << 8) | e[5], gp->regs + MAC_ADDR0);
	writel((e[2] << 8) | e[3], gp->regs + MAC_ADDR1);
	writel((e[0] << 8) | e[1], gp->regs + MAC_ADDR2);

	return 0;
}

static void gem_set_multicast(struct net_device *dev)
{
	struct gem *gp = netdev_priv(dev);
	u32 rxcfg, rxcfg_new;
	int limit = 10000;

	if (!netif_running(dev) || !netif_device_present(dev))
		return;

	/* Better safe than sorry... */
	if (gp->reset_task_pending || WARN_ON(!gp->cell_enabled))
		return;

	rxcfg = readl(gp->regs + MAC_RXCFG);
	rxcfg_new = gem_setup_multicast(gp);
#ifdef STRIP_FCS
	rxcfg_new |= MAC_RXCFG_SFCS;
#endif
	gp->mac_rx_cfg = rxcfg_new;

	writel(rxcfg & ~MAC_RXCFG_ENAB, gp->regs + MAC_RXCFG);
	while (readl(gp->regs + MAC_RXCFG) & MAC_RXCFG_ENAB) {
		if (!limit--)
			break;
		udelay(10);
	}

	rxcfg &= ~(MAC_RXCFG_PROM | MAC_RXCFG_HFE);
	rxcfg |= rxcfg_new;

	writel(rxcfg, gp->regs + MAC_RXCFG);
}

/* Jumbo-grams don't seem to work :-( */
#define GEM_MIN_MTU	68
#if 1
#define GEM_MAX_MTU	1500
#else
#define GEM_MAX_MTU	9000
#endif

static int gem_change_mtu(struct net_device *dev, int new_mtu)
{
	struct gem *gp = netdev_priv(dev);

	if (new_mtu < GEM_MIN_MTU || new_mtu > GEM_MAX_MTU)
		return -EINVAL;

	dev->mtu = new_mtu;

	/* We'll just catch it later when the device is up'd or resumed */
	if (!netif_running(dev) || !netif_device_present(dev))
		return 0;

	/* Better safe than sorry... */
	if (WARN_ON(!gp->cell_enabled))
		return 0;

	gem_netif_stop(gp);
	gem_reinit_chip(gp);
	if (gp->lstate == link_up)
		gem_set_link_modes(gp);
	gem_netif_start(gp);

	return 0;
}

static void gem_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *info)
{
	struct gem *gp = netdev_priv(dev);

	strcpy(info->driver, DRV_NAME);
	strcpy(info->version, DRV_VERSION);
	strcpy(info->bus_info, pci_name(gp->pdev));
}

static int gem_get_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct gem *gp = netdev_priv(dev);

	if (gp->phy_type == phy_mii_mdio0 ||
	    gp->phy_type == phy_mii_mdio1) {
		if (gp->phy_mii.def)
			cmd->supported = gp->phy_mii.def->features;
		else
			cmd->supported = (SUPPORTED_10baseT_Half |
					  SUPPORTED_10baseT_Full);

		/* XXX hardcoded stuff for now */
		cmd->port = PORT_MII;
		cmd->transceiver = XCVR_EXTERNAL;
		cmd->phy_address = 0; /* XXX fixed PHYAD */

		/* Return current PHY settings */
		cmd->autoneg = gp->want_autoneg;
		ethtool_cmd_speed_set(cmd, gp->phy_mii.speed);
		cmd->duplex = gp->phy_mii.duplex;
		cmd->advertising = gp->phy_mii.advertising;

		/* If we started with a forced mode, we don't have a default
		 * advertise set, we need to return something sensible so
		 * userland can re-enable autoneg properly.
		 */
		if (cmd->advertising == 0)
			cmd->advertising = cmd->supported;
	} else { // XXX PCS ?
		cmd->supported =
			(SUPPORTED_10baseT_Half | SUPPORTED_10baseT_Full |
			 SUPPORTED_100baseT_Half | SUPPORTED_100baseT_Full |
			 SUPPORTED_Autoneg);
		cmd->advertising = cmd->supported;
		ethtool_cmd_speed_set(cmd, 0);
		cmd->duplex = cmd->port = cmd->phy_address =
			cmd->transceiver = cmd->autoneg = 0;

		/* serdes means usually a Fibre connector, with most fixed */
		if (gp->phy_type == phy_serdes) {
			cmd->port = PORT_FIBRE;
			cmd->supported = (SUPPORTED_1000baseT_Half |
				SUPPORTED_1000baseT_Full |
				SUPPORTED_FIBRE | SUPPORTED_Autoneg |
				SUPPORTED_Pause | SUPPORTED_Asym_Pause);
			cmd->advertising = cmd->supported;
			cmd->transceiver = XCVR_INTERNAL;
			if (gp->lstate == link_up)
				ethtool_cmd_speed_set(cmd, SPEED_1000);
			cmd->duplex = DUPLEX_FULL;
			cmd->autoneg = 1;
		}
	}
	cmd->maxtxpkt = cmd->maxrxpkt = 0;

	return 0;
}

static int gem_set_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct gem *gp = netdev_priv(dev);
	u32 speed = ethtool_cmd_speed(cmd);

	/* Verify the settings we care about. */
	if (cmd->autoneg != AUTONEG_ENABLE &&
	    cmd->autoneg != AUTONEG_DISABLE)
		return -EINVAL;

	if (cmd->autoneg == AUTONEG_ENABLE &&
	    cmd->advertising == 0)
		return -EINVAL;

	if (cmd->autoneg == AUTONEG_DISABLE &&
	    ((speed != SPEED_1000 &&
	      speed != SPEED_100 &&
	      speed != SPEED_10) ||
	     (cmd->duplex != DUPLEX_HALF &&
	      cmd->duplex != DUPLEX_FULL)))
		return -EINVAL;

	/* Apply settings and restart link process. */
	if (netif_device_present(gp->dev)) {
		del_timer_sync(&gp->link_timer);
		gem_begin_auto_negotiation(gp, cmd);
	}

	return 0;
}

static int gem_nway_reset(struct net_device *dev)
{
	struct gem *gp = netdev_priv(dev);

	if (!gp->want_autoneg)
		return -EINVAL;

	/* Restart link process  */
	if (netif_device_present(gp->dev)) {
		del_timer_sync(&gp->link_timer);
		gem_begin_auto_negotiation(gp, NULL);
	}

	return 0;
}

static u32 gem_get_msglevel(struct net_device *dev)
{
	struct gem *gp = netdev_priv(dev);
	return gp->msg_enable;
}

static void gem_set_msglevel(struct net_device *dev, u32 value)
{
	struct gem *gp = netdev_priv(dev);
	gp->msg_enable = value;
}


/* Add more when I understand how to program the chip */
/* like WAKE_UCAST | WAKE_MCAST | WAKE_BCAST */

#define WOL_SUPPORTED_MASK	(WAKE_MAGIC)

static void gem_get_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	struct gem *gp = netdev_priv(dev);

	/* Add more when I understand how to program the chip */
	if (gp->has_wol) {
		wol->supported = WOL_SUPPORTED_MASK;
		wol->wolopts = gp->wake_on_lan;
	} else {
		wol->supported = 0;
		wol->wolopts = 0;
	}
}

static int gem_set_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	struct gem *gp = netdev_priv(dev);

	if (!gp->has_wol)
		return -EOPNOTSUPP;
	gp->wake_on_lan = wol->wolopts & WOL_SUPPORTED_MASK;
	return 0;
}

static const struct ethtool_ops gem_ethtool_ops = {
	.get_drvinfo		= gem_get_drvinfo,
	.get_link		= ethtool_op_get_link,
	.get_settings		= gem_get_settings,
	.set_settings		= gem_set_settings,
	.nway_reset		= gem_nway_reset,
	.get_msglevel		= gem_get_msglevel,
	.set_msglevel		= gem_set_msglevel,
	.get_wol		= gem_get_wol,
	.set_wol		= gem_set_wol,
};

static int gem_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct gem *gp = netdev_priv(dev);
	struct mii_ioctl_data *data = if_mii(ifr);
	int rc = -EOPNOTSUPP;

	/* For SIOCGMIIREG and SIOCSMIIREG the core checks for us that
	 * netif_device_present() is true and holds rtnl_lock for us
	 * so we have nothing to worry about
	 */

	switch (cmd) {
	case SIOCGMIIPHY:		/* Get address of MII PHY in use. */
		data->phy_id = gp->mii_phy_addr;
		/* Fallthrough... */

	case SIOCGMIIREG:		/* Read MII PHY register. */
		data->val_out = __phy_read(gp, data->phy_id & 0x1f,
					   data->reg_num & 0x1f);
		rc = 0;
		break;

	case SIOCSMIIREG:		/* Write MII PHY register. */
		__phy_write(gp, data->phy_id & 0x1f, data->reg_num & 0x1f,
			    data->val_in);
		rc = 0;
		break;
	}
	return rc;
}

#if (!defined(CONFIG_SPARC) && !defined(CONFIG_PPC_PMAC))
/* Fetch MAC address from vital product data of PCI ROM. */
static int find_eth_addr_in_vpd(void __iomem *rom_base, int len, unsigned char *dev_addr)
{
	int this_offset;

	for (this_offset = 0x20; this_offset < len; this_offset++) {
		void __iomem *p = rom_base + this_offset;
		int i;

		if (readb(p + 0) != 0x90 ||
		    readb(p + 1) != 0x00 ||
		    readb(p + 2) != 0x09 ||
		    readb(p + 3) != 0x4e ||
		    readb(p + 4) != 0x41 ||
		    readb(p + 5) != 0x06)
			continue;

		this_offset += 6;
		p += 6;

		for (i = 0; i < 6; i++)
			dev_addr[i] = readb(p + i);
		return 1;
	}
	return 0;
}

static void get_gem_mac_nonobp(struct pci_dev *pdev, unsigned char *dev_addr)
{
	size_t size;
	void __iomem *p = pci_map_rom(pdev, &size);

	if (p) {
			int found;

		found = readb(p) == 0x55 &&
			readb(p + 1) == 0xaa &&
			find_eth_addr_in_vpd(p, (64 * 1024), dev_addr);
		pci_unmap_rom(pdev, p);
		if (found)
			return;
	}

	/* Sun MAC prefix then 3 random bytes. */
	dev_addr[0] = 0x08;
	dev_addr[1] = 0x00;
	dev_addr[2] = 0x20;
	get_random_bytes(dev_addr + 3, 3);
}
#endif /* not Sparc and not PPC */

static int __devinit gem_get_device_address(struct gem *gp)
{
#if defined(CONFIG_SPARC) || defined(CONFIG_PPC_PMAC)
	struct net_device *dev = gp->dev;
	const unsigned char *addr;

	addr = of_get_property(gp->of_node, "local-mac-address", NULL);
	if (addr == NULL) {
#ifdef CONFIG_SPARC
		addr = idprom->id_ethaddr;
#else
		printk("\n");
		pr_err("%s: can't get mac-address\n", dev->name);
		return -1;
#endif
	}
	memcpy(dev->dev_addr, addr, 6);
#else
	get_gem_mac_nonobp(gp->pdev, gp->dev->dev_addr);
#endif
	return 0;
}

static void gem_remove_one(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);

	if (dev) {
		struct gem *gp = netdev_priv(dev);

		unregister_netdev(dev);

		/* Ensure reset task is truely gone */
		cancel_work_sync(&gp->reset_task);

		/* Free resources */
		pci_free_consistent(pdev,
				    sizeof(struct gem_init_block),
				    gp->init_block,
				    gp->gblock_dvma);
		iounmap(gp->regs);
		pci_release_regions(pdev);
		free_netdev(dev);

		pci_set_drvdata(pdev, NULL);
	}
}

static const struct net_device_ops gem_netdev_ops = {
	.ndo_open		= gem_open,
	.ndo_stop		= gem_close,
	.ndo_start_xmit		= gem_start_xmit,
	.ndo_get_stats		= gem_get_stats,
	.ndo_set_rx_mode	= gem_set_multicast,
	.ndo_do_ioctl		= gem_ioctl,
	.ndo_tx_timeout		= gem_tx_timeout,
	.ndo_change_mtu		= gem_change_mtu,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address    = gem_set_mac_address,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller    = gem_poll_controller,
#endif
};

static int __devinit gem_init_one(struct pci_dev *pdev,
				  const struct pci_device_id *ent)
{
	unsigned long gemreg_base, gemreg_len;
	struct net_device *dev;
	struct gem *gp;
	int err, pci_using_dac;

	printk_once(KERN_INFO "%s", version);

	/* Apple gmac note: during probe, the chip is powered up by
	 * the arch code to allow the code below to work (and to let
	 * the chip be probed on the config space. It won't stay powered
	 * up until the interface is brought up however, so we can't rely
	 * on register configuration done at this point.
	 */
	err = pci_enable_device(pdev);
	if (err) {
		pr_err("Cannot enable MMIO operation, aborting\n");
		return err;
	}
	pci_set_master(pdev);

	/* Configure DMA attributes. */

	/* All of the GEM documentation states that 64-bit DMA addressing
	 * is fully supported and should work just fine.  However the
	 * front end for RIO based GEMs is different and only supports
	 * 32-bit addressing.
	 *
	 * For now we assume the various PPC GEMs are 32-bit only as well.
	 */
	if (pdev->vendor == PCI_VENDOR_ID_SUN &&
	    pdev->device == PCI_DEVICE_ID_SUN_GEM &&
	    !pci_set_dma_mask(pdev, DMA_BIT_MASK(64))) {
		pci_using_dac = 1;
	} else {
		err = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
		if (err) {
			pr_err("No usable DMA configuration, aborting\n");
			goto err_disable_device;
		}
		pci_using_dac = 0;
	}

	gemreg_base = pci_resource_start(pdev, 0);
	gemreg_len = pci_resource_len(pdev, 0);

	if ((pci_resource_flags(pdev, 0) & IORESOURCE_IO) != 0) {
		pr_err("Cannot find proper PCI device base address, aborting\n");
		err = -ENODEV;
		goto err_disable_device;
	}

	dev = alloc_etherdev(sizeof(*gp));
	if (!dev) {
		pr_err("Etherdev alloc failed, aborting\n");
		err = -ENOMEM;
		goto err_disable_device;
	}
	SET_NETDEV_DEV(dev, &pdev->dev);

	gp = netdev_priv(dev);

	err = pci_request_regions(pdev, DRV_NAME);
	if (err) {
		pr_err("Cannot obtain PCI resources, aborting\n");
		goto err_out_free_netdev;
	}

	gp->pdev = pdev;
	dev->base_addr = (long) pdev;
	gp->dev = dev;

	gp->msg_enable = DEFAULT_MSG;

	init_timer(&gp->link_timer);
	gp->link_timer.function = gem_link_timer;
	gp->link_timer.data = (unsigned long) gp;

	INIT_WORK(&gp->reset_task, gem_reset_task);

	gp->lstate = link_down;
	gp->timer_ticks = 0;
	netif_carrier_off(dev);

	gp->regs = ioremap(gemreg_base, gemreg_len);
	if (!gp->regs) {
		pr_err("Cannot map device registers, aborting\n");
		err = -EIO;
		goto err_out_free_res;
	}

	/* On Apple, we want a reference to the Open Firmware device-tree
	 * node. We use it for clock control.
	 */
#if defined(CONFIG_PPC_PMAC) || defined(CONFIG_SPARC)
	gp->of_node = pci_device_to_OF_node(pdev);
#endif

	/* Only Apple version supports WOL afaik */
	if (pdev->vendor == PCI_VENDOR_ID_APPLE)
		gp->has_wol = 1;

	/* Make sure cell is enabled */
	gem_get_cell(gp);

	/* Make sure everything is stopped and in init state */
	gem_reset(gp);

	/* Fill up the mii_phy structure (even if we won't use it) */
	gp->phy_mii.dev = dev;
	gp->phy_mii.mdio_read = _phy_read;
	gp->phy_mii.mdio_write = _phy_write;
#ifdef CONFIG_PPC_PMAC
	gp->phy_mii.platform_data = gp->of_node;
#endif
	/* By default, we start with autoneg */
	gp->want_autoneg = 1;

	/* Check fifo sizes, PHY type, etc... */
	if (gem_check_invariants(gp)) {
		err = -ENODEV;
		goto err_out_iounmap;
	}

	/* It is guaranteed that the returned buffer will be at least
	 * PAGE_SIZE aligned.
	 */
	gp->init_block = (struct gem_init_block *)
		pci_alloc_consistent(pdev, sizeof(struct gem_init_block),
				     &gp->gblock_dvma);
	if (!gp->init_block) {
		pr_err("Cannot allocate init block, aborting\n");
		err = -ENOMEM;
		goto err_out_iounmap;
	}

	if (gem_get_device_address(gp))
		goto err_out_free_consistent;

	dev->netdev_ops = &gem_netdev_ops;
	netif_napi_add(dev, &gp->napi, gem_poll, 64);
	dev->ethtool_ops = &gem_ethtool_ops;
	dev->watchdog_timeo = 5 * HZ;
	dev->irq = pdev->irq;
	dev->dma = 0;

	/* Set that now, in case PM kicks in now */
	pci_set_drvdata(pdev, dev);

	/* We can do scatter/gather and HW checksum */
	dev->hw_features = NETIF_F_SG | NETIF_F_HW_CSUM;
	dev->features |= dev->hw_features | NETIF_F_RXCSUM;
	if (pci_using_dac)
		dev->features |= NETIF_F_HIGHDMA;

	/* Register with kernel */
	if (register_netdev(dev)) {
		pr_err("Cannot register net device, aborting\n");
		err = -ENOMEM;
		goto err_out_free_consistent;
	}

	/* Undo the get_cell with appropriate locking (we could use
	 * ndo_init/uninit but that would be even more clumsy imho)
	 */
	rtnl_lock();
	gem_put_cell(gp);
	rtnl_unlock();

	netdev_info(dev, "Sun GEM (PCI) 10/100/1000BaseT Ethernet %pM\n",
		    dev->dev_addr);
	return 0;

err_out_free_consistent:
	gem_remove_one(pdev);
err_out_iounmap:
	gem_put_cell(gp);
	iounmap(gp->regs);

err_out_free_res:
	pci_release_regions(pdev);

err_out_free_netdev:
	free_netdev(dev);
err_disable_device:
	pci_disable_device(pdev);
	return err;

}


static struct pci_driver gem_driver = {
	.name		= GEM_MODULE_NAME,
	.id_table	= gem_pci_tbl,
	.probe		= gem_init_one,
	.remove		= gem_remove_one,
#ifdef CONFIG_PM
	.suspend	= gem_suspend,
	.resume		= gem_resume,
#endif /* CONFIG_PM */
};

static int __init gem_init(void)
{
	return pci_register_driver(&gem_driver);
}

static void __exit gem_cleanup(void)
{
	pci_unregister_driver(&gem_driver);
}

module_init(gem_init);
module_exit(gem_cleanup);
