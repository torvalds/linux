/*
 * drivers/usb/sunxi_usb/udc/sw_udc_config.h
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * javen <javen@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
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
    #define DMSG_DBG_UDC     			DMSG_MSG
#else
    #define DMSG_DBG_UDC(...)
#endif


#endif   //__SW_UDC_CONFIG_H__

