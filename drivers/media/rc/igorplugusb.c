// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * IgorPlug-USB IR Receiver
 *
 * Copyright (C) 2014 Sean Young <sean@mess.org>
 *
 * Supports the standard homebrew IgorPlugUSB receiver with Igor's firmware.
 * See http://www.cesko.host.sk/IgorPlugUSB/IgorPlug-USB%20(AVR)_eng.htm
 *
 * Based on the lirc_igorplugusb.c driver:
 *	Copyright (C) 2004 Jan M. Hochstein
 *	<hochstein@algo.informatik.tu-darmstadt.de>
 */
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/usb/input.h>
#include <media/rc-core.h>

#define DRIVER_DESC		"IgorPlug-USB IR Receiver"
#define DRIVER_NAME		"igorplugusb"

#define HEADERLEN		3
#define BUFLEN			36
#define MAX_PACKET		(HEADERLEN + BUFLEN)

#define SET_INFRABUFFER_EMPTY	1
#define GET_INFRACODE		2


struct igorplugusb {
	struct rc_dev *rc;
	struct device *dev;

	struct urb *urb;
	struct usb_ctrlrequest request;

	struct timer_list timer;

	uint8_t buf_in[MAX_PACKET];

	char phys[64];
};

static void igorplugusb_cmd(struct igorplugusb *ir, int cmd);

static void igorplugusb_irdata(struct igorplugusb *ir, unsigned len)
{
	struct ir_raw_event rawir = {};
	unsigned i, start, overflow;

	dev_dbg(ir->dev, "irdata: %*ph (len=%u)", len, ir->buf_in, len);

	/*
	 * If more than 36 pulses and spaces follow each other, the igorplugusb
	 * overwrites its buffer from the beginning. The overflow value is the
	 * last offset which was not overwritten. Everything from this offset
	 * onwards occurred before everything until this offset.
	 */
	overflow = ir->buf_in[2];
	i = start = overflow + HEADERLEN;

	if (start >= len) {
		dev_err(ir->dev, "receive overflow invalid: %u", overflow);
	} else {
		if (overflow > 0)
			dev_warn(ir->dev, "receive overflow, at least %u lost",
								overflow);

		do {
			rawir.duration = ir->buf_in[i] * 85;
			rawir.pulse = i & 1;

			ir_raw_event_store_with_filter(ir->rc, &rawir);

			if (++i == len)
				i = HEADERLEN;
		} while (i != start);

		/* add a trailing space */
		rawir.duration = ir->rc->timeout;
		rawir.pulse = false;
		ir_raw_event_store_with_filter(ir->rc, &rawir);

		ir_raw_event_handle(ir->rc);
	}

	igorplugusb_cmd(ir, SET_INFRABUFFER_EMPTY);
}

static void igorplugusb_callback(struct urb *urb)
{
	struct usb_ctrlrequest *req;
	struct igorplugusb *ir = urb->context;

	req = (struct usb_ctrlrequest *)urb->setup_packet;

	switch (urb->status) {
	case 0:
		if (req->bRequest == GET_INFRACODE &&
					urb->actual_length > HEADERLEN)
			igorplugusb_irdata(ir, urb->actual_length);
		else /* request IR */
			mod_timer(&ir->timer, jiffies + msecs_to_jiffies(50));
		break;
	case -EPROTO:
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		usb_unlink_urb(urb);
		return;
	default:
		dev_warn(ir->dev, "Error: urb status = %d\n", urb->status);
		igorplugusb_cmd(ir, SET_INFRABUFFER_EMPTY);
		break;
	}
}

static void igorplugusb_cmd(struct igorplugusb *ir, int cmd)
{
	int ret;

	ir->request.bRequest = cmd;
	ir->urb->transfer_flags = 0;
	ret = usb_submit_urb(ir->urb, GFP_ATOMIC);
	if (ret)
		dev_err(ir->dev, "submit urb failed: %d", ret);
}

static void igorplugusb_timer(struct timer_list *t)
{
	struct igorplugusb *ir = from_timer(ir, t, timer);

	igorplugusb_cmd(ir, GET_INFRACODE);
}

static int igorplugusb_probe(struct usb_interface *intf,
					const struct usb_device_id *id)
{
	struct usb_device *udev;
	struct usb_host_interface *idesc;
	struct usb_endpoint_descriptor *ep;
	struct igorplugusb *ir;
	struct rc_dev *rc;
	int ret = -ENOMEM;

	udev = interface_to_usbdev(intf);
	idesc = intf->cur_altsetting;

	if (idesc->desc.bNumEndpoints != 1) {
		dev_err(&intf->dev, "incorrect number of endpoints");
		return -ENODEV;
	}

	ep = &idesc->endpoint[0].desc;
	if (!usb_endpoint_dir_in(ep) || !usb_endpoint_xfer_control(ep)) {
		dev_err(&intf->dev, "endpoint incorrect");
		return -ENODEV;
	}

	ir = devm_kzalloc(&intf->dev, sizeof(*ir), GFP_KERNEL);
	if (!ir)
		return -ENOMEM;

	ir->dev = &intf->dev;

	timer_setup(&ir->timer, igorplugusb_timer, 0);

	ir->request.bRequest = GET_INFRACODE;
	ir->request.bRequestType = USB_TYPE_VENDOR | USB_DIR_IN;
	ir->request.wLength = cpu_to_le16(sizeof(ir->buf_in));

	ir->urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!ir->urb)
		goto fail;

	usb_fill_control_urb(ir->urb, udev,
		usb_rcvctrlpipe(udev, 0), (uint8_t *)&ir->request,
		ir->buf_in, sizeof(ir->buf_in), igorplugusb_callback, ir);

	usb_make_path(udev, ir->phys, sizeof(ir->phys));

	rc = rc_allocate_device(RC_DRIVER_IR_RAW);
	if (!rc)
		goto fail;

	rc->device_name = DRIVER_DESC;
	rc->input_phys = ir->phys;
	usb_to_input_id(udev, &rc->input_id);
	rc->dev.parent = &intf->dev;
	/*
	 * This device can only store 36 pulses + spaces, which is not enough
	 * for the NEC protocol and many others.
	 */
	rc->allowed_protocols = RC_PROTO_BIT_ALL_IR_DECODER &
		~(RC_PROTO_BIT_NEC | RC_PROTO_BIT_NECX | RC_PROTO_BIT_NEC32 |
		  RC_PROTO_BIT_RC6_6A_20 | RC_PROTO_BIT_RC6_6A_24 |
		  RC_PROTO_BIT_RC6_6A_32 | RC_PROTO_BIT_RC6_MCE |
		  RC_PROTO_BIT_SONY20 | RC_PROTO_BIT_SANYO);

	rc->priv = ir;
	rc->driver_name = DRIVER_NAME;
	rc->map_name = RC_MAP_HAUPPAUGE;
	rc->timeout = MS_TO_US(100);
	rc->rx_resolution = 85;

	ir->rc = rc;
	ret = rc_register_device(rc);
	if (ret) {
		dev_err(&intf->dev, "failed to register rc device: %d", ret);
		goto fail;
	}

	usb_set_intfdata(intf, ir);

	igorplugusb_cmd(ir, SET_INFRABUFFER_EMPTY);

	return 0;
fail:
	rc_free_device(ir->rc);
	usb_free_urb(ir->urb);
	del_timer(&ir->timer);

	return ret;
}

static void igorplugusb_disconnect(struct usb_interface *intf)
{
	struct igorplugusb *ir = usb_get_intfdata(intf);

	rc_unregister_device(ir->rc);
	del_timer_sync(&ir->timer);
	usb_set_intfdata(intf, NULL);
	usb_kill_urb(ir->urb);
	usb_free_urb(ir->urb);
}

static const struct usb_device_id igorplugusb_table[] = {
	/* Igor Plug USB (Atmel's Manufact. ID) */
	{ USB_DEVICE(0x03eb, 0x0002) },
	/* Fit PC2 Infrared Adapter */
	{ USB_DEVICE(0x03eb, 0x21fe) },
	/* Terminating entry */
	{ }
};

static struct usb_driver igorplugusb_driver = {
	.name =	DRIVER_NAME,
	.probe = igorplugusb_probe,
	.disconnect = igorplugusb_disconnect,
	.id_table = igorplugusb_table
};

module_usb_driver(igorplugusb_driver);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR("Sean Young <sean@mess.org>");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(usb, igorplugusb_table);
