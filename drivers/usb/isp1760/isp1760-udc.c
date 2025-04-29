// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for the NXP ISP1761 device controller
 *
 * Copyright 2021 Linaro, Rui Miguel Silva
 * Copyright 2014 Ideas on Board Oy
 *
 * Contacts:
 *	Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *	Rui Miguel Silva <rui.silva@linaro.org>
 */

#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/usb.h>

#include "isp1760-core.h"
#include "isp1760-regs.h"
#include "isp1760-udc.h"

#define ISP1760_VBUS_POLL_INTERVAL	msecs_to_jiffies(500)

struct isp1760_request {
	struct usb_request req;
	struct list_head queue;
	struct isp1760_ep *ep;
	unsigned int packet_size;
};

static inline struct isp1760_udc *gadget_to_udc(struct usb_gadget *gadget)
{
	return container_of(gadget, struct isp1760_udc, gadget);
}

static inline struct isp1760_ep *ep_to_udc_ep(struct usb_ep *ep)
{
	return container_of(ep, struct isp1760_ep, ep);
}

static inline struct isp1760_request *req_to_udc_req(struct usb_request *req)
{
	return container_of(req, struct isp1760_request, req);
}

static u32 isp1760_udc_read(struct isp1760_udc *udc, u16 field)
{
	return isp1760_field_read(udc->fields, field);
}

static void isp1760_udc_write(struct isp1760_udc *udc, u16 field, u32 val)
{
	isp1760_field_write(udc->fields, field, val);
}

static u32 isp1760_udc_read_raw(struct isp1760_udc *udc, u16 reg)
{
	__le32 val;

	regmap_raw_read(udc->regs, reg, &val, 4);

	return le32_to_cpu(val);
}

static u16 isp1760_udc_read_raw16(struct isp1760_udc *udc, u16 reg)
{
	__le16 val;

	regmap_raw_read(udc->regs, reg, &val, 2);

	return le16_to_cpu(val);
}

static void isp1760_udc_write_raw(struct isp1760_udc *udc, u16 reg, u32 val)
{
	__le32 val_le = cpu_to_le32(val);

	regmap_raw_write(udc->regs, reg, &val_le, 4);
}

static void isp1760_udc_write_raw16(struct isp1760_udc *udc, u16 reg, u16 val)
{
	__le16 val_le = cpu_to_le16(val);

	regmap_raw_write(udc->regs, reg, &val_le, 2);
}

static void isp1760_udc_set(struct isp1760_udc *udc, u32 field)
{
	isp1760_udc_write(udc, field, 0xFFFFFFFF);
}

static void isp1760_udc_clear(struct isp1760_udc *udc, u32 field)
{
	isp1760_udc_write(udc, field, 0);
}

static bool isp1760_udc_is_set(struct isp1760_udc *udc, u32 field)
{
	return !!isp1760_udc_read(udc, field);
}
/* -----------------------------------------------------------------------------
 * Endpoint Management
 */

static struct isp1760_ep *isp1760_udc_find_ep(struct isp1760_udc *udc,
					      u16 index)
{
	unsigned int i;

	if (index == 0)
		return &udc->ep[0];

	for (i = 1; i < ARRAY_SIZE(udc->ep); ++i) {
		if (udc->ep[i].addr == index)
			return udc->ep[i].desc ? &udc->ep[i] : NULL;
	}

	return NULL;
}

static void __isp1760_udc_select_ep(struct isp1760_udc *udc,
				    struct isp1760_ep *ep, int dir)
{
	isp1760_udc_write(udc, DC_ENDPIDX, ep->addr & USB_ENDPOINT_NUMBER_MASK);

	if (dir == USB_DIR_IN)
		isp1760_udc_set(udc, DC_EPDIR);
	else
		isp1760_udc_clear(udc, DC_EPDIR);
}

/**
 * isp1760_udc_select_ep - Select an endpoint for register access
 * @ep: The endpoint
 * @udc: Reference to the device controller
 *
 * The ISP1761 endpoint registers are banked. This function selects the target
 * endpoint for banked register access. The selection remains valid until the
 * next call to this function, the next direct access to the EPINDEX register
 * or the next reset, whichever comes first.
 *
 * Called with the UDC spinlock held.
 */
static void isp1760_udc_select_ep(struct isp1760_udc *udc,
				  struct isp1760_ep *ep)
{
	__isp1760_udc_select_ep(udc, ep, ep->addr & USB_ENDPOINT_DIR_MASK);
}

/* Called with the UDC spinlock held. */
static void isp1760_udc_ctrl_send_status(struct isp1760_ep *ep, int dir)
{
	struct isp1760_udc *udc = ep->udc;

	/*
	 * Proceed to the status stage. The status stage data packet flows in
	 * the direction opposite to the data stage data packets, we thus need
	 * to select the OUT/IN endpoint for IN/OUT transfers.
	 */
	if (dir == USB_DIR_IN)
		isp1760_udc_clear(udc, DC_EPDIR);
	else
		isp1760_udc_set(udc, DC_EPDIR);

	isp1760_udc_write(udc, DC_ENDPIDX, 1);
	isp1760_udc_set(udc, DC_STATUS);

	/*
	 * The hardware will terminate the request automatically and go back to
	 * the setup stage without notifying us.
	 */
	udc->ep0_state = ISP1760_CTRL_SETUP;
}

/* Called without the UDC spinlock held. */
static void isp1760_udc_request_complete(struct isp1760_ep *ep,
					 struct isp1760_request *req,
					 int status)
{
	struct isp1760_udc *udc = ep->udc;
	unsigned long flags;

	dev_dbg(ep->udc->isp->dev, "completing request %p with status %d\n",
		req, status);

	req->ep = NULL;
	req->req.status = status;
	req->req.complete(&ep->ep, &req->req);

	spin_lock_irqsave(&udc->lock, flags);

	/*
	 * When completing control OUT requests, move to the status stage after
	 * calling the request complete callback. This gives the gadget an
	 * opportunity to stall the control transfer if needed.
	 */
	if (status == 0 && ep->addr == 0 && udc->ep0_dir == USB_DIR_OUT)
		isp1760_udc_ctrl_send_status(ep, USB_DIR_OUT);

	spin_unlock_irqrestore(&udc->lock, flags);
}

static void isp1760_udc_ctrl_send_stall(struct isp1760_ep *ep)
{
	struct isp1760_udc *udc = ep->udc;
	unsigned long flags;

	dev_dbg(ep->udc->isp->dev, "%s(ep%02x)\n", __func__, ep->addr);

	spin_lock_irqsave(&udc->lock, flags);

	/* Stall both the IN and OUT endpoints. */
	__isp1760_udc_select_ep(udc, ep, USB_DIR_OUT);
	isp1760_udc_set(udc, DC_STALL);
	__isp1760_udc_select_ep(udc, ep, USB_DIR_IN);
	isp1760_udc_set(udc, DC_STALL);

	/* A protocol stall completes the control transaction. */
	udc->ep0_state = ISP1760_CTRL_SETUP;

	spin_unlock_irqrestore(&udc->lock, flags);
}

/* -----------------------------------------------------------------------------
 * Data Endpoints
 */

/* Called with the UDC spinlock held. */
static bool isp1760_udc_receive(struct isp1760_ep *ep,
				struct isp1760_request *req)
{
	struct isp1760_udc *udc = ep->udc;
	unsigned int len;
	u32 *buf;
	int i;

	isp1760_udc_select_ep(udc, ep);
	len = isp1760_udc_read(udc, DC_BUFLEN);

	dev_dbg(udc->isp->dev, "%s: received %u bytes (%u/%u done)\n",
		__func__, len, req->req.actual, req->req.length);

	len = min(len, req->req.length - req->req.actual);

	if (!len) {
		/*
		 * There's no data to be read from the FIFO, acknowledge the RX
		 * interrupt by clearing the buffer.
		 *
		 * TODO: What if another packet arrives in the meantime ? The
		 * datasheet doesn't clearly document how this should be
		 * handled.
		 */
		isp1760_udc_set(udc, DC_CLBUF);
		return false;
	}

	buf = req->req.buf + req->req.actual;

	/*
	 * Make sure not to read more than one extra byte, otherwise data from
	 * the next packet might be removed from the FIFO.
	 */
	for (i = len; i > 2; i -= 4, ++buf)
		*buf = isp1760_udc_read_raw(udc, ISP176x_DC_DATAPORT);
	if (i > 0)
		*(u16 *)buf = isp1760_udc_read_raw16(udc, ISP176x_DC_DATAPORT);

	req->req.actual += len;

	/*
	 * TODO: The short_not_ok flag isn't supported yet, but isn't used by
	 * any gadget driver either.
	 */

	dev_dbg(udc->isp->dev,
		"%s: req %p actual/length %u/%u maxpacket %u packet size %u\n",
		__func__, req, req->req.actual, req->req.length, ep->maxpacket,
		len);

	ep->rx_pending = false;

	/*
	 * Complete the request if all data has been received or if a short
	 * packet has been received.
	 */
	if (req->req.actual == req->req.length || len < ep->maxpacket) {
		list_del(&req->queue);
		return true;
	}

	return false;
}

static void isp1760_udc_transmit(struct isp1760_ep *ep,
				 struct isp1760_request *req)
{
	struct isp1760_udc *udc = ep->udc;
	u32 *buf = req->req.buf + req->req.actual;
	int i;

	req->packet_size = min(req->req.length - req->req.actual,
			       ep->maxpacket);

	dev_dbg(udc->isp->dev, "%s: transferring %u bytes (%u/%u done)\n",
		__func__, req->packet_size, req->req.actual,
		req->req.length);

	__isp1760_udc_select_ep(udc, ep, USB_DIR_IN);

	if (req->packet_size)
		isp1760_udc_write(udc, DC_BUFLEN, req->packet_size);

	/*
	 * Make sure not to write more than one extra byte, otherwise extra data
	 * will stay in the FIFO and will be transmitted during the next control
	 * request. The endpoint control CLBUF bit is supposed to allow flushing
	 * the FIFO for this kind of conditions, but doesn't seem to work.
	 */
	for (i = req->packet_size; i > 2; i -= 4, ++buf)
		isp1760_udc_write_raw(udc, ISP176x_DC_DATAPORT, *buf);
	if (i > 0)
		isp1760_udc_write_raw16(udc, ISP176x_DC_DATAPORT, *(u16 *)buf);

	if (ep->addr == 0)
		isp1760_udc_set(udc, DC_DSEN);
	if (!req->packet_size)
		isp1760_udc_set(udc, DC_VENDP);
}

static void isp1760_ep_rx_ready(struct isp1760_ep *ep)
{
	struct isp1760_udc *udc = ep->udc;
	struct isp1760_request *req;
	bool complete;

	spin_lock(&udc->lock);

	if (ep->addr == 0 && udc->ep0_state != ISP1760_CTRL_DATA_OUT) {
		spin_unlock(&udc->lock);
		dev_dbg(udc->isp->dev, "%s: invalid ep0 state %u\n", __func__,
			udc->ep0_state);
		return;
	}

	if (ep->addr != 0 && !ep->desc) {
		spin_unlock(&udc->lock);
		dev_dbg(udc->isp->dev, "%s: ep%02x is disabled\n", __func__,
			ep->addr);
		return;
	}

	if (list_empty(&ep->queue)) {
		ep->rx_pending = true;
		spin_unlock(&udc->lock);
		dev_dbg(udc->isp->dev, "%s: ep%02x (%p) has no request queued\n",
			__func__, ep->addr, ep);
		return;
	}

	req = list_first_entry(&ep->queue, struct isp1760_request,
			       queue);
	complete = isp1760_udc_receive(ep, req);

	spin_unlock(&udc->lock);

	if (complete)
		isp1760_udc_request_complete(ep, req, 0);
}

static void isp1760_ep_tx_complete(struct isp1760_ep *ep)
{
	struct isp1760_udc *udc = ep->udc;
	struct isp1760_request *complete = NULL;
	struct isp1760_request *req;
	bool need_zlp;

	spin_lock(&udc->lock);

	if (ep->addr == 0 && udc->ep0_state != ISP1760_CTRL_DATA_IN) {
		spin_unlock(&udc->lock);
		dev_dbg(udc->isp->dev, "TX IRQ: invalid endpoint state %u\n",
			udc->ep0_state);
		return;
	}

	if (list_empty(&ep->queue)) {
		/*
		 * This can happen for the control endpoint when the reply to
		 * the GET_STATUS IN control request is sent directly by the
		 * setup IRQ handler. Just proceed to the status stage.
		 */
		if (ep->addr == 0) {
			isp1760_udc_ctrl_send_status(ep, USB_DIR_IN);
			spin_unlock(&udc->lock);
			return;
		}

		spin_unlock(&udc->lock);
		dev_dbg(udc->isp->dev, "%s: ep%02x has no request queued\n",
			__func__, ep->addr);
		return;
	}

	req = list_first_entry(&ep->queue, struct isp1760_request,
			       queue);
	req->req.actual += req->packet_size;

	need_zlp = req->req.actual == req->req.length &&
		   !(req->req.length % ep->maxpacket) &&
		   req->packet_size && req->req.zero;

	dev_dbg(udc->isp->dev,
		"TX IRQ: req %p actual/length %u/%u maxpacket %u packet size %u zero %u need zlp %u\n",
		 req, req->req.actual, req->req.length, ep->maxpacket,
		 req->packet_size, req->req.zero, need_zlp);

	/*
	 * Complete the request if all data has been sent and we don't need to
	 * transmit a zero length packet.
	 */
	if (req->req.actual == req->req.length && !need_zlp) {
		complete = req;
		list_del(&req->queue);

		if (ep->addr == 0)
			isp1760_udc_ctrl_send_status(ep, USB_DIR_IN);

		if (!list_empty(&ep->queue))
			req = list_first_entry(&ep->queue,
					       struct isp1760_request, queue);
		else
			req = NULL;
	}

	/*
	 * Transmit the next packet or start the next request, if any.
	 *
	 * TODO: If the endpoint is stalled the next request shouldn't be
	 * started, but what about the next packet ?
	 */
	if (req)
		isp1760_udc_transmit(ep, req);

	spin_unlock(&udc->lock);

	if (complete)
		isp1760_udc_request_complete(ep, complete, 0);
}

static int __isp1760_udc_set_halt(struct isp1760_ep *ep, bool halt)
{
	struct isp1760_udc *udc = ep->udc;

	dev_dbg(udc->isp->dev, "%s: %s halt on ep%02x\n", __func__,
		halt ? "set" : "clear", ep->addr);

	if (ep->desc && usb_endpoint_xfer_isoc(ep->desc)) {
		dev_dbg(udc->isp->dev, "%s: ep%02x is isochronous\n", __func__,
			ep->addr);
		return -EINVAL;
	}

	isp1760_udc_select_ep(udc, ep);

	if (halt)
		isp1760_udc_set(udc, DC_STALL);
	else
		isp1760_udc_clear(udc, DC_STALL);

	if (ep->addr == 0) {
		/* When halting the control endpoint, stall both IN and OUT. */
		__isp1760_udc_select_ep(udc, ep, USB_DIR_IN);
		if (halt)
			isp1760_udc_set(udc, DC_STALL);
		else
			isp1760_udc_clear(udc, DC_STALL);
	} else if (!halt) {
		/* Reset the data PID by cycling the endpoint enable bit. */
		isp1760_udc_clear(udc, DC_EPENABLE);
		isp1760_udc_set(udc, DC_EPENABLE);

		/*
		 * Disabling the endpoint emptied the transmit FIFO, fill it
		 * again if a request is pending.
		 *
		 * TODO: Does the gadget framework require synchronizatino with
		 * the TX IRQ handler ?
		 */
		if ((ep->addr & USB_DIR_IN) && !list_empty(&ep->queue)) {
			struct isp1760_request *req;

			req = list_first_entry(&ep->queue,
					       struct isp1760_request, queue);
			isp1760_udc_transmit(ep, req);
		}
	}

	ep->halted = halt;

	return 0;
}

/* -----------------------------------------------------------------------------
 * Control Endpoint
 */

static int isp1760_udc_get_status(struct isp1760_udc *udc,
				  const struct usb_ctrlrequest *req)
{
	struct isp1760_ep *ep;
	u16 status;

	if (req->wLength != cpu_to_le16(2) || req->wValue != cpu_to_le16(0))
		return -EINVAL;

	switch (req->bRequestType) {
	case USB_DIR_IN | USB_RECIP_DEVICE:
		status = udc->devstatus;
		break;

	case USB_DIR_IN | USB_RECIP_INTERFACE:
		status = 0;
		break;

	case USB_DIR_IN | USB_RECIP_ENDPOINT:
		ep = isp1760_udc_find_ep(udc, le16_to_cpu(req->wIndex));
		if (!ep)
			return -EINVAL;

		status = 0;
		if (ep->halted)
			status |= 1 << USB_ENDPOINT_HALT;
		break;

	default:
		return -EINVAL;
	}

	isp1760_udc_set(udc, DC_EPDIR);
	isp1760_udc_write(udc, DC_ENDPIDX, 1);

	isp1760_udc_write(udc, DC_BUFLEN, 2);

	isp1760_udc_write_raw16(udc, ISP176x_DC_DATAPORT, status);

	isp1760_udc_set(udc, DC_DSEN);

	dev_dbg(udc->isp->dev, "%s: status 0x%04x\n", __func__, status);

	return 0;
}

static int isp1760_udc_set_address(struct isp1760_udc *udc, u16 addr)
{
	if (addr > 127) {
		dev_dbg(udc->isp->dev, "invalid device address %u\n", addr);
		return -EINVAL;
	}

	if (udc->gadget.state != USB_STATE_DEFAULT &&
	    udc->gadget.state != USB_STATE_ADDRESS) {
		dev_dbg(udc->isp->dev, "can't set address in state %u\n",
			udc->gadget.state);
		return -EINVAL;
	}

	usb_gadget_set_state(&udc->gadget, addr ? USB_STATE_ADDRESS :
			     USB_STATE_DEFAULT);

	isp1760_udc_write(udc, DC_DEVADDR, addr);
	isp1760_udc_set(udc, DC_DEVEN);

	spin_lock(&udc->lock);
	isp1760_udc_ctrl_send_status(&udc->ep[0], USB_DIR_OUT);
	spin_unlock(&udc->lock);

	return 0;
}

static bool isp1760_ep0_setup_standard(struct isp1760_udc *udc,
				       struct usb_ctrlrequest *req)
{
	bool stall;

	switch (req->bRequest) {
	case USB_REQ_GET_STATUS:
		return isp1760_udc_get_status(udc, req);

	case USB_REQ_CLEAR_FEATURE:
		switch (req->bRequestType) {
		case USB_DIR_OUT | USB_RECIP_DEVICE: {
			/* TODO: Handle remote wakeup feature. */
			return true;
		}

		case USB_DIR_OUT | USB_RECIP_ENDPOINT: {
			u16 index = le16_to_cpu(req->wIndex);
			struct isp1760_ep *ep;

			if (req->wLength != cpu_to_le16(0) ||
			    req->wValue != cpu_to_le16(USB_ENDPOINT_HALT))
				return true;

			ep = isp1760_udc_find_ep(udc, index);
			if (!ep)
				return true;

			spin_lock(&udc->lock);

			/*
			 * If the endpoint is wedged only the gadget can clear
			 * the halt feature. Pretend success in that case, but
			 * keep the endpoint halted.
			 */
			if (!ep->wedged)
				stall = __isp1760_udc_set_halt(ep, false);
			else
				stall = false;

			if (!stall)
				isp1760_udc_ctrl_send_status(&udc->ep[0],
							     USB_DIR_OUT);

			spin_unlock(&udc->lock);
			return stall;
		}

		default:
			return true;
		}
		break;

	case USB_REQ_SET_FEATURE:
		switch (req->bRequestType) {
		case USB_DIR_OUT | USB_RECIP_DEVICE: {
			/* TODO: Handle remote wakeup and test mode features */
			return true;
		}

		case USB_DIR_OUT | USB_RECIP_ENDPOINT: {
			u16 index = le16_to_cpu(req->wIndex);
			struct isp1760_ep *ep;

			if (req->wLength != cpu_to_le16(0) ||
			    req->wValue != cpu_to_le16(USB_ENDPOINT_HALT))
				return true;

			ep = isp1760_udc_find_ep(udc, index);
			if (!ep)
				return true;

			spin_lock(&udc->lock);

			stall = __isp1760_udc_set_halt(ep, true);
			if (!stall)
				isp1760_udc_ctrl_send_status(&udc->ep[0],
							     USB_DIR_OUT);

			spin_unlock(&udc->lock);
			return stall;
		}

		default:
			return true;
		}
		break;

	case USB_REQ_SET_ADDRESS:
		if (req->bRequestType != (USB_DIR_OUT | USB_RECIP_DEVICE))
			return true;

		return isp1760_udc_set_address(udc, le16_to_cpu(req->wValue));

	case USB_REQ_SET_CONFIGURATION:
		if (req->bRequestType != (USB_DIR_OUT | USB_RECIP_DEVICE))
			return true;

		if (udc->gadget.state != USB_STATE_ADDRESS &&
		    udc->gadget.state != USB_STATE_CONFIGURED)
			return true;

		stall = udc->driver->setup(&udc->gadget, req) < 0;
		if (stall)
			return true;

		usb_gadget_set_state(&udc->gadget, req->wValue ?
				     USB_STATE_CONFIGURED : USB_STATE_ADDRESS);

		/*
		 * SET_CONFIGURATION (and SET_INTERFACE) must reset the halt
		 * feature on all endpoints. There is however no need to do so
		 * explicitly here as the gadget driver will disable and
		 * reenable endpoints, clearing the halt feature.
		 */
		return false;

	default:
		return udc->driver->setup(&udc->gadget, req) < 0;
	}
}

static void isp1760_ep0_setup(struct isp1760_udc *udc)
{
	union {
		struct usb_ctrlrequest r;
		u32 data[2];
	} req;
	unsigned int count;
	bool stall = false;

	spin_lock(&udc->lock);

	isp1760_udc_set(udc, DC_EP0SETUP);

	count = isp1760_udc_read(udc, DC_BUFLEN);
	if (count != sizeof(req)) {
		spin_unlock(&udc->lock);

		dev_err(udc->isp->dev, "invalid length %u for setup packet\n",
			count);

		isp1760_udc_ctrl_send_stall(&udc->ep[0]);
		return;
	}

	req.data[0] = isp1760_udc_read_raw(udc, ISP176x_DC_DATAPORT);
	req.data[1] = isp1760_udc_read_raw(udc, ISP176x_DC_DATAPORT);

	if (udc->ep0_state != ISP1760_CTRL_SETUP) {
		spin_unlock(&udc->lock);
		dev_dbg(udc->isp->dev, "unexpected SETUP packet\n");
		return;
	}

	/* Move to the data stage. */
	if (!req.r.wLength)
		udc->ep0_state = ISP1760_CTRL_STATUS;
	else if (req.r.bRequestType & USB_DIR_IN)
		udc->ep0_state = ISP1760_CTRL_DATA_IN;
	else
		udc->ep0_state = ISP1760_CTRL_DATA_OUT;

	udc->ep0_dir = req.r.bRequestType & USB_DIR_IN;
	udc->ep0_length = le16_to_cpu(req.r.wLength);

	spin_unlock(&udc->lock);

	dev_dbg(udc->isp->dev,
		"%s: bRequestType 0x%02x bRequest 0x%02x wValue 0x%04x wIndex 0x%04x wLength 0x%04x\n",
		__func__, req.r.bRequestType, req.r.bRequest,
		le16_to_cpu(req.r.wValue), le16_to_cpu(req.r.wIndex),
		le16_to_cpu(req.r.wLength));

	if ((req.r.bRequestType & USB_TYPE_MASK) == USB_TYPE_STANDARD)
		stall = isp1760_ep0_setup_standard(udc, &req.r);
	else
		stall = udc->driver->setup(&udc->gadget, &req.r) < 0;

	if (stall)
		isp1760_udc_ctrl_send_stall(&udc->ep[0]);
}

/* -----------------------------------------------------------------------------
 * Gadget Endpoint Operations
 */

static int isp1760_ep_enable(struct usb_ep *ep,
			     const struct usb_endpoint_descriptor *desc)
{
	struct isp1760_ep *uep = ep_to_udc_ep(ep);
	struct isp1760_udc *udc = uep->udc;
	unsigned long flags;
	unsigned int type;

	dev_dbg(uep->udc->isp->dev, "%s\n", __func__);

	/*
	 * Validate the descriptor. The control endpoint can't be enabled
	 * manually.
	 */
	if (desc->bDescriptorType != USB_DT_ENDPOINT ||
	    desc->bEndpointAddress == 0 ||
	    desc->bEndpointAddress != uep->addr ||
	    le16_to_cpu(desc->wMaxPacketSize) > ep->maxpacket) {
		dev_dbg(udc->isp->dev,
			"%s: invalid descriptor type %u addr %02x ep addr %02x max packet size %u/%u\n",
			__func__, desc->bDescriptorType,
			desc->bEndpointAddress, uep->addr,
			le16_to_cpu(desc->wMaxPacketSize), ep->maxpacket);
		return -EINVAL;
	}

	switch (usb_endpoint_type(desc)) {
	case USB_ENDPOINT_XFER_ISOC:
		type = ISP176x_DC_ENDPTYP_ISOC;
		break;
	case USB_ENDPOINT_XFER_BULK:
		type = ISP176x_DC_ENDPTYP_BULK;
		break;
	case USB_ENDPOINT_XFER_INT:
		type = ISP176x_DC_ENDPTYP_INTERRUPT;
		break;
	case USB_ENDPOINT_XFER_CONTROL:
	default:
		dev_dbg(udc->isp->dev, "%s: control endpoints unsupported\n",
			__func__);
		return -EINVAL;
	}

	spin_lock_irqsave(&udc->lock, flags);

	uep->desc = desc;
	uep->maxpacket = le16_to_cpu(desc->wMaxPacketSize);
	uep->rx_pending = false;
	uep->halted = false;
	uep->wedged = false;

	isp1760_udc_select_ep(udc, uep);

	isp1760_udc_write(udc, DC_FFOSZ, uep->maxpacket);
	isp1760_udc_write(udc, DC_BUFLEN, uep->maxpacket);

	isp1760_udc_write(udc, DC_ENDPTYP, type);
	isp1760_udc_set(udc, DC_EPENABLE);

	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;
}

static int isp1760_ep_disable(struct usb_ep *ep)
{
	struct isp1760_ep *uep = ep_to_udc_ep(ep);
	struct isp1760_udc *udc = uep->udc;
	struct isp1760_request *req, *nreq;
	LIST_HEAD(req_list);
	unsigned long flags;

	dev_dbg(udc->isp->dev, "%s\n", __func__);

	spin_lock_irqsave(&udc->lock, flags);

	if (!uep->desc) {
		dev_dbg(udc->isp->dev, "%s: endpoint not enabled\n", __func__);
		spin_unlock_irqrestore(&udc->lock, flags);
		return -EINVAL;
	}

	uep->desc = NULL;
	uep->maxpacket = 0;

	isp1760_udc_select_ep(udc, uep);
	isp1760_udc_clear(udc, DC_EPENABLE);
	isp1760_udc_clear(udc, DC_ENDPTYP);

	/* TODO Synchronize with the IRQ handler */

	list_splice_init(&uep->queue, &req_list);

	spin_unlock_irqrestore(&udc->lock, flags);

	list_for_each_entry_safe(req, nreq, &req_list, queue) {
		list_del(&req->queue);
		isp1760_udc_request_complete(uep, req, -ESHUTDOWN);
	}

	return 0;
}

static struct usb_request *isp1760_ep_alloc_request(struct usb_ep *ep,
						    gfp_t gfp_flags)
{
	struct isp1760_request *req;

	req = kzalloc(sizeof(*req), gfp_flags);
	if (!req)
		return NULL;

	return &req->req;
}

static void isp1760_ep_free_request(struct usb_ep *ep, struct usb_request *_req)
{
	struct isp1760_request *req = req_to_udc_req(_req);

	kfree(req);
}

static int isp1760_ep_queue(struct usb_ep *ep, struct usb_request *_req,
			    gfp_t gfp_flags)
{
	struct isp1760_request *req = req_to_udc_req(_req);
	struct isp1760_ep *uep = ep_to_udc_ep(ep);
	struct isp1760_udc *udc = uep->udc;
	bool complete = false;
	unsigned long flags;
	int ret = 0;

	_req->status = -EINPROGRESS;
	_req->actual = 0;

	spin_lock_irqsave(&udc->lock, flags);

	dev_dbg(udc->isp->dev,
		"%s: req %p (%u bytes%s) ep %p(0x%02x)\n", __func__, _req,
		_req->length, _req->zero ? " (zlp)" : "", uep, uep->addr);

	req->ep = uep;

	if (uep->addr == 0) {
		if (_req->length != udc->ep0_length &&
		    udc->ep0_state != ISP1760_CTRL_DATA_IN) {
			dev_dbg(udc->isp->dev,
				"%s: invalid length %u for req %p\n",
				__func__, _req->length, req);
			ret = -EINVAL;
			goto done;
		}

		switch (udc->ep0_state) {
		case ISP1760_CTRL_DATA_IN:
			dev_dbg(udc->isp->dev, "%s: transmitting req %p\n",
				__func__, req);

			list_add_tail(&req->queue, &uep->queue);
			isp1760_udc_transmit(uep, req);
			break;

		case ISP1760_CTRL_DATA_OUT:
			list_add_tail(&req->queue, &uep->queue);
			__isp1760_udc_select_ep(udc, uep, USB_DIR_OUT);
			isp1760_udc_set(udc, DC_DSEN);
			break;

		case ISP1760_CTRL_STATUS:
			complete = true;
			break;

		default:
			dev_dbg(udc->isp->dev, "%s: invalid ep0 state\n",
				__func__);
			ret = -EINVAL;
			break;
		}
	} else if (uep->desc) {
		bool empty = list_empty(&uep->queue);

		list_add_tail(&req->queue, &uep->queue);
		if ((uep->addr & USB_DIR_IN) && !uep->halted && empty)
			isp1760_udc_transmit(uep, req);
		else if (!(uep->addr & USB_DIR_IN) && uep->rx_pending)
			complete = isp1760_udc_receive(uep, req);
	} else {
		dev_dbg(udc->isp->dev,
			"%s: can't queue request to disabled ep%02x\n",
			__func__, uep->addr);
		ret = -ESHUTDOWN;
	}

done:
	if (ret < 0)
		req->ep = NULL;

	spin_unlock_irqrestore(&udc->lock, flags);

	if (complete)
		isp1760_udc_request_complete(uep, req, 0);

	return ret;
}

static int isp1760_ep_dequeue(struct usb_ep *ep, struct usb_request *_req)
{
	struct isp1760_request *req = req_to_udc_req(_req);
	struct isp1760_ep *uep = ep_to_udc_ep(ep);
	struct isp1760_udc *udc = uep->udc;
	unsigned long flags;

	dev_dbg(uep->udc->isp->dev, "%s(ep%02x)\n", __func__, uep->addr);

	spin_lock_irqsave(&udc->lock, flags);

	if (req->ep != uep)
		req = NULL;
	else
		list_del(&req->queue);

	spin_unlock_irqrestore(&udc->lock, flags);

	if (!req)
		return -EINVAL;

	isp1760_udc_request_complete(uep, req, -ECONNRESET);
	return 0;
}

static int __isp1760_ep_set_halt(struct isp1760_ep *uep, bool stall, bool wedge)
{
	struct isp1760_udc *udc = uep->udc;
	int ret;

	if (!uep->addr) {
		/*
		 * Halting the control endpoint is only valid as a delayed error
		 * response to a SETUP packet. Make sure EP0 is in the right
		 * stage and that the gadget isn't trying to clear the halt
		 * condition.
		 */
		if (WARN_ON(udc->ep0_state == ISP1760_CTRL_SETUP || !stall ||
			     wedge)) {
			return -EINVAL;
		}
	}

	if (uep->addr && !uep->desc) {
		dev_dbg(udc->isp->dev, "%s: ep%02x is disabled\n", __func__,
			uep->addr);
		return -EINVAL;
	}

	if (uep->addr & USB_DIR_IN) {
		/* Refuse to halt IN endpoints with active transfers. */
		if (!list_empty(&uep->queue)) {
			dev_dbg(udc->isp->dev,
				"%s: ep%02x has request pending\n", __func__,
				uep->addr);
			return -EAGAIN;
		}
	}

	ret = __isp1760_udc_set_halt(uep, stall);
	if (ret < 0)
		return ret;

	if (!uep->addr) {
		/*
		 * Stalling EP0 completes the control transaction, move back to
		 * the SETUP state.
		 */
		udc->ep0_state = ISP1760_CTRL_SETUP;
		return 0;
	}

	if (wedge)
		uep->wedged = true;
	else if (!stall)
		uep->wedged = false;

	return 0;
}

static int isp1760_ep_set_halt(struct usb_ep *ep, int value)
{
	struct isp1760_ep *uep = ep_to_udc_ep(ep);
	unsigned long flags;
	int ret;

	dev_dbg(uep->udc->isp->dev, "%s: %s halt on ep%02x\n", __func__,
		value ? "set" : "clear", uep->addr);

	spin_lock_irqsave(&uep->udc->lock, flags);
	ret = __isp1760_ep_set_halt(uep, value, false);
	spin_unlock_irqrestore(&uep->udc->lock, flags);

	return ret;
}

static int isp1760_ep_set_wedge(struct usb_ep *ep)
{
	struct isp1760_ep *uep = ep_to_udc_ep(ep);
	unsigned long flags;
	int ret;

	dev_dbg(uep->udc->isp->dev, "%s: set wedge on ep%02x)\n", __func__,
		uep->addr);

	spin_lock_irqsave(&uep->udc->lock, flags);
	ret = __isp1760_ep_set_halt(uep, true, true);
	spin_unlock_irqrestore(&uep->udc->lock, flags);

	return ret;
}

static void isp1760_ep_fifo_flush(struct usb_ep *ep)
{
	struct isp1760_ep *uep = ep_to_udc_ep(ep);
	struct isp1760_udc *udc = uep->udc;
	unsigned long flags;

	spin_lock_irqsave(&udc->lock, flags);

	isp1760_udc_select_ep(udc, uep);

	/*
	 * Set the CLBUF bit twice to flush both buffers in case double
	 * buffering is enabled.
	 */
	isp1760_udc_set(udc, DC_CLBUF);
	isp1760_udc_set(udc, DC_CLBUF);

	spin_unlock_irqrestore(&udc->lock, flags);
}

static const struct usb_ep_ops isp1760_ep_ops = {
	.enable = isp1760_ep_enable,
	.disable = isp1760_ep_disable,
	.alloc_request = isp1760_ep_alloc_request,
	.free_request = isp1760_ep_free_request,
	.queue = isp1760_ep_queue,
	.dequeue = isp1760_ep_dequeue,
	.set_halt = isp1760_ep_set_halt,
	.set_wedge = isp1760_ep_set_wedge,
	.fifo_flush = isp1760_ep_fifo_flush,
};

/* -----------------------------------------------------------------------------
 * Device States
 */

/* Called with the UDC spinlock held. */
static void isp1760_udc_connect(struct isp1760_udc *udc)
{
	usb_gadget_set_state(&udc->gadget, USB_STATE_POWERED);
	mod_timer(&udc->vbus_timer, jiffies + ISP1760_VBUS_POLL_INTERVAL);
}

/* Called with the UDC spinlock held. */
static void isp1760_udc_disconnect(struct isp1760_udc *udc)
{
	if (udc->gadget.state < USB_STATE_POWERED)
		return;

	dev_dbg(udc->isp->dev, "Device disconnected in state %u\n",
		 udc->gadget.state);

	udc->gadget.speed = USB_SPEED_UNKNOWN;
	usb_gadget_set_state(&udc->gadget, USB_STATE_ATTACHED);

	if (udc->driver->disconnect)
		udc->driver->disconnect(&udc->gadget);

	timer_delete(&udc->vbus_timer);

	/* TODO Reset all endpoints ? */
}

static void isp1760_udc_init_hw(struct isp1760_udc *udc)
{
	u32 intconf = udc->is_isp1763 ? ISP1763_DC_INTCONF : ISP176x_DC_INTCONF;
	u32 intena = udc->is_isp1763 ? ISP1763_DC_INTENABLE :
						ISP176x_DC_INTENABLE;

	/*
	 * The device controller currently shares its interrupt with the host
	 * controller, the DC_IRQ polarity and signaling mode are ignored. Set
	 * the to active-low level-triggered.
	 *
	 * Configure the control, in and out pipes to generate interrupts on
	 * ACK tokens only (and NYET for the out pipe). The default
	 * configuration also generates an interrupt on the first NACK token.
	 */
	isp1760_reg_write(udc->regs, intconf,
			  ISP176x_DC_CDBGMOD_ACK | ISP176x_DC_DDBGMODIN_ACK |
			  ISP176x_DC_DDBGMODOUT_ACK);

	isp1760_reg_write(udc->regs, intena, DC_IEPRXTX(7) |
			  DC_IEPRXTX(6) | DC_IEPRXTX(5) | DC_IEPRXTX(4) |
			  DC_IEPRXTX(3) | DC_IEPRXTX(2) | DC_IEPRXTX(1) |
			  DC_IEPRXTX(0) | ISP176x_DC_IEP0SETUP |
			  ISP176x_DC_IEVBUS | ISP176x_DC_IERESM |
			  ISP176x_DC_IESUSP | ISP176x_DC_IEHS_STA |
			  ISP176x_DC_IEBRST);

	if (udc->connected)
		isp1760_set_pullup(udc->isp, true);

	isp1760_udc_set(udc, DC_DEVEN);
}

static void isp1760_udc_reset(struct isp1760_udc *udc)
{
	unsigned long flags;

	spin_lock_irqsave(&udc->lock, flags);

	/*
	 * The bus reset has reset most registers to their default value,
	 * reinitialize the UDC hardware.
	 */
	isp1760_udc_init_hw(udc);

	udc->ep0_state = ISP1760_CTRL_SETUP;
	udc->gadget.speed = USB_SPEED_FULL;

	usb_gadget_udc_reset(&udc->gadget, udc->driver);

	spin_unlock_irqrestore(&udc->lock, flags);
}

static void isp1760_udc_suspend(struct isp1760_udc *udc)
{
	if (udc->gadget.state < USB_STATE_DEFAULT)
		return;

	if (udc->driver->suspend)
		udc->driver->suspend(&udc->gadget);
}

static void isp1760_udc_resume(struct isp1760_udc *udc)
{
	if (udc->gadget.state < USB_STATE_DEFAULT)
		return;

	if (udc->driver->resume)
		udc->driver->resume(&udc->gadget);
}

/* -----------------------------------------------------------------------------
 * Gadget Operations
 */

static int isp1760_udc_get_frame(struct usb_gadget *gadget)
{
	struct isp1760_udc *udc = gadget_to_udc(gadget);

	return isp1760_udc_read(udc, DC_FRAMENUM);
}

static int isp1760_udc_wakeup(struct usb_gadget *gadget)
{
	struct isp1760_udc *udc = gadget_to_udc(gadget);

	dev_dbg(udc->isp->dev, "%s\n", __func__);
	return -ENOTSUPP;
}

static int isp1760_udc_set_selfpowered(struct usb_gadget *gadget,
				       int is_selfpowered)
{
	struct isp1760_udc *udc = gadget_to_udc(gadget);

	if (is_selfpowered)
		udc->devstatus |= 1 << USB_DEVICE_SELF_POWERED;
	else
		udc->devstatus &= ~(1 << USB_DEVICE_SELF_POWERED);

	return 0;
}

static int isp1760_udc_pullup(struct usb_gadget *gadget, int is_on)
{
	struct isp1760_udc *udc = gadget_to_udc(gadget);

	isp1760_set_pullup(udc->isp, is_on);
	udc->connected = is_on;

	return 0;
}

static int isp1760_udc_start(struct usb_gadget *gadget,
			     struct usb_gadget_driver *driver)
{
	struct isp1760_udc *udc = gadget_to_udc(gadget);
	unsigned long flags;

	/* The hardware doesn't support low speed. */
	if (driver->max_speed < USB_SPEED_FULL) {
		dev_err(udc->isp->dev, "Invalid gadget driver\n");
		return -EINVAL;
	}

	spin_lock_irqsave(&udc->lock, flags);

	if (udc->driver) {
		dev_err(udc->isp->dev, "UDC already has a gadget driver\n");
		spin_unlock_irqrestore(&udc->lock, flags);
		return -EBUSY;
	}

	udc->driver = driver;

	spin_unlock_irqrestore(&udc->lock, flags);

	dev_dbg(udc->isp->dev, "starting UDC with driver %s\n",
		driver->function);

	udc->devstatus = 0;
	udc->connected = true;

	usb_gadget_set_state(&udc->gadget, USB_STATE_ATTACHED);

	/* DMA isn't supported yet, don't enable the DMA clock. */
	isp1760_udc_set(udc, DC_GLINTENA);

	isp1760_udc_init_hw(udc);

	dev_dbg(udc->isp->dev, "UDC started with driver %s\n",
		driver->function);

	return 0;
}

static int isp1760_udc_stop(struct usb_gadget *gadget)
{
	struct isp1760_udc *udc = gadget_to_udc(gadget);
	u32 mode_reg = udc->is_isp1763 ? ISP1763_DC_MODE : ISP176x_DC_MODE;
	unsigned long flags;

	dev_dbg(udc->isp->dev, "%s\n", __func__);

	timer_delete_sync(&udc->vbus_timer);

	isp1760_reg_write(udc->regs, mode_reg, 0);

	spin_lock_irqsave(&udc->lock, flags);
	udc->driver = NULL;
	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;
}

static const struct usb_gadget_ops isp1760_udc_ops = {
	.get_frame = isp1760_udc_get_frame,
	.wakeup = isp1760_udc_wakeup,
	.set_selfpowered = isp1760_udc_set_selfpowered,
	.pullup = isp1760_udc_pullup,
	.udc_start = isp1760_udc_start,
	.udc_stop = isp1760_udc_stop,
};

/* -----------------------------------------------------------------------------
 * Interrupt Handling
 */

static u32 isp1760_udc_irq_get_status(struct isp1760_udc *udc)
{
	u32 status;

	if (udc->is_isp1763) {
		status = isp1760_reg_read(udc->regs, ISP1763_DC_INTERRUPT)
			& isp1760_reg_read(udc->regs, ISP1763_DC_INTENABLE);
		isp1760_reg_write(udc->regs, ISP1763_DC_INTERRUPT, status);
	} else {
		status = isp1760_reg_read(udc->regs, ISP176x_DC_INTERRUPT)
			& isp1760_reg_read(udc->regs, ISP176x_DC_INTENABLE);
		isp1760_reg_write(udc->regs, ISP176x_DC_INTERRUPT, status);
	}

	return status;
}

static irqreturn_t isp1760_udc_irq(int irq, void *dev)
{
	struct isp1760_udc *udc = dev;
	unsigned int i;
	u32 status;

	status = isp1760_udc_irq_get_status(udc);

	if (status & ISP176x_DC_IEVBUS) {
		dev_dbg(udc->isp->dev, "%s(VBUS)\n", __func__);
		/* The VBUS interrupt is only triggered when VBUS appears. */
		spin_lock(&udc->lock);
		isp1760_udc_connect(udc);
		spin_unlock(&udc->lock);
	}

	if (status & ISP176x_DC_IEBRST) {
		dev_dbg(udc->isp->dev, "%s(BRST)\n", __func__);

		isp1760_udc_reset(udc);
	}

	for (i = 0; i <= 7; ++i) {
		struct isp1760_ep *ep = &udc->ep[i*2];

		if (status & DC_IEPTX(i)) {
			dev_dbg(udc->isp->dev, "%s(EPTX%u)\n", __func__, i);
			isp1760_ep_tx_complete(ep);
		}

		if (status & DC_IEPRX(i)) {
			dev_dbg(udc->isp->dev, "%s(EPRX%u)\n", __func__, i);
			isp1760_ep_rx_ready(i ? ep - 1 : ep);
		}
	}

	if (status & ISP176x_DC_IEP0SETUP) {
		dev_dbg(udc->isp->dev, "%s(EP0SETUP)\n", __func__);

		isp1760_ep0_setup(udc);
	}

	if (status & ISP176x_DC_IERESM) {
		dev_dbg(udc->isp->dev, "%s(RESM)\n", __func__);
		isp1760_udc_resume(udc);
	}

	if (status & ISP176x_DC_IESUSP) {
		dev_dbg(udc->isp->dev, "%s(SUSP)\n", __func__);

		spin_lock(&udc->lock);
		if (!isp1760_udc_is_set(udc, DC_VBUSSTAT))
			isp1760_udc_disconnect(udc);
		else
			isp1760_udc_suspend(udc);
		spin_unlock(&udc->lock);
	}

	if (status & ISP176x_DC_IEHS_STA) {
		dev_dbg(udc->isp->dev, "%s(HS_STA)\n", __func__);
		udc->gadget.speed = USB_SPEED_HIGH;
	}

	return status ? IRQ_HANDLED : IRQ_NONE;
}

static void isp1760_udc_vbus_poll(struct timer_list *t)
{
	struct isp1760_udc *udc = from_timer(udc, t, vbus_timer);
	unsigned long flags;

	spin_lock_irqsave(&udc->lock, flags);

	if (!(isp1760_udc_is_set(udc, DC_VBUSSTAT)))
		isp1760_udc_disconnect(udc);
	else if (udc->gadget.state >= USB_STATE_POWERED)
		mod_timer(&udc->vbus_timer,
			  jiffies + ISP1760_VBUS_POLL_INTERVAL);

	spin_unlock_irqrestore(&udc->lock, flags);
}

/* -----------------------------------------------------------------------------
 * Registration
 */

static void isp1760_udc_init_eps(struct isp1760_udc *udc)
{
	unsigned int i;

	INIT_LIST_HEAD(&udc->gadget.ep_list);

	for (i = 0; i < ARRAY_SIZE(udc->ep); ++i) {
		struct isp1760_ep *ep = &udc->ep[i];
		unsigned int ep_num = (i + 1) / 2;
		bool is_in = !(i & 1);

		ep->udc = udc;

		INIT_LIST_HEAD(&ep->queue);

		ep->addr = (ep_num && is_in ? USB_DIR_IN : USB_DIR_OUT)
			 | ep_num;
		ep->desc = NULL;

		sprintf(ep->name, "ep%u%s", ep_num,
			ep_num ? (is_in ? "in" : "out") : "");

		ep->ep.ops = &isp1760_ep_ops;
		ep->ep.name = ep->name;

		/*
		 * Hardcode the maximum packet sizes for now, to 64 bytes for
		 * the control endpoint and 512 bytes for all other endpoints.
		 * This fits in the 8kB FIFO without double-buffering.
		 */
		if (ep_num == 0) {
			usb_ep_set_maxpacket_limit(&ep->ep, 64);
			ep->ep.caps.type_control = true;
			ep->ep.caps.dir_in = true;
			ep->ep.caps.dir_out = true;
			ep->maxpacket = 64;
			udc->gadget.ep0 = &ep->ep;
		} else {
			usb_ep_set_maxpacket_limit(&ep->ep, 512);
			ep->ep.caps.type_iso = true;
			ep->ep.caps.type_bulk = true;
			ep->ep.caps.type_int = true;
			ep->maxpacket = 0;
			list_add_tail(&ep->ep.ep_list, &udc->gadget.ep_list);
		}

		if (is_in)
			ep->ep.caps.dir_in = true;
		else
			ep->ep.caps.dir_out = true;
	}
}

static int isp1760_udc_init(struct isp1760_udc *udc)
{
	u32 mode_reg = udc->is_isp1763 ? ISP1763_DC_MODE : ISP176x_DC_MODE;
	u16 scratch;
	u32 chipid;

	/*
	 * Check that the controller is present by writing to the scratch
	 * register, modifying the bus pattern by reading from the chip ID
	 * register, and reading the scratch register value back. The chip ID
	 * and scratch register contents must match the expected values.
	 */
	isp1760_udc_write(udc, DC_SCRATCH, 0xbabe);
	chipid = isp1760_udc_read(udc, DC_CHIP_ID_HIGH) << 16;
	chipid |= isp1760_udc_read(udc, DC_CHIP_ID_LOW);
	scratch = isp1760_udc_read(udc, DC_SCRATCH);

	if (scratch != 0xbabe) {
		dev_err(udc->isp->dev,
			"udc: scratch test failed (0x%04x/0x%08x)\n",
			scratch, chipid);
		return -ENODEV;
	}

	if (chipid != 0x00011582 && chipid != 0x00158210 &&
	    chipid != 0x00176320) {
		dev_err(udc->isp->dev, "udc: invalid chip ID 0x%08x\n", chipid);
		return -ENODEV;
	}

	/* Reset the device controller. */
	isp1760_udc_set(udc, DC_SFRESET);
	usleep_range(10000, 11000);
	isp1760_reg_write(udc->regs, mode_reg, 0);
	usleep_range(10000, 11000);

	return 0;
}

int isp1760_udc_register(struct isp1760_device *isp, int irq,
			 unsigned long irqflags)
{
	struct isp1760_udc *udc = &isp->udc;
	int ret;

	udc->irq = -1;
	udc->isp = isp;

	spin_lock_init(&udc->lock);
	timer_setup(&udc->vbus_timer, isp1760_udc_vbus_poll, 0);

	ret = isp1760_udc_init(udc);
	if (ret < 0)
		return ret;

	udc->irqname = kasprintf(GFP_KERNEL, "%s (udc)", dev_name(isp->dev));
	if (!udc->irqname)
		return -ENOMEM;

	ret = request_irq(irq, isp1760_udc_irq, IRQF_SHARED | irqflags,
			  udc->irqname, udc);
	if (ret < 0)
		goto error;

	udc->irq = irq;

	/*
	 * Initialize the gadget static fields and register its device. Gadget
	 * fields that vary during the life time of the gadget are initialized
	 * by the UDC core.
	 */
	udc->gadget.ops = &isp1760_udc_ops;
	udc->gadget.speed = USB_SPEED_UNKNOWN;
	udc->gadget.max_speed = USB_SPEED_HIGH;
	udc->gadget.name = "isp1761_udc";

	isp1760_udc_init_eps(udc);

	ret = usb_add_gadget_udc(isp->dev, &udc->gadget);
	if (ret < 0)
		goto error;

	return 0;

error:
	if (udc->irq >= 0)
		free_irq(udc->irq, udc);
	kfree(udc->irqname);

	return ret;
}

void isp1760_udc_unregister(struct isp1760_device *isp)
{
	struct isp1760_udc *udc = &isp->udc;

	if (!udc->isp)
		return;

	usb_del_gadget_udc(&udc->gadget);

	free_irq(udc->irq, udc);
	kfree(udc->irqname);
}
