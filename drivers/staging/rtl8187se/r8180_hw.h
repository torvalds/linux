/*
	This is part of rtl8180 OpenSource driver.
	Copyright (C) Andrea Merello 2004-2005  <andrea.merello@gmail.com>
	Released under the terms of GPL (General Public Licence)

	Parts of this driver are based on the GPL part of the
	official Realtek driver.
	Parts of this driver are based on the rtl8180 driver skeleton
	from Patric Schenke & Andres Salomon.
	Parts of this driver are based on the Intel Pro Wireless
	2100 GPL driver.

	We want to tanks the Authors of those projects
	and the Ndiswrapper project Authors.
*/

/* Mariusz Matuszek added full registers definition with Realtek's name */

/* this file contains register definitions for the rtl8180 MAC controller */
#ifndef R8180_HW
#define R8180_HW


#define BIT0	0x00000001
#define BIT1	0x00000002
#define BIT2	0x00000004
#define BIT3	0x00000008
#define BIT4	0x00000010
#define BIT5	0x00000020
#define BIT6	0x00000040
#define BIT7	0x00000080
#define BIT9	0x00000200
#define BIT11	0x00000800
#define BIT13	0x00002000
#define BIT15	0x00008000
#define BIT20	0x00100000
#define BIT21	0x00200000
#define BIT22	0x00400000
#define BIT23	0x00800000
#define BIT24	0x01000000
#define BIT25	0x02000000
#define BIT26	0x04000000
#define BIT27	0x08000000
#define BIT28	0x10000000
#define BIT29	0x20000000
#define BIT30	0x40000000
#define BIT31	0x80000000

#define MAX_SLEEP_TIME (10000)
#define MIN_SLEEP_TIME (50)

#define BB_HOST_BANG_EN (1<<2)
#define BB_HOST_BANG_CLK (1<<1)

#define MAC0 0
#define MAC4 4

#define CMD 0x37
#define CMD_RST_SHIFT 4
#define CMD_RX_ENABLE_SHIFT 3
#define CMD_TX_ENABLE_SHIFT 2

#define EPROM_CMD 0x50
#define EPROM_CMD_RESERVED_MASK ((1<<5)|(1<<4))
#define EPROM_CMD_OPERATING_MODE_SHIFT 6
#define EPROM_CMD_OPERATING_MODE_MASK ((1<<7)|(1<<6))
#define EPROM_CMD_CONFIG 0x3
#define EPROM_CMD_NORMAL 0
#define EPROM_CMD_LOAD 1
#define EPROM_CMD_PROGRAM 2
#define EPROM_CS_SHIFT 3
#define EPROM_CK_SHIFT 2
#define EPROM_W_SHIFT 1
#define EPROM_R_SHIFT 0
#define CONFIG2_DMA_POLLING_MODE_SHIFT 3

#define INTA_TXOVERFLOW (1<<15)
#define INTA_TIMEOUT (1<<14)
#define INTA_HIPRIORITYDESCERR (1<<9)
#define INTA_HIPRIORITYDESCOK (1<<8)
#define INTA_NORMPRIORITYDESCERR (1<<7)
#define INTA_NORMPRIORITYDESCOK (1<<6)
#define INTA_RXOVERFLOW (1<<5)
#define INTA_RXDESCERR (1<<4)
#define INTA_LOWPRIORITYDESCERR (1<<3)
#define INTA_LOWPRIORITYDESCOK (1<<2)
#define INTA_RXOK (1)
#define INTA_MASK 0x3c

#define RXRING_ADDR 0xe4 /* page 0 */
#define PGSELECT 0x5e
#define PGSELECT_PG_SHIFT 0
#define RX_CONF 0x44
#define MAC_FILTER_MASK ((1<<0) | (1<<1) | (1<<2) | (1<<3) | (1<<5) | \
(1<<12) | (1<<18) | (1<<19) | (1<<20) | (1<<21) | (1<<22) | (1<<23))
#define RX_CHECK_BSSID_SHIFT 23
#define ACCEPT_PWR_FRAME_SHIFT 22
#define ACCEPT_MNG_FRAME_SHIFT 20
#define ACCEPT_CTL_FRAME_SHIFT 19
#define ACCEPT_DATA_FRAME_SHIFT 18
#define ACCEPT_ICVERR_FRAME_SHIFT 12
#define ACCEPT_CRCERR_FRAME_SHIFT 5
#define ACCEPT_BCAST_FRAME_SHIFT 3
#define ACCEPT_MCAST_FRAME_SHIFT 2
#define ACCEPT_ALLMAC_FRAME_SHIFT 0
#define ACCEPT_NICMAC_FRAME_SHIFT 1

#define RX_FIFO_THRESHOLD_MASK ((1<<13) | (1<<14) | (1<<15))
#define RX_FIFO_THRESHOLD_SHIFT 13
#define RX_FIFO_THRESHOLD_NONE 7
#define RX_AUTORESETPHY_SHIFT 28

#define TX_CONF 0x40
#define TX_CONF_HEADER_AUTOICREMENT_SHIFT 30
#define TX_LOOPBACK_SHIFT 17
#define TX_LOOPBACK_NONE 0
#define TX_LOOPBACK_CONTINUE 3
#define TX_LOOPBACK_MASK ((1<<17)|(1<<18))
#define TX_DPRETRY_SHIFT 0
#define R8180_MAX_RETRY 255
#define TX_RTSRETRY_SHIFT 8
#define TX_NOICV_SHIFT 19
#define TX_NOCRC_SHIFT 16
#define TX_DMA_POLLING 0xd9
#define TX_DMA_POLLING_BEACON_SHIFT 7
#define TX_DMA_POLLING_HIPRIORITY_SHIFT 6
#define TX_DMA_POLLING_NORMPRIORITY_SHIFT 5
#define TX_DMA_POLLING_LOWPRIORITY_SHIFT 4
#define TX_MANAGEPRIORITY_RING_ADDR 0x0C
#define TX_BKPRIORITY_RING_ADDR 0x10
#define TX_BEPRIORITY_RING_ADDR 0x14
#define TX_VIPRIORITY_RING_ADDR 0x20
#define TX_VOPRIORITY_RING_ADDR 0x24
#define TX_HIGHPRIORITY_RING_ADDR 0x28
#define MAX_RX_DMA_MASK ((1<<8) | (1<<9) | (1<<10))
#define MAX_RX_DMA_2048 7
#define MAX_RX_DMA_1024	6
#define MAX_RX_DMA_SHIFT 10
#define INT_TIMEOUT 0x48
#define CONFIG3_CLKRUN_SHIFT 2
#define CONFIG3_ANAPARAM_W_SHIFT 6
#define ANAPARAM 0x54
#define BEACON_INTERVAL 0x70
#define BEACON_INTERVAL_MASK ((1<<0)|(1<<1)|(1<<2)|(1<<3)|(1<<4)|(1<<5)| \
(1<<6)|(1<<7)|(1<<8)|(1<<9))
#define ATIM_MASK ((1<<0)|(1<<1)|(1<<2)|(1<<3)|(1<<4)|(1<<5)|(1<<6)|(1<<7)| \
(1<<8)|(1<<9))
#define ATIM 0x72
#define EPROM_CS_SHIFT 3
#define EPROM_CK_SHIFT 2
#define PHY_ADR 0x7c
#define SECURITY 0x5f /* 1209 this is sth wrong */
#define SECURITY_WEP_TX_ENABLE_SHIFT 1
#define SECURITY_WEP_RX_ENABLE_SHIFT 0
#define SECURITY_ENCRYP_104 1
#define SECURITY_ENCRYP_SHIFT 4
#define SECURITY_ENCRYP_MASK ((1<<4)|(1<<5))
#define KEY0 0x90  /* 1209 this is sth wrong */
#define CONFIG2_ANTENNA_SHIFT 6
#define TX_BEACON_RING_ADDR 0x4c
#define CONFIG0_WEP40_SHIFT 7
#define CONFIG0_WEP104_SHIFT 6
#define AGCRESET_SHIFT 5



/*
 * Operational registers offsets in PCI (I/O) space.
 * RealTek names are used.
 */

#define TSFTR 0x0018

#define TLPDA 0x0020

#define BSSID 0x002E

#define CR 0x0037

#define RF_SW_CONFIG	        0x8			/* store data which is transmitted to RF for driver	*/
#define RF_SW_CFG_SI		BIT1
#define EIFS			0x2D			/* Extended InterFrame Space Timer, in unit of 4 us.	*/

#define BRSR			0x34			/* Basic rate set										*/

#define IMR 0x006C
#define ISR 0x003C

#define TCR 0x0040

#define RCR 0x0044

#define TimerInt 0x0048

#define CR9346 0x0050

#define CONFIG0 0x0051
#define CONFIG2 0x0053

#define MSR 0x0058

#define CONFIG3 0x0059
#define CONFIG4 0x005A
	/* SD3 szuyitasi: Mac0x57= CC -> B0 Mac0x60= D1 -> C6	*/
	/* Mac0x60 = 0x000004C6 power save parameters			*/
	#define ANAPARM_ASIC_ON    0xB0054D00
	#define ANAPARM2_ASIC_ON  0x000004C6

	#define ANAPARM_ON ANAPARM_ASIC_ON
	#define ANAPARM2_ON ANAPARM2_ASIC_ON

#define TESTR 0x005B

#define PSR 0x005E

#define BcnItv 0x0070

#define AtimWnd 0x0072

#define BintrItv 0x0074

#define PhyAddr 0x007C
#define PhyDataR 0x007E

/* following are for rtl8185 */
#define RFPinsOutput 0x80
#define RFPinsEnable 0x82
#define RF_TIMING 0x8c
#define RFPinsSelect 0x84
#define ANAPARAM2 0x60
#define RF_PARA 0x88
#define RFPinsInput 0x86
#define GP_ENABLE 0x90
#define GPIO 0x91
#define SW_CONTROL_GPIO 0x400
#define TX_ANTENNA 0x9f
#define TX_GAIN_OFDM 0x9e
#define TX_GAIN_CCK 0x9d
#define WPA_CONFIG 0xb0
#define TX_AGC_CTL 0x9c
#define TX_AGC_CTL_PERPACKET_GAIN_SHIFT 0
#define TX_AGC_CTL_PERPACKET_ANTSEL_SHIFT 1
#define TX_AGC_CTL_FEEDBACK_ANT 2
#define RESP_RATE 0x34
#define SIFS 0xb4
#define DIFS 0xb5

#define SLOT 0xb6
#define CW_CONF 0xbc
#define CW_CONF_PERPACKET_RETRY_SHIFT 1
#define CW_CONF_PERPACKET_CW_SHIFT 0
#define CW_VAL 0xbd
#define MAX_RESP_RATE_SHIFT 4
#define MIN_RESP_RATE_SHIFT 0
#define RATE_FALLBACK 0xbe

#define CONFIG5 0x00D8

#define PHYPR			0xDA			/* 0xDA - 0x0B PHY Parameter Register.	*/

#define FEMR			0x1D4	/* Function Event Mask register	*/

#define FFER 0x00FC
#define FFER_END 0x00FF



/*
 * Bitmasks for specific register functions.
 * Names are derived from the register name and function name.
 *
 * <REGISTER>_<FUNCTION>[<bit>]
 *
 * this leads to some awkward names...
 */

#define BRSR_BPLCP  ((1 << 8))
#define BRSR_MBR    ((1 << 1)|(1 << 0))
#define BRSR_MBR_8185 ((1 << 11)|(1 << 10)|(1 << 9)|(1 << 8)|(1 << 7)|(1 << 6)|(1 << 5)|(1 << 4)|(1 << 3)|(1 << 2)|(1 << 1)|(1 << 0))
#define BRSR_MBR0   ((1 << 0))
#define BRSR_MBR1   ((1 << 1))

#define CR_RST      ((1 << 4))
#define CR_RE       ((1 << 3))
#define CR_TE       ((1 << 2))
#define CR_MulRW    ((1 << 0))

#define IMR_Dot11hInt	((1 << 25))			/*802.11h Measurement Interrupt					*/
#define IMR_BcnDmaInt	((1 << 24))			/*Beacon DMA Interrupt */ /*What differenct between BcnDmaInt and BcnInt???	*/
#define IMR_WakeInt		((1 << 23))			/*Wake Up Interrupt								*/
#define IMR_TXFOVW		((1 << 22))			/*Tx FIFO Overflow Interrupt					*/
#define IMR_TimeOut1	((1 << 21))			/*Time Out Interrupt 1							*/
#define IMR_BcnInt		((1 << 20))			/*Beacon Time out Interrupt						*/
#define IMR_ATIMInt		((1 << 19))			/*ATIM Time Out Interrupt						*/
#define IMR_TBDER		((1 << 18))			/*Tx Beacon Descriptor Error Interrupt			*/
#define IMR_TBDOK		((1 << 17))			/*Tx Beacon Descriptor OK Interrupt				*/
#define IMR_THPDER		((1 << 16))			/*Tx High Priority Descriptor Error Interrupt	*/
#define IMR_THPDOK		((1 << 15))			/*Tx High Priority Descriptor OK Interrupt		*/
#define IMR_TVODER		((1 << 14))			/*Tx AC_VO Descriptor Error Interrupt			*/
#define IMR_TVODOK		((1 << 13))			/*Tx AC_VO Descriptor OK Interrupt				*/
#define IMR_FOVW		((1 << 12))			/*Rx FIFO Overflow Interrupt					*/
#define IMR_RDU			((1 << 11))			/*Rx Descriptor Unavailable Interrupt			*/
#define IMR_TVIDER		((1 << 10))			/*Tx AC_VI Descriptor Error Interrupt			*/
#define IMR_TVIDOK		((1 << 9))			/*Tx AC_VI Descriptor OK Interrupt				*/
#define IMR_RER			((1 << 8))			/*Rx Error Interrupt							*/
#define IMR_ROK			((1 << 7))			/*Receive OK Interrupt							*/
#define IMR_TBEDER		((1 << 6))			/*Tx AC_BE Descriptor Error Interrupt			*/
#define IMR_TBEDOK		((1 << 5))			/*Tx AC_BE Descriptor OK Interrupt				*/
#define IMR_TBKDER		((1 << 4))			/*Tx AC_BK Descriptor Error Interrupt			*/
#define IMR_TBKDOK		((1 << 3))			/*Tx AC_BK Descriptor OK Interrupt				*/
#define IMR_RQoSOK		((1 << 2))			/*Rx QoS OK Interrupt							*/
#define IMR_TimeOut2	((1 << 1))			/*Time Out Interrupt 2							*/
#define IMR_TimeOut3	((1 << 0))			/*Time Out Interrupt 3							*/
#define IMR_TMGDOK      ((1 << 30))
#define ISR_Dot11hInt	((1 << 25))			/*802.11h Measurement Interrupt					*/
#define ISR_BcnDmaInt	((1 << 24))			/*Beacon DMA Interrupt	*/ /*What differenct between BcnDmaInt and BcnInt???	*/
#define ISR_WakeInt		((1 << 23))			/*Wake Up Interrupt								*/
#define ISR_TXFOVW		((1 << 22))			/*Tx FIFO Overflow Interrupt					*/
#define ISR_TimeOut1	((1 << 21))			/*Time Out Interrupt 1							*/
#define ISR_BcnInt		((1 << 20))			/*Beacon Time out Interrupt						*/
#define ISR_ATIMInt		((1 << 19))			/*ATIM Time Out Interrupt						*/
#define ISR_TBDER		((1 << 18))			/*Tx Beacon Descriptor Error Interrupt			*/
#define ISR_TBDOK		((1 << 17))			/*Tx Beacon Descriptor OK Interrupt				*/
#define ISR_THPDER		((1 << 16))			/*Tx High Priority Descriptor Error Interrupt	*/
#define ISR_THPDOK		((1 << 15))			/*Tx High Priority Descriptor OK Interrupt		*/
#define ISR_TVODER		((1 << 14))			/*Tx AC_VO Descriptor Error Interrupt			*/
#define ISR_TVODOK		((1 << 13))			/*Tx AC_VO Descriptor OK Interrupt				*/
#define ISR_FOVW		((1 << 12))			/*Rx FIFO Overflow Interrupt					*/
#define ISR_RDU			((1 << 11))			/*Rx Descriptor Unavailable Interrupt			*/
#define ISR_TVIDER		((1 << 10))			/*Tx AC_VI Descriptor Error Interrupt			*/
#define ISR_TVIDOK		((1 << 9))			/*Tx AC_VI Descriptor OK Interrupt				*/
#define ISR_RER			((1 << 8))			/*Rx Error Interrupt							*/
#define ISR_ROK			((1 << 7))			/*Receive OK Interrupt							*/
#define ISR_TBEDER		((1 << 6))			/*Tx AC_BE Descriptor Error Interrupt			*/
#define ISR_TBEDOK		((1 << 5))			/*Tx AC_BE Descriptor OK Interrupt				*/
#define ISR_TBKDER		((1 << 4))			/*Tx AC_BK Descriptor Error Interrupt			*/
#define ISR_TBKDOK		((1 << 3))			/*Tx AC_BK Descriptor OK Interrupt				*/
#define ISR_RQoSOK		((1 << 2))			/*Rx QoS OK Interrupt							*/
#define ISR_TimeOut2	((1 << 1))			/*Time Out Interrupt 2							*/
#define ISR_TimeOut3	((1 << 0))			/*Time Out Interrupt 3							*/

/* these definition is used for Tx/Rx test temporarily */
#define ISR_TLPDER  ISR_TVIDER
#define ISR_TLPDOK  ISR_TVIDOK
#define ISR_TNPDER  ISR_TVODER
#define ISR_TNPDOK  ISR_TVODOK
#define ISR_TimeOut ISR_TimeOut1
#define ISR_RXFOVW ISR_FOVW


#define HW_VERID_R8180_F 3
#define HW_VERID_R8180_ABCD 2
#define HW_VERID_R8185_ABC 4
#define HW_VERID_R8185_D 5
#define HW_VERID_R8185B_B 6

#define TCR_CWMIN   ((1 << 31))
#define TCR_SWSEQ   ((1 << 30))
#define TCR_HWVERID_MASK ((1 << 27)|(1 << 26)|(1 << 25))
#define TCR_HWVERID_SHIFT 25
#define TCR_SAT     ((1 << 24))
#define TCR_PLCP_LEN TCR_SAT /* rtl8180 */
#define TCR_MXDMA_MASK   ((1 << 23)|(1 << 22)|(1 << 21))
#define TCR_MXDMA_1024 6
#define TCR_MXDMA_2048 7
#define TCR_MXDMA_SHIFT  21
#define TCR_DISCW   ((1 << 20))
#define TCR_ICV     ((1 << 19))
#define TCR_LBK     ((1 << 18)|(1 << 17))
#define TCR_LBK1    ((1 << 18))
#define TCR_LBK0    ((1 << 17))
#define TCR_CRC     ((1 << 16))
#define TCR_DPRETRY_MASK   ((1 << 15)|(1 << 14)|(1 << 13)|(1 << 12)|(1 << 11)|(1 << 10)|(1 << 9)|(1 << 8))
#define TCR_RTSRETRY_MASK   ((1 << 0)|(1 << 1)|(1 << 2)|(1 << 3)|(1 << 4)|(1 << 5)|(1 << 6)|(1 << 7))
#define TCR_PROBE_NOTIMESTAMP_SHIFT 29 /* rtl8185 */

#define RCR_ONLYERLPKT ((1 << 31))
#define RCR_CS_SHIFT   29
#define RCR_CS_MASK    ((1 << 30) | (1 << 29))
#define RCR_ENMARP     ((1 << 28))
#define RCR_CBSSID     ((1 << 23))
#define RCR_APWRMGT    ((1 << 22))
#define RCR_ADD3       ((1 << 21))
#define RCR_AMF        ((1 << 20))
#define RCR_ACF        ((1 << 19))
#define RCR_ADF        ((1 << 18))
#define RCR_RXFTH      ((1 << 15)|(1 << 14)|(1 << 13))
#define RCR_RXFTH2     ((1 << 15))
#define RCR_RXFTH1     ((1 << 14))
#define RCR_RXFTH0     ((1 << 13))
#define RCR_AICV       ((1 << 12))
#define RCR_MXDMA      ((1 << 10)|(1 << 9)|(1 << 8))
#define RCR_MXDMA2     ((1 << 10))
#define RCR_MXDMA1     ((1 << 9))
#define RCR_MXDMA0     ((1 << 8))
#define RCR_9356SEL    ((1 << 6))
#define RCR_ACRC32     ((1 << 5))
#define RCR_AB         ((1 << 3))
#define RCR_AM         ((1 << 2))
#define RCR_APM        ((1 << 1))
#define RCR_AAP        ((1 << 0))

#define CR9346_EEM     ((1 << 7)|(1 << 6))
#define CR9346_EEM1    ((1 << 7))
#define CR9346_EEM0    ((1 << 6))
#define CR9346_EECS    ((1 << 3))
#define CR9346_EESK    ((1 << 2))
#define CR9346_EED1    ((1 << 1))
#define CR9346_EED0    ((1 << 0))

#define CONFIG3_PARM_En    ((1 << 6))
#define CONFIG3_FuncRegEn  ((1 << 1))

#define CONFIG4_PWRMGT     ((1 << 5))

#define MSR_LINK_MASK      ((1 << 2)|(1 << 3))
#define MSR_LINK_MANAGED   2
#define MSR_LINK_NONE      0
#define MSR_LINK_SHIFT     2
#define MSR_LINK_ADHOC     1
#define MSR_LINK_MASTER    3

#define BcnItv_BcnItv      (0x01FF)

#define AtimWnd_AtimWnd    (0x01FF)

#define BintrItv_BintrItv  (0x01FF)

#define FEMR_INTR    ((1 << 15))
#define FEMR_WKUP    ((1 << 14))
#define FEMR_GWAKE   ((1 << 4))

#define FFER_INTR    ((1 << 15))
#define FFER_GWAKE   ((1 << 4))

/* Three wire mode.					*/
#define SW_THREE_WIRE			0
#define HW_THREE_WIRE			2
/* RTL8187S by amy					*/
#define HW_THREE_WIRE_PI		5
#define HW_THREE_WIRE_SI		6
/* by amy							*/
#define TCR_LRL_OFFSET		0
#define TCR_SRL_OFFSET		8
#define TCR_MXDMA_OFFSET	21
#define TCR_DISReqQsize_OFFSET		28
#define TCR_DurProcMode_OFFSET		30

#define RCR_MXDMA_OFFSET				8
#define RCR_FIFO_OFFSET					13

#define AckTimeOutReg	0x79		/* ACK timeout register, in unit of 4 us. */

#define RFTiming			0x8C

#define TPPollStop		0x93

#define TXAGC_CTL		0x9C			/*< RJ_TODO_8185B> TX_AGC_CONTROL (0x9C seems be removed at 8185B, see p37). */
#define CCK_TXAGC		0x9D
#define OFDM_TXAGC		0x9E
#define ANTSEL			0x9F

#define ACM_CONTROL             0x00BF      /* ACM Control Registe */

#define	IntMig			0xE2			/* Interrupt Migration (0xE2 ~ 0xE3)	*/

#define TID_AC_MAP		0xE8			/* TID to AC Mapping Register			*/

#define ANAPARAM3		0xEE			/* <RJ_TODO_8185B> How to use it?		*/

#define AC_VO_PARAM		0xF0			/* AC_VO Parameters Record				*/
#define AC_VI_PARAM		0xF4			/* AC_VI Parameters Record				*/
#define AC_BE_PARAM		0xF8			/* AC_BE Parameters Record				*/
#define AC_BK_PARAM		0xFC			/* AC_BK Parameters Record				*/

#define GPIOCtrl			0x16B			/*GPIO Control Register.			*/
#define ARFR			0x1E0	/* Auto Rate Fallback Register (0x1e0 ~ 0x1e2)	*/

#define RFSW_CTRL			0x272	/* 0x272-0x273.								*/
#define SW_3W_DB0			0x274	/* Software 3-wire data buffer bit 31~0.		*/
#define SW_3W_DB1			0x278	/* Software 3-wire data buffer bit 63~32.	*/
#define SW_3W_CMD0			0x27C	/* Software 3-wire Control/Status Register.	*/
#define SW_3W_CMD1			0x27D	/* Software 3-wire Control/Status Register.	*/

#define PI_DATA_READ		0X360	/* 0x360 - 0x361  Parallel Interface Data Register.	*/
#define SI_DATA_READ		0x362	/* 0x362 - 0x363  Serial Interface Data Register.	*/

/*
----------------------------------------------------------------------------
		8185B TPPollStop bits					(offset 0x93, 1 byte)
----------------------------------------------------------------------------
*/
#define TPPOLLSTOP_BQ			(0x01 << 7)
#define TPPOLLSTOP_AC_VIQ		(0x01 << 4)

#define MSR_LINK_ENEDCA	   (1<<4)

/*
----------------------------------------------------------------------------
		8187B AC_XX_PARAM bits
----------------------------------------------------------------------------
*/
#define AC_PARAM_TXOP_LIMIT_OFFSET		16
#define AC_PARAM_ECW_MAX_OFFSET			12
#define AC_PARAM_ECW_MIN_OFFSET			8
#define AC_PARAM_AIFS_OFFSET			0

/*
----------------------------------------------------------------------------
		8187B ACM_CONTROL bits					(Offset 0xBF, 1 Byte)
----------------------------------------------------------------------------
*/
#define VOQ_ACM_EN				(0x01 << 7)	/*BIT7	*/
#define VIQ_ACM_EN				(0x01 << 6)	/*BIT6	*/
#define BEQ_ACM_EN				(0x01 << 5)	/*BIT5	*/
#define ACM_HW_EN				(0x01 << 4)	/*BIT4	*/
#define VOQ_ACM_CTL				(0x01 << 2)	/*BIT2	*/	/* Set to 1 when AC_VO used time reaches or exceeds the admitted time	*/
#define VIQ_ACM_CTL				(0x01 << 1)	/*BIT1	*/	/* Set to 1 when AC_VI used time reaches or exceeds the admitted time	*/
#define BEQ_ACM_CTL				(0x01 << 0)	/*BIT0	*/	/* Set to 1 when AC_BE used time reaches or exceeds the admitted time	*/


/*
----------------------------------------------------------------------------
		8185B SW_3W_CMD bits					(Offset 0x27C-0x27D, 16bit)
----------------------------------------------------------------------------
*/
#define SW_3W_CMD0_HOLD		((1 << 7))
#define SW_3W_CMD1_RE		((1 << 0)) /* BIT8		*/
#define SW_3W_CMD1_WE		((1 << 1)) /* BIT9		*/
#define SW_3W_CMD1_DONE		((1 << 2)) /* BIT10		*/

#define BB_HOST_BANG_RW		(1 << 3)

/*
----------------------------------------------------------------------------
		8185B RATE_FALLBACK_CTL bits			(Offset 0xBE, 8bit)
----------------------------------------------------------------------------
*/
#define RATE_FALLBACK_CTL_ENABLE				((1 << 7))
#define RATE_FALLBACK_CTL_ENABLE_RTSCTS		((1 << 6))
/* Auto rate fallback per 2^n retry. */
#define RATE_FALLBACK_CTL_AUTO_STEP0	0x00
#define RATE_FALLBACK_CTL_AUTO_STEP1	0x01
#define RATE_FALLBACK_CTL_AUTO_STEP2	0x02
#define RATE_FALLBACK_CTL_AUTO_STEP3	0x03


#define RTL8225z2_ANAPARAM_OFF	0x55480658
#define RTL8225z2_ANAPARAM2_OFF	0x72003f70
/* by amy for power save		*/
#define RF_CHANGE_BY_HW BIT30
#define RF_CHANGE_BY_PS BIT29
#define RF_CHANGE_BY_IPS BIT28
/* by amy for power save		*/
/* by amy for antenna			*/
#define EEPROM_SW_REVD_OFFSET 0x3f

/*  BIT[8-9] is for SW Antenna Diversity. 
 *  Only the value EEPROM_SW_AD_ENABLE means enable, other values are disable.
 */
#define EEPROM_SW_AD_MASK			0x0300
#define EEPROM_SW_AD_ENABLE			0x0100

/* BIT[10-11] determine if Antenna 1 is the Default Antenna.
 * Only the value EEPROM_DEF_ANT_1 means TRUE, other values are FALSE.	
 */
#define EEPROM_DEF_ANT_MASK			0x0C00
#define EEPROM_DEF_ANT_1			0x0400
/*by amy for antenna																				*/
/* {by amy 080312																					*/
/* 0x7C, 0x7D Crystal calibration and Tx Power tracking mechanism. Added by Roger. 2007.12.10.		*/
#define EEPROM_RSV						0x7C
#define EEPROM_XTAL_CAL_XOUT_MASK	0x0F	/* 0x7C[3:0], Crystal calibration for Xout.				*/
#define EEPROM_XTAL_CAL_XIN_MASK		0xF0	/* 0x7C[7:4], Crystal calibration for Xin.			*/
#define EEPROM_THERMAL_METER_MASK	0x0F00	/* 0x7D[3:0], Thermal meter reference level.			*/
#define EEPROM_XTAL_CAL_ENABLE		0x1000	/* 0x7D[4], Crystal calibration enabled/disabled BIT.	*/
#define EEPROM_THERMAL_METER_ENABLE	0x2000	/* 0x7D[5], Thermal meter enabled/disabled BIT.			*/
#define EN_LPF_CAL			0x238	/* Enable LPF Calibration.										*/
#define PWR_METER_EN		BIT1
/* <RJ_TODO_8185B> where are false alarm counters in 8185B? */
#define CCK_FALSE_ALARM		0xD0
/* by amy 080312} */

/* YJ,add for Country IE, 080630 */
#define EEPROM_COUNTRY_CODE  0x2E
/* YJ,add,080630,end */

#endif
