// SPDX-License-Identifier: GPL-1.0+
/*
 * Renesas USB driver
 *
 * Copyright (C) 2011 Renesas Solutions Corp.
 * Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 */
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/usb/otg.h>
#include "common.h"

/*
 *		struct
 */
struct usbhsg_request {
	struct usb_request	req;
	struct usbhs_pkt	pkt;
};

#define EP_NAME_SIZE 8
struct usbhsg_gpriv;
struct usbhsg_uep {
	struct usb_ep		 ep;
	struct usbhs_pipe	*pipe;
	spinlock_t		lock;	/* protect the pipe */

	char ep_name[EP_NAME_SIZE];

	struct usbhsg_gpriv *gpriv;
};

struct usbhsg_gpriv {
	struct usb_gadget	 gadget;
	struct usbhs_mod	 mod;

	struct usbhsg_uep	*uep;
	int			 uep_size;

	struct usb_gadget_driver	*driver;
	struct usb_phy		*transceiver;
	bool			 vbus_active;

	u32	status;
#define USBHSG_STATUS_STARTED		(1 << 0)
#define USBHSG_STATUS_REGISTERD		(1 << 1)
#define USBHSG_STATUS_WEDGE		(1 << 2)
#define USBHSG_STATUS_SELF_POWERED	(1 << 3)
#define USBHSG_STATUS_SOFT_CONNECT	(1 << 4)
};

struct usbhsg_recip_handle {
	char *name;
	int (*device)(struct usbhs_priv *priv, struct usbhsg_uep *uep,
		      struct usb_ctrlrequest *ctrl);
	int (*interface)(struct usbhs_priv *priv, struct usbhsg_uep *uep,
			 struct usb_ctrlrequest *ctrl);
	int (*endpoint)(struct usbhs_priv *priv, struct usbhsg_uep *uep,
			struct usb_ctrlrequest *ctrl);
};

/*
 *		macro
 */
#define usbhsg_priv_to_gpriv(priv)			\
	container_of(					\
		usbhs_mod_get(priv, USBHS_GADGET),	\
		struct usbhsg_gpriv, mod)

#define __usbhsg_for_each_uep(start, pos, g, i)	\
	for ((i) = start;					\
	     ((i) < (g)->uep_size) && ((pos) = (g)->uep + (i));	\
	     (i)++)

#define usbhsg_for_each_uep(pos, gpriv, i)	\
	__usbhsg_for_each_uep(1, pos, gpriv, i)

#define usbhsg_for_each_uep_with_dcp(pos, gpriv, i)	\
	__usbhsg_for_each_uep(0, pos, gpriv, i)

#define usbhsg_gadget_to_gpriv(g)\
	container_of(g, struct usbhsg_gpriv, gadget)

#define usbhsg_req_to_ureq(r)\
	container_of(r, struct usbhsg_request, req)

#define usbhsg_ep_to_uep(e)		container_of(e, struct usbhsg_uep, ep)
#define usbhsg_gpriv_to_dev(gp)		usbhs_priv_to_dev((gp)->mod.priv)
#define usbhsg_gpriv_to_priv(gp)	((gp)->mod.priv)
#define usbhsg_gpriv_to_dcp(gp)		((gp)->uep)
#define usbhsg_gpriv_to_nth_uep(gp, i)	((gp)->uep + i)
#define usbhsg_uep_to_gpriv(u)		((u)->gpriv)
#define usbhsg_uep_to_pipe(u)		((u)->pipe)
#define usbhsg_pipe_to_uep(p)		((p)->mod_private)
#define usbhsg_is_dcp(u)		((u) == usbhsg_gpriv_to_dcp((u)->gpriv))

#define usbhsg_ureq_to_pkt(u)		(&(u)->pkt)
#define usbhsg_pkt_to_ureq(i)	\
	container_of(i, struct usbhsg_request, pkt)

#define usbhsg_is_not_connected(gp) ((gp)->gadget.speed == USB_SPEED_UNKNOWN)

/* status */
#define usbhsg_status_init(gp)   do {(gp)->status = 0; } while (0)
#define usbhsg_status_set(gp, b) (gp->status |=  b)
#define usbhsg_status_clr(gp, b) (gp->status &= ~b)
#define usbhsg_status_has(gp, b) (gp->status &   b)

/*
 *		queue push/pop
 */
static void __usbhsg_queue_pop(struct usbhsg_uep *uep,
			       struct usbhsg_request *ureq,
			       int status)
{
	struct usbhsg_gpriv *gpriv = usbhsg_uep_to_gpriv(uep);
	struct usbhs_pipe *pipe = usbhsg_uep_to_pipe(uep);
	struct device *dev = usbhsg_gpriv_to_dev(gpriv);
	struct usbhs_priv *priv = usbhsg_gpriv_to_priv(gpriv);

	if (pipe)
		dev_dbg(dev, "pipe %d : queue pop\n", usbhs_pipe_number(pipe));

	ureq->req.status = status;
	spin_unlock(usbhs_priv_to_lock(priv));
	usb_gadget_giveback_request(&uep->ep, &ureq->req);
	spin_lock(usbhs_priv_to_lock(priv));
}

static void usbhsg_queue_pop(struct usbhsg_uep *uep,
			     struct usbhsg_request *ureq,
			     int status)
{
	struct usbhsg_gpriv *gpriv = usbhsg_uep_to_gpriv(uep);
	struct usbhs_priv *priv = usbhsg_gpriv_to_priv(gpriv);
	unsigned long flags;

	usbhs_lock(priv, flags);
	__usbhsg_queue_pop(uep, ureq, status);
	usbhs_unlock(priv, flags);
}

static void usbhsg_queue_done(struct usbhs_priv *priv, struct usbhs_pkt *pkt)
{
	struct usbhs_pipe *pipe = pkt->pipe;
	struct usbhsg_uep *uep = usbhsg_pipe_to_uep(pipe);
	struct usbhsg_request *ureq = usbhsg_pkt_to_ureq(pkt);
	unsigned long flags;

	ureq->req.actual = pkt->actual;

	usbhs_lock(priv, flags);
	if (uep)
		__usbhsg_queue_pop(uep, ureq, 0);
	usbhs_unlock(priv, flags);
}

static void usbhsg_queue_push(struct usbhsg_uep *uep,
			      struct usbhsg_request *ureq)
{
	struct usbhsg_gpriv *gpriv = usbhsg_uep_to_gpriv(uep);
	struct device *dev = usbhsg_gpriv_to_dev(gpriv);
	struct usbhs_pipe *pipe = usbhsg_uep_to_pipe(uep);
	struct usbhs_pkt *pkt = usbhsg_ureq_to_pkt(ureq);
	struct usb_request *req = &ureq->req;

	req->actual = 0;
	req->status = -EINPROGRESS;
	usbhs_pkt_push(pipe, pkt, usbhsg_queue_done,
		       req->buf, req->length, req->zero, -1);
	usbhs_pkt_start(pipe);

	dev_dbg(dev, "pipe %d : queue push (%d)\n",
		usbhs_pipe_number(pipe),
		req->length);
}

/*
 *		dma map/unmap
 */
static int usbhsg_dma_map_ctrl(struct device *dma_dev, struct usbhs_pkt *pkt,
			       int map)
{
	struct usbhsg_request *ureq = usbhsg_pkt_to_ureq(pkt);
	struct usb_request *req = &ureq->req;
	struct usbhs_pipe *pipe = pkt->pipe;
	enum dma_data_direction dir;
	int ret = 0;

	dir = usbhs_pipe_is_dir_host(pipe);

	if (map) {
		/* it can not use scatter/gather */
		WARN_ON(req->num_sgs);

		ret = usb_gadget_map_request_by_dev(dma_dev, req, dir);
		if (ret < 0)
			return ret;

		pkt->dma = req->dma;
	} else {
		usb_gadget_unmap_request_by_dev(dma_dev, req, dir);
	}

	return ret;
}

/*
 *		USB_TYPE_STANDARD / clear feature functions
 */
static int usbhsg_recip_handler_std_control_done(struct usbhs_priv *priv,
						 struct usbhsg_uep *uep,
						 struct usb_ctrlrequest *ctrl)
{
	struct usbhsg_gpriv *gpriv = usbhsg_priv_to_gpriv(priv);
	struct usbhsg_uep *dcp = usbhsg_gpriv_to_dcp(gpriv);
	struct usbhs_pipe *pipe = usbhsg_uep_to_pipe(dcp);

	usbhs_dcp_control_transfer_done(pipe);

	return 0;
}

static int usbhsg_recip_handler_std_clear_endpoint(struct usbhs_priv *priv,
						   struct usbhsg_uep *uep,
						   struct usb_ctrlrequest *ctrl)
{
	struct usbhsg_gpriv *gpriv = usbhsg_uep_to_gpriv(uep);
	struct usbhs_pipe *pipe = usbhsg_uep_to_pipe(uep);

	if (!usbhsg_status_has(gpriv, USBHSG_STATUS_WEDGE)) {
		usbhs_pipe_disable(pipe);
		usbhs_pipe_sequence_data0(pipe);
		usbhs_pipe_enable(pipe);
	}

	usbhsg_recip_handler_std_control_done(priv, uep, ctrl);

	usbhs_pkt_start(pipe);

	return 0;
}

static struct usbhsg_recip_handle req_clear_feature = {
	.name		= "clear feature",
	.device		= usbhsg_recip_handler_std_control_done,
	.interface	= usbhsg_recip_handler_std_control_done,
	.endpoint	= usbhsg_recip_handler_std_clear_endpoint,
};

/*
 *		USB_TYPE_STANDARD / set feature functions
 */
static int usbhsg_recip_handler_std_set_device(struct usbhs_priv *priv,
						 struct usbhsg_uep *uep,
						 struct usb_ctrlrequest *ctrl)
{
	switch (le16_to_cpu(ctrl->wValue)) {
	case USB_DEVICE_TEST_MODE:
		usbhsg_recip_handler_std_control_done(priv, uep, ctrl);
		udelay(100);
		usbhs_sys_set_test_mode(priv, le16_to_cpu(ctrl->wIndex >> 8));
		break;
	default:
		usbhsg_recip_handler_std_control_done(priv, uep, ctrl);
		break;
	}

	return 0;
}

static int usbhsg_recip_handler_std_set_endpoint(struct usbhs_priv *priv,
						 struct usbhsg_uep *uep,
						 struct usb_ctrlrequest *ctrl)
{
	struct usbhs_pipe *pipe = usbhsg_uep_to_pipe(uep);

	usbhs_pipe_stall(pipe);

	usbhsg_recip_handler_std_control_done(priv, uep, ctrl);

	return 0;
}

static struct usbhsg_recip_handle req_set_feature = {
	.name		= "set feature",
	.device		= usbhsg_recip_handler_std_set_device,
	.interface	= usbhsg_recip_handler_std_control_done,
	.endpoint	= usbhsg_recip_handler_std_set_endpoint,
};

/*
 *		USB_TYPE_STANDARD / get status functions
 */
static void __usbhsg_recip_send_complete(struct usb_ep *ep,
					 struct usb_request *req)
{
	struct usbhsg_request *ureq = usbhsg_req_to_ureq(req);

	/* free allocated recip-buffer/usb_request */
	kfree(ureq->pkt.buf);
	usb_ep_free_request(ep, req);
}

static void __usbhsg_recip_send_status(struct usbhsg_gpriv *gpriv,
				       unsigned short status)
{
	struct usbhsg_uep *dcp = usbhsg_gpriv_to_dcp(gpriv);
	struct usbhs_pipe *pipe = usbhsg_uep_to_pipe(dcp);
	struct device *dev = usbhsg_gpriv_to_dev(gpriv);
	struct usb_request *req;
	unsigned short *buf;

	/* alloc new usb_request for recip */
	req = usb_ep_alloc_request(&dcp->ep, GFP_ATOMIC);
	if (!req) {
		dev_err(dev, "recip request allocation fail\n");
		return;
	}

	/* alloc recip data buffer */
	buf = kmalloc(sizeof(*buf), GFP_ATOMIC);
	if (!buf) {
		usb_ep_free_request(&dcp->ep, req);
		return;
	}

	/* recip data is status */
	*buf = cpu_to_le16(status);

	/* allocated usb_request/buffer will be freed */
	req->complete	= __usbhsg_recip_send_complete;
	req->buf	= buf;
	req->length	= sizeof(*buf);
	req->zero	= 0;

	/* push packet */
	pipe->handler = &usbhs_fifo_pio_push_handler;
	usbhsg_queue_push(dcp, usbhsg_req_to_ureq(req));
}

static int usbhsg_recip_handler_std_get_device(struct usbhs_priv *priv,
					       struct usbhsg_uep *uep,
					       struct usb_ctrlrequest *ctrl)
{
	struct usbhsg_gpriv *gpriv = usbhsg_uep_to_gpriv(uep);
	unsigned short status = 0;

	if (usbhsg_status_has(gpriv, USBHSG_STATUS_SELF_POWERED))
		status = 1 << USB_DEVICE_SELF_POWERED;

	__usbhsg_recip_send_status(gpriv, status);

	return 0;
}

static int usbhsg_recip_handler_std_get_interface(struct usbhs_priv *priv,
						  struct usbhsg_uep *uep,
						  struct usb_ctrlrequest *ctrl)
{
	struct usbhsg_gpriv *gpriv = usbhsg_uep_to_gpriv(uep);
	unsigned short status = 0;

	__usbhsg_recip_send_status(gpriv, status);

	return 0;
}

static int usbhsg_recip_handler_std_get_endpoint(struct usbhs_priv *priv,
						 struct usbhsg_uep *uep,
						 struct usb_ctrlrequest *ctrl)
{
	struct usbhsg_gpriv *gpriv = usbhsg_uep_to_gpriv(uep);
	struct usbhs_pipe *pipe = usbhsg_uep_to_pipe(uep);
	unsigned short status = 0;

	if (usbhs_pipe_is_stall(pipe))
		status = 1 << USB_ENDPOINT_HALT;

	__usbhsg_recip_send_status(gpriv, status);

	return 0;
}

static struct usbhsg_recip_handle req_get_status = {
	.name		= "get status",
	.device		= usbhsg_recip_handler_std_get_device,
	.interface	= usbhsg_recip_handler_std_get_interface,
	.endpoint	= usbhsg_recip_handler_std_get_endpoint,
};

/*
 *		USB_TYPE handler
 */
static int usbhsg_recip_run_handle(struct usbhs_priv *priv,
				   struct usbhsg_recip_handle *handler,
				   struct usb_ctrlrequest *ctrl)
{
	struct usbhsg_gpriv *gpriv = usbhsg_priv_to_gpriv(priv);
	struct device *dev = usbhsg_gpriv_to_dev(gpriv);
	struct usbhsg_uep *uep;
	struct usbhs_pipe *pipe;
	int recip = ctrl->bRequestType & USB_RECIP_MASK;
	int nth = le16_to_cpu(ctrl->wIndex) & USB_ENDPOINT_NUMBER_MASK;
	int ret = 0;
	int (*func)(struct usbhs_priv *priv, struct usbhsg_uep *uep,
		    struct usb_ctrlrequest *ctrl);
	char *msg;

	uep = usbhsg_gpriv_to_nth_uep(gpriv, nth);
	pipe = usbhsg_uep_to_pipe(uep);
	if (!pipe) {
		dev_err(dev, "wrong recip request\n");
		return -EINVAL;
	}

	switch (recip) {
	case USB_RECIP_DEVICE:
		msg	= "DEVICE";
		func	= handler->device;
		break;
	case USB_RECIP_INTERFACE:
		msg	= "INTERFACE";
		func	= handler->interface;
		break;
	case USB_RECIP_ENDPOINT:
		msg	= "ENDPOINT";
		func	= handler->endpoint;
		break;
	default:
		dev_warn(dev, "unsupported RECIP(%d)\n", recip);
		func = NULL;
		ret = -EINVAL;
	}

	if (func) {
		dev_dbg(dev, "%s (pipe %d :%s)\n", handler->name, nth, msg);
		ret = func(priv, uep, ctrl);
	}

	return ret;
}

/*
 *		irq functions
 *
 * it will be called from usbhs_interrupt
 */
static int usbhsg_irq_dev_state(struct usbhs_priv *priv,
				struct usbhs_irq_state *irq_state)
{
	struct usbhsg_gpriv *gpriv = usbhsg_priv_to_gpriv(priv);
	struct device *dev = usbhsg_gpriv_to_dev(gpriv);

	gpriv->gadget.speed = usbhs_bus_get_speed(priv);

	dev_dbg(dev, "state = %x : speed : %d\n",
		usbhs_status_get_device_state(irq_state),
		gpriv->gadget.speed);

	return 0;
}

static int usbhsg_irq_ctrl_stage(struct usbhs_priv *priv,
				 struct usbhs_irq_state *irq_state)
{
	struct usbhsg_gpriv *gpriv = usbhsg_priv_to_gpriv(priv);
	struct usbhsg_uep *dcp = usbhsg_gpriv_to_dcp(gpriv);
	struct usbhs_pipe *pipe = usbhsg_uep_to_pipe(dcp);
	struct device *dev = usbhsg_gpriv_to_dev(gpriv);
	struct usb_ctrlrequest ctrl;
	struct usbhsg_recip_handle *recip_handler = NULL;
	int stage = usbhs_status_get_ctrl_stage(irq_state);
	int ret = 0;

	dev_dbg(dev, "stage = %d\n", stage);

	/*
	 * see Manual
	 *
	 *  "Operation"
	 *  - "Interrupt Function"
	 *    - "Control Transfer Stage Transition Interrupt"
	 *      - Fig. "Control Transfer Stage Transitions"
	 */

	switch (stage) {
	case READ_DATA_STAGE:
		pipe->handler = &usbhs_fifo_pio_push_handler;
		break;
	case WRITE_DATA_STAGE:
		pipe->handler = &usbhs_fifo_pio_pop_handler;
		break;
	case NODATA_STATUS_STAGE:
		pipe->handler = &usbhs_ctrl_stage_end_handler;
		break;
	case READ_STATUS_STAGE:
	case WRITE_STATUS_STAGE:
		usbhs_dcp_control_transfer_done(pipe);
		/* fall through */
	default:
		return ret;
	}

	/*
	 * get usb request
	 */
	usbhs_usbreq_get_val(priv, &ctrl);

	switch (ctrl.bRequestType & USB_TYPE_MASK) {
	case USB_TYPE_STANDARD:
		switch (ctrl.bRequest) {
		case USB_REQ_CLEAR_FEATURE:
			recip_handler = &req_clear_feature;
			break;
		case USB_REQ_SET_FEATURE:
			recip_handler = &req_set_feature;
			break;
		case USB_REQ_GET_STATUS:
			recip_handler = &req_get_status;
			break;
		}
	}

	/*
	 * setup stage / run recip
	 */
	if (recip_handler)
		ret = usbhsg_recip_run_handle(priv, recip_handler, &ctrl);
	else
		ret = gpriv->driver->setup(&gpriv->gadget, &ctrl);

	if (ret < 0)
		usbhs_pipe_stall(pipe);

	return ret;
}

/*
 *
 *		usb_dcp_ops
 *
 */
static int usbhsg_pipe_disable(struct usbhsg_uep *uep)
{
	struct usbhs_pipe *pipe = usbhsg_uep_to_pipe(uep);
	struct usbhs_pkt *pkt;

	while (1) {
		pkt = usbhs_pkt_pop(pipe, NULL);
		if (!pkt)
			break;

		usbhsg_queue_pop(uep, usbhsg_pkt_to_ureq(pkt), -ESHUTDOWN);
	}

	usbhs_pipe_disable(pipe);

	return 0;
}

/*
 *
 *		usb_ep_ops
 *
 */
static int usbhsg_ep_enable(struct usb_ep *ep,
			 const struct usb_endpoint_descriptor *desc)
{
	struct usbhsg_uep *uep   = usbhsg_ep_to_uep(ep);
	struct usbhsg_gpriv *gpriv = usbhsg_uep_to_gpriv(uep);
	struct usbhs_priv *priv = usbhsg_gpriv_to_priv(gpriv);
	struct usbhs_pipe *pipe;
	int ret = -EIO;
	unsigned long flags;

	usbhs_lock(priv, flags);

	/*
	 * if it already have pipe,
	 * nothing to do
	 */
	if (uep->pipe) {
		usbhs_pipe_clear(uep->pipe);
		usbhs_pipe_sequence_data0(uep->pipe);
		ret = 0;
		goto usbhsg_ep_enable_end;
	}

	pipe = usbhs_pipe_malloc(priv,
				 usb_endpoint_type(desc),
				 usb_endpoint_dir_in(desc));
	if (pipe) {
		uep->pipe		= pipe;
		pipe->mod_private	= uep;

		/* set epnum / maxp */
		usbhs_pipe_config_update(pipe, 0,
					 usb_endpoint_num(desc),
					 usb_endpoint_maxp(desc));

		/*
		 * usbhs_fifo_dma_push/pop_handler try to
		 * use dmaengine if possible.
		 * It will use pio handler if impossible.
		 */
		if (usb_endpoint_dir_in(desc)) {
			pipe->handler = &usbhs_fifo_dma_push_handler;
		} else {
			pipe->handler = &usbhs_fifo_dma_pop_handler;
			usbhs_xxxsts_clear(priv, BRDYSTS,
					   usbhs_pipe_number(pipe));
		}

		ret = 0;
	}

usbhsg_ep_enable_end:
	usbhs_unlock(priv, flags);

	return ret;
}

static int usbhsg_ep_disable(struct usb_ep *ep)
{
	struct usbhsg_uep *uep = usbhsg_ep_to_uep(ep);
	struct usbhs_pipe *pipe;
	unsigned long flags;

	spin_lock_irqsave(&uep->lock, flags);
	pipe = usbhsg_uep_to_pipe(uep);
	if (!pipe)
		goto out;

	usbhsg_pipe_disable(uep);
	usbhs_pipe_free(pipe);

	uep->pipe->mod_private	= NULL;
	uep->pipe		= NULL;

out:
	spin_unlock_irqrestore(&uep->lock, flags);

	return 0;
}

static struct usb_request *usbhsg_ep_alloc_request(struct usb_ep *ep,
						   gfp_t gfp_flags)
{
	struct usbhsg_request *ureq;

	ureq = kzalloc(sizeof *ureq, gfp_flags);
	if (!ureq)
		return NULL;

	usbhs_pkt_init(usbhsg_ureq_to_pkt(ureq));

	return &ureq->req;
}

static void usbhsg_ep_free_request(struct usb_ep *ep,
				   struct usb_request *req)
{
	struct usbhsg_request *ureq = usbhsg_req_to_ureq(req);

	WARN_ON(!list_empty(&ureq->pkt.node));
	kfree(ureq);
}

static int usbhsg_ep_queue(struct usb_ep *ep, struct usb_request *req,
			  gfp_t gfp_flags)
{
	struct usbhsg_uep *uep = usbhsg_ep_to_uep(ep);
	struct usbhsg_gpriv *gpriv = usbhsg_uep_to_gpriv(uep);
	struct usbhsg_request *ureq = usbhsg_req_to_ureq(req);
	struct usbhs_pipe *pipe = usbhsg_uep_to_pipe(uep);

	/* param check */
	if (usbhsg_is_not_connected(gpriv)	||
	    unlikely(!gpriv->driver)		||
	    unlikely(!pipe))
		return -ESHUTDOWN;

	usbhsg_queue_push(uep, ureq);

	return 0;
}

static int usbhsg_ep_dequeue(struct usb_ep *ep, struct usb_request *req)
{
	struct usbhsg_uep *uep = usbhsg_ep_to_uep(ep);
	struct usbhsg_request *ureq = usbhsg_req_to_ureq(req);
	struct usbhs_pipe *pipe;
	unsigned long flags;

	spin_lock_irqsave(&uep->lock, flags);
	pipe = usbhsg_uep_to_pipe(uep);
	if (pipe)
		usbhs_pkt_pop(pipe, usbhsg_ureq_to_pkt(ureq));

	/*
	 * To dequeue a request, this driver should call the usbhsg_queue_pop()
	 * even if the pipe is NULL.
	 */
	usbhsg_queue_pop(uep, ureq, -ECONNRESET);
	spin_unlock_irqrestore(&uep->lock, flags);

	return 0;
}

static int __usbhsg_ep_set_halt_wedge(struct usb_ep *ep, int halt, int wedge)
{
	struct usbhsg_uep *uep = usbhsg_ep_to_uep(ep);
	struct usbhs_pipe *pipe = usbhsg_uep_to_pipe(uep);
	struct usbhsg_gpriv *gpriv = usbhsg_uep_to_gpriv(uep);
	struct usbhs_priv *priv = usbhsg_gpriv_to_priv(gpriv);
	struct device *dev = usbhsg_gpriv_to_dev(gpriv);
	unsigned long flags;

	usbhsg_pipe_disable(uep);

	dev_dbg(dev, "set halt %d (pipe %d)\n",
		halt, usbhs_pipe_number(pipe));

	/********************  spin lock ********************/
	usbhs_lock(priv, flags);

	if (halt)
		usbhs_pipe_stall(pipe);
	else
		usbhs_pipe_disable(pipe);

	if (halt && wedge)
		usbhsg_status_set(gpriv, USBHSG_STATUS_WEDGE);
	else
		usbhsg_status_clr(gpriv, USBHSG_STATUS_WEDGE);

	usbhs_unlock(priv, flags);
	/********************  spin unlock ******************/

	return 0;
}

static int usbhsg_ep_set_halt(struct usb_ep *ep, int value)
{
	return __usbhsg_ep_set_halt_wedge(ep, value, 0);
}

static int usbhsg_ep_set_wedge(struct usb_ep *ep)
{
	return __usbhsg_ep_set_halt_wedge(ep, 1, 1);
}

static const struct usb_ep_ops usbhsg_ep_ops = {
	.enable		= usbhsg_ep_enable,
	.disable	= usbhsg_ep_disable,

	.alloc_request	= usbhsg_ep_alloc_request,
	.free_request	= usbhsg_ep_free_request,

	.queue		= usbhsg_ep_queue,
	.dequeue	= usbhsg_ep_dequeue,

	.set_halt	= usbhsg_ep_set_halt,
	.set_wedge	= usbhsg_ep_set_wedge,
};

/*
 *		pullup control
 */
static int usbhsg_can_pullup(struct usbhs_priv *priv)
{
	struct usbhsg_gpriv *gpriv = usbhsg_priv_to_gpriv(priv);

	return gpriv->driver &&
	       usbhsg_status_has(gpriv, USBHSG_STATUS_SOFT_CONNECT);
}

static void usbhsg_update_pullup(struct usbhs_priv *priv)
{
	if (usbhsg_can_pullup(priv))
		usbhs_sys_function_pullup(priv, 1);
	else
		usbhs_sys_function_pullup(priv, 0);
}

/*
 *		usb module start/end
 */
static int usbhsg_try_start(struct usbhs_priv *priv, u32 status)
{
	struct usbhsg_gpriv *gpriv = usbhsg_priv_to_gpriv(priv);
	struct usbhsg_uep *dcp = usbhsg_gpriv_to_dcp(gpriv);
	struct usbhs_mod *mod = usbhs_mod_get_current(priv);
	struct device *dev = usbhs_priv_to_dev(priv);
	unsigned long flags;
	int ret = 0;

	/********************  spin lock ********************/
	usbhs_lock(priv, flags);

	usbhsg_status_set(gpriv, status);
	if (!(usbhsg_status_has(gpriv, USBHSG_STATUS_STARTED) &&
	      usbhsg_status_has(gpriv, USBHSG_STATUS_REGISTERD)))
		ret = -1; /* not ready */

	usbhs_unlock(priv, flags);
	/********************  spin unlock ********************/

	if (ret < 0)
		return 0; /* not ready is not error */

	/*
	 * enable interrupt and systems if ready
	 */
	dev_dbg(dev, "start gadget\n");

	/*
	 * pipe initialize and enable DCP
	 */
	usbhs_fifo_init(priv);
	usbhs_pipe_init(priv,
			usbhsg_dma_map_ctrl);

	/* dcp init instead of usbhsg_ep_enable() */
	dcp->pipe		= usbhs_dcp_malloc(priv);
	dcp->pipe->mod_private	= dcp;
	usbhs_pipe_config_update(dcp->pipe, 0, 0, 64);

	/*
	 * system config enble
	 * - HI speed
	 * - function
	 * - usb module
	 */
	usbhs_sys_function_ctrl(priv, 1);
	usbhsg_update_pullup(priv);

	/*
	 * enable irq callback
	 */
	mod->irq_dev_state	= usbhsg_irq_dev_state;
	mod->irq_ctrl_stage	= usbhsg_irq_ctrl_stage;
	usbhs_irq_callback_update(priv, mod);

	return 0;
}

static int usbhsg_try_stop(struct usbhs_priv *priv, u32 status)
{
	struct usbhsg_gpriv *gpriv = usbhsg_priv_to_gpriv(priv);
	struct usbhs_mod *mod = usbhs_mod_get_current(priv);
	struct usbhsg_uep *uep;
	struct device *dev = usbhs_priv_to_dev(priv);
	unsigned long flags;
	int ret = 0, i;

	/********************  spin lock ********************/
	usbhs_lock(priv, flags);

	usbhsg_status_clr(gpriv, status);
	if (!usbhsg_status_has(gpriv, USBHSG_STATUS_STARTED) &&
	    !usbhsg_status_has(gpriv, USBHSG_STATUS_REGISTERD))
		ret = -1; /* already done */

	usbhs_unlock(priv, flags);
	/********************  spin unlock ********************/

	if (ret < 0)
		return 0; /* already done is not error */

	/*
	 * disable interrupt and systems if 1st try
	 */
	usbhs_fifo_quit(priv);

	/* disable all irq */
	mod->irq_dev_state	= NULL;
	mod->irq_ctrl_stage	= NULL;
	usbhs_irq_callback_update(priv, mod);

	gpriv->gadget.speed = USB_SPEED_UNKNOWN;

	/* disable sys */
	usbhs_sys_set_test_mode(priv, 0);
	usbhs_sys_function_ctrl(priv, 0);

	/* disable all eps */
	usbhsg_for_each_uep_with_dcp(uep, gpriv, i)
		usbhsg_ep_disable(&uep->ep);

	dev_dbg(dev, "stop gadget\n");

	return 0;
}

/*
 * VBUS provided by the PHY
 */
static int usbhsm_phy_get_vbus(struct platform_device *pdev)
{
	struct usbhs_priv *priv = usbhs_pdev_to_priv(pdev);
	struct usbhsg_gpriv *gpriv = usbhsg_priv_to_gpriv(priv);

	return  gpriv->vbus_active;
}

static void usbhs_mod_phy_mode(struct usbhs_priv *priv)
{
	struct usbhs_mod_info *info = &priv->mod_info;

	info->irq_vbus		= NULL;
	priv->pfunc.get_vbus	= usbhsm_phy_get_vbus;

	usbhs_irq_callback_update(priv, NULL);
}

/*
 *
 *		linux usb function
 *
 */
static int usbhsg_gadget_start(struct usb_gadget *gadget,
		struct usb_gadget_driver *driver)
{
	struct usbhsg_gpriv *gpriv = usbhsg_gadget_to_gpriv(gadget);
	struct usbhs_priv *priv = usbhsg_gpriv_to_priv(gpriv);
	struct device *dev = usbhs_priv_to_dev(priv);
	int ret;

	if (!driver		||
	    !driver->setup	||
	    driver->max_speed < USB_SPEED_FULL)
		return -EINVAL;

	/* connect to bus through transceiver */
	if (!IS_ERR_OR_NULL(gpriv->transceiver)) {
		ret = otg_set_peripheral(gpriv->transceiver->otg,
					&gpriv->gadget);
		if (ret) {
			dev_err(dev, "%s: can't bind to transceiver\n",
				gpriv->gadget.name);
			return ret;
		}

		/* get vbus using phy versions */
		usbhs_mod_phy_mode(priv);
	}

	/* first hook up the driver ... */
	gpriv->driver = driver;

	return usbhsg_try_start(priv, USBHSG_STATUS_REGISTERD);
}

static int usbhsg_gadget_stop(struct usb_gadget *gadget)
{
	struct usbhsg_gpriv *gpriv = usbhsg_gadget_to_gpriv(gadget);
	struct usbhs_priv *priv = usbhsg_gpriv_to_priv(gpriv);

	usbhsg_try_stop(priv, USBHSG_STATUS_REGISTERD);

	if (!IS_ERR_OR_NULL(gpriv->transceiver))
		otg_set_peripheral(gpriv->transceiver->otg, NULL);

	gpriv->driver = NULL;

	return 0;
}

/*
 *		usb gadget ops
 */
static int usbhsg_get_frame(struct usb_gadget *gadget)
{
	struct usbhsg_gpriv *gpriv = usbhsg_gadget_to_gpriv(gadget);
	struct usbhs_priv *priv = usbhsg_gpriv_to_priv(gpriv);

	return usbhs_frame_get_num(priv);
}

static int usbhsg_pullup(struct usb_gadget *gadget, int is_on)
{
	struct usbhsg_gpriv *gpriv = usbhsg_gadget_to_gpriv(gadget);
	struct usbhs_priv *priv = usbhsg_gpriv_to_priv(gpriv);
	unsigned long flags;

	usbhs_lock(priv, flags);
	if (is_on)
		usbhsg_status_set(gpriv, USBHSG_STATUS_SOFT_CONNECT);
	else
		usbhsg_status_clr(gpriv, USBHSG_STATUS_SOFT_CONNECT);
	usbhsg_update_pullup(priv);
	usbhs_unlock(priv, flags);

	return 0;
}

static int usbhsg_set_selfpowered(struct usb_gadget *gadget, int is_self)
{
	struct usbhsg_gpriv *gpriv = usbhsg_gadget_to_gpriv(gadget);

	if (is_self)
		usbhsg_status_set(gpriv, USBHSG_STATUS_SELF_POWERED);
	else
		usbhsg_status_clr(gpriv, USBHSG_STATUS_SELF_POWERED);

	gadget->is_selfpowered = (is_self != 0);

	return 0;
}

static int usbhsg_vbus_session(struct usb_gadget *gadget, int is_active)
{
	struct usbhsg_gpriv *gpriv = usbhsg_gadget_to_gpriv(gadget);
	struct usbhs_priv *priv = usbhsg_gpriv_to_priv(gpriv);
	struct platform_device *pdev = usbhs_priv_to_pdev(priv);

	gpriv->vbus_active = !!is_active;

	renesas_usbhs_call_notify_hotplug(pdev);

	return 0;
}

static const struct usb_gadget_ops usbhsg_gadget_ops = {
	.get_frame		= usbhsg_get_frame,
	.set_selfpowered	= usbhsg_set_selfpowered,
	.udc_start		= usbhsg_gadget_start,
	.udc_stop		= usbhsg_gadget_stop,
	.pullup			= usbhsg_pullup,
	.vbus_session		= usbhsg_vbus_session,
};

static int usbhsg_start(struct usbhs_priv *priv)
{
	return usbhsg_try_start(priv, USBHSG_STATUS_STARTED);
}

static int usbhsg_stop(struct usbhs_priv *priv)
{
	struct usbhsg_gpriv *gpriv = usbhsg_priv_to_gpriv(priv);

	/* cable disconnect */
	if (gpriv->driver &&
	    gpriv->driver->disconnect)
		gpriv->driver->disconnect(&gpriv->gadget);

	return usbhsg_try_stop(priv, USBHSG_STATUS_STARTED);
}

int usbhs_mod_gadget_probe(struct usbhs_priv *priv)
{
	struct usbhsg_gpriv *gpriv;
	struct usbhsg_uep *uep;
	struct device *dev = usbhs_priv_to_dev(priv);
	struct renesas_usbhs_driver_pipe_config *pipe_configs =
					usbhs_get_dparam(priv, pipe_configs);
	int pipe_size = usbhs_get_dparam(priv, pipe_size);
	int i;
	int ret;

	gpriv = kzalloc(sizeof(struct usbhsg_gpriv), GFP_KERNEL);
	if (!gpriv)
		return -ENOMEM;

	uep = kcalloc(pipe_size, sizeof(struct usbhsg_uep), GFP_KERNEL);
	if (!uep) {
		ret = -ENOMEM;
		goto usbhs_mod_gadget_probe_err_gpriv;
	}

	gpriv->transceiver = usb_get_phy(USB_PHY_TYPE_UNDEFINED);
	dev_info(dev, "%stransceiver found\n",
		 !IS_ERR(gpriv->transceiver) ? "" : "no ");

	/*
	 * CAUTION
	 *
	 * There is no guarantee that it is possible to access usb module here.
	 * Don't accesses to it.
	 * The accesse will be enable after "usbhsg_start"
	 */

	/*
	 * register itself
	 */
	usbhs_mod_register(priv, &gpriv->mod, USBHS_GADGET);

	/* init gpriv */
	gpriv->mod.name		= "gadget";
	gpriv->mod.start	= usbhsg_start;
	gpriv->mod.stop		= usbhsg_stop;
	gpriv->uep		= uep;
	gpriv->uep_size		= pipe_size;
	usbhsg_status_init(gpriv);

	/*
	 * init gadget
	 */
	gpriv->gadget.dev.parent	= dev;
	gpriv->gadget.name		= "renesas_usbhs_udc";
	gpriv->gadget.ops		= &usbhsg_gadget_ops;
	gpriv->gadget.max_speed		= USB_SPEED_HIGH;
	gpriv->gadget.quirk_avoids_skb_reserve = usbhs_get_dparam(priv,
								has_usb_dmac);

	INIT_LIST_HEAD(&gpriv->gadget.ep_list);

	/*
	 * init usb_ep
	 */
	usbhsg_for_each_uep_with_dcp(uep, gpriv, i) {
		uep->gpriv	= gpriv;
		uep->pipe	= NULL;
		snprintf(uep->ep_name, EP_NAME_SIZE, "ep%d", i);

		uep->ep.name		= uep->ep_name;
		uep->ep.ops		= &usbhsg_ep_ops;
		INIT_LIST_HEAD(&uep->ep.ep_list);
		spin_lock_init(&uep->lock);

		/* init DCP */
		if (usbhsg_is_dcp(uep)) {
			gpriv->gadget.ep0 = &uep->ep;
			usb_ep_set_maxpacket_limit(&uep->ep, 64);
			uep->ep.caps.type_control = true;
		} else {
			/* init normal pipe */
			if (pipe_configs[i].type == USB_ENDPOINT_XFER_ISOC)
				uep->ep.caps.type_iso = true;
			if (pipe_configs[i].type == USB_ENDPOINT_XFER_BULK)
				uep->ep.caps.type_bulk = true;
			if (pipe_configs[i].type == USB_ENDPOINT_XFER_INT)
				uep->ep.caps.type_int = true;
			usb_ep_set_maxpacket_limit(&uep->ep,
						   pipe_configs[i].bufsize);
			list_add_tail(&uep->ep.ep_list, &gpriv->gadget.ep_list);
		}
		uep->ep.caps.dir_in = true;
		uep->ep.caps.dir_out = true;
	}

	ret = usb_add_gadget_udc(dev, &gpriv->gadget);
	if (ret)
		goto err_add_udc;


	dev_info(dev, "gadget probed\n");

	return 0;

err_add_udc:
	kfree(gpriv->uep);

usbhs_mod_gadget_probe_err_gpriv:
	kfree(gpriv);

	return ret;
}

void usbhs_mod_gadget_remove(struct usbhs_priv *priv)
{
	struct usbhsg_gpriv *gpriv = usbhsg_priv_to_gpriv(priv);

	usb_del_gadget_udc(&gpriv->gadget);

	kfree(gpriv->uep);
	kfree(gpriv);
}
