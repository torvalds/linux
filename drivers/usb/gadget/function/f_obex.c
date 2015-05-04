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
 */

/* #define VERBOSE_DEBUG */

#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/module.h>

#include "u_serial.h"
#include "gadget_chips.h"


/*
 * This CDC OBEX function support just packages a TTY-ish byte stream.
 * A user mode server will put it into "raw" mode and handle all the
 * relevant protocol details ... this is just a kernel passthrough.
 * When possible, we prevent gadget enumeration until that server is
 * ready to handle the commands.
 */

struct f_obex {
	struct gserial			port;
	u8				ctrl_id;
	u8				data_id;
	u8				cur_alt;
	u8				port_num;
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

static struct usb_interface_descriptor obex_control_intf = {
	.bLength		= sizeof(obex_control_intf),
	.bDescriptorType	= USB_DT_INTERFACE,
	.bInterfaceNumber	= 0,

	.bAlternateSetting	= 0,
	.bNumEndpoints		= 0,
	.bInterfaceClass	= USB_CLASS_COMM,
	.bInterfaceSubClass	= USB_CDC_SUBCLASS_OBEX,
};

static struct usb_interface_descriptor obex_data_nop_intf = {
	.bLength		= sizeof(obex_data_nop_intf),
	.bDescriptorType	= USB_DT_INTERFACE,
	.bInterfaceNumber	= 1,

	.bAlternateSetting	= 0,
	.bNumEndpoints		= 0,
	.bInterfaceClass	= USB_CLASS_CDC_DATA,
};

static struct usb_interface_descriptor obex_data_intf = {
	.bLength		= sizeof(obex_data_intf),
	.bDescriptorType	= USB_DT_INTERFACE,
	.bInterfaceNumber	= 2,

	.bAlternateSetting	= 1,
	.bNumEndpoints		= 2,
	.bInterfaceClass	= USB_CLASS_CDC_DATA,
};

static struct usb_cdc_header_desc obex_cdc_header_desc = {
	.bLength		= sizeof(obex_cdc_header_desc),
	.bDescriptorType	= USB_DT_CS_INTERFACE,
	.bDescriptorSubType	= USB_CDC_HEADER_TYPE,
	.bcdCDC			= cpu_to_le16(0x0120),
};

static struct usb_cdc_union_desc obex_cdc_union_desc = {
	.bLength		= sizeof(obex_cdc_union_desc),
	.bDescriptorType	= USB_DT_CS_INTERFACE,
	.bDescriptorSubType	= USB_CDC_UNION_TYPE,
	.bMasterInterface0	= 1,
	.bSlaveInterface0	= 2,
};

static struct usb_cdc_obex_desc obex_desc = {
	.bLength		= sizeof(obex_desc),
	.bDescriptorType	= USB_DT_CS_INTERFACE,
	.bDescriptorSubType	= USB_CDC_OBEX_TYPE,
	.bcdVersion		= cpu_to_le16(0x0100),
};

/* High-Speed Support */

static struct usb_endpoint_descriptor obex_hs_ep_out_desc = {
	.bLength		= USB_DT_ENDPOINT_SIZE,
	.bDescriptorType	= USB_DT_ENDPOINT,

	.bEndpointAddress	= USB_DIR_OUT,
	.bmAttributes		= USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize		= cpu_to_le16(512),
};

static struct usb_endpoint_descriptor obex_hs_ep_in_desc = {
	.bLength		= USB_DT_ENDPOINT_SIZE,
	.bDescriptorType	= USB_DT_ENDPOINT,

	.bEndpointAddress	= USB_DIR_IN,
	.bmAttributes		= USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize		= cpu_to_le16(512),
};

static struct usb_descriptor_header *hs_function[] = {
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

static struct usb_endpoint_descriptor obex_fs_ep_in_desc = {
	.bLength		= USB_DT_ENDPOINT_SIZE,
	.bDescriptorType	= USB_DT_ENDPOINT,

	.bEndpointAddress	= USB_DIR_IN,
	.bmAttributes		= USB_ENDPOINT_XFER_BULK,
};

static struct usb_endpoint_descriptor obex_fs_ep_out_desc = {
	.bLength		= USB_DT_ENDPOINT_SIZE,
	.bDescriptorType	= USB_DT_ENDPOINT,

	.bEndpointAddress	= USB_DIR_OUT,
	.bmAttributes		= USB_ENDPOINT_XFER_BULK,
};

static struct usb_descriptor_header *fs_function[] = {
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
		dev_dbg(&cdev->gadget->dev,
			"reset obex ttyGS%d control\n", obex->port_num);

	} else if (intf == obex->data_id) {
		if (alt > 1)
			goto fail;

		if (obex->port.in->driver_data) {
			dev_dbg(&cdev->gadget->dev,
				"reset obex ttyGS%d\n", obex->port_num);
			gserial_disconnect(&obex->port);
		}

		if (!obex->port.in->desc || !obex->port.out->desc) {
			dev_dbg(&cdev->gadget->dev,
				"init obex ttyGS%d\n", obex->port_num);
			if (config_ep_by_speed(cdev->gadget, f,
					       obex->port.in) ||
			    config_ep_by_speed(cdev->gadget, f,
					       obex->port.out)) {
				obex->port.out->desc = NULL;
				obex->port.in->desc = NULL;
				goto fail;
			}
		}

		if (alt == 1) {
			dev_dbg(&cdev->gadget->dev,
				"activate obex ttyGS%d\n", obex->port_num);
			gserial_connect(&obex->port, obex->port_num);
		}

	} else
		goto fail;

	obex->cur_alt = alt;

	return 0;

fail:
	return -EINVAL;
}

static int obex_get_alt(struct usb_function *f, unsigned intf)
{
	struct f_obex		*obex = func_to_obex(f);

	return obex->cur_alt;
}

static void obex_disable(struct usb_function *f)
{
	struct f_obex	*obex = func_to_obex(f);
	struct usb_composite_dev *cdev = f->config->cdev;

	dev_dbg(&cdev->gadget->dev, "obex ttyGS%d disable\n", obex->port_num);
	gserial_disconnect(&obex->port);
}

/*-------------------------------------------------------------------------*/

static void obex_connect(struct gserial *g)
{
	struct f_obex		*obex = port_to_obex(g);
	struct usb_composite_dev *cdev = g->func.config->cdev;
	int			status;

	status = usb_function_activate(&g->func);
	if (status)
		dev_dbg(&cdev->gadget->dev,
			"obex ttyGS%d function activate --> %d\n",
			obex->port_num, status);
}

static void obex_disconnect(struct gserial *g)
{
	struct f_obex		*obex = port_to_obex(g);
	struct usb_composite_dev *cdev = g->func.config->cdev;
	int			status;

	status = usb_function_deactivate(&g->func);
	if (status)
		dev_dbg(&cdev->gadget->dev,
			"obex ttyGS%d function deactivate --> %d\n",
			obex->port_num, status);
}

/*-------------------------------------------------------------------------*/

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

static int obex_bind(struct usb_configuration *c, struct usb_function *f)
{
	struct usb_composite_dev *cdev = c->cdev;
	struct f_obex		*obex = func_to_obex(f);
	struct usb_string	*us;
	int			status;
	struct usb_ep		*ep;

	if (!can_support_obex(c))
		return -EINVAL;

	us = usb_gstrings_attach(cdev, obex_strings,
				 ARRAY_SIZE(obex_string_defs));
	if (IS_ERR(us))
		return PTR_ERR(us);
	obex_control_intf.iInterface = us[OBEX_CTRL_IDX].id;
	obex_data_nop_intf.iInterface = us[OBEX_DATA_IDX].id;
	obex_data_intf.iInterface = us[OBEX_DATA_IDX].id;

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

	status = -ENODEV;
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

	/* support all relevant hardware speeds... we expect that when
	 * hardware is dual speed, all bulk-capable endpoints work at
	 * both speeds
	 */

	obex_hs_ep_in_desc.bEndpointAddress =
		obex_fs_ep_in_desc.bEndpointAddress;
	obex_hs_ep_out_desc.bEndpointAddress =
		obex_fs_ep_out_desc.bEndpointAddress;

	status = usb_assign_descriptors(f, fs_function, hs_function, NULL);
	if (status)
		goto fail;

	dev_dbg(&cdev->gadget->dev, "obex ttyGS%d: %s speed IN/%s OUT/%s\n",
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

static inline struct f_serial_opts *to_f_serial_opts(struct config_item *item)
{
	return container_of(to_config_group(item), struct f_serial_opts,
			    func_inst.group);
}

CONFIGFS_ATTR_STRUCT(f_serial_opts);
static ssize_t f_obex_attr_show(struct config_item *item,
				struct configfs_attribute *attr,
				char *page)
{
	struct f_serial_opts *opts = to_f_serial_opts(item);
	struct f_serial_opts_attribute *f_serial_opts_attr =
		container_of(attr, struct f_serial_opts_attribute, attr);
	ssize_t ret = 0;

	if (f_serial_opts_attr->show)
		ret = f_serial_opts_attr->show(opts, page);

	return ret;
}

static void obex_attr_release(struct config_item *item)
{
	struct f_serial_opts *opts = to_f_serial_opts(item);

	usb_put_function_instance(&opts->func_inst);
}

static struct configfs_item_operations obex_item_ops = {
	.release	= obex_attr_release,
	.show_attribute = f_obex_attr_show,
};

static ssize_t f_obex_port_num_show(struct f_serial_opts *opts, char *page)
{
	return sprintf(page, "%u\n", opts->port_num);
}

static struct f_serial_opts_attribute f_obex_port_num =
	__CONFIGFS_ATTR_RO(port_num, f_obex_port_num_show);

static struct configfs_attribute *acm_attrs[] = {
	&f_obex_port_num.attr,
	NULL,
};

static struct config_item_type obex_func_type = {
	.ct_item_ops	= &obex_item_ops,
	.ct_attrs	= acm_attrs,
	.ct_owner	= THIS_MODULE,
};

static void obex_free_inst(struct usb_function_instance *f)
{
	struct f_serial_opts *opts;

	opts = container_of(f, struct f_serial_opts, func_inst);
	gserial_free_line(opts->port_num);
	kfree(opts);
}

static struct usb_function_instance *obex_alloc_inst(void)
{
	struct f_serial_opts *opts;
	int ret;

	opts = kzalloc(sizeof(*opts), GFP_KERNEL);
	if (!opts)
		return ERR_PTR(-ENOMEM);

	opts->func_inst.free_func_inst = obex_free_inst;
	ret = gserial_alloc_line(&opts->port_num);
	if (ret) {
		kfree(opts);
		return ERR_PTR(ret);
	}
	config_group_init_type_name(&opts->func_inst.group, "",
				    &obex_func_type);

	return &opts->func_inst;
}

static void obex_free(struct usb_function *f)
{
	struct f_obex *obex;

	obex = func_to_obex(f);
	kfree(obex);
}

static void obex_unbind(struct usb_configuration *c, struct usb_function *f)
{
	usb_free_all_descriptors(f);
}

static struct usb_function *obex_alloc(struct usb_function_instance *fi)
{
	struct f_obex	*obex;
	struct f_serial_opts *opts;

	/* allocate and initialize one new instance */
	obex = kzalloc(sizeof(*obex), GFP_KERNEL);
	if (!obex)
		return ERR_PTR(-ENOMEM);

	opts = container_of(fi, struct f_serial_opts, func_inst);

	obex->port_num = opts->port_num;

	obex->port.connect = obex_connect;
	obex->port.disconnect = obex_disconnect;

	obex->port.func.name = "obex";
	/* descriptors are per-instance copies */
	obex->port.func.bind = obex_bind;
	obex->port.func.unbind = obex_unbind;
	obex->port.func.set_alt = obex_set_alt;
	obex->port.func.get_alt = obex_get_alt;
	obex->port.func.disable = obex_disable;
	obex->port.func.free_func = obex_free;
	obex->port.func.bind_deactivated = true;

	return &obex->port.func;
}

DECLARE_USB_FUNCTION_INIT(obex, obex_alloc_inst, obex_alloc);
MODULE_AUTHOR("Felipe Balbi");
MODULE_LICENSE("GPL");
