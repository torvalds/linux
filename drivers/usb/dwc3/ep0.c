// SPDX-License-Identifier: GPL-2.0
/*
 * ep0.c - DesignWare USB3 DRD Controller Endpoint 0 Handling
 *
 * Copyright (C) 2010-2011 Texas Instruments Incorporated - https://www.ti.com
 *
 * Authors: Felipe Balbi <balbi@ti.com>,
 *	    Sebastian Andrzej Siewior <bigeasy@linutronix.de>
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
#include "debug.h"
#include "gadget.h"
#include "io.h"

static void __dwc3_ep0_do_control_status(struct dwc3 *dwc, struct dwc3_ep *dep);
static void __dwc3_ep0_do_control_data(struct dwc3 *dwc,
		struct dwc3_ep *dep, struct dwc3_request *req);

static void dwc3_ep0_prepare_one_trb(struct dwc3_ep *dep,
		dma_addr_t buf_dma, u32 len, u32 type, bool chain)
{
	struct dwc3_trb			*trb;
	struct dwc3			*dwc;

	dwc = dep->dwc;
	trb = &dwc->ep0_trb[dep->trb_enqueue];

	if (chain)
		dep->trb_enqueue++;

	trb->bpl = lower_32_bits(buf_dma);
	trb->bph = upper_32_bits(buf_dma);
	trb->size = len;
	trb->ctrl = type;

	trb->ctrl |= (DWC3_TRB_CTRL_HWO
			| DWC3_TRB_CTRL_ISP_IMI);

	if (chain)
		trb->ctrl |= DWC3_TRB_CTRL_CHN;
	else
		trb->ctrl |= (DWC3_TRB_CTRL_IOC
				| DWC3_TRB_CTRL_LST);

	trace_dwc3_prepare_trb(dep, trb);
}

static int dwc3_ep0_start_trans(struct dwc3_ep *dep)
{
	struct dwc3_gadget_ep_cmd_params params;
	struct dwc3			*dwc;
	int				ret;

	if (dep->flags & DWC3_EP_TRANSFER_STARTED)
		return 0;

	dwc = dep->dwc;

	memset(&params, 0, sizeof(params));
	params.param0 = upper_32_bits(dwc->ep0_trb_addr);
	params.param1 = lower_32_bits(dwc->ep0_trb_addr);

	ret = dwc3_send_gadget_ep_cmd(dep, DWC3_DEPCMD_STARTTRANSFER, &params);
	if (ret < 0)
		return ret;

	dwc->ep0_next_event = DWC3_EP0_COMPLETE;

	return 0;
}

static int __dwc3_gadget_ep0_queue(struct dwc3_ep *dep,
		struct dwc3_request *req)
{
	struct dwc3		*dwc = dep->dwc;

	req->request.actual	= 0;
	req->request.status	= -EINPROGRESS;
	req->epnum		= dep->number;

	list_add_tail(&req->list, &dep->pending_list);

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
		unsigned int direction;

		direction = !!(dep->flags & DWC3_EP0_DIR_IN);

		if (dwc->ep0state != EP0_DATA_PHASE) {
			dev_WARN(dwc->dev, "Unexpected pending request\n");
			return 0;
		}

		__dwc3_ep0_do_control_data(dwc, dwc->eps[direction], req);

		dep->flags &= ~(DWC3_EP_PENDING_REQUEST |
				DWC3_EP0_DIR_IN);

		return 0;
	}

	/*
	 * In case gadget driver asked us to delay the STATUS phase,
	 * handle it here.
	 */
	if (dwc->delayed_status) {
		unsigned int direction;

		direction = !dwc->ep0_expect_in;
		dwc->delayed_status = false;
		usb_gadget_set_state(dwc->gadget, USB_STATE_CONFIGURED);

		if (dwc->ep0state == EP0_STATUS_PHASE)
			__dwc3_ep0_do_control_status(dwc, dwc->eps[direction]);

		return 0;
	}

	/*
	 * Unfortunately we have uncovered a limitation wrt the Data Phase.
	 *
	 * Section 9.4 says we can wait for the XferNotReady(DATA) event to
	 * come before issueing Start Transfer command, but if we do, we will
	 * miss situations where the host starts another SETUP phase instead of
	 * the DATA phase.  Such cases happen at least on TD.7.6 of the Link
	 * Layer Compliance Suite.
	 *
	 * The problem surfaces due to the fact that in case of back-to-back
	 * SETUP packets there will be no XferNotReady(DATA) generated and we
	 * will be stuck waiting for XferNotReady(DATA) forever.
	 *
	 * By looking at tables 9-13 and 9-14 of the Databook, we can see that
	 * it tells us to start Data Phase right away. It also mentions that if
	 * we receive a SETUP phase instead of the DATA phase, core will issue
	 * XferComplete for the DATA phase, before actually initiating it in
	 * the wire, with the TRB's status set to "SETUP_PENDING". Such status
	 * can only be used to print some debugging logs, as the core expects
	 * us to go through to the STATUS phase and start a CONTROL_STATUS TRB,
	 * just so it completes right away, without transferring anything and,
	 * only then, we can go back to the SETUP phase.
	 *
	 * Because of this scenario, SNPS decided to change the programming
	 * model of control transfers and support on-demand transfers only for
	 * the STATUS phase. To fix the issue we have now, we will always wait
	 * for gadget driver to queue the DATA phase's struct usb_request, then
	 * start it right away.
	 *
	 * If we're actually in a 2-stage transfer, we will wait for
	 * XferNotReady(STATUS).
	 */
	if (dwc->three_stage_setup) {
		unsigned int direction;

		direction = dwc->ep0_expect_in;
		dwc->ep0state = EP0_DATA_PHASE;

		__dwc3_ep0_do_control_data(dwc, dwc->eps[direction], req);

		dep->flags &= ~DWC3_EP0_DIR_IN;
	}

	return 0;
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
	if (!dep->endpoint.desc || !dwc->pullups_connected) {
		dev_err(dwc->dev, "%s: can't queue to disabled endpoint\n",
				dep->name);
		ret = -ESHUTDOWN;
		goto out;
	}

	/* we share one TRB for ep0/1 */
	if (!list_empty(&dep->pending_list)) {
		ret = -EBUSY;
		goto out;
	}

	ret = __dwc3_gadget_ep0_queue(dep, req);

out:
	spin_unlock_irqrestore(&dwc->lock, flags);

	return ret;
}

static void dwc3_ep0_stall_and_restart(struct dwc3 *dwc)
{
	struct dwc3_ep		*dep;

	/* reinitialize physical ep1 */
	dep = dwc->eps[1];
	dep->flags = DWC3_EP_ENABLED;

	/* stall is always issued on EP0 */
	dep = dwc->eps[0];
	__dwc3_gadget_ep_set_halt(dep, 1, false);
	dep->flags = DWC3_EP_ENABLED;
	dwc->delayed_status = false;

	if (!list_empty(&dep->pending_list)) {
		struct dwc3_request	*req;

		req = next_request(&dep->pending_list);
		dwc3_gadget_giveback(dep, req, -ECONNRESET);
	}

	dwc->ep0state = EP0_SETUP_PHASE;
	dwc3_ep0_out_start(dwc);
}

int __dwc3_gadget_ep0_set_halt(struct usb_ep *ep, int value)
{
	struct dwc3_ep			*dep = to_dwc3_ep(ep);
	struct dwc3			*dwc = dep->dwc;

	dwc3_ep0_stall_and_restart(dwc);

	return 0;
}

int dwc3_gadget_ep0_set_halt(struct usb_ep *ep, int value)
{
	struct dwc3_ep			*dep = to_dwc3_ep(ep);
	struct dwc3			*dwc = dep->dwc;
	unsigned long			flags;
	int				ret;

	spin_lock_irqsave(&dwc->lock, flags);
	ret = __dwc3_gadget_ep0_set_halt(ep, value);
	spin_unlock_irqrestore(&dwc->lock, flags);

	return ret;
}

void dwc3_ep0_out_start(struct dwc3 *dwc)
{
	struct dwc3_ep			*dep;
	int				ret;

	complete(&dwc->ep0_in_setup);

	dep = dwc->eps[0];
	dwc3_ep0_prepare_one_trb(dep, dwc->ep0_trb_addr, 8,
			DWC3_TRBCTL_CONTROL_SETUP, false);
	ret = dwc3_ep0_start_trans(dep);
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
	if (dep == NULL)
		return NULL;

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
	u32			value;
	u32			reg;
	u16			usb_status = 0;
	__le16			*response_pkt;

	/* We don't support PTM_STATUS */
	value = le16_to_cpu(ctrl->wValue);
	if (value != 0)
		return -EINVAL;

	recip = ctrl->bRequestType & USB_RECIP_MASK;
	switch (recip) {
	case USB_RECIP_DEVICE:
		/*
		 * LTM will be set once we know how to set this in HW.
		 */
		usb_status |= dwc->gadget->is_selfpowered;

		if ((dwc->speed == DWC3_DSTS_SUPERSPEED) ||
		    (dwc->speed == DWC3_DSTS_SUPERSPEED_PLUS)) {
			reg = dwc3_readl(dwc->regs, DWC3_DCTL);
			if (reg & DWC3_DCTL_INITU1ENA)
				usb_status |= 1 << USB_DEV_STAT_U1_ENABLED;
			if (reg & DWC3_DCTL_INITU2ENA)
				usb_status |= 1 << USB_DEV_STAT_U2_ENABLED;
		}

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
	}

	response_pkt = (__le16 *) dwc->setup_buf;
	*response_pkt = cpu_to_le16(usb_status);

	dep = dwc->eps[0];
	dwc->ep0_usb_req.dep = dep;
	dwc->ep0_usb_req.request.length = sizeof(*response_pkt);
	dwc->ep0_usb_req.request.buf = dwc->setup_buf;
	dwc->ep0_usb_req.request.complete = dwc3_ep0_status_cmpl;

	return __dwc3_gadget_ep0_queue(dep, &dwc->ep0_usb_req);
}

static int dwc3_ep0_handle_u1(struct dwc3 *dwc, enum usb_device_state state,
		int set)
{
	u32 reg;

	if (state != USB_STATE_CONFIGURED)
		return -EINVAL;
	if ((dwc->speed != DWC3_DSTS_SUPERSPEED) &&
			(dwc->speed != DWC3_DSTS_SUPERSPEED_PLUS))
		return -EINVAL;
	if (set && dwc->dis_u1_entry_quirk)
		return -EINVAL;

	reg = dwc3_readl(dwc->regs, DWC3_DCTL);
	if (set)
		reg |= DWC3_DCTL_INITU1ENA;
	else
		reg &= ~DWC3_DCTL_INITU1ENA;
	dwc3_writel(dwc->regs, DWC3_DCTL, reg);

	return 0;
}

static int dwc3_ep0_handle_u2(struct dwc3 *dwc, enum usb_device_state state,
		int set)
{
	u32 reg;


	if (state != USB_STATE_CONFIGURED)
		return -EINVAL;
	if ((dwc->speed != DWC3_DSTS_SUPERSPEED) &&
			(dwc->speed != DWC3_DSTS_SUPERSPEED_PLUS))
		return -EINVAL;
	if (set && dwc->dis_u2_entry_quirk)
		return -EINVAL;

	reg = dwc3_readl(dwc->regs, DWC3_DCTL);
	if (set)
		reg |= DWC3_DCTL_INITU2ENA;
	else
		reg &= ~DWC3_DCTL_INITU2ENA;
	dwc3_writel(dwc->regs, DWC3_DCTL, reg);

	return 0;
}

static int dwc3_ep0_handle_test(struct dwc3 *dwc, enum usb_device_state state,
		u32 wIndex, int set)
{
	if ((wIndex & 0xff) != 0)
		return -EINVAL;
	if (!set)
		return -EINVAL;

	switch (wIndex >> 8) {
	case USB_TEST_J:
	case USB_TEST_K:
	case USB_TEST_SE0_NAK:
	case USB_TEST_PACKET:
	case USB_TEST_FORCE_ENABLE:
		dwc->test_mode_nr = wIndex >> 8;
		dwc->test_mode = true;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int dwc3_ep0_handle_device(struct dwc3 *dwc,
		struct usb_ctrlrequest *ctrl, int set)
{
	enum usb_device_state	state;
	u32			wValue;
	u32			wIndex;
	int			ret = 0;

	wValue = le16_to_cpu(ctrl->wValue);
	wIndex = le16_to_cpu(ctrl->wIndex);
	state = dwc->gadget->state;

	switch (wValue) {
	case USB_DEVICE_REMOTE_WAKEUP:
		break;
	/*
	 * 9.4.1 says only only for SS, in AddressState only for
	 * default control pipe
	 */
	case USB_DEVICE_U1_ENABLE:
		ret = dwc3_ep0_handle_u1(dwc, state, set);
		break;
	case USB_DEVICE_U2_ENABLE:
		ret = dwc3_ep0_handle_u2(dwc, state, set);
		break;
	case USB_DEVICE_LTM_ENABLE:
		ret = -EINVAL;
		break;
	case USB_DEVICE_TEST_MODE:
		ret = dwc3_ep0_handle_test(dwc, state, wIndex, set);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int dwc3_ep0_handle_intf(struct dwc3 *dwc,
		struct usb_ctrlrequest *ctrl, int set)
{
	u32			wValue;
	int			ret = 0;

	wValue = le16_to_cpu(ctrl->wValue);

	switch (wValue) {
	case USB_INTRF_FUNC_SUSPEND:
		/*
		 * REVISIT: Ideally we would enable some low power mode here,
		 * however it's unclear what we should be doing here.
		 *
		 * For now, we're not doing anything, just making sure we return
		 * 0 so USB Command Verifier tests pass without any errors.
		 */
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int dwc3_ep0_handle_endpoint(struct dwc3 *dwc,
		struct usb_ctrlrequest *ctrl, int set)
{
	struct dwc3_ep		*dep;
	u32			wValue;
	int			ret;

	wValue = le16_to_cpu(ctrl->wValue);

	switch (wValue) {
	case USB_ENDPOINT_HALT:
		dep = dwc3_wIndex_to_dep(dwc, ctrl->wIndex);
		if (!dep)
			return -EINVAL;

		if (set == 0 && (dep->flags & DWC3_EP_WEDGE))
			break;

		ret = __dwc3_gadget_ep_set_halt(dep, set, true);
		if (ret)
			return -EINVAL;

		/* ClearFeature(Halt) may need delayed status */
		if (!set && (dep->flags & DWC3_EP_END_TRANSFER_PENDING))
			return USB_GADGET_DELAYED_STATUS;

		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int dwc3_ep0_handle_feature(struct dwc3 *dwc,
		struct usb_ctrlrequest *ctrl, int set)
{
	u32			recip;
	int			ret;

	recip = ctrl->bRequestType & USB_RECIP_MASK;

	switch (recip) {
	case USB_RECIP_DEVICE:
		ret = dwc3_ep0_handle_device(dwc, ctrl, set);
		break;
	case USB_RECIP_INTERFACE:
		ret = dwc3_ep0_handle_intf(dwc, ctrl, set);
		break;
	case USB_RECIP_ENDPOINT:
		ret = dwc3_ep0_handle_endpoint(dwc, ctrl, set);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int dwc3_ep0_set_address(struct dwc3 *dwc, struct usb_ctrlrequest *ctrl)
{
	enum usb_device_state state = dwc->gadget->state;
	u32 addr;
	u32 reg;

	addr = le16_to_cpu(ctrl->wValue);
	if (addr > 127) {
		dev_err(dwc->dev, "invalid device address %d\n", addr);
		return -EINVAL;
	}

	if (state == USB_STATE_CONFIGURED) {
		dev_err(dwc->dev, "can't SetAddress() from Configured State\n");
		return -EINVAL;
	}

	reg = dwc3_readl(dwc->regs, DWC3_DCFG);
	reg &= ~(DWC3_DCFG_DEVADDR_MASK);
	reg |= DWC3_DCFG_DEVADDR(addr);
	dwc3_writel(dwc->regs, DWC3_DCFG, reg);

	if (addr)
		usb_gadget_set_state(dwc->gadget, USB_STATE_ADDRESS);
	else
		usb_gadget_set_state(dwc->gadget, USB_STATE_DEFAULT);

	return 0;
}

static int dwc3_ep0_delegate_req(struct dwc3 *dwc, struct usb_ctrlrequest *ctrl)
{
	int ret = -EINVAL;

	if (dwc->async_callbacks) {
		spin_unlock(&dwc->lock);
		ret = dwc->gadget_driver->setup(dwc->gadget, ctrl);
		spin_lock(&dwc->lock);
	}
	return ret;
}

static int dwc3_ep0_set_config(struct dwc3 *dwc, struct usb_ctrlrequest *ctrl)
{
	enum usb_device_state state = dwc->gadget->state;
	u32 cfg;
	int ret;
	u32 reg;

	cfg = le16_to_cpu(ctrl->wValue);

	switch (state) {
	case USB_STATE_DEFAULT:
		return -EINVAL;

	case USB_STATE_ADDRESS:
		dwc3_gadget_clear_tx_fifos(dwc);

		ret = dwc3_ep0_delegate_req(dwc, ctrl);
		/* if the cfg matches and the cfg is non zero */
		if (cfg && (!ret || (ret == USB_GADGET_DELAYED_STATUS))) {

			/*
			 * only change state if set_config has already
			 * been processed. If gadget driver returns
			 * USB_GADGET_DELAYED_STATUS, we will wait
			 * to change the state on the next usb_ep_queue()
			 */
			if (ret == 0)
				usb_gadget_set_state(dwc->gadget,
						USB_STATE_CONFIGURED);

			/*
			 * Enable transition to U1/U2 state when
			 * nothing is pending from application.
			 */
			reg = dwc3_readl(dwc->regs, DWC3_DCTL);
			if (!dwc->dis_u1_entry_quirk)
				reg |= DWC3_DCTL_ACCEPTU1ENA;
			if (!dwc->dis_u2_entry_quirk)
				reg |= DWC3_DCTL_ACCEPTU2ENA;
			dwc3_writel(dwc->regs, DWC3_DCTL, reg);
		}
		break;

	case USB_STATE_CONFIGURED:
		ret = dwc3_ep0_delegate_req(dwc, ctrl);
		if (!cfg && !ret)
			usb_gadget_set_state(dwc->gadget,
					USB_STATE_ADDRESS);
		break;
	default:
		ret = -EINVAL;
	}
	return ret;
}

static void dwc3_ep0_set_sel_cmpl(struct usb_ep *ep, struct usb_request *req)
{
	struct dwc3_ep	*dep = to_dwc3_ep(ep);
	struct dwc3	*dwc = dep->dwc;

	u32		param = 0;
	u32		reg;

	struct timing {
		u8	u1sel;
		u8	u1pel;
		__le16	u2sel;
		__le16	u2pel;
	} __packed timing;

	int		ret;

	memcpy(&timing, req->buf, sizeof(timing));

	dwc->u1sel = timing.u1sel;
	dwc->u1pel = timing.u1pel;
	dwc->u2sel = le16_to_cpu(timing.u2sel);
	dwc->u2pel = le16_to_cpu(timing.u2pel);

	reg = dwc3_readl(dwc->regs, DWC3_DCTL);
	if (reg & DWC3_DCTL_INITU2ENA)
		param = dwc->u2pel;
	if (reg & DWC3_DCTL_INITU1ENA)
		param = dwc->u1pel;

	/*
	 * According to Synopsys Databook, if parameter is
	 * greater than 125, a value of zero should be
	 * programmed in the register.
	 */
	if (param > 125)
		param = 0;

	/* now that we have the time, issue DGCMD Set Sel */
	ret = dwc3_send_gadget_generic_command(dwc,
			DWC3_DGCMD_SET_PERIODIC_PAR, param);
	WARN_ON(ret < 0);
}

static int dwc3_ep0_set_sel(struct dwc3 *dwc, struct usb_ctrlrequest *ctrl)
{
	struct dwc3_ep	*dep;
	enum usb_device_state state = dwc->gadget->state;
	u16		wLength;

	if (state == USB_STATE_DEFAULT)
		return -EINVAL;

	wLength = le16_to_cpu(ctrl->wLength);

	if (wLength != 6) {
		dev_err(dwc->dev, "Set SEL should be 6 bytes, got %d\n",
				wLength);
		return -EINVAL;
	}

	/*
	 * To handle Set SEL we need to receive 6 bytes from Host. So let's
	 * queue a usb_request for 6 bytes.
	 *
	 * Remember, though, this controller can't handle non-wMaxPacketSize
	 * aligned transfers on the OUT direction, so we queue a request for
	 * wMaxPacketSize instead.
	 */
	dep = dwc->eps[0];
	dwc->ep0_usb_req.dep = dep;
	dwc->ep0_usb_req.request.length = dep->endpoint.maxpacket;
	dwc->ep0_usb_req.request.buf = dwc->setup_buf;
	dwc->ep0_usb_req.request.complete = dwc3_ep0_set_sel_cmpl;

	return __dwc3_gadget_ep0_queue(dep, &dwc->ep0_usb_req);
}

static int dwc3_ep0_set_isoch_delay(struct dwc3 *dwc, struct usb_ctrlrequest *ctrl)
{
	u16		wLength;
	u16		wValue;
	u16		wIndex;

	wValue = le16_to_cpu(ctrl->wValue);
	wLength = le16_to_cpu(ctrl->wLength);
	wIndex = le16_to_cpu(ctrl->wIndex);

	if (wIndex || wLength)
		return -EINVAL;

	dwc->gadget->isoch_delay = wValue;

	return 0;
}

static int dwc3_ep0_std_request(struct dwc3 *dwc, struct usb_ctrlrequest *ctrl)
{
	int ret;

	switch (ctrl->bRequest) {
	case USB_REQ_GET_STATUS:
		ret = dwc3_ep0_handle_status(dwc, ctrl);
		break;
	case USB_REQ_CLEAR_FEATURE:
		ret = dwc3_ep0_handle_feature(dwc, ctrl, 0);
		break;
	case USB_REQ_SET_FEATURE:
		ret = dwc3_ep0_handle_feature(dwc, ctrl, 1);
		break;
	case USB_REQ_SET_ADDRESS:
		ret = dwc3_ep0_set_address(dwc, ctrl);
		break;
	case USB_REQ_SET_CONFIGURATION:
		ret = dwc3_ep0_set_config(dwc, ctrl);
		break;
	case USB_REQ_SET_SEL:
		ret = dwc3_ep0_set_sel(dwc, ctrl);
		break;
	case USB_REQ_SET_ISOCH_DELAY:
		ret = dwc3_ep0_set_isoch_delay(dwc, ctrl);
		break;
	default:
		ret = dwc3_ep0_delegate_req(dwc, ctrl);
		break;
	}

	return ret;
}

static void dwc3_ep0_inspect_setup(struct dwc3 *dwc,
		const struct dwc3_event_depevt *event)
{
	struct usb_ctrlrequest *ctrl = (void *) dwc->ep0_trb;
	int ret = -EINVAL;
	u32 len;

	if (!dwc->gadget_driver)
		goto out;

	trace_dwc3_ctrl_req(ctrl);

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

out:
	if (ret < 0)
		dwc3_ep0_stall_and_restart(dwc);
}

static void dwc3_ep0_complete_data(struct dwc3 *dwc,
		const struct dwc3_event_depevt *event)
{
	struct dwc3_request	*r;
	struct usb_request	*ur;
	struct dwc3_trb		*trb;
	struct dwc3_ep		*ep0;
	u32			transferred = 0;
	u32			status;
	u32			length;
	u8			epnum;

	epnum = event->endpoint_number;
	ep0 = dwc->eps[0];

	dwc->ep0_next_event = DWC3_EP0_NRDY_STATUS;
	trb = dwc->ep0_trb;
	trace_dwc3_complete_trb(ep0, trb);

	r = next_request(&ep0->pending_list);
	if (!r)
		return;

	status = DWC3_TRB_SIZE_TRBSTS(trb->size);
	if (status == DWC3_TRBSTS_SETUP_PENDING) {
		dwc->setup_packet_pending = true;
		if (r)
			dwc3_gadget_giveback(ep0, r, -ECONNRESET);

		return;
	}

	ur = &r->request;

	length = trb->size & DWC3_TRB_SIZE_MASK;
	transferred = ur->length - length;
	ur->actual += transferred;

	if ((IS_ALIGNED(ur->length, ep0->endpoint.maxpacket) &&
	     ur->length && ur->zero) || dwc->ep0_bounced) {
		trb++;
		trb->ctrl &= ~DWC3_TRB_CTRL_HWO;
		trace_dwc3_complete_trb(ep0, trb);

		if (r->direction)
			dwc->eps[1]->trb_enqueue = 0;
		else
			dwc->eps[0]->trb_enqueue = 0;

		dwc->ep0_bounced = false;
	}

	if ((epnum & 1) && ur->actual < ur->length)
		dwc3_ep0_stall_and_restart(dwc);
	else
		dwc3_gadget_giveback(ep0, r, 0);
}

static void dwc3_ep0_complete_status(struct dwc3 *dwc,
		const struct dwc3_event_depevt *event)
{
	struct dwc3_request	*r;
	struct dwc3_ep		*dep;
	struct dwc3_trb		*trb;
	u32			status;

	dep = dwc->eps[0];
	trb = dwc->ep0_trb;

	trace_dwc3_complete_trb(dep, trb);

	if (!list_empty(&dep->pending_list)) {
		r = next_request(&dep->pending_list);

		dwc3_gadget_giveback(dep, r, 0);
	}

	if (dwc->test_mode) {
		int ret;

		ret = dwc3_gadget_set_test_mode(dwc, dwc->test_mode_nr);
		if (ret < 0) {
			dev_err(dwc->dev, "invalid test #%d\n",
					dwc->test_mode_nr);
			dwc3_ep0_stall_and_restart(dwc);
			return;
		}
	}

	status = DWC3_TRB_SIZE_TRBSTS(trb->size);
	if (status == DWC3_TRBSTS_SETUP_PENDING)
		dwc->setup_packet_pending = true;

	dwc->ep0state = EP0_SETUP_PHASE;
	dwc3_ep0_out_start(dwc);
}

static void dwc3_ep0_xfer_complete(struct dwc3 *dwc,
			const struct dwc3_event_depevt *event)
{
	struct dwc3_ep		*dep = dwc->eps[event->endpoint_number];

	dep->flags &= ~DWC3_EP_TRANSFER_STARTED;
	dep->resource_index = 0;
	dwc->setup_packet_pending = false;

	switch (dwc->ep0state) {
	case EP0_SETUP_PHASE:
		dwc3_ep0_inspect_setup(dwc, event);
		break;

	case EP0_DATA_PHASE:
		dwc3_ep0_complete_data(dwc, event);
		break;

	case EP0_STATUS_PHASE:
		dwc3_ep0_complete_status(dwc, event);
		break;
	default:
		WARN(true, "UNKNOWN ep0state %d\n", dwc->ep0state);
	}
}

static void __dwc3_ep0_do_control_data(struct dwc3 *dwc,
		struct dwc3_ep *dep, struct dwc3_request *req)
{
	unsigned int		trb_length = 0;
	int			ret;

	req->direction = !!dep->number;

	if (req->request.length == 0) {
		if (!req->direction)
			trb_length = dep->endpoint.maxpacket;

		dwc3_ep0_prepare_one_trb(dep, dwc->bounce_addr, trb_length,
				DWC3_TRBCTL_CONTROL_DATA, false);
		ret = dwc3_ep0_start_trans(dep);
	} else if (!IS_ALIGNED(req->request.length, dep->endpoint.maxpacket)
			&& (dep->number == 0)) {
		u32	maxpacket;
		u32	rem;

		ret = usb_gadget_map_request_by_dev(dwc->sysdev,
				&req->request, dep->number);
		if (ret)
			return;

		maxpacket = dep->endpoint.maxpacket;
		rem = req->request.length % maxpacket;
		dwc->ep0_bounced = true;

		/* prepare normal TRB */
		dwc3_ep0_prepare_one_trb(dep, req->request.dma,
					 req->request.length,
					 DWC3_TRBCTL_CONTROL_DATA,
					 true);

		req->trb = &dwc->ep0_trb[dep->trb_enqueue - 1];

		/* Now prepare one extra TRB to align transfer size */
		dwc3_ep0_prepare_one_trb(dep, dwc->bounce_addr,
					 maxpacket - rem,
					 DWC3_TRBCTL_CONTROL_DATA,
					 false);
		ret = dwc3_ep0_start_trans(dep);
	} else if (IS_ALIGNED(req->request.length, dep->endpoint.maxpacket) &&
		   req->request.length && req->request.zero) {

		ret = usb_gadget_map_request_by_dev(dwc->sysdev,
				&req->request, dep->number);
		if (ret)
			return;

		/* prepare normal TRB */
		dwc3_ep0_prepare_one_trb(dep, req->request.dma,
					 req->request.length,
					 DWC3_TRBCTL_CONTROL_DATA,
					 true);

		req->trb = &dwc->ep0_trb[dep->trb_enqueue - 1];

		if (!req->direction)
			trb_length = dep->endpoint.maxpacket;

		/* Now prepare one extra TRB to align transfer size */
		dwc3_ep0_prepare_one_trb(dep, dwc->bounce_addr,
					 trb_length, DWC3_TRBCTL_CONTROL_DATA,
					 false);
		ret = dwc3_ep0_start_trans(dep);
	} else {
		ret = usb_gadget_map_request_by_dev(dwc->sysdev,
				&req->request, dep->number);
		if (ret)
			return;

		dwc3_ep0_prepare_one_trb(dep, req->request.dma,
				req->request.length, DWC3_TRBCTL_CONTROL_DATA,
				false);

		req->trb = &dwc->ep0_trb[dep->trb_enqueue];

		ret = dwc3_ep0_start_trans(dep);
	}

	WARN_ON(ret < 0);
}

static int dwc3_ep0_start_control_status(struct dwc3_ep *dep)
{
	struct dwc3		*dwc = dep->dwc;
	u32			type;

	type = dwc->three_stage_setup ? DWC3_TRBCTL_CONTROL_STATUS3
		: DWC3_TRBCTL_CONTROL_STATUS2;

	dwc3_ep0_prepare_one_trb(dep, dwc->ep0_trb_addr, 0, type, false);
	return dwc3_ep0_start_trans(dep);
}

static void __dwc3_ep0_do_control_status(struct dwc3 *dwc, struct dwc3_ep *dep)
{
	WARN_ON(dwc3_ep0_start_control_status(dep));
}

static void dwc3_ep0_do_control_status(struct dwc3 *dwc,
		const struct dwc3_event_depevt *event)
{
	struct dwc3_ep		*dep = dwc->eps[event->endpoint_number];

	__dwc3_ep0_do_control_status(dwc, dep);
}

void dwc3_ep0_send_delayed_status(struct dwc3 *dwc)
{
	unsigned int direction = !dwc->ep0_expect_in;

	dwc->delayed_status = false;

	if (dwc->ep0state != EP0_STATUS_PHASE)
		return;

	__dwc3_ep0_do_control_status(dwc, dwc->eps[direction]);
}

static void dwc3_ep0_end_control_data(struct dwc3 *dwc, struct dwc3_ep *dep)
{
	struct dwc3_gadget_ep_cmd_params params;
	u32			cmd;
	int			ret;

	if (!dep->resource_index)
		return;

	cmd = DWC3_DEPCMD_ENDTRANSFER;
	cmd |= DWC3_DEPCMD_CMDIOC;
	cmd |= DWC3_DEPCMD_PARAM(dep->resource_index);
	memset(&params, 0, sizeof(params));
	ret = dwc3_send_gadget_ep_cmd(dep, cmd, &params);
	WARN_ON_ONCE(ret);
	dep->resource_index = 0;
}

static void dwc3_ep0_xfernotready(struct dwc3 *dwc,
		const struct dwc3_event_depevt *event)
{
	switch (event->status) {
	case DEPEVT_STATUS_CONTROL_DATA:
		/*
		 * We already have a DATA transfer in the controller's cache,
		 * if we receive a XferNotReady(DATA) we will ignore it, unless
		 * it's for the wrong direction.
		 *
		 * In that case, we must issue END_TRANSFER command to the Data
		 * Phase we already have started and issue SetStall on the
		 * control endpoint.
		 */
		if (dwc->ep0_expect_in != event->endpoint_number) {
			struct dwc3_ep	*dep = dwc->eps[dwc->ep0_expect_in];

			dev_err(dwc->dev, "unexpected direction for Data Phase\n");
			dwc3_ep0_end_control_data(dwc, dep);
			dwc3_ep0_stall_and_restart(dwc);
			return;
		}

		break;

	case DEPEVT_STATUS_CONTROL_STATUS:
		if (dwc->ep0_next_event != DWC3_EP0_NRDY_STATUS)
			return;

		dwc->ep0state = EP0_STATUS_PHASE;

		if (dwc->delayed_status) {
			struct dwc3_ep *dep = dwc->eps[0];

			WARN_ON_ONCE(event->endpoint_number != 1);
			/*
			 * We should handle the delay STATUS phase here if the
			 * request for handling delay STATUS has been queued
			 * into the list.
			 */
			if (!list_empty(&dep->pending_list)) {
				dwc->delayed_status = false;
				usb_gadget_set_state(dwc->gadget,
						     USB_STATE_CONFIGURED);
				dwc3_ep0_do_control_status(dwc, event);
			}

			return;
		}

		dwc3_ep0_do_control_status(dwc, event);
	}
}

void dwc3_ep0_interrupt(struct dwc3 *dwc,
		const struct dwc3_event_depevt *event)
{
	struct dwc3_ep	*dep = dwc->eps[event->endpoint_number];
	u8		cmd;

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
		break;
	case DWC3_DEPEVT_EPCMDCMPLT:
		cmd = DEPEVT_PARAMETER_CMD(event->parameters);

		if (cmd == DWC3_DEPCMD_ENDTRANSFER) {
			dep->flags &= ~DWC3_EP_END_TRANSFER_PENDING;
			dep->flags &= ~DWC3_EP_TRANSFER_STARTED;
		}
		break;
	}
}
