/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *                                        
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/

#ifndef __RTL8192D_SPEC_H__
#define __RTL8192D_SPEC_H__

#include <drv_conf.h>

#ifndef BIT
#define BIT(x)		(1 << (x))
#endif

#define BIT0		0x00000001
#define BIT1		0x00000002
#define BIT2		0x00000004
#define BIT3		0x00000008
#define BIT4		0x00000010
#define BIT5		0x00000020
#define BIT6		0x00000040
#define BIT7		0x00000080
#define BIT8		0x00000100
#define BIT9		0x00000200
#define BIT10	0x00000400
#define BIT11	0x00000800
#define BIT12	0x00001000
#define BIT13	0x00002000
#define BIT14	0x00004000
#define BIT15	0x00008000
#define BIT16	0x00010000
#define BIT17	0x00020000
#define BIT18	0x00040000
#define BIT19	0x00080000
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


//============================================================
//       8192D Regsiter offset definition
//============================================================


//============================================================
//
//============================================================

//-----------------------------------------------------
//
//	0x0000h ~ 0x00FFh	System Configuration
//
//-----------------------------------------------------
#define REG_SYS_ISO_CTRL			0x0000
#define REG_SYS_FUNC_EN			0x0002
#define REG_APS_FSMCO				0x0004
#define REG_SYS_CLKR				0x0008
#define REG_9346CR					0x000A
#define REG_EE_VPD					0x000C
#define REG_AFE_MISC				0x0010
#define REG_SPS0_CTRL				0x0011
#define REG_POWER_OFF_IN_PROCESS 0x0017
#define REG_SPS_OCP_CFG			0x0018
#define REG_RSV_CTRL				0x001C
#define REG_RF_CTRL					0x001F
#define REG_LDOA15_CTRL			0x0020
#define REG_LDOV12D_CTRL			0x0021
#define REG_LDOHCI12_CTRL			0x0022
#define REG_LPLDO_CTRL				0x0023
#define REG_AFE_XTAL_CTRL			0x0024
#define REG_AFE_PLL_CTRL			0x0028
#define REG_MAC_PHY_CTRL			0x002c //for 92d, DMDP,SMSP,DMSP contrl
#define REG_EFUSE_CTRL				0x0030
#define REG_EFUSE_TEST				0x0034
#define REG_PWR_DATA				0x0038
#define REG_CAL_TIMER				0x003C
#define REG_ACLK_MON				0x003E
#define REG_GPIO_MUXCFG			0x0040
//#define REG_GPIO_MUXCFG				0x0041
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

#define REG_MCUFWDL				0x0080
#ifdef CONFIG_WOWLAN
#define REG_WOWLAN_REASON			0x00FC
#endif // CONFIG_WOWLAN
#define REG_HMEBOX_EXT_0			0x0088
#define REG_HMEBOX_EXT_1			0x008A
#define REG_HMEBOX_EXT_2			0x008C
#define REG_HMEBOX_EXT_3			0x008E

#define REG_BIST_SCAN				0x00D0
#define REG_BIST_RPT				0x00D4
#define REG_BIST_ROM_RPT			0x00D8
#define REG_USB_SIE_INTF			0x00E0
#define REG_PCIE_MIO_INTF			0x00E4
#define REG_PCIE_MIO_INTD			0x00E8
#define REG_HPON_FSM				0x00EC
#define REG_SYS_CFG					0x00F0
#define REG_MAC_PHY_CTRL_NORMAL	0x00f8

#define  REG_MAC0                                	0x0081
#define  REG_MAC1                                	0x0053
#define  FW_MAC0_ready                      	0x18
#define  FW_MAC1_ready                      	0x1A
#define  MAC0_ON                                  	BIT7
#define  MAC1_ON                                  	BIT0
#define  mac0_ready                             	BIT0
#define  mac1_ready                             	BIT0


//-----------------------------------------------------
//
//	0x0100h ~ 0x01FFh	MACTOP General Configuration
//
//-----------------------------------------------------
#define REG_CR						0x0100
#define REG_PBP						0x0104
#define REG_TRXDMA_CTRL			0x010C
#define REG_TRXFF_BNDY				0x0114
#define REG_TRXFF_STATUS			0x0118
#define REG_RXFF_PTR				0x011C
#define REG_HIMR					0x0120
#define REG_HISR					0x0124
#define REG_HIMRE					0x0128
#define REG_HISRE					0x012C
#define REG_CPWM					0x012F
#define REG_FWIMR					0x0130
#define REG_FWISR					0x0134
#define REG_FTIMR					0x0138
#define REG_PKTBUF_DBG_CTRL		0x0140
#define REG_PKTBUF_DBG_DATA_L		0x0144
#define REG_PKTBUF_DBG_DATA_H	0x0148

#define REG_TC0_CTRL				0x0150
#define REG_TC1_CTRL				0x0154
#define REG_TC2_CTRL				0x0158
#define REG_TC3_CTRL				0x015C
#define REG_TC4_CTRL				0x0160
#define REG_TCUNIT_BASE			0x0164
#define REG_MBIST_START			0x0174
#define REG_MBIST_DONE				0x0178
#define REG_MBIST_FAIL				0x017C
#define REG_C2HEVT_MSG_NORMAL	0x01A0
#define REG_C2HEVT_CLEAR			0x01AF
#define REG_C2HEVT_MSG_TEST		0x01B8
#define REG_MCUTST_1				0x01c0
#define REG_FMETHR					0x01C8
#define REG_HMETFR					0x01CC
#define REG_HMEBOX_0				0x01D0
#define REG_HMEBOX_1				0x01D4
#define REG_HMEBOX_2				0x01D8
#define REG_HMEBOX_3				0x01DC

#define REG_LLT_INIT				0x01E0
#define REG_BB_ACCEESS_CTRL		0x01E8
#define REG_BB_ACCESS_DATA		0x01EC


//-----------------------------------------------------
//
//	0x0200h ~ 0x027Fh	TXDMA Configuration
//
//-----------------------------------------------------
#define REG_RQPN					0x0200
#define REG_FIFOPAGE				0x0204
#define REG_TDECTRL					0x0208
#define REG_TXDMA_OFFSET_CHK		0x020C
#define REG_TXDMA_STATUS			0x0210
#define REG_RQPN_NPQ				0x0214

//-----------------------------------------------------
//
//	0x0280h ~ 0x02FFh	RXDMA Configuration
//
//-----------------------------------------------------
#define REG_RXDMA_AGG_PG_TH		0x0280
#define REG_RXPKT_NUM				0x0284
#define REG_RXDMA_STATUS			0x0288


//-----------------------------------------------------
//
//	0x0300h ~ 0x03FFh	PCIe
//
//-----------------------------------------------------
#define	REG_PCIE_CTRL_REG			0x0300
#define	REG_INT_MIG				0x0304	// Interrupt Migration 
#define	REG_BCNQ_DESA				0x0308	// TX Beacon Descriptor Address
#define	REG_HQ_DESA				0x0310	// TX High Queue Descriptor Address
#define	REG_MGQ_DESA				0x0318	// TX Manage Queue Descriptor Address
#define	REG_VOQ_DESA				0x0320	// TX VO Queue Descriptor Address
#define	REG_VIQ_DESA				0x0328	// TX VI Queue Descriptor Address
#define	REG_BEQ_DESA				0x0330	// TX BE Queue Descriptor Address
#define	REG_BKQ_DESA				0x0338	// TX BK Queue Descriptor Address
#define	REG_RX_DESA				0x0340	// RX Queue	Descriptor Address
#define	REG_DBI					0x0348	// Backdoor REG for Access Configuration
//sherry added for DBI Read/Write  20091126
#define	REG_DBI_WDATA				0x0348	// Backdoor REG for Access Configuration
#define  	REG_DBI_RDATA				0x034C //Backdoor REG for Access Configuration
#define	REG_DBI_CTRL				0x0350  //Backdoor REG for Access Configuration
#define	REG_DBI_FLAG				0x0352 //Backdoor REG for Access Configuration#define	REG_MDIO					0x0354	// MDIO for Access PCIE PHY
#define	REG_MDIO					0x0354	// MDIO for Access PCIE PHY
#define	REG_DBG_SEL				0x0360	// Debug Selection Register
#define	REG_PCIE_HRPWM			0x0361	//PCIe RPWM
#define	REG_PCIE_HCPWM			0x0363	//PCIe CPWM
#define	REG_UART_CTRL				0x0364	// UART	Control
#define	REG_UART_TX_DESA			0x0370	// UART TX Descriptor Address
#define	REG_UART_RX_DESA			0x0378	// UART Rx Descriptor Address


// spec version 11
//-----------------------------------------------------
//
//	0x0400h ~ 0x047Fh	Protocol Configuration
//
//-----------------------------------------------------
#define REG_VOQ_INFORMATION			0x0400
#define REG_VIQ_INFORMATION			0x0404
#define REG_BEQ_INFORMATION			0x0408
#define REG_BKQ_INFORMATION			0x040C
#define REG_MGQ_INFORMATION			0x0410
#define REG_HGQ_INFORMATION			0x0414
#define REG_BCNQ_INFORMATION			0x0418


#define REG_CPU_MGQ_INFORMATION		0x041C
#define REG_FWHW_TXQ_CTRL				0x0420
#define REG_HWSEQ_CTRL					0x0423
#define REG_TXPKTBUF_BCNQ_BDNY		0x0424
#define REG_TXPKTBUF_MGQ_BDNY		0x0425
#define REG_LIFETIME_EN				0x0426
#define REG_MULTI_BCNQ_OFFSET			0x0427
#define REG_SPEC_SIFS					0x0428
#define REG_RL							0x042A
#define REG_DARFRC						0x0430
#define REG_RARFRC						0x0438
#define REG_RRSR						0x0440
#define REG_ARFR0						0x0444
#define REG_ARFR1						0x0448
#define REG_ARFR2						0x044C
#define REG_ARFR3						0x0450
#define REG_AGGLEN_LMT					0x0458
#define REG_AMPDU_MIN_SPACE			0x045C
#define REG_TXPKTBUF_WMAC_LBK_BF_HD	0x045D
#define REG_FAST_EDCA_CTRL				0x0460
#define REG_RD_RESP_PKT_TH				0x0463
#define REG_INIRTS_RATE_SEL			0x0480
#define REG_INIDATA_RATE_SEL			0x0484

//#define REG_FW_TSF_SYNC_CNT				0x04A0
#define REG_FW_RESET_TSF_CNT_1				0x05FC
#define REG_FW_RESET_TSF_CNT_0				0x05FD
#define REG_FW_BCN_DIS_CNT				0x05FE

#define REG_POWER_STATUS				0x04A4
#define REG_POWER_STAGE1				0x04B4
#define REG_POWER_STAGE2				0x04B8
#define REG_PKT_VO_VI_LIFE_TIME			0x04C0
#define REG_PKT_BE_BK_LIFE_TIME			0x04C2
#define REG_STBC_SETTING				0x04C4
#define REG_PROT_MODE_CTRL			0x04C8
#define REG_MAX_AGGR_NUM				0x04CA
#define REG_RTS_MAX_AGGR_NUM			0x04CB
#define REG_BAR_MODE_CTRL				0x04CC
#define REG_RA_TRY_RATE_AGG_LMT		0x04CF
#define REG_EARLY_MODE_CONTROL		0x04D0
#define REG_NQOS_SEQ					0x04DC
#define REG_QOS_SEQ					0x04DE
#define REG_NEED_CPU_HANDLE			0x04E0
#define REG_PKT_LOSE_RPT				0x04E1
#define REG_PTCL_ERR_STATUS			0x04E2
#define REG_DUMMY						0x04FC



//-----------------------------------------------------
//
//	0x0500h ~ 0x05FFh	EDCA Configuration
//
//-----------------------------------------------------
#define REG_EDCA_VO_PARAM			0x0500
#define REG_EDCA_VI_PARAM			0x0504
#define REG_EDCA_BE_PARAM			0x0508
#define REG_EDCA_BK_PARAM			0x050C
#define REG_BCNTCFG					0x0510
#define REG_PIFS						0x0512
#define REG_RDG_PIFS				0x0513
#define REG_SIFS_CTX				0x0514
#define REG_SIFS_TRX				0x0516
#define REG_TSFTR_SYN_OFFSET		0x0518
#define REG_AGGR_BREAK_TIME		0x051A
#define REG_SLOT					0x051B
#define REG_TX_PTCL_CTRL			0x0520
#define REG_TXPAUSE					0x0522
#define REG_DIS_TXREQ_CLR			0x0523
#define REG_RD_CTRL					0x0524
#define REG_TBTT_PROHIBIT			0x0540
#define REG_RD_NAV_NXT				0x0544
#define REG_NAV_PROT_LEN			0x0546
#define REG_BCN_CTRL				0x0550
#define REG_BCN_CTRL_1				0x0551
#define REG_MBID_NUM				0x0552
#define REG_DUAL_TSF_RST			0x0553
#define REG_BCN_INTERVAL			0x0554	// The same as REG_MBSSID_BCN_SPACE
#define REG_MBSSID_BCN_SPACE		0x0554
#define REG_DRVERLYINT				0x0558
#define REG_BCNDMATIM				0x0559
#define REG_ATIMWND				0x055A
#define REG_USTIME_TSF				0x055C
#define REG_BCN_MAX_ERR			0x055D
#define REG_RXTSF_OFFSET_CCK		0x055E
#define REG_RXTSF_OFFSET_OFDM		0x055F	
#define REG_TSFTR					0x0560
#define REG_TSFTR1					0x0568
#define REG_INIT_TSFTR				0x0564
#define REG_ATIMWND_1				0x0570
#define REG_PSTIMER					0x0580
#define REG_TIMER0					0x0584
#define REG_TIMER1					0x0588
#define REG_ACMHWCTRL				0x05C0
#define REG_ACMRSTCTRL				0x05C1
#define REG_ACMAVG					0x05C2
#define REG_VO_ADMTIME				0x05C4
#define REG_VI_ADMTIME				0x05C6
#define REG_BE_ADMTIME				0x05C8
#define REG_EDCA_RANDOM_GEN		0x05CC
#define REG_SCH_TXCMD				0x05D0

#define REG_DMC						0x05F0	//Dual MAC Co-Existence Register


//-----------------------------------------------------
//
//	0x0600h ~ 0x07FFh	WMAC Configuration
//
//-----------------------------------------------------
#define REG_APSD_CTRL				0x0600
#define REG_BWOPMODE				0x0603
#define REG_TCR						0x0604
#define REG_RCR						0x0608
#define REG_RX_PKT_LIMIT			0x060C
#define REG_RX_DLK_TIME			0x060D
#define REG_RX_DRVINFO_SZ			0x060F

#define REG_MACID					0x0610
#define REG_BSSID					0x0618
#define REG_MAR						0x0620
#define REG_MBIDCAMCFG				0x0628

#define REG_USTIME_EDCA			0x0638
#define REG_MAC_SPEC_SIFS			0x063A
#define REG_RESP_SIFS_CCK			0x063C
#define REG_RESP_SIFS_OFDM			0x063E
#define REG_ACKTO					0x0640
#define REG_CTS2TO					0x0641
#define REG_EIFS						0x0642


//WMA, BA, CCX
#define REG_NAV_CTRL				0x0650
#define REG_BACAMCMD				0x0654
#define REG_BACAMCONTENT			0x0658
#define REG_LBDLY					0x0660
#define REG_FWDLY					0x0661
#define REG_RXERR_RPT				0x0664
#define REG_WMAC_TRXPTCL_CTL		0x0668


// Security
#define REG_CAMCMD					0x0670
#define REG_CAMWRITE				0x0674
#define REG_CAMREAD				0x0678
#define REG_CAMDBG					0x067C
#define REG_SECCFG					0x0680

// Power
#define REG_WOW_CTRL				0x0690
#define REG_PSSTATUS				0x0691
#define REG_PS_RX_INFO				0x0692
#define REG_LPNAV_CTRL				0x0694
#define REG_WKFMCAM_CMD			0x0698
#define REG_WKFMCAM_RWD			0x069C
#define REG_RXFLTMAP0				0x06A0
#define REG_RXFLTMAP1				0x06A2
#define REG_RXFLTMAP2				0x06A4
#define REG_BCN_PSR_RPT			0x06A8
#define REG_CALB32K_CTRL			0x06AC
#define REG_PKT_MON_CTRL			0x06B4
#define REG_BT_COEX_TABLE			0x06C0
#define REG_WMAC_RESP_TXINFO		0x06D8

#define REG_MACID1					0x0700
#define REG_BSSID1					0x0708

//-----------------------------------------------------
//
//	0xFE00h ~ 0xFE55h	USB Configuration
//
//-----------------------------------------------------
#define REG_USB_INFO				0xFE17
#define REG_USB_SPECIAL_OPTION	0xFE55
#define REG_USB_DMA_AGG_TO		0xFE5B
#define REG_USB_AGG_TO				0xFE5C
#define REG_USB_AGG_TH				0xFE5D

// for 92DU high_Queue low_Queue Normal_Queue select 
#define REG_USB_High_NORMAL_Queue_Select_MAC0  0xFE44
//#define REG_USB_LOW_Queue_Select_MAC0       0xFE45
#define REG_USB_High_NORMAL_Queue_Select_MAC1  0xFE47
//#define REG_USB_LOW_Queue_Select_MAC1       0xFE48

// For test chip
#define REG_TEST_USB_TXQS			0xFE48
#define REG_TEST_SIE_VID			0xFE60		// 0xFE60~0xFE61
#define REG_TEST_SIE_PID			0xFE62		// 0xFE62~0xFE63
#define REG_TEST_SIE_OPTIONAL		0xFE64
#define REG_TEST_SIE_CHIRP_K		0xFE65
#define REG_TEST_SIE_PHY			0xFE66		// 0xFE66~0xFE6B
#define REG_TEST_SIE_MAC_ADDR		0xFE70		// 0xFE70~0xFE75
#define REG_TEST_SIE_STRING		0xFE80		// 0xFE80~0xFEB9


// For normal chip
#define REG_NORMAL_SIE_VID			0xFE60		// 0xFE60~0xFE61
#define REG_NORMAL_SIE_PID			0xFE62		// 0xFE62~0xFE63
#define REG_NORMAL_SIE_OPTIONAL	0xFE64
#define REG_NORMAL_SIE_EP			0xFE65		// 0xFE65~0xFE67
#define REG_NORMAL_SIE_PHY		0xFE68		// 0xFE68~0xFE6B
#define REG_NORMAL_SIE_MAC_ADDR	0xFE70		// 0xFE70~0xFE75
#define REG_NORMAL_SIE_STRING		0xFE80		// 0xFE80~0xFEDF


//-----------------------------------------------------
//
//	Redifine 8192C register definition for compatibility
//
//-----------------------------------------------------

// TODO: use these definition when using REG_xxx naming rule.
// NOTE: DO NOT Remove these definition. Use later.

#define	SYS_ISO_CTRL				REG_SYS_ISO_CTRL	// System Isolation Interface Control.
#define	SYS_FUNC_EN				REG_SYS_FUNC_EN		// System Function Enable.
#define	SYS_CLK						REG_SYS_CLKR
#define	CR9346						REG_9346CR			// 93C46/93C56 Command Register.
#define	EFUSE_CTRL					REG_EFUSE_CTRL		// E-Fuse Control.
#define	EFUSE_TEST					REG_EFUSE_TEST		// E-Fuse Test.
#define	MSR							(REG_CR + 2)		// Media Status register
#define	ISR							REG_HISR
#define	TSFR						REG_TSFTR			// Timing Sync Function Timer Register.

#define	MACIDR0					REG_MACID			// MAC ID Register, Offset 0x0050-0x0053
#define	MACIDR4					(REG_MACID + 4)		// MAC ID Register, Offset 0x0054-0x0055

#define 	PBP							REG_PBP

// Redifine MACID register, to compatible prior ICs.
#define	IDR0						MACIDR0
#define	IDR4						MACIDR4


//
// 9. Security Control Registers	(Offset: )
//
#define	RWCAM						REG_CAMCMD		//IN 8190 Data Sheet is called CAMcmd
#define	WCAMI						REG_CAMWRITE	// Software write CAM input content
#define	RCAMO						REG_CAMREAD		// Software read/write CAM config
#define	CAMDBG						REG_CAMDBG
#define	SECR						REG_SECCFG		//Security Configuration Register

// Unused register
#define	UnusedRegister				0x1BF
#define	DCAM						UnusedRegister
#define	PSR							UnusedRegister
#define 	BBAddr						UnusedRegister
#define	PhyDataR					UnusedRegister

#define	InvalidBBRFValue			0x12345678

// Min Spacing related settings.
#define	MAX_MSS_DENSITY_2T 			0x13
#define	MAX_MSS_DENSITY_1T 			0x0A

//----------------------------------------------------------------------------
//       8192C Cmd9346CR bits					(Offset 0xA, 16bit)
//----------------------------------------------------------------------------
#define	CmdEEPROM_En				BIT5	 // EEPROM enable when set 1
#define	CmdEERPOMSEL				BIT4 	// System EEPROM select, 0: boot from E-FUSE, 1: The EEPROM used is 9346
#define	Cmd9346CR_9356SEL			BIT4
#define	AutoLoadEEPROM			(CmdEEPROM_En|CmdEERPOMSEL)
#define	AutoLoadEFUSE				CmdEEPROM_En

//       8192C GPIO MUX Configuration Register (offset 0x40, 4 byte)
//----------------------------------------------------------------------------
#define	GPIOSEL_GPIO				0
#define	GPIOSEL_ENBT				BIT5

//----------------------------------------------------------------------------
//       8192C GPIO PIN Control Register (offset 0x44, 4 byte)
//----------------------------------------------------------------------------
#define	GPIO_IN						REG_GPIO_PIN_CTRL		// GPIO pins input value
#define	GPIO_OUT					(REG_GPIO_PIN_CTRL+1)	// GPIO pins output value
#define	GPIO_IO_SEL				(REG_GPIO_PIN_CTRL+2)	// GPIO pins output enable when a bit is set to "1"; otherwise, input is configured.
#define	GPIO_MOD					(REG_GPIO_PIN_CTRL+3)


//----------------------------------------------------------------------------
//       8192C (MSR) Media Status Register	(Offset 0x4C, 8 bits)  
//----------------------------------------------------------------------------
/*
Network Type
00: No link
01: Link in ad hoc network
10: Link in infrastructure network
11: AP mode
Default: 00b.
*/
#define	MSR_NOLINK					0x00
#define	MSR_ADHOC					0x01
#define	MSR_INFRA					0x02
#define	MSR_AP						0x03

//
// 6. Adaptive Control Registers  (Offset: 0x0160 - 0x01CF)
//
//----------------------------------------------------------------------------
//       8192C Response Rate Set Register	(offset 0x181, 24bits)
//----------------------------------------------------------------------------
#define	RRSR_RSC_OFFSET			21
#define	RRSR_SHORT_OFFSET			23
#define	RRSR_RSC_BW_40M			0x600000
#define	RRSR_RSC_UPSUBCHNL		0x400000
#define	RRSR_RSC_LOWSUBCHNL		0x200000
#define	RRSR_SHORT					0x800000
#define	RRSR_1M					BIT0
#define	RRSR_2M					BIT1 
#define	RRSR_5_5M					BIT2 
#define	RRSR_11M					BIT3 
#define	RRSR_6M					BIT4 
#define	RRSR_9M					BIT5 
#define	RRSR_12M					BIT6 
#define	RRSR_18M					BIT7 
#define	RRSR_24M					BIT8 
#define	RRSR_36M					BIT9 
#define	RRSR_48M					BIT10 
#define	RRSR_54M					BIT11
#define	RRSR_MCS0					BIT12
#define	RRSR_MCS1					BIT13
#define	RRSR_MCS2					BIT14
#define	RRSR_MCS3					BIT15
#define	RRSR_MCS4					BIT16
#define	RRSR_MCS5					BIT17
#define	RRSR_MCS6					BIT18
#define	RRSR_MCS7					BIT19
#define	BRSR_AckShortPmb			BIT23	
// CCK ACK: use Short Preamble or not


//----------------------------------------------------------------------------
//       8192C Rate Definition
//----------------------------------------------------------------------------
//CCK
#define	RATR_1M					0x00000001
#define	RATR_2M					0x00000002
#define	RATR_55M					0x00000004
#define	RATR_11M					0x00000008
//OFDM 		
#define	RATR_6M					0x00000010
#define	RATR_9M					0x00000020
#define	RATR_12M					0x00000040
#define	RATR_18M					0x00000080
#define	RATR_24M					0x00000100
#define	RATR_36M					0x00000200
#define	RATR_48M					0x00000400
#define	RATR_54M					0x00000800
//MCS 1 Spatial Stream	
#define	RATR_MCS0					0x00001000
#define	RATR_MCS1					0x00002000
#define	RATR_MCS2					0x00004000
#define	RATR_MCS3					0x00008000
#define	RATR_MCS4					0x00010000
#define	RATR_MCS5					0x00020000
#define	RATR_MCS6					0x00040000
#define	RATR_MCS7					0x00080000
//MCS 2 Spatial Stream
#define	RATR_MCS8					0x00100000
#define	RATR_MCS9					0x00200000
#define	RATR_MCS10					0x00400000
#define	RATR_MCS11					0x00800000
#define	RATR_MCS12					0x01000000
#define	RATR_MCS13					0x02000000
#define	RATR_MCS14					0x04000000
#define	RATR_MCS15					0x08000000

//----------------------------------------------------------------------------
//       8192C BW_OPMODE bits					(Offset 0x203, 8bit)
//----------------------------------------------------------------------------
#define	BW_OPMODE_20MHZ			BIT2
#define	BW_OPMODE_5G				BIT1
#define	BW_OPMODE_11J				BIT0


//----------------------------------------------------------------------------
//       8192C CAM Config Setting (offset 0x250, 1 byte)
//----------------------------------------------------------------------------
#define	CAM_VALID					BIT15
#define	CAM_NOTVALID				0x0000
#define	CAM_USEDK					BIT5

#define	CAM_CONTENT_COUNT 		8

#define	CAM_NONE					0x0
#define	CAM_WEP40					0x01
#define	CAM_TKIP					0x02
#define	CAM_AES					0x04
#define	CAM_WEP104				0x05
#define	CAM_SMS4					0x6


#define	TOTAL_CAM_ENTRY			32
#define	HALF_CAM_ENTRY			16	
       		
#define	CAM_CONFIG_USEDK			_TRUE
#define	CAM_CONFIG_NO_USEDK		_FALSE
       		
#define	CAM_WRITE					BIT16
#define	CAM_READ					0x00000000
#define	CAM_POLLINIG				BIT31

#define	SCR_UseDK					0x01
#define	SCR_TxSecEnable			0x02
#define	SCR_RxSecEnable			0x04


//
// 12. Host Interrupt Status Registers	 (Offset: 0x0300 - 0x030F)
//
//----------------------------------------------------------------------------
//       8190 IMR/ISR bits						(offset 0xfd,  8bits)
//----------------------------------------------------------------------------
#define	IMR8190_DISABLED			0x0
// IMR DW0 Bit 0-31
#define	IMR_BCNDMAINT6			BIT31		// Beacon DMA Interrupt 6
#define	IMR_BCNDMAINT5			BIT30		// Beacon DMA Interrupt 5
#define	IMR_BCNDMAINT4			BIT29		// Beacon DMA Interrupt 4
#define	IMR_BCNDMAINT3			BIT28		// Beacon DMA Interrupt 3
#define	IMR_BCNDMAINT2			BIT27		// Beacon DMA Interrupt 2
#define	IMR_BCNDMAINT1			BIT26		// Beacon DMA Interrupt 1
#define	IMR_BCNDOK8				BIT25		// Beacon Queue DMA OK Interrup 8
#define	IMR_BCNDOK7				BIT24		// Beacon Queue DMA OK Interrup 7
#define	IMR_BCNDOK6				BIT23		// Beacon Queue DMA OK Interrup 6
#define	IMR_BCNDOK5				BIT22		// Beacon Queue DMA OK Interrup 5
#define	IMR_BCNDOK4				BIT21		// Beacon Queue DMA OK Interrup 4
#define	IMR_BCNDOK3				BIT20		// Beacon Queue DMA OK Interrup 3
#define	IMR_BCNDOK2				BIT19		// Beacon Queue DMA OK Interrup 2
#define	IMR_BCNDOK1				BIT18		// Beacon Queue DMA OK Interrup 1
#define	IMR_TIMEOUT2				BIT17		// Timeout interrupt 2
#define	IMR_TIMEOUT1				BIT16		// Timeout interrupt 1
#define	IMR_TXFOVW				BIT15		// Transmit FIFO Overflow
#define	IMR_PSTIMEOUT				BIT14		// Power save time out interrupt 
#define	IMR_BcnInt					BIT13		// Beacon DMA Interrupt 0
#define	IMR_RXFOVW				BIT12		// Receive FIFO Overflow
#define	IMR_RDU					BIT11		// Receive Descriptor Unavailable
#define	IMR_ATIMEND				BIT10		// For 92C,ATIM Window End Interrupt
#define	IMR_BDOK					BIT9		// Beacon Queue DMA OK Interrup
#define	IMR_HIGHDOK				BIT8		// High Queue DMA OK Interrupt
#define	IMR_TBDOK					BIT7		// Transmit Beacon OK interrup
#define	IMR_MGNTDOK				BIT6		// Management Queue DMA OK Interrupt
#define	IMR_TBDER					BIT5		// For 92C,Transmit Beacon Error Interrupt
#define	IMR_BKDOK					BIT4		// AC_BK DMA OK Interrupt
#define	IMR_BEDOK					BIT3		// AC_BE DMA OK Interrupt
#define	IMR_VIDOK					BIT2		// AC_VI DMA OK Interrupt
#define	IMR_VODOK					BIT1		// AC_VO DMA Interrupt
#define	IMR_ROK					BIT0		// Receive DMA OK Interrupt

// 13. Host Interrupt Status Extension Register	 (Offset: 0x012C-012Eh)
#define	IMR_TXERR					BIT11
#define	IMR_RXERR					BIT10
#define	IMR_C2HCMD				BIT9
#define	IMR_CPWM					BIT8
//RSVD [2-7]
#define	IMR_OCPINT					BIT1
#define	IMR_WLANOFF				BIT0



//----------------------------------------------------------------------------
// 8192D EFUSE
//----------------------------------------------------------------------------
#define	HWSET_MAX_SIZE				256

//----------------------------------------------------------------------------
//       8192C EEPROM/EFUSE share register definition.
//----------------------------------------------------------------------------

//
// Default Value for EEPROM or EFUSE!!!
//
#define	EEPROM_Default_TSSI					0x0
#define 	EEPROM_Default_TxPowerDiff				0x0
#define 	EEPROM_Default_CrystalCap				0x0		//92D default 0x0
#define 	EEPROM_Default_BoardType				0x02 // Default: 2X2, RTL8192CE(QFPN68)
#define 	EEPROM_Default_TxPower				0x1010
#define	EEPROM_Default_HT2T_TxPwr				0x10

#define	EEPROM_Default_LegacyHTTxPowerDiff	0x4
#define	EEPROM_Default_ThermalMeter			0x12

#define	EEPROM_Default_AntTxPowerDiff			0x0
//#define	EEPROM_Default_TxPwDiff_CrystalCap		0x5
#define	EEPROM_Default_TxPowerLevel_2G		0x2C
#define	EEPROM_Default_TxPowerLevel_5G		0x22

#define	EEPROM_Default_HT40_2SDiff				0x0
#define 	EEPROM_Default_HT20_Diff				2	// HT20<->40 default Tx Power Index Difference
#define	EEPROM_Default_LegacyHTTxPowerDiff	0x4	//OFDM Tx Power index diff
#define 	EEPROM_Default_HT40_PwrMaxOffset		0
#define 	EEPROM_Default_HT20_PwrMaxOffset		0

// For debug
#define 	EEPROM_Default_PID						0x1234
#define 	EEPROM_Default_VID						0x5678
#define 	EEPROM_Default_CustomerID				0xAB
#define 	EEPROM_Default_SubCustomerID			0xCD
#define 	EEPROM_Default_Version					0

#define	EEPROM_Default_externalPA_C9		0x00
#define	EEPROM_Default_externalPA_CC		0xFF
#define	EEPROM_Default_internalPA_SP3T_C9	0xAA
#define	EEPROM_Default_internalPA_SP3T_CC	0xAF
#define	EEPROM_Default_internalPA_SPDT_C9	0xAA
#ifdef CONFIG_PCI_HCI
#define	EEPROM_Default_internalPA_SPDT_CC	0xA0
#else
#define	EEPROM_Default_internalPA_SPDT_CC	0xFA
#endif

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
#define	EEPROM_CHANNEL_PLAN_BY_HW_MASK	0x80


#define 	EEPROM_CID_DEFAULT					0x0
#define 	EEPROM_CID_TOSHIBA					0x4
#define	EEPROM_CID_CCX						0x10 // CCX test. By Bruce, 2009-02-25.
#define	EEPROM_CID_QMI						0x0D
#define 	EEPROM_CID_WHQL 						0xFE // added by chiyoko for dtm, 20090108


#define	RTL8192_EEPROM_ID						0x8129
#define	EEPROM_WAPI_SUPPORT					0x78


#ifdef CONFIG_PCI_HCI
#define RT_IBSS_INT_MASKS				(IMR_BcnInt | IMR_TBDOK | IMR_TBDER)
#define RT_AC_INT_MASKS				(IMR_VIDOK | IMR_VODOK | IMR_BEDOK|IMR_BKDOK)
#define RT_BSS_INT_MASKS				(RT_IBSS_INT_MASKS)

#define RTL8190_EEPROM_ID						0x8129	// 0-1
#define EEPROM_HPON							0x02 // LDO settings.2-5
#define EEPROM_CLK								0x06 // Clock settings.6-7
#define EEPROM_MAC_FUNCTION					0x08 // SE Test mode.8

#define EEPROM_VID								0x28 // SE Vendor ID.A-B
#define EEPROM_DID								0x2A // SE Device ID. C-D
#define EEPROM_SVID								0x2C // SE Vendor ID.E-F
#define EEPROM_SMID							0x2E // SE PCI Subsystem ID. 10-11

#define EEPROM_MAC_ADDR						0x16 // SEMAC Address. 12-17
#define EEPROM_MAC_ADDR_MAC0_92D			0x55
#define EEPROM_MAC_ADDR_MAC1_92D			0x5B
//----------------------------------------------------------------
// 2.4G band Tx power index setting
#define EEPROM_CCK_TX_PWR_INX_2G				0x61
#define EEPROM_HT40_1S_TX_PWR_INX_2G		0x67
#define EEPROM_HT40_2S_TX_PWR_INX_DIFF_2G	0x6D
#define EEPROM_HT20_TX_PWR_INX_DIFF_2G		0x70
#define EEPROM_OFDM_TX_PWR_INX_DIFF_2G		0x73
#define EEPROM_HT40_MAX_PWR_OFFSET_2G		0x76
#define EEPROM_HT20_MAX_PWR_OFFSET_2G		0x79

//5GL channel 32-64
#define EEPROM_HT40_1S_TX_PWR_INX_5GL		0x7C
#define EEPROM_HT40_2S_TX_PWR_INX_DIFF_5GL	0x82
#define EEPROM_HT20_TX_PWR_INX_DIFF_5GL		0x85
#define EEPROM_OFDM_TX_PWR_INX_DIFF_5GL		0x88
#define EEPROM_HT40_MAX_PWR_OFFSET_5GL		0x8B
#define EEPROM_HT20_MAX_PWR_OFFSET_5GL		0x8E

//5GM channel 100-140
#define EEPROM_HT40_1S_TX_PWR_INX_5GM		0x91
#define EEPROM_HT40_2S_TX_PWR_INX_DIFF_5GM	0x97
#define EEPROM_HT20_TX_PWR_INX_DIFF_5GM		0x9A
#define EEPROM_OFDM_TX_PWR_INX_DIFF_5GM	0x9D
#define EEPROM_HT40_MAX_PWR_OFFSET_5GM		0xA0
#define EEPROM_HT20_MAX_PWR_OFFSET_5GM		0xA3

//5GH channel 149-165
#define EEPROM_HT40_1S_TX_PWR_INX_5GH		0xA6
#define EEPROM_HT40_2S_TX_PWR_INX_DIFF_5GH	0xAC
#define EEPROM_HT20_TX_PWR_INX_DIFF_5GH		0xAF
#define EEPROM_OFDM_TX_PWR_INX_DIFF_5GH		0xB2
#define EEPROM_HT40_MAX_PWR_OFFSET_5GH		0xB5
#define EEPROM_HT20_MAX_PWR_OFFSET_5GH		0xB8

#define EEPROM_CHANNEL_PLAN					0xBB // Map of supported channels.	
#define EEPROM_IQK_DELTA						0xBC
#define EEPROM_LCK_DELTA						0xBC
#define EEPROM_XTAL_K							0xBD	//[7:5]
#define EEPROM_TSSI_A_5G						0xBE
#define EEPROM_TSSI_B_5G						0xBF
#define EEPROM_TSSI_AB_5G						0xC0
#define EEPROM_THERMAL_METER					0xC3	//[4:0]
#define EEPROM_PATHDIV							0xC4
#define EEPROM_RF_OPT1							0xC4
#define EEPROM_RF_OPT2							0xC5
#define EEPROM_RF_OPT3							0xC6
#define EEPROM_RF_OPT4							0xC7
#define EEPROM_RF_OPT5							0xC8
#define EEPROM_RF_OPT6							0xC9
#define EEPROM_VERSION							0xCA
#define EEPROM_CUSTOMER_ID					0xCB
#define EEPROM_RF_OPT7							0xCC

#define EEPROM_WIDIPAIRING_ADDR				0xF0
#define EEPROM_WIDIPAIRING_KEY				0xF6

#define EEPROM_DEF_PART_NO					0x3FD  //Byte
#define EEPROME_CHIP_VERSION_L				0x3FF
#define EEPROME_CHIP_VERSION_H				0x3FE
#endif 

#ifdef CONFIG_USB_HCI
#define RTL8190_EEPROM_ID						0x8129	// 0-1
#define EEPROM_HPON							0x02 // LDO settings.2-5
#define EEPROM_CLK								0x06 // Clock settings.6-7
#define EEPROM_MAC_FUNCTION					0x08 // SE Test mode.8

#define EEPROM_VID								0xC // SE Vendor ID.A-B
#define EEPROM_PID								0xE // SE Device ID. C-D
#define EEPROM_ENDPOINT_SETTING				0x10
#ifdef CONFIG_WOWLAN
#define EEPROM_Option_Setting				0x11
#endif // CONFIG_WOWLAN
#define EEPROM_CHIRP_K							0x12	// Changed
#define EEPROM_USB_PHY							0x13	// Changed
#define EEPROM_NORMAL_BoardType				EEPROM_RF_OPT1	//[7:5]
#define EEPROM_MAC_ADDR						0x16 // SEMAC Address. 12-17
#define EEPROM_STRING							0x1F
#define EEPROM_SUBCUSTOMER_ID					0x59

#define EEPROM_MAC_ADDR_MAC0_92D			0x19
#define EEPROM_MAC_ADDR_MAC1_92D			0x5B
//----------------------------------------------------------------
// 2.4G band Tx power index setting
#define EEPROM_CCK_TX_PWR_INX_2G				0x61
#define EEPROM_HT40_1S_TX_PWR_INX_2G		0x67
#define EEPROM_HT40_2S_TX_PWR_INX_DIFF_2G	0x6D
#define EEPROM_HT20_TX_PWR_INX_DIFF_2G		0x70
#define EEPROM_OFDM_TX_PWR_INX_DIFF_2G		0x73
#define EEPROM_HT40_MAX_PWR_OFFSET_2G		0x76
#define EEPROM_HT20_MAX_PWR_OFFSET_2G		0x79

//5GL channel 32-64
#define EEPROM_HT40_1S_TX_PWR_INX_5GL		0x7C
#define EEPROM_HT40_2S_TX_PWR_INX_DIFF_5GL	0x82
#define EEPROM_HT20_TX_PWR_INX_DIFF_5GL		0x85
#define EEPROM_OFDM_TX_PWR_INX_DIFF_5GL		0x88
#define EEPROM_HT40_MAX_PWR_OFFSET_5GL		0x8B
#define EEPROM_HT20_MAX_PWR_OFFSET_5GL		0x8E

//5GM channel 100-140
#define EEPROM_HT40_1S_TX_PWR_INX_5GM		0x91
#define EEPROM_HT40_2S_TX_PWR_INX_DIFF_5GM	0x97
#define EEPROM_HT20_TX_PWR_INX_DIFF_5GM		0x9A
#define EEPROM_OFDM_TX_PWR_INX_DIFF_5GM	0x9D
#define EEPROM_HT40_MAX_PWR_OFFSET_5GM		0xA0
#define EEPROM_HT20_MAX_PWR_OFFSET_5GM		0xA3

//5GH channel 149-165
#define EEPROM_HT40_1S_TX_PWR_INX_5GH		0xA6
#define EEPROM_HT40_2S_TX_PWR_INX_DIFF_5GH	0xAC
#define EEPROM_HT20_TX_PWR_INX_DIFF_5GH		0xAF
#define EEPROM_OFDM_TX_PWR_INX_DIFF_5GH		0xB2
#define EEPROM_HT40_MAX_PWR_OFFSET_5GH		0xB5
#define EEPROM_HT20_MAX_PWR_OFFSET_5GH		0xB8

#define EEPROM_CHANNEL_PLAN					0xBB // Map of supported channels.	
#define EEPROM_TEST_CHANNEL_PLAN				0xBB
#define EEPROM_IQK_DELTA						0xBC
#define EEPROM_LCK_DELTA						0xBC
#define EEPROM_XTAL_K							0xBD	//[7:5]
#define EEPROM_TSSI_A_5G						0xBE
#define EEPROM_TSSI_B_5G						0xBF
#define EEPROM_TSSI_AB_5G						0xC0
#define EEPROM_THERMAL_METER					0xC3	//[4:0]
#define EEPROM_RF_OPT1							0xC4
#define EEPROM_RF_OPT2							0xC5
#define EEPROM_RF_OPT3							0xC6
#define EEPROM_RF_OPT4							0xC7
#define EEPROM_RF_OPT5							0xC8
#define EEPROM_RF_OPT6							0xC9
#define EEPROM_VERSION							0xCA
#define EEPROM_CUSTOMER_ID					0xCB
#define EEPROM_RF_OPT7							0xCC

#define EEPROM_DEF_PART_NO					0x3FD  //Byte
#define EEPROME_CHIP_VERSION_L				0x3FF
#define EEPROME_CHIP_VERSION_H				0x3FE

//-------------------------------------------------------------
//	EEPROM content definitions
//-------------------------------------------------------------
#define OS_LINK_SPEED_NORMAL_MASK			BIT3 | BIT2
#define OS_LINK_SPEED_TEST_MASK				BIT3 | BIT4

#define BOARD_TYPE_NORMAL_MASK				0xE0
#define BOARD_TYPE_TEST_MASK					0xF

#define BT_COEXISTENCE_TEST					BIT4
#define BT_COEXISTENCE_NORMAL					BIT5

#define BT_CO_SHIFT_TEST						4
#define BT_CO_SHIFT_NORMAL					5

#define EP_NUMBER_MASK_TEST					0x30	//bit 4:5 0Eh
#define EP_NUMBER_SHIFT_TEST					4

#define USB_PHY_PARA_SIZE_TEST				6
#define USB_PHY_PARA_SIZE_NORMAL				4

//-------------------------------------------------------------
//	EEPROM default value definitions
//-------------------------------------------------------------
// Use 0xABCD instead of 0x8192 for debug
#define	EEPROM_DEF_ID_0						0xCD	// Byte 0x00
#define	EEPROM_DEF_ID_1						0xAB	// Byte 0x01

#define EEPROM_DEF_RTK_RSV_A3					0x74	// Byte 0x03
#define EEPROM_DEF_RTK_RSV_A4					0x6D	// Byte 0x04
#define EEPROM_DEF_RTK_RSV_A8					0xFF	// Byte 0x08

#define EEPROM_DEF_VID_0						0x0A	// Byte 0x0A
#define EEPROM_DEF_VID_1						0x0B

#define EEPROM_DEF_PID_0						0x92	// Byte 0x0C
#define EEPROM_DEF_PID_1						0x81


#define EEPROM_TEST_DEF_USB_OPT				0x80	// Byte 0x0E
#define EEPROM_NORMAL_DEF_USB_OPT			0x00	// Byte 0x0E

#define EEPROM_DEF_CHIRPK						0x15	// Byte 0x0F

#define EEPROM_DEF_USB_PHY_0					0x85	// Byte 0x10
#define EEPROM_DEF_USB_PHY_1					0x62	// Byte 0x11
#define EEPROM_DEF_USB_PHY_2					0x9E	// Byte 0x12
#define EEPROM_DEF_USB_PHY_3					0x06	// Byte 0x13

#define EEPROM_DEF_TSSI_A						0x09	// Byte 0x78
#define EEPROM_DEF_TSSI_B						0x09	// Byte 0x79


#define EEPROM_DEF_THERMAL_METER				0x12	// Byte 0x7A


#define	EEPROM_USB_SN							BIT(0)
#define	EEPROM_USB_REMOTE_WAKEUP			BIT(1)
#define	EEPROM_USB_DEVICE_PWR				BIT(2)
#define	EEPROM_EP_NUMBER						(BIT(3)|BIT(4))

#if 0
#define	EEPROM_CHANNEL_PLAN_FCC					0x0
#define	EEPROM_CHANNEL_PLAN_IC					0x1
#define	EEPROM_CHANNEL_PLAN_ETSI				0x2
#define	EEPROM_CHANNEL_PLAN_SPAIN				0x3
#define	EEPROM_CHANNEL_PLAN_FRANCE				0x4
#define	EEPROM_CHANNEL_PLAN_MKK					0x5
#define	EEPROM_CHANNEL_PLAN_MKK1				0x6
#define	EEPROM_CHANNEL_PLAN_ISRAEL				0x7
#define	EEPROM_CHANNEL_PLAN_TELEC				0x8
#define	EEPROM_CHANNEL_PLAN_GLOBAL_DOMAIN		0x9
#define	EEPROM_CHANNEL_PLAN_WORLD_WIDE_13		0xA
#define	EEPROM_CHANNEL_PLAN_BY_HW_MASK			0x80

#define	EEPROM_CID_DEFAULT						0x0

#define	EEPROM_CID_WHQL 						0xFE // added by chiyoko for dtm, 20090108


#define	EEPROM_CID_CCX							0x10 // CCX test. By Bruce, 2009-02-25.

#endif

//should be renamed and moved to another file
typedef	enum _BOARD_TYPE_8192DUSB{
	BOARD_USB_DONGLE 			= 0,		// USB dongle
	BOARD_USB_High_PA 		= 1,		// USB dongle with high power PA
	BOARD_MINICARD		  	= 2,		// Minicard
	BOARD_USB_SOLO 		 	= 3,		// USB solo-Slim module
	BOARD_USB_COMBO			= 4,		// USB Combo-Slim module
} BOARD_TYPE_8192DUSB, *PBOARD_TYPE_8192DUSB;
#endif


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

//----------------------------------------------------------------------------
//       8192C (RCR) Receive Configuration Register	(Offset 0x608, 32 bits)
//----------------------------------------------------------------------------
#define	RCR_APPFCS					BIT31		//WMAC append FCS after pauload
#define	RCR_APP_MIC				BIT30		//
#define	RCR_APP_ICV				BIT29       //
#define	RCR_APP_PHYST_RXFF		BIT28       //
#define	RCR_APP_BA_SSN			BIT27		//Accept BA SSN
#define	RCR_ENMBID					BIT24		//Enable Multiple BssId.
#define	RCR_LSIGEN					BIT23
#define	RCR_MFBEN					BIT22
#define	RCR_HTC_LOC_CTRL			BIT14       //MFC<--HTC=1 MFC-->HTC=0
#define	RCR_AMF					BIT13		//Accept management type frame
#define	RCR_ACF					BIT12		//Accept control type frame
#define	RCR_ADF					BIT11		//Accept data type frame
#define	RCR_AICV					BIT9		//Accept ICV error packet
#define	RCR_ACRC32					BIT8		//Accept CRC32 error packet 
#define	RCR_CBSSID_BCN			BIT7		//Accept BSSID match packet (Rx beacon, probe rsp)
#define	RCR_CBSSID_DATA			BIT6		//Accept BSSID match packet (Data)
#define	RCR_CBSSID					RCR_CBSSID_DATA		//Accept BSSID match packet
#define	RCR_APWRMGT				BIT5		//Accept power management packet
#define	RCR_ADD3					BIT4		//Accept address 3 match packet
#define	RCR_AB						BIT3		//Accept broadcast packet 
#define	RCR_AM						BIT2		//Accept multicast packet 
#define	RCR_APM					BIT1		//Accept physical match packet
#define	RCR_AAP					BIT0		//Accept all unicast packet 
#define	RCR_MXDMA_OFFSET			8
#define	RCR_FIFO_OFFSET			13



//============================================================================
//       8192c USB specific Regsiter Offset and Content definition, 
//       2009.08.18, added by vivi. for merge 92c and 92C into one driver
//============================================================================
//#define APS_FSMCO				0x0004  same with 92Ce
#define RSV_CTRL					0x001C
#define RD_CTRL					0x0524

//-----------------------------------------------------
//
//	0xFE00h ~ 0xFE55h	USB Configuration
//
//-----------------------------------------------------
#define REG_USB_INFO				0xFE17
#define REG_USB_SPECIAL_OPTION	0xFE55
#define REG_USB_DMA_AGG_TO		0xFE5B
#define REG_USB_AGG_TO				0xFE5C
#define REG_USB_AGG_TH				0xFE5D

#define REG_USB_VID					0xFE60
#define REG_USB_PID					0xFE62
#define REG_USB_OPTIONAL			0xFE64
#define REG_USB_CHIRP_K			0xFE65
#define REG_USB_PHY					0xFE66
#define REG_USB_MAC_ADDR			0xFE70

#define REG_USB_HRPWM				0xFE58
#define REG_USB_HCPWM				0xFE57

#define InvalidBBRFValue			0x12345678

//============================================================================
//       8192C Regsiter Bit and Content definition 
//============================================================================
//-----------------------------------------------------
//
//	0x0000h ~ 0x00FFh	System Configuration
//
//-----------------------------------------------------

//2 SPS0_CTRL
#define SW18_FPWM					BIT(3)


//2 SYS_ISO_CTRL
#define ISO_MD2PP					BIT(0)
#define ISO_UA2USB					BIT(1)
#define ISO_UD2CORE					BIT(2)
#define ISO_PA2PCIE					BIT(3)
#define ISO_PD2CORE					BIT(4)
#define ISO_IP2MAC					BIT(5)
#define ISO_DIOP						BIT(6)
#define ISO_DIOE						BIT(7)
#define ISO_EB2CORE					BIT(8)
#define ISO_DIOR						BIT(9)

#define PWC_EV25V					BIT(14)
#define PWC_EV12V					BIT(15)


//2 SYS_FUNC_EN
#define FEN_BBRSTB					BIT(0)
#define FEN_BB_GLB_RSTn			BIT(1)
#define FEN_USBA					BIT(2)
#define FEN_UPLL					BIT(3)
#define FEN_USBD					BIT(4)
#define FEN_DIO_PCIE				BIT(5)
#define FEN_PCIEA					BIT(6)
#define FEN_PPLL						BIT(7)
#define FEN_PCIED					BIT(8)
#define FEN_DIOE					BIT(9)
#define FEN_CPUEN					BIT(10)
#define FEN_DCORE					BIT(11)
#define FEN_ELDR					BIT(12)
#define FEN_DIO_RF					BIT(13)
#define FEN_HWPDN					BIT(14)
#define FEN_MREGEN					BIT(15)

//2 APS_FSMCO
#define PFM_LDALL					BIT(0)
#define PFM_ALDN					BIT(1)
#define PFM_LDKP					BIT(2)
#define PFM_WOWL					BIT(3)
#define EnPDN						BIT(4)
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

//2 SYS_CLKR
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


//2 9346CR

#define BOOT_FROM_EEPROM			BIT(4)
#define EEPROM_EN					BIT(5)


//2 AFE_MISC
#define AFE_BGEN					BIT(0)
#define AFE_MBEN					BIT(1)
#define MAC_ID_EN					BIT(7)


//2 SPS0_CTRL


//2 SPS_OCP_CFG


//2 RSV_CTRL
#define WLOCK_ALL					BIT(0)
#define WLOCK_00					BIT(1)
#define WLOCK_04					BIT(2)
#define WLOCK_08					BIT(3)
#define WLOCK_40					BIT(4)
#define R_DIS_PRST_0				BIT(5)
#define R_DIS_PRST_1				BIT(6)
#define LOCK_ALL_EN					BIT(7)

//2 RF_CTRL
#define RF_EN						BIT(0)
#define RF_RSTB						BIT(1)
#define RF_SDMRSTB					BIT(2)



//2 LDOA15_CTRL
#define LDA15_EN					BIT(0)
#define LDA15_STBY					BIT(1)
#define LDA15_OBUF					BIT(2)
#define LDA15_REG_VOS				BIT(3)
#define _LDA15_VOADJ(x)				(((x) & 0x7) << 4)



//2 LDOV12D_CTRL
#define LDV12_EN					BIT(0)
#define LDV12_SDBY					BIT(1)
#define LPLDO_HSM					BIT(2)
#define LPLDO_LSM_DIS				BIT(3)
#define _LDV12_VADJ(x)				(((x) & 0xF) << 4)


//2 AFE_XTAL_CTRL
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


//2 AFE_PLL_CTRL
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


//2 EFUSE_CTRL
#define ALD_EN						BIT(18)
#define EF_PD						BIT(19)
#define EF_FLAG						BIT(31)

//2 EFUSE_TEST 
#define EF_TRPT						BIT(7)
#define LDOE25_EN					BIT(31)

//2 PWR_DATA 

//2 CAL_TIMER

//2 ACLK_MON
#define RSM_EN						BIT(0)
#define Timer_EN						BIT(4)


//2 GPIO_MUXCFG
#define TRSW0EN						BIT(2)
#define TRSW1EN						BIT(3)
#define EROM_EN						BIT(4)
#define EnBT							BIT(5)
#define EnUart						BIT(8)
#define Uart_910						BIT(9)
#define EnPMAC						BIT(10)
#define SIC_SWRST					BIT(11)
#define EnSIC						BIT(12)
#define SIC_23						BIT(13)
#define EnHDP						BIT(14)
#define SIC_LBK						BIT(15)

//2 GPIO_PIN_CTRL



//2 GPIO_INTM

//2 LEDCFG
#define LED0PL 					BIT(4)  
#define LED1PL 					BIT(12)
#define LED0DIS					BIT(7)

#define  SECCAM_CLR				BIT(30)

//2 FSIMR

//2 FSISR


//2 8051FWDL
//2 MCUFWDL
#define MCUFWDL_EN					BIT(0)
#define MCUFWDL_RDY				BIT(1)
#define FWDL_ChkSum_rpt			BIT(2)
#define MACINI_RDY					BIT(3)
#define BBINI_RDY					BIT(4)
#define RFINI_RDY					BIT(5)
#define WINTINI_RDY					BIT(6)
#define MAC1_WINTINI_RDY			BIT(11)// 0X81 BIT3
#define CPRST						BIT(23)




//2 REG_SYS_CFG
#define XCLK_VLD						BIT(0)
#define ACLK_VLD					BIT(1)
#define UCLK_VLD					BIT(2)
#define PCLK_VLD						BIT(3)
#define PCIRSTB						BIT(4)
#define V15_VLD						BIT(5)
#define TRP_B15V_EN					BIT(7)
#define SIC_IDLE						BIT(8)
#define BD_MAC2						BIT(9)
#define BD_MAC1						BIT(10)
#define IC_MACPHY_MODE				BIT(11)
#define PAD_HWPD_IDN				BIT(22)
#define TRP_VAUX_EN					BIT(23)
#define TRP_BT_EN					BIT(24)
#define BD_PKG_SEL					BIT(25)
#define BD_HCI_SEL					BIT(26)
#define TYPE_ID						BIT(27)

#define CHIP_VER_RTL_MASK			0xF000	//Bit 12 ~ 15
#define CHIP_VER_RTL_SHIFT			12

//-----------------------------------------------------
//
//	0x0100h ~ 0x01FFh	MACTOP General Configuration
//
//-----------------------------------------------------


//2 Function Enable Registers
//2 CR

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

// Network type
#define _NETTYPE(x)					(((x) & 0x3) << 16)
#define MASK_NETTYPE				0x30000
#define NT_NO_LINK					0x0
#define NT_LINK_AD_HOC				0x1
#define NT_LINK_AP					0x2
#define NT_AS_AP					0x3

#define _LBMODE(x)					(((x) & 0xF) << 24)
#define MASK_LBMODE				0xF000000
#define LOOPBACK_NORMAL			0x0
#define LOOPBACK_IMMEDIATELY		0xB
#define LOOPBACK_MAC_DELAY		0x3
#define LOOPBACK_PHY				0x1
#define LOOPBACK_DMA				0x7


//2 PBP - Page Size Register
#define GET_RX_PAGE_SIZE(value)		((value) & 0xF)
#define GET_TX_PAGE_SIZE(value)		(((value) & 0xF0) >> 4)
#define _PSRX_MASK					0xF
#define _PSTX_MASK					0xF0
#define _PSRX(x)						(x)
#define _PSTX(x)						((x) << 4)

#define PBP_64						0x0
#define PBP_128						0x1
#define PBP_256						0x2
#define PBP_512						0x3
#define PBP_1024					0x4


//2 TX/RXDMA
#define RXDMA_ARBBW_EN			BIT(0)
#define RXSHFT_EN					BIT(1)
#define RXDMA_AGG_EN				BIT(2)
#define QS_VO_QUEUE				BIT(8)
#define QS_VI_QUEUE					BIT(9)
#define QS_BE_QUEUE				BIT(10)
#define QS_BK_QUEUE				BIT(11)
#define QS_MANAGER_QUEUE			BIT(12)
#define QS_HIGH_QUEUE				BIT(13)

#define HQSEL_VOQ					BIT(0)
#define HQSEL_VIQ					BIT(1)
#define HQSEL_BEQ					BIT(2)
#define HQSEL_BKQ					BIT(3)
#define HQSEL_MGTQ					BIT(4)
#define HQSEL_HIQ					BIT(5)

// For normal driver, 0x10C
#define _TXDMA_HIQ_MAP(x) 	 		(((x)&0x3) << 14)
#define _TXDMA_MGQ_MAP(x) 	 		(((x)&0x3) << 12)
#define _TXDMA_BKQ_MAP(x) 	 		(((x)&0x3) << 10)		
#define _TXDMA_BEQ_MAP(x) 	 		(((x)&0x3) << 8 )
#define _TXDMA_VIQ_MAP(x) 	 		(((x)&0x3) << 6 )
#define _TXDMA_VOQ_MAP(x) 	 		(((x)&0x3) << 4 )

#define QUEUE_LOW					1
#define QUEUE_NORMAL				2
#define QUEUE_HIGH					3



//2 TRXFF_BNDY


//2 LLT_INIT
#define _LLT_NO_ACTIVE				0x0
#define _LLT_WRITE_ACCESS			0x1
#define _LLT_READ_ACCESS			0x2

#define _LLT_INIT_DATA(x)			((x) & 0xFF)
#define _LLT_INIT_ADDR(x)			(((x) & 0xFF) << 8)
#define _LLT_OP(x)					(((x) & 0x3) << 30)
#define _LLT_OP_VALUE(x)			(((x) >> 30) & 0x3)


//2 BB_ACCESS_CTRL
#define BB_WRITE_READ_MASK		(BIT(31) | BIT(30))
#define BB_WRITE_EN					BIT(30)
#define BB_READ_EN					BIT(31)
//#define BB_ADDR_MASK				0xFFF
//#define _BB_ADDR(x)					((x) & BB_ADDR_MASK)

//-----------------------------------------------------
//
//	0x0200h ~ 0x027Fh	TXDMA Configuration
//
//-----------------------------------------------------
//2 RQPN
#define _HPQ(x)						((x) & 0xFF)
#define _LPQ(x)						(((x) & 0xFF) << 8)
#define _PUBQ(x)						(((x) & 0xFF) << 16)
#define _NPQ(x)						((x) & 0xFF)			// NOTE: in RQPN_NPQ register


#define HPQ_PUBLIC_DIS				BIT(24)
#define LPQ_PUBLIC_DIS				BIT(25)
#define LD_RQPN						BIT(31)


//2 TDECTRL
#define BCN_VALID					BIT(16)
#define BCN_HEAD(x)					(((x) & 0xFF) << 8)
#define	BCN_HEAD_MASK				0xFF00

//2 TDECTL
#define BLK_DESC_NUM_SHIFT			4
#define BLK_DESC_NUM_MASK			0xF


//2 TXDMA_OFFSET_CHK
#define DROP_DATA_EN				BIT(9)

//-----------------------------------------------------
//
//	0x0400h ~ 0x047Fh	Protocol Configuration
//
//-----------------------------------------------------
//2 FWHW_TXQ_CTRL
#define EN_AMPDU_RTY_NEW			BIT(7)

//2 INIRTSMCS_SEL
#define _INIRTSMCS_SEL(x)			((x) & 0x3F)


//2 SPEC SIFS
#define _SPEC_SIFS_CCK(x)			((x) & 0xFF)
#define _SPEC_SIFS_OFDM(x)			(((x) & 0xFF) << 8)


//2 RRSR

#define RATE_REG_BITMAP_ALL			0xFFFFF

#define _RRSC_BITMAP(x)					((x) & 0xFFFFF)

#define _RRSR_RSC(x)						(((x) & 0x3) << 21)
#define RRSR_RSC_RESERVED				0x0
#define RRSR_RSC_UPPER_SUBCHANNEL	0x1
#define RRSR_RSC_LOWER_SUBCHANNEL	0x2
#define RRSR_RSC_DUPLICATE_MODE		0x3


//2 ARFR
#define USE_SHORT_G1				BIT(20)

//2 AGGLEN_LMT_L
#define _AGGLMT_MCS0(x)				((x) & 0xF)
#define _AGGLMT_MCS1(x)				(((x) & 0xF) << 4)
#define _AGGLMT_MCS2(x)				(((x) & 0xF) << 8)
#define _AGGLMT_MCS3(x)				(((x) & 0xF) << 12)
#define _AGGLMT_MCS4(x)				(((x) & 0xF) << 16)
#define _AGGLMT_MCS5(x)				(((x) & 0xF) << 20)
#define _AGGLMT_MCS6(x)				(((x) & 0xF) << 24)
#define _AGGLMT_MCS7(x)				(((x) & 0xF) << 28)


//2 RL
#define	RETRY_LIMIT_SHORT_SHIFT		8
#define	RETRY_LIMIT_LONG_SHIFT		0


//2 DARFRC
#define _DARF_RC1(x)				((x) & 0x1F)
#define _DARF_RC2(x)				(((x) & 0x1F) << 8)
#define _DARF_RC3(x)				(((x) & 0x1F) << 16)
#define _DARF_RC4(x)				(((x) & 0x1F) << 24)
// NOTE: shift starting from address (DARFRC + 4)
#define _DARF_RC5(x)				((x) & 0x1F)
#define _DARF_RC6(x)				(((x) & 0x1F) << 8)
#define _DARF_RC7(x)				(((x) & 0x1F) << 16)
#define _DARF_RC8(x)				(((x) & 0x1F) << 24)


//2 RARFRC
#define _RARF_RC1(x)				((x) & 0x1F)
#define _RARF_RC2(x)				(((x) & 0x1F) << 8)
#define _RARF_RC3(x)				(((x) & 0x1F) << 16)
#define _RARF_RC4(x)				(((x) & 0x1F) << 24)
// NOTE: shift starting from address (RARFRC + 4)
#define _RARF_RC5(x)				((x) & 0x1F)
#define _RARF_RC6(x)				(((x) & 0x1F) << 8)
#define _RARF_RC7(x)				(((x) & 0x1F) << 16)
#define _RARF_RC8(x)				(((x) & 0x1F) << 24)




//-----------------------------------------------------
//
//	0x0500h ~ 0x05FFh	EDCA Configuration
//
//-----------------------------------------------------



//2 EDCA setting
#define AC_PARAM_TXOP_LIMIT_OFFSET	16
#define AC_PARAM_ECW_MAX_OFFSET		12
#define AC_PARAM_ECW_MIN_OFFSET		8
#define AC_PARAM_AIFS_OFFSET			0


//2 EDCA_VO_PARAM
#define _AIFS(x)						(x)
#define _ECW_MAX_MIN(x)			((x) << 8)
#define _TXOP_LIMIT(x)				((x) << 16)


#define _BCNIFS(x)					((x) & 0xFF)
#define _BCNECW(x)					(((x) & 0xF))<< 8)


#define _LRL(x)						((x) & 0x3F)
#define _SRL(x)						(((x) & 0x3F) << 8)


//2 SIFS_CCK
#define _SIFS_CCK_CTX(x)			((x) & 0xFF)
#define _SIFS_CCK_TRX(x)			(((x) & 0xFF) << 8);


//2 SIFS_OFDM
#define _SIFS_OFDM_CTX(x)			((x) & 0xFF)
#define _SIFS_OFDM_TRX(x)			(((x) & 0xFF) << 8);


//2 TBTT PROHIBIT
#define _TBTT_PROHIBIT_HOLD(x)		(((x) & 0xFF) << 8)


//2 REG_RD_CTRL
#define DIS_EDCA_CNT_DWN			BIT(11)


//2 BCN_CTRL
#define EN_MBSSID						BIT(1)
#define EN_TXBCN_RPT					BIT(2)
#define EN_BCN_FUNCTION				BIT(3)
// The same function but different bit field.
#define DIS_TSF_UDT0_NORMAL_CHIP		BIT(4)
#define DIS_TSF_UDT0_TEST_CHIP		BIT(5)

//2 ACMHWCTRL
#define	AcmHw_HwEn					BIT(0)
#define	AcmHw_BeqEn					BIT(1)
#define	AcmHw_ViqEn					BIT(2)
#define	AcmHw_VoqEn					BIT(3)
#define	AcmHw_BeqStatus				BIT(4)
#define	AcmHw_ViqStatus				BIT(5)
#define	AcmHw_VoqStatus				BIT(6)



//-----------------------------------------------------
//
//	0x0600h ~ 0x07FFh	WMAC Configuration
//
//-----------------------------------------------------

//2 APSD_CTRL
#define APSDOFF						BIT(6)
#define APSDOFF_STATUS				BIT(7)


//2 BWOPMODE
#define BW_20MHZ					BIT(2)
//#define BW_OPMODE_20MHZ				BIT(2)	// For compability


#define RATE_BITMAP_ALL			0xFFFFF

// Only use CCK 1M rate for ACK
#define RATE_RRSR_CCK_ONLY_1M		0xFFFF1
#define RATE_RRSR_WITHOUT_CCK		0xFFFF0

//2 TCR
#define TSFRST						BIT(0)
#define DIS_GCLK						BIT(1)
#define PAD_SEL						BIT(2)
#define PWR_ST						BIT(6)
#define PWRBIT_OW_EN				BIT(7)
#define ACRC							BIT(8)
#define CFENDFORM					BIT(9)
#define ICV							BIT(10)



//2 RCR
#define AAP							BIT(0)
#define APM							BIT(1)
#define AM							BIT(2)
#define AB							BIT(3)
#define ADD3							BIT(4)
#define APWRMGT					BIT(5)
#define CBSSID						BIT(6)
#define CBSSID_BCN					BIT(7)
#define ACRC32						BIT(8)
#define AICV							BIT(9)
#define ADF							BIT(11)
#define ACF							BIT(12)
#define AMF							BIT(13)
#define HTC_LOC_CTRL				BIT(14)
#define UC_DATA_EN					BIT(16)
#define BM_DATA_EN					BIT(17)
#define MFBEN						BIT(22)
#define LSIGEN						BIT(23)
#define EnMBID						BIT(24)
#define APP_BASSN					BIT(27)
#define APP_PHYSTS					BIT(28)
#define APP_ICV						BIT(29)
#define APP_MIC						BIT(30)
#define APP_FCS						BIT(31)

//2 RX_PKT_LIMIT

//2 RX_DLK_TIME

//2 MBIDCAMCFG



//2 AMPDU_MIN_SPACE
#define _MIN_SPACE(x)				((x) & 0x7)
#define _SHORT_GI_PADDING(x)		(((x) & 0x1F) << 3)


//2 RXERR_RPT
#define RXERR_TYPE_OFDM_PPDU			0
#define RXERR_TYPE_OFDM_FALSE_ALARM	1
#define RXERR_TYPE_OFDM_MPDU_OK		2
#define RXERR_TYPE_OFDM_MPDU_FAIL	3
#define RXERR_TYPE_CCK_PPDU			4
#define RXERR_TYPE_CCK_FALSE_ALARM	5
#define RXERR_TYPE_CCK_MPDU_OK		6
#define RXERR_TYPE_CCK_MPDU_FAIL		7
#define RXERR_TYPE_HT_PPDU				8
#define RXERR_TYPE_HT_FALSE_ALARM	9
#define RXERR_TYPE_HT_MPDU_TOTAL		10
#define RXERR_TYPE_HT_MPDU_OK			11
#define RXERR_TYPE_HT_MPDU_FAIL		12
#define RXERR_TYPE_RX_FULL_DROP		15

#define RXERR_COUNTER_MASK			0xFFFFF
#define RXERR_RPT_RST					BIT(27)
#define _RXERR_RPT_SEL(type)			((type) << 28)


//2 SECCFG
#define	SCR_TxUseDK					BIT(0)			//Force Tx Use Default Key
#define	SCR_RxUseDK					BIT(1)			//Force Rx Use Default Key
#define	SCR_TxEncEnable				BIT(2)			//Enable Tx Encryption
#define	SCR_RxDecEnable				BIT(3)			//Enable Rx Decryption
#define	SCR_SKByA2						BIT(4)			//Search kEY BY A2
#define	SCR_NoSKMC						BIT(5)			//No Key Search Multicast
#define 	SCR_TXBCUSEDK					BIT(6)			// Force Tx Broadcast packets Use Default Key
#define 	SCR_RXBCUSEDK					BIT(7)			// Force Rx Broadcast packets Use Default Key

//vivi added for new cam search flow, 20091028
#ifdef HW_EN_DE_CRYPTION_FOR_NEW_CAM_SEARCH_FLOW
#define	SCR_TxUseBroadcastDK			BIT6			//Force Tx Use Broadcast Default Key
#define	SCR_RxUseBroadcastDK			BIT7			//Force Rx Use Broadcast Default Key
#endif


//-----------------------------------------------------
//
//	0xFE00h ~ 0xFE55h	USB Configuration
//
//-----------------------------------------------------

//2 USB Information (0xFE17)
#define USB_IS_HIGH_SPEED				0
#define USB_IS_FULL_SPEED				1
#define USB_SPEED_MASK					BIT(5)

#define USB_NORMAL_SIE_EP_MASK		0xF
#define USB_NORMAL_SIE_EP_SHIFT		4

#define USB_TEST_EP_MASK				0x30
#define USB_TEST_EP_SHIFT				4

//2 Special Option
#define USB_AGG_EN						BIT(3)


//2REG_C2HEVT_CLEAR
#define C2H_EVT_HOST_CLOSE				0x00	// Set by driver and notify FW that the driver has read the C2H command message
#define C2H_EVT_FW_CLOSE				0xFF	// Set by FW indicating that FW had set the C2H command message and it's not yet read by driver.

//2 8192D PartNo.
#define		PARTNO_92D_NIC			(BIT7|BIT6)
#define		PARTNO_92D_NIC_REMARK (BIT5|BIT4)
#define		PARTNO_SINGLE_BAND_VS  BIT3
#define		PARTNO_SINGLE_BAND_VS_REMARK BIT1
#define		PARTNO_CONCURRENT_BAND_VC (BIT3|BIT2)
#define		PARTNO_CONCURRENT_BAND_VC_REMARK (BIT1|BIT0)
//========================================================
// General definitions
//========================================================

#define MAC_ADDR_LEN						6
#define LAST_ENTRY_OF_TX_PKT_BUFFER		255
#define LAST_ENTRY_OF_TX_PKT_BUFFER_DUAL_MAC	127

#define POLLING_LLT_THRESHOLD				20
#define POLLING_READY_TIMEOUT_COUNT		1000

// Min Spacing related settings.
#define	MAX_MSS_DENSITY_2T				0x13
#define	MAX_MSS_DENSITY_1T				0x0A
// GPIO BIT
#define	HAL_8192C_HW_GPIO_WPS_BIT		BIT2


#include "basic_types.h"

#endif

