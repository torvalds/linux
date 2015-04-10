/*
 * cdc2.c -- CDC Composite driver, with ECM and ACM support
 *
 * Copyright (C) 2008 David Brownell
 * Copyright (C) 2008 Nokia Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/module.h>

#include "u_ether.h"
#include "u_serial.h"
#include "u_ecm.h"


#define DRIVER_DESC		"CDC Composite Gadget"
#define DRIVER_VERSION		"King Kamehameha Day 2008"

/*-------------------------------------------------------------------------*/

/* DO NOT REUSE THESE IDs with a protocol-incompatible driver!!  Ever!!
 * Instead:  allocate your own, using normal USB-IF procedures.
 */

/* Thanks to NetChip Technologies for donating this product ID.
 * It's for devices with only this composite CDC configuration.
 */
#define CDC_VENDOR_NUM		0x0525	/* NetChip */
#define CDC_PRODUCT_NUM		0xa4aa	/* CDC Composite: ECM + ACM */

USB_GADGET_COMPOSITE_OPTIONS();

USB_ETHERNET_MODULE_PARAMETERS();

/*-------------------------------------------------------------------------*/

static struct usb_device_descriptor device_desc = {
	.bLength =		sizeof device_desc,
	.bDescriptorType =	USB_DT_DEVICE,

	.bcdUSB =		cpu_to_le16(0x0200),

	.bDeviceClass =		USB_CLASS_COMM,
	.bDeviceSubClass =	0,
	.bDeviceProtocol =	0,
	/* .bMaxPacketSize0 = f(hardware) */

	/* Vendor and product id can be overridden by module parameters.  */
	.idVendor =		cpu_to_le16(CDC_VENDOR_NUM),
	.idProduct =		cpu_to_le16(CDC_PRODUCT_NUM),
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


/* string IDs are assigned dynamically */
static struct usb_string strings_dev[] = {
	[USB_GADGET_MANUFACTURER_IDX].s = "",
	[USB_GADGET_PRODUCT_IDX].s = DRIVER_DESC,
	[USB_GADGET_SERIAL_IDX].s = "",
	{  } /* end of list */
};

static struct usb_gadget_strings stringtab_dev = {
	.language	= 0x0409,	/* en-us */
	.strings	= strings_dev,
};

static struct usb_gadget_strings *dev_strings[] = {
	&stringtab_dev,
	NULL,
};

/*-------------------------------------------------------------------------*/
static struct usb_function *f_acm;
static struct usb_function_instance *fi_serial;

static struct usb_function *f_ecm;
static struct usb_function_instance *fi_ecm;

/*
 * We _always_ have both CDC ECM and CDC ACM functions.
 */
static int cdc_do_config(struct usb_configuration *c)
{
	int	status;

	if (gadget_is_otg(c->cdev->gadget)) {
		c->descriptors = otg_desc;
		c->bmAttributes |= USB_CONFIG_ATT_WAKEUP;
	}

	f_ecm = usb_get_function(fi_ecm);
	if (IS_ERR(f_ecm)) {
		status = PTR_ERR(f_ecm);
		goto err_get_ecm;
	}

	status = usb_add_function(c, f_ecm);
	if (status)
		goto err_add_ecm;

	f_acm = usb_get_function(fi_serial);
	if (IS_ERR(f_acm)) {
		status = PTR_ERR(f_acm);
		goto err_get_acm;
	}

	status = usb_add_function(c, f_acm);
	if (status)
		goto err_add_acm;
	return 0;

err_add_acm:
	usb_put_function(f_acm);
err_get_acm:
	usb_remove_function(c, f_ecm);
err_add_ecm:
	usb_put_function(f_ecm);
err_get_ecm:
	return status;
}

static struct usb_configuration cdc_config_driver = {
	.label			= "CDC Composite (ECM + ACM)",
	.bConfigurationValue	= 1,
	/* .iConfiguration = DYNAMIC */
	.bmAttributes		= USB_CONFIG_ATT_SELFPOWER,
};

/*-------------------------------------------------------------------------*/

static int cdc_bind(struct usb_composite_dev *cdev)
{
	struct usb_gadget	*gadget = cdev->gadget;
	struct f_ecm_opts	*ecm_opts;
	int			status;

	if (!can_support_ecm(cdev->gadget)) {
		dev_err(&gadget->dev, "controller '%s' not usable\n",
				gadget->name);
		return -EINVAL;
	}

	fi_ecm = usb_get_function_instance("ecm");
	if (IS_ERR(fi_ecm))
		return PTR_ERR(fi_ecm);

	ecm_opts = container_of(fi_ecm, struct f_ecm_opts, func_inst);

	gether_set_qmult(ecm_opts->net, qmult);
	if (!gether_set_host_addr(ecm_opts->net, host_addr))
		pr_info("using host ethernet address: %s", host_addr);
	if (!gether_set_dev_addr(ecm_opts->net, dev_addr))
		pr_info("using self ethernet address: %s", dev_addr);

	fi_serial = usb_get_function_instance("acm");
	if (IS_ERR(fi_serial)) {
		status = PTR_ERR(fi_serial);
		goto fail;
	}

	/* Allocate string descriptor numbers ... note that string
	 * contents can be overridden by the composite_dev glue.
	 */

	status = usb_string_ids_tab(cdev, strings_dev);
	if (status < 0)
		goto fail1;
	device_desc.iManufacturer = strings_dev[USB_GADGET_MANUFACTURER_IDX].id;
	device_desc.iProduct = strings_dev[USB_GADGET_PRODUCT_IDX].id;

	/* register our configuration */
	status = usb_add_config(cdev, &cdc_config_driver, cdc_do_config);
	if (status < 0)
		goto fail1;

	usb_composite_overwrite_options(cdev, &coverwrite);
	dev_info(&gadget->dev, "%s, version: " DRIVER_VERSION "\n",
			DRIVER_DESC);

	return 0;

fail1:
	usb_put_function_instance(fi_serial);
fail:
	usb_put_function_instance(fi_ecm);
	return status;
}

static int cdc_unbind(struct usb_composite_dev *cdev)
{
	usb_put_function(f_acm);
	usb_put_function_instance(fi_serial);
	if (!IS_ERR_OR_NULL(f_ecm))
		usb_put_function(f_ecm);
	if (!IS_ERR_OR_NULL(fi_ecm))
		usb_put_function_instance(fi_ecm);
	return 0;
}

static struct usb_composite_driver cdc_driver = {
	.name		= "g_cdc",
	.dev		= &device_desc,
	.strings	= dev_strings,
	.max_speed	= USB_SPEED_HIGH,
	.bind		= cdc_bind,
	.unbind		= cdc_unbind,
};

module_usb_composite_driver(cdc_driver);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR("David Brownell");
MODULE_LICENSE("GPL");
