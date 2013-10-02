/*
	This is part of rtl8187 OpenSource driver.
	Copyright (C) Andrea Merello 2004-2005  <andrea.merello@gmail.com>
	Released under the terms of GPL (General Public Licence)

	Parts of this driver are based on the GPL part of the
	official Realtek driver.
	Parts of this driver are based on the rtl8180 driver skeleton
	from Patric Schenke & Andres Salomon.
	Parts of this driver are based on the Intel Pro Wireless
	2100 GPL driver.

	We want to thank the Authors of those projects
	and the Ndiswrapper project Authors.
*/

/* Mariusz Matuszek added full registers definition with Realtek's name */

/* this file contains register definitions for the rtl8187 MAC controller */
#ifndef R8192_HW
#define R8192_HW

typedef enum _VERSION_819xU{
	VERSION_819xU_A, // A-cut
	VERSION_819xU_B, // B-cut
	VERSION_819xU_C,// C-cut
}VERSION_819xU,*PVERSION_819xU;
//added for different RF type
typedef enum _RT_RF_TYPE_DEF
{
	RF_1T2R = 0,
	RF_2T4R,

	RF_819X_MAX_TYPE
}RT_RF_TYPE_DEF;


typedef enum _BaseBand_Config_Type{
	BaseBand_Config_PHY_REG = 0,			//Radio Path A
	BaseBand_Config_AGC_TAB = 1,			//Radio Path B
}BaseBand_Config_Type, *PBaseBand_Config_Type;
#define	RTL8187_REQT_READ	0xc0
#define	RTL8187_REQT_WRITE	0x40
#define	RTL8187_REQ_GET_REGS	0x05
#define	RTL8187_REQ_SET_REGS	0x05

#define MAX_TX_URB 5
#define MAX_RX_URB 16

#define R8180_MAX_RETRY 255
//#define MAX_RX_NORMAL_URB 3
//#define MAX_RX_COMMAND_URB 2
#define RX_URB_SIZE 9100

#define BB_ANTATTEN_CHAN14	0x0c
#define BB_ANTENNA_B 0x40

#define BB_HOST_BANG (1<<30)
#define BB_HOST_BANG_EN (1<<2)
#define BB_HOST_BANG_CLK (1<<1)
#define BB_HOST_BANG_RW (1<<3)
#define BB_HOST_BANG_DATA	 1

//#if (RTL819X_FPGA_VER & RTL819X_FPGA_VIVI_070920)
#define AFR			0x010
#define AFR_CardBEn		(1<<0)
#define AFR_CLKRUN_SEL		(1<<1)
#define AFR_FuncRegEn		(1<<2)
#define RTL8190_EEPROM_ID	0x8129
#define EEPROM_VID		0x02
#define EEPROM_PID		0x04
#define EEPROM_NODE_ADDRESS_BYTE_0	0x0C

#define EEPROM_TxPowerDiff	0x1F
#define EEPROM_ThermalMeter	0x20
#define EEPROM_PwDiff		0x21	//0x21
#define EEPROM_CrystalCap	0x22	//0x22

#define EEPROM_TxPwIndex_CCK	0x23	//0x23
#define EEPROM_TxPwIndex_OFDM_24G	0x24	//0x24~0x26
#define EEPROM_TxPwIndex_CCK_V1		0x29	//0x29~0x2B
#define EEPROM_TxPwIndex_OFDM_24G_V1	0x2C	//0x2C~0x2E
#define EEPROM_TxPwIndex_Ver		0x27	//0x27

#define EEPROM_Default_TxPowerDiff		0x0
#define EEPROM_Default_ThermalMeter		0x7
#define EEPROM_Default_PwDiff			0x4
#define EEPROM_Default_CrystalCap		0x5
#define EEPROM_Default_TxPower			0x1010
#define EEPROM_Customer_ID			0x7B	//0x7B:CustomerID
#define EEPROM_ChannelPlan			0x16	//0x7C
#define EEPROM_IC_VER				0x7d	//0x7D
#define EEPROM_CRC				0x7e	//0x7E~0x7F

#define EEPROM_CID_DEFAULT			0x0
#define EEPROM_CID_CAMEO				0x1
#define EEPROM_CID_RUNTOP				0x2
#define EEPROM_CID_Senao				0x3
#define EEPROM_CID_TOSHIBA				0x4	// Toshiba setting, Merge by Jacken, 2008/01/31
#define EEPROM_CID_NetCore				0x5
#define EEPROM_CID_Nettronix			0x6
#define EEPROM_CID_Pronet				0x7
#define EEPROM_CID_DLINK				0x8

#define AC_PARAM_TXOP_LIMIT_OFFSET	16
#define AC_PARAM_ECW_MAX_OFFSET		12
#define AC_PARAM_ECW_MIN_OFFSET		8
#define AC_PARAM_AIFS_OFFSET		0

//#endif
enum _RTL8192Usb_HW {

	PCIF			= 0x009, // PCI Function Register 0x0009h~0x000bh
#define	BB_GLOBAL_RESET_BIT	0x1
	BB_GLOBAL_RESET		= 0x020, // BasebandGlobal Reset Register
	BSSIDR			= 0x02E, // BSSID Register
	CMDR			= 0x037, // Command register
#define CR_RST			0x10
#define CR_RE			0x08
#define CR_TE			0x04
#define CR_MulRW		0x01
	SIFS			= 0x03E, // SIFS register
	TCR			= 0x040, // Transmit Configuration Register

#define TCR_MXDMA_2048		7
#define TCR_LRL_OFFSET		0
#define TCR_SRL_OFFSET		8
#define TCR_MXDMA_OFFSET	21
#define TCR_SAT			BIT24		// Enable Rate depedent ack timeout timer
	RCR			= 0x044, // Receive Configuration Register
#define MAC_FILTER_MASK ((1<<0) | (1<<1) | (1<<2) | (1<<3) | (1<<5) | \
		(1<<12) | (1<<18) | (1<<19) | (1<<20) | (1<<21) | (1<<22) | (1<<23))
#define RX_FIFO_THRESHOLD_MASK ((1<<13) | (1<<14) | (1<<15))
#define RX_FIFO_THRESHOLD_SHIFT 13
#define RX_FIFO_THRESHOLD_128 3
#define RX_FIFO_THRESHOLD_256 4
#define RX_FIFO_THRESHOLD_512 5
#define RX_FIFO_THRESHOLD_1024 6
#define RX_FIFO_THRESHOLD_NONE 7
#define MAX_RX_DMA_MASK ((1<<8) | (1<<9) | (1<<10))
#define RCR_MXDMA_OFFSET	8
#define RCR_FIFO_OFFSET		13
#define RCR_ONLYERLPKT		BIT31			// Early Receiving based on Packet Size.
#define RCR_ENCS2		BIT30			// Enable Carrier Sense Detection Method 2
#define RCR_ENCS1		BIT29			// Enable Carrier Sense Detection Method 1
#define RCR_ENMBID		BIT27			// Enable Multiple BssId.
#define RCR_ACKTXBW		(BIT24|BIT25)		// TXBW Setting of ACK frames
#define RCR_CBSSID		BIT23			// Accept BSSID match packet
#define RCR_APWRMGT		BIT22			// Accept power management packet
#define	RCR_ADD3		BIT21			// Accept address 3 match packet
#define RCR_AMF			BIT20			// Accept management type frame
#define RCR_ACF			BIT19			// Accept control type frame
#define RCR_ADF			BIT18			// Accept data type frame
#define RCR_RXFTH		BIT13			// Rx FIFO Threshold
#define RCR_AICV		BIT12			// Accept ICV error packet
#define	RCR_ACRC32		BIT5			// Accept CRC32 error packet
#define	RCR_AB			BIT3			// Accept broadcast packet
#define	RCR_AM			BIT2			// Accept multicast packet
#define	RCR_APM			BIT1			// Accept physical match packet
#define	RCR_AAP			BIT0			// Accept all unicast packet
	SLOT_TIME		= 0x049, // Slot Time Register
	ACK_TIMEOUT		= 0x04c, // Ack Timeout Register
	PIFS_TIME		= 0x04d, // PIFS time
	USTIME			= 0x04e, // Microsecond Tuning Register, Sets the microsecond time unit used by MAC clock.
	EDCAPARA_BE		= 0x050, // EDCA Parameter of AC BE
	EDCAPARA_BK		= 0x054, // EDCA Parameter of AC BK
	EDCAPARA_VO		= 0x058, // EDCA Parameter of AC VO
	EDCAPARA_VI		= 0x05C, // EDCA Parameter of AC VI
	RFPC			= 0x05F, // Rx FIFO Packet Count
	CWRR			= 0x060, // Contention Window Report Register
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
	RCAMO			= 0x0A8, // Software read/write CAM config
	SECR			= 0x0B0, //Security Configuration Register
#define	SCR_TxUseDK		BIT0			//Force Tx Use Default Key
#define SCR_RxUseDK		BIT1			//Force Rx Use Default Key
#define SCR_TxEncEnable		BIT2			//Enable Tx Encryption
#define SCR_RxDecEnable		BIT3			//Enable Rx Decryption
#define SCR_SKByA2		BIT4			//Search kEY BY A2
#define SCR_NoSKMC		BIT5			//No Key Search for Multicast
#define SCR_UseDK		0x01
#define SCR_TxSecEnable		0x02
#define SCR_RxSecEnable		0x04
	TPPoll			= 0x0fd, // Transmit priority polling register
	PSR			= 0x0ff, // Page Select Register
#define CPU_CCK_LOOPBACK	0x00030000
#define CPU_GEN_SYSTEM_RESET	0x00000001
#define CPU_GEN_FIRMWARE_RESET	0x00000008
#define CPU_GEN_BOOT_RDY	0x00000010
#define CPU_GEN_FIRM_RDY	0x00000020
#define CPU_GEN_PUT_CODE_OK	0x00000080
#define CPU_GEN_BB_RST		0x00000100
#define CPU_GEN_PWR_STB_CPU	0x00000004
#define CPU_GEN_NO_LOOPBACK_MSK	0xFFF8FFFF		// Set bit18,17,16 to 0. Set bit19
#define CPU_GEN_NO_LOOPBACK_SET	0x00080000		// Set BIT19 to 1

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
	LED1Cfg			=		0x154,// LED1 Configuration Register
	LED0Cfg			=		0x155,// LED0 Configuration Register

	AcmAvg			= 0x170, // ACM Average Period Register
	AcmHwCtrl		= 0x171, // ACM Hardware Control Register
//----------------------------------------------------------------------------
////
////       8190 AcmHwCtrl bits                                    (offset 0x171, 1 byte)
////----------------------------------------------------------------------------
//
#define AcmHw_HwEn              BIT0
#define AcmHw_BeqEn             BIT1
#define AcmHw_ViqEn             BIT2
#define AcmHw_VoqEn             BIT3
#define AcmHw_BeqStatus         BIT4
#define AcmHw_ViqStatus         BIT5
#define AcmHw_VoqStatus         BIT6

	AcmFwCtrl		= 0x172, // ACM Firmware Control Register
	AES_11N_FIX		= 0x173,
	VOAdmTime		= 0x174, // VO Queue Admitted Time Register
	VIAdmTime		= 0x178, // VI Queue Admitted Time Register
	BEAdmTime		= 0x17C, // BE Queue Admitted Time Register
	RQPN1			= 0x180, // Reserved Queue Page Number , Vo Vi, Be, Bk
	RQPN2			= 0x184, // Reserved Queue Page Number, HCCA, Cmd, Mgnt, High
	RQPN3			= 0x188, // Reserved Queue Page Number, Bcn, Public,
//	QPRR			= 0x1E0, // Queue Page Report per TID
	QPNR			= 0x1D0, //0x1F0, // Queue Packet Number report per TID
	BQDA			= 0x200, // Beacon Queue Descriptor Address
	HQDA			= 0x204, // High Priority Queue Descriptor Address
	CQDA			= 0x208, // Command Queue Descriptor Address
	MQDA			= 0x20C, // Management Queue Descriptor Address
	HCCAQDA			= 0x210, // HCCA Queue Descriptor Address
	VOQDA			= 0x214, // VO Queue Descriptor Address
	VIQDA			= 0x218, // VI Queue Descriptor Address
	BEQDA			= 0x21C, // BE Queue Descriptor Address
	BKQDA			= 0x220, // BK Queue Descriptor Address
	RCQDA			= 0x224, // Receive command Queue Descriptor Address
	RDQDA			= 0x228, // Receive Queue Descriptor Start Address

	MAR0			= 0x240, // Multicast filter.
	MAR4			= 0x244,

	CCX_PERIOD		= 0x250, // CCX Measurement Period Register, in unit of TU.
	CLM_RESULT		= 0x251, // CCA Busy fraction register.
	NHM_PERIOD		= 0x252, // NHM Measurement Period register, in unit of TU.

	NHM_THRESHOLD0		= 0x253, // Noise Histogram Meashorement0.
	NHM_THRESHOLD1		= 0x254, // Noise Histogram Meashorement1.
	NHM_THRESHOLD2		= 0x255, // Noise Histogram Meashorement2.
	NHM_THRESHOLD3		= 0x256, // Noise Histogram Meashorement3.
	NHM_THRESHOLD4		= 0x257, // Noise Histogram Meashorement4.
	NHM_THRESHOLD5		= 0x258, // Noise Histogram Meashorement5.
	NHM_THRESHOLD6		= 0x259, // Noise Histogram Meashorement6

	MCTRL			= 0x25A, // Measurement Control

	NHM_RPI_COUNTER0	= 0x264, // Noise Histogram RPI counter0, the fraction of signal strength < NHM_THRESHOLD0.
	NHM_RPI_COUNTER1	= 0x265, // Noise Histogram RPI counter1, the fraction of signal strength in (NHM_THRESHOLD0, NHM_THRESHOLD1].
	NHM_RPI_COUNTER2	= 0x266, // Noise Histogram RPI counter2, the fraction of signal strength in (NHM_THRESHOLD1, NHM_THRESHOLD2].
	NHM_RPI_COUNTER3	= 0x267, // Noise Histogram RPI counter3, the fraction of signal strength in (NHM_THRESHOLD2, NHM_THRESHOLD3].
	NHM_RPI_COUNTER4	= 0x268, // Noise Histogram RPI counter4, the fraction of signal strength in (NHM_THRESHOLD3, NHM_THRESHOLD4].
	NHM_RPI_COUNTER5	= 0x269, // Noise Histogram RPI counter5, the fraction of signal strength in (NHM_THRESHOLD4, NHM_THRESHOLD5].
	NHM_RPI_COUNTER6	= 0x26A, // Noise Histogram RPI counter6, the fraction of signal strength in (NHM_THRESHOLD5, NHM_THRESHOLD6].
	NHM_RPI_COUNTER7	= 0x26B, // Noise Histogram RPI counter7, the fraction of signal strength in (NHM_THRESHOLD6, NHM_THRESHOLD7].
#define	BW_OPMODE_11J			BIT0
#define	BW_OPMODE_5G			BIT1
#define	BW_OPMODE_20MHZ			BIT2
	BW_OPMODE		= 0x300, // Bandwidth operation mode
	MSR			= 0x303, // Media Status register
#define MSR_LINK_MASK      ((1<<0)|(1<<1))
#define MSR_LINK_MANAGED   2
#define MSR_LINK_NONE      0
#define MSR_LINK_SHIFT     0
#define MSR_LINK_ADHOC     1
#define MSR_LINK_MASTER    3
#define MSR_LINK_ENEDCA	   (1<<4)
	RETRY_LIMIT		= 0x304, // Retry Limit [15:8]-short, [7:0]-long
#define RETRY_LIMIT_SHORT_SHIFT 8
#define RETRY_LIMIT_LONG_SHIFT 0
	TSFR			= 0x308,
	RRSR			= 0x310, // Response Rate Set
#define RRSR_RSC_OFFSET			21
#define RRSR_SHORT_OFFSET			23
#define RRSR_RSC_DUPLICATE			0x600000
#define RRSR_RSC_LOWSUBCHNL		0x400000
#define RRSR_RSC_UPSUBCHANL		0x200000
#define RRSR_SHORT					0x800000
#define RRSR_1M						BIT0
#define RRSR_2M						BIT1
#define RRSR_5_5M					BIT2
#define RRSR_11M					BIT3
#define RRSR_6M						BIT4
#define RRSR_9M						BIT5
#define RRSR_12M					BIT6
#define RRSR_18M					BIT7
#define RRSR_24M					BIT8
#define RRSR_36M					BIT9
#define RRSR_48M					BIT10
#define RRSR_54M					BIT11
#define RRSR_MCS0					BIT12
#define RRSR_MCS1					BIT13
#define RRSR_MCS2					BIT14
#define RRSR_MCS3					BIT15
#define RRSR_MCS4					BIT16
#define RRSR_MCS5					BIT17
#define RRSR_MCS6					BIT18
#define RRSR_MCS7					BIT19
#define BRSR_AckShortPmb			BIT23		// CCK ACK: use Short Preamble or not.
	RATR0			= 0x320, // Rate Adaptive Table register1
	UFWP			= 0x318,
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
#define RATE_ALL_CCK		RATR_1M|RATR_2M|RATR_55M|RATR_11M
#define RATE_ALL_OFDM_AG	RATR_6M|RATR_9M|RATR_12M|RATR_18M|RATR_24M\
							|RATR_36M|RATR_48M|RATR_54M
#define RATE_ALL_OFDM_1SS	RATR_MCS0|RATR_MCS1|RATR_MCS2|RATR_MCS3 | \
							RATR_MCS4|RATR_MCS5|RATR_MCS6|RATR_MCS7
#define RATE_ALL_OFDM_2SS	RATR_MCS8|RATR_MCS9	|RATR_MCS10|RATR_MCS11| \
							RATR_MCS12|RATR_MCS13|RATR_MCS14|RATR_MCS15

	MCS_TXAGC		= 0x340, // MCS AGC
	CCK_TXAGC		= 0x348, // CCK AGC
//	ISR			= 0x350, // Interrupt Status Register
//	IMR			= 0x354, // Interrupt Mask Register
//	IMR_POLL		= 0x360,
	MacBlkCtrl		= 0x403, // Mac block on/off control register

	EPROM_CMD		= 0xfe58,
#define Cmd9346CR_9356SEL	(1<<4)
#define EPROM_CMD_RESERVED_MASK (1<<5)
#define EPROM_CMD_OPERATING_MODE_SHIFT 6
#define EPROM_CMD_OPERATING_MODE_MASK ((1<<7)|(1<<6))
#define EPROM_CMD_CONFIG 0x3
#define EPROM_CMD_NORMAL 0
#define EPROM_CMD_LOAD 1
#define EPROM_CMD_PROGRAM 2
#define EPROM_CS_BIT BIT(3)
#define EPROM_CK_BIT BIT(2)
#define EPROM_W_BIT  BIT(1)
#define EPROM_R_BIT  BIT(0)

	MAC0			= 0x000,
	MAC1			= 0x001,
	MAC2			= 0x002,
	MAC3			= 0x003,
	MAC4			= 0x004,
	MAC5			= 0x005,

};
//----------------------------------------------------------------------------
//       818xB AnaParm & AnaParm2 Register
//----------------------------------------------------------------------------
//#define ANAPARM_ASIC_ON    0x45090658
//#define ANAPARM2_ASIC_ON   0x727f3f52
#define GPI 0x108
#define GPO 0x109
#define GPE 0x10a
#endif
