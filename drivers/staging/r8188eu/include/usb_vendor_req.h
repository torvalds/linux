/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#ifndef _USB_VENDOR_REQUEST_H_
#define _USB_VENDOR_REQUEST_H_

/* 4	Set/Get Register related wIndex/Data */
#define	RT_USB_RESET_MASK_OFF		0
#define	RT_USB_RESET_MASK_ON		1
#define	RT_USB_SLEEP_MASK_OFF		0
#define	RT_USB_SLEEP_MASK_ON		1
#define	RT_USB_LDO_ON				1
#define	RT_USB_LDO_OFF				0

/* 4	Set/Get SYSCLK related	wValue or Data */
#define	RT_USB_SYSCLK_32KHZ		0
#define	RT_USB_SYSCLK_40MHZ		1
#define	RT_USB_SYSCLK_60MHZ		2

enum bt_usb_request {
	RT_USB_SET_REGISTER		= 1,
	RT_USB_SET_SYSCLK		= 2,
	RT_USB_GET_SYSCLK		= 3,
	RT_USB_GET_REGISTER		= 4
};

enum rt_usb_wvalue {
	RT_USB_RESET_MASK	=	1,
	RT_USB_SLEEP_MASK	=	2,
	RT_USB_USB_HRCPWM	=	3,
	RT_USB_LDO			=	4,
	RT_USB_BOOT_TYPE	=	5
};

#endif
