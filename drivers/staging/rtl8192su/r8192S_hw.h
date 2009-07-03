/*****************************************************************************
 *	Copyright(c) 2008,  RealTEK Technology Inc. All Right Reserved.
 *
 * Module:	__INC_HAL8192SEREG_H
 *
 *
 * Note:	1. Define Mac register address and corresponding bit mask map
 *			2. CCX register
 *			3. Backward compatible register with useless address.
 *			4. Define 92SU required register address and definition.
 *
 *
 * Export:	Constants, macro, functions(API), global variables(None).
 *
 * Abbrev:
 *
 * History:
 *		Data		Who		Remark
 *      08/07/2007  MHC    	1. Porting from 9x series PHYCFG.h.
 *							2. Reorganize code architecture.
 *
 *****************************************************************************/
#ifndef R8192S_HW
#define R8192S_HW

typedef enum _VERSION_8192S{
	VERSION_8192S_ACUT,
	VERSION_8192S_BCUT,
	VERSION_8192S_CCUT
}VERSION_8192S,*PVERSION_8192S;

//#ifdef RTL8192SU
typedef enum _VERSION_8192SUsb{
	VERSION_8192SU_A, //A-Cut
	VERSION_8192SU_B, //B-Cut
	VERSION_8192SU_C, //C-Cut
}VERSION_8192SUsb, *PVERSION_8192SUsb;
//#else
typedef enum _VERSION_819xU{
	VERSION_819xU_A, // A-cut
	VERSION_819xU_B, // B-cut
	VERSION_819xU_C,// C-cut
}VERSION_819xU,*PVERSION_819xU;
//#endif

/* 2007/11/15 MH Define different RF type. */
typedef	enum _RT_RF_TYPE_DEFINITION
{
	RF_1T2R = 0,
	RF_2T4R,
	RF_2T2R,
	RF_1T1R,
	RF_2T2R_GREEN,
	//RF_3T3R,
	//RF_3T4R,
	//RF_4T4R,
	RF_819X_MAX_TYPE
}RT_RF_TYPE_DEF_E;

typedef enum _BaseBand_Config_Type{
	BaseBand_Config_PHY_REG = 0,			//Radio Path A
	BaseBand_Config_AGC_TAB = 1,			//Radio Path B
}BaseBand_Config_Type, *PBaseBand_Config_Type;

#define	RTL8187_REQT_READ		0xc0
#define	RTL8187_REQT_WRITE	0x40
#define	RTL8187_REQ_GET_REGS	0x05
#define	RTL8187_REQ_SET_REGS	0x05

#define MAX_TX_URB 5
#define MAX_RX_URB 16

#define R8180_MAX_RETRY 255
//#define MAX_RX_NORMAL_URB 3
//#define MAX_RX_COMMAND_URB 2
#define RX_URB_SIZE 		9100

#define BB_ANTATTEN_CHAN14	0x0c
#define BB_ANTENNA_B 		0x40

#define BB_HOST_BANG 		(1<<30)
#define BB_HOST_BANG_EN 	(1<<2)
#define BB_HOST_BANG_CLK 	(1<<1)
#define BB_HOST_BANG_RW 	(1<<3)
#define BB_HOST_BANG_DATA	1


//============================================================
//       8192S Regsiter bit
//============================================================
#define	BB_GLOBAL_RESET_BIT	0x1

#define CR_RST			0x10
#define CR_RE			0x08
#define CR_TE			0x04
#define CR_MulRW			0x01

#define MAC_FILTER_MASK ((1<<0) | (1<<1) | (1<<2) | (1<<3) | (1<<5) | \
		(1<<12) | (1<<18) | (1<<19) | (1<<20) | (1<<21) | (1<<22) | (1<<23))

#define RX_FIFO_THRESHOLD_MASK 	((1<<13) | (1<<14) | (1<<15))
#define RX_FIFO_THRESHOLD_SHIFT 	13
#define RX_FIFO_THRESHOLD_128 		3
#define RX_FIFO_THRESHOLD_256 		4
#define RX_FIFO_THRESHOLD_512 		5
#define RX_FIFO_THRESHOLD_1024 		6
#define RX_FIFO_THRESHOLD_NONE 	7

#define MAX_RX_DMA_MASK ((1<<8) | (1<<9) | (1<<10))

//----------------------------------------------------------------------------
//       8190 CPU General Register		(offset 0x100, 4 byte)
//----------------------------------------------------------------------------
#define	CPU_CCK_LOOPBACK			0x00030000
#define	CPU_GEN_SYSTEM_RESET		0x00000001
#define	CPU_GEN_FIRMWARE_RESET	0x00000008
#define	CPU_GEN_BOOT_RDY			0x00000010
#define	CPU_GEN_FIRM_RDY			0x00000020
#define	CPU_GEN_PUT_CODE_OK		0x00000080
#define	CPU_GEN_BB_RST				0x00000100
#define	CPU_GEN_PWR_STB_CPU		0x00000004
#define 	CPU_GEN_NO_LOOPBACK_MSK	0xFFF8FFFF // Set bit18,17,16 to 0. Set bit19
#define	CPU_GEN_NO_LOOPBACK_SET	0x00080000 // Set BIT19 to 1
//----------------------------------------------------------------------------
////
////       8190 AcmHwCtrl bits                                    (offset 0x171, 1 byte)
////----------------------------------------------------------------------------
#define MSR_LINK_MASK      	((1<<0)|(1<<1))
#define MSR_LINK_MANAGED   	2
#define MSR_LINK_NONE      	0
#define MSR_LINK_SHIFT     	0
#define MSR_LINK_ADHOC     	1
#define MSR_LINK_MASTER    	3
#define MSR_LINK_ENEDCA	   	(1<<4)


//#define Cmd9346CR_9356SEL	(1<<4)
#define EPROM_CMD_RESERVED_MASK 			(1<<5)
#define EPROM_CMD_OPERATING_MODE_SHIFT 	6
#define EPROM_CMD_OPERATING_MODE_MASK 	((1<<7)|(1<<6))
#define EPROM_CMD_CONFIG 		0x3
#define EPROM_CMD_NORMAL 		0
#define EPROM_CMD_LOAD 			1
#define EPROM_CMD_PROGRAM		2
#define EPROM_CS_SHIFT 			3
#define EPROM_CK_SHIFT 			2
#define EPROM_W_SHIFT 			1
#define EPROM_R_SHIFT 			0

//#define	MAC0 			 0x000,
//#define	MAC1 			 0x001,
//#define	MAC2 			 0x002,
//#define	MAC3 			 0x003,
//#define	MAC4 			 0x004,
//#define	MAC5 			 0x005,

//============================================================
//       8192S Regsiter offset definition
//============================================================

//
// MAC register 0x0 - 0x5xx
// 1. System configuration registers.
// 2. Command Control Registers
// 3. MACID Setting Registers
// 4. Timing Control Registers
// 5. FIFO Control Registers
// 6. Adaptive Control Registers
// 7. EDCA Setting Registers
// 8. WMAC, BA and CCX related Register.
// 9. Security Control Registers
// 10. Power Save Control Registers
// 11. General Purpose Registers
// 12. Host Interrupt Status Registers
// 13. Test Mode and Debug Control Registers
// 14. PCIE config register
//


//
// 1. System Configuration Registers	 (Offset: 0x0000 - 0x003F)
//
#define		SYS_ISO_CTRL		0x0000	// System Isolation Interface Control.
#define		SYS_FUNC_EN		0x0002	// System Function Enable.
#define		PMC_FSM			0x0004	// Power Sequence Control.
#define		SYS_CLKR			0x0008	// System Clock.
#define		EPROM_CMD			0x000A	// 93C46/93C56 Command Register. (win CR93C46)
#define		EE_VPD				0x000C	// EEPROM VPD Data.
#define		AFE_MISC			0x0010	// AFE Misc.
#define		SPS0_CTRL			0x0011	// Switching Power Supply 0 Control.
#define		SPS1_CTRL			0x0018	// Switching Power Supply 1 Control.
#define		RF_CTRL				0x001F	// RF Block Control.
#define		LDOA15_CTRL		0x0020	// V15 Digital LDO Control.
#define		LDOV12D_CTRL		0x0021	// V12 Digital LDO Control.
#define		LDOHCI12_CTRL		0x0022	// V12 Digital LDO Control.
#define		LDO_USB_SDIO		0x0023	// LDO USB Control.
#define		LPLDO_CTRL			0x0024	// Low Power LDO Control.
#define		AFE_XTAL_CTRL		0x0026	// AFE Crystal Control.
#define		AFE_PLL_CTRL		0x0028	// System Function Enable.
#define		EFUSE_CTRL			0x0030	// E-Fuse Control.
#define		EFUSE_TEST			0x0034	// E-Fuse Test.
#define		PWR_DATA			0x0038	// Power on date.
#define		DBG_PORT			0x003A	// MAC debug port select
#define		DPS_TIMER			0x003C	// Deep Power Save Timer Register.
#define		RCLK_MON			0x003E	// Retention Clock Monitor.

//
// 2. Command Control Registers	 (Offset: 0x0040 - 0x004F)
//
#define		CMDR				0x0040	// MAC Command Register.
#define		TXPAUSE				0x0042	// Transmission Pause Register.
#define		LBKMD_SEL			0x0043	// Loopback Mode Select Register.
#define		TCR					0x0044	// Transmit Configuration Register
#define		RCR					0x0048	// Receive Configuration Register
#define		MSR					0x004C	// Media Status register
#define		SYSF_CFG			0x004D	// System Function Configuration.
#define		RX_PKY_LIMIT		0x004E	// RX packet length limit
#define		MBIDCTRL			0x004F	// MBSSID Control.

//
// 3. MACID Setting Registers	(Offset: 0x0050 - 0x007F)
//
#define		MACIDR				0x0050	// MAC ID Register, Offset 0x0050-0x0055
#define		MACIDR0				0x0050	// MAC ID Register, Offset 0x0050-0x0053
#define		MACIDR4				0x0054	// MAC ID Register, Offset 0x0054-0x0055
#define		BSSIDR				0x0058	// BSSID Register, Offset 0x0058-0x005D
#define		HWVID				0x005E	// HW Version ID.
#define		MAR					0x0060	// Multicase Address.
#define		MBIDCAMCONTENT	0x0068	// MBSSID CAM Content.
#define		MBIDCAMCFG			0x0070	// MBSSID CAM Configuration.
#define		BUILDTIME			0x0074	// Build Time Register.
#define		BUILDUSER			0x0078	// Build User Register.

// Redifine MACID register, to compatible prior ICs.
#define		IDR0				MACIDR0
#define		IDR4				MACIDR4

//
// 4. Timing Control Registers	(Offset: 0x0080 - 0x009F)
//
#define		TSFR				0x0080	// Timing Sync Function Timer Register.
#define		SLOT_TIME			0x0089	// Slot Time Register, in us.
#define		USTIME				0x008A	// EDCA/TSF clock unit time us unit.
#define		SIFS_CCK			0x008C	// SIFS for CCK, in us.
#define		SIFS_OFDM			0x008E	// SIFS for OFDM, in us.
#define		PIFS_TIME			0x0090	// PIFS time register.
#define		ACK_TIMEOUT		0x0091	// Ack Timeout Register
#define		EIFSTR				0x0092	// EIFS time regiser.
#define		BCN_INTERVAL		0x0094	// Beacon Interval, in TU.
#define		ATIMWND			0x0096	// ATIM Window width, in TU.
#define		BCN_DRV_EARLY_INT	0x0098	// Driver Early Interrupt.
#define		BCN_DMATIME		0x009A	// Beacon DMA and ATIM INT Time.
#define		BCN_ERR_THRESH		0x009C	// Beacon Error Threshold.
#define		MLT					0x009D	// MSDU Lifetime.
#define		RSVD_MAC_TUNE_US	0x009E	// MAC Internal USE.

//
// 5. FIFO Control Registers	 (Offset: 0x00A0 - 0x015F)
//
#define 	RQPN				0x00A0
#define	RQPN1				0x00A0	// Reserved Queue Page Number for BK
#define	RQPN2				0x00A1	// Reserved Queue Page Number for BE
#define	RQPN3				0x00A2	// Reserved Queue Page Number for VI
#define	RQPN4				0x00A3	// Reserved Queue Page Number for VO
#define	RQPN5				0x00A4	// Reserved Queue Page Number for HCCA
#define	RQPN6				0x00A5	// Reserved Queue Page Number for CMD
#define	RQPN7				0x00A6	// Reserved Queue Page Number for MGNT
#define	RQPN8				0x00A7	// Reserved Queue Page Number for HIGH
#define	RQPN9				0x00A8	// Reserved Queue Page Number for Beacon
#define	RQPN10				0x00A9	// Reserved Queue Page Number for Public
#define		LD_RQPN				0x00AB  //
#define		RXFF_BNDY			0x00AC  //
#define		RXRPT_BNDY			0x00B0  //
#define		TXPKTBUF_PGBNDY		0x00B4  //
#define		PBP					0x00B5  //
#define		RXDRVINFO_SZ		0x00B6  //
#define		TXFF_STATUS			0x00B7  //
#define		RXFF_STATUS			0x00B8  //
#define		TXFF_EMPTY_TH		0x00B9  //
#define		SDIO_RX_BLKSZ		0x00BC  //
#define		RXDMA				0x00BD  //
#define		RXPKT_NUM			0x00BE  //
#define		C2HCMD_UDT_SIZE		0x00C0  //
#define		C2HCMD_UDT_ADDR		0x00C2  //
#define		FIFOPAGE1			0x00C4  // Available public queue page number
#define		FIFOPAGE2			0x00C8  //
#define		FIFOPAGE3			0x00CC  //
#define		FIFOPAGE4			0x00D0  //
#define		FIFOPAGE5			0x00D4  //
#define		FW_RSVD_PG_CRTL		0x00D8  //
#define		RXDMA_AGG_PG_TH		0x00D9  //
#define		TXRPTFF_RDPTR		0x00E0  //
#define		TXRPTFF_WTPTR		0x00E4  //
#define		C2HFF_RDPTR			0x00E8	//FIFO Read pointer register.
#define		C2HFF_WTPTR			0x00EC	//FIFO Write pointer register.
#define		RXFF0_RDPTR			0x00F0	//
#define		RXFF0_WTPTR			0x00F4  //
#define		RXFF1_RDPTR			0x00F8  //
#define		RXFF1_WTPTR			0x00FC  //
#define		RXRPT0_RDPTR		0x0100  //
#define		RXRPT0_WTPTR		0x0104  //
#define		RXRPT1_RDPTR		0x0108  //
#define		RXRPT1_WTPTR		0x010C  //
#define		RX0_UDT_SIZE		0x0110  //
#define		RX1PKTNUM			0x0114  //
#define		RXFILTERMAP			0x0116  //
#define		RXFILTERMAP_GP1		0x0118  //
#define		RXFILTERMAP_GP2		0x011A  //
#define		RXFILTERMAP_GP3		0x011C  //
#define		BCNQ_CTRL			0x0120  //
#define		MGTQ_CTRL			0x0124  //
#define		HIQ_CTRL			0x0128  //
#define		VOTID7_CTRL			0x012c  //
#define		VOTID6_CTRL			0x0130  //
#define		VITID5_CTRL			0x0134  //
#define		VITID4_CTRL			0x0138  //
#define		BETID3_CTRL			0x013c  //
#define		BETID0_CTRL			0x0140  //
#define		BKTID2_CTRL			0x0144  //
#define		BKTID1_CTRL			0x0148  //
#define		CMDQ_CTRL			0x014c  //
#define		TXPKT_NUM_CTRL		0x0150  //
#define		TXQ_PGADD			0x0152  //
#define		TXFF_PG_NUM			0x0154  //
#define		TRXDMA_STATUS		0x0156  //

//
// 6. Adaptive Control Registers  (Offset: 0x0160 - 0x01CF)
//
#define		INIMCS_SEL			0x0160	// Init MCSrate for 32 MACID 0x160-17f
#define		TX_RATE_REG		INIMCS_SEL //Current Tx rate register
#define		INIRTSMCS_SEL		0x0180	// Init RTSMCSrate
#define		RRSR				0x0181	// Response rate setting.
#define		ARFR0				0x0184	// Auto Rate Fallback 0 Register.
#define		ARFR1				0x0188	//
#define		ARFR2				0x018C  //
#define		ARFR3				0x0190  //
#define		ARFR4				0x0194  //
#define		ARFR5				0x0198  //
#define		ARFR6				0x019C  //
#define		ARFR7				0x01A0  //
#define		AGGLEN_LMT_H		0x01A7	// Aggregation Length Limit for High-MCS
#define		AGGLEN_LMT_L		0x01A8	// Aggregation Length Limit for Low-MCS.
#define		DARFRC				0x01B0	// Data Auto Rate Fallback Retry Count.
#define		RARFRC				0x01B8	// Response Auto Rate Fallback Count.
#define		MCS_TXAGC			0x01C0
#define		CCK_TXAGC			0x01C8

//
// 7. EDCA Setting Registers	 (Offset: 0x01D0 - 0x01FF)
//
#define		EDCAPARA_VO 		0x01D0	// EDCA Parameter Register for VO queue.
#define		EDCAPARA_VI			0x01D4	// EDCA Parameter Register for VI queue.
#define		EDCAPARA_BE			0x01D8	// EDCA Parameter Register for BE queue.
#define		EDCAPARA_BK			0x01DC	// EDCA Parameter Register for BK queue.
#define		BCNTCFG				0x01E0	// Beacon Time Configuration Register.
#define		CWRR				0x01E2	// Contention Window Report Register.
#define		ACMAVG				0x01E4	// ACM Average Register.
#define		AcmHwCtrl			0x01E7
#define		VO_ADMTM			0x01E8	// Admission Time Register.
#define		VI_ADMTM			0x01EC
#define		BE_ADMTM			0x01F0
#define		RETRY_LIMIT			0x01F4	// Retry Limit Registers[15:8]-short, [7:0]-long
#define		SG_RATE				0x01F6	// Max MCS Rate Available Register, which we Set the hightst SG rate.

//
// 8. WMAC, BA and CCX related Register.	 (Offset: 0x0200 - 0x023F)
//
#define		NAV_CTRL			0x0200
#define		BW_OPMODE			0x0203
#define		BACAMCMD			0x0204
#define		BACAMCONTENT		0x0208	// Block ACK CAM R/W Register.

// Roger had defined the 0x2xx register WMAC definition
#define		LBDLY				0x0210	// Loopback Delay Register.
#define		FWDLY				0x0211	// FW Delay Register.
#define		HWPC_RX_CTRL		0x0218	// HW Packet Conversion RX Control Reg
#define		MQIR				0x0220	// Mesh Qos Type Indication Register.
#define		MAIR				0x0222	// Mesh ACK.
#define		MSIR				0x0224	// Mesh HW Security Requirement Indication Reg
#define		CLM_RESULT			0x0227	// CCA Busy Fraction(Channel Load)
#define		NHM_RPI_CNT			0x0228	// Noise Histogram Measurement (NHM) RPI Report.
#define		RXERR_RPT			0x0230	// Rx Error Report.
#define		NAV_PROT_LEN		0x0234	// NAV Protection Length.
#define		CFEND_TH			0x0236	// CF-End Threshold.
#define		AMPDU_MIN_SPACE		0x0237	// AMPDU Min Space.
#define		TXOP_STALL_CTRL		0x0238

//
// 9. Security Control Registers	(Offset: 0x0240 - 0x025F)
//
#define		RWCAM				0x0240	//IN 8190 Data Sheet is called CAMcmd
#define		WCAMI				0x0244	// Software write CAM input content
#define		RCAMO				0x0248	// Software read/write CAM config
#define		CAMDBG				0x024C
#define		SECR				0x0250	//Security Configuration Register

//
// 10. Power Save Control Registers	 (Offset: 0x0260 - 0x02DF)
//
#define		WOW_CTRL			0x0260	//Wake On WLAN Control.
#define		PSSTATUS			0x0261	// Power Save Status.
#define		PSSWITCH			0x0262	// Power Save Switch.
#define		MIMOPS_WAIT_PERIOD	0x0263
#define		LPNAV_CTRL			0x0264
#define		WFM0				0x0270	// Wakeup Frame Mask.
#define		WFM1				0x0280	//
#define		WFM2				0x0290  //
#define		WFM3				0x02A0  //
#define		WFM4				0x02B0  //
#define		WFM5				0x02C0  // FW Control register.
#define		WFCRC				0x02D0	// Wakeup Frame CRC.
#define		RPWM				0x02DC	// Host Request Power Mode.
#define		CPWM				0x02DD	// Current Power Mode.
#define		FW_RPT_REG			0x02c4

//
// 11. General Purpose Registers	(Offset: 0x02E0 - 0x02FF)
//
#define		PSTIME				0x02E0  // Power Save Timer Register
#define		TIMER0				0x02E4  //
#define		TIMER1				0x02E8  //
#define		GPIO_CTRL			0x02EC  // GPIO Control Register
#define		GPIO_IN				0x02EC	// GPIO pins input value
#define		GPIO_OUT			0x02ED	// GPIO pins output value
#define		GPIO_IO_SEL			0x02EE	// GPIO pins output enable when a bit is set to "1"; otherwise, input is configured.
#define		GPIO_MOD			0x02EF	//
#define		GPIO_INTCTRL		0x02F0  // GPIO Interrupt Control Register[7:0]
#define		MAC_PINMUX_CFG		0x02F1  // MAC PINMUX Configuration Reg[7:0]
#define		LEDCFG				0x02F2  // System PINMUX Configuration Reg[7:0]
#define		PHY_REG				0x02F3  // RPT: PHY REG Access Report Reg[7:0]
#define		PHY_REG_DATA		0x02F4  // PHY REG Read DATA Register [31:0]
#define		EFUSE_CLK			0x02F8  // CTRL: E-FUSE Clock Control Reg[7:0]
//#define		GPIO_INTCTRL		0x02F9  // GPIO Interrupt Control Register[7:0]

//
// 12. Host Interrupt Status Registers	 (Offset: 0x0300 - 0x030F)
//
#define		IMR					0x0300	// Interrupt Mask Register
#define		ISR					0x0308	// Interrupt Status Register

//
// 13. Test Mode and Debug Control Registers	(Offset: 0x0310 - 0x034F)
//
#define		DBG_PORT_SWITCH		0x003A
#define		BIST				0x0310	// Bist reg definition
#define		DBS					0x0314	// Debug Select ???
#define		CPUINST				0x0318	// CPU Instruction Read Register
#define		CPUCAUSE			0x031C	// CPU Cause Register
#define		LBUS_ERR_ADDR		0x0320	// Lexra Bus Error Address Register
#define		LBUS_ERR_CMD		0x0324	// Lexra Bus Error Command Register
#define		LBUS_ERR_DATA_L		0x0328	// Lexra Bus Error Data Low DW Register
#define		LBUS_ERR_DATA_H		0x032C	//
#define		LX_EXCEPTION_ADDR	0x0330	// Lexra Bus Exception Address Register
#define		WDG_CTRL			0x0334	// Watch Dog Control Register
#define		INTMTU				0x0338	// Interrupt Mitigation Time Unit Reg
#define		INTM				0x033A	// Interrupt Mitigation Register
#define		FDLOCKTURN0			0x033C	// FW/DRV Lock Turn 0 Register
#define		FDLOCKTURN1			0x033D	// FW/DRV Lock Turn 1 Register
#define		TRXPKTBUF_DBG_DATA	0x0340	// TRX Packet Buffer Debug Data Register
#define		TRXPKTBUF_DBG_CTRL	0x0348	// TRX Packet Buffer Debug Control Reg
#define		DPLL				0x034A	// DPLL Monitor Register [15:0]
#define		CBUS_ERR_ADDR		0x0350	// CPU Bus Error Address Register
#define		CBUS_ERR_CMD		0x0354	// CPU Bus Error Command Register
#define		CBUS_ERR_DATA_L		0x0358	// CPU Bus Error Data Low DW Register
#define		CBUS_ERR_DATA_H 	0x035C	//
#define		USB_SIE_INTF_ADDR	0x0360	// USB SIE Access Interface Address Reg
#define		USB_SIE_INTF_WD		0x0361	// USB SIE Access Interface WData Reg
#define		USB_SIE_INTF_RD		0x0362	// USB SIE Access Interface RData Reg
#define		USB_SIE_INTF_CTRL	0x0363	// USB SIE Access Interface Control Reg

// Boundary is 0x37F

//
// 14. PCIE config register	(Offset 0x500-)
//
#define		TPPoll				0x0500	// Transmit Polling
#define		PM_CTRL				0x0502	// PCIE power management control Register
#define		PCIF				0x0503	// PCI Function Register 0x0009h~0x000bh

#define		THPDA				0x0514	// Transmit High Priority Desc Addr
#define		TMDA				0x0518	// Transmit Management Desc Addr
#define		TCDA				0x051C	// Transmit Command Desc Addr
#define		HDA				0x0520	// HCCA Desc Addr
#define		TVODA				0x0524	// Transmit VO Desc Addr
#define		TVIDA				0x0528	// Transmit VI Desc Addr
#define		TBEDA				0x052C	// Transmit BE Desc Addr
#define		TBKDA				0x0530	// Transmit BK Desc Addr
#define		TBDA				0x0534	// Transmit Beacon Desc Addr
#define		RCDA				0x0538	// Receive Command Desc Addr
#define		RDSA				0x053C	// Receive Desc Starting Addr
#define		DBI_WDATA			0x0540	// DBI write data Register
#define		DBI_RDATA			0x0544	// DBI read data Register
#define		DBI_CTRL			0x0548	// PCIE DBI control Register
#define		MDIO_DATA			0x0550	// PCIE MDIO data Register
#define		MDIO_CTRL			0x0554	// PCIE MDIO control Register
#define		PCI_RPWM			0x0561	// PCIE RPWM register
#define		PCI_CPWM				0x0563	// Current Power Mode.

//
// Config register   (Offset 0x800-)
//
#define 	PHY_CCA				0x803   // CCA related register

//============================================================================
//       8192S USB specific Regsiter Offset and Content definition,
//       2008.08.28, added by Roger.
//============================================================================
// Rx Aggregation time-out reg.
#define	USB_RX_AGG_TIMEOUT	0xFE5B

// Firware reserved Tx page control.
#define	FW_OFFLOAD_EN		BIT7

// Min Spacing related settings.
#define	MAX_MSS_DENSITY 			0x13
#define	MAX_MSS_DENSITY_2T 		0x13
#define	MAX_MSS_DENSITY_1T 		0x0A

// Rx DMA Control related settings
#define	RXDMA_AGG_EN		BIT7

// USB Rx Aggregation TimeOut settings
#define	RXDMA_AGG_TIMEOUT_DISABLE		0x00
#define	RXDMA_AGG_TIMEOUT_17MS  			0x01
#define	RXDMA_AGG_TIMEOUT_17_2_MS  		0x02
#define	RXDMA_AGG_TIMEOUT_17_4_MS  		0x04
#define	RXDMA_AGG_TIMEOUT_17_10_MS  		0x0A
// USB RPWM register
#define	USB_RPWM			0xFE58

//FIXLZM SVN_BRACH NOT MOD HERE, IF MOD RX IS LITTLE LOW
//#if ((HAL_CODE_BASE == RTL8192_S) &&  (DEV_BUS_TYPE==PCI_INTERFACE))
//#define	RPWM		PCI_RPWM
//#elif ((HAL_CODE_BASE == RTL8192_S) &&  (DEV_BUS_TYPE==USB_INTERFACE))
//#define	RPWM		USB_RPWM
//#endif


//============================================================================
//       8190 Regsiter offset definition
//============================================================================
#if 1	// Delete the register later
#define		AFR					0x010	// AutoLoad Function Register
#define		BCN_TCFG			0x062	// Beacon Time Configuration
#define		RATR0				0x320	// Rate Adaptive Table register1
#endif
// TODO: Remove unused register, We must declare backward compatiable
//Undefined register set in 8192S. 0x320/350 DW is useless
#define		UnusedRegister		0x0320
#define		PSR					UnusedRegister	// Page Select Register
//Security Related
#define		DCAM				UnusedRegister	// Debug CAM Interface
//PHY Configuration related
#define		BBAddr				UnusedRegister	// Phy register address register
#define		PhyDataR			UnusedRegister	// Phy register data read
#define		UFWP				UnusedRegister


//============================================================================
//       8192S Regsiter Bit and Content definition
//============================================================================

//
// 1. System Configuration Registers	 (Offset: 0x0000 - 0x003F)
//
//----------------------------------------------------------------------------
//       8192S SYS_ISO_CTRL bits					(Offset 0x0, 16bit)
//----------------------------------------------------------------------------
#define		ISO_MD2PP			BIT0	// MACTOP/BB/PCIe Digital to Power On.
#define		ISO_PA2PCIE			BIT3	// PCIe Analog 1.2V to PCIe 3.3V
#define		ISO_PLL2MD			BIT4	// AFE PLL to MACTOP/BB/PCIe Digital.
#define		ISO_PWC_DV2RP		BIT11	// Digital Vdd to Retention Path
#define		ISO_PWC_RV2RP		BIT12	// LPLDOR12 to Retenrion Path, 1: isolation, 0: attach.

//----------------------------------------------------------------------------
//       8192S SYS_FUNC_EN bits					(Offset 0x2, 16bit)
//----------------------------------------------------------------------------
#define		FEN_MREGEN			BIT15	// MAC I/O Registers Enable.
#define		FEN_DCORE			BIT11	// Enable Core Digital.
#define		FEN_CPUEN			BIT10	// Enable CPU Core Digital.
//       8192S PMC_FSM bits					(Offset 0x4, 32bit)
//----------------------------------------------------------------------------
#define		PAD_HWPD_IDN		BIT22	// HWPDN PAD status Indicator

//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
//       8192S SYS_CLKR bits					(Offset 0x8, 16bit)
//----------------------------------------------------------------------------
#define		SYS_CLKSEL_80M		BIT0	// System Clock 80MHz
#define		SYS_PS_CLKSEL		BIT1	 //System power save clock select.
#define		SYS_CPU_CLKSEL		BIT2	// System Clock select, 1: AFE source, 0: System clock(L-Bus)
#define		SYS_MAC_CLK_EN		BIT11	// MAC Clock Enable.
#define		SYS_SWHW_SEL		BIT14	// Load done, control path seitch.
#define		SYS_FWHW_SEL		BIT15	// Sleep exit, control path swith.


//----------------------------------------------------------------------------
//       8192S Cmd9346CR bits					(Offset 0xA, 16bit)
//----------------------------------------------------------------------------
#define		CmdEEPROM_En						BIT5	 // EEPROM enable when set 1
#define		CmdEERPOMSEL						BIT4 // System EEPROM select, 0: boot from E-FUSE, 1: The EEPROM used is 9346
#define		Cmd9346CR_9356SEL					BIT4
#define		AutoLoadEEPROM						(CmdEEPROM_En|CmdEERPOMSEL)
#define		AutoLoadEFUSE						CmdEEPROM_En


//----------------------------------------------------------------------------
//       8192S AFE_MISC bits		AFE Misc			(Offset 0x10, 8bits)
//----------------------------------------------------------------------------
#define		AFE_MBEN			BIT1	// Enable AFE Macro Block's Mbias.
#define		AFE_BGEN			BIT0	// Enable AFE Macro Block's Bandgap.

//----------------------------------------------------------------------------
//       8192S SPS1_CTRL bits					(Offset 0x18-1E, 56bits)
//----------------------------------------------------------------------------
#define		SPS1_SWEN			BIT1	// Enable vsps18 SW Macro Block.
#define		SPS1_LDEN			BIT0	// Enable VSPS12 LDO Macro block.

//----------------------------------------------------------------------------
//       8192S RF_CTRL bits					(Offset 0x1F, 8bits)
//----------------------------------------------------------------------------
#define		RF_EN				BIT0 // Enable RF module.
#define		RF_RSTB			BIT1 // Reset RF module.
#define		RF_SDMRSTB			BIT2 // Reset RF SDM module.

//----------------------------------------------------------------------------
//       8192S LDOA15_CTRL bits					(Offset 0x20, 8bits)
//----------------------------------------------------------------------------
#define		LDA15_EN			BIT0	// Enable LDOA15 Macro Block

//----------------------------------------------------------------------------
//       8192S LDOV12D_CTRL bits					(Offset 0x21, 8bits)
//----------------------------------------------------------------------------
#define		LDV12_EN			BIT0	// Enable LDOVD12 Macro Block
#define		LDV12_SDBY			BIT1	// LDOVD12 standby mode

//----------------------------------------------------------------------------
//       8192S AFE_XTAL_CTRL bits	AFE Crystal Control.	(Offset 0x26,16bits)
//----------------------------------------------------------------------------
#define		XTAL_GATE_AFE		BIT10
// Gated Control. 1: AFE Clock source gated, 0: Clock enable.

//----------------------------------------------------------------------------
//       8192S AFE_PLL_CTRL bits	System Function Enable	(Offset 0x28,64bits)
//----------------------------------------------------------------------------
#define		APLL_EN				BIT0	// Enable AFE PLL Macro Block.

// Find which card bus type
#define		AFR_CardBEn			BIT0
#define		AFR_CLKRUN_SEL		BIT1
#define		AFR_FuncRegEn		BIT2

//
// 2. Command Control Registers	 (Offset: 0x0040 - 0x004F)
//
//----------------------------------------------------------------------------
//       8192S (CMD) command register bits		(Offset 0x40, 16 bits)
//----------------------------------------------------------------------------
#define		APSDOFF_STATUS		BIT15	//
#define		APSDOFF				BIT14   //
#define		BBRSTn				BIT13   //Enable OFDM/CCK
#define		BB_GLB_RSTn			BIT12   //Enable BB
#define		SCHEDULE_EN			BIT10   //Enable MAC scheduler
#define		MACRXEN				BIT9    //
#define		MACTXEN				BIT8    //
#define		DDMA_EN				BIT7    //FW off load function enable
#define		FW2HW_EN			BIT6    //MAC every module reset as below
#define		RXDMA_EN			BIT5    //
#define		TXDMA_EN			BIT4    //
#define		HCI_RXDMA_EN		BIT3    //
#define		HCI_TXDMA_EN		BIT2    //

//----------------------------------------------------------------------------
//       8192S (TXPAUSE) transmission pause		(Offset 0x42, 8 bits)
//----------------------------------------------------------------------------
#define		StopHCCA			BIT6
#define		StopHigh			BIT5
#define		StopMgt				BIT4
#define		StopVO				BIT3
#define		StopVI				BIT2
#define		StopBE				BIT1
#define		StopBK				BIT0

//----------------------------------------------------------------------------
//       8192S (LBKMD) LoopBack Mode Select 		(Offset 0x43, 8 bits)
//----------------------------------------------------------------------------
//
//	[3] no buffer, 1: no delay, 0: delay; [2] dmalbk, [1] no_txphy, [0] diglbk.
//	0000: Normal
//	1011: MAC loopback (involving CPU)
//	0011: MAC Delay Loopback
//	0001: PHY loopback (not yet implemented)
//	0111: DMA loopback (only uses TxPktBuffer and DMA engine)
//	All other combinations are reserved.
//	Default: 0000b.
//
#define		LBK_NORMAL		0x00
#define		LBK_MAC_LB		(BIT0|BIT1|BIT3)
#define		LBK_MAC_DLB		(BIT0|BIT1)
#define		LBK_DMA_LB		(BIT0|BIT1|BIT2)

//----------------------------------------------------------------------------
//       8192S (TCR) transmission Configuration Register (Offset 0x44, 32 bits)
//----------------------------------------------------------------------------
#define		TCP_OFDL_EN				BIT25	//For CE packet conversion
#define		HWPC_TX_EN				BIT24   //""
#define		TXDMAPRE2FULL			BIT23   //TXDMA enable pre2full sync
#define		DISCW					BIT20   //CW disable
#define		TCRICV					BIT19   //Append ICV or not
#define		CfendForm				BIT17   //AP mode
#define		TCRCRC					BIT16   //Append CRC32
#define		FAKE_IMEM_EN			BIT15   //
#define		TSFRST					BIT9    //
#define		TSFEN					BIT8    //
// For TCR FW download ready --> write by FW  Bit0-7 must all one
#define		FWALLRDY				(BIT0|BIT1|BIT2|BIT3|BIT4|BIT5|BIT6|BIT7)
#define		FWRDY					BIT7
#define		BASECHG					BIT6
#define		IMEM					BIT5
#define		DMEM_CODE_DONE			BIT4
#define		EXT_IMEM_CHK_RPT		BIT3
#define		EXT_IMEM_CODE_DONE		BIT2
#define		IMEM_CHK_RPT			BIT1
#define		IMEM_CODE_DONE			BIT0
// Copy fomr 92SU definition
#define		IMEM_CODE_DONE		BIT0
#define		IMEM_CHK_RPT		BIT1
#define		EMEM_CODE_DONE		BIT2
#define		EMEM_CHK_RPT		BIT3
#define		DMEM_CODE_DONE		BIT4
#define		IMEM_RDY			BIT5
#define		BASECHG			BIT6
#define		FWRDY				BIT7
#define		LOAD_FW_READY		(IMEM_CODE_DONE|IMEM_CHK_RPT|EMEM_CODE_DONE|\
					EMEM_CHK_RPT|DMEM_CODE_DONE|IMEM_RDY|BASECHG|\
					FWRDY)
#define		TCR_TSFEN			BIT8		// TSF function on or off.
#define		TCR_TSFRST			BIT9		// Reset TSF function to zero.
#define		TCR_FAKE_IMEM_EN	BIT15
#define		TCR_CRC				BIT16
#define		TCR_ICV				BIT19	// Integrity Check Value.
#define		TCR_DISCW			BIT20	// Disable Contention Windows Backoff.
#define		TCR_HWPC_TX_EN	BIT24
#define		TCR_TCP_OFDL_EN	BIT25
#define		TXDMA_INIT_VALUE	(IMEM_CHK_RPT|EXT_IMEM_CHK_RPT)
//----------------------------------------------------------------------------
//       8192S (RCR) Receive Configuration Register	(Offset 0x48, 32 bits)
//----------------------------------------------------------------------------
#define		RCR_APPFCS				BIT31		//WMAC append FCS after pauload
#define		RCR_DIS_ENC_2BYTE		BIT30       //HW encrypt 2 or 1 byte mode
#define		RCR_DIS_AES_2BYTE		BIT29       //
#define		RCR_HTC_LOC_CTRL		BIT28       //MFC<--HTC=1 MFC-->HTC=0
#define		RCR_ENMBID				BIT27		//Enable Multiple BssId.
#define		RCR_RX_TCPOFDL_EN		BIT26		//
#define		RCR_APP_PHYST_RXFF	BIT25       //
#define		RCR_APP_PHYST_STAFF	BIT24       //
#define		RCR_CBSSID				BIT23		//Accept BSSID match packet
#define		RCR_APWRMGT			BIT22		//Accept power management packet
#define		RCR_ADD3				BIT21		//Accept address 3 match packet
#define		RCR_AMF				BIT20		//Accept management type frame
#define		RCR_ACF					BIT19		//Accept control type frame
#define		RCR_ADF					BIT18		//Accept data type frame
#define		RCR_APP_MIC			BIT17		//
#define		RCR_APP_ICV			BIT16       //
#define		RCR_RXFTH				BIT13		//Rx FIFO Threshold Bot 13 - 15
#define		RCR_AICV				BIT12		//Accept ICV error packet
#define		RCR_RXDESC_LK_EN		BIT11		//Accept to update rx desc length
#define		RCR_APP_BA_SSN			BIT6		//Accept BA SSN
#define		RCR_ACRC32				BIT5		//Accept CRC32 error packet
#define		RCR_RXSHFT_EN			BIT4		//Accept broadcast packet
#define		RCR_AB					BIT3		//Accept broadcast packet
#define		RCR_AM					BIT2		//Accept multicast packet
#define		RCR_APM				BIT1		//Accept physical match packet
#define		RCR_AAP					BIT0		//Accept all unicast packet
#define		RCR_MXDMA_OFFSET		8
#define		RCR_FIFO_OFFSET		13

//in 92U FIXLZM
//#ifdef RTL8192U
#define RCR_ONLYERLPKT		BIT31			// Early Receiving based on Packet Size.
#define RCR_ENCS2			BIT30			// Enable Carrier Sense Detection Method 2
#define RCR_ENCS1			BIT29			// Enable Carrier Sense Detection Method 1
#define RCR_ACKTXBW			(BIT24|BIT25)		// TXBW Setting of ACK frames
//#endif
//----------------------------------------------------------------------------
//       8192S (MSR) Media Status Register	(Offset 0x4C, 8 bits)
//----------------------------------------------------------------------------
/*
Network Type
00: No link
01: Link in ad hoc network
10: Link in infrastructure network
11: AP mode
Default: 00b.
*/
#define		MSR_NOLINK				0x00
#define		MSR_ADHOC				0x01
#define		MSR_INFRA				0x02
#define		MSR_AP					0x03

//----------------------------------------------------------------------------
//       8192S (SYSF_CFG) system Fucntion Config Reg	(Offset 0x4D, 8 bits)
//----------------------------------------------------------------------------
#define		ENUART					BIT7
#define		ENJTAG					BIT3
#define		BTMODE					(BIT2|BIT1)
#define		ENBT					BIT0

//----------------------------------------------------------------------------
//       8192S (MBIDCTRL) MBSSID Control Register	(Offset 0x4F, 8 bits)
//----------------------------------------------------------------------------
#define		ENMBID					BIT7
#define		BCNUM					(BIT6|BIT5|BIT4)

//
// 3. MACID Setting Registers	(Offset: 0x0050 - 0x007F)
//

//
// 4. Timing Control Registers	(Offset: 0x0080 - 0x009F)
//
//----------------------------------------------------------------------------
//       8192S (USTIME) US Time Tunning Register	(Offset 0x8A, 16 bits)
//----------------------------------------------------------------------------
#define		USTIME_EDCA				0xFF00
#define		USTIME_TSF				0x00FF

//----------------------------------------------------------------------------
//       8192S (SIFS_CCK/OFDM) US Time Tunning Register	(Offset 0x8C/8E,16 bits)
//----------------------------------------------------------------------------
#define		SIFS_TRX				0xFF00
#define		SIFS_CTX				0x00FF

//----------------------------------------------------------------------------
//       8192S (DRVERLYINT)	Driver Early Interrupt Reg		(Offset 0x98, 16bit)
//----------------------------------------------------------------------------
#define		ENSWBCN					BIT15
#define		DRVERLY_TU				0x0FF0
#define		DRVERLY_US				0x000F
#define		BCN_TCFG_CW_SHIFT		8
#define		BCN_TCFG_IFS			0

//
// 5. FIFO Control Registers	 (Offset: 0x00A0 - 0x015F)
//

//
// 6. Adaptive Control Registers  (Offset: 0x0160 - 0x01CF)
//
//----------------------------------------------------------------------------
//       8192S Response Rate Set Register	(offset 0x181, 24bits)
//----------------------------------------------------------------------------
#define		RRSR_RSC_OFFSET			21
#define		RRSR_SHORT_OFFSET		23
#define		RRSR_RSC_BW_40M		0x600000
#define		RRSR_RSC_UPSUBCHNL		0x400000
#define		RRSR_RSC_LOWSUBCHNL		0x200000
#define		RRSR_SHORT				0x800000
#define		RRSR_1M					BIT0
#define		RRSR_2M					BIT1
#define		RRSR_5_5M				BIT2
#define		RRSR_11M				BIT3
#define		RRSR_6M					BIT4
#define		RRSR_9M					BIT5
#define		RRSR_12M				BIT6
#define		RRSR_18M				BIT7
#define		RRSR_24M				BIT8
#define		RRSR_36M				BIT9
#define		RRSR_48M				BIT10
#define		RRSR_54M				BIT11
#define		RRSR_MCS0				BIT12
#define		RRSR_MCS1				BIT13
#define		RRSR_MCS2				BIT14
#define		RRSR_MCS3				BIT15
#define		RRSR_MCS4				BIT16
#define		RRSR_MCS5				BIT17
#define		RRSR_MCS6				BIT18
#define		RRSR_MCS7				BIT19
#define		BRSR_AckShortPmb		BIT23

#define 		RRSR_RSC_UPSUBCHANL	0x200000
// CCK ACK: use Short Preamble or not

//----------------------------------------------------------------------------
//       8192S Rate Definition
//----------------------------------------------------------------------------
//CCK
#define		RATR_1M					0x00000001
#define		RATR_2M					0x00000002
#define		RATR_55M				0x00000004
#define		RATR_11M				0x00000008
//OFDM
#define		RATR_6M					0x00000010
#define		RATR_9M					0x00000020
#define		RATR_12M				0x00000040
#define		RATR_18M				0x00000080
#define		RATR_24M				0x00000100
#define		RATR_36M				0x00000200
#define		RATR_48M				0x00000400
#define		RATR_54M				0x00000800
//MCS 1 Spatial Stream
#define		RATR_MCS0				0x00001000
#define		RATR_MCS1				0x00002000
#define		RATR_MCS2				0x00004000
#define		RATR_MCS3				0x00008000
#define		RATR_MCS4				0x00010000
#define		RATR_MCS5				0x00020000
#define		RATR_MCS6				0x00040000
#define		RATR_MCS7				0x00080000
//MCS 2 Spatial Stream
#define		RATR_MCS8				0x00100000
#define		RATR_MCS9				0x00200000
#define		RATR_MCS10				0x00400000
#define		RATR_MCS11				0x00800000
#define		RATR_MCS12				0x01000000
#define		RATR_MCS13				0x02000000
#define		RATR_MCS14				0x04000000
#define		RATR_MCS15				0x08000000
// ALL CCK Rate
#define	RATE_ALL_CCK				RATR_1M|RATR_2M|RATR_55M|RATR_11M
#define	RATE_ALL_OFDM_AG			RATR_6M|RATR_9M|RATR_12M|RATR_18M|RATR_24M|\
									RATR_36M|RATR_48M|RATR_54M
#define	RATE_ALL_OFDM_1SS			RATR_MCS0|RATR_MCS1|RATR_MCS2|RATR_MCS3 |\
									RATR_MCS4|RATR_MCS5|RATR_MCS6	|RATR_MCS7
#define	RATE_ALL_OFDM_2SS			RATR_MCS8|RATR_MCS9	|RATR_MCS10|RATR_MCS11|\
									RATR_MCS12|RATR_MCS13|RATR_MCS14|RATR_MCS15

//
// 7. EDCA Setting Registers	 (Offset: 0x01D0 - 0x01FF)
//
//----------------------------------------------------------------------------
//       8192S EDCA Setting 	(offset 0x1D0-1DF, 4DW VO/VI/BE/BK)
//----------------------------------------------------------------------------
#define		AC_PARAM_TXOP_LIMIT_OFFSET		16
#define		AC_PARAM_ECW_MAX_OFFSET			12
#define		AC_PARAM_ECW_MIN_OFFSET			8
#define		AC_PARAM_AIFS_OFFSET			0

//----------------------------------------------------------------------------
//       8192S AcmHwCtrl bits 					(offset 0x1E7, 1 byte)
//----------------------------------------------------------------------------
#define		AcmHw_HwEn				BIT0
#define		AcmHw_BeqEn				BIT1
#define		AcmHw_ViqEn				BIT2
#define		AcmHw_VoqEn				BIT3
#define		AcmHw_BeqStatus			BIT4
#define		AcmHw_ViqStatus			BIT5
#define		AcmHw_VoqStatus			BIT6

//----------------------------------------------------------------------------
//       8192S Retry Limit					(Offset 0x1F4, 16bit)
//----------------------------------------------------------------------------
#define		RETRY_LIMIT_SHORT_SHIFT	8
#define		RETRY_LIMIT_LONG_SHIFT	0

//
// 8. WMAC, BA and CCX related Register.	 (Offset: 0x0200 - 0x023F)
//
//----------------------------------------------------------------------------
//       8192S NAV_CTRL bits					(Offset 0x200, 24bit)
//----------------------------------------------------------------------------
#define		NAV_UPPER_EN			BIT16
#define		NAV_UPPER				0xFF00
#define		NAV_RTSRST				0xFF
//----------------------------------------------------------------------------
//       8192S BW_OPMODE bits					(Offset 0x203, 8bit)
//----------------------------------------------------------------------------
#define		BW_OPMODE_20MHZ			BIT2
#define		BW_OPMODE_5G			BIT1
#define		BW_OPMODE_11J			BIT0
//----------------------------------------------------------------------------
//       8192S BW_OPMODE bits					(Offset 0x230, 4 Byte)
//----------------------------------------------------------------------------
#define		RXERR_RPT_RST			BIT27 // Write "one" to set the counter to zero.
// RXERR_RPT_SEL
#define		RXERR_OFDM_PPDU			0
#define		RXERR_OFDM_FALSE_ALARM	1
#define		RXERR_OFDM_MPDU_OK		2
#define		RXERR_OFDM_MPDU_FAIL	3
#define		RXERR_CCK_PPDU			4
#define		RXERR_CCK_FALSE_ALARM	5
#define		RXERR_CCK_MPDU_OK		6
#define		RXERR_CCK_MPDU_FAIL		7
#define		RXERR_HT_PPDU			8
#define		RXERR_HT_FALSE_ALARM	9
#define		RXERR_HT_MPDU_TOTAL		10
#define		RXERR_HT_MPDU_OK		11
#define		RXERR_HT_MPDU_FAIL		12
#define		RXERR_RX_FULL_DROP		15

//
// 9. Security Control Registers	(Offset: 0x0240 - 0x025F)
//
//----------------------------------------------------------------------------
//       8192S RWCAM CAM Command Register     		(offset 0x240, 4 byte)
//----------------------------------------------------------------------------
#define		CAM_CM_SecCAMPolling	BIT31		//Security CAM Polling
#define		CAM_CM_SecCAMClr		BIT30		//Clear all bits in CAM
#define		CAM_CM_SecCAMWE			BIT16		//Security CAM enable
#define		CAM_ADDR				0xFF		//CAM Address Offset

//----------------------------------------------------------------------------
//       8192S CAMDBG Debug CAM Register	 			(offset 0x24C, 4 byte)
//----------------------------------------------------------------------------
#define		Dbg_CAM_TXSecCAMInfo	BIT31		//Retrieve lastest Tx Info
#define		Dbg_CAM_SecKeyFound		BIT30		//Security KEY Found


//----------------------------------------------------------------------------
//       8192S SECR Security Configuration Register	(offset 0x250, 1 byte)
//----------------------------------------------------------------------------
#define		SCR_TxUseDK				BIT0			//Force Tx Use Default Key
#define		SCR_RxUseDK				BIT1			//Force Rx Use Default Key
#define		SCR_TxEncEnable			BIT2			//Enable Tx Encryption
#define		SCR_RxDecEnable			BIT3			//Enable Rx Decryption
#define		SCR_SKByA2				BIT4			//Search kEY BY A2
#define		SCR_NoSKMC				BIT5			//No Key Search Multicast
//----------------------------------------------------------------------------
//       8192S CAM Config Setting (offset 0x250, 1 byte)
//----------------------------------------------------------------------------
#define		CAM_VALID				BIT15
#define		CAM_NOTVALID			0x0000
#define		CAM_USEDK				BIT5

#define		CAM_NONE				0x0
#define		CAM_WEP40				0x01
#define		CAM_TKIP				0x02
#define		CAM_AES					0x04
#define		CAM_WEP104				0x05

#define		TOTAL_CAM_ENTRY			32

#define		CAM_CONFIG_USEDK		TRUE
#define		CAM_CONFIG_NO_USEDK		FALSE

#define		CAM_WRITE				BIT16
#define		CAM_READ				0x00000000
#define		CAM_POLLINIG			BIT31

#define		SCR_UseDK				0x01
#define		SCR_TxSecEnable		0x02
#define		SCR_RxSecEnable		0x04

//
// 10. Power Save Control Registers	 (Offset: 0x0260 - 0x02DF)
//
#define		WOW_PMEN				BIT0 // Power management Enable.
#define		WOW_WOMEN			BIT1 // WoW function on or off.
#define		WOW_MAGIC				BIT2 // Magic packet
#define		WOW_UWF				BIT3 // Unicast Wakeup frame.

//
// 11. General Purpose Registers	(Offset: 0x02E0 - 0x02FF)
//       8192S GPIO Config Setting (offset 0x2F1, 1 byte)
//----------------------------------------------------------------------------
#define		GPIOMUX_EN			BIT3 // When this bit is set to "1", GPIO PINs will switch to MAC GPIO Function
#define		GPIOSEL_GPIO		0	// UART or JTAG or pure GPIO
#define		GPIOSEL_PHYDBG		1	// PHYDBG
#define		GPIOSEL_BT			2	// BT_coex
#define		GPIOSEL_WLANDBG		3	// WLANDBG
#define		GPIOSEL_GPIO_MASK	~(BIT0|BIT1)

//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
// PHY REG Access Report Register definition
//----------------------------------------------------------------------------
#define		HST_RDBUSY				BIT0
#define		CPU_WTBUSY			BIT1

//
// 12. Host Interrupt Status Registers	 (Offset: 0x0300 - 0x030F)
//
//----------------------------------------------------------------------------
//       8190 IMR/ISR bits						(offset 0xfd,  8bits)
//----------------------------------------------------------------------------
#define		IMR8190_DISABLED	0x0

// IMR DW1 Bit 0-31
#define		IMR_CPUERR			BIT5		// CPU error interrupt
#define		IMR_ATIMEND			BIT4		// ATIM Window End Interrupt
#define		IMR_TBDOK			BIT3		// Transmit Beacon OK Interrupt
#define		IMR_TBDER			BIT2		// Transmit Beacon Error Interrupt
#define		IMR_BCNDMAINT8		BIT1		// Beacon DMA Interrupt 8
#define		IMR_BCNDMAINT7		BIT0		// Beacon DMA Interrupt 7
// IMR DW0 Bit 0-31

#define		IMR_BCNDMAINT6		BIT31		// Beacon DMA Interrupt 6
#define		IMR_BCNDMAINT5		BIT30		// Beacon DMA Interrupt 5
#define		IMR_BCNDMAINT4		BIT29		// Beacon DMA Interrupt 4
#define		IMR_BCNDMAINT3		BIT28		// Beacon DMA Interrupt 3
#define		IMR_BCNDMAINT2		BIT27		// Beacon DMA Interrupt 2
#define		IMR_BCNDMAINT1		BIT26		// Beacon DMA Interrupt 1
#define		IMR_BCNDOK8			BIT25		// Beacon Queue DMA OK Interrup 8
#define		IMR_BCNDOK7			BIT24		// Beacon Queue DMA OK Interrup 7
#define		IMR_BCNDOK6			BIT23		// Beacon Queue DMA OK Interrup 6
#define		IMR_BCNDOK5			BIT22		// Beacon Queue DMA OK Interrup 5
#define		IMR_BCNDOK4			BIT21		// Beacon Queue DMA OK Interrup 4
#define		IMR_BCNDOK3			BIT20		// Beacon Queue DMA OK Interrup 3
#define		IMR_BCNDOK2			BIT19		// Beacon Queue DMA OK Interrup 2
#define		IMR_BCNDOK1			BIT18		// Beacon Queue DMA OK Interrup 1
#define		IMR_TIMEOUT2		BIT17		// Timeout interrupt 2
#define		IMR_TIMEOUT1		BIT16		// Timeout interrupt 1
#define		IMR_TXFOVW			BIT15		// Transmit FIFO Overflow
#define		IMR_PSTIMEOUT		BIT14		// Power save time out interrupt
#define		IMR_BcnInt			BIT13		// Beacon DMA Interrupt 0
#define		IMR_RXFOVW			BIT12		// Receive FIFO Overflow
#define		IMR_RDU				BIT11		// Receive Descriptor Unavailable
#define		IMR_RXCMDOK			BIT10		// Receive Command Packet OK
#define		IMR_BDOK			BIT9		// Beacon Queue DMA OK Interrup
#define		IMR_HIGHDOK			BIT8		// High Queue DMA OK Interrupt
#define		IMR_COMDOK			BIT7		// Command Queue DMA OK Interrupt
#define		IMR_MGNTDOK			BIT6		// Management Queue DMA OK Interrupt
#define		IMR_HCCADOK			BIT5		// HCCA Queue DMA OK Interrupt
#define		IMR_BKDOK			BIT4		// AC_BK DMA OK Interrupt
#define		IMR_BEDOK			BIT3		// AC_BE DMA OK Interrupt
#define		IMR_VIDOK			BIT2		// AC_VI DMA OK Interrupt
#define		IMR_VODOK			BIT1		// AC_VO DMA Interrupt
#define		IMR_ROK				BIT0		// Receive DMA OK Interrupt

//
// 13. Test Mode and Debug Control Registers	(Offset: 0x0310 - 0x034F)
//

//
// 14. PCIE config register	(Offset 0x500-)
//
//----------------------------------------------------------------------------
//       8190 TPPool bits 					(offset 0xd9, 2 byte)
//----------------------------------------------------------------------------
#define		TPPoll_BKQ			BIT0			// BK queue polling
#define		TPPoll_BEQ			BIT1			// BE queue polling
#define		TPPoll_VIQ			BIT2			// VI queue polling
#define		TPPoll_VOQ			BIT3			// VO queue polling
#define		TPPoll_BQ			BIT4			// Beacon queue polling
#define		TPPoll_CQ			BIT5			// Command queue polling
#define		TPPoll_MQ			BIT6			// Management queue polling
#define		TPPoll_HQ			BIT7			// High queue polling
#define		TPPoll_HCCAQ		BIT8			// HCCA queue polling
#define		TPPoll_StopBK		BIT9			// Stop BK queue
#define		TPPoll_StopBE		BIT10			// Stop BE queue
#define		TPPoll_StopVI		BIT11			// Stop VI queue
#define		TPPoll_StopVO		BIT12			// Stop VO queue
#define		TPPoll_StopMgt		BIT13			// Stop Mgnt queue
#define		TPPoll_StopHigh		BIT14			// Stop High queue
#define		TPPoll_StopHCCA		BIT15			// Stop HCCA queue
#define		TPPoll_SHIFT		8				// Queue ID mapping

//----------------------------------------------------------------------------
//       8192S PCIF 							(Offset 0x500, 32bit)
//----------------------------------------------------------------------------
#define		MXDMA2_16bytes		0x000
#define		MXDMA2_32bytes		0x001
#define		MXDMA2_64bytes		0x010
#define		MXDMA2_128bytes		0x011
#define		MXDMA2_256bytes		0x100
#define		MXDMA2_512bytes		0x101
#define		MXDMA2_1024bytes	0x110
#define		MXDMA2_NoLimit		0x7

#define		MULRW_SHIFT			3
#define		MXDMA2_RX_SHIFT		4
#define		MXDMA2_TX_SHIFT		0

//----------------------------------------------------------------------------
//       8190 CCX_COMMAND_REG Setting (offset 0x25A, 1 byte)
//----------------------------------------------------------------------------
#define		CCX_CMD_CLM_ENABLE				BIT0	// Enable Channel Load
#define		CCX_CMD_NHM_ENABLE				BIT1	// Enable Noise Histogram
#define		CCX_CMD_FUNCTION_ENABLE			BIT8
// CCX function (Channel Load/RPI/Noise Histogram).
#define		CCX_CMD_IGNORE_CCA				BIT9
// Treat CCA period as IDLE time for NHM.
#define		CCX_CMD_IGNORE_TXON				BIT10
// Treat TXON period as IDLE time for NHM.
#define		CCX_CLM_RESULT_READY			BIT16
// 1: Indicate the result of Channel Load is ready.
#define		CCX_NHM_RESULT_READY			BIT16
// 1: Indicate the result of Noise histogram is ready.
#define		CCX_CMD_RESET					0x0
// Clear all the result of CCX measurement and disable the CCX function.


//----------------------------------------------------------------------------
// 8192S EFUSE
//----------------------------------------------------------------------------
//#define		HWSET_MAX_SIZE_92S				128


//----------------------------------------------------------------------------
//       8192S EEPROM/EFUSE share register definition.
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
//       8192S EEROM and Compatible E-Fuse definition. Added by Roger, 2008.10.21.
//----------------------------------------------------------------------------
#define 		RTL8190_EEPROM_ID				0x8129
#define 		EEPROM_HPON					0x02 // LDO settings.
#define 		EEPROM_VID						0x08 // USB Vendor ID.
#define 		EEPROM_PID						0x0A // USB Product ID.
#define 		EEPROM_USB_OPTIONAL			0x0C // For optional function.
#define 		EEPROM_USB_PHY_PARA1			0x0D // For fine tune USB PHY.
#define 		EEPROM_NODE_ADDRESS_BYTE_0	0x12 // MAC address.
#define 		EEPROM_TxPowerDiff				0x1F

#define 		EEPROM_Version					0x50
#define 		EEPROM_ChannelPlan				0x51 // Map of supported channels.
#define		EEPROM_CustomID				0x52
#define 		EEPROM_SubCustomID			0x53 // Reserved  for customer use.


	// <Roger_Notes> The followin are for different version of EEPROM contents purpose. 2008.11.22.
#define 		EEPROM_BoardType				0x54 //0x0: RTL8188SU, 0x1: RTL8191SU, 0x2: RTL8192SU, 0x3: RTL8191GU
#define		EEPROM_TxPwIndex				0x55 //0x55-0x66, Tx Power index.
#define 		EEPROM_PwDiff					0x67 // Difference of gain index between legacy and high throughput OFDM.
#define 		EEPROM_ThermalMeter			0x68 // Thermal meter default value.
#define 		EEPROM_CrystalCap				0x69 // Crystal Cap.
#define 		EEPROM_TxPowerBase			0x6a // Tx Power of serving station.
#define 		EEPROM_TSSI_A					0x6b //TSSI value of path A.
#define 		EEPROM_TSSI_B					0x6c //TSSI value of path B.
#define 		EEPROM_TxPwTkMode			0x6d //Tx Power tracking mode.
//#define 		EEPROM_Reserved				0x6e //0x6e-0x7f, reserved.

// 2009/02/09 Cosa Add for SD3 requirement
#define 		EEPROM_TX_PWR_HT20_DIFF		0x6e// HT20 Tx Power Index Difference
#define 		DEFAULT_HT20_TXPWR_DIFF		2	// HT20<->40 default Tx Power Index Difference
#define 		EEPROM_TX_PWR_OFDM_DIFF		0x71// OFDM Tx Power Index Difference
#define 		EEPROM_TX_PWR_BAND_EDGE	0x73// TX Power offset at band-edge channel
#define 		TX_PWR_BAND_EDGE_CHK		0x79// Check if band-edge scheme is enabled
#define		EEPROM_Default_LegacyHTTxPowerDiff	0x3
#define		EEPROM_USB_Default_OPTIONAL_FUNC	0x8
#define		EEPROM_USB_Default_PHY_PARAM		0x0
#define		EEPROM_Default_TSSI			0x0
#define		EEPROM_Default_TxPwrTkMode	0x0
#define 		EEPROM_Default_TxPowerDiff		0x0
#define 		EEPROM_Default_TxPowerBase	0x0
#define 		EEPROM_Default_ThermalMeter	0x7
#define 		EEPROM_Default_PwDiff			0x4
#define 		EEPROM_Default_CrystalCap		0x5
#define 		EEPROM_Default_TxPower		0x1010
#define 		EEPROM_Default_BoardType		0x02 // Default: 2X2, RTL8192SU(QFPN68)
#define		EEPROM_Default_HT2T_TxPwr		0x10
#define		EEPROM_USB_SN					BIT0
#define		EEPROM_USB_REMOTE_WAKEUP	BIT1
#define		EEPROM_USB_DEVICE_PWR		BIT2
#define		EEPROM_EP_NUMBER				(BIT3|BIT4)

#define		EEPROM_CHANNEL_PLAN_FCC				0x0
#define		EEPROM_CHANNEL_PLAN_IC				0x1
#define		EEPROM_CHANNEL_PLAN_ETSI			0x2
#define		EEPROM_CHANNEL_PLAN_SPAIN			0x3
#define		EEPROM_CHANNEL_PLAN_FRANCE			0x4
#define		EEPROM_CHANNEL_PLAN_MKK				0x5
#define		EEPROM_CHANNEL_PLAN_MKK1			0x6
#define		EEPROM_CHANNEL_PLAN_ISRAEL			0x7
#define		EEPROM_CHANNEL_PLAN_TELEC			0x8
#define		EEPROM_CHANNEL_PLAN_GLOBAL_DOMAIN	0x9
#define		EEPROM_CHANNEL_PLAN_WORLD_WIDE_13	0xA
#define		EEPROM_CHANNEL_PLAN_BY_HW_MASK	0x80

#define 		EEPROM_CID_DEFAULT				0x0
#define 		EEPROM_CID_ALPHA				0x1
#define		EEPROM_CID_CAMEO					0X8
#define 		EEPROM_CID_SITECOM				0x9

//#define EEPROM_CID_RUNTOP						0x2
//#define EEPROM_CID_Senao						0x3
//#define EEPROM_CID_TOSHIBA						0x4
//#define EEPROM_CID_NetCore						0x5
#define 		EEPROM_CID_WHQL 				0xFE // added by chiyoko for dtm, 20090108

//-----------------------------------------------------------------
// 0x2c0 FW Command Control register definition, added by Roger, 2008.11.27.
//-----------------------------------------------------------------
#define		FW_DIG_DISABLE				0xfd00cc00
#define		FW_DIG_ENABLE					0xfd000000
#define		FW_DIG_HALT					0xfd000001
#define		FW_DIG_RESUME					0xfd000002
#define		FW_HIGH_PWR_DISABLE			0xfd000008
#define		FW_HIGH_PWR_ENABLE			0xfd000009
#define		FW_TXPWR_TRACK_ENABLE		0xfd000017
#define		FW_TXPWR_TRACK_DISABLE		0xfd000018
#define		FW_RA_RESET					0xfd0000af
#define		FW_RA_ACTIVE					0xfd0000a6
#define		FW_RA_REFRESH					0xfd0000a0
#define		FW_RA_ENABLE_BG				0xfd0000ac
#define		FW_IQK_ENABLE					0xf0000020
#define		FW_IQK_SUCCESS				0x0000dddd
#define		FW_IQK_FAIL					0x0000ffff
#define		FW_OP_FAILURE					0xffffffff
#define		FW_DM_DISABLE					0xfd00aa00
#define		FW_BB_RESET_ENABLE			0xff00000d
#define		FW_BB_RESET_DISABLE			0xff00000e

//
//--------------92SU require delete or move to other place later
//



//
//
// 2008/08/06 MH For share the same 92S source/header files, we copy some
// definition to pass 92SU compiler. But we must delete thm later.
//
//

//============================================================================
//       819xUsb Regsiter offset definition
//============================================================================

//2 define it temp!!!
#define RFPC					0x5F			// Rx FIFO Packet Count
#define RCR_9356SEL			BIT6
#define TCR_LRL_OFFSET		0
#define TCR_SRL_OFFSET		8
#define TCR_MXDMA_OFFSET	21
#define TCR_MXDMA_2048 		7
#define TCR_SAT				BIT24		// Enable Rate depedent ack timeout timer
#define RCR_MXDMA_OFFSET	8
#define RCR_FIFO_OFFSET		13
#define RCR_OnlyErlPkt		BIT31				// Rx Early mode is performed for packet size greater than 1536
#define CWR					0xDC			// Contention window register
#define RetryCTR				0xDE			// Retry Count register


// For backward compatible for 9xUSB
#define		LED1Cfg				UnusedRegister	// LED1 Configuration Register
#define 	LED0Cfg				UnusedRegister	// LED0 Configuration Register
#define 	GPI					UnusedRegister	// LED0 Configuration Register
#define 	BRSR				UnusedRegister	// LED0 Configuration Register
#define 	CPU_GEN				UnusedRegister	// LED0 Configuration Register
#define 	SIFS				UnusedRegister	// LED0 Configuration Register

//----------------------------------------------------------------------------
//       8190 CPU General Register		(offset 0x100, 4 byte)
//----------------------------------------------------------------------------
//#define 	CPU_CCK_LOOPBACK			0x00030000
#define 	CPU_GEN_SYSTEM_RESET		0x00000001
//#define 	CPU_GEN_FIRMWARE_RESET	0x00000008
//#define 	CPU_GEN_BOOT_RDY			0x00000010
//#define 	CPU_GEN_FIRM_RDY			0x00000020
//#define 	CPU_GEN_PUT_CODE_OK		0x00000080
//#define 	CPU_GEN_BB_RST			0x00000100
//#define 	CPU_GEN_PWR_STB_CPU		0x00000004
//#define 	CPU_GEN_NO_LOOPBACK_MSK	0xFFF8FFFF		// Set bit18,17,16 to 0. Set bit19
//#define 	CPU_GEN_NO_LOOPBACK_SET	0x00080000		// Set BIT19 to 1

//----------------------------------------------------------------------------
//       8192S EEROM
//----------------------------------------------------------------------------

//#define RTL8190_EEPROM_ID				0x8129
//#define EEPROM_VID						0x08
//#define EEPROM_PID						0x0A
//#define EEPROM_USB_OPTIONAL			0x0C
//#define EEPROM_NODE_ADDRESS_BYTE_0	0x12
//
//#define EEPROM_TxPowerDiff				0x1F
//#define EEPROM_ThermalMeter			0x20
//#define EEPROM_PwDiff					0x21	//0x21
//#define EEPROM_CrystalCap				0x22	//0x22
//
//#define EEPROM_TxPwIndex_CCK			0x23	//0x23
//#define EEPROM_TxPwIndex_OFDM_24G	0x24	//0x24~0x26
#define EEPROM_TxPwIndex_CCK_V1			0x29	//0x29~0x2B
#define EEPROM_TxPwIndex_OFDM_24G_V1	0x2C	//0x2C~0x2E
#define EEPROM_TxPwIndex_Ver				0x27	//0x27
//
//#define EEPROM_Default_TxPowerDiff		0x0
//#define EEPROM_Default_ThermalMeter	0x7
//#define EEPROM_Default_PwDiff			0x4
//#define EEPROM_Default_CrystalCap		0x5
//#define EEPROM_Default_TxPower			0x1010
//#define EEPROM_Customer_ID				0x7B	//0x7B:CustomerID
//#define EEPROM_Version					0x50	// 0x50
//#define EEPROM_CustomID				0x52
//#define EEPROM_ChannelPlan				0x7c	//0x7C
//#define EEPROM_IC_VER					0x7d	//0x7D
//#define EEPROM_CRC						0x7e	//0x7E~0x7F
//
//
//#define EEPROM_CID_DEFAULT			0x0
//#define EEPROM_CID_CAMEO				0x1
//#define EEPROM_CID_RUNTOP				0x2
//#define EEPROM_CID_Senao				0x3
//#define EEPROM_CID_TOSHIBA				0x4	// Toshiba setting, Merge by Jacken, 2008/01/31
//#define EEPROM_CID_NetCore				0x5


//
//--------------92SU require delete or move to other place later
//

//============================================================
// CCX Related Register
//============================================================
#define		CCX_COMMAND_REG			0x890
// CCX Measurement Command Register. 4 Bytes.
// Bit[0]: R_CLM_En, 1=enable, 0=disable. Enable or disable "Channel Load
// Measurement (CLM)".
// Bit[1]: R_NHM_En, 1=enable, 0=disable. Enable or disalbe "Noise Histogram
// Measurement (NHM)".
// Bit[2~7]: Reserved
// Bit[8]: R_CCX_En: 1=enable, 0=disable. Enable or disable CCX function.
// Note: After clearing this bit, all the result of all NHM_Result and CLM_
// Result are cleared concurrently.
// Bit[9]: R_Ignore_CCA: 1=enable, 0=disable. Enable means that treat CCA
// period as idle time for NHM.
// Bit[10]: R_Ignore_TXON: 1=enable, 0=disable. Enable means that treat TXON
// period as idle time for NHM.
// Bit[11~31]: Reserved.
#define		CLM_PERIOD_REG			0x894
// CCX Measurement Period Register, in unit of 4us. 2 Bytes.
#define		NHM_PERIOD_REG			0x896
// Noise Histogram Measurement Period Register, in unit of 4us. 2Bytes.
#define		NHM_THRESHOLD0			0x898	// Noise Histogram Meashorement0
#define		NHM_THRESHOLD1			0x899	// Noise Histogram Meashorement1
#define		NHM_THRESHOLD2			0x89A	// Noise Histogram Meashorement2
#define		NHM_THRESHOLD3			0x89B	// Noise Histogram Meashorement3
#define		NHM_THRESHOLD4			0x89C	// Noise Histogram Meashorement4
#define		NHM_THRESHOLD5			0x89D	// Noise Histogram Meashorement5
#define		NHM_THRESHOLD6			0x89E	// Noise Histogram Meashorement6
#define		CLM_RESULT_REG			0x8D0
// Channel Load result register. 4 Bytes.
// Bit[0~15]: Total measured duration of CLM. The CCA busy fraction is caculate
// by CLM_RESULT_REG/CLM_PERIOD_REG.
// Bit[16]: Indicate the CLM result is ready.
// Bit[17~31]: Reserved.
#define		NHM_RESULT_REG			0x8D4
// Noise Histogram result register. 4 Bytes.
// Bit[0~15]: Total measured duration of NHM. If R_Ignore_CCA=1 or
// R_Ignore_TXON=1, this value will be less than NHM_PERIOD_REG.
// Bit[16]: Indicate the NHM result is ready.
// Bit[17~31]: Reserved.
#define		NHM_RPI_COUNTER0		0x8D8
// NHM RPI counter0, the fraction of signal strength < NHM_THRESHOLD0.
#define		NHM_RPI_COUNTER1		0x8D9
// NHM RPI counter1, the fraction of signal stren in NHM_THRESH0, NHM_THRESH1
#define		NHM_RPI_COUNTER2		0x8DA
// NHM RPI counter2, the fraction of signal stren in NHM_THRESH2, NHM_THRESH3
#define		NHM_RPI_COUNTER3		0x8DB
// NHM RPI counter3, the fraction of signal stren in NHM_THRESH4, NHM_THRESH5
#define		NHM_RPI_COUNTER4		0x8DC
// NHM RPI counter4, the fraction of signal stren in NHM_THRESH6, NHM_THRESH7
#define		NHM_RPI_COUNTER5		0x8DD
// NHM RPI counter5, the fraction of signal stren in NHM_THRESH8, NHM_THRESH9
#define		NHM_RPI_COUNTER6		0x8DE
// NHM RPI counter6, the fraction of signal stren in NHM_THRESH10, NHM_THRESH11
#define		NHM_RPI_COUNTER7		0x8DF
// NHM RPI counter7, the fraction of signal stren in NHM_THRESH12, NHM_THRESH13

#define HAL_RETRY_LIMIT_INFRA							48
#define HAL_RETRY_LIMIT_AP_ADHOC						7

// HW Readio OFF switch (GPIO BIT)
#define		HAL_8192S_HW_GPIO_OFF_BIT	BIT3
#define		HAL_8192S_HW_GPIO_OFF_MASK	0xF7
#define		HAL_8192S_HW_GPIO_WPS_BIT	BIT4

#endif  //R8192S_HW






























