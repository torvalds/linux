/*
 * audio.c -- Audio gadget driver
 *
 * Copyright (C) 2008 Bryan Wu <cooloney@kernel.org>
 * Copyright (C) 2008 Analog Devices, Inc
 *
 * Enter bugs at http://blackfin.uclinux.org/
 *
 * Licensed under the GPL-2 or later.
 */

/* #define VERBOSE_DEBUG */

#include <linux/kernel.h>
#include <linux/utsname.h>

#include "u_audio.h"

#define DRIVER_DESC		"Linux USB Audio Gadget"
#define DRIVER_VERSION		"Dec 18, 2008"

/*-------------------------------------------------------------------------*/

/*
 * Kbuild is not very cooperative with respect to linking separately
 * compiled library objects into one module.  So for now we won't use
 * separate compilation ... ensuring init/exit sections work to shrink
 * the runtime footprint, and giving us at least some parts of what
 * a "gcc --combine ... part1.c part2.c part3.c ... " build would.
 */
#include "composite.c"
#include "usbstring.c"
#include "config.c"
#include "epautoconf.c"

#include "u_audio.c"
#include "f_audio.c"

/*-------------------------------------------------------------------------*/

/* DO NOT REUSE THESE IDs with a protocol-incompatible driver!!  Ever!!
 * Instead:  allocate your own, using normal USB-IF procedures.
 */

/* Thanks to Linux Foundation for donating this product ID. */
#define AUDIO_VENDOR_NUM		0x1d6b	/* Linux Foundation */
#define AUDIO_PRODUCT_NUM		0x0101	/* Linux-USB Audio Gadget */

/*-------------------------------------------------------------------------*/

static struct usb_device_descriptor device_desc = {
	.bLength =		sizeof device_desc,
	.bDescriptorType =	USB_DT_DEVICE,

	.bcdUSB =		__constant_cpu_to_le16(0x200),

	.bDeviceClass =		USB_CLASS_PER_INTERFACE,
	.bDeviceSubClass =	0,
	.bDeviceProtocol =	0,
	/* .bMaxPacketSize0 = f(hardware) */

	/* Vendor and product id defaults change according to what configs
	 * we support.  (As does bNumConfigurations.)  These values can
	 * also be overridden by module parameters.
	 */
	.idVendor =		__constant_cpu_to_le16(AUDIO_VENDOR_NUM),
	.idProduct =		__constant_cpu_to_le16(AUDIO_PRODUCT_NUM),
	/* .bcdDevice = f(hardware) */
	/* .iManufacturer = DYNAMIC */
	/* .iProduct = DYNAMIC */
	/* NO SERIAL NUMBER */
	.bNumConfigurations =	1,
};

static struct usb_otg_descriptor otg_descriptor = {
	.bLength =		sizeof otg_descriptor,
	.bDescriptorType =	USB_DT_OTG,

	/* REVISIT SRP-only hardware is possible, although
	 * it would not be called "OTG" ...
	 */
	.bmAttributes =		USB_OTG_SRP | USB_OTG_HNP,
};

static const struct usb_descriptor_header *otg_desc[] = {
	(struct usb_descriptor_header *) &otg_descriptor,
	NULL,
};

/*-------------------------------------------------------------------------*/

/**
 * Handle USB audio endpoint set/get command in setup class request
 */

static int audio_set_endpoint_req(struct usb_configuration *c,
		const struct usb_ctrlrequest *ctrl)
{
	struct usb_composite_dev *cdev = c->cdev;
	int			value = -EOPNOTSUPP;
	u16			ep = le16_to_cpu(ctrl->wIndex);
	u16			len = le16_to_cpu(ctrl->wLength);
	u16			w_value = le16_to_cpu(ctrl->wValue);

	DBG(cdev, "bRequest 0x%x, w_value 0x%04x, len %d, endpoint %d\n",
			ctrl->bRequest, w_value, len, ep);

	switch (ctrl->bRequest) {
	case SET_CUR:
		value = 0;
		break;

	case SET_MIN:
		break;

	case SET_MAX:
		break;

	case SET_RES:
		break;

	case SET_MEM:
		break;

	default:
		break;
	}

	return value;
}

static int audio_get_endpoint_req(struct usb_configuration *c,
		const struct usb_ctrlrequest *ctrl)
{
	struct usb_composite_dev *cdev = c->cdev;
	int value = -EOPNOTSUPP;
	u8 ep = ((le16_to_cpu(ctrl->wIndex) >> 8) & 0xFF);
	u16 len = le16_to_cpu(ctrl->wLength);
	u16 w_value = le16_to_cpu(ctrl->wValue);

	DBG(cdev, "bRequest 0x%x, w_value 0x%04x, len %d, endpoint %d\n",
			ctrl->bRequest, w_value, len, ep);

	switch (ctrl->bRequest) {
	case GET_CUR:
	case GET_MIN:
	case GET_MAX:
	case GET_RES:
		value = 3;
		break;
	case GET_MEM:
		break;
	default:
		break;
	}

	return value;
}

static int
audio_setup(struct usb_configuration *c, const struct usb_ctrlrequest *ctrl)
{
	struct usb_composite_dev *cdev = c->cdev;
	struct usb_request *req = cdev->req;
	int value = -EOPNOTSUPP;
	u16 w_index = le16_to_cpu(ctrl->wIndex);
	u16 w_value = le16_to_cpu(ctrl->wValue);
	u16 w_length = le16_to_cpu(ctrl->wLength);

	/* composite driver infrastructure handles everything except
	 * Audio class messages; interface activation uses set_alt().
	 */
	switch (ctrl->bRequestType) {
	case USB_AUDIO_SET_ENDPOINT:
		value = audio_set_endpoint_req(c, ctrl);
		break;

	case USB_AUDIO_GET_ENDPOINT:
		value = audio_get_endpoint_req(c, ctrl);
		break;

	default:
		ERROR(cdev, "Invalid control req%02x.%02x v%04x i%04x l%d\n",
			ctrl->bRequestType, ctrl->bRequest,
			w_value, w_index, w_length);
	}

	/* respond with data transfer or status phase? */
	if (value >= 0) {
		DBG(cdev, "Audio req%02x.%02x v%04x i%04x l%d\n",
			ctrl->bRequestType, ctrl->bRequest,
			w_value, w_index, w_length);
		req->zero = 0;
		req->length = value;
		value = usb_ep_queue(cdev->gadget->ep0, req, GFP_ATOMIC);
		if (value < 0)
			ERROR(cdev, "Audio response on err %d\n", value);
	}

	/* device either stalls (value < 0) or reports success */
	return value;
}

/*-------------------------------------------------------------------------*/

static int __init audio_do_config(struct usb_configuration *c)
{
	/* FIXME alloc iConfiguration string, set it in c->strings */

	if (gadget_is_otg(c->cdev->gadget)) {
		c->descriptors = otg_desc;
		c->bmAttributes |= USB_CONFIG_ATT_WAKEUP;
	}

	audio_bind_config(c);

	return 0;
}

static struct usb_configuration audio_config_driver = {
	.label			= DRIVER_DESC,
	.bind			= audio_do_config,
	.setup			= audio_setup,
	.bConfigurationValue	= 1,
	/* .iConfiguration = DYNAMIC */
	.bmAttributes		= USB_CONFIG_ATT_SELFPOWER,
};

/*-------------------------------------------------------------------------*/

static int __init audio_bind(struct usb_composite_dev *cdev)
{
	int			gcnum;
	int			status;

	gcnum = usb_gadget_controller_number(cdev->gadget);
	if (gcnum >= 0)
		device_desc.bcdDevice = cpu_to_le16(0x0300 | gcnum);
	else {
		ERROR(cdev, "controller '%s' not recognized; trying %s\n",
			cdev->gadget->name,
			audio_config_driver.label);
		device_desc.bcdDevice =
			__constant_cpu_to_le16(0x0300 | 0x0099);
	}

	/* device descriptor strings: manufacturer, product */
	snprintf(manufacturer, sizeof manufacturer, "%s %s with %s",
		init_utsname()->sysname, init_utsname()->release,
		cdev->gadget->name);
	status = usb_string_id(cdev);
	if (status < 0)
		goto fail;
	strings_dev[STRING_MANUFACTURER_IDX].id = status;
	device_desc.iManufacturer = status;

	status = usb_string_id(cdev);
	if (status < 0)
		goto fail;
	strings_dev[STRING_PRODUCT_IDX].id = status;
	device_desc.iProduct = status;

	status = usb_add_config(cdev, &audio_config_driver);
	if (status < 0)
		goto fail;

	INFO(cdev, "%s, version: %s\n", DRIVER_DESC, DRIVER_VERSION);
	return 0;

fail:
	return status;
}

static int __exit audio_unbind(struct usb_composite_dev *cdev)
{
	return 0;
}

static struct usb_composite_driver audio_driver = {
	.name		= "g_audio",
	.dev		= &device_desc,
	.strings	= audio_strings,
	.bind		= audio_bind,
	.unbind		= __exit_p(audio_unbind),
};

static int __init init(void)
{
	return usb_composite_register(&audio_driver);
}
module_init(init);

static void __exit cleanup(void)
{
	usb_composite_unregister(&audio_driver);
}
module_exit(cleanup);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR("Bryan Wu <cooloney@kernel.org>");
MODULE_LICENSE("GPL");

