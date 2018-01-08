// SPDX-License-Identifier: GPL-2.0+
/*
 * f_eem.c -- USB CDC Ethernet (EEM) link function driver
 *
 * Copyright (C) 2003-2005,2008 David Brownell
 * Copyright (C) 2008 Nokia Corporation
 * Copyright (C) 2009 EF Johnson Technologies
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/etherdevice.h>
#include <linux/crc32.h>
#include <linux/slab.h>

#include "u_ether.h"
#include "u_ether_configfs.h"
#include "u_eem.h"

#define EEM_HLEN 2

/*
 * This function is a "CDC Ethernet Emulation Model" (CDC EEM)
 * Ethernet link.
 */

struct f_eem {
	struct gether			port;
	u8				ctrl_id;
};

static inline struct f_eem *func_to_eem(struct usb_function *f)
{
	return container_of(f, struct f_eem, port.func);
}

/*-------------------------------------------------------------------------*/

/* interface descriptor: */

static struct usb_interface_descriptor eem_intf = {
	.bLength =		sizeof eem_intf,
	.bDescriptorType =	USB_DT_INTERFACE,

	/* .bInterfaceNumber = DYNAMIC */
	.bNumEndpoints =	2,
	.bInterfaceClass =	USB_CLASS_COMM,
	.bInterfaceSubClass =	USB_CDC_SUBCLASS_EEM,
	.bInterfaceProtocol =	USB_CDC_PROTO_EEM,
	/* .iInterface = DYNAMIC */
};

/* full speed support: */

static struct usb_endpoint_descriptor eem_fs_in_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
};

static struct usb_endpoint_descriptor eem_fs_out_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_OUT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
};

static struct usb_descriptor_header *eem_fs_function[] = {
	/* CDC EEM control descriptors */
	(struct usb_descriptor_header *) &eem_intf,
	(struct usb_descriptor_header *) &eem_fs_in_desc,
	(struct usb_descriptor_header *) &eem_fs_out_desc,
	NULL,
};

/* high speed support: */

static struct usb_endpoint_descriptor eem_hs_in_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(512),
};

static struct usb_endpoint_descriptor eem_hs_out_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_OUT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(512),
};

static struct usb_descriptor_header *eem_hs_function[] = {
	/* CDC EEM control descriptors */
	(struct usb_descriptor_header *) &eem_intf,
	(struct usb_descriptor_header *) &eem_hs_in_desc,
	(struct usb_descriptor_header *) &eem_hs_out_desc,
	NULL,
};

/* super speed support: */

static struct usb_endpoint_descriptor eem_ss_in_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(1024),
};

static struct usb_endpoint_descriptor eem_ss_out_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_OUT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(1024),
};

static struct usb_ss_ep_comp_descriptor eem_ss_bulk_comp_desc = {
	.bLength =		sizeof eem_ss_bulk_comp_desc,
	.bDescriptorType =	USB_DT_SS_ENDPOINT_COMP,

	/* the following 2 values can be tweaked if necessary */
	/* .bMaxBurst =		0, */
	/* .bmAttributes =	0, */
};

static struct usb_descriptor_header *eem_ss_function[] = {
	/* CDC EEM control descriptors */
	(struct usb_descriptor_header *) &eem_intf,
	(struct usb_descriptor_header *) &eem_ss_in_desc,
	(struct usb_descriptor_header *) &eem_ss_bulk_comp_desc,
	(struct usb_descriptor_header *) &eem_ss_out_desc,
	(struct usb_descriptor_header *) &eem_ss_bulk_comp_desc,
	NULL,
};

/* string descriptors: */

static struct usb_string eem_string_defs[] = {
	[0].s = "CDC Ethernet Emulation Model (EEM)",
	{  } /* end of list */
};

static struct usb_gadget_strings eem_string_table = {
	.language =		0x0409,	/* en-us */
	.strings =		eem_string_defs,
};

static struct usb_gadget_strings *eem_strings[] = {
	&eem_string_table,
	NULL,
};

/*-------------------------------------------------------------------------*/

static int eem_setup(struct usb_function *f, const struct usb_ctrlrequest *ctrl)
{
	struct usb_composite_dev *cdev = f->config->cdev;
	int			value = -EOPNOTSUPP;
	u16			w_index = le16_to_cpu(ctrl->wIndex);
	u16			w_value = le16_to_cpu(ctrl->wValue);
	u16			w_length = le16_to_cpu(ctrl->wLength);

	DBG(cdev, "invalid control req%02x.%02x v%04x i%04x l%d\n",
		ctrl->bRequestType, ctrl->bRequest,
		w_value, w_index, w_length);

	/* device either stalls (value < 0) or reports success */
	return value;
}


static int eem_set_alt(struct usb_function *f, unsigned intf, unsigned alt)
{
	struct f_eem		*eem = func_to_eem(f);
	struct usb_composite_dev *cdev = f->config->cdev;
	struct net_device	*net;

	/* we know alt == 0, so this is an activation or a reset */
	if (alt != 0)
		goto fail;

	if (intf == eem->ctrl_id) {
		DBG(cdev, "reset eem\n");
		gether_disconnect(&eem->port);

		if (!eem->port.in_ep->desc || !eem->port.out_ep->desc) {
			DBG(cdev, "init eem\n");
			if (config_ep_by_speed(cdev->gadget, f,
					       eem->port.in_ep) ||
			    config_ep_by_speed(cdev->gadget, f,
					       eem->port.out_ep)) {
				eem->port.in_ep->desc = NULL;
				eem->port.out_ep->desc = NULL;
				goto fail;
			}
		}

		/* zlps should not occur because zero-length EEM packets
		 * will be inserted in those cases where they would occur
		 */
		eem->port.is_zlp_ok = 1;
		eem->port.cdc_filter = DEFAULT_FILTER;
		DBG(cdev, "activate eem\n");
		net = gether_connect(&eem->port);
		if (IS_ERR(net))
			return PTR_ERR(net);
	} else
		goto fail;

	return 0;
fail:
	return -EINVAL;
}

static void eem_disable(struct usb_function *f)
{
	struct f_eem		*eem = func_to_eem(f);
	struct usb_composite_dev *cdev = f->config->cdev;

	DBG(cdev, "eem deactivated\n");

	if (eem->port.in_ep->enabled)
		gether_disconnect(&eem->port);
}

/*-------------------------------------------------------------------------*/

/* EEM function driver setup/binding */

static int eem_bind(struct usb_configuration *c, struct usb_function *f)
{
	struct usb_composite_dev *cdev = c->cdev;
	struct f_eem		*eem = func_to_eem(f);
	struct usb_string	*us;
	int			status;
	struct usb_ep		*ep;

	struct f_eem_opts	*eem_opts;

	eem_opts = container_of(f->fi, struct f_eem_opts, func_inst);
	/*
	 * in drivers/usb/gadget/configfs.c:configfs_composite_bind()
	 * configurations are bound in sequence with list_for_each_entry,
	 * in each configuration its functions are bound in sequence
	 * with list_for_each_entry, so we assume no race condition
	 * with regard to eem_opts->bound access
	 */
	if (!eem_opts->bound) {
		mutex_lock(&eem_opts->lock);
		gether_set_gadget(eem_opts->net, cdev->gadget);
		status = gether_register_netdev(eem_opts->net);
		mutex_unlock(&eem_opts->lock);
		if (status)
			return status;
		eem_opts->bound = true;
	}

	us = usb_gstrings_attach(cdev, eem_strings,
				 ARRAY_SIZE(eem_string_defs));
	if (IS_ERR(us))
		return PTR_ERR(us);
	eem_intf.iInterface = us[0].id;

	/* allocate instance-specific interface IDs */
	status = usb_interface_id(c, f);
	if (status < 0)
		goto fail;
	eem->ctrl_id = status;
	eem_intf.bInterfaceNumber = status;

	status = -ENODEV;

	/* allocate instance-specific endpoints */
	ep = usb_ep_autoconfig(cdev->gadget, &eem_fs_in_desc);
	if (!ep)
		goto fail;
	eem->port.in_ep = ep;

	ep = usb_ep_autoconfig(cdev->gadget, &eem_fs_out_desc);
	if (!ep)
		goto fail;
	eem->port.out_ep = ep;

	status = -ENOMEM;

	/* support all relevant hardware speeds... we expect that when
	 * hardware is dual speed, all bulk-capable endpoints work at
	 * both speeds
	 */
	eem_hs_in_desc.bEndpointAddress = eem_fs_in_desc.bEndpointAddress;
	eem_hs_out_desc.bEndpointAddress = eem_fs_out_desc.bEndpointAddress;

	eem_ss_in_desc.bEndpointAddress = eem_fs_in_desc.bEndpointAddress;
	eem_ss_out_desc.bEndpointAddress = eem_fs_out_desc.bEndpointAddress;

	status = usb_assign_descriptors(f, eem_fs_function, eem_hs_function,
			eem_ss_function, NULL);
	if (status)
		goto fail;

	DBG(cdev, "CDC Ethernet (EEM): %s speed IN/%s OUT/%s\n",
			gadget_is_superspeed(c->cdev->gadget) ? "super" :
			gadget_is_dualspeed(c->cdev->gadget) ? "dual" : "full",
			eem->port.in_ep->name, eem->port.out_ep->name);
	return 0;

fail:
	ERROR(cdev, "%s: can't bind, err %d\n", f->name, status);

	return status;
}

static void eem_cmd_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct sk_buff *skb = (struct sk_buff *)req->context;

	dev_kfree_skb_any(skb);
}

/*
 * Add the EEM header and ethernet checksum.
 * We currently do not attempt to put multiple ethernet frames
 * into a single USB transfer
 */
static struct sk_buff *eem_wrap(struct gether *port, struct sk_buff *skb)
{
	struct sk_buff	*skb2 = NULL;
	struct usb_ep	*in = port->in_ep;
	int		headroom, tailroom, padlen = 0;
	u16		len;

	if (!skb)
		return NULL;

	len = skb->len;
	headroom = skb_headroom(skb);
	tailroom = skb_tailroom(skb);

	/* When (len + EEM_HLEN + ETH_FCS_LEN) % in->maxpacket) is 0,
	 * stick two bytes of zero-length EEM packet on the end.
	 */
	if (((len + EEM_HLEN + ETH_FCS_LEN) % in->maxpacket) == 0)
		padlen += 2;

	if ((tailroom >= (ETH_FCS_LEN + padlen)) &&
			(headroom >= EEM_HLEN) && !skb_cloned(skb))
		goto done;

	skb2 = skb_copy_expand(skb, EEM_HLEN, ETH_FCS_LEN + padlen, GFP_ATOMIC);
	dev_kfree_skb_any(skb);
	skb = skb2;
	if (!skb)
		return skb;

done:
	/* use the "no CRC" option */
	put_unaligned_be32(0xdeadbeef, skb_put(skb, 4));

	/* EEM packet header format:
	 * b0..13:	length of ethernet frame
	 * b14:		bmCRC (0 == sentinel CRC)
	 * b15:		bmType (0 == data)
	 */
	len = skb->len;
	put_unaligned_le16(len & 0x3FFF, skb_push(skb, 2));

	/* add a zero-length EEM packet, if needed */
	if (padlen)
		put_unaligned_le16(0, skb_put(skb, 2));

	return skb;
}

/*
 * Remove the EEM header.  Note that there can be many EEM packets in a single
 * USB transfer, so we need to break them out and handle them independently.
 */
static int eem_unwrap(struct gether *port,
			struct sk_buff *skb,
			struct sk_buff_head *list)
{
	struct usb_composite_dev	*cdev = port->func.config->cdev;
	int				status = 0;

	do {
		struct sk_buff	*skb2;
		u16		header;
		u16		len = 0;

		if (skb->len < EEM_HLEN) {
			status = -EINVAL;
			DBG(cdev, "invalid EEM header\n");
			goto error;
		}

		/* remove the EEM header */
		header = get_unaligned_le16(skb->data);
		skb_pull(skb, EEM_HLEN);

		/* EEM packet header format:
		 * b0..14:	EEM type dependent (data or command)
		 * b15:		bmType (0 == data, 1 == command)
		 */
		if (header & BIT(15)) {
			struct usb_request	*req = cdev->req;
			u16			bmEEMCmd;

			/* EEM command packet format:
			 * b0..10:	bmEEMCmdParam
			 * b11..13:	bmEEMCmd
			 * b14:		reserved (must be zero)
			 * b15:		bmType (1 == command)
			 */
			if (header & BIT(14))
				continue;

			bmEEMCmd = (header >> 11) & 0x7;
			switch (bmEEMCmd) {
			case 0: /* echo */
				len = header & 0x7FF;
				if (skb->len < len) {
					status = -EOVERFLOW;
					goto error;
				}

				skb2 = skb_clone(skb, GFP_ATOMIC);
				if (unlikely(!skb2)) {
					DBG(cdev, "EEM echo response error\n");
					goto next;
				}
				skb_trim(skb2, len);
				put_unaligned_le16(BIT(15) | BIT(11) | len,
							skb_push(skb2, 2));
				skb_copy_bits(skb2, 0, req->buf, skb2->len);
				req->length = skb2->len;
				req->complete = eem_cmd_complete;
				req->zero = 1;
				req->context = skb2;
				if (usb_ep_queue(port->in_ep, req, GFP_ATOMIC))
					DBG(cdev, "echo response queue fail\n");
				break;

			case 1:  /* echo response */
			case 2:  /* suspend hint */
			case 3:  /* response hint */
			case 4:  /* response complete hint */
			case 5:  /* tickle */
			default: /* reserved */
				continue;
			}
		} else {
			u32		crc, crc2;
			struct sk_buff	*skb3;

			/* check for zero-length EEM packet */
			if (header == 0)
				continue;

			/* EEM data packet format:
			 * b0..13:	length of ethernet frame
			 * b14:		bmCRC (0 == sentinel, 1 == calculated)
			 * b15:		bmType (0 == data)
			 */
			len = header & 0x3FFF;
			if ((skb->len < len)
					|| (len < (ETH_HLEN + ETH_FCS_LEN))) {
				status = -EINVAL;
				goto error;
			}

			/* validate CRC */
			if (header & BIT(14)) {
				crc = get_unaligned_le32(skb->data + len
							- ETH_FCS_LEN);
				crc2 = ~crc32_le(~0,
						skb->data, len - ETH_FCS_LEN);
			} else {
				crc = get_unaligned_be32(skb->data + len
							- ETH_FCS_LEN);
				crc2 = 0xdeadbeef;
			}
			if (crc != crc2) {
				DBG(cdev, "invalid EEM CRC\n");
				goto next;
			}

			skb2 = skb_clone(skb, GFP_ATOMIC);
			if (unlikely(!skb2)) {
				DBG(cdev, "unable to unframe EEM packet\n");
				continue;
			}
			skb_trim(skb2, len - ETH_FCS_LEN);

			skb3 = skb_copy_expand(skb2,
						NET_IP_ALIGN,
						0,
						GFP_ATOMIC);
			if (unlikely(!skb3)) {
				DBG(cdev, "unable to realign EEM packet\n");
				dev_kfree_skb_any(skb2);
				continue;
			}
			dev_kfree_skb_any(skb2);
			skb_queue_tail(list, skb3);
		}
next:
		skb_pull(skb, len);
	} while (skb->len);

error:
	dev_kfree_skb_any(skb);
	return status;
}

static inline struct f_eem_opts *to_f_eem_opts(struct config_item *item)
{
	return container_of(to_config_group(item), struct f_eem_opts,
			    func_inst.group);
}

/* f_eem_item_ops */
USB_ETHERNET_CONFIGFS_ITEM(eem);

/* f_eem_opts_dev_addr */
USB_ETHERNET_CONFIGFS_ITEM_ATTR_DEV_ADDR(eem);

/* f_eem_opts_host_addr */
USB_ETHERNET_CONFIGFS_ITEM_ATTR_HOST_ADDR(eem);

/* f_eem_opts_qmult */
USB_ETHERNET_CONFIGFS_ITEM_ATTR_QMULT(eem);

/* f_eem_opts_ifname */
USB_ETHERNET_CONFIGFS_ITEM_ATTR_IFNAME(eem);

static struct configfs_attribute *eem_attrs[] = {
	&eem_opts_attr_dev_addr,
	&eem_opts_attr_host_addr,
	&eem_opts_attr_qmult,
	&eem_opts_attr_ifname,
	NULL,
};

static const struct config_item_type eem_func_type = {
	.ct_item_ops	= &eem_item_ops,
	.ct_attrs	= eem_attrs,
	.ct_owner	= THIS_MODULE,
};

static void eem_free_inst(struct usb_function_instance *f)
{
	struct f_eem_opts *opts;

	opts = container_of(f, struct f_eem_opts, func_inst);
	if (opts->bound)
		gether_cleanup(netdev_priv(opts->net));
	else
		free_netdev(opts->net);
	kfree(opts);
}

static struct usb_function_instance *eem_alloc_inst(void)
{
	struct f_eem_opts *opts;

	opts = kzalloc(sizeof(*opts), GFP_KERNEL);
	if (!opts)
		return ERR_PTR(-ENOMEM);
	mutex_init(&opts->lock);
	opts->func_inst.free_func_inst = eem_free_inst;
	opts->net = gether_setup_default();
	if (IS_ERR(opts->net)) {
		struct net_device *net = opts->net;
		kfree(opts);
		return ERR_CAST(net);
	}

	config_group_init_type_name(&opts->func_inst.group, "", &eem_func_type);

	return &opts->func_inst;
}

static void eem_free(struct usb_function *f)
{
	struct f_eem *eem;
	struct f_eem_opts *opts;

	eem = func_to_eem(f);
	opts = container_of(f->fi, struct f_eem_opts, func_inst);
	kfree(eem);
	mutex_lock(&opts->lock);
	opts->refcnt--;
	mutex_unlock(&opts->lock);
}

static void eem_unbind(struct usb_configuration *c, struct usb_function *f)
{
	DBG(c->cdev, "eem unbind\n");

	usb_free_all_descriptors(f);
}

static struct usb_function *eem_alloc(struct usb_function_instance *fi)
{
	struct f_eem	*eem;
	struct f_eem_opts *opts;

	/* allocate and initialize one new instance */
	eem = kzalloc(sizeof(*eem), GFP_KERNEL);
	if (!eem)
		return ERR_PTR(-ENOMEM);

	opts = container_of(fi, struct f_eem_opts, func_inst);
	mutex_lock(&opts->lock);
	opts->refcnt++;

	eem->port.ioport = netdev_priv(opts->net);
	mutex_unlock(&opts->lock);
	eem->port.cdc_filter = DEFAULT_FILTER;

	eem->port.func.name = "cdc_eem";
	/* descriptors are per-instance copies */
	eem->port.func.bind = eem_bind;
	eem->port.func.unbind = eem_unbind;
	eem->port.func.set_alt = eem_set_alt;
	eem->port.func.setup = eem_setup;
	eem->port.func.disable = eem_disable;
	eem->port.func.free_func = eem_free;
	eem->port.wrap = eem_wrap;
	eem->port.unwrap = eem_unwrap;
	eem->port.header_len = EEM_HLEN;

	return &eem->port.func;
}

DECLARE_USB_FUNCTION_INIT(eem, eem_alloc_inst, eem_alloc);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("David Brownell");
