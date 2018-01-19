// SPDX-License-Identifier: GPL-2.0+
/*
 * amd5536.c -- AMD 5536 UDC high/full speed USB device controller
 *
 * Copyright (C) 2005-2007 AMD (http://www.amd.com)
 * Author: Thomas Dahlmann
 */

/*
 * This file does the core driver implementation for the UDC that is based
 * on Synopsys device controller IP (different than HS OTG IP) that is either
 * connected through PCI bus or integrated to SoC platforms.
 */

/* Driver strings */
#define UDC_MOD_DESCRIPTION		"Synopsys USB Device Controller"
#define UDC_DRIVER_VERSION_STRING	"01.00.0206"

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/ioctl.h>
#include <linux/fs.h>
#include <linux/dmapool.h>
#include <linux/prefetch.h>
#include <linux/moduleparam.h>
#include <asm/byteorder.h>
#include <asm/unaligned.h>
#include "amd5536udc.h"

static void udc_tasklet_disconnect(unsigned long);
static void udc_setup_endpoints(struct udc *dev);
static void udc_soft_reset(struct udc *dev);
static struct udc_request *udc_alloc_bna_dummy(struct udc_ep *ep);
static void udc_free_request(struct usb_ep *usbep, struct usb_request *usbreq);

/* description */
static const char mod_desc[] = UDC_MOD_DESCRIPTION;
static const char name[] = "udc";

/* structure to hold endpoint function pointers */
static const struct usb_ep_ops udc_ep_ops;

/* received setup data */
static union udc_setup_data setup_data;

/* pointer to device object */
static struct udc *udc;

/* irq spin lock for soft reset */
static DEFINE_SPINLOCK(udc_irq_spinlock);
/* stall spin lock */
static DEFINE_SPINLOCK(udc_stall_spinlock);

/*
* slave mode: pending bytes in rx fifo after nyet,
* used if EPIN irq came but no req was available
*/
static unsigned int udc_rxfifo_pending;

/* count soft resets after suspend to avoid loop */
static int soft_reset_occured;
static int soft_reset_after_usbreset_occured;

/* timer */
static struct timer_list udc_timer;
static int stop_timer;

/* set_rde -- Is used to control enabling of RX DMA. Problem is
 * that UDC has only one bit (RDE) to enable/disable RX DMA for
 * all OUT endpoints. So we have to handle race conditions like
 * when OUT data reaches the fifo but no request was queued yet.
 * This cannot be solved by letting the RX DMA disabled until a
 * request gets queued because there may be other OUT packets
 * in the FIFO (important for not blocking control traffic).
 * The value of set_rde controls the correspondig timer.
 *
 * set_rde -1 == not used, means it is alloed to be set to 0 or 1
 * set_rde  0 == do not touch RDE, do no start the RDE timer
 * set_rde  1 == timer function will look whether FIFO has data
 * set_rde  2 == set by timer function to enable RX DMA on next call
 */
static int set_rde = -1;

static DECLARE_COMPLETION(on_exit);
static struct timer_list udc_pollstall_timer;
static int stop_pollstall_timer;
static DECLARE_COMPLETION(on_pollstall_exit);

/* tasklet for usb disconnect */
static DECLARE_TASKLET(disconnect_tasklet, udc_tasklet_disconnect,
		(unsigned long) &udc);


/* endpoint names used for print */
static const char ep0_string[] = "ep0in";
static const struct {
	const char *name;
	const struct usb_ep_caps caps;
} ep_info[] = {
#define EP_INFO(_name, _caps) \
	{ \
		.name = _name, \
		.caps = _caps, \
	}

	EP_INFO(ep0_string,
		USB_EP_CAPS(USB_EP_CAPS_TYPE_CONTROL, USB_EP_CAPS_DIR_IN)),
	EP_INFO("ep1in-int",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_BULK, USB_EP_CAPS_DIR_IN)),
	EP_INFO("ep2in-bulk",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_BULK, USB_EP_CAPS_DIR_IN)),
	EP_INFO("ep3in-bulk",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_BULK, USB_EP_CAPS_DIR_IN)),
	EP_INFO("ep4in-bulk",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_BULK, USB_EP_CAPS_DIR_IN)),
	EP_INFO("ep5in-bulk",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_BULK, USB_EP_CAPS_DIR_IN)),
	EP_INFO("ep6in-bulk",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_BULK, USB_EP_CAPS_DIR_IN)),
	EP_INFO("ep7in-bulk",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_BULK, USB_EP_CAPS_DIR_IN)),
	EP_INFO("ep8in-bulk",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_BULK, USB_EP_CAPS_DIR_IN)),
	EP_INFO("ep9in-bulk",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_BULK, USB_EP_CAPS_DIR_IN)),
	EP_INFO("ep10in-bulk",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_BULK, USB_EP_CAPS_DIR_IN)),
	EP_INFO("ep11in-bulk",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_BULK, USB_EP_CAPS_DIR_IN)),
	EP_INFO("ep12in-bulk",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_BULK, USB_EP_CAPS_DIR_IN)),
	EP_INFO("ep13in-bulk",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_BULK, USB_EP_CAPS_DIR_IN)),
	EP_INFO("ep14in-bulk",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_BULK, USB_EP_CAPS_DIR_IN)),
	EP_INFO("ep15in-bulk",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_BULK, USB_EP_CAPS_DIR_IN)),
	EP_INFO("ep0out",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_CONTROL, USB_EP_CAPS_DIR_OUT)),
	EP_INFO("ep1out-bulk",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_BULK, USB_EP_CAPS_DIR_OUT)),
	EP_INFO("ep2out-bulk",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_BULK, USB_EP_CAPS_DIR_OUT)),
	EP_INFO("ep3out-bulk",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_BULK, USB_EP_CAPS_DIR_OUT)),
	EP_INFO("ep4out-bulk",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_BULK, USB_EP_CAPS_DIR_OUT)),
	EP_INFO("ep5out-bulk",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_BULK, USB_EP_CAPS_DIR_OUT)),
	EP_INFO("ep6out-bulk",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_BULK, USB_EP_CAPS_DIR_OUT)),
	EP_INFO("ep7out-bulk",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_BULK, USB_EP_CAPS_DIR_OUT)),
	EP_INFO("ep8out-bulk",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_BULK, USB_EP_CAPS_DIR_OUT)),
	EP_INFO("ep9out-bulk",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_BULK, USB_EP_CAPS_DIR_OUT)),
	EP_INFO("ep10out-bulk",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_BULK, USB_EP_CAPS_DIR_OUT)),
	EP_INFO("ep11out-bulk",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_BULK, USB_EP_CAPS_DIR_OUT)),
	EP_INFO("ep12out-bulk",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_BULK, USB_EP_CAPS_DIR_OUT)),
	EP_INFO("ep13out-bulk",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_BULK, USB_EP_CAPS_DIR_OUT)),
	EP_INFO("ep14out-bulk",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_BULK, USB_EP_CAPS_DIR_OUT)),
	EP_INFO("ep15out-bulk",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_BULK, USB_EP_CAPS_DIR_OUT)),

#undef EP_INFO
};

/* buffer fill mode */
static int use_dma_bufferfill_mode;
/* tx buffer size for high speed */
static unsigned long hs_tx_buf = UDC_EPIN_BUFF_SIZE;

/*---------------------------------------------------------------------------*/
/* Prints UDC device registers and endpoint irq registers */
static void print_regs(struct udc *dev)
{
	DBG(dev, "------- Device registers -------\n");
	DBG(dev, "dev config     = %08x\n", readl(&dev->regs->cfg));
	DBG(dev, "dev control    = %08x\n", readl(&dev->regs->ctl));
	DBG(dev, "dev status     = %08x\n", readl(&dev->regs->sts));
	DBG(dev, "\n");
	DBG(dev, "dev int's      = %08x\n", readl(&dev->regs->irqsts));
	DBG(dev, "dev intmask    = %08x\n", readl(&dev->regs->irqmsk));
	DBG(dev, "\n");
	DBG(dev, "dev ep int's   = %08x\n", readl(&dev->regs->ep_irqsts));
	DBG(dev, "dev ep intmask = %08x\n", readl(&dev->regs->ep_irqmsk));
	DBG(dev, "\n");
	DBG(dev, "USE DMA        = %d\n", use_dma);
	if (use_dma && use_dma_ppb && !use_dma_ppb_du) {
		DBG(dev, "DMA mode       = PPBNDU (packet per buffer "
			"WITHOUT desc. update)\n");
		dev_info(dev->dev, "DMA mode (%s)\n", "PPBNDU");
	} else if (use_dma && use_dma_ppb && use_dma_ppb_du) {
		DBG(dev, "DMA mode       = PPBDU (packet per buffer "
			"WITH desc. update)\n");
		dev_info(dev->dev, "DMA mode (%s)\n", "PPBDU");
	}
	if (use_dma && use_dma_bufferfill_mode) {
		DBG(dev, "DMA mode       = BF (buffer fill mode)\n");
		dev_info(dev->dev, "DMA mode (%s)\n", "BF");
	}
	if (!use_dma)
		dev_info(dev->dev, "FIFO mode\n");
	DBG(dev, "-------------------------------------------------------\n");
}

/* Masks unused interrupts */
int udc_mask_unused_interrupts(struct udc *dev)
{
	u32 tmp;

	/* mask all dev interrupts */
	tmp =	AMD_BIT(UDC_DEVINT_SVC) |
		AMD_BIT(UDC_DEVINT_ENUM) |
		AMD_BIT(UDC_DEVINT_US) |
		AMD_BIT(UDC_DEVINT_UR) |
		AMD_BIT(UDC_DEVINT_ES) |
		AMD_BIT(UDC_DEVINT_SI) |
		AMD_BIT(UDC_DEVINT_SOF)|
		AMD_BIT(UDC_DEVINT_SC);
	writel(tmp, &dev->regs->irqmsk);

	/* mask all ep interrupts */
	writel(UDC_EPINT_MSK_DISABLE_ALL, &dev->regs->ep_irqmsk);

	return 0;
}
EXPORT_SYMBOL_GPL(udc_mask_unused_interrupts);

/* Enables endpoint 0 interrupts */
static int udc_enable_ep0_interrupts(struct udc *dev)
{
	u32 tmp;

	DBG(dev, "udc_enable_ep0_interrupts()\n");

	/* read irq mask */
	tmp = readl(&dev->regs->ep_irqmsk);
	/* enable ep0 irq's */
	tmp &= AMD_UNMASK_BIT(UDC_EPINT_IN_EP0)
		& AMD_UNMASK_BIT(UDC_EPINT_OUT_EP0);
	writel(tmp, &dev->regs->ep_irqmsk);

	return 0;
}

/* Enables device interrupts for SET_INTF and SET_CONFIG */
int udc_enable_dev_setup_interrupts(struct udc *dev)
{
	u32 tmp;

	DBG(dev, "enable device interrupts for setup data\n");

	/* read irq mask */
	tmp = readl(&dev->regs->irqmsk);

	/* enable SET_INTERFACE, SET_CONFIG and other needed irq's */
	tmp &= AMD_UNMASK_BIT(UDC_DEVINT_SI)
		& AMD_UNMASK_BIT(UDC_DEVINT_SC)
		& AMD_UNMASK_BIT(UDC_DEVINT_UR)
		& AMD_UNMASK_BIT(UDC_DEVINT_SVC)
		& AMD_UNMASK_BIT(UDC_DEVINT_ENUM);
	writel(tmp, &dev->regs->irqmsk);

	return 0;
}
EXPORT_SYMBOL_GPL(udc_enable_dev_setup_interrupts);

/* Calculates fifo start of endpoint based on preceding endpoints */
static int udc_set_txfifo_addr(struct udc_ep *ep)
{
	struct udc	*dev;
	u32 tmp;
	int i;

	if (!ep || !(ep->in))
		return -EINVAL;

	dev = ep->dev;
	ep->txfifo = dev->txfifo;

	/* traverse ep's */
	for (i = 0; i < ep->num; i++) {
		if (dev->ep[i].regs) {
			/* read fifo size */
			tmp = readl(&dev->ep[i].regs->bufin_framenum);
			tmp = AMD_GETBITS(tmp, UDC_EPIN_BUFF_SIZE);
			ep->txfifo += tmp;
		}
	}
	return 0;
}

/* CNAK pending field: bit0 = ep0in, bit16 = ep0out */
static u32 cnak_pending;

static void UDC_QUEUE_CNAK(struct udc_ep *ep, unsigned num)
{
	if (readl(&ep->regs->ctl) & AMD_BIT(UDC_EPCTL_NAK)) {
		DBG(ep->dev, "NAK could not be cleared for ep%d\n", num);
		cnak_pending |= 1 << (num);
		ep->naking = 1;
	} else
		cnak_pending = cnak_pending & (~(1 << (num)));
}


/* Enables endpoint, is called by gadget driver */
static int
udc_ep_enable(struct usb_ep *usbep, const struct usb_endpoint_descriptor *desc)
{
	struct udc_ep		*ep;
	struct udc		*dev;
	u32			tmp;
	unsigned long		iflags;
	u8 udc_csr_epix;
	unsigned		maxpacket;

	if (!usbep
			|| usbep->name == ep0_string
			|| !desc
			|| desc->bDescriptorType != USB_DT_ENDPOINT)
		return -EINVAL;

	ep = container_of(usbep, struct udc_ep, ep);
	dev = ep->dev;

	DBG(dev, "udc_ep_enable() ep %d\n", ep->num);

	if (!dev->driver || dev->gadget.speed == USB_SPEED_UNKNOWN)
		return -ESHUTDOWN;

	spin_lock_irqsave(&dev->lock, iflags);
	ep->ep.desc = desc;

	ep->halted = 0;

	/* set traffic type */
	tmp = readl(&dev->ep[ep->num].regs->ctl);
	tmp = AMD_ADDBITS(tmp, desc->bmAttributes, UDC_EPCTL_ET);
	writel(tmp, &dev->ep[ep->num].regs->ctl);

	/* set max packet size */
	maxpacket = usb_endpoint_maxp(desc);
	tmp = readl(&dev->ep[ep->num].regs->bufout_maxpkt);
	tmp = AMD_ADDBITS(tmp, maxpacket, UDC_EP_MAX_PKT_SIZE);
	ep->ep.maxpacket = maxpacket;
	writel(tmp, &dev->ep[ep->num].regs->bufout_maxpkt);

	/* IN ep */
	if (ep->in) {

		/* ep ix in UDC CSR register space */
		udc_csr_epix = ep->num;

		/* set buffer size (tx fifo entries) */
		tmp = readl(&dev->ep[ep->num].regs->bufin_framenum);
		/* double buffering: fifo size = 2 x max packet size */
		tmp = AMD_ADDBITS(
				tmp,
				maxpacket * UDC_EPIN_BUFF_SIZE_MULT
					  / UDC_DWORD_BYTES,
				UDC_EPIN_BUFF_SIZE);
		writel(tmp, &dev->ep[ep->num].regs->bufin_framenum);

		/* calc. tx fifo base addr */
		udc_set_txfifo_addr(ep);

		/* flush fifo */
		tmp = readl(&ep->regs->ctl);
		tmp |= AMD_BIT(UDC_EPCTL_F);
		writel(tmp, &ep->regs->ctl);

	/* OUT ep */
	} else {
		/* ep ix in UDC CSR register space */
		udc_csr_epix = ep->num - UDC_CSR_EP_OUT_IX_OFS;

		/* set max packet size UDC CSR	*/
		tmp = readl(&dev->csr->ne[ep->num - UDC_CSR_EP_OUT_IX_OFS]);
		tmp = AMD_ADDBITS(tmp, maxpacket,
					UDC_CSR_NE_MAX_PKT);
		writel(tmp, &dev->csr->ne[ep->num - UDC_CSR_EP_OUT_IX_OFS]);

		if (use_dma && !ep->in) {
			/* alloc and init BNA dummy request */
			ep->bna_dummy_req = udc_alloc_bna_dummy(ep);
			ep->bna_occurred = 0;
		}

		if (ep->num != UDC_EP0OUT_IX)
			dev->data_ep_enabled = 1;
	}

	/* set ep values */
	tmp = readl(&dev->csr->ne[udc_csr_epix]);
	/* max packet */
	tmp = AMD_ADDBITS(tmp, maxpacket, UDC_CSR_NE_MAX_PKT);
	/* ep number */
	tmp = AMD_ADDBITS(tmp, desc->bEndpointAddress, UDC_CSR_NE_NUM);
	/* ep direction */
	tmp = AMD_ADDBITS(tmp, ep->in, UDC_CSR_NE_DIR);
	/* ep type */
	tmp = AMD_ADDBITS(tmp, desc->bmAttributes, UDC_CSR_NE_TYPE);
	/* ep config */
	tmp = AMD_ADDBITS(tmp, ep->dev->cur_config, UDC_CSR_NE_CFG);
	/* ep interface */
	tmp = AMD_ADDBITS(tmp, ep->dev->cur_intf, UDC_CSR_NE_INTF);
	/* ep alt */
	tmp = AMD_ADDBITS(tmp, ep->dev->cur_alt, UDC_CSR_NE_ALT);
	/* write reg */
	writel(tmp, &dev->csr->ne[udc_csr_epix]);

	/* enable ep irq */
	tmp = readl(&dev->regs->ep_irqmsk);
	tmp &= AMD_UNMASK_BIT(ep->num);
	writel(tmp, &dev->regs->ep_irqmsk);

	/*
	 * clear NAK by writing CNAK
	 * avoid BNA for OUT DMA, don't clear NAK until DMA desc. written
	 */
	if (!use_dma || ep->in) {
		tmp = readl(&ep->regs->ctl);
		tmp |= AMD_BIT(UDC_EPCTL_CNAK);
		writel(tmp, &ep->regs->ctl);
		ep->naking = 0;
		UDC_QUEUE_CNAK(ep, ep->num);
	}
	tmp = desc->bEndpointAddress;
	DBG(dev, "%s enabled\n", usbep->name);

	spin_unlock_irqrestore(&dev->lock, iflags);
	return 0;
}

/* Resets endpoint */
static void ep_init(struct udc_regs __iomem *regs, struct udc_ep *ep)
{
	u32		tmp;

	VDBG(ep->dev, "ep-%d reset\n", ep->num);
	ep->ep.desc = NULL;
	ep->ep.ops = &udc_ep_ops;
	INIT_LIST_HEAD(&ep->queue);

	usb_ep_set_maxpacket_limit(&ep->ep,(u16) ~0);
	/* set NAK */
	tmp = readl(&ep->regs->ctl);
	tmp |= AMD_BIT(UDC_EPCTL_SNAK);
	writel(tmp, &ep->regs->ctl);
	ep->naking = 1;

	/* disable interrupt */
	tmp = readl(&regs->ep_irqmsk);
	tmp |= AMD_BIT(ep->num);
	writel(tmp, &regs->ep_irqmsk);

	if (ep->in) {
		/* unset P and IN bit of potential former DMA */
		tmp = readl(&ep->regs->ctl);
		tmp &= AMD_UNMASK_BIT(UDC_EPCTL_P);
		writel(tmp, &ep->regs->ctl);

		tmp = readl(&ep->regs->sts);
		tmp |= AMD_BIT(UDC_EPSTS_IN);
		writel(tmp, &ep->regs->sts);

		/* flush the fifo */
		tmp = readl(&ep->regs->ctl);
		tmp |= AMD_BIT(UDC_EPCTL_F);
		writel(tmp, &ep->regs->ctl);

	}
	/* reset desc pointer */
	writel(0, &ep->regs->desptr);
}

/* Disables endpoint, is called by gadget driver */
static int udc_ep_disable(struct usb_ep *usbep)
{
	struct udc_ep	*ep = NULL;
	unsigned long	iflags;

	if (!usbep)
		return -EINVAL;

	ep = container_of(usbep, struct udc_ep, ep);
	if (usbep->name == ep0_string || !ep->ep.desc)
		return -EINVAL;

	DBG(ep->dev, "Disable ep-%d\n", ep->num);

	spin_lock_irqsave(&ep->dev->lock, iflags);
	udc_free_request(&ep->ep, &ep->bna_dummy_req->req);
	empty_req_queue(ep);
	ep_init(ep->dev->regs, ep);
	spin_unlock_irqrestore(&ep->dev->lock, iflags);

	return 0;
}

/* Allocates request packet, called by gadget driver */
static struct usb_request *
udc_alloc_request(struct usb_ep *usbep, gfp_t gfp)
{
	struct udc_request	*req;
	struct udc_data_dma	*dma_desc;
	struct udc_ep	*ep;

	if (!usbep)
		return NULL;

	ep = container_of(usbep, struct udc_ep, ep);

	VDBG(ep->dev, "udc_alloc_req(): ep%d\n", ep->num);
	req = kzalloc(sizeof(struct udc_request), gfp);
	if (!req)
		return NULL;

	req->req.dma = DMA_DONT_USE;
	INIT_LIST_HEAD(&req->queue);

	if (ep->dma) {
		/* ep0 in requests are allocated from data pool here */
		dma_desc = dma_pool_alloc(ep->dev->data_requests, gfp,
						&req->td_phys);
		if (!dma_desc) {
			kfree(req);
			return NULL;
		}

		VDBG(ep->dev, "udc_alloc_req: req = %p dma_desc = %p, "
				"td_phys = %lx\n",
				req, dma_desc,
				(unsigned long)req->td_phys);
		/* prevent from using desc. - set HOST BUSY */
		dma_desc->status = AMD_ADDBITS(dma_desc->status,
						UDC_DMA_STP_STS_BS_HOST_BUSY,
						UDC_DMA_STP_STS_BS);
		dma_desc->bufptr = cpu_to_le32(DMA_DONT_USE);
		req->td_data = dma_desc;
		req->td_data_last = NULL;
		req->chain_len = 1;
	}

	return &req->req;
}

/* frees pci pool descriptors of a DMA chain */
static void udc_free_dma_chain(struct udc *dev, struct udc_request *req)
{
	struct udc_data_dma *td = req->td_data;
	unsigned int i;

	dma_addr_t addr_next = 0x00;
	dma_addr_t addr = (dma_addr_t)td->next;

	DBG(dev, "free chain req = %p\n", req);

	/* do not free first desc., will be done by free for request */
	for (i = 1; i < req->chain_len; i++) {
		td = phys_to_virt(addr);
		addr_next = (dma_addr_t)td->next;
		dma_pool_free(dev->data_requests, td, addr);
		addr = addr_next;
	}
}

/* Frees request packet, called by gadget driver */
static void
udc_free_request(struct usb_ep *usbep, struct usb_request *usbreq)
{
	struct udc_ep	*ep;
	struct udc_request	*req;

	if (!usbep || !usbreq)
		return;

	ep = container_of(usbep, struct udc_ep, ep);
	req = container_of(usbreq, struct udc_request, req);
	VDBG(ep->dev, "free_req req=%p\n", req);
	BUG_ON(!list_empty(&req->queue));
	if (req->td_data) {
		VDBG(ep->dev, "req->td_data=%p\n", req->td_data);

		/* free dma chain if created */
		if (req->chain_len > 1)
			udc_free_dma_chain(ep->dev, req);

		dma_pool_free(ep->dev->data_requests, req->td_data,
							req->td_phys);
	}
	kfree(req);
}

/* Init BNA dummy descriptor for HOST BUSY and pointing to itself */
static void udc_init_bna_dummy(struct udc_request *req)
{
	if (req) {
		/* set last bit */
		req->td_data->status |= AMD_BIT(UDC_DMA_IN_STS_L);
		/* set next pointer to itself */
		req->td_data->next = req->td_phys;
		/* set HOST BUSY */
		req->td_data->status
			= AMD_ADDBITS(req->td_data->status,
					UDC_DMA_STP_STS_BS_DMA_DONE,
					UDC_DMA_STP_STS_BS);
#ifdef UDC_VERBOSE
		pr_debug("bna desc = %p, sts = %08x\n",
			req->td_data, req->td_data->status);
#endif
	}
}

/* Allocate BNA dummy descriptor */
static struct udc_request *udc_alloc_bna_dummy(struct udc_ep *ep)
{
	struct udc_request *req = NULL;
	struct usb_request *_req = NULL;

	/* alloc the dummy request */
	_req = udc_alloc_request(&ep->ep, GFP_ATOMIC);
	if (_req) {
		req = container_of(_req, struct udc_request, req);
		ep->bna_dummy_req = req;
		udc_init_bna_dummy(req);
	}
	return req;
}

/* Write data to TX fifo for IN packets */
static void
udc_txfifo_write(struct udc_ep *ep, struct usb_request *req)
{
	u8			*req_buf;
	u32			*buf;
	int			i, j;
	unsigned		bytes = 0;
	unsigned		remaining = 0;

	if (!req || !ep)
		return;

	req_buf = req->buf + req->actual;
	prefetch(req_buf);
	remaining = req->length - req->actual;

	buf = (u32 *) req_buf;

	bytes = ep->ep.maxpacket;
	if (bytes > remaining)
		bytes = remaining;

	/* dwords first */
	for (i = 0; i < bytes / UDC_DWORD_BYTES; i++)
		writel(*(buf + i), ep->txfifo);

	/* remaining bytes must be written by byte access */
	for (j = 0; j < bytes % UDC_DWORD_BYTES; j++) {
		writeb((u8)(*(buf + i) >> (j << UDC_BITS_PER_BYTE_SHIFT)),
							ep->txfifo);
	}

	/* dummy write confirm */
	writel(0, &ep->regs->confirm);
}

/* Read dwords from RX fifo for OUT transfers */
static int udc_rxfifo_read_dwords(struct udc *dev, u32 *buf, int dwords)
{
	int i;

	VDBG(dev, "udc_read_dwords(): %d dwords\n", dwords);

	for (i = 0; i < dwords; i++)
		*(buf + i) = readl(dev->rxfifo);
	return 0;
}

/* Read bytes from RX fifo for OUT transfers */
static int udc_rxfifo_read_bytes(struct udc *dev, u8 *buf, int bytes)
{
	int i, j;
	u32 tmp;

	VDBG(dev, "udc_read_bytes(): %d bytes\n", bytes);

	/* dwords first */
	for (i = 0; i < bytes / UDC_DWORD_BYTES; i++)
		*((u32 *)(buf + (i<<2))) = readl(dev->rxfifo);

	/* remaining bytes must be read by byte access */
	if (bytes % UDC_DWORD_BYTES) {
		tmp = readl(dev->rxfifo);
		for (j = 0; j < bytes % UDC_DWORD_BYTES; j++) {
			*(buf + (i<<2) + j) = (u8)(tmp & UDC_BYTE_MASK);
			tmp = tmp >> UDC_BITS_PER_BYTE;
		}
	}

	return 0;
}

/* Read data from RX fifo for OUT transfers */
static int
udc_rxfifo_read(struct udc_ep *ep, struct udc_request *req)
{
	u8 *buf;
	unsigned buf_space;
	unsigned bytes = 0;
	unsigned finished = 0;

	/* received number bytes */
	bytes = readl(&ep->regs->sts);
	bytes = AMD_GETBITS(bytes, UDC_EPSTS_RX_PKT_SIZE);

	buf_space = req->req.length - req->req.actual;
	buf = req->req.buf + req->req.actual;
	if (bytes > buf_space) {
		if ((buf_space % ep->ep.maxpacket) != 0) {
			DBG(ep->dev,
				"%s: rx %d bytes, rx-buf space = %d bytesn\n",
				ep->ep.name, bytes, buf_space);
			req->req.status = -EOVERFLOW;
		}
		bytes = buf_space;
	}
	req->req.actual += bytes;

	/* last packet ? */
	if (((bytes % ep->ep.maxpacket) != 0) || (!bytes)
		|| ((req->req.actual == req->req.length) && !req->req.zero))
		finished = 1;

	/* read rx fifo bytes */
	VDBG(ep->dev, "ep %s: rxfifo read %d bytes\n", ep->ep.name, bytes);
	udc_rxfifo_read_bytes(ep->dev, buf, bytes);

	return finished;
}

/* Creates or re-inits a DMA chain */
static int udc_create_dma_chain(
	struct udc_ep *ep,
	struct udc_request *req,
	unsigned long buf_len, gfp_t gfp_flags
)
{
	unsigned long bytes = req->req.length;
	unsigned int i;
	dma_addr_t dma_addr;
	struct udc_data_dma	*td = NULL;
	struct udc_data_dma	*last = NULL;
	unsigned long txbytes;
	unsigned create_new_chain = 0;
	unsigned len;

	VDBG(ep->dev, "udc_create_dma_chain: bytes=%ld buf_len=%ld\n",
	     bytes, buf_len);
	dma_addr = DMA_DONT_USE;

	/* unset L bit in first desc for OUT */
	if (!ep->in)
		req->td_data->status &= AMD_CLEAR_BIT(UDC_DMA_IN_STS_L);

	/* alloc only new desc's if not already available */
	len = req->req.length / ep->ep.maxpacket;
	if (req->req.length % ep->ep.maxpacket)
		len++;

	if (len > req->chain_len) {
		/* shorter chain already allocated before */
		if (req->chain_len > 1)
			udc_free_dma_chain(ep->dev, req);
		req->chain_len = len;
		create_new_chain = 1;
	}

	td = req->td_data;
	/* gen. required number of descriptors and buffers */
	for (i = buf_len; i < bytes; i += buf_len) {
		/* create or determine next desc. */
		if (create_new_chain) {
			td = dma_pool_alloc(ep->dev->data_requests,
					    gfp_flags, &dma_addr);
			if (!td)
				return -ENOMEM;

			td->status = 0;
		} else if (i == buf_len) {
			/* first td */
			td = (struct udc_data_dma *)phys_to_virt(
						req->td_data->next);
			td->status = 0;
		} else {
			td = (struct udc_data_dma *)phys_to_virt(last->next);
			td->status = 0;
		}

		if (td)
			td->bufptr = req->req.dma + i; /* assign buffer */
		else
			break;

		/* short packet ? */
		if ((bytes - i) >= buf_len) {
			txbytes = buf_len;
		} else {
			/* short packet */
			txbytes = bytes - i;
		}

		/* link td and assign tx bytes */
		if (i == buf_len) {
			if (create_new_chain)
				req->td_data->next = dma_addr;
			/*
			 * else
			 *	req->td_data->next = virt_to_phys(td);
			 */
			/* write tx bytes */
			if (ep->in) {
				/* first desc */
				req->td_data->status =
					AMD_ADDBITS(req->td_data->status,
						    ep->ep.maxpacket,
						    UDC_DMA_IN_STS_TXBYTES);
				/* second desc */
				td->status = AMD_ADDBITS(td->status,
							txbytes,
							UDC_DMA_IN_STS_TXBYTES);
			}
		} else {
			if (create_new_chain)
				last->next = dma_addr;
			/*
			 * else
			 *	last->next = virt_to_phys(td);
			 */
			if (ep->in) {
				/* write tx bytes */
				td->status = AMD_ADDBITS(td->status,
							txbytes,
							UDC_DMA_IN_STS_TXBYTES);
			}
		}
		last = td;
	}
	/* set last bit */
	if (td) {
		td->status |= AMD_BIT(UDC_DMA_IN_STS_L);
		/* last desc. points to itself */
		req->td_data_last = td;
	}

	return 0;
}

/* create/re-init a DMA descriptor or a DMA descriptor chain */
static int prep_dma(struct udc_ep *ep, struct udc_request *req, gfp_t gfp)
{
	int	retval = 0;
	u32	tmp;

	VDBG(ep->dev, "prep_dma\n");
	VDBG(ep->dev, "prep_dma ep%d req->td_data=%p\n",
			ep->num, req->td_data);

	/* set buffer pointer */
	req->td_data->bufptr = req->req.dma;

	/* set last bit */
	req->td_data->status |= AMD_BIT(UDC_DMA_IN_STS_L);

	/* build/re-init dma chain if maxpkt scatter mode, not for EP0 */
	if (use_dma_ppb) {

		retval = udc_create_dma_chain(ep, req, ep->ep.maxpacket, gfp);
		if (retval != 0) {
			if (retval == -ENOMEM)
				DBG(ep->dev, "Out of DMA memory\n");
			return retval;
		}
		if (ep->in) {
			if (req->req.length == ep->ep.maxpacket) {
				/* write tx bytes */
				req->td_data->status =
					AMD_ADDBITS(req->td_data->status,
						ep->ep.maxpacket,
						UDC_DMA_IN_STS_TXBYTES);

			}
		}

	}

	if (ep->in) {
		VDBG(ep->dev, "IN: use_dma_ppb=%d req->req.len=%d "
				"maxpacket=%d ep%d\n",
				use_dma_ppb, req->req.length,
				ep->ep.maxpacket, ep->num);
		/*
		 * if bytes < max packet then tx bytes must
		 * be written in packet per buffer mode
		 */
		if (!use_dma_ppb || req->req.length < ep->ep.maxpacket
				|| ep->num == UDC_EP0OUT_IX
				|| ep->num == UDC_EP0IN_IX) {
			/* write tx bytes */
			req->td_data->status =
				AMD_ADDBITS(req->td_data->status,
						req->req.length,
						UDC_DMA_IN_STS_TXBYTES);
			/* reset frame num */
			req->td_data->status =
				AMD_ADDBITS(req->td_data->status,
						0,
						UDC_DMA_IN_STS_FRAMENUM);
		}
		/* set HOST BUSY */
		req->td_data->status =
			AMD_ADDBITS(req->td_data->status,
				UDC_DMA_STP_STS_BS_HOST_BUSY,
				UDC_DMA_STP_STS_BS);
	} else {
		VDBG(ep->dev, "OUT set host ready\n");
		/* set HOST READY */
		req->td_data->status =
			AMD_ADDBITS(req->td_data->status,
				UDC_DMA_STP_STS_BS_HOST_READY,
				UDC_DMA_STP_STS_BS);


			/* clear NAK by writing CNAK */
			if (ep->naking) {
				tmp = readl(&ep->regs->ctl);
				tmp |= AMD_BIT(UDC_EPCTL_CNAK);
				writel(tmp, &ep->regs->ctl);
				ep->naking = 0;
				UDC_QUEUE_CNAK(ep, ep->num);
			}

	}

	return retval;
}

/* Completes request packet ... caller MUST hold lock */
static void
complete_req(struct udc_ep *ep, struct udc_request *req, int sts)
__releases(ep->dev->lock)
__acquires(ep->dev->lock)
{
	struct udc		*dev;
	unsigned		halted;

	VDBG(ep->dev, "complete_req(): ep%d\n", ep->num);

	dev = ep->dev;
	/* unmap DMA */
	if (ep->dma)
		usb_gadget_unmap_request(&dev->gadget, &req->req, ep->in);

	halted = ep->halted;
	ep->halted = 1;

	/* set new status if pending */
	if (req->req.status == -EINPROGRESS)
		req->req.status = sts;

	/* remove from ep queue */
	list_del_init(&req->queue);

	VDBG(ep->dev, "req %p => complete %d bytes at %s with sts %d\n",
		&req->req, req->req.length, ep->ep.name, sts);

	spin_unlock(&dev->lock);
	usb_gadget_giveback_request(&ep->ep, &req->req);
	spin_lock(&dev->lock);
	ep->halted = halted;
}

/* Iterates to the end of a DMA chain and returns last descriptor */
static struct udc_data_dma *udc_get_last_dma_desc(struct udc_request *req)
{
	struct udc_data_dma	*td;

	td = req->td_data;
	while (td && !(td->status & AMD_BIT(UDC_DMA_IN_STS_L)))
		td = phys_to_virt(td->next);

	return td;

}

/* Iterates to the end of a DMA chain and counts bytes received */
static u32 udc_get_ppbdu_rxbytes(struct udc_request *req)
{
	struct udc_data_dma	*td;
	u32 count;

	td = req->td_data;
	/* received number bytes */
	count = AMD_GETBITS(td->status, UDC_DMA_OUT_STS_RXBYTES);

	while (td && !(td->status & AMD_BIT(UDC_DMA_IN_STS_L))) {
		td = phys_to_virt(td->next);
		/* received number bytes */
		if (td) {
			count += AMD_GETBITS(td->status,
				UDC_DMA_OUT_STS_RXBYTES);
		}
	}

	return count;

}

/* Enabling RX DMA */
static void udc_set_rde(struct udc *dev)
{
	u32 tmp;

	VDBG(dev, "udc_set_rde()\n");
	/* stop RDE timer */
	if (timer_pending(&udc_timer)) {
		set_rde = 0;
		mod_timer(&udc_timer, jiffies - 1);
	}
	/* set RDE */
	tmp = readl(&dev->regs->ctl);
	tmp |= AMD_BIT(UDC_DEVCTL_RDE);
	writel(tmp, &dev->regs->ctl);
}

/* Queues a request packet, called by gadget driver */
static int
udc_queue(struct usb_ep *usbep, struct usb_request *usbreq, gfp_t gfp)
{
	int			retval = 0;
	u8			open_rxfifo = 0;
	unsigned long		iflags;
	struct udc_ep		*ep;
	struct udc_request	*req;
	struct udc		*dev;
	u32			tmp;

	/* check the inputs */
	req = container_of(usbreq, struct udc_request, req);

	if (!usbep || !usbreq || !usbreq->complete || !usbreq->buf
			|| !list_empty(&req->queue))
		return -EINVAL;

	ep = container_of(usbep, struct udc_ep, ep);
	if (!ep->ep.desc && (ep->num != 0 && ep->num != UDC_EP0OUT_IX))
		return -EINVAL;

	VDBG(ep->dev, "udc_queue(): ep%d-in=%d\n", ep->num, ep->in);
	dev = ep->dev;

	if (!dev->driver || dev->gadget.speed == USB_SPEED_UNKNOWN)
		return -ESHUTDOWN;

	/* map dma (usually done before) */
	if (ep->dma) {
		VDBG(dev, "DMA map req %p\n", req);
		retval = usb_gadget_map_request(&udc->gadget, usbreq, ep->in);
		if (retval)
			return retval;
	}

	VDBG(dev, "%s queue req %p, len %d req->td_data=%p buf %p\n",
			usbep->name, usbreq, usbreq->length,
			req->td_data, usbreq->buf);

	spin_lock_irqsave(&dev->lock, iflags);
	usbreq->actual = 0;
	usbreq->status = -EINPROGRESS;
	req->dma_done = 0;

	/* on empty queue just do first transfer */
	if (list_empty(&ep->queue)) {
		/* zlp */
		if (usbreq->length == 0) {
			/* IN zlp's are handled by hardware */
			complete_req(ep, req, 0);
			VDBG(dev, "%s: zlp\n", ep->ep.name);
			/*
			 * if set_config or set_intf is waiting for ack by zlp
			 * then set CSR_DONE
			 */
			if (dev->set_cfg_not_acked) {
				tmp = readl(&dev->regs->ctl);
				tmp |= AMD_BIT(UDC_DEVCTL_CSR_DONE);
				writel(tmp, &dev->regs->ctl);
				dev->set_cfg_not_acked = 0;
			}
			/* setup command is ACK'ed now by zlp */
			if (dev->waiting_zlp_ack_ep0in) {
				/* clear NAK by writing CNAK in EP0_IN */
				tmp = readl(&dev->ep[UDC_EP0IN_IX].regs->ctl);
				tmp |= AMD_BIT(UDC_EPCTL_CNAK);
				writel(tmp, &dev->ep[UDC_EP0IN_IX].regs->ctl);
				dev->ep[UDC_EP0IN_IX].naking = 0;
				UDC_QUEUE_CNAK(&dev->ep[UDC_EP0IN_IX],
							UDC_EP0IN_IX);
				dev->waiting_zlp_ack_ep0in = 0;
			}
			goto finished;
		}
		if (ep->dma) {
			retval = prep_dma(ep, req, GFP_ATOMIC);
			if (retval != 0)
				goto finished;
			/* write desc pointer to enable DMA */
			if (ep->in) {
				/* set HOST READY */
				req->td_data->status =
					AMD_ADDBITS(req->td_data->status,
						UDC_DMA_IN_STS_BS_HOST_READY,
						UDC_DMA_IN_STS_BS);
			}

			/* disabled rx dma while descriptor update */
			if (!ep->in) {
				/* stop RDE timer */
				if (timer_pending(&udc_timer)) {
					set_rde = 0;
					mod_timer(&udc_timer, jiffies - 1);
				}
				/* clear RDE */
				tmp = readl(&dev->regs->ctl);
				tmp &= AMD_UNMASK_BIT(UDC_DEVCTL_RDE);
				writel(tmp, &dev->regs->ctl);
				open_rxfifo = 1;

				/*
				 * if BNA occurred then let BNA dummy desc.
				 * point to current desc.
				 */
				if (ep->bna_occurred) {
					VDBG(dev, "copy to BNA dummy desc.\n");
					memcpy(ep->bna_dummy_req->td_data,
						req->td_data,
						sizeof(struct udc_data_dma));
				}
			}
			/* write desc pointer */
			writel(req->td_phys, &ep->regs->desptr);

			/* clear NAK by writing CNAK */
			if (ep->naking) {
				tmp = readl(&ep->regs->ctl);
				tmp |= AMD_BIT(UDC_EPCTL_CNAK);
				writel(tmp, &ep->regs->ctl);
				ep->naking = 0;
				UDC_QUEUE_CNAK(ep, ep->num);
			}

			if (ep->in) {
				/* enable ep irq */
				tmp = readl(&dev->regs->ep_irqmsk);
				tmp &= AMD_UNMASK_BIT(ep->num);
				writel(tmp, &dev->regs->ep_irqmsk);
			}
		} else if (ep->in) {
				/* enable ep irq */
				tmp = readl(&dev->regs->ep_irqmsk);
				tmp &= AMD_UNMASK_BIT(ep->num);
				writel(tmp, &dev->regs->ep_irqmsk);
			}

	} else if (ep->dma) {

		/*
		 * prep_dma not used for OUT ep's, this is not possible
		 * for PPB modes, because of chain creation reasons
		 */
		if (ep->in) {
			retval = prep_dma(ep, req, GFP_ATOMIC);
			if (retval != 0)
				goto finished;
		}
	}
	VDBG(dev, "list_add\n");
	/* add request to ep queue */
	if (req) {

		list_add_tail(&req->queue, &ep->queue);

		/* open rxfifo if out data queued */
		if (open_rxfifo) {
			/* enable DMA */
			req->dma_going = 1;
			udc_set_rde(dev);
			if (ep->num != UDC_EP0OUT_IX)
				dev->data_ep_queued = 1;
		}
		/* stop OUT naking */
		if (!ep->in) {
			if (!use_dma && udc_rxfifo_pending) {
				DBG(dev, "udc_queue(): pending bytes in "
					"rxfifo after nyet\n");
				/*
				 * read pending bytes afer nyet:
				 * referring to isr
				 */
				if (udc_rxfifo_read(ep, req)) {
					/* finish */
					complete_req(ep, req, 0);
				}
				udc_rxfifo_pending = 0;

			}
		}
	}

finished:
	spin_unlock_irqrestore(&dev->lock, iflags);
	return retval;
}

/* Empty request queue of an endpoint; caller holds spinlock */
void empty_req_queue(struct udc_ep *ep)
{
	struct udc_request	*req;

	ep->halted = 1;
	while (!list_empty(&ep->queue)) {
		req = list_entry(ep->queue.next,
			struct udc_request,
			queue);
		complete_req(ep, req, -ESHUTDOWN);
	}
}
EXPORT_SYMBOL_GPL(empty_req_queue);

/* Dequeues a request packet, called by gadget driver */
static int udc_dequeue(struct usb_ep *usbep, struct usb_request *usbreq)
{
	struct udc_ep		*ep;
	struct udc_request	*req;
	unsigned		halted;
	unsigned long		iflags;

	ep = container_of(usbep, struct udc_ep, ep);
	if (!usbep || !usbreq || (!ep->ep.desc && (ep->num != 0
				&& ep->num != UDC_EP0OUT_IX)))
		return -EINVAL;

	req = container_of(usbreq, struct udc_request, req);

	spin_lock_irqsave(&ep->dev->lock, iflags);
	halted = ep->halted;
	ep->halted = 1;
	/* request in processing or next one */
	if (ep->queue.next == &req->queue) {
		if (ep->dma && req->dma_going) {
			if (ep->in)
				ep->cancel_transfer = 1;
			else {
				u32 tmp;
				u32 dma_sts;
				/* stop potential receive DMA */
				tmp = readl(&udc->regs->ctl);
				writel(tmp & AMD_UNMASK_BIT(UDC_DEVCTL_RDE),
							&udc->regs->ctl);
				/*
				 * Cancel transfer later in ISR
				 * if descriptor was touched.
				 */
				dma_sts = AMD_GETBITS(req->td_data->status,
							UDC_DMA_OUT_STS_BS);
				if (dma_sts != UDC_DMA_OUT_STS_BS_HOST_READY)
					ep->cancel_transfer = 1;
				else {
					udc_init_bna_dummy(ep->req);
					writel(ep->bna_dummy_req->td_phys,
						&ep->regs->desptr);
				}
				writel(tmp, &udc->regs->ctl);
			}
		}
	}
	complete_req(ep, req, -ECONNRESET);
	ep->halted = halted;

	spin_unlock_irqrestore(&ep->dev->lock, iflags);
	return 0;
}

/* Halt or clear halt of endpoint */
static int
udc_set_halt(struct usb_ep *usbep, int halt)
{
	struct udc_ep	*ep;
	u32 tmp;
	unsigned long iflags;
	int retval = 0;

	if (!usbep)
		return -EINVAL;

	pr_debug("set_halt %s: halt=%d\n", usbep->name, halt);

	ep = container_of(usbep, struct udc_ep, ep);
	if (!ep->ep.desc && (ep->num != 0 && ep->num != UDC_EP0OUT_IX))
		return -EINVAL;
	if (!ep->dev->driver || ep->dev->gadget.speed == USB_SPEED_UNKNOWN)
		return -ESHUTDOWN;

	spin_lock_irqsave(&udc_stall_spinlock, iflags);
	/* halt or clear halt */
	if (halt) {
		if (ep->num == 0)
			ep->dev->stall_ep0in = 1;
		else {
			/*
			 * set STALL
			 * rxfifo empty not taken into acount
			 */
			tmp = readl(&ep->regs->ctl);
			tmp |= AMD_BIT(UDC_EPCTL_S);
			writel(tmp, &ep->regs->ctl);
			ep->halted = 1;

			/* setup poll timer */
			if (!timer_pending(&udc_pollstall_timer)) {
				udc_pollstall_timer.expires = jiffies +
					HZ * UDC_POLLSTALL_TIMER_USECONDS
					/ (1000 * 1000);
				if (!stop_pollstall_timer) {
					DBG(ep->dev, "start polltimer\n");
					add_timer(&udc_pollstall_timer);
				}
			}
		}
	} else {
		/* ep is halted by set_halt() before */
		if (ep->halted) {
			tmp = readl(&ep->regs->ctl);
			/* clear stall bit */
			tmp = tmp & AMD_CLEAR_BIT(UDC_EPCTL_S);
			/* clear NAK by writing CNAK */
			tmp |= AMD_BIT(UDC_EPCTL_CNAK);
			writel(tmp, &ep->regs->ctl);
			ep->halted = 0;
			UDC_QUEUE_CNAK(ep, ep->num);
		}
	}
	spin_unlock_irqrestore(&udc_stall_spinlock, iflags);
	return retval;
}

/* gadget interface */
static const struct usb_ep_ops udc_ep_ops = {
	.enable		= udc_ep_enable,
	.disable	= udc_ep_disable,

	.alloc_request	= udc_alloc_request,
	.free_request	= udc_free_request,

	.queue		= udc_queue,
	.dequeue	= udc_dequeue,

	.set_halt	= udc_set_halt,
	/* fifo ops not implemented */
};

/*-------------------------------------------------------------------------*/

/* Get frame counter (not implemented) */
static int udc_get_frame(struct usb_gadget *gadget)
{
	return -EOPNOTSUPP;
}

/* Initiates a remote wakeup */
static int udc_remote_wakeup(struct udc *dev)
{
	unsigned long flags;
	u32 tmp;

	DBG(dev, "UDC initiates remote wakeup\n");

	spin_lock_irqsave(&dev->lock, flags);

	tmp = readl(&dev->regs->ctl);
	tmp |= AMD_BIT(UDC_DEVCTL_RES);
	writel(tmp, &dev->regs->ctl);
	tmp &= AMD_CLEAR_BIT(UDC_DEVCTL_RES);
	writel(tmp, &dev->regs->ctl);

	spin_unlock_irqrestore(&dev->lock, flags);
	return 0;
}

/* Remote wakeup gadget interface */
static int udc_wakeup(struct usb_gadget *gadget)
{
	struct udc		*dev;

	if (!gadget)
		return -EINVAL;
	dev = container_of(gadget, struct udc, gadget);
	udc_remote_wakeup(dev);

	return 0;
}

static int amd5536_udc_start(struct usb_gadget *g,
		struct usb_gadget_driver *driver);
static int amd5536_udc_stop(struct usb_gadget *g);

static const struct usb_gadget_ops udc_ops = {
	.wakeup		= udc_wakeup,
	.get_frame	= udc_get_frame,
	.udc_start	= amd5536_udc_start,
	.udc_stop	= amd5536_udc_stop,
};

/* Setups endpoint parameters, adds endpoints to linked list */
static void make_ep_lists(struct udc *dev)
{
	/* make gadget ep lists */
	INIT_LIST_HEAD(&dev->gadget.ep_list);
	list_add_tail(&dev->ep[UDC_EPIN_STATUS_IX].ep.ep_list,
						&dev->gadget.ep_list);
	list_add_tail(&dev->ep[UDC_EPIN_IX].ep.ep_list,
						&dev->gadget.ep_list);
	list_add_tail(&dev->ep[UDC_EPOUT_IX].ep.ep_list,
						&dev->gadget.ep_list);

	/* fifo config */
	dev->ep[UDC_EPIN_STATUS_IX].fifo_depth = UDC_EPIN_SMALLINT_BUFF_SIZE;
	if (dev->gadget.speed == USB_SPEED_FULL)
		dev->ep[UDC_EPIN_IX].fifo_depth = UDC_FS_EPIN_BUFF_SIZE;
	else if (dev->gadget.speed == USB_SPEED_HIGH)
		dev->ep[UDC_EPIN_IX].fifo_depth = hs_tx_buf;
	dev->ep[UDC_EPOUT_IX].fifo_depth = UDC_RXFIFO_SIZE;
}

/* Inits UDC context */
void udc_basic_init(struct udc *dev)
{
	u32	tmp;

	DBG(dev, "udc_basic_init()\n");

	dev->gadget.speed = USB_SPEED_UNKNOWN;

	/* stop RDE timer */
	if (timer_pending(&udc_timer)) {
		set_rde = 0;
		mod_timer(&udc_timer, jiffies - 1);
	}
	/* stop poll stall timer */
	if (timer_pending(&udc_pollstall_timer))
		mod_timer(&udc_pollstall_timer, jiffies - 1);
	/* disable DMA */
	tmp = readl(&dev->regs->ctl);
	tmp &= AMD_UNMASK_BIT(UDC_DEVCTL_RDE);
	tmp &= AMD_UNMASK_BIT(UDC_DEVCTL_TDE);
	writel(tmp, &dev->regs->ctl);

	/* enable dynamic CSR programming */
	tmp = readl(&dev->regs->cfg);
	tmp |= AMD_BIT(UDC_DEVCFG_CSR_PRG);
	/* set self powered */
	tmp |= AMD_BIT(UDC_DEVCFG_SP);
	/* set remote wakeupable */
	tmp |= AMD_BIT(UDC_DEVCFG_RWKP);
	writel(tmp, &dev->regs->cfg);

	make_ep_lists(dev);

	dev->data_ep_enabled = 0;
	dev->data_ep_queued = 0;
}
EXPORT_SYMBOL_GPL(udc_basic_init);

/* init registers at driver load time */
static int startup_registers(struct udc *dev)
{
	u32 tmp;

	/* init controller by soft reset */
	udc_soft_reset(dev);

	/* mask not needed interrupts */
	udc_mask_unused_interrupts(dev);

	/* put into initial config */
	udc_basic_init(dev);
	/* link up all endpoints */
	udc_setup_endpoints(dev);

	/* program speed */
	tmp = readl(&dev->regs->cfg);
	if (use_fullspeed)
		tmp = AMD_ADDBITS(tmp, UDC_DEVCFG_SPD_FS, UDC_DEVCFG_SPD);
	else
		tmp = AMD_ADDBITS(tmp, UDC_DEVCFG_SPD_HS, UDC_DEVCFG_SPD);
	writel(tmp, &dev->regs->cfg);

	return 0;
}

/* Sets initial endpoint parameters */
static void udc_setup_endpoints(struct udc *dev)
{
	struct udc_ep	*ep;
	u32	tmp;
	u32	reg;

	DBG(dev, "udc_setup_endpoints()\n");

	/* read enum speed */
	tmp = readl(&dev->regs->sts);
	tmp = AMD_GETBITS(tmp, UDC_DEVSTS_ENUM_SPEED);
	if (tmp == UDC_DEVSTS_ENUM_SPEED_HIGH)
		dev->gadget.speed = USB_SPEED_HIGH;
	else if (tmp == UDC_DEVSTS_ENUM_SPEED_FULL)
		dev->gadget.speed = USB_SPEED_FULL;

	/* set basic ep parameters */
	for (tmp = 0; tmp < UDC_EP_NUM; tmp++) {
		ep = &dev->ep[tmp];
		ep->dev = dev;
		ep->ep.name = ep_info[tmp].name;
		ep->ep.caps = ep_info[tmp].caps;
		ep->num = tmp;
		/* txfifo size is calculated at enable time */
		ep->txfifo = dev->txfifo;

		/* fifo size */
		if (tmp < UDC_EPIN_NUM) {
			ep->fifo_depth = UDC_TXFIFO_SIZE;
			ep->in = 1;
		} else {
			ep->fifo_depth = UDC_RXFIFO_SIZE;
			ep->in = 0;

		}
		ep->regs = &dev->ep_regs[tmp];
		/*
		 * ep will be reset only if ep was not enabled before to avoid
		 * disabling ep interrupts when ENUM interrupt occurs but ep is
		 * not enabled by gadget driver
		 */
		if (!ep->ep.desc)
			ep_init(dev->regs, ep);

		if (use_dma) {
			/*
			 * ep->dma is not really used, just to indicate that
			 * DMA is active: remove this
			 * dma regs = dev control regs
			 */
			ep->dma = &dev->regs->ctl;

			/* nak OUT endpoints until enable - not for ep0 */
			if (tmp != UDC_EP0IN_IX && tmp != UDC_EP0OUT_IX
						&& tmp > UDC_EPIN_NUM) {
				/* set NAK */
				reg = readl(&dev->ep[tmp].regs->ctl);
				reg |= AMD_BIT(UDC_EPCTL_SNAK);
				writel(reg, &dev->ep[tmp].regs->ctl);
				dev->ep[tmp].naking = 1;

			}
		}
	}
	/* EP0 max packet */
	if (dev->gadget.speed == USB_SPEED_FULL) {
		usb_ep_set_maxpacket_limit(&dev->ep[UDC_EP0IN_IX].ep,
					   UDC_FS_EP0IN_MAX_PKT_SIZE);
		usb_ep_set_maxpacket_limit(&dev->ep[UDC_EP0OUT_IX].ep,
					   UDC_FS_EP0OUT_MAX_PKT_SIZE);
	} else if (dev->gadget.speed == USB_SPEED_HIGH) {
		usb_ep_set_maxpacket_limit(&dev->ep[UDC_EP0IN_IX].ep,
					   UDC_EP0IN_MAX_PKT_SIZE);
		usb_ep_set_maxpacket_limit(&dev->ep[UDC_EP0OUT_IX].ep,
					   UDC_EP0OUT_MAX_PKT_SIZE);
	}

	/*
	 * with suspend bug workaround, ep0 params for gadget driver
	 * are set at gadget driver bind() call
	 */
	dev->gadget.ep0 = &dev->ep[UDC_EP0IN_IX].ep;
	dev->ep[UDC_EP0IN_IX].halted = 0;
	INIT_LIST_HEAD(&dev->gadget.ep0->ep_list);

	/* init cfg/alt/int */
	dev->cur_config = 0;
	dev->cur_intf = 0;
	dev->cur_alt = 0;
}

/* Bringup after Connect event, initial bringup to be ready for ep0 events */
static void usb_connect(struct udc *dev)
{
	/* Return if already connected */
	if (dev->connected)
		return;

	dev_info(dev->dev, "USB Connect\n");

	dev->connected = 1;

	/* put into initial config */
	udc_basic_init(dev);

	/* enable device setup interrupts */
	udc_enable_dev_setup_interrupts(dev);
}

/*
 * Calls gadget with disconnect event and resets the UDC and makes
 * initial bringup to be ready for ep0 events
 */
static void usb_disconnect(struct udc *dev)
{
	/* Return if already disconnected */
	if (!dev->connected)
		return;

	dev_info(dev->dev, "USB Disconnect\n");

	dev->connected = 0;

	/* mask interrupts */
	udc_mask_unused_interrupts(dev);

	/* REVISIT there doesn't seem to be a point to having this
	 * talk to a tasklet ... do it directly, we already hold
	 * the spinlock needed to process the disconnect.
	 */

	tasklet_schedule(&disconnect_tasklet);
}

/* Tasklet for disconnect to be outside of interrupt context */
static void udc_tasklet_disconnect(unsigned long par)
{
	struct udc *dev = (struct udc *)(*((struct udc **) par));
	u32 tmp;

	DBG(dev, "Tasklet disconnect\n");
	spin_lock_irq(&dev->lock);

	if (dev->driver) {
		spin_unlock(&dev->lock);
		dev->driver->disconnect(&dev->gadget);
		spin_lock(&dev->lock);

		/* empty queues */
		for (tmp = 0; tmp < UDC_EP_NUM; tmp++)
			empty_req_queue(&dev->ep[tmp]);

	}

	/* disable ep0 */
	ep_init(dev->regs,
			&dev->ep[UDC_EP0IN_IX]);


	if (!soft_reset_occured) {
		/* init controller by soft reset */
		udc_soft_reset(dev);
		soft_reset_occured++;
	}

	/* re-enable dev interrupts */
	udc_enable_dev_setup_interrupts(dev);
	/* back to full speed ? */
	if (use_fullspeed) {
		tmp = readl(&dev->regs->cfg);
		tmp = AMD_ADDBITS(tmp, UDC_DEVCFG_SPD_FS, UDC_DEVCFG_SPD);
		writel(tmp, &dev->regs->cfg);
	}

	spin_unlock_irq(&dev->lock);
}

/* Reset the UDC core */
static void udc_soft_reset(struct udc *dev)
{
	unsigned long	flags;

	DBG(dev, "Soft reset\n");
	/*
	 * reset possible waiting interrupts, because int.
	 * status is lost after soft reset,
	 * ep int. status reset
	 */
	writel(UDC_EPINT_MSK_DISABLE_ALL, &dev->regs->ep_irqsts);
	/* device int. status reset */
	writel(UDC_DEV_MSK_DISABLE, &dev->regs->irqsts);

	/* Don't do this for Broadcom UDC since this is a reserved
	 * bit.
	 */
	if (dev->chiprev != UDC_BCM_REV) {
		spin_lock_irqsave(&udc_irq_spinlock, flags);
		writel(AMD_BIT(UDC_DEVCFG_SOFTRESET), &dev->regs->cfg);
		readl(&dev->regs->cfg);
		spin_unlock_irqrestore(&udc_irq_spinlock, flags);
	}
}

/* RDE timer callback to set RDE bit */
static void udc_timer_function(struct timer_list *unused)
{
	u32 tmp;

	spin_lock_irq(&udc_irq_spinlock);

	if (set_rde > 0) {
		/*
		 * open the fifo if fifo was filled on last timer call
		 * conditionally
		 */
		if (set_rde > 1) {
			/* set RDE to receive setup data */
			tmp = readl(&udc->regs->ctl);
			tmp |= AMD_BIT(UDC_DEVCTL_RDE);
			writel(tmp, &udc->regs->ctl);
			set_rde = -1;
		} else if (readl(&udc->regs->sts)
				& AMD_BIT(UDC_DEVSTS_RXFIFO_EMPTY)) {
			/*
			 * if fifo empty setup polling, do not just
			 * open the fifo
			 */
			udc_timer.expires = jiffies + HZ/UDC_RDE_TIMER_DIV;
			if (!stop_timer)
				add_timer(&udc_timer);
		} else {
			/*
			 * fifo contains data now, setup timer for opening
			 * the fifo when timer expires to be able to receive
			 * setup packets, when data packets gets queued by
			 * gadget layer then timer will forced to expire with
			 * set_rde=0 (RDE is set in udc_queue())
			 */
			set_rde++;
			/* debug: lhadmot_timer_start = 221070 */
			udc_timer.expires = jiffies + HZ*UDC_RDE_TIMER_SECONDS;
			if (!stop_timer)
				add_timer(&udc_timer);
		}

	} else
		set_rde = -1; /* RDE was set by udc_queue() */
	spin_unlock_irq(&udc_irq_spinlock);
	if (stop_timer)
		complete(&on_exit);

}

/* Handle halt state, used in stall poll timer */
static void udc_handle_halt_state(struct udc_ep *ep)
{
	u32 tmp;
	/* set stall as long not halted */
	if (ep->halted == 1) {
		tmp = readl(&ep->regs->ctl);
		/* STALL cleared ? */
		if (!(tmp & AMD_BIT(UDC_EPCTL_S))) {
			/*
			 * FIXME: MSC spec requires that stall remains
			 * even on receivng of CLEAR_FEATURE HALT. So
			 * we would set STALL again here to be compliant.
			 * But with current mass storage drivers this does
			 * not work (would produce endless host retries).
			 * So we clear halt on CLEAR_FEATURE.
			 *
			DBG(ep->dev, "ep %d: set STALL again\n", ep->num);
			tmp |= AMD_BIT(UDC_EPCTL_S);
			writel(tmp, &ep->regs->ctl);*/

			/* clear NAK by writing CNAK */
			tmp |= AMD_BIT(UDC_EPCTL_CNAK);
			writel(tmp, &ep->regs->ctl);
			ep->halted = 0;
			UDC_QUEUE_CNAK(ep, ep->num);
		}
	}
}

/* Stall timer callback to poll S bit and set it again after */
static void udc_pollstall_timer_function(struct timer_list *unused)
{
	struct udc_ep *ep;
	int halted = 0;

	spin_lock_irq(&udc_stall_spinlock);
	/*
	 * only one IN and OUT endpoints are handled
	 * IN poll stall
	 */
	ep = &udc->ep[UDC_EPIN_IX];
	udc_handle_halt_state(ep);
	if (ep->halted)
		halted = 1;
	/* OUT poll stall */
	ep = &udc->ep[UDC_EPOUT_IX];
	udc_handle_halt_state(ep);
	if (ep->halted)
		halted = 1;

	/* setup timer again when still halted */
	if (!stop_pollstall_timer && halted) {
		udc_pollstall_timer.expires = jiffies +
					HZ * UDC_POLLSTALL_TIMER_USECONDS
					/ (1000 * 1000);
		add_timer(&udc_pollstall_timer);
	}
	spin_unlock_irq(&udc_stall_spinlock);

	if (stop_pollstall_timer)
		complete(&on_pollstall_exit);
}

/* Inits endpoint 0 so that SETUP packets are processed */
static void activate_control_endpoints(struct udc *dev)
{
	u32 tmp;

	DBG(dev, "activate_control_endpoints\n");

	/* flush fifo */
	tmp = readl(&dev->ep[UDC_EP0IN_IX].regs->ctl);
	tmp |= AMD_BIT(UDC_EPCTL_F);
	writel(tmp, &dev->ep[UDC_EP0IN_IX].regs->ctl);

	/* set ep0 directions */
	dev->ep[UDC_EP0IN_IX].in = 1;
	dev->ep[UDC_EP0OUT_IX].in = 0;

	/* set buffer size (tx fifo entries) of EP0_IN */
	tmp = readl(&dev->ep[UDC_EP0IN_IX].regs->bufin_framenum);
	if (dev->gadget.speed == USB_SPEED_FULL)
		tmp = AMD_ADDBITS(tmp, UDC_FS_EPIN0_BUFF_SIZE,
					UDC_EPIN_BUFF_SIZE);
	else if (dev->gadget.speed == USB_SPEED_HIGH)
		tmp = AMD_ADDBITS(tmp, UDC_EPIN0_BUFF_SIZE,
					UDC_EPIN_BUFF_SIZE);
	writel(tmp, &dev->ep[UDC_EP0IN_IX].regs->bufin_framenum);

	/* set max packet size of EP0_IN */
	tmp = readl(&dev->ep[UDC_EP0IN_IX].regs->bufout_maxpkt);
	if (dev->gadget.speed == USB_SPEED_FULL)
		tmp = AMD_ADDBITS(tmp, UDC_FS_EP0IN_MAX_PKT_SIZE,
					UDC_EP_MAX_PKT_SIZE);
	else if (dev->gadget.speed == USB_SPEED_HIGH)
		tmp = AMD_ADDBITS(tmp, UDC_EP0IN_MAX_PKT_SIZE,
				UDC_EP_MAX_PKT_SIZE);
	writel(tmp, &dev->ep[UDC_EP0IN_IX].regs->bufout_maxpkt);

	/* set max packet size of EP0_OUT */
	tmp = readl(&dev->ep[UDC_EP0OUT_IX].regs->bufout_maxpkt);
	if (dev->gadget.speed == USB_SPEED_FULL)
		tmp = AMD_ADDBITS(tmp, UDC_FS_EP0OUT_MAX_PKT_SIZE,
					UDC_EP_MAX_PKT_SIZE);
	else if (dev->gadget.speed == USB_SPEED_HIGH)
		tmp = AMD_ADDBITS(tmp, UDC_EP0OUT_MAX_PKT_SIZE,
					UDC_EP_MAX_PKT_SIZE);
	writel(tmp, &dev->ep[UDC_EP0OUT_IX].regs->bufout_maxpkt);

	/* set max packet size of EP0 in UDC CSR */
	tmp = readl(&dev->csr->ne[0]);
	if (dev->gadget.speed == USB_SPEED_FULL)
		tmp = AMD_ADDBITS(tmp, UDC_FS_EP0OUT_MAX_PKT_SIZE,
					UDC_CSR_NE_MAX_PKT);
	else if (dev->gadget.speed == USB_SPEED_HIGH)
		tmp = AMD_ADDBITS(tmp, UDC_EP0OUT_MAX_PKT_SIZE,
					UDC_CSR_NE_MAX_PKT);
	writel(tmp, &dev->csr->ne[0]);

	if (use_dma) {
		dev->ep[UDC_EP0OUT_IX].td->status |=
			AMD_BIT(UDC_DMA_OUT_STS_L);
		/* write dma desc address */
		writel(dev->ep[UDC_EP0OUT_IX].td_stp_dma,
			&dev->ep[UDC_EP0OUT_IX].regs->subptr);
		writel(dev->ep[UDC_EP0OUT_IX].td_phys,
			&dev->ep[UDC_EP0OUT_IX].regs->desptr);
		/* stop RDE timer */
		if (timer_pending(&udc_timer)) {
			set_rde = 0;
			mod_timer(&udc_timer, jiffies - 1);
		}
		/* stop pollstall timer */
		if (timer_pending(&udc_pollstall_timer))
			mod_timer(&udc_pollstall_timer, jiffies - 1);
		/* enable DMA */
		tmp = readl(&dev->regs->ctl);
		tmp |= AMD_BIT(UDC_DEVCTL_MODE)
				| AMD_BIT(UDC_DEVCTL_RDE)
				| AMD_BIT(UDC_DEVCTL_TDE);
		if (use_dma_bufferfill_mode)
			tmp |= AMD_BIT(UDC_DEVCTL_BF);
		else if (use_dma_ppb_du)
			tmp |= AMD_BIT(UDC_DEVCTL_DU);
		writel(tmp, &dev->regs->ctl);
	}

	/* clear NAK by writing CNAK for EP0IN */
	tmp = readl(&dev->ep[UDC_EP0IN_IX].regs->ctl);
	tmp |= AMD_BIT(UDC_EPCTL_CNAK);
	writel(tmp, &dev->ep[UDC_EP0IN_IX].regs->ctl);
	dev->ep[UDC_EP0IN_IX].naking = 0;
	UDC_QUEUE_CNAK(&dev->ep[UDC_EP0IN_IX], UDC_EP0IN_IX);

	/* clear NAK by writing CNAK for EP0OUT */
	tmp = readl(&dev->ep[UDC_EP0OUT_IX].regs->ctl);
	tmp |= AMD_BIT(UDC_EPCTL_CNAK);
	writel(tmp, &dev->ep[UDC_EP0OUT_IX].regs->ctl);
	dev->ep[UDC_EP0OUT_IX].naking = 0;
	UDC_QUEUE_CNAK(&dev->ep[UDC_EP0OUT_IX], UDC_EP0OUT_IX);
}

/* Make endpoint 0 ready for control traffic */
static int setup_ep0(struct udc *dev)
{
	activate_control_endpoints(dev);
	/* enable ep0 interrupts */
	udc_enable_ep0_interrupts(dev);
	/* enable device setup interrupts */
	udc_enable_dev_setup_interrupts(dev);

	return 0;
}

/* Called by gadget driver to register itself */
static int amd5536_udc_start(struct usb_gadget *g,
		struct usb_gadget_driver *driver)
{
	struct udc *dev = to_amd5536_udc(g);
	u32 tmp;

	driver->driver.bus = NULL;
	dev->driver = driver;

	/* Some gadget drivers use both ep0 directions.
	 * NOTE: to gadget driver, ep0 is just one endpoint...
	 */
	dev->ep[UDC_EP0OUT_IX].ep.driver_data =
		dev->ep[UDC_EP0IN_IX].ep.driver_data;

	/* get ready for ep0 traffic */
	setup_ep0(dev);

	/* clear SD */
	tmp = readl(&dev->regs->ctl);
	tmp = tmp & AMD_CLEAR_BIT(UDC_DEVCTL_SD);
	writel(tmp, &dev->regs->ctl);

	usb_connect(dev);

	return 0;
}

/* shutdown requests and disconnect from gadget */
static void
shutdown(struct udc *dev, struct usb_gadget_driver *driver)
__releases(dev->lock)
__acquires(dev->lock)
{
	int tmp;

	/* empty queues and init hardware */
	udc_basic_init(dev);

	for (tmp = 0; tmp < UDC_EP_NUM; tmp++)
		empty_req_queue(&dev->ep[tmp]);

	udc_setup_endpoints(dev);
}

/* Called by gadget driver to unregister itself */
static int amd5536_udc_stop(struct usb_gadget *g)
{
	struct udc *dev = to_amd5536_udc(g);
	unsigned long flags;
	u32 tmp;

	spin_lock_irqsave(&dev->lock, flags);
	udc_mask_unused_interrupts(dev);
	shutdown(dev, NULL);
	spin_unlock_irqrestore(&dev->lock, flags);

	dev->driver = NULL;

	/* set SD */
	tmp = readl(&dev->regs->ctl);
	tmp |= AMD_BIT(UDC_DEVCTL_SD);
	writel(tmp, &dev->regs->ctl);

	return 0;
}

/* Clear pending NAK bits */
static void udc_process_cnak_queue(struct udc *dev)
{
	u32 tmp;
	u32 reg;

	/* check epin's */
	DBG(dev, "CNAK pending queue processing\n");
	for (tmp = 0; tmp < UDC_EPIN_NUM_USED; tmp++) {
		if (cnak_pending & (1 << tmp)) {
			DBG(dev, "CNAK pending for ep%d\n", tmp);
			/* clear NAK by writing CNAK */
			reg = readl(&dev->ep[tmp].regs->ctl);
			reg |= AMD_BIT(UDC_EPCTL_CNAK);
			writel(reg, &dev->ep[tmp].regs->ctl);
			dev->ep[tmp].naking = 0;
			UDC_QUEUE_CNAK(&dev->ep[tmp], dev->ep[tmp].num);
		}
	}
	/* ...	and ep0out */
	if (cnak_pending & (1 << UDC_EP0OUT_IX)) {
		DBG(dev, "CNAK pending for ep%d\n", UDC_EP0OUT_IX);
		/* clear NAK by writing CNAK */
		reg = readl(&dev->ep[UDC_EP0OUT_IX].regs->ctl);
		reg |= AMD_BIT(UDC_EPCTL_CNAK);
		writel(reg, &dev->ep[UDC_EP0OUT_IX].regs->ctl);
		dev->ep[UDC_EP0OUT_IX].naking = 0;
		UDC_QUEUE_CNAK(&dev->ep[UDC_EP0OUT_IX],
				dev->ep[UDC_EP0OUT_IX].num);
	}
}

/* Enabling RX DMA after setup packet */
static void udc_ep0_set_rde(struct udc *dev)
{
	if (use_dma) {
		/*
		 * only enable RXDMA when no data endpoint enabled
		 * or data is queued
		 */
		if (!dev->data_ep_enabled || dev->data_ep_queued) {
			udc_set_rde(dev);
		} else {
			/*
			 * setup timer for enabling RDE (to not enable
			 * RXFIFO DMA for data endpoints to early)
			 */
			if (set_rde != 0 && !timer_pending(&udc_timer)) {
				udc_timer.expires =
					jiffies + HZ/UDC_RDE_TIMER_DIV;
				set_rde = 1;
				if (!stop_timer)
					add_timer(&udc_timer);
			}
		}
	}
}


/* Interrupt handler for data OUT traffic */
static irqreturn_t udc_data_out_isr(struct udc *dev, int ep_ix)
{
	irqreturn_t		ret_val = IRQ_NONE;
	u32			tmp;
	struct udc_ep		*ep;
	struct udc_request	*req;
	unsigned int		count;
	struct udc_data_dma	*td = NULL;
	unsigned		dma_done;

	VDBG(dev, "ep%d irq\n", ep_ix);
	ep = &dev->ep[ep_ix];

	tmp = readl(&ep->regs->sts);
	if (use_dma) {
		/* BNA event ? */
		if (tmp & AMD_BIT(UDC_EPSTS_BNA)) {
			DBG(dev, "BNA ep%dout occurred - DESPTR = %x\n",
					ep->num, readl(&ep->regs->desptr));
			/* clear BNA */
			writel(tmp | AMD_BIT(UDC_EPSTS_BNA), &ep->regs->sts);
			if (!ep->cancel_transfer)
				ep->bna_occurred = 1;
			else
				ep->cancel_transfer = 0;
			ret_val = IRQ_HANDLED;
			goto finished;
		}
	}
	/* HE event ? */
	if (tmp & AMD_BIT(UDC_EPSTS_HE)) {
		dev_err(dev->dev, "HE ep%dout occurred\n", ep->num);

		/* clear HE */
		writel(tmp | AMD_BIT(UDC_EPSTS_HE), &ep->regs->sts);
		ret_val = IRQ_HANDLED;
		goto finished;
	}

	if (!list_empty(&ep->queue)) {

		/* next request */
		req = list_entry(ep->queue.next,
			struct udc_request, queue);
	} else {
		req = NULL;
		udc_rxfifo_pending = 1;
	}
	VDBG(dev, "req = %p\n", req);
	/* fifo mode */
	if (!use_dma) {

		/* read fifo */
		if (req && udc_rxfifo_read(ep, req)) {
			ret_val = IRQ_HANDLED;

			/* finish */
			complete_req(ep, req, 0);
			/* next request */
			if (!list_empty(&ep->queue) && !ep->halted) {
				req = list_entry(ep->queue.next,
					struct udc_request, queue);
			} else
				req = NULL;
		}

	/* DMA */
	} else if (!ep->cancel_transfer && req) {
		ret_val = IRQ_HANDLED;

		/* check for DMA done */
		if (!use_dma_ppb) {
			dma_done = AMD_GETBITS(req->td_data->status,
						UDC_DMA_OUT_STS_BS);
		/* packet per buffer mode - rx bytes */
		} else {
			/*
			 * if BNA occurred then recover desc. from
			 * BNA dummy desc.
			 */
			if (ep->bna_occurred) {
				VDBG(dev, "Recover desc. from BNA dummy\n");
				memcpy(req->td_data, ep->bna_dummy_req->td_data,
						sizeof(struct udc_data_dma));
				ep->bna_occurred = 0;
				udc_init_bna_dummy(ep->req);
			}
			td = udc_get_last_dma_desc(req);
			dma_done = AMD_GETBITS(td->status, UDC_DMA_OUT_STS_BS);
		}
		if (dma_done == UDC_DMA_OUT_STS_BS_DMA_DONE) {
			/* buffer fill mode - rx bytes */
			if (!use_dma_ppb) {
				/* received number bytes */
				count = AMD_GETBITS(req->td_data->status,
						UDC_DMA_OUT_STS_RXBYTES);
				VDBG(dev, "rx bytes=%u\n", count);
			/* packet per buffer mode - rx bytes */
			} else {
				VDBG(dev, "req->td_data=%p\n", req->td_data);
				VDBG(dev, "last desc = %p\n", td);
				/* received number bytes */
				if (use_dma_ppb_du) {
					/* every desc. counts bytes */
					count = udc_get_ppbdu_rxbytes(req);
				} else {
					/* last desc. counts bytes */
					count = AMD_GETBITS(td->status,
						UDC_DMA_OUT_STS_RXBYTES);
					if (!count && req->req.length
						== UDC_DMA_MAXPACKET) {
						/*
						 * on 64k packets the RXBYTES
						 * field is zero
						 */
						count = UDC_DMA_MAXPACKET;
					}
				}
				VDBG(dev, "last desc rx bytes=%u\n", count);
			}

			tmp = req->req.length - req->req.actual;
			if (count > tmp) {
				if ((tmp % ep->ep.maxpacket) != 0) {
					DBG(dev, "%s: rx %db, space=%db\n",
						ep->ep.name, count, tmp);
					req->req.status = -EOVERFLOW;
				}
				count = tmp;
			}
			req->req.actual += count;
			req->dma_going = 0;
			/* complete request */
			complete_req(ep, req, 0);

			/* next request */
			if (!list_empty(&ep->queue) && !ep->halted) {
				req = list_entry(ep->queue.next,
					struct udc_request,
					queue);
				/*
				 * DMA may be already started by udc_queue()
				 * called by gadget drivers completion
				 * routine. This happens when queue
				 * holds one request only.
				 */
				if (req->dma_going == 0) {
					/* next dma */
					if (prep_dma(ep, req, GFP_ATOMIC) != 0)
						goto finished;
					/* write desc pointer */
					writel(req->td_phys,
						&ep->regs->desptr);
					req->dma_going = 1;
					/* enable DMA */
					udc_set_rde(dev);
				}
			} else {
				/*
				 * implant BNA dummy descriptor to allow
				 * RXFIFO opening by RDE
				 */
				if (ep->bna_dummy_req) {
					/* write desc pointer */
					writel(ep->bna_dummy_req->td_phys,
						&ep->regs->desptr);
					ep->bna_occurred = 0;
				}

				/*
				 * schedule timer for setting RDE if queue
				 * remains empty to allow ep0 packets pass
				 * through
				 */
				if (set_rde != 0
						&& !timer_pending(&udc_timer)) {
					udc_timer.expires =
						jiffies
						+ HZ*UDC_RDE_TIMER_SECONDS;
					set_rde = 1;
					if (!stop_timer)
						add_timer(&udc_timer);
				}
				if (ep->num != UDC_EP0OUT_IX)
					dev->data_ep_queued = 0;
			}

		} else {
			/*
			* RX DMA must be reenabled for each desc in PPBDU mode
			* and must be enabled for PPBNDU mode in case of BNA
			*/
			udc_set_rde(dev);
		}

	} else if (ep->cancel_transfer) {
		ret_val = IRQ_HANDLED;
		ep->cancel_transfer = 0;
	}

	/* check pending CNAKS */
	if (cnak_pending) {
		/* CNAk processing when rxfifo empty only */
		if (readl(&dev->regs->sts) & AMD_BIT(UDC_DEVSTS_RXFIFO_EMPTY))
			udc_process_cnak_queue(dev);
	}

	/* clear OUT bits in ep status */
	writel(UDC_EPSTS_OUT_CLEAR, &ep->regs->sts);
finished:
	return ret_val;
}

/* Interrupt handler for data IN traffic */
static irqreturn_t udc_data_in_isr(struct udc *dev, int ep_ix)
{
	irqreturn_t ret_val = IRQ_NONE;
	u32 tmp;
	u32 epsts;
	struct udc_ep *ep;
	struct udc_request *req;
	struct udc_data_dma *td;
	unsigned len;

	ep = &dev->ep[ep_ix];

	epsts = readl(&ep->regs->sts);
	if (use_dma) {
		/* BNA ? */
		if (epsts & AMD_BIT(UDC_EPSTS_BNA)) {
			dev_err(dev->dev,
				"BNA ep%din occurred - DESPTR = %08lx\n",
				ep->num,
				(unsigned long) readl(&ep->regs->desptr));

			/* clear BNA */
			writel(epsts, &ep->regs->sts);
			ret_val = IRQ_HANDLED;
			goto finished;
		}
	}
	/* HE event ? */
	if (epsts & AMD_BIT(UDC_EPSTS_HE)) {
		dev_err(dev->dev,
			"HE ep%dn occurred - DESPTR = %08lx\n",
			ep->num, (unsigned long) readl(&ep->regs->desptr));

		/* clear HE */
		writel(epsts | AMD_BIT(UDC_EPSTS_HE), &ep->regs->sts);
		ret_val = IRQ_HANDLED;
		goto finished;
	}

	/* DMA completion */
	if (epsts & AMD_BIT(UDC_EPSTS_TDC)) {
		VDBG(dev, "TDC set- completion\n");
		ret_val = IRQ_HANDLED;
		if (!ep->cancel_transfer && !list_empty(&ep->queue)) {
			req = list_entry(ep->queue.next,
					struct udc_request, queue);
			/*
			 * length bytes transferred
			 * check dma done of last desc. in PPBDU mode
			 */
			if (use_dma_ppb_du) {
				td = udc_get_last_dma_desc(req);
				if (td)
					req->req.actual = req->req.length;
			} else {
				/* assume all bytes transferred */
				req->req.actual = req->req.length;
			}

			if (req->req.actual == req->req.length) {
				/* complete req */
				complete_req(ep, req, 0);
				req->dma_going = 0;
				/* further request available ? */
				if (list_empty(&ep->queue)) {
					/* disable interrupt */
					tmp = readl(&dev->regs->ep_irqmsk);
					tmp |= AMD_BIT(ep->num);
					writel(tmp, &dev->regs->ep_irqmsk);
				}
			}
		}
		ep->cancel_transfer = 0;

	}
	/*
	 * status reg has IN bit set and TDC not set (if TDC was handled,
	 * IN must not be handled (UDC defect) ?
	 */
	if ((epsts & AMD_BIT(UDC_EPSTS_IN))
			&& !(epsts & AMD_BIT(UDC_EPSTS_TDC))) {
		ret_val = IRQ_HANDLED;
		if (!list_empty(&ep->queue)) {
			/* next request */
			req = list_entry(ep->queue.next,
					struct udc_request, queue);
			/* FIFO mode */
			if (!use_dma) {
				/* write fifo */
				udc_txfifo_write(ep, &req->req);
				len = req->req.length - req->req.actual;
				if (len > ep->ep.maxpacket)
					len = ep->ep.maxpacket;
				req->req.actual += len;
				if (req->req.actual == req->req.length
					|| (len != ep->ep.maxpacket)) {
					/* complete req */
					complete_req(ep, req, 0);
				}
			/* DMA */
			} else if (req && !req->dma_going) {
				VDBG(dev, "IN DMA : req=%p req->td_data=%p\n",
					req, req->td_data);
				if (req->td_data) {

					req->dma_going = 1;

					/*
					 * unset L bit of first desc.
					 * for chain
					 */
					if (use_dma_ppb && req->req.length >
							ep->ep.maxpacket) {
						req->td_data->status &=
							AMD_CLEAR_BIT(
							UDC_DMA_IN_STS_L);
					}

					/* write desc pointer */
					writel(req->td_phys, &ep->regs->desptr);

					/* set HOST READY */
					req->td_data->status =
						AMD_ADDBITS(
						req->td_data->status,
						UDC_DMA_IN_STS_BS_HOST_READY,
						UDC_DMA_IN_STS_BS);

					/* set poll demand bit */
					tmp = readl(&ep->regs->ctl);
					tmp |= AMD_BIT(UDC_EPCTL_P);
					writel(tmp, &ep->regs->ctl);
				}
			}

		} else if (!use_dma && ep->in) {
			/* disable interrupt */
			tmp = readl(
				&dev->regs->ep_irqmsk);
			tmp |= AMD_BIT(ep->num);
			writel(tmp,
				&dev->regs->ep_irqmsk);
		}
	}
	/* clear status bits */
	writel(epsts, &ep->regs->sts);

finished:
	return ret_val;

}

/* Interrupt handler for Control OUT traffic */
static irqreturn_t udc_control_out_isr(struct udc *dev)
__releases(dev->lock)
__acquires(dev->lock)
{
	irqreturn_t ret_val = IRQ_NONE;
	u32 tmp;
	int setup_supported;
	u32 count;
	int set = 0;
	struct udc_ep	*ep;
	struct udc_ep	*ep_tmp;

	ep = &dev->ep[UDC_EP0OUT_IX];

	/* clear irq */
	writel(AMD_BIT(UDC_EPINT_OUT_EP0), &dev->regs->ep_irqsts);

	tmp = readl(&dev->ep[UDC_EP0OUT_IX].regs->sts);
	/* check BNA and clear if set */
	if (tmp & AMD_BIT(UDC_EPSTS_BNA)) {
		VDBG(dev, "ep0: BNA set\n");
		writel(AMD_BIT(UDC_EPSTS_BNA),
			&dev->ep[UDC_EP0OUT_IX].regs->sts);
		ep->bna_occurred = 1;
		ret_val = IRQ_HANDLED;
		goto finished;
	}

	/* type of data: SETUP or DATA 0 bytes */
	tmp = AMD_GETBITS(tmp, UDC_EPSTS_OUT);
	VDBG(dev, "data_typ = %x\n", tmp);

	/* setup data */
	if (tmp == UDC_EPSTS_OUT_SETUP) {
		ret_val = IRQ_HANDLED;

		ep->dev->stall_ep0in = 0;
		dev->waiting_zlp_ack_ep0in = 0;

		/* set NAK for EP0_IN */
		tmp = readl(&dev->ep[UDC_EP0IN_IX].regs->ctl);
		tmp |= AMD_BIT(UDC_EPCTL_SNAK);
		writel(tmp, &dev->ep[UDC_EP0IN_IX].regs->ctl);
		dev->ep[UDC_EP0IN_IX].naking = 1;
		/* get setup data */
		if (use_dma) {

			/* clear OUT bits in ep status */
			writel(UDC_EPSTS_OUT_CLEAR,
				&dev->ep[UDC_EP0OUT_IX].regs->sts);

			setup_data.data[0] =
				dev->ep[UDC_EP0OUT_IX].td_stp->data12;
			setup_data.data[1] =
				dev->ep[UDC_EP0OUT_IX].td_stp->data34;
			/* set HOST READY */
			dev->ep[UDC_EP0OUT_IX].td_stp->status =
					UDC_DMA_STP_STS_BS_HOST_READY;
		} else {
			/* read fifo */
			udc_rxfifo_read_dwords(dev, setup_data.data, 2);
		}

		/* determine direction of control data */
		if ((setup_data.request.bRequestType & USB_DIR_IN) != 0) {
			dev->gadget.ep0 = &dev->ep[UDC_EP0IN_IX].ep;
			/* enable RDE */
			udc_ep0_set_rde(dev);
			set = 0;
		} else {
			dev->gadget.ep0 = &dev->ep[UDC_EP0OUT_IX].ep;
			/*
			 * implant BNA dummy descriptor to allow RXFIFO opening
			 * by RDE
			 */
			if (ep->bna_dummy_req) {
				/* write desc pointer */
				writel(ep->bna_dummy_req->td_phys,
					&dev->ep[UDC_EP0OUT_IX].regs->desptr);
				ep->bna_occurred = 0;
			}

			set = 1;
			dev->ep[UDC_EP0OUT_IX].naking = 1;
			/*
			 * setup timer for enabling RDE (to not enable
			 * RXFIFO DMA for data to early)
			 */
			set_rde = 1;
			if (!timer_pending(&udc_timer)) {
				udc_timer.expires = jiffies +
							HZ/UDC_RDE_TIMER_DIV;
				if (!stop_timer)
					add_timer(&udc_timer);
			}
		}

		/*
		 * mass storage reset must be processed here because
		 * next packet may be a CLEAR_FEATURE HALT which would not
		 * clear the stall bit when no STALL handshake was received
		 * before (autostall can cause this)
		 */
		if (setup_data.data[0] == UDC_MSCRES_DWORD0
				&& setup_data.data[1] == UDC_MSCRES_DWORD1) {
			DBG(dev, "MSC Reset\n");
			/*
			 * clear stall bits
			 * only one IN and OUT endpoints are handled
			 */
			ep_tmp = &udc->ep[UDC_EPIN_IX];
			udc_set_halt(&ep_tmp->ep, 0);
			ep_tmp = &udc->ep[UDC_EPOUT_IX];
			udc_set_halt(&ep_tmp->ep, 0);
		}

		/* call gadget with setup data received */
		spin_unlock(&dev->lock);
		setup_supported = dev->driver->setup(&dev->gadget,
						&setup_data.request);
		spin_lock(&dev->lock);

		tmp = readl(&dev->ep[UDC_EP0IN_IX].regs->ctl);
		/* ep0 in returns data (not zlp) on IN phase */
		if (setup_supported >= 0 && setup_supported <
				UDC_EP0IN_MAXPACKET) {
			/* clear NAK by writing CNAK in EP0_IN */
			tmp |= AMD_BIT(UDC_EPCTL_CNAK);
			writel(tmp, &dev->ep[UDC_EP0IN_IX].regs->ctl);
			dev->ep[UDC_EP0IN_IX].naking = 0;
			UDC_QUEUE_CNAK(&dev->ep[UDC_EP0IN_IX], UDC_EP0IN_IX);

		/* if unsupported request then stall */
		} else if (setup_supported < 0) {
			tmp |= AMD_BIT(UDC_EPCTL_S);
			writel(tmp, &dev->ep[UDC_EP0IN_IX].regs->ctl);
		} else
			dev->waiting_zlp_ack_ep0in = 1;


		/* clear NAK by writing CNAK in EP0_OUT */
		if (!set) {
			tmp = readl(&dev->ep[UDC_EP0OUT_IX].regs->ctl);
			tmp |= AMD_BIT(UDC_EPCTL_CNAK);
			writel(tmp, &dev->ep[UDC_EP0OUT_IX].regs->ctl);
			dev->ep[UDC_EP0OUT_IX].naking = 0;
			UDC_QUEUE_CNAK(&dev->ep[UDC_EP0OUT_IX], UDC_EP0OUT_IX);
		}

		if (!use_dma) {
			/* clear OUT bits in ep status */
			writel(UDC_EPSTS_OUT_CLEAR,
				&dev->ep[UDC_EP0OUT_IX].regs->sts);
		}

	/* data packet 0 bytes */
	} else if (tmp == UDC_EPSTS_OUT_DATA) {
		/* clear OUT bits in ep status */
		writel(UDC_EPSTS_OUT_CLEAR, &dev->ep[UDC_EP0OUT_IX].regs->sts);

		/* get setup data: only 0 packet */
		if (use_dma) {
			/* no req if 0 packet, just reactivate */
			if (list_empty(&dev->ep[UDC_EP0OUT_IX].queue)) {
				VDBG(dev, "ZLP\n");

				/* set HOST READY */
				dev->ep[UDC_EP0OUT_IX].td->status =
					AMD_ADDBITS(
					dev->ep[UDC_EP0OUT_IX].td->status,
					UDC_DMA_OUT_STS_BS_HOST_READY,
					UDC_DMA_OUT_STS_BS);
				/* enable RDE */
				udc_ep0_set_rde(dev);
				ret_val = IRQ_HANDLED;

			} else {
				/* control write */
				ret_val |= udc_data_out_isr(dev, UDC_EP0OUT_IX);
				/* re-program desc. pointer for possible ZLPs */
				writel(dev->ep[UDC_EP0OUT_IX].td_phys,
					&dev->ep[UDC_EP0OUT_IX].regs->desptr);
				/* enable RDE */
				udc_ep0_set_rde(dev);
			}
		} else {

			/* received number bytes */
			count = readl(&dev->ep[UDC_EP0OUT_IX].regs->sts);
			count = AMD_GETBITS(count, UDC_EPSTS_RX_PKT_SIZE);
			/* out data for fifo mode not working */
			count = 0;

			/* 0 packet or real data ? */
			if (count != 0) {
				ret_val |= udc_data_out_isr(dev, UDC_EP0OUT_IX);
			} else {
				/* dummy read confirm */
				readl(&dev->ep[UDC_EP0OUT_IX].regs->confirm);
				ret_val = IRQ_HANDLED;
			}
		}
	}

	/* check pending CNAKS */
	if (cnak_pending) {
		/* CNAk processing when rxfifo empty only */
		if (readl(&dev->regs->sts) & AMD_BIT(UDC_DEVSTS_RXFIFO_EMPTY))
			udc_process_cnak_queue(dev);
	}

finished:
	return ret_val;
}

/* Interrupt handler for Control IN traffic */
static irqreturn_t udc_control_in_isr(struct udc *dev)
{
	irqreturn_t ret_val = IRQ_NONE;
	u32 tmp;
	struct udc_ep *ep;
	struct udc_request *req;
	unsigned len;

	ep = &dev->ep[UDC_EP0IN_IX];

	/* clear irq */
	writel(AMD_BIT(UDC_EPINT_IN_EP0), &dev->regs->ep_irqsts);

	tmp = readl(&dev->ep[UDC_EP0IN_IX].regs->sts);
	/* DMA completion */
	if (tmp & AMD_BIT(UDC_EPSTS_TDC)) {
		VDBG(dev, "isr: TDC clear\n");
		ret_val = IRQ_HANDLED;

		/* clear TDC bit */
		writel(AMD_BIT(UDC_EPSTS_TDC),
				&dev->ep[UDC_EP0IN_IX].regs->sts);

	/* status reg has IN bit set ? */
	} else if (tmp & AMD_BIT(UDC_EPSTS_IN)) {
		ret_val = IRQ_HANDLED;

		if (ep->dma) {
			/* clear IN bit */
			writel(AMD_BIT(UDC_EPSTS_IN),
				&dev->ep[UDC_EP0IN_IX].regs->sts);
		}
		if (dev->stall_ep0in) {
			DBG(dev, "stall ep0in\n");
			/* halt ep0in */
			tmp = readl(&ep->regs->ctl);
			tmp |= AMD_BIT(UDC_EPCTL_S);
			writel(tmp, &ep->regs->ctl);
		} else {
			if (!list_empty(&ep->queue)) {
				/* next request */
				req = list_entry(ep->queue.next,
						struct udc_request, queue);

				if (ep->dma) {
					/* write desc pointer */
					writel(req->td_phys, &ep->regs->desptr);
					/* set HOST READY */
					req->td_data->status =
						AMD_ADDBITS(
						req->td_data->status,
						UDC_DMA_STP_STS_BS_HOST_READY,
						UDC_DMA_STP_STS_BS);

					/* set poll demand bit */
					tmp =
					readl(&dev->ep[UDC_EP0IN_IX].regs->ctl);
					tmp |= AMD_BIT(UDC_EPCTL_P);
					writel(tmp,
					&dev->ep[UDC_EP0IN_IX].regs->ctl);

					/* all bytes will be transferred */
					req->req.actual = req->req.length;

					/* complete req */
					complete_req(ep, req, 0);

				} else {
					/* write fifo */
					udc_txfifo_write(ep, &req->req);

					/* lengh bytes transferred */
					len = req->req.length - req->req.actual;
					if (len > ep->ep.maxpacket)
						len = ep->ep.maxpacket;

					req->req.actual += len;
					if (req->req.actual == req->req.length
						|| (len != ep->ep.maxpacket)) {
						/* complete req */
						complete_req(ep, req, 0);
					}
				}

			}
		}
		ep->halted = 0;
		dev->stall_ep0in = 0;
		if (!ep->dma) {
			/* clear IN bit */
			writel(AMD_BIT(UDC_EPSTS_IN),
				&dev->ep[UDC_EP0IN_IX].regs->sts);
		}
	}

	return ret_val;
}


/* Interrupt handler for global device events */
static irqreturn_t udc_dev_isr(struct udc *dev, u32 dev_irq)
__releases(dev->lock)
__acquires(dev->lock)
{
	irqreturn_t ret_val = IRQ_NONE;
	u32 tmp;
	u32 cfg;
	struct udc_ep *ep;
	u16 i;
	u8 udc_csr_epix;

	/* SET_CONFIG irq ? */
	if (dev_irq & AMD_BIT(UDC_DEVINT_SC)) {
		ret_val = IRQ_HANDLED;

		/* read config value */
		tmp = readl(&dev->regs->sts);
		cfg = AMD_GETBITS(tmp, UDC_DEVSTS_CFG);
		DBG(dev, "SET_CONFIG interrupt: config=%d\n", cfg);
		dev->cur_config = cfg;
		dev->set_cfg_not_acked = 1;

		/* make usb request for gadget driver */
		memset(&setup_data, 0 , sizeof(union udc_setup_data));
		setup_data.request.bRequest = USB_REQ_SET_CONFIGURATION;
		setup_data.request.wValue = cpu_to_le16(dev->cur_config);

		/* programm the NE registers */
		for (i = 0; i < UDC_EP_NUM; i++) {
			ep = &dev->ep[i];
			if (ep->in) {

				/* ep ix in UDC CSR register space */
				udc_csr_epix = ep->num;


			/* OUT ep */
			} else {
				/* ep ix in UDC CSR register space */
				udc_csr_epix = ep->num - UDC_CSR_EP_OUT_IX_OFS;
			}

			tmp = readl(&dev->csr->ne[udc_csr_epix]);
			/* ep cfg */
			tmp = AMD_ADDBITS(tmp, ep->dev->cur_config,
						UDC_CSR_NE_CFG);
			/* write reg */
			writel(tmp, &dev->csr->ne[udc_csr_epix]);

			/* clear stall bits */
			ep->halted = 0;
			tmp = readl(&ep->regs->ctl);
			tmp = tmp & AMD_CLEAR_BIT(UDC_EPCTL_S);
			writel(tmp, &ep->regs->ctl);
		}
		/* call gadget zero with setup data received */
		spin_unlock(&dev->lock);
		tmp = dev->driver->setup(&dev->gadget, &setup_data.request);
		spin_lock(&dev->lock);

	} /* SET_INTERFACE ? */
	if (dev_irq & AMD_BIT(UDC_DEVINT_SI)) {
		ret_val = IRQ_HANDLED;

		dev->set_cfg_not_acked = 1;
		/* read interface and alt setting values */
		tmp = readl(&dev->regs->sts);
		dev->cur_alt = AMD_GETBITS(tmp, UDC_DEVSTS_ALT);
		dev->cur_intf = AMD_GETBITS(tmp, UDC_DEVSTS_INTF);

		/* make usb request for gadget driver */
		memset(&setup_data, 0 , sizeof(union udc_setup_data));
		setup_data.request.bRequest = USB_REQ_SET_INTERFACE;
		setup_data.request.bRequestType = USB_RECIP_INTERFACE;
		setup_data.request.wValue = cpu_to_le16(dev->cur_alt);
		setup_data.request.wIndex = cpu_to_le16(dev->cur_intf);

		DBG(dev, "SET_INTERFACE interrupt: alt=%d intf=%d\n",
				dev->cur_alt, dev->cur_intf);

		/* programm the NE registers */
		for (i = 0; i < UDC_EP_NUM; i++) {
			ep = &dev->ep[i];
			if (ep->in) {

				/* ep ix in UDC CSR register space */
				udc_csr_epix = ep->num;


			/* OUT ep */
			} else {
				/* ep ix in UDC CSR register space */
				udc_csr_epix = ep->num - UDC_CSR_EP_OUT_IX_OFS;
			}

			/* UDC CSR reg */
			/* set ep values */
			tmp = readl(&dev->csr->ne[udc_csr_epix]);
			/* ep interface */
			tmp = AMD_ADDBITS(tmp, ep->dev->cur_intf,
						UDC_CSR_NE_INTF);
			/* tmp = AMD_ADDBITS(tmp, 2, UDC_CSR_NE_INTF); */
			/* ep alt */
			tmp = AMD_ADDBITS(tmp, ep->dev->cur_alt,
						UDC_CSR_NE_ALT);
			/* write reg */
			writel(tmp, &dev->csr->ne[udc_csr_epix]);

			/* clear stall bits */
			ep->halted = 0;
			tmp = readl(&ep->regs->ctl);
			tmp = tmp & AMD_CLEAR_BIT(UDC_EPCTL_S);
			writel(tmp, &ep->regs->ctl);
		}

		/* call gadget zero with setup data received */
		spin_unlock(&dev->lock);
		tmp = dev->driver->setup(&dev->gadget, &setup_data.request);
		spin_lock(&dev->lock);

	} /* USB reset */
	if (dev_irq & AMD_BIT(UDC_DEVINT_UR)) {
		DBG(dev, "USB Reset interrupt\n");
		ret_val = IRQ_HANDLED;

		/* allow soft reset when suspend occurs */
		soft_reset_occured = 0;

		dev->waiting_zlp_ack_ep0in = 0;
		dev->set_cfg_not_acked = 0;

		/* mask not needed interrupts */
		udc_mask_unused_interrupts(dev);

		/* call gadget to resume and reset configs etc. */
		spin_unlock(&dev->lock);
		if (dev->sys_suspended && dev->driver->resume) {
			dev->driver->resume(&dev->gadget);
			dev->sys_suspended = 0;
		}
		usb_gadget_udc_reset(&dev->gadget, dev->driver);
		spin_lock(&dev->lock);

		/* disable ep0 to empty req queue */
		empty_req_queue(&dev->ep[UDC_EP0IN_IX]);
		ep_init(dev->regs, &dev->ep[UDC_EP0IN_IX]);

		/* soft reset when rxfifo not empty */
		tmp = readl(&dev->regs->sts);
		if (!(tmp & AMD_BIT(UDC_DEVSTS_RXFIFO_EMPTY))
				&& !soft_reset_after_usbreset_occured) {
			udc_soft_reset(dev);
			soft_reset_after_usbreset_occured++;
		}

		/*
		 * DMA reset to kill potential old DMA hw hang,
		 * POLL bit is already reset by ep_init() through
		 * disconnect()
		 */
		DBG(dev, "DMA machine reset\n");
		tmp = readl(&dev->regs->cfg);
		writel(tmp | AMD_BIT(UDC_DEVCFG_DMARST), &dev->regs->cfg);
		writel(tmp, &dev->regs->cfg);

		/* put into initial config */
		udc_basic_init(dev);

		/* enable device setup interrupts */
		udc_enable_dev_setup_interrupts(dev);

		/* enable suspend interrupt */
		tmp = readl(&dev->regs->irqmsk);
		tmp &= AMD_UNMASK_BIT(UDC_DEVINT_US);
		writel(tmp, &dev->regs->irqmsk);

	} /* USB suspend */
	if (dev_irq & AMD_BIT(UDC_DEVINT_US)) {
		DBG(dev, "USB Suspend interrupt\n");
		ret_val = IRQ_HANDLED;
		if (dev->driver->suspend) {
			spin_unlock(&dev->lock);
			dev->sys_suspended = 1;
			dev->driver->suspend(&dev->gadget);
			spin_lock(&dev->lock);
		}
	} /* new speed ? */
	if (dev_irq & AMD_BIT(UDC_DEVINT_ENUM)) {
		DBG(dev, "ENUM interrupt\n");
		ret_val = IRQ_HANDLED;
		soft_reset_after_usbreset_occured = 0;

		/* disable ep0 to empty req queue */
		empty_req_queue(&dev->ep[UDC_EP0IN_IX]);
		ep_init(dev->regs, &dev->ep[UDC_EP0IN_IX]);

		/* link up all endpoints */
		udc_setup_endpoints(dev);
		dev_info(dev->dev, "Connect: %s\n",
			 usb_speed_string(dev->gadget.speed));

		/* init ep 0 */
		activate_control_endpoints(dev);

		/* enable ep0 interrupts */
		udc_enable_ep0_interrupts(dev);
	}
	/* session valid change interrupt */
	if (dev_irq & AMD_BIT(UDC_DEVINT_SVC)) {
		DBG(dev, "USB SVC interrupt\n");
		ret_val = IRQ_HANDLED;

		/* check that session is not valid to detect disconnect */
		tmp = readl(&dev->regs->sts);
		if (!(tmp & AMD_BIT(UDC_DEVSTS_SESSVLD))) {
			/* disable suspend interrupt */
			tmp = readl(&dev->regs->irqmsk);
			tmp |= AMD_BIT(UDC_DEVINT_US);
			writel(tmp, &dev->regs->irqmsk);
			DBG(dev, "USB Disconnect (session valid low)\n");
			/* cleanup on disconnect */
			usb_disconnect(udc);
		}

	}

	return ret_val;
}

/* Interrupt Service Routine, see Linux Kernel Doc for parameters */
irqreturn_t udc_irq(int irq, void *pdev)
{
	struct udc *dev = pdev;
	u32 reg;
	u16 i;
	u32 ep_irq;
	irqreturn_t ret_val = IRQ_NONE;

	spin_lock(&dev->lock);

	/* check for ep irq */
	reg = readl(&dev->regs->ep_irqsts);
	if (reg) {
		if (reg & AMD_BIT(UDC_EPINT_OUT_EP0))
			ret_val |= udc_control_out_isr(dev);
		if (reg & AMD_BIT(UDC_EPINT_IN_EP0))
			ret_val |= udc_control_in_isr(dev);

		/*
		 * data endpoint
		 * iterate ep's
		 */
		for (i = 1; i < UDC_EP_NUM; i++) {
			ep_irq = 1 << i;
			if (!(reg & ep_irq) || i == UDC_EPINT_OUT_EP0)
				continue;

			/* clear irq status */
			writel(ep_irq, &dev->regs->ep_irqsts);

			/* irq for out ep ? */
			if (i > UDC_EPIN_NUM)
				ret_val |= udc_data_out_isr(dev, i);
			else
				ret_val |= udc_data_in_isr(dev, i);
		}

	}


	/* check for dev irq */
	reg = readl(&dev->regs->irqsts);
	if (reg) {
		/* clear irq */
		writel(reg, &dev->regs->irqsts);
		ret_val |= udc_dev_isr(dev, reg);
	}


	spin_unlock(&dev->lock);
	return ret_val;
}
EXPORT_SYMBOL_GPL(udc_irq);

/* Tears down device */
void gadget_release(struct device *pdev)
{
	struct amd5536udc *dev = dev_get_drvdata(pdev);
	kfree(dev);
}
EXPORT_SYMBOL_GPL(gadget_release);

/* Cleanup on device remove */
void udc_remove(struct udc *dev)
{
	/* remove timer */
	stop_timer++;
	if (timer_pending(&udc_timer))
		wait_for_completion(&on_exit);
	del_timer_sync(&udc_timer);
	/* remove pollstall timer */
	stop_pollstall_timer++;
	if (timer_pending(&udc_pollstall_timer))
		wait_for_completion(&on_pollstall_exit);
	del_timer_sync(&udc_pollstall_timer);
	udc = NULL;
}
EXPORT_SYMBOL_GPL(udc_remove);

/* free all the dma pools */
void free_dma_pools(struct udc *dev)
{
	dma_pool_free(dev->stp_requests, dev->ep[UDC_EP0OUT_IX].td,
		      dev->ep[UDC_EP0OUT_IX].td_phys);
	dma_pool_free(dev->stp_requests, dev->ep[UDC_EP0OUT_IX].td_stp,
		      dev->ep[UDC_EP0OUT_IX].td_stp_dma);
	dma_pool_destroy(dev->stp_requests);
	dma_pool_destroy(dev->data_requests);
}
EXPORT_SYMBOL_GPL(free_dma_pools);

/* create dma pools on init */
int init_dma_pools(struct udc *dev)
{
	struct udc_stp_dma	*td_stp;
	struct udc_data_dma	*td_data;
	int retval;

	/* consistent DMA mode setting ? */
	if (use_dma_ppb) {
		use_dma_bufferfill_mode = 0;
	} else {
		use_dma_ppb_du = 0;
		use_dma_bufferfill_mode = 1;
	}

	/* DMA setup */
	dev->data_requests = dma_pool_create("data_requests", dev->dev,
		sizeof(struct udc_data_dma), 0, 0);
	if (!dev->data_requests) {
		DBG(dev, "can't get request data pool\n");
		return -ENOMEM;
	}

	/* EP0 in dma regs = dev control regs */
	dev->ep[UDC_EP0IN_IX].dma = &dev->regs->ctl;

	/* dma desc for setup data */
	dev->stp_requests = dma_pool_create("setup requests", dev->dev,
		sizeof(struct udc_stp_dma), 0, 0);
	if (!dev->stp_requests) {
		DBG(dev, "can't get stp request pool\n");
		retval = -ENOMEM;
		goto err_create_dma_pool;
	}
	/* setup */
	td_stp = dma_pool_alloc(dev->stp_requests, GFP_KERNEL,
				&dev->ep[UDC_EP0OUT_IX].td_stp_dma);
	if (!td_stp) {
		retval = -ENOMEM;
		goto err_alloc_dma;
	}
	dev->ep[UDC_EP0OUT_IX].td_stp = td_stp;

	/* data: 0 packets !? */
	td_data = dma_pool_alloc(dev->stp_requests, GFP_KERNEL,
				&dev->ep[UDC_EP0OUT_IX].td_phys);
	if (!td_data) {
		retval = -ENOMEM;
		goto err_alloc_phys;
	}
	dev->ep[UDC_EP0OUT_IX].td = td_data;
	return 0;

err_alloc_phys:
	dma_pool_free(dev->stp_requests, dev->ep[UDC_EP0OUT_IX].td_stp,
		      dev->ep[UDC_EP0OUT_IX].td_stp_dma);
err_alloc_dma:
	dma_pool_destroy(dev->stp_requests);
	dev->stp_requests = NULL;
err_create_dma_pool:
	dma_pool_destroy(dev->data_requests);
	dev->data_requests = NULL;
	return retval;
}
EXPORT_SYMBOL_GPL(init_dma_pools);

/* general probe */
int udc_probe(struct udc *dev)
{
	char		tmp[128];
	u32		reg;
	int		retval;

	/* device struct setup */
	dev->gadget.ops = &udc_ops;

	dev_set_name(&dev->gadget.dev, "gadget");
	dev->gadget.name = name;
	dev->gadget.max_speed = USB_SPEED_HIGH;

	/* init registers, interrupts, ... */
	startup_registers(dev);

	dev_info(dev->dev, "%s\n", mod_desc);

	snprintf(tmp, sizeof(tmp), "%d", dev->irq);

	/* Print this device info for AMD chips only*/
	if (dev->chiprev == UDC_HSA0_REV ||
	    dev->chiprev == UDC_HSB1_REV) {
		dev_info(dev->dev, "irq %s, pci mem %08lx, chip rev %02x(Geode5536 %s)\n",
			 tmp, dev->phys_addr, dev->chiprev,
			 (dev->chiprev == UDC_HSA0_REV) ?
			 "A0" : "B1");
		strcpy(tmp, UDC_DRIVER_VERSION_STRING);
		if (dev->chiprev == UDC_HSA0_REV) {
			dev_err(dev->dev, "chip revision is A0; too old\n");
			retval = -ENODEV;
			goto finished;
		}
		dev_info(dev->dev,
			 "driver version: %s(for Geode5536 B1)\n", tmp);
	}

	udc = dev;

	retval = usb_add_gadget_udc_release(udc->dev, &dev->gadget,
					    gadget_release);
	if (retval)
		goto finished;

	/* timer init */
	timer_setup(&udc_timer, udc_timer_function, 0);
	timer_setup(&udc_pollstall_timer, udc_pollstall_timer_function, 0);

	/* set SD */
	reg = readl(&dev->regs->ctl);
	reg |= AMD_BIT(UDC_DEVCTL_SD);
	writel(reg, &dev->regs->ctl);

	/* print dev register info */
	print_regs(dev);

	return 0;

finished:
	return retval;
}
EXPORT_SYMBOL_GPL(udc_probe);

MODULE_DESCRIPTION(UDC_MOD_DESCRIPTION);
MODULE_AUTHOR("Thomas Dahlmann");
MODULE_LICENSE("GPL");
