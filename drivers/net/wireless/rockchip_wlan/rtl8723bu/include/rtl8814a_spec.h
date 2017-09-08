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
#ifndef __RTL8814A_SPEC_H__
#define __RTL8814A_SPEC_H__

#include <drv_conf.h>


//============================================================
//
//============================================================

//-----------------------------------------------------
//
//	0x0000h ~ 0x00FFh	System Configuration
//
//-----------------------------------------------------
#define REG_SYS_ISO_CTRL_8814A			0x0000	// 2 Byte
#define REG_SYS_FUNC_EN_8814A			0x0002	// 2 Byte
#define REG_SYS_PW_CTRL_8814A			0x0004	// 4 Byte        
#define REG_SYS_CLKR_8814A				0x0008	// 2 Byte
#define REG_SYS_EEPROM_CTRL_8814A		0x000A	// 2 Byte        
#define REG_EE_VPD_8814A				0x000C	// 2 Byte
#define REG_SYS_SWR_CTRL1_8814A			0x0010	// 1 Byte
#define REG_SPS0_CTRL_8814A				0x0011	// 7 Byte
#define REG_SYS_SWR_CTRL3_8814A			0x0018	// 4 Byte
#define REG_RSV_CTRL_8814A				0x001C	// 3 Byte
#define REG_RF_CTRL0_8814A				0x001F	// 1 Byte
#define REG_RF_CTRL1_8814A				0x0020	// 1 Byte
#define REG_RF_CTRL2_8814A				0x0021	// 1 Byte
#define REG_LPLDO_CTRL_8814A			0x0023	// 1 Byte
#define REG_AFE_CTRL1_8814A				0x0024	// 4 Byte        
#define REG_AFE_CTRL2_8814A				0x0028	// 4 Byte        
#define REG_AFE_CTRL3_8814A				0x002c 	// 4 Byte 
#define REG_EFUSE_CTRL_8814A			0x0030
#define REG_LDO_EFUSE_CTRL_8814A		0x0034 
#define REG_PWR_DATA_8814A				0x0038
#define REG_CAL_TIMER_8814A				0x003C
#define REG_ACLK_MON_8814A				0x003E
#define REG_GPIO_MUXCFG_8814A			0x0040
#define REG_GPIO_IO_SEL_8814A			0x0042
#define REG_MAC_PINMUX_CFG_8814A		0x0043
#define REG_GPIO_PIN_CTRL_8814A			0x0044
#define REG_GPIO_INTM_8814A				0x0048
#define REG_LEDCFG0_8814A				0x004C
#define REG_LEDCFG1_8814A				0x004D
#define REG_LEDCFG2_8814A				0x004E
#define REG_LEDCFG3_8814A				0x004F
#define REG_FSIMR_8814A					0x0050
#define REG_FSISR_8814A					0x0054
#define REG_HSIMR_8814A					0x0058
#define REG_HSISR_8814A					0x005c
#define REG_GPIO_EXT_CTRL_8814A			0x0060
#define REG_GPIO_STATUS_8814A			0x006C
#define REG_SDIO_CTRL_8814A				0x0070
#define REG_HCI_OPT_CTRL_8814A			0x0074
#define REG_RF_CTRL3_8814A				0x0076	// 1 Byte
#define REG_AFE_CTRL4_8814A				0x0078 
#define REG_8051FW_CTRL_8814A			0x0080 
#define REG_HIMR0_8814A					0x00B0
#define REG_HISR0_8814A					0x00B4
#define REG_HIMR1_8814A					0x00B8
#define REG_HISR1_8814A					0x00BC
#define REG_SYS_CFG1_8814A				0x00F0
#define REG_SYS_CFG2_8814A				0x00FC
#define REG_SYS_CFG3_8814A				0x1000

//-----------------------------------------------------
//
//	0x0100h ~ 0x01FFh	MACTOP General Configuration
//
//-----------------------------------------------------
#define REG_CR_8814A						0x0100
#define REG_PBP_8814A					0x0104
#define REG_PKT_BUFF_ACCESS_CTRL_8814A	0x0106
#define REG_TRXDMA_CTRL_8814A			0x010C
#define REG_TRXFF_BNDY_8814A			0x0114
#define REG_TRXFF_STATUS_8814A			0x0118
#define REG_RXFF_PTR_8814A				0x011C
#define REG_CPWM_8814A					0x012F
#define REG_FWIMR_8814A					0x0130
#define REG_FWISR_8814A					0x0134
#define REG_FTIMR_8814A					0x0138
#define REG_PKTBUF_DBG_CTRL_8814A		0x0140
#define REG_RXPKTBUF_CTRL_8814A		0x0142
#define REG_PKTBUF_DBG_DATA_L_8814A	0x0144
#define REG_PKTBUF_DBG_DATA_H_8814A	0x0148

#define REG_TC0_CTRL_8814A				0x0150
#define REG_TC1_CTRL_8814A				0x0154
#define REG_TC2_CTRL_8814A				0x0158
#define REG_TC3_CTRL_8814A				0x015C
#define REG_TC4_CTRL_8814A				0x0160
#define REG_TCUNIT_BASE_8814A			0x0164
#define REG_RSVD3_8814A					0x0168
#define REG_C2HEVT_MSG_NORMAL_8814A	0x01A0
#define REG_C2HEVT_CLEAR_8814A			0x01AF
#define REG_MCUTST_1_8814A				0x01C0
#define REG_MCUTST_WOWLAN_8814A		0x01C7
#define REG_FMETHR_8814A				0x01C8
#define REG_HMETFR_8814A				0x01CC
#define REG_HMEBOX_0_8814A				0x01D0
#define REG_HMEBOX_1_8814A				0x01D4
#define REG_HMEBOX_2_8814A				0x01D8
#define REG_HMEBOX_3_8814A				0x01DC
#define REG_LLT_INIT_8814A				0x01E0
#define REG_LLT_ADDR_8814A				0x01E4 //20130415 KaiYuan add for 8814
#define REG_HMEBOX_EXT0_8814A			0x01F0
#define REG_HMEBOX_EXT1_8814A			0x01F4
#define REG_HMEBOX_EXT2_8814A			0x01F8
#define REG_HMEBOX_EXT3_8814A			0x01FC

//-----------------------------------------------------
//
//	0x0200h ~ 0x027Fh	TXDMA Configuration
//
//-----------------------------------------------------
#define REG_FIFOPAGE_CTRL_1_8814A			0x0200
#define REG_FIFOPAGE_CTRL_2_8814A		0x0204
#define REG_AUTO_LLT_8814A					0x0208
#define REG_TXDMA_OFFSET_CHK_8814A	0x020C
#define REG_TXDMA_STATUS_8814A			0x0210
#define REG_RQPN_NPQ_8814A				0x0214
#define REG_TQPNT1_8814A					0x0218
#define REG_TQPNT2_8814A					0x021C
#define REG_TQPNT3_8814A					0x0220
#define REG_TQPNT4_8814A					0x0224
#define REG_RQPN_CTRL_1_8814A				0x0228
#define REG_RQPN_CTRL_2_8814A				0x022C
#define REG_FIFOPAGE_INFO_1_8814A			0x0230
#define REG_FIFOPAGE_INFO_2_8814A			0x0234
#define REG_FIFOPAGE_INFO_3_8814A			0x0238
#define REG_FIFOPAGE_INFO_4_8814A			0x023C
#define REG_FIFOPAGE_INFO_5_8814A			0x0240


//-----------------------------------------------------
//
//	0x0280h ~ 0x02FFh	RXDMA Configuration
//
//-----------------------------------------------------
#define REG_RXDMA_AGG_PG_TH_8814A		0x0280
#define REG_RXPKT_NUM_8814A				0x0284 // The number of packets in RXPKTBUF.
#define REG_RXDMA_CONTROL_8814A			0x0286 // ?????? Control the RX DMA.
#define REG_RXDMA_STATUS_8814A			0x0288
#define REG_RXDMA_MODE_8814A				0x0290 // ??????
#define REG_EARLY_MODE_CONTROL_8814A	0x02BC // ??????
#define REG_RSVD5_8814A					0x02F0 // ??????


//-----------------------------------------------------
//
//	0x0300h ~ 0x03FFh	PCIe
//
//-----------------------------------------------------
#define	REG_PCIE_CTRL_REG_8814A			0x0300
#define	REG_INT_MIG_8814A				0x0304	// Interrupt Migration 
#define	REG_BCNQ_TXBD_DESA_8814A		0x0308	// TX Beacon Descriptor Address
#define	REG_MGQ_TXBD_DESA_8814A			0x0310	// TX Manage Queue Descriptor Address
#define	REG_VOQ_TXBD_DESA_8814A			0x0318	// TX VO Queue Descriptor Address
#define	REG_VIQ_TXBD_DESA_8814A			0x0320	// TX VI Queue Descriptor Address
#define	REG_BEQ_TXBD_DESA_8814A			0x0328	// TX BE Queue Descriptor Address
#define	REG_BKQ_TXBD_DESA_8814A			0x0330	// TX BK Queue Descriptor Address
#define	REG_RXQ_RXBD_DESA_8814A			0x0338	// RX Queue	Descriptor Address
#define REG_HI0Q_TXBD_DESA_8814A		0x0340
#define REG_HI1Q_TXBD_DESA_8814A		0x0348
#define REG_HI2Q_TXBD_DESA_8814A		0x0350
#define REG_HI3Q_TXBD_DESA_8814A		0x0358
#define REG_HI4Q_TXBD_DESA_8814A		0x0360
#define REG_HI5Q_TXBD_DESA_8814A		0x0368
#define REG_HI6Q_TXBD_DESA_8814A		0x0370
#define REG_HI7Q_TXBD_DESA_8814A		0x0378
#define	REG_MGQ_TXBD_NUM_8814A			0x0380
#define	REG_RX_RXBD_NUM_8814A			0x0382
#define	REG_VOQ_TXBD_NUM_8814A			0x0384
#define	REG_VIQ_TXBD_NUM_8814A			0x0386
#define	REG_BEQ_TXBD_NUM_8814A			0x0388
#define	REG_BKQ_TXBD_NUM_8814A			0x038A
#define	REG_HI0Q_TXBD_NUM_8814A			0x038C
#define	REG_HI1Q_TXBD_NUM_8814A			0x038E
#define	REG_HI2Q_TXBD_NUM_8814A			0x0390
#define	REG_HI3Q_TXBD_NUM_8814A			0x0392
#define	REG_HI4Q_TXBD_NUM_8814A			0x0394
#define	REG_HI5Q_TXBD_NUM_8814A			0x0396
#define	REG_HI6Q_TXBD_NUM_8814A			0x0398
#define	REG_HI7Q_TXBD_NUM_8814A			0x039A
#define	REG_TSFTIMER_HCI_8814A			0x039C

//Read Write Point
#define	REG_VOQ_TXBD_IDX_8814A			0x03A0
#define	REG_VIQ_TXBD_IDX_8814A			0x03A4
#define	REG_BEQ_TXBD_IDX_8814A			0x03A8
#define	REG_BKQ_TXBD_IDX_8814A			0x03AC
#define	REG_MGQ_TXBD_IDX_8814A			0x03B0
#define	REG_RXQ_TXBD_IDX_8814A			0x03B4
#define	REG_HI0Q_TXBD_IDX_8814A			0x03B8
#define	REG_HI1Q_TXBD_IDX_8814A			0x03BC
#define	REG_HI2Q_TXBD_IDX_8814A			0x03C0
#define	REG_HI3Q_TXBD_IDX_8814A			0x03C4
#define	REG_HI4Q_TXBD_IDX_8814A			0x03C8
#define	REG_HI5Q_TXBD_IDX_8814A			0x03CC
#define	REG_HI6Q_TXBD_IDX_8814A			0x03D0
#define	REG_HI7Q_TXBD_IDX_8814A			0x03D4
#define REG_DBG_SEL_V1_8814A				0x03D8
#define REG_PCIE_HRPWM1_V1_8814A			0x03D9
#define REG_PCIE_HCPWM1_V1_8814A			0x03DA
#define REG_PCIE_CTRL2_8814A				0x03DB
#define REG_PCIE_HRPWM2_V1_8814A			0x03DC
#define REG_PCIE_HCPWM2_V1_8814A			0x03DE
#define REG_PCIE_H2C_MSG_V1_8814A		0x03E0
#define REG_PCIE_C2H_MSG_V1_8814A		0x03E4
#define REG_DBI_WDATA_V1_8814A			0x03E8
#define REG_DBI_RDATA_V1_8814A			0x03EC
#define REG_DBI_FLAG_V1_8814A				0x03F0
#define REG_MDIO_V1_8814A					0x03F4
#define REG_PCIE_MIX_CFG_8814A			0x03F8
#define REG_DBG_8814A						0x03FC
//-----------------------------------------------------
//
//	0x0400h ~ 0x047Fh	Protocol Configuration
//
//-----------------------------------------------------
#define REG_VOQ_INFORMATION_8814A		0x0400
#define REG_VIQ_INFORMATION_8814A		0x0404
#define REG_BEQ_INFORMATION_8814A		0x0408
#define REG_BKQ_INFORMATION_8814A		0x040C
#define REG_MGQ_INFORMATION_8814A		0x0410
#define REG_HGQ_INFORMATION_8814A		0x0414
#define REG_BCNQ_INFORMATION_8814A	0x0418
#define REG_TXPKT_EMPTY_8814A			0x041A
#define REG_CPU_MGQ_INFORMATION_8814A	0x041C
#define REG_FWHW_TXQ_CTRL_8814A		0x0420
#define REG_HWSEQ_CTRL_8814A			0x0423
#define REG_TXPKTBUF_BCNQ_BDNY_8814A	0x0424
//#define REG_MGQ_BDNY_8814A				0x0425
#define REG_LIFETIME_EN_8814A				0x0426
//#define REG_FW_FREE_TAIL_8814A			0x0427
#define REG_SPEC_SIFS_8814A				0x0428
#define REG_RETRY_LIMIT_8814A				0x042A
#define REG_TXBF_CTRL_8814A				0x042C
#define REG_DARFRC_8814A				0x0430
#define REG_RARFRC_8814A				0x0438
#define REG_RRSR_8814A					0x0440
#define REG_ARFR0_8814A					0x0444
#define REG_ARFR1_8814A					0x044C
#define REG_CCK_CHECK_8814A				0x0454
#define REG_AMPDU_MAX_TIME_8814A			0x0455
#define REG_TXPKTBUF_BCNQ1_BDNY_8814A	0x0456
#define REG_AMPDU_MAX_LENGTH_8814A	0x0458
#define REG_ACQ_STOP_8814A				0x045C
#define REG_NDPA_RATE_8814A				0x045D
#define REG_TX_HANG_CTRL_8814A			0x045E
#define REG_NDPA_OPT_CTRL_8814A		0x045F
#define REG_FAST_EDCA_CTRL_8814A		0x0460
#define REG_RD_RESP_PKT_TH_8814A		0x0463
#define REG_CMDQ_INFO_8814A 				0x0464
#define REG_Q4_INFO_8814A 					0x0468
#define REG_Q5_INFO_8814A 					0x046C
#define REG_Q6_INFO_8814A 					0x0470
#define REG_Q7_INFO_8814A 					0x0474
#define REG_WMAC_LBK_BUF_HD_8814A		0x0478
#define REG_MGQ_PGBNDY_8814A 				0x047A
#define REG_INIRTS_RATE_SEL_8814A 			0x0480
#define REG_BASIC_CFEND_RATE_8814A 		0x0481
#define REG_STBC_CFEND_RATE_8814A 		0x0482
#define REG_DATA_SC_8814A					0x0483
#define REG_MACID_SLEEP3_8814A 			0x0484
#define REG_MACID_SLEEP1_8814A 			0x0488
#define REG_ARFR2_8814A 					0x048C
#define REG_ARFR3_8814A 					0x0494
#define REG_ARFR4_8814A 					0x049C
#define REG_ARFR5_8814A 					0x04A4
#define REG_TXRPT_START_OFFSET_8814A		0x04AC
#define REG_TRYING_CNT_TH_8814A 			0x04B0
#define REG_POWER_STAGE1_8814A		0x04B4
#define REG_POWER_STAGE2_8814A		0x04B8
#define REG_SW_AMPDU_BURST_MODE_CTRL_8814A	0x04BC
#define REG_PKT_LIFE_TIME_8814A			0x04C0
#define REG_PKT_BE_BK_LIFE_TIME_8814A		0x04C2 // ??????
#define REG_STBC_SETTING_8814A			0x04C4
#define REG_STBC_8814A						0x04C5
#define REG_QUEUE_CTRL_8814A 				0x04C6
#define REG_SINGLE_AMPDU_CTRL_8814A 		0x04C7
#define REG_PROT_MODE_CTRL_8814A		0x04C8
#define REG_MAX_AGGR_NUM_8814A		0x04CA
#define REG_RTS_MAX_AGGR_NUM_8814A	0x04CB
#define REG_BAR_MODE_CTRL_8814A		0x04CC
#define REG_RA_TRY_RATE_AGG_LMT_8814A	0x04CF
#define REG_MACID_SLEEP2_8814A			0x04D0
#define REG_MACID_SLEEP0_8814A			0x04D4
#define REG_HW_SEQ0_8814A 				0x04D8
#define REG_HW_SEQ1_8814A 				0x04DA
#define REG_HW_SEQ2_8814A 				0x04DC
#define REG_HW_SEQ3_8814A 				0x04DE
#define REG_NULL_PKT_STATUS_8814A 			0x04E0
#define REG_PTCL_ERR_STATUS_8814A 			0x04E2
#define REG_DROP_PKT_NUM_8814A 			0x04EC
#define REG_PTCL_TX_RPT_8814A 				0x04F0
#define REG_Dummy_8814A 					0x04FC


//-----------------------------------------------------
//
//	0x0500h ~ 0x05FFh	EDCA Configuration
//
//-----------------------------------------------------
#define REG_EDCA_VO_PARAM_8814A			0x0500
#define REG_EDCA_VI_PARAM_8814A			0x0504
#define REG_EDCA_BE_PARAM_8814A			0x0508
#define REG_EDCA_BK_PARAM_8814A			0x050C
#define REG_BCNTCFG_8814A					0x0510
#define REG_PIFS_8814A						0x0512
#define REG_RDG_PIFS_8814A					0x0513
#define REG_SIFS_CTX_8814A					0x0514
#define REG_SIFS_TRX_8814A					0x0516
#define REG_AGGR_BREAK_TIME_8814A			0x051A
#define REG_SLOT_8814A						0x051B
#define REG_TX_PTCL_CTRL_8814A				0x0520
#define REG_TXPAUSE_8814A					0x0522
#define REG_DIS_TXREQ_CLR_8814A			0x0523
#define REG_RD_CTRL_8814A					0x0524
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
#define REG_TBTT_PROHIBIT_8814A			0x0540
#define REG_RD_NAV_NXT_8814A				0x0544
#define REG_NAV_PROT_LEN_8814A			0x0546
#define REG_BCN_CTRL_8814A					0x0550
#define REG_BCN_CTRL_1_8814A				0x0551
#define REG_MBID_NUM_8814A				0x0552
#define REG_DUAL_TSF_RST_8814A				0x0553
#define REG_MBSSID_BCN_SPACE_8814A		0x0554
#define REG_DRVERLYINT_8814A				0x0558
#define REG_BCNDMATIM_8814A				0x0559
#define REG_ATIMWND_8814A					0x055A
#define REG_USTIME_TSF_8814A				0x055C
#define REG_BCN_MAX_ERR_8814A				0x055D
#define REG_RXTSF_OFFSET_CCK_8814A		0x055E
#define REG_RXTSF_OFFSET_OFDM_8814A		0x055F	
#define REG_TSFTR_8814A						0x0560
#define REG_CTWND_8814A					0x0572
#define REG_SECONDARY_CCA_CTRL_8814A		0x0577 // ??????
#define REG_PSTIMER_8814A					0x0580
#define REG_TIMER0_8814A					0x0584
#define REG_TIMER1_8814A					0x0588
#define REG_BCN_PREDL_ITV_8814A			0x058F	//Pre download beacon interval
#define REG_ACMHWCTRL_8814A				0x05C0

//-----------------------------------------------------
//
//	0x0600h ~ 0x07FFh	WMAC Configuration
//
//-----------------------------------------------------
#define REG_MAC_CR_8814A					0x0600
#define REG_TCR_8814A						0x0604
#define REG_RCR_8814A						0x0608
#define REG_RX_PKT_LIMIT_8814A				0x060C
#define REG_RX_DLK_TIME_8814A				0x060D
#define REG_RX_DRVINFO_SZ_8814A			0x060F

#define REG_MACID_8814A					0x0610
#define REG_BSSID_8814A						0x0618
#define REG_MAR_8814A						0x0620
#define REG_MBIDCAMCFG_8814A				0x0628

#define REG_USTIME_EDCA_8814A				0x0638
#define REG_MAC_SPEC_SIFS_8814A			0x063A
#define REG_RESP_SIFP_CCK_8814A			0x063C
#define REG_RESP_SIFS_OFDM_8814A			0x063E
#define REG_ACKTO_8814A					0x0640
#define REG_CTS2TO_8814A					0x0641
#define REG_EIFS_8814A						0x0642

#define	REG_NAV_UPPER_8814A				0x0652	// unit of 128
#define REG_TRXPTCL_CTL_8814A				0x0668

// Security
#define REG_CAMCMD_8814A					0x0670
#define REG_CAMWRITE_8814A				0x0674
#define REG_CAMREAD_8814A					0x0678
#define REG_CAMDBG_8814A					0x067C
#define REG_SECCFG_8814A					0x0680

// Power
#define REG_WOW_CTRL_8814A				0x0690
#define REG_PS_RX_INFO_8814A				0x0692
#define REG_UAPSD_TID_8814A				0x0693
#define REG_WKFMCAM_NUM_8814A			0x0698
#define REG_RXFLTMAP0_8814A				0x06A0
#define REG_RXFLTMAP1_8814A				0x06A2
#define REG_RXFLTMAP2_8814A				0x06A4
#define REG_BCN_PSR_RPT_8814A				0x06A8
#define REG_BT_COEX_TABLE_8814A			0x06C0
#define REG_TX_DATA_RSP_RATE_8814A		0x06DE
#define REG_ASSOCIATED_BFMER0_INFO_8814A	0x06E4
#define REG_ASSOCIATED_BFMER1_INFO_8814A	0x06EC
#define REG_CSI_RPT_PARAM_BW20_8814A		0x06F4
#define REG_CSI_RPT_PARAM_BW40_8814A		0x06F8
#define REG_CSI_RPT_PARAM_BW80_8814A		0x06FC

// Hardware Port 2
#define REG_MACID1_8814A					0x0700
#define REG_BSSID1_8814A					0x0708
// Hardware Port 3
#define REG_MACID2_8814A					0x1620
#define REG_BSSID2_8814A					0x1628
// Hardware Port 4
#define REG_MACID3_8814A					0x1630
#define REG_BSSID3_8814A					0x1638
// Hardware Port 5
#define REG_MACID4_8814A					0x1640
#define REG_BSSID4_8814A					0x1648

#define REG_ASSOCIATED_BFMEE_SEL_8814A	0x0714
#define REG_SND_PTCL_CTRL_8814A			0x0718
#define REG_IQ_DUMP_8814A					0x07C0

/**** page 19 ****/
//TX BeamForming
#define	REG_BB_TXBF_ANT_SET_BF1				0x19ac
#define	REG_BB_TXBF_ANT_SET_BF0				0x19b4

//	0x1200h ~ 0x12FFh	DDMA CTRL
//
//-----------------------------------------------------
#define REG_DDMA_CH0SA                   0x1200
#define REG_DDMA_CH0DA                   0x1204
#define REG_DDMA_CH0CTRL                0x1208
#define REG_DDMA_CH1SA                   0x1210
#define REG_DDMA_CH1DA                	0x1214
#define REG_DDMA_CH1CTRL                0x1218
#define REG_DDMA_CH2SA                   0x1220
#define REG_DDMA_CH2DA                   0x1224
#define REG_DDMA_CH2CTRL                0x1228
#define REG_DDMA_CH3SA                   0x1230
#define REG_DDMA_CH3DA                   0x1234
#define REG_DDMA_CH3CTRL                0x1238
#define REG_DDMA_CH4SA                   0x1240
#define REG_DDMA_CH4DA                   0x1244
#define REG_DDMA_CH4CTRL                0x1248
#define REG_DDMA_CH5SA                   0x1250
#define REG_DDMA_CH5DA                   0x1254
#define REG_DDMA_CH5CTRL                0x1258
#define REG_DDMA_INT_MSK                0x12E0
#define REG_DDMA_CHSTATUS              0x12E8
#define REG_DDMA_CHKSUM                 0x12F0
#define REG_DDMA_MONITER                0x12FC

#define DDMA_LEN_MASK              	 	0x0001FFFF
#define FW_CHKSUM_DUMMY_SZ		8
#define DDMA_CH_CHKSUM_CNT		BIT(24)
#define DDMA_RST_CHKSUM_STS		BIT(25)
#define DDMA_MODE_BLOCK_CPU		BIT(26)
#define DDMA_CHKSUM_FAIL			BIT(27)
#define DDMA_DA_W_DISABLE			BIT(28)
#define DDMA_CHKSUM_EN			BIT(29)
#define DDMA_CH_OWN                 	BIT(31)


//3081 FWDL
#define FWDL_EN                 BIT0
#define IMEM_BOOT_DL_RDY        BIT1
#define IMEM_BOOT_CHKSUM_FAIL   BIT2
#define IMEM_DL_RDY             BIT3
#define IMEM_CHKSUM_OK        BIT4
#define DMEM_DL_RDY             BIT5
#define DMEM_CHKSUM_OK        BIT6
#define EMEM_DL_RDY             BIT7
#define EMEM_CHKSUM_FAIL        BIT8
#define EMEM_TXBUF_DL_RDY       BIT9
#define EMEM_TXBUF_CHKSUM_FAIL  BIT10
#define CPU_CLK_SWITCH_BUSY     BIT11
#define CPU_CLK_SEL             (BIT12|BIT13)
#define FWDL_OK                 BIT14
#define FW_INIT_RDY             BIT15
#define R_EN_BOOT_FLASH         BIT20

#define OCPBASE_IMEM_3081        0x00000000
#define OCPBASE_DMEM_3081        0x00200000 
#define OCPBASE_RPTBUF_3081      0x18660000
#define OCPBASE_RXBUF2_3081      0x18680000
#define OCPBASE_RXBUF_3081       0x18700000
#define OCPBASE_TXBUF_3081       0x18780000


#define REG_FAST_EDCA_VOVI_SETTING_8814A 0x1448
#define REG_FAST_EDCA_BEBK_SETTING_8814A 0x144C


//-----------------------------------------------------
//


//-----------------------------------------------------
//
//	Redifine 8192C register definition for compatibility
//
//-----------------------------------------------------

// TODO: use these definition when using REG_xxx naming rule.
// NOTE: DO NOT Remove these definition. Use later.
#define	EFUSE_CTRL_8814A					REG_EFUSE_CTRL_8814A		// E-Fuse Control.
#define	EFUSE_TEST_8814A					REG_LDO_EFUSE_CTRL_8814A		// E-Fuse Test.
#define	MSR_8814A							(REG_CR_8814A + 2)		// Media Status register
#define	ISR_8814A							REG_HISR0_8814A
#define	TSFR_8814A							REG_TSFTR_8814A			// Timing Sync Function Timer Register.
					
#define PBP_8814A							REG_PBP_8814A

// Redifine MACID register, to compatible prior ICs.
#define	IDR0_8814A							REG_MACID_8814A			// MAC ID Register, Offset 0x0050-0x0053
#define	IDR4_8814A							(REG_MACID_8814A + 4)	// MAC ID Register, Offset 0x0054-0x0055


//
// 9. Security Control Registers	(Offset: )
//
#define	RWCAM_8814A						REG_CAMCMD_8814A		//IN 8190 Data Sheet is called CAMcmd
#define	WCAMI_8814A						REG_CAMWRITE_8814A		// Software write CAM input content
#define	RCAMO_8814A						REG_CAMREAD_8814A		// Software read/write CAM config
#define	CAMDBG_8814A						REG_CAMDBG_8814A
#define	SECR_8814A							REG_SECCFG_8814A		//Security Configuration Register


//----------------------------------------------------------------------------
//       8195 IMR/ISR bits						(offset 0xB0,  8bits)
//----------------------------------------------------------------------------
#define	IMR_DISABLED_8814A					0
// IMR DW0(0x00B0-00B3) Bit 0-31
#define	IMR_TIMER2_8814A					BIT31		// Timeout interrupt 2
#define	IMR_TIMER1_8814A					BIT30		// Timeout interrupt 1	
#define	IMR_PSTIMEOUT_8814A				BIT29		// Power Save Time Out Interrupt
#define	IMR_GTINT4_8814A					BIT28		// When GTIMER4 expires, this bit is set to 1	
#define	IMR_GTINT3_8814A					BIT27		// When GTIMER3 expires, this bit is set to 1	
#define	IMR_TXBCN0ERR_8814A				BIT26		// Transmit Beacon0 Error			
#define	IMR_TXBCN0OK_8814A					BIT25		// Transmit Beacon0 OK			
#define	IMR_TSF_BIT32_TOGGLE_8814A		BIT24		// TSF Timer BIT32 toggle indication interrupt			
#define	IMR_BCNDMAINT0_8814A				BIT20		// Beacon DMA Interrupt 0			
#define	IMR_BCNDERR0_8814A					BIT16		// Beacon Queue DMA OK0			
#define	IMR_HSISR_IND_ON_INT_8814A		BIT15		// HSISR Indicator (HSIMR & HSISR is true, this bit is set to 1)
#define	IMR_BCNDMAINT_E_8814A				BIT14		// Beacon DMA Interrupt Extension for Win7			
#define	IMR_ATIMEND_8814A					BIT12		// CTWidnow End or ATIM Window End
#define	IMR_C2HCMD_8814A					BIT10		// CPU to Host Command INT Status, Write 1 clear	
#define	IMR_CPWM2_8814A					BIT9			// CPU power Mode exchange INT Status, Write 1 clear	
#define	IMR_CPWM_8814A						BIT8			// CPU power Mode exchange INT Status, Write 1 clear	
#define	IMR_HIGHDOK_8814A					BIT7			// High Queue DMA OK	
#define	IMR_MGNTDOK_8814A					BIT6			// Management Queue DMA OK	
#define	IMR_BKDOK_8814A					BIT5			// AC_BK DMA OK		
#define	IMR_BEDOK_8814A					BIT4			// AC_BE DMA OK	
#define	IMR_VIDOK_8814A					BIT3			// AC_VI DMA OK		
#define	IMR_VODOK_8814A					BIT2			// AC_VO DMA OK	
#define	IMR_RDU_8814A						BIT1			// Rx Descriptor Unavailable	
#define	IMR_ROK_8814A						BIT0			// Receive DMA OK

// IMR DW1(0x00B4-00B7) Bit 0-31
#define	IMR_MCUERR_8814A						BIT28		// Beacon DMA Interrupt 7
#define	IMR_BCNDMAINT7_8814A				BIT27		// Beacon DMA Interrupt 7
#define	IMR_BCNDMAINT6_8814A				BIT26		// Beacon DMA Interrupt 6
#define	IMR_BCNDMAINT5_8814A				BIT25		// Beacon DMA Interrupt 5
#define	IMR_BCNDMAINT4_8814A				BIT24		// Beacon DMA Interrupt 4
#define	IMR_BCNDMAINT3_8814A				BIT23		// Beacon DMA Interrupt 3
#define	IMR_BCNDMAINT2_8814A				BIT22		// Beacon DMA Interrupt 2
#define	IMR_BCNDMAINT1_8814A				BIT21		// Beacon DMA Interrupt 1
#define	IMR_BCNDOK7_8814A					BIT20		// Beacon Queue DMA OK Interrup 7
#define	IMR_BCNDOK6_8814A					BIT19		// Beacon Queue DMA OK Interrup 6
#define	IMR_BCNDOK5_8814A					BIT18		// Beacon Queue DMA OK Interrup 5
#define	IMR_BCNDOK4_8814A					BIT17		// Beacon Queue DMA OK Interrup 4
#define	IMR_BCNDOK3_8814A					BIT16		// Beacon Queue DMA OK Interrup 3
#define	IMR_BCNDOK2_8814A					BIT15		// Beacon Queue DMA OK Interrup 2
#define	IMR_BCNDOK1_8814A					BIT14		// Beacon Queue DMA OK Interrup 1
#define	IMR_ATIMEND_E_8814A				BIT13		// ATIM Window End Extension for Win7
#define	IMR_TXERR_8814A					BIT11		// Tx Error Flag Interrupt Status, write 1 clear.
#define	IMR_RXERR_8814A					BIT10		// Rx Error Flag INT Status, Write 1 clear
#define	IMR_TXFOVW_8814A					BIT9			// Transmit FIFO Overflow
#define	IMR_RXFOVW_8814A					BIT8			// Receive FIFO Overflow


#ifdef CONFIG_PCI_HCI
#define IMR_TX_MASK			(IMR_VODOK_8814A | IMR_VIDOK_8814A | IMR_BEDOK_8814A | IMR_BKDOK_8814A | IMR_MGNTDOK_8814A | IMR_HIGHDOK_8814A)

#define RT_BCN_INT_MASKS	(IMR_BCNDMAINT0_8814A | IMR_TXBCN0OK_8814A | IMR_TXBCN0ERR_8814A | IMR_BCNDERR0_8814A)

#define RT_AC_INT_MASKS	(IMR_VIDOK_8814A | IMR_VODOK_8814A | IMR_BEDOK_8814A | IMR_BKDOK_8814A)
#endif


/*===================================================================
=====================================================================
Here the register defines are for 92C. When the define is as same with 92C, 
we will use the 92C's define for the consistency
So the following defines for 92C is not entire!!!!!!
=====================================================================
=====================================================================*/


//-----------------------------------------------------
//
//	0xFE00h ~ 0xFE55h	USB Configuration
//
//-----------------------------------------------------

//2 Special Option
#define USB_AGG_EN_8814A			BIT(7)
#define REG_USB_HRPWM_U3			0xF052

#define LAST_ENTRY_OF_TX_PKT_BUFFER_8814A       2048-1	//20130415 KaiYuan add for 8814

#define MACID_NUM_8814A 128
#define SEC_CAM_ENT_NUM_8814A 64
#define NSS_NUM_8814A 3
#define BAND_CAP_8814A (BAND_CAP_2G | BAND_CAP_5G)
#define BW_CAP_8814A (BW_CAP_20M | BW_CAP_40M | BW_CAP_80M)
#define PROTO_CAP_8814A (PROTO_CAP_11B|PROTO_CAP_11G|PROTO_CAP_11N|PROTO_CAP_11AC)

#endif //__RTL8814A_SPEC_H__
