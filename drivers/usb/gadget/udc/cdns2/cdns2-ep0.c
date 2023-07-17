// SPDX-License-Identifier: GPL-2.0
/*
 * Cadence USBHS-DEV driver.
 *
 * Copyright (C) 2023 Cadence Design Systems.
 *
 * Authors: Pawel Laszczak <pawell@cadence.com>
 */

#include <linux/usb/composite.h>
#include <asm/unaligned.h>

#include "cdns2-gadget.h"
#include "cdns2-trace.h"

static struct usb_endpoint_descriptor cdns2_gadget_ep0_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bmAttributes =	 USB_ENDPOINT_XFER_CONTROL,
	.wMaxPacketSize = cpu_to_le16(64)
};

static int cdns2_w_index_to_ep_index(u16 wIndex)
{
	if (!(wIndex & USB_ENDPOINT_NUMBER_MASK))
		return 0;

	return ((wIndex & USB_ENDPOINT_NUMBER_MASK) * 2) +
		(wIndex & USB_ENDPOINT_DIR_MASK ? 1 : 0) - 1;
}

static bool cdns2_check_new_setup(struct cdns2_device *pdev)
{
	u8 reg;

	reg = readb(&pdev->ep0_regs->cs);

	return !!(reg & EP0CS_CHGSET);
}

static void cdns2_ep0_enqueue(struct cdns2_device *pdev, dma_addr_t dma_addr,
			      unsigned int length, int zlp)
{
	struct cdns2_adma_regs __iomem *regs = pdev->adma_regs;
	struct cdns2_endpoint *pep = &pdev->eps[0];
	struct cdns2_ring *ring = &pep->ring;

	ring->trbs[0].buffer = cpu_to_le32(TRB_BUFFER(dma_addr));
	ring->trbs[0].length = cpu_to_le32(TRB_LEN(length));

	if (zlp) {
		ring->trbs[0].control = cpu_to_le32(TRB_CYCLE |
						    TRB_TYPE(TRB_NORMAL));
		ring->trbs[1].buffer = cpu_to_le32(TRB_BUFFER(dma_addr));
		ring->trbs[1].length = cpu_to_le32(TRB_LEN(0));
		ring->trbs[1].control = cpu_to_le32(TRB_CYCLE | TRB_IOC |
					TRB_TYPE(TRB_NORMAL));
	} else {
		ring->trbs[0].control = cpu_to_le32(TRB_CYCLE | TRB_IOC |
					TRB_TYPE(TRB_NORMAL));
		ring->trbs[1].control = 0;
	}

	trace_cdns2_queue_trb(pep, ring->trbs);

	if (!pep->dir)
		writel(0, &pdev->ep0_regs->rxbc);

	cdns2_select_ep(pdev, pep->dir);

	writel(DMA_EP_STS_TRBERR, &regs->ep_sts);
	writel(pep->ring.dma, &regs->ep_traddr);

	trace_cdns2_doorbell_ep0(pep, readl(&regs->ep_traddr));

	writel(DMA_EP_CMD_DRDY, &regs->ep_cmd);
}

static int cdns2_ep0_delegate_req(struct cdns2_device *pdev)
{
	int ret;

	spin_unlock(&pdev->lock);
	ret = pdev->gadget_driver->setup(&pdev->gadget, &pdev->setup);
	spin_lock(&pdev->lock);

	return ret;
}

static void cdns2_ep0_stall(struct cdns2_device *pdev)
{
	struct cdns2_endpoint *pep = &pdev->eps[0];
	struct cdns2_request *preq;

	preq = cdns2_next_preq(&pep->pending_list);
	set_reg_bit_8(&pdev->ep0_regs->cs, EP0CS_DSTALL);

	if (pdev->ep0_stage == CDNS2_DATA_STAGE && preq)
		cdns2_gadget_giveback(pep, preq, -ECONNRESET);
	else if (preq)
		list_del_init(&preq->list);

	pdev->ep0_stage = CDNS2_SETUP_STAGE;
	pep->ep_state |= EP_STALLED;
}

static void cdns2_status_stage(struct cdns2_device *pdev)
{
	struct cdns2_endpoint *pep = &pdev->eps[0];
	struct cdns2_request *preq;

	preq = cdns2_next_preq(&pep->pending_list);
	if (preq)
		list_del_init(&preq->list);

	pdev->ep0_stage = CDNS2_SETUP_STAGE;
	writeb(EP0CS_HSNAK, &pdev->ep0_regs->cs);
}

static int cdns2_req_ep0_set_configuration(struct cdns2_device *pdev,
					   struct usb_ctrlrequest *ctrl_req)
{
	enum usb_device_state state = pdev->gadget.state;
	u32 config = le16_to_cpu(ctrl_req->wValue);
	int ret;

	if (state < USB_STATE_ADDRESS) {
		dev_err(pdev->dev, "Set Configuration - bad device state\n");
		return -EINVAL;
	}

	ret = cdns2_ep0_delegate_req(pdev);
	if (ret)
		return ret;

	trace_cdns2_device_state(config ? "configured" : "addressed");

	if (!config)
		usb_gadget_set_state(&pdev->gadget, USB_STATE_ADDRESS);

	return 0;
}

static int cdns2_req_ep0_set_address(struct cdns2_device *pdev, u32 addr)
{
	enum usb_device_state device_state = pdev->gadget.state;
	u8 reg;

	if (addr > USB_DEVICE_MAX_ADDRESS) {
		dev_err(pdev->dev,
			"Device address (%d) cannot be greater than %d\n",
			addr, USB_DEVICE_MAX_ADDRESS);
		return -EINVAL;
	}

	if (device_state == USB_STATE_CONFIGURED) {
		dev_err(pdev->dev,
			"can't set_address from configured state\n");
		return -EINVAL;
	}

	reg = readb(&pdev->usb_regs->fnaddr);
	pdev->dev_address = reg;

	usb_gadget_set_state(&pdev->gadget,
			     (addr ? USB_STATE_ADDRESS : USB_STATE_DEFAULT));

	trace_cdns2_device_state(addr ? "addressed" : "default");

	return 0;
}

static int cdns2_req_ep0_handle_status(struct cdns2_device *pdev,
				       struct usb_ctrlrequest *ctrl)
{
	struct cdns2_endpoint *pep;
	__le16 *response_pkt;
	u16 status = 0;
	int ep_sts;
	u32 recip;

	recip = ctrl->bRequestType & USB_RECIP_MASK;

	switch (recip) {
	case USB_RECIP_DEVICE:
		status = pdev->gadget.is_selfpowered;
		status |= pdev->may_wakeup << USB_DEVICE_REMOTE_WAKEUP;
		break;
	case USB_RECIP_INTERFACE:
		return cdns2_ep0_delegate_req(pdev);
	case USB_RECIP_ENDPOINT:
		ep_sts = cdns2_w_index_to_ep_index(le16_to_cpu(ctrl->wIndex));
		pep = &pdev->eps[ep_sts];

		if (pep->ep_state & EP_STALLED)
			status =  BIT(USB_ENDPOINT_HALT);
		break;
	default:
		return -EINVAL;
	}

	put_unaligned_le16(status, (__le16 *)pdev->ep0_preq.request.buf);

	cdns2_ep0_enqueue(pdev, pdev->ep0_preq.request.dma,
			  sizeof(*response_pkt), 0);

	return 0;
}

static int cdns2_ep0_handle_feature_device(struct cdns2_device *pdev,
					   struct usb_ctrlrequest *ctrl,
					   int set)
{
	enum usb_device_state state;
	enum usb_device_speed speed;
	int ret = 0;
	u32 wValue;
	u16 tmode;

	wValue = le16_to_cpu(ctrl->wValue);
	state = pdev->gadget.state;
	speed = pdev->gadget.speed;

	switch (wValue) {
	case USB_DEVICE_REMOTE_WAKEUP:
		pdev->may_wakeup = !!set;
		break;
	case USB_DEVICE_TEST_MODE:
		if (state != USB_STATE_CONFIGURED || speed > USB_SPEED_HIGH)
			return -EINVAL;

		tmode = le16_to_cpu(ctrl->wIndex);

		if (!set || (tmode & 0xff) != 0)
			return -EINVAL;

		tmode >>= 8;
		switch (tmode) {
		case USB_TEST_J:
		case USB_TEST_K:
		case USB_TEST_SE0_NAK:
		case USB_TEST_PACKET:
			/*
			 * The USBHS controller automatically handles the
			 * Set_Feature(testmode) request. Standard test modes
			 * that use values of test mode selector from
			 * 01h to 04h (Test_J, Test_K, Test_SE0_NAK,
			 * Test_Packet) are supported by the
			 * controller(HS - ack, FS - stall).
			 */
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

static int cdns2_ep0_handle_feature_intf(struct cdns2_device *pdev,
					 struct usb_ctrlrequest *ctrl,
					 int set)
{
	int ret = 0;
	u32 wValue;

	wValue = le16_to_cpu(ctrl->wValue);

	switch (wValue) {
	case USB_INTRF_FUNC_SUSPEND:
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int cdns2_ep0_handle_feature_endpoint(struct cdns2_device *pdev,
					     struct usb_ctrlrequest *ctrl,
					     int set)
{
	struct cdns2_endpoint *pep;
	u8 wValue;

	wValue = le16_to_cpu(ctrl->wValue);
	pep = &pdev->eps[cdns2_w_index_to_ep_index(le16_to_cpu(ctrl->wIndex))];

	if (wValue != USB_ENDPOINT_HALT)
		return -EINVAL;

	if (!(le16_to_cpu(ctrl->wIndex) & ~USB_DIR_IN))
		return 0;

	switch (wValue) {
	case USB_ENDPOINT_HALT:
		if (set || !(pep->ep_state & EP_WEDGE))
			return cdns2_halt_endpoint(pdev, pep, set);
		break;
	default:
		dev_warn(pdev->dev, "WARN Incorrect wValue %04x\n", wValue);
		return -EINVAL;
	}

	return 0;
}

static int cdns2_req_ep0_handle_feature(struct cdns2_device *pdev,
					struct usb_ctrlrequest *ctrl,
					int set)
{
	switch (ctrl->bRequestType & USB_RECIP_MASK) {
	case USB_RECIP_DEVICE:
		return cdns2_ep0_handle_feature_device(pdev, ctrl, set);
	case USB_RECIP_INTERFACE:
		return cdns2_ep0_handle_feature_intf(pdev, ctrl, set);
	case USB_RECIP_ENDPOINT:
		return cdns2_ep0_handle_feature_endpoint(pdev, ctrl, set);
	default:
		return -EINVAL;
	}
}

static int cdns2_ep0_std_request(struct cdns2_device *pdev)
{
	struct usb_ctrlrequest *ctrl = &pdev->setup;
	int ret;

	switch (ctrl->bRequest) {
	case USB_REQ_SET_ADDRESS:
		ret = cdns2_req_ep0_set_address(pdev,
						le16_to_cpu(ctrl->wValue));
		break;
	case USB_REQ_SET_CONFIGURATION:
		ret = cdns2_req_ep0_set_configuration(pdev, ctrl);
		break;
	case USB_REQ_GET_STATUS:
		ret = cdns2_req_ep0_handle_status(pdev, ctrl);
		break;
	case USB_REQ_CLEAR_FEATURE:
		ret = cdns2_req_ep0_handle_feature(pdev, ctrl, 0);
		break;
	case USB_REQ_SET_FEATURE:
		ret = cdns2_req_ep0_handle_feature(pdev, ctrl, 1);
		break;
	default:
		ret = cdns2_ep0_delegate_req(pdev);
		break;
	}

	return ret;
}

static void __pending_setup_status_handler(struct cdns2_device *pdev)
{
	struct usb_request *request = pdev->pending_status_request;

	if (pdev->status_completion_no_call && request && request->complete) {
		request->complete(&pdev->eps[0].endpoint, request);
		pdev->status_completion_no_call = 0;
	}
}

void cdns2_pending_setup_status_handler(struct work_struct *work)
{
	struct cdns2_device *pdev = container_of(work, struct cdns2_device,
						 pending_status_wq);
	unsigned long flags;

	spin_lock_irqsave(&pdev->lock, flags);
	__pending_setup_status_handler(pdev);
	spin_unlock_irqrestore(&pdev->lock, flags);
}

void cdns2_handle_setup_packet(struct cdns2_device *pdev)
{
	struct usb_ctrlrequest *ctrl = &pdev->setup;
	struct cdns2_endpoint *pep = &pdev->eps[0];
	struct cdns2_request *preq;
	int ret = 0;
	u16 len;
	u8 reg;
	int i;

	writeb(EP0CS_CHGSET, &pdev->ep0_regs->cs);

	for (i = 0; i < 8; i++)
		((u8 *)&pdev->setup)[i] = readb(&pdev->ep0_regs->setupdat[i]);

	/*
	 * If SETUP packet was modified while reading just simple ignore it.
	 * The new one will be handled latter.
	 */
	if (cdns2_check_new_setup(pdev)) {
		trace_cdns2_ep0_setup("overridden");
		return;
	}

	trace_cdns2_ctrl_req(ctrl);

	if (!pdev->gadget_driver)
		goto out;

	if (pdev->gadget.state == USB_STATE_NOTATTACHED) {
		dev_err(pdev->dev, "ERR: Setup detected in unattached state\n");
		ret = -EINVAL;
		goto out;
	}

	pep = &pdev->eps[0];

	/* Halt for Ep0 is cleared automatically when SETUP packet arrives. */
	pep->ep_state &= ~EP_STALLED;

	if (!list_empty(&pep->pending_list)) {
		preq = cdns2_next_preq(&pep->pending_list);
		cdns2_gadget_giveback(pep, preq, -ECONNRESET);
	}

	len = le16_to_cpu(ctrl->wLength);
	if (len)
		pdev->ep0_stage = CDNS2_DATA_STAGE;
	else
		pdev->ep0_stage = CDNS2_STATUS_STAGE;

	pep->dir = ctrl->bRequestType & USB_DIR_IN;

	/*
	 * SET_ADDRESS request is acknowledged automatically by controller and
	 * in the worse case driver may not notice this request. To check
	 * whether this request has been processed driver can use
	 * fnaddr register.
	 */
	reg = readb(&pdev->usb_regs->fnaddr);
	if (pdev->setup.bRequest != USB_REQ_SET_ADDRESS &&
	    pdev->dev_address != reg)
		cdns2_req_ep0_set_address(pdev, reg);

	if ((ctrl->bRequestType & USB_TYPE_MASK) == USB_TYPE_STANDARD)
		ret = cdns2_ep0_std_request(pdev);
	else
		ret = cdns2_ep0_delegate_req(pdev);

	if (ret == USB_GADGET_DELAYED_STATUS) {
		trace_cdns2_ep0_status_stage("delayed");
		return;
	}

out:
	if (ret < 0)
		cdns2_ep0_stall(pdev);
	else if (pdev->ep0_stage == CDNS2_STATUS_STAGE)
		cdns2_status_stage(pdev);
}

static void cdns2_transfer_completed(struct cdns2_device *pdev)
{
	struct cdns2_endpoint *pep = &pdev->eps[0];

	if (!list_empty(&pep->pending_list)) {
		struct cdns2_request *preq;

		trace_cdns2_complete_trb(pep, pep->ring.trbs);
		preq = cdns2_next_preq(&pep->pending_list);

		preq->request.actual =
			TRB_LEN(le32_to_cpu(pep->ring.trbs->length));
		cdns2_gadget_giveback(pep, preq, 0);
	}

	cdns2_status_stage(pdev);
}

void cdns2_handle_ep0_interrupt(struct cdns2_device *pdev, int dir)
{
	u32 ep_sts_reg;

	cdns2_select_ep(pdev, dir);

	trace_cdns2_ep0_irq(pdev);

	ep_sts_reg = readl(&pdev->adma_regs->ep_sts);
	writel(ep_sts_reg, &pdev->adma_regs->ep_sts);

	__pending_setup_status_handler(pdev);

	if ((ep_sts_reg & DMA_EP_STS_IOC) || (ep_sts_reg & DMA_EP_STS_ISP)) {
		pdev->eps[0].dir = dir;
		cdns2_transfer_completed(pdev);
	}
}

/*
 * Function shouldn't be called by gadget driver,
 * endpoint 0 is allways active.
 */
static int cdns2_gadget_ep0_enable(struct usb_ep *ep,
				   const struct usb_endpoint_descriptor *desc)
{
	return -EINVAL;
}

/*
 * Function shouldn't be called by gadget driver,
 * endpoint 0 is allways active.
 */
static int cdns2_gadget_ep0_disable(struct usb_ep *ep)
{
	return -EINVAL;
}

static int cdns2_gadget_ep0_set_halt(struct usb_ep *ep, int value)
{
	struct cdns2_endpoint *pep = ep_to_cdns2_ep(ep);
	struct cdns2_device *pdev = pep->pdev;
	unsigned long flags;

	if (!value)
		return 0;

	spin_lock_irqsave(&pdev->lock, flags);
	cdns2_ep0_stall(pdev);
	spin_unlock_irqrestore(&pdev->lock, flags);

	return 0;
}

static int cdns2_gadget_ep0_set_wedge(struct usb_ep *ep)
{
	return cdns2_gadget_ep0_set_halt(ep, 1);
}

static int cdns2_gadget_ep0_queue(struct usb_ep *ep,
				  struct usb_request *request,
				  gfp_t gfp_flags)
{
	struct cdns2_endpoint *pep = ep_to_cdns2_ep(ep);
	struct cdns2_device *pdev = pep->pdev;
	struct cdns2_request *preq;
	unsigned long flags;
	u8 zlp = 0;
	int ret;

	spin_lock_irqsave(&pdev->lock, flags);

	preq = to_cdns2_request(request);

	trace_cdns2_request_enqueue(preq);

	/* Cancel the request if controller receive new SETUP packet. */
	if (cdns2_check_new_setup(pdev)) {
		trace_cdns2_ep0_setup("overridden");
		spin_unlock_irqrestore(&pdev->lock, flags);
		return -ECONNRESET;
	}

	/* Send STATUS stage. Should be called only for SET_CONFIGURATION. */
	if (pdev->ep0_stage == CDNS2_STATUS_STAGE) {
		cdns2_status_stage(pdev);

		request->actual = 0;
		pdev->status_completion_no_call = true;
		pdev->pending_status_request = request;
		usb_gadget_set_state(&pdev->gadget, USB_STATE_CONFIGURED);
		spin_unlock_irqrestore(&pdev->lock, flags);

		/*
		 * Since there is no completion interrupt for status stage,
		 * it needs to call ->completion in software after
		 * cdns2_gadget_ep0_queue is back.
		 */
		queue_work(system_freezable_wq, &pdev->pending_status_wq);
		return 0;
	}

	if (!list_empty(&pep->pending_list)) {
		trace_cdns2_ep0_setup("pending");
		dev_err(pdev->dev,
			"can't handle multiple requests for ep0\n");
		spin_unlock_irqrestore(&pdev->lock, flags);
		return -EBUSY;
	}

	ret = usb_gadget_map_request_by_dev(pdev->dev, request, pep->dir);
	if (ret) {
		spin_unlock_irqrestore(&pdev->lock, flags);
		dev_err(pdev->dev, "failed to map request\n");
		return -EINVAL;
	}

	request->status = -EINPROGRESS;
	list_add_tail(&preq->list, &pep->pending_list);

	if (request->zero && request->length &&
	    (request->length % ep->maxpacket == 0))
		zlp = 1;

	cdns2_ep0_enqueue(pdev, request->dma, request->length, zlp);

	spin_unlock_irqrestore(&pdev->lock, flags);

	return 0;
}

static const struct usb_ep_ops cdns2_gadget_ep0_ops = {
	.enable = cdns2_gadget_ep0_enable,
	.disable = cdns2_gadget_ep0_disable,
	.alloc_request = cdns2_gadget_ep_alloc_request,
	.free_request = cdns2_gadget_ep_free_request,
	.queue = cdns2_gadget_ep0_queue,
	.dequeue = cdns2_gadget_ep_dequeue,
	.set_halt = cdns2_gadget_ep0_set_halt,
	.set_wedge = cdns2_gadget_ep0_set_wedge,
};

void cdns2_ep0_config(struct cdns2_device *pdev)
{
	struct cdns2_endpoint *pep;

	pep = &pdev->eps[0];

	if (!list_empty(&pep->pending_list)) {
		struct cdns2_request *preq;

		preq = cdns2_next_preq(&pep->pending_list);
		list_del_init(&preq->list);
	}

	writeb(EP0_FIFO_AUTO, &pdev->ep0_regs->fifo);
	cdns2_select_ep(pdev, USB_DIR_OUT);
	writel(DMA_EP_CFG_ENABLE, &pdev->adma_regs->ep_cfg);

	writeb(EP0_FIFO_IO_TX | EP0_FIFO_AUTO, &pdev->ep0_regs->fifo);
	cdns2_select_ep(pdev, USB_DIR_IN);
	writel(DMA_EP_CFG_ENABLE, &pdev->adma_regs->ep_cfg);

	writeb(pdev->gadget.ep0->maxpacket, &pdev->ep0_regs->maxpack);
	writel(DMA_EP_IEN_EP_OUT0 | DMA_EP_IEN_EP_IN0,
	       &pdev->adma_regs->ep_ien);
}

void cdns2_init_ep0(struct cdns2_device *pdev,
		    struct cdns2_endpoint *pep)
{
	u16 maxpacket = le16_to_cpu(cdns2_gadget_ep0_desc.wMaxPacketSize);

	usb_ep_set_maxpacket_limit(&pep->endpoint, maxpacket);

	pep->endpoint.ops = &cdns2_gadget_ep0_ops;
	pep->endpoint.desc = &cdns2_gadget_ep0_desc;
	pep->endpoint.caps.type_control = true;
	pep->endpoint.caps.dir_in = true;
	pep->endpoint.caps.dir_out = true;

	pdev->gadget.ep0 = &pep->endpoint;
}
