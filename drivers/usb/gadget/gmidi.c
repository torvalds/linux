/*
 * gmidi.c -- USB MIDI Gadget Driver
 *
 * Copyright (C) 2006 Thumtronics Pty Ltd.
 * Developed for Thumtronics by Grey Innovation
 * Ben Williamson <ben.williamson@greyinnovation.com>
 *
 * This software is distributed under the terms of the GNU General Public
 * License ("GPL") version 2, as published by the Free Software Foundation.
 *
 * This code is based in part on:
 *
 * Gadget Zero driver, Copyright (C) 2003-2004 David Brownell.
 * USB Audio driver, Copyright (C) 2002 by Takashi Iwai.
 * USB MIDI driver, Copyright (C) 2002-2005 Clemens Ladisch.
 *
 * Refer to the USB Device Class Definition for MIDI Devices:
 * http://www.usb.org/developers/devclass_docs/midi10.pdf
 */

#define DEBUG 1
// #define VERBOSE

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/utsname.h>
#include <linux/device.h>
#include <linux/moduleparam.h>

#include <sound/driver.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/rawmidi.h>

#include <linux/usb/ch9.h>
#include <linux/usb_gadget.h>
#include <linux/usb/audio.h>
#include <linux/usb/midi.h>

#include "gadget_chips.h"

MODULE_AUTHOR("Ben Williamson");
MODULE_LICENSE("GPL v2");

#define DRIVER_VERSION "25 Jul 2006"

static const char shortname[] = "g_midi";
static const char longname[] = "MIDI Gadget";

static int index = SNDRV_DEFAULT_IDX1;
static char *id = SNDRV_DEFAULT_STR1;

module_param(index, int, 0444);
MODULE_PARM_DESC(index, "Index value for the USB MIDI Gadget adapter.");
module_param(id, charp, 0444);
MODULE_PARM_DESC(id, "ID string for the USB MIDI Gadget adapter.");

/* Some systems will want different product identifers published in the
 * device descriptor, either numbers or strings or both.  These string
 * parameters are in UTF-8 (superset of ASCII's 7 bit characters).
 */

static ushort idVendor;
module_param(idVendor, ushort, S_IRUGO);
MODULE_PARM_DESC(idVendor, "USB Vendor ID");

static ushort idProduct;
module_param(idProduct, ushort, S_IRUGO);
MODULE_PARM_DESC(idProduct, "USB Product ID");

static ushort bcdDevice;
module_param(bcdDevice, ushort, S_IRUGO);
MODULE_PARM_DESC(bcdDevice, "USB Device version (BCD)");

static char *iManufacturer;
module_param(iManufacturer, charp, S_IRUGO);
MODULE_PARM_DESC(iManufacturer, "USB Manufacturer string");

static char *iProduct;
module_param(iProduct, charp, S_IRUGO);
MODULE_PARM_DESC(iProduct, "USB Product string");

static char *iSerialNumber;
module_param(iSerialNumber, charp, S_IRUGO);
MODULE_PARM_DESC(iSerialNumber, "SerialNumber");

/*
 * this version autoconfigures as much as possible,
 * which is reasonable for most "bulk-only" drivers.
 */
static const char *EP_IN_NAME;
static const char *EP_OUT_NAME;


/* big enough to hold our biggest descriptor */
#define USB_BUFSIZ 256


/* This is a gadget, and the IN/OUT naming is from the host's perspective.
   USB -> OUT endpoint -> rawmidi
   USB <- IN endpoint  <- rawmidi */
struct gmidi_in_port {
	struct gmidi_device* dev;
	int active;
	uint8_t cable;		/* cable number << 4 */
	uint8_t state;
#define STATE_UNKNOWN	0
#define STATE_1PARAM	1
#define STATE_2PARAM_1	2
#define STATE_2PARAM_2	3
#define STATE_SYSEX_0	4
#define STATE_SYSEX_1	5
#define STATE_SYSEX_2	6
	uint8_t data[2];
};

struct gmidi_device {
	spinlock_t		lock;
	struct usb_gadget	*gadget;
	struct usb_request	*req;		/* for control responses */
	u8			config;
	struct usb_ep		*in_ep, *out_ep;
	struct snd_card		*card;
	struct snd_rawmidi	*rmidi;
	struct snd_rawmidi_substream *in_substream;
	struct snd_rawmidi_substream *out_substream;

	/* For the moment we only support one port in
	   each direction, but in_port is kept as a
	   separate struct so we can have more later. */
	struct gmidi_in_port	in_port;
	unsigned long		out_triggered;
	struct tasklet_struct	tasklet;
};

static void gmidi_transmit(struct gmidi_device* dev, struct usb_request* req);


#define xprintk(d,level,fmt,args...) \
	dev_printk(level , &(d)->gadget->dev , fmt , ## args)

#ifdef DEBUG
#define DBG(dev,fmt,args...) \
	xprintk(dev , KERN_DEBUG , fmt , ## args)
#else
#define DBG(dev,fmt,args...) \
	do { } while (0)
#endif /* DEBUG */

#ifdef VERBOSE
#define VDBG	DBG
#else
#define VDBG(dev,fmt,args...) \
	do { } while (0)
#endif /* VERBOSE */

#define ERROR(dev,fmt,args...) \
	xprintk(dev , KERN_ERR , fmt , ## args)
#define WARN(dev,fmt,args...) \
	xprintk(dev , KERN_WARNING , fmt , ## args)
#define INFO(dev,fmt,args...) \
	xprintk(dev , KERN_INFO , fmt , ## args)


static unsigned buflen = 256;
static unsigned qlen = 32;

module_param(buflen, uint, S_IRUGO);
module_param(qlen, uint, S_IRUGO);


/* Thanks to Grey Innovation for donating this product ID.
 *
 * DO NOT REUSE THESE IDs with a protocol-incompatible driver!!  Ever!!
 * Instead:  allocate your own, using normal USB-IF procedures.
 */
#define DRIVER_VENDOR_NUM	0x17b3		/* Grey Innovation */
#define DRIVER_PRODUCT_NUM	0x0004		/* Linux-USB "MIDI Gadget" */


/*
 * DESCRIPTORS ... most are static, but strings and (full)
 * configuration descriptors are built on demand.
 */

#define STRING_MANUFACTURER	25
#define STRING_PRODUCT		42
#define STRING_SERIAL		101
#define STRING_MIDI_GADGET	250

/* We only have the one configuration, it's number 1. */
#define	GMIDI_CONFIG		1

/* We have two interfaces- AudioControl and MIDIStreaming */
#define GMIDI_AC_INTERFACE	0
#define GMIDI_MS_INTERFACE	1
#define GMIDI_NUM_INTERFACES	2

DECLARE_USB_AC_HEADER_DESCRIPTOR(1);
DECLARE_USB_MIDI_OUT_JACK_DESCRIPTOR(1);
DECLARE_USB_MS_ENDPOINT_DESCRIPTOR(1);

/* B.1  Device Descriptor */
static struct usb_device_descriptor device_desc = {
	.bLength =		USB_DT_DEVICE_SIZE,
	.bDescriptorType =	USB_DT_DEVICE,
	.bcdUSB =		__constant_cpu_to_le16(0x0200),
	.bDeviceClass =		USB_CLASS_PER_INTERFACE,
	.idVendor =		__constant_cpu_to_le16(DRIVER_VENDOR_NUM),
	.idProduct =		__constant_cpu_to_le16(DRIVER_PRODUCT_NUM),
	.iManufacturer =	STRING_MANUFACTURER,
	.iProduct =		STRING_PRODUCT,
	.bNumConfigurations =	1,
};

/* B.2  Configuration Descriptor */
static struct usb_config_descriptor config_desc = {
	.bLength =		USB_DT_CONFIG_SIZE,
	.bDescriptorType =	USB_DT_CONFIG,
	/* compute wTotalLength on the fly */
	.bNumInterfaces =	GMIDI_NUM_INTERFACES,
	.bConfigurationValue =	GMIDI_CONFIG,
	.iConfiguration =	STRING_MIDI_GADGET,
	/*
	 * FIXME: When embedding this driver in a device,
	 * these need to be set to reflect the actual
	 * power properties of the device. Is it selfpowered?
	 */
	.bmAttributes =		USB_CONFIG_ATT_ONE,
	.bMaxPower =		1,
};

/* B.3.1  Standard AC Interface Descriptor */
static const struct usb_interface_descriptor ac_interface_desc = {
	.bLength =		USB_DT_INTERFACE_SIZE,
	.bDescriptorType =	USB_DT_INTERFACE,
	.bInterfaceNumber =	GMIDI_AC_INTERFACE,
	.bNumEndpoints =	0,
	.bInterfaceClass =	USB_CLASS_AUDIO,
	.bInterfaceSubClass =	USB_SUBCLASS_AUDIOCONTROL,
	.iInterface =		STRING_MIDI_GADGET,
};

/* B.3.2  Class-Specific AC Interface Descriptor */
static const struct usb_ac_header_descriptor_1 ac_header_desc = {
	.bLength =		USB_DT_AC_HEADER_SIZE(1),
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubtype =	USB_MS_HEADER,
	.bcdADC =		__constant_cpu_to_le16(0x0100),
	.wTotalLength =		USB_DT_AC_HEADER_SIZE(1),
	.bInCollection =	1,
	.baInterfaceNr = {
		[0] =		GMIDI_MS_INTERFACE,
	}
};

/* B.4.1  Standard MS Interface Descriptor */
static const struct usb_interface_descriptor ms_interface_desc = {
	.bLength =		USB_DT_INTERFACE_SIZE,
	.bDescriptorType =	USB_DT_INTERFACE,
	.bInterfaceNumber =	GMIDI_MS_INTERFACE,
	.bNumEndpoints =	2,
	.bInterfaceClass =	USB_CLASS_AUDIO,
	.bInterfaceSubClass =	USB_SUBCLASS_MIDISTREAMING,
	.iInterface =		STRING_MIDI_GADGET,
};

/* B.4.2  Class-Specific MS Interface Descriptor */
static const struct usb_ms_header_descriptor ms_header_desc = {
	.bLength =		USB_DT_MS_HEADER_SIZE,
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubtype =	USB_MS_HEADER,
	.bcdMSC =		__constant_cpu_to_le16(0x0100),
	.wTotalLength =		USB_DT_MS_HEADER_SIZE
				+ 2*USB_DT_MIDI_IN_SIZE
				+ 2*USB_DT_MIDI_OUT_SIZE(1),
};

#define JACK_IN_EMB	1
#define JACK_IN_EXT	2
#define JACK_OUT_EMB	3
#define JACK_OUT_EXT	4

/* B.4.3  MIDI IN Jack Descriptors */
static const struct usb_midi_in_jack_descriptor jack_in_emb_desc = {
	.bLength =		USB_DT_MIDI_IN_SIZE,
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubtype =	USB_MS_MIDI_IN_JACK,
	.bJackType =		USB_MS_EMBEDDED,
	.bJackID =		JACK_IN_EMB,
};

static const struct usb_midi_in_jack_descriptor jack_in_ext_desc = {
	.bLength =		USB_DT_MIDI_IN_SIZE,
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubtype =	USB_MS_MIDI_IN_JACK,
	.bJackType =		USB_MS_EXTERNAL,
	.bJackID =		JACK_IN_EXT,
};

/* B.4.4  MIDI OUT Jack Descriptors */
static const struct usb_midi_out_jack_descriptor_1 jack_out_emb_desc = {
	.bLength =		USB_DT_MIDI_OUT_SIZE(1),
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubtype =	USB_MS_MIDI_OUT_JACK,
	.bJackType =		USB_MS_EMBEDDED,
	.bJackID =		JACK_OUT_EMB,
	.bNrInputPins =		1,
	.pins = {
		[0] = {
			.baSourceID =	JACK_IN_EXT,
			.baSourcePin =	1,
		}
	}
};

static const struct usb_midi_out_jack_descriptor_1 jack_out_ext_desc = {
	.bLength =		USB_DT_MIDI_OUT_SIZE(1),
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubtype =	USB_MS_MIDI_OUT_JACK,
	.bJackType =		USB_MS_EXTERNAL,
	.bJackID =		JACK_OUT_EXT,
	.bNrInputPins =		1,
	.pins = {
		[0] = {
			.baSourceID =	JACK_IN_EMB,
			.baSourcePin =	1,
		}
	}
};

/* B.5.1  Standard Bulk OUT Endpoint Descriptor */
static struct usb_endpoint_descriptor bulk_out_desc = {
	.bLength =		USB_DT_ENDPOINT_AUDIO_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_OUT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
};

/* B.5.2  Class-specific MS Bulk OUT Endpoint Descriptor */
static const struct usb_ms_endpoint_descriptor_1 ms_out_desc = {
	.bLength =		USB_DT_MS_ENDPOINT_SIZE(1),
	.bDescriptorType =	USB_DT_CS_ENDPOINT,
	.bDescriptorSubtype =	USB_MS_GENERAL,
	.bNumEmbMIDIJack =	1,
	.baAssocJackID = {
		[0] =		JACK_IN_EMB,
	}
};

/* B.6.1  Standard Bulk IN Endpoint Descriptor */
static struct usb_endpoint_descriptor bulk_in_desc = {
	.bLength =		USB_DT_ENDPOINT_AUDIO_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
};

/* B.6.2  Class-specific MS Bulk IN Endpoint Descriptor */
static const struct usb_ms_endpoint_descriptor_1 ms_in_desc = {
	.bLength =		USB_DT_MS_ENDPOINT_SIZE(1),
	.bDescriptorType =	USB_DT_CS_ENDPOINT,
	.bDescriptorSubtype =	USB_MS_GENERAL,
	.bNumEmbMIDIJack =	1,
	.baAssocJackID = {
		[0] =		JACK_OUT_EMB,
	}
};

static const struct usb_descriptor_header *gmidi_function [] = {
	(struct usb_descriptor_header *)&ac_interface_desc,
	(struct usb_descriptor_header *)&ac_header_desc,
	(struct usb_descriptor_header *)&ms_interface_desc,

	(struct usb_descriptor_header *)&ms_header_desc,
	(struct usb_descriptor_header *)&jack_in_emb_desc,
	(struct usb_descriptor_header *)&jack_in_ext_desc,
	(struct usb_descriptor_header *)&jack_out_emb_desc,
	(struct usb_descriptor_header *)&jack_out_ext_desc,
	/* If you add more jacks, update ms_header_desc.wTotalLength */

	(struct usb_descriptor_header *)&bulk_out_desc,
	(struct usb_descriptor_header *)&ms_out_desc,
	(struct usb_descriptor_header *)&bulk_in_desc,
	(struct usb_descriptor_header *)&ms_in_desc,
	NULL,
};

static char manufacturer[50];
static char product_desc[40] = "MIDI Gadget";
static char serial_number[20];

/* static strings, in UTF-8 */
static struct usb_string strings [] = {
	{ STRING_MANUFACTURER, manufacturer, },
	{ STRING_PRODUCT, product_desc, },
	{ STRING_SERIAL, serial_number, },
	{ STRING_MIDI_GADGET, longname, },
	{  }			/* end of list */
};

static struct usb_gadget_strings stringtab = {
	.language	= 0x0409,	/* en-us */
	.strings	= strings,
};

static int config_buf(struct usb_gadget *gadget,
		u8 *buf, u8 type, unsigned index)
{
	int len;

	/* only one configuration */
	if (index != 0) {
		return -EINVAL;
	}
	len = usb_gadget_config_buf(&config_desc,
			buf, USB_BUFSIZ, gmidi_function);
	if (len < 0) {
		return len;
	}
	((struct usb_config_descriptor *)buf)->bDescriptorType = type;
	return len;
}

static struct usb_request* alloc_ep_req(struct usb_ep *ep, unsigned length)
{
	struct usb_request	*req;

	req = usb_ep_alloc_request(ep, GFP_ATOMIC);
	if (req) {
		req->length = length;
		req->buf = kmalloc(length, GFP_ATOMIC);
		if (!req->buf) {
			usb_ep_free_request(ep, req);
			req = NULL;
		}
	}
	return req;
}

static void free_ep_req(struct usb_ep *ep, struct usb_request *req)
{
	kfree(req->buf);
	usb_ep_free_request(ep, req);
}

static const uint8_t gmidi_cin_length[] = {
	0, 0, 2, 3, 3, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3, 1
};

/*
 * Receives a chunk of MIDI data.
 */
static void gmidi_read_data(struct usb_ep *ep, int cable,
				   uint8_t* data, int length)
{
	struct gmidi_device *dev = ep->driver_data;
	/* cable is ignored, because for now we only have one. */

	if (!dev->out_substream) {
		/* Nobody is listening - throw it on the floor. */
		return;
	}
	if (!test_bit(dev->out_substream->number, &dev->out_triggered)) {
		return;
	}
	snd_rawmidi_receive(dev->out_substream, data, length);
}

static void gmidi_handle_out_data(struct usb_ep *ep, struct usb_request *req)
{
	unsigned i;
	u8 *buf = req->buf;

	for (i = 0; i + 3 < req->actual; i += 4) {
		if (buf[i] != 0) {
			int cable = buf[i] >> 4;
			int length = gmidi_cin_length[buf[i] & 0x0f];
			gmidi_read_data(ep, cable, &buf[i + 1], length);
		}
	}
}

static void gmidi_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct gmidi_device *dev = ep->driver_data;
	int status = req->status;

	switch (status) {
	case 0:				/* normal completion */
		if (ep == dev->out_ep) {
			/* we received stuff.
			   req is queued again, below */
			gmidi_handle_out_data(ep, req);
		} else if (ep == dev->in_ep) {
			/* our transmit completed.
			   see if there's more to go.
			   gmidi_transmit eats req, don't queue it again. */
			gmidi_transmit(dev, req);
			return;
		}
		break;

	/* this endpoint is normally active while we're configured */
	case -ECONNABORTED:		/* hardware forced ep reset */
	case -ECONNRESET:		/* request dequeued */
	case -ESHUTDOWN:		/* disconnect from host */
		VDBG(dev, "%s gone (%d), %d/%d\n", ep->name, status,
				req->actual, req->length);
		if (ep == dev->out_ep) {
			gmidi_handle_out_data(ep, req);
		}
		free_ep_req(ep, req);
		return;

	case -EOVERFLOW:		/* buffer overrun on read means that
					 * we didn't provide a big enough
					 * buffer.
					 */
	default:
		DBG(dev, "%s complete --> %d, %d/%d\n", ep->name,
				status, req->actual, req->length);
		break;
	case -EREMOTEIO:		/* short read */
		break;
	}

	status = usb_ep_queue(ep, req, GFP_ATOMIC);
	if (status) {
		ERROR(dev, "kill %s:  resubmit %d bytes --> %d\n",
				ep->name, req->length, status);
		usb_ep_set_halt(ep);
		/* FIXME recover later ... somehow */
	}
}

static int set_gmidi_config(struct gmidi_device *dev, gfp_t gfp_flags)
{
	int err = 0;
	struct usb_request *req;
	struct usb_ep* ep;
	unsigned i;

	err = usb_ep_enable(dev->in_ep, &bulk_in_desc);
	if (err) {
		ERROR(dev, "can't start %s: %d\n", dev->in_ep->name, err);
		goto fail;
	}
	dev->in_ep->driver_data = dev;

	err = usb_ep_enable(dev->out_ep, &bulk_out_desc);
	if (err) {
		ERROR(dev, "can't start %s: %d\n", dev->out_ep->name, err);
		goto fail;
	}
	dev->out_ep->driver_data = dev;

	/* allocate a bunch of read buffers and queue them all at once. */
	ep = dev->out_ep;
	for (i = 0; i < qlen && err == 0; i++) {
		req = alloc_ep_req(ep, buflen);
		if (req) {
			req->complete = gmidi_complete;
			err = usb_ep_queue(ep, req, GFP_ATOMIC);
			if (err) {
				DBG(dev, "%s queue req: %d\n", ep->name, err);
			}
		} else {
			err = -ENOMEM;
		}
	}
fail:
	/* caller is responsible for cleanup on error */
	return err;
}


static void gmidi_reset_config(struct gmidi_device *dev)
{
	if (dev->config == 0) {
		return;
	}

	DBG(dev, "reset config\n");

	/* just disable endpoints, forcing completion of pending i/o.
	 * all our completion handlers free their requests in this case.
	 */
	usb_ep_disable(dev->in_ep);
	usb_ep_disable(dev->out_ep);
	dev->config = 0;
}

/* change our operational config.  this code must agree with the code
 * that returns config descriptors, and altsetting code.
 *
 * it's also responsible for power management interactions. some
 * configurations might not work with our current power sources.
 *
 * note that some device controller hardware will constrain what this
 * code can do, perhaps by disallowing more than one configuration or
 * by limiting configuration choices (like the pxa2xx).
 */
static int
gmidi_set_config(struct gmidi_device *dev, unsigned number, gfp_t gfp_flags)
{
	int result = 0;
	struct usb_gadget *gadget = dev->gadget;

#if 0
	/* FIXME */
	/* Hacking this bit out fixes a bug where on receipt of two
	   USB_REQ_SET_CONFIGURATION messages, we end up with no
	   buffered OUT requests waiting for data. This is clearly
	   hiding a bug elsewhere, because if the config didn't
	   change then we really shouldn't do anything. */
	/* Having said that, when we do "change" from config 1
	   to config 1, we at least gmidi_reset_config() which
	   clears out any requests on endpoints, so it's not like
	   we leak or anything. */
	if (number == dev->config) {
		return 0;
	}
#endif

	if (gadget_is_sa1100(gadget) && dev->config) {
		/* tx fifo is full, but we can't clear it...*/
		INFO(dev, "can't change configurations\n");
		return -ESPIPE;
	}
	gmidi_reset_config(dev);

	switch (number) {
	case GMIDI_CONFIG:
		result = set_gmidi_config(dev, gfp_flags);
		break;
	default:
		result = -EINVAL;
		/* FALL THROUGH */
	case 0:
		return result;
	}

	if (!result && (!dev->in_ep || !dev->out_ep)) {
		result = -ENODEV;
	}
	if (result) {
		gmidi_reset_config(dev);
	} else {
		char *speed;

		switch (gadget->speed) {
		case USB_SPEED_LOW:	speed = "low"; break;
		case USB_SPEED_FULL:	speed = "full"; break;
		case USB_SPEED_HIGH:	speed = "high"; break;
		default:		speed = "?"; break;
		}

		dev->config = number;
		INFO(dev, "%s speed\n", speed);
	}
	return result;
}


static void gmidi_setup_complete(struct usb_ep *ep, struct usb_request *req)
{
	if (req->status || req->actual != req->length) {
		DBG((struct gmidi_device *) ep->driver_data,
				"setup complete --> %d, %d/%d\n",
				req->status, req->actual, req->length);
	}
}

/*
 * The setup() callback implements all the ep0 functionality that's
 * not handled lower down, in hardware or the hardware driver (like
 * device and endpoint feature flags, and their status).  It's all
 * housekeeping for the gadget function we're implementing.  Most of
 * the work is in config-specific setup.
 */
static int gmidi_setup(struct usb_gadget *gadget,
			const struct usb_ctrlrequest *ctrl)
{
	struct gmidi_device *dev = get_gadget_data(gadget);
	struct usb_request *req = dev->req;
	int value = -EOPNOTSUPP;
	u16 w_index = le16_to_cpu(ctrl->wIndex);
	u16 w_value = le16_to_cpu(ctrl->wValue);
	u16 w_length = le16_to_cpu(ctrl->wLength);

	/* usually this stores reply data in the pre-allocated ep0 buffer,
	 * but config change events will reconfigure hardware.
	 */
	req->zero = 0;
	switch (ctrl->bRequest) {

	case USB_REQ_GET_DESCRIPTOR:
		if (ctrl->bRequestType != USB_DIR_IN) {
			goto unknown;
		}
		switch (w_value >> 8) {

		case USB_DT_DEVICE:
			value = min(w_length, (u16) sizeof(device_desc));
			memcpy(req->buf, &device_desc, value);
			break;
		case USB_DT_CONFIG:
			value = config_buf(gadget, req->buf,
					w_value >> 8,
					w_value & 0xff);
			if (value >= 0) {
				value = min(w_length, (u16)value);
			}
			break;

		case USB_DT_STRING:
			/* wIndex == language code.
			 * this driver only handles one language, you can
			 * add string tables for other languages, using
			 * any UTF-8 characters
			 */
			value = usb_gadget_get_string(&stringtab,
					w_value & 0xff, req->buf);
			if (value >= 0) {
				value = min(w_length, (u16)value);
			}
			break;
		}
		break;

	/* currently two configs, two speeds */
	case USB_REQ_SET_CONFIGURATION:
		if (ctrl->bRequestType != 0) {
			goto unknown;
		}
		if (gadget->a_hnp_support) {
			DBG(dev, "HNP available\n");
		} else if (gadget->a_alt_hnp_support) {
			DBG(dev, "HNP needs a different root port\n");
		} else {
			VDBG(dev, "HNP inactive\n");
		}
		spin_lock(&dev->lock);
		value = gmidi_set_config(dev, w_value, GFP_ATOMIC);
		spin_unlock(&dev->lock);
		break;
	case USB_REQ_GET_CONFIGURATION:
		if (ctrl->bRequestType != USB_DIR_IN) {
			goto unknown;
		}
		*(u8 *)req->buf = dev->config;
		value = min(w_length, (u16)1);
		break;

	/* until we add altsetting support, or other interfaces,
	 * only 0/0 are possible.  pxa2xx only supports 0/0 (poorly)
	 * and already killed pending endpoint I/O.
	 */
	case USB_REQ_SET_INTERFACE:
		if (ctrl->bRequestType != USB_RECIP_INTERFACE) {
			goto unknown;
		}
		spin_lock(&dev->lock);
		if (dev->config && w_index < GMIDI_NUM_INTERFACES
			&& w_value == 0)
		{
			u8 config = dev->config;

			/* resets interface configuration, forgets about
			 * previous transaction state (queued bufs, etc)
			 * and re-inits endpoint state (toggle etc)
			 * no response queued, just zero status == success.
			 * if we had more than one interface we couldn't
			 * use this "reset the config" shortcut.
			 */
			gmidi_reset_config(dev);
			gmidi_set_config(dev, config, GFP_ATOMIC);
			value = 0;
		}
		spin_unlock(&dev->lock);
		break;
	case USB_REQ_GET_INTERFACE:
		if (ctrl->bRequestType != (USB_DIR_IN|USB_RECIP_INTERFACE)) {
			goto unknown;
		}
		if (!dev->config) {
			break;
		}
		if (w_index >= GMIDI_NUM_INTERFACES) {
			value = -EDOM;
			break;
		}
		*(u8 *)req->buf = 0;
		value = min(w_length, (u16)1);
		break;

	default:
unknown:
		VDBG(dev, "unknown control req%02x.%02x v%04x i%04x l%d\n",
			ctrl->bRequestType, ctrl->bRequest,
			w_value, w_index, w_length);
	}

	/* respond with data transfer before status phase? */
	if (value >= 0) {
		req->length = value;
		req->zero = value < w_length;
		value = usb_ep_queue(gadget->ep0, req, GFP_ATOMIC);
		if (value < 0) {
			DBG(dev, "ep_queue --> %d\n", value);
			req->status = 0;
			gmidi_setup_complete(gadget->ep0, req);
		}
	}

	/* device either stalls (value < 0) or reports success */
	return value;
}

static void gmidi_disconnect(struct usb_gadget *gadget)
{
	struct gmidi_device *dev = get_gadget_data(gadget);
	unsigned long flags;

	spin_lock_irqsave(&dev->lock, flags);
	gmidi_reset_config(dev);

	/* a more significant application might have some non-usb
	 * activities to quiesce here, saving resources like power
	 * or pushing the notification up a network stack.
	 */
	spin_unlock_irqrestore(&dev->lock, flags);

	/* next we may get setup() calls to enumerate new connections;
	 * or an unbind() during shutdown (including removing module).
	 */
}

static void /* __init_or_exit */ gmidi_unbind(struct usb_gadget *gadget)
{
	struct gmidi_device *dev = get_gadget_data(gadget);
	struct snd_card* card;

	DBG(dev, "unbind\n");

	card = dev->card;
	dev->card = NULL;
	if (card) {
		snd_card_free(card);
	}

	/* we've already been disconnected ... no i/o is active */
	if (dev->req) {
		dev->req->length = USB_BUFSIZ;
		free_ep_req(gadget->ep0, dev->req);
	}
	kfree(dev);
	set_gadget_data(gadget, NULL);
}

static int gmidi_snd_free(struct snd_device *device)
{
	return 0;
}

static void gmidi_transmit_packet(struct usb_request* req, uint8_t p0,
					uint8_t p1, uint8_t p2, uint8_t p3)
{
	unsigned length = req->length;

	uint8_t* buf = (uint8_t*)req->buf + length;
	buf[0] = p0;
	buf[1] = p1;
	buf[2] = p2;
	buf[3] = p3;
	req->length = length + 4;
}

/*
 * Converts MIDI commands to USB MIDI packets.
 */
static void gmidi_transmit_byte(struct usb_request* req,
				struct gmidi_in_port* port, uint8_t b)
{
	uint8_t p0 = port->cable;

	if (b >= 0xf8) {
		gmidi_transmit_packet(req, p0 | 0x0f, b, 0, 0);
	} else if (b >= 0xf0) {
		switch (b) {
		case 0xf0:
			port->data[0] = b;
			port->state = STATE_SYSEX_1;
			break;
		case 0xf1:
		case 0xf3:
			port->data[0] = b;
			port->state = STATE_1PARAM;
			break;
		case 0xf2:
			port->data[0] = b;
			port->state = STATE_2PARAM_1;
			break;
		case 0xf4:
		case 0xf5:
			port->state = STATE_UNKNOWN;
			break;
		case 0xf6:
			gmidi_transmit_packet(req, p0 | 0x05, 0xf6, 0, 0);
			port->state = STATE_UNKNOWN;
			break;
		case 0xf7:
			switch (port->state) {
			case STATE_SYSEX_0:
				gmidi_transmit_packet(req,
					p0 | 0x05, 0xf7, 0, 0);
				break;
			case STATE_SYSEX_1:
				gmidi_transmit_packet(req,
					p0 | 0x06, port->data[0], 0xf7, 0);
				break;
			case STATE_SYSEX_2:
				gmidi_transmit_packet(req,
					p0 | 0x07, port->data[0],
					port->data[1], 0xf7);
				break;
			}
			port->state = STATE_UNKNOWN;
			break;
		}
	} else if (b >= 0x80) {
		port->data[0] = b;
		if (b >= 0xc0 && b <= 0xdf)
			port->state = STATE_1PARAM;
		else
			port->state = STATE_2PARAM_1;
	} else { /* b < 0x80 */
		switch (port->state) {
		case STATE_1PARAM:
			if (port->data[0] < 0xf0) {
				p0 |= port->data[0] >> 4;
			} else {
				p0 |= 0x02;
				port->state = STATE_UNKNOWN;
			}
			gmidi_transmit_packet(req, p0, port->data[0], b, 0);
			break;
		case STATE_2PARAM_1:
			port->data[1] = b;
			port->state = STATE_2PARAM_2;
			break;
		case STATE_2PARAM_2:
			if (port->data[0] < 0xf0) {
				p0 |= port->data[0] >> 4;
				port->state = STATE_2PARAM_1;
			} else {
				p0 |= 0x03;
				port->state = STATE_UNKNOWN;
			}
			gmidi_transmit_packet(req,
				p0, port->data[0], port->data[1], b);
			break;
		case STATE_SYSEX_0:
			port->data[0] = b;
			port->state = STATE_SYSEX_1;
			break;
		case STATE_SYSEX_1:
			port->data[1] = b;
			port->state = STATE_SYSEX_2;
			break;
		case STATE_SYSEX_2:
			gmidi_transmit_packet(req,
				p0 | 0x04, port->data[0], port->data[1], b);
			port->state = STATE_SYSEX_0;
			break;
		}
	}
}

static void gmidi_transmit(struct gmidi_device* dev, struct usb_request* req)
{
	struct usb_ep* ep = dev->in_ep;
	struct gmidi_in_port* port = &dev->in_port;

	if (!ep) {
		return;
	}
	if (!req) {
		req = alloc_ep_req(ep, buflen);
	}
	if (!req) {
		ERROR(dev, "gmidi_transmit: alloc_ep_request failed\n");
		return;
	}
	req->length = 0;
	req->complete = gmidi_complete;

	if (port->active) {
		while (req->length + 3 < buflen) {
			uint8_t b;
			if (snd_rawmidi_transmit(dev->in_substream, &b, 1)
				!= 1)
			{
				port->active = 0;
				break;
			}
			gmidi_transmit_byte(req, port, b);
		}
	}
	if (req->length > 0) {
		usb_ep_queue(ep, req, GFP_ATOMIC);
	} else {
		free_ep_req(ep, req);
	}
}

static void gmidi_in_tasklet(unsigned long data)
{
	struct gmidi_device* dev = (struct gmidi_device*)data;

	gmidi_transmit(dev, NULL);
}

static int gmidi_in_open(struct snd_rawmidi_substream *substream)
{
	struct gmidi_device* dev = substream->rmidi->private_data;

	VDBG(dev, "gmidi_in_open\n");
	dev->in_substream = substream;
	dev->in_port.state = STATE_UNKNOWN;
	return 0;
}

static int gmidi_in_close(struct snd_rawmidi_substream *substream)
{
	VDBG(dev, "gmidi_in_close\n");
	return 0;
}

static void gmidi_in_trigger(struct snd_rawmidi_substream *substream, int up)
{
	struct gmidi_device* dev = substream->rmidi->private_data;

	VDBG(dev, "gmidi_in_trigger %d\n", up);
	dev->in_port.active = up;
	if (up) {
		tasklet_hi_schedule(&dev->tasklet);
	}
}

static int gmidi_out_open(struct snd_rawmidi_substream *substream)
{
	struct gmidi_device* dev = substream->rmidi->private_data;

	VDBG(dev, "gmidi_out_open\n");
	dev->out_substream = substream;
	return 0;
}

static int gmidi_out_close(struct snd_rawmidi_substream *substream)
{
	VDBG(dev, "gmidi_out_close\n");
	return 0;
}

static void gmidi_out_trigger(struct snd_rawmidi_substream *substream, int up)
{
	struct gmidi_device* dev = substream->rmidi->private_data;

	VDBG(dev, "gmidi_out_trigger %d\n", up);
	if (up) {
		set_bit(substream->number, &dev->out_triggered);
	} else {
		clear_bit(substream->number, &dev->out_triggered);
	}
}

static struct snd_rawmidi_ops gmidi_in_ops = {
	.open = gmidi_in_open,
	.close = gmidi_in_close,
	.trigger = gmidi_in_trigger,
};

static struct snd_rawmidi_ops gmidi_out_ops = {
	.open = gmidi_out_open,
	.close = gmidi_out_close,
	.trigger = gmidi_out_trigger
};

/* register as a sound "card" */
static int gmidi_register_card(struct gmidi_device *dev)
{
	struct snd_card *card;
	struct snd_rawmidi *rmidi;
	int err;
	int out_ports = 1;
	int in_ports = 1;
	static struct snd_device_ops ops = {
		.dev_free = gmidi_snd_free,
	};

	card = snd_card_new(index, id, THIS_MODULE, 0);
	if (!card) {
		ERROR(dev, "snd_card_new failed\n");
		err = -ENOMEM;
		goto fail;
	}
	dev->card = card;

	err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, dev, &ops);
	if (err < 0) {
		ERROR(dev, "snd_device_new failed: error %d\n", err);
		goto fail;
	}

	strcpy(card->driver, longname);
	strcpy(card->longname, longname);
	strcpy(card->shortname, shortname);

	/* Set up rawmidi */
	dev->in_port.dev = dev;
	dev->in_port.active = 0;
	snd_component_add(card, "MIDI");
	err = snd_rawmidi_new(card, "USB MIDI Gadget", 0,
			      out_ports, in_ports, &rmidi);
	if (err < 0) {
		ERROR(dev, "snd_rawmidi_new failed: error %d\n", err);
		goto fail;
	}
	dev->rmidi = rmidi;
	strcpy(rmidi->name, card->shortname);
	rmidi->info_flags = SNDRV_RAWMIDI_INFO_OUTPUT |
			    SNDRV_RAWMIDI_INFO_INPUT |
			    SNDRV_RAWMIDI_INFO_DUPLEX;
	rmidi->private_data = dev;

	/* Yes, rawmidi OUTPUT = USB IN, and rawmidi INPUT = USB OUT.
	   It's an upside-down world being a gadget. */
	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_OUTPUT, &gmidi_in_ops);
	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_INPUT, &gmidi_out_ops);

	snd_card_set_dev(card, &dev->gadget->dev);

	/* register it - we're ready to go */
	err = snd_card_register(card);
	if (err < 0) {
		ERROR(dev, "snd_card_register failed\n");
		goto fail;
	}

	VDBG(dev, "gmidi_register_card finished ok\n");
	return 0;

fail:
	if (dev->card) {
		snd_card_free(dev->card);
		dev->card = NULL;
	}
	return err;
}

/*
 * Creates an output endpoint, and initializes output ports.
 */
static int __devinit gmidi_bind(struct usb_gadget *gadget)
{
	struct gmidi_device *dev;
	struct usb_ep *in_ep, *out_ep;
	int gcnum, err = 0;

	/* support optional vendor/distro customization */
	if (idVendor) {
		if (!idProduct) {
			printk(KERN_ERR "idVendor needs idProduct!\n");
			return -ENODEV;
		}
		device_desc.idVendor = cpu_to_le16(idVendor);
		device_desc.idProduct = cpu_to_le16(idProduct);
		if (bcdDevice) {
			device_desc.bcdDevice = cpu_to_le16(bcdDevice);
		}
	}
	if (iManufacturer) {
		strlcpy(manufacturer, iManufacturer, sizeof(manufacturer));
	} else {
		snprintf(manufacturer, sizeof(manufacturer), "%s %s with %s",
			init_utsname()->sysname, init_utsname()->release,
			gadget->name);
	}
	if (iProduct) {
		strlcpy(product_desc, iProduct, sizeof(product_desc));
	}
	if (iSerialNumber) {
		device_desc.iSerialNumber = STRING_SERIAL,
		strlcpy(serial_number, iSerialNumber, sizeof(serial_number));
	}

	/* Bulk-only drivers like this one SHOULD be able to
	 * autoconfigure on any sane usb controller driver,
	 * but there may also be important quirks to address.
	 */
	usb_ep_autoconfig_reset(gadget);
	in_ep = usb_ep_autoconfig(gadget, &bulk_in_desc);
	if (!in_ep) {
autoconf_fail:
		printk(KERN_ERR "%s: can't autoconfigure on %s\n",
			shortname, gadget->name);
		return -ENODEV;
	}
	EP_IN_NAME = in_ep->name;
	in_ep->driver_data = in_ep;	/* claim */

	out_ep = usb_ep_autoconfig(gadget, &bulk_out_desc);
	if (!out_ep) {
		goto autoconf_fail;
	}
	EP_OUT_NAME = out_ep->name;
	out_ep->driver_data = out_ep;	/* claim */

	gcnum = usb_gadget_controller_number(gadget);
	if (gcnum >= 0) {
		device_desc.bcdDevice = cpu_to_le16(0x0200 + gcnum);
	} else {
		/* gmidi is so simple (no altsettings) that
		 * it SHOULD NOT have problems with bulk-capable hardware.
		 * so warn about unrecognized controllers, don't panic.
		 */
		printk(KERN_WARNING "%s: controller '%s' not recognized\n",
			shortname, gadget->name);
		device_desc.bcdDevice = __constant_cpu_to_le16(0x9999);
	}


	/* ok, we made sense of the hardware ... */
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		return -ENOMEM;
	}
	spin_lock_init(&dev->lock);
	dev->gadget = gadget;
	dev->in_ep = in_ep;
	dev->out_ep = out_ep;
	set_gadget_data(gadget, dev);
	tasklet_init(&dev->tasklet, gmidi_in_tasklet, (unsigned long)dev);

	/* preallocate control response and buffer */
	dev->req = alloc_ep_req(gadget->ep0, USB_BUFSIZ);
	if (!dev->req) {
		err = -ENOMEM;
		goto fail;
	}

	dev->req->complete = gmidi_setup_complete;

	device_desc.bMaxPacketSize0 = gadget->ep0->maxpacket;

	gadget->ep0->driver_data = dev;

	INFO(dev, "%s, version: " DRIVER_VERSION "\n", longname);
	INFO(dev, "using %s, OUT %s IN %s\n", gadget->name,
		EP_OUT_NAME, EP_IN_NAME);

	/* register as an ALSA sound card */
	err = gmidi_register_card(dev);
	if (err < 0) {
		goto fail;
	}

	VDBG(dev, "gmidi_bind finished ok\n");
	return 0;

fail:
	gmidi_unbind(gadget);
	return err;
}


static void gmidi_suspend(struct usb_gadget *gadget)
{
	struct gmidi_device *dev = get_gadget_data(gadget);

	if (gadget->speed == USB_SPEED_UNKNOWN) {
		return;
	}

	DBG(dev, "suspend\n");
}

static void gmidi_resume(struct usb_gadget *gadget)
{
	struct gmidi_device *dev = get_gadget_data(gadget);

	DBG(dev, "resume\n");
}


static struct usb_gadget_driver gmidi_driver = {
	.speed		= USB_SPEED_FULL,
	.function	= (char *)longname,
	.bind		= gmidi_bind,
	.unbind		= gmidi_unbind,

	.setup		= gmidi_setup,
	.disconnect	= gmidi_disconnect,

	.suspend	= gmidi_suspend,
	.resume		= gmidi_resume,

	.driver		= {
		.name		= (char *)shortname,
		.owner		= THIS_MODULE,
	},
};

static int __init gmidi_init(void)
{
	return usb_gadget_register_driver(&gmidi_driver);
}
module_init(gmidi_init);

static void __exit gmidi_cleanup(void)
{
	usb_gadget_unregister_driver(&gmidi_driver);
}
module_exit(gmidi_cleanup);

