// SPDX-License-Identifier: GPL-2.0+
/*
 * aspeed-vhub -- Driver for Aspeed SoC "vHub" USB gadget
 *
 * ep0.c - Endpoint 0 handling
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

#include "vhub.h"

int ast_vhub_reply(struct ast_vhub_ep *ep, char *ptr, int len)
{
	struct usb_request *req = &ep->ep0.req.req;
	int rc;

	if (WARN_ON(ep->d_idx != 0))
		return std_req_stall;
	if (WARN_ON(!ep->ep0.dir_in))
		return std_req_stall;
	if (WARN_ON(len > AST_VHUB_EP0_MAX_PACKET))
		return std_req_stall;
	if (WARN_ON(req->status == -EINPROGRESS))
		return std_req_stall;

	req->buf = ptr;
	req->length = len;
	req->complete = NULL;
	req->zero = true;

	/*
	 * Call internal queue directly after dropping the lock. This is
	 * safe to do as the reply is always the last thing done when
	 * processing a SETUP packet, usually as a tail call
	 */
	spin_unlock(&ep->vhub->lock);
	if (ep->ep.ops->queue(&ep->ep, req, GFP_ATOMIC))
		rc = std_req_stall;
	else
		rc = std_req_data;
	spin_lock(&ep->vhub->lock);
	return rc;
}

int __ast_vhub_simple_reply(struct ast_vhub_ep *ep, int len, ...)
{
	u8 *buffer = ep->buf;
	unsigned int i;
	va_list args;

	va_start(args, len);

	/* Copy data directly into EP buffer */
	for (i = 0; i < len; i++)
		buffer[i] = va_arg(args, int);
	va_end(args);

	/* req->buf NULL means data is already there */
	return ast_vhub_reply(ep, NULL, len);
}

void ast_vhub_ep0_handle_setup(struct ast_vhub_ep *ep)
{
	struct usb_ctrlrequest crq;
	enum std_req_rc std_req_rc;
	int rc = -ENODEV;

	if (WARN_ON(ep->d_idx != 0))
		return;

	/*
	 * Grab the setup packet from the chip and byteswap
	 * interesting fields
	 */
	memcpy_fromio(&crq, ep->ep0.setup, sizeof(crq));

	EPDBG(ep, "SETUP packet %02x/%02x/%04x/%04x/%04x [%s] st=%d\n",
	      crq.bRequestType, crq.bRequest,
	       le16_to_cpu(crq.wValue),
	       le16_to_cpu(crq.wIndex),
	       le16_to_cpu(crq.wLength),
	       (crq.bRequestType & USB_DIR_IN) ? "in" : "out",
	       ep->ep0.state);

	/*
	 * Check our state, cancel pending requests if needed
	 *
	 * Note: Under some circumstances, we can get a new setup
	 * packet while waiting for the stall ack, just accept it.
	 *
	 * In any case, a SETUP packet in wrong state should have
	 * reset the HW state machine, so let's just log, nuke
	 * requests, move on.
	 */
	if (ep->ep0.state != ep0_state_token &&
	    ep->ep0.state != ep0_state_stall) {
		EPDBG(ep, "wrong state\n");
		ast_vhub_nuke(ep, -EIO);
	}

	/* Calculate next state for EP0 */
	ep->ep0.state = ep0_state_data;
	ep->ep0.dir_in = !!(crq.bRequestType & USB_DIR_IN);

	/* If this is the vHub, we handle requests differently */
	std_req_rc = std_req_driver;
	if (ep->dev == NULL) {
		if ((crq.bRequestType & USB_TYPE_MASK) == USB_TYPE_STANDARD)
			std_req_rc = ast_vhub_std_hub_request(ep, &crq);
		else if ((crq.bRequestType & USB_TYPE_MASK) == USB_TYPE_CLASS)
			std_req_rc = ast_vhub_class_hub_request(ep, &crq);
		else
			std_req_rc = std_req_stall;
	} else if ((crq.bRequestType & USB_TYPE_MASK) == USB_TYPE_STANDARD)
		std_req_rc = ast_vhub_std_dev_request(ep, &crq);

	/* Act upon result */
	switch(std_req_rc) {
	case std_req_complete:
		goto complete;
	case std_req_stall:
		goto stall;
	case std_req_driver:
		break;
	case std_req_data:
		return;
	}

	/* Pass request up to the gadget driver */
	if (WARN_ON(!ep->dev))
		goto stall;
	if (ep->dev->driver) {
		EPDBG(ep, "forwarding to gadget...\n");
		spin_unlock(&ep->vhub->lock);
		rc = ep->dev->driver->setup(&ep->dev->gadget, &crq);
		spin_lock(&ep->vhub->lock);
		EPDBG(ep, "driver returned %d\n", rc);
	} else {
		EPDBG(ep, "no gadget for request !\n");
	}
	if (rc >= 0)
		return;

 stall:
	EPDBG(ep, "stalling\n");
	writel(VHUB_EP0_CTRL_STALL, ep->ep0.ctlstat);
	ep->ep0.state = ep0_state_stall;
	ep->ep0.dir_in = false;
	return;

 complete:
	EPVDBG(ep, "sending [in] status with no data\n");
	writel(VHUB_EP0_TX_BUFF_RDY, ep->ep0.ctlstat);
	ep->ep0.state = ep0_state_status;
	ep->ep0.dir_in = false;
}


static void ast_vhub_ep0_do_send(struct ast_vhub_ep *ep,
				 struct ast_vhub_req *req)
{
	unsigned int chunk;
	u32 reg;

	/* If this is a 0-length request, it's the gadget trying to
	 * send a status on our behalf. We take it from here.
	 */
	if (req->req.length == 0)
		req->last_desc = 1;

	/* Are we done ? Complete request, otherwise wait for next interrupt */
	if (req->last_desc >= 0) {
		EPVDBG(ep, "complete send %d/%d\n",
		       req->req.actual, req->req.length);
		ep->ep0.state = ep0_state_status;
		writel(VHUB_EP0_RX_BUFF_RDY, ep->ep0.ctlstat);
		ast_vhub_done(ep, req, 0);
		return;
	}

	/*
	 * Next chunk cropped to max packet size. Also check if this
	 * is the last packet
	 */
	chunk = req->req.length - req->req.actual;
	if (chunk > ep->ep.maxpacket)
		chunk = ep->ep.maxpacket;
	else if ((chunk < ep->ep.maxpacket) || !req->req.zero)
		req->last_desc = 1;

	EPVDBG(ep, "send chunk=%d last=%d, req->act=%d mp=%d\n",
	       chunk, req->last_desc, req->req.actual, ep->ep.maxpacket);

	/*
	 * Copy data if any (internal requests already have data
	 * in the EP buffer)
	 */
	if (chunk && req->req.buf)
		memcpy(ep->buf, req->req.buf + req->req.actual, chunk);

	vhub_dma_workaround(ep->buf);

	/* Remember chunk size and trigger send */
	reg = VHUB_EP0_SET_TX_LEN(chunk);
	writel(reg, ep->ep0.ctlstat);
	writel(reg | VHUB_EP0_TX_BUFF_RDY, ep->ep0.ctlstat);
	req->req.actual += chunk;
}

static void ast_vhub_ep0_rx_prime(struct ast_vhub_ep *ep)
{
	EPVDBG(ep, "rx prime\n");

	/* Prime endpoint for receiving data */
	writel(VHUB_EP0_RX_BUFF_RDY, ep->ep0.ctlstat);
}

static void ast_vhub_ep0_do_receive(struct ast_vhub_ep *ep, struct ast_vhub_req *req,
				    unsigned int len)
{
	unsigned int remain;
	int rc = 0;

	/* We are receiving... grab request */
	remain = req->req.length - req->req.actual;

	EPVDBG(ep, "receive got=%d remain=%d\n", len, remain);

	/* Are we getting more than asked ? */
	if (len > remain) {
		EPDBG(ep, "receiving too much (ovf: %d) !\n",
		      len - remain);
		len = remain;
		rc = -EOVERFLOW;
	}

	/* Hardware return wrong data len */
	if (len < ep->ep.maxpacket && len != remain) {
		EPDBG(ep, "using expected data len instead\n");
		len = remain;
	}

	if (len && req->req.buf)
		memcpy(req->req.buf + req->req.actual, ep->buf, len);
	req->req.actual += len;

	/* Done ? */
	if (len < ep->ep.maxpacket || len == remain) {
		ep->ep0.state = ep0_state_status;
		writel(VHUB_EP0_TX_BUFF_RDY, ep->ep0.ctlstat);
		ast_vhub_done(ep, req, rc);
	} else
		ast_vhub_ep0_rx_prime(ep);
}

void ast_vhub_ep0_handle_ack(struct ast_vhub_ep *ep, bool in_ack)
{
	struct ast_vhub_req *req;
	struct ast_vhub *vhub = ep->vhub;
	struct device *dev = &vhub->pdev->dev;
	bool stall = false;
	u32 stat;

	/* Read EP0 status */
	stat = readl(ep->ep0.ctlstat);

	/* Grab current request if any */
	req = list_first_entry_or_null(&ep->queue, struct ast_vhub_req, queue);

	EPVDBG(ep, "ACK status=%08x,state=%d is_in=%d in_ack=%d req=%p\n",
		stat, ep->ep0.state, ep->ep0.dir_in, in_ack, req);

	switch(ep->ep0.state) {
	case ep0_state_token:
		/* There should be no request queued in that state... */
		if (req) {
			dev_warn(dev, "request present while in TOKEN state\n");
			ast_vhub_nuke(ep, -EINVAL);
		}
		dev_warn(dev, "ack while in TOKEN state\n");
		stall = true;
		break;
	case ep0_state_data:
		/* Check the state bits corresponding to our direction */
		if ((ep->ep0.dir_in && (stat & VHUB_EP0_TX_BUFF_RDY)) ||
		    (!ep->ep0.dir_in && (stat & VHUB_EP0_RX_BUFF_RDY)) ||
		    (ep->ep0.dir_in != in_ack)) {
			/* In that case, ignore interrupt */
			dev_warn(dev, "irq state mismatch");
			break;
		}
		/*
		 * We are in data phase and there's no request, something is
		 * wrong, stall
		 */
		if (!req) {
			dev_warn(dev, "data phase, no request\n");
			stall = true;
			break;
		}

		/* We have a request, handle data transfers */
		if (ep->ep0.dir_in)
			ast_vhub_ep0_do_send(ep, req);
		else
			ast_vhub_ep0_do_receive(ep, req, VHUB_EP0_RX_LEN(stat));
		return;
	case ep0_state_status:
		/* Nuke stale requests */
		if (req) {
			dev_warn(dev, "request present while in STATUS state\n");
			ast_vhub_nuke(ep, -EINVAL);
		}

		/*
		 * If the status phase completes with the wrong ack, stall
		 * the endpoint just in case, to abort whatever the host
		 * was doing.
		 */
		if (ep->ep0.dir_in == in_ack) {
			dev_warn(dev, "status direction mismatch\n");
			stall = true;
		}
		break;
	case ep0_state_stall:
		/*
		 * There shouldn't be any request left, but nuke just in case
		 * otherwise the stale request will block subsequent ones
		 */
		ast_vhub_nuke(ep, -EIO);
		break;
	}

	/* Reset to token state or stall */
	if (stall) {
		writel(VHUB_EP0_CTRL_STALL, ep->ep0.ctlstat);
		ep->ep0.state = ep0_state_stall;
	} else
		ep->ep0.state = ep0_state_token;
}

static int ast_vhub_ep0_queue(struct usb_ep* u_ep, struct usb_request *u_req,
			      gfp_t gfp_flags)
{
	struct ast_vhub_req *req = to_ast_req(u_req);
	struct ast_vhub_ep *ep = to_ast_ep(u_ep);
	struct ast_vhub *vhub = ep->vhub;
	struct device *dev = &vhub->pdev->dev;
	unsigned long flags;

	/* Paranoid cheks */
	if (!u_req || (!u_req->complete && !req->internal)) {
		dev_warn(dev, "Bogus EP0 request ! u_req=%p\n", u_req);
		if (u_req) {
			dev_warn(dev, "complete=%p internal=%d\n",
				 u_req->complete, req->internal);
		}
		return -EINVAL;
	}

	/* Not endpoint 0 ? */
	if (WARN_ON(ep->d_idx != 0))
		return -EINVAL;

	/* Disabled device */
	if (ep->dev && !ep->dev->enabled)
		return -ESHUTDOWN;

	/* Data, no buffer and not internal ? */
	if (u_req->length && !u_req->buf && !req->internal) {
		dev_warn(dev, "Request with no buffer !\n");
		return -EINVAL;
	}

	EPVDBG(ep, "enqueue req @%p\n", req);
	EPVDBG(ep, "  l=%d zero=%d noshort=%d is_in=%d\n",
	       u_req->length, u_req->zero,
	       u_req->short_not_ok, ep->ep0.dir_in);

	/* Initialize request progress fields */
	u_req->status = -EINPROGRESS;
	u_req->actual = 0;
	req->last_desc = -1;
	req->active = false;

	spin_lock_irqsave(&vhub->lock, flags);

	/* EP0 can only support a single request at a time */
	if (!list_empty(&ep->queue) ||
	    ep->ep0.state == ep0_state_token ||
	    ep->ep0.state == ep0_state_stall) {
		dev_warn(dev, "EP0: Request in wrong state\n");
	        EPVDBG(ep, "EP0: list_empty=%d state=%d\n",
		       list_empty(&ep->queue), ep->ep0.state);
		spin_unlock_irqrestore(&vhub->lock, flags);
		return -EBUSY;
	}

	/* Add request to list and kick processing if empty */
	list_add_tail(&req->queue, &ep->queue);

	if (ep->ep0.dir_in) {
		/* IN request, send data */
		ast_vhub_ep0_do_send(ep, req);
	} else if (u_req->length == 0) {
		/* 0-len request, send completion as rx */
		EPVDBG(ep, "0-length rx completion\n");
		ep->ep0.state = ep0_state_status;
		writel(VHUB_EP0_TX_BUFF_RDY, ep->ep0.ctlstat);
		ast_vhub_done(ep, req, 0);
	} else {
		/* OUT request, start receiver */
		ast_vhub_ep0_rx_prime(ep);
	}

	spin_unlock_irqrestore(&vhub->lock, flags);

	return 0;
}

static int ast_vhub_ep0_dequeue(struct usb_ep* u_ep, struct usb_request *u_req)
{
	struct ast_vhub_ep *ep = to_ast_ep(u_ep);
	struct ast_vhub *vhub = ep->vhub;
	struct ast_vhub_req *req;
	unsigned long flags;
	int rc = -EINVAL;

	spin_lock_irqsave(&vhub->lock, flags);

	/* Only one request can be in the queue */
	req = list_first_entry_or_null(&ep->queue, struct ast_vhub_req, queue);

	/* Is it ours ? */
	if (req && u_req == &req->req) {
		EPVDBG(ep, "dequeue req @%p\n", req);

		/*
		 * We don't have to deal with "active" as all
		 * DMAs go to the EP buffers, not the request.
		 */
		ast_vhub_done(ep, req, -ECONNRESET);

		/* We do stall the EP to clean things up in HW */
		writel(VHUB_EP0_CTRL_STALL, ep->ep0.ctlstat);
		ep->ep0.state = ep0_state_status;
		ep->ep0.dir_in = false;
		rc = 0;
	}
	spin_unlock_irqrestore(&vhub->lock, flags);
	return rc;
}


static const struct usb_ep_ops ast_vhub_ep0_ops = {
	.queue		= ast_vhub_ep0_queue,
	.dequeue	= ast_vhub_ep0_dequeue,
	.alloc_request	= ast_vhub_alloc_request,
	.free_request	= ast_vhub_free_request,
};

void ast_vhub_reset_ep0(struct ast_vhub_dev *dev)
{
	struct ast_vhub_ep *ep = &dev->ep0;

	ast_vhub_nuke(ep, -EIO);
	ep->ep0.state = ep0_state_token;
}


void ast_vhub_init_ep0(struct ast_vhub *vhub, struct ast_vhub_ep *ep,
		       struct ast_vhub_dev *dev)
{
	memset(ep, 0, sizeof(*ep));

	INIT_LIST_HEAD(&ep->ep.ep_list);
	INIT_LIST_HEAD(&ep->queue);
	ep->ep.ops = &ast_vhub_ep0_ops;
	ep->ep.name = "ep0";
	ep->ep.caps.type_control = true;
	usb_ep_set_maxpacket_limit(&ep->ep, AST_VHUB_EP0_MAX_PACKET);
	ep->d_idx = 0;
	ep->dev = dev;
	ep->vhub = vhub;
	ep->ep0.state = ep0_state_token;
	INIT_LIST_HEAD(&ep->ep0.req.queue);
	ep->ep0.req.internal = true;

	/* Small difference between vHub and devices */
	if (dev) {
		ep->ep0.ctlstat = dev->regs + AST_VHUB_DEV_EP0_CTRL;
		ep->ep0.setup = vhub->regs +
			AST_VHUB_SETUP0 + 8 * (dev->index + 1);
		ep->buf = vhub->ep0_bufs +
			AST_VHUB_EP0_MAX_PACKET * (dev->index + 1);
		ep->buf_dma = vhub->ep0_bufs_dma +
			AST_VHUB_EP0_MAX_PACKET * (dev->index + 1);
	} else {
		ep->ep0.ctlstat = vhub->regs + AST_VHUB_EP0_CTRL;
		ep->ep0.setup = vhub->regs + AST_VHUB_SETUP0;
		ep->buf = vhub->ep0_bufs;
		ep->buf_dma = vhub->ep0_bufs_dma;
	}
}
