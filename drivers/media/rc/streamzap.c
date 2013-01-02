/*
 * Streamzap Remote Control driver
 *
 * Copyright (c) 2005 Christoph Bartelmus <lirc@bartelmus.de>
 * Copyright (c) 2010 Jarod Wilson <jarod@wilsonet.com>
 *
 * This driver was based on the work of Greg Wickham and Adrian
 * Dewhurst. It was substantially rewritten to support correct signal
 * gaps and now maintains a delay buffer, which is used to present
 * consistent timing behaviour to user space applications. Without the
 * delay buffer an ugly hack would be required in lircd, which can
 * cause sluggish signal decoding in certain situations.
 *
 * Ported to in-kernel ir-core interface by Jarod Wilson
 *
 * This driver is based on the USB skeleton driver packaged with the
 * kernel; copyright (C) 2001-2003 Greg Kroah-Hartman (greg@kroah.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/usb/input.h>
#include <media/rc-core.h>

#define DRIVER_VERSION	"1.61"
#define DRIVER_NAME	"streamzap"
#define DRIVER_DESC	"Streamzap Remote Control driver"

#ifdef CONFIG_USB_DEBUG
static bool debug = 1;
#else
static bool debug;
#endif

#define USB_STREAMZAP_VENDOR_ID		0x0e9c
#define USB_STREAMZAP_PRODUCT_ID	0x0000

/* table of devices that work with this driver */
static struct usb_device_id streamzap_table[] = {
	/* Streamzap Remote Control */
	{ USB_DEVICE(USB_STREAMZAP_VENDOR_ID, USB_STREAMZAP_PRODUCT_ID) },
	/* Terminating entry */
	{ }
};

MODULE_DEVICE_TABLE(usb, streamzap_table);

#define SZ_PULSE_MASK 0xf0
#define SZ_SPACE_MASK 0x0f
#define SZ_TIMEOUT    0xff
#define SZ_RESOLUTION 256

/* number of samples buffered */
#define SZ_BUF_LEN 128

/* from ir-rc5-sz-decoder.c */
#ifdef CONFIG_IR_RC5_SZ_DECODER_MODULE
#define load_rc5_sz_decode()    request_module("ir-rc5-sz-decoder")
#else
#define load_rc5_sz_decode()    {}
#endif

enum StreamzapDecoderState {
	PulseSpace,
	FullPulse,
	FullSpace,
	IgnorePulse
};

/* structure to hold our device specific stuff */
struct streamzap_ir {
	/* ir-core */
	struct rc_dev *rdev;

	/* core device info */
	struct device *dev;

	/* usb */
	struct usb_device	*usbdev;
	struct usb_interface	*interface;
	struct usb_endpoint_descriptor *endpoint;
	struct urb		*urb_in;

	/* buffer & dma */
	unsigned char		*buf_in;
	dma_addr_t		dma_in;
	unsigned int		buf_in_len;

	/* track what state we're in */
	enum StreamzapDecoderState decoder_state;
	/* tracks whether we are currently receiving some signal */
	bool			idle;
	/* sum of signal lengths received since signal start */
	unsigned long		sum;
	/* start time of signal; necessary for gap tracking */
	struct timeval		signal_last;
	struct timeval		signal_start;
	bool			timeout_enabled;

	char			name[128];
	char			phys[64];
};


/* local function prototypes */
static int streamzap_probe(struct usb_interface *interface,
			   const struct usb_device_id *id);
static void streamzap_disconnect(struct usb_interface *interface);
static void streamzap_callback(struct urb *urb);
static int streamzap_suspend(struct usb_interface *intf, pm_message_t message);
static int streamzap_resume(struct usb_interface *intf);

/* usb specific object needed to register this driver with the usb subsystem */
static struct usb_driver streamzap_driver = {
	.name =		DRIVER_NAME,
	.probe =	streamzap_probe,
	.disconnect =	streamzap_disconnect,
	.suspend =	streamzap_suspend,
	.resume =	streamzap_resume,
	.id_table =	streamzap_table,
};

static void sz_push(struct streamzap_ir *sz, struct ir_raw_event rawir)
{
	dev_dbg(sz->dev, "Storing %s with duration %u us\n",
		(rawir.pulse ? "pulse" : "space"), rawir.duration);
	ir_raw_event_store_with_filter(sz->rdev, &rawir);
}

static void sz_push_full_pulse(struct streamzap_ir *sz,
			       unsigned char value)
{
	DEFINE_IR_RAW_EVENT(rawir);

	if (sz->idle) {
		long deltv;

		sz->signal_last = sz->signal_start;
		do_gettimeofday(&sz->signal_start);

		deltv = sz->signal_start.tv_sec - sz->signal_last.tv_sec;
		rawir.pulse = false;
		if (deltv > 15) {
			/* really long time */
			rawir.duration = IR_MAX_DURATION;
		} else {
			rawir.duration = (int)(deltv * 1000000 +
				sz->signal_start.tv_usec -
				sz->signal_last.tv_usec);
			rawir.duration -= sz->sum;
			rawir.duration = US_TO_NS(rawir.duration);
			rawir.duration &= IR_MAX_DURATION;
		}
		sz_push(sz, rawir);

		sz->idle = false;
		sz->sum = 0;
	}

	rawir.pulse = true;
	rawir.duration = ((int) value) * SZ_RESOLUTION;
	rawir.duration += SZ_RESOLUTION / 2;
	sz->sum += rawir.duration;
	rawir.duration = US_TO_NS(rawir.duration);
	rawir.duration &= IR_MAX_DURATION;
	sz_push(sz, rawir);
}

static void sz_push_half_pulse(struct streamzap_ir *sz,
			       unsigned char value)
{
	sz_push_full_pulse(sz, (value & SZ_PULSE_MASK) >> 4);
}

static void sz_push_full_space(struct streamzap_ir *sz,
			       unsigned char value)
{
	DEFINE_IR_RAW_EVENT(rawir);

	rawir.pulse = false;
	rawir.duration = ((int) value) * SZ_RESOLUTION;
	rawir.duration += SZ_RESOLUTION / 2;
	sz->sum += rawir.duration;
	rawir.duration = US_TO_NS(rawir.duration);
	sz_push(sz, rawir);
}

static void sz_push_half_space(struct streamzap_ir *sz,
			       unsigned long value)
{
	sz_push_full_space(sz, value & SZ_SPACE_MASK);
}

/**
 * streamzap_callback - usb IRQ handler callback
 *
 * This procedure is invoked on reception of data from
 * the usb remote.
 */
static void streamzap_callback(struct urb *urb)
{
	struct streamzap_ir *sz;
	unsigned int i;
	int len;

	if (!urb)
		return;

	sz = urb->context;
	len = urb->actual_length;

	switch (urb->status) {
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/*
		 * this urb is terminated, clean up.
		 * sz might already be invalid at this point
		 */
		dev_err(sz->dev, "urb terminated, status: %d\n", urb->status);
		return;
	default:
		break;
	}

	dev_dbg(sz->dev, "%s: received urb, len %d\n", __func__, len);
	for (i = 0; i < len; i++) {
		dev_dbg(sz->dev, "sz->buf_in[%d]: %x\n",
			i, (unsigned char)sz->buf_in[i]);
		switch (sz->decoder_state) {
		case PulseSpace:
			if ((sz->buf_in[i] & SZ_PULSE_MASK) ==
				SZ_PULSE_MASK) {
				sz->decoder_state = FullPulse;
				continue;
			} else if ((sz->buf_in[i] & SZ_SPACE_MASK)
					== SZ_SPACE_MASK) {
				sz_push_half_pulse(sz, sz->buf_in[i]);
				sz->decoder_state = FullSpace;
				continue;
			} else {
				sz_push_half_pulse(sz, sz->buf_in[i]);
				sz_push_half_space(sz, sz->buf_in[i]);
			}
			break;
		case FullPulse:
			sz_push_full_pulse(sz, sz->buf_in[i]);
			sz->decoder_state = IgnorePulse;
			break;
		case FullSpace:
			if (sz->buf_in[i] == SZ_TIMEOUT) {
				DEFINE_IR_RAW_EVENT(rawir);

				rawir.pulse = false;
				rawir.duration = sz->rdev->timeout;
				sz->idle = true;
				if (sz->timeout_enabled)
					sz_push(sz, rawir);
				ir_raw_event_handle(sz->rdev);
				ir_raw_event_reset(sz->rdev);
			} else {
				sz_push_full_space(sz, sz->buf_in[i]);
			}
			sz->decoder_state = PulseSpace;
			break;
		case IgnorePulse:
			if ((sz->buf_in[i] & SZ_SPACE_MASK) ==
				SZ_SPACE_MASK) {
				sz->decoder_state = FullSpace;
				continue;
			}
			sz_push_half_space(sz, sz->buf_in[i]);
			sz->decoder_state = PulseSpace;
			break;
		}
	}

	ir_raw_event_handle(sz->rdev);
	usb_submit_urb(urb, GFP_ATOMIC);

	return;
}

static struct rc_dev *streamzap_init_rc_dev(struct streamzap_ir *sz)
{
	struct rc_dev *rdev;
	struct device *dev = sz->dev;
	int ret;

	rdev = rc_allocate_device();
	if (!rdev) {
		dev_err(dev, "remote dev allocation failed\n");
		goto out;
	}

	snprintf(sz->name, sizeof(sz->name), "Streamzap PC Remote Infrared "
		 "Receiver (%04x:%04x)",
		 le16_to_cpu(sz->usbdev->descriptor.idVendor),
		 le16_to_cpu(sz->usbdev->descriptor.idProduct));
	usb_make_path(sz->usbdev, sz->phys, sizeof(sz->phys));
	strlcat(sz->phys, "/input0", sizeof(sz->phys));

	rdev->input_name = sz->name;
	rdev->input_phys = sz->phys;
	usb_to_input_id(sz->usbdev, &rdev->input_id);
	rdev->dev.parent = dev;
	rdev->priv = sz;
	rdev->driver_type = RC_DRIVER_IR_RAW;
	rdev->allowed_protos = RC_BIT_ALL;
	rdev->driver_name = DRIVER_NAME;
	rdev->map_name = RC_MAP_STREAMZAP;

	ret = rc_register_device(rdev);
	if (ret < 0) {
		dev_err(dev, "remote input device register failed\n");
		goto out;
	}

	return rdev;

out:
	rc_free_device(rdev);
	return NULL;
}

/**
 *	streamzap_probe
 *
 *	Called by usb-core to associated with a candidate device
 *	On any failure the return value is the ERROR
 *	On success return 0
 */
static int __devinit streamzap_probe(struct usb_interface *intf,
				     const struct usb_device_id *id)
{
	struct usb_device *usbdev = interface_to_usbdev(intf);
	struct usb_host_interface *iface_host;
	struct streamzap_ir *sz = NULL;
	char buf[63], name[128] = "";
	int retval = -ENOMEM;
	int pipe, maxp;

	/* Allocate space for device driver specific data */
	sz = kzalloc(sizeof(struct streamzap_ir), GFP_KERNEL);
	if (!sz)
		return -ENOMEM;

	sz->usbdev = usbdev;
	sz->interface = intf;

	/* Check to ensure endpoint information matches requirements */
	iface_host = intf->cur_altsetting;

	if (iface_host->desc.bNumEndpoints != 1) {
		dev_err(&intf->dev, "%s: Unexpected desc.bNumEndpoints (%d)\n",
			__func__, iface_host->desc.bNumEndpoints);
		retval = -ENODEV;
		goto free_sz;
	}

	sz->endpoint = &(iface_host->endpoint[0].desc);
	if ((sz->endpoint->bEndpointAddress & USB_ENDPOINT_DIR_MASK)
	    != USB_DIR_IN) {
		dev_err(&intf->dev, "%s: endpoint doesn't match input device "
			"02%02x\n", __func__, sz->endpoint->bEndpointAddress);
		retval = -ENODEV;
		goto free_sz;
	}

	if ((sz->endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
	    != USB_ENDPOINT_XFER_INT) {
		dev_err(&intf->dev, "%s: endpoint attributes don't match xfer "
			"02%02x\n", __func__, sz->endpoint->bmAttributes);
		retval = -ENODEV;
		goto free_sz;
	}

	pipe = usb_rcvintpipe(usbdev, sz->endpoint->bEndpointAddress);
	maxp = usb_maxpacket(usbdev, pipe, usb_pipeout(pipe));

	if (maxp == 0) {
		dev_err(&intf->dev, "%s: endpoint Max Packet Size is 0!?!\n",
			__func__);
		retval = -ENODEV;
		goto free_sz;
	}

	/* Allocate the USB buffer and IRQ URB */
	sz->buf_in = usb_alloc_coherent(usbdev, maxp, GFP_ATOMIC, &sz->dma_in);
	if (!sz->buf_in)
		goto free_sz;

	sz->urb_in = usb_alloc_urb(0, GFP_KERNEL);
	if (!sz->urb_in)
		goto free_buf_in;

	sz->dev = &intf->dev;
	sz->buf_in_len = maxp;

	if (usbdev->descriptor.iManufacturer
	    && usb_string(usbdev, usbdev->descriptor.iManufacturer,
			  buf, sizeof(buf)) > 0)
		strlcpy(name, buf, sizeof(name));

	if (usbdev->descriptor.iProduct
	    && usb_string(usbdev, usbdev->descriptor.iProduct,
			  buf, sizeof(buf)) > 0)
		snprintf(name + strlen(name), sizeof(name) - strlen(name),
			 " %s", buf);

	sz->rdev = streamzap_init_rc_dev(sz);
	if (!sz->rdev)
		goto rc_dev_fail;

	sz->idle = true;
	sz->decoder_state = PulseSpace;
	/* FIXME: don't yet have a way to set this */
	sz->timeout_enabled = true;
	sz->rdev->timeout = ((US_TO_NS(SZ_TIMEOUT * SZ_RESOLUTION) &
				IR_MAX_DURATION) | 0x03000000);
	#if 0
	/* not yet supported, depends on patches from maxim */
	/* see also: LIRC_GET_REC_RESOLUTION and LIRC_SET_REC_TIMEOUT */
	sz->min_timeout = US_TO_NS(SZ_TIMEOUT * SZ_RESOLUTION);
	sz->max_timeout = US_TO_NS(SZ_TIMEOUT * SZ_RESOLUTION);
	#endif

	do_gettimeofday(&sz->signal_start);

	/* Complete final initialisations */
	usb_fill_int_urb(sz->urb_in, usbdev, pipe, sz->buf_in,
			 maxp, (usb_complete_t)streamzap_callback,
			 sz, sz->endpoint->bInterval);
	sz->urb_in->transfer_dma = sz->dma_in;
	sz->urb_in->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	usb_set_intfdata(intf, sz);

	if (usb_submit_urb(sz->urb_in, GFP_ATOMIC))
		dev_err(sz->dev, "urb submit failed\n");

	dev_info(sz->dev, "Registered %s on usb%d:%d\n", name,
		 usbdev->bus->busnum, usbdev->devnum);

	/* Load the streamzap not-quite-rc5 decoder too */
	load_rc5_sz_decode();

	return 0;

rc_dev_fail:
	usb_free_urb(sz->urb_in);
free_buf_in:
	usb_free_coherent(usbdev, maxp, sz->buf_in, sz->dma_in);
free_sz:
	kfree(sz);

	return retval;
}

/**
 * streamzap_disconnect
 *
 * Called by the usb core when the device is removed from the system.
 *
 * This routine guarantees that the driver will not submit any more urbs
 * by clearing dev->usbdev.  It is also supposed to terminate any currently
 * active urbs.  Unfortunately, usb_bulk_msg(), used in streamzap_read(),
 * does not provide any way to do this.
 */
static void streamzap_disconnect(struct usb_interface *interface)
{
	struct streamzap_ir *sz = usb_get_intfdata(interface);
	struct usb_device *usbdev = interface_to_usbdev(interface);

	usb_set_intfdata(interface, NULL);

	if (!sz)
		return;

	sz->usbdev = NULL;
	rc_unregister_device(sz->rdev);
	usb_kill_urb(sz->urb_in);
	usb_free_urb(sz->urb_in);
	usb_free_coherent(usbdev, sz->buf_in_len, sz->buf_in, sz->dma_in);

	kfree(sz);
}

static int streamzap_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct streamzap_ir *sz = usb_get_intfdata(intf);

	usb_kill_urb(sz->urb_in);

	return 0;
}

static int streamzap_resume(struct usb_interface *intf)
{
	struct streamzap_ir *sz = usb_get_intfdata(intf);

	if (usb_submit_urb(sz->urb_in, GFP_ATOMIC)) {
		dev_err(sz->dev, "Error sumbiting urb\n");
		return -EIO;
	}

	return 0;
}

module_usb_driver(streamzap_driver);

MODULE_AUTHOR("Jarod Wilson <jarod@wilsonet.com>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Enable debugging messages");
