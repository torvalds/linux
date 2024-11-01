/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
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

struct vnt_interrupt_data {
	u8 tsr0;
	u8 pkt0;
	u16 time0;
	u8 tsr1;
	u8 pkt1;
	u16 time1;
	u8 tsr2;
	u8 pkt2;
	u16 time2;
	u8 tsr3;
	u8 pkt3;
	u16 time3;
	__le64 tsf;
	u8 isr0;
	u8 isr1;
	u8 rts_success;
	u8 rts_fail;
	u8 ack_fail;
	u8 fcs_err;
	u8 sw[2];
} __packed;

struct vnt_tx_usb_header {
	u8 type;
	u8 pkt_no;
	__le16 tx_byte_count;
} __packed;

#define VNT_REG_BLOCK_SIZE	64

int vnt_control_out(struct vnt_private *priv, u8 request, u16 value,
		    u16 index, u16 length, const u8 *buffer);
int vnt_control_in(struct vnt_private *priv, u8 request, u16 value,
		   u16 index, u16 length,  u8 *buffer);

int vnt_control_out_u8(struct vnt_private *priv, u8 reg, u8 ref_off, u8 data);
int vnt_control_in_u8(struct vnt_private *priv, u8 reg, u8 reg_off, u8 *data);

int vnt_control_out_blocks(struct vnt_private *priv,
			   u16 block, u8 reg, u16 len, const u8 *data);

int vnt_start_interrupt_urb(struct vnt_private *priv);
int vnt_submit_rx_urb(struct vnt_private *priv, struct vnt_rcb *rcb);
int vnt_tx_context(struct vnt_private *priv,
		   struct vnt_usb_send_context *context,
		   struct sk_buff *skb);

#endif /* __USBPIPE_H__ */
