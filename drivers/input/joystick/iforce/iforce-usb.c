 /*
 *  Copyright (c) 2000-2002 Vojtech Pavlik <vojtech@ucw.cz>
 *  Copyright (c) 2001-2002, 2007 Johann Deneux <johann.deneux@gmail.com>
 *
 *  USB/RS232 I-Force joysticks and wheels.
 */

/*
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include "iforce.h"

static void __iforce_usb_xmit(struct iforce *iforce)
{
	int n, c;
	unsigned long flags;

	spin_lock_irqsave(&iforce->xmit_lock, flags);

	if (iforce->xmit.head == iforce->xmit.tail) {
		clear_bit(IFORCE_XMIT_RUNNING, iforce->xmit_flags);
		spin_unlock_irqrestore(&iforce->xmit_lock, flags);
		return;
	}

	((char *)iforce->out->transfer_buffer)[0] = iforce->xmit.buf[iforce->xmit.tail];
	XMIT_INC(iforce->xmit.tail, 1);
	n = iforce->xmit.buf[iforce->xmit.tail];
	XMIT_INC(iforce->xmit.tail, 1);

	iforce->out->transfer_buffer_length = n + 1;
	iforce->out->dev = iforce->usbdev;

	/* Copy rest of data then */
	c = CIRC_CNT_TO_END(iforce->xmit.head, iforce->xmit.tail, XMIT_SIZE);
	if (n < c) c=n;

	memcpy(iforce->out->transfer_buffer + 1,
	       &iforce->xmit.buf[iforce->xmit.tail],
	       c);
	if (n != c) {
		memcpy(iforce->out->transfer_buffer + 1 + c,
		       &iforce->xmit.buf[0],
		       n-c);
	}
	XMIT_INC(iforce->xmit.tail, n);

	if ( (n=usb_submit_urb(iforce->out, GFP_ATOMIC)) ) {
		clear_bit(IFORCE_XMIT_RUNNING, iforce->xmit_flags);
		dev_warn(&iforce->intf->dev, "usb_submit_urb failed %d\n", n);
	}

	/* The IFORCE_XMIT_RUNNING bit is not cleared here. That's intended.
	 * As long as the urb completion handler is not called, the transmiting
	 * is considered to be running */
	spin_unlock_irqrestore(&iforce->xmit_lock, flags);
}

static void iforce_usb_xmit(struct iforce *iforce)
{
	if (!test_and_set_bit(IFORCE_XMIT_RUNNING, iforce->xmit_flags))
		__iforce_usb_xmit(iforce);
}

static int iforce_usb_get_id(struct iforce *iforce, u8 *packet)
{
	int status;

	iforce->cr.bRequest = packet[0];
	iforce->ctrl->dev = iforce->usbdev;

	status = usb_submit_urb(iforce->ctrl, GFP_KERNEL);
	if (status) {
		dev_err(&iforce->intf->dev,
			"usb_submit_urb failed %d\n", status);
		return -EIO;
	}

	wait_event_interruptible_timeout(iforce->wait,
		iforce->ctrl->status != -EINPROGRESS, HZ);

	if (iforce->ctrl->status) {
		dev_dbg(&iforce->intf->dev,
			"iforce->ctrl->status = %d\n",
			iforce->ctrl->status);
		usb_unlink_urb(iforce->ctrl);
		return -EIO;
	}

	return -(iforce->edata[0] != packet[0]);
}

static int iforce_usb_start_io(struct iforce *iforce)
{
	if (usb_submit_urb(iforce->irq, GFP_KERNEL))
		return -EIO;

	return 0;
}

static void iforce_usb_stop_io(struct iforce *iforce)
{
	usb_kill_urb(iforce->irq);
	usb_kill_urb(iforce->out);
	usb_kill_urb(iforce->ctrl);
}

static const struct iforce_xport_ops iforce_usb_xport_ops = {
	.xmit		= iforce_usb_xmit,
	.get_id		= iforce_usb_get_id,
	.start_io	= iforce_usb_start_io,
	.stop_io	= iforce_usb_stop_io,
};

static void iforce_usb_irq(struct urb *urb)
{
	struct iforce *iforce = urb->context;
	struct device *dev = &iforce->intf->dev;
	int status;

	switch (urb->status) {
	case 0:
		/* success */
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* this urb is terminated, clean up */
		dev_dbg(dev, "%s - urb shutting down with status: %d\n",
			__func__, urb->status);
		return;
	default:
		dev_dbg(dev, "%s - urb has status of: %d\n",
			__func__, urb->status);
		goto exit;
	}

	iforce_process_packet(iforce,
		(iforce->data[0] << 8) | (urb->actual_length - 1), iforce->data + 1);

exit:
	status = usb_submit_urb (urb, GFP_ATOMIC);
	if (status)
		dev_err(dev, "%s - usb_submit_urb failed with result %d\n",
			__func__, status);
}

static void iforce_usb_out(struct urb *urb)
{
	struct iforce *iforce = urb->context;

	if (urb->status) {
		clear_bit(IFORCE_XMIT_RUNNING, iforce->xmit_flags);
		dev_dbg(&iforce->intf->dev, "urb->status %d, exiting\n",
			urb->status);
		return;
	}

	__iforce_usb_xmit(iforce);

	wake_up(&iforce->wait);
}

static void iforce_usb_ctrl(struct urb *urb)
{
	struct iforce *iforce = urb->context;
	if (urb->status) return;
	iforce->ecmd = 0xff00 | urb->actual_length;
	wake_up(&iforce->wait);
}

static int iforce_usb_probe(struct usb_interface *intf,
				const struct usb_device_id *id)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	struct usb_host_interface *interface;
	struct usb_endpoint_descriptor *epirq, *epout;
	struct iforce *iforce;
	int err = -ENOMEM;

	interface = intf->cur_altsetting;

	if (interface->desc.bNumEndpoints < 2)
		return -ENODEV;

	epirq = &interface->endpoint[0].desc;
	epout = &interface->endpoint[1].desc;

	if (!(iforce = kzalloc(sizeof(struct iforce) + 32, GFP_KERNEL)))
		goto fail;

	if (!(iforce->irq = usb_alloc_urb(0, GFP_KERNEL)))
		goto fail;

	if (!(iforce->out = usb_alloc_urb(0, GFP_KERNEL)))
		goto fail;

	if (!(iforce->ctrl = usb_alloc_urb(0, GFP_KERNEL)))
		goto fail;

	iforce->xport_ops = &iforce_usb_xport_ops;
	iforce->bus = IFORCE_USB;
	iforce->usbdev = dev;
	iforce->intf = intf;

	iforce->cr.bRequestType = USB_TYPE_VENDOR | USB_DIR_IN | USB_RECIP_INTERFACE;
	iforce->cr.wIndex = 0;
	iforce->cr.wLength = cpu_to_le16(16);

	usb_fill_int_urb(iforce->irq, dev, usb_rcvintpipe(dev, epirq->bEndpointAddress),
			iforce->data, 16, iforce_usb_irq, iforce, epirq->bInterval);

	usb_fill_int_urb(iforce->out, dev, usb_sndintpipe(dev, epout->bEndpointAddress),
			iforce + 1, 32, iforce_usb_out, iforce, epout->bInterval);

	usb_fill_control_urb(iforce->ctrl, dev, usb_rcvctrlpipe(dev, 0),
			(void*) &iforce->cr, iforce->edata, 16, iforce_usb_ctrl, iforce);

	err = iforce_init_device(iforce);
	if (err)
		goto fail;

	usb_set_intfdata(intf, iforce);
	return 0;

fail:
	if (iforce) {
		usb_free_urb(iforce->irq);
		usb_free_urb(iforce->out);
		usb_free_urb(iforce->ctrl);
		kfree(iforce);
	}

	return err;
}

static void iforce_usb_disconnect(struct usb_interface *intf)
{
	struct iforce *iforce = usb_get_intfdata(intf);

	usb_set_intfdata(intf, NULL);

	input_unregister_device(iforce->dev);

	usb_free_urb(iforce->irq);
	usb_free_urb(iforce->out);
	usb_free_urb(iforce->ctrl);

	kfree(iforce);
}

static const struct usb_device_id iforce_usb_ids[] = {
	{ USB_DEVICE(0x044f, 0xa01c) },		/* Thrustmaster Motor Sport GT */
	{ USB_DEVICE(0x046d, 0xc281) },		/* Logitech WingMan Force */
	{ USB_DEVICE(0x046d, 0xc291) },		/* Logitech WingMan Formula Force */
	{ USB_DEVICE(0x05ef, 0x020a) },		/* AVB Top Shot Pegasus */
	{ USB_DEVICE(0x05ef, 0x8884) },		/* AVB Mag Turbo Force */
	{ USB_DEVICE(0x05ef, 0x8888) },		/* AVB Top Shot FFB Racing Wheel */
	{ USB_DEVICE(0x061c, 0xc0a4) },         /* ACT LABS Force RS */
	{ USB_DEVICE(0x061c, 0xc084) },         /* ACT LABS Force RS */
	{ USB_DEVICE(0x06f8, 0x0001) },		/* Guillemot Race Leader Force Feedback */
	{ USB_DEVICE(0x06f8, 0x0003) },		/* Guillemot Jet Leader Force Feedback */
	{ USB_DEVICE(0x06f8, 0x0004) },		/* Guillemot Force Feedback Racing Wheel */
	{ USB_DEVICE(0x06f8, 0xa302) },		/* Guillemot Jet Leader 3D */
	{ }					/* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, iforce_usb_ids);

struct usb_driver iforce_usb_driver = {
	.name =		"iforce",
	.probe =	iforce_usb_probe,
	.disconnect =	iforce_usb_disconnect,
	.id_table =	iforce_usb_ids,
};
