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
#ifndef __RTL8188F_SPEC_H__
#define __RTL8188F_SPEC_H__

#include <drv_conf.h>


#define HAL_NAV_UPPER_UNIT_8188F		128		// micro-second

//-----------------------------------------------------
//
//	0x0000h ~ 0x00FFh	System Configuration
//
//-----------------------------------------------------
#define REG_RSV_CTRL_8188F				0x001C	// 3 Byte
#define REG_BT_WIFI_ANTENNA_SWITCH_8188F	0x0038
#define REG_HSISR_8188F					0x005c
#define REG_PAD_CTRL1_8188F		0x0064
#define REG_AFE_CTRL_4_8188F		0x0078
#define REG_HMEBOX_DBG_0_8188F	0x0088
#define REG_HMEBOX_DBG_1_8188F	0x008A
#define REG_HMEBOX_DBG_2_8188F	0x008C
#define REG_HMEBOX_DBG_3_8188F	0x008E
#define REG_HIMR0_8188F					0x00B0
#define REG_HISR0_8188F					0x00B4
#define REG_HIMR1_8188F					0x00B8
#define REG_HISR1_8188F					0x00BC
#define REG_PMC_DBG_CTRL2_8188F			0x00CC

//-----------------------------------------------------
//
//	0x0100h ~ 0x01FFh	MACTOP General Configuration
//
//-----------------------------------------------------
#define REG_C2HEVT_CMD_ID_8188F	0x01A0
#define REG_C2HEVT_CMD_LEN_8188F	0x01AE
#define REG_WOWLAN_WAKE_REASON 0x01C7
#define REG_WOWLAN_GTK_DBG1	0x630
#define REG_WOWLAN_GTK_DBG2	0x634

#define REG_HMEBOX_EXT0_8188F			0x01F0
#define REG_HMEBOX_EXT1_8188F			0x01F4
#define REG_HMEBOX_EXT2_8188F			0x01F8
#define REG_HMEBOX_EXT3_8188F			0x01FC

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
#define REG_RXDMA_CONTROL_8188F		0x0286 // Control the RX DMA.
#define REG_RXDMA_MODE_CTRL_8188F		0x0290

//-----------------------------------------------------
//
//	0x0300h ~ 0x03FFh	PCIe
//
//-----------------------------------------------------
#define	REG_PCIE_CTRL_REG_8188F		0x0300
#define	REG_INT_MIG_8188F				0x0304	// Interrupt Migration 
#define	REG_BCNQ_DESA_8188F			0x0308	// TX Beacon Descriptor Address
#define	REG_HQ_DESA_8188F				0x0310	// TX High Queue Descriptor Address
#define	REG_MGQ_DESA_8188F			0x0318	// TX Manage Queue Descriptor Address
#define	REG_VOQ_DESA_8188F			0x0320	// TX VO Queue Descriptor Address
#define	REG_VIQ_DESA_8188F				0x0328	// TX VI Queue Descriptor Address
#define	REG_BEQ_DESA_8188F			0x0330	// TX BE Queue Descriptor Address
#define	REG_BKQ_DESA_8188F			0x0338	// TX BK Queue Descriptor Address
#define	REG_RX_DESA_8188F				0x0340	// RX Queue	Descriptor Address
#define	REG_DBI_WDATA_8188F			0x0348	// DBI Write Data
#define	REG_DBI_RDATA_8188F			0x034C	// DBI Read Data
#define	REG_DBI_ADDR_8188F				0x0350	// DBI Address
#define	REG_DBI_FLAG_8188F				0x0352	// DBI Read/Write Flag
#define	REG_MDIO_WDATA_8188F		0x0354	// MDIO for Write PCIE PHY
#define	REG_MDIO_RDATA_8188F			0x0356	// MDIO for Reads PCIE PHY
#define	REG_MDIO_CTL_8188F			0x0358	// MDIO for Control
#define	REG_DBG_SEL_8188F				0x0360	// Debug Selection Register
#define	REG_PCIE_HRPWM_8188F			0x0361	//PCIe RPWM
#define	REG_PCIE_HCPWM_8188F			0x0363	//PCIe CPWM
#define	REG_PCIE_MULTIFET_CTRL_8188F	0x036A	//PCIE Multi-Fethc Control

//-----------------------------------------------------
//
//	0x0400h ~ 0x047Fh	Protocol Configuration
//
//-----------------------------------------------------
#define REG_TXPKTBUF_BCNQ_BDNY_8188F	0x0424
#define REG_TXPKTBUF_MGQ_BDNY_8188F	0x0425
#define REG_TXPKTBUF_WMAC_LBK_BF_HD_8188F	0x045D
#ifdef CONFIG_WOWLAN
#define REG_TXPKTBUF_IV_LOW             0x0484
#define REG_TXPKTBUF_IV_HIGH            0x0488
#endif
#define REG_AMPDU_BURST_MODE_8188F	0x04BC

//-----------------------------------------------------
//
//	0x0500h ~ 0x05FFh	EDCA Configuration
//
//-----------------------------------------------------
#define REG_SECONDARY_CCA_CTRL_8188F	0x0577

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
#define SDIO_REG_HIQ_FREEPG_8188F		0x0020
#define SDIO_REG_MID_FREEPG_8188F		0x0022
#define SDIO_REG_LOW_FREEPG_8188F		0x0024
#define SDIO_REG_PUB_FREEPG_8188F		0x0026
#define SDIO_REG_EXQ_FREEPG_8188F		0x0028
#define SDIO_REG_AC_OQT_FREEPG_8188F	0x002A
#define SDIO_REG_NOAC_OQT_FREEPG_8188F	0x002B

#define SDIO_REG_HCPWM1_8188F			0x0038

/* indirect access */
#define SDIO_REG_INDIRECT_REG_CFG_8188F		0x40
#define SET_INDIRECT_REG_ADDR(_cmd, _addr)	SET_BITS_TO_LE_2BYTE(((u8 *)(_cmd)) + 0, 0, 16, (_addr))
#define SET_INDIRECT_REG_SIZE_1BYTE(_cmd)	SET_BITS_TO_LE_1BYTE(((u8 *)(_cmd)) + 2, 0, 2, 0)
#define SET_INDIRECT_REG_SIZE_2BYTE(_cmd)	SET_BITS_TO_LE_1BYTE(((u8 *)(_cmd)) + 2, 0, 2, 1)
#define SET_INDIRECT_REG_SIZE_4BYTE(_cmd)	SET_BITS_TO_LE_1BYTE(((u8 *)(_cmd)) + 2, 0, 2, 2)
#define SET_INDIRECT_REG_WRITE(_cmd)		SET_BITS_TO_LE_1BYTE(((u8 *)(_cmd)) + 2, 2, 1, 1)
#define SET_INDIRECT_REG_READ(_cmd)			SET_BITS_TO_LE_1BYTE(((u8 *)(_cmd)) + 2, 3, 1, 1)
#define GET_INDIRECT_REG_RDY(_cmd)			LE_BITS_TO_1BYTE(((u8 *)(_cmd)) + 2, 4, 1)

#define SDIO_REG_INDIRECT_REG_DATA_8188F	0x44

//============================================================================
//	8188 Regsiter Bit and Content definition
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
#define BIT_DMA_MODE			BIT(1)

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
//       8188F REG_CCK_CHECK						(offset 0x454)
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

//----------------------------------------------------------------------------
//       8195 IMR/ISR bits						(offset 0xB0,  8bits)
//----------------------------------------------------------------------------
#define	IMR_DISABLED_8188F					0
// IMR DW0(0x00B0-00B3) Bit 0-31
#define	IMR_TIMER2_8188F					BIT31		// Timeout interrupt 2
#define	IMR_TIMER1_8188F					BIT30		// Timeout interrupt 1	
#define	IMR_PSTIMEOUT_8188F				BIT29		// Power Save Time Out Interrupt
#define	IMR_GTINT4_8188F					BIT28		// When GTIMER4 expires, this bit is set to 1	
#define	IMR_GTINT3_8188F					BIT27		// When GTIMER3 expires, this bit is set to 1	
#define	IMR_TXBCN0ERR_8188F				BIT26		// Transmit Beacon0 Error			
#define	IMR_TXBCN0OK_8188F				BIT25		// Transmit Beacon0 OK			
#define	IMR_TSF_BIT32_TOGGLE_8188F		BIT24		// TSF Timer BIT32 toggle indication interrupt			
#define	IMR_BCNDMAINT0_8188F				BIT20		// Beacon DMA Interrupt 0			
#define	IMR_BCNDERR0_8188F				BIT16		// Beacon Queue DMA OK0			
#define	IMR_HSISR_IND_ON_INT_8188F		BIT15		// HSISR Indicator (HSIMR & HSISR is true, this bit is set to 1)
#define	IMR_BCNDMAINT_E_8188F			BIT14		// Beacon DMA Interrupt Extension for Win7			
#define	IMR_ATIMEND_8188F				BIT12		// CTWidnow End or ATIM Window End
#define	IMR_C2HCMD_8188F					BIT10		// CPU to Host Command INT Status, Write 1 clear	
#define	IMR_CPWM2_8188F					BIT9			// CPU power Mode exchange INT Status, Write 1 clear	
#define	IMR_CPWM_8188F					BIT8			// CPU power Mode exchange INT Status, Write 1 clear	
#define	IMR_HIGHDOK_8188F				BIT7			// High Queue DMA OK	
#define	IMR_MGNTDOK_8188F				BIT6			// Management Queue DMA OK	
#define	IMR_BKDOK_8188F					BIT5			// AC_BK DMA OK		
#define	IMR_BEDOK_8188F					BIT4			// AC_BE DMA OK	
#define	IMR_VIDOK_8188F					BIT3			// AC_VI DMA OK		
#define	IMR_VODOK_8188F					BIT2			// AC_VO DMA OK	
#define	IMR_RDU_8188F					BIT1			// Rx Descriptor Unavailable	
#define	IMR_ROK_8188F					BIT0			// Receive DMA OK

// IMR DW1(0x00B4-00B7) Bit 0-31
#define	IMR_BCNDMAINT7_8188F				BIT27		// Beacon DMA Interrupt 7
#define	IMR_BCNDMAINT6_8188F				BIT26		// Beacon DMA Interrupt 6
#define	IMR_BCNDMAINT5_8188F				BIT25		// Beacon DMA Interrupt 5
#define	IMR_BCNDMAINT4_8188F				BIT24		// Beacon DMA Interrupt 4
#define	IMR_BCNDMAINT3_8188F				BIT23		// Beacon DMA Interrupt 3
#define	IMR_BCNDMAINT2_8188F				BIT22		// Beacon DMA Interrupt 2
#define	IMR_BCNDMAINT1_8188F				BIT21		// Beacon DMA Interrupt 1
#define	IMR_BCNDOK7_8188F					BIT20		// Beacon Queue DMA OK Interrup 7
#define	IMR_BCNDOK6_8188F					BIT19		// Beacon Queue DMA OK Interrup 6
#define	IMR_BCNDOK5_8188F					BIT18		// Beacon Queue DMA OK Interrup 5
#define	IMR_BCNDOK4_8188F					BIT17		// Beacon Queue DMA OK Interrup 4
#define	IMR_BCNDOK3_8188F					BIT16		// Beacon Queue DMA OK Interrup 3
#define	IMR_BCNDOK2_8188F					BIT15		// Beacon Queue DMA OK Interrup 2
#define	IMR_BCNDOK1_8188F					BIT14		// Beacon Queue DMA OK Interrup 1
#define	IMR_ATIMEND_E_8188F				BIT13		// ATIM Window End Extension for Win7
#define	IMR_TXERR_8188F					BIT11		// Tx Error Flag Interrupt Status, write 1 clear.
#define	IMR_RXERR_8188F					BIT10		// Rx Error Flag INT Status, Write 1 clear
#define	IMR_TXFOVW_8188F					BIT9			// Transmit FIFO Overflow
#define	IMR_RXFOVW_8188F					BIT8			// Receive FIFO Overflow

#ifdef CONFIG_PCI_HCI
//#define IMR_RX_MASK		(IMR_ROK_8188F|IMR_RDU_8188F|IMR_RXFOVW_8188F)
#define IMR_TX_MASK			(IMR_VODOK_8188F|IMR_VIDOK_8188F|IMR_BEDOK_8188F|IMR_BKDOK_8188F|IMR_MGNTDOK_8188F|IMR_HIGHDOK_8188F)

#define RT_BCN_INT_MASKS	(IMR_BCNDMAINT0_8188F | IMR_TXBCN0OK_8188F | IMR_TXBCN0ERR_8188F | IMR_BCNDERR0_8188F)

#define RT_AC_INT_MASKS	(IMR_VIDOK_8188F | IMR_VODOK_8188F | IMR_BEDOK_8188F|IMR_BKDOK_8188F)
#endif

//========================================================
// General definitions
//========================================================

#define MACID_NUM_8188F 16
#define SEC_CAM_ENT_NUM_8188F 16
#define NSS_NUM_8188F 1
#define BAND_CAP_8188F (BAND_CAP_2G)
#define BW_CAP_8188F (BW_CAP_20M | BW_CAP_40M)
#define PROTO_CAP_8188F (PROTO_CAP_11B|PROTO_CAP_11G|PROTO_CAP_11N)

#endif /* __RTL8188F_SPEC_H__ */

