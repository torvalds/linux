/*
 * multi.c -- Multifunction Composite driver
 *
 * Copyright (C) 2008 David Brownell
 * Copyright (C) 2008 Nokia Corporation
 * Copyright (C) 2009 Samsung Electronics
 * Author: Michal Nazarewicz (m.nazarewicz@samsung.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */


#include <linux/kernel.h>
#include <linux/utsname.h>


#if defined USB_ETH_RNDIS
#  undef USB_ETH_RNDIS
#endif
#ifdef CONFIG_USB_G_MULTI_RNDIS
#  define USB_ETH_RNDIS y
#endif


#define DRIVER_DESC		"Multifunction Composite Gadget"
#define DRIVER_VERSION		"2009/07/21"

/*-------------------------------------------------------------------------*/

#define MULTI_VENDOR_NUM	0x0525	/* XXX NetChip */
#define MULTI_PRODUCT_NUM	0xa4ab	/* XXX */

/*-------------------------------------------------------------------------*/

/*
 * kbuild is not very cooperative with respect to linking separately
 * compiled library objects into one module.  So for now we won't use
 * separate compilation ... ensuring init/exit sections work to shrink
 * the runtime footprint, and giving us at least some parts of what
 * a "gcc --combine ... part1.c part2.c part3.c ... " build would.
 */

#include "composite.c"
#include "usbstring.c"
#include "config.c"
#include "epautoconf.c"

#include "u_serial.c"
#include "f_acm.c"

#include "f_ecm.c"
#include "f_subset.c"
#ifdef USB_ETH_RNDIS
#  include "f_rndis.c"
#  include "rndis.c"
#endif
#include "u_ether.c"

#undef DBG     /* u_ether.c has broken idea about macros */
#undef VDBG    /* so clean up after it */
#undef ERROR
#undef INFO
#include "f_mass_storage.c"

/*-------------------------------------------------------------------------*/

static struct usb_device_descriptor device_desc = {
	.bLength =		sizeof device_desc,
	.bDescriptorType =	USB_DT_DEVICE,

	.bcdUSB =		cpu_to_le16(0x0200),

	/* .bDeviceClass =		USB_CLASS_COMM, */
	/* .bDeviceSubClass =	0, */
	/* .bDeviceProtocol =	0, */
	.bDeviceClass =		0xEF,
	.bDeviceSubClass =	2,
	.bDeviceProtocol =	1,
	/* .bMaxPacketSize0 = f(hardware) */

	/* Vendor and product id can be overridden by module parameters.  */
	.idVendor =		cpu_to_le16(MULTI_VENDOR_NUM),
	.idProduct =		cpu_to_le16(MULTI_PRODUCT_NUM),
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

#define STRING_MANUFACTURER_IDX		0
#define STRING_PRODUCT_IDX		1

static char manufacturer[50];

static struct usb_string strings_dev[] = {
	[STRING_MANUFACTURER_IDX].s = manufacturer,
	[STRING_PRODUCT_IDX].s = DRIVER_DESC,
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

static u8 hostaddr[ETH_ALEN];



/****************************** Configurations ******************************/

static struct fsg_module_parameters mod_data = {
	.stall = 1
};
FSG_MODULE_PARAMETERS(/* no prefix */, mod_data);

static struct fsg_common *fsg_common;


#ifdef USB_ETH_RNDIS

static int __init rndis_do_config(struct usb_configuration *c)
{
	int ret;

	if (gadget_is_otg(c->cdev->gadget)) {
		c->descriptors = otg_desc;
		c->bmAttributes |= USB_CONFIG_ATT_WAKEUP;
	}

	ret = rndis_bind_config(c, hostaddr);
	if (ret < 0)
		return ret;

	ret = acm_bind_config(c, 0);
	if (ret < 0)
		return ret;

	ret = fsg_add(c->cdev, c, fsg_common);
	if (ret < 0)
		return ret;

	return 0;
}

static struct usb_configuration rndis_config_driver = {
	.label			= "Multifunction Composite (RNDIS + MS + ACM)",
	.bind			= rndis_do_config,
	.bConfigurationValue	= 2,
	/* .iConfiguration = DYNAMIC */
	.bmAttributes		= USB_CONFIG_ATT_SELFPOWER,
};

#endif

#ifdef CONFIG_USB_G_MULTI_CDC

static int __init cdc_do_config(struct usb_configuration *c)
{
	int ret;

	if (gadget_is_otg(c->cdev->gadget)) {
		c->descriptors = otg_desc;
		c->bmAttributes |= USB_CONFIG_ATT_WAKEUP;
	}

	ret = ecm_bind_config(c, hostaddr);
	if (ret < 0)
		return ret;

	ret = acm_bind_config(c, 0);
	if (ret < 0)
		return ret;

	ret = fsg_add(c->cdev, c, fsg_common);
	if (ret < 0)
		return ret;
	if (ret < 0)
		return ret;

	return 0;
}

static struct usb_configuration cdc_config_driver = {
	.label			= "Multifunction Composite (CDC + MS + ACM)",
	.bind			= cdc_do_config,
	.bConfigurationValue	= 1,
	/* .iConfiguration = DYNAMIC */
	.bmAttributes		= USB_CONFIG_ATT_SELFPOWER,
};

#endif



/****************************** Gadget Bind ******************************/


static int __init multi_bind(struct usb_composite_dev *cdev)
{
	struct usb_gadget *gadget = cdev->gadget;
	int status, gcnum;

	if (!can_support_ecm(cdev->gadget)) {
		dev_err(&gadget->dev, "controller '%s' not usable\n",
		        gadget->name);
		return -EINVAL;
	}

	/* set up network link layer */
	status = gether_setup(cdev->gadget, hostaddr);
	if (status < 0)
		return status;

	/* set up serial link layer */
	status = gserial_setup(cdev->gadget, 1);
	if (status < 0)
		goto fail0;

	/* set up mass storage function */
	fsg_common = fsg_common_from_params(0, cdev, &mod_data);
	if (IS_ERR(fsg_common)) {
		status = PTR_ERR(fsg_common);
		goto fail1;
	}


	gcnum = usb_gadget_controller_number(gadget);
	if (gcnum >= 0)
		device_desc.bcdDevice = cpu_to_le16(0x0300 | gcnum);
	else {
		/* We assume that can_support_ecm() tells the truth;
		 * but if the controller isn't recognized at all then
		 * that assumption is a bit more likely to be wrong.
		 */
		WARNING(cdev, "controller '%s' not recognized\n",
		        gadget->name);
		device_desc.bcdDevice = cpu_to_le16(0x0300 | 0x0099);
	}


	/* Allocate string descriptor numbers ... note that string
	 * contents can be overridden by the composite_dev glue.
	 */

	/* device descriptor strings: manufacturer, product */
	snprintf(manufacturer, sizeof manufacturer, "%s %s with %s",
	         init_utsname()->sysname, init_utsname()->release,
	         gadget->name);
	status = usb_string_id(cdev);
	if (status < 0)
		goto fail2;
	strings_dev[STRING_MANUFACTURER_IDX].id = status;
	device_desc.iManufacturer = status;

	status = usb_string_id(cdev);
	if (status < 0)
		goto fail2;
	strings_dev[STRING_PRODUCT_IDX].id = status;
	device_desc.iProduct = status;

#ifdef USB_ETH_RNDIS
	/* register our first configuration */
	status = usb_add_config(cdev, &rndis_config_driver);
	if (status < 0)
		goto fail2;
#endif

#ifdef CONFIG_USB_G_MULTI_CDC
	/* register our second configuration */
	status = usb_add_config(cdev, &cdc_config_driver);
	if (status < 0)
		goto fail2;
#endif

	dev_info(&gadget->dev, DRIVER_DESC ", version: " DRIVER_VERSION "\n");
	fsg_common_put(fsg_common);
	return 0;

fail2:
	fsg_common_put(fsg_common);
fail1:
	gserial_cleanup();
fail0:
	gether_cleanup();
	return status;
}

static int __exit multi_unbind(struct usb_composite_dev *cdev)
{
	gserial_cleanup();
	gether_cleanup();
	return 0;
}


/****************************** Some noise ******************************/


static struct usb_composite_driver multi_driver = {
	.name		= "g_multi",
	.dev		= &device_desc,
	.strings	= dev_strings,
	.bind		= multi_bind,
	.unbind		= __exit_p(multi_unbind),
};

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR("Michal Nazarewicz");
MODULE_LICENSE("GPL");

static int __init g_multi_init(void)
{
	return usb_composite_register(&multi_driver);
}
module_init(g_multi_init);

static void __exit g_multi_cleanup(void)
{
	usb_composite_unregister(&multi_driver);
}
module_exit(g_multi_cleanup);
