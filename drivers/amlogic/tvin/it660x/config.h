///*****************************************
//  Copyright (C) 2009-2014
//  ITE Tech. Inc. All Rights Reserved
//  Proprietary and Confidential
///*****************************************
//   @file   <config.h>
//   @author Jau-Chih.Tseng@ite.com.tw
//   @date   2012/07/24
//   @fileversion: HDMIRX_SAMPLE_2.18
//******************************************/

#ifndef _CONFIG_H_
#define _CONFIG_H_

#define SUPPORT_INPUTRGB
#define SUPPORT_INPUTYUV444
#define SUPPORT_INPUTYUV422

#if defined(SUPPORT_INPUTYUV444)|| defined(SUPPORT_INPUTYUV422)
#define SUPPORT_INPUTYUV
#endif


#define SUPPORT_OUTPUTYUV
#define SUPPORT_OUTPUTYUV444
#define SUPPORT_OUTPUTYUV422
#if (defined(SUPPORT_OUTPUTYUV444))||(defined(SUPPORT_OUTPUTYUV422))
#define SUPPORT_OUTPUTYUV
#endif

#define SUPPORT_OUTPUTRGB

#define LOOP_MSEC 20
// 2010/01/26 added a option to disable HDCP.
// #define _COPY_EDID_
// #define DISABLE_COLOR_DEPTH_RESET

#endif // _CONFIG_H_
