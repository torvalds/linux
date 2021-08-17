/* SPDX-License-Identifier: GPL-2.0 */
/*
 *	This is part of rtl8187 OpenSource driver.
 *	Copyright (C) Andrea Merello 2004-2005  <andrea.merello@gmail.com>
 *
 *	Parts of this driver are based on the GPL part of the
 *	official Realtek driver.
 *	Parts of this driver are based on the rtl8180 driver skeleton
 *	from Patric Schenke & Andres Salomon.
 *	Parts of this driver are based on the Intel Pro Wireless
 *	2100 GPL driver.
 *
 *	We want to thank the Authors of those projects
 *	and the Ndiswrapper project Authors.
 */

/* Mariusz Matuszek added full registers definition with Realtek's name */

/* this file contains register definitions for the rtl8187 MAC controller */
#ifndef R8192_HW
#define R8192_HW

#define	RTL8187_REQT_READ	0xc0
#define	RTL8187_REQT_WRITE	0x40
#define	RTL8187_REQ_GET_REGS	0x05
#define	RTL8187_REQ_SET_REGS	0x05

#define MAX_TX_URB 5
#define MAX_RX_URB 16

#define R8180_MAX_RETRY 255

#define RX_URB_SIZE 9100

#define RTL8190_EEPROM_ID	0x8129
#define EEPROM_VID		0x02
#define EEPROM_PID		0x04
#define EEPROM_NODE_ADDRESS_BYTE_0	0x0C

#define EEPROM_TX_POWER_DIFF	0x1F
#define EEPROM_THERMAL_METER	0x20
#define EEPROM_PW_DIFF		0x21	//0x21
#define EEPROM_CRYSTAL_CAP	0x22	//0x22

#define EEPROM_TX_PW_INDEX_CCK	0x23	//0x23
#define EEPROM_TX_PW_INDEX_OFDM_24G	0x24	//0x24~0x26
#define EEPROM_TX_PW_INDEX_CCK_V1	0x29	//0x29~0x2B
#define EEPROM_TX_PW_INDEX_OFDM_24G_V1	0x2C	//0x2C~0x2E
#define EEPROM_TX_PW_INDEX_VER		0x27	//0x27

#define EEPROM_DEFAULT_THERNAL_METER		0x7
#define EEPROM_DEFAULT_PW_DIFF			0x4
#define EEPROM_DEFAULT_CRYSTAL_CAP		0x5
#define EEPROM_DEFAULT_TX_POWER		0x1010
#define EEPROM_CUSTOMER_ID			0x7B	//0x7B:CustomerID
#define EEPROM_CHANNEL_PLAN			0x16	//0x7C

#define EEPROM_CID_RUNTOP				0x2
#define EEPROM_CID_DLINK				0x8

#define AC_PARAM_TXOP_LIMIT_OFFSET	16
#define AC_PARAM_ECW_MAX_OFFSET		12
#define AC_PARAM_ECW_MIN_OFFSET		8
#define AC_PARAM_AIFS_OFFSET		0

//#endif
enum _RTL8192Usb_HW {
	MAC0			= 0x000,
	MAC4			= 0x004,

#define	BB_GLOBAL_RESET_BIT	0x1
	BB_GLOBAL_RESET		= 0x020, // BasebandGlobal Reset Register
	BSSIDR			= 0x02E, // BSSID Register
	CMDR			= 0x037, // Command register
#define CR_RE			0x08
#define CR_TE			0x04
	SIFS			= 0x03E, // SIFS register

#define TCR_MXDMA_2048		7
#define TCR_LRL_OFFSET		0
#define TCR_SRL_OFFSET		8
#define TCR_MXDMA_OFFSET	21
#define TCR_SAT			BIT(24)	// Enable Rate depedent ack timeout timer
	RCR			= 0x044, // Receive Configuration Register
#define MAC_FILTER_MASK (BIT(0) | BIT(1) | BIT(2) | BIT(3) | BIT(5) | \
			 BIT(12) | BIT(18) | BIT(19) | BIT(20) | BIT(21) | \
			 BIT(22) | BIT(23))
#define RX_FIFO_THRESHOLD_MASK (BIT(13) | BIT(14) | BIT(15))
#define RX_FIFO_THRESHOLD_SHIFT 13
#define RX_FIFO_THRESHOLD_NONE 7
#define MAX_RX_DMA_MASK	(BIT(8) | BIT(9) | BIT(10))
#define RCR_MXDMA_OFFSET	8
#define RCR_FIFO_OFFSET		13
#define RCR_ONLYERLPKT		BIT(31)			// Early Receiving based on Packet Size.
#define RCR_CBSSID		BIT(23)			// Accept BSSID match packet
#define RCR_APWRMGT		BIT(22)			// Accept power management packet
#define RCR_AMF			BIT(20)			// Accept management type frame
#define RCR_ACF			BIT(19)			// Accept control type frame
#define RCR_ADF			BIT(18)			// Accept data type frame
#define RCR_AICV		BIT(12)			// Accept ICV error packet
#define	RCR_ACRC32		BIT(5)			// Accept CRC32 error packet
#define	RCR_AB			BIT(3)			// Accept broadcast packet
#define	RCR_AM			BIT(2)			// Accept multicast packet
#define	RCR_APM			BIT(1)			// Accept physical match packet
#define	RCR_AAP			BIT(0)			// Accept all unicast packet
	SLOT_TIME		= 0x049, // Slot Time Register
	ACK_TIMEOUT		= 0x04c, // Ack Timeout Register
	EDCAPARA_BE		= 0x050, // EDCA Parameter of AC BE
	EDCAPARA_BK		= 0x054, // EDCA Parameter of AC BK
	EDCAPARA_VO		= 0x058, // EDCA Parameter of AC VO
	EDCAPARA_VI		= 0x05C, // EDCA Parameter of AC VI
	BCN_TCFG		= 0x062, // Beacon Time Configuration
#define BCN_TCFG_CW_SHIFT		8
#define BCN_TCFG_IFS			0
	BCN_INTERVAL		= 0x070, // Beacon Interval (TU)
	ATIMWND			= 0x072, // ATIM Window Size (TU)
	BCN_DRV_EARLY_INT	= 0x074, // Driver Early Interrupt Time (TU). Time to send interrupt to notify to change beacon content before TBTT
	BCN_DMATIME		= 0x076, // Beacon DMA and ATIM interrupt time (US). Indicates the time before TBTT to perform beacon queue DMA
	BCN_ERR_THRESH		= 0x078, // Beacon Error Threshold
	RWCAM			= 0x0A0, //IN 8190 Data Sheet is called CAMcmd
	WCAMI			= 0x0A4, // Software write CAM input content
	SECR			= 0x0B0, //Security Configuration Register
#define	SCR_TxUseDK		BIT(0)			//Force Tx Use Default Key
#define SCR_RxUseDK		BIT(1)			//Force Rx Use Default Key
#define SCR_TxEncEnable		BIT(2)			//Enable Tx Encryption
#define SCR_RxDecEnable		BIT(3)			//Enable Rx Decryption
#define SCR_SKByA2		BIT(4)			//Search kEY BY A2
#define SCR_NoSKMC		BIT(5)			//No Key Search for Multicast

//----------------------------------------------------------------------------
//       8190 CPU General Register		(offset 0x100, 4 byte)
//----------------------------------------------------------------------------
#define	CPU_CCK_LOOPBACK	0x00030000
#define	CPU_GEN_SYSTEM_RESET	0x00000001
#define	CPU_GEN_FIRMWARE_RESET	0x00000008
#define	CPU_GEN_BOOT_RDY	0x00000010
#define	CPU_GEN_FIRM_RDY	0x00000020
#define	CPU_GEN_PUT_CODE_OK	0x00000080
#define	CPU_GEN_BB_RST		0x00000100
#define	CPU_GEN_PWR_STB_CPU	0x00000004
#define CPU_GEN_NO_LOOPBACK_MSK	0xFFF8FFFF // Set bit18,17,16 to 0. Set bit19
#define CPU_GEN_NO_LOOPBACK_SET	0x00080000 // Set BIT19 to 1
	CPU_GEN			= 0x100, // CPU Reset Register

	AcmHwCtrl		= 0x171, // ACM Hardware Control Register
//----------------------------------------------------------------------------
////
////       8190 AcmHwCtrl bits                                    (offset 0x171, 1 byte)
////----------------------------------------------------------------------------
//
#define AcmHw_BeqEn             BIT(1)

	RQPN1			= 0x180, // Reserved Queue Page Number , Vo Vi, Be, Bk
	RQPN2			= 0x184, // Reserved Queue Page Number, HCCA, Cmd, Mgnt, High
	RQPN3			= 0x188, // Reserved Queue Page Number, Bcn, Public,
	QPNR			= 0x1D0, //0x1F0, // Queue Packet Number report per TID

#define	BW_OPMODE_5G			BIT(1)
#define	BW_OPMODE_20MHZ			BIT(2)
	BW_OPMODE		= 0x300, // Bandwidth operation mode
	MSR			= 0x303, // Media Status register
#define MSR_LINK_MASK      (BIT(0) | BIT(1))
#define MSR_LINK_MANAGED   2
#define MSR_LINK_NONE      0
#define MSR_LINK_SHIFT     0
#define MSR_LINK_ADHOC     1
#define MSR_LINK_MASTER    3
	RETRY_LIMIT		= 0x304, // Retry Limit [15:8]-short, [7:0]-long
#define RETRY_LIMIT_SHORT_SHIFT 8
#define RETRY_LIMIT_LONG_SHIFT 0
	RRSR			= 0x310, // Response Rate Set
#define RRSR_1M						BIT(0)
#define RRSR_2M						BIT(1)
#define RRSR_5_5M					BIT(2)
#define RRSR_11M					BIT(3)
#define RRSR_6M						BIT(4)
#define RRSR_9M						BIT(5)
#define RRSR_12M					BIT(6)
#define RRSR_18M					BIT(7)
#define RRSR_24M					BIT(8)
#define RRSR_36M					BIT(9)
#define RRSR_48M					BIT(10)
#define RRSR_54M					BIT(11)
#define BRSR_AckShortPmb			BIT(23)		// CCK ACK: use Short Preamble or not.
	UFWP			= 0x318,
	RATR0			= 0x320, // Rate Adaptive Table register1
	DRIVER_RSSI		= 0x32c,					// Driver tell Firmware current RSSI
//----------------------------------------------------------------------------
//       8190 Rate Adaptive Table Register	(offset 0x320, 4 byte)
//----------------------------------------------------------------------------
//CCK
#define	RATR_1M			0x00000001
#define	RATR_2M			0x00000002
#define	RATR_55M		0x00000004
#define	RATR_11M		0x00000008
//OFDM
#define	RATR_6M			0x00000010
#define	RATR_9M			0x00000020
#define	RATR_12M		0x00000040
#define	RATR_18M		0x00000080
#define	RATR_24M		0x00000100
#define	RATR_36M		0x00000200
#define	RATR_48M		0x00000400
#define	RATR_54M		0x00000800
//MCS 1 Spatial Stream
#define	RATR_MCS0		0x00001000
#define	RATR_MCS1		0x00002000
#define	RATR_MCS2		0x00004000
#define	RATR_MCS3		0x00008000
#define	RATR_MCS4		0x00010000
#define	RATR_MCS5		0x00020000
#define	RATR_MCS6		0x00040000
#define	RATR_MCS7		0x00080000
//MCS 2 Spatial Stream
#define	RATR_MCS8		0x00100000
#define	RATR_MCS9		0x00200000
#define	RATR_MCS10		0x00400000
#define	RATR_MCS11		0x00800000
#define	RATR_MCS12		0x01000000
#define	RATR_MCS13		0x02000000
#define	RATR_MCS14		0x04000000
#define	RATR_MCS15		0x08000000
// ALL CCK Rate
#define RATE_ALL_CCK		(RATR_1M | RATR_2M | RATR_55M | RATR_11M)
#define RATE_ALL_OFDM_AG	(RATR_6M | RATR_9M | RATR_12M | RATR_18M |\
				 RATR_24M | RATR_36M | RATR_48M | RATR_54M)
#define RATE_ALL_OFDM_1SS	(RATR_MCS0 | RATR_MCS1 | RATR_MCS2 | RATR_MCS3 |\
				 RATR_MCS4 | RATR_MCS5 | RATR_MCS6 | RATR_MCS7)
#define RATE_ALL_OFDM_2SS	(RATR_MCS8 | RATR_MCS9 | RATR_MCS10 | RATR_MCS11 |\
				 RATR_MCS12 | RATR_MCS13 | RATR_MCS14 | RATR_MCS15)
	EPROM_CMD		= 0xfe58,
#define Cmd9346CR_9356SEL	BIT(4)
#define EPROM_CMD_OPERATING_MODE_SHIFT 6
#define EPROM_CMD_NORMAL 0
#define EPROM_CMD_PROGRAM 2
#define EPROM_CS_BIT BIT(3)
#define EPROM_CK_BIT BIT(2)
#define EPROM_W_BIT  BIT(1)
#define EPROM_R_BIT  BIT(0)
};

//----------------------------------------------------------------------------
//       818xB AnaParm & AnaParm2 Register
//----------------------------------------------------------------------------
#define GPI 0x108
#endif
