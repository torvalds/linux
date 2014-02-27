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
#define REG_WOWLAN_WAKE_REASON 0x01C7
#define REG_WOWLAN_GTK_DBG1	0x630
#define REG_WOWLAN_GTK_DBG2	0x634

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
#ifdef CONFIG_WOWLAN
#define REG_TXPKTBUF_IV_LOW             0x0484
#define REG_TXPKTBUF_IV_HIGH            0x0488
#endif

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


//============================================================
// SDIO Bus Specification
//============================================================

//-----------------------------------------------------
// SDIO CMD Address Mapping
//-----------------------------------------------------

//-----------------------------------------------------
// I/O bus domain (Host)
//-----------------------------------------------------

//-----------------------------------------------------
// SDIO register
//-----------------------------------------------------
#define SDIO_REG_HCPWM1_8723B	0x025 // HCI Current Power Mode 1


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
#define BIT_USB_RXDMA_AGG_EN	BIT(31)
#define RXDMA_AGG_MODE_EN		BIT(1)

#ifdef CONFIG_WOWLAN
#define RXPKT_RELEASE_POLL		BIT(16)
#define RXDMA_IDLE				BIT(17)
#define RW_RELEASE_EN			BIT(18)
#endif

//-----------------------------------------------------
//
//	0x0400h ~ 0x047Fh	Protocol Configuration
//
//-----------------------------------------------------

//----------------------------------------------------------------------------
//       8723B REG_CCK_CHECK						(offset 0x454)
//----------------------------------------------------------------------------
#define BIT_BCN_PORT_SEL		BIT5

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
#ifdef CONFIG_RF_GAIN_OFFSET

#ifdef CONFIG_RTL8723B
#define EEPROM_RF_GAIN_OFFSET			0xC1
#endif

#define EEPROM_RF_GAIN_VAL				0x1F6
#endif //CONFIG_RF_GAIN_OFFSET

#endif

#ifdef CONFIG_USB_HCI
//should be renamed and moved to another file
typedef	enum _BOARD_TYPE_8192CUSB{
	BOARD_USB_DONGLE 			= 0,		// USB dongle
	BOARD_USB_High_PA 		= 1,		// USB dongle with high power PA
	BOARD_MINICARD		  	= 2,		// Minicard
	BOARD_USB_SOLO 		 	= 3,		// USB solo-Slim module
	BOARD_USB_COMBO			= 4,		// USB Combo-Slim module
} BOARD_TYPE_8723BUSB, *PBOARD_TYPE_8723BUSB;

#endif