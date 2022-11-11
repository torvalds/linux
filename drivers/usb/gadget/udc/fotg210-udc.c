// SPDX-License-Identifier: GPL-2.0
/*
 * FOTG210 UDC Driver supports Bulk transfer so far
 *
 * Copyright (C) 2013 Faraday Technology Corporation
 *
 * Author : Yuan-Hsin Chen <yhchen@faraday-tech.com>
 */

#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>

#include "fotg210.h"

#define	DRIVER_DESC	"FOTG210 USB Device Controller Driver"
#define	DRIVER_VERSION	"30-April-2013"

static const char udc_name[] = "fotg210_udc";
static const char * const fotg210_ep_name[] = {
	"ep0", "ep1", "ep2", "ep3", "ep4"};

static void fotg210_disable_fifo_int(struct fotg210_ep *ep)
{
	u32 value = ioread32(ep->fotg210->reg + FOTG210_DMISGR1);

	if (ep->dir_in)
		value |= DMISGR1_MF_IN_INT(ep->epnum - 1);
	else
		value |= DMISGR1_MF_OUTSPK_INT(ep->epnum - 1);
	iowrite32(value, ep->fotg210->reg + FOTG210_DMISGR1);
}

static void fotg210_enable_fifo_int(struct fotg210_ep *ep)
{
	u32 value = ioread32(ep->fotg210->reg + FOTG210_DMISGR1);

	if (ep->dir_in)
		value &= ~DMISGR1_MF_IN_INT(ep->epnum - 1);
	else
		value &= ~DMISGR1_MF_OUTSPK_INT(ep->epnum - 1);
	iowrite32(value, ep->fotg210->reg + FOTG210_DMISGR1);
}

static void fotg210_set_cxdone(struct fotg210_udc *fotg210)
{
	u32 value = ioread32(fotg210->reg + FOTG210_DCFESR);

	value |= DCFESR_CX_DONE;
	iowrite32(value, fotg210->reg + FOTG210_DCFESR);
}

static void fotg210_done(struct fotg210_ep *ep, struct fotg210_request *req,
			int status)
{
	list_del_init(&req->queue);

	/* don't modify queue heads during completion callback */
	if (ep->fotg210->gadget.speed == USB_SPEED_UNKNOWN)
		req->req.status = -ESHUTDOWN;
	else
		req->req.status = status;

	spin_unlock(&ep->fotg210->lock);
	usb_gadget_giveback_request(&ep->ep, &req->req);
	spin_lock(&ep->fotg210->lock);

	if (ep->epnum) {
		if (list_empty(&ep->queue))
			fotg210_disable_fifo_int(ep);
	} else {
		fotg210_set_cxdone(ep->fotg210);
	}
}

static void fotg210_fifo_ep_mapping(struct fotg210_ep *ep, u32 epnum,
				u32 dir_in)
{
	struct fotg210_udc *fotg210 = ep->fotg210;
	u32 val;

	/* Driver should map an ep to a fifo and then map the fifo
	 * to the ep. What a brain-damaged design!
	 */

	/* map a fifo to an ep */
	val = ioread32(fotg210->reg + FOTG210_EPMAP);
	val &= ~EPMAP_FIFONOMSK(epnum, dir_in);
	val |= EPMAP_FIFONO(epnum, dir_in);
	iowrite32(val, fotg210->reg + FOTG210_EPMAP);

	/* map the ep to the fifo */
	val = ioread32(fotg210->reg + FOTG210_FIFOMAP);
	val &= ~FIFOMAP_EPNOMSK(epnum);
	val |= FIFOMAP_EPNO(epnum);
	iowrite32(val, fotg210->reg + FOTG210_FIFOMAP);

	/* enable fifo */
	val = ioread32(fotg210->reg + FOTG210_FIFOCF);
	val |= FIFOCF_FIFO_EN(epnum - 1);
	iowrite32(val, fotg210->reg + FOTG210_FIFOCF);
}

static void fotg210_set_fifo_dir(struct fotg210_ep *ep, u32 epnum, u32 dir_in)
{
	struct fotg210_udc *fotg210 = ep->fotg210;
	u32 val;

	val = ioread32(fotg210->reg + FOTG210_FIFOMAP);
	val |= (dir_in ? FIFOMAP_DIRIN(epnum - 1) : FIFOMAP_DIROUT(epnum - 1));
	iowrite32(val, fotg210->reg + FOTG210_FIFOMAP);
}

static void fotg210_set_tfrtype(struct fotg210_ep *ep, u32 epnum, u32 type)
{
	struct fotg210_udc *fotg210 = ep->fotg210;
	u32 val;

	val = ioread32(fotg210->reg + FOTG210_FIFOCF);
	val |= FIFOCF_TYPE(type, epnum - 1);
	iowrite32(val, fotg210->reg + FOTG210_FIFOCF);
}

static void fotg210_set_mps(struct fotg210_ep *ep, u32 epnum, u32 mps,
				u32 dir_in)
{
	struct fotg210_udc *fotg210 = ep->fotg210;
	u32 val;
	u32 offset = dir_in ? FOTG210_INEPMPSR(epnum) :
				FOTG210_OUTEPMPSR(epnum);

	val = ioread32(fotg210->reg + offset);
	val |= INOUTEPMPSR_MPS(mps);
	iowrite32(val, fotg210->reg + offset);
}

static int fotg210_config_ep(struct fotg210_ep *ep,
		     const struct usb_endpoint_descriptor *desc)
{
	struct fotg210_udc *fotg210 = ep->fotg210;

	fotg210_set_fifo_dir(ep, ep->epnum, ep->dir_in);
	fotg210_set_tfrtype(ep, ep->epnum, ep->type);
	fotg210_set_mps(ep, ep->epnum, ep->ep.maxpacket, ep->dir_in);
	fotg210_fifo_ep_mapping(ep, ep->epnum, ep->dir_in);

	fotg210->ep[ep->epnum] = ep;

	return 0;
}

static int fotg210_ep_enable(struct usb_ep *_ep,
			  const struct usb_endpoint_descriptor *desc)
{
	struct fotg210_ep *ep;

	ep = container_of(_ep, struct fotg210_ep, ep);

	ep->desc = desc;
	ep->epnum = usb_endpoint_num(desc);
	ep->type = usb_endpoint_type(desc);
	ep->dir_in = usb_endpoint_dir_in(desc);
	ep->ep.maxpacket = usb_endpoint_maxp(desc);

	return fotg210_config_ep(ep, desc);
}

static void fotg210_reset_tseq(struct fotg210_udc *fotg210, u8 epnum)
{
	struct fotg210_ep *ep = fotg210->ep[epnum];
	u32 value;
	void __iomem *reg;

	reg = (ep->dir_in) ?
		fotg210->reg + FOTG210_INEPMPSR(epnum) :
		fotg210->reg + FOTG210_OUTEPMPSR(epnum);

	/* Note: Driver needs to set and clear INOUTEPMPSR_RESET_TSEQ
	 *	 bit. Controller wouldn't clear this bit. WTF!!!
	 */

	value = ioread32(reg);
	value |= INOUTEPMPSR_RESET_TSEQ;
	iowrite32(value, reg);

	value = ioread32(reg);
	value &= ~INOUTEPMPSR_RESET_TSEQ;
	iowrite32(value, reg);
}

static int fotg210_ep_release(struct fotg210_ep *ep)
{
	if (!ep->epnum)
		return 0;
	ep->epnum = 0;
	ep->stall = 0;
	ep->wedged = 0;

	fotg210_reset_tseq(ep->fotg210, ep->epnum);

	return 0;
}

static int fotg210_ep_disable(struct usb_ep *_ep)
{
	struct fotg210_ep *ep;
	struct fotg210_request *req;
	unsigned long flags;

	BUG_ON(!_ep);

	ep = container_of(_ep, struct fotg210_ep, ep);

	while (!list_empty(&ep->queue)) {
		req = list_entry(ep->queue.next,
			struct fotg210_request, queue);
		spin_lock_irqsave(&ep->fotg210->lock, flags);
		fotg210_done(ep, req, -ECONNRESET);
		spin_unlock_irqrestore(&ep->fotg210->lock, flags);
	}

	return fotg210_ep_release(ep);
}

static struct usb_request *fotg210_ep_alloc_request(struct usb_ep *_ep,
						gfp_t gfp_flags)
{
	struct fotg210_request *req;

	req = kzalloc(sizeof(struct fotg210_request), gfp_flags);
	if (!req)
		return NULL;

	INIT_LIST_HEAD(&req->queue);

	return &req->req;
}

static void fotg210_ep_free_request(struct usb_ep *_ep,
					struct usb_request *_req)
{
	struct fotg210_request *req;

	req = container_of(_req, struct fotg210_request, req);
	kfree(req);
}

static void fotg210_enable_dma(struct fotg210_ep *ep,
			      dma_addr_t d, u32 len)
{
	u32 value;
	struct fotg210_udc *fotg210 = ep->fotg210;

	/* set transfer length and direction */
	value = ioread32(fotg210->reg + FOTG210_DMACPSR1);
	value &= ~(DMACPSR1_DMA_LEN(0xFFFF) | DMACPSR1_DMA_TYPE(1));
	value |= DMACPSR1_DMA_LEN(len) | DMACPSR1_DMA_TYPE(ep->dir_in);
	iowrite32(value, fotg210->reg + FOTG210_DMACPSR1);

	/* set device DMA target FIFO number */
	value = ioread32(fotg210->reg + FOTG210_DMATFNR);
	if (ep->epnum)
		value |= DMATFNR_ACC_FN(ep->epnum - 1);
	else
		value |= DMATFNR_ACC_CXF;
	iowrite32(value, fotg210->reg + FOTG210_DMATFNR);

	/* set DMA memory address */
	iowrite32(d, fotg210->reg + FOTG210_DMACPSR2);

	/* enable MDMA_EROR and MDMA_CMPLT interrupt */
	value = ioread32(fotg210->reg + FOTG210_DMISGR2);
	value &= ~(DMISGR2_MDMA_CMPLT | DMISGR2_MDMA_ERROR);
	iowrite32(value, fotg210->reg + FOTG210_DMISGR2);

	/* start DMA */
	value = ioread32(fotg210->reg + FOTG210_DMACPSR1);
	value |= DMACPSR1_DMA_START;
	iowrite32(value, fotg210->reg + FOTG210_DMACPSR1);
}

static void fotg210_disable_dma(struct fotg210_ep *ep)
{
	iowrite32(DMATFNR_DISDMA, ep->fotg210->reg + FOTG210_DMATFNR);
}

static void fotg210_wait_dma_done(struct fotg210_ep *ep)
{
	u32 value;

	do {
		value = ioread32(ep->fotg210->reg + FOTG210_DISGR2);
		if ((value & DISGR2_USBRST_INT) ||
		    (value & DISGR2_DMA_ERROR))
			goto dma_reset;
	} while (!(value & DISGR2_DMA_CMPLT));

	value &= ~DISGR2_DMA_CMPLT;
	iowrite32(value, ep->fotg210->reg + FOTG210_DISGR2);
	return;

dma_reset:
	value = ioread32(ep->fotg210->reg + FOTG210_DMACPSR1);
	value |= DMACPSR1_DMA_ABORT;
	iowrite32(value, ep->fotg210->reg + FOTG210_DMACPSR1);

	/* reset fifo */
	if (ep->epnum) {
		value = ioread32(ep->fotg210->reg +
				FOTG210_FIBCR(ep->epnum - 1));
		value |= FIBCR_FFRST;
		iowrite32(value, ep->fotg210->reg +
				FOTG210_FIBCR(ep->epnum - 1));
	} else {
		value = ioread32(ep->fotg210->reg + FOTG210_DCFESR);
		value |= DCFESR_CX_CLR;
		iowrite32(value, ep->fotg210->reg + FOTG210_DCFESR);
	}
}

static void fotg210_start_dma(struct fotg210_ep *ep,
			struct fotg210_request *req)
{
	struct device *dev = &ep->fotg210->gadget.dev;
	dma_addr_t d;
	u8 *buffer;
	u32 length;

	if (ep->epnum) {
		if (ep->dir_in) {
			buffer = req->req.buf;
			length = req->req.length;
		} else {
			buffer = req->req.buf + req->req.actual;
			length = ioread32(ep->fotg210->reg +
					FOTG210_FIBCR(ep->epnum - 1)) & FIBCR_BCFX;
			if (length > req->req.length - req->req.actual)
				length = req->req.length - req->req.actual;
		}
	} else {
		buffer = req->req.buf + req->req.actual;
		if (req->req.length - req->req.actual > ep->ep.maxpacket)
			length = ep->ep.maxpacket;
		else
			length = req->req.length - req->req.actual;
	}

	d = dma_map_single(dev, buffer, length,
			ep->dir_in ? DMA_TO_DEVICE : DMA_FROM_DEVICE);

	if (dma_mapping_error(dev, d)) {
		pr_err("dma_mapping_error\n");
		return;
	}

	fotg210_enable_dma(ep, d, length);

	/* check if dma is done */
	fotg210_wait_dma_done(ep);

	fotg210_disable_dma(ep);

	/* update actual transfer length */
	req->req.actual += length;

	dma_unmap_single(dev, d, length, DMA_TO_DEVICE);
}

static void fotg210_ep0_queue(struct fotg210_ep *ep,
				struct fotg210_request *req)
{
	if (!req->req.length) {
		fotg210_done(ep, req, 0);
		return;
	}
	if (ep->dir_in) { /* if IN */
		fotg210_start_dma(ep, req);
		if (req->req.length == req->req.actual)
			fotg210_done(ep, req, 0);
	} else { /* OUT */
		u32 value = ioread32(ep->fotg210->reg + FOTG210_DMISGR0);

		value &= ~DMISGR0_MCX_OUT_INT;
		iowrite32(value, ep->fotg210->reg + FOTG210_DMISGR0);
	}
}

static int fotg210_ep_queue(struct usb_ep *_ep, struct usb_request *_req,
				gfp_t gfp_flags)
{
	struct fotg210_ep *ep;
	struct fotg210_request *req;
	unsigned long flags;
	int request = 0;

	ep = container_of(_ep, struct fotg210_ep, ep);
	req = container_of(_req, struct fotg210_request, req);

	if (ep->fotg210->gadget.speed == USB_SPEED_UNKNOWN)
		return -ESHUTDOWN;

	spin_lock_irqsave(&ep->fotg210->lock, flags);

	if (list_empty(&ep->queue))
		request = 1;

	list_add_tail(&req->queue, &ep->queue);

	req->req.actual = 0;
	req->req.status = -EINPROGRESS;

	if (!ep->epnum) /* ep0 */
		fotg210_ep0_queue(ep, req);
	else if (request && !ep->stall)
		fotg210_enable_fifo_int(ep);

	spin_unlock_irqrestore(&ep->fotg210->lock, flags);

	return 0;
}

static int fotg210_ep_dequeue(struct usb_ep *_ep, struct usb_request *_req)
{
	struct fotg210_ep *ep;
	struct fotg210_request *req;
	unsigned long flags;

	ep = container_of(_ep, struct fotg210_ep, ep);
	req = container_of(_req, struct fotg210_request, req);

	spin_lock_irqsave(&ep->fotg210->lock, flags);
	if (!list_empty(&ep->queue))
		fotg210_done(ep, req, -ECONNRESET);
	spin_unlock_irqrestore(&ep->fotg210->lock, flags);

	return 0;
}

static void fotg210_set_epnstall(struct fotg210_ep *ep)
{
	struct fotg210_udc *fotg210 = ep->fotg210;
	u32 value;
	void __iomem *reg;

	/* check if IN FIFO is empty before stall */
	if (ep->dir_in) {
		do {
			value = ioread32(fotg210->reg + FOTG210_DCFESR);
		} while (!(value & DCFESR_FIFO_EMPTY(ep->epnum - 1)));
	}

	reg = (ep->dir_in) ?
		fotg210->reg + FOTG210_INEPMPSR(ep->epnum) :
		fotg210->reg + FOTG210_OUTEPMPSR(ep->epnum);
	value = ioread32(reg);
	value |= INOUTEPMPSR_STL_EP;
	iowrite32(value, reg);
}

static void fotg210_clear_epnstall(struct fotg210_ep *ep)
{
	struct fotg210_udc *fotg210 = ep->fotg210;
	u32 value;
	void __iomem *reg;

	reg = (ep->dir_in) ?
		fotg210->reg + FOTG210_INEPMPSR(ep->epnum) :
		fotg210->reg + FOTG210_OUTEPMPSR(ep->epnum);
	value = ioread32(reg);
	value &= ~INOUTEPMPSR_STL_EP;
	iowrite32(value, reg);
}

static int fotg210_set_halt_and_wedge(struct usb_ep *_ep, int value, int wedge)
{
	struct fotg210_ep *ep;
	struct fotg210_udc *fotg210;
	unsigned long flags;

	ep = container_of(_ep, struct fotg210_ep, ep);

	fotg210 = ep->fotg210;

	spin_lock_irqsave(&ep->fotg210->lock, flags);

	if (value) {
		fotg210_set_epnstall(ep);
		ep->stall = 1;
		if (wedge)
			ep->wedged = 1;
	} else {
		fotg210_reset_tseq(fotg210, ep->epnum);
		fotg210_clear_epnstall(ep);
		ep->stall = 0;
		ep->wedged = 0;
		if (!list_empty(&ep->queue))
			fotg210_enable_fifo_int(ep);
	}

	spin_unlock_irqrestore(&ep->fotg210->lock, flags);
	return 0;
}

static int fotg210_ep_set_halt(struct usb_ep *_ep, int value)
{
	return fotg210_set_halt_and_wedge(_ep, value, 0);
}

static int fotg210_ep_set_wedge(struct usb_ep *_ep)
{
	return fotg210_set_halt_and_wedge(_ep, 1, 1);
}

static void fotg210_ep_fifo_flush(struct usb_ep *_ep)
{
}

static const struct usb_ep_ops fotg210_ep_ops = {
	.enable		= fotg210_ep_enable,
	.disable	= fotg210_ep_disable,

	.alloc_request	= fotg210_ep_alloc_request,
	.free_request	= fotg210_ep_free_request,

	.queue		= fotg210_ep_queue,
	.dequeue	= fotg210_ep_dequeue,

	.set_halt	= fotg210_ep_set_halt,
	.fifo_flush	= fotg210_ep_fifo_flush,
	.set_wedge	= fotg210_ep_set_wedge,
};

static void fotg210_clear_tx0byte(struct fotg210_udc *fotg210)
{
	u32 value = ioread32(fotg210->reg + FOTG210_TX0BYTE);

	value &= ~(TX0BYTE_EP1 | TX0BYTE_EP2 | TX0BYTE_EP3
		   | TX0BYTE_EP4);
	iowrite32(value, fotg210->reg + FOTG210_TX0BYTE);
}

static void fotg210_clear_rx0byte(struct fotg210_udc *fotg210)
{
	u32 value = ioread32(fotg210->reg + FOTG210_RX0BYTE);

	value &= ~(RX0BYTE_EP1 | RX0BYTE_EP2 | RX0BYTE_EP3
		   | RX0BYTE_EP4);
	iowrite32(value, fotg210->reg + FOTG210_RX0BYTE);
}

/* read 8-byte setup packet only */
static void fotg210_rdsetupp(struct fotg210_udc *fotg210,
		   u8 *buffer)
{
	int i = 0;
	u8 *tmp = buffer;
	u32 data;
	u32 length = 8;

	iowrite32(DMATFNR_ACC_CXF, fotg210->reg + FOTG210_DMATFNR);

	for (i = (length >> 2); i > 0; i--) {
		data = ioread32(fotg210->reg + FOTG210_CXPORT);
		*tmp = data & 0xFF;
		*(tmp + 1) = (data >> 8) & 0xFF;
		*(tmp + 2) = (data >> 16) & 0xFF;
		*(tmp + 3) = (data >> 24) & 0xFF;
		tmp = tmp + 4;
	}

	switch (length % 4) {
	case 1:
		data = ioread32(fotg210->reg + FOTG210_CXPORT);
		*tmp = data & 0xFF;
		break;
	case 2:
		data = ioread32(fotg210->reg + FOTG210_CXPORT);
		*tmp = data & 0xFF;
		*(tmp + 1) = (data >> 8) & 0xFF;
		break;
	case 3:
		data = ioread32(fotg210->reg + FOTG210_CXPORT);
		*tmp = data & 0xFF;
		*(tmp + 1) = (data >> 8) & 0xFF;
		*(tmp + 2) = (data >> 16) & 0xFF;
		break;
	default:
		break;
	}

	iowrite32(DMATFNR_DISDMA, fotg210->reg + FOTG210_DMATFNR);
}

static void fotg210_set_configuration(struct fotg210_udc *fotg210)
{
	u32 value = ioread32(fotg210->reg + FOTG210_DAR);

	value |= DAR_AFT_CONF;
	iowrite32(value, fotg210->reg + FOTG210_DAR);
}

static void fotg210_set_dev_addr(struct fotg210_udc *fotg210, u32 addr)
{
	u32 value = ioread32(fotg210->reg + FOTG210_DAR);

	value |= (addr & 0x7F);
	iowrite32(value, fotg210->reg + FOTG210_DAR);
}

static void fotg210_set_cxstall(struct fotg210_udc *fotg210)
{
	u32 value = ioread32(fotg210->reg + FOTG210_DCFESR);

	value |= DCFESR_CX_STL;
	iowrite32(value, fotg210->reg + FOTG210_DCFESR);
}

static void fotg210_request_error(struct fotg210_udc *fotg210)
{
	fotg210_set_cxstall(fotg210);
	pr_err("request error!!\n");
}

static void fotg210_set_address(struct fotg210_udc *fotg210,
				struct usb_ctrlrequest *ctrl)
{
	if (le16_to_cpu(ctrl->wValue) >= 0x0100) {
		fotg210_request_error(fotg210);
	} else {
		fotg210_set_dev_addr(fotg210, le16_to_cpu(ctrl->wValue));
		fotg210_set_cxdone(fotg210);
	}
}

static void fotg210_set_feature(struct fotg210_udc *fotg210,
				struct usb_ctrlrequest *ctrl)
{
	switch (ctrl->bRequestType & USB_RECIP_MASK) {
	case USB_RECIP_DEVICE:
		fotg210_set_cxdone(fotg210);
		break;
	case USB_RECIP_INTERFACE:
		fotg210_set_cxdone(fotg210);
		break;
	case USB_RECIP_ENDPOINT: {
		u8 epnum;
		epnum = le16_to_cpu(ctrl->wIndex) & USB_ENDPOINT_NUMBER_MASK;
		if (epnum)
			fotg210_set_epnstall(fotg210->ep[epnum]);
		else
			fotg210_set_cxstall(fotg210);
		fotg210_set_cxdone(fotg210);
		}
		break;
	default:
		fotg210_request_error(fotg210);
		break;
	}
}

static void fotg210_clear_feature(struct fotg210_udc *fotg210,
				struct usb_ctrlrequest *ctrl)
{
	struct fotg210_ep *ep =
		fotg210->ep[ctrl->wIndex & USB_ENDPOINT_NUMBER_MASK];

	switch (ctrl->bRequestType & USB_RECIP_MASK) {
	case USB_RECIP_DEVICE:
		fotg210_set_cxdone(fotg210);
		break;
	case USB_RECIP_INTERFACE:
		fotg210_set_cxdone(fotg210);
		break;
	case USB_RECIP_ENDPOINT:
		if (ctrl->wIndex & USB_ENDPOINT_NUMBER_MASK) {
			if (ep->wedged) {
				fotg210_set_cxdone(fotg210);
				break;
			}
			if (ep->stall)
				fotg210_set_halt_and_wedge(&ep->ep, 0, 0);
		}
		fotg210_set_cxdone(fotg210);
		break;
	default:
		fotg210_request_error(fotg210);
		break;
	}
}

static int fotg210_is_epnstall(struct fotg210_ep *ep)
{
	struct fotg210_udc *fotg210 = ep->fotg210;
	u32 value;
	void __iomem *reg;

	reg = (ep->dir_in) ?
		fotg210->reg + FOTG210_INEPMPSR(ep->epnum) :
		fotg210->reg + FOTG210_OUTEPMPSR(ep->epnum);
	value = ioread32(reg);
	return value & INOUTEPMPSR_STL_EP ? 1 : 0;
}

static void fotg210_get_status(struct fotg210_udc *fotg210,
				struct usb_ctrlrequest *ctrl)
{
	u8 epnum;

	switch (ctrl->bRequestType & USB_RECIP_MASK) {
	case USB_RECIP_DEVICE:
		fotg210->ep0_data = cpu_to_le16(1 << USB_DEVICE_SELF_POWERED);
		break;
	case USB_RECIP_INTERFACE:
		fotg210->ep0_data = cpu_to_le16(0);
		break;
	case USB_RECIP_ENDPOINT:
		epnum = ctrl->wIndex & USB_ENDPOINT_NUMBER_MASK;
		if (epnum)
			fotg210->ep0_data =
				cpu_to_le16(fotg210_is_epnstall(fotg210->ep[epnum])
					    << USB_ENDPOINT_HALT);
		else
			fotg210_request_error(fotg210);
		break;

	default:
		fotg210_request_error(fotg210);
		return;		/* exit */
	}

	fotg210->ep0_req->buf = &fotg210->ep0_data;
	fotg210->ep0_req->length = 2;

	spin_unlock(&fotg210->lock);
	fotg210_ep_queue(fotg210->gadget.ep0, fotg210->ep0_req, GFP_ATOMIC);
	spin_lock(&fotg210->lock);
}

static int fotg210_setup_packet(struct fotg210_udc *fotg210,
				struct usb_ctrlrequest *ctrl)
{
	u8 *p = (u8 *)ctrl;
	u8 ret = 0;

	fotg210_rdsetupp(fotg210, p);

	fotg210->ep[0]->dir_in = ctrl->bRequestType & USB_DIR_IN;

	if (fotg210->gadget.speed == USB_SPEED_UNKNOWN) {
		u32 value = ioread32(fotg210->reg + FOTG210_DMCR);
		fotg210->gadget.speed = value & DMCR_HS_EN ?
				USB_SPEED_HIGH : USB_SPEED_FULL;
	}

	/* check request */
	if ((ctrl->bRequestType & USB_TYPE_MASK) == USB_TYPE_STANDARD) {
		switch (ctrl->bRequest) {
		case USB_REQ_GET_STATUS:
			fotg210_get_status(fotg210, ctrl);
			break;
		case USB_REQ_CLEAR_FEATURE:
			fotg210_clear_feature(fotg210, ctrl);
			break;
		case USB_REQ_SET_FEATURE:
			fotg210_set_feature(fotg210, ctrl);
			break;
		case USB_REQ_SET_ADDRESS:
			fotg210_set_address(fotg210, ctrl);
			break;
		case USB_REQ_SET_CONFIGURATION:
			fotg210_set_configuration(fotg210);
			ret = 1;
			break;
		default:
			ret = 1;
			break;
		}
	} else {
		ret = 1;
	}

	return ret;
}

static void fotg210_ep0out(struct fotg210_udc *fotg210)
{
	struct fotg210_ep *ep = fotg210->ep[0];

	if (!list_empty(&ep->queue) && !ep->dir_in) {
		struct fotg210_request *req;

		req = list_first_entry(&ep->queue,
			struct fotg210_request, queue);

		if (req->req.length)
			fotg210_start_dma(ep, req);

		if ((req->req.length - req->req.actual) < ep->ep.maxpacket)
			fotg210_done(ep, req, 0);
	} else {
		pr_err("%s : empty queue\n", __func__);
	}
}

static void fotg210_ep0in(struct fotg210_udc *fotg210)
{
	struct fotg210_ep *ep = fotg210->ep[0];

	if ((!list_empty(&ep->queue)) && (ep->dir_in)) {
		struct fotg210_request *req;

		req = list_entry(ep->queue.next,
				struct fotg210_request, queue);

		if (req->req.length)
			fotg210_start_dma(ep, req);

		if (req->req.actual == req->req.length)
			fotg210_done(ep, req, 0);
	} else {
		fotg210_set_cxdone(fotg210);
	}
}

static void fotg210_clear_comabt_int(struct fotg210_udc *fotg210)
{
	u32 value = ioread32(fotg210->reg + FOTG210_DISGR0);

	value &= ~DISGR0_CX_COMABT_INT;
	iowrite32(value, fotg210->reg + FOTG210_DISGR0);
}

static void fotg210_in_fifo_handler(struct fotg210_ep *ep)
{
	struct fotg210_request *req = list_entry(ep->queue.next,
					struct fotg210_request, queue);

	if (req->req.length)
		fotg210_start_dma(ep, req);
	fotg210_done(ep, req, 0);
}

static void fotg210_out_fifo_handler(struct fotg210_ep *ep)
{
	struct fotg210_request *req = list_entry(ep->queue.next,
						 struct fotg210_request, queue);
	int disgr1 = ioread32(ep->fotg210->reg + FOTG210_DISGR1);

	fotg210_start_dma(ep, req);

	/* Complete the request when it's full or a short packet arrived.
	 * Like other drivers, short_not_ok isn't handled.
	 */

	if (req->req.length == req->req.actual ||
	    (disgr1 & DISGR1_SPK_INT(ep->epnum - 1)))
		fotg210_done(ep, req, 0);
}

static irqreturn_t fotg210_irq(int irq, void *_fotg210)
{
	struct fotg210_udc *fotg210 = _fotg210;
	u32 int_grp = ioread32(fotg210->reg + FOTG210_DIGR);
	u32 int_msk = ioread32(fotg210->reg + FOTG210_DMIGR);

	int_grp &= ~int_msk;

	spin_lock(&fotg210->lock);

	if (int_grp & DIGR_INT_G2) {
		void __iomem *reg = fotg210->reg + FOTG210_DISGR2;
		u32 int_grp2 = ioread32(reg);
		u32 int_msk2 = ioread32(fotg210->reg + FOTG210_DMISGR2);
		u32 value;

		int_grp2 &= ~int_msk2;

		if (int_grp2 & DISGR2_USBRST_INT) {
			usb_gadget_udc_reset(&fotg210->gadget,
					     fotg210->driver);
			value = ioread32(reg);
			value &= ~DISGR2_USBRST_INT;
			iowrite32(value, reg);
			pr_info("fotg210 udc reset\n");
		}
		if (int_grp2 & DISGR2_SUSP_INT) {
			value = ioread32(reg);
			value &= ~DISGR2_SUSP_INT;
			iowrite32(value, reg);
			pr_info("fotg210 udc suspend\n");
		}
		if (int_grp2 & DISGR2_RESM_INT) {
			value = ioread32(reg);
			value &= ~DISGR2_RESM_INT;
			iowrite32(value, reg);
			pr_info("fotg210 udc resume\n");
		}
		if (int_grp2 & DISGR2_ISO_SEQ_ERR_INT) {
			value = ioread32(reg);
			value &= ~DISGR2_ISO_SEQ_ERR_INT;
			iowrite32(value, reg);
			pr_info("fotg210 iso sequence error\n");
		}
		if (int_grp2 & DISGR2_ISO_SEQ_ABORT_INT) {
			value = ioread32(reg);
			value &= ~DISGR2_ISO_SEQ_ABORT_INT;
			iowrite32(value, reg);
			pr_info("fotg210 iso sequence abort\n");
		}
		if (int_grp2 & DISGR2_TX0BYTE_INT) {
			fotg210_clear_tx0byte(fotg210);
			value = ioread32(reg);
			value &= ~DISGR2_TX0BYTE_INT;
			iowrite32(value, reg);
			pr_info("fotg210 transferred 0 byte\n");
		}
		if (int_grp2 & DISGR2_RX0BYTE_INT) {
			fotg210_clear_rx0byte(fotg210);
			value = ioread32(reg);
			value &= ~DISGR2_RX0BYTE_INT;
			iowrite32(value, reg);
			pr_info("fotg210 received 0 byte\n");
		}
		if (int_grp2 & DISGR2_DMA_ERROR) {
			value = ioread32(reg);
			value &= ~DISGR2_DMA_ERROR;
			iowrite32(value, reg);
		}
	}

	if (int_grp & DIGR_INT_G0) {
		void __iomem *reg = fotg210->reg + FOTG210_DISGR0;
		u32 int_grp0 = ioread32(reg);
		u32 int_msk0 = ioread32(fotg210->reg + FOTG210_DMISGR0);
		struct usb_ctrlrequest ctrl;

		int_grp0 &= ~int_msk0;

		/* the highest priority in this source register */
		if (int_grp0 & DISGR0_CX_COMABT_INT) {
			fotg210_clear_comabt_int(fotg210);
			pr_info("fotg210 CX command abort\n");
		}

		if (int_grp0 & DISGR0_CX_SETUP_INT) {
			if (fotg210_setup_packet(fotg210, &ctrl)) {
				spin_unlock(&fotg210->lock);
				if (fotg210->driver->setup(&fotg210->gadget,
							   &ctrl) < 0)
					fotg210_set_cxstall(fotg210);
				spin_lock(&fotg210->lock);
			}
		}
		if (int_grp0 & DISGR0_CX_COMEND_INT)
			pr_info("fotg210 cmd end\n");

		if (int_grp0 & DISGR0_CX_IN_INT)
			fotg210_ep0in(fotg210);

		if (int_grp0 & DISGR0_CX_OUT_INT)
			fotg210_ep0out(fotg210);

		if (int_grp0 & DISGR0_CX_COMFAIL_INT) {
			fotg210_set_cxstall(fotg210);
			pr_info("fotg210 ep0 fail\n");
		}
	}

	if (int_grp & DIGR_INT_G1) {
		void __iomem *reg = fotg210->reg + FOTG210_DISGR1;
		u32 int_grp1 = ioread32(reg);
		u32 int_msk1 = ioread32(fotg210->reg + FOTG210_DMISGR1);
		int fifo;

		int_grp1 &= ~int_msk1;

		for (fifo = 0; fifo < FOTG210_MAX_FIFO_NUM; fifo++) {
			if (int_grp1 & DISGR1_IN_INT(fifo))
				fotg210_in_fifo_handler(fotg210->ep[fifo + 1]);

			if ((int_grp1 & DISGR1_OUT_INT(fifo)) ||
			    (int_grp1 & DISGR1_SPK_INT(fifo)))
				fotg210_out_fifo_handler(fotg210->ep[fifo + 1]);
		}
	}

	spin_unlock(&fotg210->lock);

	return IRQ_HANDLED;
}

static void fotg210_disable_unplug(struct fotg210_udc *fotg210)
{
	u32 reg = ioread32(fotg210->reg + FOTG210_PHYTMSR);

	reg &= ~PHYTMSR_UNPLUG;
	iowrite32(reg, fotg210->reg + FOTG210_PHYTMSR);
}

static int fotg210_udc_start(struct usb_gadget *g,
		struct usb_gadget_driver *driver)
{
	struct fotg210_udc *fotg210 = gadget_to_fotg210(g);
	u32 value;

	/* hook up the driver */
	driver->driver.bus = NULL;
	fotg210->driver = driver;

	/* enable device global interrupt */
	value = ioread32(fotg210->reg + FOTG210_DMCR);
	value |= DMCR_GLINT_EN;
	iowrite32(value, fotg210->reg + FOTG210_DMCR);

	return 0;
}

static void fotg210_init(struct fotg210_udc *fotg210)
{
	u32 value;

	/* disable global interrupt and set int polarity to active high */
	iowrite32(GMIR_MHC_INT | GMIR_MOTG_INT | GMIR_INT_POLARITY,
		  fotg210->reg + FOTG210_GMIR);

	/* disable device global interrupt */
	value = ioread32(fotg210->reg + FOTG210_DMCR);
	value &= ~DMCR_GLINT_EN;
	iowrite32(value, fotg210->reg + FOTG210_DMCR);

	/* enable only grp2 irqs we handle */
	iowrite32(~(DISGR2_DMA_ERROR | DISGR2_RX0BYTE_INT | DISGR2_TX0BYTE_INT
		    | DISGR2_ISO_SEQ_ABORT_INT | DISGR2_ISO_SEQ_ERR_INT
		    | DISGR2_RESM_INT | DISGR2_SUSP_INT | DISGR2_USBRST_INT),
		  fotg210->reg + FOTG210_DMISGR2);

	/* disable all fifo interrupt */
	iowrite32(~(u32)0, fotg210->reg + FOTG210_DMISGR1);

	/* disable cmd end */
	value = ioread32(fotg210->reg + FOTG210_DMISGR0);
	value |= DMISGR0_MCX_COMEND;
	iowrite32(value, fotg210->reg + FOTG210_DMISGR0);
}

static int fotg210_udc_stop(struct usb_gadget *g)
{
	struct fotg210_udc *fotg210 = gadget_to_fotg210(g);
	unsigned long	flags;

	spin_lock_irqsave(&fotg210->lock, flags);

	fotg210_init(fotg210);
	fotg210->driver = NULL;

	spin_unlock_irqrestore(&fotg210->lock, flags);

	return 0;
}

static const struct usb_gadget_ops fotg210_gadget_ops = {
	.udc_start		= fotg210_udc_start,
	.udc_stop		= fotg210_udc_stop,
};

static int fotg210_udc_remove(struct platform_device *pdev)
{
	struct fotg210_udc *fotg210 = platform_get_drvdata(pdev);
	int i;

	usb_del_gadget_udc(&fotg210->gadget);
	iounmap(fotg210->reg);
	free_irq(platform_get_irq(pdev, 0), fotg210);

	fotg210_ep_free_request(&fotg210->ep[0]->ep, fotg210->ep0_req);
	for (i = 0; i < FOTG210_MAX_NUM_EP; i++)
		kfree(fotg210->ep[i]);
	kfree(fotg210);

	return 0;
}

static int fotg210_udc_probe(struct platform_device *pdev)
{
	struct resource *res, *ires;
	struct fotg210_udc *fotg210 = NULL;
	struct fotg210_ep *_ep[FOTG210_MAX_NUM_EP];
	int ret = 0;
	int i;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		pr_err("platform_get_resource error.\n");
		return -ENODEV;
	}

	ires = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!ires) {
		pr_err("platform_get_resource IORESOURCE_IRQ error.\n");
		return -ENODEV;
	}

	ret = -ENOMEM;

	/* initialize udc */
	fotg210 = kzalloc(sizeof(struct fotg210_udc), GFP_KERNEL);
	if (fotg210 == NULL)
		goto err;

	for (i = 0; i < FOTG210_MAX_NUM_EP; i++) {
		_ep[i] = kzalloc(sizeof(struct fotg210_ep), GFP_KERNEL);
		if (_ep[i] == NULL)
			goto err_alloc;
		fotg210->ep[i] = _ep[i];
	}

	fotg210->reg = ioremap(res->start, resource_size(res));
	if (fotg210->reg == NULL) {
		pr_err("ioremap error.\n");
		goto err_alloc;
	}

	spin_lock_init(&fotg210->lock);

	platform_set_drvdata(pdev, fotg210);

	fotg210->gadget.ops = &fotg210_gadget_ops;

	fotg210->gadget.max_speed = USB_SPEED_HIGH;
	fotg210->gadget.dev.parent = &pdev->dev;
	fotg210->gadget.dev.dma_mask = pdev->dev.dma_mask;
	fotg210->gadget.name = udc_name;

	INIT_LIST_HEAD(&fotg210->gadget.ep_list);

	for (i = 0; i < FOTG210_MAX_NUM_EP; i++) {
		struct fotg210_ep *ep = fotg210->ep[i];

		if (i) {
			INIT_LIST_HEAD(&fotg210->ep[i]->ep.ep_list);
			list_add_tail(&fotg210->ep[i]->ep.ep_list,
				      &fotg210->gadget.ep_list);
		}
		ep->fotg210 = fotg210;
		INIT_LIST_HEAD(&ep->queue);
		ep->ep.name = fotg210_ep_name[i];
		ep->ep.ops = &fotg210_ep_ops;
		usb_ep_set_maxpacket_limit(&ep->ep, (unsigned short) ~0);

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
	usb_ep_set_maxpacket_limit(&fotg210->ep[0]->ep, 0x40);
	fotg210->gadget.ep0 = &fotg210->ep[0]->ep;
	INIT_LIST_HEAD(&fotg210->gadget.ep0->ep_list);

	fotg210->ep0_req = fotg210_ep_alloc_request(&fotg210->ep[0]->ep,
				GFP_KERNEL);
	if (fotg210->ep0_req == NULL)
		goto err_map;

	fotg210_init(fotg210);

	fotg210_disable_unplug(fotg210);

	ret = request_irq(ires->start, fotg210_irq, IRQF_SHARED,
			  udc_name, fotg210);
	if (ret < 0) {
		pr_err("request_irq error (%d)\n", ret);
		goto err_req;
	}

	ret = usb_add_gadget_udc(&pdev->dev, &fotg210->gadget);
	if (ret)
		goto err_add_udc;

	dev_info(&pdev->dev, "version %s\n", DRIVER_VERSION);

	return 0;

err_add_udc:
	free_irq(ires->start, fotg210);

err_req:
	fotg210_ep_free_request(&fotg210->ep[0]->ep, fotg210->ep0_req);

err_map:
	iounmap(fotg210->reg);

err_alloc:
	for (i = 0; i < FOTG210_MAX_NUM_EP; i++)
		kfree(fotg210->ep[i]);
	kfree(fotg210);

err:
	return ret;
}

static struct platform_driver fotg210_driver = {
	.driver		= {
		.name =	udc_name,
	},
	.probe		= fotg210_udc_probe,
	.remove		= fotg210_udc_remove,
};

module_platform_driver(fotg210_driver);

MODULE_AUTHOR("Yuan-Hsin Chen, Feng-Hsin Chiang <john453@faraday-tech.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(DRIVER_DESC);
