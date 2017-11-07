// SPDX-License-Identifier: GPL-2.0+
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

int vnt_control_out(struct vnt_private *priv, u8 request, u16 value,
		    u16 index, u16 length, u8 *buffer);
int vnt_control_in(struct vnt_private *priv, u8 request, u16 value,
		   u16 index, u16 length,  u8 *buffer);

void vnt_control_out_u8(struct vnt_private *priv, u8 reg, u8 ref_off, u8 data);
void vnt_control_in_u8(struct vnt_private *priv, u8 reg, u8 reg_off, u8 *data);

int vnt_start_interrupt_urb(struct vnt_private *priv);
int vnt_submit_rx_urb(struct vnt_private *priv, struct vnt_rcb *rcb);
int vnt_tx_context(struct vnt_private *priv,
		   struct vnt_usb_send_context *context);

#endif /* __USBPIPE_H__ */
