/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 *
 * File: usbpipe.h
 *
 * Purpose:
 *
 * Author: Warren Hsu
 *
 * Date: Mar. 30, 2005
 *
 */

#ifndef __USBPIPE_H__
#define __USBPIPE_H__

#include "device.h"

int PIPEnsControlOut(struct vnt_private *, u8 byRequest, u16 wValue,
		u16 wIndex, u16 wLength, u8 *pbyBuffer);
int PIPEnsControlOutAsyn(struct vnt_private *, u8 byRequest,
	u16 wValue, u16 wIndex, u16 wLength, u8 *pbyBuffer);
int PIPEnsControlIn(struct vnt_private *, u8 byRequest, u16 wValue,
	u16 wIndex, u16 wLength,  u8 *pbyBuffer);

int PIPEnsInterruptRead(struct vnt_private *);
int PIPEnsBulkInUsbRead(struct vnt_private *, struct vnt_rcb *pRCB);
int PIPEnsSendBulkOut(struct vnt_private *,
				struct vnt_usb_send_context *pContext);

#endif /* __USBPIPE_H__ */
