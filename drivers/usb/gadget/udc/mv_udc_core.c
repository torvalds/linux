// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2011 Marvell International Ltd. All rights reserved.
 * Author: Chao Xie <chao.xie@marvell.com>
 *	   Neil Zhang <zhangwm@marvell.com>
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/usb/otg.h>
#include <linux/pm.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/platform_data/mv_usb.h>
#include <linux/unaligned.h>

#include "mv_udc.h"

#define DRIVER_DESC		"Marvell PXA USB Device Controller driver"

#define ep_dir(ep)	(((ep)->ep_num == 0) ? \
				((ep)->udc->ep0_dir) : ((ep)->direction))

/* timeout value -- usec */
#define RESET_TIMEOUT		10000
#define FLUSH_TIMEOUT		10000
#define EPSTATUS_TIMEOUT	10000
#define PRIME_TIMEOUT		10000
#define READSAFE_TIMEOUT	1000

#define LOOPS_USEC_SHIFT	1
#define LOOPS_USEC		(1 << LOOPS_USEC_SHIFT)
#define LOOPS(timeout)		((timeout) >> LOOPS_USEC_SHIFT)

static DECLARE_COMPLETION(release_done);

static const char driver_name[] = "mv_udc";

static void nuke(struct mv_ep *ep, int status);
static void stop_activity(struct mv_udc *udc, struct usb_gadget_driver *driver);

/* for endpoint 0 operations */
static const struct usb_endpoint_descriptor mv_ep0_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	0,
	.bmAttributes =		USB_ENDPOINT_XFER_CONTROL,
	.wMaxPacketSize =	EP0_MAX_PKT_SIZE,
};

static void ep0_reset(struct mv_udc *udc)
{
	struct mv_ep *ep;
	u32 epctrlx;
	int i = 0;

	/* ep0 in and out */
	for (i = 0; i < 2; i++) {
		ep = &udc->eps[i];
		ep->udc = udc;

		/* ep0 dQH */
		ep->dqh = &udc->ep_dqh[i];

		/* configure ep0 endpoint capabilities in dQH */
		ep->dqh->max_packet_length =
			(EP0_MAX_PKT_SIZE << EP_QUEUE_HEAD_MAX_PKT_LEN_POS)
			| EP_QUEUE_HEAD_IOS;

		ep->dqh->next_dtd_ptr = EP_QUEUE_HEAD_NEXT_TERMINATE;

		epctrlx = readl(&udc->op_regs->epctrlx[0]);
		if (i) {	/* TX */
			epctrlx |= EPCTRL_TX_ENABLE
				| (USB_ENDPOINT_XFER_CONTROL
					<< EPCTRL_TX_EP_TYPE_SHIFT);

		} else {	/* RX */
			epctrlx |= EPCTRL_RX_ENABLE
				| (USB_ENDPOINT_XFER_CONTROL
					<< EPCTRL_RX_EP_TYPE_SHIFT);
		}

		writel(epctrlx, &udc->op_regs->epctrlx[0]);
	}
}

/* protocol ep0 stall, will automatically be cleared on new transaction */
static void ep0_stall(struct mv_udc *udc)
{
	u32	epctrlx;

	/* set TX and RX to stall */
	epctrlx = readl(&udc->op_regs->epctrlx[0]);
	epctrlx |= EPCTRL_RX_EP_STALL | EPCTRL_TX_EP_STALL;
	writel(epctrlx, &udc->op_regs->epctrlx[0]);

	/* update ep0 state */
	udc->ep0_state = WAIT_FOR_SETUP;
	udc->ep0_dir = EP_DIR_OUT;
}

static int process_ep_req(struct mv_udc *udc, int index,
	struct mv_req *curr_req)
{
	struct mv_dtd	*curr_dtd;
	struct mv_dqh	*curr_dqh;
	int actual, remaining_length;
	int i, direction;
	int retval = 0;
	u32 errors;
	u32 bit_pos;

	curr_dqh = &udc->ep_dqh[index];
	direction = index % 2;

	curr_dtd = curr_req->head;
	actual = curr_req->req.length;

	for (i = 0; i < curr_req->dtd_count; i++) {
		if (curr_dtd->size_ioc_sts & DTD_STATUS_ACTIVE) {
			dev_dbg(&udc->dev->dev, "%s, dTD not completed\n",
				udc->eps[index].name);
			return 1;
		}

		errors = curr_dtd->size_ioc_sts & DTD_ERROR_MASK;
		if (!errors) {
			remaining_length =
				(curr_dtd->size_ioc_sts	& DTD_PACKET_SIZE)
					>> DTD_LENGTH_BIT_POS;
			actual -= remaining_length;

			if (remaining_length) {
				if (direction) {
					dev_dbg(&udc->dev->dev,
						"TX dTD remains data\n");
					retval = -EPROTO;
					break;
				} else
					break;
			}
		} else {
			dev_info(&udc->dev->dev,
				"complete_tr error: ep=%d %s: error = 0x%x\n",
				index >> 1, direction ? "SEND" : "RECV",
				errors);
			if (errors & DTD_STATUS_HALTED) {
				/* Clear the errors and Halt condition */
				curr_dqh->size_ioc_int_sts &= ~errors;
				retval = -EPIPE;
			} else if (errors & DTD_STATUS_DATA_BUFF_ERR) {
				retval = -EPROTO;
			} else if (errors & DTD_STATUS_TRANSACTION_ERR) {
				retval = -EILSEQ;
			}
		}
		if (i != curr_req->dtd_count - 1)
			curr_dtd = (struct mv_dtd *)curr_dtd->next_dtd_virt;
	}
	if (retval)
		return retval;

	if (direction == EP_DIR_OUT)
		bit_pos = 1 << curr_req->ep->ep_num;
	else
		bit_pos = 1 << (16 + curr_req->ep->ep_num);

	while (curr_dqh->curr_dtd_ptr == curr_dtd->td_dma) {
		if (curr_dtd->dtd_next == EP_QUEUE_HEAD_NEXT_TERMINATE) {
			while (readl(&udc->op_regs->epstatus) & bit_pos)
				udelay(1);
			break;
		}
		udelay(1);
	}

	curr_req->req.actual = actual;

	return 0;
}

/*
 * done() - retire a request; caller blocked irqs
 * @status : request status to be set, only works when
 * request is still in progress.
 */
static void done(struct mv_ep *ep, struct mv_req *req, int status)
	__releases(&ep->udc->lock)
	__acquires(&ep->udc->lock)
{
	struct mv_udc *udc = NULL;
	unsigned char stopped = ep->stopped;
	struct mv_dtd *curr_td, *next_td;
	int j;

	udc = (struct mv_udc *)ep->udc;
	/* Removed the req from fsl_ep->queue */
	list_del_init(&req->queue);

	/* req.status should be set as -EINPROGRESS in ep_queue() */
	if (req->req.status == -EINPROGRESS)
		req->req.status = status;
	else
		status = req->req.status;

	/* Free dtd for the request */
	next_td = req->head;
	for (j = 0; j < req->dtd_count; j++) {
		curr_td = next_td;
		if (j != req->dtd_count - 1)
			next_td = curr_td->next_dtd_virt;
		dma_pool_free(udc->dtd_pool, curr_td, curr_td->td_dma);
	}

	usb_gadget_unmap_request(&udc->gadget, &req->req, ep_dir(ep));

	if (status && (status != -ESHUTDOWN))
		dev_info(&udc->dev->dev, "complete %s req %p stat %d len %u/%u",
			ep->ep.name, &req->req, status,
			req->req.actual, req->req.length);

	ep->stopped = 1;

	spin_unlock(&ep->udc->lock);

	usb_gadget_giveback_request(&ep->ep, &req->req);

	spin_lock(&ep->udc->lock);
	ep->stopped = stopped;
}

static int queue_dtd(struct mv_ep *ep, struct mv_req *req)
{
	struct mv_udc *udc;
	struct mv_dqh *dqh;
	u32 bit_pos, direction;
	u32 usbcmd, epstatus;
	unsigned int loops;
	int retval = 0;

	udc = ep->udc;
	direction = ep_dir(ep);
	dqh = &(udc->ep_dqh[ep->ep_num * 2 + direction]);
	bit_pos = 1 << (((direction == EP_DIR_OUT) ? 0 : 16) + ep->ep_num);

	/* check if the pipe is empty */
	if (!(list_empty(&ep->queue))) {
		struct mv_req *lastreq;
		lastreq = list_entry(ep->queue.prev, struct mv_req, queue);
		lastreq->tail->dtd_next =
			req->head->td_dma & EP_QUEUE_HEAD_NEXT_POINTER_MASK;

		wmb();

		if (readl(&udc->op_regs->epprime) & bit_pos)
			goto done;

		loops = LOOPS(READSAFE_TIMEOUT);
		while (1) {
			/* start with setting the semaphores */
			usbcmd = readl(&udc->op_regs->usbcmd);
			usbcmd |= USBCMD_ATDTW_TRIPWIRE_SET;
			writel(usbcmd, &udc->op_regs->usbcmd);

			/* read the endpoint status */
			epstatus = readl(&udc->op_regs->epstatus) & bit_pos;

			/*
			 * Reread the ATDTW semaphore bit to check if it is
			 * cleared. When hardware see a hazard, it will clear
			 * the bit or else we remain set to 1 and we can
			 * proceed with priming of endpoint if not already
			 * primed.
			 */
			if (readl(&udc->op_regs->usbcmd)
				& USBCMD_ATDTW_TRIPWIRE_SET)
				break;

			loops--;
			if (loops == 0) {
				dev_err(&udc->dev->dev,
					"Timeout for ATDTW_TRIPWIRE...\n");
				retval = -ETIME;
				goto done;
			}
			udelay(LOOPS_USEC);
		}

		/* Clear the semaphore */
		usbcmd = readl(&udc->op_regs->usbcmd);
		usbcmd &= USBCMD_ATDTW_TRIPWIRE_CLEAR;
		writel(usbcmd, &udc->op_regs->usbcmd);

		if (epstatus)
			goto done;
	}

	/* Write dQH next pointer and terminate bit to 0 */
	dqh->next_dtd_ptr = req->head->td_dma
				& EP_QUEUE_HEAD_NEXT_POINTER_MASK;

	/* clear active and halt bit, in case set from a previous error */
	dqh->size_ioc_int_sts &= ~(DTD_STATUS_ACTIVE | DTD_STATUS_HALTED);

	/* Ensure that updates to the QH will occur before priming. */
	wmb();

	/* Prime the Endpoint */
	writel(bit_pos, &udc->op_regs->epprime);

done:
	return retval;
}

static struct mv_dtd *build_dtd(struct mv_req *req, unsigned *length,
		dma_addr_t *dma, int *is_last)
{
	struct mv_dtd *dtd;
	struct mv_udc *udc;
	struct mv_dqh *dqh;
	u32 temp, mult = 0;

	/* how big will this transfer be? */
	if (usb_endpoint_xfer_isoc(req->ep->ep.desc)) {
		dqh = req->ep->dqh;
		mult = (dqh->max_packet_length >> EP_QUEUE_HEAD_MULT_POS)
				& 0x3;
		*length = min(req->req.length - req->req.actual,
				(unsigned)(mult * req->ep->ep.maxpacket));
	} else
		*length = min(req->req.length - req->req.actual,
				(unsigned)EP_MAX_LENGTH_TRANSFER);

	udc = req->ep->udc;

	/*
	 * Be careful that no _GFP_HIGHMEM is set,
	 * or we can not use dma_to_virt
	 */
	dtd = dma_pool_alloc(udc->dtd_pool, GFP_ATOMIC, dma);
	if (dtd == NULL)
		return dtd;

	dtd->td_dma = *dma;
	/* initialize buffer page pointers */
	temp = (u32)(req->req.dma + req->req.actual);
	dtd->buff_ptr0 = cpu_to_le32(temp);
	temp &= ~0xFFF;
	dtd->buff_ptr1 = cpu_to_le32(temp + 0x1000);
	dtd->buff_ptr2 = cpu_to_le32(temp + 0x2000);
	dtd->buff_ptr3 = cpu_to_le32(temp + 0x3000);
	dtd->buff_ptr4 = cpu_to_le32(temp + 0x4000);

	req->req.actual += *length;

	/* zlp is needed if req->req.zero is set */
	if (req->req.zero) {
		if (*length == 0 || (*length % req->ep->ep.maxpacket) != 0)
			*is_last = 1;
		else
			*is_last = 0;
	} else if (req->req.length == req->req.actual)
		*is_last = 1;
	else
		*is_last = 0;

	/* Fill in the transfer size; set active bit */
	temp = ((*length << DTD_LENGTH_BIT_POS) | DTD_STATUS_ACTIVE);

	/* Enable interrupt for the last dtd of a request */
	if (*is_last && !req->req.no_interrupt)
		temp |= DTD_IOC;

	temp |= mult << 10;

	dtd->size_ioc_sts = temp;

	mb();

	return dtd;
}

/* generate dTD linked list for a request */
static int req_to_dtd(struct mv_req *req)
{
	unsigned count;
	int is_last, is_first = 1;
	struct mv_dtd *dtd, *last_dtd = NULL;
	dma_addr_t dma;

	do {
		dtd = build_dtd(req, &count, &dma, &is_last);
		if (dtd == NULL)
			return -ENOMEM;

		if (is_first) {
			is_first = 0;
			req->head = dtd;
		} else {
			last_dtd->dtd_next = dma;
			last_dtd->next_dtd_virt = dtd;
		}
		last_dtd = dtd;
		req->dtd_count++;
	} while (!is_last);

	/* set terminate bit to 1 for the last dTD */
	dtd->dtd_next = DTD_NEXT_TERMINATE;

	req->tail = dtd;

	return 0;
}

static int mv_ep_enable(struct usb_ep *_ep,
		const struct usb_endpoint_descriptor *desc)
{
	struct mv_udc *udc;
	struct mv_ep *ep;
	struct mv_dqh *dqh;
	u16 max = 0;
	u32 bit_pos, epctrlx, direction;
	const unsigned char zlt = 1;
	unsigned char ios, mult;
	unsigned long flags;

	ep = container_of(_ep, struct mv_ep, ep);
	udc = ep->udc;

	if (!_ep || !desc
			|| desc->bDescriptorType != USB_DT_ENDPOINT)
		return -EINVAL;

	if (!udc->driver || udc->gadget.speed == USB_SPEED_UNKNOWN)
		return -ESHUTDOWN;

	direction = ep_dir(ep);
	max = usb_endpoint_maxp(desc);

	/*
	 * disable HW zero length termination select
	 * driver handles zero length packet through req->req.zero
	 */
	bit_pos = 1 << ((direction == EP_DIR_OUT ? 0 : 16) + ep->ep_num);

	/* Check if the Endpoint is Primed */
	if ((readl(&udc->op_regs->epprime) & bit_pos)
		|| (readl(&udc->op_regs->epstatus) & bit_pos)) {
		dev_info(&udc->dev->dev,
			"ep=%d %s: Init ERROR: ENDPTPRIME=0x%x,"
			" ENDPTSTATUS=0x%x, bit_pos=0x%x\n",
			(unsigned)ep->ep_num, direction ? "SEND" : "RECV",
			(unsigned)readl(&udc->op_regs->epprime),
			(unsigned)readl(&udc->op_regs->epstatus),
			(unsigned)bit_pos);
		goto en_done;
	}

	/* Set the max packet length, interrupt on Setup and Mult fields */
	ios = 0;
	mult = 0;
	switch (desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) {
	case USB_ENDPOINT_XFER_BULK:
	case USB_ENDPOINT_XFER_INT:
		break;
	case USB_ENDPOINT_XFER_CONTROL:
		ios = 1;
		break;
	case USB_ENDPOINT_XFER_ISOC:
		/* Calculate transactions needed for high bandwidth iso */
		mult = usb_endpoint_maxp_mult(desc);
		/* 3 transactions at most */
		if (mult > 3)
			goto en_done;
		break;
	default:
		goto en_done;
	}

	spin_lock_irqsave(&udc->lock, flags);
	/* Get the endpoint queue head address */
	dqh = ep->dqh;
	dqh->max_packet_length = (max << EP_QUEUE_HEAD_MAX_PKT_LEN_POS)
		| (mult << EP_QUEUE_HEAD_MULT_POS)
		| (zlt ? EP_QUEUE_HEAD_ZLT_SEL : 0)
		| (ios ? EP_QUEUE_HEAD_IOS : 0);
	dqh->next_dtd_ptr = 1;
	dqh->size_ioc_int_sts = 0;

	ep->ep.maxpacket = max;
	ep->ep.desc = desc;
	ep->stopped = 0;

	/* Enable the endpoint for Rx or Tx and set the endpoint type */
	epctrlx = readl(&udc->op_regs->epctrlx[ep->ep_num]);
	if (direction == EP_DIR_IN) {
		epctrlx &= ~EPCTRL_TX_ALL_MASK;
		epctrlx |= EPCTRL_TX_ENABLE | EPCTRL_TX_DATA_TOGGLE_RST
			| ((desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
				<< EPCTRL_TX_EP_TYPE_SHIFT);
	} else {
		epctrlx &= ~EPCTRL_RX_ALL_MASK;
		epctrlx |= EPCTRL_RX_ENABLE | EPCTRL_RX_DATA_TOGGLE_RST
			| ((desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
				<< EPCTRL_RX_EP_TYPE_SHIFT);
	}
	writel(epctrlx, &udc->op_regs->epctrlx[ep->ep_num]);

	/*
	 * Implement Guideline (GL# USB-7) The unused endpoint type must
	 * be programmed to bulk.
	 */
	epctrlx = readl(&udc->op_regs->epctrlx[ep->ep_num]);
	if ((epctrlx & EPCTRL_RX_ENABLE) == 0) {
		epctrlx |= (USB_ENDPOINT_XFER_BULK
				<< EPCTRL_RX_EP_TYPE_SHIFT);
		writel(epctrlx, &udc->op_regs->epctrlx[ep->ep_num]);
	}

	epctrlx = readl(&udc->op_regs->epctrlx[ep->ep_num]);
	if ((epctrlx & EPCTRL_TX_ENABLE) == 0) {
		epctrlx |= (USB_ENDPOINT_XFER_BULK
				<< EPCTRL_TX_EP_TYPE_SHIFT);
		writel(epctrlx, &udc->op_regs->epctrlx[ep->ep_num]);
	}

	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;
en_done:
	return -EINVAL;
}

static int  mv_ep_disable(struct usb_ep *_ep)
{
	struct mv_udc *udc;
	struct mv_ep *ep;
	struct mv_dqh *dqh;
	u32 epctrlx, direction;
	unsigned long flags;

	ep = container_of(_ep, struct mv_ep, ep);
	if ((_ep == NULL) || !ep->ep.desc)
		return -EINVAL;

	udc = ep->udc;

	/* Get the endpoint queue head address */
	dqh = ep->dqh;

	spin_lock_irqsave(&udc->lock, flags);

	direction = ep_dir(ep);

	/* Reset the max packet length and the interrupt on Setup */
	dqh->max_packet_length = 0;

	/* Disable the endpoint for Rx or Tx and reset the endpoint type */
	epctrlx = readl(&udc->op_regs->epctrlx[ep->ep_num]);
	epctrlx &= ~((direction == EP_DIR_IN)
			? (EPCTRL_TX_ENABLE | EPCTRL_TX_TYPE)
			: (EPCTRL_RX_ENABLE | EPCTRL_RX_TYPE));
	writel(epctrlx, &udc->op_regs->epctrlx[ep->ep_num]);

	/* nuke all pending requests (does flush) */
	nuke(ep, -ESHUTDOWN);

	ep->ep.desc = NULL;
	ep->stopped = 1;

	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;
}

static struct usb_request *
mv_alloc_request(struct usb_ep *_ep, gfp_t gfp_flags)
{
	struct mv_req *req;

	req = kzalloc(sizeof *req, gfp_flags);
	if (!req)
		return NULL;

	req->req.dma = DMA_ADDR_INVALID;
	INIT_LIST_HEAD(&req->queue);

	return &req->req;
}

static void mv_free_request(struct usb_ep *_ep, struct usb_request *_req)
{
	struct mv_req *req = NULL;

	req = container_of(_req, struct mv_req, req);

	if (_req)
		kfree(req);
}

static void mv_ep_fifo_flush(struct usb_ep *_ep)
{
	struct mv_udc *udc;
	u32 bit_pos, direction;
	struct mv_ep *ep;
	unsigned int loops;

	if (!_ep)
		return;

	ep = container_of(_ep, struct mv_ep, ep);
	if (!ep->ep.desc)
		return;

	udc = ep->udc;
	direction = ep_dir(ep);

	if (ep->ep_num == 0)
		bit_pos = (1 << 16) | 1;
	else if (direction == EP_DIR_OUT)
		bit_pos = 1 << ep->ep_num;
	else
		bit_pos = 1 << (16 + ep->ep_num);

	loops = LOOPS(EPSTATUS_TIMEOUT);
	do {
		unsigned int inter_loops;

		if (loops == 0) {
			dev_err(&udc->dev->dev,
				"TIMEOUT for ENDPTSTATUS=0x%x, bit_pos=0x%x\n",
				(unsigned)readl(&udc->op_regs->epstatus),
				(unsigned)bit_pos);
			return;
		}
		/* Write 1 to the Flush register */
		writel(bit_pos, &udc->op_regs->epflush);

		/* Wait until flushing completed */
		inter_loops = LOOPS(FLUSH_TIMEOUT);
		while (readl(&udc->op_regs->epflush)) {
			/*
			 * ENDPTFLUSH bit should be cleared to indicate this
			 * operation is complete
			 */
			if (inter_loops == 0) {
				dev_err(&udc->dev->dev,
					"TIMEOUT for ENDPTFLUSH=0x%x,"
					"bit_pos=0x%x\n",
					(unsigned)readl(&udc->op_regs->epflush),
					(unsigned)bit_pos);
				return;
			}
			inter_loops--;
			udelay(LOOPS_USEC);
		}
		loops--;
	} while (readl(&udc->op_regs->epstatus) & bit_pos);
}

/* queues (submits) an I/O request to an endpoint */
static int
mv_ep_queue(struct usb_ep *_ep, struct usb_request *_req, gfp_t gfp_flags)
{
	struct mv_ep *ep = container_of(_ep, struct mv_ep, ep);
	struct mv_req *req = container_of(_req, struct mv_req, req);
	struct mv_udc *udc = ep->udc;
	unsigned long flags;
	int retval;

	/* catch various bogus parameters */
	if (!_req || !req->req.complete || !req->req.buf
			|| !list_empty(&req->queue)) {
		dev_err(&udc->dev->dev, "%s, bad params", __func__);
		return -EINVAL;
	}
	if (unlikely(!_ep || !ep->ep.desc)) {
		dev_err(&udc->dev->dev, "%s, bad ep", __func__);
		return -EINVAL;
	}

	udc = ep->udc;
	if (!udc->driver || udc->gadget.speed == USB_SPEED_UNKNOWN)
		return -ESHUTDOWN;

	req->ep = ep;

	/* map virtual address to hardware */
	retval = usb_gadget_map_request(&udc->gadget, _req, ep_dir(ep));
	if (retval)
		return retval;

	req->req.status = -EINPROGRESS;
	req->req.actual = 0;
	req->dtd_count = 0;

	spin_lock_irqsave(&udc->lock, flags);

	/* build dtds and push them to device queue */
	if (!req_to_dtd(req)) {
		retval = queue_dtd(ep, req);
		if (retval) {
			spin_unlock_irqrestore(&udc->lock, flags);
			dev_err(&udc->dev->dev, "Failed to queue dtd\n");
			goto err_unmap_dma;
		}
	} else {
		spin_unlock_irqrestore(&udc->lock, flags);
		dev_err(&udc->dev->dev, "Failed to dma_pool_alloc\n");
		retval = -ENOMEM;
		goto err_unmap_dma;
	}

	/* Update ep0 state */
	if (ep->ep_num == 0)
		udc->ep0_state = DATA_STATE_XMIT;

	/* irq handler advances the queue */
	list_add_tail(&req->queue, &ep->queue);
	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;

err_unmap_dma:
	usb_gadget_unmap_request(&udc->gadget, _req, ep_dir(ep));

	return retval;
}

static void mv_prime_ep(struct mv_ep *ep, struct mv_req *req)
{
	struct mv_dqh *dqh = ep->dqh;
	u32 bit_pos;

	/* Write dQH next pointer and terminate bit to 0 */
	dqh->next_dtd_ptr = req->head->td_dma
		& EP_QUEUE_HEAD_NEXT_POINTER_MASK;

	/* clear active and halt bit, in case set from a previous error */
	dqh->size_ioc_int_sts &= ~(DTD_STATUS_ACTIVE | DTD_STATUS_HALTED);

	/* Ensure that updates to the QH will occure before priming. */
	wmb();

	bit_pos = 1 << (((ep_dir(ep) == EP_DIR_OUT) ? 0 : 16) + ep->ep_num);

	/* Prime the Endpoint */
	writel(bit_pos, &ep->udc->op_regs->epprime);
}

/* dequeues (cancels, unlinks) an I/O request from an endpoint */
static int mv_ep_dequeue(struct usb_ep *_ep, struct usb_request *_req)
{
	struct mv_ep *ep = container_of(_ep, struct mv_ep, ep);
	struct mv_req *req = NULL, *iter;
	struct mv_udc *udc = ep->udc;
	unsigned long flags;
	int stopped, ret = 0;
	u32 epctrlx;

	if (!_ep || !_req)
		return -EINVAL;

	spin_lock_irqsave(&ep->udc->lock, flags);
	stopped = ep->stopped;

	/* Stop the ep before we deal with the queue */
	ep->stopped = 1;
	epctrlx = readl(&udc->op_regs->epctrlx[ep->ep_num]);
	if (ep_dir(ep) == EP_DIR_IN)
		epctrlx &= ~EPCTRL_TX_ENABLE;
	else
		epctrlx &= ~EPCTRL_RX_ENABLE;
	writel(epctrlx, &udc->op_regs->epctrlx[ep->ep_num]);

	/* make sure it's actually queued on this endpoint */
	list_for_each_entry(iter, &ep->queue, queue) {
		if (&iter->req != _req)
			continue;
		req = iter;
		break;
	}
	if (!req) {
		ret = -EINVAL;
		goto out;
	}

	/* The request is in progress, or completed but not dequeued */
	if (ep->queue.next == &req->queue) {
		_req->status = -ECONNRESET;
		mv_ep_fifo_flush(_ep);	/* flush current transfer */

		/* The request isn't the last request in this ep queue */
		if (req->queue.next != &ep->queue) {
			struct mv_req *next_req;

			next_req = list_entry(req->queue.next,
				struct mv_req, queue);

			/* Point the QH to the first TD of next request */
			mv_prime_ep(ep, next_req);
		} else {
			struct mv_dqh *qh;

			qh = ep->dqh;
			qh->next_dtd_ptr = 1;
			qh->size_ioc_int_sts = 0;
		}

		/* The request hasn't been processed, patch up the TD chain */
	} else {
		struct mv_req *prev_req;

		prev_req = list_entry(req->queue.prev, struct mv_req, queue);
		writel(readl(&req->tail->dtd_next),
				&prev_req->tail->dtd_next);

	}

	done(ep, req, -ECONNRESET);

	/* Enable EP */
out:
	epctrlx = readl(&udc->op_regs->epctrlx[ep->ep_num]);
	if (ep_dir(ep) == EP_DIR_IN)
		epctrlx |= EPCTRL_TX_ENABLE;
	else
		epctrlx |= EPCTRL_RX_ENABLE;
	writel(epctrlx, &udc->op_regs->epctrlx[ep->ep_num]);
	ep->stopped = stopped;

	spin_unlock_irqrestore(&ep->udc->lock, flags);
	return ret;
}

static void ep_set_stall(struct mv_udc *udc, u8 ep_num, u8 direction, int stall)
{
	u32 epctrlx;

	epctrlx = readl(&udc->op_regs->epctrlx[ep_num]);

	if (stall) {
		if (direction == EP_DIR_IN)
			epctrlx |= EPCTRL_TX_EP_STALL;
		else
			epctrlx |= EPCTRL_RX_EP_STALL;
	} else {
		if (direction == EP_DIR_IN) {
			epctrlx &= ~EPCTRL_TX_EP_STALL;
			epctrlx |= EPCTRL_TX_DATA_TOGGLE_RST;
		} else {
			epctrlx &= ~EPCTRL_RX_EP_STALL;
			epctrlx |= EPCTRL_RX_DATA_TOGGLE_RST;
		}
	}
	writel(epctrlx, &udc->op_regs->epctrlx[ep_num]);
}

static int ep_is_stall(struct mv_udc *udc, u8 ep_num, u8 direction)
{
	u32 epctrlx;

	epctrlx = readl(&udc->op_regs->epctrlx[ep_num]);

	if (direction == EP_DIR_OUT)
		return (epctrlx & EPCTRL_RX_EP_STALL) ? 1 : 0;
	else
		return (epctrlx & EPCTRL_TX_EP_STALL) ? 1 : 0;
}

static int mv_ep_set_halt_wedge(struct usb_ep *_ep, int halt, int wedge)
{
	struct mv_ep *ep;
	unsigned long flags;
	int status = 0;
	struct mv_udc *udc;

	ep = container_of(_ep, struct mv_ep, ep);
	udc = ep->udc;
	if (!_ep || !ep->ep.desc) {
		status = -EINVAL;
		goto out;
	}

	if (ep->ep.desc->bmAttributes == USB_ENDPOINT_XFER_ISOC) {
		status = -EOPNOTSUPP;
		goto out;
	}

	/*
	 * Attempt to halt IN ep will fail if any transfer requests
	 * are still queue
	 */
	if (halt && (ep_dir(ep) == EP_DIR_IN) && !list_empty(&ep->queue)) {
		status = -EAGAIN;
		goto out;
	}

	spin_lock_irqsave(&ep->udc->lock, flags);
	ep_set_stall(udc, ep->ep_num, ep_dir(ep), halt);
	if (halt && wedge)
		ep->wedge = 1;
	else if (!halt)
		ep->wedge = 0;
	spin_unlock_irqrestore(&ep->udc->lock, flags);

	if (ep->ep_num == 0) {
		udc->ep0_state = WAIT_FOR_SETUP;
		udc->ep0_dir = EP_DIR_OUT;
	}
out:
	return status;
}

static int mv_ep_set_halt(struct usb_ep *_ep, int halt)
{
	return mv_ep_set_halt_wedge(_ep, halt, 0);
}

static int mv_ep_set_wedge(struct usb_ep *_ep)
{
	return mv_ep_set_halt_wedge(_ep, 1, 1);
}

static const struct usb_ep_ops mv_ep_ops = {
	.enable		= mv_ep_enable,
	.disable	= mv_ep_disable,

	.alloc_request	= mv_alloc_request,
	.free_request	= mv_free_request,

	.queue		= mv_ep_queue,
	.dequeue	= mv_ep_dequeue,

	.set_wedge	= mv_ep_set_wedge,
	.set_halt	= mv_ep_set_halt,
	.fifo_flush	= mv_ep_fifo_flush,	/* flush fifo */
};

static int udc_clock_enable(struct mv_udc *udc)
{
	return clk_prepare_enable(udc->clk);
}

static void udc_clock_disable(struct mv_udc *udc)
{
	clk_disable_unprepare(udc->clk);
}

static void udc_stop(struct mv_udc *udc)
{
	u32 tmp;

	/* Disable interrupts */
	tmp = readl(&udc->op_regs->usbintr);
	tmp &= ~(USBINTR_INT_EN | USBINTR_ERR_INT_EN |
		USBINTR_PORT_CHANGE_DETECT_EN | USBINTR_RESET_EN);
	writel(tmp, &udc->op_regs->usbintr);

	udc->stopped = 1;

	/* Reset the Run the bit in the command register to stop VUSB */
	tmp = readl(&udc->op_regs->usbcmd);
	tmp &= ~USBCMD_RUN_STOP;
	writel(tmp, &udc->op_regs->usbcmd);
}

static void udc_start(struct mv_udc *udc)
{
	u32 usbintr;

	usbintr = USBINTR_INT_EN | USBINTR_ERR_INT_EN
		| USBINTR_PORT_CHANGE_DETECT_EN
		| USBINTR_RESET_EN | USBINTR_DEVICE_SUSPEND;
	/* Enable interrupts */
	writel(usbintr, &udc->op_regs->usbintr);

	udc->stopped = 0;

	/* Set the Run bit in the command register */
	writel(USBCMD_RUN_STOP, &udc->op_regs->usbcmd);
}

static int udc_reset(struct mv_udc *udc)
{
	unsigned int loops;
	u32 tmp, portsc;

	/* Stop the controller */
	tmp = readl(&udc->op_regs->usbcmd);
	tmp &= ~USBCMD_RUN_STOP;
	writel(tmp, &udc->op_regs->usbcmd);

	/* Reset the controller to get default values */
	writel(USBCMD_CTRL_RESET, &udc->op_regs->usbcmd);

	/* wait for reset to complete */
	loops = LOOPS(RESET_TIMEOUT);
	while (readl(&udc->op_regs->usbcmd) & USBCMD_CTRL_RESET) {
		if (loops == 0) {
			dev_err(&udc->dev->dev,
				"Wait for RESET completed TIMEOUT\n");
			return -ETIMEDOUT;
		}
		loops--;
		udelay(LOOPS_USEC);
	}

	/* set controller to device mode */
	tmp = readl(&udc->op_regs->usbmode);
	tmp |= USBMODE_CTRL_MODE_DEVICE;

	/* turn setup lockout off, require setup tripwire in usbcmd */
	tmp |= USBMODE_SETUP_LOCK_OFF;

	writel(tmp, &udc->op_regs->usbmode);

	writel(0x0, &udc->op_regs->epsetupstat);

	/* Configure the Endpoint List Address */
	writel(udc->ep_dqh_dma & USB_EP_LIST_ADDRESS_MASK,
		&udc->op_regs->eplistaddr);

	portsc = readl(&udc->op_regs->portsc[0]);
	if (readl(&udc->cap_regs->hcsparams) & HCSPARAMS_PPC)
		portsc &= (~PORTSCX_W1C_BITS | ~PORTSCX_PORT_POWER);

	if (udc->force_fs)
		portsc |= PORTSCX_FORCE_FULL_SPEED_CONNECT;
	else
		portsc &= (~PORTSCX_FORCE_FULL_SPEED_CONNECT);

	writel(portsc, &udc->op_regs->portsc[0]);

	tmp = readl(&udc->op_regs->epctrlx[0]);
	tmp &= ~(EPCTRL_TX_EP_STALL | EPCTRL_RX_EP_STALL);
	writel(tmp, &udc->op_regs->epctrlx[0]);

	return 0;
}

static int mv_udc_enable_internal(struct mv_udc *udc)
{
	int retval;

	if (udc->active)
		return 0;

	dev_dbg(&udc->dev->dev, "enable udc\n");
	retval = udc_clock_enable(udc);
	if (retval)
		return retval;

	if (udc->pdata->phy_init) {
		retval = udc->pdata->phy_init(udc->phy_regs);
		if (retval) {
			dev_err(&udc->dev->dev,
				"init phy error %d\n", retval);
			udc_clock_disable(udc);
			return retval;
		}
	}
	udc->active = 1;

	return 0;
}

static int mv_udc_enable(struct mv_udc *udc)
{
	if (udc->clock_gating)
		return mv_udc_enable_internal(udc);

	return 0;
}

static void mv_udc_disable_internal(struct mv_udc *udc)
{
	if (udc->active) {
		dev_dbg(&udc->dev->dev, "disable udc\n");
		if (udc->pdata->phy_deinit)
			udc->pdata->phy_deinit(udc->phy_regs);
		udc_clock_disable(udc);
		udc->active = 0;
	}
}

static void mv_udc_disable(struct mv_udc *udc)
{
	if (udc->clock_gating)
		mv_udc_disable_internal(udc);
}

static int mv_udc_get_frame(struct usb_gadget *gadget)
{
	struct mv_udc *udc;
	u16	retval;

	if (!gadget)
		return -ENODEV;

	udc = container_of(gadget, struct mv_udc, gadget);

	retval = readl(&udc->op_regs->frindex) & USB_FRINDEX_MASKS;

	return retval;
}

/* Tries to wake up the host connected to this gadget */
static int mv_udc_wakeup(struct usb_gadget *gadget)
{
	struct mv_udc *udc = container_of(gadget, struct mv_udc, gadget);
	u32 portsc;

	/* Remote wakeup feature not enabled by host */
	if (!udc->remote_wakeup)
		return -ENOTSUPP;

	portsc = readl(&udc->op_regs->portsc);
	/* not suspended? */
	if (!(portsc & PORTSCX_PORT_SUSPEND))
		return 0;
	/* trigger force resume */
	portsc |= PORTSCX_PORT_FORCE_RESUME;
	writel(portsc, &udc->op_regs->portsc[0]);
	return 0;
}

static int mv_udc_vbus_session(struct usb_gadget *gadget, int is_active)
{
	struct mv_udc *udc;
	unsigned long flags;
	int retval = 0;

	udc = container_of(gadget, struct mv_udc, gadget);
	spin_lock_irqsave(&udc->lock, flags);

	udc->vbus_active = (is_active != 0);

	dev_dbg(&udc->dev->dev, "%s: softconnect %d, vbus_active %d\n",
		__func__, udc->softconnect, udc->vbus_active);

	if (udc->driver && udc->softconnect && udc->vbus_active) {
		retval = mv_udc_enable(udc);
		if (retval == 0) {
			/* Clock is disabled, need re-init registers */
			udc_reset(udc);
			ep0_reset(udc);
			udc_start(udc);
		}
	} else if (udc->driver && udc->softconnect) {
		if (!udc->active)
			goto out;

		/* stop all the transfer in queue*/
		stop_activity(udc, udc->driver);
		udc_stop(udc);
		mv_udc_disable(udc);
	}

out:
	spin_unlock_irqrestore(&udc->lock, flags);
	return retval;
}

static int mv_udc_pullup(struct usb_gadget *gadget, int is_on)
{
	struct mv_udc *udc;
	unsigned long flags;
	int retval = 0;

	udc = container_of(gadget, struct mv_udc, gadget);
	spin_lock_irqsave(&udc->lock, flags);

	udc->softconnect = (is_on != 0);

	dev_dbg(&udc->dev->dev, "%s: softconnect %d, vbus_active %d\n",
			__func__, udc->softconnect, udc->vbus_active);

	if (udc->driver && udc->softconnect && udc->vbus_active) {
		retval = mv_udc_enable(udc);
		if (retval == 0) {
			/* Clock is disabled, need re-init registers */
			udc_reset(udc);
			ep0_reset(udc);
			udc_start(udc);
		}
	} else if (udc->driver && udc->vbus_active) {
		/* stop all the transfer in queue*/
		stop_activity(udc, udc->driver);
		udc_stop(udc);
		mv_udc_disable(udc);
	}

	spin_unlock_irqrestore(&udc->lock, flags);
	return retval;
}

static int mv_udc_start(struct usb_gadget *, struct usb_gadget_driver *);
static int mv_udc_stop(struct usb_gadget *);
/* device controller usb_gadget_ops structure */
static const struct usb_gadget_ops mv_ops = {

	/* returns the current frame number */
	.get_frame	= mv_udc_get_frame,

	/* tries to wake up the host connected to this gadget */
	.wakeup		= mv_udc_wakeup,

	/* notify controller that VBUS is powered or not */
	.vbus_session	= mv_udc_vbus_session,

	/* D+ pullup, software-controlled connect/disconnect to USB host */
	.pullup		= mv_udc_pullup,
	.udc_start	= mv_udc_start,
	.udc_stop	= mv_udc_stop,
};

static int eps_init(struct mv_udc *udc)
{
	struct mv_ep	*ep;
	char name[14];
	int i;

	/* initialize ep0 */
	ep = &udc->eps[0];
	ep->udc = udc;
	strncpy(ep->name, "ep0", sizeof(ep->name));
	ep->ep.name = ep->name;
	ep->ep.ops = &mv_ep_ops;
	ep->wedge = 0;
	ep->stopped = 0;
	usb_ep_set_maxpacket_limit(&ep->ep, EP0_MAX_PKT_SIZE);
	ep->ep.caps.type_control = true;
	ep->ep.caps.dir_in = true;
	ep->ep.caps.dir_out = true;
	ep->ep_num = 0;
	ep->ep.desc = &mv_ep0_desc;
	INIT_LIST_HEAD(&ep->queue);

	ep->ep_type = USB_ENDPOINT_XFER_CONTROL;

	/* initialize other endpoints */
	for (i = 2; i < udc->max_eps * 2; i++) {
		ep = &udc->eps[i];
		if (i % 2) {
			snprintf(name, sizeof(name), "ep%din", i / 2);
			ep->direction = EP_DIR_IN;
			ep->ep.caps.dir_in = true;
		} else {
			snprintf(name, sizeof(name), "ep%dout", i / 2);
			ep->direction = EP_DIR_OUT;
			ep->ep.caps.dir_out = true;
		}
		ep->udc = udc;
		strncpy(ep->name, name, sizeof(ep->name));
		ep->ep.name = ep->name;

		ep->ep.caps.type_iso = true;
		ep->ep.caps.type_bulk = true;
		ep->ep.caps.type_int = true;

		ep->ep.ops = &mv_ep_ops;
		ep->stopped = 0;
		usb_ep_set_maxpacket_limit(&ep->ep, (unsigned short) ~0);
		ep->ep_num = i / 2;

		INIT_LIST_HEAD(&ep->queue);
		list_add_tail(&ep->ep.ep_list, &udc->gadget.ep_list);

		ep->dqh = &udc->ep_dqh[i];
	}

	return 0;
}

/* delete all endpoint requests, called with spinlock held */
static void nuke(struct mv_ep *ep, int status)
{
	/* called with spinlock held */
	ep->stopped = 1;

	/* endpoint fifo flush */
	mv_ep_fifo_flush(&ep->ep);

	while (!list_empty(&ep->queue)) {
		struct mv_req *req = NULL;
		req = list_entry(ep->queue.next, struct mv_req, queue);
		done(ep, req, status);
	}
}

static void gadget_reset(struct mv_udc *udc, struct usb_gadget_driver *driver)
{
	struct mv_ep	*ep;

	nuke(&udc->eps[0], -ESHUTDOWN);

	list_for_each_entry(ep, &udc->gadget.ep_list, ep.ep_list) {
		nuke(ep, -ESHUTDOWN);
	}

	/* report reset; the driver is already quiesced */
	if (driver) {
		spin_unlock(&udc->lock);
		usb_gadget_udc_reset(&udc->gadget, driver);
		spin_lock(&udc->lock);
	}
}
/* stop all USB activities */
static void stop_activity(struct mv_udc *udc, struct usb_gadget_driver *driver)
{
	struct mv_ep	*ep;

	nuke(&udc->eps[0], -ESHUTDOWN);

	list_for_each_entry(ep, &udc->gadget.ep_list, ep.ep_list) {
		nuke(ep, -ESHUTDOWN);
	}

	/* report disconnect; the driver is already quiesced */
	if (driver) {
		spin_unlock(&udc->lock);
		driver->disconnect(&udc->gadget);
		spin_lock(&udc->lock);
	}
}

static int mv_udc_start(struct usb_gadget *gadget,
		struct usb_gadget_driver *driver)
{
	struct mv_udc *udc;
	int retval = 0;
	unsigned long flags;

	udc = container_of(gadget, struct mv_udc, gadget);

	if (udc->driver)
		return -EBUSY;

	spin_lock_irqsave(&udc->lock, flags);

	/* hook up the driver ... */
	udc->driver = driver;

	udc->usb_state = USB_STATE_ATTACHED;
	udc->ep0_state = WAIT_FOR_SETUP;
	udc->ep0_dir = EP_DIR_OUT;

	spin_unlock_irqrestore(&udc->lock, flags);

	if (udc->transceiver) {
		retval = otg_set_peripheral(udc->transceiver->otg,
					&udc->gadget);
		if (retval) {
			dev_err(&udc->dev->dev,
				"unable to register peripheral to otg\n");
			udc->driver = NULL;
			return retval;
		}
	}

	/* When boot with cable attached, there will be no vbus irq occurred */
	if (udc->qwork)
		queue_work(udc->qwork, &udc->vbus_work);

	return 0;
}

static int mv_udc_stop(struct usb_gadget *gadget)
{
	struct mv_udc *udc;
	unsigned long flags;

	udc = container_of(gadget, struct mv_udc, gadget);

	spin_lock_irqsave(&udc->lock, flags);

	mv_udc_enable(udc);
	udc_stop(udc);

	/* stop all usb activities */
	udc->gadget.speed = USB_SPEED_UNKNOWN;
	stop_activity(udc, NULL);
	mv_udc_disable(udc);

	spin_unlock_irqrestore(&udc->lock, flags);

	/* unbind gadget driver */
	udc->driver = NULL;

	return 0;
}

static void mv_set_ptc(struct mv_udc *udc, u32 mode)
{
	u32 portsc;

	portsc = readl(&udc->op_regs->portsc[0]);
	portsc |= mode << 16;
	writel(portsc, &udc->op_regs->portsc[0]);
}

static void prime_status_complete(struct usb_ep *ep, struct usb_request *_req)
{
	struct mv_ep *mvep = container_of(ep, struct mv_ep, ep);
	struct mv_req *req = container_of(_req, struct mv_req, req);
	struct mv_udc *udc;
	unsigned long flags;

	udc = mvep->udc;

	dev_info(&udc->dev->dev, "switch to test mode %d\n", req->test_mode);

	spin_lock_irqsave(&udc->lock, flags);
	if (req->test_mode) {
		mv_set_ptc(udc, req->test_mode);
		req->test_mode = 0;
	}
	spin_unlock_irqrestore(&udc->lock, flags);
}

static int
udc_prime_status(struct mv_udc *udc, u8 direction, u16 status, bool empty)
{
	int retval = 0;
	struct mv_req *req;
	struct mv_ep *ep;

	ep = &udc->eps[0];
	udc->ep0_dir = direction;
	udc->ep0_state = WAIT_FOR_OUT_STATUS;

	req = udc->status_req;

	/* fill in the request structure */
	if (empty == false) {
		*((u16 *) req->req.buf) = cpu_to_le16(status);
		req->req.length = 2;
	} else
		req->req.length = 0;

	req->ep = ep;
	req->req.status = -EINPROGRESS;
	req->req.actual = 0;
	if (udc->test_mode) {
		req->req.complete = prime_status_complete;
		req->test_mode = udc->test_mode;
		udc->test_mode = 0;
	} else
		req->req.complete = NULL;
	req->dtd_count = 0;

	if (req->req.dma == DMA_ADDR_INVALID) {
		req->req.dma = dma_map_single(ep->udc->gadget.dev.parent,
				req->req.buf, req->req.length,
				ep_dir(ep) ? DMA_TO_DEVICE : DMA_FROM_DEVICE);
		req->mapped = 1;
	}

	/* prime the data phase */
	if (!req_to_dtd(req)) {
		retval = queue_dtd(ep, req);
		if (retval) {
			dev_err(&udc->dev->dev,
				"Failed to queue dtd when prime status\n");
			goto out;
		}
	} else{	/* no mem */
		retval = -ENOMEM;
		dev_err(&udc->dev->dev,
			"Failed to dma_pool_alloc when prime status\n");
		goto out;
	}

	list_add_tail(&req->queue, &ep->queue);

	return 0;
out:
	usb_gadget_unmap_request(&udc->gadget, &req->req, ep_dir(ep));

	return retval;
}

static void mv_udc_testmode(struct mv_udc *udc, u16 index)
{
	if (index <= USB_TEST_FORCE_ENABLE) {
		udc->test_mode = index;
		if (udc_prime_status(udc, EP_DIR_IN, 0, true))
			ep0_stall(udc);
	} else
		dev_err(&udc->dev->dev,
			"This test mode(%d) is not supported\n", index);
}

static void ch9setaddress(struct mv_udc *udc, struct usb_ctrlrequest *setup)
{
	udc->dev_addr = (u8)setup->wValue;

	/* update usb state */
	udc->usb_state = USB_STATE_ADDRESS;

	if (udc_prime_status(udc, EP_DIR_IN, 0, true))
		ep0_stall(udc);
}

static void ch9getstatus(struct mv_udc *udc, u8 ep_num,
	struct usb_ctrlrequest *setup)
{
	u16 status = 0;
	int retval;

	if ((setup->bRequestType & (USB_DIR_IN | USB_TYPE_MASK))
		!= (USB_DIR_IN | USB_TYPE_STANDARD))
		return;

	if ((setup->bRequestType & USB_RECIP_MASK) == USB_RECIP_DEVICE) {
		status = 1 << USB_DEVICE_SELF_POWERED;
		status |= udc->remote_wakeup << USB_DEVICE_REMOTE_WAKEUP;
	} else if ((setup->bRequestType & USB_RECIP_MASK)
			== USB_RECIP_INTERFACE) {
		/* get interface status */
		status = 0;
	} else if ((setup->bRequestType & USB_RECIP_MASK)
			== USB_RECIP_ENDPOINT) {
		u8 ep_num, direction;

		ep_num = setup->wIndex & USB_ENDPOINT_NUMBER_MASK;
		direction = (setup->wIndex & USB_ENDPOINT_DIR_MASK)
				? EP_DIR_IN : EP_DIR_OUT;
		status = ep_is_stall(udc, ep_num, direction)
				<< USB_ENDPOINT_HALT;
	}

	retval = udc_prime_status(udc, EP_DIR_IN, status, false);
	if (retval)
		ep0_stall(udc);
	else
		udc->ep0_state = DATA_STATE_XMIT;
}

static void ch9clearfeature(struct mv_udc *udc, struct usb_ctrlrequest *setup)
{
	u8 ep_num;
	u8 direction;
	struct mv_ep *ep;

	if ((setup->bRequestType & (USB_TYPE_MASK | USB_RECIP_MASK))
		== ((USB_TYPE_STANDARD | USB_RECIP_DEVICE))) {
		switch (setup->wValue) {
		case USB_DEVICE_REMOTE_WAKEUP:
			udc->remote_wakeup = 0;
			break;
		default:
			goto out;
		}
	} else if ((setup->bRequestType & (USB_TYPE_MASK | USB_RECIP_MASK))
		== ((USB_TYPE_STANDARD | USB_RECIP_ENDPOINT))) {
		switch (setup->wValue) {
		case USB_ENDPOINT_HALT:
			ep_num = setup->wIndex & USB_ENDPOINT_NUMBER_MASK;
			direction = (setup->wIndex & USB_ENDPOINT_DIR_MASK)
				? EP_DIR_IN : EP_DIR_OUT;
			if (setup->wValue != 0 || setup->wLength != 0
				|| ep_num > udc->max_eps)
				goto out;
			ep = &udc->eps[ep_num * 2 + direction];
			if (ep->wedge == 1)
				break;
			spin_unlock(&udc->lock);
			ep_set_stall(udc, ep_num, direction, 0);
			spin_lock(&udc->lock);
			break;
		default:
			goto out;
		}
	} else
		goto out;

	if (udc_prime_status(udc, EP_DIR_IN, 0, true))
		ep0_stall(udc);
out:
	return;
}

static void ch9setfeature(struct mv_udc *udc, struct usb_ctrlrequest *setup)
{
	u8 ep_num;
	u8 direction;

	if ((setup->bRequestType & (USB_TYPE_MASK | USB_RECIP_MASK))
		== ((USB_TYPE_STANDARD | USB_RECIP_DEVICE))) {
		switch (setup->wValue) {
		case USB_DEVICE_REMOTE_WAKEUP:
			udc->remote_wakeup = 1;
			break;
		case USB_DEVICE_TEST_MODE:
			if (setup->wIndex & 0xFF
				||  udc->gadget.speed != USB_SPEED_HIGH)
				ep0_stall(udc);

			if (udc->usb_state != USB_STATE_CONFIGURED
				&& udc->usb_state != USB_STATE_ADDRESS
				&& udc->usb_state != USB_STATE_DEFAULT)
				ep0_stall(udc);

			mv_udc_testmode(udc, (setup->wIndex >> 8));
			goto out;
		default:
			goto out;
		}
	} else if ((setup->bRequestType & (USB_TYPE_MASK | USB_RECIP_MASK))
		== ((USB_TYPE_STANDARD | USB_RECIP_ENDPOINT))) {
		switch (setup->wValue) {
		case USB_ENDPOINT_HALT:
			ep_num = setup->wIndex & USB_ENDPOINT_NUMBER_MASK;
			direction = (setup->wIndex & USB_ENDPOINT_DIR_MASK)
				? EP_DIR_IN : EP_DIR_OUT;
			if (setup->wValue != 0 || setup->wLength != 0
				|| ep_num > udc->max_eps)
				goto out;
			spin_unlock(&udc->lock);
			ep_set_stall(udc, ep_num, direction, 1);
			spin_lock(&udc->lock);
			break;
		default:
			goto out;
		}
	} else
		goto out;

	if (udc_prime_status(udc, EP_DIR_IN, 0, true))
		ep0_stall(udc);
out:
	return;
}

static void handle_setup_packet(struct mv_udc *udc, u8 ep_num,
	struct usb_ctrlrequest *setup)
	__releases(&ep->udc->lock)
	__acquires(&ep->udc->lock)
{
	bool delegate = false;

	nuke(&udc->eps[ep_num * 2 + EP_DIR_OUT], -ESHUTDOWN);

	dev_dbg(&udc->dev->dev, "SETUP %02x.%02x v%04x i%04x l%04x\n",
			setup->bRequestType, setup->bRequest,
			setup->wValue, setup->wIndex, setup->wLength);
	/* We process some standard setup requests here */
	if ((setup->bRequestType & USB_TYPE_MASK) == USB_TYPE_STANDARD) {
		switch (setup->bRequest) {
		case USB_REQ_GET_STATUS:
			ch9getstatus(udc, ep_num, setup);
			break;

		case USB_REQ_SET_ADDRESS:
			ch9setaddress(udc, setup);
			break;

		case USB_REQ_CLEAR_FEATURE:
			ch9clearfeature(udc, setup);
			break;

		case USB_REQ_SET_FEATURE:
			ch9setfeature(udc, setup);
			break;

		default:
			delegate = true;
		}
	} else
		delegate = true;

	/* delegate USB standard requests to the gadget driver */
	if (delegate == true) {
		/* USB requests handled by gadget */
		if (setup->wLength) {
			/* DATA phase from gadget, STATUS phase from udc */
			udc->ep0_dir = (setup->bRequestType & USB_DIR_IN)
					?  EP_DIR_IN : EP_DIR_OUT;
			spin_unlock(&udc->lock);
			if (udc->driver->setup(&udc->gadget,
				&udc->local_setup_buff) < 0)
				ep0_stall(udc);
			spin_lock(&udc->lock);
			udc->ep0_state = (setup->bRequestType & USB_DIR_IN)
					?  DATA_STATE_XMIT : DATA_STATE_RECV;
		} else {
			/* no DATA phase, IN STATUS phase from gadget */
			udc->ep0_dir = EP_DIR_IN;
			spin_unlock(&udc->lock);
			if (udc->driver->setup(&udc->gadget,
				&udc->local_setup_buff) < 0)
				ep0_stall(udc);
			spin_lock(&udc->lock);
			udc->ep0_state = WAIT_FOR_OUT_STATUS;
		}
	}
}

/* complete DATA or STATUS phase of ep0 prime status phase if needed */
static void ep0_req_complete(struct mv_udc *udc,
	struct mv_ep *ep0, struct mv_req *req)
{
	u32 new_addr;

	if (udc->usb_state == USB_STATE_ADDRESS) {
		/* set the new address */
		new_addr = (u32)udc->dev_addr;
		writel(new_addr << USB_DEVICE_ADDRESS_BIT_SHIFT,
			&udc->op_regs->deviceaddr);
	}

	done(ep0, req, 0);

	switch (udc->ep0_state) {
	case DATA_STATE_XMIT:
		/* receive status phase */
		if (udc_prime_status(udc, EP_DIR_OUT, 0, true))
			ep0_stall(udc);
		break;
	case DATA_STATE_RECV:
		/* send status phase */
		if (udc_prime_status(udc, EP_DIR_IN, 0 , true))
			ep0_stall(udc);
		break;
	case WAIT_FOR_OUT_STATUS:
		udc->ep0_state = WAIT_FOR_SETUP;
		break;
	case WAIT_FOR_SETUP:
		dev_err(&udc->dev->dev, "unexpect ep0 packets\n");
		break;
	default:
		ep0_stall(udc);
		break;
	}
}

static void get_setup_data(struct mv_udc *udc, u8 ep_num, u8 *buffer_ptr)
{
	u32 temp;
	struct mv_dqh *dqh;

	dqh = &udc->ep_dqh[ep_num * 2 + EP_DIR_OUT];

	/* Clear bit in ENDPTSETUPSTAT */
	writel((1 << ep_num), &udc->op_regs->epsetupstat);

	/* while a hazard exists when setup package arrives */
	do {
		/* Set Setup Tripwire */
		temp = readl(&udc->op_regs->usbcmd);
		writel(temp | USBCMD_SETUP_TRIPWIRE_SET, &udc->op_regs->usbcmd);

		/* Copy the setup packet to local buffer */
		memcpy(buffer_ptr, (u8 *) dqh->setup_buffer, 8);
	} while (!(readl(&udc->op_regs->usbcmd) & USBCMD_SETUP_TRIPWIRE_SET));

	/* Clear Setup Tripwire */
	temp = readl(&udc->op_regs->usbcmd);
	writel(temp & ~USBCMD_SETUP_TRIPWIRE_SET, &udc->op_regs->usbcmd);
}

static void irq_process_tr_complete(struct mv_udc *udc)
{
	u32 tmp, bit_pos;
	int i, ep_num = 0, direction = 0;
	struct mv_ep	*curr_ep;
	struct mv_req *curr_req, *temp_req;
	int status;

	/*
	 * We use separate loops for ENDPTSETUPSTAT and ENDPTCOMPLETE
	 * because the setup packets are to be read ASAP
	 */

	/* Process all Setup packet received interrupts */
	tmp = readl(&udc->op_regs->epsetupstat);

	if (tmp) {
		for (i = 0; i < udc->max_eps; i++) {
			if (tmp & (1 << i)) {
				get_setup_data(udc, i,
					(u8 *)(&udc->local_setup_buff));
				handle_setup_packet(udc, i,
					&udc->local_setup_buff);
			}
		}
	}

	/* Don't clear the endpoint setup status register here.
	 * It is cleared as a setup packet is read out of the buffer
	 */

	/* Process non-setup transaction complete interrupts */
	tmp = readl(&udc->op_regs->epcomplete);

	if (!tmp)
		return;

	writel(tmp, &udc->op_regs->epcomplete);

	for (i = 0; i < udc->max_eps * 2; i++) {
		ep_num = i >> 1;
		direction = i % 2;

		bit_pos = 1 << (ep_num + 16 * direction);

		if (!(bit_pos & tmp))
			continue;

		if (i == 1)
			curr_ep = &udc->eps[0];
		else
			curr_ep = &udc->eps[i];
		/* process the req queue until an uncomplete request */
		list_for_each_entry_safe(curr_req, temp_req,
			&curr_ep->queue, queue) {
			status = process_ep_req(udc, i, curr_req);
			if (status)
				break;

			/* write back status to req */
			curr_req->req.status = status;

			/* ep0 request completion */
			if (ep_num == 0) {
				ep0_req_complete(udc, curr_ep, curr_req);
				break;
			} else {
				done(curr_ep, curr_req, status);
			}
		}
	}
}

static void irq_process_reset(struct mv_udc *udc)
{
	u32 tmp;
	unsigned int loops;

	udc->ep0_dir = EP_DIR_OUT;
	udc->ep0_state = WAIT_FOR_SETUP;
	udc->remote_wakeup = 0;		/* default to 0 on reset */

	/* The address bits are past bit 25-31. Set the address */
	tmp = readl(&udc->op_regs->deviceaddr);
	tmp &= ~(USB_DEVICE_ADDRESS_MASK);
	writel(tmp, &udc->op_regs->deviceaddr);

	/* Clear all the setup token semaphores */
	tmp = readl(&udc->op_regs->epsetupstat);
	writel(tmp, &udc->op_regs->epsetupstat);

	/* Clear all the endpoint complete status bits */
	tmp = readl(&udc->op_regs->epcomplete);
	writel(tmp, &udc->op_regs->epcomplete);

	/* wait until all endptprime bits cleared */
	loops = LOOPS(PRIME_TIMEOUT);
	while (readl(&udc->op_regs->epprime) & 0xFFFFFFFF) {
		if (loops == 0) {
			dev_err(&udc->dev->dev,
				"Timeout for ENDPTPRIME = 0x%x\n",
				readl(&udc->op_regs->epprime));
			break;
		}
		loops--;
		udelay(LOOPS_USEC);
	}

	/* Write 1s to the Flush register */
	writel((u32)~0, &udc->op_regs->epflush);

	if (readl(&udc->op_regs->portsc[0]) & PORTSCX_PORT_RESET) {
		dev_info(&udc->dev->dev, "usb bus reset\n");
		udc->usb_state = USB_STATE_DEFAULT;
		/* reset all the queues, stop all USB activities */
		gadget_reset(udc, udc->driver);
	} else {
		dev_info(&udc->dev->dev, "USB reset portsc 0x%x\n",
			readl(&udc->op_regs->portsc));

		/*
		 * re-initialize
		 * controller reset
		 */
		udc_reset(udc);

		/* reset all the queues, stop all USB activities */
		stop_activity(udc, udc->driver);

		/* reset ep0 dQH and endptctrl */
		ep0_reset(udc);

		/* enable interrupt and set controller to run state */
		udc_start(udc);

		udc->usb_state = USB_STATE_ATTACHED;
	}
}

static void handle_bus_resume(struct mv_udc *udc)
{
	udc->usb_state = udc->resume_state;
	udc->resume_state = 0;

	/* report resume to the driver */
	if (udc->driver) {
		if (udc->driver->resume) {
			spin_unlock(&udc->lock);
			udc->driver->resume(&udc->gadget);
			spin_lock(&udc->lock);
		}
	}
}

static void irq_process_suspend(struct mv_udc *udc)
{
	udc->resume_state = udc->usb_state;
	udc->usb_state = USB_STATE_SUSPENDED;

	if (udc->driver->suspend) {
		spin_unlock(&udc->lock);
		udc->driver->suspend(&udc->gadget);
		spin_lock(&udc->lock);
	}
}

static void irq_process_port_change(struct mv_udc *udc)
{
	u32 portsc;

	portsc = readl(&udc->op_regs->portsc[0]);
	if (!(portsc & PORTSCX_PORT_RESET)) {
		/* Get the speed */
		u32 speed = portsc & PORTSCX_PORT_SPEED_MASK;
		switch (speed) {
		case PORTSCX_PORT_SPEED_HIGH:
			udc->gadget.speed = USB_SPEED_HIGH;
			break;
		case PORTSCX_PORT_SPEED_FULL:
			udc->gadget.speed = USB_SPEED_FULL;
			break;
		case PORTSCX_PORT_SPEED_LOW:
			udc->gadget.speed = USB_SPEED_LOW;
			break;
		default:
			udc->gadget.speed = USB_SPEED_UNKNOWN;
			break;
		}
	}

	if (portsc & PORTSCX_PORT_SUSPEND) {
		udc->resume_state = udc->usb_state;
		udc->usb_state = USB_STATE_SUSPENDED;
		if (udc->driver->suspend) {
			spin_unlock(&udc->lock);
			udc->driver->suspend(&udc->gadget);
			spin_lock(&udc->lock);
		}
	}

	if (!(portsc & PORTSCX_PORT_SUSPEND)
		&& udc->usb_state == USB_STATE_SUSPENDED) {
		handle_bus_resume(udc);
	}

	if (!udc->resume_state)
		udc->usb_state = USB_STATE_DEFAULT;
}

static void irq_process_error(struct mv_udc *udc)
{
	/* Increment the error count */
	udc->errors++;
}

static irqreturn_t mv_udc_irq(int irq, void *dev)
{
	struct mv_udc *udc = (struct mv_udc *)dev;
	u32 status, intr;

	/* Disable ISR when stopped bit is set */
	if (udc->stopped)
		return IRQ_NONE;

	spin_lock(&udc->lock);

	status = readl(&udc->op_regs->usbsts);
	intr = readl(&udc->op_regs->usbintr);
	status &= intr;

	if (status == 0) {
		spin_unlock(&udc->lock);
		return IRQ_NONE;
	}

	/* Clear all the interrupts occurred */
	writel(status, &udc->op_regs->usbsts);

	if (status & USBSTS_ERR)
		irq_process_error(udc);

	if (status & USBSTS_RESET)
		irq_process_reset(udc);

	if (status & USBSTS_PORT_CHANGE)
		irq_process_port_change(udc);

	if (status & USBSTS_INT)
		irq_process_tr_complete(udc);

	if (status & USBSTS_SUSPEND)
		irq_process_suspend(udc);

	spin_unlock(&udc->lock);

	return IRQ_HANDLED;
}

static irqreturn_t mv_udc_vbus_irq(int irq, void *dev)
{
	struct mv_udc *udc = (struct mv_udc *)dev;

	/* polling VBUS and init phy may cause too much time*/
	if (udc->qwork)
		queue_work(udc->qwork, &udc->vbus_work);

	return IRQ_HANDLED;
}

static void mv_udc_vbus_work(struct work_struct *work)
{
	struct mv_udc *udc;
	unsigned int vbus;

	udc = container_of(work, struct mv_udc, vbus_work);
	if (!udc->pdata->vbus)
		return;

	vbus = udc->pdata->vbus->poll();
	dev_info(&udc->dev->dev, "vbus is %d\n", vbus);

	if (vbus == VBUS_HIGH)
		mv_udc_vbus_session(&udc->gadget, 1);
	else if (vbus == VBUS_LOW)
		mv_udc_vbus_session(&udc->gadget, 0);
}

/* release device structure */
static void gadget_release(struct device *_dev)
{
	struct mv_udc *udc;

	udc = dev_get_drvdata(_dev);

	complete(udc->done);
}

static void mv_udc_remove(struct platform_device *pdev)
{
	struct mv_udc *udc;

	udc = platform_get_drvdata(pdev);

	usb_del_gadget_udc(&udc->gadget);

	if (udc->qwork)
		destroy_workqueue(udc->qwork);

	/* free memory allocated in probe */
	dma_pool_destroy(udc->dtd_pool);

	if (udc->ep_dqh)
		dma_free_coherent(&pdev->dev, udc->ep_dqh_size,
			udc->ep_dqh, udc->ep_dqh_dma);

	mv_udc_disable(udc);

	/* free dev, wait for the release() finished */
	wait_for_completion(udc->done);
}

static int mv_udc_probe(struct platform_device *pdev)
{
	struct mv_usb_platform_data *pdata = dev_get_platdata(&pdev->dev);
	struct mv_udc *udc;
	int retval = 0;
	struct resource *r;
	size_t size;

	if (pdata == NULL) {
		dev_err(&pdev->dev, "missing platform_data\n");
		return -ENODEV;
	}

	udc = devm_kzalloc(&pdev->dev, sizeof(*udc), GFP_KERNEL);
	if (udc == NULL)
		return -ENOMEM;

	udc->done = &release_done;
	udc->pdata = dev_get_platdata(&pdev->dev);
	spin_lock_init(&udc->lock);

	udc->dev = pdev;

	if (pdata->mode == MV_USB_MODE_OTG) {
		udc->transceiver = devm_usb_get_phy(&pdev->dev,
					USB_PHY_TYPE_USB2);
		if (IS_ERR(udc->transceiver)) {
			retval = PTR_ERR(udc->transceiver);

			if (retval == -ENXIO)
				return retval;

			udc->transceiver = NULL;
			return -EPROBE_DEFER;
		}
	}

	/* udc only have one sysclk. */
	udc->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(udc->clk))
		return PTR_ERR(udc->clk);

	r = platform_get_resource_byname(udc->dev, IORESOURCE_MEM, "capregs");
	if (r == NULL) {
		dev_err(&pdev->dev, "no I/O memory resource defined\n");
		return -ENODEV;
	}

	udc->cap_regs = (struct mv_cap_regs __iomem *)
		devm_ioremap(&pdev->dev, r->start, resource_size(r));
	if (udc->cap_regs == NULL) {
		dev_err(&pdev->dev, "failed to map I/O memory\n");
		return -EBUSY;
	}

	r = platform_get_resource_byname(udc->dev, IORESOURCE_MEM, "phyregs");
	if (r == NULL) {
		dev_err(&pdev->dev, "no phy I/O memory resource defined\n");
		return -ENODEV;
	}

	udc->phy_regs = devm_ioremap(&pdev->dev, r->start, resource_size(r));
	if (udc->phy_regs == NULL) {
		dev_err(&pdev->dev, "failed to map phy I/O memory\n");
		return -EBUSY;
	}

	/* we will acces controller register, so enable the clk */
	retval = mv_udc_enable_internal(udc);
	if (retval)
		return retval;

	udc->op_regs =
		(struct mv_op_regs __iomem *)((unsigned long)udc->cap_regs
		+ (readl(&udc->cap_regs->caplength_hciversion)
			& CAPLENGTH_MASK));
	udc->max_eps = readl(&udc->cap_regs->dccparams) & DCCPARAMS_DEN_MASK;

	/*
	 * some platform will use usb to download image, it may not disconnect
	 * usb gadget before loading kernel. So first stop udc here.
	 */
	udc_stop(udc);
	writel(0xFFFFFFFF, &udc->op_regs->usbsts);

	size = udc->max_eps * sizeof(struct mv_dqh) *2;
	size = (size + DQH_ALIGNMENT - 1) & ~(DQH_ALIGNMENT - 1);
	udc->ep_dqh = dma_alloc_coherent(&pdev->dev, size,
					&udc->ep_dqh_dma, GFP_KERNEL);

	if (udc->ep_dqh == NULL) {
		dev_err(&pdev->dev, "allocate dQH memory failed\n");
		retval = -ENOMEM;
		goto err_disable_clock;
	}
	udc->ep_dqh_size = size;

	/* create dTD dma_pool resource */
	udc->dtd_pool = dma_pool_create("mv_dtd",
			&pdev->dev,
			sizeof(struct mv_dtd),
			DTD_ALIGNMENT,
			DMA_BOUNDARY);

	if (!udc->dtd_pool) {
		retval = -ENOMEM;
		goto err_free_dma;
	}

	size = udc->max_eps * sizeof(struct mv_ep) *2;
	udc->eps = devm_kzalloc(&pdev->dev, size, GFP_KERNEL);
	if (udc->eps == NULL) {
		retval = -ENOMEM;
		goto err_destroy_dma;
	}

	/* initialize ep0 status request structure */
	udc->status_req = devm_kzalloc(&pdev->dev, sizeof(struct mv_req),
					GFP_KERNEL);
	if (!udc->status_req) {
		retval = -ENOMEM;
		goto err_destroy_dma;
	}
	INIT_LIST_HEAD(&udc->status_req->queue);

	/* allocate a small amount of memory to get valid address */
	udc->status_req->req.buf = devm_kzalloc(&pdev->dev, 8, GFP_KERNEL);
	if (!udc->status_req->req.buf) {
		retval = -ENOMEM;
		goto err_destroy_dma;
	}
	udc->status_req->req.dma = DMA_ADDR_INVALID;

	udc->resume_state = USB_STATE_NOTATTACHED;
	udc->usb_state = USB_STATE_POWERED;
	udc->ep0_dir = EP_DIR_OUT;
	udc->remote_wakeup = 0;

	r = platform_get_resource(udc->dev, IORESOURCE_IRQ, 0);
	if (r == NULL) {
		dev_err(&pdev->dev, "no IRQ resource defined\n");
		retval = -ENODEV;
		goto err_destroy_dma;
	}
	udc->irq = r->start;
	if (devm_request_irq(&pdev->dev, udc->irq, mv_udc_irq,
		IRQF_SHARED, driver_name, udc)) {
		dev_err(&pdev->dev, "Request irq %d for UDC failed\n",
			udc->irq);
		retval = -ENODEV;
		goto err_destroy_dma;
	}

	/* initialize gadget structure */
	udc->gadget.ops = &mv_ops;	/* usb_gadget_ops */
	udc->gadget.ep0 = &udc->eps[0].ep;	/* gadget ep0 */
	INIT_LIST_HEAD(&udc->gadget.ep_list);	/* ep_list */
	udc->gadget.speed = USB_SPEED_UNKNOWN;	/* speed */
	udc->gadget.max_speed = USB_SPEED_HIGH;	/* support dual speed */

	/* the "gadget" abstracts/virtualizes the controller */
	udc->gadget.name = driver_name;		/* gadget name */

	eps_init(udc);

	/* VBUS detect: we can disable/enable clock on demand.*/
	if (udc->transceiver)
		udc->clock_gating = 1;
	else if (pdata->vbus) {
		udc->clock_gating = 1;
		retval = devm_request_threaded_irq(&pdev->dev,
				pdata->vbus->irq, NULL,
				mv_udc_vbus_irq, IRQF_ONESHOT, "vbus", udc);
		if (retval) {
			dev_info(&pdev->dev,
				"Can not request irq for VBUS, "
				"disable clock gating\n");
			udc->clock_gating = 0;
		}

		udc->qwork = create_singlethread_workqueue("mv_udc_queue");
		if (!udc->qwork) {
			dev_err(&pdev->dev, "cannot create workqueue\n");
			retval = -ENOMEM;
			goto err_destroy_dma;
		}

		INIT_WORK(&udc->vbus_work, mv_udc_vbus_work);
	}

	/*
	 * When clock gating is supported, we can disable clk and phy.
	 * If not, it means that VBUS detection is not supported, we
	 * have to enable vbus active all the time to let controller work.
	 */
	if (udc->clock_gating)
		mv_udc_disable_internal(udc);
	else
		udc->vbus_active = 1;

	retval = usb_add_gadget_udc_release(&pdev->dev, &udc->gadget,
			gadget_release);
	if (retval)
		goto err_create_workqueue;

	platform_set_drvdata(pdev, udc);
	dev_info(&pdev->dev, "successful probe UDC device %s clock gating.\n",
		udc->clock_gating ? "with" : "without");

	return 0;

err_create_workqueue:
	if (udc->qwork)
		destroy_workqueue(udc->qwork);
err_destroy_dma:
	dma_pool_destroy(udc->dtd_pool);
err_free_dma:
	dma_free_coherent(&pdev->dev, udc->ep_dqh_size,
			udc->ep_dqh, udc->ep_dqh_dma);
err_disable_clock:
	mv_udc_disable_internal(udc);

	return retval;
}

#ifdef CONFIG_PM
static int mv_udc_suspend(struct device *dev)
{
	struct mv_udc *udc;

	udc = dev_get_drvdata(dev);

	/* if OTG is enabled, the following will be done in OTG driver*/
	if (udc->transceiver)
		return 0;

	if (udc->pdata->vbus && udc->pdata->vbus->poll)
		if (udc->pdata->vbus->poll() == VBUS_HIGH) {
			dev_info(&udc->dev->dev, "USB cable is connected!\n");
			return -EAGAIN;
		}

	/*
	 * only cable is unplugged, udc can suspend.
	 * So do not care about clock_gating == 1.
	 */
	if (!udc->clock_gating) {
		udc_stop(udc);

		spin_lock_irq(&udc->lock);
		/* stop all usb activities */
		stop_activity(udc, udc->driver);
		spin_unlock_irq(&udc->lock);

		mv_udc_disable_internal(udc);
	}

	return 0;
}

static int mv_udc_resume(struct device *dev)
{
	struct mv_udc *udc;
	int retval;

	udc = dev_get_drvdata(dev);

	/* if OTG is enabled, the following will be done in OTG driver*/
	if (udc->transceiver)
		return 0;

	if (!udc->clock_gating) {
		retval = mv_udc_enable_internal(udc);
		if (retval)
			return retval;

		if (udc->driver && udc->softconnect) {
			udc_reset(udc);
			ep0_reset(udc);
			udc_start(udc);
		}
	}

	return 0;
}

static const struct dev_pm_ops mv_udc_pm_ops = {
	.suspend	= mv_udc_suspend,
	.resume		= mv_udc_resume,
};
#endif

static void mv_udc_shutdown(struct platform_device *pdev)
{
	struct mv_udc *udc;
	u32 mode;

	udc = platform_get_drvdata(pdev);
	/* reset controller mode to IDLE */
	mv_udc_enable(udc);
	mode = readl(&udc->op_regs->usbmode);
	mode &= ~3;
	writel(mode, &udc->op_regs->usbmode);
	mv_udc_disable(udc);
}

static struct platform_driver udc_driver = {
	.probe		= mv_udc_probe,
	.remove_new	= mv_udc_remove,
	.shutdown	= mv_udc_shutdown,
	.driver		= {
		.name	= "mv-udc",
#ifdef CONFIG_PM
		.pm	= &mv_udc_pm_ops,
#endif
	},
};

module_platform_driver(udc_driver);
MODULE_ALIAS("platform:mv-udc");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR("Chao Xie <chao.xie@marvell.com>");
MODULE_LICENSE("GPL");
