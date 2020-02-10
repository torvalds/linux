/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
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

#define VNT_REG_BLOCK_SIZE	64

int vnt_control_out(struct vnt_private *priv, u8 request, u16 value,
		    u16 index, u16 length, const u8 *buffer);
int vnt_control_in(struct vnt_private *priv, u8 request, u16 value,
		   u16 index, u16 length,  u8 *buffer);

int vnt_control_out_u8(struct vnt_private *priv, u8 reg, u8 ref_off, u8 data);
int vnt_control_in_u8(struct vnt_private *priv, u8 reg, u8 reg_off, u8 *data);

int vnt_control_out_blocks(struct vnt_private *priv,
			   u16 block, u8 reg, u16 len, u8 *data);

int vnt_start_interrupt_urb(struct vnt_private *priv);
int vnt_submit_rx_urb(struct vnt_private *priv, struct vnt_rcb *rcb);
int vnt_tx_context(struct vnt_private *priv,
		   struct vnt_usb_send_context *context);

#endif /* __USBPIPE_H__ */
