/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * Contact Information: wlanfae <wlanfae@realtek.com>
 */
#ifndef R8180_HW
#define R8180_HW

enum baseband_config {
	BB_CONFIG_PHY_REG = 0,
	BB_CONFIG_AGC_TAB = 1,
};

#define RTL8190_EEPROM_ID	0x8129
#define EEPROM_VID		0x02
#define EEPROM_DID		0x04
#define EEPROM_NODE_ADDRESS_BYTE_0	0x0C

#define EEPROM_Default_ThermalMeter		0x77
#define EEPROM_Default_AntTxPowerDiff		0x0
#define EEPROM_Default_TxPwDiff_CrystalCap	0x5
#define EEPROM_Default_TxPower			0x1010
#define EEPROM_ICVersion_ChannelPlan	0x7C
#define EEPROM_Customer_ID			0x7B
#define EEPROM_RFInd_PowerDiff			0x28

#define EEPROM_ThermalMeter			0x29
#define EEPROM_TxPwDiff_CrystalCap		0x2A
#define EEPROM_TxPwIndex_CCK			0x2C
#define EEPROM_TxPwIndex_OFDM_24G	0x3A

#define EEPROM_CID_TOSHIBA				0x4
#define EEPROM_CID_NetCore				0x5
enum _RTL8192PCI_HW {
	MAC0			= 0x000,
	MAC4			= 0x004,
	PCIF			= 0x009,
#define MXDMA2_NO_LIMIT		0x7

#define	MXDMA2_RX_SHIFT		4
#define	MXDMA2_TX_SHIFT		0
	PMR			= 0x00c,
	EPROM_CMD		= 0x00e,

#define EPROM_CMD_9356SEL	BIT(4)
#define EPROM_CMD_OPERATING_MODE_SHIFT 6
#define EPROM_CMD_NORMAL 0
#define EPROM_CMD_PROGRAM 2
#define EPROM_CS_BIT 3
#define EPROM_CK_BIT 2
#define EPROM_W_BIT 1
#define EPROM_R_BIT 0

	ANAPAR			= 0x17,
#define	BB_GLOBAL_RESET_BIT	0x1
	BB_GLOBAL_RESET		= 0x020,
	BSSIDR			= 0x02E,
	CMDR			= 0x037,
#define		CR_RE					0x08
#define		CR_TE					0x04
	SIFS		= 0x03E,
	RCR			= 0x044,
#define RCR_ONLYERLPKT		BIT(31)
#define RCR_CBSSID		BIT(23)
#define	RCR_ADD3		BIT(21)
#define RCR_AMF			BIT(20)
#define RCR_ADF			BIT(18)
#define RCR_AICV		BIT(12)
#define	RCR_AB			BIT(3)
#define	RCR_AM			BIT(2)
#define	RCR_APM			BIT(1)
#define	RCR_AAP			BIT(0)
#define RCR_MXDMA_OFFSET	8
#define RCR_FIFO_OFFSET		13
	SLOT_TIME		= 0x049,
	ACK_TIMEOUT		= 0x04c,
	EDCAPARA_BE		= 0x050,
	EDCAPARA_BK		= 0x054,
	EDCAPARA_VO		= 0x058,
	EDCAPARA_VI		= 0x05C,
#define	AC_PARAM_TXOP_LIMIT_OFFSET		16
#define	AC_PARAM_ECW_MAX_OFFSET		12
#define	AC_PARAM_ECW_MIN_OFFSET			8
#define	AC_PARAM_AIFS_OFFSET				0
	BCN_TCFG		= 0x062,
#define BCN_TCFG_CW_SHIFT		8
#define BCN_TCFG_IFS			0
	BCN_INTERVAL		= 0x070,
	ATIMWND			= 0x072,
	BCN_DRV_EARLY_INT	= 0x074,
	BCN_DMATIME		= 0x076,
	BCN_ERR_THRESH		= 0x078,
	RWCAM			= 0x0A0,
#define   TOTAL_CAM_ENTRY				32
	WCAMI			= 0x0A4,
	SECR			= 0x0B0,
#define	SCR_TxUseDK			BIT(0)
#define   SCR_RxUseDK			BIT(1)
#define   SCR_TxEncEnable		BIT(2)
#define   SCR_RxDecEnable		BIT(3)
#define   SCR_NoSKMC				BIT(5)
	SWREGULATOR	= 0x0BD,
	INTA_MASK		= 0x0f4,
#define IMR_TBDOK			BIT(27)
#define IMR_TBDER			BIT(26)
#define IMR_TXFOVW			BIT(15)
#define IMR_TIMEOUT0			BIT(14)
#define IMR_BcnInt			BIT(13)
#define	IMR_RXFOVW			BIT(12)
#define IMR_RDU				BIT(11)
#define IMR_RXCMDOK			BIT(10)
#define IMR_BDOK			BIT(9)
#define IMR_HIGHDOK			BIT(8)
#define	IMR_COMDOK			BIT(7)
#define IMR_MGNTDOK			BIT(6)
#define IMR_HCCADOK			BIT(5)
#define	IMR_BKDOK			BIT(4)
#define	IMR_BEDOK			BIT(3)
#define	IMR_VIDOK			BIT(2)
#define	IMR_VODOK			BIT(1)
#define	IMR_ROK				BIT(0)
	ISR			= 0x0f8,
	TP_POLL			= 0x0fd,
#define TP_POLL_CQ		BIT(5)
	PSR			= 0x0ff,
	CPU_GEN			= 0x100,
#define	CPU_CCK_LOOPBACK	0x00030000
#define	CPU_GEN_SYSTEM_RESET	0x00000001
#define	CPU_GEN_FIRMWARE_RESET	0x00000008
#define	CPU_GEN_BOOT_RDY	0x00000010
#define	CPU_GEN_FIRM_RDY	0x00000020
#define	CPU_GEN_PUT_CODE_OK	0x00000080
#define	CPU_GEN_BB_RST		0x00000100
#define	CPU_GEN_PWR_STB_CPU	0x00000004
#define CPU_GEN_NO_LOOPBACK_MSK	0xFFF8FFFF
#define CPU_GEN_NO_LOOPBACK_SET	0x00080000
	ACM_HW_CTRL		= 0x171,
#define	ACM_HW_BEQ_EN		BIT(1)
#define	ACM_HW_VIQ_EN		BIT(2)
#define	ACM_HW_VOQ_EN		BIT(3)
	RQPN1			= 0x180,
	RQPN2			= 0x184,
	RQPN3			= 0x188,
	QPNR			= 0x1F0,
	BQDA			= 0x200,
	HQDA			= 0x204,
	CQDA			= 0x208,
	MQDA			= 0x20C,
	HCCAQDA			= 0x210,
	VOQDA			= 0x214,
	VIQDA			= 0x218,
	BEQDA			= 0x21C,
	BKQDA			= 0x220,
	RDQDA			= 0x228,

	WFCRC0		  = 0x2f0,
	WFCRC1		  = 0x2f4,
	WFCRC2		  = 0x2f8,

	BW_OPMODE		= 0x300,
#define	BW_OPMODE_20MHZ			BIT(2)
	IC_VERRSION		= 0x301,
	MSR			= 0x303,
#define MSR_LINK_MASK		(BIT(1) | BIT(0))
#define MSR_LINK_MANAGED   2
#define MSR_LINK_ADHOC     1
#define MSR_LINK_MASTER    3

#define	MSR_NOLINK					0x00
#define	MSR_ADHOC					0x01
#define	MSR_INFRA					0x02
#define	MSR_AP						0x03

	RETRY_LIMIT		= 0x304,
#define RETRY_LIMIT_SHORT_SHIFT 8
#define RETRY_LIMIT_LONG_SHIFT 0
	TSFR			= 0x308,
	RRSR			= 0x310,
#define RRSR_SHORT_OFFSET			23
#define RRSR_1M					BIT(0)
#define RRSR_2M					BIT(1)
#define RRSR_5_5M				BIT(2)
#define RRSR_11M				BIT(3)
#define RRSR_6M					BIT(4)
#define RRSR_9M					BIT(5)
#define RRSR_12M				BIT(6)
#define RRSR_18M				BIT(7)
#define RRSR_24M				BIT(8)
#define RRSR_36M				BIT(9)
#define RRSR_48M				BIT(10)
#define RRSR_54M				BIT(11)
#define BRSR_AckShortPmb			BIT(23)
	UFWP			= 0x318,
	RATR0			= 0x320,
#define	RATR_1M			0x00000001
#define	RATR_2M			0x00000002
#define	RATR_55M		0x00000004
#define	RATR_11M		0x00000008
#define	RATR_6M			0x00000010
#define	RATR_9M			0x00000020
#define	RATR_12M		0x00000040
#define	RATR_18M		0x00000080
#define	RATR_24M		0x00000100
#define	RATR_36M		0x00000200
#define	RATR_48M		0x00000400
#define	RATR_54M		0x00000800
#define	RATR_MCS0		0x00001000
#define	RATR_MCS1		0x00002000
#define	RATR_MCS2		0x00004000
#define	RATR_MCS3		0x00008000
#define	RATR_MCS4		0x00010000
#define	RATR_MCS5		0x00020000
#define	RATR_MCS6		0x00040000
#define	RATR_MCS7		0x00080000
#define	RATR_MCS8		0x00100000
#define	RATR_MCS9		0x00200000
#define	RATR_MCS10		0x00400000
#define	RATR_MCS11		0x00800000
#define	RATR_MCS12		0x01000000
#define	RATR_MCS13		0x02000000
#define	RATR_MCS14		0x04000000
#define	RATR_MCS15		0x08000000
#define RATE_ALL_CCK		(RATR_1M | RATR_2M | RATR_55M | RATR_11M)
#define RATE_ALL_OFDM_AG	(RATR_6M | RATR_9M | RATR_12M | RATR_18M | \
				RATR_24M | RATR_36M | RATR_48M | RATR_54M)
#define RATE_ALL_OFDM_1SS	(RATR_MCS0 | RATR_MCS1 | RATR_MCS2 |	\
				RATR_MCS3 | RATR_MCS4 | RATR_MCS5 |	\
				RATR_MCS6 | RATR_MCS7)
#define RATE_ALL_OFDM_2SS	(RATR_MCS8 | RATR_MCS9 | RATR_MCS10 |	\
				RATR_MCS11 | RATR_MCS12 | RATR_MCS13 |	\
				RATR_MCS14|RATR_MCS15)

	DRIVER_RSSI		= 0x32c,
	MCS_TXAGC		= 0x340,
	CCK_TXAGC		= 0x348,
	MAC_BLK_CTRL		= 0x403,
};

#define GPI 0x108

#define	ANAPAR_FOR_8192PCIE	0x17

#endif
