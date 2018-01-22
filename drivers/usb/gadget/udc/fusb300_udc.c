// SPDX-License-Identifier: GPL-2.0
/*
 * Fusb300 UDC (USB gadget)
 *
 * Copyright (C) 2010 Faraday Technology Corp.
 *
 * Author : Yuan-hsin Chen <yhchen@faraday-tech.com>
 */
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>

#include "fusb300_udc.h"

MODULE_DESCRIPTION("FUSB300  USB gadget driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yuan-Hsin Chen, Feng-Hsin Chiang <john453@faraday-tech.com>");
MODULE_ALIAS("platform:fusb300_udc");

#define DRIVER_VERSION	"20 October 2010"

static const char udc_name[] = "fusb300_udc";
static const char * const fusb300_ep_name[] = {
	"ep0", "ep1", "ep2", "ep3", "ep4", "ep5", "ep6", "ep7", "ep8", "ep9",
	"ep10", "ep11", "ep12", "ep13", "ep14", "ep15"
};

static void done(struct fusb300_ep *ep, struct fusb300_request *req,
		 int status);

static void fusb300_enable_bit(struct fusb300 *fusb300, u32 offset,
			       u32 value)
{
	u32 reg = ioread32(fusb300->reg + offset);

	reg |= value;
	iowrite32(reg, fusb300->reg + offset);
}

static void fusb300_disable_bit(struct fusb300 *fusb300, u32 offset,
				u32 value)
{
	u32 reg = ioread32(fusb300->reg + offset);

	reg &= ~value;
	iowrite32(reg, fusb300->reg + offset);
}


static void fusb300_ep_setting(struct fusb300_ep *ep,
			       struct fusb300_ep_info info)
{
	ep->epnum = info.epnum;
	ep->type = info.type;
}

static int fusb300_ep_release(struct fusb300_ep *ep)
{
	if (!ep->epnum)
		return 0;
	ep->epnum = 0;
	ep->stall = 0;
	ep->wedged = 0;
	return 0;
}

static void fusb300_set_fifo_entry(struct fusb300 *fusb300,
				   u32 ep)
{
	u32 val = ioread32(fusb300->reg + FUSB300_OFFSET_EPSET1(ep));

	val &= ~FUSB300_EPSET1_FIFOENTRY_MSK;
	val |= FUSB300_EPSET1_FIFOENTRY(FUSB300_FIFO_ENTRY_NUM);
	iowrite32(val, fusb300->reg + FUSB300_OFFSET_EPSET1(ep));
}

static void fusb300_set_start_entry(struct fusb300 *fusb300,
				    u8 ep)
{
	u32 reg = ioread32(fusb300->reg + FUSB300_OFFSET_EPSET1(ep));
	u32 start_entry = fusb300->fifo_entry_num * FUSB300_FIFO_ENTRY_NUM;

	reg &= ~FUSB300_EPSET1_START_ENTRY_MSK	;
	reg |= FUSB300_EPSET1_START_ENTRY(start_entry);
	iowrite32(reg, fusb300->reg + FUSB300_OFFSET_EPSET1(ep));
	if (fusb300->fifo_entry_num == FUSB300_MAX_FIFO_ENTRY) {
		fusb300->fifo_entry_num = 0;
		fusb300->addrofs = 0;
		pr_err("fifo entry is over the maximum number!\n");
	} else
		fusb300->fifo_entry_num++;
}

/* set fusb300_set_start_entry first before fusb300_set_epaddrofs */
static void fusb300_set_epaddrofs(struct fusb300 *fusb300,
				  struct fusb300_ep_info info)
{
	u32 reg = ioread32(fusb300->reg + FUSB300_OFFSET_EPSET2(info.epnum));

	reg &= ~FUSB300_EPSET2_ADDROFS_MSK;
	reg |= FUSB300_EPSET2_ADDROFS(fusb300->addrofs);
	iowrite32(reg, fusb300->reg + FUSB300_OFFSET_EPSET2(info.epnum));
	fusb300->addrofs += (info.maxpacket + 7) / 8 * FUSB300_FIFO_ENTRY_NUM;
}

static void ep_fifo_setting(struct fusb300 *fusb300,
			    struct fusb300_ep_info info)
{
	fusb300_set_fifo_entry(fusb300, info.epnum);
	fusb300_set_start_entry(fusb300, info.epnum);
	fusb300_set_epaddrofs(fusb300, info);
}

static void fusb300_set_eptype(struct fusb300 *fusb300,
			       struct fusb300_ep_info info)
{
	u32 reg = ioread32(fusb300->reg + FUSB300_OFFSET_EPSET1(info.epnum));

	reg &= ~FUSB300_EPSET1_TYPE_MSK;
	reg |= FUSB300_EPSET1_TYPE(info.type);
	iowrite32(reg, fusb300->reg + FUSB300_OFFSET_EPSET1(info.epnum));
}

static void fusb300_set_epdir(struct fusb300 *fusb300,
			      struct fusb300_ep_info info)
{
	u32 reg;

	if (!info.dir_in)
		return;
	reg = ioread32(fusb300->reg + FUSB300_OFFSET_EPSET1(info.epnum));
	reg &= ~FUSB300_EPSET1_DIR_MSK;
	reg |= FUSB300_EPSET1_DIRIN;
	iowrite32(reg, fusb300->reg + FUSB300_OFFSET_EPSET1(info.epnum));
}

static void fusb300_set_ep_active(struct fusb300 *fusb300,
			  u8 ep)
{
	u32 reg = ioread32(fusb300->reg + FUSB300_OFFSET_EPSET1(ep));

	reg |= FUSB300_EPSET1_ACTEN;
	iowrite32(reg, fusb300->reg + FUSB300_OFFSET_EPSET1(ep));
}

static void fusb300_set_epmps(struct fusb300 *fusb300,
			      struct fusb300_ep_info info)
{
	u32 reg = ioread32(fusb300->reg + FUSB300_OFFSET_EPSET2(info.epnum));

	reg &= ~FUSB300_EPSET2_MPS_MSK;
	reg |= FUSB300_EPSET2_MPS(info.maxpacket);
	iowrite32(reg, fusb300->reg + FUSB300_OFFSET_EPSET2(info.epnum));
}

static void fusb300_set_interval(struct fusb300 *fusb300,
				 struct fusb300_ep_info info)
{
	u32 reg = ioread32(fusb300->reg + FUSB300_OFFSET_EPSET1(info.epnum));

	reg &= ~FUSB300_EPSET1_INTERVAL(0x7);
	reg |= FUSB300_EPSET1_INTERVAL(info.interval);
	iowrite32(reg, fusb300->reg + FUSB300_OFFSET_EPSET1(info.epnum));
}

static void fusb300_set_bwnum(struct fusb300 *fusb300,
			      struct fusb300_ep_info info)
{
	u32 reg = ioread32(fusb300->reg + FUSB300_OFFSET_EPSET1(info.epnum));

	reg &= ~FUSB300_EPSET1_BWNUM(0x3);
	reg |= FUSB300_EPSET1_BWNUM(info.bw_num);
	iowrite32(reg, fusb300->reg + FUSB300_OFFSET_EPSET1(info.epnum));
}

static void set_ep_reg(struct fusb300 *fusb300,
		      struct fusb300_ep_info info)
{
	fusb300_set_eptype(fusb300, info);
	fusb300_set_epdir(fusb300, info);
	fusb300_set_epmps(fusb300, info);

	if (info.interval)
		fusb300_set_interval(fusb300, info);

	if (info.bw_num)
		fusb300_set_bwnum(fusb300, info);

	fusb300_set_ep_active(fusb300, info.epnum);
}

static int config_ep(struct fusb300_ep *ep,
		     const struct usb_endpoint_descriptor *desc)
{
	struct fusb300 *fusb300 = ep->fusb300;
	struct fusb300_ep_info info;

	ep->ep.desc = desc;

	info.interval = 0;
	info.addrofs = 0;
	info.bw_num = 0;

	info.type = desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK;
	info.dir_in = (desc->bEndpointAddress & USB_ENDPOINT_DIR_MASK) ? 1 : 0;
	info.maxpacket = usb_endpoint_maxp(desc);
	info.epnum = desc->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK;

	if ((info.type == USB_ENDPOINT_XFER_INT) ||
	   (info.type == USB_ENDPOINT_XFER_ISOC)) {
		info.interval = desc->bInterval;
		if (info.type == USB_ENDPOINT_XFER_ISOC)
			info.bw_num = usb_endpoint_maxp_mult(desc);
	}

	ep_fifo_setting(fusb300, info);

	set_ep_reg(fusb300, info);

	fusb300_ep_setting(ep, info);

	fusb300->ep[info.epnum] = ep;

	return 0;
}

static int fusb300_enable(struct usb_ep *_ep,
			  const struct usb_endpoint_descriptor *desc)
{
	struct fusb300_ep *ep;

	ep = container_of(_ep, struct fusb300_ep, ep);

	if (ep->fusb300->reenum) {
		ep->fusb300->fifo_entry_num = 0;
		ep->fusb300->addrofs = 0;
		ep->fusb300->reenum = 0;
	}

	return config_ep(ep, desc);
}

static int fusb300_disable(struct usb_ep *_ep)
{
	struct fusb300_ep *ep;
	struct fusb300_request *req;
	unsigned long flags;

	ep = container_of(_ep, struct fusb300_ep, ep);

	BUG_ON(!ep);

	while (!list_empty(&ep->queue)) {
		req = list_entry(ep->queue.next, struct fusb300_request, queue);
		spin_lock_irqsave(&ep->fusb300->lock, flags);
		done(ep, req, -ECONNRESET);
		spin_unlock_irqrestore(&ep->fusb300->lock, flags);
	}

	return fusb300_ep_release(ep);
}

static struct usb_request *fusb300_alloc_request(struct usb_ep *_ep,
						gfp_t gfp_flags)
{
	struct fusb300_request *req;

	req = kzalloc(sizeof(struct fusb300_request), gfp_flags);
	if (!req)
		return NULL;
	INIT_LIST_HEAD(&req->queue);

	return &req->req;
}

static void fusb300_free_request(struct usb_ep *_ep, struct usb_request *_req)
{
	struct fusb300_request *req;

	req = container_of(_req, struct fusb300_request, req);
	kfree(req);
}

static int enable_fifo_int(struct fusb300_ep *ep)
{
	struct fusb300 *fusb300 = ep->fusb300;

	if (ep->epnum) {
		fusb300_enable_bit(fusb300, FUSB300_OFFSET_IGER0,
			FUSB300_IGER0_EEPn_FIFO_INT(ep->epnum));
	} else {
		pr_err("can't enable_fifo_int ep0\n");
		return -EINVAL;
	}

	return 0;
}

static int disable_fifo_int(struct fusb300_ep *ep)
{
	struct fusb300 *fusb300 = ep->fusb300;

	if (ep->epnum) {
		fusb300_disable_bit(fusb300, FUSB300_OFFSET_IGER0,
			FUSB300_IGER0_EEPn_FIFO_INT(ep->epnum));
	} else {
		pr_err("can't disable_fifo_int ep0\n");
		return -EINVAL;
	}

	return 0;
}

static void fusb300_set_cxlen(struct fusb300 *fusb300, u32 length)
{
	u32 reg;

	reg = ioread32(fusb300->reg + FUSB300_OFFSET_CSR);
	reg &= ~FUSB300_CSR_LEN_MSK;
	reg |= FUSB300_CSR_LEN(length);
	iowrite32(reg, fusb300->reg + FUSB300_OFFSET_CSR);
}

/* write data to cx fifo */
static void fusb300_wrcxf(struct fusb300_ep *ep,
		   struct fusb300_request *req)
{
	int i = 0;
	u8 *tmp;
	u32 data;
	struct fusb300 *fusb300 = ep->fusb300;
	u32 length = req->req.length - req->req.actual;

	tmp = req->req.buf + req->req.actual;

	if (length > SS_CTL_MAX_PACKET_SIZE) {
		fusb300_set_cxlen(fusb300, SS_CTL_MAX_PACKET_SIZE);
		for (i = (SS_CTL_MAX_PACKET_SIZE >> 2); i > 0; i--) {
			data = *tmp | *(tmp + 1) << 8 | *(tmp + 2) << 16 |
				*(tmp + 3) << 24;
			iowrite32(data, fusb300->reg + FUSB300_OFFSET_CXPORT);
			tmp += 4;
		}
		req->req.actual += SS_CTL_MAX_PACKET_SIZE;
	} else { /* length is less than max packet size */
		fusb300_set_cxlen(fusb300, length);
		for (i = length >> 2; i > 0; i--) {
			data = *tmp | *(tmp + 1) << 8 | *(tmp + 2) << 16 |
				*(tmp + 3) << 24;
			printk(KERN_DEBUG "    0x%x\n", data);
			iowrite32(data, fusb300->reg + FUSB300_OFFSET_CXPORT);
			tmp = tmp + 4;
		}
		switch (length % 4) {
		case 1:
			data = *tmp;
			printk(KERN_DEBUG "    0x%x\n", data);
			iowrite32(data, fusb300->reg + FUSB300_OFFSET_CXPORT);
			break;
		case 2:
			data = *tmp | *(tmp + 1) << 8;
			printk(KERN_DEBUG "    0x%x\n", data);
			iowrite32(data, fusb300->reg + FUSB300_OFFSET_CXPORT);
			break;
		case 3:
			data = *tmp | *(tmp + 1) << 8 | *(tmp + 2) << 16;
			printk(KERN_DEBUG "    0x%x\n", data);
			iowrite32(data, fusb300->reg + FUSB300_OFFSET_CXPORT);
			break;
		default:
			break;
		}
		req->req.actual += length;
	}
}

static void fusb300_set_epnstall(struct fusb300 *fusb300, u8 ep)
{
	fusb300_enable_bit(fusb300, FUSB300_OFFSET_EPSET0(ep),
		FUSB300_EPSET0_STL);
}

static void fusb300_clear_epnstall(struct fusb300 *fusb300, u8 ep)
{
	u32 reg = ioread32(fusb300->reg + FUSB300_OFFSET_EPSET0(ep));

	if (reg & FUSB300_EPSET0_STL) {
		printk(KERN_DEBUG "EP%d stall... Clear!!\n", ep);
		reg |= FUSB300_EPSET0_STL_CLR;
		iowrite32(reg, fusb300->reg + FUSB300_OFFSET_EPSET0(ep));
	}
}

static void ep0_queue(struct fusb300_ep *ep, struct fusb300_request *req)
{
	if (ep->fusb300->ep0_dir) { /* if IN */
		if (req->req.length) {
			fusb300_wrcxf(ep, req);
		} else
			printk(KERN_DEBUG "%s : req->req.length = 0x%x\n",
				__func__, req->req.length);
		if ((req->req.length == req->req.actual) ||
		    (req->req.actual < ep->ep.maxpacket))
			done(ep, req, 0);
	} else { /* OUT */
		if (!req->req.length)
			done(ep, req, 0);
		else
			fusb300_enable_bit(ep->fusb300, FUSB300_OFFSET_IGER1,
				FUSB300_IGER1_CX_OUT_INT);
	}
}

static int fusb300_queue(struct usb_ep *_ep, struct usb_request *_req,
			 gfp_t gfp_flags)
{
	struct fusb300_ep *ep;
	struct fusb300_request *req;
	unsigned long flags;
	int request  = 0;

	ep = container_of(_ep, struct fusb300_ep, ep);
	req = container_of(_req, struct fusb300_request, req);

	if (ep->fusb300->gadget.speed == USB_SPEED_UNKNOWN)
		return -ESHUTDOWN;

	spin_lock_irqsave(&ep->fusb300->lock, flags);

	if (list_empty(&ep->queue))
		request = 1;

	list_add_tail(&req->queue, &ep->queue);

	req->req.actual = 0;
	req->req.status = -EINPROGRESS;

	if (ep->ep.desc == NULL) /* ep0 */
		ep0_queue(ep, req);
	else if (request && !ep->stall)
		enable_fifo_int(ep);

	spin_unlock_irqrestore(&ep->fusb300->lock, flags);

	return 0;
}

static int fusb300_dequeue(struct usb_ep *_ep, struct usb_request *_req)
{
	struct fusb300_ep *ep;
	struct fusb300_request *req;
	unsigned long flags;

	ep = container_of(_ep, struct fusb300_ep, ep);
	req = container_of(_req, struct fusb300_request, req);

	spin_lock_irqsave(&ep->fusb300->lock, flags);
	if (!list_empty(&ep->queue))
		done(ep, req, -ECONNRESET);
	spin_unlock_irqrestore(&ep->fusb300->lock, flags);

	return 0;
}

static int fusb300_set_halt_and_wedge(struct usb_ep *_ep, int value, int wedge)
{
	struct fusb300_ep *ep;
	struct fusb300 *fusb300;
	unsigned long flags;
	int ret = 0;

	ep = container_of(_ep, struct fusb300_ep, ep);

	fusb300 = ep->fusb300;

	spin_lock_irqsave(&ep->fusb300->lock, flags);

	if (!list_empty(&ep->queue)) {
		ret = -EAGAIN;
		goto out;
	}

	if (value) {
		fusb300_set_epnstall(fusb300, ep->epnum);
		ep->stall = 1;
		if (wedge)
			ep->wedged = 1;
	} else {
		fusb300_clear_epnstall(fusb300, ep->epnum);
		ep->stall = 0;
		ep->wedged = 0;
	}

out:
	spin_unlock_irqrestore(&ep->fusb300->lock, flags);
	return ret;
}

static int fusb300_set_halt(struct usb_ep *_ep, int value)
{
	return fusb300_set_halt_and_wedge(_ep, value, 0);
}

static int fusb300_set_wedge(struct usb_ep *_ep)
{
	return fusb300_set_halt_and_wedge(_ep, 1, 1);
}

static void fusb300_fifo_flush(struct usb_ep *_ep)
{
}

static const struct usb_ep_ops fusb300_ep_ops = {
	.enable		= fusb300_enable,
	.disable	= fusb300_disable,

	.alloc_request	= fusb300_alloc_request,
	.free_request	= fusb300_free_request,

	.queue		= fusb300_queue,
	.dequeue	= fusb300_dequeue,

	.set_halt	= fusb300_set_halt,
	.fifo_flush	= fusb300_fifo_flush,
	.set_wedge	= fusb300_set_wedge,
};

/*****************************************************************************/
static void fusb300_clear_int(struct fusb300 *fusb300, u32 offset,
		       u32 value)
{
	iowrite32(value, fusb300->reg + offset);
}

static void fusb300_reset(void)
{
}

static void fusb300_set_cxstall(struct fusb300 *fusb300)
{
	fusb300_enable_bit(fusb300, FUSB300_OFFSET_CSR,
			   FUSB300_CSR_STL);
}

static void fusb300_set_cxdone(struct fusb300 *fusb300)
{
	fusb300_enable_bit(fusb300, FUSB300_OFFSET_CSR,
			   FUSB300_CSR_DONE);
}

/* read data from cx fifo */
static void fusb300_rdcxf(struct fusb300 *fusb300,
		   u8 *buffer, u32 length)
{
	int i = 0;
	u8 *tmp;
	u32 data;

	tmp = buffer;

	for (i = (length >> 2); i > 0; i--) {
		data = ioread32(fusb300->reg + FUSB300_OFFSET_CXPORT);
		printk(KERN_DEBUG "    0x%x\n", data);
		*tmp = data & 0xFF;
		*(tmp + 1) = (data >> 8) & 0xFF;
		*(tmp + 2) = (data >> 16) & 0xFF;
		*(tmp + 3) = (data >> 24) & 0xFF;
		tmp = tmp + 4;
	}

	switch (length % 4) {
	case 1:
		data = ioread32(fusb300->reg + FUSB300_OFFSET_CXPORT);
		printk(KERN_DEBUG "    0x%x\n", data);
		*tmp = data & 0xFF;
		break;
	case 2:
		data = ioread32(fusb300->reg + FUSB300_OFFSET_CXPORT);
		printk(KERN_DEBUG "    0x%x\n", data);
		*tmp = data & 0xFF;
		*(tmp + 1) = (data >> 8) & 0xFF;
		break;
	case 3:
		data = ioread32(fusb300->reg + FUSB300_OFFSET_CXPORT);
		printk(KERN_DEBUG "    0x%x\n", data);
		*tmp = data & 0xFF;
		*(tmp + 1) = (data >> 8) & 0xFF;
		*(tmp + 2) = (data >> 16) & 0xFF;
		break;
	default:
		break;
	}
}

static void fusb300_rdfifo(struct fusb300_ep *ep,
			  struct fusb300_request *req,
			  u32 length)
{
	int i = 0;
	u8 *tmp;
	u32 data, reg;
	struct fusb300 *fusb300 = ep->fusb300;

	tmp = req->req.buf + req->req.actual;
	req->req.actual += length;

	if (req->req.actual > req->req.length)
		printk(KERN_DEBUG "req->req.actual > req->req.length\n");

	for (i = (length >> 2); i > 0; i--) {
		data = ioread32(fusb300->reg +
			FUSB300_OFFSET_EPPORT(ep->epnum));
		*tmp = data & 0xFF;
		*(tmp + 1) = (data >> 8) & 0xFF;
		*(tmp + 2) = (data >> 16) & 0xFF;
		*(tmp + 3) = (data >> 24) & 0xFF;
		tmp = tmp + 4;
	}

	switch (length % 4) {
	case 1:
		data = ioread32(fusb300->reg +
			FUSB300_OFFSET_EPPORT(ep->epnum));
		*tmp = data & 0xFF;
		break;
	case 2:
		data = ioread32(fusb300->reg +
			FUSB300_OFFSET_EPPORT(ep->epnum));
		*tmp = data & 0xFF;
		*(tmp + 1) = (data >> 8) & 0xFF;
		break;
	case 3:
		data = ioread32(fusb300->reg +
			FUSB300_OFFSET_EPPORT(ep->epnum));
		*tmp = data & 0xFF;
		*(tmp + 1) = (data >> 8) & 0xFF;
		*(tmp + 2) = (data >> 16) & 0xFF;
		break;
	default:
		break;
	}

	do {
		reg = ioread32(fusb300->reg + FUSB300_OFFSET_IGR1);
		reg &= FUSB300_IGR1_SYNF0_EMPTY_INT;
		if (i)
			printk(KERN_INFO "sync fifo is not empty!\n");
		i++;
	} while (!reg);
}

static u8 fusb300_get_epnstall(struct fusb300 *fusb300, u8 ep)
{
	u8 value;
	u32 reg = ioread32(fusb300->reg + FUSB300_OFFSET_EPSET0(ep));

	value = reg & FUSB300_EPSET0_STL;

	return value;
}

static u8 fusb300_get_cxstall(struct fusb300 *fusb300)
{
	u8 value;
	u32 reg = ioread32(fusb300->reg + FUSB300_OFFSET_CSR);

	value = (reg & FUSB300_CSR_STL) >> 1;

	return value;
}

static void request_error(struct fusb300 *fusb300)
{
	fusb300_set_cxstall(fusb300);
	printk(KERN_DEBUG "request error!!\n");
}

static void get_status(struct fusb300 *fusb300, struct usb_ctrlrequest *ctrl)
__releases(fusb300->lock)
__acquires(fusb300->lock)
{
	u8 ep;
	u16 status = 0;
	u16 w_index = ctrl->wIndex;

	switch (ctrl->bRequestType & USB_RECIP_MASK) {
	case USB_RECIP_DEVICE:
		status = 1 << USB_DEVICE_SELF_POWERED;
		break;
	case USB_RECIP_INTERFACE:
		status = 0;
		break;
	case USB_RECIP_ENDPOINT:
		ep = w_index & USB_ENDPOINT_NUMBER_MASK;
		if (ep) {
			if (fusb300_get_epnstall(fusb300, ep))
				status = 1 << USB_ENDPOINT_HALT;
		} else {
			if (fusb300_get_cxstall(fusb300))
				status = 0;
		}
		break;

	default:
		request_error(fusb300);
		return;		/* exit */
	}

	fusb300->ep0_data = cpu_to_le16(status);
	fusb300->ep0_req->buf = &fusb300->ep0_data;
	fusb300->ep0_req->length = 2;

	spin_unlock(&fusb300->lock);
	fusb300_queue(fusb300->gadget.ep0, fusb300->ep0_req, GFP_KERNEL);
	spin_lock(&fusb300->lock);
}

static void set_feature(struct fusb300 *fusb300, struct usb_ctrlrequest *ctrl)
{
	u8 ep;

	switch (ctrl->bRequestType & USB_RECIP_MASK) {
	case USB_RECIP_DEVICE:
		fusb300_set_cxdone(fusb300);
		break;
	case USB_RECIP_INTERFACE:
		fusb300_set_cxdone(fusb300);
		break;
	case USB_RECIP_ENDPOINT: {
		u16 w_index = le16_to_cpu(ctrl->wIndex);

		ep = w_index & USB_ENDPOINT_NUMBER_MASK;
		if (ep)
			fusb300_set_epnstall(fusb300, ep);
		else
			fusb300_set_cxstall(fusb300);
		fusb300_set_cxdone(fusb300);
		}
		break;
	default:
		request_error(fusb300);
		break;
	}
}

static void fusb300_clear_seqnum(struct fusb300 *fusb300, u8 ep)
{
	fusb300_enable_bit(fusb300, FUSB300_OFFSET_EPSET0(ep),
			    FUSB300_EPSET0_CLRSEQNUM);
}

static void clear_feature(struct fusb300 *fusb300, struct usb_ctrlrequest *ctrl)
{
	struct fusb300_ep *ep =
		fusb300->ep[ctrl->wIndex & USB_ENDPOINT_NUMBER_MASK];

	switch (ctrl->bRequestType & USB_RECIP_MASK) {
	case USB_RECIP_DEVICE:
		fusb300_set_cxdone(fusb300);
		break;
	case USB_RECIP_INTERFACE:
		fusb300_set_cxdone(fusb300);
		break;
	case USB_RECIP_ENDPOINT:
		if (ctrl->wIndex & USB_ENDPOINT_NUMBER_MASK) {
			if (ep->wedged) {
				fusb300_set_cxdone(fusb300);
				break;
			}
			if (ep->stall) {
				ep->stall = 0;
				fusb300_clear_seqnum(fusb300, ep->epnum);
				fusb300_clear_epnstall(fusb300, ep->epnum);
				if (!list_empty(&ep->queue))
					enable_fifo_int(ep);
			}
		}
		fusb300_set_cxdone(fusb300);
		break;
	default:
		request_error(fusb300);
		break;
	}
}

static void fusb300_set_dev_addr(struct fusb300 *fusb300, u16 addr)
{
	u32 reg = ioread32(fusb300->reg + FUSB300_OFFSET_DAR);

	reg &= ~FUSB300_DAR_DRVADDR_MSK;
	reg |= FUSB300_DAR_DRVADDR(addr);

	iowrite32(reg, fusb300->reg + FUSB300_OFFSET_DAR);
}

static void set_address(struct fusb300 *fusb300, struct usb_ctrlrequest *ctrl)
{
	if (ctrl->wValue >= 0x0100)
		request_error(fusb300);
	else {
		fusb300_set_dev_addr(fusb300, ctrl->wValue);
		fusb300_set_cxdone(fusb300);
	}
}

#define UVC_COPY_DESCRIPTORS(mem, src) \
	do { \
		const struct usb_descriptor_header * const *__src; \
		for (__src = src; *__src; ++__src) { \
			memcpy(mem, *__src, (*__src)->bLength); \
			mem += (*__src)->bLength; \
		} \
	} while (0)

static int setup_packet(struct fusb300 *fusb300, struct usb_ctrlrequest *ctrl)
{
	u8 *p = (u8 *)ctrl;
	u8 ret = 0;
	u8 i = 0;

	fusb300_rdcxf(fusb300, p, 8);
	fusb300->ep0_dir = ctrl->bRequestType & USB_DIR_IN;
	fusb300->ep0_length = ctrl->wLength;

	/* check request */
	if ((ctrl->bRequestType & USB_TYPE_MASK) == USB_TYPE_STANDARD) {
		switch (ctrl->bRequest) {
		case USB_REQ_GET_STATUS:
			get_status(fusb300, ctrl);
			break;
		case USB_REQ_CLEAR_FEATURE:
			clear_feature(fusb300, ctrl);
			break;
		case USB_REQ_SET_FEATURE:
			set_feature(fusb300, ctrl);
			break;
		case USB_REQ_SET_ADDRESS:
			set_address(fusb300, ctrl);
			break;
		case USB_REQ_SET_CONFIGURATION:
			fusb300_enable_bit(fusb300, FUSB300_OFFSET_DAR,
					   FUSB300_DAR_SETCONFG);
			/* clear sequence number */
			for (i = 1; i <= FUSB300_MAX_NUM_EP; i++)
				fusb300_clear_seqnum(fusb300, i);
			fusb300->reenum = 1;
			ret = 1;
			break;
		default:
			ret = 1;
			break;
		}
	} else
		ret = 1;

	return ret;
}

static void done(struct fusb300_ep *ep, struct fusb300_request *req,
		 int status)
{
	list_del_init(&req->queue);

	/* don't modify queue heads during completion callback */
	if (ep->fusb300->gadget.speed == USB_SPEED_UNKNOWN)
		req->req.status = -ESHUTDOWN;
	else
		req->req.status = status;

	spin_unlock(&ep->fusb300->lock);
	usb_gadget_giveback_request(&ep->ep, &req->req);
	spin_lock(&ep->fusb300->lock);

	if (ep->epnum) {
		disable_fifo_int(ep);
		if (!list_empty(&ep->queue))
			enable_fifo_int(ep);
	} else
		fusb300_set_cxdone(ep->fusb300);
}

static void fusb300_fill_idma_prdtbl(struct fusb300_ep *ep, dma_addr_t d,
		u32 len)
{
	u32 value;
	u32 reg;

	/* wait SW owner */
	do {
		reg = ioread32(ep->fusb300->reg +
			FUSB300_OFFSET_EPPRD_W0(ep->epnum));
		reg &= FUSB300_EPPRD0_H;
	} while (reg);

	iowrite32(d, ep->fusb300->reg + FUSB300_OFFSET_EPPRD_W1(ep->epnum));

	value = FUSB300_EPPRD0_BTC(len) | FUSB300_EPPRD0_H |
		FUSB300_EPPRD0_F | FUSB300_EPPRD0_L | FUSB300_EPPRD0_I;
	iowrite32(value, ep->fusb300->reg + FUSB300_OFFSET_EPPRD_W0(ep->epnum));

	iowrite32(0x0, ep->fusb300->reg + FUSB300_OFFSET_EPPRD_W2(ep->epnum));

	fusb300_enable_bit(ep->fusb300, FUSB300_OFFSET_EPPRDRDY,
		FUSB300_EPPRDR_EP_PRD_RDY(ep->epnum));
}

static void fusb300_wait_idma_finished(struct fusb300_ep *ep)
{
	u32 reg;

	do {
		reg = ioread32(ep->fusb300->reg + FUSB300_OFFSET_IGR1);
		if ((reg & FUSB300_IGR1_VBUS_CHG_INT) ||
		    (reg & FUSB300_IGR1_WARM_RST_INT) ||
		    (reg & FUSB300_IGR1_HOT_RST_INT) ||
		    (reg & FUSB300_IGR1_USBRST_INT)
		)
			goto IDMA_RESET;
		reg = ioread32(ep->fusb300->reg + FUSB300_OFFSET_IGR0);
		reg &= FUSB300_IGR0_EPn_PRD_INT(ep->epnum);
	} while (!reg);

	fusb300_clear_int(ep->fusb300, FUSB300_OFFSET_IGR0,
		FUSB300_IGR0_EPn_PRD_INT(ep->epnum));
	return;

IDMA_RESET:
	reg = ioread32(ep->fusb300->reg + FUSB300_OFFSET_IGER0);
	reg &= ~FUSB300_IGER0_EEPn_PRD_INT(ep->epnum);
	iowrite32(reg, ep->fusb300->reg + FUSB300_OFFSET_IGER0);
}

static void fusb300_set_idma(struct fusb300_ep *ep,
			struct fusb300_request *req)
{
	int ret;

	ret = usb_gadget_map_request(&ep->fusb300->gadget,
			&req->req, DMA_TO_DEVICE);
	if (ret)
		return;

	fusb300_enable_bit(ep->fusb300, FUSB300_OFFSET_IGER0,
		FUSB300_IGER0_EEPn_PRD_INT(ep->epnum));

	fusb300_fill_idma_prdtbl(ep, req->req.dma, req->req.length);
	/* check idma is done */
	fusb300_wait_idma_finished(ep);

	usb_gadget_unmap_request(&ep->fusb300->gadget,
			&req->req, DMA_TO_DEVICE);
}

static void in_ep_fifo_handler(struct fusb300_ep *ep)
{
	struct fusb300_request *req = list_entry(ep->queue.next,
					struct fusb300_request, queue);

	if (req->req.length)
		fusb300_set_idma(ep, req);
	done(ep, req, 0);
}

static void out_ep_fifo_handler(struct fusb300_ep *ep)
{
	struct fusb300 *fusb300 = ep->fusb300;
	struct fusb300_request *req = list_entry(ep->queue.next,
						 struct fusb300_request, queue);
	u32 reg = ioread32(fusb300->reg + FUSB300_OFFSET_EPFFR(ep->epnum));
	u32 length = reg & FUSB300_FFR_BYCNT;

	fusb300_rdfifo(ep, req, length);

	/* finish out transfer */
	if ((req->req.length == req->req.actual) || (length < ep->ep.maxpacket))
		done(ep, req, 0);
}

static void check_device_mode(struct fusb300 *fusb300)
{
	u32 reg = ioread32(fusb300->reg + FUSB300_OFFSET_GCR);

	switch (reg & FUSB300_GCR_DEVEN_MSK) {
	case FUSB300_GCR_DEVEN_SS:
		fusb300->gadget.speed = USB_SPEED_SUPER;
		break;
	case FUSB300_GCR_DEVEN_HS:
		fusb300->gadget.speed = USB_SPEED_HIGH;
		break;
	case FUSB300_GCR_DEVEN_FS:
		fusb300->gadget.speed = USB_SPEED_FULL;
		break;
	default:
		fusb300->gadget.speed = USB_SPEED_UNKNOWN;
		break;
	}
	printk(KERN_INFO "dev_mode = %d\n", (reg & FUSB300_GCR_DEVEN_MSK));
}


static void fusb300_ep0out(struct fusb300 *fusb300)
{
	struct fusb300_ep *ep = fusb300->ep[0];
	u32 reg;

	if (!list_empty(&ep->queue)) {
		struct fusb300_request *req;

		req = list_first_entry(&ep->queue,
			struct fusb300_request, queue);
		if (req->req.length)
			fusb300_rdcxf(ep->fusb300, req->req.buf,
				req->req.length);
		done(ep, req, 0);
		reg = ioread32(fusb300->reg + FUSB300_OFFSET_IGER1);
		reg &= ~FUSB300_IGER1_CX_OUT_INT;
		iowrite32(reg, fusb300->reg + FUSB300_OFFSET_IGER1);
	} else
		pr_err("%s : empty queue\n", __func__);
}

static void fusb300_ep0in(struct fusb300 *fusb300)
{
	struct fusb300_request *req;
	struct fusb300_ep *ep = fusb300->ep[0];

	if ((!list_empty(&ep->queue)) && (fusb300->ep0_dir)) {
		req = list_entry(ep->queue.next,
				struct fusb300_request, queue);
		if (req->req.length)
			fusb300_wrcxf(ep, req);
		if ((req->req.length - req->req.actual) < ep->ep.maxpacket)
			done(ep, req, 0);
	} else
		fusb300_set_cxdone(fusb300);
}

static void fusb300_grp2_handler(void)
{
}

static void fusb300_grp3_handler(void)
{
}

static void fusb300_grp4_handler(void)
{
}

static void fusb300_grp5_handler(void)
{
}

static irqreturn_t fusb300_irq(int irq, void *_fusb300)
{
	struct fusb300 *fusb300 = _fusb300;
	u32 int_grp1 = ioread32(fusb300->reg + FUSB300_OFFSET_IGR1);
	u32 int_grp1_en = ioread32(fusb300->reg + FUSB300_OFFSET_IGER1);
	u32 int_grp0 = ioread32(fusb300->reg + FUSB300_OFFSET_IGR0);
	u32 int_grp0_en = ioread32(fusb300->reg + FUSB300_OFFSET_IGER0);
	struct usb_ctrlrequest ctrl;
	u8 in;
	u32 reg;
	int i;

	spin_lock(&fusb300->lock);

	int_grp1 &= int_grp1_en;
	int_grp0 &= int_grp0_en;

	if (int_grp1 & FUSB300_IGR1_WARM_RST_INT) {
		fusb300_clear_int(fusb300, FUSB300_OFFSET_IGR1,
				  FUSB300_IGR1_WARM_RST_INT);
		printk(KERN_INFO"fusb300_warmreset\n");
		fusb300_reset();
	}

	if (int_grp1 & FUSB300_IGR1_HOT_RST_INT) {
		fusb300_clear_int(fusb300, FUSB300_OFFSET_IGR1,
				  FUSB300_IGR1_HOT_RST_INT);
		printk(KERN_INFO"fusb300_hotreset\n");
		fusb300_reset();
	}

	if (int_grp1 & FUSB300_IGR1_USBRST_INT) {
		fusb300_clear_int(fusb300, FUSB300_OFFSET_IGR1,
				  FUSB300_IGR1_USBRST_INT);
		fusb300_reset();
	}
	/* COMABT_INT has a highest priority */

	if (int_grp1 & FUSB300_IGR1_CX_COMABT_INT) {
		fusb300_clear_int(fusb300, FUSB300_OFFSET_IGR1,
				  FUSB300_IGR1_CX_COMABT_INT);
		printk(KERN_INFO"fusb300_ep0abt\n");
	}

	if (int_grp1 & FUSB300_IGR1_VBUS_CHG_INT) {
		fusb300_clear_int(fusb300, FUSB300_OFFSET_IGR1,
				  FUSB300_IGR1_VBUS_CHG_INT);
		printk(KERN_INFO"fusb300_vbus_change\n");
	}

	if (int_grp1 & FUSB300_IGR1_U3_EXIT_FAIL_INT) {
		fusb300_clear_int(fusb300, FUSB300_OFFSET_IGR1,
				  FUSB300_IGR1_U3_EXIT_FAIL_INT);
	}

	if (int_grp1 & FUSB300_IGR1_U2_EXIT_FAIL_INT) {
		fusb300_clear_int(fusb300, FUSB300_OFFSET_IGR1,
				  FUSB300_IGR1_U2_EXIT_FAIL_INT);
	}

	if (int_grp1 & FUSB300_IGR1_U1_EXIT_FAIL_INT) {
		fusb300_clear_int(fusb300, FUSB300_OFFSET_IGR1,
				  FUSB300_IGR1_U1_EXIT_FAIL_INT);
	}

	if (int_grp1 & FUSB300_IGR1_U2_ENTRY_FAIL_INT) {
		fusb300_clear_int(fusb300, FUSB300_OFFSET_IGR1,
				  FUSB300_IGR1_U2_ENTRY_FAIL_INT);
	}

	if (int_grp1 & FUSB300_IGR1_U1_ENTRY_FAIL_INT) {
		fusb300_clear_int(fusb300, FUSB300_OFFSET_IGR1,
				  FUSB300_IGR1_U1_ENTRY_FAIL_INT);
	}

	if (int_grp1 & FUSB300_IGR1_U3_EXIT_INT) {
		fusb300_clear_int(fusb300, FUSB300_OFFSET_IGR1,
				  FUSB300_IGR1_U3_EXIT_INT);
		printk(KERN_INFO "FUSB300_IGR1_U3_EXIT_INT\n");
	}

	if (int_grp1 & FUSB300_IGR1_U2_EXIT_INT) {
		fusb300_clear_int(fusb300, FUSB300_OFFSET_IGR1,
				  FUSB300_IGR1_U2_EXIT_INT);
		printk(KERN_INFO "FUSB300_IGR1_U2_EXIT_INT\n");
	}

	if (int_grp1 & FUSB300_IGR1_U1_EXIT_INT) {
		fusb300_clear_int(fusb300, FUSB300_OFFSET_IGR1,
				  FUSB300_IGR1_U1_EXIT_INT);
		printk(KERN_INFO "FUSB300_IGR1_U1_EXIT_INT\n");
	}

	if (int_grp1 & FUSB300_IGR1_U3_ENTRY_INT) {
		fusb300_clear_int(fusb300, FUSB300_OFFSET_IGR1,
				  FUSB300_IGR1_U3_ENTRY_INT);
		printk(KERN_INFO "FUSB300_IGR1_U3_ENTRY_INT\n");
		fusb300_enable_bit(fusb300, FUSB300_OFFSET_SSCR1,
				   FUSB300_SSCR1_GO_U3_DONE);
	}

	if (int_grp1 & FUSB300_IGR1_U2_ENTRY_INT) {
		fusb300_clear_int(fusb300, FUSB300_OFFSET_IGR1,
				  FUSB300_IGR1_U2_ENTRY_INT);
		printk(KERN_INFO "FUSB300_IGR1_U2_ENTRY_INT\n");
	}

	if (int_grp1 & FUSB300_IGR1_U1_ENTRY_INT) {
		fusb300_clear_int(fusb300, FUSB300_OFFSET_IGR1,
				  FUSB300_IGR1_U1_ENTRY_INT);
		printk(KERN_INFO "FUSB300_IGR1_U1_ENTRY_INT\n");
	}

	if (int_grp1 & FUSB300_IGR1_RESM_INT) {
		fusb300_clear_int(fusb300, FUSB300_OFFSET_IGR1,
				  FUSB300_IGR1_RESM_INT);
		printk(KERN_INFO "fusb300_resume\n");
	}

	if (int_grp1 & FUSB300_IGR1_SUSP_INT) {
		fusb300_clear_int(fusb300, FUSB300_OFFSET_IGR1,
				  FUSB300_IGR1_SUSP_INT);
		printk(KERN_INFO "fusb300_suspend\n");
	}

	if (int_grp1 & FUSB300_IGR1_HS_LPM_INT) {
		fusb300_clear_int(fusb300, FUSB300_OFFSET_IGR1,
				  FUSB300_IGR1_HS_LPM_INT);
		printk(KERN_INFO "fusb300_HS_LPM_INT\n");
	}

	if (int_grp1 & FUSB300_IGR1_DEV_MODE_CHG_INT) {
		fusb300_clear_int(fusb300, FUSB300_OFFSET_IGR1,
				  FUSB300_IGR1_DEV_MODE_CHG_INT);
		check_device_mode(fusb300);
	}

	if (int_grp1 & FUSB300_IGR1_CX_COMFAIL_INT) {
		fusb300_set_cxstall(fusb300);
		printk(KERN_INFO "fusb300_ep0fail\n");
	}

	if (int_grp1 & FUSB300_IGR1_CX_SETUP_INT) {
		printk(KERN_INFO "fusb300_ep0setup\n");
		if (setup_packet(fusb300, &ctrl)) {
			spin_unlock(&fusb300->lock);
			if (fusb300->driver->setup(&fusb300->gadget, &ctrl) < 0)
				fusb300_set_cxstall(fusb300);
			spin_lock(&fusb300->lock);
		}
	}

	if (int_grp1 & FUSB300_IGR1_CX_CMDEND_INT)
		printk(KERN_INFO "fusb300_cmdend\n");


	if (int_grp1 & FUSB300_IGR1_CX_OUT_INT) {
		printk(KERN_INFO "fusb300_cxout\n");
		fusb300_ep0out(fusb300);
	}

	if (int_grp1 & FUSB300_IGR1_CX_IN_INT) {
		printk(KERN_INFO "fusb300_cxin\n");
		fusb300_ep0in(fusb300);
	}

	if (int_grp1 & FUSB300_IGR1_INTGRP5)
		fusb300_grp5_handler();

	if (int_grp1 & FUSB300_IGR1_INTGRP4)
		fusb300_grp4_handler();

	if (int_grp1 & FUSB300_IGR1_INTGRP3)
		fusb300_grp3_handler();

	if (int_grp1 & FUSB300_IGR1_INTGRP2)
		fusb300_grp2_handler();

	if (int_grp0) {
		for (i = 1; i < FUSB300_MAX_NUM_EP; i++) {
			if (int_grp0 & FUSB300_IGR0_EPn_FIFO_INT(i)) {
				reg = ioread32(fusb300->reg +
					FUSB300_OFFSET_EPSET1(i));
				in = (reg & FUSB300_EPSET1_DIRIN) ? 1 : 0;
				if (in)
					in_ep_fifo_handler(fusb300->ep[i]);
				else
					out_ep_fifo_handler(fusb300->ep[i]);
			}
		}
	}

	spin_unlock(&fusb300->lock);

	return IRQ_HANDLED;
}

static void fusb300_set_u2_timeout(struct fusb300 *fusb300,
				   u32 time)
{
	u32 reg;

	reg = ioread32(fusb300->reg + FUSB300_OFFSET_TT);
	reg &= ~0xff;
	reg |= FUSB300_SSCR2_U2TIMEOUT(time);

	iowrite32(reg, fusb300->reg + FUSB300_OFFSET_TT);
}

static void fusb300_set_u1_timeout(struct fusb300 *fusb300,
				   u32 time)
{
	u32 reg;

	reg = ioread32(fusb300->reg + FUSB300_OFFSET_TT);
	reg &= ~(0xff << 8);
	reg |= FUSB300_SSCR2_U1TIMEOUT(time);

	iowrite32(reg, fusb300->reg + FUSB300_OFFSET_TT);
}

static void init_controller(struct fusb300 *fusb300)
{
	u32 reg;
	u32 mask = 0;
	u32 val = 0;

	/* split on */
	mask = val = FUSB300_AHBBCR_S0_SPLIT_ON | FUSB300_AHBBCR_S1_SPLIT_ON;
	reg = ioread32(fusb300->reg + FUSB300_OFFSET_AHBCR);
	reg &= ~mask;
	reg |= val;
	iowrite32(reg, fusb300->reg + FUSB300_OFFSET_AHBCR);

	/* enable high-speed LPM */
	mask = val = FUSB300_HSCR_HS_LPM_PERMIT;
	reg = ioread32(fusb300->reg + FUSB300_OFFSET_HSCR);
	reg &= ~mask;
	reg |= val;
	iowrite32(reg, fusb300->reg + FUSB300_OFFSET_HSCR);

	/*set u1 u2 timmer*/
	fusb300_set_u2_timeout(fusb300, 0xff);
	fusb300_set_u1_timeout(fusb300, 0xff);

	/* enable all grp1 interrupt */
	iowrite32(0xcfffff9f, fusb300->reg + FUSB300_OFFSET_IGER1);
}
/*------------------------------------------------------------------------*/
static int fusb300_udc_start(struct usb_gadget *g,
		struct usb_gadget_driver *driver)
{
	struct fusb300 *fusb300 = to_fusb300(g);

	/* hook up the driver */
	driver->driver.bus = NULL;
	fusb300->driver = driver;

	return 0;
}

static int fusb300_udc_stop(struct usb_gadget *g)
{
	struct fusb300 *fusb300 = to_fusb300(g);

	init_controller(fusb300);
	fusb300->driver = NULL;

	return 0;
}
/*--------------------------------------------------------------------------*/

static int fusb300_udc_pullup(struct usb_gadget *_gadget, int is_active)
{
	return 0;
}

static const struct usb_gadget_ops fusb300_gadget_ops = {
	.pullup		= fusb300_udc_pullup,
	.udc_start	= fusb300_udc_start,
	.udc_stop	= fusb300_udc_stop,
};

static int fusb300_remove(struct platform_device *pdev)
{
	struct fusb300 *fusb300 = platform_get_drvdata(pdev);

	usb_del_gadget_udc(&fusb300->gadget);
	iounmap(fusb300->reg);
	free_irq(platform_get_irq(pdev, 0), fusb300);

	fusb300_free_request(&fusb300->ep[0]->ep, fusb300->ep0_req);
	kfree(fusb300);

	return 0;
}

static int fusb300_probe(struct platform_device *pdev)
{
	struct resource *res, *ires, *ires1;
	void __iomem *reg = NULL;
	struct fusb300 *fusb300 = NULL;
	struct fusb300_ep *_ep[FUSB300_MAX_NUM_EP];
	int ret = 0;
	int i;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		ret = -ENODEV;
		pr_err("platform_get_resource error.\n");
		goto clean_up;
	}

	ires = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!ires) {
		ret = -ENODEV;
		dev_err(&pdev->dev,
			"platform_get_resource IORESOURCE_IRQ error.\n");
		goto clean_up;
	}

	ires1 = platform_get_resource(pdev, IORESOURCE_IRQ, 1);
	if (!ires1) {
		ret = -ENODEV;
		dev_err(&pdev->dev,
			"platform_get_resource IORESOURCE_IRQ 1 error.\n");
		goto clean_up;
	}

	reg = ioremap(res->start, resource_size(res));
	if (reg == NULL) {
		ret = -ENOMEM;
		pr_err("ioremap error.\n");
		goto clean_up;
	}

	/* initialize udc */
	fusb300 = kzalloc(sizeof(struct fusb300), GFP_KERNEL);
	if (fusb300 == NULL) {
		ret = -ENOMEM;
		goto clean_up;
	}

	for (i = 0; i < FUSB300_MAX_NUM_EP; i++) {
		_ep[i] = kzalloc(sizeof(struct fusb300_ep), GFP_KERNEL);
		if (_ep[i] == NULL) {
			ret = -ENOMEM;
			goto clean_up;
		}
		fusb300->ep[i] = _ep[i];
	}

	spin_lock_init(&fusb300->lock);

	platform_set_drvdata(pdev, fusb300);

	fusb300->gadget.ops = &fusb300_gadget_ops;

	fusb300->gadget.max_speed = USB_SPEED_HIGH;
	fusb300->gadget.name = udc_name;
	fusb300->reg = reg;

	ret = request_irq(ires->start, fusb300_irq, IRQF_SHARED,
			  udc_name, fusb300);
	if (ret < 0) {
		pr_err("request_irq error (%d)\n", ret);
		goto clean_up;
	}

	ret = request_irq(ires1->start, fusb300_irq,
			IRQF_SHARED, udc_name, fusb300);
	if (ret < 0) {
		pr_err("request_irq1 error (%d)\n", ret);
		goto clean_up;
	}

	INIT_LIST_HEAD(&fusb300->gadget.ep_list);

	for (i = 0; i < FUSB300_MAX_NUM_EP ; i++) {
		struct fusb300_ep *ep = fusb300->ep[i];

		if (i != 0) {
			INIT_LIST_HEAD(&fusb300->ep[i]->ep.ep_list);
			list_add_tail(&fusb300->ep[i]->ep.ep_list,
				     &fusb300->gadget.ep_list);
		}
		ep->fusb300 = fusb300;
		INIT_LIST_HEAD(&ep->queue);
		ep->ep.name = fusb300_ep_name[i];
		ep->ep.ops = &fusb300_ep_ops;
		usb_ep_set_maxpacket_limit(&ep->ep, HS_BULK_MAX_PACKET_SIZE);

		if (i == 0) {
			ep->ep.caps.type_control = true;
		} else {
			ep->ep.caps.type_iso = true;
			ep->ep.caps.type_bulk = true;
			ep->ep.caps.type_int = true;
		}

		ep->ep.caps.dir_in = true;
		ep->ep.caps.dir_out = true;
	}
	usb_ep_set_maxpacket_limit(&fusb300->ep[0]->ep, HS_CTL_MAX_PACKET_SIZE);
	fusb300->ep[0]->epnum = 0;
	fusb300->gadget.ep0 = &fusb300->ep[0]->ep;
	INIT_LIST_HEAD(&fusb300->gadget.ep0->ep_list);

	fusb300->ep0_req = fusb300_alloc_request(&fusb300->ep[0]->ep,
				GFP_KERNEL);
	if (fusb300->ep0_req == NULL) {
		ret = -ENOMEM;
		goto clean_up3;
	}

	init_controller(fusb300);
	ret = usb_add_gadget_udc(&pdev->dev, &fusb300->gadget);
	if (ret)
		goto err_add_udc;

	dev_info(&pdev->dev, "version %s\n", DRIVER_VERSION);

	return 0;

err_add_udc:
	fusb300_free_request(&fusb300->ep[0]->ep, fusb300->ep0_req);

clean_up3:
	free_irq(ires->start, fusb300);

clean_up:
	if (fusb300) {
		if (fusb300->ep0_req)
			fusb300_free_request(&fusb300->ep[0]->ep,
				fusb300->ep0_req);
		kfree(fusb300);
	}
	if (reg)
		iounmap(reg);

	return ret;
}

static struct platform_driver fusb300_driver = {
	.remove =	fusb300_remove,
	.driver		= {
		.name =	(char *) udc_name,
	},
};

module_platform_driver_probe(fusb300_driver, fusb300_probe);
