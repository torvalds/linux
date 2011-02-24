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
#include <linux/init.h>
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
#include <asm/system.h>
#include <asm/unaligned.h>

#include "mv_udc.h"

#define DRIVER_DESC		"Marvell PXA USB Device Controller driver"
#define DRIVER_VERSION		"8 Nov 2010"

#define ep_dir(ep)	(((ep)->ep_num == 0) ? \
				((ep)->udc->ep0_dir) : ((ep)->direction))

/* timeout value -- usec */
#define RESET_TIMEOUT		10000
#define FLUSH_TIMEOUT		10000
#define EPSTATUS_TIMEOUT	10000
#define PRIME_TIMEOUT		10000
#define READSAFE_TIMEOUT	1000
#define DTD_TIMEOUT		1000

#define LOOPS_USEC_SHIFT	4
#define LOOPS_USEC		(1 << LOOPS_USEC_SHIFT)
#define LOOPS(timeout)		((timeout) >> LOOPS_USEC_SHIFT)

static const char driver_name[] = "mv_udc";
static const char driver_desc[] = DRIVER_DESC;

/* controller device global variable */
static struct mv_udc	*the_controller;
int mv_usb_otgsc;

static void nuke(struct mv_ep *ep, int status);

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

		epctrlx = readl(&udc->op_regs->epctrlx[0]);
		if (i) {	/* TX */
			epctrlx |= EPCTRL_TX_ENABLE | EPCTRL_TX_DATA_TOGGLE_RST
				| (USB_ENDPOINT_XFER_CONTROL
					<< EPCTRL_TX_EP_TYPE_SHIFT);

		} else {	/* RX */
			epctrlx |= EPCTRL_RX_ENABLE | EPCTRL_RX_DATA_TOGGLE_RST
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
	int td_complete, actual, remaining_length;
	int i, direction;
	int retval = 0;
	u32 errors;

	curr_dqh = &udc->ep_dqh[index];
	direction = index % 2;

	curr_dtd = curr_req->head;
	td_complete = 0;
	actual = curr_req->req.length;

	for (i = 0; i < curr_req->dtd_count; i++) {
		if (curr_dtd->size_ioc_sts & DTD_STATUS_ACTIVE) {
			dev_dbg(&udc->dev->dev, "%s, dTD not completed\n",
				udc->eps[index].name);
			return 1;
		}

		errors = curr_dtd->size_ioc_sts & DTD_ERROR_MASK;
		if (!errors) {
			remaining_length +=
				(curr_dtd->size_ioc_sts	& DTD_PACKET_SIZE)
					>> DTD_LENGTH_BIT_POS;
			actual -= remaining_length;
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

	curr_req->req.actual = actual;

	return 0;
}

/*
 * done() - retire a request; caller blocked irqs
 * @status : request status to be set, only works when
 * request is still in progress.
 */
static void done(struct mv_ep *ep, struct mv_req *req, int status)
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

	if (req->mapped) {
		dma_unmap_single(ep->udc->gadget.dev.parent,
			req->req.dma, req->req.length,
			((ep_dir(ep) == EP_DIR_IN) ?
				DMA_TO_DEVICE : DMA_FROM_DEVICE));
		req->req.dma = DMA_ADDR_INVALID;
		req->mapped = 0;
	} else
		dma_sync_single_for_cpu(ep->udc->gadget.dev.parent,
			req->req.dma, req->req.length,
			((ep_dir(ep) == EP_DIR_IN) ?
				DMA_TO_DEVICE : DMA_FROM_DEVICE));

	if (status && (status != -ESHUTDOWN))
		dev_info(&udc->dev->dev, "complete %s req %p stat %d len %u/%u",
			ep->ep.name, &req->req, status,
			req->req.actual, req->req.length);

	ep->stopped = 1;

	spin_unlock(&ep->udc->lock);
	/*
	 * complete() is from gadget layer,
	 * eg fsg->bulk_in_complete()
	 */
	if (req->req.complete)
		req->req.complete(&ep->ep, &req->req);

	spin_lock(&ep->udc->lock);
	ep->stopped = stopped;
}

static int queue_dtd(struct mv_ep *ep, struct mv_req *req)
{
	u32 tmp, epstatus, bit_pos, direction;
	struct mv_udc *udc;
	struct mv_dqh *dqh;
	unsigned int loops;
	int readsafe, retval = 0;

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
		if (readl(&udc->op_regs->epprime) & bit_pos) {
			loops = LOOPS(PRIME_TIMEOUT);
			while (readl(&udc->op_regs->epprime) & bit_pos) {
				if (loops == 0) {
					retval = -ETIME;
					goto done;
				}
				udelay(LOOPS_USEC);
				loops--;
			}
			if (readl(&udc->op_regs->epstatus) & bit_pos)
				goto done;
		}
		readsafe = 0;
		loops = LOOPS(READSAFE_TIMEOUT);
		while (readsafe == 0) {
			if (loops == 0) {
				retval = -ETIME;
				goto done;
			}
			/* start with setting the semaphores */
			tmp = readl(&udc->op_regs->usbcmd);
			tmp |= USBCMD_ATDTW_TRIPWIRE_SET;
			writel(tmp, &udc->op_regs->usbcmd);

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
				& USBCMD_ATDTW_TRIPWIRE_SET) {
				readsafe = 1;
			}
			loops--;
			udelay(LOOPS_USEC);
		}

		/* Clear the semaphore */
		tmp = readl(&udc->op_regs->usbcmd);
		tmp &= USBCMD_ATDTW_TRIPWIRE_CLEAR;
		writel(tmp, &udc->op_regs->usbcmd);

		/* If endpoint is not active, we activate it now. */
		if (!epstatus) {
			if (direction == EP_DIR_IN) {
				struct mv_dtd *curr_dtd = dma_to_virt(
					&udc->dev->dev, dqh->curr_dtd_ptr);

				loops = LOOPS(DTD_TIMEOUT);
				while (curr_dtd->size_ioc_sts
					& DTD_STATUS_ACTIVE) {
					if (loops == 0) {
						retval = -ETIME;
						goto done;
					}
					loops--;
					udelay(LOOPS_USEC);
				}
			}
			/* No other transfers on the queue */

			/* Write dQH next pointer and terminate bit to 0 */
			dqh->next_dtd_ptr = req->head->td_dma
				& EP_QUEUE_HEAD_NEXT_POINTER_MASK;
			dqh->size_ioc_int_sts = 0;

			/*
			 * Ensure that updates to the QH will
			 * occure before priming.
			 */
			wmb();

			/* Prime the Endpoint */
			writel(bit_pos, &udc->op_regs->epprime);
		}
	} else {
		/* Write dQH next pointer and terminate bit to 0 */
		dqh->next_dtd_ptr = req->head->td_dma
			& EP_QUEUE_HEAD_NEXT_POINTER_MASK;;
		dqh->size_ioc_int_sts = 0;

		/* Ensure that updates to the QH will occure before priming. */
		wmb();

		/* Prime the Endpoint */
		writel(bit_pos, &udc->op_regs->epprime);

		if (direction == EP_DIR_IN) {
			/* FIXME add status check after prime the IN ep */
			int prime_again;
			u32 curr_dtd_ptr = dqh->curr_dtd_ptr;

			loops = LOOPS(DTD_TIMEOUT);
			prime_again = 0;
			while ((curr_dtd_ptr != req->head->td_dma)) {
				curr_dtd_ptr = dqh->curr_dtd_ptr;
				if (loops == 0) {
					dev_err(&udc->dev->dev,
						"failed to prime %s\n",
						ep->name);
					retval = -ETIME;
					goto done;
				}
				loops--;
				udelay(LOOPS_USEC);

				if (loops == (LOOPS(DTD_TIMEOUT) >> 2)) {
					if (prime_again)
						goto done;
					dev_info(&udc->dev->dev,
						"prime again\n");
					writel(bit_pos,
						&udc->op_regs->epprime);
					prime_again = 1;
				}
			}
		}
	}
done:
	return retval;;
}

static struct mv_dtd *build_dtd(struct mv_req *req, unsigned *length,
		dma_addr_t *dma, int *is_last)
{
	u32 temp;
	struct mv_dtd *dtd;
	struct mv_udc *udc;

	/* how big will this transfer be? */
	*length = min(req->req.length - req->req.actual,
			(unsigned)EP_MAX_LENGTH_TRANSFER);

	udc = req->ep->udc;

	/*
	 * Be careful that no _GFP_HIGHMEM is set,
	 * or we can not use dma_to_virt
	 */
	dtd = dma_pool_alloc(udc->dtd_pool, GFP_KERNEL, dma);
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
	struct mv_udc *udc;
	dma_addr_t dma;

	udc = req->ep->udc;

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
	unsigned char zlt = 0, ios = 0, mult = 0;

	ep = container_of(_ep, struct mv_ep, ep);
	udc = ep->udc;

	if (!_ep || !desc || ep->desc
			|| desc->bDescriptorType != USB_DT_ENDPOINT)
		return -EINVAL;

	if (!udc->driver || udc->gadget.speed == USB_SPEED_UNKNOWN)
		return -ESHUTDOWN;

	direction = ep_dir(ep);
	max = le16_to_cpu(desc->wMaxPacketSize);

	/*
	 * disable HW zero length termination select
	 * driver handles zero length packet through req->req.zero
	 */
	zlt = 1;

	/* Get the endpoint queue head address */
	dqh = (struct mv_dqh *)ep->dqh;

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
	switch (desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) {
	case USB_ENDPOINT_XFER_BULK:
		zlt = 1;
		mult = 0;
		break;
	case USB_ENDPOINT_XFER_CONTROL:
		ios = 1;
	case USB_ENDPOINT_XFER_INT:
		mult = 0;
		break;
	case USB_ENDPOINT_XFER_ISOC:
		/* Calculate transactions needed for high bandwidth iso */
		mult = (unsigned char)(1 + ((max >> 11) & 0x03));
		max = max & 0x8ff;	/* bit 0~10 */
		/* 3 transactions at most */
		if (mult > 3)
			goto en_done;
		break;
	default:
		goto en_done;
	}
	dqh->max_packet_length = (max << EP_QUEUE_HEAD_MAX_PKT_LEN_POS)
		| (mult << EP_QUEUE_HEAD_MULT_POS)
		| (zlt ? EP_QUEUE_HEAD_ZLT_SEL : 0)
		| (ios ? EP_QUEUE_HEAD_IOS : 0);
	dqh->next_dtd_ptr = 1;
	dqh->size_ioc_int_sts = 0;

	ep->ep.maxpacket = max;
	ep->desc = desc;
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
		epctrlx |= ((desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
				<< EPCTRL_RX_EP_TYPE_SHIFT);
		writel(epctrlx, &udc->op_regs->epctrlx[ep->ep_num]);
	}

	epctrlx = readl(&udc->op_regs->epctrlx[ep->ep_num]);
	if ((epctrlx & EPCTRL_TX_ENABLE) == 0) {
		epctrlx |= ((desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
				<< EPCTRL_TX_EP_TYPE_SHIFT);
		writel(epctrlx, &udc->op_regs->epctrlx[ep->ep_num]);
	}

	return 0;
en_done:
	return -EINVAL;
}

static int  mv_ep_disable(struct usb_ep *_ep)
{
	struct mv_udc *udc;
	struct mv_ep *ep;
	struct mv_dqh *dqh;
	u32 bit_pos, epctrlx, direction;

	ep = container_of(_ep, struct mv_ep, ep);
	if ((_ep == NULL) || !ep->desc)
		return -EINVAL;

	udc = ep->udc;

	/* Get the endpoint queue head address */
	dqh = ep->dqh;

	direction = ep_dir(ep);
	bit_pos = 1 << ((direction == EP_DIR_OUT ? 0 : 16) + ep->ep_num);

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

	ep->desc = NULL;
	ep->stopped = 1;
	return 0;
}

static struct usb_request *
mv_alloc_request(struct usb_ep *_ep, gfp_t gfp_flags)
{
	struct mv_req *req = NULL;

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
	struct mv_ep *ep = container_of(_ep, struct mv_ep, ep);
	unsigned int loops;

	udc = ep->udc;
	direction = ep_dir(ep);
	bit_pos = 1 << ((direction == EP_DIR_OUT ? 0 : 16) + ep->ep_num);
	/*
	 * Flushing will halt the pipe
	 * Write 1 to the Flush register
	 */
	writel(bit_pos, &udc->op_regs->epflush);

	/* Wait until flushing completed */
	loops = LOOPS(FLUSH_TIMEOUT);
	while (readl(&udc->op_regs->epflush) & bit_pos) {
		/*
		 * ENDPTFLUSH bit should be cleared to indicate this
		 * operation is complete
		 */
		if (loops == 0) {
			dev_err(&udc->dev->dev,
				"TIMEOUT for ENDPTFLUSH=0x%x, bit_pos=0x%x\n",
				(unsigned)readl(&udc->op_regs->epflush),
				(unsigned)bit_pos);
			return;
		}
		loops--;
		udelay(LOOPS_USEC);
	}
	loops = LOOPS(EPSTATUS_TIMEOUT);
	while (readl(&udc->op_regs->epstatus) & bit_pos) {
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
		while (readl(&udc->op_regs->epflush) & bit_pos) {
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
	}
}

/* queues (submits) an I/O request to an endpoint */
static int
mv_ep_queue(struct usb_ep *_ep, struct usb_request *_req, gfp_t gfp_flags)
{
	struct mv_ep *ep = container_of(_ep, struct mv_ep, ep);
	struct mv_req *req = container_of(_req, struct mv_req, req);
	struct mv_udc *udc = ep->udc;
	unsigned long flags;

	/* catch various bogus parameters */
	if (!_req || !req->req.complete || !req->req.buf
			|| !list_empty(&req->queue)) {
		dev_err(&udc->dev->dev, "%s, bad params", __func__);
		return -EINVAL;
	}
	if (unlikely(!_ep || !ep->desc)) {
		dev_err(&udc->dev->dev, "%s, bad ep", __func__);
		return -EINVAL;
	}
	if (ep->desc->bmAttributes == USB_ENDPOINT_XFER_ISOC) {
		if (req->req.length > ep->ep.maxpacket)
			return -EMSGSIZE;
	}

	udc = ep->udc;
	if (!udc->driver || udc->gadget.speed == USB_SPEED_UNKNOWN)
		return -ESHUTDOWN;

	req->ep = ep;

	/* map virtual address to hardware */
	if (req->req.dma == DMA_ADDR_INVALID) {
		req->req.dma = dma_map_single(ep->udc->gadget.dev.parent,
					req->req.buf,
					req->req.length, ep_dir(ep)
						? DMA_TO_DEVICE
						: DMA_FROM_DEVICE);
		req->mapped = 1;
	} else {
		dma_sync_single_for_device(ep->udc->gadget.dev.parent,
					req->req.dma, req->req.length,
					ep_dir(ep)
						? DMA_TO_DEVICE
						: DMA_FROM_DEVICE);
		req->mapped = 0;
	}

	req->req.status = -EINPROGRESS;
	req->req.actual = 0;
	req->dtd_count = 0;

	spin_lock_irqsave(&udc->lock, flags);

	/* build dtds and push them to device queue */
	if (!req_to_dtd(req)) {
		int retval;
		retval = queue_dtd(ep, req);
		if (retval) {
			spin_unlock_irqrestore(&udc->lock, flags);
			return retval;
		}
	} else {
		spin_unlock_irqrestore(&udc->lock, flags);
		return -ENOMEM;
	}

	/* Update ep0 state */
	if (ep->ep_num == 0)
		udc->ep0_state = DATA_STATE_XMIT;

	/* irq handler advances the queue */
	if (req != NULL)
		list_add_tail(&req->queue, &ep->queue);
	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;
}

/* dequeues (cancels, unlinks) an I/O request from an endpoint */
static int mv_ep_dequeue(struct usb_ep *_ep, struct usb_request *_req)
{
	struct mv_ep *ep = container_of(_ep, struct mv_ep, ep);
	struct mv_req *req;
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
	list_for_each_entry(req, &ep->queue, queue) {
		if (&req->req == _req)
			break;
	}
	if (&req->req != _req) {
		ret = -EINVAL;
		goto out;
	}

	/* The request is in progress, or completed but not dequeued */
	if (ep->queue.next == &req->queue) {
		_req->status = -ECONNRESET;
		mv_ep_fifo_flush(_ep);	/* flush current transfer */

		/* The request isn't the last request in this ep queue */
		if (req->queue.next != &ep->queue) {
			struct mv_dqh *qh;
			struct mv_req *next_req;

			qh = ep->dqh;
			next_req = list_entry(req->queue.next, struct mv_req,
					queue);

			/* Point the QH to the first TD of next request */
			writel((u32) next_req->head, &qh->curr_dtd_ptr);
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
	unsigned long flags = 0;
	int status = 0;
	struct mv_udc *udc;

	ep = container_of(_ep, struct mv_ep, ep);
	udc = ep->udc;
	if (!_ep || !ep->desc) {
		status = -EINVAL;
		goto out;
	}

	if (ep->desc->bmAttributes == USB_ENDPOINT_XFER_ISOC) {
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

static struct usb_ep_ops mv_ep_ops = {
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

static void udc_stop(struct mv_udc *udc)
{
	u32 tmp;

	/* Disable interrupts */
	tmp = readl(&udc->op_regs->usbintr);
	tmp &= ~(USBINTR_INT_EN | USBINTR_ERR_INT_EN |
		USBINTR_PORT_CHANGE_DETECT_EN | USBINTR_RESET_EN);
	writel(tmp, &udc->op_regs->usbintr);

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
	tmp |= USBMODE_SETUP_LOCK_OFF | USBMODE_STREAM_DISABLE;

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

static int mv_udc_get_frame(struct usb_gadget *gadget)
{
	struct mv_udc *udc;
	u16	retval;

	if (!gadget)
		return -ENODEV;

	udc = container_of(gadget, struct mv_udc, gadget);

	retval = readl(udc->op_regs->frindex) & USB_FRINDEX_MASKS;

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

static int mv_udc_pullup(struct usb_gadget *gadget, int is_on)
{
	struct mv_udc *udc;
	unsigned long flags;

	udc = container_of(gadget, struct mv_udc, gadget);
	spin_lock_irqsave(&udc->lock, flags);

	udc->softconnect = (is_on != 0);
	if (udc->driver && udc->softconnect)
		udc_start(udc);
	else
		udc_stop(udc);

	spin_unlock_irqrestore(&udc->lock, flags);
	return 0;
}

/* device controller usb_gadget_ops structure */
static const struct usb_gadget_ops mv_ops = {

	/* returns the current frame number */
	.get_frame	= mv_udc_get_frame,

	/* tries to wake up the host connected to this gadget */
	.wakeup		= mv_udc_wakeup,

	/* D+ pullup, software-controlled connect/disconnect to USB host */
	.pullup		= mv_udc_pullup,
};

static void mv_udc_testmode(struct mv_udc *udc, u16 index, bool enter)
{
	dev_info(&udc->dev->dev, "Test Mode is not support yet\n");
}

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
	ep->ep.maxpacket = EP0_MAX_PKT_SIZE;
	ep->ep_num = 0;
	ep->desc = &mv_ep0_desc;
	INIT_LIST_HEAD(&ep->queue);

	ep->ep_type = USB_ENDPOINT_XFER_CONTROL;

	/* initialize other endpoints */
	for (i = 2; i < udc->max_eps * 2; i++) {
		ep = &udc->eps[i];
		if (i % 2) {
			snprintf(name, sizeof(name), "ep%din", i / 2);
			ep->direction = EP_DIR_IN;
		} else {
			snprintf(name, sizeof(name), "ep%dout", i / 2);
			ep->direction = EP_DIR_OUT;
		}
		ep->udc = udc;
		strncpy(ep->name, name, sizeof(ep->name));
		ep->ep.name = ep->name;

		ep->ep.ops = &mv_ep_ops;
		ep->stopped = 0;
		ep->ep.maxpacket = (unsigned short) ~0;
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

int usb_gadget_probe_driver(struct usb_gadget_driver *driver,
		int (*bind)(struct usb_gadget *))
{
	struct mv_udc *udc = the_controller;
	int retval = 0;
	unsigned long flags;

	if (!udc)
		return -ENODEV;

	if (udc->driver)
		return -EBUSY;

	spin_lock_irqsave(&udc->lock, flags);

	/* hook up the driver ... */
	driver->driver.bus = NULL;
	udc->driver = driver;
	udc->gadget.dev.driver = &driver->driver;

	udc->usb_state = USB_STATE_ATTACHED;
	udc->ep0_state = WAIT_FOR_SETUP;
	udc->ep0_dir = USB_DIR_OUT;

	spin_unlock_irqrestore(&udc->lock, flags);

	retval = bind(&udc->gadget);
	if (retval) {
		dev_err(&udc->dev->dev, "bind to driver %s --> %d\n",
				driver->driver.name, retval);
		udc->driver = NULL;
		udc->gadget.dev.driver = NULL;
		return retval;
	}
	udc_reset(udc);
	ep0_reset(udc);
	udc_start(udc);

	return 0;
}
EXPORT_SYMBOL(usb_gadget_probe_driver);

int usb_gadget_unregister_driver(struct usb_gadget_driver *driver)
{
	struct mv_udc *udc = the_controller;
	unsigned long flags;

	if (!udc)
		return -ENODEV;

	udc_stop(udc);

	spin_lock_irqsave(&udc->lock, flags);

	/* stop all usb activities */
	udc->gadget.speed = USB_SPEED_UNKNOWN;
	stop_activity(udc, driver);
	spin_unlock_irqrestore(&udc->lock, flags);

	/* unbind gadget driver */
	driver->unbind(&udc->gadget);
	udc->gadget.dev.driver = NULL;
	udc->driver = NULL;

	return 0;
}
EXPORT_SYMBOL(usb_gadget_unregister_driver);

static int
udc_prime_status(struct mv_udc *udc, u8 direction, u16 status, bool empty)
{
	int retval = 0;
	struct mv_req *req;
	struct mv_ep *ep;

	ep = &udc->eps[0];
	udc->ep0_dir = direction;

	req = udc->status_req;

	/* fill in the reqest structure */
	if (empty == false) {
		*((u16 *) req->req.buf) = cpu_to_le16(status);
		req->req.length = 2;
	} else
		req->req.length = 0;

	req->ep = ep;
	req->req.status = -EINPROGRESS;
	req->req.actual = 0;
	req->req.complete = NULL;
	req->dtd_count = 0;

	/* prime the data phase */
	if (!req_to_dtd(req))
		retval = queue_dtd(ep, req);
	else{	/* no mem */
		retval = -ENOMEM;
		goto out;
	}

	if (retval) {
		dev_err(&udc->dev->dev, "response error on GET_STATUS request\n");
		goto out;
	}

	list_add_tail(&req->queue, &ep->queue);

	return 0;
out:
	return retval;
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
	u16 status;
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
		case USB_DEVICE_TEST_MODE:
			mv_udc_testmode(udc, 0, false);
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
	else
		udc->ep0_state = DATA_STATE_XMIT;
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
				&& udc->gadget.speed != USB_SPEED_HIGH)
				goto out;
			if (udc->usb_state == USB_STATE_CONFIGURED
				|| udc->usb_state == USB_STATE_ADDRESS
				|| udc->usb_state == USB_STATE_DEFAULT)
				mv_udc_testmode(udc,
					setup->wIndex & 0xFF00, true);
			else
				goto out;
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
{
	bool delegate = false;

	nuke(&udc->eps[ep_num * 2 + EP_DIR_OUT], -ESHUTDOWN);

	dev_dbg(&udc->dev->dev, "SETUP %02x.%02x v%04x i%04x l%04x\n",
			setup->bRequestType, setup->bRequest,
			setup->wValue, setup->wIndex, setup->wLength);
	/* We process some stardard setup requests here */
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
	temp = readl(&udc->op_regs->epsetupstat);
	writel(temp | (1 << ep_num), &udc->op_regs->epsetupstat);

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

void irq_process_reset(struct mv_udc *udc)
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
		stop_activity(udc, udc->driver);
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

	spin_lock(&udc->lock);

	status = readl(&udc->op_regs->usbsts);
	intr = readl(&udc->op_regs->usbintr);
	status &= intr;

	if (status == 0) {
		spin_unlock(&udc->lock);
		return IRQ_NONE;
	}

	/* Clear all the interrupts occured */
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

/* release device structure */
static void gadget_release(struct device *_dev)
{
	struct mv_udc *udc = the_controller;

	complete(udc->done);
	kfree(udc);
}

static int mv_udc_remove(struct platform_device *dev)
{
	struct mv_udc *udc = the_controller;

	DECLARE_COMPLETION(done);

	udc->done = &done;

	/* free memory allocated in probe */
	if (udc->dtd_pool)
		dma_pool_destroy(udc->dtd_pool);

	if (udc->ep_dqh)
		dma_free_coherent(&dev->dev, udc->ep_dqh_size,
			udc->ep_dqh, udc->ep_dqh_dma);

	kfree(udc->eps);

	if (udc->irq)
		free_irq(udc->irq, &dev->dev);

	if (udc->cap_regs)
		iounmap(udc->cap_regs);
	udc->cap_regs = NULL;

	if (udc->phy_regs)
		iounmap((void *)udc->phy_regs);
	udc->phy_regs = 0;

	if (udc->status_req) {
		kfree(udc->status_req->req.buf);
		kfree(udc->status_req);
	}

	device_unregister(&udc->gadget.dev);

	/* free dev, wait for the release() finished */
	wait_for_completion(&done);

	the_controller = NULL;

	return 0;
}

int mv_udc_probe(struct platform_device *dev)
{
	struct mv_udc *udc;
	int retval = 0;
	struct resource *r;
	size_t size;

	udc = kzalloc(sizeof *udc, GFP_KERNEL);
	if (udc == NULL) {
		dev_err(&dev->dev, "failed to allocate memory for udc\n");
		retval = -ENOMEM;
		goto error;
	}

	spin_lock_init(&udc->lock);

	udc->dev = dev;

	udc->clk = clk_get(&dev->dev, "U2OCLK");
	if (IS_ERR(udc->clk)) {
		retval = PTR_ERR(udc->clk);
		goto error;
	}

	r = platform_get_resource_byname(udc->dev, IORESOURCE_MEM, "u2o");
	if (r == NULL) {
		dev_err(&dev->dev, "no I/O memory resource defined\n");
		retval = -ENODEV;
		goto error;
	}

	udc->cap_regs = (struct mv_cap_regs __iomem *)
		ioremap(r->start, resource_size(r));
	if (udc->cap_regs == NULL) {
		dev_err(&dev->dev, "failed to map I/O memory\n");
		retval = -EBUSY;
		goto error;
	}

	r = platform_get_resource_byname(udc->dev, IORESOURCE_MEM, "u2ophy");
	if (r == NULL) {
		dev_err(&dev->dev, "no phy I/O memory resource defined\n");
		retval = -ENODEV;
		goto error;
	}

	udc->phy_regs = (unsigned int)ioremap(r->start, resource_size(r));
	if (udc->phy_regs == 0) {
		dev_err(&dev->dev, "failed to map phy I/O memory\n");
		retval = -EBUSY;
		goto error;
	}

	/* we will acces controller register, so enable the clk */
	clk_enable(udc->clk);
	retval = mv_udc_phy_init(udc->phy_regs);
	if (retval) {
		dev_err(&dev->dev, "phy initialization error %d\n", retval);
		goto error;
	}

	udc->op_regs = (struct mv_op_regs __iomem *)((u32)udc->cap_regs
		+ (readl(&udc->cap_regs->caplength_hciversion)
			& CAPLENGTH_MASK));
	udc->max_eps = readl(&udc->cap_regs->dccparams) & DCCPARAMS_DEN_MASK;

	size = udc->max_eps * sizeof(struct mv_dqh) *2;
	size = (size + DQH_ALIGNMENT - 1) & ~(DQH_ALIGNMENT - 1);
	udc->ep_dqh = dma_alloc_coherent(&dev->dev, size,
					&udc->ep_dqh_dma, GFP_KERNEL);

	if (udc->ep_dqh == NULL) {
		dev_err(&dev->dev, "allocate dQH memory failed\n");
		retval = -ENOMEM;
		goto error;
	}
	udc->ep_dqh_size = size;

	/* create dTD dma_pool resource */
	udc->dtd_pool = dma_pool_create("mv_dtd",
			&dev->dev,
			sizeof(struct mv_dtd),
			DTD_ALIGNMENT,
			DMA_BOUNDARY);

	if (!udc->dtd_pool) {
		retval = -ENOMEM;
		goto error;
	}

	size = udc->max_eps * sizeof(struct mv_ep) *2;
	udc->eps = kzalloc(size, GFP_KERNEL);
	if (udc->eps == NULL) {
		dev_err(&dev->dev, "allocate ep memory failed\n");
		retval = -ENOMEM;
		goto error;
	}

	/* initialize ep0 status request structure */
	udc->status_req = kzalloc(sizeof(struct mv_req), GFP_KERNEL);
	if (!udc->status_req) {
		dev_err(&dev->dev, "allocate status_req memory failed\n");
		retval = -ENOMEM;
		goto error;
	}
	INIT_LIST_HEAD(&udc->status_req->queue);

	/* allocate a small amount of memory to get valid address */
	udc->status_req->req.buf = kzalloc(8, GFP_KERNEL);
	udc->status_req->req.dma = virt_to_phys(udc->status_req->req.buf);

	udc->resume_state = USB_STATE_NOTATTACHED;
	udc->usb_state = USB_STATE_POWERED;
	udc->ep0_dir = EP_DIR_OUT;
	udc->remote_wakeup = 0;

	r = platform_get_resource(udc->dev, IORESOURCE_IRQ, 0);
	if (r == NULL) {
		dev_err(&dev->dev, "no IRQ resource defined\n");
		retval = -ENODEV;
		goto error;
	}
	udc->irq = r->start;
	if (request_irq(udc->irq, mv_udc_irq,
		IRQF_DISABLED | IRQF_SHARED, driver_name, udc)) {
		dev_err(&dev->dev, "Request irq %d for UDC failed\n",
			udc->irq);
		retval = -ENODEV;
		goto error;
	}

	/* initialize gadget structure */
	udc->gadget.ops = &mv_ops;	/* usb_gadget_ops */
	udc->gadget.ep0 = &udc->eps[0].ep;	/* gadget ep0 */
	INIT_LIST_HEAD(&udc->gadget.ep_list);	/* ep_list */
	udc->gadget.speed = USB_SPEED_UNKNOWN;	/* speed */
	udc->gadget.is_dualspeed = 1;		/* support dual speed */

	/* the "gadget" abstracts/virtualizes the controller */
	dev_set_name(&udc->gadget.dev, "gadget");
	udc->gadget.dev.parent = &dev->dev;
	udc->gadget.dev.dma_mask = dev->dev.dma_mask;
	udc->gadget.dev.release = gadget_release;
	udc->gadget.name = driver_name;		/* gadget name */

	retval = device_register(&udc->gadget.dev);
	if (retval)
		goto error;

	eps_init(udc);

	the_controller = udc;

	goto out;
error:
	if (udc)
		mv_udc_remove(udc->dev);
out:
	return retval;
}

#ifdef CONFIG_PM
static int mv_udc_suspend(struct platform_device *_dev, pm_message_t state)
{
	struct mv_udc *udc = the_controller;

	udc_stop(udc);

	return 0;
}

static int mv_udc_resume(struct platform_device *_dev)
{
	struct mv_udc *udc = the_controller;
	int retval;

	retval = mv_udc_phy_init(udc->phy_regs);
	if (retval) {
		dev_err(_dev, "phy initialization error %d\n", retval);
		goto error;
	}
	udc_reset(udc);
	ep0_reset(udc);
	udc_start(udc);

	return 0;
}

static const struct dev_pm_ops mv_udc_pm_ops = {
	.suspend	= mv_udc_suspend,
	.resume		= mv_udc_resume,
};
#endif

static struct platform_driver udc_driver = {
	.probe		= mv_udc_probe,
	.remove		= __exit_p(mv_udc_remove),
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "pxa-u2o",
#ifdef CONFIG_PM
		.pm	= mv_udc_pm_ops,
#endif
	},
};


MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR("Chao Xie <chao.xie@marvell.com>");
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL");


static int __init init(void)
{
	return platform_driver_register(&udc_driver);
}
module_init(init);


static void __exit cleanup(void)
{
	platform_driver_unregister(&udc_driver);
}
module_exit(cleanup);

