// SPDX-License-Identifier: GPL-2.0
/*
 * Cadence USBSS DRD Driver - gadget side.
 *
 * Copyright (C) 2018 Cadence Design Systems.
 * Copyright (C) 2017-2018 NXP
 *
 * Authors: Pawel Jez <pjez@cadence.com>,
 *          Pawel Laszczak <pawell@cadence.com>
 *          Peter Chen <peter.chen@nxp.com>
 */

#include <linux/usb/composite.h>
#include <linux/iopoll.h>

#include "gadget.h"
#include "trace.h"

static struct usb_endpoint_descriptor cdns3_gadget_ep0_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bmAttributes =	USB_ENDPOINT_XFER_CONTROL,
};

/**
 * cdns3_ep0_run_transfer - Do transfer on default endpoint hardware
 * @priv_dev: extended gadget object
 * @dma_addr: physical address where data is/will be stored
 * @length: data length
 * @erdy: set it to 1 when ERDY packet should be sent -
 *        exit from flow control state
 */
static void cdns3_ep0_run_transfer(struct cdns3_device *priv_dev,
				   dma_addr_t dma_addr,
				   unsigned int length, int erdy, int zlp)
{
	struct cdns3_usb_regs __iomem *regs = priv_dev->regs;
	struct cdns3_endpoint *priv_ep = priv_dev->eps[0];

	priv_ep->trb_pool[0].buffer = TRB_BUFFER(dma_addr);
	priv_ep->trb_pool[0].length = TRB_LEN(length);

	if (zlp) {
		priv_ep->trb_pool[0].control = TRB_CYCLE | TRB_TYPE(TRB_NORMAL);
		priv_ep->trb_pool[1].buffer = TRB_BUFFER(dma_addr);
		priv_ep->trb_pool[1].length = TRB_LEN(0);
		priv_ep->trb_pool[1].control = TRB_CYCLE | TRB_IOC |
		    TRB_TYPE(TRB_NORMAL);
	} else {
		priv_ep->trb_pool[0].control = TRB_CYCLE | TRB_IOC |
		    TRB_TYPE(TRB_NORMAL);
		priv_ep->trb_pool[1].control = 0;
	}

	trace_cdns3_prepare_trb(priv_ep, priv_ep->trb_pool);

	cdns3_select_ep(priv_dev, priv_dev->ep0_data_dir);

	writel(EP_STS_TRBERR, &regs->ep_sts);
	writel(EP_TRADDR_TRADDR(priv_ep->trb_pool_dma), &regs->ep_traddr);
	trace_cdns3_doorbell_ep0(priv_dev->ep0_data_dir ? "ep0in" : "ep0out",
				 readl(&regs->ep_traddr));

	/* TRB should be prepared before starting transfer. */
	writel(EP_CMD_DRDY, &regs->ep_cmd);

	/* Resume controller before arming transfer. */
	__cdns3_gadget_wakeup(priv_dev);

	if (erdy)
		writel(EP_CMD_ERDY, &priv_dev->regs->ep_cmd);
}

/**
 * cdns3_ep0_delegate_req - Returns status of handling setup packet
 * Setup is handled by gadget driver
 * @priv_dev: extended gadget object
 * @ctrl_req: pointer to received setup packet
 *
 * Returns zero on success or negative value on failure
 */
static int cdns3_ep0_delegate_req(struct cdns3_device *priv_dev,
				  struct usb_ctrlrequest *ctrl_req)
{
	int ret;

	spin_unlock(&priv_dev->lock);
	priv_dev->setup_pending = 1;
	ret = priv_dev->gadget_driver->setup(&priv_dev->gadget, ctrl_req);
	priv_dev->setup_pending = 0;
	spin_lock(&priv_dev->lock);
	return ret;
}

static void cdns3_prepare_setup_packet(struct cdns3_device *priv_dev)
{
	priv_dev->ep0_data_dir = 0;
	priv_dev->ep0_stage = CDNS3_SETUP_STAGE;
	cdns3_ep0_run_transfer(priv_dev, priv_dev->setup_dma,
			       sizeof(struct usb_ctrlrequest), 0, 0);
}

static void cdns3_ep0_complete_setup(struct cdns3_device *priv_dev,
				     u8 send_stall, u8 send_erdy)
{
	struct cdns3_endpoint *priv_ep = priv_dev->eps[0];
	struct usb_request *request;

	request = cdns3_next_request(&priv_ep->pending_req_list);
	if (request)
		list_del_init(&request->list);

	if (send_stall) {
		trace_cdns3_halt(priv_ep, send_stall, 0);
		/* set_stall on ep0 */
		cdns3_select_ep(priv_dev, 0x00);
		writel(EP_CMD_SSTALL, &priv_dev->regs->ep_cmd);
	} else {
		cdns3_prepare_setup_packet(priv_dev);
	}

	priv_dev->ep0_stage = CDNS3_SETUP_STAGE;
	writel((send_erdy ? EP_CMD_ERDY : 0) | EP_CMD_REQ_CMPL,
	       &priv_dev->regs->ep_cmd);

	cdns3_allow_enable_l1(priv_dev, 1);
}

/**
 * cdns3_req_ep0_set_configuration - Handling of SET_CONFIG standard USB request
 * @priv_dev: extended gadget object
 * @ctrl_req: pointer to received setup packet
 *
 * Returns 0 if success, USB_GADGET_DELAYED_STATUS on deferred status stage,
 * error code on error
 */
static int cdns3_req_ep0_set_configuration(struct cdns3_device *priv_dev,
					   struct usb_ctrlrequest *ctrl_req)
{
	enum usb_device_state device_state = priv_dev->gadget.state;
	struct cdns3_endpoint *priv_ep;
	u32 config = le16_to_cpu(ctrl_req->wValue);
	int result = 0;
	int i;

	switch (device_state) {
	case USB_STATE_ADDRESS:
		/* Configure non-control EPs */
		for (i = 0; i < CDNS3_ENDPOINTS_MAX_COUNT; i++) {
			priv_ep = priv_dev->eps[i];
			if (!priv_ep)
				continue;

			if (priv_ep->flags & EP_CLAIMED)
				cdns3_ep_config(priv_ep);
		}

		result = cdns3_ep0_delegate_req(priv_dev, ctrl_req);

		if (result)
			return result;

		if (config) {
			cdns3_set_hw_configuration(priv_dev);
		} else {
			cdns3_hw_reset_eps_config(priv_dev);
			usb_gadget_set_state(&priv_dev->gadget,
					     USB_STATE_ADDRESS);
		}
		break;
	case USB_STATE_CONFIGURED:
		result = cdns3_ep0_delegate_req(priv_dev, ctrl_req);

		if (!config && !result) {
			cdns3_hw_reset_eps_config(priv_dev);
			usb_gadget_set_state(&priv_dev->gadget,
					     USB_STATE_ADDRESS);
		}
		break;
	default:
		result = -EINVAL;
	}

	return result;
}

/**
 * cdns3_req_ep0_set_address - Handling of SET_ADDRESS standard USB request
 * @priv_dev: extended gadget object
 * @ctrl_req: pointer to received setup packet
 *
 * Returns 0 if success, error code on error
 */
static int cdns3_req_ep0_set_address(struct cdns3_device *priv_dev,
				     struct usb_ctrlrequest *ctrl_req)
{
	enum usb_device_state device_state = priv_dev->gadget.state;
	u32 reg;
	u32 addr;

	addr = le16_to_cpu(ctrl_req->wValue);

	if (addr > USB_DEVICE_MAX_ADDRESS) {
		dev_err(priv_dev->dev,
			"Device address (%d) cannot be greater than %d\n",
			addr, USB_DEVICE_MAX_ADDRESS);
		return -EINVAL;
	}

	if (device_state == USB_STATE_CONFIGURED) {
		dev_err(priv_dev->dev,
			"can't set_address from configured state\n");
		return -EINVAL;
	}

	reg = readl(&priv_dev->regs->usb_cmd);

	writel(reg | USB_CMD_FADDR(addr) | USB_CMD_SET_ADDR,
	       &priv_dev->regs->usb_cmd);

	usb_gadget_set_state(&priv_dev->gadget,
			     (addr ? USB_STATE_ADDRESS : USB_STATE_DEFAULT));

	return 0;
}

/**
 * cdns3_req_ep0_get_status - Handling of GET_STATUS standard USB request
 * @priv_dev: extended gadget object
 * @ctrl_req: pointer to received setup packet
 *
 * Returns 0 if success, error code on error
 */
static int cdns3_req_ep0_get_status(struct cdns3_device *priv_dev,
				    struct usb_ctrlrequest *ctrl)
{
	struct cdns3_endpoint *priv_ep;
	__le16 *response_pkt;
	u16 usb_status = 0;
	u32 recip;
	u8 index;

	recip = ctrl->bRequestType & USB_RECIP_MASK;

	switch (recip) {
	case USB_RECIP_DEVICE:
		/* self powered */
		if (priv_dev->is_selfpowered)
			usb_status = BIT(USB_DEVICE_SELF_POWERED);

		if (priv_dev->wake_up_flag)
			usb_status |= BIT(USB_DEVICE_REMOTE_WAKEUP);

		if (priv_dev->gadget.speed != USB_SPEED_SUPER)
			break;

		if (priv_dev->u1_allowed)
			usb_status |= BIT(USB_DEV_STAT_U1_ENABLED);

		if (priv_dev->u2_allowed)
			usb_status |= BIT(USB_DEV_STAT_U2_ENABLED);

		break;
	case USB_RECIP_INTERFACE:
		return cdns3_ep0_delegate_req(priv_dev, ctrl);
	case USB_RECIP_ENDPOINT:
		index = cdns3_ep_addr_to_index(ctrl->wIndex);
		priv_ep = priv_dev->eps[index];

		/* check if endpoint is stalled or stall is pending */
		cdns3_select_ep(priv_dev, ctrl->wIndex);
		if (EP_STS_STALL(readl(&priv_dev->regs->ep_sts)) ||
		    (priv_ep->flags & EP_STALL_PENDING))
			usb_status =  BIT(USB_ENDPOINT_HALT);
		break;
	default:
		return -EINVAL;
	}

	response_pkt = (__le16 *)priv_dev->setup_buf;
	*response_pkt = cpu_to_le16(usb_status);

	cdns3_ep0_run_transfer(priv_dev, priv_dev->setup_dma,
			       sizeof(*response_pkt), 1, 0);
	return 0;
}

static int cdns3_ep0_feature_handle_device(struct cdns3_device *priv_dev,
					   struct usb_ctrlrequest *ctrl,
					   int set)
{
	enum usb_device_state state;
	enum usb_device_speed speed;
	int ret = 0;
	u32 wValue;
	u16 tmode;

	wValue = le16_to_cpu(ctrl->wValue);
	state = priv_dev->gadget.state;
	speed = priv_dev->gadget.speed;

	switch (wValue) {
	case USB_DEVICE_REMOTE_WAKEUP:
		priv_dev->wake_up_flag = !!set;
		break;
	case USB_DEVICE_U1_ENABLE:
		if (state != USB_STATE_CONFIGURED || speed != USB_SPEED_SUPER)
			return -EINVAL;

		priv_dev->u1_allowed = !!set;
		break;
	case USB_DEVICE_U2_ENABLE:
		if (state != USB_STATE_CONFIGURED || speed != USB_SPEED_SUPER)
			return -EINVAL;

		priv_dev->u2_allowed = !!set;
		break;
	case USB_DEVICE_LTM_ENABLE:
		ret = -EINVAL;
		break;
	case USB_DEVICE_TEST_MODE:
		if (state != USB_STATE_CONFIGURED || speed > USB_SPEED_HIGH)
			return -EINVAL;

		tmode = le16_to_cpu(ctrl->wIndex);

		if (!set || (tmode & 0xff) != 0)
			return -EINVAL;

		switch (tmode >> 8) {
		case TEST_J:
		case TEST_K:
		case TEST_SE0_NAK:
		case TEST_PACKET:
			cdns3_set_register_bit(&priv_dev->regs->usb_cmd,
					       USB_CMD_STMODE |
					       USB_STS_TMODE_SEL(tmode - 1));
			break;
		default:
			ret = -EINVAL;
		}
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int cdns3_ep0_feature_handle_intf(struct cdns3_device *priv_dev,
					 struct usb_ctrlrequest *ctrl,
					 int set)
{
	u32 wValue;
	int ret = 0;

	wValue = le16_to_cpu(ctrl->wValue);

	switch (wValue) {
	case USB_INTRF_FUNC_SUSPEND:
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int cdns3_ep0_feature_handle_endpoint(struct cdns3_device *priv_dev,
					     struct usb_ctrlrequest *ctrl,
					     int set)
{
	struct cdns3_endpoint *priv_ep;
	int ret = 0;
	u8 index;

	if (le16_to_cpu(ctrl->wValue) != USB_ENDPOINT_HALT)
		return -EINVAL;

	if (!(ctrl->wIndex & ~USB_DIR_IN))
		return 0;

	index = cdns3_ep_addr_to_index(ctrl->wIndex);
	priv_ep = priv_dev->eps[index];

	cdns3_select_ep(priv_dev, ctrl->wIndex);

	if (set)
		__cdns3_gadget_ep_set_halt(priv_ep);
	else if (!(priv_ep->flags & EP_WEDGE))
		ret = __cdns3_gadget_ep_clear_halt(priv_ep);

	cdns3_select_ep(priv_dev, 0x00);

	return ret;
}

/**
 * cdns3_req_ep0_handle_feature -
 * Handling of GET/SET_FEATURE standard USB request
 *
 * @priv_dev: extended gadget object
 * @ctrl_req: pointer to received setup packet
 * @set: must be set to 1 for SET_FEATURE request
 *
 * Returns 0 if success, error code on error
 */
static int cdns3_req_ep0_handle_feature(struct cdns3_device *priv_dev,
					struct usb_ctrlrequest *ctrl,
					int set)
{
	int ret = 0;
	u32 recip;

	recip = ctrl->bRequestType & USB_RECIP_MASK;

	switch (recip) {
	case USB_RECIP_DEVICE:
		ret = cdns3_ep0_feature_handle_device(priv_dev, ctrl, set);
		break;
	case USB_RECIP_INTERFACE:
		ret = cdns3_ep0_feature_handle_intf(priv_dev, ctrl, set);
		break;
	case USB_RECIP_ENDPOINT:
		ret = cdns3_ep0_feature_handle_endpoint(priv_dev, ctrl, set);
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

/**
 * cdns3_req_ep0_set_sel - Handling of SET_SEL standard USB request
 * @priv_dev: extended gadget object
 * @ctrl_req: pointer to received setup packet
 *
 * Returns 0 if success, error code on error
 */
static int cdns3_req_ep0_set_sel(struct cdns3_device *priv_dev,
				 struct usb_ctrlrequest *ctrl_req)
{
	if (priv_dev->gadget.state < USB_STATE_ADDRESS)
		return -EINVAL;

	if (ctrl_req->wLength != 6) {
		dev_err(priv_dev->dev, "Set SEL should be 6 bytes, got %d\n",
			ctrl_req->wLength);
		return -EINVAL;
	}

	cdns3_ep0_run_transfer(priv_dev, priv_dev->setup_dma, 6, 1, 0);
	return 0;
}

/**
 * cdns3_req_ep0_set_isoch_delay -
 * Handling of GET_ISOCH_DELAY standard USB request
 * @priv_dev: extended gadget object
 * @ctrl_req: pointer to received setup packet
 *
 * Returns 0 if success, error code on error
 */
static int cdns3_req_ep0_set_isoch_delay(struct cdns3_device *priv_dev,
					 struct usb_ctrlrequest *ctrl_req)
{
	if (ctrl_req->wIndex || ctrl_req->wLength)
		return -EINVAL;

	priv_dev->isoch_delay = ctrl_req->wValue;

	return 0;
}

/**
 * cdns3_ep0_standard_request - Handling standard USB requests
 * @priv_dev: extended gadget object
 * @ctrl_req: pointer to received setup packet
 *
 * Returns 0 if success, error code on error
 */
static int cdns3_ep0_standard_request(struct cdns3_device *priv_dev,
				      struct usb_ctrlrequest *ctrl_req)
{
	int ret;

	switch (ctrl_req->bRequest) {
	case USB_REQ_SET_ADDRESS:
		ret = cdns3_req_ep0_set_address(priv_dev, ctrl_req);
		break;
	case USB_REQ_SET_CONFIGURATION:
		ret = cdns3_req_ep0_set_configuration(priv_dev, ctrl_req);
		break;
	case USB_REQ_GET_STATUS:
		ret = cdns3_req_ep0_get_status(priv_dev, ctrl_req);
		break;
	case USB_REQ_CLEAR_FEATURE:
		ret = cdns3_req_ep0_handle_feature(priv_dev, ctrl_req, 0);
		break;
	case USB_REQ_SET_FEATURE:
		ret = cdns3_req_ep0_handle_feature(priv_dev, ctrl_req, 1);
		break;
	case USB_REQ_SET_SEL:
		ret = cdns3_req_ep0_set_sel(priv_dev, ctrl_req);
		break;
	case USB_REQ_SET_ISOCH_DELAY:
		ret = cdns3_req_ep0_set_isoch_delay(priv_dev, ctrl_req);
		break;
	default:
		ret = cdns3_ep0_delegate_req(priv_dev, ctrl_req);
		break;
	}

	return ret;
}

static void __pending_setup_status_handler(struct cdns3_device *priv_dev)
{
	struct usb_request *request = priv_dev->pending_status_request;

	if (priv_dev->status_completion_no_call && request &&
	    request->complete) {
		request->complete(&priv_dev->eps[0]->endpoint, request);
		priv_dev->status_completion_no_call = 0;
	}
}

void cdns3_pending_setup_status_handler(struct work_struct *work)
{
	struct cdns3_device *priv_dev = container_of(work, struct cdns3_device,
			pending_status_wq);
	unsigned long flags;

	spin_lock_irqsave(&priv_dev->lock, flags);
	__pending_setup_status_handler(priv_dev);
	spin_unlock_irqrestore(&priv_dev->lock, flags);
}

/**
 * cdns3_ep0_setup_phase - Handling setup USB requests
 * @priv_dev: extended gadget object
 */
static void cdns3_ep0_setup_phase(struct cdns3_device *priv_dev)
{
	struct usb_ctrlrequest *ctrl = priv_dev->setup_buf;
	struct cdns3_endpoint *priv_ep = priv_dev->eps[0];
	int result;

	priv_dev->ep0_data_dir = ctrl->bRequestType & USB_DIR_IN;

	trace_cdns3_ctrl_req(ctrl);

	if (!list_empty(&priv_ep->pending_req_list)) {
		struct usb_request *request;

		request = cdns3_next_request(&priv_ep->pending_req_list);
		priv_ep->dir = priv_dev->ep0_data_dir;
		cdns3_gadget_giveback(priv_ep, to_cdns3_request(request),
				      -ECONNRESET);
	}

	if (le16_to_cpu(ctrl->wLength))
		priv_dev->ep0_stage = CDNS3_DATA_STAGE;
	else
		priv_dev->ep0_stage = CDNS3_STATUS_STAGE;

	if ((ctrl->bRequestType & USB_TYPE_MASK) == USB_TYPE_STANDARD)
		result = cdns3_ep0_standard_request(priv_dev, ctrl);
	else
		result = cdns3_ep0_delegate_req(priv_dev, ctrl);

	if (result == USB_GADGET_DELAYED_STATUS)
		return;

	if (result < 0)
		cdns3_ep0_complete_setup(priv_dev, 1, 1);
	else if (priv_dev->ep0_stage == CDNS3_STATUS_STAGE)
		cdns3_ep0_complete_setup(priv_dev, 0, 1);
}

static void cdns3_transfer_completed(struct cdns3_device *priv_dev)
{
	struct cdns3_endpoint *priv_ep = priv_dev->eps[0];

	if (!list_empty(&priv_ep->pending_req_list)) {
		struct usb_request *request;

		trace_cdns3_complete_trb(priv_ep, priv_ep->trb_pool);
		request = cdns3_next_request(&priv_ep->pending_req_list);

		request->actual =
			TRB_LEN(le32_to_cpu(priv_ep->trb_pool->length));

		priv_ep->dir = priv_dev->ep0_data_dir;
		cdns3_gadget_giveback(priv_ep, to_cdns3_request(request), 0);
	}

	cdns3_ep0_complete_setup(priv_dev, 0, 0);
}

/**
 * cdns3_check_new_setup - Check if controller receive new SETUP packet.
 * @priv_dev: extended gadget object
 *
 * The SETUP packet can be kept in on-chip memory or in system memory.
 */
static bool cdns3_check_new_setup(struct cdns3_device *priv_dev)
{
	u32 ep_sts_reg;

	cdns3_select_ep(priv_dev, 0 | USB_DIR_OUT);
	ep_sts_reg = readl(&priv_dev->regs->ep_sts);

	return !!(ep_sts_reg & (EP_STS_SETUP | EP_STS_STPWAIT));
}

/**
 * cdns3_check_ep0_interrupt_proceed - Processes interrupt related to endpoint 0
 * @priv_dev: extended gadget object
 * @dir: USB_DIR_IN for IN direction, USB_DIR_OUT for OUT direction
 */
void cdns3_check_ep0_interrupt_proceed(struct cdns3_device *priv_dev, int dir)
{
	u32 ep_sts_reg;

	cdns3_select_ep(priv_dev, dir);

	ep_sts_reg = readl(&priv_dev->regs->ep_sts);
	writel(ep_sts_reg, &priv_dev->regs->ep_sts);

	trace_cdns3_ep0_irq(priv_dev, ep_sts_reg);

	__pending_setup_status_handler(priv_dev);

	if (ep_sts_reg & EP_STS_SETUP)
		priv_dev->wait_for_setup = 1;

	if (priv_dev->wait_for_setup && ep_sts_reg & EP_STS_IOC) {
		priv_dev->wait_for_setup = 0;
		cdns3_allow_enable_l1(priv_dev, 0);
		cdns3_ep0_setup_phase(priv_dev);
	} else if ((ep_sts_reg & EP_STS_IOC) || (ep_sts_reg & EP_STS_ISP)) {
		priv_dev->ep0_data_dir = dir;
		cdns3_transfer_completed(priv_dev);
	}

	if (ep_sts_reg & EP_STS_DESCMIS) {
		if (dir == 0 && !priv_dev->setup_pending)
			cdns3_prepare_setup_packet(priv_dev);
	}
}

/**
 * cdns3_gadget_ep0_enable
 * Function shouldn't be called by gadget driver,
 * endpoint 0 is allways active
 */
static int cdns3_gadget_ep0_enable(struct usb_ep *ep,
				   const struct usb_endpoint_descriptor *desc)
{
	return -EINVAL;
}

/**
 * cdns3_gadget_ep0_disable
 * Function shouldn't be called by gadget driver,
 * endpoint 0 is allways active
 */
static int cdns3_gadget_ep0_disable(struct usb_ep *ep)
{
	return -EINVAL;
}

/**
 * cdns3_gadget_ep0_set_halt
 * @ep: pointer to endpoint zero object
 * @value: 1 for set stall, 0 for clear stall
 *
 * Returns 0
 */
static int cdns3_gadget_ep0_set_halt(struct usb_ep *ep, int value)
{
	/* TODO */
	return 0;
}

/**
 * cdns3_gadget_ep0_queue Transfer data on endpoint zero
 * @ep: pointer to endpoint zero object
 * @request: pointer to request object
 * @gfp_flags: gfp flags
 *
 * Returns 0 on success, error code elsewhere
 */
static int cdns3_gadget_ep0_queue(struct usb_ep *ep,
				  struct usb_request *request,
				  gfp_t gfp_flags)
{
	struct cdns3_endpoint *priv_ep = ep_to_cdns3_ep(ep);
	struct cdns3_device *priv_dev = priv_ep->cdns3_dev;
	unsigned long flags;
	int erdy_sent = 0;
	int ret = 0;
	u8 zlp = 0;

	trace_cdns3_ep0_queue(priv_dev, request);

	/* cancel the request if controller receive new SETUP packet. */
	if (cdns3_check_new_setup(priv_dev))
		return -ECONNRESET;

	/* send STATUS stage. Should be called only for SET_CONFIGURATION */
	if (priv_dev->ep0_stage == CDNS3_STATUS_STAGE) {
		spin_lock_irqsave(&priv_dev->lock, flags);
		cdns3_select_ep(priv_dev, 0x00);

		erdy_sent = !priv_dev->hw_configured_flag;
		cdns3_set_hw_configuration(priv_dev);

		if (!erdy_sent)
			cdns3_ep0_complete_setup(priv_dev, 0, 1);

		cdns3_allow_enable_l1(priv_dev, 1);

		request->actual = 0;
		priv_dev->status_completion_no_call = true;
		priv_dev->pending_status_request = request;
		spin_unlock_irqrestore(&priv_dev->lock, flags);

		/*
		 * Since there is no completion interrupt for status stage,
		 * it needs to call ->completion in software after
		 * ep0_queue is back.
		 */
		queue_work(system_freezable_wq, &priv_dev->pending_status_wq);
		return 0;
	}

	spin_lock_irqsave(&priv_dev->lock, flags);
	if (!list_empty(&priv_ep->pending_req_list)) {
		dev_err(priv_dev->dev,
			"can't handle multiple requests for ep0\n");
		spin_unlock_irqrestore(&priv_dev->lock, flags);
		return -EBUSY;
	}

	ret = usb_gadget_map_request_by_dev(priv_dev->sysdev, request,
					    priv_dev->ep0_data_dir);
	if (ret) {
		spin_unlock_irqrestore(&priv_dev->lock, flags);
		dev_err(priv_dev->dev, "failed to map request\n");
		return -EINVAL;
	}

	request->status = -EINPROGRESS;
	list_add_tail(&request->list, &priv_ep->pending_req_list);

	if (request->zero && request->length &&
	    (request->length % ep->maxpacket == 0))
		zlp = 1;

	cdns3_ep0_run_transfer(priv_dev, request->dma, request->length, 1, zlp);

	spin_unlock_irqrestore(&priv_dev->lock, flags);

	return ret;
}

/**
 * cdns3_gadget_ep_set_wedge Set wedge on selected endpoint
 * @ep: endpoint object
 *
 * Returns 0
 */
int cdns3_gadget_ep_set_wedge(struct usb_ep *ep)
{
	struct cdns3_endpoint *priv_ep = ep_to_cdns3_ep(ep);
	struct cdns3_device *priv_dev = priv_ep->cdns3_dev;

	dev_dbg(priv_dev->dev, "Wedge for %s\n", ep->name);
	cdns3_gadget_ep_set_halt(ep, 1);
	priv_ep->flags |= EP_WEDGE;

	return 0;
}

const struct usb_ep_ops cdns3_gadget_ep0_ops = {
	.enable = cdns3_gadget_ep0_enable,
	.disable = cdns3_gadget_ep0_disable,
	.alloc_request = cdns3_gadget_ep_alloc_request,
	.free_request = cdns3_gadget_ep_free_request,
	.queue = cdns3_gadget_ep0_queue,
	.dequeue = cdns3_gadget_ep_dequeue,
	.set_halt = cdns3_gadget_ep0_set_halt,
	.set_wedge = cdns3_gadget_ep_set_wedge,
};

/**
 * cdns3_ep0_config - Configures default endpoint
 * @priv_dev: extended gadget object
 *
 * Functions sets parameters: maximal packet size and enables interrupts
 */
void cdns3_ep0_config(struct cdns3_device *priv_dev)
{
	struct cdns3_usb_regs __iomem *regs;
	struct cdns3_endpoint *priv_ep;
	u32 max_packet_size = 64;

	regs = priv_dev->regs;

	if (priv_dev->gadget.speed == USB_SPEED_SUPER)
		max_packet_size = 512;

	priv_ep = priv_dev->eps[0];

	if (!list_empty(&priv_ep->pending_req_list)) {
		struct usb_request *request;

		request = cdns3_next_request(&priv_ep->pending_req_list);
		list_del_init(&request->list);
	}

	priv_dev->u1_allowed = 0;
	priv_dev->u2_allowed = 0;

	priv_dev->gadget.ep0->maxpacket = max_packet_size;
	cdns3_gadget_ep0_desc.wMaxPacketSize = cpu_to_le16(max_packet_size);

	/* init ep out */
	cdns3_select_ep(priv_dev, USB_DIR_OUT);

	if (priv_dev->dev_ver >= DEV_VER_V3) {
		cdns3_set_register_bit(&priv_dev->regs->dtrans,
				       BIT(0) | BIT(16));
		cdns3_set_register_bit(&priv_dev->regs->tdl_from_trb,
				       BIT(0) | BIT(16));
	}

	writel(EP_CFG_ENABLE | EP_CFG_MAXPKTSIZE(max_packet_size),
	       &regs->ep_cfg);

	writel(EP_STS_EN_SETUPEN | EP_STS_EN_DESCMISEN | EP_STS_EN_TRBERREN,
	       &regs->ep_sts_en);

	/* init ep in */
	cdns3_select_ep(priv_dev, USB_DIR_IN);

	writel(EP_CFG_ENABLE | EP_CFG_MAXPKTSIZE(max_packet_size),
	       &regs->ep_cfg);

	writel(EP_STS_EN_SETUPEN | EP_STS_EN_TRBERREN, &regs->ep_sts_en);

	cdns3_set_register_bit(&regs->usb_conf, USB_CONF_U1DS | USB_CONF_U2DS);
}

/**
 * cdns3_init_ep0 Initializes software endpoint 0 of gadget
 * @priv_dev: extended gadget object
 * @ep_priv: extended endpoint object
 *
 * Returns 0 on success else error code.
 */
int cdns3_init_ep0(struct cdns3_device *priv_dev,
		   struct cdns3_endpoint *priv_ep)
{
	sprintf(priv_ep->name, "ep0");

	/* fill linux fields */
	priv_ep->endpoint.ops = &cdns3_gadget_ep0_ops;
	priv_ep->endpoint.maxburst = 1;
	usb_ep_set_maxpacket_limit(&priv_ep->endpoint,
				   CDNS3_EP0_MAX_PACKET_LIMIT);
	priv_ep->endpoint.address = 0;
	priv_ep->endpoint.caps.type_control = 1;
	priv_ep->endpoint.caps.dir_in = 1;
	priv_ep->endpoint.caps.dir_out = 1;
	priv_ep->endpoint.name = priv_ep->name;
	priv_ep->endpoint.desc = &cdns3_gadget_ep0_desc;
	priv_dev->gadget.ep0 = &priv_ep->endpoint;
	priv_ep->type = USB_ENDPOINT_XFER_CONTROL;

	return cdns3_allocate_trb_pool(priv_ep);
}
