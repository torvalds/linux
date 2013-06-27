/*
 * drivers/usb/sunxi_usb/hcd/include/sw_hcd_config.h
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

