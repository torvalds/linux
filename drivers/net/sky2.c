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
 * the Free Software Foundation; either version 2 of the License.
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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/crc32.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/dma-mapping.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/pci.h>
#include <linux/ip.h>
#include <linux/slab.h>
#include <net/ip.h>
#include <linux/tcp.h>
#include <linux/in.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/if_vlan.h>
#include <linux/prefetch.h>
#include <linux/debugfs.h>
#include <linux/mii.h>

#include <asm/irq.h>

#include "sky2.h"

#define DRV_NAME		"sky2"
#define DRV_VERSION		"1.28"

/*
 * The Yukon II chipset takes 64 bit command blocks (called list elements)
 * that are organized into three (receive, transmit, status) different rings
 * similar to Tigon3.
 */

#define RX_LE_SIZE	    	1024
#define RX_LE_BYTES		(RX_LE_SIZE*sizeof(struct sky2_rx_le))
#define RX_MAX_PENDING		(RX_LE_SIZE/6 - 2)
#define RX_DEF_PENDING		RX_MAX_PENDING

/* This is the worst case number of transmit list elements for a single skb:
   VLAN:GSO + CKSUM + Data + skb_frags * DMA */
#define MAX_SKB_TX_LE	(2 + (sizeof(dma_addr_t)/sizeof(u32))*(MAX_SKB_FRAGS+1))
#define TX_MIN_PENDING		(MAX_SKB_TX_LE+1)
#define TX_MAX_PENDING		1024
#define TX_DEF_PENDING		127

#define TX_WATCHDOG		(5 * HZ)
#define NAPI_WEIGHT		64
#define PHY_RETRIES		1000

#define SKY2_EEPROM_MAGIC	0x9955aabb

#define RING_NEXT(x, s)	(((x)+1) & ((s)-1))

static const u32 default_msg =
    NETIF_MSG_DRV | NETIF_MSG_PROBE | NETIF_MSG_LINK
    | NETIF_MSG_TIMER | NETIF_MSG_TX_ERR | NETIF_MSG_RX_ERR
    | NETIF_MSG_IFUP | NETIF_MSG_IFDOWN;

static int debug = -1;		/* defaults above */
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "Debug level (0=none,...,16=all)");

static int copybreak __read_mostly = 128;
module_param(copybreak, int, 0);
MODULE_PARM_DESC(copybreak, "Receive copy threshold");

static int disable_msi = 0;
module_param(disable_msi, int, 0);
MODULE_PARM_DESC(disable_msi, "Disable Message Signaled Interrupt (MSI)");

static int legacy_pme = 0;
module_param(legacy_pme, int, 0);
MODULE_PARM_DESC(legacy_pme, "Legacy power management");

static DEFINE_PCI_DEVICE_TABLE(sky2_id_table) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_SYSKONNECT, 0x9000) }, /* SK-9Sxx */
	{ PCI_DEVICE(PCI_VENDOR_ID_SYSKONNECT, 0x9E00) }, /* SK-9Exx */
	{ PCI_DEVICE(PCI_VENDOR_ID_SYSKONNECT, 0x9E01) }, /* SK-9E21M */
	{ PCI_DEVICE(PCI_VENDOR_ID_DLINK, 0x4b00) },	/* DGE-560T */
	{ PCI_DEVICE(PCI_VENDOR_ID_DLINK, 0x4001) }, 	/* DGE-550SX */
	{ PCI_DEVICE(PCI_VENDOR_ID_DLINK, 0x4B02) },	/* DGE-560SX */
	{ PCI_DEVICE(PCI_VENDOR_ID_DLINK, 0x4B03) },	/* DGE-550T */
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x4340) }, /* 88E8021 */
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x4341) }, /* 88E8022 */
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x4342) }, /* 88E8061 */
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x4343) }, /* 88E8062 */
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x4344) }, /* 88E8021 */
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x4345) }, /* 88E8022 */
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x4346) }, /* 88E8061 */
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x4347) }, /* 88E8062 */
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x4350) }, /* 88E8035 */
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x4351) }, /* 88E8036 */
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x4352) }, /* 88E8038 */
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x4353) }, /* 88E8039 */
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x4354) }, /* 88E8040 */
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x4355) }, /* 88E8040T */
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x4356) }, /* 88EC033 */
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x4357) }, /* 88E8042 */
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x435A) }, /* 88E8048 */
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x4360) }, /* 88E8052 */
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x4361) }, /* 88E8050 */
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x4362) }, /* 88E8053 */
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x4363) }, /* 88E8055 */
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x4364) }, /* 88E8056 */
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x4365) }, /* 88E8070 */
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x4366) }, /* 88EC036 */
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x4367) }, /* 88EC032 */
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x4368) }, /* 88EC034 */
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x4369) }, /* 88EC042 */
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x436A) }, /* 88E8058 */
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x436B) }, /* 88E8071 */
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x436C) }, /* 88E8072 */
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x436D) }, /* 88E8055 */
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x4370) }, /* 88E8075 */
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x4380) }, /* 88E8057 */
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x4381) }, /* 88E8059 */
	{ 0 }
};

MODULE_DEVICE_TABLE(pci, sky2_id_table);

/* Avoid conditionals by using array */
static const unsigned txqaddr[] = { Q_XA1, Q_XA2 };
static const unsigned rxqaddr[] = { Q_R1, Q_R2 };
static const u32 portirq_msk[] = { Y2_IS_PORT_1, Y2_IS_PORT_2 };

static void sky2_set_multicast(struct net_device *dev);

/* Access to PHY via serial interconnect */
static int gm_phy_write(struct sky2_hw *hw, unsigned port, u16 reg, u16 val)
{
	int i;

	gma_write16(hw, port, GM_SMI_DATA, val);
	gma_write16(hw, port, GM_SMI_CTRL,
		    GM_SMI_CT_PHY_AD(PHY_ADDR_MARV) | GM_SMI_CT_REG_AD(reg));

	for (i = 0; i < PHY_RETRIES; i++) {
		u16 ctrl = gma_read16(hw, port, GM_SMI_CTRL);
		if (ctrl == 0xffff)
			goto io_error;

		if (!(ctrl & GM_SMI_CT_BUSY))
			return 0;

		udelay(10);
	}

	dev_warn(&hw->pdev->dev, "%s: phy write timeout\n", hw->dev[port]->name);
	return -ETIMEDOUT;

io_error:
	dev_err(&hw->pdev->dev, "%s: phy I/O error\n", hw->dev[port]->name);
	return -EIO;
}

static int __gm_phy_read(struct sky2_hw *hw, unsigned port, u16 reg, u16 *val)
{
	int i;

	gma_write16(hw, port, GM_SMI_CTRL, GM_SMI_CT_PHY_AD(PHY_ADDR_MARV)
		    | GM_SMI_CT_REG_AD(reg) | GM_SMI_CT_OP_RD);

	for (i = 0; i < PHY_RETRIES; i++) {
		u16 ctrl = gma_read16(hw, port, GM_SMI_CTRL);
		if (ctrl == 0xffff)
			goto io_error;

		if (ctrl & GM_SMI_CT_RD_VAL) {
			*val = gma_read16(hw, port, GM_SMI_DATA);
			return 0;
		}

		udelay(10);
	}

	dev_warn(&hw->pdev->dev, "%s: phy read timeout\n", hw->dev[port]->name);
	return -ETIMEDOUT;
io_error:
	dev_err(&hw->pdev->dev, "%s: phy I/O error\n", hw->dev[port]->name);
	return -EIO;
}

static inline u16 gm_phy_read(struct sky2_hw *hw, unsigned port, u16 reg)
{
	u16 v;
	__gm_phy_read(hw, port, reg, &v);
	return v;
}


static void sky2_power_on(struct sky2_hw *hw)
{
	/* switch power to VCC (WA for VAUX problem) */
	sky2_write8(hw, B0_POWER_CTRL,
		    PC_VAUX_ENA | PC_VCC_ENA | PC_VAUX_OFF | PC_VCC_ON);

	/* disable Core Clock Division, */
	sky2_write32(hw, B2_Y2_CLK_CTRL, Y2_CLK_DIV_DIS);

	if (hw->chip_id == CHIP_ID_YUKON_XL && hw->chip_rev > CHIP_REV_YU_XL_A1)
		/* enable bits are inverted */
		sky2_write8(hw, B2_Y2_CLK_GATE,
			    Y2_PCI_CLK_LNK1_DIS | Y2_COR_CLK_LNK1_DIS |
			    Y2_CLK_GAT_LNK1_DIS | Y2_PCI_CLK_LNK2_DIS |
			    Y2_COR_CLK_LNK2_DIS | Y2_CLK_GAT_LNK2_DIS);
	else
		sky2_write8(hw, B2_Y2_CLK_GATE, 0);

	if (hw->flags & SKY2_HW_ADV_POWER_CTL) {
		u32 reg;

		sky2_pci_write32(hw, PCI_DEV_REG3, 0);

		reg = sky2_pci_read32(hw, PCI_DEV_REG4);
		/* set all bits to 0 except bits 15..12 and 8 */
		reg &= P_ASPM_CONTROL_MSK;
		sky2_pci_write32(hw, PCI_DEV_REG4, reg);

		reg = sky2_pci_read32(hw, PCI_DEV_REG5);
		/* set all bits to 0 except bits 28 & 27 */
		reg &= P_CTL_TIM_VMAIN_AV_MSK;
		sky2_pci_write32(hw, PCI_DEV_REG5, reg);

		sky2_pci_write32(hw, PCI_CFG_REG_1, 0);

		sky2_write16(hw, B0_CTST, Y2_HW_WOL_ON);

		/* Enable workaround for dev 4.107 on Yukon-Ultra & Extreme */
		reg = sky2_read32(hw, B2_GP_IO);
		reg |= GLB_GPIO_STAT_RACE_DIS;
		sky2_write32(hw, B2_GP_IO, reg);

		sky2_read32(hw, B2_GP_IO);
	}

	/* Turn on "driver loaded" LED */
	sky2_write16(hw, B0_CTST, Y2_LED_STAT_ON);
}

static void sky2_power_aux(struct sky2_hw *hw)
{
	if (hw->chip_id == CHIP_ID_YUKON_XL && hw->chip_rev > CHIP_REV_YU_XL_A1)
		sky2_write8(hw, B2_Y2_CLK_GATE, 0);
	else
		/* enable bits are inverted */
		sky2_write8(hw, B2_Y2_CLK_GATE,
			    Y2_PCI_CLK_LNK1_DIS | Y2_COR_CLK_LNK1_DIS |
			    Y2_CLK_GAT_LNK1_DIS | Y2_PCI_CLK_LNK2_DIS |
			    Y2_COR_CLK_LNK2_DIS | Y2_CLK_GAT_LNK2_DIS);

	/* switch power to VAUX if supported and PME from D3cold */
	if ( (sky2_read32(hw, B0_CTST) & Y2_VAUX_AVAIL) &&
	     pci_pme_capable(hw->pdev, PCI_D3cold))
		sky2_write8(hw, B0_POWER_CTRL,
			    (PC_VAUX_ENA | PC_VCC_ENA |
			     PC_VAUX_ON | PC_VCC_OFF));

	/* turn off "driver loaded LED" */
	sky2_write16(hw, B0_CTST, Y2_LED_STAT_OFF);
}

static void sky2_gmac_reset(struct sky2_hw *hw, unsigned port)
{
	u16 reg;

	/* disable all GMAC IRQ's */
	sky2_write8(hw, SK_REG(port, GMAC_IRQ_MSK), 0);

	gma_write16(hw, port, GM_MC_ADDR_H1, 0);	/* clear MC hash */
	gma_write16(hw, port, GM_MC_ADDR_H2, 0);
	gma_write16(hw, port, GM_MC_ADDR_H3, 0);
	gma_write16(hw, port, GM_MC_ADDR_H4, 0);

	reg = gma_read16(hw, port, GM_RX_CTRL);
	reg |= GM_RXCR_UCF_ENA | GM_RXCR_MCF_ENA;
	gma_write16(hw, port, GM_RX_CTRL, reg);
}

/* flow control to advertise bits */
static const u16 copper_fc_adv[] = {
	[FC_NONE]	= 0,
	[FC_TX]		= PHY_M_AN_ASP,
	[FC_RX]		= PHY_M_AN_PC,
	[FC_BOTH]	= PHY_M_AN_PC | PHY_M_AN_ASP,
};

/* flow control to advertise bits when using 1000BaseX */
static const u16 fiber_fc_adv[] = {
	[FC_NONE] = PHY_M_P_NO_PAUSE_X,
	[FC_TX]   = PHY_M_P_ASYM_MD_X,
	[FC_RX]	  = PHY_M_P_SYM_MD_X,
	[FC_BOTH] = PHY_M_P_BOTH_MD_X,
};

/* flow control to GMA disable bits */
static const u16 gm_fc_disable[] = {
	[FC_NONE] = GM_GPCR_FC_RX_DIS | GM_GPCR_FC_TX_DIS,
	[FC_TX]	  = GM_GPCR_FC_RX_DIS,
	[FC_RX]	  = GM_GPCR_FC_TX_DIS,
	[FC_BOTH] = 0,
};


static void sky2_phy_init(struct sky2_hw *hw, unsigned port)
{
	struct sky2_port *sky2 = netdev_priv(hw->dev[port]);
	u16 ctrl, ct1000, adv, pg, ledctrl, ledover, reg;

	if ( (sky2->flags & SKY2_FLAG_AUTO_SPEED) &&
	    !(hw->flags & SKY2_HW_NEWER_PHY)) {
		u16 ectrl = gm_phy_read(hw, port, PHY_MARV_EXT_CTRL);

		ectrl &= ~(PHY_M_EC_M_DSC_MSK | PHY_M_EC_S_DSC_MSK |
			   PHY_M_EC_MAC_S_MSK);
		ectrl |= PHY_M_EC_MAC_S(MAC_TX_CLK_25_MHZ);

		/* on PHY 88E1040 Rev.D0 (and newer) downshift control changed */
		if (hw->chip_id == CHIP_ID_YUKON_EC)
			/* set downshift counter to 3x and enable downshift */
			ectrl |= PHY_M_EC_DSC_2(2) | PHY_M_EC_DOWN_S_ENA;
		else
			/* set master & slave downshift counter to 1x */
			ectrl |= PHY_M_EC_M_DSC(0) | PHY_M_EC_S_DSC(1);

		gm_phy_write(hw, port, PHY_MARV_EXT_CTRL, ectrl);
	}

	ctrl = gm_phy_read(hw, port, PHY_MARV_PHY_CTRL);
	if (sky2_is_copper(hw)) {
		if (!(hw->flags & SKY2_HW_GIGABIT)) {
			/* enable automatic crossover */
			ctrl |= PHY_M_PC_MDI_XMODE(PHY_M_PC_ENA_AUTO) >> 1;

			if (hw->chip_id == CHIP_ID_YUKON_FE_P &&
			    hw->chip_rev == CHIP_REV_YU_FE2_A0) {
				u16 spec;

				/* Enable Class A driver for FE+ A0 */
				spec = gm_phy_read(hw, port, PHY_MARV_FE_SPEC_2);
				spec |= PHY_M_FESC_SEL_CL_A;
				gm_phy_write(hw, port, PHY_MARV_FE_SPEC_2, spec);
			}
		} else {
			/* disable energy detect */
			ctrl &= ~PHY_M_PC_EN_DET_MSK;

			/* enable automatic crossover */
			ctrl |= PHY_M_PC_MDI_XMODE(PHY_M_PC_ENA_AUTO);

			/* downshift on PHY 88E1112 and 88E1149 is changed */
			if ( (sky2->flags & SKY2_FLAG_AUTO_SPEED) &&
			     (hw->flags & SKY2_HW_NEWER_PHY)) {
				/* set downshift counter to 3x and enable downshift */
				ctrl &= ~PHY_M_PC_DSC_MSK;
				ctrl |= PHY_M_PC_DSC(2) | PHY_M_PC_DOWN_S_ENA;
			}
		}
	} else {
		/* workaround for deviation #4.88 (CRC errors) */
		/* disable Automatic Crossover */

		ctrl &= ~PHY_M_PC_MDIX_MSK;
	}

	gm_phy_write(hw, port, PHY_MARV_PHY_CTRL, ctrl);

	/* special setup for PHY 88E1112 Fiber */
	if (hw->chip_id == CHIP_ID_YUKON_XL && (hw->flags & SKY2_HW_FIBRE_PHY)) {
		pg = gm_phy_read(hw, port, PHY_MARV_EXT_ADR);

		/* Fiber: select 1000BASE-X only mode MAC Specific Ctrl Reg. */
		gm_phy_write(hw, port, PHY_MARV_EXT_ADR, 2);
		ctrl = gm_phy_read(hw, port, PHY_MARV_PHY_CTRL);
		ctrl &= ~PHY_M_MAC_MD_MSK;
		ctrl |= PHY_M_MAC_MODE_SEL(PHY_M_MAC_MD_1000BX);
		gm_phy_write(hw, port, PHY_MARV_PHY_CTRL, ctrl);

		if (hw->pmd_type  == 'P') {
			/* select page 1 to access Fiber registers */
			gm_phy_write(hw, port, PHY_MARV_EXT_ADR, 1);

			/* for SFP-module set SIGDET polarity to low */
			ctrl = gm_phy_read(hw, port, PHY_MARV_PHY_CTRL);
			ctrl |= PHY_M_FIB_SIGD_POL;
			gm_phy_write(hw, port, PHY_MARV_PHY_CTRL, ctrl);
		}

		gm_phy_write(hw, port, PHY_MARV_EXT_ADR, pg);
	}

	ctrl = PHY_CT_RESET;
	ct1000 = 0;
	adv = PHY_AN_CSMA;
	reg = 0;

	if (sky2->flags & SKY2_FLAG_AUTO_SPEED) {
		if (sky2_is_copper(hw)) {
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

		} else {	/* special defines for FIBER (88E1040S only) */
			if (sky2->advertising & ADVERTISED_1000baseT_Full)
				adv |= PHY_M_AN_1000X_AFD;
			if (sky2->advertising & ADVERTISED_1000baseT_Half)
				adv |= PHY_M_AN_1000X_AHD;
		}

		/* Restart Auto-negotiation */
		ctrl |= PHY_CT_ANE | PHY_CT_RE_CFG;
	} else {
		/* forced speed/duplex settings */
		ct1000 = PHY_M_1000C_MSE;

		/* Disable auto update for duplex flow control and duplex */
		reg |= GM_GPCR_AU_DUP_DIS | GM_GPCR_AU_SPD_DIS;

		switch (sky2->speed) {
		case SPEED_1000:
			ctrl |= PHY_CT_SP1000;
			reg |= GM_GPCR_SPEED_1000;
			break;
		case SPEED_100:
			ctrl |= PHY_CT_SP100;
			reg |= GM_GPCR_SPEED_100;
			break;
		}

		if (sky2->duplex == DUPLEX_FULL) {
			reg |= GM_GPCR_DUP_FULL;
			ctrl |= PHY_CT_DUP_MD;
		} else if (sky2->speed < SPEED_1000)
			sky2->flow_mode = FC_NONE;
	}

	if (sky2->flags & SKY2_FLAG_AUTO_PAUSE) {
		if (sky2_is_copper(hw))
			adv |= copper_fc_adv[sky2->flow_mode];
		else
			adv |= fiber_fc_adv[sky2->flow_mode];
	} else {
		reg |= GM_GPCR_AU_FCT_DIS;
 		reg |= gm_fc_disable[sky2->flow_mode];

		/* Forward pause packets to GMAC? */
		if (sky2->flow_mode & FC_RX)
			sky2_write8(hw, SK_REG(port, GMAC_CTRL), GMC_PAUSE_ON);
		else
			sky2_write8(hw, SK_REG(port, GMAC_CTRL), GMC_PAUSE_OFF);
	}

	gma_write16(hw, port, GM_GP_CTRL, reg);

	if (hw->flags & SKY2_HW_GIGABIT)
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

	case CHIP_ID_YUKON_FE_P:
		/* Enable Link Partner Next Page */
		ctrl = gm_phy_read(hw, port, PHY_MARV_PHY_CTRL);
		ctrl |= PHY_M_PC_ENA_LIP_NP;

		/* disable Energy Detect and enable scrambler */
		ctrl &= ~(PHY_M_PC_ENA_ENE_DT | PHY_M_PC_DIS_SCRAMB);
		gm_phy_write(hw, port, PHY_MARV_PHY_CTRL, ctrl);

		/* set LED2 -> ACT, LED1 -> LINK, LED0 -> SPEED */
		ctrl = PHY_M_FELP_LED2_CTRL(LED_PAR_CTRL_ACT_BL) |
			PHY_M_FELP_LED1_CTRL(LED_PAR_CTRL_LINK) |
			PHY_M_FELP_LED0_CTRL(LED_PAR_CTRL_SPEED);

		gm_phy_write(hw, port, PHY_MARV_FE_LED_PAR, ctrl);
		break;

	case CHIP_ID_YUKON_XL:
		pg = gm_phy_read(hw, port, PHY_MARV_EXT_ADR);

		/* select page 3 to access LED control register */
		gm_phy_write(hw, port, PHY_MARV_EXT_ADR, 3);

		/* set LED Function Control register */
		gm_phy_write(hw, port, PHY_MARV_PHY_CTRL,
			     (PHY_M_LEDC_LOS_CTRL(1) |	/* LINK/ACT */
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

	case CHIP_ID_YUKON_EC_U:
	case CHIP_ID_YUKON_EX:
	case CHIP_ID_YUKON_SUPR:
		pg = gm_phy_read(hw, port, PHY_MARV_EXT_ADR);

		/* select page 3 to access LED control register */
		gm_phy_write(hw, port, PHY_MARV_EXT_ADR, 3);

		/* set LED Function Control register */
		gm_phy_write(hw, port, PHY_MARV_PHY_CTRL,
			     (PHY_M_LEDC_LOS_CTRL(1) |	/* LINK/ACT */
			      PHY_M_LEDC_INIT_CTRL(8) |	/* 10 Mbps */
			      PHY_M_LEDC_STA1_CTRL(7) |	/* 100 Mbps */
			      PHY_M_LEDC_STA0_CTRL(7)));/* 1000 Mbps */

		/* set Blink Rate in LED Timer Control Register */
		gm_phy_write(hw, port, PHY_MARV_INT_MASK,
			     ledctrl | PHY_M_LED_BLINK_RT(BLINK_84MS));
		/* restore page register */
		gm_phy_write(hw, port, PHY_MARV_EXT_ADR, pg);
		break;

	default:
		/* set Tx LED (LED_TX) to blink mode on Rx OR Tx activity */
		ledctrl |= PHY_M_LED_BLINK_RT(BLINK_84MS) | PHY_M_LEDC_TX_CTRL;

		/* turn off the Rx LED (LED_RX) */
		ledover |= PHY_M_LED_MO_RX(MO_LED_OFF);
	}

	if (hw->chip_id == CHIP_ID_YUKON_EC_U || hw->chip_id == CHIP_ID_YUKON_UL_2) {
		/* apply fixes in PHY AFE */
		gm_phy_write(hw, port, PHY_MARV_EXT_ADR, 255);

		/* increase differential signal amplitude in 10BASE-T */
		gm_phy_write(hw, port, 0x18, 0xaa99);
		gm_phy_write(hw, port, 0x17, 0x2011);

		if (hw->chip_id == CHIP_ID_YUKON_EC_U) {
			/* fix for IEEE A/B Symmetry failure in 1000BASE-T */
			gm_phy_write(hw, port, 0x18, 0xa204);
			gm_phy_write(hw, port, 0x17, 0x2002);
		}

		/* set page register to 0 */
		gm_phy_write(hw, port, PHY_MARV_EXT_ADR, 0);
	} else if (hw->chip_id == CHIP_ID_YUKON_FE_P &&
		   hw->chip_rev == CHIP_REV_YU_FE2_A0) {
		/* apply workaround for integrated resistors calibration */
		gm_phy_write(hw, port, PHY_MARV_PAGE_ADDR, 17);
		gm_phy_write(hw, port, PHY_MARV_PAGE_DATA, 0x3f60);
	} else if (hw->chip_id == CHIP_ID_YUKON_OPT && hw->chip_rev == 0) {
		/* apply fixes in PHY AFE */
		gm_phy_write(hw, port, PHY_MARV_EXT_ADR, 0x00ff);

		/* apply RDAC termination workaround */
		gm_phy_write(hw, port, 24, 0x2800);
		gm_phy_write(hw, port, 23, 0x2001);

		/* set page register back to 0 */
		gm_phy_write(hw, port, PHY_MARV_EXT_ADR, 0);
	} else if (hw->chip_id != CHIP_ID_YUKON_EX &&
		   hw->chip_id < CHIP_ID_YUKON_SUPR) {
		/* no effect on Yukon-XL */
		gm_phy_write(hw, port, PHY_MARV_LED_CTRL, ledctrl);

		if (!(sky2->flags & SKY2_FLAG_AUTO_SPEED) ||
		    sky2->speed == SPEED_100) {
			/* turn on 100 Mbps LED (LED_LINK100) */
			ledover |= PHY_M_LED_MO_100(MO_LED_ON);
		}

		if (ledover)
			gm_phy_write(hw, port, PHY_MARV_LED_OVER, ledover);

	}

	/* Enable phy interrupt on auto-negotiation complete (or link up) */
	if (sky2->flags & SKY2_FLAG_AUTO_SPEED)
		gm_phy_write(hw, port, PHY_MARV_INT_MASK, PHY_M_IS_AN_COMPL);
	else
		gm_phy_write(hw, port, PHY_MARV_INT_MASK, PHY_M_DEF_MSK);
}

static const u32 phy_power[] = { PCI_Y2_PHY1_POWD, PCI_Y2_PHY2_POWD };
static const u32 coma_mode[] = { PCI_Y2_PHY1_COMA, PCI_Y2_PHY2_COMA };

static void sky2_phy_power_up(struct sky2_hw *hw, unsigned port)
{
	u32 reg1;

	sky2_write8(hw, B2_TST_CTRL1, TST_CFG_WRITE_ON);
	reg1 = sky2_pci_read32(hw, PCI_DEV_REG1);
	reg1 &= ~phy_power[port];

	if (hw->chip_id == CHIP_ID_YUKON_XL && hw->chip_rev > CHIP_REV_YU_XL_A1)
		reg1 |= coma_mode[port];

	sky2_pci_write32(hw, PCI_DEV_REG1, reg1);
	sky2_write8(hw, B2_TST_CTRL1, TST_CFG_WRITE_OFF);
	sky2_pci_read32(hw, PCI_DEV_REG1);

	if (hw->chip_id == CHIP_ID_YUKON_FE)
		gm_phy_write(hw, port, PHY_MARV_CTRL, PHY_CT_ANE);
	else if (hw->flags & SKY2_HW_ADV_POWER_CTL)
		sky2_write8(hw, SK_REG(port, GPHY_CTRL), GPC_RST_CLR);
}

static void sky2_phy_power_down(struct sky2_hw *hw, unsigned port)
{
	u32 reg1;
	u16 ctrl;

	/* release GPHY Control reset */
	sky2_write8(hw, SK_REG(port, GPHY_CTRL), GPC_RST_CLR);

	/* release GMAC reset */
	sky2_write8(hw, SK_REG(port, GMAC_CTRL), GMC_RST_CLR);

	if (hw->flags & SKY2_HW_NEWER_PHY) {
		/* select page 2 to access MAC control register */
		gm_phy_write(hw, port, PHY_MARV_EXT_ADR, 2);

		ctrl = gm_phy_read(hw, port, PHY_MARV_PHY_CTRL);
		/* allow GMII Power Down */
		ctrl &= ~PHY_M_MAC_GMIF_PUP;
		gm_phy_write(hw, port, PHY_MARV_PHY_CTRL, ctrl);

		/* set page register back to 0 */
		gm_phy_write(hw, port, PHY_MARV_EXT_ADR, 0);
	}

	/* setup General Purpose Control Register */
	gma_write16(hw, port, GM_GP_CTRL,
		    GM_GPCR_FL_PASS | GM_GPCR_SPEED_100 |
		    GM_GPCR_AU_DUP_DIS | GM_GPCR_AU_FCT_DIS |
		    GM_GPCR_AU_SPD_DIS);

	if (hw->chip_id != CHIP_ID_YUKON_EC) {
		if (hw->chip_id == CHIP_ID_YUKON_EC_U) {
			/* select page 2 to access MAC control register */
			gm_phy_write(hw, port, PHY_MARV_EXT_ADR, 2);

			ctrl = gm_phy_read(hw, port, PHY_MARV_PHY_CTRL);
			/* enable Power Down */
			ctrl |= PHY_M_PC_POW_D_ENA;
			gm_phy_write(hw, port, PHY_MARV_PHY_CTRL, ctrl);

			/* set page register back to 0 */
			gm_phy_write(hw, port, PHY_MARV_EXT_ADR, 0);
		}

		/* set IEEE compatible Power Down Mode (dev. #4.99) */
		gm_phy_write(hw, port, PHY_MARV_CTRL, PHY_CT_PDOWN);
	}

	sky2_write8(hw, B2_TST_CTRL1, TST_CFG_WRITE_ON);
	reg1 = sky2_pci_read32(hw, PCI_DEV_REG1);
	reg1 |= phy_power[port];		/* set PHY to PowerDown/COMA Mode */
	sky2_pci_write32(hw, PCI_DEV_REG1, reg1);
	sky2_write8(hw, B2_TST_CTRL1, TST_CFG_WRITE_OFF);
}

/* Enable Rx/Tx */
static void sky2_enable_rx_tx(struct sky2_port *sky2)
{
	struct sky2_hw *hw = sky2->hw;
	unsigned port = sky2->port;
	u16 reg;

	reg = gma_read16(hw, port, GM_GP_CTRL);
	reg |= GM_GPCR_RX_ENA | GM_GPCR_TX_ENA;
	gma_write16(hw, port, GM_GP_CTRL, reg);
}

/* Force a renegotiation */
static void sky2_phy_reinit(struct sky2_port *sky2)
{
	spin_lock_bh(&sky2->phy_lock);
	sky2_phy_init(sky2->hw, sky2->port);
	sky2_enable_rx_tx(sky2);
	spin_unlock_bh(&sky2->phy_lock);
}

/* Put device in state to listen for Wake On Lan */
static void sky2_wol_init(struct sky2_port *sky2)
{
	struct sky2_hw *hw = sky2->hw;
	unsigned port = sky2->port;
	enum flow_control save_mode;
	u16 ctrl;

	/* Bring hardware out of reset */
	sky2_write16(hw, B0_CTST, CS_RST_CLR);
	sky2_write16(hw, SK_REG(port, GMAC_LINK_CTRL), GMLC_RST_CLR);

	sky2_write8(hw, SK_REG(port, GPHY_CTRL), GPC_RST_CLR);
	sky2_write8(hw, SK_REG(port, GMAC_CTRL), GMC_RST_CLR);

	/* Force to 10/100
	 * sky2_reset will re-enable on resume
	 */
	save_mode = sky2->flow_mode;
	ctrl = sky2->advertising;

	sky2->advertising &= ~(ADVERTISED_1000baseT_Half|ADVERTISED_1000baseT_Full);
	sky2->flow_mode = FC_NONE;

	spin_lock_bh(&sky2->phy_lock);
	sky2_phy_power_up(hw, port);
	sky2_phy_init(hw, port);
	spin_unlock_bh(&sky2->phy_lock);

	sky2->flow_mode = save_mode;
	sky2->advertising = ctrl;

	/* Set GMAC to no flow control and auto update for speed/duplex */
	gma_write16(hw, port, GM_GP_CTRL,
		    GM_GPCR_FC_TX_DIS|GM_GPCR_TX_ENA|GM_GPCR_RX_ENA|
		    GM_GPCR_DUP_FULL|GM_GPCR_FC_RX_DIS|GM_GPCR_AU_FCT_DIS);

	/* Set WOL address */
	memcpy_toio(hw->regs + WOL_REGS(port, WOL_MAC_ADDR),
		    sky2->netdev->dev_addr, ETH_ALEN);

	/* Turn on appropriate WOL control bits */
	sky2_write16(hw, WOL_REGS(port, WOL_CTRL_STAT), WOL_CTL_CLEAR_RESULT);
	ctrl = 0;
	if (sky2->wol & WAKE_PHY)
		ctrl |= WOL_CTL_ENA_PME_ON_LINK_CHG|WOL_CTL_ENA_LINK_CHG_UNIT;
	else
		ctrl |= WOL_CTL_DIS_PME_ON_LINK_CHG|WOL_CTL_DIS_LINK_CHG_UNIT;

	if (sky2->wol & WAKE_MAGIC)
		ctrl |= WOL_CTL_ENA_PME_ON_MAGIC_PKT|WOL_CTL_ENA_MAGIC_PKT_UNIT;
	else
		ctrl |= WOL_CTL_DIS_PME_ON_MAGIC_PKT|WOL_CTL_DIS_MAGIC_PKT_UNIT;

	ctrl |= WOL_CTL_DIS_PME_ON_PATTERN|WOL_CTL_DIS_PATTERN_UNIT;
	sky2_write16(hw, WOL_REGS(port, WOL_CTRL_STAT), ctrl);

	/* Disable PiG firmware */
	sky2_write16(hw, B0_CTST, Y2_HW_WOL_OFF);

	/* Needed by some broken BIOSes, use PCI rather than PCI-e for WOL */
	if (legacy_pme) {
		u32 reg1 = sky2_pci_read32(hw, PCI_DEV_REG1);
		reg1 |= PCI_Y2_PME_LEGACY;
		sky2_pci_write32(hw, PCI_DEV_REG1, reg1);
	}

	/* block receiver */
	sky2_write8(hw, SK_REG(port, RX_GMF_CTRL_T), GMF_RST_SET);
}

static void sky2_set_tx_stfwd(struct sky2_hw *hw, unsigned port)
{
	struct net_device *dev = hw->dev[port];

	if ( (hw->chip_id == CHIP_ID_YUKON_EX &&
	      hw->chip_rev != CHIP_REV_YU_EX_A0) ||
	     hw->chip_id >= CHIP_ID_YUKON_FE_P) {
		/* Yukon-Extreme B0 and further Extreme devices */
		sky2_write32(hw, SK_REG(port, TX_GMF_CTRL_T), TX_STFW_ENA);
	} else if (dev->mtu > ETH_DATA_LEN) {
		/* set Tx GMAC FIFO Almost Empty Threshold */
		sky2_write32(hw, SK_REG(port, TX_GMF_AE_THR),
			     (ECU_JUMBO_WM << 16) | ECU_AE_THR);

		sky2_write32(hw, SK_REG(port, TX_GMF_CTRL_T), TX_STFW_DIS);
	} else
		sky2_write32(hw, SK_REG(port, TX_GMF_CTRL_T), TX_STFW_ENA);
}

static void sky2_mac_init(struct sky2_hw *hw, unsigned port)
{
	struct sky2_port *sky2 = netdev_priv(hw->dev[port]);
	u16 reg;
	u32 rx_reg;
	int i;
	const u8 *addr = hw->dev[port]->dev_addr;

	sky2_write8(hw, SK_REG(port, GPHY_CTRL), GPC_RST_SET);
	sky2_write8(hw, SK_REG(port, GPHY_CTRL), GPC_RST_CLR);

	sky2_write8(hw, SK_REG(port, GMAC_CTRL), GMC_RST_CLR);

	if (hw->chip_id == CHIP_ID_YUKON_XL &&
	    hw->chip_rev == CHIP_REV_YU_XL_A0 &&
	    port == 1) {
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

	sky2_read16(hw, SK_REG(port, GMAC_IRQ_SRC));

	/* Enable Transmit FIFO Underrun */
	sky2_write8(hw, SK_REG(port, GMAC_IRQ_MSK), GMAC_DEF_MSK);

	spin_lock_bh(&sky2->phy_lock);
	sky2_phy_power_up(hw, port);
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

	if (hw->chip_id == CHIP_ID_YUKON_EC_U &&
	    hw->chip_rev == CHIP_REV_YU_EC_U_B1)
		reg |= GM_NEW_FLOW_CTRL;

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
	rx_reg = GMF_OPER_ON | GMF_RX_F_FL_ON;
	if (hw->chip_id == CHIP_ID_YUKON_EX ||
	    hw->chip_id == CHIP_ID_YUKON_FE_P)
		rx_reg |= GMF_RX_OVER_ON;

	sky2_write32(hw, SK_REG(port, RX_GMF_CTRL_T), rx_reg);

	if (hw->chip_id == CHIP_ID_YUKON_XL) {
		/* Hardware errata - clear flush mask */
		sky2_write16(hw, SK_REG(port, RX_GMF_FL_MSK), 0);
	} else {
		/* Flush Rx MAC FIFO on any flow control or error */
		sky2_write16(hw, SK_REG(port, RX_GMF_FL_MSK), GMR_FS_ANY_ERR);
	}

	/* Set threshold to 0xa (64 bytes) + 1 to workaround pause bug  */
	reg = RX_GMF_FL_THR_DEF + 1;
	/* Another magic mystery workaround from sk98lin */
	if (hw->chip_id == CHIP_ID_YUKON_FE_P &&
	    hw->chip_rev == CHIP_REV_YU_FE2_A0)
		reg = 0x178;
	sky2_write16(hw, SK_REG(port, RX_GMF_FL_THR), reg);

	/* Configure Tx MAC FIFO */
	sky2_write8(hw, SK_REG(port, TX_GMF_CTRL_T), GMF_RST_CLR);
	sky2_write16(hw, SK_REG(port, TX_GMF_CTRL_T), GMF_OPER_ON);

	/* On chips without ram buffer, pause is controlled by MAC level */
	if (!(hw->flags & SKY2_HW_RAM_BUFFER)) {
		/* Pause threshold is scaled by 8 in bytes */
		if (hw->chip_id == CHIP_ID_YUKON_FE_P &&
		    hw->chip_rev == CHIP_REV_YU_FE2_A0)
			reg = 1568 / 8;
		else
			reg = 1024 / 8;
		sky2_write16(hw, SK_REG(port, RX_GMF_UP_THR), reg);
		sky2_write16(hw, SK_REG(port, RX_GMF_LP_THR), 768 / 8);

		sky2_set_tx_stfwd(hw, port);
	}

	if (hw->chip_id == CHIP_ID_YUKON_FE_P &&
	    hw->chip_rev == CHIP_REV_YU_FE2_A0) {
		/* disable dynamic watermark */
		reg = sky2_read16(hw, SK_REG(port, TX_GMF_EA));
		reg &= ~TX_DYN_WM_ENA;
		sky2_write16(hw, SK_REG(port, TX_GMF_EA), reg);
	}
}

/* Assign Ram Buffer allocation to queue */
static void sky2_ramset(struct sky2_hw *hw, u16 q, u32 start, u32 space)
{
	u32 end;

	/* convert from K bytes to qwords used for hw register */
	start *= 1024/8;
	space *= 1024/8;
	end = start + space - 1;

	sky2_write8(hw, RB_ADDR(q, RB_CTRL), RB_RST_CLR);
	sky2_write32(hw, RB_ADDR(q, RB_START), start);
	sky2_write32(hw, RB_ADDR(q, RB_END), end);
	sky2_write32(hw, RB_ADDR(q, RB_WP), start);
	sky2_write32(hw, RB_ADDR(q, RB_RP), start);

	if (q == Q_R1 || q == Q_R2) {
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
			       dma_addr_t addr, u32 last)
{
	sky2_write32(hw, Y2_QADDR(qaddr, PREF_UNIT_CTRL), PREF_UNIT_RST_SET);
	sky2_write32(hw, Y2_QADDR(qaddr, PREF_UNIT_CTRL), PREF_UNIT_RST_CLR);
	sky2_write32(hw, Y2_QADDR(qaddr, PREF_UNIT_ADDR_HI), upper_32_bits(addr));
	sky2_write32(hw, Y2_QADDR(qaddr, PREF_UNIT_ADDR_LO), lower_32_bits(addr));
	sky2_write16(hw, Y2_QADDR(qaddr, PREF_UNIT_LAST_IDX), last);
	sky2_write32(hw, Y2_QADDR(qaddr, PREF_UNIT_CTRL), PREF_UNIT_OP_ON);

	sky2_read32(hw, Y2_QADDR(qaddr, PREF_UNIT_CTRL));
}

static inline struct sky2_tx_le *get_tx_le(struct sky2_port *sky2, u16 *slot)
{
	struct sky2_tx_le *le = sky2->tx_le + *slot;

	*slot = RING_NEXT(*slot, sky2->tx_ring_size);
	le->ctrl = 0;
	return le;
}

static void tx_init(struct sky2_port *sky2)
{
	struct sky2_tx_le *le;

	sky2->tx_prod = sky2->tx_cons = 0;
	sky2->tx_tcpsum = 0;
	sky2->tx_last_mss = 0;

	le = get_tx_le(sky2, &sky2->tx_prod);
	le->addr = 0;
	le->opcode = OP_ADDR64 | HW_OWNER;
	sky2->tx_last_upper = 0;
}

/* Update chip's next pointer */
static inline void sky2_put_idx(struct sky2_hw *hw, unsigned q, u16 idx)
{
	/* Make sure write' to descriptors are complete before we tell hardware */
	wmb();
	sky2_write16(hw, Y2_QADDR(q, PREF_UNIT_PUT_IDX), idx);

	/* Synchronize I/O on since next processor may write to tail */
	mmiowb();
}


static inline struct sky2_rx_le *sky2_next_rx(struct sky2_port *sky2)
{
	struct sky2_rx_le *le = sky2->rx_le + sky2->rx_put;
	sky2->rx_put = RING_NEXT(sky2->rx_put, RX_LE_SIZE);
	le->ctrl = 0;
	return le;
}

static unsigned sky2_get_rx_threshold(struct sky2_port *sky2)
{
	unsigned size;

	/* Space needed for frame data + headers rounded up */
	size = roundup(sky2->netdev->mtu + ETH_HLEN + VLAN_HLEN, 8);

	/* Stopping point for hardware truncation */
	return (size - 8) / sizeof(u32);
}

static unsigned sky2_get_rx_data_size(struct sky2_port *sky2)
{
	struct rx_ring_info *re;
	unsigned size;

	/* Space needed for frame data + headers rounded up */
	size = roundup(sky2->netdev->mtu + ETH_HLEN + VLAN_HLEN, 8);

	sky2->rx_nfrags = size >> PAGE_SHIFT;
	BUG_ON(sky2->rx_nfrags > ARRAY_SIZE(re->frag_addr));

	/* Compute residue after pages */
	size -= sky2->rx_nfrags << PAGE_SHIFT;

	/* Optimize to handle small packets and headers */
	if (size < copybreak)
		size = copybreak;
	if (size < ETH_HLEN)
		size = ETH_HLEN;

	return size;
}

/* Build description to hardware for one receive segment */
static void sky2_rx_add(struct sky2_port *sky2, u8 op,
			dma_addr_t map, unsigned len)
{
	struct sky2_rx_le *le;

	if (sizeof(dma_addr_t) > sizeof(u32)) {
		le = sky2_next_rx(sky2);
		le->addr = cpu_to_le32(upper_32_bits(map));
		le->opcode = OP_ADDR64 | HW_OWNER;
	}

	le = sky2_next_rx(sky2);
	le->addr = cpu_to_le32(lower_32_bits(map));
	le->length = cpu_to_le16(len);
	le->opcode = op | HW_OWNER;
}

/* Build description to hardware for one possibly fragmented skb */
static void sky2_rx_submit(struct sky2_port *sky2,
			   const struct rx_ring_info *re)
{
	int i;

	sky2_rx_add(sky2, OP_PACKET, re->data_addr, sky2->rx_data_size);

	for (i = 0; i < skb_shinfo(re->skb)->nr_frags; i++)
		sky2_rx_add(sky2, OP_BUFFER, re->frag_addr[i], PAGE_SIZE);
}


static int sky2_rx_map_skb(struct pci_dev *pdev, struct rx_ring_info *re,
			    unsigned size)
{
	struct sk_buff *skb = re->skb;
	int i;

	re->data_addr = pci_map_single(pdev, skb->data, size, PCI_DMA_FROMDEVICE);
	if (pci_dma_mapping_error(pdev, re->data_addr))
		goto mapping_error;

	dma_unmap_len_set(re, data_size, size);

	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

		re->frag_addr[i] = pci_map_page(pdev, frag->page,
						frag->page_offset,
						frag->size,
						PCI_DMA_FROMDEVICE);

		if (pci_dma_mapping_error(pdev, re->frag_addr[i]))
			goto map_page_error;
	}
	return 0;

map_page_error:
	while (--i >= 0) {
		pci_unmap_page(pdev, re->frag_addr[i],
			       skb_shinfo(skb)->frags[i].size,
			       PCI_DMA_FROMDEVICE);
	}

	pci_unmap_single(pdev, re->data_addr, dma_unmap_len(re, data_size),
			 PCI_DMA_FROMDEVICE);

mapping_error:
	if (net_ratelimit())
		dev_warn(&pdev->dev, "%s: rx mapping error\n",
			 skb->dev->name);
	return -EIO;
}

static void sky2_rx_unmap_skb(struct pci_dev *pdev, struct rx_ring_info *re)
{
	struct sk_buff *skb = re->skb;
	int i;

	pci_unmap_single(pdev, re->data_addr, dma_unmap_len(re, data_size),
			 PCI_DMA_FROMDEVICE);

	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++)
		pci_unmap_page(pdev, re->frag_addr[i],
			       skb_shinfo(skb)->frags[i].size,
			       PCI_DMA_FROMDEVICE);
}

/* Tell chip where to start receive checksum.
 * Actually has two checksums, but set both same to avoid possible byte
 * order problems.
 */
static void rx_set_checksum(struct sky2_port *sky2)
{
	struct sky2_rx_le *le = sky2_next_rx(sky2);

	le->addr = cpu_to_le32((ETH_HLEN << 16) | ETH_HLEN);
	le->ctrl = 0;
	le->opcode = OP_TCPSTART | HW_OWNER;

	sky2_write32(sky2->hw,
		     Q_ADDR(rxqaddr[sky2->port], Q_CSR),
		     (sky2->netdev->features & NETIF_F_RXCSUM)
		     ? BMU_ENA_RX_CHKSUM : BMU_DIS_RX_CHKSUM);
}

/* Enable/disable receive hash calculation (RSS) */
static void rx_set_rss(struct net_device *dev, u32 features)
{
	struct sky2_port *sky2 = netdev_priv(dev);
	struct sky2_hw *hw = sky2->hw;
	int i, nkeys = 4;

	/* Supports IPv6 and other modes */
	if (hw->flags & SKY2_HW_NEW_LE) {
		nkeys = 10;
		sky2_write32(hw, SK_REG(sky2->port, RSS_CFG), HASH_ALL);
	}

	/* Program RSS initial values */
	if (features & NETIF_F_RXHASH) {
		u32 key[nkeys];

		get_random_bytes(key, nkeys * sizeof(u32));
		for (i = 0; i < nkeys; i++)
			sky2_write32(hw, SK_REG(sky2->port, RSS_KEY + i * 4),
				     key[i]);

		/* Need to turn on (undocumented) flag to make hashing work  */
		sky2_write32(hw, SK_REG(sky2->port, RX_GMF_CTRL_T),
			     RX_STFW_ENA);

		sky2_write32(hw, Q_ADDR(rxqaddr[sky2->port], Q_CSR),
			     BMU_ENA_RX_RSS_HASH);
	} else
		sky2_write32(hw, Q_ADDR(rxqaddr[sky2->port], Q_CSR),
			     BMU_DIS_RX_RSS_HASH);
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

	netdev_warn(sky2->netdev, "receiver stop failed\n");
stopped:
	sky2_write32(hw, Q_ADDR(rxq, Q_CSR), BMU_RST_SET | BMU_FIFO_RST);

	/* reset the Rx prefetch unit */
	sky2_write32(hw, Y2_QADDR(rxq, PREF_UNIT_CTRL), PREF_UNIT_RST_SET);
	mmiowb();
}

/* Clean out receive buffer area, assumes receiver hardware stopped */
static void sky2_rx_clean(struct sky2_port *sky2)
{
	unsigned i;

	memset(sky2->rx_le, 0, RX_LE_BYTES);
	for (i = 0; i < sky2->rx_pending; i++) {
		struct rx_ring_info *re = sky2->rx_ring + i;

		if (re->skb) {
			sky2_rx_unmap_skb(sky2->hw->pdev, re);
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
		spin_lock_bh(&sky2->phy_lock);
		err = gm_phy_write(hw, sky2->port, data->reg_num & 0x1f,
				   data->val_in);
		spin_unlock_bh(&sky2->phy_lock);
		break;
	}
	return err;
}

#define SKY2_VLAN_OFFLOADS (NETIF_F_IP_CSUM | NETIF_F_SG | NETIF_F_TSO)

static void sky2_vlan_mode(struct net_device *dev, u32 features)
{
	struct sky2_port *sky2 = netdev_priv(dev);
	struct sky2_hw *hw = sky2->hw;
	u16 port = sky2->port;

	if (features & NETIF_F_HW_VLAN_RX)
		sky2_write32(hw, SK_REG(port, RX_GMF_CTRL_T),
			     RX_VLAN_STRIP_ON);
	else
		sky2_write32(hw, SK_REG(port, RX_GMF_CTRL_T),
			     RX_VLAN_STRIP_OFF);

	if (features & NETIF_F_HW_VLAN_TX) {
		sky2_write32(hw, SK_REG(port, TX_GMF_CTRL_T),
			     TX_VLAN_TAG_ON);

		dev->vlan_features |= SKY2_VLAN_OFFLOADS;
	} else {
		sky2_write32(hw, SK_REG(port, TX_GMF_CTRL_T),
			     TX_VLAN_TAG_OFF);

		/* Can't do transmit offload of vlan without hw vlan */
		dev->vlan_features &= ~SKY2_VLAN_OFFLOADS;
	}
}

/* Amount of required worst case padding in rx buffer */
static inline unsigned sky2_rx_pad(const struct sky2_hw *hw)
{
	return (hw->flags & SKY2_HW_RAM_BUFFER) ? 8 : 2;
}

/*
 * Allocate an skb for receiving. If the MTU is large enough
 * make the skb non-linear with a fragment list of pages.
 */
static struct sk_buff *sky2_rx_alloc(struct sky2_port *sky2)
{
	struct sk_buff *skb;
	int i;

	skb = netdev_alloc_skb(sky2->netdev,
			       sky2->rx_data_size + sky2_rx_pad(sky2->hw));
	if (!skb)
		goto nomem;

	if (sky2->hw->flags & SKY2_HW_RAM_BUFFER) {
		unsigned char *start;
		/*
		 * Workaround for a bug in FIFO that cause hang
		 * if the FIFO if the receive buffer is not 64 byte aligned.
		 * The buffer returned from netdev_alloc_skb is
		 * aligned except if slab debugging is enabled.
		 */
		start = PTR_ALIGN(skb->data, 8);
		skb_reserve(skb, start - skb->data);
	} else
		skb_reserve(skb, NET_IP_ALIGN);

	for (i = 0; i < sky2->rx_nfrags; i++) {
		struct page *page = alloc_page(GFP_ATOMIC);

		if (!page)
			goto free_partial;
		skb_fill_page_desc(skb, i, page, 0, PAGE_SIZE);
	}

	return skb;
free_partial:
	kfree_skb(skb);
nomem:
	return NULL;
}

static inline void sky2_rx_update(struct sky2_port *sky2, unsigned rxq)
{
	sky2_put_idx(sky2->hw, rxq, sky2->rx_put);
}

static int sky2_alloc_rx_skbs(struct sky2_port *sky2)
{
	struct sky2_hw *hw = sky2->hw;
	unsigned i;

	sky2->rx_data_size = sky2_get_rx_data_size(sky2);

	/* Fill Rx ring */
	for (i = 0; i < sky2->rx_pending; i++) {
		struct rx_ring_info *re = sky2->rx_ring + i;

		re->skb = sky2_rx_alloc(sky2);
		if (!re->skb)
			return -ENOMEM;

		if (sky2_rx_map_skb(hw->pdev, re, sky2->rx_data_size)) {
			dev_kfree_skb(re->skb);
			re->skb = NULL;
			return -ENOMEM;
		}
	}
	return 0;
}

/*
 * Setup receiver buffer pool.
 * Normal case this ends up creating one list element for skb
 * in the receive ring. Worst case if using large MTU and each
 * allocation falls on a different 64 bit region, that results
 * in 6 list elements per ring entry.
 * One element is used for checksum enable/disable, and one
 * extra to avoid wrap.
 */
static void sky2_rx_start(struct sky2_port *sky2)
{
	struct sky2_hw *hw = sky2->hw;
	struct rx_ring_info *re;
	unsigned rxq = rxqaddr[sky2->port];
	unsigned i, thresh;

	sky2->rx_put = sky2->rx_next = 0;
	sky2_qset(hw, rxq);

	/* On PCI express lowering the watermark gives better performance */
	if (pci_find_capability(hw->pdev, PCI_CAP_ID_EXP))
		sky2_write32(hw, Q_ADDR(rxq, Q_WM), BMU_WM_PEX);

	/* These chips have no ram buffer?
	 * MAC Rx RAM Read is controlled by hardware */
	if (hw->chip_id == CHIP_ID_YUKON_EC_U &&
	    hw->chip_rev > CHIP_REV_YU_EC_U_A0)
		sky2_write32(hw, Q_ADDR(rxq, Q_TEST), F_M_RX_RAM_DIS);

	sky2_prefetch_init(hw, rxq, sky2->rx_le_map, RX_LE_SIZE - 1);

	if (!(hw->flags & SKY2_HW_NEW_LE))
		rx_set_checksum(sky2);

	if (!(hw->flags & SKY2_HW_RSS_BROKEN))
		rx_set_rss(sky2->netdev, sky2->netdev->features);

	/* submit Rx ring */
	for (i = 0; i < sky2->rx_pending; i++) {
		re = sky2->rx_ring + i;
		sky2_rx_submit(sky2, re);
	}

	/*
	 * The receiver hangs if it receives frames larger than the
	 * packet buffer. As a workaround, truncate oversize frames, but
	 * the register is limited to 9 bits, so if you do frames > 2052
	 * you better get the MTU right!
	 */
	thresh = sky2_get_rx_threshold(sky2);
	if (thresh > 0x1ff)
		sky2_write32(hw, SK_REG(sky2->port, RX_GMF_CTRL_T), RX_TRUNC_OFF);
	else {
		sky2_write16(hw, SK_REG(sky2->port, RX_GMF_TR_THR), thresh);
		sky2_write32(hw, SK_REG(sky2->port, RX_GMF_CTRL_T), RX_TRUNC_ON);
	}

	/* Tell chip about available buffers */
	sky2_rx_update(sky2, rxq);

	if (hw->chip_id == CHIP_ID_YUKON_EX ||
	    hw->chip_id == CHIP_ID_YUKON_SUPR) {
		/*
		 * Disable flushing of non ASF packets;
		 * must be done after initializing the BMUs;
		 * drivers without ASF support should do this too, otherwise
		 * it may happen that they cannot run on ASF devices;
		 * remember that the MAC FIFO isn't reset during initialization.
		 */
		sky2_write32(hw, SK_REG(sky2->port, RX_GMF_CTRL_T), RX_MACSEC_FLUSH_OFF);
	}

	if (hw->chip_id >= CHIP_ID_YUKON_SUPR) {
		/* Enable RX Home Address & Routing Header checksum fix */
		sky2_write16(hw, SK_REG(sky2->port, RX_GMF_FL_CTRL),
			     RX_IPV6_SA_MOB_ENA | RX_IPV6_DA_MOB_ENA);

		/* Enable TX Home Address & Routing Header checksum fix */
		sky2_write32(hw, Q_ADDR(txqaddr[sky2->port], Q_TEST),
			     TBMU_TEST_HOME_ADD_FIX_EN | TBMU_TEST_ROUTING_ADD_FIX_EN);
	}
}

static int sky2_alloc_buffers(struct sky2_port *sky2)
{
	struct sky2_hw *hw = sky2->hw;

	/* must be power of 2 */
	sky2->tx_le = pci_alloc_consistent(hw->pdev,
					   sky2->tx_ring_size *
					   sizeof(struct sky2_tx_le),
					   &sky2->tx_le_map);
	if (!sky2->tx_le)
		goto nomem;

	sky2->tx_ring = kcalloc(sky2->tx_ring_size, sizeof(struct tx_ring_info),
				GFP_KERNEL);
	if (!sky2->tx_ring)
		goto nomem;

	sky2->rx_le = pci_alloc_consistent(hw->pdev, RX_LE_BYTES,
					   &sky2->rx_le_map);
	if (!sky2->rx_le)
		goto nomem;
	memset(sky2->rx_le, 0, RX_LE_BYTES);

	sky2->rx_ring = kcalloc(sky2->rx_pending, sizeof(struct rx_ring_info),
				GFP_KERNEL);
	if (!sky2->rx_ring)
		goto nomem;

	return sky2_alloc_rx_skbs(sky2);
nomem:
	return -ENOMEM;
}

static void sky2_free_buffers(struct sky2_port *sky2)
{
	struct sky2_hw *hw = sky2->hw;

	sky2_rx_clean(sky2);

	if (sky2->rx_le) {
		pci_free_consistent(hw->pdev, RX_LE_BYTES,
				    sky2->rx_le, sky2->rx_le_map);
		sky2->rx_le = NULL;
	}
	if (sky2->tx_le) {
		pci_free_consistent(hw->pdev,
				    sky2->tx_ring_size * sizeof(struct sky2_tx_le),
				    sky2->tx_le, sky2->tx_le_map);
		sky2->tx_le = NULL;
	}
	kfree(sky2->tx_ring);
	kfree(sky2->rx_ring);

	sky2->tx_ring = NULL;
	sky2->rx_ring = NULL;
}

static void sky2_hw_up(struct sky2_port *sky2)
{
	struct sky2_hw *hw = sky2->hw;
	unsigned port = sky2->port;
	u32 ramsize;
	int cap;
	struct net_device *otherdev = hw->dev[sky2->port^1];

	tx_init(sky2);

	/*
 	 * On dual port PCI-X card, there is an problem where status
	 * can be received out of order due to split transactions
	 */
	if (otherdev && netif_running(otherdev) &&
 	    (cap = pci_find_capability(hw->pdev, PCI_CAP_ID_PCIX))) {
 		u16 cmd;

		cmd = sky2_pci_read16(hw, cap + PCI_X_CMD);
 		cmd &= ~PCI_X_CMD_MAX_SPLIT;
 		sky2_pci_write16(hw, cap + PCI_X_CMD, cmd);
	}

	sky2_mac_init(hw, port);

	/* Register is number of 4K blocks on internal RAM buffer. */
	ramsize = sky2_read8(hw, B2_E_0) * 4;
	if (ramsize > 0) {
		u32 rxspace;

		netdev_dbg(sky2->netdev, "ram buffer %dK\n", ramsize);
		if (ramsize < 16)
			rxspace = ramsize / 2;
		else
			rxspace = 8 + (2*(ramsize - 16))/3;

		sky2_ramset(hw, rxqaddr[port], 0, rxspace);
		sky2_ramset(hw, txqaddr[port], rxspace, ramsize - rxspace);

		/* Make sure SyncQ is disabled */
		sky2_write8(hw, RB_ADDR(port == 0 ? Q_XS1 : Q_XS2, RB_CTRL),
			    RB_RST_SET);
	}

	sky2_qset(hw, txqaddr[port]);

	/* This is copied from sk98lin 10.0.5.3; no one tells me about erratta's */
	if (hw->chip_id == CHIP_ID_YUKON_EX && hw->chip_rev == CHIP_REV_YU_EX_B0)
		sky2_write32(hw, Q_ADDR(txqaddr[port], Q_TEST), F_TX_CHK_AUTO_OFF);

	/* Set almost empty threshold */
	if (hw->chip_id == CHIP_ID_YUKON_EC_U &&
	    hw->chip_rev == CHIP_REV_YU_EC_U_A0)
		sky2_write16(hw, Q_ADDR(txqaddr[port], Q_AL), ECU_TXFF_LEV);

	sky2_prefetch_init(hw, txqaddr[port], sky2->tx_le_map,
			   sky2->tx_ring_size - 1);

	sky2_vlan_mode(sky2->netdev, sky2->netdev->features);
	netdev_update_features(sky2->netdev);

	sky2_rx_start(sky2);
}

/* Bring up network interface. */
static int sky2_up(struct net_device *dev)
{
	struct sky2_port *sky2 = netdev_priv(dev);
	struct sky2_hw *hw = sky2->hw;
	unsigned port = sky2->port;
	u32 imask;
	int err;

	netif_carrier_off(dev);

	err = sky2_alloc_buffers(sky2);
	if (err)
		goto err_out;

	sky2_hw_up(sky2);

	/* Enable interrupts from phy/mac for port */
	imask = sky2_read32(hw, B0_IMSK);
	imask |= portirq_msk[port];
	sky2_write32(hw, B0_IMSK, imask);
	sky2_read32(hw, B0_IMSK);

	netif_info(sky2, ifup, dev, "enabling interface\n");

	return 0;

err_out:
	sky2_free_buffers(sky2);
	return err;
}

/* Modular subtraction in ring */
static inline int tx_inuse(const struct sky2_port *sky2)
{
	return (sky2->tx_prod - sky2->tx_cons) & (sky2->tx_ring_size - 1);
}

/* Number of list elements available for next tx */
static inline int tx_avail(const struct sky2_port *sky2)
{
	return sky2->tx_pending - tx_inuse(sky2);
}

/* Estimate of number of transmit list elements required */
static unsigned tx_le_req(const struct sk_buff *skb)
{
	unsigned count;

	count = (skb_shinfo(skb)->nr_frags + 1)
		* (sizeof(dma_addr_t) / sizeof(u32));

	if (skb_is_gso(skb))
		++count;
	else if (sizeof(dma_addr_t) == sizeof(u32))
		++count;	/* possible vlan */

	if (skb->ip_summed == CHECKSUM_PARTIAL)
		++count;

	return count;
}

static void sky2_tx_unmap(struct pci_dev *pdev, struct tx_ring_info *re)
{
	if (re->flags & TX_MAP_SINGLE)
		pci_unmap_single(pdev, dma_unmap_addr(re, mapaddr),
				 dma_unmap_len(re, maplen),
				 PCI_DMA_TODEVICE);
	else if (re->flags & TX_MAP_PAGE)
		pci_unmap_page(pdev, dma_unmap_addr(re, mapaddr),
			       dma_unmap_len(re, maplen),
			       PCI_DMA_TODEVICE);
	re->flags = 0;
}

/*
 * Put one packet in ring for transmit.
 * A single packet can generate multiple list elements, and
 * the number of ring elements will probably be less than the number
 * of list elements used.
 */
static netdev_tx_t sky2_xmit_frame(struct sk_buff *skb,
				   struct net_device *dev)
{
	struct sky2_port *sky2 = netdev_priv(dev);
	struct sky2_hw *hw = sky2->hw;
	struct sky2_tx_le *le = NULL;
	struct tx_ring_info *re;
	unsigned i, len;
	dma_addr_t mapping;
	u32 upper;
	u16 slot;
	u16 mss;
	u8 ctrl;

 	if (unlikely(tx_avail(sky2) < tx_le_req(skb)))
  		return NETDEV_TX_BUSY;

	len = skb_headlen(skb);
	mapping = pci_map_single(hw->pdev, skb->data, len, PCI_DMA_TODEVICE);

	if (pci_dma_mapping_error(hw->pdev, mapping))
		goto mapping_error;

	slot = sky2->tx_prod;
	netif_printk(sky2, tx_queued, KERN_DEBUG, dev,
		     "tx queued, slot %u, len %d\n", slot, skb->len);

	/* Send high bits if needed */
	upper = upper_32_bits(mapping);
	if (upper != sky2->tx_last_upper) {
		le = get_tx_le(sky2, &slot);
		le->addr = cpu_to_le32(upper);
		sky2->tx_last_upper = upper;
		le->opcode = OP_ADDR64 | HW_OWNER;
	}

	/* Check for TCP Segmentation Offload */
	mss = skb_shinfo(skb)->gso_size;
	if (mss != 0) {

		if (!(hw->flags & SKY2_HW_NEW_LE))
			mss += ETH_HLEN + ip_hdrlen(skb) + tcp_hdrlen(skb);

  		if (mss != sky2->tx_last_mss) {
			le = get_tx_le(sky2, &slot);
  			le->addr = cpu_to_le32(mss);

			if (hw->flags & SKY2_HW_NEW_LE)
				le->opcode = OP_MSS | HW_OWNER;
			else
				le->opcode = OP_LRGLEN | HW_OWNER;
			sky2->tx_last_mss = mss;
		}
	}

	ctrl = 0;

	/* Add VLAN tag, can piggyback on LRGLEN or ADDR64 */
	if (vlan_tx_tag_present(skb)) {
		if (!le) {
			le = get_tx_le(sky2, &slot);
			le->addr = 0;
			le->opcode = OP_VLAN|HW_OWNER;
		} else
			le->opcode |= OP_VLAN;
		le->length = cpu_to_be16(vlan_tx_tag_get(skb));
		ctrl |= INS_VLAN;
	}

	/* Handle TCP checksum offload */
	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		/* On Yukon EX (some versions) encoding change. */
 		if (hw->flags & SKY2_HW_AUTO_TX_SUM)
 			ctrl |= CALSUM;	/* auto checksum */
		else {
			const unsigned offset = skb_transport_offset(skb);
			u32 tcpsum;

			tcpsum = offset << 16;			/* sum start */
			tcpsum |= offset + skb->csum_offset;	/* sum write */

			ctrl |= CALSUM | WR_SUM | INIT_SUM | LOCK_SUM;
			if (ip_hdr(skb)->protocol == IPPROTO_UDP)
				ctrl |= UDPTCP;

			if (tcpsum != sky2->tx_tcpsum) {
				sky2->tx_tcpsum = tcpsum;

				le = get_tx_le(sky2, &slot);
				le->addr = cpu_to_le32(tcpsum);
				le->length = 0;	/* initial checksum value */
				le->ctrl = 1;	/* one packet */
				le->opcode = OP_TCPLISW | HW_OWNER;
			}
		}
	}

	re = sky2->tx_ring + slot;
	re->flags = TX_MAP_SINGLE;
	dma_unmap_addr_set(re, mapaddr, mapping);
	dma_unmap_len_set(re, maplen, len);

	le = get_tx_le(sky2, &slot);
	le->addr = cpu_to_le32(lower_32_bits(mapping));
	le->length = cpu_to_le16(len);
	le->ctrl = ctrl;
	le->opcode = mss ? (OP_LARGESEND | HW_OWNER) : (OP_PACKET | HW_OWNER);


	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		const skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

		mapping = pci_map_page(hw->pdev, frag->page, frag->page_offset,
				       frag->size, PCI_DMA_TODEVICE);

		if (pci_dma_mapping_error(hw->pdev, mapping))
			goto mapping_unwind;

		upper = upper_32_bits(mapping);
		if (upper != sky2->tx_last_upper) {
			le = get_tx_le(sky2, &slot);
			le->addr = cpu_to_le32(upper);
			sky2->tx_last_upper = upper;
			le->opcode = OP_ADDR64 | HW_OWNER;
		}

		re = sky2->tx_ring + slot;
		re->flags = TX_MAP_PAGE;
		dma_unmap_addr_set(re, mapaddr, mapping);
		dma_unmap_len_set(re, maplen, frag->size);

		le = get_tx_le(sky2, &slot);
		le->addr = cpu_to_le32(lower_32_bits(mapping));
		le->length = cpu_to_le16(frag->size);
		le->ctrl = ctrl;
		le->opcode = OP_BUFFER | HW_OWNER;
	}

	re->skb = skb;
	le->ctrl |= EOP;

	sky2->tx_prod = slot;

	if (tx_avail(sky2) <= MAX_SKB_TX_LE)
		netif_stop_queue(dev);

	sky2_put_idx(hw, txqaddr[sky2->port], sky2->tx_prod);

	return NETDEV_TX_OK;

mapping_unwind:
	for (i = sky2->tx_prod; i != slot; i = RING_NEXT(i, sky2->tx_ring_size)) {
		re = sky2->tx_ring + i;

		sky2_tx_unmap(hw->pdev, re);
	}

mapping_error:
	if (net_ratelimit())
		dev_warn(&hw->pdev->dev, "%s: tx mapping error\n", dev->name);
	dev_kfree_skb(skb);
	return NETDEV_TX_OK;
}

/*
 * Free ring elements from starting at tx_cons until "done"
 *
 * NB:
 *  1. The hardware will tell us about partial completion of multi-part
 *     buffers so make sure not to free skb to early.
 *  2. This may run in parallel start_xmit because the it only
 *     looks at the tail of the queue of FIFO (tx_cons), not
 *     the head (tx_prod)
 */
static void sky2_tx_complete(struct sky2_port *sky2, u16 done)
{
	struct net_device *dev = sky2->netdev;
	unsigned idx;

	BUG_ON(done >= sky2->tx_ring_size);

	for (idx = sky2->tx_cons; idx != done;
	     idx = RING_NEXT(idx, sky2->tx_ring_size)) {
		struct tx_ring_info *re = sky2->tx_ring + idx;
		struct sk_buff *skb = re->skb;

		sky2_tx_unmap(sky2->hw->pdev, re);

		if (skb) {
			netif_printk(sky2, tx_done, KERN_DEBUG, dev,
				     "tx done %u\n", idx);

			u64_stats_update_begin(&sky2->tx_stats.syncp);
			++sky2->tx_stats.packets;
			sky2->tx_stats.bytes += skb->len;
			u64_stats_update_end(&sky2->tx_stats.syncp);

			re->skb = NULL;
			dev_kfree_skb_any(skb);

			sky2->tx_next = RING_NEXT(idx, sky2->tx_ring_size);
		}
	}

	sky2->tx_cons = idx;
	smp_mb();
}

static void sky2_tx_reset(struct sky2_hw *hw, unsigned port)
{
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
	sky2_write8(hw, SK_REG(port, TX_GMF_CTRL_T), GMF_RST_SET);
}

static void sky2_hw_down(struct sky2_port *sky2)
{
	struct sky2_hw *hw = sky2->hw;
	unsigned port = sky2->port;
	u16 ctrl;

	/* Force flow control off */
	sky2_write8(hw, SK_REG(port, GMAC_CTRL), GMC_PAUSE_OFF);

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
	if (!(hw->chip_id == CHIP_ID_YUKON_XL && hw->chip_rev == 0 &&
	      port == 0 && hw->dev[1] && netif_running(hw->dev[1])))
		sky2_write8(hw, SK_REG(port, GMAC_CTRL), GMC_RST_SET);

	sky2_write8(hw, SK_REG(port, RX_GMF_CTRL_T), GMF_RST_SET);

	/* Force any delayed status interrrupt and NAPI */
	sky2_write32(hw, STAT_LEV_TIMER_CNT, 0);
	sky2_write32(hw, STAT_TX_TIMER_CNT, 0);
	sky2_write32(hw, STAT_ISR_TIMER_CNT, 0);
	sky2_read8(hw, STAT_ISR_TIMER_CTRL);

	sky2_rx_stop(sky2);

	spin_lock_bh(&sky2->phy_lock);
	sky2_phy_power_down(hw, port);
	spin_unlock_bh(&sky2->phy_lock);

	sky2_tx_reset(hw, port);

	/* Free any pending frames stuck in HW queue */
	sky2_tx_complete(sky2, sky2->tx_prod);
}

/* Network shutdown */
static int sky2_down(struct net_device *dev)
{
	struct sky2_port *sky2 = netdev_priv(dev);
	struct sky2_hw *hw = sky2->hw;

	/* Never really got started! */
	if (!sky2->tx_le)
		return 0;

	netif_info(sky2, ifdown, dev, "disabling interface\n");

	/* Disable port IRQ */
	sky2_write32(hw, B0_IMSK,
		     sky2_read32(hw, B0_IMSK) & ~portirq_msk[sky2->port]);
	sky2_read32(hw, B0_IMSK);

	synchronize_irq(hw->pdev->irq);
	napi_synchronize(&hw->napi);

	sky2_hw_down(sky2);

	sky2_free_buffers(sky2);

	return 0;
}

static u16 sky2_phy_speed(const struct sky2_hw *hw, u16 aux)
{
	if (hw->flags & SKY2_HW_FIBRE_PHY)
		return SPEED_1000;

	if (!(hw->flags & SKY2_HW_GIGABIT)) {
		if (aux & PHY_M_PS_SPEED_100)
			return SPEED_100;
		else
			return SPEED_10;
	}

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
	static const char *fc_name[] = {
		[FC_NONE]	= "none",
		[FC_TX]		= "tx",
		[FC_RX]		= "rx",
		[FC_BOTH]	= "both",
	};

	sky2_enable_rx_tx(sky2);

	gm_phy_write(hw, port, PHY_MARV_INT_MASK, PHY_M_DEF_MSK);

	netif_carrier_on(sky2->netdev);

	mod_timer(&hw->watchdog_timer, jiffies + 1);

	/* Turn on link LED */
	sky2_write8(hw, SK_REG(port, LNK_LED_REG),
		    LINKLED_ON | LINKLED_BLINK_OFF | LINKLED_LINKSYNC_OFF);

	netif_info(sky2, link, sky2->netdev,
		   "Link is up at %d Mbps, %s duplex, flow control %s\n",
		   sky2->speed,
		   sky2->duplex == DUPLEX_FULL ? "full" : "half",
		   fc_name[sky2->flow_status]);
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

	netif_carrier_off(sky2->netdev);

	/* Turn off link LED */
	sky2_write8(hw, SK_REG(port, LNK_LED_REG), LINKLED_OFF);

	netif_info(sky2, link, sky2->netdev, "Link is down\n");

	sky2_phy_init(hw, port);
}

static enum flow_control sky2_flow(int rx, int tx)
{
	if (rx)
		return tx ? FC_BOTH : FC_RX;
	else
		return tx ? FC_TX : FC_NONE;
}

static int sky2_autoneg_done(struct sky2_port *sky2, u16 aux)
{
	struct sky2_hw *hw = sky2->hw;
	unsigned port = sky2->port;
	u16 advert, lpa;

	advert = gm_phy_read(hw, port, PHY_MARV_AUNE_ADV);
	lpa = gm_phy_read(hw, port, PHY_MARV_AUNE_LP);
	if (lpa & PHY_M_AN_RF) {
		netdev_err(sky2->netdev, "remote fault\n");
		return -1;
	}

	if (!(aux & PHY_M_PS_SPDUP_RES)) {
		netdev_err(sky2->netdev, "speed/duplex mismatch\n");
		return -1;
	}

	sky2->speed = sky2_phy_speed(hw, aux);
	sky2->duplex = (aux & PHY_M_PS_FULL_DUP) ? DUPLEX_FULL : DUPLEX_HALF;

	/* Since the pause result bits seem to in different positions on
	 * different chips. look at registers.
	 */
	if (hw->flags & SKY2_HW_FIBRE_PHY) {
		/* Shift for bits in fiber PHY */
		advert &= ~(ADVERTISE_PAUSE_CAP|ADVERTISE_PAUSE_ASYM);
		lpa &= ~(LPA_PAUSE_CAP|LPA_PAUSE_ASYM);

		if (advert & ADVERTISE_1000XPAUSE)
			advert |= ADVERTISE_PAUSE_CAP;
		if (advert & ADVERTISE_1000XPSE_ASYM)
			advert |= ADVERTISE_PAUSE_ASYM;
		if (lpa & LPA_1000XPAUSE)
			lpa |= LPA_PAUSE_CAP;
		if (lpa & LPA_1000XPAUSE_ASYM)
			lpa |= LPA_PAUSE_ASYM;
	}

	sky2->flow_status = FC_NONE;
	if (advert & ADVERTISE_PAUSE_CAP) {
		if (lpa & LPA_PAUSE_CAP)
			sky2->flow_status = FC_BOTH;
		else if (advert & ADVERTISE_PAUSE_ASYM)
			sky2->flow_status = FC_RX;
	} else if (advert & ADVERTISE_PAUSE_ASYM) {
		if ((lpa & LPA_PAUSE_CAP) && (lpa & LPA_PAUSE_ASYM))
			sky2->flow_status = FC_TX;
	}

	if (sky2->duplex == DUPLEX_HALF && sky2->speed < SPEED_1000 &&
	    !(hw->chip_id == CHIP_ID_YUKON_EC_U || hw->chip_id == CHIP_ID_YUKON_EX))
		sky2->flow_status = FC_NONE;

	if (sky2->flow_status & FC_TX)
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

	if (!netif_running(dev))
		return;

	spin_lock(&sky2->phy_lock);
	istatus = gm_phy_read(hw, port, PHY_MARV_INT_STAT);
	phystat = gm_phy_read(hw, port, PHY_MARV_PHY_STAT);

	netif_info(sky2, intr, sky2->netdev, "phy interrupt status 0x%x 0x%x\n",
		   istatus, phystat);

	if (istatus & PHY_M_IS_AN_COMPL) {
		if (sky2_autoneg_done(sky2, phystat) == 0 &&
		    !netif_carrier_ok(dev))
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

/* Special quick link interrupt (Yukon-2 Optima only) */
static void sky2_qlink_intr(struct sky2_hw *hw)
{
	struct sky2_port *sky2 = netdev_priv(hw->dev[0]);
	u32 imask;
	u16 phy;

	/* disable irq */
	imask = sky2_read32(hw, B0_IMSK);
	imask &= ~Y2_IS_PHY_QLNK;
	sky2_write32(hw, B0_IMSK, imask);

	/* reset PHY Link Detect */
	phy = sky2_pci_read16(hw, PSM_CONFIG_REG4);
	sky2_write8(hw, B2_TST_CTRL1, TST_CFG_WRITE_ON);
	sky2_pci_write16(hw, PSM_CONFIG_REG4, phy | 1);
	sky2_write8(hw, B2_TST_CTRL1, TST_CFG_WRITE_OFF);

	sky2_link_up(sky2);
}

/* Transmit timeout is only called if we are running, carrier is up
 * and tx queue is full (stopped).
 */
static void sky2_tx_timeout(struct net_device *dev)
{
	struct sky2_port *sky2 = netdev_priv(dev);
	struct sky2_hw *hw = sky2->hw;

	netif_err(sky2, timer, dev, "tx timeout\n");

	netdev_printk(KERN_DEBUG, dev, "transmit ring %u .. %u report=%u done=%u\n",
		      sky2->tx_cons, sky2->tx_prod,
		      sky2_read16(hw, sky2->port == 0 ? STAT_TXA1_RIDX : STAT_TXA2_RIDX),
		      sky2_read16(hw, Q_ADDR(txqaddr[sky2->port], Q_DONE)));

	/* can't restart safely under softirq */
	schedule_work(&hw->restart_work);
}

static int sky2_change_mtu(struct net_device *dev, int new_mtu)
{
	struct sky2_port *sky2 = netdev_priv(dev);
	struct sky2_hw *hw = sky2->hw;
	unsigned port = sky2->port;
	int err;
	u16 ctl, mode;
	u32 imask;

	/* MTU size outside the spec */
	if (new_mtu < ETH_ZLEN || new_mtu > ETH_JUMBO_MTU)
		return -EINVAL;

	/* MTU > 1500 on yukon FE and FE+ not allowed */
	if (new_mtu > ETH_DATA_LEN &&
	    (hw->chip_id == CHIP_ID_YUKON_FE ||
	     hw->chip_id == CHIP_ID_YUKON_FE_P))
		return -EINVAL;

	if (!netif_running(dev)) {
		dev->mtu = new_mtu;
		netdev_update_features(dev);
		return 0;
	}

	imask = sky2_read32(hw, B0_IMSK);
	sky2_write32(hw, B0_IMSK, 0);

	dev->trans_start = jiffies;	/* prevent tx timeout */
	napi_disable(&hw->napi);
	netif_tx_disable(dev);

	synchronize_irq(hw->pdev->irq);

	if (!(hw->flags & SKY2_HW_RAM_BUFFER))
		sky2_set_tx_stfwd(hw, port);

	ctl = gma_read16(hw, port, GM_GP_CTRL);
	gma_write16(hw, port, GM_GP_CTRL, ctl & ~GM_GPCR_RX_ENA);
	sky2_rx_stop(sky2);
	sky2_rx_clean(sky2);

	dev->mtu = new_mtu;
	netdev_update_features(dev);

	mode = DATA_BLIND_VAL(DATA_BLIND_DEF) |
		GM_SMOD_VLAN_ENA | IPG_DATA_VAL(IPG_DATA_DEF);

	if (dev->mtu > ETH_DATA_LEN)
		mode |= GM_SMOD_JUMBO_ENA;

	gma_write16(hw, port, GM_SERIAL_MODE, mode);

	sky2_write8(hw, RB_ADDR(rxqaddr[port], RB_CTRL), RB_ENA_OP_MD);

	err = sky2_alloc_rx_skbs(sky2);
	if (!err)
		sky2_rx_start(sky2);
	else
		sky2_rx_clean(sky2);
	sky2_write32(hw, B0_IMSK, imask);

	sky2_read32(hw, B0_Y2_SP_LISR);
	napi_enable(&hw->napi);

	if (err)
		dev_close(dev);
	else {
		gma_write16(hw, port, GM_GP_CTRL, ctl);

		netif_wake_queue(dev);
	}

	return err;
}

/* For small just reuse existing skb for next receive */
static struct sk_buff *receive_copy(struct sky2_port *sky2,
				    const struct rx_ring_info *re,
				    unsigned length)
{
	struct sk_buff *skb;

	skb = netdev_alloc_skb_ip_align(sky2->netdev, length);
	if (likely(skb)) {
		pci_dma_sync_single_for_cpu(sky2->hw->pdev, re->data_addr,
					    length, PCI_DMA_FROMDEVICE);
		skb_copy_from_linear_data(re->skb, skb->data, length);
		skb->ip_summed = re->skb->ip_summed;
		skb->csum = re->skb->csum;
		skb->rxhash = re->skb->rxhash;
		skb->vlan_tci = re->skb->vlan_tci;

		pci_dma_sync_single_for_device(sky2->hw->pdev, re->data_addr,
					       length, PCI_DMA_FROMDEVICE);
		re->skb->vlan_tci = 0;
		re->skb->rxhash = 0;
		re->skb->ip_summed = CHECKSUM_NONE;
		skb_put(skb, length);
	}
	return skb;
}

/* Adjust length of skb with fragments to match received data */
static void skb_put_frags(struct sk_buff *skb, unsigned int hdr_space,
			  unsigned int length)
{
	int i, num_frags;
	unsigned int size;

	/* put header into skb */
	size = min(length, hdr_space);
	skb->tail += size;
	skb->len += size;
	length -= size;

	num_frags = skb_shinfo(skb)->nr_frags;
	for (i = 0; i < num_frags; i++) {
		skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

		if (length == 0) {
			/* don't need this page */
			__free_page(frag->page);
			--skb_shinfo(skb)->nr_frags;
		} else {
			size = min(length, (unsigned) PAGE_SIZE);

			frag->size = size;
			skb->data_len += size;
			skb->truesize += size;
			skb->len += size;
			length -= size;
		}
	}
}

/* Normal packet - take skb from ring element and put in a new one  */
static struct sk_buff *receive_new(struct sky2_port *sky2,
				   struct rx_ring_info *re,
				   unsigned int length)
{
	struct sk_buff *skb;
	struct rx_ring_info nre;
	unsigned hdr_space = sky2->rx_data_size;

	nre.skb = sky2_rx_alloc(sky2);
	if (unlikely(!nre.skb))
		goto nobuf;

	if (sky2_rx_map_skb(sky2->hw->pdev, &nre, hdr_space))
		goto nomap;

	skb = re->skb;
	sky2_rx_unmap_skb(sky2->hw->pdev, re);
	prefetch(skb->data);
	*re = nre;

	if (skb_shinfo(skb)->nr_frags)
		skb_put_frags(skb, hdr_space, length);
	else
		skb_put(skb, length);
	return skb;

nomap:
	dev_kfree_skb(nre.skb);
nobuf:
	return NULL;
}

/*
 * Receive one packet.
 * For larger packets, get new buffer.
 */
static struct sk_buff *sky2_receive(struct net_device *dev,
				    u16 length, u32 status)
{
 	struct sky2_port *sky2 = netdev_priv(dev);
	struct rx_ring_info *re = sky2->rx_ring + sky2->rx_next;
	struct sk_buff *skb = NULL;
	u16 count = (status & GMR_FS_LEN) >> 16;

	netif_printk(sky2, rx_status, KERN_DEBUG, dev,
		     "rx slot %u status 0x%x len %d\n",
		     sky2->rx_next, status, length);

	sky2->rx_next = (sky2->rx_next + 1) % sky2->rx_pending;
	prefetch(sky2->rx_ring + sky2->rx_next);

	if (vlan_tx_tag_present(re->skb))
		count -= VLAN_HLEN;	/* Account for vlan tag */

	/* This chip has hardware problems that generates bogus status.
	 * So do only marginal checking and expect higher level protocols
	 * to handle crap frames.
	 */
	if (sky2->hw->chip_id == CHIP_ID_YUKON_FE_P &&
	    sky2->hw->chip_rev == CHIP_REV_YU_FE2_A0 &&
	    length != count)
		goto okay;

	if (status & GMR_FS_ANY_ERR)
		goto error;

	if (!(status & GMR_FS_RX_OK))
		goto resubmit;

	/* if length reported by DMA does not match PHY, packet was truncated */
	if (length != count)
		goto error;

okay:
	if (length < copybreak)
		skb = receive_copy(sky2, re, length);
	else
		skb = receive_new(sky2, re, length);

	dev->stats.rx_dropped += (skb == NULL);

resubmit:
	sky2_rx_submit(sky2, re);

	return skb;

error:
	++dev->stats.rx_errors;

	if (net_ratelimit())
		netif_info(sky2, rx_err, dev,
			   "rx error, status 0x%x length %d\n", status, length);

	goto resubmit;
}

/* Transmit complete */
static inline void sky2_tx_done(struct net_device *dev, u16 last)
{
	struct sky2_port *sky2 = netdev_priv(dev);

	if (netif_running(dev)) {
		sky2_tx_complete(sky2, last);

		/* Wake unless it's detached, and called e.g. from sky2_down() */
		if (tx_avail(sky2) > MAX_SKB_TX_LE + 4)
			netif_wake_queue(dev);
	}
}

static inline void sky2_skb_rx(const struct sky2_port *sky2,
			       struct sk_buff *skb)
{
	if (skb->ip_summed == CHECKSUM_NONE)
		netif_receive_skb(skb);
	else
		napi_gro_receive(&sky2->hw->napi, skb);
}

static inline void sky2_rx_done(struct sky2_hw *hw, unsigned port,
				unsigned packets, unsigned bytes)
{
	struct net_device *dev = hw->dev[port];
	struct sky2_port *sky2 = netdev_priv(dev);

	if (packets == 0)
		return;

	u64_stats_update_begin(&sky2->rx_stats.syncp);
	sky2->rx_stats.packets += packets;
	sky2->rx_stats.bytes += bytes;
	u64_stats_update_end(&sky2->rx_stats.syncp);

	dev->last_rx = jiffies;
	sky2_rx_update(netdev_priv(dev), rxqaddr[port]);
}

static void sky2_rx_checksum(struct sky2_port *sky2, u32 status)
{
	/* If this happens then driver assuming wrong format for chip type */
	BUG_ON(sky2->hw->flags & SKY2_HW_NEW_LE);

	/* Both checksum counters are programmed to start at
	 * the same offset, so unless there is a problem they
	 * should match. This failure is an early indication that
	 * hardware receive checksumming won't work.
	 */
	if (likely((u16)(status >> 16) == (u16)status)) {
		struct sk_buff *skb = sky2->rx_ring[sky2->rx_next].skb;
		skb->ip_summed = CHECKSUM_COMPLETE;
		skb->csum = le16_to_cpu(status);
	} else {
		dev_notice(&sky2->hw->pdev->dev,
			   "%s: receive checksum problem (status = %#x)\n",
			   sky2->netdev->name, status);

		/* Disable checksum offload
		 * It will be reenabled on next ndo_set_features, but if it's
		 * really broken, will get disabled again
		 */
		sky2->netdev->features &= ~NETIF_F_RXCSUM;
		sky2_write32(sky2->hw, Q_ADDR(rxqaddr[sky2->port], Q_CSR),
			     BMU_DIS_RX_CHKSUM);
	}
}

static void sky2_rx_tag(struct sky2_port *sky2, u16 length)
{
	struct sk_buff *skb;

	skb = sky2->rx_ring[sky2->rx_next].skb;
	__vlan_hwaccel_put_tag(skb, be16_to_cpu(length));
}

static void sky2_rx_hash(struct sky2_port *sky2, u32 status)
{
	struct sk_buff *skb;

	skb = sky2->rx_ring[sky2->rx_next].skb;
	skb->rxhash = le32_to_cpu(status);
}

/* Process status response ring */
static int sky2_status_intr(struct sky2_hw *hw, int to_do, u16 idx)
{
	int work_done = 0;
	unsigned int total_bytes[2] = { 0 };
	unsigned int total_packets[2] = { 0 };

	rmb();
	do {
		struct sky2_port *sky2;
		struct sky2_status_le *le  = hw->st_le + hw->st_idx;
		unsigned port;
		struct net_device *dev;
		struct sk_buff *skb;
		u32 status;
		u16 length;
		u8 opcode = le->opcode;

		if (!(opcode & HW_OWNER))
			break;

		hw->st_idx = RING_NEXT(hw->st_idx, hw->st_size);

		port = le->css & CSS_LINK_BIT;
		dev = hw->dev[port];
		sky2 = netdev_priv(dev);
		length = le16_to_cpu(le->length);
		status = le32_to_cpu(le->status);

		le->opcode = 0;
		switch (opcode & ~HW_OWNER) {
		case OP_RXSTAT:
			total_packets[port]++;
			total_bytes[port] += length;

			skb = sky2_receive(dev, length, status);
			if (!skb)
				break;

			/* This chip reports checksum status differently */
			if (hw->flags & SKY2_HW_NEW_LE) {
				if ((dev->features & NETIF_F_RXCSUM) &&
				    (le->css & (CSS_ISIPV4 | CSS_ISIPV6)) &&
				    (le->css & CSS_TCPUDPCSOK))
					skb->ip_summed = CHECKSUM_UNNECESSARY;
				else
					skb->ip_summed = CHECKSUM_NONE;
			}

			skb->protocol = eth_type_trans(skb, dev);
			sky2_skb_rx(sky2, skb);

			/* Stop after net poll weight */
			if (++work_done >= to_do)
				goto exit_loop;
			break;

		case OP_RXVLAN:
			sky2_rx_tag(sky2, length);
			break;

		case OP_RXCHKSVLAN:
			sky2_rx_tag(sky2, length);
			/* fall through */
		case OP_RXCHKS:
			if (likely(dev->features & NETIF_F_RXCSUM))
				sky2_rx_checksum(sky2, status);
			break;

		case OP_RSS_HASH:
			sky2_rx_hash(sky2, status);
			break;

		case OP_TXINDEXLE:
			/* TX index reports status for both ports */
			sky2_tx_done(hw->dev[0], status & 0xfff);
			if (hw->dev[1])
				sky2_tx_done(hw->dev[1],
				     ((status >> 24) & 0xff)
					     | (u16)(length & 0xf) << 8);
			break;

		default:
			if (net_ratelimit())
				pr_warning("unknown status opcode 0x%x\n", opcode);
		}
	} while (hw->st_idx != idx);

	/* Fully processed status ring so clear irq */
	sky2_write32(hw, STAT_CTRL, SC_STAT_CLR_IRQ);

exit_loop:
	sky2_rx_done(hw, 0, total_packets[0], total_bytes[0]);
	sky2_rx_done(hw, 1, total_packets[1], total_bytes[1]);

	return work_done;
}

static void sky2_hw_error(struct sky2_hw *hw, unsigned port, u32 status)
{
	struct net_device *dev = hw->dev[port];

	if (net_ratelimit())
		netdev_info(dev, "hw error interrupt status 0x%x\n", status);

	if (status & Y2_IS_PAR_RD1) {
		if (net_ratelimit())
			netdev_err(dev, "ram data read parity error\n");
		/* Clear IRQ */
		sky2_write16(hw, RAM_BUFFER(port, B3_RI_CTRL), RI_CLR_RD_PERR);
	}

	if (status & Y2_IS_PAR_WR1) {
		if (net_ratelimit())
			netdev_err(dev, "ram data write parity error\n");

		sky2_write16(hw, RAM_BUFFER(port, B3_RI_CTRL), RI_CLR_WR_PERR);
	}

	if (status & Y2_IS_PAR_MAC1) {
		if (net_ratelimit())
			netdev_err(dev, "MAC parity error\n");
		sky2_write8(hw, SK_REG(port, TX_GMF_CTRL_T), GMF_CLI_TX_PE);
	}

	if (status & Y2_IS_PAR_RX1) {
		if (net_ratelimit())
			netdev_err(dev, "RX parity error\n");
		sky2_write32(hw, Q_ADDR(rxqaddr[port], Q_CSR), BMU_CLR_IRQ_PAR);
	}

	if (status & Y2_IS_TCP_TXA1) {
		if (net_ratelimit())
			netdev_err(dev, "TCP segmentation error\n");
		sky2_write32(hw, Q_ADDR(txqaddr[port], Q_CSR), BMU_CLR_IRQ_TCP);
	}
}

static void sky2_hw_intr(struct sky2_hw *hw)
{
	struct pci_dev *pdev = hw->pdev;
	u32 status = sky2_read32(hw, B0_HWE_ISRC);
	u32 hwmsk = sky2_read32(hw, B0_HWE_IMSK);

	status &= hwmsk;

	if (status & Y2_IS_TIST_OV)
		sky2_write8(hw, GMAC_TI_ST_CTRL, GMT_ST_CLR_IRQ);

	if (status & (Y2_IS_MST_ERR | Y2_IS_IRQ_STAT)) {
		u16 pci_err;

		sky2_write8(hw, B2_TST_CTRL1, TST_CFG_WRITE_ON);
		pci_err = sky2_pci_read16(hw, PCI_STATUS);
		if (net_ratelimit())
			dev_err(&pdev->dev, "PCI hardware error (0x%x)\n",
			        pci_err);

		sky2_pci_write16(hw, PCI_STATUS,
				      pci_err | PCI_STATUS_ERROR_BITS);
		sky2_write8(hw, B2_TST_CTRL1, TST_CFG_WRITE_OFF);
	}

	if (status & Y2_IS_PCI_EXP) {
		/* PCI-Express uncorrectable Error occurred */
		u32 err;

		sky2_write8(hw, B2_TST_CTRL1, TST_CFG_WRITE_ON);
		err = sky2_read32(hw, Y2_CFG_AER + PCI_ERR_UNCOR_STATUS);
		sky2_write32(hw, Y2_CFG_AER + PCI_ERR_UNCOR_STATUS,
			     0xfffffffful);
		if (net_ratelimit())
			dev_err(&pdev->dev, "PCI Express error (0x%x)\n", err);

		sky2_read32(hw, Y2_CFG_AER + PCI_ERR_UNCOR_STATUS);
		sky2_write8(hw, B2_TST_CTRL1, TST_CFG_WRITE_OFF);
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

	netif_info(sky2, intr, dev, "mac interrupt status 0x%x\n", status);

	if (status & GM_IS_RX_CO_OV)
		gma_read16(hw, port, GM_RX_IRQ_SRC);

	if (status & GM_IS_TX_CO_OV)
		gma_read16(hw, port, GM_TX_IRQ_SRC);

	if (status & GM_IS_RX_FF_OR) {
		++dev->stats.rx_fifo_errors;
		sky2_write8(hw, SK_REG(port, RX_GMF_CTRL_T), GMF_CLI_RX_FO);
	}

	if (status & GM_IS_TX_FF_UR) {
		++dev->stats.tx_fifo_errors;
		sky2_write8(hw, SK_REG(port, TX_GMF_CTRL_T), GMF_CLI_TX_FU);
	}
}

/* This should never happen it is a bug. */
static void sky2_le_error(struct sky2_hw *hw, unsigned port, u16 q)
{
	struct net_device *dev = hw->dev[port];
	u16 idx = sky2_read16(hw, Y2_QADDR(q, PREF_UNIT_GET_IDX));

	dev_err(&hw->pdev->dev, "%s: descriptor error q=%#x get=%u put=%u\n",
		dev->name, (unsigned) q, (unsigned) idx,
		(unsigned) sky2_read16(hw, Y2_QADDR(q, PREF_UNIT_PUT_IDX)));

	sky2_write32(hw, Q_ADDR(q, Q_CSR), BMU_CLR_IRQ_CHK);
}

static int sky2_rx_hung(struct net_device *dev)
{
	struct sky2_port *sky2 = netdev_priv(dev);
	struct sky2_hw *hw = sky2->hw;
	unsigned port = sky2->port;
	unsigned rxq = rxqaddr[port];
	u32 mac_rp = sky2_read32(hw, SK_REG(port, RX_GMF_RP));
	u8 mac_lev = sky2_read8(hw, SK_REG(port, RX_GMF_RLEV));
	u8 fifo_rp = sky2_read8(hw, Q_ADDR(rxq, Q_RP));
	u8 fifo_lev = sky2_read8(hw, Q_ADDR(rxq, Q_RL));

	/* If idle and MAC or PCI is stuck */
	if (sky2->check.last == dev->last_rx &&
	    ((mac_rp == sky2->check.mac_rp &&
	      mac_lev != 0 && mac_lev >= sky2->check.mac_lev) ||
	     /* Check if the PCI RX hang */
	     (fifo_rp == sky2->check.fifo_rp &&
	      fifo_lev != 0 && fifo_lev >= sky2->check.fifo_lev))) {
		netdev_printk(KERN_DEBUG, dev,
			      "hung mac %d:%d fifo %d (%d:%d)\n",
			      mac_lev, mac_rp, fifo_lev,
			      fifo_rp, sky2_read8(hw, Q_ADDR(rxq, Q_WP)));
		return 1;
	} else {
		sky2->check.last = dev->last_rx;
		sky2->check.mac_rp = mac_rp;
		sky2->check.mac_lev = mac_lev;
		sky2->check.fifo_rp = fifo_rp;
		sky2->check.fifo_lev = fifo_lev;
		return 0;
	}
}

static void sky2_watchdog(unsigned long arg)
{
	struct sky2_hw *hw = (struct sky2_hw *) arg;

	/* Check for lost IRQ once a second */
	if (sky2_read32(hw, B0_ISRC)) {
		napi_schedule(&hw->napi);
	} else {
		int i, active = 0;

		for (i = 0; i < hw->ports; i++) {
			struct net_device *dev = hw->dev[i];
			if (!netif_running(dev))
				continue;
			++active;

			/* For chips with Rx FIFO, check if stuck */
			if ((hw->flags & SKY2_HW_RAM_BUFFER) &&
			     sky2_rx_hung(dev)) {
				netdev_info(dev, "receiver hang detected\n");
				schedule_work(&hw->restart_work);
				return;
			}
		}

		if (active == 0)
			return;
	}

	mod_timer(&hw->watchdog_timer, round_jiffies(jiffies + HZ));
}

/* Hardware/software error handling */
static void sky2_err_intr(struct sky2_hw *hw, u32 status)
{
	if (net_ratelimit())
		dev_warn(&hw->pdev->dev, "error interrupt status=%#x\n", status);

	if (status & Y2_IS_HW_ERR)
		sky2_hw_intr(hw);

	if (status & Y2_IS_IRQ_MAC1)
		sky2_mac_intr(hw, 0);

	if (status & Y2_IS_IRQ_MAC2)
		sky2_mac_intr(hw, 1);

	if (status & Y2_IS_CHK_RX1)
		sky2_le_error(hw, 0, Q_R1);

	if (status & Y2_IS_CHK_RX2)
		sky2_le_error(hw, 1, Q_R2);

	if (status & Y2_IS_CHK_TXA1)
		sky2_le_error(hw, 0, Q_XA1);

	if (status & Y2_IS_CHK_TXA2)
		sky2_le_error(hw, 1, Q_XA2);
}

static int sky2_poll(struct napi_struct *napi, int work_limit)
{
	struct sky2_hw *hw = container_of(napi, struct sky2_hw, napi);
	u32 status = sky2_read32(hw, B0_Y2_SP_EISR);
	int work_done = 0;
	u16 idx;

	if (unlikely(status & Y2_IS_ERROR))
		sky2_err_intr(hw, status);

	if (status & Y2_IS_IRQ_PHY1)
		sky2_phy_intr(hw, 0);

	if (status & Y2_IS_IRQ_PHY2)
		sky2_phy_intr(hw, 1);

	if (status & Y2_IS_PHY_QLNK)
		sky2_qlink_intr(hw);

	while ((idx = sky2_read16(hw, STAT_PUT_IDX)) != hw->st_idx) {
		work_done += sky2_status_intr(hw, work_limit - work_done, idx);

		if (work_done >= work_limit)
			goto done;
	}

	napi_complete(napi);
	sky2_read32(hw, B0_Y2_SP_LISR);
done:

	return work_done;
}

static irqreturn_t sky2_intr(int irq, void *dev_id)
{
	struct sky2_hw *hw = dev_id;
	u32 status;

	/* Reading this mask interrupts as side effect */
	status = sky2_read32(hw, B0_Y2_SP_ISRC2);
	if (status == 0 || status == ~0) {
		sky2_write32(hw, B0_Y2_SP_ICR, 2);
		return IRQ_NONE;
	}

	prefetch(&hw->st_le[hw->st_idx]);

	napi_schedule(&hw->napi);

	return IRQ_HANDLED;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void sky2_netpoll(struct net_device *dev)
{
	struct sky2_port *sky2 = netdev_priv(dev);

	napi_schedule(&sky2->hw->napi);
}
#endif

/* Chip internal frequency for clock calculations */
static u32 sky2_mhz(const struct sky2_hw *hw)
{
	switch (hw->chip_id) {
	case CHIP_ID_YUKON_EC:
	case CHIP_ID_YUKON_EC_U:
	case CHIP_ID_YUKON_EX:
	case CHIP_ID_YUKON_SUPR:
	case CHIP_ID_YUKON_UL_2:
	case CHIP_ID_YUKON_OPT:
		return 125;

	case CHIP_ID_YUKON_FE:
		return 100;

	case CHIP_ID_YUKON_FE_P:
		return 50;

	case CHIP_ID_YUKON_XL:
		return 156;

	default:
		BUG();
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


static int __devinit sky2_init(struct sky2_hw *hw)
{
	u8 t8;

	/* Enable all clocks and check for bad PCI access */
	sky2_pci_write32(hw, PCI_DEV_REG3, 0);

	sky2_write8(hw, B0_CTST, CS_RST_CLR);

	hw->chip_id = sky2_read8(hw, B2_CHIP_ID);
	hw->chip_rev = (sky2_read8(hw, B2_MAC_CFG) & CFG_CHIP_R_MSK) >> 4;

	switch (hw->chip_id) {
	case CHIP_ID_YUKON_XL:
		hw->flags = SKY2_HW_GIGABIT | SKY2_HW_NEWER_PHY;
		if (hw->chip_rev < CHIP_REV_YU_XL_A2)
			hw->flags |= SKY2_HW_RSS_BROKEN;
		break;

	case CHIP_ID_YUKON_EC_U:
		hw->flags = SKY2_HW_GIGABIT
			| SKY2_HW_NEWER_PHY
			| SKY2_HW_ADV_POWER_CTL;
		break;

	case CHIP_ID_YUKON_EX:
		hw->flags = SKY2_HW_GIGABIT
			| SKY2_HW_NEWER_PHY
			| SKY2_HW_NEW_LE
			| SKY2_HW_ADV_POWER_CTL;

		/* New transmit checksum */
		if (hw->chip_rev != CHIP_REV_YU_EX_B0)
			hw->flags |= SKY2_HW_AUTO_TX_SUM;
		break;

	case CHIP_ID_YUKON_EC:
		/* This rev is really old, and requires untested workarounds */
		if (hw->chip_rev == CHIP_REV_YU_EC_A1) {
			dev_err(&hw->pdev->dev, "unsupported revision Yukon-EC rev A1\n");
			return -EOPNOTSUPP;
		}
		hw->flags = SKY2_HW_GIGABIT | SKY2_HW_RSS_BROKEN;
		break;

	case CHIP_ID_YUKON_FE:
		hw->flags = SKY2_HW_RSS_BROKEN;
		break;

	case CHIP_ID_YUKON_FE_P:
		hw->flags = SKY2_HW_NEWER_PHY
			| SKY2_HW_NEW_LE
			| SKY2_HW_AUTO_TX_SUM
			| SKY2_HW_ADV_POWER_CTL;

		/* The workaround for status conflicts VLAN tag detection. */
		if (hw->chip_rev == CHIP_REV_YU_FE2_A0)
			hw->flags |= SKY2_HW_VLAN_BROKEN;
		break;

	case CHIP_ID_YUKON_SUPR:
		hw->flags = SKY2_HW_GIGABIT
			| SKY2_HW_NEWER_PHY
			| SKY2_HW_NEW_LE
			| SKY2_HW_AUTO_TX_SUM
			| SKY2_HW_ADV_POWER_CTL;
		break;

	case CHIP_ID_YUKON_UL_2:
		hw->flags = SKY2_HW_GIGABIT
			| SKY2_HW_ADV_POWER_CTL;
		break;

	case CHIP_ID_YUKON_OPT:
		hw->flags = SKY2_HW_GIGABIT
			| SKY2_HW_NEW_LE
			| SKY2_HW_ADV_POWER_CTL;
		break;

	default:
		dev_err(&hw->pdev->dev, "unsupported chip type 0x%x\n",
			hw->chip_id);
		return -EOPNOTSUPP;
	}

	hw->pmd_type = sky2_read8(hw, B2_PMD_TYP);
	if (hw->pmd_type == 'L' || hw->pmd_type == 'S' || hw->pmd_type == 'P')
		hw->flags |= SKY2_HW_FIBRE_PHY;

	hw->ports = 1;
	t8 = sky2_read8(hw, B2_Y2_HW_RES);
	if ((t8 & CFG_DUAL_MAC_MSK) == CFG_DUAL_MAC_MSK) {
		if (!(sky2_read8(hw, B2_Y2_CLK_GATE) & Y2_STATUS_LNK2_INAC))
			++hw->ports;
	}

	if (sky2_read8(hw, B2_E_0))
		hw->flags |= SKY2_HW_RAM_BUFFER;

	return 0;
}

static void sky2_reset(struct sky2_hw *hw)
{
	struct pci_dev *pdev = hw->pdev;
	u16 status;
	int i, cap;
	u32 hwe_mask = Y2_HWE_ALL_MASK;

	/* disable ASF */
	if (hw->chip_id == CHIP_ID_YUKON_EX
	    || hw->chip_id == CHIP_ID_YUKON_SUPR) {
		sky2_write32(hw, CPU_WDOG, 0);
		status = sky2_read16(hw, HCU_CCSR);
		status &= ~(HCU_CCSR_AHB_RST | HCU_CCSR_CPU_RST_MODE |
			    HCU_CCSR_UC_STATE_MSK);
		/*
		 * CPU clock divider shouldn't be used because
		 * - ASF firmware may malfunction
		 * - Yukon-Supreme: Parallel FLASH doesn't support divided clocks
		 */
		status &= ~HCU_CCSR_CPU_CLK_DIVIDE_MSK;
		sky2_write16(hw, HCU_CCSR, status);
		sky2_write32(hw, CPU_WDOG, 0);
	} else
		sky2_write8(hw, B28_Y2_ASF_STAT_CMD, Y2_ASF_RESET);
	sky2_write16(hw, B0_CTST, Y2_ASF_DISABLE);

	/* do a SW reset */
	sky2_write8(hw, B0_CTST, CS_RST_SET);
	sky2_write8(hw, B0_CTST, CS_RST_CLR);

	/* allow writes to PCI config */
	sky2_write8(hw, B2_TST_CTRL1, TST_CFG_WRITE_ON);

	/* clear PCI errors, if any */
	status = sky2_pci_read16(hw, PCI_STATUS);
	status |= PCI_STATUS_ERROR_BITS;
	sky2_pci_write16(hw, PCI_STATUS, status);

	sky2_write8(hw, B0_CTST, CS_MRST_CLR);

	cap = pci_find_capability(pdev, PCI_CAP_ID_EXP);
	if (cap) {
		sky2_write32(hw, Y2_CFG_AER + PCI_ERR_UNCOR_STATUS,
			     0xfffffffful);

		/* If error bit is stuck on ignore it */
		if (sky2_read32(hw, B0_HWE_ISRC) & Y2_IS_PCI_EXP)
			dev_info(&pdev->dev, "ignoring stuck error report bit\n");
		else
			hwe_mask |= Y2_IS_PCI_EXP;
	}

	sky2_power_on(hw);
	sky2_write8(hw, B2_TST_CTRL1, TST_CFG_WRITE_OFF);

	for (i = 0; i < hw->ports; i++) {
		sky2_write8(hw, SK_REG(i, GMAC_LINK_CTRL), GMLC_RST_SET);
		sky2_write8(hw, SK_REG(i, GMAC_LINK_CTRL), GMLC_RST_CLR);

		if (hw->chip_id == CHIP_ID_YUKON_EX ||
		    hw->chip_id == CHIP_ID_YUKON_SUPR)
			sky2_write16(hw, SK_REG(i, GMAC_CTRL),
				     GMC_BYP_MACSECRX_ON | GMC_BYP_MACSECTX_ON
				     | GMC_BYP_RETR_ON);

	}

	if (hw->chip_id == CHIP_ID_YUKON_SUPR && hw->chip_rev > CHIP_REV_YU_SU_B0) {
		/* enable MACSec clock gating */
		sky2_pci_write32(hw, PCI_DEV_REG3, P_CLK_MACSEC_DIS);
	}

	if (hw->chip_id == CHIP_ID_YUKON_OPT) {
		u16 reg;
		u32 msk;

		if (hw->chip_rev == 0) {
			/* disable PCI-E PHY power down (set PHY reg 0x80, bit 7 */
			sky2_write32(hw, Y2_PEX_PHY_DATA, (0x80UL << 16) | (1 << 7));

			/* set PHY Link Detect Timer to 1.1 second (11x 100ms) */
			reg = 10;
		} else {
			/* set PHY Link Detect Timer to 0.4 second (4x 100ms) */
			reg = 3;
		}

		reg <<= PSM_CONFIG_REG4_TIMER_PHY_LINK_DETECT_BASE;

		/* reset PHY Link Detect */
		sky2_write8(hw, B2_TST_CTRL1, TST_CFG_WRITE_ON);
		sky2_pci_write16(hw, PSM_CONFIG_REG4,
				 reg | PSM_CONFIG_REG4_RST_PHY_LINK_DETECT);
		sky2_pci_write16(hw, PSM_CONFIG_REG4, reg);


		/* enable PHY Quick Link */
		msk = sky2_read32(hw, B0_IMSK);
		msk |= Y2_IS_PHY_QLNK;
		sky2_write32(hw, B0_IMSK, msk);

		/* check if PSMv2 was running before */
		reg = sky2_pci_read16(hw, PSM_CONFIG_REG3);
		if (reg & PCI_EXP_LNKCTL_ASPMC) {
			cap = pci_find_capability(pdev, PCI_CAP_ID_EXP);
			/* restore the PCIe Link Control register */
			sky2_pci_write16(hw, cap + PCI_EXP_LNKCTL, reg);
		}
		sky2_write8(hw, B2_TST_CTRL1, TST_CFG_WRITE_OFF);

		/* re-enable PEX PM in PEX PHY debug reg. 8 (clear bit 12) */
		sky2_write32(hw, Y2_PEX_PHY_DATA, PEX_DB_ACCESS | (0x08UL << 16));
	}

	/* Clear I2C IRQ noise */
	sky2_write32(hw, B2_I2C_IRQ, 1);

	/* turn off hardware timer (unused) */
	sky2_write8(hw, B2_TI_CTRL, TIM_STOP);
	sky2_write8(hw, B2_TI_CTRL, TIM_CLR_IRQ);

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

	sky2_write32(hw, B0_HWE_IMSK, hwe_mask);

	for (i = 0; i < hw->ports; i++)
		sky2_gmac_reset(hw, i);

	memset(hw->st_le, 0, hw->st_size * sizeof(struct sky2_status_le));
	hw->st_idx = 0;

	sky2_write32(hw, STAT_CTRL, SC_STAT_RST_SET);
	sky2_write32(hw, STAT_CTRL, SC_STAT_RST_CLR);

	sky2_write32(hw, STAT_LIST_ADDR_LO, hw->st_dma);
	sky2_write32(hw, STAT_LIST_ADDR_HI, (u64) hw->st_dma >> 32);

	/* Set the list last index */
	sky2_write16(hw, STAT_LAST_IDX, hw->st_size - 1);

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
}

/* Take device down (offline).
 * Equivalent to doing dev_stop() but this does not
 * inform upper layers of the transition.
 */
static void sky2_detach(struct net_device *dev)
{
	if (netif_running(dev)) {
		netif_tx_lock(dev);
		netif_device_detach(dev);	/* stop txq */
		netif_tx_unlock(dev);
		sky2_down(dev);
	}
}

/* Bring device back after doing sky2_detach */
static int sky2_reattach(struct net_device *dev)
{
	int err = 0;

	if (netif_running(dev)) {
		err = sky2_up(dev);
		if (err) {
			netdev_info(dev, "could not restart %d\n", err);
			dev_close(dev);
		} else {
			netif_device_attach(dev);
			sky2_set_multicast(dev);
		}
	}

	return err;
}

static void sky2_all_down(struct sky2_hw *hw)
{
	int i;

	sky2_read32(hw, B0_IMSK);
	sky2_write32(hw, B0_IMSK, 0);
	synchronize_irq(hw->pdev->irq);
	napi_disable(&hw->napi);

	for (i = 0; i < hw->ports; i++) {
		struct net_device *dev = hw->dev[i];
		struct sky2_port *sky2 = netdev_priv(dev);

		if (!netif_running(dev))
			continue;

		netif_carrier_off(dev);
		netif_tx_disable(dev);
		sky2_hw_down(sky2);
	}
}

static void sky2_all_up(struct sky2_hw *hw)
{
	u32 imask = Y2_IS_BASE;
	int i;

	for (i = 0; i < hw->ports; i++) {
		struct net_device *dev = hw->dev[i];
		struct sky2_port *sky2 = netdev_priv(dev);

		if (!netif_running(dev))
			continue;

		sky2_hw_up(sky2);
		sky2_set_multicast(dev);
		imask |= portirq_msk[i];
		netif_wake_queue(dev);
	}

	sky2_write32(hw, B0_IMSK, imask);
	sky2_read32(hw, B0_IMSK);

	sky2_read32(hw, B0_Y2_SP_LISR);
	napi_enable(&hw->napi);
}

static void sky2_restart(struct work_struct *work)
{
	struct sky2_hw *hw = container_of(work, struct sky2_hw, restart_work);

	rtnl_lock();

	sky2_all_down(hw);
	sky2_reset(hw);
	sky2_all_up(hw);

	rtnl_unlock();
}

static inline u8 sky2_wol_supported(const struct sky2_hw *hw)
{
	return sky2_is_copper(hw) ? (WAKE_PHY | WAKE_MAGIC) : 0;
}

static void sky2_get_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	const struct sky2_port *sky2 = netdev_priv(dev);

	wol->supported = sky2_wol_supported(sky2->hw);
	wol->wolopts = sky2->wol;
}

static int sky2_set_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	struct sky2_port *sky2 = netdev_priv(dev);
	struct sky2_hw *hw = sky2->hw;
	bool enable_wakeup = false;
	int i;

	if ((wol->wolopts & ~sky2_wol_supported(sky2->hw)) ||
	    !device_can_wakeup(&hw->pdev->dev))
		return -EOPNOTSUPP;

	sky2->wol = wol->wolopts;

	for (i = 0; i < hw->ports; i++) {
		struct net_device *dev = hw->dev[i];
		struct sky2_port *sky2 = netdev_priv(dev);

		if (sky2->wol)
			enable_wakeup = true;
	}
	device_set_wakeup_enable(&hw->pdev->dev, enable_wakeup);

	return 0;
}

static u32 sky2_supported_modes(const struct sky2_hw *hw)
{
	if (sky2_is_copper(hw)) {
		u32 modes = SUPPORTED_10baseT_Half
			| SUPPORTED_10baseT_Full
			| SUPPORTED_100baseT_Half
			| SUPPORTED_100baseT_Full;

		if (hw->flags & SKY2_HW_GIGABIT)
			modes |= SUPPORTED_1000baseT_Half
				| SUPPORTED_1000baseT_Full;
		return modes;
	} else
		return SUPPORTED_1000baseT_Half
			| SUPPORTED_1000baseT_Full;
}

static int sky2_get_settings(struct net_device *dev, struct ethtool_cmd *ecmd)
{
	struct sky2_port *sky2 = netdev_priv(dev);
	struct sky2_hw *hw = sky2->hw;

	ecmd->transceiver = XCVR_INTERNAL;
	ecmd->supported = sky2_supported_modes(hw);
	ecmd->phy_address = PHY_ADDR_MARV;
	if (sky2_is_copper(hw)) {
		ecmd->port = PORT_TP;
		ethtool_cmd_speed_set(ecmd, sky2->speed);
		ecmd->supported |=  SUPPORTED_Autoneg | SUPPORTED_TP;
	} else {
		ethtool_cmd_speed_set(ecmd, SPEED_1000);
		ecmd->port = PORT_FIBRE;
		ecmd->supported |=  SUPPORTED_Autoneg | SUPPORTED_FIBRE;
	}

	ecmd->advertising = sky2->advertising;
	ecmd->autoneg = (sky2->flags & SKY2_FLAG_AUTO_SPEED)
		? AUTONEG_ENABLE : AUTONEG_DISABLE;
	ecmd->duplex = sky2->duplex;
	return 0;
}

static int sky2_set_settings(struct net_device *dev, struct ethtool_cmd *ecmd)
{
	struct sky2_port *sky2 = netdev_priv(dev);
	const struct sky2_hw *hw = sky2->hw;
	u32 supported = sky2_supported_modes(hw);

	if (ecmd->autoneg == AUTONEG_ENABLE) {
		if (ecmd->advertising & ~supported)
			return -EINVAL;

		if (sky2_is_copper(hw))
			sky2->advertising = ecmd->advertising |
					    ADVERTISED_TP |
					    ADVERTISED_Autoneg;
		else
			sky2->advertising = ecmd->advertising |
					    ADVERTISED_FIBRE |
					    ADVERTISED_Autoneg;

		sky2->flags |= SKY2_FLAG_AUTO_SPEED;
		sky2->duplex = -1;
		sky2->speed = -1;
	} else {
		u32 setting;
		u32 speed = ethtool_cmd_speed(ecmd);

		switch (speed) {
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

		sky2->speed = speed;
		sky2->duplex = ecmd->duplex;
		sky2->flags &= ~SKY2_FLAG_AUTO_SPEED;
	}

	if (netif_running(dev)) {
		sky2_phy_reinit(sky2);
		sky2_set_multicast(dev);
	}

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

static u32 sky2_get_msglevel(struct net_device *netdev)
{
	struct sky2_port *sky2 = netdev_priv(netdev);
	return sky2->msg_enable;
}

static int sky2_nway_reset(struct net_device *dev)
{
	struct sky2_port *sky2 = netdev_priv(dev);

	if (!netif_running(dev) || !(sky2->flags & SKY2_FLAG_AUTO_SPEED))
		return -EINVAL;

	sky2_phy_reinit(sky2);
	sky2_set_multicast(dev);

	return 0;
}

static void sky2_phy_stats(struct sky2_port *sky2, u64 * data, unsigned count)
{
	struct sky2_hw *hw = sky2->hw;
	unsigned port = sky2->port;
	int i;

	data[0] = get_stats64(hw, port, GM_TXO_OK_LO);
	data[1] = get_stats64(hw, port, GM_RXO_OK_LO);

	for (i = 2; i < count; i++)
		data[i] = get_stats32(hw, port, sky2_stats[i].offset);
}

static void sky2_set_msglevel(struct net_device *netdev, u32 value)
{
	struct sky2_port *sky2 = netdev_priv(netdev);
	sky2->msg_enable = value;
}

static int sky2_get_sset_count(struct net_device *dev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return ARRAY_SIZE(sky2_stats);
	default:
		return -EOPNOTSUPP;
	}
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

static inline void sky2_add_filter(u8 filter[8], const u8 *addr)
{
	u32 bit;

	bit = ether_crc(ETH_ALEN, addr) & 63;
	filter[bit >> 3] |= 1 << (bit & 7);
}

static void sky2_set_multicast(struct net_device *dev)
{
	struct sky2_port *sky2 = netdev_priv(dev);
	struct sky2_hw *hw = sky2->hw;
	unsigned port = sky2->port;
	struct netdev_hw_addr *ha;
	u16 reg;
	u8 filter[8];
	int rx_pause;
	static const u8 pause_mc_addr[ETH_ALEN] = { 0x1, 0x80, 0xc2, 0x0, 0x0, 0x1 };

	rx_pause = (sky2->flow_status == FC_RX || sky2->flow_status == FC_BOTH);
	memset(filter, 0, sizeof(filter));

	reg = gma_read16(hw, port, GM_RX_CTRL);
	reg |= GM_RXCR_UCF_ENA;

	if (dev->flags & IFF_PROMISC)	/* promiscuous */
		reg &= ~(GM_RXCR_UCF_ENA | GM_RXCR_MCF_ENA);
	else if (dev->flags & IFF_ALLMULTI)
		memset(filter, 0xff, sizeof(filter));
	else if (netdev_mc_empty(dev) && !rx_pause)
		reg &= ~GM_RXCR_MCF_ENA;
	else {
		reg |= GM_RXCR_MCF_ENA;

		if (rx_pause)
			sky2_add_filter(filter, pause_mc_addr);

		netdev_for_each_mc_addr(ha, dev)
			sky2_add_filter(filter, ha->addr);
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

static struct rtnl_link_stats64 *sky2_get_stats(struct net_device *dev,
						struct rtnl_link_stats64 *stats)
{
	struct sky2_port *sky2 = netdev_priv(dev);
	struct sky2_hw *hw = sky2->hw;
	unsigned port = sky2->port;
	unsigned int start;
	u64 _bytes, _packets;

	do {
		start = u64_stats_fetch_begin_bh(&sky2->rx_stats.syncp);
		_bytes = sky2->rx_stats.bytes;
		_packets = sky2->rx_stats.packets;
	} while (u64_stats_fetch_retry_bh(&sky2->rx_stats.syncp, start));

	stats->rx_packets = _packets;
	stats->rx_bytes = _bytes;

	do {
		start = u64_stats_fetch_begin_bh(&sky2->tx_stats.syncp);
		_bytes = sky2->tx_stats.bytes;
		_packets = sky2->tx_stats.packets;
	} while (u64_stats_fetch_retry_bh(&sky2->tx_stats.syncp, start));

	stats->tx_packets = _packets;
	stats->tx_bytes = _bytes;

	stats->multicast = get_stats32(hw, port, GM_RXF_MC_OK)
		+ get_stats32(hw, port, GM_RXF_BC_OK);

	stats->collisions = get_stats32(hw, port, GM_TXF_COL);

	stats->rx_length_errors = get_stats32(hw, port, GM_RXF_LNG_ERR);
	stats->rx_crc_errors = get_stats32(hw, port, GM_RXF_FCS_ERR);
	stats->rx_frame_errors = get_stats32(hw, port, GM_RXF_SHT)
		+ get_stats32(hw, port, GM_RXE_FRAG);
	stats->rx_over_errors = get_stats32(hw, port, GM_RXE_FIFO_OV);

	stats->rx_dropped = dev->stats.rx_dropped;
	stats->rx_fifo_errors = dev->stats.rx_fifo_errors;
	stats->tx_fifo_errors = dev->stats.tx_fifo_errors;

	return stats;
}

/* Can have one global because blinking is controlled by
 * ethtool and that is always under RTNL mutex
 */
static void sky2_led(struct sky2_port *sky2, enum led_mode mode)
{
	struct sky2_hw *hw = sky2->hw;
	unsigned port = sky2->port;

	spin_lock_bh(&sky2->phy_lock);
	if (hw->chip_id == CHIP_ID_YUKON_EC_U ||
	    hw->chip_id == CHIP_ID_YUKON_EX ||
	    hw->chip_id == CHIP_ID_YUKON_SUPR) {
		u16 pg;
		pg = gm_phy_read(hw, port, PHY_MARV_EXT_ADR);
		gm_phy_write(hw, port, PHY_MARV_EXT_ADR, 3);

		switch (mode) {
		case MO_LED_OFF:
			gm_phy_write(hw, port, PHY_MARV_PHY_CTRL,
				     PHY_M_LEDC_LOS_CTRL(8) |
				     PHY_M_LEDC_INIT_CTRL(8) |
				     PHY_M_LEDC_STA1_CTRL(8) |
				     PHY_M_LEDC_STA0_CTRL(8));
			break;
		case MO_LED_ON:
			gm_phy_write(hw, port, PHY_MARV_PHY_CTRL,
				     PHY_M_LEDC_LOS_CTRL(9) |
				     PHY_M_LEDC_INIT_CTRL(9) |
				     PHY_M_LEDC_STA1_CTRL(9) |
				     PHY_M_LEDC_STA0_CTRL(9));
			break;
		case MO_LED_BLINK:
			gm_phy_write(hw, port, PHY_MARV_PHY_CTRL,
				     PHY_M_LEDC_LOS_CTRL(0xa) |
				     PHY_M_LEDC_INIT_CTRL(0xa) |
				     PHY_M_LEDC_STA1_CTRL(0xa) |
				     PHY_M_LEDC_STA0_CTRL(0xa));
			break;
		case MO_LED_NORM:
			gm_phy_write(hw, port, PHY_MARV_PHY_CTRL,
				     PHY_M_LEDC_LOS_CTRL(1) |
				     PHY_M_LEDC_INIT_CTRL(8) |
				     PHY_M_LEDC_STA1_CTRL(7) |
				     PHY_M_LEDC_STA0_CTRL(7));
		}

		gm_phy_write(hw, port, PHY_MARV_EXT_ADR, pg);
	} else
		gm_phy_write(hw, port, PHY_MARV_LED_OVER,
				     PHY_M_LED_MO_DUP(mode) |
				     PHY_M_LED_MO_10(mode) |
				     PHY_M_LED_MO_100(mode) |
				     PHY_M_LED_MO_1000(mode) |
				     PHY_M_LED_MO_RX(mode) |
				     PHY_M_LED_MO_TX(mode));

	spin_unlock_bh(&sky2->phy_lock);
}

/* blink LED's for finding board */
static int sky2_set_phys_id(struct net_device *dev,
			    enum ethtool_phys_id_state state)
{
	struct sky2_port *sky2 = netdev_priv(dev);

	switch (state) {
	case ETHTOOL_ID_ACTIVE:
		return 1;	/* cycle on/off once per second */
	case ETHTOOL_ID_INACTIVE:
		sky2_led(sky2, MO_LED_NORM);
		break;
	case ETHTOOL_ID_ON:
		sky2_led(sky2, MO_LED_ON);
		break;
	case ETHTOOL_ID_OFF:
		sky2_led(sky2, MO_LED_OFF);
		break;
	}

	return 0;
}

static void sky2_get_pauseparam(struct net_device *dev,
				struct ethtool_pauseparam *ecmd)
{
	struct sky2_port *sky2 = netdev_priv(dev);

	switch (sky2->flow_mode) {
	case FC_NONE:
		ecmd->tx_pause = ecmd->rx_pause = 0;
		break;
	case FC_TX:
		ecmd->tx_pause = 1, ecmd->rx_pause = 0;
		break;
	case FC_RX:
		ecmd->tx_pause = 0, ecmd->rx_pause = 1;
		break;
	case FC_BOTH:
		ecmd->tx_pause = ecmd->rx_pause = 1;
	}

	ecmd->autoneg = (sky2->flags & SKY2_FLAG_AUTO_PAUSE)
		? AUTONEG_ENABLE : AUTONEG_DISABLE;
}

static int sky2_set_pauseparam(struct net_device *dev,
			       struct ethtool_pauseparam *ecmd)
{
	struct sky2_port *sky2 = netdev_priv(dev);

	if (ecmd->autoneg == AUTONEG_ENABLE)
		sky2->flags |= SKY2_FLAG_AUTO_PAUSE;
	else
		sky2->flags &= ~SKY2_FLAG_AUTO_PAUSE;

	sky2->flow_mode = sky2_flow(ecmd->rx_pause, ecmd->tx_pause);

	if (netif_running(dev))
		sky2_phy_reinit(sky2);

	return 0;
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

	if (ecmd->tx_max_coalesced_frames >= sky2->tx_ring_size-1)
		return -EINVAL;
	if (ecmd->rx_max_coalesced_frames > RX_MAX_PENDING)
		return -EINVAL;
	if (ecmd->rx_max_coalesced_frames_irq > RX_MAX_PENDING)
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
	ering->tx_max_pending = TX_MAX_PENDING;

	ering->rx_pending = sky2->rx_pending;
	ering->rx_mini_pending = 0;
	ering->rx_jumbo_pending = 0;
	ering->tx_pending = sky2->tx_pending;
}

static int sky2_set_ringparam(struct net_device *dev,
			      struct ethtool_ringparam *ering)
{
	struct sky2_port *sky2 = netdev_priv(dev);

	if (ering->rx_pending > RX_MAX_PENDING ||
	    ering->rx_pending < 8 ||
	    ering->tx_pending < TX_MIN_PENDING ||
	    ering->tx_pending > TX_MAX_PENDING)
		return -EINVAL;

	sky2_detach(dev);

	sky2->rx_pending = ering->rx_pending;
	sky2->tx_pending = ering->tx_pending;
	sky2->tx_ring_size = roundup_pow_of_two(sky2->tx_pending+1);

	return sky2_reattach(dev);
}

static int sky2_get_regs_len(struct net_device *dev)
{
	return 0x4000;
}

static int sky2_reg_access_ok(struct sky2_hw *hw, unsigned int b)
{
	/* This complicated switch statement is to make sure and
	 * only access regions that are unreserved.
	 * Some blocks are only valid on dual port cards.
	 */
	switch (b) {
	/* second port */
	case 5:		/* Tx Arbiter 2 */
	case 9:		/* RX2 */
	case 14 ... 15:	/* TX2 */
	case 17: case 19: /* Ram Buffer 2 */
	case 22 ... 23: /* Tx Ram Buffer 2 */
	case 25:	/* Rx MAC Fifo 1 */
	case 27:	/* Tx MAC Fifo 2 */
	case 31:	/* GPHY 2 */
	case 40 ... 47: /* Pattern Ram 2 */
	case 52: case 54: /* TCP Segmentation 2 */
	case 112 ... 116: /* GMAC 2 */
		return hw->ports > 1;

	case 0:		/* Control */
	case 2:		/* Mac address */
	case 4:		/* Tx Arbiter 1 */
	case 7:		/* PCI express reg */
	case 8:		/* RX1 */
	case 12 ... 13: /* TX1 */
	case 16: case 18:/* Rx Ram Buffer 1 */
	case 20 ... 21: /* Tx Ram Buffer 1 */
	case 24:	/* Rx MAC Fifo 1 */
	case 26:	/* Tx MAC Fifo 1 */
	case 28 ... 29: /* Descriptor and status unit */
	case 30:	/* GPHY 1*/
	case 32 ... 39: /* Pattern Ram 1 */
	case 48: case 50: /* TCP Segmentation 1 */
	case 56 ... 60:	/* PCI space */
	case 80 ... 84:	/* GMAC 1 */
		return 1;

	default:
		return 0;
	}
}

/*
 * Returns copy of control register region
 * Note: ethtool_get_regs always provides full size (16k) buffer
 */
static void sky2_get_regs(struct net_device *dev, struct ethtool_regs *regs,
			  void *p)
{
	const struct sky2_port *sky2 = netdev_priv(dev);
	const void __iomem *io = sky2->hw->regs;
	unsigned int b;

	regs->version = 1;

	for (b = 0; b < 128; b++) {
		/* skip poisonous diagnostic ram region in block 3 */
		if (b == 3)
			memcpy_fromio(p + 0x10, io + 0x10, 128 - 0x10);
		else if (sky2_reg_access_ok(sky2->hw, b))
			memcpy_fromio(p, io, 128);
		else
			memset(p, 0, 128);

		p += 128;
		io += 128;
	}
}

static int sky2_get_eeprom_len(struct net_device *dev)
{
	struct sky2_port *sky2 = netdev_priv(dev);
	struct sky2_hw *hw = sky2->hw;
	u16 reg2;

	reg2 = sky2_pci_read16(hw, PCI_DEV_REG2);
	return 1 << ( ((reg2 & PCI_VPD_ROM_SZ) >> 14) + 8);
}

static int sky2_vpd_wait(const struct sky2_hw *hw, int cap, u16 busy)
{
	unsigned long start = jiffies;

	while ( (sky2_pci_read16(hw, cap + PCI_VPD_ADDR) & PCI_VPD_ADDR_F) == busy) {
		/* Can take up to 10.6 ms for write */
		if (time_after(jiffies, start + HZ/4)) {
			dev_err(&hw->pdev->dev, "VPD cycle timed out\n");
			return -ETIMEDOUT;
		}
		mdelay(1);
	}

	return 0;
}

static int sky2_vpd_read(struct sky2_hw *hw, int cap, void *data,
			 u16 offset, size_t length)
{
	int rc = 0;

	while (length > 0) {
		u32 val;

		sky2_pci_write16(hw, cap + PCI_VPD_ADDR, offset);
		rc = sky2_vpd_wait(hw, cap, 0);
		if (rc)
			break;

		val = sky2_pci_read32(hw, cap + PCI_VPD_DATA);

		memcpy(data, &val, min(sizeof(val), length));
		offset += sizeof(u32);
		data += sizeof(u32);
		length -= sizeof(u32);
	}

	return rc;
}

static int sky2_vpd_write(struct sky2_hw *hw, int cap, const void *data,
			  u16 offset, unsigned int length)
{
	unsigned int i;
	int rc = 0;

	for (i = 0; i < length; i += sizeof(u32)) {
		u32 val = *(u32 *)(data + i);

		sky2_pci_write32(hw, cap + PCI_VPD_DATA, val);
		sky2_pci_write32(hw, cap + PCI_VPD_ADDR, offset | PCI_VPD_ADDR_F);

		rc = sky2_vpd_wait(hw, cap, PCI_VPD_ADDR_F);
		if (rc)
			break;
	}
	return rc;
}

static int sky2_get_eeprom(struct net_device *dev, struct ethtool_eeprom *eeprom,
			   u8 *data)
{
	struct sky2_port *sky2 = netdev_priv(dev);
	int cap = pci_find_capability(sky2->hw->pdev, PCI_CAP_ID_VPD);

	if (!cap)
		return -EINVAL;

	eeprom->magic = SKY2_EEPROM_MAGIC;

	return sky2_vpd_read(sky2->hw, cap, data, eeprom->offset, eeprom->len);
}

static int sky2_set_eeprom(struct net_device *dev, struct ethtool_eeprom *eeprom,
			   u8 *data)
{
	struct sky2_port *sky2 = netdev_priv(dev);
	int cap = pci_find_capability(sky2->hw->pdev, PCI_CAP_ID_VPD);

	if (!cap)
		return -EINVAL;

	if (eeprom->magic != SKY2_EEPROM_MAGIC)
		return -EINVAL;

	/* Partial writes not supported */
	if ((eeprom->offset & 3) || (eeprom->len & 3))
		return -EINVAL;

	return sky2_vpd_write(sky2->hw, cap, data, eeprom->offset, eeprom->len);
}

static u32 sky2_fix_features(struct net_device *dev, u32 features)
{
	const struct sky2_port *sky2 = netdev_priv(dev);
	const struct sky2_hw *hw = sky2->hw;

	/* In order to do Jumbo packets on these chips, need to turn off the
	 * transmit store/forward. Therefore checksum offload won't work.
	 */
	if (dev->mtu > ETH_DATA_LEN && hw->chip_id == CHIP_ID_YUKON_EC_U)
		features &= ~(NETIF_F_TSO|NETIF_F_SG|NETIF_F_ALL_CSUM);

	return features;
}

static int sky2_set_features(struct net_device *dev, u32 features)
{
	struct sky2_port *sky2 = netdev_priv(dev);
	u32 changed = dev->features ^ features;

	if ((changed & NETIF_F_RXCSUM) &&
	    !(sky2->hw->flags & SKY2_HW_NEW_LE)) {
		sky2_write32(sky2->hw,
			     Q_ADDR(rxqaddr[sky2->port], Q_CSR),
			     (features & NETIF_F_RXCSUM)
			     ? BMU_ENA_RX_CHKSUM : BMU_DIS_RX_CHKSUM);
	}

	if (changed & NETIF_F_RXHASH)
		rx_set_rss(dev, features);

	if (changed & (NETIF_F_HW_VLAN_TX|NETIF_F_HW_VLAN_RX))
		sky2_vlan_mode(dev, features);

	return 0;
}

static const struct ethtool_ops sky2_ethtool_ops = {
	.get_settings	= sky2_get_settings,
	.set_settings	= sky2_set_settings,
	.get_drvinfo	= sky2_get_drvinfo,
	.get_wol	= sky2_get_wol,
	.set_wol	= sky2_set_wol,
	.get_msglevel	= sky2_get_msglevel,
	.set_msglevel	= sky2_set_msglevel,
	.nway_reset	= sky2_nway_reset,
	.get_regs_len	= sky2_get_regs_len,
	.get_regs	= sky2_get_regs,
	.get_link	= ethtool_op_get_link,
	.get_eeprom_len	= sky2_get_eeprom_len,
	.get_eeprom	= sky2_get_eeprom,
	.set_eeprom	= sky2_set_eeprom,
	.get_strings	= sky2_get_strings,
	.get_coalesce	= sky2_get_coalesce,
	.set_coalesce	= sky2_set_coalesce,
	.get_ringparam	= sky2_get_ringparam,
	.set_ringparam	= sky2_set_ringparam,
	.get_pauseparam = sky2_get_pauseparam,
	.set_pauseparam = sky2_set_pauseparam,
	.set_phys_id	= sky2_set_phys_id,
	.get_sset_count = sky2_get_sset_count,
	.get_ethtool_stats = sky2_get_ethtool_stats,
};

#ifdef CONFIG_SKY2_DEBUG

static struct dentry *sky2_debug;


/*
 * Read and parse the first part of Vital Product Data
 */
#define VPD_SIZE	128
#define VPD_MAGIC	0x82

static const struct vpd_tag {
	char tag[2];
	char *label;
} vpd_tags[] = {
	{ "PN",	"Part Number" },
	{ "EC", "Engineering Level" },
	{ "MN", "Manufacturer" },
	{ "SN", "Serial Number" },
	{ "YA", "Asset Tag" },
	{ "VL", "First Error Log Message" },
	{ "VF", "Second Error Log Message" },
	{ "VB", "Boot Agent ROM Configuration" },
	{ "VE", "EFI UNDI Configuration" },
};

static void sky2_show_vpd(struct seq_file *seq, struct sky2_hw *hw)
{
	size_t vpd_size;
	loff_t offs;
	u8 len;
	unsigned char *buf;
	u16 reg2;

	reg2 = sky2_pci_read16(hw, PCI_DEV_REG2);
	vpd_size = 1 << ( ((reg2 & PCI_VPD_ROM_SZ) >> 14) + 8);

	seq_printf(seq, "%s Product Data\n", pci_name(hw->pdev));
	buf = kmalloc(vpd_size, GFP_KERNEL);
	if (!buf) {
		seq_puts(seq, "no memory!\n");
		return;
	}

	if (pci_read_vpd(hw->pdev, 0, vpd_size, buf) < 0) {
		seq_puts(seq, "VPD read failed\n");
		goto out;
	}

	if (buf[0] != VPD_MAGIC) {
		seq_printf(seq, "VPD tag mismatch: %#x\n", buf[0]);
		goto out;
	}
	len = buf[1];
	if (len == 0 || len > vpd_size - 4) {
		seq_printf(seq, "Invalid id length: %d\n", len);
		goto out;
	}

	seq_printf(seq, "%.*s\n", len, buf + 3);
	offs = len + 3;

	while (offs < vpd_size - 4) {
		int i;

		if (!memcmp("RW", buf + offs, 2))	/* end marker */
			break;
		len = buf[offs + 2];
		if (offs + len + 3 >= vpd_size)
			break;

		for (i = 0; i < ARRAY_SIZE(vpd_tags); i++) {
			if (!memcmp(vpd_tags[i].tag, buf + offs, 2)) {
				seq_printf(seq, " %s: %.*s\n",
					   vpd_tags[i].label, len, buf + offs + 3);
				break;
			}
		}
		offs += len + 3;
	}
out:
	kfree(buf);
}

static int sky2_debug_show(struct seq_file *seq, void *v)
{
	struct net_device *dev = seq->private;
	const struct sky2_port *sky2 = netdev_priv(dev);
	struct sky2_hw *hw = sky2->hw;
	unsigned port = sky2->port;
	unsigned idx, last;
	int sop;

	sky2_show_vpd(seq, hw);

	seq_printf(seq, "\nIRQ src=%x mask=%x control=%x\n",
		   sky2_read32(hw, B0_ISRC),
		   sky2_read32(hw, B0_IMSK),
		   sky2_read32(hw, B0_Y2_SP_ICR));

	if (!netif_running(dev)) {
		seq_printf(seq, "network not running\n");
		return 0;
	}

	napi_disable(&hw->napi);
	last = sky2_read16(hw, STAT_PUT_IDX);

	seq_printf(seq, "Status ring %u\n", hw->st_size);
	if (hw->st_idx == last)
		seq_puts(seq, "Status ring (empty)\n");
	else {
		seq_puts(seq, "Status ring\n");
		for (idx = hw->st_idx; idx != last && idx < hw->st_size;
		     idx = RING_NEXT(idx, hw->st_size)) {
			const struct sky2_status_le *le = hw->st_le + idx;
			seq_printf(seq, "[%d] %#x %d %#x\n",
				   idx, le->opcode, le->length, le->status);
		}
		seq_puts(seq, "\n");
	}

	seq_printf(seq, "Tx ring pending=%u...%u report=%d done=%d\n",
		   sky2->tx_cons, sky2->tx_prod,
		   sky2_read16(hw, port == 0 ? STAT_TXA1_RIDX : STAT_TXA2_RIDX),
		   sky2_read16(hw, Q_ADDR(txqaddr[port], Q_DONE)));

	/* Dump contents of tx ring */
	sop = 1;
	for (idx = sky2->tx_next; idx != sky2->tx_prod && idx < sky2->tx_ring_size;
	     idx = RING_NEXT(idx, sky2->tx_ring_size)) {
		const struct sky2_tx_le *le = sky2->tx_le + idx;
		u32 a = le32_to_cpu(le->addr);

		if (sop)
			seq_printf(seq, "%u:", idx);
		sop = 0;

		switch (le->opcode & ~HW_OWNER) {
		case OP_ADDR64:
			seq_printf(seq, " %#x:", a);
			break;
		case OP_LRGLEN:
			seq_printf(seq, " mtu=%d", a);
			break;
		case OP_VLAN:
			seq_printf(seq, " vlan=%d", be16_to_cpu(le->length));
			break;
		case OP_TCPLISW:
			seq_printf(seq, " csum=%#x", a);
			break;
		case OP_LARGESEND:
			seq_printf(seq, " tso=%#x(%d)", a, le16_to_cpu(le->length));
			break;
		case OP_PACKET:
			seq_printf(seq, " %#x(%d)", a, le16_to_cpu(le->length));
			break;
		case OP_BUFFER:
			seq_printf(seq, " frag=%#x(%d)", a, le16_to_cpu(le->length));
			break;
		default:
			seq_printf(seq, " op=%#x,%#x(%d)", le->opcode,
				   a, le16_to_cpu(le->length));
		}

		if (le->ctrl & EOP) {
			seq_putc(seq, '\n');
			sop = 1;
		}
	}

	seq_printf(seq, "\nRx ring hw get=%d put=%d last=%d\n",
		   sky2_read16(hw, Y2_QADDR(rxqaddr[port], PREF_UNIT_GET_IDX)),
		   sky2_read16(hw, Y2_QADDR(rxqaddr[port], PREF_UNIT_PUT_IDX)),
		   sky2_read16(hw, Y2_QADDR(rxqaddr[port], PREF_UNIT_LAST_IDX)));

	sky2_read32(hw, B0_Y2_SP_LISR);
	napi_enable(&hw->napi);
	return 0;
}

static int sky2_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, sky2_debug_show, inode->i_private);
}

static const struct file_operations sky2_debug_fops = {
	.owner		= THIS_MODULE,
	.open		= sky2_debug_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/*
 * Use network device events to create/remove/rename
 * debugfs file entries
 */
static int sky2_device_event(struct notifier_block *unused,
			     unsigned long event, void *ptr)
{
	struct net_device *dev = ptr;
	struct sky2_port *sky2 = netdev_priv(dev);

	if (dev->netdev_ops->ndo_open != sky2_up || !sky2_debug)
		return NOTIFY_DONE;

	switch (event) {
	case NETDEV_CHANGENAME:
		if (sky2->debugfs) {
			sky2->debugfs = debugfs_rename(sky2_debug, sky2->debugfs,
						       sky2_debug, dev->name);
		}
		break;

	case NETDEV_GOING_DOWN:
		if (sky2->debugfs) {
			netdev_printk(KERN_DEBUG, dev, "remove debugfs\n");
			debugfs_remove(sky2->debugfs);
			sky2->debugfs = NULL;
		}
		break;

	case NETDEV_UP:
		sky2->debugfs = debugfs_create_file(dev->name, S_IRUGO,
						    sky2_debug, dev,
						    &sky2_debug_fops);
		if (IS_ERR(sky2->debugfs))
			sky2->debugfs = NULL;
	}

	return NOTIFY_DONE;
}

static struct notifier_block sky2_notifier = {
	.notifier_call = sky2_device_event,
};


static __init void sky2_debug_init(void)
{
	struct dentry *ent;

	ent = debugfs_create_dir("sky2", NULL);
	if (!ent || IS_ERR(ent))
		return;

	sky2_debug = ent;
	register_netdevice_notifier(&sky2_notifier);
}

static __exit void sky2_debug_cleanup(void)
{
	if (sky2_debug) {
		unregister_netdevice_notifier(&sky2_notifier);
		debugfs_remove(sky2_debug);
		sky2_debug = NULL;
	}
}

#else
#define sky2_debug_init()
#define sky2_debug_cleanup()
#endif

/* Two copies of network device operations to handle special case of
   not allowing netpoll on second port */
static const struct net_device_ops sky2_netdev_ops[2] = {
  {
	.ndo_open		= sky2_up,
	.ndo_stop		= sky2_down,
	.ndo_start_xmit		= sky2_xmit_frame,
	.ndo_do_ioctl		= sky2_ioctl,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address	= sky2_set_mac_address,
	.ndo_set_multicast_list	= sky2_set_multicast,
	.ndo_change_mtu		= sky2_change_mtu,
	.ndo_fix_features	= sky2_fix_features,
	.ndo_set_features	= sky2_set_features,
	.ndo_tx_timeout		= sky2_tx_timeout,
	.ndo_get_stats64	= sky2_get_stats,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= sky2_netpoll,
#endif
  },
  {
	.ndo_open		= sky2_up,
	.ndo_stop		= sky2_down,
	.ndo_start_xmit		= sky2_xmit_frame,
	.ndo_do_ioctl		= sky2_ioctl,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address	= sky2_set_mac_address,
	.ndo_set_multicast_list	= sky2_set_multicast,
	.ndo_change_mtu		= sky2_change_mtu,
	.ndo_fix_features	= sky2_fix_features,
	.ndo_set_features	= sky2_set_features,
	.ndo_tx_timeout		= sky2_tx_timeout,
	.ndo_get_stats64	= sky2_get_stats,
  },
};

/* Initialize network device */
static __devinit struct net_device *sky2_init_netdev(struct sky2_hw *hw,
						     unsigned port,
						     int highmem, int wol)
{
	struct sky2_port *sky2;
	struct net_device *dev = alloc_etherdev(sizeof(*sky2));

	if (!dev) {
		dev_err(&hw->pdev->dev, "etherdev alloc failed\n");
		return NULL;
	}

	SET_NETDEV_DEV(dev, &hw->pdev->dev);
	dev->irq = hw->pdev->irq;
	SET_ETHTOOL_OPS(dev, &sky2_ethtool_ops);
	dev->watchdog_timeo = TX_WATCHDOG;
	dev->netdev_ops = &sky2_netdev_ops[port];

	sky2 = netdev_priv(dev);
	sky2->netdev = dev;
	sky2->hw = hw;
	sky2->msg_enable = netif_msg_init(debug, default_msg);

	/* Auto speed and flow control */
	sky2->flags = SKY2_FLAG_AUTO_SPEED | SKY2_FLAG_AUTO_PAUSE;
	if (hw->chip_id != CHIP_ID_YUKON_XL)
		dev->hw_features |= NETIF_F_RXCSUM;

	sky2->flow_mode = FC_BOTH;

	sky2->duplex = -1;
	sky2->speed = -1;
	sky2->advertising = sky2_supported_modes(hw);
	sky2->wol = wol;

	spin_lock_init(&sky2->phy_lock);

	sky2->tx_pending = TX_DEF_PENDING;
	sky2->tx_ring_size = roundup_pow_of_two(TX_DEF_PENDING+1);
	sky2->rx_pending = RX_DEF_PENDING;

	hw->dev[port] = dev;

	sky2->port = port;

	dev->hw_features |= NETIF_F_IP_CSUM | NETIF_F_SG | NETIF_F_TSO;

	if (highmem)
		dev->features |= NETIF_F_HIGHDMA;

	/* Enable receive hashing unless hardware is known broken */
	if (!(hw->flags & SKY2_HW_RSS_BROKEN))
		dev->hw_features |= NETIF_F_RXHASH;

	if (!(hw->flags & SKY2_HW_VLAN_BROKEN)) {
		dev->hw_features |= NETIF_F_HW_VLAN_TX | NETIF_F_HW_VLAN_RX;
		dev->vlan_features |= SKY2_VLAN_OFFLOADS;
	}

	dev->features |= dev->hw_features;

	/* read the mac address */
	memcpy_fromio(dev->dev_addr, hw->regs + B2_MAC_1 + port * 8, ETH_ALEN);
	memcpy(dev->perm_addr, dev->dev_addr, dev->addr_len);

	return dev;
}

static void __devinit sky2_show_addr(struct net_device *dev)
{
	const struct sky2_port *sky2 = netdev_priv(dev);

	netif_info(sky2, probe, dev, "addr %pM\n", dev->dev_addr);
}

/* Handle software interrupt used during MSI test */
static irqreturn_t __devinit sky2_test_intr(int irq, void *dev_id)
{
	struct sky2_hw *hw = dev_id;
	u32 status = sky2_read32(hw, B0_Y2_SP_ISRC2);

	if (status == 0)
		return IRQ_NONE;

	if (status & Y2_IS_IRQ_SW) {
		hw->flags |= SKY2_HW_USE_MSI;
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

	init_waitqueue_head(&hw->msi_wait);

	sky2_write32(hw, B0_IMSK, Y2_IS_IRQ_SW);

	err = request_irq(pdev->irq, sky2_test_intr, 0, DRV_NAME, hw);
	if (err) {
		dev_err(&pdev->dev, "cannot assign irq %d\n", pdev->irq);
		return err;
	}

	sky2_write8(hw, B0_CTST, CS_ST_SW_IRQ);
	sky2_read8(hw, B0_CTST);

	wait_event_timeout(hw->msi_wait, (hw->flags & SKY2_HW_USE_MSI), HZ/10);

	if (!(hw->flags & SKY2_HW_USE_MSI)) {
		/* MSI test failed, go back to INTx mode */
		dev_info(&pdev->dev, "No interrupt generated using MSI, "
			 "switching to INTx mode.\n");

		err = -EOPNOTSUPP;
		sky2_write8(hw, B0_CTST, CS_CL_SW_IRQ);
	}

	sky2_write32(hw, B0_IMSK, 0);
	sky2_read32(hw, B0_IMSK);

	free_irq(pdev->irq, hw);

	return err;
}

/* This driver supports yukon2 chipset only */
static const char *sky2_name(u8 chipid, char *buf, int sz)
{
	const char *name[] = {
		"XL",		/* 0xb3 */
		"EC Ultra", 	/* 0xb4 */
		"Extreme",	/* 0xb5 */
		"EC",		/* 0xb6 */
		"FE",		/* 0xb7 */
		"FE+",		/* 0xb8 */
		"Supreme",	/* 0xb9 */
		"UL 2",		/* 0xba */
		"Unknown",	/* 0xbb */
		"Optima",	/* 0xbc */
	};

	if (chipid >= CHIP_ID_YUKON_XL && chipid <= CHIP_ID_YUKON_OPT)
		strncpy(buf, name[chipid - CHIP_ID_YUKON_XL], sz);
	else
		snprintf(buf, sz, "(chip %#x)", chipid);
	return buf;
}

static int __devinit sky2_probe(struct pci_dev *pdev,
				const struct pci_device_id *ent)
{
	struct net_device *dev;
	struct sky2_hw *hw;
	int err, using_dac = 0, wol_default;
	u32 reg;
	char buf1[16];

	err = pci_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "cannot enable PCI device\n");
		goto err_out;
	}

	/* Get configuration information
	 * Note: only regular PCI config access once to test for HW issues
	 *       other PCI access through shared memory for speed and to
	 *	 avoid MMCONFIG problems.
	 */
	err = pci_read_config_dword(pdev, PCI_DEV_REG2, &reg);
	if (err) {
		dev_err(&pdev->dev, "PCI read config failed\n");
		goto err_out;
	}

	if (~reg == 0) {
		dev_err(&pdev->dev, "PCI configuration read error\n");
		goto err_out;
	}

	err = pci_request_regions(pdev, DRV_NAME);
	if (err) {
		dev_err(&pdev->dev, "cannot obtain PCI resources\n");
		goto err_out_disable;
	}

	pci_set_master(pdev);

	if (sizeof(dma_addr_t) > sizeof(u32) &&
	    !(err = pci_set_dma_mask(pdev, DMA_BIT_MASK(64)))) {
		using_dac = 1;
		err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64));
		if (err < 0) {
			dev_err(&pdev->dev, "unable to obtain 64 bit DMA "
				"for consistent allocations\n");
			goto err_out_free_regions;
		}
	} else {
		err = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
		if (err) {
			dev_err(&pdev->dev, "no usable DMA configuration\n");
			goto err_out_free_regions;
		}
	}


#ifdef __BIG_ENDIAN
	/* The sk98lin vendor driver uses hardware byte swapping but
	 * this driver uses software swapping.
	 */
	reg &= ~PCI_REV_DESC;
	err = pci_write_config_dword(pdev, PCI_DEV_REG2, reg);
	if (err) {
		dev_err(&pdev->dev, "PCI write config failed\n");
		goto err_out_free_regions;
	}
#endif

	wol_default = device_may_wakeup(&pdev->dev) ? WAKE_MAGIC : 0;

	err = -ENOMEM;

	hw = kzalloc(sizeof(*hw) + strlen(DRV_NAME "@pci:")
		     + strlen(pci_name(pdev)) + 1, GFP_KERNEL);
	if (!hw) {
		dev_err(&pdev->dev, "cannot allocate hardware struct\n");
		goto err_out_free_regions;
	}

	hw->pdev = pdev;
	sprintf(hw->irq_name, DRV_NAME "@pci:%s", pci_name(pdev));

	hw->regs = ioremap_nocache(pci_resource_start(pdev, 0), 0x4000);
	if (!hw->regs) {
		dev_err(&pdev->dev, "cannot map device registers\n");
		goto err_out_free_hw;
	}

	err = sky2_init(hw);
	if (err)
		goto err_out_iounmap;

	/* ring for status responses */
	hw->st_size = hw->ports * roundup_pow_of_two(3*RX_MAX_PENDING + TX_MAX_PENDING);
	hw->st_le = pci_alloc_consistent(pdev, hw->st_size * sizeof(struct sky2_status_le),
					 &hw->st_dma);
	if (!hw->st_le)
		goto err_out_reset;

	dev_info(&pdev->dev, "Yukon-2 %s chip revision %d\n",
		 sky2_name(hw->chip_id, buf1, sizeof(buf1)), hw->chip_rev);

	sky2_reset(hw);

	dev = sky2_init_netdev(hw, 0, using_dac, wol_default);
	if (!dev) {
		err = -ENOMEM;
		goto err_out_free_pci;
	}

	if (!disable_msi && pci_enable_msi(pdev) == 0) {
		err = sky2_test_msi(hw);
		if (err == -EOPNOTSUPP)
 			pci_disable_msi(pdev);
		else if (err)
			goto err_out_free_netdev;
 	}

	err = register_netdev(dev);
	if (err) {
		dev_err(&pdev->dev, "cannot register net device\n");
		goto err_out_free_netdev;
	}

	netif_carrier_off(dev);

	netif_napi_add(dev, &hw->napi, sky2_poll, NAPI_WEIGHT);

	err = request_irq(pdev->irq, sky2_intr,
			  (hw->flags & SKY2_HW_USE_MSI) ? 0 : IRQF_SHARED,
			  hw->irq_name, hw);
	if (err) {
		dev_err(&pdev->dev, "cannot assign irq %d\n", pdev->irq);
		goto err_out_unregister;
	}
	sky2_write32(hw, B0_IMSK, Y2_IS_BASE);
	napi_enable(&hw->napi);

	sky2_show_addr(dev);

	if (hw->ports > 1) {
		struct net_device *dev1;

		err = -ENOMEM;
		dev1 = sky2_init_netdev(hw, 1, using_dac, wol_default);
		if (dev1 && (err = register_netdev(dev1)) == 0)
			sky2_show_addr(dev1);
		else {
			dev_warn(&pdev->dev,
				 "register of second port failed (%d)\n", err);
			hw->dev[1] = NULL;
			hw->ports = 1;
			if (dev1)
				free_netdev(dev1);
		}
	}

	setup_timer(&hw->watchdog_timer, sky2_watchdog, (unsigned long) hw);
	INIT_WORK(&hw->restart_work, sky2_restart);

	pci_set_drvdata(pdev, hw);
	pdev->d3_delay = 150;

	return 0;

err_out_unregister:
	if (hw->flags & SKY2_HW_USE_MSI)
		pci_disable_msi(pdev);
	unregister_netdev(dev);
err_out_free_netdev:
	free_netdev(dev);
err_out_free_pci:
	pci_free_consistent(pdev, hw->st_size * sizeof(struct sky2_status_le),
			    hw->st_le, hw->st_dma);
err_out_reset:
	sky2_write8(hw, B0_CTST, CS_RST_SET);
err_out_iounmap:
	iounmap(hw->regs);
err_out_free_hw:
	kfree(hw);
err_out_free_regions:
	pci_release_regions(pdev);
err_out_disable:
	pci_disable_device(pdev);
err_out:
	pci_set_drvdata(pdev, NULL);
	return err;
}

static void __devexit sky2_remove(struct pci_dev *pdev)
{
	struct sky2_hw *hw = pci_get_drvdata(pdev);
	int i;

	if (!hw)
		return;

	del_timer_sync(&hw->watchdog_timer);
	cancel_work_sync(&hw->restart_work);

	for (i = hw->ports-1; i >= 0; --i)
		unregister_netdev(hw->dev[i]);

	sky2_write32(hw, B0_IMSK, 0);

	sky2_power_aux(hw);

	sky2_write8(hw, B0_CTST, CS_RST_SET);
	sky2_read8(hw, B0_CTST);

	free_irq(pdev->irq, hw);
	if (hw->flags & SKY2_HW_USE_MSI)
		pci_disable_msi(pdev);
	pci_free_consistent(pdev, hw->st_size * sizeof(struct sky2_status_le),
			    hw->st_le, hw->st_dma);
	pci_release_regions(pdev);
	pci_disable_device(pdev);

	for (i = hw->ports-1; i >= 0; --i)
		free_netdev(hw->dev[i]);

	iounmap(hw->regs);
	kfree(hw);

	pci_set_drvdata(pdev, NULL);
}

static int sky2_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct sky2_hw *hw = pci_get_drvdata(pdev);
	int i;

	if (!hw)
		return 0;

	del_timer_sync(&hw->watchdog_timer);
	cancel_work_sync(&hw->restart_work);

	rtnl_lock();

	sky2_all_down(hw);
	for (i = 0; i < hw->ports; i++) {
		struct net_device *dev = hw->dev[i];
		struct sky2_port *sky2 = netdev_priv(dev);

		if (sky2->wol)
			sky2_wol_init(sky2);
	}

	sky2_power_aux(hw);
	rtnl_unlock();

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int sky2_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct sky2_hw *hw = pci_get_drvdata(pdev);
	int err;

	if (!hw)
		return 0;

	/* Re-enable all clocks */
	err = pci_write_config_dword(pdev, PCI_DEV_REG3, 0);
	if (err) {
		dev_err(&pdev->dev, "PCI write config failed\n");
		goto out;
	}

	rtnl_lock();
	sky2_reset(hw);
	sky2_all_up(hw);
	rtnl_unlock();

	return 0;
out:

	dev_err(&pdev->dev, "resume failed (%d)\n", err);
	pci_disable_device(pdev);
	return err;
}

static SIMPLE_DEV_PM_OPS(sky2_pm_ops, sky2_suspend, sky2_resume);
#define SKY2_PM_OPS (&sky2_pm_ops)

#else

#define SKY2_PM_OPS NULL
#endif

static void sky2_shutdown(struct pci_dev *pdev)
{
	sky2_suspend(&pdev->dev);
	pci_wake_from_d3(pdev, device_may_wakeup(&pdev->dev));
	pci_set_power_state(pdev, PCI_D3hot);
}

static struct pci_driver sky2_driver = {
	.name = DRV_NAME,
	.id_table = sky2_id_table,
	.probe = sky2_probe,
	.remove = __devexit_p(sky2_remove),
	.shutdown = sky2_shutdown,
	.driver.pm = SKY2_PM_OPS,
};

static int __init sky2_init_module(void)
{
	pr_info("driver version " DRV_VERSION "\n");

	sky2_debug_init();
	return pci_register_driver(&sky2_driver);
}

static void __exit sky2_cleanup_module(void)
{
	pci_unregister_driver(&sky2_driver);
	sky2_debug_cleanup();
}

module_init(sky2_init_module);
module_exit(sky2_cleanup_module);

MODULE_DESCRIPTION("Marvell Yukon 2 Gigabit Ethernet driver");
MODULE_AUTHOR("Stephen Hemminger <shemminger@linux-foundation.org>");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);
