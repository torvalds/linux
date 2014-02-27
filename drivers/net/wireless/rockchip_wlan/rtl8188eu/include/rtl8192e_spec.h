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
#ifndef __RTL8192E_SPEC_H__
#define __RTL8192E_SPEC_H__

#include <drv_conf.h>


//============================================================
//       8192E Regsiter offset definition
//============================================================

//============================================================
//
//============================================================

//-----------------------------------------------------
//
//	0x0000h ~ 0x00FFh	System Configuration
//
//-----------------------------------------------------
#define REG_AFE_CTRL1_8192E			0x0024
#define REG_AFE_CTRL2_8192E			0x0028
#define REG_AFE_CTRL3_8192E			0x002c


#define REG_SDIO_CTRL_8192E			0x0070
#define REG_OPT_CTRL_8192E				0x0074
#define REG_RF_B_CTRL_8192E			0x0076
#define REG_AFE_CTRL4_8192E			0x0078 
#define REG_LDO_SWR_CTRL				0x007C
#define REG_FW_DRV_MSG_8192E			0x0088
#define REG_HMEBOX_E2_E3_8192E		0x008C
#define REG_HIMR0_8192E				0x00B0
#define REG_HISR0_8192E					0x00B4
#define REG_HIMR1_8192E					0x00B8
#define REG_HISR1_8192E					0x00BC

#define REG_SYS_CFG1_8192E				0x00F0
#define REG_SYS_CFG2_8192E				0x00FC 
//-----------------------------------------------------
//
//	0x0100h ~ 0x01FFh	MACTOP General Configuration
//
//-----------------------------------------------------
#define REG_PKTBUF_DBG_ADDR 			(REG_PKTBUF_DBG_CTRL)
#define REG_RXPKTBUF_DBG				(REG_PKTBUF_DBG_CTRL+2)
#define REG_TXPKTBUF_DBG				(REG_PKTBUF_DBG_CTRL+3)
#define REG_WOWLAN_WAKE_REASON		REG_MCUTST_WOWLAN

#define REG_RSVD3_8192E					0x0168
#define REG_C2HEVT_CMD_SEQ_88XX		0x01A1
#define REG_C2hEVT_CMD_CONTENT_88XX	0x01A2
#define REG_C2HEVT_CMD_LEN_88XX		0x01AE

#define REG_HMEBOX_EXT0_8192E			0x01F0
#define REG_HMEBOX_EXT1_8192E			0x01F4
#define REG_HMEBOX_EXT2_8192E			0x01F8
#define REG_HMEBOX_EXT3_8192E			0x01FC

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
#define REG_RXDMA_8192E					0x0290
#define REG_EARLY_MODE_CONTROL_8192E		0x02BC

#define REG_RSVD5_8192E					0x02F0
#define REG_RSVD6_8192E					0x02F4
#define REG_RSVD7_8192E					0x02F8
#define REG_RSVD8_8192E					0x02FC

//-----------------------------------------------------
//
//	0x0300h ~ 0x03FFh	PCIe
//
//-----------------------------------------------------
#define	REG_PCIE_MULTIFET_CTRL_8192E	0x036A	//PCIE Multi-Fethc Control

//-----------------------------------------------------
//
//	0x0400h ~ 0x047Fh	Protocol Configuration
//
//-----------------------------------------------------
#define REG_TXBF_CTRL_8192E				0x042C
#define REG_ARFR1_8192E					0x044C
#define REG_CCK_CHECK_8192E				0x0454
#define REG_AMPDU_MAX_TIME_8192E			0x0456
#define REG_BCNQ1_BDNY_8192E				0x0457

#define REG_AMPDU_MAX_LENGTH_8192E	0x0458
#define REG_NDPA_OPT_CTRL_8192E		0x045F
#define REG_DATA_SC_8192E				0x0483
#define REG_TXRPT_START_OFFSET			0x04AC
#define REG_AMPDU_BURST_MODE_8192E	0x04BC
#define REG_HT_SINGLE_AMPDU_8192E		0x04C7
#define REG_MACID_PKT_DROP0_8192E		0x04D0

//-----------------------------------------------------
//
//	0x0500h ~ 0x05FFh	EDCA Configuration
//
//-----------------------------------------------------
#define REG_CTWND_8192E					0x0572
#define REG_SECONDARY_CCA_CTRL_8192E	0x0577
#define REG_SCH_TXCMD_8192E			0x05F8

//-----------------------------------------------------
//
//	0x0600h ~ 0x07FFh	WMAC Configuration
//
//-----------------------------------------------------
#define REG_MAC_CR_8192E				0x0600

#define REG_MAC_TX_SM_STATE_8192E		0x06B4

// Power
#define REG_BFMER0_INFO_8192E			0x06E4
#define REG_BFMER1_INFO_8192E			0x06EC
#define REG_CSI_RPT_PARAM_BW20_8192E	0x06F4
#define REG_CSI_RPT_PARAM_BW40_8192E	0x06F8
#define REG_CSI_RPT_PARAM_BW80_8192E	0x06FC

// Hardware Port 2
#define REG_BFMEE_SEL_8192E				0x0714
#define REG_SND_PTCL_CTRL_8192E		0x0718


//-----------------------------------------------------
//
//	Redifine register definition for compatibility
//
//-----------------------------------------------------

// TODO: use these definition when using REG_xxx naming rule.
// NOTE: DO NOT Remove these definition. Use later.
#define	ISR_8192E							REG_HISR0_8192E

//----------------------------------------------------------------------------
//       8192E IMR/ISR bits						(offset 0xB0,  8bits)
//----------------------------------------------------------------------------
#define	IMR_DISABLED_8192E					0
// IMR DW0(0x00B0-00B3) Bit 0-31
#define	IMR_TIMER2_8192E					BIT31		// Timeout interrupt 2
#define	IMR_TIMER1_8192E					BIT30		// Timeout interrupt 1	
#define	IMR_PSTIMEOUT_8192E				BIT29		// Power Save Time Out Interrupt
#define	IMR_GTINT4_8192E					BIT28		// When GTIMER4 expires, this bit is set to 1	
#define	IMR_GTINT3_8192E					BIT27		// When GTIMER3 expires, this bit is set to 1	
#define	IMR_TXBCN0ERR_8192E				BIT26		// Transmit Beacon0 Error			
#define	IMR_TXBCN0OK_8192E					BIT25		// Transmit Beacon0 OK			
#define	IMR_TSF_BIT32_TOGGLE_8192E		BIT24		// TSF Timer BIT32 toggle indication interrupt			
#define	IMR_BCNDMAINT0_8192E				BIT20		// Beacon DMA Interrupt 0			
#define	IMR_BCNDERR0_8192E					BIT16		// Beacon Queue DMA OK0			
#define	IMR_HSISR_IND_ON_INT_8192E		BIT15		// HSISR Indicator (HSIMR & HSISR is true, this bit is set to 1)
#define	IMR_BCNDMAINT_E_8192E				BIT14		// Beacon DMA Interrupt Extension for Win7			
#define	IMR_ATIMEND_8192E					BIT12		// CTWidnow End or ATIM Window End
#define	IMR_C2HCMD_8192E					BIT10		// CPU to Host Command INT Status, Write 1 clear	
#define	IMR_CPWM2_8192E					BIT9			// CPU power Mode exchange INT Status, Write 1 clear	
#define	IMR_CPWM_8192E						BIT8			// CPU power Mode exchange INT Status, Write 1 clear	
#define	IMR_HIGHDOK_8192E					BIT7			// High Queue DMA OK	
#define	IMR_MGNTDOK_8192E					BIT6			// Management Queue DMA OK	
#define	IMR_BKDOK_8192E					BIT5			// AC_BK DMA OK		
#define	IMR_BEDOK_8192E					BIT4			// AC_BE DMA OK	
#define	IMR_VIDOK_8192E					BIT3			// AC_VI DMA OK		
#define	IMR_VODOK_8192E					BIT2			// AC_VO DMA OK	
#define	IMR_RDU_8192E						BIT1			// Rx Descriptor Unavailable	
#define	IMR_ROK_8192E						BIT0			// Receive DMA OK

// IMR DW1(0x00B4-00B7) Bit 0-31
#define	IMR_BCNDMAINT7_8192E				BIT27		// Beacon DMA Interrupt 7
#define	IMR_BCNDMAINT6_8192E				BIT26		// Beacon DMA Interrupt 6
#define	IMR_BCNDMAINT5_8192E				BIT25		// Beacon DMA Interrupt 5
#define	IMR_BCNDMAINT4_8192E				BIT24		// Beacon DMA Interrupt 4
#define	IMR_BCNDMAINT3_8192E				BIT23		// Beacon DMA Interrupt 3
#define	IMR_BCNDMAINT2_8192E				BIT22		// Beacon DMA Interrupt 2
#define	IMR_BCNDMAINT1_8192E				BIT21		// Beacon DMA Interrupt 1
#define	IMR_BCNDOK7_8192E					BIT20		// Beacon Queue DMA OK Interrup 7
#define	IMR_BCNDOK6_8192E					BIT19		// Beacon Queue DMA OK Interrup 6
#define	IMR_BCNDOK5_8192E					BIT18		// Beacon Queue DMA OK Interrup 5
#define	IMR_BCNDOK4_8192E					BIT17		// Beacon Queue DMA OK Interrup 4
#define	IMR_BCNDOK3_8192E					BIT16		// Beacon Queue DMA OK Interrup 3
#define	IMR_BCNDOK2_8192E					BIT15		// Beacon Queue DMA OK Interrup 2
#define	IMR_BCNDOK1_8192E					BIT14		// Beacon Queue DMA OK Interrup 1
#define	IMR_ATIMEND_E_8192E				BIT13		// ATIM Window End Extension for Win7
#define	IMR_TXERR_8192E					BIT11		// Tx Error Flag Interrupt Status, write 1 clear.
#define	IMR_RXERR_8192E					BIT10		// Rx Error Flag INT Status, Write 1 clear
#define	IMR_TXFOVW_8192E					BIT9			// Transmit FIFO Overflow
#define	IMR_RXFOVW_8192E					BIT8			// Receive FIFO Overflow

//----------------------------------------------------------------------------
//       8192E Auto LLT bits						(offset 0x224,  8bits)
//----------------------------------------------------------------------------
//224 REG_AUTO_LLT
// move to hal_com_reg.h

//----------------------------------------------------------------------------
//       8192E Auto LLT bits						(offset 0x290,  32bits)
//----------------------------------------------------------------------------
#define BIT_DMA_MODE			BIT1
#define BIT_USB_RXDMA_AGG_EN	BIT31

//----------------------------------------------------------------------------
//       8192E REG_SYS_CFG1						(offset 0xF0,  32bits)
//----------------------------------------------------------------------------
#define BIT_SPSLDO_SEL			BIT24


//----------------------------------------------------------------------------
//       8192E REG_CCK_CHECK						(offset 0x454,  8bits)
//----------------------------------------------------------------------------
#define BIT_BCN_PORT_SEL		BIT5

//============================================================================
//       Regsiter Bit and Content definition 
//============================================================================

//2 ACMHWCTRL 0x05C0
#define	AcmHw_HwEn_8192E				BIT(0)
#define	AcmHw_VoqEn_8192E				BIT(1)
#define	AcmHw_ViqEn_8192E				BIT(2)
#define	AcmHw_BeqEn_8192E				BIT(3)
#define	AcmHw_VoqStatus_8192E			BIT(5)
#define	AcmHw_ViqStatus_8192E			BIT(6)
#define	AcmHw_BeqStatus_8192E			BIT(7)




#endif //__RTL8192E_SPEC_H__

