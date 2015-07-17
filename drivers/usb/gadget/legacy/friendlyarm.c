/*
 * friendlyarm.c -- USB gadget (serial + ECM) driver
 *                  (based on serial.c)
 *
 * Copyright (C) 2003 Al Borchers (alborchers@steinerpoint.com)
 * Copyright (C) 2008 by David Brownell
 * Copyright (C) 2008 by Nokia Corporation
 * Copyright (C) 2015 by FriendlyARM (www.arm9.net)
 *
 * This software is distributed under the terms of the GNU General
 * Public License ("GPL") as published by the Free Software Foundation,
 * either version 2 of that License or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>

#include "u_serial.h"
#include "u_ether.h"
#include "u_ecm.h"


/* Defines */

#define GS_VERSION_STR			"v2.4"
#define GS_VERSION_NUM			0x2400

#define GS_LONG_NAME			"FriendlyARM Gadget"
#define GS_VERSION_NAME			GS_LONG_NAME " " GS_VERSION_STR

/*-------------------------------------------------------------------------*/
USB_GADGET_COMPOSITE_OPTIONS();

USB_ETHERNET_MODULE_PARAMETERS();

/* Thanks to NetChip Technologies for donating this product ID.
*
* DO NOT REUSE THESE IDs with a protocol-incompatible driver!!  Ever!!
* Instead:  allocate your own, using normal USB-IF procedures.
*/
#define GS_VENDOR_ID			0x0525	/* NetChip */
#define GS_PRODUCT_ID			0xa4a6	/* Linux-USB Serial Gadget */
#define GS_CDC_PRODUCT_ID		0xa4a7	/* ... as CDC-ACM */

/* string IDs are assigned dynamically */

#define STRING_DESCRIPTION_IDX		USB_GADGET_FIRST_AVAIL_IDX

static struct usb_string strings_dev[] = {
	[USB_GADGET_MANUFACTURER_IDX].s = "",
	[USB_GADGET_PRODUCT_IDX].s = GS_VERSION_NAME,
	[USB_GADGET_SERIAL_IDX].s = "",
	[STRING_DESCRIPTION_IDX].s = NULL /* updated; f(use_acm) */,
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
	.bLength =		USB_DT_DEVICE_SIZE,
	.bDescriptorType =	USB_DT_DEVICE,
	.bcdUSB =		cpu_to_le16(0x0200),
	/* .bDeviceClass = f(use_acm) */
	.bDeviceSubClass =	0,
	.bDeviceProtocol =	0,
	/* .bMaxPacketSize0 = f(hardware) */
	.idVendor =		cpu_to_le16(GS_VENDOR_ID),
	/* .idProduct =	f(use_acm) */
	.bcdDevice = cpu_to_le16(GS_VERSION_NUM),
	/* .iManufacturer = DYNAMIC */
	/* .iProduct = DYNAMIC */
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

/* Module */
MODULE_DESCRIPTION(GS_VERSION_NAME);
MODULE_AUTHOR("Al Borchers");
MODULE_AUTHOR("David Brownell");
MODULE_AUTHOR("FriendlyARM (www.arm9.net)");
MODULE_LICENSE("GPL");

static bool use_acm = true;
module_param(use_acm, bool, 0);
MODULE_PARM_DESC(use_acm, "Use CDC ACM, default=yes");

static unsigned n_ports = 1;
module_param(n_ports, uint, 0);
MODULE_PARM_DESC(n_ports, "number of ports to create, default=1");

/*-------------------------------------------------------------------------*/

static struct usb_configuration friendlyarm_config_driver = {
	/* .label = f(use_acm) */
	/* .bConfigurationValue = f(use_acm) */
	/* .iConfiguration = DYNAMIC */
	.bmAttributes	= USB_CONFIG_ATT_SELFPOWER,
};

static struct usb_function_instance *fi_serial[MAX_U_SERIAL_PORTS];
static struct usb_function *f_serial[MAX_U_SERIAL_PORTS];

static struct usb_function *f_ecm;
static struct usb_function_instance *fi_ecm;

static int friendlyarm_bind_config(struct usb_composite_dev *cdev,
		struct usb_configuration *c, const char *f_name)
{
	int i;
	int ret;

	ret = usb_add_config_only(cdev, c);
	if (ret)
		goto out;

	for (i = 0; i < n_ports; i++) {

		fi_serial[i] = usb_get_function_instance(f_name);
		if (IS_ERR(fi_serial[i])) {
			ret = PTR_ERR(fi_serial[i]);
			goto fail;
		}

		f_serial[i] = usb_get_function(fi_serial[i]);
		if (IS_ERR(f_serial[i])) {
			ret = PTR_ERR(f_serial[i]);
			goto err_get_func;
		}

		ret = usb_add_function(c, f_serial[i]);
		if (ret)
			goto err_add_func;
	}

	f_ecm = usb_get_function(fi_ecm);
	if (IS_ERR(f_ecm)) {
		ret = PTR_ERR(f_ecm);
		goto err_ecm;
	}

	ret = usb_add_function(c, f_ecm);
	if (ret)
		goto err_ecm;

	return 0;

err_ecm:
	if (!IS_ERR_OR_NULL(f_ecm))
		usb_put_function(f_ecm);
	if (!IS_ERR_OR_NULL(fi_ecm))
		usb_put_function_instance(fi_ecm);

err_add_func:
	usb_put_function(f_serial[i]);
err_get_func:
	usb_put_function_instance(fi_serial[i]);

fail:
	i--;
	while (i >= 0) {
		usb_remove_function(c, f_serial[i]);
		usb_put_function(f_serial[i]);
		usb_put_function_instance(fi_serial[i]);
		i--;
	}
out:
	return ret;
}

static int friendlyarm_bind(struct usb_composite_dev *cdev)
{
	struct f_ecm_opts	*ecm_opts;
	int			status;

	if (can_support_ecm(cdev->gadget)) {
		fi_ecm = usb_get_function_instance("ecm");
		if (IS_ERR(fi_ecm))
			return PTR_ERR(fi_ecm);

		ecm_opts = container_of(fi_ecm, struct f_ecm_opts, func_inst);

		gether_set_qmult(ecm_opts->net, qmult);
		if (!gether_set_host_addr(ecm_opts->net, host_addr))
			pr_info("using host ethernet address: %s", host_addr);
		if (!gether_set_dev_addr(ecm_opts->net, dev_addr))
			pr_info("using self ethernet address: %s", dev_addr);
	}

	/* Allocate string descriptor numbers ... note that string
	 * contents can be overridden by the composite_dev glue.
	 */

	status = usb_string_ids_tab(cdev, strings_dev);
	if (status < 0)
		goto fail;
	device_desc.iManufacturer = strings_dev[USB_GADGET_MANUFACTURER_IDX].id;
	device_desc.iProduct = strings_dev[USB_GADGET_PRODUCT_IDX].id;
	status = strings_dev[STRING_DESCRIPTION_IDX].id;
	friendlyarm_config_driver.iConfiguration = status;

	if (gadget_is_otg(cdev->gadget)) {
		friendlyarm_config_driver.descriptors = otg_desc;
		friendlyarm_config_driver.bmAttributes |= USB_CONFIG_ATT_WAKEUP;
	}

	/* register our configuration */
	if (use_acm) {
		status = friendlyarm_bind_config(cdev,
						&friendlyarm_config_driver, "acm");
		usb_ep_autoconfig_reset(cdev->gadget);
	} else {
		status = friendlyarm_bind_config(cdev,
						&friendlyarm_config_driver, "gser");
	}
	if (status < 0)
		goto fail;

	usb_composite_overwrite_options(cdev, &coverwrite);
	INFO(cdev, "%s\n", GS_VERSION_NAME);

	return 0;

fail:
	if (!IS_ERR_OR_NULL(fi_ecm))
		usb_put_function_instance(fi_ecm);

	return status;
}

static int friendlyarm_unbind(struct usb_composite_dev *cdev)
{
	int i;

	for (i = 0; i < n_ports; i++) {
		usb_put_function(f_serial[i]);
		usb_put_function_instance(fi_serial[i]);
	}
	if (!IS_ERR_OR_NULL(f_ecm))
		usb_put_function(f_ecm);
	if (!IS_ERR_OR_NULL(fi_ecm))
		usb_put_function_instance(fi_ecm);
	return 0;
}

static struct usb_composite_driver friendlyarm_driver = {
	.name		= "g_friendlyarm",
	.dev		= &device_desc,
	.strings	= dev_strings,
	.max_speed	= USB_SPEED_SUPER,
	.bind		= friendlyarm_bind,
	.unbind		= friendlyarm_unbind,
};

static int __init init(void)
{
	/* We *could* export two configs; that'd be much cleaner...
	 * but neither of these product IDs was defined that way.
	 */
	if (use_acm) {
		friendlyarm_config_driver.label = "FriendlyARM (ACM + ECM)";
		friendlyarm_config_driver.bConfigurationValue = 2;
		device_desc.bDeviceClass = USB_CLASS_COMM;
		device_desc.idProduct =
				cpu_to_le16(GS_CDC_PRODUCT_ID);
	} else {
		friendlyarm_config_driver.label = "FriendlyARM (Generic Serial + ECM)";
		friendlyarm_config_driver.bConfigurationValue = 1;
		device_desc.bDeviceClass = USB_CLASS_VENDOR_SPEC;
		device_desc.idProduct =
				cpu_to_le16(GS_PRODUCT_ID);
	}
	strings_dev[STRING_DESCRIPTION_IDX].s = friendlyarm_config_driver.label;

	return usb_composite_probe(&friendlyarm_driver);
}
module_init(init);

static void __exit cleanup(void)
{
	usb_composite_unregister(&friendlyarm_driver);
}
module_exit(cleanup);
