//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Copyright (c) 1996-2004 Winbond Electronic Corporation
//
//  Module Name:
//    wbusb_s.h
//
//  Abstract:
//    Linux driver.
//
//  Author:
//
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

#ifndef __WINBOND_WBUSB_S_H
#define __WINBOND_WBUSB_S_H

#include <linux/types.h>

struct wb_usb {
	u32	IsUsb20;
	struct usb_device *udev;
	u32	DetectCount;
};

#endif
