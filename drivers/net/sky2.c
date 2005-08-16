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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * TODO
 *	- coalescing setting?
 *	- variable ring size?
 *
 * TOTEST
 *	- speed setting
 *	- power management
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/pci.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/in.h>
#include <linux/delay.h>
#include <linux/crc32.h>

#include <asm/irq.h>

#include "sky2.h"

#define DRV_NAME		"sky2"
#define DRV_VERSION		"0.2"
#define PFX			DRV_NAME " "

/*
 * The Yukon II chipset takes 64 bit command blocks (called list elements)
 * that are organized into three (receive, transmit, status) different rings
 * similar to Tigon3. A transmit can require several elements;
 * a receive requires one (or two if using 64 bit dma).
 */

#ifdef CONFIG_SKY2_EC_A1
#define is_ec_a1(hw) \
	((hw)->chip_id == CHIP_ID_YUKON_EC && \
	 (hw)->chip_rev == CHIP_REV_YU_EC_A1)
#else
#define is_ec_a1(hw)	0
#endif

#define RX_LE_SIZE		256
#define MIN_RX_BUFFERS		8
#define MAX_RX_BUFFERS		124
#define RX_LE_BYTES		(RX_LE_SIZE*sizeof(struct sky2_rx_le))

#define TX_RING_SIZE		256	// min 64 max 4096
#define STATUS_RING_SIZE	1024	// pow2 > (2*Rx + Tx)
#define STATUS_LE_BYTES		(STATUS_RING_SIZE*sizeof(struct sky2_status_le))
#define ETH_JUMBO_MTU		9000
#define TX_WATCHDOG		(5 * HZ)
#define NAPI_WEIGHT		64
#define PHY_RETRIES		1000

static const u32 default_msg =
	NETIF_MSG_DRV| NETIF_MSG_PROBE| NETIF_MSG_LINK
	| NETIF_MSG_TIMER | NETIF_MSG_TX_ERR | NETIF_MSG_RX_ERR
	| NETIF_MSG_IFUP| NETIF_MSG_IFDOWN;

static int debug = -1;	/* defaults above */
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "Debug level (0=none,...,16=all)");

static const struct pci_device_id sky2_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_SYSKONNECT, 0x9E00) },
	{ PCI_DEVICE(PCI_VENDOR_ID_DLINK, 0x4b00) },
	{ PCI_DEVICE(PCI_VENDOR_ID_DLINK, 0x4b01) },
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
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x4360) },
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x4361) },
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0x4362) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, sky2_id_table);

/* Avoid conditionals by using array */
static const unsigned txqaddr[] = { Q_XA1, Q_XA2 };
static const unsigned rxqaddr[] = { Q_R1, Q_R2 };

static inline const char *chip_name(u8 chip_id)
{
	switch (chip_id) {
	case CHIP_ID_GENESIS:
		return "Genesis";
	case CHIP_ID_YUKON:
		return "Yukon";
	case CHIP_ID_YUKON_LITE:
		return "Yukon-Lite";
	case CHIP_ID_YUKON_LP:
		return  "Yukon-LP";
	case CHIP_ID_YUKON_XL:
		return "Yukon-XL";
	case CHIP_ID_YUKON_EC:
		return "Yukon-EC";
	case CHIP_ID_YUKON_FE:
		return "Yukon-FE";
	default:
		return "???";
	}
}

static void gm_phy_write(struct sky2_hw *hw, unsigned port, u16 reg, u16 val)
{
	int i;

	gma_write16(hw, port, GM_SMI_DATA, val);
	gma_write16(hw, port, GM_SMI_CTRL,
		    GM_SMI_CT_PHY_AD(PHY_ADDR_MARV) | GM_SMI_CT_REG_AD(reg));

	for (i = 0; i < PHY_RETRIES; i++) {
		udelay(1);

		if (!(gma_read16(hw, port, GM_SMI_CTRL) & GM_SMI_CT_BUSY))
			break;
	}
}

static u16 gm_phy_read(struct sky2_hw *hw, unsigned port, u16 reg)
{
	int i;

	gma_write16(hw, port, GM_SMI_CTRL,
		    GM_SMI_CT_PHY_AD(PHY_ADDR_MARV)
		    | GM_SMI_CT_REG_AD(reg) | GM_SMI_CT_OP_RD);

	for (i = 0; i < PHY_RETRIES; i++) {
		udelay(1);
		if (gma_read16(hw, port, GM_SMI_CTRL) & GM_SMI_CT_RD_VAL)
			goto ready;
	}

	printk(KERN_WARNING PFX "%s: phy read timeout\n",
	       hw->dev[port]->name);
 ready:
	return gma_read16(hw, port, GM_SMI_DATA);
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
	u16 ctrl, ct1000, adv;
	u16 ledctrl, ledover;

	pr_debug("phy reset autoneg=%s advertising=0x%x pause rx=%s tx=%s\n",
		 sky2->autoneg == AUTONEG_ENABLE ? "enable" : "disable",
		 sky2->advertising,
		 sky2->rx_pause ? "on" : "off",
		 sky2->tx_pause ? "on" : "off");

	if (sky2->autoneg == AUTONEG_ENABLE &&
	    hw->chip_id != CHIP_ID_YUKON_XL) {
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

		ctrl &= ~(PHY_M_PC_MDIX_MSK | PHY_M_MAC_MD_MSK);
		ctrl |= PHY_M_MAC_MODE_SEL(PHY_M_MAC_MD_1000BX);
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
		} else	/* special defines for FIBER (88E1011S only) */
			adv |= PHY_M_AN_1000X_AHD | PHY_M_AN_1000X_AFD;

		/* Set Flow-control capabilities */
		if (sky2->tx_pause && sky2->rx_pause)
			adv |= PHY_AN_PAUSE_CAP;		/* symmetric */
		else if (sky2->rx_pause && !sky2->tx_pause)
			adv |= PHY_AN_PAUSE_ASYM|PHY_AN_PAUSE_CAP;
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
		ctrl = gm_phy_read(hw, port, PHY_MARV_EXT_ADR);

		/* select page 3 to access LED control register */
		gm_phy_write(hw, port, PHY_MARV_EXT_ADR, 3);

		/* set LED Function Control register */
		gm_phy_write(hw, port, PHY_MARV_PHY_CTRL,
			     (PHY_M_LEDC_LOS_CTRL(1) |		/* LINK/ACT */
			      PHY_M_LEDC_INIT_CTRL(7) |		/* 10 Mbps */
			      PHY_M_LEDC_STA1_CTRL(7) |		/* 100 Mbps */
			      PHY_M_LEDC_STA0_CTRL(7)));		/* 1000 Mbps */

		/* set Polarity Control register */
		gm_phy_write(hw, port, PHY_MARV_PHY_STAT,
			     (PHY_M_POLC_LS1_P_MIX(4) | PHY_M_POLC_IS0_P_MIX(4) |
			      PHY_M_POLC_LOS_CTRL(2) | PHY_M_POLC_INIT_CTRL(2) |
			      PHY_M_POLC_STA1_CTRL(2) | PHY_M_POLC_STA0_CTRL(2)));

		/* restore page register */
		gm_phy_write(hw, port, PHY_MARV_EXT_ADR, ctrl);
		break;

	default:
		/* set Tx LED (LED_TX) to blink mode on Rx OR Tx activity */
		ledctrl |= PHY_M_LED_BLINK_RT(BLINK_84MS) | PHY_M_LEDC_TX_CTRL;
		/* turn off the Rx LED (LED_RX) */
		ledover |= PHY_M_LED_MO_RX(MO_LED_OFF);
	}

	gm_phy_write(hw, port, PHY_MARV_LED_CTRL, ledctrl);

	if (sky2->autoneg == AUTONEG_DISABLE || sky2->speed == SPEED_100) {
		/* turn on 100 Mbps LED (LED_LINK100) */
		ledover |= PHY_M_LED_MO_100(MO_LED_ON);
	}

	if (ledover)
		gm_phy_write(hw, port, PHY_MARV_LED_OVER, ledover);

	/* Enable phy interrupt on autonegotiation complete (or link up) */
	if (sky2->autoneg == AUTONEG_ENABLE)
		gm_phy_write(hw, port, PHY_MARV_INT_MASK, PHY_M_IS_AN_COMPL);
	else
		gm_phy_write(hw, port, PHY_MARV_INT_MASK, PHY_M_DEF_MSK);
}

static void sky2_mac_init(struct sky2_hw *hw, unsigned port)
{
	struct sky2_port *sky2 = netdev_priv(hw->dev[port]);
	u16 reg;
	int i;
	const u8 *addr = hw->dev[port]->dev_addr;

	sky2_write8(hw, SK_REG(port, GPHY_CTRL), GPC_RST_SET);
	sky2_write8(hw, SK_REG(port, GPHY_CTRL), GPC_RST_CLR);

	sky2_write8(hw, SK_REG(port, GMAC_CTRL), GMC_RST_CLR);

	if (hw->chip_id == CHIP_ID_YUKON_XL && hw->chip_rev == 0
	    && port == 1) {
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
			reg |= GM_GPCR_SPEED_1000;
			/* fallthru */
		case SPEED_100:
			reg |= GM_GPCR_SPEED_100;
		}

		if (sky2->duplex == DUPLEX_FULL)
			reg |= GM_GPCR_DUP_FULL;
	} else
		reg = GM_GPCR_SPEED_1000 | GM_GPCR_SPEED_100 | GM_GPCR_DUP_FULL;

	if (!sky2->tx_pause && !sky2->rx_pause) {
		sky2_write32(hw, SK_REG(port, GMAC_CTRL), GMC_PAUSE_OFF);
		reg |= GM_GPCR_FC_TX_DIS | GM_GPCR_FC_RX_DIS | GM_GPCR_AU_FCT_DIS;
	} else if (sky2->tx_pause &&!sky2->rx_pause) {
		/* disable Rx flow-control */
		reg |= GM_GPCR_FC_RX_DIS | GM_GPCR_AU_FCT_DIS;
	}

	gma_write16(hw, port, GM_GP_CTRL, reg);

	sky2_read16(hw, GMAC_IRQ_SRC);

	spin_lock_bh(&hw->phy_lock);
	sky2_phy_init(hw, port);
	spin_unlock_bh(&hw->phy_lock);

	/* MIB clear */
	reg = gma_read16(hw, port, GM_PHY_ADDR);
	gma_write16(hw, port, GM_PHY_ADDR, reg | GM_PAR_MIB_CLR);

	for (i = 0; i < GM_MIB_CNT_SIZE; i++)
		gma_read16(hw, port, GM_MIB_CNT_BASE + 8*i);
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

	if (hw->dev[port]->mtu > 1500)
		reg |= GM_SMOD_JUMBO_ENA;

	gma_write16(hw, port, GM_SERIAL_MODE, reg);

	/* physical address: used for pause frames */
	gma_set_addr(hw, port, GM_SRC_ADDR_1L, addr);
	/* virtual address for data */
	gma_set_addr(hw, port, GM_SRC_ADDR_2L, addr);

	/* enable interrupt mask for counter overflows */
	gma_write16(hw, port, GM_TX_IRQ_MSK, 0);
	gma_write16(hw, port, GM_RX_IRQ_MSK, 0);
	gma_write16(hw, port, GM_TR_IRQ_MSK, 0);

	/* Configure Rx MAC FIFO */
	sky2_write8(hw, SK_REG(port, RX_GMF_CTRL_T), GMF_RST_CLR);
	sky2_write16(hw, SK_REG(port, RX_GMF_CTRL_T), 
		     GMF_OPER_ON | GMF_RX_F_FL_ON);

	reg = RX_FF_FL_DEF_MSK;
	if (hw->chip_id == CHIP_ID_YUKON_XL && hw->chip_rev <= 1)
		reg = 0;	/* WA Dev #4115 */

	sky2_write16(hw, SK_REG(port, RX_GMF_FL_MSK), reg);
	/* Set threshold to 0xa (64 bytes) 
	 *  ASF disabled so no need to do WA dev #4.30 
	 */
	sky2_write16(hw, SK_REG(port, RX_GMF_FL_THR), RX_GMF_FL_THR_DEF);

	/* Configure Tx MAC FIFO */
	sky2_write8(hw, SK_REG(port, TX_GMF_CTRL_T), GMF_RST_CLR);
	sky2_write16(hw, SK_REG(port, TX_GMF_CTRL_T), GMF_OPER_ON);

	/* Turn off Rx fifo flush (per sk98lin) */
	sky2_write8(hw, SK_REG(port, RX_GMF_CTRL_T), GMF_RX_F_FL_OFF);
}

static void sky2_ramset(struct sky2_hw *hw, u16 q, u32 start, size_t len)
{
	u32 end;

	start /= 8;
	len /= 8;
	end = start + len - 1;
	pr_debug("ramset q=%d start=0x%x end=0x%x\n", q, start, end);

	sky2_write8(hw, RB_ADDR(q, RB_CTRL), RB_RST_CLR);
	sky2_write32(hw, RB_ADDR(q, RB_START), start);
	sky2_write32(hw, RB_ADDR(q, RB_END), end);
	sky2_write32(hw, RB_ADDR(q, RB_WP), start);
	sky2_write32(hw, RB_ADDR(q, RB_RP), start);

	if (q == Q_R1 || q == Q_R2) {
		/* Set thresholds on receive queue's */
		sky2_write32(hw, RB_ADDR(q, RB_RX_UTPP),
			     start + (2*len)/3);
		sky2_write32(hw, RB_ADDR(q, RB_RX_LTPP),
			     start + (len/3));
	} else {
		/* Enable store & forward on Tx queue's because
		 * Tx FIFO is only 1K on Yukon
		 */
		sky2_write8(hw, RB_ADDR(q, RB_CTRL), RB_ENA_STFWD);
	}

	sky2_write8(hw, RB_ADDR(q, RB_CTRL), RB_ENA_OP_MD);
}


/* Setup Bus Memory Interface */
static void sky2_qset(struct sky2_hw *hw, u16 q, u32 wm)
{
	sky2_write32(hw, Q_ADDR(q, Q_CSR), BMU_CLR_RESET);
	sky2_write32(hw, Q_ADDR(q, Q_CSR), BMU_OPER_INIT);
	sky2_write32(hw, Q_ADDR(q, Q_CSR), BMU_FIFO_OP_ON);
	sky2_write32(hw, Q_ADDR(q, Q_WM), wm);
}


/* Setup prefetch unit registers. This is the interface between
 * hardware and driver list elements
 */
static inline void sky2_prefetch_init(struct sky2_hw *hw, u32 qaddr,
				      u64 addr, u32 last)
{
	pr_debug("sky2 prefetch init q=%x addr=%llx last=%x\n",
		 Y2_QADDR(qaddr, 0), addr, last);

	sky2_write32(hw, Y2_QADDR(qaddr, PREF_UNIT_CTRL), PREF_UNIT_RST_SET);
	sky2_write32(hw, Y2_QADDR(qaddr, PREF_UNIT_CTRL), PREF_UNIT_RST_CLR);
	sky2_write32(hw, Y2_QADDR(qaddr, PREF_UNIT_ADDR_HI), addr >> 32);
	sky2_write32(hw, Y2_QADDR(qaddr, PREF_UNIT_ADDR_LO), (u32) addr);
	sky2_write16(hw, Y2_QADDR(qaddr, PREF_UNIT_LAST_IDX), last);
	sky2_write32(hw, Y2_QADDR(qaddr, PREF_UNIT_CTRL), PREF_UNIT_OP_ON);
}


/*
 * This is a workaround code taken from syskonnect sk98lin driver
 * to deal with chip bug in the wraparound case.
 */
static inline void sky2_put_idx(struct sky2_hw *hw, unsigned q,
				u16 idx, u16 *last, u16 size)

{
	BUG_ON(idx >= size);

	wmb();
	if (is_ec_a1(hw) && idx < *last) {
		u16 hwget = sky2_read16(hw, Y2_QADDR(q, PREF_UNIT_GET_IDX));

		if (hwget == 0) {
			/* Start prefetching again */
			sky2_write8(hw, Y2_QADDR(q, PREF_UNIT_FIFO_WM),
				    0xe0);
			goto setnew;
		}

		if (hwget == size-1) {
			/* set watermark to one list element */
			sky2_write8(hw, Y2_QADDR(q, PREF_UNIT_FIFO_WM), 8);

			/* set put index to first list element */
			sky2_write16(hw, Y2_QADDR(q, PREF_UNIT_PUT_IDX), 0);
		} else 	/* have hardware go to end of list */
			sky2_write16(hw, Y2_QADDR(q, PREF_UNIT_PUT_IDX), size-1);
	} else {
	setnew:
		sky2_write16(hw, Y2_QADDR(q, PREF_UNIT_PUT_IDX), idx);
		*last = idx;
	}
}

static inline struct sky2_rx_le *sky2_next_rx(struct sky2_port *sky2)
{
	struct sky2_rx_le *le = sky2->rx_le + sky2->rx_put;
	sky2->rx_put = (sky2->rx_put + 1) % RX_LE_SIZE;
	return le;
}

static inline void sky2_rx_add(struct sky2_port *sky2, dma_addr_t map, u16 len)
{
	struct sky2_rx_le *le;

	if (sizeof(map) > sizeof(u32)) {
		le = sky2_next_rx(sky2);
		le->rx.addr = cpu_to_le32((u64) map >> 32);
		le->ctrl = 0;
		le->opcode = OP_ADDR64 | HW_OWNER;
	}
	
	le = sky2_next_rx(sky2);
	le->rx.addr = cpu_to_le32((u32) map);
	le->length = cpu_to_le16(len);
	le->ctrl = 0;
	le->opcode = OP_PACKET | HW_OWNER;
}

/* Tell chip where to start receive checksum.
 * Actually has two checksums, but set both same to avoid possible byte
 * order problems.
 */
static void sky2_rx_set_offset(struct sky2_port *sky2)
{
	struct sky2_rx_le *le;

	sky2_write32(sky2->hw, 
		     Q_ADDR(rxqaddr[sky2->port], Q_CSR),
		     sky2->rx_csum ? BMU_ENA_RX_CHKSUM : BMU_DIS_RX_CHKSUM);

	le = sky2_next_rx(sky2);
	le->rx.csum.start1 = ETH_HLEN;
	le->rx.csum.start2 = ETH_HLEN;
	le->ctrl = 0;
	le->opcode = OP_TCPSTART | HW_OWNER;
	wmb();
	sky2_write16(sky2->hw, 
		     Y2_QADDR(rxqaddr[sky2->port], PREF_UNIT_PUT_IDX),
		     sky2->rx_put);

}

/* Cleanout receive buffer area, assumes receiver hardware stopped */
static void sky2_rx_clean(struct sky2_port *sky2)
{
	unsigned i;

	memset(sky2->rx_le, 0, RX_LE_BYTES);
	for (i = 0; i < sky2->rx_ring_size; i++) {
		struct ring_info *re = sky2->rx_ring + i;

		if (re->skb) {
			pci_unmap_single(sky2->hw->pdev, 
					 pci_unmap_addr(re, mapaddr),
					 pci_unmap_len(re, maplen),
					 PCI_DMA_FROMDEVICE);
			kfree_skb(re->skb);
			re->skb = NULL;
		}
	}
}

static inline struct sk_buff *sky2_rx_alloc_skb(struct sky2_port *sky2,
						unsigned int size, int gfp_mask)
{
	struct sk_buff *skb;

	skb = alloc_skb(size, gfp_mask);
	if (likely(skb)) {
		skb->dev = sky2->netdev;
		skb_reserve(skb, NET_IP_ALIGN);
	}
	return skb;
}

/*
 * Allocate and setup receiver buffer pool.
 * In case of 64 bit dma, there are 2X as many list elements
 * available as ring entries
 * and need to reserve one list element so we don't wrap around.
 */
static int sky2_rx_fill(struct sky2_port *sky2)
{
	unsigned i;
	unsigned int rx_buf_size = sky2->netdev->mtu + ETH_HLEN + 8;

	pr_debug("sky2_rx_fill %d\n", sky2->rx_ring_size);
	for (i = 0; i < sky2->rx_ring_size; i++) {
		struct ring_info *re = sky2->rx_ring + i;
		dma_addr_t paddr;

		re->skb = sky2_rx_alloc_skb(sky2, rx_buf_size, GFP_KERNEL);
		if (!re->skb)
			goto nomem;

		paddr = pci_map_single(sky2->hw->pdev, re->skb->data,
				       rx_buf_size, PCI_DMA_FROMDEVICE);

		pci_unmap_len_set(re, maplen, rx_buf_size);
		pci_unmap_addr_set(re, mapaddr, paddr);
		sky2_rx_add(sky2, paddr, rx_buf_size);
	}

	sky2_write16(sky2->hw, 
		     Y2_QADDR(rxqaddr[sky2->port], PREF_UNIT_PUT_IDX),
		     sky2->rx_put);

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
	u32 ramsize, rxspace;
	int err = -ENOMEM;

	if (netif_msg_ifup(sky2))
		printk(KERN_INFO PFX "%s: enabling interface\n", dev->name);

	/* must be power of 2 */
	sky2->tx_le = pci_alloc_consistent(hw->pdev,
					   TX_RING_SIZE * sizeof(struct sky2_tx_le),
					   &sky2->tx_le_map);
	if (!sky2->tx_le)
		goto err_out;

	sky2->tx_ring = kmalloc(TX_RING_SIZE * sizeof(struct ring_info),
				GFP_KERNEL);
	if (!sky2->tx_ring)
		goto err_out;
	sky2->tx_prod = sky2->tx_cons = 0;
	memset(sky2->tx_ring, 0, TX_RING_SIZE * sizeof(struct ring_info));

	sky2->rx_le = pci_alloc_consistent(hw->pdev, RX_LE_BYTES,
					   &sky2->rx_le_map);
	if (!sky2->rx_le)
		goto err_out;
	memset(sky2->rx_le, 0, RX_LE_BYTES);

	sky2->rx_ring = kmalloc(sky2->rx_ring_size * sizeof(struct ring_info),
				GFP_KERNEL);
	if (!sky2->rx_ring)
		goto err_out;

	sky2_mac_init(hw, port);

	/* Configure RAM buffers */
	if (hw->chip_id == CHIP_ID_YUKON_FE ||
	    (hw->chip_id == CHIP_ID_YUKON_EC && hw->chip_rev == 2))
		ramsize = 4096;
	else {
		u8 e0  = sky2_read8(hw, B2_E_0);
		ramsize = (e0 == 0) ? (128*1024) : (e0 * 4096);
	}

	/* 2/3 for Rx */
	rxspace = (2 * ramsize) / 3;
	sky2_ramset(hw, rxqaddr[port], 0, rxspace);
	sky2_ramset(hw, txqaddr[port], rxspace, ramsize - rxspace);

	sky2_qset(hw, rxqaddr[port], is_pciex(hw) ? 0x80 : 0x600);
	sky2_qset(hw, txqaddr[port], 0x600);

	sky2->rx_put = sky2->rx_next = 0;
	sky2_prefetch_init(hw, rxqaddr[port], sky2->rx_le_map, RX_LE_SIZE-1);

	sky2_rx_set_offset(sky2);

	err = sky2_rx_fill(sky2);
	if (err)
		goto err_out;

	sky2_prefetch_init(hw, txqaddr[port], sky2->tx_le_map,
			   TX_RING_SIZE - 1);

	/* Enable interrupts from phy/mac for port */
	hw->intr_mask |= (port == 0) ? Y2_IS_PORT_1 : Y2_IS_PORT_2;
	sky2_write32(hw, B0_IMSK, hw->intr_mask);
	return 0;

err_out:
	if (sky2->rx_le)
		pci_free_consistent(hw->pdev, RX_LE_BYTES,
				    sky2->rx_le, sky2->rx_le_map);
	if (sky2->tx_le)
		pci_free_consistent(hw->pdev,
				    TX_RING_SIZE * sizeof(struct sky2_tx_le),
				    sky2->tx_le, sky2->tx_le_map);
	if (sky2->tx_ring)
		kfree(sky2->tx_ring);
	if (sky2->rx_ring)
		kfree(sky2->rx_ring);

	return err;
}

/*
 * Worst case number of list elements is 36
 *	TSO + CHKSUM + ADDR64 + BUFFER + (ADDR+BUFFER)*MAXFRAGS
 */
#define MAX_SKB_TX_LE	(4 + 2*MAX_SKB_FRAGS)

static inline int sky2_xmit_avail(const struct sky2_port *sky2)
{
	return (sky2->tx_cons > sky2->tx_prod ? 0 : TX_RING_SIZE)
		+ sky2->tx_cons - sky2->tx_prod - 1;
}

static inline struct sky2_tx_le *get_tx_le(struct sky2_port *sky2)
{
	struct sky2_tx_le *le = sky2->tx_le + sky2->tx_prod;
	sky2->tx_prod = (sky2->tx_prod + 1) % TX_RING_SIZE;
	return le;
}

/* Put one frame in ring for transmit. */
static int sky2_xmit_frame(struct sk_buff *skb, struct net_device *dev)
{
	struct sky2_port *sky2 = netdev_priv(dev);
	struct sky2_hw *hw = sky2->hw;
	struct sky2_tx_le *le;
	struct ring_info *re;
	unsigned i, len;
	dma_addr_t mapping;
	u32 addr64;
	u16 mss;
	u8 ctrl;

	skb = skb_padto(skb, ETH_ZLEN);
	if (!skb)
		return NETDEV_TX_OK;

	if (!spin_trylock(&sky2->tx_lock))
		return NETDEV_TX_LOCKED;

	if (unlikely(sky2_xmit_avail(sky2) < MAX_SKB_TX_LE)) {
		netif_stop_queue(dev);
		spin_unlock(&sky2->tx_lock);

		printk(KERN_WARNING PFX "%s: ring full when queue awake!\n",
		       dev->name);
		return NETDEV_TX_BUSY;
	}

	if (netif_msg_tx_queued(sky2))
		printk(KERN_DEBUG "%s: tx queued, slot %u, len %d\n",
		       dev->name, sky2->tx_prod, skb->len);


	len = skb_headlen(skb);
	mapping = pci_map_single(hw->pdev, skb->data, len, PCI_DMA_TODEVICE);

	/* Check for TCP Segmentation Offload */
	mss = skb_shinfo(skb)->tso_size;
	if (mss) {
		/* just drop the packet if non-linear expansion fails */
		if (skb_header_cloned(skb) &&
		    pskb_expand_head(skb, 0, 0, GFP_ATOMIC)) {
			dev_kfree_skb(skb);
			return NETDEV_TX_OK;
		}

		mss += ((skb->h.th->doff - 5) * 4);	/* TCP options */
		mss += (skb->nh.iph->ihl * 4) + sizeof(struct tcphdr);
		mss += ETH_HLEN;

		le = get_tx_le(sky2);
		le->tx.tso.size = cpu_to_le16(mss);
		le->ctrl = 0;
		le->opcode = OP_LRGLEN | HW_OWNER;
	}

	/* Handle Hi DMA */
	if (sizeof(mapping) > sizeof(u32)) {
		addr64 = (u64)mapping >> 32;

		le = get_tx_le(sky2);
		le->tx.addr = cpu_to_le32(addr64);
		le->ctrl = 0;
		le->opcode = OP_ADDR64 | HW_OWNER;
	}

	/* Handle TCP checksum offload */
	ctrl = 0;
	if (skb->ip_summed == CHECKSUM_HW) {
		ptrdiff_t hdr = skb->h.raw - skb->data;

		ctrl = CALSUM | WR_SUM | INIT_SUM | LOCK_SUM;
		if (skb->nh.iph->protocol == IPPROTO_UDP)
			ctrl |= UDPTCP;

		le = get_tx_le(sky2);
		le->tx.csum.start = cpu_to_le16(hdr);
		le->tx.csum.offset = cpu_to_le16(hdr + skb->csum);
		le->length = 0;
		le->ctrl = 1;	/* one packet */
		le->opcode = OP_TCPLISW|HW_OWNER;
	}

	le = get_tx_le(sky2);
	le->tx.addr = cpu_to_le32((u32) mapping);
	le->length = cpu_to_le16(len);
	le->ctrl = ctrl;
	le->opcode = (mss ? OP_LARGESEND : OP_PACKET) |HW_OWNER;

	re = &sky2->tx_ring[le - sky2->tx_le];
	re->skb = skb;
	pci_unmap_addr_set(re, mapaddr, mapping);
	pci_unmap_len_set(re, maplen, len);

	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

		mapping = pci_map_page(hw->pdev, frag->page, frag->page_offset,
				       frag->size, PCI_DMA_TODEVICE);

		if (sizeof(mapping) > sizeof(u32)) {
			u32 hi = (u64) mapping  >> 32;
			if (hi != addr64) {
				le = get_tx_le(sky2);
				le->tx.addr = cpu_to_le32(hi);
				le->ctrl = 0;
				le->opcode = OP_ADDR64|HW_OWNER;
				addr64 = hi;
			}
		}

		le = get_tx_le(sky2);
		le->tx.addr = cpu_to_le32((u32) mapping);
		le->length = cpu_to_le16(frag->size);
		le->ctrl = ctrl;
		le->opcode = OP_BUFFER|HW_OWNER;

		re = &sky2->tx_ring[le - sky2->tx_le];
		pci_unmap_addr_set(re, mapaddr, mapping);
		pci_unmap_len_set(re, maplen, frag->size);
	}

	le->ctrl |= EOP;

	sky2_put_idx(sky2->hw, txqaddr[sky2->port], sky2->tx_prod,
		     &sky2->tx_last_put, TX_RING_SIZE);

	if (sky2_xmit_avail(sky2) < MAX_SKB_TX_LE) {
		pr_debug("%s: transmit queue full\n", dev->name);
		netif_stop_queue(dev);
	}
	spin_unlock(&sky2->tx_lock);

	dev->trans_start = jiffies;
	return NETDEV_TX_OK;
}


/*
 * Free ring elements from starting at tx_cons until done
 * This unwinds the elements based on the usage assigned
 * xmit routine.
 */
static void sky2_tx_complete(struct net_device *dev, u16 done)
{
	struct sky2_port *sky2 = netdev_priv(dev);
	unsigned idx = sky2->tx_cons;
	struct sk_buff *skb = NULL;

	BUG_ON(done >= TX_RING_SIZE);

	spin_lock(&sky2->tx_lock);
	while (idx != done) {
		struct ring_info *re = sky2->tx_ring + idx;
		struct sky2_tx_le *le = sky2->tx_le + idx;

		BUG_ON(le->opcode == 0);

		switch(le->opcode & ~HW_OWNER) {
		case OP_LARGESEND:
		case OP_PACKET:
			if (skb)
				dev_kfree_skb_any(skb);
			skb = re->skb;
			BUG_ON(!skb);
			re->skb = NULL;

			pci_unmap_single(sky2->hw->pdev, 
					 pci_unmap_addr(re, mapaddr),
					 pci_unmap_len(re, maplen),
					 PCI_DMA_TODEVICE);
			break;

		case OP_BUFFER:
			pci_unmap_page(sky2->hw->pdev,
				       pci_unmap_addr(re, mapaddr),
				       pci_unmap_len(re, maplen),
				       PCI_DMA_TODEVICE);
			break;
		}

		le->opcode = 0;
		idx = (idx + 1) % TX_RING_SIZE;
	}

	if (skb)
		dev_kfree_skb_any(skb);
	sky2->tx_cons = idx;

	if (sky2_xmit_avail(sky2) > MAX_SKB_TX_LE)
		netif_wake_queue(dev);
	spin_unlock(&sky2->tx_lock);
}

/* Cleanup all untransmitted buffers, assume transmitter not running */
static inline void sky2_tx_clean(struct sky2_port *sky2)
{
	sky2_tx_complete(sky2->netdev, sky2->tx_prod);
}

/* Network shutdown */
static int sky2_down(struct net_device *dev)
{
	struct sky2_port *sky2 = netdev_priv(dev);
	struct sky2_hw *hw = sky2->hw;
	unsigned port = sky2->port;
	u16 ctrl;
	int i;

	if (netif_msg_ifdown(sky2))
		printk(KERN_INFO PFX "%s: disabling interface\n", dev->name);

	netif_stop_queue(dev);

	/* Stop transmitter */
	sky2_write32(hw, Q_ADDR(txqaddr[port], Q_CSR), BMU_STOP);
	sky2_read32(hw, Q_ADDR(txqaddr[port], Q_CSR));

	sky2_write32(hw, RB_ADDR(txqaddr[port], RB_CTRL),
		     RB_RST_SET|RB_DIS_OP_MD);

	ctrl = gma_read16(hw, port, GM_GP_CTRL);
	ctrl &= ~(GM_GPCR_TX_ENA|GM_GPCR_RX_ENA);
	gma_write16(hw, port, GM_GP_CTRL, ctrl);

	sky2_write8(hw, SK_REG(port, GPHY_CTRL), GPC_RST_SET);

	/* Workaround shared GMAC reset */
	if (! (hw->chip_id == CHIP_ID_YUKON_XL && hw->chip_rev == 0
	       && port == 0 && hw->dev[1] && netif_running(hw->dev[1])))
		sky2_write8(hw, SK_REG(port, GMAC_CTRL), GMC_RST_SET);

	/* Disable Force Sync bit and Enable Alloc bit */
	sky2_write8(hw, SK_REG(port, TXA_CTRL),
		    TXA_DIS_FSYNC | TXA_DIS_ALLOC | TXA_STOP_RC);

	/* Stop Interval Timer and Limit Counter of Tx Arbiter */
	sky2_write32(hw, SK_REG(port, TXA_ITI_INI), 0L);
	sky2_write32(hw, SK_REG(port, TXA_LIM_INI), 0L);

	/* Reset the PCI FIFO of the async Tx queue */
	sky2_write32(hw, Q_ADDR(txqaddr[port], Q_CSR), BMU_RST_SET | BMU_FIFO_RST);

	/* Reset the Tx prefetch units */
	sky2_write32(hw, Y2_QADDR(txqaddr[port], PREF_UNIT_CTRL),
		     PREF_UNIT_RST_SET);

	sky2_write32(hw, RB_ADDR(txqaddr[port], RB_CTRL), RB_RST_SET);

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

	/* disable the RAM Buffer receive queue */
	sky2_write8(hw, RB_ADDR(rxqaddr[port], RB_CTRL), RB_DIS_OP_MD);

	for (i = 0; i < 0xffff; i++)
		if (sky2_read8(hw, RB_ADDR(rxqaddr[port], Q_RSL))
		    == sky2_read8(hw, RB_ADDR(rxqaddr[port], Q_RL)))
			break;

	sky2_write32(hw, Q_ADDR(rxqaddr[port], Q_CSR),
		     BMU_RST_SET | BMU_FIFO_RST);
	/* reset the Rx prefetch unit */
	sky2_write32(hw, Y2_QADDR(rxqaddr[port], PREF_UNIT_CTRL),
		     PREF_UNIT_RST_SET);

	sky2_write8(hw, SK_REG(port, RX_GMF_CTRL_T), GMF_RST_SET);
	sky2_write8(hw, SK_REG(port, TX_GMF_CTRL_T), GMF_RST_SET);

	/* turn off led's */
	sky2_write16(hw, B0_Y2LED, LED_STAT_OFF);

	sky2_tx_clean(sky2);
	sky2_rx_clean(sky2);

	pci_free_consistent(hw->pdev, RX_LE_BYTES,
			    sky2->rx_le, sky2->rx_le_map);
	kfree(sky2->rx_ring);

	pci_free_consistent(hw->pdev,
			    TX_RING_SIZE * sizeof(struct sky2_tx_le),
			    sky2->tx_le, sky2->tx_le_map);
	kfree(sky2->tx_ring);

	return 0;
}

static u16 sky2_phy_speed(const struct sky2_hw *hw, u16 aux)
{
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
	sky2_write8(hw, GMAC_IRQ_MSK, GMAC_DEF_MSK);

	reg = gma_read16(hw, port, GM_GP_CTRL);
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

	if (netif_msg_link(sky2))
		printk(KERN_INFO PFX
		       "%s: Link is up at %d Mbps, %s duplex, flowcontrol %s\n",
		       sky2->netdev->name, sky2->speed,
		       sky2->duplex == DUPLEX_FULL ? "full" : "half",
		       (sky2->tx_pause && sky2->rx_pause) ? "both" :
		       sky2->tx_pause ? "tx" :
		       sky2->rx_pause ? "rx" : "none");
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
				  gm_phy_read(hw, port,
						   PHY_MARV_AUNE_ADV)
				  | PHY_M_AN_ASP);
	}

	sky2_phy_reset(hw, port);

	netif_carrier_off(sky2->netdev);
	netif_stop_queue(sky2->netdev);

	/* Turn on link LED */
	sky2_write8(hw, SK_REG(port, LNK_LED_REG), LINKLED_OFF);

	if (netif_msg_link(sky2))
		printk(KERN_INFO PFX "%s: Link is down.\n", sky2->netdev->name);
	sky2_phy_init(hw, port);
}


/*
 * Interrrupt from PHY are handled in tasklet (soft irq)
 * because accessing phy registers requires spin wait which might
 * cause excess interrupt latency.
 */
static void sky2_phy_task(unsigned long data)
{
	struct sky2_port *sky2 = (struct sky2_port *) data;
	struct sky2_hw *hw = sky2->hw;
	unsigned port = sky2->port;
	u16 istatus, phystat;

	istatus = gm_phy_read(hw, port, PHY_MARV_INT_STAT);

	phystat = gm_phy_read(hw, port, PHY_MARV_PHY_STAT);

	if (netif_msg_intr(sky2))
		printk(KERN_INFO PFX "%s: phy interrupt status 0x%x 0x%x\n",
		       sky2->netdev->name, istatus, phystat);

	if (istatus & PHY_M_IS_AN_COMPL) {
		u16 lpa = gm_phy_read(hw, port, PHY_MARV_AUNE_LP);

		if (lpa & PHY_M_AN_RF) {
			printk(KERN_ERR PFX "%s: remote fault",
			       sky2->netdev->name);
		}
		else if (hw->chip_id != CHIP_ID_YUKON_FE
			 && gm_phy_read(hw, port, PHY_MARV_1000T_STAT)
			 & PHY_B_1000S_MSF) {
			printk(KERN_ERR PFX "%s: master/slave fault",
			       sky2->netdev->name);
		}
		else if (!(phystat & PHY_M_PS_SPDUP_RES)) {
			printk(KERN_ERR PFX "%s: speed/duplex mismatch",
			       sky2->netdev->name);
		}
		else {
			sky2->duplex = (phystat & PHY_M_PS_FULL_DUP)
				? DUPLEX_FULL : DUPLEX_HALF;

			sky2->speed = sky2_phy_speed(hw, phystat);
			
			sky2->tx_pause = (phystat & PHY_M_PS_TX_P_EN) != 0;
			sky2->rx_pause = (phystat & PHY_M_PS_RX_P_EN) != 0;

			if ((!sky2->tx_pause && !sky2->rx_pause) ||
			    (sky2->speed < SPEED_1000 && sky2->duplex == DUPLEX_HALF))
				sky2_write8(hw, SK_REG(port, GMAC_CTRL), GMC_PAUSE_OFF);
			else
				sky2_write8(hw, SK_REG(port, GMAC_CTRL), GMC_PAUSE_ON);
			sky2_link_up(sky2);
		}
	} else {

		if (istatus & PHY_M_IS_LSP_CHANGE)
			sky2->speed = sky2_phy_speed(hw, phystat);

		if (istatus & PHY_M_IS_DUP_CHANGE)
			sky2->duplex = (phystat & PHY_M_PS_FULL_DUP) ? DUPLEX_FULL : DUPLEX_HALF;
		if (istatus & PHY_M_IS_LST_CHANGE) {
			if (phystat & PHY_M_PS_LINK_UP)
				sky2_link_up(sky2);
			else
				sky2_link_down(sky2);
		}
	}

	local_irq_disable();
	hw->intr_mask |= (port == 0) ? Y2_IS_IRQ_PHY1 : Y2_IS_IRQ_PHY2;
	sky2_write32(hw, B0_IMSK, hw->intr_mask);
	local_irq_enable();
}

static void sky2_tx_timeout(struct net_device *dev)
{
	struct sky2_port *sky2 = netdev_priv(dev);

	if (netif_msg_timer(sky2))
		printk(KERN_ERR PFX "%s: tx timeout\n", dev->name);

	sky2_write32(sky2->hw, Q_ADDR(txqaddr[sky2->port], Q_CSR), BMU_STOP);
	sky2_read32(sky2->hw, Q_ADDR(txqaddr[sky2->port], Q_CSR));

	sky2_tx_clean(sky2);
}

static int sky2_change_mtu(struct net_device *dev, int new_mtu)
{
	int err = 0;

	if (new_mtu < ETH_ZLEN || new_mtu > ETH_JUMBO_MTU)
		return -EINVAL;

	if (netif_running(dev))
		sky2_down(dev);

	dev->mtu = new_mtu;

	if (netif_running(dev))
		err = sky2_up(dev);

	return err;
}

/*
 * Receive one packet.
 * For small packets or errors, just reuse existing skb.
 * For larger pakects, get new buffer.
 */
static struct sk_buff *sky2_receive(struct sky2_hw *hw, unsigned port,
				    u16 length, u32 status)
{
	struct net_device *dev = hw->dev[port];
	struct sky2_port *sky2 = netdev_priv(dev);
	struct ring_info *re = sky2->rx_ring + sky2->rx_next;
	struct sk_buff *skb = re->skb;
	dma_addr_t mapping;
	const unsigned int rx_buf_size = dev->mtu + ETH_HLEN + 8;

	if (unlikely(netif_msg_rx_status(sky2)))
		printk(KERN_DEBUG PFX "%s: rx slot %u status 0x%x len %d\n",
		       dev->name, sky2->rx_next, status, length);

	sky2->rx_next = (sky2->rx_next + 1) % sky2->rx_ring_size;

	pci_unmap_single(sky2->hw->pdev,
			 pci_unmap_addr(re, mapaddr),
			 pci_unmap_len(re, maplen),
			 PCI_DMA_FROMDEVICE);
	prefetch(skb->data);

	if (!(status & GMR_FS_RX_OK) 
	    || (status & GMR_FS_ANY_ERR) 
	    || (length << 16) != (status & GMR_FS_LEN) 
	    || length > rx_buf_size) 
		goto error;

	re->skb = sky2_rx_alloc_skb(sky2, rx_buf_size, GFP_ATOMIC);
	if (!re->skb) 
		goto reuse;

submit:
	mapping = pci_map_single(sky2->hw->pdev, re->skb->data,
				 rx_buf_size, PCI_DMA_FROMDEVICE);

	pci_unmap_len_set(re, maplen, rx_buf_size);
	pci_unmap_addr_set(re, mapaddr, mapping);

	sky2_rx_add(sky2, mapping, rx_buf_size);
	sky2_put_idx(sky2->hw, rxqaddr[sky2->port],
		     sky2->rx_put, &sky2->rx_last_put, RX_LE_SIZE);

	return skb;

error:
	if (netif_msg_rx_err(sky2))
		printk(KERN_INFO PFX "%s: rx error, status 0x%x length %d\n",
		       sky2->netdev->name, status, length);
	
	if (status & (GMR_FS_LONG_ERR|GMR_FS_UN_SIZE))
		sky2->net_stats.rx_length_errors++;
	if (status & GMR_FS_FRAGMENT)
		sky2->net_stats.rx_frame_errors++;
	if (status & GMR_FS_CRC_ERR)
		sky2->net_stats.rx_crc_errors++;
reuse:
	re->skb = skb;
	skb = NULL;
	goto submit;
}

static u16 get_tx_index(u8 port, u32 status, u16 len)
{
	if (port == 0)
		return status & 0xfff;
	else
		return ((status >> 24) & 0xff) | (len & 0xf) << 8;
}

/*
 * NAPI poll routine.
 * Both ports share the same status interrupt, therefore there is only
 * one poll routine.
 *
 */
static int sky2_poll(struct net_device *dev, int *budget)
{
	struct sky2_port *sky2 = netdev_priv(dev);
	struct sky2_hw *hw = sky2->hw;
	unsigned int to_do = min(dev->quota, *budget);
	unsigned int work_done = 0;
	unsigned char summed[2] = { CHECKSUM_NONE, CHECKSUM_NONE };
	unsigned int csum[2] = { 0 };
	unsigned int rx_handled[2] = { 0, 0};
	u16 last;

	sky2_write32(hw, STAT_CTRL, SC_STAT_CLR_IRQ);
	last = sky2_read16(hw, STAT_PUT_IDX);

	while (hw->st_idx != last && work_done < to_do) {
		struct sky2_status_le *le = hw->st_le + hw->st_idx;
		struct sk_buff *skb;
		u8 port;
		u32 status;
		u16 length;

		rmb();
		status = le32_to_cpu(le->status);
		length = le16_to_cpu(le->length);
		port = le->link;

		BUG_ON(port >= hw->ports);

		switch(le->opcode & ~HW_OWNER) {
		case OP_RXSTAT:
			++rx_handled[port];
			skb = sky2_receive(hw, port, length, status);
			if (likely(skb)) {
				__skb_put(skb, length);
				skb->protocol = eth_type_trans(skb, dev);

				/* Add hw checksum if available */
				skb->ip_summed = summed[port];
				skb->csum = csum[port];

				/* Clear for next packet */
				csum[port] = 0;
				summed[port] = CHECKSUM_NONE;

				netif_receive_skb(skb);

				dev->last_rx = jiffies;
				++work_done;
			}
			break;

		case OP_RXCHKS:
			/* Save computed checksum for next rx */
			csum[port] = le16_to_cpu(status & 0xffff);
			summed[port] = CHECKSUM_HW;
			break;

		case OP_TXINDEXLE:
			sky2_tx_complete(hw->dev[port],
					 get_tx_index(port, status, length));
			break;

		case OP_RXTIMESTAMP:
			break;

		default:
			if (net_ratelimit())
				printk(KERN_WARNING PFX "unknown status opcode 0x%x\n",
				       le->opcode);
			break;
		}

		hw->st_idx = (hw->st_idx + 1) & (STATUS_RING_SIZE -1);
	}

	*budget -= work_done;
	dev->quota -= work_done;
	if (work_done < to_do) {
		/*
		 * Another chip workaround, need to restart TX timer if status
		 * LE was handled. WA_DEV_43_418
		 */
		if (is_ec_a1(hw)) {
			sky2_write8(hw, STAT_TX_TIMER_CTRL, TIM_STOP);
			sky2_write8(hw, STAT_TX_TIMER_CTRL, TIM_START);
		}

		hw->intr_mask |= Y2_IS_STAT_BMU;
		sky2_write32(hw, B0_IMSK, hw->intr_mask);
		netif_rx_complete(dev);
	}

	return work_done >= to_do;

}

static void sky2_hw_error(struct sky2_hw *hw, unsigned port, u32 status)
{
	struct net_device *dev = hw->dev[port];

	printk(KERN_INFO PFX "%s: hw error interrupt status 0x%x\n",
	       dev->name, status);

	if (status & Y2_IS_PAR_RD1) {
		printk(KERN_ERR PFX "%s: ram data read parity error\n",
		       dev->name);
		/* Clear IRQ */
		sky2_write16(hw, RAM_BUFFER(port, B3_RI_CTRL), RI_CLR_RD_PERR);
	}

	if (status & Y2_IS_PAR_WR1) {
		printk(KERN_ERR PFX "%s: ram data write parity error\n",
		       dev->name);

		sky2_write16(hw, RAM_BUFFER(port, B3_RI_CTRL), RI_CLR_WR_PERR);
	}

	if (status & Y2_IS_PAR_MAC1) {
		printk(KERN_ERR PFX "%s: MAC parity error\n", dev->name);
		sky2_write8(hw, SK_REG(port, TX_GMF_CTRL_T), GMF_CLI_TX_PE);
	}

	if (status & Y2_IS_PAR_RX1) {
		printk(KERN_ERR PFX "%s: RX parity error\n", dev->name);
		sky2_write32(hw, Q_ADDR(rxqaddr[port], Q_CSR), BMU_CLR_IRQ_PAR);
	}

	if (status & Y2_IS_TCP_TXA1) {
		printk(KERN_ERR PFX "%s: TCP segmentation error\n", dev->name);
		sky2_write32(hw, Q_ADDR(txqaddr[port], Q_CSR), BMU_CLR_IRQ_TCP);
	}
}

static void sky2_hw_intr(struct sky2_hw *hw)
{
	u32 status = sky2_read32(hw, B0_HWE_ISRC);

	if (status & Y2_IS_TIST_OV) {
		pr_debug (PFX "%s: unused timer overflow??\n", 
			  pci_name(hw->pdev));
		sky2_write8(hw, GMAC_TI_ST_CTRL, GMT_ST_CLR_IRQ);
	}

	if (status & (Y2_IS_MST_ERR | Y2_IS_IRQ_STAT)) {
		u16 pci_err = sky2_read16(hw, PCI_C(PCI_STATUS));
		printk(KERN_ERR PFX "%s: pci hw error (0x%x)\n",
		       pci_name(hw->pdev), pci_err);

		sky2_write8(hw, B2_TST_CTRL1, TST_CFG_WRITE_ON);
		sky2_write16(hw, PCI_C(PCI_STATUS),
			     pci_err | PCI_STATUS_ERROR_BITS);
		sky2_write8(hw, B2_TST_CTRL1, TST_CFG_WRITE_OFF);
	}

	if (status & Y2_IS_PCI_EXP) {
		/* PCI-Express uncorrectable Error occured */
		u32 pex_err =  sky2_read32(hw, PCI_C(PEX_UNC_ERR_STAT));

		/*
		 * On PCI-Express bus bridges are called root complexes.
		 * PCI-Express errors are recognized by the root complex too,
		 * which requests the system to handle the problem. After error
		 * occurence it may be that no access to the adapter may be performed
		 * any longer.
		 */
		printk(KERN_ERR PFX "%s: pci express error (0x%x)\n",
		       pci_name(hw->pdev), pex_err);

		/* clear the interrupt */
		sky2_write32(hw, B2_TST_CTRL1, TST_CFG_WRITE_ON);
		sky2_write32(hw, PCI_C(PEX_UNC_ERR_STAT), 0xffffffffUL);
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

static void sky2_phy_intr(struct sky2_hw *hw, unsigned port)
{
	struct net_device *dev = hw->dev[port];
	struct sky2_port *sky2 = netdev_priv(dev);

	hw->intr_mask &= ~(port == 0 ? Y2_IS_IRQ_PHY1 : Y2_IS_IRQ_PHY2);
	sky2_write32(hw, B0_IMSK, hw->intr_mask);
	tasklet_schedule(&sky2->phy_task);
}

static irqreturn_t sky2_intr(int irq, void *dev_id, struct pt_regs *regs)
{
	struct sky2_hw *hw = dev_id;
	u32 status;

	status = sky2_read32(hw, B0_Y2_SP_ISRC2);
	if (status == 0 || status == ~0) /* hotplug or shared irq */
		return IRQ_NONE;

	if (status & Y2_IS_HW_ERR)
		sky2_hw_intr(hw);

	if ((status & Y2_IS_STAT_BMU) && netif_rx_schedule_prep(hw->dev[0])) {
		hw->intr_mask &= ~Y2_IS_STAT_BMU;
		sky2_write32(hw, B0_IMSK, hw->intr_mask);
		__netif_rx_schedule(hw->dev[0]);
	}

	if (status & Y2_IS_IRQ_PHY1) 
		sky2_phy_intr(hw, 0);

	if (status & Y2_IS_IRQ_PHY2)
		sky2_phy_intr(hw, 1);

	if (status & Y2_IS_IRQ_MAC1)
		sky2_mac_intr(hw, 0);

	if (status & Y2_IS_IRQ_MAC2)
		sky2_mac_intr(hw, 1);


	sky2_write32(hw, B0_Y2_SP_ICR, 2);
	return IRQ_HANDLED;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void sky2_netpoll(struct net_device *dev)
{
	struct sky2_port *sky2 = netdev_priv(dev);

	disable_irq(dev->irq);
	sky2_intr(dev->irq, sky2->hw, NULL);
	enable_irq(dev->irq);
}
#endif

/* Chip internal frequency for clock calculations */
static inline u32 sky2_khz(const struct sky2_hw *hw)
{
	switch(hw->chip_id) {
	case CHIP_ID_YUKON_EC:
		return 125000;	/* 125 Mhz */
	case CHIP_ID_YUKON_FE:
		return 100000;	/* 100 Mhz */
	default: /* YUKON_XL */
		return 156000;	/* 156 Mhz */
	}
}

static inline u32 sky2_ms2clk(const struct sky2_hw *hw, u32 ms)
{
	return sky2_khz(hw) * ms;
}

static inline u32 sky2_us2clk(const struct sky2_hw *hw, u32 us)
{
	return (sky2_khz(hw) * 75) / 1000;
}

static int sky2_reset(struct sky2_hw *hw)
{
	u32 ctst, power;
	u16 status;
	u8 t8, pmd_type;
	int i;

	ctst = sky2_read32(hw, B0_CTST);

	sky2_write8(hw, B0_CTST, CS_RST_CLR);
	hw->chip_id = sky2_read8(hw, B2_CHIP_ID);
	if (hw->chip_id < CHIP_ID_YUKON_XL || hw->chip_id > CHIP_ID_YUKON_FE) {
		printk(KERN_ERR PFX "%s: unsupported chip type 0x%x\n",
		       pci_name(hw->pdev), hw->chip_id);
		return -EOPNOTSUPP;
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
	status = sky2_read16(hw, PCI_C(PCI_STATUS));
	sky2_write8(hw, B2_TST_CTRL1, TST_CFG_WRITE_ON);
	sky2_write16(hw, PCI_C(PCI_STATUS),
		     status | PCI_STATUS_ERROR_BITS);

	sky2_write8(hw, B0_CTST, CS_MRST_CLR);

	/* clear any PEX errors */
	if (is_pciex(hw)) {
		sky2_write32(hw, PCI_C(PEX_UNC_ERR_STAT), 0xffffffffUL);
		sky2_read16(hw, PCI_C(PEX_LNK_STAT));
	}

	pmd_type = sky2_read8(hw, B2_PMD_TYP);
	hw->copper = !(pmd_type == 'L' || pmd_type == 'S');

	hw->ports = 1;
	t8 = sky2_read8(hw, B2_Y2_HW_RES);
	if ((t8 & CFG_DUAL_MAC_MSK) == CFG_DUAL_MAC_MSK) {
		if (!(sky2_read8(hw, B2_Y2_CLK_GATE) & Y2_STATUS_LNK2_INAC))
			++hw->ports;
	}
	hw->chip_rev = (sky2_read8(hw, B2_MAC_CFG) & CFG_CHIP_R_MSK) >> 4;

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
	power = sky2_read32(hw, PCI_C(PCI_DEV_REG1));
	power &= ~(PCI_Y2_PHY1_POWD|PCI_Y2_PHY2_POWD);

	/* back asswards .. */
	if (hw->chip_id == CHIP_ID_YUKON_XL && hw->chip_rev > 1) {
		power |= PCI_Y2_PHY1_COMA;
		if (hw->ports > 1)
			power |= PCI_Y2_PHY2_COMA;
	}
	sky2_write32(hw, PCI_C(PCI_DEV_REG1), power);

	for (i = 0; i < hw->ports; i++) {
		sky2_write8(hw, SK_REG(i, GMAC_LINK_CTRL), GMLC_RST_SET);
		sky2_write8(hw, SK_REG(i, GMAC_LINK_CTRL), GMLC_RST_CLR);
	}

	sky2_write8(hw, B2_TST_CTRL1, TST_CFG_WRITE_OFF);

	sky2_write32(hw, B2_I2C_IRQ, 1); /* Clear I2C IRQ noise */

	/* turn off hardware timer (unused) */
	sky2_write8(hw, B2_TI_CTRL, TIM_STOP);
	sky2_write8(hw, B2_TI_CTRL, TIM_CLR_IRQ);
	
	sky2_write8(hw, B0_Y2LED, LED_STAT_ON);

	/* Turn on descriptor polling  -- is this necessary? */
	sky2_write32(hw, B28_DPT_INI, sky2_us2clk(hw, 75));
	sky2_write8(hw, B28_DPT_CTRL, DPT_START);

	/* Turn off receive timestamp */
	sky2_write8(hw, GMAC_TI_ST_CTRL, GMT_ST_STOP);

	/* enable the Tx Arbiters */
	for (i = 0; i < hw->ports; i++)
		sky2_write8(hw, SK_REG(i, TXA_CTRL), TXA_ENA_ARB);

	/* Initialize ram interface */
	for (i = 0; i < hw->ports; i++) {
		sky2_write16(hw, RAM_BUFFER(i, B3_RI_CTRL), RI_RST_CLR);

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

	/* Optimize PCI Express access */
	if (is_pciex(hw)) {
		u16 ctrl = sky2_read32(hw, PCI_C(PEX_DEV_CTRL));
		ctrl &= ~PEX_DC_MAX_RRS_MSK;
		ctrl |= PEX_DC_MAX_RD_RQ_SIZE(4);
		sky2_write8(hw, B2_TST_CTRL1, TST_CFG_WRITE_ON);
		sky2_write16(hw, PCI_C(PEX_DEV_CTRL), ctrl);
		sky2_write8(hw, B2_TST_CTRL1, TST_CFG_WRITE_OFF);
	}

	sky2_write32(hw, B0_HWE_IMSK, Y2_HWE_ALL_MASK);

	hw->intr_mask = Y2_IS_BASE;
	sky2_write32(hw, B0_IMSK, hw->intr_mask);

	/* disable all GMAC IRQ's */
	sky2_write8(hw, GMAC_IRQ_MSK, 0);

	spin_lock_bh(&hw->phy_lock);
	for (i = 0; i < hw->ports; i++)
		sky2_phy_reset(hw, i);
	spin_unlock_bh(&hw->phy_lock);

	/* Setup ring for status responses */
	hw->st_le = pci_alloc_consistent(hw->pdev, STATUS_LE_BYTES,
					 &hw->st_dma);
	if (!hw->st_le)
		return -ENOMEM;

	memset(hw->st_le, 0, STATUS_LE_BYTES);
	hw->st_idx = 0;

	sky2_write32(hw, STAT_CTRL, SC_STAT_RST_SET);
	sky2_write32(hw, STAT_CTRL, SC_STAT_RST_CLR);

	sky2_write32(hw, STAT_LIST_ADDR_LO, hw->st_dma);
	sky2_write32(hw, STAT_LIST_ADDR_HI, (u64)hw->st_dma >> 32);

	/* Set the list last index */
	sky2_write16(hw, STAT_LAST_IDX, STATUS_RING_SIZE-1);

	if (is_ec_a1(hw)) {
		/* WA for dev. #4.3 */
		sky2_write16(hw, STAT_TX_IDX_TH, ST_TXTH_IDX_MASK);

		/* set Status-FIFO watermark */
		sky2_write8(hw, STAT_FIFO_WM, 0x21);	/* WA for dev. #4.18 */

		/* set Status-FIFO ISR watermark */
		sky2_write8(hw, STAT_FIFO_ISR_WM, 0x07);/* WA for dev. #4.18 */

		/* WA for dev. #4.3 and #4.18 */
		/* set Status-FIFO Tx timer init value */
		sky2_write32(hw, STAT_TX_TIMER_INI, sky2_ms2clk(hw, 10));
	} else {
		/*
		 * Theses settings should avoid the
		 * temporary hanging of the status BMU.
		 * May be not all required... still under investigation...
		 */
		sky2_write16(hw, STAT_TX_IDX_TH, 0x000a);

		/* set Status-FIFO watermark */
		sky2_write8(hw, STAT_FIFO_WM, 0x10);

		/* set Status-FIFO ISR watermark */
		if (hw->chip_id == CHIP_ID_YUKON_XL && hw->chip_rev == 0)
			sky2_write8(hw, STAT_FIFO_ISR_WM, 0x10);

		else	 /* WA 4109 */
			sky2_write8(hw, STAT_FIFO_ISR_WM, 0x04);

		sky2_write32(hw, STAT_ISR_TIMER_INI, 0x0190);
	}

	/* enable the prefetch unit */
	/* operational bit not functional for Yukon-EC, but fixed in Yukon-2? */
	sky2_write32(hw, STAT_CTRL, SC_STAT_OP_ON);

	sky2_write8(hw, STAT_TX_TIMER_CTRL, TIM_START);
	sky2_write8(hw, STAT_LEV_TIMER_CTRL, TIM_START);
	sky2_write8(hw, STAT_ISR_TIMER_CTRL, TIM_START);

	return 0;
}

static inline u32 sky2_supported_modes(const struct sky2_hw *hw)
{
	u32 modes;
	if (hw->copper) {
		modes =  SUPPORTED_10baseT_Half
			| SUPPORTED_10baseT_Full
			| SUPPORTED_100baseT_Half
			| SUPPORTED_100baseT_Full
			| SUPPORTED_Autoneg| SUPPORTED_TP;

		if (hw->chip_id != CHIP_ID_YUKON_FE)
			modes |= SUPPORTED_1000baseT_Half
				| SUPPORTED_1000baseT_Full;
	} else
		modes = SUPPORTED_1000baseT_Full | SUPPORTED_FIBRE
			| SUPPORTED_Autoneg;
	return modes;
}

static int sky2_get_settings(struct net_device *dev,
			     struct ethtool_cmd *ecmd)
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
			| SUPPORTED_Autoneg| SUPPORTED_TP;
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

		switch(ecmd->speed) {
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

	if (netif_running(dev)) {
		sky2_down(dev);
		sky2_up(dev);
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
	char 	   name[ETH_GSTRING_LEN];
	u16	   offset;
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
	{ "collisions",    GM_TXF_SNG_COL },
	{ "late_collision",GM_TXF_LAT_COL },
	{ "aborted", 	   GM_TXF_ABO_COL },
	{ "multi_collisions", GM_TXF_MUL_COL },
	{ "fifo_underrun", GM_TXE_FIFO_UR },
	{ "fifo_overflow", GM_RXE_FIFO_OV },
	{ "rx_toolong",    GM_RXF_LNG_ERR },
	{ "rx_jabber",     GM_RXF_JAB_PKT },
	{ "rx_runt", 	   GM_RXE_FRAG },
	{ "rx_too_long",   GM_RXF_LNG_ERR },
	{ "rx_fcs_error",   GM_RXF_FCS_ERR },
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

static void sky2_phy_stats(struct sky2_port *sky2, u64 *data)
{
	struct sky2_hw *hw = sky2->hw;
	unsigned port = sky2->port;
	int i;

	data[0] = (u64) gma_read32(hw, port, GM_TXO_OK_HI) << 32
		| (u64) gma_read32(hw, port, GM_TXO_OK_LO);
	data[1] = (u64) gma_read32(hw, port, GM_RXO_OK_HI) << 32
		| (u64) gma_read32(hw, port, GM_RXO_OK_LO);

	for (i = 2; i < ARRAY_SIZE(sky2_stats); i++)
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
				   struct ethtool_stats *stats, u64 *data)
{
	struct sky2_port *sky2 = netdev_priv(dev);

	sky2_phy_stats(sky2, data);
}

static void sky2_get_strings(struct net_device *dev, u32 stringset, u8 *data)
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
	u64 data[ARRAY_SIZE(sky2_stats)];

	sky2_phy_stats(sky2, data);

	sky2->net_stats.tx_bytes = data[0];
	sky2->net_stats.rx_bytes = data[1];
	sky2->net_stats.tx_packets = data[2] + data[4] + data[6];
	sky2->net_stats.rx_packets = data[3] + data[5] + data[7];
	sky2->net_stats.multicast = data[5] + data[7];
	sky2->net_stats.collisions = data[10];
	sky2->net_stats.tx_aborted_errors = data[12];

	return &sky2->net_stats;
}

static int sky2_set_mac_address(struct net_device *dev, void *p)
{
	struct sky2_port *sky2 = netdev_priv(dev);
	struct sockaddr *addr = p;
	int err = 0;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	sky2_down(dev);
	memcpy(dev->dev_addr, addr->sa_data, ETH_ALEN);
	memcpy_toio(sky2->hw->regs + B2_MAC_1 + sky2->port*8,
		    dev->dev_addr, ETH_ALEN);
	memcpy_toio(sky2->hw->regs + B2_MAC_2 + sky2->port*8,
		    dev->dev_addr, ETH_ALEN);
	if (dev->flags & IFF_UP)
		err = sky2_up(dev);
	return err;
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

	if (dev->flags & IFF_PROMISC) 		/* promiscious */
		reg &= ~(GM_RXCR_UCF_ENA | GM_RXCR_MCF_ENA);
	else if (dev->flags & IFF_ALLMULTI)	/* all multicast */
		memset(filter, 0xff, sizeof(filter));
	else if (dev->mc_count == 0)		/* no multicast */
		reg &= ~GM_RXCR_MCF_ENA;
	else {
		int i;
		reg |= GM_RXCR_MCF_ENA;

		for (i = 0; list && i < dev->mc_count; i++, list = list->next) {
			u32 bit = ether_crc(ETH_ALEN, list->dmi_addr) & 0x3f;
			filter[bit/8] |= 1 << (bit%8);
		}
	}


	gma_write16(hw, port, GM_MC_ADDR_H1,
			 (u16)filter[0] | ((u16)filter[1] << 8));
	gma_write16(hw, port, GM_MC_ADDR_H2,
			 (u16)filter[2] | ((u16)filter[3] << 8));
	gma_write16(hw, port, GM_MC_ADDR_H3,
			 (u16)filter[4] | ((u16)filter[5] << 8));
	gma_write16(hw, port, GM_MC_ADDR_H4,
			 (u16)filter[6] | ((u16)filter[7] << 8));

	gma_write16(hw, port, GM_RX_CTRL, reg);
}

/* Can have one global because blinking is controlled by
 * ethtool and that is always under RTNL mutex
 */
static inline void sky2_led(struct sky2_hw *hw, unsigned port, int on)
{
	spin_lock_bh(&hw->phy_lock);
	gm_phy_write(hw, port, PHY_MARV_LED_CTRL, 0);
	if (on)
		gm_phy_write(hw, port, PHY_MARV_LED_OVER,
			     PHY_M_LED_MO_DUP(MO_LED_ON)  |
			     PHY_M_LED_MO_10(MO_LED_ON)   |
			     PHY_M_LED_MO_100(MO_LED_ON)  |
			     PHY_M_LED_MO_1000(MO_LED_ON) |
			     PHY_M_LED_MO_RX(MO_LED_ON));
	else
		gm_phy_write(hw, port, PHY_MARV_LED_OVER,

			     PHY_M_LED_MO_DUP(MO_LED_OFF)  |
			     PHY_M_LED_MO_10(MO_LED_OFF)   |
			     PHY_M_LED_MO_100(MO_LED_OFF)  |
			     PHY_M_LED_MO_1000(MO_LED_OFF) |
			     PHY_M_LED_MO_RX(MO_LED_OFF));

	spin_unlock_bh(&hw->phy_lock);
}

/* blink LED's for finding board */
static int sky2_phys_id(struct net_device *dev, u32 data)
{
	struct sky2_port *sky2 = netdev_priv(dev);
	struct sky2_hw *hw = sky2->hw;
	unsigned port = sky2->port;
	u16 ledctrl, ledover;
	long ms;
	int onoff = 1;

	if (!data || data > (u32)(MAX_SCHEDULE_TIMEOUT / HZ))
		ms = jiffies_to_msecs(MAX_SCHEDULE_TIMEOUT);
	else
		ms = data * 1000;

	/* save initial values */
	spin_lock_bh(&hw->phy_lock);
	ledctrl = gm_phy_read(hw, port, PHY_MARV_LED_CTRL);
	ledover = gm_phy_read(hw, port, PHY_MARV_LED_OVER);
	spin_unlock_bh(&hw->phy_lock);

	while (ms > 0) {
		sky2_led(hw, port, onoff);
		onoff = !onoff;

		if (msleep_interruptible(250))
			break;	/* interrupted */
		ms -= 250;
	}

	/* resume regularly scheduled programming */
	spin_lock_bh(&hw->phy_lock);
	gm_phy_write(hw, port, PHY_MARV_LED_CTRL, ledctrl);
	gm_phy_write(hw, port, PHY_MARV_LED_OVER, ledover);
	spin_unlock_bh(&hw->phy_lock);

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

	if (netif_running(dev)) {
		sky2_down(dev);
		err = sky2_up(dev);
	}

	return err;
}

#ifdef CONFIG_PM
static void sky2_get_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	struct sky2_port *sky2 = netdev_priv(dev);

	wol->supported = WAKE_MAGIC;
	wol->wolopts = sky2->wol ? WAKE_MAGIC : 0;
}

static int sky2_set_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	struct sky2_port *sky2 = netdev_priv(dev);
	struct sky2_hw *hw = sky2->hw;

	if (wol->wolopts != WAKE_MAGIC && wol->wolopts != 0)
		return -EOPNOTSUPP;

	sky2->wol = wol->wolopts == WAKE_MAGIC;

	if (sky2->wol) {
		memcpy_toio(hw->regs + WOL_MAC_ADDR, dev->dev_addr, ETH_ALEN);

		sky2_write16(hw, WOL_CTRL_STAT,
			     WOL_CTL_ENA_PME_ON_MAGIC_PKT |
			     WOL_CTL_ENA_MAGIC_PKT_UNIT);
	} else
		sky2_write16(hw, WOL_CTRL_STAT, WOL_CTL_DEFAULT);

	return 0;
}
#endif


static struct ethtool_ops sky2_ethtool_ops = {
	.get_settings	= sky2_get_settings,
	.set_settings	= sky2_set_settings,
	.get_drvinfo	= sky2_get_drvinfo,
	.get_msglevel	= sky2_get_msglevel,
	.set_msglevel	= sky2_set_msglevel,
	.get_link	= ethtool_op_get_link,
	.get_sg		= ethtool_op_get_sg,
	.set_sg		= ethtool_op_set_sg,
	.get_tx_csum	= ethtool_op_get_tx_csum,
	.set_tx_csum	= ethtool_op_set_tx_csum,
	.get_tso	= ethtool_op_get_tso,
	.set_tso	= ethtool_op_set_tso,
	.get_rx_csum	= sky2_get_rx_csum,
	.set_rx_csum	= sky2_set_rx_csum,
	.get_strings	= sky2_get_strings,
	.get_pauseparam = sky2_get_pauseparam,
	.set_pauseparam = sky2_set_pauseparam,
#ifdef CONFIG_PM
	.get_wol	= sky2_get_wol,
	.set_wol	= sky2_set_wol,
#endif
	.phys_id	= sky2_phys_id,
	.get_stats_count = sky2_get_stats_count,
	.get_ethtool_stats = sky2_get_ethtool_stats,
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
	dev->open = sky2_up;
	dev->stop = sky2_down;
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
	dev->irq = hw->pdev->irq;

	sky2 = netdev_priv(dev);
	sky2->netdev = dev;
	sky2->hw = hw;
	sky2->msg_enable = netif_msg_init(debug, default_msg);

	spin_lock_init(&sky2->tx_lock);
	/* Auto speed and flow control */
	sky2->autoneg = AUTONEG_ENABLE;
	sky2->tx_pause = 0;
	sky2->rx_pause = 1;
	sky2->duplex = -1;
	sky2->speed = -1;
	sky2->advertising = sky2_supported_modes(hw);
	sky2->rx_csum = 1;
	sky2->rx_ring_size = is_ec_a1(hw) ? MIN_RX_BUFFERS : MAX_RX_BUFFERS;
	tasklet_init(&sky2->phy_task, sky2_phy_task, (unsigned long) sky2);

	hw->dev[port] = dev;

	sky2->port = port;

	dev->features |= NETIF_F_LLTX;
	if (highmem)
		dev->features |= NETIF_F_HIGHDMA;
	dev->features |= NETIF_F_IP_CSUM | NETIF_F_SG | NETIF_F_TSO;

	/* read the mac address */
	memcpy_fromio(dev->dev_addr, hw->regs + B2_MAC_1 + port*8, ETH_ALEN);

	/* device is off until link detection */
	netif_carrier_off(dev);
	netif_stop_queue(dev);

	return dev;
}

static inline void sky2_show_addr(struct net_device *dev)
{
	const struct sky2_port *sky2 = netdev_priv(dev);

	if (netif_msg_probe(sky2))
		printk(KERN_INFO PFX "%s: addr %02x:%02x:%02x:%02x:%02x:%02x\n",
		       dev->name,
		       dev->dev_addr[0], dev->dev_addr[1], dev->dev_addr[2],
		       dev->dev_addr[3], dev->dev_addr[4], dev->dev_addr[5]);
}

static int __devinit sky2_probe(struct pci_dev *pdev,
				const struct pci_device_id *ent)
{
	struct net_device *dev, *dev1;
	struct sky2_hw *hw;
	int err, using_dac = 0;

	if ((err = pci_enable_device(pdev))) {
		printk(KERN_ERR PFX "%s cannot enable PCI device\n",
		       pci_name(pdev));
		goto err_out;
	}

	if ((err = pci_request_regions(pdev, DRV_NAME))) {
		printk(KERN_ERR PFX "%s cannot obtain PCI resources\n",
		       pci_name(pdev));
		goto err_out_disable_pdev;
	}

	pci_set_master(pdev);

	if (sizeof(dma_addr_t) > sizeof(u32)) {
		err = pci_set_dma_mask(pdev, DMA_64BIT_MASK);
		if (!err)
			using_dac = 1;
	}

	if (!using_dac) {
		err = pci_set_dma_mask(pdev, DMA_32BIT_MASK);
		if (err) {
			printk(KERN_ERR PFX "%s no usable DMA configuration\n",
			       pci_name(pdev));
			goto err_out_free_regions;
		}
	}

#ifdef __BIG_ENDIAN
	/* byte swap decriptors in hardware */
	{
		u32 reg;

		pci_read_config_dword(pdev, PCI_DEV_REG2, &reg);
		reg |= PCI_REV_DESC;
		pci_write_config_dword(pdev, PCI_DEV_REG2, reg);
	}
#endif

	err = -ENOMEM;
	hw = kmalloc(sizeof(*hw), GFP_KERNEL);
	if (!hw) {
		printk(KERN_ERR PFX "%s: cannot allocate hardware struct\n",
		       pci_name(pdev));
		goto err_out_free_regions;
	}

	memset(hw, 0, sizeof(*hw));
	hw->pdev = pdev;
	spin_lock_init(&hw->phy_lock);

	hw->regs = ioremap_nocache(pci_resource_start(pdev, 0), 0x4000);
	if (!hw->regs) {
		printk(KERN_ERR PFX "%s: cannot map device registers\n",
		       pci_name(pdev));
		goto err_out_free_hw;
	}

	err = request_irq(pdev->irq, sky2_intr, SA_SHIRQ, DRV_NAME, hw);
	if (err) {
		printk(KERN_ERR PFX "%s: cannot assign irq %d\n",
		       pci_name(pdev), pdev->irq);
		goto err_out_iounmap;
	}
	pci_set_drvdata(pdev, hw);

	err = sky2_reset(hw);
	if (err)
		goto err_out_free_irq;

	printk(KERN_INFO PFX "addr 0x%lx irq %d chip 0x%x (%s) rev %d\n",
	       pci_resource_start(pdev, 0), pdev->irq,
	       hw->chip_id, chip_name(hw->chip_id), hw->chip_rev);

	if ((dev = sky2_init_netdev(hw, 0, using_dac)) == NULL)
		goto err_out_free_pci;

	if ((err = register_netdev(dev))) {
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
			printk(KERN_WARNING PFX "register of second port failed\n");
			hw->dev[1] = NULL;
			free_netdev(dev1);
		}
	}

	return 0;

err_out_free_netdev:
	free_netdev(dev);

err_out_free_irq:
	free_irq(pdev->irq, hw);
err_out_free_pci:
	pci_free_consistent(hw->pdev, STATUS_LE_BYTES, hw->st_le, hw->st_dma);
err_out_iounmap:
	iounmap(hw->regs);
err_out_free_hw:
	kfree(hw);
err_out_free_regions:
	pci_release_regions(pdev);
err_out_disable_pdev:
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
err_out:
	return err;
}

static void __devexit sky2_remove(struct pci_dev *pdev)
{
	struct sky2_hw *hw  = pci_get_drvdata(pdev);
	struct net_device *dev0, *dev1;

	if(!hw)
		return;

	if ((dev1 = hw->dev[1]))
		unregister_netdev(dev1);
	dev0 = hw->dev[0];
	unregister_netdev(dev0);

	sky2_write16(hw, B0_Y2LED, LED_STAT_OFF);

	free_irq(pdev->irq, hw);
	pci_free_consistent(pdev, STATUS_LE_BYTES,
			    hw->st_le, hw->st_dma);
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
	struct sky2_hw *hw  = pci_get_drvdata(pdev);
	int i, wol = 0;

	for (i = 0; i < 2; i++) {
		struct net_device *dev = hw->dev[i];

		if (dev) {
			struct sky2_port *sky2 = netdev_priv(dev);
			if (netif_running(dev)) {
				netif_carrier_off(dev);
				sky2_down(dev);
			}
			netif_device_detach(dev);
			wol |= sky2->wol;
		}
	}

	pci_save_state(pdev);
	pci_enable_wake(pdev, pci_choose_state(pdev, state), wol);
	pci_disable_device(pdev);
	pci_set_power_state(pdev, pci_choose_state(pdev, state));

	return 0;
}

static int sky2_resume(struct pci_dev *pdev)
{
	struct sky2_hw *hw  = pci_get_drvdata(pdev);
	int i;

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);
	pci_enable_wake(pdev, PCI_D0, 0);

	sky2_reset(hw);

	for (i = 0; i < 2; i++) {
		struct net_device *dev = hw->dev[i];
		if (dev) {
			netif_device_attach(dev);
			if (netif_running(dev))
				sky2_up(dev);
		}
	}
	return 0;
}
#endif

static struct pci_driver sky2_driver = {
	.name =         DRV_NAME,
	.id_table =     sky2_id_table,
	.probe =        sky2_probe,
	.remove =       __devexit_p(sky2_remove),
#ifdef CONFIG_PM
	.suspend =	sky2_suspend,
	.resume = 	sky2_resume,
#endif
};

static int __init sky2_init_module(void)
{

	return pci_module_init(&sky2_driver);
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
