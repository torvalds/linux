/**
 * ep0.c - DesignWare USB3 DRD Controller Endpoint 0 Handling
 *
 * Copyright (C) 2010-2011 Texas Instruments Incorporated - http://www.ti.com
 *
 * Authors: Felipe Balbi <balbi@ti.com>,
 *	    Sebastian Andrzej Siewior <bigeasy@linutronix.de>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the above-listed copyright holders may not be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2, as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/list.h>
#include <linux/dma-mapping.h>

#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/usb/composite.h>

#include "core.h"
#include "gadget.h"
#include "io.h"

static void dwc3_ep0_do_control_status(struct dwc3 *dwc, u32 epnum);

static const char *dwc3_ep0_state_string(enum dwc3_ep0_state state)
{
	switch (state) {
	case EP0_UNCONNECTED:
		return "Unconnected";
	case EP0_SETUP_PHASE:
		return "Setup Phase";
	case EP0_DATA_PHASE:
		return "Data Phase";
	case EP0_STATUS_PHASE:
		return "Status Phase";
	default:
		return "UNKNOWN";
	}
}

static int dwc3_ep0_start_trans(struct dwc3 *dwc, u8 epnum, dma_addr_t buf_dma,
		u32 len, u32 type)
{
	struct dwc3_gadget_ep_cmd_params params;
	struct dwc3_trb_hw		*trb_hw;
	struct dwc3_trb			trb;
	struct dwc3_ep			*dep;

	int				ret;

	dep = dwc->eps[epnum];
	if (dep->flags & DWC3_EP_BUSY) {
		dev_vdbg(dwc->dev, "%s: still busy\n", dep->name);
		return 0;
	}

	trb_hw = dwc->ep0_trb;
	memset(&trb, 0, sizeof(trb));

	trb.trbctl = type;
	trb.bplh = buf_dma;
	trb.length = len;

	trb.hwo	= 1;
	trb.lst	= 1;
	trb.ioc	= 1;
	trb.isp_imi = 1;

	dwc3_trb_to_hw(&trb, trb_hw);

	memset(&params, 0, sizeof(params));
	params.param0 = upper_32_bits(dwc->ep0_trb_addr);
	params.param1 = lower_32_bits(dwc->ep0_trb_addr);

	ret = dwc3_send_gadget_ep_cmd(dwc, dep->number,
			DWC3_DEPCMD_STARTTRANSFER, &params);
	if (ret < 0) {
		dev_dbg(dwc->dev, "failed to send STARTTRANSFER command\n");
		return ret;
	}

	dep->flags |= DWC3_EP_BUSY;
	dep->res_trans_idx = dwc3_gadget_ep_get_transfer_index(dwc,
			dep->number);

	dwc->ep0_next_event = DWC3_EP0_COMPLETE;

	return 0;
}

static int __dwc3_gadget_ep0_queue(struct dwc3_ep *dep,
		struct dwc3_request *req)
{
	struct dwc3		*dwc = dep->dwc;
	int			ret = 0;

	req->request.actual	= 0;
	req->request.status	= -EINPROGRESS;
	req->epnum		= dep->number;

	list_add_tail(&req->list, &dep->request_list);

	/*
	 * Gadget driver might not be quick enough to queue a request
	 * before we get a Transfer Not Ready event on this endpoint.
	 *
	 * In that case, we will set DWC3_EP_PENDING_REQUEST. When that
	 * flag is set, it's telling us that as soon as Gadget queues the
	 * required request, we should kick the transfer here because the
	 * IRQ we were waiting for is long gone.
	 */
	if (dep->flags & DWC3_EP_PENDING_REQUEST) {
		unsigned	direction;

		direction = !!(dep->flags & DWC3_EP0_DIR_IN);

		if (dwc->ep0state != EP0_DATA_PHASE) {
			dev_WARN(dwc->dev, "Unexpected pending request\n");
			return 0;
		}

		ret = dwc3_ep0_start_trans(dwc, direction,
				req->request.dma, req->request.length,
				DWC3_TRBCTL_CONTROL_DATA);
		dep->flags &= ~(DWC3_EP_PENDING_REQUEST |
				DWC3_EP0_DIR_IN);
	} else if (dwc->delayed_status) {
		dwc->delayed_status = false;

		if (dwc->ep0state == EP0_STATUS_PHASE)
			dwc3_ep0_do_control_status(dwc, 1);
		else
			dev_dbg(dwc->dev, "too early for delayed status\n");
	}

	return ret;
}

int dwc3_gadget_ep0_queue(struct usb_ep *ep, struct usb_request *request,
		gfp_t gfp_flags)
{
	struct dwc3_request		*req = to_dwc3_request(request);
	struct dwc3_ep			*dep = to_dwc3_ep(ep);
	struct dwc3			*dwc = dep->dwc;

	unsigned long			flags;

	int				ret;

	spin_lock_irqsave(&dwc->lock, flags);
	if (!dep->desc) {
		dev_dbg(dwc->dev, "trying to queue request %p to disabled %s\n",
				request, dep->name);
		ret = -ESHUTDOWN;
		goto out;
	}

	/* we share one TRB for ep0/1 */
	if (!list_empty(&dep->request_list)) {
		ret = -EBUSY;
		goto out;
	}

	dev_vdbg(dwc->dev, "queueing request %p to %s length %d, state '%s'\n",
			request, dep->name, request->length,
			dwc3_ep0_state_string(dwc->ep0state));

	ret = __dwc3_gadget_ep0_queue(dep, req);

out:
	spin_unlock_irqrestore(&dwc->lock, flags);

	return ret;
}

static void dwc3_ep0_stall_and_restart(struct dwc3 *dwc)
{
	struct dwc3_ep		*dep = dwc->eps[0];

	/* stall is always issued on EP0 */
	__dwc3_gadget_ep_set_halt(dep, 1);
	dep->flags = DWC3_EP_ENABLED;
	dwc->delayed_status = false;

	if (!list_empty(&dep->request_list)) {
		struct dwc3_request	*req;

		req = next_request(&dep->request_list);
		dwc3_gadget_giveback(dep, req, -ECONNRESET);
	}

	dwc->ep0state = EP0_SETUP_PHASE;
	dwc3_ep0_out_start(dwc);
}

void dwc3_ep0_out_start(struct dwc3 *dwc)
{
	int				ret;

	ret = dwc3_ep0_start_trans(dwc, 0, dwc->ctrl_req_addr, 8,
			DWC3_TRBCTL_CONTROL_SETUP);
	WARN_ON(ret < 0);
}

static struct dwc3_ep *dwc3_wIndex_to_dep(struct dwc3 *dwc, __le16 wIndex_le)
{
	struct dwc3_ep		*dep;
	u32			windex = le16_to_cpu(wIndex_le);
	u32			epnum;

	epnum = (windex & USB_ENDPOINT_NUMBER_MASK) << 1;
	if ((windex & USB_ENDPOINT_DIR_MASK) == USB_DIR_IN)
		epnum |= 1;

	dep = dwc->eps[epnum];
	if (dep->flags & DWC3_EP_ENABLED)
		return dep;

	return NULL;
}

static void dwc3_ep0_status_cmpl(struct usb_ep *ep, struct usb_request *req)
{
}
/*
 * ch 9.4.5
 */
static int dwc3_ep0_handle_status(struct dwc3 *dwc,
		struct usb_ctrlrequest *ctrl)
{
	struct dwc3_ep		*dep;
	u32			recip;
	u16			usb_status = 0;
	__le16			*response_pkt;

	recip = ctrl->bRequestType & USB_RECIP_MASK;
	switch (recip) {
	case USB_RECIP_DEVICE:
		/*
		 * We are self-powered. U1/U2/LTM will be set later
		 * once we handle this states. RemoteWakeup is 0 on SS
		 */
		usb_status |= dwc->is_selfpowered << USB_DEVICE_SELF_POWERED;
		break;

	case USB_RECIP_INTERFACE:
		/*
		 * Function Remote Wake Capable	D0
		 * Function Remote Wakeup	D1
		 */
		break;

	case USB_RECIP_ENDPOINT:
		dep = dwc3_wIndex_to_dep(dwc, ctrl->wIndex);
		if (!dep)
			return -EINVAL;

		if (dep->flags & DWC3_EP_STALL)
			usb_status = 1 << USB_ENDPOINT_HALT;
		break;
	default:
		return -EINVAL;
	};

	response_pkt = (__le16 *) dwc->setup_buf;
	*response_pkt = cpu_to_le16(usb_status);

	dep = dwc->eps[0];
	dwc->ep0_usb_req.dep = dep;
	dwc->ep0_usb_req.request.length = sizeof(*response_pkt);
	dwc->ep0_usb_req.request.dma = dwc->setup_buf_addr;
	dwc->ep0_usb_req.request.complete = dwc3_ep0_status_cmpl;

	return __dwc3_gadget_ep0_queue(dep, &dwc->ep0_usb_req);
}

static int dwc3_ep0_handle_feature(struct dwc3 *dwc,
		struct usb_ctrlrequest *ctrl, int set)
{
	struct dwc3_ep		*dep;
	u32			recip;
	u32			wValue;
	u32			wIndex;
	u32			reg;
	int			ret;
	u32			mode;

	wValue = le16_to_cpu(ctrl->wValue);
	wIndex = le16_to_cpu(ctrl->wIndex);
	recip = ctrl->bRequestType & USB_RECIP_MASK;
	switch (recip) {
	case USB_RECIP_DEVICE:

		/*
		 * 9.4.1 says only only for SS, in AddressState only for
		 * default control pipe
		 */
		switch (wValue) {
		case USB_DEVICE_U1_ENABLE:
		case USB_DEVICE_U2_ENABLE:
		case USB_DEVICE_LTM_ENABLE:
			if (dwc->dev_state != DWC3_CONFIGURED_STATE)
				return -EINVAL;
			if (dwc->speed != DWC3_DSTS_SUPERSPEED)
				return -EINVAL;
		}

		/* XXX add U[12] & LTM */
		switch (wValue) {
		case USB_DEVICE_REMOTE_WAKEUP:
			break;
		case USB_DEVICE_U1_ENABLE:
			break;
		case USB_DEVICE_U2_ENABLE:
			break;
		case USB_DEVICE_LTM_ENABLE:
			break;

		case USB_DEVICE_TEST_MODE:
			if ((wIndex & 0xff) != 0)
				return -EINVAL;
			if (!set)
				return -EINVAL;

			mode = wIndex >> 8;
			reg = dwc3_readl(dwc->regs, DWC3_DCTL);
			reg &= ~DWC3_DCTL_TSTCTRL_MASK;

			switch (mode) {
			case TEST_J:
			case TEST_K:
			case TEST_SE0_NAK:
			case TEST_PACKET:
			case TEST_FORCE_EN:
				reg |= mode << 1;
				break;
			default:
				return -EINVAL;
			}
			dwc3_writel(dwc->regs, DWC3_DCTL, reg);
			break;
		default:
			return -EINVAL;
		}
		break;

	case USB_RECIP_INTERFACE:
		switch (wValue) {
		case USB_INTRF_FUNC_SUSPEND:
			if (wIndex & USB_INTRF_FUNC_SUSPEND_LP)
				/* XXX enable Low power suspend */
				;
			if (wIndex & USB_INTRF_FUNC_SUSPEND_RW)
				/* XXX enable remote wakeup */
				;
			break;
		default:
			return -EINVAL;
		}
		break;

	case USB_RECIP_ENDPOINT:
		switch (wValue) {
		case USB_ENDPOINT_HALT:
			dep =  dwc3_wIndex_to_dep(dwc, wIndex);
			if (!dep)
				return -EINVAL;
			ret = __dwc3_gadget_ep_set_halt(dep, set);
			if (ret)
				return -EINVAL;
			break;
		default:
			return -EINVAL;
		}
		break;

	default:
		return -EINVAL;
	};

	return 0;
}

static int dwc3_ep0_set_address(struct dwc3 *dwc, struct usb_ctrlrequest *ctrl)
{
	u32 addr;
	u32 reg;

	addr = le16_to_cpu(ctrl->wValue);
	if (addr > 127) {
		dev_dbg(dwc->dev, "invalid device address %d\n", addr);
		return -EINVAL;
	}

	if (dwc->dev_state == DWC3_CONFIGURED_STATE) {
		dev_dbg(dwc->dev, "trying to set address when configured\n");
		return -EINVAL;
	}

	reg = dwc3_readl(dwc->regs, DWC3_DCFG);
	reg &= ~(DWC3_DCFG_DEVADDR_MASK);
	reg |= DWC3_DCFG_DEVADDR(addr);
	dwc3_writel(dwc->regs, DWC3_DCFG, reg);

	if (addr)
		dwc->dev_state = DWC3_ADDRESS_STATE;
	else
		dwc->dev_state = DWC3_DEFAULT_STATE;

	return 0;
}

static int dwc3_ep0_delegate_req(struct dwc3 *dwc, struct usb_ctrlrequest *ctrl)
{
	int ret;

	spin_unlock(&dwc->lock);
	ret = dwc->gadget_driver->setup(&dwc->gadget, ctrl);
	spin_lock(&dwc->lock);
	return ret;
}

static int dwc3_ep0_set_config(struct dwc3 *dwc, struct usb_ctrlrequest *ctrl)
{
	u32 cfg;
	int ret;

	dwc->start_config_issued = false;
	cfg = le16_to_cpu(ctrl->wValue);

	switch (dwc->dev_state) {
	case DWC3_DEFAULT_STATE:
		return -EINVAL;
		break;

	case DWC3_ADDRESS_STATE:
		ret = dwc3_ep0_delegate_req(dwc, ctrl);
		/* if the cfg matches and the cfg is non zero */
		if (!ret && cfg)
			dwc->dev_state = DWC3_CONFIGURED_STATE;
		break;

	case DWC3_CONFIGURED_STATE:
		ret = dwc3_ep0_delegate_req(dwc, ctrl);
		if (!cfg)
			dwc->dev_state = DWC3_ADDRESS_STATE;
		break;
	default:
		ret = -EINVAL;
	}
	return ret;
}

static int dwc3_ep0_std_request(struct dwc3 *dwc, struct usb_ctrlrequest *ctrl)
{
	int ret;

	switch (ctrl->bRequest) {
	case USB_REQ_GET_STATUS:
		dev_vdbg(dwc->dev, "USB_REQ_GET_STATUS\n");
		ret = dwc3_ep0_handle_status(dwc, ctrl);
		break;
	case USB_REQ_CLEAR_FEATURE:
		dev_vdbg(dwc->dev, "USB_REQ_CLEAR_FEATURE\n");
		ret = dwc3_ep0_handle_feature(dwc, ctrl, 0);
		break;
	case USB_REQ_SET_FEATURE:
		dev_vdbg(dwc->dev, "USB_REQ_SET_FEATURE\n");
		ret = dwc3_ep0_handle_feature(dwc, ctrl, 1);
		break;
	case USB_REQ_SET_ADDRESS:
		dev_vdbg(dwc->dev, "USB_REQ_SET_ADDRESS\n");
		ret = dwc3_ep0_set_address(dwc, ctrl);
		break;
	case USB_REQ_SET_CONFIGURATION:
		dev_vdbg(dwc->dev, "USB_REQ_SET_CONFIGURATION\n");
		ret = dwc3_ep0_set_config(dwc, ctrl);
		break;
	default:
		dev_vdbg(dwc->dev, "Forwarding to gadget driver\n");
		ret = dwc3_ep0_delegate_req(dwc, ctrl);
		break;
	};

	return ret;
}

static void dwc3_ep0_inspect_setup(struct dwc3 *dwc,
		const struct dwc3_event_depevt *event)
{
	struct usb_ctrlrequest *ctrl = dwc->ctrl_req;
	int ret;
	u32 len;

	if (!dwc->gadget_driver)
		goto err;

	len = le16_to_cpu(ctrl->wLength);
	if (!len) {
		dwc->three_stage_setup = false;
		dwc->ep0_expect_in = false;
		dwc->ep0_next_event = DWC3_EP0_NRDY_STATUS;
	} else {
		dwc->three_stage_setup = true;
		dwc->ep0_expect_in = !!(ctrl->bRequestType & USB_DIR_IN);
		dwc->ep0_next_event = DWC3_EP0_NRDY_DATA;
	}

	if ((ctrl->bRequestType & USB_TYPE_MASK) == USB_TYPE_STANDARD)
		ret = dwc3_ep0_std_request(dwc, ctrl);
	else
		ret = dwc3_ep0_delegate_req(dwc, ctrl);

	if (ret == USB_GADGET_DELAYED_STATUS)
		dwc->delayed_status = true;

	if (ret >= 0)
		return;

err:
	dwc3_ep0_stall_and_restart(dwc);
}

static void dwc3_ep0_complete_data(struct dwc3 *dwc,
		const struct dwc3_event_depevt *event)
{
	struct dwc3_request	*r = NULL;
	struct usb_request	*ur;
	struct dwc3_trb		trb;
	struct dwc3_ep		*ep0;
	u32			transferred;
	u8			epnum;

	epnum = event->endpoint_number;
	ep0 = dwc->eps[0];

	dwc->ep0_next_event = DWC3_EP0_NRDY_STATUS;

	r = next_request(&ep0->request_list);
	ur = &r->request;

	dwc3_trb_to_nat(dwc->ep0_trb, &trb);

	if (dwc->ep0_bounced) {

		transferred = min_t(u32, ur->length,
				ep0->endpoint.maxpacket - trb.length);
		memcpy(ur->buf, dwc->ep0_bounce, transferred);
		dwc->ep0_bounced = false;
	} else {
		transferred = ur->length - trb.length;
		ur->actual += transferred;
	}

	if ((epnum & 1) && ur->actual < ur->length) {
		/* for some reason we did not get everything out */

		dwc3_ep0_stall_and_restart(dwc);
	} else {
		/*
		 * handle the case where we have to send a zero packet. This
		 * seems to be case when req.length > maxpacket. Could it be?
		 */
		if (r)
			dwc3_gadget_giveback(ep0, r, 0);
	}
}

static void dwc3_ep0_complete_req(struct dwc3 *dwc,
		const struct dwc3_event_depevt *event)
{
	struct dwc3_request	*r;
	struct dwc3_ep		*dep;

	dep = dwc->eps[0];

	if (!list_empty(&dep->request_list)) {
		r = next_request(&dep->request_list);

		dwc3_gadget_giveback(dep, r, 0);
	}

	dwc->ep0state = EP0_SETUP_PHASE;
	dwc3_ep0_out_start(dwc);
}

static void dwc3_ep0_xfer_complete(struct dwc3 *dwc,
			const struct dwc3_event_depevt *event)
{
	struct dwc3_ep		*dep = dwc->eps[event->endpoint_number];

	dep->flags &= ~DWC3_EP_BUSY;
	dwc->setup_packet_pending = false;

	switch (dwc->ep0state) {
	case EP0_SETUP_PHASE:
		dev_vdbg(dwc->dev, "Inspecting Setup Bytes\n");
		dwc3_ep0_inspect_setup(dwc, event);
		break;

	case EP0_DATA_PHASE:
		dev_vdbg(dwc->dev, "Data Phase\n");
		dwc3_ep0_complete_data(dwc, event);
		break;

	case EP0_STATUS_PHASE:
		dev_vdbg(dwc->dev, "Status Phase\n");
		dwc3_ep0_complete_req(dwc, event);
		break;
	default:
		WARN(true, "UNKNOWN ep0state %d\n", dwc->ep0state);
	}
}

static void dwc3_ep0_do_control_setup(struct dwc3 *dwc,
		const struct dwc3_event_depevt *event)
{
	dwc3_ep0_out_start(dwc);
}

static void dwc3_ep0_do_control_data(struct dwc3 *dwc,
		const struct dwc3_event_depevt *event)
{
	struct dwc3_ep		*dep;
	struct dwc3_request	*req;
	int			ret;

	dep = dwc->eps[0];

	if (list_empty(&dep->request_list)) {
		dev_vdbg(dwc->dev, "pending request for EP0 Data phase\n");
		dep->flags |= DWC3_EP_PENDING_REQUEST;

		if (event->endpoint_number)
			dep->flags |= DWC3_EP0_DIR_IN;
		return;
	}

	req = next_request(&dep->request_list);
	req->direction = !!event->endpoint_number;

	if (req->request.length == 0) {
		ret = dwc3_ep0_start_trans(dwc, event->endpoint_number,
				dwc->ctrl_req_addr, 0,
				DWC3_TRBCTL_CONTROL_DATA);
	} else if ((req->request.length % dep->endpoint.maxpacket)
			&& (event->endpoint_number == 0)) {
		dwc3_map_buffer_to_dma(req);

		WARN_ON(req->request.length > dep->endpoint.maxpacket);

		dwc->ep0_bounced = true;

		/*
		 * REVISIT in case request length is bigger than EP0
		 * wMaxPacketSize, we will need two chained TRBs to handle
		 * the transfer.
		 */
		ret = dwc3_ep0_start_trans(dwc, event->endpoint_number,
				dwc->ep0_bounce_addr, dep->endpoint.maxpacket,
				DWC3_TRBCTL_CONTROL_DATA);
	} else {
		dwc3_map_buffer_to_dma(req);

		ret = dwc3_ep0_start_trans(dwc, event->endpoint_number,
				req->request.dma, req->request.length,
				DWC3_TRBCTL_CONTROL_DATA);
	}

	WARN_ON(ret < 0);
}

static int dwc3_ep0_start_control_status(struct dwc3_ep *dep)
{
	struct dwc3		*dwc = dep->dwc;
	u32			type;

	type = dwc->three_stage_setup ? DWC3_TRBCTL_CONTROL_STATUS3
		: DWC3_TRBCTL_CONTROL_STATUS2;

	return dwc3_ep0_start_trans(dwc, dep->number,
			dwc->ctrl_req_addr, 0, type);
}

static void dwc3_ep0_do_control_status(struct dwc3 *dwc, u32 epnum)
{
	struct dwc3_ep		*dep = dwc->eps[epnum];

	WARN_ON(dwc3_ep0_start_control_status(dep));
}

static void dwc3_ep0_xfernotready(struct dwc3 *dwc,
		const struct dwc3_event_depevt *event)
{
	dwc->setup_packet_pending = true;

	/*
	 * This part is very tricky: If we has just handled
	 * XferNotReady(Setup) and we're now expecting a
	 * XferComplete but, instead, we receive another
	 * XferNotReady(Setup), we should STALL and restart
	 * the state machine.
	 *
	 * In all other cases, we just continue waiting
	 * for the XferComplete event.
	 *
	 * We are a little bit unsafe here because we're
	 * not trying to ensure that last event was, indeed,
	 * XferNotReady(Setup).
	 *
	 * Still, we don't expect any condition where that
	 * should happen and, even if it does, it would be
	 * another error condition.
	 */
	if (dwc->ep0_next_event == DWC3_EP0_COMPLETE) {
		switch (event->status) {
		case DEPEVT_STATUS_CONTROL_SETUP:
			dev_vdbg(dwc->dev, "Unexpected XferNotReady(Setup)\n");
			dwc3_ep0_stall_and_restart(dwc);
			break;
		case DEPEVT_STATUS_CONTROL_DATA:
			/* FALLTHROUGH */
		case DEPEVT_STATUS_CONTROL_STATUS:
			/* FALLTHROUGH */
		default:
			dev_vdbg(dwc->dev, "waiting for XferComplete\n");
		}

		return;
	}

	switch (event->status) {
	case DEPEVT_STATUS_CONTROL_SETUP:
		dev_vdbg(dwc->dev, "Control Setup\n");

		dwc->ep0state = EP0_SETUP_PHASE;

		dwc3_ep0_do_control_setup(dwc, event);
		break;

	case DEPEVT_STATUS_CONTROL_DATA:
		dev_vdbg(dwc->dev, "Control Data\n");

		dwc->ep0state = EP0_DATA_PHASE;

		if (dwc->ep0_next_event != DWC3_EP0_NRDY_DATA) {
			dev_vdbg(dwc->dev, "Expected %d got %d\n",
					dwc->ep0_next_event,
					DWC3_EP0_NRDY_DATA);

			dwc3_ep0_stall_and_restart(dwc);
			return;
		}

		/*
		 * One of the possible error cases is when Host _does_
		 * request for Data Phase, but it does so on the wrong
		 * direction.
		 *
		 * Here, we already know ep0_next_event is DATA (see above),
		 * so we only need to check for direction.
		 */
		if (dwc->ep0_expect_in != event->endpoint_number) {
			dev_vdbg(dwc->dev, "Wrong direction for Data phase\n");
			dwc3_ep0_stall_and_restart(dwc);
			return;
		}

		dwc3_ep0_do_control_data(dwc, event);
		break;

	case DEPEVT_STATUS_CONTROL_STATUS:
		dev_vdbg(dwc->dev, "Control Status\n");

		dwc->ep0state = EP0_STATUS_PHASE;

		if (dwc->ep0_next_event != DWC3_EP0_NRDY_STATUS) {
			dev_vdbg(dwc->dev, "Expected %d got %d\n",
					dwc->ep0_next_event,
					DWC3_EP0_NRDY_STATUS);

			dwc3_ep0_stall_and_restart(dwc);
			return;
		}

		if (dwc->delayed_status) {
			WARN_ON_ONCE(event->endpoint_number != 1);
			dev_vdbg(dwc->dev, "Mass Storage delayed status\n");
			return;
		}

		dwc3_ep0_do_control_status(dwc, event->endpoint_number);
	}
}

void dwc3_ep0_interrupt(struct dwc3 *dwc,
		const struct dwc3_event_depevt *event)
{
	u8			epnum = event->endpoint_number;

	dev_dbg(dwc->dev, "%s while ep%d%s in state '%s'\n",
			dwc3_ep_event_string(event->endpoint_event),
			epnum >> 1, (epnum & 1) ? "in" : "out",
			dwc3_ep0_state_string(dwc->ep0state));

	switch (event->endpoint_event) {
	case DWC3_DEPEVT_XFERCOMPLETE:
		dwc3_ep0_xfer_complete(dwc, event);
		break;

	case DWC3_DEPEVT_XFERNOTREADY:
		dwc3_ep0_xfernotready(dwc, event);
		break;

	case DWC3_DEPEVT_XFERINPROGRESS:
	case DWC3_DEPEVT_RXTXFIFOEVT:
	case DWC3_DEPEVT_STREAMEVT:
	case DWC3_DEPEVT_EPCMDCMPLT:
		break;
	}
}
