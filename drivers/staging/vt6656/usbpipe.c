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
 * File: usbpipe.c
 *
 * Purpose: Handle USB control endpoint
 *
 * Author: Warren Hsu
 *
 * Date: Mar. 29, 2005
 *
 * Functions:
 *	vnt_control_out - Write variable length bytes to MEM/BB/MAC/EEPROM
 *	vnt_control_in - Read variable length bytes from MEM/BB/MAC/EEPROM
 *	vnt_control_out_u8 - Write one byte to MEM/BB/MAC/EEPROM
 *	vnt_control_in_u8 - Read one byte from MEM/BB/MAC/EEPROM
 *
 * Revision History:
 *      04-05-2004 Jerry Chen:  Initial release
 *      11-24-2004 Warren Hsu: Add ControlvWriteByte,ControlvReadByte,ControlvMaskByte
 *
 */

#include "int.h"
#include "rxtx.h"
#include "dpc.h"
#include "desc.h"
#include "device.h"
#include "usbpipe.h"

#define USB_CTL_WAIT	500 /* ms */

int vnt_control_out(struct vnt_private *priv, u8 request, u16 value,
		u16 index, u16 length, u8 *buffer)
{
	int status = 0;

	if (test_bit(DEVICE_FLAGS_DISCONNECTED, &priv->flags))
		return STATUS_FAILURE;

	mutex_lock(&priv->usb_lock);

	status = usb_control_msg(priv->usb,
		usb_sndctrlpipe(priv->usb, 0), request, 0x40, value,
			index, buffer, length, USB_CTL_WAIT);

	mutex_unlock(&priv->usb_lock);

	if (status < (int)length)
		return STATUS_FAILURE;

	return STATUS_SUCCESS;
}

void vnt_control_out_u8(struct vnt_private *priv, u8 reg, u8 reg_off, u8 data)
{
	vnt_control_out(priv, MESSAGE_TYPE_WRITE,
					reg_off, reg, sizeof(u8), &data);
}

int vnt_control_in(struct vnt_private *priv, u8 request, u16 value,
		u16 index, u16 length, u8 *buffer)
{
	int status;
	u8 *usb_buffer;

	if (test_bit(DEVICE_FLAGS_DISCONNECTED, &priv->flags))
		return STATUS_FAILURE;

	mutex_lock(&priv->usb_lock);

	usb_buffer = kmalloc(length, GFP_KERNEL);
	if (!usb_buffer) {
		mutex_unlock(&priv->usb_lock);
		return -ENOMEM;
	}

	status = usb_control_msg(priv->usb,
				 usb_rcvctrlpipe(priv->usb, 0),
				 request, 0xc0, value,
				 index, usb_buffer, length, USB_CTL_WAIT);

	if (status == length)
		memcpy(buffer, usb_buffer, length);

	kfree(usb_buffer);

	mutex_unlock(&priv->usb_lock);

	if (status < (int)length)
		return STATUS_FAILURE;

	return STATUS_SUCCESS;
}

void vnt_control_in_u8(struct vnt_private *priv, u8 reg, u8 reg_off, u8 *data)
{
	vnt_control_in(priv, MESSAGE_TYPE_READ,
			reg_off, reg, sizeof(u8), data);
}

static void vnt_start_interrupt_urb_complete(struct urb *urb)
{
	struct vnt_private *priv = urb->context;
	int status;

	switch (urb->status) {
	case 0:
	case -ETIMEDOUT:
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		priv->int_buf.in_use = false;
		return;
	default:
		break;
	}

	status = urb->status;

	if (status != STATUS_SUCCESS) {
		priv->int_buf.in_use = false;

		dev_dbg(&priv->usb->dev, "%s status = %d\n", __func__, status);
	} else {
		vnt_int_process_data(priv);
	}

	status = usb_submit_urb(priv->interrupt_urb, GFP_ATOMIC);
	if (status)
		dev_dbg(&priv->usb->dev, "Submit int URB failed %d\n", status);
	else
		priv->int_buf.in_use = true;
}

int vnt_start_interrupt_urb(struct vnt_private *priv)
{
	int status = STATUS_FAILURE;

	if (priv->int_buf.in_use)
		return STATUS_FAILURE;

	priv->int_buf.in_use = true;

	usb_fill_int_urb(priv->interrupt_urb,
			 priv->usb,
			 usb_rcvintpipe(priv->usb, 1),
			 priv->int_buf.data_buf,
			 MAX_INTERRUPT_SIZE,
			 vnt_start_interrupt_urb_complete,
			 priv,
			 priv->int_interval);

	status = usb_submit_urb(priv->interrupt_urb, GFP_ATOMIC);
	if (status) {
		dev_dbg(&priv->usb->dev, "Submit int URB failed %d\n", status);
		priv->int_buf.in_use = false;
	}

	return status;
}

static void vnt_submit_rx_urb_complete(struct urb *urb)
{
	struct vnt_rcb *rcb = urb->context;
	struct vnt_private *priv = rcb->priv;

	switch (urb->status) {
	case 0:
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		return;
	case -ETIMEDOUT:
	default:
		dev_dbg(&priv->usb->dev, "BULK In failed %d\n", urb->status);
		break;
	}

	if (urb->actual_length) {
		if (vnt_rx_data(priv, rcb, urb->actual_length)) {
			rcb->skb = dev_alloc_skb(priv->rx_buf_sz);
			if (!rcb->skb) {
				dev_dbg(&priv->usb->dev,
					"Failed to re-alloc rx skb\n");

				rcb->in_use = false;
				return;
			}
		} else {
			skb_push(rcb->skb, skb_headroom(rcb->skb));
			skb_trim(rcb->skb, 0);
		}

		urb->transfer_buffer = skb_put(rcb->skb,
						skb_tailroom(rcb->skb));
	}

	if (usb_submit_urb(urb, GFP_ATOMIC)) {
		dev_dbg(&priv->usb->dev, "Failed to re submit rx skb\n");

		rcb->in_use = false;
	}
}

int vnt_submit_rx_urb(struct vnt_private *priv, struct vnt_rcb *rcb)
{
	int status = 0;
	struct urb *urb;

	urb = rcb->urb;
	if (rcb->skb == NULL) {
		dev_dbg(&priv->usb->dev, "rcb->skb is null\n");
		return status;
	}

	usb_fill_bulk_urb(urb,
			  priv->usb,
			  usb_rcvbulkpipe(priv->usb, 2),
			  skb_put(rcb->skb, skb_tailroom(rcb->skb)),
			  MAX_TOTAL_SIZE_WITH_ALL_HEADERS,
			  vnt_submit_rx_urb_complete,
			  rcb);

	status = usb_submit_urb(urb, GFP_ATOMIC);
	if (status != 0) {
		dev_dbg(&priv->usb->dev, "Submit Rx URB failed %d\n", status);
		return STATUS_FAILURE;
	}

	rcb->in_use = true;

	return status;
}

static void vnt_tx_context_complete(struct urb *urb)
{
	struct vnt_usb_send_context *context = urb->context;
	struct vnt_private *priv = context->priv;

	switch (urb->status) {
	case 0:
		dev_dbg(&priv->usb->dev, "Write %d bytes\n", context->buf_len);
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		context->in_use = false;
		return;
	case -ETIMEDOUT:
	default:
		dev_dbg(&priv->usb->dev, "BULK Out failed %d\n", urb->status);
		break;
	}

	if (context->type == CONTEXT_DATA_PACKET)
		ieee80211_wake_queues(priv->hw);

	if (urb->status || context->type == CONTEXT_BEACON_PACKET) {
		if (context->skb)
			ieee80211_free_txskb(priv->hw, context->skb);

		context->in_use = false;
	}
}

int vnt_tx_context(struct vnt_private *priv,
		   struct vnt_usb_send_context *context)
{
	int status;
	struct urb *urb;

	if (test_bit(DEVICE_FLAGS_DISCONNECTED, &priv->flags)) {
		context->in_use = false;
		return STATUS_RESOURCES;
	}

	urb = context->urb;

	usb_fill_bulk_urb(urb,
			  priv->usb,
			  usb_sndbulkpipe(priv->usb, 3),
			  context->data,
			  context->buf_len,
			  vnt_tx_context_complete,
			  context);

	status = usb_submit_urb(urb, GFP_ATOMIC);
	if (status != 0) {
		dev_dbg(&priv->usb->dev, "Submit Tx URB failed %d\n", status);

		context->in_use = false;
		return STATUS_FAILURE;
	}

	return STATUS_PENDING;
}
