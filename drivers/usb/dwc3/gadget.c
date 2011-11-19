/**
 * gadget.c - DesignWare USB3 DRD Controller Gadget Framework Link
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
#include <linux/delay.h>
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

#include "core.h"
#include "gadget.h"
#include "io.h"

#define	DMA_ADDR_INVALID	(~(dma_addr_t)0)

void dwc3_map_buffer_to_dma(struct dwc3_request *req)
{
	struct dwc3			*dwc = req->dep->dwc;

	if (req->request.length == 0) {
		/* req->request.dma = dwc->setup_buf_addr; */
		return;
	}

	if (req->request.dma == DMA_ADDR_INVALID) {
		req->request.dma = dma_map_single(dwc->dev, req->request.buf,
				req->request.length, req->direction
				? DMA_TO_DEVICE : DMA_FROM_DEVICE);
		req->mapped = true;
	}
}

void dwc3_unmap_buffer_from_dma(struct dwc3_request *req)
{
	struct dwc3			*dwc = req->dep->dwc;

	if (req->request.length == 0) {
		req->request.dma = DMA_ADDR_INVALID;
		return;
	}

	if (req->mapped) {
		dma_unmap_single(dwc->dev, req->request.dma,
				req->request.length, req->direction
				? DMA_TO_DEVICE : DMA_FROM_DEVICE);
		req->mapped = 0;
		req->request.dma = DMA_ADDR_INVALID;
	}
}

void dwc3_gadget_giveback(struct dwc3_ep *dep, struct dwc3_request *req,
		int status)
{
	struct dwc3			*dwc = dep->dwc;

	if (req->queued) {
		dep->busy_slot++;
		/*
		 * Skip LINK TRB. We can't use req->trb and check for
		 * DWC3_TRBCTL_LINK_TRB because it points the TRB we just
		 * completed (not the LINK TRB).
		 */
		if (((dep->busy_slot & DWC3_TRB_MASK) == DWC3_TRB_NUM - 1) &&
				usb_endpoint_xfer_isoc(dep->desc))
			dep->busy_slot++;
	}
	list_del(&req->list);

	if (req->request.status == -EINPROGRESS)
		req->request.status = status;

	dwc3_unmap_buffer_from_dma(req);

	dev_dbg(dwc->dev, "request %p from %s completed %d/%d ===> %d\n",
			req, dep->name, req->request.actual,
			req->request.length, status);

	spin_unlock(&dwc->lock);
	req->request.complete(&req->dep->endpoint, &req->request);
	spin_lock(&dwc->lock);
}

static const char *dwc3_gadget_ep_cmd_string(u8 cmd)
{
	switch (cmd) {
	case DWC3_DEPCMD_DEPSTARTCFG:
		return "Start New Configuration";
	case DWC3_DEPCMD_ENDTRANSFER:
		return "End Transfer";
	case DWC3_DEPCMD_UPDATETRANSFER:
		return "Update Transfer";
	case DWC3_DEPCMD_STARTTRANSFER:
		return "Start Transfer";
	case DWC3_DEPCMD_CLEARSTALL:
		return "Clear Stall";
	case DWC3_DEPCMD_SETSTALL:
		return "Set Stall";
	case DWC3_DEPCMD_GETSEQNUMBER:
		return "Get Data Sequence Number";
	case DWC3_DEPCMD_SETTRANSFRESOURCE:
		return "Set Endpoint Transfer Resource";
	case DWC3_DEPCMD_SETEPCONFIG:
		return "Set Endpoint Configuration";
	default:
		return "UNKNOWN command";
	}
}

int dwc3_send_gadget_ep_cmd(struct dwc3 *dwc, unsigned ep,
		unsigned cmd, struct dwc3_gadget_ep_cmd_params *params)
{
	struct dwc3_ep		*dep = dwc->eps[ep];
	u32			timeout = 500;
	u32			reg;

	dev_vdbg(dwc->dev, "%s: cmd '%s' params %08x %08x %08x\n",
			dep->name,
			dwc3_gadget_ep_cmd_string(cmd), params->param0,
			params->param1, params->param2);

	dwc3_writel(dwc->regs, DWC3_DEPCMDPAR0(ep), params->param0);
	dwc3_writel(dwc->regs, DWC3_DEPCMDPAR1(ep), params->param1);
	dwc3_writel(dwc->regs, DWC3_DEPCMDPAR2(ep), params->param2);

	dwc3_writel(dwc->regs, DWC3_DEPCMD(ep), cmd | DWC3_DEPCMD_CMDACT);
	do {
		reg = dwc3_readl(dwc->regs, DWC3_DEPCMD(ep));
		if (!(reg & DWC3_DEPCMD_CMDACT)) {
			dev_vdbg(dwc->dev, "Command Complete --> %d\n",
					DWC3_DEPCMD_STATUS(reg));
			return 0;
		}

		/*
		 * We can't sleep here, because it is also called from
		 * interrupt context.
		 */
		timeout--;
		if (!timeout)
			return -ETIMEDOUT;

		udelay(1);
	} while (1);
}

static dma_addr_t dwc3_trb_dma_offset(struct dwc3_ep *dep,
		struct dwc3_trb_hw *trb)
{
	u32		offset = (char *) trb - (char *) dep->trb_pool;

	return dep->trb_pool_dma + offset;
}

static int dwc3_alloc_trb_pool(struct dwc3_ep *dep)
{
	struct dwc3		*dwc = dep->dwc;

	if (dep->trb_pool)
		return 0;

	if (dep->number == 0 || dep->number == 1)
		return 0;

	dep->trb_pool = dma_alloc_coherent(dwc->dev,
			sizeof(struct dwc3_trb) * DWC3_TRB_NUM,
			&dep->trb_pool_dma, GFP_KERNEL);
	if (!dep->trb_pool) {
		dev_err(dep->dwc->dev, "failed to allocate trb pool for %s\n",
				dep->name);
		return -ENOMEM;
	}

	return 0;
}

static void dwc3_free_trb_pool(struct dwc3_ep *dep)
{
	struct dwc3		*dwc = dep->dwc;

	dma_free_coherent(dwc->dev, sizeof(struct dwc3_trb) * DWC3_TRB_NUM,
			dep->trb_pool, dep->trb_pool_dma);

	dep->trb_pool = NULL;
	dep->trb_pool_dma = 0;
}

static int dwc3_gadget_start_config(struct dwc3 *dwc, struct dwc3_ep *dep)
{
	struct dwc3_gadget_ep_cmd_params params;
	u32			cmd;

	memset(&params, 0x00, sizeof(params));

	if (dep->number != 1) {
		cmd = DWC3_DEPCMD_DEPSTARTCFG;
		/* XferRscIdx == 0 for ep0 and 2 for the remaining */
		if (dep->number > 1) {
			if (dwc->start_config_issued)
				return 0;
			dwc->start_config_issued = true;
			cmd |= DWC3_DEPCMD_PARAM(2);
		}

		return dwc3_send_gadget_ep_cmd(dwc, 0, cmd, &params);
	}

	return 0;
}

static int dwc3_gadget_set_ep_config(struct dwc3 *dwc, struct dwc3_ep *dep,
		const struct usb_endpoint_descriptor *desc)
{
	struct dwc3_gadget_ep_cmd_params params;

	memset(&params, 0x00, sizeof(params));

	params.param0 = DWC3_DEPCFG_EP_TYPE(usb_endpoint_type(desc))
		| DWC3_DEPCFG_MAX_PACKET_SIZE(usb_endpoint_maxp(desc))
		| DWC3_DEPCFG_BURST_SIZE(dep->endpoint.maxburst);

	params.param1 = DWC3_DEPCFG_XFER_COMPLETE_EN
		| DWC3_DEPCFG_XFER_NOT_READY_EN;

	if (usb_endpoint_xfer_bulk(desc) && dep->endpoint.max_streams) {
		params.param1 |= DWC3_DEPCFG_STREAM_CAPABLE
			| DWC3_DEPCFG_STREAM_EVENT_EN;
		dep->stream_capable = true;
	}

	if (usb_endpoint_xfer_isoc(desc))
		params.param1 |= DWC3_DEPCFG_XFER_IN_PROGRESS_EN;

	/*
	 * We are doing 1:1 mapping for endpoints, meaning
	 * Physical Endpoints 2 maps to Logical Endpoint 2 and
	 * so on. We consider the direction bit as part of the physical
	 * endpoint number. So USB endpoint 0x81 is 0x03.
	 */
	params.param1 |= DWC3_DEPCFG_EP_NUMBER(dep->number);

	/*
	 * We must use the lower 16 TX FIFOs even though
	 * HW might have more
	 */
	if (dep->direction)
		params.param0 |= DWC3_DEPCFG_FIFO_NUMBER(dep->number >> 1);

	if (desc->bInterval) {
		params.param1 |= DWC3_DEPCFG_BINTERVAL_M1(desc->bInterval - 1);
		dep->interval = 1 << (desc->bInterval - 1);
	}

	return dwc3_send_gadget_ep_cmd(dwc, dep->number,
			DWC3_DEPCMD_SETEPCONFIG, &params);
}

static int dwc3_gadget_set_xfer_resource(struct dwc3 *dwc, struct dwc3_ep *dep)
{
	struct dwc3_gadget_ep_cmd_params params;

	memset(&params, 0x00, sizeof(params));

	params.param0 = DWC3_DEPXFERCFG_NUM_XFER_RES(1);

	return dwc3_send_gadget_ep_cmd(dwc, dep->number,
			DWC3_DEPCMD_SETTRANSFRESOURCE, &params);
}

/**
 * __dwc3_gadget_ep_enable - Initializes a HW endpoint
 * @dep: endpoint to be initialized
 * @desc: USB Endpoint Descriptor
 *
 * Caller should take care of locking
 */
static int __dwc3_gadget_ep_enable(struct dwc3_ep *dep,
		const struct usb_endpoint_descriptor *desc)
{
	struct dwc3		*dwc = dep->dwc;
	u32			reg;
	int			ret = -ENOMEM;

	if (!(dep->flags & DWC3_EP_ENABLED)) {
		ret = dwc3_gadget_start_config(dwc, dep);
		if (ret)
			return ret;
	}

	ret = dwc3_gadget_set_ep_config(dwc, dep, desc);
	if (ret)
		return ret;

	if (!(dep->flags & DWC3_EP_ENABLED)) {
		struct dwc3_trb_hw	*trb_st_hw;
		struct dwc3_trb_hw	*trb_link_hw;
		struct dwc3_trb		trb_link;

		ret = dwc3_gadget_set_xfer_resource(dwc, dep);
		if (ret)
			return ret;

		dep->desc = desc;
		dep->type = usb_endpoint_type(desc);
		dep->flags |= DWC3_EP_ENABLED;

		reg = dwc3_readl(dwc->regs, DWC3_DALEPENA);
		reg |= DWC3_DALEPENA_EP(dep->number);
		dwc3_writel(dwc->regs, DWC3_DALEPENA, reg);

		if (!usb_endpoint_xfer_isoc(desc))
			return 0;

		memset(&trb_link, 0, sizeof(trb_link));

		/* Link TRB for ISOC. The HWO but is never reset */
		trb_st_hw = &dep->trb_pool[0];

		trb_link.bplh = dwc3_trb_dma_offset(dep, trb_st_hw);
		trb_link.trbctl = DWC3_TRBCTL_LINK_TRB;
		trb_link.hwo = true;

		trb_link_hw = &dep->trb_pool[DWC3_TRB_NUM - 1];
		dwc3_trb_to_hw(&trb_link, trb_link_hw);
	}

	return 0;
}

static void dwc3_stop_active_transfer(struct dwc3 *dwc, u32 epnum);
static void dwc3_remove_requests(struct dwc3 *dwc, struct dwc3_ep *dep)
{
	struct dwc3_request		*req;

	if (!list_empty(&dep->req_queued))
		dwc3_stop_active_transfer(dwc, dep->number);

	while (!list_empty(&dep->request_list)) {
		req = next_request(&dep->request_list);

		dwc3_gadget_giveback(dep, req, -ESHUTDOWN);
	}
}

/**
 * __dwc3_gadget_ep_disable - Disables a HW endpoint
 * @dep: the endpoint to disable
 *
 * This function also removes requests which are currently processed ny the
 * hardware and those which are not yet scheduled.
 * Caller should take care of locking.
 */
static int __dwc3_gadget_ep_disable(struct dwc3_ep *dep)
{
	struct dwc3		*dwc = dep->dwc;
	u32			reg;

	dwc3_remove_requests(dwc, dep);

	reg = dwc3_readl(dwc->regs, DWC3_DALEPENA);
	reg &= ~DWC3_DALEPENA_EP(dep->number);
	dwc3_writel(dwc->regs, DWC3_DALEPENA, reg);

	dep->stream_capable = false;
	dep->desc = NULL;
	dep->type = 0;
	dep->flags = 0;

	return 0;
}

/* -------------------------------------------------------------------------- */

static int dwc3_gadget_ep0_enable(struct usb_ep *ep,
		const struct usb_endpoint_descriptor *desc)
{
	return -EINVAL;
}

static int dwc3_gadget_ep0_disable(struct usb_ep *ep)
{
	return -EINVAL;
}

/* -------------------------------------------------------------------------- */

static int dwc3_gadget_ep_enable(struct usb_ep *ep,
		const struct usb_endpoint_descriptor *desc)
{
	struct dwc3_ep			*dep;
	struct dwc3			*dwc;
	unsigned long			flags;
	int				ret;

	if (!ep || !desc || desc->bDescriptorType != USB_DT_ENDPOINT) {
		pr_debug("dwc3: invalid parameters\n");
		return -EINVAL;
	}

	if (!desc->wMaxPacketSize) {
		pr_debug("dwc3: missing wMaxPacketSize\n");
		return -EINVAL;
	}

	dep = to_dwc3_ep(ep);
	dwc = dep->dwc;

	switch (usb_endpoint_type(desc)) {
	case USB_ENDPOINT_XFER_CONTROL:
		strncat(dep->name, "-control", sizeof(dep->name));
		break;
	case USB_ENDPOINT_XFER_ISOC:
		strncat(dep->name, "-isoc", sizeof(dep->name));
		break;
	case USB_ENDPOINT_XFER_BULK:
		strncat(dep->name, "-bulk", sizeof(dep->name));
		break;
	case USB_ENDPOINT_XFER_INT:
		strncat(dep->name, "-int", sizeof(dep->name));
		break;
	default:
		dev_err(dwc->dev, "invalid endpoint transfer type\n");
	}

	if (dep->flags & DWC3_EP_ENABLED) {
		dev_WARN_ONCE(dwc->dev, true, "%s is already enabled\n",
				dep->name);
		return 0;
	}

	dev_vdbg(dwc->dev, "Enabling %s\n", dep->name);

	spin_lock_irqsave(&dwc->lock, flags);
	ret = __dwc3_gadget_ep_enable(dep, desc);
	spin_unlock_irqrestore(&dwc->lock, flags);

	return ret;
}

static int dwc3_gadget_ep_disable(struct usb_ep *ep)
{
	struct dwc3_ep			*dep;
	struct dwc3			*dwc;
	unsigned long			flags;
	int				ret;

	if (!ep) {
		pr_debug("dwc3: invalid parameters\n");
		return -EINVAL;
	}

	dep = to_dwc3_ep(ep);
	dwc = dep->dwc;

	if (!(dep->flags & DWC3_EP_ENABLED)) {
		dev_WARN_ONCE(dwc->dev, true, "%s is already disabled\n",
				dep->name);
		return 0;
	}

	snprintf(dep->name, sizeof(dep->name), "ep%d%s",
			dep->number >> 1,
			(dep->number & 1) ? "in" : "out");

	spin_lock_irqsave(&dwc->lock, flags);
	ret = __dwc3_gadget_ep_disable(dep);
	spin_unlock_irqrestore(&dwc->lock, flags);

	return ret;
}

static struct usb_request *dwc3_gadget_ep_alloc_request(struct usb_ep *ep,
	gfp_t gfp_flags)
{
	struct dwc3_request		*req;
	struct dwc3_ep			*dep = to_dwc3_ep(ep);
	struct dwc3			*dwc = dep->dwc;

	req = kzalloc(sizeof(*req), gfp_flags);
	if (!req) {
		dev_err(dwc->dev, "not enough memory\n");
		return NULL;
	}

	req->epnum	= dep->number;
	req->dep	= dep;
	req->request.dma = DMA_ADDR_INVALID;

	return &req->request;
}

static void dwc3_gadget_ep_free_request(struct usb_ep *ep,
		struct usb_request *request)
{
	struct dwc3_request		*req = to_dwc3_request(request);

	kfree(req);
}

/*
 * dwc3_prepare_trbs - setup TRBs from requests
 * @dep: endpoint for which requests are being prepared
 * @starting: true if the endpoint is idle and no requests are queued.
 *
 * The functions goes through the requests list and setups TRBs for the
 * transfers. The functions returns once there are not more TRBs available or
 * it run out of requests.
 */
static struct dwc3_request *dwc3_prepare_trbs(struct dwc3_ep *dep,
		bool starting)
{
	struct dwc3_request	*req, *n, *ret = NULL;
	struct dwc3_trb_hw	*trb_hw;
	struct dwc3_trb		trb;
	u32			trbs_left;

	BUILD_BUG_ON_NOT_POWER_OF_2(DWC3_TRB_NUM);

	/* the first request must not be queued */
	trbs_left = (dep->busy_slot - dep->free_slot) & DWC3_TRB_MASK;
	/*
	 * if busy & slot are equal than it is either full or empty. If we are
	 * starting to proceed requests then we are empty. Otherwise we ar
	 * full and don't do anything
	 */
	if (!trbs_left) {
		if (!starting)
			return NULL;
		trbs_left = DWC3_TRB_NUM;
		/*
		 * In case we start from scratch, we queue the ISOC requests
		 * starting from slot 1. This is done because we use ring
		 * buffer and have no LST bit to stop us. Instead, we place
		 * IOC bit TRB_NUM/4. We try to avoid to having an interrupt
		 * after the first request so we start at slot 1 and have
		 * 7 requests proceed before we hit the first IOC.
		 * Other transfer types don't use the ring buffer and are
		 * processed from the first TRB until the last one. Since we
		 * don't wrap around we have to start at the beginning.
		 */
		if (usb_endpoint_xfer_isoc(dep->desc)) {
			dep->busy_slot = 1;
			dep->free_slot = 1;
		} else {
			dep->busy_slot = 0;
			dep->free_slot = 0;
		}
	}

	/* The last TRB is a link TRB, not used for xfer */
	if ((trbs_left <= 1) && usb_endpoint_xfer_isoc(dep->desc))
		return NULL;

	list_for_each_entry_safe(req, n, &dep->request_list, list) {
		unsigned int last_one = 0;
		unsigned int cur_slot;

		trb_hw = &dep->trb_pool[dep->free_slot & DWC3_TRB_MASK];
		cur_slot = dep->free_slot;
		dep->free_slot++;

		/* Skip the LINK-TRB on ISOC */
		if (((cur_slot & DWC3_TRB_MASK) == DWC3_TRB_NUM - 1) &&
				usb_endpoint_xfer_isoc(dep->desc))
			continue;

		dwc3_gadget_move_request_queued(req);
		memset(&trb, 0, sizeof(trb));
		trbs_left--;

		/* Is our TRB pool empty? */
		if (!trbs_left)
			last_one = 1;
		/* Is this the last request? */
		if (list_empty(&dep->request_list))
			last_one = 1;

		/*
		 * FIXME we shouldn't need to set LST bit always but we are
		 * facing some weird problem with the Hardware where it doesn't
		 * complete even though it has been previously started.
		 *
		 * While we're debugging the problem, as a workaround to
		 * multiple TRBs handling, use only one TRB at a time.
		 */
		last_one = 1;

		req->trb = trb_hw;
		if (!ret)
			ret = req;

		trb.bplh = req->request.dma;

		if (usb_endpoint_xfer_isoc(dep->desc)) {
			trb.isp_imi = true;
			trb.csp = true;
		} else {
			trb.lst = last_one;
		}

		if (usb_endpoint_xfer_bulk(dep->desc) && dep->stream_capable)
			trb.sid_sofn = req->request.stream_id;

		switch (usb_endpoint_type(dep->desc)) {
		case USB_ENDPOINT_XFER_CONTROL:
			trb.trbctl = DWC3_TRBCTL_CONTROL_SETUP;
			break;

		case USB_ENDPOINT_XFER_ISOC:
			trb.trbctl = DWC3_TRBCTL_ISOCHRONOUS_FIRST;

			/* IOC every DWC3_TRB_NUM / 4 so we can refill */
			if (!(cur_slot % (DWC3_TRB_NUM / 4)))
				trb.ioc = last_one;
			break;

		case USB_ENDPOINT_XFER_BULK:
		case USB_ENDPOINT_XFER_INT:
			trb.trbctl = DWC3_TRBCTL_NORMAL;
			break;
		default:
			/*
			 * This is only possible with faulty memory because we
			 * checked it already :)
			 */
			BUG();
		}

		trb.length	= req->request.length;
		trb.hwo = true;

		dwc3_trb_to_hw(&trb, trb_hw);
		req->trb_dma = dwc3_trb_dma_offset(dep, trb_hw);

		if (last_one)
			break;
	}

	return ret;
}

static int __dwc3_gadget_kick_transfer(struct dwc3_ep *dep, u16 cmd_param,
		int start_new)
{
	struct dwc3_gadget_ep_cmd_params params;
	struct dwc3_request		*req;
	struct dwc3			*dwc = dep->dwc;
	int				ret;
	u32				cmd;

	if (start_new && (dep->flags & DWC3_EP_BUSY)) {
		dev_vdbg(dwc->dev, "%s: endpoint busy\n", dep->name);
		return -EBUSY;
	}
	dep->flags &= ~DWC3_EP_PENDING_REQUEST;

	/*
	 * If we are getting here after a short-out-packet we don't enqueue any
	 * new requests as we try to set the IOC bit only on the last request.
	 */
	if (start_new) {
		if (list_empty(&dep->req_queued))
			dwc3_prepare_trbs(dep, start_new);

		/* req points to the first request which will be sent */
		req = next_request(&dep->req_queued);
	} else {
		/*
		 * req points to the first request where HWO changed
		 * from 0 to 1
		 */
		req = dwc3_prepare_trbs(dep, start_new);
	}
	if (!req) {
		dep->flags |= DWC3_EP_PENDING_REQUEST;
		return 0;
	}

	memset(&params, 0, sizeof(params));
	params.param0 = upper_32_bits(req->trb_dma);
	params.param1 = lower_32_bits(req->trb_dma);

	if (start_new)
		cmd = DWC3_DEPCMD_STARTTRANSFER;
	else
		cmd = DWC3_DEPCMD_UPDATETRANSFER;

	cmd |= DWC3_DEPCMD_PARAM(cmd_param);
	ret = dwc3_send_gadget_ep_cmd(dwc, dep->number, cmd, &params);
	if (ret < 0) {
		dev_dbg(dwc->dev, "failed to send STARTTRANSFER command\n");

		/*
		 * FIXME we need to iterate over the list of requests
		 * here and stop, unmap, free and del each of the linked
		 * requests instead of we do now.
		 */
		dwc3_unmap_buffer_from_dma(req);
		list_del(&req->list);
		return ret;
	}

	dep->flags |= DWC3_EP_BUSY;
	dep->res_trans_idx = dwc3_gadget_ep_get_transfer_index(dwc,
			dep->number);
	if (!dep->res_trans_idx)
		printk_once(KERN_ERR "%s() res_trans_idx is invalid\n", __func__);
	return 0;
}

static int __dwc3_gadget_ep_queue(struct dwc3_ep *dep, struct dwc3_request *req)
{
	req->request.actual	= 0;
	req->request.status	= -EINPROGRESS;
	req->direction		= dep->direction;
	req->epnum		= dep->number;

	/*
	 * We only add to our list of requests now and
	 * start consuming the list once we get XferNotReady
	 * IRQ.
	 *
	 * That way, we avoid doing anything that we don't need
	 * to do now and defer it until the point we receive a
	 * particular token from the Host side.
	 *
	 * This will also avoid Host cancelling URBs due to too
	 * many NACKs.
	 */
	dwc3_map_buffer_to_dma(req);
	list_add_tail(&req->list, &dep->request_list);

	/*
	 * There is one special case: XferNotReady with
	 * empty list of requests. We need to kick the
	 * transfer here in that situation, otherwise
	 * we will be NAKing forever.
	 *
	 * If we get XferNotReady before gadget driver
	 * has a chance to queue a request, we will ACK
	 * the IRQ but won't be able to receive the data
	 * until the next request is queued. The following
	 * code is handling exactly that.
	 */
	if (dep->flags & DWC3_EP_PENDING_REQUEST) {
		int ret;
		int start_trans;

		start_trans = 1;
		if (usb_endpoint_xfer_isoc(dep->endpoint.desc) &&
				dep->flags & DWC3_EP_BUSY)
			start_trans = 0;

		ret =  __dwc3_gadget_kick_transfer(dep, 0, start_trans);
		if (ret && ret != -EBUSY) {
			struct dwc3	*dwc = dep->dwc;

			dev_dbg(dwc->dev, "%s: failed to kick transfers\n",
					dep->name);
		}
	};

	return 0;
}

static int dwc3_gadget_ep_queue(struct usb_ep *ep, struct usb_request *request,
	gfp_t gfp_flags)
{
	struct dwc3_request		*req = to_dwc3_request(request);
	struct dwc3_ep			*dep = to_dwc3_ep(ep);
	struct dwc3			*dwc = dep->dwc;

	unsigned long			flags;

	int				ret;

	if (!dep->desc) {
		dev_dbg(dwc->dev, "trying to queue request %p to disabled %s\n",
				request, ep->name);
		return -ESHUTDOWN;
	}

	dev_vdbg(dwc->dev, "queing request %p to %s length %d\n",
			request, ep->name, request->length);

	spin_lock_irqsave(&dwc->lock, flags);
	ret = __dwc3_gadget_ep_queue(dep, req);
	spin_unlock_irqrestore(&dwc->lock, flags);

	return ret;
}

static int dwc3_gadget_ep_dequeue(struct usb_ep *ep,
		struct usb_request *request)
{
	struct dwc3_request		*req = to_dwc3_request(request);
	struct dwc3_request		*r = NULL;

	struct dwc3_ep			*dep = to_dwc3_ep(ep);
	struct dwc3			*dwc = dep->dwc;

	unsigned long			flags;
	int				ret = 0;

	spin_lock_irqsave(&dwc->lock, flags);

	list_for_each_entry(r, &dep->request_list, list) {
		if (r == req)
			break;
	}

	if (r != req) {
		list_for_each_entry(r, &dep->req_queued, list) {
			if (r == req)
				break;
		}
		if (r == req) {
			/* wait until it is processed */
			dwc3_stop_active_transfer(dwc, dep->number);
			goto out0;
		}
		dev_err(dwc->dev, "request %p was not queued to %s\n",
				request, ep->name);
		ret = -EINVAL;
		goto out0;
	}

	/* giveback the request */
	dwc3_gadget_giveback(dep, req, -ECONNRESET);

out0:
	spin_unlock_irqrestore(&dwc->lock, flags);

	return ret;
}

int __dwc3_gadget_ep_set_halt(struct dwc3_ep *dep, int value)
{
	struct dwc3_gadget_ep_cmd_params	params;
	struct dwc3				*dwc = dep->dwc;
	int					ret;

	memset(&params, 0x00, sizeof(params));

	if (value) {
		if (dep->number == 0 || dep->number == 1) {
			/*
			 * Whenever EP0 is stalled, we will restart
			 * the state machine, thus moving back to
			 * Setup Phase
			 */
			dwc->ep0state = EP0_SETUP_PHASE;
		}

		ret = dwc3_send_gadget_ep_cmd(dwc, dep->number,
			DWC3_DEPCMD_SETSTALL, &params);
		if (ret)
			dev_err(dwc->dev, "failed to %s STALL on %s\n",
					value ? "set" : "clear",
					dep->name);
		else
			dep->flags |= DWC3_EP_STALL;
	} else {
		if (dep->flags & DWC3_EP_WEDGE)
			return 0;

		ret = dwc3_send_gadget_ep_cmd(dwc, dep->number,
			DWC3_DEPCMD_CLEARSTALL, &params);
		if (ret)
			dev_err(dwc->dev, "failed to %s STALL on %s\n",
					value ? "set" : "clear",
					dep->name);
		else
			dep->flags &= ~DWC3_EP_STALL;
	}

	return ret;
}

static int dwc3_gadget_ep_set_halt(struct usb_ep *ep, int value)
{
	struct dwc3_ep			*dep = to_dwc3_ep(ep);
	struct dwc3			*dwc = dep->dwc;

	unsigned long			flags;

	int				ret;

	spin_lock_irqsave(&dwc->lock, flags);

	if (usb_endpoint_xfer_isoc(dep->desc)) {
		dev_err(dwc->dev, "%s is of Isochronous type\n", dep->name);
		ret = -EINVAL;
		goto out;
	}

	ret = __dwc3_gadget_ep_set_halt(dep, value);
out:
	spin_unlock_irqrestore(&dwc->lock, flags);

	return ret;
}

static int dwc3_gadget_ep_set_wedge(struct usb_ep *ep)
{
	struct dwc3_ep			*dep = to_dwc3_ep(ep);

	dep->flags |= DWC3_EP_WEDGE;

	return dwc3_gadget_ep_set_halt(ep, 1);
}

/* -------------------------------------------------------------------------- */

static struct usb_endpoint_descriptor dwc3_gadget_ep0_desc = {
	.bLength	= USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bmAttributes	= USB_ENDPOINT_XFER_CONTROL,
};

static const struct usb_ep_ops dwc3_gadget_ep0_ops = {
	.enable		= dwc3_gadget_ep0_enable,
	.disable	= dwc3_gadget_ep0_disable,
	.alloc_request	= dwc3_gadget_ep_alloc_request,
	.free_request	= dwc3_gadget_ep_free_request,
	.queue		= dwc3_gadget_ep0_queue,
	.dequeue	= dwc3_gadget_ep_dequeue,
	.set_halt	= dwc3_gadget_ep_set_halt,
	.set_wedge	= dwc3_gadget_ep_set_wedge,
};

static const struct usb_ep_ops dwc3_gadget_ep_ops = {
	.enable		= dwc3_gadget_ep_enable,
	.disable	= dwc3_gadget_ep_disable,
	.alloc_request	= dwc3_gadget_ep_alloc_request,
	.free_request	= dwc3_gadget_ep_free_request,
	.queue		= dwc3_gadget_ep_queue,
	.dequeue	= dwc3_gadget_ep_dequeue,
	.set_halt	= dwc3_gadget_ep_set_halt,
	.set_wedge	= dwc3_gadget_ep_set_wedge,
};

/* -------------------------------------------------------------------------- */

static int dwc3_gadget_get_frame(struct usb_gadget *g)
{
	struct dwc3		*dwc = gadget_to_dwc(g);
	u32			reg;

	reg = dwc3_readl(dwc->regs, DWC3_DSTS);
	return DWC3_DSTS_SOFFN(reg);
}

static int dwc3_gadget_wakeup(struct usb_gadget *g)
{
	struct dwc3		*dwc = gadget_to_dwc(g);

	unsigned long		timeout;
	unsigned long		flags;

	u32			reg;

	int			ret = 0;

	u8			link_state;
	u8			speed;

	spin_lock_irqsave(&dwc->lock, flags);

	/*
	 * According to the Databook Remote wakeup request should
	 * be issued only when the device is in early suspend state.
	 *
	 * We can check that via USB Link State bits in DSTS register.
	 */
	reg = dwc3_readl(dwc->regs, DWC3_DSTS);

	speed = reg & DWC3_DSTS_CONNECTSPD;
	if (speed == DWC3_DSTS_SUPERSPEED) {
		dev_dbg(dwc->dev, "no wakeup on SuperSpeed\n");
		ret = -EINVAL;
		goto out;
	}

	link_state = DWC3_DSTS_USBLNKST(reg);

	switch (link_state) {
	case DWC3_LINK_STATE_RX_DET:	/* in HS, means Early Suspend */
	case DWC3_LINK_STATE_U3:	/* in HS, means SUSPEND */
		break;
	default:
		dev_dbg(dwc->dev, "can't wakeup from link state %d\n",
				link_state);
		ret = -EINVAL;
		goto out;
	}

	reg = dwc3_readl(dwc->regs, DWC3_DCTL);

	/*
	 * Switch link state to Recovery. In HS/FS/LS this means
	 * RemoteWakeup Request
	 */
	reg |= DWC3_DCTL_ULSTCHNG_RECOVERY;
	dwc3_writel(dwc->regs, DWC3_DCTL, reg);

	/* wait for at least 2000us */
	usleep_range(2000, 2500);

	/* write zeroes to Link Change Request */
	reg &= ~DWC3_DCTL_ULSTCHNGREQ_MASK;
	dwc3_writel(dwc->regs, DWC3_DCTL, reg);

	/* pool until Link State change to ON */
	timeout = jiffies + msecs_to_jiffies(100);

	while (!(time_after(jiffies, timeout))) {
		reg = dwc3_readl(dwc->regs, DWC3_DSTS);

		/* in HS, means ON */
		if (DWC3_DSTS_USBLNKST(reg) == DWC3_LINK_STATE_U0)
			break;
	}

	if (DWC3_DSTS_USBLNKST(reg) != DWC3_LINK_STATE_U0) {
		dev_err(dwc->dev, "failed to send remote wakeup\n");
		ret = -EINVAL;
	}

out:
	spin_unlock_irqrestore(&dwc->lock, flags);

	return ret;
}

static int dwc3_gadget_set_selfpowered(struct usb_gadget *g,
		int is_selfpowered)
{
	struct dwc3		*dwc = gadget_to_dwc(g);

	dwc->is_selfpowered = !!is_selfpowered;

	return 0;
}

static void dwc3_gadget_run_stop(struct dwc3 *dwc, int is_on)
{
	u32			reg;
	u32			timeout = 500;

	reg = dwc3_readl(dwc->regs, DWC3_DCTL);
	if (is_on)
		reg |= DWC3_DCTL_RUN_STOP;
	else
		reg &= ~DWC3_DCTL_RUN_STOP;

	dwc3_writel(dwc->regs, DWC3_DCTL, reg);

	do {
		reg = dwc3_readl(dwc->regs, DWC3_DSTS);
		if (is_on) {
			if (!(reg & DWC3_DSTS_DEVCTRLHLT))
				break;
		} else {
			if (reg & DWC3_DSTS_DEVCTRLHLT)
				break;
		}
		timeout--;
		if (!timeout)
			break;
		udelay(1);
	} while (1);

	dev_vdbg(dwc->dev, "gadget %s data soft-%s\n",
			dwc->gadget_driver
			? dwc->gadget_driver->function : "no-function",
			is_on ? "connect" : "disconnect");
}

static int dwc3_gadget_pullup(struct usb_gadget *g, int is_on)
{
	struct dwc3		*dwc = gadget_to_dwc(g);
	unsigned long		flags;

	is_on = !!is_on;

	spin_lock_irqsave(&dwc->lock, flags);
	dwc3_gadget_run_stop(dwc, is_on);
	spin_unlock_irqrestore(&dwc->lock, flags);

	return 0;
}

static int dwc3_gadget_start(struct usb_gadget *g,
		struct usb_gadget_driver *driver)
{
	struct dwc3		*dwc = gadget_to_dwc(g);
	struct dwc3_ep		*dep;
	unsigned long		flags;
	int			ret = 0;
	u32			reg;

	spin_lock_irqsave(&dwc->lock, flags);

	if (dwc->gadget_driver) {
		dev_err(dwc->dev, "%s is already bound to %s\n",
				dwc->gadget.name,
				dwc->gadget_driver->driver.name);
		ret = -EBUSY;
		goto err0;
	}

	dwc->gadget_driver	= driver;
	dwc->gadget.dev.driver	= &driver->driver;

	reg = dwc3_readl(dwc->regs, DWC3_GCTL);

	reg &= ~DWC3_GCTL_SCALEDOWN(3);
	reg &= ~DWC3_GCTL_PRTCAPDIR(DWC3_GCTL_PRTCAP_OTG);
	reg &= ~DWC3_GCTL_DISSCRAMBLE;
	reg |= DWC3_GCTL_PRTCAPDIR(DWC3_GCTL_PRTCAP_DEVICE);

	switch (DWC3_GHWPARAMS1_EN_PWROPT(dwc->hwparams.hwparams0)) {
	case DWC3_GHWPARAMS1_EN_PWROPT_CLK:
		reg &= ~DWC3_GCTL_DSBLCLKGTNG;
		break;
	default:
		dev_dbg(dwc->dev, "No power optimization available\n");
	}

	/*
	 * WORKAROUND: DWC3 revisions <1.90a have a bug
	 * when The device fails to connect at SuperSpeed
	 * and falls back to high-speed mode which causes
	 * the device to enter in a Connect/Disconnect loop
	 */
	if (dwc->revision < DWC3_REVISION_190A)
		reg |= DWC3_GCTL_U2RSTECN;

	dwc3_writel(dwc->regs, DWC3_GCTL, reg);

	reg = dwc3_readl(dwc->regs, DWC3_DCFG);
	reg &= ~(DWC3_DCFG_SPEED_MASK);
	reg |= DWC3_DCFG_SUPERSPEED;
	dwc3_writel(dwc->regs, DWC3_DCFG, reg);

	dwc->start_config_issued = false;

	/* Start with SuperSpeed Default */
	dwc3_gadget_ep0_desc.wMaxPacketSize = cpu_to_le16(512);

	dep = dwc->eps[0];
	ret = __dwc3_gadget_ep_enable(dep, &dwc3_gadget_ep0_desc);
	if (ret) {
		dev_err(dwc->dev, "failed to enable %s\n", dep->name);
		goto err0;
	}

	dep = dwc->eps[1];
	ret = __dwc3_gadget_ep_enable(dep, &dwc3_gadget_ep0_desc);
	if (ret) {
		dev_err(dwc->dev, "failed to enable %s\n", dep->name);
		goto err1;
	}

	/* begin to receive SETUP packets */
	dwc->ep0state = EP0_SETUP_PHASE;
	dwc3_ep0_out_start(dwc);

	spin_unlock_irqrestore(&dwc->lock, flags);

	return 0;

err1:
	__dwc3_gadget_ep_disable(dwc->eps[0]);

err0:
	spin_unlock_irqrestore(&dwc->lock, flags);

	return ret;
}

static int dwc3_gadget_stop(struct usb_gadget *g,
		struct usb_gadget_driver *driver)
{
	struct dwc3		*dwc = gadget_to_dwc(g);
	unsigned long		flags;

	spin_lock_irqsave(&dwc->lock, flags);

	__dwc3_gadget_ep_disable(dwc->eps[0]);
	__dwc3_gadget_ep_disable(dwc->eps[1]);

	dwc->gadget_driver	= NULL;
	dwc->gadget.dev.driver	= NULL;

	spin_unlock_irqrestore(&dwc->lock, flags);

	return 0;
}
static const struct usb_gadget_ops dwc3_gadget_ops = {
	.get_frame		= dwc3_gadget_get_frame,
	.wakeup			= dwc3_gadget_wakeup,
	.set_selfpowered	= dwc3_gadget_set_selfpowered,
	.pullup			= dwc3_gadget_pullup,
	.udc_start		= dwc3_gadget_start,
	.udc_stop		= dwc3_gadget_stop,
};

/* -------------------------------------------------------------------------- */

static int __devinit dwc3_gadget_init_endpoints(struct dwc3 *dwc)
{
	struct dwc3_ep			*dep;
	u8				epnum;

	INIT_LIST_HEAD(&dwc->gadget.ep_list);

	for (epnum = 0; epnum < DWC3_ENDPOINTS_NUM; epnum++) {
		dep = kzalloc(sizeof(*dep), GFP_KERNEL);
		if (!dep) {
			dev_err(dwc->dev, "can't allocate endpoint %d\n",
					epnum);
			return -ENOMEM;
		}

		dep->dwc = dwc;
		dep->number = epnum;
		dwc->eps[epnum] = dep;

		snprintf(dep->name, sizeof(dep->name), "ep%d%s", epnum >> 1,
				(epnum & 1) ? "in" : "out");
		dep->endpoint.name = dep->name;
		dep->direction = (epnum & 1);

		if (epnum == 0 || epnum == 1) {
			dep->endpoint.maxpacket = 512;
			dep->endpoint.ops = &dwc3_gadget_ep0_ops;
			if (!epnum)
				dwc->gadget.ep0 = &dep->endpoint;
		} else {
			int		ret;

			dep->endpoint.maxpacket = 1024;
			dep->endpoint.max_streams = 15;
			dep->endpoint.ops = &dwc3_gadget_ep_ops;
			list_add_tail(&dep->endpoint.ep_list,
					&dwc->gadget.ep_list);

			ret = dwc3_alloc_trb_pool(dep);
			if (ret) {
				dev_err(dwc->dev, "%s: failed to allocate TRB pool\n", dep->name);
				return ret;
			}
		}
		INIT_LIST_HEAD(&dep->request_list);
		INIT_LIST_HEAD(&dep->req_queued);
	}

	return 0;
}

static void dwc3_gadget_free_endpoints(struct dwc3 *dwc)
{
	struct dwc3_ep			*dep;
	u8				epnum;

	for (epnum = 0; epnum < DWC3_ENDPOINTS_NUM; epnum++) {
		dep = dwc->eps[epnum];
		dwc3_free_trb_pool(dep);

		if (epnum != 0 && epnum != 1)
			list_del(&dep->endpoint.ep_list);

		kfree(dep);
	}
}

static void dwc3_gadget_release(struct device *dev)
{
	dev_dbg(dev, "%s\n", __func__);
}

/* -------------------------------------------------------------------------- */
static int dwc3_cleanup_done_reqs(struct dwc3 *dwc, struct dwc3_ep *dep,
		const struct dwc3_event_depevt *event, int status)
{
	struct dwc3_request	*req;
	struct dwc3_trb         trb;
	unsigned int		count;
	unsigned int		s_pkt = 0;

	do {
		req = next_request(&dep->req_queued);
		if (!req)
			break;

		dwc3_trb_to_nat(req->trb, &trb);

		if (trb.hwo && status != -ESHUTDOWN)
			/*
			 * We continue despite the error. There is not much we
			 * can do. If we don't clean in up we loop for ever. If
			 * we skip the TRB than it gets overwritten reused after
			 * a while since we use them in a ring buffer. a BUG()
			 * would help. Lets hope that if this occures, someone
			 * fixes the root cause instead of looking away :)
			 */
			dev_err(dwc->dev, "%s's TRB (%p) still owned by HW\n",
					dep->name, req->trb);
		count = trb.length;

		if (dep->direction) {
			if (count) {
				dev_err(dwc->dev, "incomplete IN transfer %s\n",
						dep->name);
				status = -ECONNRESET;
			}
		} else {
			if (count && (event->status & DEPEVT_STATUS_SHORT))
				s_pkt = 1;
		}

		/*
		 * We assume here we will always receive the entire data block
		 * which we should receive. Meaning, if we program RX to
		 * receive 4K but we receive only 2K, we assume that's all we
		 * should receive and we simply bounce the request back to the
		 * gadget driver for further processing.
		 */
		req->request.actual += req->request.length - count;
		dwc3_gadget_giveback(dep, req, status);
		if (s_pkt)
			break;
		if ((event->status & DEPEVT_STATUS_LST) && trb.lst)
			break;
		if ((event->status & DEPEVT_STATUS_IOC) && trb.ioc)
			break;
	} while (1);

	if ((event->status & DEPEVT_STATUS_IOC) && trb.ioc)
		return 0;
	return 1;
}

static void dwc3_endpoint_transfer_complete(struct dwc3 *dwc,
		struct dwc3_ep *dep, const struct dwc3_event_depevt *event,
		int start_new)
{
	unsigned		status = 0;
	int			clean_busy;

	if (event->status & DEPEVT_STATUS_BUSERR)
		status = -ECONNRESET;

	clean_busy =  dwc3_cleanup_done_reqs(dwc, dep, event, status);
	if (clean_busy) {
		dep->flags &= ~DWC3_EP_BUSY;
		dep->res_trans_idx = 0;
	}
}

static void dwc3_gadget_start_isoc(struct dwc3 *dwc,
		struct dwc3_ep *dep, const struct dwc3_event_depevt *event)
{
	u32 uf;

	if (list_empty(&dep->request_list)) {
		dev_vdbg(dwc->dev, "ISOC ep %s run out for requests.\n",
			dep->name);
		return;
	}

	if (event->parameters) {
		u32 mask;

		mask = ~(dep->interval - 1);
		uf = event->parameters & mask;
		/* 4 micro frames in the future */
		uf += dep->interval * 4;
	} else {
		uf = 0;
	}

	__dwc3_gadget_kick_transfer(dep, uf, 1);
}

static void dwc3_process_ep_cmd_complete(struct dwc3_ep *dep,
		const struct dwc3_event_depevt *event)
{
	struct dwc3 *dwc = dep->dwc;
	struct dwc3_event_depevt mod_ev = *event;

	/*
	 * We were asked to remove one requests. It is possible that this
	 * request and a few other were started together and have the same
	 * transfer index. Since we stopped the complete endpoint we don't
	 * know how many requests were already completed (and not yet)
	 * reported and how could be done (later). We purge them all until
	 * the end of the list.
	 */
	mod_ev.status = DEPEVT_STATUS_LST;
	dwc3_cleanup_done_reqs(dwc, dep, &mod_ev, -ESHUTDOWN);
	dep->flags &= ~DWC3_EP_BUSY;
	/* pending requets are ignored and are queued on XferNotReady */
}

static void dwc3_ep_cmd_compl(struct dwc3_ep *dep,
		const struct dwc3_event_depevt *event)
{
	u32 param = event->parameters;
	u32 cmd_type = (param >> 8) & ((1 << 5) - 1);

	switch (cmd_type) {
	case DWC3_DEPCMD_ENDTRANSFER:
		dwc3_process_ep_cmd_complete(dep, event);
		break;
	case DWC3_DEPCMD_STARTTRANSFER:
		dep->res_trans_idx = param & 0x7f;
		break;
	default:
		printk(KERN_ERR "%s() unknown /unexpected type: %d\n",
				__func__, cmd_type);
		break;
	};
}

static void dwc3_endpoint_interrupt(struct dwc3 *dwc,
		const struct dwc3_event_depevt *event)
{
	struct dwc3_ep		*dep;
	u8			epnum = event->endpoint_number;

	dep = dwc->eps[epnum];

	dev_vdbg(dwc->dev, "%s: %s\n", dep->name,
			dwc3_ep_event_string(event->endpoint_event));

	if (epnum == 0 || epnum == 1) {
		dwc3_ep0_interrupt(dwc, event);
		return;
	}

	switch (event->endpoint_event) {
	case DWC3_DEPEVT_XFERCOMPLETE:
		if (usb_endpoint_xfer_isoc(dep->desc)) {
			dev_dbg(dwc->dev, "%s is an Isochronous endpoint\n",
					dep->name);
			return;
		}

		dwc3_endpoint_transfer_complete(dwc, dep, event, 1);
		break;
	case DWC3_DEPEVT_XFERINPROGRESS:
		if (!usb_endpoint_xfer_isoc(dep->desc)) {
			dev_dbg(dwc->dev, "%s is not an Isochronous endpoint\n",
					dep->name);
			return;
		}

		dwc3_endpoint_transfer_complete(dwc, dep, event, 0);
		break;
	case DWC3_DEPEVT_XFERNOTREADY:
		if (usb_endpoint_xfer_isoc(dep->desc)) {
			dwc3_gadget_start_isoc(dwc, dep, event);
		} else {
			int ret;

			dev_vdbg(dwc->dev, "%s: reason %s\n",
					dep->name, event->status
					? "Transfer Active"
					: "Transfer Not Active");

			ret = __dwc3_gadget_kick_transfer(dep, 0, 1);
			if (!ret || ret == -EBUSY)
				return;

			dev_dbg(dwc->dev, "%s: failed to kick transfers\n",
					dep->name);
		}

		break;
	case DWC3_DEPEVT_STREAMEVT:
		if (!usb_endpoint_xfer_bulk(dep->desc)) {
			dev_err(dwc->dev, "Stream event for non-Bulk %s\n",
					dep->name);
			return;
		}

		switch (event->status) {
		case DEPEVT_STREAMEVT_FOUND:
			dev_vdbg(dwc->dev, "Stream %d found and started\n",
					event->parameters);

			break;
		case DEPEVT_STREAMEVT_NOTFOUND:
			/* FALLTHROUGH */
		default:
			dev_dbg(dwc->dev, "Couldn't find suitable stream\n");
		}
		break;
	case DWC3_DEPEVT_RXTXFIFOEVT:
		dev_dbg(dwc->dev, "%s FIFO Overrun\n", dep->name);
		break;
	case DWC3_DEPEVT_EPCMDCMPLT:
		dwc3_ep_cmd_compl(dep, event);
		break;
	}
}

static void dwc3_disconnect_gadget(struct dwc3 *dwc)
{
	if (dwc->gadget_driver && dwc->gadget_driver->disconnect) {
		spin_unlock(&dwc->lock);
		dwc->gadget_driver->disconnect(&dwc->gadget);
		spin_lock(&dwc->lock);
	}
}

static void dwc3_stop_active_transfer(struct dwc3 *dwc, u32 epnum)
{
	struct dwc3_ep *dep;
	struct dwc3_gadget_ep_cmd_params params;
	u32 cmd;
	int ret;

	dep = dwc->eps[epnum];

	WARN_ON(!dep->res_trans_idx);
	if (dep->res_trans_idx) {
		cmd = DWC3_DEPCMD_ENDTRANSFER;
		cmd |= DWC3_DEPCMD_HIPRI_FORCERM | DWC3_DEPCMD_CMDIOC;
		cmd |= DWC3_DEPCMD_PARAM(dep->res_trans_idx);
		memset(&params, 0, sizeof(params));
		ret = dwc3_send_gadget_ep_cmd(dwc, dep->number, cmd, &params);
		WARN_ON_ONCE(ret);
		dep->res_trans_idx = 0;
	}
}

static void dwc3_stop_active_transfers(struct dwc3 *dwc)
{
	u32 epnum;

	for (epnum = 2; epnum < DWC3_ENDPOINTS_NUM; epnum++) {
		struct dwc3_ep *dep;

		dep = dwc->eps[epnum];
		if (!(dep->flags & DWC3_EP_ENABLED))
			continue;

		dwc3_remove_requests(dwc, dep);
	}
}

static void dwc3_clear_stall_all_ep(struct dwc3 *dwc)
{
	u32 epnum;

	for (epnum = 1; epnum < DWC3_ENDPOINTS_NUM; epnum++) {
		struct dwc3_ep *dep;
		struct dwc3_gadget_ep_cmd_params params;
		int ret;

		dep = dwc->eps[epnum];

		if (!(dep->flags & DWC3_EP_STALL))
			continue;

		dep->flags &= ~DWC3_EP_STALL;

		memset(&params, 0, sizeof(params));
		ret = dwc3_send_gadget_ep_cmd(dwc, dep->number,
				DWC3_DEPCMD_CLEARSTALL, &params);
		WARN_ON_ONCE(ret);
	}
}

static void dwc3_gadget_disconnect_interrupt(struct dwc3 *dwc)
{
	dev_vdbg(dwc->dev, "%s\n", __func__);
#if 0
	XXX
	U1/U2 is powersave optimization. Skip it for now. Anyway we need to
	enable it before we can disable it.

	reg = dwc3_readl(dwc->regs, DWC3_DCTL);
	reg &= ~DWC3_DCTL_INITU1ENA;
	dwc3_writel(dwc->regs, DWC3_DCTL, reg);

	reg &= ~DWC3_DCTL_INITU2ENA;
	dwc3_writel(dwc->regs, DWC3_DCTL, reg);
#endif

	dwc3_stop_active_transfers(dwc);
	dwc3_disconnect_gadget(dwc);
	dwc->start_config_issued = false;

	dwc->gadget.speed = USB_SPEED_UNKNOWN;
}

static void dwc3_gadget_usb3_phy_power(struct dwc3 *dwc, int on)
{
	u32			reg;

	reg = dwc3_readl(dwc->regs, DWC3_GUSB3PIPECTL(0));

	if (on)
		reg &= ~DWC3_GUSB3PIPECTL_SUSPHY;
	else
		reg |= DWC3_GUSB3PIPECTL_SUSPHY;

	dwc3_writel(dwc->regs, DWC3_GUSB3PIPECTL(0), reg);
}

static void dwc3_gadget_usb2_phy_power(struct dwc3 *dwc, int on)
{
	u32			reg;

	reg = dwc3_readl(dwc->regs, DWC3_GUSB2PHYCFG(0));

	if (on)
		reg &= ~DWC3_GUSB2PHYCFG_SUSPHY;
	else
		reg |= DWC3_GUSB2PHYCFG_SUSPHY;

	dwc3_writel(dwc->regs, DWC3_GUSB2PHYCFG(0), reg);
}

static void dwc3_gadget_reset_interrupt(struct dwc3 *dwc)
{
	u32			reg;

	dev_vdbg(dwc->dev, "%s\n", __func__);

	/* Enable PHYs */
	dwc3_gadget_usb2_phy_power(dwc, true);
	dwc3_gadget_usb3_phy_power(dwc, true);

	if (dwc->gadget.speed != USB_SPEED_UNKNOWN)
		dwc3_disconnect_gadget(dwc);

	reg = dwc3_readl(dwc->regs, DWC3_DCTL);
	reg &= ~DWC3_DCTL_TSTCTRL_MASK;
	dwc3_writel(dwc->regs, DWC3_DCTL, reg);

	dwc3_stop_active_transfers(dwc);
	dwc3_clear_stall_all_ep(dwc);
	dwc->start_config_issued = false;

	/* Reset device address to zero */
	reg = dwc3_readl(dwc->regs, DWC3_DCFG);
	reg &= ~(DWC3_DCFG_DEVADDR_MASK);
	dwc3_writel(dwc->regs, DWC3_DCFG, reg);
}

static void dwc3_update_ram_clk_sel(struct dwc3 *dwc, u32 speed)
{
	u32 reg;
	u32 usb30_clock = DWC3_GCTL_CLK_BUS;

	/*
	 * We change the clock only at SS but I dunno why I would want to do
	 * this. Maybe it becomes part of the power saving plan.
	 */

	if (speed != DWC3_DSTS_SUPERSPEED)
		return;

	/*
	 * RAMClkSel is reset to 0 after USB reset, so it must be reprogrammed
	 * each time on Connect Done.
	 */
	if (!usb30_clock)
		return;

	reg = dwc3_readl(dwc->regs, DWC3_GCTL);
	reg |= DWC3_GCTL_RAMCLKSEL(usb30_clock);
	dwc3_writel(dwc->regs, DWC3_GCTL, reg);
}

static void dwc3_gadget_disable_phy(struct dwc3 *dwc, u8 speed)
{
	switch (speed) {
	case USB_SPEED_SUPER:
		dwc3_gadget_usb2_phy_power(dwc, false);
		break;
	case USB_SPEED_HIGH:
	case USB_SPEED_FULL:
	case USB_SPEED_LOW:
		dwc3_gadget_usb3_phy_power(dwc, false);
		break;
	}
}

static void dwc3_gadget_conndone_interrupt(struct dwc3 *dwc)
{
	struct dwc3_gadget_ep_cmd_params params;
	struct dwc3_ep		*dep;
	int			ret;
	u32			reg;
	u8			speed;

	dev_vdbg(dwc->dev, "%s\n", __func__);

	memset(&params, 0x00, sizeof(params));

	reg = dwc3_readl(dwc->regs, DWC3_DSTS);
	speed = reg & DWC3_DSTS_CONNECTSPD;
	dwc->speed = speed;

	dwc3_update_ram_clk_sel(dwc, speed);

	switch (speed) {
	case DWC3_DCFG_SUPERSPEED:
		dwc3_gadget_ep0_desc.wMaxPacketSize = cpu_to_le16(512);
		dwc->gadget.ep0->maxpacket = 512;
		dwc->gadget.speed = USB_SPEED_SUPER;
		break;
	case DWC3_DCFG_HIGHSPEED:
		dwc3_gadget_ep0_desc.wMaxPacketSize = cpu_to_le16(64);
		dwc->gadget.ep0->maxpacket = 64;
		dwc->gadget.speed = USB_SPEED_HIGH;
		break;
	case DWC3_DCFG_FULLSPEED2:
	case DWC3_DCFG_FULLSPEED1:
		dwc3_gadget_ep0_desc.wMaxPacketSize = cpu_to_le16(64);
		dwc->gadget.ep0->maxpacket = 64;
		dwc->gadget.speed = USB_SPEED_FULL;
		break;
	case DWC3_DCFG_LOWSPEED:
		dwc3_gadget_ep0_desc.wMaxPacketSize = cpu_to_le16(8);
		dwc->gadget.ep0->maxpacket = 8;
		dwc->gadget.speed = USB_SPEED_LOW;
		break;
	}

	/* Disable unneded PHY */
	dwc3_gadget_disable_phy(dwc, dwc->gadget.speed);

	dep = dwc->eps[0];
	ret = __dwc3_gadget_ep_enable(dep, &dwc3_gadget_ep0_desc);
	if (ret) {
		dev_err(dwc->dev, "failed to enable %s\n", dep->name);
		return;
	}

	dep = dwc->eps[1];
	ret = __dwc3_gadget_ep_enable(dep, &dwc3_gadget_ep0_desc);
	if (ret) {
		dev_err(dwc->dev, "failed to enable %s\n", dep->name);
		return;
	}

	/*
	 * Configure PHY via GUSB3PIPECTLn if required.
	 *
	 * Update GTXFIFOSIZn
	 *
	 * In both cases reset values should be sufficient.
	 */
}

static void dwc3_gadget_wakeup_interrupt(struct dwc3 *dwc)
{
	dev_vdbg(dwc->dev, "%s\n", __func__);

	/*
	 * TODO take core out of low power mode when that's
	 * implemented.
	 */

	dwc->gadget_driver->resume(&dwc->gadget);
}

static void dwc3_gadget_linksts_change_interrupt(struct dwc3 *dwc,
		unsigned int evtinfo)
{
	/*  The fith bit says SuperSpeed yes or no. */
	dwc->link_state = evtinfo & DWC3_LINK_STATE_MASK;

	dev_vdbg(dwc->dev, "%s link %d\n", __func__, dwc->link_state);
}

static void dwc3_gadget_interrupt(struct dwc3 *dwc,
		const struct dwc3_event_devt *event)
{
	switch (event->type) {
	case DWC3_DEVICE_EVENT_DISCONNECT:
		dwc3_gadget_disconnect_interrupt(dwc);
		break;
	case DWC3_DEVICE_EVENT_RESET:
		dwc3_gadget_reset_interrupt(dwc);
		break;
	case DWC3_DEVICE_EVENT_CONNECT_DONE:
		dwc3_gadget_conndone_interrupt(dwc);
		break;
	case DWC3_DEVICE_EVENT_WAKEUP:
		dwc3_gadget_wakeup_interrupt(dwc);
		break;
	case DWC3_DEVICE_EVENT_LINK_STATUS_CHANGE:
		dwc3_gadget_linksts_change_interrupt(dwc, event->event_info);
		break;
	case DWC3_DEVICE_EVENT_EOPF:
		dev_vdbg(dwc->dev, "End of Periodic Frame\n");
		break;
	case DWC3_DEVICE_EVENT_SOF:
		dev_vdbg(dwc->dev, "Start of Periodic Frame\n");
		break;
	case DWC3_DEVICE_EVENT_ERRATIC_ERROR:
		dev_vdbg(dwc->dev, "Erratic Error\n");
		break;
	case DWC3_DEVICE_EVENT_CMD_CMPL:
		dev_vdbg(dwc->dev, "Command Complete\n");
		break;
	case DWC3_DEVICE_EVENT_OVERFLOW:
		dev_vdbg(dwc->dev, "Overflow\n");
		break;
	default:
		dev_dbg(dwc->dev, "UNKNOWN IRQ %d\n", event->type);
	}
}

static void dwc3_process_event_entry(struct dwc3 *dwc,
		const union dwc3_event *event)
{
	/* Endpoint IRQ, handle it and return early */
	if (event->type.is_devspec == 0) {
		/* depevt */
		return dwc3_endpoint_interrupt(dwc, &event->depevt);
	}

	switch (event->type.type) {
	case DWC3_EVENT_TYPE_DEV:
		dwc3_gadget_interrupt(dwc, &event->devt);
		break;
	/* REVISIT what to do with Carkit and I2C events ? */
	default:
		dev_err(dwc->dev, "UNKNOWN IRQ type %d\n", event->raw);
	}
}

static irqreturn_t dwc3_process_event_buf(struct dwc3 *dwc, u32 buf)
{
	struct dwc3_event_buffer *evt;
	int left;
	u32 count;

	count = dwc3_readl(dwc->regs, DWC3_GEVNTCOUNT(buf));
	count &= DWC3_GEVNTCOUNT_MASK;
	if (!count)
		return IRQ_NONE;

	evt = dwc->ev_buffs[buf];
	left = count;

	while (left > 0) {
		union dwc3_event event;

		memcpy(&event.raw, (evt->buf + evt->lpos), sizeof(event.raw));
		dwc3_process_event_entry(dwc, &event);
		/*
		 * XXX we wrap around correctly to the next entry as almost all
		 * entries are 4 bytes in size. There is one entry which has 12
		 * bytes which is a regular entry followed by 8 bytes data. ATM
		 * I don't know how things are organized if were get next to the
		 * a boundary so I worry about that once we try to handle that.
		 */
		evt->lpos = (evt->lpos + 4) % DWC3_EVENT_BUFFERS_SIZE;
		left -= 4;

		dwc3_writel(dwc->regs, DWC3_GEVNTCOUNT(buf), 4);
	}

	return IRQ_HANDLED;
}

static irqreturn_t dwc3_interrupt(int irq, void *_dwc)
{
	struct dwc3			*dwc = _dwc;
	int				i;
	irqreturn_t			ret = IRQ_NONE;

	spin_lock(&dwc->lock);

	for (i = 0; i < DWC3_EVENT_BUFFERS_NUM; i++) {
		irqreturn_t status;

		status = dwc3_process_event_buf(dwc, i);
		if (status == IRQ_HANDLED)
			ret = status;
	}

	spin_unlock(&dwc->lock);

	return ret;
}

/**
 * dwc3_gadget_init - Initializes gadget related registers
 * @dwc: Pointer to out controller context structure
 *
 * Returns 0 on success otherwise negative errno.
 */
int __devinit dwc3_gadget_init(struct dwc3 *dwc)
{
	u32					reg;
	int					ret;
	int					irq;

	dwc->ctrl_req = dma_alloc_coherent(dwc->dev, sizeof(*dwc->ctrl_req),
			&dwc->ctrl_req_addr, GFP_KERNEL);
	if (!dwc->ctrl_req) {
		dev_err(dwc->dev, "failed to allocate ctrl request\n");
		ret = -ENOMEM;
		goto err0;
	}

	dwc->ep0_trb = dma_alloc_coherent(dwc->dev, sizeof(*dwc->ep0_trb),
			&dwc->ep0_trb_addr, GFP_KERNEL);
	if (!dwc->ep0_trb) {
		dev_err(dwc->dev, "failed to allocate ep0 trb\n");
		ret = -ENOMEM;
		goto err1;
	}

	dwc->setup_buf = dma_alloc_coherent(dwc->dev,
			sizeof(*dwc->setup_buf) * 2,
			&dwc->setup_buf_addr, GFP_KERNEL);
	if (!dwc->setup_buf) {
		dev_err(dwc->dev, "failed to allocate setup buffer\n");
		ret = -ENOMEM;
		goto err2;
	}

	dwc->ep0_bounce = dma_alloc_coherent(dwc->dev,
			512, &dwc->ep0_bounce_addr, GFP_KERNEL);
	if (!dwc->ep0_bounce) {
		dev_err(dwc->dev, "failed to allocate ep0 bounce buffer\n");
		ret = -ENOMEM;
		goto err3;
	}

	dev_set_name(&dwc->gadget.dev, "gadget");

	dwc->gadget.ops			= &dwc3_gadget_ops;
	dwc->gadget.max_speed		= USB_SPEED_SUPER;
	dwc->gadget.speed		= USB_SPEED_UNKNOWN;
	dwc->gadget.dev.parent		= dwc->dev;

	dma_set_coherent_mask(&dwc->gadget.dev, dwc->dev->coherent_dma_mask);

	dwc->gadget.dev.dma_parms	= dwc->dev->dma_parms;
	dwc->gadget.dev.dma_mask	= dwc->dev->dma_mask;
	dwc->gadget.dev.release		= dwc3_gadget_release;
	dwc->gadget.name		= "dwc3-gadget";

	/*
	 * REVISIT: Here we should clear all pending IRQs to be
	 * sure we're starting from a well known location.
	 */

	ret = dwc3_gadget_init_endpoints(dwc);
	if (ret)
		goto err4;

	irq = platform_get_irq(to_platform_device(dwc->dev), 0);

	ret = request_irq(irq, dwc3_interrupt, IRQF_SHARED,
			"dwc3", dwc);
	if (ret) {
		dev_err(dwc->dev, "failed to request irq #%d --> %d\n",
				irq, ret);
		goto err5;
	}

	/* Enable all but Start and End of Frame IRQs */
	reg = (DWC3_DEVTEN_VNDRDEVTSTRCVEDEN |
			DWC3_DEVTEN_EVNTOVERFLOWEN |
			DWC3_DEVTEN_CMDCMPLTEN |
			DWC3_DEVTEN_ERRTICERREN |
			DWC3_DEVTEN_WKUPEVTEN |
			DWC3_DEVTEN_ULSTCNGEN |
			DWC3_DEVTEN_CONNECTDONEEN |
			DWC3_DEVTEN_USBRSTEN |
			DWC3_DEVTEN_DISCONNEVTEN);
	dwc3_writel(dwc->regs, DWC3_DEVTEN, reg);

	ret = device_register(&dwc->gadget.dev);
	if (ret) {
		dev_err(dwc->dev, "failed to register gadget device\n");
		put_device(&dwc->gadget.dev);
		goto err6;
	}

	ret = usb_add_gadget_udc(dwc->dev, &dwc->gadget);
	if (ret) {
		dev_err(dwc->dev, "failed to register udc\n");
		goto err7;
	}

	return 0;

err7:
	device_unregister(&dwc->gadget.dev);

err6:
	dwc3_writel(dwc->regs, DWC3_DEVTEN, 0x00);
	free_irq(irq, dwc);

err5:
	dwc3_gadget_free_endpoints(dwc);

err4:
	dma_free_coherent(dwc->dev, 512, dwc->ep0_bounce,
			dwc->ep0_bounce_addr);

err3:
	dma_free_coherent(dwc->dev, sizeof(*dwc->setup_buf) * 2,
			dwc->setup_buf, dwc->setup_buf_addr);

err2:
	dma_free_coherent(dwc->dev, sizeof(*dwc->ep0_trb),
			dwc->ep0_trb, dwc->ep0_trb_addr);

err1:
	dma_free_coherent(dwc->dev, sizeof(*dwc->ctrl_req),
			dwc->ctrl_req, dwc->ctrl_req_addr);

err0:
	return ret;
}

void dwc3_gadget_exit(struct dwc3 *dwc)
{
	int			irq;
	int			i;

	usb_del_gadget_udc(&dwc->gadget);
	irq = platform_get_irq(to_platform_device(dwc->dev), 0);

	dwc3_writel(dwc->regs, DWC3_DEVTEN, 0x00);
	free_irq(irq, dwc);

	for (i = 0; i < ARRAY_SIZE(dwc->eps); i++)
		__dwc3_gadget_ep_disable(dwc->eps[i]);

	dwc3_gadget_free_endpoints(dwc);

	dma_free_coherent(dwc->dev, 512, dwc->ep0_bounce,
			dwc->ep0_bounce_addr);

	dma_free_coherent(dwc->dev, sizeof(*dwc->setup_buf) * 2,
			dwc->setup_buf, dwc->setup_buf_addr);

	dma_free_coherent(dwc->dev, sizeof(*dwc->ep0_trb),
			dwc->ep0_trb, dwc->ep0_trb_addr);

	dma_free_coherent(dwc->dev, sizeof(*dwc->ctrl_req),
			dwc->ctrl_req, dwc->ctrl_req_addr);

	device_unregister(&dwc->gadget.dev);
}
