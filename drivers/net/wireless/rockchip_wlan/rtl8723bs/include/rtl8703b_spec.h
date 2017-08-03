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
#ifndef __RTL8703B_SPEC_H__
#define __RTL8703B_SPEC_H__

#include <drv_conf.h>


#define HAL_NAV_UPPER_UNIT_8703B		128		// micro-second

//-----------------------------------------------------
//
//	0x0000h ~ 0x00FFh	System Configuration
//
//-----------------------------------------------------
#define REG_SYS_ISO_CTRL_8703B			0x0000	// 2 Byte
#define REG_SYS_FUNC_EN_8703B			0x0002	// 2 Byte
#define REG_APS_FSMCO_8703B			0x0004	// 4 Byte
#define REG_SYS_CLKR_8703B				0x0008	// 2 Byte
#define REG_9346CR_8703B				0x000A	// 2 Byte
#define REG_EE_VPD_8703B				0x000C	// 2 Byte
#define REG_AFE_MISC_8703B				0x0010	// 1 Byte
#define REG_SPS0_CTRL_8703B				0x0011	// 7 Byte
#define REG_SPS_OCP_CFG_8703B			0x0018	// 4 Byte
#define REG_RSV_CTRL_8703B				0x001C	// 3 Byte
#define REG_RF_CTRL_8703B				0x001F	// 1 Byte
#define REG_LPLDO_CTRL_8703B			0x0023	// 1 Byte
#define REG_AFE_XTAL_CTRL_8703B		0x0024	// 4 Byte
#define REG_AFE_PLL_CTRL_8703B			0x0028	// 4 Byte
#define REG_MAC_PLL_CTRL_EXT_8703B		0x002c 	// 4 Byte
#define REG_EFUSE_CTRL_8703B			0x0030
#define REG_EFUSE_TEST_8703B			0x0034
#define REG_PWR_DATA_8703B				0x0038
#define REG_CAL_TIMER_8703B				0x003C
#define REG_ACLK_MON_8703B				0x003E
#define REG_GPIO_MUXCFG_8703B			0x0040
#define REG_GPIO_IO_SEL_8703B			0x0042
#define REG_MAC_PINMUX_CFG_8703B		0x0043
#define REG_GPIO_PIN_CTRL_8703B			0x0044
#define REG_GPIO_INTM_8703B				0x0048
#define REG_LEDCFG0_8703B				0x004C
#define REG_LEDCFG1_8703B				0x004D
#define REG_LEDCFG2_8703B				0x004E
#define REG_LEDCFG3_8703B				0x004F
#define REG_FSIMR_8703B					0x0050
#define REG_FSISR_8703B					0x0054
#define REG_HSIMR_8703B					0x0058
#define REG_HSISR_8703B					0x005c
#define REG_GPIO_EXT_CTRL				0x0060
#define REG_PAD_CTRL1_8703B		0x0064
#define REG_MULTI_FUNC_CTRL_8703B		0x0068
#define REG_GPIO_STATUS_8703B			0x006C
#define REG_SDIO_CTRL_8703B				0x0070
#define REG_OPT_CTRL_8703B				0x0074
#define REG_AFE_CTRL_4_8703B		0x0078
#define REG_MCUFWDL_8703B				0x0080
#define REG_HMEBOX_DBG_0_8703B	0x0088
#define REG_HMEBOX_DBG_1_8703B	0x008A
#define REG_HMEBOX_DBG_2_8703B	0x008C
#define REG_HMEBOX_DBG_3_8703B	0x008E
#define REG_HIMR0_8703B					0x00B0
#define REG_HISR0_8703B					0x00B4
#define REG_HIMR1_8703B					0x00B8
#define REG_HISR1_8703B					0x00BC
#define REG_PMC_DBG_CTRL2_8703B			0x00CC
#define	REG_EFUSE_BURN_GNT_8703B		0x00CF
#define REG_HPON_FSM_8703B				0x00EC
#define REG_SYS_CFG_8703B				0x00F0
#define REG_SYS_CFG1_8703B				0x00FC
#define REG_ROM_VERSION					0x00FD

//-----------------------------------------------------
//
//	0x0100h ~ 0x01FFh	MACTOP General Configuration
//
//-----------------------------------------------------
#define REG_C2HEVT_CMD_ID_8703B	0x01A0
#define REG_C2HEVT_CMD_SEQ_88XX		0x01A1
#define REG_C2hEVT_CMD_CONTENT_88XX	0x01A2
#define REG_C2HEVT_CMD_LEN_8703B        0x01AE
#define REG_C2HEVT_CMD_LEN_88XX		REG_C2HEVT_CMD_LEN_8703B
#define REG_C2HEVT_CLEAR_8703B			0x01AF
#define REG_MCUTST_1_8703B				0x01C0
#define REG_WOWLAN_WAKE_REASON 0x01C7
#define REG_FMETHR_8703B				0x01C8
#define REG_HMETFR_8703B				0x01CC
#define REG_HMEBOX_0_8703B				0x01D0
#define REG_HMEBOX_1_8703B				0x01D4
#define REG_HMEBOX_2_8703B				0x01D8
#define REG_HMEBOX_3_8703B				0x01DC
#define REG_LLT_INIT_8703B				0x01E0
#define REG_HMEBOX_EXT0_8703B			0x01F0
#define REG_HMEBOX_EXT1_8703B			0x01F4
#define REG_HMEBOX_EXT2_8703B			0x01F8
#define REG_HMEBOX_EXT3_8703B			0x01FC

//-----------------------------------------------------
//
//	0x0200h ~ 0x027Fh	TXDMA Configuration
//
//-----------------------------------------------------
#define REG_RQPN_8703B					0x0200
#define REG_FIFOPAGE_8703B				0x0204
#define REG_DWBCN0_CTRL_8703B			REG_TDECTRL
#define REG_TXDMA_OFFSET_CHK_8703B	0x020C
#define REG_TXDMA_STATUS_8703B		0x0210
#define REG_RQPN_NPQ_8703B			0x0214
#define REG_DWBCN1_CTRL_8703B			0x0228


//-----------------------------------------------------
//
//	0x0280h ~ 0x02FFh	RXDMA Configuration
//
//-----------------------------------------------------
#define REG_RXDMA_AGG_PG_TH_8703B		0x0280
#define REG_FW_UPD_RDPTR_8703B		0x0284 // FW shall update this register before FW write RXPKT_RELEASE_POLL to 1
#define REG_RXDMA_CONTROL_8703B		0x0286 // Control the RX DMA.
#define REG_RXPKT_NUM_8703B			0x0287 // The number of packets in RXPKTBUF.	
#define REG_RXDMA_STATUS_8703B			0x0288
#define REG_RXDMA_MODE_CTRL_8703B		0x0290
#define REG_EARLY_MODE_CONTROL_8703B	0x02BC
#define REG_RSVD5_8703B					0x02F0
#define REG_RSVD6_8703B					0x02F4

//-----------------------------------------------------
//
//	0x0300h ~ 0x03FFh	PCIe
//
//-----------------------------------------------------
#define	REG_PCIE_CTRL_REG_8703B		0x0300
#define	REG_INT_MIG_8703B				0x0304	// Interrupt Migration 
#define	REG_BCNQ_DESA_8703B			0x0308	// TX Beacon Descriptor Address
#define	REG_HQ_DESA_8703B				0x0310	// TX High Queue Descriptor Address
#define	REG_MGQ_DESA_8703B			0x0318	// TX Manage Queue Descriptor Address
#define	REG_VOQ_DESA_8703B			0x0320	// TX VO Queue Descriptor Address
#define	REG_VIQ_DESA_8703B				0x0328	// TX VI Queue Descriptor Address
#define	REG_BEQ_DESA_8703B			0x0330	// TX BE Queue Descriptor Address
#define	REG_BKQ_DESA_8703B			0x0338	// TX BK Queue Descriptor Address
#define	REG_RX_DESA_8703B				0x0340	// RX Queue	Descriptor Address
#define	REG_DBI_WDATA_8703B			0x0348	// DBI Write Data
#define	REG_DBI_RDATA_8703B			0x034C	// DBI Read Data
#define	REG_DBI_ADDR_8703B				0x0350	// DBI Address
#define	REG_DBI_FLAG_8703B				0x0352	// DBI Read/Write Flag
#define	REG_MDIO_WDATA_8703B		0x0354	// MDIO for Write PCIE PHY
#define	REG_MDIO_RDATA_8703B			0x0356	// MDIO for Reads PCIE PHY
#define	REG_MDIO_CTL_8703B			0x0358	// MDIO for Control
#define	REG_DBG_SEL_8703B				0x0360	// Debug Selection Register
#define	REG_PCIE_HRPWM_8703B			0x0361	//PCIe RPWM
#define	REG_PCIE_HCPWM_8703B			0x0363	//PCIe CPWM
#define	REG_PCIE_MULTIFET_CTRL_8703B	0x036A	//PCIE Multi-Fethc Control

//-----------------------------------------------------
//
//	0x0400h ~ 0x047Fh	Protocol Configuration
//
//-----------------------------------------------------
#define REG_VOQ_INFORMATION_8703B		0x0400
#define REG_VIQ_INFORMATION_8703B		0x0404
#define REG_BEQ_INFORMATION_8703B		0x0408
#define REG_BKQ_INFORMATION_8703B		0x040C
#define REG_MGQ_INFORMATION_8703B		0x0410
#define REG_HGQ_INFORMATION_8703B		0x0414
#define REG_BCNQ_INFORMATION_8703B	0x0418
#define REG_TXPKT_EMPTY_8703B			0x041A

#define REG_FWHW_TXQ_CTRL_8703B		0x0420
#define REG_HWSEQ_CTRL_8703B			0x0423
#define REG_TXPKTBUF_BCNQ_BDNY_8703B	0x0424
#define REG_TXPKTBUF_MGQ_BDNY_8703B	0x0425
#define REG_LIFECTRL_CTRL_8703B			0x0426
#define REG_MULTI_BCNQ_OFFSET_8703B	0x0427
#define REG_SPEC_SIFS_8703B				0x0428
#define REG_RL_8703B						0x042A
#define REG_TXBF_CTRL_8703B				0x042C
#define REG_DARFRC_8703B				0x0430
#define REG_RARFRC_8703B				0x0438
#define REG_RRSR_8703B					0x0440
#define REG_ARFR0_8703B					0x0444
#define REG_ARFR1_8703B					0x044C
#define REG_CCK_CHECK_8703B				0x0454
#define REG_AMPDU_MAX_TIME_8703B		0x0456
#define REG_TXPKTBUF_BCNQ_BDNY1_8703B	0x0457

#define REG_AMPDU_MAX_LENGTH_8703B	0x0458
#define REG_TXPKTBUF_WMAC_LBK_BF_HD_8703B	0x045D
#define REG_NDPA_OPT_CTRL_8703B		0x045F
#define REG_FAST_EDCA_CTRL_8703B		0x0460
#define REG_RD_RESP_PKT_TH_8703B		0x0463
#define REG_DATA_SC_8703B				0x0483
#ifdef CONFIG_WOWLAN
#define REG_TXPKTBUF_IV_LOW             0x0484
#define REG_TXPKTBUF_IV_HIGH            0x0488
#endif
#define REG_TXRPT_START_OFFSET		0x04AC
#define REG_POWER_STAGE1_8703B		0x04B4
#define REG_POWER_STAGE2_8703B		0x04B8
#define REG_AMPDU_BURST_MODE_8703B	0x04BC
#define REG_PKT_VO_VI_LIFE_TIME_8703B	0x04C0
#define REG_PKT_BE_BK_LIFE_TIME_8703B	0x04C2
#define REG_STBC_SETTING_8703B			0x04C4
#define REG_HT_SINGLE_AMPDU_8703B		0x04C7
#define REG_PROT_MODE_CTRL_8703B		0x04C8
#define REG_MAX_AGGR_NUM_8703B		0x04CA
#define REG_RTS_MAX_AGGR_NUM_8703B	0x04CB
#define REG_BAR_MODE_CTRL_8703B		0x04CC
#define REG_RA_TRY_RATE_AGG_LMT_8703B	0x04CF
#define REG_MACID_PKT_DROP0_8703B		0x04D0
#define REG_MACID_PKT_SLEEP_8703B		0x04D4

//-----------------------------------------------------
//
//	0x0500h ~ 0x05FFh	EDCA Configuration
//
//-----------------------------------------------------
#define REG_EDCA_VO_PARAM_8703B		0x0500
#define REG_EDCA_VI_PARAM_8703B		0x0504
#define REG_EDCA_BE_PARAM_8703B		0x0508
#define REG_EDCA_BK_PARAM_8703B		0x050C
#define REG_BCNTCFG_8703B				0x0510
#define REG_PIFS_8703B					0x0512
#define REG_RDG_PIFS_8703B				0x0513
#define REG_SIFS_CTX_8703B				0x0514
#define REG_SIFS_TRX_8703B				0x0516
#define REG_AGGR_BREAK_TIME_8703B		0x051A
#define REG_SLOT_8703B					0x051B
#define REG_TX_PTCL_CTRL_8703B			0x0520
#define REG_TXPAUSE_8703B				0x0522
#define REG_DIS_TXREQ_CLR_8703B		0x0523
#define REG_RD_CTRL_8703B				0x0524
//
// Format for offset 540h-542h:
//	[3:0]:   TBTT prohibit setup in unit of 32us. The time for HW getting beacon content before TBTT.
//	[7:4]:   Reserved.
//	[19:8]:  TBTT prohibit hold in unit of 32us. The time for HW holding to send the beacon packet.
//	[23:20]: Reserved
// Description:
//	              |
//     |<--Setup--|--Hold------------>|
//	--------------|----------------------
//                |
//               TBTT
// Note: We cannot update beacon content to HW or send any AC packets during the time between Setup and Hold.
// Described by Designer Tim and Bruce, 2011-01-14.
//
#define REG_TBTT_PROHIBIT_8703B			0x0540
#define REG_RD_NAV_NXT_8703B			0x0544
#define REG_NAV_PROT_LEN_8703B			0x0546
#define REG_BCN_CTRL_8703B				0x0550
#define REG_BCN_CTRL_1_8703B			0x0551
#define REG_MBID_NUM_8703B				0x0552
#define REG_DUAL_TSF_RST_8703B			0x0553
#define REG_BCN_INTERVAL_8703B			0x0554
#define REG_DRVERLYINT_8703B			0x0558
#define REG_BCNDMATIM_8703B			0x0559
#define REG_ATIMWND_8703B				0x055A
#define REG_USTIME_TSF_8703B			0x055C
#define REG_BCN_MAX_ERR_8703B			0x055D
#define REG_RXTSF_OFFSET_CCK_8703B		0x055E
#define REG_RXTSF_OFFSET_OFDM_8703B	0x055F	
#define REG_TSFTR_8703B					0x0560
#define REG_CTWND_8703B					0x0572
#define REG_SECONDARY_CCA_CTRL_8703B	0x0577
#define REG_PSTIMER_8703B				0x0580
#define REG_TIMER0_8703B				0x0584
#define REG_TIMER1_8703B				0x0588
#define REG_ACMHWCTRL_8703B			0x05C0
#define REG_SCH_TXCMD_8703B			0x05F8

//-----------------------------------------------------
//
//	0x0600h ~ 0x07FFh	WMAC Configuration
//
//-----------------------------------------------------
#define REG_MAC_CR_8703B				0x0600
#define REG_TCR_8703B					0x0604
#define REG_RCR_8703B					0x0608
#define REG_RX_PKT_LIMIT_8703B			0x060C
#define REG_RX_DLK_TIME_8703B			0x060D
#define REG_RX_DRVINFO_SZ_8703B		0x060F

#define REG_MACID_8703B					0x0610
#define REG_BSSID_8703B					0x0618
#define REG_MAR_8703B					0x0620
#define REG_MBIDCAMCFG_8703B			0x0628
#define REG_WOWLAN_GTK_DBG1	0x630
#define REG_WOWLAN_GTK_DBG2	0x634

#define REG_USTIME_EDCA_8703B			0x0638
#define REG_MAC_SPEC_SIFS_8703B		0x063A
#define REG_RESP_SIFP_CCK_8703B			0x063C
#define REG_RESP_SIFS_OFDM_8703B		0x063E
#define REG_ACKTO_8703B					0x0640
#define REG_CTS2TO_8703B				0x0641
#define REG_EIFS_8703B					0x0642

#define REG_NAV_UPPER_8703B			0x0652	// unit of 128
#define REG_TRXPTCL_CTL_8703B			0x0668

// Security
#define REG_CAMCMD_8703B				0x0670
#define REG_CAMWRITE_8703B				0x0674
#define REG_CAMREAD_8703B				0x0678
#define REG_CAMDBG_8703B				0x067C
#define REG_SECCFG_8703B				0x0680

// Power
#define REG_WOW_CTRL_8703B				0x0690
#define REG_PS_RX_INFO_8703B			0x0692
#define REG_UAPSD_TID_8703B				0x0693
#define REG_WKFMCAM_CMD_8703B			0x0698
#define REG_WKFMCAM_NUM_8703B			0x0698
#define REG_WKFMCAM_RWD_8703B			0x069C
#define REG_RXFLTMAP0_8703B				0x06A0
#define REG_RXFLTMAP1_8703B				0x06A2
#define REG_RXFLTMAP2_8703B				0x06A4
#define REG_BCN_PSR_RPT_8703B			0x06A8
#define REG_BT_COEX_TABLE_8703B		0x06C0
#define REG_BFMER0_INFO_8703B			0x06E4
#define REG_BFMER1_INFO_8703B			0x06EC
#define REG_CSI_RPT_PARAM_BW20_8703B	0x06F4
#define REG_CSI_RPT_PARAM_BW40_8703B	0x06F8
#define REG_CSI_RPT_PARAM_BW80_8703B	0x06FC

// Hardware Port 2
#define REG_MACID1_8703B				0x0700
#define REG_BSSID1_8703B				0x0708
#define REG_BFMEE_SEL_8703B				0x0714
#define REG_SND_PTCL_CTRL_8703B		0x0718

// LTE_COEX
#define REG_LTECOEX_CTRL			0x07C0
#define REG_LTECOEX_WRITE_DATA		0x07C4	
#define REG_LTECOEX_READ_DATA		0x07C8
#define REG_LTECOEX_PATH_CONTROL	0x70

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
#define SDIO_REG_HCPWM1_8703B	0x025 // HCI Current Power Mode 1


//============================================================================
//	8703 Regsiter Bit and Content definition
//============================================================================

#define BIT_USB_RXDMA_AGG_EN	BIT(31)
#define RXDMA_AGG_MODE_EN		BIT(1)

#ifdef CONFIG_WOWLAN
#define RXPKT_RELEASE_POLL		BIT(16)
#define RXDMA_IDLE				BIT(17)
#define RW_RELEASE_EN			BIT(18)
#endif

//2 HSISR
// interrupt mask which needs to clear
#define MASK_HSISR_CLEAR		(HSISR_GPIO12_0_INT |\
								HSISR_SPS_OCP_INT |\
								HSISR_RON_INT |\
								HSISR_PDNINT |\
								HSISR_GPIO9_INT)


//----------------------------------------------------------------------------
//       8703B REG_CCK_CHECK						(offset 0x454)
//----------------------------------------------------------------------------
#define BIT_BCN_PORT_SEL		BIT5

#ifdef CONFIG_RF_GAIN_OFFSET

#ifdef CONFIG_RTL8703B
#define EEPROM_RF_GAIN_OFFSET			0xC1
#endif

#define EEPROM_RF_GAIN_VAL				0x1F6
#endif //CONFIG_RF_GAIN_OFFSET


//----------------------------------------------------------------------------
//       8195 IMR/ISR bits						(offset 0xB0,  8bits)
//----------------------------------------------------------------------------
#define	IMR_DISABLED_8703B					0
// IMR DW0(0x00B0-00B3) Bit 0-31
#define	IMR_TIMER2_8703B					BIT31		// Timeout interrupt 2
#define	IMR_TIMER1_8703B					BIT30		// Timeout interrupt 1	
#define	IMR_PSTIMEOUT_8703B				BIT29		// Power Save Time Out Interrupt
#define	IMR_GTINT4_8703B					BIT28		// When GTIMER4 expires, this bit is set to 1	
#define	IMR_GTINT3_8703B					BIT27		// When GTIMER3 expires, this bit is set to 1	
#define	IMR_TXBCN0ERR_8703B				BIT26		// Transmit Beacon0 Error			
#define	IMR_TXBCN0OK_8703B				BIT25		// Transmit Beacon0 OK			
#define	IMR_TSF_BIT32_TOGGLE_8703B		BIT24		// TSF Timer BIT32 toggle indication interrupt			
#define	IMR_BCNDMAINT0_8703B				BIT20		// Beacon DMA Interrupt 0			
#define	IMR_BCNDERR0_8703B				BIT16		// Beacon Queue DMA OK0			
#define	IMR_HSISR_IND_ON_INT_8703B		BIT15		// HSISR Indicator (HSIMR & HSISR is true, this bit is set to 1)
#define	IMR_BCNDMAINT_E_8703B			BIT14		// Beacon DMA Interrupt Extension for Win7			
#define	IMR_ATIMEND_8703B				BIT12		// CTWidnow End or ATIM Window End
#define	IMR_C2HCMD_8703B					BIT10		// CPU to Host Command INT Status, Write 1 clear	
#define	IMR_CPWM2_8703B					BIT9			// CPU power Mode exchange INT Status, Write 1 clear	
#define	IMR_CPWM_8703B					BIT8			// CPU power Mode exchange INT Status, Write 1 clear	
#define	IMR_HIGHDOK_8703B				BIT7			// High Queue DMA OK	
#define	IMR_MGNTDOK_8703B				BIT6			// Management Queue DMA OK	
#define	IMR_BKDOK_8703B					BIT5			// AC_BK DMA OK		
#define	IMR_BEDOK_8703B					BIT4			// AC_BE DMA OK	
#define	IMR_VIDOK_8703B					BIT3			// AC_VI DMA OK		
#define	IMR_VODOK_8703B					BIT2			// AC_VO DMA OK	
#define	IMR_RDU_8703B					BIT1			// Rx Descriptor Unavailable	
#define	IMR_ROK_8703B					BIT0			// Receive DMA OK

// IMR DW1(0x00B4-00B7) Bit 0-31
#define	IMR_BCNDMAINT7_8703B				BIT27		// Beacon DMA Interrupt 7
#define	IMR_BCNDMAINT6_8703B				BIT26		// Beacon DMA Interrupt 6
#define	IMR_BCNDMAINT5_8703B				BIT25		// Beacon DMA Interrupt 5
#define	IMR_BCNDMAINT4_8703B				BIT24		// Beacon DMA Interrupt 4
#define	IMR_BCNDMAINT3_8703B				BIT23		// Beacon DMA Interrupt 3
#define	IMR_BCNDMAINT2_8703B				BIT22		// Beacon DMA Interrupt 2
#define	IMR_BCNDMAINT1_8703B				BIT21		// Beacon DMA Interrupt 1
#define	IMR_BCNDOK7_8703B					BIT20		// Beacon Queue DMA OK Interrup 7
#define	IMR_BCNDOK6_8703B					BIT19		// Beacon Queue DMA OK Interrup 6
#define	IMR_BCNDOK5_8703B					BIT18		// Beacon Queue DMA OK Interrup 5
#define	IMR_BCNDOK4_8703B					BIT17		// Beacon Queue DMA OK Interrup 4
#define	IMR_BCNDOK3_8703B					BIT16		// Beacon Queue DMA OK Interrup 3
#define	IMR_BCNDOK2_8703B					BIT15		// Beacon Queue DMA OK Interrup 2
#define	IMR_BCNDOK1_8703B					BIT14		// Beacon Queue DMA OK Interrup 1
#define	IMR_ATIMEND_E_8703B				BIT13		// ATIM Window End Extension for Win7
#define	IMR_TXERR_8703B					BIT11		// Tx Error Flag Interrupt Status, write 1 clear.
#define	IMR_RXERR_8703B					BIT10		// Rx Error Flag INT Status, Write 1 clear
#define	IMR_TXFOVW_8703B					BIT9			// Transmit FIFO Overflow
#define	IMR_RXFOVW_8703B					BIT8			// Receive FIFO Overflow

#ifdef CONFIG_PCI_HCI
//#define IMR_RX_MASK		(IMR_ROK_8703B|IMR_RDU_8703B|IMR_RXFOVW_8703B)
#define IMR_TX_MASK			(IMR_VODOK_8703B|IMR_VIDOK_8703B|IMR_BEDOK_8703B|IMR_BKDOK_8703B|IMR_MGNTDOK_8703B|IMR_HIGHDOK_8703B)

#define RT_BCN_INT_MASKS	(IMR_BCNDMAINT0_8703B | IMR_TXBCN0OK_8703B | IMR_TXBCN0ERR_8703B | IMR_BCNDERR0_8703B)

#define RT_AC_INT_MASKS	(IMR_VIDOK_8703B | IMR_VODOK_8703B | IMR_BEDOK_8703B|IMR_BKDOK_8703B)
#endif

//========================================================
// General definitions
//========================================================

#define MACID_NUM_8703B 16
#define SEC_CAM_ENT_NUM_8703B 16
#define NSS_NUM_8703B 1
#define BAND_CAP_8703B (BAND_CAP_2G)
#define BW_CAP_8703B (BW_CAP_20M | BW_CAP_40M)
#define PROTO_CAP_8703B (PROTO_CAP_11B|PROTO_CAP_11G|PROTO_CAP_11N)

#endif /* __RTL8703B_SPEC_H__ */

