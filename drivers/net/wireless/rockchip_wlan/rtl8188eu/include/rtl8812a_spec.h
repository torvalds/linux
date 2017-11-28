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
#ifndef __RTL8812A_SPEC_H__
#define __RTL8812A_SPEC_H__

#include <drv_conf.h>


/* ************************************************************
* 8812 Regsiter offset definition
* ************************************************************ */

/* ************************************************************
*
* ************************************************************ */

/* -----------------------------------------------------
*
*	0x0000h ~ 0x00FFh	System Configuration
*
* ----------------------------------------------------- */
#define REG_SYS_CLKR_8812A				0x0008
#define REG_AFE_PLL_CTRL_8812A		0x0028
#define REG_HSIMR_8812					0x0058
#define REG_HSISR_8812					0x005c
#define REG_GPIO_EXT_CTRL				0x0060
#define REG_GPIO_STATUS_8812			0x006C
#define REG_SDIO_CTRL_8812				0x0070
#define REG_OPT_CTRL_8812				0x0074
#define REG_RF_B_CTRL_8812				0x0076
#define REG_FW_DRV_MSG_8812			0x0088
#define REG_HMEBOX_E2_E3_8812			0x008C
#define REG_HIMR0_8812					0x00B0
#define REG_HISR0_8812					0x00B4
#define REG_HIMR1_8812					0x00B8
#define REG_HISR1_8812					0x00BC
#define REG_EFUSE_BURN_GNT_8812		0x00CF
#define REG_SYS_CFG1_8812				0x00FC

/* -----------------------------------------------------
*
*	0x0100h ~ 0x01FFh	MACTOP General Configuration
*
* ----------------------------------------------------- */
#define REG_CR_8812A					0x100
#define REG_PKTBUF_DBG_ADDR			(REG_PKTBUF_DBG_CTRL)
#define REG_RXPKTBUF_DBG				(REG_PKTBUF_DBG_CTRL+2)
#define REG_TXPKTBUF_DBG				(REG_PKTBUF_DBG_CTRL+3)
#define REG_WOWLAN_WAKE_REASON			REG_MCUTST_WOWLAN

#define REG_RSVD3_8812					0x0168
#define REG_C2HEVT_CMD_SEQ_88XX		0x01A1
#define REG_C2hEVT_CMD_CONTENT_88XX	0x01A2
#define REG_C2HEVT_CMD_LEN_88XX		0x01AE

#define REG_HMEBOX_EXT0_8812			0x01F0
#define REG_HMEBOX_EXT1_8812			0x01F4
#define REG_HMEBOX_EXT2_8812			0x01F8
#define REG_HMEBOX_EXT3_8812			0x01FC

/* -----------------------------------------------------
*
*	0x0200h ~ 0x027Fh	TXDMA Configuration
*
* ----------------------------------------------------- */
#define REG_DWBCN0_CTRL_8812				REG_TDECTRL
#define REG_DWBCN1_CTRL_8812				0x0228

/* -----------------------------------------------------
*
*	0x0280h ~ 0x02FFh	RXDMA Configuration
*
* ----------------------------------------------------- */
#define REG_TDECTRL_8812A				0x0208
#define REG_RXDMA_CONTROL_8812A		0x0286		/*Control the RX DMA.*/
#define REG_RXDMA_PRO_8812			0x0290
#define REG_EARLY_MODE_CONTROL_8812	0x02BC
#define REG_RSVD5_8812					0x02F0
#define REG_RSVD6_8812					0x02F4
#define REG_RSVD7_8812					0x02F8
#define REG_RSVD8_8812					0x02FC


/* -----------------------------------------------------
*
*	0x0300h ~ 0x03FFh	PCIe
*
* ----------------------------------------------------- */
#define	REG_PCIE_CTRL_REG_8812A		0x0300
#define	REG_DBI_WDATA_8812			0x0348	/* DBI Write Data */
#define	REG_DBI_RDATA_8812			0x034C	/* DBI Read Data */
#define	REG_DBI_ADDR_8812				0x0350	/* DBI Address */
#define	REG_DBI_FLAG_8812				0x0352	/* DBI Read/Write Flag */
#define	REG_MDIO_WDATA_8812			0x0354	/* MDIO for Write PCIE PHY */
#define	REG_MDIO_RDATA_8812			0x0356	/* MDIO for Reads PCIE PHY */
#define	REG_MDIO_CTL_8812				0x0358	/* MDIO for Control */
#define	REG_PCIE_MULTIFET_CTRL_8812	0x036A	/* PCIE Multi-Fethc Control */

/* -----------------------------------------------------
*
*	0x0400h ~ 0x047Fh	Protocol Configuration
*
* ----------------------------------------------------- */
#define REG_TXPKT_EMPTY_8812A			0x041A
#define REG_FWHW_TXQ_CTRL_8812A		0x0420
#define REG_TXBF_CTRL_8812A			0x042C
#define REG_ARFR0_8812					0x0444
#define REG_ARFR1_8812					0x044C
#define REG_CCK_CHECK_8812				0x0454
#define REG_AMPDU_MAX_TIME_8812		0x0456
#define REG_TXPKTBUF_BCNQ_BDNY1_8812	0x0457

#define REG_AMPDU_MAX_LENGTH_8812	0x0458
#define REG_TXPKTBUF_WMAC_LBK_BF_HD_8812	0x045D
#define REG_NDPA_OPT_CTRL_8812A		0x045F
#define REG_DATA_SC_8812				0x0483
#ifdef CONFIG_WOWLAN
#define REG_TXPKTBUF_IV_LOW             0x0484
#define REG_TXPKTBUF_IV_HIGH            0x0488
#endif
#define REG_ARFR2_8812					0x048C
#define REG_ARFR3_8812					0x0494
#define REG_TXRPT_START_OFFSET		0x04AC
#define REG_AMPDU_BURST_MODE_8812	0x04BC
#define REG_HT_SINGLE_AMPDU_8812		0x04C7
#define REG_MACID_PKT_DROP0_8812		0x04D0

/* -----------------------------------------------------
*
*	0x0500h ~ 0x05FFh	EDCA Configuration
*
* ----------------------------------------------------- */
#define REG_TXPAUSE_8812A				0x0522
#define REG_CTWND_8812					0x0572
#define REG_SECONDARY_CCA_CTRL_8812	0x0577
#define REG_SCH_TXCMD_8812A			0x05F8

/* -----------------------------------------------------
*
*	0x0600h ~ 0x07FFh	WMAC Configuration
*
* ----------------------------------------------------- */
#define REG_MAC_CR_8812				0x0600

#define REG_MAC_TX_SM_STATE_8812		0x06B4

/* Power */
#define REG_BFMER0_INFO_8812A			0x06E4
#define REG_BFMER1_INFO_8812A			0x06EC
#define REG_CSI_RPT_PARAM_BW20_8812A	0x06F4
#define REG_CSI_RPT_PARAM_BW40_8812A	0x06F8
#define REG_CSI_RPT_PARAM_BW80_8812A	0x06FC

/* Hardware Port 2 */
#define REG_BFMEE_SEL_8812A			0x0714
#define REG_SND_PTCL_CTRL_8812A		0x0718


/* -----------------------------------------------------
*
*	Redifine register definition for compatibility
*
* ----------------------------------------------------- */

/* TODO: use these definition when using REG_xxx naming rule.
* NOTE: DO NOT Remove these definition. Use later. */
#define	ISR_8812							REG_HISR0_8812

/* ----------------------------------------------------------------------------
* 8195 IMR/ISR bits						(offset 0xB0,  8bits)
* ---------------------------------------------------------------------------- */
#define	IMR_DISABLED_8812					0
/* IMR DW0(0x00B0-00B3) Bit 0-31 */
#define	IMR_TIMER2_8812					BIT31		/* Timeout interrupt 2 */
#define	IMR_TIMER1_8812					BIT30		/* Timeout interrupt 1	 */
#define	IMR_PSTIMEOUT_8812				BIT29		/* Power Save Time Out Interrupt */
#define	IMR_GTINT4_8812					BIT28		/* When GTIMER4 expires, this bit is set to 1	 */
#define	IMR_GTINT3_8812					BIT27		/* When GTIMER3 expires, this bit is set to 1	 */
#define	IMR_TXBCN0ERR_8812				BIT26		/* Transmit Beacon0 Error			 */
#define	IMR_TXBCN0OK_8812					BIT25		/* Transmit Beacon0 OK			 */
#define	IMR_TSF_BIT32_TOGGLE_8812		BIT24		/* TSF Timer BIT32 toggle indication interrupt			 */
#define	IMR_BCNDMAINT0_8812				BIT20		/* Beacon DMA Interrupt 0			 */
#define	IMR_BCNDERR0_8812					BIT16		/* Beacon Queue DMA OK0			 */
#define	IMR_HSISR_IND_ON_INT_8812		BIT15		/* HSISR Indicator (HSIMR & HSISR is true, this bit is set to 1) */
#define	IMR_BCNDMAINT_E_8812				BIT14		/* Beacon DMA Interrupt Extension for Win7			 */
#define	IMR_ATIMEND_8812					BIT12		/* CTWidnow End or ATIM Window End */
#define	IMR_C2HCMD_8812					BIT10		/* CPU to Host Command INT Status, Write 1 clear	 */
#define	IMR_CPWM2_8812					BIT9			/* CPU power Mode exchange INT Status, Write 1 clear	 */
#define	IMR_CPWM_8812						BIT8			/* CPU power Mode exchange INT Status, Write 1 clear	 */
#define	IMR_HIGHDOK_8812					BIT7			/* High Queue DMA OK	 */
#define	IMR_MGNTDOK_8812					BIT6			/* Management Queue DMA OK	 */
#define	IMR_BKDOK_8812					BIT5			/* AC_BK DMA OK		 */
#define	IMR_BEDOK_8812					BIT4			/* AC_BE DMA OK	 */
#define	IMR_VIDOK_8812					BIT3			/* AC_VI DMA OK		 */
#define	IMR_VODOK_8812					BIT2			/* AC_VO DMA OK	 */
#define	IMR_RDU_8812						BIT1			/* Rx Descriptor Unavailable	 */
#define	IMR_ROK_8812						BIT0			/* Receive DMA OK */

/* IMR DW1(0x00B4-00B7) Bit 0-31 */
#define	IMR_BCNDMAINT7_8812				BIT27		/* Beacon DMA Interrupt 7 */
#define	IMR_BCNDMAINT6_8812				BIT26		/* Beacon DMA Interrupt 6 */
#define	IMR_BCNDMAINT5_8812				BIT25		/* Beacon DMA Interrupt 5 */
#define	IMR_BCNDMAINT4_8812				BIT24		/* Beacon DMA Interrupt 4 */
#define	IMR_BCNDMAINT3_8812				BIT23		/* Beacon DMA Interrupt 3 */
#define	IMR_BCNDMAINT2_8812				BIT22		/* Beacon DMA Interrupt 2 */
#define	IMR_BCNDMAINT1_8812				BIT21		/* Beacon DMA Interrupt 1 */
#define	IMR_BCNDOK7_8812					BIT20		/* Beacon Queue DMA OK Interrup 7 */
#define	IMR_BCNDOK6_8812					BIT19		/* Beacon Queue DMA OK Interrup 6 */
#define	IMR_BCNDOK5_8812					BIT18		/* Beacon Queue DMA OK Interrup 5 */
#define	IMR_BCNDOK4_8812					BIT17		/* Beacon Queue DMA OK Interrup 4 */
#define	IMR_BCNDOK3_8812					BIT16		/* Beacon Queue DMA OK Interrup 3 */
#define	IMR_BCNDOK2_8812					BIT15		/* Beacon Queue DMA OK Interrup 2 */
#define	IMR_BCNDOK1_8812					BIT14		/* Beacon Queue DMA OK Interrup 1 */
#define	IMR_ATIMEND_E_8812				BIT13		/* ATIM Window End Extension for Win7 */
#define	IMR_TXERR_8812					BIT11		/* Tx Error Flag Interrupt Status, write 1 clear. */
#define	IMR_RXERR_8812					BIT10		/* Rx Error Flag INT Status, Write 1 clear */
#define	IMR_TXFOVW_8812					BIT9			/* Transmit FIFO Overflow */
#define	IMR_RXFOVW_8812					BIT8			/* Receive FIFO Overflow */


#ifdef CONFIG_PCI_HCI
/* #define IMR_RX_MASK		(IMR_ROK_8812|IMR_RDU_8812|IMR_RXFOVW_8812) */
#define IMR_TX_MASK			(IMR_VODOK_8812 | IMR_VIDOK_8812 | IMR_BEDOK_8812 | IMR_BKDOK_8812 | IMR_MGNTDOK_8812 | IMR_HIGHDOK_8812)

#define RT_BCN_INT_MASKS	(IMR_BCNDMAINT0_8812 | IMR_TXBCN0OK_8812 | IMR_TXBCN0ERR_8812 | IMR_BCNDERR0_8812)

#define RT_AC_INT_MASKS	(IMR_VIDOK_8812 | IMR_VODOK_8812 | IMR_BEDOK_8812 | IMR_BKDOK_8812)
#endif


/* ****************************************************************************
* Regsiter Bit and Content definition
* **************************************************************************** */

/* 2 ACMHWCTRL 0x05C0 */
#define	AcmHw_HwEn_8812				BIT(0)
#define	AcmHw_VoqEn_8812				BIT(1)
#define	AcmHw_ViqEn_8812				BIT(2)
#define	AcmHw_BeqEn_8812				BIT(3)
#define	AcmHw_VoqStatus_8812			BIT(5)
#define	AcmHw_ViqStatus_8812			BIT(6)
#define	AcmHw_BeqStatus_8812			BIT(7)

#endif /* __RTL8812A_SPEC_H__ */

#ifdef CONFIG_RTL8821A
#include "rtl8821a_spec.h"
#endif /* CONFIG_RTL8821A */
