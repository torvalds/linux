// SPDX-License-Identifier: GPL-2.0+
/*
 * aspeed-vhub -- Driver for Aspeed SoC "vHub" USB gadget
 *
 * dev.c - Individual device/gadget management (ie, a port = a gadget)
 *
 * Copyright 2017 IBM Corporation
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
#include <linux/regmap.h>
#include <linux/dma-mapping.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>

#include "vhub.h"

void ast_vhub_dev_irq(struct ast_vhub_dev *d)
{
	u32 istat = readl(d->regs + AST_VHUB_DEV_ISR);

	writel(istat, d->regs + AST_VHUB_DEV_ISR);

	if (istat & VHUV_DEV_IRQ_EP0_IN_ACK_STALL)
		ast_vhub_ep0_handle_ack(&d->ep0, true);
	if (istat & VHUV_DEV_IRQ_EP0_OUT_ACK_STALL)
		ast_vhub_ep0_handle_ack(&d->ep0, false);
	if (istat & VHUV_DEV_IRQ_EP0_SETUP)
		ast_vhub_ep0_handle_setup(&d->ep0);
}

static void ast_vhub_dev_enable(struct ast_vhub_dev *d)
{
	u32 reg, hmsk, i;

	if (d->enabled)
		return;

	/* Cleanup EP0 state */
	ast_vhub_reset_ep0(d);

	/* Enable device and its EP0 interrupts */
	reg = VHUB_DEV_EN_ENABLE_PORT |
		VHUB_DEV_EN_EP0_IN_ACK_IRQEN |
		VHUB_DEV_EN_EP0_OUT_ACK_IRQEN |
		VHUB_DEV_EN_EP0_SETUP_IRQEN;
	if (d->gadget.speed == USB_SPEED_HIGH)
		reg |= VHUB_DEV_EN_SPEED_SEL_HIGH;
	writel(reg, d->regs + AST_VHUB_DEV_EN_CTRL);

	/* Enable device interrupt in the hub as well */
	hmsk = VHUB_IRQ_DEVICE1 << d->index;
	reg = readl(d->vhub->regs + AST_VHUB_IER);
	reg |= hmsk;
	writel(reg, d->vhub->regs + AST_VHUB_IER);

	/* Set EP0 DMA buffer address */
	writel(d->ep0.buf_dma, d->regs + AST_VHUB_DEV_EP0_DATA);

	/* Clear stall on all EPs */
	for (i = 0; i < d->max_epns; i++) {
		struct ast_vhub_ep *ep = d->epns[i];

		if (ep && (ep->epn.stalled || ep->epn.wedged)) {
			ep->epn.stalled = false;
			ep->epn.wedged = false;
			ast_vhub_update_epn_stall(ep);
		}
	}

	/* Additional cleanups */
	d->wakeup_en = false;
	d->enabled = true;
}

static void ast_vhub_dev_disable(struct ast_vhub_dev *d)
{
	u32 reg, hmsk;

	if (!d->enabled)
		return;

	/* Disable device interrupt in the hub */
	hmsk = VHUB_IRQ_DEVICE1 << d->index;
	reg = readl(d->vhub->regs + AST_VHUB_IER);
	reg &= ~hmsk;
	writel(reg, d->vhub->regs + AST_VHUB_IER);

	/* Then disable device */
	writel(0, d->regs + AST_VHUB_DEV_EN_CTRL);
	d->gadget.speed = USB_SPEED_UNKNOWN;
	d->enabled = false;
}

static int ast_vhub_dev_feature(struct ast_vhub_dev *d,
				u16 wIndex, u16 wValue,
				bool is_set)
{
	u32 val;

	DDBG(d, "%s_FEATURE(dev val=%02x)\n",
	     is_set ? "SET" : "CLEAR", wValue);

	if (wValue == USB_DEVICE_REMOTE_WAKEUP) {
		d->wakeup_en = is_set;
		return std_req_complete;
	}

	if (wValue == USB_DEVICE_TEST_MODE) {
		val = readl(d->vhub->regs + AST_VHUB_CTRL);
		val &= ~GENMASK(10, 8);
		val |= VHUB_CTRL_SET_TEST_MODE((wIndex >> 8) & 0x7);
		writel(val, d->vhub->regs + AST_VHUB_CTRL);

		return std_req_complete;
	}

	return std_req_driver;
}

static int ast_vhub_ep_feature(struct ast_vhub_dev *d,
			       u16 wIndex, u16 wValue, bool is_set)
{
	struct ast_vhub_ep *ep;
	int ep_num;

	ep_num = wIndex & USB_ENDPOINT_NUMBER_MASK;
	DDBG(d, "%s_FEATURE(ep%d val=%02x)\n",
	     is_set ? "SET" : "CLEAR", ep_num, wValue);
	if (ep_num == 0)
		return std_req_complete;
	if (ep_num >= d->max_epns || !d->epns[ep_num - 1])
		return std_req_stall;
	if (wValue != USB_ENDPOINT_HALT)
		return std_req_driver;

	ep = d->epns[ep_num - 1];
	if (WARN_ON(!ep))
		return std_req_stall;

	if (!ep->epn.enabled || !ep->ep.desc || ep->epn.is_iso ||
	    ep->epn.is_in != !!(wIndex & USB_DIR_IN))
		return std_req_stall;

	DDBG(d, "%s stall on EP %d\n",
	     is_set ? "setting" : "clearing", ep_num);
	ep->epn.stalled = is_set;
	ast_vhub_update_epn_stall(ep);

	return std_req_complete;
}

static int ast_vhub_dev_status(struct ast_vhub_dev *d,
			       u16 wIndex, u16 wValue)
{
	u8 st0;

	DDBG(d, "GET_STATUS(dev)\n");

	st0 = d->gadget.is_selfpowered << USB_DEVICE_SELF_POWERED;
	if (d->wakeup_en)
		st0 |= 1 << USB_DEVICE_REMOTE_WAKEUP;

	return ast_vhub_simple_reply(&d->ep0, st0, 0);
}

static int ast_vhub_ep_status(struct ast_vhub_dev *d,
			      u16 wIndex, u16 wValue)
{
	int ep_num = wIndex & USB_ENDPOINT_NUMBER_MASK;
	struct ast_vhub_ep *ep;
	u8 st0 = 0;

	DDBG(d, "GET_STATUS(ep%d)\n", ep_num);

	if (ep_num >= d->max_epns)
		return std_req_stall;
	if (ep_num != 0) {
		ep = d->epns[ep_num - 1];
		if (!ep)
			return std_req_stall;
		if (!ep->epn.enabled || !ep->ep.desc || ep->epn.is_iso ||
		    ep->epn.is_in != !!(wIndex & USB_DIR_IN))
			return std_req_stall;
		if (ep->epn.stalled)
			st0 |= 1 << USB_ENDPOINT_HALT;
	}

	return ast_vhub_simple_reply(&d->ep0, st0, 0);
}

static void ast_vhub_dev_set_address(struct ast_vhub_dev *d, u8 addr)
{
	u32 reg;

	DDBG(d, "SET_ADDRESS: Got address %x\n", addr);

	reg = readl(d->regs + AST_VHUB_DEV_EN_CTRL);
	reg &= ~VHUB_DEV_EN_ADDR_MASK;
	reg |= VHUB_DEV_EN_SET_ADDR(addr);
	writel(reg, d->regs + AST_VHUB_DEV_EN_CTRL);
}

int ast_vhub_std_dev_request(struct ast_vhub_ep *ep,
			     struct usb_ctrlrequest *crq)
{
	struct ast_vhub_dev *d = ep->dev;
	u16 wValue, wIndex;

	/* No driver, we shouldn't be enabled ... */
	if (!d->driver || !d->enabled) {
		EPDBG(ep,
		      "Device is wrong state driver=%p enabled=%d\n",
		      d->driver, d->enabled);
		return std_req_stall;
	}

	/*
	 * Note: we used to reject/stall requests while suspended,
	 * we don't do that anymore as we seem to have cases of
	 * mass storage getting very upset.
	 */

	/* First packet, grab speed */
	if (d->gadget.speed == USB_SPEED_UNKNOWN) {
		d->gadget.speed = ep->vhub->speed;
		if (d->gadget.speed > d->driver->max_speed)
			d->gadget.speed = d->driver->max_speed;
		DDBG(d, "fist packet, captured speed %d\n",
		     d->gadget.speed);
	}

	wValue = le16_to_cpu(crq->wValue);
	wIndex = le16_to_cpu(crq->wIndex);

	switch ((crq->bRequestType << 8) | crq->bRequest) {
		/* SET_ADDRESS */
	case DeviceOutRequest | USB_REQ_SET_ADDRESS:
		ast_vhub_dev_set_address(d, wValue);
		return std_req_complete;

		/* GET_STATUS */
	case DeviceRequest | USB_REQ_GET_STATUS:
		return ast_vhub_dev_status(d, wIndex, wValue);
	case InterfaceRequest | USB_REQ_GET_STATUS:
		return ast_vhub_simple_reply(ep, 0, 0);
	case EndpointRequest | USB_REQ_GET_STATUS:
		return ast_vhub_ep_status(d, wIndex, wValue);

		/* SET/CLEAR_FEATURE */
	case DeviceOutRequest | USB_REQ_SET_FEATURE:
		return ast_vhub_dev_feature(d, wIndex, wValue, true);
	case DeviceOutRequest | USB_REQ_CLEAR_FEATURE:
		return ast_vhub_dev_feature(d, wIndex, wValue, false);
	case EndpointOutRequest | USB_REQ_SET_FEATURE:
		return ast_vhub_ep_feature(d, wIndex, wValue, true);
	case EndpointOutRequest | USB_REQ_CLEAR_FEATURE:
		return ast_vhub_ep_feature(d, wIndex, wValue, false);
	}
	return std_req_driver;
}

static int ast_vhub_udc_wakeup(struct usb_gadget* gadget)
{
	struct ast_vhub_dev *d = to_ast_dev(gadget);
	unsigned long flags;
	int rc = -EINVAL;

	spin_lock_irqsave(&d->vhub->lock, flags);
	if (!d->wakeup_en)
		goto err;

	DDBG(d, "Device initiated wakeup\n");

	/* Wakeup the host */
	ast_vhub_hub_wake_all(d->vhub);
	rc = 0;
 err:
	spin_unlock_irqrestore(&d->vhub->lock, flags);
	return rc;
}

static int ast_vhub_udc_get_frame(struct usb_gadget* gadget)
{
	struct ast_vhub_dev *d = to_ast_dev(gadget);

	return (readl(d->vhub->regs + AST_VHUB_USBSTS) >> 16) & 0x7ff;
}

static void ast_vhub_dev_nuke(struct ast_vhub_dev *d)
{
	unsigned int i;

	for (i = 0; i < d->max_epns; i++) {
		if (!d->epns[i])
			continue;
		ast_vhub_nuke(d->epns[i], -ESHUTDOWN);
	}
}

static int ast_vhub_udc_pullup(struct usb_gadget* gadget, int on)
{
	struct ast_vhub_dev *d = to_ast_dev(gadget);
	unsigned long flags;

	spin_lock_irqsave(&d->vhub->lock, flags);

	DDBG(d, "pullup(%d)\n", on);

	/* Mark disconnected in the hub */
	ast_vhub_device_connect(d->vhub, d->index, on);

	/*
	 * If enabled, nuke all requests if any (there shouldn't be)
	 * and disable the port. This will clear the address too.
	 */
	if (d->enabled) {
		ast_vhub_dev_nuke(d);
		ast_vhub_dev_disable(d);
	}

	spin_unlock_irqrestore(&d->vhub->lock, flags);

	return 0;
}

static int ast_vhub_udc_start(struct usb_gadget *gadget,
			      struct usb_gadget_driver *driver)
{
	struct ast_vhub_dev *d = to_ast_dev(gadget);
	unsigned long flags;

	spin_lock_irqsave(&d->vhub->lock, flags);

	DDBG(d, "start\n");

	/* We don't do much more until the hub enables us */
	d->driver = driver;
	d->gadget.is_selfpowered = 1;

	spin_unlock_irqrestore(&d->vhub->lock, flags);

	return 0;
}

static struct usb_ep *ast_vhub_udc_match_ep(struct usb_gadget *gadget,
					    struct usb_endpoint_descriptor *desc,
					    struct usb_ss_ep_comp_descriptor *ss)
{
	struct ast_vhub_dev *d = to_ast_dev(gadget);
	struct ast_vhub_ep *ep;
	struct usb_ep *u_ep;
	unsigned int max, addr, i;

	DDBG(d, "Match EP type %d\n", usb_endpoint_type(desc));

	/*
	 * First we need to look for an existing unclaimed EP as another
	 * configuration may have already associated a bunch of EPs with
	 * this gadget. This duplicates the code in usb_ep_autoconfig_ss()
	 * unfortunately.
	 */
	list_for_each_entry(u_ep, &gadget->ep_list, ep_list) {
		if (usb_gadget_ep_match_desc(gadget, u_ep, desc, ss)) {
			DDBG(d, " -> using existing EP%d\n",
			     to_ast_ep(u_ep)->d_idx);
			return u_ep;
		}
	}

	/*
	 * We didn't find one, we need to grab one from the pool.
	 *
	 * First let's do some sanity checking
	 */
	switch(usb_endpoint_type(desc)) {
	case USB_ENDPOINT_XFER_CONTROL:
		/* Only EP0 can be a control endpoint */
		return NULL;
	case USB_ENDPOINT_XFER_ISOC:
		/* ISO:	 limit 1023 bytes full speed, 1024 high/super speed */
		if (gadget_is_dualspeed(gadget))
			max = 1024;
		else
			max = 1023;
		break;
	case USB_ENDPOINT_XFER_BULK:
		if (gadget_is_dualspeed(gadget))
			max = 512;
		else
			max = 64;
		break;
	case USB_ENDPOINT_XFER_INT:
		if (gadget_is_dualspeed(gadget))
			max = 1024;
		else
			max = 64;
		break;
	}
	if (usb_endpoint_maxp(desc) > max)
		return NULL;

	/*
	 * Find a free EP address for that device. We can't
	 * let the generic code assign these as it would
	 * create overlapping numbers for IN and OUT which
	 * we don't support, so also create a suitable name
	 * that will allow the generic code to use our
	 * assigned address.
	 */
	for (i = 0; i < d->max_epns; i++)
		if (d->epns[i] == NULL)
			break;
	if (i >= d->max_epns)
		return NULL;
	addr = i + 1;

	/*
	 * Now grab an EP from the shared pool and associate
	 * it with our device
	 */
	ep = ast_vhub_alloc_epn(d, addr);
	if (!ep)
		return NULL;
	DDBG(d, "Allocated epn#%d for port EP%d\n",
	     ep->epn.g_idx, addr);

	return &ep->ep;
}

static int ast_vhub_udc_stop(struct usb_gadget *gadget)
{
	struct ast_vhub_dev *d = to_ast_dev(gadget);
	unsigned long flags;

	spin_lock_irqsave(&d->vhub->lock, flags);

	DDBG(d, "stop\n");

	d->driver = NULL;
	d->gadget.speed = USB_SPEED_UNKNOWN;

	ast_vhub_dev_nuke(d);

	if (d->enabled)
		ast_vhub_dev_disable(d);

	spin_unlock_irqrestore(&d->vhub->lock, flags);

	return 0;
}

static const struct usb_gadget_ops ast_vhub_udc_ops = {
	.get_frame	= ast_vhub_udc_get_frame,
	.wakeup		= ast_vhub_udc_wakeup,
	.pullup		= ast_vhub_udc_pullup,
	.udc_start	= ast_vhub_udc_start,
	.udc_stop	= ast_vhub_udc_stop,
	.match_ep	= ast_vhub_udc_match_ep,
};

void ast_vhub_dev_suspend(struct ast_vhub_dev *d)
{
	if (d->driver && d->driver->suspend) {
		spin_unlock(&d->vhub->lock);
		d->driver->suspend(&d->gadget);
		spin_lock(&d->vhub->lock);
	}
}

void ast_vhub_dev_resume(struct ast_vhub_dev *d)
{
	if (d->driver && d->driver->resume) {
		spin_unlock(&d->vhub->lock);
		d->driver->resume(&d->gadget);
		spin_lock(&d->vhub->lock);
	}
}

void ast_vhub_dev_reset(struct ast_vhub_dev *d)
{
	/* No driver, just disable the device and return */
	if (!d->driver) {
		ast_vhub_dev_disable(d);
		return;
	}

	/* If the port isn't enabled, just enable it */
	if (!d->enabled) {
		DDBG(d, "Reset of disabled device, enabling...\n");
		ast_vhub_dev_enable(d);
	} else {
		DDBG(d, "Reset of enabled device, resetting...\n");
		spin_unlock(&d->vhub->lock);
		usb_gadget_udc_reset(&d->gadget, d->driver);
		spin_lock(&d->vhub->lock);

		/*
		 * Disable and maybe re-enable HW, this will clear the address
		 * and speed setting.
		 */
		ast_vhub_dev_disable(d);
		ast_vhub_dev_enable(d);
	}
}

void ast_vhub_del_dev(struct ast_vhub_dev *d)
{
	unsigned long flags;

	spin_lock_irqsave(&d->vhub->lock, flags);
	if (!d->registered) {
		spin_unlock_irqrestore(&d->vhub->lock, flags);
		return;
	}
	d->registered = false;
	spin_unlock_irqrestore(&d->vhub->lock, flags);

	usb_del_gadget_udc(&d->gadget);
	device_unregister(d->port_dev);
	kfree(d->epns);
}

static void ast_vhub_dev_release(struct device *dev)
{
	kfree(dev);
}

int ast_vhub_init_dev(struct ast_vhub *vhub, unsigned int idx)
{
	struct ast_vhub_dev *d = &vhub->ports[idx].dev;
	struct device *parent = &vhub->pdev->dev;
	int rc;

	d->vhub = vhub;
	d->index = idx;
	d->name = devm_kasprintf(parent, GFP_KERNEL, "port%d", idx+1);
	d->regs = vhub->regs + 0x100 + 0x10 * idx;

	ast_vhub_init_ep0(vhub, &d->ep0, d);

	/*
	 * A USB device can have up to 30 endpoints besides control
	 * endpoint 0.
	 */
	d->max_epns = min_t(u32, vhub->max_epns, 30);
	d->epns = kcalloc(d->max_epns, sizeof(*d->epns), GFP_KERNEL);
	if (!d->epns)
		return -ENOMEM;

	/*
	 * The UDC core really needs us to have separate and uniquely
	 * named "parent" devices for each port so we create a sub device
	 * here for that purpose
	 */
	d->port_dev = kzalloc(sizeof(struct device), GFP_KERNEL);
	if (!d->port_dev) {
		rc = -ENOMEM;
		goto fail_alloc;
	}
	device_initialize(d->port_dev);
	d->port_dev->release = ast_vhub_dev_release;
	d->port_dev->parent = parent;
	dev_set_name(d->port_dev, "%s:p%d", dev_name(parent), idx + 1);
	rc = device_add(d->port_dev);
	if (rc)
		goto fail_add;

	/* Populate gadget */
	INIT_LIST_HEAD(&d->gadget.ep_list);
	d->gadget.ops = &ast_vhub_udc_ops;
	d->gadget.ep0 = &d->ep0.ep;
	d->gadget.name = KBUILD_MODNAME;
	if (vhub->force_usb1)
		d->gadget.max_speed = USB_SPEED_FULL;
	else
		d->gadget.max_speed = USB_SPEED_HIGH;
	d->gadget.speed = USB_SPEED_UNKNOWN;
	d->gadget.dev.of_node = vhub->pdev->dev.of_node;
	d->gadget.dev.of_node_reused = true;

	rc = usb_add_gadget_udc(d->port_dev, &d->gadget);
	if (rc != 0)
		goto fail_udc;
	d->registered = true;

	return 0;
 fail_udc:
	device_del(d->port_dev);
 fail_add:
	put_device(d->port_dev);
 fail_alloc:
	kfree(d->epns);

	return rc;
}
