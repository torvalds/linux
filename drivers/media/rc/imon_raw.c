// SPDX-License-Identifier: GPL-2.0+
//
// Copyright (C) 2018 Sean Young <sean@mess.org>

#include <linux/module.h>
#include <linux/usb.h>
#include <linux/usb/input.h>
#include <media/rc-core.h>

/* Each bit is 250us */
#define BIT_DURATION 250000

struct imon {
	struct device *dev;
	struct urb *ir_urb;
	struct rc_dev *rcdev;
	u8 ir_buf[8];
	char phys[64];
};

/*
 * ffs/find_next_bit() searches in the wrong direction, so open-code our own.
 */
static inline int is_bit_set(const u8 *buf, int bit)
{
	return buf[bit / 8] & (0x80 >> (bit & 7));
}

static void imon_ir_data(struct imon *imon)
{
	struct ir_raw_event rawir = {};
	int offset = 0, size = 5 * 8;
	int bit;

	dev_dbg(imon->dev, "data: %*ph", 8, imon->ir_buf);

	while (offset < size) {
		bit = offset;
		while (!is_bit_set(imon->ir_buf, bit) && bit < size)
			bit++;
		dev_dbg(imon->dev, "pulse: %d bits", bit - offset);
		if (bit > offset) {
			rawir.pulse = true;
			rawir.duration = (bit - offset) * BIT_DURATION;
			ir_raw_event_store_with_filter(imon->rcdev, &rawir);
		}

		if (bit >= size)
			break;

		offset = bit;
		while (is_bit_set(imon->ir_buf, bit) && bit < size)
			bit++;
		dev_dbg(imon->dev, "space: %d bits", bit - offset);

		rawir.pulse = false;
		rawir.duration = (bit - offset) * BIT_DURATION;
		ir_raw_event_store_with_filter(imon->rcdev, &rawir);

		offset = bit;
	}

	if (imon->ir_buf[7] == 0x0a) {
		ir_raw_event_set_idle(imon->rcdev, true);
		ir_raw_event_handle(imon->rcdev);
	}
}

static void imon_ir_rx(struct urb *urb)
{
	struct imon *imon = urb->context;
	int ret;

	switch (urb->status) {
	case 0:
		if (imon->ir_buf[7] != 0xff)
			imon_ir_data(imon);
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		usb_unlink_urb(urb);
		return;
	case -EPIPE:
	default:
		dev_dbg(imon->dev, "error: urb status = %d", urb->status);
		break;
	}

	ret = usb_submit_urb(urb, GFP_ATOMIC);
	if (ret && ret != -ENODEV)
		dev_warn(imon->dev, "failed to resubmit urb: %d", ret);
}

static int imon_probe(struct usb_interface *intf,
		      const struct usb_device_id *id)
{
	struct usb_endpoint_descriptor *ir_ep = NULL;
	struct usb_host_interface *idesc;
	struct usb_device *udev;
	struct rc_dev *rcdev;
	struct imon *imon;
	int i, ret;

	udev = interface_to_usbdev(intf);
	idesc = intf->cur_altsetting;

	for (i = 0; i < idesc->desc.bNumEndpoints; i++) {
		struct usb_endpoint_descriptor *ep = &idesc->endpoint[i].desc;

		if (usb_endpoint_is_int_in(ep)) {
			ir_ep = ep;
			break;
		}
	}

	if (!ir_ep) {
		dev_err(&intf->dev, "IR endpoint missing");
		return -ENODEV;
	}

	imon = devm_kmalloc(&intf->dev, sizeof(*imon), GFP_KERNEL);
	if (!imon)
		return -ENOMEM;

	imon->ir_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!imon->ir_urb)
		return -ENOMEM;

	imon->dev = &intf->dev;
	usb_fill_int_urb(imon->ir_urb, udev,
			 usb_rcvintpipe(udev, ir_ep->bEndpointAddress),
			 imon->ir_buf, sizeof(imon->ir_buf),
			 imon_ir_rx, imon, ir_ep->bInterval);

	rcdev = devm_rc_allocate_device(&intf->dev, RC_DRIVER_IR_RAW);
	if (!rcdev) {
		ret = -ENOMEM;
		goto free_urb;
	}

	usb_make_path(udev, imon->phys, sizeof(imon->phys));

	rcdev->device_name = "iMON Station";
	rcdev->driver_name = KBUILD_MODNAME;
	rcdev->input_phys = imon->phys;
	usb_to_input_id(udev, &rcdev->input_id);
	rcdev->dev.parent = &intf->dev;
	rcdev->allowed_protocols = RC_PROTO_BIT_ALL_IR_DECODER;
	rcdev->map_name = RC_MAP_IMON_RSC;
	rcdev->rx_resolution = BIT_DURATION;
	rcdev->priv = imon;

	ret = devm_rc_register_device(&intf->dev, rcdev);
	if (ret)
		goto free_urb;

	imon->rcdev = rcdev;

	ret = usb_submit_urb(imon->ir_urb, GFP_KERNEL);
	if (ret)
		goto free_urb;

	usb_set_intfdata(intf, imon);

	return 0;

free_urb:
	usb_free_urb(imon->ir_urb);
	return ret;
}

static void imon_disconnect(struct usb_interface *intf)
{
	struct imon *imon = usb_get_intfdata(intf);

	usb_kill_urb(imon->ir_urb);
	usb_free_urb(imon->ir_urb);
}

static const struct usb_device_id imon_table[] = {
	/* SoundGraph iMON (IR only) -- sg_imon.inf */
	{ USB_DEVICE(0x04e8, 0xff30) },
	{}
};

static struct usb_driver imon_driver = {
	.name = KBUILD_MODNAME,
	.probe = imon_probe,
	.disconnect = imon_disconnect,
	.id_table = imon_table
};

module_usb_driver(imon_driver);

MODULE_DESCRIPTION("Early raw iMON IR devices");
MODULE_AUTHOR("Sean Young <sean@mess.org>");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(usb, imon_table);
