/*
 * f_obex.c -- USB CDC OBEX function driver
 *
 * Copyright (C) 2008 Nokia Corporation
 * Contact: Felipe Balbi <felipe.balbi@nokia.com>
 *
 * Based on f_acm.c by Al Borchers and David Brownell.
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

/* #define VERBOSE_DEBUG */

#include <linux/kernel.h>
#include <linux/device.h>

#include "u_serial.h"
#include "gadget_chips.h"


/*
 * This CDC OBEX function support just packages a TTY-ish byte stream.
 * A user mode server will put it into "raw" mode and handle all the
 * relevant protocol details ... this is just a kernel passthrough.
 * When possible, we prevent gadget enumeration until that server is
 * ready to handle the commands.
 */

struct obex_ep_descs {
	struct usb_endpoint_descriptor	*obex_in;
	struct usb_endpoint_descriptor	*obex_out;
};

struct f_obex {
	struct gserial			port;
	u8				ctrl_id;
	u8				data_id;
	u8				port_num;
	u8				can_activate;

	struct obex_ep_descs		fs;
	struct obex_ep_descs		hs;
};

static inline struct f_obex *func_to_obex(struct usb_function *f)
{
	return container_of(f, struct f_obex, port.func);
}

static inline struct f_obex *port_to_obex(struct gserial *p)
{
	return container_of(p, struct f_obex, port);
}

/*-------------------------------------------------------------------------*/

#define OBEX_CTRL_IDX	0
#define OBEX_DATA_IDX	1

static struct usb_string obex_string_defs[] = {
	[OBEX_CTRL_IDX].s	= "CDC Object Exchange (OBEX)",
	[OBEX_DATA_IDX].s	= "CDC OBEX Data",
	{  },	/* end of list */
};

static struct usb_gadget_strings obex_string_table = {
	.language		= 0x0409,	/* en-US */
	.strings		= obex_string_defs,
};

static struct usb_gadget_strings *obex_strings[] = {
	&obex_string_table,
	NULL,
};

/*-------------------------------------------------------------------------*/

static struct usb_interface_descriptor obex_control_intf __initdata = {
	.bLength		= sizeof(obex_control_intf),
	.bDescriptorType	= USB_DT_INTERFACE,
	.bInterfaceNumber	= 0,

	.bAlternateSetting	= 0,
	.bNumEndpoints		= 0,
	.bInterfaceClass	= USB_CLASS_COMM,
	.bInterfaceSubClass	= USB_CDC_SUBCLASS_OBEX,
};

static struct usb_interface_descriptor obex_data_nop_intf __initdata = {
	.bLength		= sizeof(obex_data_nop_intf),
	.bDescriptorType	= USB_DT_INTERFACE,
	.bInterfaceNumber	= 1,

	.bAlternateSetting	= 0,
	.bNumEndpoints		= 0,
	.bInterfaceClass	= USB_CLASS_CDC_DATA,
};

static struct usb_interface_descriptor obex_data_intf __initdata = {
	.bLength		= sizeof(obex_data_intf),
	.bDescriptorType	= USB_DT_INTERFACE,
	.bInterfaceNumber	= 2,

	.bAlternateSetting	= 1,
	.bNumEndpoints		= 2,
	.bInterfaceClass	= USB_CLASS_CDC_DATA,
};

static struct usb_cdc_header_desc obex_cdc_header_desc __initdata = {
	.bLength		= sizeof(obex_cdc_header_desc),
	.bDescriptorType	= USB_DT_CS_INTERFACE,
	.bDescriptorSubType	= USB_CDC_HEADER_TYPE,
	.bcdCDC			= cpu_to_le16(0x0120),
};

static struct usb_cdc_union_desc obex_cdc_union_desc __initdata = {
	.bLength		= sizeof(obex_cdc_union_desc),
	.bDescriptorType	= USB_DT_CS_INTERFACE,
	.bDescriptorSubType	= USB_CDC_UNION_TYPE,
	.bMasterInterface0	= 1,
	.bSlaveInterface0	= 2,
};

static struct usb_cdc_obex_desc obex_desc __initdata = {
	.bLength		= sizeof(obex_desc),
	.bDescriptorType	= USB_DT_CS_INTERFACE,
	.bDescriptorSubType	= USB_CDC_OBEX_TYPE,
	.bcdVersion		= cpu_to_le16(0x0100),
};

/* High-Speed Support */

static struct usb_endpoint_descriptor obex_hs_ep_out_desc __initdata = {
	.bLength		= USB_DT_ENDPOINT_SIZE,
	.bDescriptorType	= USB_DT_ENDPOINT,

	.bEndpointAddress	= USB_DIR_OUT,
	.bmAttributes		= USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize		= cpu_to_le16(512),
};

static struct usb_endpoint_descriptor obex_hs_ep_in_desc __initdata = {
	.bLength		= USB_DT_ENDPOINT_SIZE,
	.bDescriptorType	= USB_DT_ENDPOINT,

	.bEndpointAddress	= USB_DIR_IN,
	.bmAttributes		= USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize		= cpu_to_le16(512),
};

static struct usb_descriptor_header *hs_function[] __initdata = {
	(struct usb_descriptor_header *) &obex_control_intf,
	(struct usb_descriptor_header *) &obex_cdc_header_desc,
	(struct usb_descriptor_header *) &obex_desc,
	(struct usb_descriptor_header *) &obex_cdc_union_desc,

	(struct usb_descriptor_header *) &obex_data_nop_intf,
	(struct usb_descriptor_header *) &obex_data_intf,
	(struct usb_descriptor_header *) &obex_hs_ep_in_desc,
	(struct usb_descriptor_header *) &obex_hs_ep_out_desc,
	NULL,
};

/* Full-Speed Support */

static struct usb_endpoint_descriptor obex_fs_ep_in_desc __initdata = {
	.bLength		= USB_DT_ENDPOINT_SIZE,
	.bDescriptorType	= USB_DT_ENDPOINT,

	.bEndpointAddress	= USB_DIR_IN,
	.bmAttributes		= USB_ENDPOINT_XFER_BULK,
};

static struct usb_endpoint_descriptor obex_fs_ep_out_desc __initdata = {
	.bLength		= USB_DT_ENDPOINT_SIZE,
	.bDescriptorType	= USB_DT_ENDPOINT,

	.bEndpointAddress	= USB_DIR_OUT,
	.bmAttributes		= USB_ENDPOINT_XFER_BULK,
};

static struct usb_descriptor_header *fs_function[] __initdata = {
	(struct usb_descriptor_header *) &obex_control_intf,
	(struct usb_descriptor_header *) &obex_cdc_header_desc,
	(struct usb_descriptor_header *) &obex_desc,
	(struct usb_descriptor_header *) &obex_cdc_union_desc,

	(struct usb_descriptor_header *) &obex_data_nop_intf,
	(struct usb_descriptor_header *) &obex_data_intf,
	(struct usb_descriptor_header *) &obex_fs_ep_in_desc,
	(struct usb_descriptor_header *) &obex_fs_ep_out_desc,
	NULL,
};

/*-------------------------------------------------------------------------*/

static int obex_set_alt(struct usb_function *f, unsigned intf, unsigned alt)
{
	struct f_obex		*obex = func_to_obex(f);
	struct usb_composite_dev *cdev = f->config->cdev;

	if (intf == obex->ctrl_id) {
		if (alt != 0)
			goto fail;
		/* NOP */
		DBG(cdev, "reset obex ttyGS%d control\n", obex->port_num);

	} else if (intf == obex->data_id) {
		if (alt > 1)
			goto fail;

		if (obex->port.in->driver_data) {
			DBG(cdev, "reset obex ttyGS%d\n", obex->port_num);
			gserial_disconnect(&obex->port);
		}

		if (!obex->port.in_desc) {
			DBG(cdev, "init obex ttyGS%d\n", obex->port_num);
			obex->port.in_desc = ep_choose(cdev->gadget,
					obex->hs.obex_in, obex->fs.obex_in);
			obex->port.out_desc = ep_choose(cdev->gadget,
					obex->hs.obex_out, obex->fs.obex_out);
		}

		if (alt == 1) {
			DBG(cdev, "activate obex ttyGS%d\n", obex->port_num);
			gserial_connect(&obex->port, obex->port_num);
		}

	} else
		goto fail;

	return 0;

fail:
	return -EINVAL;
}

static int obex_get_alt(struct usb_function *f, unsigned intf)
{
	struct f_obex		*obex = func_to_obex(f);

	if (intf == obex->ctrl_id)
		return 0;

	return obex->port.in->driver_data ? 1 : 0;
}

static void obex_disable(struct usb_function *f)
{
	struct f_obex	*obex = func_to_obex(f);
	struct usb_composite_dev *cdev = f->config->cdev;

	DBG(cdev, "obex ttyGS%d disable\n", obex->port_num);
	gserial_disconnect(&obex->port);
}

/*-------------------------------------------------------------------------*/

static void obex_connect(struct gserial *g)
{
	struct f_obex		*obex = port_to_obex(g);
	struct usb_composite_dev *cdev = g->func.config->cdev;
	int			status;

	if (!obex->can_activate)
		return;

	status = usb_function_activate(&g->func);
	if (status)
		DBG(cdev, "obex ttyGS%d function activate --> %d\n",
			obex->port_num, status);
}

static void obex_disconnect(struct gserial *g)
{
	struct f_obex		*obex = port_to_obex(g);
	struct usb_composite_dev *cdev = g->func.config->cdev;
	int			status;

	if (!obex->can_activate)
		return;

	status = usb_function_deactivate(&g->func);
	if (status)
		DBG(cdev, "obex ttyGS%d function deactivate --> %d\n",
			obex->port_num, status);
}

/*-------------------------------------------------------------------------*/

static int __init
obex_bind(struct usb_configuration *c, struct usb_function *f)
{
	struct usb_composite_dev *cdev = c->cdev;
	struct f_obex		*obex = func_to_obex(f);
	int			status;
	struct usb_ep		*ep;

	/* allocate instance-specific interface IDs, and patch descriptors */

	status = usb_interface_id(c, f);
	if (status < 0)
		goto fail;
	obex->ctrl_id = status;

	obex_control_intf.bInterfaceNumber = status;
	obex_cdc_union_desc.bMasterInterface0 = status;

	status = usb_interface_id(c, f);
	if (status < 0)
		goto fail;
	obex->data_id = status;

	obex_data_nop_intf.bInterfaceNumber = status;
	obex_data_intf.bInterfaceNumber = status;
	obex_cdc_union_desc.bSlaveInterface0 = status;

	/* allocate instance-specific endpoints */

	ep = usb_ep_autoconfig(cdev->gadget, &obex_fs_ep_in_desc);
	if (!ep)
		goto fail;
	obex->port.in = ep;
	ep->driver_data = cdev;	/* claim */

	ep = usb_ep_autoconfig(cdev->gadget, &obex_fs_ep_out_desc);
	if (!ep)
		goto fail;
	obex->port.out = ep;
	ep->driver_data = cdev;	/* claim */

	/* copy descriptors, and track endpoint copies */
	f->descriptors = usb_copy_descriptors(fs_function);

	obex->fs.obex_in = usb_find_endpoint(fs_function,
			f->descriptors, &obex_fs_ep_in_desc);
	obex->fs.obex_out = usb_find_endpoint(fs_function,
			f->descriptors, &obex_fs_ep_out_desc);

	/* support all relevant hardware speeds... we expect that when
	 * hardware is dual speed, all bulk-capable endpoints work at
	 * both speeds
	 */
	if (gadget_is_dualspeed(c->cdev->gadget)) {

		obex_hs_ep_in_desc.bEndpointAddress =
				obex_fs_ep_in_desc.bEndpointAddress;
		obex_hs_ep_out_desc.bEndpointAddress =
				obex_fs_ep_out_desc.bEndpointAddress;

		/* copy descriptors, and track endpoint copies */
		f->hs_descriptors = usb_copy_descriptors(hs_function);

		obex->hs.obex_in = usb_find_endpoint(hs_function,
				f->hs_descriptors, &obex_hs_ep_in_desc);
		obex->hs.obex_out = usb_find_endpoint(hs_function,
				f->hs_descriptors, &obex_hs_ep_out_desc);
	}

	/* Avoid letting this gadget enumerate until the userspace
	 * OBEX server is active.
	 */
	status = usb_function_deactivate(f);
	if (status < 0)
		WARNING(cdev, "obex ttyGS%d: can't prevent enumeration, %d\n",
			obex->port_num, status);
	else
		obex->can_activate = true;


	DBG(cdev, "obex ttyGS%d: %s speed IN/%s OUT/%s\n",
			obex->port_num,
			gadget_is_dualspeed(c->cdev->gadget) ? "dual" : "full",
			obex->port.in->name, obex->port.out->name);

	return 0;

fail:
	/* we might as well release our claims on endpoints */
	if (obex->port.out)
		obex->port.out->driver_data = NULL;
	if (obex->port.in)
		obex->port.in->driver_data = NULL;

	ERROR(cdev, "%s/%p: can't bind, err %d\n", f->name, f, status);

	return status;
}

static void
obex_unbind(struct usb_configuration *c, struct usb_function *f)
{
	if (gadget_is_dualspeed(c->cdev->gadget))
		usb_free_descriptors(f->hs_descriptors);
	usb_free_descriptors(f->descriptors);
	kfree(func_to_obex(f));
}

/* Some controllers can't support CDC OBEX ... */
static inline bool can_support_obex(struct usb_configuration *c)
{
	/* Since the first interface is a NOP, we can ignore the
	 * issue of multi-interface support on most controllers.
	 *
	 * Altsettings are mandatory, however...
	 */
	if (!gadget_supports_altsettings(c->cdev->gadget))
		return false;

	/* everything else is *probably* fine ... */
	return true;
}

/**
 * obex_bind_config - add a CDC OBEX function to a configuration
 * @c: the configuration to support the CDC OBEX instance
 * @port_num: /dev/ttyGS* port this interface will use
 * Context: single threaded during gadget setup
 *
 * Returns zero on success, else negative errno.
 *
 * Caller must have called @gserial_setup() with enough ports to
 * handle all the ones it binds.  Caller is also responsible
 * for calling @gserial_cleanup() before module unload.
 */
int __init obex_bind_config(struct usb_configuration *c, u8 port_num)
{
	struct f_obex	*obex;
	int		status;

	if (!can_support_obex(c))
		return -EINVAL;

	/* maybe allocate device-global string IDs, and patch descriptors */
	if (obex_string_defs[OBEX_CTRL_IDX].id == 0) {
		status = usb_string_id(c->cdev);
		if (status < 0)
			return status;
		obex_string_defs[OBEX_CTRL_IDX].id = status;

		obex_control_intf.iInterface = status;

		status = usb_string_id(c->cdev);
		if (status < 0)
			return status;
		obex_string_defs[OBEX_DATA_IDX].id = status;

		obex_data_nop_intf.iInterface =
			obex_data_intf.iInterface = status;
	}

	/* allocate and initialize one new instance */
	obex = kzalloc(sizeof *obex, GFP_KERNEL);
	if (!obex)
		return -ENOMEM;

	obex->port_num = port_num;

	obex->port.connect = obex_connect;
	obex->port.disconnect = obex_disconnect;

	obex->port.func.name = "obex";
	obex->port.func.strings = obex_strings;
	/* descriptors are per-instance copies */
	obex->port.func.bind = obex_bind;
	obex->port.func.unbind = obex_unbind;
	obex->port.func.set_alt = obex_set_alt;
	obex->port.func.get_alt = obex_get_alt;
	obex->port.func.disable = obex_disable;

	status = usb_add_function(c, &obex->port.func);
	if (status)
		kfree(obex);

	return status;
}

MODULE_AUTHOR("Felipe Balbi");
MODULE_LICENSE("GPL");
