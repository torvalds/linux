// SPDX-License-Identifier: GPL-2.0+
/*
 * aspeed-vhub -- Driver for Aspeed SoC "vHub" USB gadget
 *
 * epn.c - Generic endpoints management
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

#include "vhub.h"

#define EXTRA_CHECKS

#ifdef EXTRA_CHECKS
#define CHECK(ep, expr, fmt...)					\
	do {							\
		if (!(expr)) EPDBG(ep, "CHECK:" fmt);		\
	} while(0)
#else
#define CHECK(ep, expr, fmt...)	do { } while(0)
#endif

static void ast_vhub_epn_kick(struct ast_vhub_ep *ep, struct ast_vhub_req *req)
{
	unsigned int act = req->req.actual;
	unsigned int len = req->req.length;
	unsigned int chunk;

	/* There should be no DMA ongoing */
	WARN_ON(req->active);

	/* Calculate next chunk size */
	chunk = len - act;
	if (chunk > ep->ep.maxpacket)
		chunk = ep->ep.maxpacket;
	else if ((chunk < ep->ep.maxpacket) || !req->req.zero)
		req->last_desc = 1;

	EPVDBG(ep, "kick req %p act=%d/%d chunk=%d last=%d\n",
	       req, act, len, chunk, req->last_desc);

	/* If DMA unavailable, using staging EP buffer */
	if (!req->req.dma) {

		/* For IN transfers, copy data over first */
		if (ep->epn.is_in) {
			memcpy(ep->buf, req->req.buf + act, chunk);
			vhub_dma_workaround(ep->buf);
		}
		writel(ep->buf_dma, ep->epn.regs + AST_VHUB_EP_DESC_BASE);
	} else {
		if (ep->epn.is_in)
			vhub_dma_workaround(req->req.buf);
		writel(req->req.dma + act, ep->epn.regs + AST_VHUB_EP_DESC_BASE);
	}

	/* Start DMA */
	req->active = true;
	writel(VHUB_EP_DMA_SET_TX_SIZE(chunk),
	       ep->epn.regs + AST_VHUB_EP_DESC_STATUS);
	writel(VHUB_EP_DMA_SET_TX_SIZE(chunk) | VHUB_EP_DMA_SINGLE_KICK,
	       ep->epn.regs + AST_VHUB_EP_DESC_STATUS);
}

static void ast_vhub_epn_handle_ack(struct ast_vhub_ep *ep)
{
	struct ast_vhub_req *req;
	unsigned int len;
	u32 stat;

	/* Read EP status */
	stat = readl(ep->epn.regs + AST_VHUB_EP_DESC_STATUS);

	/* Grab current request if any */
	req = list_first_entry_or_null(&ep->queue, struct ast_vhub_req, queue);

	EPVDBG(ep, "ACK status=%08x is_in=%d, req=%p (active=%d)\n",
	       stat, ep->epn.is_in, req, req ? req->active : 0);

	/* In absence of a request, bail out, must have been dequeued */
	if (!req)
		return;

	/*
	 * Request not active, move on to processing queue, active request
	 * was probably dequeued
	 */
	if (!req->active)
		goto next_chunk;

	/* Check if HW has moved on */
	if (VHUB_EP_DMA_RPTR(stat) != 0) {
		EPDBG(ep, "DMA read pointer not 0 !\n");
		return;
	}

	/* No current DMA ongoing */
	req->active = false;

	/* Grab length out of HW */
	len = VHUB_EP_DMA_TX_SIZE(stat);

	/* If not using DMA, copy data out if needed */
	if (!req->req.dma && !ep->epn.is_in && len)
		memcpy(req->req.buf + req->req.actual, ep->buf, len);

	/* Adjust size */
	req->req.actual += len;

	/* Check for short packet */
	if (len < ep->ep.maxpacket)
		req->last_desc = 1;

	/* That's it ? complete the request and pick a new one */
	if (req->last_desc >= 0) {
		ast_vhub_done(ep, req, 0);
		req = list_first_entry_or_null(&ep->queue, struct ast_vhub_req,
					       queue);

		/*
		 * Due to lock dropping inside "done" the next request could
		 * already be active, so check for that and bail if needed.
		 */
		if (!req || req->active)
			return;
	}

 next_chunk:
	ast_vhub_epn_kick(ep, req);
}

static inline unsigned int ast_vhub_count_free_descs(struct ast_vhub_ep *ep)
{
	/*
	 * d_next == d_last means descriptor list empty to HW,
	 * thus we can only have AST_VHUB_DESCS_COUNT-1 descriptors
	 * in the list
	 */
	return (ep->epn.d_last + AST_VHUB_DESCS_COUNT - ep->epn.d_next - 1) &
		(AST_VHUB_DESCS_COUNT - 1);
}

static void ast_vhub_epn_kick_desc(struct ast_vhub_ep *ep,
				   struct ast_vhub_req *req)
{
	struct ast_vhub_desc *desc = NULL;
	unsigned int act = req->act_count;
	unsigned int len = req->req.length;
	unsigned int chunk;

	/* Mark request active if not already */
	req->active = true;

	/* If the request was already completely written, do nothing */
	if (req->last_desc >= 0)
		return;

	EPVDBG(ep, "kick act=%d/%d chunk_max=%d free_descs=%d\n",
	       act, len, ep->epn.chunk_max, ast_vhub_count_free_descs(ep));

	/* While we can create descriptors */
	while (ast_vhub_count_free_descs(ep) && req->last_desc < 0) {
		unsigned int d_num;

		/* Grab next free descriptor */
		d_num = ep->epn.d_next;
		desc = &ep->epn.descs[d_num];
		ep->epn.d_next = (d_num + 1) & (AST_VHUB_DESCS_COUNT - 1);

		/* Calculate next chunk size */
		chunk = len - act;
		if (chunk <= ep->epn.chunk_max) {
			/*
			 * Is this the last packet ? Because of having up to 8
			 * packets in a descriptor we can't just compare "chunk"
			 * with ep.maxpacket. We have to see if it's a multiple
			 * of it to know if we have to send a zero packet.
			 * Sadly that involves a modulo which is a bit expensive
			 * but probably still better than not doing it.
			 */
			if (!chunk || !req->req.zero || (chunk % ep->ep.maxpacket) != 0)
				req->last_desc = d_num;
		} else {
			chunk = ep->epn.chunk_max;
		}

		EPVDBG(ep, " chunk: act=%d/%d chunk=%d last=%d desc=%d free=%d\n",
		       act, len, chunk, req->last_desc, d_num,
		       ast_vhub_count_free_descs(ep));

		/* Populate descriptor */
		desc->w0 = cpu_to_le32(req->req.dma + act);

		/* Interrupt if end of request or no more descriptors */

		/*
		 * TODO: Be smarter about it, if we don't have enough
		 * descriptors request an interrupt before queue empty
		 * or so in order to be able to populate more before
		 * the HW runs out. This isn't a problem at the moment
		 * as we use 256 descriptors and only put at most one
		 * request in the ring.
		 */
		desc->w1 = cpu_to_le32(VHUB_DSC1_IN_SET_LEN(chunk));
		if (req->last_desc >= 0 || !ast_vhub_count_free_descs(ep))
			desc->w1 |= cpu_to_le32(VHUB_DSC1_IN_INTERRUPT);

		/* Account packet */
		req->act_count = act = act + chunk;
	}

	if (likely(desc))
		vhub_dma_workaround(desc);

	/* Tell HW about new descriptors */
	writel(VHUB_EP_DMA_SET_CPU_WPTR(ep->epn.d_next),
	       ep->epn.regs + AST_VHUB_EP_DESC_STATUS);

	EPVDBG(ep, "HW kicked, d_next=%d dstat=%08x\n",
	       ep->epn.d_next, readl(ep->epn.regs + AST_VHUB_EP_DESC_STATUS));
}

static void ast_vhub_epn_handle_ack_desc(struct ast_vhub_ep *ep)
{
	struct ast_vhub_req *req;
	unsigned int len, d_last;
	u32 stat, stat1;

	/* Read EP status, workaround HW race */
	do {
		stat = readl(ep->epn.regs + AST_VHUB_EP_DESC_STATUS);
		stat1 = readl(ep->epn.regs + AST_VHUB_EP_DESC_STATUS);
	} while(stat != stat1);

	/* Extract RPTR */
	d_last = VHUB_EP_DMA_RPTR(stat);

	/* Grab current request if any */
	req = list_first_entry_or_null(&ep->queue, struct ast_vhub_req, queue);

	EPVDBG(ep, "ACK status=%08x is_in=%d ep->d_last=%d..%d\n",
	       stat, ep->epn.is_in, ep->epn.d_last, d_last);

	/* Check all completed descriptors */
	while (ep->epn.d_last != d_last) {
		struct ast_vhub_desc *desc;
		unsigned int d_num;
		bool is_last_desc;

		/* Grab next completed descriptor */
		d_num = ep->epn.d_last;
		desc = &ep->epn.descs[d_num];
		ep->epn.d_last = (d_num + 1) & (AST_VHUB_DESCS_COUNT - 1);

		/* Grab len out of descriptor */
		len = VHUB_DSC1_IN_LEN(le32_to_cpu(desc->w1));

		EPVDBG(ep, " desc %d len=%d req=%p (act=%d)\n",
		       d_num, len, req, req ? req->active : 0);

		/* If no active request pending, move on */
		if (!req || !req->active)
			continue;

		/* Adjust size */
		req->req.actual += len;

		/* Is that the last chunk ? */
		is_last_desc = req->last_desc == d_num;
		CHECK(ep, is_last_desc == (len < ep->ep.maxpacket ||
					   (req->req.actual >= req->req.length &&
					    !req->req.zero)),
		      "Last packet discrepancy: last_desc=%d len=%d r.act=%d "
		      "r.len=%d r.zero=%d mp=%d\n",
		      is_last_desc, len, req->req.actual, req->req.length,
		      req->req.zero, ep->ep.maxpacket);

		if (is_last_desc) {
			/*
			 * Because we can only have one request at a time
			 * in our descriptor list in this implementation,
			 * d_last and ep->d_last should now be equal
			 */
			CHECK(ep, d_last == ep->epn.d_last,
			      "DMA read ptr mismatch %d vs %d\n",
			      d_last, ep->epn.d_last);

			/* Note: done will drop and re-acquire the lock */
			ast_vhub_done(ep, req, 0);
			req = list_first_entry_or_null(&ep->queue,
						       struct ast_vhub_req,
						       queue);
			break;
		}
	}

	/* More work ? */
	if (req)
		ast_vhub_epn_kick_desc(ep, req);
}

void ast_vhub_epn_ack_irq(struct ast_vhub_ep *ep)
{
	if (ep->epn.desc_mode)
		ast_vhub_epn_handle_ack_desc(ep);
	else
		ast_vhub_epn_handle_ack(ep);
}

static int ast_vhub_epn_queue(struct usb_ep* u_ep, struct usb_request *u_req,
			      gfp_t gfp_flags)
{
	struct ast_vhub_req *req = to_ast_req(u_req);
	struct ast_vhub_ep *ep = to_ast_ep(u_ep);
	struct ast_vhub *vhub = ep->vhub;
	unsigned long flags;
	bool empty;
	int rc;

	/* Paranoid checks */
	if (!u_req || !u_req->complete || !u_req->buf) {
		dev_warn(&vhub->pdev->dev, "Bogus EPn request ! u_req=%p\n", u_req);
		if (u_req) {
			dev_warn(&vhub->pdev->dev, "complete=%p internal=%d\n",
				 u_req->complete, req->internal);
		}
		return -EINVAL;
	}

	/* Endpoint enabled ? */
	if (!ep->epn.enabled || !u_ep->desc || !ep->dev || !ep->d_idx ||
	    !ep->dev->enabled) {
		EPDBG(ep, "Enqueuing request on wrong or disabled EP\n");
		return -ESHUTDOWN;
	}

	/* Map request for DMA if possible. For now, the rule for DMA is
	 * that:
	 *
	 *  * For single stage mode (no descriptors):
	 *
	 *   - The buffer is aligned to a 8 bytes boundary (HW requirement)
	 *   - For a OUT endpoint, the request size is a multiple of the EP
	 *     packet size (otherwise the controller will DMA past the end
	 *     of the buffer if the host is sending a too long packet).
	 *
	 *  * For descriptor mode (tx only for now), always.
	 *
	 * We could relax the latter by making the decision to use the bounce
	 * buffer based on the size of a given *segment* of the request rather
	 * than the whole request.
	 */
	if (ep->epn.desc_mode ||
	    ((((unsigned long)u_req->buf & 7) == 0) &&
	     (ep->epn.is_in || !(u_req->length & (u_ep->maxpacket - 1))))) {
		rc = usb_gadget_map_request_by_dev(&vhub->pdev->dev, u_req,
					    ep->epn.is_in);
		if (rc) {
			dev_warn(&vhub->pdev->dev,
				 "Request mapping failure %d\n", rc);
			return rc;
		}
	} else
		u_req->dma = 0;

	EPVDBG(ep, "enqueue req @%p\n", req);
	EPVDBG(ep, " l=%d dma=0x%x zero=%d noshort=%d noirq=%d is_in=%d\n",
	       u_req->length, (u32)u_req->dma, u_req->zero,
	       u_req->short_not_ok, u_req->no_interrupt,
	       ep->epn.is_in);

	/* Initialize request progress fields */
	u_req->status = -EINPROGRESS;
	u_req->actual = 0;
	req->act_count = 0;
	req->active = false;
	req->last_desc = -1;
	spin_lock_irqsave(&vhub->lock, flags);
	empty = list_empty(&ep->queue);

	/* Add request to list and kick processing if empty */
	list_add_tail(&req->queue, &ep->queue);
	if (empty) {
		if (ep->epn.desc_mode)
			ast_vhub_epn_kick_desc(ep, req);
		else
			ast_vhub_epn_kick(ep, req);
	}
	spin_unlock_irqrestore(&vhub->lock, flags);

	return 0;
}

static void ast_vhub_stop_active_req(struct ast_vhub_ep *ep,
				     bool restart_ep)
{
	u32 state, reg, loops;

	/* Stop DMA activity */
	if (ep->epn.desc_mode)
		writel(VHUB_EP_DMA_CTRL_RESET, ep->epn.regs + AST_VHUB_EP_DMA_CTLSTAT);
	else
		writel(0, ep->epn.regs + AST_VHUB_EP_DMA_CTLSTAT);

	/* Wait for it to complete */
	for (loops = 0; loops < 1000; loops++) {
		state = readl(ep->epn.regs + AST_VHUB_EP_DMA_CTLSTAT);
		state = VHUB_EP_DMA_PROC_STATUS(state);
		if (state == EP_DMA_PROC_RX_IDLE ||
		    state == EP_DMA_PROC_TX_IDLE)
			break;
		udelay(1);
	}
	if (loops >= 1000)
		dev_warn(&ep->vhub->pdev->dev, "Timeout waiting for DMA\n");

	/* If we don't have to restart the endpoint, that's it */
	if (!restart_ep)
		return;

	/* Restart the endpoint */
	if (ep->epn.desc_mode) {
		/*
		 * Take out descriptors by resetting the DMA read
		 * pointer to be equal to the CPU write pointer.
		 *
		 * Note: If we ever support creating descriptors for
		 * requests that aren't the head of the queue, we
		 * may have to do something more complex here,
		 * especially if the request being taken out is
		 * not the current head descriptors.
		 */
		reg = VHUB_EP_DMA_SET_RPTR(ep->epn.d_next) |
			VHUB_EP_DMA_SET_CPU_WPTR(ep->epn.d_next);
		writel(reg, ep->epn.regs + AST_VHUB_EP_DESC_STATUS);

		/* Then turn it back on */
		writel(ep->epn.dma_conf,
		       ep->epn.regs + AST_VHUB_EP_DMA_CTLSTAT);
	} else {
		/* Single mode: just turn it back on */
		writel(ep->epn.dma_conf,
		       ep->epn.regs + AST_VHUB_EP_DMA_CTLSTAT);
	}
}

static int ast_vhub_epn_dequeue(struct usb_ep* u_ep, struct usb_request *u_req)
{
	struct ast_vhub_ep *ep = to_ast_ep(u_ep);
	struct ast_vhub *vhub = ep->vhub;
	struct ast_vhub_req *req;
	unsigned long flags;
	int rc = -EINVAL;

	spin_lock_irqsave(&vhub->lock, flags);

	/* Make sure it's actually queued on this endpoint */
	list_for_each_entry (req, &ep->queue, queue) {
		if (&req->req == u_req)
			break;
	}

	if (&req->req == u_req) {
		EPVDBG(ep, "dequeue req @%p active=%d\n",
		       req, req->active);
		if (req->active)
			ast_vhub_stop_active_req(ep, true);
		ast_vhub_done(ep, req, -ECONNRESET);
		rc = 0;
	}

	spin_unlock_irqrestore(&vhub->lock, flags);
	return rc;
}

void ast_vhub_update_epn_stall(struct ast_vhub_ep *ep)
{
	u32 reg;

	if (WARN_ON(ep->d_idx == 0))
		return;
	reg = readl(ep->epn.regs + AST_VHUB_EP_CONFIG);
	if (ep->epn.stalled || ep->epn.wedged)
		reg |= VHUB_EP_CFG_STALL_CTRL;
	else
		reg &= ~VHUB_EP_CFG_STALL_CTRL;
	writel(reg, ep->epn.regs + AST_VHUB_EP_CONFIG);

	if (!ep->epn.stalled && !ep->epn.wedged)
		writel(VHUB_EP_TOGGLE_SET_EPNUM(ep->epn.g_idx),
		       ep->vhub->regs + AST_VHUB_EP_TOGGLE);
}

static int ast_vhub_set_halt_and_wedge(struct usb_ep* u_ep, bool halt,
				      bool wedge)
{
	struct ast_vhub_ep *ep = to_ast_ep(u_ep);
	struct ast_vhub *vhub = ep->vhub;
	unsigned long flags;

	EPDBG(ep, "Set halt (%d) & wedge (%d)\n", halt, wedge);

	if (!u_ep || !u_ep->desc)
		return -EINVAL;
	if (ep->d_idx == 0)
		return 0;
	if (ep->epn.is_iso)
		return -EOPNOTSUPP;

	spin_lock_irqsave(&vhub->lock, flags);

	/* Fail with still-busy IN endpoints */
	if (halt && ep->epn.is_in && !list_empty(&ep->queue)) {
		spin_unlock_irqrestore(&vhub->lock, flags);
		return -EAGAIN;
	}
	ep->epn.stalled = halt;
	ep->epn.wedged = wedge;
	ast_vhub_update_epn_stall(ep);

	spin_unlock_irqrestore(&vhub->lock, flags);

	return 0;
}

static int ast_vhub_epn_set_halt(struct usb_ep *u_ep, int value)
{
	return ast_vhub_set_halt_and_wedge(u_ep, value != 0, false);
}

static int ast_vhub_epn_set_wedge(struct usb_ep *u_ep)
{
	return ast_vhub_set_halt_and_wedge(u_ep, true, true);
}

static int ast_vhub_epn_disable(struct usb_ep* u_ep)
{
	struct ast_vhub_ep *ep = to_ast_ep(u_ep);
	struct ast_vhub *vhub = ep->vhub;
	unsigned long flags;
	u32 imask, ep_ier;

	EPDBG(ep, "Disabling !\n");

	spin_lock_irqsave(&vhub->lock, flags);

	ep->epn.enabled = false;

	/* Stop active DMA if any */
	ast_vhub_stop_active_req(ep, false);

	/* Disable endpoint */
	writel(0, ep->epn.regs + AST_VHUB_EP_CONFIG);

	/* Disable ACK interrupt */
	imask = VHUB_EP_IRQ(ep->epn.g_idx);
	ep_ier = readl(vhub->regs + AST_VHUB_EP_ACK_IER);
	ep_ier &= ~imask;
	writel(ep_ier, vhub->regs + AST_VHUB_EP_ACK_IER);
	writel(imask, vhub->regs + AST_VHUB_EP_ACK_ISR);

	/* Nuke all pending requests */
	ast_vhub_nuke(ep, -ESHUTDOWN);

	/* No more descriptor associated with request */
	ep->ep.desc = NULL;

	spin_unlock_irqrestore(&vhub->lock, flags);

	return 0;
}

static int ast_vhub_epn_enable(struct usb_ep* u_ep,
			       const struct usb_endpoint_descriptor *desc)
{
	struct ast_vhub_ep *ep = to_ast_ep(u_ep);
	struct ast_vhub_dev *dev;
	struct ast_vhub *vhub;
	u16 maxpacket, type;
	unsigned long flags;
	u32 ep_conf, ep_ier, imask;

	/* Check arguments */
	if (!u_ep || !desc)
		return -EINVAL;

	maxpacket = usb_endpoint_maxp(desc);
	if (!ep->d_idx || !ep->dev ||
	    desc->bDescriptorType != USB_DT_ENDPOINT ||
	    maxpacket == 0 || maxpacket > ep->ep.maxpacket) {
		EPDBG(ep, "Invalid EP enable,d_idx=%d,dev=%p,type=%d,mp=%d/%d\n",
		      ep->d_idx, ep->dev, desc->bDescriptorType,
		      maxpacket, ep->ep.maxpacket);
		return -EINVAL;
	}
	if (ep->d_idx != usb_endpoint_num(desc)) {
		EPDBG(ep, "EP number mismatch !\n");
		return -EINVAL;
	}

	if (ep->epn.enabled) {
		EPDBG(ep, "Already enabled\n");
		return -EBUSY;
	}
	dev = ep->dev;
	vhub = ep->vhub;

	/* Check device state */
	if (!dev->driver) {
		EPDBG(ep, "Bogus device state: driver=%p speed=%d\n",
		       dev->driver, dev->gadget.speed);
		return -ESHUTDOWN;
	}

	/* Grab some info from the descriptor */
	ep->epn.is_in = usb_endpoint_dir_in(desc);
	ep->ep.maxpacket = maxpacket;
	type = usb_endpoint_type(desc);
	ep->epn.d_next = ep->epn.d_last = 0;
	ep->epn.is_iso = false;
	ep->epn.stalled = false;
	ep->epn.wedged = false;

	EPDBG(ep, "Enabling [%s] %s num %d maxpacket=%d\n",
	      ep->epn.is_in ? "in" : "out", usb_ep_type_string(type),
	      usb_endpoint_num(desc), maxpacket);

	/* Can we use DMA descriptor mode ? */
	ep->epn.desc_mode = ep->epn.descs && ep->epn.is_in;
	if (ep->epn.desc_mode)
		memset(ep->epn.descs, 0, 8 * AST_VHUB_DESCS_COUNT);

	/*
	 * Large send function can send up to 8 packets from
	 * one descriptor with a limit of 4095 bytes.
	 */
	ep->epn.chunk_max = ep->ep.maxpacket;
	if (ep->epn.is_in) {
		ep->epn.chunk_max <<= 3;
		while (ep->epn.chunk_max > 4095)
			ep->epn.chunk_max -= ep->ep.maxpacket;
	}

	switch(type) {
	case USB_ENDPOINT_XFER_CONTROL:
		EPDBG(ep, "Only one control endpoint\n");
		return -EINVAL;
	case USB_ENDPOINT_XFER_INT:
		ep_conf = VHUB_EP_CFG_SET_TYPE(EP_TYPE_INT);
		break;
	case USB_ENDPOINT_XFER_BULK:
		ep_conf = VHUB_EP_CFG_SET_TYPE(EP_TYPE_BULK);
		break;
	case USB_ENDPOINT_XFER_ISOC:
		ep_conf = VHUB_EP_CFG_SET_TYPE(EP_TYPE_ISO);
		ep->epn.is_iso = true;
		break;
	default:
		return -EINVAL;
	}

	/* Encode the rest of the EP config register */
	if (maxpacket < 1024)
		ep_conf |= VHUB_EP_CFG_SET_MAX_PKT(maxpacket);
	if (!ep->epn.is_in)
		ep_conf |= VHUB_EP_CFG_DIR_OUT;
	ep_conf |= VHUB_EP_CFG_SET_EP_NUM(usb_endpoint_num(desc));
	ep_conf |= VHUB_EP_CFG_ENABLE;
	ep_conf |= VHUB_EP_CFG_SET_DEV(dev->index + 1);
	EPVDBG(ep, "config=%08x\n", ep_conf);

	spin_lock_irqsave(&vhub->lock, flags);

	/* Disable HW and reset DMA */
	writel(0, ep->epn.regs + AST_VHUB_EP_CONFIG);
	writel(VHUB_EP_DMA_CTRL_RESET,
	       ep->epn.regs + AST_VHUB_EP_DMA_CTLSTAT);

	/* Configure and enable */
	writel(ep_conf, ep->epn.regs + AST_VHUB_EP_CONFIG);

	if (ep->epn.desc_mode) {
		/* Clear DMA status, including the DMA read ptr */
		writel(0, ep->epn.regs + AST_VHUB_EP_DESC_STATUS);

		/* Set descriptor base */
		writel(ep->epn.descs_dma,
		       ep->epn.regs + AST_VHUB_EP_DESC_BASE);

		/* Set base DMA config value */
		ep->epn.dma_conf = VHUB_EP_DMA_DESC_MODE;
		if (ep->epn.is_in)
			ep->epn.dma_conf |= VHUB_EP_DMA_IN_LONG_MODE;

		/* First reset and disable all operations */
		writel(ep->epn.dma_conf | VHUB_EP_DMA_CTRL_RESET,
		       ep->epn.regs + AST_VHUB_EP_DMA_CTLSTAT);

		/* Enable descriptor mode */
		writel(ep->epn.dma_conf,
		       ep->epn.regs + AST_VHUB_EP_DMA_CTLSTAT);
	} else {
		/* Set base DMA config value */
		ep->epn.dma_conf = VHUB_EP_DMA_SINGLE_STAGE;

		/* Reset and switch to single stage mode */
		writel(ep->epn.dma_conf | VHUB_EP_DMA_CTRL_RESET,
		       ep->epn.regs + AST_VHUB_EP_DMA_CTLSTAT);
		writel(ep->epn.dma_conf,
		       ep->epn.regs + AST_VHUB_EP_DMA_CTLSTAT);
		writel(0, ep->epn.regs + AST_VHUB_EP_DESC_STATUS);
	}

	/* Cleanup data toggle just in case */
	writel(VHUB_EP_TOGGLE_SET_EPNUM(ep->epn.g_idx),
	       vhub->regs + AST_VHUB_EP_TOGGLE);

	/* Cleanup and enable ACK interrupt */
	imask = VHUB_EP_IRQ(ep->epn.g_idx);
	writel(imask, vhub->regs + AST_VHUB_EP_ACK_ISR);
	ep_ier = readl(vhub->regs + AST_VHUB_EP_ACK_IER);
	ep_ier |= imask;
	writel(ep_ier, vhub->regs + AST_VHUB_EP_ACK_IER);

	/* Woot, we are online ! */
	ep->epn.enabled = true;

	spin_unlock_irqrestore(&vhub->lock, flags);

	return 0;
}

static void ast_vhub_epn_dispose(struct usb_ep *u_ep)
{
	struct ast_vhub_ep *ep = to_ast_ep(u_ep);

	if (WARN_ON(!ep->dev || !ep->d_idx))
		return;

	EPDBG(ep, "Releasing endpoint\n");

	/* Take it out of the EP list */
	list_del_init(&ep->ep.ep_list);

	/* Mark the address free in the device */
	ep->dev->epns[ep->d_idx - 1] = NULL;

	/* Free name & DMA buffers */
	kfree(ep->ep.name);
	ep->ep.name = NULL;
	dma_free_coherent(&ep->vhub->pdev->dev,
			  AST_VHUB_EPn_MAX_PACKET +
			  8 * AST_VHUB_DESCS_COUNT,
			  ep->buf, ep->buf_dma);
	ep->buf = NULL;
	ep->epn.descs = NULL;

	/* Mark free */
	ep->dev = NULL;
}

static const struct usb_ep_ops ast_vhub_epn_ops = {
	.enable		= ast_vhub_epn_enable,
	.disable	= ast_vhub_epn_disable,
	.dispose	= ast_vhub_epn_dispose,
	.queue		= ast_vhub_epn_queue,
	.dequeue	= ast_vhub_epn_dequeue,
	.set_halt	= ast_vhub_epn_set_halt,
	.set_wedge	= ast_vhub_epn_set_wedge,
	.alloc_request	= ast_vhub_alloc_request,
	.free_request	= ast_vhub_free_request,
};

struct ast_vhub_ep *ast_vhub_alloc_epn(struct ast_vhub_dev *d, u8 addr)
{
	struct ast_vhub *vhub = d->vhub;
	struct ast_vhub_ep *ep;
	unsigned long flags;
	int i;

	/* Find a free one (no device) */
	spin_lock_irqsave(&vhub->lock, flags);
	for (i = 0; i < vhub->max_epns; i++)
		if (vhub->epns[i].dev == NULL)
			break;
	if (i >= vhub->max_epns) {
		spin_unlock_irqrestore(&vhub->lock, flags);
		return NULL;
	}

	/* Set it up */
	ep = &vhub->epns[i];
	ep->dev = d;
	spin_unlock_irqrestore(&vhub->lock, flags);

	DDBG(d, "Allocating gen EP %d for addr %d\n", i, addr);
	INIT_LIST_HEAD(&ep->queue);
	ep->d_idx = addr;
	ep->vhub = vhub;
	ep->ep.ops = &ast_vhub_epn_ops;
	ep->ep.name = kasprintf(GFP_KERNEL, "ep%d", addr);
	d->epns[addr-1] = ep;
	ep->epn.g_idx = i;
	ep->epn.regs = vhub->regs + 0x200 + (i * 0x10);

	ep->buf = dma_alloc_coherent(&vhub->pdev->dev,
				     AST_VHUB_EPn_MAX_PACKET +
				     8 * AST_VHUB_DESCS_COUNT,
				     &ep->buf_dma, GFP_KERNEL);
	if (!ep->buf) {
		kfree(ep->ep.name);
		ep->ep.name = NULL;
		return NULL;
	}
	ep->epn.descs = ep->buf + AST_VHUB_EPn_MAX_PACKET;
	ep->epn.descs_dma = ep->buf_dma + AST_VHUB_EPn_MAX_PACKET;

	usb_ep_set_maxpacket_limit(&ep->ep, AST_VHUB_EPn_MAX_PACKET);
	list_add_tail(&ep->ep.ep_list, &d->gadget.ep_list);
	ep->ep.caps.type_iso = true;
	ep->ep.caps.type_bulk = true;
	ep->ep.caps.type_int = true;
	ep->ep.caps.dir_in = true;
	ep->ep.caps.dir_out = true;

	return ep;
}
