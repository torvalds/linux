/*
 * drivers/net/ibm_emac/ibm_emac_core.c
 *
 * Driver for PowerPC 4xx on-chip ethernet controller.
 *
 * Copyright (c) 2004, 2005 Zultys Technologies.
 * Eugene Surovegin <eugene.surovegin@zultys.com> or <ebs@ebshome.net>
 *
 * Based on original work by
 * 	Matt Porter <mporter@kernel.crashing.org>
 *	(c) 2003 Benjamin Herrenschmidt <benh@kernel.crashing.org>
 *      Armin Kuster <akuster@mvista.com>
 * 	Johnnie Peters <jpeters@mvista.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/crc32.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/bitops.h>

#include <asm/processor.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/uaccess.h>
#include <asm/ocp.h>

#include "ibm_emac_core.h"
#include "ibm_emac_debug.h"

/*
 * Lack of dma_unmap_???? calls is intentional.
 *
 * API-correct usage requires additional support state information to be 
 * maintained for every RX and TX buffer descriptor (BD). Unfortunately, due to
 * EMAC design (e.g. TX buffer passed from network stack can be split into
 * several BDs, dma_map_single/dma_map_page can be used to map particular BD),
 * maintaining such information will add additional overhead.
 * Current DMA API implementation for 4xx processors only ensures cache coherency
 * and dma_unmap_???? routines are empty and are likely to stay this way.
 * I decided to omit dma_unmap_??? calls because I don't want to add additional
 * complexity just for the sake of following some abstract API, when it doesn't
 * add any real benefit to the driver. I understand that this decision maybe 
 * controversial, but I really tried to make code API-correct and efficient 
 * at the same time and didn't come up with code I liked :(.                --ebs
 */

#define DRV_NAME        "emac"
#define DRV_VERSION     "3.53"
#define DRV_DESC        "PPC 4xx OCP EMAC driver"

MODULE_DESCRIPTION(DRV_DESC);
MODULE_AUTHOR
    ("Eugene Surovegin <eugene.surovegin@zultys.com> or <ebs@ebshome.net>");
MODULE_LICENSE("GPL");

/* minimum number of free TX descriptors required to wake up TX process */
#define EMAC_TX_WAKEUP_THRESH		(NUM_TX_BUFF / 4)

/* If packet size is less than this number, we allocate small skb and copy packet 
 * contents into it instead of just sending original big skb up
 */
#define EMAC_RX_COPY_THRESH		CONFIG_IBM_EMAC_RX_COPY_THRESHOLD

/* Since multiple EMACs share MDIO lines in various ways, we need
 * to avoid re-using the same PHY ID in cases where the arch didn't
 * setup precise phy_map entries
 */
static u32 busy_phy_map;

#if defined(CONFIG_IBM_EMAC_PHY_RX_CLK_FIX) && (defined(CONFIG_405EP) || defined(CONFIG_440EP))
/* 405EP has "EMAC to PHY Control Register" (CPC0_EPCTL) which can help us
 * with PHY RX clock problem.
 * 440EP has more sane SDR0_MFR register implementation than 440GX, which
 * also allows controlling each EMAC clock
 */
static inline void EMAC_RX_CLK_TX(int idx)
{
	unsigned long flags;
	local_irq_save(flags);

#if defined(CONFIG_405EP)
	mtdcr(0xf3, mfdcr(0xf3) | (1 << idx));
#else /* CONFIG_440EP */
	SDR_WRITE(DCRN_SDR_MFR, SDR_READ(DCRN_SDR_MFR) | (0x08000000 >> idx));
#endif

	local_irq_restore(flags);
}

static inline void EMAC_RX_CLK_DEFAULT(int idx)
{
	unsigned long flags;
	local_irq_save(flags);

#if defined(CONFIG_405EP)
	mtdcr(0xf3, mfdcr(0xf3) & ~(1 << idx));
#else /* CONFIG_440EP */
	SDR_WRITE(DCRN_SDR_MFR, SDR_READ(DCRN_SDR_MFR) & ~(0x08000000 >> idx));
#endif

	local_irq_restore(flags);
}
#else
#define EMAC_RX_CLK_TX(idx)		((void)0)
#define EMAC_RX_CLK_DEFAULT(idx)	((void)0)
#endif

#if defined(CONFIG_IBM_EMAC_PHY_RX_CLK_FIX) && defined(CONFIG_440GX)
/* We can switch Ethernet clock to the internal source through SDR0_MFR[ECS],
 * unfortunately this is less flexible than 440EP case, because it's a global 
 * setting for all EMACs, therefore we do this clock trick only during probe.
 */
#define EMAC_CLK_INTERNAL		SDR_WRITE(DCRN_SDR_MFR, \
					    SDR_READ(DCRN_SDR_MFR) | 0x08000000)
#define EMAC_CLK_EXTERNAL		SDR_WRITE(DCRN_SDR_MFR, \
					    SDR_READ(DCRN_SDR_MFR) & ~0x08000000)
#else
#define EMAC_CLK_INTERNAL		((void)0)
#define EMAC_CLK_EXTERNAL		((void)0)
#endif

/* I don't want to litter system log with timeout errors 
 * when we have brain-damaged PHY.
 */
static inline void emac_report_timeout_error(struct ocp_enet_private *dev,
					     const char *error)
{
#if defined(CONFIG_IBM_EMAC_PHY_RX_CLK_FIX)
	DBG("%d: %s" NL, dev->def->index, error);
#else
	if (net_ratelimit())
		printk(KERN_ERR "emac%d: %s\n", dev->def->index, error);
#endif
}

/* PHY polling intervals */
#define PHY_POLL_LINK_ON	HZ
#define PHY_POLL_LINK_OFF	(HZ / 5)

/* Please, keep in sync with struct ibm_emac_stats/ibm_emac_error_stats */
static const char emac_stats_keys[EMAC_ETHTOOL_STATS_COUNT][ETH_GSTRING_LEN] = {
	"rx_packets", "rx_bytes", "tx_packets", "tx_bytes", "rx_packets_csum",
	"tx_packets_csum", "tx_undo", "rx_dropped_stack", "rx_dropped_oom",
	"rx_dropped_error", "rx_dropped_resize", "rx_dropped_mtu",
	"rx_stopped", "rx_bd_errors", "rx_bd_overrun", "rx_bd_bad_packet",
	"rx_bd_runt_packet", "rx_bd_short_event", "rx_bd_alignment_error",
	"rx_bd_bad_fcs", "rx_bd_packet_too_long", "rx_bd_out_of_range",
	"rx_bd_in_range", "rx_parity", "rx_fifo_overrun", "rx_overrun",
	"rx_bad_packet", "rx_runt_packet", "rx_short_event",
	"rx_alignment_error", "rx_bad_fcs", "rx_packet_too_long",
	"rx_out_of_range", "rx_in_range", "tx_dropped", "tx_bd_errors",
	"tx_bd_bad_fcs", "tx_bd_carrier_loss", "tx_bd_excessive_deferral",
	"tx_bd_excessive_collisions", "tx_bd_late_collision",
	"tx_bd_multple_collisions", "tx_bd_single_collision",
	"tx_bd_underrun", "tx_bd_sqe", "tx_parity", "tx_underrun", "tx_sqe",
	"tx_errors"
};

static irqreturn_t emac_irq(int irq, void *dev_instance, struct pt_regs *regs);
static void emac_clean_tx_ring(struct ocp_enet_private *dev);

static inline int emac_phy_supports_gige(int phy_mode)
{
	return  phy_mode == PHY_MODE_GMII ||
		phy_mode == PHY_MODE_RGMII ||
		phy_mode == PHY_MODE_TBI ||
		phy_mode == PHY_MODE_RTBI;
}

static inline int emac_phy_gpcs(int phy_mode)
{
	return  phy_mode == PHY_MODE_TBI ||
		phy_mode == PHY_MODE_RTBI;
}

static inline void emac_tx_enable(struct ocp_enet_private *dev)
{
	struct emac_regs *p = dev->emacp;
	unsigned long flags;
	u32 r;

	local_irq_save(flags);

	DBG("%d: tx_enable" NL, dev->def->index);

	r = in_be32(&p->mr0);
	if (!(r & EMAC_MR0_TXE))
		out_be32(&p->mr0, r | EMAC_MR0_TXE);
	local_irq_restore(flags);
}

static void emac_tx_disable(struct ocp_enet_private *dev)
{
	struct emac_regs *p = dev->emacp;
	unsigned long flags;
	u32 r;

	local_irq_save(flags);

	DBG("%d: tx_disable" NL, dev->def->index);

	r = in_be32(&p->mr0);
	if (r & EMAC_MR0_TXE) {
		int n = 300;
		out_be32(&p->mr0, r & ~EMAC_MR0_TXE);
		while (!(in_be32(&p->mr0) & EMAC_MR0_TXI) && n)
			--n;
		if (unlikely(!n))
			emac_report_timeout_error(dev, "TX disable timeout");
	}
	local_irq_restore(flags);
}

static void emac_rx_enable(struct ocp_enet_private *dev)
{
	struct emac_regs *p = dev->emacp;
	unsigned long flags;
	u32 r;

	local_irq_save(flags);
	if (unlikely(dev->commac.rx_stopped))
		goto out;

	DBG("%d: rx_enable" NL, dev->def->index);

	r = in_be32(&p->mr0);
	if (!(r & EMAC_MR0_RXE)) {
		if (unlikely(!(r & EMAC_MR0_RXI))) {
			/* Wait if previous async disable is still in progress */
			int n = 100;
			while (!(r = in_be32(&p->mr0) & EMAC_MR0_RXI) && n)
				--n;
			if (unlikely(!n))
				emac_report_timeout_error(dev,
							  "RX disable timeout");
		}
		out_be32(&p->mr0, r | EMAC_MR0_RXE);
	}
      out:
	local_irq_restore(flags);
}

static void emac_rx_disable(struct ocp_enet_private *dev)
{
	struct emac_regs *p = dev->emacp;
	unsigned long flags;
	u32 r;

	local_irq_save(flags);

	DBG("%d: rx_disable" NL, dev->def->index);

	r = in_be32(&p->mr0);
	if (r & EMAC_MR0_RXE) {
		int n = 300;
		out_be32(&p->mr0, r & ~EMAC_MR0_RXE);
		while (!(in_be32(&p->mr0) & EMAC_MR0_RXI) && n)
			--n;
		if (unlikely(!n))
			emac_report_timeout_error(dev, "RX disable timeout");
	}
	local_irq_restore(flags);
}

static inline void emac_rx_disable_async(struct ocp_enet_private *dev)
{
	struct emac_regs *p = dev->emacp;
	unsigned long flags;
	u32 r;

	local_irq_save(flags);

	DBG("%d: rx_disable_async" NL, dev->def->index);

	r = in_be32(&p->mr0);
	if (r & EMAC_MR0_RXE)
		out_be32(&p->mr0, r & ~EMAC_MR0_RXE);
	local_irq_restore(flags);
}

static int emac_reset(struct ocp_enet_private *dev)
{
	struct emac_regs *p = dev->emacp;
	unsigned long flags;
	int n = 20;

	DBG("%d: reset" NL, dev->def->index);

	local_irq_save(flags);

	if (!dev->reset_failed) {
		/* 40x erratum suggests stopping RX channel before reset,
		 * we stop TX as well
		 */
		emac_rx_disable(dev);
		emac_tx_disable(dev);
	}

	out_be32(&p->mr0, EMAC_MR0_SRST);
	while ((in_be32(&p->mr0) & EMAC_MR0_SRST) && n)
		--n;
	local_irq_restore(flags);

	if (n) {
		dev->reset_failed = 0;
		return 0;
	} else {
		emac_report_timeout_error(dev, "reset timeout");
		dev->reset_failed = 1;
		return -ETIMEDOUT;
	}
}

static void emac_hash_mc(struct ocp_enet_private *dev)
{
	struct emac_regs *p = dev->emacp;
	u16 gaht[4] = { 0 };
	struct dev_mc_list *dmi;

	DBG("%d: hash_mc %d" NL, dev->def->index, dev->ndev->mc_count);

	for (dmi = dev->ndev->mc_list; dmi; dmi = dmi->next) {
		int bit;
		DBG2("%d: mc %02x:%02x:%02x:%02x:%02x:%02x" NL,
		     dev->def->index,
		     dmi->dmi_addr[0], dmi->dmi_addr[1], dmi->dmi_addr[2],
		     dmi->dmi_addr[3], dmi->dmi_addr[4], dmi->dmi_addr[5]);

		bit = 63 - (ether_crc(ETH_ALEN, dmi->dmi_addr) >> 26);
		gaht[bit >> 4] |= 0x8000 >> (bit & 0x0f);
	}
	out_be32(&p->gaht1, gaht[0]);
	out_be32(&p->gaht2, gaht[1]);
	out_be32(&p->gaht3, gaht[2]);
	out_be32(&p->gaht4, gaht[3]);
}

static inline u32 emac_iff2rmr(struct net_device *ndev)
{
	u32 r = EMAC_RMR_SP | EMAC_RMR_SFCS | EMAC_RMR_IAE | EMAC_RMR_BAE |
	    EMAC_RMR_BASE;

	if (ndev->flags & IFF_PROMISC)
		r |= EMAC_RMR_PME;
	else if (ndev->flags & IFF_ALLMULTI || ndev->mc_count > 32)
		r |= EMAC_RMR_PMME;
	else if (ndev->mc_count > 0)
		r |= EMAC_RMR_MAE;

	return r;
}

static inline int emac_opb_mhz(void)
{
	return (ocp_sys_info.opb_bus_freq + 500000) / 1000000;
}

/* BHs disabled */
static int emac_configure(struct ocp_enet_private *dev)
{
	struct emac_regs *p = dev->emacp;
	struct net_device *ndev = dev->ndev;
	int gige;
	u32 r;

	DBG("%d: configure" NL, dev->def->index);

	if (emac_reset(dev) < 0)
		return -ETIMEDOUT;

	tah_reset(dev->tah_dev);

	/* Mode register */
	r = EMAC_MR1_BASE(emac_opb_mhz()) | EMAC_MR1_VLE | EMAC_MR1_IST;
	if (dev->phy.duplex == DUPLEX_FULL)
		r |= EMAC_MR1_FDE;
	switch (dev->phy.speed) {
	case SPEED_1000:
		if (emac_phy_gpcs(dev->phy.mode)) {
			r |= EMAC_MR1_MF_1000GPCS |
			    EMAC_MR1_MF_IPPA(dev->phy.address);

			/* Put some arbitrary OUI, Manuf & Rev IDs so we can
			 * identify this GPCS PHY later.
			 */
			out_be32(&p->ipcr, 0xdeadbeef);
		} else
			r |= EMAC_MR1_MF_1000;
		r |= EMAC_MR1_RFS_16K;
		gige = 1;
		
		if (dev->ndev->mtu > ETH_DATA_LEN)
			r |= EMAC_MR1_JPSM;
		break;
	case SPEED_100:
		r |= EMAC_MR1_MF_100;
		/* Fall through */
	default:
		r |= EMAC_MR1_RFS_4K;
		gige = 0;
		break;
	}

	if (dev->rgmii_dev)
		rgmii_set_speed(dev->rgmii_dev, dev->rgmii_input,
				dev->phy.speed);
	else
		zmii_set_speed(dev->zmii_dev, dev->zmii_input, dev->phy.speed);

#if !defined(CONFIG_40x)
	/* on 40x erratum forces us to NOT use integrated flow control, 
	 * let's hope it works on 44x ;)
	 */
	if (dev->phy.duplex == DUPLEX_FULL) {
		if (dev->phy.pause)
			r |= EMAC_MR1_EIFC | EMAC_MR1_APP;
		else if (dev->phy.asym_pause)
			r |= EMAC_MR1_APP;
	}
#endif
	out_be32(&p->mr1, r);

	/* Set individual MAC address */
	out_be32(&p->iahr, (ndev->dev_addr[0] << 8) | ndev->dev_addr[1]);
	out_be32(&p->ialr, (ndev->dev_addr[2] << 24) |
		 (ndev->dev_addr[3] << 16) | (ndev->dev_addr[4] << 8) |
		 ndev->dev_addr[5]);

	/* VLAN Tag Protocol ID */
	out_be32(&p->vtpid, 0x8100);

	/* Receive mode register */
	r = emac_iff2rmr(ndev);
	if (r & EMAC_RMR_MAE)
		emac_hash_mc(dev);
	out_be32(&p->rmr, r);

	/* FIFOs thresholds */
	r = EMAC_TMR1((EMAC_MAL_BURST_SIZE / EMAC_FIFO_ENTRY_SIZE) + 1,
		      EMAC_TX_FIFO_SIZE / 2 / EMAC_FIFO_ENTRY_SIZE);
	out_be32(&p->tmr1, r);
	out_be32(&p->trtr, EMAC_TRTR(EMAC_TX_FIFO_SIZE / 2));

	/* PAUSE frame is sent when RX FIFO reaches its high-water mark,
	   there should be still enough space in FIFO to allow the our link
	   partner time to process this frame and also time to send PAUSE 
	   frame itself.

	   Here is the worst case scenario for the RX FIFO "headroom"
	   (from "The Switch Book") (100Mbps, without preamble, inter-frame gap):

	   1) One maximum-length frame on TX                    1522 bytes
	   2) One PAUSE frame time                                64 bytes
	   3) PAUSE frame decode time allowance                   64 bytes
	   4) One maximum-length frame on RX                    1522 bytes
	   5) Round-trip propagation delay of the link (100Mb)    15 bytes
	   ----------       
	   3187 bytes

	   I chose to set high-water mark to RX_FIFO_SIZE / 4 (1024 bytes)
	   low-water mark  to RX_FIFO_SIZE / 8 (512 bytes)
	 */
	r = EMAC_RWMR(EMAC_RX_FIFO_SIZE(gige) / 8 / EMAC_FIFO_ENTRY_SIZE,
		      EMAC_RX_FIFO_SIZE(gige) / 4 / EMAC_FIFO_ENTRY_SIZE);
	out_be32(&p->rwmr, r);

	/* Set PAUSE timer to the maximum */
	out_be32(&p->ptr, 0xffff);

	/* IRQ sources */
	out_be32(&p->iser, EMAC_ISR_TXPE | EMAC_ISR_RXPE | /* EMAC_ISR_TXUE |
		 EMAC_ISR_RXOE | */ EMAC_ISR_OVR | EMAC_ISR_BP | EMAC_ISR_SE |
		 EMAC_ISR_ALE | EMAC_ISR_BFCS | EMAC_ISR_PTLE | EMAC_ISR_ORE |
		 EMAC_ISR_IRE | EMAC_ISR_TE);
		 
	/* We need to take GPCS PHY out of isolate mode after EMAC reset */
	if (emac_phy_gpcs(dev->phy.mode)) 
		mii_reset_phy(&dev->phy);
		 
	return 0;
}

/* BHs disabled */
static void emac_reinitialize(struct ocp_enet_private *dev)
{
	DBG("%d: reinitialize" NL, dev->def->index);

	if (!emac_configure(dev)) {
		emac_tx_enable(dev);
		emac_rx_enable(dev);
	}
}

/* BHs disabled */
static void emac_full_tx_reset(struct net_device *ndev)
{
	struct ocp_enet_private *dev = ndev->priv;
	struct ocp_func_emac_data *emacdata = dev->def->additions;

	DBG("%d: full_tx_reset" NL, dev->def->index);

	emac_tx_disable(dev);
	mal_disable_tx_channel(dev->mal, emacdata->mal_tx_chan);
	emac_clean_tx_ring(dev);
	dev->tx_cnt = dev->tx_slot = dev->ack_slot = 0;

	emac_configure(dev);

	mal_enable_tx_channel(dev->mal, emacdata->mal_tx_chan);
	emac_tx_enable(dev);
	emac_rx_enable(dev);

	netif_wake_queue(ndev);
}

static int __emac_mdio_read(struct ocp_enet_private *dev, u8 id, u8 reg)
{
	struct emac_regs *p = dev->emacp;
	u32 r;
	int n;

	DBG2("%d: mdio_read(%02x,%02x)" NL, dev->def->index, id, reg);

	/* Enable proper MDIO port */
	zmii_enable_mdio(dev->zmii_dev, dev->zmii_input);

	/* Wait for management interface to become idle */
	n = 10;
	while (!(in_be32(&p->stacr) & EMAC_STACR_OC)) {
		udelay(1);
		if (!--n)
			goto to;
	}

	/* Issue read command */
	out_be32(&p->stacr,
		 EMAC_STACR_BASE(emac_opb_mhz()) | EMAC_STACR_STAC_READ |
		 (reg & EMAC_STACR_PRA_MASK)
		 | ((id & EMAC_STACR_PCDA_MASK) << EMAC_STACR_PCDA_SHIFT));

	/* Wait for read to complete */
	n = 100;
	while (!((r = in_be32(&p->stacr)) & EMAC_STACR_OC)) {
		udelay(1);
		if (!--n)
			goto to;
	}

	if (unlikely(r & EMAC_STACR_PHYE)) {
		DBG("%d: mdio_read(%02x, %02x) failed" NL, dev->def->index,
		    id, reg);
		return -EREMOTEIO;
	}

	r = ((r >> EMAC_STACR_PHYD_SHIFT) & EMAC_STACR_PHYD_MASK);
	DBG2("%d: mdio_read -> %04x" NL, dev->def->index, r);
	return r;
      to:
	DBG("%d: MII management interface timeout (read)" NL, dev->def->index);
	return -ETIMEDOUT;
}

static void __emac_mdio_write(struct ocp_enet_private *dev, u8 id, u8 reg,
			      u16 val)
{
	struct emac_regs *p = dev->emacp;
	int n;

	DBG2("%d: mdio_write(%02x,%02x,%04x)" NL, dev->def->index, id, reg,
	     val);

	/* Enable proper MDIO port */
	zmii_enable_mdio(dev->zmii_dev, dev->zmii_input);

	/* Wait for management interface to be idle */
	n = 10;
	while (!(in_be32(&p->stacr) & EMAC_STACR_OC)) {
		udelay(1);
		if (!--n)
			goto to;
	}

	/* Issue write command */
	out_be32(&p->stacr,
		 EMAC_STACR_BASE(emac_opb_mhz()) | EMAC_STACR_STAC_WRITE |
		 (reg & EMAC_STACR_PRA_MASK) |
		 ((id & EMAC_STACR_PCDA_MASK) << EMAC_STACR_PCDA_SHIFT) |
		 (val << EMAC_STACR_PHYD_SHIFT));

	/* Wait for write to complete */
	n = 100;
	while (!(in_be32(&p->stacr) & EMAC_STACR_OC)) {
		udelay(1);
		if (!--n)
			goto to;
	}
	return;
      to:
	DBG("%d: MII management interface timeout (write)" NL, dev->def->index);
}

static int emac_mdio_read(struct net_device *ndev, int id, int reg)
{
	struct ocp_enet_private *dev = ndev->priv;
	int res;

	local_bh_disable();
	res = __emac_mdio_read(dev->mdio_dev ? dev->mdio_dev : dev, (u8) id,
			       (u8) reg);
	local_bh_enable();
	return res;
}

static void emac_mdio_write(struct net_device *ndev, int id, int reg, int val)
{
	struct ocp_enet_private *dev = ndev->priv;

	local_bh_disable();
	__emac_mdio_write(dev->mdio_dev ? dev->mdio_dev : dev, (u8) id,
			  (u8) reg, (u16) val);
	local_bh_enable();
}

/* BHs disabled */
static void emac_set_multicast_list(struct net_device *ndev)
{
	struct ocp_enet_private *dev = ndev->priv;
	struct emac_regs *p = dev->emacp;
	u32 rmr = emac_iff2rmr(ndev);

	DBG("%d: multicast %08x" NL, dev->def->index, rmr);
	BUG_ON(!netif_running(dev->ndev));

	/* I decided to relax register access rules here to avoid
	 * full EMAC reset.
	 *
	 * There is a real problem with EMAC4 core if we use MWSW_001 bit 
	 * in MR1 register and do a full EMAC reset.
	 * One TX BD status update is delayed and, after EMAC reset, it 
	 * never happens, resulting in TX hung (it'll be recovered by TX 
	 * timeout handler eventually, but this is just gross).
	 * So we either have to do full TX reset or try to cheat here :)
	 *
	 * The only required change is to RX mode register, so I *think* all
	 * we need is just to stop RX channel. This seems to work on all
	 * tested SoCs.                                                --ebs
	 */
	emac_rx_disable(dev);
	if (rmr & EMAC_RMR_MAE)
		emac_hash_mc(dev);
	out_be32(&p->rmr, rmr);
	emac_rx_enable(dev);
}

/* BHs disabled */
static int emac_resize_rx_ring(struct ocp_enet_private *dev, int new_mtu)
{
	struct ocp_func_emac_data *emacdata = dev->def->additions;
	int rx_sync_size = emac_rx_sync_size(new_mtu);
	int rx_skb_size = emac_rx_skb_size(new_mtu);
	int i, ret = 0;

	emac_rx_disable(dev);
	mal_disable_rx_channel(dev->mal, emacdata->mal_rx_chan);

	if (dev->rx_sg_skb) {
		++dev->estats.rx_dropped_resize;
		dev_kfree_skb(dev->rx_sg_skb);
		dev->rx_sg_skb = NULL;
	}

	/* Make a first pass over RX ring and mark BDs ready, dropping 
	 * non-processed packets on the way. We need this as a separate pass
	 * to simplify error recovery in the case of allocation failure later.
	 */
	for (i = 0; i < NUM_RX_BUFF; ++i) {
		if (dev->rx_desc[i].ctrl & MAL_RX_CTRL_FIRST)
			++dev->estats.rx_dropped_resize;

		dev->rx_desc[i].data_len = 0;
		dev->rx_desc[i].ctrl = MAL_RX_CTRL_EMPTY |
		    (i == (NUM_RX_BUFF - 1) ? MAL_RX_CTRL_WRAP : 0);
	}

	/* Reallocate RX ring only if bigger skb buffers are required */
	if (rx_skb_size <= dev->rx_skb_size)
		goto skip;

	/* Second pass, allocate new skbs */
	for (i = 0; i < NUM_RX_BUFF; ++i) {
		struct sk_buff *skb = alloc_skb(rx_skb_size, GFP_ATOMIC);
		if (!skb) {
			ret = -ENOMEM;
			goto oom;
		}

		BUG_ON(!dev->rx_skb[i]);
		dev_kfree_skb(dev->rx_skb[i]);

		skb_reserve(skb, EMAC_RX_SKB_HEADROOM + 2);
		dev->rx_desc[i].data_ptr =
		    dma_map_single(dev->ldev, skb->data - 2, rx_sync_size,
				   DMA_FROM_DEVICE) + 2;
		dev->rx_skb[i] = skb;
	}
      skip:
	/* Check if we need to change "Jumbo" bit in MR1 */
	if ((new_mtu > ETH_DATA_LEN) ^ (dev->ndev->mtu > ETH_DATA_LEN)) {
		/* This is to prevent starting RX channel in emac_rx_enable() */
		dev->commac.rx_stopped = 1;

		dev->ndev->mtu = new_mtu;
		emac_full_tx_reset(dev->ndev);
	}

	mal_set_rcbs(dev->mal, emacdata->mal_rx_chan, emac_rx_size(new_mtu));
      oom:
	/* Restart RX */
	dev->commac.rx_stopped = dev->rx_slot = 0;
	mal_enable_rx_channel(dev->mal, emacdata->mal_rx_chan);
	emac_rx_enable(dev);

	return ret;
}

/* Process ctx, rtnl_lock semaphore */
static int emac_change_mtu(struct net_device *ndev, int new_mtu)
{
	struct ocp_enet_private *dev = ndev->priv;
	int ret = 0;

	if (new_mtu < EMAC_MIN_MTU || new_mtu > EMAC_MAX_MTU)
		return -EINVAL;

	DBG("%d: change_mtu(%d)" NL, dev->def->index, new_mtu);

	local_bh_disable();
	if (netif_running(ndev)) {
		/* Check if we really need to reinitalize RX ring */
		if (emac_rx_skb_size(ndev->mtu) != emac_rx_skb_size(new_mtu))
			ret = emac_resize_rx_ring(dev, new_mtu);
	}

	if (!ret) {
		ndev->mtu = new_mtu;
		dev->rx_skb_size = emac_rx_skb_size(new_mtu);
		dev->rx_sync_size = emac_rx_sync_size(new_mtu);
	}	
	local_bh_enable();

	return ret;
}

static void emac_clean_tx_ring(struct ocp_enet_private *dev)
{
	int i;
	for (i = 0; i < NUM_TX_BUFF; ++i) {
		if (dev->tx_skb[i]) {
			dev_kfree_skb(dev->tx_skb[i]);
			dev->tx_skb[i] = NULL;
			if (dev->tx_desc[i].ctrl & MAL_TX_CTRL_READY)
				++dev->estats.tx_dropped;
		}
		dev->tx_desc[i].ctrl = 0;
		dev->tx_desc[i].data_ptr = 0;
	}
}

static void emac_clean_rx_ring(struct ocp_enet_private *dev)
{
	int i;
	for (i = 0; i < NUM_RX_BUFF; ++i)
		if (dev->rx_skb[i]) {
			dev->rx_desc[i].ctrl = 0;
			dev_kfree_skb(dev->rx_skb[i]);
			dev->rx_skb[i] = NULL;
			dev->rx_desc[i].data_ptr = 0;
		}

	if (dev->rx_sg_skb) {
		dev_kfree_skb(dev->rx_sg_skb);
		dev->rx_sg_skb = NULL;
	}
}

static inline int emac_alloc_rx_skb(struct ocp_enet_private *dev, int slot,
				    int flags)
{
	struct sk_buff *skb = alloc_skb(dev->rx_skb_size, flags);
	if (unlikely(!skb))
		return -ENOMEM;

	dev->rx_skb[slot] = skb;
	dev->rx_desc[slot].data_len = 0;

	skb_reserve(skb, EMAC_RX_SKB_HEADROOM + 2);
	dev->rx_desc[slot].data_ptr = 
	    dma_map_single(dev->ldev, skb->data - 2, dev->rx_sync_size, 
			   DMA_FROM_DEVICE) + 2;
	barrier();
	dev->rx_desc[slot].ctrl = MAL_RX_CTRL_EMPTY |
	    (slot == (NUM_RX_BUFF - 1) ? MAL_RX_CTRL_WRAP : 0);

	return 0;
}

static void emac_print_link_status(struct ocp_enet_private *dev)
{
	if (netif_carrier_ok(dev->ndev))
		printk(KERN_INFO "%s: link is up, %d %s%s\n",
		       dev->ndev->name, dev->phy.speed,
		       dev->phy.duplex == DUPLEX_FULL ? "FDX" : "HDX",
		       dev->phy.pause ? ", pause enabled" :
		       dev->phy.asym_pause ? ", assymetric pause enabled" : "");
	else
		printk(KERN_INFO "%s: link is down\n", dev->ndev->name);
}

/* Process ctx, rtnl_lock semaphore */
static int emac_open(struct net_device *ndev)
{
	struct ocp_enet_private *dev = ndev->priv;
	struct ocp_func_emac_data *emacdata = dev->def->additions;
	int err, i;

	DBG("%d: open" NL, dev->def->index);

	/* Setup error IRQ handler */
	err = request_irq(dev->def->irq, emac_irq, 0, "EMAC", dev);
	if (err) {
		printk(KERN_ERR "%s: failed to request IRQ %d\n",
		       ndev->name, dev->def->irq);
		return err;
	}

	/* Allocate RX ring */
	for (i = 0; i < NUM_RX_BUFF; ++i)
		if (emac_alloc_rx_skb(dev, i, GFP_KERNEL)) {
			printk(KERN_ERR "%s: failed to allocate RX ring\n",
			       ndev->name);
			goto oom;
		}

	local_bh_disable();
	dev->tx_cnt = dev->tx_slot = dev->ack_slot = dev->rx_slot =
	    dev->commac.rx_stopped = 0;
	dev->rx_sg_skb = NULL;

	if (dev->phy.address >= 0) {
		int link_poll_interval;
		if (dev->phy.def->ops->poll_link(&dev->phy)) {
			dev->phy.def->ops->read_link(&dev->phy);
			EMAC_RX_CLK_DEFAULT(dev->def->index);
			netif_carrier_on(dev->ndev);
			link_poll_interval = PHY_POLL_LINK_ON;
		} else {
			EMAC_RX_CLK_TX(dev->def->index);
			netif_carrier_off(dev->ndev);
			link_poll_interval = PHY_POLL_LINK_OFF;
		}
		mod_timer(&dev->link_timer, jiffies + link_poll_interval);
		emac_print_link_status(dev);
	} else
		netif_carrier_on(dev->ndev);

	emac_configure(dev);
	mal_poll_add(dev->mal, &dev->commac);
	mal_enable_tx_channel(dev->mal, emacdata->mal_tx_chan);
	mal_set_rcbs(dev->mal, emacdata->mal_rx_chan, emac_rx_size(ndev->mtu));
	mal_enable_rx_channel(dev->mal, emacdata->mal_rx_chan);
	emac_tx_enable(dev);
	emac_rx_enable(dev);
	netif_start_queue(ndev);
	local_bh_enable();

	return 0;
      oom:
	emac_clean_rx_ring(dev);
	free_irq(dev->def->irq, dev);
	return -ENOMEM;
}

/* BHs disabled */
static int emac_link_differs(struct ocp_enet_private *dev)
{
	u32 r = in_be32(&dev->emacp->mr1);

	int duplex = r & EMAC_MR1_FDE ? DUPLEX_FULL : DUPLEX_HALF;
	int speed, pause, asym_pause;

	if (r & (EMAC_MR1_MF_1000 | EMAC_MR1_MF_1000GPCS))
		speed = SPEED_1000;
	else if (r & EMAC_MR1_MF_100)
		speed = SPEED_100;
	else
		speed = SPEED_10;

	switch (r & (EMAC_MR1_EIFC | EMAC_MR1_APP)) {
	case (EMAC_MR1_EIFC | EMAC_MR1_APP):
		pause = 1;
		asym_pause = 0;
		break;
	case EMAC_MR1_APP:
		pause = 0;
		asym_pause = 1;
		break;
	default:
		pause = asym_pause = 0;
	}
	return speed != dev->phy.speed || duplex != dev->phy.duplex ||
	    pause != dev->phy.pause || asym_pause != dev->phy.asym_pause;
}

/* BHs disabled */
static void emac_link_timer(unsigned long data)
{
	struct ocp_enet_private *dev = (struct ocp_enet_private *)data;
	int link_poll_interval;

	DBG2("%d: link timer" NL, dev->def->index);

	if (dev->phy.def->ops->poll_link(&dev->phy)) {
		if (!netif_carrier_ok(dev->ndev)) {
			EMAC_RX_CLK_DEFAULT(dev->def->index);

			/* Get new link parameters */
			dev->phy.def->ops->read_link(&dev->phy);

			if (dev->tah_dev || emac_link_differs(dev))
				emac_full_tx_reset(dev->ndev);

			netif_carrier_on(dev->ndev);
			emac_print_link_status(dev);
		}
		link_poll_interval = PHY_POLL_LINK_ON;
	} else {
		if (netif_carrier_ok(dev->ndev)) {
			EMAC_RX_CLK_TX(dev->def->index);
#if defined(CONFIG_IBM_EMAC_PHY_RX_CLK_FIX)
			emac_reinitialize(dev);
#endif
			netif_carrier_off(dev->ndev);
			emac_print_link_status(dev);
		}

		/* Retry reset if the previous attempt failed.
		 * This is needed mostly for CONFIG_IBM_EMAC_PHY_RX_CLK_FIX
		 * case, but I left it here because it shouldn't trigger for
		 * sane PHYs anyway.
		 */
		if (unlikely(dev->reset_failed))
			emac_reinitialize(dev);

		link_poll_interval = PHY_POLL_LINK_OFF;
	}
	mod_timer(&dev->link_timer, jiffies + link_poll_interval);
}

/* BHs disabled */
static void emac_force_link_update(struct ocp_enet_private *dev)
{
	netif_carrier_off(dev->ndev);
	if (timer_pending(&dev->link_timer))
		mod_timer(&dev->link_timer, jiffies + PHY_POLL_LINK_OFF);
}

/* Process ctx, rtnl_lock semaphore */
static int emac_close(struct net_device *ndev)
{
	struct ocp_enet_private *dev = ndev->priv;
	struct ocp_func_emac_data *emacdata = dev->def->additions;

	DBG("%d: close" NL, dev->def->index);

	local_bh_disable();

	if (dev->phy.address >= 0)
		del_timer_sync(&dev->link_timer);

	netif_stop_queue(ndev);
	emac_rx_disable(dev);
	emac_tx_disable(dev);
	mal_disable_rx_channel(dev->mal, emacdata->mal_rx_chan);
	mal_disable_tx_channel(dev->mal, emacdata->mal_tx_chan);
	mal_poll_del(dev->mal, &dev->commac);
	local_bh_enable();

	emac_clean_tx_ring(dev);
	emac_clean_rx_ring(dev);
	free_irq(dev->def->irq, dev);

	return 0;
}

static inline u16 emac_tx_csum(struct ocp_enet_private *dev,
			       struct sk_buff *skb)
{
#if defined(CONFIG_IBM_EMAC_TAH)
	if (skb->ip_summed == CHECKSUM_HW) {
		++dev->stats.tx_packets_csum;
		return EMAC_TX_CTRL_TAH_CSUM;
	}
#endif
	return 0;
}

static inline int emac_xmit_finish(struct ocp_enet_private *dev, int len)
{
	struct emac_regs *p = dev->emacp;
	struct net_device *ndev = dev->ndev;

	/* Send the packet out */
	out_be32(&p->tmr0, EMAC_TMR0_XMIT);

	if (unlikely(++dev->tx_cnt == NUM_TX_BUFF)) {
		netif_stop_queue(ndev);
		DBG2("%d: stopped TX queue" NL, dev->def->index);
	}

	ndev->trans_start = jiffies;
	++dev->stats.tx_packets;
	dev->stats.tx_bytes += len;

	return 0;
}

/* BHs disabled */
static int emac_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct ocp_enet_private *dev = ndev->priv;
	unsigned int len = skb->len;
	int slot;

	u16 ctrl = EMAC_TX_CTRL_GFCS | EMAC_TX_CTRL_GP | MAL_TX_CTRL_READY |
	    MAL_TX_CTRL_LAST | emac_tx_csum(dev, skb);

	slot = dev->tx_slot++;
	if (dev->tx_slot == NUM_TX_BUFF) {
		dev->tx_slot = 0;
		ctrl |= MAL_TX_CTRL_WRAP;
	}

	DBG2("%d: xmit(%u) %d" NL, dev->def->index, len, slot);

	dev->tx_skb[slot] = skb;
	dev->tx_desc[slot].data_ptr = dma_map_single(dev->ldev, skb->data, len,
						     DMA_TO_DEVICE);
	dev->tx_desc[slot].data_len = (u16) len;
	barrier();
	dev->tx_desc[slot].ctrl = ctrl;

	return emac_xmit_finish(dev, len);
}

#if defined(CONFIG_IBM_EMAC_TAH)
static inline int emac_xmit_split(struct ocp_enet_private *dev, int slot,
				  u32 pd, int len, int last, u16 base_ctrl)
{
	while (1) {
		u16 ctrl = base_ctrl;
		int chunk = min(len, MAL_MAX_TX_SIZE);
		len -= chunk;

		slot = (slot + 1) % NUM_TX_BUFF;

		if (last && !len)
			ctrl |= MAL_TX_CTRL_LAST;
		if (slot == NUM_TX_BUFF - 1)
			ctrl |= MAL_TX_CTRL_WRAP;

		dev->tx_skb[slot] = NULL;
		dev->tx_desc[slot].data_ptr = pd;
		dev->tx_desc[slot].data_len = (u16) chunk;
		dev->tx_desc[slot].ctrl = ctrl;
		++dev->tx_cnt;

		if (!len)
			break;

		pd += chunk;
	}
	return slot;
}

/* BHs disabled (SG version for TAH equipped EMACs) */
static int emac_start_xmit_sg(struct sk_buff *skb, struct net_device *ndev)
{
	struct ocp_enet_private *dev = ndev->priv;
	int nr_frags = skb_shinfo(skb)->nr_frags;
	int len = skb->len, chunk;
	int slot, i;
	u16 ctrl;
	u32 pd;

	/* This is common "fast" path */
	if (likely(!nr_frags && len <= MAL_MAX_TX_SIZE))
		return emac_start_xmit(skb, ndev);

	len -= skb->data_len;

	/* Note, this is only an *estimation*, we can still run out of empty
	 * slots because of the additional fragmentation into
	 * MAL_MAX_TX_SIZE-sized chunks
	 */
	if (unlikely(dev->tx_cnt + nr_frags + mal_tx_chunks(len) > NUM_TX_BUFF))
		goto stop_queue;

	ctrl = EMAC_TX_CTRL_GFCS | EMAC_TX_CTRL_GP | MAL_TX_CTRL_READY |
	    emac_tx_csum(dev, skb);
	slot = dev->tx_slot;

	/* skb data */
	dev->tx_skb[slot] = NULL;
	chunk = min(len, MAL_MAX_TX_SIZE);
	dev->tx_desc[slot].data_ptr = pd =
	    dma_map_single(dev->ldev, skb->data, len, DMA_TO_DEVICE);
	dev->tx_desc[slot].data_len = (u16) chunk;
	len -= chunk;
	if (unlikely(len))
		slot = emac_xmit_split(dev, slot, pd + chunk, len, !nr_frags,
				       ctrl);
	/* skb fragments */
	for (i = 0; i < nr_frags; ++i) {
		struct skb_frag_struct *frag = &skb_shinfo(skb)->frags[i];
		len = frag->size;

		if (unlikely(dev->tx_cnt + mal_tx_chunks(len) >= NUM_TX_BUFF))
			goto undo_frame;

		pd = dma_map_page(dev->ldev, frag->page, frag->page_offset, len,
				  DMA_TO_DEVICE);

		slot = emac_xmit_split(dev, slot, pd, len, i == nr_frags - 1,
				       ctrl);
	}

	DBG2("%d: xmit_sg(%u) %d - %d" NL, dev->def->index, skb->len,
	     dev->tx_slot, slot);

	/* Attach skb to the last slot so we don't release it too early */
	dev->tx_skb[slot] = skb;

	/* Send the packet out */
	if (dev->tx_slot == NUM_TX_BUFF - 1)
		ctrl |= MAL_TX_CTRL_WRAP;
	barrier();
	dev->tx_desc[dev->tx_slot].ctrl = ctrl;
	dev->tx_slot = (slot + 1) % NUM_TX_BUFF;

	return emac_xmit_finish(dev, skb->len);

      undo_frame:
	/* Well, too bad. Our previous estimation was overly optimistic. 
	 * Undo everything.
	 */
	while (slot != dev->tx_slot) {
		dev->tx_desc[slot].ctrl = 0;
		--dev->tx_cnt;
		if (--slot < 0)
			slot = NUM_TX_BUFF - 1;
	}
	++dev->estats.tx_undo;

      stop_queue:
	netif_stop_queue(ndev);
	DBG2("%d: stopped TX queue" NL, dev->def->index);
	return 1;
}
#else
# define emac_start_xmit_sg	emac_start_xmit
#endif	/* !defined(CONFIG_IBM_EMAC_TAH) */

/* BHs disabled */
static void emac_parse_tx_error(struct ocp_enet_private *dev, u16 ctrl)
{
	struct ibm_emac_error_stats *st = &dev->estats;
	DBG("%d: BD TX error %04x" NL, dev->def->index, ctrl);

	++st->tx_bd_errors;
	if (ctrl & EMAC_TX_ST_BFCS)
		++st->tx_bd_bad_fcs;
	if (ctrl & EMAC_TX_ST_LCS)
		++st->tx_bd_carrier_loss;
	if (ctrl & EMAC_TX_ST_ED)
		++st->tx_bd_excessive_deferral;
	if (ctrl & EMAC_TX_ST_EC)
		++st->tx_bd_excessive_collisions;
	if (ctrl & EMAC_TX_ST_LC)
		++st->tx_bd_late_collision;
	if (ctrl & EMAC_TX_ST_MC)
		++st->tx_bd_multple_collisions;
	if (ctrl & EMAC_TX_ST_SC)
		++st->tx_bd_single_collision;
	if (ctrl & EMAC_TX_ST_UR)
		++st->tx_bd_underrun;
	if (ctrl & EMAC_TX_ST_SQE)
		++st->tx_bd_sqe;
}

static void emac_poll_tx(void *param)
{
	struct ocp_enet_private *dev = param;
	DBG2("%d: poll_tx, %d %d" NL, dev->def->index, dev->tx_cnt,
	     dev->ack_slot);

	if (dev->tx_cnt) {
		u16 ctrl;
		int slot = dev->ack_slot, n = 0;
	      again:
		ctrl = dev->tx_desc[slot].ctrl;
		if (!(ctrl & MAL_TX_CTRL_READY)) {
			struct sk_buff *skb = dev->tx_skb[slot];
			++n;

			if (skb) {
				dev_kfree_skb(skb);
				dev->tx_skb[slot] = NULL;
			}
			slot = (slot + 1) % NUM_TX_BUFF;

			if (unlikely(EMAC_IS_BAD_TX(ctrl)))
				emac_parse_tx_error(dev, ctrl);

			if (--dev->tx_cnt)
				goto again;
		}
		if (n) {
			dev->ack_slot = slot;
			if (netif_queue_stopped(dev->ndev) &&
			    dev->tx_cnt < EMAC_TX_WAKEUP_THRESH)
				netif_wake_queue(dev->ndev);

			DBG2("%d: tx %d pkts" NL, dev->def->index, n);
		}
	}
}

static inline void emac_recycle_rx_skb(struct ocp_enet_private *dev, int slot,
				       int len)
{
	struct sk_buff *skb = dev->rx_skb[slot];
	DBG2("%d: recycle %d %d" NL, dev->def->index, slot, len);

	if (len) 
		dma_map_single(dev->ldev, skb->data - 2, 
			       EMAC_DMA_ALIGN(len + 2), DMA_FROM_DEVICE);

	dev->rx_desc[slot].data_len = 0;
	barrier();
	dev->rx_desc[slot].ctrl = MAL_RX_CTRL_EMPTY |
	    (slot == (NUM_RX_BUFF - 1) ? MAL_RX_CTRL_WRAP : 0);
}

static void emac_parse_rx_error(struct ocp_enet_private *dev, u16 ctrl)
{
	struct ibm_emac_error_stats *st = &dev->estats;
	DBG("%d: BD RX error %04x" NL, dev->def->index, ctrl);

	++st->rx_bd_errors;
	if (ctrl & EMAC_RX_ST_OE)
		++st->rx_bd_overrun;
	if (ctrl & EMAC_RX_ST_BP)
		++st->rx_bd_bad_packet;
	if (ctrl & EMAC_RX_ST_RP)
		++st->rx_bd_runt_packet;
	if (ctrl & EMAC_RX_ST_SE)
		++st->rx_bd_short_event;
	if (ctrl & EMAC_RX_ST_AE)
		++st->rx_bd_alignment_error;
	if (ctrl & EMAC_RX_ST_BFCS)
		++st->rx_bd_bad_fcs;
	if (ctrl & EMAC_RX_ST_PTL)
		++st->rx_bd_packet_too_long;
	if (ctrl & EMAC_RX_ST_ORE)
		++st->rx_bd_out_of_range;
	if (ctrl & EMAC_RX_ST_IRE)
		++st->rx_bd_in_range;
}

static inline void emac_rx_csum(struct ocp_enet_private *dev,
				struct sk_buff *skb, u16 ctrl)
{
#if defined(CONFIG_IBM_EMAC_TAH)
	if (!ctrl && dev->tah_dev) {
		skb->ip_summed = CHECKSUM_UNNECESSARY;
		++dev->stats.rx_packets_csum;
	}
#endif
}

static inline int emac_rx_sg_append(struct ocp_enet_private *dev, int slot)
{
	if (likely(dev->rx_sg_skb != NULL)) {
		int len = dev->rx_desc[slot].data_len;
		int tot_len = dev->rx_sg_skb->len + len;

		if (unlikely(tot_len + 2 > dev->rx_skb_size)) {
			++dev->estats.rx_dropped_mtu;
			dev_kfree_skb(dev->rx_sg_skb);
			dev->rx_sg_skb = NULL;
		} else {
			cacheable_memcpy(dev->rx_sg_skb->tail,
					 dev->rx_skb[slot]->data, len);
			skb_put(dev->rx_sg_skb, len);
			emac_recycle_rx_skb(dev, slot, len);
			return 0;
		}
	}
	emac_recycle_rx_skb(dev, slot, 0);
	return -1;
}

/* BHs disabled */
static int emac_poll_rx(void *param, int budget)
{
	struct ocp_enet_private *dev = param;
	int slot = dev->rx_slot, received = 0;

	DBG2("%d: poll_rx(%d)" NL, dev->def->index, budget);

      again:
	while (budget > 0) {
		int len;
		struct sk_buff *skb;
		u16 ctrl = dev->rx_desc[slot].ctrl;

		if (ctrl & MAL_RX_CTRL_EMPTY)
			break;

		skb = dev->rx_skb[slot];
		barrier();
		len = dev->rx_desc[slot].data_len;

		if (unlikely(!MAL_IS_SINGLE_RX(ctrl)))
			goto sg;

		ctrl &= EMAC_BAD_RX_MASK;
		if (unlikely(ctrl && ctrl != EMAC_RX_TAH_BAD_CSUM)) {
			emac_parse_rx_error(dev, ctrl);
			++dev->estats.rx_dropped_error;
			emac_recycle_rx_skb(dev, slot, 0);
			len = 0;
			goto next;
		}

		if (len && len < EMAC_RX_COPY_THRESH) {
			struct sk_buff *copy_skb =
			    alloc_skb(len + EMAC_RX_SKB_HEADROOM + 2, GFP_ATOMIC);
			if (unlikely(!copy_skb))
				goto oom;

			skb_reserve(copy_skb, EMAC_RX_SKB_HEADROOM + 2);
			cacheable_memcpy(copy_skb->data - 2, skb->data - 2,
					 len + 2);
			emac_recycle_rx_skb(dev, slot, len);
			skb = copy_skb;
		} else if (unlikely(emac_alloc_rx_skb(dev, slot, GFP_ATOMIC)))
			goto oom;

		skb_put(skb, len);
	      push_packet:
		skb->dev = dev->ndev;
		skb->protocol = eth_type_trans(skb, dev->ndev);
		emac_rx_csum(dev, skb, ctrl);

		if (unlikely(netif_receive_skb(skb) == NET_RX_DROP))
			++dev->estats.rx_dropped_stack;
	      next:
		++dev->stats.rx_packets;
	      skip:
		dev->stats.rx_bytes += len;
		slot = (slot + 1) % NUM_RX_BUFF;
		--budget;
		++received;
		continue;
	      sg:
		if (ctrl & MAL_RX_CTRL_FIRST) {
			BUG_ON(dev->rx_sg_skb);
			if (unlikely(emac_alloc_rx_skb(dev, slot, GFP_ATOMIC))) {
				DBG("%d: rx OOM %d" NL, dev->def->index, slot);
				++dev->estats.rx_dropped_oom;
				emac_recycle_rx_skb(dev, slot, 0);
			} else {
				dev->rx_sg_skb = skb;
				skb_put(skb, len);
			}
		} else if (!emac_rx_sg_append(dev, slot) &&
			   (ctrl & MAL_RX_CTRL_LAST)) {

			skb = dev->rx_sg_skb;
			dev->rx_sg_skb = NULL;

			ctrl &= EMAC_BAD_RX_MASK;
			if (unlikely(ctrl && ctrl != EMAC_RX_TAH_BAD_CSUM)) {
				emac_parse_rx_error(dev, ctrl);
				++dev->estats.rx_dropped_error;
				dev_kfree_skb(skb);
				len = 0;
			} else
				goto push_packet;
		}
		goto skip;
	      oom:
		DBG("%d: rx OOM %d" NL, dev->def->index, slot);
		/* Drop the packet and recycle skb */
		++dev->estats.rx_dropped_oom;
		emac_recycle_rx_skb(dev, slot, 0);
		goto next;
	}

	if (received) {
		DBG2("%d: rx %d BDs" NL, dev->def->index, received);
		dev->rx_slot = slot;
	}

	if (unlikely(budget && dev->commac.rx_stopped)) {
		struct ocp_func_emac_data *emacdata = dev->def->additions;

		barrier();
		if (!(dev->rx_desc[slot].ctrl & MAL_RX_CTRL_EMPTY)) {
			DBG2("%d: rx restart" NL, dev->def->index);
			received = 0;
			goto again;
		}

		if (dev->rx_sg_skb) {
			DBG2("%d: dropping partial rx packet" NL,
			     dev->def->index);
			++dev->estats.rx_dropped_error;
			dev_kfree_skb(dev->rx_sg_skb);
			dev->rx_sg_skb = NULL;
		}

		dev->commac.rx_stopped = 0;
		mal_enable_rx_channel(dev->mal, emacdata->mal_rx_chan);
		emac_rx_enable(dev);
		dev->rx_slot = 0;
	}
	return received;
}

/* BHs disabled */
static int emac_peek_rx(void *param)
{
	struct ocp_enet_private *dev = param;
	return !(dev->rx_desc[dev->rx_slot].ctrl & MAL_RX_CTRL_EMPTY);
}

/* BHs disabled */
static int emac_peek_rx_sg(void *param)
{
	struct ocp_enet_private *dev = param;
	int slot = dev->rx_slot;
	while (1) {
		u16 ctrl = dev->rx_desc[slot].ctrl;
		if (ctrl & MAL_RX_CTRL_EMPTY)
			return 0;
		else if (ctrl & MAL_RX_CTRL_LAST)
			return 1;

		slot = (slot + 1) % NUM_RX_BUFF;

		/* I'm just being paranoid here :) */
		if (unlikely(slot == dev->rx_slot))
			return 0;
	}
}

/* Hard IRQ */
static void emac_rxde(void *param)
{
	struct ocp_enet_private *dev = param;
	++dev->estats.rx_stopped;
	emac_rx_disable_async(dev);
}

/* Hard IRQ */
static irqreturn_t emac_irq(int irq, void *dev_instance, struct pt_regs *regs)
{
	struct ocp_enet_private *dev = dev_instance;
	struct emac_regs *p = dev->emacp;
	struct ibm_emac_error_stats *st = &dev->estats;

	u32 isr = in_be32(&p->isr);
	out_be32(&p->isr, isr);

	DBG("%d: isr = %08x" NL, dev->def->index, isr);

	if (isr & EMAC_ISR_TXPE)
		++st->tx_parity;
	if (isr & EMAC_ISR_RXPE)
		++st->rx_parity;
	if (isr & EMAC_ISR_TXUE)
		++st->tx_underrun;
	if (isr & EMAC_ISR_RXOE)
		++st->rx_fifo_overrun;
	if (isr & EMAC_ISR_OVR)
		++st->rx_overrun;
	if (isr & EMAC_ISR_BP)
		++st->rx_bad_packet;
	if (isr & EMAC_ISR_RP)
		++st->rx_runt_packet;
	if (isr & EMAC_ISR_SE)
		++st->rx_short_event;
	if (isr & EMAC_ISR_ALE)
		++st->rx_alignment_error;
	if (isr & EMAC_ISR_BFCS)
		++st->rx_bad_fcs;
	if (isr & EMAC_ISR_PTLE)
		++st->rx_packet_too_long;
	if (isr & EMAC_ISR_ORE)
		++st->rx_out_of_range;
	if (isr & EMAC_ISR_IRE)
		++st->rx_in_range;
	if (isr & EMAC_ISR_SQE)
		++st->tx_sqe;
	if (isr & EMAC_ISR_TE)
		++st->tx_errors;

	return IRQ_HANDLED;
}

static struct net_device_stats *emac_stats(struct net_device *ndev)
{
	struct ocp_enet_private *dev = ndev->priv;
	struct ibm_emac_stats *st = &dev->stats;
	struct ibm_emac_error_stats *est = &dev->estats;
	struct net_device_stats *nst = &dev->nstats;

	DBG2("%d: stats" NL, dev->def->index);

	/* Compute "legacy" statistics */
	local_irq_disable();
	nst->rx_packets = (unsigned long)st->rx_packets;
	nst->rx_bytes = (unsigned long)st->rx_bytes;
	nst->tx_packets = (unsigned long)st->tx_packets;
	nst->tx_bytes = (unsigned long)st->tx_bytes;
	nst->rx_dropped = (unsigned long)(est->rx_dropped_oom +
					  est->rx_dropped_error +
					  est->rx_dropped_resize +
					  est->rx_dropped_mtu);
	nst->tx_dropped = (unsigned long)est->tx_dropped;

	nst->rx_errors = (unsigned long)est->rx_bd_errors;
	nst->rx_fifo_errors = (unsigned long)(est->rx_bd_overrun +
					      est->rx_fifo_overrun +
					      est->rx_overrun);
	nst->rx_frame_errors = (unsigned long)(est->rx_bd_alignment_error +
					       est->rx_alignment_error);
	nst->rx_crc_errors = (unsigned long)(est->rx_bd_bad_fcs +
					     est->rx_bad_fcs);
	nst->rx_length_errors = (unsigned long)(est->rx_bd_runt_packet +
						est->rx_bd_short_event +
						est->rx_bd_packet_too_long +
						est->rx_bd_out_of_range +
						est->rx_bd_in_range +
						est->rx_runt_packet +
						est->rx_short_event +
						est->rx_packet_too_long +
						est->rx_out_of_range +
						est->rx_in_range);

	nst->tx_errors = (unsigned long)(est->tx_bd_errors + est->tx_errors);
	nst->tx_fifo_errors = (unsigned long)(est->tx_bd_underrun +
					      est->tx_underrun);
	nst->tx_carrier_errors = (unsigned long)est->tx_bd_carrier_loss;
	nst->collisions = (unsigned long)(est->tx_bd_excessive_deferral +
					  est->tx_bd_excessive_collisions +
					  est->tx_bd_late_collision +
					  est->tx_bd_multple_collisions);
	local_irq_enable();
	return nst;
}

static void emac_remove(struct ocp_device *ocpdev)
{
	struct ocp_enet_private *dev = ocp_get_drvdata(ocpdev);

	DBG("%d: remove" NL, dev->def->index);

	ocp_set_drvdata(ocpdev, 0);
	unregister_netdev(dev->ndev);

	tah_fini(dev->tah_dev);
	rgmii_fini(dev->rgmii_dev, dev->rgmii_input);
	zmii_fini(dev->zmii_dev, dev->zmii_input);

	emac_dbg_register(dev->def->index, 0);

	mal_unregister_commac(dev->mal, &dev->commac);
	iounmap((void *)dev->emacp);
	kfree(dev->ndev);
}

static struct mal_commac_ops emac_commac_ops = {
	.poll_tx = &emac_poll_tx,
	.poll_rx = &emac_poll_rx,
	.peek_rx = &emac_peek_rx,
	.rxde = &emac_rxde,
};

static struct mal_commac_ops emac_commac_sg_ops = {
	.poll_tx = &emac_poll_tx,
	.poll_rx = &emac_poll_rx,
	.peek_rx = &emac_peek_rx_sg,
	.rxde = &emac_rxde,
};

/* Ethtool support */
static int emac_ethtool_get_settings(struct net_device *ndev,
				     struct ethtool_cmd *cmd)
{
	struct ocp_enet_private *dev = ndev->priv;

	cmd->supported = dev->phy.features;
	cmd->port = PORT_MII;
	cmd->phy_address = dev->phy.address;
	cmd->transceiver =
	    dev->phy.address >= 0 ? XCVR_EXTERNAL : XCVR_INTERNAL;

	local_bh_disable();
	cmd->advertising = dev->phy.advertising;
	cmd->autoneg = dev->phy.autoneg;
	cmd->speed = dev->phy.speed;
	cmd->duplex = dev->phy.duplex;
	local_bh_enable();

	return 0;
}

static int emac_ethtool_set_settings(struct net_device *ndev,
				     struct ethtool_cmd *cmd)
{
	struct ocp_enet_private *dev = ndev->priv;
	u32 f = dev->phy.features;

	DBG("%d: set_settings(%d, %d, %d, 0x%08x)" NL, dev->def->index,
	    cmd->autoneg, cmd->speed, cmd->duplex, cmd->advertising);

	/* Basic sanity checks */
	if (dev->phy.address < 0)
		return -EOPNOTSUPP;
	if (cmd->autoneg != AUTONEG_ENABLE && cmd->autoneg != AUTONEG_DISABLE)
		return -EINVAL;
	if (cmd->autoneg == AUTONEG_ENABLE && cmd->advertising == 0)
		return -EINVAL;
	if (cmd->duplex != DUPLEX_HALF && cmd->duplex != DUPLEX_FULL)
		return -EINVAL;

	if (cmd->autoneg == AUTONEG_DISABLE) {
		switch (cmd->speed) {
		case SPEED_10:
			if (cmd->duplex == DUPLEX_HALF
			    && !(f & SUPPORTED_10baseT_Half))
				return -EINVAL;
			if (cmd->duplex == DUPLEX_FULL
			    && !(f & SUPPORTED_10baseT_Full))
				return -EINVAL;
			break;
		case SPEED_100:
			if (cmd->duplex == DUPLEX_HALF
			    && !(f & SUPPORTED_100baseT_Half))
				return -EINVAL;
			if (cmd->duplex == DUPLEX_FULL
			    && !(f & SUPPORTED_100baseT_Full))
				return -EINVAL;
			break;
		case SPEED_1000:
			if (cmd->duplex == DUPLEX_HALF
			    && !(f & SUPPORTED_1000baseT_Half))
				return -EINVAL;
			if (cmd->duplex == DUPLEX_FULL
			    && !(f & SUPPORTED_1000baseT_Full))
				return -EINVAL;
			break;
		default:
			return -EINVAL;
		}

		local_bh_disable();
		dev->phy.def->ops->setup_forced(&dev->phy, cmd->speed,
						cmd->duplex);

	} else {
		if (!(f & SUPPORTED_Autoneg))
			return -EINVAL;

		local_bh_disable();
		dev->phy.def->ops->setup_aneg(&dev->phy,
					      (cmd->advertising & f) |
					      (dev->phy.advertising &
					       (ADVERTISED_Pause |
						ADVERTISED_Asym_Pause)));
	}
	emac_force_link_update(dev);
	local_bh_enable();

	return 0;
}

static void emac_ethtool_get_ringparam(struct net_device *ndev,
				       struct ethtool_ringparam *rp)
{
	rp->rx_max_pending = rp->rx_pending = NUM_RX_BUFF;
	rp->tx_max_pending = rp->tx_pending = NUM_TX_BUFF;
}

static void emac_ethtool_get_pauseparam(struct net_device *ndev,
					struct ethtool_pauseparam *pp)
{
	struct ocp_enet_private *dev = ndev->priv;

	local_bh_disable();
	if ((dev->phy.features & SUPPORTED_Autoneg) &&
	    (dev->phy.advertising & (ADVERTISED_Pause | ADVERTISED_Asym_Pause)))
		pp->autoneg = 1;

	if (dev->phy.duplex == DUPLEX_FULL) {
		if (dev->phy.pause)
			pp->rx_pause = pp->tx_pause = 1;
		else if (dev->phy.asym_pause)
			pp->tx_pause = 1;
	}
	local_bh_enable();
}

static u32 emac_ethtool_get_rx_csum(struct net_device *ndev)
{
	struct ocp_enet_private *dev = ndev->priv;
	return dev->tah_dev != 0;
}

static int emac_get_regs_len(struct ocp_enet_private *dev)
{
	return sizeof(struct emac_ethtool_regs_subhdr) + EMAC_ETHTOOL_REGS_SIZE;
}

static int emac_ethtool_get_regs_len(struct net_device *ndev)
{
	struct ocp_enet_private *dev = ndev->priv;
	return sizeof(struct emac_ethtool_regs_hdr) +
	    emac_get_regs_len(dev) + mal_get_regs_len(dev->mal) +
	    zmii_get_regs_len(dev->zmii_dev) +
	    rgmii_get_regs_len(dev->rgmii_dev) +
	    tah_get_regs_len(dev->tah_dev);
}

static void *emac_dump_regs(struct ocp_enet_private *dev, void *buf)
{
	struct emac_ethtool_regs_subhdr *hdr = buf;

	hdr->version = EMAC_ETHTOOL_REGS_VER;
	hdr->index = dev->def->index;
	memcpy_fromio(hdr + 1, dev->emacp, EMAC_ETHTOOL_REGS_SIZE);
	return ((void *)(hdr + 1) + EMAC_ETHTOOL_REGS_SIZE);
}

static void emac_ethtool_get_regs(struct net_device *ndev,
				  struct ethtool_regs *regs, void *buf)
{
	struct ocp_enet_private *dev = ndev->priv;
	struct emac_ethtool_regs_hdr *hdr = buf;

	hdr->components = 0;
	buf = hdr + 1;

	local_irq_disable();
	buf = mal_dump_regs(dev->mal, buf);
	buf = emac_dump_regs(dev, buf);
	if (dev->zmii_dev) {
		hdr->components |= EMAC_ETHTOOL_REGS_ZMII;
		buf = zmii_dump_regs(dev->zmii_dev, buf);
	}
	if (dev->rgmii_dev) {
		hdr->components |= EMAC_ETHTOOL_REGS_RGMII;
		buf = rgmii_dump_regs(dev->rgmii_dev, buf);
	}
	if (dev->tah_dev) {
		hdr->components |= EMAC_ETHTOOL_REGS_TAH;
		buf = tah_dump_regs(dev->tah_dev, buf);
	}
	local_irq_enable();
}

static int emac_ethtool_nway_reset(struct net_device *ndev)
{
	struct ocp_enet_private *dev = ndev->priv;
	int res = 0;

	DBG("%d: nway_reset" NL, dev->def->index);

	if (dev->phy.address < 0)
		return -EOPNOTSUPP;

	local_bh_disable();
	if (!dev->phy.autoneg) {
		res = -EINVAL;
		goto out;
	}

	dev->phy.def->ops->setup_aneg(&dev->phy, dev->phy.advertising);
	emac_force_link_update(dev);

      out:
	local_bh_enable();
	return res;
}

static int emac_ethtool_get_stats_count(struct net_device *ndev)
{
	return EMAC_ETHTOOL_STATS_COUNT;
}

static void emac_ethtool_get_strings(struct net_device *ndev, u32 stringset,
				     u8 * buf)
{
	if (stringset == ETH_SS_STATS)
		memcpy(buf, &emac_stats_keys, sizeof(emac_stats_keys));
}

static void emac_ethtool_get_ethtool_stats(struct net_device *ndev,
					   struct ethtool_stats *estats,
					   u64 * tmp_stats)
{
	struct ocp_enet_private *dev = ndev->priv;
	local_irq_disable();
	memcpy(tmp_stats, &dev->stats, sizeof(dev->stats));
	tmp_stats += sizeof(dev->stats) / sizeof(u64);
	memcpy(tmp_stats, &dev->estats, sizeof(dev->estats));
	local_irq_enable();
}

static void emac_ethtool_get_drvinfo(struct net_device *ndev,
				     struct ethtool_drvinfo *info)
{
	struct ocp_enet_private *dev = ndev->priv;

	strcpy(info->driver, "ibm_emac");
	strcpy(info->version, DRV_VERSION);
	info->fw_version[0] = '\0';
	sprintf(info->bus_info, "PPC 4xx EMAC %d", dev->def->index);
	info->n_stats = emac_ethtool_get_stats_count(ndev);
	info->regdump_len = emac_ethtool_get_regs_len(ndev);
}

static struct ethtool_ops emac_ethtool_ops = {
	.get_settings = emac_ethtool_get_settings,
	.set_settings = emac_ethtool_set_settings,
	.get_drvinfo = emac_ethtool_get_drvinfo,

	.get_regs_len = emac_ethtool_get_regs_len,
	.get_regs = emac_ethtool_get_regs,

	.nway_reset = emac_ethtool_nway_reset,

	.get_ringparam = emac_ethtool_get_ringparam,
	.get_pauseparam = emac_ethtool_get_pauseparam,

	.get_rx_csum = emac_ethtool_get_rx_csum,

	.get_strings = emac_ethtool_get_strings,
	.get_stats_count = emac_ethtool_get_stats_count,
	.get_ethtool_stats = emac_ethtool_get_ethtool_stats,

	.get_link = ethtool_op_get_link,
	.get_tx_csum = ethtool_op_get_tx_csum,
	.get_sg = ethtool_op_get_sg,
};

static int emac_ioctl(struct net_device *ndev, struct ifreq *rq, int cmd)
{
	struct ocp_enet_private *dev = ndev->priv;
	uint16_t *data = (uint16_t *) & rq->ifr_ifru;

	DBG("%d: ioctl %08x" NL, dev->def->index, cmd);

	if (dev->phy.address < 0)
		return -EOPNOTSUPP;

	switch (cmd) {
	case SIOCGMIIPHY:
	case SIOCDEVPRIVATE:
		data[0] = dev->phy.address;
		/* Fall through */
	case SIOCGMIIREG:
	case SIOCDEVPRIVATE + 1:
		data[3] = emac_mdio_read(ndev, dev->phy.address, data[1]);
		return 0;

	case SIOCSMIIREG:
	case SIOCDEVPRIVATE + 2:
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		emac_mdio_write(ndev, dev->phy.address, data[1], data[2]);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int __init emac_probe(struct ocp_device *ocpdev)
{
	struct ocp_func_emac_data *emacdata = ocpdev->def->additions;
	struct net_device *ndev;
	struct ocp_device *maldev;
	struct ocp_enet_private *dev;
	int err, i;

	DBG("%d: probe" NL, ocpdev->def->index);

	if (!emacdata) {
		printk(KERN_ERR "emac%d: Missing additional data!\n",
		       ocpdev->def->index);
		return -ENODEV;
	}

	/* Allocate our net_device structure */
	ndev = alloc_etherdev(sizeof(struct ocp_enet_private));
	if (!ndev) {
		printk(KERN_ERR "emac%d: could not allocate ethernet device!\n",
		       ocpdev->def->index);
		return -ENOMEM;
	}
	dev = ndev->priv;
	dev->ndev = ndev;
	dev->ldev = &ocpdev->dev;
	dev->def = ocpdev->def;
	SET_MODULE_OWNER(ndev);

	/* Find MAL device we are connected to */
	maldev =
	    ocp_find_device(OCP_VENDOR_IBM, OCP_FUNC_MAL, emacdata->mal_idx);
	if (!maldev) {
		printk(KERN_ERR "emac%d: unknown mal%d device!\n",
		       dev->def->index, emacdata->mal_idx);
		err = -ENODEV;
		goto out;
	}
	dev->mal = ocp_get_drvdata(maldev);
	if (!dev->mal) {
		printk(KERN_ERR "emac%d: mal%d hasn't been initialized yet!\n",
		       dev->def->index, emacdata->mal_idx);
		err = -ENODEV;
		goto out;
	}

	/* Register with MAL */
	dev->commac.ops = &emac_commac_ops;
	dev->commac.dev = dev;
	dev->commac.tx_chan_mask = MAL_CHAN_MASK(emacdata->mal_tx_chan);
	dev->commac.rx_chan_mask = MAL_CHAN_MASK(emacdata->mal_rx_chan);
	err = mal_register_commac(dev->mal, &dev->commac);
	if (err) {
		printk(KERN_ERR "emac%d: failed to register with mal%d!\n",
		       dev->def->index, emacdata->mal_idx);
		goto out;
	}
	dev->rx_skb_size = emac_rx_skb_size(ndev->mtu);
	dev->rx_sync_size = emac_rx_sync_size(ndev->mtu);

	/* Get pointers to BD rings */
	dev->tx_desc =
	    dev->mal->bd_virt + mal_tx_bd_offset(dev->mal,
						 emacdata->mal_tx_chan);
	dev->rx_desc =
	    dev->mal->bd_virt + mal_rx_bd_offset(dev->mal,
						 emacdata->mal_rx_chan);

	DBG("%d: tx_desc %p" NL, ocpdev->def->index, dev->tx_desc);
	DBG("%d: rx_desc %p" NL, ocpdev->def->index, dev->rx_desc);

	/* Clean rings */
	memset(dev->tx_desc, 0, NUM_TX_BUFF * sizeof(struct mal_descriptor));
	memset(dev->rx_desc, 0, NUM_RX_BUFF * sizeof(struct mal_descriptor));

	/* If we depend on another EMAC for MDIO, check whether it was probed already */
	if (emacdata->mdio_idx >= 0 && emacdata->mdio_idx != ocpdev->def->index) {
		struct ocp_device *mdiodev =
		    ocp_find_device(OCP_VENDOR_IBM, OCP_FUNC_EMAC,
				    emacdata->mdio_idx);
		if (!mdiodev) {
			printk(KERN_ERR "emac%d: unknown emac%d device!\n",
			       dev->def->index, emacdata->mdio_idx);
			err = -ENODEV;
			goto out2;
		}
		dev->mdio_dev = ocp_get_drvdata(mdiodev);
		if (!dev->mdio_dev) {
			printk(KERN_ERR
			       "emac%d: emac%d hasn't been initialized yet!\n",
			       dev->def->index, emacdata->mdio_idx);
			err = -ENODEV;
			goto out2;
		}
	}

	/* Attach to ZMII, if needed */
	if ((err = zmii_attach(dev)) != 0)
		goto out2;

	/* Attach to RGMII, if needed */
	if ((err = rgmii_attach(dev)) != 0)
		goto out3;

	/* Attach to TAH, if needed */
	if ((err = tah_attach(dev)) != 0)
		goto out4;

	/* Map EMAC regs */
	dev->emacp =
	    (struct emac_regs *)ioremap(dev->def->paddr,
					sizeof(struct emac_regs));
	if (!dev->emacp) {
		printk(KERN_ERR "emac%d: could not ioremap device registers!\n",
		       dev->def->index);
		err = -ENOMEM;
		goto out5;
	}

	/* Fill in MAC address */
	for (i = 0; i < 6; ++i)
		ndev->dev_addr[i] = emacdata->mac_addr[i];

	/* Set some link defaults before we can find out real parameters */
	dev->phy.speed = SPEED_100;
	dev->phy.duplex = DUPLEX_FULL;
	dev->phy.autoneg = AUTONEG_DISABLE;
	dev->phy.pause = dev->phy.asym_pause = 0;
	init_timer(&dev->link_timer);
	dev->link_timer.function = emac_link_timer;
	dev->link_timer.data = (unsigned long)dev;

	/* Find PHY if any */
	dev->phy.dev = ndev;
	dev->phy.mode = emacdata->phy_mode;
	if (emacdata->phy_map != 0xffffffff) {
		u32 phy_map = emacdata->phy_map | busy_phy_map;
		u32 adv;

		DBG("%d: PHY maps %08x %08x" NL, dev->def->index,
		    emacdata->phy_map, busy_phy_map);

		EMAC_RX_CLK_TX(dev->def->index);

		dev->phy.mdio_read = emac_mdio_read;
		dev->phy.mdio_write = emac_mdio_write;

		/* Configure EMAC with defaults so we can at least use MDIO
		 * This is needed mostly for 440GX
		 */
		if (emac_phy_gpcs(dev->phy.mode)) {
			/* XXX
			 * Make GPCS PHY address equal to EMAC index.
			 * We probably should take into account busy_phy_map
			 * and/or phy_map here.
			 */
			dev->phy.address = dev->def->index;
		}
		
		emac_configure(dev);

		for (i = 0; i < 0x20; phy_map >>= 1, ++i)
			if (!(phy_map & 1)) {
				int r;
				busy_phy_map |= 1 << i;

				/* Quick check if there is a PHY at the address */
				r = emac_mdio_read(dev->ndev, i, MII_BMCR);
				if (r == 0xffff || r < 0)
					continue;
				if (!mii_phy_probe(&dev->phy, i))
					break;
			}
		if (i == 0x20) {
			printk(KERN_WARNING "emac%d: can't find PHY!\n",
			       dev->def->index);
			goto out6;
		}

		/* Init PHY */
		if (dev->phy.def->ops->init)
			dev->phy.def->ops->init(&dev->phy);
		
		/* Disable any PHY features not supported by the platform */
		dev->phy.def->features &= ~emacdata->phy_feat_exc;

		/* Setup initial link parameters */
		if (dev->phy.features & SUPPORTED_Autoneg) {
			adv = dev->phy.features;
#if !defined(CONFIG_40x)
			adv |= ADVERTISED_Pause | ADVERTISED_Asym_Pause;
#endif
			/* Restart autonegotiation */
			dev->phy.def->ops->setup_aneg(&dev->phy, adv);
		} else {
			u32 f = dev->phy.def->features;
			int speed = SPEED_10, fd = DUPLEX_HALF;

			/* Select highest supported speed/duplex */
			if (f & SUPPORTED_1000baseT_Full) {
				speed = SPEED_1000;
				fd = DUPLEX_FULL;
			} else if (f & SUPPORTED_1000baseT_Half)
				speed = SPEED_1000;
			else if (f & SUPPORTED_100baseT_Full) {
				speed = SPEED_100;
				fd = DUPLEX_FULL;
			} else if (f & SUPPORTED_100baseT_Half)
				speed = SPEED_100;
			else if (f & SUPPORTED_10baseT_Full)
				fd = DUPLEX_FULL;

			/* Force link parameters */
			dev->phy.def->ops->setup_forced(&dev->phy, speed, fd);
		}
	} else {
		emac_reset(dev);

		/* PHY-less configuration.
		 * XXX I probably should move these settings to emacdata
		 */
		dev->phy.address = -1;
		dev->phy.features = SUPPORTED_100baseT_Full | SUPPORTED_MII;
		dev->phy.pause = 1;
	}

	/* Fill in the driver function table */
	ndev->open = &emac_open;
	if (dev->tah_dev) {
		ndev->hard_start_xmit = &emac_start_xmit_sg;
		ndev->features |= NETIF_F_IP_CSUM | NETIF_F_SG;
	} else
		ndev->hard_start_xmit = &emac_start_xmit;
	ndev->tx_timeout = &emac_full_tx_reset;
	ndev->watchdog_timeo = 5 * HZ;
	ndev->stop = &emac_close;
	ndev->get_stats = &emac_stats;
	ndev->set_multicast_list = &emac_set_multicast_list;
	ndev->do_ioctl = &emac_ioctl;
	if (emac_phy_supports_gige(emacdata->phy_mode)) {
		ndev->change_mtu = &emac_change_mtu;
		dev->commac.ops = &emac_commac_sg_ops;
	}
	SET_ETHTOOL_OPS(ndev, &emac_ethtool_ops);

	netif_carrier_off(ndev);
	netif_stop_queue(ndev);

	err = register_netdev(ndev);
	if (err) {
		printk(KERN_ERR "emac%d: failed to register net device (%d)!\n",
		       dev->def->index, err);
		goto out6;
	}

	ocp_set_drvdata(ocpdev, dev);

	printk("%s: emac%d, MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
	       ndev->name, dev->def->index,
	       ndev->dev_addr[0], ndev->dev_addr[1], ndev->dev_addr[2],
	       ndev->dev_addr[3], ndev->dev_addr[4], ndev->dev_addr[5]);

	if (dev->phy.address >= 0)
		printk("%s: found %s PHY (0x%02x)\n", ndev->name,
		       dev->phy.def->name, dev->phy.address);

	emac_dbg_register(dev->def->index, dev);

	return 0;
      out6:
	iounmap((void *)dev->emacp);
      out5:
	tah_fini(dev->tah_dev);
      out4:
	rgmii_fini(dev->rgmii_dev, dev->rgmii_input);
      out3:
	zmii_fini(dev->zmii_dev, dev->zmii_input);
      out2:
	mal_unregister_commac(dev->mal, &dev->commac);
      out:
	kfree(ndev);
	return err;
}

static struct ocp_device_id emac_ids[] = {
	{ .vendor = OCP_VENDOR_IBM, .function = OCP_FUNC_EMAC },
	{ .vendor = OCP_VENDOR_INVALID}
};

static struct ocp_driver emac_driver = {
	.name = "emac",
	.id_table = emac_ids,
	.probe = emac_probe,
	.remove = emac_remove,
};

static int __init emac_init(void)
{
	printk(KERN_INFO DRV_DESC ", version " DRV_VERSION "\n");

	DBG(": init" NL);

	if (mal_init())
		return -ENODEV;

	EMAC_CLK_INTERNAL;
	if (ocp_register_driver(&emac_driver)) {
		EMAC_CLK_EXTERNAL;
		ocp_unregister_driver(&emac_driver);
		mal_exit();
		return -ENODEV;
	}
	EMAC_CLK_EXTERNAL;

	emac_init_debug();
	return 0;
}

static void __exit emac_exit(void)
{
	DBG(": exit" NL);
	ocp_unregister_driver(&emac_driver);
	mal_exit();
	emac_fini_debug();
}

module_init(emac_init);
module_exit(emac_exit);
