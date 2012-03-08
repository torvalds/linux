/******************************************************************************
 *
 * Copyright(c) 2009-2012  Realtek Corporation.
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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
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

#ifndef __RTL92D_REG_H__
#define __RTL92D_REG_H__

/* ----------------------------------------------------- */
/* 0x0000h ~ 0x00FFh System Configuration */
/* ----------------------------------------------------- */
#define REG_SYS_ISO_CTRL		0x0000
#define REG_SYS_FUNC_EN			0x0002
#define REG_APS_FSMCO			0x0004
#define REG_SYS_CLKR			0x0008
#define REG_9346CR			0x000A
#define REG_EE_VPD			0x000C
#define REG_AFE_MISC			0x0010
#define REG_SPS0_CTRL			0x0011
#define REG_POWER_OFF_IN_PROCESS	0x0017
#define REG_SPS_OCP_CFG			0x0018
#define REG_RSV_CTRL			0x001C
#define REG_RF_CTRL			0x001F
#define REG_LDOA15_CTRL			0x0020
#define REG_LDOV12D_CTRL		0x0021
#define REG_LDOHCI12_CTRL		0x0022
#define REG_LPLDO_CTRL			0x0023
#define REG_AFE_XTAL_CTRL		0x0024
#define REG_AFE_PLL_CTRL		0x0028
/* for 92d, DMDP,SMSP,DMSP contrl */
#define REG_MAC_PHY_CTRL		0x002c
#define REG_EFUSE_CTRL			0x0030
#define REG_EFUSE_TEST			0x0034
#define REG_PWR_DATA			0x0038
#define REG_CAL_TIMER			0x003C
#define REG_ACLK_MON			0x003E
#define REG_GPIO_MUXCFG			0x0040
#define REG_GPIO_IO_SEL			0x0042
#define REG_MAC_PINMUX_CFG		0x0043
#define REG_GPIO_PIN_CTRL		0x0044
#define REG_GPIO_INTM			0x0048
#define REG_LEDCFG0			0x004C
#define REG_LEDCFG1			0x004D
#define REG_LEDCFG2			0x004E
#define REG_LEDCFG3			0x004F
#define REG_FSIMR			0x0050
#define REG_FSISR			0x0054

#define REG_MCUFWDL			0x0080

#define REG_HMEBOX_EXT_0		0x0088
#define REG_HMEBOX_EXT_1		0x008A
#define REG_HMEBOX_EXT_2		0x008C
#define REG_HMEBOX_EXT_3		0x008E

#define REG_BIST_SCAN			0x00D0
#define REG_BIST_RPT			0x00D4
#define REG_BIST_ROM_RPT		0x00D8
#define REG_USB_SIE_INTF		0x00E0
#define REG_PCIE_MIO_INTF		0x00E4
#define REG_PCIE_MIO_INTD		0x00E8
#define REG_HPON_FSM			0x00EC
#define REG_SYS_CFG			0x00F0
#define REG_MAC_PHY_CTRL_NORMAL		0x00f8

#define  REG_MAC0			0x0081
#define  REG_MAC1			0x0053
#define  FW_MAC0_READY			0x18
#define  FW_MAC1_READY			0x1A
#define  MAC0_ON			BIT(7)
#define  MAC1_ON			BIT(0)
#define  MAC0_READY			BIT(0)
#define  MAC1_READY			BIT(0)

/* ----------------------------------------------------- */
/* 0x0100h ~ 0x01FFh	MACTOP General Configuration */
/* ----------------------------------------------------- */
#define REG_CR				0x0100
#define REG_PBP				0x0104
#define REG_TRXDMA_CTRL			0x010C
#define REG_TRXFF_BNDY			0x0114
#define REG_TRXFF_STATUS		0x0118
#define REG_RXFF_PTR			0x011C
#define REG_HIMR			0x0120
#define REG_HISR			0x0124
#define REG_HIMRE			0x0128
#define REG_HISRE			0x012C
#define REG_CPWM			0x012F
#define REG_FWIMR			0x0130
#define REG_FWISR			0x0134
#define REG_PKTBUF_DBG_CTRL		0x0140
#define REG_PKTBUF_DBG_DATA_L		0x0144
#define REG_PKTBUF_DBG_DATA_H		0x0148

#define REG_TC0_CTRL			0x0150
#define REG_TC1_CTRL			0x0154
#define REG_TC2_CTRL			0x0158
#define REG_TC3_CTRL			0x015C
#define REG_TC4_CTRL			0x0160
#define REG_TCUNIT_BASE			0x0164
#define REG_MBIST_START			0x0174
#define REG_MBIST_DONE			0x0178
#define REG_MBIST_FAIL			0x017C
#define REG_C2HEVT_MSG_NORMAL		0x01A0
#define REG_C2HEVT_MSG_TEST		0x01B8
#define REG_C2HEVT_CLEAR		0x01BF
#define REG_MCUTST_1			0x01c0
#define REG_FMETHR			0x01C8
#define REG_HMETFR			0x01CC
#define REG_HMEBOX_0			0x01D0
#define REG_HMEBOX_1			0x01D4
#define REG_HMEBOX_2			0x01D8
#define REG_HMEBOX_3			0x01DC

#define REG_LLT_INIT			0x01E0
#define REG_BB_ACCEESS_CTRL		0x01E8
#define REG_BB_ACCESS_DATA		0x01EC


/* ----------------------------------------------------- */
/*	0x0200h ~ 0x027Fh	TXDMA Configuration */
/* ----------------------------------------------------- */
#define REG_RQPN			0x0200
#define REG_FIFOPAGE			0x0204
#define REG_TDECTRL			0x0208
#define REG_TXDMA_OFFSET_CHK		0x020C
#define REG_TXDMA_STATUS		0x0210
#define REG_RQPN_NPQ			0x0214

/* ----------------------------------------------------- */
/*	0x0280h ~ 0x02FFh	RXDMA Configuration */
/* ----------------------------------------------------- */
#define REG_RXDMA_AGG_PG_TH		0x0280
#define REG_RXPKT_NUM			0x0284
#define REG_RXDMA_STATUS		0x0288

/* ----------------------------------------------------- */
/*	0x0300h ~ 0x03FFh	PCIe  */
/* ----------------------------------------------------- */
#define	REG_PCIE_CTRL_REG		0x0300
#define	REG_INT_MIG			0x0304
#define	REG_BCNQ_DESA			0x0308
#define	REG_HQ_DESA			0x0310
#define	REG_MGQ_DESA			0x0318
#define	REG_VOQ_DESA			0x0320
#define	REG_VIQ_DESA			0x0328
#define	REG_BEQ_DESA			0x0330
#define	REG_BKQ_DESA			0x0338
#define	REG_RX_DESA			0x0340
#define	REG_DBI				0x0348
#define	REG_DBI_WDATA			0x0348
#define REG_DBI_RDATA			0x034C
#define REG_DBI_CTRL			0x0350
#define REG_DBI_FLAG			0x0352
#define	REG_MDIO			0x0354
#define	REG_DBG_SEL			0x0360
#define	REG_PCIE_HRPWM			0x0361
#define	REG_PCIE_HCPWM			0x0363
#define	REG_UART_CTRL			0x0364
#define	REG_UART_TX_DESA		0x0370
#define	REG_UART_RX_DESA		0x0378

/* ----------------------------------------------------- */
/*	0x0400h ~ 0x047Fh	Protocol Configuration  */
/* ----------------------------------------------------- */
#define REG_VOQ_INFORMATION		0x0400
#define REG_VIQ_INFORMATION		0x0404
#define REG_BEQ_INFORMATION		0x0408
#define REG_BKQ_INFORMATION		0x040C
#define REG_MGQ_INFORMATION		0x0410
#define REG_HGQ_INFORMATION		0x0414
#define REG_BCNQ_INFORMATION		0x0418


#define REG_CPU_MGQ_INFORMATION		0x041C
#define REG_FWHW_TXQ_CTRL		0x0420
#define REG_HWSEQ_CTRL			0x0423
#define REG_TXPKTBUF_BCNQ_BDNY		0x0424
#define REG_TXPKTBUF_MGQ_BDNY		0x0425
#define REG_MULTI_BCNQ_EN		0x0426
#define REG_MULTI_BCNQ_OFFSET		0x0427
#define REG_SPEC_SIFS			0x0428
#define REG_RL				0x042A
#define REG_DARFRC			0x0430
#define REG_RARFRC			0x0438
#define REG_RRSR			0x0440
#define REG_ARFR0			0x0444
#define REG_ARFR1			0x0448
#define REG_ARFR2			0x044C
#define REG_ARFR3			0x0450
#define REG_AGGLEN_LMT			0x0458
#define REG_AMPDU_MIN_SPACE		0x045C
#define REG_TXPKTBUF_WMAC_LBK_BF_HD	0x045D
#define REG_FAST_EDCA_CTRL		0x0460
#define REG_RD_RESP_PKT_TH		0x0463
#define REG_INIRTS_RATE_SEL		0x0480
#define REG_INIDATA_RATE_SEL		0x0484
#define REG_POWER_STATUS		0x04A4
#define REG_POWER_STAGE1		0x04B4
#define REG_POWER_STAGE2		0x04B8
#define REG_PKT_LIFE_TIME		0x04C0
#define REG_STBC_SETTING		0x04C4
#define REG_PROT_MODE_CTRL		0x04C8
#define REG_MAX_AGGR_NUM		0x04CA
#define REG_RTS_MAX_AGGR_NUM		0x04CB
#define REG_BAR_MODE_CTRL		0x04CC
#define REG_RA_TRY_RATE_AGG_LMT		0x04CF
#define REG_EARLY_MODE_CONTROL		0x4D0
#define REG_NQOS_SEQ			0x04DC
#define REG_QOS_SEQ			0x04DE
#define REG_NEED_CPU_HANDLE		0x04E0
#define REG_PKT_LOSE_RPT		0x04E1
#define REG_PTCL_ERR_STATUS		0x04E2
#define REG_DUMMY			0x04FC

/* ----------------------------------------------------- */
/*	0x0500h ~ 0x05FFh	EDCA Configuration   */
/* ----------------------------------------------------- */
#define REG_EDCA_VO_PARAM		0x0500
#define REG_EDCA_VI_PARAM		0x0504
#define REG_EDCA_BE_PARAM		0x0508
#define REG_EDCA_BK_PARAM		0x050C
#define REG_BCNTCFG			0x0510
#define REG_PIFS			0x0512
#define REG_RDG_PIFS			0x0513
#define REG_SIFS_CTX			0x0514
#define REG_SIFS_TRX			0x0516
#define REG_AGGR_BREAK_TIME		0x051A
#define REG_SLOT			0x051B
#define REG_TX_PTCL_CTRL		0x0520
#define REG_TXPAUSE			0x0522
#define REG_DIS_TXREQ_CLR		0x0523
#define REG_RD_CTRL			0x0524
#define REG_TBTT_PROHIBIT		0x0540
#define REG_RD_NAV_NXT			0x0544
#define REG_NAV_PROT_LEN		0x0546
#define REG_BCN_CTRL			0x0550
#define REG_USTIME_TSF			0x0551
#define REG_MBID_NUM			0x0552
#define REG_DUAL_TSF_RST		0x0553
#define REG_BCN_INTERVAL		0x0554
#define REG_MBSSID_BCN_SPACE		0x0554
#define REG_DRVERLYINT			0x0558
#define REG_BCNDMATIM			0x0559
#define REG_ATIMWND			0x055A
#define REG_BCN_MAX_ERR			0x055D
#define REG_RXTSF_OFFSET_CCK		0x055E
#define REG_RXTSF_OFFSET_OFDM		0x055F
#define REG_TSFTR			0x0560
#define REG_INIT_TSFTR			0x0564
#define REG_PSTIMER			0x0580
#define REG_TIMER0			0x0584
#define REG_TIMER1			0x0588
#define REG_ACMHWCTRL			0x05C0
#define REG_ACMRSTCTRL			0x05C1
#define REG_ACMAVG			0x05C2
#define REG_VO_ADMTIME			0x05C4
#define REG_VI_ADMTIME			0x05C6
#define REG_BE_ADMTIME			0x05C8
#define REG_EDCA_RANDOM_GEN		0x05CC
#define REG_SCH_TXCMD			0x05D0

/* Dual MAC Co-Existence Register  */
#define REG_DMC				0x05F0

/* ----------------------------------------------------- */
/*	0x0600h ~ 0x07FFh	WMAC Configuration */
/* ----------------------------------------------------- */
#define REG_APSD_CTRL			0x0600
#define REG_BWOPMODE			0x0603
#define REG_TCR				0x0604
#define REG_RCR				0x0608
#define REG_RX_PKT_LIMIT		0x060C
#define REG_RX_DLK_TIME			0x060D
#define REG_RX_DRVINFO_SZ		0x060F

#define REG_MACID			0x0610
#define REG_BSSID			0x0618
#define REG_MAR				0x0620
#define REG_MBIDCAMCFG			0x0628

#define REG_USTIME_EDCA			0x0638
#define REG_MAC_SPEC_SIFS		0x063A
#define REG_RESP_SIFS_CCK		0x063C
#define REG_RESP_SIFS_OFDM		0x063E
#define REG_ACKTO			0x0640
#define REG_CTS2TO			0x0641
#define REG_EIFS			0x0642


/* WMA, BA, CCX */
#define REG_NAV_CTRL			0x0650
#define REG_BACAMCMD			0x0654
#define REG_BACAMCONTENT		0x0658
#define REG_LBDLY			0x0660
#define REG_FWDLY			0x0661
#define REG_RXERR_RPT			0x0664
#define REG_WMAC_TRXPTCL_CTL		0x0668


/* Security  */
#define REG_CAMCMD			0x0670
#define REG_CAMWRITE			0x0674
#define REG_CAMREAD			0x0678
#define REG_CAMDBG			0x067C
#define REG_SECCFG			0x0680

/* Power  */
#define REG_WOW_CTRL			0x0690
#define REG_PSSTATUS			0x0691
#define REG_PS_RX_INFO			0x0692
#define REG_LPNAV_CTRL			0x0694
#define REG_WKFMCAM_CMD			0x0698
#define REG_WKFMCAM_RWD			0x069C
#define REG_RXFLTMAP0			0x06A0
#define REG_RXFLTMAP1			0x06A2
#define REG_RXFLTMAP2			0x06A4
#define REG_BCN_PSR_RPT			0x06A8
#define REG_CALB32K_CTRL		0x06AC
#define REG_PKT_MON_CTRL		0x06B4
#define REG_BT_COEX_TABLE		0x06C0
#define REG_WMAC_RESP_TXINFO		0x06D8


/* ----------------------------------------------------- */
/*	Redifine 8192C register definition for compatibility */
/* ----------------------------------------------------- */
#define	CR9346				REG_9346CR
#define	MSR				(REG_CR + 2)
#define	ISR				REG_HISR
#define	TSFR				REG_TSFTR

#define	MACIDR0				REG_MACID
#define	MACIDR4				(REG_MACID + 4)

#define PBP				REG_PBP

#define	IDR0				MACIDR0
#define	IDR4				MACIDR4

/* ----------------------------------------------------- */
/* 8192C (MSR) Media Status Register(Offset 0x4C, 8 bits)*/
/* ----------------------------------------------------- */
#define	MSR_NOLINK			0x00
#define	MSR_ADHOC			0x01
#define	MSR_INFRA			0x02
#define	MSR_AP				0x03

/* 6. Adaptive Control Registers  (Offset: 0x0160 - 0x01CF) */
/* ----------------------------------------------------- */
/* 8192C Response Rate Set Register(offset 0x181, 24bits)*/
/* ----------------------------------------------------- */
#define	RRSR_RSC_OFFSET			21
#define	RRSR_SHORT_OFFSET		23
#define	RRSR_RSC_BW_40M			0x600000
#define	RRSR_RSC_UPSUBCHNL		0x400000
#define	RRSR_RSC_LOWSUBCHNL		0x200000
#define	RRSR_SHORT			0x800000
#define	RRSR_1M				BIT0
#define	RRSR_2M				BIT1
#define	RRSR_5_5M			BIT2
#define	RRSR_11M			BIT3
#define	RRSR_6M				BIT4
#define	RRSR_9M				BIT5
#define	RRSR_12M			BIT6
#define	RRSR_18M			BIT7
#define	RRSR_24M			BIT8
#define	RRSR_36M			BIT9
#define	RRSR_48M			BIT10
#define	RRSR_54M			BIT11
#define	RRSR_MCS0			BIT12
#define	RRSR_MCS1			BIT13
#define	RRSR_MCS2			BIT14
#define	RRSR_MCS3			BIT15
#define	RRSR_MCS4			BIT16
#define	RRSR_MCS5			BIT17
#define	RRSR_MCS6			BIT18
#define	RRSR_MCS7			BIT19
#define	BRSR_ACKSHORTPMB		BIT23

/* ----------------------------------------------------- */
/*       8192C Rate Definition  */
/* ----------------------------------------------------- */
/* CCK */
#define	RATR_1M				0x00000001
#define	RATR_2M				0x00000002
#define	RATR_55M			0x00000004
#define	RATR_11M			0x00000008
/* OFDM */
#define	RATR_6M				0x00000010
#define	RATR_9M				0x00000020
#define	RATR_12M			0x00000040
#define	RATR_18M			0x00000080
#define	RATR_24M			0x00000100
#define	RATR_36M			0x00000200
#define	RATR_48M			0x00000400
#define	RATR_54M			0x00000800
/* MCS 1 Spatial Stream	*/
#define	RATR_MCS0			0x00001000
#define	RATR_MCS1			0x00002000
#define	RATR_MCS2			0x00004000
#define	RATR_MCS3			0x00008000
#define	RATR_MCS4			0x00010000
#define	RATR_MCS5			0x00020000
#define	RATR_MCS6			0x00040000
#define	RATR_MCS7			0x00080000
/* MCS 2 Spatial Stream */
#define	RATR_MCS8			0x00100000
#define	RATR_MCS9			0x00200000
#define	RATR_MCS10			0x00400000
#define	RATR_MCS11			0x00800000
#define	RATR_MCS12			0x01000000
#define	RATR_MCS13			0x02000000
#define	RATR_MCS14			0x04000000
#define	RATR_MCS15			0x08000000

/* CCK */
#define RATE_1M				BIT(0)
#define RATE_2M				BIT(1)
#define RATE_5_5M			BIT(2)
#define RATE_11M			BIT(3)
/* OFDM  */
#define RATE_6M				BIT(4)
#define RATE_9M				BIT(5)
#define RATE_12M			BIT(6)
#define RATE_18M			BIT(7)
#define RATE_24M			BIT(8)
#define RATE_36M			BIT(9)
#define RATE_48M			BIT(10)
#define RATE_54M			BIT(11)
/* MCS 1 Spatial Stream */
#define RATE_MCS0			BIT(12)
#define RATE_MCS1			BIT(13)
#define RATE_MCS2			BIT(14)
#define RATE_MCS3			BIT(15)
#define RATE_MCS4			BIT(16)
#define RATE_MCS5			BIT(17)
#define RATE_MCS6			BIT(18)
#define RATE_MCS7			BIT(19)
/* MCS 2 Spatial Stream */
#define RATE_MCS8			BIT(20)
#define RATE_MCS9			BIT(21)
#define RATE_MCS10			BIT(22)
#define RATE_MCS11			BIT(23)
#define RATE_MCS12			BIT(24)
#define RATE_MCS13			BIT(25)
#define RATE_MCS14			BIT(26)
#define RATE_MCS15			BIT(27)

/* ALL CCK Rate */
#define	RATE_ALL_CCK			(RATR_1M | RATR_2M | RATR_55M | \
					RATR_11M)
#define	RATE_ALL_OFDM_AG		(RATR_6M | RATR_9M | RATR_12M | \
					RATR_18M | RATR_24M | \
					RATR_36M | RATR_48M | RATR_54M)
#define	RATE_ALL_OFDM_1SS		(RATR_MCS0 | RATR_MCS1 | RATR_MCS2 | \
					RATR_MCS3 | RATR_MCS4 | RATR_MCS5 | \
					RATR_MCS6 | RATR_MCS7)
#define	RATE_ALL_OFDM_2SS		(RATR_MCS8 | RATR_MCS9 | RATR_MCS10 | \
					RATR_MCS11 | RATR_MCS12 | RATR_MCS13 | \
					RATR_MCS14 | RATR_MCS15)

/* ----------------------------------------------------- */
/*    8192C BW_OPMODE bits		(Offset 0x203, 8bit)     */
/* ----------------------------------------------------- */
#define	BW_OPMODE_20MHZ			BIT(2)
#define	BW_OPMODE_5G			BIT(1)
#define	BW_OPMODE_11J			BIT(0)


/* ----------------------------------------------------- */
/*     8192C CAM Config Setting (offset 0x250, 1 byte)   */
/* ----------------------------------------------------- */
#define	CAM_VALID			BIT(15)
#define	CAM_NOTVALID			0x0000
#define	CAM_USEDK			BIT(5)

#define	CAM_NONE			0x0
#define	CAM_WEP40			0x01
#define	CAM_TKIP			0x02
#define	CAM_AES				0x04
#define	CAM_WEP104			0x05
#define	CAM_SMS4			0x6


#define	TOTAL_CAM_ENTRY			32
#define	HALF_CAM_ENTRY			16

#define	CAM_WRITE			BIT(16)
#define	CAM_READ			0x00000000
#define	CAM_POLLINIG			BIT(31)

/* 10. Power Save Control Registers	 (Offset: 0x0260 - 0x02DF) */
#define	WOW_PMEN			BIT0 /* Power management Enable. */
#define	WOW_WOMEN			BIT1 /* WoW function on or off. */
#define	WOW_MAGIC			BIT2 /* Magic packet */
#define	WOW_UWF				BIT3 /* Unicast Wakeup frame. */

/* 12. Host Interrupt Status Registers	 (Offset: 0x0300 - 0x030F) */
/* ----------------------------------------------------- */
/*      8190 IMR/ISR bits	(offset 0xfd,  8bits) */
/* ----------------------------------------------------- */
#define	IMR8190_DISABLED		0x0
#define	IMR_BCNDMAINT6			BIT(31)
#define	IMR_BCNDMAINT5			BIT(30)
#define	IMR_BCNDMAINT4			BIT(29)
#define	IMR_BCNDMAINT3			BIT(28)
#define	IMR_BCNDMAINT2			BIT(27)
#define	IMR_BCNDMAINT1			BIT(26)
#define	IMR_BCNDOK8			BIT(25)
#define	IMR_BCNDOK7			BIT(24)
#define	IMR_BCNDOK6			BIT(23)
#define	IMR_BCNDOK5			BIT(22)
#define	IMR_BCNDOK4			BIT(21)
#define	IMR_BCNDOK3			BIT(20)
#define	IMR_BCNDOK2			BIT(19)
#define	IMR_BCNDOK1			BIT(18)
#define	IMR_TIMEOUT2			BIT(17)
#define	IMR_TIMEOUT1			BIT(16)
#define	IMR_TXFOVW			BIT(15)
#define	IMR_PSTIMEOUT			BIT(14)
#define	IMR_BcnInt			BIT(13)
#define	IMR_RXFOVW			BIT(12)
#define	IMR_RDU				BIT(11)
#define	IMR_ATIMEND			BIT(10)
#define	IMR_BDOK			BIT(9)
#define	IMR_HIGHDOK			BIT(8)
#define	IMR_TBDOK			BIT(7)
#define	IMR_MGNTDOK			BIT(6)
#define	IMR_TBDER			BIT(5)
#define	IMR_BKDOK			BIT(4)
#define	IMR_BEDOK			BIT(3)
#define	IMR_VIDOK			BIT(2)
#define	IMR_VODOK			BIT(1)
#define	IMR_ROK				BIT(0)

#define	IMR_TXERR			BIT(11)
#define	IMR_RXERR			BIT(10)
#define	IMR_C2HCMD			BIT(9)
#define	IMR_CPWM			BIT(8)
#define	IMR_OCPINT			BIT(1)
#define	IMR_WLANOFF			BIT(0)

/* ----------------------------------------------------- */
/* 8192C EFUSE */
/* ----------------------------------------------------- */
#define	HWSET_MAX_SIZE			256
#define EFUSE_MAX_SECTION		32
#define EFUSE_REAL_CONTENT_LEN		512

/* ----------------------------------------------------- */
/*     8192C EEPROM/EFUSE share register definition. */
/* ----------------------------------------------------- */
#define	EEPROM_DEFAULT_TSSI			0x0
#define EEPROM_DEFAULT_CRYSTALCAP		0x0
#define	EEPROM_DEFAULT_THERMALMETER		0x12

#define	EEPROM_DEFAULT_TXPOWERLEVEL_2G		0x2C
#define	EEPROM_DEFAULT_TXPOWERLEVEL_5G		0x22

#define	EEPROM_DEFAULT_HT40_2SDIFF		0x0
/* HT20<->40 default Tx Power Index Difference */
#define EEPROM_DEFAULT_HT20_DIFF		2
/* OFDM Tx Power index diff */
#define	EEPROM_DEFAULT_LEGACYHTTXPOWERDIFF	0x4
#define EEPROM_DEFAULT_HT40_PWRMAXOFFSET	0
#define EEPROM_DEFAULT_HT20_PWRMAXOFFSET	0

#define	EEPROM_CHANNEL_PLAN_FCC			0x0
#define	EEPROM_CHANNEL_PLAN_IC			0x1
#define	EEPROM_CHANNEL_PLAN_ETSI		0x2
#define	EEPROM_CHANNEL_PLAN_SPAIN		0x3
#define	EEPROM_CHANNEL_PLAN_FRANCE		0x4
#define	EEPROM_CHANNEL_PLAN_MKK			0x5
#define	EEPROM_CHANNEL_PLAN_MKK1		0x6
#define	EEPROM_CHANNEL_PLAN_ISRAEL		0x7
#define	EEPROM_CHANNEL_PLAN_TELEC		0x8
#define	EEPROM_CHANNEL_PLAN_GLOBAL_DOMAIN	0x9
#define	EEPROM_CHANNEL_PLAN_WORLD_WIDE_13	0xA
#define	EEPROM_CHANNEL_PLAN_NCC			0xB
#define	EEPROM_CHANNEL_PLAN_BY_HW_MASK		0x80

#define EEPROM_CID_DEFAULT			0x0
#define EEPROM_CID_TOSHIBA			0x4
#define	EEPROM_CID_CCX				0x10
#define	EEPROM_CID_QMI				0x0D
#define EEPROM_CID_WHQL				0xFE


#define	RTL8192_EEPROM_ID			0x8129
#define	EEPROM_WAPI_SUPPORT			0x78


#define RTL8190_EEPROM_ID		0x8129	/* 0-1 */
#define EEPROM_HPON			0x02 /* LDO settings.2-5 */
#define EEPROM_CLK			0x06 /* Clock settings.6-7 */
#define EEPROM_MAC_FUNCTION		0x08 /* SE Test mode.8 */

#define EEPROM_VID			0x28 /* SE Vendor ID.A-B */
#define EEPROM_DID			0x2A /* SE Device ID. C-D */
#define EEPROM_SVID			0x2C /* SE Vendor ID.E-F */
#define EEPROM_SMID			0x2E /* SE PCI Subsystem ID. 10-11 */

#define EEPROM_MAC_ADDR			0x16 /* SEMAC Address. 12-17 */
#define EEPROM_MAC_ADDR_MAC0_92D	0x55
#define EEPROM_MAC_ADDR_MAC1_92D	0x5B

/* 2.4G band Tx power index setting */
#define EEPROM_CCK_TX_PWR_INX_2G	0x61
#define EEPROM_HT40_1S_TX_PWR_INX_2G	0x67
#define EEPROM_HT40_2S_TX_PWR_INX_DIFF_2G	0x6D
#define EEPROM_HT20_TX_PWR_INX_DIFF_2G		0x70
#define EEPROM_OFDM_TX_PWR_INX_DIFF_2G		0x73
#define EEPROM_HT40_MAX_PWR_OFFSET_2G		0x76
#define EEPROM_HT20_MAX_PWR_OFFSET_2G		0x79

/*5GL channel 32-64 */
#define EEPROM_HT40_1S_TX_PWR_INX_5GL		0x7C
#define EEPROM_HT40_2S_TX_PWR_INX_DIFF_5GL	0x82
#define EEPROM_HT20_TX_PWR_INX_DIFF_5GL		0x85
#define EEPROM_OFDM_TX_PWR_INX_DIFF_5GL		0x88
#define EEPROM_HT40_MAX_PWR_OFFSET_5GL		0x8B
#define EEPROM_HT20_MAX_PWR_OFFSET_5GL		0x8E

/* 5GM channel 100-140 */
#define EEPROM_HT40_1S_TX_PWR_INX_5GM		0x91
#define EEPROM_HT40_2S_TX_PWR_INX_DIFF_5GM	0x97
#define EEPROM_HT20_TX_PWR_INX_DIFF_5GM		0x9A
#define EEPROM_OFDM_TX_PWR_INX_DIFF_5GM		0x9D
#define EEPROM_HT40_MAX_PWR_OFFSET_5GM		0xA0
#define EEPROM_HT20_MAX_PWR_OFFSET_5GM		0xA3

/* 5GH channel 149-165 */
#define EEPROM_HT40_1S_TX_PWR_INX_5GH		0xA6
#define EEPROM_HT40_2S_TX_PWR_INX_DIFF_5GH	0xAC
#define EEPROM_HT20_TX_PWR_INX_DIFF_5GH		0xAF
#define EEPROM_OFDM_TX_PWR_INX_DIFF_5GH		0xB2
#define EEPROM_HT40_MAX_PWR_OFFSET_5GH		0xB5
#define EEPROM_HT20_MAX_PWR_OFFSET_5GH		0xB8

/* Map of supported channels. */
#define EEPROM_CHANNEL_PLAN			0xBB
#define EEPROM_IQK_DELTA			0xBC
#define EEPROM_LCK_DELTA			0xBC
#define EEPROM_XTAL_K				0xBD	/* [7:5] */
#define EEPROM_TSSI_A_5G			0xBE
#define EEPROM_TSSI_B_5G			0xBF
#define EEPROM_TSSI_AB_5G			0xC0
#define EEPROM_THERMAL_METER			0xC3	/* [4:0] */
#define EEPROM_RF_OPT1				0xC4
#define EEPROM_RF_OPT2				0xC5
#define EEPROM_RF_OPT3				0xC6
#define EEPROM_RF_OPT4				0xC7
#define EEPROM_RF_OPT5				0xC8
#define EEPROM_RF_OPT6				0xC9
#define EEPROM_VERSION				0xCA
#define EEPROM_CUSTOMER_ID			0xCB
#define EEPROM_RF_OPT7				0xCC

#define EEPROM_DEF_PART_NO			0x3FD    /* Byte */
#define EEPROME_CHIP_VERSION_L			0x3FF
#define EEPROME_CHIP_VERSION_H			0x3FE

/*
 * Current IOREG MAP
 * 0x0000h ~ 0x00FFh   System Configuration (256 Bytes)
 * 0x0100h ~ 0x01FFh   MACTOP General Configuration (256 Bytes)
 * 0x0200h ~ 0x027Fh   TXDMA Configuration (128 Bytes)
 * 0x0280h ~ 0x02FFh   RXDMA Configuration (128 Bytes)
 * 0x0300h ~ 0x03FFh   PCIE EMAC Reserved Region (256 Bytes)
 * 0x0400h ~ 0x04FFh   Protocol Configuration (256 Bytes)
 * 0x0500h ~ 0x05FFh   EDCA Configuration (256 Bytes)
 * 0x0600h ~ 0x07FFh   WMAC Configuration (512 Bytes)
 * 0x2000h ~ 0x3FFFh   8051 FW Download Region (8196 Bytes)
 */

/* ----------------------------------------------------- */
/* 8192C (RCR)	(Offset 0x608, 32 bits) */
/* ----------------------------------------------------- */
#define	RCR_APPFCS				BIT(31)
#define	RCR_APP_MIC				BIT(30)
#define	RCR_APP_ICV				BIT(29)
#define	RCR_APP_PHYST_RXFF			BIT(28)
#define	RCR_APP_BA_SSN				BIT(27)
#define	RCR_ENMBID				BIT(24)
#define	RCR_LSIGEN				BIT(23)
#define	RCR_MFBEN				BIT(22)
#define	RCR_HTC_LOC_CTRL			BIT(14)
#define	RCR_AMF					BIT(13)
#define	RCR_ACF					BIT(12)
#define	RCR_ADF					BIT(11)
#define	RCR_AICV				BIT(9)
#define	RCR_ACRC32				BIT(8)
#define	RCR_CBSSID_BCN				BIT(7)
#define	RCR_CBSSID_DATA				BIT(6)
#define	RCR_APWRMGT				BIT(5)
#define	RCR_ADD3				BIT(4)
#define	RCR_AB					BIT(3)
#define	RCR_AM					BIT(2)
#define	RCR_APM					BIT(1)
#define	RCR_AAP					BIT(0)
#define	RCR_MXDMA_OFFSET			8
#define	RCR_FIFO_OFFSET				13

/* ----------------------------------------------------- */
/*       8192C Regsiter Bit and Content definition	 */
/* ----------------------------------------------------- */
/* ----------------------------------------------------- */
/*	0x0000h ~ 0x00FFh	System Configuration */
/* ----------------------------------------------------- */

/* SPS0_CTRL */
#define SW18_FPWM				BIT(3)


/* SYS_ISO_CTRL */
#define ISO_MD2PP				BIT(0)
#define ISO_UA2USB				BIT(1)
#define ISO_UD2CORE				BIT(2)
#define ISO_PA2PCIE				BIT(3)
#define ISO_PD2CORE				BIT(4)
#define ISO_IP2MAC				BIT(5)
#define ISO_DIOP				BIT(6)
#define ISO_DIOE				BIT(7)
#define ISO_EB2CORE				BIT(8)
#define ISO_DIOR				BIT(9)

#define PWC_EV25V				BIT(14)
#define PWC_EV12V				BIT(15)


/* SYS_FUNC_EN */
#define FEN_BBRSTB				BIT(0)
#define FEN_BB_GLB_RSTn				BIT(1)
#define FEN_USBA				BIT(2)
#define FEN_UPLL				BIT(3)
#define FEN_USBD				BIT(4)
#define FEN_DIO_PCIE				BIT(5)
#define FEN_PCIEA				BIT(6)
#define FEN_PPLL				BIT(7)
#define FEN_PCIED				BIT(8)
#define FEN_DIOE				BIT(9)
#define FEN_CPUEN				BIT(10)
#define FEN_DCORE				BIT(11)
#define FEN_ELDR				BIT(12)
#define FEN_DIO_RF				BIT(13)
#define FEN_HWPDN				BIT(14)
#define FEN_MREGEN				BIT(15)

/* APS_FSMCO */
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

/* SYS_CLKR */
#define ANAD16V_EN				BIT(0)
#define ANA8M					BIT(1)
#define MACSLP					BIT(4)
#define LOADER_CLK_EN				BIT(5)
#define _80M_SSC_DIS				BIT(7)
#define _80M_SSC_EN_HO				BIT(8)
#define PHY_SSC_RSTB				BIT(9)
#define SEC_CLK_EN				BIT(10)
#define MAC_CLK_EN				BIT(11)
#define SYS_CLK_EN				BIT(12)
#define RING_CLK_EN				BIT(13)


/* 9346CR */
#define	BOOT_FROM_EEPROM			BIT(4)
#define	EEPROM_EN				BIT(5)

/* AFE_MISC */
#define AFE_BGEN				BIT(0)
#define AFE_MBEN				BIT(1)
#define MAC_ID_EN				BIT(7)

/* RSV_CTRL */
#define WLOCK_ALL				BIT(0)
#define WLOCK_00				BIT(1)
#define WLOCK_04				BIT(2)
#define WLOCK_08				BIT(3)
#define WLOCK_40				BIT(4)
#define R_DIS_PRST_0				BIT(5)
#define R_DIS_PRST_1				BIT(6)
#define LOCK_ALL_EN				BIT(7)

/* RF_CTRL */
#define RF_EN					BIT(0)
#define RF_RSTB					BIT(1)
#define RF_SDMRSTB				BIT(2)



/* LDOA15_CTRL */
#define LDA15_EN				BIT(0)
#define LDA15_STBY				BIT(1)
#define LDA15_OBUF				BIT(2)
#define LDA15_REG_VOS				BIT(3)
#define _LDA15_VOADJ(x)				(((x) & 0x7) << 4)



/* LDOV12D_CTRL */
#define LDV12_EN				BIT(0)
#define LDV12_SDBY				BIT(1)
#define LPLDO_HSM				BIT(2)
#define LPLDO_LSM_DIS				BIT(3)
#define _LDV12_VADJ(x)				(((x) & 0xF) << 4)


/* AFE_XTAL_CTRL */
#define XTAL_EN					BIT(0)
#define XTAL_BSEL				BIT(1)
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


#define CKDLY_AFE				BIT(26)
#define CKDLY_USB				BIT(27)
#define CKDLY_DIG				BIT(28)
#define CKDLY_BT				BIT(29)


/* AFE_PLL_CTRL */
#define APLL_EN					BIT(0)
#define APLL_320_EN				BIT(1)
#define APLL_FREF_SEL				BIT(2)
#define APLL_EDGE_SEL				BIT(3)
#define APLL_WDOGB				BIT(4)
#define APLL_LPFEN				BIT(5)

#define APLL_REF_CLK_13MHZ			0x1
#define APLL_REF_CLK_19_2MHZ			0x2
#define APLL_REF_CLK_20MHZ			0x3
#define APLL_REF_CLK_25MHZ			0x4
#define APLL_REF_CLK_26MHZ			0x5
#define APLL_REF_CLK_38_4MHZ			0x6
#define APLL_REF_CLK_40MHZ			0x7

#define APLL_320EN				BIT(14)
#define APLL_80EN				BIT(15)
#define APLL_1MEN				BIT(24)


/* EFUSE_CTRL */
#define ALD_EN					BIT(18)
#define EF_PD					BIT(19)
#define EF_FLAG					BIT(31)

/* EFUSE_TEST  */
#define EF_TRPT					BIT(7)
#define LDOE25_EN				BIT(31)

/* MCUFWDL  */
#define MCUFWDL_EN				BIT(0)
#define MCUFWDL_RDY				BIT(1)
#define FWDL_ChkSum_rpt				BIT(2)
#define MACINI_RDY				BIT(3)
#define BBINI_RDY				BIT(4)
#define RFINI_RDY				BIT(5)
#define WINTINI_RDY				BIT(6)
#define MAC1_WINTINI_RDY			BIT(11)
#define CPRST					BIT(23)

/*  REG_SYS_CFG */
#define XCLK_VLD				BIT(0)
#define ACLK_VLD				BIT(1)
#define UCLK_VLD				BIT(2)
#define PCLK_VLD				BIT(3)
#define PCIRSTB					BIT(4)
#define V15_VLD					BIT(5)
#define TRP_B15V_EN				BIT(7)
#define SIC_IDLE				BIT(8)
#define BD_MAC2					BIT(9)
#define BD_MAC1					BIT(10)
#define IC_MACPHY_MODE				BIT(11)
#define PAD_HWPD_IDN				BIT(22)
#define TRP_VAUX_EN				BIT(23)
#define TRP_BT_EN				BIT(24)
#define BD_PKG_SEL				BIT(25)
#define BD_HCI_SEL				BIT(26)
#define TYPE_ID					BIT(27)

/* LLT_INIT */
#define _LLT_NO_ACTIVE				0x0
#define _LLT_WRITE_ACCESS			0x1
#define _LLT_READ_ACCESS			0x2

#define _LLT_INIT_DATA(x)			((x) & 0xFF)
#define _LLT_INIT_ADDR(x)			(((x) & 0xFF) << 8)
#define _LLT_OP(x)				(((x) & 0x3) << 30)
#define _LLT_OP_VALUE(x)			(((x) >> 30) & 0x3)


/* ----------------------------------------------------- */
/*	0x0400h ~ 0x047Fh	Protocol Configuration	 */
/* ----------------------------------------------------- */
#define	RETRY_LIMIT_SHORT_SHIFT			8
#define	RETRY_LIMIT_LONG_SHIFT			0


/* ----------------------------------------------------- */
/*	0x0500h ~ 0x05FFh	EDCA Configuration */
/* ----------------------------------------------------- */
/* EDCA setting */
#define AC_PARAM_TXOP_LIMIT_OFFSET		16
#define AC_PARAM_ECW_MAX_OFFSET			12
#define AC_PARAM_ECW_MIN_OFFSET			8
#define AC_PARAM_AIFS_OFFSET			0

/* ACMHWCTRL */
#define	ACMHW_HWEN				BIT(0)
#define	ACMHW_BEQEN				BIT(1)
#define	ACMHW_VIQEN				BIT(2)
#define	ACMHW_VOQEN				BIT(3)

/* ----------------------------------------------------- */
/*	0x0600h ~ 0x07FFh	WMAC Configuration */
/* ----------------------------------------------------- */

/* TCR */
#define TSFRST					BIT(0)
#define DIS_GCLK				BIT(1)
#define PAD_SEL					BIT(2)
#define PWR_ST					BIT(6)
#define PWRBIT_OW_EN				BIT(7)
#define ACRC					BIT(8)
#define CFENDFORM				BIT(9)
#define ICV					BIT(10)

/* SECCFG */
#define	SCR_TXUSEDK				BIT(0)
#define	SCR_RXUSEDK				BIT(1)
#define	SCR_TXENCENABLE				BIT(2)
#define	SCR_RXENCENABLE				BIT(3)
#define	SCR_SKBYA2				BIT(4)
#define	SCR_NOSKMC				BIT(5)
#define SCR_TXBCUSEDK				BIT(6)
#define SCR_RXBCUSEDK				BIT(7)

/* General definitions */
#define MAC_ADDR_LEN				6
#define LAST_ENTRY_OF_TX_PKT_BUFFER		255
#define LAST_ENTRY_OF_TX_PKT_BUFFER_DUAL_MAC	127

#define POLLING_LLT_THRESHOLD			20
#define POLLING_READY_TIMEOUT_COUNT		1000

/* Min Spacing related settings. */
#define	MAX_MSS_DENSITY_2T			0x13
#define	MAX_MSS_DENSITY_1T			0x0A


/* BB-PHY register PMAC 0x100 PHY 0x800 - 0xEFF */
/* 1. PMAC duplicate register due to connection: */
/*    RF_Mode, TRxRN, NumOf L-STF */
/* 2. 0x800/0x900/0xA00/0xC00/0xD00/0xE00 */
/* 3. RF register 0x00-2E */
/* 4. Bit Mask for BB/RF register */
/* 5. Other defintion for BB/RF R/W */

/* 3. Page8(0x800) */
#define	RFPGA0_RFMOD				0x800

#define	RFPGA0_TXINFO				0x804
#define	RFPGA0_PSDFUNCTION			0x808

#define	RFPGA0_TXGAINSTAGE			0x80c

#define	RFPGA0_RFTIMING1			0x810
#define	RFPGA0_RFTIMING2			0x814

#define	RFPGA0_XA_HSSIPARAMETER1		0x820
#define	RFPGA0_XA_HSSIPARAMETER2		0x824
#define	RFPGA0_XB_HSSIPARAMETER1		0x828
#define	RFPGA0_XB_HSSIPARAMETER2		0x82c

#define	RFPGA0_XA_LSSIPARAMETER			0x840
#define	RFPGA0_XB_LSSIPARAMETER			0x844

#define	RFPGA0_RFWAkEUPPARAMETER		0x850
#define	RFPGA0_RFSLEEPUPPARAMETER		0x854

#define	RFPGA0_XAB_SWITCHCONTROL		0x858
#define	RFPGA0_XCD_SWITCHCONTROL		0x85c

#define	RFPGA0_XA_RFINTERFACEOE			0x860
#define	RFPGA0_XB_RFINTERFACEOE			0x864

#define	RFPGA0_XAB_RFINTERFACESW		0x870
#define	RFPGA0_XCD_RFINTERFACESW		0x874

#define	RFPGA0_XAB_RFPARAMETER			0x878
#define	RFPGA0_XCD_RFPARAMETER			0x87c

#define	RFPGA0_ANALOGPARAMETER1			0x880
#define	RFPGA0_ANALOGPARAMETER2			0x884
#define	RFPGA0_ANALOGPARAMETER3			0x888
#define	RFPGA0_ADDALLOCKEN			0x888
#define	RFPGA0_ANALOGPARAMETER4			0x88c

#define	RFPGA0_XA_LSSIREADBACK			0x8a0
#define	RFPGA0_XB_LSSIREADBACK			0x8a4
#define	RFPGA0_XC_LSSIREADBACK			0x8a8
#define	RFPGA0_XD_LSSIREADBACK			0x8ac

#define	RFPGA0_PSDREPORT			0x8b4
#define	TRANSCEIVERA_HSPI_READBACK		0x8b8
#define	TRANSCEIVERB_HSPI_READBACK		0x8bc
#define	RFPGA0_XAB_RFINTERFACERB		0x8e0
#define	RFPGA0_XCD_RFINTERFACERB		0x8e4

/* 4. Page9(0x900) */
#define	RFPGA1_RFMOD				0x900

#define	RFPGA1_TXBLOCK				0x904
#define	RFPGA1_DEBUGSELECT			0x908
#define	RFPGA1_TXINFO				0x90c

/* 5. PageA(0xA00)  */
#define	RCCK0_SYSTEM				0xa00

#define	RCCK0_AFESSTTING			0xa04
#define	RCCK0_CCA				0xa08

#define	RCCK0_RXAGC1				0xa0c
#define	RCCK0_RXAGC2				0xa10

#define	RCCK0_RXHP				0xa14

#define	RCCK0_DSPPARAMETER1			0xa18
#define	RCCK0_DSPPARAMETER2			0xa1c

#define	RCCK0_TXFILTER1				0xa20
#define	RCCK0_TXFILTER2				0xa24
#define	RCCK0_DEBUGPORT				0xa28
#define	RCCK0_FALSEALARMREPORT			0xa2c
#define	RCCK0_TRSSIREPORT			0xa50
#define	RCCK0_RXREPORT				0xa54
#define	RCCK0_FACOUNTERLOWER			0xa5c
#define	RCCK0_FACOUNTERUPPER			0xa58

/* 6. PageC(0xC00) */
#define	ROFDM0_LSTF				0xc00

#define	ROFDM0_TRXPATHENABLE			0xc04
#define	ROFDM0_TRMUXPAR				0xc08
#define	ROFDM0_TRSWISOLATION			0xc0c

#define	ROFDM0_XARXAFE				0xc10
#define	ROFDM0_XARXIQIMBALANCE			0xc14
#define	ROFDM0_XBRXAFE				0xc18
#define	ROFDM0_XBRXIQIMBALANCE			0xc1c
#define	ROFDM0_XCRXAFE				0xc20
#define	ROFDM0_XCRXIQIMBALANCE			0xc24
#define	ROFDM0_XDRXAFE				0xc28
#define	ROFDM0_XDRXIQIMBALANCE			0xc2c

#define	ROFDM0_RXDETECTOR1			0xc30
#define	ROFDM0_RXDETECTOR2			0xc34
#define	ROFDM0_RXDETECTOR3			0xc38
#define	ROFDM0_RXDETECTOR4			0xc3c

#define	ROFDM0_RXDSP				0xc40
#define	ROFDM0_CFOANDDAGC			0xc44
#define	ROFDM0_CCADROPTHRESHOLD			0xc48
#define	ROFDM0_ECCATHRESHOLD			0xc4c

#define	ROFDM0_XAAGCCORE1			0xc50
#define	ROFDM0_XAAGCCORE2			0xc54
#define	ROFDM0_XBAGCCORE1			0xc58
#define	ROFDM0_XBAGCCORE2			0xc5c
#define	ROFDM0_XCAGCCORE1			0xc60
#define	ROFDM0_XCAGCCORE2			0xc64
#define	ROFDM0_XDAGCCORE1			0xc68
#define	ROFDM0_XDAGCCORE2			0xc6c

#define	ROFDM0_AGCPARAMETER1			0xc70
#define	ROFDM0_AGCPARAMETER2			0xc74
#define	ROFDM0_AGCRSSITABLE			0xc78
#define	ROFDM0_HTSTFAGC				0xc7c

#define	ROFDM0_XATxIQIMBALANCE			0xc80
#define	ROFDM0_XATxAFE				0xc84
#define	ROFDM0_XBTxIQIMBALANCE			0xc88
#define	ROFDM0_XBTxAFE				0xc8c
#define	ROFDM0_XCTxIQIMBALANCE			0xc90
#define	ROFDM0_XCTxAFE				0xc94
#define	ROFDM0_XDTxIQIMBALANCE			0xc98
#define	ROFDM0_XDTxAFE				0xc9c

#define	ROFDM0_RXHPPARAMETER			0xce0
#define	ROFDM0_TXPSEUDONOISEWGT			0xce4
#define	ROFDM0_FRAMESYNC			0xcf0
#define	ROFDM0_DFSREPORT			0xcf4
#define	ROFDM0_TXCOEFF1				0xca4
#define	ROFDM0_TXCOEFF2				0xca8
#define	ROFDM0_TXCOEFF3				0xcac
#define	ROFDM0_TXCOEFF4				0xcb0
#define	ROFDM0_TXCOEFF5				0xcb4
#define	ROFDM0_TXCOEFF6				0xcb8

/* 7. PageD(0xD00) */
#define	ROFDM1_LSTF				0xd00
#define	ROFDM1_TRXPATHENABLE			0xd04

#define	ROFDM1_CFO				0xd08
#define	ROFDM1_CSI1				0xd10
#define	ROFDM1_SBD				0xd14
#define	ROFDM1_CSI2				0xd18
#define	ROFDM1_CFOTRACKING			0xd2c
#define	ROFDM1_TRXMESAURE1			0xd34
#define	ROFDM1_INTFDET				0xd3c
#define	ROFDM1_PSEUDONOISESTATEAB		0xd50
#define	ROFDM1_PSEUDONOISESTATECD		0xd54
#define	ROFDM1_RXPSEUDONOISEWGT			0xd58

#define	ROFDM_PHYCOUNTER1			0xda0
#define	ROFDM_PHYCOUNTER2			0xda4
#define	ROFDM_PHYCOUNTER3			0xda8

#define	ROFDM_SHORTCFOAB			0xdac
#define	ROFDM_SHORTCFOCD			0xdb0
#define	ROFDM_LONGCFOAB				0xdb4
#define	ROFDM_LONGCFOCD				0xdb8
#define	ROFDM_TAILCFOAB				0xdbc
#define	ROFDM_TAILCFOCD				0xdc0
#define	ROFDM_PWMEASURE1			0xdc4
#define	ROFDM_PWMEASURE2			0xdc8
#define	ROFDM_BWREPORT				0xdcc
#define	ROFDM_AGCREPORT				0xdd0
#define	ROFDM_RXSNR				0xdd4
#define	ROFDM_RXEVMCSI				0xdd8
#define	ROFDM_SIGReport				0xddc

/* 8. PageE(0xE00) */
#define	RTXAGC_A_RATE18_06			0xe00
#define	RTXAGC_A_RATE54_24			0xe04
#define	RTXAGC_A_CCK1_MCS32			0xe08
#define	RTXAGC_A_MCS03_MCS00			0xe10
#define	RTXAGC_A_MCS07_MCS04			0xe14
#define	RTXAGC_A_MCS11_MCS08			0xe18
#define	RTXAGC_A_MCS15_MCS12			0xe1c

#define	RTXAGC_B_RATE18_06			0x830
#define	RTXAGC_B_RATE54_24			0x834
#define	RTXAGC_B_CCK1_55_MCS32			0x838
#define	RTXAGC_B_MCS03_MCS00			0x83c
#define	RTXAGC_B_MCS07_MCS04			0x848
#define	RTXAGC_B_MCS11_MCS08			0x84c
#define	RTXAGC_B_MCS15_MCS12			0x868
#define	RTXAGC_B_CCK11_A_CCK2_11		0x86c

/* RL6052 Register definition */
#define	RF_AC					0x00

#define	RF_IQADJ_G1				0x01
#define	RF_IQADJ_G2				0x02
#define	RF_POW_TRSW				0x05

#define	RF_GAIN_RX				0x06
#define	RF_GAIN_TX				0x07

#define	RF_TXM_IDAC				0x08
#define	RF_BS_IQGEN				0x0F

#define	RF_MODE1				0x10
#define	RF_MODE2				0x11

#define	RF_RX_AGC_HP				0x12
#define	RF_TX_AGC				0x13
#define	RF_BIAS					0x14
#define	RF_IPA					0x15
#define	RF_POW_ABILITY				0x17
#define	RF_MODE_AG				0x18
#define	rRfChannel				0x18
#define	RF_CHNLBW				0x18
#define	RF_TOP					0x19

#define	RF_RX_G1				0x1A
#define	RF_RX_G2				0x1B

#define	RF_RX_BB2				0x1C
#define	RF_RX_BB1				0x1D

#define	RF_RCK1					0x1E
#define	RF_RCK2					0x1F

#define	RF_TX_G1				0x20
#define	RF_TX_G2				0x21
#define	RF_TX_G3				0x22

#define	RF_TX_BB1				0x23

#define	RF_T_METER				0x42

#define	RF_SYN_G1				0x25
#define	RF_SYN_G2				0x26
#define	RF_SYN_G3				0x27
#define	RF_SYN_G4				0x28
#define	RF_SYN_G5				0x29
#define	RF_SYN_G6				0x2A
#define	RF_SYN_G7				0x2B
#define	RF_SYN_G8				0x2C

#define	RF_RCK_OS				0x30

#define	RF_TXPA_G1				0x31
#define	RF_TXPA_G2				0x32
#define	RF_TXPA_G3				0x33

/* Bit Mask */

/* 2. Page8(0x800) */
#define	BRFMOD					0x1
#define	BCCKTXSC				0x30
#define	BCCKEN					0x1000000
#define	BOFDMEN					0x2000000

#define	B3WIREDATALENGTH			0x800
#define	B3WIREADDRESSLENGTH			0x400

#define	BRFSI_RFENV				0x10

#define	BLSSIREADADDRESS			0x7f800000
#define	BLSSIREADEDGE				0x80000000
#define	BLSSIREADBACKDATA			0xfffff
/* 4. PageA(0xA00) */
#define BCCKSIDEBAND				0x10

/* Other Definition */
#define	BBYTE0					0x1
#define	BBYTE1					0x2
#define	BBYTE2					0x4
#define	BBYTE3					0x8
#define	BWORD0					0x3
#define	BWORD1					0xc
#define	BDWORD					0xf

#define	BMASKBYTE0				0xff
#define	BMASKBYTE1				0xff00
#define	BMASKBYTE2				0xff0000
#define	BMASKBYTE3				0xff000000
#define	BMASKHWORD				0xffff0000
#define	BMASKLWORD				0x0000ffff
#define	BMASKDWORD				0xffffffff
#define	BMASK12BITS				0xfff
#define	BMASKH4BITS				0xf0000000
#define BMASKOFDM_D				0xffc00000
#define	BMASKCCK				0x3f3f3f3f

#define BRFREGOFFSETMASK			0xfffff

#endif
