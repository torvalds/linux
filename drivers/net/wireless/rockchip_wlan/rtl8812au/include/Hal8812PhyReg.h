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
#ifndef __INC_HAL8812PHYREG_H__
#define __INC_HAL8812PHYREG_H__
/*--------------------------Define Parameters-------------------------------*/
//
// BB-PHY register PMAC 0x100 PHY 0x800 - 0xEFF
// 1. PMAC duplicate register due to connection: RF_Mode, TRxRN, NumOf L-STF
// 2. 0x800/0x900/0xA00/0xC00/0xD00/0xE00
// 3. RF register 0x00-2E
// 4. Bit Mask for BB/RF register
// 5. Other defintion for BB/RF R/W
//


// BB Register Definition

#define rCCAonSec_Jaguar		0x838
#define rPwed_TH_Jaguar			0x830

// BW and sideband setting
#define rBWIndication_Jaguar		0x834
#define rL1PeakTH_Jaguar		0x848
#define rRFMOD_Jaguar			0x8ac	//RF mode 
#define rADC_Buf_Clk_Jaguar		0x8c4
#define rRFECTRL_Jaguar			0x900
#define bRFMOD_Jaguar			0xc3
#define rCCK_System_Jaguar		0xa00   // for cck sideband
#define bCCK_System_Jaguar		0x10

// Block & Path enable
#define rOFDMCCKEN_Jaguar 		0x808 // OFDM/CCK block enable
#define bOFDMEN_Jaguar			0x20000000
#define bCCKEN_Jaguar			0x10000000
#define rRxPath_Jaguar			0x808	// Rx antenna
#define bRxPath_Jaguar			0xff
#define rTxPath_Jaguar			0x80c	// Tx antenna
#define bTxPath_Jaguar			0x0fffffff
#define rCCK_RX_Jaguar			0xa04	// for cck rx path selection
#define bCCK_RX_Jaguar			0x0c000000 
#define rVhtlen_Use_Lsig_Jaguar	0x8c3	// Use LSIG for VHT length

// RF read/write-related
#define rHSSIRead_Jaguar			0x8b0  // RF read addr
#define bHSSIRead_addr_Jaguar		0xff
#define bHSSIRead_trigger_Jaguar	0x100
#define rA_PIRead_Jaguar			0xd04 // RF readback with PI
#define rB_PIRead_Jaguar			0xd44 // RF readback with PI
#define rA_SIRead_Jaguar			0xd08 // RF readback with SI
#define rB_SIRead_Jaguar			0xd48 // RF readback with SI
#define rRead_data_Jaguar			0xfffff
#define rA_LSSIWrite_Jaguar			0xc90 // RF write addr
#define rB_LSSIWrite_Jaguar			0xe90 // RF write addr
#define bLSSIWrite_data_Jaguar		0x000fffff
#define bLSSIWrite_addr_Jaguar		0x0ff00000



// YN: mask the following register definition temporarily 
#define rFPGA0_XA_RFInterfaceOE			0x860	// RF Channel switch
#define rFPGA0_XB_RFInterfaceOE			0x864

#define rFPGA0_XAB_RFInterfaceSW		0x870	// RF Interface Software Control
#define rFPGA0_XCD_RFInterfaceSW		0x874

//#define rFPGA0_XAB_RFParameter		0x878	// RF Parameter
//#define rFPGA0_XCD_RFParameter		0x87c

//#define rFPGA0_AnalogParameter1		0x880	// Crystal cap setting RF-R/W protection for parameter4??
//#define rFPGA0_AnalogParameter2		0x884
//#define rFPGA0_AnalogParameter3		0x888
//#define rFPGA0_AdDaClockEn			0x888	// enable ad/da clock1 for dual-phy
//#define rFPGA0_AnalogParameter4		0x88c


// CCK TX scaling
#define rCCK_TxFilter1_Jaguar		0xa20
#define bCCK_TxFilter1_C0_Jaguar   	0x00ff0000
#define bCCK_TxFilter1_C1_Jaguar		0xff000000
#define rCCK_TxFilter2_Jaguar		0xa24
#define bCCK_TxFilter2_C2_Jaguar		0x000000ff
#define bCCK_TxFilter2_C3_Jaguar		0x0000ff00
#define bCCK_TxFilter2_C4_Jaguar		0x00ff0000
#define bCCK_TxFilter2_C5_Jaguar		0xff000000
#define rCCK_TxFilter3_Jaguar		0xa28
#define bCCK_TxFilter3_C6_Jaguar		0x000000ff
#define bCCK_TxFilter3_C7_Jaguar		0x0000ff00


// YN: mask the following register definition temporarily
//#define rPdp_AntA      					0xb00  
//#define rPdp_AntA_4    				0xb04
//#define rConfig_Pmpd_AntA 			0xb28
//#define rConfig_AntA 					0xb68
//#define rConfig_AntB 					0xb6c
//#define rPdp_AntB 					0xb70
//#define rPdp_AntB_4 					0xb74
//#define rConfig_Pmpd_AntB			0xb98
//#define rAPK							0xbd8

// RXIQC
#define rA_RxIQC_AB_Jaguar    	0xc10  //RxIQ imblance matrix coeff. A & B
#define rA_RxIQC_CD_Jaguar    	0xc14  //RxIQ imblance matrix coeff. C & D
#define rA_TxScale_Jaguar 		0xc1c  // Pah_A TX scaling factor
#define rB_TxScale_Jaguar 		0xe1c  // Path_B TX scaling factor
#define rB_RxIQC_AB_Jaguar    	0xe10  //RxIQ imblance matrix coeff. A & B
#define rB_RxIQC_CD_Jaguar    	0xe14  //RxIQ imblance matrix coeff. C & D
#define b_RxIQC_AC_Jaguar		0x02ff  // bit mask for IQC matrix element A & C
#define b_RxIQC_BD_Jaguar		0x02ff0000 // bit mask for IQC matrix element A & C


// DIG-related
#define rA_IGI_Jaguar				0xc50	// Initial Gain for path-A
#define rB_IGI_Jaguar				0xe50	// Initial Gain for path-B
#define rOFDM_FalseAlarm1_Jaguar	0xf48  // counter for break
#define rOFDM_FalseAlarm2_Jaguar	0xf4c  // counter for spoofing
#define rCCK_FalseAlarm_Jaguar        	0xa5c // counter for cck false alarm
#define b_FalseAlarm_Jaguar			0xffff
#define rCCK_CCA_Jaguar				0xa08	// cca threshold
#define bCCK_CCA_Jaguar				0x00ff0000

// Tx Power Ttraining-related
#define rA_TxPwrTraing_Jaguar		0xc54
#define rB_TxPwrTraing_Jaguar		0xe54

// Report-related
#define rOFDM_ShortCFOAB_Jaguar	0xf60  
#define rOFDM_LongCFOAB_Jaguar		0xf64
#define rOFDM_EndCFOAB_Jaguar		0xf70
#define rOFDM_AGCReport_Jaguar		0xf84
#define rOFDM_RxSNR_Jaguar			0xf88
#define rOFDM_RxEVMCSI_Jaguar		0xf8c
#define rOFDM_SIGReport_Jaguar		0xf90

// Misc functions
#define rEDCCA_Jaguar				0x8a4 // EDCCA
#define bEDCCA_Jaguar				0xffff
#define rAGC_table_Jaguar			0x82c   // AGC tabel select
#define bAGC_table_Jaguar			0x3
#define b_sel5g_Jaguar    				0x1000 // sel5g
#define b_LNA_sw_Jaguar				0x8000 // HW/WS control for LNA
#define rFc_area_Jaguar				0x860   // fc_area 
#define bFc_area_Jaguar				0x1ffe000
#define rSingleTone_ContTx_Jaguar	0x914

// RFE
#define rA_RFE_Pinmux_Jaguar	0xcb0  // Path_A RFE cotrol pinmux
#define rB_RFE_Pinmux_Jaguar	0xeb0 // Path_B RFE control pinmux
#define rA_RFE_Inv_Jaguar		0xcb4  // Path_A RFE cotrol   
#define rB_RFE_Inv_Jaguar		0xeb4 // Path_B RFE control
#define rA_RFE_Jaguar			0xcb8  // Path_A RFE cotrol   
#define rB_RFE_Jaguar			0xeb8 // Path_B RFE control
#define r_ANTSEL_SW_Jaguar		0x900 // ANTSEL SW Control
#define bMask_RFEInv_Jaguar		0x3ff00000
#define bMask_AntselPathFollow_Jaguar 0x00030000

// TX AGC 
#define rTxAGC_A_CCK11_CCK1_JAguar				0xc20
#define rTxAGC_A_Ofdm18_Ofdm6_JAguar				0xc24
#define rTxAGC_A_Ofdm54_Ofdm24_JAguar			0xc28
#define rTxAGC_A_MCS3_MCS0_JAguar					0xc2c
#define rTxAGC_A_MCS7_MCS4_JAguar					0xc30
#define rTxAGC_A_MCS11_MCS8_JAguar				0xc34
#define rTxAGC_A_MCS15_MCS12_JAguar				0xc38
#define rTxAGC_A_Nss1Index3_Nss1Index0_JAguar	0xc3c
#define rTxAGC_A_Nss1Index7_Nss1Index4_JAguar	0xc40
#define rTxAGC_A_Nss2Index1_Nss1Index8_JAguar	0xc44
#define rTxAGC_A_Nss2Index5_Nss2Index2_JAguar	0xc48
#define rTxAGC_A_Nss2Index9_Nss2Index6_JAguar	0xc4c
#define rTxAGC_B_CCK11_CCK1_JAguar				0xe20
#define rTxAGC_B_Ofdm18_Ofdm6_JAguar				0xe24
#define rTxAGC_B_Ofdm54_Ofdm24_JAguar			0xe28
#define rTxAGC_B_MCS3_MCS0_JAguar					0xe2c
#define rTxAGC_B_MCS7_MCS4_JAguar					0xe30
#define rTxAGC_B_MCS11_MCS8_JAguar				0xe34
#define rTxAGC_B_MCS15_MCS12_JAguar				0xe38
#define rTxAGC_B_Nss1Index3_Nss1Index0_JAguar		0xe3c
#define rTxAGC_B_Nss1Index7_Nss1Index4_JAguar		0xe40
#define rTxAGC_B_Nss2Index1_Nss1Index8_JAguar		0xe44
#define rTxAGC_B_Nss2Index5_Nss2Index2_JAguar		0xe48
#define rTxAGC_B_Nss2Index9_Nss2Index6_JAguar		0xe4c
#define bTxAGC_byte0_Jaguar							0xff
#define bTxAGC_byte1_Jaguar							0xff00
#define bTxAGC_byte2_Jaguar							0xff0000
#define bTxAGC_byte3_Jaguar							0xff000000

// IQK YN: temporaily mask this part
//#define rFPGA0_IQK					0xe28
//#define rTx_IQK_Tone_A				0xe30
//#define rRx_IQK_Tone_A				0xe34
//#define rTx_IQK_PI_A					0xe38
//#define rRx_IQK_PI_A					0xe3c

//#define rTx_IQK 						0xe40
//#define rRx_IQK						0xe44
//#define rIQK_AGC_Pts					0xe48
//#define rIQK_AGC_Rsp					0xe4c
//#define rTx_IQK_Tone_B				0xe50
//#define rRx_IQK_Tone_B				0xe54
//#define rTx_IQK_PI_B					0xe58
//#define rRx_IQK_PI_B					0xe5c
//#define rIQK_AGC_Cont				0xe60


// AFE-related
#define rA_AFEPwr1_Jaguar					0xc60 // dynamic AFE power control
#define rA_AFEPwr2_Jaguar					0xc64 // dynamic AFE power control
#define rA_Rx_WaitCCA_Tx_CCKRFON_Jaguar	0xc68
#define rA_Tx_CCKBBON_OFDMRFON_Jaguar	0xc6c
#define rA_Tx_OFDMBBON_Tx2Rx_Jaguar		0xc70
#define rA_Tx2Tx_RXCCK_Jaguar				0xc74
#define rA_Rx_OFDM_WaitRIFS_Jaguar			0xc78
#define rA_Rx2Rx_BT_Jaguar					0xc7c
#define rA_sleep_nav_Jaguar 					0xc80
#define rA_pmpd_Jaguar 						0xc84
#define rB_AFEPwr1_Jaguar					0xe60 // dynamic AFE power control
#define rB_AFEPwr2_Jaguar					0xe64 // dynamic AFE power control
#define rB_Rx_WaitCCA_Tx_CCKRFON_Jaguar	0xe68
#define rB_Tx_CCKBBON_OFDMRFON_Jaguar	0xe6c
#define rB_Tx_OFDMBBON_Tx2Rx_Jaguar		0xe70
#define rB_Tx2Tx_RXCCK_Jaguar				0xe74
#define rB_Rx_OFDM_WaitRIFS_Jaguar			0xe78
#define rB_Rx2Rx_BT_Jaguar					0xe7c
#define rB_sleep_nav_Jaguar 					0xe80
#define rB_pmpd_Jaguar 						0xe84


// YN: mask these registers temporaily
//#define rTx_Power_Before_IQK_A		0xe94
//#define rTx_Power_After_IQK_A			0xe9c

//#define rRx_Power_Before_IQK_A		0xea0
//#define rRx_Power_Before_IQK_A_2		0xea4
//#define rRx_Power_After_IQK_A			0xea8
//#define rRx_Power_After_IQK_A_2		0xeac

//#define rTx_Power_Before_IQK_B		0xeb4
//#define rTx_Power_After_IQK_B			0xebc

//#define rRx_Power_Before_IQK_B		0xec0
//#define rRx_Power_Before_IQK_B_2		0xec4
//#define rRx_Power_After_IQK_B			0xec8
//#define rRx_Power_After_IQK_B_2		0xecc


// RSSI Dump
#define rA_RSSIDump_Jaguar 			0xBF0
#define rB_RSSIDump_Jaguar 			0xBF1
#define rS1_RXevmDump_Jaguar		0xBF4 
#define rS2_RXevmDump_Jaguar 		0xBF5
#define rA_RXsnrDump_Jaguar		0xBF6
#define rB_RXsnrDump_Jaguar		0xBF7
#define rA_CfoShortDump_Jaguar		0xBF8 
#define rB_CfoShortDump_Jaguar		0xBFA
#define rA_CfoLongDump_Jaguar		0xBEC
#define rB_CfoLongDump_Jaguar		0xBEE
 

// RF Register
//
#define RF_AC_Jaguar				0x00	// 
#define RF_RF_Top_Jaguar			0x07	// 
#define RF_TXLOK_Jaguar				0x08	// 
#define RF_TXAPK_Jaguar				0x0B
#define RF_CHNLBW_Jaguar 			0x18	// RF channel and BW switch
#define RF_RCK1_Jaguar				0x1c	// 
#define RF_RCK2_Jaguar				0x1d
#define RF_RCK3_Jaguar   			0x1e
#define RF_ModeTableAddr			0x30
#define RF_ModeTableData0			0x31
#define RF_ModeTableData1			0x32
#define RF_TxLCTank_Jaguar          	0x54
#define RF_APK_Jaguar				0x63
#define RF_LCK						0xB4
#define RF_WeLut_Jaguar				0xEF

#define bRF_CHNLBW_MOD_AG_Jaguar	0x70300
#define bRF_CHNLBW_BW 				0xc00


//
// RL6052 Register definition
//
#define RF_AC						0x00	// 
#define RF_IPA_A					0x0C	// 
#define RF_TXBIAS_A					0x0D
#define RF_BS_PA_APSET_G9_G11		0x0E
#define RF_MODE1					0x10	// 
#define RF_MODE2					0x11	// 
#define RF_CHNLBW					0x18	// RF channel and BW switch
#define RF_RCK_OS					0x30	// RF TX PA control
#define RF_TXPA_G1					0x31	// RF TX PA control
#define RF_TXPA_G2					0x32	// RF TX PA control
#define RF_TXPA_G3					0x33	// RF TX PA control
#define RF_0x52 						0x52
#define RF_WE_LUT					0xEF

//
//Bit Mask
//
// 1. Page1(0x100)
#define bBBResetB					0x100	// Useless now?
#define bGlobalResetB				0x200
#define bOFDMTxStart				0x4
#define bCCKTxStart					0x8
#define bCRC32Debug					0x100
#define bPMACLoopback				0x10
#define bTxLSIG						0xffffff
#define bOFDMTxRate					0xf
#define bOFDMTxReserved			0x10
#define bOFDMTxLength				0x1ffe0
#define bOFDMTxParity				0x20000
#define bTxHTSIG1					0xffffff
#define bTxHTMCSRate				0x7f
#define bTxHTBW						0x80
#define bTxHTLength					0xffff00
#define bTxHTSIG2					0xffffff
#define bTxHTSmoothing				0x1
#define bTxHTSounding				0x2
#define bTxHTReserved				0x4
#define bTxHTAggreation				0x8
#define bTxHTSTBC					0x30
#define bTxHTAdvanceCoding			0x40
#define bTxHTShortGI					0x80
#define bTxHTNumberHT_LTF			0x300
#define bTxHTCRC8					0x3fc00
#define bCounterReset				0x10000
#define bNumOfOFDMTx				0xffff
#define bNumOfCCKTx					0xffff0000
#define bTxIdleInterval				0xffff
#define bOFDMService				0xffff0000
#define bTxMACHeader				0xffffffff
#define bTxDataInit					0xff
#define bTxHTMode					0x100
#define bTxDataType					0x30000
#define bTxRandomSeed				0xffffffff
#define bCCKTxPreamble				0x1
#define bCCKTxSFD					0xffff0000
#define bCCKTxSIG					0xff
#define bCCKTxService				0xff00
#define bCCKLengthExt				0x8000
#define bCCKTxLength				0xffff0000
#define bCCKTxCRC16					0xffff
#define bCCKTxStatus					0x1
#define bOFDMTxStatus				0x2


//
// 1. PMAC duplicate register due to connection: RF_Mode, TRxRN, NumOf L-STF
// 1. Page1(0x100)
//
#define rPMAC_Reset					0x100
#define rPMAC_TxStart				0x104
#define rPMAC_TxLegacySIG			0x108
#define rPMAC_TxHTSIG1				0x10c
#define rPMAC_TxHTSIG2				0x110
#define rPMAC_PHYDebug				0x114
#define rPMAC_TxPacketNum			0x118
#define rPMAC_TxIdle					0x11c
#define rPMAC_TxMACHeader0			0x120
#define rPMAC_TxMACHeader1			0x124
#define rPMAC_TxMACHeader2			0x128
#define rPMAC_TxMACHeader3			0x12c
#define rPMAC_TxMACHeader4			0x130
#define rPMAC_TxMACHeader5			0x134
#define rPMAC_TxDataType			0x138
#define rPMAC_TxRandomSeed		0x13c
#define rPMAC_CCKPLCPPreamble		0x140
#define rPMAC_CCKPLCPHeader		0x144
#define rPMAC_CCKCRC16				0x148
#define rPMAC_OFDMRxCRC32OK		0x170
#define rPMAC_OFDMRxCRC32Er		0x174
#define rPMAC_OFDMRxParityEr		0x178
#define rPMAC_OFDMRxCRC8Er			0x17c
#define rPMAC_CCKCRxRC16Er			0x180
#define rPMAC_CCKCRxRC32Er			0x184
#define rPMAC_CCKCRxRC32OK			0x188
#define rPMAC_TxStatus				0x18c

//
// 3. Page8(0x800)
//
#define rFPGA0_RFMOD				0x800	//RF mode & CCK TxSC // RF BW Setting??

#define rFPGA0_TxInfo				0x804	// Status report??
#define rFPGA0_PSDFunction			0x808
#define rFPGA0_TxGainStage			0x80c	// Set TX PWR init gain?

#define rFPGA0_XA_HSSIParameter1	0x820	// RF 3 wire register
#define rFPGA0_XA_HSSIParameter2	0x824
#define rFPGA0_XB_HSSIParameter1	0x828
#define rFPGA0_XB_HSSIParameter2	0x82c

#define rFPGA0_XAB_SwitchControl	0x858	// RF Channel switch
#define rFPGA0_XCD_SwitchControl	0x85c

#define rFPGA0_XAB_RFParameter		0x878	// RF Parameter
#define rFPGA0_XCD_RFParameter		0x87c

#define rFPGA0_AnalogParameter1	0x880	// Crystal cap setting RF-R/W protection for parameter4??
#define rFPGA0_AnalogParameter2	0x884
#define rFPGA0_AnalogParameter3	0x888
#define rFPGA0_AdDaClockEn			0x888	// enable ad/da clock1 for dual-phy
#define rFPGA0_AnalogParameter4	0x88c
#define rFPGA0_XB_LSSIReadBack		0x8a4
#define rFPGA0_XCD_RFPara	0x8b4

//
// 4. Page9(0x900)
//
#define rFPGA1_RFMOD				0x900	//RF mode & OFDM TxSC // RF BW Setting??

#define rFPGA1_TxBlock				0x904	// Useless now
#define rFPGA1_DebugSelect			0x908	// Useless now
#define rFPGA1_TxInfo				0x90c	// Useless now // Status report??

//
// PageA(0xA00)
//
#define rCCK0_System				0xa00
#define rCCK0_AFESetting				0xa04	// Disable init gain now // Select RX path by RSSI
#define	rCCK0_DSPParameter2			0xa1c	//SQ threshold
#define rCCK0_TxFilter1				0xa20
#define rCCK0_TxFilter2				0xa24
#define rCCK0_DebugPort				0xa28	//debug port and Tx filter3
#define	rCCK0_FalseAlarmReport			0xa2c	//0xa2d	useless now 0xa30-a4f channel report

//
// PageB(0xB00)
//
#define rPdp_AntA      				0xb00  
#define rPdp_AntA_4    				0xb04
#define rConfig_Pmpd_AntA 			0xb28
#define rConfig_AntA 					0xb68
#define rConfig_AntB 					0xb6c
#define rPdp_AntB 					0xb70
#define rPdp_AntB_4 					0xb74
#define rConfig_Pmpd_AntB			0xb98
#define rAPK							0xbd8

//
// 6. PageC(0xC00)
//
#define rOFDM0_LSTF					0xc00

#define rOFDM0_TRxPathEnable		0xc04
#define rOFDM0_TRMuxPar			0xc08
#define rOFDM0_TRSWIsolation		0xc0c

#define rOFDM0_XARxAFE				0xc10  //RxIQ DC offset, Rx digital filter, DC notch filter
#define rOFDM0_XARxIQImbalance    	0xc14  //RxIQ imblance matrix
#define rOFDM0_XBRxAFE            		0xc18
#define rOFDM0_XBRxIQImbalance    	0xc1c
#define rOFDM0_XCRxAFE            		0xc20
#define rOFDM0_XCRxIQImbalance    	0xc24
#define rOFDM0_XDRxAFE            		0xc28
#define rOFDM0_XDRxIQImbalance    	0xc2c

#define rOFDM0_RxDetector1			0xc30  //PD,BW & SBD	// DM tune init gain
#define rOFDM0_RxDetector2			0xc34  //SBD & Fame Sync. 
#define rOFDM0_RxDetector3			0xc38  //Frame Sync.
#define rOFDM0_RxDetector4			0xc3c  //PD, SBD, Frame Sync & Short-GI

#define rOFDM0_RxDSP				0xc40  //Rx Sync Path
#define rOFDM0_CFOandDAGC			0xc44  //CFO & DAGC
#define rOFDM0_CCADropThreshold	0xc48 //CCA Drop threshold
#define rOFDM0_ECCAThreshold		0xc4c // energy CCA

#define rOFDM0_XAAGCCore1			0xc50	// DIG
#define rOFDM0_XAAGCCore2			0xc54
#define rOFDM0_XBAGCCore1			0xc58
#define rOFDM0_XBAGCCore2			0xc5c
#define rOFDM0_XCAGCCore1			0xc60
#define rOFDM0_XCAGCCore2			0xc64
#define rOFDM0_XDAGCCore1			0xc68
#define rOFDM0_XDAGCCore2			0xc6c

#define rOFDM0_AGCParameter1		0xc70
#define rOFDM0_AGCParameter2		0xc74
#define rOFDM0_AGCRSSITable		0xc78
#define rOFDM0_HTSTFAGC			0xc7c

#define rOFDM0_XATxIQImbalance		0xc80	// TX PWR TRACK and DIG
#define rOFDM0_XATxAFE				0xc84
#define rOFDM0_XBTxIQImbalance		0xc88
#define rOFDM0_XBTxAFE				0xc8c
#define rOFDM0_XCTxIQImbalance		0xc90
#define rOFDM0_XCTxAFE            		0xc94
#define rOFDM0_XDTxIQImbalance		0xc98
#define rOFDM0_XDTxAFE				0xc9c

#define rOFDM0_RxIQExtAnta			0xca0
#define rOFDM0_TxCoeff1				0xca4
#define rOFDM0_TxCoeff2				0xca8
#define rOFDM0_TxCoeff3				0xcac
#define rOFDM0_TxCoeff4				0xcb0
#define rOFDM0_TxCoeff5				0xcb4
#define rOFDM0_TxCoeff6				0xcb8
#define rOFDM0_RxHPParameter		0xce0
#define rOFDM0_TxPseudoNoiseWgt	0xce4
#define rOFDM0_FrameSync			0xcf0
#define rOFDM0_DFSReport			0xcf4

//
// 7. PageD(0xD00)
//
#define rOFDM1_LSTF					0xd00
#define rOFDM1_TRxPathEnable		0xd04

//
// 8. PageE(0xE00)
//
#define rTxAGC_A_Rate18_06			0xe00
#define rTxAGC_A_Rate54_24			0xe04
#define rTxAGC_A_CCK1_Mcs32		0xe08
#define rTxAGC_A_Mcs03_Mcs00		0xe10
#define rTxAGC_A_Mcs07_Mcs04		0xe14
#define rTxAGC_A_Mcs11_Mcs08		0xe18
#define rTxAGC_A_Mcs15_Mcs12		0xe1c

#define rTxAGC_B_Rate18_06			0x830
#define rTxAGC_B_Rate54_24			0x834
#define rTxAGC_B_CCK1_55_Mcs32	0x838
#define rTxAGC_B_Mcs03_Mcs00		0x83c
#define rTxAGC_B_Mcs07_Mcs04		0x848
#define rTxAGC_B_Mcs11_Mcs08		0x84c
#define rTxAGC_B_Mcs15_Mcs12		0x868
#define rTxAGC_B_CCK11_A_CCK2_11	0x86c

#define rFPGA0_IQK					0xe28
#define rTx_IQK_Tone_A				0xe30
#define rRx_IQK_Tone_A				0xe34
#define rTx_IQK_PI_A				0xe38
#define rRx_IQK_PI_A				0xe3c

#define rTx_IQK 						0xe40
#define rRx_IQK						0xe44
#define rIQK_AGC_Pts					0xe48
#define rIQK_AGC_Rsp				0xe4c
#define rTx_IQK_Tone_B				0xe50
#define rRx_IQK_Tone_B				0xe54
#define rTx_IQK_PI_B					0xe58
#define rRx_IQK_PI_B					0xe5c
#define rIQK_AGC_Cont				0xe60

#define rBlue_Tooth					0xe6c
#define rRx_Wait_CCA				0xe70
#define rTx_CCK_RFON				0xe74
#define rTx_CCK_BBON				0xe78
#define rTx_OFDM_RFON				0xe7c
#define rTx_OFDM_BBON				0xe80
#define rTx_To_Rx					0xe84
#define rTx_To_Tx					0xe88
#define rRx_CCK						0xe8c

#define rTx_Power_Before_IQK_A		0xe94
#define rTx_Power_After_IQK_A		0xe9c

#define rRx_Power_Before_IQK_A		0xea0
#define rRx_Power_Before_IQK_A_2	0xea4
#define rRx_Power_After_IQK_A		0xea8
#define rRx_Power_After_IQK_A_2		0xeac

#define rTx_Power_Before_IQK_B		0xeb4
#define rTx_Power_After_IQK_B		0xebc

#define rRx_Power_Before_IQK_B		0xec0
#define rRx_Power_Before_IQK_B_2	0xec4
#define rRx_Power_After_IQK_B		0xec8
#define rRx_Power_After_IQK_B_2		0xecc

#define rRx_OFDM					0xed0
#define rRx_Wait_RIFS 				0xed4
#define rRx_TO_Rx 					0xed8
#define rStandby 						0xedc
#define rSleep 						0xee0
#define rPMPD_ANAEN				0xeec


// 2. Page8(0x800)
#define bRFMOD						0x1	// Reg 0x800 rFPGA0_RFMOD
#define bJapanMode					0x2
#define bCCKTxSC					0x30
#define bCCKEn						0x1000000
#define bOFDMEn						0x2000000
#define bXBTxAGC                  			0xf00	// Reg 80c rFPGA0_TxGainStage
#define bXCTxAGC                  			0xf000
#define bXDTxAGC                  			0xf0000

// 4. PageA(0xA00)
#define bCCKBBMode                			0x3	// Useless
#define bCCKTxPowerSaving         		0x80
#define bCCKRxPowerSaving         		0x40

#define bCCKSideBand              		0x10	// Reg 0xa00 rCCK0_System 20/40 switch

#define bCCKScramble              		0x8	// Useless
#define bCCKAntDiversity    		      	0x8000
#define bCCKCarrierRecovery   	    	0x4000
#define bCCKTxRate           		     	0x3000
#define bCCKDCCancel             	 		0x0800
#define bCCKISICancel             			0x0400
#define bCCKMatchFilter           		0x0200
#define bCCKEqualizer             			0x0100
#define bCCKPreambleDetect       	 	0x800000
#define bCCKFastFalseCCA          		0x400000
#define bCCKChEstStart            		0x300000
#define bCCKCCACount              		0x080000
#define bCCKcs_lim                			0x070000
#define bCCKBistMode              			0x80000000
#define bCCKCCAMask             	  		0x40000000
#define bCCKTxDACPhase         	   	0x4
#define bCCKRxADCPhase         	   	0x20000000   //r_rx_clk
#define bCCKr_cp_mode0         	   	0x0100
#define bCCKTxDCOffset           	 	0xf0
#define bCCKRxDCOffset           	 	0xf
#define bCCKCCAMode              	 		0xc000
#define bCCKFalseCS_lim           		0x3f00
#define bCCKCS_ratio              			0xc00000
#define bCCKCorgBit_sel           		0x300000
#define bCCKPD_lim                			0x0f0000
#define bCCKNewCCA                		0x80000000
#define bCCKRxHPofIG              		0x8000
#define bCCKRxIG                  			0x7f00
#define bCCKLNAPolarity           		0x800000
#define bCCKRx1stGain             		0x7f0000
#define bCCKRFExtend              		0x20000000 //CCK Rx Iinital gain polarity
#define bCCKRxAGCSatLevel        	 	0x1f000000
#define bCCKRxAGCSatCount       	  	0xe0
#define bCCKRxRFSettle            		0x1f       //AGCsamp_dly
#define bCCKFixedRxAGC           	 	0x8000
//#define bCCKRxAGCFormat         	 	0x4000   //remove to HSSI register 0x824
#define bCCKAntennaPolarity      	 	0x2000
#define bCCKTxFilterType          		0x0c00
#define bCCKRxAGCReportType   	   	0x0300
#define bCCKRxDAGCEn              		0x80000000
#define bCCKRxDAGCPeriod        	  	0x20000000
#define bCCKRxDAGCSatLevel     	   	0x1f000000
#define bCCKTimingRecovery       	 	0x800000
#define bCCKTxC0                  			0x3f0000
#define bCCKTxC1                  			0x3f000000
#define bCCKTxC2                  			0x3f
#define bCCKTxC3                  			0x3f00
#define bCCKTxC4                  			0x3f0000
#define bCCKTxC5                  			0x3f000000
#define bCCKTxC6                  			0x3f
#define bCCKTxC7                  			0x3f00
#define bCCKDebugPort             		0xff0000
#define bCCKDACDebug              		0x0f000000
#define bCCKFalseAlarmEnable      		0x8000
#define bCCKFalseAlarmRead        		0x4000
#define bCCKTRSSI                 			0x7f
#define bCCKRxAGCReport           		0xfe
#define bCCKRxReport_AntSel       		0x80000000
#define bCCKRxReport_MFOff        		0x40000000
#define bCCKRxRxReport_SQLoss     	0x20000000
#define bCCKRxReport_Pktloss      		0x10000000
#define bCCKRxReport_Lockedbit    	0x08000000
#define bCCKRxReport_RateError    	0x04000000
#define bCCKRxReport_RxRate       		0x03000000
#define bCCKRxFACounterLower      	0xff
#define bCCKRxFACounterUpper      	0xff000000
#define bCCKRxHPAGCStart          		0xe000
#define bCCKRxHPAGCFinal          		0x1c00       		
#define bCCKRxFalseAlarmEnable    	0x8000
#define bCCKFACounterFreeze       		0x4000       		
#define bCCKTxPathSel             		0x10000000
#define bCCKDefaultRxPath         		0xc000000
#define bCCKOptionRxPath          		0x3000000

// 6. PageE(0xE00)
#define bSTBCEn                  			0x4	// Useless
#define bAntennaMapping          		0x10
#define bNss                     				0x20
#define bCFOAntSumD              		0x200
#define bPHYCounterReset         		0x8000000
#define bCFOReportGet            			0x4000000
#define bOFDMContinueTx          		0x10000000
#define bOFDMSingleCarrier       		0x20000000
#define bOFDMSingleTone          		0x40000000


//
// Other Definition
//

#define bEnable                   0x1	// Useless
#define bDisable                  0x0

//byte endable for srwrite
#define bByte0                    		0x1	// Useless
#define bByte1                    		0x2
#define bByte2                    		0x4
#define bByte3                    		0x8
#define bWord0                    		0x3
#define bWord1                    		0xc
#define bDWord                    		0xf

//for PutRegsetting & GetRegSetting BitMask
#define bMaskByte0                		0xff	// Reg 0xc50 rOFDM0_XAAGCCore~0xC6f
#define bMaskByte1                		0xff00
#define bMaskByte2                		0xff0000
#define bMaskByte3                		0xff000000
#define bMaskHWord                	0xffff0000
#define bMaskLWord                		0x0000ffff
#define bMaskDWord                	0xffffffff
#define bMaskH3Bytes				0xffffff00
#define bMask12Bits				0xfff	
#define bMaskH4Bits				0xf0000000	
#define bMaskOFDM_D			0xffc00000
#define bMaskCCK				0x3f3f3f3f


/*--------------------------Define Parameters-------------------------------*/


#endif

