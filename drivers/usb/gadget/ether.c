/*
 * ether.c -- Ethernet gadget driver, with CDC and non-CDC options
 *
 * Copyright (C) 2003-2005,2008 David Brownell
 * Copyright (C) 2003-2004 Robert Schwebel, Benedikt Spranger
 * Copyright (C) 2008 Nokia Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* #define VERBOSE_DEBUG */

#include <linux/kernel.h>
#include <linux/netdevice.h>

#if defined USB_ETH_RNDIS
#  undef USB_ETH_RNDIS
#endif
#ifdef CONFIG_USB_ETH_RNDIS
#  define USB_ETH_RNDIS y
#endif

#include "u_ether.h"


/*
 * Ethernet gadget driver -- with CDC and non-CDC options
 * Builds on hardware support for a full duplex link.
 *
 * CDC Ethernet is the standard USB solution for sending Ethernet frames
 * using USB.  Real hardware tends to use the same framing protocol but look
 * different for control features.  This driver strongly prefers to use
 * this USB-IF standard as its open-systems interoperability solution;
 * most host side USB stacks (except from Microsoft) support it.
 *
 * This is sometimes called "CDC ECM" (Ethernet Control Model) to support
 * TLA-soup.  "CDC ACM" (Abstract Control Model) is for modems, and a new
 * "CDC EEM" (Ethernet Emulation Model) is starting to spread.
 *
 * There's some hardware that can't talk CDC ECM.  We make that hardware
 * implement a "minimalist" vendor-agnostic CDC core:  same framing, but
 * link-level setup only requires activating the configuration.  Only the
 * endpoint descriptors, and product/vendor IDs, are relevant; no control
 * operations are available.  Linux supports it, but other host operating
 * systems may not.  (This is a subset of CDC Ethernet.)
 *
 * It turns out that if you add a few descriptors to that "CDC Subset",
 * (Windows) host side drivers from MCCI can treat it as one submode of
 * a proprietary scheme called "SAFE" ... without needing to know about
 * specific product/vendor IDs.  So we do that, making it easier to use
 * those MS-Windows drivers.  Those added descriptors make it resemble a
 * CDC MDLM device, but they don't change device behavior at all.  (See
 * MCCI Engineering report 950198 "SAFE Networking Functions".)
 *
 * A third option is also in use.  Rather than CDC Ethernet, or something
 * simpler, Microsoft pushes their own approach: RNDIS.  The published
 * RNDIS specs are ambiguous and appear to be incomplete, and are also
 * needlessly complex.  They borrow more from CDC ACM than CDC ECM.
 */

#define DRIVER_DESC		"Ethernet Gadget"
#define DRIVER_VERSION		"Memorial Day 2008"

#ifdef USB_ETH_RNDIS
#define PREFIX			"RNDIS/"
#else
#define PREFIX			""
#endif

/*
 * This driver aims for interoperability by using CDC ECM unless
 *
 *		can_support_ecm()
 *
 * returns false, in which case it supports the CDC Subset.  By default,
 * that returns true; most hardware has no problems with CDC ECM, that's
 * a good default.  Previous versions of this driver had no default; this
 * version changes that, removing overhead for new controller support.
 *
 *	IF YOUR HARDWARE CAN'T SUPPORT CDC ECM, UPDATE THAT ROUTINE!
 */

static inline bool has_rndis(void)
{
#ifdef	USB_ETH_RNDIS
	return true;
#else
	return false;
#endif
}

#include <linux/module.h>

#include "u_ecm.h"
#include "u_gether.h"
#ifdef	USB_ETH_RNDIS
#include "u_rndis.h"
#include "rndis.h"
#else
#define rndis_borrow_net(...) do {} while (0)
#endif
#include "u_eem.h"

/*-------------------------------------------------------------------------*/
USB_GADGET_COMPOSITE_OPTIONS();

USB_ETHERNET_MODULE_PARAMETERS();

/* DO NOT REUSE THESE IDs with a protocol-incompatible driver!!  Ever!!
 * Instead:  allocate your own, using normal USB-IF procedures.
 */

/* Thanks to NetChip Technologies for donating this product ID.
 * It's for devices with only CDC Ethernet configurations.
 */
#define CDC_VENDOR_NUM		0x0525	/* NetChip */
#define CDC_PRODUCT_NUM		0xa4a1	/* Linux-USB Ethernet Gadget */

/* For hardware that can't talk CDC, we use the same vendor ID that
 * ARM Linux has used for ethernet-over-usb, both with sa1100 and
 * with pxa250.  We're protocol-compatible, if the host-side drivers
 * use the endpoint descriptors.  bcdDevice (version) is nonzero, so
 * drivers that need to hard-wire endpoint numbers have a hook.
 *
 * The protocol is a minimal subset of CDC Ether, which works on any bulk
 * hardware that's not deeply broken ... even on hardware that can't talk
 * RNDIS (like SA-1100, with no interrupt endpoint, or anything that
 * doesn't handle control-OUT).
 */
#define	SIMPLE_VENDOR_NUM	0x049f
#define	SIMPLE_PRODUCT_NUM	0x505a

/* For hardware that can talk RNDIS and either of the above protocols,
 * use this ID ... the windows INF files will know it.  Unless it's
 * used with CDC Ethernet, Linux 2.4 hosts will need updates to choose
 * the non-RNDIS configuration.
 */
#define RNDIS_VENDOR_NUM	0x0525	/* NetChip */
#define RNDIS_PRODUCT_NUM	0xa4a2	/* Ethernet/RNDIS Gadget */

/* For EEM gadgets */
#define EEM_VENDOR_NUM		0x1d6b	/* Linux Foundation */
#define EEM_PRODUCT_NUM		0x0102	/* EEM Gadget */

/*-------------------------------------------------------------------------*/

static struct usb_device_descriptor device_desc = {
	.bLength =		sizeof device_desc,
	.bDescriptorType =	USB_DT_DEVICE,

	.bcdUSB =		cpu_to_le16 (0x0200),

	.bDeviceClass =		USB_CLASS_COMM,
	.bDeviceSubClass =	0,
	.bDeviceProtocol =	0,
	/* .bMaxPacketSize0 = f(hardware) */

	/* Vendor and product id defaults change according to what configs
	 * we support.  (As does bNumConfigurations.)  These values can
	 * also be overridden by module parameters.
	 */
	.idVendor =		cpu_to_le16 (CDC_VENDOR_NUM),
	.idProduct =		cpu_to_le16 (CDC_PRODUCT_NUM),
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

static struct usb_string strings_dev[] = {
	[USB_GADGET_MANUFACTURER_IDX].s = "",
	[USB_GADGET_PRODUCT_IDX].s = PREFIX DRIVER_DESC,
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

static struct usb_function_instance *fi_ecm;
static struct usb_function *f_ecm;

static struct usb_function_instance *fi_eem;
static struct usb_function *f_eem;

static struct usb_function_instance *fi_geth;
static struct usb_function *f_geth;

static struct usb_function_instance *fi_rndis;
static struct usb_function *f_rndis;

/*-------------------------------------------------------------------------*/

/*
 * We may not have an RNDIS configuration, but if we do it needs to be
 * the first one present.  That's to make Microsoft's drivers happy,
 * and to follow DOCSIS 1.0 (cable modem standard).
 */
static int __init rndis_do_config(struct usb_configuration *c)
{
	int status;

	/* FIXME alloc iConfiguration string, set it in c->strings */

	if (gadget_is_otg(c->cdev->gadget)) {
		c->descriptors = otg_desc;
		c->bmAttributes |= USB_CONFIG_ATT_WAKEUP;
	}

	f_rndis = usb_get_function(fi_rndis);
	if (IS_ERR(f_rndis))
		return PTR_ERR(f_rndis);

	status = usb_add_function(c, f_rndis);
	if (status < 0)
		usb_put_function(f_rndis);

	return status;
}

static struct usb_configuration rndis_config_driver = {
	.label			= "RNDIS",
	.bConfigurationValue	= 2,
	/* .iConfiguration = DYNAMIC */
	.bmAttributes		= USB_CONFIG_ATT_SELFPOWER,
};

/*-------------------------------------------------------------------------*/

#ifdef CONFIG_USB_ETH_EEM
static bool use_eem = 1;
#else
static bool use_eem;
#endif
module_param(use_eem, bool, 0);
MODULE_PARM_DESC(use_eem, "use CDC EEM mode");

/*
 * We _always_ have an ECM, CDC Subset, or EEM configuration.
 */
static int __init eth_do_config(struct usb_configuration *c)
{
	int status = 0;

	/* FIXME alloc iConfiguration string, set it in c->strings */

	if (gadget_is_otg(c->cdev->gadget)) {
		c->descriptors = otg_desc;
		c->bmAttributes |= USB_CONFIG_ATT_WAKEUP;
	}

	if (use_eem) {
		f_eem = usb_get_function(fi_eem);
		if (IS_ERR(f_eem))
			return PTR_ERR(f_eem);

		status = usb_add_function(c, f_eem);
		if (status < 0)
			usb_put_function(f_eem);

		return status;
	} else if (can_support_ecm(c->cdev->gadget)) {
		f_ecm = usb_get_function(fi_ecm);
		if (IS_ERR(f_ecm))
			return PTR_ERR(f_ecm);

		status = usb_add_function(c, f_ecm);
		if (status < 0)
			usb_put_function(f_ecm);

		return status;
	} else {
		f_geth = usb_get_function(fi_geth);
		if (IS_ERR(f_geth))
			return PTR_ERR(f_geth);

		status = usb_add_function(c, f_geth);
		if (status < 0)
			usb_put_function(f_geth);

		return status;
	}

}

static struct usb_configuration eth_config_driver = {
	/* .label = f(hardware) */
	.bConfigurationValue	= 1,
	/* .iConfiguration = DYNAMIC */
	.bmAttributes		= USB_CONFIG_ATT_SELFPOWER,
};

/*-------------------------------------------------------------------------*/

static int __init eth_bind(struct usb_composite_dev *cdev)
{
	struct usb_gadget	*gadget = cdev->gadget;
	struct f_eem_opts	*eem_opts = NULL;
	struct f_ecm_opts	*ecm_opts = NULL;
	struct f_gether_opts	*geth_opts = NULL;
	struct net_device	*net;
	int			status;

	/* set up main config label and device descriptor */
	if (use_eem) {
		/* EEM */
		fi_eem = usb_get_function_instance("eem");
		if (IS_ERR(fi_eem))
			return PTR_ERR(fi_eem);

		eem_opts = container_of(fi_eem, struct f_eem_opts, func_inst);

		net = eem_opts->net;

		eth_config_driver.label = "CDC Ethernet (EEM)";
		device_desc.idVendor = cpu_to_le16(EEM_VENDOR_NUM);
		device_desc.idProduct = cpu_to_le16(EEM_PRODUCT_NUM);
	} else if (can_support_ecm(gadget)) {
		/* ECM */

		fi_ecm = usb_get_function_instance("ecm");
		if (IS_ERR(fi_ecm))
			return PTR_ERR(fi_ecm);

		ecm_opts = container_of(fi_ecm, struct f_ecm_opts, func_inst);

		net = ecm_opts->net;

		eth_config_driver.label = "CDC Ethernet (ECM)";
	} else {
		/* CDC Subset */

		fi_geth = usb_get_function_instance("geth");
		if (IS_ERR(fi_geth))
			return PTR_ERR(fi_geth);

		geth_opts = container_of(fi_geth, struct f_gether_opts,
					 func_inst);

		net = geth_opts->net;

		eth_config_driver.label = "CDC Subset/SAFE";

		device_desc.idVendor = cpu_to_le16(SIMPLE_VENDOR_NUM);
		device_desc.idProduct = cpu_to_le16(SIMPLE_PRODUCT_NUM);
		if (!has_rndis())
			device_desc.bDeviceClass = USB_CLASS_VENDOR_SPEC;
	}

	gether_set_qmult(net, qmult);
	if (!gether_set_host_addr(net, host_addr))
		pr_info("using host ethernet address: %s", host_addr);
	if (!gether_set_dev_addr(net, dev_addr))
		pr_info("using self ethernet address: %s", dev_addr);

	if (has_rndis()) {
		/* RNDIS plus ECM-or-Subset */
		gether_set_gadget(net, cdev->gadget);
		status = gether_register_netdev(net);
		if (status)
			goto fail;

		if (use_eem)
			eem_opts->bound = true;
		else if (can_support_ecm(gadget))
			ecm_opts->bound = true;
		else
			geth_opts->bound = true;

		fi_rndis = usb_get_function_instance("rndis");
		if (IS_ERR(fi_rndis)) {
			status = PTR_ERR(fi_rndis);
			goto fail;
		}

		rndis_borrow_net(fi_rndis, net);

		device_desc.idVendor = cpu_to_le16(RNDIS_VENDOR_NUM);
		device_desc.idProduct = cpu_to_le16(RNDIS_PRODUCT_NUM);
		device_desc.bNumConfigurations = 2;
	}

	/* Allocate string descriptor numbers ... note that string
	 * contents can be overridden by the composite_dev glue.
	 */

	status = usb_string_ids_tab(cdev, strings_dev);
	if (status < 0)
		goto fail1;
	device_desc.iManufacturer = strings_dev[USB_GADGET_MANUFACTURER_IDX].id;
	device_desc.iProduct = strings_dev[USB_GADGET_PRODUCT_IDX].id;

	/* register our configuration(s); RNDIS first, if it's used */
	if (has_rndis()) {
		status = usb_add_config(cdev, &rndis_config_driver,
				rndis_do_config);
		if (status < 0)
			goto fail1;
	}

	status = usb_add_config(cdev, &eth_config_driver, eth_do_config);
	if (status < 0)
		goto fail1;

	usb_composite_overwrite_options(cdev, &coverwrite);
	dev_info(&gadget->dev, "%s, version: " DRIVER_VERSION "\n",
			DRIVER_DESC);

	return 0;

fail1:
	if (has_rndis())
		usb_put_function_instance(fi_rndis);
fail:
	if (use_eem)
		usb_put_function_instance(fi_eem);
	else if (can_support_ecm(gadget))
		usb_put_function_instance(fi_ecm);
	else
		usb_put_function_instance(fi_geth);
	return status;
}

static int __exit eth_unbind(struct usb_composite_dev *cdev)
{
	if (has_rndis())
		usb_put_function_instance(fi_rndis);
	if (use_eem)
		usb_put_function_instance(fi_eem);
	else if (can_support_ecm(cdev->gadget))
		usb_put_function_instance(fi_ecm);
	else
		usb_put_function_instance(fi_geth);
	return 0;
}

static __refdata struct usb_composite_driver eth_driver = {
	.name		= "g_ether",
	.dev		= &device_desc,
	.strings	= dev_strings,
	.max_speed	= USB_SPEED_SUPER,
	.bind		= eth_bind,
	.unbind		= __exit_p(eth_unbind),
};

MODULE_DESCRIPTION(PREFIX DRIVER_DESC);
MODULE_AUTHOR("David Brownell, Benedikt Spanger");
MODULE_LICENSE("GPL");

static int __init init(void)
{
	return usb_composite_probe(&eth_driver);
}
module_init(init);

static void __exit cleanup(void)
{
	usb_composite_unregister(&eth_driver);
}
module_exit(cleanup);
