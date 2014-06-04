/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 ******************************************************************************/
#ifndef _USB_VENDOR_REQUEST_H_
#define _USB_VENDOR_REQUEST_H_

/* 4	Set/Get Register related wIndex/Data */
#define	RT_USB_RESET_MASK_OFF		0
#define	RT_USB_RESET_MASK_ON		1
#define	RT_USB_SLEEP_MASK_OFF		0
#define	RT_USB_SLEEP_MASK_ON		1
#define	RT_USB_LDO_ON			1
#define	RT_USB_LDO_OFF			0

/* 4	Set/Get SYSCLK related	wValue or Data */
#define	RT_USB_SYSCLK_32KHZ		0
#define	RT_USB_SYSCLK_40MHZ		1
#define	RT_USB_SYSCLK_60MHZ		2

#endif
