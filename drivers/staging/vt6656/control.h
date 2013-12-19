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
 * File: control.h
 *
 * Purpose:
 *
 * Author: Jerry Chen
 *
 * Date: Apr. 5, 2004
 *
 */

#ifndef __CONTROL_H__
#define __CONTROL_H__

#include "device.h"
#include "usbpipe.h"

#define CONTROLnsRequestOut(Device, Request, Value, Index, Length, Buffer) \
	PIPEnsControlOut(Device, Request, Value, Index, Length, Buffer)

#define CONTROLnsRequestOutAsyn(Device, Request, Value, Index, Length, Buffer) \
	PIPEnsControlOutAsyn(Device, Request, Value, Index, Length, Buffer)

#define CONTROLnsRequestIn(Device, Request, Value, Index, Length, Buffer) \
	PIPEnsControlIn(Device, Request, Value, Index, Length, Buffer)

void ControlvWriteByte(struct vnt_private *pDevice, u8 reg, u8 reg_off,
			u8 data);

void ControlvReadByte(struct vnt_private *pDevice, u8 reg, u8 reg_off,
			u8 *data);

void ControlvMaskByte(struct vnt_private *pDevice, u8 reg_type, u8 reg_off,
			u8 reg_mask, u8 data);

#endif /* __CONTROL_H__ */
