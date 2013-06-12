/*
 * multi.c -- Multifunction Composite driver
 *
 * Copyright (C) 2008 David Brownell
 * Copyright (C) 2008 Nokia Corporation
 * Copyright (C) 2009 Samsung Electronics
 * Author: Michal Nazarewicz (mina86@mina86.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */


#include <linux/kernel.h>
#include <linux/module.h>

#include "u_serial.h"
#if defined USB_ETH_RNDIS
#  undef USB_ETH_RNDIS
#endif
#ifdef CONFIG_USB_G_MULTI_RNDIS
#  define USB_ETH_RNDIS y
#endif


#define DRIVER_DESC		"Multifunction Composite Gadget"

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR("Michal Nazarewicz");
MODULE_LICENSE("GPL");


/***************************** All the files... *****************************/

/*
 * kbuild is not very cooperative with respect to linking separately
 * compiled library objects into one module.  So for now we won't use
 * separate compilation ... ensuring init/exit sections work to shrink
 * the runtime footprint, and giving us at least some parts of what
 * a "gcc --combine ... part1.c part2.c part3.c ... " build would.
 */
#include "f_mass_storage.c"

#define USBF_ECM_INCLUDED
#include "f_ecm.c"
#ifdef USB_ETH_RNDIS
#  define USB_FRNDIS_INCLUDED
#  include "f_rndis.c"
#  include "rndis.h"
#endif
#include "u_ether.h"

USB_GADGET_COMPOSITE_OPTIONS();

USB_ETHERNET_MODULE_PARAMETERS();

/***************************** Device Descriptor ****************************/

#define MULTI_VENDOR_NUM	0x1d6b	/* Linux Foundation */
#define MULTI_PRODUCT_NUM	0x0104	/* Multifunction Composite Gadget */


enum {
	__MULTI_NO_CONFIG,
#ifdef CONFIG_USB_G_MULTI_RNDIS
	MULTI_RNDIS_CONFIG_NUM,
#endif
#ifdef CONFIG_USB_G_MULTI_CDC
	MULTI_CDC_CONFIG_NUM,
#endif
};


static struct usb_device_descriptor device_desc = {
	.bLength =		sizeof device_desc,
	.bDescriptorType =	USB_DT_DEVICE,

	.bcdUSB =		cpu_to_le16(0x0200),

	.bDeviceClass =		USB_CLASS_MISC /* 0xEF */,
	.bDeviceSubClass =	2,
	.bDeviceProtocol =	1,

	/* Vendor and product id can be overridden by module parameters.  */
	.idVendor =		cpu_to_le16(MULTI_VENDOR_NUM),
	.idProduct =		cpu_to_le16(MULTI_PRODUCT_NUM),
};


static const struct usb_descriptor_header *otg_desc[] = {
	(struct usb_descriptor_header *) &(struct usb_otg_descriptor){
		.bLength =		sizeof(struct usb_otg_descriptor),
		.bDescriptorType =	USB_DT_OTG,

		/*
		 * REVISIT SRP-only hardware is possible, although
		 * it would not be called "OTG" ...
		 */
		.bmAttributes =		USB_OTG_SRP | USB_OTG_HNP,
	},
	NULL,
};


enum {
	MULTI_STRING_RNDIS_CONFIG_IDX = USB_GADGET_FIRST_AVAIL_IDX,
	MULTI_STRING_CDC_CONFIG_IDX,
};

static struct usb_string strings_dev[] = {
	[USB_GADGET_MANUFACTURER_IDX].s = "",
	[USB_GADGET_PRODUCT_IDX].s = DRIVER_DESC,
	[USB_GADGET_SERIAL_IDX].s = "",
	[MULTI_STRING_RNDIS_CONFIG_IDX].s = "Multifunction with RNDIS",
	[MULTI_STRING_CDC_CONFIG_IDX].s   = "Multifunction with CDC ECM",
	{  } /* end of list */
};

static struct usb_gadget_strings *dev_strings[] = {
	&(struct usb_gadget_strings){
		.language	= 0x0409,	/* en-us */
		.strings	= strings_dev,
	},
	NULL,
};




/****************************** Configurations ******************************/

static struct fsg_module_parameters fsg_mod_data = { .stall = 1 };
FSG_MODULE_PARAMETERS(/* no prefix */, fsg_mod_data);

static struct fsg_common fsg_common;

static u8 host_mac[ETH_ALEN];

static struct usb_function_instance *fi_acm;
static struct eth_dev *the_dev;

/********** RNDIS **********/

#ifdef USB_ETH_RNDIS
static struct usb_function *f_acm_rndis;

static __init int rndis_do_config(struct usb_configuration *c)
{
	int ret;

	if (gadget_is_otg(c->cdev->gadget)) {
		c->descriptors = otg_desc;
		c->bmAttributes |= USB_CONFIG_ATT_WAKEUP;
	}

	ret = rndis_bind_config(c, host_mac, the_dev);
	if (ret < 0)
		return ret;

	f_acm_rndis = usb_get_function(fi_acm);
	if (IS_ERR(f_acm_rndis)) {
		ret = PTR_ERR(f_acm_rndis);
		goto err_func_acm;
	}

	ret = usb_add_function(c, f_acm_rndis);
	if (ret)
		goto err_conf;

	ret = fsg_bind_config(c->cdev, c, &fsg_common);
	if (ret < 0)
		goto err_fsg;

	return 0;
err_fsg:
	usb_remove_function(c, f_acm_rndis);
err_conf:
	usb_put_function(f_acm_rndis);
err_func_acm:
	return ret;
}

static int rndis_config_register(struct usb_composite_dev *cdev)
{
	static struct usb_configuration config = {
		.bConfigurationValue	= MULTI_RNDIS_CONFIG_NUM,
		.bmAttributes		= USB_CONFIG_ATT_SELFPOWER,
	};

	config.label          = strings_dev[MULTI_STRING_RNDIS_CONFIG_IDX].s;
	config.iConfiguration = strings_dev[MULTI_STRING_RNDIS_CONFIG_IDX].id;

	return usb_add_config(cdev, &config, rndis_do_config);
}

#else

static int rndis_config_register(struct usb_composite_dev *cdev)
{
	return 0;
}

#endif


/********** CDC ECM **********/

#ifdef CONFIG_USB_G_MULTI_CDC
static struct usb_function *f_acm_multi;

static __init int cdc_do_config(struct usb_configuration *c)
{
	int ret;

	if (gadget_is_otg(c->cdev->gadget)) {
		c->descriptors = otg_desc;
		c->bmAttributes |= USB_CONFIG_ATT_WAKEUP;
	}

	ret = ecm_bind_config(c, host_mac, the_dev);
	if (ret < 0)
		return ret;

	/* implicit port_num is zero */
	f_acm_multi = usb_get_function(fi_acm);
	if (IS_ERR(f_acm_multi))
		goto err_func_acm;

	ret = usb_add_function(c, f_acm_multi);
	if (ret)
		goto err_conf;

	ret = fsg_bind_config(c->cdev, c, &fsg_common);
	if (ret < 0)
		goto err_fsg;

	return 0;
err_fsg:
	usb_remove_function(c, f_acm_multi);
err_conf:
	usb_put_function(f_acm_multi);
err_func_acm:
	return ret;
}

static int cdc_config_register(struct usb_composite_dev *cdev)
{
	static struct usb_configuration config = {
		.bConfigurationValue	= MULTI_CDC_CONFIG_NUM,
		.bmAttributes		= USB_CONFIG_ATT_SELFPOWER,
	};

	config.label          = strings_dev[MULTI_STRING_CDC_CONFIG_IDX].s;
	config.iConfiguration = strings_dev[MULTI_STRING_CDC_CONFIG_IDX].id;

	return usb_add_config(cdev, &config, cdc_do_config);
}

#else

static int cdc_config_register(struct usb_composite_dev *cdev)
{
	return 0;
}

#endif



/****************************** Gadget Bind ******************************/

static int __ref multi_bind(struct usb_composite_dev *cdev)
{
	struct usb_gadget *gadget = cdev->gadget;
	int status;

	if (!can_support_ecm(cdev->gadget)) {
		dev_err(&gadget->dev, "controller '%s' not usable\n",
		        gadget->name);
		return -EINVAL;
	}

	/* set up network link layer */
	the_dev = gether_setup(cdev->gadget, dev_addr, host_addr, host_mac,
			       qmult);
	if (IS_ERR(the_dev))
		return PTR_ERR(the_dev);

	/* set up serial link layer */
	fi_acm = usb_get_function_instance("acm");
	if (IS_ERR(fi_acm)) {
		status = PTR_ERR(fi_acm);
		goto fail0;
	}

	/* set up mass storage function */
	{
		void *retp;
		retp = fsg_common_from_params(&fsg_common, cdev, &fsg_mod_data);
		if (IS_ERR(retp)) {
			status = PTR_ERR(retp);
			goto fail1;
		}
	}

	/* allocate string IDs */
	status = usb_string_ids_tab(cdev, strings_dev);
	if (unlikely(status < 0))
		goto fail2;
	device_desc.iProduct = strings_dev[USB_GADGET_PRODUCT_IDX].id;

	/* register configurations */
	status = rndis_config_register(cdev);
	if (unlikely(status < 0))
		goto fail2;

	status = cdc_config_register(cdev);
	if (unlikely(status < 0))
		goto fail2;
	usb_composite_overwrite_options(cdev, &coverwrite);

	/* we're done */
	dev_info(&gadget->dev, DRIVER_DESC "\n");
	fsg_common_put(&fsg_common);
	return 0;


	/* error recovery */
fail2:
	fsg_common_put(&fsg_common);
fail1:
	usb_put_function_instance(fi_acm);
fail0:
	gether_cleanup(the_dev);
	return status;
}

static int __exit multi_unbind(struct usb_composite_dev *cdev)
{
#ifdef CONFIG_USB_G_MULTI_CDC
	usb_put_function(f_acm_multi);
#endif
#ifdef USB_ETH_RNDIS
	usb_put_function(f_acm_rndis);
#endif
	usb_put_function_instance(fi_acm);
	gether_cleanup(the_dev);
	return 0;
}


/****************************** Some noise ******************************/


static __refdata struct usb_composite_driver multi_driver = {
	.name		= "g_multi",
	.dev		= &device_desc,
	.strings	= dev_strings,
	.max_speed	= USB_SPEED_HIGH,
	.bind		= multi_bind,
	.unbind		= __exit_p(multi_unbind),
	.needs_serial	= 1,
};


static int __init multi_init(void)
{
	return usb_composite_probe(&multi_driver);
}
module_init(multi_init);

static void __exit multi_exit(void)
{
	usb_composite_unregister(&multi_driver);
}
module_exit(multi_exit);
