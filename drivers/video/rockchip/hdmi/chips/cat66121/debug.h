///*****************************************
//  Copyright (C) 2009-2014
//  ITE Tech. Inc. All Rights Reserved
//  Proprietary and Confidential
///*****************************************
//   @file   <debug.h>
//   @author Jau-Chih.Tseng@ite.com.tw
//   @date   2012/12/20
//   @fileversion: ITE_HDMITX_SAMPLE_3.14
//******************************************/
#ifndef _DEBUG_H_
#define _DEBUG_H_

#ifdef CONFIG_RK_HDMI_DEBUG
#define Debug_message 1
#else
#define Debug_message 0
#endif

//#pragma message("debug.h")

#if Debug_message

    #define HDMITX_DEBUG_PRINTF(x)  printk x
    #define HDCP_DEBUG_PRINTF(x)  printk x
    #define EDID_DEBUG_PRINTF(x)  printk x
    #define HDMITX_DEBUG_INFO(x) printk x
#else
    #define HDMITX_DEBUG_PRINTF(x)
    #define HDCP_DEBUG_PRINTF(x)
    #define EDID_DEBUG_PRINTF(x)
    #define HDMITX_DEBUG_INFO(x)
#endif


#if( Debug_message & (1<<1))
    #define HDMITX_DEBUG_PRINTF1(x) printk x
    #define HDCP_DEBUG_PRINTF1(x) printk x
    #define EDID_DEBUG_PRINTF1(x) printk x
#else
    #define HDMITX_DEBUG_PRINTF1(x)
    #define HDCP_DEBUG_PRINTF1(x)
    #define EDID_DEBUG_PRINTF1(x)
#endif

#if( Debug_message & (1<<2))
    #define HDMITX_DEBUG_PRINTF2(x) printk x
    #define HDCP_DEBUG_PRINTF2(x) printk x
    #define EDID_DEBUG_PRINTF2(x) printk x
#else
    #define HDMITX_DEBUG_PRINTF2(x)
    #define HDCP_DEBUG_PRINTF2(x)
    #define EDID_DEBUG_PRINTF2(x)
#endif

#if( Debug_message & (1<<3))
    #define HDMITX_DEBUG_PRINTF3(x) printk x
    #define HDCP_DEBUG_PRINTF3(x) printk x
    #define EDID_DEBUG_PRINTF3(x) printk x
#else
    #define HDMITX_DEBUG_PRINTF3(x)
    #define HDCP_DEBUG_PRINTF3(x)
    #define EDID_DEBUG_PRINTF3(x)
#endif

#if( Debug_message & (1<<4))
    #define HDMITX_DEBUG_PRINTF4(x) printk x
    #define HDCP_DEBUG_PRINTF4(x) printk x
    #define EDID_DEBUG_PRINTF4(x) printk x
#else
    #define HDMITX_DEBUG_PRINTF4(x)
    #define HDCP_DEBUG_PRINTF4(x)
    #define EDID_DEBUG_PRINTF4(x)
#endif

#if( Debug_message & (1<<5))
    #define HDMITX_DEBUG_PRINTF5(x) printk x
    #define HDCP_DEBUG_PRINTF5(x) printk x
    #define EDID_DEBUG_PRINTF5(x) printk x
#else
    #define HDMITX_DEBUG_PRINTF5(x)
    #define HDCP_DEBUG_PRINTF5(x)
    #define EDID_DEBUG_PRINTF5(x)
#endif

#if( Debug_message & (1<<6))
    #define HDMITX_DEBUG_PRINTF6(x) printk x
    #define HDCP_DEBUG_PRINTF6(x) printk x
    #define EDID_DEBUG_PRINTF6(x) printk x
#else
    #define HDMITX_DEBUG_PRINTF6(x)
    #define HDCP_DEBUG_PRINTF6(x)
    #define EDID_DEBUG_PRINTF6(x)
#endif

#if( Debug_message & (1<<7))
    #define HDMITX_DEBUG_PRINTF7(x) printk x
    #define HDCP_DEBUG_PRINTF7(x) printk x
    #define EDID_DEBUG_PRINTF7(x) printk x
#else
    #define HDMITX_DEBUG_PRINTF7(x)
    #define HDCP_DEBUG_PRINTF7(x)
    #define EDID_DEBUG_PRINTF7(x)
#endif


#endif//  _DEBUG_H_
