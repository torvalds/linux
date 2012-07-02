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

/**
 * dwc3_gadget_set_test_mode - Enables USB2 Test Modes
 * @dwc: pointer to our context structure
 * @mode: the mode to set (J, K SE0 NAK, Force Enable)
 *
 * Caller should take care of locking. This function will
 * return 0 on success or -EINVAL if wrong Test Selector
 * is passed
 */
int dwc3_gadget_set_test_mode(struct dwc3 *dwc, int mode)
{
	u32		reg;

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

	return 0;
}

/**
 * dwc3_gadget_set_link_state - Sets USB Link to a particular State
 * @dwc: pointer to our context structure
 * @state: the state to put link into
 *
 * Caller should take care of locking. This function will
 * return 0 on success or -ETIMEDOUT.
 */
int dwc3_gadget_set_link_state(struct dwc3 *dwc, enum dwc3_link_state state)
{
	int		retries = 10000;
	u32		reg;

	/*
	 * Wait until device controller is ready. Only applies to 1.94a and
	 * later RTL.
	 */
	if (dwc->revision >= DWC3_REVISION_194A) {
		while (--retries) {
			reg = dwc3_readl(dwc->regs, DWC3_DSTS);
			if (reg & DWC3_DSTS_DCNRD)
				udelay(5);
			else
				break;
		}

		if (retries <= 0)
			return -ETIMEDOUT;
	}

	reg = dwc3_readl(dwc->regs, DWC3_DCTL);
	reg &= ~DWC3_DCTL_ULSTCHNGREQ_MASK;

	/* set requested state */
	reg |= DWC3_DCTL_ULSTCHNGREQ(state);
	dwc3_writel(dwc->regs, DWC3_DCTL, reg);

	/*
	 * The following code is racy when called from dwc3_gadget_wakeup,
	 * and is not needed, at least on newer versions
	 */
	if (dwc->revision >= DWC3_REVISION_194A)
		return 0;

	/* wait for a change in DSTS */
	retries = 10000;
	while (--retries) {
		reg = dwc3_readl(dwc->regs, DWC3_DSTS);

		if (DWC3_DSTS_USBLNKST(reg) == state)
			return 0;

		udelay(5);
	}

	dev_vdbg(dwc->dev, "link state change request timed out\n");

	return -ETIMEDOUT;
}

/**
 * dwc3_gadget_resize_tx_fifos - reallocate fifo spaces for current use-case
 * @dwc: pointer to our context structure
 *
 * This function will a best effort FIFO allocation in order
 * to improve FIFO usage and throughput, while still allowing
 * us to enable as many endpoints as possible.
 *
 * Keep in mind that this operation will be highly dependent
 * on the configured size for RAM1 - which contains TxFifo -,
 * the amount of endpoints enabled on coreConsultant tool, and
 * the width of the Master Bus.
 *
 * In the ideal world, we would always be able to satisfy the
 * following equation:
 *
 * ((512 + 2 * MDWIDTH-Bytes) + (Number of IN Endpoints - 1) * \
 * (3 * (1024 + MDWIDTH-Bytes) + MDWIDTH-Bytes)) / MDWIDTH-Bytes
 *
 * Unfortunately, due to many variables that's not always the case.
 */
int dwc3_gadget_resize_tx_fifos(struct dwc3 *dwc)
{
	int		last_fifo_depth = 0;
	int		ram1_depth;
	int		fifo_size;
	int		mdwidth;
	int		num;

	if (!dwc->needs_fifo_resize)
		return 0;

	ram1_depth = DWC3_RAM1_DEPTH(dwc->hwparams.hwparams7);
	mdwidth = DWC3_MDWIDTH(dwc->hwparams.hwparams0);

	/* MDWIDTH is represented in bits, we need it in bytes */
	mdwidth >>= 3;

	/*
	 * FIXME For now we will only allocate 1 wMaxPacketSize space
	 * for each enabled endpoint, later patches will come to
	 * improve this algorithm so that we better use the internal
	 * FIFO space
	 */
	for (num = 0; num < DWC3_ENDPOINTS_NUM; num++) {
		struct dwc3_ep	*dep = dwc->eps[num];
		int		fifo_number = dep->number >> 1;
		int		mult = 1;
		int		tmp;

		if (!(dep->number & 1))
			continue;

		if (!(dep->flags & DWC3_EP_ENABLED))
			continue;

		if (usb_endpoint_xfer_bulk(dep->endpoint.desc)
				|| usb_endpoint_xfer_isoc(dep->endpoint.desc))
			mult = 3;

		/*
		 * REVISIT: the following assumes we will always have enough
		 * space available on the FIFO RAM for all possible use cases.
		 * Make sure that's true somehow and change FIFO allocation
		 * accordingly.
		 *
		 * If we have Bulk or Isochronous endpoints, we want
		 * them to be able to be very, very fast. So we're giving
		 * those endpoints a fifo_size which is enough for 3 full
		 * packets
		 */
		tmp = mult * (dep->endpoint.maxpacket + mdwidth);
		tmp += mdwidth;

		fifo_size = DIV_ROUND_UP(tmp, mdwidth);

		fifo_size |= (last_fifo_depth << 16);

		dev_vdbg(dwc->dev, "%s: Fifo Addr %04x Size %d\n",
				dep->name, last_fifo_depth, fifo_size & 0xffff);

		dwc3_writel(dwc->regs, DWC3_GTXFIFOSIZ(fifo_number),
				fifo_size);

		last_fifo_depth += (fifo_size & 0xffff);
	}

	return 0;
}

void dwc3_gadget_giveback(struct dwc3_ep *dep, struct dwc3_request *req,
		int status)
{
	struct dwc3			*dwc = dep->dwc;

	if (req->queued) {
		if (req->request.num_mapped_sgs)
			dep->busy_slot += req->request.num_mapped_sgs;
		else
			dep->busy_slot++;

		/*
		 * Skip LINK TRB. We can't use req->trb and check for
		 * DWC3_TRBCTL_LINK_TRB because it points the TRB we just
		 * completed (not the LINK TRB).
		 */
		if (((dep->busy_slot & DWC3_TRB_MASK) == DWC3_TRB_NUM - 1) &&
				usb_endpoint_xfer_isoc(dep->endpoint.desc))
			dep->busy_slot++;
	}
	list_del(&req->list);
	req->trb = NULL;

	if (req->request.status == -EINPROGRESS)
		req->request.status = status;

	usb_gadget_unmap_request(&dwc->gadget, &req->request,
			req->direction);

	dev_dbg(dwc->dev, "request %p from %s completed %d/%d ===> %d\n",
			req, dep->name, req->request.actual,
			req->request.length, status);

	spin_unlock(&dwc->lock);
	req->request.complete(&dep->endpoint, &req->request);
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
	case DWC3_DEPCMD_GETEPSTATE:
		return "Get Endpoint State";
	case DWC3_DEPCMD_SETTRANSFRESOURCE:
		return "Set Endpoint Transfer Resource";
	case DWC3_DEPCMD_SETEPCONFIG:
		return "Set Endpoint Configuration";
	default:
		return "UNKNOWN command";
	}
}

int dwc3_send_gadget_generic_command(struct dwc3 *dwc, int cmd, u32 param)
{
	u32		timeout = 500;
	u32		reg;

	dwc3_writel(dwc->regs, DWC3_DGCMDPAR, param);
	dwc3_writel(dwc->regs, DWC3_DGCMD, cmd | DWC3_DGCMD_CMDACT);

	do {
		reg = dwc3_readl(dwc->regs, DWC3_DGCMD);
		if (!(reg & DWC3_DGCMD_CMDACT)) {
			dev_vdbg(dwc->dev, "Command Complete --> %d\n",
					DWC3_DGCMD_STATUS(reg));
			return 0;
		}

		/*
		 * We can't sleep here, because it's also called from
		 * interrupt context.
		 */
		timeout--;
		if (!timeout)
			return -ETIMEDOUT;
		udelay(1);
	} while (1);
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
		struct dwc3_trb *trb)
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
		const struct usb_endpoint_descriptor *desc,
		const struct usb_ss_ep_comp_descriptor *comp_desc)
{
	struct dwc3_gadget_ep_cmd_params params;

	memset(&params, 0x00, sizeof(params));

	params.param0 = DWC3_DEPCFG_EP_TYPE(usb_endpoint_type(desc))
		| DWC3_DEPCFG_MAX_PACKET_SIZE(usb_endpoint_maxp(desc))
		| DWC3_DEPCFG_BURST_SIZE(dep->endpoint.maxburst);

	params.param1 = DWC3_DEPCFG_XFER_COMPLETE_EN
		| DWC3_DEPCFG_XFER_NOT_READY_EN;

	if (usb_ss_max_streams(comp_desc) && usb_endpoint_xfer_bulk(desc)) {
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
		const struct usb_endpoint_descriptor *desc,
		const struct usb_ss_ep_comp_descriptor *comp_desc)
{
	struct dwc3		*dwc = dep->dwc;
	u32			reg;
	int			ret = -ENOMEM;

	if (!(dep->flags & DWC3_EP_ENABLED)) {
		ret = dwc3_gadget_start_config(dwc, dep);
		if (ret)
			return ret;
	}

	ret = dwc3_gadget_set_ep_config(dwc, dep, desc, comp_desc);
	if (ret)
		return ret;

	if (!(dep->flags & DWC3_EP_ENABLED)) {
		struct dwc3_trb	*trb_st_hw;
		struct dwc3_trb	*trb_link;

		ret = dwc3_gadget_set_xfer_resource(dwc, dep);
		if (ret)
			return ret;

		dep->endpoint.desc = desc;
		dep->comp_desc = comp_desc;
		dep->type = usb_endpoint_type(desc);
		dep->flags |= DWC3_EP_ENABLED;

		reg = dwc3_readl(dwc->regs, DWC3_DALEPENA);
		reg |= DWC3_DALEPENA_EP(dep->number);
		dwc3_writel(dwc->regs, DWC3_DALEPENA, reg);

		if (!usb_endpoint_xfer_isoc(desc))
			return 0;

		memset(&trb_link, 0, sizeof(trb_link));

		/* Link TRB for ISOC. The HWO bit is never reset */
		trb_st_hw = &dep->trb_pool[0];

		trb_link = &dep->trb_pool[DWC3_TRB_NUM - 1];

		trb_link->bpl = lower_32_bits(dwc3_trb_dma_offset(dep, trb_st_hw));
		trb_link->bph = upper_32_bits(dwc3_trb_dma_offset(dep, trb_st_hw));
		trb_link->ctrl |= DWC3_TRBCTL_LINK_TRB;
		trb_link->ctrl |= DWC3_TRB_CTRL_HWO;
	}

	return 0;
}

static void dwc3_stop_active_transfer(struct dwc3 *dwc, u32 epnum);
static void dwc3_remove_requests(struct dwc3 *dwc, struct dwc3_ep *dep)
{
	struct dwc3_request		*req;

	if (!list_empty(&dep->req_queued)) {
		dwc3_stop_active_transfer(dwc, dep->number);

		/*
		 * NOTICE: We are violating what the Databook says about the
		 * EndTransfer command. Ideally we would _always_ wait for the
		 * EndTransfer Command Completion IRQ, but that's causing too
		 * much trouble synchronizing between us and gadget driver.
		 *
		 * We have discussed this with the IP Provider and it was
		 * suggested to giveback all requests here, but give HW some
		 * extra time to synchronize with the interconnect. We're using
		 * an arbitraty 100us delay for that.
		 *
		 * Note also that a similar handling was tested by Synopsys
		 * (thanks a lot Paul) and nothing bad has come out of it.
		 * In short, what we're doing is:
		 *
		 * - Issue EndTransfer WITH CMDIOC bit set
		 * - Wait 100us
		 * - giveback all requests to gadget driver
		 */
		udelay(100);

		while (!list_empty(&dep->req_queued)) {
			req = next_request(&dep->req_queued);

			dwc3_gadget_giveback(dep, req, -ESHUTDOWN);
		}
	}

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
	dep->endpoint.desc = NULL;
	dep->comp_desc = NULL;
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
		strlcat(dep->name, "-control", sizeof(dep->name));
		break;
	case USB_ENDPOINT_XFER_ISOC:
		strlcat(dep->name, "-isoc", sizeof(dep->name));
		break;
	case USB_ENDPOINT_XFER_BULK:
		strlcat(dep->name, "-bulk", sizeof(dep->name));
		break;
	case USB_ENDPOINT_XFER_INT:
		strlcat(dep->name, "-int", sizeof(dep->name));
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
	ret = __dwc3_gadget_ep_enable(dep, desc, ep->comp_desc);
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

	return &req->request;
}

static void dwc3_gadget_ep_free_request(struct usb_ep *ep,
		struct usb_request *request)
{
	struct dwc3_request		*req = to_dwc3_request(request);

	kfree(req);
}

/**
 * dwc3_prepare_one_trb - setup one TRB from one request
 * @dep: endpoint for which this request is prepared
 * @req: dwc3_request pointer
 */
static void dwc3_prepare_one_trb(struct dwc3_ep *dep,
		struct dwc3_request *req, dma_addr_t dma,
		unsigned length, unsigned last, unsigned chain)
{
	struct dwc3		*dwc = dep->dwc;
	struct dwc3_trb		*trb;

	unsigned int		cur_slot;

	dev_vdbg(dwc->dev, "%s: req %p dma %08llx length %d%s%s\n",
			dep->name, req, (unsigned long long) dma,
			length, last ? " last" : "",
			chain ? " chain" : "");

	trb = &dep->trb_pool[dep->free_slot & DWC3_TRB_MASK];
	cur_slot = dep->free_slot;
	dep->free_slot++;

	/* Skip the LINK-TRB on ISOC */
	if (((cur_slot & DWC3_TRB_MASK) == DWC3_TRB_NUM - 1) &&
			usb_endpoint_xfer_isoc(dep->endpoint.desc))
		return;

	if (!req->trb) {
		dwc3_gadget_move_request_queued(req);
		req->trb = trb;
		req->trb_dma = dwc3_trb_dma_offset(dep, trb);
	}

	trb->size = DWC3_TRB_SIZE_LENGTH(length);
	trb->bpl = lower_32_bits(dma);
	trb->bph = upper_32_bits(dma);

	switch (usb_endpoint_type(dep->endpoint.desc)) {
	case USB_ENDPOINT_XFER_CONTROL:
		trb->ctrl = DWC3_TRBCTL_CONTROL_SETUP;
		break;

	case USB_ENDPOINT_XFER_ISOC:
		trb->ctrl = DWC3_TRBCTL_ISOCHRONOUS_FIRST;

		if (!req->request.no_interrupt)
			trb->ctrl |= DWC3_TRB_CTRL_IOC;
		break;

	case USB_ENDPOINT_XFER_BULK:
	case USB_ENDPOINT_XFER_INT:
		trb->ctrl = DWC3_TRBCTL_NORMAL;
		break;
	default:
		/*
		 * This is only possible with faulty memory because we
		 * checked it already :)
		 */
		BUG();
	}

	if (usb_endpoint_xfer_isoc(dep->endpoint.desc)) {
		trb->ctrl |= DWC3_TRB_CTRL_ISP_IMI;
		trb->ctrl |= DWC3_TRB_CTRL_CSP;
	} else {
		if (chain)
			trb->ctrl |= DWC3_TRB_CTRL_CHN;

		if (last)
			trb->ctrl |= DWC3_TRB_CTRL_LST;
	}

	if (usb_endpoint_xfer_bulk(dep->endpoint.desc) && dep->stream_capable)
		trb->ctrl |= DWC3_TRB_CTRL_SID_SOFN(req->request.stream_id);

	trb->ctrl |= DWC3_TRB_CTRL_HWO;
}

/*
 * dwc3_prepare_trbs - setup TRBs from requests
 * @dep: endpoint for which requests are being prepared
 * @starting: true if the endpoint is idle and no requests are queued.
 *
 * The function goes through the requests list and sets up TRBs for the
 * transfers. The function returns once there are no more TRBs available or
 * it runs out of requests.
 */
static void dwc3_prepare_trbs(struct dwc3_ep *dep, bool starting)
{
	struct dwc3_request	*req, *n;
	u32			trbs_left;
	u32			max;
	unsigned int		last_one = 0;

	BUILD_BUG_ON_NOT_POWER_OF_2(DWC3_TRB_NUM);

	/* the first request must not be queued */
	trbs_left = (dep->busy_slot - dep->free_slot) & DWC3_TRB_MASK;

	/* Can't wrap around on a non-isoc EP since there's no link TRB */
	if (!usb_endpoint_xfer_isoc(dep->endpoint.desc)) {
		max = DWC3_TRB_NUM - (dep->free_slot & DWC3_TRB_MASK);
		if (trbs_left > max)
			trbs_left = max;
	}

	/*
	 * If busy & slot are equal than it is either full or empty. If we are
	 * starting to process requests then we are empty. Otherwise we are
	 * full and don't do anything
	 */
	if (!trbs_left) {
		if (!starting)
			return;
		trbs_left = DWC3_TRB_NUM;
		/*
		 * In case we start from scratch, we queue the ISOC requests
		 * starting from slot 1. This is done because we use ring
		 * buffer and have no LST bit to stop us. Instead, we place
		 * IOC bit every TRB_NUM/4. We try to avoid having an interrupt
		 * after the first request so we start at slot 1 and have
		 * 7 requests proceed before we hit the first IOC.
		 * Other transfer types don't use the ring buffer and are
		 * processed from the first TRB until the last one. Since we
		 * don't wrap around we have to start at the beginning.
		 */
		if (usb_endpoint_xfer_isoc(dep->endpoint.desc)) {
			dep->busy_slot = 1;
			dep->free_slot = 1;
		} else {
			dep->busy_slot = 0;
			dep->free_slot = 0;
		}
	}

	/* The last TRB is a link TRB, not used for xfer */
	if ((trbs_left <= 1) && usb_endpoint_xfer_isoc(dep->endpoint.desc))
		return;

	list_for_each_entry_safe(req, n, &dep->request_list, list) {
		unsigned	length;
		dma_addr_t	dma;

		if (req->request.num_mapped_sgs > 0) {
			struct usb_request *request = &req->request;
			struct scatterlist *sg = request->sg;
			struct scatterlist *s;
			int		i;

			for_each_sg(sg, s, request->num_mapped_sgs, i) {
				unsigned chain = true;

				length = sg_dma_len(s);
				dma = sg_dma_address(s);

				if (i == (request->num_mapped_sgs - 1) ||
						sg_is_last(s)) {
					last_one = true;
					chain = false;
				}

				trbs_left--;
				if (!trbs_left)
					last_one = true;

				if (last_one)
					chain = false;

				dwc3_prepare_one_trb(dep, req, dma, length,
						last_one, chain);

				if (last_one)
					break;
			}
		} else {
			dma = req->request.dma;
			length = req->request.length;
			trbs_left--;

			if (!trbs_left)
				last_one = 1;

			/* Is this the last request? */
			if (list_is_last(&req->list, &dep->request_list))
				last_one = 1;

			dwc3_prepare_one_trb(dep, req, dma, length,
					last_one, false);

			if (last_one)
				break;
		}
	}
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
		dwc3_prepare_trbs(dep, start_new);

		/*
		 * req points to the first request where HWO changed from 0 to 1
		 */
		req = next_request(&dep->req_queued);
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
		 * requests instead of what we do now.
		 */
		usb_gadget_unmap_request(&dwc->gadget, &req->request,
				req->direction);
		list_del(&req->list);
		return ret;
	}

	dep->flags |= DWC3_EP_BUSY;

	if (start_new) {
		dep->resource_index = dwc3_gadget_ep_get_transfer_index(dwc,
				dep->number);
		WARN_ON_ONCE(!dep->resource_index);
	}

	return 0;
}

static void __dwc3_gadget_start_isoc(struct dwc3 *dwc,
		struct dwc3_ep *dep, u32 cur_uf)
{
	u32 uf;

	if (list_empty(&dep->request_list)) {
		dev_vdbg(dwc->dev, "ISOC ep %s run out for requests.\n",
			dep->name);
		return;
	}

	/* 4 micro frames in the future */
	uf = cur_uf + dep->interval * 4;

	__dwc3_gadget_kick_transfer(dep, uf, 1);
}

static void dwc3_gadget_start_isoc(struct dwc3 *dwc,
		struct dwc3_ep *dep, const struct dwc3_event_depevt *event)
{
	u32 cur_uf, mask;

	mask = ~(dep->interval - 1);
	cur_uf = event->parameters & mask;

	__dwc3_gadget_start_isoc(dwc, dep, cur_uf);
}

static int __dwc3_gadget_ep_queue(struct dwc3_ep *dep, struct dwc3_request *req)
{
	struct dwc3		*dwc = dep->dwc;
	int			ret;

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
	 * many NAKs.
	 */
	ret = usb_gadget_map_request(&dwc->gadget, &req->request,
			dep->direction);
	if (ret)
		return ret;

	list_add_tail(&req->list, &dep->request_list);

	/*
	 * There are a few special cases:
	 *
	 * 1. XferNotReady with empty list of requests. We need to kick the
	 *    transfer here in that situation, otherwise we will be NAKing
	 *    forever. If we get XferNotReady before gadget driver has a
	 *    chance to queue a request, we will ACK the IRQ but won't be
	 *    able to receive the data until the next request is queued.
	 *    The following code is handling exactly that.
	 *
	 */
	if (dep->flags & DWC3_EP_PENDING_REQUEST) {
		int	ret;

		ret = __dwc3_gadget_kick_transfer(dep, 0, true);
		if (ret && ret != -EBUSY) {
			struct dwc3	*dwc = dep->dwc;

			dev_dbg(dwc->dev, "%s: failed to kick transfers\n",
					dep->name);
		}
	}

	/*
	 * 2. XferInProgress on Isoc EP with an active transfer. We need to
	 *    kick the transfer here after queuing a request, otherwise the
	 *    core may not see the modified TRB(s).
	 */
	if (usb_endpoint_xfer_isoc(dep->endpoint.desc) &&
			(dep->flags & DWC3_EP_BUSY)) {
		WARN_ON_ONCE(!dep->resource_index);
		ret = __dwc3_gadget_kick_transfer(dep, dep->resource_index,
				false);
		if (ret && ret != -EBUSY) {
			struct dwc3	*dwc = dep->dwc;

			dev_dbg(dwc->dev, "%s: failed to kick transfers\n",
					dep->name);
		}
	}

	/*
	 * 3. Missed ISOC Handling. We need to start isoc transfer on the saved
	 * uframe number.
	 */
	if (usb_endpoint_xfer_isoc(dep->endpoint.desc) &&
		(dep->flags & DWC3_EP_MISSED_ISOC)) {
			__dwc3_gadget_start_isoc(dwc, dep, dep->current_uf);
			dep->flags &= ~DWC3_EP_MISSED_ISOC;
	}

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

	if (!dep->endpoint.desc) {
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

	if (usb_endpoint_xfer_isoc(dep->endpoint.desc)) {
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
	struct dwc3			*dwc = dep->dwc;
	unsigned long			flags;

	spin_lock_irqsave(&dwc->lock, flags);
	dep->flags |= DWC3_EP_WEDGE;
	spin_unlock_irqrestore(&dwc->lock, flags);

	if (dep->number == 0 || dep->number == 1)
		return dwc3_gadget_ep0_set_halt(ep, 1);
	else
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
	.set_halt	= dwc3_gadget_ep0_set_halt,
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

	ret = dwc3_gadget_set_link_state(dwc, DWC3_LINK_STATE_RECOV);
	if (ret < 0) {
		dev_err(dwc->dev, "failed to put link in Recovery\n");
		goto out;
	}

	/* Recent versions do this automatically */
	if (dwc->revision < DWC3_REVISION_194A) {
		/* write zeroes to Link Change Request */
		reg = dwc3_readl(dwc->regs, DWC3_DCTL);
		reg &= ~DWC3_DCTL_ULSTCHNGREQ_MASK;
		dwc3_writel(dwc->regs, DWC3_DCTL, reg);
	}

	/* poll until Link State changes to ON */
	timeout = jiffies + msecs_to_jiffies(100);

	while (!time_after(jiffies, timeout)) {
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
	unsigned long		flags;

	spin_lock_irqsave(&dwc->lock, flags);
	dwc->is_selfpowered = !!is_selfpowered;
	spin_unlock_irqrestore(&dwc->lock, flags);

	return 0;
}

static void dwc3_gadget_run_stop(struct dwc3 *dwc, int is_on)
{
	u32			reg;
	u32			timeout = 500;

	reg = dwc3_readl(dwc->regs, DWC3_DCTL);
	if (is_on) {
		if (dwc->revision <= DWC3_REVISION_187A) {
			reg &= ~DWC3_DCTL_TRGTULST_MASK;
			reg |= DWC3_DCTL_TRGTULST_RX_DET;
		}

		if (dwc->revision >= DWC3_REVISION_194A)
			reg &= ~DWC3_DCTL_KEEP_CONNECT;
		reg |= DWC3_DCTL_RUN_STOP;
	} else {
		reg &= ~DWC3_DCTL_RUN_STOP;
	}

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

	reg = dwc3_readl(dwc->regs, DWC3_DCFG);
	reg &= ~(DWC3_DCFG_SPEED_MASK);

	/**
	 * WORKAROUND: DWC3 revision < 2.20a have an issue
	 * which would cause metastability state on Run/Stop
	 * bit if we try to force the IP to USB2-only mode.
	 *
	 * Because of that, we cannot configure the IP to any
	 * speed other than the SuperSpeed
	 *
	 * Refers to:
	 *
	 * STAR#9000525659: Clock Domain Crossing on DCTL in
	 * USB 2.0 Mode
	 */
	if (dwc->revision < DWC3_REVISION_220A)
		reg |= DWC3_DCFG_SUPERSPEED;
	else
		reg |= dwc->maximum_speed;
	dwc3_writel(dwc->regs, DWC3_DCFG, reg);

	dwc->start_config_issued = false;

	/* Start with SuperSpeed Default */
	dwc3_gadget_ep0_desc.wMaxPacketSize = cpu_to_le16(512);

	dep = dwc->eps[0];
	ret = __dwc3_gadget_ep_enable(dep, &dwc3_gadget_ep0_desc, NULL);
	if (ret) {
		dev_err(dwc->dev, "failed to enable %s\n", dep->name);
		goto err0;
	}

	dep = dwc->eps[1];
	ret = __dwc3_gadget_ep_enable(dep, &dwc3_gadget_ep0_desc, NULL);
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
			if (ret)
				return ret;
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
	struct dwc3_trb		*trb;
	unsigned int		count;
	unsigned int		s_pkt = 0;
	unsigned int		trb_status;

	do {
		req = next_request(&dep->req_queued);
		if (!req) {
			WARN_ON_ONCE(1);
			return 1;
		}

		trb = req->trb;

		if ((trb->ctrl & DWC3_TRB_CTRL_HWO) && status != -ESHUTDOWN)
			/*
			 * We continue despite the error. There is not much we
			 * can do. If we don't clean it up we loop forever. If
			 * we skip the TRB then it gets overwritten after a
			 * while since we use them in a ring buffer. A BUG()
			 * would help. Lets hope that if this occurs, someone
			 * fixes the root cause instead of looking away :)
			 */
			dev_err(dwc->dev, "%s's TRB (%p) still owned by HW\n",
					dep->name, req->trb);
		count = trb->size & DWC3_TRB_SIZE_MASK;

		if (dep->direction) {
			if (count) {
				trb_status = DWC3_TRB_SIZE_TRBSTS(trb->size);
				if (trb_status == DWC3_TRBSTS_MISSED_ISOC) {
					dev_dbg(dwc->dev, "incomplete IN transfer %s\n",
							dep->name);
					dep->current_uf = event->parameters &
						~(dep->interval - 1);
					dep->flags |= DWC3_EP_MISSED_ISOC;
				} else {
					dev_err(dwc->dev, "incomplete IN transfer %s\n",
							dep->name);
					status = -ECONNRESET;
				}
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
		if ((event->status & DEPEVT_STATUS_LST) &&
				(trb->ctrl & (DWC3_TRB_CTRL_LST |
						DWC3_TRB_CTRL_HWO)))
			break;
		if ((event->status & DEPEVT_STATUS_IOC) &&
				(trb->ctrl & DWC3_TRB_CTRL_IOC))
			break;
	} while (1);

	if ((event->status & DEPEVT_STATUS_IOC) &&
			(trb->ctrl & DWC3_TRB_CTRL_IOC))
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

	clean_busy = dwc3_cleanup_done_reqs(dwc, dep, event, status);
	if (clean_busy)
		dep->flags &= ~DWC3_EP_BUSY;

	/*
	 * WORKAROUND: This is the 2nd half of U1/U2 -> U0 workaround.
	 * See dwc3_gadget_linksts_change_interrupt() for 1st half.
	 */
	if (dwc->revision < DWC3_REVISION_183A) {
		u32		reg;
		int		i;

		for (i = 0; i < DWC3_ENDPOINTS_NUM; i++) {
			struct dwc3_ep	*dep = dwc->eps[i];

			if (!(dep->flags & DWC3_EP_ENABLED))
				continue;

			if (!list_empty(&dep->req_queued))
				return;
		}

		reg = dwc3_readl(dwc->regs, DWC3_DCTL);
		reg |= dwc->u1u2;
		dwc3_writel(dwc->regs, DWC3_DCTL, reg);

		dwc->u1u2 = 0;
	}
}

static void dwc3_endpoint_interrupt(struct dwc3 *dwc,
		const struct dwc3_event_depevt *event)
{
	struct dwc3_ep		*dep;
	u8			epnum = event->endpoint_number;

	dep = dwc->eps[epnum];

	if (!(dep->flags & DWC3_EP_ENABLED))
		return;

	dev_vdbg(dwc->dev, "%s: %s\n", dep->name,
			dwc3_ep_event_string(event->endpoint_event));

	if (epnum == 0 || epnum == 1) {
		dwc3_ep0_interrupt(dwc, event);
		return;
	}

	switch (event->endpoint_event) {
	case DWC3_DEPEVT_XFERCOMPLETE:
		dep->resource_index = 0;

		if (usb_endpoint_xfer_isoc(dep->endpoint.desc)) {
			dev_dbg(dwc->dev, "%s is an Isochronous endpoint\n",
					dep->name);
			return;
		}

		dwc3_endpoint_transfer_complete(dwc, dep, event, 1);
		break;
	case DWC3_DEPEVT_XFERINPROGRESS:
		if (!usb_endpoint_xfer_isoc(dep->endpoint.desc)) {
			dev_dbg(dwc->dev, "%s is not an Isochronous endpoint\n",
					dep->name);
			return;
		}

		dwc3_endpoint_transfer_complete(dwc, dep, event, 0);
		break;
	case DWC3_DEPEVT_XFERNOTREADY:
		if (usb_endpoint_xfer_isoc(dep->endpoint.desc)) {
			dwc3_gadget_start_isoc(dwc, dep, event);
		} else {
			int ret;

			dev_vdbg(dwc->dev, "%s: reason %s\n",
					dep->name, event->status &
					DEPEVT_STATUS_TRANSFER_ACTIVE
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
		if (!usb_endpoint_xfer_bulk(dep->endpoint.desc)) {
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
		dev_vdbg(dwc->dev, "Endpoint Command Complete\n");
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

	if (!dep->resource_index)
		return;

	cmd = DWC3_DEPCMD_ENDTRANSFER;
	cmd |= DWC3_DEPCMD_HIPRI_FORCERM | DWC3_DEPCMD_CMDIOC;
	cmd |= DWC3_DEPCMD_PARAM(dep->resource_index);
	memset(&params, 0, sizeof(params));
	ret = dwc3_send_gadget_ep_cmd(dwc, dep->number, cmd, &params);
	WARN_ON_ONCE(ret);
	dep->resource_index = 0;
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
	int			reg;

	dev_vdbg(dwc->dev, "%s\n", __func__);

	reg = dwc3_readl(dwc->regs, DWC3_DCTL);
	reg &= ~DWC3_DCTL_INITU1ENA;
	dwc3_writel(dwc->regs, DWC3_DCTL, reg);

	reg &= ~DWC3_DCTL_INITU2ENA;
	dwc3_writel(dwc->regs, DWC3_DCTL, reg);

	dwc3_disconnect_gadget(dwc);
	dwc->start_config_issued = false;

	dwc->gadget.speed = USB_SPEED_UNKNOWN;
	dwc->setup_packet_pending = false;
}

static void dwc3_gadget_usb3_phy_suspend(struct dwc3 *dwc, int suspend)
{
	u32			reg;

	reg = dwc3_readl(dwc->regs, DWC3_GUSB3PIPECTL(0));

	if (suspend)
		reg |= DWC3_GUSB3PIPECTL_SUSPHY;
	else
		reg &= ~DWC3_GUSB3PIPECTL_SUSPHY;

	dwc3_writel(dwc->regs, DWC3_GUSB3PIPECTL(0), reg);
}

static void dwc3_gadget_usb2_phy_suspend(struct dwc3 *dwc, int suspend)
{
	u32			reg;

	reg = dwc3_readl(dwc->regs, DWC3_GUSB2PHYCFG(0));

	if (suspend)
		reg |= DWC3_GUSB2PHYCFG_SUSPHY;
	else
		reg &= ~DWC3_GUSB2PHYCFG_SUSPHY;

	dwc3_writel(dwc->regs, DWC3_GUSB2PHYCFG(0), reg);
}

static void dwc3_gadget_reset_interrupt(struct dwc3 *dwc)
{
	u32			reg;

	dev_vdbg(dwc->dev, "%s\n", __func__);

	/*
	 * WORKAROUND: DWC3 revisions <1.88a have an issue which
	 * would cause a missing Disconnect Event if there's a
	 * pending Setup Packet in the FIFO.
	 *
	 * There's no suggested workaround on the official Bug
	 * report, which states that "unless the driver/application
	 * is doing any special handling of a disconnect event,
	 * there is no functional issue".
	 *
	 * Unfortunately, it turns out that we _do_ some special
	 * handling of a disconnect event, namely complete all
	 * pending transfers, notify gadget driver of the
	 * disconnection, and so on.
	 *
	 * Our suggested workaround is to follow the Disconnect
	 * Event steps here, instead, based on a setup_packet_pending
	 * flag. Such flag gets set whenever we have a XferNotReady
	 * event on EP0 and gets cleared on XferComplete for the
	 * same endpoint.
	 *
	 * Refers to:
	 *
	 * STAR#9000466709: RTL: Device : Disconnect event not
	 * generated if setup packet pending in FIFO
	 */
	if (dwc->revision < DWC3_REVISION_188A) {
		if (dwc->setup_packet_pending)
			dwc3_gadget_disconnect_interrupt(dwc);
	}

	/* after reset -> Default State */
	dwc->dev_state = DWC3_DEFAULT_STATE;

	/* Recent versions support automatic phy suspend and don't need this */
	if (dwc->revision < DWC3_REVISION_194A) {
		/* Resume PHYs */
		dwc3_gadget_usb2_phy_suspend(dwc, false);
		dwc3_gadget_usb3_phy_suspend(dwc, false);
	}

	if (dwc->gadget.speed != USB_SPEED_UNKNOWN)
		dwc3_disconnect_gadget(dwc);

	reg = dwc3_readl(dwc->regs, DWC3_DCTL);
	reg &= ~DWC3_DCTL_TSTCTRL_MASK;
	reg &= ~(DWC3_DCTL_INITU1ENA | DWC3_DCTL_INITU2ENA);
	reg |= (DWC3_DCTL_ACCEPTU1ENA | DWC3_DCTL_ACCEPTU2ENA);
	dwc3_writel(dwc->regs, DWC3_DCTL, reg);
	dwc->test_mode = false;

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

static void dwc3_gadget_phy_suspend(struct dwc3 *dwc, u8 speed)
{
	switch (speed) {
	case USB_SPEED_SUPER:
		dwc3_gadget_usb2_phy_suspend(dwc, true);
		break;
	case USB_SPEED_HIGH:
	case USB_SPEED_FULL:
	case USB_SPEED_LOW:
		dwc3_gadget_usb3_phy_suspend(dwc, true);
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
		/*
		 * WORKAROUND: DWC3 revisions <1.90a have an issue which
		 * would cause a missing USB3 Reset event.
		 *
		 * In such situations, we should force a USB3 Reset
		 * event by calling our dwc3_gadget_reset_interrupt()
		 * routine.
		 *
		 * Refers to:
		 *
		 * STAR#9000483510: RTL: SS : USB3 reset event may
		 * not be generated always when the link enters poll
		 */
		if (dwc->revision < DWC3_REVISION_190A)
			dwc3_gadget_reset_interrupt(dwc);

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

	/* Recent versions support automatic phy suspend and don't need this */
	if (dwc->revision < DWC3_REVISION_194A) {
		/* Suspend unneeded PHY */
		dwc3_gadget_phy_suspend(dwc, dwc->gadget.speed);
	}

	dep = dwc->eps[0];
	ret = __dwc3_gadget_ep_enable(dep, &dwc3_gadget_ep0_desc, NULL);
	if (ret) {
		dev_err(dwc->dev, "failed to enable %s\n", dep->name);
		return;
	}

	dep = dwc->eps[1];
	ret = __dwc3_gadget_ep_enable(dep, &dwc3_gadget_ep0_desc, NULL);
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
	enum dwc3_link_state	next = evtinfo & DWC3_LINK_STATE_MASK;

	/*
	 * WORKAROUND: DWC3 Revisions <1.83a have an issue which, depending
	 * on the link partner, the USB session might do multiple entry/exit
	 * of low power states before a transfer takes place.
	 *
	 * Due to this problem, we might experience lower throughput. The
	 * suggested workaround is to disable DCTL[12:9] bits if we're
	 * transitioning from U1/U2 to U0 and enable those bits again
	 * after a transfer completes and there are no pending transfers
	 * on any of the enabled endpoints.
	 *
	 * This is the first half of that workaround.
	 *
	 * Refers to:
	 *
	 * STAR#9000446952: RTL: Device SS : if U1/U2 ->U0 takes >128us
	 * core send LGO_Ux entering U0
	 */
	if (dwc->revision < DWC3_REVISION_183A) {
		if (next == DWC3_LINK_STATE_U0) {
			u32	u1u2;
			u32	reg;

			switch (dwc->link_state) {
			case DWC3_LINK_STATE_U1:
			case DWC3_LINK_STATE_U2:
				reg = dwc3_readl(dwc->regs, DWC3_DCTL);
				u1u2 = reg & (DWC3_DCTL_INITU2ENA
						| DWC3_DCTL_ACCEPTU2ENA
						| DWC3_DCTL_INITU1ENA
						| DWC3_DCTL_ACCEPTU1ENA);

				if (!dwc->u1u2)
					dwc->u1u2 = reg & u1u2;

				reg &= ~u1u2;

				dwc3_writel(dwc->regs, DWC3_DCTL, reg);
				break;
			default:
				/* do nothing */
				break;
			}
		}
	}

	dwc->link_state = next;

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

		event.raw = *(u32 *) (evt->buf + evt->lpos);

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

	for (i = 0; i < dwc->num_event_buffers; i++) {
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
 * @dwc: pointer to our controller context structure
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

	dwc->setup_buf = kzalloc(DWC3_EP0_BOUNCE_SIZE, GFP_KERNEL);
	if (!dwc->setup_buf) {
		dev_err(dwc->dev, "failed to allocate setup buffer\n");
		ret = -ENOMEM;
		goto err2;
	}

	dwc->ep0_bounce = dma_alloc_coherent(dwc->dev,
			DWC3_EP0_BOUNCE_SIZE, &dwc->ep0_bounce_addr,
			GFP_KERNEL);
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
	dwc->gadget.sg_supported	= true;

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

	reg = dwc3_readl(dwc->regs, DWC3_DCFG);
	reg |= DWC3_DCFG_LPM_CAP;
	dwc3_writel(dwc->regs, DWC3_DCFG, reg);

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

	/* Enable USB2 LPM and automatic phy suspend only on recent versions */
	if (dwc->revision >= DWC3_REVISION_194A) {
		reg = dwc3_readl(dwc->regs, DWC3_DCFG);
		reg |= DWC3_DCFG_LPM_CAP;
		dwc3_writel(dwc->regs, DWC3_DCFG, reg);

		reg = dwc3_readl(dwc->regs, DWC3_DCTL);
		reg &= ~(DWC3_DCTL_HIRD_THRES_MASK | DWC3_DCTL_L1_HIBER_EN);

		/* TODO: This should be configurable */
		reg |= DWC3_DCTL_HIRD_THRES(28);

		dwc3_writel(dwc->regs, DWC3_DCTL, reg);

		dwc3_gadget_usb2_phy_suspend(dwc, false);
		dwc3_gadget_usb3_phy_suspend(dwc, false);
	}

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
	dma_free_coherent(dwc->dev, DWC3_EP0_BOUNCE_SIZE,
			dwc->ep0_bounce, dwc->ep0_bounce_addr);

err3:
	kfree(dwc->setup_buf);

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

	usb_del_gadget_udc(&dwc->gadget);
	irq = platform_get_irq(to_platform_device(dwc->dev), 0);

	dwc3_writel(dwc->regs, DWC3_DEVTEN, 0x00);
	free_irq(irq, dwc);

	dwc3_gadget_free_endpoints(dwc);

	dma_free_coherent(dwc->dev, DWC3_EP0_BOUNCE_SIZE,
			dwc->ep0_bounce, dwc->ep0_bounce_addr);

	kfree(dwc->setup_buf);

	dma_free_coherent(dwc->dev, sizeof(*dwc->ep0_trb),
			dwc->ep0_trb, dwc->ep0_trb_addr);

	dma_free_coherent(dwc->dev, sizeof(*dwc->ctrl_req),
			dwc->ctrl_req, dwc->ctrl_req_addr);

	device_unregister(&dwc->gadget.dev);
}
