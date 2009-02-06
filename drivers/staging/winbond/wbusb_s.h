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

//---------------------------------------------------------------------------
//  RW_CONTEXT --
//
//  Used to track driver-generated io irps
//---------------------------------------------------------------------------
typedef struct _RW_CONTEXT
{
	void*			pHwData;
	struct urb		*urb;
	void*			pCallBackFunctionParameter;
} RW_CONTEXT, *PRW_CONTEXT;

typedef struct _WBUSB {
	u32	IsUsb20;
	struct usb_device *udev;
	u32	DetectCount;
} WBUSB, *PWBUSB;

#endif
