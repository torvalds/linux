/*
 * drivers/usb/gadget/sw_usb_platform.h
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
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

/*
*************************************************************************************
*                         			    Linux
*					           USB Device Controller Driver
*
*				        (c) Copyright 2006-2010, All winners Co,Ld.
*							       All Rights Reserved
*
* File Name 	: sw_usb_platform.h
*
* Author 		: javen
*
* Description 	: USB 产品信息
*
* Notes         :
*
* History 		:
*      <author>    		<time>       	<version >    		<desc>
*       javen     	  2011-3-13            1.0          create this file
*
*************************************************************************************
*/
#ifndef  __SW_USB_PLATFORM_H__
#define  __SW_USB_PLATFORM_H__

//---------------------------------------------------------------
//  USB接口
//---------------------------------------------------------------
/* 厂商ID, 产品ID, 产品版本号 */
#if 0
#define  SW_USB_VENDOR_ID               0x1F3A

#define  SW_USB_UMS_PRODUCT_ID          0x1000  /* USB Mass Storage             */
#define  SW_USB_ADB_PRODUCT_ID          0x1001  /* USB Android Debug Bridge     */
#define  SW_USB_ACM_PRODUCT_ID          0x1002  /* USB Abstract Control Model   */
#define  SW_USB_MTP_PRODUCT_ID          0x1003  /* USB Media Transfer Protocol  */
#define  SW_USB_RNDIS_PRODUCT_ID        0x1004  /* USB RNDIS ethernet           */
#define  SW_USB_VERSION                 100
#else
#define  SW_USB_VENDOR_ID               0x18D1

#define  SW_USB_UMS_PRODUCT_ID          0x0001  /* USB Mass Storage             */
#define  SW_USB_ADB_PRODUCT_ID          0x0002  /* USB Android Debug Bridge     */
#define  SW_USB_ACM_PRODUCT_ID          0x0003  /* USB Abstract Control Model   */
#define  SW_USB_MTP_PRODUCT_ID          0x0004  /* USB Media Transfer Protocol  */
#define  SW_USB_RNDIS_PRODUCT_ID        0x0005  /* USB RNDIS ethernet           */
#define  SW_USB_VERSION                 100
#endif

//---------------------------------------------------------------
//  Android USB device descriptor
//---------------------------------------------------------------

/* 厂商名, 产品名, 产品序列号 */
#define  SW_USB_MANUFACTURER_NAME           "USB Developer"
#define  SW_USB_PRODUCT_NAME                "Android"
#define  SW_USB_SERIAL_NUMBER               "20080411"

//---------------------------------------------------------------
//  usb_mass_storage
//---------------------------------------------------------------
/* 厂商名, 产品名, 产品发布版本号 */
#define  SW_USB_MASS_STORAGE_VENDOR_NAME    "USB 2.0"
#define  SW_USB_MASS_STORAGE_PRODUCT_NAME   "USB Flash Driver"
#define  SW_USB_MASS_STORAGE_RELEASE        100

/* 逻辑单元个数， 即PC上能够看见的U盘盘符的个数 */
#define  SW_USB_NLUNS               3

//---------------------------------------------------------------
//  USB ethernet
//---------------------------------------------------------------


//---------------------------------------------------------------
//  USB Abstract Control Model
//---------------------------------------------------------------
#define  SW_USB_ACM_NUM_INST        1

#endif   //__SW_USB_PLATFORM_H__

