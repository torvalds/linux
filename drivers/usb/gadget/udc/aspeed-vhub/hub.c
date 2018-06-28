// SPDX-License-Identifier: GPL-2.0+
/*
 * aspeed-vhub -- Driver for Aspeed SoC "vHub" USB gadget
 *
 * hub.c - virtual hub handling
 *
 * Copyright 2017 IBM Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/proc_fs.h>
#include <linux/prefetch.h>
#include <linux/clk.h>
#include <linux/usb/gadget.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/regmap.h>
#include <linux/dma-mapping.h>
#include <linux/bcd.h>
#include <linux/version.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>

#include "vhub.h"

/* usb 2.0 hub device descriptor
 *
 * A few things we may want to improve here:
 *
 *    - We may need to indicate TT support
 *    - We may need a device qualifier descriptor
 *	as devices can pretend to be usb1 or 2
 *    - Make vid/did overridable
 *    - make it look like usb1 if usb1 mode forced
 */
#define KERNEL_REL	bin2bcd(((LINUX_VERSION_CODE >> 16) & 0x0ff))
#define KERNEL_VER	bin2bcd(((LINUX_VERSION_CODE >> 8) & 0x0ff))

enum {
	AST_VHUB_STR_MANUF = 3,
	AST_VHUB_STR_PRODUCT = 2,
	AST_VHUB_STR_SERIAL = 1,
};

static const struct usb_device_descriptor ast_vhub_dev_desc = {
	.bLength		= USB_DT_DEVICE_SIZE,
	.bDescriptorType	= USB_DT_DEVICE,
	.bcdUSB			= cpu_to_le16(0x0200),
	.bDeviceClass		= USB_CLASS_HUB,
	.bDeviceSubClass	= 0,
	.bDeviceProtocol	= 1,
	.bMaxPacketSize0	= 64,
	.idVendor		= cpu_to_le16(0x1d6b),
	.idProduct		= cpu_to_le16(0x0107),
	.bcdDevice		= cpu_to_le16(0x0100),
	.iManufacturer		= AST_VHUB_STR_MANUF,
	.iProduct		= AST_VHUB_STR_PRODUCT,
	.iSerialNumber		= AST_VHUB_STR_SERIAL,
	.bNumConfigurations	= 1,
};

/* Patches to the above when forcing USB1 mode */
static void ast_vhub_patch_dev_desc_usb1(struct usb_device_descriptor *desc)
{
	desc->bcdUSB = cpu_to_le16(0x0100);
	desc->bDeviceProtocol = 0;
}

/*
 * Configuration descriptor: same comments as above
 * regarding handling USB1 mode.
 */

/*
 * We don't use sizeof() as Linux definition of
 * struct usb_endpoint_descriptor contains 2
 * extra bytes
 */
#define AST_VHUB_CONF_DESC_SIZE	(USB_DT_CONFIG_SIZE + \
				 USB_DT_INTERFACE_SIZE + \
				 USB_DT_ENDPOINT_SIZE)

static const struct ast_vhub_full_cdesc {
	struct usb_config_descriptor	cfg;
	struct usb_interface_descriptor intf;
	struct usb_endpoint_descriptor	ep;
} __attribute__ ((packed)) ast_vhub_conf_desc = {
	.cfg = {
		.bLength		= USB_DT_CONFIG_SIZE,
		.bDescriptorType	= USB_DT_CONFIG,
		.wTotalLength		= cpu_to_le16(AST_VHUB_CONF_DESC_SIZE),
		.bNumInterfaces		= 1,
		.bConfigurationValue	= 1,
		.iConfiguration		= 0,
		.bmAttributes		= USB_CONFIG_ATT_ONE |
					  USB_CONFIG_ATT_SELFPOWER |
					  USB_CONFIG_ATT_WAKEUP,
		.bMaxPower		= 0,
	},
	.intf = {
		.bLength		= USB_DT_INTERFACE_SIZE,
		.bDescriptorType	= USB_DT_INTERFACE,
		.bInterfaceNumber	= 0,
		.bAlternateSetting	= 0,
		.bNumEndpoints		= 1,
		.bInterfaceClass	= USB_CLASS_HUB,
		.bInterfaceSubClass	= 0,
		.bInterfaceProtocol	= 0,
		.iInterface		= 0,
	},
	.ep = {
		.bLength		= USB_DT_ENDPOINT_SIZE,
		.bDescriptorType	= USB_DT_ENDPOINT,
		.bEndpointAddress	= 0x81,
		.bmAttributes		= USB_ENDPOINT_XFER_INT,
		.wMaxPacketSize		= cpu_to_le16(1),
		.bInterval		= 0x0c,
	},
};

#define AST_VHUB_HUB_DESC_SIZE	(USB_DT_HUB_NONVAR_SIZE + 2)

static const struct usb_hub_descriptor ast_vhub_hub_desc = {
	.bDescLength			= AST_VHUB_HUB_DESC_SIZE,
	.bDescriptorType		= USB_DT_HUB,
	.bNbrPorts			= AST_VHUB_NUM_PORTS,
	.wHubCharacteristics		= cpu_to_le16(HUB_CHAR_NO_LPSM),
	.bPwrOn2PwrGood			= 10,
	.bHubContrCurrent		= 0,
	.u.hs.DeviceRemovable[0]	= 0,
	.u.hs.DeviceRemovable[1]	= 0xff,
};

/*
 * These strings converted to UTF-16 must be smaller than
 * our EP0 buffer.
 */
static const struct usb_string ast_vhub_str_array[] = {
	{
		.id = AST_VHUB_STR_SERIAL,
		.s = "00000000"
	},
	{
		.id = AST_VHUB_STR_PRODUCT,
		.s = "USB Virtual Hub"
	},
	{
		.id = AST_VHUB_STR_MANUF,
		.s = "Aspeed"
	},
	{ }
};

static const struct usb_gadget_strings ast_vhub_strings = {
	.language = 0x0409,
	.strings = (struct usb_string *)ast_vhub_str_array
};

static int ast_vhub_hub_dev_status(struct ast_vhub_ep *ep,
				   u16 wIndex, u16 wValue)
{
	u8 st0;

	EPDBG(ep, "GET_STATUS(dev)\n");

	/*
	 * Mark it as self-powered, I doubt the BMC is powered off
	 * the USB bus ...
	 */
	st0 = 1 << USB_DEVICE_SELF_POWERED;

	/*
	 * Need to double check how remote wakeup actually works
	 * on that chip and what triggers it.
	 */
	if (ep->vhub->wakeup_en)
		st0 |= 1 << USB_DEVICE_REMOTE_WAKEUP;

	return ast_vhub_simple_reply(ep, st0, 0);
}

static int ast_vhub_hub_ep_status(struct ast_vhub_ep *ep,
				  u16 wIndex, u16 wValue)
{
	int ep_num;
	u8 st0 = 0;

	ep_num = wIndex & USB_ENDPOINT_NUMBER_MASK;
	EPDBG(ep, "GET_STATUS(ep%d)\n", ep_num);

	/* On the hub we have only EP 0 and 1 */
	if (ep_num == 1) {
		if (ep->vhub->ep1_stalled)
			st0 |= 1 << USB_ENDPOINT_HALT;
	} else if (ep_num != 0)
		return std_req_stall;

	return ast_vhub_simple_reply(ep, st0, 0);
}

static int ast_vhub_hub_dev_feature(struct ast_vhub_ep *ep,
				    u16 wIndex, u16 wValue,
				    bool is_set)
{
	EPDBG(ep, "%s_FEATURE(dev val=%02x)\n",
	      is_set ? "SET" : "CLEAR", wValue);

	if (wValue != USB_DEVICE_REMOTE_WAKEUP)
		return std_req_stall;

	ep->vhub->wakeup_en = is_set;
	EPDBG(ep, "Hub remote wakeup %s\n",
	      is_set ? "enabled" : "disabled");

	return std_req_complete;
}

static int ast_vhub_hub_ep_feature(struct ast_vhub_ep *ep,
				   u16 wIndex, u16 wValue,
				   bool is_set)
{
	int ep_num;
	u32 reg;

	ep_num = wIndex & USB_ENDPOINT_NUMBER_MASK;
	EPDBG(ep, "%s_FEATURE(ep%d val=%02x)\n",
	      is_set ? "SET" : "CLEAR", ep_num, wValue);

	if (ep_num > 1)
		return std_req_stall;
	if (wValue != USB_ENDPOINT_HALT)
		return std_req_stall;
	if (ep_num == 0)
		return std_req_complete;

	EPDBG(ep, "%s stall on EP 1\n",
	      is_set ? "setting" : "clearing");

	ep->vhub->ep1_stalled = is_set;
	reg = readl(ep->vhub->regs + AST_VHUB_EP1_CTRL);
	if (is_set) {
		reg |= VHUB_EP1_CTRL_STALL;
	} else {
		reg &= ~VHUB_EP1_CTRL_STALL;
		reg |= VHUB_EP1_CTRL_RESET_TOGGLE;
	}
	writel(reg, ep->vhub->regs + AST_VHUB_EP1_CTRL);

	return std_req_complete;
}

static int ast_vhub_rep_desc(struct ast_vhub_ep *ep,
			     u8 desc_type, u16 len)
{
	size_t dsize;

	EPDBG(ep, "GET_DESCRIPTOR(type:%d)\n", desc_type);

	/*
	 * Copy first to EP buffer and send from there, so
	 * we can do some in-place patching if needed. We know
	 * the EP buffer is big enough but ensure that doesn't
	 * change. We do that now rather than later after we
	 * have checked sizes etc... to avoid a gcc bug where
	 * it thinks len is constant and barfs about read
	 * overflows in memcpy.
	 */
	switch(desc_type) {
	case USB_DT_DEVICE:
		dsize = USB_DT_DEVICE_SIZE;
		memcpy(ep->buf, &ast_vhub_dev_desc, dsize);
		BUILD_BUG_ON(dsize > sizeof(ast_vhub_dev_desc));
		BUILD_BUG_ON(USB_DT_DEVICE_SIZE >= AST_VHUB_EP0_MAX_PACKET);
		break;
	case USB_DT_CONFIG:
		dsize = AST_VHUB_CONF_DESC_SIZE;
		memcpy(ep->buf, &ast_vhub_conf_desc, dsize);
		BUILD_BUG_ON(dsize > sizeof(ast_vhub_conf_desc));
		BUILD_BUG_ON(AST_VHUB_CONF_DESC_SIZE >= AST_VHUB_EP0_MAX_PACKET);
		break;
	case USB_DT_HUB:
		dsize = AST_VHUB_HUB_DESC_SIZE;
		memcpy(ep->buf, &ast_vhub_hub_desc, dsize);
		BUILD_BUG_ON(dsize > sizeof(ast_vhub_hub_desc));
	BUILD_BUG_ON(AST_VHUB_HUB_DESC_SIZE >= AST_VHUB_EP0_MAX_PACKET);
		break;
	default:
		return std_req_stall;
	}

	/* Crop requested length */
	if (len > dsize)
		len = dsize;

	/* Patch it if forcing USB1 */
	if (desc_type == USB_DT_DEVICE && ep->vhub->force_usb1)
		ast_vhub_patch_dev_desc_usb1(ep->buf);

	/* Shoot it from the EP buffer */
	return ast_vhub_reply(ep, NULL, len);
}

static int ast_vhub_rep_string(struct ast_vhub_ep *ep,
			       u8 string_id, u16 lang_id,
			       u16 len)
{
	int rc = usb_gadget_get_string (&ast_vhub_strings, string_id, ep->buf);

	/*
	 * This should never happen unless we put too big strings in
	 * the array above
	 */
	BUG_ON(rc >= AST_VHUB_EP0_MAX_PACKET);

	if (rc < 0)
		return std_req_stall;

	/* Shoot it from the EP buffer */
	return ast_vhub_reply(ep, NULL, min_t(u16, rc, len));
}

enum std_req_rc ast_vhub_std_hub_request(struct ast_vhub_ep *ep,
					 struct usb_ctrlrequest *crq)
{
	struct ast_vhub *vhub = ep->vhub;
	u16 wValue, wIndex, wLength;

	wValue = le16_to_cpu(crq->wValue);
	wIndex = le16_to_cpu(crq->wIndex);
	wLength = le16_to_cpu(crq->wLength);

	/* First packet, grab speed */
	if (vhub->speed == USB_SPEED_UNKNOWN) {
		u32 ustat = readl(vhub->regs + AST_VHUB_USBSTS);
		if (ustat & VHUB_USBSTS_HISPEED)
			vhub->speed = USB_SPEED_HIGH;
		else
			vhub->speed = USB_SPEED_FULL;
		UDCDBG(vhub, "USB status=%08x speed=%s\n", ustat,
		       vhub->speed == USB_SPEED_HIGH ? "high" : "full");
	}

	switch ((crq->bRequestType << 8) | crq->bRequest) {
		/* SET_ADDRESS */
	case DeviceOutRequest | USB_REQ_SET_ADDRESS:
		EPDBG(ep, "SET_ADDRESS: Got address %x\n", wValue);
		writel(wValue, vhub->regs + AST_VHUB_CONF);
		return std_req_complete;

		/* GET_STATUS */
	case DeviceRequest | USB_REQ_GET_STATUS:
		return ast_vhub_hub_dev_status(ep, wIndex, wValue);
	case InterfaceRequest | USB_REQ_GET_STATUS:
		return ast_vhub_simple_reply(ep, 0, 0);
	case EndpointRequest | USB_REQ_GET_STATUS:
		return ast_vhub_hub_ep_status(ep, wIndex, wValue);

		/* SET/CLEAR_FEATURE */
	case DeviceOutRequest | USB_REQ_SET_FEATURE:
		return ast_vhub_hub_dev_feature(ep, wIndex, wValue, true);
	case DeviceOutRequest | USB_REQ_CLEAR_FEATURE:
		return ast_vhub_hub_dev_feature(ep, wIndex, wValue, false);
	case EndpointOutRequest | USB_REQ_SET_FEATURE:
		return ast_vhub_hub_ep_feature(ep, wIndex, wValue, true);
	case EndpointOutRequest | USB_REQ_CLEAR_FEATURE:
		return ast_vhub_hub_ep_feature(ep, wIndex, wValue, false);

		/* GET/SET_CONFIGURATION */
	case DeviceRequest | USB_REQ_GET_CONFIGURATION:
		return ast_vhub_simple_reply(ep, 1);
	case DeviceOutRequest | USB_REQ_SET_CONFIGURATION:
		if (wValue != 1)
			return std_req_stall;
		return std_req_complete;

		/* GET_DESCRIPTOR */
	case DeviceRequest | USB_REQ_GET_DESCRIPTOR:
		switch (wValue >> 8) {
		case USB_DT_DEVICE:
		case USB_DT_CONFIG:
			return ast_vhub_rep_desc(ep, wValue >> 8,
						 wLength);
		case USB_DT_STRING:
			return ast_vhub_rep_string(ep, wValue & 0xff,
						   wIndex, wLength);
		}
		return std_req_stall;

		/* GET/SET_INTERFACE */
	case DeviceRequest | USB_REQ_GET_INTERFACE:
		return ast_vhub_simple_reply(ep, 0);
	case DeviceOutRequest | USB_REQ_SET_INTERFACE:
		if (wValue != 0 || wIndex != 0)
			return std_req_stall;
		return std_req_complete;
	}
	return std_req_stall;
}

static void ast_vhub_update_hub_ep1(struct ast_vhub *vhub,
				    unsigned int port)
{
	/* Update HW EP1 response */
	u32 reg = readl(vhub->regs + AST_VHUB_EP1_STS_CHG);
	u32 pmask = (1 << (port + 1));
	if (vhub->ports[port].change)
		reg |= pmask;
	else
		reg &= ~pmask;
	writel(reg, vhub->regs + AST_VHUB_EP1_STS_CHG);
}

static void ast_vhub_change_port_stat(struct ast_vhub *vhub,
				      unsigned int port,
				      u16 clr_flags,
				      u16 set_flags,
				      bool set_c)
{
	struct ast_vhub_port *p = &vhub->ports[port];
	u16 prev;

	/* Update port status */
	prev = p->status;
	p->status = (prev & ~clr_flags) | set_flags;
	DDBG(&p->dev, "port %d status %04x -> %04x (C=%d)\n",
	     port + 1, prev, p->status, set_c);

	/* Update change bits if needed */
	if (set_c) {
		u16 chg = p->status ^ prev;

		/* Only these are relevant for change */
		chg &= USB_PORT_STAT_C_CONNECTION |
		       USB_PORT_STAT_C_ENABLE |
		       USB_PORT_STAT_C_SUSPEND |
		       USB_PORT_STAT_C_OVERCURRENT |
		       USB_PORT_STAT_C_RESET |
		       USB_PORT_STAT_C_L1;
		p->change |= chg;

		ast_vhub_update_hub_ep1(vhub, port);
	}
}

static void ast_vhub_send_host_wakeup(struct ast_vhub *vhub)
{
	u32 reg = readl(vhub->regs + AST_VHUB_CTRL);
	UDCDBG(vhub, "Waking up host !\n");
	reg |= VHUB_CTRL_MANUAL_REMOTE_WAKEUP;
	writel(reg, vhub->regs + AST_VHUB_CTRL);
}

void ast_vhub_device_connect(struct ast_vhub *vhub,
			     unsigned int port, bool on)
{
	if (on)
		ast_vhub_change_port_stat(vhub, port, 0,
					  USB_PORT_STAT_CONNECTION, true);
	else
		ast_vhub_change_port_stat(vhub, port,
					  USB_PORT_STAT_CONNECTION |
					  USB_PORT_STAT_ENABLE,
					  0, true);

	/*
	 * If the hub is set to wakup the host on connection events
	 * then send a wakeup.
	 */
	if (vhub->wakeup_en)
		ast_vhub_send_host_wakeup(vhub);
}

static void ast_vhub_wake_work(struct work_struct *work)
{
	struct ast_vhub *vhub = container_of(work,
					     struct ast_vhub,
					     wake_work);
	unsigned long flags;
	unsigned int i;

	/*
	 * Wake all sleeping ports. If a port is suspended by
	 * the host suspend (without explicit state suspend),
	 * we let the normal host wake path deal with it later.
	 */
	spin_lock_irqsave(&vhub->lock, flags);
	for (i = 0; i < AST_VHUB_NUM_PORTS; i++) {
		struct ast_vhub_port *p = &vhub->ports[i];

		if (!(p->status & USB_PORT_STAT_SUSPEND))
			continue;
		ast_vhub_change_port_stat(vhub, i,
					  USB_PORT_STAT_SUSPEND,
					  0, true);
		ast_vhub_dev_resume(&p->dev);
	}
	ast_vhub_send_host_wakeup(vhub);
	spin_unlock_irqrestore(&vhub->lock, flags);
}

void ast_vhub_hub_wake_all(struct ast_vhub *vhub)
{
	/*
	 * A device is trying to wake the world, because this
	 * can recurse into the device, we break the call chain
	 * using a work queue
	 */
	schedule_work(&vhub->wake_work);
}

static void ast_vhub_port_reset(struct ast_vhub *vhub, u8 port)
{
	struct ast_vhub_port *p = &vhub->ports[port];
	u16 set, clr, speed;

	/* First mark disabled */
	ast_vhub_change_port_stat(vhub, port,
				  USB_PORT_STAT_ENABLE |
				  USB_PORT_STAT_SUSPEND,
				  USB_PORT_STAT_RESET,
				  false);

	if (!p->dev.driver)
		return;

	/*
	 * This will either "start" the port or reset the
	 * device if already started...
	 */
	ast_vhub_dev_reset(&p->dev);

	/* Grab the right speed */
	speed = p->dev.driver->max_speed;
	if (speed == USB_SPEED_UNKNOWN || speed > vhub->speed)
		speed = vhub->speed;

	switch (speed) {
	case USB_SPEED_LOW:
		set = USB_PORT_STAT_LOW_SPEED;
		clr = USB_PORT_STAT_HIGH_SPEED;
		break;
	case USB_SPEED_FULL:
		set = 0;
		clr = USB_PORT_STAT_LOW_SPEED |
			USB_PORT_STAT_HIGH_SPEED;
		break;
	case USB_SPEED_HIGH:
		set = USB_PORT_STAT_HIGH_SPEED;
		clr = USB_PORT_STAT_LOW_SPEED;
		break;
	default:
		UDCDBG(vhub, "Unsupported speed %d when"
		       " connecting device\n",
		       speed);
		return;
	}
	clr |= USB_PORT_STAT_RESET;
	set |= USB_PORT_STAT_ENABLE;

	/* This should ideally be delayed ... */
	ast_vhub_change_port_stat(vhub, port, clr, set, true);
}

static enum std_req_rc ast_vhub_set_port_feature(struct ast_vhub_ep *ep,
						 u8 port, u16 feat)
{
	struct ast_vhub *vhub = ep->vhub;
	struct ast_vhub_port *p;

	if (port == 0 || port > AST_VHUB_NUM_PORTS)
		return std_req_stall;
	port--;
	p = &vhub->ports[port];

	switch(feat) {
	case USB_PORT_FEAT_SUSPEND:
		if (!(p->status & USB_PORT_STAT_ENABLE))
			return std_req_complete;
		ast_vhub_change_port_stat(vhub, port,
					  0, USB_PORT_STAT_SUSPEND,
					  false);
		ast_vhub_dev_suspend(&p->dev);
		return std_req_complete;
	case USB_PORT_FEAT_RESET:
		EPDBG(ep, "Port reset !\n");
		ast_vhub_port_reset(vhub, port);
		return std_req_complete;
	case USB_PORT_FEAT_POWER:
		/*
		 * On Power-on, we mark the connected flag changed,
		 * if there's a connected device, some hosts will
		 * otherwise fail to detect it.
		 */
		if (p->status & USB_PORT_STAT_CONNECTION) {
			p->change |= USB_PORT_STAT_C_CONNECTION;
			ast_vhub_update_hub_ep1(vhub, port);
		}
		return std_req_complete;
	case USB_PORT_FEAT_TEST:
	case USB_PORT_FEAT_INDICATOR:
		/* We don't do anything with these */
		return std_req_complete;
	}
	return std_req_stall;
}

static enum std_req_rc ast_vhub_clr_port_feature(struct ast_vhub_ep *ep,
						 u8 port, u16 feat)
{
	struct ast_vhub *vhub = ep->vhub;
	struct ast_vhub_port *p;

	if (port == 0 || port > AST_VHUB_NUM_PORTS)
		return std_req_stall;
	port--;
	p = &vhub->ports[port];

	switch(feat) {
	case USB_PORT_FEAT_ENABLE:
		ast_vhub_change_port_stat(vhub, port,
					  USB_PORT_STAT_ENABLE |
					  USB_PORT_STAT_SUSPEND, 0,
					  false);
		ast_vhub_dev_suspend(&p->dev);
		return std_req_complete;
	case USB_PORT_FEAT_SUSPEND:
		if (!(p->status & USB_PORT_STAT_SUSPEND))
			return std_req_complete;
		ast_vhub_change_port_stat(vhub, port,
					  USB_PORT_STAT_SUSPEND, 0,
					  false);
		ast_vhub_dev_resume(&p->dev);
		return std_req_complete;
	case USB_PORT_FEAT_POWER:
		/* We don't do power control */
		return std_req_complete;
	case USB_PORT_FEAT_INDICATOR:
		/* We don't have indicators */
		return std_req_complete;
	case USB_PORT_FEAT_C_CONNECTION:
	case USB_PORT_FEAT_C_ENABLE:
	case USB_PORT_FEAT_C_SUSPEND:
	case USB_PORT_FEAT_C_OVER_CURRENT:
	case USB_PORT_FEAT_C_RESET:
		/* Clear state-change feature */
		p->change &= ~(1u << (feat - 16));
		ast_vhub_update_hub_ep1(vhub, port);
		return std_req_complete;
	}
	return std_req_stall;
}

static enum std_req_rc ast_vhub_get_port_stat(struct ast_vhub_ep *ep,
					      u8 port)
{
	struct ast_vhub *vhub = ep->vhub;
	u16 stat, chg;

	if (port == 0 || port > AST_VHUB_NUM_PORTS)
		return std_req_stall;
	port--;

	stat = vhub->ports[port].status;
	chg = vhub->ports[port].change;

	/* We always have power */
	stat |= USB_PORT_STAT_POWER;

	EPDBG(ep, " port status=%04x change=%04x\n", stat, chg);

	return ast_vhub_simple_reply(ep,
				     stat & 0xff,
				     stat >> 8,
				     chg & 0xff,
				     chg >> 8);
}

enum std_req_rc ast_vhub_class_hub_request(struct ast_vhub_ep *ep,
					   struct usb_ctrlrequest *crq)
{
	u16 wValue, wIndex, wLength;

	wValue = le16_to_cpu(crq->wValue);
	wIndex = le16_to_cpu(crq->wIndex);
	wLength = le16_to_cpu(crq->wLength);

	switch ((crq->bRequestType << 8) | crq->bRequest) {
	case GetHubStatus:
		EPDBG(ep, "GetHubStatus\n");
		return ast_vhub_simple_reply(ep, 0, 0, 0, 0);
	case GetPortStatus:
		EPDBG(ep, "GetPortStatus(%d)\n", wIndex & 0xff);
		return ast_vhub_get_port_stat(ep, wIndex & 0xf);
	case GetHubDescriptor:
		if (wValue != (USB_DT_HUB << 8))
			return std_req_stall;
		EPDBG(ep, "GetHubDescriptor(%d)\n", wIndex & 0xff);
		return ast_vhub_rep_desc(ep, USB_DT_HUB, wLength);
	case SetHubFeature:
	case ClearHubFeature:
		EPDBG(ep, "Get/SetHubFeature(%d)\n", wValue);
		/* No feature, just complete the requests */
		if (wValue == C_HUB_LOCAL_POWER ||
		    wValue == C_HUB_OVER_CURRENT)
			return std_req_complete;
		return std_req_stall;
	case SetPortFeature:
		EPDBG(ep, "SetPortFeature(%d,%d)\n", wIndex & 0xf, wValue);
		return ast_vhub_set_port_feature(ep, wIndex & 0xf, wValue);
	case ClearPortFeature:
		EPDBG(ep, "ClearPortFeature(%d,%d)\n", wIndex & 0xf, wValue);
		return ast_vhub_clr_port_feature(ep, wIndex & 0xf, wValue);
	default:
		EPDBG(ep, "Unknown class request\n");
	}
	return std_req_stall;
}

void ast_vhub_hub_suspend(struct ast_vhub *vhub)
{
	unsigned int i;

	UDCDBG(vhub, "USB bus suspend\n");

	if (vhub->suspended)
		return;

	vhub->suspended = true;

	/*
	 * Forward to unsuspended ports without changing
	 * their connection status.
	 */
	for (i = 0; i < AST_VHUB_NUM_PORTS; i++) {
		struct ast_vhub_port *p = &vhub->ports[i];

		if (!(p->status & USB_PORT_STAT_SUSPEND))
			ast_vhub_dev_suspend(&p->dev);
	}
}

void ast_vhub_hub_resume(struct ast_vhub *vhub)
{
	unsigned int i;

	UDCDBG(vhub, "USB bus resume\n");

	if (!vhub->suspended)
		return;

	vhub->suspended = false;

	/*
	 * Forward to unsuspended ports without changing
	 * their connection status.
	 */
	for (i = 0; i < AST_VHUB_NUM_PORTS; i++) {
		struct ast_vhub_port *p = &vhub->ports[i];

		if (!(p->status & USB_PORT_STAT_SUSPEND))
			ast_vhub_dev_resume(&p->dev);
	}
}

void ast_vhub_hub_reset(struct ast_vhub *vhub)
{
	unsigned int i;

	UDCDBG(vhub, "USB bus reset\n");

	/*
	 * Is the speed known ? If not we don't care, we aren't
	 * initialized yet and ports haven't been enabled.
	 */
	if (vhub->speed == USB_SPEED_UNKNOWN)
		return;

	/* We aren't suspended anymore obviously */
	vhub->suspended = false;

	/* No speed set */
	vhub->speed = USB_SPEED_UNKNOWN;

	/* Wakeup not enabled anymore */
	vhub->wakeup_en = false;

	/*
	 * Clear all port status, disable gadgets and "suspend"
	 * them. They will be woken up by a port reset.
	 */
	for (i = 0; i < AST_VHUB_NUM_PORTS; i++) {
		struct ast_vhub_port *p = &vhub->ports[i];

		/* Only keep the connected flag */
		p->status &= USB_PORT_STAT_CONNECTION;
		p->change = 0;

		/* Suspend the gadget if any */
		ast_vhub_dev_suspend(&p->dev);
	}

	/* Cleanup HW */
	writel(0, vhub->regs + AST_VHUB_CONF);
	writel(0, vhub->regs + AST_VHUB_EP0_CTRL);
	writel(VHUB_EP1_CTRL_RESET_TOGGLE |
	       VHUB_EP1_CTRL_ENABLE,
	       vhub->regs + AST_VHUB_EP1_CTRL);
	writel(0, vhub->regs + AST_VHUB_EP1_STS_CHG);
}

void ast_vhub_init_hub(struct ast_vhub *vhub)
{
	vhub->speed = USB_SPEED_UNKNOWN;
	INIT_WORK(&vhub->wake_work, ast_vhub_wake_work);
}

