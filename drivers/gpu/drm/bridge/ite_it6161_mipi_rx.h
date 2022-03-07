// SPDX-License-Identifier: GPL-2.0
///*****************************************
//  Copyright (C) 2009-2019
//  ITE Tech. Inc. All Rights Reserved
///*****************************************
//   @file   <mipi_rx.h>
//   @author Pet.Weng@ite.com.tw
//   @date   2019/02/15
//   @fileversion: IT6161_SAMPLE_0.50
//******************************************/

#ifndef _MIPIRX_H_
#define _MIPIRX_H_

#define MIPIRX_Debug_message 1
#define DEBUG_MIPIRX
#ifdef	DEBUG_MIPIRX
#define	MIPIRX_DEBUG_PRINT(x) printf x
#else
#define	MIPIRX_DEBUG_PRINT(x)
#endif

#define ENABLE_MIPI_SOURCE		(0)
//////////////////////////////////////////////////////////////////////
//reference: MIPI Alliance Specification for DSI Ch8.7 Table 16 Data Types for Processor-sourced Packets
#define RGB_24b         0x3E
#define RGB_30b         0x0D
#define RGB_36b         0x1D
#define RGB_18b         0x1E
#define RGB_18b_L       0x2E
#define YCbCr_16b       0x2C
#define YCbCr_20b       0x0C
#define YCbCr_24b       0x1C




#define FrmPkt          0
#define SbSFull         3
#define TopBtm          6
#define SbSHalf         8

#define DDC75K          0
#define DDC125K         1
#define DDC312K         2


#define PICAR_NO        0
#define PICAR4_3        1
#define PICAR16_9       2

#define ACTAR_PIC       8
#define ACTAR4_3        9
#define ACTAR16_9       10
#define ACTAR14_9       11


// int LaneNo, EnLBR, VidFmt, TstPat, LaneNoOri, EnLBROri;

// Debug Option
//int EnDebug = FALSE; // (default = FALSE)
//int REG_RXTXDebug = 1; // 1:TX 0:RX
//tx
//int TxDbgSel = 0x00; //0x10:test
//int REGDBGMUX = 0;
//int EnDbgTxOut = 0;
//rx
// int RxDbgSel = 3;//0x11:PPS
// int PPSDbgSel = 0;
#define LMDbgSel 0 //int LMDbgSel= 0;   //0~7


///////////////////////////MPRX/////////////////////
// for PatGen
#define EnRxPatGen FALSE// int EnRxPatGen = FALSE;   // TRUE : HSync , VSync , DE  from patgen, progressive mode only (default = FALSE)



// MP PtGen option
#define MPVidType RGB_24b//RGB_24b//RGB_24b//int MPVidType  = RGB_18b_L; // RGB_24b, RGB_18b, RGB_18b_L

//system control
// #define MPPHYPCLKInv FALSE//int MPPHYPCLKInv = FALSE; // for FPGA only, >200MHz set TRUE

#if (IC_VERSION == 0xC0)
// #define MPPHYMCLKInv FALSE//int MPPHYMCLKInv = TRUE;  //default:TRUE
#define InvMCLK TRUE //int InvMCLK = FALSE; //FALSE for solomon, if NonUFO, MCLK max = 140MHz with InvMCLK=TRUE
#else
// #define MPPHYMCLKInv TRUE//int MPPHYMCLKInv = TRUE;  //default:TRUE
#define InvMCLK FALSE //int InvMCLK = FALSE; //FALSE for solomon, if NonUFO, MCLK max = 140MHz with InvMCLK=TRUE
#endif //#if (IC_VERSION == 0xC0)

// #define MPPHYMCLKOInv FALSE//int MPPHYMCLKOInv = FALSE;

#define InvPCLK FALSE//int InvPCLK = FALSE;
//#define MPLaneNum (MIPIRX_LANE_NUM - 1)//3//int MPLaneNum = 3;   // 0: 1-lane, 1: 2-lane, 2: 3-lane, 3: 4-lane
// int MPPCLKSel = AUTO;
// #define EnMPx1PCLK FALSE//int EnMPx1PCLK = FALSE;//FALSE;  // FALSE: 3/4(for 4 Lane) , 3/2(for 2 Lane) , 3(for 1 Lane) PCLK
                         // TRUE : 1  (for 4 Lane) , 2  (for 2 Lane) , 4(for 1 Lane) PCLK
//system misc control
#if (IC_VERSION == 0xC0)
#define PDREFCLK FALSE//int PDREFCLK = TRUE;
#else
#define PDREFCLK TRUE//int PDREFCLK = TRUE;
#endif //#if (IC_VERSION == C0)

#define PDREFCNT 0//int PDREFCNT = 0;   //when PDREFCLK=TRUE, 0:div2, 1:div4, 2:div8, 3:divg16
#define EnIntWakeU3 FALSE//int EnIntWakeU3 = FALSE;
#define EnIOIDDQ FALSE//int EnIOIDDQ = FALSE;
#define EnStb2Rst FALSE//int EnStb2Rst = FALSE;
#define EnExtStdby FALSE//int EnExtStdby = FALSE;
#define EnStandby FALSE//int EnStandby = FALSE;
#define MPLaneSwap FALSE//int MPLaneSwap = FALSE;
#define MPPNSwap FALSE//int MPPNSwap = FALSE; //TRUE: MTK , FALSE: Solomon

// int ForceTxCLKStb = FALSE;  //TRUE:define _hdmitx_jitter_



//int DisMHSyncErr = FALSE;
#define DisPHSyncErr FALSE//int DisPHSyncErr = FALSE;
#define DisECCErr FALSE//int DisECCErr = FALSE;

// PPI option
#define EnContCK TRUE//int EnContCK = TRUE;
#define HSSetNum 3//int HSSetNum = 3;
#if (IC_VERSION == 0xC0)
#define SkipStg 4//int SkipStg = 2;
#else
#define SkipStg 2//int SkipStg = 2;
#endif //#if (IC_VERSION == 0xC0)
#define EnDeSkew TRUE//int EnDeSkew = TRUE;
#define PPIDbgSel 12//0//int PPIDbgSel= 0;   //0~15
#define RegIgnrNull 1//int RegIgnrNull = 1;
#define RegIgnrBlk 1//int RegIgnrBlk = 1;
#define RegEnDummyECC 0//int RegEnDummyECC = 0;

// LM option
#define EOTPSel 0//int EOTPSel = 0;   //0~15

// PPS option
#define EnMBPM FALSE//FALSE//int EnMBPM = FALSE;	// enable MIPI Bypass Mode
#if (EnMBPM == TRUE)
#define PREC_Update TRUE//int PREC_Update = FALSE;	// enable P-timing update
#define MREC_Update TRUE//int MREC_Update = FALSE;	// enable M-timing update
#define EnTBPM TRUE//int EnTBPM = FALSE;	// enable HDMITX Bypass Mode
#else
#define PREC_Update FALSE//int PREC_Update = FALSE;	// enable P-timing update
#define MREC_Update FALSE//int MREC_Update = FALSE;	// enable M-timing update
#define EnTBPM FALSE//int EnTBPM = FALSE;	// enable HDMITX Bypass Mode
#endif //#if (EnMBPM == TRUE)

#define REGSELDEF FALSE//int REGSELDEF   = FALSE;
#define EnMPExtPCLK FALSE//int EnMPExtPCLK = FALSE;
#define MPForceStb FALSE//int MPForceStb = FALSE;
#define EnHReSync FALSE//int EnHReSync = FALSE ; // default FALSE
#define EnVReSync FALSE//int EnVReSync = FALSE;  // default TRUE
#define EnFReSync FALSE//int EnFReSync = FALSE;
#define EnVREnh FALSE//int EnVREnh = FALSE;  // default TRUE
#define EnVREnhSel 1//int EnVREnhSel = 1;  // 0:Div2, 1:Div4, 2:Div8, 3:Div16, 4:Div32
#define EnMAvg TRUE//int EnMAvg = TRUE;
#if (IC_VERSION == 0xC0)
#define MShift 4//int MShift = 5; // default: 0 //fmt2 fmt4 :4
#define PPSFFRdStg 0x04//int PPSFFRdStg = 0x10; //PPSFFRdStg(2:0)
#define RegAutoSync TRUE//int RegAutoSync = TRUE;//add sync falling //pet:D0 20200211
#else
#define MShift 5//int MShift = 5; // default: 0 //fmt2 fmt4 :4
#define PPSFFRdStg 0x10//int PPSFFRdStg = 0x10; //PPSFFRdStg(2:0)
#endif //#if (IC_VERSION == 0xC0)
#define PShift 3//int PShift = 3; // default: 3
#define EnFFAutoRst TRUE//int EnFFAutoRst = TRUE;

#define RegEnSyncErr FALSE//int RegEnSyncErr = FALSE;
#define EnTxCRC TRUE//int EnTxCRC = TRUE;
#define TxCRCnum (0x20) //D0 20200211//(0x00)//TxCRCnum(6:0)//int TxCRCnum = 0x00; //TxCRCnum(6:0)



//////////////////////////////////////////////////////////////////////
void DumpMIPIRXReg(void);
void MIPIRX_DevLoopProc(void);
void set_ppara(void);
#endif // _MIPIRX_H_
