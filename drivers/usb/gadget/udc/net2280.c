// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for the PLX NET2280 USB device controller.
 * Specs and errata are available from <http://www.plxtech.com>.
 *
 * PLX Technology Inc. (formerly NetChip Technology) supported the
 * development of this driver.
 *
 *
 * CODE STATUS HIGHLIGHTS
 *
 * This driver should work well with most "gadget" drivers, including
 * the Mass Storage, Serial, and Ethernet/RNDIS gadget drivers
 * as well as Gadget Zero and Gadgetfs.
 *
 * DMA is enabled by default.
 *
 * MSI is enabled by default.  The legacy IRQ is used if MSI couldn't
 * be enabled.
 *
 * Note that almost all the errata workarounds here are only needed for
 * rev1 chips.  Rev1a silicon (0110) fixes almost all of them.
 */

/*
 * Copyright (C) 2003 David Brownell
 * Copyright (C) 2003-2005 PLX Technology, Inc.
 * Copyright (C) 2014 Ricardo Ribalda - Qtechnology/AS
 *
 * Modified Seth Levy 2005 PLX Technology, Inc. to provide compatibility
 *	with 2282 chip
 *
 * Modified Ricardo Ribalda Qtechnology AS  to provide compatibility
 *	with usb 338x chip. Based on PLX driver
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ioport.h>
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
#include <linux/prefetch.h>
#include <linux/io.h>

#include <asm/byteorder.h>
#include <asm/irq.h>
#include <asm/unaligned.h>

#define	DRIVER_DESC		"PLX NET228x/USB338x USB Peripheral Controller"
#define	DRIVER_VERSION		"2005 Sept 27/v3.0"

#define	EP_DONTUSE		13	/* nonzero */

#define USE_RDK_LEDS		/* GPIO pins control three LEDs */


static const char driver_name[] = "net2280";
static const char driver_desc[] = DRIVER_DESC;

static const u32 ep_bit[9] = { 0, 17, 2, 19, 4, 1, 18, 3, 20 };
static const char ep0name[] = "ep0";

#define EP_INFO(_name, _caps) \
	{ \
		.name = _name, \
		.caps = _caps, \
	}

static const struct {
	const char *name;
	const struct usb_ep_caps caps;
} ep_info_dft[] = { /* Default endpoint configuration */
	EP_INFO(ep0name,
		USB_EP_CAPS(USB_EP_CAPS_TYPE_CONTROL, USB_EP_CAPS_DIR_ALL)),
	EP_INFO("ep-a",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_ALL, USB_EP_CAPS_DIR_ALL)),
	EP_INFO("ep-b",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_ALL, USB_EP_CAPS_DIR_ALL)),
	EP_INFO("ep-c",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_ALL, USB_EP_CAPS_DIR_ALL)),
	EP_INFO("ep-d",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_ALL, USB_EP_CAPS_DIR_ALL)),
	EP_INFO("ep-e",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_ALL, USB_EP_CAPS_DIR_ALL)),
	EP_INFO("ep-f",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_ALL, USB_EP_CAPS_DIR_ALL)),
	EP_INFO("ep-g",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_ALL, USB_EP_CAPS_DIR_ALL)),
	EP_INFO("ep-h",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_ALL, USB_EP_CAPS_DIR_ALL)),
}, ep_info_adv[] = { /* Endpoints for usb3380 advance mode */
	EP_INFO(ep0name,
		USB_EP_CAPS(USB_EP_CAPS_TYPE_CONTROL, USB_EP_CAPS_DIR_ALL)),
	EP_INFO("ep1in",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_ALL, USB_EP_CAPS_DIR_IN)),
	EP_INFO("ep2out",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_ALL, USB_EP_CAPS_DIR_OUT)),
	EP_INFO("ep3in",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_ALL, USB_EP_CAPS_DIR_IN)),
	EP_INFO("ep4out",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_ALL, USB_EP_CAPS_DIR_OUT)),
	EP_INFO("ep1out",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_ALL, USB_EP_CAPS_DIR_OUT)),
	EP_INFO("ep2in",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_ALL, USB_EP_CAPS_DIR_IN)),
	EP_INFO("ep3out",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_ALL, USB_EP_CAPS_DIR_OUT)),
	EP_INFO("ep4in",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_ALL, USB_EP_CAPS_DIR_IN)),
};

#undef EP_INFO

/* mode 0 == ep-{a,b,c,d} 1K fifo each
 * mode 1 == ep-{a,b} 2K fifo each, ep-{c,d} unavailable
 * mode 2 == ep-a 2K fifo, ep-{b,c} 1K each, ep-d unavailable
 */
static ushort fifo_mode;

/* "modprobe net2280 fifo_mode=1" etc */
module_param(fifo_mode, ushort, 0644);

/* enable_suspend -- When enabled, the driver will respond to
 * USB suspend requests by powering down the NET2280.  Otherwise,
 * USB suspend requests will be ignored.  This is acceptable for
 * self-powered devices
 */
static bool enable_suspend;

/* "modprobe net2280 enable_suspend=1" etc */
module_param(enable_suspend, bool, 0444);

#define	DIR_STRING(bAddress) (((bAddress) & USB_DIR_IN) ? "in" : "out")

static char *type_string(u8 bmAttributes)
{
	switch ((bmAttributes) & USB_ENDPOINT_XFERTYPE_MASK) {
	case USB_ENDPOINT_XFER_BULK:	return "bulk";
	case USB_ENDPOINT_XFER_ISOC:	return "iso";
	case USB_ENDPOINT_XFER_INT:	return "intr";
	}
	return "control";
}

#include "net2280.h"

#define valid_bit	cpu_to_le32(BIT(VALID_BIT))
#define dma_done_ie	cpu_to_le32(BIT(DMA_DONE_INTERRUPT_ENABLE))

static void ep_clear_seqnum(struct net2280_ep *ep);
static void stop_activity(struct net2280 *dev,
					struct usb_gadget_driver *driver);
static void ep0_start(struct net2280 *dev);

/*-------------------------------------------------------------------------*/
static inline void enable_pciirqenb(struct net2280_ep *ep)
{
	u32 tmp = readl(&ep->dev->regs->pciirqenb0);

	if (ep->dev->quirks & PLX_LEGACY)
		tmp |= BIT(ep->num);
	else
		tmp |= BIT(ep_bit[ep->num]);
	writel(tmp, &ep->dev->regs->pciirqenb0);

	return;
}

static int
net2280_enable(struct usb_ep *_ep, const struct usb_endpoint_descriptor *desc)
{
	struct net2280		*dev;
	struct net2280_ep	*ep;
	u32			max;
	u32 tmp = 0;
	u32 type;
	unsigned long		flags;
	static const u32 ep_key[9] = { 1, 0, 1, 0, 1, 1, 0, 1, 0 };
	int ret = 0;

	ep = container_of(_ep, struct net2280_ep, ep);
	if (!_ep || !desc || ep->desc || _ep->name == ep0name ||
			desc->bDescriptorType != USB_DT_ENDPOINT) {
		pr_err("%s: failed at line=%d\n", __func__, __LINE__);
		return -EINVAL;
	}
	dev = ep->dev;
	if (!dev->driver || dev->gadget.speed == USB_SPEED_UNKNOWN) {
		ret = -ESHUTDOWN;
		goto print_err;
	}

	/* erratum 0119 workaround ties up an endpoint number */
	if ((desc->bEndpointAddress & 0x0f) == EP_DONTUSE) {
		ret = -EDOM;
		goto print_err;
	}

	if (dev->quirks & PLX_PCIE) {
		if ((desc->bEndpointAddress & 0x0f) >= 0x0c) {
			ret = -EDOM;
			goto print_err;
		}
		ep->is_in = !!usb_endpoint_dir_in(desc);
		if (dev->enhanced_mode && ep->is_in && ep_key[ep->num]) {
			ret = -EINVAL;
			goto print_err;
		}
	}

	/* sanity check ep-e/ep-f since their fifos are small */
	max = usb_endpoint_maxp(desc);
	if (ep->num > 4 && max > 64 && (dev->quirks & PLX_LEGACY)) {
		ret = -ERANGE;
		goto print_err;
	}

	spin_lock_irqsave(&dev->lock, flags);
	_ep->maxpacket = max;
	ep->desc = desc;

	/* ep_reset() has already been called */
	ep->stopped = 0;
	ep->wedged = 0;
	ep->out_overflow = 0;

	/* set speed-dependent max packet; may kick in high bandwidth */
	set_max_speed(ep, max);

	/* set type, direction, address; reset fifo counters */
	writel(BIT(FIFO_FLUSH), &ep->regs->ep_stat);

	if ((dev->quirks & PLX_PCIE) && dev->enhanced_mode) {
		tmp = readl(&ep->cfg->ep_cfg);
		/* If USB ep number doesn't match hardware ep number */
		if ((tmp & 0xf) != usb_endpoint_num(desc)) {
			ret = -EINVAL;
			spin_unlock_irqrestore(&dev->lock, flags);
			goto print_err;
		}
		if (ep->is_in)
			tmp &= ~USB3380_EP_CFG_MASK_IN;
		else
			tmp &= ~USB3380_EP_CFG_MASK_OUT;
	}
	type = (desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK);
	if (type == USB_ENDPOINT_XFER_INT) {
		/* erratum 0105 workaround prevents hs NYET */
		if (dev->chiprev == 0100 &&
				dev->gadget.speed == USB_SPEED_HIGH &&
				!(desc->bEndpointAddress & USB_DIR_IN))
			writel(BIT(CLEAR_NAK_OUT_PACKETS_MODE),
				&ep->regs->ep_rsp);
	} else if (type == USB_ENDPOINT_XFER_BULK) {
		/* catch some particularly blatant driver bugs */
		if ((dev->gadget.speed == USB_SPEED_SUPER && max != 1024) ||
		    (dev->gadget.speed == USB_SPEED_HIGH && max != 512) ||
		    (dev->gadget.speed == USB_SPEED_FULL && max > 64)) {
			spin_unlock_irqrestore(&dev->lock, flags);
			ret = -ERANGE;
			goto print_err;
		}
	}
	ep->is_iso = (type == USB_ENDPOINT_XFER_ISOC);
	/* Enable this endpoint */
	if (dev->quirks & PLX_LEGACY) {
		tmp |= type << ENDPOINT_TYPE;
		tmp |= desc->bEndpointAddress;
		/* default full fifo lines */
		tmp |= (4 << ENDPOINT_BYTE_COUNT);
		tmp |= BIT(ENDPOINT_ENABLE);
		ep->is_in = (tmp & USB_DIR_IN) != 0;
	} else {
		/* In Legacy mode, only OUT endpoints are used */
		if (dev->enhanced_mode && ep->is_in) {
			tmp |= type << IN_ENDPOINT_TYPE;
			tmp |= BIT(IN_ENDPOINT_ENABLE);
		} else {
			tmp |= type << OUT_ENDPOINT_TYPE;
			tmp |= BIT(OUT_ENDPOINT_ENABLE);
			tmp |= (ep->is_in << ENDPOINT_DIRECTION);
		}

		tmp |= (4 << ENDPOINT_BYTE_COUNT);
		if (!dev->enhanced_mode)
			tmp |= usb_endpoint_num(desc);
		tmp |= (ep->ep.maxburst << MAX_BURST_SIZE);
	}

	/* Make sure all the registers are written before ep_rsp*/
	wmb();

	/* for OUT transfers, block the rx fifo until a read is posted */
	if (!ep->is_in)
		writel(BIT(SET_NAK_OUT_PACKETS), &ep->regs->ep_rsp);
	else if (!(dev->quirks & PLX_2280)) {
		/* Added for 2282, Don't use nak packets on an in endpoint,
		 * this was ignored on 2280
		 */
		writel(BIT(CLEAR_NAK_OUT_PACKETS) |
			BIT(CLEAR_NAK_OUT_PACKETS_MODE), &ep->regs->ep_rsp);
	}

	if (dev->quirks & PLX_PCIE)
		ep_clear_seqnum(ep);
	writel(tmp, &ep->cfg->ep_cfg);

	/* enable irqs */
	if (!ep->dma) {				/* pio, per-packet */
		enable_pciirqenb(ep);

		tmp = BIT(DATA_PACKET_RECEIVED_INTERRUPT_ENABLE) |
			BIT(DATA_PACKET_TRANSMITTED_INTERRUPT_ENABLE);
		if (dev->quirks & PLX_2280)
			tmp |= readl(&ep->regs->ep_irqenb);
		writel(tmp, &ep->regs->ep_irqenb);
	} else {				/* dma, per-request */
		tmp = BIT((8 + ep->num));	/* completion */
		tmp |= readl(&dev->regs->pciirqenb1);
		writel(tmp, &dev->regs->pciirqenb1);

		/* for short OUT transfers, dma completions can't
		 * advance the queue; do it pio-style, by hand.
		 * NOTE erratum 0112 workaround #2
		 */
		if ((desc->bEndpointAddress & USB_DIR_IN) == 0) {
			tmp = BIT(SHORT_PACKET_TRANSFERRED_INTERRUPT_ENABLE);
			writel(tmp, &ep->regs->ep_irqenb);

			enable_pciirqenb(ep);
		}
	}

	tmp = desc->bEndpointAddress;
	ep_dbg(dev, "enabled %s (ep%d%s-%s) %s max %04x\n",
		_ep->name, tmp & 0x0f, DIR_STRING(tmp),
		type_string(desc->bmAttributes),
		ep->dma ? "dma" : "pio", max);

	/* pci writes may still be posted */
	spin_unlock_irqrestore(&dev->lock, flags);
	return ret;

print_err:
	dev_err(&ep->dev->pdev->dev, "%s: error=%d\n", __func__, ret);
	return ret;
}

static int handshake(u32 __iomem *ptr, u32 mask, u32 done, int usec)
{
	u32	result;

	do {
		result = readl(ptr);
		if (result == ~(u32)0)		/* "device unplugged" */
			return -ENODEV;
		result &= mask;
		if (result == done)
			return 0;
		udelay(1);
		usec--;
	} while (usec > 0);
	return -ETIMEDOUT;
}

static const struct usb_ep_ops net2280_ep_ops;

static void ep_reset_228x(struct net2280_regs __iomem *regs,
			  struct net2280_ep *ep)
{
	u32		tmp;

	ep->desc = NULL;
	INIT_LIST_HEAD(&ep->queue);

	usb_ep_set_maxpacket_limit(&ep->ep, ~0);
	ep->ep.ops = &net2280_ep_ops;

	/* disable the dma, irqs, endpoint... */
	if (ep->dma) {
		writel(0, &ep->dma->dmactl);
		writel(BIT(DMA_SCATTER_GATHER_DONE_INTERRUPT) |
			BIT(DMA_TRANSACTION_DONE_INTERRUPT) |
			BIT(DMA_ABORT),
			&ep->dma->dmastat);

		tmp = readl(&regs->pciirqenb0);
		tmp &= ~BIT(ep->num);
		writel(tmp, &regs->pciirqenb0);
	} else {
		tmp = readl(&regs->pciirqenb1);
		tmp &= ~BIT((8 + ep->num));	/* completion */
		writel(tmp, &regs->pciirqenb1);
	}
	writel(0, &ep->regs->ep_irqenb);

	/* init to our chosen defaults, notably so that we NAK OUT
	 * packets until the driver queues a read (+note erratum 0112)
	 */
	if (!ep->is_in || (ep->dev->quirks & PLX_2280)) {
		tmp = BIT(SET_NAK_OUT_PACKETS_MODE) |
		BIT(SET_NAK_OUT_PACKETS) |
		BIT(CLEAR_EP_HIDE_STATUS_PHASE) |
		BIT(CLEAR_INTERRUPT_MODE);
	} else {
		/* added for 2282 */
		tmp = BIT(CLEAR_NAK_OUT_PACKETS_MODE) |
		BIT(CLEAR_NAK_OUT_PACKETS) |
		BIT(CLEAR_EP_HIDE_STATUS_PHASE) |
		BIT(CLEAR_INTERRUPT_MODE);
	}

	if (ep->num != 0) {
		tmp |= BIT(CLEAR_ENDPOINT_TOGGLE) |
			BIT(CLEAR_ENDPOINT_HALT);
	}
	writel(tmp, &ep->regs->ep_rsp);

	/* scrub most status bits, and flush any fifo state */
	if (ep->dev->quirks & PLX_2280)
		tmp = BIT(FIFO_OVERFLOW) |
			BIT(FIFO_UNDERFLOW);
	else
		tmp = 0;

	writel(tmp | BIT(TIMEOUT) |
		BIT(USB_STALL_SENT) |
		BIT(USB_IN_NAK_SENT) |
		BIT(USB_IN_ACK_RCVD) |
		BIT(USB_OUT_PING_NAK_SENT) |
		BIT(USB_OUT_ACK_SENT) |
		BIT(FIFO_FLUSH) |
		BIT(SHORT_PACKET_OUT_DONE_INTERRUPT) |
		BIT(SHORT_PACKET_TRANSFERRED_INTERRUPT) |
		BIT(DATA_PACKET_RECEIVED_INTERRUPT) |
		BIT(DATA_PACKET_TRANSMITTED_INTERRUPT) |
		BIT(DATA_OUT_PING_TOKEN_INTERRUPT) |
		BIT(DATA_IN_TOKEN_INTERRUPT),
		&ep->regs->ep_stat);

	/* fifo size is handled separately */
}

static void ep_reset_338x(struct net2280_regs __iomem *regs,
					struct net2280_ep *ep)
{
	u32 tmp, dmastat;

	ep->desc = NULL;
	INIT_LIST_HEAD(&ep->queue);

	usb_ep_set_maxpacket_limit(&ep->ep, ~0);
	ep->ep.ops = &net2280_ep_ops;

	/* disable the dma, irqs, endpoint... */
	if (ep->dma) {
		writel(0, &ep->dma->dmactl);
		writel(BIT(DMA_ABORT_DONE_INTERRUPT) |
		       BIT(DMA_PAUSE_DONE_INTERRUPT) |
		       BIT(DMA_SCATTER_GATHER_DONE_INTERRUPT) |
		       BIT(DMA_TRANSACTION_DONE_INTERRUPT),
		       /* | BIT(DMA_ABORT), */
		       &ep->dma->dmastat);

		dmastat = readl(&ep->dma->dmastat);
		if (dmastat == 0x5002) {
			ep_warn(ep->dev, "The dmastat return = %x!!\n",
			       dmastat);
			writel(0x5a, &ep->dma->dmastat);
		}

		tmp = readl(&regs->pciirqenb0);
		tmp &= ~BIT(ep_bit[ep->num]);
		writel(tmp, &regs->pciirqenb0);
	} else {
		if (ep->num < 5) {
			tmp = readl(&regs->pciirqenb1);
			tmp &= ~BIT((8 + ep->num));	/* completion */
			writel(tmp, &regs->pciirqenb1);
		}
	}
	writel(0, &ep->regs->ep_irqenb);

	writel(BIT(SHORT_PACKET_OUT_DONE_INTERRUPT) |
	       BIT(SHORT_PACKET_TRANSFERRED_INTERRUPT) |
	       BIT(FIFO_OVERFLOW) |
	       BIT(DATA_PACKET_RECEIVED_INTERRUPT) |
	       BIT(DATA_PACKET_TRANSMITTED_INTERRUPT) |
	       BIT(DATA_OUT_PING_TOKEN_INTERRUPT) |
	       BIT(DATA_IN_TOKEN_INTERRUPT), &ep->regs->ep_stat);

	tmp = readl(&ep->cfg->ep_cfg);
	if (ep->is_in)
		tmp &= ~USB3380_EP_CFG_MASK_IN;
	else
		tmp &= ~USB3380_EP_CFG_MASK_OUT;
	writel(tmp, &ep->cfg->ep_cfg);
}

static void nuke(struct net2280_ep *);

static int net2280_disable(struct usb_ep *_ep)
{
	struct net2280_ep	*ep;
	unsigned long		flags;

	ep = container_of(_ep, struct net2280_ep, ep);
	if (!_ep || !ep->desc || _ep->name == ep0name) {
		pr_err("%s: Invalid ep=%p or ep->desc\n", __func__, _ep);
		return -EINVAL;
	}
	spin_lock_irqsave(&ep->dev->lock, flags);
	nuke(ep);

	if (ep->dev->quirks & PLX_PCIE)
		ep_reset_338x(ep->dev->regs, ep);
	else
		ep_reset_228x(ep->dev->regs, ep);

	ep_vdbg(ep->dev, "disabled %s %s\n",
			ep->dma ? "dma" : "pio", _ep->name);

	/* synch memory views with the device */
	(void)readl(&ep->cfg->ep_cfg);

	if (!ep->dma && ep->num >= 1 && ep->num <= 4)
		ep->dma = &ep->dev->dma[ep->num - 1];

	spin_unlock_irqrestore(&ep->dev->lock, flags);
	return 0;
}

/*-------------------------------------------------------------------------*/

static struct usb_request
*net2280_alloc_request(struct usb_ep *_ep, gfp_t gfp_flags)
{
	struct net2280_ep	*ep;
	struct net2280_request	*req;

	if (!_ep) {
		pr_err("%s: Invalid ep\n", __func__);
		return NULL;
	}
	ep = container_of(_ep, struct net2280_ep, ep);

	req = kzalloc(sizeof(*req), gfp_flags);
	if (!req)
		return NULL;

	INIT_LIST_HEAD(&req->queue);

	/* this dma descriptor may be swapped with the previous dummy */
	if (ep->dma) {
		struct net2280_dma	*td;

		td = dma_pool_alloc(ep->dev->requests, gfp_flags,
				&req->td_dma);
		if (!td) {
			kfree(req);
			return NULL;
		}
		td->dmacount = 0;	/* not VALID */
		td->dmadesc = td->dmaaddr;
		req->td = td;
	}
	return &req->req;
}

static void net2280_free_request(struct usb_ep *_ep, struct usb_request *_req)
{
	struct net2280_ep	*ep;
	struct net2280_request	*req;

	ep = container_of(_ep, struct net2280_ep, ep);
	if (!_ep || !_req) {
		dev_err(&ep->dev->pdev->dev, "%s: Invalid ep=%p or req=%p\n",
							__func__, _ep, _req);
		return;
	}

	req = container_of(_req, struct net2280_request, req);
	WARN_ON(!list_empty(&req->queue));
	if (req->td)
		dma_pool_free(ep->dev->requests, req->td, req->td_dma);
	kfree(req);
}

/*-------------------------------------------------------------------------*/

/* load a packet into the fifo we use for usb IN transfers.
 * works for all endpoints.
 *
 * NOTE: pio with ep-a..ep-d could stuff multiple packets into the fifo
 * at a time, but this code is simpler because it knows it only writes
 * one packet.  ep-a..ep-d should use dma instead.
 */
static void write_fifo(struct net2280_ep *ep, struct usb_request *req)
{
	struct net2280_ep_regs	__iomem *regs = ep->regs;
	u8			*buf;
	u32			tmp;
	unsigned		count, total;

	/* INVARIANT:  fifo is currently empty. (testable) */

	if (req) {
		buf = req->buf + req->actual;
		prefetch(buf);
		total = req->length - req->actual;
	} else {
		total = 0;
		buf = NULL;
	}

	/* write just one packet at a time */
	count = ep->ep.maxpacket;
	if (count > total)	/* min() cannot be used on a bitfield */
		count = total;

	ep_vdbg(ep->dev, "write %s fifo (IN) %d bytes%s req %p\n",
			ep->ep.name, count,
			(count != ep->ep.maxpacket) ? " (short)" : "",
			req);
	while (count >= 4) {
		/* NOTE be careful if you try to align these. fifo lines
		 * should normally be full (4 bytes) and successive partial
		 * lines are ok only in certain cases.
		 */
		tmp = get_unaligned((u32 *)buf);
		cpu_to_le32s(&tmp);
		writel(tmp, &regs->ep_data);
		buf += 4;
		count -= 4;
	}

	/* last fifo entry is "short" unless we wrote a full packet.
	 * also explicitly validate last word in (periodic) transfers
	 * when maxpacket is not a multiple of 4 bytes.
	 */
	if (count || total < ep->ep.maxpacket) {
		tmp = count ? get_unaligned((u32 *)buf) : count;
		cpu_to_le32s(&tmp);
		set_fifo_bytecount(ep, count & 0x03);
		writel(tmp, &regs->ep_data);
	}

	/* pci writes may still be posted */
}

/* work around erratum 0106: PCI and USB race over the OUT fifo.
 * caller guarantees chiprev 0100, out endpoint is NAKing, and
 * there's no real data in the fifo.
 *
 * NOTE:  also used in cases where that erratum doesn't apply:
 * where the host wrote "too much" data to us.
 */
static void out_flush(struct net2280_ep *ep)
{
	u32	__iomem *statp;
	u32	tmp;

	statp = &ep->regs->ep_stat;

	tmp = readl(statp);
	if (tmp & BIT(NAK_OUT_PACKETS)) {
		ep_dbg(ep->dev, "%s %s %08x !NAK\n",
			ep->ep.name, __func__, tmp);
		writel(BIT(SET_NAK_OUT_PACKETS), &ep->regs->ep_rsp);
	}

	writel(BIT(DATA_OUT_PING_TOKEN_INTERRUPT) |
		BIT(DATA_PACKET_RECEIVED_INTERRUPT),
		statp);
	writel(BIT(FIFO_FLUSH), statp);
	/* Make sure that stap is written */
	mb();
	tmp = readl(statp);
	if (tmp & BIT(DATA_OUT_PING_TOKEN_INTERRUPT) &&
			/* high speed did bulk NYET; fifo isn't filling */
			ep->dev->gadget.speed == USB_SPEED_FULL) {
		unsigned	usec;

		usec = 50;		/* 64 byte bulk/interrupt */
		handshake(statp, BIT(USB_OUT_PING_NAK_SENT),
				BIT(USB_OUT_PING_NAK_SENT), usec);
		/* NAK done; now CLEAR_NAK_OUT_PACKETS is safe */
	}
}

/* unload packet(s) from the fifo we use for usb OUT transfers.
 * returns true iff the request completed, because of short packet
 * or the request buffer having filled with full packets.
 *
 * for ep-a..ep-d this will read multiple packets out when they
 * have been accepted.
 */
static int read_fifo(struct net2280_ep *ep, struct net2280_request *req)
{
	struct net2280_ep_regs	__iomem *regs = ep->regs;
	u8			*buf = req->req.buf + req->req.actual;
	unsigned		count, tmp, is_short;
	unsigned		cleanup = 0, prevent = 0;

	/* erratum 0106 ... packets coming in during fifo reads might
	 * be incompletely rejected.  not all cases have workarounds.
	 */
	if (ep->dev->chiprev == 0x0100 &&
			ep->dev->gadget.speed == USB_SPEED_FULL) {
		udelay(1);
		tmp = readl(&ep->regs->ep_stat);
		if ((tmp & BIT(NAK_OUT_PACKETS)))
			cleanup = 1;
		else if ((tmp & BIT(FIFO_FULL))) {
			start_out_naking(ep);
			prevent = 1;
		}
		/* else: hope we don't see the problem */
	}

	/* never overflow the rx buffer. the fifo reads packets until
	 * it sees a short one; we might not be ready for them all.
	 */
	prefetchw(buf);
	count = readl(&regs->ep_avail);
	if (unlikely(count == 0)) {
		udelay(1);
		tmp = readl(&ep->regs->ep_stat);
		count = readl(&regs->ep_avail);
		/* handled that data already? */
		if (count == 0 && (tmp & BIT(NAK_OUT_PACKETS)) == 0)
			return 0;
	}

	tmp = req->req.length - req->req.actual;
	if (count > tmp) {
		/* as with DMA, data overflow gets flushed */
		if ((tmp % ep->ep.maxpacket) != 0) {
			ep_err(ep->dev,
				"%s out fifo %d bytes, expected %d\n",
				ep->ep.name, count, tmp);
			req->req.status = -EOVERFLOW;
			cleanup = 1;
			/* NAK_OUT_PACKETS will be set, so flushing is safe;
			 * the next read will start with the next packet
			 */
		} /* else it's a ZLP, no worries */
		count = tmp;
	}
	req->req.actual += count;

	is_short = (count == 0) || ((count % ep->ep.maxpacket) != 0);

	ep_vdbg(ep->dev, "read %s fifo (OUT) %d bytes%s%s%s req %p %d/%d\n",
			ep->ep.name, count, is_short ? " (short)" : "",
			cleanup ? " flush" : "", prevent ? " nak" : "",
			req, req->req.actual, req->req.length);

	while (count >= 4) {
		tmp = readl(&regs->ep_data);
		cpu_to_le32s(&tmp);
		put_unaligned(tmp, (u32 *)buf);
		buf += 4;
		count -= 4;
	}
	if (count) {
		tmp = readl(&regs->ep_data);
		/* LE conversion is implicit here: */
		do {
			*buf++ = (u8) tmp;
			tmp >>= 8;
		} while (--count);
	}
	if (cleanup)
		out_flush(ep);
	if (prevent) {
		writel(BIT(CLEAR_NAK_OUT_PACKETS), &ep->regs->ep_rsp);
		(void) readl(&ep->regs->ep_rsp);
	}

	return is_short || ((req->req.actual == req->req.length) &&
			!req->req.zero);
}

/* fill out dma descriptor to match a given request */
static void fill_dma_desc(struct net2280_ep *ep,
					struct net2280_request *req, int valid)
{
	struct net2280_dma	*td = req->td;
	u32			dmacount = req->req.length;

	/* don't let DMA continue after a short OUT packet,
	 * so overruns can't affect the next transfer.
	 * in case of overruns on max-size packets, we can't
	 * stop the fifo from filling but we can flush it.
	 */
	if (ep->is_in)
		dmacount |= BIT(DMA_DIRECTION);
	if ((!ep->is_in && (dmacount % ep->ep.maxpacket) != 0) ||
					!(ep->dev->quirks & PLX_2280))
		dmacount |= BIT(END_OF_CHAIN);

	req->valid = valid;
	if (valid)
		dmacount |= BIT(VALID_BIT);
	dmacount |= BIT(DMA_DONE_INTERRUPT_ENABLE);

	/* td->dmadesc = previously set by caller */
	td->dmaaddr = cpu_to_le32 (req->req.dma);

	/* 2280 may be polling VALID_BIT through ep->dma->dmadesc */
	wmb();
	td->dmacount = cpu_to_le32(dmacount);
}

static const u32 dmactl_default =
		BIT(DMA_SCATTER_GATHER_DONE_INTERRUPT) |
		BIT(DMA_CLEAR_COUNT_ENABLE) |
		/* erratum 0116 workaround part 1 (use POLLING) */
		(POLL_100_USEC << DESCRIPTOR_POLLING_RATE) |
		BIT(DMA_VALID_BIT_POLLING_ENABLE) |
		BIT(DMA_VALID_BIT_ENABLE) |
		BIT(DMA_SCATTER_GATHER_ENABLE) |
		/* erratum 0116 workaround part 2 (no AUTOSTART) */
		BIT(DMA_ENABLE);

static inline void spin_stop_dma(struct net2280_dma_regs __iomem *dma)
{
	handshake(&dma->dmactl, BIT(DMA_ENABLE), 0, 50);
}

static inline void stop_dma(struct net2280_dma_regs __iomem *dma)
{
	writel(readl(&dma->dmactl) & ~BIT(DMA_ENABLE), &dma->dmactl);
	spin_stop_dma(dma);
}

static void start_queue(struct net2280_ep *ep, u32 dmactl, u32 td_dma)
{
	struct net2280_dma_regs	__iomem *dma = ep->dma;
	unsigned int tmp = BIT(VALID_BIT) | (ep->is_in << DMA_DIRECTION);

	if (!(ep->dev->quirks & PLX_2280))
		tmp |= BIT(END_OF_CHAIN);

	writel(tmp, &dma->dmacount);
	writel(readl(&dma->dmastat), &dma->dmastat);

	writel(td_dma, &dma->dmadesc);
	if (ep->dev->quirks & PLX_PCIE)
		dmactl |= BIT(DMA_REQUEST_OUTSTANDING);
	writel(dmactl, &dma->dmactl);

	/* erratum 0116 workaround part 3:  pci arbiter away from net2280 */
	(void) readl(&ep->dev->pci->pcimstctl);

	writel(BIT(DMA_START), &dma->dmastat);
}

static void start_dma(struct net2280_ep *ep, struct net2280_request *req)
{
	u32			tmp;
	struct net2280_dma_regs	__iomem *dma = ep->dma;

	/* FIXME can't use DMA for ZLPs */

	/* on this path we "know" there's no dma active (yet) */
	WARN_ON(readl(&dma->dmactl) & BIT(DMA_ENABLE));
	writel(0, &ep->dma->dmactl);

	/* previous OUT packet might have been short */
	if (!ep->is_in && (readl(&ep->regs->ep_stat) &
				BIT(NAK_OUT_PACKETS))) {
		writel(BIT(SHORT_PACKET_TRANSFERRED_INTERRUPT),
			&ep->regs->ep_stat);

		tmp = readl(&ep->regs->ep_avail);
		if (tmp) {
			writel(readl(&dma->dmastat), &dma->dmastat);

			/* transfer all/some fifo data */
			writel(req->req.dma, &dma->dmaaddr);
			tmp = min(tmp, req->req.length);

			/* dma irq, faking scatterlist status */
			req->td->dmacount = cpu_to_le32(req->req.length - tmp);
			writel(BIT(DMA_DONE_INTERRUPT_ENABLE) | tmp,
					&dma->dmacount);
			req->td->dmadesc = 0;
			req->valid = 1;

			writel(BIT(DMA_ENABLE), &dma->dmactl);
			writel(BIT(DMA_START), &dma->dmastat);
			return;
		}
		stop_out_naking(ep);
	}

	tmp = dmactl_default;

	/* force packet boundaries between dma requests, but prevent the
	 * controller from automagically writing a last "short" packet
	 * (zero length) unless the driver explicitly said to do that.
	 */
	if (ep->is_in) {
		if (likely((req->req.length % ep->ep.maxpacket) ||
							req->req.zero)){
			tmp |= BIT(DMA_FIFO_VALIDATE);
			ep->in_fifo_validate = 1;
		} else
			ep->in_fifo_validate = 0;
	}

	/* init req->td, pointing to the current dummy */
	req->td->dmadesc = cpu_to_le32 (ep->td_dma);
	fill_dma_desc(ep, req, 1);

	req->td->dmacount |= cpu_to_le32(BIT(END_OF_CHAIN));

	start_queue(ep, tmp, req->td_dma);
}

static inline void
queue_dma(struct net2280_ep *ep, struct net2280_request *req, int valid)
{
	struct net2280_dma	*end;
	dma_addr_t		tmp;

	/* swap new dummy for old, link; fill and maybe activate */
	end = ep->dummy;
	ep->dummy = req->td;
	req->td = end;

	tmp = ep->td_dma;
	ep->td_dma = req->td_dma;
	req->td_dma = tmp;

	end->dmadesc = cpu_to_le32 (ep->td_dma);

	fill_dma_desc(ep, req, valid);
}

static void
done(struct net2280_ep *ep, struct net2280_request *req, int status)
{
	struct net2280		*dev;
	unsigned		stopped = ep->stopped;

	list_del_init(&req->queue);

	if (req->req.status == -EINPROGRESS)
		req->req.status = status;
	else
		status = req->req.status;

	dev = ep->dev;
	if (ep->dma)
		usb_gadget_unmap_request(&dev->gadget, &req->req, ep->is_in);

	if (status && status != -ESHUTDOWN)
		ep_vdbg(dev, "complete %s req %p stat %d len %u/%u\n",
			ep->ep.name, &req->req, status,
			req->req.actual, req->req.length);

	/* don't modify queue heads during completion callback */
	ep->stopped = 1;
	spin_unlock(&dev->lock);
	usb_gadget_giveback_request(&ep->ep, &req->req);
	spin_lock(&dev->lock);
	ep->stopped = stopped;
}

/*-------------------------------------------------------------------------*/

static int
net2280_queue(struct usb_ep *_ep, struct usb_request *_req, gfp_t gfp_flags)
{
	struct net2280_request	*req;
	struct net2280_ep	*ep;
	struct net2280		*dev;
	unsigned long		flags;
	int ret = 0;

	/* we always require a cpu-view buffer, so that we can
	 * always use pio (as fallback or whatever).
	 */
	ep = container_of(_ep, struct net2280_ep, ep);
	if (!_ep || (!ep->desc && ep->num != 0)) {
		pr_err("%s: Invalid ep=%p or ep->desc\n", __func__, _ep);
		return -EINVAL;
	}
	req = container_of(_req, struct net2280_request, req);
	if (!_req || !_req->complete || !_req->buf ||
				!list_empty(&req->queue)) {
		ret = -EINVAL;
		goto print_err;
	}
	if (_req->length > (~0 & DMA_BYTE_COUNT_MASK)) {
		ret = -EDOM;
		goto print_err;
	}
	dev = ep->dev;
	if (!dev->driver || dev->gadget.speed == USB_SPEED_UNKNOWN) {
		ret = -ESHUTDOWN;
		goto print_err;
	}

	/* FIXME implement PIO fallback for ZLPs with DMA */
	if (ep->dma && _req->length == 0) {
		ret = -EOPNOTSUPP;
		goto print_err;
	}

	/* set up dma mapping in case the caller didn't */
	if (ep->dma) {
		ret = usb_gadget_map_request(&dev->gadget, _req,
				ep->is_in);
		if (ret)
			goto print_err;
	}

	ep_vdbg(dev, "%s queue req %p, len %d buf %p\n",
			_ep->name, _req, _req->length, _req->buf);

	spin_lock_irqsave(&dev->lock, flags);

	_req->status = -EINPROGRESS;
	_req->actual = 0;

	/* kickstart this i/o queue? */
	if  (list_empty(&ep->queue) && !ep->stopped &&
		!((dev->quirks & PLX_PCIE) && ep->dma &&
		  (readl(&ep->regs->ep_rsp) & BIT(CLEAR_ENDPOINT_HALT)))) {

		/* use DMA if the endpoint supports it, else pio */
		if (ep->dma)
			start_dma(ep, req);
		else {
			/* maybe there's no control data, just status ack */
			if (ep->num == 0 && _req->length == 0) {
				allow_status(ep);
				done(ep, req, 0);
				ep_vdbg(dev, "%s status ack\n", ep->ep.name);
				goto done;
			}

			/* PIO ... stuff the fifo, or unblock it.  */
			if (ep->is_in)
				write_fifo(ep, _req);
			else if (list_empty(&ep->queue)) {
				u32	s;

				/* OUT FIFO might have packet(s) buffered */
				s = readl(&ep->regs->ep_stat);
				if ((s & BIT(FIFO_EMPTY)) == 0) {
					/* note:  _req->short_not_ok is
					 * ignored here since PIO _always_
					 * stops queue advance here, and
					 * _req->status doesn't change for
					 * short reads (only _req->actual)
					 */
					if (read_fifo(ep, req) &&
							ep->num == 0) {
						done(ep, req, 0);
						allow_status(ep);
						/* don't queue it */
						req = NULL;
					} else if (read_fifo(ep, req) &&
							ep->num != 0) {
						done(ep, req, 0);
						req = NULL;
					} else
						s = readl(&ep->regs->ep_stat);
				}

				/* don't NAK, let the fifo fill */
				if (req && (s & BIT(NAK_OUT_PACKETS)))
					writel(BIT(CLEAR_NAK_OUT_PACKETS),
							&ep->regs->ep_rsp);
			}
		}

	} else if (ep->dma) {
		int	valid = 1;

		if (ep->is_in) {
			int	expect;

			/* preventing magic zlps is per-engine state, not
			 * per-transfer; irq logic must recover hiccups.
			 */
			expect = likely(req->req.zero ||
				(req->req.length % ep->ep.maxpacket));
			if (expect != ep->in_fifo_validate)
				valid = 0;
		}
		queue_dma(ep, req, valid);

	} /* else the irq handler advances the queue. */

	ep->responded = 1;
	if (req)
		list_add_tail(&req->queue, &ep->queue);
done:
	spin_unlock_irqrestore(&dev->lock, flags);

	/* pci writes may still be posted */
	return ret;

print_err:
	dev_err(&ep->dev->pdev->dev, "%s: error=%d\n", __func__, ret);
	return ret;
}

static inline void
dma_done(struct net2280_ep *ep,	struct net2280_request *req, u32 dmacount,
		int status)
{
	req->req.actual = req->req.length - (DMA_BYTE_COUNT_MASK & dmacount);
	done(ep, req, status);
}

static int scan_dma_completions(struct net2280_ep *ep)
{
	int num_completed = 0;

	/* only look at descriptors that were "naturally" retired,
	 * so fifo and list head state won't matter
	 */
	while (!list_empty(&ep->queue)) {
		struct net2280_request	*req;
		u32 req_dma_count;

		req = list_entry(ep->queue.next,
				struct net2280_request, queue);
		if (!req->valid)
			break;
		rmb();
		req_dma_count = le32_to_cpup(&req->td->dmacount);
		if ((req_dma_count & BIT(VALID_BIT)) != 0)
			break;

		/* SHORT_PACKET_TRANSFERRED_INTERRUPT handles "usb-short"
		 * cases where DMA must be aborted; this code handles
		 * all non-abort DMA completions.
		 */
		if (unlikely(req->td->dmadesc == 0)) {
			/* paranoia */
			u32 const ep_dmacount = readl(&ep->dma->dmacount);

			if (ep_dmacount & DMA_BYTE_COUNT_MASK)
				break;
			/* single transfer mode */
			dma_done(ep, req, req_dma_count, 0);
			num_completed++;
			break;
		} else if (!ep->is_in &&
			   (req->req.length % ep->ep.maxpacket) &&
			   !(ep->dev->quirks & PLX_PCIE)) {

			u32 const ep_stat = readl(&ep->regs->ep_stat);
			/* AVOID TROUBLE HERE by not issuing short reads from
			 * your gadget driver.  That helps avoids errata 0121,
			 * 0122, and 0124; not all cases trigger the warning.
			 */
			if ((ep_stat & BIT(NAK_OUT_PACKETS)) == 0) {
				ep_warn(ep->dev, "%s lost packet sync!\n",
						ep->ep.name);
				req->req.status = -EOVERFLOW;
			} else {
				u32 const ep_avail = readl(&ep->regs->ep_avail);
				if (ep_avail) {
					/* fifo gets flushed later */
					ep->out_overflow = 1;
					ep_dbg(ep->dev,
						"%s dma, discard %d len %d\n",
						ep->ep.name, ep_avail,
						req->req.length);
					req->req.status = -EOVERFLOW;
				}
			}
		}
		dma_done(ep, req, req_dma_count, 0);
		num_completed++;
	}

	return num_completed;
}

static void restart_dma(struct net2280_ep *ep)
{
	struct net2280_request	*req;

	if (ep->stopped)
		return;
	req = list_entry(ep->queue.next, struct net2280_request, queue);

	start_dma(ep, req);
}

static void abort_dma(struct net2280_ep *ep)
{
	/* abort the current transfer */
	if (likely(!list_empty(&ep->queue))) {
		/* FIXME work around errata 0121, 0122, 0124 */
		writel(BIT(DMA_ABORT), &ep->dma->dmastat);
		spin_stop_dma(ep->dma);
	} else
		stop_dma(ep->dma);
	scan_dma_completions(ep);
}

/* dequeue ALL requests */
static void nuke(struct net2280_ep *ep)
{
	struct net2280_request	*req;

	/* called with spinlock held */
	ep->stopped = 1;
	if (ep->dma)
		abort_dma(ep);
	while (!list_empty(&ep->queue)) {
		req = list_entry(ep->queue.next,
				struct net2280_request,
				queue);
		done(ep, req, -ESHUTDOWN);
	}
}

/* dequeue JUST ONE request */
static int net2280_dequeue(struct usb_ep *_ep, struct usb_request *_req)
{
	struct net2280_ep	*ep;
	struct net2280_request	*req;
	unsigned long		flags;
	u32			dmactl;
	int			stopped;

	ep = container_of(_ep, struct net2280_ep, ep);
	if (!_ep || (!ep->desc && ep->num != 0) || !_req) {
		pr_err("%s: Invalid ep=%p or ep->desc or req=%p\n",
						__func__, _ep, _req);
		return -EINVAL;
	}

	spin_lock_irqsave(&ep->dev->lock, flags);
	stopped = ep->stopped;

	/* quiesce dma while we patch the queue */
	dmactl = 0;
	ep->stopped = 1;
	if (ep->dma) {
		dmactl = readl(&ep->dma->dmactl);
		/* WARNING erratum 0127 may kick in ... */
		stop_dma(ep->dma);
		scan_dma_completions(ep);
	}

	/* make sure it's still queued on this endpoint */
	list_for_each_entry(req, &ep->queue, queue) {
		if (&req->req == _req)
			break;
	}
	if (&req->req != _req) {
		ep->stopped = stopped;
		spin_unlock_irqrestore(&ep->dev->lock, flags);
		ep_dbg(ep->dev, "%s: Request mismatch\n", __func__);
		return -EINVAL;
	}

	/* queue head may be partially complete. */
	if (ep->queue.next == &req->queue) {
		if (ep->dma) {
			ep_dbg(ep->dev, "unlink (%s) dma\n", _ep->name);
			_req->status = -ECONNRESET;
			abort_dma(ep);
			if (likely(ep->queue.next == &req->queue)) {
				/* NOTE: misreports single-transfer mode*/
				req->td->dmacount = 0;	/* invalidate */
				dma_done(ep, req,
					readl(&ep->dma->dmacount),
					-ECONNRESET);
			}
		} else {
			ep_dbg(ep->dev, "unlink (%s) pio\n", _ep->name);
			done(ep, req, -ECONNRESET);
		}
		req = NULL;
	}

	if (req)
		done(ep, req, -ECONNRESET);
	ep->stopped = stopped;

	if (ep->dma) {
		/* turn off dma on inactive queues */
		if (list_empty(&ep->queue))
			stop_dma(ep->dma);
		else if (!ep->stopped) {
			/* resume current request, or start new one */
			if (req)
				writel(dmactl, &ep->dma->dmactl);
			else
				start_dma(ep, list_entry(ep->queue.next,
					struct net2280_request, queue));
		}
	}

	spin_unlock_irqrestore(&ep->dev->lock, flags);
	return 0;
}

/*-------------------------------------------------------------------------*/

static int net2280_fifo_status(struct usb_ep *_ep);

static int
net2280_set_halt_and_wedge(struct usb_ep *_ep, int value, int wedged)
{
	struct net2280_ep	*ep;
	unsigned long		flags;
	int			retval = 0;

	ep = container_of(_ep, struct net2280_ep, ep);
	if (!_ep || (!ep->desc && ep->num != 0)) {
		pr_err("%s: Invalid ep=%p or ep->desc\n", __func__, _ep);
		return -EINVAL;
	}
	if (!ep->dev->driver || ep->dev->gadget.speed == USB_SPEED_UNKNOWN) {
		retval = -ESHUTDOWN;
		goto print_err;
	}
	if (ep->desc /* not ep0 */ && (ep->desc->bmAttributes & 0x03)
						== USB_ENDPOINT_XFER_ISOC) {
		retval = -EINVAL;
		goto print_err;
	}

	spin_lock_irqsave(&ep->dev->lock, flags);
	if (!list_empty(&ep->queue)) {
		retval = -EAGAIN;
		goto print_unlock;
	} else if (ep->is_in && value && net2280_fifo_status(_ep) != 0) {
		retval = -EAGAIN;
		goto print_unlock;
	} else {
		ep_vdbg(ep->dev, "%s %s %s\n", _ep->name,
				value ? "set" : "clear",
				wedged ? "wedge" : "halt");
		/* set/clear, then synch memory views with the device */
		if (value) {
			if (ep->num == 0)
				ep->dev->protocol_stall = 1;
			else
				set_halt(ep);
			if (wedged)
				ep->wedged = 1;
		} else {
			clear_halt(ep);
			if (ep->dev->quirks & PLX_PCIE &&
				!list_empty(&ep->queue) && ep->td_dma)
					restart_dma(ep);
			ep->wedged = 0;
		}
		(void) readl(&ep->regs->ep_rsp);
	}
	spin_unlock_irqrestore(&ep->dev->lock, flags);

	return retval;

print_unlock:
	spin_unlock_irqrestore(&ep->dev->lock, flags);
print_err:
	dev_err(&ep->dev->pdev->dev, "%s: error=%d\n", __func__, retval);
	return retval;
}

static int net2280_set_halt(struct usb_ep *_ep, int value)
{
	return net2280_set_halt_and_wedge(_ep, value, 0);
}

static int net2280_set_wedge(struct usb_ep *_ep)
{
	if (!_ep || _ep->name == ep0name) {
		pr_err("%s: Invalid ep=%p or ep0\n", __func__, _ep);
		return -EINVAL;
	}
	return net2280_set_halt_and_wedge(_ep, 1, 1);
}

static int net2280_fifo_status(struct usb_ep *_ep)
{
	struct net2280_ep	*ep;
	u32			avail;

	ep = container_of(_ep, struct net2280_ep, ep);
	if (!_ep || (!ep->desc && ep->num != 0)) {
		pr_err("%s: Invalid ep=%p or ep->desc\n", __func__, _ep);
		return -ENODEV;
	}
	if (!ep->dev->driver || ep->dev->gadget.speed == USB_SPEED_UNKNOWN) {
		dev_err(&ep->dev->pdev->dev,
			"%s: Invalid driver=%p or speed=%d\n",
			__func__, ep->dev->driver, ep->dev->gadget.speed);
		return -ESHUTDOWN;
	}

	avail = readl(&ep->regs->ep_avail) & (BIT(12) - 1);
	if (avail > ep->fifo_size) {
		dev_err(&ep->dev->pdev->dev, "%s: Fifo overflow\n", __func__);
		return -EOVERFLOW;
	}
	if (ep->is_in)
		avail = ep->fifo_size - avail;
	return avail;
}

static void net2280_fifo_flush(struct usb_ep *_ep)
{
	struct net2280_ep	*ep;

	ep = container_of(_ep, struct net2280_ep, ep);
	if (!_ep || (!ep->desc && ep->num != 0)) {
		pr_err("%s: Invalid ep=%p or ep->desc\n", __func__, _ep);
		return;
	}
	if (!ep->dev->driver || ep->dev->gadget.speed == USB_SPEED_UNKNOWN) {
		dev_err(&ep->dev->pdev->dev,
			"%s: Invalid driver=%p or speed=%d\n",
			__func__, ep->dev->driver, ep->dev->gadget.speed);
		return;
	}

	writel(BIT(FIFO_FLUSH), &ep->regs->ep_stat);
	(void) readl(&ep->regs->ep_rsp);
}

static const struct usb_ep_ops net2280_ep_ops = {
	.enable		= net2280_enable,
	.disable	= net2280_disable,

	.alloc_request	= net2280_alloc_request,
	.free_request	= net2280_free_request,

	.queue		= net2280_queue,
	.dequeue	= net2280_dequeue,

	.set_halt	= net2280_set_halt,
	.set_wedge	= net2280_set_wedge,
	.fifo_status	= net2280_fifo_status,
	.fifo_flush	= net2280_fifo_flush,
};

/*-------------------------------------------------------------------------*/

static int net2280_get_frame(struct usb_gadget *_gadget)
{
	struct net2280		*dev;
	unsigned long		flags;
	u16			retval;

	if (!_gadget)
		return -ENODEV;
	dev = container_of(_gadget, struct net2280, gadget);
	spin_lock_irqsave(&dev->lock, flags);
	retval = get_idx_reg(dev->regs, REG_FRAME) & 0x03ff;
	spin_unlock_irqrestore(&dev->lock, flags);
	return retval;
}

static int net2280_wakeup(struct usb_gadget *_gadget)
{
	struct net2280		*dev;
	u32			tmp;
	unsigned long		flags;

	if (!_gadget)
		return 0;
	dev = container_of(_gadget, struct net2280, gadget);

	spin_lock_irqsave(&dev->lock, flags);
	tmp = readl(&dev->usb->usbctl);
	if (tmp & BIT(DEVICE_REMOTE_WAKEUP_ENABLE))
		writel(BIT(GENERATE_RESUME), &dev->usb->usbstat);
	spin_unlock_irqrestore(&dev->lock, flags);

	/* pci writes may still be posted */
	return 0;
}

static int net2280_set_selfpowered(struct usb_gadget *_gadget, int value)
{
	struct net2280		*dev;
	u32			tmp;
	unsigned long		flags;

	if (!_gadget)
		return 0;
	dev = container_of(_gadget, struct net2280, gadget);

	spin_lock_irqsave(&dev->lock, flags);
	tmp = readl(&dev->usb->usbctl);
	if (value) {
		tmp |= BIT(SELF_POWERED_STATUS);
		_gadget->is_selfpowered = 1;
	} else {
		tmp &= ~BIT(SELF_POWERED_STATUS);
		_gadget->is_selfpowered = 0;
	}
	writel(tmp, &dev->usb->usbctl);
	spin_unlock_irqrestore(&dev->lock, flags);

	return 0;
}

static int net2280_pullup(struct usb_gadget *_gadget, int is_on)
{
	struct net2280  *dev;
	u32             tmp;
	unsigned long   flags;

	if (!_gadget)
		return -ENODEV;
	dev = container_of(_gadget, struct net2280, gadget);

	spin_lock_irqsave(&dev->lock, flags);
	tmp = readl(&dev->usb->usbctl);
	dev->softconnect = (is_on != 0);
	if (is_on) {
		ep0_start(dev);
		writel(tmp | BIT(USB_DETECT_ENABLE), &dev->usb->usbctl);
	} else {
		writel(tmp & ~BIT(USB_DETECT_ENABLE), &dev->usb->usbctl);
		stop_activity(dev, NULL);
	}

	spin_unlock_irqrestore(&dev->lock, flags);

	if (!is_on && dev->driver)
		dev->driver->disconnect(&dev->gadget);

	return 0;
}

static struct usb_ep *net2280_match_ep(struct usb_gadget *_gadget,
		struct usb_endpoint_descriptor *desc,
		struct usb_ss_ep_comp_descriptor *ep_comp)
{
	char name[8];
	struct usb_ep *ep;

	if (usb_endpoint_type(desc) == USB_ENDPOINT_XFER_INT) {
		/* ep-e, ep-f are PIO with only 64 byte fifos */
		ep = gadget_find_ep_by_name(_gadget, "ep-e");
		if (ep && usb_gadget_ep_match_desc(_gadget, ep, desc, ep_comp))
			return ep;
		ep = gadget_find_ep_by_name(_gadget, "ep-f");
		if (ep && usb_gadget_ep_match_desc(_gadget, ep, desc, ep_comp))
			return ep;
	}

	/* USB3380: Only first four endpoints have DMA channels. Allocate
	 * slower interrupt endpoints from PIO hw endpoints, to allow bulk/isoc
	 * endpoints use DMA hw endpoints.
	 */
	if (usb_endpoint_type(desc) == USB_ENDPOINT_XFER_INT &&
	    usb_endpoint_dir_in(desc)) {
		ep = gadget_find_ep_by_name(_gadget, "ep2in");
		if (ep && usb_gadget_ep_match_desc(_gadget, ep, desc, ep_comp))
			return ep;
		ep = gadget_find_ep_by_name(_gadget, "ep4in");
		if (ep && usb_gadget_ep_match_desc(_gadget, ep, desc, ep_comp))
			return ep;
	} else if (usb_endpoint_type(desc) == USB_ENDPOINT_XFER_INT &&
		   !usb_endpoint_dir_in(desc)) {
		ep = gadget_find_ep_by_name(_gadget, "ep1out");
		if (ep && usb_gadget_ep_match_desc(_gadget, ep, desc, ep_comp))
			return ep;
		ep = gadget_find_ep_by_name(_gadget, "ep3out");
		if (ep && usb_gadget_ep_match_desc(_gadget, ep, desc, ep_comp))
			return ep;
	} else if (usb_endpoint_type(desc) != USB_ENDPOINT_XFER_BULK &&
		   usb_endpoint_dir_in(desc)) {
		ep = gadget_find_ep_by_name(_gadget, "ep1in");
		if (ep && usb_gadget_ep_match_desc(_gadget, ep, desc, ep_comp))
			return ep;
		ep = gadget_find_ep_by_name(_gadget, "ep3in");
		if (ep && usb_gadget_ep_match_desc(_gadget, ep, desc, ep_comp))
			return ep;
	} else if (usb_endpoint_type(desc) != USB_ENDPOINT_XFER_BULK &&
		   !usb_endpoint_dir_in(desc)) {
		ep = gadget_find_ep_by_name(_gadget, "ep2out");
		if (ep && usb_gadget_ep_match_desc(_gadget, ep, desc, ep_comp))
			return ep;
		ep = gadget_find_ep_by_name(_gadget, "ep4out");
		if (ep && usb_gadget_ep_match_desc(_gadget, ep, desc, ep_comp))
			return ep;
	}

	/* USB3380: use same address for usb and hardware endpoints */
	snprintf(name, sizeof(name), "ep%d%s", usb_endpoint_num(desc),
			usb_endpoint_dir_in(desc) ? "in" : "out");
	ep = gadget_find_ep_by_name(_gadget, name);
	if (ep && usb_gadget_ep_match_desc(_gadget, ep, desc, ep_comp))
		return ep;

	return NULL;
}

static int net2280_start(struct usb_gadget *_gadget,
		struct usb_gadget_driver *driver);
static int net2280_stop(struct usb_gadget *_gadget);

static const struct usb_gadget_ops net2280_ops = {
	.get_frame	= net2280_get_frame,
	.wakeup		= net2280_wakeup,
	.set_selfpowered = net2280_set_selfpowered,
	.pullup		= net2280_pullup,
	.udc_start	= net2280_start,
	.udc_stop	= net2280_stop,
	.match_ep	= net2280_match_ep,
};

/*-------------------------------------------------------------------------*/

#ifdef	CONFIG_USB_GADGET_DEBUG_FILES

/* FIXME move these into procfs, and use seq_file.
 * Sysfs _still_ doesn't behave for arbitrarily sized files,
 * and also doesn't help products using this with 2.4 kernels.
 */

/* "function" sysfs attribute */
static ssize_t function_show(struct device *_dev, struct device_attribute *attr,
			     char *buf)
{
	struct net2280	*dev = dev_get_drvdata(_dev);

	if (!dev->driver || !dev->driver->function ||
			strlen(dev->driver->function) > PAGE_SIZE)
		return 0;
	return scnprintf(buf, PAGE_SIZE, "%s\n", dev->driver->function);
}
static DEVICE_ATTR_RO(function);

static ssize_t registers_show(struct device *_dev,
			      struct device_attribute *attr, char *buf)
{
	struct net2280		*dev;
	char			*next;
	unsigned		size, t;
	unsigned long		flags;
	int			i;
	u32			t1, t2;
	const char		*s;

	dev = dev_get_drvdata(_dev);
	next = buf;
	size = PAGE_SIZE;
	spin_lock_irqsave(&dev->lock, flags);

	if (dev->driver)
		s = dev->driver->driver.name;
	else
		s = "(none)";

	/* Main Control Registers */
	t = scnprintf(next, size, "%s version " DRIVER_VERSION
			", chiprev %04x\n\n"
			"devinit %03x fifoctl %08x gadget '%s'\n"
			"pci irqenb0 %02x irqenb1 %08x "
			"irqstat0 %04x irqstat1 %08x\n",
			driver_name, dev->chiprev,
			readl(&dev->regs->devinit),
			readl(&dev->regs->fifoctl),
			s,
			readl(&dev->regs->pciirqenb0),
			readl(&dev->regs->pciirqenb1),
			readl(&dev->regs->irqstat0),
			readl(&dev->regs->irqstat1));
	size -= t;
	next += t;

	/* USB Control Registers */
	t1 = readl(&dev->usb->usbctl);
	t2 = readl(&dev->usb->usbstat);
	if (t1 & BIT(VBUS_PIN)) {
		if (t2 & BIT(HIGH_SPEED))
			s = "high speed";
		else if (dev->gadget.speed == USB_SPEED_UNKNOWN)
			s = "powered";
		else
			s = "full speed";
		/* full speed bit (6) not working?? */
	} else
			s = "not attached";
	t = scnprintf(next, size,
			"stdrsp %08x usbctl %08x usbstat %08x "
				"addr 0x%02x (%s)\n",
			readl(&dev->usb->stdrsp), t1, t2,
			readl(&dev->usb->ouraddr), s);
	size -= t;
	next += t;

	/* PCI Master Control Registers */

	/* DMA Control Registers */

	/* Configurable EP Control Registers */
	for (i = 0; i < dev->n_ep; i++) {
		struct net2280_ep	*ep;

		ep = &dev->ep[i];
		if (i && !ep->desc)
			continue;

		t1 = readl(&ep->cfg->ep_cfg);
		t2 = readl(&ep->regs->ep_rsp) & 0xff;
		t = scnprintf(next, size,
				"\n%s\tcfg %05x rsp (%02x) %s%s%s%s%s%s%s%s"
					"irqenb %02x\n",
				ep->ep.name, t1, t2,
				(t2 & BIT(CLEAR_NAK_OUT_PACKETS))
					? "NAK " : "",
				(t2 & BIT(CLEAR_EP_HIDE_STATUS_PHASE))
					? "hide " : "",
				(t2 & BIT(CLEAR_EP_FORCE_CRC_ERROR))
					? "CRC " : "",
				(t2 & BIT(CLEAR_INTERRUPT_MODE))
					? "interrupt " : "",
				(t2 & BIT(CLEAR_CONTROL_STATUS_PHASE_HANDSHAKE))
					? "status " : "",
				(t2 & BIT(CLEAR_NAK_OUT_PACKETS_MODE))
					? "NAKmode " : "",
				(t2 & BIT(CLEAR_ENDPOINT_TOGGLE))
					? "DATA1 " : "DATA0 ",
				(t2 & BIT(CLEAR_ENDPOINT_HALT))
					? "HALT " : "",
				readl(&ep->regs->ep_irqenb));
		size -= t;
		next += t;

		t = scnprintf(next, size,
				"\tstat %08x avail %04x "
				"(ep%d%s-%s)%s\n",
				readl(&ep->regs->ep_stat),
				readl(&ep->regs->ep_avail),
				t1 & 0x0f, DIR_STRING(t1),
				type_string(t1 >> 8),
				ep->stopped ? "*" : "");
		size -= t;
		next += t;

		if (!ep->dma)
			continue;

		t = scnprintf(next, size,
				"  dma\tctl %08x stat %08x count %08x\n"
				"\taddr %08x desc %08x\n",
				readl(&ep->dma->dmactl),
				readl(&ep->dma->dmastat),
				readl(&ep->dma->dmacount),
				readl(&ep->dma->dmaaddr),
				readl(&ep->dma->dmadesc));
		size -= t;
		next += t;

	}

	/* Indexed Registers (none yet) */

	/* Statistics */
	t = scnprintf(next, size, "\nirqs:  ");
	size -= t;
	next += t;
	for (i = 0; i < dev->n_ep; i++) {
		struct net2280_ep	*ep;

		ep = &dev->ep[i];
		if (i && !ep->irqs)
			continue;
		t = scnprintf(next, size, " %s/%lu", ep->ep.name, ep->irqs);
		size -= t;
		next += t;

	}
	t = scnprintf(next, size, "\n");
	size -= t;
	next += t;

	spin_unlock_irqrestore(&dev->lock, flags);

	return PAGE_SIZE - size;
}
static DEVICE_ATTR_RO(registers);

static ssize_t queues_show(struct device *_dev, struct device_attribute *attr,
			   char *buf)
{
	struct net2280		*dev;
	char			*next;
	unsigned		size;
	unsigned long		flags;
	int			i;

	dev = dev_get_drvdata(_dev);
	next = buf;
	size = PAGE_SIZE;
	spin_lock_irqsave(&dev->lock, flags);

	for (i = 0; i < dev->n_ep; i++) {
		struct net2280_ep		*ep = &dev->ep[i];
		struct net2280_request		*req;
		int				t;

		if (i != 0) {
			const struct usb_endpoint_descriptor	*d;

			d = ep->desc;
			if (!d)
				continue;
			t = d->bEndpointAddress;
			t = scnprintf(next, size,
				"\n%s (ep%d%s-%s) max %04x %s fifo %d\n",
				ep->ep.name, t & USB_ENDPOINT_NUMBER_MASK,
				(t & USB_DIR_IN) ? "in" : "out",
				type_string(d->bmAttributes),
				usb_endpoint_maxp(d),
				ep->dma ? "dma" : "pio", ep->fifo_size
				);
		} else /* ep0 should only have one transfer queued */
			t = scnprintf(next, size, "ep0 max 64 pio %s\n",
					ep->is_in ? "in" : "out");
		if (t <= 0 || t > size)
			goto done;
		size -= t;
		next += t;

		if (list_empty(&ep->queue)) {
			t = scnprintf(next, size, "\t(nothing queued)\n");
			if (t <= 0 || t > size)
				goto done;
			size -= t;
			next += t;
			continue;
		}
		list_for_each_entry(req, &ep->queue, queue) {
			if (ep->dma && req->td_dma == readl(&ep->dma->dmadesc))
				t = scnprintf(next, size,
					"\treq %p len %d/%d "
					"buf %p (dmacount %08x)\n",
					&req->req, req->req.actual,
					req->req.length, req->req.buf,
					readl(&ep->dma->dmacount));
			else
				t = scnprintf(next, size,
					"\treq %p len %d/%d buf %p\n",
					&req->req, req->req.actual,
					req->req.length, req->req.buf);
			if (t <= 0 || t > size)
				goto done;
			size -= t;
			next += t;

			if (ep->dma) {
				struct net2280_dma	*td;

				td = req->td;
				t = scnprintf(next, size, "\t    td %08x "
					" count %08x buf %08x desc %08x\n",
					(u32) req->td_dma,
					le32_to_cpu(td->dmacount),
					le32_to_cpu(td->dmaaddr),
					le32_to_cpu(td->dmadesc));
				if (t <= 0 || t > size)
					goto done;
				size -= t;
				next += t;
			}
		}
	}

done:
	spin_unlock_irqrestore(&dev->lock, flags);
	return PAGE_SIZE - size;
}
static DEVICE_ATTR_RO(queues);


#else

#define device_create_file(a, b)	(0)
#define device_remove_file(a, b)	do { } while (0)

#endif

/*-------------------------------------------------------------------------*/

/* another driver-specific mode might be a request type doing dma
 * to/from another device fifo instead of to/from memory.
 */

static void set_fifo_mode(struct net2280 *dev, int mode)
{
	/* keeping high bits preserves BAR2 */
	writel((0xffff << PCI_BASE2_RANGE) | mode, &dev->regs->fifoctl);

	/* always ep-{a,b,e,f} ... maybe not ep-c or ep-d */
	INIT_LIST_HEAD(&dev->gadget.ep_list);
	list_add_tail(&dev->ep[1].ep.ep_list, &dev->gadget.ep_list);
	list_add_tail(&dev->ep[2].ep.ep_list, &dev->gadget.ep_list);
	switch (mode) {
	case 0:
		list_add_tail(&dev->ep[3].ep.ep_list, &dev->gadget.ep_list);
		list_add_tail(&dev->ep[4].ep.ep_list, &dev->gadget.ep_list);
		dev->ep[1].fifo_size = dev->ep[2].fifo_size = 1024;
		break;
	case 1:
		dev->ep[1].fifo_size = dev->ep[2].fifo_size = 2048;
		break;
	case 2:
		list_add_tail(&dev->ep[3].ep.ep_list, &dev->gadget.ep_list);
		dev->ep[1].fifo_size = 2048;
		dev->ep[2].fifo_size = 1024;
		break;
	}
	/* fifo sizes for ep0, ep-c, ep-d, ep-e, and ep-f never change */
	list_add_tail(&dev->ep[5].ep.ep_list, &dev->gadget.ep_list);
	list_add_tail(&dev->ep[6].ep.ep_list, &dev->gadget.ep_list);
}

static void defect7374_disable_data_eps(struct net2280 *dev)
{
	/*
	 * For Defect 7374, disable data EPs (and more):
	 *  - This phase undoes the earlier phase of the Defect 7374 workaround,
	 *    returing ep regs back to normal.
	 */
	struct net2280_ep *ep;
	int i;
	unsigned char ep_sel;
	u32 tmp_reg;

	for (i = 1; i < 5; i++) {
		ep = &dev->ep[i];
		writel(i, &ep->cfg->ep_cfg);
	}

	/* CSROUT, CSRIN, PCIOUT, PCIIN, STATIN, RCIN */
	for (i = 0; i < 6; i++)
		writel(0, &dev->dep[i].dep_cfg);

	for (ep_sel = 0; ep_sel <= 21; ep_sel++) {
		/* Select an endpoint for subsequent operations: */
		tmp_reg = readl(&dev->plregs->pl_ep_ctrl);
		writel(((tmp_reg & ~0x1f) | ep_sel), &dev->plregs->pl_ep_ctrl);

		if (ep_sel < 2 || (ep_sel > 9 && ep_sel < 14) ||
					ep_sel == 18 || ep_sel == 20)
			continue;

		/* Change settings on some selected endpoints */
		tmp_reg = readl(&dev->plregs->pl_ep_cfg_4);
		tmp_reg &= ~BIT(NON_CTRL_IN_TOLERATE_BAD_DIR);
		writel(tmp_reg, &dev->plregs->pl_ep_cfg_4);
		tmp_reg = readl(&dev->plregs->pl_ep_ctrl);
		tmp_reg |= BIT(EP_INITIALIZED);
		writel(tmp_reg, &dev->plregs->pl_ep_ctrl);
	}
}

static void defect7374_enable_data_eps_zero(struct net2280 *dev)
{
	u32 tmp = 0, tmp_reg;
	u32 scratch;
	int i;
	unsigned char ep_sel;

	scratch = get_idx_reg(dev->regs, SCRATCH);

	WARN_ON((scratch & (0xf << DEFECT7374_FSM_FIELD))
		== DEFECT7374_FSM_SS_CONTROL_READ);

	scratch &= ~(0xf << DEFECT7374_FSM_FIELD);

	ep_warn(dev, "Operate Defect 7374 workaround soft this time");
	ep_warn(dev, "It will operate on cold-reboot and SS connect");

	/*GPEPs:*/
	tmp = ((0 << ENDPOINT_NUMBER) | BIT(ENDPOINT_DIRECTION) |
			(2 << OUT_ENDPOINT_TYPE) | (2 << IN_ENDPOINT_TYPE) |
			((dev->enhanced_mode) ?
			 BIT(OUT_ENDPOINT_ENABLE) | BIT(IN_ENDPOINT_ENABLE) :
			 BIT(ENDPOINT_ENABLE)));

	for (i = 1; i < 5; i++)
		writel(tmp, &dev->ep[i].cfg->ep_cfg);

	/* CSRIN, PCIIN, STATIN, RCIN*/
	tmp = ((0 << ENDPOINT_NUMBER) | BIT(ENDPOINT_ENABLE));
	writel(tmp, &dev->dep[1].dep_cfg);
	writel(tmp, &dev->dep[3].dep_cfg);
	writel(tmp, &dev->dep[4].dep_cfg);
	writel(tmp, &dev->dep[5].dep_cfg);

	/*Implemented for development and debug.
	 * Can be refined/tuned later.*/
	for (ep_sel = 0; ep_sel <= 21; ep_sel++) {
		/* Select an endpoint for subsequent operations: */
		tmp_reg = readl(&dev->plregs->pl_ep_ctrl);
		writel(((tmp_reg & ~0x1f) | ep_sel),
				&dev->plregs->pl_ep_ctrl);

		if (ep_sel == 1) {
			tmp =
				(readl(&dev->plregs->pl_ep_ctrl) |
				 BIT(CLEAR_ACK_ERROR_CODE) | 0);
			writel(tmp, &dev->plregs->pl_ep_ctrl);
			continue;
		}

		if (ep_sel == 0 || (ep_sel > 9 && ep_sel < 14) ||
				ep_sel == 18  || ep_sel == 20)
			continue;

		tmp = (readl(&dev->plregs->pl_ep_cfg_4) |
				BIT(NON_CTRL_IN_TOLERATE_BAD_DIR) | 0);
		writel(tmp, &dev->plregs->pl_ep_cfg_4);

		tmp = readl(&dev->plregs->pl_ep_ctrl) &
			~BIT(EP_INITIALIZED);
		writel(tmp, &dev->plregs->pl_ep_ctrl);

	}

	/* Set FSM to focus on the first Control Read:
	 * - Tip: Connection speed is known upon the first
	 * setup request.*/
	scratch |= DEFECT7374_FSM_WAITING_FOR_CONTROL_READ;
	set_idx_reg(dev->regs, SCRATCH, scratch);

}

/* keeping it simple:
 * - one bus driver, initted first;
 * - one function driver, initted second
 *
 * most of the work to support multiple net2280 controllers would
 * be to associate this gadget driver (yes?) with all of them, or
 * perhaps to bind specific drivers to specific devices.
 */

static void usb_reset_228x(struct net2280 *dev)
{
	u32	tmp;

	dev->gadget.speed = USB_SPEED_UNKNOWN;
	(void) readl(&dev->usb->usbctl);

	net2280_led_init(dev);

	/* disable automatic responses, and irqs */
	writel(0, &dev->usb->stdrsp);
	writel(0, &dev->regs->pciirqenb0);
	writel(0, &dev->regs->pciirqenb1);

	/* clear old dma and irq state */
	for (tmp = 0; tmp < 4; tmp++) {
		struct net2280_ep       *ep = &dev->ep[tmp + 1];
		if (ep->dma)
			abort_dma(ep);
	}

	writel(~0, &dev->regs->irqstat0),
	writel(~(u32)BIT(SUSPEND_REQUEST_INTERRUPT), &dev->regs->irqstat1),

	/* reset, and enable pci */
	tmp = readl(&dev->regs->devinit) |
		BIT(PCI_ENABLE) |
		BIT(FIFO_SOFT_RESET) |
		BIT(USB_SOFT_RESET) |
		BIT(M8051_RESET);
	writel(tmp, &dev->regs->devinit);

	/* standard fifo and endpoint allocations */
	set_fifo_mode(dev, (fifo_mode <= 2) ? fifo_mode : 0);
}

static void usb_reset_338x(struct net2280 *dev)
{
	u32 tmp;

	dev->gadget.speed = USB_SPEED_UNKNOWN;
	(void)readl(&dev->usb->usbctl);

	net2280_led_init(dev);

	if (dev->bug7734_patched) {
		/* disable automatic responses, and irqs */
		writel(0, &dev->usb->stdrsp);
		writel(0, &dev->regs->pciirqenb0);
		writel(0, &dev->regs->pciirqenb1);
	}

	/* clear old dma and irq state */
	for (tmp = 0; tmp < 4; tmp++) {
		struct net2280_ep *ep = &dev->ep[tmp + 1];
		struct net2280_dma_regs __iomem *dma;

		if (ep->dma) {
			abort_dma(ep);
		} else {
			dma = &dev->dma[tmp];
			writel(BIT(DMA_ABORT), &dma->dmastat);
			writel(0, &dma->dmactl);
		}
	}

	writel(~0, &dev->regs->irqstat0), writel(~0, &dev->regs->irqstat1);

	if (dev->bug7734_patched) {
		/* reset, and enable pci */
		tmp = readl(&dev->regs->devinit) |
		    BIT(PCI_ENABLE) |
		    BIT(FIFO_SOFT_RESET) |
		    BIT(USB_SOFT_RESET) |
		    BIT(M8051_RESET);

		writel(tmp, &dev->regs->devinit);
	}

	/* always ep-{1,2,3,4} ... maybe not ep-3 or ep-4 */
	INIT_LIST_HEAD(&dev->gadget.ep_list);

	for (tmp = 1; tmp < dev->n_ep; tmp++)
		list_add_tail(&dev->ep[tmp].ep.ep_list, &dev->gadget.ep_list);

}

static void usb_reset(struct net2280 *dev)
{
	if (dev->quirks & PLX_LEGACY)
		return usb_reset_228x(dev);
	return usb_reset_338x(dev);
}

static void usb_reinit_228x(struct net2280 *dev)
{
	u32	tmp;

	/* basic endpoint init */
	for (tmp = 0; tmp < 7; tmp++) {
		struct net2280_ep	*ep = &dev->ep[tmp];

		ep->ep.name = ep_info_dft[tmp].name;
		ep->ep.caps = ep_info_dft[tmp].caps;
		ep->dev = dev;
		ep->num = tmp;

		if (tmp > 0 && tmp <= 4) {
			ep->fifo_size = 1024;
			ep->dma = &dev->dma[tmp - 1];
		} else
			ep->fifo_size = 64;
		ep->regs = &dev->epregs[tmp];
		ep->cfg = &dev->epregs[tmp];
		ep_reset_228x(dev->regs, ep);
	}
	usb_ep_set_maxpacket_limit(&dev->ep[0].ep, 64);
	usb_ep_set_maxpacket_limit(&dev->ep[5].ep, 64);
	usb_ep_set_maxpacket_limit(&dev->ep[6].ep, 64);

	dev->gadget.ep0 = &dev->ep[0].ep;
	dev->ep[0].stopped = 0;
	INIT_LIST_HEAD(&dev->gadget.ep0->ep_list);

	/* we want to prevent lowlevel/insecure access from the USB host,
	 * but erratum 0119 means this enable bit is ignored
	 */
	for (tmp = 0; tmp < 5; tmp++)
		writel(EP_DONTUSE, &dev->dep[tmp].dep_cfg);
}

static void usb_reinit_338x(struct net2280 *dev)
{
	int i;
	u32 tmp, val;
	static const u32 ne[9] = { 0, 1, 2, 3, 4, 1, 2, 3, 4 };
	static const u32 ep_reg_addr[9] = { 0x00, 0xC0, 0x00, 0xC0, 0x00,
						0x00, 0xC0, 0x00, 0xC0 };

	/* basic endpoint init */
	for (i = 0; i < dev->n_ep; i++) {
		struct net2280_ep *ep = &dev->ep[i];

		ep->ep.name = dev->enhanced_mode ? ep_info_adv[i].name :
						   ep_info_dft[i].name;
		ep->ep.caps = dev->enhanced_mode ? ep_info_adv[i].caps :
						   ep_info_dft[i].caps;
		ep->dev = dev;
		ep->num = i;

		if (i > 0 && i <= 4)
			ep->dma = &dev->dma[i - 1];

		if (dev->enhanced_mode) {
			ep->cfg = &dev->epregs[ne[i]];
			/*
			 * Set USB endpoint number, hardware allows same number
			 * in both directions.
			 */
			 if (i > 0 && i < 5)
				writel(ne[i], &ep->cfg->ep_cfg);
			ep->regs = (struct net2280_ep_regs __iomem *)
				(((void __iomem *)&dev->epregs[ne[i]]) +
				ep_reg_addr[i]);
		} else {
			ep->cfg = &dev->epregs[i];
			ep->regs = &dev->epregs[i];
		}

		ep->fifo_size = (i != 0) ? 2048 : 512;

		ep_reset_338x(dev->regs, ep);
	}
	usb_ep_set_maxpacket_limit(&dev->ep[0].ep, 512);

	dev->gadget.ep0 = &dev->ep[0].ep;
	dev->ep[0].stopped = 0;

	/* Link layer set up */
	if (dev->bug7734_patched) {
		tmp = readl(&dev->usb_ext->usbctl2) &
		    ~(BIT(U1_ENABLE) | BIT(U2_ENABLE) | BIT(LTM_ENABLE));
		writel(tmp, &dev->usb_ext->usbctl2);
	}

	/* Hardware Defect and Workaround */
	val = readl(&dev->ll_lfps_regs->ll_lfps_5);
	val &= ~(0xf << TIMER_LFPS_6US);
	val |= 0x5 << TIMER_LFPS_6US;
	writel(val, &dev->ll_lfps_regs->ll_lfps_5);

	val = readl(&dev->ll_lfps_regs->ll_lfps_6);
	val &= ~(0xffff << TIMER_LFPS_80US);
	val |= 0x0100 << TIMER_LFPS_80US;
	writel(val, &dev->ll_lfps_regs->ll_lfps_6);

	/*
	 * AA_AB Errata. Issue 4. Workaround for SuperSpeed USB
	 * Hot Reset Exit Handshake may Fail in Specific Case using
	 * Default Register Settings. Workaround for Enumeration test.
	 */
	val = readl(&dev->ll_tsn_regs->ll_tsn_counters_2);
	val &= ~(0x1f << HOT_TX_NORESET_TS2);
	val |= 0x10 << HOT_TX_NORESET_TS2;
	writel(val, &dev->ll_tsn_regs->ll_tsn_counters_2);

	val = readl(&dev->ll_tsn_regs->ll_tsn_counters_3);
	val &= ~(0x1f << HOT_RX_RESET_TS2);
	val |= 0x3 << HOT_RX_RESET_TS2;
	writel(val, &dev->ll_tsn_regs->ll_tsn_counters_3);

	/*
	 * Set Recovery Idle to Recover bit:
	 * - On SS connections, setting Recovery Idle to Recover Fmw improves
	 *   link robustness with various hosts and hubs.
	 * - It is safe to set for all connection speeds; all chip revisions.
	 * - R-M-W to leave other bits undisturbed.
	 * - Reference PLX TT-7372
	*/
	val = readl(&dev->ll_chicken_reg->ll_tsn_chicken_bit);
	val |= BIT(RECOVERY_IDLE_TO_RECOVER_FMW);
	writel(val, &dev->ll_chicken_reg->ll_tsn_chicken_bit);

	INIT_LIST_HEAD(&dev->gadget.ep0->ep_list);

	/* disable dedicated endpoints */
	writel(0x0D, &dev->dep[0].dep_cfg);
	writel(0x0D, &dev->dep[1].dep_cfg);
	writel(0x0E, &dev->dep[2].dep_cfg);
	writel(0x0E, &dev->dep[3].dep_cfg);
	writel(0x0F, &dev->dep[4].dep_cfg);
	writel(0x0C, &dev->dep[5].dep_cfg);
}

static void usb_reinit(struct net2280 *dev)
{
	if (dev->quirks & PLX_LEGACY)
		return usb_reinit_228x(dev);
	return usb_reinit_338x(dev);
}

static void ep0_start_228x(struct net2280 *dev)
{
	writel(BIT(CLEAR_EP_HIDE_STATUS_PHASE) |
		BIT(CLEAR_NAK_OUT_PACKETS) |
		BIT(CLEAR_CONTROL_STATUS_PHASE_HANDSHAKE),
		&dev->epregs[0].ep_rsp);

	/*
	 * hardware optionally handles a bunch of standard requests
	 * that the API hides from drivers anyway.  have it do so.
	 * endpoint status/features are handled in software, to
	 * help pass tests for some dubious behavior.
	 */
	writel(BIT(SET_TEST_MODE) |
		BIT(SET_ADDRESS) |
		BIT(DEVICE_SET_CLEAR_DEVICE_REMOTE_WAKEUP) |
		BIT(GET_DEVICE_STATUS) |
		BIT(GET_INTERFACE_STATUS),
		&dev->usb->stdrsp);
	writel(BIT(USB_ROOT_PORT_WAKEUP_ENABLE) |
		BIT(SELF_POWERED_USB_DEVICE) |
		BIT(REMOTE_WAKEUP_SUPPORT) |
		(dev->softconnect << USB_DETECT_ENABLE) |
		BIT(SELF_POWERED_STATUS),
		&dev->usb->usbctl);

	/* enable irqs so we can see ep0 and general operation  */
	writel(BIT(SETUP_PACKET_INTERRUPT_ENABLE) |
		BIT(ENDPOINT_0_INTERRUPT_ENABLE),
		&dev->regs->pciirqenb0);
	writel(BIT(PCI_INTERRUPT_ENABLE) |
		BIT(PCI_MASTER_ABORT_RECEIVED_INTERRUPT_ENABLE) |
		BIT(PCI_TARGET_ABORT_RECEIVED_INTERRUPT_ENABLE) |
		BIT(PCI_RETRY_ABORT_INTERRUPT_ENABLE) |
		BIT(VBUS_INTERRUPT_ENABLE) |
		BIT(ROOT_PORT_RESET_INTERRUPT_ENABLE) |
		BIT(SUSPEND_REQUEST_CHANGE_INTERRUPT_ENABLE),
		&dev->regs->pciirqenb1);

	/* don't leave any writes posted */
	(void) readl(&dev->usb->usbctl);
}

static void ep0_start_338x(struct net2280 *dev)
{

	if (dev->bug7734_patched)
		writel(BIT(CLEAR_NAK_OUT_PACKETS_MODE) |
		       BIT(SET_EP_HIDE_STATUS_PHASE),
		       &dev->epregs[0].ep_rsp);

	/*
	 * hardware optionally handles a bunch of standard requests
	 * that the API hides from drivers anyway.  have it do so.
	 * endpoint status/features are handled in software, to
	 * help pass tests for some dubious behavior.
	 */
	writel(BIT(SET_ISOCHRONOUS_DELAY) |
	       BIT(SET_SEL) |
	       BIT(SET_TEST_MODE) |
	       BIT(SET_ADDRESS) |
	       BIT(GET_INTERFACE_STATUS) |
	       BIT(GET_DEVICE_STATUS),
		&dev->usb->stdrsp);
	dev->wakeup_enable = 1;
	writel(BIT(USB_ROOT_PORT_WAKEUP_ENABLE) |
	       (dev->softconnect << USB_DETECT_ENABLE) |
	       BIT(DEVICE_REMOTE_WAKEUP_ENABLE),
	       &dev->usb->usbctl);

	/* enable irqs so we can see ep0 and general operation  */
	writel(BIT(SETUP_PACKET_INTERRUPT_ENABLE) |
	       BIT(ENDPOINT_0_INTERRUPT_ENABLE),
	       &dev->regs->pciirqenb0);
	writel(BIT(PCI_INTERRUPT_ENABLE) |
	       BIT(ROOT_PORT_RESET_INTERRUPT_ENABLE) |
	       BIT(SUSPEND_REQUEST_CHANGE_INTERRUPT_ENABLE) |
	       BIT(VBUS_INTERRUPT_ENABLE),
	       &dev->regs->pciirqenb1);

	/* don't leave any writes posted */
	(void)readl(&dev->usb->usbctl);
}

static void ep0_start(struct net2280 *dev)
{
	if (dev->quirks & PLX_LEGACY)
		return ep0_start_228x(dev);
	return ep0_start_338x(dev);
}

/* when a driver is successfully registered, it will receive
 * control requests including set_configuration(), which enables
 * non-control requests.  then usb traffic follows until a
 * disconnect is reported.  then a host may connect again, or
 * the driver might get unbound.
 */
static int net2280_start(struct usb_gadget *_gadget,
		struct usb_gadget_driver *driver)
{
	struct net2280		*dev;
	int			retval;
	unsigned		i;

	/* insist on high speed support from the driver, since
	 * (dev->usb->xcvrdiag & FORCE_FULL_SPEED_MODE)
	 * "must not be used in normal operation"
	 */
	if (!driver || driver->max_speed < USB_SPEED_HIGH ||
			!driver->setup)
		return -EINVAL;

	dev = container_of(_gadget, struct net2280, gadget);

	for (i = 0; i < dev->n_ep; i++)
		dev->ep[i].irqs = 0;

	/* hook up the driver ... */
	driver->driver.bus = NULL;
	dev->driver = driver;

	retval = device_create_file(&dev->pdev->dev, &dev_attr_function);
	if (retval)
		goto err_unbind;
	retval = device_create_file(&dev->pdev->dev, &dev_attr_queues);
	if (retval)
		goto err_func;

	/* enable host detection and ep0; and we're ready
	 * for set_configuration as well as eventual disconnect.
	 */
	net2280_led_active(dev, 1);

	if ((dev->quirks & PLX_PCIE) && !dev->bug7734_patched)
		defect7374_enable_data_eps_zero(dev);

	ep0_start(dev);

	/* pci writes may still be posted */
	return 0;

err_func:
	device_remove_file(&dev->pdev->dev, &dev_attr_function);
err_unbind:
	dev->driver = NULL;
	return retval;
}

static void stop_activity(struct net2280 *dev, struct usb_gadget_driver *driver)
{
	int			i;

	/* don't disconnect if it's not connected */
	if (dev->gadget.speed == USB_SPEED_UNKNOWN)
		driver = NULL;

	/* stop hardware; prevent new request submissions;
	 * and kill any outstanding requests.
	 */
	usb_reset(dev);
	for (i = 0; i < dev->n_ep; i++)
		nuke(&dev->ep[i]);

	/* report disconnect; the driver is already quiesced */
	if (driver) {
		spin_unlock(&dev->lock);
		driver->disconnect(&dev->gadget);
		spin_lock(&dev->lock);
	}

	usb_reinit(dev);
}

static int net2280_stop(struct usb_gadget *_gadget)
{
	struct net2280	*dev;
	unsigned long	flags;

	dev = container_of(_gadget, struct net2280, gadget);

	spin_lock_irqsave(&dev->lock, flags);
	stop_activity(dev, NULL);
	spin_unlock_irqrestore(&dev->lock, flags);

	net2280_led_active(dev, 0);

	device_remove_file(&dev->pdev->dev, &dev_attr_function);
	device_remove_file(&dev->pdev->dev, &dev_attr_queues);

	dev->driver = NULL;

	return 0;
}

/*-------------------------------------------------------------------------*/

/* handle ep0, ep-e, ep-f with 64 byte packets: packet per irq.
 * also works for dma-capable endpoints, in pio mode or just
 * to manually advance the queue after short OUT transfers.
 */
static void handle_ep_small(struct net2280_ep *ep)
{
	struct net2280_request	*req;
	u32			t;
	/* 0 error, 1 mid-data, 2 done */
	int			mode = 1;

	if (!list_empty(&ep->queue))
		req = list_entry(ep->queue.next,
			struct net2280_request, queue);
	else
		req = NULL;

	/* ack all, and handle what we care about */
	t = readl(&ep->regs->ep_stat);
	ep->irqs++;

	ep_vdbg(ep->dev, "%s ack ep_stat %08x, req %p\n",
			ep->ep.name, t, req ? &req->req : NULL);

	if (!ep->is_in || (ep->dev->quirks & PLX_2280))
		writel(t & ~BIT(NAK_OUT_PACKETS), &ep->regs->ep_stat);
	else
		/* Added for 2282 */
		writel(t, &ep->regs->ep_stat);

	/* for ep0, monitor token irqs to catch data stage length errors
	 * and to synchronize on status.
	 *
	 * also, to defer reporting of protocol stalls ... here's where
	 * data or status first appears, handling stalls here should never
	 * cause trouble on the host side..
	 *
	 * control requests could be slightly faster without token synch for
	 * status, but status can jam up that way.
	 */
	if (unlikely(ep->num == 0)) {
		if (ep->is_in) {
			/* status; stop NAKing */
			if (t & BIT(DATA_OUT_PING_TOKEN_INTERRUPT)) {
				if (ep->dev->protocol_stall) {
					ep->stopped = 1;
					set_halt(ep);
				}
				if (!req)
					allow_status(ep);
				mode = 2;
			/* reply to extra IN data tokens with a zlp */
			} else if (t & BIT(DATA_IN_TOKEN_INTERRUPT)) {
				if (ep->dev->protocol_stall) {
					ep->stopped = 1;
					set_halt(ep);
					mode = 2;
				} else if (ep->responded &&
						!req && !ep->stopped)
					write_fifo(ep, NULL);
			}
		} else {
			/* status; stop NAKing */
			if (t & BIT(DATA_IN_TOKEN_INTERRUPT)) {
				if (ep->dev->protocol_stall) {
					ep->stopped = 1;
					set_halt(ep);
				}
				mode = 2;
			/* an extra OUT token is an error */
			} else if (((t & BIT(DATA_OUT_PING_TOKEN_INTERRUPT)) &&
					req &&
					req->req.actual == req->req.length) ||
					(ep->responded && !req)) {
				ep->dev->protocol_stall = 1;
				set_halt(ep);
				ep->stopped = 1;
				if (req)
					done(ep, req, -EOVERFLOW);
				req = NULL;
			}
		}
	}

	if (unlikely(!req))
		return;

	/* manual DMA queue advance after short OUT */
	if (likely(ep->dma)) {
		if (t & BIT(SHORT_PACKET_TRANSFERRED_INTERRUPT)) {
			struct net2280_request *stuck_req = NULL;
			int	stopped = ep->stopped;
			int	num_completed;
			int	stuck = 0;
			u32	count;

			/* TRANSFERRED works around OUT_DONE erratum 0112.
			 * we expect (N <= maxpacket) bytes; host wrote M.
			 * iff (M < N) we won't ever see a DMA interrupt.
			 */
			ep->stopped = 1;
			for (count = 0; ; t = readl(&ep->regs->ep_stat)) {

				/* any preceding dma transfers must finish.
				 * dma handles (M >= N), may empty the queue
				 */
				num_completed = scan_dma_completions(ep);
				if (unlikely(list_empty(&ep->queue) ||
						ep->out_overflow)) {
					req = NULL;
					break;
				}
				req = list_entry(ep->queue.next,
					struct net2280_request, queue);

				/* here either (M < N), a "real" short rx;
				 * or (M == N) and the queue didn't empty
				 */
				if (likely(t & BIT(FIFO_EMPTY))) {
					count = readl(&ep->dma->dmacount);
					count &= DMA_BYTE_COUNT_MASK;
					if (readl(&ep->dma->dmadesc)
							!= req->td_dma)
						req = NULL;
					break;
				}

				/* Escape loop if no dma transfers completed
				 * after few retries.
				 */
				if (num_completed == 0) {
					if (stuck_req == req &&
					    readl(&ep->dma->dmadesc) !=
						  req->td_dma && stuck++ > 5) {
						count = readl(
							&ep->dma->dmacount);
						count &= DMA_BYTE_COUNT_MASK;
						req = NULL;
						ep_dbg(ep->dev, "%s escape stuck %d, count %u\n",
							ep->ep.name, stuck,
							count);
						break;
					} else if (stuck_req != req) {
						stuck_req = req;
						stuck = 0;
					}
				} else {
					stuck_req = NULL;
					stuck = 0;
				}

				udelay(1);
			}

			/* stop DMA, leave ep NAKing */
			writel(BIT(DMA_ABORT), &ep->dma->dmastat);
			spin_stop_dma(ep->dma);

			if (likely(req)) {
				req->td->dmacount = 0;
				t = readl(&ep->regs->ep_avail);
				dma_done(ep, req, count,
					(ep->out_overflow || t)
						? -EOVERFLOW : 0);
			}

			/* also flush to prevent erratum 0106 trouble */
			if (unlikely(ep->out_overflow ||
					(ep->dev->chiprev == 0x0100 &&
					ep->dev->gadget.speed
					== USB_SPEED_FULL))) {
				out_flush(ep);
				ep->out_overflow = 0;
			}

			/* (re)start dma if needed, stop NAKing */
			ep->stopped = stopped;
			if (!list_empty(&ep->queue))
				restart_dma(ep);
		} else
			ep_dbg(ep->dev, "%s dma ep_stat %08x ??\n",
					ep->ep.name, t);
		return;

	/* data packet(s) received (in the fifo, OUT) */
	} else if (t & BIT(DATA_PACKET_RECEIVED_INTERRUPT)) {
		if (read_fifo(ep, req) && ep->num != 0)
			mode = 2;

	/* data packet(s) transmitted (IN) */
	} else if (t & BIT(DATA_PACKET_TRANSMITTED_INTERRUPT)) {
		unsigned	len;

		len = req->req.length - req->req.actual;
		if (len > ep->ep.maxpacket)
			len = ep->ep.maxpacket;
		req->req.actual += len;

		/* if we wrote it all, we're usually done */
		/* send zlps until the status stage */
		if ((req->req.actual == req->req.length) &&
			(!req->req.zero || len != ep->ep.maxpacket) && ep->num)
				mode = 2;

	/* there was nothing to do ...  */
	} else if (mode == 1)
		return;

	/* done */
	if (mode == 2) {
		/* stream endpoints often resubmit/unlink in completion */
		done(ep, req, 0);

		/* maybe advance queue to next request */
		if (ep->num == 0) {
			/* NOTE:  net2280 could let gadget driver start the
			 * status stage later. since not all controllers let
			 * them control that, the api doesn't (yet) allow it.
			 */
			if (!ep->stopped)
				allow_status(ep);
			req = NULL;
		} else {
			if (!list_empty(&ep->queue) && !ep->stopped)
				req = list_entry(ep->queue.next,
					struct net2280_request, queue);
			else
				req = NULL;
			if (req && !ep->is_in)
				stop_out_naking(ep);
		}
	}

	/* is there a buffer for the next packet?
	 * for best streaming performance, make sure there is one.
	 */
	if (req && !ep->stopped) {

		/* load IN fifo with next packet (may be zlp) */
		if (t & BIT(DATA_PACKET_TRANSMITTED_INTERRUPT))
			write_fifo(ep, &req->req);
	}
}

static struct net2280_ep *get_ep_by_addr(struct net2280 *dev, u16 wIndex)
{
	struct net2280_ep	*ep;

	if ((wIndex & USB_ENDPOINT_NUMBER_MASK) == 0)
		return &dev->ep[0];
	list_for_each_entry(ep, &dev->gadget.ep_list, ep.ep_list) {
		u8	bEndpointAddress;

		if (!ep->desc)
			continue;
		bEndpointAddress = ep->desc->bEndpointAddress;
		if ((wIndex ^ bEndpointAddress) & USB_DIR_IN)
			continue;
		if ((wIndex & 0x0f) == (bEndpointAddress & 0x0f))
			return ep;
	}
	return NULL;
}

static void defect7374_workaround(struct net2280 *dev, struct usb_ctrlrequest r)
{
	u32 scratch, fsmvalue;
	u32 ack_wait_timeout, state;

	/* Workaround for Defect 7374 (U1/U2 erroneously rejected): */
	scratch = get_idx_reg(dev->regs, SCRATCH);
	fsmvalue = scratch & (0xf << DEFECT7374_FSM_FIELD);
	scratch &= ~(0xf << DEFECT7374_FSM_FIELD);

	if (!((fsmvalue == DEFECT7374_FSM_WAITING_FOR_CONTROL_READ) &&
				(r.bRequestType & USB_DIR_IN)))
		return;

	/* This is the first Control Read for this connection: */
	if (!(readl(&dev->usb->usbstat) & BIT(SUPER_SPEED_MODE))) {
		/*
		 * Connection is NOT SS:
		 * - Connection must be FS or HS.
		 * - This FSM state should allow workaround software to
		 * run after the next USB connection.
		 */
		scratch |= DEFECT7374_FSM_NON_SS_CONTROL_READ;
		dev->bug7734_patched = 1;
		goto restore_data_eps;
	}

	/* Connection is SS: */
	for (ack_wait_timeout = 0;
			ack_wait_timeout < DEFECT_7374_NUMBEROF_MAX_WAIT_LOOPS;
			ack_wait_timeout++) {

		state =	readl(&dev->plregs->pl_ep_status_1)
			& (0xff << STATE);
		if ((state >= (ACK_GOOD_NORMAL << STATE)) &&
			(state <= (ACK_GOOD_MORE_ACKS_TO_COME << STATE))) {
			scratch |= DEFECT7374_FSM_SS_CONTROL_READ;
			dev->bug7734_patched = 1;
			break;
		}

		/*
		 * We have not yet received host's Data Phase ACK
		 * - Wait and try again.
		 */
		udelay(DEFECT_7374_PROCESSOR_WAIT_TIME);

		continue;
	}


	if (ack_wait_timeout >= DEFECT_7374_NUMBEROF_MAX_WAIT_LOOPS) {
		ep_err(dev, "FAIL: Defect 7374 workaround waited but failed "
		"to detect SS host's data phase ACK.");
		ep_err(dev, "PL_EP_STATUS_1(23:16):.Expected from 0x11 to 0x16"
		"got 0x%2.2x.\n", state >> STATE);
	} else {
		ep_warn(dev, "INFO: Defect 7374 workaround waited about\n"
		"%duSec for Control Read Data Phase ACK\n",
			DEFECT_7374_PROCESSOR_WAIT_TIME * ack_wait_timeout);
	}

restore_data_eps:
	/*
	 * Restore data EPs to their pre-workaround settings (disabled,
	 * initialized, and other details).
	 */
	defect7374_disable_data_eps(dev);

	set_idx_reg(dev->regs, SCRATCH, scratch);

	return;
}

static void ep_clear_seqnum(struct net2280_ep *ep)
{
	struct net2280 *dev = ep->dev;
	u32 val;
	static const u32 ep_pl[9] = { 0, 3, 4, 7, 8, 2, 5, 6, 9 };

	val = readl(&dev->plregs->pl_ep_ctrl) & ~0x1f;
	val |= ep_pl[ep->num];
	writel(val, &dev->plregs->pl_ep_ctrl);
	val |= BIT(SEQUENCE_NUMBER_RESET);
	writel(val, &dev->plregs->pl_ep_ctrl);

	return;
}

static void handle_stat0_irqs_superspeed(struct net2280 *dev,
		struct net2280_ep *ep, struct usb_ctrlrequest r)
{
	int tmp = 0;

#define	w_value		le16_to_cpu(r.wValue)
#define	w_index		le16_to_cpu(r.wIndex)
#define	w_length	le16_to_cpu(r.wLength)

	switch (r.bRequest) {
		struct net2280_ep *e;
		u16 status;

	case USB_REQ_SET_CONFIGURATION:
		dev->addressed_state = !w_value;
		goto usb3_delegate;

	case USB_REQ_GET_STATUS:
		switch (r.bRequestType) {
		case (USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE):
			status = dev->wakeup_enable ? 0x02 : 0x00;
			if (dev->gadget.is_selfpowered)
				status |= BIT(0);
			status |= (dev->u1_enable << 2 | dev->u2_enable << 3 |
							dev->ltm_enable << 4);
			writel(0, &dev->epregs[0].ep_irqenb);
			set_fifo_bytecount(ep, sizeof(status));
			writel((__force u32) status, &dev->epregs[0].ep_data);
			allow_status_338x(ep);
			break;

		case (USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_ENDPOINT):
			e = get_ep_by_addr(dev, w_index);
			if (!e)
				goto do_stall3;
			status = readl(&e->regs->ep_rsp) &
						BIT(CLEAR_ENDPOINT_HALT);
			writel(0, &dev->epregs[0].ep_irqenb);
			set_fifo_bytecount(ep, sizeof(status));
			writel((__force u32) status, &dev->epregs[0].ep_data);
			allow_status_338x(ep);
			break;

		default:
			goto usb3_delegate;
		}
		break;

	case USB_REQ_CLEAR_FEATURE:
		switch (r.bRequestType) {
		case (USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE):
			if (!dev->addressed_state) {
				switch (w_value) {
				case USB_DEVICE_U1_ENABLE:
					dev->u1_enable = 0;
					writel(readl(&dev->usb_ext->usbctl2) &
						~BIT(U1_ENABLE),
						&dev->usb_ext->usbctl2);
					allow_status_338x(ep);
					goto next_endpoints3;

				case USB_DEVICE_U2_ENABLE:
					dev->u2_enable = 0;
					writel(readl(&dev->usb_ext->usbctl2) &
						~BIT(U2_ENABLE),
						&dev->usb_ext->usbctl2);
					allow_status_338x(ep);
					goto next_endpoints3;

				case USB_DEVICE_LTM_ENABLE:
					dev->ltm_enable = 0;
					writel(readl(&dev->usb_ext->usbctl2) &
						~BIT(LTM_ENABLE),
						&dev->usb_ext->usbctl2);
					allow_status_338x(ep);
					goto next_endpoints3;

				default:
					break;
				}
			}
			if (w_value == USB_DEVICE_REMOTE_WAKEUP) {
				dev->wakeup_enable = 0;
				writel(readl(&dev->usb->usbctl) &
					~BIT(DEVICE_REMOTE_WAKEUP_ENABLE),
					&dev->usb->usbctl);
				allow_status_338x(ep);
				break;
			}
			goto usb3_delegate;

		case (USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_ENDPOINT):
			e = get_ep_by_addr(dev,	w_index);
			if (!e)
				goto do_stall3;
			if (w_value != USB_ENDPOINT_HALT)
				goto do_stall3;
			ep_vdbg(dev, "%s clear halt\n", e->ep.name);
			/*
			 * Workaround for SS SeqNum not cleared via
			 * Endpoint Halt (Clear) bit. select endpoint
			 */
			ep_clear_seqnum(e);
			clear_halt(e);
			if (!list_empty(&e->queue) && e->td_dma)
				restart_dma(e);
			allow_status(ep);
			ep->stopped = 1;
			break;

		default:
			goto usb3_delegate;
		}
		break;
	case USB_REQ_SET_FEATURE:
		switch (r.bRequestType) {
		case (USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE):
			if (!dev->addressed_state) {
				switch (w_value) {
				case USB_DEVICE_U1_ENABLE:
					dev->u1_enable = 1;
					writel(readl(&dev->usb_ext->usbctl2) |
						BIT(U1_ENABLE),
						&dev->usb_ext->usbctl2);
					allow_status_338x(ep);
					goto next_endpoints3;

				case USB_DEVICE_U2_ENABLE:
					dev->u2_enable = 1;
					writel(readl(&dev->usb_ext->usbctl2) |
						BIT(U2_ENABLE),
						&dev->usb_ext->usbctl2);
					allow_status_338x(ep);
					goto next_endpoints3;

				case USB_DEVICE_LTM_ENABLE:
					dev->ltm_enable = 1;
					writel(readl(&dev->usb_ext->usbctl2) |
						BIT(LTM_ENABLE),
						&dev->usb_ext->usbctl2);
					allow_status_338x(ep);
					goto next_endpoints3;
				default:
					break;
				}
			}

			if (w_value == USB_DEVICE_REMOTE_WAKEUP) {
				dev->wakeup_enable = 1;
				writel(readl(&dev->usb->usbctl) |
					BIT(DEVICE_REMOTE_WAKEUP_ENABLE),
					&dev->usb->usbctl);
				allow_status_338x(ep);
				break;
			}
			goto usb3_delegate;

		case (USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_ENDPOINT):
			e = get_ep_by_addr(dev,	w_index);
			if (!e || (w_value != USB_ENDPOINT_HALT))
				goto do_stall3;
			ep->stopped = 1;
			if (ep->num == 0)
				ep->dev->protocol_stall = 1;
			else {
				if (ep->dma)
					abort_dma(ep);
				set_halt(ep);
			}
			allow_status_338x(ep);
			break;

		default:
			goto usb3_delegate;
		}

		break;
	default:

usb3_delegate:
		ep_vdbg(dev, "setup %02x.%02x v%04x i%04x l%04x ep_cfg %08x\n",
				r.bRequestType, r.bRequest,
				w_value, w_index, w_length,
				readl(&ep->cfg->ep_cfg));

		ep->responded = 0;
		spin_unlock(&dev->lock);
		tmp = dev->driver->setup(&dev->gadget, &r);
		spin_lock(&dev->lock);
	}
do_stall3:
	if (tmp < 0) {
		ep_vdbg(dev, "req %02x.%02x protocol STALL; stat %d\n",
				r.bRequestType, r.bRequest, tmp);
		dev->protocol_stall = 1;
		/* TD 9.9 Halt Endpoint test. TD 9.22 Set feature test */
		set_halt(ep);
	}

next_endpoints3:

#undef	w_value
#undef	w_index
#undef	w_length

	return;
}

static void usb338x_handle_ep_intr(struct net2280 *dev, u32 stat0)
{
	u32 index;
	u32 bit;

	for (index = 0; index < ARRAY_SIZE(ep_bit); index++) {
		bit = BIT(ep_bit[index]);

		if (!stat0)
			break;

		if (!(stat0 & bit))
			continue;

		stat0 &= ~bit;

		handle_ep_small(&dev->ep[index]);
	}
}

static void handle_stat0_irqs(struct net2280 *dev, u32 stat)
{
	struct net2280_ep	*ep;
	u32			num, scratch;

	/* most of these don't need individual acks */
	stat &= ~BIT(INTA_ASSERTED);
	if (!stat)
		return;
	/* ep_dbg(dev, "irqstat0 %04x\n", stat); */

	/* starting a control request? */
	if (unlikely(stat & BIT(SETUP_PACKET_INTERRUPT))) {
		union {
			u32			raw[2];
			struct usb_ctrlrequest	r;
		} u;
		int				tmp;
		struct net2280_request		*req;

		if (dev->gadget.speed == USB_SPEED_UNKNOWN) {
			u32 val = readl(&dev->usb->usbstat);
			if (val & BIT(SUPER_SPEED)) {
				dev->gadget.speed = USB_SPEED_SUPER;
				usb_ep_set_maxpacket_limit(&dev->ep[0].ep,
						EP0_SS_MAX_PACKET_SIZE);
			} else if (val & BIT(HIGH_SPEED)) {
				dev->gadget.speed = USB_SPEED_HIGH;
				usb_ep_set_maxpacket_limit(&dev->ep[0].ep,
						EP0_HS_MAX_PACKET_SIZE);
			} else {
				dev->gadget.speed = USB_SPEED_FULL;
				usb_ep_set_maxpacket_limit(&dev->ep[0].ep,
						EP0_HS_MAX_PACKET_SIZE);
			}
			net2280_led_speed(dev, dev->gadget.speed);
			ep_dbg(dev, "%s\n",
					usb_speed_string(dev->gadget.speed));
		}

		ep = &dev->ep[0];
		ep->irqs++;

		/* make sure any leftover request state is cleared */
		stat &= ~BIT(ENDPOINT_0_INTERRUPT);
		while (!list_empty(&ep->queue)) {
			req = list_entry(ep->queue.next,
					struct net2280_request, queue);
			done(ep, req, (req->req.actual == req->req.length)
						? 0 : -EPROTO);
		}
		ep->stopped = 0;
		dev->protocol_stall = 0;
		if (!(dev->quirks & PLX_PCIE)) {
			if (ep->dev->quirks & PLX_2280)
				tmp = BIT(FIFO_OVERFLOW) |
				    BIT(FIFO_UNDERFLOW);
			else
				tmp = 0;

			writel(tmp | BIT(TIMEOUT) |
				   BIT(USB_STALL_SENT) |
				   BIT(USB_IN_NAK_SENT) |
				   BIT(USB_IN_ACK_RCVD) |
				   BIT(USB_OUT_PING_NAK_SENT) |
				   BIT(USB_OUT_ACK_SENT) |
				   BIT(SHORT_PACKET_OUT_DONE_INTERRUPT) |
				   BIT(SHORT_PACKET_TRANSFERRED_INTERRUPT) |
				   BIT(DATA_PACKET_RECEIVED_INTERRUPT) |
				   BIT(DATA_PACKET_TRANSMITTED_INTERRUPT) |
				   BIT(DATA_OUT_PING_TOKEN_INTERRUPT) |
				   BIT(DATA_IN_TOKEN_INTERRUPT),
				   &ep->regs->ep_stat);
		}
		u.raw[0] = readl(&dev->usb->setup0123);
		u.raw[1] = readl(&dev->usb->setup4567);

		cpu_to_le32s(&u.raw[0]);
		cpu_to_le32s(&u.raw[1]);

		if ((dev->quirks & PLX_PCIE) && !dev->bug7734_patched)
			defect7374_workaround(dev, u.r);

		tmp = 0;

#define	w_value		le16_to_cpu(u.r.wValue)
#define	w_index		le16_to_cpu(u.r.wIndex)
#define	w_length	le16_to_cpu(u.r.wLength)

		/* ack the irq */
		writel(BIT(SETUP_PACKET_INTERRUPT), &dev->regs->irqstat0);
		stat ^= BIT(SETUP_PACKET_INTERRUPT);

		/* watch control traffic at the token level, and force
		 * synchronization before letting the status stage happen.
		 * FIXME ignore tokens we'll NAK, until driver responds.
		 * that'll mean a lot less irqs for some drivers.
		 */
		ep->is_in = (u.r.bRequestType & USB_DIR_IN) != 0;
		if (ep->is_in) {
			scratch = BIT(DATA_PACKET_TRANSMITTED_INTERRUPT) |
				BIT(DATA_OUT_PING_TOKEN_INTERRUPT) |
				BIT(DATA_IN_TOKEN_INTERRUPT);
			stop_out_naking(ep);
		} else
			scratch = BIT(DATA_PACKET_RECEIVED_INTERRUPT) |
				BIT(DATA_OUT_PING_TOKEN_INTERRUPT) |
				BIT(DATA_IN_TOKEN_INTERRUPT);
		writel(scratch, &dev->epregs[0].ep_irqenb);

		/* we made the hardware handle most lowlevel requests;
		 * everything else goes uplevel to the gadget code.
		 */
		ep->responded = 1;

		if (dev->gadget.speed == USB_SPEED_SUPER) {
			handle_stat0_irqs_superspeed(dev, ep, u.r);
			goto next_endpoints;
		}

		switch (u.r.bRequest) {
		case USB_REQ_GET_STATUS: {
			struct net2280_ep	*e;
			__le32			status;

			/* hw handles device and interface status */
			if (u.r.bRequestType != (USB_DIR_IN|USB_RECIP_ENDPOINT))
				goto delegate;
			e = get_ep_by_addr(dev, w_index);
			if (!e || w_length > 2)
				goto do_stall;

			if (readl(&e->regs->ep_rsp) & BIT(SET_ENDPOINT_HALT))
				status = cpu_to_le32(1);
			else
				status = cpu_to_le32(0);

			/* don't bother with a request object! */
			writel(0, &dev->epregs[0].ep_irqenb);
			set_fifo_bytecount(ep, w_length);
			writel((__force u32)status, &dev->epregs[0].ep_data);
			allow_status(ep);
			ep_vdbg(dev, "%s stat %02x\n", ep->ep.name, status);
			goto next_endpoints;
			}
			break;
		case USB_REQ_CLEAR_FEATURE: {
			struct net2280_ep	*e;

			/* hw handles device features */
			if (u.r.bRequestType != USB_RECIP_ENDPOINT)
				goto delegate;
			if (w_value != USB_ENDPOINT_HALT || w_length != 0)
				goto do_stall;
			e = get_ep_by_addr(dev, w_index);
			if (!e)
				goto do_stall;
			if (e->wedged) {
				ep_vdbg(dev, "%s wedged, halt not cleared\n",
						ep->ep.name);
			} else {
				ep_vdbg(dev, "%s clear halt\n", e->ep.name);
				clear_halt(e);
				if ((ep->dev->quirks & PLX_PCIE) &&
					!list_empty(&e->queue) && e->td_dma)
						restart_dma(e);
			}
			allow_status(ep);
			goto next_endpoints;
			}
			break;
		case USB_REQ_SET_FEATURE: {
			struct net2280_ep	*e;

			/* hw handles device features */
			if (u.r.bRequestType != USB_RECIP_ENDPOINT)
				goto delegate;
			if (w_value != USB_ENDPOINT_HALT || w_length != 0)
				goto do_stall;
			e = get_ep_by_addr(dev, w_index);
			if (!e)
				goto do_stall;
			if (e->ep.name == ep0name)
				goto do_stall;
			set_halt(e);
			if ((dev->quirks & PLX_PCIE) && e->dma)
				abort_dma(e);
			allow_status(ep);
			ep_vdbg(dev, "%s set halt\n", ep->ep.name);
			goto next_endpoints;
			}
			break;
		default:
delegate:
			ep_vdbg(dev, "setup %02x.%02x v%04x i%04x l%04x "
				"ep_cfg %08x\n",
				u.r.bRequestType, u.r.bRequest,
				w_value, w_index, w_length,
				readl(&ep->cfg->ep_cfg));
			ep->responded = 0;
			spin_unlock(&dev->lock);
			tmp = dev->driver->setup(&dev->gadget, &u.r);
			spin_lock(&dev->lock);
		}

		/* stall ep0 on error */
		if (tmp < 0) {
do_stall:
			ep_vdbg(dev, "req %02x.%02x protocol STALL; stat %d\n",
					u.r.bRequestType, u.r.bRequest, tmp);
			dev->protocol_stall = 1;
		}

		/* some in/out token irq should follow; maybe stall then.
		 * driver must queue a request (even zlp) or halt ep0
		 * before the host times out.
		 */
	}

#undef	w_value
#undef	w_index
#undef	w_length

next_endpoints:
	if ((dev->quirks & PLX_PCIE) && dev->enhanced_mode) {
		u32 mask = (BIT(ENDPOINT_0_INTERRUPT) |
			USB3380_IRQSTAT0_EP_INTR_MASK_IN |
			USB3380_IRQSTAT0_EP_INTR_MASK_OUT);

		if (stat & mask) {
			usb338x_handle_ep_intr(dev, stat & mask);
			stat &= ~mask;
		}
	} else {
		/* endpoint data irq ? */
		scratch = stat & 0x7f;
		stat &= ~0x7f;
		for (num = 0; scratch; num++) {
			u32		t;

			/* do this endpoint's FIFO and queue need tending? */
			t = BIT(num);
			if ((scratch & t) == 0)
				continue;
			scratch ^= t;

			ep = &dev->ep[num];
			handle_ep_small(ep);
		}
	}

	if (stat)
		ep_dbg(dev, "unhandled irqstat0 %08x\n", stat);
}

#define DMA_INTERRUPTS (BIT(DMA_D_INTERRUPT) | \
		BIT(DMA_C_INTERRUPT) | \
		BIT(DMA_B_INTERRUPT) | \
		BIT(DMA_A_INTERRUPT))
#define	PCI_ERROR_INTERRUPTS ( \
		BIT(PCI_MASTER_ABORT_RECEIVED_INTERRUPT) | \
		BIT(PCI_TARGET_ABORT_RECEIVED_INTERRUPT) | \
		BIT(PCI_RETRY_ABORT_INTERRUPT))

static void handle_stat1_irqs(struct net2280 *dev, u32 stat)
__releases(dev->lock)
__acquires(dev->lock)
{
	struct net2280_ep	*ep;
	u32			tmp, num, mask, scratch;

	/* after disconnect there's nothing else to do! */
	tmp = BIT(VBUS_INTERRUPT) | BIT(ROOT_PORT_RESET_INTERRUPT);
	mask = BIT(SUPER_SPEED) | BIT(HIGH_SPEED) | BIT(FULL_SPEED);

	/* VBUS disconnect is indicated by VBUS_PIN and VBUS_INTERRUPT set.
	 * Root Port Reset is indicated by ROOT_PORT_RESET_INTERRUPT set and
	 * both HIGH_SPEED and FULL_SPEED clear (as ROOT_PORT_RESET_INTERRUPT
	 * only indicates a change in the reset state).
	 */
	if (stat & tmp) {
		bool	reset = false;
		bool	disconnect = false;

		/*
		 * Ignore disconnects and resets if the speed hasn't been set.
		 * VBUS can bounce and there's always an initial reset.
		 */
		writel(tmp, &dev->regs->irqstat1);
		if (dev->gadget.speed != USB_SPEED_UNKNOWN) {
			if ((stat & BIT(VBUS_INTERRUPT)) &&
					(readl(&dev->usb->usbctl) &
						BIT(VBUS_PIN)) == 0) {
				disconnect = true;
				ep_dbg(dev, "disconnect %s\n",
						dev->driver->driver.name);
			} else if ((stat & BIT(ROOT_PORT_RESET_INTERRUPT)) &&
					(readl(&dev->usb->usbstat) & mask)
						== 0) {
				reset = true;
				ep_dbg(dev, "reset %s\n",
						dev->driver->driver.name);
			}

			if (disconnect || reset) {
				stop_activity(dev, dev->driver);
				ep0_start(dev);
				spin_unlock(&dev->lock);
				if (reset)
					usb_gadget_udc_reset
						(&dev->gadget, dev->driver);
				else
					(dev->driver->disconnect)
						(&dev->gadget);
				spin_lock(&dev->lock);
				return;
			}
		}
		stat &= ~tmp;

		/* vBUS can bounce ... one of many reasons to ignore the
		 * notion of hotplug events on bus connect/disconnect!
		 */
		if (!stat)
			return;
	}

	/* NOTE: chip stays in PCI D0 state for now, but it could
	 * enter D1 to save more power
	 */
	tmp = BIT(SUSPEND_REQUEST_CHANGE_INTERRUPT);
	if (stat & tmp) {
		writel(tmp, &dev->regs->irqstat1);
		spin_unlock(&dev->lock);
		if (stat & BIT(SUSPEND_REQUEST_INTERRUPT)) {
			if (dev->driver->suspend)
				dev->driver->suspend(&dev->gadget);
			if (!enable_suspend)
				stat &= ~BIT(SUSPEND_REQUEST_INTERRUPT);
		} else {
			if (dev->driver->resume)
				dev->driver->resume(&dev->gadget);
			/* at high speed, note erratum 0133 */
		}
		spin_lock(&dev->lock);
		stat &= ~tmp;
	}

	/* clear any other status/irqs */
	if (stat)
		writel(stat, &dev->regs->irqstat1);

	/* some status we can just ignore */
	if (dev->quirks & PLX_2280)
		stat &= ~(BIT(CONTROL_STATUS_INTERRUPT) |
			  BIT(SUSPEND_REQUEST_INTERRUPT) |
			  BIT(RESUME_INTERRUPT) |
			  BIT(SOF_INTERRUPT));
	else
		stat &= ~(BIT(CONTROL_STATUS_INTERRUPT) |
			  BIT(RESUME_INTERRUPT) |
			  BIT(SOF_DOWN_INTERRUPT) |
			  BIT(SOF_INTERRUPT));

	if (!stat)
		return;
	/* ep_dbg(dev, "irqstat1 %08x\n", stat);*/

	/* DMA status, for ep-{a,b,c,d} */
	scratch = stat & DMA_INTERRUPTS;
	stat &= ~DMA_INTERRUPTS;
	scratch >>= 9;
	for (num = 0; scratch; num++) {
		struct net2280_dma_regs	__iomem *dma;

		tmp = BIT(num);
		if ((tmp & scratch) == 0)
			continue;
		scratch ^= tmp;

		ep = &dev->ep[num + 1];
		dma = ep->dma;

		if (!dma)
			continue;

		/* clear ep's dma status */
		tmp = readl(&dma->dmastat);
		writel(tmp, &dma->dmastat);

		/* dma sync*/
		if (dev->quirks & PLX_PCIE) {
			u32 r_dmacount = readl(&dma->dmacount);
			if (!ep->is_in &&  (r_dmacount & 0x00FFFFFF) &&
			    (tmp & BIT(DMA_TRANSACTION_DONE_INTERRUPT)))
				continue;
		}

		if (!(tmp & BIT(DMA_TRANSACTION_DONE_INTERRUPT))) {
			ep_dbg(ep->dev, "%s no xact done? %08x\n",
				ep->ep.name, tmp);
			continue;
		}
		stop_dma(ep->dma);

		/* OUT transfers terminate when the data from the
		 * host is in our memory.  Process whatever's done.
		 * On this path, we know transfer's last packet wasn't
		 * less than req->length. NAK_OUT_PACKETS may be set,
		 * or the FIFO may already be holding new packets.
		 *
		 * IN transfers can linger in the FIFO for a very
		 * long time ... we ignore that for now, accounting
		 * precisely (like PIO does) needs per-packet irqs
		 */
		scan_dma_completions(ep);

		/* disable dma on inactive queues; else maybe restart */
		if (!list_empty(&ep->queue)) {
			tmp = readl(&dma->dmactl);
			restart_dma(ep);
		}
		ep->irqs++;
	}

	/* NOTE:  there are other PCI errors we might usefully notice.
	 * if they appear very often, here's where to try recovering.
	 */
	if (stat & PCI_ERROR_INTERRUPTS) {
		ep_err(dev, "pci dma error; stat %08x\n", stat);
		stat &= ~PCI_ERROR_INTERRUPTS;
		/* these are fatal errors, but "maybe" they won't
		 * happen again ...
		 */
		stop_activity(dev, dev->driver);
		ep0_start(dev);
		stat = 0;
	}

	if (stat)
		ep_dbg(dev, "unhandled irqstat1 %08x\n", stat);
}

static irqreturn_t net2280_irq(int irq, void *_dev)
{
	struct net2280		*dev = _dev;

	/* shared interrupt, not ours */
	if ((dev->quirks & PLX_LEGACY) &&
		(!(readl(&dev->regs->irqstat0) & BIT(INTA_ASSERTED))))
		return IRQ_NONE;

	spin_lock(&dev->lock);

	/* handle disconnect, dma, and more */
	handle_stat1_irqs(dev, readl(&dev->regs->irqstat1));

	/* control requests and PIO */
	handle_stat0_irqs(dev, readl(&dev->regs->irqstat0));

	if (dev->quirks & PLX_PCIE) {
		/* re-enable interrupt to trigger any possible new interrupt */
		u32 pciirqenb1 = readl(&dev->regs->pciirqenb1);
		writel(pciirqenb1 & 0x7FFFFFFF, &dev->regs->pciirqenb1);
		writel(pciirqenb1, &dev->regs->pciirqenb1);
	}

	spin_unlock(&dev->lock);

	return IRQ_HANDLED;
}

/*-------------------------------------------------------------------------*/

static void gadget_release(struct device *_dev)
{
	struct net2280	*dev = dev_get_drvdata(_dev);

	kfree(dev);
}

/* tear down the binding between this driver and the pci device */

static void net2280_remove(struct pci_dev *pdev)
{
	struct net2280		*dev = pci_get_drvdata(pdev);

	usb_del_gadget_udc(&dev->gadget);

	BUG_ON(dev->driver);

	/* then clean up the resources we allocated during probe() */
	if (dev->requests) {
		int		i;
		for (i = 1; i < 5; i++) {
			if (!dev->ep[i].dummy)
				continue;
			dma_pool_free(dev->requests, dev->ep[i].dummy,
					dev->ep[i].td_dma);
		}
		dma_pool_destroy(dev->requests);
	}
	if (dev->got_irq)
		free_irq(pdev->irq, dev);
	if (dev->quirks & PLX_PCIE)
		pci_disable_msi(pdev);
	if (dev->regs) {
		net2280_led_shutdown(dev);
		iounmap(dev->regs);
	}
	if (dev->region)
		release_mem_region(pci_resource_start(pdev, 0),
				pci_resource_len(pdev, 0));
	if (dev->enabled)
		pci_disable_device(pdev);
	device_remove_file(&pdev->dev, &dev_attr_registers);

	ep_info(dev, "unbind\n");
}

/* wrap this driver around the specified device, but
 * don't respond over USB until a gadget driver binds to us.
 */

static int net2280_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct net2280		*dev;
	unsigned long		resource, len;
	void			__iomem *base = NULL;
	int			retval, i;

	/* alloc, and start init */
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (dev == NULL) {
		retval = -ENOMEM;
		goto done;
	}

	pci_set_drvdata(pdev, dev);
	spin_lock_init(&dev->lock);
	dev->quirks = id->driver_data;
	dev->pdev = pdev;
	dev->gadget.ops = &net2280_ops;
	dev->gadget.max_speed = (dev->quirks & PLX_SUPERSPEED) ?
				USB_SPEED_SUPER : USB_SPEED_HIGH;

	/* the "gadget" abstracts/virtualizes the controller */
	dev->gadget.name = driver_name;

	/* now all the pci goodies ... */
	if (pci_enable_device(pdev) < 0) {
		retval = -ENODEV;
		goto done;
	}
	dev->enabled = 1;

	/* BAR 0 holds all the registers
	 * BAR 1 is 8051 memory; unused here (note erratum 0103)
	 * BAR 2 is fifo memory; unused here
	 */
	resource = pci_resource_start(pdev, 0);
	len = pci_resource_len(pdev, 0);
	if (!request_mem_region(resource, len, driver_name)) {
		ep_dbg(dev, "controller already in use\n");
		retval = -EBUSY;
		goto done;
	}
	dev->region = 1;

	/* FIXME provide firmware download interface to put
	 * 8051 code into the chip, e.g. to turn on PCI PM.
	 */

	base = ioremap_nocache(resource, len);
	if (base == NULL) {
		ep_dbg(dev, "can't map memory\n");
		retval = -EFAULT;
		goto done;
	}
	dev->regs = (struct net2280_regs __iomem *) base;
	dev->usb = (struct net2280_usb_regs __iomem *) (base + 0x0080);
	dev->pci = (struct net2280_pci_regs __iomem *) (base + 0x0100);
	dev->dma = (struct net2280_dma_regs __iomem *) (base + 0x0180);
	dev->dep = (struct net2280_dep_regs __iomem *) (base + 0x0200);
	dev->epregs = (struct net2280_ep_regs __iomem *) (base + 0x0300);

	if (dev->quirks & PLX_PCIE) {
		u32 fsmvalue;
		u32 usbstat;
		dev->usb_ext = (struct usb338x_usb_ext_regs __iomem *)
							(base + 0x00b4);
		dev->llregs = (struct usb338x_ll_regs __iomem *)
							(base + 0x0700);
		dev->ll_lfps_regs = (struct usb338x_ll_lfps_regs __iomem *)
							(base + 0x0748);
		dev->ll_tsn_regs = (struct usb338x_ll_tsn_regs __iomem *)
							(base + 0x077c);
		dev->ll_chicken_reg = (struct usb338x_ll_chi_regs __iomem *)
							(base + 0x079c);
		dev->plregs = (struct usb338x_pl_regs __iomem *)
							(base + 0x0800);
		usbstat = readl(&dev->usb->usbstat);
		dev->enhanced_mode = !!(usbstat & BIT(11));
		dev->n_ep = (dev->enhanced_mode) ? 9 : 5;
		/* put into initial config, link up all endpoints */
		fsmvalue = get_idx_reg(dev->regs, SCRATCH) &
					(0xf << DEFECT7374_FSM_FIELD);
		/* See if firmware needs to set up for workaround: */
		if (fsmvalue == DEFECT7374_FSM_SS_CONTROL_READ) {
			dev->bug7734_patched = 1;
			writel(0, &dev->usb->usbctl);
		} else
			dev->bug7734_patched = 0;
	} else {
		dev->enhanced_mode = 0;
		dev->n_ep = 7;
		/* put into initial config, link up all endpoints */
		writel(0, &dev->usb->usbctl);
	}

	usb_reset(dev);
	usb_reinit(dev);

	/* irq setup after old hardware is cleaned up */
	if (!pdev->irq) {
		ep_err(dev, "No IRQ.  Check PCI setup!\n");
		retval = -ENODEV;
		goto done;
	}

	if (dev->quirks & PLX_PCIE)
		if (pci_enable_msi(pdev))
			ep_err(dev, "Failed to enable MSI mode\n");

	if (request_irq(pdev->irq, net2280_irq, IRQF_SHARED,
							driver_name, dev)) {
		ep_err(dev, "request interrupt %d failed\n", pdev->irq);
		retval = -EBUSY;
		goto done;
	}
	dev->got_irq = 1;

	/* DMA setup */
	/* NOTE:  we know only the 32 LSBs of dma addresses may be nonzero */
	dev->requests = dma_pool_create("requests", &pdev->dev,
		sizeof(struct net2280_dma),
		0 /* no alignment requirements */,
		0 /* or page-crossing issues */);
	if (!dev->requests) {
		ep_dbg(dev, "can't get request pool\n");
		retval = -ENOMEM;
		goto done;
	}
	for (i = 1; i < 5; i++) {
		struct net2280_dma	*td;

		td = dma_pool_alloc(dev->requests, GFP_KERNEL,
				&dev->ep[i].td_dma);
		if (!td) {
			ep_dbg(dev, "can't get dummy %d\n", i);
			retval = -ENOMEM;
			goto done;
		}
		td->dmacount = 0;	/* not VALID */
		td->dmadesc = td->dmaaddr;
		dev->ep[i].dummy = td;
	}

	/* enable lower-overhead pci memory bursts during DMA */
	if (dev->quirks & PLX_LEGACY)
		writel(BIT(DMA_MEMORY_WRITE_AND_INVALIDATE_ENABLE) |
			/*
			 * 256 write retries may not be enough...
			   BIT(PCI_RETRY_ABORT_ENABLE) |
			*/
			BIT(DMA_READ_MULTIPLE_ENABLE) |
			BIT(DMA_READ_LINE_ENABLE),
			&dev->pci->pcimstctl);
	/* erratum 0115 shouldn't appear: Linux inits PCI_LATENCY_TIMER */
	pci_set_master(pdev);
	pci_try_set_mwi(pdev);

	/* ... also flushes any posted pci writes */
	dev->chiprev = get_idx_reg(dev->regs, REG_CHIPREV) & 0xffff;

	/* done */
	ep_info(dev, "%s\n", driver_desc);
	ep_info(dev, "irq %d, pci mem %p, chip rev %04x\n",
			pdev->irq, base, dev->chiprev);
	ep_info(dev, "version: " DRIVER_VERSION "; %s\n",
		dev->enhanced_mode ? "enhanced mode" : "legacy mode");
	retval = device_create_file(&pdev->dev, &dev_attr_registers);
	if (retval)
		goto done;

	retval = usb_add_gadget_udc_release(&pdev->dev, &dev->gadget,
			gadget_release);
	if (retval)
		goto done;
	return 0;

done:
	if (dev) {
		net2280_remove(pdev);
		kfree(dev);
	}
	return retval;
}

/* make sure the board is quiescent; otherwise it will continue
 * generating IRQs across the upcoming reboot.
 */

static void net2280_shutdown(struct pci_dev *pdev)
{
	struct net2280		*dev = pci_get_drvdata(pdev);

	/* disable IRQs */
	writel(0, &dev->regs->pciirqenb0);
	writel(0, &dev->regs->pciirqenb1);

	/* disable the pullup so the host will think we're gone */
	writel(0, &dev->usb->usbctl);

}


/*-------------------------------------------------------------------------*/

static const struct pci_device_id pci_ids[] = { {
	.class =	PCI_CLASS_SERIAL_USB_DEVICE,
	.class_mask =	~0,
	.vendor =	PCI_VENDOR_ID_PLX_LEGACY,
	.device =	0x2280,
	.subvendor =	PCI_ANY_ID,
	.subdevice =	PCI_ANY_ID,
	.driver_data =	PLX_LEGACY | PLX_2280,
	}, {
	.class =	PCI_CLASS_SERIAL_USB_DEVICE,
	.class_mask =	~0,
	.vendor =	PCI_VENDOR_ID_PLX_LEGACY,
	.device =	0x2282,
	.subvendor =	PCI_ANY_ID,
	.subdevice =	PCI_ANY_ID,
	.driver_data =	PLX_LEGACY,
	},
	{
	.class =	PCI_CLASS_SERIAL_USB_DEVICE,
	.class_mask =	~0,
	.vendor =	PCI_VENDOR_ID_PLX,
	.device =	0x2380,
	.subvendor =	PCI_ANY_ID,
	.subdevice =	PCI_ANY_ID,
	.driver_data =	PLX_PCIE,
	 },
	{
	.class =	((PCI_CLASS_SERIAL_USB << 8) | 0xfe),
	.class_mask =	~0,
	.vendor =	PCI_VENDOR_ID_PLX,
	.device =	0x3380,
	.subvendor =	PCI_ANY_ID,
	.subdevice =	PCI_ANY_ID,
	.driver_data =	PLX_PCIE | PLX_SUPERSPEED,
	 },
	{
	.class =	PCI_CLASS_SERIAL_USB_DEVICE,
	.class_mask =	~0,
	.vendor =	PCI_VENDOR_ID_PLX,
	.device =	0x3382,
	.subvendor =	PCI_ANY_ID,
	.subdevice =	PCI_ANY_ID,
	.driver_data =	PLX_PCIE | PLX_SUPERSPEED,
	 },
{ /* end: all zeroes */ }
};
MODULE_DEVICE_TABLE(pci, pci_ids);

/* pci driver glue; this is a "new style" PCI driver module */
static struct pci_driver net2280_pci_driver = {
	.name =		(char *) driver_name,
	.id_table =	pci_ids,

	.probe =	net2280_probe,
	.remove =	net2280_remove,
	.shutdown =	net2280_shutdown,

	/* FIXME add power management support */
};

module_pci_driver(net2280_pci_driver);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR("David Brownell");
MODULE_LICENSE("GPL");
