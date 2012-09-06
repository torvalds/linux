/*
 * dbgp.c -- EHCI Debug Port device gadget
 *
 * Copyright (C) 2010 Stephane Duverger
 *
 * Released under the GPLv2.
 */

/* verbose messages */
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>

/* See comments in "zero.c" */
#include "epautoconf.c"

#ifdef CONFIG_USB_G_DBGP_SERIAL
#include "u_serial.c"
#endif

#define DRIVER_VENDOR_ID	0x0525 /* NetChip */
#define DRIVER_PRODUCT_ID	0xc0de /* undefined */

#define USB_DEBUG_MAX_PACKET_SIZE     8
#define DBGP_REQ_EP0_LEN              128
#define DBGP_REQ_LEN                  512

static struct dbgp {
	struct usb_gadget  *gadget;
	struct usb_request *req;
	struct usb_ep      *i_ep;
	struct usb_ep      *o_ep;
#ifdef CONFIG_USB_G_DBGP_SERIAL
	struct gserial     *serial;
#endif
} dbgp;

static struct usb_device_descriptor device_desc = {
	.bLength = sizeof device_desc,
	.bDescriptorType = USB_DT_DEVICE,
	.bcdUSB = __constant_cpu_to_le16(0x0200),
	.bDeviceClass =	USB_CLASS_VENDOR_SPEC,
	.idVendor = __constant_cpu_to_le16(DRIVER_VENDOR_ID),
	.idProduct = __constant_cpu_to_le16(DRIVER_PRODUCT_ID),
	.bNumConfigurations = 1,
};

static struct usb_debug_descriptor dbg_desc = {
	.bLength = sizeof dbg_desc,
	.bDescriptorType = USB_DT_DEBUG,
};

static struct usb_endpoint_descriptor i_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bmAttributes = USB_ENDPOINT_XFER_BULK,
	.bEndpointAddress = USB_DIR_IN,
};

static struct usb_endpoint_descriptor o_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bmAttributes = USB_ENDPOINT_XFER_BULK,
	.bEndpointAddress = USB_DIR_OUT,
};

#ifdef CONFIG_USB_G_DBGP_PRINTK
static int dbgp_consume(char *buf, unsigned len)
{
	char c;

	if (!len)
		return 0;

	c = buf[len-1];
	if (c != 0)
		buf[len-1] = 0;

	printk(KERN_NOTICE "%s%c", buf, c);
	return 0;
}

static void __disable_ep(struct usb_ep *ep)
{
	if (ep && ep->driver_data == dbgp.gadget) {
		usb_ep_disable(ep);
		ep->driver_data = NULL;
	}
}

static void dbgp_disable_ep(void)
{
	__disable_ep(dbgp.i_ep);
	__disable_ep(dbgp.o_ep);
}

static void dbgp_complete(struct usb_ep *ep, struct usb_request *req)
{
	int stp;
	int err = 0;
	int status = req->status;

	if (ep == dbgp.i_ep) {
		stp = 1;
		goto fail;
	}

	if (status != 0) {
		stp = 2;
		goto release_req;
	}

	dbgp_consume(req->buf, req->actual);

	req->length = DBGP_REQ_LEN;
	err = usb_ep_queue(ep, req, GFP_ATOMIC);
	if (err < 0) {
		stp = 3;
		goto release_req;
	}

	return;

release_req:
	kfree(req->buf);
	usb_ep_free_request(dbgp.o_ep, req);
	dbgp_disable_ep();
fail:
	dev_dbg(&dbgp.gadget->dev,
		"complete: failure (%d:%d) ==> %d\n", stp, err, status);
}

static int dbgp_enable_ep_req(struct usb_ep *ep)
{
	int err, stp;
	struct usb_request *req;

	req = usb_ep_alloc_request(ep, GFP_KERNEL);
	if (!req) {
		err = -ENOMEM;
		stp = 1;
		goto fail_1;
	}

	req->buf = kmalloc(DBGP_REQ_LEN, GFP_KERNEL);
	if (!req->buf) {
		err = -ENOMEM;
		stp = 2;
		goto fail_2;
	}

	req->complete = dbgp_complete;
	req->length = DBGP_REQ_LEN;
	err = usb_ep_queue(ep, req, GFP_ATOMIC);
	if (err < 0) {
		stp = 3;
		goto fail_3;
	}

	return 0;

fail_3:
	kfree(req->buf);
fail_2:
	usb_ep_free_request(dbgp.o_ep, req);
fail_1:
	dev_dbg(&dbgp.gadget->dev,
		"enable ep req: failure (%d:%d)\n", stp, err);
	return err;
}

static int __enable_ep(struct usb_ep *ep, struct usb_endpoint_descriptor *desc)
{
	int err;
	ep->desc = desc;
	err = usb_ep_enable(ep);
	ep->driver_data = dbgp.gadget;
	return err;
}

static int dbgp_enable_ep(void)
{
	int err, stp;

	err = __enable_ep(dbgp.i_ep, &i_desc);
	if (err < 0) {
		stp = 1;
		goto fail_1;
	}

	err = __enable_ep(dbgp.o_ep, &o_desc);
	if (err < 0) {
		stp = 2;
		goto fail_2;
	}

	err = dbgp_enable_ep_req(dbgp.o_ep);
	if (err < 0) {
		stp = 3;
		goto fail_3;
	}

	return 0;

fail_3:
	__disable_ep(dbgp.o_ep);
fail_2:
	__disable_ep(dbgp.i_ep);
fail_1:
	dev_dbg(&dbgp.gadget->dev, "enable ep: failure (%d:%d)\n", stp, err);
	return err;
}
#endif

static void dbgp_disconnect(struct usb_gadget *gadget)
{
#ifdef CONFIG_USB_G_DBGP_PRINTK
	dbgp_disable_ep();
#else
	gserial_disconnect(dbgp.serial);
#endif
}

static void dbgp_unbind(struct usb_gadget *gadget)
{
#ifdef CONFIG_USB_G_DBGP_SERIAL
	kfree(dbgp.serial);
#endif
	if (dbgp.req) {
		kfree(dbgp.req->buf);
		usb_ep_free_request(gadget->ep0, dbgp.req);
	}

	gadget->ep0->driver_data = NULL;
}

static int __init dbgp_configure_endpoints(struct usb_gadget *gadget)
{
	int stp;

	usb_ep_autoconfig_reset(gadget);

	dbgp.i_ep = usb_ep_autoconfig(gadget, &i_desc);
	if (!dbgp.i_ep) {
		stp = 1;
		goto fail_1;
	}

	dbgp.i_ep->driver_data = gadget;
	i_desc.wMaxPacketSize =
		__constant_cpu_to_le16(USB_DEBUG_MAX_PACKET_SIZE);

	dbgp.o_ep = usb_ep_autoconfig(gadget, &o_desc);
	if (!dbgp.o_ep) {
		dbgp.i_ep->driver_data = NULL;
		stp = 2;
		goto fail_2;
	}

	dbgp.o_ep->driver_data = gadget;
	o_desc.wMaxPacketSize =
		__constant_cpu_to_le16(USB_DEBUG_MAX_PACKET_SIZE);

	dbg_desc.bDebugInEndpoint = i_desc.bEndpointAddress;
	dbg_desc.bDebugOutEndpoint = o_desc.bEndpointAddress;

#ifdef CONFIG_USB_G_DBGP_SERIAL
	dbgp.serial->in = dbgp.i_ep;
	dbgp.serial->out = dbgp.o_ep;

	dbgp.serial->in->desc = &i_desc;
	dbgp.serial->out->desc = &o_desc;

	if (gserial_setup(gadget, 1) < 0) {
		stp = 3;
		goto fail_3;
	}

	return 0;

fail_3:
	dbgp.o_ep->driver_data = NULL;
#else
	return 0;
#endif
fail_2:
	dbgp.i_ep->driver_data = NULL;
fail_1:
	dev_dbg(&dbgp.gadget->dev, "ep config: failure (%d)\n", stp);
	return -ENODEV;
}

static int __init dbgp_bind(struct usb_gadget *gadget)
{
	int err, stp;

	dbgp.gadget = gadget;

	dbgp.req = usb_ep_alloc_request(gadget->ep0, GFP_KERNEL);
	if (!dbgp.req) {
		err = -ENOMEM;
		stp = 1;
		goto fail;
	}

	dbgp.req->buf = kmalloc(DBGP_REQ_EP0_LEN, GFP_KERNEL);
	if (!dbgp.req->buf) {
		err = -ENOMEM;
		stp = 2;
		goto fail;
	}

	dbgp.req->length = DBGP_REQ_EP0_LEN;
	gadget->ep0->driver_data = gadget;

#ifdef CONFIG_USB_G_DBGP_SERIAL
	dbgp.serial = kzalloc(sizeof(struct gserial), GFP_KERNEL);
	if (!dbgp.serial) {
		stp = 3;
		err = -ENOMEM;
		goto fail;
	}
#endif
	err = dbgp_configure_endpoints(gadget);
	if (err < 0) {
		stp = 4;
		goto fail;
	}

	dev_dbg(&dbgp.gadget->dev, "bind: success\n");
	return 0;

fail:
	dev_dbg(&gadget->dev, "bind: failure (%d:%d)\n", stp, err);
	dbgp_unbind(gadget);
	return err;
}

static void dbgp_setup_complete(struct usb_ep *ep,
				struct usb_request *req)
{
	dev_dbg(&dbgp.gadget->dev, "setup complete: %d, %d/%d\n",
		req->status, req->actual, req->length);
}

static int dbgp_setup(struct usb_gadget *gadget,
		      const struct usb_ctrlrequest *ctrl)
{
	struct usb_request *req = dbgp.req;
	u8 request = ctrl->bRequest;
	u16 value = le16_to_cpu(ctrl->wValue);
	u16 length = le16_to_cpu(ctrl->wLength);
	int err = -EOPNOTSUPP;
	void *data = NULL;
	u16 len = 0;

	gadget->ep0->driver_data = gadget;

	if (request == USB_REQ_GET_DESCRIPTOR) {
		switch (value>>8) {
		case USB_DT_DEVICE:
			dev_dbg(&dbgp.gadget->dev, "setup: desc device\n");
			len = sizeof device_desc;
			data = &device_desc;
			device_desc.bMaxPacketSize0 = gadget->ep0->maxpacket;
			break;
		case USB_DT_DEBUG:
			dev_dbg(&dbgp.gadget->dev, "setup: desc debug\n");
			len = sizeof dbg_desc;
			data = &dbg_desc;
			break;
		default:
			goto fail;
		}
		err = 0;
	} else if (request == USB_REQ_SET_FEATURE &&
		   value == USB_DEVICE_DEBUG_MODE) {
		dev_dbg(&dbgp.gadget->dev, "setup: feat debug\n");
#ifdef CONFIG_USB_G_DBGP_PRINTK
		err = dbgp_enable_ep();
#else
		err = gserial_connect(dbgp.serial, 0);
#endif
		if (err < 0)
			goto fail;
	} else
		goto fail;

	req->length = min(length, len);
	req->zero = len < req->length;
	if (data && req->length)
		memcpy(req->buf, data, req->length);

	req->complete = dbgp_setup_complete;
	return usb_ep_queue(gadget->ep0, req, GFP_ATOMIC);

fail:
	dev_dbg(&dbgp.gadget->dev,
		"setup: failure req %x v %x\n", request, value);
	return err;
}

static __refdata struct usb_gadget_driver dbgp_driver = {
	.function = "dbgp",
	.max_speed = USB_SPEED_HIGH,
	.bind = dbgp_bind,
	.unbind = dbgp_unbind,
	.setup = dbgp_setup,
	.disconnect = dbgp_disconnect,
	.driver	= {
		.owner = THIS_MODULE,
		.name = "dbgp"
	},
};

static int __init dbgp_init(void)
{
	return usb_gadget_probe_driver(&dbgp_driver);
}

static void __exit dbgp_exit(void)
{
	usb_gadget_unregister_driver(&dbgp_driver);
#ifdef CONFIG_USB_G_DBGP_SERIAL
	gserial_cleanup();
#endif
}

MODULE_AUTHOR("Stephane Duverger");
MODULE_LICENSE("GPL");
module_init(dbgp_init);
module_exit(dbgp_exit);
