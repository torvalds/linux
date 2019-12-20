// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
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
 *      04-05-2004 Jerry Chen: Initial release
 *      11-24-2004 Warren Hsu: Add ControlvWriteByte,ControlvReadByte,
 *                             ControlvMaskByte
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
	int ret = 0;
	u8 *usb_buffer;

	if (test_bit(DEVICE_FLAGS_DISCONNECTED, &priv->flags)) {
		ret = -EINVAL;
		goto end;
	}

	mutex_lock(&priv->usb_lock);

	usb_buffer = kmemdup(buffer, length, GFP_KERNEL);
	if (!usb_buffer) {
		ret = -ENOMEM;
		goto end_unlock;
	}

	ret = usb_control_msg(priv->usb,
			      usb_sndctrlpipe(priv->usb, 0),
			      request, 0x40, value,
			      index, usb_buffer, length, USB_CTL_WAIT);

	kfree(usb_buffer);

	if (ret == (int)length)
		ret = 0;
	else
		ret = -EIO;

end_unlock:
	mutex_unlock(&priv->usb_lock);
end:
	return ret;
}

int vnt_control_out_u8(struct vnt_private *priv, u8 reg, u8 reg_off, u8 data)
{
	return vnt_control_out(priv, MESSAGE_TYPE_WRITE,
			       reg_off, reg, sizeof(u8), &data);
}

int vnt_control_out_blocks(struct vnt_private *priv,
			   u16 block, u8 reg, u16 length, u8 *data)
{
	int ret = 0, i;

	for (i = 0; i < length; i += block) {
		u16 len = min_t(int, length - i, block);

		ret = vnt_control_out(priv, MESSAGE_TYPE_WRITE,
				      i, reg, len, data + i);
		if (ret)
			goto end;
	}
end:
	return ret;
}

int vnt_control_in(struct vnt_private *priv, u8 request, u16 value,
		   u16 index, u16 length, u8 *buffer)
{
	int ret = 0;
	u8 *usb_buffer;

	if (test_bit(DEVICE_FLAGS_DISCONNECTED, &priv->flags)) {
		ret = -EINVAL;
		goto end;
	}

	mutex_lock(&priv->usb_lock);

	usb_buffer = kmalloc(length, GFP_KERNEL);
	if (!usb_buffer) {
		ret = -ENOMEM;
		goto end_unlock;
	}

	ret = usb_control_msg(priv->usb,
			      usb_rcvctrlpipe(priv->usb, 0),
			      request, 0xc0, value,
			      index, usb_buffer, length, USB_CTL_WAIT);

	if (ret == length)
		memcpy(buffer, usb_buffer, length);

	kfree(usb_buffer);

	if (ret == (int)length)
		ret = 0;
	else
		ret = -EIO;

end_unlock:
	mutex_unlock(&priv->usb_lock);
end:
	return ret;
}

int vnt_control_in_u8(struct vnt_private *priv, u8 reg, u8 reg_off, u8 *data)
{
	return vnt_control_in(priv, MESSAGE_TYPE_READ,
			      reg_off, reg, sizeof(u8), data);
}

static void vnt_start_interrupt_urb_complete(struct urb *urb)
{
	struct vnt_private *priv = urb->context;
	int status = urb->status;

	switch (status) {
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

	if (status) {
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
	int ret = 0;

	if (priv->int_buf.in_use) {
		ret = -EBUSY;
		goto err;
	}

	priv->int_buf.in_use = true;

	usb_fill_int_urb(priv->interrupt_urb,
			 priv->usb,
			 usb_rcvintpipe(priv->usb, 1),
			 priv->int_buf.data_buf,
			 MAX_INTERRUPT_SIZE,
			 vnt_start_interrupt_urb_complete,
			 priv,
			 priv->int_interval);

	ret = usb_submit_urb(priv->interrupt_urb, GFP_ATOMIC);
	if (ret) {
		dev_dbg(&priv->usb->dev, "Submit int URB failed %d\n", ret);
		goto err_submit;
	}

	return 0;

err_submit:
	priv->int_buf.in_use = false;
err:
	return ret;
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
	int ret = 0;
	struct urb *urb = rcb->urb;

	if (!rcb->skb) {
		dev_dbg(&priv->usb->dev, "rcb->skb is null\n");
		ret = -EINVAL;
		goto end;
	}

	usb_fill_bulk_urb(urb,
			  priv->usb,
			  usb_rcvbulkpipe(priv->usb, 2),
			  skb_put(rcb->skb, skb_tailroom(rcb->skb)),
			  MAX_TOTAL_SIZE_WITH_ALL_HEADERS,
			  vnt_submit_rx_urb_complete,
			  rcb);

	ret = usb_submit_urb(urb, GFP_ATOMIC);
	if (ret) {
		dev_dbg(&priv->usb->dev, "Submit Rx URB failed %d\n", ret);
		goto end;
	}

	rcb->in_use = true;

end:
	return ret;
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
	struct urb *urb = context->urb;

	if (test_bit(DEVICE_FLAGS_DISCONNECTED, &priv->flags)) {
		context->in_use = false;
		return STATUS_RESOURCES;
	}

	usb_fill_bulk_urb(urb,
			  priv->usb,
			  usb_sndbulkpipe(priv->usb, 3),
			  context->data,
			  context->buf_len,
			  vnt_tx_context_complete,
			  context);

	status = usb_submit_urb(urb, GFP_ATOMIC);
	if (status) {
		dev_dbg(&priv->usb->dev, "Submit Tx URB failed %d\n", status);

		context->in_use = false;
		return STATUS_FAILURE;
	}

	return STATUS_PENDING;
}
