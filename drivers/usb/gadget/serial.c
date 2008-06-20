/*
 * serial.c -- USB gadget serial driver
 *
 * Copyright (C) 2003 Al Borchers (alborchers@steinerpoint.com)
 * Copyright (C) 2008 by David Brownell
 *
 * This software is distributed under the terms of the GNU General
 * Public License ("GPL") as published by the Free Software Foundation,
 * either version 2 of that License or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/utsname.h>
#include <linux/device.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>

#include "u_serial.h"
#include "gadget_chips.h"


/* Defines */

#define GS_VERSION_STR			"v2.3"
#define GS_VERSION_NUM			0x2300

#define GS_LONG_NAME			"Gadget Serial"
#define GS_SHORT_NAME			"g_serial"

#define GS_VERSION_NAME			GS_LONG_NAME " " GS_VERSION_STR


/* REVISIT only one port is supported for now;
 * see gs_{send,recv}_packet() ... no multiplexing,
 * and no support for multiple ACM devices.
 */
#define GS_NUM_PORTS			1

#define GS_NUM_CONFIGS			1
#define GS_NO_CONFIG_ID			0
#define GS_BULK_CONFIG_ID		1
#define GS_ACM_CONFIG_ID		2

#define GS_MAX_NUM_INTERFACES		2
#define GS_BULK_INTERFACE_ID		0
#define GS_CONTROL_INTERFACE_ID		0
#define GS_DATA_INTERFACE_ID		1

#define GS_MAX_DESC_LEN			256

#define GS_DEFAULT_USE_ACM		0


/* maxpacket and other transfer characteristics vary by speed. */
static inline struct usb_endpoint_descriptor *
choose_ep_desc(struct usb_gadget *g, struct usb_endpoint_descriptor *hs,
		struct usb_endpoint_descriptor *fs)
{
	if (gadget_is_dualspeed(g) && g->speed == USB_SPEED_HIGH)
		return hs;
	return fs;
}


/* Thanks to NetChip Technologies for donating this product ID.
 *
 * DO NOT REUSE THESE IDs with a protocol-incompatible driver!!  Ever!!
 * Instead:  allocate your own, using normal USB-IF procedures.
 */
#define GS_VENDOR_ID			0x0525	/* NetChip */
#define GS_PRODUCT_ID			0xa4a6	/* Linux-USB Serial Gadget */
#define GS_CDC_PRODUCT_ID		0xa4a7	/* ... as CDC-ACM */

#define GS_LOG2_NOTIFY_INTERVAL		5	/* 1 << 5 == 32 msec */
#define GS_NOTIFY_MAXPACKET		8

/* the device structure holds info for the USB device */
struct gs_dev {
	struct usb_gadget	*dev_gadget;	/* gadget device pointer */
	spinlock_t		dev_lock;	/* lock for set/reset config */
	int			dev_config;	/* configuration number */
	struct usb_request	*dev_ctrl_req;	/* control request */

	struct gserial		gser;		/* serial/tty port */
};


/* Functions */

/* gadget driver internals */
static int gs_set_config(struct gs_dev *dev, unsigned config);
static void gs_reset_config(struct gs_dev *dev);
static int gs_build_config_buf(u8 *buf, struct usb_gadget *g,
		u8 type, unsigned int index, int is_otg);

static struct usb_request *gs_alloc_req(struct usb_ep *ep, unsigned int len,
	gfp_t kmalloc_flags);
static void gs_free_req(struct usb_ep *ep, struct usb_request *req);

/*-------------------------------------------------------------------------*/

/* USB descriptors */

#define GS_MANUFACTURER_STR_ID	1
#define GS_PRODUCT_STR_ID	2
#define GS_SERIAL_STR_ID	3
#define GS_BULK_CONFIG_STR_ID	4
#define GS_ACM_CONFIG_STR_ID	5
#define GS_CONTROL_STR_ID	6
#define GS_DATA_STR_ID		7

/* static strings, in UTF-8 */
static char manufacturer[50];
static struct usb_string gs_strings[] = {
	{ GS_MANUFACTURER_STR_ID, manufacturer },
	{ GS_PRODUCT_STR_ID, GS_VERSION_NAME },
	{ GS_BULK_CONFIG_STR_ID, "Gadget Serial Bulk" },
	{ GS_ACM_CONFIG_STR_ID, "Gadget Serial CDC ACM" },
	{ GS_CONTROL_STR_ID, "Gadget Serial Control" },
	{ GS_DATA_STR_ID, "Gadget Serial Data" },
	{  } /* end of list */
};

static struct usb_gadget_strings gs_string_table = {
	.language =		0x0409,	/* en-us */
	.strings =		gs_strings,
};

static struct usb_device_descriptor gs_device_desc = {
	.bLength =		USB_DT_DEVICE_SIZE,
	.bDescriptorType =	USB_DT_DEVICE,
	.bcdUSB =		__constant_cpu_to_le16(0x0200),
	.bDeviceSubClass =	0,
	.bDeviceProtocol =	0,
	.idVendor =		__constant_cpu_to_le16(GS_VENDOR_ID),
	.idProduct =		__constant_cpu_to_le16(GS_PRODUCT_ID),
	.iManufacturer =	GS_MANUFACTURER_STR_ID,
	.iProduct =		GS_PRODUCT_STR_ID,
	.bNumConfigurations =	GS_NUM_CONFIGS,
};

static struct usb_otg_descriptor gs_otg_descriptor = {
	.bLength =		sizeof(gs_otg_descriptor),
	.bDescriptorType =	USB_DT_OTG,
	.bmAttributes =		USB_OTG_SRP,
};

static struct usb_config_descriptor gs_bulk_config_desc = {
	.bLength =		USB_DT_CONFIG_SIZE,
	.bDescriptorType =	USB_DT_CONFIG,
	/* .wTotalLength computed dynamically */
	.bNumInterfaces =	1,
	.bConfigurationValue =	GS_BULK_CONFIG_ID,
	.iConfiguration =	GS_BULK_CONFIG_STR_ID,
	.bmAttributes =		USB_CONFIG_ATT_ONE | USB_CONFIG_ATT_SELFPOWER,
	.bMaxPower =		1,
};

static struct usb_config_descriptor gs_acm_config_desc = {
	.bLength =		USB_DT_CONFIG_SIZE,
	.bDescriptorType =	USB_DT_CONFIG,
	/* .wTotalLength computed dynamically */
	.bNumInterfaces =	2,
	.bConfigurationValue =	GS_ACM_CONFIG_ID,
	.iConfiguration =	GS_ACM_CONFIG_STR_ID,
	.bmAttributes =		USB_CONFIG_ATT_ONE | USB_CONFIG_ATT_SELFPOWER,
	.bMaxPower =		1,
};

static const struct usb_interface_descriptor gs_bulk_interface_desc = {
	.bLength =		USB_DT_INTERFACE_SIZE,
	.bDescriptorType =	USB_DT_INTERFACE,
	.bInterfaceNumber =	GS_BULK_INTERFACE_ID,
	.bNumEndpoints =	2,
	.bInterfaceClass =	USB_CLASS_VENDOR_SPEC,
	.bInterfaceSubClass =	0,
	.bInterfaceProtocol =	0,
	.iInterface =		GS_DATA_STR_ID,
};

static const struct usb_interface_descriptor gs_control_interface_desc = {
	.bLength =		USB_DT_INTERFACE_SIZE,
	.bDescriptorType =	USB_DT_INTERFACE,
	.bInterfaceNumber =	GS_CONTROL_INTERFACE_ID,
	.bNumEndpoints =	1,
	.bInterfaceClass =	USB_CLASS_COMM,
	.bInterfaceSubClass =	USB_CDC_SUBCLASS_ACM,
	.bInterfaceProtocol =	USB_CDC_ACM_PROTO_AT_V25TER,
	.iInterface =		GS_CONTROL_STR_ID,
};

static const struct usb_interface_descriptor gs_data_interface_desc = {
	.bLength =		USB_DT_INTERFACE_SIZE,
	.bDescriptorType =	USB_DT_INTERFACE,
	.bInterfaceNumber =	GS_DATA_INTERFACE_ID,
	.bNumEndpoints =	2,
	.bInterfaceClass =	USB_CLASS_CDC_DATA,
	.bInterfaceSubClass =	0,
	.bInterfaceProtocol =	0,
	.iInterface =		GS_DATA_STR_ID,
};

static const struct usb_cdc_header_desc gs_header_desc = {
	.bLength =		sizeof(gs_header_desc),
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubType =	USB_CDC_HEADER_TYPE,
	.bcdCDC =		__constant_cpu_to_le16(0x0110),
};

static const struct usb_cdc_call_mgmt_descriptor gs_call_mgmt_descriptor = {
	.bLength =		sizeof(gs_call_mgmt_descriptor),
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubType =	USB_CDC_CALL_MANAGEMENT_TYPE,
	.bmCapabilities =	0,
	.bDataInterface =	1,	/* index of data interface */
};

static struct usb_cdc_acm_descriptor gs_acm_descriptor = {
	.bLength =		sizeof(gs_acm_descriptor),
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubType =	USB_CDC_ACM_TYPE,
	.bmCapabilities =	(1 << 1),
};

static const struct usb_cdc_union_desc gs_union_desc = {
	.bLength =		sizeof(gs_union_desc),
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubType =	USB_CDC_UNION_TYPE,
	.bMasterInterface0 =	0,	/* index of control interface */
	.bSlaveInterface0 =	1,	/* index of data interface */
};

static struct usb_endpoint_descriptor gs_fullspeed_notify_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_INT,
	.wMaxPacketSize =	__constant_cpu_to_le16(GS_NOTIFY_MAXPACKET),
	.bInterval =		1 << GS_LOG2_NOTIFY_INTERVAL,
};

static struct usb_endpoint_descriptor gs_fullspeed_in_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
};

static struct usb_endpoint_descriptor gs_fullspeed_out_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_OUT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
};

static const struct usb_descriptor_header *gs_bulk_fullspeed_function[] = {
	(struct usb_descriptor_header *) &gs_otg_descriptor,
	(struct usb_descriptor_header *) &gs_bulk_interface_desc,
	(struct usb_descriptor_header *) &gs_fullspeed_in_desc,
	(struct usb_descriptor_header *) &gs_fullspeed_out_desc,
	NULL,
};

static const struct usb_descriptor_header *gs_acm_fullspeed_function[] = {
	(struct usb_descriptor_header *) &gs_otg_descriptor,
	(struct usb_descriptor_header *) &gs_control_interface_desc,
	(struct usb_descriptor_header *) &gs_header_desc,
	(struct usb_descriptor_header *) &gs_call_mgmt_descriptor,
	(struct usb_descriptor_header *) &gs_acm_descriptor,
	(struct usb_descriptor_header *) &gs_union_desc,
	(struct usb_descriptor_header *) &gs_fullspeed_notify_desc,
	(struct usb_descriptor_header *) &gs_data_interface_desc,
	(struct usb_descriptor_header *) &gs_fullspeed_in_desc,
	(struct usb_descriptor_header *) &gs_fullspeed_out_desc,
	NULL,
};

static struct usb_endpoint_descriptor gs_highspeed_notify_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_INT,
	.wMaxPacketSize =	__constant_cpu_to_le16(GS_NOTIFY_MAXPACKET),
	.bInterval =		GS_LOG2_NOTIFY_INTERVAL+4,
};

static struct usb_endpoint_descriptor gs_highspeed_in_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	__constant_cpu_to_le16(512),
};

static struct usb_endpoint_descriptor gs_highspeed_out_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	__constant_cpu_to_le16(512),
};

static struct usb_qualifier_descriptor gs_qualifier_desc = {
	.bLength =		sizeof(struct usb_qualifier_descriptor),
	.bDescriptorType =	USB_DT_DEVICE_QUALIFIER,
	.bcdUSB =		__constant_cpu_to_le16 (0x0200),
	/* assumes ep0 uses the same value for both speeds ... */
	.bNumConfigurations =	GS_NUM_CONFIGS,
};

static const struct usb_descriptor_header *gs_bulk_highspeed_function[] = {
	(struct usb_descriptor_header *) &gs_otg_descriptor,
	(struct usb_descriptor_header *) &gs_bulk_interface_desc,
	(struct usb_descriptor_header *) &gs_highspeed_in_desc,
	(struct usb_descriptor_header *) &gs_highspeed_out_desc,
	NULL,
};

static const struct usb_descriptor_header *gs_acm_highspeed_function[] = {
	(struct usb_descriptor_header *) &gs_otg_descriptor,
	(struct usb_descriptor_header *) &gs_control_interface_desc,
	(struct usb_descriptor_header *) &gs_header_desc,
	(struct usb_descriptor_header *) &gs_call_mgmt_descriptor,
	(struct usb_descriptor_header *) &gs_acm_descriptor,
	(struct usb_descriptor_header *) &gs_union_desc,
	(struct usb_descriptor_header *) &gs_highspeed_notify_desc,
	(struct usb_descriptor_header *) &gs_data_interface_desc,
	(struct usb_descriptor_header *) &gs_highspeed_in_desc,
	(struct usb_descriptor_header *) &gs_highspeed_out_desc,
	NULL,
};


/*-------------------------------------------------------------------------*/

/* Module */
MODULE_DESCRIPTION(GS_VERSION_NAME);
MODULE_AUTHOR("Al Borchers");
MODULE_AUTHOR("David Brownell");
MODULE_LICENSE("GPL");

static unsigned int use_acm = GS_DEFAULT_USE_ACM;
module_param(use_acm, uint, S_IRUGO);
MODULE_PARM_DESC(use_acm, "Use CDC ACM, 0=no, 1=yes, default=no");

/*-------------------------------------------------------------------------*/

/* Gadget Driver */

/*
 * gs_unbind
 *
 * Called on module unload.  Frees the control request and device
 * structure.
 */
static void __exit gs_unbind(struct usb_gadget *gadget)
{
	struct gs_dev *dev = get_gadget_data(gadget);

	/* read/write requests already freed, only control request remains */
	if (dev != NULL) {
		if (dev->dev_ctrl_req != NULL) {
			gs_free_req(gadget->ep0, dev->dev_ctrl_req);
			dev->dev_ctrl_req = NULL;
		}
		gs_reset_config(dev);
		kfree(dev);
		set_gadget_data(gadget, NULL);
	}

	pr_info("gs_unbind: %s unbound\n", GS_VERSION_NAME);

	gserial_cleanup();
}

/*
 * gs_bind
 *
 * Called on module load.  Allocates and initializes the device
 * structure and a control request.
 */
static int __init gs_bind(struct usb_gadget *gadget)
{
	int ret;
	struct usb_ep *ep;
	struct gs_dev *dev;
	int gcnum;

	ret = gserial_setup(gadget, GS_NUM_PORTS);
	if (ret < 0)
		return ret;

	/* Some controllers can't support CDC ACM:
	 * - sh doesn't support multiple interfaces or configs;
	 * - sa1100 doesn't have a third interrupt endpoint
	 */
	if (gadget_is_sh(gadget) || gadget_is_sa1100(gadget))
		use_acm = 0;

	gcnum = usb_gadget_controller_number(gadget);
	if (gcnum >= 0)
		gs_device_desc.bcdDevice =
				cpu_to_le16(GS_VERSION_NUM | gcnum);
	else {
		pr_warning("gs_bind: controller '%s' not recognized\n",
			gadget->name);
		/* unrecognized, but safe unless bulk is REALLY quirky */
		gs_device_desc.bcdDevice =
			__constant_cpu_to_le16(GS_VERSION_NUM|0x0099);
	}

	dev = kzalloc(sizeof(struct gs_dev), GFP_KERNEL);
	if (dev == NULL) {
		ret = -ENOMEM;
		goto autoconf_fail;
	}

	usb_ep_autoconfig_reset(gadget);
	ret = -ENXIO;

	ep = usb_ep_autoconfig(gadget, &gs_fullspeed_in_desc);
	if (!ep)
		goto autoconf_fail;
	dev->gser.in = ep;
	ep->driver_data = dev;	/* claim the endpoint */

	ep = usb_ep_autoconfig(gadget, &gs_fullspeed_out_desc);
	if (!ep)
		goto autoconf_fail;
	dev->gser.out = ep;
	ep->driver_data = dev;	/* claim the endpoint */

	if (use_acm) {
		ep = usb_ep_autoconfig(gadget, &gs_fullspeed_notify_desc);
		if (!ep) {
			pr_err("gs_bind: cannot run ACM on %s\n", gadget->name);
			goto autoconf_fail;
		}
		gs_device_desc.idProduct = __constant_cpu_to_le16(
						GS_CDC_PRODUCT_ID),
		dev->gser.notify = ep;
		ep->driver_data = dev;	/* claim the endpoint */
	}

	gs_device_desc.bDeviceClass = use_acm
		? USB_CLASS_COMM : USB_CLASS_VENDOR_SPEC;
	gs_device_desc.bMaxPacketSize0 = gadget->ep0->maxpacket;

	if (gadget_is_dualspeed(gadget)) {
		gs_qualifier_desc.bDeviceClass = use_acm
			? USB_CLASS_COMM : USB_CLASS_VENDOR_SPEC;
		/* assume ep0 uses the same packet size for both speeds */
		gs_qualifier_desc.bMaxPacketSize0 =
			gs_device_desc.bMaxPacketSize0;
		/* assume endpoints are dual-speed */
		gs_highspeed_notify_desc.bEndpointAddress =
			gs_fullspeed_notify_desc.bEndpointAddress;
		gs_highspeed_in_desc.bEndpointAddress =
			gs_fullspeed_in_desc.bEndpointAddress;
		gs_highspeed_out_desc.bEndpointAddress =
			gs_fullspeed_out_desc.bEndpointAddress;
	}

	usb_gadget_set_selfpowered(gadget);

	if (gadget_is_otg(gadget)) {
		gs_otg_descriptor.bmAttributes |= USB_OTG_HNP,
		gs_bulk_config_desc.bmAttributes |= USB_CONFIG_ATT_WAKEUP;
		gs_acm_config_desc.bmAttributes |= USB_CONFIG_ATT_WAKEUP;
	}

	snprintf(manufacturer, sizeof(manufacturer), "%s %s with %s",
		init_utsname()->sysname, init_utsname()->release,
		gadget->name);

	dev->dev_gadget = gadget;
	spin_lock_init(&dev->dev_lock);
	set_gadget_data(gadget, dev);

	/* preallocate control response and buffer */
	dev->dev_ctrl_req = gs_alloc_req(gadget->ep0, GS_MAX_DESC_LEN,
		GFP_KERNEL);
	if (dev->dev_ctrl_req == NULL) {
		ret = -ENOMEM;
		goto autoconf_fail;
	}
	gadget->ep0->driver_data = dev;

	pr_info("gs_bind: %s bound\n", GS_VERSION_NAME);

	return 0;

autoconf_fail:
	kfree(dev);
	gserial_cleanup();
	pr_err("gs_bind: to %s, err %d\n", gadget->name, ret);
	return ret;
}

static int gs_setup_standard(struct usb_gadget *gadget,
	const struct usb_ctrlrequest *ctrl)
{
	int ret = -EOPNOTSUPP;
	struct gs_dev *dev = get_gadget_data(gadget);
	struct usb_request *req = dev->dev_ctrl_req;
	u16 wIndex = le16_to_cpu(ctrl->wIndex);
	u16 wValue = le16_to_cpu(ctrl->wValue);
	u16 wLength = le16_to_cpu(ctrl->wLength);

	switch (ctrl->bRequest) {
	case USB_REQ_GET_DESCRIPTOR:
		if (ctrl->bRequestType != USB_DIR_IN)
			break;

		switch (wValue >> 8) {
		case USB_DT_DEVICE:
			ret = min(wLength,
				(u16)sizeof(struct usb_device_descriptor));
			memcpy(req->buf, &gs_device_desc, ret);
			break;

		case USB_DT_DEVICE_QUALIFIER:
			if (!gadget_is_dualspeed(gadget))
				break;
			ret = min(wLength,
				(u16)sizeof(struct usb_qualifier_descriptor));
			memcpy(req->buf, &gs_qualifier_desc, ret);
			break;

		case USB_DT_OTHER_SPEED_CONFIG:
			if (!gadget_is_dualspeed(gadget))
				break;
			/* fall through */
		case USB_DT_CONFIG:
			ret = gs_build_config_buf(req->buf, gadget,
				wValue >> 8, wValue & 0xff,
				gadget_is_otg(gadget));
			if (ret >= 0)
				ret = min(wLength, (u16)ret);
			break;

		case USB_DT_STRING:
			/* wIndex == language code. */
			ret = usb_gadget_get_string(&gs_string_table,
				wValue & 0xff, req->buf);
			if (ret >= 0)
				ret = min(wLength, (u16)ret);
			break;
		}
		break;

	case USB_REQ_SET_CONFIGURATION:
		if (ctrl->bRequestType != 0)
			break;
		spin_lock(&dev->dev_lock);
		ret = gs_set_config(dev, wValue);
		spin_unlock(&dev->dev_lock);
		break;

	case USB_REQ_GET_CONFIGURATION:
		if (ctrl->bRequestType != USB_DIR_IN)
			break;
		*(u8 *)req->buf = dev->dev_config;
		ret = min(wLength, (u16)1);
		break;

	case USB_REQ_SET_INTERFACE:
		if (ctrl->bRequestType != USB_RECIP_INTERFACE
				|| !dev->dev_config
				|| wIndex >= GS_MAX_NUM_INTERFACES)
			break;
		if (dev->dev_config == GS_BULK_CONFIG_ID
				&& wIndex != GS_BULK_INTERFACE_ID)
			break;
		/* no alternate interface settings */
		if (wValue != 0)
			break;
		spin_lock(&dev->dev_lock);
		/* PXA hardware partially handles SET_INTERFACE;
		 * we need to kluge around that interference.  */
		if (gadget_is_pxa(gadget)) {
			ret = gs_set_config(dev, use_acm ?
				GS_ACM_CONFIG_ID : GS_BULK_CONFIG_ID);
			goto set_interface_done;
		}
		if (dev->dev_config != GS_BULK_CONFIG_ID
				&& wIndex == GS_CONTROL_INTERFACE_ID) {
			if (dev->gser.notify) {
				usb_ep_disable(dev->gser.notify);
				usb_ep_enable(dev->gser.notify,
						dev->gser.notify_desc);
			}
		} else {
			gserial_connect(&dev->gser, 0);
			gserial_disconnect(&dev->gser);
		}
		ret = 0;
set_interface_done:
		spin_unlock(&dev->dev_lock);
		break;

	case USB_REQ_GET_INTERFACE:
		if (ctrl->bRequestType != (USB_DIR_IN|USB_RECIP_INTERFACE)
		|| dev->dev_config == GS_NO_CONFIG_ID)
			break;
		if (wIndex >= GS_MAX_NUM_INTERFACES
				|| (dev->dev_config == GS_BULK_CONFIG_ID
				&& wIndex != GS_BULK_INTERFACE_ID)) {
			ret = -EDOM;
			break;
		}
		/* no alternate interface settings */
		*(u8 *)req->buf = 0;
		ret = min(wLength, (u16)1);
		break;

	default:
		pr_err("gs_setup: unknown standard request, type=%02x, "
			"request=%02x, value=%04x, index=%04x, length=%d\n",
			ctrl->bRequestType, ctrl->bRequest,
			wValue, wIndex, wLength);
		break;
	}

	return ret;
}

static void gs_setup_complete_set_line_coding(struct usb_ep *ep,
		struct usb_request *req)
{
	struct gs_dev *dev = ep->driver_data;

	switch (req->status) {
	case 0:
		/* normal completion */
		if (req->actual != sizeof(dev->gser.port_line_coding))
			usb_ep_set_halt(ep);
		else {
			struct usb_cdc_line_coding	*value = req->buf;

			/* REVISIT:  we currently just remember this data.
			 * If we change that, (a) validate it first, then
			 * (b) update whatever hardware needs updating.
			 */
			spin_lock(&dev->dev_lock);
			dev->gser.port_line_coding = *value;
			spin_unlock(&dev->dev_lock);
		}
		break;

	case -ESHUTDOWN:
		/* disconnect */
		gs_free_req(ep, req);
		break;

	default:
		/* unexpected */
		break;
	}
	return;
}

static int gs_setup_class(struct usb_gadget *gadget,
	const struct usb_ctrlrequest *ctrl)
{
	int ret = -EOPNOTSUPP;
	struct gs_dev *dev = get_gadget_data(gadget);
	struct usb_request *req = dev->dev_ctrl_req;
	u16 wIndex = le16_to_cpu(ctrl->wIndex);
	u16 wValue = le16_to_cpu(ctrl->wValue);
	u16 wLength = le16_to_cpu(ctrl->wLength);

	switch (ctrl->bRequest) {
	case USB_CDC_REQ_SET_LINE_CODING:
		if (wLength != sizeof(struct usb_cdc_line_coding))
			break;
		ret = wLength;
		req->complete = gs_setup_complete_set_line_coding;
		break;

	case USB_CDC_REQ_GET_LINE_CODING:
		ret = min_t(int, wLength, sizeof(struct usb_cdc_line_coding));
		spin_lock(&dev->dev_lock);
		memcpy(req->buf, &dev->gser.port_line_coding, ret);
		spin_unlock(&dev->dev_lock);
		break;

	case USB_CDC_REQ_SET_CONTROL_LINE_STATE:
		if (wLength != 0)
			break;
		ret = 0;
		/* REVISIT:  we currently just remember this data.
		 * If we change that, update whatever hardware needs
		 * updating.
		 */
		spin_lock(&dev->dev_lock);
		dev->gser.port_handshake_bits = wValue;
		spin_unlock(&dev->dev_lock);
		break;

	default:
		/* NOTE:  strictly speaking, we should accept AT-commands
		 * using SEND_ENCPSULATED_COMMAND/GET_ENCAPSULATED_RESPONSE.
		 * But our call management descriptor says we don't handle
		 * call management, so we should be able to get by without
		 * handling those "required" commands (except by stalling).
		 */
		pr_err("gs_setup: unknown class request, "
				"type=%02x, request=%02x, value=%04x, "
				"index=%04x, length=%d\n",
			ctrl->bRequestType, ctrl->bRequest,
			wValue, wIndex, wLength);
		break;
	}

	return ret;
}

/*
 * gs_setup_complete
 */
static void gs_setup_complete(struct usb_ep *ep, struct usb_request *req)
{
	if (req->status || req->actual != req->length) {
		pr_err("gs_setup_complete: status error, status=%d, "
			"actual=%d, length=%d\n",
			req->status, req->actual, req->length);
	}
}

/*
 * gs_setup
 *
 * Implements all the control endpoint functionality that's not
 * handled in hardware or the hardware driver.
 *
 * Returns the size of the data sent to the host, or a negative
 * error number.
 */
static int gs_setup(struct usb_gadget *gadget,
	const struct usb_ctrlrequest *ctrl)
{
	int		ret = -EOPNOTSUPP;
	struct gs_dev	*dev = get_gadget_data(gadget);
	struct usb_request *req = dev->dev_ctrl_req;
	u16		wIndex = le16_to_cpu(ctrl->wIndex);
	u16		wValue = le16_to_cpu(ctrl->wValue);
	u16		wLength = le16_to_cpu(ctrl->wLength);

	req->complete = gs_setup_complete;

	switch (ctrl->bRequestType & USB_TYPE_MASK) {
	case USB_TYPE_STANDARD:
		ret = gs_setup_standard(gadget, ctrl);
		break;

	case USB_TYPE_CLASS:
		ret = gs_setup_class(gadget, ctrl);
		break;

	default:
		pr_err("gs_setup: unknown request, type=%02x, request=%02x, "
			"value=%04x, index=%04x, length=%d\n",
			ctrl->bRequestType, ctrl->bRequest,
			wValue, wIndex, wLength);
		break;
	}

	/* respond with data transfer before status phase? */
	if (ret >= 0) {
		req->length = ret;
		req->zero = ret < wLength
				&& (ret % gadget->ep0->maxpacket) == 0;
		ret = usb_ep_queue(gadget->ep0, req, GFP_ATOMIC);
		if (ret < 0) {
			pr_err("gs_setup: cannot queue response, ret=%d\n",
				ret);
			req->status = 0;
			gs_setup_complete(gadget->ep0, req);
		}
	}

	/* device either stalls (ret < 0) or reports success */
	return ret;
}

/*
 * gs_disconnect
 *
 * Called when the device is disconnected.  Frees the closed
 * ports and disconnects open ports.  Open ports will be freed
 * on close.  Then reallocates the ports for the next connection.
 */
static void gs_disconnect(struct usb_gadget *gadget)
{
	unsigned long flags;
	struct gs_dev *dev = get_gadget_data(gadget);

	spin_lock_irqsave(&dev->dev_lock, flags);
	gs_reset_config(dev);
	spin_unlock_irqrestore(&dev->dev_lock, flags);

	pr_info("gs_disconnect: %s disconnected\n", GS_LONG_NAME);
}

static struct usb_gadget_driver gs_gadget_driver = {
#ifdef CONFIG_USB_GADGET_DUALSPEED
	.speed =		USB_SPEED_HIGH,
#else
	.speed =		USB_SPEED_FULL,
#endif /* CONFIG_USB_GADGET_DUALSPEED */
	.function =		GS_LONG_NAME,
	.bind =			gs_bind,
	.unbind =		gs_unbind,
	.setup =		gs_setup,
	.disconnect =		gs_disconnect,
	.driver = {
		.name =		GS_SHORT_NAME,
		.owner =	THIS_MODULE,
	},
};

/*
 * gs_set_config
 *
 * Configures the device by enabling device specific
 * optimizations, setting up the endpoints, allocating
 * read and write requests and queuing read requests.
 *
 * The device lock must be held when calling this function.
 */
static int gs_set_config(struct gs_dev *dev, unsigned config)
{
	int ret = 0;
	struct usb_gadget *gadget = dev->dev_gadget;

	if (config == dev->dev_config)
		return 0;

	gs_reset_config(dev);

	switch (config) {
	case GS_NO_CONFIG_ID:
		return 0;
	case GS_BULK_CONFIG_ID:
		if (use_acm)
			return -EINVAL;
		break;
	case GS_ACM_CONFIG_ID:
		if (!use_acm)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	dev->gser.in_desc = choose_ep_desc(gadget,
			&gs_highspeed_in_desc,
			&gs_fullspeed_in_desc);
	dev->gser.out_desc = choose_ep_desc(gadget,
			&gs_highspeed_out_desc,
			&gs_fullspeed_out_desc);
	dev->gser.notify_desc = dev->gser.notify
		? choose_ep_desc(gadget,
				&gs_highspeed_notify_desc,
				&gs_fullspeed_notify_desc)
		: NULL;

	/* only support one "serial" port for now */
	if (dev->gser.notify) {
		ret = usb_ep_enable(dev->gser.notify, dev->gser.notify_desc);
		if (ret < 0)
			return ret;
		dev->gser.notify->driver_data = dev;
	}

	ret = gserial_connect(&dev->gser, 0);
	if (ret < 0) {
		if (dev->gser.notify) {
			usb_ep_disable(dev->gser.notify);
			dev->gser.notify->driver_data = NULL;
		}
		return ret;
	}

	dev->dev_config = config;

	/* REVISIT the ACM mode should be able to actually *issue* some
	 * notifications, for at least serial state change events if
	 * not also for network connection; say so in bmCapabilities.
	 */

	pr_info("gs_set_config: %s configured, %s speed %s config\n",
		GS_LONG_NAME,
		gadget->speed == USB_SPEED_HIGH ? "high" : "full",
		config == GS_BULK_CONFIG_ID ? "BULK" : "CDC-ACM");

	return 0;
}

/*
 * gs_reset_config
 *
 * Mark the device as not configured, disable all endpoints,
 * which forces completion of pending I/O and frees queued
 * requests, and free the remaining write requests on the
 * free list.
 *
 * The device lock must be held when calling this function.
 */
static void gs_reset_config(struct gs_dev *dev)
{
	if (dev->dev_config == GS_NO_CONFIG_ID)
		return;

	dev->dev_config = GS_NO_CONFIG_ID;

	gserial_disconnect(&dev->gser);
	if (dev->gser.notify) {
		usb_ep_disable(dev->gser.notify);
		dev->gser.notify->driver_data = NULL;
	}
}

/*
 * gs_build_config_buf
 *
 * Builds the config descriptors in the given buffer and returns the
 * length, or a negative error number.
 */
static int gs_build_config_buf(u8 *buf, struct usb_gadget *g,
	u8 type, unsigned int index, int is_otg)
{
	int len;
	int high_speed = 0;
	const struct usb_config_descriptor *config_desc;
	const struct usb_descriptor_header **function;

	if (index >= gs_device_desc.bNumConfigurations)
		return -EINVAL;

	/* other speed switches high and full speed */
	if (gadget_is_dualspeed(g)) {
		high_speed = (g->speed == USB_SPEED_HIGH);
		if (type == USB_DT_OTHER_SPEED_CONFIG)
			high_speed = !high_speed;
	}

	if (use_acm) {
		config_desc = &gs_acm_config_desc;
		function = high_speed
			? gs_acm_highspeed_function
			: gs_acm_fullspeed_function;
	} else {
		config_desc = &gs_bulk_config_desc;
		function = high_speed
			? gs_bulk_highspeed_function
			: gs_bulk_fullspeed_function;
	}

	/* for now, don't advertise srp-only devices */
	if (!is_otg)
		function++;

	len = usb_gadget_config_buf(config_desc, buf, GS_MAX_DESC_LEN, function);
	if (len < 0)
		return len;

	((struct usb_config_descriptor *)buf)->bDescriptorType = type;

	return len;
}

/*
 * gs_alloc_req
 *
 * Allocate a usb_request and its buffer.  Returns a pointer to the
 * usb_request or NULL if there is an error.
 */
static struct usb_request *
gs_alloc_req(struct usb_ep *ep, unsigned int len, gfp_t kmalloc_flags)
{
	struct usb_request *req;

	if (ep == NULL)
		return NULL;

	req = usb_ep_alloc_request(ep, kmalloc_flags);

	if (req != NULL) {
		req->length = len;
		req->buf = kmalloc(len, kmalloc_flags);
		if (req->buf == NULL) {
			usb_ep_free_request(ep, req);
			return NULL;
		}
	}

	return req;
}

/*
 * gs_free_req
 *
 * Free a usb_request and its buffer.
 */
static void gs_free_req(struct usb_ep *ep, struct usb_request *req)
{
	if (ep != NULL && req != NULL) {
		kfree(req->buf);
		usb_ep_free_request(ep, req);
	}
}

/*-------------------------------------------------------------------------*/

/*
 *  gs_module_init
 *
 *  Register as a USB gadget driver and a tty driver.
 */
static int __init gs_module_init(void)
{
	return usb_gadget_register_driver(&gs_gadget_driver);
}
module_init(gs_module_init);

/*
 * gs_module_exit
 *
 * Unregister as a tty driver and a USB gadget driver.
 */
static void __exit gs_module_exit(void)
{
	usb_gadget_unregister_driver(&gs_gadget_driver);
}
module_exit(gs_module_exit);
