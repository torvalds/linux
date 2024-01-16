/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * Contact Information: wlanfae <wlanfae@realtek.com>
 */
#ifndef R8180_HW
#define R8180_HW

enum baseband_config {
	BaseBand_Config_PHY_REG = 0,
	BaseBand_Config_AGC_TAB = 1,
};

#define	RTL8187_REQT_READ	0xc0
#define	RTL8187_REQT_WRITE	0x40
#define	RTL8187_REQ_GET_REGS	0x05
#define	RTL8187_REQ_SET_REGS	0x05

#define MAX_TX_URB 5
#define MAX_RX_URB 16
#define RX_URB_SIZE 9100

#define BB_ANTATTEN_CHAN14	0x0c
#define BB_ANTENNA_B 0x40

#define BB_HOST_BANG (1<<30)
#define BB_HOST_BANG_EN (1<<2)
#define BB_HOST_BANG_CLK (1<<1)
#define BB_HOST_BANG_RW (1<<3)
#define BB_HOST_BANG_DATA	 1

#define RTL8190_EEPROM_ID	0x8129
#define EEPROM_VID		0x02
#define EEPROM_DID		0x04
#define EEPROM_NODE_ADDRESS_BYTE_0	0x0C

#define EEPROM_TxPowerDiff	0x1F


#define EEPROM_PwDiff		0x21
#define EEPROM_CrystalCap	0x22



#define EEPROM_TxPwIndex_CCK_V1		0x29
#define EEPROM_TxPwIndex_OFDM_24G_V1	0x2C
#define EEPROM_TxPwIndex_Ver		0x27

#define EEPROM_Default_TxPowerDiff		0x0
#define EEPROM_Default_ThermalMeter		0x77
#define EEPROM_Default_AntTxPowerDiff		0x0
#define EEPROM_Default_TxPwDiff_CrystalCap	0x5
#define EEPROM_Default_PwDiff			0x4
#define EEPROM_Default_CrystalCap		0x5
#define EEPROM_Default_TxPower			0x1010
#define EEPROM_ICVersion_ChannelPlan	0x7C
#define EEPROM_Customer_ID			0x7B
#define EEPROM_RFInd_PowerDiff			0x28
#define EEPROM_ThermalMeter			0x29
#define EEPROM_TxPwDiff_CrystalCap		0x2A
#define EEPROM_TxPwIndex_CCK			0x2C
#define EEPROM_TxPwIndex_OFDM_24G	0x3A
#define EEPROM_Default_TxPowerLevel		0x10
#define EEPROM_IC_VER				0x7d
#define EEPROM_CRC				0x7e

#define EEPROM_CID_DEFAULT			0x0
#define EEPROM_CID_CAMEO				0x1
#define EEPROM_CID_RUNTOP				0x2
#define EEPROM_CID_Senao				0x3
#define EEPROM_CID_TOSHIBA				0x4
#define EEPROM_CID_NetCore				0x5
#define EEPROM_CID_Nettronix			0x6
#define EEPROM_CID_Pronet				0x7
#define EEPROM_CID_DLINK				0x8
#define EEPROM_CID_WHQL					0xFE
enum _RTL8192Pci_HW {
	MAC0			= 0x000,
	MAC1			= 0x001,
	MAC2			= 0x002,
	MAC3			= 0x003,
	MAC4			= 0x004,
	MAC5			= 0x005,
	PCIF			= 0x009,
#define MXDMA2_16bytes		0x000
#define MXDMA2_32bytes		0x001
#define MXDMA2_64bytes		0x010
#define MXDMA2_128bytes		0x011
#define MXDMA2_256bytes		0x100
#define MXDMA2_512bytes		0x101
#define MXDMA2_1024bytes	0x110
#define MXDMA2_NoLimit		0x7

#define	MULRW_SHIFT		3
#define	MXDMA2_RX_SHIFT		4
#define	MXDMA2_TX_SHIFT		0
	PMR			= 0x00c,
	EPROM_CMD		= 0x00e,
#define EPROM_CMD_RESERVED_MASK BIT5
#define EPROM_CMD_9356SEL	BIT4
#define EPROM_CMD_OPERATING_MODE_SHIFT 6
#define EPROM_CMD_OPERATING_MODE_MASK ((1<<7)|(1<<6))
#define EPROM_CMD_CONFIG 0x3
#define EPROM_CMD_NORMAL 0
#define EPROM_CMD_LOAD 1
#define EPROM_CMD_PROGRAM 2
#define EPROM_CS_BIT 3
#define EPROM_CK_BIT 2
#define EPROM_W_BIT 1
#define EPROM_R_BIT 0

	AFR			 = 0x010,
#define AFR_CardBEn		(1<<0)
#define AFR_CLKRUN_SEL		(1<<1)
#define AFR_FuncRegEn		(1<<2)

	ANAPAR			= 0x17,
#define	BB_GLOBAL_RESET_BIT	0x1
	BB_GLOBAL_RESET		= 0x020,
	BSSIDR			= 0x02E,
	CMDR			= 0x037,
#define		CR_RST					0x10
#define		CR_RE					0x08
#define		CR_TE					0x04
#define		CR_MulRW				0x01
	SIFS		= 0x03E,
	TCR			= 0x040,
	RCR			= 0x044,
#define RCR_FILTER_MASK (BIT0 | BIT1 | BIT2 | BIT3 | BIT5 | BIT12 |	\
			BIT18 | BIT19 | BIT20 | BIT21 | BIT22 | BIT23)
#define RCR_ONLYERLPKT		BIT31
#define RCR_ENCS2		BIT30
#define RCR_ENCS1		BIT29
#define RCR_ENMBID		BIT27
#define RCR_ACKTXBW		(BIT24|BIT25)
#define RCR_CBSSID		BIT23
#define RCR_APWRMGT		BIT22
#define	RCR_ADD3		BIT21
#define RCR_AMF			BIT20
#define RCR_ACF			BIT19
#define RCR_ADF			BIT18
#define RCR_RXFTH		BIT13
#define RCR_AICV		BIT12
#define	RCR_ACRC32		BIT5
#define	RCR_AB			BIT3
#define	RCR_AM			BIT2
#define	RCR_APM			BIT1
#define	RCR_AAP			BIT0
#define RCR_MXDMA_OFFSET	8
#define RCR_FIFO_OFFSET		13
	SLOT_TIME		= 0x049,
	ACK_TIMEOUT		= 0x04c,
	PIFS_TIME		= 0x04d,
	USTIME			= 0x04e,
	EDCAPARA_BE		= 0x050,
	EDCAPARA_BK		= 0x054,
	EDCAPARA_VO		= 0x058,
	EDCAPARA_VI		= 0x05C,
#define	AC_PARAM_TXOP_LIMIT_OFFSET		16
#define	AC_PARAM_ECW_MAX_OFFSET		12
#define	AC_PARAM_ECW_MIN_OFFSET			8
#define	AC_PARAM_AIFS_OFFSET				0
	RFPC			= 0x05F,
	CWRR			= 0x060,
	BCN_TCFG		= 0x062,
#define BCN_TCFG_CW_SHIFT		8
#define BCN_TCFG_IFS			0
	BCN_INTERVAL		= 0x070,
	ATIMWND			= 0x072,
	BCN_DRV_EARLY_INT	= 0x074,
#define	BCN_DRV_EARLY_INT_SWBCN_SHIFT	8
#define	BCN_DRV_EARLY_INT_TIME_SHIFT	0
	BCN_DMATIME		= 0x076,
	BCN_ERR_THRESH		= 0x078,
	RWCAM			= 0x0A0,
#define   CAM_CM_SecCAMPolling		BIT31
#define   CAM_CM_SecCAMClr			BIT30
#define   CAM_CM_SecCAMWE			BIT16
#define   CAM_VALID			       BIT15
#define   CAM_NOTVALID			0x0000
#define   CAM_USEDK				BIT5

#define   CAM_NONE				0x0
#define   CAM_WEP40				0x01
#define   CAM_TKIP				0x02
#define   CAM_AES				0x04
#define   CAM_WEP104			0x05

#define   TOTAL_CAM_ENTRY				32

#define   CAM_CONFIG_USEDK	true
#define   CAM_CONFIG_NO_USEDK	false
#define   CAM_WRITE		BIT16
#define   CAM_READ		0x00000000
#define   CAM_POLLINIG		BIT31
#define   SCR_UseDK		0x01
	WCAMI			= 0x0A4,
	RCAMO			= 0x0A8,
	SECR			= 0x0B0,
#define	SCR_TxUseDK			BIT0
#define   SCR_RxUseDK			BIT1
#define   SCR_TxEncEnable		BIT2
#define   SCR_RxDecEnable		BIT3
#define   SCR_SKByA2				BIT4
#define   SCR_NoSKMC				BIT5
	SWREGULATOR	= 0x0BD,
	INTA_MASK		= 0x0f4,
#define IMR8190_DISABLED		0x0
#define IMR_ATIMEND			BIT28
#define IMR_TBDOK			BIT27
#define IMR_TBDER			BIT26
#define IMR_TXFOVW			BIT15
#define IMR_TIMEOUT0			BIT14
#define IMR_BcnInt			BIT13
#define	IMR_RXFOVW			BIT12
#define IMR_RDU				BIT11
#define IMR_RXCMDOK			BIT10
#define IMR_BDOK			BIT9
#define IMR_HIGHDOK			BIT8
#define	IMR_COMDOK			BIT7
#define IMR_MGNTDOK			BIT6
#define IMR_HCCADOK			BIT5
#define	IMR_BKDOK			BIT4
#define	IMR_BEDOK			BIT3
#define	IMR_VIDOK			BIT2
#define	IMR_VODOK			BIT1
#define	IMR_ROK				BIT0
	ISR			= 0x0f8,
	TPPoll			= 0x0fd,
#define TPPoll_BKQ		BIT0
#define TPPoll_BEQ		BIT1
#define TPPoll_VIQ		BIT2
#define TPPoll_VOQ		BIT3
#define TPPoll_BQ		BIT4
#define TPPoll_CQ		BIT5
#define TPPoll_MQ		BIT6
#define TPPoll_HQ		BIT7
#define TPPoll_HCCAQ		BIT8
#define TPPoll_StopBK	BIT9
#define TPPoll_StopBE	BIT10
#define TPPoll_StopVI		BIT11
#define TPPoll_StopVO	BIT12
#define TPPoll_StopMgt	BIT13
#define TPPoll_StopHigh	BIT14
#define TPPoll_StopHCCA	BIT15
#define TPPoll_SHIFT		8

	PSR			= 0x0ff,
#define PSR_GEN			0x0
#define PSR_CPU			0x1
	CPU_GEN			= 0x100,
	BB_RESET			= 0x101,
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
#define	CPU_GEN_GPIO_UART		0x00007000

	LED1Cfg			= 0x154,
	LED0Cfg			= 0x155,

	AcmAvg			= 0x170,
	AcmHwCtrl		= 0x171,
#define	AcmHw_HwEn		BIT0
#define	AcmHw_BeqEn		BIT1
#define	AcmHw_ViqEn		BIT2
#define	AcmHw_VoqEn		BIT3
#define	AcmHw_BeqStatus		BIT4
#define	AcmHw_ViqStatus		BIT5
#define	AcmHw_VoqStatus		BIT6
	AcmFwCtrl		= 0x172,
#define	AcmFw_BeqStatus		BIT0
#define	AcmFw_ViqStatus		BIT1
#define	AcmFw_VoqStatus		BIT2
	VOAdmTime		= 0x174,
	VIAdmTime		= 0x178,
	BEAdmTime		= 0x17C,
	RQPN1			= 0x180,
	RQPN2			= 0x184,
	RQPN3			= 0x188,
	QPRR			= 0x1E0,
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
	RCQDA			= 0x224,
	RDQDA			= 0x228,

	MAR0			= 0x240,
	MAR4			= 0x244,

	CCX_PERIOD		= 0x250,
	CLM_RESULT		= 0x251,
	NHM_PERIOD		= 0x252,

	NHM_THRESHOLD0		= 0x253,
	NHM_THRESHOLD1		= 0x254,
	NHM_THRESHOLD2		= 0x255,
	NHM_THRESHOLD3		= 0x256,
	NHM_THRESHOLD4		= 0x257,
	NHM_THRESHOLD5		= 0x258,
	NHM_THRESHOLD6		= 0x259,

	MCTRL			= 0x25A,

	NHM_RPI_COUNTER0	= 0x264,
	NHM_RPI_COUNTER1	= 0x265,
	NHM_RPI_COUNTER2	= 0x266,
	NHM_RPI_COUNTER3	= 0x267,
	NHM_RPI_COUNTER4	= 0x268,
	NHM_RPI_COUNTER5	= 0x269,
	NHM_RPI_COUNTER6	= 0x26A,
	NHM_RPI_COUNTER7	= 0x26B,
	WFCRC0		  = 0x2f0,
	WFCRC1		  = 0x2f4,
	WFCRC2		  = 0x2f8,

	BW_OPMODE		= 0x300,
#define	BW_OPMODE_11J			BIT0
#define	BW_OPMODE_5G			BIT1
#define	BW_OPMODE_20MHZ			BIT2
	IC_VERRSION		= 0x301,
	MSR			= 0x303,
#define MSR_LINK_MASK      ((1<<0)|(1<<1))
#define MSR_LINK_MANAGED   2
#define MSR_LINK_NONE      0
#define MSR_LINK_SHIFT     0
#define MSR_LINK_ADHOC     1
#define MSR_LINK_MASTER    3
#define MSR_LINK_ENEDCA	   (1<<4)

#define	MSR_NOLINK					0x00
#define	MSR_ADHOC					0x01
#define	MSR_INFRA					0x02
#define	MSR_AP						0x03

	RETRY_LIMIT		= 0x304,
#define RETRY_LIMIT_SHORT_SHIFT 8
#define RETRY_LIMIT_LONG_SHIFT 0
	TSFR			= 0x308,
	RRSR			= 0x310,
#define RRSR_RSC_OFFSET				21
#define RRSR_SHORT_OFFSET			23
#define RRSR_RSC_DUPLICATE			0x600000
#define RRSR_RSC_UPSUBCHNL			0x400000
#define RRSR_RSC_LOWSUBCHNL			0x200000
#define RRSR_SHORT				0x800000
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
#define BRSR_AckShortPmb			BIT23
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
	MacBlkCtrl		= 0x403,

}
;

#define GPI 0x108
#define GPO 0x109
#define GPE 0x10a

#define	 HWSET_MAX_SIZE_92S				128

#define	ANAPAR_FOR_8192PciE				0x17

#endif
