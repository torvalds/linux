/*
 * Intel Langwell USB Device Controller driver
 * Copyright (C) 2008-2009, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */


/* #undef	DEBUG */
/* #undef	VERBOSE */

#if defined(CONFIG_USB_LANGWELL_OTG)
#define	OTG_TRANSCEIVER
#endif


#include <linux/module.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
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
#include <asm/system.h>
#include <asm/unaligned.h>

#include "langwell_udc.h"


#define	DRIVER_DESC		"Intel Langwell USB Device Controller driver"
#define	DRIVER_VERSION		"16 May 2009"

static const char driver_name[] = "langwell_udc";
static const char driver_desc[] = DRIVER_DESC;


/* controller device global variable */
static struct langwell_udc	*the_controller;

/* for endpoint 0 operations */
static const struct usb_endpoint_descriptor
langwell_ep0_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	0,
	.bmAttributes =		USB_ENDPOINT_XFER_CONTROL,
	.wMaxPacketSize =	EP0_MAX_PKT_SIZE,
};


/*-------------------------------------------------------------------------*/
/* debugging */

#ifdef	DEBUG
#define	DBG(dev, fmt, args...) \
	pr_debug("%s %s: " fmt , driver_name, \
			pci_name(dev->pdev), ## args)
#else
#define	DBG(dev, fmt, args...) \
	do { } while (0)
#endif /* DEBUG */


#ifdef	VERBOSE
#define	VDBG DBG
#else
#define	VDBG(dev, fmt, args...) \
	do { } while (0)
#endif	/* VERBOSE */


#define	ERROR(dev, fmt, args...) \
	pr_err("%s %s: " fmt , driver_name, \
			pci_name(dev->pdev), ## args)

#define	WARNING(dev, fmt, args...) \
	pr_warning("%s %s: " fmt , driver_name, \
			pci_name(dev->pdev), ## args)

#define	INFO(dev, fmt, args...) \
	pr_info("%s %s: " fmt , driver_name, \
			pci_name(dev->pdev), ## args)


#ifdef	VERBOSE
static inline void print_all_registers(struct langwell_udc *dev)
{
	int	i;

	/* Capability Registers */
	printk(KERN_DEBUG "Capability Registers (offset: "
			"0x%04x, length: 0x%08x)\n",
			CAP_REG_OFFSET,
			(u32)sizeof(struct langwell_cap_regs));
	printk(KERN_DEBUG "caplength=0x%02x\n",
			readb(&dev->cap_regs->caplength));
	printk(KERN_DEBUG "hciversion=0x%04x\n",
			readw(&dev->cap_regs->hciversion));
	printk(KERN_DEBUG "hcsparams=0x%08x\n",
			readl(&dev->cap_regs->hcsparams));
	printk(KERN_DEBUG "hccparams=0x%08x\n",
			readl(&dev->cap_regs->hccparams));
	printk(KERN_DEBUG "dciversion=0x%04x\n",
			readw(&dev->cap_regs->dciversion));
	printk(KERN_DEBUG "dccparams=0x%08x\n",
			readl(&dev->cap_regs->dccparams));

	/* Operational Registers */
	printk(KERN_DEBUG "Operational Registers (offset: "
			"0x%04x, length: 0x%08x)\n",
			OP_REG_OFFSET,
			(u32)sizeof(struct langwell_op_regs));
	printk(KERN_DEBUG "extsts=0x%08x\n",
			readl(&dev->op_regs->extsts));
	printk(KERN_DEBUG "extintr=0x%08x\n",
			readl(&dev->op_regs->extintr));
	printk(KERN_DEBUG "usbcmd=0x%08x\n",
			readl(&dev->op_regs->usbcmd));
	printk(KERN_DEBUG "usbsts=0x%08x\n",
			readl(&dev->op_regs->usbsts));
	printk(KERN_DEBUG "usbintr=0x%08x\n",
			readl(&dev->op_regs->usbintr));
	printk(KERN_DEBUG "frindex=0x%08x\n",
			readl(&dev->op_regs->frindex));
	printk(KERN_DEBUG "ctrldssegment=0x%08x\n",
			readl(&dev->op_regs->ctrldssegment));
	printk(KERN_DEBUG "deviceaddr=0x%08x\n",
			readl(&dev->op_regs->deviceaddr));
	printk(KERN_DEBUG "endpointlistaddr=0x%08x\n",
			readl(&dev->op_regs->endpointlistaddr));
	printk(KERN_DEBUG "ttctrl=0x%08x\n",
			readl(&dev->op_regs->ttctrl));
	printk(KERN_DEBUG "burstsize=0x%08x\n",
			readl(&dev->op_regs->burstsize));
	printk(KERN_DEBUG "txfilltuning=0x%08x\n",
			readl(&dev->op_regs->txfilltuning));
	printk(KERN_DEBUG "txttfilltuning=0x%08x\n",
			readl(&dev->op_regs->txttfilltuning));
	printk(KERN_DEBUG "ic_usb=0x%08x\n",
			readl(&dev->op_regs->ic_usb));
	printk(KERN_DEBUG "ulpi_viewport=0x%08x\n",
			readl(&dev->op_regs->ulpi_viewport));
	printk(KERN_DEBUG "configflag=0x%08x\n",
			readl(&dev->op_regs->configflag));
	printk(KERN_DEBUG "portsc1=0x%08x\n",
			readl(&dev->op_regs->portsc1));
	printk(KERN_DEBUG "devlc=0x%08x\n",
			readl(&dev->op_regs->devlc));
	printk(KERN_DEBUG "otgsc=0x%08x\n",
			readl(&dev->op_regs->otgsc));
	printk(KERN_DEBUG "usbmode=0x%08x\n",
			readl(&dev->op_regs->usbmode));
	printk(KERN_DEBUG "endptnak=0x%08x\n",
			readl(&dev->op_regs->endptnak));
	printk(KERN_DEBUG "endptnaken=0x%08x\n",
			readl(&dev->op_regs->endptnaken));
	printk(KERN_DEBUG "endptsetupstat=0x%08x\n",
			readl(&dev->op_regs->endptsetupstat));
	printk(KERN_DEBUG "endptprime=0x%08x\n",
			readl(&dev->op_regs->endptprime));
	printk(KERN_DEBUG "endptflush=0x%08x\n",
			readl(&dev->op_regs->endptflush));
	printk(KERN_DEBUG "endptstat=0x%08x\n",
			readl(&dev->op_regs->endptstat));
	printk(KERN_DEBUG "endptcomplete=0x%08x\n",
			readl(&dev->op_regs->endptcomplete));

	for (i = 0; i < dev->ep_max / 2; i++) {
		printk(KERN_DEBUG "endptctrl[%d]=0x%08x\n",
				i, readl(&dev->op_regs->endptctrl[i]));
	}
}
#endif /* VERBOSE */


/*-------------------------------------------------------------------------*/

#define	DIR_STRING(bAddress)	(((bAddress) & USB_DIR_IN) ? "in" : "out")

#define is_in(ep)	(((ep)->ep_num == 0) ? ((ep)->dev->ep0_dir == \
			USB_DIR_IN) : ((ep)->desc->bEndpointAddress \
			& USB_DIR_IN) == USB_DIR_IN)


#ifdef	DEBUG
static char *type_string(u8 bmAttributes)
{
	switch ((bmAttributes) & USB_ENDPOINT_XFERTYPE_MASK) {
	case USB_ENDPOINT_XFER_BULK:
		return "bulk";
	case USB_ENDPOINT_XFER_ISOC:
		return "iso";
	case USB_ENDPOINT_XFER_INT:
		return "int";
	};

	return "control";
}
#endif


/* configure endpoint control registers */
static void ep_reset(struct langwell_ep *ep, unsigned char ep_num,
		unsigned char is_in, unsigned char ep_type)
{
	struct langwell_udc	*dev;
	u32			endptctrl;

	dev = ep->dev;
	VDBG(dev, "---> %s()\n", __func__);

	endptctrl = readl(&dev->op_regs->endptctrl[ep_num]);
	if (is_in) {	/* TX */
		if (ep_num)
			endptctrl |= EPCTRL_TXR;
		endptctrl |= EPCTRL_TXE;
		endptctrl |= ep_type << EPCTRL_TXT_SHIFT;
	} else {	/* RX */
		if (ep_num)
			endptctrl |= EPCTRL_RXR;
		endptctrl |= EPCTRL_RXE;
		endptctrl |= ep_type << EPCTRL_RXT_SHIFT;
	}

	writel(endptctrl, &dev->op_regs->endptctrl[ep_num]);

	VDBG(dev, "<--- %s()\n", __func__);
}


/* reset ep0 dQH and endptctrl */
static void ep0_reset(struct langwell_udc *dev)
{
	struct langwell_ep	*ep;
	int			i;

	VDBG(dev, "---> %s()\n", __func__);

	/* ep0 in and out */
	for (i = 0; i < 2; i++) {
		ep = &dev->ep[i];
		ep->dev = dev;

		/* ep0 dQH */
		ep->dqh = &dev->ep_dqh[i];

		/* configure ep0 endpoint capabilities in dQH */
		ep->dqh->dqh_ios = 1;
		ep->dqh->dqh_mpl = EP0_MAX_PKT_SIZE;

		/* FIXME: enable ep0-in HW zero length termination select */
		if (is_in(ep))
			ep->dqh->dqh_zlt = 0;
		ep->dqh->dqh_mult = 0;

		/* configure ep0 control registers */
		ep_reset(&dev->ep[0], 0, i, USB_ENDPOINT_XFER_CONTROL);
	}

	VDBG(dev, "<--- %s()\n", __func__);
	return;
}


/*-------------------------------------------------------------------------*/

/* endpoints operations */

/* configure endpoint, making it usable */
static int langwell_ep_enable(struct usb_ep *_ep,
		const struct usb_endpoint_descriptor *desc)
{
	struct langwell_udc	*dev;
	struct langwell_ep	*ep;
	u16			max = 0;
	unsigned long		flags;
	int			retval = 0;
	unsigned char		zlt, ios = 0, mult = 0;

	ep = container_of(_ep, struct langwell_ep, ep);
	dev = ep->dev;
	VDBG(dev, "---> %s()\n", __func__);

	if (!_ep || !desc || ep->desc
			|| desc->bDescriptorType != USB_DT_ENDPOINT)
		return -EINVAL;

	if (!dev->driver || dev->gadget.speed == USB_SPEED_UNKNOWN)
		return -ESHUTDOWN;

	max = le16_to_cpu(desc->wMaxPacketSize);

	/*
	 * disable HW zero length termination select
	 * driver handles zero length packet through req->req.zero
	 */
	zlt = 1;

	/*
	 * sanity check type, direction, address, and then
	 * initialize the endpoint capabilities fields in dQH
	 */
	switch (desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) {
	case USB_ENDPOINT_XFER_CONTROL:
		ios = 1;
		break;
	case USB_ENDPOINT_XFER_BULK:
		if ((dev->gadget.speed == USB_SPEED_HIGH
					&& max != 512)
				|| (dev->gadget.speed == USB_SPEED_FULL
					&& max > 64)) {
			goto done;
		}
		break;
	case USB_ENDPOINT_XFER_INT:
		if (strstr(ep->ep.name, "-iso")) /* bulk is ok */
			goto done;

		switch (dev->gadget.speed) {
		case USB_SPEED_HIGH:
			if (max <= 1024)
				break;
		case USB_SPEED_FULL:
			if (max <= 64)
				break;
		default:
			if (max <= 8)
				break;
			goto done;
		}
		break;
	case USB_ENDPOINT_XFER_ISOC:
		if (strstr(ep->ep.name, "-bulk")
				|| strstr(ep->ep.name, "-int"))
			goto done;

		switch (dev->gadget.speed) {
		case USB_SPEED_HIGH:
			if (max <= 1024)
				break;
		case USB_SPEED_FULL:
			if (max <= 1023)
				break;
		default:
			goto done;
		}
		/*
		 * FIXME:
		 * calculate transactions needed for high bandwidth iso
		 */
		mult = (unsigned char)(1 + ((max >> 11) & 0x03));
		max = max & 0x8ff;	/* bit 0~10 */
		/* 3 transactions at most */
		if (mult > 3)
			goto done;
		break;
	default:
		goto done;
	}

	spin_lock_irqsave(&dev->lock, flags);

	/* configure endpoint capabilities in dQH */
	ep->dqh->dqh_ios = ios;
	ep->dqh->dqh_mpl = cpu_to_le16(max);
	ep->dqh->dqh_zlt = zlt;
	ep->dqh->dqh_mult = mult;

	ep->ep.maxpacket = max;
	ep->desc = desc;
	ep->stopped = 0;
	ep->ep_num = desc->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK;

	/* ep_type */
	ep->ep_type = desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK;

	/* configure endpoint control registers */
	ep_reset(ep, ep->ep_num, is_in(ep), ep->ep_type);

	DBG(dev, "enabled %s (ep%d%s-%s), max %04x\n",
			_ep->name,
			ep->ep_num,
			DIR_STRING(desc->bEndpointAddress),
			type_string(desc->bmAttributes),
			max);

	spin_unlock_irqrestore(&dev->lock, flags);
done:
	VDBG(dev, "<--- %s()\n", __func__);
	return retval;
}


/*-------------------------------------------------------------------------*/

/* retire a request */
static void done(struct langwell_ep *ep, struct langwell_request *req,
		int status)
{
	struct langwell_udc	*dev = ep->dev;
	unsigned		stopped = ep->stopped;
	struct langwell_dtd	*curr_dtd, *next_dtd;
	int			i;

	VDBG(dev, "---> %s()\n", __func__);

	/* remove the req from ep->queue */
	list_del_init(&req->queue);

	if (req->req.status == -EINPROGRESS)
		req->req.status = status;
	else
		status = req->req.status;

	/* free dTD for the request */
	next_dtd = req->head;
	for (i = 0; i < req->dtd_count; i++) {
		curr_dtd = next_dtd;
		if (i != req->dtd_count - 1)
			next_dtd = curr_dtd->next_dtd_virt;
		dma_pool_free(dev->dtd_pool, curr_dtd, curr_dtd->dtd_dma);
	}

	if (req->mapped) {
		dma_unmap_single(&dev->pdev->dev, req->req.dma, req->req.length,
			is_in(ep) ? PCI_DMA_TODEVICE : PCI_DMA_FROMDEVICE);
		req->req.dma = DMA_ADDR_INVALID;
		req->mapped = 0;
	} else
		dma_sync_single_for_cpu(&dev->pdev->dev, req->req.dma,
				req->req.length,
				is_in(ep) ? DMA_TO_DEVICE : DMA_FROM_DEVICE);

	if (status != -ESHUTDOWN)
		DBG(dev, "complete %s, req %p, stat %d, len %u/%u\n",
			ep->ep.name, &req->req, status,
			req->req.actual, req->req.length);

	/* don't modify queue heads during completion callback */
	ep->stopped = 1;

	spin_unlock(&dev->lock);
	/* complete routine from gadget driver */
	if (req->req.complete)
		req->req.complete(&ep->ep, &req->req);

	spin_lock(&dev->lock);
	ep->stopped = stopped;

	VDBG(dev, "<--- %s()\n", __func__);
}


static void langwell_ep_fifo_flush(struct usb_ep *_ep);

/* delete all endpoint requests, called with spinlock held */
static void nuke(struct langwell_ep *ep, int status)
{
	/* called with spinlock held */
	ep->stopped = 1;

	/* endpoint fifo flush */
	if (&ep->ep && ep->desc)
		langwell_ep_fifo_flush(&ep->ep);

	while (!list_empty(&ep->queue)) {
		struct langwell_request	*req = NULL;
		req = list_entry(ep->queue.next, struct langwell_request,
				queue);
		done(ep, req, status);
	}
}


/*-------------------------------------------------------------------------*/

/* endpoint is no longer usable */
static int langwell_ep_disable(struct usb_ep *_ep)
{
	struct langwell_ep	*ep;
	unsigned long		flags;
	struct langwell_udc	*dev;
	int			ep_num;
	u32			endptctrl;

	ep = container_of(_ep, struct langwell_ep, ep);
	dev = ep->dev;
	VDBG(dev, "---> %s()\n", __func__);

	if (!_ep || !ep->desc)
		return -EINVAL;

	spin_lock_irqsave(&dev->lock, flags);

	/* disable endpoint control register */
	ep_num = ep->ep_num;
	endptctrl = readl(&dev->op_regs->endptctrl[ep_num]);
	if (is_in(ep))
		endptctrl &= ~EPCTRL_TXE;
	else
		endptctrl &= ~EPCTRL_RXE;
	writel(endptctrl, &dev->op_regs->endptctrl[ep_num]);

	/* nuke all pending requests (does flush) */
	nuke(ep, -ESHUTDOWN);

	ep->desc = NULL;
	ep->stopped = 1;

	spin_unlock_irqrestore(&dev->lock, flags);

	DBG(dev, "disabled %s\n", _ep->name);
	VDBG(dev, "<--- %s()\n", __func__);

	return 0;
}


/* allocate a request object to use with this endpoint */
static struct usb_request *langwell_alloc_request(struct usb_ep *_ep,
		gfp_t gfp_flags)
{
	struct langwell_ep	*ep;
	struct langwell_udc	*dev;
	struct langwell_request	*req = NULL;

	if (!_ep)
		return NULL;

	ep = container_of(_ep, struct langwell_ep, ep);
	dev = ep->dev;
	VDBG(dev, "---> %s()\n", __func__);

	req = kzalloc(sizeof(*req), gfp_flags);
	if (!req)
		return NULL;

	req->req.dma = DMA_ADDR_INVALID;
	INIT_LIST_HEAD(&req->queue);

	VDBG(dev, "alloc request for %s\n", _ep->name);
	VDBG(dev, "<--- %s()\n", __func__);
	return &req->req;
}


/* free a request object */
static void langwell_free_request(struct usb_ep *_ep,
		struct usb_request *_req)
{
	struct langwell_ep	*ep;
	struct langwell_udc	*dev;
	struct langwell_request	*req = NULL;

	ep = container_of(_ep, struct langwell_ep, ep);
	dev = ep->dev;
	VDBG(dev, "---> %s()\n", __func__);

	if (!_ep || !_req)
		return;

	req = container_of(_req, struct langwell_request, req);
	WARN_ON(!list_empty(&req->queue));

	if (_req)
		kfree(req);

	VDBG(dev, "free request for %s\n", _ep->name);
	VDBG(dev, "<--- %s()\n", __func__);
}


/*-------------------------------------------------------------------------*/

/* queue dTD and PRIME endpoint */
static int queue_dtd(struct langwell_ep *ep, struct langwell_request *req)
{
	u32			bit_mask, usbcmd, endptstat, dtd_dma;
	u8			dtd_status;
	int			i;
	struct langwell_dqh	*dqh;
	struct langwell_udc	*dev;

	dev = ep->dev;
	VDBG(dev, "---> %s()\n", __func__);

	i = ep->ep_num * 2 + is_in(ep);
	dqh = &dev->ep_dqh[i];

	if (ep->ep_num)
		VDBG(dev, "%s\n", ep->name);
	else
		/* ep0 */
		VDBG(dev, "%s-%s\n", ep->name, is_in(ep) ? "in" : "out");

	VDBG(dev, "ep_dqh[%d] addr: 0x%08x\n", i, (u32)&(dev->ep_dqh[i]));

	bit_mask = is_in(ep) ?
		(1 << (ep->ep_num + 16)) : (1 << (ep->ep_num));

	VDBG(dev, "bit_mask = 0x%08x\n", bit_mask);

	/* check if the pipe is empty */
	if (!(list_empty(&ep->queue))) {
		/* add dTD to the end of linked list */
		struct langwell_request	*lastreq;
		lastreq = list_entry(ep->queue.prev,
				struct langwell_request, queue);

		lastreq->tail->dtd_next =
			cpu_to_le32(req->head->dtd_dma & DTD_NEXT_MASK);

		/* read prime bit, if 1 goto out */
		if (readl(&dev->op_regs->endptprime) & bit_mask)
			goto out;

		do {
			/* set ATDTW bit in USBCMD */
			usbcmd = readl(&dev->op_regs->usbcmd);
			writel(usbcmd | CMD_ATDTW, &dev->op_regs->usbcmd);

			/* read correct status bit */
			endptstat = readl(&dev->op_regs->endptstat) & bit_mask;

		} while (!(readl(&dev->op_regs->usbcmd) & CMD_ATDTW));

		/* write ATDTW bit to 0 */
		usbcmd = readl(&dev->op_regs->usbcmd);
		writel(usbcmd & ~CMD_ATDTW, &dev->op_regs->usbcmd);

		if (endptstat)
			goto out;
	}

	/* write dQH next pointer and terminate bit to 0 */
	dtd_dma = req->head->dtd_dma & DTD_NEXT_MASK;
	dqh->dtd_next = cpu_to_le32(dtd_dma);

	/* clear active and halt bit */
	dtd_status = (u8) ~(DTD_STS_ACTIVE | DTD_STS_HALTED);
	dqh->dtd_status &= dtd_status;
	VDBG(dev, "dqh->dtd_status = 0x%x\n", dqh->dtd_status);

	/* write 1 to endptprime register to PRIME endpoint */
	bit_mask = is_in(ep) ? (1 << (ep->ep_num + 16)) : (1 << ep->ep_num);
	VDBG(dev, "endprime bit_mask = 0x%08x\n", bit_mask);
	writel(bit_mask, &dev->op_regs->endptprime);
out:
	VDBG(dev, "<--- %s()\n", __func__);
	return 0;
}


/* fill in the dTD structure to build a transfer descriptor */
static struct langwell_dtd *build_dtd(struct langwell_request *req,
		unsigned *length, dma_addr_t *dma, int *is_last)
{
	u32			 buf_ptr;
	struct langwell_dtd	*dtd;
	struct langwell_udc	*dev;
	int			i;

	dev = req->ep->dev;
	VDBG(dev, "---> %s()\n", __func__);

	/* the maximum transfer length, up to 16k bytes */
	*length = min(req->req.length - req->req.actual,
			(unsigned)DTD_MAX_TRANSFER_LENGTH);

	/* create dTD dma_pool resource */
	dtd = dma_pool_alloc(dev->dtd_pool, GFP_KERNEL, dma);
	if (dtd == NULL)
		return dtd;
	dtd->dtd_dma = *dma;

	/* initialize buffer page pointers */
	buf_ptr = (u32)(req->req.dma + req->req.actual);
	for (i = 0; i < 5; i++)
		dtd->dtd_buf[i] = cpu_to_le32(buf_ptr + i * PAGE_SIZE);

	req->req.actual += *length;

	/* fill in total bytes with transfer size */
	dtd->dtd_total = cpu_to_le16(*length);
	VDBG(dev, "dtd->dtd_total = %d\n", dtd->dtd_total);

	/* set is_last flag if req->req.zero is set or not */
	if (req->req.zero) {
		if (*length == 0 || (*length % req->ep->ep.maxpacket) != 0)
			*is_last = 1;
		else
			*is_last = 0;
	} else if (req->req.length == req->req.actual) {
		*is_last = 1;
	} else
		*is_last = 0;

	if (*is_last == 0)
		VDBG(dev, "multi-dtd request!\n");

	/* set interrupt on complete bit for the last dTD */
	if (*is_last && !req->req.no_interrupt)
		dtd->dtd_ioc = 1;

	/* set multiplier override 0 for non-ISO and non-TX endpoint */
	dtd->dtd_multo = 0;

	/* set the active bit of status field to 1 */
	dtd->dtd_status = DTD_STS_ACTIVE;
	VDBG(dev, "dtd->dtd_status = 0x%02x\n", dtd->dtd_status);

	VDBG(dev, "length = %d, dma addr= 0x%08x\n", *length, (int)*dma);
	VDBG(dev, "<--- %s()\n", __func__);
	return dtd;
}


/* generate dTD linked list for a request */
static int req_to_dtd(struct langwell_request *req)
{
	unsigned		count;
	int			is_last, is_first = 1;
	struct langwell_dtd	*dtd, *last_dtd = NULL;
	struct langwell_udc	*dev;
	dma_addr_t		dma;

	dev = req->ep->dev;
	VDBG(dev, "---> %s()\n", __func__);
	do {
		dtd = build_dtd(req, &count, &dma, &is_last);
		if (dtd == NULL)
			return -ENOMEM;

		if (is_first) {
			is_first = 0;
			req->head = dtd;
		} else {
			last_dtd->dtd_next = cpu_to_le32(dma);
			last_dtd->next_dtd_virt = dtd;
		}
		last_dtd = dtd;
		req->dtd_count++;
	} while (!is_last);

	/* set terminate bit to 1 for the last dTD */
	dtd->dtd_next = DTD_TERM;

	req->tail = dtd;

	VDBG(dev, "<--- %s()\n", __func__);
	return 0;
}

/*-------------------------------------------------------------------------*/

/* queue (submits) an I/O requests to an endpoint */
static int langwell_ep_queue(struct usb_ep *_ep, struct usb_request *_req,
		gfp_t gfp_flags)
{
	struct langwell_request	*req;
	struct langwell_ep	*ep;
	struct langwell_udc	*dev;
	unsigned long		flags;
	int			is_iso = 0, zlflag = 0;

	/* always require a cpu-view buffer */
	req = container_of(_req, struct langwell_request, req);
	ep = container_of(_ep, struct langwell_ep, ep);

	if (!_req || !_req->complete || !_req->buf
			|| !list_empty(&req->queue)) {
		return -EINVAL;
	}

	if (unlikely(!_ep || !ep->desc))
		return -EINVAL;

	dev = ep->dev;
	req->ep = ep;
	VDBG(dev, "---> %s()\n", __func__);

	if (ep->desc->bmAttributes == USB_ENDPOINT_XFER_ISOC) {
		if (req->req.length > ep->ep.maxpacket)
			return -EMSGSIZE;
		is_iso = 1;
	}

	if (unlikely(!dev->driver || dev->gadget.speed == USB_SPEED_UNKNOWN))
		return -ESHUTDOWN;

	/* set up dma mapping in case the caller didn't */
	if (_req->dma == DMA_ADDR_INVALID) {
		/* WORKAROUND: WARN_ON(size == 0) */
		if (_req->length == 0) {
			VDBG(dev, "req->length: 0->1\n");
			zlflag = 1;
			_req->length++;
		}

		_req->dma = dma_map_single(&dev->pdev->dev,
				_req->buf, _req->length,
				is_in(ep) ? DMA_TO_DEVICE : DMA_FROM_DEVICE);
		if (zlflag && (_req->length == 1)) {
			VDBG(dev, "req->length: 1->0\n");
			zlflag = 0;
			_req->length = 0;
		}

		req->mapped = 1;
		VDBG(dev, "req->mapped = 1\n");
	} else {
		dma_sync_single_for_device(&dev->pdev->dev,
				_req->dma, _req->length,
				is_in(ep) ?  DMA_TO_DEVICE : DMA_FROM_DEVICE);
		req->mapped = 0;
		VDBG(dev, "req->mapped = 0\n");
	}

	DBG(dev, "%s queue req %p, len %u, buf %p, dma 0x%08x\n",
			_ep->name,
			_req, _req->length, _req->buf, _req->dma);

	_req->status = -EINPROGRESS;
	_req->actual = 0;
	req->dtd_count = 0;

	spin_lock_irqsave(&dev->lock, flags);

	/* build and put dTDs to endpoint queue */
	if (!req_to_dtd(req)) {
		queue_dtd(ep, req);
	} else {
		spin_unlock_irqrestore(&dev->lock, flags);
		return -ENOMEM;
	}

	/* update ep0 state */
	if (ep->ep_num == 0)
		dev->ep0_state = DATA_STATE_XMIT;

	if (likely(req != NULL)) {
		list_add_tail(&req->queue, &ep->queue);
		VDBG(dev, "list_add_tail() \n");
	}

	spin_unlock_irqrestore(&dev->lock, flags);

	VDBG(dev, "<--- %s()\n", __func__);
	return 0;
}


/* dequeue (cancels, unlinks) an I/O request from an endpoint */
static int langwell_ep_dequeue(struct usb_ep *_ep, struct usb_request *_req)
{
	struct langwell_ep	*ep;
	struct langwell_udc	*dev;
	struct langwell_request	*req;
	unsigned long		flags;
	int			stopped, ep_num, retval = 0;
	u32			endptctrl;

	ep = container_of(_ep, struct langwell_ep, ep);
	dev = ep->dev;
	VDBG(dev, "---> %s()\n", __func__);

	if (!_ep || !ep->desc || !_req)
		return -EINVAL;

	if (!dev->driver)
		return -ESHUTDOWN;

	spin_lock_irqsave(&dev->lock, flags);
	stopped = ep->stopped;

	/* quiesce dma while we patch the queue */
	ep->stopped = 1;
	ep_num = ep->ep_num;

	/* disable endpoint control register */
	endptctrl = readl(&dev->op_regs->endptctrl[ep_num]);
	if (is_in(ep))
		endptctrl &= ~EPCTRL_TXE;
	else
		endptctrl &= ~EPCTRL_RXE;
	writel(endptctrl, &dev->op_regs->endptctrl[ep_num]);

	/* make sure it's still queued on this endpoint */
	list_for_each_entry(req, &ep->queue, queue) {
		if (&req->req == _req)
			break;
	}

	if (&req->req != _req) {
		retval = -EINVAL;
		goto done;
	}

	/* queue head may be partially complete. */
	if (ep->queue.next == &req->queue) {
		DBG(dev, "unlink (%s) dma\n", _ep->name);
		_req->status = -ECONNRESET;
		langwell_ep_fifo_flush(&ep->ep);

		/* not the last request in endpoint queue */
		if (likely(ep->queue.next == &req->queue)) {
			struct langwell_dqh	*dqh;
			struct langwell_request	*next_req;

			dqh = ep->dqh;
			next_req = list_entry(req->queue.next,
					struct langwell_request, queue);

			/* point the dQH to the first dTD of next request */
			writel((u32) next_req->head, &dqh->dqh_current);
		}
	} else {
		struct langwell_request	*prev_req;

		prev_req = list_entry(req->queue.prev,
				struct langwell_request, queue);
		writel(readl(&req->tail->dtd_next),
				&prev_req->tail->dtd_next);
	}

	done(ep, req, -ECONNRESET);

done:
	/* enable endpoint again */
	endptctrl = readl(&dev->op_regs->endptctrl[ep_num]);
	if (is_in(ep))
		endptctrl |= EPCTRL_TXE;
	else
		endptctrl |= EPCTRL_RXE;
	writel(endptctrl, &dev->op_regs->endptctrl[ep_num]);

	ep->stopped = stopped;
	spin_unlock_irqrestore(&dev->lock, flags);

	VDBG(dev, "<--- %s()\n", __func__);
	return retval;
}


/*-------------------------------------------------------------------------*/

/* endpoint set/clear halt */
static void ep_set_halt(struct langwell_ep *ep, int value)
{
	u32			endptctrl = 0;
	int			ep_num;
	struct langwell_udc	*dev = ep->dev;
	VDBG(dev, "---> %s()\n", __func__);

	ep_num = ep->ep_num;
	endptctrl = readl(&dev->op_regs->endptctrl[ep_num]);

	/* value: 1 - set halt, 0 - clear halt */
	if (value) {
		/* set the stall bit */
		if (is_in(ep))
			endptctrl |= EPCTRL_TXS;
		else
			endptctrl |= EPCTRL_RXS;
	} else {
		/* clear the stall bit and reset data toggle */
		if (is_in(ep)) {
			endptctrl &= ~EPCTRL_TXS;
			endptctrl |= EPCTRL_TXR;
		} else {
			endptctrl &= ~EPCTRL_RXS;
			endptctrl |= EPCTRL_RXR;
		}
	}

	writel(endptctrl, &dev->op_regs->endptctrl[ep_num]);

	VDBG(dev, "<--- %s()\n", __func__);
}


/* set the endpoint halt feature */
static int langwell_ep_set_halt(struct usb_ep *_ep, int value)
{
	struct langwell_ep	*ep;
	struct langwell_udc	*dev;
	unsigned long		flags;
	int			retval = 0;

	ep = container_of(_ep, struct langwell_ep, ep);
	dev = ep->dev;

	VDBG(dev, "---> %s()\n", __func__);

	if (!_ep || !ep->desc)
		return -EINVAL;

	if (!dev->driver || dev->gadget.speed == USB_SPEED_UNKNOWN)
		return -ESHUTDOWN;

	if (ep->desc && (ep->desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
			== USB_ENDPOINT_XFER_ISOC)
		return  -EOPNOTSUPP;

	spin_lock_irqsave(&dev->lock, flags);

	/*
	 * attempt to halt IN ep will fail if any transfer requests
	 * are still queue
	 */
	if (!list_empty(&ep->queue) && is_in(ep) && value) {
		/* IN endpoint FIFO holds bytes */
		DBG(dev, "%s FIFO holds bytes\n", _ep->name);
		retval = -EAGAIN;
		goto done;
	}

	/* endpoint set/clear halt */
	if (ep->ep_num) {
		ep_set_halt(ep, value);
	} else { /* endpoint 0 */
		dev->ep0_state = WAIT_FOR_SETUP;
		dev->ep0_dir = USB_DIR_OUT;
	}
done:
	spin_unlock_irqrestore(&dev->lock, flags);
	DBG(dev, "%s %s halt\n", _ep->name, value ? "set" : "clear");
	VDBG(dev, "<--- %s()\n", __func__);
	return retval;
}


/* set the halt feature and ignores clear requests */
static int langwell_ep_set_wedge(struct usb_ep *_ep)
{
	struct langwell_ep	*ep;
	struct langwell_udc	*dev;

	ep = container_of(_ep, struct langwell_ep, ep);
	dev = ep->dev;

	VDBG(dev, "---> %s()\n", __func__);

	if (!_ep || !ep->desc)
		return -EINVAL;

	VDBG(dev, "<--- %s()\n", __func__);
	return usb_ep_set_halt(_ep);
}


/* flush contents of a fifo */
static void langwell_ep_fifo_flush(struct usb_ep *_ep)
{
	struct langwell_ep	*ep;
	struct langwell_udc	*dev;
	u32			flush_bit;
	unsigned long		timeout;

	ep = container_of(_ep, struct langwell_ep, ep);
	dev = ep->dev;

	VDBG(dev, "---> %s()\n", __func__);

	if (!_ep || !ep->desc) {
		VDBG(dev, "ep or ep->desc is NULL\n");
		VDBG(dev, "<--- %s()\n", __func__);
		return;
	}

	VDBG(dev, "%s-%s fifo flush\n", _ep->name, is_in(ep) ? "in" : "out");

	/* flush endpoint buffer */
	if (ep->ep_num == 0)
		flush_bit = (1 << 16) | 1;
	else if (is_in(ep))
		flush_bit = 1 << (ep->ep_num + 16);	/* TX */
	else
		flush_bit = 1 << ep->ep_num;		/* RX */

	/* wait until flush complete */
	timeout = jiffies + FLUSH_TIMEOUT;
	do {
		writel(flush_bit, &dev->op_regs->endptflush);
		while (readl(&dev->op_regs->endptflush)) {
			if (time_after(jiffies, timeout)) {
				ERROR(dev, "ep flush timeout\n");
				goto done;
			}
			cpu_relax();
		}
	} while (readl(&dev->op_regs->endptstat) & flush_bit);
done:
	VDBG(dev, "<--- %s()\n", __func__);
}


/* endpoints operations structure */
static const struct usb_ep_ops langwell_ep_ops = {

	/* configure endpoint, making it usable */
	.enable		= langwell_ep_enable,

	/* endpoint is no longer usable */
	.disable	= langwell_ep_disable,

	/* allocate a request object to use with this endpoint */
	.alloc_request	= langwell_alloc_request,

	/* free a request object */
	.free_request	= langwell_free_request,

	/* queue (submits) an I/O requests to an endpoint */
	.queue		= langwell_ep_queue,

	/* dequeue (cancels, unlinks) an I/O request from an endpoint */
	.dequeue	= langwell_ep_dequeue,

	/* set the endpoint halt feature */
	.set_halt	= langwell_ep_set_halt,

	/* set the halt feature and ignores clear requests */
	.set_wedge	= langwell_ep_set_wedge,

	/* flush contents of a fifo */
	.fifo_flush	= langwell_ep_fifo_flush,
};


/*-------------------------------------------------------------------------*/

/* device controller usb_gadget_ops structure */

/* returns the current frame number */
static int langwell_get_frame(struct usb_gadget *_gadget)
{
	struct langwell_udc	*dev;
	u16			retval;

	if (!_gadget)
		return -ENODEV;

	dev = container_of(_gadget, struct langwell_udc, gadget);
	VDBG(dev, "---> %s()\n", __func__);

	retval = readl(&dev->op_regs->frindex) & FRINDEX_MASK;

	VDBG(dev, "<--- %s()\n", __func__);
	return retval;
}


/* tries to wake up the host connected to this gadget */
static int langwell_wakeup(struct usb_gadget *_gadget)
{
	struct langwell_udc	*dev;
	u32 			portsc1, devlc;
	unsigned long   	flags;

	if (!_gadget)
		return 0;

	dev = container_of(_gadget, struct langwell_udc, gadget);
	VDBG(dev, "---> %s()\n", __func__);

	/* Remote Wakeup feature not enabled by host */
	if (!dev->remote_wakeup)
		return -ENOTSUPP;

	spin_lock_irqsave(&dev->lock, flags);

	portsc1 = readl(&dev->op_regs->portsc1);
	if (!(portsc1 & PORTS_SUSP)) {
		spin_unlock_irqrestore(&dev->lock, flags);
		return 0;
	}

	/* LPM L1 to L0, remote wakeup */
	if (dev->lpm && dev->lpm_state == LPM_L1) {
		portsc1 |= PORTS_SLP;
		writel(portsc1, &dev->op_regs->portsc1);
	}

	/* force port resume */
	if (dev->usb_state == USB_STATE_SUSPENDED) {
		portsc1 |= PORTS_FPR;
		writel(portsc1, &dev->op_regs->portsc1);
	}

	/* exit PHY low power suspend */
	devlc = readl(&dev->op_regs->devlc);
	VDBG(dev, "devlc = 0x%08x\n", devlc);
	devlc &= ~LPM_PHCD;
	writel(devlc, &dev->op_regs->devlc);

	spin_unlock_irqrestore(&dev->lock, flags);

	VDBG(dev, "<--- %s()\n", __func__);
	return 0;
}


/* notify controller that VBUS is powered or not */
static int langwell_vbus_session(struct usb_gadget *_gadget, int is_active)
{
	struct langwell_udc	*dev;
	unsigned long		flags;
	u32             	usbcmd;

	if (!_gadget)
		return -ENODEV;

	dev = container_of(_gadget, struct langwell_udc, gadget);
	VDBG(dev, "---> %s()\n", __func__);

	spin_lock_irqsave(&dev->lock, flags);
	VDBG(dev, "VBUS status: %s\n", is_active ? "on" : "off");

	dev->vbus_active = (is_active != 0);
	if (dev->driver && dev->softconnected && dev->vbus_active) {
		usbcmd = readl(&dev->op_regs->usbcmd);
		usbcmd |= CMD_RUNSTOP;
		writel(usbcmd, &dev->op_regs->usbcmd);
	} else {
		usbcmd = readl(&dev->op_regs->usbcmd);
		usbcmd &= ~CMD_RUNSTOP;
		writel(usbcmd, &dev->op_regs->usbcmd);
	}

	spin_unlock_irqrestore(&dev->lock, flags);

	VDBG(dev, "<--- %s()\n", __func__);
	return 0;
}


/* constrain controller's VBUS power usage */
static int langwell_vbus_draw(struct usb_gadget *_gadget, unsigned mA)
{
	struct langwell_udc	*dev;

	if (!_gadget)
		return -ENODEV;

	dev = container_of(_gadget, struct langwell_udc, gadget);
	VDBG(dev, "---> %s()\n", __func__);

	if (dev->transceiver) {
		VDBG(dev, "otg_set_power\n");
		VDBG(dev, "<--- %s()\n", __func__);
		return otg_set_power(dev->transceiver, mA);
	}

	VDBG(dev, "<--- %s()\n", __func__);
	return -ENOTSUPP;
}


/* D+ pullup, software-controlled connect/disconnect to USB host */
static int langwell_pullup(struct usb_gadget *_gadget, int is_on)
{
	struct langwell_udc	*dev;
	u32             	usbcmd;
	unsigned long   	flags;

	if (!_gadget)
		return -ENODEV;

	dev = container_of(_gadget, struct langwell_udc, gadget);

	VDBG(dev, "---> %s()\n", __func__);

	spin_lock_irqsave(&dev->lock, flags);
	dev->softconnected = (is_on != 0);

	if (dev->driver && dev->softconnected && dev->vbus_active) {
		usbcmd = readl(&dev->op_regs->usbcmd);
		usbcmd |= CMD_RUNSTOP;
		writel(usbcmd, &dev->op_regs->usbcmd);
	} else {
		usbcmd = readl(&dev->op_regs->usbcmd);
		usbcmd &= ~CMD_RUNSTOP;
		writel(usbcmd, &dev->op_regs->usbcmd);
	}
	spin_unlock_irqrestore(&dev->lock, flags);

	VDBG(dev, "<--- %s()\n", __func__);
	return 0;
}


/* device controller usb_gadget_ops structure */
static const struct usb_gadget_ops langwell_ops = {

	/* returns the current frame number */
	.get_frame	= langwell_get_frame,

	/* tries to wake up the host connected to this gadget */
	.wakeup		= langwell_wakeup,

	/* set the device selfpowered feature, always selfpowered */
	/* .set_selfpowered = langwell_set_selfpowered, */

	/* notify controller that VBUS is powered or not */
	.vbus_session	= langwell_vbus_session,

	/* constrain controller's VBUS power usage */
	.vbus_draw	= langwell_vbus_draw,

	/* D+ pullup, software-controlled connect/disconnect to USB host */
	.pullup		= langwell_pullup,
};


/*-------------------------------------------------------------------------*/

/* device controller operations */

/* reset device controller */
static int langwell_udc_reset(struct langwell_udc *dev)
{
	u32		usbcmd, usbmode, devlc, endpointlistaddr;
	unsigned long	timeout;

	if (!dev)
		return -EINVAL;

	DBG(dev, "---> %s()\n", __func__);

	/* set controller to stop state */
	usbcmd = readl(&dev->op_regs->usbcmd);
	usbcmd &= ~CMD_RUNSTOP;
	writel(usbcmd, &dev->op_regs->usbcmd);

	/* reset device controller */
	usbcmd = readl(&dev->op_regs->usbcmd);
	usbcmd |= CMD_RST;
	writel(usbcmd, &dev->op_regs->usbcmd);

	/* wait for reset to complete */
	timeout = jiffies + RESET_TIMEOUT;
	while (readl(&dev->op_regs->usbcmd) & CMD_RST) {
		if (time_after(jiffies, timeout)) {
			ERROR(dev, "device reset timeout\n");
			return -ETIMEDOUT;
		}
		cpu_relax();
	}

	/* set controller to device mode */
	usbmode = readl(&dev->op_regs->usbmode);
	usbmode |= MODE_DEVICE;

	/* turn setup lockout off, require setup tripwire in usbcmd */
	usbmode |= MODE_SLOM;

	writel(usbmode, &dev->op_regs->usbmode);
	usbmode = readl(&dev->op_regs->usbmode);
	VDBG(dev, "usbmode=0x%08x\n", usbmode);

	/* Write-Clear setup status */
	writel(0, &dev->op_regs->usbsts);

	/* if support USB LPM, ACK all LPM token */
	if (dev->lpm) {
		devlc = readl(&dev->op_regs->devlc);
		devlc &= ~LPM_STL;	/* don't STALL LPM token */
		devlc &= ~LPM_NYT_ACK;	/* ACK LPM token */
		writel(devlc, &dev->op_regs->devlc);
	}

	/* fill endpointlistaddr register */
	endpointlistaddr = dev->ep_dqh_dma;
	endpointlistaddr &= ENDPOINTLISTADDR_MASK;
	writel(endpointlistaddr, &dev->op_regs->endpointlistaddr);

	VDBG(dev, "dQH base (vir: %p, phy: 0x%08x), endpointlistaddr=0x%08x\n",
			dev->ep_dqh, endpointlistaddr,
			readl(&dev->op_regs->endpointlistaddr));
	DBG(dev, "<--- %s()\n", __func__);
	return 0;
}


/* reinitialize device controller endpoints */
static int eps_reinit(struct langwell_udc *dev)
{
	struct langwell_ep	*ep;
	char			name[14];
	int			i;

	VDBG(dev, "---> %s()\n", __func__);

	/* initialize ep0 */
	ep = &dev->ep[0];
	ep->dev = dev;
	strncpy(ep->name, "ep0", sizeof(ep->name));
	ep->ep.name = ep->name;
	ep->ep.ops = &langwell_ep_ops;
	ep->stopped = 0;
	ep->ep.maxpacket = EP0_MAX_PKT_SIZE;
	ep->ep_num = 0;
	ep->desc = &langwell_ep0_desc;
	INIT_LIST_HEAD(&ep->queue);

	ep->ep_type = USB_ENDPOINT_XFER_CONTROL;

	/* initialize other endpoints */
	for (i = 2; i < dev->ep_max; i++) {
		ep = &dev->ep[i];
		if (i % 2)
			snprintf(name, sizeof(name), "ep%din", i / 2);
		else
			snprintf(name, sizeof(name), "ep%dout", i / 2);
		ep->dev = dev;
		strncpy(ep->name, name, sizeof(ep->name));
		ep->ep.name = ep->name;

		ep->ep.ops = &langwell_ep_ops;
		ep->stopped = 0;
		ep->ep.maxpacket = (unsigned short) ~0;
		ep->ep_num = i / 2;

		INIT_LIST_HEAD(&ep->queue);
		list_add_tail(&ep->ep.ep_list, &dev->gadget.ep_list);

		ep->dqh = &dev->ep_dqh[i];
	}

	VDBG(dev, "<--- %s()\n", __func__);
	return 0;
}


/* enable interrupt and set controller to run state */
static void langwell_udc_start(struct langwell_udc *dev)
{
	u32	usbintr, usbcmd;
	DBG(dev, "---> %s()\n", __func__);

	/* enable interrupts */
	usbintr = INTR_ULPIE	/* ULPI */
		| INTR_SLE	/* suspend */
		/* | INTR_SRE	SOF received */
		| INTR_URE	/* USB reset */
		| INTR_AAE	/* async advance */
		| INTR_SEE	/* system error */
		| INTR_FRE	/* frame list rollover */
		| INTR_PCE	/* port change detect */
		| INTR_UEE	/* USB error interrupt */
		| INTR_UE;	/* USB interrupt */
	writel(usbintr, &dev->op_regs->usbintr);

	/* clear stopped bit */
	dev->stopped = 0;

	/* set controller to run */
	usbcmd = readl(&dev->op_regs->usbcmd);
	usbcmd |= CMD_RUNSTOP;
	writel(usbcmd, &dev->op_regs->usbcmd);

	DBG(dev, "<--- %s()\n", __func__);
	return;
}


/* disable interrupt and set controller to stop state */
static void langwell_udc_stop(struct langwell_udc *dev)
{
	u32	usbcmd;

	DBG(dev, "---> %s()\n", __func__);

	/* disable all interrupts */
	writel(0, &dev->op_regs->usbintr);

	/* set stopped bit */
	dev->stopped = 1;

	/* set controller to stop state */
	usbcmd = readl(&dev->op_regs->usbcmd);
	usbcmd &= ~CMD_RUNSTOP;
	writel(usbcmd, &dev->op_regs->usbcmd);

	DBG(dev, "<--- %s()\n", __func__);
	return;
}


/* stop all USB activities */
static void stop_activity(struct langwell_udc *dev,
		struct usb_gadget_driver *driver)
{
	struct langwell_ep	*ep;
	DBG(dev, "---> %s()\n", __func__);

	nuke(&dev->ep[0], -ESHUTDOWN);

	list_for_each_entry(ep, &dev->gadget.ep_list, ep.ep_list) {
		nuke(ep, -ESHUTDOWN);
	}

	/* report disconnect; the driver is already quiesced */
	if (driver) {
		spin_unlock(&dev->lock);
		driver->disconnect(&dev->gadget);
		spin_lock(&dev->lock);
	}

	DBG(dev, "<--- %s()\n", __func__);
}


/*-------------------------------------------------------------------------*/

/* device "function" sysfs attribute file */
static ssize_t show_function(struct device *_dev,
		struct device_attribute *attr, char *buf)
{
	struct langwell_udc	*dev = the_controller;

	if (!dev->driver || !dev->driver->function
			|| strlen(dev->driver->function) > PAGE_SIZE)
		return 0;

	return scnprintf(buf, PAGE_SIZE, "%s\n", dev->driver->function);
}
static DEVICE_ATTR(function, S_IRUGO, show_function, NULL);


/* device "langwell_udc" sysfs attribute file */
static ssize_t show_langwell_udc(struct device *_dev,
		struct device_attribute *attr, char *buf)
{
	struct langwell_udc	*dev = the_controller;
	struct langwell_request *req;
	struct langwell_ep	*ep = NULL;
	char			*next;
	unsigned		size;
	unsigned		t;
	unsigned		i;
	unsigned long		flags;
	u32			tmp_reg;

	next = buf;
	size = PAGE_SIZE;
	spin_lock_irqsave(&dev->lock, flags);

	/* driver basic information */
	t = scnprintf(next, size,
			DRIVER_DESC "\n"
			"%s version: %s\n"
			"Gadget driver: %s\n\n",
			driver_name, DRIVER_VERSION,
			dev->driver ? dev->driver->driver.name : "(none)");
	size -= t;
	next += t;

	/* device registers */
	tmp_reg = readl(&dev->op_regs->usbcmd);
	t = scnprintf(next, size,
			"USBCMD reg:\n"
			"SetupTW: %d\n"
			"Run/Stop: %s\n\n",
			(tmp_reg & CMD_SUTW) ? 1 : 0,
			(tmp_reg & CMD_RUNSTOP) ? "Run" : "Stop");
	size -= t;
	next += t;

	tmp_reg = readl(&dev->op_regs->usbsts);
	t = scnprintf(next, size,
			"USB Status Reg:\n"
			"Device Suspend: %d\n"
			"Reset Received: %d\n"
			"System Error: %s\n"
			"USB Error Interrupt: %s\n\n",
			(tmp_reg & STS_SLI) ? 1 : 0,
			(tmp_reg & STS_URI) ? 1 : 0,
			(tmp_reg & STS_SEI) ? "Error" : "No error",
			(tmp_reg & STS_UEI) ? "Error detected" : "No error");
	size -= t;
	next += t;

	tmp_reg = readl(&dev->op_regs->usbintr);
	t = scnprintf(next, size,
			"USB Intrrupt Enable Reg:\n"
			"Sleep Enable: %d\n"
			"SOF Received Enable: %d\n"
			"Reset Enable: %d\n"
			"System Error Enable: %d\n"
			"Port Change Dectected Enable: %d\n"
			"USB Error Intr Enable: %d\n"
			"USB Intr Enable: %d\n\n",
			(tmp_reg & INTR_SLE) ? 1 : 0,
			(tmp_reg & INTR_SRE) ? 1 : 0,
			(tmp_reg & INTR_URE) ? 1 : 0,
			(tmp_reg & INTR_SEE) ? 1 : 0,
			(tmp_reg & INTR_PCE) ? 1 : 0,
			(tmp_reg & INTR_UEE) ? 1 : 0,
			(tmp_reg & INTR_UE) ? 1 : 0);
	size -= t;
	next += t;

	tmp_reg = readl(&dev->op_regs->frindex);
	t = scnprintf(next, size,
			"USB Frame Index Reg:\n"
			"Frame Number is 0x%08x\n\n",
			(tmp_reg & FRINDEX_MASK));
	size -= t;
	next += t;

	tmp_reg = readl(&dev->op_regs->deviceaddr);
	t = scnprintf(next, size,
			"USB Device Address Reg:\n"
			"Device Addr is 0x%x\n\n",
			USBADR(tmp_reg));
	size -= t;
	next += t;

	tmp_reg = readl(&dev->op_regs->endpointlistaddr);
	t = scnprintf(next, size,
			"USB Endpoint List Address Reg:\n"
			"Endpoint List Pointer is 0x%x\n\n",
			EPBASE(tmp_reg));
	size -= t;
	next += t;

	tmp_reg = readl(&dev->op_regs->portsc1);
	t = scnprintf(next, size,
		"USB Port Status & Control Reg:\n"
		"Port Reset: %s\n"
		"Port Suspend Mode: %s\n"
		"Over-current Change: %s\n"
		"Port Enable/Disable Change: %s\n"
		"Port Enabled/Disabled: %s\n"
		"Current Connect Status: %s\n\n",
		(tmp_reg & PORTS_PR) ? "Reset" : "Not Reset",
		(tmp_reg & PORTS_SUSP) ? "Suspend " : "Not Suspend",
		(tmp_reg & PORTS_OCC) ? "Detected" : "No",
		(tmp_reg & PORTS_PEC) ? "Changed" : "Not Changed",
		(tmp_reg & PORTS_PE) ? "Enable" : "Not Correct",
		(tmp_reg & PORTS_CCS) ?  "Attached" : "Not Attached");
	size -= t;
	next += t;

	tmp_reg = readl(&dev->op_regs->devlc);
	t = scnprintf(next, size,
		"Device LPM Control Reg:\n"
		"Parallel Transceiver : %d\n"
		"Serial Transceiver : %d\n"
		"Port Speed: %s\n"
		"Port Force Full Speed Connenct: %s\n"
		"PHY Low Power Suspend Clock Disable: %s\n"
		"BmAttributes: %d\n\n",
		LPM_PTS(tmp_reg),
		(tmp_reg & LPM_STS) ? 1 : 0,
		({
			char	*s;
			switch (LPM_PSPD(tmp_reg)) {
			case LPM_SPEED_FULL:
				s = "Full Speed"; break;
			case LPM_SPEED_LOW:
				s = "Low Speed"; break;
			case LPM_SPEED_HIGH:
				s = "High Speed"; break;
			default:
				s = "Unknown Speed"; break;
			}
			s;
		}),
		(tmp_reg & LPM_PFSC) ? "Force Full Speed" : "Not Force",
		(tmp_reg & LPM_PHCD) ? "Disabled" : "Enabled",
		LPM_BA(tmp_reg));
	size -= t;
	next += t;

	tmp_reg = readl(&dev->op_regs->usbmode);
	t = scnprintf(next, size,
			"USB Mode Reg:\n"
			"Controller Mode is : %s\n\n", ({
				char *s;
				switch (MODE_CM(tmp_reg)) {
				case MODE_IDLE:
					s = "Idle"; break;
				case MODE_DEVICE:
					s = "Device Controller"; break;
				case MODE_HOST:
					s = "Host Controller"; break;
				default:
					s = "None"; break;
				}
				s;
			}));
	size -= t;
	next += t;

	tmp_reg = readl(&dev->op_regs->endptsetupstat);
	t = scnprintf(next, size,
			"Endpoint Setup Status Reg:\n"
			"SETUP on ep 0x%04x\n\n",
			tmp_reg & SETUPSTAT_MASK);
	size -= t;
	next += t;

	for (i = 0; i < dev->ep_max / 2; i++) {
		tmp_reg = readl(&dev->op_regs->endptctrl[i]);
		t = scnprintf(next, size, "EP Ctrl Reg [%d]: 0x%08x\n",
				i, tmp_reg);
		size -= t;
		next += t;
	}
	tmp_reg = readl(&dev->op_regs->endptprime);
	t = scnprintf(next, size, "EP Prime Reg: 0x%08x\n\n", tmp_reg);
	size -= t;
	next += t;

	/* langwell_udc, langwell_ep, langwell_request structure information */
	ep = &dev->ep[0];
	t = scnprintf(next, size, "%s MaxPacketSize: 0x%x, ep_num: %d\n",
			ep->ep.name, ep->ep.maxpacket, ep->ep_num);
	size -= t;
	next += t;

	if (list_empty(&ep->queue)) {
		t = scnprintf(next, size, "its req queue is empty\n\n");
		size -= t;
		next += t;
	} else {
		list_for_each_entry(req, &ep->queue, queue) {
			t = scnprintf(next, size,
				"req %p actual 0x%x length 0x%x  buf %p\n",
				&req->req, req->req.actual,
				req->req.length, req->req.buf);
			size -= t;
			next += t;
		}
	}
	/* other gadget->eplist ep */
	list_for_each_entry(ep, &dev->gadget.ep_list, ep.ep_list) {
		if (ep->desc) {
			t = scnprintf(next, size,
					"\n%s MaxPacketSize: 0x%x, "
					"ep_num: %d\n",
					ep->ep.name, ep->ep.maxpacket,
					ep->ep_num);
			size -= t;
			next += t;

			if (list_empty(&ep->queue)) {
				t = scnprintf(next, size,
						"its req queue is empty\n\n");
				size -= t;
				next += t;
			} else {
				list_for_each_entry(req, &ep->queue, queue) {
					t = scnprintf(next, size,
						"req %p actual 0x%x length "
						"0x%x  buf %p\n",
						&req->req, req->req.actual,
						req->req.length, req->req.buf);
					size -= t;
					next += t;
				}
			}
		}
	}

	spin_unlock_irqrestore(&dev->lock, flags);
	return PAGE_SIZE - size;
}
static DEVICE_ATTR(langwell_udc, S_IRUGO, show_langwell_udc, NULL);


/*-------------------------------------------------------------------------*/

/*
 * when a driver is successfully registered, it will receive
 * control requests including set_configuration(), which enables
 * non-control requests.  then usb traffic follows until a
 * disconnect is reported.  then a host may connect again, or
 * the driver might get unbound.
 */

int usb_gadget_register_driver(struct usb_gadget_driver *driver)
{
	struct langwell_udc	*dev = the_controller;
	unsigned long		flags;
	int			retval;

	if (!dev)
		return -ENODEV;

	DBG(dev, "---> %s()\n", __func__);

	if (dev->driver)
		return -EBUSY;

	spin_lock_irqsave(&dev->lock, flags);

	/* hook up the driver ... */
	driver->driver.bus = NULL;
	dev->driver = driver;
	dev->gadget.dev.driver = &driver->driver;

	spin_unlock_irqrestore(&dev->lock, flags);

	retval = driver->bind(&dev->gadget);
	if (retval) {
		DBG(dev, "bind to driver %s --> %d\n",
				driver->driver.name, retval);
		dev->driver = NULL;
		dev->gadget.dev.driver = NULL;
		return retval;
	}

	retval = device_create_file(&dev->pdev->dev, &dev_attr_function);
	if (retval)
		goto err_unbind;

	dev->usb_state = USB_STATE_ATTACHED;
	dev->ep0_state = WAIT_FOR_SETUP;
	dev->ep0_dir = USB_DIR_OUT;

	/* enable interrupt and set controller to run state */
	if (dev->got_irq)
		langwell_udc_start(dev);

	VDBG(dev, "After langwell_udc_start(), print all registers:\n");
#ifdef	VERBOSE
	print_all_registers(dev);
#endif

	INFO(dev, "register driver: %s\n", driver->driver.name);
	VDBG(dev, "<--- %s()\n", __func__);
	return 0;

err_unbind:
	driver->unbind(&dev->gadget);
	dev->gadget.dev.driver = NULL;
	dev->driver = NULL;

	DBG(dev, "<--- %s()\n", __func__);
	return retval;
}
EXPORT_SYMBOL(usb_gadget_register_driver);


/* unregister gadget driver */
int usb_gadget_unregister_driver(struct usb_gadget_driver *driver)
{
	struct langwell_udc	*dev = the_controller;
	unsigned long		flags;

	if (!dev)
		return -ENODEV;

	DBG(dev, "---> %s()\n", __func__);

	if (unlikely(!driver || !driver->bind || !driver->unbind))
		return -EINVAL;

	/* unbind OTG transceiver */
	if (dev->transceiver)
		(void)otg_set_peripheral(dev->transceiver, 0);

	/* disable interrupt and set controller to stop state */
	langwell_udc_stop(dev);

	dev->usb_state = USB_STATE_ATTACHED;
	dev->ep0_state = WAIT_FOR_SETUP;
	dev->ep0_dir = USB_DIR_OUT;

	spin_lock_irqsave(&dev->lock, flags);

	/* stop all usb activities */
	dev->gadget.speed = USB_SPEED_UNKNOWN;
	stop_activity(dev, driver);
	spin_unlock_irqrestore(&dev->lock, flags);

	/* unbind gadget driver */
	driver->unbind(&dev->gadget);
	dev->gadget.dev.driver = NULL;
	dev->driver = NULL;

	device_remove_file(&dev->pdev->dev, &dev_attr_function);

	INFO(dev, "unregistered driver '%s'\n", driver->driver.name);
	DBG(dev, "<--- %s()\n", __func__);
	return 0;
}
EXPORT_SYMBOL(usb_gadget_unregister_driver);


/*-------------------------------------------------------------------------*/

/*
 * setup tripwire is used as a semaphore to ensure that the setup data
 * payload is extracted from a dQH without being corrupted
 */
static void setup_tripwire(struct langwell_udc *dev)
{
	u32			usbcmd,
				endptsetupstat;
	unsigned long		timeout;
	struct langwell_dqh	*dqh;

	VDBG(dev, "---> %s()\n", __func__);

	/* ep0 OUT dQH */
	dqh = &dev->ep_dqh[EP_DIR_OUT];

	/* Write-Clear endptsetupstat */
	endptsetupstat = readl(&dev->op_regs->endptsetupstat);
	writel(endptsetupstat, &dev->op_regs->endptsetupstat);

	/* wait until endptsetupstat is cleared */
	timeout = jiffies + SETUPSTAT_TIMEOUT;
	while (readl(&dev->op_regs->endptsetupstat)) {
		if (time_after(jiffies, timeout)) {
			ERROR(dev, "setup_tripwire timeout\n");
			break;
		}
		cpu_relax();
	}

	/* while a hazard exists when setup packet arrives */
	do {
		/* set setup tripwire bit */
		usbcmd = readl(&dev->op_regs->usbcmd);
		writel(usbcmd | CMD_SUTW, &dev->op_regs->usbcmd);

		/* copy the setup packet to local buffer */
		memcpy(&dev->local_setup_buff, &dqh->dqh_setup, 8);
	} while (!(readl(&dev->op_regs->usbcmd) & CMD_SUTW));

	/* Write-Clear setup tripwire bit */
	usbcmd = readl(&dev->op_regs->usbcmd);
	writel(usbcmd & ~CMD_SUTW, &dev->op_regs->usbcmd);

	VDBG(dev, "<--- %s()\n", __func__);
}


/* protocol ep0 stall, will automatically be cleared on new transaction */
static void ep0_stall(struct langwell_udc *dev)
{
	u32	endptctrl;

	VDBG(dev, "---> %s()\n", __func__);

	/* set TX and RX to stall */
	endptctrl = readl(&dev->op_regs->endptctrl[0]);
	endptctrl |= EPCTRL_TXS | EPCTRL_RXS;
	writel(endptctrl, &dev->op_regs->endptctrl[0]);

	/* update ep0 state */
	dev->ep0_state = WAIT_FOR_SETUP;
	dev->ep0_dir = USB_DIR_OUT;

	VDBG(dev, "<--- %s()\n", __func__);
}


/* PRIME a status phase for ep0 */
static int prime_status_phase(struct langwell_udc *dev, int dir)
{
	struct langwell_request	*req;
	struct langwell_ep	*ep;
	int			status = 0;

	VDBG(dev, "---> %s()\n", __func__);

	if (dir == EP_DIR_IN)
		dev->ep0_dir = USB_DIR_IN;
	else
		dev->ep0_dir = USB_DIR_OUT;

	ep = &dev->ep[0];
	dev->ep0_state = WAIT_FOR_OUT_STATUS;

	req = dev->status_req;

	req->ep = ep;
	req->req.length = 0;
	req->req.status = -EINPROGRESS;
	req->req.actual = 0;
	req->req.complete = NULL;
	req->dtd_count = 0;

	if (!req_to_dtd(req))
		status = queue_dtd(ep, req);
	else
		return -ENOMEM;

	if (status)
		ERROR(dev, "can't queue ep0 status request\n");

	list_add_tail(&req->queue, &ep->queue);

	VDBG(dev, "<--- %s()\n", __func__);
	return status;
}


/* SET_ADDRESS request routine */
static void set_address(struct langwell_udc *dev, u16 value,
		u16 index, u16 length)
{
	VDBG(dev, "---> %s()\n", __func__);

	/* save the new address to device struct */
	dev->dev_addr = (u8) value;
	VDBG(dev, "dev->dev_addr = %d\n", dev->dev_addr);

	/* update usb state */
	dev->usb_state = USB_STATE_ADDRESS;

	/* STATUS phase */
	if (prime_status_phase(dev, EP_DIR_IN))
		ep0_stall(dev);

	VDBG(dev, "<--- %s()\n", __func__);
}


/* return endpoint by windex */
static struct langwell_ep *get_ep_by_windex(struct langwell_udc *dev,
		u16 wIndex)
{
	struct langwell_ep		*ep;
	VDBG(dev, "---> %s()\n", __func__);

	if ((wIndex & USB_ENDPOINT_NUMBER_MASK) == 0)
		return &dev->ep[0];

	list_for_each_entry(ep, &dev->gadget.ep_list, ep.ep_list) {
		u8	bEndpointAddress;
		if (!ep->desc)
			continue;

		bEndpointAddress = ep->desc->bEndpointAddress;
		if ((wIndex ^ bEndpointAddress) & USB_DIR_IN)
			continue;

		if ((wIndex & USB_ENDPOINT_NUMBER_MASK)
			== (bEndpointAddress & USB_ENDPOINT_NUMBER_MASK))
			return ep;
	}

	VDBG(dev, "<--- %s()\n", __func__);
	return NULL;
}


/* return whether endpoint is stalled, 0: not stalled; 1: stalled */
static int ep_is_stall(struct langwell_ep *ep)
{
	struct langwell_udc	*dev = ep->dev;
	u32			endptctrl;
	int			retval;

	VDBG(dev, "---> %s()\n", __func__);

	endptctrl = readl(&dev->op_regs->endptctrl[ep->ep_num]);
	if (is_in(ep))
		retval = endptctrl & EPCTRL_TXS ? 1 : 0;
	else
		retval = endptctrl & EPCTRL_RXS ? 1 : 0;

	VDBG(dev, "<--- %s()\n", __func__);
	return retval;
}


/* GET_STATUS request routine */
static void get_status(struct langwell_udc *dev, u8 request_type, u16 value,
		u16 index, u16 length)
{
	struct langwell_request	*req;
	struct langwell_ep	*ep;
	u16	status_data = 0;	/* 16 bits cpu view status data */
	int	status = 0;

	VDBG(dev, "---> %s()\n", __func__);

	ep = &dev->ep[0];

	if ((request_type & USB_RECIP_MASK) == USB_RECIP_DEVICE) {
		/* get device status */
		status_data = 1 << USB_DEVICE_SELF_POWERED;
		status_data |= dev->remote_wakeup << USB_DEVICE_REMOTE_WAKEUP;
	} else if ((request_type & USB_RECIP_MASK) == USB_RECIP_INTERFACE) {
		/* get interface status */
		status_data = 0;
	} else if ((request_type & USB_RECIP_MASK) == USB_RECIP_ENDPOINT) {
		/* get endpoint status */
		struct langwell_ep	*epn;
		epn = get_ep_by_windex(dev, index);
		/* stall if endpoint doesn't exist */
		if (!epn)
			goto stall;

		status_data = ep_is_stall(epn) << USB_ENDPOINT_HALT;
	}

	dev->ep0_dir = USB_DIR_IN;

	/* borrow the per device status_req */
	req = dev->status_req;

	/* fill in the reqest structure */
	*((u16 *) req->req.buf) = cpu_to_le16(status_data);
	req->ep = ep;
	req->req.length = 2;
	req->req.status = -EINPROGRESS;
	req->req.actual = 0;
	req->req.complete = NULL;
	req->dtd_count = 0;

	/* prime the data phase */
	if (!req_to_dtd(req))
		status = queue_dtd(ep, req);
	else			/* no mem */
		goto stall;

	if (status) {
		ERROR(dev, "response error on GET_STATUS request\n");
		goto stall;
	}

	list_add_tail(&req->queue, &ep->queue);
	dev->ep0_state = DATA_STATE_XMIT;

	VDBG(dev, "<--- %s()\n", __func__);
	return;
stall:
	ep0_stall(dev);
	VDBG(dev, "<--- %s()\n", __func__);
}


/* setup packet interrupt handler */
static void handle_setup_packet(struct langwell_udc *dev,
		struct usb_ctrlrequest *setup)
{
	u16	wValue = le16_to_cpu(setup->wValue);
	u16	wIndex = le16_to_cpu(setup->wIndex);
	u16	wLength = le16_to_cpu(setup->wLength);

	VDBG(dev, "---> %s()\n", __func__);

	/* ep0 fifo flush */
	nuke(&dev->ep[0], -ESHUTDOWN);

	DBG(dev, "SETUP %02x.%02x v%04x i%04x l%04x\n",
			setup->bRequestType, setup->bRequest,
			wValue, wIndex, wLength);

	/* RNDIS gadget delegate */
	if ((setup->bRequestType == 0x21) && (setup->bRequest == 0x00)) {
		/* USB_CDC_SEND_ENCAPSULATED_COMMAND */
		goto delegate;
	}

	/* USB_CDC_GET_ENCAPSULATED_RESPONSE */
	if ((setup->bRequestType == 0xa1) && (setup->bRequest == 0x01)) {
		/* USB_CDC_GET_ENCAPSULATED_RESPONSE */
		goto delegate;
	}

	/* We process some stardard setup requests here */
	switch (setup->bRequest) {
	case USB_REQ_GET_STATUS:
		DBG(dev, "SETUP: USB_REQ_GET_STATUS\n");
		/* get status, DATA and STATUS phase */
		if ((setup->bRequestType & (USB_DIR_IN | USB_TYPE_MASK))
					!= (USB_DIR_IN | USB_TYPE_STANDARD))
			break;
		get_status(dev, setup->bRequestType, wValue, wIndex, wLength);
		goto end;

	case USB_REQ_SET_ADDRESS:
		DBG(dev, "SETUP: USB_REQ_SET_ADDRESS\n");
		/* STATUS phase */
		if (setup->bRequestType != (USB_DIR_OUT | USB_TYPE_STANDARD
						| USB_RECIP_DEVICE))
			break;
		set_address(dev, wValue, wIndex, wLength);
		goto end;

	case USB_REQ_CLEAR_FEATURE:
	case USB_REQ_SET_FEATURE:
		/* STATUS phase */
	{
		int rc = -EOPNOTSUPP;
		if (setup->bRequest == USB_REQ_SET_FEATURE)
			DBG(dev, "SETUP: USB_REQ_SET_FEATURE\n");
		else if (setup->bRequest == USB_REQ_CLEAR_FEATURE)
			DBG(dev, "SETUP: USB_REQ_CLEAR_FEATURE\n");

		if ((setup->bRequestType & (USB_RECIP_MASK | USB_TYPE_MASK))
				== (USB_RECIP_ENDPOINT | USB_TYPE_STANDARD)) {
			struct langwell_ep	*epn;
			epn = get_ep_by_windex(dev, wIndex);
			/* stall if endpoint doesn't exist */
			if (!epn) {
				ep0_stall(dev);
				goto end;
			}

			if (wValue != 0 || wLength != 0
					|| epn->ep_num > dev->ep_max)
				break;

			spin_unlock(&dev->lock);
			rc = langwell_ep_set_halt(&epn->ep,
					(setup->bRequest == USB_REQ_SET_FEATURE)
						? 1 : 0);
			spin_lock(&dev->lock);

		} else if ((setup->bRequestType & (USB_RECIP_MASK
				| USB_TYPE_MASK)) == (USB_RECIP_DEVICE
				| USB_TYPE_STANDARD)) {
			if (!gadget_is_otg(&dev->gadget))
				break;
			else if (setup->bRequest == USB_DEVICE_B_HNP_ENABLE) {
				dev->gadget.b_hnp_enable = 1;
#ifdef	OTG_TRANSCEIVER
				if (!dev->lotg->otg.default_a)
					dev->lotg->hsm.b_hnp_enable = 1;
#endif
			} else if (setup->bRequest == USB_DEVICE_A_HNP_SUPPORT)
				dev->gadget.a_hnp_support = 1;
			else if (setup->bRequest ==
					USB_DEVICE_A_ALT_HNP_SUPPORT)
				dev->gadget.a_alt_hnp_support = 1;
			else
				break;
			rc = 0;
		} else
			break;

		if (rc == 0) {
			if (prime_status_phase(dev, EP_DIR_IN))
				ep0_stall(dev);
		}
		goto end;
	}

	case USB_REQ_GET_DESCRIPTOR:
		DBG(dev, "SETUP: USB_REQ_GET_DESCRIPTOR\n");
		goto delegate;

	case USB_REQ_SET_DESCRIPTOR:
		DBG(dev, "SETUP: USB_REQ_SET_DESCRIPTOR unsupported\n");
		goto delegate;

	case USB_REQ_GET_CONFIGURATION:
		DBG(dev, "SETUP: USB_REQ_GET_CONFIGURATION\n");
		goto delegate;

	case USB_REQ_SET_CONFIGURATION:
		DBG(dev, "SETUP: USB_REQ_SET_CONFIGURATION\n");
		goto delegate;

	case USB_REQ_GET_INTERFACE:
		DBG(dev, "SETUP: USB_REQ_GET_INTERFACE\n");
		goto delegate;

	case USB_REQ_SET_INTERFACE:
		DBG(dev, "SETUP: USB_REQ_SET_INTERFACE\n");
		goto delegate;

	case USB_REQ_SYNCH_FRAME:
		DBG(dev, "SETUP: USB_REQ_SYNCH_FRAME unsupported\n");
		goto delegate;

	default:
		/* delegate USB standard requests to the gadget driver */
		goto delegate;
delegate:
		/* USB requests handled by gadget */
		if (wLength) {
			/* DATA phase from gadget, STATUS phase from udc */
			dev->ep0_dir = (setup->bRequestType & USB_DIR_IN)
					?  USB_DIR_IN : USB_DIR_OUT;
			VDBG(dev, "dev->ep0_dir = 0x%x, wLength = %d\n",
					dev->ep0_dir, wLength);
			spin_unlock(&dev->lock);
			if (dev->driver->setup(&dev->gadget,
					&dev->local_setup_buff) < 0)
				ep0_stall(dev);
			spin_lock(&dev->lock);
			dev->ep0_state = (setup->bRequestType & USB_DIR_IN)
					?  DATA_STATE_XMIT : DATA_STATE_RECV;
		} else {
			/* no DATA phase, IN STATUS phase from gadget */
			dev->ep0_dir = USB_DIR_IN;
			VDBG(dev, "dev->ep0_dir = 0x%x, wLength = %d\n",
					dev->ep0_dir, wLength);
			spin_unlock(&dev->lock);
			if (dev->driver->setup(&dev->gadget,
					&dev->local_setup_buff) < 0)
				ep0_stall(dev);
			spin_lock(&dev->lock);
			dev->ep0_state = WAIT_FOR_OUT_STATUS;
		}
		break;
	}
end:
	VDBG(dev, "<--- %s()\n", __func__);
	return;
}


/* transfer completion, process endpoint request and free the completed dTDs
 * for this request
 */
static int process_ep_req(struct langwell_udc *dev, int index,
		struct langwell_request *curr_req)
{
	struct langwell_dtd	*curr_dtd;
	struct langwell_dqh	*curr_dqh;
	int			td_complete, actual, remaining_length;
	int			i, dir;
	u8			dtd_status = 0;
	int			retval = 0;

	curr_dqh = &dev->ep_dqh[index];
	dir = index % 2;

	curr_dtd = curr_req->head;
	td_complete = 0;
	actual = curr_req->req.length;

	VDBG(dev, "---> %s()\n", __func__);

	for (i = 0; i < curr_req->dtd_count; i++) {
		remaining_length = le16_to_cpu(curr_dtd->dtd_total);
		actual -= remaining_length;

		/* command execution states by dTD */
		dtd_status = curr_dtd->dtd_status;

		if (!dtd_status) {
			/* transfers completed successfully */
			if (!remaining_length) {
				td_complete++;
				VDBG(dev, "dTD transmitted successfully\n");
			} else {
				if (dir) {
					VDBG(dev, "TX dTD remains data\n");
					retval = -EPROTO;
					break;

				} else {
					td_complete++;
					break;
				}
			}
		} else {
			/* transfers completed with errors */
			if (dtd_status & DTD_STS_ACTIVE) {
				DBG(dev, "request not completed\n");
				retval = 1;
				return retval;
			} else if (dtd_status & DTD_STS_HALTED) {
				ERROR(dev, "dTD error %08x dQH[%d]\n",
						dtd_status, index);
				/* clear the errors and halt condition */
				curr_dqh->dtd_status = 0;
				retval = -EPIPE;
				break;
			} else if (dtd_status & DTD_STS_DBE) {
				DBG(dev, "data buffer (overflow) error\n");
				retval = -EPROTO;
				break;
			} else if (dtd_status & DTD_STS_TRE) {
				DBG(dev, "transaction(ISO) error\n");
				retval = -EILSEQ;
				break;
			} else
				ERROR(dev, "unknown error (0x%x)!\n",
						dtd_status);
		}

		if (i != curr_req->dtd_count - 1)
			curr_dtd = (struct langwell_dtd *)
				curr_dtd->next_dtd_virt;
	}

	if (retval)
		return retval;

	curr_req->req.actual = actual;

	VDBG(dev, "<--- %s()\n", __func__);
	return 0;
}


/* complete DATA or STATUS phase of ep0 prime status phase if needed */
static void ep0_req_complete(struct langwell_udc *dev,
		struct langwell_ep *ep0, struct langwell_request *req)
{
	u32	new_addr;
	VDBG(dev, "---> %s()\n", __func__);

	if (dev->usb_state == USB_STATE_ADDRESS) {
		/* set the new address */
		new_addr = (u32)dev->dev_addr;
		writel(new_addr << USBADR_SHIFT, &dev->op_regs->deviceaddr);

		new_addr = USBADR(readl(&dev->op_regs->deviceaddr));
		VDBG(dev, "new_addr = %d\n", new_addr);
	}

	done(ep0, req, 0);

	switch (dev->ep0_state) {
	case DATA_STATE_XMIT:
		/* receive status phase */
		if (prime_status_phase(dev, EP_DIR_OUT))
			ep0_stall(dev);
		break;
	case DATA_STATE_RECV:
		/* send status phase */
		if (prime_status_phase(dev, EP_DIR_IN))
			ep0_stall(dev);
		break;
	case WAIT_FOR_OUT_STATUS:
		dev->ep0_state = WAIT_FOR_SETUP;
		break;
	case WAIT_FOR_SETUP:
		ERROR(dev, "unexpect ep0 packets\n");
		break;
	default:
		ep0_stall(dev);
		break;
	}

	VDBG(dev, "<--- %s()\n", __func__);
}


/* USB transfer completion interrupt */
static void handle_trans_complete(struct langwell_udc *dev)
{
	u32			complete_bits;
	int			i, ep_num, dir, bit_mask, status;
	struct langwell_ep	*epn;
	struct langwell_request	*curr_req, *temp_req;

	VDBG(dev, "---> %s()\n", __func__);

	complete_bits = readl(&dev->op_regs->endptcomplete);
	VDBG(dev, "endptcomplete register: 0x%08x\n", complete_bits);

	/* Write-Clear the bits in endptcomplete register */
	writel(complete_bits, &dev->op_regs->endptcomplete);

	if (!complete_bits) {
		DBG(dev, "complete_bits = 0\n");
		goto done;
	}

	for (i = 0; i < dev->ep_max; i++) {
		ep_num = i / 2;
		dir = i % 2;

		bit_mask = 1 << (ep_num + 16 * dir);

		if (!(complete_bits & bit_mask))
			continue;

		/* ep0 */
		if (i == 1)
			epn = &dev->ep[0];
		else
			epn = &dev->ep[i];

		if (epn->name == NULL) {
			WARNING(dev, "invalid endpoint\n");
			continue;
		}

		if (i < 2)
			/* ep0 in and out */
			DBG(dev, "%s-%s transfer completed\n",
					epn->name,
					is_in(epn) ? "in" : "out");
		else
			DBG(dev, "%s transfer completed\n", epn->name);

		/* process the req queue until an uncomplete request */
		list_for_each_entry_safe(curr_req, temp_req,
				&epn->queue, queue) {
			status = process_ep_req(dev, i, curr_req);
			VDBG(dev, "%s req status: %d\n", epn->name, status);

			if (status)
				break;

			/* write back status to req */
			curr_req->req.status = status;

			/* ep0 request completion */
			if (ep_num == 0) {
				ep0_req_complete(dev, epn, curr_req);
				break;
			} else {
				done(epn, curr_req, status);
			}
		}
	}
done:
	VDBG(dev, "<--- %s()\n", __func__);
	return;
}


/* port change detect interrupt handler */
static void handle_port_change(struct langwell_udc *dev)
{
	u32	portsc1, devlc;
	u32	speed;

	VDBG(dev, "---> %s()\n", __func__);

	if (dev->bus_reset)
		dev->bus_reset = 0;

	portsc1 = readl(&dev->op_regs->portsc1);
	devlc = readl(&dev->op_regs->devlc);
	VDBG(dev, "portsc1 = 0x%08x, devlc = 0x%08x\n",
			portsc1, devlc);

	/* bus reset is finished */
	if (!(portsc1 & PORTS_PR)) {
		/* get the speed */
		speed = LPM_PSPD(devlc);
		switch (speed) {
		case LPM_SPEED_HIGH:
			dev->gadget.speed = USB_SPEED_HIGH;
			break;
		case LPM_SPEED_FULL:
			dev->gadget.speed = USB_SPEED_FULL;
			break;
		case LPM_SPEED_LOW:
			dev->gadget.speed = USB_SPEED_LOW;
			break;
		default:
			dev->gadget.speed = USB_SPEED_UNKNOWN;
			break;
		}
		VDBG(dev, "speed = %d, dev->gadget.speed = %d\n",
				speed, dev->gadget.speed);
	}

	/* LPM L0 to L1 */
	if (dev->lpm && dev->lpm_state == LPM_L0)
		if (portsc1 & PORTS_SUSP && portsc1 & PORTS_SLP) {
				INFO(dev, "LPM L0 to L1\n");
				dev->lpm_state = LPM_L1;
		}

	/* LPM L1 to L0, force resume or remote wakeup finished */
	if (dev->lpm && dev->lpm_state == LPM_L1)
		if (!(portsc1 & PORTS_SUSP)) {
			if (portsc1 & PORTS_SLP)
				INFO(dev, "LPM L1 to L0, force resume\n");
			else
				INFO(dev, "LPM L1 to L0, remote wakeup\n");

			dev->lpm_state = LPM_L0;
		}

	/* update USB state */
	if (!dev->resume_state)
		dev->usb_state = USB_STATE_DEFAULT;

	VDBG(dev, "<--- %s()\n", __func__);
}


/* USB reset interrupt handler */
static void handle_usb_reset(struct langwell_udc *dev)
{
	u32		deviceaddr,
			endptsetupstat,
			endptcomplete;
	unsigned long	timeout;

	VDBG(dev, "---> %s()\n", __func__);

	/* Write-Clear the device address */
	deviceaddr = readl(&dev->op_regs->deviceaddr);
	writel(deviceaddr & ~USBADR_MASK, &dev->op_regs->deviceaddr);

	dev->dev_addr = 0;

	/* clear usb state */
	dev->resume_state = 0;

	/* LPM L1 to L0, reset */
	if (dev->lpm)
		dev->lpm_state = LPM_L0;

	dev->ep0_dir = USB_DIR_OUT;
	dev->ep0_state = WAIT_FOR_SETUP;
	dev->remote_wakeup = 0;		/* default to 0 on reset */
	dev->gadget.b_hnp_enable = 0;
	dev->gadget.a_hnp_support = 0;
	dev->gadget.a_alt_hnp_support = 0;

	/* Write-Clear all the setup token semaphores */
	endptsetupstat = readl(&dev->op_regs->endptsetupstat);
	writel(endptsetupstat, &dev->op_regs->endptsetupstat);

	/* Write-Clear all the endpoint complete status bits */
	endptcomplete = readl(&dev->op_regs->endptcomplete);
	writel(endptcomplete, &dev->op_regs->endptcomplete);

	/* wait until all endptprime bits cleared */
	timeout = jiffies + PRIME_TIMEOUT;
	while (readl(&dev->op_regs->endptprime)) {
		if (time_after(jiffies, timeout)) {
			ERROR(dev, "USB reset timeout\n");
			break;
		}
		cpu_relax();
	}

	/* write 1s to endptflush register to clear any primed buffers */
	writel((u32) ~0, &dev->op_regs->endptflush);

	if (readl(&dev->op_regs->portsc1) & PORTS_PR) {
		VDBG(dev, "USB bus reset\n");
		/* bus is reseting */
		dev->bus_reset = 1;

		/* reset all the queues, stop all USB activities */
		stop_activity(dev, dev->driver);
		dev->usb_state = USB_STATE_DEFAULT;
	} else {
		VDBG(dev, "device controller reset\n");
		/* controller reset */
		langwell_udc_reset(dev);

		/* reset all the queues, stop all USB activities */
		stop_activity(dev, dev->driver);

		/* reset ep0 dQH and endptctrl */
		ep0_reset(dev);

		/* enable interrupt and set controller to run state */
		langwell_udc_start(dev);

		dev->usb_state = USB_STATE_ATTACHED;
	}

#ifdef	OTG_TRANSCEIVER
	/* refer to USB OTG 6.6.2.3 b_hnp_en is cleared */
	if (!dev->lotg->otg.default_a)
		dev->lotg->hsm.b_hnp_enable = 0;
#endif

	VDBG(dev, "<--- %s()\n", __func__);
}


/* USB bus suspend/resume interrupt */
static void handle_bus_suspend(struct langwell_udc *dev)
{
	u32		devlc;
	DBG(dev, "---> %s()\n", __func__);

	dev->resume_state = dev->usb_state;
	dev->usb_state = USB_STATE_SUSPENDED;

#ifdef	OTG_TRANSCEIVER
	if (dev->lotg->otg.default_a) {
		if (dev->lotg->hsm.b_bus_suspend_vld == 1) {
			dev->lotg->hsm.b_bus_suspend = 1;
			/* notify transceiver the state changes */
			if (spin_trylock(&dev->lotg->wq_lock)) {
				langwell_update_transceiver();
				spin_unlock(&dev->lotg->wq_lock);
			}
		}
		dev->lotg->hsm.b_bus_suspend_vld++;
	} else {
		if (!dev->lotg->hsm.a_bus_suspend) {
			dev->lotg->hsm.a_bus_suspend = 1;
			/* notify transceiver the state changes */
			if (spin_trylock(&dev->lotg->wq_lock)) {
				langwell_update_transceiver();
				spin_unlock(&dev->lotg->wq_lock);
			}
		}
	}
#endif

	/* report suspend to the driver */
	if (dev->driver) {
		if (dev->driver->suspend) {
			spin_unlock(&dev->lock);
			dev->driver->suspend(&dev->gadget);
			spin_lock(&dev->lock);
			DBG(dev, "suspend %s\n", dev->driver->driver.name);
		}
	}

	/* enter PHY low power suspend */
	devlc = readl(&dev->op_regs->devlc);
	VDBG(dev, "devlc = 0x%08x\n", devlc);
	devlc |= LPM_PHCD;
	writel(devlc, &dev->op_regs->devlc);

	DBG(dev, "<--- %s()\n", __func__);
}


static void handle_bus_resume(struct langwell_udc *dev)
{
	u32		devlc;
	DBG(dev, "---> %s()\n", __func__);

	dev->usb_state = dev->resume_state;
	dev->resume_state = 0;

	/* exit PHY low power suspend */
	devlc = readl(&dev->op_regs->devlc);
	VDBG(dev, "devlc = 0x%08x\n", devlc);
	devlc &= ~LPM_PHCD;
	writel(devlc, &dev->op_regs->devlc);

#ifdef	OTG_TRANSCEIVER
	if (dev->lotg->otg.default_a == 0)
		dev->lotg->hsm.a_bus_suspend = 0;
#endif

	/* report resume to the driver */
	if (dev->driver) {
		if (dev->driver->resume) {
			spin_unlock(&dev->lock);
			dev->driver->resume(&dev->gadget);
			spin_lock(&dev->lock);
			DBG(dev, "resume %s\n", dev->driver->driver.name);
		}
	}

	DBG(dev, "<--- %s()\n", __func__);
}


/* USB device controller interrupt handler */
static irqreturn_t langwell_irq(int irq, void *_dev)
{
	struct langwell_udc	*dev = _dev;
	u32			usbsts,
				usbintr,
				irq_sts,
				portsc1;

	VDBG(dev, "---> %s()\n", __func__);

	if (dev->stopped) {
		VDBG(dev, "handle IRQ_NONE\n");
		VDBG(dev, "<--- %s()\n", __func__);
		return IRQ_NONE;
	}

	spin_lock(&dev->lock);

	/* USB status */
	usbsts = readl(&dev->op_regs->usbsts);

	/* USB interrupt enable */
	usbintr = readl(&dev->op_regs->usbintr);

	irq_sts = usbsts & usbintr;
	VDBG(dev, "usbsts = 0x%08x, usbintr = 0x%08x, irq_sts = 0x%08x\n",
			usbsts, usbintr, irq_sts);

	if (!irq_sts) {
		VDBG(dev, "handle IRQ_NONE\n");
		VDBG(dev, "<--- %s()\n", __func__);
		spin_unlock(&dev->lock);
		return IRQ_NONE;
	}

	/* Write-Clear interrupt status bits */
	writel(irq_sts, &dev->op_regs->usbsts);

	/* resume from suspend */
	portsc1 = readl(&dev->op_regs->portsc1);
	if (dev->usb_state == USB_STATE_SUSPENDED)
		if (!(portsc1 & PORTS_SUSP))
			handle_bus_resume(dev);

	/* USB interrupt */
	if (irq_sts & STS_UI) {
		VDBG(dev, "USB interrupt\n");

		/* setup packet received from ep0 */
		if (readl(&dev->op_regs->endptsetupstat)
				& EP0SETUPSTAT_MASK) {
			VDBG(dev, "USB SETUP packet received interrupt\n");
			/* setup tripwire semaphone */
			setup_tripwire(dev);
			handle_setup_packet(dev, &dev->local_setup_buff);
		}

		/* USB transfer completion */
		if (readl(&dev->op_regs->endptcomplete)) {
			VDBG(dev, "USB transfer completion interrupt\n");
			handle_trans_complete(dev);
		}
	}

	/* SOF received interrupt (for ISO transfer) */
	if (irq_sts & STS_SRI) {
		/* FIXME */
		/* VDBG(dev, "SOF received interrupt\n"); */
	}

	/* port change detect interrupt */
	if (irq_sts & STS_PCI) {
		VDBG(dev, "port change detect interrupt\n");
		handle_port_change(dev);
	}

	/* suspend interrrupt */
	if (irq_sts & STS_SLI) {
		VDBG(dev, "suspend interrupt\n");
		handle_bus_suspend(dev);
	}

	/* USB reset interrupt */
	if (irq_sts & STS_URI) {
		VDBG(dev, "USB reset interrupt\n");
		handle_usb_reset(dev);
	}

	/* USB error or system error interrupt */
	if (irq_sts & (STS_UEI | STS_SEI)) {
		/* FIXME */
		WARNING(dev, "error IRQ, irq_sts: %x\n", irq_sts);
	}

	spin_unlock(&dev->lock);

	VDBG(dev, "<--- %s()\n", __func__);
	return IRQ_HANDLED;
}


/*-------------------------------------------------------------------------*/

/* release device structure */
static void gadget_release(struct device *_dev)
{
	struct langwell_udc	*dev = the_controller;

	DBG(dev, "---> %s()\n", __func__);

	complete(dev->done);

	DBG(dev, "<--- %s()\n", __func__);
	kfree(dev);
}


/* tear down the binding between this driver and the pci device */
static void langwell_udc_remove(struct pci_dev *pdev)
{
	struct langwell_udc	*dev = the_controller;

	DECLARE_COMPLETION(done);

	BUG_ON(dev->driver);
	DBG(dev, "---> %s()\n", __func__);

	dev->done = &done;

	/* free memory allocated in probe */
	if (dev->dtd_pool)
		dma_pool_destroy(dev->dtd_pool);

	if (dev->status_req) {
		kfree(dev->status_req->req.buf);
		kfree(dev->status_req);
	}

	if (dev->ep_dqh)
		dma_free_coherent(&pdev->dev, dev->ep_dqh_size,
			dev->ep_dqh, dev->ep_dqh_dma);

	kfree(dev->ep);

	/* diable IRQ handler */
	if (dev->got_irq)
		free_irq(pdev->irq, dev);

#ifndef	OTG_TRANSCEIVER
	if (dev->cap_regs)
		iounmap(dev->cap_regs);

	if (dev->region)
		release_mem_region(pci_resource_start(pdev, 0),
				pci_resource_len(pdev, 0));

	if (dev->enabled)
		pci_disable_device(pdev);
#else
	if (dev->transceiver) {
		otg_put_transceiver(dev->transceiver);
		dev->transceiver = NULL;
		dev->lotg = NULL;
	}
#endif

	dev->cap_regs = NULL;

	INFO(dev, "unbind\n");
	DBG(dev, "<--- %s()\n", __func__);

	device_unregister(&dev->gadget.dev);
	device_remove_file(&pdev->dev, &dev_attr_langwell_udc);

#ifndef	OTG_TRANSCEIVER
	pci_set_drvdata(pdev, NULL);
#endif

	/* free dev, wait for the release() finished */
	wait_for_completion(&done);

	the_controller = NULL;
}


/*
 * wrap this driver around the specified device, but
 * don't respond over USB until a gadget driver binds to us.
 */
static int langwell_udc_probe(struct pci_dev *pdev,
		const struct pci_device_id *id)
{
	struct langwell_udc	*dev;
#ifndef	OTG_TRANSCEIVER
	unsigned long		resource, len;
#endif
	void			__iomem *base = NULL;
	size_t			size;
	int			retval;

	if (the_controller) {
		dev_warn(&pdev->dev, "ignoring\n");
		return -EBUSY;
	}

	/* alloc, and start init */
	dev = kzalloc(sizeof *dev, GFP_KERNEL);
	if (dev == NULL) {
		retval = -ENOMEM;
		goto error;
	}

	/* initialize device spinlock */
	spin_lock_init(&dev->lock);

	dev->pdev = pdev;
	DBG(dev, "---> %s()\n", __func__);

#ifdef	OTG_TRANSCEIVER
	/* PCI device is already enabled by otg_transceiver driver */
	dev->enabled = 1;

	/* mem region and register base */
	dev->region = 1;
	dev->transceiver = otg_get_transceiver();
	dev->lotg = otg_to_langwell(dev->transceiver);
	base = dev->lotg->regs;
#else
	pci_set_drvdata(pdev, dev);

	/* now all the pci goodies ... */
	if (pci_enable_device(pdev) < 0) {
		retval = -ENODEV;
		goto error;
	}
	dev->enabled = 1;

	/* control register: BAR 0 */
	resource = pci_resource_start(pdev, 0);
	len = pci_resource_len(pdev, 0);
	if (!request_mem_region(resource, len, driver_name)) {
		ERROR(dev, "controller already in use\n");
		retval = -EBUSY;
		goto error;
	}
	dev->region = 1;

	base = ioremap_nocache(resource, len);
#endif
	if (base == NULL) {
		ERROR(dev, "can't map memory\n");
		retval = -EFAULT;
		goto error;
	}

	dev->cap_regs = (struct langwell_cap_regs __iomem *) base;
	VDBG(dev, "dev->cap_regs: %p\n", dev->cap_regs);
	dev->op_regs = (struct langwell_op_regs __iomem *)
		(base + OP_REG_OFFSET);
	VDBG(dev, "dev->op_regs: %p\n", dev->op_regs);

	/* irq setup after old hardware is cleaned up */
	if (!pdev->irq) {
		ERROR(dev, "No IRQ. Check PCI setup!\n");
		retval = -ENODEV;
		goto error;
	}

#ifndef	OTG_TRANSCEIVER
	INFO(dev, "irq %d, io mem: 0x%08lx, len: 0x%08lx, pci mem 0x%p\n",
			pdev->irq, resource, len, base);
	/* enables bus-mastering for device dev */
	pci_set_master(pdev);

	if (request_irq(pdev->irq, langwell_irq, IRQF_SHARED,
				driver_name, dev) != 0) {
		ERROR(dev, "request interrupt %d failed\n", pdev->irq);
		retval = -EBUSY;
		goto error;
	}
	dev->got_irq = 1;
#endif

	/* set stopped bit */
	dev->stopped = 1;

	/* capabilities and endpoint number */
	dev->lpm = (readl(&dev->cap_regs->hccparams) & HCC_LEN) ? 1 : 0;
	dev->dciversion = readw(&dev->cap_regs->dciversion);
	dev->devcap = (readl(&dev->cap_regs->dccparams) & DEVCAP) ? 1 : 0;
	VDBG(dev, "dev->lpm: %d\n", dev->lpm);
	VDBG(dev, "dev->dciversion: 0x%04x\n", dev->dciversion);
	VDBG(dev, "dccparams: 0x%08x\n", readl(&dev->cap_regs->dccparams));
	VDBG(dev, "dev->devcap: %d\n", dev->devcap);
	if (!dev->devcap) {
		ERROR(dev, "can't support device mode\n");
		retval = -ENODEV;
		goto error;
	}

	/* a pair of endpoints (out/in) for each address */
	dev->ep_max = DEN(readl(&dev->cap_regs->dccparams)) * 2;
	VDBG(dev, "dev->ep_max: %d\n", dev->ep_max);

	/* allocate endpoints memory */
	dev->ep = kzalloc(sizeof(struct langwell_ep) * dev->ep_max,
			GFP_KERNEL);
	if (!dev->ep) {
		ERROR(dev, "allocate endpoints memory failed\n");
		retval = -ENOMEM;
		goto error;
	}

	/* allocate device dQH memory */
	size = dev->ep_max * sizeof(struct langwell_dqh);
	VDBG(dev, "orig size = %d\n", size);
	if (size < DQH_ALIGNMENT)
		size = DQH_ALIGNMENT;
	else if ((size % DQH_ALIGNMENT) != 0) {
		size += DQH_ALIGNMENT + 1;
		size &= ~(DQH_ALIGNMENT - 1);
	}
	dev->ep_dqh = dma_alloc_coherent(&pdev->dev, size,
					&dev->ep_dqh_dma, GFP_KERNEL);
	if (!dev->ep_dqh) {
		ERROR(dev, "allocate dQH memory failed\n");
		retval = -ENOMEM;
		goto error;
	}
	dev->ep_dqh_size = size;
	VDBG(dev, "ep_dqh_size = %d\n", dev->ep_dqh_size);

	/* initialize ep0 status request structure */
	dev->status_req = kzalloc(sizeof(struct langwell_request), GFP_KERNEL);
	if (!dev->status_req) {
		ERROR(dev, "allocate status_req memory failed\n");
		retval = -ENOMEM;
		goto error;
	}
	INIT_LIST_HEAD(&dev->status_req->queue);

	/* allocate a small amount of memory to get valid address */
	dev->status_req->req.buf = kmalloc(8, GFP_KERNEL);
	dev->status_req->req.dma = virt_to_phys(dev->status_req->req.buf);

	dev->resume_state = USB_STATE_NOTATTACHED;
	dev->usb_state = USB_STATE_POWERED;
	dev->ep0_dir = USB_DIR_OUT;
	dev->remote_wakeup = 0;	/* default to 0 on reset */

#ifndef	OTG_TRANSCEIVER
	/* reset device controller */
	langwell_udc_reset(dev);
#endif

	/* initialize gadget structure */
	dev->gadget.ops = &langwell_ops;	/* usb_gadget_ops */
	dev->gadget.ep0 = &dev->ep[0].ep;	/* gadget ep0 */
	INIT_LIST_HEAD(&dev->gadget.ep_list);	/* ep_list */
	dev->gadget.speed = USB_SPEED_UNKNOWN;	/* speed */
	dev->gadget.is_dualspeed = 1;		/* support dual speed */
#ifdef	OTG_TRANSCEIVER
	dev->gadget.is_otg = 1;			/* support otg mode */
#endif

	/* the "gadget" abstracts/virtualizes the controller */
	dev_set_name(&dev->gadget.dev, "gadget");
	dev->gadget.dev.parent = &pdev->dev;
	dev->gadget.dev.dma_mask = pdev->dev.dma_mask;
	dev->gadget.dev.release = gadget_release;
	dev->gadget.name = driver_name;		/* gadget name */

	/* controller endpoints reinit */
	eps_reinit(dev);

#ifndef	OTG_TRANSCEIVER
	/* reset ep0 dQH and endptctrl */
	ep0_reset(dev);
#endif

	/* create dTD dma_pool resource */
	dev->dtd_pool = dma_pool_create("langwell_dtd",
			&dev->pdev->dev,
			sizeof(struct langwell_dtd),
			DTD_ALIGNMENT,
			DMA_BOUNDARY);

	if (!dev->dtd_pool) {
		retval = -ENOMEM;
		goto error;
	}

	/* done */
	INFO(dev, "%s\n", driver_desc);
	INFO(dev, "irq %d, pci mem %p\n", pdev->irq, base);
	INFO(dev, "Driver version: " DRIVER_VERSION "\n");
	INFO(dev, "Support (max) %d endpoints\n", dev->ep_max);
	INFO(dev, "Device interface version: 0x%04x\n", dev->dciversion);
	INFO(dev, "Controller mode: %s\n", dev->devcap ? "Device" : "Host");
	INFO(dev, "Support USB LPM: %s\n", dev->lpm ? "Yes" : "No");

	VDBG(dev, "After langwell_udc_probe(), print all registers:\n");
#ifdef	VERBOSE
	print_all_registers(dev);
#endif

	the_controller = dev;

	retval = device_register(&dev->gadget.dev);
	if (retval)
		goto error;

	retval = device_create_file(&pdev->dev, &dev_attr_langwell_udc);
	if (retval)
		goto error;

	VDBG(dev, "<--- %s()\n", __func__);
	return 0;

error:
	if (dev) {
		DBG(dev, "<--- %s()\n", __func__);
		langwell_udc_remove(pdev);
	}

	return retval;
}


/* device controller suspend */
static int langwell_udc_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct langwell_udc	*dev = the_controller;
	u32			devlc;

	DBG(dev, "---> %s()\n", __func__);

	/* disable interrupt and set controller to stop state */
	langwell_udc_stop(dev);

	/* diable IRQ handler */
	if (dev->got_irq)
		free_irq(pdev->irq, dev);
	dev->got_irq = 0;


	/* save PCI state */
	pci_save_state(pdev);

	/* set device power state */
	pci_set_power_state(pdev, PCI_D3hot);

	/* enter PHY low power suspend */
	devlc = readl(&dev->op_regs->devlc);
	VDBG(dev, "devlc = 0x%08x\n", devlc);
	devlc |= LPM_PHCD;
	writel(devlc, &dev->op_regs->devlc);

	DBG(dev, "<--- %s()\n", __func__);
	return 0;
}


/* device controller resume */
static int langwell_udc_resume(struct pci_dev *pdev)
{
	struct langwell_udc	*dev = the_controller;
	u32			devlc;

	DBG(dev, "---> %s()\n", __func__);

	/* exit PHY low power suspend */
	devlc = readl(&dev->op_regs->devlc);
	VDBG(dev, "devlc = 0x%08x\n", devlc);
	devlc &= ~LPM_PHCD;
	writel(devlc, &dev->op_regs->devlc);

	/* set device D0 power state */
	pci_set_power_state(pdev, PCI_D0);

	/* restore PCI state */
	pci_restore_state(pdev);

	/* enable IRQ handler */
	if (request_irq(pdev->irq, langwell_irq, IRQF_SHARED, driver_name, dev)
			!= 0) {
		ERROR(dev, "request interrupt %d failed\n", pdev->irq);
		return -1;
	}
	dev->got_irq = 1;

	/* reset and start controller to run state */
	if (dev->stopped) {
		/* reset device controller */
		langwell_udc_reset(dev);

		/* reset ep0 dQH and endptctrl */
		ep0_reset(dev);

		/* start device if gadget is loaded */
		if (dev->driver)
			langwell_udc_start(dev);
	}

	/* reset USB status */
	dev->usb_state = USB_STATE_ATTACHED;
	dev->ep0_state = WAIT_FOR_SETUP;
	dev->ep0_dir = USB_DIR_OUT;

	DBG(dev, "<--- %s()\n", __func__);
	return 0;
}


/* pci driver shutdown */
static void langwell_udc_shutdown(struct pci_dev *pdev)
{
	struct langwell_udc	*dev = the_controller;
	u32			usbmode;

	DBG(dev, "---> %s()\n", __func__);

	/* reset controller mode to IDLE */
	usbmode = readl(&dev->op_regs->usbmode);
	DBG(dev, "usbmode = 0x%08x\n", usbmode);
	usbmode &= (~3 | MODE_IDLE);
	writel(usbmode, &dev->op_regs->usbmode);

	DBG(dev, "<--- %s()\n", __func__);
}

/*-------------------------------------------------------------------------*/

static const struct pci_device_id pci_ids[] = { {
	.class =	((PCI_CLASS_SERIAL_USB << 8) | 0xfe),
	.class_mask =	~0,
	.vendor =	0x8086,
	.device =	0x0811,
	.subvendor =	PCI_ANY_ID,
	.subdevice =	PCI_ANY_ID,
}, { /* end: all zeroes */ }
};


MODULE_DEVICE_TABLE(pci, pci_ids);


static struct pci_driver langwell_pci_driver = {
	.name =		(char *) driver_name,
	.id_table =	pci_ids,

	.probe =	langwell_udc_probe,
	.remove =	langwell_udc_remove,

	/* device controller suspend/resume */
	.suspend =	langwell_udc_suspend,
	.resume =	langwell_udc_resume,

	.shutdown =	langwell_udc_shutdown,
};


MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR("Xiaochen Shen <xiaochen.shen@intel.com>");
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL");


static int __init init(void)
{
#ifdef	OTG_TRANSCEIVER
	return langwell_register_peripheral(&langwell_pci_driver);
#else
	return pci_register_driver(&langwell_pci_driver);
#endif
}
module_init(init);


static void __exit cleanup(void)
{
#ifdef	OTG_TRANSCEIVER
	return langwell_unregister_peripheral(&langwell_pci_driver);
#else
	pci_unregister_driver(&langwell_pci_driver);
#endif
}
module_exit(cleanup);

