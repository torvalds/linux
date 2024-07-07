/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
/*****************************************************************************
 *
 * Module:	__INC_HAL8192CPHYREG_H
 *
 *
 * Note:	1. Define PMAC/BB register map
 *		2. Define RF register map
 *		3. PMAC/BB register bit mask.
 *		4. RF reg bit mask.
 *		5. Other BB/RF relative definition.
 *
 *
 * Export:	Constants, macro, functions(API), global variables(None).
 *
 * Abbrev:
 *
 * History:
 *	Data		Who		Remark
 *      08/07/2007  MHC		1. Porting from 9x series PHYCFG.h.
 *						2. Reorganize code architecture.
 *09/25/2008	MH		1. Add RL6052 register definition
 *
 *****************************************************************************/
#ifndef __INC_HAL8192CPHYREG_H
#define __INC_HAL8192CPHYREG_H


/*--------------------------Define Parameters-------------------------------*/

/*  */
/*        8192S Register offset definition */
/*  */

/*  */
/*  BB-PHY register PMAC 0x100 PHY 0x800 - 0xEFF */
/*  1. PMAC duplicate register due to connection: RF_Mode, TRxRN, NumOf L-STF */
/*  2. 0x800/0x900/0xA00/0xC00/0xD00/0xE00 */
/*  3. RF register 0x00-2E */
/*  4. Bit Mask for BB/RF register */
/*  5. Other definition for BB/RF R/W */
/*  */

/*  */
/*  3. Page8(0x800) */
/*  */
#define		rFPGA0_RFMOD				0x800	/* RF mode & CCK TxSC  RF BW Setting?? */

#define		rFPGA0_XA_HSSIParameter1		0x820	/*  RF 3 wire register */
#define		rFPGA0_XA_HSSIParameter2		0x824
#define		rFPGA0_XB_HSSIParameter1		0x828
#define		rFPGA0_XB_HSSIParameter2		0x82c
#define		rTxAGC_B_Rate18_06				0x830
#define		rTxAGC_B_Rate54_24				0x834
#define		rTxAGC_B_CCK1_55_Mcs32		0x838
#define		rTxAGC_B_Mcs03_Mcs00			0x83c

#define		rTxAGC_B_Mcs07_Mcs04			0x848

#define		rFPGA0_XA_LSSIParameter		0x840
#define		rFPGA0_XB_LSSIParameter		0x844

#define		rFPGA0_XCD_SwitchControl		0x85c

#define		rFPGA0_XA_RFInterfaceOE		0x860	/*  RF Channel switch */
#define		rFPGA0_XB_RFInterfaceOE		0x864

#define		rTxAGC_B_CCK11_A_CCK2_11		0x86c

#define		rFPGA0_XAB_RFInterfaceSW		0x870	/*  RF Interface Software Control */
#define		rFPGA0_XCD_RFInterfaceSW		0x874

#define		rFPGA0_XA_LSSIReadBack		0x8a0	/*  Transceiver LSSI Readback */
#define		rFPGA0_XB_LSSIReadBack		0x8a4

#define		TransceiverA_HSPI_Readback	0x8b8	/*  Transceiver A HSPI Readback */
#define		TransceiverB_HSPI_Readback	0x8bc	/*  Transceiver B HSPI Readback */

/*  */
/*  4. Page9(0x900) */
/*  */
#define		rFPGA1_RFMOD				0x900	/* RF mode & OFDM TxSC  RF BW Setting?? */

#define		rS0S1_PathSwitch			0x948

/*  */
/*  5. PageA(0xA00) */
/*  */
/*  Set Control channel to upper or lower. These settings are required only for 40MHz */
#define		rCCK0_System				0xa00

#define		rCCK0_AFESetting			0xa04	/*  Disable init gain now Select RX path by RSSI */

/*  */
/*  PageB(0xB00) */
/*  */
#define		rConfig_AntA				0xb68
#define		rConfig_AntB				0xb6c

/*  */
/*  6. PageC(0xC00) */
/*  */
#define		rOFDM0_TRxPathEnable		0xc04
#define		rOFDM0_TRMuxPar			0xc08

#define		rOFDM0_XARxIQImbalance		0xc14  /* RxIQ imbalance matrix */
#define		rOFDM0_XBRxIQImbalance		0xc1c

#define		rOFDM0_RxDSP				0xc40  /* Rx Sync Path */
#define		rOFDM0_ECCAThreshold		0xc4c /*  energy CCA */

#define		rOFDM0_AGCRSSITable			0xc78

#define		rOFDM0_XATxIQImbalance		0xc80	/*  TX PWR TRACK and DIG */
#define		rOFDM0_XBTxIQImbalance		0xc88
#define		rOFDM0_XCTxAFE					0xc94
#define		rOFDM0_XDTxAFE				0xc9c

#define		rOFDM0_RxIQExtAnta			0xca0
#define		rOFDM0_TxPseudoNoiseWgt		0xce4

/*  */
/*  7. PageD(0xD00) */
/*  */
#define		rOFDM1_LSTF					0xd00

/*  */
/*  8. PageE(0xE00) */
/*  */
#define		rTxAGC_A_Rate18_06			0xe00
#define		rTxAGC_A_Rate54_24			0xe04
#define		rTxAGC_A_CCK1_Mcs32			0xe08
#define		rTxAGC_A_Mcs03_Mcs00			0xe10
#define		rTxAGC_A_Mcs07_Mcs04			0xe14

#define		rFPGA0_IQK					0xe28
#define		rTx_IQK_Tone_A				0xe30
#define		rRx_IQK_Tone_A				0xe34
#define		rTx_IQK_PI_A					0xe38
#define		rRx_IQK_PI_A					0xe3c

#define		rTx_IQK							0xe40
#define		rRx_IQK						0xe44
#define		rIQK_AGC_Pts					0xe48
#define		rIQK_AGC_Rsp					0xe4c
#define		rTx_IQK_Tone_B				0xe50
#define		rRx_IQK_Tone_B				0xe54
#define		rTx_IQK_PI_B					0xe58
#define		rRx_IQK_PI_B					0xe5c

#define		rBlue_Tooth					0xe6c
#define		rRx_Wait_CCA					0xe70
#define		rTx_CCK_RFON					0xe74
#define		rTx_CCK_BBON				0xe78
#define		rTx_OFDM_RFON				0xe7c
#define		rTx_OFDM_BBON				0xe80
#define		rTx_To_Rx					0xe84
#define		rTx_To_Tx					0xe88
#define		rRx_CCK						0xe8c

#define		rTx_Power_Before_IQK_A		0xe94
#define		rTx_Power_After_IQK_A			0xe9c

#define		rRx_Power_Before_IQK_A_2		0xea4
#define		rRx_Power_After_IQK_A_2		0xeac

#define		rRx_OFDM					0xed0
#define		rRx_Wait_RIFS				0xed4
#define		rRx_TO_Rx					0xed8
#define		rStandby						0xedc
#define		rSleep						0xee0
#define		rPMPD_ANAEN				0xeec

/*  */
/*  RL6052 Register definition */
/*  */
#define		RF_AC						0x00	/*  */

#define		RF_TXM_IDAC				0x08	/*  */

#define		RF_CHNLBW					0x18	/*  RF channel and BW switch */

#define		RF_RCK_OS					0x30	/*  RF TX PA control */

#define		RF_TXPA_G1					0x31	/*  RF TX PA control */
#define		RF_TXPA_G2					0x32	/*  RF TX PA control */

#define		RF_WE_LUT					0xEF

/*  2. Page8(0x800) */
#define		bRFMOD							0x1	/*  Reg 0x800 rFPGA0_RFMOD */

#define		b3WireDataLength			0x800	/*  Reg 0x820~84f rFPGA0_XA_HSSIParameter1 */
#define		b3WireAddressLength			0x400

#define		bRFSI_RFENV				0x10	/*  Reg 0x870 rFPGA0_XAB_RFInterfaceSW */

#define		bLSSIReadAddress			0x7f800000   /*  T65 RF */

#define		bLSSIReadEdge				0x80000000   /* LSSI "Read" edge signal */

#define		bLSSIReadBackData			0xfffff		/*  T65 RF */

/*  4. PageA(0xA00) */
#define		bCCKSideBand			0x10	/*  Reg 0xa00 rCCK0_System 20/40 switch */

/*  */
/*  Other Definition */
/*  */

/* for PutRegsetting & GetRegSetting BitMask */
#define		bMaskByte0			0xff	/*  Reg 0xc50 rOFDM0_XAAGCCore~0xC6f */
#define		bMaskByte1			0xff00
#define		bMaskByte2			0xff0000
#define		bMaskByte3			0xff000000
#define		bMaskHWord		0xffff0000
#define		bMaskLWord			0x0000ffff
#define		bMaskDWord		0xffffffff
#define		bMaskH3Bytes		0xffffff00
#define		bMask12Bits			0xfff
#define		bMaskH4Bits			0xf0000000

#define		bEnable			0x1	/*  Useless */

#define rDPDT_control				0x92c

#endif	/* __INC_HAL8192SPHYREG_H */
