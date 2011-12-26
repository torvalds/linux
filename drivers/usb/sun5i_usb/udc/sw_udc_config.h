/*
*************************************************************************************
*                         			      Linux
*					           USB Device Controller Driver
*
*				        (c) Copyright 2006-2010, All winners Co,Ld.
*							       All Rights Reserved
*
* File Name 	: sw_udc_config.h
*
* Author 		: javen
*
* Description 	:
*
* History 		:
*      <author>    		<time>       	<version >    		<desc>
*       javen     	   2010-3-3            1.0          create this file
*
*************************************************************************************
*/
#ifndef  __SW_UDC_CONFIG_H__
#define  __SW_UDC_CONFIG_H__


#include <linux/slab.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/usb/ch9.h>

#include  "../include/sw_usb_config.h"

#define  SW_UDC_DOUBLE_FIFO       /* 双 FIFO          */
//#define  SW_UDC_DMA               /* DMA 传输         */
#define  SW_UDC_HS_TO_FS          /* 支持高速跳转全速 */
//#define  SW_UDC_DEBUG

//---------------------------------------------------------------
//  调试
//---------------------------------------------------------------

/* sw udc 调试打印 */
#if	0
    #define DMSG_DBG_UDC     		DMSG_MSG
#else
    #define DMSG_DBG_UDC(...)
#endif


#endif   //__SW_UDC_CONFIG_H__

