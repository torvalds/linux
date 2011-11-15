/*
*************************************************************************************
*                         			      Linux
*					           USB Host Controller Driver
*
*				        (c) Copyright 2006-2010, All winners Co,Ld.
*							       All Rights Reserved
*
* File Name 	: sw_hcd_config.h
*
* Author 		: javen
*
* Description 	:
*
* History 		:
*      <author>    		<time>       	<version >    		<desc>
*       javen     	  2010-12-20           1.0          create this file
*
*************************************************************************************
*/
#ifndef  __SW_HCD_CONFIG_H__
#define  __SW_HCD_CONFIG_H__

#include <linux/slab.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/errno.h>

#include  "../../include/sw_usb_config.h"
#include  "sw_hcd_debug.h"

//-------------------------------------------------------
//
//-------------------------------------------------------
//#define        XUSB_DEBUG    /* 调试开关 */

/* xusb hcd 调试打印 */
#if	0
    #define DMSG_DBG_HCD     			DMSG_PRINT
#else
    #define DMSG_DBG_HCD(...)
#endif

#endif   //__SW_HCD_CONFIG_H__

