/*
 * New driver for Marvell Yukon 2 chipset.
 * Based on earlier sk98lin, and skge driver.
 *
 * This driver intentionally does not support all the features
 * of the original driver such as link fail-over and link management because
 * those should be done at higher levels.
 *
 * Copyright (C) 2005 Stephen Hemminger <shemminger@osdl.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/config.h>
#include <linux/crc32.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/dma-mapping.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/pci.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/in.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/if_vlan.h>
#include <linux/prefetch.h>
#include <linux/mii.h>

#include <asm/irq.h>

#if defined(CONFIG_VLAN_8021Q) || defined(CONFIG_VLAN_8021Q_MODULE)
#define SKY2_VLAN_TAG_USED 1
#endif

#include "sky2.h"

#define DRV_NAME		"sky2"
#define DRV_VERSION		"1.2"
#define PFX			DRV_NAME " "

/*
 * The Yukon II chipset takes 64 bit command blocks (called list elements)
 * that are organized into three (receive, transmit, status) different rings
 * similar to Tigon3. A transmit can require several elements;
 * a receive requires one (or two if using 64 bit dma).
 */

#define RX_LE_SIZE	    	512
#define RX_LE_BYTES		(RX_LE_SIZE*sizeof(struct sky2_rx_le))
#define RX_MAX_PENDING		(RX_LE_SIZE/2 - 2)
#define RX_DEF_PENDING		RX_MAX_PENDING
#define RX_SKB_ALIGN		8

#define TX_RING_SIZE		512
#define TX_DEF_PENDING		(TX_RING_SIZE - 1)
#define TX_MIN_PENDING		64
#define MAX_SKB_TX_LE		(4 + (sizeof(dma_addr_t)/sizeof(u32))*MAX_SKB_FRAGS)

#define STATUS_RING_SIZE	2048	/* 2 ports * (TX + 2*RX) */
#define STATUS_LE_BYTES		(STATUS_RING_SIZE*sizeof(struct sky2_status_le))
#define ETH_JUMBO_MTU		9000
#define TX_WATCHDOG		(5 * HZ)
#define NAPI_WEIGHT		64
#define PHY_RETRIES		1000

static const u32 default_msg =
    NETIF_MSG_DRV | NETIF_MSG_PROBE | NETIF_MSG_LINK
    | NETIF_MSG_TIMER | NETIF_MSG_TX_ERR | NETIF_MSG_RX_ERR
    | NETIF_MSG_IFUP | NETIF_MSG_IFDOWN;

static int debug = -1;		/* defaults above */
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "Debug level (0=none,...,16=all)");

static int copybreak __read_mostly = 256;
module_param(copybreak, int, 0);
MODULE_PARM_DESC(copybreak, "Receive copy threshold");

static int disable_msi = 0;
module_param(disable_msi, int, 0);
MODULE_PARM_DESC(disable_msi, "Disable Message Signaled Interrupt (MSI)");

static const struct pci_device_id sky2_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_SYSKONNECT, 0x9000) },
	{ PCI_DEVICE(PCI_VENDOR_ID_SYSKONNECT, 0x9E00) },
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x4340) },
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x4341) },
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x4342) },
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x4343) },
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x4344) },
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x4345) },
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x4346) },
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x4347) },
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x4350) },
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x4351) },
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x4352) },
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x4360) },
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x4361) },
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x4362) },
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x4363) },
	{ 0 }
};

MODULE_DEVICE_TABLE(pci, sky2_id_table);

/* Avoid conditionals by using array */
static const unsigned txqaddr[] = { Q_XA1, Q_XA2 };
static const unsigned rxqaddr[] = { Q_R1, Q_R2 };

/* This driver supports yukon2 chipset only */
static const char *yukon2_name[] = {
	"XL",		/* 0xb3 */
	"EC Ultra", 	/* 0xb4 */
	"UNKNOWN",	/* 0xb5 */
	"EC",		/* 0xb6 */
	"FE",		/* 0xb7 */
};

/* Access to external PHY */
static int gm_phy_write(struct sky2_hw *hw, unsigned port, u16 reg, u16 val)
{
	int i;

	gma_write16(hw, port, GM_SMI_DATA, val);
	gma_write16(hw, port, GM_SMI_CTRL,
		    GM_SMI_CT_PHY_AD(PHY_ADDR_MARV) | GM_SMI_CT_REG_AD(reg));

	for (i = 0; i < PHY_RETRIES; i++) {
		if (!(gma_read16(hw, port, GM_SMI_CTRL) & GM_SMI_CT_BUSY))
			return 0;
		udelay(1);
	}

	printk(KERN_WARNING PFX "%s: phy write timeout\n", hw->dev[port]->name);
	return -ETIMEDOUT;
}

static int __gm_phy_read(struct sky2_hw *hw, unsigned port, u16 reg, u16 *val)
{
	int i;

	gma_write16(hw, port, GM_SMI_CTRL, GM_SMI_CT_PHY_AD(PHY_ADDR_MARV)
		    | GM_SMI_CT_REG_AD(reg) | GM_SMI_CT_OP_RD);

	for (i = 0; i < PHY_RETRIES; i++) {
		if (gma_read16(hw, port, GM_SMI_CTRL) & GM_SMI_CT_RD_VAL) {
			*val = gma_read16(hw, port, GM_SMI_DATA);
			return 0;
		}

		udelay(1);
	}

	return -ETIMEDOUT;
}

static u16 gm_phy_read(struct sky2_hw *hw, unsigned port, u16 reg)
{
	u16 v;

	if (__gm_phy_read(hw, port, reg, &v) != 0)
		printk(KERN_WARNING PFX "%s: phy read timeout\n", hw->dev[port]->name);
	return v;
}

static int sky2_set_power_state(struct sky2_hw *hw, pci_power_t state)
{
	u16 power_control;
	u32 reg1;
	int vaux;
	int ret = 0;

	pr_debug("sky2_set_power_state %d\n", state);
	sky2_write8(hw, B2_TST_CTRL1, TST_CFG_WRITE_ON);

	power_control = sky2_pci_read16(hw, hw->pm_cap + PCI_PM_PMC);
	vaux = (sky2_read16(hw, B0_CTST) & Y2_VAUX_AVAIL) &&
		(power_control & PCI_PM_CAP_PME_D3cold);

	power_control = sky2_pci_read16(hw, hw->pm_cap + PCI_PM_CTRL);

	power_control |= PCI_PM_CTRL_PME_STATUS;
	power_control &= ~(PCI_PM_CTRL_STATE_MASK);

	switch (state) {
	case PCI_D0:
		/* switch power to VCC (WA for VAUX problem) */
		sky2_write8(hw, B0_POWER_CTRL,
			    PC_VAUX_ENA | PC_VCC_ENA | PC_VAUX_OFF | PC_VCC_ON);

		/* disable Core Clock Division, */
		sky2_write32(hw, B2_Y2_CLK_CTRL, Y2_CLK_DIV_DIS);

		if (hw->chip_id == CHIP_ID_YUKON_XL && hw->chip_rev > 1)
			/* enable bits are inverted */
			sky2_write8(hw, B2_Y2_CLK_GATE,
				    Y2_PCI_CLK_LNK1_DIS | Y2_COR_CLK_LNK1_DIS |
				    Y2_CLK_GAT_LNK1_DIS | Y2_PCI_CLK_LNK2_DIS |
				    Y2_COR_CLK_LNK2_DIS | Y2_CLK_GAT_LNK2_DIS);
		else
			sky2_write8(hw, B2_Y2_CLK_GATE, 0);

		/* Turn off phy power saving */
		reg1 = sky2_pci_read32(hw, PCI_DEV_REG1);
		reg1 &= ~(PCI_Y2_PHY1_POWD | PCI_Y2_PHY2_POWD);

		/* looks like this XL is back asswards .. */
		if (hw->chip_id == CHIP_ID_YUKON_XL && hw->chip_rev > 1) {
			reg1 |= PCI_Y2_PHY1_COMA;
			if (hw->ports > 1)
				reg1 |= PCI_Y2_PHY2_COMA;
		}

		if (hw->chip_id == CHIP_ID_YUKON_EC_U) {
			sky2_pci_write32(hw, PCI_DEV_REG3, 0);
			reg1 = sky2_pci_read32(hw, PCI_DEV_REG4);
			reg1 &= P_ASPM_CONTROL_MSK;
			sky2_pci_write32(hw, PCI_DEV_REG4, reg1);
			sky2_pci_write32(hw, PCI_DEV_REG5, 0);
		}

		sky2_pci_write32(hw, PCI_DEV_REG1, reg1);

		break;

	case PCI_D3hot:
	case PCI_D3cold:
		/* Turn on phy power saving */
		reg1 = sky2_pci_read32(hw, PCI_DEV_REG1);
		if (hw->chip_id == CHIP_ID_YUKON_XL && hw->chip_rev > 1)
			reg1 &= ~(PCI_Y2_PHY1_POWD | PCI_Y2_PHY2_POWD);
		else
			reg1 |= (PCI_Y2_PHY1_POWD | PCI_Y2_PHY2_POWD);
		sky2_pci_write32(hw, PCI_DEV_REG1, reg1);

		if (hw->chip_id == CHIP_ID_YUKON_XL && hw->chip_rev > 1)
			sky2_write8(hw, B2_Y2_CLK_GATE, 0);
		else
			/* enable bits are inverted */
			sky2_write8(hw, B2_Y2_CLK_GATE,
				    Y2_PCI_CLK_LNK1_DIS | Y2_COR_CLK_LNK1_DIS |
				    Y2_CLK_GAT_LNK1_DIS | Y2_PCI_CLK_LNK2_DIS |
				    Y2_COR_CLK_LNK2_DIS | Y2_CLK_GAT_LNK2_DIS);

		/* switch power to VAUX */
		if (vaux && state != PCI_D3cold)
			sky2_write8(hw, B0_POWER_CTRL,
				    (PC_VAUX_ENA | PC_VCC_ENA |
				     PC_VAUX_ON | PC_VCC_OFF));
		break;
	default:
		printk(KERN_ERR PFX "Unknown power state %d\n", state);
		ret = -1;
	}

	sky2_pci_write16(hw, hw->pm_cap + PCI_PM_CTRL, power_control);
	sky2_write8(hw, B2_TST_CTRL1, TST_CFG_WRITE_OFF);
	return ret;
}

static void sky2_phy_reset(struct sky2_hw *hw, unsigned port)
{
	u16 reg;

	/* disable all GMAC IRQ's */
	sky2_write8(hw, SK_REG(port, GMAC_IRQ_MSK), 0);
	/* disable PHY IRQs */
	gm_phy_write(hw, port, PHY_MARV_INT_MASK, 0);

	gma_write16(hw, port, GM_MC_ADDR_H1, 0);	/* clear MC hash */
	gma_write16(hw, port, GM_MC_ADDR_H2, 0);
	gma_write16(hw, port, GM_MC_ADDR_H3, 0);
	gma_write16(hw, port, GM_MC_ADDR_H4, 0);

	reg = gma_read16(hw, port, GM_RX_CTRL);
	reg |= GM_RXCR_UCF_ENA | GM_RXCR_MCF_ENA;
	gma_write16(hw, port, GM_RX_CTRL, reg);
}

static void sky2_phy_init(struct sky2_hw *hw, unsigned port)
{
	struct sky2_port *sky2 = netdev_priv(hw->dev[port]);
	u16 ctrl, ct1000, adv, pg, ledctrl, ledover;

	if (sky2->autoneg == AUTONEG_ENABLE && hw->chip_id != CHIP_ID_YUKON_XL) {
		u16 ectrl = gm_phy_read(hw, port, PHY_MARV_EXT_CTRL);

		ectrl &= ~(PHY_M_EC_M_DSC_MSK | PHY_M_EC_S_DSC_MSK |
			   PHY_M_EC_MAC_S_MSK);
		ectrl |= PHY_M_EC_MAC_S(MAC_TX_CLK_25_MHZ);

		if (hw->chip_id == CHIP_ID_YUKON_EC)
			ectrl |= PHY_M_EC_DSC_2(2) | PHY_M_EC_DOWN_S_ENA;
		else
			ectrl |= PHY_M_EC_M_DSC(2) | PHY_M_EC_S_DSC(3);

		gm_phy_write(hw, port, PHY_MARV_EXT_CTRL, ectrl);
	}

	ctrl = gm_phy_read(hw, port, PHY_MARV_PHY_CTRL);
	if (hw->copper) {
		if (hw->chip_id == CHIP_ID_YUKON_FE) {
			/* enable automatic crossover */
			ctrl |= PHY_M_PC_MDI_XMODE(PHY_M_PC_ENA_AUTO) >> 1;
		} else {
			/* disable energy detect */
			ctrl &= ~PHY_M_PC_EN_DET_MSK;

			/* enable automatic crossover */
			ctrl |= PHY_M_PC_MDI_XMODE(PHY_M_PC_ENA_AUTO);

			if (sky2->autoneg == AUTONEG_ENABLE &&
			    hw->chip_id == CHIP_ID_YUKON_XL) {
				ctrl &= ~PHY_M_PC_DSC_MSK;
				ctrl |= PHY_M_PC_DSC(2) | PHY_M_PC_DOWN_S_ENA;
			}
		}
		gm_phy_write(hw, port, PHY_MARV_PHY_CTRL, ctrl);
	} else {
		/* workaround for deviation #4.88 (CRC errors) */
		/* disable Automatic Crossover */

		ctrl &= ~PHY_M_PC_MDIX_MSK;
		gm_phy_write(hw, port, PHY_MARV_PHY_CTRL, ctrl);

		if (hw->chip_id == CHIP_ID_YUKON_XL) {
			/* Fiber: select 1000BASE-X only mode MAC Specific Ctrl Reg. */
			gm_phy_write(hw, port, PHY_MARV_EXT_ADR, 2);
			ctrl = gm_phy_read(hw, port, PHY_MARV_PHY_CTRL);
			ctrl &= ~PHY_M_MAC_MD_MSK;
			ctrl |= PHY_M_MAC_MODE_SEL(PHY_M_MAC_MD_1000BX);
			gm_phy_write(hw, port, PHY_MARV_PHY_CTRL, ctrl);

			/* select page 1 to access Fiber registers */
			gm_phy_write(hw, port, PHY_MARV_EXT_ADR, 1);
		}
	}

	ctrl = gm_phy_read(hw, port, PHY_MARV_CTRL);
	if (sky2->autoneg == AUTONEG_DISABLE)
		ctrl &= ~PHY_CT_ANE;
	else
		ctrl |= PHY_CT_ANE;

	ctrl |= PHY_CT_RESET;
	gm_phy_write(hw, port, PHY_MARV_CTRL, ctrl);

	ctrl = 0;
	ct1000 = 0;
	adv = PHY_AN_CSMA;

	if (sky2->autoneg == AUTONEG_ENABLE) {
		if (hw->copper) {
			if (sky2->advertising & ADVERTISED_1000baseT_Full)
				ct1000 |= PHY_M_1000C_AFD;
			if (sky2->advertising & ADVERTISED_1000baseT_Half)
				ct1000 |= PHY_M_1000C_AHD;
			if (sky2->advertising & ADVERTISED_100baseT_Full)
				adv |= PHY_M_AN_100_FD;
			if (sky2->advertising & ADVERTISED_100baseT_Half)
				adv |= PHY_M_AN_100_HD;
			if (sky2->advertising & ADVERTISED_10baseT_Full)
				adv |= PHY_M_AN_10_FD;
			if (sky2->advertising & ADVERTISED_10baseT_Half)
				adv |= PHY_M_AN_10_HD;
		} else		/* special defines for FIBER (88E1011S only) */
			adv |= PHY_M_AN_1000X_AHD | PHY_M_AN_1000X_AFD;

		/* Set Flow-control capabilities */
		if (sky2->tx_pause && sky2->rx_pause)
			adv |= PHY_AN_PAUSE_CAP;	/* symmetric */
		else if (sky2->rx_pause && !sky2->tx_pause)
			adv |= PHY_AN_PAUSE_ASYM | PHY_AN_PAUSE_CAP;
		else if (!sky2->rx_pause && sky2->tx_pause)
			adv |= PHY_AN_PAUSE_ASYM;	/* local */

		/* Restart Auto-negotiation */
		ctrl |= PHY_CT_ANE | PHY_CT_RE_CFG;
	} else {
		/* forced speed/duplex settings */
		ct1000 = PHY_M_1000C_MSE;

		if (sky2->duplex == DUPLEX_FULL)
			ctrl |= PHY_CT_DUP_MD;

		switch (sky2->speed) {
		case SPEED_1000:
			ctrl |= PHY_CT_SP1000;
			break;
		case SPEED_100:
			ctrl |= PHY_CT_SP100;
			break;
		}

		ctrl |= PHY_CT_RESET;
	}

	if (hw->chip_id != CHIP_ID_YUKON_FE)
		gm_phy_write(hw, port, PHY_MARV_1000T_CTRL, ct1000);

	gm_phy_write(hw, port, PHY_MARV_AUNE_ADV, adv);
	gm_phy_write(hw, port, PHY_MARV_CTRL, ctrl);

	/* Setup Phy LED's */
	ledctrl = PHY_M_LED_PULS_DUR(PULS_170MS);
	ledover = 0;

	switch (hw->chip_id) {
	case CHIP_ID_YUKON_FE:
		/* on 88E3082 these bits are at 11..9 (shifted left) */
		ledctrl |= PHY_M_LED_BLINK_RT(BLINK_84MS) << 1;

		ctrl = gm_phy_read(hw, port, PHY_MARV_FE_LED_PAR);

		/* delete ACT LED control bits */
		ctrl &= ~PHY_M_FELP_LED1_MSK;
		/* change ACT LED control to blink mode */
		ctrl |= PHY_M_FELP_LED1_CTRL(LED_PAR_CTRL_ACT_BL);
		gm_phy_write(hw, port, PHY_MARV_FE_LED_PAR, ctrl);
		break;

	case CHIP_ID_YUKON_XL:
		pg = gm_phy_read(hw, port, PHY_MARV_EXT_ADR);

		/* select page 3 to access LED control register */
		gm_phy_write(hw, port, PHY_MARV_EXT_ADR, 3);

		/* set LED Function Control register */
		gm_phy_write(hw, port, PHY_MARV_PHY_CTRL, (PHY_M_LEDC_LOS_CTRL(1) |	/* LINK/ACT */
							   PHY_M_LEDC_INIT_CTRL(7) |	/* 10 Mbps */
							   PHY_M_LEDC_STA1_CTRL(7) |	/* 100 Mbps */
							   PHY_M_LEDC_STA0_CTRL(7)));	/* 1000 Mbps */

		/* set Polarity Control register */
		gm_phy_write(hw, port, PHY_MARV_PHY_STAT,
			     (PHY_M_POLC_LS1_P_MIX(4) |
			      PHY_M_POLC_IS0_P_MIX(4) |
			      PHY_M_POLC_LOS_CTRL(2) |
			      PHY_M_POLC_INIT_CTRL(2) |
			      PHY_M_POLC_STA1_CTRL(2) |
			      PHY_M_POLC_STA0_CTRL(2)));

		/* restore page register */
		gm_phy_write(hw, port, PHY_MARV_EXT_ADR, pg);
		break;

	default:
		/* set Tx LED (LED_TX) to blink mode on Rx OR Tx activity */
		ledctrl |= PHY_M_LED_BLINK_RT(BLINK_84MS) | PHY_M_LEDC_TX_CTRL;
		/* turn off the Rx LED (LED_RX) */
		ledover |= PHY_M_LED_MO_RX(MO_LED_OFF);
	}

	if (hw->chip_id == CHIP_ID_YUKON_EC_U && hw->chip_rev >= 2) {
		/* apply fixes in PHY AFE */
		gm_phy_write(hw, port, 22, 255);
		/* increase differential signal amplitude in 10BASE-T */
		gm_phy_write(hw, port, 24, 0xaa99);
		gm_phy_write(hw, port, 23, 0x2011);

		/* fix for IEEE A/B Symmetry failure in 1000BASE-T */
		gm_phy_write(hw, port, 24, 0xa204);
		gm_phy_write(hw, port, 23, 0x2002);

		/* set page register to 0 */
		gm_phy_write(hw, port, 22, 0);
	} else {
		gm_phy_write(hw, port, PHY_MARV_LED_CTRL, ledctrl);

		if (sky2->autoneg == AUTONEG_DISABLE || sky2->speed == SPEED_100) {
			/* turn on 100 Mbps LED (LED_LINK100) */
			ledover |= PHY_M_LED_MO_100(MO_LED_ON);
		}

		if (ledover)
			gm_phy_write(hw, port, PHY_MARV_LED_OVER, ledover);

	}
	/* Enable phy interrupt on auto-negotiation complete (or link up) */
	if (sky2->autoneg == AUTONEG_ENABLE)
		gm_phy_write(hw, port, PHY_MARV_INT_MASK, PHY_M_IS_AN_COMPL);
	else
		gm_phy_write(hw, port, PHY_MARV_INT_MASK, PHY_M_DEF_MSK);
}

/* Force a renegotiation */
static void sky2_phy_reinit(struct sky2_port *sky2)
{
	spin_lock_bh(&sky2->phy_lock);
	sky2_phy_init(sky2->hw, sky2->port);
	spin_unlock_bh(&sky2->phy_lock);
}

static void sky2_mac_init(struct sky2_hw *hw, unsigned port)
{
	struct sky2_port *sky2 = netdev_priv(hw->dev[port]);
	u16 reg;
	int i;
	const u8 *addr = hw->dev[port]->dev_addr;

	sky2_write32(hw, SK_REG(port, GPHY_CTRL), GPC_RST_SET);
	sky2_write32(hw, SK_REG(port, GPHY_CTRL), GPC_RST_CLR|GPC_ENA_PAUSE);

	sky2_write8(hw, SK_REG(port, GMAC_CTRL), GMC_RST_CLR);

	if (hw->chip_id == CHIP_ID_YUKON_XL && hw->chip_rev == 0 && port == 1) {
		/* WA DEV_472 -- looks like crossed wires on port 2 */
		/* clear GMAC 1 Control reset */
		sky2_write8(hw, SK_REG(0, GMAC_CTRL), GMC_RST_CLR);
		do {
			sky2_write8(hw, SK_REG(1, GMAC_CTRL), GMC_RST_SET);
			sky2_write8(hw, SK_REG(1, GMAC_CTRL), GMC_RST_CLR);
		} while (gm_phy_read(hw, 1, PHY_MARV_ID0) != PHY_MARV_ID0_VAL ||
			 gm_phy_read(hw, 1, PHY_MARV_ID1) != PHY_MARV_ID1_Y2 ||
			 gm_phy_read(hw, 1, PHY_MARV_INT_MASK) != 0);
	}

	if (sky2->autoneg == AUTONEG_DISABLE) {
		reg = gma_read16(hw, port, GM_GP_CTRL);
		reg |= GM_GPCR_AU_ALL_DIS;
		gma_write16(hw, port, GM_GP_CTRL, reg);
		gma_read16(hw, port, GM_GP_CTRL);

		switch (sky2->speed) {
		case SPEED_1000:
			reg &= ~GM_GPCR_SPEED_100;
			reg |= GM_GPCR_SPEED_1000;
			break;
		case SPEED_100:
			reg &= ~GM_GPCR_SPEED_1000;
			reg |= GM_GPCR_SPEED_100;
			break;
		case SPEED_10:
			reg &= ~(GM_GPCR_SPEED_1000 | GM_GPCR_SPEED_100);
			break;
		}

		if (sky2->duplex == DUPLEX_FULL)
			reg |= GM_GPCR_DUP_FULL;
	} else
		reg = GM_GPCR_SPEED_1000 | GM_GPCR_SPEED_100 | GM_GPCR_DUP_FULL;

	if (!sky2->tx_pause && !sky2->rx_pause) {
		sky2_write32(hw, SK_REG(port, GMAC_CTRL), GMC_PAUSE_OFF);
		reg |=
		    GM_GPCR_FC_TX_DIS | GM_GPCR_FC_RX_DIS | GM_GPCR_AU_FCT_DIS;
	} else if (sky2->tx_pause && !sky2->rx_pause) {
		/* disable Rx flow-control */
		reg |= GM_GPCR_FC_RX_DIS | GM_GPCR_AU_FCT_DIS;
	}

	gma_write16(hw, port, GM_GP_CTRL, reg);

	sky2_read16(hw, SK_REG(port, GMAC_IRQ_SRC));

	spin_lock_bh(&sky2->phy_lock);
	sky2_phy_init(hw, port);
	spin_unlock_bh(&sky2->phy_lock);

	/* MIB clear */
	reg = gma_read16(hw, port, GM_PHY_ADDR);
	gma_write16(hw, port, GM_PHY_ADDR, reg | GM_PAR_MIB_CLR);

	for (i = GM_MIB_CNT_BASE; i <= GM_MIB_CNT_END; i += 4)
		gma_read16(hw, port, i);
	gma_write16(hw, port, GM_PHY_ADDR, reg);

	/* transmit control */
	gma_write16(hw, port, GM_TX_CTRL, TX_COL_THR(TX_COL_DEF));

	/* receive control reg: unicast + multicast + no FCS  */
	gma_write16(hw, port, GM_RX_CTRL,
		    GM_RXCR_UCF_ENA | GM_RXCR_CRC_DIS | GM_RXCR_MCF_ENA);

	/* transmit flow control */
	gma_write16(hw, port, GM_TX_FLOW_CTRL, 0xffff);

	/* transmit parameter */
	gma_write16(hw, port, GM_TX_PARAM,
		    TX_JAM_LEN_VAL(TX_JAM_LEN_DEF) |
		    TX_JAM_IPG_VAL(TX_JAM_IPG_DEF) |
		    TX_IPG_JAM_DATA(TX_IPG_JAM_DEF) |
		    TX_BACK_OFF_LIM(TX_BOF_LIM_DEF));

	/* serial mode register */
	reg = DATA_BLIND_VAL(DATA_BLIND_DEF) |
		GM_SMOD_VLAN_ENA | IPG_DATA_VAL(IPG_DATA_DEF);

	if (hw->dev[port]->mtu > ETH_DATA_LEN)
		reg |= GM_SMOD_JUMBO_ENA;

	gma_write16(hw, port, GM_SERIAL_MODE, reg);

	/* virtual address for data */
	gma_set_addr(hw, port, GM_SRC_ADDR_2L, addr);

	/* physical address: used for pause frames */
	gma_set_addr(hw, port, GM_SRC_ADDR_1L, addr);

	/* ignore counter overflows */
	gma_write16(hw, port, GM_TX_IRQ_MSK, 0);
	gma_write16(hw, port, GM_RX_IRQ_MSK, 0);
	gma_write16(hw, port, GM_TR_IRQ_MSK, 0);

	/* Configure Rx MAC FIFO */
	sky2_write8(hw, SK_REG(port, RX_GMF_CTRL_T), GMF_RST_CLR);
	sky2_write32(hw, SK_REG(port, RX_GMF_CTRL_T),
		     GMF_OPER_ON | GMF_RX_F_FL_ON);

	/* Flush Rx MAC FIFO on any flow control or error */
	sky2_write16(hw, SK_REG(port, RX_GMF_FL_MSK), GMR_FS_ANY_ERR);

	/* Set threshold to 0xa (64 bytes)
	 *  ASF disabled so no need to do WA dev #4.30
	 */
	sky2_write16(hw, SK_REG(port, RX_GMF_FL_THR), RX_GMF_FL_THR_DEF);

	/* Configure Tx MAC FIFO */
	sky2_write8(hw, SK_REG(port, TX_GMF_CTRL_T), GMF_RST_CLR);
	sky2_write16(hw, SK_REG(port, TX_GMF_CTRL_T), GMF_OPER_ON);

	if (hw->chip_id == CHIP_ID_YUKON_EC_U) {
		sky2_write8(hw, SK_REG(port, RX_GMF_LP_THR), 768/8);
		sky2_write8(hw, SK_REG(port, RX_GMF_UP_THR), 1024/8);
		if (hw->dev[port]->mtu > ETH_DATA_LEN) {
			/* set Tx GMAC FIFO Almost Empty Threshold */
			sky2_write32(hw, SK_REG(port, TX_GMF_AE_THR), 0x180);
			/* Disable Store & Forward mode for TX */
			sky2_write32(hw, SK_REG(port, TX_GMF_CTRL_T), TX_STFW_DIS);
		}
	}

}

/* Assign Ram Buffer allocation.
 * start and end are in units of 4k bytes
 * ram registers are in units of 64bit words
 */
static void sky2_ramset(struct sky2_hw *hw, u16 q, u8 startk, u8 endk)
{
	u32 start, end;

	start = startk * 4096/8;
	end = (endk * 4096/8) - 1;

	sky2_write8(hw, RB_ADDR(q, RB_CTRL), RB_RST_CLR);
	sky2_write32(hw, RB_ADDR(q, RB_START), start);
	sky2_write32(hw, RB_ADDR(q, RB_END), end);
	sky2_write32(hw, RB_ADDR(q, RB_WP), start);
	sky2_write32(hw, RB_ADDR(q, RB_RP), start);

	if (q == Q_R1 || q == Q_R2) {
		u32 space = (endk - startk) * 4096/8;
		u32 tp = space - space/4;

		/* On receive queue's set the thresholds
		 * give receiver priority when > 3/4 full
		 * send pause when down to 2K
		 */
		sky2_write32(hw, RB_ADDR(q, RB_RX_UTHP), tp);
		sky2_write32(hw, RB_ADDR(q, RB_RX_LTHP), space/2);

		tp = space - 2048/8;
		sky2_write32(hw, RB_ADDR(q, RB_RX_UTPP), tp);
		sky2_write32(hw, RB_ADDR(q, RB_RX_LTPP), space/4);
	} else {
		/* Enable store & forward on Tx queue's because
		 * Tx FIFO is only 1K on Yukon
		 */
		sky2_write8(hw, RB_ADDR(q, RB_CTRL), RB_ENA_STFWD);
	}

	sky2_write8(hw, RB_ADDR(q, RB_CTRL), RB_ENA_OP_MD);
	sky2_read8(hw, RB_ADDR(q, RB_CTRL));
}

/* Setup Bus Memory Interface */
static void sky2_qset(struct sky2_hw *hw, u16 q)
{
	sky2_write32(hw, Q_ADDR(q, Q_CSR), BMU_CLR_RESET);
	sky2_write32(hw, Q_ADDR(q, Q_CSR), BMU_OPER_INIT);
	sky2_write32(hw, Q_ADDR(q, Q_CSR), BMU_FIFO_OP_ON);
	sky2_write32(hw, Q_ADDR(q, Q_WM),  BMU_WM_DEFAULT);
}

/* Setup prefetch unit registers. This is the interface between
 * hardware and driver list elements
 */
static void sky2_prefetch_init(struct sky2_hw *hw, u32 qaddr,
				      u64 addr, u32 last)
{
	sky2_write32(hw, Y2_QADDR(qaddr, PREF_UNIT_CTRL), PREF_UNIT_RST_SET);
	sky2_write32(hw, Y2_QADDR(qaddr, PREF_UNIT_CTRL), PREF_UNIT_RST_CLR);
	sky2_write32(hw, Y2_QADDR(qaddr, PREF_UNIT_ADDR_HI), addr >> 32);
	sky2_write32(hw, Y2_QADDR(qaddr, PREF_UNIT_ADDR_LO), (u32) addr);
	sky2_write16(hw, Y2_QADDR(qaddr, PREF_UNIT_LAST_IDX), last);
	sky2_write32(hw, Y2_QADDR(qaddr, PREF_UNIT_CTRL), PREF_UNIT_OP_ON);

	sky2_read32(hw, Y2_QADDR(qaddr, PREF_UNIT_CTRL));
}

static inline struct sky2_tx_le *get_tx_le(struct sky2_port *sky2)
{
	struct sky2_tx_le *le = sky2->tx_le + sky2->tx_prod;

	sky2->tx_prod = (sky2->tx_prod + 1) % TX_RING_SIZE;
	return le;
}

/* Update chip's next pointer */
static inline void sky2_put_idx(struct sky2_hw *hw, unsigned q, u16 idx)
{
	wmb();
	sky2_write16(hw, Y2_QADDR(q, PREF_UNIT_PUT_IDX), idx);
	mmiowb();
}


static inline struct sky2_rx_le *sky2_next_rx(struct sky2_port *sky2)
{
	struct sky2_rx_le *le = sky2->rx_le + sky2->rx_put;
	sky2->rx_put = (sky2->rx_put + 1) % RX_LE_SIZE;
	return le;
}

/* Return high part of DMA address (could be 32 or 64 bit) */
static inline u32 high32(dma_addr_t a)
{
	return sizeof(a) > sizeof(u32) ? (a >> 16) >> 16 : 0;
}

/* Build description to hardware about buffer */
static void sky2_rx_add(struct sky2_port *sky2, dma_addr_t map)
{
	struct sky2_rx_le *le;
	u32 hi = high32(map);
	u16 len = sky2->rx_bufsize;

	if (sky2->rx_addr64 != hi) {
		le = sky2_next_rx(sky2);
		le->addr = cpu_to_le32(hi);
		le->ctrl = 0;
		le->opcode = OP_ADDR64 | HW_OWNER;
		sky2->rx_addr64 = high32(map + len);
	}

	le = sky2_next_rx(sky2);
	le->addr = cpu_to_le32((u32) map);
	le->length = cpu_to_le16(len);
	le->ctrl = 0;
	le->opcode = OP_PACKET | HW_OWNER;
}


/* Tell chip where to start receive checksum.
 * Actually has two checksums, but set both same to avoid possible byte
 * order problems.
 */
static void rx_set_checksum(struct sky2_port *sky2)
{
	struct sky2_rx_le *le;

	le = sky2_next_rx(sky2);
	le->addr = (ETH_HLEN << 16) | ETH_HLEN;
	le->ctrl = 0;
	le->opcode = OP_TCPSTART | HW_OWNER;

	sky2_write32(sky2->hw,
		     Q_ADDR(rxqaddr[sky2->port], Q_CSR),
		     sky2->rx_csum ? BMU_ENA_RX_CHKSUM : BMU_DIS_RX_CHKSUM);

}

/*
 * The RX Stop command will not work for Yukon-2 if the BMU does not
 * reach the end of packet and since we can't make sure that we have
 * incoming data, we must reset the BMU while it is not doing a DMA
 * transfer. Since it is possible that the RX path is still active,
 * the RX RAM buffer will be stopped first, so any possible incoming
 * data will not trigger a DMA. After the RAM buffer is stopped, the
 * BMU is polled until any DMA in progress is ended and only then it
 * will be reset.
 */
static void sky2_rx_stop(struct sky2_port *sky2)
{
	struct sky2_hw *hw = sky2->hw;
	unsigned rxq = rxqaddr[sky2->port];
	int i;

	/* disable the RAM Buffer receive queue */
	sky2_write8(hw, RB_ADDR(rxq, RB_CTRL), RB_DIS_OP_MD);

	for (i = 0; i < 0xffff; i++)
		if (sky2_read8(hw, RB_ADDR(rxq, Q_RSL))
		    == sky2_read8(hw, RB_ADDR(rxq, Q_RL)))
			goto stopped;

	printk(KERN_WARNING PFX "%s: receiver stop failed\n",
	       sky2->netdev->name);
stopped:
	sky2_write32(hw, Q_ADDR(rxq, Q_CSR), BMU_RST_SET | BMU_FIFO_RST);

	/* reset the Rx prefetch unit */
	sky2_write32(hw, Y2_QADDR(rxq, PREF_UNIT_CTRL), PREF_UNIT_RST_SET);
}

/* Clean out receive buffer area, assumes receiver hardware stopped */
static void sky2_rx_clean(struct sky2_port *sky2)
{
	unsigned i;

	memset(sky2->rx_le, 0, RX_LE_BYTES);
	for (i = 0; i < sky2->rx_pending; i++) {
		struct ring_info *re = sky2->rx_ring + i;

		if (re->skb) {
			pci_unmap_single(sky2->hw->pdev,
					 re->mapaddr, sky2->rx_bufsize,
					 PCI_DMA_FROMDEVICE);
			kfree_skb(re->skb);
			re->skb = NULL;
		}
	}
}

/* Basic MII support */
static int sky2_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct mii_ioctl_data *data = if_mii(ifr);
	struct sky2_port *sky2 = netdev_priv(dev);
	struct sky2_hw *hw = sky2->hw;
	int err = -EOPNOTSUPP;

	if (!netif_running(dev))
		return -ENODEV;	/* Phy still in reset */

	switch (cmd) {
	case SIOCGMIIPHY:
		data->phy_id = PHY_ADDR_MARV;

		/* fallthru */
	case SIOCGMIIREG: {
		u16 val = 0;

		spin_lock_bh(&sky2->phy_lock);
		err = __gm_phy_read(hw, sky2->port, data->reg_num & 0x1f, &val);
		spin_unlock_bh(&sky2->phy_lock);

		data->val_out = val;
		break;
	}

	case SIOCSMIIREG:
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;

		spin_lock_bh(&sky2->phy_lock);
		err = gm_phy_write(hw, sky2->port, data->reg_num & 0x1f,
				   data->val_in);
		spin_unlock_bh(&sky2->phy_lock);
		break;
	}
	return err;
}

#ifdef SKY2_VLAN_TAG_USED
static void sky2_vlan_rx_register(struct net_device *dev, struct vlan_group *grp)
{
	struct sky2_port *sky2 = netdev_priv(dev);
	struct sky2_hw *hw = sky2->hw;
	u16 port = sky2->port;

	spin_lock_bh(&sky2->tx_lock);

	sky2_write32(hw, SK_REG(port, RX_GMF_CTRL_T), RX_VLAN_STRIP_ON);
	sky2_write32(hw, SK_REG(port, TX_GMF_CTRL_T), TX_VLAN_TAG_ON);
	sky2->vlgrp = grp;

	spin_unlock_bh(&sky2->tx_lock);
}

static void sky2_vlan_rx_kill_vid(struct net_device *dev, unsigned short vid)
{
	struct sky2_port *sky2 = netdev_priv(dev);
	struct sky2_hw *hw = sky2->hw;
	u16 port = sky2->port;

	spin_lock_bh(&sky2->tx_lock);

	sky2_write32(hw, SK_REG(port, RX_GMF_CTRL_T), RX_VLAN_STRIP_OFF);
	sky2_write32(hw, SK_REG(port, TX_GMF_CTRL_T), TX_VLAN_TAG_OFF);
	if (sky2->vlgrp)
		sky2->vlgrp->vlan_devices[vid] = NULL;

	spin_unlock_bh(&sky2->tx_lock);
}
#endif

/*
 * It appears the hardware has a bug in the FIFO logic that
 * cause it to hang if the FIFO gets overrun and the receive buffer
 * is not aligned. ALso alloc_skb() won't align properly if slab
 * debugging is enabled.
 */
static inline struct sk_buff *sky2_alloc_skb(unsigned int size, gfp_t gfp_mask)
{
	struct sk_buff *skb;

	skb = alloc_skb(size + RX_SKB_ALIGN, gfp_mask);
	if (likely(skb)) {
		unsigned long p	= (unsigned long) skb->data;
		skb_reserve(skb, ALIGN(p, RX_SKB_ALIGN) - p);
	}

	return skb;
}

/*
 * Allocate and setup receiver buffer pool.
 * In case of 64 bit dma, there are 2X as many list elements
 * available as ring entries
 * and need to reserve one list element so we don't wrap around.
 */
static int sky2_rx_start(struct sky2_port *sky2)
{
	struct sky2_hw *hw = sky2->hw;
	unsigned rxq = rxqaddr[sky2->port];
	int i;

	sky2->rx_put = sky2->rx_next = 0;
	sky2_qset(hw, rxq);

	if (hw->chip_id == CHIP_ID_YUKON_EC_U && hw->chip_rev >= 2) {
		/* MAC Rx RAM Read is controlled by hardware */
		sky2_write32(hw, Q_ADDR(rxq, Q_F), F_M_RX_RAM_DIS);
	}

	sky2_prefetch_init(hw, rxq, sky2->rx_le_map, RX_LE_SIZE - 1);

	rx_set_checksum(sky2);
	for (i = 0; i < sky2->rx_pending; i++) {
		struct ring_info *re = sky2->rx_ring + i;

		re->skb = sky2_alloc_skb(sky2->rx_bufsize, GFP_KERNEL);
		if (!re->skb)
			goto nomem;

		re->mapaddr = pci_map_single(hw->pdev, re->skb->data,
					     sky2->rx_bufsize, PCI_DMA_FROMDEVICE);
		sky2_rx_add(sky2, re->mapaddr);
	}

 	/* Truncate oversize frames */
 	sky2_write16(hw, SK_REG(sky2->port, RX_GMF_TR_THR), sky2->rx_bufsize - 8);
 	sky2_write32(hw, SK_REG(sky2->port, RX_GMF_CTRL_T), RX_TRUNC_ON);

	/* Tell chip about available buffers */
	sky2_write16(hw, Y2_QADDR(rxq, PREF_UNIT_PUT_IDX), sky2->rx_put);
	return 0;
nomem:
	sky2_rx_clean(sky2);
	return -ENOMEM;
}

/* Bring up network interface. */
static int sky2_up(struct net_device *dev)
{
	struct sky2_port *sky2 = netdev_priv(dev);
	struct sky2_hw *hw = sky2->hw;
	unsigned port = sky2->port;
	u32 ramsize, rxspace, imask;
	int err = -ENOMEM;

	if (netif_msg_ifup(sky2))
		printk(KERN_INFO PFX "%s: enabling interface\n", dev->name);

	/* must be power of 2 */
	sky2->tx_le = pci_alloc_consistent(hw->pdev,
					   TX_RING_SIZE *
					   sizeof(struct sky2_tx_le),
					   &sky2->tx_le_map);
	if (!sky2->tx_le)
		goto err_out;

	sky2->tx_ring = kcalloc(TX_RING_SIZE, sizeof(struct tx_ring_info),
				GFP_KERNEL);
	if (!sky2->tx_ring)
		goto err_out;
	sky2->tx_prod = sky2->tx_cons = 0;

	sky2->rx_le = pci_alloc_consistent(hw->pdev, RX_LE_BYTES,
					   &sky2->rx_le_map);
	if (!sky2->rx_le)
		goto err_out;
	memset(sky2->rx_le, 0, RX_LE_BYTES);

	sky2->rx_ring = kcalloc(sky2->rx_pending, sizeof(struct ring_info),
				GFP_KERNEL);
	if (!sky2->rx_ring)
		goto err_out;

	sky2_mac_init(hw, port);

	/* Determine available ram buffer space (in 4K blocks).
	 * Note: not sure about the FE setting below yet
	 */
	if (hw->chip_id == CHIP_ID_YUKON_FE)
		ramsize = 4;
	else
		ramsize = sky2_read8(hw, B2_E_0);

	/* Give transmitter one third (rounded up) */
	rxspace = ramsize - (ramsize + 2) / 3;

	sky2_ramset(hw, rxqaddr[port], 0, rxspace);
	sky2_ramset(hw, txqaddr[port], rxspace, ramsize);

	/* Make sure SyncQ is disabled */
	sky2_write8(hw, RB_ADDR(port == 0 ? Q_XS1 : Q_XS2, RB_CTRL),
		    RB_RST_SET);

	sky2_qset(hw, txqaddr[port]);

	/* Set almost empty threshold */
	if (hw->chip_id == CHIP_ID_YUKON_EC_U && hw->chip_rev == 1)
		sky2_write16(hw, Q_ADDR(txqaddr[port], Q_AL), 0x1a0);

	sky2_prefetch_init(hw, txqaddr[port], sky2->tx_le_map,
			   TX_RING_SIZE - 1);

	err = sky2_rx_start(sky2);
	if (err)
		goto err_out;

	/* Enable interrupts from phy/mac for port */
	imask = sky2_read32(hw, B0_IMSK);
	imask |= (port == 0) ? Y2_IS_PORT_1 : Y2_IS_PORT_2;
	sky2_write32(hw, B0_IMSK, imask);

	return 0;

err_out:
	if (sky2->rx_le) {
		pci_free_consistent(hw->pdev, RX_LE_BYTES,
				    sky2->rx_le, sky2->rx_le_map);
		sky2->rx_le = NULL;
	}
	if (sky2->tx_le) {
		pci_free_consistent(hw->pdev,
				    TX_RING_SIZE * sizeof(struct sky2_tx_le),
				    sky2->tx_le, sky2->tx_le_map);
		sky2->tx_le = NULL;
	}
	kfree(sky2->tx_ring);
	kfree(sky2->rx_ring);

	sky2->tx_ring = NULL;
	sky2->rx_ring = NULL;
	return err;
}

/* Modular subtraction in ring */
static inline int tx_dist(unsigned tail, unsigned head)
{
	return (head - tail) % TX_RING_SIZE;
}

/* Number of list elements available for next tx */
static inline int tx_avail(const struct sky2_port *sky2)
{
	return sky2->tx_pending - tx_dist(sky2->tx_cons, sky2->tx_prod);
}

/* Estimate of number of transmit list elements required */
static unsigned tx_le_req(const struct sk_buff *skb)
{
	unsigned count;

	count = sizeof(dma_addr_t) / sizeof(u32);
	count += skb_shinfo(skb)->nr_frags * count;

	if (skb_shinfo(skb)->tso_size)
		++count;

	if (skb->ip_summed == CHECKSUM_HW)
		++count;

	return count;
}

/*
 * Put one packet in ring for transmit.
 * A single packet can generate multiple list elements, and
 * the number of ring elements will probably be less than the number
 * of list elements used.
 *
 * No BH disabling for tx_lock here (like tg3)
 */
static int sky2_xmit_frame(struct sk_buff *skb, struct net_device *dev)
{
	struct sky2_port *sky2 = netdev_priv(dev);
	struct sky2_hw *hw = sky2->hw;
	struct sky2_tx_le *le = NULL;
	struct tx_ring_info *re;
	unsigned i, len;
	int avail;
	dma_addr_t mapping;
	u32 addr64;
	u16 mss;
	u8 ctrl;

	/* No BH disabling for tx_lock here.  We are running in BH disabled
	 * context and TX reclaim runs via poll inside of a software
	 * interrupt, and no related locks in IRQ processing.
	 */
	if (!spin_trylock(&sky2->tx_lock))
		return NETDEV_TX_LOCKED;

	if (unlikely(tx_avail(sky2) < tx_le_req(skb))) {
		/* There is a known but harmless race with lockless tx
		 * and netif_stop_queue.
		 */
		if (!netif_queue_stopped(dev)) {
			netif_stop_queue(dev);
			if (net_ratelimit())
				printk(KERN_WARNING PFX "%s: ring full when queue awake!\n",
				       dev->name);
		}
		spin_unlock(&sky2->tx_lock);

		return NETDEV_TX_BUSY;
	}

	if (unlikely(netif_msg_tx_queued(sky2)))
		printk(KERN_DEBUG "%s: tx queued, slot %u, len %d\n",
		       dev->name, sky2->tx_prod, skb->len);

	len = skb_headlen(skb);
	mapping = pci_map_single(hw->pdev, skb->data, len, PCI_DMA_TODEVICE);
	addr64 = high32(mapping);

	re = sky2->tx_ring + sky2->tx_prod;

	/* Send high bits if changed or crosses boundary */
	if (addr64 != sky2->tx_addr64 || high32(mapping + len) != sky2->tx_addr64) {
		le = get_tx_le(sky2);
		le->tx.addr = cpu_to_le32(addr64);
		le->ctrl = 0;
		le->opcode = OP_ADDR64 | HW_OWNER;
		sky2->tx_addr64 = high32(mapping + len);
	}

	/* Check for TCP Segmentation Offload */
	mss = skb_shinfo(skb)->tso_size;
	if (mss != 0) {
		/* just drop the packet if non-linear expansion fails */
		if (skb_header_cloned(skb) &&
		    pskb_expand_head(skb, 0, 0, GFP_ATOMIC)) {
			dev_kfree_skb(skb);
			goto out_unlock;
		}

		mss += ((skb->h.th->doff - 5) * 4);	/* TCP options */
		mss += (skb->nh.iph->ihl * 4) + sizeof(struct tcphdr);
		mss += ETH_HLEN;
	}

	if (mss != sky2->tx_last_mss) {
		le = get_tx_le(sky2);
		le->tx.tso.size = cpu_to_le16(mss);
		le->tx.tso.rsvd = 0;
		le->opcode = OP_LRGLEN | HW_OWNER;
		le->ctrl = 0;
		sky2->tx_last_mss = mss;
	}

	ctrl = 0;
#ifdef SKY2_VLAN_TAG_USED
	/* Add VLAN tag, can piggyback on LRGLEN or ADDR64 */
	if (sky2->vlgrp && vlan_tx_tag_present(skb)) {
		if (!le) {
			le = get_tx_le(sky2);
			le->tx.addr = 0;
			le->opcode = OP_VLAN|HW_OWNER;
			le->ctrl = 0;
		} else
			le->opcode |= OP_VLAN;
		le->length = cpu_to_be16(vlan_tx_tag_get(skb));
		ctrl |= INS_VLAN;
	}
#endif

	/* Handle TCP checksum offload */
	if (skb->ip_summed == CHECKSUM_HW) {
		u16 hdr = skb->h.raw - skb->data;
		u16 offset = hdr + skb->csum;

		ctrl = CALSUM | WR_SUM | INIT_SUM | LOCK_SUM;
		if (skb->nh.iph->protocol == IPPROTO_UDP)
			ctrl |= UDPTCP;

		le = get_tx_le(sky2);
		le->tx.csum.start = cpu_to_le16(hdr);
		le->tx.csum.offset = cpu_to_le16(offset);
		le->length = 0;	/* initial checksum value */
		le->ctrl = 1;	/* one packet */
		le->opcode = OP_TCPLISW | HW_OWNER;
	}

	le = get_tx_le(sky2);
	le->tx.addr = cpu_to_le32((u32) mapping);
	le->length = cpu_to_le16(len);
	le->ctrl = ctrl;
	le->opcode = mss ? (OP_LARGESEND | HW_OWNER) : (OP_PACKET | HW_OWNER);

	/* Record the transmit mapping info */
	re->skb = skb;
	pci_unmap_addr_set(re, mapaddr, mapping);

	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		skb_frag_t *frag = &skb_shinfo(skb)->frags[i];
		struct tx_ring_info *fre;

		mapping = pci_map_page(hw->pdev, frag->page, frag->page_offset,
				       frag->size, PCI_DMA_TODEVICE);
		addr64 = high32(mapping);
		if (addr64 != sky2->tx_addr64) {
			le = get_tx_le(sky2);
			le->tx.addr = cpu_to_le32(addr64);
			le->ctrl = 0;
			le->opcode = OP_ADDR64 | HW_OWNER;
			sky2->tx_addr64 = addr64;
		}

		le = get_tx_le(sky2);
		le->tx.addr = cpu_to_le32((u32) mapping);
		le->length = cpu_to_le16(frag->size);
		le->ctrl = ctrl;
		le->opcode = OP_BUFFER | HW_OWNER;

		fre = sky2->tx_ring
		    + ((re - sky2->tx_ring) + i + 1) % TX_RING_SIZE;
		pci_unmap_addr_set(fre, mapaddr, mapping);
	}

	re->idx = sky2->tx_prod;
	le->ctrl |= EOP;

	avail = tx_avail(sky2);
	if (mss != 0 || avail < TX_MIN_PENDING) {
 		le->ctrl |= FRC_STAT;
		if (avail <= MAX_SKB_TX_LE)
			netif_stop_queue(dev);
	}

	sky2_put_idx(hw, txqaddr[sky2->port], sky2->tx_prod);

out_unlock:
	spin_unlock(&sky2->tx_lock);

	dev->trans_start = jiffies;
	return NETDEV_TX_OK;
}

/*
 * Free ring elements from starting at tx_cons until "done"
 *
 * NB: the hardware will tell us about partial completion of multi-part
 *     buffers; these are deferred until completion.
 */
static void sky2_tx_complete(struct sky2_port *sky2, u16 done)
{
	struct net_device *dev = sky2->netdev;
	struct pci_dev *pdev = sky2->hw->pdev;
	u16 nxt, put;
	unsigned i;

	BUG_ON(done >= TX_RING_SIZE);

	if (unlikely(netif_msg_tx_done(sky2)))
		printk(KERN_DEBUG "%s: tx done, up to %u\n",
		       dev->name, done);

	for (put = sky2->tx_cons; put != done; put = nxt) {
		struct tx_ring_info *re = sky2->tx_ring + put;
		struct sk_buff *skb = re->skb;

		nxt = re->idx;
		BUG_ON(nxt >= TX_RING_SIZE);
		prefetch(sky2->tx_ring + nxt);

		/* Check for partial status */
		if (tx_dist(put, done) < tx_dist(put, nxt))
			break;

		skb = re->skb;
		pci_unmap_single(pdev, pci_unmap_addr(re, mapaddr),
				 skb_headlen(skb), PCI_DMA_TODEVICE);

		for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
			struct tx_ring_info *fre;
			fre = sky2->tx_ring + (put + i + 1) % TX_RING_SIZE;
			pci_unmap_page(pdev, pci_unmap_addr(fre, mapaddr),
				       skb_shinfo(skb)->frags[i].size,
				       PCI_DMA_TODEVICE);
		}

		dev_kfree_skb(skb);
	}

	sky2->tx_cons = put;
	if (tx_avail(sky2) > MAX_SKB_TX_LE)
		netif_wake_queue(dev);
}

/* Cleanup all untransmitted buffers, assume transmitter not running */
static void sky2_tx_clean(struct sky2_port *sky2)
{
	spin_lock_bh(&sky2->tx_lock);
	sky2_tx_complete(sky2, sky2->tx_prod);
	spin_unlock_bh(&sky2->tx_lock);
}

/* Network shutdown */
static int sky2_down(struct net_device *dev)
{
	struct sky2_port *sky2 = netdev_priv(dev);
	struct sky2_hw *hw = sky2->hw;
	unsigned port = sky2->port;
	u16 ctrl;
	u32 imask;

	/* Never really got started! */
	if (!sky2->tx_le)
		return 0;

	if (netif_msg_ifdown(sky2))
		printk(KERN_INFO PFX "%s: disabling interface\n", dev->name);

	/* Stop more packets from being queued */
	netif_stop_queue(dev);

	sky2_phy_reset(hw, port);

	/* Stop transmitter */
	sky2_write32(hw, Q_ADDR(txqaddr[port], Q_CSR), BMU_STOP);
	sky2_read32(hw, Q_ADDR(txqaddr[port], Q_CSR));

	sky2_write32(hw, RB_ADDR(txqaddr[port], RB_CTRL),
		     RB_RST_SET | RB_DIS_OP_MD);

	ctrl = gma_read16(hw, port, GM_GP_CTRL);
	ctrl &= ~(GM_GPCR_TX_ENA | GM_GPCR_RX_ENA);
	gma_write16(hw, port, GM_GP_CTRL, ctrl);

	sky2_write8(hw, SK_REG(port, GPHY_CTRL), GPC_RST_SET);

	/* Workaround shared GMAC reset */
	if (!(hw->chip_id == CHIP_ID_YUKON_XL && hw->chip_rev == 0
	      && port == 0 && hw->dev[1] && netif_running(hw->dev[1])))
		sky2_write8(hw, SK_REG(port, GMAC_CTRL), GMC_RST_SET);

	/* Disable Force Sync bit and Enable Alloc bit */
	sky2_write8(hw, SK_REG(port, TXA_CTRL),
		    TXA_DIS_FSYNC | TXA_DIS_ALLOC | TXA_STOP_RC);

	/* Stop Interval Timer and Limit Counter of Tx Arbiter */
	sky2_write32(hw, SK_REG(port, TXA_ITI_INI), 0L);
	sky2_write32(hw, SK_REG(port, TXA_LIM_INI), 0L);

	/* Reset the PCI FIFO of the async Tx queue */
	sky2_write32(hw, Q_ADDR(txqaddr[port], Q_CSR),
		     BMU_RST_SET | BMU_FIFO_RST);

	/* Reset the Tx prefetch units */
	sky2_write32(hw, Y2_QADDR(txqaddr[port], PREF_UNIT_CTRL),
		     PREF_UNIT_RST_SET);

	sky2_write32(hw, RB_ADDR(txqaddr[port], RB_CTRL), RB_RST_SET);

	sky2_rx_stop(sky2);

	sky2_write8(hw, SK_REG(port, RX_GMF_CTRL_T), GMF_RST_SET);
	sky2_write8(hw, SK_REG(port, TX_GMF_CTRL_T), GMF_RST_SET);

	/* Disable port IRQ */
	imask = sky2_read32(hw, B0_IMSK);
	imask &= ~(sky2->port == 0) ? Y2_IS_PORT_1 : Y2_IS_PORT_2;
	sky2_write32(hw, B0_IMSK, imask);

	/* turn off LED's */
	sky2_write16(hw, B0_Y2LED, LED_STAT_OFF);

	synchronize_irq(hw->pdev->irq);

	sky2_tx_clean(sky2);
	sky2_rx_clean(sky2);

	pci_free_consistent(hw->pdev, RX_LE_BYTES,
			    sky2->rx_le, sky2->rx_le_map);
	kfree(sky2->rx_ring);

	pci_free_consistent(hw->pdev,
			    TX_RING_SIZE * sizeof(struct sky2_tx_le),
			    sky2->tx_le, sky2->tx_le_map);
	kfree(sky2->tx_ring);

	sky2->tx_le = NULL;
	sky2->rx_le = NULL;

	sky2->rx_ring = NULL;
	sky2->tx_ring = NULL;

	return 0;
}

static u16 sky2_phy_speed(const struct sky2_hw *hw, u16 aux)
{
	if (!hw->copper)
		return SPEED_1000;

	if (hw->chip_id == CHIP_ID_YUKON_FE)
		return (aux & PHY_M_PS_SPEED_100) ? SPEED_100 : SPEED_10;

	switch (aux & PHY_M_PS_SPEED_MSK) {
	case PHY_M_PS_SPEED_1000:
		return SPEED_1000;
	case PHY_M_PS_SPEED_100:
		return SPEED_100;
	default:
		return SPEED_10;
	}
}

static void sky2_link_up(struct sky2_port *sky2)
{
	struct sky2_hw *hw = sky2->hw;
	unsigned port = sky2->port;
	u16 reg;

	/* Enable Transmit FIFO Underrun */
	sky2_write8(hw, SK_REG(port, GMAC_IRQ_MSK), GMAC_DEF_MSK);

	reg = gma_read16(hw, port, GM_GP_CTRL);
	if (sky2->autoneg == AUTONEG_DISABLE) {
		reg |= GM_GPCR_AU_ALL_DIS;

		/* Is write/read necessary?  Copied from sky2_mac_init */
		gma_write16(hw, port, GM_GP_CTRL, reg);
		gma_read16(hw, port, GM_GP_CTRL);

		switch (sky2->speed) {
		case SPEED_1000:
			reg &= ~GM_GPCR_SPEED_100;
			reg |= GM_GPCR_SPEED_1000;
			break;
		case SPEED_100:
			reg &= ~GM_GPCR_SPEED_1000;
			reg |= GM_GPCR_SPEED_100;
			break;
		case SPEED_10:
			reg &= ~(GM_GPCR_SPEED_1000 | GM_GPCR_SPEED_100);
			break;
		}
	} else
		reg &= ~GM_GPCR_AU_ALL_DIS;

	if (sky2->duplex == DUPLEX_FULL || sky2->autoneg == AUTONEG_ENABLE)
		reg |= GM_GPCR_DUP_FULL;

	/* enable Rx/Tx */
	reg |= GM_GPCR_RX_ENA | GM_GPCR_TX_ENA;
	gma_write16(hw, port, GM_GP_CTRL, reg);
	gma_read16(hw, port, GM_GP_CTRL);

	gm_phy_write(hw, port, PHY_MARV_INT_MASK, PHY_M_DEF_MSK);

	netif_carrier_on(sky2->netdev);
	netif_wake_queue(sky2->netdev);

	/* Turn on link LED */
	sky2_write8(hw, SK_REG(port, LNK_LED_REG),
		    LINKLED_ON | LINKLED_BLINK_OFF | LINKLED_LINKSYNC_OFF);

	if (hw->chip_id == CHIP_ID_YUKON_XL) {
		u16 pg = gm_phy_read(hw, port, PHY_MARV_EXT_ADR);

		gm_phy_write(hw, port, PHY_MARV_EXT_ADR, 3);
		gm_phy_write(hw, port, PHY_MARV_PHY_CTRL, PHY_M_LEDC_LOS_CTRL(1) |	/* LINK/ACT */
			     PHY_M_LEDC_INIT_CTRL(sky2->speed ==
						  SPEED_10 ? 7 : 0) |
			     PHY_M_LEDC_STA1_CTRL(sky2->speed ==
						  SPEED_100 ? 7 : 0) |
			     PHY_M_LEDC_STA0_CTRL(sky2->speed ==
						  SPEED_1000 ? 7 : 0));
		gm_phy_write(hw, port, PHY_MARV_EXT_ADR, pg);
	}

	if (netif_msg_link(sky2))
		printk(KERN_INFO PFX
		       "%s: Link is up at %d Mbps, %s duplex, flow control %s\n",
		       sky2->netdev->name, sky2->speed,
		       sky2->duplex == DUPLEX_FULL ? "full" : "half",
		       (sky2->tx_pause && sky2->rx_pause) ? "both" :
		       sky2->tx_pause ? "tx" : sky2->rx_pause ? "rx" : "none");
}

static void sky2_link_down(struct sky2_port *sky2)
{
	struct sky2_hw *hw = sky2->hw;
	unsigned port = sky2->port;
	u16 reg;

	gm_phy_write(hw, port, PHY_MARV_INT_MASK, 0);

	reg = gma_read16(hw, port, GM_GP_CTRL);
	reg &= ~(GM_GPCR_RX_ENA | GM_GPCR_TX_ENA);
	gma_write16(hw, port, GM_GP_CTRL, reg);
	gma_read16(hw, port, GM_GP_CTRL);	/* PCI post */

	if (sky2->rx_pause && !sky2->tx_pause) {
		/* restore Asymmetric Pause bit */
		gm_phy_write(hw, port, PHY_MARV_AUNE_ADV,
			     gm_phy_read(hw, port, PHY_MARV_AUNE_ADV)
			     | PHY_M_AN_ASP);
	}

	netif_carrier_off(sky2->netdev);
	netif_stop_queue(sky2->netdev);

	/* Turn on link LED */
	sky2_write8(hw, SK_REG(port, LNK_LED_REG), LINKLED_OFF);

	if (netif_msg_link(sky2))
		printk(KERN_INFO PFX "%s: Link is down.\n", sky2->netdev->name);
	sky2_phy_init(hw, port);
}

static int sky2_autoneg_done(struct sky2_port *sky2, u16 aux)
{
	struct sky2_hw *hw = sky2->hw;
	unsigned port = sky2->port;
	u16 lpa;

	lpa = gm_phy_read(hw, port, PHY_MARV_AUNE_LP);

	if (lpa & PHY_M_AN_RF) {
		printk(KERN_ERR PFX "%s: remote fault", sky2->netdev->name);
		return -1;
	}

	if (hw->chip_id != CHIP_ID_YUKON_FE &&
	    gm_phy_read(hw, port, PHY_MARV_1000T_STAT) & PHY_B_1000S_MSF) {
		printk(KERN_ERR PFX "%s: master/slave fault",
		       sky2->netdev->name);
		return -1;
	}

	if (!(aux & PHY_M_PS_SPDUP_RES)) {
		printk(KERN_ERR PFX "%s: speed/duplex mismatch",
		       sky2->netdev->name);
		return -1;
	}

	sky2->duplex = (aux & PHY_M_PS_FULL_DUP) ? DUPLEX_FULL : DUPLEX_HALF;

	sky2->speed = sky2_phy_speed(hw, aux);

	/* Pause bits are offset (9..8) */
	if (hw->chip_id == CHIP_ID_YUKON_XL)
		aux >>= 6;

	sky2->rx_pause = (aux & PHY_M_PS_RX_P_EN) != 0;
	sky2->tx_pause = (aux & PHY_M_PS_TX_P_EN) != 0;

	if ((sky2->tx_pause || sky2->rx_pause)
	    && !(sky2->speed < SPEED_1000 && sky2->duplex == DUPLEX_HALF))
		sky2_write8(hw, SK_REG(port, GMAC_CTRL), GMC_PAUSE_ON);
	else
		sky2_write8(hw, SK_REG(port, GMAC_CTRL), GMC_PAUSE_OFF);

	return 0;
}

/* Interrupt from PHY */
static void sky2_phy_intr(struct sky2_hw *hw, unsigned port)
{
	struct net_device *dev = hw->dev[port];
	struct sky2_port *sky2 = netdev_priv(dev);
	u16 istatus, phystat;

	spin_lock(&sky2->phy_lock);
	istatus = gm_phy_read(hw, port, PHY_MARV_INT_STAT);
	phystat = gm_phy_read(hw, port, PHY_MARV_PHY_STAT);

	if (!netif_running(dev))
		goto out;

	if (netif_msg_intr(sky2))
		printk(KERN_INFO PFX "%s: phy interrupt status 0x%x 0x%x\n",
		       sky2->netdev->name, istatus, phystat);

	if (istatus & PHY_M_IS_AN_COMPL) {
		if (sky2_autoneg_done(sky2, phystat) == 0)
			sky2_link_up(sky2);
		goto out;
	}

	if (istatus & PHY_M_IS_LSP_CHANGE)
		sky2->speed = sky2_phy_speed(hw, phystat);

	if (istatus & PHY_M_IS_DUP_CHANGE)
		sky2->duplex =
		    (phystat & PHY_M_PS_FULL_DUP) ? DUPLEX_FULL : DUPLEX_HALF;

	if (istatus & PHY_M_IS_LST_CHANGE) {
		if (phystat & PHY_M_PS_LINK_UP)
			sky2_link_up(sky2);
		else
			sky2_link_down(sky2);
	}
out:
	spin_unlock(&sky2->phy_lock);
}


/* Transmit timeout is only called if we are running, carries is up
 * and tx queue is full (stopped).
 */
static void sky2_tx_timeout(struct net_device *dev)
{
	struct sky2_port *sky2 = netdev_priv(dev);
	struct sky2_hw *hw = sky2->hw;
	unsigned txq = txqaddr[sky2->port];
	u16 report, done;

	if (netif_msg_timer(sky2))
		printk(KERN_ERR PFX "%s: tx timeout\n", dev->name);

	report = sky2_read16(hw, sky2->port == 0 ? STAT_TXA1_RIDX : STAT_TXA2_RIDX);
	done = sky2_read16(hw, Q_ADDR(txq, Q_DONE));

	printk(KERN_DEBUG PFX "%s: transmit ring %u .. %u report=%u done=%u\n",
	       dev->name,
	       sky2->tx_cons, sky2->tx_prod, report, done);

	if (report != done) {
		printk(KERN_INFO PFX "status burst pending (irq moderation?)\n");

		sky2_write8(hw, STAT_TX_TIMER_CTRL, TIM_STOP);
		sky2_write8(hw, STAT_TX_TIMER_CTRL, TIM_START);
	} else if (report != sky2->tx_cons) {
		printk(KERN_INFO PFX "status report lost?\n");

		spin_lock_bh(&sky2->tx_lock);
		sky2_tx_complete(sky2, report);
		spin_unlock_bh(&sky2->tx_lock);
	} else {
		printk(KERN_INFO PFX "hardware hung? flushing\n");

		sky2_write32(hw, Q_ADDR(txq, Q_CSR), BMU_STOP);
		sky2_write32(hw, Y2_QADDR(txq, PREF_UNIT_CTRL), PREF_UNIT_RST_SET);

		sky2_tx_clean(sky2);

		sky2_qset(hw, txq);
		sky2_prefetch_init(hw, txq, sky2->tx_le_map, TX_RING_SIZE - 1);
	}
}


/* Want receive buffer size to be multiple of 64 bits
 * and incl room for vlan and truncation
 */
static inline unsigned sky2_buf_size(int mtu)
{
	return ALIGN(mtu + ETH_HLEN + VLAN_HLEN, 8) + 8;
}

static int sky2_change_mtu(struct net_device *dev, int new_mtu)
{
	struct sky2_port *sky2 = netdev_priv(dev);
	struct sky2_hw *hw = sky2->hw;
	int err;
	u16 ctl, mode;
	u32 imask;

	if (new_mtu < ETH_ZLEN || new_mtu > ETH_JUMBO_MTU)
		return -EINVAL;

	if (hw->chip_id == CHIP_ID_YUKON_EC_U && new_mtu > ETH_DATA_LEN)
		return -EINVAL;

	if (!netif_running(dev)) {
		dev->mtu = new_mtu;
		return 0;
	}

	imask = sky2_read32(hw, B0_IMSK);
	sky2_write32(hw, B0_IMSK, 0);

	dev->trans_start = jiffies;	/* prevent tx timeout */
	netif_stop_queue(dev);
	netif_poll_disable(hw->dev[0]);

	synchronize_irq(hw->pdev->irq);

	ctl = gma_read16(hw, sky2->port, GM_GP_CTRL);
	gma_write16(hw, sky2->port, GM_GP_CTRL, ctl & ~GM_GPCR_RX_ENA);
	sky2_rx_stop(sky2);
	sky2_rx_clean(sky2);

	dev->mtu = new_mtu;
	sky2->rx_bufsize = sky2_buf_size(new_mtu);
	mode = DATA_BLIND_VAL(DATA_BLIND_DEF) |
		GM_SMOD_VLAN_ENA | IPG_DATA_VAL(IPG_DATA_DEF);

	if (dev->mtu > ETH_DATA_LEN)
		mode |= GM_SMOD_JUMBO_ENA;

	gma_write16(hw, sky2->port, GM_SERIAL_MODE, mode);

	sky2_write8(hw, RB_ADDR(rxqaddr[sky2->port], RB_CTRL), RB_ENA_OP_MD);

	err = sky2_rx_start(sky2);
	sky2_write32(hw, B0_IMSK, imask);

	if (err)
		dev_close(dev);
	else {
		gma_write16(hw, sky2->port, GM_GP_CTRL, ctl);

		netif_poll_enable(hw->dev[0]);
		netif_wake_queue(dev);
	}

	return err;
}

/*
 * Receive one packet.
 * For small packets or errors, just reuse existing skb.
 * For larger packets, get new buffer.
 */
static struct sk_buff *sky2_receive(struct sky2_port *sky2,
				    u16 length, u32 status)
{
	struct ring_info *re = sky2->rx_ring + sky2->rx_next;
	struct sk_buff *skb = NULL;

	if (unlikely(netif_msg_rx_status(sky2)))
		printk(KERN_DEBUG PFX "%s: rx slot %u status 0x%x len %d\n",
		       sky2->netdev->name, sky2->rx_next, status, length);

	sky2->rx_next = (sky2->rx_next + 1) % sky2->rx_pending;
	prefetch(sky2->rx_ring + sky2->rx_next);

	if (status & GMR_FS_ANY_ERR)
		goto error;

	if (!(status & GMR_FS_RX_OK))
		goto resubmit;

	if (length > sky2->netdev->mtu + ETH_HLEN)
		goto oversize;

	if (length < copybreak) {
		skb = alloc_skb(length + 2, GFP_ATOMIC);
		if (!skb)
			goto resubmit;

		skb_reserve(skb, 2);
		pci_dma_sync_single_for_cpu(sky2->hw->pdev, re->mapaddr,
					    length, PCI_DMA_FROMDEVICE);
		memcpy(skb->data, re->skb->data, length);
		skb->ip_summed = re->skb->ip_summed;
		skb->csum = re->skb->csum;
		pci_dma_sync_single_for_device(sky2->hw->pdev, re->mapaddr,
					       length, PCI_DMA_FROMDEVICE);
	} else {
		struct sk_buff *nskb;

		nskb = sky2_alloc_skb(sky2->rx_bufsize, GFP_ATOMIC);
		if (!nskb)
			goto resubmit;

		skb = re->skb;
		re->skb = nskb;
		pci_unmap_single(sky2->hw->pdev, re->mapaddr,
				 sky2->rx_bufsize, PCI_DMA_FROMDEVICE);
		prefetch(skb->data);

		re->mapaddr = pci_map_single(sky2->hw->pdev, nskb->data,
					     sky2->rx_bufsize, PCI_DMA_FROMDEVICE);
	}

	skb_put(skb, length);
resubmit:
	re->skb->ip_summed = CHECKSUM_NONE;
	sky2_rx_add(sky2, re->mapaddr);

	/* Tell receiver about new buffers. */
	sky2_put_idx(sky2->hw, rxqaddr[sky2->port], sky2->rx_put);

	return skb;

oversize:
	++sky2->net_stats.rx_over_errors;
	goto resubmit;

error:
	++sky2->net_stats.rx_errors;

	if (netif_msg_rx_err(sky2) && net_ratelimit())
		printk(KERN_INFO PFX "%s: rx error, status 0x%x length %d\n",
		       sky2->netdev->name, status, length);

	if (status & (GMR_FS_LONG_ERR | GMR_FS_UN_SIZE))
		sky2->net_stats.rx_length_errors++;
	if (status & GMR_FS_FRAGMENT)
		sky2->net_stats.rx_frame_errors++;
	if (status & GMR_FS_CRC_ERR)
		sky2->net_stats.rx_crc_errors++;
	if (status & GMR_FS_RX_FF_OV)
		sky2->net_stats.rx_fifo_errors++;

	goto resubmit;
}

/* Transmit complete */
static inline void sky2_tx_done(struct net_device *dev, u16 last)
{
	struct sky2_port *sky2 = netdev_priv(dev);

	if (netif_running(dev)) {
		spin_lock(&sky2->tx_lock);
		sky2_tx_complete(sky2, last);
		spin_unlock(&sky2->tx_lock);
	}
}

/* Process status response ring */
static int sky2_status_intr(struct sky2_hw *hw, int to_do)
{
	int work_done = 0;

	rmb();

	for(;;) {
		struct sky2_status_le *le  = hw->st_le + hw->st_idx;
		struct net_device *dev;
		struct sky2_port *sky2;
		struct sk_buff *skb;
		u32 status;
		u16 length;
		u8  link, opcode;

		opcode = le->opcode;
		if (!opcode)
			break;
		opcode &= ~HW_OWNER;

		hw->st_idx = (hw->st_idx + 1) % STATUS_RING_SIZE;
		le->opcode = 0;

		link = le->link;
		BUG_ON(link >= 2);
		dev = hw->dev[link];

		sky2 = netdev_priv(dev);
		length = le->length;
		status = le->status;

		switch (opcode) {
		case OP_RXSTAT:
			skb = sky2_receive(sky2, length, status);
			if (!skb)
				break;

			skb->dev = dev;
			skb->protocol = eth_type_trans(skb, dev);
			dev->last_rx = jiffies;

#ifdef SKY2_VLAN_TAG_USED
			if (sky2->vlgrp && (status & GMR_FS_VLAN)) {
				vlan_hwaccel_receive_skb(skb,
							 sky2->vlgrp,
							 be16_to_cpu(sky2->rx_tag));
			} else
#endif
				netif_receive_skb(skb);

			if (++work_done >= to_do)
				goto exit_loop;
			break;

#ifdef SKY2_VLAN_TAG_USED
		case OP_RXVLAN:
			sky2->rx_tag = length;
			break;

		case OP_RXCHKSVLAN:
			sky2->rx_tag = length;
			/* fall through */
#endif
		case OP_RXCHKS:
			skb = sky2->rx_ring[sky2->rx_next].skb;
			skb->ip_summed = CHECKSUM_HW;
			skb->csum = le16_to_cpu(status);
			break;

		case OP_TXINDEXLE:
			/* TX index reports status for both ports */
			sky2_tx_done(hw->dev[0], status & 0xffff);
			if (hw->dev[1])
				sky2_tx_done(hw->dev[1],
				     ((status >> 24) & 0xff)
					     | (u16)(length & 0xf) << 8);
			break;

		default:
			if (net_ratelimit())
				printk(KERN_WARNING PFX
				       "unknown status opcode 0x%x\n", opcode);
			break;
		}
	}

exit_loop:
	return work_done;
}

static void sky2_hw_error(struct sky2_hw *hw, unsigned port, u32 status)
{
	struct net_device *dev = hw->dev[port];

	if (net_ratelimit())
		printk(KERN_INFO PFX "%s: hw error interrupt status 0x%x\n",
		       dev->name, status);

	if (status & Y2_IS_PAR_RD1) {
		if (net_ratelimit())
			printk(KERN_ERR PFX "%s: ram data read parity error\n",
			       dev->name);
		/* Clear IRQ */
		sky2_write16(hw, RAM_BUFFER(port, B3_RI_CTRL), RI_CLR_RD_PERR);
	}

	if (status & Y2_IS_PAR_WR1) {
		if (net_ratelimit())
			printk(KERN_ERR PFX "%s: ram data write parity error\n",
			       dev->name);

		sky2_write16(hw, RAM_BUFFER(port, B3_RI_CTRL), RI_CLR_WR_PERR);
	}

	if (status & Y2_IS_PAR_MAC1) {
		if (net_ratelimit())
			printk(KERN_ERR PFX "%s: MAC parity error\n", dev->name);
		sky2_write8(hw, SK_REG(port, TX_GMF_CTRL_T), GMF_CLI_TX_PE);
	}

	if (status & Y2_IS_PAR_RX1) {
		if (net_ratelimit())
			printk(KERN_ERR PFX "%s: RX parity error\n", dev->name);
		sky2_write32(hw, Q_ADDR(rxqaddr[port], Q_CSR), BMU_CLR_IRQ_PAR);
	}

	if (status & Y2_IS_TCP_TXA1) {
		if (net_ratelimit())
			printk(KERN_ERR PFX "%s: TCP segmentation error\n",
			       dev->name);
		sky2_write32(hw, Q_ADDR(txqaddr[port], Q_CSR), BMU_CLR_IRQ_TCP);
	}
}

static void sky2_hw_intr(struct sky2_hw *hw)
{
	u32 status = sky2_read32(hw, B0_HWE_ISRC);

	if (status & Y2_IS_TIST_OV)
		sky2_write8(hw, GMAC_TI_ST_CTRL, GMT_ST_CLR_IRQ);

	if (status & (Y2_IS_MST_ERR | Y2_IS_IRQ_STAT)) {
		u16 pci_err;

		pci_err = sky2_pci_read16(hw, PCI_STATUS);
		if (net_ratelimit())
			printk(KERN_ERR PFX "%s: pci hw error (0x%x)\n",
			       pci_name(hw->pdev), pci_err);

		sky2_write8(hw, B2_TST_CTRL1, TST_CFG_WRITE_ON);
		sky2_pci_write16(hw, PCI_STATUS,
				      pci_err | PCI_STATUS_ERROR_BITS);
		sky2_write8(hw, B2_TST_CTRL1, TST_CFG_WRITE_OFF);
	}

	if (status & Y2_IS_PCI_EXP) {
		/* PCI-Express uncorrectable Error occurred */
		u32 pex_err;

		pex_err = sky2_pci_read32(hw, PEX_UNC_ERR_STAT);

		if (net_ratelimit())
			printk(KERN_ERR PFX "%s: pci express error (0x%x)\n",
			       pci_name(hw->pdev), pex_err);

		/* clear the interrupt */
		sky2_write32(hw, B2_TST_CTRL1, TST_CFG_WRITE_ON);
		sky2_pci_write32(hw, PEX_UNC_ERR_STAT,
				       0xffffffffUL);
		sky2_write32(hw, B2_TST_CTRL1, TST_CFG_WRITE_OFF);

		if (pex_err & PEX_FATAL_ERRORS) {
			u32 hwmsk = sky2_read32(hw, B0_HWE_IMSK);
			hwmsk &= ~Y2_IS_PCI_EXP;
			sky2_write32(hw, B0_HWE_IMSK, hwmsk);
		}
	}

	if (status & Y2_HWE_L1_MASK)
		sky2_hw_error(hw, 0, status);
	status >>= 8;
	if (status & Y2_HWE_L1_MASK)
		sky2_hw_error(hw, 1, status);
}

static void sky2_mac_intr(struct sky2_hw *hw, unsigned port)
{
	struct net_device *dev = hw->dev[port];
	struct sky2_port *sky2 = netdev_priv(dev);
	u8 status = sky2_read8(hw, SK_REG(port, GMAC_IRQ_SRC));

	if (netif_msg_intr(sky2))
		printk(KERN_INFO PFX "%s: mac interrupt status 0x%x\n",
		       dev->name, status);

	if (status & GM_IS_RX_FF_OR) {
		++sky2->net_stats.rx_fifo_errors;
		sky2_write8(hw, SK_REG(port, RX_GMF_CTRL_T), GMF_CLI_RX_FO);
	}

	if (status & GM_IS_TX_FF_UR) {
		++sky2->net_stats.tx_fifo_errors;
		sky2_write8(hw, SK_REG(port, TX_GMF_CTRL_T), GMF_CLI_TX_FU);
	}
}

/* This should never happen it is a fatal situation */
static void sky2_descriptor_error(struct sky2_hw *hw, unsigned port,
				  const char *rxtx, u32 mask)
{
	struct net_device *dev = hw->dev[port];
	struct sky2_port *sky2 = netdev_priv(dev);
	u32 imask;

	printk(KERN_ERR PFX "%s: %s descriptor error (hardware problem)\n",
	       dev ? dev->name : "<not registered>", rxtx);

	imask = sky2_read32(hw, B0_IMSK);
	imask &= ~mask;
	sky2_write32(hw, B0_IMSK, imask);

	if (dev) {
		spin_lock(&sky2->phy_lock);
		sky2_link_down(sky2);
		spin_unlock(&sky2->phy_lock);
	}
}

/* If idle then force a fake soft NAPI poll once a second
 * to work around cases where sharing an edge triggered interrupt.
 */
static void sky2_idle(unsigned long arg)
{
	struct net_device *dev = (struct net_device *) arg;

	local_irq_disable();
	if (__netif_rx_schedule_prep(dev))
		__netif_rx_schedule(dev);
	local_irq_enable();
}


static int sky2_poll(struct net_device *dev0, int *budget)
{
	struct sky2_hw *hw = ((struct sky2_port *) netdev_priv(dev0))->hw;
	int work_limit = min(dev0->quota, *budget);
	int work_done = 0;
	u32 status = sky2_read32(hw, B0_Y2_SP_EISR);

	if (status & Y2_IS_HW_ERR)
		sky2_hw_intr(hw);

	if (status & Y2_IS_IRQ_PHY1)
		sky2_phy_intr(hw, 0);

	if (status & Y2_IS_IRQ_PHY2)
		sky2_phy_intr(hw, 1);

	if (status & Y2_IS_IRQ_MAC1)
		sky2_mac_intr(hw, 0);

	if (status & Y2_IS_IRQ_MAC2)
		sky2_mac_intr(hw, 1);

	if (status & Y2_IS_CHK_RX1)
		sky2_descriptor_error(hw, 0, "receive", Y2_IS_CHK_RX1);

	if (status & Y2_IS_CHK_RX2)
		sky2_descriptor_error(hw, 1, "receive", Y2_IS_CHK_RX2);

	if (status & Y2_IS_CHK_TXA1)
		sky2_descriptor_error(hw, 0, "transmit", Y2_IS_CHK_TXA1);

	if (status & Y2_IS_CHK_TXA2)
		sky2_descriptor_error(hw, 1, "transmit", Y2_IS_CHK_TXA2);

	if (status & Y2_IS_STAT_BMU)
		sky2_write32(hw, STAT_CTRL, SC_STAT_CLR_IRQ);

	work_done = sky2_status_intr(hw, work_limit);
	*budget -= work_done;
	dev0->quota -= work_done;

	if (work_done >= work_limit)
		return 1;

	mod_timer(&hw->idle_timer, jiffies + HZ);

	netif_rx_complete(dev0);

	status = sky2_read32(hw, B0_Y2_SP_LISR);
	return 0;
}

static irqreturn_t sky2_intr(int irq, void *dev_id, struct pt_regs *regs)
{
	struct sky2_hw *hw = dev_id;
	struct net_device *dev0 = hw->dev[0];
	u32 status;

	/* Reading this mask interrupts as side effect */
	status = sky2_read32(hw, B0_Y2_SP_ISRC2);
	if (status == 0 || status == ~0)
		return IRQ_NONE;

	prefetch(&hw->st_le[hw->st_idx]);
	if (likely(__netif_rx_schedule_prep(dev0)))
		__netif_rx_schedule(dev0);
	else
		printk(KERN_DEBUG PFX "irq race detected\n");

	return IRQ_HANDLED;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void sky2_netpoll(struct net_device *dev)
{
	struct sky2_port *sky2 = netdev_priv(dev);

	sky2_intr(sky2->hw->pdev->irq, sky2->hw, NULL);
}
#endif

/* Chip internal frequency for clock calculations */
static inline u32 sky2_mhz(const struct sky2_hw *hw)
{
	switch (hw->chip_id) {
	case CHIP_ID_YUKON_EC:
	case CHIP_ID_YUKON_EC_U:
		return 125;	/* 125 Mhz */
	case CHIP_ID_YUKON_FE:
		return 100;	/* 100 Mhz */
	default:		/* YUKON_XL */
		return 156;	/* 156 Mhz */
	}
}

static inline u32 sky2_us2clk(const struct sky2_hw *hw, u32 us)
{
	return sky2_mhz(hw) * us;
}

static inline u32 sky2_clk2us(const struct sky2_hw *hw, u32 clk)
{
	return clk / sky2_mhz(hw);
}


static int __devinit sky2_reset(struct sky2_hw *hw)
{
	u16 status;
	u8 t8, pmd_type;
	int i;

	sky2_write8(hw, B0_CTST, CS_RST_CLR);

	hw->chip_id = sky2_read8(hw, B2_CHIP_ID);
	if (hw->chip_id < CHIP_ID_YUKON_XL || hw->chip_id > CHIP_ID_YUKON_FE) {
		printk(KERN_ERR PFX "%s: unsupported chip type 0x%x\n",
		       pci_name(hw->pdev), hw->chip_id);
		return -EOPNOTSUPP;
	}

	hw->chip_rev = (sky2_read8(hw, B2_MAC_CFG) & CFG_CHIP_R_MSK) >> 4;

	/* This rev is really old, and requires untested workarounds */
	if (hw->chip_id == CHIP_ID_YUKON_EC && hw->chip_rev == CHIP_REV_YU_EC_A1) {
		printk(KERN_ERR PFX "%s: unsupported revision Yukon-%s (0x%x) rev %d\n",
		       pci_name(hw->pdev), yukon2_name[hw->chip_id - CHIP_ID_YUKON_XL],
		       hw->chip_id, hw->chip_rev);
		return -EOPNOTSUPP;
	}

	/* This chip is new and not tested yet */
	if (hw->chip_id == CHIP_ID_YUKON_EC_U) {
		pr_info(PFX "%s: is a version of Yukon 2 chipset that has not been tested yet.\n",
			pci_name(hw->pdev));
		pr_info("Please report success/failure to maintainer <shemminger@osdl.org>\n");
	}

	/* disable ASF */
	if (hw->chip_id <= CHIP_ID_YUKON_EC) {
		sky2_write8(hw, B28_Y2_ASF_STAT_CMD, Y2_ASF_RESET);
		sky2_write16(hw, B0_CTST, Y2_ASF_DISABLE);
	}

	/* do a SW reset */
	sky2_write8(hw, B0_CTST, CS_RST_SET);
	sky2_write8(hw, B0_CTST, CS_RST_CLR);

	/* clear PCI errors, if any */
	status = sky2_pci_read16(hw, PCI_STATUS);

	sky2_write8(hw, B2_TST_CTRL1, TST_CFG_WRITE_ON);
	sky2_pci_write16(hw, PCI_STATUS, status | PCI_STATUS_ERROR_BITS);


	sky2_write8(hw, B0_CTST, CS_MRST_CLR);

	/* clear any PEX errors */
	if (pci_find_capability(hw->pdev, PCI_CAP_ID_EXP))
		sky2_pci_write32(hw, PEX_UNC_ERR_STAT, 0xffffffffUL);


	pmd_type = sky2_read8(hw, B2_PMD_TYP);
	hw->copper = !(pmd_type == 'L' || pmd_type == 'S');

	hw->ports = 1;
	t8 = sky2_read8(hw, B2_Y2_HW_RES);
	if ((t8 & CFG_DUAL_MAC_MSK) == CFG_DUAL_MAC_MSK) {
		if (!(sky2_read8(hw, B2_Y2_CLK_GATE) & Y2_STATUS_LNK2_INAC))
			++hw->ports;
	}

	sky2_set_power_state(hw, PCI_D0);

	for (i = 0; i < hw->ports; i++) {
		sky2_write8(hw, SK_REG(i, GMAC_LINK_CTRL), GMLC_RST_SET);
		sky2_write8(hw, SK_REG(i, GMAC_LINK_CTRL), GMLC_RST_CLR);
	}

	sky2_write8(hw, B2_TST_CTRL1, TST_CFG_WRITE_OFF);

	/* Clear I2C IRQ noise */
	sky2_write32(hw, B2_I2C_IRQ, 1);

	/* turn off hardware timer (unused) */
	sky2_write8(hw, B2_TI_CTRL, TIM_STOP);
	sky2_write8(hw, B2_TI_CTRL, TIM_CLR_IRQ);

	sky2_write8(hw, B0_Y2LED, LED_STAT_ON);

	/* Turn off descriptor polling */
	sky2_write32(hw, B28_DPT_CTRL, DPT_STOP);

	/* Turn off receive timestamp */
	sky2_write8(hw, GMAC_TI_ST_CTRL, GMT_ST_STOP);
	sky2_write8(hw, GMAC_TI_ST_CTRL, GMT_ST_CLR_IRQ);

	/* enable the Tx Arbiters */
	for (i = 0; i < hw->ports; i++)
		sky2_write8(hw, SK_REG(i, TXA_CTRL), TXA_ENA_ARB);

	/* Initialize ram interface */
	for (i = 0; i < hw->ports; i++) {
		sky2_write8(hw, RAM_BUFFER(i, B3_RI_CTRL), RI_RST_CLR);

		sky2_write8(hw, RAM_BUFFER(i, B3_RI_WTO_R1), SK_RI_TO_53);
		sky2_write8(hw, RAM_BUFFER(i, B3_RI_WTO_XA1), SK_RI_TO_53);
		sky2_write8(hw, RAM_BUFFER(i, B3_RI_WTO_XS1), SK_RI_TO_53);
		sky2_write8(hw, RAM_BUFFER(i, B3_RI_RTO_R1), SK_RI_TO_53);
		sky2_write8(hw, RAM_BUFFER(i, B3_RI_RTO_XA1), SK_RI_TO_53);
		sky2_write8(hw, RAM_BUFFER(i, B3_RI_RTO_XS1), SK_RI_TO_53);
		sky2_write8(hw, RAM_BUFFER(i, B3_RI_WTO_R2), SK_RI_TO_53);
		sky2_write8(hw, RAM_BUFFER(i, B3_RI_WTO_XA2), SK_RI_TO_53);
		sky2_write8(hw, RAM_BUFFER(i, B3_RI_WTO_XS2), SK_RI_TO_53);
		sky2_write8(hw, RAM_BUFFER(i, B3_RI_RTO_R2), SK_RI_TO_53);
		sky2_write8(hw, RAM_BUFFER(i, B3_RI_RTO_XA2), SK_RI_TO_53);
		sky2_write8(hw, RAM_BUFFER(i, B3_RI_RTO_XS2), SK_RI_TO_53);
	}

	sky2_write32(hw, B0_HWE_IMSK, Y2_HWE_ALL_MASK);

	for (i = 0; i < hw->ports; i++)
		sky2_phy_reset(hw, i);

	memset(hw->st_le, 0, STATUS_LE_BYTES);
	hw->st_idx = 0;

	sky2_write32(hw, STAT_CTRL, SC_STAT_RST_SET);
	sky2_write32(hw, STAT_CTRL, SC_STAT_RST_CLR);

	sky2_write32(hw, STAT_LIST_ADDR_LO, hw->st_dma);
	sky2_write32(hw, STAT_LIST_ADDR_HI, (u64) hw->st_dma >> 32);

	/* Set the list last index */
	sky2_write16(hw, STAT_LAST_IDX, STATUS_RING_SIZE - 1);

	sky2_write16(hw, STAT_TX_IDX_TH, 10);
	sky2_write8(hw, STAT_FIFO_WM, 16);

	/* set Status-FIFO ISR watermark */
	if (hw->chip_id == CHIP_ID_YUKON_XL && hw->chip_rev == 0)
		sky2_write8(hw, STAT_FIFO_ISR_WM, 4);
	else
		sky2_write8(hw, STAT_FIFO_ISR_WM, 16);

	sky2_write32(hw, STAT_TX_TIMER_INI, sky2_us2clk(hw, 1000));
	sky2_write32(hw, STAT_ISR_TIMER_INI, sky2_us2clk(hw, 20));
	sky2_write32(hw, STAT_LEV_TIMER_INI, sky2_us2clk(hw, 100));

	/* enable status unit */
	sky2_write32(hw, STAT_CTRL, SC_STAT_OP_ON);

	sky2_write8(hw, STAT_TX_TIMER_CTRL, TIM_START);
	sky2_write8(hw, STAT_LEV_TIMER_CTRL, TIM_START);
	sky2_write8(hw, STAT_ISR_TIMER_CTRL, TIM_START);

	return 0;
}

static u32 sky2_supported_modes(const struct sky2_hw *hw)
{
	u32 modes;
	if (hw->copper) {
		modes = SUPPORTED_10baseT_Half
		    | SUPPORTED_10baseT_Full
		    | SUPPORTED_100baseT_Half
		    | SUPPORTED_100baseT_Full
		    | SUPPORTED_Autoneg | SUPPORTED_TP;

		if (hw->chip_id != CHIP_ID_YUKON_FE)
			modes |= SUPPORTED_1000baseT_Half
			    | SUPPORTED_1000baseT_Full;
	} else
		modes = SUPPORTED_1000baseT_Full | SUPPORTED_FIBRE
		    | SUPPORTED_Autoneg;
	return modes;
}

static int sky2_get_settings(struct net_device *dev, struct ethtool_cmd *ecmd)
{
	struct sky2_port *sky2 = netdev_priv(dev);
	struct sky2_hw *hw = sky2->hw;

	ecmd->transceiver = XCVR_INTERNAL;
	ecmd->supported = sky2_supported_modes(hw);
	ecmd->phy_address = PHY_ADDR_MARV;
	if (hw->copper) {
		ecmd->supported = SUPPORTED_10baseT_Half
		    | SUPPORTED_10baseT_Full
		    | SUPPORTED_100baseT_Half
		    | SUPPORTED_100baseT_Full
		    | SUPPORTED_1000baseT_Half
		    | SUPPORTED_1000baseT_Full
		    | SUPPORTED_Autoneg | SUPPORTED_TP;
		ecmd->port = PORT_TP;
	} else
		ecmd->port = PORT_FIBRE;

	ecmd->advertising = sky2->advertising;
	ecmd->autoneg = sky2->autoneg;
	ecmd->speed = sky2->speed;
	ecmd->duplex = sky2->duplex;
	return 0;
}

static int sky2_set_settings(struct net_device *dev, struct ethtool_cmd *ecmd)
{
	struct sky2_port *sky2 = netdev_priv(dev);
	const struct sky2_hw *hw = sky2->hw;
	u32 supported = sky2_supported_modes(hw);

	if (ecmd->autoneg == AUTONEG_ENABLE) {
		ecmd->advertising = supported;
		sky2->duplex = -1;
		sky2->speed = -1;
	} else {
		u32 setting;

		switch (ecmd->speed) {
		case SPEED_1000:
			if (ecmd->duplex == DUPLEX_FULL)
				setting = SUPPORTED_1000baseT_Full;
			else if (ecmd->duplex == DUPLEX_HALF)
				setting = SUPPORTED_1000baseT_Half;
			else
				return -EINVAL;
			break;
		case SPEED_100:
			if (ecmd->duplex == DUPLEX_FULL)
				setting = SUPPORTED_100baseT_Full;
			else if (ecmd->duplex == DUPLEX_HALF)
				setting = SUPPORTED_100baseT_Half;
			else
				return -EINVAL;
			break;

		case SPEED_10:
			if (ecmd->duplex == DUPLEX_FULL)
				setting = SUPPORTED_10baseT_Full;
			else if (ecmd->duplex == DUPLEX_HALF)
				setting = SUPPORTED_10baseT_Half;
			else
				return -EINVAL;
			break;
		default:
			return -EINVAL;
		}

		if ((setting & supported) == 0)
			return -EINVAL;

		sky2->speed = ecmd->speed;
		sky2->duplex = ecmd->duplex;
	}

	sky2->autoneg = ecmd->autoneg;
	sky2->advertising = ecmd->advertising;

	if (netif_running(dev))
		sky2_phy_reinit(sky2);

	return 0;
}

static void sky2_get_drvinfo(struct net_device *dev,
			     struct ethtool_drvinfo *info)
{
	struct sky2_port *sky2 = netdev_priv(dev);

	strcpy(info->driver, DRV_NAME);
	strcpy(info->version, DRV_VERSION);
	strcpy(info->fw_version, "N/A");
	strcpy(info->bus_info, pci_name(sky2->hw->pdev));
}

static const struct sky2_stat {
	char name[ETH_GSTRING_LEN];
	u16 offset;
} sky2_stats[] = {
	{ "tx_bytes",	   GM_TXO_OK_HI },
	{ "rx_bytes",	   GM_RXO_OK_HI },
	{ "tx_broadcast",  GM_TXF_BC_OK },
	{ "rx_broadcast",  GM_RXF_BC_OK },
	{ "tx_multicast",  GM_TXF_MC_OK },
	{ "rx_multicast",  GM_RXF_MC_OK },
	{ "tx_unicast",    GM_TXF_UC_OK },
	{ "rx_unicast",    GM_RXF_UC_OK },
	{ "tx_mac_pause",  GM_TXF_MPAUSE },
	{ "rx_mac_pause",  GM_RXF_MPAUSE },
	{ "collisions",    GM_TXF_COL },
	{ "late_collision",GM_TXF_LAT_COL },
	{ "aborted", 	   GM_TXF_ABO_COL },
	{ "single_collisions", GM_TXF_SNG_COL },
	{ "multi_collisions", GM_TXF_MUL_COL },

	{ "rx_short",      GM_RXF_SHT },
	{ "rx_runt", 	   GM_RXE_FRAG },
	{ "rx_64_byte_packets", GM_RXF_64B },
	{ "rx_65_to_127_byte_packets", GM_RXF_127B },
	{ "rx_128_to_255_byte_packets", GM_RXF_255B },
	{ "rx_256_to_511_byte_packets", GM_RXF_511B },
	{ "rx_512_to_1023_byte_packets", GM_RXF_1023B },
	{ "rx_1024_to_1518_byte_packets", GM_RXF_1518B },
	{ "rx_1518_to_max_byte_packets", GM_RXF_MAX_SZ },
	{ "rx_too_long",   GM_RXF_LNG_ERR },
	{ "rx_fifo_overflow", GM_RXE_FIFO_OV },
	{ "rx_jabber",     GM_RXF_JAB_PKT },
	{ "rx_fcs_error",   GM_RXF_FCS_ERR },

	{ "tx_64_byte_packets", GM_TXF_64B },
	{ "tx_65_to_127_byte_packets", GM_TXF_127B },
	{ "tx_128_to_255_byte_packets", GM_TXF_255B },
	{ "tx_256_to_511_byte_packets", GM_TXF_511B },
	{ "tx_512_to_1023_byte_packets", GM_TXF_1023B },
	{ "tx_1024_to_1518_byte_packets", GM_TXF_1518B },
	{ "tx_1519_to_max_byte_packets", GM_TXF_MAX_SZ },
	{ "tx_fifo_underrun", GM_TXE_FIFO_UR },
};

static u32 sky2_get_rx_csum(struct net_device *dev)
{
	struct sky2_port *sky2 = netdev_priv(dev);

	return sky2->rx_csum;
}

static int sky2_set_rx_csum(struct net_device *dev, u32 data)
{
	struct sky2_port *sky2 = netdev_priv(dev);

	sky2->rx_csum = data;

	sky2_write32(sky2->hw, Q_ADDR(rxqaddr[sky2->port], Q_CSR),
		     data ? BMU_ENA_RX_CHKSUM : BMU_DIS_RX_CHKSUM);

	return 0;
}

static u32 sky2_get_msglevel(struct net_device *netdev)
{
	struct sky2_port *sky2 = netdev_priv(netdev);
	return sky2->msg_enable;
}

static int sky2_nway_reset(struct net_device *dev)
{
	struct sky2_port *sky2 = netdev_priv(dev);

	if (sky2->autoneg != AUTONEG_ENABLE)
		return -EINVAL;

	sky2_phy_reinit(sky2);

	return 0;
}

static void sky2_phy_stats(struct sky2_port *sky2, u64 * data, unsigned count)
{
	struct sky2_hw *hw = sky2->hw;
	unsigned port = sky2->port;
	int i;

	data[0] = (u64) gma_read32(hw, port, GM_TXO_OK_HI) << 32
	    | (u64) gma_read32(hw, port, GM_TXO_OK_LO);
	data[1] = (u64) gma_read32(hw, port, GM_RXO_OK_HI) << 32
	    | (u64) gma_read32(hw, port, GM_RXO_OK_LO);

	for (i = 2; i < count; i++)
		data[i] = (u64) gma_read32(hw, port, sky2_stats[i].offset);
}

static void sky2_set_msglevel(struct net_device *netdev, u32 value)
{
	struct sky2_port *sky2 = netdev_priv(netdev);
	sky2->msg_enable = value;
}

static int sky2_get_stats_count(struct net_device *dev)
{
	return ARRAY_SIZE(sky2_stats);
}

static void sky2_get_ethtool_stats(struct net_device *dev,
				   struct ethtool_stats *stats, u64 * data)
{
	struct sky2_port *sky2 = netdev_priv(dev);

	sky2_phy_stats(sky2, data, ARRAY_SIZE(sky2_stats));
}

static void sky2_get_strings(struct net_device *dev, u32 stringset, u8 * data)
{
	int i;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < ARRAY_SIZE(sky2_stats); i++)
			memcpy(data + i * ETH_GSTRING_LEN,
			       sky2_stats[i].name, ETH_GSTRING_LEN);
		break;
	}
}

/* Use hardware MIB variables for critical path statistics and
 * transmit feedback not reported at interrupt.
 * Other errors are accounted for in interrupt handler.
 */
static struct net_device_stats *sky2_get_stats(struct net_device *dev)
{
	struct sky2_port *sky2 = netdev_priv(dev);
	u64 data[13];

	sky2_phy_stats(sky2, data, ARRAY_SIZE(data));

	sky2->net_stats.tx_bytes = data[0];
	sky2->net_stats.rx_bytes = data[1];
	sky2->net_stats.tx_packets = data[2] + data[4] + data[6];
	sky2->net_stats.rx_packets = data[3] + data[5] + data[7];
	sky2->net_stats.multicast = data[3] + data[5];
	sky2->net_stats.collisions = data[10];
	sky2->net_stats.tx_aborted_errors = data[12];

	return &sky2->net_stats;
}

static int sky2_set_mac_address(struct net_device *dev, void *p)
{
	struct sky2_port *sky2 = netdev_priv(dev);
	struct sky2_hw *hw = sky2->hw;
	unsigned port = sky2->port;
	const struct sockaddr *addr = p;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	memcpy(dev->dev_addr, addr->sa_data, ETH_ALEN);
	memcpy_toio(hw->regs + B2_MAC_1 + port * 8,
		    dev->dev_addr, ETH_ALEN);
	memcpy_toio(hw->regs + B2_MAC_2 + port * 8,
		    dev->dev_addr, ETH_ALEN);

	/* virtual address for data */
	gma_set_addr(hw, port, GM_SRC_ADDR_2L, dev->dev_addr);

	/* physical address: used for pause frames */
	gma_set_addr(hw, port, GM_SRC_ADDR_1L, dev->dev_addr);

	return 0;
}

static void sky2_set_multicast(struct net_device *dev)
{
	struct sky2_port *sky2 = netdev_priv(dev);
	struct sky2_hw *hw = sky2->hw;
	unsigned port = sky2->port;
	struct dev_mc_list *list = dev->mc_list;
	u16 reg;
	u8 filter[8];

	memset(filter, 0, sizeof(filter));

	reg = gma_read16(hw, port, GM_RX_CTRL);
	reg |= GM_RXCR_UCF_ENA;

	if (dev->flags & IFF_PROMISC)	/* promiscuous */
		reg &= ~(GM_RXCR_UCF_ENA | GM_RXCR_MCF_ENA);
	else if ((dev->flags & IFF_ALLMULTI) || dev->mc_count > 16)	/* all multicast */
		memset(filter, 0xff, sizeof(filter));
	else if (dev->mc_count == 0)	/* no multicast */
		reg &= ~GM_RXCR_MCF_ENA;
	else {
		int i;
		reg |= GM_RXCR_MCF_ENA;

		for (i = 0; list && i < dev->mc_count; i++, list = list->next) {
			u32 bit = ether_crc(ETH_ALEN, list->dmi_addr) & 0x3f;
			filter[bit / 8] |= 1 << (bit % 8);
		}
	}

	gma_write16(hw, port, GM_MC_ADDR_H1,
		    (u16) filter[0] | ((u16) filter[1] << 8));
	gma_write16(hw, port, GM_MC_ADDR_H2,
		    (u16) filter[2] | ((u16) filter[3] << 8));
	gma_write16(hw, port, GM_MC_ADDR_H3,
		    (u16) filter[4] | ((u16) filter[5] << 8));
	gma_write16(hw, port, GM_MC_ADDR_H4,
		    (u16) filter[6] | ((u16) filter[7] << 8));

	gma_write16(hw, port, GM_RX_CTRL, reg);
}

/* Can have one global because blinking is controlled by
 * ethtool and that is always under RTNL mutex
 */
static void sky2_led(struct sky2_hw *hw, unsigned port, int on)
{
	u16 pg;

	switch (hw->chip_id) {
	case CHIP_ID_YUKON_XL:
		pg = gm_phy_read(hw, port, PHY_MARV_EXT_ADR);
		gm_phy_write(hw, port, PHY_MARV_EXT_ADR, 3);
		gm_phy_write(hw, port, PHY_MARV_PHY_CTRL,
			     on ? (PHY_M_LEDC_LOS_CTRL(1) |
				   PHY_M_LEDC_INIT_CTRL(7) |
				   PHY_M_LEDC_STA1_CTRL(7) |
				   PHY_M_LEDC_STA0_CTRL(7))
			     : 0);

		gm_phy_write(hw, port, PHY_MARV_EXT_ADR, pg);
		break;

	default:
		gm_phy_write(hw, port, PHY_MARV_LED_CTRL, 0);
		gm_phy_write(hw, port, PHY_MARV_LED_OVER,
			     on ? PHY_M_LED_MO_DUP(MO_LED_ON) |
			     PHY_M_LED_MO_10(MO_LED_ON) |
			     PHY_M_LED_MO_100(MO_LED_ON) |
			     PHY_M_LED_MO_1000(MO_LED_ON) |
			     PHY_M_LED_MO_RX(MO_LED_ON)
			     : PHY_M_LED_MO_DUP(MO_LED_OFF) |
			     PHY_M_LED_MO_10(MO_LED_OFF) |
			     PHY_M_LED_MO_100(MO_LED_OFF) |
			     PHY_M_LED_MO_1000(MO_LED_OFF) |
			     PHY_M_LED_MO_RX(MO_LED_OFF));

	}
}

/* blink LED's for finding board */
static int sky2_phys_id(struct net_device *dev, u32 data)
{
	struct sky2_port *sky2 = netdev_priv(dev);
	struct sky2_hw *hw = sky2->hw;
	unsigned port = sky2->port;
	u16 ledctrl, ledover = 0;
	long ms;
	int interrupted;
	int onoff = 1;

	if (!data || data > (u32) (MAX_SCHEDULE_TIMEOUT / HZ))
		ms = jiffies_to_msecs(MAX_SCHEDULE_TIMEOUT);
	else
		ms = data * 1000;

	/* save initial values */
	spin_lock_bh(&sky2->phy_lock);
	if (hw->chip_id == CHIP_ID_YUKON_XL) {
		u16 pg = gm_phy_read(hw, port, PHY_MARV_EXT_ADR);
		gm_phy_write(hw, port, PHY_MARV_EXT_ADR, 3);
		ledctrl = gm_phy_read(hw, port, PHY_MARV_PHY_CTRL);
		gm_phy_write(hw, port, PHY_MARV_EXT_ADR, pg);
	} else {
		ledctrl = gm_phy_read(hw, port, PHY_MARV_LED_CTRL);
		ledover = gm_phy_read(hw, port, PHY_MARV_LED_OVER);
	}

	interrupted = 0;
	while (!interrupted && ms > 0) {
		sky2_led(hw, port, onoff);
		onoff = !onoff;

		spin_unlock_bh(&sky2->phy_lock);
		interrupted = msleep_interruptible(250);
		spin_lock_bh(&sky2->phy_lock);

		ms -= 250;
	}

	/* resume regularly scheduled programming */
	if (hw->chip_id == CHIP_ID_YUKON_XL) {
		u16 pg = gm_phy_read(hw, port, PHY_MARV_EXT_ADR);
		gm_phy_write(hw, port, PHY_MARV_EXT_ADR, 3);
		gm_phy_write(hw, port, PHY_MARV_PHY_CTRL, ledctrl);
		gm_phy_write(hw, port, PHY_MARV_EXT_ADR, pg);
	} else {
		gm_phy_write(hw, port, PHY_MARV_LED_CTRL, ledctrl);
		gm_phy_write(hw, port, PHY_MARV_LED_OVER, ledover);
	}
	spin_unlock_bh(&sky2->phy_lock);

	return 0;
}

static void sky2_get_pauseparam(struct net_device *dev,
				struct ethtool_pauseparam *ecmd)
{
	struct sky2_port *sky2 = netdev_priv(dev);

	ecmd->tx_pause = sky2->tx_pause;
	ecmd->rx_pause = sky2->rx_pause;
	ecmd->autoneg = sky2->autoneg;
}

static int sky2_set_pauseparam(struct net_device *dev,
			       struct ethtool_pauseparam *ecmd)
{
	struct sky2_port *sky2 = netdev_priv(dev);
	int err = 0;

	sky2->autoneg = ecmd->autoneg;
	sky2->tx_pause = ecmd->tx_pause != 0;
	sky2->rx_pause = ecmd->rx_pause != 0;

	sky2_phy_reinit(sky2);

	return err;
}

static int sky2_get_coalesce(struct net_device *dev,
			     struct ethtool_coalesce *ecmd)
{
	struct sky2_port *sky2 = netdev_priv(dev);
	struct sky2_hw *hw = sky2->hw;

	if (sky2_read8(hw, STAT_TX_TIMER_CTRL) == TIM_STOP)
		ecmd->tx_coalesce_usecs = 0;
	else {
		u32 clks = sky2_read32(hw, STAT_TX_TIMER_INI);
		ecmd->tx_coalesce_usecs = sky2_clk2us(hw, clks);
	}
	ecmd->tx_max_coalesced_frames = sky2_read16(hw, STAT_TX_IDX_TH);

	if (sky2_read8(hw, STAT_LEV_TIMER_CTRL) == TIM_STOP)
		ecmd->rx_coalesce_usecs = 0;
	else {
		u32 clks = sky2_read32(hw, STAT_LEV_TIMER_INI);
		ecmd->rx_coalesce_usecs = sky2_clk2us(hw, clks);
	}
	ecmd->rx_max_coalesced_frames = sky2_read8(hw, STAT_FIFO_WM);

	if (sky2_read8(hw, STAT_ISR_TIMER_CTRL) == TIM_STOP)
		ecmd->rx_coalesce_usecs_irq = 0;
	else {
		u32 clks = sky2_read32(hw, STAT_ISR_TIMER_INI);
		ecmd->rx_coalesce_usecs_irq = sky2_clk2us(hw, clks);
	}

	ecmd->rx_max_coalesced_frames_irq = sky2_read8(hw, STAT_FIFO_ISR_WM);

	return 0;
}

/* Note: this affect both ports */
static int sky2_set_coalesce(struct net_device *dev,
			     struct ethtool_coalesce *ecmd)
{
	struct sky2_port *sky2 = netdev_priv(dev);
	struct sky2_hw *hw = sky2->hw;
	const u32 tmax = sky2_clk2us(hw, 0x0ffffff);

	if (ecmd->tx_coalesce_usecs > tmax ||
	    ecmd->rx_coalesce_usecs > tmax ||
	    ecmd->rx_coalesce_usecs_irq > tmax)
		return -EINVAL;

	if (ecmd->tx_max_coalesced_frames >= TX_RING_SIZE-1)
		return -EINVAL;
	if (ecmd->rx_max_coalesced_frames > RX_MAX_PENDING)
		return -EINVAL;
	if (ecmd->rx_max_coalesced_frames_irq >RX_MAX_PENDING)
		return -EINVAL;

	if (ecmd->tx_coalesce_usecs == 0)
		sky2_write8(hw, STAT_TX_TIMER_CTRL, TIM_STOP);
	else {
		sky2_write32(hw, STAT_TX_TIMER_INI,
			     sky2_us2clk(hw, ecmd->tx_coalesce_usecs));
		sky2_write8(hw, STAT_TX_TIMER_CTRL, TIM_START);
	}
	sky2_write16(hw, STAT_TX_IDX_TH, ecmd->tx_max_coalesced_frames);

	if (ecmd->rx_coalesce_usecs == 0)
		sky2_write8(hw, STAT_LEV_TIMER_CTRL, TIM_STOP);
	else {
		sky2_write32(hw, STAT_LEV_TIMER_INI,
			     sky2_us2clk(hw, ecmd->rx_coalesce_usecs));
		sky2_write8(hw, STAT_LEV_TIMER_CTRL, TIM_START);
	}
	sky2_write8(hw, STAT_FIFO_WM, ecmd->rx_max_coalesced_frames);

	if (ecmd->rx_coalesce_usecs_irq == 0)
		sky2_write8(hw, STAT_ISR_TIMER_CTRL, TIM_STOP);
	else {
		sky2_write32(hw, STAT_ISR_TIMER_INI,
			     sky2_us2clk(hw, ecmd->rx_coalesce_usecs_irq));
		sky2_write8(hw, STAT_ISR_TIMER_CTRL, TIM_START);
	}
	sky2_write8(hw, STAT_FIFO_ISR_WM, ecmd->rx_max_coalesced_frames_irq);
	return 0;
}

static void sky2_get_ringparam(struct net_device *dev,
			       struct ethtool_ringparam *ering)
{
	struct sky2_port *sky2 = netdev_priv(dev);

	ering->rx_max_pending = RX_MAX_PENDING;
	ering->rx_mini_max_pending = 0;
	ering->rx_jumbo_max_pending = 0;
	ering->tx_max_pending = TX_RING_SIZE - 1;

	ering->rx_pending = sky2->rx_pending;
	ering->rx_mini_pending = 0;
	ering->rx_jumbo_pending = 0;
	ering->tx_pending = sky2->tx_pending;
}

static int sky2_set_ringparam(struct net_device *dev,
			      struct ethtool_ringparam *ering)
{
	struct sky2_port *sky2 = netdev_priv(dev);
	int err = 0;

	if (ering->rx_pending > RX_MAX_PENDING ||
	    ering->rx_pending < 8 ||
	    ering->tx_pending < MAX_SKB_TX_LE ||
	    ering->tx_pending > TX_RING_SIZE - 1)
		return -EINVAL;

	if (netif_running(dev))
		sky2_down(dev);

	sky2->rx_pending = ering->rx_pending;
	sky2->tx_pending = ering->tx_pending;

	if (netif_running(dev)) {
		err = sky2_up(dev);
		if (err)
			dev_close(dev);
		else
			sky2_set_multicast(dev);
	}

	return err;
}

static int sky2_get_regs_len(struct net_device *dev)
{
	return 0x4000;
}

/*
 * Returns copy of control register region
 * Note: access to the RAM address register set will cause timeouts.
 */
static void sky2_get_regs(struct net_device *dev, struct ethtool_regs *regs,
			  void *p)
{
	const struct sky2_port *sky2 = netdev_priv(dev);
	const void __iomem *io = sky2->hw->regs;

	BUG_ON(regs->len < B3_RI_WTO_R1);
	regs->version = 1;
	memset(p, 0, regs->len);

	memcpy_fromio(p, io, B3_RAM_ADDR);

	memcpy_fromio(p + B3_RI_WTO_R1,
		      io + B3_RI_WTO_R1,
		      regs->len - B3_RI_WTO_R1);
}

static struct ethtool_ops sky2_ethtool_ops = {
	.get_settings = sky2_get_settings,
	.set_settings = sky2_set_settings,
	.get_drvinfo = sky2_get_drvinfo,
	.get_msglevel = sky2_get_msglevel,
	.set_msglevel = sky2_set_msglevel,
	.nway_reset   = sky2_nway_reset,
	.get_regs_len = sky2_get_regs_len,
	.get_regs = sky2_get_regs,
	.get_link = ethtool_op_get_link,
	.get_sg = ethtool_op_get_sg,
	.set_sg = ethtool_op_set_sg,
	.get_tx_csum = ethtool_op_get_tx_csum,
	.set_tx_csum = ethtool_op_set_tx_csum,
	.get_tso = ethtool_op_get_tso,
	.set_tso = ethtool_op_set_tso,
	.get_rx_csum = sky2_get_rx_csum,
	.set_rx_csum = sky2_set_rx_csum,
	.get_strings = sky2_get_strings,
	.get_coalesce = sky2_get_coalesce,
	.set_coalesce = sky2_set_coalesce,
	.get_ringparam = sky2_get_ringparam,
	.set_ringparam = sky2_set_ringparam,
	.get_pauseparam = sky2_get_pauseparam,
	.set_pauseparam = sky2_set_pauseparam,
	.phys_id = sky2_phys_id,
	.get_stats_count = sky2_get_stats_count,
	.get_ethtool_stats = sky2_get_ethtool_stats,
	.get_perm_addr	= ethtool_op_get_perm_addr,
};

/* Initialize network device */
static __devinit struct net_device *sky2_init_netdev(struct sky2_hw *hw,
						     unsigned port, int highmem)
{
	struct sky2_port *sky2;
	struct net_device *dev = alloc_etherdev(sizeof(*sky2));

	if (!dev) {
		printk(KERN_ERR "sky2 etherdev alloc failed");
		return NULL;
	}

	SET_MODULE_OWNER(dev);
	SET_NETDEV_DEV(dev, &hw->pdev->dev);
	dev->irq = hw->pdev->irq;
	dev->open = sky2_up;
	dev->stop = sky2_down;
	dev->do_ioctl = sky2_ioctl;
	dev->hard_start_xmit = sky2_xmit_frame;
	dev->get_stats = sky2_get_stats;
	dev->set_multicast_list = sky2_set_multicast;
	dev->set_mac_address = sky2_set_mac_address;
	dev->change_mtu = sky2_change_mtu;
	SET_ETHTOOL_OPS(dev, &sky2_ethtool_ops);
	dev->tx_timeout = sky2_tx_timeout;
	dev->watchdog_timeo = TX_WATCHDOG;
	if (port == 0)
		dev->poll = sky2_poll;
	dev->weight = NAPI_WEIGHT;
#ifdef CONFIG_NET_POLL_CONTROLLER
	dev->poll_controller = sky2_netpoll;
#endif

	sky2 = netdev_priv(dev);
	sky2->netdev = dev;
	sky2->hw = hw;
	sky2->msg_enable = netif_msg_init(debug, default_msg);

	spin_lock_init(&sky2->tx_lock);
	/* Auto speed and flow control */
	sky2->autoneg = AUTONEG_ENABLE;
	sky2->tx_pause = 1;
	sky2->rx_pause = 1;
	sky2->duplex = -1;
	sky2->speed = -1;
	sky2->advertising = sky2_supported_modes(hw);

	/* Receive checksum disabled for Yukon XL
	 * because of observed problems with incorrect
	 * values when multiple packets are received in one interrupt
	 */
	sky2->rx_csum = (hw->chip_id != CHIP_ID_YUKON_XL);

	spin_lock_init(&sky2->phy_lock);
	sky2->tx_pending = TX_DEF_PENDING;
	sky2->rx_pending = RX_DEF_PENDING;
	sky2->rx_bufsize = sky2_buf_size(ETH_DATA_LEN);

	hw->dev[port] = dev;

	sky2->port = port;

	dev->features |= NETIF_F_LLTX;
	if (hw->chip_id != CHIP_ID_YUKON_EC_U)
		dev->features |= NETIF_F_TSO;
	if (highmem)
		dev->features |= NETIF_F_HIGHDMA;
	dev->features |= NETIF_F_IP_CSUM | NETIF_F_SG;

#ifdef SKY2_VLAN_TAG_USED
	dev->features |= NETIF_F_HW_VLAN_TX | NETIF_F_HW_VLAN_RX;
	dev->vlan_rx_register = sky2_vlan_rx_register;
	dev->vlan_rx_kill_vid = sky2_vlan_rx_kill_vid;
#endif

	/* read the mac address */
	memcpy_fromio(dev->dev_addr, hw->regs + B2_MAC_1 + port * 8, ETH_ALEN);
	memcpy(dev->perm_addr, dev->dev_addr, dev->addr_len);

	/* device is off until link detection */
	netif_carrier_off(dev);
	netif_stop_queue(dev);

	return dev;
}

static void __devinit sky2_show_addr(struct net_device *dev)
{
	const struct sky2_port *sky2 = netdev_priv(dev);

	if (netif_msg_probe(sky2))
		printk(KERN_INFO PFX "%s: addr %02x:%02x:%02x:%02x:%02x:%02x\n",
		       dev->name,
		       dev->dev_addr[0], dev->dev_addr[1], dev->dev_addr[2],
		       dev->dev_addr[3], dev->dev_addr[4], dev->dev_addr[5]);
}

/* Handle software interrupt used during MSI test */
static irqreturn_t __devinit sky2_test_intr(int irq, void *dev_id,
					    struct pt_regs *regs)
{
	struct sky2_hw *hw = dev_id;
	u32 status = sky2_read32(hw, B0_Y2_SP_ISRC2);

	if (status == 0)
		return IRQ_NONE;

	if (status & Y2_IS_IRQ_SW) {
		hw->msi_detected = 1;
		wake_up(&hw->msi_wait);
		sky2_write8(hw, B0_CTST, CS_CL_SW_IRQ);
	}
	sky2_write32(hw, B0_Y2_SP_ICR, 2);

	return IRQ_HANDLED;
}

/* Test interrupt path by forcing a a software IRQ */
static int __devinit sky2_test_msi(struct sky2_hw *hw)
{
	struct pci_dev *pdev = hw->pdev;
	int err;

	sky2_write32(hw, B0_IMSK, Y2_IS_IRQ_SW);

	err = request_irq(pdev->irq, sky2_test_intr, SA_SHIRQ, DRV_NAME, hw);
	if (err) {
		printk(KERN_ERR PFX "%s: cannot assign irq %d\n",
		       pci_name(pdev), pdev->irq);
		return err;
	}

	init_waitqueue_head (&hw->msi_wait);

	sky2_write8(hw, B0_CTST, CS_ST_SW_IRQ);
	wmb();

	wait_event_timeout(hw->msi_wait, hw->msi_detected, HZ/10);

	if (!hw->msi_detected) {
		/* MSI test failed, go back to INTx mode */
		printk(KERN_WARNING PFX "%s: No interrupt was generated using MSI, "
		       "switching to INTx mode. Please report this failure to "
		       "the PCI maintainer and include system chipset information.\n",
		       pci_name(pdev));

		err = -EOPNOTSUPP;
		sky2_write8(hw, B0_CTST, CS_CL_SW_IRQ);
	}

	sky2_write32(hw, B0_IMSK, 0);

	free_irq(pdev->irq, hw);

	return err;
}

static int __devinit sky2_probe(struct pci_dev *pdev,
				const struct pci_device_id *ent)
{
	struct net_device *dev, *dev1 = NULL;
	struct sky2_hw *hw;
	int err, pm_cap, using_dac = 0;

	err = pci_enable_device(pdev);
	if (err) {
		printk(KERN_ERR PFX "%s cannot enable PCI device\n",
		       pci_name(pdev));
		goto err_out;
	}

	err = pci_request_regions(pdev, DRV_NAME);
	if (err) {
		printk(KERN_ERR PFX "%s cannot obtain PCI resources\n",
		       pci_name(pdev));
		goto err_out;
	}

	pci_set_master(pdev);

	/* Find power-management capability. */
	pm_cap = pci_find_capability(pdev, PCI_CAP_ID_PM);
	if (pm_cap == 0) {
		printk(KERN_ERR PFX "Cannot find PowerManagement capability, "
		       "aborting.\n");
		err = -EIO;
		goto err_out_free_regions;
	}

	if (sizeof(dma_addr_t) > sizeof(u32) &&
	    !(err = pci_set_dma_mask(pdev, DMA_64BIT_MASK))) {
		using_dac = 1;
		err = pci_set_consistent_dma_mask(pdev, DMA_64BIT_MASK);
		if (err < 0) {
			printk(KERN_ERR PFX "%s unable to obtain 64 bit DMA "
			       "for consistent allocations\n", pci_name(pdev));
			goto err_out_free_regions;
		}

	} else {
		err = pci_set_dma_mask(pdev, DMA_32BIT_MASK);
		if (err) {
			printk(KERN_ERR PFX "%s no usable DMA configuration\n",
			       pci_name(pdev));
			goto err_out_free_regions;
		}
	}

	err = -ENOMEM;
	hw = kzalloc(sizeof(*hw), GFP_KERNEL);
	if (!hw) {
		printk(KERN_ERR PFX "%s: cannot allocate hardware struct\n",
		       pci_name(pdev));
		goto err_out_free_regions;
	}

	hw->pdev = pdev;

	hw->regs = ioremap_nocache(pci_resource_start(pdev, 0), 0x4000);
	if (!hw->regs) {
		printk(KERN_ERR PFX "%s: cannot map device registers\n",
		       pci_name(pdev));
		goto err_out_free_hw;
	}
	hw->pm_cap = pm_cap;

#ifdef __BIG_ENDIAN
	/* byte swap descriptors in hardware */
	{
		u32 reg;

		reg = sky2_pci_read32(hw, PCI_DEV_REG2);
		reg |= PCI_REV_DESC;
		sky2_pci_write32(hw, PCI_DEV_REG2, reg);
	}
#endif

	/* ring for status responses */
	hw->st_le = pci_alloc_consistent(hw->pdev, STATUS_LE_BYTES,
					 &hw->st_dma);
	if (!hw->st_le)
		goto err_out_iounmap;

	err = sky2_reset(hw);
	if (err)
		goto err_out_iounmap;

	printk(KERN_INFO PFX "v%s addr 0x%lx irq %d Yukon-%s (0x%x) rev %d\n",
	       DRV_VERSION, pci_resource_start(pdev, 0), pdev->irq,
	       yukon2_name[hw->chip_id - CHIP_ID_YUKON_XL],
	       hw->chip_id, hw->chip_rev);

	dev = sky2_init_netdev(hw, 0, using_dac);
	if (!dev)
		goto err_out_free_pci;

	err = register_netdev(dev);
	if (err) {
		printk(KERN_ERR PFX "%s: cannot register net device\n",
		       pci_name(pdev));
		goto err_out_free_netdev;
	}

	sky2_show_addr(dev);

	if (hw->ports > 1 && (dev1 = sky2_init_netdev(hw, 1, using_dac))) {
		if (register_netdev(dev1) == 0)
			sky2_show_addr(dev1);
		else {
			/* Failure to register second port need not be fatal */
			printk(KERN_WARNING PFX
			       "register of second port failed\n");
			hw->dev[1] = NULL;
			free_netdev(dev1);
		}
	}

	if (!disable_msi && pci_enable_msi(pdev) == 0) {
		err = sky2_test_msi(hw);
		if (err == -EOPNOTSUPP)
 			pci_disable_msi(pdev);
		else if (err)
			goto err_out_unregister;
 	}

	err = request_irq(pdev->irq,  sky2_intr, SA_SHIRQ, DRV_NAME, hw);
	if (err) {
		printk(KERN_ERR PFX "%s: cannot assign irq %d\n",
		       pci_name(pdev), pdev->irq);
		goto err_out_unregister;
	}

	sky2_write32(hw, B0_IMSK, Y2_IS_BASE);

	setup_timer(&hw->idle_timer, sky2_idle, (unsigned long) dev);

	pci_set_drvdata(pdev, hw);

	return 0;

err_out_unregister:
	pci_disable_msi(pdev);
	if (dev1) {
		unregister_netdev(dev1);
		free_netdev(dev1);
	}
	unregister_netdev(dev);
err_out_free_netdev:
	free_netdev(dev);
err_out_free_pci:
	sky2_write8(hw, B0_CTST, CS_RST_SET);
	pci_free_consistent(hw->pdev, STATUS_LE_BYTES, hw->st_le, hw->st_dma);
err_out_iounmap:
	iounmap(hw->regs);
err_out_free_hw:
	kfree(hw);
err_out_free_regions:
	pci_release_regions(pdev);
	pci_disable_device(pdev);
err_out:
	return err;
}

static void __devexit sky2_remove(struct pci_dev *pdev)
{
	struct sky2_hw *hw = pci_get_drvdata(pdev);
	struct net_device *dev0, *dev1;

	if (!hw)
		return;

	del_timer_sync(&hw->idle_timer);

	sky2_write32(hw, B0_IMSK, 0);
	dev0 = hw->dev[0];
	dev1 = hw->dev[1];
	if (dev1)
		unregister_netdev(dev1);
	unregister_netdev(dev0);

	sky2_set_power_state(hw, PCI_D3hot);
	sky2_write16(hw, B0_Y2LED, LED_STAT_OFF);
	sky2_write8(hw, B0_CTST, CS_RST_SET);
	sky2_read8(hw, B0_CTST);

	free_irq(pdev->irq, hw);
	pci_disable_msi(pdev);
	pci_free_consistent(pdev, STATUS_LE_BYTES, hw->st_le, hw->st_dma);
	pci_release_regions(pdev);
	pci_disable_device(pdev);

	if (dev1)
		free_netdev(dev1);
	free_netdev(dev0);
	iounmap(hw->regs);
	kfree(hw);

	pci_set_drvdata(pdev, NULL);
}

#ifdef CONFIG_PM
static int sky2_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct sky2_hw *hw = pci_get_drvdata(pdev);
	int i;

	for (i = 0; i < 2; i++) {
		struct net_device *dev = hw->dev[i];

		if (dev) {
			if (!netif_running(dev))
				continue;

			sky2_down(dev);
			netif_device_detach(dev);
		}
	}

	return sky2_set_power_state(hw, pci_choose_state(pdev, state));
}

static int sky2_resume(struct pci_dev *pdev)
{
	struct sky2_hw *hw = pci_get_drvdata(pdev);
	int i, err;

	pci_restore_state(pdev);
	pci_enable_wake(pdev, PCI_D0, 0);
	err = sky2_set_power_state(hw, PCI_D0);
	if (err)
		goto out;

	err = sky2_reset(hw);
	if (err)
		goto out;

	for (i = 0; i < 2; i++) {
		struct net_device *dev = hw->dev[i];
		if (dev && netif_running(dev)) {
			netif_device_attach(dev);
			err = sky2_up(dev);
			if (err) {
				printk(KERN_ERR PFX "%s: could not up: %d\n",
				       dev->name, err);
				dev_close(dev);
				break;
			}
		}
	}
out:
	return err;
}
#endif

static struct pci_driver sky2_driver = {
	.name = DRV_NAME,
	.id_table = sky2_id_table,
	.probe = sky2_probe,
	.remove = __devexit_p(sky2_remove),
#ifdef CONFIG_PM
	.suspend = sky2_suspend,
	.resume = sky2_resume,
#endif
};

static int __init sky2_init_module(void)
{
	return pci_register_driver(&sky2_driver);
}

static void __exit sky2_cleanup_module(void)
{
	pci_unregister_driver(&sky2_driver);
}

module_init(sky2_init_module);
module_exit(sky2_cleanup_module);

MODULE_DESCRIPTION("Marvell Yukon 2 Gigabit Ethernet driver");
MODULE_AUTHOR("Stephen Hemminger <shemminger@osdl.org>");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);
