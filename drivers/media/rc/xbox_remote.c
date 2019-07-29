// SPDX-License-Identifier: GPL-2.0+
// Driver for Xbox DVD Movie Playback Kit
// Copyright (c) 2018 by Benjamin Valentin <benpicco@googlemail.com>

/*
 *  Xbox DVD Movie Playback Kit USB IR dongle support
 *
 *  The driver was derived from the ati_remote driver 2.2.1
 *          and used information from lirc_xbox.c
 *
 *          Copyright (c) 2011, 2012 Anssi Hannula <anssi.hannula@iki.fi>
 *          Copyright (c) 2004 Torrey Hoffman <thoffman@arnor.net>
 *          Copyright (c) 2002 Vladimir Dergachev
 *          Copyright (c) 2003-2004 Paul Miller <pmiller9@users.sourceforge.net>
 */

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/usb/input.h>
#include <media/rc-core.h>

/*
 * Module and Version Information
 */
#define DRIVER_VERSION	"1.0.0"
#define DRIVER_AUTHOR	"Benjamin Valentin <benpicco@googlemail.com>"
#define DRIVER_DESC		"Xbox DVD USB Remote Control"

#define NAME_BUFSIZE      80    /* size of product name, path buffers */
#define DATA_BUFSIZE      8     /* size of URB data buffers */

/*
 * USB vendor ids for XBOX DVD Dongles
 */
#define VENDOR_GAMESTER     0x040b
#define VENDOR_MICROSOFT    0x045e

static const struct usb_device_id xbox_remote_table[] = {
	/* Gamester Xbox DVD Movie Playback Kit IR */
	{
		USB_DEVICE(VENDOR_GAMESTER, 0x6521),
	},
	/* Microsoft Xbox DVD Movie Playback Kit IR */
	{
		USB_DEVICE(VENDOR_MICROSOFT, 0x0284),
	},
	{}	/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, xbox_remote_table);

struct xbox_remote {
	struct rc_dev *rdev;
	struct usb_device *udev;
	struct usb_interface *interface;

	struct urb *irq_urb;
	unsigned char inbuf[DATA_BUFSIZE] __aligned(sizeof(u16));

	char rc_name[NAME_BUFSIZE];
	char rc_phys[NAME_BUFSIZE];
};

static int xbox_remote_rc_open(struct rc_dev *rdev)
{
	struct xbox_remote *xbox_remote = rdev->priv;

	/* On first open, submit the read urb which was set up previously. */
	xbox_remote->irq_urb->dev = xbox_remote->udev;
	if (usb_submit_urb(xbox_remote->irq_urb, GFP_KERNEL)) {
		dev_err(&xbox_remote->interface->dev,
			"%s: usb_submit_urb failed!\n", __func__);
		return -EIO;
	}

	return 0;
}

static void xbox_remote_rc_close(struct rc_dev *rdev)
{
	struct xbox_remote *xbox_remote = rdev->priv;

	usb_kill_urb(xbox_remote->irq_urb);
}

/*
 * xbox_remote_report_input
 */
static void xbox_remote_input_report(struct urb *urb)
{
	struct xbox_remote *xbox_remote = urb->context;
	unsigned char *data = xbox_remote->inbuf;

	/*
	 * data[0] = 0x00
	 * data[1] = length - always 0x06
	 * data[2] = the key code
	 * data[3] = high part of key code
	 * data[4] = last_press_ms (low)
	 * data[5] = last_press_ms (high)
	 */

	/* Deal with strange looking inputs */
	if (urb->actual_length != 6 || urb->actual_length != data[1]) {
		dev_warn(&urb->dev->dev, "Weird data, len=%d: %*ph\n",
			 urb->actual_length, urb->actual_length, data);
		return;
	}

	rc_keydown(xbox_remote->rdev, RC_PROTO_UNKNOWN,
		   le16_to_cpup((__le16 *)(data + 2)), 0);
}

/*
 * xbox_remote_irq_in
 */
static void xbox_remote_irq_in(struct urb *urb)
{
	struct xbox_remote *xbox_remote = urb->context;
	int retval;

	switch (urb->status) {
	case 0:			/* success */
		xbox_remote_input_report(urb);
		break;
	case -ECONNRESET:	/* unlink */
	case -ENOENT:
	case -ESHUTDOWN:
		dev_dbg(&xbox_remote->interface->dev,
			"%s: urb error status, unlink?\n",
			__func__);
		return;
	default:		/* error */
		dev_dbg(&xbox_remote->interface->dev,
			"%s: Nonzero urb status %d\n",
			__func__, urb->status);
	}

	retval = usb_submit_urb(urb, GFP_ATOMIC);
	if (retval)
		dev_err(&xbox_remote->interface->dev,
			"%s: usb_submit_urb()=%d\n",
			__func__, retval);
}

static void xbox_remote_rc_init(struct xbox_remote *xbox_remote)
{
	struct rc_dev *rdev = xbox_remote->rdev;

	rdev->priv = xbox_remote;
	rdev->allowed_protocols = RC_PROTO_BIT_UNKNOWN;
	rdev->driver_name = "xbox_remote";

	rdev->open = xbox_remote_rc_open;
	rdev->close = xbox_remote_rc_close;

	rdev->device_name = xbox_remote->rc_name;
	rdev->input_phys = xbox_remote->rc_phys;

	usb_to_input_id(xbox_remote->udev, &rdev->input_id);
	rdev->dev.parent = &xbox_remote->interface->dev;
}

static int xbox_remote_initialize(struct xbox_remote *xbox_remote,
				  struct usb_endpoint_descriptor *endpoint_in)
{
	struct usb_device *udev = xbox_remote->udev;
	int pipe, maxp;

	/* Set up irq_urb */
	pipe = usb_rcvintpipe(udev, endpoint_in->bEndpointAddress);
	maxp = usb_maxpacket(udev, pipe, usb_pipeout(pipe));
	maxp = (maxp > DATA_BUFSIZE) ? DATA_BUFSIZE : maxp;

	usb_fill_int_urb(xbox_remote->irq_urb, udev, pipe, xbox_remote->inbuf,
			 maxp, xbox_remote_irq_in, xbox_remote,
			 endpoint_in->bInterval);

	return 0;
}

/*
 * xbox_remote_probe
 */
static int xbox_remote_probe(struct usb_interface *interface,
			     const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(interface);
	struct usb_host_interface *iface_host = interface->cur_altsetting;
	struct usb_endpoint_descriptor *endpoint_in;
	struct xbox_remote *xbox_remote;
	struct rc_dev *rc_dev;
	int err = -ENOMEM;

	// why is there also a device with no endpoints?
	if (iface_host->desc.bNumEndpoints == 0)
		return -ENODEV;

	if (iface_host->desc.bNumEndpoints != 1) {
		pr_err("%s: Unexpected desc.bNumEndpoints: %d\n",
		       __func__, iface_host->desc.bNumEndpoints);
		return -ENODEV;
	}

	endpoint_in = &iface_host->endpoint[0].desc;

	if (!usb_endpoint_is_int_in(endpoint_in)) {
		pr_err("%s: Unexpected endpoint_in\n", __func__);
		return -ENODEV;
	}
	if (le16_to_cpu(endpoint_in->wMaxPacketSize) == 0) {
		pr_err("%s: endpoint_in message size==0?\n", __func__);
		return -ENODEV;
	}

	xbox_remote = kzalloc(sizeof(*xbox_remote), GFP_KERNEL);
	rc_dev = rc_allocate_device(RC_DRIVER_SCANCODE);
	if (!xbox_remote || !rc_dev)
		goto exit_free_dev_rdev;

	/* Allocate URB buffer */
	xbox_remote->irq_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!xbox_remote->irq_urb)
		goto exit_free_buffers;

	xbox_remote->udev = udev;
	xbox_remote->rdev = rc_dev;
	xbox_remote->interface = interface;

	usb_make_path(udev, xbox_remote->rc_phys, sizeof(xbox_remote->rc_phys));

	strlcat(xbox_remote->rc_phys, "/input0", sizeof(xbox_remote->rc_phys));

	snprintf(xbox_remote->rc_name, sizeof(xbox_remote->rc_name), "%s%s%s",
		 udev->manufacturer ?: "",
		 udev->manufacturer && udev->product ? " " : "",
		 udev->product ?: "");

	if (!strlen(xbox_remote->rc_name))
		snprintf(xbox_remote->rc_name, sizeof(xbox_remote->rc_name),
			 DRIVER_DESC "(%04x,%04x)",
			 le16_to_cpu(xbox_remote->udev->descriptor.idVendor),
			 le16_to_cpu(xbox_remote->udev->descriptor.idProduct));

	rc_dev->map_name = RC_MAP_XBOX_DVD; /* default map */

	xbox_remote_rc_init(xbox_remote);

	/* Device Hardware Initialization */
	err = xbox_remote_initialize(xbox_remote, endpoint_in);
	if (err)
		goto exit_kill_urbs;

	/* Set up and register rc device */
	err = rc_register_device(xbox_remote->rdev);
	if (err)
		goto exit_kill_urbs;

	usb_set_intfdata(interface, xbox_remote);

	return 0;

exit_kill_urbs:
	usb_kill_urb(xbox_remote->irq_urb);
exit_free_buffers:
	usb_free_urb(xbox_remote->irq_urb);
exit_free_dev_rdev:
	rc_free_device(rc_dev);
	kfree(xbox_remote);

	return err;
}

/*
 * xbox_remote_disconnect
 */
static void xbox_remote_disconnect(struct usb_interface *interface)
{
	struct xbox_remote *xbox_remote;

	xbox_remote = usb_get_intfdata(interface);
	usb_set_intfdata(interface, NULL);
	if (!xbox_remote) {
		dev_warn(&interface->dev, "%s - null device?\n", __func__);
		return;
	}

	usb_kill_urb(xbox_remote->irq_urb);
	rc_unregister_device(xbox_remote->rdev);
	usb_free_urb(xbox_remote->irq_urb);
	kfree(xbox_remote);
}

/* usb specific object to register with the usb subsystem */
static struct usb_driver xbox_remote_driver = {
	.name         = "xbox_remote",
	.probe        = xbox_remote_probe,
	.disconnect   = xbox_remote_disconnect,
	.id_table     = xbox_remote_table,
};

module_usb_driver(xbox_remote_driver);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
