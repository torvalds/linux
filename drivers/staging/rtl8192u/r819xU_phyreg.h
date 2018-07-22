/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _R819XU_PHYREG_H
#define _R819XU_PHYREG_H


#define   RF_DATA				0x1d4					/* FW will write RF data in the register.*/

/* page8 */
#define rFPGA0_RFMOD				0x800  /* RF mode & CCK TxSC */
#define rFPGA0_TxGainStage			0x80c
#define rFPGA0_XA_HSSIParameter1	0x820
#define rFPGA0_XA_HSSIParameter2	0x824
#define rFPGA0_XB_HSSIParameter1	0x828
#define rFPGA0_XB_HSSIParameter2	0x82c
#define rFPGA0_XC_HSSIParameter1	0x830
#define rFPGA0_XC_HSSIParameter2	0x834
#define rFPGA0_XD_HSSIParameter1	0x838
#define rFPGA0_XD_HSSIParameter2	0x83c
#define rFPGA0_XA_LSSIParameter		0x840
#define rFPGA0_XB_LSSIParameter		0x844
#define rFPGA0_XC_LSSIParameter		0x848
#define rFPGA0_XD_LSSIParameter		0x84c
#define rFPGA0_XAB_SwitchControl	0x858
#define rFPGA0_XCD_SwitchControl	0x85c
#define rFPGA0_XA_RFInterfaceOE		0x860
#define rFPGA0_XB_RFInterfaceOE		0x864
#define rFPGA0_XC_RFInterfaceOE		0x868
#define rFPGA0_XD_RFInterfaceOE		0x86c
#define rFPGA0_XAB_RFInterfaceSW	0x870
#define rFPGA0_XCD_RFInterfaceSW	0x874
#define rFPGA0_XAB_RFParameter		0x878
#define rFPGA0_XCD_RFParameter		0x87c
#define rFPGA0_AnalogParameter1		0x880
#define rFPGA0_AnalogParameter4		0x88c
#define rFPGA0_XA_LSSIReadBack		0x8a0
#define rFPGA0_XB_LSSIReadBack		0x8a4
#define rFPGA0_XC_LSSIReadBack		0x8a8
#define rFPGA0_XD_LSSIReadBack		0x8ac
#define rFPGA0_XAB_RFInterfaceRB	0x8e0
#define rFPGA0_XCD_RFInterfaceRB	0x8e4

/* page 9 */
#define rFPGA1_RFMOD				0x900  /* RF mode & OFDM TxSC */

/* page a */
#define rCCK0_System				0xa00
#define rCCK0_AFESetting			0xa04
#define rCCK0_CCA					0xa08
#define rCCK0_TxFilter1				0xa20
#define rCCK0_TxFilter2				0xa24
#define rCCK0_DebugPort				0xa28  /* debug port and Tx filter3 */

/* page c */
#define rOFDM0_TRxPathEnable		0xc04
#define rOFDM0_XARxAFE				0xc10  /* RxIQ DC offset, Rx digital filter, DC notch filter */
#define rOFDM0_XARxIQImbalance		0xc14  /* RxIQ imblance matrix */
#define rOFDM0_XBRxAFE				0xc18
#define rOFDM0_XBRxIQImbalance		0xc1c
#define rOFDM0_XCRxAFE				0xc20
#define rOFDM0_XCRxIQImbalance		0xc24
#define rOFDM0_XDRxAFE				0xc28
#define rOFDM0_XDRxIQImbalance		0xc2c
#define rOFDM0_RxDetector1			0xc30  /* PD,BW & SBD */
#define rOFDM0_RxDetector2			0xc34  /* SBD & Fame Sync.*/
#define rOFDM0_RxDetector3			0xc38  /* Frame Sync.*/
#define rOFDM0_ECCAThreshold		0xc4c /* energy CCA */
#define rOFDM0_XAAGCCore1		0xc50
#define rOFDM0_XAAGCCore2		0xc54
#define rOFDM0_XBAGCCore1		0xc58
#define rOFDM0_XBAGCCore2		0xc5c
#define rOFDM0_XCAGCCore1		0xc60
#define rOFDM0_XCAGCCore2		0xc64
#define rOFDM0_XDAGCCore1		0xc68
#define rOFDM0_XDAGCCore2		0xc6c
#define rOFDM0_XATxIQImbalance		0xc80
#define rOFDM0_XATxAFE				0xc84
#define rOFDM0_XBTxIQImbalance		0xc88
#define rOFDM0_XBTxAFE				0xc8c
#define rOFDM0_XCTxIQImbalance		0xc90
#define rOFDM0_XCTxAFE				0xc94
#define rOFDM0_XDTxIQImbalance		0xc98
#define rOFDM0_XDTxAFE				0xc9c


/* page d */
#define rOFDM1_LSTF				0xd00
#define rOFDM1_TRxPathEnable		0xd04

/* page e */
#define rTxAGC_Rate18_06			0xe00
#define rTxAGC_Rate54_24			0xe04
#define rTxAGC_CCK_Mcs32			0xe08
#define rTxAGC_Mcs03_Mcs00			0xe10
#define rTxAGC_Mcs07_Mcs04			0xe14
#define rTxAGC_Mcs11_Mcs08			0xe18
#define rTxAGC_Mcs15_Mcs12			0xe1c


/* RF
 * Zebra1
 */
#define rZebra1_Channel				0x7

/* Zebra4 */
#define rGlobalCtrl				0

/* Bit Mask
 * page-8
 */
#define bRFMOD						0x1
#define bCCKEn						0x1000000
#define bOFDMEn						0x2000000
#define bXBTxAGC					0xf00
#define bXCTxAGC					0xf000
#define b3WireDataLength			0x800
#define b3WireAddressLength			0x400
#define bRFSI_RFENV				0x10
#define bLSSIReadAddress			0x3f000000   /* LSSI "Read" Address */
#define bLSSIReadEdge				0x80000000   /* LSSI "Read" edge signal */
#define bLSSIReadBackData			0xfff
#define bXtalCap					0x0f000000

/* page-a */
#define bCCKSideBand				0x10

/* page d */
#define bSTBCEn                  0x4
#define bAntennaMapping          0x10
#define bNss                     0x20
#define bCFOAntSumD              0x200
#define bPHYCounterReset         0x8000000
#define bCFOReportGet            0x4000000
#define bOFDMContinueTx          0x10000000
#define bOFDMSingleCarrier       0x20000000
#define bOFDMSingleTone          0x40000000
/* #define bRxPath1                 0x01
 * #define bRxPath2                 0x02
 * #define bRxPath3                 0x04
 * #define bRxPath4                 0x08
 * #define bTxPath1                 0x10
 * #define bTxPath2                 0x20
 */
#define bHTDetect                0x100
#define bCFOEn                   0x10000
#define bCFOValue                0xfff00000
#define bSigTone_Re              0x3f
#define bSigTone_Im              0x7f00
#define bCounter_CCA             0xffff
#define bCounter_ParityFail      0xffff0000
#define bCounter_RateIllegal     0xffff
#define bCounter_CRC8Fail        0xffff0000
#define bCounter_MCSNoSupport    0xffff
#define bCounter_FastSync        0xffff
#define bShortCFO                0xfff
#define bShortCFOTLength         12   /* total */
#define bShortCFOFLength         11   /* fraction */
#define bLongCFO                 0x7ff
#define bLongCFOTLength          11
#define bLongCFOFLength          11
#define bTailCFO                 0x1fff
#define bTailCFOTLength          13
#define bTailCFOFLength          12

#define bmax_en_pwdB             0xffff
#define bCC_power_dB             0xffff0000
#define bnoise_pwdB              0xffff
#define bPowerMeasTLength        10
#define bPowerMeasFLength        3
#define bRx_HT_BW                0x1
#define bRxSC                    0x6
#define bRx_HT                   0x8

#define bNB_intf_det_on          0x1
#define bIntf_win_len_cfg        0x30
#define bNB_Intf_TH_cfg          0x1c0

#define bRFGain                  0x3f
#define bTableSel                0x40
#define bTRSW                    0x80

#define bRxSNR_A                 0xff
#define bRxSNR_B                 0xff00
#define bRxSNR_C                 0xff0000
#define bRxSNR_D                 0xff000000
#define bSNREVMTLength           8
#define bSNREVMFLength           1

#define bCSI1st                  0xff
#define bCSI2nd                  0xff00
#define bRxEVM1st                0xff0000
#define bRxEVM2nd                0xff000000

#define bSIGEVM                  0xff
#define bPWDB                    0xff00
#define bSGIEN                   0x10000

#define bSFactorQAM1             0xf
#define bSFactorQAM2             0xf0
#define bSFactorQAM3             0xf00
#define bSFactorQAM4             0xf000
#define bSFactorQAM5             0xf0000
#define bSFactorQAM6             0xf0000
#define bSFactorQAM7             0xf00000
#define bSFactorQAM8             0xf000000
#define bSFactorQAM9             0xf0000000
#define bCSIScheme               0x100000

#define bNoiseLvlTopSet          0x3
#define bChSmooth                0x4
#define bChSmoothCfg1            0x38
#define bChSmoothCfg2            0x1c0
#define bChSmoothCfg3            0xe00
#define bChSmoothCfg4            0x7000
#define bMRCMode                 0x800000
#define bTHEVMCfg                0x7000000

#define bLoopFitType             0x1
#define bUpdCFO                  0x40
#define bUpdCFOOffData           0x80
#define bAdvUpdCFO               0x100
#define bAdvTimeCtrl             0x800
#define bUpdClko                 0x1000
#define bFC                      0x6000
#define bTrackingMode            0x8000
#define bPhCmpEnable             0x10000
#define bUpdClkoLTF              0x20000
#define bComChCFO                0x40000
#define bCSIEstiMode             0x80000
#define bAdvUpdEqz               0x100000
#define bUChCfg                  0x7000000
#define bUpdEqz                  0x8000000

/* page e */
#define bTxAGCRate18_06			0x7f7f7f7f
#define bTxAGCRate54_24			0x7f7f7f7f
#define bTxAGCRateMCS32		0x7f
#define bTxAGCRateCCK			0x7f00
#define bTxAGCRateMCS3_MCS0	0x7f7f7f7f
#define bTxAGCRateMCS7_MCS4	0x7f7f7f7f
#define bTxAGCRateMCS11_MCS8	0x7f7f7f7f
#define bTxAGCRateMCS15_MCS12	0x7f7f7f7f


/* Rx Pseduo noise */
#define bRxPesudoNoiseOn         0x20000000
#define bRxPesudoNoise_A         0xff
#define bRxPesudoNoise_B         0xff00
#define bRxPesudoNoise_C         0xff0000
#define bRxPesudoNoise_D         0xff000000
#define bPesudoNoiseState_A      0xffff
#define bPesudoNoiseState_B      0xffff0000
#define bPesudoNoiseState_C      0xffff
#define bPesudoNoiseState_D      0xffff0000

/* RF
 * Zebra1
 */
#define bZebra1_HSSIEnable        0x8
#define bZebra1_TRxControl        0xc00
#define bZebra1_TRxGainSetting    0x07f
#define bZebra1_RxCorner          0xc00
#define bZebra1_TxChargePump      0x38
#define bZebra1_RxChargePump      0x7
#define bZebra1_ChannelNum        0xf80
#define bZebra1_TxLPFBW           0x400
#define bZebra1_RxLPFBW           0x600

/* Zebra4 */
#define bRTL8256RegModeCtrl1      0x100
#define bRTL8256RegModeCtrl0      0x40
#define bRTL8256_TxLPFBW          0x18
#define bRTL8256_RxLPFBW          0x600

/* RTL8258 */
#define bRTL8258_TxLPFBW          0xc
#define bRTL8258_RxLPFBW          0xc00
#define bRTL8258_RSSILPFBW        0xc0

/* byte endable for sb_write */
#define bByte0                    0x1
#define bByte1                    0x2
#define bByte2                    0x4
#define bByte3                    0x8
#define bWord0                    0x3
#define bWord1                    0xc
#define bDWord                    0xf

/* for PutRegsetting & GetRegSetting BitMask */
#define bMaskByte0                0xff
#define bMaskByte1                0xff00
#define bMaskByte2                0xff0000
#define bMaskByte3                0xff000000
#define bMaskHWord                0xffff0000
#define bMaskLWord                0x0000ffff
#define bMaskDWord                0xffffffff

/* for PutRFRegsetting & GetRFRegSetting BitMask */
#define bMask12Bits               0xfff

#define bEnable                   0x1
#define bDisable                  0x0

#define LeftAntenna               0x0
#define RightAntenna              0x1

#define tCheckTxStatus            500   /* 500ms */
#define tUpdateRxCounter          100   /* 100ms */

#define rateCCK     0
#define rateOFDM    1
#define rateHT      2

/* define Register-End */
#define bPMAC_End                 0x1ff
#define bFPGAPHY0_End             0x8ff
#define bFPGAPHY1_End             0x9ff
#define bCCKPHY0_End              0xaff
#define bOFDMPHY0_End             0xcff
#define bOFDMPHY1_End             0xdff

/* define max debug item in each debug page
 * #define bMaxItem_FPGA_PHY0        0x9
 * #define bMaxItem_FPGA_PHY1        0x3
 * #define bMaxItem_PHY_11B          0x16
 * #define bMaxItem_OFDM_PHY0        0x29
 * #define bMaxItem_OFDM_PHY1        0x0
 */

#define bPMACControl              0x0
#define bWMACControl              0x1
#define bWNICControl              0x2

#define PathA                     0x0
#define PathB                     0x1
#define PathC                     0x2
#define PathD                     0x3

#define	rRTL8256RxMixerPole		0xb
#define		bZebraRxMixerPole		0x6
#define		rRTL8256TxBBOPBias        0x9
#define		bRTL8256TxBBOPBias       0x400
#define		rRTL8256TxBBBW             19
#define		bRTL8256TxBBBW			0x18

#endif	/* __INC_HAL8190PCIPHYREG_H */
