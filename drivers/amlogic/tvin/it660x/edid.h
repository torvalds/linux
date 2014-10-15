///*****************************************
//  Copyright (C) 2009-2014
//  ITE Tech. Inc. All Rights Reserved
//  Proprietary and Confidential
///*****************************************
//   @file   <edid.h>
//   @author Jau-Chih.Tseng@ite.com.tw
//   @date   2011/01/13
//   @fileversion: HDMIRX_SAMPLE_2.09
//******************************************/

#ifndef _EDID_h_
#define _EDID_h_
#include "mcu.h"
#include "debug.h"

//#define _COPY_EDID_ 

#ifdef _COPY_EDID_

#define Ext_EDID_Read_Fail	    1
#define Ext_EDID_CheckSum_Fail	2
#define Int_EDID_Write_Fail	    3
#define Int_EDID_Read_Fail	    4
#define Int_EDID_CheckSum_Fail	5
#define EDID_Read_Success   	6
#define EDID_Copy_Success   	7
#define EDID_Write_Finish   	8
// #define _Block_0_Only_


//    #message --Ext_Chip_EDID
//======================================================
#define rol16(x,y) (((x)<<(y%16))|((x)>>(16-(y%16))))
#define ror16(x,y) (((x)>>(y%16))|((x)<<(16-(y%16))))
#define rol8(x,y) (((x)<<(y%8))|((x)>>(8-(y%8))))
#define ror8(x,y) (((x)>>(y%8))|((x)<<(8-(y%8))))

#define EDIDADR			0xA0
#define EXT_EDIDADR     0xA0

void CopyDefaultEDID() ;
#endif

#endif
