/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#ifndef __HAL_COMMON_REG_H__
#define __HAL_COMMON_REG_H__


#define MAC_ADDR_LEN				6

#define HAL_NAV_UPPER_UNIT		128		/*  micro-second */

/*  8188E PKT_BUFF_ACCESS_CTRL value */
#define TXPKT_BUF_SELECT				0x69
#define RXPKT_BUF_SELECT				0xA5
#define DISABLE_TRXPKT_BUF_ACCESS		0x0

/*  */
/*  */
/*  */

/*  */
/*  */
/* 	0x0000h ~ 0x00FFh	System Configuration */
/*  */
/*  */
#define REG_SYS_ISO_CTRL				0x0000
#define REG_SYS_FUNC_EN				0x0002
#define REG_APS_FSMCO					0x0004
#define REG_SYS_CLKR					0x0008
#define REG_9346CR						0x000A
#define REG_SYS_EEPROM_CTRL			0x000A
#define REG_EE_VPD						0x000C
#define REG_AFE_MISC					0x0010
#define REG_SPS0_CTRL					0x0011
#define REG_SPS0_CTRL_6					0x0016
#define REG_POWER_OFF_IN_PROCESS		0x0017
#define REG_SPS_OCP_CFG				0x0018
#define REG_RSV_CTRL					0x001C
#define REG_RF_CTRL						0x001F
#define REG_LDOA15_CTRL				0x0020
#define REG_LDOV12D_CTRL				0x0021
#define REG_LDOHCI12_CTRL				0x0022
#define REG_LPLDO_CTRL					0x0023
#define REG_AFE_XTAL_CTRL				0x0024
#define REG_AFE_LDO_CTRL				0x0027 /*  1.5v for 8188EE test chip, 1.4v for MP chip */
#define REG_AFE_PLL_CTRL				0x0028
#define REG_MAC_PHY_CTRL				0x002c /* for 92d, DMDP, SMSP, DMSP contrl */
#define REG_APE_PLL_CTRL_EXT			0x002c
#define REG_EFUSE_CTRL					0x0030
#define REG_EFUSE_TEST					0x0034
#define REG_PWR_DATA					0x0038
#define REG_CAL_TIMER					0x003C
#define REG_ACLK_MON					0x003E
#define REG_GPIO_MUXCFG				0x0040
#define REG_GPIO_IO_SEL					0x0042
#define REG_MAC_PINMUX_CFG			0x0043
#define REG_GPIO_PIN_CTRL				0x0044
#define REG_GPIO_INTM					0x0048
#define REG_LEDCFG0						0x004C
#define REG_LEDCFG1						0x004D
#define REG_LEDCFG2						0x004E
#define REG_LEDCFG3						0x004F
#define REG_FSIMR						0x0050
#define REG_FSISR						0x0054
#define REG_HSIMR						0x0058
#define REG_HSISR						0x005c
#define REG_GPIO_PIN_CTRL_2			0x0060 /*  RTL8723 WIFI/BT/GPS Multi-Function GPIO Pin Control. */
#define REG_GPIO_IO_SEL_2				0x0062 /*  RTL8723 WIFI/BT/GPS Multi-Function GPIO Select. */
#define REG_MULTI_FUNC_CTRL			0x0068 /*  RTL8723 WIFI/BT/GPS Multi-Function control source. */
#define REG_GSSR						0x006c
#define REG_AFE_XTAL_CTRL_EXT			0x0078 /* RTL8188E */
#define REG_XCK_OUT_CTRL				0x007c /* RTL8188E */
#define REG_MCUFWDL					0x0080
#define REG_WOL_EVENT					0x0081 /* RTL8188E */
#define REG_MCUTSTCFG					0x0084
#define REG_FDHM0						0x0088
#define REG_HOST_SUSP_CNT				0x00BC	/*  RTL8192C Host suspend counter on FPGA platform */
#define REG_SYSTEM_ON_CTRL			0x00CC	/*  For 8723AE Reset after S3 */
#define REG_EFUSE_ACCESS				0x00CF	/*  Efuse access protection for RTL8723 */
#define REG_BIST_SCAN					0x00D0
#define REG_BIST_RPT					0x00D4
#define REG_BIST_ROM_RPT				0x00D8
#define REG_USB_SIE_INTF				0x00E0
#define REG_PCIE_MIO_INTF				0x00E4
#define REG_PCIE_MIO_INTD				0x00E8
#define REG_HPON_FSM					0x00EC
#define REG_SYS_CFG						0x00F0
#define REG_GPIO_OUTSTS				0x00F4	/*  For RTL8723 only. */
#define REG_TYPE_ID						0x00FC

/*  */
/*  2010/12/29 MH Add for 92D */
/*  */
#define REG_MAC_PHY_CTRL_NORMAL		0x00f8


/*  */
/*  */
/* 	0x0100h ~ 0x01FFh	MACTOP General Configuration */
/*  */
/*  */
#define REG_CR							0x0100
#define REG_PBP							0x0104
#define REG_PKT_BUFF_ACCESS_CTRL		0x0106
#define REG_TRXDMA_CTRL				0x010C
#define REG_TRXFF_BNDY					0x0114
#define REG_TRXFF_STATUS				0x0118
#define REG_RXFF_PTR					0x011C
#define REG_HIMR						0x0120
#define REG_HISR						0x0124
#define REG_HIMRE						0x0128
#define REG_HISRE						0x012C
#define REG_CPWM						0x012F
#define REG_FWIMR						0x0130
#define REG_FWISR						0x0134
#define REG_FTIMR						0x0138
#define REG_FTISR						0x013C /* RTL8192C */
#define REG_PKTBUF_DBG_CTRL			0x0140
#define REG_RXPKTBUF_CTRL				(REG_PKTBUF_DBG_CTRL+2)
#define REG_PKTBUF_DBG_DATA_L			0x0144
#define REG_PKTBUF_DBG_DATA_H		0x0148

#define REG_TC0_CTRL					0x0150
#define REG_TC1_CTRL					0x0154
#define REG_TC2_CTRL					0x0158
#define REG_TC3_CTRL					0x015C
#define REG_TC4_CTRL					0x0160
#define REG_TCUNIT_BASE				0x0164
#define REG_MBIST_START				0x0174
#define REG_MBIST_DONE					0x0178
#define REG_MBIST_FAIL					0x017C
#define REG_32K_CTRL					0x0194 /* RTL8188E */
#define REG_C2HEVT_MSG_NORMAL		0x01A0
#define REG_C2HEVT_CLEAR				0x01AF
#define REG_MCUTST_1					0x01c0
#define REG_MCUTST_WOWLAN			0x01C7	/*  Defined after 8188E series. */
#define REG_FMETHR						0x01C8
#define REG_HMETFR						0x01CC
#define REG_HMEBOX_0					0x01D0
#define REG_HMEBOX_1					0x01D4
#define REG_HMEBOX_2					0x01D8
#define REG_HMEBOX_3					0x01DC
#define REG_LLT_INIT					0x01E0


/*  */
/*  */
/* 	0x0200h ~ 0x027Fh	TXDMA Configuration */
/*  */
/*  */
#define REG_RQPN						0x0200
#define REG_FIFOPAGE					0x0204
#define REG_TDECTRL						0x0208
#define REG_TXDMA_OFFSET_CHK			0x020C
#define REG_TXDMA_STATUS				0x0210
#define REG_RQPN_NPQ					0x0214
#define REG_AUTO_LLT					0x0224


/*  */
/*  */
/* 	0x0280h ~ 0x02FFh	RXDMA Configuration */
/*  */
/*  */
#define REG_RXDMA_AGG_PG_TH			0x0280
#define REG_RXPKT_NUM					0x0284
#define REG_RXDMA_STATUS				0x0288

/*  */
/*  */
/* 	0x0300h ~ 0x03FFh	PCIe */
/*  */
/*  */
#define REG_PCIE_CTRL_REG				0x0300
#define REG_INT_MIG						0x0304	/*  Interrupt Migration */
#define REG_BCNQ_DESA					0x0308	/*  TX Beacon Descriptor Address */
#define REG_HQ_DESA					0x0310	/*  TX High Queue Descriptor Address */
#define REG_MGQ_DESA					0x0318	/*  TX Manage Queue Descriptor Address */
#define REG_VOQ_DESA					0x0320	/*  TX VO Queue Descriptor Address */
#define REG_VIQ_DESA					0x0328	/*  TX VI Queue Descriptor Address */
#define REG_BEQ_DESA					0x0330	/*  TX BE Queue Descriptor Address */
#define REG_BKQ_DESA					0x0338	/*  TX BK Queue Descriptor Address */
#define REG_RX_DESA						0x0340	/*  RX Queue	Descriptor Address */
/* sherry added for DBI Read/Write  20091126 */
#define REG_DBI_WDATA					0x0348	/*  Backdoor REG for Access Configuration */
#define REG_DBI_RDATA				0x034C	/* Backdoor REG for Access Configuration */
#define REG_DBI_CTRL					0x0350	/* Backdoor REG for Access Configuration */
#define REG_DBI_FLAG					0x0352	/* Backdoor REG for Access Configuration */
#define REG_MDIO						0x0354	/*  MDIO for Access PCIE PHY */
#define REG_DBG_SEL						0x0360	/*  Debug Selection Register */
#define REG_PCIE_HRPWM					0x0361	/* PCIe RPWM */
#define REG_PCIE_HCPWM					0x0363	/* PCIe CPWM */
#define REG_WATCH_DOG					0x0368

/*  RTL8723 series ------------------------------- */
#define REG_PCIE_HISR_EN				0x0394	/* PCIE Local Interrupt Enable Register */
#define REG_PCIE_HISR					0x03A0
#define REG_PCIE_HISRE					0x03A4
#define REG_PCIE_HIMR					0x03A8
#define REG_PCIE_HIMRE					0x03AC

#define REG_USB_HIMR					0xFE38
#define REG_USB_HIMRE					0xFE3C
#define REG_USB_HISR					0xFE78
#define REG_USB_HISRE					0xFE7C


/*  */
/*  */
/* 	0x0400h ~ 0x047Fh	Protocol Configuration */
/*  */
/*  */
#define REG_VOQ_INFORMATION			0x0400
#define REG_VIQ_INFORMATION			0x0404
#define REG_BEQ_INFORMATION			0x0408
#define REG_BKQ_INFORMATION			0x040C
#define REG_MGQ_INFORMATION			0x0410
#define REG_HGQ_INFORMATION			0x0414
#define REG_BCNQ_INFORMATION			0x0418
#define REG_TXPKT_EMPTY				0x041A
#define REG_CPU_MGQ_INFORMATION		0x041C
#define REG_FWHW_TXQ_CTRL				0x0420
#define REG_HWSEQ_CTRL					0x0423
#define REG_BCNQ_BDNY					0x0424
#define REG_MGQ_BDNY					0x0425
#define REG_LIFETIME_CTRL				0x0426
#define REG_MULTI_BCNQ_OFFSET			0x0427
#define REG_SPEC_SIFS					0x0428
#define REG_RL							0x042A
#define REG_DARFRC						0x0430
#define REG_RARFRC						0x0438
#define REG_RRSR						0x0440
#define REG_ARFR0						0x0444
#define REG_ARFR1						0x0448
#define REG_ARFR2						0x044C
#define REG_ARFR3						0x0450
#define REG_BCNQ1_BDNY					0x0457

#define REG_AGGLEN_LMT					0x0458
#define REG_AMPDU_MIN_SPACE			0x045C
#define REG_WMAC_LBK_BF_HD			0x045D
#define REG_FAST_EDCA_CTRL				0x0460
#define REG_RD_RESP_PKT_TH				0x0463

#define REG_INIRTS_RATE_SEL				0x0480
#define REG_INIDATA_RATE_SEL			0x0484

#define REG_POWER_STAGE1				0x04B4
#define REG_POWER_STAGE2				0x04B8
#define REG_PKT_VO_VI_LIFE_TIME		0x04C0
#define REG_PKT_BE_BK_LIFE_TIME		0x04C2
#define REG_STBC_SETTING				0x04C4
#define REG_QUEUE_CTRL					0x04C6
#define REG_SINGLE_AMPDU_CTRL			0x04c7
#define REG_PROT_MODE_CTRL			0x04C8
#define REG_MAX_AGGR_NUM				0x04CA
#define REG_RTS_MAX_AGGR_NUM			0x04CB
#define REG_BAR_MODE_CTRL				0x04CC
#define REG_RA_TRY_RATE_AGG_LMT		0x04CF
#define REG_EARLY_MODE_CONTROL		0x04D0
#define REG_MACID_SLEEP				0x04D4
#define REG_NQOS_SEQ					0x04DC
#define REG_QOS_SEQ					0x04DE
#define REG_NEED_CPU_HANDLE			0x04E0
#define REG_PKT_LOSE_RPT				0x04E1
#define REG_PTCL_ERR_STATUS			0x04E2
#define REG_TX_RPT_CTRL					0x04EC
#define REG_TX_RPT_TIME					0x04F0	/*  2 byte */
#define REG_DUMMY						0x04FC

/*  */
/*  */
/* 	0x0500h ~ 0x05FFh	EDCA Configuration */
/*  */
/*  */
#define REG_EDCA_VO_PARAM				0x0500
#define REG_EDCA_VI_PARAM				0x0504
#define REG_EDCA_BE_PARAM				0x0508
#define REG_EDCA_BK_PARAM				0x050C
#define REG_BCNTCFG						0x0510
#define REG_PIFS							0x0512
#define REG_RDG_PIFS					0x0513
#define REG_SIFS_CTX					0x0514
#define REG_SIFS_TRX					0x0516
#define REG_TSFTR_SYN_OFFSET			0x0518
#define REG_AGGR_BREAK_TIME			0x051A
#define REG_SLOT						0x051B
#define REG_TX_PTCL_CTRL				0x0520
#define REG_TXPAUSE						0x0522
#define REG_DIS_TXREQ_CLR				0x0523
#define REG_RD_CTRL						0x0524
/*  */
/*  Format for offset 540h-542h: */
/* 	[3:0]:   TBTT prohibit setup in unit of 32us. The time for HW getting beacon content before TBTT. */
/* 	[7:4]:   Reserved. */
/* 	[19:8]:  TBTT prohibit hold in unit of 32us. The time for HW holding to send the beacon packet. */
/* 	[23:20]: Reserved */
/*  Description: */
/* 	              | */
/*      |<--Setup--|--Hold------------>| */
/* 	--------------|---------------------- */
/*                 | */
/*                TBTT */
/*  Note: We cannot update beacon content to HW or send any AC packets during the time between Setup and Hold. */
/*  Described by Designer Tim and Bruce, 2011-01-14. */
/*  */
#define REG_TBTT_PROHIBIT				0x0540
#define REG_RD_NAV_NXT					0x0544
#define REG_NAV_PROT_LEN				0x0546
#define REG_BCN_CTRL					0x0550
#define REG_BCN_CTRL_1					0x0551
#define REG_MBID_NUM					0x0552
#define REG_DUAL_TSF_RST				0x0553
#define REG_BCN_INTERVAL				0x0554	/*  The same as REG_MBSSID_BCN_SPACE */
#define REG_DRVERLYINT					0x0558
#define REG_BCNDMATIM					0x0559
#define REG_ATIMWND					0x055A
#define REG_USTIME_TSF					0x055C
#define REG_BCN_MAX_ERR				0x055D
#define REG_RXTSF_OFFSET_CCK			0x055E
#define REG_RXTSF_OFFSET_OFDM			0x055F
#define REG_TSFTR						0x0560
#define REG_TSFTR1						0x0568	/*  HW Port 1 TSF Register */
#define REG_ATIMWND_1					0x0570
#define REG_P2P_CTWIN					0x0572 /*  1 Byte long (in unit of TU) */
#define REG_PSTIMER						0x0580
#define REG_TIMER0						0x0584
#define REG_TIMER1						0x0588
#define REG_ACMHWCTRL					0x05C0
#define REG_NOA_DESC_SEL				0x05CF
#define REG_NOA_DESC_DURATION		0x05E0
#define REG_NOA_DESC_INTERVAL			0x05E4
#define REG_NOA_DESC_START			0x05E8
#define REG_NOA_DESC_COUNT			0x05EC

#define REG_DMC							0x05F0	/* Dual MAC Co-Existence Register */
#define REG_SCH_TX_CMD					0x05F8

#define REG_FW_RESET_TSF_CNT_1		0x05FC
#define REG_FW_RESET_TSF_CNT_0		0x05FD
#define REG_FW_BCN_DIS_CNT			0x05FE

/*  */
/*  */
/* 	0x0600h ~ 0x07FFh	WMAC Configuration */
/*  */
/*  */
#define REG_APSD_CTRL					0x0600
#define REG_BWOPMODE					0x0603
#define REG_TCR							0x0604
#define REG_RCR							0x0608
#define REG_RX_PKT_LIMIT				0x060C
#define REG_RX_DLK_TIME				0x060D
#define REG_RX_DRVINFO_SZ				0x060F

#define REG_MACID						0x0610
#define REG_BSSID						0x0618
#define REG_MAR							0x0620
#define REG_MBIDCAMCFG					0x0628

#define REG_PNO_STATUS					0x0631
#define REG_USTIME_EDCA				0x0638
#define REG_MAC_SPEC_SIFS				0x063A
/*  20100719 Joseph: Hardware register definition change. (HW datasheet v54) */
#define REG_RESP_SIFS_CCK				0x063C	/*  [15:8]SIFS_R2T_OFDM, [7:0]SIFS_R2T_CCK */
#define REG_RESP_SIFS_OFDM                    0x063E	/*  [15:8]SIFS_T2T_OFDM, [7:0]SIFS_T2T_CCK */

#define REG_ACKTO						0x0640
#define REG_CTS2TO						0x0641
#define REG_EIFS							0x0642


/* RXERR_RPT */
#define RXERR_TYPE_OFDM_PPDU			0
#define RXERR_TYPE_OFDMfalse_ALARM	1
#define RXERR_TYPE_OFDM_MPDU_OK			2
#define RXERR_TYPE_OFDM_MPDU_FAIL	3
#define RXERR_TYPE_CCK_PPDU			4
#define RXERR_TYPE_CCKfalse_ALARM	5
#define RXERR_TYPE_CCK_MPDU_OK		6
#define RXERR_TYPE_CCK_MPDU_FAIL		7
#define RXERR_TYPE_HT_PPDU				8
#define RXERR_TYPE_HTfalse_ALARM	9
#define RXERR_TYPE_HT_MPDU_TOTAL		10
#define RXERR_TYPE_HT_MPDU_OK			11
#define RXERR_TYPE_HT_MPDU_FAIL			12
#define RXERR_TYPE_RX_FULL_DROP			15

#define RXERR_COUNTER_MASK			0xFFFFF
#define RXERR_RPT_RST					BIT(27)
#define _RXERR_RPT_SEL(type)			((type) << 28)

/*  */
/*  Note: */
/* 	The NAV upper value is very important to WiFi 11n 5.2.3 NAV test. The default value is */
/* 	always too small, but the WiFi TestPlan test by 25, 000 microseconds of NAV through sending */
/* 	CTS in the air. We must update this value greater than 25, 000 microseconds to pass the item. */
/* 	The offset of NAV_UPPER in 8192C Spec is incorrect, and the offset should be 0x0652. Commented */
/* 	by SD1 Scott. */
/*  By Bruce, 2011-07-18. */
/*  */
#define REG_NAV_UPPER					0x0652	/*  unit of 128 */

/* WMA, BA, CCX */
#define REG_NAV_CTRL					0x0650
#define REG_BACAMCMD					0x0654
#define REG_BACAMCONTENT				0x0658
#define REG_LBDLY						0x0660
#define REG_FWDLY						0x0661
#define REG_RXERR_RPT					0x0664
#define REG_WMAC_TRXPTCL_CTL			0x0668

/*  Security */
#define REG_CAMCMD						0x0670
#define REG_CAMWRITE					0x0674
#define REG_CAMREAD					0x0678
#define REG_CAMDBG						0x067C
#define REG_SECCFG						0x0680

/*  Power */
#define REG_WOW_CTRL					0x0690
#define REG_PS_RX_INFO					0x0692
#define REG_UAPSD_TID					0x0693
#define REG_WKFMCAM_CMD				0x0698
#define REG_WKFMCAM_NUM				REG_WKFMCAM_CMD
#define REG_WKFMCAM_RWD				0x069C
#define REG_RXFLTMAP0					0x06A0
#define REG_RXFLTMAP1					0x06A2
#define REG_RXFLTMAP2					0x06A4
#define REG_BCN_PSR_RPT				0x06A8
#define REG_BT_COEX_TABLE				0x06C0

/*  Hardware Port 2 */
#define REG_MACID1						0x0700
#define REG_BSSID1						0x0708


/*  */
/*  */
/* 	0xFE00h ~ 0xFE55h	USB Configuration */
/*  */
/*  */
#define REG_USB_INFO					0xFE17
#define REG_USB_SPECIAL_OPTION		0xFE55
#define REG_USB_DMA_AGG_TO			0xFE5B
#define REG_USB_AGG_TO					0xFE5C
#define REG_USB_AGG_TH					0xFE5D

#define REG_USB_HRPWM					0xFE58
#define REG_USB_HCPWM					0xFE57

/*  for 92DU high_Queue low_Queue Normal_Queue select */
#define REG_USB_High_NORMAL_Queue_Select_MAC0	0xFE44
/* define REG_USB_LOW_Queue_Select_MAC0		0xFE45 */
#define REG_USB_High_NORMAL_Queue_Select_MAC1	0xFE47
/* define REG_USB_LOW_Queue_Select_MAC1		0xFE48 */

/*  For test chip */
#define REG_TEST_USB_TXQS				0xFE48
#define REG_TEST_SIE_VID				0xFE60		/*  0xFE60~0xFE61 */
#define REG_TEST_SIE_PID				0xFE62		/*  0xFE62~0xFE63 */
#define REG_TEST_SIE_OPTIONAL			0xFE64
#define REG_TEST_SIE_CHIRP_K			0xFE65
#define REG_TEST_SIE_PHY				0xFE66		/*  0xFE66~0xFE6B */
#define REG_TEST_SIE_MAC_ADDR			0xFE70		/*  0xFE70~0xFE75 */
#define REG_TEST_SIE_STRING			0xFE80		/*  0xFE80~0xFEB9 */


/*  For normal chip */
#define REG_NORMAL_SIE_VID				0xFE60		/*  0xFE60~0xFE61 */
#define REG_NORMAL_SIE_PID				0xFE62		/*  0xFE62~0xFE63 */
#define REG_NORMAL_SIE_OPTIONAL		0xFE64
#define REG_NORMAL_SIE_EP				0xFE65		/*  0xFE65~0xFE67 */
#define REG_NORMAL_SIE_PHY			0xFE68		/*  0xFE68~0xFE6B */
#define REG_NORMAL_SIE_OPTIONAL2		0xFE6C
#define REG_NORMAL_SIE_GPS_EP			0xFE6D		/*  0xFE6D, for RTL8723 only. */
#define REG_NORMAL_SIE_MAC_ADDR		0xFE70		/*  0xFE70~0xFE75 */
#define REG_NORMAL_SIE_STRING			0xFE80		/*  0xFE80~0xFEDF */


/*  */
/*  */
/* 	Redifine 8192C register definition for compatibility */
/*  */
/*  */

/*  TODO: use these definition when using REG_xxx naming rule. */
/*  NOTE: DO NOT Remove these definition. Use later. */

#define EFUSE_CTRL				REG_EFUSE_CTRL		/*  E-Fuse Control. */
#define EFUSE_TEST				REG_EFUSE_TEST		/*  E-Fuse Test. */
#define MSR						(REG_CR + 2)		/*  Media Status register */
/* define ISR						REG_HISR */

#define TSFR						REG_TSFTR			/*  Timing Sync Function Timer Register. */
#define TSFR1					REG_TSFTR1			/*  HW Port 1 TSF Register */

#define PBP						REG_PBP

/*  Redifine MACID register, to compatible prior ICs. */
#define IDR0						REG_MACID			/*  MAC ID Register, Offset 0x0050-0x0053 */
#define IDR4						(REG_MACID + 4)		/*  MAC ID Register, Offset 0x0054-0x0055 */


/*  */
/*  9. Security Control Registers	(Offset:) */
/*  */
#define RWCAM					REG_CAMCMD		/* IN 8190 Data Sheet is called CAMcmd */
#define WCAMI					REG_CAMWRITE	/*  Software write CAM input content */
#define RCAMO					REG_CAMREAD		/*  Software read/write CAM config */
#define CAMDBG					REG_CAMDBG
#define SECR						REG_SECCFG		/* Security Configuration Register */

/*  Unused register */
#define UnusedRegister			0x1BF
#define DCAM					UnusedRegister
#define PSR						UnusedRegister
#define BBAddr					UnusedRegister
#define PhyDataR					UnusedRegister

/*  Min Spacing related settings. */
#define MAX_MSS_DENSITY_2T			0x13
#define MAX_MSS_DENSITY_1T			0x0A

/*  */
/*        8192C Cmd9346CR bits					(Offset 0xA, 16bit) */
/*  */
#define CmdEEPROM_En				BIT5	 /*  EEPROM enable when set 1 */
#define CmdEERPOMSEL				BIT4	/*  System EEPROM select, 0: boot from E-FUSE, 1: The EEPROM used is 9346 */
#define Cmd9346CR_9356SEL			BIT4

/*  */
/*        8192C GPIO MUX Configuration Register (offset 0x40, 4 byte) */
/*  */
#define GPIOSEL_GPIO				0
#define GPIOSEL_ENBT				BIT5

/*  */
/*        8192C GPIO PIN Control Register (offset 0x44, 4 byte) */
/*  */
#define GPIO_IN					REG_GPIO_PIN_CTRL		/*  GPIO pins input value */
#define GPIO_OUT				(REG_GPIO_PIN_CTRL+1)	/*  GPIO pins output value */
#define GPIO_IO_SEL				(REG_GPIO_PIN_CTRL+2)	/*  GPIO pins output enable when a bit is set to "1"; otherwise, input is configured. */
#define GPIO_MOD				(REG_GPIO_PIN_CTRL+3)

/*  */
/*        8811A GPIO PIN Control Register (offset 0x60, 4 byte) */
/*  */
#define GPIO_IN_8811A			REG_GPIO_PIN_CTRL_2		/*  GPIO pins input value */
#define GPIO_OUT_8811A			(REG_GPIO_PIN_CTRL_2+1)	/*  GPIO pins output value */
#define GPIO_IO_SEL_8811A		(REG_GPIO_PIN_CTRL_2+2)	/*  GPIO pins output enable when a bit is set to "1"; otherwise, input is configured. */
#define GPIO_MOD_8811A			(REG_GPIO_PIN_CTRL_2+3)

/*  */
/*        8723/8188E Host System Interrupt Mask Register (offset 0x58, 32 byte) */
/*  */
#define HSIMR_GPIO12_0_INT_EN			BIT0
#define HSIMR_SPS_OCP_INT_EN			BIT5
#define HSIMR_RON_INT_EN				BIT6
#define HSIMR_PDN_INT_EN				BIT7
#define HSIMR_GPIO9_INT_EN				BIT25

/*  */
/*        8723/8188E Host System Interrupt Status Register (offset 0x5C, 32 byte) */
/*  */
#define HSISR_GPIO12_0_INT				BIT0
#define HSISR_SPS_OCP_INT				BIT5
#define HSISR_RON_INT					BIT6
#define HSISR_PDNINT					BIT7
#define HSISR_GPIO9_INT					BIT25

/*  */
/*        8192C (MSR) Media Status Register	(Offset 0x4C, 8 bits) */
/*  */
/*
Network Type
00: No link
01: Link in ad hoc network
10: Link in infrastructure network
11: AP mode
Default: 00b.
*/
#define MSR_NOLINK				0x00
#define MSR_ADHOC				0x01
#define MSR_INFRA				0x02
#define MSR_AP					0x03

/*  */
/*        USB INTR CONTENT */
/*  */
#define USB_C2H_CMDID_OFFSET					0
#define USB_C2H_SEQ_OFFSET					1
#define USB_C2H_EVENT_OFFSET					2
#define USB_INTR_CPWM_OFFSET					16
#define USB_INTR_CONTENT_C2H_OFFSET			0
#define USB_INTR_CONTENT_CPWM1_OFFSET		16
#define USB_INTR_CONTENT_CPWM2_OFFSET		20
#define USB_INTR_CONTENT_HISR_OFFSET			48
#define USB_INTR_CONTENT_HISRE_OFFSET		52
#define USB_INTR_CONTENT_LENGTH				56

/*  */
/*        Response Rate Set Register	(offset 0x440, 24bits) */
/*  */
#define RRSR_1M					BIT0
#define RRSR_2M					BIT1
#define RRSR_5_5M				BIT2
#define RRSR_11M				BIT3
#define RRSR_6M					BIT4
#define RRSR_9M					BIT5
#define RRSR_12M				BIT6
#define RRSR_18M				BIT7
#define RRSR_24M				BIT8
#define RRSR_36M				BIT9
#define RRSR_48M				BIT10
#define RRSR_54M				BIT11
#define RRSR_MCS0				BIT12
#define RRSR_MCS1				BIT13
#define RRSR_MCS2				BIT14
#define RRSR_MCS3				BIT15
#define RRSR_MCS4				BIT16
#define RRSR_MCS5				BIT17
#define RRSR_MCS6				BIT18
#define RRSR_MCS7				BIT19

#define RRSR_CCK_RATES (RRSR_11M|RRSR_5_5M|RRSR_2M|RRSR_1M)
#define RRSR_OFDM_RATES (RRSR_54M|RRSR_48M|RRSR_36M|RRSR_24M|RRSR_18M|RRSR_12M|RRSR_9M|RRSR_6M)

/*  WOL bit information */
#define HAL92C_WOL_PTK_UPDATE_EVENT		BIT0
#define HAL92C_WOL_GTK_UPDATE_EVENT		BIT1
#define HAL92C_WOL_DISASSOC_EVENT		BIT2
#define HAL92C_WOL_DEAUTH_EVENT			BIT3
#define HAL92C_WOL_FW_DISCONNECT_EVENT	BIT4

/*  */
/*        Rate Definition */
/*  */
/* CCK */
#define	RATR_1M					0x00000001
#define	RATR_2M					0x00000002
#define	RATR_55M					0x00000004
#define	RATR_11M					0x00000008
/* OFDM */
#define	RATR_6M					0x00000010
#define	RATR_9M					0x00000020
#define	RATR_12M					0x00000040
#define	RATR_18M					0x00000080
#define	RATR_24M					0x00000100
#define	RATR_36M					0x00000200
#define	RATR_48M					0x00000400
#define	RATR_54M					0x00000800
/* MCS 1 Spatial Stream */
#define	RATR_MCS0					0x00001000
#define	RATR_MCS1					0x00002000
#define	RATR_MCS2					0x00004000
#define	RATR_MCS3					0x00008000
#define	RATR_MCS4					0x00010000
#define	RATR_MCS5					0x00020000
#define	RATR_MCS6					0x00040000
#define	RATR_MCS7					0x00080000

/* CCK */
#define RATE_1M					BIT(0)
#define RATE_2M					BIT(1)
#define RATE_5_5M				BIT(2)
#define RATE_11M				BIT(3)
/* OFDM */
#define RATE_6M					BIT(4)
#define RATE_9M					BIT(5)
#define RATE_12M				BIT(6)
#define RATE_18M				BIT(7)
#define RATE_24M				BIT(8)
#define RATE_36M				BIT(9)
#define RATE_48M				BIT(10)
#define RATE_54M				BIT(11)
/* MCS 1 Spatial Stream */
#define RATE_MCS0				BIT(12)
#define RATE_MCS1				BIT(13)
#define RATE_MCS2				BIT(14)
#define RATE_MCS3				BIT(15)
#define RATE_MCS4				BIT(16)
#define RATE_MCS5				BIT(17)
#define RATE_MCS6				BIT(18)
#define RATE_MCS7				BIT(19)

/*  ALL CCK Rate */
#define RATE_BITMAP_ALL			0xFFFFF

/*  Only use CCK 1M rate for ACK */
#define RATE_RRSR_CCK_ONLY_1M		0xFFFF1
#define RATE_RRSR_WITHOUT_CCK		0xFFFF0

/*  */
/*        BW_OPMODE bits				(Offset 0x603, 8bit) */
/*  */
#define BW_OPMODE_20MHZ			BIT2

/*  */
/*        CAM Config Setting (offset 0x680, 1 byte) */
/*  */
#define CAM_VALID				BIT15
#define CAM_NOTVALID			0x0000
#define CAM_USEDK				BIT5

#define CAM_CONTENT_COUNT	8

#define CAM_NONE				0x0
#define CAM_WEP40				0x01
#define CAM_TKIP				0x02
#define CAM_AES					0x04
#define CAM_WEP104				0x05
#define CAM_SMS4				0x6

#define TOTAL_CAM_ENTRY		32
#define HALF_CAM_ENTRY			16

#define CAM_CONFIG_USEDK		true
#define CAM_CONFIG_NO_USEDK	false

#define CAM_WRITE				BIT16
#define CAM_READ				0x00000000
#define CAM_POLLINIG			BIT31

/*  */
/*  10. Power Save Control Registers */
/*  */
#define WOW_PMEN				BIT0 /*  Power management Enable. */
#define WOW_WOMEN				BIT1 /*  WoW function on or off. */
#define WOW_MAGIC				BIT2 /*  Magic packet */
#define WOW_UWF				BIT3 /*  Unicast Wakeup frame. */

/*  */
/*  12. Host Interrupt Status Registers */
/*  */
/*  */
/*       8190 IMR/ISR bits */
/*  */
#define IMR8190_DISABLED		0x0
#define IMR_DISABLED			0x0
/*  IMR DW0 Bit 0-31 */
#define IMR_BCNDMAINT6			BIT31		/*  Beacon DMA Interrupt 6 */
#define IMR_BCNDMAINT5			BIT30		/*  Beacon DMA Interrupt 5 */
#define IMR_BCNDMAINT4			BIT29		/*  Beacon DMA Interrupt 4 */
#define IMR_BCNDMAINT3			BIT28		/*  Beacon DMA Interrupt 3 */
#define IMR_BCNDMAINT2			BIT27		/*  Beacon DMA Interrupt 2 */
#define IMR_BCNDMAINT1			BIT26		/*  Beacon DMA Interrupt 1 */
#define IMR_BCNDOK8				BIT25		/*  Beacon Queue DMA OK Interrupt 8 */
#define IMR_BCNDOK7				BIT24		/*  Beacon Queue DMA OK Interrupt 7 */
#define IMR_BCNDOK6				BIT23		/*  Beacon Queue DMA OK Interrupt 6 */
#define IMR_BCNDOK5				BIT22		/*  Beacon Queue DMA OK Interrupt 5 */
#define IMR_BCNDOK4				BIT21		/*  Beacon Queue DMA OK Interrupt 4 */
#define IMR_BCNDOK3				BIT20		/*  Beacon Queue DMA OK Interrupt 3 */
#define IMR_BCNDOK2				BIT19		/*  Beacon Queue DMA OK Interrupt 2 */
#define IMR_BCNDOK1				BIT18		/*  Beacon Queue DMA OK Interrupt 1 */
#define IMR_TIMEOUT2			BIT17		/*  Timeout interrupt 2 */
#define IMR_TIMEOUT1			BIT16		/*  Timeout interrupt 1 */
#define IMR_TXFOVW				BIT15		/*  Transmit FIFO Overflow */
#define IMR_PSTIMEOUT			BIT14		/*  Power save time out interrupt */
#define IMR_BcnInt				BIT13		/*  Beacon DMA Interrupt 0 */
#define IMR_RXFOVW				BIT12		/*  Receive FIFO Overflow */
#define IMR_RDU					BIT11		/*  Receive Descriptor Unavailable */
#define IMR_ATIMEND				BIT10		/*  For 92C, ATIM Window End Interrupt. For 8723 and later ICs, it also means P2P CTWin End interrupt. */
#define IMR_BDOK				BIT9		/*  Beacon Queue DMA OK Interrupt */
#define IMR_HIGHDOK				BIT8		/*  High Queue DMA OK Interrupt */
#define IMR_TBDOK				BIT7		/*  Transmit Beacon OK interrupt */
#define IMR_MGNTDOK			BIT6		/*  Management Queue DMA OK Interrupt */
#define IMR_TBDER				BIT5		/*  For 92C, Transmit Beacon Error Interrupt */
#define IMR_BKDOK				BIT4		/*  AC_BK DMA OK Interrupt */
#define IMR_BEDOK				BIT3		/*  AC_BE DMA OK Interrupt */
#define IMR_VIDOK				BIT2		/*  AC_VI DMA OK Interrupt */
#define IMR_VODOK				BIT1		/*  AC_VO DMA Interrupt */
#define IMR_ROK					BIT0		/*  Receive DMA OK Interrupt */

/*  13. Host Interrupt Status Extension Register	 (Offset: 0x012C-012Eh) */
#define IMR_TSF_BIT32_TOGGLE	BIT15
#define IMR_BcnInt_E				BIT12
#define IMR_TXERR				BIT11
#define IMR_RXERR				BIT10
#define IMR_C2HCMD				BIT9
#define IMR_CPWM				BIT8
/* RSVD [2-7] */
#define IMR_OCPINT				BIT1
#define IMR_WLANOFF			BIT0

/*  */
/*  8723E series PCIE Host IMR/ISR bit */
/*  */
/*  IMR DW0 Bit 0-31 */
#define PHIMR_TIMEOUT2				BIT31
#define PHIMR_TIMEOUT1				BIT30
#define PHIMR_PSTIMEOUT			BIT29
#define PHIMR_GTINT4				BIT28
#define PHIMR_GTINT3				BIT27
#define PHIMR_TXBCNERR				BIT26
#define PHIMR_TXBCNOK				BIT25
#define PHIMR_TSF_BIT32_TOGGLE	BIT24
#define PHIMR_BCNDMAINT3			BIT23
#define PHIMR_BCNDMAINT2			BIT22
#define PHIMR_BCNDMAINT1			BIT21
#define PHIMR_BCNDMAINT0			BIT20
#define PHIMR_BCNDOK3				BIT19
#define PHIMR_BCNDOK2				BIT18
#define PHIMR_BCNDOK1				BIT17
#define PHIMR_BCNDOK0				BIT16
#define PHIMR_HSISR_IND_ON			BIT15
#define PHIMR_BCNDMAINT_E			BIT14
#define PHIMR_ATIMEND_E			BIT13
#define PHIMR_ATIM_CTW_END		BIT12
#define PHIMR_HISRE_IND			BIT11	/*  RO. HISRE Indicator (HISRE & HIMRE is true, this bit is set to 1) */
#define PHIMR_C2HCMD				BIT10
#define PHIMR_CPWM2				BIT9
#define PHIMR_CPWM					BIT8
#define PHIMR_HIGHDOK				BIT7		/*  High Queue DMA OK Interrupt */
#define PHIMR_MGNTDOK				BIT6		/*  Management Queue DMA OK Interrupt */
#define PHIMR_BKDOK					BIT5		/*  AC_BK DMA OK Interrupt */
#define PHIMR_BEDOK					BIT4		/*  AC_BE DMA OK Interrupt */
#define PHIMR_VIDOK					BIT3		/*  AC_VI DMA OK Interrupt */
#define PHIMR_VODOK				BIT2		/*  AC_VO DMA Interrupt */
#define PHIMR_RDU					BIT1		/*  Receive Descriptor Unavailable */
#define PHIMR_ROK					BIT0		/*  Receive DMA OK Interrupt */

/*  PCIE Host Interrupt Status Extension bit */
#define PHIMR_BCNDMAINT7			BIT23
#define PHIMR_BCNDMAINT6			BIT22
#define PHIMR_BCNDMAINT5			BIT21
#define PHIMR_BCNDMAINT4			BIT20
#define PHIMR_BCNDOK7				BIT19
#define PHIMR_BCNDOK6				BIT18
#define PHIMR_BCNDOK5				BIT17
#define PHIMR_BCNDOK4				BIT16
/*  bit12 15: RSVD */
#define PHIMR_TXERR					BIT11
#define PHIMR_RXERR					BIT10
#define PHIMR_TXFOVW				BIT9
#define PHIMR_RXFOVW				BIT8
/*  bit2-7: RSVD */
#define PHIMR_OCPINT				BIT1
/*  bit0: RSVD */

#define UHIMR_TIMEOUT2				BIT31
#define UHIMR_TIMEOUT1				BIT30
#define UHIMR_PSTIMEOUT			BIT29
#define UHIMR_GTINT4				BIT28
#define UHIMR_GTINT3				BIT27
#define UHIMR_TXBCNERR				BIT26
#define UHIMR_TXBCNOK				BIT25
#define UHIMR_TSF_BIT32_TOGGLE	BIT24
#define UHIMR_BCNDMAINT3			BIT23
#define UHIMR_BCNDMAINT2			BIT22
#define UHIMR_BCNDMAINT1			BIT21
#define UHIMR_BCNDMAINT0			BIT20
#define UHIMR_BCNDOK3				BIT19
#define UHIMR_BCNDOK2				BIT18
#define UHIMR_BCNDOK1				BIT17
#define UHIMR_BCNDOK0				BIT16
#define UHIMR_HSISR_IND			BIT15
#define UHIMR_BCNDMAINT_E			BIT14
/* RSVD	BIT13 */
#define UHIMR_CTW_END				BIT12
/* RSVD	BIT11 */
#define UHIMR_C2HCMD				BIT10
#define UHIMR_CPWM2				BIT9
#define UHIMR_CPWM					BIT8
#define UHIMR_HIGHDOK				BIT7		/*  High Queue DMA OK Interrupt */
#define UHIMR_MGNTDOK				BIT6		/*  Management Queue DMA OK Interrupt */
#define UHIMR_BKDOK				BIT5		/*  AC_BK DMA OK Interrupt */
#define UHIMR_BEDOK				BIT4		/*  AC_BE DMA OK Interrupt */
#define UHIMR_VIDOK					BIT3		/*  AC_VI DMA OK Interrupt */
#define UHIMR_VODOK				BIT2		/*  AC_VO DMA Interrupt */
#define UHIMR_RDU					BIT1		/*  Receive Descriptor Unavailable */
#define UHIMR_ROK					BIT0		/*  Receive DMA OK Interrupt */

/*  USB Host Interrupt Status Extension bit */
#define UHIMR_BCNDMAINT7			BIT23
#define UHIMR_BCNDMAINT6			BIT22
#define UHIMR_BCNDMAINT5			BIT21
#define UHIMR_BCNDMAINT4			BIT20
#define UHIMR_BCNDOK7				BIT19
#define UHIMR_BCNDOK6				BIT18
#define UHIMR_BCNDOK5				BIT17
#define UHIMR_BCNDOK4				BIT16
/*  bit14-15: RSVD */
#define UHIMR_ATIMEND_E			BIT13
#define UHIMR_ATIMEND				BIT12
#define UHIMR_TXERR					BIT11
#define UHIMR_RXERR					BIT10
#define UHIMR_TXFOVW				BIT9
#define UHIMR_RXFOVW				BIT8
/*  bit2-7: RSVD */
#define UHIMR_OCPINT				BIT1
/*  bit0: RSVD */


#define HAL_NIC_UNPLUG_ISR			0xFFFFFFFF	/*  The value when the NIC is unplugged for PCI. */
#define HAL_NIC_UNPLUG_PCI_ISR		0xEAEAEAEA	/*  The value when the NIC is unplugged for PCI in PCI interrupt (page 3). */

/*  */
/*        8188 IMR/ISR bits */
/*  */
#define IMR_DISABLED_88E			0x0
/*  IMR DW0(0x0060-0063) Bit 0-31 */
#define IMR_TXCCK_88E				BIT30		/*  TXRPT interrupt when CCX bit of the packet is set */
#define IMR_PSTIMEOUT_88E			BIT29		/*  Power Save Time Out Interrupt */
#define IMR_GTINT4_88E				BIT28		/*  When GTIMER4 expires, this bit is set to 1 */
#define IMR_GTINT3_88E				BIT27		/*  When GTIMER3 expires, this bit is set to 1 */
#define IMR_TBDER_88E				BIT26		/*  Transmit Beacon0 Error */
#define IMR_TBDOK_88E				BIT25		/*  Transmit Beacon0 OK */
#define IMR_TSF_BIT32_TOGGLE_88E	BIT24		/*  TSF Timer BIT32 toggle indication interrupt */
#define IMR_BCNDMAINT0_88E		BIT20		/*  Beacon DMA Interrupt 0 */
#define IMR_BCNDERR0_88E			BIT16		/*  Beacon Queue DMA Error 0 */
#define IMR_HSISR_IND_ON_INT_88E	BIT15		/*  HSISR Indicator (HSIMR & HSISR is true, this bit is set to 1) */
#define IMR_BCNDMAINT_E_88E		BIT14		/*  Beacon DMA Interrupt Extension for Win7 */
#define IMR_ATIMEND_88E			BIT12		/*  CTWidnow End or ATIM Window End */
#define IMR_HISR1_IND_INT_88E		BIT11		/*  HISR1 Indicator (HISR1 & HIMR1 is true, this bit is set to 1) */
#define IMR_C2HCMD_88E				BIT10		/*  CPU to Host Command INT Status, Write 1 clear */
#define IMR_CPWM2_88E				BIT9			/*  CPU power Mode exchange INT Status, Write 1 clear */
#define IMR_CPWM_88E				BIT8			/*  CPU power Mode exchange INT Status, Write 1 clear */
#define IMR_HIGHDOK_88E			BIT7			/*  High Queue DMA OK */
#define IMR_MGNTDOK_88E			BIT6			/*  Management Queue DMA OK */
#define IMR_BKDOK_88E				BIT5			/*  AC_BK DMA OK */
#define IMR_BEDOK_88E				BIT4			/*  AC_BE DMA OK */
#define IMR_VIDOK_88E				BIT3			/*  AC_VI DMA OK */
#define IMR_VODOK_88E				BIT2			/*  AC_VO DMA OK */
#define IMR_RDU_88E					BIT1			/*  Rx Descriptor Unavailable */
#define IMR_ROK_88E					BIT0			/*  Receive DMA OK */

/*  IMR DW1(0x00B4-00B7) Bit 0-31 */
#define IMR_BCNDMAINT7_88E		BIT27		/*  Beacon DMA Interrupt 7 */
#define IMR_BCNDMAINT6_88E		BIT26		/*  Beacon DMA Interrupt 6 */
#define IMR_BCNDMAINT5_88E		BIT25		/*  Beacon DMA Interrupt 5 */
#define IMR_BCNDMAINT4_88E		BIT24		/*  Beacon DMA Interrupt 4 */
#define IMR_BCNDMAINT3_88E		BIT23		/*  Beacon DMA Interrupt 3 */
#define IMR_BCNDMAINT2_88E		BIT22		/*  Beacon DMA Interrupt 2 */
#define IMR_BCNDMAINT1_88E		BIT21		/*  Beacon DMA Interrupt 1 */
#define IMR_BCNDOK7_88E			BIT20		/*  Beacon Queue DMA OK Interrupt 7 */
#define IMR_BCNDOK6_88E			BIT19		/*  Beacon Queue DMA OK Interrupt 6 */
#define IMR_BCNDOK5_88E			BIT18		/*  Beacon Queue DMA OK Interrupt 5 */
#define IMR_BCNDOK4_88E			BIT17		/*  Beacon Queue DMA OK Interrupt 4 */
#define IMR_BCNDOK3_88E			BIT16		/*  Beacon Queue DMA OK Interrupt 3 */
#define IMR_BCNDOK2_88E			BIT15		/*  Beacon Queue DMA OK Interrupt 2 */
#define IMR_BCNDOK1_88E			BIT14		/*  Beacon Queue DMA OK Interrupt 1 */
#define IMR_ATIMEND_E_88E			BIT13		/*  ATIM Window End Extension for Win7 */
#define IMR_TXERR_88E				BIT11		/*  Tx Error Flag Interrupt Status, write 1 clear. */
#define IMR_RXERR_88E				BIT10		/*  Rx Error Flag INT Status, Write 1 clear */
#define IMR_TXFOVW_88E				BIT9			/*  Transmit FIFO Overflow */
#define IMR_RXFOVW_88E				BIT8			/*  Receive FIFO Overflow */

/*===================================================================
=====================================================================
Here the register defines are for 92C. When the define is as same with 92C,
we will use the 92C's define for the consistency
So the following defines for 92C is not entire!!!!!!
=====================================================================
=====================================================================*/
/*
Based on Datasheet V33---090401
Register Summary
Current IOREG MAP
0x0000h ~ 0x00FFh   System Configuration (256 Bytes)
0x0100h ~ 0x01FFh   MACTOP General Configuration (256 Bytes)
0x0200h ~ 0x027Fh   TXDMA Configuration (128 Bytes)
0x0280h ~ 0x02FFh   RXDMA Configuration (128 Bytes)
0x0300h ~ 0x03FFh   PCIE EMAC Reserved Region (256 Bytes)
0x0400h ~ 0x04FFh   Protocol Configuration (256 Bytes)
0x0500h ~ 0x05FFh   EDCA Configuration (256 Bytes)
0x0600h ~ 0x07FFh   WMAC Configuration (512 Bytes)
0x2000h ~ 0x3FFFh   8051 FW Download Region (8196 Bytes)
*/
	/*  */
	/* 		 8192C (TXPAUSE) transmission pause	(Offset 0x522, 8 bits) */
	/*  */
/*  Note: */
/* 	The  bits of stopping AC(VO/VI/BE/BK) queue in datasheet RTL8192S/RTL8192C are wrong, */
/* 	the correct arrangement is VO - Bit0, VI - Bit1, BE - Bit2, and BK - Bit3. */
/* 	8723 and 88E may be not correct either in the earlier version. Confirmed with DD Tim. */
/*  By Bruce, 2011-09-22. */
#define StopBecon		BIT6
#define StopHigh			BIT5
#define StopMgt			BIT4
#define StopBK			BIT3
#define StopBE			BIT2
#define StopVI			BIT1
#define StopVO			BIT0

/*  */
/*        8192C (RCR) Receive Configuration Register	(Offset 0x608, 32 bits) */
/*  */
#define RCR_APPFCS				BIT31	/*  WMAC append FCS after pauload */
#define RCR_APP_MIC				BIT30	/*  MACRX will retain the MIC at the bottom of the packet. */
#define RCR_APP_ICV				BIT29	/*  MACRX will retain the ICV at the bottom of the packet. */
#define RCR_APP_PHYST_RXFF		BIT28	/*  PHY Status is appended before RX packet in RXFF */
#define RCR_APP_BA_SSN			BIT27	/*  SSN of previous TXBA is appended as after original RXDESC as the 4-th DW of RXDESC. */
#define RCR_NONQOS_VHT			BIT26	/*  Reserved */
#define RCR_RSVD_BIT25			BIT25	/*  Reserved */
#define RCR_ENMBID				BIT24	/*  Enable Multiple BssId. Only response ACK to the packets whose DID(A1) matching to the addresses in the MBSSID CAM Entries. */
#define RCR_LSIGEN				BIT23	/*  Enable LSIG TXOP Protection function. Search KEYCAM for each rx packet to check if LSIGEN bit is set. */
#define RCR_MFBEN				BIT22	/*  Enable immediate MCS Feedback function. When Rx packet with MRQ = 1'b1, then search KEYCAM to find sender's MCS Feedback function and send response. */
#define RCR_RSVD_BIT21			BIT21	/*  Reserved */
#define RCR_RSVD_BIT20			BIT20	/*  Reserved */
#define RCR_RSVD_BIT19			BIT19	/*  Reserved */
#define RCR_TIM_PARSER_EN		BIT18	/*  RX Beacon TIM Parser. */
#define RCR_BM_DATA_EN			BIT17	/*  Broadcast data packet interrupt enable. */
#define RCR_UC_DATA_EN			BIT16	/*  Unicast data packet interrupt enable. */
#define RCR_RSVD_BIT15			BIT15	/*  Reserved */
#define RCR_HTC_LOC_CTRL		BIT14	/*  MFC<--HTC = 1 MFC-->HTC = 0 */
#define RCR_AMF					BIT13	/*  Accept management type frame */
#define RCR_ACF					BIT12	/*  Accept control type frame. Control frames BA, BAR, and PS-Poll (when in AP mode) are not controlled by this bit. They are controlled by ADF. */
#define RCR_ADF					BIT11	/*  Accept data type frame. This bit also regulates BA, BAR, and PS-Poll (AP mode only). */
#define RCR_RSVD_BIT10			BIT10	/*  Reserved */
#define RCR_AICV					BIT9		/*  Accept ICV error packet */
#define RCR_ACRC32				BIT8		/*  Accept CRC32 error packet */
#define RCR_CBSSID_BCN			BIT7		/*  Accept BSSID match packet (Rx beacon, probe rsp) */
#define RCR_CBSSID_DATA		BIT6		/*  Accept BSSID match packet (Data) */
#define RCR_CBSSID				RCR_CBSSID_DATA	/*  Accept BSSID match packet */
#define RCR_APWRMGT			BIT5		/*  Accept power management packet */
#define RCR_ADD3				BIT4		/*  Accept address 3 match packet */
#define RCR_AB					BIT3		/*  Accept broadcast packet */
#define RCR_AM					BIT2		/*  Accept multicast packet */
#define RCR_APM					BIT1		/*  Accept physical match packet */
#define RCR_AAP					BIT0		/*  Accept all unicast packet */


/*  */
/*  */
/* 	0x0000h ~ 0x00FFh	System Configuration */
/*  */
/*  */

/* 2 SYS_ISO_CTRL */
#define ISO_MD2PP				BIT(0)
#define ISO_UA2USB				BIT(1)
#define ISO_UD2CORE				BIT(2)
#define ISO_PA2PCIE				BIT(3)
#define ISO_PD2CORE				BIT(4)
#define ISO_IP2MAC				BIT(5)
#define ISO_DIOP					BIT(6)
#define ISO_DIOE					BIT(7)
#define ISO_EB2CORE				BIT(8)
#define ISO_DIOR					BIT(9)
#define PWC_EV12V				BIT(15)


/* 2 SYS_FUNC_EN */
#define FEN_BBRSTB				BIT(0)
#define FEN_BB_GLB_RSTn		BIT(1)
#define FEN_USBA				BIT(2)
#define FEN_UPLL				BIT(3)
#define FEN_USBD				BIT(4)
#define FEN_DIO_PCIE			BIT(5)
#define FEN_PCIEA				BIT(6)
#define FEN_PPLL					BIT(7)
#define FEN_PCIED				BIT(8)
#define FEN_DIOE				BIT(9)
#define FEN_CPUEN				BIT(10)
#define FEN_DCORE				BIT(11)
#define FEN_ELDR				BIT(12)
#define FEN_EN_25_1				BIT(13)
#define FEN_HWPDN				BIT(14)
#define FEN_MREGEN				BIT(15)

/* 2 APS_FSMCO */
#define PFM_LDALL				BIT(0)
#define PFM_ALDN				BIT(1)
#define PFM_LDKP				BIT(2)
#define PFM_WOWL				BIT(3)
#define EnPDN					BIT(4)
#define PDN_PL					BIT(5)
#define APFM_ONMAC				BIT(8)
#define APFM_OFF				BIT(9)
#define APFM_RSM				BIT(10)
#define AFSM_HSUS				BIT(11)
#define AFSM_PCIE				BIT(12)
#define APDM_MAC				BIT(13)
#define APDM_HOST				BIT(14)
#define APDM_HPDN				BIT(15)
#define RDY_MACON				BIT(16)
#define SUS_HOST				BIT(17)
#define ROP_ALD					BIT(20)
#define ROP_PWR					BIT(21)
#define ROP_SPS					BIT(22)
#define SOP_MRST				BIT(25)
#define SOP_FUSE				BIT(26)
#define SOP_ABG					BIT(27)
#define SOP_AMB					BIT(28)
#define SOP_RCK					BIT(29)
#define SOP_A8M					BIT(30)
#define XOP_BTCK				BIT(31)

/* 2 SYS_CLKR */
#define ANAD16V_EN				BIT(0)
#define ANA8M					BIT(1)
#define MACSLP					BIT(4)
#define LOADER_CLK_EN			BIT(5)


/* 2 9346CR /REG_SYS_EEPROM_CTRL */
#define BOOT_FROM_EEPROM		BIT(4)
#define EEPROMSEL				BIT(4)
#define EEPROM_EN				BIT(5)


/* 2 RF_CTRL */
#define RF_EN					BIT(0)
#define RF_RSTB					BIT(1)
#define RF_SDMRSTB				BIT(2)


/* 2 LDOV12D_CTRL */
#define LDV12_EN				BIT(0)
#define LDV12_SDBY				BIT(1)
#define LPLDO_HSM				BIT(2)
#define LPLDO_LSM_DIS			BIT(3)
#define _LDV12_VADJ(x)			(((x) & 0xF) << 4)



/* 2 EFUSE_TEST (For RTL8723 partially) */
#define EF_TRPT					BIT(7)
#define EF_CELL_SEL				(BIT(8)|BIT(9)) /*  00: Wifi Efuse, 01: BT Efuse0, 10: BT Efuse1, 11: BT Efuse2 */
#define LDOE25_EN				BIT(31)
#define EFUSE_SEL(x)				(((x) & 0x3) << 8)
#define EFUSE_SEL_MASK			0x300
#define EFUSE_WIFI_SEL_0		0x0
#define EFUSE_BT_SEL_0			0x1
#define EFUSE_BT_SEL_1			0x2
#define EFUSE_BT_SEL_2			0x3


/* 2 8051FWDL */
/* 2 MCUFWDL */
#define MCUFWDL_EN				BIT(0)
#define MCUFWDL_RDY			BIT(1)
#define FWDL_ChkSum_rpt		BIT(2)
#define MACINI_RDY				BIT(3)
#define BBINI_RDY				BIT(4)
#define RFINI_RDY				BIT(5)
#define WINTINI_RDY				BIT(6)
#define RAM_DL_SEL				BIT(7)
#define ROM_DLEN				BIT(19)
#define CPRST					BIT(23)


/* 2 REG_SYS_CFG */
#define XCLK_VLD				BIT(0)
#define ACLK_VLD				BIT(1)
#define UCLK_VLD				BIT(2)
#define PCLK_VLD				BIT(3)
#define PCIRSTB					BIT(4)
#define V15_VLD					BIT(5)
#define SW_OFFLOAD_EN			BIT(7)
#define SIC_IDLE					BIT(8)
#define BD_MAC2					BIT(9)
#define BD_MAC1					BIT(10)
#define IC_MACPHY_MODE		BIT(11)
#define CHIP_VER				(BIT(12)|BIT(13)|BIT(14)|BIT(15))
#define BT_FUNC					BIT(16)
#define VENDOR_ID				BIT(19)
#define EXT_VENDOR_ID			(BIT(18)|BIT(19)) /* Currently only for RTL8723B */
#define PAD_HWPD_IDN			BIT(22)
#define TRP_VAUX_EN				BIT(23)	/*  RTL ID */
#define TRP_BT_EN				BIT(24)
#define BD_PKG_SEL				BIT(25)
#define BD_HCI_SEL				BIT(26)
#define TYPE_ID					BIT(27)
#define RF_TYPE_ID				BIT(27)

#define RTL_ID					BIT(23) /*  TestChip ID, 1:Test(RLE); 0:MP(RL) */
#define SPS_SEL					BIT(24) /*  1:LDO regulator mode; 0:Switching regulator mode */


#define CHIP_VER_RTL_MASK		0xF000	/* Bit 12 ~ 15 */
#define CHIP_VER_RTL_SHIFT		12
#define EXT_VENDOR_ID_SHIFT	18

/* 2 REG_GPIO_OUTSTS (For RTL8723 only) */
#define EFS_HCI_SEL				(BIT(0)|BIT(1))
#define PAD_HCI_SEL				(BIT(2)|BIT(3))
#define HCI_SEL					(BIT(4)|BIT(5))
#define PKG_SEL_HCI				BIT(6)
#define FEN_GPS					BIT(7)
#define FEN_BT					BIT(8)
#define FEN_WL					BIT(9)
#define FEN_PCI					BIT(10)
#define FEN_USB					BIT(11)
#define BTRF_HWPDN_N			BIT(12)
#define WLRF_HWPDN_N			BIT(13)
#define PDN_BT_N				BIT(14)
#define PDN_GPS_N				BIT(15)
#define BT_CTL_HWPDN			BIT(16)
#define GPS_CTL_HWPDN			BIT(17)
#define PPHY_SUSB				BIT(20)
#define UPHY_SUSB				BIT(21)
#define PCI_SUSEN				BIT(22)
#define USB_SUSEN				BIT(23)
#define RF_RL_ID					(BIT(31)|BIT(30)|BIT(29)|BIT(28))


/*  */
/*  */
/* 	0x0100h ~ 0x01FFh	MACTOP General Configuration */
/*  */
/*  */

/* 2 Function Enable Registers */
/* 2 CR */
#define HCI_TXDMA_EN			BIT(0)
#define HCI_RXDMA_EN			BIT(1)
#define TXDMA_EN				BIT(2)
#define RXDMA_EN				BIT(3)
#define PROTOCOL_EN				BIT(4)
#define SCHEDULE_EN				BIT(5)
#define MACTXEN					BIT(6)
#define MACRXEN					BIT(7)
#define ENSWBCN					BIT(8)
#define ENSEC					BIT(9)
#define CALTMR_EN				BIT(10)	/*  32k CAL TMR enable */

/*  Network type */
#define _NETTYPE(x)				(((x) & 0x3) << 16)
#define MASK_NETTYPE			0x30000
#define NT_NO_LINK				0x0
#define NT_LINK_AD_HOC			0x1
#define NT_LINK_AP				0x2
#define NT_AS_AP				0x3

/* 2 PBP - Page Size Register */
#define GET_RX_PAGE_SIZE(value)			((value) & 0xF)
#define GET_TX_PAGE_SIZE(value)			(((value) & 0xF0) >> 4)
#define _PSRX_MASK				0xF
#define _PSTX_MASK				0xF0
#define _PSRX(x)				(x)
#define _PSTX(x)				((x) << 4)

#define PBP_64					0x0
#define PBP_128					0x1
#define PBP_256					0x2
#define PBP_512					0x3
#define PBP_1024				0x4


/* 2 TX/RXDMA */
#define RXDMA_ARBBW_EN		BIT(0)
#define RXSHFT_EN				BIT(1)
#define RXDMA_AGG_EN			BIT(2)
#define QS_VO_QUEUE			BIT(8)
#define QS_VI_QUEUE				BIT(9)
#define QS_BE_QUEUE			BIT(10)
#define QS_BK_QUEUE			BIT(11)
#define QS_MANAGER_QUEUE		BIT(12)
#define QS_HIGH_QUEUE			BIT(13)

#define HQSEL_VOQ				BIT(0)
#define HQSEL_VIQ				BIT(1)
#define HQSEL_BEQ				BIT(2)
#define HQSEL_BKQ				BIT(3)
#define HQSEL_MGTQ				BIT(4)
#define HQSEL_HIQ				BIT(5)

/*  For normal driver, 0x10C */
#define _TXDMA_CMQ_MAP(x)			(((x)&0x3) << 16)
#define _TXDMA_HIQ_MAP(x)			(((x)&0x3) << 14)
#define _TXDMA_MGQ_MAP(x)			(((x)&0x3) << 12)
#define _TXDMA_BKQ_MAP(x)			(((x)&0x3) << 10)
#define _TXDMA_BEQ_MAP(x)			(((x)&0x3) << 8)
#define _TXDMA_VIQ_MAP(x)			(((x)&0x3) << 6)
#define _TXDMA_VOQ_MAP(x)			(((x)&0x3) << 4)

#define QUEUE_EXTRA				0
#define QUEUE_LOW				1
#define QUEUE_NORMAL			2
#define QUEUE_HIGH				3


/* 2 TRXFF_BNDY */


/* 2 LLT_INIT */
#define _LLT_NO_ACTIVE				0x0
#define _LLT_WRITE_ACCESS			0x1
#define _LLT_READ_ACCESS			0x2

#define _LLT_INIT_DATA(x)			((x) & 0xFF)
#define _LLT_INIT_ADDR(x)			(((x) & 0xFF) << 8)
#define _LLT_OP(x)					(((x) & 0x3) << 30)
#define _LLT_OP_VALUE(x)			(((x) >> 30) & 0x3)


/*  */
/*  */
/* 	0x0200h ~ 0x027Fh	TXDMA Configuration */
/*  */
/*  */
/* 2 RQPN */
#define _HPQ(x)					((x) & 0xFF)
#define _LPQ(x)					(((x) & 0xFF) << 8)
#define _PUBQ(x)					(((x) & 0xFF) << 16)
#define _NPQ(x)					((x) & 0xFF)			/*  NOTE: in RQPN_NPQ register */
#define _EPQ(x)					(((x) & 0xFF) << 16)	/*  NOTE: in RQPN_EPQ register */


#define HPQ_PUBLIC_DIS			BIT(24)
#define LPQ_PUBLIC_DIS			BIT(25)
#define LD_RQPN					BIT(31)


/* 2 TDECTL */
#define BLK_DESC_NUM_SHIFT			4
#define BLK_DESC_NUM_MASK			0xF


/* 2 TXDMA_OFFSET_CHK */
#define DROP_DATA_EN				BIT(9)

/* 2 AUTO_LLT */
#define BIT_SHIFT_TXPKTNUM 24
#define BIT_MASK_TXPKTNUM 0xff
#define BIT_TXPKTNUM(x) (((x) & BIT_MASK_TXPKTNUM) << BIT_SHIFT_TXPKTNUM)

#define BIT_TDE_DBG_SEL BIT(23)
#define BIT_AUTO_INIT_LLT BIT(16)

#define BIT_SHIFT_Tx_OQT_free_space 8
#define BIT_MASK_Tx_OQT_free_space 0xff
#define BIT_Tx_OQT_free_space(x) (((x) & BIT_MASK_Tx_OQT_free_space) << BIT_SHIFT_Tx_OQT_free_space)


/*  */
/*  */
/* 	0x0280h ~ 0x028Bh	RX DMA Configuration */
/*  */
/*  */

/* 2 REG_RXDMA_CONTROL, 0x0286h */
/*  Write only. When this bit is set, RXDMA will decrease RX PKT counter by one. Before */
/*  this bit is polled, FW shall update RXFF_RD_PTR first. This register is write pulse and auto clear. */
/* define RXPKT_RELEASE_POLL			BIT(0) */
/*  Read only. When RXMA finishes on-going DMA operation, RXMDA will report idle state in */
/*  this bit. FW can start releasing packets after RXDMA entering idle mode. */
/* define RXDMA_IDLE					BIT(1) */
/*  When this bit is set, RXDMA will enter this mode after on-going RXDMA packet to host */
/*  completed, and stop DMA packet to host. RXDMA will then report Default: 0; */
/* define RW_RELEASE_EN				BIT(2) */

/* 2 REG_RXPKT_NUM, 0x0284 */
#define		RXPKT_RELEASE_POLL	BIT(16)
#define	RXDMA_IDLE				BIT(17)
#define	RW_RELEASE_EN			BIT(18)

/*  */
/*  */
/* 	0x0400h ~ 0x047Fh	Protocol Configuration */
/*  */
/*  */
/* 2 FWHW_TXQ_CTRL */
#define EN_AMPDU_RTY_NEW			BIT(7)


/* 2 SPEC SIFS */
#define _SPEC_SIFS_CCK(x)			((x) & 0xFF)
#define _SPEC_SIFS_OFDM(x)			(((x) & 0xFF) << 8)

/* 2 RL */
#define	RETRY_LIMIT_SHORT_SHIFT			8
#define	RETRY_LIMIT_LONG_SHIFT			0

/*  */
/*  */
/* 	0x0500h ~ 0x05FFh	EDCA Configuration */
/*  */
/*  */

/* 2 EDCA setting */
#define AC_PARAM_TXOP_LIMIT_OFFSET		16
#define AC_PARAM_ECW_MAX_OFFSET			12
#define AC_PARAM_ECW_MIN_OFFSET			8
#define AC_PARAM_AIFS_OFFSET				0


#define _LRL(x)					((x) & 0x3F)
#define _SRL(x)					(((x) & 0x3F) << 8)


/* 2 BCN_CTRL */
#define EN_TXBCN_RPT			BIT(2)
#define EN_BCN_FUNCTION		BIT(3)
#define STOP_BCNQ				BIT(6)
#define DIS_RX_BSSID_FIT		BIT(6)

#define DIS_ATIM					BIT(0)
#define DIS_BCNQ_SUB			BIT(1)
#define DIS_TSF_UDT				BIT(4)

/*  The same function but different bit field. */
#define DIS_TSF_UDT0_NORMAL_CHIP	BIT(4)
#define DIS_TSF_UDT0_TEST_CHIP	BIT(5)


/* 2 ACMHWCTRL */
#define AcmHw_HwEn				BIT(0)
#define AcmHw_BeqEn			BIT(1)
#define AcmHw_ViqEn				BIT(2)
#define AcmHw_VoqEn			BIT(3)
#define AcmHw_BeqStatus		BIT(4)
#define AcmHw_ViqStatus			BIT(5)
#define AcmHw_VoqStatus		BIT(6)

/* 2 REG_DUAL_TSF_RST (0x553) */
#define DUAL_TSF_RST_P2P		BIT(4)

/* 2  REG_NOA_DESC_SEL (0x5CF) */
#define NOA_DESC_SEL_0			0
#define NOA_DESC_SEL_1			BIT(4)

/*  */
/*  */
/* 	0x0600h ~ 0x07FFh	WMAC Configuration */
/*  */
/*  */

/* 2 APSD_CTRL */
#define APSDOFF					BIT(6)

/* 2 TCR */
#define TSFRST					BIT(0)
#define DIS_GCLK					BIT(1)
#define PAD_SEL					BIT(2)
#define PWR_ST					BIT(6)
#define PWRBIT_OW_EN			BIT(7)
#define ACRC						BIT(8)
#define CFENDFORM				BIT(9)
#define ICV						BIT(10)


/* 2 RCR */
#define AAP						BIT(0)
#define APM						BIT(1)
#define AM						BIT(2)
#define AB						BIT(3)
#define ADD3						BIT(4)
#define APWRMGT				BIT(5)
#define CBSSID					BIT(6)
#define CBSSID_DATA				BIT(6)
#define CBSSID_BCN				BIT(7)
#define ACRC32					BIT(8)
#define AICV						BIT(9)
#define ADF						BIT(11)
#define ACF						BIT(12)
#define AMF						BIT(13)
#define HTC_LOC_CTRL			BIT(14)
#define UC_DATA_EN				BIT(16)
#define BM_DATA_EN				BIT(17)
#define MFBEN					BIT(22)
#define LSIGEN					BIT(23)
#define EnMBID					BIT(24)
#define FORCEACK				BIT(26)
#define APP_BASSN				BIT(27)
#define APP_PHYSTS				BIT(28)
#define APP_ICV					BIT(29)
#define APP_MIC					BIT(30)
#define APP_FCS					BIT(31)


/* 2 SECCFG */
#define SCR_TxUseDK				BIT(0)			/* Force Tx Use Default Key */
#define SCR_RxUseDK				BIT(1)			/* Force Rx Use Default Key */
#define SCR_TxEncEnable			BIT(2)			/* Enable Tx Encryption */
#define SCR_RxDecEnable			BIT(3)			/* Enable Rx Decryption */
#define SCR_SKByA2				BIT(4)			/* Search kEY BY A2 */
#define SCR_NoSKMC				BIT(5)			/* No Key Search Multicast */
#define SCR_TXBCUSEDK			BIT(6)			/*  Force Tx Broadcast packets Use Default Key */
#define SCR_RXBCUSEDK			BIT(7)			/*  Force Rx Broadcast packets Use Default Key */
#define SCR_CHK_KEYID			BIT(8)

/*  */
/*  */
/* 	SDIO Bus Specification */
/*  */
/*  */

/*  I/O bus domain address mapping */
#define SDIO_LOCAL_BASE		0x10250000
#define WLAN_IOREG_BASE		0x10260000
#define FIRMWARE_FIFO_BASE	0x10270000
#define TX_HIQ_BASE				0x10310000
#define TX_MIQ_BASE				0x10320000
#define TX_LOQ_BASE				0x10330000
#define TX_EPQ_BASE				0x10350000
#define RX_RX0FF_BASE			0x10340000

/* SDIO host local register space mapping. */
#define SDIO_LOCAL_MSK				0x0FFF
#define WLAN_IOREG_MSK			0x7FFF
#define WLAN_FIFO_MSK				0x1FFF	/*  Aggregation Length[12:0] */
#define WLAN_RX0FF_MSK				0x0003

#define SDIO_WITHOUT_REF_DEVICE_ID	0	/*  Without reference to the SDIO Device ID */
#define SDIO_LOCAL_DEVICE_ID			0	/*  0b[16], 000b[15:13] */
#define WLAN_TX_HIQ_DEVICE_ID			4	/*  0b[16], 100b[15:13] */
#define WLAN_TX_MIQ_DEVICE_ID		5	/*  0b[16], 101b[15:13] */
#define WLAN_TX_LOQ_DEVICE_ID		6	/*  0b[16], 110b[15:13] */
#define WLAN_TX_EXQ_DEVICE_ID		3	/*  0b[16], 011b[15:13] */
#define WLAN_RX0FF_DEVICE_ID			7	/*  0b[16], 111b[15:13] */
#define WLAN_IOREG_DEVICE_ID			8	/*  1b[16] */

/* SDIO Tx Free Page Index */
#define HI_QUEUE_IDX				0
#define MID_QUEUE_IDX				1
#define LOW_QUEUE_IDX				2
#define PUBLIC_QUEUE_IDX			3

#define SDIO_MAX_TX_QUEUE			3		/*  HIQ, MIQ and LOQ */
#define SDIO_MAX_RX_QUEUE			1

#define SDIO_REG_TX_CTRL			0x0000 /*  SDIO Tx Control */
#define SDIO_REG_HIMR				0x0014 /*  SDIO Host Interrupt Mask */
#define SDIO_REG_HISR				0x0018 /*  SDIO Host Interrupt Service Routine */
#define SDIO_REG_HCPWM			0x0019 /*  HCI Current Power Mode */
#define SDIO_REG_RX0_REQ_LEN		0x001C /*  RXDMA Request Length */
#define SDIO_REG_OQT_FREE_PG		0x001E /*  OQT Free Page */
#define SDIO_REG_FREE_TXPG			0x0020 /*  Free Tx Buffer Page */
#define SDIO_REG_HCPWM1			0x0024 /*  HCI Current Power Mode 1 */
#define SDIO_REG_HCPWM2			0x0026 /*  HCI Current Power Mode 2 */
#define SDIO_REG_FREE_TXPG_SEQ	0x0028 /*  Free Tx Page Sequence */
#define SDIO_REG_HTSFR_INFO		0x0030 /*  HTSF Informaion */
#define SDIO_REG_HRPWM1			0x0080 /*  HCI Request Power Mode 1 */
#define SDIO_REG_HRPWM2			0x0082 /*  HCI Request Power Mode 2 */
#define SDIO_REG_HPS_CLKR			0x0084 /*  HCI Power Save Clock */
#define SDIO_REG_HSUS_CTRL			0x0086 /*  SDIO HCI Suspend Control */
#define SDIO_REG_HIMR_ON			0x0090 /* SDIO Host Extension Interrupt Mask Always */
#define SDIO_REG_HISR_ON			0x0091 /* SDIO Host Extension Interrupt Status Always */

#define SDIO_HIMR_DISABLED			0

/*  RTL8723/RTL8188E SDIO Host Interrupt Mask Register */
#define SDIO_HIMR_RX_REQUEST_MSK		BIT0
#define SDIO_HIMR_AVAL_MSK			BIT1
#define SDIO_HIMR_TXERR_MSK			BIT2
#define SDIO_HIMR_RXERR_MSK			BIT3
#define SDIO_HIMR_TXFOVW_MSK			BIT4
#define SDIO_HIMR_RXFOVW_MSK			BIT5
#define SDIO_HIMR_TXBCNOK_MSK			BIT6
#define SDIO_HIMR_TXBCNERR_MSK		BIT7
#define SDIO_HIMR_BCNERLY_INT_MSK		BIT16
#define SDIO_HIMR_C2HCMD_MSK			BIT17
#define SDIO_HIMR_CPWM1_MSK			BIT18
#define SDIO_HIMR_CPWM2_MSK			BIT19
#define SDIO_HIMR_HSISR_IND_MSK		BIT20
#define SDIO_HIMR_GTINT3_IND_MSK		BIT21
#define SDIO_HIMR_GTINT4_IND_MSK		BIT22
#define SDIO_HIMR_PSTIMEOUT_MSK		BIT23
#define SDIO_HIMR_OCPINT_MSK			BIT24
#define SDIO_HIMR_ATIMEND_MSK			BIT25
#define SDIO_HIMR_ATIMEND_E_MSK		BIT26
#define SDIO_HIMR_CTWEND_MSK			BIT27

/* RTL8188E SDIO Specific */
#define SDIO_HIMR_MCU_ERR_MSK			BIT28
#define SDIO_HIMR_TSF_BIT32_TOGGLE_MSK		BIT29

/*  SDIO Host Interrupt Service Routine */
#define SDIO_HISR_RX_REQUEST			BIT0
#define SDIO_HISR_AVAL					BIT1
#define SDIO_HISR_TXERR					BIT2
#define SDIO_HISR_RXERR					BIT3
#define SDIO_HISR_TXFOVW				BIT4
#define SDIO_HISR_RXFOVW				BIT5
#define SDIO_HISR_TXBCNOK				BIT6
#define SDIO_HISR_TXBCNERR				BIT7
#define SDIO_HISR_BCNERLY_INT			BIT16
#define SDIO_HISR_C2HCMD				BIT17
#define SDIO_HISR_CPWM1				BIT18
#define SDIO_HISR_CPWM2				BIT19
#define SDIO_HISR_HSISR_IND			BIT20
#define SDIO_HISR_GTINT3_IND			BIT21
#define SDIO_HISR_GTINT4_IND			BIT22
#define SDIO_HISR_PSTIMEOUT			BIT23
#define SDIO_HISR_OCPINT				BIT24
#define SDIO_HISR_ATIMEND				BIT25
#define SDIO_HISR_ATIMEND_E			BIT26
#define SDIO_HISR_CTWEND				BIT27

/* RTL8188E SDIO Specific */
#define SDIO_HISR_MCU_ERR				BIT28
#define SDIO_HISR_TSF_BIT32_TOGGLE	BIT29

#define MASK_SDIO_HISR_CLEAR		(SDIO_HISR_TXERR |\
									SDIO_HISR_RXERR |\
									SDIO_HISR_TXFOVW |\
									SDIO_HISR_RXFOVW |\
									SDIO_HISR_TXBCNOK |\
									SDIO_HISR_TXBCNERR |\
									SDIO_HISR_C2HCMD |\
									SDIO_HISR_CPWM1 |\
									SDIO_HISR_CPWM2 |\
									SDIO_HISR_HSISR_IND |\
									SDIO_HISR_GTINT3_IND |\
									SDIO_HISR_GTINT4_IND |\
									SDIO_HISR_PSTIMEOUT |\
									SDIO_HISR_OCPINT)

/*  SDIO HCI Suspend Control Register */
#define HCI_RESUME_PWR_RDY			BIT1
#define HCI_SUS_CTRL					BIT0

/*  SDIO Tx FIFO related */
#define SDIO_TX_FREE_PG_QUEUE			4	/*  The number of Tx FIFO free page */
#define SDIO_TX_FIFO_PAGE_SZ			128

#define MAX_TX_AGG_PACKET_NUMBER	0x8

/*  */
/*  */
/* 	0xFE00h ~ 0xFE55h	USB Configuration */
/*  */
/*  */

/* 2 USB Information (0xFE17) */
#define USB_IS_HIGH_SPEED			0
#define USB_IS_FULL_SPEED			1
#define USB_SPEED_MASK				BIT(5)

#define USB_NORMAL_SIE_EP_MASK	0xF
#define USB_NORMAL_SIE_EP_SHIFT	4

/* 2 Special Option */
#define USB_AGG_EN				BIT(3)

/*  0; Use interrupt endpoint to upload interrupt pkt */
/*  1; Use bulk endpoint to upload interrupt pkt, */
#define INT_BULK_SEL			BIT(4)

/* 2REG_C2HEVT_CLEAR */
#define C2H_EVT_HOST_CLOSE		0x00	/*  Set by driver and notify FW that the driver has read the C2H command message */
#define C2H_EVT_FW_CLOSE		0xFF	/*  Set by FW indicating that FW had set the C2H command message and it's not yet read by driver. */


/* 2REG_MULTI_FUNC_CTRL(For RTL8723 Only) */
#define WL_HWPDN_EN			BIT0	/*  Enable GPIO[9] as WiFi HW PDn source */
#define WL_HWPDN_SL			BIT1	/*  WiFi HW PDn polarity control */
#define WL_FUNC_EN				BIT2	/*  WiFi function enable */
#define WL_HWROF_EN			BIT3	/*  Enable GPIO[9] as WiFi RF HW PDn source */
#define BT_HWPDN_EN			BIT16	/*  Enable GPIO[11] as BT HW PDn source */
#define BT_HWPDN_SL			BIT17	/*  BT HW PDn polarity control */
#define BT_FUNC_EN				BIT18	/*  BT function enable */
#define BT_HWROF_EN			BIT19	/*  Enable GPIO[11] as BT/GPS RF HW PDn source */
#define GPS_HWPDN_EN			BIT20	/*  Enable GPIO[10] as GPS HW PDn source */
#define GPS_HWPDN_SL			BIT21	/*  GPS HW PDn polarity control */
#define GPS_FUNC_EN			BIT22	/*  GPS function enable */

/* 3 REG_LIFECTRL_CTRL */
#define HAL92C_EN_PKT_LIFE_TIME_BK		BIT3
#define HAL92C_EN_PKT_LIFE_TIME_BE		BIT2
#define HAL92C_EN_PKT_LIFE_TIME_VI		BIT1
#define HAL92C_EN_PKT_LIFE_TIME_VO		BIT0

#define HAL92C_MSDU_LIFE_TIME_UNIT		128	/*  in us, said by Tim. */

/* 2 8192D PartNo. */
#define PARTNO_92D_NIC							(BIT7|BIT6)
#define PARTNO_92D_NIC_REMARK				(BIT5|BIT4)
#define PARTNO_SINGLE_BAND_VS				BIT3
#define PARTNO_SINGLE_BAND_VS_REMARK		BIT1
#define PARTNO_CONCURRENT_BAND_VC			(BIT3|BIT2)
#define PARTNO_CONCURRENT_BAND_VC_REMARK	(BIT1|BIT0)

/*  */
/*  General definitions */
/*  */

#define LAST_ENTRY_OF_TX_PKT_BUFFER_8188E		176
#define LAST_ENTRY_OF_TX_PKT_BUFFER_8812			255
#define LAST_ENTRY_OF_TX_PKT_BUFFER_8723B		255
#define LAST_ENTRY_OF_TX_PKT_BUFFER_8192C		255
#define LAST_ENTRY_OF_TX_PKT_BUFFER_DUAL_MAC	127

#define POLLING_LLT_THRESHOLD				20
#define POLLING_READY_TIMEOUT_COUNT		1000


/*  GPIO BIT */
#define	HAL_8192C_HW_GPIO_WPS_BIT	BIT2
#define	HAL_8192EU_HW_GPIO_WPS_BIT	BIT7
#define	HAL_8188E_HW_GPIO_WPS_BIT	BIT7

#endif /* __HAL_COMMON_H__ */
