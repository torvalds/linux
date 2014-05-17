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
 *      CONTROLnsRequestOut - Write variable length bytes to MEM/BB/MAC/EEPROM
 *      CONTROLnsRequestIn - Read variable length bytes from MEM/BB/MAC/EEPROM
 *      ControlvWriteByte - Write one byte to MEM/BB/MAC/EEPROM
 *      ControlvReadByte - Read one byte from MEM/BB/MAC/EEPROM
 *      ControlvMaskByte - Read one byte from MEM/BB/MAC/EEPROM and clear/set some bits in the same address
 *
 * Revision History:
 *      04-05-2004 Jerry Chen:  Initial release
 *      11-24-2004 Warren Hsu: Add ControlvWriteByte,ControlvReadByte,ControlvMaskByte
 *
 */

#include "int.h"
#include "rxtx.h"
#include "dpc.h"
#include "control.h"
#include "desc.h"
#include "device.h"

//endpoint def
//endpoint 0: control
//endpoint 1: interrupt
//endpoint 2: read bulk
//endpoint 3: write bulk

//static int          msglevel                =MSG_LEVEL_DEBUG;
static int          msglevel                =MSG_LEVEL_INFO;

#define USB_CTL_WAIT   500 //ms

#ifndef URB_ASYNC_UNLINK
#define URB_ASYNC_UNLINK    0
#endif

static void s_nsInterruptUsbIoCompleteRead(struct urb *urb);
static void s_nsBulkInUsbIoCompleteRead(struct urb *urb);
static void s_nsBulkOutIoCompleteWrite(struct urb *urb);

int PIPEnsControlOut(struct vnt_private *pDevice, u8 byRequest, u16 wValue,
		u16 wIndex, u16 wLength, u8 *pbyBuffer)
{
	int ntStatus = 0;

	if (pDevice->Flags & fMP_DISCONNECTED)
		return STATUS_FAILURE;

	mutex_lock(&pDevice->usb_lock);

	ntStatus = usb_control_msg(pDevice->usb,
		usb_sndctrlpipe(pDevice->usb, 0), byRequest, 0x40, wValue,
			wIndex, pbyBuffer, wLength, USB_CTL_WAIT);

	mutex_unlock(&pDevice->usb_lock);

	if (ntStatus < (int)wLength)
		return STATUS_FAILURE;

	return STATUS_SUCCESS;
}

int PIPEnsControlIn(struct vnt_private *pDevice, u8 byRequest, u16 wValue,
	u16 wIndex, u16 wLength, u8 *pbyBuffer)
{
	int ntStatus;

	if (pDevice->Flags & fMP_DISCONNECTED)
		return STATUS_FAILURE;

	mutex_lock(&pDevice->usb_lock);

	ntStatus = usb_control_msg(pDevice->usb,
		usb_rcvctrlpipe(pDevice->usb, 0), byRequest, 0xc0, wValue,
			wIndex, pbyBuffer, wLength, USB_CTL_WAIT);

	mutex_unlock(&pDevice->usb_lock);

	if (ntStatus < (int)wLength)
		return STATUS_FAILURE;

	return STATUS_SUCCESS;
}

/*
 * Description:
 *      Allocates an usb interrupt in irp and calls USBD.
 *
 * Parameters:
 *  In:
 *      pDevice     - Pointer to the adapter
 *  Out:
 *      none
 *
 * Return Value: STATUS_INSUFFICIENT_RESOURCES or result of IoCallDriver
 *
 */

int PIPEnsInterruptRead(struct vnt_private *priv)
{
	int status = STATUS_FAILURE;

	if (priv->int_buf.in_use == true)
		return STATUS_FAILURE;

	priv->int_buf.in_use = true;

	usb_fill_int_urb(priv->pInterruptURB,
		priv->usb,
		usb_rcvintpipe(priv->usb, 1),
		priv->int_buf.data_buf,
		MAX_INTERRUPT_SIZE,
		s_nsInterruptUsbIoCompleteRead,
		priv,
		priv->int_interval);

	status = usb_submit_urb(priv->pInterruptURB, GFP_ATOMIC);
	if (status) {
		DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO
			"Submit int URB failed %d\n", status);
		priv->int_buf.in_use = false;
	}

	return status;
}

/*
 * Description:
 *      Complete function of usb interrupt in irp.
 *
 * Parameters:
 *  In:
 *      pDevice     - Pointer to the adapter
 *
 *  Out:
 *      none
 *
 * Return Value: STATUS_INSUFFICIENT_RESOURCES or result of IoCallDriver
 *
 */

static void s_nsInterruptUsbIoCompleteRead(struct urb *urb)
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

		DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO
			"IntUSBIoCompleteControl STATUS = %d\n", status);
	} else {
		INTnsProcessData(priv);
	}

	status = usb_submit_urb(priv->pInterruptURB, GFP_ATOMIC);
	if (status) {
		DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO
			"Submit int URB failed %d\n", status);
	} else {
		priv->int_buf.in_use = true;
	}

	return;
}

/*
 * Description:
 *      Allocates an usb BulkIn  irp and calls USBD.
 *
 * Parameters:
 *  In:
 *      pDevice     - Pointer to the adapter
 *  Out:
 *      none
 *
 * Return Value: STATUS_INSUFFICIENT_RESOURCES or result of IoCallDriver
 *
 */

int PIPEnsBulkInUsbRead(struct vnt_private *priv, struct vnt_rcb *rcb)
{
	int status = 0;
	struct urb *urb;

	if (priv->Flags & fMP_DISCONNECTED)
		return STATUS_FAILURE;

	urb = rcb->pUrb;
	if (rcb->skb == NULL) {
		DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"rcb->skb is null\n");
		return status;
	}

	usb_fill_bulk_urb(urb,
		priv->usb,
		usb_rcvbulkpipe(priv->usb, 2),
		(void *) (rcb->skb->data),
		MAX_TOTAL_SIZE_WITH_ALL_HEADERS,
		s_nsBulkInUsbIoCompleteRead,
		rcb);

	status = usb_submit_urb(urb, GFP_ATOMIC);
	if (status != 0) {
		DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO
				"Submit Rx URB failed %d\n", status);
		return STATUS_FAILURE ;
	}

	rcb->Ref = 1;
	rcb->bBoolInUse = true;

	return status;
}

/*
 * Description:
 *      Complete function of usb BulkIn irp.
 *
 * Parameters:
 *  In:
 *      pDevice     - Pointer to the adapter
 *
 *  Out:
 *      none
 *
 * Return Value: STATUS_INSUFFICIENT_RESOURCES or result of IoCallDriver
 *
 */

static void s_nsBulkInUsbIoCompleteRead(struct urb *urb)
{
	struct vnt_rcb *rcb = urb->context;
	struct vnt_private *priv = rcb->pDevice;
	unsigned long flags;
	int re_alloc_skb = false;

	switch (urb->status) {
	case 0:
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		return;
	case -ETIMEDOUT:
	default:
		DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO
				"BULK In failed %d\n", urb->status);
		break;
	}

	if (urb->actual_length) {
		spin_lock_irqsave(&priv->lock, flags);

		if (RXbBulkInProcessData(priv, rcb, urb->actual_length) == true)
			re_alloc_skb = true;

		spin_unlock_irqrestore(&priv->lock, flags);
	}

	rcb->Ref--;
	if (rcb->Ref == 0) {
		DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"RxvFreeNormal %d\n",
							priv->NumRecvFreeList);
		spin_lock_irqsave(&priv->lock, flags);

		RXvFreeRCB(rcb, re_alloc_skb);

		spin_unlock_irqrestore(&priv->lock, flags);
	}

	return;
}

/*
 * Description:
 *      Allocates an usb BulkOut  irp and calls USBD.
 *
 * Parameters:
 *  In:
 *      pDevice     - Pointer to the adapter
 *  Out:
 *      none
 *
 * Return Value: STATUS_INSUFFICIENT_RESOURCES or result of IoCallDriver
 *
 */

int PIPEnsSendBulkOut(struct vnt_private *priv,
				struct vnt_usb_send_context *context)
{
	int status;
	struct urb *urb;

	priv->bPWBitOn = false;

	if (!(MP_IS_READY(priv) && priv->Flags & fMP_POST_WRITES)) {
		context->in_use = false;
		return STATUS_RESOURCES;
	}

	urb = context->urb;

	usb_fill_bulk_urb(urb,
			priv->usb,
			usb_sndbulkpipe(priv->usb, 3),
			context->data,
			context->buf_len,
			s_nsBulkOutIoCompleteWrite,
			context);

	status = usb_submit_urb(urb, GFP_ATOMIC);
	if (status != 0) {
		DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO
				"Submit Tx URB failed %d\n", status);
		context->in_use = false;
		return STATUS_FAILURE;
	}

	return STATUS_PENDING;
}

/*
 * Description: s_nsBulkOutIoCompleteWrite
 *     1a) Indicate to the protocol the status of the write.
 *     1b) Return ownership of the packet to the protocol.
 *
 *     2)  If any more packets are queue for sending, send another packet
 *         to USBD.
 *         If the attempt to send the packet to the driver fails,
 *         return ownership of the packet to the protocol and
 *         try another packet (until one succeeds).
 *
 * Parameters:
 *  In:
 *      pdoUsbDevObj  - pointer to the USB device object which
 *                      completed the irp
 *      pIrp          - the irp which was completed by the
 *                      device object
 *      pContext      - the context given to IoSetCompletionRoutine
 *                      before calling IoCallDriver on the irp
 *                      The pContext is a pointer to the USB device object.
 *  Out:
 *      none
 *
 * Return Value: STATUS_MORE_PROCESSING_REQUIRED - allows the completion routine
 *               (IofCompleteRequest) to stop working on the irp.
 *
 */

static void s_nsBulkOutIoCompleteWrite(struct urb *urb)
{
	struct vnt_usb_send_context *context = urb->context;
	struct vnt_private *priv = context->priv;
	u8 context_type = context->type;

	switch (urb->status) {
	case 0:
		DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO
			"Write %d bytes\n", context->buf_len);
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		context->in_use = false;
		return;
	case -ETIMEDOUT:
	default:
		DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO
				"BULK Out failed %d\n", urb->status);
		break;
	}

	if (!netif_device_present(priv->dev))
		return;

	if (CONTEXT_DATA_PACKET == context_type) {
		if (context->skb != NULL) {
			dev_kfree_skb_irq(context->skb);
			context->skb = NULL;
			DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO
				"tx  %d bytes\n", context->buf_len);
		}

		priv->dev->trans_start = jiffies;
	}

	if (priv->bLinkPass == true) {
		if (netif_queue_stopped(priv->dev))
			netif_wake_queue(priv->dev);
	}

	context->in_use = false;

	return;
}
