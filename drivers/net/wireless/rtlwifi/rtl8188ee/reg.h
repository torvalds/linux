/******************************************************************************
 *
 * Copyright(c) 2009-2013  Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

#ifndef __RTL92C_REG_H__
#define __RTL92C_REG_H__

#define TXPKT_BUF_SELECT				0x69
#define RXPKT_BUF_SELECT				0xA5
#define DISABLE_TRXPKT_BUF_ACCESS			0x0

#define REG_SYS_ISO_CTRL			0x0000
#define REG_SYS_FUNC_EN				0x0002
#define REG_APS_FSMCO				0x0004
#define REG_SYS_CLKR				0x0008
#define REG_9346CR					0x000A
#define REG_EE_VPD					0x000C
#define REG_AFE_MISC				0x0010
#define REG_SPS0_CTRL				0x0011
#define REG_SPS_OCP_CFG				0x0018
#define REG_RSV_CTRL				0x001C
#define REG_RF_CTRL					0x001F
#define REG_LDOA15_CTRL				0x0020
#define REG_LDOV12D_CTRL			0x0021
#define REG_LDOHCI12_CTRL			0x0022
#define REG_LPLDO_CTRL				0x0023
#define REG_AFE_XTAL_CTRL			0x0024
/* 1.5v for 8188EE test chip, 1.4v for MP chip */
#define REG_AFE_LDO_CTRL			0x0027
#define REG_AFE_PLL_CTRL			0x0028
#define REG_EFUSE_CTRL				0x0030
#define REG_EFUSE_TEST				0x0034
#define REG_PWR_DATA				0x0038
#define REG_CAL_TIMER				0x003C
#define REG_ACLK_MON				0x003E
#define REG_GPIO_MUXCFG			0x0040
#define REG_GPIO_IO_SEL				0x0042
#define REG_MAC_PINMUX_CFG		0x0043
#define REG_GPIO_PIN_CTRL			0x0044
#define REG_GPIO_INTM				0x0048
#define REG_LEDCFG0					0x004C
#define REG_LEDCFG1					0x004D
#define REG_LEDCFG2					0x004E
#define REG_LEDCFG3					0x004F
#define REG_FSIMR					0x0050
#define REG_FSISR					0x0054
#define REG_HSIMR					0x0058
#define REG_HSISR					0x005c
#define REG_GPIO_PIN_CTRL_2		0x0060
#define REG_GPIO_IO_SEL_2			0x0062
#define REG_GPIO_OUTPUT			0x006c
#define REG_AFE_XTAL_CTRL_EXT		0x0078
#define REG_XCK_OUT_CTRL			0x007c
#define REG_MCUFWDL				0x0080
#define REG_WOL_EVENT				0x0081
#define REG_MCUTSTCFG				0x0084

#define REG_HIMR					0x00B0
#define REG_HISR					0x00B4
#define REG_HIMRE					0x00B8
#define REG_HISRE					0x00BC

#define REG_EFUSE_ACCESS			0x00CF

#define REG_BIST_SCAN				0x00D0
#define REG_BIST_RPT				0x00D4
#define REG_BIST_ROM_RPT			0x00D8
#define REG_USB_SIE_INTF			0x00E0
#define REG_PCIE_MIO_INTF			0x00E4
#define REG_PCIE_MIO_INTD			0x00E8
#define REG_HPON_FSM				0x00EC
#define REG_SYS_CFG					0x00F0

#define REG_CR						0x0100
#define REG_PBP						0x0104
#define REG_PKT_BUFF_ACCESS_CTRL	0x0106
#define REG_TRXDMA_CTRL				0x010C
#define REG_TRXFF_BNDY				0x0114
#define REG_TRXFF_STATUS			0x0118
#define REG_RXFF_PTR				0x011C

#define REG_CPWM					0x012F
#define REG_FWIMR					0x0130
#define REG_FWISR					0x0134
#define REG_PKTBUF_DBG_CTRL			0x0140
#define REG_PKTBUF_DBG_DATA_L		0x0144
#define REG_PKTBUF_DBG_DATA_H		0x0148
#define REG_RXPKTBUF_CTRL	(REG_PKTBUF_DBG_CTRL+2)

#define REG_TC0_CTRL				0x0150
#define REG_TC1_CTRL				0x0154
#define REG_TC2_CTRL				0x0158
#define REG_TC3_CTRL				0x015C
#define REG_TC4_CTRL				0x0160
#define REG_TCUNIT_BASE				0x0164
#define REG_MBIST_START				0x0174
#define REG_MBIST_DONE				0x0178
#define REG_MBIST_FAIL				0x017C
#define REG_32K_CTRL					0x0194
#define REG_C2HEVT_MSG_NORMAL		0x01A0
#define REG_C2HEVT_CLEAR			0x01AF
#define REG_C2HEVT_MSG_TEST			0x01B8
#define REG_MCUTST_1				0x01c0
#define REG_FMETHR					0x01C8
#define REG_HMETFR					0x01CC
#define REG_HMEBOX_0				0x01D0
#define REG_HMEBOX_1				0x01D4
#define REG_HMEBOX_2				0x01D8
#define REG_HMEBOX_3				0x01DC

#define REG_LLT_INIT				0x01E0
#define REG_BB_ACCEESS_CTRL			0x01E8
#define REG_BB_ACCESS_DATA			0x01EC

#define REG_HMEBOX_EXT_0			0x01F0
#define REG_HMEBOX_EXT_1			0x01F4
#define REG_HMEBOX_EXT_2			0x01F8
#define REG_HMEBOX_EXT_3			0x01FC

#define REG_RQPN					0x0200
#define REG_FIFOPAGE				0x0204
#define REG_TDECTRL					0x0208
#define REG_TXDMA_OFFSET_CHK		0x020C
#define REG_TXDMA_STATUS			0x0210
#define REG_RQPN_NPQ				0x0214

#define REG_RXDMA_AGG_PG_TH		0x0280
/* FW shall update this register before
 * FW write RXPKT_RELEASE_POLL to 1
 */
#define REG_FW_UPD_RDPTR			0x0284
/* Control the RX DMA.*/
#define REG_RXDMA_CONTROL			0x0286
/* The number of packets in RXPKTBUF.	*/
#define REG_RXPKT_NUM				0x0287

#define	REG_PCIE_CTRL_REG			0x0300
#define	REG_INT_MIG					0x0304
#define	REG_BCNQ_DESA				0x0308
#define	REG_HQ_DESA					0x0310
#define	REG_MGQ_DESA				0x0318
#define	REG_VOQ_DESA				0x0320
#define	REG_VIQ_DESA				0x0328
#define	REG_BEQ_DESA				0x0330
#define	REG_BKQ_DESA				0x0338
#define	REG_RX_DESA					0x0340

#define	REG_DBI						0x0348
#define	REG_MDIO					0x0354
#define	REG_DBG_SEL					0x0360
#define	REG_PCIE_HRPWM				0x0361
#define	REG_PCIE_HCPWM				0x0363
#define	REG_UART_CTRL				0x0364
#define	REG_WATCH_DOG				0x0368
#define	REG_UART_TX_DESA			0x0370
#define	REG_UART_RX_DESA			0x0378

#define	REG_HDAQ_DESA_NODEF			0x0000
#define	REG_CMDQ_DESA_NODEF			0x0000

#define REG_VOQ_INFORMATION			0x0400
#define REG_VIQ_INFORMATION			0x0404
#define REG_BEQ_INFORMATION			0x0408
#define REG_BKQ_INFORMATION			0x040C
#define REG_MGQ_INFORMATION			0x0410
#define REG_HGQ_INFORMATION			0x0414
#define REG_BCNQ_INFORMATION		0x0418
#define REG_TXPKT_EMPTY				0x041A

#define REG_CPU_MGQ_INFORMATION		0x041C
#define REG_FWHW_TXQ_CTRL			0x0420
#define REG_HWSEQ_CTRL				0x0423
#define REG_TXPKTBUF_BCNQ_BDNY		0x0424
#define REG_TXPKTBUF_MGQ_BDNY		0x0425
#define REG_MULTI_BCNQ_EN			0x0426
#define REG_MULTI_BCNQ_OFFSET		0x0427
#define REG_SPEC_SIFS				0x0428
#define REG_RL						0x042A
#define REG_DARFRC					0x0430
#define REG_RARFRC					0x0438
#define REG_RRSR					0x0440
#define REG_ARFR0					0x0444
#define REG_ARFR1					0x0448
#define REG_ARFR2					0x044C
#define REG_ARFR3					0x0450
#define REG_AGGLEN_LMT				0x0458
#define REG_AMPDU_MIN_SPACE			0x045C
#define REG_TXPKTBUF_WMAC_LBK_BF_HD	0x045D
#define REG_FAST_EDCA_CTRL			0x0460
#define REG_RD_RESP_PKT_TH			0x0463
#define REG_INIRTS_RATE_SEL			0x0480
#define REG_INIDATA_RATE_SEL		0x0484
#define REG_POWER_STATUS			0x04A4
#define REG_POWER_STAGE1			0x04B4
#define REG_POWER_STAGE2			0x04B8
#define REG_PKT_LIFE_TIME			0x04C0
#define REG_STBC_SETTING			0x04C4
#define REG_PROT_MODE_CTRL			0x04C8
#define REG_BAR_MODE_CTRL			0x04CC
#define REG_RA_TRY_RATE_AGG_LMT		0x04CF
#define REG_EARLY_MODE_CONTROL		0x04D0
#define REG_NQOS_SEQ				0x04DC
#define REG_QOS_SEQ					0x04DE
#define REG_NEED_CPU_HANDLE			0x04E0
#define REG_PKT_LOSE_RPT			0x04E1
#define REG_PTCL_ERR_STATUS			0x04E2
#define REG_TX_RPT_CTRL				0x04EC
#define REG_TX_RPT_TIME				0x04F0
#define REG_DUMMY					0x04FC

#define REG_EDCA_VO_PARAM			0x0500
#define REG_EDCA_VI_PARAM			0x0504
#define REG_EDCA_BE_PARAM			0x0508
#define REG_EDCA_BK_PARAM			0x050C
#define REG_BCNTCFG					0x0510
#define REG_PIFS					0x0512
#define REG_RDG_PIFS				0x0513
#define REG_SIFS_CTX				0x0514
#define REG_SIFS_TRX				0x0516
#define REG_AGGR_BREAK_TIME			0x051A
#define REG_SLOT					0x051B
#define REG_TX_PTCL_CTRL			0x0520
#define REG_TXPAUSE					0x0522
#define REG_DIS_TXREQ_CLR			0x0523
#define REG_RD_CTRL					0x0524
#define REG_TBTT_PROHIBIT			0x0540
#define REG_RD_NAV_NXT				0x0544
#define REG_NAV_PROT_LEN			0x0546
#define REG_BCN_CTRL				0x0550
#define REG_USTIME_TSF				0x0551
#define REG_MBID_NUM				0x0552
#define REG_DUAL_TSF_RST			0x0553
#define REG_BCN_INTERVAL			0x0554
#define REG_MBSSID_BCN_SPACE		0x0554
#define REG_DRVERLYINT				0x0558
#define REG_BCNDMATIM				0x0559
#define REG_ATIMWND					0x055A
#define REG_BCN_MAX_ERR				0x055D
#define REG_RXTSF_OFFSET_CCK		0x055E
#define REG_RXTSF_OFFSET_OFDM		0x055F
#define REG_TSFTR					0x0560
#define REG_INIT_TSFTR				0x0564
#define REG_PSTIMER					0x0580
#define REG_TIMER0					0x0584
#define REG_TIMER1					0x0588
#define REG_ACMHWCTRL				0x05C0
#define REG_ACMRSTCTRL				0x05C1
#define REG_ACMAVG					0x05C2
#define REG_VO_ADMTIME				0x05C4
#define REG_VI_ADMTIME				0x05C6
#define REG_BE_ADMTIME				0x05C8
#define REG_EDCA_RANDOM_GEN			0x05CC
#define REG_SCH_TXCMD				0x05D0

#define REG_APSD_CTRL				0x0600
#define REG_BWOPMODE				0x0603
#define REG_TCR						0x0604
#define REG_RCR						0x0608
#define REG_RX_PKT_LIMIT			0x060C
#define REG_RX_DLK_TIME				0x060D
#define REG_RX_DRVINFO_SZ			0x060F

#define REG_MACID					0x0610
#define REG_BSSID					0x0618
#define REG_MAR						0x0620
#define REG_MBIDCAMCFG				0x0628

#define REG_USTIME_EDCA				0x0638
#define REG_MAC_SPEC_SIFS			0x063A
#define REG_RESP_SIFS_CCK			0x063C
#define REG_RESP_SIFS_OFDM			0x063E
#define REG_ACKTO					0x0640
#define REG_CTS2TO					0x0641
#define REG_EIFS					0x0642

#define REG_NAV_CTRL				0x0650
#define REG_BACAMCMD				0x0654
#define REG_BACAMCONTENT			0x0658
#define REG_LBDLY					0x0660
#define REG_FWDLY					0x0661
#define REG_RXERR_RPT				0x0664
#define REG_TRXPTCL_CTL				0x0668

#define REG_CAMCMD					0x0670
#define REG_CAMWRITE				0x0674
#define REG_CAMREAD					0x0678
#define REG_CAMDBG					0x067C
#define REG_SECCFG					0x0680

#define REG_WOW_CTRL				0x0690
#define REG_PSSTATUS				0x0691
#define REG_PS_RX_INFO				0x0692
#define REG_UAPSD_TID				0x0693
#define REG_LPNAV_CTRL				0x0694
#define REG_WKFMCAM_NUM				0x0698
#define REG_WKFMCAM_RWD				0x069C
#define REG_RXFLTMAP0				0x06A0
#define REG_RXFLTMAP1				0x06A2
#define REG_RXFLTMAP2				0x06A4
#define REG_BCN_PSR_RPT				0x06A8
#define REG_CALB32K_CTRL			0x06AC
#define REG_PKT_MON_CTRL			0x06B4
#define REG_BT_COEX_TABLE			0x06C0
#define REG_WMAC_RESP_TXINFO		0x06D8

#define REG_USB_INFO				0xFE17
#define REG_USB_SPECIAL_OPTION		0xFE55
#define REG_USB_DMA_AGG_TO			0xFE5B
#define REG_USB_AGG_TO				0xFE5C
#define REG_USB_AGG_TH				0xFE5D

#define REG_TEST_USB_TXQS			0xFE48
#define REG_TEST_SIE_VID			0xFE60
#define REG_TEST_SIE_PID			0xFE62
#define REG_TEST_SIE_OPTIONAL		0xFE64
#define REG_TEST_SIE_CHIRP_K		0xFE65
#define REG_TEST_SIE_PHY			0xFE66
#define REG_TEST_SIE_MAC_ADDR		0xFE70
#define REG_TEST_SIE_STRING			0xFE80

#define REG_NORMAL_SIE_VID			0xFE60
#define REG_NORMAL_SIE_PID			0xFE62
#define REG_NORMAL_SIE_OPTIONAL		0xFE64
#define REG_NORMAL_SIE_EP			0xFE65
#define REG_NORMAL_SIE_PHY			0xFE68
#define REG_NORMAL_SIE_MAC_ADDR		0xFE70
#define REG_NORMAL_SIE_STRING		0xFE80

#define	CR9346				REG_9346CR
#define	MSR				(REG_CR + 2)
#define	ISR				REG_HISR
#define	TSFR				REG_TSFTR

#define	MACIDR0				REG_MACID
#define	MACIDR4				(REG_MACID + 4)

#define PBP				REG_PBP

#define	IDR0				MACIDR0
#define	IDR4				MACIDR4

#define	UNUSED_REGISTER			0x1BF
#define	DCAM				UNUSED_REGISTER
#define	PSR				UNUSED_REGISTER
#define BBADDR				UNUSED_REGISTER
#define	PHYDATAR			UNUSED_REGISTER

#define	INVALID_BBRF_VALUE		0x12345678

#define	MAX_MSS_DENSITY_2T		0x13
#define	MAX_MSS_DENSITY_1T		0x0A

#define	CMDEEPROM_EN			BIT(5)
#define	CMDEEPROM_SEL			BIT(4)
#define	CMD9346CR_9356SEL		BIT(4)
#define	AUTOLOAD_EEPROM			(CMDEEPROM_EN|CMDEEPROM_SEL)
#define	AUTOLOAD_EFUSE			CMDEEPROM_EN

#define	GPIOSEL_GPIO			0
#define	GPIOSEL_ENBT			BIT(5)

#define	GPIO_IN				REG_GPIO_PIN_CTRL
#define	GPIO_OUT			(REG_GPIO_PIN_CTRL+1)
#define	GPIO_IO_SEL			(REG_GPIO_PIN_CTRL+2)
#define	GPIO_MOD			(REG_GPIO_PIN_CTRL+3)

/*8723/8188E Host System Interrupt
 *Mask Register (offset 0x58, 32 byte)
 */
#define	HSIMR_GPIO12_0_INT_EN			BIT(0)
#define	HSIMR_SPS_OCP_INT_EN			BIT(5)
#define	HSIMR_RON_INT_EN			BIT(6)
#define	HSIMR_PDN_INT_EN			BIT(7)
#define	HSIMR_GPIO9_INT_EN			BIT(25)

/*       8723/8188E Host System Interrupt
 *		Status Register (offset 0x5C, 32 byte)
 */
#define	HSISR_GPIO12_0_INT			BIT(0)
#define	HSISR_SPS_OCP_INT			BIT(5)
#define	HSISR_RON_INT_EN			BIT(6)
#define	HSISR_PDNINT				BIT(7)
#define	HSISR_GPIO9_INT				BIT(25)

#define	MSR_NOLINK					0x00
#define	MSR_ADHOC					0x01
#define	MSR_INFRA					0x02
#define	MSR_AP						0x03

#define	RRSR_RSC_OFFSET				21
#define	RRSR_SHORT_OFFSET			23
#define	RRSR_RSC_BW_40M				0x600000
#define	RRSR_RSC_UPSUBCHNL			0x400000
#define	RRSR_RSC_LOWSUBCHNL			0x200000
#define	RRSR_SHORT					0x800000
#define	RRSR_1M						BIT(0)
#define	RRSR_2M						BIT(1)
#define	RRSR_5_5M					BIT(2)
#define	RRSR_11M					BIT(3)
#define	RRSR_6M						BIT(4)
#define	RRSR_9M						BIT(5)
#define	RRSR_12M					BIT(6)
#define	RRSR_18M					BIT(7)
#define	RRSR_24M					BIT(8)
#define	RRSR_36M					BIT(9)
#define	RRSR_48M					BIT(10)
#define	RRSR_54M					BIT(11)
#define	RRSR_MCS0					BIT(12)
#define	RRSR_MCS1					BIT(13)
#define	RRSR_MCS2					BIT(14)
#define	RRSR_MCS3					BIT(15)
#define	RRSR_MCS4					BIT(16)
#define	RRSR_MCS5					BIT(17)
#define	RRSR_MCS6					BIT(18)
#define	RRSR_MCS7					BIT(19)
#define	BRSR_ACKSHORTPMB			BIT(23)

#define	RATR_1M						0x00000001
#define	RATR_2M						0x00000002
#define	RATR_55M					0x00000004
#define	RATR_11M					0x00000008
#define	RATR_6M						0x00000010
#define	RATR_9M						0x00000020
#define	RATR_12M					0x00000040
#define	RATR_18M					0x00000080
#define	RATR_24M					0x00000100
#define	RATR_36M					0x00000200
#define	RATR_48M					0x00000400
#define	RATR_54M					0x00000800
#define	RATR_MCS0					0x00001000
#define	RATR_MCS1					0x00002000
#define	RATR_MCS2					0x00004000
#define	RATR_MCS3					0x00008000
#define	RATR_MCS4					0x00010000
#define	RATR_MCS5					0x00020000
#define	RATR_MCS6					0x00040000
#define	RATR_MCS7					0x00080000
#define	RATR_MCS8					0x00100000
#define	RATR_MCS9					0x00200000
#define	RATR_MCS10					0x00400000
#define	RATR_MCS11					0x00800000
#define	RATR_MCS12					0x01000000
#define	RATR_MCS13					0x02000000
#define	RATR_MCS14					0x04000000
#define	RATR_MCS15					0x08000000

#define RATE_1M						BIT(0)
#define RATE_2M						BIT(1)
#define RATE_5_5M					BIT(2)
#define RATE_11M					BIT(3)
#define RATE_6M						BIT(4)
#define RATE_9M						BIT(5)
#define RATE_12M					BIT(6)
#define RATE_18M					BIT(7)
#define RATE_24M					BIT(8)
#define RATE_36M					BIT(9)
#define RATE_48M					BIT(10)
#define RATE_54M					BIT(11)
#define RATE_MCS0					BIT(12)
#define RATE_MCS1					BIT(13)
#define RATE_MCS2					BIT(14)
#define RATE_MCS3					BIT(15)
#define RATE_MCS4					BIT(16)
#define RATE_MCS5					BIT(17)
#define RATE_MCS6					BIT(18)
#define RATE_MCS7					BIT(19)
#define RATE_MCS8					BIT(20)
#define RATE_MCS9					BIT(21)
#define RATE_MCS10					BIT(22)
#define RATE_MCS11					BIT(23)
#define RATE_MCS12					BIT(24)
#define RATE_MCS13					BIT(25)
#define RATE_MCS14					BIT(26)
#define RATE_MCS15					BIT(27)

#define	RATE_ALL_CCK		(RATR_1M | RATR_2M | RATR_55M | RATR_11M)
#define	RATE_ALL_OFDM_AG	(RATR_6M | RATR_9M | RATR_12M | RATR_18M |\
				RATR_24M | RATR_36M | RATR_48M | RATR_54M)
#define	RATE_ALL_OFDM_1SS	(RATR_MCS0 | RATR_MCS1 | RATR_MCS2 |\
				RATR_MCS3 | RATR_MCS4 | RATR_MCS5 |\
				RATR_MCS6 | RATR_MCS7)
#define	RATE_ALL_OFDM_2SS	(RATR_MCS8 | RATR_MCS9 | RATR_MCS10 |\
				RATR_MCS11 | RATR_MCS12 | RATR_MCS13 |\
				RATR_MCS14 | RATR_MCS15)

#define	BW_OPMODE_20MHZ				BIT(2)
#define	BW_OPMODE_5G				BIT(1)
#define	BW_OPMODE_11J				BIT(0)

#define	CAM_VALID					BIT(15)
#define	CAM_NOTVALID				0x0000
#define	CAM_USEDK					BIT(5)

#define	CAM_NONE					0x0
#define	CAM_WEP40					0x01
#define	CAM_TKIP					0x02
#define	CAM_AES						0x04
#define	CAM_WEP104					0x05

#define	TOTAL_CAM_ENTRY				32
#define	HALF_CAM_ENTRY				16

#define	CAM_WRITE					BIT(16)
#define	CAM_READ					0x00000000
#define	CAM_POLLINIG				BIT(31)

#define	SCR_USEDK					0x01
#define	SCR_TXSEC_ENABLE			0x02
#define	SCR_RXSEC_ENABLE			0x04

#define	WOW_PMEN					BIT(0)
#define	WOW_WOMEN					BIT(1)
#define	WOW_MAGIC					BIT(2)
#define	WOW_UWF						BIT(3)

/*********************************************
*       8188 IMR/ISR bits
**********************************************/
#define	IMR_DISABLED			0x0
/* IMR DW0(0x0060-0063) Bit 0-31 */
/* TXRPT interrupt when CCX bit of the packet is set	*/
#define	IMR_TXCCK				BIT(30)
/* Power Save Time Out Interrupt */
#define	IMR_PSTIMEOUT			BIT(29)
/* When GTIMER4 expires, this bit is set to 1	*/
#define	IMR_GTINT4				BIT(28)
/* When GTIMER3 expires, this bit is set to 1	*/
#define	IMR_GTINT3				BIT(27)
/* Transmit Beacon0 Error			*/
#define	IMR_TBDER				BIT(26)
/* Transmit Beacon0 OK			*/
#define	IMR_TBDOK				BIT(25)
/* TSF Timer BIT32 toggle indication interrupt	*/
#define	IMR_TSF_BIT32_TOGGLE		BIT(24)
/* Beacon DMA Interrupt 0			*/
#define	IMR_BCNDMAINT0			BIT(20)
/* Beacon Queue DMA OK0			*/
#define	IMR_BCNDOK0				BIT(16)
/* HSISR Indicator (HSIMR & HSISR is true, this bit is set to 1)	*/
#define	IMR_HSISR_IND_ON_INT		BIT(15)
/* Beacon DMA Interrupt Extension for Win7			*/
#define	IMR_BCNDMAINT_E			BIT(14)
/* CTWidnow End or ATIM Window End */
#define	IMR_ATIMEND				BIT(12)
/* HISR1 Indicator (HISR1 & HIMR1 is true, this bit is set to 1)*/
#define	IMR_HISR1_IND_INT			BIT(11)
/* CPU to Host Command INT Status, Write 1 clear	*/
#define	IMR_C2HCMD				BIT(10)
/* CPU power Mode exchange INT Status, Write 1 clear	*/
#define	IMR_CPWM2			BIT(9)
/* CPU power Mode exchange INT Status, Write 1 clear	*/
#define	IMR_CPWM				BIT(8)
/* High Queue DMA OK	*/
#define	IMR_HIGHDOK				BIT(7)
/* Management Queue DMA OK	*/
#define	IMR_MGNTDOK				BIT(6)
/* AC_BK DMA OK		*/
#define	IMR_BKDOK				BIT(5)
/* AC_BE DMA OK	*/
#define	IMR_BEDOK				BIT(4)
/* AC_VI DMA OK	*/
#define	IMR_VIDOK				BIT(3)
/* AC_VO DMA OK	*/
#define	IMR_VODOK				BIT(2)
/* Rx Descriptor Unavailable	*/
#define	IMR_RDU				BIT(1)
/* Receive DMA OK */
#define	IMR_ROK				BIT(0)

/* IMR DW1(0x00B4-00B7) Bit 0-31 */
/* Beacon DMA Interrupt 7	*/
#define	IMR_BCNDMAINT7			BIT(27)
/* Beacon DMA Interrupt 6		*/
#define	IMR_BCNDMAINT6			BIT(26)
/* Beacon DMA Interrupt 5		*/
#define	IMR_BCNDMAINT5			BIT(25)
/* Beacon DMA Interrupt 4		*/
#define	IMR_BCNDMAINT4			BIT(24)
/* Beacon DMA Interrupt 3		*/
#define	IMR_BCNDMAINT3			BIT(23)
/* Beacon DMA Interrupt 2		*/
#define	IMR_BCNDMAINT2			BIT(22)
/* Beacon DMA Interrupt 1		*/
#define	IMR_BCNDMAINT1			BIT(21)
/* Beacon Queue DMA OK Interrup 7 */
#define	IMR_BCNDOK7				BIT(20)
/* Beacon Queue DMA OK Interrup 6 */
#define	IMR_BCNDOK6				BIT(19)
/* Beacon Queue DMA OK Interrup 5 */
#define	IMR_BCNDOK5				BIT(18)
/* Beacon Queue DMA OK Interrup 4 */
#define	IMR_BCNDOK4				BIT(17)
/* Beacon Queue DMA OK Interrup 3 */
#define	IMR_BCNDOK3				BIT(16)
/* Beacon Queue DMA OK Interrup 2 */
#define	IMR_BCNDOK2				BIT(15)
/* Beacon Queue DMA OK Interrup 1 */
#define	IMR_BCNDOK1				BIT(14)
/* ATIM Window End Extension for Win7 */
#define	IMR_ATIMEND_E		BIT(13)
/* Tx Error Flag Interrupt Status, write 1 clear. */
#define	IMR_TXERR				BIT(11)
/* Rx Error Flag INT Status, Write 1 clear */
#define	IMR_RXERR				BIT(10)
/* Transmit FIFO Overflow */
#define	IMR_TXFOVW				BIT(9)
/* Receive FIFO Overflow */
#define	IMR_RXFOVW				BIT(8)

#define	HWSET_MAX_SIZE				512
#define   EFUSE_MAX_SECTION			64
#define   EFUSE_REAL_CONTENT_LEN			256
/* PG data exclude header, dummy 7 bytes frome CP test and reserved 1byte.*/
#define		EFUSE_OOB_PROTECT_BYTES		18

#define	EEPROM_DEFAULT_TSSI					0x0
#define EEPROM_DEFAULT_TXPOWERDIFF			0x0
#define EEPROM_DEFAULT_CRYSTALCAP			0x5
#define EEPROM_DEFAULT_BOARDTYPE			0x02
#define EEPROM_DEFAULT_TXPOWER				0x1010
#define	EEPROM_DEFAULT_HT2T_TXPWR			0x10

#define	EEPROM_DEFAULT_LEGACYHTTXPOWERDIFF	0x3
#define	EEPROM_DEFAULT_THERMALMETER			0x18
#define	EEPROM_DEFAULT_ANTTXPOWERDIFF		0x0
#define	EEPROM_DEFAULT_TXPWDIFF_CRYSTALCAP	0x5
#define	EEPROM_DEFAULT_TXPOWERLEVEL			0x22
#define	EEPROM_DEFAULT_HT40_2SDIFF			0x0
#define EEPROM_DEFAULT_HT20_DIFF			2
#define	EEPROM_DEFAULT_LEGACYHTTXPOWERDIFF	0x3
#define EEPROM_DEFAULT_HT40_PWRMAXOFFSET	0
#define EEPROM_DEFAULT_HT20_PWRMAXOFFSET	0

#define RF_OPTION1							0x79
#define RF_OPTION2							0x7A
#define RF_OPTION3							0x7B
#define RF_OPTION4							0x7C

#define EEPROM_DEFAULT_PID					0x1234
#define EEPROM_DEFAULT_VID					0x5678
#define EEPROM_DEFAULT_CUSTOMERID			0xAB
#define EEPROM_DEFAULT_SUBCUSTOMERID		0xCD
#define EEPROM_DEFAULT_VERSION				0

#define	EEPROM_CHANNEL_PLAN_FCC				0x0
#define	EEPROM_CHANNEL_PLAN_IC				0x1
#define	EEPROM_CHANNEL_PLAN_ETSI			0x2
#define	EEPROM_CHANNEL_PLAN_SPAIN			0x3
#define	EEPROM_CHANNEL_PLAN_FRANCE			0x4
#define	EEPROM_CHANNEL_PLAN_MKK				0x5
#define	EEPROM_CHANNEL_PLAN_MKK1			0x6
#define	EEPROM_CHANNEL_PLAN_ISRAEL			0x7
#define	EEPROM_CHANNEL_PLAN_TELEC			0x8
#define	EEPROM_CHANNEL_PLAN_GLOBAL_DOMAIN	0x9
#define	EEPROM_CHANNEL_PLAN_WORLD_WIDE_13	0xA
#define	EEPROM_CHANNEL_PLAN_NCC				0xB
#define	EEPROM_CHANNEL_PLAN_BY_HW_MASK		0x80

#define EEPROM_CID_DEFAULT					0x0
#define EEPROM_CID_TOSHIBA					0x4
#define	EEPROM_CID_CCX						0x10
#define	EEPROM_CID_QMI						0x0D
#define EEPROM_CID_WHQL						0xFE

#define	RTL8188E_EEPROM_ID					0x8129

#define EEPROM_HPON							0x02
#define EEPROM_CLK							0x06
#define EEPROM_TESTR						0x08

#define EEPROM_TXPOWERCCK			0x10
#define	EEPROM_TXPOWERHT40_1S		0x16
#define EEPROM_TXPOWERHT20DIFF		0x1B
#define EEPROM_TXPOWER_OFDMDIFF		0x1B

#define	EEPROM_TX_PWR_INX				0x10

#define	EEPROM_CHANNELPLAN					0xB8
#define	EEPROM_XTAL_88E						0xB9
#define	EEPROM_THERMAL_METER_88E			0xBA
#define	EEPROM_IQK_LCK_88E					0xBB

#define	EEPROM_RF_BOARD_OPTION_88E			0xC1
#define	EEPROM_RF_FEATURE_OPTION_88E		0xC2
#define	EEPROM_RF_BT_SETTING_88E				0xC3
#define	EEPROM_VERSION					0xC4
#define	EEPROM_CUSTOMER_ID					0xC5
#define	EEPROM_RF_ANTENNA_OPT_88E			0xC9

#define	EEPROM_MAC_ADDR					0xD0
#define EEPROM_VID							0xD6
#define EEPROM_DID							0xD8
#define EEPROM_SVID							0xDA
#define EEPROM_SMID						0xDC

#define	STOPBECON					BIT(6)
#define	STOPHIGHT					BIT(5)
#define	STOPMGT						BIT(4)
#define	STOPVO						BIT(3)
#define	STOPVI						BIT(2)
#define	STOPBE						BIT(1)
#define	STOPBK						BIT(0)

#define	RCR_APPFCS					BIT(31)
#define	RCR_APP_MIC					BIT(30)
#define	RCR_APP_ICV					BIT(29)
#define	RCR_APP_PHYST_RXFF			BIT(28)
#define	RCR_APP_BA_SSN				BIT(27)
#define	RCR_ENMBID					BIT(24)
#define	RCR_LSIGEN					BIT(23)
#define	RCR_MFBEN					BIT(22)
#define	RCR_HTC_LOC_CTRL			BIT(14)
#define	RCR_AMF						BIT(13)
#define	RCR_ACF						BIT(12)
#define	RCR_ADF						BIT(11)
#define	RCR_AICV					BIT(9)
#define	RCR_ACRC32					BIT(8)
#define	RCR_CBSSID_BCN				BIT(7)
#define	RCR_CBSSID_DATA				BIT(6)
#define	RCR_CBSSID					RCR_CBSSID_DATA
#define	RCR_APWRMGT					BIT(5)
#define	RCR_ADD3					BIT(4)
#define	RCR_AB						BIT(3)
#define	RCR_AM						BIT(2)
#define	RCR_APM						BIT(1)
#define	RCR_AAP						BIT(0)
#define	RCR_MXDMA_OFFSET			8
#define	RCR_FIFO_OFFSET				13

#define RSV_CTRL					0x001C
#define RD_CTRL						0x0524

#define REG_USB_INFO				0xFE17
#define REG_USB_SPECIAL_OPTION		0xFE55
#define REG_USB_DMA_AGG_TO			0xFE5B
#define REG_USB_AGG_TO				0xFE5C
#define REG_USB_AGG_TH				0xFE5D

#define REG_USB_VID					0xFE60
#define REG_USB_PID					0xFE62
#define REG_USB_OPTIONAL			0xFE64
#define REG_USB_CHIRP_K				0xFE65
#define REG_USB_PHY					0xFE66
#define REG_USB_MAC_ADDR			0xFE70
#define REG_USB_HRPWM				0xFE58
#define REG_USB_HCPWM				0xFE57

#define SW18_FPWM					BIT(3)

#define ISO_MD2PP					BIT(0)
#define ISO_UA2USB					BIT(1)
#define ISO_UD2CORE					BIT(2)
#define ISO_PA2PCIE					BIT(3)
#define ISO_PD2CORE					BIT(4)
#define ISO_IP2MAC					BIT(5)
#define ISO_DIOP					BIT(6)
#define ISO_DIOE					BIT(7)
#define ISO_EB2CORE					BIT(8)
#define ISO_DIOR					BIT(9)

#define PWC_EV25V					BIT(14)
#define PWC_EV12V					BIT(15)

#define FEN_BBRSTB					BIT(0)
#define FEN_BB_GLB_RSTN				BIT(1)
#define FEN_USBA					BIT(2)
#define FEN_UPLL					BIT(3)
#define FEN_USBD					BIT(4)
#define FEN_DIO_PCIE				BIT(5)
#define FEN_PCIEA					BIT(6)
#define FEN_PPLL					BIT(7)
#define FEN_PCIED					BIT(8)
#define FEN_DIOE					BIT(9)
#define FEN_CPUEN					BIT(10)
#define FEN_DCORE					BIT(11)
#define FEN_ELDR					BIT(12)
#define FEN_DIO_RF					BIT(13)
#define FEN_HWPDN					BIT(14)
#define FEN_MREGEN					BIT(15)

#define PFM_LDALL					BIT(0)
#define PFM_ALDN					BIT(1)
#define PFM_LDKP					BIT(2)
#define PFM_WOWL					BIT(3)
#define ENPDN						BIT(4)
#define PDN_PL						BIT(5)
#define APFM_ONMAC					BIT(8)
#define APFM_OFF					BIT(9)
#define APFM_RSM					BIT(10)
#define AFSM_HSUS					BIT(11)
#define AFSM_PCIE					BIT(12)
#define APDM_MAC					BIT(13)
#define APDM_HOST					BIT(14)
#define APDM_HPDN					BIT(15)
#define RDY_MACON					BIT(16)
#define SUS_HOST					BIT(17)
#define ROP_ALD						BIT(20)
#define ROP_PWR						BIT(21)
#define ROP_SPS						BIT(22)
#define SOP_MRST					BIT(25)
#define SOP_FUSE					BIT(26)
#define SOP_ABG						BIT(27)
#define SOP_AMB						BIT(28)
#define SOP_RCK						BIT(29)
#define SOP_A8M						BIT(30)
#define XOP_BTCK					BIT(31)

#define ANAD16V_EN					BIT(0)
#define ANA8M						BIT(1)
#define MACSLP						BIT(4)
#define LOADER_CLK_EN				BIT(5)
#define _80M_SSC_DIS				BIT(7)
#define _80M_SSC_EN_HO				BIT(8)
#define PHY_SSC_RSTB				BIT(9)
#define SEC_CLK_EN					BIT(10)
#define MAC_CLK_EN					BIT(11)
#define SYS_CLK_EN					BIT(12)
#define RING_CLK_EN					BIT(13)

#define	BOOT_FROM_EEPROM			BIT(4)
#define	EEPROM_EN					BIT(5)

#define AFE_BGEN					BIT(0)
#define AFE_MBEN					BIT(1)
#define MAC_ID_EN					BIT(7)

#define WLOCK_ALL					BIT(0)
#define WLOCK_00					BIT(1)
#define WLOCK_04					BIT(2)
#define WLOCK_08					BIT(3)
#define WLOCK_40					BIT(4)
#define R_DIS_PRST_0				BIT(5)
#define R_DIS_PRST_1				BIT(6)
#define LOCK_ALL_EN					BIT(7)

#define RF_EN						BIT(0)
#define RF_RSTB						BIT(1)
#define RF_SDMRSTB					BIT(2)

#define LDA15_EN					BIT(0)
#define LDA15_STBY					BIT(1)
#define LDA15_OBUF					BIT(2)
#define LDA15_REG_VOS				BIT(3)
#define _LDA15_VOADJ(x)				(((x) & 0x7) << 4)

#define LDV12_EN					BIT(0)
#define LDV12_SDBY					BIT(1)
#define LPLDO_HSM					BIT(2)
#define LPLDO_LSM_DIS				BIT(3)
#define _LDV12_VADJ(x)				(((x) & 0xF) << 4)

#define XTAL_EN						BIT(0)
#define XTAL_BSEL					BIT(1)
#define _XTAL_BOSC(x)				(((x) & 0x3) << 2)
#define _XTAL_CADJ(x)				(((x) & 0xF) << 4)
#define XTAL_GATE_USB				BIT(8)
#define _XTAL_USB_DRV(x)			(((x) & 0x3) << 9)
#define XTAL_GATE_AFE				BIT(11)
#define _XTAL_AFE_DRV(x)			(((x) & 0x3) << 12)
#define XTAL_RF_GATE				BIT(14)
#define _XTAL_RF_DRV(x)				(((x) & 0x3) << 15)
#define XTAL_GATE_DIG				BIT(17)
#define _XTAL_DIG_DRV(x)			(((x) & 0x3) << 18)
#define XTAL_BT_GATE				BIT(20)
#define _XTAL_BT_DRV(x)				(((x) & 0x3) << 21)
#define _XTAL_GPIO(x)				(((x) & 0x7) << 23)

#define CKDLY_AFE					BIT(26)
#define CKDLY_USB					BIT(27)
#define CKDLY_DIG					BIT(28)
#define CKDLY_BT					BIT(29)

#define APLL_EN						BIT(0)
#define APLL_320_EN					BIT(1)
#define APLL_FREF_SEL				BIT(2)
#define APLL_EDGE_SEL				BIT(3)
#define APLL_WDOGB					BIT(4)
#define APLL_LPFEN					BIT(5)

#define APLL_REF_CLK_13MHZ			0x1
#define APLL_REF_CLK_19_2MHZ		0x2
#define APLL_REF_CLK_20MHZ			0x3
#define APLL_REF_CLK_25MHZ			0x4
#define APLL_REF_CLK_26MHZ			0x5
#define APLL_REF_CLK_38_4MHZ		0x6
#define APLL_REF_CLK_40MHZ			0x7

#define APLL_320EN					BIT(14)
#define APLL_80EN					BIT(15)
#define APLL_1MEN					BIT(24)

#define ALD_EN						BIT(18)
#define EF_PD						BIT(19)
#define EF_FLAG						BIT(31)

#define EF_TRPT						BIT(7)
#define LDOE25_EN					BIT(31)

#define RSM_EN						BIT(0)
#define TIMER_EN					BIT(4)

#define TRSW0EN						BIT(2)
#define TRSW1EN						BIT(3)
#define EROM_EN						BIT(4)
#define ENBT						BIT(5)
#define ENUART						BIT(8)
#define UART_910					BIT(9)
#define ENPMAC						BIT(10)
#define SIC_SWRST					BIT(11)
#define ENSIC						BIT(12)
#define SIC_23						BIT(13)
#define ENHDP						BIT(14)
#define SIC_LBK						BIT(15)

#define LED0PL						BIT(4)
#define LED1PL						BIT(12)
#define LED0DIS						BIT(7)

#define MCUFWDL_EN					BIT(0)
#define MCUFWDL_RDY					BIT(1)
#define FWDL_CHKSUM_RPT				BIT(2)
#define MACINI_RDY					BIT(3)
#define BBINI_RDY					BIT(4)
#define RFINI_RDY					BIT(5)
#define WINTINI_RDY					BIT(6)
#define CPRST						BIT(23)

#define XCLK_VLD					BIT(0)
#define ACLK_VLD					BIT(1)
#define UCLK_VLD					BIT(2)
#define PCLK_VLD					BIT(3)
#define PCIRSTB						BIT(4)
#define V15_VLD						BIT(5)
#define TRP_B15V_EN					BIT(7)
#define SIC_IDLE					BIT(8)
#define BD_MAC2						BIT(9)
#define BD_MAC1						BIT(10)
#define IC_MACPHY_MODE				BIT(11)
#define VENDOR_ID					BIT(19)
#define PAD_HWPD_IDN				BIT(22)
#define TRP_VAUX_EN					BIT(23)
#define TRP_BT_EN					BIT(24)
#define BD_PKG_SEL					BIT(25)
#define BD_HCI_SEL					BIT(26)
#define TYPE_ID						BIT(27)

#define CHIP_VER_RTL_MASK			0xF000
#define CHIP_VER_RTL_SHIFT			12

#define REG_LBMODE					(REG_CR + 3)

#define HCI_TXDMA_EN				BIT(0)
#define HCI_RXDMA_EN				BIT(1)
#define TXDMA_EN					BIT(2)
#define RXDMA_EN					BIT(3)
#define PROTOCOL_EN					BIT(4)
#define SCHEDULE_EN					BIT(5)
#define MACTXEN						BIT(6)
#define MACRXEN						BIT(7)
#define ENSWBCN						BIT(8)
#define ENSEC						BIT(9)

#define _NETTYPE(x)					(((x) & 0x3) << 16)
#define MASK_NETTYPE				0x30000
#define NT_NO_LINK					0x0
#define NT_LINK_AD_HOC				0x1
#define NT_LINK_AP					0x2
#define NT_AS_AP					0x3

#define _LBMODE(x)					(((x) & 0xF) << 24)
#define MASK_LBMODE					0xF000000
#define LOOPBACK_NORMAL				0x0
#define LOOPBACK_IMMEDIATELY		0xB
#define LOOPBACK_MAC_DELAY			0x3
#define LOOPBACK_PHY				0x1
#define LOOPBACK_DMA				0x7

#define GET_RX_PAGE_SIZE(value)		((value) & 0xF)
#define GET_TX_PAGE_SIZE(value)		(((value) & 0xF0) >> 4)
#define _PSRX_MASK					0xF
#define _PSTX_MASK					0xF0
#define _PSRX(x)					(x)
#define _PSTX(x)					((x) << 4)

#define PBP_64						0x0
#define PBP_128						0x1
#define PBP_256						0x2
#define PBP_512						0x3
#define PBP_1024					0x4

#define RXDMA_ARBBW_EN				BIT(0)
#define RXSHFT_EN					BIT(1)
#define RXDMA_AGG_EN				BIT(2)
#define QS_VO_QUEUE					BIT(8)
#define QS_VI_QUEUE					BIT(9)
#define QS_BE_QUEUE					BIT(10)
#define QS_BK_QUEUE					BIT(11)
#define QS_MANAGER_QUEUE			BIT(12)
#define QS_HIGH_QUEUE				BIT(13)

#define HQSEL_VOQ					BIT(0)
#define HQSEL_VIQ					BIT(1)
#define HQSEL_BEQ					BIT(2)
#define HQSEL_BKQ					BIT(3)
#define HQSEL_MGTQ					BIT(4)
#define HQSEL_HIQ					BIT(5)

#define _TXDMA_HIQ_MAP(x)			(((x)&0x3) << 14)
#define _TXDMA_MGQ_MAP(x)			(((x)&0x3) << 12)
#define _TXDMA_BKQ_MAP(x)			(((x)&0x3) << 10)
#define _TXDMA_BEQ_MAP(x)			(((x)&0x3) << 8)
#define _TXDMA_VIQ_MAP(x)			(((x)&0x3) << 6)
#define _TXDMA_VOQ_MAP(x)			(((x)&0x3) << 4)

#define QUEUE_LOW					1
#define QUEUE_NORMAL				2
#define QUEUE_HIGH					3

#define _LLT_NO_ACTIVE				0x0
#define _LLT_WRITE_ACCESS			0x1
#define _LLT_READ_ACCESS			0x2

#define _LLT_INIT_DATA(x)			((x) & 0xFF)
#define _LLT_INIT_ADDR(x)			(((x) & 0xFF) << 8)
#define _LLT_OP(x)					(((x) & 0x3) << 30)
#define _LLT_OP_VALUE(x)			(((x) >> 30) & 0x3)

#define BB_WRITE_READ_MASK			(BIT(31) | BIT(30))
#define BB_WRITE_EN					BIT(30)
#define BB_READ_EN					BIT(31)

#define _HPQ(x)			((x) & 0xFF)
#define _LPQ(x)			(((x) & 0xFF) << 8)
#define _PUBQ(x)		(((x) & 0xFF) << 16)
#define _NPQ(x)			((x) & 0xFF)

#define HPQ_PUBLIC_DIS		BIT(24)
#define LPQ_PUBLIC_DIS		BIT(25)
#define LD_RQPN			BIT(31)

#define BCN_VALID		BIT(16)
#define BCN_HEAD(x)		(((x) & 0xFF) << 8)
#define	BCN_HEAD_MASK		0xFF00

#define BLK_DESC_NUM_SHIFT			4
#define BLK_DESC_NUM_MASK			0xF

#define DROP_DATA_EN				BIT(9)

#define EN_AMPDU_RTY_NEW			BIT(7)

#define _INIRTSMCS_SEL(x)			((x) & 0x3F)

#define _SPEC_SIFS_CCK(x)			((x) & 0xFF)
#define _SPEC_SIFS_OFDM(x)			(((x) & 0xFF) << 8)

#define RATE_REG_BITMAP_ALL			0xFFFFF

#define _RRSC_BITMAP(x)				((x) & 0xFFFFF)

#define _RRSR_RSC(x)				(((x) & 0x3) << 21)
#define RRSR_RSC_RESERVED			0x0
#define RRSR_RSC_UPPER_SUBCHANNEL	0x1
#define RRSR_RSC_LOWER_SUBCHANNEL	0x2
#define RRSR_RSC_DUPLICATE_MODE		0x3

#define USE_SHORT_G1				BIT(20)

#define _AGGLMT_MCS0(x)				((x) & 0xF)
#define _AGGLMT_MCS1(x)				(((x) & 0xF) << 4)
#define _AGGLMT_MCS2(x)				(((x) & 0xF) << 8)
#define _AGGLMT_MCS3(x)				(((x) & 0xF) << 12)
#define _AGGLMT_MCS4(x)				(((x) & 0xF) << 16)
#define _AGGLMT_MCS5(x)				(((x) & 0xF) << 20)
#define _AGGLMT_MCS6(x)				(((x) & 0xF) << 24)
#define _AGGLMT_MCS7(x)				(((x) & 0xF) << 28)

#define	RETRY_LIMIT_SHORT_SHIFT		8
#define	RETRY_LIMIT_LONG_SHIFT		0

#define _DARF_RC1(x)				((x) & 0x1F)
#define _DARF_RC2(x)				(((x) & 0x1F) << 8)
#define _DARF_RC3(x)				(((x) & 0x1F) << 16)
#define _DARF_RC4(x)				(((x) & 0x1F) << 24)
#define _DARF_RC5(x)				((x) & 0x1F)
#define _DARF_RC6(x)				(((x) & 0x1F) << 8)
#define _DARF_RC7(x)				(((x) & 0x1F) << 16)
#define _DARF_RC8(x)				(((x) & 0x1F) << 24)

#define _RARF_RC1(x)				((x) & 0x1F)
#define _RARF_RC2(x)				(((x) & 0x1F) << 8)
#define _RARF_RC3(x)				(((x) & 0x1F) << 16)
#define _RARF_RC4(x)				(((x) & 0x1F) << 24)
#define _RARF_RC5(x)				((x) & 0x1F)
#define _RARF_RC6(x)				(((x) & 0x1F) << 8)
#define _RARF_RC7(x)				(((x) & 0x1F) << 16)
#define _RARF_RC8(x)				(((x) & 0x1F) << 24)

#define AC_PARAM_TXOP_LIMIT_OFFSET	16
#define AC_PARAM_ECW_MAX_OFFSET		12
#define AC_PARAM_ECW_MIN_OFFSET		8
#define AC_PARAM_AIFS_OFFSET		0

#define _AIFS(x)					(x)
#define _ECW_MAX_MIN(x)				((x) << 8)
#define _TXOP_LIMIT(x)				((x) << 16)

#define _BCNIFS(x)					((x) & 0xFF)
#define _BCNECW(x)					((((x) & 0xF)) << 8)

#define _LRL(x)						((x) & 0x3F)
#define _SRL(x)						(((x) & 0x3F) << 8)

#define _SIFS_CCK_CTX(x)			((x) & 0xFF)
#define _SIFS_CCK_TRX(x)			(((x) & 0xFF) << 8);

#define _SIFS_OFDM_CTX(x)			((x) & 0xFF)
#define _SIFS_OFDM_TRX(x)			(((x) & 0xFF) << 8);

#define _TBTT_PROHIBIT_HOLD(x)		(((x) & 0xFF) << 8)

#define DIS_EDCA_CNT_DWN			BIT(11)

#define EN_MBSSID					BIT(1)
#define EN_TXBCN_RPT				BIT(2)
#define	EN_BCN_FUNCTION				BIT(3)

#define TSFTR_RST					BIT(0)
#define TSFTR1_RST					BIT(1)

#define STOP_BCNQ					BIT(6)

#define	DIS_TSF_UDT0_NORMAL_CHIP	BIT(4)
#define	DIS_TSF_UDT0_TEST_CHIP		BIT(5)

#define	ACMHW_HWEN					BIT(0)
#define	ACMHW_BEQEN					BIT(1)
#define	ACMHW_VIQEN					BIT(2)
#define	ACMHW_VOQEN					BIT(3)
#define	ACMHW_BEQSTATUS				BIT(4)
#define	ACMHW_VIQSTATUS				BIT(5)
#define	ACMHW_VOQSTATUS				BIT(6)

#define APSDOFF						BIT(6)
#define APSDOFF_STATUS				BIT(7)

#define BW_20MHZ					BIT(2)

#define RATE_BITMAP_ALL				0xFFFFF

#define RATE_RRSR_CCK_ONLY_1M		0xFFFF1

#define TSFRST						BIT(0)
#define DIS_GCLK					BIT(1)
#define PAD_SEL						BIT(2)
#define PWR_ST						BIT(6)
#define PWRBIT_OW_EN				BIT(7)
#define ACRC						BIT(8)
#define CFENDFORM					BIT(9)
#define ICV							BIT(10)

#define AAP							BIT(0)
#define APM							BIT(1)
#define AM							BIT(2)
#define AB							BIT(3)
#define ADD3						BIT(4)
#define APWRMGT						BIT(5)
#define CBSSID						BIT(6)
#define CBSSID_DATA					BIT(6)
#define CBSSID_BCN					BIT(7)
#define ACRC32						BIT(8)
#define AICV						BIT(9)
#define ADF							BIT(11)
#define ACF							BIT(12)
#define AMF							BIT(13)
#define HTC_LOC_CTRL				BIT(14)
#define UC_DATA_EN					BIT(16)
#define BM_DATA_EN					BIT(17)
#define MFBEN						BIT(22)
#define LSIGEN						BIT(23)
#define ENMBID						BIT(24)
#define APP_BASSN					BIT(27)
#define APP_PHYSTS					BIT(28)
#define APP_ICV						BIT(29)
#define APP_MIC						BIT(30)
#define APP_FCS						BIT(31)

#define _MIN_SPACE(x)				((x) & 0x7)
#define _SHORT_GI_PADDING(x)		(((x) & 0x1F) << 3)

#define RXERR_TYPE_OFDM_PPDU		0
#define RXERR_TYPE_OFDM_FALSE_ALARM	1
#define	RXERR_TYPE_OFDM_MPDU_OK		2
#define RXERR_TYPE_OFDM_MPDU_FAIL	3
#define RXERR_TYPE_CCK_PPDU			4
#define RXERR_TYPE_CCK_FALSE_ALARM	5
#define RXERR_TYPE_CCK_MPDU_OK		6
#define RXERR_TYPE_CCK_MPDU_FAIL	7
#define RXERR_TYPE_HT_PPDU			8
#define RXERR_TYPE_HT_FALSE_ALARM	9
#define RXERR_TYPE_HT_MPDU_TOTAL	10
#define RXERR_TYPE_HT_MPDU_OK		11
#define RXERR_TYPE_HT_MPDU_FAIL		12
#define RXERR_TYPE_RX_FULL_DROP		15

#define RXERR_COUNTER_MASK			0xFFFFF
#define RXERR_RPT_RST				BIT(27)
#define _RXERR_RPT_SEL(type)		((type) << 28)

#define	SCR_TXUSEDK					BIT(0)
#define	SCR_RXUSEDK					BIT(1)
#define	SCR_TXENCENABLE				BIT(2)
#define	SCR_RXDECENABLE				BIT(3)
#define	SCR_SKBYA2					BIT(4)
#define	SCR_NOSKMC					BIT(5)
#define SCR_TXBCUSEDK				BIT(6)
#define SCR_RXBCUSEDK				BIT(7)

#define USB_IS_HIGH_SPEED			0
#define USB_IS_FULL_SPEED			1
#define USB_SPEED_MASK				BIT(5)

#define USB_NORMAL_SIE_EP_MASK		0xF
#define USB_NORMAL_SIE_EP_SHIFT		4

#define USB_TEST_EP_MASK			0x30
#define USB_TEST_EP_SHIFT			4

#define USB_AGG_EN					BIT(3)

#define MAC_ADDR_LEN				6
#define LAST_ENTRY_OF_TX_PKT_BUFFER	175/*255    88e*/

#define POLLING_LLT_THRESHOLD		20
#define POLLING_READY_TIMEOUT_COUNT		3000

#define	MAX_MSS_DENSITY_2T			0x13
#define	MAX_MSS_DENSITY_1T			0x0A

#define EPROM_CMD_OPERATING_MODE_MASK	((1<<7)|(1<<6))
#define EPROM_CMD_CONFIG			0x3
#define EPROM_CMD_LOAD				1

#define	HWSET_MAX_SIZE_92S			HWSET_MAX_SIZE

#define	HAL_8192C_HW_GPIO_WPS_BIT	BIT(2)

#define	RPMAC_RESET					0x100
#define	RPMAC_TXSTART				0x104
#define	RPMAC_TXLEGACYSIG			0x108
#define	RPMAC_TXHTSIG1				0x10c
#define	RPMAC_TXHTSIG2				0x110
#define	RPMAC_PHYDEBUG				0x114
#define	RPMAC_TXPACKETNUM			0x118
#define	RPMAC_TXIDLE				0x11c
#define	RPMAC_TXMACHEADER0			0x120
#define	RPMAC_TXMACHEADER1			0x124
#define	RPMAC_TXMACHEADER2			0x128
#define	RPMAC_TXMACHEADER3			0x12c
#define	RPMAC_TXMACHEADER4			0x130
#define	RPMAC_TXMACHEADER5			0x134
#define	RPMAC_TXDADATYPE			0x138
#define	RPMAC_TXRANDOMSEED			0x13c
#define	RPMAC_CCKPLCPPREAMBLE		0x140
#define	RPMAC_CCKPLCPHEADER			0x144
#define	RPMAC_CCKCRC16				0x148
#define	RPMAC_OFDMRXCRC32OK			0x170
#define	RPMAC_OFDMRXCRC32ER			0x174
#define	RPMAC_OFDMRXPARITYER		0x178
#define	RPMAC_OFDMRXCRC8ER			0x17c
#define	RPMAC_CCKCRXRC16ER			0x180
#define	RPMAC_CCKCRXRC32ER			0x184
#define	RPMAC_CCKCRXRC32OK			0x188
#define	RPMAC_TXSTATUS				0x18c

#define	RFPGA0_RFMOD				0x800

#define	RFPGA0_TXINFO				0x804
#define	RFPGA0_PSDFUNCTION			0x808

#define	RFPGA0_TXGAINSTAGE			0x80c

#define	RFPGA0_RFTIMING1			0x810
#define	RFPGA0_RFTIMING2			0x814

#define	RFPGA0_XA_HSSIPARAMETER1	0x820
#define	RFPGA0_XA_HSSIPARAMETER2	0x824
#define	RFPGA0_XB_HSSIPARAMETER1	0x828
#define	RFPGA0_XB_HSSIPARAMETER2	0x82c

#define	RFPGA0_XA_LSSIPARAMETER		0x840
#define	RFPGA0_XB_LSSIPARAMETER		0x844

#define	RFPGA0_RFWAKEUPPARAMETER	0x850
#define	RFPGA0_RFSLEEPUPPARAMETER	0x854

#define	RFPGA0_XAB_SWITCHCONTROL	0x858
#define	RFPGA0_XCD_SWITCHCONTROL	0x85c

#define	RFPGA0_XA_RFINTERFACEOE		0x860
#define	RFPGA0_XB_RFINTERFACEOE		0x864

#define	RFPGA0_XAB_RFINTERFACESW	0x870
#define	RFPGA0_XCD_RFINTERFACESW	0x874

#define	RFPGA0_XAB_RFPARAMETER		0x878
#define	RFPGA0_XCD_RFPARAMETER		0x87c

#define	RFPGA0_ANALOGPARAMETER1		0x880
#define	RFPGA0_ANALOGPARAMETER2		0x884
#define	RFPGA0_ANALOGPARAMETER3		0x888
#define	RFPGA0_ANALOGPARAMETER4		0x88c

#define	RFPGA0_XA_LSSIREADBACK		0x8a0
#define	RFPGA0_XB_LSSIREADBACK		0x8a4
#define	RFPGA0_XC_LSSIREADBACK		0x8a8
#define	RFPGA0_XD_LSSIREADBACK		0x8ac

#define	RFPGA0_PSDREPORT			0x8b4
#define	TRANSCEIVEA_HSPI_READBACK	0x8b8
#define	TRANSCEIVEB_HSPI_READBACK	0x8bc
#define	REG_SC_CNT						0x8c4
#define	RFPGA0_XAB_RFINTERFACERB	0x8e0
#define	RFPGA0_XCD_RFINTERFACERB	0x8e4

#define	RFPGA1_RFMOD				0x900

#define	RFPGA1_TXBLOCK				0x904
#define	RFPGA1_DEBUGSELECT			0x908
#define	RFPGA1_TXINFO				0x90c

#define	RCCK0_SYSTEM				0xa00

#define	RCCK0_AFESETTING			0xa04
#define	RCCK0_CCA					0xa08

#define	RCCK0_RXAGC1				0xa0c
#define	RCCK0_RXAGC2				0xa10

#define	RCCK0_RXHP					0xa14

#define	RCCK0_DSPPARAMETER1			0xa18
#define	RCCK0_DSPPARAMETER2			0xa1c

#define	RCCK0_TXFILTER1				0xa20
#define	RCCK0_TXFILTER2				0xa24
#define	RCCK0_DEBUGPORT				0xa28
#define	RCCK0_FALSEALARMREPORT		0xa2c
#define	RCCK0_TRSSIREPORT		0xa50
#define	RCCK0_RXREPORT			0xa54
#define	RCCK0_FACOUNTERLOWER		0xa5c
#define	RCCK0_FACOUNTERUPPER		0xa58
#define	RCCK0_CCA_CNT			0xa60

/* PageB(0xB00) */
#define	RPDP_ANTA					0xb00
#define	RPDP_ANTA_4				0xb04
#define	RPDP_ANTA_8				0xb08
#define	RPDP_ANTA_C				0xb0c
#define	RPDP_ANTA_10					0xb10
#define	RPDP_ANTA_14					0xb14
#define	RPDP_ANTA_18					0xb18
#define	RPDP_ANTA_1C					0xb1c
#define	RPDP_ANTA_20					0xb20
#define	RPDP_ANTA_24					0xb24

#define	RCONFIG_PMPD_ANTA			0xb28
#define	RCONFIG_RAM64x16				0xb2c

#define	RBNDA						0xb30
#define	RHSSIPAR						0xb34

#define	RCONFIG_ANTA					0xb68
#define	RCONFIG_ANTB					0xb6c

#define	RPDP_ANTB					0xb70
#define	RPDP_ANTB_4					0xb74
#define	RPDP_ANTB_8					0xb78
#define	RPDP_ANTB_C					0xb7c
#define	RPDP_ANTB_10					0xb80
#define	RPDP_ANTB_14					0xb84
#define	RPDP_ANTB_18					0xb88
#define	RPDP_ANTB_1C					0xb8c
#define	RPDP_ANTB_20					0xb90
#define	RPDP_ANTB_24					0xb94

#define	RCONFIG_PMPD_ANTB			0xb98

#define	RBNDB						0xba0

#define	RAPK							0xbd8
#define	RPM_RX0_ANTA				0xbdc
#define	RPM_RX1_ANTA				0xbe0
#define	RPM_RX2_ANTA				0xbe4
#define	RPM_RX3_ANTA				0xbe8
#define	RPM_RX0_ANTB				0xbec
#define	RPM_RX1_ANTB				0xbf0
#define	RPM_RX2_ANTB				0xbf4
#define	RPM_RX3_ANTB				0xbf8

/*Page C*/
#define	ROFDM0_LSTF					0xc00

#define	ROFDM0_TRXPATHENABLE		0xc04
#define	ROFDM0_TRMUXPAR				0xc08
#define	ROFDM0_TRSWISOLATION		0xc0c

#define	ROFDM0_XARXAFE				0xc10
#define	ROFDM0_XARXIQIMBALANCE		0xc14
#define	ROFDM0_XBRXAFE			0xc18
#define	ROFDM0_XBRXIQIMBALANCE		0xc1c
#define	ROFDM0_XCRXAFE			0xc20
#define	ROFDM0_XCRXIQIMBANLANCE		0xc24
#define	ROFDM0_XDRXAFE			0xc28
#define	ROFDM0_XDRXIQIMBALANCE		0xc2c

#define	ROFDM0_RXDETECTOR1			0xc30
#define	ROFDM0_RXDETECTOR2			0xc34
#define	ROFDM0_RXDETECTOR3			0xc38
#define	ROFDM0_RXDETECTOR4			0xc3c

#define	ROFDM0_RXDSP				0xc40
#define	ROFDM0_CFOANDDAGC			0xc44
#define	ROFDM0_CCADROPTHRESHOLD		0xc48
#define	ROFDM0_ECCATHRESHOLD		0xc4c

#define	ROFDM0_XAAGCCORE1			0xc50
#define	ROFDM0_XAAGCCORE2			0xc54
#define	ROFDM0_XBAGCCORE1			0xc58
#define	ROFDM0_XBAGCCORE2			0xc5c
#define	ROFDM0_XCAGCCORE1			0xc60
#define	ROFDM0_XCAGCCORE2			0xc64
#define	ROFDM0_XDAGCCORE1			0xc68
#define	ROFDM0_XDAGCCORE2			0xc6c

#define	ROFDM0_AGCPARAMETER1		0xc70
#define	ROFDM0_AGCPARAMETER2		0xc74
#define	ROFDM0_AGCRSSITABLE			0xc78
#define	ROFDM0_HTSTFAGC				0xc7c

#define	ROFDM0_XATXIQIMBALANCE		0xc80
#define	ROFDM0_XATXAFE				0xc84
#define	ROFDM0_XBTXIQIMBALANCE		0xc88
#define	ROFDM0_XBTXAFE				0xc8c
#define	ROFDM0_XCTXIQIMBALANCE		0xc90
#define	ROFDM0_XCTXAFE			0xc94
#define	ROFDM0_XDTXIQIMBALANCE		0xc98
#define	ROFDM0_XDTXAFE				0xc9c

#define ROFDM0_RXIQEXTANTA			0xca0
#define	ROFDM0_TXCOEFF1				0xca4
#define	ROFDM0_TXCOEFF2				0xca8
#define	ROFDM0_TXCOEFF3				0xcac
#define	ROFDM0_TXCOEFF4				0xcb0
#define	ROFDM0_TXCOEFF5				0xcb4
#define	ROFDM0_TXCOEFF6				0xcb8

#define	ROFDM0_RXHPPARAMETER		0xce0
#define	ROFDM0_TXPSEUDONOISEWGT		0xce4
#define	ROFDM0_FRAMESYNC			0xcf0
#define	ROFDM0_DFSREPORT			0xcf4

#define	ROFDM1_LSTF					0xd00
#define	ROFDM1_TRXPATHENABLE		0xd04

#define	ROFDM1_CF0					0xd08
#define	ROFDM1_CSI1					0xd10
#define	ROFDM1_SBD					0xd14
#define	ROFDM1_CSI2					0xd18
#define	ROFDM1_CFOTRACKING			0xd2c
#define	ROFDM1_TRXMESAURE1			0xd34
#define	ROFDM1_INTFDET				0xd3c
#define	ROFDM1_PSEUDONOISESTATEAB	0xd50
#define	ROFDM1_PSEUDONOISESTATECD	0xd54
#define	ROFDM1_RXPSEUDONOISEWGT		0xd58

#define	ROFDM_PHYCOUNTER1			0xda0
#define	ROFDM_PHYCOUNTER2			0xda4
#define	ROFDM_PHYCOUNTER3			0xda8

#define	ROFDM_SHORTCFOAB			0xdac
#define	ROFDM_SHORTCFOCD			0xdb0
#define	ROFDM_LONGCFOAB				0xdb4
#define	ROFDM_LONGCFOCD				0xdb8
#define	ROFDM_TAILCF0AB				0xdbc
#define	ROFDM_TAILCF0CD				0xdc0
#define	ROFDM_PWMEASURE1		0xdc4
#define	ROFDM_PWMEASURE2		0xdc8
#define	ROFDM_BWREPORT				0xdcc
#define	ROFDM_AGCREPORT				0xdd0
#define	ROFDM_RXSNR					0xdd4
#define	ROFDM_RXEVMCSI				0xdd8
#define	ROFDM_SIGREPORT				0xddc

#define	RTXAGC_A_RATE18_06			0xe00
#define	RTXAGC_A_RATE54_24			0xe04
#define	RTXAGC_A_CCK1_MCS32			0xe08
#define	RTXAGC_A_MCS03_MCS00		0xe10
#define	RTXAGC_A_MCS07_MCS04		0xe14
#define	RTXAGC_A_MCS11_MCS08		0xe18
#define	RTXAGC_A_MCS15_MCS12		0xe1c

#define	RTXAGC_B_RATE18_06			0x830
#define	RTXAGC_B_RATE54_24			0x834
#define	RTXAGC_B_CCK1_55_MCS32		0x838
#define	RTXAGC_B_MCS03_MCS00		0x83c
#define	RTXAGC_B_MCS07_MCS04		0x848
#define	RTXAGC_B_MCS11_MCS08		0x84c
#define	RTXAGC_B_MCS15_MCS12		0x868
#define	RTXAGC_B_CCK11_A_CCK2_11	0x86c

#define	RFPGA0_IQK					0xe28
#define	RTX_IQK_TONE_A				0xe30
#define	RRX_IQK_TONE_A				0xe34
#define	RTX_IQK_PI_A					0xe38
#define	RRX_IQK_PI_A					0xe3c

#define	RTX_IQK							0xe40
#define	RRX_IQK						0xe44
#define	RIQK_AGC_PTS					0xe48
#define	RIQK_AGC_RSP					0xe4c
#define	RTX_IQK_TONE_B				0xe50
#define	RRX_IQK_TONE_B				0xe54
#define	RTX_IQK_PI_B					0xe58
#define	RRX_IQK_PI_B					0xe5c
#define	RIQK_AGC_CONT				0xe60

#define	RBLUE_TOOTH					0xe6c
#define	RRX_WAIT_CCA					0xe70
#define	RTX_CCK_RFON					0xe74
#define	RTX_CCK_BBON				0xe78
#define	RTX_OFDM_RFON				0xe7c
#define	RTX_OFDM_BBON				0xe80
#define	RTX_TO_RX					0xe84
#define	RTX_TO_TX					0xe88
#define	RRX_CCK						0xe8c

#define	RTX_POWER_BEFORE_IQK_A		0xe94
#define	RTX_POWER_AFTER_IQK_A			0xe9c

#define	RRX_POWER_BEFORE_IQK_A		0xea0
#define	RRX_POWER_BEFORE_IQK_A_2		0xea4
#define	RRX_POWER_AFTER_IQK_A			0xea8
#define	RRX_POWER_AFTER_IQK_A_2		0xeac

#define	RTX_POWER_BEFORE_IQK_B		0xeb4
#define	RTX_POWER_AFTER_IQK_B			0xebc

#define	RRX_POWER_BEFORE_IQK_B		0xec0
#define	RRX_POWER_BEFORE_IQK_B_2		0xec4
#define	RRX_POWER_AFTER_IQK_B			0xec8
#define	RRX_POWER_AFTER_IQK_B_2		0xecc

#define	RRX_OFDM					0xed0
#define	RRX_WAIT_RIFS				0xed4
#define	RRX_TO_RX					0xed8
#define	RSTANDBY						0xedc
#define	RSLEEP						0xee0
#define	RPMPD_ANAEN				0xeec

#define	RZEBRA1_HSSIENABLE			0x0
#define	RZEBRA1_TRXENABLE1			0x1
#define	RZEBRA1_TRXENABLE2			0x2
#define	RZEBRA1_AGC					0x4
#define	RZEBRA1_CHARGEPUMP			0x5
#define	RZEBRA1_CHANNEL				0x7

#define	RZEBRA1_TXGAIN				0x8
#define	RZEBRA1_TXLPF				0x9
#define	RZEBRA1_RXLPF				0xb
#define	RZEBRA1_RXHPFCORNER			0xc

#define	RGLOBALCTRL					0
#define	RRTL8256_TXLPF				19
#define	RRTL8256_RXLPF				11
#define	RRTL8258_TXLPF				0x11
#define	RRTL8258_RXLPF				0x13
#define	RRTL8258_RSSILPF			0xa

#define	RF_AC						0x00

#define	RF_IQADJ_G1					0x01
#define	RF_IQADJ_G2					0x02
#define	RF_POW_TRSW					0x05

#define	RF_GAIN_RX					0x06
#define	RF_GAIN_TX					0x07

#define	RF_TXM_IDAC					0x08
#define	RF_BS_IQGEN					0x0F

#define	RF_MODE1					0x10
#define	RF_MODE2					0x11

#define	RF_RX_AGC_HP				0x12
#define	RF_TX_AGC					0x13
#define	RF_BIAS						0x14
#define	RF_IPA						0x15
#define	RF_POW_ABILITY				0x17
#define	RF_MODE_AG					0x18
#define	RRFCHANNEL					0x18
#define	RF_CHNLBW					0x18
#define	RF_TOP						0x19

#define	RF_RX_G1					0x1A
#define	RF_RX_G2					0x1B

#define	RF_RX_BB2					0x1C
#define	RF_RX_BB1					0x1D

#define	RF_RCK1						0x1E
#define	RF_RCK2						0x1F

#define	RF_TX_G1					0x20
#define	RF_TX_G2					0x21
#define	RF_TX_G3					0x22

#define	RF_TX_BB1					0x23
#define	RF_T_METER					0x42

#define	RF_SYN_G1					0x25
#define	RF_SYN_G2					0x26
#define	RF_SYN_G3					0x27
#define	RF_SYN_G4					0x28
#define	RF_SYN_G5					0x29
#define	RF_SYN_G6					0x2A
#define	RF_SYN_G7					0x2B
#define	RF_SYN_G8					0x2C

#define	RF_RCK_OS					0x30
#define	RF_TXPA_G1					0x31
#define	RF_TXPA_G2					0x32
#define	RF_TXPA_G3					0x33

#define	RF_TX_BIAS_A					0x35
#define	RF_TX_BIAS_D					0x36
#define	RF_LOBF_9					0x38
#define	RF_RXRF_A3					0x3C
#define	RF_TRSW						0x3F

#define	RF_TXRF_A2					0x41
#define	RF_TXPA_G4					0x46
#define	RF_TXPA_A4					0x4B

#define	RF_WE_LUT					0xEF

#define	BBBRESETB					0x100
#define	BGLOBALRESETB				0x200
#define	BOFDMTXSTART				0x4
#define	BCCKTXSTART					0x8
#define	BCRC32DEBUG					0x100
#define	BPMACLOOPBACK				0x10
#define	BTXLSIG						0xffffff
#define	BOFDMTXRATE					0xf
#define	BOFDMTXRESERVED				0x10
#define	BOFDMTXLENGTH				0x1ffe0
#define	BOFDMTXPARITY				0x20000
#define	BTXHTSIG1					0xffffff
#define	BTXHTMCSRATE				0x7f
#define	BTXHTBW						0x80
#define	BTXHTLENGTH					0xffff00
#define	BTXHTSIG2					0xffffff
#define	BTXHTSMOOTHING				0x1
#define	BTXHTSOUNDING				0x2
#define	BTXHTRESERVED				0x4
#define	BTXHTAGGREATION				0x8
#define	BTXHTSTBC					0x30
#define	BTXHTADVANCECODING			0x40
#define	BTXHTSHORTGI				0x80
#define	BTXHTNUMBERHT_LTF			0x300
#define	BTXHTCRC8					0x3fc00
#define	BCOUNTERRESET				0x10000
#define	BNUMOFOFDMTX				0xffff
#define	BNUMOFCCKTX					0xffff0000
#define	BTXIDLEINTERVAL				0xffff
#define	BOFDMSERVICE				0xffff0000
#define	BTXMACHEADER				0xffffffff
#define	BTXDATAINIT					0xff
#define	BTXHTMODE					0x100
#define	BTXDATATYPE					0x30000
#define	BTXRANDOMSEED				0xffffffff
#define	BCCKTXPREAMBLE				0x1
#define	BCCKTXSFD					0xffff0000
#define	BCCKTXSIG					0xff
#define	BCCKTXSERVICE				0xff00
#define	BCCKLENGTHEXT				0x8000
#define	BCCKTXLENGHT				0xffff0000
#define	BCCKTXCRC16					0xffff
#define	BCCKTXSTATUS				0x1
#define	BOFDMTXSTATUS				0x2
#define IS_BB_REG_OFFSET_92S(_offset)	\
	((_offset >= 0x800) && (_offset <= 0xfff))

#define	BRFMOD						0x1
#define	BJAPANMODE					0x2
#define	BCCKTXSC					0x30
#define	BCCKEN						0x1000000
#define	BOFDMEN						0x2000000

#define	BOFDMRXADCPHASE			0x10000
#define	BOFDMTXDACPHASE			0x40000
#define	BXATXAGC			0x3f

#define	BXBTXAGC			0xf00
#define	BXCTXAGC			0xf000
#define	BXDTXAGC			0xf0000

#define	BPASTART			0xf0000000
#define	BTRSTART			0x00f00000
#define	BRFSTART			0x0000f000
#define	BBBSTART			0x000000f0
#define	BBBCCKSTART			0x0000000f
#define	BPAEND				0xf
#define	BTREND				0x0f000000
#define	BRFEND				0x000f0000
#define	BCCAMASK			0x000000f0
#define	BR2RCCAMASK			0x00000f00
#define	BHSSI_R2TDELAY			0xf8000000
#define	BHSSI_T2RDELAY			0xf80000
#define	BCONTXHSSI			0x400
#define	BIGFROMCCK			0x200
#define	BAGCADDRESS			0x3f
#define	BRXHPTX				0x7000
#define	BRXHP2RX			0x38000
#define	BRXHPCCKINI			0xc0000
#define	BAGCTXCODE			0xc00000
#define	BAGCRXCODE			0x300000

#define	B3WIREDATALENGTH		0x800
#define	B3WIREADDREAALENGTH		0x400

#define	B3WIRERFPOWERDOWN		0x1
#define	B5GPAPEPOLARITY			0x40000000
#define	B2GPAPEPOLARITY			0x80000000
#define	BRFSW_TXDEFAULTANT		0x3
#define	BRFSW_TXOPTIONANT		0x30
#define	BRFSW_RXDEFAULTANT		0x300
#define	BRFSW_RXOPTIONANT		0x3000
#define	BRFSI_3WIREDATA			0x1
#define	BRFSI_3WIRECLOCK		0x2
#define	BRFSI_3WIRELOAD			0x4
#define	BRFSI_3WIRERW			0x8
#define	BRFSI_3WIRE			0xf

#define	BRFSI_RFENV			0x10

#define	BRFSI_TRSW			0x20
#define	BRFSI_TRSWB			0x40
#define	BRFSI_ANTSW			0x100
#define	BRFSI_ANTSWB			0x200
#define	BRFSI_PAPE			0x400
#define	BRFSI_PAPE5G			0x800
#define	BBANDSELECT			0x1
#define	BHTSIG2_GI			0x80
#define	BHTSIG2_SMOOTHING		0x01
#define	BHTSIG2_SOUNDING		0x02
#define	BHTSIG2_AGGREATON		0x08
#define	BHTSIG2_STBC			0x30
#define	BHTSIG2_ADVCODING		0x40
#define	BHTSIG2_NUMOFHTLTF		0x300
#define	BHTSIG2_CRC8			0x3fc
#define	BHTSIG1_MCS			0x7f
#define	BHTSIG1_BANDWIDTH		0x80
#define	BHTSIG1_HTLENGTH		0xffff
#define	BLSIG_RATE			0xf
#define	BLSIG_RESERVED			0x10
#define	BLSIG_LENGTH			0x1fffe
#define	BLSIG_PARITY			0x20
#define	BCCKRXPHASE			0x4

#define	BLSSIREADADDRESS		0x7f800000
#define	BLSSIREADEDGE			0x80000000

#define	BLSSIREADBACKDATA		0xfffff

#define	BLSSIREADOKFLAG			0x1000
#define	BCCKSAMPLERATE			0x8
#define	BREGULATOR0STANDBY		0x1
#define	BREGULATORPLLSTANDBY		0x2
#define	BREGULATOR1STANDBY		0x4
#define	BPLLPOWERUP			0x8
#define	BDPLLPOWERUP			0x10
#define	BDA10POWERUP			0x20
#define	BAD7POWERUP			0x200
#define	BDA6POWERUP			0x2000
#define	BXTALPOWERUP			0x4000
#define	B40MDCLKPOWERUP			0x8000
#define	BDA6DEBUGMODE			0x20000
#define	BDA6SWING			0x380000

#define	BADCLKPHASE			0x4000000
#define	B80MCLKDELAY			0x18000000
#define	BAFEWATCHDOGENABLE		0x20000000

#define	BXTALCAP01			0xc0000000
#define	BXTALCAP23			0x3
#define	BXTALCAP92X					0x0f000000
#define BXTALCAP			0x0f000000

#define	BINTDIFCLKENABLE		0x400
#define	BEXTSIGCLKENABLE		0x800
#define	BBANDGAP_MBIAS_POWERUP      0x10000
#define	BAD11SH_GAIN			0xc0000
#define	BAD11NPUT_RANGE			0x700000
#define	BAD110P_CURRENT			0x3800000
#define	BLPATH_LOOPBACK			0x4000000
#define	BQPATH_LOOPBACK			0x8000000
#define	BAFE_LOOPBACK			0x10000000
#define	BDA10_SWING			0x7e0
#define	BDA10_REVERSE			0x800
#define	BDA_CLK_SOURCE              0x1000
#define	BDA7INPUT_RANGE			0x6000
#define	BDA7_GAIN			0x38000
#define	BDA7OUTPUT_CM_MODE          0x40000
#define	BDA7INPUT_CM_MODE           0x380000
#define	BDA7CURRENT			0xc00000
#define	BREGULATOR_ADJUST		0x7000000
#define	BAD11POWERUP_ATTX		0x1
#define	BDA10PS_ATTX			0x10
#define	BAD11POWERUP_ATRX		0x100
#define	BDA10PS_ATRX			0x1000
#define	BCCKRX_AGC_FORMAT           0x200
#define	BPSDFFT_SAMPLE_POINT		0xc000
#define	BPSD_AVERAGE_NUM            0x3000
#define	BIQPATH_CONTROL			0xc00
#define	BPSD_FREQ			0x3ff
#define	BPSD_ANTENNA_PATH           0x30
#define	BPSD_IQ_SWITCH              0x40
#define	BPSD_RX_TRIGGER             0x400000
#define	BPSD_TX_TRIGGER             0x80000000
#define	BPSD_SINE_TONE_SCALE        0x7f000000
#define	BPSD_REPORT			0xffff

#define	BOFDM_TXSC			0x30000000
#define	BCCK_TXON			0x1
#define	BOFDM_TXON			0x2
#define	BDEBUG_PAGE			0xfff
#define	BDEBUG_ITEM			0xff
#define	BANTL				0x10
#define	BANT_NONHT		    0x100
#define	BANT_HT1			0x1000
#define	BANT_HT2			0x10000
#define	BANT_HT1S1			0x100000
#define	BANT_NONHTS1			0x1000000

#define	BCCK_BBMODE			0x3
#define	BCCK_TXPOWERSAVING		0x80
#define	BCCK_RXPOWERSAVING		0x40

#define	BCCK_SIDEBAND			0x10

#define	BCCK_SCRAMBLE			0x8
#define	BCCK_ANTDIVERSITY		0x8000
#define	BCCK_CARRIER_RECOVERY		0x4000
#define	BCCK_TXRATE			0x3000
#define	BCCK_DCCANCEL			0x0800
#define	BCCK_ISICANCEL			0x0400
#define	BCCK_MATCH_FILTER           0x0200
#define	BCCK_EQUALIZER			0x0100
#define	BCCK_PREAMBLE_DETECT		0x800000
#define	BCCK_FAST_FALSECCA          0x400000
#define	BCCK_CH_ESTSTART            0x300000
#define	BCCK_CCA_COUNT              0x080000
#define	BCCK_CS_LIM			0x070000
#define	BCCK_BIST_MODE              0x80000000
#define	BCCK_CCAMASK			0x40000000
#define	BCCK_TX_DAC_PHASE		0x4
#define	BCCK_RX_ADC_PHASE		0x20000000
#define	BCCKR_CP_MODE			0x0100
#define	BCCK_TXDC_OFFSET		0xf0
#define	BCCK_RXDC_OFFSET		0xf
#define	BCCK_CCA_MODE			0xc000
#define	BCCK_FALSECS_LIM		0x3f00
#define	BCCK_CS_RATIO			0xc00000
#define	BCCK_CORGBIT_SEL		0x300000
#define	BCCK_PD_LIM			0x0f0000
#define	BCCK_NEWCCA			0x80000000
#define	BCCK_RXHP_OF_IG             0x8000
#define	BCCK_RXIG			0x7f00
#define	BCCK_LNA_POLARITY           0x800000
#define	BCCK_RX1ST_BAIN             0x7f0000
#define	BCCK_RF_EXTEND              0x20000000
#define	BCCK_RXAGC_SATLEVEL		0x1f000000
#define	BCCK_RXAGC_SATCOUNT		0xe0
#define	BCCKRXRFSETTLE			0x1f
#define	BCCK_FIXED_RXAGC		0x8000
#define	BCCK_ANTENNA_POLARITY		0x2000
#define	BCCK_TXFILTER_TYPE          0x0c00
#define	BCCK_RXAGC_REPORTTYPE		0x0300
#define	BCCK_RXDAGC_EN              0x80000000
#define	BCCK_RXDAGC_PERIOD		0x20000000
#define	BCCK_RXDAGC_SATLEVEL		0x1f000000
#define	BCCK_TIMING_RECOVERY		0x800000
#define	BCCK_TXC0			0x3f0000
#define	BCCK_TXC1			0x3f000000
#define	BCCK_TXC2			0x3f
#define	BCCK_TXC3			0x3f00
#define	BCCK_TXC4			0x3f0000
#define	BCCK_TXC5			0x3f000000
#define	BCCK_TXC6			0x3f
#define	BCCK_TXC7			0x3f00
#define	BCCK_DEBUGPORT			0xff0000
#define	BCCK_DAC_DEBUG              0x0f000000
#define	BCCK_FALSEALARM_ENABLE      0x8000
#define	BCCK_FALSEALARM_READ        0x4000
#define	BCCK_TRSSI			0x7f
#define	BCCK_RXAGC_REPORT           0xfe
#define	BCCK_RXREPORT_ANTSEL		0x80000000
#define	BCCK_RXREPORT_MFOFF		0x40000000
#define	BCCK_RXREPORT_SQLOSS		0x20000000
#define	BCCK_RXREPORT_PKTLOSS		0x10000000
#define	BCCK_RXREPORT_LOCKEDBIT		0x08000000
#define	BCCK_RXREPORT_RATEERROR		0x04000000
#define	BCCK_RXREPORT_RXRATE		0x03000000
#define	BCCK_RXFA_COUNTER_LOWER     0xff
#define	BCCK_RXFA_COUNTER_UPPER     0xff000000
#define	BCCK_RXHPAGC_START          0xe000
#define	BCCK_RXHPAGC_FINAL          0x1c00
#define	BCCK_RXFALSEALARM_ENABLE    0x8000
#define	BCCK_FACOUNTER_FREEZE       0x4000
#define	BCCK_TXPATH_SEL             0x10000000
#define	BCCK_DEFAULT_RXPATH         0xc000000
#define	BCCK_OPTION_RXPATH          0x3000000

#define	BNUM_OFSTF			0x3
#define	BSHIFT_L			0xc0
#define	BGI_TH				0xc
#define	BRXPATH_A			0x1
#define	BRXPATH_B			0x2
#define	BRXPATH_C			0x4
#define	BRXPATH_D			0x8
#define	BTXPATH_A			0x1
#define	BTXPATH_B			0x2
#define	BTXPATH_C			0x4
#define	BTXPATH_D			0x8
#define	BTRSSI_FREQ			0x200
#define	BADC_BACKOFF			0x3000
#define	BDFIR_BACKOFF			0xc000
#define	BTRSSI_LATCH_PHASE		0x10000
#define	BRX_LDC_OFFSET			0xff
#define	BRX_QDC_OFFSET			0xff00
#define	BRX_DFIR_MODE			0x1800000
#define	BRX_DCNF_TYPE			0xe000000
#define	BRXIQIMB_A			0x3ff
#define	BRXIQIMB_B			0xfc00
#define	BRXIQIMB_C			0x3f0000
#define	BRXIQIMB_D			0xffc00000
#define	BDC_DC_NOTCH			0x60000
#define	BRXNB_NOTCH			0x1f000000
#define	BPD_TH				0xf
#define	BPD_TH_OPT2			0xc000
#define	BPWED_TH			0x700
#define	BIFMF_WIN_L			0x800
#define	BPD_OPTION			0x1000
#define	BMF_WIN_L			0xe000
#define	BBW_SEARCH_L			0x30000
#define	BWIN_ENH_L			0xc0000
#define	BBW_TH				0x700000
#define	BED_TH2				0x3800000
#define	BBW_OPTION			0x4000000
#define	BRADIO_TH			0x18000000
#define	BWINDOW_L			0xe0000000
#define	BSBD_OPTION			0x1
#define	BFRAME_TH			0x1c
#define	BFS_OPTION			0x60
#define	BDC_SLOPE_CHECK			0x80
#define	BFGUARD_COUNTER_DC_L		0xe00
#define	BFRAME_WEIGHT_SHORT		0x7000
#define	BSUB_TUNE			0xe00000
#define	BFRAME_DC_LENGTH		0xe000000
#define	BSBD_START_OFFSET		0x30000000
#define	BFRAME_TH_2			0x7
#define	BFRAME_GI2_TH			0x38
#define	BGI2_SYNC_EN			0x40
#define	BSARCH_SHORT_EARLY		0x300
#define	BSARCH_SHORT_LATE		0xc00
#define	BSARCH_GI2_LATE			0x70000
#define	BCFOANTSUM			0x1
#define	BCFOACC				0x2
#define	BCFOSTARTOFFSET			0xc
#define	BCFOLOOPBACK			0x70
#define	BCFOSUMWEIGHT			0x80
#define	BDAGCENABLE			0x10000
#define	BTXIQIMB_A			0x3ff
#define	BTXIQIMB_b			0xfc00
#define	BTXIQIMB_C			0x3f0000
#define	BTXIQIMB_D			0xffc00000
#define	BTXIDCOFFSET			0xff
#define	BTXIQDCOFFSET			0xff00
#define	BTXDFIRMODE			0x10000
#define	BTXPESUDO_NOISEON		0x4000000
#define	BTXPESUDO_NOISE_A		0xff
#define	BTXPESUDO_NOISE_B		0xff00
#define	BTXPESUDO_NOISE_C		0xff0000
#define	BTXPESUDO_NOISE_D		0xff000000
#define	BCCA_DROPOPTION			0x20000
#define	BCCA_DROPTHRES			0xfff00000
#define	BEDCCA_H			0xf
#define	BEDCCA_L			0xf0
#define	BLAMBDA_ED			0x300
#define	BRX_INITIALGAIN			0x7f
#define	BRX_ANTDIV_EN			0x80
#define	BRX_AGC_ADDRESS_FOR_LNA     0x7f00
#define	BRX_HIGHPOWER_FLOW		0x8000
#define	BRX_AGC_FREEZE_THRES        0xc0000
#define	BRX_FREEZESTEP_AGC1		0x300000
#define	BRX_FREEZESTEP_AGC2		0xc00000
#define	BRX_FREEZESTEP_AGC3		0x3000000
#define	BRX_FREEZESTEP_AGC0		0xc000000
#define	BRXRSSI_CMP_EN			0x10000000
#define	BRXQUICK_AGCEN			0x20000000
#define	BRXAGC_FREEZE_THRES_MODE    0x40000000
#define	BRX_OVERFLOW_CHECKTYPE		0x80000000
#define	BRX_AGCSHIFT			0x7f
#define	BTRSW_TRI_ONLY			0x80
#define	BPOWER_THRES			0x300
#define	BRXAGC_EN			0x1
#define	BRXAGC_TOGETHER_EN		0x2
#define	BRXAGC_MIN			0x4
#define	BRXHP_INI			0x7
#define	BRXHP_TRLNA			0x70
#define	BRXHP_RSSI			0x700
#define	BRXHP_BBP1			0x7000
#define	BRXHP_BBP2			0x70000
#define	BRXHP_BBP3			0x700000
#define	BRSSI_H				0x7f0000
#define	BRSSI_GEN			0x7f000000
#define	BRXSETTLE_TRSW			0x7
#define	BRXSETTLE_LNA			0x38
#define	BRXSETTLE_RSSI			0x1c0
#define	BRXSETTLE_BBP			0xe00
#define	BRXSETTLE_RXHP			0x7000
#define	BRXSETTLE_ANTSW_RSSI		0x38000
#define	BRXSETTLE_ANTSW			0xc0000
#define	BRXPROCESS_TIME_DAGC		0x300000
#define	BRXSETTLE_HSSI			0x400000
#define	BRXPROCESS_TIME_BBPPW		0x800000
#define	BRXANTENNA_POWER_SHIFT		0x3000000
#define	BRSSI_TABLE_SELECT		0xc000000
#define	BRXHP_FINAL			0x7000000
#define	BRXHPSETTLE_BBP			0x7
#define	BRXHTSETTLE_HSSI		0x8
#define	BRXHTSETTLE_RXHP		0x70
#define	BRXHTSETTLE_BBPPW		0x80
#define	BRXHTSETTLE_IDLE		0x300
#define	BRXHTSETTLE_RESERVED		0x1c00
#define	BRXHT_RXHP_EN			0x8000
#define	BRXAGC_FREEZE_THRES		0x30000
#define	BRXAGC_TOGETHEREN		0x40000
#define	BRXHTAGC_MIN			0x80000
#define	BRXHTAGC_EN			0x100000
#define	BRXHTDAGC_EN			0x200000
#define	BRXHT_RXHP_BBP			0x1c00000
#define	BRXHT_RXHP_FINAL		0xe0000000
#define	BRXPW_RADIO_TH			0x3
#define	BRXPW_RADIO_EN			0x4
#define	BRXMF_HOLD			0x3800
#define	BRXPD_DELAY_TH1			0x38
#define	BRXPD_DELAY_TH2			0x1c0
#define	BRXPD_DC_COUNT_MAX		0x600
#define	BRXPD_DELAY_TH			0x8000
#define	BRXPROCESS_DELAY		0xf0000
#define	BRXSEARCHRANGE_GI2_EARLY	0x700000
#define	BRXFRAME_FUARD_COUNTER_L	0x3800000
#define	BRXSGI_GUARD_L			0xc000000
#define	BRXSGI_SEARCH_L			0x30000000
#define	BRXSGI_TH			0xc0000000
#define	BDFSCNT0			0xff
#define	BDFSCNT1			0xff00
#define	BDFSFLAG			0xf0000
#define	BMF_WEIGHT_SUM			0x300000
#define	BMINIDX_TH			0x7f000000
#define	BDAFORMAT			0x40000
#define	BTXCH_EMU_ENABLE		0x01000000
#define	BTRSW_ISOLATION_A		0x7f
#define	BTRSW_ISOLATION_B		0x7f00
#define	BTRSW_ISOLATION_C		0x7f0000
#define	BTRSW_ISOLATION_D		0x7f000000
#define	BEXT_LNA_GAIN			0x7c00

#define	BSTBC_EN			0x4
#define	BANTENNA_MAPPING		0x10
#define	BNSS				0x20
#define	BCFO_ANTSUM_ID              0x200
#define	BPHY_COUNTER_RESET		0x8000000
#define	BCFO_REPORT_GET			0x4000000
#define	BOFDM_CONTINUE_TX		0x10000000
#define	BOFDM_SINGLE_CARRIER		0x20000000
#define	BOFDM_SINGLE_TONE		0x40000000
#define	BHT_DETECT			0x100
#define	BCFOEN				0x10000
#define	BCFOVALUE			0xfff00000
#define	BSIGTONE_RE			0x3f
#define	BSIGTONE_IM			0x7f00
#define	BCOUNTER_CCA			0xffff
#define	BCOUNTER_PARITYFAIL		0xffff0000
#define	BCOUNTER_RATEILLEGAL		0xffff
#define	BCOUNTER_CRC8FAIL		0xffff0000
#define	BCOUNTER_MCSNOSUPPORT		0xffff
#define	BCOUNTER_FASTSYNC		0xffff
#define	BSHORTCFO			0xfff
#define	BSHORTCFOT_LENGTH		12
#define	BSHORTCFOF_LENGTH		11
#define	BLONGCFO			0x7ff
#define	BLONGCFOT_LENGTH		11
#define	BLONGCFOF_LENGTH		11
#define	BTAILCFO			0x1fff
#define	BTAILCFOT_LENGTH		13
#define	BTAILCFOF_LENGTH		12
#define	BNOISE_EN_PWDB			0xffff
#define	BCC_POWER_DB			0xffff0000
#define	BMOISE_PWDB			0xffff
#define	BPOWERMEAST_LENGTH		10
#define	BPOWERMEASF_LENGTH		3
#define	BRX_HT_BW			0x1
#define	BRXSC				0x6
#define	BRX_HT				0x8
#define	BNB_INTF_DET_ON			0x1
#define	BINTF_WIN_LEN_CFG		0x30
#define	BNB_INTF_TH_CFG			0x1c0
#define	BRFGAIN				0x3f
#define	BTABLESEL			0x40
#define	BTRSW				0x80
#define	BRXSNR_A			0xff
#define	BRXSNR_B			0xff00
#define	BRXSNR_C			0xff0000
#define	BRXSNR_D			0xff000000
#define	BSNR_EVMT_LENGTH		8
#define	BSNR_EVMF_LENGTH		1
#define	BCSI1ST				0xff
#define	BCSI2ND				0xff00
#define	BRXEVM1ST			0xff0000
#define	BRXEVM2ND			0xff000000
#define	BSIGEVM				0xff
#define	BPWDB				0xff00
#define	BSGIEN				0x10000

#define	BSFACTOR_QMA1			0xf
#define	BSFACTOR_QMA2			0xf0
#define	BSFACTOR_QMA3			0xf00
#define	BSFACTOR_QMA4			0xf000
#define	BSFACTOR_QMA5			0xf0000
#define	BSFACTOR_QMA6			0xf0000
#define	BSFACTOR_QMA7			0xf00000
#define	BSFACTOR_QMA8			0xf000000
#define	BSFACTOR_QMA9			0xf0000000
#define	BCSI_SCHEME			0x100000

#define	BNOISE_LVL_TOP_SET          0x3
#define	BCHSMOOTH			0x4
#define	BCHSMOOTH_CFG1			0x38
#define	BCHSMOOTH_CFG2			0x1c0
#define	BCHSMOOTH_CFG3			0xe00
#define	BCHSMOOTH_CFG4			0x7000
#define	BMRCMODE			0x800000
#define	BTHEVMCFG			0x7000000

#define	BLOOP_FIT_TYPE			0x1
#define	BUPD_CFO			0x40
#define	BUPD_CFO_OFFDATA		0x80
#define	BADV_UPD_CFO			0x100
#define	BADV_TIME_CTRL			0x800
#define	BUPD_CLKO			0x1000
#define	BFC				0x6000
#define	BTRACKING_MODE			0x8000
#define	BPHCMP_ENABLE			0x10000
#define	BUPD_CLKO_LTF			0x20000
#define	BCOM_CH_CFO			0x40000
#define	BCSI_ESTI_MODE			0x80000
#define	BADV_UPD_EQZ			0x100000
#define	BUCHCFG				0x7000000
#define	BUPDEQZ				0x8000000

#define	BRX_PESUDO_NOISE_ON         0x20000000
#define	BRX_PESUDO_NOISE_A		0xff
#define	BRX_PESUDO_NOISE_B		0xff00
#define	BRX_PESUDO_NOISE_C		0xff0000
#define	BRX_PESUDO_NOISE_D		0xff000000
#define	BRX_PESUDO_NOISESTATE_A     0xffff
#define	BRX_PESUDO_NOISESTATE_B     0xffff0000
#define	BRX_PESUDO_NOISESTATE_C     0xffff
#define	BRX_PESUDO_NOISESTATE_D     0xffff0000

#define	BZEBRA1_HSSIENABLE		0x8
#define	BZEBRA1_TRXCONTROL		0xc00
#define	BZEBRA1_TRXGAINSETTING		0x07f
#define	BZEBRA1_RXCOUNTER		0xc00
#define	BZEBRA1_TXCHANGEPUMP		0x38
#define	BZEBRA1_RXCHANGEPUMP		0x7
#define	BZEBRA1_CHANNEL_NUM		0xf80
#define	BZEBRA1_TXLPFBW			0x400
#define	BZEBRA1_RXLPFBW			0x600

#define	BRTL8256REG_MODE_CTRL1      0x100
#define	BRTL8256REG_MODE_CTRL0      0x40
#define	BRTL8256REG_TXLPFBW         0x18
#define	BRTL8256REG_RXLPFBW         0x600

#define	BRTL8258_TXLPFBW		0xc
#define	BRTL8258_RXLPFBW		0xc00
#define	BRTL8258_RSSILPFBW		0xc0

#define	BBYTE0				0x1
#define	BBYTE1				0x2
#define	BBYTE2				0x4
#define	BBYTE3				0x8
#define	BWORD0				0x3
#define	BWORD1				0xc
#define	BWORD				0xf

#define	MASKBYTE0			0xff
#define	MASKBYTE1			0xff00
#define	MASKBYTE2			0xff0000
#define	MASKBYTE3			0xff000000
#define	MASKHWORD			0xffff0000
#define	MASKLWORD			0x0000ffff
#define	MASKDWORD					0xffffffff
#define	MASK12BITS					0xfff
#define	MASKH4BITS					0xf0000000
#define MASKOFDM_D					0xffc00000
#define	MASKCCK						0x3f3f3f3f

#define	MASK4BITS			0x0f
#define	MASK20BITS			0xfffff
#define RFREG_OFFSET_MASK			0xfffff

#define	BENABLE				0x1
#define	BDISABLE			0x0

#define	LEFT_ANTENNA			0x0
#define	RIGHT_ANTENNA			0x1

#define	TCHECK_TXSTATUS			500
#define	TUPDATE_RXCOUNTER		100

#define	REG_UN_used_register		0x01bf

/* WOL bit information */
#define	HAL92C_WOL_PTK_UPDATE_EVENT		BIT(0)
#define	HAL92C_WOL_GTK_UPDATE_EVENT		BIT(1)
#define	HAL92C_WOL_DISASSOC_EVENT		BIT(2)
#define	HAL92C_WOL_DEAUTH_EVENT			BIT(3)
#define	HAL92C_WOL_FW_DISCONNECT_EVENT	BIT(4)

#define		WOL_REASON_PTK_UPDATE		BIT(0)
#define		WOL_REASON_GTK_UPDATE		BIT(1)
#define		WOL_REASON_DISASSOC			BIT(2)
#define		WOL_REASON_DEAUTH			BIT(3)
#define		WOL_REASON_FW_DISCONNECT	BIT(4)
#endif
