/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#ifndef __HAL_COMMON_REG_H__
#define __HAL_COMMON_REG_H__

/*  */
/*  */
/* 	0x0000h ~ 0x00FFh	System Configuration */
/*  */
/*  */
#define REG_SYS_FUNC_EN				0x0002
#define REG_APS_FSMCO					0x0004
#define REG_SYS_CLKR					0x0008
#define REG_9346CR						0x000A
#define REG_SYS_EEPROM_CTRL			0x000A
#define REG_RSV_CTRL					0x001C
#define REG_RF_CTRL						0x001F
#define REG_AFE_XTAL_CTRL				0x0024
#define REG_MAC_PHY_CTRL				0x002c /* for 92d, DMDP, SMSP, DMSP contrl */
#define REG_EFUSE_CTRL					0x0030
#define REG_EFUSE_TEST					0x0034
#define REG_PWR_DATA					0x0038
#define REG_GPIO_MUXCFG				0x0040
#define REG_GPIO_INTM					0x0048
#define REG_LEDCFG0						0x004C
#define REG_LEDCFG2						0x004E
#define REG_HSIMR						0x0058
#define REG_GPIO_IO_SEL_2				0x0062 /*  RTL8723 WIFI/BT/GPS Multi-Function GPIO Select. */
#define REG_MULTI_FUNC_CTRL			0x0068 /*  RTL8723 WIFI/BT/GPS Multi-Function control source. */
#define REG_MCUFWDL					0x0080
#define REG_EFUSE_ACCESS				0x00CF	/*  Efuse access protection for RTL8723 */
#define REG_SYS_CFG						0x00F0
#define REG_GPIO_OUTSTS				0x00F4	/*  For RTL8723 only. */

/*  */
/*  */
/* 	0x0100h ~ 0x01FFh	MACTOP General Configuration */
/*  */
/*  */
#define REG_CR							0x0100
#define REG_PBP							0x0104
#define REG_TRXDMA_CTRL				0x010C
#define REG_TRXFF_BNDY					0x0114
#define REG_HIMR						0x0120
#define REG_HISR						0x0124

#define REG_C2HEVT_MSG_NORMAL		0x01A0
#define REG_C2HEVT_CLEAR				0x01AF
#define REG_HMETFR						0x01CC
#define REG_HMEBOX_0					0x01D0

/*  */
/*  */
/* 	0x0200h ~ 0x027Fh	TXDMA Configuration */
/*  */
/*  */
#define REG_RQPN						0x0200
#define REG_TDECTRL						0x0208
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

/*  */
/*  */
/* 	0x0400h ~ 0x047Fh	Protocol Configuration */
/*  */
/*  */
#define REG_TXPKT_EMPTY				0x041A
#define REG_FWHW_TXQ_CTRL				0x0420
#define REG_HWSEQ_CTRL					0x0423
#define REG_SPEC_SIFS					0x0428
#define REG_RL							0x042A
#define REG_RRSR						0x0440

#define REG_PKT_VO_VI_LIFE_TIME		0x04C0
#define REG_PKT_BE_BK_LIFE_TIME		0x04C2
#define REG_BAR_MODE_CTRL				0x04CC
#define REG_EARLY_MODE_CONTROL		0x04D0
#define REG_MACID_SLEEP				0x04D4
#define REG_NQOS_SEQ					0x04DC

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
#define REG_SIFS_CTX					0x0514
#define REG_SIFS_TRX					0x0516
#define REG_TSFTR_SYN_OFFSET			0x0518
#define REG_SLOT						0x051B
#define REG_TXPAUSE						0x0522
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
#define REG_BCN_CTRL					0x0550
#define REG_BCN_CTRL_1					0x0551
#define REG_DUAL_TSF_RST				0x0553
#define REG_BCN_INTERVAL				0x0554	/*  The same as REG_MBSSID_BCN_SPACE */
#define REG_DRVERLYINT					0x0558
#define REG_BCNDMATIM					0x0559
#define REG_ATIMWND					0x055A
#define REG_BCN_MAX_ERR				0x055D
#define REG_RXTSF_OFFSET_CCK			0x055E
#define REG_RXTSF_OFFSET_OFDM			0x055F
#define REG_TSFTR						0x0560
#define REG_ACMHWCTRL					0x05C0

/*  */
/*  */
/* 	0x0600h ~ 0x07FFh	WMAC Configuration */
/*  */
/*  */
#define REG_BWOPMODE					0x0603
#define REG_TCR							0x0604
#define REG_RCR							0x0608
#define REG_RX_DRVINFO_SZ				0x060F

#define REG_MACID						0x0610
#define REG_BSSID						0x0618
#define REG_MAR							0x0620

#define REG_MAC_SPEC_SIFS				0x063A
/*  20100719 Joseph: Hardware register definition change. (HW datasheet v54) */
#define REG_RESP_SIFS_CCK				0x063C	/*  [15:8]SIFS_R2T_OFDM, [7:0]SIFS_R2T_CCK */
#define REG_RESP_SIFS_OFDM                    0x063E	/*  [15:8]SIFS_T2T_OFDM, [7:0]SIFS_T2T_CCK */

#define REG_ACKTO						0x0640

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
#define REG_RXERR_RPT					0x0664

/*  Security */
#define REG_CAMCMD						0x0670
#define REG_CAMWRITE					0x0674
#define REG_CAMREAD					0x0678
#define REG_SECCFG						0x0680

/*  Power */
#define REG_RXFLTMAP0					0x06A0
#define REG_RXFLTMAP1					0x06A2
#define REG_RXFLTMAP2					0x06A4
#define REG_BCN_PSR_RPT				0x06A8

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

#define PBP						REG_PBP

/*  */
/*  9. Security Control Registers	(Offset:) */
/*  */
#define RWCAM					REG_CAMCMD		/* IN 8190 Data Sheet is called CAMcmd */
#define WCAMI					REG_CAMWRITE	/*  Software write CAM input content */

/*  */
/*        8723/8188E Host System Interrupt Status Register (offset 0x5C, 32 byte) */
/*  */
#define HSISR_GPIO12_0_INT				BIT0
#define HSISR_SPS_OCP_INT				BIT5
#define HSISR_RON_INT					BIT6
#define HSISR_PDNINT					BIT7
#define HSISR_GPIO9_INT					BIT25

/*  */
/*        Response Rate Set Register	(offset 0x440, 24bits) */
/*  */
#define RRSR_1M					BIT0
#define RRSR_2M					BIT1
#define RRSR_5_5M				BIT2
#define RRSR_11M				BIT3
#define RRSR_6M					BIT4
#define RRSR_12M				BIT6
#define RRSR_24M				BIT8

#define RRSR_CCK_RATES (RRSR_11M|RRSR_5_5M|RRSR_2M|RRSR_1M)

/*  */
/*        Rate Definition */
/*  */
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

/*  ALL CCK Rate */
#define RATE_BITMAP_ALL			0xFFFFF

/*  Only use CCK 1M rate for ACK */
#define RATE_RRSR_CCK_ONLY_1M		0xFFFF1

/*  */
/*        BW_OPMODE bits				(Offset 0x603, 8bit) */
/*  */
#define BW_OPMODE_20MHZ			BIT2

/*  */
/*        CAM Config Setting (offset 0x680, 1 byte) */
/*  */
#define CAM_VALID				BIT15

#define CAM_CONTENT_COUNT	8

#define CAM_AES					0x04

#define TOTAL_CAM_ENTRY		32

#define CAM_WRITE				BIT16
#define CAM_POLLINIG			BIT31

/*  */
/*  12. Host Interrupt Status Registers */
/*  */

/*  */
/*        8192C (RCR) Receive Configuration Register	(Offset 0x608, 32 bits) */
/*  */
#define RCR_APPFCS				BIT31	/*  WMAC append FCS after pauload */
#define RCR_APP_MIC				BIT30	/*  MACRX will retain the MIC at the bottom of the packet. */
#define RCR_APP_ICV				BIT29	/*  MACRX will retain the ICV at the bottom of the packet. */
#define RCR_APP_PHYST_RXFF		BIT28	/*  PHY Status is appended before RX packet in RXFF */
#define RCR_APP_BA_SSN			BIT27	/*  SSN of previous TXBA is appended as after original RXDESC as the 4-th DW of RXDESC. */
#define RCR_HTC_LOC_CTRL		BIT14	/*  MFC<--HTC = 1 MFC-->HTC = 0 */
#define RCR_AMF					BIT13	/*  Accept management type frame */
#define RCR_ADF					BIT11	/*  Accept data type frame. This bit also regulates BA, BAR, and PS-Poll (AP mode only). */
#define RCR_ACRC32				BIT8		/*  Accept CRC32 error packet */
#define RCR_CBSSID_BCN			BIT7		/*  Accept BSSID match packet (Rx beacon, probe rsp) */
#define RCR_CBSSID_DATA		BIT6		/*  Accept BSSID match packet (Data) */
#define RCR_AB					BIT3		/*  Accept broadcast packet */
#define RCR_AM					BIT2		/*  Accept multicast packet */
#define RCR_APM					BIT1		/*  Accept physical match packet */


/*  */
/*  */
/* 	0x0000h ~ 0x00FFh	System Configuration */
/*  */
/*  */

/* 2 SYS_FUNC_EN */
#define FEN_BBRSTB				BIT(0)
#define FEN_BB_GLB_RSTn		BIT(1)
#define FEN_DIO_PCIE			BIT(5)
#define FEN_PCIEA				BIT(6)
#define FEN_PPLL					BIT(7)
#define FEN_CPUEN				BIT(10)
#define FEN_ELDR				BIT(12)

/* 2 APS_FSMCO */
#define EnPDN					BIT(4)

/* 2 SYS_CLKR */
#define ANA8M					BIT(1)
#define LOADER_CLK_EN			BIT(5)


/* 2 9346CR /REG_SYS_EEPROM_CTRL */
#define BOOT_FROM_EEPROM		BIT(4)
#define EEPROM_EN				BIT(5)


/* 2 RF_CTRL */
#define RF_EN					BIT(0)
#define RF_RSTB					BIT(1)
#define RF_SDMRSTB				BIT(2)

/* 2 EFUSE_TEST (For RTL8723 partially) */
#define EFUSE_SEL(x)				(((x) & 0x3) << 8)
#define EFUSE_SEL_MASK			0x300
#define EFUSE_WIFI_SEL_0		0x0
#define EFUSE_BT_SEL_0			0x1
#define EFUSE_BT_SEL_1			0x2
#define EFUSE_BT_SEL_2			0x3


/* 2 8051FWDL */
/* 2 MCUFWDL */
#define MCUFWDL_RDY			BIT(1)
#define FWDL_ChkSum_rpt		BIT(2)
#define WINTINI_RDY				BIT(6)
#define RAM_DL_SEL				BIT(7)

/* 2 REG_SYS_CFG */
#define VENDOR_ID				BIT(19)

#define RTL_ID					BIT(23) /*  TestChip ID, 1:Test(RLE); 0:MP(RL) */
#define SPS_SEL					BIT(24) /*  1:LDO regulator mode; 0:Switching regulator mode */


#define CHIP_VER_RTL_MASK		0xF000	/* Bit 12 ~ 15 */
#define CHIP_VER_RTL_SHIFT		12

/* 2 REG_GPIO_OUTSTS (For RTL8723 only) */
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
#define NT_LINK_AD_HOC			0x1
#define NT_LINK_AP				0x2

/* 2 PBP - Page Size Register */
#define _PSRX(x)				(x)
#define _PSTX(x)				((x) << 4)

#define PBP_128					0x1

/* 2 TX/RXDMA */
#define RXDMA_AGG_EN			BIT(2)

/*  For normal driver, 0x10C */
#define _TXDMA_HIQ_MAP(x)			(((x)&0x3) << 14)
#define _TXDMA_MGQ_MAP(x)			(((x)&0x3) << 12)
#define _TXDMA_BKQ_MAP(x)			(((x)&0x3) << 10)
#define _TXDMA_BEQ_MAP(x)			(((x)&0x3) << 8)
#define _TXDMA_VIQ_MAP(x)			(((x)&0x3) << 6)
#define _TXDMA_VOQ_MAP(x)			(((x)&0x3) << 4)

#define QUEUE_LOW				1
#define QUEUE_NORMAL			2
#define QUEUE_HIGH				3

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

#define LD_RQPN					BIT(31)

/* 2 AUTO_LLT */
#define BIT_AUTO_INIT_LLT BIT(16)

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

#define _LRL(x)					((x) & 0x3F)
#define _SRL(x)					(((x) & 0x3F) << 8)


/* 2 BCN_CTRL */
#define EN_TXBCN_RPT			BIT(2)
#define EN_BCN_FUNCTION		BIT(3)

#define DIS_ATIM					BIT(0)
#define DIS_BCNQ_SUB			BIT(1)
#define DIS_TSF_UDT				BIT(4)

/* 2 ACMHWCTRL */
#define AcmHw_HwEn				BIT(0)
#define AcmHw_BeqEn			BIT(1)
#define AcmHw_ViqEn				BIT(2)
#define AcmHw_VoqEn			BIT(3)

/*  */
/*  */
/* 	0x0600h ~ 0x07FFh	WMAC Configuration */
/*  */
/*  */

/* 2 TCR */
#define TSFRST					BIT(0)

/* 2 RCR */
#define AB						BIT(3)

/* 2 SECCFG */
#define SCR_TxUseDK				BIT(0)			/* Force Tx Use Default Key */
#define SCR_RxUseDK				BIT(1)			/* Force Rx Use Default Key */
#define SCR_TxEncEnable			BIT(2)			/* Enable Tx Encryption */
#define SCR_RxDecEnable			BIT(3)			/* Enable Rx Decryption */
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

/* SDIO host local register space mapping. */
#define SDIO_LOCAL_MSK				0x0FFF
#define WLAN_IOREG_MSK			0x7FFF
#define WLAN_FIFO_MSK				0x1FFF	/*  Aggregation Length[12:0] */
#define WLAN_RX0FF_MSK				0x0003

#define SDIO_LOCAL_DEVICE_ID			0	/*  0b[16], 000b[15:13] */
#define WLAN_TX_HIQ_DEVICE_ID			4	/*  0b[16], 100b[15:13] */
#define WLAN_TX_MIQ_DEVICE_ID		5	/*  0b[16], 101b[15:13] */
#define WLAN_TX_LOQ_DEVICE_ID		6	/*  0b[16], 110b[15:13] */
#define WLAN_RX0FF_DEVICE_ID			7	/*  0b[16], 111b[15:13] */
#define WLAN_IOREG_DEVICE_ID			8	/*  1b[16] */

/* SDIO Tx Free Page Index */
#define HI_QUEUE_IDX				0
#define MID_QUEUE_IDX				1
#define LOW_QUEUE_IDX				2
#define PUBLIC_QUEUE_IDX			3

#define SDIO_MAX_TX_QUEUE			3		/*  HIQ, MIQ and LOQ */

#define SDIO_REG_TX_CTRL			0x0000 /*  SDIO Tx Control */
#define SDIO_REG_HIMR				0x0014 /*  SDIO Host Interrupt Mask */
#define SDIO_REG_HISR				0x0018 /*  SDIO Host Interrupt Service Routine */
#define SDIO_REG_RX0_REQ_LEN		0x001C /*  RXDMA Request Length */
#define SDIO_REG_OQT_FREE_PG		0x001E /*  OQT Free Page */
#define SDIO_REG_FREE_TXPG			0x0020 /*  Free Tx Buffer Page */
#define SDIO_REG_HRPWM1			0x0080 /*  HCI Request Power Mode 1 */
#define SDIO_REG_HSUS_CTRL			0x0086 /*  SDIO HCI Suspend Control */

#define SDIO_HIMR_DISABLED			0

/*  RTL8723/RTL8188E SDIO Host Interrupt Mask Register */
#define SDIO_HIMR_RX_REQUEST_MSK		BIT0
#define SDIO_HIMR_AVAL_MSK			BIT1

/*  SDIO Host Interrupt Service Routine */
#define SDIO_HISR_RX_REQUEST			BIT0
#define SDIO_HISR_AVAL					BIT1
#define SDIO_HISR_TXERR					BIT2
#define SDIO_HISR_RXERR					BIT3
#define SDIO_HISR_TXFOVW				BIT4
#define SDIO_HISR_RXFOVW				BIT5
#define SDIO_HISR_TXBCNOK				BIT6
#define SDIO_HISR_TXBCNERR				BIT7
#define SDIO_HISR_C2HCMD				BIT17
#define SDIO_HISR_CPWM1				BIT18
#define SDIO_HISR_CPWM2				BIT19
#define SDIO_HISR_HSISR_IND			BIT20
#define SDIO_HISR_GTINT3_IND			BIT21
#define SDIO_HISR_GTINT4_IND			BIT22
#define SDIO_HISR_PSTIMEOUT			BIT23
#define SDIO_HISR_OCPINT				BIT24

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

/*  SDIO Tx FIFO related */
#define SDIO_TX_FREE_PG_QUEUE			4	/*  The number of Tx FIFO free page */

/*  */
/*  */
/* 	0xFE00h ~ 0xFE55h	USB Configuration */
/*  */
/*  */

/* 2REG_C2HEVT_CLEAR */
#define C2H_EVT_HOST_CLOSE		0x00	/*  Set by driver and notify FW that the driver has read the C2H command message */
#define C2H_EVT_FW_CLOSE		0xFF	/*  Set by FW indicating that FW had set the C2H command message and it's not yet read by driver. */

/* 2REG_MULTI_FUNC_CTRL(For RTL8723 Only) */
#define WL_HWPDN_SL			BIT1	/*  WiFi HW PDn polarity control */
#define WL_FUNC_EN				BIT2	/*  WiFi function enable */
#define BT_FUNC_EN				BIT18	/*  BT function enable */
#define GPS_FUNC_EN			BIT22	/*  GPS function enable */

#endif /* __HAL_COMMON_H__ */
