/*
 * f_subset.c -- "CDC Subset" Ethernet link function driver
 *
 * Copyright (C) 2003-2005,2008 David Brownell
 * Copyright (C) 2008 Nokia Corporation
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

#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/etherdevice.h>

#include "u_ether.h"


/*
 * This function packages a simple "CDC Subset" Ethernet port with no real
 * control mechanisms; just raw data transfer over two bulk endpoints.
 * The data transfer model is exactly that of CDC Ethernet, which is
 * why we call it the "CDC Subset".
 *
 * Because it's not standardized, this has some interoperability issues.
 * They mostly relate to driver binding, since the data transfer model is
 * so simple (CDC Ethernet).  The original versions of this protocol used
 * specific product/vendor IDs:  byteswapped IDs for Digital Equipment's
 * SA-1100 "Itsy" board, which could run Linux 2.4 kernels and supported
 * daughtercards with USB peripheral connectors.  (It was used more often
 * with other boards, using the Itsy identifiers.)  Linux hosts recognized
 * this with CONFIG_USB_ARMLINUX; these devices have only one configuration
 * and one interface.
 *
 * At some point, MCCI defined a (nonconformant) CDC MDLM variant called
 * "SAFE", which happens to have a mode which is identical to the "CDC
 * Subset" in terms of data transfer and lack of control model.  This was
 * adopted by later Sharp Zaurus models, and by some other software which
 * Linux hosts recognize with CONFIG_USB_NET_ZAURUS.
 *
 * Because Microsoft's RNDIS drivers are far from robust, we added a few
 * descriptors to the CDC Subset code, making this code look like a SAFE
 * implementation.  This lets you use MCCI's host side MS-Windows drivers
 * if you get fed up with RNDIS.  It also makes it easier for composite
 * drivers to work, since they can use class based binding instead of
 * caring about specific product and vendor IDs.
 */

struct geth_descs {
	struct usb_endpoint_descriptor	*in;
	struct usb_endpoint_descriptor	*out;
};

struct f_gether {
	struct gether			port;

	char				ethaddr[14];

	struct geth_descs		fs;
	struct geth_descs		hs;
};

static inline struct f_gether *func_to_geth(struct usb_function *f)
{
	return container_of(f, struct f_gether, port.func);
}

/*-------------------------------------------------------------------------*/

/*
 * "Simple" CDC-subset option is a simple vendor-neutral model that most
 * full speed controllers can handle:  one interface, two bulk endpoints.
 * To assist host side drivers, we fancy it up a bit, and add descriptors so
 * some host side drivers will understand it as a "SAFE" variant.
 *
 * "SAFE" loosely follows CDC WMC MDLM, violating the spec in various ways.
 * Data endpoints live in the control interface, there's no data interface.
 * And it's not used to talk to a cell phone radio.
 */

/* interface descriptor: */

static struct usb_interface_descriptor subset_data_intf __initdata = {
	.bLength =		sizeof subset_data_intf,
	.bDescriptorType =	USB_DT_INTERFACE,

	/* .bInterfaceNumber = DYNAMIC */
	.bAlternateSetting =	0,
	.bNumEndpoints =	2,
	.bInterfaceClass =      USB_CLASS_COMM,
	.bInterfaceSubClass =	USB_CDC_SUBCLASS_MDLM,
	.bInterfaceProtocol =	0,
	/* .iInterface = DYNAMIC */
};

static struct usb_cdc_header_desc mdlm_header_desc __initdata = {
	.bLength =		sizeof mdlm_header_desc,
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubType =	USB_CDC_HEADER_TYPE,

	.bcdCDC =		cpu_to_le16(0x0110),
};

static struct usb_cdc_mdlm_desc mdlm_desc __initdata = {
	.bLength =		sizeof mdlm_desc,
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubType =	USB_CDC_MDLM_TYPE,

	.bcdVersion =		cpu_to_le16(0x0100),
	.bGUID = {
		0x5d, 0x34, 0xcf, 0x66, 0x11, 0x18, 0x11, 0xd6,
		0xa2, 0x1a, 0x00, 0x01, 0x02, 0xca, 0x9a, 0x7f,
	},
};

/* since "usb_cdc_mdlm_detail_desc" is a variable length structure, we
 * can't really use its struct.  All we do here is say that we're using
 * the submode of "SAFE" which directly matches the CDC Subset.
 */
static u8 mdlm_detail_desc[] __initdata = {
	6,
	USB_DT_CS_INTERFACE,
	USB_CDC_MDLM_DETAIL_TYPE,

	0,	/* "SAFE" */
	0,	/* network control capabilities (none) */
	0,	/* network data capabilities ("raw" encapsulation) */
};

static struct usb_cdc_ether_desc ether_desc __initdata = {
	.bLength =		sizeof ether_desc,
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubType =	USB_CDC_ETHERNET_TYPE,

	/* this descriptor actually adds value, surprise! */
	/* .iMACAddress = DYNAMIC */
	.bmEthernetStatistics =	cpu_to_le32(0), /* no statistics */
	.wMaxSegmentSize =	cpu_to_le16(ETH_FRAME_LEN),
	.wNumberMCFilters =	cpu_to_le16(0),
	.bNumberPowerFilters =	0,
};

/* full speed support: */

static struct usb_endpoint_descriptor fs_subset_in_desc __initdata = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
};

static struct usb_endpoint_descriptor fs_subset_out_desc __initdata = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_OUT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
};

static struct usb_descriptor_header *fs_eth_function[] __initdata = {
	(struct usb_descriptor_header *) &subset_data_intf,
	(struct usb_descriptor_header *) &mdlm_header_desc,
	(struct usb_descriptor_header *) &mdlm_desc,
	(struct usb_descriptor_header *) &mdlm_detail_desc,
	(struct usb_descriptor_header *) &ether_desc,
	(struct usb_descriptor_header *) &fs_subset_in_desc,
	(struct usb_descriptor_header *) &fs_subset_out_desc,
	NULL,
};

/* high speed support: */

static struct usb_endpoint_descriptor hs_subset_in_desc __initdata = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(512),
};

static struct usb_endpoint_descriptor hs_subset_out_desc __initdata = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(512),
};

static struct usb_descriptor_header *hs_eth_function[] __initdata = {
	(struct usb_descriptor_header *) &subset_data_intf,
	(struct usb_descriptor_header *) &mdlm_header_desc,
	(struct usb_descriptor_header *) &mdlm_desc,
	(struct usb_descriptor_header *) &mdlm_detail_desc,
	(struct usb_descriptor_header *) &ether_desc,
	(struct usb_descriptor_header *) &hs_subset_in_desc,
	(struct usb_descriptor_header *) &hs_subset_out_desc,
	NULL,
};

/* string descriptors: */

static struct usb_string geth_string_defs[] = {
	[0].s = "CDC Ethernet Subset/SAFE",
	[1].s = NULL /* DYNAMIC */,
	{  } /* end of list */
};

static struct usb_gadget_strings geth_string_table = {
	.language =		0x0409,	/* en-us */
	.strings =		geth_string_defs,
};

static struct usb_gadget_strings *geth_strings[] = {
	&geth_string_table,
	NULL,
};

/*-------------------------------------------------------------------------*/

static int geth_set_alt(struct usb_function *f, unsigned intf, unsigned alt)
{
	struct f_gether		*geth = func_to_geth(f);
	struct usb_composite_dev *cdev = f->config->cdev;
	struct net_device	*net;

	/* we know alt == 0, so this is an activation or a reset */

	if (geth->port.in_ep->driver_data) {
		DBG(cdev, "reset cdc subset\n");
		gether_disconnect(&geth->port);
	}

	DBG(cdev, "init + activate cdc subset\n");
	geth->port.in = ep_choose(cdev->gadget,
			geth->hs.in, geth->fs.in);
	geth->port.out = ep_choose(cdev->gadget,
			geth->hs.out, geth->fs.out);

	net = gether_connect(&geth->port);
	return IS_ERR(net) ? PTR_ERR(net) : 0;
}

static void geth_disable(struct usb_function *f)
{
	struct f_gether	*geth = func_to_geth(f);
	struct usb_composite_dev *cdev = f->config->cdev;

	DBG(cdev, "net deactivated\n");
	gether_disconnect(&geth->port);
}

/*-------------------------------------------------------------------------*/

/* serial function driver setup/binding */

static int __init
geth_bind(struct usb_configuration *c, struct usb_function *f)
{
	struct usb_composite_dev *cdev = c->cdev;
	struct f_gether		*geth = func_to_geth(f);
	int			status;
	struct usb_ep		*ep;

	/* allocate instance-specific interface IDs */
	status = usb_interface_id(c, f);
	if (status < 0)
		goto fail;
	subset_data_intf.bInterfaceNumber = status;

	status = -ENODEV;

	/* allocate instance-specific endpoints */
	ep = usb_ep_autoconfig(cdev->gadget, &fs_subset_in_desc);
	if (!ep)
		goto fail;
	geth->port.in_ep = ep;
	ep->driver_data = cdev;	/* claim */

	ep = usb_ep_autoconfig(cdev->gadget, &fs_subset_out_desc);
	if (!ep)
		goto fail;
	geth->port.out_ep = ep;
	ep->driver_data = cdev;	/* claim */

	/* copy descriptors, and track endpoint copies */
	f->descriptors = usb_copy_descriptors(fs_eth_function);

	geth->fs.in = usb_find_endpoint(fs_eth_function,
			f->descriptors, &fs_subset_in_desc);
	geth->fs.out = usb_find_endpoint(fs_eth_function,
			f->descriptors, &fs_subset_out_desc);


	/* support all relevant hardware speeds... we expect that when
	 * hardware is dual speed, all bulk-capable endpoints work at
	 * both speeds
	 */
	if (gadget_is_dualspeed(c->cdev->gadget)) {
		hs_subset_in_desc.bEndpointAddress =
				fs_subset_in_desc.bEndpointAddress;
		hs_subset_out_desc.bEndpointAddress =
				fs_subset_out_desc.bEndpointAddress;

		/* copy descriptors, and track endpoint copies */
		f->hs_descriptors = usb_copy_descriptors(hs_eth_function);

		geth->hs.in = usb_find_endpoint(hs_eth_function,
				f->hs_descriptors, &hs_subset_in_desc);
		geth->hs.out = usb_find_endpoint(hs_eth_function,
				f->hs_descriptors, &hs_subset_out_desc);
	}

	/* NOTE:  all that is done without knowing or caring about
	 * the network link ... which is unavailable to this code
	 * until we're activated via set_alt().
	 */

	DBG(cdev, "CDC Subset: %s speed IN/%s OUT/%s\n",
			gadget_is_dualspeed(c->cdev->gadget) ? "dual" : "full",
			geth->port.in_ep->name, geth->port.out_ep->name);
	return 0;

fail:
	/* we might as well release our claims on endpoints */
	if (geth->port.out)
		geth->port.out_ep->driver_data = NULL;
	if (geth->port.in)
		geth->port.in_ep->driver_data = NULL;

	ERROR(cdev, "%s: can't bind, err %d\n", f->name, status);

	return status;
}

static void
geth_unbind(struct usb_configuration *c, struct usb_function *f)
{
	if (gadget_is_dualspeed(c->cdev->gadget))
		usb_free_descriptors(f->hs_descriptors);
	usb_free_descriptors(f->descriptors);
	geth_string_defs[1].s = NULL;
	kfree(func_to_geth(f));
}

/**
 * geth_bind_config - add CDC Subset network link to a configuration
 * @c: the configuration to support the network link
 * @ethaddr: a buffer in which the ethernet address of the host side
 *	side of the link was recorded
 * Context: single threaded during gadget setup
 *
 * Returns zero on success, else negative errno.
 *
 * Caller must have called @gether_setup().  Caller is also responsible
 * for calling @gether_cleanup() before module unload.
 */
int __init geth_bind_config(struct usb_configuration *c, u8 ethaddr[ETH_ALEN])
{
	struct f_gether	*geth;
	int		status;

	if (!ethaddr)
		return -EINVAL;

	/* maybe allocate device-global string IDs */
	if (geth_string_defs[0].id == 0) {

		/* interface label */
		status = usb_string_id(c->cdev);
		if (status < 0)
			return status;
		geth_string_defs[0].id = status;
		subset_data_intf.iInterface = status;

		/* MAC address */
		status = usb_string_id(c->cdev);
		if (status < 0)
			return status;
		geth_string_defs[1].id = status;
		ether_desc.iMACAddress = status;
	}

	/* allocate and initialize one new instance */
	geth = kzalloc(sizeof *geth, GFP_KERNEL);
	if (!geth)
		return -ENOMEM;

	/* export host's Ethernet address in CDC format */
	snprintf(geth->ethaddr, sizeof geth->ethaddr,
		"%02X%02X%02X%02X%02X%02X",
		ethaddr[0], ethaddr[1], ethaddr[2],
		ethaddr[3], ethaddr[4], ethaddr[5]);
	geth_string_defs[1].s = geth->ethaddr;

	geth->port.cdc_filter = DEFAULT_FILTER;

	geth->port.func.name = "cdc_subset";
	geth->port.func.strings = geth_strings;
	geth->port.func.bind = geth_bind;
	geth->port.func.unbind = geth_unbind;
	geth->port.func.set_alt = geth_set_alt;
	geth->port.func.disable = geth_disable;

	status = usb_add_function(c, &geth->port.func);
	if (status) {
		geth_string_defs[1].s = NULL;
		kfree(geth);
	}
	return status;
}
