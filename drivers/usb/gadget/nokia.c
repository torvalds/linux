/*
 * nokia.c -- Nokia Composite Gadget Driver
 *
 * Copyright (C) 2008-2010 Nokia Corporation
 * Contact: Felipe Balbi <felipe.balbi@nokia.com>
 *
 * This gadget driver borrows from serial.c which is:
 *
 * Copyright (C) 2003 Al Borchers (alborchers@steinerpoint.com)
 * Copyright (C) 2008 by David Brownell
 * Copyright (C) 2008 by Nokia Corporation
 *
 * This software is distributed under the terms of the GNU General
 * Public License ("GPL") as published by the Free Software Foundation,
 * version 2 of that License.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>

#include "u_serial.h"
#include "u_ether.h"
#include "u_phonet.h"
#include "u_ecm.h"
#include "gadget_chips.h"

/* Defines */

#define NOKIA_VERSION_NUM		0x0211
#define NOKIA_LONG_NAME			"N900 (PC-Suite Mode)"

USB_GADGET_COMPOSITE_OPTIONS();

USB_ETHERNET_MODULE_PARAMETERS();

#define NOKIA_VENDOR_ID			0x0421	/* Nokia */
#define NOKIA_PRODUCT_ID		0x01c8	/* Nokia Gadget */

/* string IDs are assigned dynamically */

#define STRING_DESCRIPTION_IDX		USB_GADGET_FIRST_AVAIL_IDX

static char manufacturer_nokia[] = "Nokia";
static const char product_nokia[] = NOKIA_LONG_NAME;
static const char description_nokia[] = "PC-Suite Configuration";

static struct usb_string strings_dev[] = {
	[USB_GADGET_MANUFACTURER_IDX].s = manufacturer_nokia,
	[USB_GADGET_PRODUCT_IDX].s = NOKIA_LONG_NAME,
	[USB_GADGET_SERIAL_IDX].s = "",
	[STRING_DESCRIPTION_IDX].s = description_nokia,
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

static struct usb_device_descriptor device_desc = {
	.bLength		= USB_DT_DEVICE_SIZE,
	.bDescriptorType	= USB_DT_DEVICE,
	.bcdUSB			= __constant_cpu_to_le16(0x0200),
	.bDeviceClass		= USB_CLASS_COMM,
	.idVendor		= __constant_cpu_to_le16(NOKIA_VENDOR_ID),
	.idProduct		= __constant_cpu_to_le16(NOKIA_PRODUCT_ID),
	.bcdDevice		= cpu_to_le16(NOKIA_VERSION_NUM),
	/* .iManufacturer = DYNAMIC */
	/* .iProduct = DYNAMIC */
	.bNumConfigurations =	1,
};

/*-------------------------------------------------------------------------*/

/* Module */
MODULE_DESCRIPTION("Nokia composite gadget driver for N900");
MODULE_AUTHOR("Felipe Balbi");
MODULE_LICENSE("GPL");

/*-------------------------------------------------------------------------*/
static struct usb_function *f_acm_cfg1;
static struct usb_function *f_acm_cfg2;
static struct usb_function *f_ecm_cfg1;
static struct usb_function *f_ecm_cfg2;
static struct usb_function *f_obex1_cfg1;
static struct usb_function *f_obex2_cfg1;
static struct usb_function *f_obex1_cfg2;
static struct usb_function *f_obex2_cfg2;
static struct usb_function *f_phonet_cfg1;
static struct usb_function *f_phonet_cfg2;


static struct usb_configuration nokia_config_500ma_driver = {
	.label		= "Bus Powered",
	.bConfigurationValue = 1,
	/* .iConfiguration = DYNAMIC */
	.bmAttributes	= USB_CONFIG_ATT_ONE,
	.MaxPower	= 500,
};

static struct usb_configuration nokia_config_100ma_driver = {
	.label		= "Self Powered",
	.bConfigurationValue = 2,
	/* .iConfiguration = DYNAMIC */
	.bmAttributes	= USB_CONFIG_ATT_ONE | USB_CONFIG_ATT_SELFPOWER,
	.MaxPower	= 100,
};

static struct usb_function_instance *fi_acm;
static struct usb_function_instance *fi_ecm;
static struct usb_function_instance *fi_obex1;
static struct usb_function_instance *fi_obex2;
static struct usb_function_instance *fi_phonet;

static int __init nokia_bind_config(struct usb_configuration *c)
{
	struct usb_function *f_acm;
	struct usb_function *f_phonet = NULL;
	struct usb_function *f_obex1 = NULL;
	struct usb_function *f_ecm;
	struct usb_function *f_obex2 = NULL;
	int status = 0;
	int obex1_stat = -1;
	int obex2_stat = -1;
	int phonet_stat = -1;

	if (!IS_ERR(fi_phonet)) {
		f_phonet = usb_get_function(fi_phonet);
		if (IS_ERR(f_phonet))
			pr_debug("could not get phonet function\n");
	}

	if (!IS_ERR(fi_obex1)) {
		f_obex1 = usb_get_function(fi_obex1);
		if (IS_ERR(f_obex1))
			pr_debug("could not get obex function 0\n");
	}

	if (!IS_ERR(fi_obex2)) {
		f_obex2 = usb_get_function(fi_obex2);
		if (IS_ERR(f_obex2))
			pr_debug("could not get obex function 1\n");
	}

	f_acm = usb_get_function(fi_acm);
	if (IS_ERR(f_acm)) {
		status = PTR_ERR(f_acm);
		goto err_get_acm;
	}

	f_ecm = usb_get_function(fi_ecm);
	if (IS_ERR(f_ecm)) {
		status = PTR_ERR(f_ecm);
		goto err_get_ecm;
	}

	if (!IS_ERR_OR_NULL(f_phonet)) {
		phonet_stat = usb_add_function(c, f_phonet);
		if (phonet_stat)
			pr_debug("could not add phonet function\n");
	}

	if (!IS_ERR_OR_NULL(f_obex1)) {
		obex1_stat = usb_add_function(c, f_obex1);
		if (obex1_stat)
			pr_debug("could not add obex function 0\n");
	}

	if (!IS_ERR_OR_NULL(f_obex2)) {
		obex2_stat = usb_add_function(c, f_obex2);
		if (obex2_stat)
			pr_debug("could not add obex function 1\n");
	}

	status = usb_add_function(c, f_acm);
	if (status)
		goto err_conf;

	status = usb_add_function(c, f_ecm);
	if (status) {
		pr_debug("could not bind ecm config %d\n", status);
		goto err_ecm;
	}
	if (c == &nokia_config_500ma_driver) {
		f_acm_cfg1 = f_acm;
		f_ecm_cfg1 = f_ecm;
		f_phonet_cfg1 = f_phonet;
		f_obex1_cfg1 = f_obex1;
		f_obex2_cfg1 = f_obex2;
	} else {
		f_acm_cfg2 = f_acm;
		f_ecm_cfg2 = f_ecm;
		f_phonet_cfg2 = f_phonet;
		f_obex1_cfg2 = f_obex1;
		f_obex2_cfg2 = f_obex2;
	}

	return status;
err_ecm:
	usb_remove_function(c, f_acm);
err_conf:
	if (!obex2_stat)
		usb_remove_function(c, f_obex2);
	if (!obex1_stat)
		usb_remove_function(c, f_obex1);
	if (!phonet_stat)
		usb_remove_function(c, f_phonet);
	usb_put_function(f_ecm);
err_get_ecm:
	usb_put_function(f_acm);
err_get_acm:
	if (!IS_ERR_OR_NULL(f_obex2))
		usb_put_function(f_obex2);
	if (!IS_ERR_OR_NULL(f_obex1))
		usb_put_function(f_obex1);
	if (!IS_ERR_OR_NULL(f_phonet))
		usb_put_function(f_phonet);
	return status;
}

static int __init nokia_bind(struct usb_composite_dev *cdev)
{
	struct usb_gadget	*gadget = cdev->gadget;
	int			status;

	status = usb_string_ids_tab(cdev, strings_dev);
	if (status < 0)
		goto err_usb;
	device_desc.iManufacturer = strings_dev[USB_GADGET_MANUFACTURER_IDX].id;
	device_desc.iProduct = strings_dev[USB_GADGET_PRODUCT_IDX].id;
	status = strings_dev[STRING_DESCRIPTION_IDX].id;
	nokia_config_500ma_driver.iConfiguration = status;
	nokia_config_100ma_driver.iConfiguration = status;

	if (!gadget_supports_altsettings(gadget)) {
		status = -ENODEV;
		goto err_usb;
	}

	fi_phonet = usb_get_function_instance("phonet");
	if (IS_ERR(fi_phonet))
		pr_debug("could not find phonet function\n");

	fi_obex1 = usb_get_function_instance("obex");
	if (IS_ERR(fi_obex1))
		pr_debug("could not find obex function 1\n");

	fi_obex2 = usb_get_function_instance("obex");
	if (IS_ERR(fi_obex2))
		pr_debug("could not find obex function 2\n");

	fi_acm = usb_get_function_instance("acm");
	if (IS_ERR(fi_acm)) {
		status = PTR_ERR(fi_acm);
		goto err_obex2_inst;
	}

	fi_ecm = usb_get_function_instance("ecm");
	if (IS_ERR(fi_ecm)) {
		status = PTR_ERR(fi_ecm);
		goto err_acm_inst;
	}

	/* finally register the configuration */
	status = usb_add_config(cdev, &nokia_config_500ma_driver,
			nokia_bind_config);
	if (status < 0)
		goto err_ecm_inst;

	status = usb_add_config(cdev, &nokia_config_100ma_driver,
			nokia_bind_config);
	if (status < 0)
		goto err_put_cfg1;

	usb_composite_overwrite_options(cdev, &coverwrite);
	dev_info(&gadget->dev, "%s\n", NOKIA_LONG_NAME);

	return 0;

err_put_cfg1:
	usb_put_function(f_acm_cfg1);
	if (!IS_ERR_OR_NULL(f_obex1_cfg1))
		usb_put_function(f_obex1_cfg1);
	if (!IS_ERR_OR_NULL(f_obex2_cfg1))
		usb_put_function(f_obex2_cfg1);
	if (!IS_ERR_OR_NULL(f_phonet_cfg1))
		usb_put_function(f_phonet_cfg1);
	usb_put_function(f_ecm_cfg1);
err_ecm_inst:
	usb_put_function_instance(fi_ecm);
err_acm_inst:
	usb_put_function_instance(fi_acm);
err_obex2_inst:
	if (!IS_ERR(fi_obex2))
		usb_put_function_instance(fi_obex2);
	if (!IS_ERR(fi_obex1))
		usb_put_function_instance(fi_obex1);
	if (!IS_ERR(fi_phonet))
		usb_put_function_instance(fi_phonet);
err_usb:
	return status;
}

static int __exit nokia_unbind(struct usb_composite_dev *cdev)
{
	if (!IS_ERR_OR_NULL(f_obex1_cfg2))
		usb_put_function(f_obex1_cfg2);
	if (!IS_ERR_OR_NULL(f_obex2_cfg2))
		usb_put_function(f_obex2_cfg2);
	if (!IS_ERR_OR_NULL(f_obex1_cfg1))
		usb_put_function(f_obex1_cfg1);
	if (!IS_ERR_OR_NULL(f_obex2_cfg1))
		usb_put_function(f_obex2_cfg1);
	if (!IS_ERR_OR_NULL(f_phonet_cfg1))
		usb_put_function(f_phonet_cfg1);
	if (!IS_ERR_OR_NULL(f_phonet_cfg2))
		usb_put_function(f_phonet_cfg2);
	usb_put_function(f_acm_cfg1);
	usb_put_function(f_acm_cfg2);
	usb_put_function(f_ecm_cfg1);
	usb_put_function(f_ecm_cfg2);

	usb_put_function_instance(fi_ecm);
	if (!IS_ERR(fi_obex2))
		usb_put_function_instance(fi_obex2);
	if (!IS_ERR(fi_obex1))
		usb_put_function_instance(fi_obex1);
	if (!IS_ERR(fi_phonet))
		usb_put_function_instance(fi_phonet);
	usb_put_function_instance(fi_acm);

	return 0;
}

static __refdata struct usb_composite_driver nokia_driver = {
	.name		= "g_nokia",
	.dev		= &device_desc,
	.strings	= dev_strings,
	.max_speed	= USB_SPEED_HIGH,
	.bind		= nokia_bind,
	.unbind		= __exit_p(nokia_unbind),
};

static int __init nokia_init(void)
{
	return usb_composite_probe(&nokia_driver);
}
module_init(nokia_init);

static void __exit nokia_cleanup(void)
{
	usb_composite_unregister(&nokia_driver);
}
module_exit(nokia_cleanup);
