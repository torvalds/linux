/* drivers/usb/gadget/s3c_udc_otg_xfer_dma.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Samsung S3C on-chip full/high speed USB OTG 2.0 device controllers
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define GINTMSK_INIT	(INT_OUT_EP | INT_IN_EP | INT_RESUME |\
				INT_ENUMDONE|INT_RESET|INT_SUSPEND)
#define DOEPMSK_INIT	(CTRL_OUT_EP_SETUP_PHASE_DONE | AHB_ERROR |\
				TRANSFER_DONE)
#define DIEPMSK_INIT	(NON_ISO_IN_EP_TIMEOUT|AHB_ERROR|TRANSFER_DONE)
#define GAHBCFG_INIT	(PTXFE_HALF | NPTXFE_HALF | MODE_DMA | BURST_INCR4 |\
				GBL_INT_UNMASK)

#define	DMA_ADDR_INVALID	(~(dma_addr_t)0)

static u8 clear_feature_num;
static int clear_feature_flag;
static int set_conf_done;

/* Bulk-Only Mass Storage Reset (class-specific request) */
#define GET_MAX_LUN_REQUEST	0xFE
#define BOT_RESET_REQUEST	0xFF

/* TEST MODE in set_feature request */
#define TEST_SELECTOR_MASK	0xFF
#define TEST_PKT_SIZE		53

static u8 test_pkt[TEST_PKT_SIZE] __attribute__((aligned(8))) = {
	/* JKJKJKJK x 9 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* JJKKJJKK x 8 */
	0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA,
	/* JJJJKKKK x 8 */
	0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE,
	/* JJJJJJJKKKKKKK x8 - '1' */
	0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	/* '1' + JJJJJJJK x 8 */
	0x7F, 0xBF, 0xDF, 0xEF, 0xF7, 0xFB, 0xFD,
	/* {JKKKKKKK x 10},JK */
	0xFC, 0x7E, 0xBF, 0xDF, 0xEF, 0xF7, 0xFB, 0xFD, 0x7E
};

static void s3c_udc_ep_set_stall(struct s3c_ep *ep);

#if defined(CONFIG_BATTERY_SAMSUNG)
void s3c_udc_cable_connect(struct s3c_udc *dev)
{
	samsung_cable_check_status(1);
}

void s3c_udc_cable_disconnect(struct s3c_udc *dev)
{
	samsung_cable_check_status(0);
}
#endif

static inline void s3c_udc_ep0_zlp(struct s3c_udc *dev)
{
	u32 ep_ctrl;

	__raw_writel(dev->usb_ctrl_dma, dev->regs + S3C_UDC_OTG_DIEPDMA(EP0_CON));
	__raw_writel((1<<19 | 0<<0), dev->regs + S3C_UDC_OTG_DIEPTSIZ(EP0_CON));

	ep_ctrl = __raw_readl(dev->regs + S3C_UDC_OTG_DIEPCTL(EP0_CON));
	__raw_writel(ep_ctrl | DEPCTL_EPENA | DEPCTL_CNAK,
		dev->regs + S3C_UDC_OTG_DIEPCTL(EP0_CON));

	DEBUG_EP0("%s:EP0 ZLP DIEPCTL0 = 0x%x\n",
		__func__, __raw_readl(dev->regs + S3C_UDC_OTG_DIEPCTL(EP0_CON)));
}

static inline void s3c_udc_pre_setup(struct s3c_udc *dev)
{
	u32 ep_ctrl;

	DEBUG_IN_EP("%s : Prepare Setup packets.\n", __func__);

	__raw_writel((3<<29) | (1<<19) | sizeof(struct usb_ctrlrequest),
		dev->regs + S3C_UDC_OTG_DOEPTSIZ(EP0_CON));
	__raw_writel(dev->usb_ctrl_dma, dev->regs + S3C_UDC_OTG_DOEPDMA(EP0_CON));

	ep_ctrl = __raw_readl(dev->regs + S3C_UDC_OTG_DOEPCTL(EP0_CON));
	__raw_writel(ep_ctrl|DEPCTL_EPENA|DEPCTL_CNAK,
		dev->regs + S3C_UDC_OTG_DOEPCTL(EP0_CON));
}

static int setdma_rx(struct s3c_ep *ep, struct s3c_request *req)
{
	u32 *buf, ctrl;
	u32 length, pktcnt;
	u32 ep_num = ep_index(ep);
	struct s3c_udc *udc = ep->dev;
	struct device *dev = &udc->dev->dev;

	buf = req->req.buf + req->req.actual;
	prefetchw(buf);

	length = req->req.length - req->req.actual;

	req->req.dma = dma_map_single(dev, buf,
				length, DMA_FROM_DEVICE);
	req->mapped = 1;

	if (length == 0)
		pktcnt = 1;
	else
		pktcnt = (length - 1)/(ep->ep.maxpacket) + 1;

	ctrl =  __raw_readl(udc->regs + S3C_UDC_OTG_DOEPCTL(ep_num));

	__raw_writel(virt_to_phys(buf), udc->regs + S3C_UDC_OTG_DOEPDMA(ep_num));
	__raw_writel((pktcnt<<19) | (length<<0),
		udc->regs + S3C_UDC_OTG_DOEPTSIZ(ep_num));
	__raw_writel(DEPCTL_EPENA | DEPCTL_CNAK | ctrl,
		udc->regs + S3C_UDC_OTG_DOEPCTL(ep_num));

	DEBUG_OUT_EP("%s: EP%d RX DMA start : DOEPDMA = 0x%x,"
			"DOEPTSIZ = 0x%x, DOEPCTL = 0x%x\n"
			"\tbuf = 0x%p, pktcnt = %d, xfersize = %d\n",
			__func__, ep_num,
			__raw_readl(udc->regs + S3C_UDC_OTG_DOEPDMA(ep_num)),
			__raw_readl(udc->regs + S3C_UDC_OTG_DOEPTSIZ(ep_num)),
			__raw_readl(udc->regs + S3C_UDC_OTG_DOEPCTL(ep_num)),
			buf, pktcnt, length);
	return 0;
}

static int setdma_tx(struct s3c_ep *ep, struct s3c_request *req)
{
	u32 *buf, ctrl = 0;
	u32 length, pktcnt;
	u32 ep_num = ep_index(ep);
	struct s3c_udc *udc = ep->dev;
	struct device *dev = &udc->dev->dev;

	buf = req->req.buf + req->req.actual;
	prefetch(buf);
	length = req->req.length - req->req.actual;

	if (ep_num == EP0_CON)
		length = min(length, (u32)ep_maxpacket(ep));

	req->req.actual += length;

	req->req.dma = dma_map_single(dev, buf,
			length, DMA_TO_DEVICE);
	req->mapped = 1;

	if (length == 0)
		pktcnt = 1;
	else
		pktcnt = (length - 1)/(ep->ep.maxpacket) + 1;

#ifdef DED_TX_FIFO
	/* Write the FIFO number to be used for this endpoint */
	ctrl = __raw_readl(udc->regs + S3C_UDC_OTG_DIEPCTL(ep_num));
	ctrl &= ~DEPCTL_TXFNUM_MASK;;
	ctrl |= (ep_num << DEPCTL_TXFNUM_BIT);
	__raw_writel(ctrl , udc->regs + S3C_UDC_OTG_DIEPCTL(ep_num));
#endif

	__raw_writel(virt_to_phys(buf), udc->regs + S3C_UDC_OTG_DIEPDMA(ep_num));
	__raw_writel((pktcnt<<19)|(length<<0), udc->regs + S3C_UDC_OTG_DIEPTSIZ(ep_num));
	ctrl = __raw_readl(udc->regs + S3C_UDC_OTG_DIEPCTL(ep_num));
	__raw_writel(DEPCTL_EPENA|DEPCTL_CNAK|ctrl,
		udc->regs + S3C_UDC_OTG_DIEPCTL(ep_num));

#ifndef DED_TX_FIFO
	ctrl = __raw_readl(udc->regs + S3C_UDC_OTG_DIEPCTL(EP0_CON));
	ctrl = (ctrl & ~(EP_MASK<<DEPCTL_NEXT_EP_BIT)) |
		(ep_num<<DEPCTL_NEXT_EP_BIT);
	__raw_writel(ctrl, udc->regs + S3C_UDC_OTG_DIEPCTL(EP0_CON));
#endif

	DEBUG_IN_EP("%s:EP%d TX DMA start : DIEPDMA0 = 0x%x,"
			"DIEPTSIZ0 = 0x%x, DIEPCTL0 = 0x%x\n"
			"\tbuf = 0x%p, pktcnt = %d, xfersize = %d\n",
			__func__, ep_num,
			__raw_readl(udc->regs + S3C_UDC_OTG_DIEPDMA(ep_num)),
			__raw_readl(udc->regs + S3C_UDC_OTG_DIEPTSIZ(ep_num)),
			__raw_readl(udc->regs + S3C_UDC_OTG_DIEPCTL(ep_num)),
			buf, pktcnt, length);

	req->written_bytes = length;
	return length;
}

static void complete_rx(struct s3c_udc *dev, u8 ep_num)
{
	struct s3c_ep *ep = &dev->ep[ep_num];
	struct s3c_request *req = NULL;
	u32 ep_tsr = 0, xfer_size = 0, xfer_length, is_short = 0;

	if (list_empty(&ep->queue)) {
		DEBUG_OUT_EP("%s: RX DMA done : NULL REQ on OUT EP-%d\n",
					__func__, ep_num);
		return;
	}

	req = list_entry(ep->queue.next, struct s3c_request, queue);

	ep_tsr = __raw_readl(dev->regs + S3C_UDC_OTG_DOEPTSIZ(ep_num));

	if (ep_num == EP0_CON)
		xfer_size = (ep_tsr & 0x7f);

	else
		xfer_size = (ep_tsr & 0x7fff);

	__dma_single_cpu_to_dev(req->req.buf, req->req.length, DMA_FROM_DEVICE);
	xfer_length = req->req.length - xfer_size;
	req->req.actual += min(xfer_length, req->req.length - req->req.actual);
	is_short = (xfer_length < ep->ep.maxpacket);

	DEBUG_OUT_EP("%s: RX DMA done : ep = %d, rx bytes = %d/%d, "
		     "is_short = %d, DOEPTSIZ = 0x%x, remained bytes = %d\n",
			__func__, ep_num, req->req.actual, req->req.length,
			is_short, ep_tsr, xfer_size);

	if (is_short || req->req.actual == xfer_length) {
		if (ep_num == EP0_CON && dev->ep0state == DATA_STATE_RECV) {
			DEBUG_OUT_EP("	=> Send ZLP\n");
			dev->ep0state = WAIT_FOR_SETUP;
			s3c_udc_ep0_zlp(dev);
		} else {
			done(ep, req, 0);

			if (!list_empty(&ep->queue)) {
				req = list_entry(ep->queue.next,
					struct s3c_request, queue);
				DEBUG_OUT_EP("%s: Next Rx request start...\n",
					 __func__);
				setdma_rx(ep, req);
			}
		}
	}
}

static void complete_tx(struct s3c_udc *dev, u8 ep_num)
{
	struct s3c_ep *ep = &dev->ep[ep_num];
	struct s3c_request *req;
	u32 ep_tsr = 0, xfer_size = 0, xfer_length, is_short = 0;
	u32 last;

	if (list_empty(&ep->queue)) {
		DEBUG_IN_EP("%s: TX DMA done : NULL REQ on IN EP-%d\n",
					__func__, ep_num);
		return;
	}

	req = list_entry(ep->queue.next, struct s3c_request, queue);

	if (dev->ep0state == DATA_STATE_XMIT) {
		DEBUG_IN_EP("%s: ep_num = %d, ep0stat == DATA_STATE_XMIT\n",
					__func__, ep_num);

		last = write_fifo_ep0(ep, req);

		if (last)
			dev->ep0state = WAIT_FOR_SETUP;

		return;
	}

	ep_tsr = __raw_readl(dev->regs + S3C_UDC_OTG_DIEPTSIZ(ep_num));

	if (ep_num == EP0_CON)
		xfer_size = (ep_tsr & 0x7f);
	else
		xfer_size = (ep_tsr & 0x7fff);

	req->req.actual = req->req.length - xfer_size;
	xfer_length = req->req.length - xfer_size;
	req->req.actual += min(xfer_length, req->req.length - req->req.actual);
	is_short = (xfer_length < ep->ep.maxpacket);

	DEBUG_IN_EP("%s: TX DMA done : ep = %d, tx bytes = %d/%d, "
		     "is_short = %d, DIEPTSIZ = 0x%x, remained bytes = %d\n",
			__func__, ep_num, req->req.actual, req->req.length,
			is_short, ep_tsr, xfer_size);

	if (req->req.actual == req->req.length) {
		/* send ZLP when req.zero is set and the last packet is maxpacket */
		if (req->req.zero) {
			req->req.zero = 0;
			if (req->written_bytes == ep_maxpacket(ep)) {
				setdma_tx(ep, req);
				return;
			}
		}
		done(ep, req, 0);

		if (!list_empty(&ep->queue)) {
			req = list_entry(ep->queue.next, struct s3c_request,
				queue);
			DEBUG_IN_EP("%s: Next Tx request start...\n", __func__);
			setdma_tx(ep, req);
		}
	}
}

static inline void s3c_udc_check_tx_queue(struct s3c_udc *dev, u8 ep_num)
{
	struct s3c_ep *ep = &dev->ep[ep_num];
	struct s3c_request *req;

	DEBUG_IN_EP("%s: Check queue, ep_num = %d\n", __func__, ep_num);

	if (!list_empty(&ep->queue)) {
		req = list_entry(ep->queue.next, struct s3c_request, queue);
		DEBUG_IN_EP("%s: Next Tx request(0x%p) start...\n",
			__func__, req);

		if (ep_is_in(ep))
			setdma_tx(ep, req);
		else
			setdma_rx(ep, req);
	} else {
		DEBUG_IN_EP("%s: NULL REQ on IN EP-%d\n", __func__, ep_num);

		return;
	}
}

static void process_ep_in_intr(struct s3c_udc *dev)
{
	u32 ep_intr, ep_intr_status;
	u8 ep_num = 0;

	ep_intr = __raw_readl(dev->regs + S3C_UDC_OTG_DAINT);
	DEBUG_IN_EP("*** %s: EP In interrupt : DAINT = 0x%x\n",
				__func__, ep_intr);

	ep_intr &= DAINT_MASK;

	while (ep_intr) {
		if (ep_intr & 0x1) {
			ep_intr_status = __raw_readl(dev->regs + S3C_UDC_OTG_DIEPINT(ep_num));
			DEBUG_IN_EP("\tEP%d-IN : DIEPINT = 0x%x\n",
						ep_num, ep_intr_status);

			/* Interrupt Clear */
			__raw_writel(ep_intr_status, dev->regs + S3C_UDC_OTG_DIEPINT(ep_num));

			if (ep_intr_status & TRANSFER_DONE) {
				complete_tx(dev, ep_num);

				if (ep_num == 0) {
					if (dev->ep0state == WAIT_FOR_SETUP)
						s3c_udc_pre_setup(dev);

					/* continue transfer after
						set_clear_halt for DMA mode */
					if (clear_feature_flag == 1) {
						s3c_udc_check_tx_queue(dev,
							clear_feature_num);
						clear_feature_flag = 0;
					}
				}
			}
		}
		ep_num++;
		ep_intr >>= 1;
	}
}

static void process_ep_out_intr(struct s3c_udc *dev)
{
	u32 ep_intr, ep_intr_status;
	u8 ep_num = 0;
	u32 ep_ctrl = 0;
	ep_intr = __raw_readl(dev->regs + S3C_UDC_OTG_DAINT);
	DEBUG_OUT_EP("*** %s: EP OUT interrupt : DAINT = 0x%x\n",
				__func__, ep_intr);

	ep_intr = (ep_intr >> DAINT_OUT_BIT) & DAINT_MASK;

	while (ep_intr) {
		if (ep_intr & 0x1) {
			ep_intr_status = __raw_readl(dev->regs + S3C_UDC_OTG_DOEPINT(ep_num));
			DEBUG_OUT_EP("\tEP%d-OUT : DOEPINT = 0x%x\n",
						ep_num, ep_intr_status);

			/* Interrupt Clear */
			__raw_writel(ep_intr_status, dev->regs + S3C_UDC_OTG_DOEPINT(ep_num));

			if (ep_num == 0) {
				if (ep_intr_status &
					CTRL_OUT_EP_SETUP_PHASE_DONE) {
					DEBUG_OUT_EP("\tSETUP"
						"packet(transaction)"
						"arrived\n");
					s3c_handle_ep0(dev);
				}

				if (ep_intr_status & TRANSFER_DONE) {
					complete_rx(dev, ep_num);
					__raw_writel((3<<29)|(1 << 19)|sizeof(struct usb_ctrlrequest),
						dev->regs + S3C_UDC_OTG_DOEPTSIZ(EP0_CON));
					__raw_writel(dev->usb_ctrl_dma,
						dev->regs + S3C_UDC_OTG_DOEPDMA(EP0_CON));

					ep_ctrl = readl(dev->regs + S3C_UDC_OTG_DOEPCTL(EP0_CON));
					__raw_writel(ep_ctrl|DEPCTL_EPENA|DEPCTL_SNAK,
						dev->regs + S3C_UDC_OTG_DOEPCTL(EP0_CON));
				}

			} else {
				if (ep_intr_status & TRANSFER_DONE)
					complete_rx(dev, ep_num);
			}
		}
		ep_num++;
		ep_intr >>= 1;
	}
}

/*
 *	usb client interrupt handler.
 */
static irqreturn_t s3c_udc_irq(int irq, void *_dev)
{
	struct s3c_udc *dev = _dev;
	u32 intr_status;
	u32 usb_status, gintmsk;
	unsigned long flags;

	spin_lock_irqsave(&dev->lock, flags);

	intr_status = __raw_readl(dev->regs + S3C_UDC_OTG_GINTSTS);
	gintmsk = __raw_readl(dev->regs + S3C_UDC_OTG_GINTMSK);

	DEBUG_ISR("\n*** %s : GINTSTS=0x%x(on state %s), GINTMSK :"
			"0x%x, DAINT : 0x%x, DAINTMSK : 0x%x\n",
			__func__, intr_status,
			state_names[dev->ep0state], gintmsk,
			__raw_readl(dev->regs + S3C_UDC_OTG_DAINT), __raw_readl(dev->regs + S3C_UDC_OTG_DAINTMSK));

	if (!intr_status) {
		spin_unlock_irqrestore(&dev->lock, flags);
		return IRQ_HANDLED;
	}

	if (intr_status & INT_ENUMDONE) {
		DEBUG_ISR("\tSpeed Detection interrupt\n");

		__raw_writel(INT_ENUMDONE, dev->regs + S3C_UDC_OTG_GINTSTS);
		usb_status = (__raw_readl(dev->regs + S3C_UDC_OTG_DSTS) & 0x6);

		if (usb_status & (USB_FULL_30_60MHZ | USB_FULL_48MHZ)) {
			DEBUG_ISR("\t\tFull Speed Detection\n");
			set_max_pktsize(dev, USB_SPEED_FULL);

		} else {
			DEBUG_ISR("\t\tHigh Speed Detection : 0x%x\n",
				usb_status);
			set_max_pktsize(dev, USB_SPEED_HIGH);
		}
	}

	if (intr_status & INT_EARLY_SUSPEND) {
		DEBUG_ISR("\tEarly suspend interrupt\n");
		__raw_writel(INT_EARLY_SUSPEND, dev->regs + S3C_UDC_OTG_GINTSTS);
	}

	if (intr_status & INT_SUSPEND) {
		usb_status = __raw_readl(dev->regs + S3C_UDC_OTG_DSTS);
		DEBUG_ISR("\tSuspend interrupt :(DSTS):0x%x\n", usb_status);
		__raw_writel(INT_SUSPEND, dev->regs + S3C_UDC_OTG_GINTSTS);

		if (dev->gadget.speed != USB_SPEED_UNKNOWN
		    && dev->driver
		    && dev->driver->suspend) {

			dev->driver->suspend(&dev->gadget);
		}
	}

	if (intr_status & INT_RESUME) {
		DEBUG_ISR("\tResume interrupt\n");
		__raw_writel(INT_RESUME, dev->regs + S3C_UDC_OTG_GINTSTS);

		if (dev->gadget.speed != USB_SPEED_UNKNOWN
		    && dev->driver
		    && dev->driver->resume) {
			dev->driver->resume(&dev->gadget);
		}
	}

	if (intr_status & INT_RESET) {
		usb_status = __raw_readl(dev->regs + S3C_UDC_OTG_GOTGCTL);
		DEBUG_ISR("\tReset interrupt - (GOTGCTL):0x%x\n", usb_status);
		__raw_writel(INT_RESET, dev->regs + S3C_UDC_OTG_GINTSTS);

		set_conf_done = 0;

		if ((usb_status & 0xc0000) == (0x3 << 18)) {
			if (reset_available) {
				DEBUG_ISR("\t\tOTG core got reset (%d)!!\n",
					reset_available);
				reset_usbd();
				dev->ep0state = WAIT_FOR_SETUP;
				reset_available = 0;
				s3c_udc_pre_setup(dev);
			} else
				reset_available = 1;
		} else {
			reset_available = 1;
			DEBUG_ISR("\t\tRESET handling skipped\n");
			/* report disconnect; the driver is already quiesced */
			if (dev->driver) {
				spin_unlock(&dev->lock);
				dev->driver->disconnect(&dev->gadget);
				spin_lock(&dev->lock);
			}

#if defined(CONFIG_BATTERY_SAMSUNG)
			s3c_udc_cable_disconnect(dev);
#endif
		}
	}

	if (intr_status & INT_IN_EP)
		process_ep_in_intr(dev);

	if (intr_status & INT_OUT_EP)
		process_ep_out_intr(dev);

	spin_unlock_irqrestore(&dev->lock, flags);

	return IRQ_HANDLED;
}

/** Queue one request
 *  Kickstart transfer if needed
 */
static int s3c_queue(struct usb_ep *_ep, struct usb_request *_req,
			 gfp_t gfp_flags)
{
	struct s3c_request *req;
	struct s3c_ep *ep;
	struct s3c_udc *dev;
	unsigned long flags;
	u32 ep_num, gintsts;

	req = container_of(_req, struct s3c_request, req);
	if (unlikely(!_req || !_req->complete ||
			!_req->buf || !list_empty(&req->queue))) {

		DEBUG("%s: bad params\n", __func__);
		return -EINVAL;
	}

	ep = container_of(_ep, struct s3c_ep, ep);

	if (unlikely(!_ep || (!ep->desc && ep->ep.name != ep0name))) {

		DEBUG("%s: bad ep\n", __func__);
		return -EINVAL;
	}

	ep_num = ep_index(ep);
	dev = ep->dev;
	if (unlikely(!dev->driver || dev->gadget.speed == USB_SPEED_UNKNOWN)) {

		DEBUG("%s: bogus device state %p\n", __func__, dev->driver);
		return -ESHUTDOWN;
	}

	spin_lock_irqsave(&dev->lock, flags);

	_req->status = -EINPROGRESS;
	_req->actual = 0;

	/* kickstart this i/o queue? */
	DEBUG("\n*** %s: %s-%s req = %p, len = %d, buf = %p"
		"Q empty = %d, stopped = %d\n",
		__func__, _ep->name, ep_is_in(ep) ? "in" : "out",
		_req, _req->length, _req->buf,
		list_empty(&ep->queue), ep->stopped);

	if (list_empty(&ep->queue) && !ep->stopped) {

		if (ep_num == 0) {
			/* EP0 */
			list_add_tail(&req->queue, &ep->queue);
			s3c_ep0_kick(dev, ep);
			req = 0;

		} else if (ep_is_in(ep)) {
			gintsts = __raw_readl(dev->regs + S3C_UDC_OTG_GINTSTS);
			DEBUG_IN_EP("%s: ep_is_in, S3C_UDC_OTG_GINTSTS=0x%x\n",
						__func__, gintsts);

			if (set_conf_done == 1) {
				setdma_tx(ep, req);
			} else {
				done(ep, req, 0);
				DEBUG("%s: Not yet Set_configureation,"
					"ep_num = %d, req = %p\n",
					__func__, ep_num, req);
				req = 0;
			}

		} else {
			gintsts = __raw_readl(dev->regs + S3C_UDC_OTG_GINTSTS);
			DEBUG_OUT_EP("%s: ep_is_out,"
				"S3C_UDC_OTG_GINTSTS=0x%x\n",
				__func__, gintsts);

			setdma_rx(ep, req);
		}
	}

	/* pio or dma irq handler advances the queue. */
	if (likely(req != 0))
		list_add_tail(&req->queue, &ep->queue);

	spin_unlock_irqrestore(&dev->lock, flags);

	return 0;
}

/****************************************************************/
/* End Point 0 related functions                                */
/****************************************************************/

/* return:  0 = still running, 1 = completed, negative = errno */
static int write_fifo_ep0(struct s3c_ep *ep, struct s3c_request *req)
{
	u32 max;
	unsigned count;
	int is_last;

	max = ep_maxpacket(ep);

	DEBUG_EP0("%s: max = %d\n", __func__, max);

	count = setdma_tx(ep, req);

	/* last packet is usually short (or a zlp) */
	if (likely(count != max))
		is_last = 1;
	else {
		if (likely(req->req.length != req->req.actual))
			is_last = 0;
		else
			is_last = 1;
	}

	DEBUG_EP0("%s: wrote %s %d bytes%s %d left %p\n", __func__,
		  ep->ep.name, count,
		  is_last ? "/L" : "", req->req.length - req->req.actual, req);

	/* requests complete when all IN data is in the FIFO */
	if (is_last) {
		ep->dev->ep0state = WAIT_FOR_SETUP;
		return 1;
	}

	return 0;
}

/**
 * udc_set_address - set the USB address for this device
 * @address:
 *
 * Called from control endpoint function
 * after it decodes a set address setup packet.
 */
static void udc_set_address(struct s3c_udc *dev, unsigned char address)
{
	u32 ctrl = __raw_readl(dev->regs + S3C_UDC_OTG_DCFG);
	__raw_writel(address << 4 | ctrl, dev->regs + S3C_UDC_OTG_DCFG);

	s3c_udc_ep0_zlp(dev);

	DEBUG_EP0("%s: USB OTG 2.0 Device address=%d, DCFG=0x%x\n",
		__func__, address, __raw_readl(dev->regs + S3C_UDC_OTG_DCFG));

	dev->usb_address = address;
}

static inline void s3c_udc_ep0_set_stall(struct s3c_ep *ep)
{
	struct s3c_udc *dev;
	u32		ep_ctrl = 0;

	dev = ep->dev;
	ep_ctrl = __raw_readl(dev->regs + S3C_UDC_OTG_DIEPCTL(EP0_CON));

	/* set the disable and stall bits */
	if (ep_ctrl & DEPCTL_EPENA)
		ep_ctrl |= DEPCTL_EPDIS;

	ep_ctrl |= DEPCTL_STALL;

	__raw_writel(ep_ctrl, dev->regs + S3C_UDC_OTG_DIEPCTL(EP0_CON));

	DEBUG_EP0("%s: set ep%d stall, DIEPCTL0 = 0x%x\n",
		__func__, ep_index(ep), __raw_readl(dev->regs + S3C_UDC_OTG_DIEPCTL(EP0_CON)));
	/*
	 * The application can only set this bit, and the core clears it,
	 * when a SETUP token is received for this endpoint
	 */
	dev->ep0state = WAIT_FOR_SETUP;

	s3c_udc_pre_setup(dev);
}

static void s3c_ep0_read(struct s3c_udc *dev)
{
	struct s3c_request *req;
	struct s3c_ep *ep = &dev->ep[0];
	int ret;

	if (!list_empty(&ep->queue)) {
		req = list_entry(ep->queue.next, struct s3c_request, queue);

	} else {
		DEBUG("%s: ---> BUG\n", __func__);
		BUG();
		return;
	}

	DEBUG_EP0("%s: req = %p, req.length = 0x%x, req.actual = 0x%x\n",
		__func__, req, req->req.length, req->req.actual);

	if (req->req.length == 0) {
		/* zlp for Set_configuration, Set_interface,
		 * or Bulk-Only mass storge reset */

		dev->ep0state = WAIT_FOR_SETUP;
		set_conf_done = 1;
		s3c_udc_ep0_zlp(dev);

		DEBUG_EP0("%s: req.length = 0, bRequest = %d\n",
			__func__, dev->usb_ctrl->bRequest);
		return;
	}

	ret = setdma_rx(ep, req);
}

/*
 * DATA_STATE_XMIT
 */
static int s3c_ep0_write(struct s3c_udc *dev)
{
	struct s3c_request *req;
	struct s3c_ep *ep = &dev->ep[0];
	int ret;

	if (list_empty(&ep->queue))
		req = 0;
	else
		req = list_entry(ep->queue.next, struct s3c_request, queue);

	if (!req) {
		DEBUG_EP0("%s: NULL REQ\n", __func__);
		return 0;
	}

	DEBUG_EP0("%s: req = %p, req.length = 0x%x, req.actual = 0x%x\n",
		__func__, req, req->req.length, req->req.actual);

	ret = write_fifo_ep0(ep, req);

	if (ret == 1) {
		/* Last packet */
		dev->ep0state = WAIT_FOR_SETUP;
		DEBUG_EP0("%s: finished, waiting for status\n", __func__);

	} else {
		dev->ep0state = DATA_STATE_XMIT;
		DEBUG_EP0("%s: not finished\n", __func__);
	}

	return 1;
}

u16     g_status __attribute__((aligned(8)));

static int s3c_udc_get_status(struct s3c_udc *dev,
		struct usb_ctrlrequest *crq)
{
	u8 ep_num = crq->wIndex & 0x7F;
	u32 ep_ctrl;

	DEBUG_SETUP("%s: *** USB_REQ_GET_STATUS\n", __func__);

	switch (crq->bRequestType & USB_RECIP_MASK) {
	case USB_RECIP_INTERFACE:
		g_status = 0;
		DEBUG_SETUP("\tGET_STATUS: USB_RECIP_INTERFACE,"
			"g_stauts = %d\n", g_status);
		break;

	case USB_RECIP_DEVICE:
		g_status = 0x0;
		DEBUG_SETUP("\tGET_STATUS: USB_RECIP_DEVICE,"
			"g_stauts = %d\n", g_status);
		break;

	case USB_RECIP_ENDPOINT:
		if (crq->wLength > 2) {
			DEBUG_SETUP("\tGET_STATUS:"
				"Not support EP or wLength\n");
			return 1;
		}

		g_status = dev->ep[ep_num].stopped;
		DEBUG_SETUP("\tGET_STATUS: USB_RECIP_ENDPOINT,"
			"g_stauts = %d\n", g_status);

		break;
	default:
		return 1;
	}

	__dma_single_cpu_to_dev(&g_status, 2, DMA_TO_DEVICE);

	__raw_writel(virt_to_phys(&g_status), dev->regs + S3C_UDC_OTG_DIEPDMA(EP0_CON));
	__raw_writel((1<<19)|(2<<0), dev->regs + S3C_UDC_OTG_DIEPTSIZ(EP0_CON));

	ep_ctrl = __raw_readl(dev->regs + S3C_UDC_OTG_DIEPCTL(EP0_CON));
	__raw_writel(ep_ctrl|DEPCTL_EPENA|DEPCTL_CNAK, dev->regs + S3C_UDC_OTG_DIEPCTL(EP0_CON));
	dev->ep0state = WAIT_FOR_SETUP;

	return 0;
}

static void s3c_udc_ep_set_stall(struct s3c_ep *ep)
{
	struct s3c_udc *dev = ep->dev;
	u8		ep_num;
	u32		ep_ctrl = 0;

	ep_num = ep_index(ep);
	DEBUG("%s: ep_num = %d, ep_type = %d\n", __func__, ep_num, ep->ep_type);

	if (ep_is_in(ep)) {
		ep_ctrl = __raw_readl(dev->regs + S3C_UDC_OTG_DIEPCTL(ep_num));

		/* set the disable and stall bits */
		if (ep_ctrl & DEPCTL_EPENA)
			ep_ctrl |= DEPCTL_EPDIS;

		ep_ctrl |= DEPCTL_STALL;

		__raw_writel(ep_ctrl, dev->regs + S3C_UDC_OTG_DIEPCTL(ep_num));
		DEBUG("%s: set stall, DIEPCTL%d = 0x%x\n",
			__func__, ep_num, __raw_readl(dev->regs + S3C_UDC_OTG_DIEPCTL(ep_num)));

	} else {
		ep_ctrl = __raw_readl(dev->regs + S3C_UDC_OTG_DOEPCTL(ep_num));

		/* set the stall bit */
		ep_ctrl |= DEPCTL_STALL;

		__raw_writel(ep_ctrl, dev->regs + S3C_UDC_OTG_DOEPCTL(ep_num));
		DEBUG("%s: set stall, DOEPCTL%d = 0x%x\n",
			__func__, ep_num, __raw_readl(dev->regs + S3C_UDC_OTG_DOEPCTL(ep_num)));
	}

	return;
}

void s3c_udc_ep_clear_stall(struct s3c_ep *ep)
{
	struct s3c_udc *dev = ep->dev;
	u8		ep_num;
	u32		ep_ctrl = 0;

	ep_num = ep_index(ep);
	DEBUG("%s: ep_num = %d, ep_type = %d\n", __func__, ep_num, ep->ep_type);

	if (ep_is_in(ep)) {
		ep_ctrl = __raw_readl(dev->regs + S3C_UDC_OTG_DIEPCTL(ep_num));

		/* clear stall bit */
		ep_ctrl &= ~DEPCTL_STALL;

		/*
		 * USB Spec 9.4.5: For endpoints using data toggle, regardless
		 * of whether an endpoint has the Halt feature set, a
		 * ClearFeature(ENDPOINT_HALT) request always results in the
		 * data toggle being reinitialized to DATA0.
		 */
		if (ep->bmAttributes == USB_ENDPOINT_XFER_INT
		    || ep->bmAttributes == USB_ENDPOINT_XFER_BULK) {
			ep_ctrl |= DEPCTL_SETD0PID; /* DATA0 */
		}

		__raw_writel(ep_ctrl, dev->regs + S3C_UDC_OTG_DIEPCTL(ep_num));
		DEBUG("%s: cleared stall, DIEPCTL%d = 0x%x\n",
			__func__, ep_num, __raw_readl(dev->regs + S3C_UDC_OTG_DIEPCTL(ep_num)));

	} else {
		ep_ctrl = __raw_readl(dev->regs + S3C_UDC_OTG_DOEPCTL(ep_num));

		/* clear stall bit */
		ep_ctrl &= ~DEPCTL_STALL;

		if (ep->bmAttributes == USB_ENDPOINT_XFER_INT
		    || ep->bmAttributes == USB_ENDPOINT_XFER_BULK) {
			ep_ctrl |= DEPCTL_SETD0PID; /* DATA0 */
		}

		__raw_writel(ep_ctrl, dev->regs + S3C_UDC_OTG_DOEPCTL(ep_num));
		DEBUG("%s: cleared stall, DOEPCTL%d = 0x%x\n",
			__func__, ep_num, __raw_readl(dev->regs + S3C_UDC_OTG_DOEPCTL(ep_num)));
	}

	return;
}

static int s3c_udc_set_halt(struct usb_ep *_ep, int value)
{
	struct s3c_ep	*ep;
	struct s3c_udc	*dev;
	unsigned long	flags;
	u8		ep_num;

	ep = container_of(_ep, struct s3c_ep, ep);
	ep_num = ep_index(ep);

	if (unlikely(!_ep || !ep->desc || ep_num == EP0_CON ||
			ep->desc->bmAttributes == USB_ENDPOINT_XFER_ISOC)) {
		DEBUG("%s: %s bad ep or descriptor\n", __func__, ep->ep.name);
		return -EINVAL;
	}

	/* Attempt to halt IN ep will fail if any transfer requests
	 * are still queue */
	if (value && ep_is_in(ep) && !list_empty(&ep->queue)) {
		DEBUG("%s: %s queue not empty, req = %p\n",
			__func__, ep->ep.name,
			list_entry(ep->queue.next, struct s3c_request, queue));

		return -EAGAIN;
	}

	dev = ep->dev;
	DEBUG("%s: ep_num = %d, value = %d\n", __func__, ep_num, value);

	spin_lock_irqsave(&dev->lock, flags);

	if (value == 0) {
		ep->stopped = 0;
		s3c_udc_ep_clear_stall(ep);
	} else {
		ep->stopped = 1;
		s3c_udc_ep_set_stall(ep);
	}

	spin_unlock_irqrestore(&dev->lock, flags);

	return 0;
}

void s3c_udc_ep_activate(struct s3c_ep *ep)
{
	struct s3c_udc *dev = ep->dev;
	u8 ep_num;
	u32 ep_ctrl = 0, daintmsk = 0;

	ep_num = ep_index(ep);

	/* Read DEPCTLn register */
	if (ep_is_in(ep)) {
		ep_ctrl = __raw_readl(dev->regs + S3C_UDC_OTG_DIEPCTL(ep_num));
		daintmsk = 1 << ep_num;
	} else {
		ep_ctrl = __raw_readl(dev->regs + S3C_UDC_OTG_DOEPCTL(ep_num));
		daintmsk = (1 << ep_num) << DAINT_OUT_BIT;
	}

	DEBUG("%s: EPCTRL%d = 0x%x, ep_is_in = %d\n",
		__func__, ep_num, ep_ctrl, ep_is_in(ep));

	/* If the EP is already active don't change the EP Control
	 * register. */
	if (!(ep_ctrl & DEPCTL_USBACTEP)) {
		ep_ctrl = (ep_ctrl & ~DEPCTL_TYPE_MASK) |
			(ep->bmAttributes << DEPCTL_TYPE_BIT);
		ep_ctrl = (ep_ctrl & ~DEPCTL_MPS_MASK) |
			(ep->ep.maxpacket << DEPCTL_MPS_BIT);
		ep_ctrl |= (DEPCTL_SETD0PID | DEPCTL_USBACTEP | DEPCTL_SNAK);

		if (ep_is_in(ep)) {
			__raw_writel(ep_ctrl, dev->regs + S3C_UDC_OTG_DIEPCTL(ep_num));
			DEBUG("%s: USB Ative EP%d, DIEPCTRL%d = 0x%x\n",
				__func__, ep_num, ep_num,
				__raw_readl(dev->regs + S3C_UDC_OTG_DIEPCTL(ep_num)));
		} else {
			__raw_writel(ep_ctrl, dev->regs + S3C_UDC_OTG_DOEPCTL(ep_num));
			DEBUG("%s: USB Ative EP%d, DOEPCTRL%d = 0x%x\n",
				__func__, ep_num, ep_num,
				__raw_readl(dev->regs + S3C_UDC_OTG_DOEPCTL(ep_num)));
		}
	}

	/* Unmask EP Interrtupt */
	__raw_writel(__raw_readl(dev->regs + S3C_UDC_OTG_DAINTMSK)|daintmsk, dev->regs + S3C_UDC_OTG_DAINTMSK);
	DEBUG("%s: DAINTMSK = 0x%x\n", __func__, __raw_readl(dev->regs + S3C_UDC_OTG_DAINTMSK));
}

static int s3c_udc_clear_feature(struct usb_ep *_ep)
{
	struct s3c_ep	*ep;
	u8		ep_num;
	struct usb_ctrlrequest *usb_ctrl;

	ep = container_of(_ep, struct s3c_ep, ep);
	ep_num = ep_index(ep);
	usb_ctrl = ep->dev->usb_ctrl;

	DEBUG_SETUP("%s: ep_num = %d, is_in = %d, clear_feature_flag = %d\n",
		__func__, ep_num, ep_is_in(ep), clear_feature_flag);

	if (usb_ctrl->wLength != 0) {
		DEBUG_SETUP("\tCLEAR_FEATURE:"
			"wLength is not zero.....\n");
		return 1;
	}

	switch (usb_ctrl->bRequestType & USB_RECIP_MASK) {
	case USB_RECIP_DEVICE:
		switch (usb_ctrl->wValue) {
		case USB_DEVICE_REMOTE_WAKEUP:
			DEBUG_SETUP("\tCLEAR_FEATURE:"
				"USB_DEVICE_REMOTE_WAKEUP\n");
			break;

		case USB_DEVICE_TEST_MODE:
			DEBUG_SETUP("\tCLEAR_FEATURE:"
				"USB_DEVICE_TEST_MODE\n");
			/** @todo Add CLEAR_FEATURE for TEST modes. */
			break;
		}

		s3c_udc_ep0_zlp(ep->dev);
		break;

	case USB_RECIP_ENDPOINT:
		DEBUG_SETUP("\tCLEAR_FEATURE: USB_RECIP_ENDPOINT,"
			"wValue = %d\n", usb_ctrl->wValue);

		if (usb_ctrl->wValue == USB_ENDPOINT_HALT) {
			if (ep_num == 0) {
				s3c_udc_ep0_set_stall(ep);
				return 0;
			}

			s3c_udc_ep0_zlp(ep->dev);

			s3c_udc_ep_clear_stall(ep);
			s3c_udc_ep_activate(ep);
			ep->stopped = 0;

			clear_feature_num = ep_num;
			clear_feature_flag = 1;
		}
		break;
	}

	return 0;
}

/* Set into the test mode for Test Mode set_feature request */
static inline void set_test_mode(struct s3c_udc *dev)
{
	u32 ep_ctrl, dctl;
	u8 test_selector = (dev->usb_ctrl->wIndex>>8) & TEST_SELECTOR_MASK;

	if (test_selector > 0 && test_selector < 6) {
		ep_ctrl = __raw_readl(dev->regs + S3C_UDC_OTG_DIEPCTL(EP0_CON));

		__raw_writel(1<<19 | 0<<0, dev->regs + S3C_UDC_OTG_DIEPTSIZ(EP0_CON));
		__raw_writel(ep_ctrl | DEPCTL_EPENA | DEPCTL_CNAK
			| EP0_CON<<DEPCTL_NEXT_EP_BIT,
			dev->regs + S3C_UDC_OTG_DIEPCTL(EP0_CON));
	}

	switch (test_selector) {
	case TEST_J_SEL:
		/* some delay is necessary like printk() or udelay() */
		printk(KERN_INFO "Test mode selector in set_feature request is"
			"TEST J\n");

		dctl = __raw_readl(dev->regs + S3C_UDC_OTG_DCTL);
		__raw_writel((dctl & ~(TEST_CONTROL_MASK)) | TEST_J_MODE,
			dev->regs + S3C_UDC_OTG_DCTL);
		break;
	case TEST_K_SEL:
		/* some delay is necessary like printk() or udelay() */
		printk(KERN_INFO "Test mode selector in set_feature request is"
			"TEST K\n");

		dctl = __raw_readl(dev->regs + S3C_UDC_OTG_DCTL);
		__raw_writel((dctl&~(TEST_CONTROL_MASK))|TEST_K_MODE,
			dev->regs + S3C_UDC_OTG_DCTL);
		break;
	case TEST_SE0_NAK_SEL:
		/* some delay is necessary like printk() or udelay() */
		printk(KERN_INFO "Test mode selector in set_feature request is"
			"TEST SE0 NAK\n");

		dctl = __raw_readl(dev->regs + S3C_UDC_OTG_DCTL);
		__raw_writel((dctl & ~(TEST_CONTROL_MASK)) | TEST_SE0_NAK_MODE,
			dev->regs + S3C_UDC_OTG_DCTL);
		break;
	case TEST_PACKET_SEL:
		/* some delay is necessary like printk() or udelay() */
		printk(KERN_INFO "Test mode selector in set_feature request is"
			"TEST PACKET\n");

		__dma_single_cpu_to_dev(test_pkt, TEST_PKT_SIZE, DMA_TO_DEVICE);
		__raw_writel(virt_to_phys(test_pkt), dev->regs + S3C_UDC_OTG_DIEPDMA(EP0_CON));

		ep_ctrl = __raw_readl(dev->regs + S3C_UDC_OTG_DIEPCTL(EP0_CON));

		__raw_writel(1<<19 | TEST_PKT_SIZE<<0, dev->regs + S3C_UDC_OTG_DIEPTSIZ(EP0_CON));
		__raw_writel(ep_ctrl | DEPCTL_EPENA | DEPCTL_CNAK
				| EP0_CON<<DEPCTL_NEXT_EP_BIT,
				dev->regs + S3C_UDC_OTG_DIEPCTL(EP0_CON));

		dctl = __raw_readl(dev->regs + S3C_UDC_OTG_DCTL);
		__raw_writel((dctl & ~(TEST_CONTROL_MASK)) | TEST_PACKET_MODE,
				dev->regs + S3C_UDC_OTG_DCTL);
		break;
	case TEST_FORCE_ENABLE_SEL:
		/* some delay is necessary like printk() or udelay() */
		printk(KERN_INFO "Test mode selector in set_feature request is"
					"TEST FORCE ENABLE\n");

		dctl = __raw_readl(dev->regs + S3C_UDC_OTG_DCTL);
		__raw_writel((dctl & ~(TEST_CONTROL_MASK)) | TEST_FORCE_ENABLE_MODE,
				dev->regs + S3C_UDC_OTG_DCTL);
		break;
	}
}

static int s3c_udc_set_feature(struct usb_ep *_ep)
{
	struct s3c_ep	*ep;
	u8		ep_num;
	struct usb_ctrlrequest *usb_ctrl;

	ep = container_of(_ep, struct s3c_ep, ep);
	ep_num = ep_index(ep);
	usb_ctrl = ep->dev->usb_ctrl;

	DEBUG_SETUP("%s: *** USB_REQ_SET_FEATURE,"
			"ep_num = %d\n", __func__, ep_num);

	if (usb_ctrl->wLength != 0) {
		DEBUG_SETUP("\tSET_FEATURE: wLength is not zero.....\n");
		return 1;
	}

	switch (usb_ctrl->bRequestType & USB_RECIP_MASK) {
	case USB_RECIP_DEVICE:
		switch (usb_ctrl->wValue) {
		case USB_DEVICE_REMOTE_WAKEUP:
			DEBUG_SETUP("\tSET_FEATURE:"
						"USB_DEVICE_REMOTE_WAKEUP\n");
			break;

		case USB_DEVICE_TEST_MODE:
			DEBUG_SETUP("\tSET_FEATURE: USB_DEVICE_TEST_MODE\n");
			set_test_mode(ep->dev);
			break;

		case USB_DEVICE_B_HNP_ENABLE:
			DEBUG_SETUP("\tSET_FEATURE: USB_DEVICE_B_HNP_ENABLE\n");
			break;

		case USB_DEVICE_A_HNP_SUPPORT:
			/* RH port supports HNP */
			DEBUG_SETUP("\tSET_FEATURE:"
					"USB_DEVICE_A_HNP_SUPPORT\n");
			break;

		case USB_DEVICE_A_ALT_HNP_SUPPORT:
			/* other RH port does */
			DEBUG_SETUP("\tSET_FEATURE:"
					"USB_DEVICE_A_ALT_HNP_SUPPORT\n");
			break;
		}

		s3c_udc_ep0_zlp(ep->dev);
		return 0;

	case USB_RECIP_INTERFACE:
		DEBUG_SETUP("\tSET_FEATURE: USB_RECIP_INTERFACE\n");
		break;

	case USB_RECIP_ENDPOINT:
		DEBUG_SETUP("\tSET_FEATURE: USB_RECIP_ENDPOINT\n");
		if (usb_ctrl->wValue == USB_ENDPOINT_HALT) {
			if (ep_num == 0) {
				s3c_udc_ep0_set_stall(ep);
				return 0;
			}
			ep->stopped = 1;
			s3c_udc_ep_set_stall(ep);
		}

		s3c_udc_ep0_zlp(ep->dev);
		return 0;
	}

	return 1;
}

/*
 * WAIT_FOR_SETUP (OUT_PKT_RDY)
 */
static void s3c_ep0_setup(struct s3c_udc *dev)
{
	struct s3c_ep *ep = &dev->ep[0];
	int i, is_in;
	u8 ep_num;
	struct usb_ctrlrequest *usb_ctrl = dev->usb_ctrl;

	/* Nuke all previous transfers */
	nuke(ep, -EPROTO);

	DEBUG_SETUP("%s: bRequestType = 0x%x(%s), bRequest = 0x%x"
			"\twLength = 0x%x, wValue = 0x%x, wIndex= 0x%x\n",
			__func__, usb_ctrl->bRequestType,
			(usb_ctrl->bRequestType & USB_DIR_IN) ? "IN" : "OUT",
			usb_ctrl->bRequest, usb_ctrl->wLength, usb_ctrl->wValue,
			usb_ctrl->wIndex);

	if (usb_ctrl->bRequest == GET_MAX_LUN_REQUEST && usb_ctrl->wLength != 1) {
		DEBUG_SETUP("\t%s:GET_MAX_LUN_REQUEST:invalid wLength = %d,"
			"setup returned\n", __func__, usb_ctrl->wLength);

		s3c_udc_ep0_set_stall(ep);
		dev->ep0state = WAIT_FOR_SETUP;

		return;
	} else if (usb_ctrl->bRequest ==
			BOT_RESET_REQUEST && usb_ctrl->wLength != 0) {
		/* Bulk-Only *mass storge reset of class-specific request */
		DEBUG_SETUP("\t%s:BOT Rest:invalid wLength = %d,"
			"setup returned\n",
			__func__, usb_ctrl->wLength);

		s3c_udc_ep0_set_stall(ep);
		dev->ep0state = WAIT_FOR_SETUP;

		return;
	}

	/* Set direction of EP0 */
	if (likely(usb_ctrl->bRequestType & USB_DIR_IN)) {
		ep->bEndpointAddress |= USB_DIR_IN;
		is_in = 1;

	} else {
		ep->bEndpointAddress &= ~USB_DIR_IN;
		is_in = 0;
	}
	/* cope with automagic for some standard requests. */
	dev->req_std = (usb_ctrl->bRequestType & USB_TYPE_MASK)
						== USB_TYPE_STANDARD;
	dev->req_config = 0;
	dev->req_pending = 1;

	/* Handle some SETUP packets ourselves */
	switch (usb_ctrl->bRequest) {
	case USB_REQ_SET_ADDRESS:
	DEBUG_SETUP("%s: *** USB_REQ_SET_ADDRESS (%d)\n",
			__func__, usb_ctrl->wValue);

		if (usb_ctrl->bRequestType
			!= (USB_TYPE_STANDARD | USB_RECIP_DEVICE))
			break;

		udc_set_address(dev, usb_ctrl->wValue);
		return;

	case USB_REQ_SET_CONFIGURATION:
		DEBUG_SETUP("============================================\n");
		DEBUG_SETUP("%s: USB_REQ_SET_CONFIGURATION (%d)\n",
				__func__, usb_ctrl->wValue);

		if (usb_ctrl->bRequestType == USB_RECIP_DEVICE) {
			reset_available = 1;
			dev->req_config = 1;
		}
#if defined(CONFIG_BATTERY_SAMSUNG)
		s3c_udc_cable_connect(dev);
#endif
		break;

	case USB_REQ_GET_DESCRIPTOR:
		DEBUG_SETUP("%s: *** USB_REQ_GET_DESCRIPTOR\n", __func__);
		break;

	case USB_REQ_SET_INTERFACE:
		DEBUG_SETUP("%s: *** USB_REQ_SET_INTERFACE (%d)\n",
				__func__, usb_ctrl->wValue);

		if (usb_ctrl->bRequestType == USB_RECIP_INTERFACE) {
			reset_available = 1;
			dev->req_config = 1;
		}
		break;

	case USB_REQ_GET_CONFIGURATION:
		DEBUG_SETUP("%s: *** USB_REQ_GET_CONFIGURATION\n", __func__);
		break;

	case USB_REQ_GET_STATUS:
		if (dev->req_std) {
			if (!s3c_udc_get_status(dev, usb_ctrl))
				return;
		}
		break;

	case USB_REQ_CLEAR_FEATURE:
		ep_num = usb_ctrl->wIndex & 0x7f;

		if (!s3c_udc_clear_feature(&dev->ep[ep_num].ep))
			return;
		break;
	case USB_REQ_SET_FEATURE:
		ep_num = usb_ctrl->wIndex & 0x7f;

		if (!s3c_udc_set_feature(&dev->ep[ep_num].ep))
			return;
		break;
	default:
		DEBUG_SETUP("%s: *** Default of usb_ctrl->bRequest=0x%x"
			"happened.\n", __func__, usb_ctrl->bRequest);
		break;
	}

	if (likely(dev->driver)) {
		/* device-2-host (IN) or no data setup command,
		 * process immediately */
		DEBUG_SETUP("%s: usb_ctrlrequest will be passed to"
			"fsg_setup()\n", __func__);

		spin_unlock(&dev->lock);
		i = dev->driver->setup(&dev->gadget, usb_ctrl);
		spin_lock(&dev->lock);

		if (i < 0) {
			if (dev->req_config) {
				DEBUG_SETUP("\tconfig change 0x%02x fail %d?\n",
					(u32)usb_ctrl->bRequest, i);
			}

			/* setup processing failed, force stall */
			s3c_udc_ep0_set_stall(ep);
			dev->ep0state = WAIT_FOR_SETUP;

			DEBUG_SETUP("\tdev->driver->setup failed (%d),"
				"bRequest = %d\n",
				i, usb_ctrl->bRequest);
		} else if (dev->req_pending) {
			dev->req_pending = 0;
			DEBUG_SETUP("\tdev->req_pending...\n");
		}

		DEBUG_SETUP("\tep0state = %s\n", state_names[dev->ep0state]);
	}
}

/*
 * handle ep0 interrupt
 */
static void s3c_handle_ep0(struct s3c_udc *dev)
{
	if (dev->ep0state == WAIT_FOR_SETUP) {
		DEBUG_OUT_EP("%s: WAIT_FOR_SETUP\n", __func__);
		s3c_ep0_setup(dev);

	} else {
		DEBUG_OUT_EP("%s: strange state!!(state = %s)\n",
			__func__, state_names[dev->ep0state]);
	}
}

static void s3c_ep0_kick(struct s3c_udc *dev, struct s3c_ep *ep)
{
	DEBUG_EP0("%s: ep_is_in = %d\n", __func__, ep_is_in(ep));
	if (ep_is_in(ep)) {
		dev->ep0state = DATA_STATE_XMIT;
		s3c_ep0_write(dev);

	} else {
		dev->ep0state = DATA_STATE_RECV;
		s3c_ep0_read(dev);
	}
}
