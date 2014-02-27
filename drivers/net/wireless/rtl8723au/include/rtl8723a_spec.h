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
 *******************************************************************************/
#ifndef __RTL8723A_SPEC_H__
#define __RTL8723A_SPEC_H__

#include <rtl8192c_spec.h>


//============================================================================
//	8723A Regsiter offset definition
//============================================================================
#define HAL_8723A_NAV_UPPER_UNIT	128		// micro-second

//-----------------------------------------------------
//
//	0x0000h ~ 0x00FFh	System Configuration
//
//-----------------------------------------------------
#define REG_SYSON_REG_LOCK		0x001C


//-----------------------------------------------------
//
//	0x0100h ~ 0x01FFh	MACTOP General Configuration
//
//-----------------------------------------------------
#define REG_FTIMR			0x0138


//-----------------------------------------------------
//
//	0x0200h ~ 0x027Fh	TXDMA Configuration
//
//-----------------------------------------------------


//-----------------------------------------------------
//
//	0x0280h ~ 0x02FFh	RXDMA Configuration
//
//-----------------------------------------------------


//-----------------------------------------------------
//
//	0x0300h ~ 0x03FFh	PCIe
//
//-----------------------------------------------------


//-----------------------------------------------------
//
//	0x0400h ~ 0x047Fh	Protocol Configuration
//
//-----------------------------------------------------
//#define REG_EARLY_MODE_CONTROL		0x4D0
#define REG_MACID_NO_LINK 0x4D0

//-----------------------------------------------------
//
//	0x0500h ~ 0x05FFh	EDCA Configuration
//
//-----------------------------------------------------

//2 BCN_CTRL
#define DIS_ATIM					BIT(0)
#define DIS_BCNQ_SUB				BIT(1)
#define DIS_TSF_UDT					BIT(4)


//-----------------------------------------------------
//
//	0x0600h ~ 0x07FFh	WMAC Configuration
//
//-----------------------------------------------------
//
// Note:
//	The NAV upper value is very important to WiFi 11n 5.2.3 NAV test. The default value is
//	always too small, but the WiFi TestPlan test by 25,000 microseconds of NAV through sending
//	CTS in the air. We must update this value greater than 25,000 microseconds to pass the item.
//	The offset of NAV_UPPER in 8192C Spec is incorrect, and the offset should be 0x0652. Commented
//	by SD1 Scott.
// By Bruce, 2011-07-18.
//
#define	REG_NAV_UPPER			0x0652	// unit of 128

#define REG_BT_COEX_TABLE_1		0x06C0
#define REG_BT_COEX_TABLE_2		0x06C4

//============================================================================
//	8723 Regsiter Bit and Content definition
//============================================================================

//-----------------------------------------------------
//
//	0x0000h ~ 0x00FFh	System Configuration
//
//-----------------------------------------------------

//2 SPS0_CTRL

//2 SYS_ISO_CTRL

//2 SYS_FUNC_EN

//2 APS_FSMCO
#define EN_WLON			BIT(16)

//2 SYS_CLKR

//2 9346CR

//2 AFE_MISC

//2 SPS0_CTRL

//2 SPS_OCP_CFG

//2 SYSON_REG_LOCK
#define WLOCK_ALL		BIT(0)
#define WLOCK_00		BIT(1)
#define WLOCK_04		BIT(2)
#define WLOCK_08		BIT(3)
#define WLOCK_40		BIT(4)
#define WLOCK_1C_B6		BIT(5)
#define R_DIS_PRST_1		BIT(6)
#define LOCK_ALL_EN		BIT(7)

//2 RF_CTRL

//2 LDOA15_CTRL

//2 LDOV12D_CTRL

//2 AFE_XTAL_CTRL

//2 AFE_PLL_CTRL

//2 EFUSE_CTRL

//2 EFUSE_TEST (For RTL8723 partially)

//2 PWR_DATA

//2 CAL_TIMER

//2 ACLK_MON

//2 GPIO_MUXCFG

//2 GPIO_PIN_CTRL

//2 GPIO_INTM

//2 LEDCFG

//2 FSIMR

//2 FSISR

//2 HSIMR
// 8723 Host System Interrupt Mask Register (offset 0x58, 32 byte)
#define HSIMR_GPIO12_0_INT_EN	BIT(0)
#define HSIMR_SPS_OCP_INT_EN	BIT(5)
#define HSIMR_RON_INT_EN		BIT(6)
#define HSIMR_PDNINT_EN		BIT(7)
#define HSIMR_GPIO9_INT_EN		BIT(25)

//2 HSISR
// 8723 Host System Interrupt Status Register (offset 0x5C, 32 byte)
#define HSISR_GPIO12_0_INT		BIT(0)
#define HSISR_SPS_OCP_INT		BIT(5)
#define HSISR_RON_INT			BIT(6)
#define HSISR_PDNINT			BIT(7)
#define	HSISR_GPIO9_INT			BIT(25)

// interrupt mask which needs to clear
#define MASK_HSISR_CLEAR		(HSISR_GPIO12_0_INT |\
								HSISR_SPS_OCP_INT |\
								HSISR_RON_INT |\
								HSISR_PDNINT |\
								HSISR_GPIO9_INT)

//2 MCUFWDL
#define RAM_DL_SEL				BIT7	// 1:RAM, 0:ROM

//2 HPON_FSM

//2 SYS_CFG
#define RTL_ID					BIT(23)	// TestChip ID, 1:Test(RLE); 0:MP(RL)
#define SPS_SEL					BIT(24) // 1:LDO regulator mode; 0:Switching regulator mode


//-----------------------------------------------------
//
//	0x0100h ~ 0x01FFh	MACTOP General Configuration
//
//-----------------------------------------------------

//2 Function Enable Registers

//2 CR
#define CALTMR_EN					BIT(10)

//2 PBP - Page Size Register

//2 TX/RXDMA

//2 TRXFF_BNDY

//2 LLT_INIT

//2 BB_ACCESS_CTRL


//-----------------------------------------------------
//
//	0x0200h ~ 0x027Fh	TXDMA Configuration
//
//-----------------------------------------------------

//2 RQPN

//2 TDECTRL

//2 TDECTL

//2 TXDMA_OFFSET_CHK


//-----------------------------------------------------
//
//	0x0400h ~ 0x047Fh	Protocol Configuration
//
//-----------------------------------------------------

//2 FWHW_TXQ_CTRL

//2 INIRTSMCS_SEL

//2 SPEC SIFS

//2 RRSR

//2 ARFR

//2 AGGLEN_LMT_L

//2 RL

//2 DARFRC

//2 RARFRC


//-----------------------------------------------------
//
//	0x0500h ~ 0x05FFh	EDCA Configuration
//
//-----------------------------------------------------

//2 EDCA setting

//2 EDCA_VO_PARAM

//2 SIFS_CCK

//2 SIFS_OFDM

//2 TBTT PROHIBIT

//2 REG_RD_CTRL

//2 BCN_CTRL

//2 ACMHWCTRL


//-----------------------------------------------------
//
//	0x0600h ~ 0x07FFh	WMAC Configuration
//
//-----------------------------------------------------

//2 APSD_CTRL

//2 BWOPMODE

//2 TCR

//2 RCR

//2 RX_PKT_LIMIT

//2 RX_DLK_TIME

//2 MBIDCAMCFG

//2 AMPDU_MIN_SPACE

//2 RXERR_RPT

//2 SECCFG


//-----------------------------------------------------
//
//	0xFE00h ~ 0xFE55h	RTL8723 SDIO Configuration
//
//-----------------------------------------------------

// I/O bus domain address mapping
#define SDIO_LOCAL_BASE				0x10250000
#define WLAN_IOREG_BASE				0x10260000
#define FIRMWARE_FIFO_BASE			0x10270000
#define TX_HIQ_BASE				0x10310000
#define TX_MIQ_BASE				0x10320000
#define TX_LOQ_BASE				0x10330000
#define RX_RX0FF_BASE				0x10340000

// SDIO host local register space mapping.
#define SDIO_LOCAL_MSK				0x0FFF
#define WLAN_IOREG_MSK				0x7FFF
#define WLAN_FIFO_MSK		      		0x1FFF	// Aggregation Length[12:0]
#define WLAN_RX0FF_MSK			      	0x0003

#define SDIO_WITHOUT_REF_DEVICE_ID		0	// Without reference to the SDIO Device ID
#define SDIO_LOCAL_DEVICE_ID			0	// 0b[16], 000b[15:13]
#define WLAN_TX_HIQ_DEVICE_ID			4	// 0b[16], 100b[15:13]
#define WLAN_TX_MIQ_DEVICE_ID			5	// 0b[16], 101b[15:13]
#define WLAN_TX_LOQ_DEVICE_ID			6	// 0b[16], 110b[15:13]
#define WLAN_RX0FF_DEVICE_ID			7	// 0b[16], 111b[15:13]
#define WLAN_IOREG_DEVICE_ID			8	// 1b[16]

// SDIO Tx Free Page Index
#define HI_QUEUE_IDX				0
#define MID_QUEUE_IDX				1
#define LOW_QUEUE_IDX				2
#define PUBLIC_QUEUE_IDX			3

#define SDIO_MAX_TX_QUEUE			3		// HIQ, MIQ and LOQ
#define SDIO_MAX_RX_QUEUE			1

#define SDIO_REG_TX_CTRL			0x0000 // SDIO Tx Control
#define SDIO_REG_HIMR				0x0014 // SDIO Host Interrupt Mask
#define SDIO_REG_HISR				0x0018 // SDIO Host Interrupt Service Routine
#define SDIO_REG_HCPWM				0x0019 // HCI Current Power Mode
#define SDIO_REG_RX0_REQ_LEN			0x001C // RXDMA Request Length
#define SDIO_REG_FREE_TXPG			0x0020 // Free Tx Buffer Page
#define SDIO_REG_HCPWM1				0x0024 // HCI Current Power Mode 1
#define SDIO_REG_HCPWM2				0x0026 // HCI Current Power Mode 2
#define SDIO_REG_HTSFR_INFO			0x0030 // HTSF Informaion
#define SDIO_REG_HRPWM1				0x0080 // HCI Request Power Mode 1
#define SDIO_REG_HRPWM2				0x0082 // HCI Request Power Mode 2
#define SDIO_REG_HPS_CLKR			0x0084 // HCI Power Save Clock
#define SDIO_REG_HSUS_CTRL			0x0086 // SDIO HCI Suspend Control
#define SDIO_REG_HIMR_ON			0x0090 // SDIO Host Extension Interrupt Mask Always
#define SDIO_REG_HISR_ON			0x0091 // SDIO Host Extension Interrupt Status Always

#define SDIO_HIMR_DISABLED			0

// SDIO Host Interrupt Mask Register
#define SDIO_HIMR_RX_REQUEST_MSK		BIT0
#define SDIO_HIMR_AVAL_MSK			BIT1
#define SDIO_HIMR_TXERR_MSK			BIT2
#define SDIO_HIMR_RXERR_MSK			BIT3
#define SDIO_HIMR_TXFOVW_MSK			BIT4
#define SDIO_HIMR_RXFOVW_MSK			BIT5
#define SDIO_HIMR_TXBCNOK_MSK			BIT6
#define SDIO_HIMR_TXBCNERR_MSK			BIT7
#define SDIO_HIMR_BCNERLY_INT_MSK		BIT16
#define SDIO_HIMR_C2HCMD_MSK			BIT17
#define SDIO_HIMR_CPWM1_MSK			BIT18
#define SDIO_HIMR_CPWM2_MSK			BIT19
#define SDIO_HIMR_HSISR_IND_MSK			BIT20
#define SDIO_HIMR_GTINT3_IND_MSK		BIT21
#define SDIO_HIMR_GTINT4_IND_MSK		BIT22
#define SDIO_HIMR_PSTIMEOUT_MSK			BIT23
#define SDIO_HIMR_OCPINT_MSK			BIT24
#define SDIO_HIMR_ATIMEND_MSK			BIT25
#define SDIO_HIMR_ATIMEND_E_MSK			BIT26
#define SDIO_HIMR_CTWEND_MSK			BIT27

// SDIO Host Interrupt Service Routine
#define SDIO_HISR_RX_REQUEST			BIT0
#define SDIO_HISR_AVAL				BIT1
#define SDIO_HISR_TXERR				BIT2
#define SDIO_HISR_RXERR				BIT3
#define SDIO_HISR_TXFOVW			BIT4
#define SDIO_HISR_RXFOVW			BIT5
#define SDIO_HISR_TXBCNOK			BIT6
#define SDIO_HISR_TXBCNERR			BIT7
#define SDIO_HISR_BCNERLY_INT			BIT16
#define SDIO_HISR_C2HCMD			BIT17
#define SDIO_HISR_CPWM1				BIT18
#define SDIO_HISR_CPWM2				BIT19
#define SDIO_HISR_HSISR_IND			BIT20
#define SDIO_HISR_GTINT3_IND			BIT21
#define SDIO_HISR_GTINT4_IND			BIT22
#define SDIO_HISR_PSTIMEOUT			BIT23
#define SDIO_HISR_OCPINT			BIT24
#define SDIO_HISR_ATIMEND			BIT25
#define SDIO_HISR_ATIMEND_E			BIT26
#define SDIO_HISR_CTWEND			BIT27

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

// SDIO HCI Suspend Control Register
#define HCI_RESUME_PWR_RDY			BIT1
#define HCI_SUS_CTRL				BIT0

// SDIO Tx FIFO related
#define SDIO_TX_FREE_PG_QUEUE			4	// The number of Tx FIFO free page
#define SDIO_TX_FIFO_PAGE_SZ 			128

// vivi added for new cam search flow, 20091028
#define SCR_TxUseBroadcastDK			BIT6	// Force Tx Use Broadcast Default Key
#define SCR_RxUseBroadcastDK			BIT7	// Force Rx Use Broadcast Default Key


//----------------------------------------------------------------------------
// 8723 EFUSE
//----------------------------------------------------------------------------
#ifdef HWSET_MAX_SIZE
#undef HWSET_MAX_SIZE
#endif
#define HWSET_MAX_SIZE				256


//-----------------------------------------------------------------------------
//USB interrupt
//-----------------------------------------------------------------------------
#define	UHIMR_TIMEOUT2					BIT31
#define	UHIMR_TIMEOUT1					BIT30
#define	UHIMR_PSTIMEOUT					BIT29
#define	UHIMR_GTINT4					BIT28
#define	UHIMR_GTINT3					BIT27
#define	UHIMR_TXBCNERR					BIT26
#define	UHIMR_TXBCNOK					BIT25
#define	UHIMR_TSF_BIT32_TOGGLE			BIT24
#define	UHIMR_BCNDMAINT3				BIT23
#define	UHIMR_BCNDMAINT2				BIT22
#define	UHIMR_BCNDMAINT1				BIT21
#define	UHIMR_BCNDMAINT0				BIT20
#define	UHIMR_BCNDOK3					BIT19
#define	UHIMR_BCNDOK2					BIT18
#define	UHIMR_BCNDOK1					BIT17
#define	UHIMR_BCNDOK0					BIT16
#define	UHIMR_HSISR_IND					BIT15
#define	UHIMR_BCNDMAINT_E				BIT14
//RSVD	BIT13
#define	UHIMR_CTW_END					BIT12
//RSVD	BIT11
#define	UHIMR_C2HCMD					BIT10
#define	UHIMR_CPWM2					BIT9
#define	UHIMR_CPWM					BIT8
#define	UHIMR_HIGHDOK					BIT7		// High Queue DMA OK Interrupt
#define	UHIMR_MGNTDOK					BIT6		// Management Queue DMA OK Interrupt
#define	UHIMR_BKDOK					BIT5		// AC_BK DMA OK Interrupt
#define	UHIMR_BEDOK					BIT4		// AC_BE DMA OK Interrupt
#define	UHIMR_VIDOK						BIT3		// AC_VI DMA OK Interrupt
#define	UHIMR_VODOK					BIT2		// AC_VO DMA Interrupt
#define	UHIMR_RDU						BIT1		// Receive Descriptor Unavailable
#define	UHIMR_ROK						BIT0		// Receive DMA OK Interrupt

// USB Host Interrupt Status Extension bit
#define	UHIMR_BCNDMAINT7				BIT23
#define	UHIMR_BCNDMAINT6				BIT22
#define	UHIMR_BCNDMAINT5				BIT21
#define	UHIMR_BCNDMAINT4				BIT20
#define	UHIMR_BCNDOK7					BIT19
#define	UHIMR_BCNDOK6					BIT18
#define	UHIMR_BCNDOK5					BIT17
#define	UHIMR_BCNDOK4					BIT16
// bit14-15: RSVD
#define	UHIMR_ATIMEND_E				BIT13
#define	UHIMR_ATIMEND					BIT12
#define	UHIMR_TXERR						BIT11
#define	UHIMR_RXERR						BIT10
#define	UHIMR_TXFOVW					BIT9
#define	UHIMR_RXFOVW					BIT8
// bit2-7: RSVD
#define	UHIMR_OCPINT					BIT1
// bit0: RSVD

#define	REG_USB_HIMR				0xFE38
#define	REG_USB_HIMRE				0xFE3C
#define	REG_USB_HISR					0xFE78
#define	REG_USB_HISRE				0xFE7C

#define	USB_INTR_CPWM_OFFSET		16
#define	USB_INTR_CONTENT_HISR_OFFSET		48
#define	USB_INTR_CONTENT_HISRE_OFFSET		52
#define	USB_INTR_CONTENT_LENGTH			56
#define	USB_C2H_CMDID_OFFSET		0
#define	USB_C2H_SEQ_OFFSET		1
#define	USB_C2H_EVENT_OFFSET		2
//============================================================================
//	General definitions
//============================================================================

#ifdef CONFIG_RF_GAIN_OFFSET
#define	EEPROM_RF_GAIN_OFFSET			0x2F
#define	EEPROM_RF_GAIN_VAL				0x1F6
#endif //CONFIG_RF_GAIN_OFFSET


#endif

