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
/*****************************************************************************
 *
 * Module:	__INC_HAL8192CPHYREG_H
 *
 *
 * Note:	1. Define PMAC/BB register map
 *			2. Define RF register map
 *			3. PMAC/BB register bit mask.
 *			4. RF reg bit mask.
 *			5. Other BB/RF relative definition.
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
 *	09/25/2008	MH		1. Add RL6052 register definition
 * 
 *****************************************************************************/
#ifndef __INC_HAL8192CPHYREG_H
#define __INC_HAL8192CPHYREG_H


/*--------------------------Define Parameters-------------------------------*/

//============================================================
//       8192S Regsiter offset definition
//============================================================

//
// BB-PHY register PMAC 0x100 PHY 0x800 - 0xEFF
// 1. PMAC duplicate register due to connection: RF_Mode, TRxRN, NumOf L-STF
// 2. 0x800/0x900/0xA00/0xC00/0xD00/0xE00
// 3. RF register 0x00-2E
// 4. Bit Mask for BB/RF register
// 5. Other defintion for BB/RF R/W
//


//
// 1. PMAC duplicate register due to connection: RF_Mode, TRxRN, NumOf L-STF
// 1. Page1(0x100)
//
#define		rPMAC_Reset					0x100
#define		rPMAC_TxStart					0x104
#define		rPMAC_TxLegacySIG				0x108
#define		rPMAC_TxHTSIG1				0x10c
#define		rPMAC_TxHTSIG2				0x110
#define		rPMAC_PHYDebug				0x114
#define		rPMAC_TxPacketNum				0x118
#define		rPMAC_TxIdle					0x11c
#define		rPMAC_TxMACHeader0			0x120
#define		rPMAC_TxMACHeader1			0x124
#define		rPMAC_TxMACHeader2			0x128
#define		rPMAC_TxMACHeader3			0x12c
#define		rPMAC_TxMACHeader4			0x130
#define		rPMAC_TxMACHeader5			0x134
#define		rPMAC_TxDataType				0x138
#define		rPMAC_TxRandomSeed			0x13c
#define		rPMAC_CCKPLCPPreamble			0x140
#define		rPMAC_CCKPLCPHeader			0x144
#define		rPMAC_CCKCRC16				0x148
#define		rPMAC_OFDMRxCRC32OK			0x170
#define		rPMAC_OFDMRxCRC32Er			0x174
#define		rPMAC_OFDMRxParityEr			0x178
#define		rPMAC_OFDMRxCRC8Er			0x17c
#define		rPMAC_CCKCRxRC16Er			0x180
#define		rPMAC_CCKCRxRC32Er			0x184
#define		rPMAC_CCKCRxRC32OK			0x188
#define		rPMAC_TxStatus					0x18c

//
// 2. Page2(0x200)
//
// The following two definition are only used for USB interface.
#define		RF_BB_CMD_ADDR				0x02c0	// RF/BB read/write command address.
#define		RF_BB_CMD_DATA				0x02c4	// RF/BB read/write command data.

//
// 3. Page8(0x800)
//
#define		rFPGA0_RFMOD				0x800	//RF mode & CCK TxSC // RF BW Setting??

#define		rFPGA0_TxInfo				0x804	// Status report??
#define		rFPGA0_PSDFunction			0x808

#define		rFPGA0_TxGainStage			0x80c	// Set TX PWR init gain?

#define		rFPGA0_RFTiming1			0x810	// Useless now
#define		rFPGA0_RFTiming2			0x814

#define		rFPGA0_XA_HSSIParameter1		0x820	// RF 3 wire register
#define		rFPGA0_XA_HSSIParameter2		0x824
#define		rFPGA0_XB_HSSIParameter1		0x828
#define		rFPGA0_XB_HSSIParameter2		0x82c
#define		rTxAGC_B_Rate18_06				0x830
#define		rTxAGC_B_Rate54_24				0x834
#define		rTxAGC_B_CCK1_55_Mcs32		0x838
#define		rTxAGC_B_Mcs03_Mcs00			0x83c

#define		rTxAGC_B_Mcs07_Mcs04			0x848
#define		rTxAGC_B_Mcs11_Mcs08			0x84c

#define		rFPGA0_XA_LSSIParameter		0x840
#define		rFPGA0_XB_LSSIParameter		0x844

#define		rFPGA0_RFWakeUpParameter		0x850	// Useless now
#define		rFPGA0_RFSleepUpParameter		0x854

#define		rFPGA0_XAB_SwitchControl		0x858	// RF Channel switch
#define		rFPGA0_XCD_SwitchControl		0x85c

#define		rFPGA0_XA_RFInterfaceOE		0x860	// RF Channel switch
#define		rFPGA0_XB_RFInterfaceOE		0x864

#define		rTxAGC_B_Mcs15_Mcs12			0x868
#define		rTxAGC_B_CCK11_A_CCK2_11		0x86c

#define		rFPGA0_XAB_RFInterfaceSW		0x870	// RF Interface Software Control
#define		rFPGA0_XCD_RFInterfaceSW		0x874

#define		rFPGA0_XAB_RFParameter		0x878	// RF Parameter
#define		rFPGA0_XCD_RFParameter		0x87c

#define		rFPGA0_AnalogParameter1		0x880	// Crystal cap setting RF-R/W protection for parameter4??
#define		rFPGA0_AnalogParameter2		0x884
#define		rFPGA0_AnalogParameter3		0x888	// Useless now
#define		rFPGA0_AnalogParameter4		0x88c

#define		rFPGA0_XA_LSSIReadBack		0x8a0	// Tranceiver LSSI Readback
#define		rFPGA0_XB_LSSIReadBack		0x8a4
#define		rFPGA0_XC_LSSIReadBack		0x8a8
#define		rFPGA0_XD_LSSIReadBack		0x8ac

#define		rFPGA0_PSDReport				0x8b4	// Useless now
#define		TransceiverA_HSPI_Readback	0x8b8	// Transceiver A HSPI Readback
#define		TransceiverB_HSPI_Readback	0x8bc	// Transceiver B HSPI Readback
#define		rFPGA0_XAB_RFInterfaceRB		0x8e0	// Useless now // RF Interface Readback Value
#define		rFPGA0_XCD_RFInterfaceRB		0x8e4	// Useless now

//
// 4. Page9(0x900)
//
#define		rFPGA1_RFMOD				0x900	//RF mode & OFDM TxSC // RF BW Setting??

#define		rFPGA1_TxBlock				0x904	// Useless now
#define		rFPGA1_DebugSelect			0x908	// Useless now
#define		rFPGA1_TxInfo				0x90c	// Useless now // Status report??
#define 	rS0S1_PathSwitch			0x948

//
// 5. PageA(0xA00)
//
// Set Control channel to upper or lower. These settings are required only for 40MHz
#define		rCCK0_System				0xa00

#define		rCCK0_AFESetting			0xa04	// Disable init gain now // Select RX path by RSSI
#define		rCCK0_CCA					0xa08	// Disable init gain now // Init gain

#define		rCCK0_RxAGC1				0xa0c 	//AGC default value, saturation level // Antenna Diversity, RX AGC, LNA Threshold, RX LNA Threshold useless now. Not the same as 90 series
#define		rCCK0_RxAGC2				0xa10 	//AGC & DAGC

#define		rCCK0_RxHP					0xa14

#define		rCCK0_DSPParameter1		0xa18	//Timing recovery & Channel estimation threshold
#define		rCCK0_DSPParameter2		0xa1c	//SQ threshold

#define		rCCK0_TxFilter1				0xa20
#define		rCCK0_TxFilter2				0xa24
#define		rCCK0_DebugPort			0xa28	//debug port and Tx filter3
#define		rCCK0_FalseAlarmReport		0xa2c	//0xa2d	useless now 0xa30-a4f channel report
#define		rCCK0_TRSSIReport         		0xa50
#define		rCCK0_RxReport            		0xa54  //0xa57
#define		rCCK0_FACounterLower      	0xa5c  //0xa5b
#define		rCCK0_FACounterUpper      	0xa58  //0xa5c
//
// PageB(0xB00)
//
#define		rPdp_AntA      				0xb00  
#define		rPdp_AntA_4    				0xb04
#define		rConfig_Pmpd_AntA 			0xb28
#define		rConfig_AntA 				0xb68
#define		rConfig_AntB 				0xb6c
#define		rPdp_AntB 					0xb70
#define		rPdp_AntB_4 				0xb74
#define		rConfig_Pmpd_AntB			0xb98
#define		rAPK						0xbd8

//
// 6. PageC(0xC00)
//
#define		rOFDM0_LSTF				0xc00

#define		rOFDM0_TRxPathEnable		0xc04
#define		rOFDM0_TRMuxPar			0xc08
#define		rOFDM0_TRSWIsolation		0xc0c

#define		rOFDM0_XARxAFE			0xc10  //RxIQ DC offset, Rx digital filter, DC notch filter
#define		rOFDM0_XARxIQImbalance    	0xc14  //RxIQ imblance matrix
#define		rOFDM0_XBRxAFE            		0xc18
#define		rOFDM0_XBRxIQImbalance    	0xc1c
#define		rOFDM0_XCRxAFE            		0xc20
#define		rOFDM0_XCRxIQImbalance    	0xc24
#define		rOFDM0_XDRxAFE            		0xc28
#define		rOFDM0_XDRxIQImbalance    	0xc2c

#define		rOFDM0_RxDetector1			0xc30  //PD,BW & SBD	// DM tune init gain
#define		rOFDM0_RxDetector2			0xc34  //SBD & Fame Sync. 
#define		rOFDM0_RxDetector3			0xc38  //Frame Sync.
#define		rOFDM0_RxDetector4			0xc3c  //PD, SBD, Frame Sync & Short-GI

#define		rOFDM0_RxDSP				0xc40  //Rx Sync Path
#define		rOFDM0_CFOandDAGC		0xc44  //CFO & DAGC
#define		rOFDM0_CCADropThreshold	0xc48 //CCA Drop threshold
#define		rOFDM0_ECCAThreshold		0xc4c // energy CCA

#define		rOFDM0_XAAGCCore1			0xc50	// DIG
#define		rOFDM0_XAAGCCore2			0xc54
#define		rOFDM0_XBAGCCore1			0xc58
#define		rOFDM0_XBAGCCore2			0xc5c
#define		rOFDM0_XCAGCCore1			0xc60
#define		rOFDM0_XCAGCCore2			0xc64
#define		rOFDM0_XDAGCCore1			0xc68
#define		rOFDM0_XDAGCCore2			0xc6c

#define		rOFDM0_AGCParameter1			0xc70
#define		rOFDM0_AGCParameter2			0xc74
#define		rOFDM0_AGCRSSITable			0xc78
#define		rOFDM0_HTSTFAGC				0xc7c

#define		rOFDM0_XATxIQImbalance		0xc80	// TX PWR TRACK and DIG
#define		rOFDM0_XATxAFE				0xc84
#define		rOFDM0_XBTxIQImbalance		0xc88
#define		rOFDM0_XBTxAFE				0xc8c
#define		rOFDM0_XCTxIQImbalance		0xc90
#define		rOFDM0_XCTxAFE            			0xc94
#define		rOFDM0_XDTxIQImbalance		0xc98
#define		rOFDM0_XDTxAFE				0xc9c

#define		rOFDM0_RxIQExtAnta			0xca0
#define		rOFDM0_TxCoeff1				0xca4
#define		rOFDM0_TxCoeff2				0xca8
#define		rOFDM0_TxCoeff3				0xcac
#define		rOFDM0_TxCoeff4				0xcb0
#define		rOFDM0_TxCoeff5				0xcb4
#define		rOFDM0_TxCoeff6				0xcb8
#define		rOFDM0_RxHPParameter			0xce0
#define		rOFDM0_TxPseudoNoiseWgt		0xce4
#define		rOFDM0_FrameSync				0xcf0
#define		rOFDM0_DFSReport				0xcf4

//
// 7. PageD(0xD00)
//
#define		rOFDM1_LSTF					0xd00
#define		rOFDM1_TRxPathEnable			0xd04

#define		rOFDM1_CFO						0xd08	// No setting now
#define		rOFDM1_CSI1					0xd10
#define		rOFDM1_SBD						0xd14
#define		rOFDM1_CSI2					0xd18
#define		rOFDM1_CFOTracking			0xd2c
#define		rOFDM1_TRxMesaure1			0xd34
#define		rOFDM1_IntfDet					0xd3c
#define		rOFDM1_PseudoNoiseStateAB		0xd50
#define		rOFDM1_PseudoNoiseStateCD		0xd54
#define		rOFDM1_RxPseudoNoiseWgt		0xd58

#define		rOFDM_PHYCounter1				0xda0  //cca, parity fail
#define		rOFDM_PHYCounter2				0xda4  //rate illegal, crc8 fail
#define		rOFDM_PHYCounter3				0xda8  //MCS not support

#define		rOFDM_ShortCFOAB				0xdac	// No setting now
#define		rOFDM_ShortCFOCD				0xdb0
#define		rOFDM_LongCFOAB				0xdb4
#define		rOFDM_LongCFOCD				0xdb8
#define		rOFDM_TailCFOAB				0xdbc
#define		rOFDM_TailCFOCD				0xdc0
#define		rOFDM_PWMeasure1          		0xdc4
#define		rOFDM_PWMeasure2          		0xdc8
#define		rOFDM_BWReport				0xdcc
#define		rOFDM_AGCReport				0xdd0
#define		rOFDM_RxSNR					0xdd4
#define		rOFDM_RxEVMCSI				0xdd8
#define		rOFDM_SIGReport				0xddc


//
// 8. PageE(0xE00)
//
#define		rTxAGC_A_Rate18_06			0xe00
#define		rTxAGC_A_Rate54_24			0xe04
#define		rTxAGC_A_CCK1_Mcs32			0xe08
#define		rTxAGC_A_Mcs03_Mcs00			0xe10
#define		rTxAGC_A_Mcs07_Mcs04			0xe14
#define		rTxAGC_A_Mcs11_Mcs08			0xe18
#define		rTxAGC_A_Mcs15_Mcs12			0xe1c

#define		rFPGA0_IQK					0xe28
#define		rTx_IQK_Tone_A				0xe30
#define		rRx_IQK_Tone_A				0xe34
#define		rTx_IQK_PI_A					0xe38
#define		rRx_IQK_PI_A					0xe3c

#define		rTx_IQK 						0xe40
#define		rRx_IQK						0xe44
#define		rIQK_AGC_Pts					0xe48
#define		rIQK_AGC_Rsp					0xe4c
#define		rTx_IQK_Tone_B				0xe50
#define		rRx_IQK_Tone_B				0xe54
#define		rTx_IQK_PI_B					0xe58
#define		rRx_IQK_PI_B					0xe5c
#define		rIQK_AGC_Cont				0xe60

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

#define		rRx_Power_Before_IQK_A		0xea0
#define		rRx_Power_Before_IQK_A_2		0xea4
#define		rRx_Power_After_IQK_A			0xea8
#define		rRx_Power_After_IQK_A_2		0xeac

#define		rTx_Power_Before_IQK_B		0xeb4
#define		rTx_Power_After_IQK_B			0xebc

#define		rRx_Power_Before_IQK_B		0xec0
#define		rRx_Power_Before_IQK_B_2		0xec4
#define		rRx_Power_After_IQK_B			0xec8
#define		rRx_Power_After_IQK_B_2		0xecc

#define		rRx_OFDM					0xed0
#define		rRx_Wait_RIFS 				0xed4
#define		rRx_TO_Rx 					0xed8
#define		rStandby 						0xedc
#define		rSleep 						0xee0
#define		rPMPD_ANAEN				0xeec

//
// 7. RF Register 0x00-0x2E (RF 8256)
//    RF-0222D 0x00-3F
//
//Zebra1
#define		rZebra1_HSSIEnable				0x0	// Useless now
#define		rZebra1_TRxEnable1				0x1
#define		rZebra1_TRxEnable2				0x2
#define		rZebra1_AGC					0x4
#define		rZebra1_ChargePump			0x5
#define		rZebra1_Channel				0x7	// RF channel switch

//#endif
#define		rZebra1_TxGain					0x8	// Useless now
#define		rZebra1_TxLPF					0x9
#define		rZebra1_RxLPF					0xb
#define		rZebra1_RxHPFCorner			0xc

//Zebra4
#define		rGlobalCtrl						0	// Useless now
#define		rRTL8256_TxLPF					19
#define		rRTL8256_RxLPF					11

//RTL8258
#define		rRTL8258_TxLPF					0x11	// Useless now
#define		rRTL8258_RxLPF					0x13
#define		rRTL8258_RSSILPF				0xa

//
// RL6052 Register definition
//
#define		RF_AC						0x00	// 

#define		RF_IQADJ_G1				0x01	// 
#define		RF_IQADJ_G2				0x02	// 
#define		RF_BS_PA_APSET_G1_G4		0x03
#define		RF_BS_PA_APSET_G5_G8		0x04
#define		RF_POW_TRSW				0x05	// 

#define		RF_GAIN_RX					0x06	// 
#define		RF_GAIN_TX					0x07	// 

#define		RF_TXM_IDAC				0x08	// 
#define		RF_IPA_G					0x09	// 
#define		RF_TXBIAS_G				0x0A
#define		RF_TXPA_AG					0x0B
#define		RF_IPA_A					0x0C	// 
#define		RF_TXBIAS_A				0x0D
#define		RF_BS_PA_APSET_G9_G11	0x0E
#define		RF_BS_IQGEN				0x0F	// 

#define		RF_MODE1					0x10	// 
#define		RF_MODE2					0x11	// 

#define		RF_RX_AGC_HP				0x12	// 
#define		RF_TX_AGC					0x13	// 
#define		RF_BIAS						0x14	// 
#define		RF_IPA						0x15	// 
#define		RF_TXBIAS					0x16 //
#define		RF_POW_ABILITY			0x17	// 
#define		RF_MODE_AG				0x18	// 
#define		rRfChannel					0x18	// RF channel and BW switch
#define		RF_CHNLBW					0x18	// RF channel and BW switch
#define		RF_TOP						0x19	// 

#define		RF_RX_G1					0x1A	// 
#define		RF_RX_G2					0x1B	// 

#define		RF_RX_BB2					0x1C	// 
#define		RF_RX_BB1					0x1D	// 

#define		RF_RCK1					0x1E	// 
#define		RF_RCK2					0x1F	// 

#define		RF_TX_G1					0x20	// 
#define		RF_TX_G2					0x21	// 
#define		RF_TX_G3					0x22	// 

#define		RF_TX_BB1					0x23	// 

#define		RF_T_METER					0x24	// 

#define		RF_SYN_G1					0x25	// RF TX Power control
#define		RF_SYN_G2					0x26	// RF TX Power control
#define		RF_SYN_G3					0x27	// RF TX Power control
#define		RF_SYN_G4					0x28	// RF TX Power control
#define		RF_SYN_G5					0x29	// RF TX Power control
#define		RF_SYN_G6					0x2A	// RF TX Power control
#define		RF_SYN_G7					0x2B	// RF TX Power control
#define		RF_SYN_G8					0x2C	// RF TX Power control

#define		RF_RCK_OS					0x30	// RF TX PA control

#define		RF_TXPA_G1					0x31	// RF TX PA control
#define		RF_TXPA_G2					0x32	// RF TX PA control
#define		RF_TXPA_G3					0x33	// RF TX PA control
#define 	RF_TX_BIAS_A				0x35
#define 	RF_TX_BIAS_D				0x36
#define 	RF_LOBF_9					0x38
#define 	RF_RXRF_A3					0x3C	//	
#define 	RF_TRSW 					0x3F

#define 	RF_TXRF_A2					0x41
#define 	RF_TXPA_G4					0x46	
#define 	RF_TXPA_A4					0x4B	
#define 	RF_0x52 					0x52
#define 	RF_WE_LUT					0xEF	
#define 	RF_S0S1 					0xB0

//
//Bit Mask
//
// 1. Page1(0x100)
#define		bBBResetB						0x100	// Useless now?
#define		bGlobalResetB					0x200
#define		bOFDMTxStart					0x4
#define		bCCKTxStart						0x8
#define		bCRC32Debug					0x100
#define		bPMACLoopback					0x10
#define		bTxLSIG							0xffffff
#define		bOFDMTxRate					0xf
#define		bOFDMTxReserved				0x10
#define		bOFDMTxLength					0x1ffe0
#define		bOFDMTxParity					0x20000
#define		bTxHTSIG1						0xffffff
#define		bTxHTMCSRate					0x7f
#define		bTxHTBW						0x80
#define		bTxHTLength					0xffff00
#define		bTxHTSIG2						0xffffff
#define		bTxHTSmoothing					0x1
#define		bTxHTSounding					0x2
#define		bTxHTReserved					0x4
#define		bTxHTAggreation				0x8
#define		bTxHTSTBC						0x30
#define		bTxHTAdvanceCoding			0x40
#define		bTxHTShortGI					0x80
#define		bTxHTNumberHT_LTF			0x300
#define		bTxHTCRC8						0x3fc00
#define		bCounterReset					0x10000
#define		bNumOfOFDMTx					0xffff
#define		bNumOfCCKTx					0xffff0000
#define		bTxIdleInterval					0xffff
#define		bOFDMService					0xffff0000
#define		bTxMACHeader					0xffffffff
#define		bTxDataInit						0xff
#define		bTxHTMode						0x100
#define		bTxDataType					0x30000
#define		bTxRandomSeed					0xffffffff
#define		bCCKTxPreamble					0x1
#define		bCCKTxSFD						0xffff0000
#define		bCCKTxSIG						0xff
#define		bCCKTxService					0xff00
#define		bCCKLengthExt					0x8000
#define		bCCKTxLength					0xffff0000
#define		bCCKTxCRC16					0xffff
#define		bCCKTxStatus					0x1
#define		bOFDMTxStatus					0x2

#define 		IS_BB_REG_OFFSET_92S(_Offset)		((_Offset >= 0x800) && (_Offset <= 0xfff))

// 2. Page8(0x800)
#define		bRFMOD							0x1	// Reg 0x800 rFPGA0_RFMOD
#define		bJapanMode						0x2
#define		bCCKTxSC						0x30
#define		bCCKEn							0x1000000
#define		bOFDMEn						0x2000000

#define		bOFDMRxADCPhase           		0x10000	// Useless now
#define		bOFDMTxDACPhase           		0x40000
#define		bXATxAGC                  			0x3f

#define		bAntennaSelect                 		0x0300

#define		bXBTxAGC                  			0xf00	// Reg 80c rFPGA0_TxGainStage
#define		bXCTxAGC                  			0xf000
#define		bXDTxAGC                  			0xf0000
       		
#define		bPAStart                  			0xf0000000	// Useless now
#define		bTRStart                  			0x00f00000
#define		bRFStart                  			0x0000f000
#define		bBBStart                  			0x000000f0
#define		bBBCCKStart               		0x0000000f
#define		bPAEnd                    			0xf          //Reg0x814
#define		bTREnd                    			0x0f000000
#define		bRFEnd                    			0x000f0000
#define		bCCAMask                  			0x000000f0   //T2R
#define		bR2RCCAMask               		0x00000f00
#define		bHSSI_R2TDelay            		0xf8000000
#define		bHSSI_T2RDelay            		0xf80000
#define		bContTxHSSI               		0x400     //chane gain at continue Tx
#define		bIGFromCCK                		0x200
#define		bAGCAddress               		0x3f
#define		bRxHPTx                   			0x7000
#define		bRxHPT2R                  			0x38000
#define		bRxHPCCKIni               		0xc0000
#define		bAGCTxCode                		0xc00000
#define		bAGCRxCode                		0x300000

#define		b3WireDataLength          		0x800	// Reg 0x820~84f rFPGA0_XA_HSSIParameter1
#define		b3WireAddressLength       		0x400

#define		b3WireRFPowerDown         		0x1	// Useless now
//#define bHWSISelect               		0x8
#define		b5GPAPEPolarity           		0x40000000
#define		b2GPAPEPolarity           		0x80000000
#define		bRFSW_TxDefaultAnt        		0x3
#define		bRFSW_TxOptionAnt         		0x30
#define		bRFSW_RxDefaultAnt        		0x300
#define		bRFSW_RxOptionAnt         		0x3000
#define		bRFSI_3WireData           		0x1
#define		bRFSI_3WireClock          		0x2
#define		bRFSI_3WireLoad           		0x4
#define		bRFSI_3WireRW             		0x8
#define		bRFSI_3Wire               			0xf

#define		bRFSI_RFENV               		0x10	// Reg 0x870 rFPGA0_XAB_RFInterfaceSW

#define		bRFSI_TRSW                		0x20	// Useless now
#define		bRFSI_TRSWB               		0x40
#define		bRFSI_ANTSW               		0x100
#define		bRFSI_ANTSWB              		0x200
#define		bRFSI_PAPE                			0x400
#define		bRFSI_PAPE5G              		0x800 
#define		bBandSelect               			0x1
#define		bHTSIG2_GI                			0x80
#define		bHTSIG2_Smoothing         		0x01
#define		bHTSIG2_Sounding          		0x02
#define		bHTSIG2_Aggreaton         		0x08
#define		bHTSIG2_STBC              		0x30
#define		bHTSIG2_AdvCoding         		0x40
#define		bHTSIG2_NumOfHTLTF        	0x300
#define		bHTSIG2_CRC8              		0x3fc
#define		bHTSIG1_MCS               		0x7f
#define		bHTSIG1_BandWidth         		0x80
#define		bHTSIG1_HTLength          		0xffff
#define		bLSIG_Rate                			0xf
#define		bLSIG_Reserved            		0x10
#define		bLSIG_Length              		0x1fffe
#define		bLSIG_Parity              			0x20
#define		bCCKRxPhase               		0x4

#define		bLSSIReadAddress          		0x7f800000   // T65 RF

#define		bLSSIReadEdge             		0x80000000   //LSSI "Read" edge signal

#define		bLSSIReadBackData         		0xfffff		// T65 RF

#define		bLSSIReadOKFlag           		0x1000	// Useless now
#define		bCCKSampleRate            		0x8       //0: 44MHz, 1:88MHz       		
#define		bRegulator0Standby        		0x1
#define		bRegulatorPLLStandby      		0x2
#define		bRegulator1Standby        		0x4
#define		bPLLPowerUp               		0x8
#define		bDPLLPowerUp              		0x10
#define		bDA10PowerUp              		0x20
#define		bAD7PowerUp               		0x200
#define		bDA6PowerUp               		0x2000
#define		bXtalPowerUp              		0x4000
#define		b40MDClkPowerUP           		0x8000
#define		bDA6DebugMode             		0x20000
#define		bDA6Swing                 			0x380000

#define		bADClkPhase               		0x4000000	// Reg 0x880 rFPGA0_AnalogParameter1 20/40 CCK support switch 40/80 BB MHZ

#define		b80MClkDelay              		0x18000000	// Useless
#define		bAFEWatchDogEnable        		0x20000000

#define		bXtalCap01                			0xc0000000	// Reg 0x884 rFPGA0_AnalogParameter2 Crystal cap
#define		bXtalCap23                			0x3
#define		bXtalCap92x					0x0f000000
#define 		bXtalCap                			0x0f000000

#define		bIntDifClkEnable          		0x400	// Useless
#define		bExtSigClkEnable         	 	0x800
#define		bBandgapMbiasPowerUp      	0x10000
#define		bAD11SHGain               		0xc0000
#define		bAD11InputRange           		0x700000
#define		bAD11OPCurrent            		0x3800000
#define		bIPathLoopback            		0x4000000
#define		bQPathLoopback            		0x8000000
#define		bAFELoopback              		0x10000000
#define		bDA10Swing                		0x7e0
#define		bDA10Reverse              		0x800
#define		bDAClkSource              		0x1000
#define		bAD7InputRange            		0x6000
#define		bAD7Gain                  			0x38000
#define		bAD7OutputCMMode          		0x40000
#define		bAD7InputCMMode           		0x380000
#define		bAD7Current               			0xc00000
#define		bRegulatorAdjust          		0x7000000
#define		bAD11PowerUpAtTx          		0x1
#define		bDA10PSAtTx               		0x10
#define		bAD11PowerUpAtRx          		0x100
#define		bDA10PSAtRx               		0x1000       		
#define		bCCKRxAGCFormat           		0x200       		
#define		bPSDFFTSamplepPoint       		0xc000
#define		bPSDAverageNum            		0x3000
#define		bIQPathControl            		0xc00
#define		bPSDFreq                  			0x3ff
#define		bPSDAntennaPath           		0x30
#define		bPSDIQSwitch              		0x40
#define		bPSDRxTrigger             		0x400000
#define		bPSDTxTrigger             		0x80000000
#define		bPSDSineToneScale        		0x7f000000
#define		bPSDReport                			0xffff

// 3. Page9(0x900)
#define		bOFDMTxSC                 		0x30000000	// Useless
#define		bCCKTxOn                  			0x1
#define		bOFDMTxOn                 		0x2
#define		bDebugPage                		0xfff  //reset debug page and also HWord, LWord
#define		bDebugItem                		0xff   //reset debug page and LWord
#define		bAntL              	       		0x10
#define		bAntNonHT           	      			0x100
#define		bAntHT1               			0x1000
#define		bAntHT2                   			0x10000
#define		bAntHT1S1                 			0x100000
#define		bAntNonHTS1               		0x1000000

// 4. PageA(0xA00)
#define		bCCKBBMode				0x3	// Useless
#define		bCCKTxPowerSaving		0x80
#define		bCCKRxPowerSaving		0x40

#define		bCCKSideBand			0x10	// Reg 0xa00 rCCK0_System 20/40 switch

#define		bCCKScramble			0x8	// Useless
#define		bCCKAntDiversity		0x8000
#define		bCCKCarrierRecovery		0x4000
#define		bCCKTxRate				0x3000
#define		bCCKDCCancel			0x0800
#define		bCCKISICancel			0x0400
#define		bCCKMatchFilter			0x0200
#define		bCCKEqualizer			0x0100
#define		bCCKPreambleDetect		0x800000
#define		bCCKFastFalseCCA		0x400000
#define		bCCKChEstStart			0x300000
#define		bCCKCCACount			0x080000
#define		bCCKcs_lim				0x070000
#define		bCCKBistMode			0x80000000
#define		bCCKCCAMask			0x40000000
#define		bCCKTxDACPhase		0x4
#define		bCCKRxADCPhase		0x20000000   //r_rx_clk
#define		bCCKr_cp_mode0		0x0100
#define		bCCKTxDCOffset			0xf0
#define		bCCKRxDCOffset			0xf
#define		bCCKCCAMode			0xc000
#define		bCCKFalseCS_lim			0x3f00
#define		bCCKCS_ratio			0xc00000
#define		bCCKCorgBit_sel			0x300000
#define		bCCKPD_lim				0x0f0000
#define		bCCKNewCCA			0x80000000
#define		bCCKRxHPofIG			0x8000
#define		bCCKRxIG				0x7f00
#define		bCCKLNAPolarity			0x800000
#define		bCCKRx1stGain			0x7f0000
#define		bCCKRFExtend			0x20000000 //CCK Rx Iinital gain polarity
#define		bCCKRxAGCSatLevel		0x1f000000
#define		bCCKRxAGCSatCount		0xe0
#define		bCCKRxRFSettle			0x1f       //AGCsamp_dly
#define		bCCKFixedRxAGC			0x8000
//#define bCCKRxAGCFormat         	 	0x4000   //remove to HSSI register 0x824
#define		bCCKAntennaPolarity		0x2000
#define		bCCKTxFilterType		0x0c00
#define		bCCKRxAGCReportType	0x0300
#define		bCCKRxDAGCEn			0x80000000
#define		bCCKRxDAGCPeriod		0x20000000
#define		bCCKRxDAGCSatLevel		0x1f000000
#define		bCCKTimingRecovery		0x800000
#define		bCCKTxC0				0x3f0000
#define		bCCKTxC1				0x3f000000
#define		bCCKTxC2				0x3f
#define		bCCKTxC3				0x3f00
#define		bCCKTxC4				0x3f0000
#define		bCCKTxC5				0x3f000000
#define		bCCKTxC6				0x3f
#define		bCCKTxC7				0x3f00
#define		bCCKDebugPort			0xff0000
#define		bCCKDACDebug			0x0f000000
#define		bCCKFalseAlarmEnable	0x8000
#define		bCCKFalseAlarmRead		0x4000
#define		bCCKTRSSI				0x7f
#define		bCCKRxAGCReport		0xfe
#define		bCCKRxReport_AntSel	0x80000000
#define		bCCKRxReport_MFOff		0x40000000
#define		bCCKRxRxReport_SQLoss	0x20000000
#define		bCCKRxReport_Pktloss	0x10000000
#define		bCCKRxReport_Lockedbit	0x08000000
#define		bCCKRxReport_RateError	0x04000000
#define		bCCKRxReport_RxRate	0x03000000
#define		bCCKRxFACounterLower	0xff
#define		bCCKRxFACounterUpper	0xff000000
#define		bCCKRxHPAGCStart		0xe000
#define		bCCKRxHPAGCFinal		0x1c00       		
#define		bCCKRxFalseAlarmEnable	0x8000
#define		bCCKFACounterFreeze	0x4000       		
#define		bCCKTxPathSel			0x10000000
#define		bCCKDefaultRxPath		0xc000000
#define		bCCKOptionRxPath		0x3000000

// 5. PageC(0xC00)
#define		bNumOfSTF				0x3	// Useless
#define		bShift_L					0xc0
#define		bGI_TH					0xc
#define		bRxPathA				0x1
#define		bRxPathB				0x2
#define		bRxPathC				0x4
#define		bRxPathD				0x8
#define		bTxPathA				0x1
#define		bTxPathB				0x2
#define		bTxPathC				0x4
#define		bTxPathD				0x8
#define		bTRSSIFreq				0x200
#define		bADCBackoff				0x3000
#define		bDFIRBackoff			0xc000
#define		bTRSSILatchPhase		0x10000
#define		bRxIDCOffset			0xff
#define		bRxQDCOffset			0xff00
#define		bRxDFIRMode			0x1800000
#define		bRxDCNFType			0xe000000
#define		bRXIQImb_A				0x3ff
#define		bRXIQImb_B				0xfc00
#define		bRXIQImb_C				0x3f0000
#define		bRXIQImb_D				0xffc00000
#define		bDC_dc_Notch			0x60000
#define		bRxNBINotch			0x1f000000
#define		bPD_TH					0xf
#define		bPD_TH_Opt2			0xc000
#define		bPWED_TH				0x700
#define		bIfMF_Win_L			0x800
#define		bPD_Option				0x1000
#define		bMF_Win_L				0xe000
#define		bBW_Search_L			0x30000
#define		bwin_enh_L				0xc0000
#define		bBW_TH					0x700000
#define		bED_TH2				0x3800000
#define		bBW_option				0x4000000
#define		bRatio_TH				0x18000000
#define		bWindow_L				0xe0000000
#define		bSBD_Option				0x1
#define		bFrame_TH				0x1c
#define		bFS_Option				0x60
#define		bDC_Slope_check		0x80
#define		bFGuard_Counter_DC_L	0xe00
#define		bFrame_Weight_Short	0x7000
#define		bSub_Tune				0xe00000
#define		bFrame_DC_Length		0xe000000
#define		bSBD_start_offset		0x30000000
#define		bFrame_TH_2			0x7
#define		bFrame_GI2_TH			0x38
#define		bGI2_Sync_en			0x40
#define		bSarch_Short_Early		0x300
#define		bSarch_Short_Late		0xc00
#define		bSarch_GI2_Late		0x70000
#define		bCFOAntSum				0x1
#define		bCFOAcc				0x2
#define		bCFOStartOffset			0xc
#define		bCFOLookBack			0x70
#define		bCFOSumWeight			0x80
#define		bDAGCEnable			0x10000
#define		bTXIQImb_A				0x3ff
#define		bTXIQImb_B				0xfc00
#define		bTXIQImb_C				0x3f0000
#define		bTXIQImb_D				0xffc00000
#define		bTxIDCOffset			0xff
#define		bTxQDCOffset			0xff00
#define		bTxDFIRMode			0x10000
#define		bTxPesudoNoiseOn		0x4000000
#define		bTxPesudoNoise_A		0xff
#define		bTxPesudoNoise_B		0xff00
#define		bTxPesudoNoise_C		0xff0000
#define		bTxPesudoNoise_D		0xff000000
#define		bCCADropOption			0x20000
#define		bCCADropThres			0xfff00000
#define		bEDCCA_H				0xf
#define		bEDCCA_L				0xf0
#define		bLambda_ED			0x300
#define		bRxInitialGain			0x7f
#define		bRxAntDivEn				0x80
#define		bRxAGCAddressForLNA	0x7f00
#define		bRxHighPowerFlow		0x8000
#define		bRxAGCFreezeThres		0xc0000
#define		bRxFreezeStep_AGC1	0x300000
#define		bRxFreezeStep_AGC2	0xc00000
#define		bRxFreezeStep_AGC3	0x3000000
#define		bRxFreezeStep_AGC0	0xc000000
#define		bRxRssi_Cmp_En			0x10000000
#define		bRxQuickAGCEn			0x20000000
#define		bRxAGCFreezeThresMode	0x40000000
#define		bRxOverFlowCheckType	0x80000000
#define		bRxAGCShift				0x7f
#define		bTRSW_Tri_Only			0x80
#define		bPowerThres			0x300
#define		bRxAGCEn				0x1
#define		bRxAGCTogetherEn		0x2
#define		bRxAGCMin				0x4
#define		bRxHP_Ini				0x7
#define		bRxHP_TRLNA			0x70
#define		bRxHP_RSSI				0x700
#define		bRxHP_BBP1				0x7000
#define		bRxHP_BBP2				0x70000
#define		bRxHP_BBP3				0x700000
#define		bRSSI_H					0x7f0000     //the threshold for high power
#define		bRSSI_Gen				0x7f000000   //the threshold for ant diversity
#define		bRxSettle_TRSW			0x7
#define		bRxSettle_LNA			0x38
#define		bRxSettle_RSSI			0x1c0
#define		bRxSettle_BBP			0xe00
#define		bRxSettle_RxHP			0x7000
#define		bRxSettle_AntSW_RSSI	0x38000
#define		bRxSettle_AntSW		0xc0000
#define		bRxProcessTime_DAGC	0x300000
#define		bRxSettle_HSSI			0x400000
#define		bRxProcessTime_BBPPW	0x800000
#define		bRxAntennaPowerShift	0x3000000
#define		bRSSITableSelect		0xc000000
#define		bRxHP_Final				0x7000000
#define		bRxHTSettle_BBP			0x7
#define		bRxHTSettle_HSSI		0x8
#define		bRxHTSettle_RxHP		0x70
#define		bRxHTSettle_BBPPW		0x80
#define		bRxHTSettle_Idle		0x300
#define		bRxHTSettle_Reserved	0x1c00
#define		bRxHTRxHPEn			0x8000
#define		bRxHTAGCFreezeThres	0x30000
#define		bRxHTAGCTogetherEn	0x40000
#define		bRxHTAGCMin			0x80000
#define		bRxHTAGCEn				0x100000
#define		bRxHTDAGCEn			0x200000
#define		bRxHTRxHP_BBP			0x1c00000
#define		bRxHTRxHP_Final		0xe0000000
#define		bRxPWRatioTH			0x3
#define		bRxPWRatioEn			0x4
#define		bRxMFHold				0x3800
#define		bRxPD_Delay_TH1		0x38
#define		bRxPD_Delay_TH2		0x1c0
#define		bRxPD_DC_COUNT_MAX	0x600
//#define bRxMF_Hold               0x3800
#define		bRxPD_Delay_TH			0x8000
#define		bRxProcess_Delay		0xf0000
#define		bRxSearchrange_GI2_Early	0x700000
#define		bRxFrame_Guard_Counter_L	0x3800000
#define		bRxSGI_Guard_L			0xc000000
#define		bRxSGI_Search_L		0x30000000
#define		bRxSGI_TH				0xc0000000
#define		bDFSCnt0				0xff
#define		bDFSCnt1				0xff00
#define		bDFSFlag				0xf0000       		
#define		bMFWeightSum			0x300000
#define		bMinIdxTH				0x7f000000       		
#define		bDAFormat				0x40000       		
#define		bTxChEmuEnable		0x01000000       		
#define		bTRSWIsolation_A		0x7f
#define		bTRSWIsolation_B		0x7f00
#define		bTRSWIsolation_C		0x7f0000
#define		bTRSWIsolation_D		0x7f000000       		
#define		bExtLNAGain				0x7c00          

// 6. PageE(0xE00)
#define		bSTBCEn				0x4	// Useless
#define		bAntennaMapping		0x10
#define		bNss					0x20
#define		bCFOAntSumD			0x200
#define		bPHYCounterReset		0x8000000
#define		bCFOReportGet			0x4000000
#define		bOFDMContinueTx		0x10000000
#define		bOFDMSingleCarrier		0x20000000
#define		bOFDMSingleTone		0x40000000
//#define bRxPath1                 0x01
//#define bRxPath2                 0x02
//#define bRxPath3                 0x04
//#define bRxPath4                 0x08
//#define bTxPath1                 0x10
//#define bTxPath2                 0x20
#define		bHTDetect			0x100
#define		bCFOEn				0x10000
#define		bCFOValue			0xfff00000
#define		bSigTone_Re		0x3f
#define		bSigTone_Im		0x7f00
#define		bCounter_CCA		0xffff
#define		bCounter_ParityFail	0xffff0000
#define		bCounter_RateIllegal		0xffff
#define		bCounter_CRC8Fail	0xffff0000
#define		bCounter_MCSNoSupport	0xffff
#define		bCounter_FastSync	0xffff
#define		bShortCFO			0xfff
#define		bShortCFOTLength	12   //total
#define		bShortCFOFLength	11   //fraction
#define		bLongCFO			0x7ff
#define		bLongCFOTLength	11
#define		bLongCFOFLength	11
#define		bTailCFO			0x1fff
#define		bTailCFOTLength		13
#define		bTailCFOFLength		12       		
#define		bmax_en_pwdB		0xffff
#define		bCC_power_dB		0xffff0000
#define		bnoise_pwdB		0xffff
#define		bPowerMeasTLength	10
#define		bPowerMeasFLength	3
#define		bRx_HT_BW			0x1
#define		bRxSC				0x6
#define		bRx_HT				0x8       		
#define		bNB_intf_det_on		0x1
#define		bIntf_win_len_cfg	0x30
#define		bNB_Intf_TH_cfg		0x1c0       		
#define		bRFGain				0x3f
#define		bTableSel			0x40
#define		bTRSW				0x80       		
#define		bRxSNR_A			0xff
#define		bRxSNR_B			0xff00
#define		bRxSNR_C			0xff0000
#define		bRxSNR_D			0xff000000
#define		bSNREVMTLength		8
#define		bSNREVMFLength		1       		
#define		bCSI1st				0xff
#define		bCSI2nd				0xff00
#define		bRxEVM1st			0xff0000
#define		bRxEVM2nd			0xff000000       		
#define		bSIGEVM			0xff
#define		bPWDB				0xff00
#define		bSGIEN				0x10000
       		
#define		bSFactorQAM1		0xf	// Useless
#define		bSFactorQAM2		0xf0
#define		bSFactorQAM3		0xf00
#define		bSFactorQAM4		0xf000
#define		bSFactorQAM5		0xf0000
#define		bSFactorQAM6		0xf0000
#define		bSFactorQAM7		0xf00000
#define		bSFactorQAM8		0xf000000
#define		bSFactorQAM9		0xf0000000
#define		bCSIScheme			0x100000
       		
#define		bNoiseLvlTopSet		0x3	// Useless
#define		bChSmooth			0x4
#define		bChSmoothCfg1		0x38
#define		bChSmoothCfg2		0x1c0
#define		bChSmoothCfg3		0xe00
#define		bChSmoothCfg4		0x7000
#define		bMRCMode			0x800000
#define		bTHEVMCfg			0x7000000
       		
#define		bLoopFitType		0x1	// Useless
#define		bUpdCFO			0x40
#define		bUpdCFOOffData		0x80
#define		bAdvUpdCFO			0x100
#define		bAdvTimeCtrl		0x800
#define		bUpdClko			0x1000
#define		bFC					0x6000
#define		bTrackingMode		0x8000
#define		bPhCmpEnable		0x10000
#define		bUpdClkoLTF		0x20000
#define		bComChCFO			0x40000
#define		bCSIEstiMode		0x80000
#define		bAdvUpdEqz			0x100000
#define		bUChCfg				0x7000000
#define		bUpdEqz			0x8000000

//Rx Pseduo noise
#define		bRxPesudoNoiseOn		0x20000000	// Useless
#define		bRxPesudoNoise_A		0xff
#define		bRxPesudoNoise_B		0xff00
#define		bRxPesudoNoise_C		0xff0000
#define		bRxPesudoNoise_D		0xff000000
#define		bPesudoNoiseState_A	0xffff
#define		bPesudoNoiseState_B	0xffff0000
#define		bPesudoNoiseState_C	0xffff
#define		bPesudoNoiseState_D	0xffff0000

//7. RF Register
//Zebra1
#define		bZebra1_HSSIEnable		0x8		// Useless
#define		bZebra1_TRxControl		0xc00
#define		bZebra1_TRxGainSetting	0x07f
#define		bZebra1_RxCorner		0xc00
#define		bZebra1_TxChargePump	0x38
#define		bZebra1_RxChargePump	0x7
#define		bZebra1_ChannelNum	0xf80
#define		bZebra1_TxLPFBW		0x400
#define		bZebra1_RxLPFBW		0x600

//Zebra4
#define		bRTL8256RegModeCtrl1	0x100	// Useless
#define		bRTL8256RegModeCtrl0	0x40
#define		bRTL8256_TxLPFBW		0x18
#define		bRTL8256_RxLPFBW		0x600

//RTL8258
#define		bRTL8258_TxLPFBW		0xc	// Useless
#define		bRTL8258_RxLPFBW		0xc00
#define		bRTL8258_RSSILPFBW	0xc0


//
// Other Definition
//

//byte endable for sb_write
#define		bByte0				0x1	// Useless
#define		bByte1				0x2
#define		bByte2				0x4
#define		bByte3				0x8
#define		bWord0				0x3
#define		bWord1				0xc
#define		bDWord				0xf

//for PutRegsetting & GetRegSetting BitMask
#define		bMaskByte0			0xff	// Reg 0xc50 rOFDM0_XAAGCCore~0xC6f
#define		bMaskByte1			0xff00
#define		bMaskByte2			0xff0000
#define		bMaskByte3			0xff000000
#define		bMaskHWord		0xffff0000
#define		bMaskLWord			0x0000ffff
#define		bMaskDWord		0xffffffff
#define		bMaskH3Bytes		0xffffff00
#define		bMask12Bits			0xfff
#define		bMaskH4Bits			0xf0000000	
#define		bMaskOFDM_D		0xffc00000
#define		bMaskCCK			0x3f3f3f3f

  		
#define		bEnable			0x1	// Useless
#define		bDisable		0x0
       		
#define		LeftAntenna		0x0	// Useless
#define		RightAntenna	0x1
       		
#define		tCheckTxStatus		500   //500ms // Useless
#define		tUpdateRxCounter	100   //100ms
       		
#define		rateCCK		0	// Useless
#define		rateOFDM	1
#define		rateHT		2

//define Register-End
#define		bPMAC_End			0x1ff	// Useless
#define		bFPGAPHY0_End		0x8ff
#define		bFPGAPHY1_End		0x9ff
#define		bCCKPHY0_End		0xaff
#define		bOFDMPHY0_End		0xcff
#define		bOFDMPHY1_End		0xdff

//define max debug item in each debug page
//#define bMaxItem_FPGA_PHY0        0x9
//#define bMaxItem_FPGA_PHY1        0x3
//#define bMaxItem_PHY_11B          0x16
//#define bMaxItem_OFDM_PHY0        0x29
//#define bMaxItem_OFDM_PHY1        0x0

#define		bPMACControl		0x0		// Useless
#define		bWMACControl		0x1
#define		bWNICControl		0x2
       		
#define		PathA			0x0	// Useless
#define		PathB			0x1
#define		PathC			0x2
#define		PathD			0x3

/*--------------------------Define Parameters-------------------------------*/


#endif	//__INC_HAL8192SPHYREG_H

