/*
 * Copyright (C) 2011 Marvell International Ltd. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/notifier.h>
#include <linux/interrupt.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/pm.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/platform_data/mv_usb.h>
#include <linux/clk.h>

#include "mv_u3d.h"

#define DRIVER_DESC		"Marvell PXA USB3.0 Device Controller driver"

static const char driver_name[] = "mv_u3d";
static const char driver_desc[] = DRIVER_DESC;

static void mv_u3d_nuke(struct mv_u3d_ep *ep, int status);
static void mv_u3d_stop_activity(struct mv_u3d *u3d,
			struct usb_gadget_driver *driver);

/* for endpoint 0 operations */
static const struct usb_endpoint_descriptor mv_u3d_ep0_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	0,
	.bmAttributes =		USB_ENDPOINT_XFER_CONTROL,
	.wMaxPacketSize =	MV_U3D_EP0_MAX_PKT_SIZE,
};

static void mv_u3d_ep0_reset(struct mv_u3d *u3d)
{
	struct mv_u3d_ep *ep;
	u32 epxcr;
	int i;

	for (i = 0; i < 2; i++) {
		ep = &u3d->eps[i];
		ep->u3d = u3d;

		/* ep0 ep context, ep0 in and out share the same ep context */
		ep->ep_context = &u3d->ep_context[1];
	}

	/* reset ep state machine */
	/* reset ep0 out */
	epxcr = ioread32(&u3d->vuc_regs->epcr[0].epxoutcr0);
	epxcr |= MV_U3D_EPXCR_EP_INIT;
	iowrite32(epxcr, &u3d->vuc_regs->epcr[0].epxoutcr0);
	udelay(5);
	epxcr &= ~MV_U3D_EPXCR_EP_INIT;
	iowrite32(epxcr, &u3d->vuc_regs->epcr[0].epxoutcr0);

	epxcr = ((MV_U3D_EP0_MAX_PKT_SIZE
		<< MV_U3D_EPXCR_MAX_PACKET_SIZE_SHIFT)
		| (1 << MV_U3D_EPXCR_MAX_BURST_SIZE_SHIFT)
		| (1 << MV_U3D_EPXCR_EP_ENABLE_SHIFT)
		| MV_U3D_EPXCR_EP_TYPE_CONTROL);
	iowrite32(epxcr, &u3d->vuc_regs->epcr[0].epxoutcr1);

	/* reset ep0 in */
	epxcr = ioread32(&u3d->vuc_regs->epcr[0].epxincr0);
	epxcr |= MV_U3D_EPXCR_EP_INIT;
	iowrite32(epxcr, &u3d->vuc_regs->epcr[0].epxincr0);
	udelay(5);
	epxcr &= ~MV_U3D_EPXCR_EP_INIT;
	iowrite32(epxcr, &u3d->vuc_regs->epcr[0].epxincr0);

	epxcr = ((MV_U3D_EP0_MAX_PKT_SIZE
		<< MV_U3D_EPXCR_MAX_PACKET_SIZE_SHIFT)
		| (1 << MV_U3D_EPXCR_MAX_BURST_SIZE_SHIFT)
		| (1 << MV_U3D_EPXCR_EP_ENABLE_SHIFT)
		| MV_U3D_EPXCR_EP_TYPE_CONTROL);
	iowrite32(epxcr, &u3d->vuc_regs->epcr[0].epxincr1);
}

static void mv_u3d_ep0_stall(struct mv_u3d *u3d)
{
	u32 tmp;
	dev_dbg(u3d->dev, "%s\n", __func__);

	/* set TX and RX to stall */
	tmp = ioread32(&u3d->vuc_regs->epcr[0].epxoutcr0);
	tmp |= MV_U3D_EPXCR_EP_HALT;
	iowrite32(tmp, &u3d->vuc_regs->epcr[0].epxoutcr0);

	tmp = ioread32(&u3d->vuc_regs->epcr[0].epxincr0);
	tmp |= MV_U3D_EPXCR_EP_HALT;
	iowrite32(tmp, &u3d->vuc_regs->epcr[0].epxincr0);

	/* update ep0 state */
	u3d->ep0_state = MV_U3D_WAIT_FOR_SETUP;
	u3d->ep0_dir = MV_U3D_EP_DIR_OUT;
}

static int mv_u3d_process_ep_req(struct mv_u3d *u3d, int index,
	struct mv_u3d_req *curr_req)
{
	struct mv_u3d_trb	*curr_trb;
	dma_addr_t cur_deq_lo;
	struct mv_u3d_ep_context	*curr_ep_context;
	int trb_complete, actual, remaining_length = 0;
	int direction, ep_num;
	int retval = 0;
	u32 tmp, status, length;

	curr_ep_context = &u3d->ep_context[index];
	direction = index % 2;
	ep_num = index / 2;

	trb_complete = 0;
	actual = curr_req->req.length;

	while (!list_empty(&curr_req->trb_list)) {
		curr_trb = list_entry(curr_req->trb_list.next,
					struct mv_u3d_trb, trb_list);
		if (!curr_trb->trb_hw->ctrl.own) {
			dev_err(u3d->dev, "%s, TRB own error!\n",
				u3d->eps[index].name);
			return 1;
		}

		curr_trb->trb_hw->ctrl.own = 0;
		if (direction == MV_U3D_EP_DIR_OUT) {
			tmp = ioread32(&u3d->vuc_regs->rxst[ep_num].statuslo);
			cur_deq_lo =
				ioread32(&u3d->vuc_regs->rxst[ep_num].curdeqlo);
		} else {
			tmp = ioread32(&u3d->vuc_regs->txst[ep_num].statuslo);
			cur_deq_lo =
				ioread32(&u3d->vuc_regs->txst[ep_num].curdeqlo);
		}

		status = tmp >> MV_U3D_XFERSTATUS_COMPLETE_SHIFT;
		length = tmp & MV_U3D_XFERSTATUS_TRB_LENGTH_MASK;

		if (status == MV_U3D_COMPLETE_SUCCESS ||
			(status == MV_U3D_COMPLETE_SHORT_PACKET &&
			direction == MV_U3D_EP_DIR_OUT)) {
			remaining_length += length;
			actual -= remaining_length;
		} else {
			dev_err(u3d->dev,
				"complete_tr error: ep=%d %s: error = 0x%x\n",
				index >> 1, direction ? "SEND" : "RECV",
				status);
			retval = -EPROTO;
		}

		list_del_init(&curr_trb->trb_list);
	}
	if (retval)
		return retval;

	curr_req->req.actual = actual;
	return 0;
}

/*
 * mv_u3d_done() - retire a request; caller blocked irqs
 * @status : request status to be set, only works when
 * request is still in progress.
 */
static
void mv_u3d_done(struct mv_u3d_ep *ep, struct mv_u3d_req *req, int status)
	__releases(&ep->udc->lock)
	__acquires(&ep->udc->lock)
{
	struct mv_u3d *u3d = (struct mv_u3d *)ep->u3d;

	dev_dbg(u3d->dev, "mv_u3d_done: remove req->queue\n");
	/* Removed the req from ep queue */
	list_del_init(&req->queue);

	/* req.status should be set as -EINPROGRESS in ep_queue() */
	if (req->req.status == -EINPROGRESS)
		req->req.status = status;
	else
		status = req->req.status;

	/* Free trb for the request */
	if (!req->chain)
		dma_pool_free(u3d->trb_pool,
			req->trb_head->trb_hw, req->trb_head->trb_dma);
	else {
		dma_unmap_single(ep->u3d->gadget.dev.parent,
			(dma_addr_t)req->trb_head->trb_dma,
			req->trb_count * sizeof(struct mv_u3d_trb_hw),
			DMA_BIDIRECTIONAL);
		kfree(req->trb_head->trb_hw);
	}
	kfree(req->trb_head);

	usb_gadget_unmap_request(&u3d->gadget, &req->req, mv_u3d_ep_dir(ep));

	if (status && (status != -ESHUTDOWN)) {
		dev_dbg(u3d->dev, "complete %s req %p stat %d len %u/%u",
			ep->ep.name, &req->req, status,
			req->req.actual, req->req.length);
	}

	spin_unlock(&ep->u3d->lock);

	usb_gadget_giveback_request(&ep->ep, &req->req);

	spin_lock(&ep->u3d->lock);
}

static int mv_u3d_queue_trb(struct mv_u3d_ep *ep, struct mv_u3d_req *req)
{
	u32 tmp, direction;
	struct mv_u3d *u3d;
	struct mv_u3d_ep_context *ep_context;
	int retval = 0;

	u3d = ep->u3d;
	direction = mv_u3d_ep_dir(ep);

	/* ep0 in and out share the same ep context slot 1*/
	if (ep->ep_num == 0)
		ep_context = &(u3d->ep_context[1]);
	else
		ep_context = &(u3d->ep_context[ep->ep_num * 2 + direction]);

	/* check if the pipe is empty or not */
	if (!list_empty(&ep->queue)) {
		dev_err(u3d->dev, "add trb to non-empty queue!\n");
		retval = -ENOMEM;
		WARN_ON(1);
	} else {
		ep_context->rsvd0 = cpu_to_le32(1);
		ep_context->rsvd1 = 0;

		/* Configure the trb address and set the DCS bit.
		 * Both DCS bit and own bit in trb should be set.
		 */
		ep_context->trb_addr_lo =
			cpu_to_le32(req->trb_head->trb_dma | DCS_ENABLE);
		ep_context->trb_addr_hi = 0;

		/* Ensure that updates to the EP Context will
		 * occure before Ring Bell.
		 */
		wmb();

		/* ring bell the ep */
		if (ep->ep_num == 0)
			tmp = 0x1;
		else
			tmp = ep->ep_num * 2
				+ ((direction == MV_U3D_EP_DIR_OUT) ? 0 : 1);

		iowrite32(tmp, &u3d->op_regs->doorbell);
	}
	return retval;
}

static struct mv_u3d_trb *mv_u3d_build_trb_one(struct mv_u3d_req *req,
				unsigned *length, dma_addr_t *dma)
{
	u32 temp;
	unsigned int direction;
	struct mv_u3d_trb *trb;
	struct mv_u3d_trb_hw *trb_hw;
	struct mv_u3d *u3d;

	/* how big will this transfer be? */
	*length = req->req.length - req->req.actual;
	BUG_ON(*length > (unsigned)MV_U3D_EP_MAX_LENGTH_TRANSFER);

	u3d = req->ep->u3d;

	trb = kzalloc(sizeof(*trb), GFP_ATOMIC);
	if (!trb)
		return NULL;

	/*
	 * Be careful that no _GFP_HIGHMEM is set,
	 * or we can not use dma_to_virt
	 * cannot use GFP_KERNEL in spin lock
	 */
	trb_hw = dma_pool_alloc(u3d->trb_pool, GFP_ATOMIC, dma);
	if (!trb_hw) {
		kfree(trb);
		dev_err(u3d->dev,
			"%s, dma_pool_alloc fail\n", __func__);
		return NULL;
	}
	trb->trb_dma = *dma;
	trb->trb_hw = trb_hw;

	/* initialize buffer page pointers */
	temp = (u32)(req->req.dma + req->req.actual);

	trb_hw->buf_addr_lo = cpu_to_le32(temp);
	trb_hw->buf_addr_hi = 0;
	trb_hw->trb_len = cpu_to_le32(*length);
	trb_hw->ctrl.own = 1;

	if (req->ep->ep_num == 0)
		trb_hw->ctrl.type = TYPE_DATA;
	else
		trb_hw->ctrl.type = TYPE_NORMAL;

	req->req.actual += *length;

	direction = mv_u3d_ep_dir(req->ep);
	if (direction == MV_U3D_EP_DIR_IN)
		trb_hw->ctrl.dir = 1;
	else
		trb_hw->ctrl.dir = 0;

	/* Enable interrupt for the last trb of a request */
	if (!req->req.no_interrupt)
		trb_hw->ctrl.ioc = 1;

	trb_hw->ctrl.chain = 0;

	wmb();
	return trb;
}

static int mv_u3d_build_trb_chain(struct mv_u3d_req *req, unsigned *length,
		struct mv_u3d_trb *trb, int *is_last)
{
	u32 temp;
	unsigned int direction;
	struct mv_u3d *u3d;

	/* how big will this transfer be? */
	*length = min(req->req.length - req->req.actual,
			(unsigned)MV_U3D_EP_MAX_LENGTH_TRANSFER);

	u3d = req->ep->u3d;

	trb->trb_dma = 0;

	/* initialize buffer page pointers */
	temp = (u32)(req->req.dma + req->req.actual);

	trb->trb_hw->buf_addr_lo = cpu_to_le32(temp);
	trb->trb_hw->buf_addr_hi = 0;
	trb->trb_hw->trb_len = cpu_to_le32(*length);
	trb->trb_hw->ctrl.own = 1;

	if (req->ep->ep_num == 0)
		trb->trb_hw->ctrl.type = TYPE_DATA;
	else
		trb->trb_hw->ctrl.type = TYPE_NORMAL;

	req->req.actual += *length;

	direction = mv_u3d_ep_dir(req->ep);
	if (direction == MV_U3D_EP_DIR_IN)
		trb->trb_hw->ctrl.dir = 1;
	else
		trb->trb_hw->ctrl.dir = 0;

	/* zlp is needed if req->req.zero is set */
	if (req->req.zero) {
		if (*length == 0 || (*length % req->ep->ep.maxpacket) != 0)
			*is_last = 1;
		else
			*is_last = 0;
	} else if (req->req.length == req->req.actual)
		*is_last = 1;
	else
		*is_last = 0;

	/* Enable interrupt for the last trb of a request */
	if (*is_last && !req->req.no_interrupt)
		trb->trb_hw->ctrl.ioc = 1;

	if (*is_last)
		trb->trb_hw->ctrl.chain = 0;
	else {
		trb->trb_hw->ctrl.chain = 1;
		dev_dbg(u3d->dev, "chain trb\n");
	}

	wmb();

	return 0;
}

/* generate TRB linked list for a request
 * usb controller only supports continous trb chain,
 * that trb structure physical address should be continous.
 */
static int mv_u3d_req_to_trb(struct mv_u3d_req *req)
{
	unsigned count;
	int is_last;
	struct mv_u3d_trb *trb;
	struct mv_u3d_trb_hw *trb_hw;
	struct mv_u3d *u3d;
	dma_addr_t dma;
	unsigned length;
	unsigned trb_num;

	u3d = req->ep->u3d;

	INIT_LIST_HEAD(&req->trb_list);

	length = req->req.length - req->req.actual;
	/* normally the request transfer length is less than 16KB.
	 * we use buil_trb_one() to optimize it.
	 */
	if (length <= (unsigned)MV_U3D_EP_MAX_LENGTH_TRANSFER) {
		trb = mv_u3d_build_trb_one(req, &count, &dma);
		list_add_tail(&trb->trb_list, &req->trb_list);
		req->trb_head = trb;
		req->trb_count = 1;
		req->chain = 0;
	} else {
		trb_num = length / MV_U3D_EP_MAX_LENGTH_TRANSFER;
		if (length % MV_U3D_EP_MAX_LENGTH_TRANSFER)
			trb_num++;

		trb = kcalloc(trb_num, sizeof(*trb), GFP_ATOMIC);
		if (!trb)
			return -ENOMEM;

		trb_hw = kcalloc(trb_num, sizeof(*trb_hw), GFP_ATOMIC);
		if (!trb_hw) {
			kfree(trb);
			return -ENOMEM;
		}

		do {
			trb->trb_hw = trb_hw;
			if (mv_u3d_build_trb_chain(req, &count,
						trb, &is_last)) {
				dev_err(u3d->dev,
					"%s, mv_u3d_build_trb_chain fail\n",
					__func__);
				return -EIO;
			}

			list_add_tail(&trb->trb_list, &req->trb_list);
			req->trb_count++;
			trb++;
			trb_hw++;
		} while (!is_last);

		req->trb_head = list_entry(req->trb_list.next,
					struct mv_u3d_trb, trb_list);
		req->trb_head->trb_dma = dma_map_single(u3d->gadget.dev.parent,
					req->trb_head->trb_hw,
					trb_num * sizeof(*trb_hw),
					DMA_BIDIRECTIONAL);

		req->chain = 1;
	}

	return 0;
}

static int
mv_u3d_start_queue(struct mv_u3d_ep *ep)
{
	struct mv_u3d *u3d = ep->u3d;
	struct mv_u3d_req *req;
	int ret;

	if (!list_empty(&ep->req_list) && !ep->processing)
		req = list_entry(ep->req_list.next, struct mv_u3d_req, list);
	else
		return 0;

	ep->processing = 1;

	/* set up dma mapping */
	ret = usb_gadget_map_request(&u3d->gadget, &req->req,
					mv_u3d_ep_dir(ep));
	if (ret)
		return ret;

	req->req.status = -EINPROGRESS;
	req->req.actual = 0;
	req->trb_count = 0;

	/* build trbs and push them to device queue */
	if (!mv_u3d_req_to_trb(req)) {
		ret = mv_u3d_queue_trb(ep, req);
		if (ret) {
			ep->processing = 0;
			return ret;
		}
	} else {
		ep->processing = 0;
		dev_err(u3d->dev, "%s, mv_u3d_req_to_trb fail\n", __func__);
		return -ENOMEM;
	}

	/* irq handler advances the queue */
	if (req)
		list_add_tail(&req->queue, &ep->queue);

	return 0;
}

static int mv_u3d_ep_enable(struct usb_ep *_ep,
		const struct usb_endpoint_descriptor *desc)
{
	struct mv_u3d *u3d;
	struct mv_u3d_ep *ep;
	struct mv_u3d_ep_context *ep_context;
	u16 max = 0;
	unsigned maxburst = 0;
	u32 epxcr, direction;

	if (!_ep || !desc || desc->bDescriptorType != USB_DT_ENDPOINT)
		return -EINVAL;

	ep = container_of(_ep, struct mv_u3d_ep, ep);
	u3d = ep->u3d;

	if (!u3d->driver || u3d->gadget.speed == USB_SPEED_UNKNOWN)
		return -ESHUTDOWN;

	direction = mv_u3d_ep_dir(ep);
	max = le16_to_cpu(desc->wMaxPacketSize);

	if (!_ep->maxburst)
		_ep->maxburst = 1;
	maxburst = _ep->maxburst;

	/* Get the endpoint context address */
	ep_context = (struct mv_u3d_ep_context *)ep->ep_context;

	/* Set the max burst size */
	switch (desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) {
	case USB_ENDPOINT_XFER_BULK:
		if (maxburst > 16) {
			dev_dbg(u3d->dev,
				"max burst should not be greater "
				"than 16 on bulk ep\n");
			maxburst = 1;
			_ep->maxburst = maxburst;
		}
		dev_dbg(u3d->dev,
			"maxburst: %d on bulk %s\n", maxburst, ep->name);
		break;
	case USB_ENDPOINT_XFER_CONTROL:
		/* control transfer only supports maxburst as one */
		maxburst = 1;
		_ep->maxburst = maxburst;
		break;
	case USB_ENDPOINT_XFER_INT:
		if (maxburst != 1) {
			dev_dbg(u3d->dev,
				"max burst should be 1 on int ep "
				"if transfer size is not 1024\n");
			maxburst = 1;
			_ep->maxburst = maxburst;
		}
		break;
	case USB_ENDPOINT_XFER_ISOC:
		if (maxburst != 1) {
			dev_dbg(u3d->dev,
				"max burst should be 1 on isoc ep "
				"if transfer size is not 1024\n");
			maxburst = 1;
			_ep->maxburst = maxburst;
		}
		break;
	default:
		goto en_done;
	}

	ep->ep.maxpacket = max;
	ep->ep.desc = desc;
	ep->enabled = 1;

	/* Enable the endpoint for Rx or Tx and set the endpoint type */
	if (direction == MV_U3D_EP_DIR_OUT) {
		epxcr = ioread32(&u3d->vuc_regs->epcr[ep->ep_num].epxoutcr0);
		epxcr |= MV_U3D_EPXCR_EP_INIT;
		iowrite32(epxcr, &u3d->vuc_regs->epcr[ep->ep_num].epxoutcr0);
		udelay(5);
		epxcr &= ~MV_U3D_EPXCR_EP_INIT;
		iowrite32(epxcr, &u3d->vuc_regs->epcr[ep->ep_num].epxoutcr0);

		epxcr = ((max << MV_U3D_EPXCR_MAX_PACKET_SIZE_SHIFT)
		      | ((maxburst - 1) << MV_U3D_EPXCR_MAX_BURST_SIZE_SHIFT)
		      | (1 << MV_U3D_EPXCR_EP_ENABLE_SHIFT)
		      | (desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK));
		iowrite32(epxcr, &u3d->vuc_regs->epcr[ep->ep_num].epxoutcr1);
	} else {
		epxcr = ioread32(&u3d->vuc_regs->epcr[ep->ep_num].epxincr0);
		epxcr |= MV_U3D_EPXCR_EP_INIT;
		iowrite32(epxcr, &u3d->vuc_regs->epcr[ep->ep_num].epxincr0);
		udelay(5);
		epxcr &= ~MV_U3D_EPXCR_EP_INIT;
		iowrite32(epxcr, &u3d->vuc_regs->epcr[ep->ep_num].epxincr0);

		epxcr = ((max << MV_U3D_EPXCR_MAX_PACKET_SIZE_SHIFT)
		      | ((maxburst - 1) << MV_U3D_EPXCR_MAX_BURST_SIZE_SHIFT)
		      | (1 << MV_U3D_EPXCR_EP_ENABLE_SHIFT)
		      | (desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK));
		iowrite32(epxcr, &u3d->vuc_regs->epcr[ep->ep_num].epxincr1);
	}

	return 0;
en_done:
	return -EINVAL;
}

static int  mv_u3d_ep_disable(struct usb_ep *_ep)
{
	struct mv_u3d *u3d;
	struct mv_u3d_ep *ep;
	struct mv_u3d_ep_context *ep_context;
	u32 epxcr, direction;
	unsigned long flags;

	if (!_ep)
		return -EINVAL;

	ep = container_of(_ep, struct mv_u3d_ep, ep);
	if (!ep->ep.desc)
		return -EINVAL;

	u3d = ep->u3d;

	/* Get the endpoint context address */
	ep_context = ep->ep_context;

	direction = mv_u3d_ep_dir(ep);

	/* nuke all pending requests (does flush) */
	spin_lock_irqsave(&u3d->lock, flags);
	mv_u3d_nuke(ep, -ESHUTDOWN);
	spin_unlock_irqrestore(&u3d->lock, flags);

	/* Disable the endpoint for Rx or Tx and reset the endpoint type */
	if (direction == MV_U3D_EP_DIR_OUT) {
		epxcr = ioread32(&u3d->vuc_regs->epcr[ep->ep_num].epxoutcr1);
		epxcr &= ~((1 << MV_U3D_EPXCR_EP_ENABLE_SHIFT)
		      | USB_ENDPOINT_XFERTYPE_MASK);
		iowrite32(epxcr, &u3d->vuc_regs->epcr[ep->ep_num].epxoutcr1);
	} else {
		epxcr = ioread32(&u3d->vuc_regs->epcr[ep->ep_num].epxincr1);
		epxcr &= ~((1 << MV_U3D_EPXCR_EP_ENABLE_SHIFT)
		      | USB_ENDPOINT_XFERTYPE_MASK);
		iowrite32(epxcr, &u3d->vuc_regs->epcr[ep->ep_num].epxincr1);
	}

	ep->enabled = 0;

	ep->ep.desc = NULL;
	return 0;
}

static struct usb_request *
mv_u3d_alloc_request(struct usb_ep *_ep, gfp_t gfp_flags)
{
	struct mv_u3d_req *req = NULL;

	req = kzalloc(sizeof *req, gfp_flags);
	if (!req)
		return NULL;

	INIT_LIST_HEAD(&req->queue);

	return &req->req;
}

static void mv_u3d_free_request(struct usb_ep *_ep, struct usb_request *_req)
{
	struct mv_u3d_req *req = container_of(_req, struct mv_u3d_req, req);

	kfree(req);
}

static void mv_u3d_ep_fifo_flush(struct usb_ep *_ep)
{
	struct mv_u3d *u3d;
	u32 direction;
	struct mv_u3d_ep *ep = container_of(_ep, struct mv_u3d_ep, ep);
	unsigned int loops;
	u32 tmp;

	/* if endpoint is not enabled, cannot flush endpoint */
	if (!ep->enabled)
		return;

	u3d = ep->u3d;
	direction = mv_u3d_ep_dir(ep);

	/* ep0 need clear bit after flushing fifo. */
	if (!ep->ep_num) {
		if (direction == MV_U3D_EP_DIR_OUT) {
			tmp = ioread32(&u3d->vuc_regs->epcr[0].epxoutcr0);
			tmp |= MV_U3D_EPXCR_EP_FLUSH;
			iowrite32(tmp, &u3d->vuc_regs->epcr[0].epxoutcr0);
			udelay(10);
			tmp &= ~MV_U3D_EPXCR_EP_FLUSH;
			iowrite32(tmp, &u3d->vuc_regs->epcr[0].epxoutcr0);
		} else {
			tmp = ioread32(&u3d->vuc_regs->epcr[0].epxincr0);
			tmp |= MV_U3D_EPXCR_EP_FLUSH;
			iowrite32(tmp, &u3d->vuc_regs->epcr[0].epxincr0);
			udelay(10);
			tmp &= ~MV_U3D_EPXCR_EP_FLUSH;
			iowrite32(tmp, &u3d->vuc_regs->epcr[0].epxincr0);
		}
		return;
	}

	if (direction == MV_U3D_EP_DIR_OUT) {
		tmp = ioread32(&u3d->vuc_regs->epcr[ep->ep_num].epxoutcr0);
		tmp |= MV_U3D_EPXCR_EP_FLUSH;
		iowrite32(tmp, &u3d->vuc_regs->epcr[ep->ep_num].epxoutcr0);

		/* Wait until flushing completed */
		loops = LOOPS(MV_U3D_FLUSH_TIMEOUT);
		while (ioread32(&u3d->vuc_regs->epcr[ep->ep_num].epxoutcr0) &
			MV_U3D_EPXCR_EP_FLUSH) {
			/*
			 * EP_FLUSH bit should be cleared to indicate this
			 * operation is complete
			 */
			if (loops == 0) {
				dev_dbg(u3d->dev,
				    "EP FLUSH TIMEOUT for ep%d%s\n", ep->ep_num,
				    direction ? "in" : "out");
				return;
			}
			loops--;
			udelay(LOOPS_USEC);
		}
	} else {	/* EP_DIR_IN */
		tmp = ioread32(&u3d->vuc_regs->epcr[ep->ep_num].epxincr0);
		tmp |= MV_U3D_EPXCR_EP_FLUSH;
		iowrite32(tmp, &u3d->vuc_regs->epcr[ep->ep_num].epxincr0);

		/* Wait until flushing completed */
		loops = LOOPS(MV_U3D_FLUSH_TIMEOUT);
		while (ioread32(&u3d->vuc_regs->epcr[ep->ep_num].epxincr0) &
			MV_U3D_EPXCR_EP_FLUSH) {
			/*
			* EP_FLUSH bit should be cleared to indicate this
			* operation is complete
			*/
			if (loops == 0) {
				dev_dbg(u3d->dev,
				    "EP FLUSH TIMEOUT for ep%d%s\n", ep->ep_num,
				    direction ? "in" : "out");
				return;
			}
			loops--;
			udelay(LOOPS_USEC);
		}
	}
}

/* queues (submits) an I/O request to an endpoint */
static int
mv_u3d_ep_queue(struct usb_ep *_ep, struct usb_request *_req, gfp_t gfp_flags)
{
	struct mv_u3d_ep *ep;
	struct mv_u3d_req *req;
	struct mv_u3d *u3d;
	unsigned long flags;
	int is_first_req = 0;

	if (unlikely(!_ep || !_req))
		return -EINVAL;

	ep = container_of(_ep, struct mv_u3d_ep, ep);
	u3d = ep->u3d;

	req = container_of(_req, struct mv_u3d_req, req);

	if (!ep->ep_num
		&& u3d->ep0_state == MV_U3D_STATUS_STAGE
		&& !_req->length) {
		dev_dbg(u3d->dev, "ep0 status stage\n");
		u3d->ep0_state = MV_U3D_WAIT_FOR_SETUP;
		return 0;
	}

	dev_dbg(u3d->dev, "%s: %s, req: 0x%p\n",
			__func__, _ep->name, req);

	/* catch various bogus parameters */
	if (!req->req.complete || !req->req.buf
			|| !list_empty(&req->queue)) {
		dev_err(u3d->dev,
			"%s, bad params, _req: 0x%p,"
			"req->req.complete: 0x%p, req->req.buf: 0x%p,"
			"list_empty: 0x%x\n",
			__func__, _req,
			req->req.complete, req->req.buf,
			list_empty(&req->queue));
		return -EINVAL;
	}
	if (unlikely(!ep->ep.desc)) {
		dev_err(u3d->dev, "%s, bad ep\n", __func__);
		return -EINVAL;
	}
	if (ep->ep.desc->bmAttributes == USB_ENDPOINT_XFER_ISOC) {
		if (req->req.length > ep->ep.maxpacket)
			return -EMSGSIZE;
	}

	if (!u3d->driver || u3d->gadget.speed == USB_SPEED_UNKNOWN) {
		dev_err(u3d->dev,
			"bad params of driver/speed\n");
		return -ESHUTDOWN;
	}

	req->ep = ep;

	/* Software list handles usb request. */
	spin_lock_irqsave(&ep->req_lock, flags);
	is_first_req = list_empty(&ep->req_list);
	list_add_tail(&req->list, &ep->req_list);
	spin_unlock_irqrestore(&ep->req_lock, flags);
	if (!is_first_req) {
		dev_dbg(u3d->dev, "list is not empty\n");
		return 0;
	}

	dev_dbg(u3d->dev, "call mv_u3d_start_queue from usb_ep_queue\n");
	spin_lock_irqsave(&u3d->lock, flags);
	mv_u3d_start_queue(ep);
	spin_unlock_irqrestore(&u3d->lock, flags);
	return 0;
}

/* dequeues (cancels, unlinks) an I/O request from an endpoint */
static int mv_u3d_ep_dequeue(struct usb_ep *_ep, struct usb_request *_req)
{
	struct mv_u3d_ep *ep;
	struct mv_u3d_req *req;
	struct mv_u3d *u3d;
	struct mv_u3d_ep_context *ep_context;
	struct mv_u3d_req *next_req;

	unsigned long flags;
	int ret = 0;

	if (!_ep || !_req)
		return -EINVAL;

	ep = container_of(_ep, struct mv_u3d_ep, ep);
	u3d = ep->u3d;

	spin_lock_irqsave(&ep->u3d->lock, flags);

	/* make sure it's actually queued on this endpoint */
	list_for_each_entry(req, &ep->queue, queue) {
		if (&req->req == _req)
			break;
	}
	if (&req->req != _req) {
		ret = -EINVAL;
		goto out;
	}

	/* The request is in progress, or completed but not dequeued */
	if (ep->queue.next == &req->queue) {
		_req->status = -ECONNRESET;
		mv_u3d_ep_fifo_flush(_ep);

		/* The request isn't the last request in this ep queue */
		if (req->queue.next != &ep->queue) {
			dev_dbg(u3d->dev,
				"it is the last request in this ep queue\n");
			ep_context = ep->ep_context;
			next_req = list_entry(req->queue.next,
					struct mv_u3d_req, queue);

			/* Point first TRB of next request to the EP context. */
			iowrite32((unsigned long) next_req->trb_head,
					&ep_context->trb_addr_lo);
		} else {
			struct mv_u3d_ep_context *ep_context;
			ep_context = ep->ep_context;
			ep_context->trb_addr_lo = 0;
			ep_context->trb_addr_hi = 0;
		}

	} else
		WARN_ON(1);

	mv_u3d_done(ep, req, -ECONNRESET);

	/* remove the req from the ep req list */
	if (!list_empty(&ep->req_list)) {
		struct mv_u3d_req *curr_req;
		curr_req = list_entry(ep->req_list.next,
					struct mv_u3d_req, list);
		if (curr_req == req) {
			list_del_init(&req->list);
			ep->processing = 0;
		}
	}

out:
	spin_unlock_irqrestore(&ep->u3d->lock, flags);
	return ret;
}

static void
mv_u3d_ep_set_stall(struct mv_u3d *u3d, u8 ep_num, u8 direction, int stall)
{
	u32 tmp;
	struct mv_u3d_ep *ep = u3d->eps;

	dev_dbg(u3d->dev, "%s\n", __func__);
	if (direction == MV_U3D_EP_DIR_OUT) {
		tmp = ioread32(&u3d->vuc_regs->epcr[ep->ep_num].epxoutcr0);
		if (stall)
			tmp |= MV_U3D_EPXCR_EP_HALT;
		else
			tmp &= ~MV_U3D_EPXCR_EP_HALT;
		iowrite32(tmp, &u3d->vuc_regs->epcr[ep->ep_num].epxoutcr0);
	} else {
		tmp = ioread32(&u3d->vuc_regs->epcr[ep->ep_num].epxincr0);
		if (stall)
			tmp |= MV_U3D_EPXCR_EP_HALT;
		else
			tmp &= ~MV_U3D_EPXCR_EP_HALT;
		iowrite32(tmp, &u3d->vuc_regs->epcr[ep->ep_num].epxincr0);
	}
}

static int mv_u3d_ep_set_halt_wedge(struct usb_ep *_ep, int halt, int wedge)
{
	struct mv_u3d_ep *ep;
	unsigned long flags = 0;
	int status = 0;
	struct mv_u3d *u3d;

	ep = container_of(_ep, struct mv_u3d_ep, ep);
	u3d = ep->u3d;
	if (!ep->ep.desc) {
		status = -EINVAL;
		goto out;
	}

	if (ep->ep.desc->bmAttributes == USB_ENDPOINT_XFER_ISOC) {
		status = -EOPNOTSUPP;
		goto out;
	}

	/*
	 * Attempt to halt IN ep will fail if any transfer requests
	 * are still queue
	 */
	if (halt && (mv_u3d_ep_dir(ep) == MV_U3D_EP_DIR_IN)
			&& !list_empty(&ep->queue)) {
		status = -EAGAIN;
		goto out;
	}

	spin_lock_irqsave(&ep->u3d->lock, flags);
	mv_u3d_ep_set_stall(u3d, ep->ep_num, mv_u3d_ep_dir(ep), halt);
	if (halt && wedge)
		ep->wedge = 1;
	else if (!halt)
		ep->wedge = 0;
	spin_unlock_irqrestore(&ep->u3d->lock, flags);

	if (ep->ep_num == 0)
		u3d->ep0_dir = MV_U3D_EP_DIR_OUT;
out:
	return status;
}

static int mv_u3d_ep_set_halt(struct usb_ep *_ep, int halt)
{
	return mv_u3d_ep_set_halt_wedge(_ep, halt, 0);
}

static int mv_u3d_ep_set_wedge(struct usb_ep *_ep)
{
	return mv_u3d_ep_set_halt_wedge(_ep, 1, 1);
}

static struct usb_ep_ops mv_u3d_ep_ops = {
	.enable		= mv_u3d_ep_enable,
	.disable	= mv_u3d_ep_disable,

	.alloc_request	= mv_u3d_alloc_request,
	.free_request	= mv_u3d_free_request,

	.queue		= mv_u3d_ep_queue,
	.dequeue	= mv_u3d_ep_dequeue,

	.set_wedge	= mv_u3d_ep_set_wedge,
	.set_halt	= mv_u3d_ep_set_halt,
	.fifo_flush	= mv_u3d_ep_fifo_flush,
};

static void mv_u3d_controller_stop(struct mv_u3d *u3d)
{
	u32 tmp;

	if (!u3d->clock_gating && u3d->vbus_valid_detect)
		iowrite32(MV_U3D_INTR_ENABLE_VBUS_VALID,
				&u3d->vuc_regs->intrenable);
	else
		iowrite32(0, &u3d->vuc_regs->intrenable);
	iowrite32(~0x0, &u3d->vuc_regs->endcomplete);
	iowrite32(~0x0, &u3d->vuc_regs->trbunderrun);
	iowrite32(~0x0, &u3d->vuc_regs->trbcomplete);
	iowrite32(~0x0, &u3d->vuc_regs->linkchange);
	iowrite32(0x1, &u3d->vuc_regs->setuplock);

	/* Reset the RUN bit in the command register to stop USB */
	tmp = ioread32(&u3d->op_regs->usbcmd);
	tmp &= ~MV_U3D_CMD_RUN_STOP;
	iowrite32(tmp, &u3d->op_regs->usbcmd);
	dev_dbg(u3d->dev, "after u3d_stop, USBCMD 0x%x\n",
		ioread32(&u3d->op_regs->usbcmd));
}

static void mv_u3d_controller_start(struct mv_u3d *u3d)
{
	u32 usbintr;
	u32 temp;

	/* enable link LTSSM state machine */
	temp = ioread32(&u3d->vuc_regs->ltssm);
	temp |= MV_U3D_LTSSM_PHY_INIT_DONE;
	iowrite32(temp, &u3d->vuc_regs->ltssm);

	/* Enable interrupts */
	usbintr = MV_U3D_INTR_ENABLE_LINK_CHG | MV_U3D_INTR_ENABLE_TXDESC_ERR |
		MV_U3D_INTR_ENABLE_RXDESC_ERR | MV_U3D_INTR_ENABLE_TX_COMPLETE |
		MV_U3D_INTR_ENABLE_RX_COMPLETE | MV_U3D_INTR_ENABLE_SETUP |
		(u3d->vbus_valid_detect ? MV_U3D_INTR_ENABLE_VBUS_VALID : 0);
	iowrite32(usbintr, &u3d->vuc_regs->intrenable);

	/* Enable ctrl ep */
	iowrite32(0x1, &u3d->vuc_regs->ctrlepenable);

	/* Set the Run bit in the command register */
	iowrite32(MV_U3D_CMD_RUN_STOP, &u3d->op_regs->usbcmd);
	dev_dbg(u3d->dev, "after u3d_start, USBCMD 0x%x\n",
		ioread32(&u3d->op_regs->usbcmd));
}

static int mv_u3d_controller_reset(struct mv_u3d *u3d)
{
	unsigned int loops;
	u32 tmp;

	/* Stop the controller */
	tmp = ioread32(&u3d->op_regs->usbcmd);
	tmp &= ~MV_U3D_CMD_RUN_STOP;
	iowrite32(tmp, &u3d->op_regs->usbcmd);

	/* Reset the controller to get default values */
	iowrite32(MV_U3D_CMD_CTRL_RESET, &u3d->op_regs->usbcmd);

	/* wait for reset to complete */
	loops = LOOPS(MV_U3D_RESET_TIMEOUT);
	while (ioread32(&u3d->op_regs->usbcmd) & MV_U3D_CMD_CTRL_RESET) {
		if (loops == 0) {
			dev_err(u3d->dev,
				"Wait for RESET completed TIMEOUT\n");
			return -ETIMEDOUT;
		}
		loops--;
		udelay(LOOPS_USEC);
	}

	/* Configure the Endpoint Context Address */
	iowrite32(u3d->ep_context_dma, &u3d->op_regs->dcbaapl);
	iowrite32(0, &u3d->op_regs->dcbaaph);

	return 0;
}

static int mv_u3d_enable(struct mv_u3d *u3d)
{
	struct mv_usb_platform_data *pdata = dev_get_platdata(u3d->dev);
	int retval;

	if (u3d->active)
		return 0;

	if (!u3d->clock_gating) {
		u3d->active = 1;
		return 0;
	}

	dev_dbg(u3d->dev, "enable u3d\n");
	clk_enable(u3d->clk);
	if (pdata->phy_init) {
		retval = pdata->phy_init(u3d->phy_regs);
		if (retval) {
			dev_err(u3d->dev,
				"init phy error %d\n", retval);
			clk_disable(u3d->clk);
			return retval;
		}
	}
	u3d->active = 1;

	return 0;
}

static void mv_u3d_disable(struct mv_u3d *u3d)
{
	struct mv_usb_platform_data *pdata = dev_get_platdata(u3d->dev);
	if (u3d->clock_gating && u3d->active) {
		dev_dbg(u3d->dev, "disable u3d\n");
		if (pdata->phy_deinit)
			pdata->phy_deinit(u3d->phy_regs);
		clk_disable(u3d->clk);
		u3d->active = 0;
	}
}

static int mv_u3d_vbus_session(struct usb_gadget *gadget, int is_active)
{
	struct mv_u3d *u3d;
	unsigned long flags;
	int retval = 0;

	u3d = container_of(gadget, struct mv_u3d, gadget);

	spin_lock_irqsave(&u3d->lock, flags);

	u3d->vbus_active = (is_active != 0);
	dev_dbg(u3d->dev, "%s: softconnect %d, vbus_active %d\n",
		__func__, u3d->softconnect, u3d->vbus_active);
	/*
	 * 1. external VBUS detect: we can disable/enable clock on demand.
	 * 2. UDC VBUS detect: we have to enable clock all the time.
	 * 3. No VBUS detect: we have to enable clock all the time.
	 */
	if (u3d->driver && u3d->softconnect && u3d->vbus_active) {
		retval = mv_u3d_enable(u3d);
		if (retval == 0) {
			/*
			 * after clock is disabled, we lost all the register
			 *  context. We have to re-init registers
			 */
			mv_u3d_controller_reset(u3d);
			mv_u3d_ep0_reset(u3d);
			mv_u3d_controller_start(u3d);
		}
	} else if (u3d->driver && u3d->softconnect) {
		if (!u3d->active)
			goto out;

		/* stop all the transfer in queue*/
		mv_u3d_stop_activity(u3d, u3d->driver);
		mv_u3d_controller_stop(u3d);
		mv_u3d_disable(u3d);
	}

out:
	spin_unlock_irqrestore(&u3d->lock, flags);
	return retval;
}

/* constrain controller's VBUS power usage
 * This call is used by gadget drivers during SET_CONFIGURATION calls,
 * reporting how much power the device may consume.  For example, this
 * could affect how quickly batteries are recharged.
 *
 * Returns zero on success, else negative errno.
 */
static int mv_u3d_vbus_draw(struct usb_gadget *gadget, unsigned mA)
{
	struct mv_u3d *u3d = container_of(gadget, struct mv_u3d, gadget);

	u3d->power = mA;

	return 0;
}

static int mv_u3d_pullup(struct usb_gadget *gadget, int is_on)
{
	struct mv_u3d *u3d = container_of(gadget, struct mv_u3d, gadget);
	unsigned long flags;
	int retval = 0;

	spin_lock_irqsave(&u3d->lock, flags);

	dev_dbg(u3d->dev, "%s: softconnect %d, vbus_active %d\n",
		__func__, u3d->softconnect, u3d->vbus_active);
	u3d->softconnect = (is_on != 0);
	if (u3d->driver && u3d->softconnect && u3d->vbus_active) {
		retval = mv_u3d_enable(u3d);
		if (retval == 0) {
			/*
			 * after clock is disabled, we lost all the register
			 *  context. We have to re-init registers
			 */
			mv_u3d_controller_reset(u3d);
			mv_u3d_ep0_reset(u3d);
			mv_u3d_controller_start(u3d);
		}
	} else if (u3d->driver && u3d->vbus_active) {
		/* stop all the transfer in queue*/
		mv_u3d_stop_activity(u3d, u3d->driver);
		mv_u3d_controller_stop(u3d);
		mv_u3d_disable(u3d);
	}

	spin_unlock_irqrestore(&u3d->lock, flags);

	return retval;
}

static int mv_u3d_start(struct usb_gadget *g,
		struct usb_gadget_driver *driver)
{
	struct mv_u3d *u3d = container_of(g, struct mv_u3d, gadget);
	struct mv_usb_platform_data *pdata = dev_get_platdata(u3d->dev);
	unsigned long flags;

	if (u3d->driver)
		return -EBUSY;

	spin_lock_irqsave(&u3d->lock, flags);

	if (!u3d->clock_gating) {
		clk_enable(u3d->clk);
		if (pdata->phy_init)
			pdata->phy_init(u3d->phy_regs);
	}

	/* hook up the driver ... */
	driver->driver.bus = NULL;
	u3d->driver = driver;

	u3d->ep0_dir = USB_DIR_OUT;

	spin_unlock_irqrestore(&u3d->lock, flags);

	u3d->vbus_valid_detect = 1;

	return 0;
}

static int mv_u3d_stop(struct usb_gadget *g,
		struct usb_gadget_driver *driver)
{
	struct mv_u3d *u3d = container_of(g, struct mv_u3d, gadget);
	struct mv_usb_platform_data *pdata = dev_get_platdata(u3d->dev);
	unsigned long flags;

	u3d->vbus_valid_detect = 0;
	spin_lock_irqsave(&u3d->lock, flags);

	/* enable clock to access controller register */
	clk_enable(u3d->clk);
	if (pdata->phy_init)
		pdata->phy_init(u3d->phy_regs);

	mv_u3d_controller_stop(u3d);
	/* stop all usb activities */
	u3d->gadget.speed = USB_SPEED_UNKNOWN;
	mv_u3d_stop_activity(u3d, driver);
	mv_u3d_disable(u3d);

	if (pdata->phy_deinit)
		pdata->phy_deinit(u3d->phy_regs);
	clk_disable(u3d->clk);

	spin_unlock_irqrestore(&u3d->lock, flags);

	u3d->driver = NULL;

	return 0;
}

/* device controller usb_gadget_ops structure */
static const struct usb_gadget_ops mv_u3d_ops = {
	/* notify controller that VBUS is powered or not */
	.vbus_session	= mv_u3d_vbus_session,

	/* constrain controller's VBUS power usage */
	.vbus_draw	= mv_u3d_vbus_draw,

	.pullup		= mv_u3d_pullup,
	.udc_start	= mv_u3d_start,
	.udc_stop	= mv_u3d_stop,
};

static int mv_u3d_eps_init(struct mv_u3d *u3d)
{
	struct mv_u3d_ep	*ep;
	char name[14];
	int i;

	/* initialize ep0, ep0 in/out use eps[1] */
	ep = &u3d->eps[1];
	ep->u3d = u3d;
	strncpy(ep->name, "ep0", sizeof(ep->name));
	ep->ep.name = ep->name;
	ep->ep.ops = &mv_u3d_ep_ops;
	ep->wedge = 0;
	usb_ep_set_maxpacket_limit(&ep->ep, MV_U3D_EP0_MAX_PKT_SIZE);
	ep->ep_num = 0;
	ep->ep.desc = &mv_u3d_ep0_desc;
	INIT_LIST_HEAD(&ep->queue);
	INIT_LIST_HEAD(&ep->req_list);
	ep->ep_type = USB_ENDPOINT_XFER_CONTROL;

	/* add ep0 ep_context */
	ep->ep_context = &u3d->ep_context[1];

	/* initialize other endpoints */
	for (i = 2; i < u3d->max_eps * 2; i++) {
		ep = &u3d->eps[i];
		if (i & 1) {
			snprintf(name, sizeof(name), "ep%din", i >> 1);
			ep->direction = MV_U3D_EP_DIR_IN;
		} else {
			snprintf(name, sizeof(name), "ep%dout", i >> 1);
			ep->direction = MV_U3D_EP_DIR_OUT;
		}
		ep->u3d = u3d;
		strncpy(ep->name, name, sizeof(ep->name));
		ep->ep.name = ep->name;

		ep->ep.ops = &mv_u3d_ep_ops;
		usb_ep_set_maxpacket_limit(&ep->ep, (unsigned short) ~0);
		ep->ep_num = i / 2;

		INIT_LIST_HEAD(&ep->queue);
		list_add_tail(&ep->ep.ep_list, &u3d->gadget.ep_list);

		INIT_LIST_HEAD(&ep->req_list);
		spin_lock_init(&ep->req_lock);
		ep->ep_context = &u3d->ep_context[i];
	}

	return 0;
}

/* delete all endpoint requests, called with spinlock held */
static void mv_u3d_nuke(struct mv_u3d_ep *ep, int status)
{
	/* endpoint fifo flush */
	mv_u3d_ep_fifo_flush(&ep->ep);

	while (!list_empty(&ep->queue)) {
		struct mv_u3d_req *req = NULL;
		req = list_entry(ep->queue.next, struct mv_u3d_req, queue);
		mv_u3d_done(ep, req, status);
	}
}

/* stop all USB activities */
static
void mv_u3d_stop_activity(struct mv_u3d *u3d, struct usb_gadget_driver *driver)
{
	struct mv_u3d_ep	*ep;

	mv_u3d_nuke(&u3d->eps[1], -ESHUTDOWN);

	list_for_each_entry(ep, &u3d->gadget.ep_list, ep.ep_list) {
		mv_u3d_nuke(ep, -ESHUTDOWN);
	}

	/* report disconnect; the driver is already quiesced */
	if (driver) {
		spin_unlock(&u3d->lock);
		driver->disconnect(&u3d->gadget);
		spin_lock(&u3d->lock);
	}
}

static void mv_u3d_irq_process_error(struct mv_u3d *u3d)
{
	/* Increment the error count */
	u3d->errors++;
	dev_err(u3d->dev, "%s\n", __func__);
}

static void mv_u3d_irq_process_link_change(struct mv_u3d *u3d)
{
	u32 linkchange;

	linkchange = ioread32(&u3d->vuc_regs->linkchange);
	iowrite32(linkchange, &u3d->vuc_regs->linkchange);

	dev_dbg(u3d->dev, "linkchange: 0x%x\n", linkchange);

	if (linkchange & MV_U3D_LINK_CHANGE_LINK_UP) {
		dev_dbg(u3d->dev, "link up: ltssm state: 0x%x\n",
			ioread32(&u3d->vuc_regs->ltssmstate));

		u3d->usb_state = USB_STATE_DEFAULT;
		u3d->ep0_dir = MV_U3D_EP_DIR_OUT;
		u3d->ep0_state = MV_U3D_WAIT_FOR_SETUP;

		/* set speed */
		u3d->gadget.speed = USB_SPEED_SUPER;
	}

	if (linkchange & MV_U3D_LINK_CHANGE_SUSPEND) {
		dev_dbg(u3d->dev, "link suspend\n");
		u3d->resume_state = u3d->usb_state;
		u3d->usb_state = USB_STATE_SUSPENDED;
	}

	if (linkchange & MV_U3D_LINK_CHANGE_RESUME) {
		dev_dbg(u3d->dev, "link resume\n");
		u3d->usb_state = u3d->resume_state;
		u3d->resume_state = 0;
	}

	if (linkchange & MV_U3D_LINK_CHANGE_WRESET) {
		dev_dbg(u3d->dev, "warm reset\n");
		u3d->usb_state = USB_STATE_POWERED;
	}

	if (linkchange & MV_U3D_LINK_CHANGE_HRESET) {
		dev_dbg(u3d->dev, "hot reset\n");
		u3d->usb_state = USB_STATE_DEFAULT;
	}

	if (linkchange & MV_U3D_LINK_CHANGE_INACT)
		dev_dbg(u3d->dev, "inactive\n");

	if (linkchange & MV_U3D_LINK_CHANGE_DISABLE_AFTER_U0)
		dev_dbg(u3d->dev, "ss.disabled\n");

	if (linkchange & MV_U3D_LINK_CHANGE_VBUS_INVALID) {
		dev_dbg(u3d->dev, "vbus invalid\n");
		u3d->usb_state = USB_STATE_ATTACHED;
		u3d->vbus_valid_detect = 1;
		/* if external vbus detect is not supported,
		 * we handle it here.
		 */
		if (!u3d->vbus) {
			spin_unlock(&u3d->lock);
			mv_u3d_vbus_session(&u3d->gadget, 0);
			spin_lock(&u3d->lock);
		}
	}
}

static void mv_u3d_ch9setaddress(struct mv_u3d *u3d,
				struct usb_ctrlrequest *setup)
{
	u32 tmp;

	if (u3d->usb_state != USB_STATE_DEFAULT) {
		dev_err(u3d->dev,
			"%s, cannot setaddr in this state (%d)\n",
			__func__, u3d->usb_state);
		goto err;
	}

	u3d->dev_addr = (u8)setup->wValue;

	dev_dbg(u3d->dev, "%s: 0x%x\n", __func__, u3d->dev_addr);

	if (u3d->dev_addr > 127) {
		dev_err(u3d->dev,
			"%s, u3d address is wrong (out of range)\n", __func__);
		u3d->dev_addr = 0;
		goto err;
	}

	/* update usb state */
	u3d->usb_state = USB_STATE_ADDRESS;

	/* set the new address */
	tmp = ioread32(&u3d->vuc_regs->devaddrtiebrkr);
	tmp &= ~0x7F;
	tmp |= (u32)u3d->dev_addr;
	iowrite32(tmp, &u3d->vuc_regs->devaddrtiebrkr);

	return;
err:
	mv_u3d_ep0_stall(u3d);
}

static int mv_u3d_is_set_configuration(struct usb_ctrlrequest *setup)
{
	if ((setup->bRequestType & USB_TYPE_MASK) == USB_TYPE_STANDARD)
		if (setup->bRequest == USB_REQ_SET_CONFIGURATION)
			return 1;

	return 0;
}

static void mv_u3d_handle_setup_packet(struct mv_u3d *u3d, u8 ep_num,
	struct usb_ctrlrequest *setup)
	__releases(&u3c->lock)
	__acquires(&u3c->lock)
{
	bool delegate = false;

	mv_u3d_nuke(&u3d->eps[ep_num * 2 + MV_U3D_EP_DIR_IN], -ESHUTDOWN);

	dev_dbg(u3d->dev, "SETUP %02x.%02x v%04x i%04x l%04x\n",
			setup->bRequestType, setup->bRequest,
			setup->wValue, setup->wIndex, setup->wLength);

	/* We process some stardard setup requests here */
	if ((setup->bRequestType & USB_TYPE_MASK) == USB_TYPE_STANDARD) {
		switch (setup->bRequest) {
		case USB_REQ_GET_STATUS:
			delegate = true;
			break;

		case USB_REQ_SET_ADDRESS:
			mv_u3d_ch9setaddress(u3d, setup);
			break;

		case USB_REQ_CLEAR_FEATURE:
			delegate = true;
			break;

		case USB_REQ_SET_FEATURE:
			delegate = true;
			break;

		default:
			delegate = true;
		}
	} else
		delegate = true;

	/* delegate USB standard requests to the gadget driver */
	if (delegate == true) {
		/* USB requests handled by gadget */
		if (setup->wLength) {
			/* DATA phase from gadget, STATUS phase from u3d */
			u3d->ep0_dir = (setup->bRequestType & USB_DIR_IN)
					? MV_U3D_EP_DIR_IN : MV_U3D_EP_DIR_OUT;
			spin_unlock(&u3d->lock);
			if (u3d->driver->setup(&u3d->gadget,
				&u3d->local_setup_buff) < 0) {
				dev_err(u3d->dev, "setup error!\n");
				mv_u3d_ep0_stall(u3d);
			}
			spin_lock(&u3d->lock);
		} else {
			/* no DATA phase, STATUS phase from gadget */
			u3d->ep0_dir = MV_U3D_EP_DIR_IN;
			u3d->ep0_state = MV_U3D_STATUS_STAGE;
			spin_unlock(&u3d->lock);
			if (u3d->driver->setup(&u3d->gadget,
				&u3d->local_setup_buff) < 0)
				mv_u3d_ep0_stall(u3d);
			spin_lock(&u3d->lock);
		}

		if (mv_u3d_is_set_configuration(setup)) {
			dev_dbg(u3d->dev, "u3d configured\n");
			u3d->usb_state = USB_STATE_CONFIGURED;
		}
	}
}

static void mv_u3d_get_setup_data(struct mv_u3d *u3d, u8 ep_num, u8 *buffer_ptr)
{
	struct mv_u3d_ep_context *epcontext;

	epcontext = &u3d->ep_context[ep_num * 2 + MV_U3D_EP_DIR_IN];

	/* Copy the setup packet to local buffer */
	memcpy(buffer_ptr, (u8 *) &epcontext->setup_buffer, 8);
}

static void mv_u3d_irq_process_setup(struct mv_u3d *u3d)
{
	u32 tmp, i;
	/* Process all Setup packet received interrupts */
	tmp = ioread32(&u3d->vuc_regs->setuplock);
	if (tmp) {
		for (i = 0; i < u3d->max_eps; i++) {
			if (tmp & (1 << i)) {
				mv_u3d_get_setup_data(u3d, i,
					(u8 *)(&u3d->local_setup_buff));
				mv_u3d_handle_setup_packet(u3d, i,
					&u3d->local_setup_buff);
			}
		}
	}

	iowrite32(tmp, &u3d->vuc_regs->setuplock);
}

static void mv_u3d_irq_process_tr_complete(struct mv_u3d *u3d)
{
	u32 tmp, bit_pos;
	int i, ep_num = 0, direction = 0;
	struct mv_u3d_ep	*curr_ep;
	struct mv_u3d_req *curr_req, *temp_req;
	int status;

	tmp = ioread32(&u3d->vuc_regs->endcomplete);

	dev_dbg(u3d->dev, "tr_complete: ep: 0x%x\n", tmp);
	if (!tmp)
		return;
	iowrite32(tmp, &u3d->vuc_regs->endcomplete);

	for (i = 0; i < u3d->max_eps * 2; i++) {
		ep_num = i >> 1;
		direction = i % 2;

		bit_pos = 1 << (ep_num + 16 * direction);

		if (!(bit_pos & tmp))
			continue;

		if (i == 0)
			curr_ep = &u3d->eps[1];
		else
			curr_ep = &u3d->eps[i];

		/* remove req out of ep request list after completion */
		dev_dbg(u3d->dev, "tr comp: check req_list\n");
		spin_lock(&curr_ep->req_lock);
		if (!list_empty(&curr_ep->req_list)) {
			struct mv_u3d_req *req;
			req = list_entry(curr_ep->req_list.next,
						struct mv_u3d_req, list);
			list_del_init(&req->list);
			curr_ep->processing = 0;
		}
		spin_unlock(&curr_ep->req_lock);

		/* process the req queue until an uncomplete request */
		list_for_each_entry_safe(curr_req, temp_req,
			&curr_ep->queue, queue) {
			status = mv_u3d_process_ep_req(u3d, i, curr_req);
			if (status)
				break;
			/* write back status to req */
			curr_req->req.status = status;

			/* ep0 request completion */
			if (ep_num == 0) {
				mv_u3d_done(curr_ep, curr_req, 0);
				break;
			} else {
				mv_u3d_done(curr_ep, curr_req, status);
			}
		}

		dev_dbg(u3d->dev, "call mv_u3d_start_queue from ep complete\n");
		mv_u3d_start_queue(curr_ep);
	}
}

static irqreturn_t mv_u3d_irq(int irq, void *dev)
{
	struct mv_u3d *u3d = (struct mv_u3d *)dev;
	u32 status, intr;
	u32 bridgesetting;
	u32 trbunderrun;

	spin_lock(&u3d->lock);

	status = ioread32(&u3d->vuc_regs->intrcause);
	intr = ioread32(&u3d->vuc_regs->intrenable);
	status &= intr;

	if (status == 0) {
		spin_unlock(&u3d->lock);
		dev_err(u3d->dev, "irq error!\n");
		return IRQ_NONE;
	}

	if (status & MV_U3D_USBINT_VBUS_VALID) {
		bridgesetting = ioread32(&u3d->vuc_regs->bridgesetting);
		if (bridgesetting & MV_U3D_BRIDGE_SETTING_VBUS_VALID) {
			/* write vbus valid bit of bridge setting to clear */
			bridgesetting = MV_U3D_BRIDGE_SETTING_VBUS_VALID;
			iowrite32(bridgesetting, &u3d->vuc_regs->bridgesetting);
			dev_dbg(u3d->dev, "vbus valid\n");

			u3d->usb_state = USB_STATE_POWERED;
			u3d->vbus_valid_detect = 0;
			/* if external vbus detect is not supported,
			 * we handle it here.
			 */
			if (!u3d->vbus) {
				spin_unlock(&u3d->lock);
				mv_u3d_vbus_session(&u3d->gadget, 1);
				spin_lock(&u3d->lock);
			}
		} else
			dev_err(u3d->dev, "vbus bit is not set\n");
	}

	/* RX data is already in the 16KB FIFO.*/
	if (status & MV_U3D_USBINT_UNDER_RUN) {
		trbunderrun = ioread32(&u3d->vuc_regs->trbunderrun);
		dev_err(u3d->dev, "under run, ep%d\n", trbunderrun);
		iowrite32(trbunderrun, &u3d->vuc_regs->trbunderrun);
		mv_u3d_irq_process_error(u3d);
	}

	if (status & (MV_U3D_USBINT_RXDESC_ERR | MV_U3D_USBINT_TXDESC_ERR)) {
		/* write one to clear */
		iowrite32(status & (MV_U3D_USBINT_RXDESC_ERR
			| MV_U3D_USBINT_TXDESC_ERR),
			&u3d->vuc_regs->intrcause);
		dev_err(u3d->dev, "desc err 0x%x\n", status);
		mv_u3d_irq_process_error(u3d);
	}

	if (status & MV_U3D_USBINT_LINK_CHG)
		mv_u3d_irq_process_link_change(u3d);

	if (status & MV_U3D_USBINT_TX_COMPLETE)
		mv_u3d_irq_process_tr_complete(u3d);

	if (status & MV_U3D_USBINT_RX_COMPLETE)
		mv_u3d_irq_process_tr_complete(u3d);

	if (status & MV_U3D_USBINT_SETUP)
		mv_u3d_irq_process_setup(u3d);

	spin_unlock(&u3d->lock);
	return IRQ_HANDLED;
}

static int mv_u3d_remove(struct platform_device *dev)
{
	struct mv_u3d *u3d = platform_get_drvdata(dev);

	BUG_ON(u3d == NULL);

	usb_del_gadget_udc(&u3d->gadget);

	/* free memory allocated in probe */
	if (u3d->trb_pool)
		dma_pool_destroy(u3d->trb_pool);

	if (u3d->ep_context)
		dma_free_coherent(&dev->dev, u3d->ep_context_size,
			u3d->ep_context, u3d->ep_context_dma);

	kfree(u3d->eps);

	if (u3d->irq)
		free_irq(u3d->irq, u3d);

	if (u3d->cap_regs)
		iounmap(u3d->cap_regs);
	u3d->cap_regs = NULL;

	kfree(u3d->status_req);

	clk_put(u3d->clk);

	kfree(u3d);

	return 0;
}

static int mv_u3d_probe(struct platform_device *dev)
{
	struct mv_u3d *u3d = NULL;
	struct mv_usb_platform_data *pdata = dev_get_platdata(&dev->dev);
	int retval = 0;
	struct resource *r;
	size_t size;

	if (!dev_get_platdata(&dev->dev)) {
		dev_err(&dev->dev, "missing platform_data\n");
		retval = -ENODEV;
		goto err_pdata;
	}

	u3d = kzalloc(sizeof(*u3d), GFP_KERNEL);
	if (!u3d) {
		retval = -ENOMEM;
		goto err_alloc_private;
	}

	spin_lock_init(&u3d->lock);

	platform_set_drvdata(dev, u3d);

	u3d->dev = &dev->dev;
	u3d->vbus = pdata->vbus;

	u3d->clk = clk_get(&dev->dev, NULL);
	if (IS_ERR(u3d->clk)) {
		retval = PTR_ERR(u3d->clk);
		goto err_get_clk;
	}

	r = platform_get_resource_byname(dev, IORESOURCE_MEM, "capregs");
	if (!r) {
		dev_err(&dev->dev, "no I/O memory resource defined\n");
		retval = -ENODEV;
		goto err_get_cap_regs;
	}

	u3d->cap_regs = (struct mv_u3d_cap_regs __iomem *)
		ioremap(r->start, resource_size(r));
	if (!u3d->cap_regs) {
		dev_err(&dev->dev, "failed to map I/O memory\n");
		retval = -EBUSY;
		goto err_map_cap_regs;
	} else {
		dev_dbg(&dev->dev, "cap_regs address: 0x%lx/0x%lx\n",
			(unsigned long) r->start,
			(unsigned long) u3d->cap_regs);
	}

	/* we will access controller register, so enable the u3d controller */
	clk_enable(u3d->clk);

	if (pdata->phy_init) {
		retval = pdata->phy_init(u3d->phy_regs);
		if (retval) {
			dev_err(&dev->dev, "init phy error %d\n", retval);
			goto err_u3d_enable;
		}
	}

	u3d->op_regs = (struct mv_u3d_op_regs __iomem *)(u3d->cap_regs
		+ MV_U3D_USB3_OP_REGS_OFFSET);

	u3d->vuc_regs = (struct mv_u3d_vuc_regs __iomem *)(u3d->cap_regs
		+ ioread32(&u3d->cap_regs->vuoff));

	u3d->max_eps = 16;

	/*
	 * some platform will use usb to download image, it may not disconnect
	 * usb gadget before loading kernel. So first stop u3d here.
	 */
	mv_u3d_controller_stop(u3d);
	iowrite32(0xFFFFFFFF, &u3d->vuc_regs->intrcause);

	if (pdata->phy_deinit)
		pdata->phy_deinit(u3d->phy_regs);
	clk_disable(u3d->clk);

	size = u3d->max_eps * sizeof(struct mv_u3d_ep_context) * 2;
	size = (size + MV_U3D_EP_CONTEXT_ALIGNMENT - 1)
		& ~(MV_U3D_EP_CONTEXT_ALIGNMENT - 1);
	u3d->ep_context = dma_alloc_coherent(&dev->dev, size,
					&u3d->ep_context_dma, GFP_KERNEL);
	if (!u3d->ep_context) {
		dev_err(&dev->dev, "allocate ep context memory failed\n");
		retval = -ENOMEM;
		goto err_alloc_ep_context;
	}
	u3d->ep_context_size = size;

	/* create TRB dma_pool resource */
	u3d->trb_pool = dma_pool_create("u3d_trb",
			&dev->dev,
			sizeof(struct mv_u3d_trb_hw),
			MV_U3D_TRB_ALIGNMENT,
			MV_U3D_DMA_BOUNDARY);

	if (!u3d->trb_pool) {
		retval = -ENOMEM;
		goto err_alloc_trb_pool;
	}

	size = u3d->max_eps * sizeof(struct mv_u3d_ep) * 2;
	u3d->eps = kzalloc(size, GFP_KERNEL);
	if (!u3d->eps) {
		retval = -ENOMEM;
		goto err_alloc_eps;
	}

	/* initialize ep0 status request structure */
	u3d->status_req = kzalloc(sizeof(struct mv_u3d_req) + 8, GFP_KERNEL);
	if (!u3d->status_req) {
		retval = -ENOMEM;
		goto err_alloc_status_req;
	}
	INIT_LIST_HEAD(&u3d->status_req->queue);

	/* allocate a small amount of memory to get valid address */
	u3d->status_req->req.buf = (char *)u3d->status_req
					+ sizeof(struct mv_u3d_req);
	u3d->status_req->req.dma = virt_to_phys(u3d->status_req->req.buf);

	u3d->resume_state = USB_STATE_NOTATTACHED;
	u3d->usb_state = USB_STATE_ATTACHED;
	u3d->ep0_dir = MV_U3D_EP_DIR_OUT;
	u3d->remote_wakeup = 0;

	r = platform_get_resource(dev, IORESOURCE_IRQ, 0);
	if (!r) {
		dev_err(&dev->dev, "no IRQ resource defined\n");
		retval = -ENODEV;
		goto err_get_irq;
	}
	u3d->irq = r->start;
	if (request_irq(u3d->irq, mv_u3d_irq,
		IRQF_SHARED, driver_name, u3d)) {
		u3d->irq = 0;
		dev_err(&dev->dev, "Request irq %d for u3d failed\n",
			u3d->irq);
		retval = -ENODEV;
		goto err_request_irq;
	}

	/* initialize gadget structure */
	u3d->gadget.ops = &mv_u3d_ops;	/* usb_gadget_ops */
	u3d->gadget.ep0 = &u3d->eps[1].ep;	/* gadget ep0 */
	INIT_LIST_HEAD(&u3d->gadget.ep_list);	/* ep_list */
	u3d->gadget.speed = USB_SPEED_UNKNOWN;	/* speed */

	/* the "gadget" abstracts/virtualizes the controller */
	u3d->gadget.name = driver_name;		/* gadget name */

	mv_u3d_eps_init(u3d);

	/* external vbus detection */
	if (u3d->vbus) {
		u3d->clock_gating = 1;
		dev_err(&dev->dev, "external vbus detection\n");
	}

	if (!u3d->clock_gating)
		u3d->vbus_active = 1;

	/* enable usb3 controller vbus detection */
	u3d->vbus_valid_detect = 1;

	retval = usb_add_gadget_udc(&dev->dev, &u3d->gadget);
	if (retval)
		goto err_unregister;

	dev_dbg(&dev->dev, "successful probe usb3 device %s clock gating.\n",
		u3d->clock_gating ? "with" : "without");

	return 0;

err_unregister:
	free_irq(u3d->irq, u3d);
err_request_irq:
err_get_irq:
	kfree(u3d->status_req);
err_alloc_status_req:
	kfree(u3d->eps);
err_alloc_eps:
	dma_pool_destroy(u3d->trb_pool);
err_alloc_trb_pool:
	dma_free_coherent(&dev->dev, u3d->ep_context_size,
		u3d->ep_context, u3d->ep_context_dma);
err_alloc_ep_context:
	if (pdata->phy_deinit)
		pdata->phy_deinit(u3d->phy_regs);
	clk_disable(u3d->clk);
err_u3d_enable:
	iounmap(u3d->cap_regs);
err_map_cap_regs:
err_get_cap_regs:
err_get_clk:
	clk_put(u3d->clk);
	kfree(u3d);
err_alloc_private:
err_pdata:
	return retval;
}

#ifdef CONFIG_PM_SLEEP
static int mv_u3d_suspend(struct device *dev)
{
	struct mv_u3d *u3d = dev_get_drvdata(dev);

	/*
	 * only cable is unplugged, usb can suspend.
	 * So do not care about clock_gating == 1, it is handled by
	 * vbus session.
	 */
	if (!u3d->clock_gating) {
		mv_u3d_controller_stop(u3d);

		spin_lock_irq(&u3d->lock);
		/* stop all usb activities */
		mv_u3d_stop_activity(u3d, u3d->driver);
		spin_unlock_irq(&u3d->lock);

		mv_u3d_disable(u3d);
	}

	return 0;
}

static int mv_u3d_resume(struct device *dev)
{
	struct mv_u3d *u3d = dev_get_drvdata(dev);
	int retval;

	if (!u3d->clock_gating) {
		retval = mv_u3d_enable(u3d);
		if (retval)
			return retval;

		if (u3d->driver && u3d->softconnect) {
			mv_u3d_controller_reset(u3d);
			mv_u3d_ep0_reset(u3d);
			mv_u3d_controller_start(u3d);
		}
	}

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(mv_u3d_pm_ops, mv_u3d_suspend, mv_u3d_resume);

static void mv_u3d_shutdown(struct platform_device *dev)
{
	struct mv_u3d *u3d = platform_get_drvdata(dev);
	u32 tmp;

	tmp = ioread32(&u3d->op_regs->usbcmd);
	tmp &= ~MV_U3D_CMD_RUN_STOP;
	iowrite32(tmp, &u3d->op_regs->usbcmd);
}

static struct platform_driver mv_u3d_driver = {
	.probe		= mv_u3d_probe,
	.remove		= mv_u3d_remove,
	.shutdown	= mv_u3d_shutdown,
	.driver		= {
		.name	= "mv-u3d",
		.pm	= &mv_u3d_pm_ops,
	},
};

module_platform_driver(mv_u3d_driver);
MODULE_ALIAS("platform:mv-u3d");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR("Yu Xu <yuxu@marvell.com>");
MODULE_LICENSE("GPL");
