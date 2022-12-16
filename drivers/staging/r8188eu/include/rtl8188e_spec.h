/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#ifndef __RTL8188E_SPEC_H__
#define __RTL8188E_SPEC_H__

/*        8192C Register offset definition */

#define		HAL_PS_TIMER_INT_DELAY	50	/*   50 microseconds */
#define		HAL_92C_NAV_UPPER_UNIT	128	/*  micro-second */

/*  8188E PKT_BUFF_ACCESS_CTRL value */
#define TXPKT_BUF_SELECT		0x69
#define RXPKT_BUF_SELECT		0xA5
#define DISABLE_TRXPKT_BUF_ACCESS	0x0

/* 	0x0000h ~ 0x00FFh	System Configuration */
#define REG_SYS_ISO_CTRL		0x0000
#define REG_SYS_FUNC_EN			0x0002
#define REG_APS_FSMCO			0x0004
#define REG_SYS_CLKR			0x0008
#define REG_9346CR			0x000A
#define REG_EE_VPD			0x000C
#define REG_AFE_MISC			0x0010
#define REG_SPS0_CTRL			0x0011
#define REG_SPS_OCP_CFG			0x0018
#define REG_RSV_CTRL			0x001C
#define REG_RF_CTRL			0x001F
#define REG_LDOA15_CTRL			0x0020
#define REG_LDOV12D_CTRL		0x0021
#define REG_LDOHCI12_CTRL		0x0022
#define REG_LPLDO_CTRL			0x0023
#define REG_AFE_XTAL_CTRL		0x0024
#define REG_AFE_PLL_CTRL		0x0028
#define REG_APE_PLL_CTRL_EXT		0x002c
#define REG_EFUSE_CTRL			0x0030
#define REG_EFUSE_TEST			0x0034
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
#define REG_HSIMR			0x0058
#define REG_HSISR			0x005c
#define REG_GPIO_PIN_CTRL_2		0x0060 /*  RTL8723 WIFI/BT/GPS
				 * Multi-Function GPIO Pin Control. */
#define REG_GPIO_IO_SEL_2		0x0062 /*  RTL8723 WIFI/BT/GPS
				 * Multi-Function GPIO Select. */
#define REG_BB_PAD_CTRL			0x0064
#define REG_MULTI_FUNC_CTRL		0x0068 /*  RTL8723 WIFI/BT/GPS
				 * Multi-Function control source. */
#define REG_GPIO_OUTPUT			0x006c
#define REG_AFE_XTAL_CTRL_EXT		0x0078 /* RTL8188E */
#define REG_XCK_OUT_CTRL		0x007c /* RTL8188E */
#define REG_MCUFWDL			0x0080
#define REG_WOL_EVENT			0x0081 /* RTL8188E */
#define REG_MCUTSTCFG			0x0084
#define REG_HMEBOX_E0			0x0088
#define REG_HMEBOX_E1			0x008A
#define REG_HMEBOX_E2			0x008C
#define REG_HMEBOX_E3			0x008E
#define REG_HMEBOX_EXT_0		0x01F0
#define REG_HMEBOX_EXT_1		0x01F4
#define REG_HMEBOX_EXT_2		0x01F8
#define REG_HMEBOX_EXT_3		0x01FC
#define REG_HIMR_88E			0x00B0
#define REG_HISR_88E			0x00B4
#define REG_HIMRE_88E			0x00B8
#define REG_HISRE_88E			0x00BC
#define REG_EFUSE_ACCESS		0x00CF	/*  Efuse access protection
						 * for RTL8723 */
#define REG_BIST_SCAN			0x00D0
#define REG_BIST_RPT			0x00D4
#define REG_BIST_ROM_RPT		0x00D8
#define REG_USB_SIE_INTF		0x00E0
#define REG_PCIE_MIO_INTF		0x00E4
#define REG_PCIE_MIO_INTD		0x00E8
#define REG_HPON_FSM			0x00EC
#define REG_SYS_CFG			0x00F0
#define REG_GPIO_OUTSTS			0x00F4	/*  For RTL8723 only. */
#define REG_TYPE_ID			0x00FC

#define REG_MAC_PHY_CTRL_NORMAL		0x00f8

/* 	0x0100h ~ 0x01FFh	MACTOP General Configuration */
#define REG_CR				0x0100
#define REG_PBP				0x0104
#define REG_PKT_BUFF_ACCESS_CTRL	0x0106
#define REG_TRXDMA_CTRL			0x010C
#define REG_TRXFF_BNDY			0x0114
#define REG_TRXFF_STATUS		0x0118
#define REG_RXFF_PTR			0x011C
/* define REG_HIMR			0x0120 */
/* define REG_HISR			0x0124 */
#define REG_HIMRE			0x0128
#define REG_HISRE			0x012C
#define REG_CPWM			0x012F
#define REG_FWIMR			0x0130
#define REG_FTIMR			0x0138
#define REG_FWISR			0x0134
#define REG_PKTBUF_DBG_CTRL		0x0140
#define REG_PKTBUF_DBG_ADDR		(REG_PKTBUF_DBG_CTRL)
#define REG_RXPKTBUF_DBG		(REG_PKTBUF_DBG_CTRL+2)
#define REG_TXPKTBUF_DBG		(REG_PKTBUF_DBG_CTRL+3)
#define REG_RXPKTBUF_CTRL		(REG_PKTBUF_DBG_CTRL+2)
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
#define REG_32K_CTRL			0x0194 /* RTL8188E */
#define REG_C2HEVT_MSG_NORMAL		0x01A0
#define REG_C2HEVT_CLEAR		0x01AF
#define REG_MCUTST_1			0x01c0
#define REG_FMETHR			0x01C8
#define REG_HMETFR			0x01CC
#define REG_HMEBOX_0			0x01D0
#define REG_HMEBOX_1			0x01D4
#define REG_HMEBOX_2			0x01D8
#define REG_HMEBOX_3			0x01DC

#define REG_LLT_INIT			0x01E0

/* 	0x0200h ~ 0x027Fh	TXDMA Configuration */
#define REG_RQPN			0x0200
#define REG_FIFOPAGE			0x0204
#define REG_TDECTRL			0x0208
#define REG_TXDMA_OFFSET_CHK		0x020C
#define REG_TXDMA_STATUS		0x0210
#define REG_RQPN_NPQ			0x0214

/* 	0x0280h ~ 0x02FFh	RXDMA Configuration */
#define		REG_RXDMA_AGG_PG_TH	0x0280
#define	REG_RXPKT_NUM			0x0284
#define		REG_RXDMA_STATUS	0x0288

/* 	0x0300h ~ 0x03FFh	PCIe */
#define	REG_PCIE_CTRL_REG		0x0300
#define	REG_INT_MIG			0x0304	/*  Interrupt Migration */
#define	REG_BCNQ_DESA			0x0308	/*  TX Beacon Descr Address */
#define	REG_HQ_DESA			0x0310	/*  TX High Queue Descr Addr */
#define	REG_MGQ_DESA			0x0318	/*  TX Manage Queue Descr Addr*/
#define	REG_VOQ_DESA			0x0320	/*  TX VO Queue Descr Addr */
#define	REG_VIQ_DESA			0x0328	/*  TX VI Queue Descr Addr */
#define	REG_BEQ_DESA			0x0330	/*  TX BE Queue Descr Addr */
#define	REG_BKQ_DESA			0x0338	/*  TX BK Queue Descr Addr */
#define	REG_RX_DESA			0x0340	/*  RX Queue Descr Addr */
#define	REG_MDIO			0x0354	/*  MDIO for Access PCIE PHY */
#define	REG_DBG_SEL			0x0360	/*  Debug Selection Register */
#define	REG_PCIE_HRPWM			0x0361	/* PCIe RPWM */
#define	REG_PCIE_HCPWM			0x0363	/* PCIe CPWM */
#define	REG_WATCH_DOG			0x0368

/*  RTL8723 series ------------------------------ */
#define	REG_PCIE_HISR			0x03A0

/*  spec version 11 */
/* 	0x0400h ~ 0x047Fh	Protocol Configuration */
#define REG_VOQ_INFORMATION		0x0400
#define REG_VIQ_INFORMATION		0x0404
#define REG_BEQ_INFORMATION		0x0408
#define REG_BKQ_INFORMATION		0x040C
#define REG_MGQ_INFORMATION		0x0410
#define REG_HGQ_INFORMATION		0x0414
#define REG_BCNQ_INFORMATION		0x0418
#define REG_TXPKT_EMPTY			0x041A

#define REG_CPU_MGQ_INFORMATION		0x041C
#define REG_FWHW_TXQ_CTRL		0x0420
#define REG_HWSEQ_CTRL			0x0423
#define REG_TXPKTBUF_BCNQ_BDNY		0x0424
#define REG_TXPKTBUF_MGQ_BDNY		0x0425
#define REG_LIFETIME_EN			0x0426
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
/* define REG_INIDATA_RATE_SEL		0x0484 */
#define REG_POWER_STATUS		0x04A4
#define REG_POWER_STAGE1		0x04B4
#define REG_POWER_STAGE2		0x04B8
#define REG_PKT_VO_VI_LIFE_TIME		0x04C0
#define REG_PKT_BE_BK_LIFE_TIME		0x04C2
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
#define REG_TX_RPT_CTRL			0x04EC
#define REG_TX_RPT_TIME			0x04F0	/*  2 byte */
#define REG_DUMMY			0x04FC

/* 	0x0500h ~ 0x05FFh	EDCA Configuration */
#define REG_EDCA_VO_PARAM		0x0500
#define REG_EDCA_VI_PARAM		0x0504
#define REG_EDCA_BE_PARAM		0x0508
#define REG_EDCA_BK_PARAM		0x050C
#define REG_BCNTCFG			0x0510
#define REG_PIFS			0x0512
#define REG_RDG_PIFS			0x0513
#define REG_SIFS_CTX			0x0514
#define REG_SIFS_TRX			0x0516
#define REG_TSFTR_SYN_OFFSET		0x0518
#define REG_AGGR_BREAK_TIME		0x051A
#define REG_SLOT			0x051B
#define REG_TX_PTCL_CTRL		0x0520
#define REG_TXPAUSE			0x0522
#define REG_DIS_TXREQ_CLR		0x0523
#define REG_RD_CTRL			0x0524
/*  Format for offset 540h-542h: */
/* 	[3:0]:   TBTT prohibit setup in unit of 32us. The time for HW getting
 *		 beacon content before TBTT. */
/* 	[7:4]:   Reserved. */
/* 	[19:8]:  TBTT prohibit hold in unit of 32us. The time for HW holding
 *		 to send the beacon packet. */
/* 	[23:20]: Reserved */
/*  Description: */
/* 	              | */
/*      |<--Setup--|--Hold------------>| */
/* 	--------------|---------------------- */
/*                 | */
/*                TBTT */
/*  Note: We cannot update beacon content to HW or send any AC packets during
 *	  the time between Setup and Hold. */
#define REG_TBTT_PROHIBIT		0x0540
#define REG_RD_NAV_NXT			0x0544
#define REG_NAV_PROT_LEN		0x0546
#define REG_BCN_CTRL			0x0550
#define REG_BCN_CTRL_1			0x0551
#define REG_MBID_NUM			0x0552
#define REG_DUAL_TSF_RST		0x0553
#define REG_BCN_INTERVAL		0x0554
#define REG_DRVERLYINT			0x0558
#define REG_BCNDMATIM			0x0559
#define REG_ATIMWND			0x055A
#define REG_BCN_MAX_ERR			0x055D
#define REG_RXTSF_OFFSET_CCK		0x055E
#define REG_RXTSF_OFFSET_OFDM		0x055F
#define REG_TSFTR			0x0560
#define REG_TSFTR1			0x0568
#define REG_ATIMWND_1			0x0570
#define REG_PSTIMER			0x0580
#define REG_TIMER0			0x0584
#define REG_TIMER1			0x0588
#define REG_ACMHWCTRL			0x05C0

/* define REG_FW_TSF_SYNC_CNT		0x04A0 */
#define REG_FW_RESET_TSF_CNT_1		0x05FC
#define REG_FW_RESET_TSF_CNT_0		0x05FD
#define REG_FW_BCN_DIS_CNT		0x05FE

/* 	0x0600h ~ 0x07FFh	WMAC Configuration */
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

/*  20100719 Joseph: Hardware register definition change. (HW datasheet v54) */
/*  [15:8]SIFS_R2T_OFDM, [7:0]SIFS_R2T_CCK */
#define REG_R2T_SIFS			0x063C
/*  [15:8]SIFS_T2T_OFDM, [7:0]SIFS_T2T_CCK */
#define REG_T2T_SIFS			0x063E
#define REG_ACKTO			0x0640
#define REG_CTS2TO			0x0641
#define REG_EIFS			0x0642

/* RXERR_RPT */
#define RXERR_TYPE_OFDM_PPDU		0
#define RXERR_TYPE_OFDM_false_ALARM	1
#define RXERR_TYPE_OFDM_MPDU_OK		2
#define RXERR_TYPE_OFDM_MPDU_FAIL	3
#define RXERR_TYPE_CCK_PPDU		4
#define RXERR_TYPE_CCK_false_ALARM	5
#define RXERR_TYPE_CCK_MPDU_OK		6
#define RXERR_TYPE_CCK_MPDU_FAIL	7
#define RXERR_TYPE_HT_PPDU		8
#define RXERR_TYPE_HT_false_ALARM	9
#define RXERR_TYPE_HT_MPDU_TOTAL	10
#define RXERR_TYPE_HT_MPDU_OK		11
#define RXERR_TYPE_HT_MPDU_FAIL		12
#define RXERR_TYPE_RX_FULL_DROP		15

#define RXERR_COUNTER_MASK		0xFFFFF
#define RXERR_RPT_RST			BIT(27)
#define _RXERR_RPT_SEL(type)		((type) << 28)

/*  Note: */
/* 	The NAV upper value is very important to WiFi 11n 5.2.3 NAV test.
 *	The default value is always too small, but the WiFi TestPlan test
 *	by 25,000 microseconds of NAV through sending CTS in the air.
 *	We must update this value greater than 25,000 microseconds to pass
 *	the item. The offset of NAV_UPPER in 8192C Spec is incorrect, and
 *	the offset should be 0x0652. */
#define REG_NAV_UPPER			0x0652	/*  unit of 128 */

/* WMA, BA, CCX */
/* define REG_NAV_CTRL			0x0650 */
#define REG_BACAMCMD			0x0654
#define REG_BACAMCONTENT		0x0658
#define REG_LBDLY			0x0660
#define REG_FWDLY			0x0661
#define REG_RXERR_RPT			0x0664
#define REG_WMAC_TRXPTCL_CTL		0x0668

/*  Security */
#define REG_CAMCMD			0x0670
#define REG_CAMWRITE			0x0674
#define REG_CAMREAD			0x0678
#define REG_CAMDBG			0x067C
#define REG_SECCFG			0x0680

/*  Power */
#define REG_WOW_CTRL			0x0690
#define REG_PS_RX_INFO			0x0692
#define REG_UAPSD_TID			0x0693
#define REG_WKFMCAM_CMD			0x0698
#define REG_WKFMCAM_NUM_88E		0x698
#define REG_RXFLTMAP0			0x06A0
#define REG_RXFLTMAP1			0x06A2
#define REG_RXFLTMAP2			0x06A4
#define REG_BCN_PSR_RPT			0x06A8
#define REG_BT_COEX_TABLE		0x06C0

/*  Hardware Port 2 */
#define REG_MACID1			0x0700
#define REG_BSSID1			0x0708

/* 	0xFE00h ~ 0xFE55h	USB Configuration */
#define REG_USB_INFO			0xFE17
#define REG_USB_SPECIAL_OPTION		0xFE55
#define REG_USB_DMA_AGG_TO		0xFE5B
#define REG_USB_AGG_TO			0xFE5C
#define REG_USB_AGG_TH			0xFE5D

/*  For normal chip */
#define REG_NORMAL_SIE_VID		0xFE60		/*  0xFE60~0xFE61 */
#define REG_NORMAL_SIE_PID		0xFE62		/*  0xFE62~0xFE63 */
#define REG_NORMAL_SIE_OPTIONAL		0xFE64
#define REG_NORMAL_SIE_EP		0xFE65		/*  0xFE65~0xFE67 */
#define REG_NORMAL_SIE_PHY		0xFE68		/*  0xFE68~0xFE6B */
#define REG_NORMAL_SIE_OPTIONAL2	0xFE6C
#define REG_NORMAL_SIE_GPS_EP		0xFE6D	/*  0xFE6D, for RTL8723 only. */
#define REG_NORMAL_SIE_MAC_ADDR		0xFE70		/*  0xFE70~0xFE75 */
#define REG_NORMAL_SIE_STRING		0xFE80		/*  0xFE80~0xFEDF */

/*  TODO: use these definition when using REG_xxx naming rule. */
/*  NOTE: DO NOT Remove these definition. Use later. */

#define	EFUSE_CTRL			REG_EFUSE_CTRL	/*  E-Fuse Control. */
#define	EFUSE_TEST			REG_EFUSE_TEST	/*  E-Fuse Test. */
#define	MSR				(REG_CR + 2)	/*  Media Status reg */
#define	ISR				REG_HISR_88E
/*  Timing Sync Function Timer Register. */
#define	TSFR				REG_TSFTR

#define		PBP			REG_PBP

/*  Redifine MACID register, to compatible prior ICs. */
/*  MAC ID Register, Offset 0x0050-0x0053 */
#define	IDR0				REG_MACID
/*  MAC ID Register, Offset 0x0054-0x0055 */
#define	IDR4				(REG_MACID + 4)

/*  9. Security Control Registers	(Offset: ) */
/* IN 8190 Data Sheet is called CAMcmd */
#define	RWCAM				REG_CAMCMD
/*  Software write CAM input content */
#define	WCAMI				REG_CAMWRITE
/*  Software read/write CAM config */
#define	RCAMO				REG_CAMREAD
#define	CAMDBG				REG_CAMDBG
/* Security Configuration Register */
#define	SECR				REG_SECCFG

/*  Unused register */
#define	UnusedRegister			0x1BF
#define	DCAM				UnusedRegister
#define	PSR				UnusedRegister
#define	BBAddr				UnusedRegister
#define	PhyDataR			UnusedRegister

/*  Min Spacing related settings. */
#define	MAX_MSS_DENSITY_2T		0x13
#define	MAX_MSS_DENSITY_1T		0x0A

/*        8192C GPIO MUX Configuration Register (offset 0x40, 4 byte) */
#define	GPIOSEL_GPIO			0
#define	GPIOSEL_ENBT			BIT(5)

/*        8192C GPIO PIN Control Register (offset 0x44, 4 byte) */
/*  GPIO pins input value */
#define	GPIO_IN				REG_GPIO_PIN_CTRL
/*  GPIO pins output value */
#define	GPIO_OUT			(REG_GPIO_PIN_CTRL+1)
/*  GPIO pins output enable when a bit is set to "1"; otherwise,
 *  input is configured. */
#define	GPIO_IO_SEL			(REG_GPIO_PIN_CTRL+2)
#define	GPIO_MOD			(REG_GPIO_PIN_CTRL+3)

/* 8723/8188E Host System Interrupt Mask Register (offset 0x58, 32 byte) */
#define	HSIMR_GPIO12_0_INT_EN		BIT(0)
#define	HSIMR_SPS_OCP_INT_EN		BIT(5)
#define	HSIMR_RON_INT_EN		BIT(6)
#define	HSIMR_PDN_INT_EN		BIT(7)
#define	HSIMR_GPIO9_INT_EN		BIT(25)

/* 8723/8188E Host System Interrupt Status Register (offset 0x5C, 32 byte) */
#define	HSISR_GPIO12_0_INT		BIT(0)
#define	HSISR_SPS_OCP_INT		BIT(5)
#define	HSISR_RON_INT_EN		BIT(6)
#define	HSISR_PDNINT			BIT(7)
#define	HSISR_GPIO9_INT			BIT(25)

/*   8192C (MSR) Media Status Register	(Offset 0x4C, 8 bits) */
/*
Network Type
00: No link
01: Link in ad hoc network
10: Link in infrastructure network
11: AP mode
Default: 00b.
*/
#define	MSR_NOLINK			0x00
#define	MSR_ADHOC			0x01
#define	MSR_INFRA			0x02
#define	MSR_AP				0x03

/*  88E Driver Initialization Offload REG_FDHM0(Offset 0x88, 8 bits) */
/* IOL config for REG_FDHM0(Reg0x88) */
#define CMD_INIT_LLT			BIT(0)
#define CMD_READ_EFUSE_MAP		BIT(1)
#define CMD_EFUSE_PATCH			BIT(2)
#define CMD_IOCONFIG			BIT(3)
#define CMD_INIT_LLT_ERR		BIT(4)
#define CMD_READ_EFUSE_MAP_ERR		BIT(5)
#define CMD_EFUSE_PATCH_ERR		BIT(6)
#define CMD_IOCONFIG_ERR		BIT(7)

/*  6. Adaptive Control Registers  (Offset: 0x0160 - 0x01CF) */
/*  8192C Response Rate Set Register	(offset 0x181, 24bits) */
#define	RRSR_1M				BIT(0)
#define	RRSR_2M				BIT(1)
#define	RRSR_5_5M			BIT(2)
#define	RRSR_11M			BIT(3)
#define	RRSR_6M				BIT(4)
#define	RRSR_9M				BIT(5)
#define	RRSR_12M			BIT(6)
#define	RRSR_18M			BIT(7)
#define	RRSR_24M			BIT(8)
#define	RRSR_36M			BIT(9)
#define	RRSR_48M			BIT(10)
#define	RRSR_54M			BIT(11)
#define	RRSR_MCS0			BIT(12)
#define	RRSR_MCS1			BIT(13)
#define	RRSR_MCS2			BIT(14)
#define	RRSR_MCS3			BIT(15)
#define	RRSR_MCS4			BIT(16)
#define	RRSR_MCS5			BIT(17)
#define	RRSR_MCS6			BIT(18)
#define	RRSR_MCS7			BIT(19)

/*  8192C Response Rate Set Register	(offset 0x1BF, 8bits) */
/*  WOL bit information */
#define	HAL92C_WOL_PTK_UPDATE_EVENT	BIT(0)
#define	HAL92C_WOL_GTK_UPDATE_EVENT	BIT(1)

/*        8192C BW_OPMODE bits		(Offset 0x203, 8bit) */
#define	BW_OPMODE_20MHZ			BIT(2)

/*        8192C CAM Config Setting (offset 0x250, 1 byte) */
#define	CAM_VALID			BIT(15)
#define	CAM_NOTVALID			0x0000
#define	CAM_USEDK			BIT(5)

#define	CAM_CONTENT_COUNT		8

#define	CAM_NONE			0x0
#define	CAM_WEP40			0x01
#define	CAM_TKIP			0x02
#define	CAM_AES				0x04
#define	CAM_WEP104			0x05
#define	CAM_SMS4			0x6

#define	TOTAL_CAM_ENTRY			32
#define	HALF_CAM_ENTRY			16

#define	CAM_CONFIG_USEDK		true
#define	CAM_CONFIG_NO_USEDK		false

#define	CAM_WRITE			BIT(16)
#define	CAM_READ			0x00000000
#define	CAM_POLLINIG			BIT(31)

#define	SCR_UseDK			0x01
#define	SCR_TxSecEnable			0x02
#define	SCR_RxSecEnable			0x04

/*  10. Power Save Control Registers	 (Offset: 0x0260 - 0x02DF) */
#define	WOW_PMEN			BIT(0) /*  Power management Enable. */
#define	WOW_WOMEN			BIT(1) /*  WoW function on or off. */
#define	WOW_MAGIC			BIT(2) /*  Magic packet */
#define	WOW_UWF				BIT(3) /*  Unicast Wakeup frame. */

/*  12. Host Interrupt Status Registers	 (Offset: 0x0300 - 0x030F) */
/*        8188 IMR/ISR bits */
#define	IMR_DISABLED_88E		0x0
/*  IMR DW0(0x0060-0063) Bit 0-31 */
#define	IMR_TXCCK_88E			BIT(30)	/*  TXRPT interrupt when CCX bit of the packet is set */
#define	IMR_PSTIMEOUT_88E		BIT(29)	/*  Power Save Time Out Interrupt */
#define	IMR_GTINT4_88E			BIT(28)	/*  When GTIMER4 expires, this bit is set to 1 */
#define	IMR_GTINT3_88E			BIT(27)	/*  When GTIMER3 expires, this bit is set to 1 */
#define	IMR_TBDER_88E			BIT(26)	/*  Transmit Beacon0 Error */
#define	IMR_TBDOK_88E			BIT(25)	/*  Transmit Beacon0 OK */
#define	IMR_TSF_BIT32_TOGGLE_88E	BIT(24)	/*  TSF Timer BIT32 toggle indication interrupt */
#define	IMR_BCNDMAINT0_88E		BIT(20)	/*  Beacon DMA Interrupt 0 */
#define	IMR_BCNDERR0_88E		BIT(16)	/*  Beacon Queue DMA Error 0 */
#define	IMR_HSISR_IND_ON_INT_88E	BIT(15)	/*  HSISR Indicator (HSIMR & HSISR is true, this bit is set to 1) */
#define	IMR_BCNDMAINT_E_88E		BIT(14)	/*  Beacon DMA Interrupt Extension for Win7 */
#define	IMR_ATIMEND_88E			BIT(12)	/*  CTWidnow End or ATIM Window End */
#define	IMR_HISR1_IND_INT_88E		BIT(11)	/*  HISR1 Indicator (HISR1 & HIMR1 is true, this bit is set to 1) */
#define	IMR_C2HCMD_88E			BIT(10)	/*  CPU to Host Command INT Status, Write 1 clear */
#define	IMR_CPWM2_88E			BIT(9)	/*  CPU power Mode exchange INT Status, Write 1 clear */
#define	IMR_CPWM_88E			BIT(8)	/*  CPU power Mode exchange INT Status, Write 1 clear */
#define	IMR_HIGHDOK_88E			BIT(7)	/*  High Queue DMA OK */
#define	IMR_MGNTDOK_88E			BIT(6)	/*  Management Queue DMA OK */
#define	IMR_BKDOK_88E			BIT(5)	/*  AC_BK DMA OK */
#define	IMR_BEDOK_88E			BIT(4)	/*  AC_BE DMA OK */
#define	IMR_VIDOK_88E			BIT(3)	/*  AC_VI DMA OK */
#define	IMR_VODOK_88E			BIT(2)	/*  AC_VO DMA OK */
#define	IMR_RDU_88E			BIT(1)	/*  Rx Descriptor Unavailable */
#define	IMR_ROK_88E			BIT(0)	/*  Receive DMA OK */

/*  IMR DW1(0x00B4-00B7) Bit 0-31 */
#define	IMR_BCNDMAINT7_88E		BIT(27)	/*  Beacon DMA Interrupt 7 */
#define	IMR_BCNDMAINT6_88E		BIT(26)	/*  Beacon DMA Interrupt 6 */
#define	IMR_BCNDMAINT5_88E		BIT(25)	/*  Beacon DMA Interrupt 5 */
#define	IMR_BCNDMAINT4_88E		BIT(24)	/*  Beacon DMA Interrupt 4 */
#define	IMR_BCNDMAINT3_88E		BIT(23)	/*  Beacon DMA Interrupt 3 */
#define	IMR_BCNDMAINT2_88E		BIT(22)	/*  Beacon DMA Interrupt 2 */
#define	IMR_BCNDMAINT1_88E		BIT(21)	/*  Beacon DMA Interrupt 1 */
#define	IMR_BCNDERR7_88E		BIT(20)	/*  Beacon DMA Error Int 7 */
#define	IMR_BCNDERR6_88E		BIT(19)	/*  Beacon DMA Error Int 6 */
#define	IMR_BCNDERR5_88E		BIT(18)	/*  Beacon DMA Error Int 5 */
#define	IMR_BCNDERR4_88E		BIT(17)	/*  Beacon DMA Error Int 4 */
#define	IMR_BCNDERR3_88E		BIT(16)	/*  Beacon DMA Error Int 3 */
#define	IMR_BCNDERR2_88E		BIT(15)	/*  Beacon DMA Error Int 2 */
#define	IMR_BCNDERR1_88E		BIT(14)	/*  Beacon DMA Error Int 1 */
#define	IMR_ATIMEND_E_88E		BIT(13)	/*  ATIM Window End Ext for Win7 */
#define	IMR_TXERR_88E			BIT(11)	/*  Tx Err Flag Int Status, write 1 clear. */
#define	IMR_RXERR_88E			BIT(10)	/*  Rx Err Flag INT Status, Write 1 clear */
#define	IMR_TXFOVW_88E			BIT(9)	/*  Transmit FIFO Overflow */
#define	IMR_RXFOVW_88E			BIT(8)	/*  Receive FIFO Overflow */

#define	HAL_NIC_UNPLUG_ISR		0xFFFFFFFF	/*  The value when the NIC is unplugged for PCI. */

/*  8192C EFUSE */
#define		HWSET_MAX_SIZE			256
#define		HWSET_MAX_SIZE_88E		512

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
/* 		 8192C (TXPAUSE) transmission pause (Offset 0x522, 8 bits) */
/*  Note: */
/* 	The bits of stopping AC(VO/VI/BE/BK) queue in datasheet
 *	RTL8192S/RTL8192C are wrong, */
/* 	the correct arragement is VO - Bit0, VI - Bit1, BE - Bit2,
 *	and BK - Bit3. */
/* 	8723 and 88E may be not correct either in the earlier version. */
#define		StopBecon			BIT(6)
#define		StopHigh			BIT(5)
#define		StopMgt				BIT(4)
#define		StopBK				BIT(3)
#define		StopBE				BIT(2)
#define		StopVI				BIT(1)
#define		StopVO				BIT(0)

/*        8192C (RCR) Receive Configuration Register(Offset 0x608, 32 bits) */
#define	RCR_APPFCS		BIT(31)	/* WMAC append FCS after payload */
#define	RCR_APP_MIC		BIT(30)
#define	RCR_APP_PHYSTS		BIT(28)
#define	RCR_APP_ICV		BIT(29)
#define	RCR_APP_PHYST_RXFF	BIT(28)
#define	RCR_APP_BA_SSN		BIT(27)	/* Accept BA SSN */
#define	RCR_ENMBID		BIT(24)	/* Enable Multiple BssId. */
#define	RCR_LSIGEN		BIT(23)
#define	RCR_MFBEN		BIT(22)
#define	RCR_HTC_LOC_CTRL	BIT(14)   /* MFC<--HTC=1 MFC-->HTC=0 */
#define	RCR_AMF			BIT(13)	/* Accept management type frame */
#define	RCR_ACF			BIT(12)	/* Accept control type frame */
#define	RCR_ADF			BIT(11)	/* Accept data type frame */
#define	RCR_AICV		BIT(9)	/* Accept ICV error packet */
#define	RCR_ACRC32		BIT(8)	/* Accept CRC32 error packet */
#define	RCR_CBSSID_BCN		BIT(7)	/* Accept BSSID match packet
					 * (Rx beacon, probe rsp) */
#define	RCR_CBSSID_DATA		BIT(6)	/* Accept BSSID match (Data)*/
#define	RCR_CBSSID		RCR_CBSSID_DATA	/* Accept BSSID match */
#define	RCR_APWRMGT		BIT(5)	/* Accept power management pkt*/
#define	RCR_ADD3		BIT(4)	/* Accept address 3 match pkt */
#define	RCR_AB			BIT(3)	/* Accept broadcast packet */
#define	RCR_AM			BIT(2)	/* Accept multicast packet */
#define	RCR_APM			BIT(1)	/* Accept physical match pkt */
#define	RCR_AAP			BIT(0)	/* Accept all unicast packet */
#define	RCR_MXDMA_OFFSET	8
#define	RCR_FIFO_OFFSET		13

/* 	0xFE00h ~ 0xFE55h	USB Configuration */
#define REG_USB_INFO			0xFE17
#define REG_USB_SPECIAL_OPTION		0xFE55
#define REG_USB_DMA_AGG_TO		0xFE5B
#define REG_USB_AGG_TO			0xFE5C
#define REG_USB_AGG_TH			0xFE5D

#define REG_USB_HRPWM			0xFE58
#define REG_USB_HCPWM			0xFE57
/*        8192C Register Bit and Content definition */
/* 	0x0000h ~ 0x00FFh	System Configuration */

/* 2 SYS_ISO_CTRL */
#define ISO_MD2PP			BIT(0)
#define ISO_UA2USB			BIT(1)
#define ISO_UD2CORE			BIT(2)
#define ISO_PA2PCIE			BIT(3)
#define ISO_PD2CORE			BIT(4)
#define ISO_IP2MAC			BIT(5)
#define ISO_DIOP			BIT(6)
#define ISO_DIOE			BIT(7)
#define ISO_EB2CORE			BIT(8)
#define ISO_DIOR			BIT(9)
#define PWC_EV12V			BIT(15)

/* 2 SYS_FUNC_EN */
#define FEN_BBRSTB			BIT(0)
#define FEN_BB_GLB_RSTn			BIT(1)
#define FEN_USBA			BIT(2)
#define FEN_UPLL			BIT(3)
#define FEN_USBD			BIT(4)
#define FEN_DIO_PCIE			BIT(5)
#define FEN_PCIEA			BIT(6)
#define FEN_PPLL			BIT(7)
#define FEN_PCIED			BIT(8)
#define FEN_DIOE			BIT(9)
#define FEN_CPUEN			BIT(10)
#define FEN_DCORE			BIT(11)
#define FEN_ELDR			BIT(12)
#define FEN_DIO_RF			BIT(13)
#define FEN_HWPDN			BIT(14)
#define FEN_MREGEN			BIT(15)

/* 2 APS_FSMCO */
#define PFM_LDALL			BIT(0)
#define PFM_ALDN			BIT(1)
#define PFM_LDKP			BIT(2)
#define PFM_WOWL			BIT(3)
#define EnPDN				BIT(4)
#define PDN_PL				BIT(5)
#define APFM_ONMAC			BIT(8)
#define APFM_OFF			BIT(9)
#define APFM_RSM			BIT(10)
#define AFSM_HSUS			BIT(11)
#define AFSM_PCIE			BIT(12)
#define APDM_MAC			BIT(13)
#define APDM_HOST			BIT(14)
#define APDM_HPDN			BIT(15)
#define RDY_MACON			BIT(16)
#define SUS_HOST			BIT(17)
#define ROP_ALD				BIT(20)
#define ROP_PWR				BIT(21)
#define ROP_SPS				BIT(22)
#define SOP_MRST			BIT(25)
#define SOP_FUSE			BIT(26)
#define SOP_ABG				BIT(27)
#define SOP_AMB				BIT(28)
#define SOP_RCK				BIT(29)
#define SOP_A8M				BIT(30)
#define XOP_BTCK			BIT(31)

/* 2 SYS_CLKR */
#define ANAD16V_EN			BIT(0)
#define ANA8M				BIT(1)
#define MACSLP				BIT(4)
#define LOADER_CLK_EN			BIT(5)

/* 2 9346CR */

#define		BOOT_FROM_EEPROM	BIT(4)
#define		EEPROM_EN		BIT(5)

/* 2 SPS0_CTRL */

/* 2 SPS_OCP_CFG */

/* 2 RF_CTRL */
#define RF_EN				BIT(0)
#define RF_RSTB				BIT(1)
#define RF_SDMRSTB			BIT(2)

/* 2 LDOV12D_CTRL */
#define LDV12_EN			BIT(0)
#define LDV12_SDBY			BIT(1)
#define LPLDO_HSM			BIT(2)
#define LPLDO_LSM_DIS			BIT(3)
#define _LDV12_VADJ(x)			(((x) & 0xF) << 4)

/* 2EFUSE_CTRL */
#define ALD_EN				BIT(18)
#define EF_PD				BIT(19)
#define EF_FLAG				BIT(31)

/* 2 EFUSE_TEST (For RTL8723 partially) */
#define EF_TRPT				BIT(7)
/*  00: Wifi Efuse, 01: BT Efuse0, 10: BT Efuse1, 11: BT Efuse2 */
#define EF_CELL_SEL			(BIT(8)|BIT(9))
#define LDOE25_EN			BIT(31)
#define EFUSE_SEL(x)			(((x) & 0x3) << 8)
#define EFUSE_SEL_MASK			0x300
#define EFUSE_WIFI_SEL_0		0x0
#define EFUSE_BT_SEL_0			0x1
#define EFUSE_BT_SEL_1			0x2
#define EFUSE_BT_SEL_2			0x3

#define EFUSE_ACCESS_ON			0x69	/*  For RTL8723 only. */
#define EFUSE_ACCESS_OFF		0x00	/*  For RTL8723 only. */

/* 2 8051FWDL */
/* 2 MCUFWDL */
#define MCUFWDL_EN			BIT(0)
#define MCUFWDL_RDY			BIT(1)
#define FWDL_CHKSUM_RPT			BIT(2)
#define MACINI_RDY			BIT(3)
#define BBINI_RDY			BIT(4)
#define RFINI_RDY			BIT(5)
#define WINTINI_RDY			BIT(6)
#define RAM_DL_SEL			BIT(7) /*  1:RAM, 0:ROM */
#define ROM_DLEN			BIT(19)
#define CPRST				BIT(23)

/* 2 REG_SYS_CFG */
#define XCLK_VLD			BIT(0)
#define ACLK_VLD			BIT(1)
#define UCLK_VLD			BIT(2)
#define PCLK_VLD			BIT(3)
#define PCIRSTB				BIT(4)
#define V15_VLD				BIT(5)
#define SW_OFFLOAD_EN			BIT(7)
#define SIC_IDLE			BIT(8)
#define BD_MAC2				BIT(9)
#define BD_MAC1				BIT(10)
#define IC_MACPHY_MODE			BIT(11)
#define CHIP_VER			(BIT(12)|BIT(13)|BIT(14)|BIT(15))
#define BT_FUNC				BIT(16)
#define VENDOR_ID			BIT(19)
#define PAD_HWPD_IDN			BIT(22)
#define TRP_VAUX_EN			BIT(23)	/*  RTL ID */
#define TRP_BT_EN			BIT(24)
#define BD_PKG_SEL			BIT(25)
#define BD_HCI_SEL			BIT(26)
#define TYPE_ID				BIT(27)

#define CHIP_VER_RTL_MASK		0xF000	/* Bit 12 ~ 15 */
#define CHIP_VER_RTL_SHIFT		12

/* 2REG_GPIO_OUTSTS (For RTL8723 only) */
#define	EFS_HCI_SEL			(BIT(0)|BIT(1))
#define	PAD_HCI_SEL			(BIT(2)|BIT(3))
#define	HCI_SEL				(BIT(4)|BIT(5))
#define	PKG_SEL_HCI			BIT(6)
#define	FEN_GPS				BIT(7)
#define	FEN_BT				BIT(8)
#define	FEN_WL				BIT(9)
#define	FEN_PCI				BIT(10)
#define	FEN_USB				BIT(11)
#define	BTRF_HWPDN_N			BIT(12)
#define	WLRF_HWPDN_N			BIT(13)
#define	PDN_BT_N			BIT(14)
#define	PDN_GPS_N			BIT(15)
#define	BT_CTL_HWPDN			BIT(16)
#define	GPS_CTL_HWPDN			BIT(17)
#define	PPHY_SUSB			BIT(20)
#define	UPHY_SUSB			BIT(21)
#define	PCI_SUSEN			BIT(22)
#define	USB_SUSEN			BIT(23)
#define	RF_RL_ID			(BIT(31)|BIT(30)|BIT(29)|BIT(28))

/* 2SYS_CFG */
#define RTL_ID				BIT(23)	/*  TestChip ID, 1:Test(RLE); 0:MP(RL) */

/* 	0x0100h ~ 0x01FFh	MACTOP General Configuration */

/* 2 Function Enable Registers */
/* 2 CR */

#define HCI_TXDMA_EN			BIT(0)
#define HCI_RXDMA_EN			BIT(1)
#define TXDMA_EN			BIT(2)
#define RXDMA_EN			BIT(3)
#define PROTOCOL_EN			BIT(4)
#define SCHEDULE_EN			BIT(5)
#define MACTXEN				BIT(6)
#define MACRXEN				BIT(7)
#define ENSWBCN				BIT(8)
#define ENSEC				BIT(9)
#define CALTMR_EN			BIT(10)	/*  32k CAL TMR enable */

/*  Network type */
#define _NETTYPE(x)			(((x) & 0x3) << 16)
#define MASK_NETTYPE			0x30000
#define NT_NO_LINK			0x0
#define NT_LINK_AD_HOC			0x1
#define NT_LINK_AP			0x2
#define NT_AS_AP			0x3

/* 2 PBP - Page Size Register */
#define GET_RX_PAGE_SIZE(value)		((value) & 0xF)
#define GET_TX_PAGE_SIZE(value)		(((value) & 0xF0) >> 4)
#define _PSRX_MASK			0xF
#define _PSTX_MASK			0xF0
#define _PSRX(x)			(x)
#define _PSTX(x)			((x) << 4)

#define PBP_128				0x1

/* 2 TX/RXDMA */
#define RXDMA_ARBBW_EN			BIT(0)
#define RXSHFT_EN			BIT(1)
#define RXDMA_AGG_EN			BIT(2)
#define QS_VO_QUEUE			BIT(8)
#define QS_VI_QUEUE			BIT(9)
#define QS_BE_QUEUE			BIT(10)
#define QS_BK_QUEUE			BIT(11)
#define QS_MANAGER_QUEUE		BIT(12)
#define QS_HIGH_QUEUE			BIT(13)

#define HQSEL_VOQ			BIT(0)
#define HQSEL_VIQ			BIT(1)
#define HQSEL_BEQ			BIT(2)
#define HQSEL_BKQ			BIT(3)
#define HQSEL_MGTQ			BIT(4)
#define HQSEL_HIQ			BIT(5)

/*  For normal driver, 0x10C */
#define _TXDMA_HIQ_MAP(x)		(((x) & 0x3) << 14)
#define _TXDMA_MGQ_MAP(x)		(((x) & 0x3) << 12)
#define _TXDMA_BKQ_MAP(x)		(((x) & 0x3) << 10)
#define _TXDMA_BEQ_MAP(x)		(((x) & 0x3) << 8)
#define _TXDMA_VIQ_MAP(x)		(((x) & 0x3) << 6)
#define _TXDMA_VOQ_MAP(x)		(((x) & 0x3) << 4)

#define QUEUE_LOW			1
#define QUEUE_NORMAL			2
#define QUEUE_HIGH			3

/* 2 TRXFF_BNDY */

/* 2 LLT_INIT */
#define _LLT_NO_ACTIVE			0x0
#define _LLT_WRITE_ACCESS		0x1
#define _LLT_READ_ACCESS		0x2

#define _LLT_INIT_DATA(x)		((x) & 0xFF)
#define _LLT_INIT_ADDR(x)		(((x) & 0xFF) << 8)
#define _LLT_OP(x)			(((x) & 0x3) << 30)
#define _LLT_OP_VALUE(x)		(((x) >> 30) & 0x3)

/* 	0x0200h ~ 0x027Fh	TXDMA Configuration */

#define NUM_HQ 0x29

#define LD_RQPN				BIT(31)

/* 2TDECTRL */
#define BCN_VALID			BIT(16)
#define BCN_HEAD(x)			(((x) & 0xFF) << 8)
#define	BCN_HEAD_MASK			0xFF00

/* 2 TDECTL */
#define BLK_DESC_NUM_SHIFT		4
#define BLK_DESC_NUM_MASK		0xF

/* 2 TXDMA_OFFSET_CHK */
#define DROP_DATA_EN			BIT(9)

/* 	0x0280h ~ 0x028Bh	RX DMA Configuration */

/*     REG_RXDMA_CONTROL, 0x0286h */

/* 2 REG_RXPKT_NUM, 0x0284 */
#define		RXPKT_RELEASE_POLL	BIT(16)
#define	RXDMA_IDLE			BIT(17)
#define	RW_RELEASE_EN			BIT(18)

/* 	0x0400h ~ 0x047Fh	Protocol Configuration */
/* 2 FWHW_TXQ_CTRL */
#define EN_AMPDU_RTY_NEW		BIT(7)

/* 2 SPEC SIFS */
#define _SPEC_SIFS_CCK(x)		((x) & 0xFF)
#define _SPEC_SIFS_OFDM(x)		(((x) & 0xFF) << 8)

/* 2 RL */
#define	RETRY_LIMIT_SHORT_SHIFT		8
#define	RETRY_LIMIT_LONG_SHIFT		0

/* 	0x0500h ~ 0x05FFh	EDCA Configuration */

/* 2 EDCA setting */
#define AC_PARAM_TXOP_LIMIT_OFFSET	16
#define AC_PARAM_ECW_MAX_OFFSET		12
#define AC_PARAM_ECW_MIN_OFFSET		8
#define AC_PARAM_AIFS_OFFSET		0

#define _LRL(x)			((x) & 0x3F)
#define _SRL(x)			(((x) & 0x3F) << 8)

/* 2 BCN_CTRL */
#define EN_MBSSID		BIT(1)
#define EN_TXBCN_RPT		BIT(2)
#define EN_BCN_FUNCTION		BIT(3)
#define DIS_TSF_UPDATE		BIT(3)

/*  The same function but different bit field. */
#define DIS_TSF_UDT0_NORMAL_CHIP	BIT(4)
#define DIS_TSF_UDT0_TEST_CHIP	BIT(5)
#define STOP_BCNQ		BIT(6)

/* 2 ACMHWCTRL */
#define ACMHW_BEQEN		BIT(1)
#define ACMHW_VIQEN		BIT(2)
#define ACMHW_VOQEN		BIT(3)

/* 	0x0600h ~ 0x07FFh	WMAC Configuration */
/* 2APSD_CTRL */
#define APSDOFF			BIT(6)
#define APSDOFF_STATUS		BIT(7)

#define RATE_BITMAP_ALL		0xFFFFF

/*  Only use CCK 1M rate for ACK */
#define RATE_RRSR_CCK_ONLY_1M	0xFFFF1

/* 2 TCR */
#define TSFRST			BIT(0)
#define DIS_GCLK		BIT(1)
#define PAD_SEL			BIT(2)
#define PWR_ST			BIT(6)
#define PWRBIT_OW_EN		BIT(7)
#define ACRC			BIT(8)
#define CFENDFORM		BIT(9)
#define ICV			BIT(10)

/* 2 RCR */
#define AAP			BIT(0)
#define APM			BIT(1)
#define AM			BIT(2)
#define AB			BIT(3)
#define ADD3			BIT(4)
#define APWRMGT			BIT(5)
#define CBSSID			BIT(6)
#define CBSSID_DATA		BIT(6)
#define CBSSID_BCN		BIT(7)
#define ACRC32			BIT(8)
#define AICV			BIT(9)
#define ADF			BIT(11)
#define ACF			BIT(12)
#define AMF			BIT(13)
#define HTC_LOC_CTRL		BIT(14)
#define UC_DATA_EN		BIT(16)
#define BM_DATA_EN		BIT(17)
#define MFBEN			BIT(22)
#define LSIGEN			BIT(23)
#define EnMBID			BIT(24)
#define APP_BASSN		BIT(27)
#define APP_PHYSTS		BIT(28)
#define APP_ICV			BIT(29)
#define APP_MIC			BIT(30)
#define APP_FCS			BIT(31)

/* 2 SECCFG */
#define	SCR_TxUseDK		BIT(0)	/* Force Tx Use Default Key */
#define	SCR_RxUseDK		BIT(1)	/* Force Rx Use Default Key */
#define	SCR_TxEncEnable		BIT(2)	/* Enable Tx Encryption */
#define	SCR_RxDecEnable		BIT(3)	/* Enable Rx Decryption */
#define	SCR_SKByA2		BIT(4)	/* Search kEY BY A2 */
#define	SCR_NoSKMC		BIT(5)	/* No Key Search Multicast */
#define SCR_TXBCUSEDK		BIT(6)	/* Force Tx Bcast pkt Use Default Key */
#define SCR_RXBCUSEDK		BIT(7)	/* Force Rx Bcast pkt Use Default Key */

/* 	0xFE00h ~ 0xFE55h	USB Configuration */

/* 2 USB Information (0xFE17) */
#define USB_IS_HIGH_SPEED			0
#define USB_IS_FULL_SPEED			1
#define USB_SPEED_MASK				BIT(5)

#define USB_NORMAL_SIE_EP_MASK			0xF
#define USB_NORMAL_SIE_EP_SHIFT			4

/* 2 Special Option */
#define USB_AGG_EN				BIT(3)

/*  0; Use interrupt endpoint to upload interrupt pkt */
/*  1; Use bulk endpoint to upload interrupt pkt, */
#define INT_BULK_SEL				BIT(4)

/* 2REG_C2HEVT_CLEAR */
/*  Set by driver and notify FW that the driver has read
 *  the C2H command message */
#define	C2H_EVT_HOST_CLOSE	0x00
/*  Set by FW indicating that FW had set the C2H command
 *  message and it's not yet read by driver. */
#define C2H_EVT_FW_CLOSE	0xFF

/* 2REG_MULTI_FUNC_CTRL(For RTL8723 Only) */
/*  Enable GPIO[9] as WiFi HW PDn source */
#define	WL_HWPDN_EN				BIT(0)
/*  WiFi HW PDn polarity control */
#define	WL_HWPDN_SL				BIT(1)
/*  WiFi function enable */
#define	WL_FUNC_EN				BIT(2)
/*  Enable GPIO[9] as WiFi RF HW PDn source */
#define	WL_HWROF_EN				BIT(3)
/*  Enable GPIO[11] as BT HW PDn source */
#define	BT_HWPDN_EN				BIT(16)
/*  BT HW PDn polarity control */
#define	BT_HWPDN_SL				BIT(17)
/*  BT function enable */
#define	BT_FUNC_EN				BIT(18)
/*  Enable GPIO[11] as BT/GPS RF HW PDn source */
#define	BT_HWROF_EN				BIT(19)
/*  Enable GPIO[10] as GPS HW PDn source */
#define	GPS_HWPDN_EN				BIT(20)
/*  GPS HW PDn polarity control */
#define	GPS_HWPDN_SL				BIT(21)
/*  GPS function enable */
#define	GPS_FUNC_EN				BIT(22)

/* 3 REG_LIFECTRL_CTRL */
#define	HAL92C_EN_PKT_LIFE_TIME_BK		BIT(3)
#define	HAL92C_EN_PKT_LIFE_TIME_BE		BIT(2)
#define	HAL92C_EN_PKT_LIFE_TIME_VI		BIT(1)
#define	HAL92C_EN_PKT_LIFE_TIME_VO		BIT(0)

#define	HAL92C_MSDU_LIFE_TIME_UNIT		128	/*  in us */

/*  General definitions */
#define LAST_ENTRY_OF_TX_PKT_BUFFER		176 /*  22k 22528 bytes */

#define POLLING_LLT_THRESHOLD			20
#define POLLING_READY_TIMEOUT_COUNT		1000
/*  GPIO BIT */
#define	HAL_8192C_HW_GPIO_WPS_BIT		BIT(2)

/*	8192C EEPROM/EFUSE share register definition. */

/* 	EEPROM/Efuse PG Offset for 88EE/88EU/88ES */
#define	EEPROM_TX_PWR_INX_88E			0x10

#define	EEPROM_ChannelPlan_88E			0xB8
#define	EEPROM_XTAL_88E				0xB9
#define	EEPROM_THERMAL_METER_88E		0xBA
#define	EEPROM_IQK_LCK_88E			0xBB

#define	EEPROM_RF_BOARD_OPTION_88E		0xC1
#define	EEPROM_RF_FEATURE_OPTION_88E		0xC2
#define	EEPROM_RF_ANTENNA_OPT_88E		0xC9

/* RTL88EU */
#define	EEPROM_MAC_ADDR_88EU			0xD7
#define EEPROM_USB_OPTIONAL_FUNCTION0		0xD4

/*  RTL88ES */
#define	EEPROM_MAC_ADDR_88ES			0x11A

#define EEPROM_Default_CrystalCap_88E		0x20
#define	EEPROM_Default_ThermalMeter_88E		0x18

/* New EFUSE default value */
#define		EEPROM_DEFAULT_24G_INDEX	0x2D
#define		EEPROM_DEFAULT_24G_HT20_DIFF	0X02
#define		EEPROM_DEFAULT_24G_OFDM_DIFF	0X04

#define		EEPROM_DEFAULT_DIFF		0XFE
#define	EEPROM_DEFAULT_BOARD_OPTION		0x00

#define EEPROM_CHANNEL_PLAN_FCC			0x0
#define EEPROM_CHANNEL_PLAN_IC			0x1
#define EEPROM_CHANNEL_PLAN_ETSI		0x2
#define EEPROM_CHANNEL_PLAN_SPA			0x3
#define EEPROM_CHANNEL_PLAN_FRANCE		0x4
#define EEPROM_CHANNEL_PLAN_MKK			0x5
#define EEPROM_CHANNEL_PLAN_MKK1		0x6
#define EEPROM_CHANNEL_PLAN_ISRAEL		0x7
#define EEPROM_CHANNEL_PLAN_TELEC		0x8
#define EEPROM_CHANNEL_PLAN_GLOBAL_DOMA		0x9
#define EEPROM_CHANNEL_PLAN_WORLD_WIDE_13	0xA
#define EEPROM_CHANNEL_PLAN_NCC			0xB
#define EEPROM_USB_OPTIONAL1			0xE
#define EEPROM_CHANNEL_PLAN_BY_HW_MASK		0x80

#define	RTL_EEPROM_ID			0x8129

#endif /* __RTL8188E_SPEC_H__ */
