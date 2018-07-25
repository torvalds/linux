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

#include <linux/usb.h>
#include "iforce.h"

struct iforce_usb {
	struct iforce iforce;

	struct usb_device *usbdev;
	struct usb_interface *intf;
	struct urb *irq, *out, *ctrl;
	struct usb_ctrlrequest cr;
};

static void __iforce_usb_xmit(struct iforce *iforce)
{
	struct iforce_usb *iforce_usb = container_of(iforce, struct iforce_usb,
						     iforce);
	int n, c;
	unsigned long flags;

	spin_lock_irqsave(&iforce->xmit_lock, flags);

	if (iforce->xmit.head == iforce->xmit.tail) {
		clear_bit(IFORCE_XMIT_RUNNING, iforce->xmit_flags);
		spin_unlock_irqrestore(&iforce->xmit_lock, flags);
		return;
	}

	((char *)iforce_usb->out->transfer_buffer)[0] = iforce->xmit.buf[iforce->xmit.tail];
	XMIT_INC(iforce->xmit.tail, 1);
	n = iforce->xmit.buf[iforce->xmit.tail];
	XMIT_INC(iforce->xmit.tail, 1);

	iforce_usb->out->transfer_buffer_length = n + 1;
	iforce_usb->out->dev = iforce_usb->usbdev;

	/* Copy rest of data then */
	c = CIRC_CNT_TO_END(iforce->xmit.head, iforce->xmit.tail, XMIT_SIZE);
	if (n < c) c=n;

	memcpy(iforce_usb->out->transfer_buffer + 1,
	       &iforce->xmit.buf[iforce->xmit.tail],
	       c);
	if (n != c) {
		memcpy(iforce_usb->out->transfer_buffer + 1 + c,
		       &iforce->xmit.buf[0],
		       n-c);
	}
	XMIT_INC(iforce->xmit.tail, n);

	if ( (n=usb_submit_urb(iforce_usb->out, GFP_ATOMIC)) ) {
		clear_bit(IFORCE_XMIT_RUNNING, iforce->xmit_flags);
		dev_warn(&iforce_usb->intf->dev,
			 "usb_submit_urb failed %d\n", n);
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
	struct iforce_usb *iforce_usb = container_of(iforce, struct iforce_usb,
						     iforce);
	int status;

	iforce_usb->cr.bRequest = packet[0];
	iforce_usb->ctrl->dev = iforce_usb->usbdev;

	status = usb_submit_urb(iforce_usb->ctrl, GFP_KERNEL);
	if (status) {
		dev_err(&iforce_usb->intf->dev,
			"usb_submit_urb failed %d\n", status);
		return -EIO;
	}

	wait_event_interruptible_timeout(iforce->wait,
		iforce_usb->ctrl->status != -EINPROGRESS, HZ);

	if (iforce_usb->ctrl->status) {
		dev_dbg(&iforce_usb->intf->dev,
			"iforce->ctrl->status = %d\n",
			iforce_usb->ctrl->status);
		usb_unlink_urb(iforce_usb->ctrl);
		return -EIO;
	}

	return -(iforce->edata[0] != packet[0]);
}

static int iforce_usb_start_io(struct iforce *iforce)
{
	struct iforce_usb *iforce_usb = container_of(iforce, struct iforce_usb,
						     iforce);

	if (usb_submit_urb(iforce_usb->irq, GFP_KERNEL))
		return -EIO;

	return 0;
}

static void iforce_usb_stop_io(struct iforce *iforce)
{
	struct iforce_usb *iforce_usb = container_of(iforce, struct iforce_usb,
						     iforce);

	usb_kill_urb(iforce_usb->irq);
	usb_kill_urb(iforce_usb->out);
	usb_kill_urb(iforce_usb->ctrl);
}

static const struct iforce_xport_ops iforce_usb_xport_ops = {
	.xmit		= iforce_usb_xmit,
	.get_id		= iforce_usb_get_id,
	.start_io	= iforce_usb_start_io,
	.stop_io	= iforce_usb_stop_io,
};

static void iforce_usb_irq(struct urb *urb)
{
	struct iforce_usb *iforce_usb = urb->context;
	struct iforce *iforce = &iforce_usb->iforce;
	struct device *dev = &iforce_usb->intf->dev;
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
	status = usb_submit_urb(urb, GFP_ATOMIC);
	if (status)
		dev_err(dev, "%s - usb_submit_urb failed with result %d\n",
			__func__, status);
}

static void iforce_usb_out(struct urb *urb)
{
	struct iforce_usb *iforce_usb = urb->context;
	struct iforce *iforce = &iforce_usb->iforce;

	if (urb->status) {
		clear_bit(IFORCE_XMIT_RUNNING, iforce->xmit_flags);
		dev_dbg(&iforce_usb->intf->dev, "urb->status %d, exiting\n",
			urb->status);
		return;
	}

	__iforce_usb_xmit(iforce);

	wake_up(&iforce->wait);
}

static void iforce_usb_ctrl(struct urb *urb)
{
	struct iforce_usb *iforce_usb = urb->context;
	struct iforce *iforce = &iforce_usb->iforce;

	if (urb->status)
		return;

	iforce->ecmd = 0xff00 | urb->actual_length;
	wake_up(&iforce->wait);
}

static int iforce_usb_probe(struct usb_interface *intf,
				const struct usb_device_id *id)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	struct usb_host_interface *interface;
	struct usb_endpoint_descriptor *epirq, *epout;
	struct iforce_usb *iforce_usb;
	struct iforce *iforce;
	int err = -ENOMEM;

	interface = intf->cur_altsetting;

	if (interface->desc.bNumEndpoints < 2)
		return -ENODEV;

	epirq = &interface->endpoint[0].desc;
	epout = &interface->endpoint[1].desc;

	if (!(iforce_usb = kzalloc(sizeof(*iforce_usb) + 32, GFP_KERNEL)))
		goto fail;

	if (!(iforce_usb->irq = usb_alloc_urb(0, GFP_KERNEL)))
		goto fail;

	if (!(iforce_usb->out = usb_alloc_urb(0, GFP_KERNEL)))
		goto fail;

	if (!(iforce_usb->ctrl = usb_alloc_urb(0, GFP_KERNEL)))
		goto fail;

	iforce = &iforce_usb->iforce;

	iforce->xport_ops = &iforce_usb_xport_ops;
	iforce->bus = IFORCE_USB;

	iforce_usb->usbdev = dev;
	iforce_usb->intf = intf;

	iforce_usb->cr.bRequestType = USB_TYPE_VENDOR | USB_DIR_IN | USB_RECIP_INTERFACE;
	iforce_usb->cr.wIndex = 0;
	iforce_usb->cr.wLength = cpu_to_le16(16);

	usb_fill_int_urb(iforce_usb->irq, dev, usb_rcvintpipe(dev, epirq->bEndpointAddress),
			iforce->data, 16, iforce_usb_irq, iforce_usb, epirq->bInterval);

	usb_fill_int_urb(iforce_usb->out, dev, usb_sndintpipe(dev, epout->bEndpointAddress),
			iforce_usb + 1, 32, iforce_usb_out, iforce_usb, epout->bInterval);

	usb_fill_control_urb(iforce_usb->ctrl, dev, usb_rcvctrlpipe(dev, 0),
			(void*) &iforce_usb->cr, iforce->edata, 16, iforce_usb_ctrl, iforce_usb);

	err = iforce_init_device(&intf->dev, BUS_USB, iforce);
	if (err)
		goto fail;

	usb_set_intfdata(intf, iforce_usb);
	return 0;

fail:
	if (iforce_usb) {
		usb_free_urb(iforce_usb->irq);
		usb_free_urb(iforce_usb->out);
		usb_free_urb(iforce_usb->ctrl);
		kfree(iforce_usb);
	}

	return err;
}

static void iforce_usb_disconnect(struct usb_interface *intf)
{
	struct iforce_usb *iforce_usb = usb_get_intfdata(intf);

	usb_set_intfdata(intf, NULL);

	input_unregister_device(iforce_usb->iforce.dev);

	usb_free_urb(iforce_usb->irq);
	usb_free_urb(iforce_usb->out);
	usb_free_urb(iforce_usb->ctrl);

	kfree(iforce_usb);
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

module_usb_driver(iforce_usb_driver);

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>, Johann Deneux <johann.deneux@gmail.com>");
MODULE_DESCRIPTION("USB I-Force joysticks and wheels driver");
MODULE_LICENSE("GPL");
