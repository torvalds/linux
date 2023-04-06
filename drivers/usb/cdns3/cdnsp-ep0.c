// SPDX-License-Identifier: GPL-2.0
/*
 * Cadence CDNSP DRD Driver.
 *
 * Copyright (C) 2020 Cadence.
 *
 * Author: Pawel Laszczak <pawell@cadence.com>
 *
 */

#include <linux/usb/composite.h>
#include <linux/usb/gadget.h>
#include <linux/list.h>

#include "cdnsp-gadget.h"
#include "cdnsp-trace.h"

static void cdnsp_ep0_stall(struct cdnsp_device *pdev)
{
	struct cdnsp_request *preq;
	struct cdnsp_ep *pep;

	pep = &pdev->eps[0];
	preq = next_request(&pep->pending_list);

	if (pdev->three_stage_setup) {
		cdnsp_halt_endpoint(pdev, pep, true);

		if (preq)
			cdnsp_gadget_giveback(pep, preq, -ECONNRESET);
	} else {
		pep->ep_state |= EP0_HALTED_STATUS;

		if (preq)
			list_del(&preq->list);

		cdnsp_status_stage(pdev);
	}
}

static int cdnsp_ep0_delegate_req(struct cdnsp_device *pdev,
				  struct usb_ctrlrequest *ctrl)
{
	int ret;

	spin_unlock(&pdev->lock);
	ret = pdev->gadget_driver->setup(&pdev->gadget, ctrl);
	spin_lock(&pdev->lock);

	return ret;
}

static int cdnsp_ep0_set_config(struct cdnsp_device *pdev,
				struct usb_ctrlrequest *ctrl)
{
	enum usb_device_state state = pdev->gadget.state;
	u32 cfg;
	int ret;

	cfg = le16_to_cpu(ctrl->wValue);

	switch (state) {
	case USB_STATE_ADDRESS:
		trace_cdnsp_ep0_set_config("from Address state");
		break;
	case USB_STATE_CONFIGURED:
		trace_cdnsp_ep0_set_config("from Configured state");
		break;
	default:
		dev_err(pdev->dev, "Set Configuration - bad device state\n");
		return -EINVAL;
	}

	ret = cdnsp_ep0_delegate_req(pdev, ctrl);
	if (ret)
		return ret;

	if (!cfg)
		usb_gadget_set_state(&pdev->gadget, USB_STATE_ADDRESS);

	return 0;
}

static int cdnsp_ep0_set_address(struct cdnsp_device *pdev,
				 struct usb_ctrlrequest *ctrl)
{
	enum usb_device_state state = pdev->gadget.state;
	struct cdnsp_slot_ctx *slot_ctx;
	unsigned int slot_state;
	int ret;
	u32 addr;

	addr = le16_to_cpu(ctrl->wValue);

	if (addr > 127) {
		dev_err(pdev->dev, "Invalid device address %d\n", addr);
		return -EINVAL;
	}

	slot_ctx = cdnsp_get_slot_ctx(&pdev->out_ctx);

	if (state == USB_STATE_CONFIGURED) {
		dev_err(pdev->dev, "Can't Set Address from Configured State\n");
		return -EINVAL;
	}

	pdev->device_address = le16_to_cpu(ctrl->wValue);

	slot_ctx = cdnsp_get_slot_ctx(&pdev->out_ctx);
	slot_state = GET_SLOT_STATE(le32_to_cpu(slot_ctx->dev_state));
	if (slot_state == SLOT_STATE_ADDRESSED)
		cdnsp_reset_device(pdev);

	/*set device address*/
	ret = cdnsp_setup_device(pdev, SETUP_CONTEXT_ADDRESS);
	if (ret)
		return ret;

	if (addr)
		usb_gadget_set_state(&pdev->gadget, USB_STATE_ADDRESS);
	else
		usb_gadget_set_state(&pdev->gadget, USB_STATE_DEFAULT);

	return 0;
}

int cdnsp_status_stage(struct cdnsp_device *pdev)
{
	pdev->ep0_stage = CDNSP_STATUS_STAGE;
	pdev->ep0_preq.request.length = 0;

	return cdnsp_ep_enqueue(pdev->ep0_preq.pep, &pdev->ep0_preq);
}

static int cdnsp_w_index_to_ep_index(u16 wIndex)
{
	if (!(wIndex & USB_ENDPOINT_NUMBER_MASK))
		return 0;

	return ((wIndex & USB_ENDPOINT_NUMBER_MASK) * 2) +
		(wIndex & USB_ENDPOINT_DIR_MASK ? 1 : 0) - 1;
}

static int cdnsp_ep0_handle_status(struct cdnsp_device *pdev,
				   struct usb_ctrlrequest *ctrl)
{
	struct cdnsp_ep *pep;
	__le16 *response;
	int ep_sts = 0;
	u16 status = 0;
	u32 recipient;

	recipient = ctrl->bRequestType & USB_RECIP_MASK;

	switch (recipient) {
	case USB_RECIP_DEVICE:
		status = pdev->gadget.is_selfpowered;
		status |= pdev->may_wakeup << USB_DEVICE_REMOTE_WAKEUP;

		if (pdev->gadget.speed >= USB_SPEED_SUPER) {
			status |= pdev->u1_allowed << USB_DEV_STAT_U1_ENABLED;
			status |= pdev->u2_allowed << USB_DEV_STAT_U2_ENABLED;
		}
		break;
	case USB_RECIP_INTERFACE:
		/*
		 * Function Remote Wake Capable	D0
		 * Function Remote Wakeup	D1
		 */
		return cdnsp_ep0_delegate_req(pdev, ctrl);
	case USB_RECIP_ENDPOINT:
		ep_sts = cdnsp_w_index_to_ep_index(le16_to_cpu(ctrl->wIndex));
		pep = &pdev->eps[ep_sts];
		ep_sts = GET_EP_CTX_STATE(pep->out_ctx);

		/* check if endpoint is stalled */
		if (ep_sts == EP_STATE_HALTED)
			status =  BIT(USB_ENDPOINT_HALT);
		break;
	default:
		return -EINVAL;
	}

	response = (__le16 *)pdev->setup_buf;
	*response = cpu_to_le16(status);

	pdev->ep0_preq.request.length = sizeof(*response);
	pdev->ep0_preq.request.buf = pdev->setup_buf;

	return cdnsp_ep_enqueue(pdev->ep0_preq.pep, &pdev->ep0_preq);
}

static void cdnsp_enter_test_mode(struct cdnsp_device *pdev)
{
	u32 temp;

	temp = readl(&pdev->active_port->regs->portpmsc) & ~GENMASK(31, 28);
	temp |= PORT_TEST_MODE(pdev->test_mode);
	writel(temp, &pdev->active_port->regs->portpmsc);
}

static int cdnsp_ep0_handle_feature_device(struct cdnsp_device *pdev,
					   struct usb_ctrlrequest *ctrl,
					   int set)
{
	enum usb_device_state state;
	enum usb_device_speed speed;
	u16 tmode;

	state = pdev->gadget.state;
	speed = pdev->gadget.speed;

	switch (le16_to_cpu(ctrl->wValue)) {
	case USB_DEVICE_REMOTE_WAKEUP:
		pdev->may_wakeup = !!set;
		trace_cdnsp_may_wakeup(set);
		break;
	case USB_DEVICE_U1_ENABLE:
		if (state != USB_STATE_CONFIGURED || speed < USB_SPEED_SUPER)
			return -EINVAL;

		pdev->u1_allowed = !!set;
		trace_cdnsp_u1(set);
		break;
	case USB_DEVICE_U2_ENABLE:
		if (state != USB_STATE_CONFIGURED || speed < USB_SPEED_SUPER)
			return -EINVAL;

		pdev->u2_allowed = !!set;
		trace_cdnsp_u2(set);
		break;
	case USB_DEVICE_LTM_ENABLE:
		return -EINVAL;
	case USB_DEVICE_TEST_MODE:
		if (state != USB_STATE_CONFIGURED || speed > USB_SPEED_HIGH)
			return -EINVAL;

		tmode = le16_to_cpu(ctrl->wIndex);

		if (!set || (tmode & 0xff) != 0)
			return -EINVAL;

		tmode = tmode >> 8;

		if (tmode > USB_TEST_FORCE_ENABLE || tmode < USB_TEST_J)
			return -EINVAL;

		pdev->test_mode = tmode;

		/*
		 * Test mode must be set before Status Stage but controller
		 * will start testing sequence after Status Stage.
		 */
		cdnsp_enter_test_mode(pdev);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int cdnsp_ep0_handle_feature_intf(struct cdnsp_device *pdev,
					 struct usb_ctrlrequest *ctrl,
					 int set)
{
	u16 wValue, wIndex;
	int ret;

	wValue = le16_to_cpu(ctrl->wValue);
	wIndex = le16_to_cpu(ctrl->wIndex);

	switch (wValue) {
	case USB_INTRF_FUNC_SUSPEND:
		ret = cdnsp_ep0_delegate_req(pdev, ctrl);
		if (ret)
			return ret;

		/*
		 * Remote wakeup is enabled when any function within a device
		 * is enabled for function remote wakeup.
		 */
		if (wIndex & USB_INTRF_FUNC_SUSPEND_RW)
			pdev->may_wakeup++;
		else
			if (pdev->may_wakeup > 0)
				pdev->may_wakeup--;

		return 0;
	default:
		return -EINVAL;
	}

	return 0;
}

static int cdnsp_ep0_handle_feature_endpoint(struct cdnsp_device *pdev,
					     struct usb_ctrlrequest *ctrl,
					     int set)
{
	struct cdnsp_ep *pep;
	u16 wValue;

	wValue = le16_to_cpu(ctrl->wValue);
	pep = &pdev->eps[cdnsp_w_index_to_ep_index(le16_to_cpu(ctrl->wIndex))];

	switch (wValue) {
	case USB_ENDPOINT_HALT:
		if (!set && (pep->ep_state & EP_WEDGE)) {
			/* Resets Sequence Number */
			cdnsp_halt_endpoint(pdev, pep, 0);
			cdnsp_halt_endpoint(pdev, pep, 1);
			break;
		}

		return cdnsp_halt_endpoint(pdev, pep, set);
	default:
		dev_warn(pdev->dev, "WARN Incorrect wValue %04x\n", wValue);
		return -EINVAL;
	}

	return 0;
}

static int cdnsp_ep0_handle_feature(struct cdnsp_device *pdev,
				    struct usb_ctrlrequest *ctrl,
				    int set)
{
	switch (ctrl->bRequestType & USB_RECIP_MASK) {
	case USB_RECIP_DEVICE:
		return cdnsp_ep0_handle_feature_device(pdev, ctrl, set);
	case USB_RECIP_INTERFACE:
		return cdnsp_ep0_handle_feature_intf(pdev, ctrl, set);
	case USB_RECIP_ENDPOINT:
		return cdnsp_ep0_handle_feature_endpoint(pdev, ctrl, set);
	default:
		return -EINVAL;
	}
}

static int cdnsp_ep0_set_sel(struct cdnsp_device *pdev,
			     struct usb_ctrlrequest *ctrl)
{
	enum usb_device_state state = pdev->gadget.state;
	u16 wLength;

	if (state == USB_STATE_DEFAULT)
		return -EINVAL;

	wLength = le16_to_cpu(ctrl->wLength);

	if (wLength != 6) {
		dev_err(pdev->dev, "Set SEL should be 6 bytes, got %d\n",
			wLength);
		return -EINVAL;
	}

	/*
	 * To handle Set SEL we need to receive 6 bytes from Host. So let's
	 * queue a usb_request for 6 bytes.
	 */
	pdev->ep0_preq.request.length = 6;
	pdev->ep0_preq.request.buf = pdev->setup_buf;

	return cdnsp_ep_enqueue(pdev->ep0_preq.pep, &pdev->ep0_preq);
}

static int cdnsp_ep0_set_isoch_delay(struct cdnsp_device *pdev,
				     struct usb_ctrlrequest *ctrl)
{
	if (le16_to_cpu(ctrl->wIndex) || le16_to_cpu(ctrl->wLength))
		return -EINVAL;

	pdev->gadget.isoch_delay = le16_to_cpu(ctrl->wValue);

	return 0;
}

static int cdnsp_ep0_std_request(struct cdnsp_device *pdev,
				 struct usb_ctrlrequest *ctrl)
{
	int ret;

	switch (ctrl->bRequest) {
	case USB_REQ_GET_STATUS:
		ret = cdnsp_ep0_handle_status(pdev, ctrl);
		break;
	case USB_REQ_CLEAR_FEATURE:
		ret = cdnsp_ep0_handle_feature(pdev, ctrl, 0);
		break;
	case USB_REQ_SET_FEATURE:
		ret = cdnsp_ep0_handle_feature(pdev, ctrl, 1);
		break;
	case USB_REQ_SET_ADDRESS:
		ret = cdnsp_ep0_set_address(pdev, ctrl);
		break;
	case USB_REQ_SET_CONFIGURATION:
		ret = cdnsp_ep0_set_config(pdev, ctrl);
		break;
	case USB_REQ_SET_SEL:
		ret = cdnsp_ep0_set_sel(pdev, ctrl);
		break;
	case USB_REQ_SET_ISOCH_DELAY:
		ret = cdnsp_ep0_set_isoch_delay(pdev, ctrl);
		break;
	default:
		ret = cdnsp_ep0_delegate_req(pdev, ctrl);
		break;
	}

	return ret;
}

void cdnsp_setup_analyze(struct cdnsp_device *pdev)
{
	struct usb_ctrlrequest *ctrl = &pdev->setup;
	int ret = 0;
	u16 len;

	trace_cdnsp_ctrl_req(ctrl);

	if (!pdev->gadget_driver)
		goto out;

	if (pdev->gadget.state == USB_STATE_NOTATTACHED) {
		dev_err(pdev->dev, "ERR: Setup detected in unattached state\n");
		ret = -EINVAL;
		goto out;
	}

	/* Restore the ep0 to Stopped/Running state. */
	if (pdev->eps[0].ep_state & EP_HALTED) {
		trace_cdnsp_ep0_halted("Restore to normal state");
		cdnsp_halt_endpoint(pdev, &pdev->eps[0], 0);
	}

	/*
	 * Finishing previous SETUP transfer by removing request from
	 * list and informing upper layer
	 */
	if (!list_empty(&pdev->eps[0].pending_list)) {
		struct cdnsp_request	*req;

		trace_cdnsp_ep0_request("Remove previous");
		req = next_request(&pdev->eps[0].pending_list);
		cdnsp_ep_dequeue(&pdev->eps[0], req);
	}

	len = le16_to_cpu(ctrl->wLength);
	if (!len) {
		pdev->three_stage_setup = false;
		pdev->ep0_expect_in = false;
	} else {
		pdev->three_stage_setup = true;
		pdev->ep0_expect_in = !!(ctrl->bRequestType & USB_DIR_IN);
	}

	if ((ctrl->bRequestType & USB_TYPE_MASK) == USB_TYPE_STANDARD)
		ret = cdnsp_ep0_std_request(pdev, ctrl);
	else
		ret = cdnsp_ep0_delegate_req(pdev, ctrl);

	if (ret == USB_GADGET_DELAYED_STATUS) {
		trace_cdnsp_ep0_status_stage("delayed");
		return;
	}
out:
	if (ret < 0)
		cdnsp_ep0_stall(pdev);
	else if (!len && pdev->ep0_stage != CDNSP_STATUS_STAGE)
		cdnsp_status_stage(pdev);
}
