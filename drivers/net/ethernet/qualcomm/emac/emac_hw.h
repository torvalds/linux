/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2013-2017, The Linux Foundation. All rights reserved.
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

#ifndef _EMAC_HW_H_
#define _EMAC_HW_H_

#include <linux/mii.h>

#include "emac_main.h"
#include "emac_regs.h"
#include "emac_defines.h"

/* function prototype */

/* REG */
u32 emac_reg_r32(struct emac_hw *hw, u8 base, u32 reg);
void emac_reg_w32(struct emac_hw *hw, u8 base, u32 reg, u32 val);
void emac_reg_update32(struct emac_hw *hw, u8 base, u32 reg,
		       u32 mask, u32 val);
u32 emac_reg_field_r32(struct emac_hw *hw, u8 base, u32 reg,
		       u32 mask, u32 shift);
void emac_hw_config_pow_save(struct emac_hw *hw, u32 speed, bool wol_en,
			     bool rx_en);
/* MAC */
void emac_hw_enable_intr(struct emac_hw *hw);
void emac_hw_disable_intr(struct emac_hw *hw);
void emac_hw_set_mc_addr(struct emac_hw *hw, u8 *addr);
void emac_hw_clear_mc_addr(struct emac_hw *hw);

void emac_hw_config_mac_ctrl(struct emac_hw *hw);
void emac_hw_config_rss(struct emac_hw *hw);
void emac_hw_config_wol(struct emac_hw *hw, u32 wufc);
int emac_hw_config_fc(struct emac_hw *hw);

void emac_hw_reset_mac(struct emac_hw *hw);
void emac_hw_config_mac(struct emac_hw *hw);
void emac_hw_start_mac(struct emac_hw *hw);
void emac_hw_stop_mac(struct emac_hw *hw);

void emac_hw_set_mac_addr(struct emac_hw *hw, u8 *addr);

/* TX Timestamp */
bool emac_hw_read_tx_tstamp(struct emac_hw *hw, struct emac_hwtxtstamp *ts);

#define IMR_NORMAL_MASK		(ISR_ERROR | ISR_OVER | ISR_TX_PKT)

#define IMR_EXTENDED_MASK       (\
		SW_MAN_INT      |\
		ISR_OVER        |\
		ISR_ERROR       |\
		ISR_TX_PKT)

#define ISR_RX_PKT      (\
	RX_PKT_INT0     |\
	RX_PKT_INT1     |\
	RX_PKT_INT2     |\
	RX_PKT_INT3)

#define ISR_TX_PKT      (\
	TX_PKT_INT      |\
	TX_PKT_INT1     |\
	TX_PKT_INT2     |\
	TX_PKT_INT3)

#define ISR_GPHY_LINK        (\
	GPHY_LINK_UP_INT     |\
	GPHY_LINK_DOWN_INT)

#define ISR_OVER        (\
	RFD0_UR_INT     |\
	RFD1_UR_INT     |\
	RFD2_UR_INT     |\
	RFD3_UR_INT     |\
	RFD4_UR_INT     |\
	RXF_OF_INT      |\
	TXF_UR_INT)

#define ISR_ERROR       (\
	DMAR_TO_INT     |\
	DMAW_TO_INT     |\
	TXQ_TO_INT)

#define REG_MAC_RX_STATUS_BIN           EMAC_RXMAC_STATC_REG0
#define REG_MAC_RX_STATUS_END           EMAC_RXMAC_STATC_REG22
#define REG_MAC_TX_STATUS_BIN           EMAC_TXMAC_STATC_REG0
#define REG_MAC_TX_STATUS_END           EMAC_TXMAC_STATC_REG24

#define RXQ0_NUM_RFD_PREF_DEF           8
#define TXQ0_NUM_TPD_PREF_DEF           5

#define EMAC_PREAMBLE_DEF               7

#define DMAR_DLY_CNT_DEF                15
#define DMAW_DLY_CNT_DEF                4

#define MDIO_CLK_25_4                   0

#define RXQ0_RSS_HSTYP_IPV6_TCP_EN      0x20
#define RXQ0_RSS_HSTYP_IPV6_EN          0x10
#define RXQ0_RSS_HSTYP_IPV4_TCP_EN      0x8
#define RXQ0_RSS_HSTYP_IPV4_EN          0x4

#define MASTER_CTRL_CLK_SEL_DIS         0x1000

#define MDIO_WAIT_TIMES                 1000

/* PHY */
#define MII_PSSR                        0x11 /* PHY Specific Status Reg */
#define MII_DBG_ADDR                    0x1D /* PHY Debug Address Reg */
#define MII_DBG_DATA                    0x1E /* PHY Debug Data Reg */
#define MII_INT_ENABLE			0x12 /* PHY Interrupt Enable Reg */
#define MII_INT_STATUS			0x13 /* PHY Interrupt Status Reg */

/* MII_BMCR (0x00) */
#define BMCR_SPEED10                    0x0000

/* MII_PSSR (0x11) */
#define PSSR_FC_RXEN                    0x0004
#define PSSR_FC_TXEN                    0x0008
#define PSSR_SPD_DPLX_RESOLVED          0x0800  /* 1=Speed & Duplex resolved */
#define PSSR_DPLX                       0x2000  /* 1=Duplex 0=Half Duplex */
#define PSSR_SPEED                      0xC000  /* Speed, bits 14:15 */
#define PSSR_10MBS                      0x0000  /* 00=10Mbs */
#define PSSR_100MBS                     0x4000  /* 01=100Mbs */
#define PSSR_1000MBS                    0x8000  /* 10=1000Mbs */

/* MII DBG registers */
#define HIBERNATE_CTRL_REG              0xB

/* HIBERNATE_CTRL_REG */
#define HIBERNATE_EN                    0x8000

/* MII_INT_ENABLE/MII_INT_STATUS */
#define LINK_SUCCESS_INTERRUPT			BIT(10)
#define LINK_SUCCESS_BX			BIT(7)
#define WOL_INT				BIT(0)
#endif /*_EMAC_HW_H_*/
