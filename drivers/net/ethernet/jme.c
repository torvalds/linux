/*
 * JMicron JMC2x0 series PCIe Ethernet Linux Device Driver
 *
 * Copyright 2008 JMicron Technology Corporation
 * http://www.jmicron.com/
 * Copyright (c) 2009 - 2010 Guo-Fu Tseng <cooldavid@cooldavid.org>
 *
 * Author: Guo-Fu Tseng <cooldavid@cooldavid.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/crc32.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/if_vlan.h>
#include <linux/slab.h>
#include <net/ip6_checksum.h>
#include "jme.h"

static int force_pseudohp = -1;
static int no_pseudohp = -1;
static int no_extplug = -1;
module_param(force_pseudohp, int, 0);
MODULE_PARM_DESC(force_pseudohp,
	"Enable pseudo hot-plug feature manually by driver instead of BIOS.");
module_param(no_pseudohp, int, 0);
MODULE_PARM_DESC(no_pseudohp, "Disable pseudo hot-plug feature.");
module_param(no_extplug, int, 0);
MODULE_PARM_DESC(no_extplug,
	"Do not use external plug signal for pseudo hot-plug.");

static int
jme_mdio_read(struct net_device *netdev, int phy, int reg)
{
	struct jme_adapter *jme = netdev_priv(netdev);
	int i, val, again = (reg == MII_BMSR) ? 1 : 0;

read_again:
	jwrite32(jme, JME_SMI, SMI_OP_REQ |
				smi_phy_addr(phy) |
				smi_reg_addr(reg));

	wmb();
	for (i = JME_PHY_TIMEOUT * 50 ; i > 0 ; --i) {
		udelay(20);
		val = jread32(jme, JME_SMI);
		if ((val & SMI_OP_REQ) == 0)
			break;
	}

	if (i == 0) {
		pr_err("phy(%d) read timeout : %d\n", phy, reg);
		return 0;
	}

	if (again--)
		goto read_again;

	return (val & SMI_DATA_MASK) >> SMI_DATA_SHIFT;
}

static void
jme_mdio_write(struct net_device *netdev,
				int phy, int reg, int val)
{
	struct jme_adapter *jme = netdev_priv(netdev);
	int i;

	jwrite32(jme, JME_SMI, SMI_OP_WRITE | SMI_OP_REQ |
		((val << SMI_DATA_SHIFT) & SMI_DATA_MASK) |
		smi_phy_addr(phy) | smi_reg_addr(reg));

	wmb();
	for (i = JME_PHY_TIMEOUT * 50 ; i > 0 ; --i) {
		udelay(20);
		if ((jread32(jme, JME_SMI) & SMI_OP_REQ) == 0)
			break;
	}

	if (i == 0)
		pr_err("phy(%d) write timeout : %d\n", phy, reg);
}

static inline void
jme_reset_phy_processor(struct jme_adapter *jme)
{
	u32 val;

	jme_mdio_write(jme->dev,
			jme->mii_if.phy_id,
			MII_ADVERTISE, ADVERTISE_ALL |
			ADVERTISE_PAUSE_CAP | ADVERTISE_PAUSE_ASYM);

	if (jme->pdev->device == PCI_DEVICE_ID_JMICRON_JMC250)
		jme_mdio_write(jme->dev,
				jme->mii_if.phy_id,
				MII_CTRL1000,
				ADVERTISE_1000FULL | ADVERTISE_1000HALF);

	val = jme_mdio_read(jme->dev,
				jme->mii_if.phy_id,
				MII_BMCR);

	jme_mdio_write(jme->dev,
			jme->mii_if.phy_id,
			MII_BMCR, val | BMCR_RESET);
}

static void
jme_setup_wakeup_frame(struct jme_adapter *jme,
		       const u32 *mask, u32 crc, int fnr)
{
	int i;

	/*
	 * Setup CRC pattern
	 */
	jwrite32(jme, JME_WFOI, WFOI_CRC_SEL | (fnr & WFOI_FRAME_SEL));
	wmb();
	jwrite32(jme, JME_WFODP, crc);
	wmb();

	/*
	 * Setup Mask
	 */
	for (i = 0 ; i < WAKEUP_FRAME_MASK_DWNR ; ++i) {
		jwrite32(jme, JME_WFOI,
				((i << WFOI_MASK_SHIFT) & WFOI_MASK_SEL) |
				(fnr & WFOI_FRAME_SEL));
		wmb();
		jwrite32(jme, JME_WFODP, mask[i]);
		wmb();
	}
}

static inline void
jme_mac_rxclk_off(struct jme_adapter *jme)
{
	jme->reg_gpreg1 |= GPREG1_RXCLKOFF;
	jwrite32f(jme, JME_GPREG1, jme->reg_gpreg1);
}

static inline void
jme_mac_rxclk_on(struct jme_adapter *jme)
{
	jme->reg_gpreg1 &= ~GPREG1_RXCLKOFF;
	jwrite32f(jme, JME_GPREG1, jme->reg_gpreg1);
}

static inline void
jme_mac_txclk_off(struct jme_adapter *jme)
{
	jme->reg_ghc &= ~(GHC_TO_CLK_SRC | GHC_TXMAC_CLK_SRC);
	jwrite32f(jme, JME_GHC, jme->reg_ghc);
}

static inline void
jme_mac_txclk_on(struct jme_adapter *jme)
{
	u32 speed = jme->reg_ghc & GHC_SPEED;
	if (speed == GHC_SPEED_1000M)
		jme->reg_ghc |= GHC_TO_CLK_GPHY | GHC_TXMAC_CLK_GPHY;
	else
		jme->reg_ghc |= GHC_TO_CLK_PCIE | GHC_TXMAC_CLK_PCIE;
	jwrite32f(jme, JME_GHC, jme->reg_ghc);
}

static inline void
jme_reset_ghc_speed(struct jme_adapter *jme)
{
	jme->reg_ghc &= ~(GHC_SPEED | GHC_DPX);
	jwrite32f(jme, JME_GHC, jme->reg_ghc);
}

static inline void
jme_reset_250A2_workaround(struct jme_adapter *jme)
{
	jme->reg_gpreg1 &= ~(GPREG1_HALFMODEPATCH |
			     GPREG1_RSSPATCH);
	jwrite32(jme, JME_GPREG1, jme->reg_gpreg1);
}

static inline void
jme_assert_ghc_reset(struct jme_adapter *jme)
{
	jme->reg_ghc |= GHC_SWRST;
	jwrite32f(jme, JME_GHC, jme->reg_ghc);
}

static inline void
jme_clear_ghc_reset(struct jme_adapter *jme)
{
	jme->reg_ghc &= ~GHC_SWRST;
	jwrite32f(jme, JME_GHC, jme->reg_ghc);
}

static inline void
jme_reset_mac_processor(struct jme_adapter *jme)
{
	static const u32 mask[WAKEUP_FRAME_MASK_DWNR] = {0, 0, 0, 0};
	u32 crc = 0xCDCDCDCD;
	u32 gpreg0;
	int i;

	jme_reset_ghc_speed(jme);
	jme_reset_250A2_workaround(jme);

	jme_mac_rxclk_on(jme);
	jme_mac_txclk_on(jme);
	udelay(1);
	jme_assert_ghc_reset(jme);
	udelay(1);
	jme_mac_rxclk_off(jme);
	jme_mac_txclk_off(jme);
	udelay(1);
	jme_clear_ghc_reset(jme);
	udelay(1);
	jme_mac_rxclk_on(jme);
	jme_mac_txclk_on(jme);
	udelay(1);
	jme_mac_rxclk_off(jme);
	jme_mac_txclk_off(jme);

	jwrite32(jme, JME_RXDBA_LO, 0x00000000);
	jwrite32(jme, JME_RXDBA_HI, 0x00000000);
	jwrite32(jme, JME_RXQDC, 0x00000000);
	jwrite32(jme, JME_RXNDA, 0x00000000);
	jwrite32(jme, JME_TXDBA_LO, 0x00000000);
	jwrite32(jme, JME_TXDBA_HI, 0x00000000);
	jwrite32(jme, JME_TXQDC, 0x00000000);
	jwrite32(jme, JME_TXNDA, 0x00000000);

	jwrite32(jme, JME_RXMCHT_LO, 0x00000000);
	jwrite32(jme, JME_RXMCHT_HI, 0x00000000);
	for (i = 0 ; i < WAKEUP_FRAME_NR ; ++i)
		jme_setup_wakeup_frame(jme, mask, crc, i);
	if (jme->fpgaver)
		gpreg0 = GPREG0_DEFAULT | GPREG0_LNKINTPOLL;
	else
		gpreg0 = GPREG0_DEFAULT;
	jwrite32(jme, JME_GPREG0, gpreg0);
}

static inline void
jme_clear_pm(struct jme_adapter *jme)
{
	jwrite32(jme, JME_PMCS, PMCS_STMASK | jme->reg_pmcs);
}

static int
jme_reload_eeprom(struct jme_adapter *jme)
{
	u32 val;
	int i;

	val = jread32(jme, JME_SMBCSR);

	if (val & SMBCSR_EEPROMD) {
		val |= SMBCSR_CNACK;
		jwrite32(jme, JME_SMBCSR, val);
		val |= SMBCSR_RELOAD;
		jwrite32(jme, JME_SMBCSR, val);
		mdelay(12);

		for (i = JME_EEPROM_RELOAD_TIMEOUT; i > 0; --i) {
			mdelay(1);
			if ((jread32(jme, JME_SMBCSR) & SMBCSR_RELOAD) == 0)
				break;
		}

		if (i == 0) {
			pr_err("eeprom reload timeout\n");
			return -EIO;
		}
	}

	return 0;
}

static void
jme_load_macaddr(struct net_device *netdev)
{
	struct jme_adapter *jme = netdev_priv(netdev);
	unsigned char macaddr[6];
	u32 val;

	spin_lock_bh(&jme->macaddr_lock);
	val = jread32(jme, JME_RXUMA_LO);
	macaddr[0] = (val >>  0) & 0xFF;
	macaddr[1] = (val >>  8) & 0xFF;
	macaddr[2] = (val >> 16) & 0xFF;
	macaddr[3] = (val >> 24) & 0xFF;
	val = jread32(jme, JME_RXUMA_HI);
	macaddr[4] = (val >>  0) & 0xFF;
	macaddr[5] = (val >>  8) & 0xFF;
	memcpy(netdev->dev_addr, macaddr, 6);
	spin_unlock_bh(&jme->macaddr_lock);
}

static inline void
jme_set_rx_pcc(struct jme_adapter *jme, int p)
{
	switch (p) {
	case PCC_OFF:
		jwrite32(jme, JME_PCCRX0,
			((PCC_OFF_TO << PCCRXTO_SHIFT) & PCCRXTO_MASK) |
			((PCC_OFF_CNT << PCCRX_SHIFT) & PCCRX_MASK));
		break;
	case PCC_P1:
		jwrite32(jme, JME_PCCRX0,
			((PCC_P1_TO << PCCRXTO_SHIFT) & PCCRXTO_MASK) |
			((PCC_P1_CNT << PCCRX_SHIFT) & PCCRX_MASK));
		break;
	case PCC_P2:
		jwrite32(jme, JME_PCCRX0,
			((PCC_P2_TO << PCCRXTO_SHIFT) & PCCRXTO_MASK) |
			((PCC_P2_CNT << PCCRX_SHIFT) & PCCRX_MASK));
		break;
	case PCC_P3:
		jwrite32(jme, JME_PCCRX0,
			((PCC_P3_TO << PCCRXTO_SHIFT) & PCCRXTO_MASK) |
			((PCC_P3_CNT << PCCRX_SHIFT) & PCCRX_MASK));
		break;
	default:
		break;
	}
	wmb();

	if (!(test_bit(JME_FLAG_POLL, &jme->flags)))
		netif_info(jme, rx_status, jme->dev, "Switched to PCC_P%d\n", p);
}

static void
jme_start_irq(struct jme_adapter *jme)
{
	register struct dynpcc_info *dpi = &(jme->dpi);

	jme_set_rx_pcc(jme, PCC_P1);
	dpi->cur		= PCC_P1;
	dpi->attempt		= PCC_P1;
	dpi->cnt		= 0;

	jwrite32(jme, JME_PCCTX,
			((PCC_TX_TO << PCCTXTO_SHIFT) & PCCTXTO_MASK) |
			((PCC_TX_CNT << PCCTX_SHIFT) & PCCTX_MASK) |
			PCCTXQ0_EN
		);

	/*
	 * Enable Interrupts
	 */
	jwrite32(jme, JME_IENS, INTR_ENABLE);
}

static inline void
jme_stop_irq(struct jme_adapter *jme)
{
	/*
	 * Disable Interrupts
	 */
	jwrite32f(jme, JME_IENC, INTR_ENABLE);
}

static u32
jme_linkstat_from_phy(struct jme_adapter *jme)
{
	u32 phylink, bmsr;

	phylink = jme_mdio_read(jme->dev, jme->mii_if.phy_id, 17);
	bmsr = jme_mdio_read(jme->dev, jme->mii_if.phy_id, MII_BMSR);
	if (bmsr & BMSR_ANCOMP)
		phylink |= PHY_LINK_AUTONEG_COMPLETE;

	return phylink;
}

static inline void
jme_set_phyfifo_5level(struct jme_adapter *jme)
{
	jme_mdio_write(jme->dev, jme->mii_if.phy_id, 27, 0x0004);
}

static inline void
jme_set_phyfifo_8level(struct jme_adapter *jme)
{
	jme_mdio_write(jme->dev, jme->mii_if.phy_id, 27, 0x0000);
}

static int
jme_check_link(struct net_device *netdev, int testonly)
{
	struct jme_adapter *jme = netdev_priv(netdev);
	u32 phylink, cnt = JME_SPDRSV_TIMEOUT, bmcr;
	char linkmsg[64];
	int rc = 0;

	linkmsg[0] = '\0';

	if (jme->fpgaver)
		phylink = jme_linkstat_from_phy(jme);
	else
		phylink = jread32(jme, JME_PHY_LINK);

	if (phylink & PHY_LINK_UP) {
		if (!(phylink & PHY_LINK_AUTONEG_COMPLETE)) {
			/*
			 * If we did not enable AN
			 * Speed/Duplex Info should be obtained from SMI
			 */
			phylink = PHY_LINK_UP;

			bmcr = jme_mdio_read(jme->dev,
						jme->mii_if.phy_id,
						MII_BMCR);

			phylink |= ((bmcr & BMCR_SPEED1000) &&
					(bmcr & BMCR_SPEED100) == 0) ?
					PHY_LINK_SPEED_1000M :
					(bmcr & BMCR_SPEED100) ?
					PHY_LINK_SPEED_100M :
					PHY_LINK_SPEED_10M;

			phylink |= (bmcr & BMCR_FULLDPLX) ?
					 PHY_LINK_DUPLEX : 0;

			strcat(linkmsg, "Forced: ");
		} else {
			/*
			 * Keep polling for speed/duplex resolve complete
			 */
			while (!(phylink & PHY_LINK_SPEEDDPU_RESOLVED) &&
				--cnt) {

				udelay(1);

				if (jme->fpgaver)
					phylink = jme_linkstat_from_phy(jme);
				else
					phylink = jread32(jme, JME_PHY_LINK);
			}
			if (!cnt)
				pr_err("Waiting speed resolve timeout\n");

			strcat(linkmsg, "ANed: ");
		}

		if (jme->phylink == phylink) {
			rc = 1;
			goto out;
		}
		if (testonly)
			goto out;

		jme->phylink = phylink;

		/*
		 * The speed/duplex setting of jme->reg_ghc already cleared
		 * by jme_reset_mac_processor()
		 */
		switch (phylink & PHY_LINK_SPEED_MASK) {
		case PHY_LINK_SPEED_10M:
			jme->reg_ghc |= GHC_SPEED_10M;
			strcat(linkmsg, "10 Mbps, ");
			break;
		case PHY_LINK_SPEED_100M:
			jme->reg_ghc |= GHC_SPEED_100M;
			strcat(linkmsg, "100 Mbps, ");
			break;
		case PHY_LINK_SPEED_1000M:
			jme->reg_ghc |= GHC_SPEED_1000M;
			strcat(linkmsg, "1000 Mbps, ");
			break;
		default:
			break;
		}

		if (phylink & PHY_LINK_DUPLEX) {
			jwrite32(jme, JME_TXMCS, TXMCS_DEFAULT);
			jwrite32(jme, JME_TXTRHD, TXTRHD_FULLDUPLEX);
			jme->reg_ghc |= GHC_DPX;
		} else {
			jwrite32(jme, JME_TXMCS, TXMCS_DEFAULT |
						TXMCS_BACKOFF |
						TXMCS_CARRIERSENSE |
						TXMCS_COLLISION);
			jwrite32(jme, JME_TXTRHD, TXTRHD_HALFDUPLEX);
		}

		jwrite32(jme, JME_GHC, jme->reg_ghc);

		if (is_buggy250(jme->pdev->device, jme->chiprev)) {
			jme->reg_gpreg1 &= ~(GPREG1_HALFMODEPATCH |
					     GPREG1_RSSPATCH);
			if (!(phylink & PHY_LINK_DUPLEX))
				jme->reg_gpreg1 |= GPREG1_HALFMODEPATCH;
			switch (phylink & PHY_LINK_SPEED_MASK) {
			case PHY_LINK_SPEED_10M:
				jme_set_phyfifo_8level(jme);
				jme->reg_gpreg1 |= GPREG1_RSSPATCH;
				break;
			case PHY_LINK_SPEED_100M:
				jme_set_phyfifo_5level(jme);
				jme->reg_gpreg1 |= GPREG1_RSSPATCH;
				break;
			case PHY_LINK_SPEED_1000M:
				jme_set_phyfifo_8level(jme);
				break;
			default:
				break;
			}
		}
		jwrite32(jme, JME_GPREG1, jme->reg_gpreg1);

		strcat(linkmsg, (phylink & PHY_LINK_DUPLEX) ?
					"Full-Duplex, " :
					"Half-Duplex, ");
		strcat(linkmsg, (phylink & PHY_LINK_MDI_STAT) ?
					"MDI-X" :
					"MDI");
		netif_info(jme, link, jme->dev, "Link is up at %s\n", linkmsg);
		netif_carrier_on(netdev);
	} else {
		if (testonly)
			goto out;

		netif_info(jme, link, jme->dev, "Link is down\n");
		jme->phylink = 0;
		netif_carrier_off(netdev);
	}

out:
	return rc;
}

static int
jme_setup_tx_resources(struct jme_adapter *jme)
{
	struct jme_ring *txring = &(jme->txring[0]);

	txring->alloc = dma_alloc_coherent(&(jme->pdev->dev),
				   TX_RING_ALLOC_SIZE(jme->tx_ring_size),
				   &(txring->dmaalloc),
				   GFP_ATOMIC);

	if (!txring->alloc)
		goto err_set_null;

	/*
	 * 16 Bytes align
	 */
	txring->desc		= (void *)ALIGN((unsigned long)(txring->alloc),
						RING_DESC_ALIGN);
	txring->dma		= ALIGN(txring->dmaalloc, RING_DESC_ALIGN);
	txring->next_to_use	= 0;
	atomic_set(&txring->next_to_clean, 0);
	atomic_set(&txring->nr_free, jme->tx_ring_size);

	txring->bufinf		= kmalloc(sizeof(struct jme_buffer_info) *
					jme->tx_ring_size, GFP_ATOMIC);
	if (unlikely(!(txring->bufinf)))
		goto err_free_txring;

	/*
	 * Initialize Transmit Descriptors
	 */
	memset(txring->alloc, 0, TX_RING_ALLOC_SIZE(jme->tx_ring_size));
	memset(txring->bufinf, 0,
		sizeof(struct jme_buffer_info) * jme->tx_ring_size);

	return 0;

err_free_txring:
	dma_free_coherent(&(jme->pdev->dev),
			  TX_RING_ALLOC_SIZE(jme->tx_ring_size),
			  txring->alloc,
			  txring->dmaalloc);

err_set_null:
	txring->desc = NULL;
	txring->dmaalloc = 0;
	txring->dma = 0;
	txring->bufinf = NULL;

	return -ENOMEM;
}

static void
jme_free_tx_resources(struct jme_adapter *jme)
{
	int i;
	struct jme_ring *txring = &(jme->txring[0]);
	struct jme_buffer_info *txbi;

	if (txring->alloc) {
		if (txring->bufinf) {
			for (i = 0 ; i < jme->tx_ring_size ; ++i) {
				txbi = txring->bufinf + i;
				if (txbi->skb) {
					dev_kfree_skb(txbi->skb);
					txbi->skb = NULL;
				}
				txbi->mapping		= 0;
				txbi->len		= 0;
				txbi->nr_desc		= 0;
				txbi->start_xmit	= 0;
			}
			kfree(txring->bufinf);
		}

		dma_free_coherent(&(jme->pdev->dev),
				  TX_RING_ALLOC_SIZE(jme->tx_ring_size),
				  txring->alloc,
				  txring->dmaalloc);

		txring->alloc		= NULL;
		txring->desc		= NULL;
		txring->dmaalloc	= 0;
		txring->dma		= 0;
		txring->bufinf		= NULL;
	}
	txring->next_to_use	= 0;
	atomic_set(&txring->next_to_clean, 0);
	atomic_set(&txring->nr_free, 0);
}

static inline void
jme_enable_tx_engine(struct jme_adapter *jme)
{
	/*
	 * Select Queue 0
	 */
	jwrite32(jme, JME_TXCS, TXCS_DEFAULT | TXCS_SELECT_QUEUE0);
	wmb();

	/*
	 * Setup TX Queue 0 DMA Bass Address
	 */
	jwrite32(jme, JME_TXDBA_LO, (__u64)jme->txring[0].dma & 0xFFFFFFFFUL);
	jwrite32(jme, JME_TXDBA_HI, (__u64)(jme->txring[0].dma) >> 32);
	jwrite32(jme, JME_TXNDA, (__u64)jme->txring[0].dma & 0xFFFFFFFFUL);

	/*
	 * Setup TX Descptor Count
	 */
	jwrite32(jme, JME_TXQDC, jme->tx_ring_size);

	/*
	 * Enable TX Engine
	 */
	wmb();
	jwrite32f(jme, JME_TXCS, jme->reg_txcs |
				TXCS_SELECT_QUEUE0 |
				TXCS_ENABLE);

	/*
	 * Start clock for TX MAC Processor
	 */
	jme_mac_txclk_on(jme);
}

static inline void
jme_restart_tx_engine(struct jme_adapter *jme)
{
	/*
	 * Restart TX Engine
	 */
	jwrite32(jme, JME_TXCS, jme->reg_txcs |
				TXCS_SELECT_QUEUE0 |
				TXCS_ENABLE);
}

static inline void
jme_disable_tx_engine(struct jme_adapter *jme)
{
	int i;
	u32 val;

	/*
	 * Disable TX Engine
	 */
	jwrite32(jme, JME_TXCS, jme->reg_txcs | TXCS_SELECT_QUEUE0);
	wmb();

	val = jread32(jme, JME_TXCS);
	for (i = JME_TX_DISABLE_TIMEOUT ; (val & TXCS_ENABLE) && i > 0 ; --i) {
		mdelay(1);
		val = jread32(jme, JME_TXCS);
		rmb();
	}

	if (!i)
		pr_err("Disable TX engine timeout\n");

	/*
	 * Stop clock for TX MAC Processor
	 */
	jme_mac_txclk_off(jme);
}

static void
jme_set_clean_rxdesc(struct jme_adapter *jme, int i)
{
	struct jme_ring *rxring = &(jme->rxring[0]);
	register struct rxdesc *rxdesc = rxring->desc;
	struct jme_buffer_info *rxbi = rxring->bufinf;
	rxdesc += i;
	rxbi += i;

	rxdesc->dw[0] = 0;
	rxdesc->dw[1] = 0;
	rxdesc->desc1.bufaddrh	= cpu_to_le32((__u64)rxbi->mapping >> 32);
	rxdesc->desc1.bufaddrl	= cpu_to_le32(
					(__u64)rxbi->mapping & 0xFFFFFFFFUL);
	rxdesc->desc1.datalen	= cpu_to_le16(rxbi->len);
	if (jme->dev->features & NETIF_F_HIGHDMA)
		rxdesc->desc1.flags = RXFLAG_64BIT;
	wmb();
	rxdesc->desc1.flags	|= RXFLAG_OWN | RXFLAG_INT;
}

static int
jme_make_new_rx_buf(struct jme_adapter *jme, int i)
{
	struct jme_ring *rxring = &(jme->rxring[0]);
	struct jme_buffer_info *rxbi = rxring->bufinf + i;
	struct sk_buff *skb;
	dma_addr_t mapping;

	skb = netdev_alloc_skb(jme->dev,
		jme->dev->mtu + RX_EXTRA_LEN);
	if (unlikely(!skb))
		return -ENOMEM;

	mapping = pci_map_page(jme->pdev, virt_to_page(skb->data),
			       offset_in_page(skb->data), skb_tailroom(skb),
			       PCI_DMA_FROMDEVICE);
	if (unlikely(pci_dma_mapping_error(jme->pdev, mapping))) {
		dev_kfree_skb(skb);
		return -ENOMEM;
	}

	if (likely(rxbi->mapping))
		pci_unmap_page(jme->pdev, rxbi->mapping,
			       rxbi->len, PCI_DMA_FROMDEVICE);

	rxbi->skb = skb;
	rxbi->len = skb_tailroom(skb);
	rxbi->mapping = mapping;
	return 0;
}

static void
jme_free_rx_buf(struct jme_adapter *jme, int i)
{
	struct jme_ring *rxring = &(jme->rxring[0]);
	struct jme_buffer_info *rxbi = rxring->bufinf;
	rxbi += i;

	if (rxbi->skb) {
		pci_unmap_page(jme->pdev,
				 rxbi->mapping,
				 rxbi->len,
				 PCI_DMA_FROMDEVICE);
		dev_kfree_skb(rxbi->skb);
		rxbi->skb = NULL;
		rxbi->mapping = 0;
		rxbi->len = 0;
	}
}

static void
jme_free_rx_resources(struct jme_adapter *jme)
{
	int i;
	struct jme_ring *rxring = &(jme->rxring[0]);

	if (rxring->alloc) {
		if (rxring->bufinf) {
			for (i = 0 ; i < jme->rx_ring_size ; ++i)
				jme_free_rx_buf(jme, i);
			kfree(rxring->bufinf);
		}

		dma_free_coherent(&(jme->pdev->dev),
				  RX_RING_ALLOC_SIZE(jme->rx_ring_size),
				  rxring->alloc,
				  rxring->dmaalloc);
		rxring->alloc    = NULL;
		rxring->desc     = NULL;
		rxring->dmaalloc = 0;
		rxring->dma      = 0;
		rxring->bufinf   = NULL;
	}
	rxring->next_to_use   = 0;
	atomic_set(&rxring->next_to_clean, 0);
}

static int
jme_setup_rx_resources(struct jme_adapter *jme)
{
	int i;
	struct jme_ring *rxring = &(jme->rxring[0]);

	rxring->alloc = dma_alloc_coherent(&(jme->pdev->dev),
				   RX_RING_ALLOC_SIZE(jme->rx_ring_size),
				   &(rxring->dmaalloc),
				   GFP_ATOMIC);
	if (!rxring->alloc)
		goto err_set_null;

	/*
	 * 16 Bytes align
	 */
	rxring->desc		= (void *)ALIGN((unsigned long)(rxring->alloc),
						RING_DESC_ALIGN);
	rxring->dma		= ALIGN(rxring->dmaalloc, RING_DESC_ALIGN);
	rxring->next_to_use	= 0;
	atomic_set(&rxring->next_to_clean, 0);

	rxring->bufinf		= kmalloc(sizeof(struct jme_buffer_info) *
					jme->rx_ring_size, GFP_ATOMIC);
	if (unlikely(!(rxring->bufinf)))
		goto err_free_rxring;

	/*
	 * Initiallize Receive Descriptors
	 */
	memset(rxring->bufinf, 0,
		sizeof(struct jme_buffer_info) * jme->rx_ring_size);
	for (i = 0 ; i < jme->rx_ring_size ; ++i) {
		if (unlikely(jme_make_new_rx_buf(jme, i))) {
			jme_free_rx_resources(jme);
			return -ENOMEM;
		}

		jme_set_clean_rxdesc(jme, i);
	}

	return 0;

err_free_rxring:
	dma_free_coherent(&(jme->pdev->dev),
			  RX_RING_ALLOC_SIZE(jme->rx_ring_size),
			  rxring->alloc,
			  rxring->dmaalloc);
err_set_null:
	rxring->desc = NULL;
	rxring->dmaalloc = 0;
	rxring->dma = 0;
	rxring->bufinf = NULL;

	return -ENOMEM;
}

static inline void
jme_enable_rx_engine(struct jme_adapter *jme)
{
	/*
	 * Select Queue 0
	 */
	jwrite32(jme, JME_RXCS, jme->reg_rxcs |
				RXCS_QUEUESEL_Q0);
	wmb();

	/*
	 * Setup RX DMA Bass Address
	 */
	jwrite32(jme, JME_RXDBA_LO, (__u64)(jme->rxring[0].dma) & 0xFFFFFFFFUL);
	jwrite32(jme, JME_RXDBA_HI, (__u64)(jme->rxring[0].dma) >> 32);
	jwrite32(jme, JME_RXNDA, (__u64)(jme->rxring[0].dma) & 0xFFFFFFFFUL);

	/*
	 * Setup RX Descriptor Count
	 */
	jwrite32(jme, JME_RXQDC, jme->rx_ring_size);

	/*
	 * Setup Unicast Filter
	 */
	jme_set_unicastaddr(jme->dev);
	jme_set_multi(jme->dev);

	/*
	 * Enable RX Engine
	 */
	wmb();
	jwrite32f(jme, JME_RXCS, jme->reg_rxcs |
				RXCS_QUEUESEL_Q0 |
				RXCS_ENABLE |
				RXCS_QST);

	/*
	 * Start clock for RX MAC Processor
	 */
	jme_mac_rxclk_on(jme);
}

static inline void
jme_restart_rx_engine(struct jme_adapter *jme)
{
	/*
	 * Start RX Engine
	 */
	jwrite32(jme, JME_RXCS, jme->reg_rxcs |
				RXCS_QUEUESEL_Q0 |
				RXCS_ENABLE |
				RXCS_QST);
}

static inline void
jme_disable_rx_engine(struct jme_adapter *jme)
{
	int i;
	u32 val;

	/*
	 * Disable RX Engine
	 */
	jwrite32(jme, JME_RXCS, jme->reg_rxcs);
	wmb();

	val = jread32(jme, JME_RXCS);
	for (i = JME_RX_DISABLE_TIMEOUT ; (val & RXCS_ENABLE) && i > 0 ; --i) {
		mdelay(1);
		val = jread32(jme, JME_RXCS);
		rmb();
	}

	if (!i)
		pr_err("Disable RX engine timeout\n");

	/*
	 * Stop clock for RX MAC Processor
	 */
	jme_mac_rxclk_off(jme);
}

static u16
jme_udpsum(struct sk_buff *skb)
{
	u16 csum = 0xFFFFu;

	if (skb->len < (ETH_HLEN + sizeof(struct iphdr)))
		return csum;
	if (skb->protocol != htons(ETH_P_IP))
		return csum;
	skb_set_network_header(skb, ETH_HLEN);
	if ((ip_hdr(skb)->protocol != IPPROTO_UDP) ||
	    (skb->len < (ETH_HLEN +
			(ip_hdr(skb)->ihl << 2) +
			sizeof(struct udphdr)))) {
		skb_reset_network_header(skb);
		return csum;
	}
	skb_set_transport_header(skb,
			ETH_HLEN + (ip_hdr(skb)->ihl << 2));
	csum = udp_hdr(skb)->check;
	skb_reset_transport_header(skb);
	skb_reset_network_header(skb);

	return csum;
}

static int
jme_rxsum_ok(struct jme_adapter *jme, u16 flags, struct sk_buff *skb)
{
	if (!(flags & (RXWBFLAG_TCPON | RXWBFLAG_UDPON | RXWBFLAG_IPV4)))
		return false;

	if (unlikely((flags & (RXWBFLAG_MF | RXWBFLAG_TCPON | RXWBFLAG_TCPCS))
			== RXWBFLAG_TCPON)) {
		if (flags & RXWBFLAG_IPV4)
			netif_err(jme, rx_err, jme->dev, "TCP Checksum error\n");
		return false;
	}

	if (unlikely((flags & (RXWBFLAG_MF | RXWBFLAG_UDPON | RXWBFLAG_UDPCS))
			== RXWBFLAG_UDPON) && jme_udpsum(skb)) {
		if (flags & RXWBFLAG_IPV4)
			netif_err(jme, rx_err, jme->dev, "UDP Checksum error\n");
		return false;
	}

	if (unlikely((flags & (RXWBFLAG_IPV4 | RXWBFLAG_IPCS))
			== RXWBFLAG_IPV4)) {
		netif_err(jme, rx_err, jme->dev, "IPv4 Checksum error\n");
		return false;
	}

	return true;
}

static void
jme_alloc_and_feed_skb(struct jme_adapter *jme, int idx)
{
	struct jme_ring *rxring = &(jme->rxring[0]);
	struct rxdesc *rxdesc = rxring->desc;
	struct jme_buffer_info *rxbi = rxring->bufinf;
	struct sk_buff *skb;
	int framesize;

	rxdesc += idx;
	rxbi += idx;

	skb = rxbi->skb;
	pci_dma_sync_single_for_cpu(jme->pdev,
					rxbi->mapping,
					rxbi->len,
					PCI_DMA_FROMDEVICE);

	if (unlikely(jme_make_new_rx_buf(jme, idx))) {
		pci_dma_sync_single_for_device(jme->pdev,
						rxbi->mapping,
						rxbi->len,
						PCI_DMA_FROMDEVICE);

		++(NET_STAT(jme).rx_dropped);
	} else {
		framesize = le16_to_cpu(rxdesc->descwb.framesize)
				- RX_PREPAD_SIZE;

		skb_reserve(skb, RX_PREPAD_SIZE);
		skb_put(skb, framesize);
		skb->protocol = eth_type_trans(skb, jme->dev);

		if (jme_rxsum_ok(jme, le16_to_cpu(rxdesc->descwb.flags), skb))
			skb->ip_summed = CHECKSUM_UNNECESSARY;
		else
			skb_checksum_none_assert(skb);

		if (rxdesc->descwb.flags & cpu_to_le16(RXWBFLAG_TAGON)) {
			u16 vid = le16_to_cpu(rxdesc->descwb.vlan);

			__vlan_hwaccel_put_tag(skb, vid);
			NET_STAT(jme).rx_bytes += 4;
		}
		jme->jme_rx(skb);

		if ((rxdesc->descwb.flags & cpu_to_le16(RXWBFLAG_DEST)) ==
		    cpu_to_le16(RXWBFLAG_DEST_MUL))
			++(NET_STAT(jme).multicast);

		NET_STAT(jme).rx_bytes += framesize;
		++(NET_STAT(jme).rx_packets);
	}

	jme_set_clean_rxdesc(jme, idx);

}

static int
jme_process_receive(struct jme_adapter *jme, int limit)
{
	struct jme_ring *rxring = &(jme->rxring[0]);
	struct rxdesc *rxdesc = rxring->desc;
	int i, j, ccnt, desccnt, mask = jme->rx_ring_mask;

	if (unlikely(!atomic_dec_and_test(&jme->rx_cleaning)))
		goto out_inc;

	if (unlikely(atomic_read(&jme->link_changing) != 1))
		goto out_inc;

	if (unlikely(!netif_carrier_ok(jme->dev)))
		goto out_inc;

	i = atomic_read(&rxring->next_to_clean);
	while (limit > 0) {
		rxdesc = rxring->desc;
		rxdesc += i;

		if ((rxdesc->descwb.flags & cpu_to_le16(RXWBFLAG_OWN)) ||
		!(rxdesc->descwb.desccnt & RXWBDCNT_WBCPL))
			goto out;
		--limit;

		rmb();
		desccnt = rxdesc->descwb.desccnt & RXWBDCNT_DCNT;

		if (unlikely(desccnt > 1 ||
		rxdesc->descwb.errstat & RXWBERR_ALLERR)) {

			if (rxdesc->descwb.errstat & RXWBERR_CRCERR)
				++(NET_STAT(jme).rx_crc_errors);
			else if (rxdesc->descwb.errstat & RXWBERR_OVERUN)
				++(NET_STAT(jme).rx_fifo_errors);
			else
				++(NET_STAT(jme).rx_errors);

			if (desccnt > 1)
				limit -= desccnt - 1;

			for (j = i, ccnt = desccnt ; ccnt-- ; ) {
				jme_set_clean_rxdesc(jme, j);
				j = (j + 1) & (mask);
			}

		} else {
			jme_alloc_and_feed_skb(jme, i);
		}

		i = (i + desccnt) & (mask);
	}

out:
	atomic_set(&rxring->next_to_clean, i);

out_inc:
	atomic_inc(&jme->rx_cleaning);

	return limit > 0 ? limit : 0;

}

static void
jme_attempt_pcc(struct dynpcc_info *dpi, int atmp)
{
	if (likely(atmp == dpi->cur)) {
		dpi->cnt = 0;
		return;
	}

	if (dpi->attempt == atmp) {
		++(dpi->cnt);
	} else {
		dpi->attempt = atmp;
		dpi->cnt = 0;
	}

}

static void
jme_dynamic_pcc(struct jme_adapter *jme)
{
	register struct dynpcc_info *dpi = &(jme->dpi);

	if ((NET_STAT(jme).rx_bytes - dpi->last_bytes) > PCC_P3_THRESHOLD)
		jme_attempt_pcc(dpi, PCC_P3);
	else if ((NET_STAT(jme).rx_packets - dpi->last_pkts) > PCC_P2_THRESHOLD ||
		 dpi->intr_cnt > PCC_INTR_THRESHOLD)
		jme_attempt_pcc(dpi, PCC_P2);
	else
		jme_attempt_pcc(dpi, PCC_P1);

	if (unlikely(dpi->attempt != dpi->cur && dpi->cnt > 5)) {
		if (dpi->attempt < dpi->cur)
			tasklet_schedule(&jme->rxclean_task);
		jme_set_rx_pcc(jme, dpi->attempt);
		dpi->cur = dpi->attempt;
		dpi->cnt = 0;
	}
}

static void
jme_start_pcc_timer(struct jme_adapter *jme)
{
	struct dynpcc_info *dpi = &(jme->dpi);
	dpi->last_bytes		= NET_STAT(jme).rx_bytes;
	dpi->last_pkts		= NET_STAT(jme).rx_packets;
	dpi->intr_cnt		= 0;
	jwrite32(jme, JME_TMCSR,
		TMCSR_EN | ((0xFFFFFF - PCC_INTERVAL_US) & TMCSR_CNT));
}

static inline void
jme_stop_pcc_timer(struct jme_adapter *jme)
{
	jwrite32(jme, JME_TMCSR, 0);
}

static void
jme_shutdown_nic(struct jme_adapter *jme)
{
	u32 phylink;

	phylink = jme_linkstat_from_phy(jme);

	if (!(phylink & PHY_LINK_UP)) {
		/*
		 * Disable all interrupt before issue timer
		 */
		jme_stop_irq(jme);
		jwrite32(jme, JME_TIMER2, TMCSR_EN | 0xFFFFFE);
	}
}

static void
jme_pcc_tasklet(unsigned long arg)
{
	struct jme_adapter *jme = (struct jme_adapter *)arg;
	struct net_device *netdev = jme->dev;

	if (unlikely(test_bit(JME_FLAG_SHUTDOWN, &jme->flags))) {
		jme_shutdown_nic(jme);
		return;
	}

	if (unlikely(!netif_carrier_ok(netdev) ||
		(atomic_read(&jme->link_changing) != 1)
	)) {
		jme_stop_pcc_timer(jme);
		return;
	}

	if (!(test_bit(JME_FLAG_POLL, &jme->flags)))
		jme_dynamic_pcc(jme);

	jme_start_pcc_timer(jme);
}

static inline void
jme_polling_mode(struct jme_adapter *jme)
{
	jme_set_rx_pcc(jme, PCC_OFF);
}

static inline void
jme_interrupt_mode(struct jme_adapter *jme)
{
	jme_set_rx_pcc(jme, PCC_P1);
}

static inline int
jme_pseudo_hotplug_enabled(struct jme_adapter *jme)
{
	u32 apmc;
	apmc = jread32(jme, JME_APMC);
	return apmc & JME_APMC_PSEUDO_HP_EN;
}

static void
jme_start_shutdown_timer(struct jme_adapter *jme)
{
	u32 apmc;

	apmc = jread32(jme, JME_APMC) | JME_APMC_PCIE_SD_EN;
	apmc &= ~JME_APMC_EPIEN_CTRL;
	if (!no_extplug) {
		jwrite32f(jme, JME_APMC, apmc | JME_APMC_EPIEN_CTRL_EN);
		wmb();
	}
	jwrite32f(jme, JME_APMC, apmc);

	jwrite32f(jme, JME_TIMER2, 0);
	set_bit(JME_FLAG_SHUTDOWN, &jme->flags);
	jwrite32(jme, JME_TMCSR,
		TMCSR_EN | ((0xFFFFFF - APMC_PHP_SHUTDOWN_DELAY) & TMCSR_CNT));
}

static void
jme_stop_shutdown_timer(struct jme_adapter *jme)
{
	u32 apmc;

	jwrite32f(jme, JME_TMCSR, 0);
	jwrite32f(jme, JME_TIMER2, 0);
	clear_bit(JME_FLAG_SHUTDOWN, &jme->flags);

	apmc = jread32(jme, JME_APMC);
	apmc &= ~(JME_APMC_PCIE_SD_EN | JME_APMC_EPIEN_CTRL);
	jwrite32f(jme, JME_APMC, apmc | JME_APMC_EPIEN_CTRL_DIS);
	wmb();
	jwrite32f(jme, JME_APMC, apmc);
}

static void
jme_link_change_tasklet(unsigned long arg)
{
	struct jme_adapter *jme = (struct jme_adapter *)arg;
	struct net_device *netdev = jme->dev;
	int rc;

	while (!atomic_dec_and_test(&jme->link_changing)) {
		atomic_inc(&jme->link_changing);
		netif_info(jme, intr, jme->dev, "Get link change lock failed\n");
		while (atomic_read(&jme->link_changing) != 1)
			netif_info(jme, intr, jme->dev, "Waiting link change lock\n");
	}

	if (jme_check_link(netdev, 1) && jme->old_mtu == netdev->mtu)
		goto out;

	jme->old_mtu = netdev->mtu;
	netif_stop_queue(netdev);
	if (jme_pseudo_hotplug_enabled(jme))
		jme_stop_shutdown_timer(jme);

	jme_stop_pcc_timer(jme);
	tasklet_disable(&jme->txclean_task);
	tasklet_disable(&jme->rxclean_task);
	tasklet_disable(&jme->rxempty_task);

	if (netif_carrier_ok(netdev)) {
		jme_disable_rx_engine(jme);
		jme_disable_tx_engine(jme);
		jme_reset_mac_processor(jme);
		jme_free_rx_resources(jme);
		jme_free_tx_resources(jme);

		if (test_bit(JME_FLAG_POLL, &jme->flags))
			jme_polling_mode(jme);

		netif_carrier_off(netdev);
	}

	jme_check_link(netdev, 0);
	if (netif_carrier_ok(netdev)) {
		rc = jme_setup_rx_resources(jme);
		if (rc) {
			pr_err("Allocating resources for RX error, Device STOPPED!\n");
			goto out_enable_tasklet;
		}

		rc = jme_setup_tx_resources(jme);
		if (rc) {
			pr_err("Allocating resources for TX error, Device STOPPED!\n");
			goto err_out_free_rx_resources;
		}

		jme_enable_rx_engine(jme);
		jme_enable_tx_engine(jme);

		netif_start_queue(netdev);

		if (test_bit(JME_FLAG_POLL, &jme->flags))
			jme_interrupt_mode(jme);

		jme_start_pcc_timer(jme);
	} else if (jme_pseudo_hotplug_enabled(jme)) {
		jme_start_shutdown_timer(jme);
	}

	goto out_enable_tasklet;

err_out_free_rx_resources:
	jme_free_rx_resources(jme);
out_enable_tasklet:
	tasklet_enable(&jme->txclean_task);
	tasklet_hi_enable(&jme->rxclean_task);
	tasklet_hi_enable(&jme->rxempty_task);
out:
	atomic_inc(&jme->link_changing);
}

static void
jme_rx_clean_tasklet(unsigned long arg)
{
	struct jme_adapter *jme = (struct jme_adapter *)arg;
	struct dynpcc_info *dpi = &(jme->dpi);

	jme_process_receive(jme, jme->rx_ring_size);
	++(dpi->intr_cnt);

}

static int
jme_poll(JME_NAPI_HOLDER(holder), JME_NAPI_WEIGHT(budget))
{
	struct jme_adapter *jme = jme_napi_priv(holder);
	int rest;

	rest = jme_process_receive(jme, JME_NAPI_WEIGHT_VAL(budget));

	while (atomic_read(&jme->rx_empty) > 0) {
		atomic_dec(&jme->rx_empty);
		++(NET_STAT(jme).rx_dropped);
		jme_restart_rx_engine(jme);
	}
	atomic_inc(&jme->rx_empty);

	if (rest) {
		JME_RX_COMPLETE(netdev, holder);
		jme_interrupt_mode(jme);
	}

	JME_NAPI_WEIGHT_SET(budget, rest);
	return JME_NAPI_WEIGHT_VAL(budget) - rest;
}

static void
jme_rx_empty_tasklet(unsigned long arg)
{
	struct jme_adapter *jme = (struct jme_adapter *)arg;

	if (unlikely(atomic_read(&jme->link_changing) != 1))
		return;

	if (unlikely(!netif_carrier_ok(jme->dev)))
		return;

	netif_info(jme, rx_status, jme->dev, "RX Queue Full!\n");

	jme_rx_clean_tasklet(arg);

	while (atomic_read(&jme->rx_empty) > 0) {
		atomic_dec(&jme->rx_empty);
		++(NET_STAT(jme).rx_dropped);
		jme_restart_rx_engine(jme);
	}
	atomic_inc(&jme->rx_empty);
}

static void
jme_wake_queue_if_stopped(struct jme_adapter *jme)
{
	struct jme_ring *txring = &(jme->txring[0]);

	smp_wmb();
	if (unlikely(netif_queue_stopped(jme->dev) &&
	atomic_read(&txring->nr_free) >= (jme->tx_wake_threshold))) {
		netif_info(jme, tx_done, jme->dev, "TX Queue Waked\n");
		netif_wake_queue(jme->dev);
	}

}

static void
jme_tx_clean_tasklet(unsigned long arg)
{
	struct jme_adapter *jme = (struct jme_adapter *)arg;
	struct jme_ring *txring = &(jme->txring[0]);
	struct txdesc *txdesc = txring->desc;
	struct jme_buffer_info *txbi = txring->bufinf, *ctxbi, *ttxbi;
	int i, j, cnt = 0, max, err, mask;

	tx_dbg(jme, "Into txclean\n");

	if (unlikely(!atomic_dec_and_test(&jme->tx_cleaning)))
		goto out;

	if (unlikely(atomic_read(&jme->link_changing) != 1))
		goto out;

	if (unlikely(!netif_carrier_ok(jme->dev)))
		goto out;

	max = jme->tx_ring_size - atomic_read(&txring->nr_free);
	mask = jme->tx_ring_mask;

	for (i = atomic_read(&txring->next_to_clean) ; cnt < max ; ) {

		ctxbi = txbi + i;

		if (likely(ctxbi->skb &&
		!(txdesc[i].descwb.flags & TXWBFLAG_OWN))) {

			tx_dbg(jme, "txclean: %d+%d@%lu\n",
			       i, ctxbi->nr_desc, jiffies);

			err = txdesc[i].descwb.flags & TXWBFLAG_ALLERR;

			for (j = 1 ; j < ctxbi->nr_desc ; ++j) {
				ttxbi = txbi + ((i + j) & (mask));
				txdesc[(i + j) & (mask)].dw[0] = 0;

				pci_unmap_page(jme->pdev,
						 ttxbi->mapping,
						 ttxbi->len,
						 PCI_DMA_TODEVICE);

				ttxbi->mapping = 0;
				ttxbi->len = 0;
			}

			dev_kfree_skb(ctxbi->skb);

			cnt += ctxbi->nr_desc;

			if (unlikely(err)) {
				++(NET_STAT(jme).tx_carrier_errors);
			} else {
				++(NET_STAT(jme).tx_packets);
				NET_STAT(jme).tx_bytes += ctxbi->len;
			}

			ctxbi->skb = NULL;
			ctxbi->len = 0;
			ctxbi->start_xmit = 0;

		} else {
			break;
		}

		i = (i + ctxbi->nr_desc) & mask;

		ctxbi->nr_desc = 0;
	}

	tx_dbg(jme, "txclean: done %d@%lu\n", i, jiffies);
	atomic_set(&txring->next_to_clean, i);
	atomic_add(cnt, &txring->nr_free);

	jme_wake_queue_if_stopped(jme);

out:
	atomic_inc(&jme->tx_cleaning);
}

static void
jme_intr_msi(struct jme_adapter *jme, u32 intrstat)
{
	/*
	 * Disable interrupt
	 */
	jwrite32f(jme, JME_IENC, INTR_ENABLE);

	if (intrstat & (INTR_LINKCH | INTR_SWINTR)) {
		/*
		 * Link change event is critical
		 * all other events are ignored
		 */
		jwrite32(jme, JME_IEVE, intrstat);
		tasklet_schedule(&jme->linkch_task);
		goto out_reenable;
	}

	if (intrstat & INTR_TMINTR) {
		jwrite32(jme, JME_IEVE, INTR_TMINTR);
		tasklet_schedule(&jme->pcc_task);
	}

	if (intrstat & (INTR_PCCTXTO | INTR_PCCTX)) {
		jwrite32(jme, JME_IEVE, INTR_PCCTXTO | INTR_PCCTX | INTR_TX0);
		tasklet_schedule(&jme->txclean_task);
	}

	if ((intrstat & (INTR_PCCRX0TO | INTR_PCCRX0 | INTR_RX0EMP))) {
		jwrite32(jme, JME_IEVE, (intrstat & (INTR_PCCRX0TO |
						     INTR_PCCRX0 |
						     INTR_RX0EMP)) |
					INTR_RX0);
	}

	if (test_bit(JME_FLAG_POLL, &jme->flags)) {
		if (intrstat & INTR_RX0EMP)
			atomic_inc(&jme->rx_empty);

		if ((intrstat & (INTR_PCCRX0TO | INTR_PCCRX0 | INTR_RX0EMP))) {
			if (likely(JME_RX_SCHEDULE_PREP(jme))) {
				jme_polling_mode(jme);
				JME_RX_SCHEDULE(jme);
			}
		}
	} else {
		if (intrstat & INTR_RX0EMP) {
			atomic_inc(&jme->rx_empty);
			tasklet_hi_schedule(&jme->rxempty_task);
		} else if (intrstat & (INTR_PCCRX0TO | INTR_PCCRX0)) {
			tasklet_hi_schedule(&jme->rxclean_task);
		}
	}

out_reenable:
	/*
	 * Re-enable interrupt
	 */
	jwrite32f(jme, JME_IENS, INTR_ENABLE);
}

static irqreturn_t
jme_intr(int irq, void *dev_id)
{
	struct net_device *netdev = dev_id;
	struct jme_adapter *jme = netdev_priv(netdev);
	u32 intrstat;

	intrstat = jread32(jme, JME_IEVE);

	/*
	 * Check if it's really an interrupt for us
	 */
	if (unlikely((intrstat & INTR_ENABLE) == 0))
		return IRQ_NONE;

	/*
	 * Check if the device still exist
	 */
	if (unlikely(intrstat == ~((typeof(intrstat))0)))
		return IRQ_NONE;

	jme_intr_msi(jme, intrstat);

	return IRQ_HANDLED;
}

static irqreturn_t
jme_msi(int irq, void *dev_id)
{
	struct net_device *netdev = dev_id;
	struct jme_adapter *jme = netdev_priv(netdev);
	u32 intrstat;

	intrstat = jread32(jme, JME_IEVE);

	jme_intr_msi(jme, intrstat);

	return IRQ_HANDLED;
}

static void
jme_reset_link(struct jme_adapter *jme)
{
	jwrite32(jme, JME_TMCSR, TMCSR_SWIT);
}

static void
jme_restart_an(struct jme_adapter *jme)
{
	u32 bmcr;

	spin_lock_bh(&jme->phy_lock);
	bmcr = jme_mdio_read(jme->dev, jme->mii_if.phy_id, MII_BMCR);
	bmcr |= (BMCR_ANENABLE | BMCR_ANRESTART);
	jme_mdio_write(jme->dev, jme->mii_if.phy_id, MII_BMCR, bmcr);
	spin_unlock_bh(&jme->phy_lock);
}

static int
jme_request_irq(struct jme_adapter *jme)
{
	int rc;
	struct net_device *netdev = jme->dev;
	irq_handler_t handler = jme_intr;
	int irq_flags = IRQF_SHARED;

	if (!pci_enable_msi(jme->pdev)) {
		set_bit(JME_FLAG_MSI, &jme->flags);
		handler = jme_msi;
		irq_flags = 0;
	}

	rc = request_irq(jme->pdev->irq, handler, irq_flags, netdev->name,
			  netdev);
	if (rc) {
		netdev_err(netdev,
			   "Unable to request %s interrupt (return: %d)\n",
			   test_bit(JME_FLAG_MSI, &jme->flags) ? "MSI" : "INTx",
			   rc);

		if (test_bit(JME_FLAG_MSI, &jme->flags)) {
			pci_disable_msi(jme->pdev);
			clear_bit(JME_FLAG_MSI, &jme->flags);
		}
	} else {
		netdev->irq = jme->pdev->irq;
	}

	return rc;
}

static void
jme_free_irq(struct jme_adapter *jme)
{
	free_irq(jme->pdev->irq, jme->dev);
	if (test_bit(JME_FLAG_MSI, &jme->flags)) {
		pci_disable_msi(jme->pdev);
		clear_bit(JME_FLAG_MSI, &jme->flags);
		jme->dev->irq = jme->pdev->irq;
	}
}

static inline void
jme_new_phy_on(struct jme_adapter *jme)
{
	u32 reg;

	reg = jread32(jme, JME_PHY_PWR);
	reg &= ~(PHY_PWR_DWN1SEL | PHY_PWR_DWN1SW |
		 PHY_PWR_DWN2 | PHY_PWR_CLKSEL);
	jwrite32(jme, JME_PHY_PWR, reg);

	pci_read_config_dword(jme->pdev, PCI_PRIV_PE1, &reg);
	reg &= ~PE1_GPREG0_PBG;
	reg |= PE1_GPREG0_ENBG;
	pci_write_config_dword(jme->pdev, PCI_PRIV_PE1, reg);
}

static inline void
jme_new_phy_off(struct jme_adapter *jme)
{
	u32 reg;

	reg = jread32(jme, JME_PHY_PWR);
	reg |= PHY_PWR_DWN1SEL | PHY_PWR_DWN1SW |
	       PHY_PWR_DWN2 | PHY_PWR_CLKSEL;
	jwrite32(jme, JME_PHY_PWR, reg);

	pci_read_config_dword(jme->pdev, PCI_PRIV_PE1, &reg);
	reg &= ~PE1_GPREG0_PBG;
	reg |= PE1_GPREG0_PDD3COLD;
	pci_write_config_dword(jme->pdev, PCI_PRIV_PE1, reg);
}

static inline void
jme_phy_on(struct jme_adapter *jme)
{
	u32 bmcr;

	bmcr = jme_mdio_read(jme->dev, jme->mii_if.phy_id, MII_BMCR);
	bmcr &= ~BMCR_PDOWN;
	jme_mdio_write(jme->dev, jme->mii_if.phy_id, MII_BMCR, bmcr);

	if (new_phy_power_ctrl(jme->chip_main_rev))
		jme_new_phy_on(jme);
}

static inline void
jme_phy_off(struct jme_adapter *jme)
{
	u32 bmcr;

	bmcr = jme_mdio_read(jme->dev, jme->mii_if.phy_id, MII_BMCR);
	bmcr |= BMCR_PDOWN;
	jme_mdio_write(jme->dev, jme->mii_if.phy_id, MII_BMCR, bmcr);

	if (new_phy_power_ctrl(jme->chip_main_rev))
		jme_new_phy_off(jme);
}

static int
jme_phy_specreg_read(struct jme_adapter *jme, u32 specreg)
{
	u32 phy_addr;

	phy_addr = JM_PHY_SPEC_REG_READ | specreg;
	jme_mdio_write(jme->dev, jme->mii_if.phy_id, JM_PHY_SPEC_ADDR_REG,
			phy_addr);
	return jme_mdio_read(jme->dev, jme->mii_if.phy_id,
			JM_PHY_SPEC_DATA_REG);
}

static void
jme_phy_specreg_write(struct jme_adapter *jme, u32 ext_reg, u32 phy_data)
{
	u32 phy_addr;

	phy_addr = JM_PHY_SPEC_REG_WRITE | ext_reg;
	jme_mdio_write(jme->dev, jme->mii_if.phy_id, JM_PHY_SPEC_DATA_REG,
			phy_data);
	jme_mdio_write(jme->dev, jme->mii_if.phy_id, JM_PHY_SPEC_ADDR_REG,
			phy_addr);
}

static int
jme_phy_calibration(struct jme_adapter *jme)
{
	u32 ctrl1000, phy_data;

	jme_phy_off(jme);
	jme_phy_on(jme);
	/*  Enabel PHY test mode 1 */
	ctrl1000 = jme_mdio_read(jme->dev, jme->mii_if.phy_id, MII_CTRL1000);
	ctrl1000 &= ~PHY_GAD_TEST_MODE_MSK;
	ctrl1000 |= PHY_GAD_TEST_MODE_1;
	jme_mdio_write(jme->dev, jme->mii_if.phy_id, MII_CTRL1000, ctrl1000);

	phy_data = jme_phy_specreg_read(jme, JM_PHY_EXT_COMM_2_REG);
	phy_data &= ~JM_PHY_EXT_COMM_2_CALI_MODE_0;
	phy_data |= JM_PHY_EXT_COMM_2_CALI_LATCH |
			JM_PHY_EXT_COMM_2_CALI_ENABLE;
	jme_phy_specreg_write(jme, JM_PHY_EXT_COMM_2_REG, phy_data);
	msleep(20);
	phy_data = jme_phy_specreg_read(jme, JM_PHY_EXT_COMM_2_REG);
	phy_data &= ~(JM_PHY_EXT_COMM_2_CALI_ENABLE |
			JM_PHY_EXT_COMM_2_CALI_MODE_0 |
			JM_PHY_EXT_COMM_2_CALI_LATCH);
	jme_phy_specreg_write(jme, JM_PHY_EXT_COMM_2_REG, phy_data);

	/*  Disable PHY test mode */
	ctrl1000 = jme_mdio_read(jme->dev, jme->mii_if.phy_id, MII_CTRL1000);
	ctrl1000 &= ~PHY_GAD_TEST_MODE_MSK;
	jme_mdio_write(jme->dev, jme->mii_if.phy_id, MII_CTRL1000, ctrl1000);
	return 0;
}

static int
jme_phy_setEA(struct jme_adapter *jme)
{
	u32 phy_comm0 = 0, phy_comm1 = 0;
	u8 nic_ctrl;

	pci_read_config_byte(jme->pdev, PCI_PRIV_SHARE_NICCTRL, &nic_ctrl);
	if ((nic_ctrl & 0x3) == JME_FLAG_PHYEA_ENABLE)
		return 0;

	switch (jme->pdev->device) {
	case PCI_DEVICE_ID_JMICRON_JMC250:
		if (((jme->chip_main_rev == 5) &&
			((jme->chip_sub_rev == 0) || (jme->chip_sub_rev == 1) ||
			(jme->chip_sub_rev == 3))) ||
			(jme->chip_main_rev >= 6)) {
			phy_comm0 = 0x008A;
			phy_comm1 = 0x4109;
		}
		if ((jme->chip_main_rev == 3) &&
			((jme->chip_sub_rev == 1) || (jme->chip_sub_rev == 2)))
			phy_comm0 = 0xE088;
		break;
	case PCI_DEVICE_ID_JMICRON_JMC260:
		if (((jme->chip_main_rev == 5) &&
			((jme->chip_sub_rev == 0) || (jme->chip_sub_rev == 1) ||
			(jme->chip_sub_rev == 3))) ||
			(jme->chip_main_rev >= 6)) {
			phy_comm0 = 0x008A;
			phy_comm1 = 0x4109;
		}
		if ((jme->chip_main_rev == 3) &&
			((jme->chip_sub_rev == 1) || (jme->chip_sub_rev == 2)))
			phy_comm0 = 0xE088;
		if ((jme->chip_main_rev == 2) && (jme->chip_sub_rev == 0))
			phy_comm0 = 0x608A;
		if ((jme->chip_main_rev == 2) && (jme->chip_sub_rev == 2))
			phy_comm0 = 0x408A;
		break;
	default:
		return -ENODEV;
	}
	if (phy_comm0)
		jme_phy_specreg_write(jme, JM_PHY_EXT_COMM_0_REG, phy_comm0);
	if (phy_comm1)
		jme_phy_specreg_write(jme, JM_PHY_EXT_COMM_1_REG, phy_comm1);

	return 0;
}

static int
jme_open(struct net_device *netdev)
{
	struct jme_adapter *jme = netdev_priv(netdev);
	int rc;

	jme_clear_pm(jme);
	JME_NAPI_ENABLE(jme);

	tasklet_enable(&jme->linkch_task);
	tasklet_enable(&jme->txclean_task);
	tasklet_hi_enable(&jme->rxclean_task);
	tasklet_hi_enable(&jme->rxempty_task);

	rc = jme_request_irq(jme);
	if (rc)
		goto err_out;

	jme_start_irq(jme);

	jme_phy_on(jme);
	if (test_bit(JME_FLAG_SSET, &jme->flags))
		jme_set_settings(netdev, &jme->old_ecmd);
	else
		jme_reset_phy_processor(jme);
	jme_phy_calibration(jme);
	jme_phy_setEA(jme);
	jme_reset_link(jme);

	return 0;

err_out:
	netif_stop_queue(netdev);
	netif_carrier_off(netdev);
	return rc;
}

static void
jme_set_100m_half(struct jme_adapter *jme)
{
	u32 bmcr, tmp;

	jme_phy_on(jme);
	bmcr = jme_mdio_read(jme->dev, jme->mii_if.phy_id, MII_BMCR);
	tmp = bmcr & ~(BMCR_ANENABLE | BMCR_SPEED100 |
		       BMCR_SPEED1000 | BMCR_FULLDPLX);
	tmp |= BMCR_SPEED100;

	if (bmcr != tmp)
		jme_mdio_write(jme->dev, jme->mii_if.phy_id, MII_BMCR, tmp);

	if (jme->fpgaver)
		jwrite32(jme, JME_GHC, GHC_SPEED_100M | GHC_LINK_POLL);
	else
		jwrite32(jme, JME_GHC, GHC_SPEED_100M);
}

#define JME_WAIT_LINK_TIME 2000 /* 2000ms */
static void
jme_wait_link(struct jme_adapter *jme)
{
	u32 phylink, to = JME_WAIT_LINK_TIME;

	mdelay(1000);
	phylink = jme_linkstat_from_phy(jme);
	while (!(phylink & PHY_LINK_UP) && (to -= 10) > 0) {
		mdelay(10);
		phylink = jme_linkstat_from_phy(jme);
	}
}

static void
jme_powersave_phy(struct jme_adapter *jme)
{
	if (jme->reg_pmcs) {
		jme_set_100m_half(jme);
		if (jme->reg_pmcs & (PMCS_LFEN | PMCS_LREN))
			jme_wait_link(jme);
		jme_clear_pm(jme);
	} else {
		jme_phy_off(jme);
	}
}

static int
jme_close(struct net_device *netdev)
{
	struct jme_adapter *jme = netdev_priv(netdev);

	netif_stop_queue(netdev);
	netif_carrier_off(netdev);

	jme_stop_irq(jme);
	jme_free_irq(jme);

	JME_NAPI_DISABLE(jme);

	tasklet_disable(&jme->linkch_task);
	tasklet_disable(&jme->txclean_task);
	tasklet_disable(&jme->rxclean_task);
	tasklet_disable(&jme->rxempty_task);

	jme_disable_rx_engine(jme);
	jme_disable_tx_engine(jme);
	jme_reset_mac_processor(jme);
	jme_free_rx_resources(jme);
	jme_free_tx_resources(jme);
	jme->phylink = 0;
	jme_phy_off(jme);

	return 0;
}

static int
jme_alloc_txdesc(struct jme_adapter *jme,
			struct sk_buff *skb)
{
	struct jme_ring *txring = &(jme->txring[0]);
	int idx, nr_alloc, mask = jme->tx_ring_mask;

	idx = txring->next_to_use;
	nr_alloc = skb_shinfo(skb)->nr_frags + 2;

	if (unlikely(atomic_read(&txring->nr_free) < nr_alloc))
		return -1;

	atomic_sub(nr_alloc, &txring->nr_free);

	txring->next_to_use = (txring->next_to_use + nr_alloc) & mask;

	return idx;
}

static void
jme_fill_tx_map(struct pci_dev *pdev,
		struct txdesc *txdesc,
		struct jme_buffer_info *txbi,
		struct page *page,
		u32 page_offset,
		u32 len,
		bool hidma)
{
	dma_addr_t dmaaddr;

	dmaaddr = pci_map_page(pdev,
				page,
				page_offset,
				len,
				PCI_DMA_TODEVICE);

	pci_dma_sync_single_for_device(pdev,
				       dmaaddr,
				       len,
				       PCI_DMA_TODEVICE);

	txdesc->dw[0] = 0;
	txdesc->dw[1] = 0;
	txdesc->desc2.flags	= TXFLAG_OWN;
	txdesc->desc2.flags	|= (hidma) ? TXFLAG_64BIT : 0;
	txdesc->desc2.datalen	= cpu_to_le16(len);
	txdesc->desc2.bufaddrh	= cpu_to_le32((__u64)dmaaddr >> 32);
	txdesc->desc2.bufaddrl	= cpu_to_le32(
					(__u64)dmaaddr & 0xFFFFFFFFUL);

	txbi->mapping = dmaaddr;
	txbi->len = len;
}

static void
jme_map_tx_skb(struct jme_adapter *jme, struct sk_buff *skb, int idx)
{
	struct jme_ring *txring = &(jme->txring[0]);
	struct txdesc *txdesc = txring->desc, *ctxdesc;
	struct jme_buffer_info *txbi = txring->bufinf, *ctxbi;
	bool hidma = jme->dev->features & NETIF_F_HIGHDMA;
	int i, nr_frags = skb_shinfo(skb)->nr_frags;
	int mask = jme->tx_ring_mask;
	const struct skb_frag_struct *frag;
	u32 len;

	for (i = 0 ; i < nr_frags ; ++i) {
		frag = &skb_shinfo(skb)->frags[i];
		ctxdesc = txdesc + ((idx + i + 2) & (mask));
		ctxbi = txbi + ((idx + i + 2) & (mask));

		jme_fill_tx_map(jme->pdev, ctxdesc, ctxbi,
				skb_frag_page(frag),
				frag->page_offset, skb_frag_size(frag), hidma);
	}

	len = skb_is_nonlinear(skb) ? skb_headlen(skb) : skb->len;
	ctxdesc = txdesc + ((idx + 1) & (mask));
	ctxbi = txbi + ((idx + 1) & (mask));
	jme_fill_tx_map(jme->pdev, ctxdesc, ctxbi, virt_to_page(skb->data),
			offset_in_page(skb->data), len, hidma);

}

static int
jme_expand_header(struct jme_adapter *jme, struct sk_buff *skb)
{
	if (unlikely(skb_shinfo(skb)->gso_size &&
			skb_header_cloned(skb) &&
			pskb_expand_head(skb, 0, 0, GFP_ATOMIC))) {
		dev_kfree_skb(skb);
		return -1;
	}

	return 0;
}

static int
jme_tx_tso(struct sk_buff *skb, __le16 *mss, u8 *flags)
{
	*mss = cpu_to_le16(skb_shinfo(skb)->gso_size << TXDESC_MSS_SHIFT);
	if (*mss) {
		*flags |= TXFLAG_LSEN;

		if (skb->protocol == htons(ETH_P_IP)) {
			struct iphdr *iph = ip_hdr(skb);

			iph->check = 0;
			tcp_hdr(skb)->check = ~csum_tcpudp_magic(iph->saddr,
								iph->daddr, 0,
								IPPROTO_TCP,
								0);
		} else {
			struct ipv6hdr *ip6h = ipv6_hdr(skb);

			tcp_hdr(skb)->check = ~csum_ipv6_magic(&ip6h->saddr,
								&ip6h->daddr, 0,
								IPPROTO_TCP,
								0);
		}

		return 0;
	}

	return 1;
}

static void
jme_tx_csum(struct jme_adapter *jme, struct sk_buff *skb, u8 *flags)
{
	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		u8 ip_proto;

		switch (skb->protocol) {
		case htons(ETH_P_IP):
			ip_proto = ip_hdr(skb)->protocol;
			break;
		case htons(ETH_P_IPV6):
			ip_proto = ipv6_hdr(skb)->nexthdr;
			break;
		default:
			ip_proto = 0;
			break;
		}

		switch (ip_proto) {
		case IPPROTO_TCP:
			*flags |= TXFLAG_TCPCS;
			break;
		case IPPROTO_UDP:
			*flags |= TXFLAG_UDPCS;
			break;
		default:
			netif_err(jme, tx_err, jme->dev, "Error upper layer protocol\n");
			break;
		}
	}
}

static inline void
jme_tx_vlan(struct sk_buff *skb, __le16 *vlan, u8 *flags)
{
	if (vlan_tx_tag_present(skb)) {
		*flags |= TXFLAG_TAGON;
		*vlan = cpu_to_le16(vlan_tx_tag_get(skb));
	}
}

static int
jme_fill_tx_desc(struct jme_adapter *jme, struct sk_buff *skb, int idx)
{
	struct jme_ring *txring = &(jme->txring[0]);
	struct txdesc *txdesc;
	struct jme_buffer_info *txbi;
	u8 flags;

	txdesc = (struct txdesc *)txring->desc + idx;
	txbi = txring->bufinf + idx;

	txdesc->dw[0] = 0;
	txdesc->dw[1] = 0;
	txdesc->dw[2] = 0;
	txdesc->dw[3] = 0;
	txdesc->desc1.pktsize = cpu_to_le16(skb->len);
	/*
	 * Set OWN bit at final.
	 * When kernel transmit faster than NIC.
	 * And NIC trying to send this descriptor before we tell
	 * it to start sending this TX queue.
	 * Other fields are already filled correctly.
	 */
	wmb();
	flags = TXFLAG_OWN | TXFLAG_INT;
	/*
	 * Set checksum flags while not tso
	 */
	if (jme_tx_tso(skb, &txdesc->desc1.mss, &flags))
		jme_tx_csum(jme, skb, &flags);
	jme_tx_vlan(skb, &txdesc->desc1.vlan, &flags);
	jme_map_tx_skb(jme, skb, idx);
	txdesc->desc1.flags = flags;
	/*
	 * Set tx buffer info after telling NIC to send
	 * For better tx_clean timing
	 */
	wmb();
	txbi->nr_desc = skb_shinfo(skb)->nr_frags + 2;
	txbi->skb = skb;
	txbi->len = skb->len;
	txbi->start_xmit = jiffies;
	if (!txbi->start_xmit)
		txbi->start_xmit = (0UL-1);

	return 0;
}

static void
jme_stop_queue_if_full(struct jme_adapter *jme)
{
	struct jme_ring *txring = &(jme->txring[0]);
	struct jme_buffer_info *txbi = txring->bufinf;
	int idx = atomic_read(&txring->next_to_clean);

	txbi += idx;

	smp_wmb();
	if (unlikely(atomic_read(&txring->nr_free) < (MAX_SKB_FRAGS+2))) {
		netif_stop_queue(jme->dev);
		netif_info(jme, tx_queued, jme->dev, "TX Queue Paused\n");
		smp_wmb();
		if (atomic_read(&txring->nr_free)
			>= (jme->tx_wake_threshold)) {
			netif_wake_queue(jme->dev);
			netif_info(jme, tx_queued, jme->dev, "TX Queue Fast Waked\n");
		}
	}

	if (unlikely(txbi->start_xmit &&
			(jiffies - txbi->start_xmit) >= TX_TIMEOUT &&
			txbi->skb)) {
		netif_stop_queue(jme->dev);
		netif_info(jme, tx_queued, jme->dev,
			   "TX Queue Stopped %d@%lu\n", idx, jiffies);
	}
}

/*
 * This function is already protected by netif_tx_lock()
 */

static netdev_tx_t
jme_start_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	struct jme_adapter *jme = netdev_priv(netdev);
	int idx;

	if (unlikely(jme_expand_header(jme, skb))) {
		++(NET_STAT(jme).tx_dropped);
		return NETDEV_TX_OK;
	}

	idx = jme_alloc_txdesc(jme, skb);

	if (unlikely(idx < 0)) {
		netif_stop_queue(netdev);
		netif_err(jme, tx_err, jme->dev,
			  "BUG! Tx ring full when queue awake!\n");

		return NETDEV_TX_BUSY;
	}

	jme_fill_tx_desc(jme, skb, idx);

	jwrite32(jme, JME_TXCS, jme->reg_txcs |
				TXCS_SELECT_QUEUE0 |
				TXCS_QUEUE0S |
				TXCS_ENABLE);

	tx_dbg(jme, "xmit: %d+%d@%lu\n",
	       idx, skb_shinfo(skb)->nr_frags + 2, jiffies);
	jme_stop_queue_if_full(jme);

	return NETDEV_TX_OK;
}

static void
jme_set_unicastaddr(struct net_device *netdev)
{
	struct jme_adapter *jme = netdev_priv(netdev);
	u32 val;

	val = (netdev->dev_addr[3] & 0xff) << 24 |
	      (netdev->dev_addr[2] & 0xff) << 16 |
	      (netdev->dev_addr[1] & 0xff) <<  8 |
	      (netdev->dev_addr[0] & 0xff);
	jwrite32(jme, JME_RXUMA_LO, val);
	val = (netdev->dev_addr[5] & 0xff) << 8 |
	      (netdev->dev_addr[4] & 0xff);
	jwrite32(jme, JME_RXUMA_HI, val);
}

static int
jme_set_macaddr(struct net_device *netdev, void *p)
{
	struct jme_adapter *jme = netdev_priv(netdev);
	struct sockaddr *addr = p;

	if (netif_running(netdev))
		return -EBUSY;

	spin_lock_bh(&jme->macaddr_lock);
	memcpy(netdev->dev_addr, addr->sa_data, netdev->addr_len);
	jme_set_unicastaddr(netdev);
	spin_unlock_bh(&jme->macaddr_lock);

	return 0;
}

static void
jme_set_multi(struct net_device *netdev)
{
	struct jme_adapter *jme = netdev_priv(netdev);
	u32 mc_hash[2] = {};

	spin_lock_bh(&jme->rxmcs_lock);

	jme->reg_rxmcs |= RXMCS_BRDFRAME | RXMCS_UNIFRAME;

	if (netdev->flags & IFF_PROMISC) {
		jme->reg_rxmcs |= RXMCS_ALLFRAME;
	} else if (netdev->flags & IFF_ALLMULTI) {
		jme->reg_rxmcs |= RXMCS_ALLMULFRAME;
	} else if (netdev->flags & IFF_MULTICAST) {
		struct netdev_hw_addr *ha;
		int bit_nr;

		jme->reg_rxmcs |= RXMCS_MULFRAME | RXMCS_MULFILTERED;
		netdev_for_each_mc_addr(ha, netdev) {
			bit_nr = ether_crc(ETH_ALEN, ha->addr) & 0x3F;
			mc_hash[bit_nr >> 5] |= 1 << (bit_nr & 0x1F);
		}

		jwrite32(jme, JME_RXMCHT_LO, mc_hash[0]);
		jwrite32(jme, JME_RXMCHT_HI, mc_hash[1]);
	}

	wmb();
	jwrite32(jme, JME_RXMCS, jme->reg_rxmcs);

	spin_unlock_bh(&jme->rxmcs_lock);
}

static int
jme_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct jme_adapter *jme = netdev_priv(netdev);

	if (new_mtu == jme->old_mtu)
		return 0;

	if (((new_mtu + ETH_HLEN) > MAX_ETHERNET_JUMBO_PACKET_SIZE) ||
		((new_mtu) < IPV6_MIN_MTU))
		return -EINVAL;


	netdev->mtu = new_mtu;
	netdev_update_features(netdev);

	jme_restart_rx_engine(jme);
	jme_reset_link(jme);

	return 0;
}

static void
jme_tx_timeout(struct net_device *netdev)
{
	struct jme_adapter *jme = netdev_priv(netdev);

	jme->phylink = 0;
	jme_reset_phy_processor(jme);
	if (test_bit(JME_FLAG_SSET, &jme->flags))
		jme_set_settings(netdev, &jme->old_ecmd);

	/*
	 * Force to Reset the link again
	 */
	jme_reset_link(jme);
}

static inline void jme_pause_rx(struct jme_adapter *jme)
{
	atomic_dec(&jme->link_changing);

	jme_set_rx_pcc(jme, PCC_OFF);
	if (test_bit(JME_FLAG_POLL, &jme->flags)) {
		JME_NAPI_DISABLE(jme);
	} else {
		tasklet_disable(&jme->rxclean_task);
		tasklet_disable(&jme->rxempty_task);
	}
}

static inline void jme_resume_rx(struct jme_adapter *jme)
{
	struct dynpcc_info *dpi = &(jme->dpi);

	if (test_bit(JME_FLAG_POLL, &jme->flags)) {
		JME_NAPI_ENABLE(jme);
	} else {
		tasklet_hi_enable(&jme->rxclean_task);
		tasklet_hi_enable(&jme->rxempty_task);
	}
	dpi->cur		= PCC_P1;
	dpi->attempt		= PCC_P1;
	dpi->cnt		= 0;
	jme_set_rx_pcc(jme, PCC_P1);

	atomic_inc(&jme->link_changing);
}

static void
jme_get_drvinfo(struct net_device *netdev,
		     struct ethtool_drvinfo *info)
{
	struct jme_adapter *jme = netdev_priv(netdev);

	strlcpy(info->driver, DRV_NAME, sizeof(info->driver));
	strlcpy(info->version, DRV_VERSION, sizeof(info->version));
	strlcpy(info->bus_info, pci_name(jme->pdev), sizeof(info->bus_info));
}

static int
jme_get_regs_len(struct net_device *netdev)
{
	return JME_REG_LEN;
}

static void
mmapio_memcpy(struct jme_adapter *jme, u32 *p, u32 reg, int len)
{
	int i;

	for (i = 0 ; i < len ; i += 4)
		p[i >> 2] = jread32(jme, reg + i);
}

static void
mdio_memcpy(struct jme_adapter *jme, u32 *p, int reg_nr)
{
	int i;
	u16 *p16 = (u16 *)p;

	for (i = 0 ; i < reg_nr ; ++i)
		p16[i] = jme_mdio_read(jme->dev, jme->mii_if.phy_id, i);
}

static void
jme_get_regs(struct net_device *netdev, struct ethtool_regs *regs, void *p)
{
	struct jme_adapter *jme = netdev_priv(netdev);
	u32 *p32 = (u32 *)p;

	memset(p, 0xFF, JME_REG_LEN);

	regs->version = 1;
	mmapio_memcpy(jme, p32, JME_MAC, JME_MAC_LEN);

	p32 += 0x100 >> 2;
	mmapio_memcpy(jme, p32, JME_PHY, JME_PHY_LEN);

	p32 += 0x100 >> 2;
	mmapio_memcpy(jme, p32, JME_MISC, JME_MISC_LEN);

	p32 += 0x100 >> 2;
	mmapio_memcpy(jme, p32, JME_RSS, JME_RSS_LEN);

	p32 += 0x100 >> 2;
	mdio_memcpy(jme, p32, JME_PHY_REG_NR);
}

static int
jme_get_coalesce(struct net_device *netdev, struct ethtool_coalesce *ecmd)
{
	struct jme_adapter *jme = netdev_priv(netdev);

	ecmd->tx_coalesce_usecs = PCC_TX_TO;
	ecmd->tx_max_coalesced_frames = PCC_TX_CNT;

	if (test_bit(JME_FLAG_POLL, &jme->flags)) {
		ecmd->use_adaptive_rx_coalesce = false;
		ecmd->rx_coalesce_usecs = 0;
		ecmd->rx_max_coalesced_frames = 0;
		return 0;
	}

	ecmd->use_adaptive_rx_coalesce = true;

	switch (jme->dpi.cur) {
	case PCC_P1:
		ecmd->rx_coalesce_usecs = PCC_P1_TO;
		ecmd->rx_max_coalesced_frames = PCC_P1_CNT;
		break;
	case PCC_P2:
		ecmd->rx_coalesce_usecs = PCC_P2_TO;
		ecmd->rx_max_coalesced_frames = PCC_P2_CNT;
		break;
	case PCC_P3:
		ecmd->rx_coalesce_usecs = PCC_P3_TO;
		ecmd->rx_max_coalesced_frames = PCC_P3_CNT;
		break;
	default:
		break;
	}

	return 0;
}

static int
jme_set_coalesce(struct net_device *netdev, struct ethtool_coalesce *ecmd)
{
	struct jme_adapter *jme = netdev_priv(netdev);
	struct dynpcc_info *dpi = &(jme->dpi);

	if (netif_running(netdev))
		return -EBUSY;

	if (ecmd->use_adaptive_rx_coalesce &&
	    test_bit(JME_FLAG_POLL, &jme->flags)) {
		clear_bit(JME_FLAG_POLL, &jme->flags);
		jme->jme_rx = netif_rx;
		dpi->cur		= PCC_P1;
		dpi->attempt		= PCC_P1;
		dpi->cnt		= 0;
		jme_set_rx_pcc(jme, PCC_P1);
		jme_interrupt_mode(jme);
	} else if (!(ecmd->use_adaptive_rx_coalesce) &&
		   !(test_bit(JME_FLAG_POLL, &jme->flags))) {
		set_bit(JME_FLAG_POLL, &jme->flags);
		jme->jme_rx = netif_receive_skb;
		jme_interrupt_mode(jme);
	}

	return 0;
}

static void
jme_get_pauseparam(struct net_device *netdev,
			struct ethtool_pauseparam *ecmd)
{
	struct jme_adapter *jme = netdev_priv(netdev);
	u32 val;

	ecmd->tx_pause = (jme->reg_txpfc & TXPFC_PF_EN) != 0;
	ecmd->rx_pause = (jme->reg_rxmcs & RXMCS_FLOWCTRL) != 0;

	spin_lock_bh(&jme->phy_lock);
	val = jme_mdio_read(jme->dev, jme->mii_if.phy_id, MII_ADVERTISE);
	spin_unlock_bh(&jme->phy_lock);

	ecmd->autoneg =
		(val & (ADVERTISE_PAUSE_CAP | ADVERTISE_PAUSE_ASYM)) != 0;
}

static int
jme_set_pauseparam(struct net_device *netdev,
			struct ethtool_pauseparam *ecmd)
{
	struct jme_adapter *jme = netdev_priv(netdev);
	u32 val;

	if (((jme->reg_txpfc & TXPFC_PF_EN) != 0) ^
		(ecmd->tx_pause != 0)) {

		if (ecmd->tx_pause)
			jme->reg_txpfc |= TXPFC_PF_EN;
		else
			jme->reg_txpfc &= ~TXPFC_PF_EN;

		jwrite32(jme, JME_TXPFC, jme->reg_txpfc);
	}

	spin_lock_bh(&jme->rxmcs_lock);
	if (((jme->reg_rxmcs & RXMCS_FLOWCTRL) != 0) ^
		(ecmd->rx_pause != 0)) {

		if (ecmd->rx_pause)
			jme->reg_rxmcs |= RXMCS_FLOWCTRL;
		else
			jme->reg_rxmcs &= ~RXMCS_FLOWCTRL;

		jwrite32(jme, JME_RXMCS, jme->reg_rxmcs);
	}
	spin_unlock_bh(&jme->rxmcs_lock);

	spin_lock_bh(&jme->phy_lock);
	val = jme_mdio_read(jme->dev, jme->mii_if.phy_id, MII_ADVERTISE);
	if (((val & (ADVERTISE_PAUSE_CAP | ADVERTISE_PAUSE_ASYM)) != 0) ^
		(ecmd->autoneg != 0)) {

		if (ecmd->autoneg)
			val |= (ADVERTISE_PAUSE_CAP | ADVERTISE_PAUSE_ASYM);
		else
			val &= ~(ADVERTISE_PAUSE_CAP | ADVERTISE_PAUSE_ASYM);

		jme_mdio_write(jme->dev, jme->mii_if.phy_id,
				MII_ADVERTISE, val);
	}
	spin_unlock_bh(&jme->phy_lock);

	return 0;
}

static void
jme_get_wol(struct net_device *netdev,
		struct ethtool_wolinfo *wol)
{
	struct jme_adapter *jme = netdev_priv(netdev);

	wol->supported = WAKE_MAGIC | WAKE_PHY;

	wol->wolopts = 0;

	if (jme->reg_pmcs & (PMCS_LFEN | PMCS_LREN))
		wol->wolopts |= WAKE_PHY;

	if (jme->reg_pmcs & PMCS_MFEN)
		wol->wolopts |= WAKE_MAGIC;

}

static int
jme_set_wol(struct net_device *netdev,
		struct ethtool_wolinfo *wol)
{
	struct jme_adapter *jme = netdev_priv(netdev);

	if (wol->wolopts & (WAKE_MAGICSECURE |
				WAKE_UCAST |
				WAKE_MCAST |
				WAKE_BCAST |
				WAKE_ARP))
		return -EOPNOTSUPP;

	jme->reg_pmcs = 0;

	if (wol->wolopts & WAKE_PHY)
		jme->reg_pmcs |= PMCS_LFEN | PMCS_LREN;

	if (wol->wolopts & WAKE_MAGIC)
		jme->reg_pmcs |= PMCS_MFEN;

	jwrite32(jme, JME_PMCS, jme->reg_pmcs);
	device_set_wakeup_enable(&jme->pdev->dev, !!(jme->reg_pmcs));

	return 0;
}

static int
jme_get_settings(struct net_device *netdev,
		     struct ethtool_cmd *ecmd)
{
	struct jme_adapter *jme = netdev_priv(netdev);
	int rc;

	spin_lock_bh(&jme->phy_lock);
	rc = mii_ethtool_gset(&(jme->mii_if), ecmd);
	spin_unlock_bh(&jme->phy_lock);
	return rc;
}

static int
jme_set_settings(struct net_device *netdev,
		     struct ethtool_cmd *ecmd)
{
	struct jme_adapter *jme = netdev_priv(netdev);
	int rc, fdc = 0;

	if (ethtool_cmd_speed(ecmd) == SPEED_1000
	    && ecmd->autoneg != AUTONEG_ENABLE)
		return -EINVAL;

	/*
	 * Check If user changed duplex only while force_media.
	 * Hardware would not generate link change interrupt.
	 */
	if (jme->mii_if.force_media &&
	ecmd->autoneg != AUTONEG_ENABLE &&
	(jme->mii_if.full_duplex != ecmd->duplex))
		fdc = 1;

	spin_lock_bh(&jme->phy_lock);
	rc = mii_ethtool_sset(&(jme->mii_if), ecmd);
	spin_unlock_bh(&jme->phy_lock);

	if (!rc) {
		if (fdc)
			jme_reset_link(jme);
		jme->old_ecmd = *ecmd;
		set_bit(JME_FLAG_SSET, &jme->flags);
	}

	return rc;
}

static int
jme_ioctl(struct net_device *netdev, struct ifreq *rq, int cmd)
{
	int rc;
	struct jme_adapter *jme = netdev_priv(netdev);
	struct mii_ioctl_data *mii_data = if_mii(rq);
	unsigned int duplex_chg;

	if (cmd == SIOCSMIIREG) {
		u16 val = mii_data->val_in;
		if (!(val & (BMCR_RESET|BMCR_ANENABLE)) &&
		    (val & BMCR_SPEED1000))
			return -EINVAL;
	}

	spin_lock_bh(&jme->phy_lock);
	rc = generic_mii_ioctl(&jme->mii_if, mii_data, cmd, &duplex_chg);
	spin_unlock_bh(&jme->phy_lock);

	if (!rc && (cmd == SIOCSMIIREG)) {
		if (duplex_chg)
			jme_reset_link(jme);
		jme_get_settings(netdev, &jme->old_ecmd);
		set_bit(JME_FLAG_SSET, &jme->flags);
	}

	return rc;
}

static u32
jme_get_link(struct net_device *netdev)
{
	struct jme_adapter *jme = netdev_priv(netdev);
	return jread32(jme, JME_PHY_LINK) & PHY_LINK_UP;
}

static u32
jme_get_msglevel(struct net_device *netdev)
{
	struct jme_adapter *jme = netdev_priv(netdev);
	return jme->msg_enable;
}

static void
jme_set_msglevel(struct net_device *netdev, u32 value)
{
	struct jme_adapter *jme = netdev_priv(netdev);
	jme->msg_enable = value;
}

static netdev_features_t
jme_fix_features(struct net_device *netdev, netdev_features_t features)
{
	if (netdev->mtu > 1900)
		features &= ~(NETIF_F_ALL_TSO | NETIF_F_ALL_CSUM);
	return features;
}

static int
jme_set_features(struct net_device *netdev, netdev_features_t features)
{
	struct jme_adapter *jme = netdev_priv(netdev);

	spin_lock_bh(&jme->rxmcs_lock);
	if (features & NETIF_F_RXCSUM)
		jme->reg_rxmcs |= RXMCS_CHECKSUM;
	else
		jme->reg_rxmcs &= ~RXMCS_CHECKSUM;
	jwrite32(jme, JME_RXMCS, jme->reg_rxmcs);
	spin_unlock_bh(&jme->rxmcs_lock);

	return 0;
}

static int
jme_nway_reset(struct net_device *netdev)
{
	struct jme_adapter *jme = netdev_priv(netdev);
	jme_restart_an(jme);
	return 0;
}

static u8
jme_smb_read(struct jme_adapter *jme, unsigned int addr)
{
	u32 val;
	int to;

	val = jread32(jme, JME_SMBCSR);
	to = JME_SMB_BUSY_TIMEOUT;
	while ((val & SMBCSR_BUSY) && --to) {
		msleep(1);
		val = jread32(jme, JME_SMBCSR);
	}
	if (!to) {
		netif_err(jme, hw, jme->dev, "SMB Bus Busy\n");
		return 0xFF;
	}

	jwrite32(jme, JME_SMBINTF,
		((addr << SMBINTF_HWADDR_SHIFT) & SMBINTF_HWADDR) |
		SMBINTF_HWRWN_READ |
		SMBINTF_HWCMD);

	val = jread32(jme, JME_SMBINTF);
	to = JME_SMB_BUSY_TIMEOUT;
	while ((val & SMBINTF_HWCMD) && --to) {
		msleep(1);
		val = jread32(jme, JME_SMBINTF);
	}
	if (!to) {
		netif_err(jme, hw, jme->dev, "SMB Bus Busy\n");
		return 0xFF;
	}

	return (val & SMBINTF_HWDATR) >> SMBINTF_HWDATR_SHIFT;
}

static void
jme_smb_write(struct jme_adapter *jme, unsigned int addr, u8 data)
{
	u32 val;
	int to;

	val = jread32(jme, JME_SMBCSR);
	to = JME_SMB_BUSY_TIMEOUT;
	while ((val & SMBCSR_BUSY) && --to) {
		msleep(1);
		val = jread32(jme, JME_SMBCSR);
	}
	if (!to) {
		netif_err(jme, hw, jme->dev, "SMB Bus Busy\n");
		return;
	}

	jwrite32(jme, JME_SMBINTF,
		((data << SMBINTF_HWDATW_SHIFT) & SMBINTF_HWDATW) |
		((addr << SMBINTF_HWADDR_SHIFT) & SMBINTF_HWADDR) |
		SMBINTF_HWRWN_WRITE |
		SMBINTF_HWCMD);

	val = jread32(jme, JME_SMBINTF);
	to = JME_SMB_BUSY_TIMEOUT;
	while ((val & SMBINTF_HWCMD) && --to) {
		msleep(1);
		val = jread32(jme, JME_SMBINTF);
	}
	if (!to) {
		netif_err(jme, hw, jme->dev, "SMB Bus Busy\n");
		return;
	}

	mdelay(2);
}

static int
jme_get_eeprom_len(struct net_device *netdev)
{
	struct jme_adapter *jme = netdev_priv(netdev);
	u32 val;
	val = jread32(jme, JME_SMBCSR);
	return (val & SMBCSR_EEPROMD) ? JME_SMB_LEN : 0;
}

static int
jme_get_eeprom(struct net_device *netdev,
		struct ethtool_eeprom *eeprom, u8 *data)
{
	struct jme_adapter *jme = netdev_priv(netdev);
	int i, offset = eeprom->offset, len = eeprom->len;

	/*
	 * ethtool will check the boundary for us
	 */
	eeprom->magic = JME_EEPROM_MAGIC;
	for (i = 0 ; i < len ; ++i)
		data[i] = jme_smb_read(jme, i + offset);

	return 0;
}

static int
jme_set_eeprom(struct net_device *netdev,
		struct ethtool_eeprom *eeprom, u8 *data)
{
	struct jme_adapter *jme = netdev_priv(netdev);
	int i, offset = eeprom->offset, len = eeprom->len;

	if (eeprom->magic != JME_EEPROM_MAGIC)
		return -EINVAL;

	/*
	 * ethtool will check the boundary for us
	 */
	for (i = 0 ; i < len ; ++i)
		jme_smb_write(jme, i + offset, data[i]);

	return 0;
}

static const struct ethtool_ops jme_ethtool_ops = {
	.get_drvinfo            = jme_get_drvinfo,
	.get_regs_len		= jme_get_regs_len,
	.get_regs		= jme_get_regs,
	.get_coalesce		= jme_get_coalesce,
	.set_coalesce		= jme_set_coalesce,
	.get_pauseparam		= jme_get_pauseparam,
	.set_pauseparam		= jme_set_pauseparam,
	.get_wol		= jme_get_wol,
	.set_wol		= jme_set_wol,
	.get_settings		= jme_get_settings,
	.set_settings		= jme_set_settings,
	.get_link		= jme_get_link,
	.get_msglevel           = jme_get_msglevel,
	.set_msglevel           = jme_set_msglevel,
	.nway_reset             = jme_nway_reset,
	.get_eeprom_len		= jme_get_eeprom_len,
	.get_eeprom		= jme_get_eeprom,
	.set_eeprom		= jme_set_eeprom,
};

static int
jme_pci_dma64(struct pci_dev *pdev)
{
	if (pdev->device == PCI_DEVICE_ID_JMICRON_JMC250 &&
	    !pci_set_dma_mask(pdev, DMA_BIT_MASK(64)))
		if (!pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64)))
			return 1;

	if (pdev->device == PCI_DEVICE_ID_JMICRON_JMC250 &&
	    !pci_set_dma_mask(pdev, DMA_BIT_MASK(40)))
		if (!pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(40)))
			return 1;

	if (!pci_set_dma_mask(pdev, DMA_BIT_MASK(32)))
		if (!pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32)))
			return 0;

	return -1;
}

static inline void
jme_phy_init(struct jme_adapter *jme)
{
	u16 reg26;

	reg26 = jme_mdio_read(jme->dev, jme->mii_if.phy_id, 26);
	jme_mdio_write(jme->dev, jme->mii_if.phy_id, 26, reg26 | 0x1000);
}

static inline void
jme_check_hw_ver(struct jme_adapter *jme)
{
	u32 chipmode;

	chipmode = jread32(jme, JME_CHIPMODE);

	jme->fpgaver = (chipmode & CM_FPGAVER_MASK) >> CM_FPGAVER_SHIFT;
	jme->chiprev = (chipmode & CM_CHIPREV_MASK) >> CM_CHIPREV_SHIFT;
	jme->chip_main_rev = jme->chiprev & 0xF;
	jme->chip_sub_rev = (jme->chiprev >> 4) & 0xF;
}

static const struct net_device_ops jme_netdev_ops = {
	.ndo_open		= jme_open,
	.ndo_stop		= jme_close,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_do_ioctl		= jme_ioctl,
	.ndo_start_xmit		= jme_start_xmit,
	.ndo_set_mac_address	= jme_set_macaddr,
	.ndo_set_rx_mode	= jme_set_multi,
	.ndo_change_mtu		= jme_change_mtu,
	.ndo_tx_timeout		= jme_tx_timeout,
	.ndo_fix_features       = jme_fix_features,
	.ndo_set_features       = jme_set_features,
};

static int __devinit
jme_init_one(struct pci_dev *pdev,
	     const struct pci_device_id *ent)
{
	int rc = 0, using_dac, i;
	struct net_device *netdev;
	struct jme_adapter *jme;
	u16 bmcr, bmsr;
	u32 apmc;

	/*
	 * set up PCI device basics
	 */
	rc = pci_enable_device(pdev);
	if (rc) {
		pr_err("Cannot enable PCI device\n");
		goto err_out;
	}

	using_dac = jme_pci_dma64(pdev);
	if (using_dac < 0) {
		pr_err("Cannot set PCI DMA Mask\n");
		rc = -EIO;
		goto err_out_disable_pdev;
	}

	if (!(pci_resource_flags(pdev, 0) & IORESOURCE_MEM)) {
		pr_err("No PCI resource region found\n");
		rc = -ENOMEM;
		goto err_out_disable_pdev;
	}

	rc = pci_request_regions(pdev, DRV_NAME);
	if (rc) {
		pr_err("Cannot obtain PCI resource region\n");
		goto err_out_disable_pdev;
	}

	pci_set_master(pdev);

	/*
	 * alloc and init net device
	 */
	netdev = alloc_etherdev(sizeof(*jme));
	if (!netdev) {
		rc = -ENOMEM;
		goto err_out_release_regions;
	}
	netdev->netdev_ops = &jme_netdev_ops;
	netdev->ethtool_ops		= &jme_ethtool_ops;
	netdev->watchdog_timeo		= TX_TIMEOUT;
	netdev->hw_features		=	NETIF_F_IP_CSUM |
						NETIF_F_IPV6_CSUM |
						NETIF_F_SG |
						NETIF_F_TSO |
						NETIF_F_TSO6 |
						NETIF_F_RXCSUM;
	netdev->features		=	NETIF_F_IP_CSUM |
						NETIF_F_IPV6_CSUM |
						NETIF_F_SG |
						NETIF_F_TSO |
						NETIF_F_TSO6 |
						NETIF_F_HW_VLAN_TX |
						NETIF_F_HW_VLAN_RX;
	if (using_dac)
		netdev->features	|=	NETIF_F_HIGHDMA;

	SET_NETDEV_DEV(netdev, &pdev->dev);
	pci_set_drvdata(pdev, netdev);

	/*
	 * init adapter info
	 */
	jme = netdev_priv(netdev);
	jme->pdev = pdev;
	jme->dev = netdev;
	jme->jme_rx = netif_rx;
	jme->old_mtu = netdev->mtu = 1500;
	jme->phylink = 0;
	jme->tx_ring_size = 1 << 10;
	jme->tx_ring_mask = jme->tx_ring_size - 1;
	jme->tx_wake_threshold = 1 << 9;
	jme->rx_ring_size = 1 << 9;
	jme->rx_ring_mask = jme->rx_ring_size - 1;
	jme->msg_enable = JME_DEF_MSG_ENABLE;
	jme->regs = ioremap(pci_resource_start(pdev, 0),
			     pci_resource_len(pdev, 0));
	if (!(jme->regs)) {
		pr_err("Mapping PCI resource region error\n");
		rc = -ENOMEM;
		goto err_out_free_netdev;
	}

	if (no_pseudohp) {
		apmc = jread32(jme, JME_APMC) & ~JME_APMC_PSEUDO_HP_EN;
		jwrite32(jme, JME_APMC, apmc);
	} else if (force_pseudohp) {
		apmc = jread32(jme, JME_APMC) | JME_APMC_PSEUDO_HP_EN;
		jwrite32(jme, JME_APMC, apmc);
	}

	NETIF_NAPI_SET(netdev, &jme->napi, jme_poll, jme->rx_ring_size >> 2)

	spin_lock_init(&jme->phy_lock);
	spin_lock_init(&jme->macaddr_lock);
	spin_lock_init(&jme->rxmcs_lock);

	atomic_set(&jme->link_changing, 1);
	atomic_set(&jme->rx_cleaning, 1);
	atomic_set(&jme->tx_cleaning, 1);
	atomic_set(&jme->rx_empty, 1);

	tasklet_init(&jme->pcc_task,
		     jme_pcc_tasklet,
		     (unsigned long) jme);
	tasklet_init(&jme->linkch_task,
		     jme_link_change_tasklet,
		     (unsigned long) jme);
	tasklet_init(&jme->txclean_task,
		     jme_tx_clean_tasklet,
		     (unsigned long) jme);
	tasklet_init(&jme->rxclean_task,
		     jme_rx_clean_tasklet,
		     (unsigned long) jme);
	tasklet_init(&jme->rxempty_task,
		     jme_rx_empty_tasklet,
		     (unsigned long) jme);
	tasklet_disable_nosync(&jme->linkch_task);
	tasklet_disable_nosync(&jme->txclean_task);
	tasklet_disable_nosync(&jme->rxclean_task);
	tasklet_disable_nosync(&jme->rxempty_task);
	jme->dpi.cur = PCC_P1;

	jme->reg_ghc = 0;
	jme->reg_rxcs = RXCS_DEFAULT;
	jme->reg_rxmcs = RXMCS_DEFAULT;
	jme->reg_txpfc = 0;
	jme->reg_pmcs = PMCS_MFEN;
	jme->reg_gpreg1 = GPREG1_DEFAULT;

	if (jme->reg_rxmcs & RXMCS_CHECKSUM)
		netdev->features |= NETIF_F_RXCSUM;

	/*
	 * Get Max Read Req Size from PCI Config Space
	 */
	pci_read_config_byte(pdev, PCI_DCSR_MRRS, &jme->mrrs);
	jme->mrrs &= PCI_DCSR_MRRS_MASK;
	switch (jme->mrrs) {
	case MRRS_128B:
		jme->reg_txcs = TXCS_DEFAULT | TXCS_DMASIZE_128B;
		break;
	case MRRS_256B:
		jme->reg_txcs = TXCS_DEFAULT | TXCS_DMASIZE_256B;
		break;
	default:
		jme->reg_txcs = TXCS_DEFAULT | TXCS_DMASIZE_512B;
		break;
	}

	/*
	 * Must check before reset_mac_processor
	 */
	jme_check_hw_ver(jme);
	jme->mii_if.dev = netdev;
	if (jme->fpgaver) {
		jme->mii_if.phy_id = 0;
		for (i = 1 ; i < 32 ; ++i) {
			bmcr = jme_mdio_read(netdev, i, MII_BMCR);
			bmsr = jme_mdio_read(netdev, i, MII_BMSR);
			if (bmcr != 0xFFFFU && (bmcr != 0 || bmsr != 0)) {
				jme->mii_if.phy_id = i;
				break;
			}
		}

		if (!jme->mii_if.phy_id) {
			rc = -EIO;
			pr_err("Can not find phy_id\n");
			goto err_out_unmap;
		}

		jme->reg_ghc |= GHC_LINK_POLL;
	} else {
		jme->mii_if.phy_id = 1;
	}
	if (pdev->device == PCI_DEVICE_ID_JMICRON_JMC250)
		jme->mii_if.supports_gmii = true;
	else
		jme->mii_if.supports_gmii = false;
	jme->mii_if.phy_id_mask = 0x1F;
	jme->mii_if.reg_num_mask = 0x1F;
	jme->mii_if.mdio_read = jme_mdio_read;
	jme->mii_if.mdio_write = jme_mdio_write;

	jme_clear_pm(jme);
	pci_set_power_state(jme->pdev, PCI_D0);
	device_set_wakeup_enable(&pdev->dev, true);

	jme_set_phyfifo_5level(jme);
	jme->pcirev = pdev->revision;
	if (!jme->fpgaver)
		jme_phy_init(jme);
	jme_phy_off(jme);

	/*
	 * Reset MAC processor and reload EEPROM for MAC Address
	 */
	jme_reset_mac_processor(jme);
	rc = jme_reload_eeprom(jme);
	if (rc) {
		pr_err("Reload eeprom for reading MAC Address error\n");
		goto err_out_unmap;
	}
	jme_load_macaddr(netdev);

	/*
	 * Tell stack that we are not ready to work until open()
	 */
	netif_carrier_off(netdev);

	rc = register_netdev(netdev);
	if (rc) {
		pr_err("Cannot register net device\n");
		goto err_out_unmap;
	}

	netif_info(jme, probe, jme->dev, "%s%s chiprev:%x pcirev:%x macaddr:%pM\n",
		   (jme->pdev->device == PCI_DEVICE_ID_JMICRON_JMC250) ?
		   "JMC250 Gigabit Ethernet" :
		   (jme->pdev->device == PCI_DEVICE_ID_JMICRON_JMC260) ?
		   "JMC260 Fast Ethernet" : "Unknown",
		   (jme->fpgaver != 0) ? " (FPGA)" : "",
		   (jme->fpgaver != 0) ? jme->fpgaver : jme->chiprev,
		   jme->pcirev, netdev->dev_addr);

	return 0;

err_out_unmap:
	iounmap(jme->regs);
err_out_free_netdev:
	pci_set_drvdata(pdev, NULL);
	free_netdev(netdev);
err_out_release_regions:
	pci_release_regions(pdev);
err_out_disable_pdev:
	pci_disable_device(pdev);
err_out:
	return rc;
}

static void __devexit
jme_remove_one(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct jme_adapter *jme = netdev_priv(netdev);

	unregister_netdev(netdev);
	iounmap(jme->regs);
	pci_set_drvdata(pdev, NULL);
	free_netdev(netdev);
	pci_release_regions(pdev);
	pci_disable_device(pdev);

}

static void
jme_shutdown(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct jme_adapter *jme = netdev_priv(netdev);

	jme_powersave_phy(jme);
	pci_pme_active(pdev, true);
}

#ifdef CONFIG_PM_SLEEP
static int
jme_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct jme_adapter *jme = netdev_priv(netdev);

	if (!netif_running(netdev))
		return 0;

	atomic_dec(&jme->link_changing);

	netif_device_detach(netdev);
	netif_stop_queue(netdev);
	jme_stop_irq(jme);

	tasklet_disable(&jme->txclean_task);
	tasklet_disable(&jme->rxclean_task);
	tasklet_disable(&jme->rxempty_task);

	if (netif_carrier_ok(netdev)) {
		if (test_bit(JME_FLAG_POLL, &jme->flags))
			jme_polling_mode(jme);

		jme_stop_pcc_timer(jme);
		jme_disable_rx_engine(jme);
		jme_disable_tx_engine(jme);
		jme_reset_mac_processor(jme);
		jme_free_rx_resources(jme);
		jme_free_tx_resources(jme);
		netif_carrier_off(netdev);
		jme->phylink = 0;
	}

	tasklet_enable(&jme->txclean_task);
	tasklet_hi_enable(&jme->rxclean_task);
	tasklet_hi_enable(&jme->rxempty_task);

	jme_powersave_phy(jme);

	return 0;
}

static int
jme_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct jme_adapter *jme = netdev_priv(netdev);

	if (!netif_running(netdev))
		return 0;

	jme_clear_pm(jme);
	jme_phy_on(jme);
	if (test_bit(JME_FLAG_SSET, &jme->flags))
		jme_set_settings(netdev, &jme->old_ecmd);
	else
		jme_reset_phy_processor(jme);
	jme_phy_calibration(jme);
	jme_phy_setEA(jme);
	jme_start_irq(jme);
	netif_device_attach(netdev);

	atomic_inc(&jme->link_changing);

	jme_reset_link(jme);

	return 0;
}

static SIMPLE_DEV_PM_OPS(jme_pm_ops, jme_suspend, jme_resume);
#define JME_PM_OPS (&jme_pm_ops)

#else

#define JME_PM_OPS NULL
#endif

static DEFINE_PCI_DEVICE_TABLE(jme_pci_tbl) = {
	{ PCI_VDEVICE(JMICRON, PCI_DEVICE_ID_JMICRON_JMC250) },
	{ PCI_VDEVICE(JMICRON, PCI_DEVICE_ID_JMICRON_JMC260) },
	{ }
};

static struct pci_driver jme_driver = {
	.name           = DRV_NAME,
	.id_table       = jme_pci_tbl,
	.probe          = jme_init_one,
	.remove         = __devexit_p(jme_remove_one),
	.shutdown       = jme_shutdown,
	.driver.pm	= JME_PM_OPS,
};

static int __init
jme_init_module(void)
{
	pr_info("JMicron JMC2XX ethernet driver version %s\n", DRV_VERSION);
	return pci_register_driver(&jme_driver);
}

static void __exit
jme_cleanup_module(void)
{
	pci_unregister_driver(&jme_driver);
}

module_init(jme_init_module);
module_exit(jme_cleanup_module);

MODULE_AUTHOR("Guo-Fu Tseng <cooldavid@cooldavid.org>");
MODULE_DESCRIPTION("JMicron JMC2x0 PCI Express Ethernet driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);
MODULE_DEVICE_TABLE(pci, jme_pci_tbl);
