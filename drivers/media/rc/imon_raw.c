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
	__be64 ir_buf;
	char phys[64];
};

/*
 * The first 5 bytes of data represent IR pulse or space. Each bit, starting
 * from highest bit in the first byte, represents 250µs of data. It is 1
 * for space and 0 for pulse.
 *
 * The station sends 10 packets, and the 7th byte will be number 1 to 10, so
 * when we receive 10 we assume all the data has arrived.
 */
static void imon_ir_data(struct imon *imon)
{
	struct ir_raw_event rawir = {};
	u64 data = be64_to_cpu(imon->ir_buf);
	u8 packet_no = data & 0xff;
	int offset = 40;
	int bit;

	if (packet_no == 0xff)
		return;

	dev_dbg(imon->dev, "data: %*ph", 8, &imon->ir_buf);

	/*
	 * Only the first 5 bytes contain IR data. Right shift so we move
	 * the IR bits to the lower 40 bits.
	 */
	data >>= 24;

	do {
		/*
		 * Find highest set bit which is less or equal to offset
		 *
		 * offset is the bit above (base 0) where we start looking.
		 *
		 * data & (BIT_ULL(offset) - 1) masks off any unwanted bits,
		 * so we have just bits less than offset.
		 *
		 * fls will tell us the highest bit set plus 1 (or 0 if no
		 * bits are set).
		 */
		rawir.pulse = !rawir.pulse;
		bit = fls64(data & (BIT_ULL(offset) - 1));
		if (bit < offset) {
			dev_dbg(imon->dev, "%s: %d bits",
				rawir.pulse ? "pulse" : "space", offset - bit);
			rawir.duration = (offset - bit) * BIT_DURATION;
			ir_raw_event_store_with_filter(imon->rcdev, &rawir);

			offset = bit;
		}

		data = ~data;
	} while (offset > 0);

	if (packet_no == 0x0a && !imon->rcdev->idle) {
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
			 &imon->ir_buf, sizeof(imon->ir_buf),
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
