// SPDX-License-Identifier: GPL-2.0-or-later
/* Copyright (c) 2013-2017, The Linux Foundation. All rights reserved.
 *
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/* Qualcomm Technologies, Inc. EMAC Ethernet Controller Hardware support
 */

#include <linux/crc32.h>
#include <linux/if_vlan.h>
#include <linux/jiffies.h>
#include <linux/phy.h>
#include <linux/of.h>
#include <linux/kernel.h>

#include <linux/qca8337.h>
#include "emac_hw.h"
#include "emac_ptp.h"

#define RFD_PREF_LOW_TH     0x10
#define RFD_PREF_UP_TH      0x10
#define JUMBO_1KAH          0x4

#define RXF_DOF_TH          0x0be
#define RXF_UOF_TH          0x1a0

#define RXD_TH              0x100

/* RGMII specific macros */
#define EMAC_RGMII_PLL_LOCK_TIMEOUT     (HZ / 1000) /* 1ms */
#define EMAC_RGMII_CORE_IE_C            0x2001
#define EMAC_RGMII_PLL_L_VAL            0x14
#define EMAC_RGMII_PHY_MODE             0

/* REG */
u32 emac_reg_r32(struct emac_hw *hw, u8 base, u32 reg)
{
	return readl_relaxed(hw->reg_addr[base] + reg);
}

void emac_reg_w32(struct emac_hw *hw, u8 base, u32 reg, u32 val)
{
	writel_relaxed(val, hw->reg_addr[base] + reg);
}

void emac_reg_update32(struct emac_hw *hw, u8 base, u32 reg, u32 mask, u32 val)
{
	u32 data;

	data = emac_reg_r32(hw, base, reg);
	emac_reg_w32(hw, base, reg, ((data & ~mask) | val));
}

u32 emac_reg_field_r32(struct emac_hw *hw, u8 base, u32 reg,
		       u32 mask, u32 shift)
{
	u32 data;

	data = emac_reg_r32(hw, base, reg);
	return (data & mask) >> shift;
}

/* INTR */
void emac_hw_enable_intr(struct emac_hw *hw)
{
	struct emac_adapter *adpt = emac_hw_get_adap(hw);
	int i;

	for (i = 0; i < EMAC_NUM_CORE_IRQ; i++) {
		struct emac_irq_per_dev *irq = &adpt->irq[i];
		const struct emac_irq_common *irq_cmn = &emac_irq_cmn_tbl[i];

		emac_reg_w32(hw, EMAC, irq_cmn->status_reg, (u32)~DIS_INT);
		emac_reg_w32(hw, EMAC, irq_cmn->mask_reg, irq->mask);
	}

	if (adpt->tstamp_en)
		emac_reg_w32(hw, EMAC_1588, EMAC_P1588_PTP_EXPANDED_INT_MASK,
			     hw->ptp_intr_mask);
	wmb(); /* ensure that irq and ptp setting are flushed to HW */
}

void emac_hw_disable_intr(struct emac_hw *hw)
{
	struct emac_adapter *adpt = emac_hw_get_adap(hw);
	int i;

	for (i = 0; i < EMAC_NUM_CORE_IRQ; i++) {
		const struct emac_irq_common *irq_cmn = &emac_irq_cmn_tbl[i];

		emac_reg_w32(hw, EMAC, irq_cmn->status_reg, DIS_INT);
		emac_reg_w32(hw, EMAC, irq_cmn->mask_reg, 0);
	}

	if (adpt->tstamp_en)
		emac_reg_w32(hw, EMAC_1588, EMAC_P1588_PTP_EXPANDED_INT_MASK,
			     0);

	wmb(); /* ensure that irq and ptp setting are flushed to HW */
}

/* MC */
void emac_hw_set_mc_addr(struct emac_hw *hw, u8 *addr)
{
	u32 crc32, bit, reg, mta;

	/* Calculate the CRC of the MAC address */
	crc32 = ether_crc(ETH_ALEN, addr);

	/* The HASH Table is an array of 2 32-bit registers. It is
	 * treated like an array of 64 bits (BitArray[hash_value]).
	 * Use the upper 6 bits of the above CRC as the hash value.
	 */
	reg = (crc32 >> 31) & 0x1;
	bit = (crc32 >> 26) & 0x1F;

	mta = emac_reg_r32(hw, EMAC, EMAC_HASH_TAB_REG0 + (reg << 2));
	mta |= (0x1 << bit);
	emac_reg_w32(hw, EMAC, EMAC_HASH_TAB_REG0 + (reg << 2), mta);
	wmb(); /* ensure that the mac address is flushed to HW */
}

void emac_hw_clear_mc_addr(struct emac_hw *hw)
{
	emac_reg_w32(hw, EMAC, EMAC_HASH_TAB_REG0, 0);
	emac_reg_w32(hw, EMAC, EMAC_HASH_TAB_REG1, 0);
	wmb(); /* ensure that clearing the mac address is flushed to HW */
}

/* definitions for RSS */
#define EMAC_RSS_KEY(_i, _type) \
		(EMAC_RSS_KEY0 + ((_i) * sizeof(_type)))
#define EMAC_RSS_TBL(_i, _type) \
		(EMAC_IDT_TABLE0 + ((_i) * sizeof(_type)))

/* RSS */
void emac_hw_config_rss(struct emac_hw *hw)
{
	int key_len_by_u32 = ARRAY_SIZE(hw->rss_key);
	int idt_len_by_u32 = ARRAY_SIZE(hw->rss_idt);
	u32 rxq0;
	int i;

	/* Fill out hash function keys */
	for (i = 0; i < key_len_by_u32 - 3; i++) {
		u32 key, idx_base;

		idx_base = (key_len_by_u32 - i);
		key = ((hw->rss_key[idx_base - 1])       |
		       (hw->rss_key[idx_base - 2] << 8)  |
		       (hw->rss_key[idx_base - 3] << 16) |
		       (hw->rss_key[idx_base - 4] << 24));
		emac_reg_w32(hw, EMAC, EMAC_RSS_KEY(i, u32), key);
	}

	/* Fill out redirection table */
	for (i = 0; i < idt_len_by_u32; i++)
		emac_reg_w32(hw, EMAC, EMAC_RSS_TBL(i, u32), hw->rss_idt[i]);

	emac_reg_w32(hw, EMAC, EMAC_BASE_CPU_NUMBER, hw->rss_base_cpu);

	rxq0 = emac_reg_r32(hw, EMAC, EMAC_RXQ_CTRL_0);
	if (hw->rss_hstype & EMAC_RSS_HSTYP_IPV4_EN)
		rxq0 |= RXQ0_RSS_HSTYP_IPV4_EN;
	else
		rxq0 &= ~RXQ0_RSS_HSTYP_IPV4_EN;

	if (hw->rss_hstype & EMAC_RSS_HSTYP_TCP4_EN)
		rxq0 |= RXQ0_RSS_HSTYP_IPV4_TCP_EN;
	else
		rxq0 &= ~RXQ0_RSS_HSTYP_IPV4_TCP_EN;

	if (hw->rss_hstype & EMAC_RSS_HSTYP_IPV6_EN)
		rxq0 |= RXQ0_RSS_HSTYP_IPV6_EN;
	else
		rxq0 &= ~RXQ0_RSS_HSTYP_IPV6_EN;

	if (hw->rss_hstype & EMAC_RSS_HSTYP_TCP6_EN)
		rxq0 |= RXQ0_RSS_HSTYP_IPV6_TCP_EN;
	else
		rxq0 &= ~RXQ0_RSS_HSTYP_IPV6_TCP_EN;

	rxq0 |= ((hw->rss_idt_size << IDT_TABLE_SIZE_SHFT) &
		IDT_TABLE_SIZE_BMSK);
	rxq0 |= RSS_HASH_EN;

	wmb(); /* ensure all parameters are written before we enable RSS */
	emac_reg_w32(hw, EMAC, EMAC_RXQ_CTRL_0, rxq0);
	wmb(); /* ensure that enabling RSS is flushed to HW */
}

/* Config MAC modes */
void emac_hw_config_mac_ctrl(struct emac_hw *hw)
{
	u32 mac;

	mac = emac_reg_r32(hw, EMAC, EMAC_MAC_CTRL);

	if (TEST_FLAG(hw, HW_VLANSTRIP_EN))
		mac |= VLAN_STRIP;
	else
		mac &= ~VLAN_STRIP;

	if (TEST_FLAG(hw, HW_PROMISC_EN))
		mac |= PROM_MODE;
	else
		mac &= ~PROM_MODE;

	if (TEST_FLAG(hw, HW_MULTIALL_EN))
		mac |= MULTI_ALL;
	else
		mac &= ~MULTI_ALL;

	if (TEST_FLAG(hw, HW_LOOPBACK_EN))
		mac |= MAC_LP_EN;
	else
		mac &= ~MAC_LP_EN;

	emac_reg_w32(hw, EMAC, EMAC_MAC_CTRL, mac);
	wmb(); /* ensure MAC setting is flushed to HW */
}

/* Wake On LAN (WOL) */
void emac_hw_config_wol(struct emac_hw *hw, u32 wufc)
{
	u32 wol = 0;

	/* turn on magic packet event */
	if (wufc & EMAC_WOL_MAGIC)
		wol |= MG_FRAME_EN | MG_FRAME_PME | WK_FRAME_EN;

	/* turn on link up event */
	if (wufc & EMAC_WOL_PHY)
		wol |=  LK_CHG_EN | LK_CHG_PME;

	emac_reg_w32(hw, EMAC, EMAC_WOL_CTRL0, wol);
	wmb(); /* ensure that WOL setting is flushed to HW */
}

/* Power Management */
void emac_hw_config_pow_save(struct emac_hw *hw, u32 speed,
			     bool wol_en, bool rx_en)
{
	struct emac_adapter *adpt = emac_hw_get_adap(hw);
	struct phy_device *phydev = adpt->phydev;
	u32 dma_mas, mac;

	dma_mas = emac_reg_r32(hw, EMAC, EMAC_DMA_MAS_CTRL);
	dma_mas &= ~LPW_CLK_SEL;
	dma_mas |= LPW_STATE;

	mac = emac_reg_r32(hw, EMAC, EMAC_MAC_CTRL);
	mac &= ~(FULLD | RXEN | TXEN);
	mac = (mac & ~SPEED_MASK) | SPEED(1);

	if (wol_en) {
		if (rx_en)
			mac |= (RXEN | BROAD_EN);

		/* If WOL is enabled, set link speed/duplex for mac */
		if (phydev->speed == SPEED_1000)
			mac = (mac & ~SPEED_MASK) | (SPEED(2) & SPEED_MASK);

		if (phydev->duplex == DUPLEX_FULL)
			if (phydev->speed == SPEED_10 ||
			    phydev->speed == SPEED_100 ||
			    phydev->speed == SPEED_1000)
				mac |= FULLD;
	} else {
		/* select lower clock speed if WOL is disabled */
		dma_mas |= LPW_CLK_SEL;
	}

	emac_reg_w32(hw, EMAC, EMAC_DMA_MAS_CTRL, dma_mas);
	emac_reg_w32(hw, EMAC, EMAC_MAC_CTRL, mac);
	wmb(); /* ensure that power setting is flushed to HW */
}

/* Config descriptor rings */
static void emac_mac_dma_rings_config(struct emac_hw *hw)
{
	struct emac_adapter *adpt = emac_hw_get_adap(hw);

	if (adpt->tstamp_en) {
		emac_reg_update32(hw, EMAC_CSR, EMAC_EMAC_WRAPPER_CSR1,
				  0, ENABLE_RRD_TIMESTAMP);
	}

	/* TPD */
	emac_reg_w32(hw, EMAC, EMAC_DESC_CTRL_1,
		     EMAC_DMA_ADDR_HI(adpt->tx_queue[0].tpd.tpdma));
	switch (adpt->num_txques) {
	case 4:
		emac_reg_w32(hw, EMAC, EMAC_H3TPD_BASE_ADDR_LO,
			     EMAC_DMA_ADDR_LO(adpt->tx_queue[3].tpd.tpdma));
		fallthrough;
	case 3:
		emac_reg_w32(hw, EMAC, EMAC_H2TPD_BASE_ADDR_LO,
			     EMAC_DMA_ADDR_LO(adpt->tx_queue[2].tpd.tpdma));
		fallthrough;
	case 2:
		emac_reg_w32(hw, EMAC, EMAC_H1TPD_BASE_ADDR_LO,
			     EMAC_DMA_ADDR_LO(adpt->tx_queue[1].tpd.tpdma));
		fallthrough;
	case 1:
		emac_reg_w32(hw, EMAC, EMAC_DESC_CTRL_8,
			     EMAC_DMA_ADDR_LO(adpt->tx_queue[0].tpd.tpdma));
		break;
	default:
		emac_err(adpt, "Invalid number of TX queues (%d)\n",
			 adpt->num_txques);
		return;
	}
	emac_reg_w32(hw, EMAC, EMAC_DESC_CTRL_9,
		     adpt->tx_queue[0].tpd.count & TPD_RING_SIZE_BMSK);

	/* RFD & RRD */
	emac_reg_w32(hw, EMAC, EMAC_DESC_CTRL_0,
		     EMAC_DMA_ADDR_HI(adpt->rx_queue[0].rfd.rfdma));
	switch (adpt->num_rxques) {
	case 4:
		emac_reg_w32(hw, EMAC, EMAC_DESC_CTRL_13,
			     EMAC_DMA_ADDR_LO(adpt->rx_queue[3].rfd.rfdma));
		emac_reg_w32(hw, EMAC, EMAC_DESC_CTRL_16,
			     EMAC_DMA_ADDR_LO(adpt->rx_queue[3].rrd.rrdma));
		fallthrough;
	case 3:
		emac_reg_w32(hw, EMAC, EMAC_DESC_CTRL_12,
			     EMAC_DMA_ADDR_LO(adpt->rx_queue[2].rfd.rfdma));
		emac_reg_w32(hw, EMAC, EMAC_DESC_CTRL_15,
			     EMAC_DMA_ADDR_LO(adpt->rx_queue[2].rrd.rrdma));
		fallthrough;
	case 2:
		emac_reg_w32(hw, EMAC, EMAC_DESC_CTRL_10,
			     EMAC_DMA_ADDR_LO(adpt->rx_queue[1].rfd.rfdma));
		emac_reg_w32(hw, EMAC, EMAC_DESC_CTRL_14,
			     EMAC_DMA_ADDR_LO(adpt->rx_queue[1].rrd.rrdma));
		fallthrough;
	case 1:
		emac_reg_w32(hw, EMAC, EMAC_DESC_CTRL_2,
			     EMAC_DMA_ADDR_LO(adpt->rx_queue[0].rfd.rfdma));
		emac_reg_w32(hw, EMAC, EMAC_DESC_CTRL_5,
			     EMAC_DMA_ADDR_LO(adpt->rx_queue[0].rrd.rrdma));
		break;
	default:
		emac_err(adpt, "Invalid number of RX queues (%d)\n",
			 adpt->num_rxques);
		return;
	}
	emac_reg_w32(hw, EMAC, EMAC_DESC_CTRL_3,
		     adpt->rx_queue[0].rfd.count & RFD_RING_SIZE_BMSK);
	emac_reg_w32(hw, EMAC, EMAC_DESC_CTRL_6,
		     adpt->rx_queue[0].rrd.count & RRD_RING_SIZE_BMSK);
	emac_reg_w32(hw, EMAC, EMAC_DESC_CTRL_4,
		     adpt->rxbuf_size & RX_BUFFER_SIZE_BMSK);

	emac_reg_w32(hw, EMAC, EMAC_DESC_CTRL_11, 0);

	wmb(); /* ensure all parameters are written before we enable them */
	/* Load all of base address above */
	emac_reg_w32(hw, EMAC, EMAC_INTER_SRAM_PART9, 1);
	wmb(); /* ensure triggering HW to read ring pointers is flushed */
}

/* Config transmit parameters */
static void emac_hw_config_tx_ctrl(struct emac_hw *hw)
{
	u16 tx_offload_thresh = EMAC_MAX_TX_OFFLOAD_THRESH;
	u32 val;

	emac_reg_w32(hw, EMAC, EMAC_TXQ_CTRL_1,
		     (tx_offload_thresh >> 3) &
		     JUMBO_TASK_OFFLOAD_THRESHOLD_BMSK);

	val = (hw->tpd_burst << NUM_TPD_BURST_PREF_SHFT) &
		NUM_TPD_BURST_PREF_BMSK;

	val |= (TXQ_MODE | LS_8023_SP);
	val |= (0x0100 << NUM_TXF_BURST_PREF_SHFT) &
		NUM_TXF_BURST_PREF_BMSK;

	emac_reg_w32(hw, EMAC, EMAC_TXQ_CTRL_0, val);
	emac_reg_update32(hw, EMAC, EMAC_TXQ_CTRL_2,
			  (TXF_HWM_BMSK | TXF_LWM_BMSK), 0);
	wmb(); /* ensure that Tx control settings are flushed to HW */
}

/* Config receive parameters */
static void emac_hw_config_rx_ctrl(struct emac_hw *hw)
{
	u32 val;

	val = ((hw->rfd_burst << NUM_RFD_BURST_PREF_SHFT) &
	       NUM_RFD_BURST_PREF_BMSK);
	val |= (SP_IPV6 | CUT_THRU_EN);

	emac_reg_w32(hw, EMAC, EMAC_RXQ_CTRL_0, val);

	val = emac_reg_r32(hw, EMAC, EMAC_RXQ_CTRL_1);
	val &= ~(JUMBO_1KAH_BMSK | RFD_PREF_LOW_THRESHOLD_BMSK |
		 RFD_PREF_UP_THRESHOLD_BMSK);
	val |= (JUMBO_1KAH << JUMBO_1KAH_SHFT) |
		(RFD_PREF_LOW_TH << RFD_PREF_LOW_THRESHOLD_SHFT) |
		(RFD_PREF_UP_TH << RFD_PREF_UP_THRESHOLD_SHFT);
	emac_reg_w32(hw, EMAC, EMAC_RXQ_CTRL_1, val);

	val = emac_reg_r32(hw, EMAC, EMAC_RXQ_CTRL_2);
	val &= ~(RXF_DOF_THRESHOLD_BMSK | RXF_UOF_THRESHOLD_BMSK);
	val |= (RXF_DOF_TH << RXF_DOF_THRESHOLD_SHFT) |
		(RXF_UOF_TH << RXF_UOF_THRESHOLD_SHFT);
	emac_reg_w32(hw, EMAC, EMAC_RXQ_CTRL_2, val);

	val = emac_reg_r32(hw, EMAC, EMAC_RXQ_CTRL_3);
	val &= ~(RXD_TIMER_BMSK | RXD_THRESHOLD_BMSK);
	val |= RXD_TH << RXD_THRESHOLD_SHFT;
	emac_reg_w32(hw, EMAC, EMAC_RXQ_CTRL_3, val);
	wmb(); /* ensure that Rx control settings are flushed to HW */
}

/* Config dma */
static void emac_hw_config_dma_ctrl(struct emac_hw *hw)
{
	u32 dma_ctrl;

	dma_ctrl = DMAR_REQ_PRI;

	switch (hw->dma_order) {
	case emac_dma_ord_in:
		dma_ctrl |= IN_ORDER_MODE;
		break;
	case emac_dma_ord_enh:
		dma_ctrl |= ENH_ORDER_MODE;
		break;
	case emac_dma_ord_out:
		dma_ctrl |= OUT_ORDER_MODE;
		break;
	default:
		break;
	}

	dma_ctrl |= (((u32)hw->dmar_block) << REGRDBLEN_SHFT) &
						REGRDBLEN_BMSK;
	dma_ctrl |= (((u32)hw->dmaw_block) << REGWRBLEN_SHFT) &
						REGWRBLEN_BMSK;
	dma_ctrl |= (((u32)hw->dmar_dly_cnt) << DMAR_DLY_CNT_SHFT) &
						DMAR_DLY_CNT_BMSK;
	dma_ctrl |= (((u32)hw->dmaw_dly_cnt) << DMAW_DLY_CNT_SHFT) &
						DMAW_DLY_CNT_BMSK;

	emac_reg_w32(hw, EMAC, EMAC_DMA_CTRL, dma_ctrl);
	wmb(); /* ensure that the DMA configuration is flushed to HW */
}

/* Configure MAC */
void emac_hw_config_mac(struct emac_hw *hw)
{
	struct emac_adapter *adpt = emac_hw_get_adap(hw);
	u32 val;

	emac_hw_set_mac_addr(hw, (u8 *)adpt->netdev->dev_addr);

	emac_mac_dma_rings_config(hw);

	emac_reg_w32(hw, EMAC, EMAC_MAX_FRAM_LEN_CTRL,
		     adpt->netdev->mtu + ETH_HLEN + VLAN_HLEN + ETH_FCS_LEN);

	emac_hw_config_tx_ctrl(hw);
	emac_hw_config_rx_ctrl(hw);
	emac_hw_config_dma_ctrl(hw);

	if (TEST_FLAG(hw, HW_PTP_CAP))
		emac_ptp_config(hw);

	val = emac_reg_r32(hw, EMAC, EMAC_AXI_MAST_CTRL);
	val &= ~(DATA_BYTE_SWAP | MAX_BOUND);
	val |= MAX_BTYPE;
	emac_reg_w32(hw, EMAC, EMAC_AXI_MAST_CTRL, val);

	emac_reg_w32(hw, EMAC, EMAC_CLK_GATE_CTRL, 0);
	emac_reg_w32(hw, EMAC, EMAC_MISC_CTRL, RX_UNCPL_INT_EN);
	wmb(); /* ensure that the MAC configuration is flushed to HW */
}

/* Reset MAC */
void emac_hw_reset_mac(struct emac_hw *hw)
{
	emac_reg_w32(hw, EMAC, EMAC_INT_MASK, 0);
	emac_reg_w32(hw, EMAC, EMAC_INT_STATUS, DIS_INT);

	emac_hw_stop_mac(hw);

	emac_reg_update32(hw, EMAC, EMAC_DMA_MAS_CTRL, 0, SOFT_RST);
	wmb(); /* ensure mac is fully reset */
	usleep_range(100, 150);

	/* interrupt clear-on-read */
	emac_reg_update32(hw, EMAC, EMAC_DMA_MAS_CTRL, 0, INT_RD_CLR_EN);
	wmb(); /* ensure the interrupt clear-on-read setting is flushed to HW */
}

/* Start MAC */
void emac_hw_start_mac(struct emac_hw *hw)
{
	struct emac_adapter *adpt = emac_hw_get_adap(hw);
	struct phy_device *phydev = adpt->phydev;
	u32 mac, csr1;

	/* enable tx queue */
	if (adpt->num_txques && adpt->num_txques <= EMAC_MAX_TX_QUEUES)
		emac_reg_update32(hw, EMAC, EMAC_TXQ_CTRL_0, 0, TXQ_EN);

	/* enable rx queue */
	if (adpt->num_rxques && adpt->num_rxques <= EMAC_MAX_RX_QUEUES)
		emac_reg_update32(hw, EMAC, EMAC_RXQ_CTRL_0, 0, RXQ_EN);

	/* enable mac control */
	mac = emac_reg_r32(hw, EMAC, EMAC_MAC_CTRL);
	csr1 = emac_reg_r32(hw, EMAC_CSR, EMAC_EMAC_WRAPPER_CSR1);

	mac |= TXEN | RXEN;     /* enable RX/TX */

	/* Configure MAC flow control to match the PHY's settings. */
	if (phydev->pause)
		mac |= RXFC;
	if (phydev->pause != phydev->asym_pause)
		mac |= TXFC;

	/* setup link speed */
	mac &= ~SPEED_MASK;

	if (phydev->phy_id == QCA8337_PHY_ID) {
		mac |= SPEED(2);
		csr1 |= FREQ_MODE;
		mac |= FULLD;
	} else {
		switch (phydev->speed) {
		case SPEED_1000:
			mac |= SPEED(2);
			csr1 |= FREQ_MODE;
			break;
		default:
			mac |= SPEED(1);
			csr1 &= ~FREQ_MODE;
			break;
		}

		if (phydev->duplex == DUPLEX_FULL)
			mac |= FULLD;
		else
			mac &= ~FULLD;
	}

	/* other parameters */
	mac |= (CRCE | PCRCE);
	mac |= ((hw->preamble << PRLEN_SHFT) & PRLEN_BMSK);
	mac |= BROAD_EN;
	mac |= FLCHK;
	mac &= ~RX_CHKSUM_EN;
	mac &= ~(HUGEN | VLAN_STRIP | TPAUSE | SIMR | HUGE | MULTI_ALL |
		 DEBUG_MODE | SINGLE_PAUSE_MODE);

	emac_reg_w32(hw, EMAC_CSR, EMAC_EMAC_WRAPPER_CSR1, csr1);
	emac_reg_w32(hw, EMAC, EMAC_MAC_CTRL, mac);

	/* enable interrupt read clear, low power sleep mode and
	 * the irq moderators
	 */
	emac_reg_w32(hw, EMAC, EMAC_IRQ_MOD_TIM_INIT, hw->irq_mod);
	emac_reg_w32(hw, EMAC, EMAC_DMA_MAS_CTRL,
		     (INT_RD_CLR_EN | LPW_MODE |
		      IRQ_MODERATOR_EN | IRQ_MODERATOR2_EN));

	if (TEST_FLAG(hw, HW_PTP_CAP))
		emac_ptp_set_linkspeed(hw, phydev->speed);

	emac_hw_config_mac_ctrl(hw);

	emac_reg_update32(hw, EMAC, EMAC_ATHR_HEADER_CTRL,
			  (HEADER_ENABLE | HEADER_CNT_EN), 0);

	emac_reg_update32(hw, EMAC_CSR, EMAC_EMAC_WRAPPER_CSR2, 0, WOL_EN);
	wmb(); /* ensure that MAC setting are flushed to HW */
}

/* Stop MAC */
void emac_hw_stop_mac(struct emac_hw *hw)
{
	emac_reg_update32(hw, EMAC, EMAC_RXQ_CTRL_0, RXQ_EN, 0);
	emac_reg_update32(hw, EMAC, EMAC_TXQ_CTRL_0, TXQ_EN, 0);
	emac_reg_update32(hw, EMAC, EMAC_MAC_CTRL, (TXEN | RXEN), 0);
	wmb(); /* ensure mac is stopped before we proceed */
	usleep_range(1000, 1050);
}

/* set MAC address */
void emac_hw_set_mac_addr(struct emac_hw *hw, u8 *addr)
{
	u32 sta;

	/* for example: 00-A0-C6-11-22-33
	 * 0<-->C6112233, 1<-->00A0.
	 */

	/* low 32bit word */
	sta = (((u32)addr[2]) << 24) | (((u32)addr[3]) << 16) |
	      (((u32)addr[4]) << 8)  | (((u32)addr[5]));
	emac_reg_w32(hw, EMAC, EMAC_MAC_STA_ADDR0, sta);

	/* hight 32bit word */
	sta = (((u32)addr[0]) << 8) | (((u32)addr[1]));
	emac_reg_w32(hw, EMAC, EMAC_MAC_STA_ADDR1, sta);
	wmb(); /* ensure that the MAC address is flushed to HW */
}

/* Read one entry from the HW tx timestamp FIFO */
bool emac_hw_read_tx_tstamp(struct emac_hw *hw, struct emac_hwtxtstamp *ts)
{
	u32 ts_idx;

	ts_idx = emac_reg_r32(hw, EMAC_CSR, EMAC_EMAC_WRAPPER_TX_TS_INX);

	if (ts_idx & EMAC_WRAPPER_TX_TS_EMPTY)
		return false;

	ts->ns = emac_reg_r32(hw, EMAC_CSR, EMAC_EMAC_WRAPPER_TX_TS_LO);
	ts->sec = emac_reg_r32(hw, EMAC_CSR, EMAC_EMAC_WRAPPER_TX_TS_HI);
	ts->ts_idx = ts_idx & EMAC_WRAPPER_TX_TS_INX_BMSK;

	return true;
}
