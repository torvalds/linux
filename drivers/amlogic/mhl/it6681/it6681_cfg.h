///*****************************************
//  Copyright (C) 2009-2014
//  ITE Tech. Inc. All Rights Reserved
//  Proprietary and Confidential
///*****************************************
//   @file   <IT6811.h>
//   @author Hermes.Wu@ite.com.tw
//   @date   2013/05/07
//   @fileversion: ITE_IT6811_6607_SAMPLE_1.06
//******************************************/
#ifndef _IT6681_CFG_H_
#define _IT6681_CFG_H_

#define _DEBUG_MHL_1 0

/////////////////////////////////////////
//Cbus command fire wait time
//Maxmun time for determin CBUS fail
//	CBUSWAITTIME(ms) x CBUSWAITNUM
/////////////////////////////////////////
#define CBUSWAITTIME    30
#define CBUSWAITNUM     200

/////////////////////////////////////////
// CBUS discover error Counter
//
// determine Cbus connection by interrupt count
//
////////////////////////////////////////
#define CBUSDETMAX      10
#define DISVFAILMAX     20
#define CBUSFAILMAX     20

//Cbus retry is disable fo SW porting issue.
//when CBUS fail just assume it will always fail.
//#define CBUSRTYMAX      20

//#define RAPBUSYNUM      50
//#define RCPBUSYNUM      50


//initial define options
//////////////////////////////////////////
// CBUS Input Option
//
// CBUS Discovery and Disconnect Option
///
/////////////////////////////////////////

#define _EnCBusReDisv     FALSE

#define _EnCBusU3IDDQ     FALSE

#define _ForceVBUSOut        FALSE	   //Some dongle need to force this

#define _AutoSwBack       TRUE

//////////////////////////////////////////
// MHL POWDN GRCLK
// MHL can power-down GRCLK only when REnCBUS=0
//
/////////////////////////////////////////
#define  _EnGRCLKPD  FALSE

//////////////////////////////////////////
// MSC Option
//
//
///
/////////////////////////////////////////

#define _MaskMSCDoneInt  	FALSE 	// MSC Rpd Done Mask and MSC Req Done Mask
#define _MSCBurstWrID  		FALSE   // TRUE: Adopter ID from MHL5E/MHL5F
//#define _EnPktFIFOBurst  	TRUE 	// TRUE for MSC test
//#define _MSCBurstWrOpt  	FALSE  	// TRUE: Not write Adopter ID into ScratchPad
//#define _EnMSCBurstWr 	TRUE
//#define _EnMSCHwRty  		FALSE
//#define _MSCRxUCP2Nack  	TRUE
#define _EnHWPathEn  		FALSE   // FALSE: FW PATH_EN

//#define _SUPPORT_RAP_ 1
//#define _SUPPORT_RCP_ 1
//#define _SUPPORT_UCP_ 1
//#define _SUPPORT_UCP_MOUSE_ 1


//////////////////////////////////////////
// Define  System EDID option
//
//
/////////////////////////////////////////
// SEGRD: Segment Read, COMBRD: Combine Read
//#define _SEGRD           0
//#define _COMBRD          1

//#define _EnEDIDRead   TRUE
//#define _EnEDIDParse  FALSE
//#define _EDIDRdByte   16
//#define _EDIDRdType   _SEGRD

//////////////////////////////////////////
// MHL Output mode config
//
//
/////////////////////////////////////////
//R/B or Cr/Cb swap after data packing
//#define  _PackSwap 	  FALSE
//Packet pixel mode enable
//#define  _EnPackPix   FALSE
//packet pixel mode band swap
//#define  _EnPPGBSwap  TRUE
//Packet pixel mode HDCP
//#define  _PPHDCPOpt	  TRUE

//////////////////////////////////////////
// video Input Option
//
//
//////////////////////////////////////////


//clip R/G/B/Y to 16~235, Cb/Cr to 16~240
#define _EnColorClip   	TRUE

#define _RegPCLKDiv2   	FALSE

#define _Reg2x656Clk   	FALSE


//#define _LMSwap 		FALSE
//#define _YCSwap 		FALSE
//#define _RBSwap 		FALSE


//////////////////////////////////////////
// HDCP setting
//
//
/////////////////////////////////////////
// HDCP Option
#define _SUPPORT_HDCP_			TRUE
#define _SUPPORT_HDCP_REPEATER_	FALSE
//count about HDCP fail retry
#define _HDCPFireMax  	100
#define _ChkKSVListMax  500
#define _CHECK_REVOCATION_BKSV 0

#define TimeLoMax   0x24A00UL

//////////////////////////////////////////
// Video color space convert
//
//
//////////////////////////////////////////

//#define DISABLE_HDMITX_CSC

#define SUPPORT_INPUTRGB
#define SUPPORT_INPUTYUV444
#define SUPPORT_INPUTYUV422
#if defined(SUPPORT_INPUTYUV422) || defined(SUPPORT_INPUTYUV444)
#define SUPPORT_INPUTYUV
#endif

#define B_HDMITX_CSC_BYPASS    0
#define B_HDMITX_CSC_RGB2YUV   2
#define B_HDMITX_CSC_YUV2RGB   3


#define F_VIDMODE_ITU709  (1<<4)
#define F_VIDMODE_ITU601  0

#define F_VIDMODE_0_255   0
#define F_VIDMODE_16_235  (1<<5)
#endif
