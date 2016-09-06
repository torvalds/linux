/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *******************************************************************************/
#ifndef __RTL8723B_SPEC_H__
#define __RTL8723B_SPEC_H__

#include <drv_conf.h>


#define HAL_NAV_UPPER_UNIT_8723B		128		// micro-second

//-----------------------------------------------------
//
//	0x0000h ~ 0x00FFh	System Configuration
//
//-----------------------------------------------------
#define REG_BT_WIFI_ANTENNA_SWITCH_8723B	0x0038
#define REG_PAD_CTRL1_8723B		0x0064
#define REG_AFE_CTRL_4_8723B		0x0078
#define REG_HMEBOX_DBG_0_8723B	0x0088
#define REG_HMEBOX_DBG_1_8723B	0x008A
#define REG_HMEBOX_DBG_2_8723B	0x008C
#define REG_HMEBOX_DBG_3_8723B	0x008E

//-----------------------------------------------------
//
//	0x0100h ~ 0x01FFh	MACTOP General Configuration
//
//-----------------------------------------------------
#define REG_C2HEVT_CMD_ID_8723B	0x01A0
#define REG_C2HEVT_CMD_LEN_8723B	0x01AE
#define REG_WOWLAN_WAKE_REASON_8723B 0x01C7

#define REG_HMEBOX_EXT0_8723B			0x01F0
#define REG_HMEBOX_EXT1_8723B			0x01F4
#define REG_HMEBOX_EXT2_8723B			0x01F8
#define REG_HMEBOX_EXT3_8723B			0x01FC

//-----------------------------------------------------
//
//	0x0200h ~ 0x027Fh	TXDMA Configuration
//
//-----------------------------------------------------

//-----------------------------------------------------
//
//	0x0280h ~ 0x02FFh	RXDMA Configuration
//
//-----------------------------------------------------
#define REG_RXDMA_MODE_CTRL_8723B		0x0290

//-----------------------------------------------------
//
//	0x0300h ~ 0x03FFh	PCIe
//
//-----------------------------------------------------

//-----------------------------------------------------
//
//	0x0400h ~ 0x047Fh	Protocol Configuration
//
//-----------------------------------------------------
#define REG_TXPKTBUF_BCNQ_BDNY_8723B	0x0424
#define REG_TXPKTBUF_MGQ_BDNY_8723B	0x0425
#define REG_TXPKTBUF_WMAC_LBK_BF_HD_8723B	0x045D

//-----------------------------------------------------
//
//	0x0500h ~ 0x05FFh	EDCA Configuration
//
//-----------------------------------------------------
#define REG_SECONDARY_CCA_CTRL_8723B	0x0577

//-----------------------------------------------------
//
//	0x0600h ~ 0x07FFh	WMAC Configuration
//
//-----------------------------------------------------

//============================================================================
//	8723 Regsiter Bit and Content definition
//============================================================================

//2 HSISR
// interrupt mask which needs to clear
#define MASK_HSISR_CLEAR		(HSISR_GPIO12_0_INT |\
								HSISR_SPS_OCP_INT |\
								HSISR_RON_INT |\
								HSISR_PDNINT |\
								HSISR_GPIO9_INT)

//-----------------------------------------------------
//
//	0x0100h ~ 0x01FFh	MACTOP General Configuration
//
//-----------------------------------------------------

#define RXDMA_AGG_MODE_EN		BIT(1)
//-----------------------------------------------------
//
//	0x0200h ~ 0x027Fh	TXDMA Configuration
//
//-----------------------------------------------------

//-----------------------------------------------------
//
//	0x0280h ~ 0x02FFh	RXDMA Configuration
//
//-----------------------------------------------------
#ifdef CONFIG_WOWLAN_8723
#define RXPKT_RELEASE_POLL		BIT(16)
#define RXDMA_IDLE				BIT(17)
#define RW_RELEASE_EN			BIT(18)
#endif

//-----------------------------------------------------
//
//	0x0400h ~ 0x047Fh	Protocol Configuration
//
//-----------------------------------------------------

//-----------------------------------------------------
//
//	0x0500h ~ 0x05FFh	EDCA Configuration
//
//-----------------------------------------------------

//-----------------------------------------------------
//
//	0x0600h ~ 0x07FFh	WMAC Configuration
//
//-----------------------------------------------------

#endif

