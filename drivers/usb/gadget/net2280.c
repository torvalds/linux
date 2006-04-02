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
 * the File Storage, Serial, and Ethernet/RNDIS gadget drivers
 * as well as Gadget Zero and Gadgetfs.
 *
 * DMA is enabled by default.  Drivers using transfer queues might use
 * DMA chaining to remove IRQ latencies between transfers.  (Except when
 * short OUT transfers happen.)  Drivers can use the req->no_interrupt
 * hint to completely eliminate some IRQs, if a later IRQ is guaranteed
 * and DMA chaining is enabled.
 *
 * Note that almost all the errata workarounds here are only needed for
 * rev1 chips.  Rev1a silicon (0110) fixes almost all of them.
 */

/*
 * Copyright (C) 2003 David Brownell
 * Copyright (C) 2003-2005 PLX Technology, Inc.
 *
 * Modified Seth Levy 2005 PLX Technology, Inc. to provide compatibility with 2282 chip
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#undef	DEBUG		/* messages on error and most fault paths */
#undef	VERBOSE		/* extra debug messages (success too) */

#include <linux/config.h>
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
#include <linux/usb_ch9.h>
#include <linux/usb_gadget.h>

#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/unaligned.h>


#define	DRIVER_DESC		"PLX NET228x USB Peripheral Controller"
#define	DRIVER_VERSION		"2005 Sept 27"

#define	DMA_ADDR_INVALID	(~(dma_addr_t)0)
#define	EP_DONTUSE		13	/* nonzero */

#define USE_RDK_LEDS		/* GPIO pins control three LEDs */


static const char driver_name [] = "net2280";
static const char driver_desc [] = DRIVER_DESC;

static const char ep0name [] = "ep0";
static const char *ep_name [] = {
	ep0name,
	"ep-a", "ep-b", "ep-c", "ep-d",
	"ep-e", "ep-f",
};

/* use_dma -- general goodness, fewer interrupts, less cpu load (vs PIO)
 * use_dma_chaining -- dma descriptor queueing gives even more irq reduction
 *
 * The net2280 DMA engines are not tightly integrated with their FIFOs;
 * not all cases are (yet) handled well in this driver or the silicon.
 * Some gadget drivers work better with the dma support here than others.
 * These two parameters let you use PIO or more aggressive DMA.
 */
static int use_dma = 1;
static int use_dma_chaining = 0;

/* "modprobe net2280 use_dma=n" etc */
module_param (use_dma, bool, S_IRUGO);
module_param (use_dma_chaining, bool, S_IRUGO);


/* mode 0 == ep-{a,b,c,d} 1K fifo each
 * mode 1 == ep-{a,b} 2K fifo each, ep-{c,d} unavailable
 * mode 2 == ep-a 2K fifo, ep-{b,c} 1K each, ep-d unavailable
 */
static ushort fifo_mode = 0;

/* "modprobe net2280 fifo_mode=1" etc */
module_param (fifo_mode, ushort, 0644);

/* enable_suspend -- When enabled, the driver will respond to
 * USB suspend requests by powering down the NET2280.  Otherwise,
 * USB suspend requests will be ignored.  This is acceptible for
 * self-powered devices
 */
static int enable_suspend = 0;

/* "modprobe net2280 enable_suspend=1" etc */
module_param (enable_suspend, bool, S_IRUGO);


#define	DIR_STRING(bAddress) (((bAddress) & USB_DIR_IN) ? "in" : "out")

#if defined(CONFIG_USB_GADGET_DEBUG_FILES) || defined (DEBUG)
static char *type_string (u8 bmAttributes)
{
	switch ((bmAttributes) & USB_ENDPOINT_XFERTYPE_MASK) {
	case USB_ENDPOINT_XFER_BULK:	return "bulk";
	case USB_ENDPOINT_XFER_ISOC:	return "iso";
	case USB_ENDPOINT_XFER_INT:	return "intr";
	};
	return "control";
}
#endif

#include "net2280.h"

#define valid_bit	__constant_cpu_to_le32 (1 << VALID_BIT)
#define dma_done_ie	__constant_cpu_to_le32 (1 << DMA_DONE_INTERRUPT_ENABLE)

/*-------------------------------------------------------------------------*/

static int
net2280_enable (struct usb_ep *_ep, const struct usb_endpoint_descriptor *desc)
{
	struct net2280		*dev;
	struct net2280_ep	*ep;
	u32			max, tmp;
	unsigned long		flags;

	ep = container_of (_ep, struct net2280_ep, ep);
	if (!_ep || !desc || ep->desc || _ep->name == ep0name
			|| desc->bDescriptorType != USB_DT_ENDPOINT)
		return -EINVAL;
	dev = ep->dev;
	if (!dev->driver || dev->gadget.speed == USB_SPEED_UNKNOWN)
		return -ESHUTDOWN;

	/* erratum 0119 workaround ties up an endpoint number */
	if ((desc->bEndpointAddress & 0x0f) == EP_DONTUSE)
		return -EDOM;

	/* sanity check ep-e/ep-f since their fifos are small */
	max = le16_to_cpu (desc->wMaxPacketSize) & 0x1fff;
	if (ep->num > 4 && max > 64)
		return -ERANGE;

	spin_lock_irqsave (&dev->lock, flags);
	_ep->maxpacket = max & 0x7ff;
	ep->desc = desc;

	/* ep_reset() has already been called */
	ep->stopped = 0;
	ep->out_overflow = 0;

	/* set speed-dependent max packet; may kick in high bandwidth */
	set_idx_reg (dev->regs, REG_EP_MAXPKT (dev, ep->num), max);

	/* FIFO lines can't go to different packets.  PIO is ok, so
	 * use it instead of troublesome (non-bulk) multi-packet DMA.
	 */
	if (ep->dma && (max % 4) != 0 && use_dma_chaining) {
		DEBUG (ep->dev, "%s, no dma for maxpacket %d\n",
			ep->ep.name, ep->ep.maxpacket);
		ep->dma = NULL;
	}

	/* set type, direction, address; reset fifo counters */
	writel ((1 << FIFO_FLUSH), &ep->regs->ep_stat);
	tmp = (desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK);
	if (tmp == USB_ENDPOINT_XFER_INT) {
		/* erratum 0105 workaround prevents hs NYET */
		if (dev->chiprev == 0100
				&& dev->gadget.speed == USB_SPEED_HIGH
				&& !(desc->bEndpointAddress & USB_DIR_IN))
			writel ((1 << CLEAR_NAK_OUT_PACKETS_MODE),
				&ep->regs->ep_rsp);
	} else if (tmp == USB_ENDPOINT_XFER_BULK) {
		/* catch some particularly blatant driver bugs */
		if ((dev->gadget.speed == USB_SPEED_HIGH
					&& max != 512)
				|| (dev->gadget.speed == USB_SPEED_FULL
					&& max > 64)) {
			spin_unlock_irqrestore (&dev->lock, flags);
			return -ERANGE;
		}
	}
	ep->is_iso = (tmp == USB_ENDPOINT_XFER_ISOC) ? 1 : 0;
	tmp <<= ENDPOINT_TYPE;
	tmp |= desc->bEndpointAddress;
	tmp |= (4 << ENDPOINT_BYTE_COUNT);	/* default full fifo lines */
	tmp |= 1 << ENDPOINT_ENABLE;
	wmb ();

	/* for OUT transfers, block the rx fifo until a read is posted */
	ep->is_in = (tmp & USB_DIR_IN) != 0;
	if (!ep->is_in)
		writel ((1 << SET_NAK_OUT_PACKETS), &ep->regs->ep_rsp);
	else if (dev->pdev->device != 0x2280) {
		/* Added for 2282, Don't use nak packets on an in endpoint, this was ignored on 2280 */
		writel ((1 << CLEAR_NAK_OUT_PACKETS)
			| (1 << CLEAR_NAK_OUT_PACKETS_MODE), &ep->regs->ep_rsp);
	}

	writel (tmp, &ep->regs->ep_cfg);

	/* enable irqs */
	if (!ep->dma) {				/* pio, per-packet */
		tmp = (1 << ep->num) | readl (&dev->regs->pciirqenb0);
		writel (tmp, &dev->regs->pciirqenb0);

		tmp = (1 << DATA_PACKET_RECEIVED_INTERRUPT_ENABLE)
			| (1 << DATA_PACKET_TRANSMITTED_INTERRUPT_ENABLE);
		if (dev->pdev->device == 0x2280)
			tmp |= readl (&ep->regs->ep_irqenb);
		writel (tmp, &ep->regs->ep_irqenb);
	} else {				/* dma, per-request */
		tmp = (1 << (8 + ep->num));	/* completion */
		tmp |= readl (&dev->regs->pciirqenb1);
		writel (tmp, &dev->regs->pciirqenb1);

		/* for short OUT transfers, dma completions can't
		 * advance the queue; do it pio-style, by hand.
		 * NOTE erratum 0112 workaround #2
		 */
		if ((desc->bEndpointAddress & USB_DIR_IN) == 0) {
			tmp = (1 << SHORT_PACKET_TRANSFERRED_INTERRUPT_ENABLE);
			writel (tmp, &ep->regs->ep_irqenb);

			tmp = (1 << ep->num) | readl (&dev->regs->pciirqenb0);
			writel (tmp, &dev->regs->pciirqenb0);
		}
	}

	tmp = desc->bEndpointAddress;
	DEBUG (dev, "enabled %s (ep%d%s-%s) %s max %04x\n",
		_ep->name, tmp & 0x0f, DIR_STRING (tmp),
		type_string (desc->bmAttributes),
		ep->dma ? "dma" : "pio", max);

	/* pci writes may still be posted */
	spin_unlock_irqrestore (&dev->lock, flags);
	return 0;
}

static int handshake (u32 __iomem *ptr, u32 mask, u32 done, int usec)
{
	u32	result;

	do {
		result = readl (ptr);
		if (result == ~(u32)0)		/* "device unplugged" */
			return -ENODEV;
		result &= mask;
		if (result == done)
			return 0;
		udelay (1);
		usec--;
	} while (usec > 0);
	return -ETIMEDOUT;
}

static struct usb_ep_ops net2280_ep_ops;

static void ep_reset (struct net2280_regs __iomem *regs, struct net2280_ep *ep)
{
	u32		tmp;

	ep->desc = NULL;
	INIT_LIST_HEAD (&ep->queue);

	ep->ep.maxpacket = ~0;
	ep->ep.ops = &net2280_ep_ops;

	/* disable the dma, irqs, endpoint... */
	if (ep->dma) {
		writel (0, &ep->dma->dmactl);
		writel (  (1 << DMA_SCATTER_GATHER_DONE_INTERRUPT)
			| (1 << DMA_TRANSACTION_DONE_INTERRUPT)
			| (1 << DMA_ABORT)
			, &ep->dma->dmastat);

		tmp = readl (&regs->pciirqenb0);
		tmp &= ~(1 << ep->num);
		writel (tmp, &regs->pciirqenb0);
	} else {
		tmp = readl (&regs->pciirqenb1);
		tmp &= ~(1 << (8 + ep->num));	/* completion */
		writel (tmp, &regs->pciirqenb1);
	}
	writel (0, &ep->regs->ep_irqenb);

	/* init to our chosen defaults, notably so that we NAK OUT
	 * packets until the driver queues a read (+note erratum 0112)
	 */
	if (!ep->is_in || ep->dev->pdev->device == 0x2280) {
		tmp = (1 << SET_NAK_OUT_PACKETS_MODE)
		| (1 << SET_NAK_OUT_PACKETS)
		| (1 << CLEAR_EP_HIDE_STATUS_PHASE)
		| (1 << CLEAR_INTERRUPT_MODE);
	} else {
		/* added for 2282 */
		tmp = (1 << CLEAR_NAK_OUT_PACKETS_MODE)
		| (1 << CLEAR_NAK_OUT_PACKETS)
		| (1 << CLEAR_EP_HIDE_STATUS_PHASE)
		| (1 << CLEAR_INTERRUPT_MODE);
	}

	if (ep->num != 0) {
		tmp |= (1 << CLEAR_ENDPOINT_TOGGLE)
			| (1 << CLEAR_ENDPOINT_HALT);
	}
	writel (tmp, &ep->regs->ep_rsp);

	/* scrub most status bits, and flush any fifo state */
	if (ep->dev->pdev->device == 0x2280)
		tmp = (1 << FIFO_OVERFLOW)
			| (1 << FIFO_UNDERFLOW);
	else
		tmp = 0;

	writel (tmp | (1 << TIMEOUT)
		| (1 << USB_STALL_SENT)
		| (1 << USB_IN_NAK_SENT)
		| (1 << USB_IN_ACK_RCVD)
		| (1 << USB_OUT_PING_NAK_SENT)
		| (1 << USB_OUT_ACK_SENT)
		| (1 << FIFO_FLUSH)
		| (1 << SHORT_PACKET_OUT_DONE_INTERRUPT)
		| (1 << SHORT_PACKET_TRANSFERRED_INTERRUPT)
		| (1 << DATA_PACKET_RECEIVED_INTERRUPT)
		| (1 << DATA_PACKET_TRANSMITTED_INTERRUPT)
		| (1 << DATA_OUT_PING_TOKEN_INTERRUPT)
		| (1 << DATA_IN_TOKEN_INTERRUPT)
		, &ep->regs->ep_stat);

	/* fifo size is handled separately */
}

static void nuke (struct net2280_ep *);

static int net2280_disable (struct usb_ep *_ep)
{
	struct net2280_ep	*ep;
	unsigned long		flags;

	ep = container_of (_ep, struct net2280_ep, ep);
	if (!_ep || !ep->desc || _ep->name == ep0name)
		return -EINVAL;

	spin_lock_irqsave (&ep->dev->lock, flags);
	nuke (ep);
	ep_reset (ep->dev->regs, ep);

	VDEBUG (ep->dev, "disabled %s %s\n",
			ep->dma ? "dma" : "pio", _ep->name);

	/* synch memory views with the device */
	(void) readl (&ep->regs->ep_cfg);

	if (use_dma && !ep->dma && ep->num >= 1 && ep->num <= 4)
		ep->dma = &ep->dev->dma [ep->num - 1];

	spin_unlock_irqrestore (&ep->dev->lock, flags);
	return 0;
}

/*-------------------------------------------------------------------------*/

static struct usb_request *
net2280_alloc_request (struct usb_ep *_ep, gfp_t gfp_flags)
{
	struct net2280_ep	*ep;
	struct net2280_request	*req;

	if (!_ep)
		return NULL;
	ep = container_of (_ep, struct net2280_ep, ep);

	req = kzalloc(sizeof(*req), gfp_flags);
	if (!req)
		return NULL;

	req->req.dma = DMA_ADDR_INVALID;
	INIT_LIST_HEAD (&req->queue);

	/* this dma descriptor may be swapped with the previous dummy */
	if (ep->dma) {
		struct net2280_dma	*td;

		td = pci_pool_alloc (ep->dev->requests, gfp_flags,
				&req->td_dma);
		if (!td) {
			kfree (req);
			return NULL;
		}
		td->dmacount = 0;	/* not VALID */
		td->dmaaddr = __constant_cpu_to_le32 (DMA_ADDR_INVALID);
		td->dmadesc = td->dmaaddr;
		req->td = td;
	}
	return &req->req;
}

static void
net2280_free_request (struct usb_ep *_ep, struct usb_request *_req)
{
	struct net2280_ep	*ep;
	struct net2280_request	*req;

	ep = container_of (_ep, struct net2280_ep, ep);
	if (!_ep || !_req)
		return;

	req = container_of (_req, struct net2280_request, req);
	WARN_ON (!list_empty (&req->queue));
	if (req->td)
		pci_pool_free (ep->dev->requests, req->td, req->td_dma);
	kfree (req);
}

/*-------------------------------------------------------------------------*/

#undef USE_KMALLOC

/* many common platforms have dma-coherent caches, which means that it's
 * safe to use kmalloc() memory for all i/o buffers without using any
 * cache flushing calls.  (unless you're trying to share cache lines
 * between dma and non-dma activities, which is a slow idea in any case.)
 *
 * other platforms need more care, with 2.5 having a moderately general
 * solution (which falls down for allocations smaller than one page)
 * that improves significantly on the 2.4 PCI allocators by removing
 * the restriction that memory never be freed in_interrupt().
 */
#if	defined(CONFIG_X86)
#define USE_KMALLOC

#elif	defined(CONFIG_PPC) && !defined(CONFIG_NOT_COHERENT_CACHE)
#define USE_KMALLOC

#elif	defined(CONFIG_MIPS) && !defined(CONFIG_DMA_NONCOHERENT)
#define USE_KMALLOC

/* FIXME there are other cases, including an x86-64 one ...  */
#endif

/* allocating buffers this way eliminates dma mapping overhead, which
 * on some platforms will mean eliminating a per-io buffer copy.  with
 * some kinds of system caches, further tweaks may still be needed.
 */
static void *
net2280_alloc_buffer (
	struct usb_ep		*_ep,
	unsigned		bytes,
	dma_addr_t		*dma,
	gfp_t			gfp_flags
)
{
	void			*retval;
	struct net2280_ep	*ep;

	ep = container_of (_ep, struct net2280_ep, ep);
	if (!_ep)
		return NULL;
	*dma = DMA_ADDR_INVALID;

#if	defined(USE_KMALLOC)
	retval = kmalloc(bytes, gfp_flags);
	if (retval)
		*dma = virt_to_phys(retval);
#else
	if (ep->dma) {
		/* the main problem with this call is that it wastes memory
		 * on typical 1/N page allocations: it allocates 1-N pages.
		 */
#warning Using dma_alloc_coherent even with buffers smaller than a page.
		retval = dma_alloc_coherent(&ep->dev->pdev->dev,
				bytes, dma, gfp_flags);
	} else
		retval = kmalloc(bytes, gfp_flags);
#endif
	return retval;
}

static void
net2280_free_buffer (
	struct usb_ep *_ep,
	void *buf,
	dma_addr_t dma,
	unsigned bytes
) {
	/* free memory into the right allocator */
#ifndef	USE_KMALLOC
	if (dma != DMA_ADDR_INVALID) {
		struct net2280_ep	*ep;

		ep = container_of(_ep, struct net2280_ep, ep);
		if (!_ep)
			return;
		dma_free_coherent(&ep->dev->pdev->dev, bytes, buf, dma);
	} else
#endif
		kfree (buf);
}

/*-------------------------------------------------------------------------*/

/* load a packet into the fifo we use for usb IN transfers.
 * works for all endpoints.
 *
 * NOTE: pio with ep-a..ep-d could stuff multiple packets into the fifo
 * at a time, but this code is simpler because it knows it only writes
 * one packet.  ep-a..ep-d should use dma instead.
 */
static void
write_fifo (struct net2280_ep *ep, struct usb_request *req)
{
	struct net2280_ep_regs	__iomem *regs = ep->regs;
	u8			*buf;
	u32			tmp;
	unsigned		count, total;

	/* INVARIANT:  fifo is currently empty. (testable) */

	if (req) {
		buf = req->buf + req->actual;
		prefetch (buf);
		total = req->length - req->actual;
	} else {
		total = 0;
		buf = NULL;
	}

	/* write just one packet at a time */
	count = ep->ep.maxpacket;
	if (count > total)	/* min() cannot be used on a bitfield */
		count = total;

	VDEBUG (ep->dev, "write %s fifo (IN) %d bytes%s req %p\n",
			ep->ep.name, count,
			(count != ep->ep.maxpacket) ? " (short)" : "",
			req);
	while (count >= 4) {
		/* NOTE be careful if you try to align these. fifo lines
		 * should normally be full (4 bytes) and successive partial
		 * lines are ok only in certain cases.
		 */
		tmp = get_unaligned ((u32 *)buf);
		cpu_to_le32s (&tmp);
		writel (tmp, &regs->ep_data);
		buf += 4;
		count -= 4;
	}

	/* last fifo entry is "short" unless we wrote a full packet.
	 * also explicitly validate last word in (periodic) transfers
	 * when maxpacket is not a multiple of 4 bytes.
	 */
	if (count || total < ep->ep.maxpacket) {
		tmp = count ? get_unaligned ((u32 *)buf) : count;
		cpu_to_le32s (&tmp);
		set_fifo_bytecount (ep, count & 0x03);
		writel (tmp, &regs->ep_data);
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
static void out_flush (struct net2280_ep *ep)
{
	u32	__iomem *statp;
	u32	tmp;

	ASSERT_OUT_NAKING (ep);

	statp = &ep->regs->ep_stat;
	writel (  (1 << DATA_OUT_PING_TOKEN_INTERRUPT)
		| (1 << DATA_PACKET_RECEIVED_INTERRUPT)
		, statp);
	writel ((1 << FIFO_FLUSH), statp);
	mb ();
	tmp = readl (statp);
	if (tmp & (1 << DATA_OUT_PING_TOKEN_INTERRUPT)
			/* high speed did bulk NYET; fifo isn't filling */
			&& ep->dev->gadget.speed == USB_SPEED_FULL) {
		unsigned	usec;

		usec = 50;		/* 64 byte bulk/interrupt */
		handshake (statp, (1 << USB_OUT_PING_NAK_SENT),
				(1 << USB_OUT_PING_NAK_SENT), usec);
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
static int
read_fifo (struct net2280_ep *ep, struct net2280_request *req)
{
	struct net2280_ep_regs	__iomem *regs = ep->regs;
	u8			*buf = req->req.buf + req->req.actual;
	unsigned		count, tmp, is_short;
	unsigned		cleanup = 0, prevent = 0;

	/* erratum 0106 ... packets coming in during fifo reads might
	 * be incompletely rejected.  not all cases have workarounds.
	 */
	if (ep->dev->chiprev == 0x0100
			&& ep->dev->gadget.speed == USB_SPEED_FULL) {
		udelay (1);
		tmp = readl (&ep->regs->ep_stat);
		if ((tmp & (1 << NAK_OUT_PACKETS)))
			cleanup = 1;
		else if ((tmp & (1 << FIFO_FULL))) {
			start_out_naking (ep);
			prevent = 1;
		}
		/* else: hope we don't see the problem */
	}

	/* never overflow the rx buffer. the fifo reads packets until
	 * it sees a short one; we might not be ready for them all.
	 */
	prefetchw (buf);
	count = readl (&regs->ep_avail);
	if (unlikely (count == 0)) {
		udelay (1);
		tmp = readl (&ep->regs->ep_stat);
		count = readl (&regs->ep_avail);
		/* handled that data already? */
		if (count == 0 && (tmp & (1 << NAK_OUT_PACKETS)) == 0)
			return 0;
	}

	tmp = req->req.length - req->req.actual;
	if (count > tmp) {
		/* as with DMA, data overflow gets flushed */
		if ((tmp % ep->ep.maxpacket) != 0) {
			ERROR (ep->dev,
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

	VDEBUG (ep->dev, "read %s fifo (OUT) %d bytes%s%s%s req %p %d/%d\n",
			ep->ep.name, count, is_short ? " (short)" : "",
			cleanup ? " flush" : "", prevent ? " nak" : "",
			req, req->req.actual, req->req.length);

	while (count >= 4) {
		tmp = readl (&regs->ep_data);
		cpu_to_le32s (&tmp);
		put_unaligned (tmp, (u32 *)buf);
		buf += 4;
		count -= 4;
	}
	if (count) {
		tmp = readl (&regs->ep_data);
		/* LE conversion is implicit here: */
		do {
			*buf++ = (u8) tmp;
			tmp >>= 8;
		} while (--count);
	}
	if (cleanup)
		out_flush (ep);
	if (prevent) {
		writel ((1 << CLEAR_NAK_OUT_PACKETS), &ep->regs->ep_rsp);
		(void) readl (&ep->regs->ep_rsp);
	}

	return is_short || ((req->req.actual == req->req.length)
				&& !req->req.zero);
}

/* fill out dma descriptor to match a given request */
static void
fill_dma_desc (struct net2280_ep *ep, struct net2280_request *req, int valid)
{
	struct net2280_dma	*td = req->td;
	u32			dmacount = req->req.length;

	/* don't let DMA continue after a short OUT packet,
	 * so overruns can't affect the next transfer.
	 * in case of overruns on max-size packets, we can't
	 * stop the fifo from filling but we can flush it.
	 */
	if (ep->is_in)
		dmacount |= (1 << DMA_DIRECTION);
	if ((!ep->is_in && (dmacount % ep->ep.maxpacket) != 0) || ep->dev->pdev->device != 0x2280)
		dmacount |= (1 << END_OF_CHAIN);

	req->valid = valid;
	if (valid)
		dmacount |= (1 << VALID_BIT);
	if (likely(!req->req.no_interrupt || !use_dma_chaining))
		dmacount |= (1 << DMA_DONE_INTERRUPT_ENABLE);

	/* td->dmadesc = previously set by caller */
	td->dmaaddr = cpu_to_le32 (req->req.dma);

	/* 2280 may be polling VALID_BIT through ep->dma->dmadesc */
	wmb ();
	td->dmacount = cpu_to_le32p (&dmacount);
}

static const u32 dmactl_default =
		  (1 << DMA_SCATTER_GATHER_DONE_INTERRUPT)
		| (1 << DMA_CLEAR_COUNT_ENABLE)
		/* erratum 0116 workaround part 1 (use POLLING) */
		| (POLL_100_USEC << DESCRIPTOR_POLLING_RATE)
		| (1 << DMA_VALID_BIT_POLLING_ENABLE)
		| (1 << DMA_VALID_BIT_ENABLE)
		| (1 << DMA_SCATTER_GATHER_ENABLE)
		/* erratum 0116 workaround part 2 (no AUTOSTART) */
		| (1 << DMA_ENABLE);

static inline void spin_stop_dma (struct net2280_dma_regs __iomem *dma)
{
	handshake (&dma->dmactl, (1 << DMA_ENABLE), 0, 50);
}

static inline void stop_dma (struct net2280_dma_regs __iomem *dma)
{
	writel (readl (&dma->dmactl) & ~(1 << DMA_ENABLE), &dma->dmactl);
	spin_stop_dma (dma);
}

static void start_queue (struct net2280_ep *ep, u32 dmactl, u32 td_dma)
{
	struct net2280_dma_regs	__iomem *dma = ep->dma;
	unsigned int tmp = (1 << VALID_BIT) | (ep->is_in << DMA_DIRECTION);

	if (ep->dev->pdev->device != 0x2280)
		tmp |= (1 << END_OF_CHAIN);

	writel (tmp, &dma->dmacount);
	writel (readl (&dma->dmastat), &dma->dmastat);

	writel (td_dma, &dma->dmadesc);
	writel (dmactl, &dma->dmactl);

	/* erratum 0116 workaround part 3:  pci arbiter away from net2280 */
	(void) readl (&ep->dev->pci->pcimstctl);

	writel ((1 << DMA_START), &dma->dmastat);

	if (!ep->is_in)
		stop_out_naking (ep);
}

static void start_dma (struct net2280_ep *ep, struct net2280_request *req)
{
	u32			tmp;
	struct net2280_dma_regs	__iomem *dma = ep->dma;

	/* FIXME can't use DMA for ZLPs */

	/* on this path we "know" there's no dma active (yet) */
	WARN_ON (readl (&dma->dmactl) & (1 << DMA_ENABLE));
	writel (0, &ep->dma->dmactl);

	/* previous OUT packet might have been short */
	if (!ep->is_in && ((tmp = readl (&ep->regs->ep_stat))
		 		& (1 << NAK_OUT_PACKETS)) != 0) {
		writel ((1 << SHORT_PACKET_TRANSFERRED_INTERRUPT),
			&ep->regs->ep_stat);

		tmp = readl (&ep->regs->ep_avail);
		if (tmp) {
			writel (readl (&dma->dmastat), &dma->dmastat);

			/* transfer all/some fifo data */
			writel (req->req.dma, &dma->dmaaddr);
			tmp = min (tmp, req->req.length);

			/* dma irq, faking scatterlist status */
			req->td->dmacount = cpu_to_le32 (req->req.length - tmp);
			writel ((1 << DMA_DONE_INTERRUPT_ENABLE)
				| tmp, &dma->dmacount);
			req->td->dmadesc = 0;
			req->valid = 1;

			writel ((1 << DMA_ENABLE), &dma->dmactl);
			writel ((1 << DMA_START), &dma->dmastat);
			return;
		}
	}

	tmp = dmactl_default;

	/* force packet boundaries between dma requests, but prevent the
	 * controller from automagically writing a last "short" packet
	 * (zero length) unless the driver explicitly said to do that.
	 */
	if (ep->is_in) {
		if (likely ((req->req.length % ep->ep.maxpacket) != 0
				|| req->req.zero)) {
			tmp |= (1 << DMA_FIFO_VALIDATE);
			ep->in_fifo_validate = 1;
		} else
			ep->in_fifo_validate = 0;
	}

	/* init req->td, pointing to the current dummy */
	req->td->dmadesc = cpu_to_le32 (ep->td_dma);
	fill_dma_desc (ep, req, 1);

	if (!use_dma_chaining)
		req->td->dmacount |= __constant_cpu_to_le32 (1 << END_OF_CHAIN);

	start_queue (ep, tmp, req->td_dma);
}

static inline void
queue_dma (struct net2280_ep *ep, struct net2280_request *req, int valid)
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

	fill_dma_desc (ep, req, valid);
}

static void
done (struct net2280_ep *ep, struct net2280_request *req, int status)
{
	struct net2280		*dev;
	unsigned		stopped = ep->stopped;

	list_del_init (&req->queue);

	if (req->req.status == -EINPROGRESS)
		req->req.status = status;
	else
		status = req->req.status;

	dev = ep->dev;
	if (req->mapped) {
		pci_unmap_single (dev->pdev, req->req.dma, req->req.length,
			ep->is_in ? PCI_DMA_TODEVICE : PCI_DMA_FROMDEVICE);
		req->req.dma = DMA_ADDR_INVALID;
		req->mapped = 0;
	}

	if (status && status != -ESHUTDOWN)
		VDEBUG (dev, "complete %s req %p stat %d len %u/%u\n",
			ep->ep.name, &req->req, status,
			req->req.actual, req->req.length);

	/* don't modify queue heads during completion callback */
	ep->stopped = 1;
	spin_unlock (&dev->lock);
	req->req.complete (&ep->ep, &req->req);
	spin_lock (&dev->lock);
	ep->stopped = stopped;
}

/*-------------------------------------------------------------------------*/

static int
net2280_queue (struct usb_ep *_ep, struct usb_request *_req, gfp_t gfp_flags)
{
	struct net2280_request	*req;
	struct net2280_ep	*ep;
	struct net2280		*dev;
	unsigned long		flags;

	/* we always require a cpu-view buffer, so that we can
	 * always use pio (as fallback or whatever).
	 */
	req = container_of (_req, struct net2280_request, req);
	if (!_req || !_req->complete || !_req->buf
			|| !list_empty (&req->queue))
		return -EINVAL;
	if (_req->length > (~0 & DMA_BYTE_COUNT_MASK))
		return -EDOM;
	ep = container_of (_ep, struct net2280_ep, ep);
	if (!_ep || (!ep->desc && ep->num != 0))
		return -EINVAL;
	dev = ep->dev;
	if (!dev->driver || dev->gadget.speed == USB_SPEED_UNKNOWN)
		return -ESHUTDOWN;

	/* FIXME implement PIO fallback for ZLPs with DMA */
	if (ep->dma && _req->length == 0)
		return -EOPNOTSUPP;

	/* set up dma mapping in case the caller didn't */
	if (ep->dma && _req->dma == DMA_ADDR_INVALID) {
		_req->dma = pci_map_single (dev->pdev, _req->buf, _req->length,
			ep->is_in ? PCI_DMA_TODEVICE : PCI_DMA_FROMDEVICE);
		req->mapped = 1;
	}

#if 0
	VDEBUG (dev, "%s queue req %p, len %d buf %p\n",
			_ep->name, _req, _req->length, _req->buf);
#endif

	spin_lock_irqsave (&dev->lock, flags);

	_req->status = -EINPROGRESS;
	_req->actual = 0;

	/* kickstart this i/o queue? */
	if (list_empty (&ep->queue) && !ep->stopped) {
		/* use DMA if the endpoint supports it, else pio */
		if (ep->dma)
			start_dma (ep, req);
		else {
			/* maybe there's no control data, just status ack */
			if (ep->num == 0 && _req->length == 0) {
				allow_status (ep);
				done (ep, req, 0);
				VDEBUG (dev, "%s status ack\n", ep->ep.name);
				goto done;
			}

			/* PIO ... stuff the fifo, or unblock it.  */
			if (ep->is_in)
				write_fifo (ep, _req);
			else if (list_empty (&ep->queue)) {
				u32	s;

				/* OUT FIFO might have packet(s) buffered */
				s = readl (&ep->regs->ep_stat);
				if ((s & (1 << FIFO_EMPTY)) == 0) {
					/* note:  _req->short_not_ok is
					 * ignored here since PIO _always_
					 * stops queue advance here, and
					 * _req->status doesn't change for
					 * short reads (only _req->actual)
					 */
					if (read_fifo (ep, req)) {
						done (ep, req, 0);
						if (ep->num == 0)
							allow_status (ep);
						/* don't queue it */
						req = NULL;
					} else
						s = readl (&ep->regs->ep_stat);
				}

				/* don't NAK, let the fifo fill */
				if (req && (s & (1 << NAK_OUT_PACKETS)))
					writel ((1 << CLEAR_NAK_OUT_PACKETS),
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
			expect = likely (req->req.zero
				|| (req->req.length % ep->ep.maxpacket) != 0);
			if (expect != ep->in_fifo_validate)
				valid = 0;
		}
		queue_dma (ep, req, valid);

	} /* else the irq handler advances the queue. */

	if (req)
		list_add_tail (&req->queue, &ep->queue);
done:
	spin_unlock_irqrestore (&dev->lock, flags);

	/* pci writes may still be posted */
	return 0;
}

static inline void
dma_done (
	struct net2280_ep *ep,
	struct net2280_request *req,
	u32 dmacount,
	int status
)
{
	req->req.actual = req->req.length - (DMA_BYTE_COUNT_MASK & dmacount);
	done (ep, req, status);
}

static void restart_dma (struct net2280_ep *ep);

static void scan_dma_completions (struct net2280_ep *ep)
{
	/* only look at descriptors that were "naturally" retired,
	 * so fifo and list head state won't matter
	 */
	while (!list_empty (&ep->queue)) {
		struct net2280_request	*req;
		u32			tmp;

		req = list_entry (ep->queue.next,
				struct net2280_request, queue);
		if (!req->valid)
			break;
		rmb ();
		tmp = le32_to_cpup (&req->td->dmacount);
		if ((tmp & (1 << VALID_BIT)) != 0)
			break;

		/* SHORT_PACKET_TRANSFERRED_INTERRUPT handles "usb-short"
		 * cases where DMA must be aborted; this code handles
		 * all non-abort DMA completions.
		 */
		if (unlikely (req->td->dmadesc == 0)) {
			/* paranoia */
			tmp = readl (&ep->dma->dmacount);
			if (tmp & DMA_BYTE_COUNT_MASK)
				break;
			/* single transfer mode */
			dma_done (ep, req, tmp, 0);
			break;
		} else if (!ep->is_in
				&& (req->req.length % ep->ep.maxpacket) != 0) {
			tmp = readl (&ep->regs->ep_stat);

			/* AVOID TROUBLE HERE by not issuing short reads from
			 * your gadget driver.  That helps avoids errata 0121,
			 * 0122, and 0124; not all cases trigger the warning.
			 */
			if ((tmp & (1 << NAK_OUT_PACKETS)) == 0) {
				WARN (ep->dev, "%s lost packet sync!\n",
						ep->ep.name);
				req->req.status = -EOVERFLOW;
			} else if ((tmp = readl (&ep->regs->ep_avail)) != 0) {
				/* fifo gets flushed later */
				ep->out_overflow = 1;
				DEBUG (ep->dev, "%s dma, discard %d len %d\n",
						ep->ep.name, tmp,
						req->req.length);
				req->req.status = -EOVERFLOW;
			}
		}
		dma_done (ep, req, tmp, 0);
	}
}

static void restart_dma (struct net2280_ep *ep)
{
	struct net2280_request	*req;
	u32			dmactl = dmactl_default;

	if (ep->stopped)
		return;
	req = list_entry (ep->queue.next, struct net2280_request, queue);

	if (!use_dma_chaining) {
		start_dma (ep, req);
		return;
	}

	/* the 2280 will be processing the queue unless queue hiccups after
	 * the previous transfer:
	 *  IN:   wanted automagic zlp, head doesn't (or vice versa)
	 *        DMA_FIFO_VALIDATE doesn't init from dma descriptors.
	 *  OUT:  was "usb-short", we must restart.
	 */
	if (ep->is_in && !req->valid) {
		struct net2280_request	*entry, *prev = NULL;
		int			reqmode, done = 0;

		DEBUG (ep->dev, "%s dma hiccup td %p\n", ep->ep.name, req->td);
		ep->in_fifo_validate = likely (req->req.zero
			|| (req->req.length % ep->ep.maxpacket) != 0);
		if (ep->in_fifo_validate)
			dmactl |= (1 << DMA_FIFO_VALIDATE);
		list_for_each_entry (entry, &ep->queue, queue) {
			__le32		dmacount;

			if (entry == req)
				continue;
			dmacount = entry->td->dmacount;
			if (!done) {
				reqmode = likely (entry->req.zero
					|| (entry->req.length
						% ep->ep.maxpacket) != 0);
				if (reqmode == ep->in_fifo_validate) {
					entry->valid = 1;
					dmacount |= valid_bit;
					entry->td->dmacount = dmacount;
					prev = entry;
					continue;
				} else {
					/* force a hiccup */
					prev->td->dmacount |= dma_done_ie;
					done = 1;
				}
			}

			/* walk the rest of the queue so unlinks behave */
			entry->valid = 0;
			dmacount &= ~valid_bit;
			entry->td->dmacount = dmacount;
			prev = entry;
		}
	}

	writel (0, &ep->dma->dmactl);
	start_queue (ep, dmactl, req->td_dma);
}

static void abort_dma (struct net2280_ep *ep)
{
	/* abort the current transfer */
	if (likely (!list_empty (&ep->queue))) {
		/* FIXME work around errata 0121, 0122, 0124 */
		writel ((1 << DMA_ABORT), &ep->dma->dmastat);
		spin_stop_dma (ep->dma);
	} else
		stop_dma (ep->dma);
	scan_dma_completions (ep);
}

/* dequeue ALL requests */
static void nuke (struct net2280_ep *ep)
{
	struct net2280_request	*req;

	/* called with spinlock held */
	ep->stopped = 1;
	if (ep->dma)
		abort_dma (ep);
	while (!list_empty (&ep->queue)) {
		req = list_entry (ep->queue.next,
				struct net2280_request,
				queue);
		done (ep, req, -ESHUTDOWN);
	}
}

/* dequeue JUST ONE request */
static int net2280_dequeue (struct usb_ep *_ep, struct usb_request *_req)
{
	struct net2280_ep	*ep;
	struct net2280_request	*req;
	unsigned long		flags;
	u32			dmactl;
	int			stopped;

	ep = container_of (_ep, struct net2280_ep, ep);
	if (!_ep || (!ep->desc && ep->num != 0) || !_req)
		return -EINVAL;

	spin_lock_irqsave (&ep->dev->lock, flags);
	stopped = ep->stopped;

	/* quiesce dma while we patch the queue */
	dmactl = 0;
	ep->stopped = 1;
	if (ep->dma) {
		dmactl = readl (&ep->dma->dmactl);
		/* WARNING erratum 0127 may kick in ... */
		stop_dma (ep->dma);
		scan_dma_completions (ep);
	}

	/* make sure it's still queued on this endpoint */
	list_for_each_entry (req, &ep->queue, queue) {
		if (&req->req == _req)
			break;
	}
	if (&req->req != _req) {
		spin_unlock_irqrestore (&ep->dev->lock, flags);
		return -EINVAL;
	}

	/* queue head may be partially complete. */
	if (ep->queue.next == &req->queue) {
		if (ep->dma) {
			DEBUG (ep->dev, "unlink (%s) dma\n", _ep->name);
			_req->status = -ECONNRESET;
			abort_dma (ep);
			if (likely (ep->queue.next == &req->queue)) {
				// NOTE: misreports single-transfer mode
				req->td->dmacount = 0;	/* invalidate */
				dma_done (ep, req,
					readl (&ep->dma->dmacount),
					-ECONNRESET);
			}
		} else {
			DEBUG (ep->dev, "unlink (%s) pio\n", _ep->name);
			done (ep, req, -ECONNRESET);
		}
		req = NULL;

	/* patch up hardware chaining data */
	} else if (ep->dma && use_dma_chaining) {
		if (req->queue.prev == ep->queue.next) {
			writel (le32_to_cpu (req->td->dmadesc),
				&ep->dma->dmadesc);
			if (req->td->dmacount & dma_done_ie)
				writel (readl (&ep->dma->dmacount)
						| le32_to_cpu(dma_done_ie),
					&ep->dma->dmacount);
		} else {
			struct net2280_request	*prev;

			prev = list_entry (req->queue.prev,
				struct net2280_request, queue);
			prev->td->dmadesc = req->td->dmadesc;
			if (req->td->dmacount & dma_done_ie)
				prev->td->dmacount |= dma_done_ie;
		}
	}

	if (req)
		done (ep, req, -ECONNRESET);
	ep->stopped = stopped;

	if (ep->dma) {
		/* turn off dma on inactive queues */
		if (list_empty (&ep->queue))
			stop_dma (ep->dma);
		else if (!ep->stopped) {
			/* resume current request, or start new one */
			if (req)
				writel (dmactl, &ep->dma->dmactl);
			else
				start_dma (ep, list_entry (ep->queue.next,
					struct net2280_request, queue));
		}
	}

	spin_unlock_irqrestore (&ep->dev->lock, flags);
	return 0;
}

/*-------------------------------------------------------------------------*/

static int net2280_fifo_status (struct usb_ep *_ep);

static int
net2280_set_halt (struct usb_ep *_ep, int value)
{
	struct net2280_ep	*ep;
	unsigned long		flags;
	int			retval = 0;

	ep = container_of (_ep, struct net2280_ep, ep);
	if (!_ep || (!ep->desc && ep->num != 0))
		return -EINVAL;
	if (!ep->dev->driver || ep->dev->gadget.speed == USB_SPEED_UNKNOWN)
		return -ESHUTDOWN;
	if (ep->desc /* not ep0 */ && (ep->desc->bmAttributes & 0x03)
						== USB_ENDPOINT_XFER_ISOC)
		return -EINVAL;

	spin_lock_irqsave (&ep->dev->lock, flags);
	if (!list_empty (&ep->queue))
		retval = -EAGAIN;
	else if (ep->is_in && value && net2280_fifo_status (_ep) != 0)
		retval = -EAGAIN;
	else {
		VDEBUG (ep->dev, "%s %s halt\n", _ep->name,
				value ? "set" : "clear");
		/* set/clear, then synch memory views with the device */
		if (value) {
			if (ep->num == 0)
				ep->dev->protocol_stall = 1;
			else
				set_halt (ep);
		} else
			clear_halt (ep);
		(void) readl (&ep->regs->ep_rsp);
	}
	spin_unlock_irqrestore (&ep->dev->lock, flags);

	return retval;
}

static int
net2280_fifo_status (struct usb_ep *_ep)
{
	struct net2280_ep	*ep;
	u32			avail;

	ep = container_of (_ep, struct net2280_ep, ep);
	if (!_ep || (!ep->desc && ep->num != 0))
		return -ENODEV;
	if (!ep->dev->driver || ep->dev->gadget.speed == USB_SPEED_UNKNOWN)
		return -ESHUTDOWN;

	avail = readl (&ep->regs->ep_avail) & ((1 << 12) - 1);
	if (avail > ep->fifo_size)
		return -EOVERFLOW;
	if (ep->is_in)
		avail = ep->fifo_size - avail;
	return avail;
}

static void
net2280_fifo_flush (struct usb_ep *_ep)
{
	struct net2280_ep	*ep;

	ep = container_of (_ep, struct net2280_ep, ep);
	if (!_ep || (!ep->desc && ep->num != 0))
		return;
	if (!ep->dev->driver || ep->dev->gadget.speed == USB_SPEED_UNKNOWN)
		return;

	writel ((1 << FIFO_FLUSH), &ep->regs->ep_stat);
	(void) readl (&ep->regs->ep_rsp);
}

static struct usb_ep_ops net2280_ep_ops = {
	.enable		= net2280_enable,
	.disable	= net2280_disable,

	.alloc_request	= net2280_alloc_request,
	.free_request	= net2280_free_request,

	.alloc_buffer	= net2280_alloc_buffer,
	.free_buffer	= net2280_free_buffer,

	.queue		= net2280_queue,
	.dequeue	= net2280_dequeue,

	.set_halt	= net2280_set_halt,
	.fifo_status	= net2280_fifo_status,
	.fifo_flush	= net2280_fifo_flush,
};

/*-------------------------------------------------------------------------*/

static int net2280_get_frame (struct usb_gadget *_gadget)
{
	struct net2280		*dev;
	unsigned long		flags;
	u16			retval;

	if (!_gadget)
		return -ENODEV;
	dev = container_of (_gadget, struct net2280, gadget);
	spin_lock_irqsave (&dev->lock, flags);
	retval = get_idx_reg (dev->regs, REG_FRAME) & 0x03ff;
	spin_unlock_irqrestore (&dev->lock, flags);
	return retval;
}

static int net2280_wakeup (struct usb_gadget *_gadget)
{
	struct net2280		*dev;
	u32			tmp;
	unsigned long		flags;

	if (!_gadget)
		return 0;
	dev = container_of (_gadget, struct net2280, gadget);

	spin_lock_irqsave (&dev->lock, flags);
	tmp = readl (&dev->usb->usbctl);
	if (tmp & (1 << DEVICE_REMOTE_WAKEUP_ENABLE))
		writel (1 << GENERATE_RESUME, &dev->usb->usbstat);
	spin_unlock_irqrestore (&dev->lock, flags);

	/* pci writes may still be posted */
	return 0;
}

static int net2280_set_selfpowered (struct usb_gadget *_gadget, int value)
{
	struct net2280		*dev;
	u32			tmp;
	unsigned long		flags;

	if (!_gadget)
		return 0;
	dev = container_of (_gadget, struct net2280, gadget);

	spin_lock_irqsave (&dev->lock, flags);
	tmp = readl (&dev->usb->usbctl);
	if (value)
		tmp |= (1 << SELF_POWERED_STATUS);
	else
		tmp &= ~(1 << SELF_POWERED_STATUS);
	writel (tmp, &dev->usb->usbctl);
	spin_unlock_irqrestore (&dev->lock, flags);

	return 0;
}

static int net2280_pullup(struct usb_gadget *_gadget, int is_on)
{
	struct net2280  *dev;
	u32             tmp;
	unsigned long   flags;

	if (!_gadget)
		return -ENODEV;
	dev = container_of (_gadget, struct net2280, gadget);

	spin_lock_irqsave (&dev->lock, flags);
	tmp = readl (&dev->usb->usbctl);
	dev->softconnect = (is_on != 0);
	if (is_on)
		tmp |= (1 << USB_DETECT_ENABLE);
	else
		tmp &= ~(1 << USB_DETECT_ENABLE);
	writel (tmp, &dev->usb->usbctl);
	spin_unlock_irqrestore (&dev->lock, flags);

	return 0;
}

static const struct usb_gadget_ops net2280_ops = {
	.get_frame	= net2280_get_frame,
	.wakeup		= net2280_wakeup,
	.set_selfpowered = net2280_set_selfpowered,
	.pullup		= net2280_pullup,
};

/*-------------------------------------------------------------------------*/

#ifdef	CONFIG_USB_GADGET_DEBUG_FILES

/* FIXME move these into procfs, and use seq_file.
 * Sysfs _still_ doesn't behave for arbitrarily sized files,
 * and also doesn't help products using this with 2.4 kernels.
 */

/* "function" sysfs attribute */
static ssize_t
show_function (struct device *_dev, struct device_attribute *attr, char *buf)
{
	struct net2280	*dev = dev_get_drvdata (_dev);

	if (!dev->driver
			|| !dev->driver->function
			|| strlen (dev->driver->function) > PAGE_SIZE)
		return 0;
	return scnprintf (buf, PAGE_SIZE, "%s\n", dev->driver->function);
}
static DEVICE_ATTR (function, S_IRUGO, show_function, NULL);

static ssize_t
show_registers (struct device *_dev, struct device_attribute *attr, char *buf)
{
	struct net2280		*dev;
	char			*next;
	unsigned		size, t;
	unsigned long		flags;
	int			i;
	u32			t1, t2;
	const char		*s;

	dev = dev_get_drvdata (_dev);
	next = buf;
	size = PAGE_SIZE;
	spin_lock_irqsave (&dev->lock, flags);

	if (dev->driver)
		s = dev->driver->driver.name;
	else
		s = "(none)";

	/* Main Control Registers */
	t = scnprintf (next, size, "%s version " DRIVER_VERSION
			", chiprev %04x, dma %s\n\n"
			"devinit %03x fifoctl %08x gadget '%s'\n"
			"pci irqenb0 %02x irqenb1 %08x "
			"irqstat0 %04x irqstat1 %08x\n",
			driver_name, dev->chiprev,
			use_dma
				? (use_dma_chaining ? "chaining" : "enabled")
				: "disabled",
			readl (&dev->regs->devinit),
			readl (&dev->regs->fifoctl),
			s,
			readl (&dev->regs->pciirqenb0),
			readl (&dev->regs->pciirqenb1),
			readl (&dev->regs->irqstat0),
			readl (&dev->regs->irqstat1));
	size -= t;
	next += t;

	/* USB Control Registers */
	t1 = readl (&dev->usb->usbctl);
	t2 = readl (&dev->usb->usbstat);
	if (t1 & (1 << VBUS_PIN)) {
		if (t2 & (1 << HIGH_SPEED))
			s = "high speed";
		else if (dev->gadget.speed == USB_SPEED_UNKNOWN)
			s = "powered";
		else
			s = "full speed";
		/* full speed bit (6) not working?? */
	} else
			s = "not attached";
	t = scnprintf (next, size,
			"stdrsp %08x usbctl %08x usbstat %08x "
				"addr 0x%02x (%s)\n",
			readl (&dev->usb->stdrsp), t1, t2,
			readl (&dev->usb->ouraddr), s);
	size -= t;
	next += t;

	/* PCI Master Control Registers */

	/* DMA Control Registers */

	/* Configurable EP Control Registers */
	for (i = 0; i < 7; i++) {
		struct net2280_ep	*ep;

		ep = &dev->ep [i];
		if (i && !ep->desc)
			continue;

		t1 = readl (&ep->regs->ep_cfg);
		t2 = readl (&ep->regs->ep_rsp) & 0xff;
		t = scnprintf (next, size,
				"\n%s\tcfg %05x rsp (%02x) %s%s%s%s%s%s%s%s"
					"irqenb %02x\n",
				ep->ep.name, t1, t2,
				(t2 & (1 << CLEAR_NAK_OUT_PACKETS))
					? "NAK " : "",
				(t2 & (1 << CLEAR_EP_HIDE_STATUS_PHASE))
					? "hide " : "",
				(t2 & (1 << CLEAR_EP_FORCE_CRC_ERROR))
					? "CRC " : "",
				(t2 & (1 << CLEAR_INTERRUPT_MODE))
					? "interrupt " : "",
				(t2 & (1<<CLEAR_CONTROL_STATUS_PHASE_HANDSHAKE))
					? "status " : "",
				(t2 & (1 << CLEAR_NAK_OUT_PACKETS_MODE))
					? "NAKmode " : "",
				(t2 & (1 << CLEAR_ENDPOINT_TOGGLE))
					? "DATA1 " : "DATA0 ",
				(t2 & (1 << CLEAR_ENDPOINT_HALT))
					? "HALT " : "",
				readl (&ep->regs->ep_irqenb));
		size -= t;
		next += t;

		t = scnprintf (next, size,
				"\tstat %08x avail %04x "
				"(ep%d%s-%s)%s\n",
				readl (&ep->regs->ep_stat),
				readl (&ep->regs->ep_avail),
				t1 & 0x0f, DIR_STRING (t1),
				type_string (t1 >> 8),
				ep->stopped ? "*" : "");
		size -= t;
		next += t;

		if (!ep->dma)
			continue;

		t = scnprintf (next, size,
				"  dma\tctl %08x stat %08x count %08x\n"
				"\taddr %08x desc %08x\n",
				readl (&ep->dma->dmactl),
				readl (&ep->dma->dmastat),
				readl (&ep->dma->dmacount),
				readl (&ep->dma->dmaaddr),
				readl (&ep->dma->dmadesc));
		size -= t;
		next += t;

	}

	/* Indexed Registers */
		// none yet 

	/* Statistics */
	t = scnprintf (next, size, "\nirqs:  ");
	size -= t;
	next += t;
	for (i = 0; i < 7; i++) {
		struct net2280_ep	*ep;

		ep = &dev->ep [i];
		if (i && !ep->irqs)
			continue;
		t = scnprintf (next, size, " %s/%lu", ep->ep.name, ep->irqs);
		size -= t;
		next += t;

	}
	t = scnprintf (next, size, "\n");
	size -= t;
	next += t;

	spin_unlock_irqrestore (&dev->lock, flags);

	return PAGE_SIZE - size;
}
static DEVICE_ATTR (registers, S_IRUGO, show_registers, NULL);

static ssize_t
show_queues (struct device *_dev, struct device_attribute *attr, char *buf)
{
	struct net2280		*dev;
	char			*next;
	unsigned		size;
	unsigned long		flags;
	int			i;

	dev = dev_get_drvdata (_dev);
	next = buf;
	size = PAGE_SIZE;
	spin_lock_irqsave (&dev->lock, flags);

	for (i = 0; i < 7; i++) {
		struct net2280_ep		*ep = &dev->ep [i];
		struct net2280_request		*req;
		int				t;

		if (i != 0) {
			const struct usb_endpoint_descriptor	*d;

			d = ep->desc;
			if (!d)
				continue;
			t = d->bEndpointAddress;
			t = scnprintf (next, size,
				"\n%s (ep%d%s-%s) max %04x %s fifo %d\n",
				ep->ep.name, t & USB_ENDPOINT_NUMBER_MASK,
				(t & USB_DIR_IN) ? "in" : "out",
				({ char *val;
				 switch (d->bmAttributes & 0x03) {
				 case USB_ENDPOINT_XFER_BULK:
				 	val = "bulk"; break;
				 case USB_ENDPOINT_XFER_INT:
				 	val = "intr"; break;
				 default:
				 	val = "iso"; break;
				 }; val; }),
				le16_to_cpu (d->wMaxPacketSize) & 0x1fff,
				ep->dma ? "dma" : "pio", ep->fifo_size
				);
		} else /* ep0 should only have one transfer queued */
			t = scnprintf (next, size, "ep0 max 64 pio %s\n",
					ep->is_in ? "in" : "out");
		if (t <= 0 || t > size)
			goto done;
		size -= t;
		next += t;

		if (list_empty (&ep->queue)) {
			t = scnprintf (next, size, "\t(nothing queued)\n");
			if (t <= 0 || t > size)
				goto done;
			size -= t;
			next += t;
			continue;
		}
		list_for_each_entry (req, &ep->queue, queue) {
			if (ep->dma && req->td_dma == readl (&ep->dma->dmadesc))
				t = scnprintf (next, size,
					"\treq %p len %d/%d "
					"buf %p (dmacount %08x)\n",
					&req->req, req->req.actual,
					req->req.length, req->req.buf,
					readl (&ep->dma->dmacount));
			else
				t = scnprintf (next, size,
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
				t = scnprintf (next, size, "\t    td %08x "
					" count %08x buf %08x desc %08x\n",
					(u32) req->td_dma,
					le32_to_cpu (td->dmacount),
					le32_to_cpu (td->dmaaddr),
					le32_to_cpu (td->dmadesc));
				if (t <= 0 || t > size)
					goto done;
				size -= t;
				next += t;
			}
		}
	}

done:
	spin_unlock_irqrestore (&dev->lock, flags);
	return PAGE_SIZE - size;
}
static DEVICE_ATTR (queues, S_IRUGO, show_queues, NULL);


#else

#define device_create_file(a,b)	do {} while (0)
#define device_remove_file	device_create_file

#endif

/*-------------------------------------------------------------------------*/

/* another driver-specific mode might be a request type doing dma
 * to/from another device fifo instead of to/from memory.
 */

static void set_fifo_mode (struct net2280 *dev, int mode)
{
	/* keeping high bits preserves BAR2 */
	writel ((0xffff << PCI_BASE2_RANGE) | mode, &dev->regs->fifoctl);

	/* always ep-{a,b,e,f} ... maybe not ep-c or ep-d */
	INIT_LIST_HEAD (&dev->gadget.ep_list);
	list_add_tail (&dev->ep [1].ep.ep_list, &dev->gadget.ep_list);
	list_add_tail (&dev->ep [2].ep.ep_list, &dev->gadget.ep_list);
	switch (mode) {
	case 0:
		list_add_tail (&dev->ep [3].ep.ep_list, &dev->gadget.ep_list);
		list_add_tail (&dev->ep [4].ep.ep_list, &dev->gadget.ep_list);
		dev->ep [1].fifo_size = dev->ep [2].fifo_size = 1024;
		break;
	case 1:
		dev->ep [1].fifo_size = dev->ep [2].fifo_size = 2048;
		break;
	case 2:
		list_add_tail (&dev->ep [3].ep.ep_list, &dev->gadget.ep_list);
		dev->ep [1].fifo_size = 2048;
		dev->ep [2].fifo_size = 1024;
		break;
	}
	/* fifo sizes for ep0, ep-c, ep-d, ep-e, and ep-f never change */
	list_add_tail (&dev->ep [5].ep.ep_list, &dev->gadget.ep_list);
	list_add_tail (&dev->ep [6].ep.ep_list, &dev->gadget.ep_list);
}

/* just declare this in any driver that really need it */
extern int net2280_set_fifo_mode (struct usb_gadget *gadget, int mode);

/**
 * net2280_set_fifo_mode - change allocation of fifo buffers
 * @gadget: access to the net2280 device that will be updated
 * @mode: 0 for default, four 1kB buffers (ep-a through ep-d);
 * 	1 for two 2kB buffers (ep-a and ep-b only);
 * 	2 for one 2kB buffer (ep-a) and two 1kB ones (ep-b, ep-c).
 *
 * returns zero on success, else negative errno.  when this succeeds,
 * the contents of gadget->ep_list may have changed.
 *
 * you may only call this function when endpoints a-d are all disabled.
 * use it whenever extra hardware buffering can help performance, such
 * as before enabling "high bandwidth" interrupt endpoints that use
 * maxpacket bigger than 512 (when double buffering would otherwise
 * be unavailable).
 */
int net2280_set_fifo_mode (struct usb_gadget *gadget, int mode)
{
	int			i;
	struct net2280		*dev;
	int			status = 0;
	unsigned long		flags;

	if (!gadget)
		return -ENODEV;
	dev = container_of (gadget, struct net2280, gadget);

	spin_lock_irqsave (&dev->lock, flags);

	for (i = 1; i <= 4; i++)
		if (dev->ep [i].desc) {
			status = -EINVAL;
			break;
		}
	if (mode < 0 || mode > 2)
		status = -EINVAL;
	if (status == 0)
		set_fifo_mode (dev, mode);
	spin_unlock_irqrestore (&dev->lock, flags);

	if (status == 0) {
		if (mode == 1)
			DEBUG (dev, "fifo:  ep-a 2K, ep-b 2K\n");
		else if (mode == 2)
			DEBUG (dev, "fifo:  ep-a 2K, ep-b 1K, ep-c 1K\n");
		/* else all are 1K */
	}
	return status;
}
EXPORT_SYMBOL (net2280_set_fifo_mode);

/*-------------------------------------------------------------------------*/

/* keeping it simple:
 * - one bus driver, initted first;
 * - one function driver, initted second
 *
 * most of the work to support multiple net2280 controllers would
 * be to associate this gadget driver (yes?) with all of them, or
 * perhaps to bind specific drivers to specific devices.
 */

static struct net2280	*the_controller;

static void usb_reset (struct net2280 *dev)
{
	u32	tmp;

	dev->gadget.speed = USB_SPEED_UNKNOWN;
	(void) readl (&dev->usb->usbctl);

	net2280_led_init (dev);

	/* disable automatic responses, and irqs */
	writel (0, &dev->usb->stdrsp);
	writel (0, &dev->regs->pciirqenb0);
	writel (0, &dev->regs->pciirqenb1);

	/* clear old dma and irq state */
	for (tmp = 0; tmp < 4; tmp++) {
		struct net2280_ep	*ep = &dev->ep [tmp + 1];

		if (ep->dma)
			abort_dma (ep);
	}
	writel (~0, &dev->regs->irqstat0),
	writel (~(1 << SUSPEND_REQUEST_INTERRUPT), &dev->regs->irqstat1),

	/* reset, and enable pci */
	tmp = readl (&dev->regs->devinit)
		| (1 << PCI_ENABLE)
		| (1 << FIFO_SOFT_RESET)
		| (1 << USB_SOFT_RESET)
		| (1 << M8051_RESET);
	writel (tmp, &dev->regs->devinit);

	/* standard fifo and endpoint allocations */
	set_fifo_mode (dev, (fifo_mode <= 2) ? fifo_mode : 0);
}

static void usb_reinit (struct net2280 *dev)
{
	u32	tmp;
	int	init_dma;

	/* use_dma changes are ignored till next device re-init */
	init_dma = use_dma;

	/* basic endpoint init */
	for (tmp = 0; tmp < 7; tmp++) {
		struct net2280_ep	*ep = &dev->ep [tmp];

		ep->ep.name = ep_name [tmp];
		ep->dev = dev;
		ep->num = tmp;

		if (tmp > 0 && tmp <= 4) {
			ep->fifo_size = 1024;
			if (init_dma)
				ep->dma = &dev->dma [tmp - 1];
		} else
			ep->fifo_size = 64;
		ep->regs = &dev->epregs [tmp];
		ep_reset (dev->regs, ep);
	}
	dev->ep [0].ep.maxpacket = 64;
	dev->ep [5].ep.maxpacket = 64;
	dev->ep [6].ep.maxpacket = 64;

	dev->gadget.ep0 = &dev->ep [0].ep;
	dev->ep [0].stopped = 0;
	INIT_LIST_HEAD (&dev->gadget.ep0->ep_list);

	/* we want to prevent lowlevel/insecure access from the USB host,
	 * but erratum 0119 means this enable bit is ignored
	 */
	for (tmp = 0; tmp < 5; tmp++)
		writel (EP_DONTUSE, &dev->dep [tmp].dep_cfg);
}

static void ep0_start (struct net2280 *dev)
{
	writel (  (1 << CLEAR_EP_HIDE_STATUS_PHASE)
		| (1 << CLEAR_NAK_OUT_PACKETS)
		| (1 << CLEAR_CONTROL_STATUS_PHASE_HANDSHAKE)
		, &dev->epregs [0].ep_rsp);

	/*
	 * hardware optionally handles a bunch of standard requests
	 * that the API hides from drivers anyway.  have it do so.
	 * endpoint status/features are handled in software, to
	 * help pass tests for some dubious behavior.
	 */
	writel (  (1 << SET_TEST_MODE)
		| (1 << SET_ADDRESS)
		| (1 << DEVICE_SET_CLEAR_DEVICE_REMOTE_WAKEUP)
		| (1 << GET_DEVICE_STATUS)
		| (1 << GET_INTERFACE_STATUS)
		, &dev->usb->stdrsp);
	writel (  (1 << USB_ROOT_PORT_WAKEUP_ENABLE)
		| (1 << SELF_POWERED_USB_DEVICE)
		| (1 << REMOTE_WAKEUP_SUPPORT)
		| (dev->softconnect << USB_DETECT_ENABLE)
		| (1 << SELF_POWERED_STATUS)
		, &dev->usb->usbctl);

	/* enable irqs so we can see ep0 and general operation  */
	writel (  (1 << SETUP_PACKET_INTERRUPT_ENABLE)
		| (1 << ENDPOINT_0_INTERRUPT_ENABLE)
		, &dev->regs->pciirqenb0);
	writel (  (1 << PCI_INTERRUPT_ENABLE)
		| (1 << PCI_MASTER_ABORT_RECEIVED_INTERRUPT_ENABLE)
		| (1 << PCI_TARGET_ABORT_RECEIVED_INTERRUPT_ENABLE)
		| (1 << PCI_RETRY_ABORT_INTERRUPT_ENABLE)
		| (1 << VBUS_INTERRUPT_ENABLE)
		| (1 << ROOT_PORT_RESET_INTERRUPT_ENABLE)
		| (1 << SUSPEND_REQUEST_CHANGE_INTERRUPT_ENABLE)
		, &dev->regs->pciirqenb1);

	/* don't leave any writes posted */
	(void) readl (&dev->usb->usbctl);
}

/* when a driver is successfully registered, it will receive
 * control requests including set_configuration(), which enables
 * non-control requests.  then usb traffic follows until a
 * disconnect is reported.  then a host may connect again, or
 * the driver might get unbound.
 */
int usb_gadget_register_driver (struct usb_gadget_driver *driver)
{
	struct net2280		*dev = the_controller;
	int			retval;
	unsigned		i;

	/* insist on high speed support from the driver, since
	 * (dev->usb->xcvrdiag & FORCE_FULL_SPEED_MODE)
	 * "must not be used in normal operation"
	 */
	if (!driver
			|| driver->speed != USB_SPEED_HIGH
			|| !driver->bind
			|| !driver->unbind
			|| !driver->setup)
		return -EINVAL;
	if (!dev)
		return -ENODEV;
	if (dev->driver)
		return -EBUSY;

	for (i = 0; i < 7; i++)
		dev->ep [i].irqs = 0;

	/* hook up the driver ... */
	dev->softconnect = 1;
	driver->driver.bus = NULL;
	dev->driver = driver;
	dev->gadget.dev.driver = &driver->driver;
	retval = driver->bind (&dev->gadget);
	if (retval) {
		DEBUG (dev, "bind to driver %s --> %d\n",
				driver->driver.name, retval);
		dev->driver = NULL;
		dev->gadget.dev.driver = NULL;
		return retval;
	}

	device_create_file (&dev->pdev->dev, &dev_attr_function);
	device_create_file (&dev->pdev->dev, &dev_attr_queues);

	/* ... then enable host detection and ep0; and we're ready
	 * for set_configuration as well as eventual disconnect.
	 */
	net2280_led_active (dev, 1);
	ep0_start (dev);

	DEBUG (dev, "%s ready, usbctl %08x stdrsp %08x\n",
			driver->driver.name,
			readl (&dev->usb->usbctl),
			readl (&dev->usb->stdrsp));

	/* pci writes may still be posted */
	return 0;
}
EXPORT_SYMBOL (usb_gadget_register_driver);

static void
stop_activity (struct net2280 *dev, struct usb_gadget_driver *driver)
{
	int			i;

	/* don't disconnect if it's not connected */
	if (dev->gadget.speed == USB_SPEED_UNKNOWN)
		driver = NULL;

	/* stop hardware; prevent new request submissions;
	 * and kill any outstanding requests.
	 */
	usb_reset (dev);
	for (i = 0; i < 7; i++)
		nuke (&dev->ep [i]);

	/* report disconnect; the driver is already quiesced */
	if (driver) {
		spin_unlock (&dev->lock);
		driver->disconnect (&dev->gadget);
		spin_lock (&dev->lock);
	}

	usb_reinit (dev);
}

int usb_gadget_unregister_driver (struct usb_gadget_driver *driver)
{
	struct net2280	*dev = the_controller;
	unsigned long	flags;

	if (!dev)
		return -ENODEV;
	if (!driver || driver != dev->driver)
		return -EINVAL;

	spin_lock_irqsave (&dev->lock, flags);
	stop_activity (dev, driver);
	spin_unlock_irqrestore (&dev->lock, flags);

	net2280_pullup (&dev->gadget, 0);

	driver->unbind (&dev->gadget);
	dev->gadget.dev.driver = NULL;
	dev->driver = NULL;

	net2280_led_active (dev, 0);
	device_remove_file (&dev->pdev->dev, &dev_attr_function);
	device_remove_file (&dev->pdev->dev, &dev_attr_queues);

	DEBUG (dev, "unregistered driver '%s'\n", driver->driver.name);
	return 0;
}
EXPORT_SYMBOL (usb_gadget_unregister_driver);


/*-------------------------------------------------------------------------*/

/* handle ep0, ep-e, ep-f with 64 byte packets: packet per irq.
 * also works for dma-capable endpoints, in pio mode or just
 * to manually advance the queue after short OUT transfers.
 */
static void handle_ep_small (struct net2280_ep *ep)
{
	struct net2280_request	*req;
	u32			t;
	/* 0 error, 1 mid-data, 2 done */
	int			mode = 1;

	if (!list_empty (&ep->queue))
		req = list_entry (ep->queue.next,
			struct net2280_request, queue);
	else
		req = NULL;

	/* ack all, and handle what we care about */
	t = readl (&ep->regs->ep_stat);
	ep->irqs++;
#if 0
	VDEBUG (ep->dev, "%s ack ep_stat %08x, req %p\n",
			ep->ep.name, t, req ? &req->req : 0);
#endif
	if (!ep->is_in || ep->dev->pdev->device == 0x2280)
		writel (t & ~(1 << NAK_OUT_PACKETS), &ep->regs->ep_stat);
	else
		/* Added for 2282 */
		writel (t, &ep->regs->ep_stat);

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
	if (unlikely (ep->num == 0)) {
		if (ep->is_in) {
			/* status; stop NAKing */
			if (t & (1 << DATA_OUT_PING_TOKEN_INTERRUPT)) {
				if (ep->dev->protocol_stall) {
					ep->stopped = 1;
					set_halt (ep);
				}
				if (!req)
					allow_status (ep);
				mode = 2;
			/* reply to extra IN data tokens with a zlp */
			} else if (t & (1 << DATA_IN_TOKEN_INTERRUPT)) {
				if (ep->dev->protocol_stall) {
					ep->stopped = 1;
					set_halt (ep);
					mode = 2;
				} else if (!req && ep->stopped)
					write_fifo (ep, NULL);
			}
		} else {
			/* status; stop NAKing */
			if (t & (1 << DATA_IN_TOKEN_INTERRUPT)) {
				if (ep->dev->protocol_stall) {
					ep->stopped = 1;
					set_halt (ep);
				}
				mode = 2;
			/* an extra OUT token is an error */
			} else if (((t & (1 << DATA_OUT_PING_TOKEN_INTERRUPT))
					&& req
					&& req->req.actual == req->req.length)
					|| !req) {
				ep->dev->protocol_stall = 1;
				set_halt (ep);
				ep->stopped = 1;
				if (req)
					done (ep, req, -EOVERFLOW);
				req = NULL;
			}
		}
	}

	if (unlikely (!req))
		return;

	/* manual DMA queue advance after short OUT */
	if (likely (ep->dma != 0)) {
		if (t & (1 << SHORT_PACKET_TRANSFERRED_INTERRUPT)) {
			u32	count;
			int	stopped = ep->stopped;

			/* TRANSFERRED works around OUT_DONE erratum 0112.
			 * we expect (N <= maxpacket) bytes; host wrote M.
			 * iff (M < N) we won't ever see a DMA interrupt.
			 */
			ep->stopped = 1;
			for (count = 0; ; t = readl (&ep->regs->ep_stat)) {

				/* any preceding dma transfers must finish.
				 * dma handles (M >= N), may empty the queue
				 */
				scan_dma_completions (ep);
				if (unlikely (list_empty (&ep->queue)
						|| ep->out_overflow)) {
					req = NULL;
					break;
				}
				req = list_entry (ep->queue.next,
					struct net2280_request, queue);

				/* here either (M < N), a "real" short rx;
				 * or (M == N) and the queue didn't empty
				 */
				if (likely (t & (1 << FIFO_EMPTY))) {
					count = readl (&ep->dma->dmacount);
					count &= DMA_BYTE_COUNT_MASK;
					if (readl (&ep->dma->dmadesc)
							!= req->td_dma)
						req = NULL;
					break;
				}
				udelay(1);
			}

			/* stop DMA, leave ep NAKing */
			writel ((1 << DMA_ABORT), &ep->dma->dmastat);
			spin_stop_dma (ep->dma);

			if (likely (req)) {
				req->td->dmacount = 0;
				t = readl (&ep->regs->ep_avail);
				dma_done (ep, req, count,
					(ep->out_overflow || t) ? -EOVERFLOW : 0);
			}

			/* also flush to prevent erratum 0106 trouble */
			if (unlikely (ep->out_overflow
					|| (ep->dev->chiprev == 0x0100
						&& ep->dev->gadget.speed
							== USB_SPEED_FULL))) {
				out_flush (ep);
				ep->out_overflow = 0;
			}

			/* (re)start dma if needed, stop NAKing */
			ep->stopped = stopped;
			if (!list_empty (&ep->queue))
				restart_dma (ep);
		} else
			DEBUG (ep->dev, "%s dma ep_stat %08x ??\n",
					ep->ep.name, t);
		return;

	/* data packet(s) received (in the fifo, OUT) */
	} else if (t & (1 << DATA_PACKET_RECEIVED_INTERRUPT)) {
		if (read_fifo (ep, req) && ep->num != 0)
			mode = 2;

	/* data packet(s) transmitted (IN) */
	} else if (t & (1 << DATA_PACKET_TRANSMITTED_INTERRUPT)) {
		unsigned	len;

		len = req->req.length - req->req.actual;
		if (len > ep->ep.maxpacket)
			len = ep->ep.maxpacket;
		req->req.actual += len;

		/* if we wrote it all, we're usually done */
		if (req->req.actual == req->req.length) {
			if (ep->num == 0) {
				/* wait for control status */
				if (mode != 2)
					req = NULL;
			} else if (!req->req.zero || len != ep->ep.maxpacket)
				mode = 2;
		}

	/* there was nothing to do ...  */
	} else if (mode == 1)
		return;

	/* done */
	if (mode == 2) {
		/* stream endpoints often resubmit/unlink in completion */
		done (ep, req, 0);

		/* maybe advance queue to next request */
		if (ep->num == 0) {
			/* NOTE:  net2280 could let gadget driver start the
			 * status stage later. since not all controllers let
			 * them control that, the api doesn't (yet) allow it.
			 */
			if (!ep->stopped)
				allow_status (ep);
			req = NULL;
		} else {
			if (!list_empty (&ep->queue) && !ep->stopped)
				req = list_entry (ep->queue.next,
					struct net2280_request, queue);
			else
				req = NULL;
			if (req && !ep->is_in)
				stop_out_naking (ep);
		}
	}

	/* is there a buffer for the next packet?
	 * for best streaming performance, make sure there is one.
	 */
	if (req && !ep->stopped) {

		/* load IN fifo with next packet (may be zlp) */
		if (t & (1 << DATA_PACKET_TRANSMITTED_INTERRUPT))
			write_fifo (ep, &req->req);
	}
}

static struct net2280_ep *
get_ep_by_addr (struct net2280 *dev, u16 wIndex)
{
	struct net2280_ep	*ep;

	if ((wIndex & USB_ENDPOINT_NUMBER_MASK) == 0)
		return &dev->ep [0];
	list_for_each_entry (ep, &dev->gadget.ep_list, ep.ep_list) {
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

static void handle_stat0_irqs (struct net2280 *dev, u32 stat)
{
	struct net2280_ep	*ep;
	u32			num, scratch;

	/* most of these don't need individual acks */
	stat &= ~(1 << INTA_ASSERTED);
	if (!stat)
		return;
	// DEBUG (dev, "irqstat0 %04x\n", stat);

	/* starting a control request? */
	if (unlikely (stat & (1 << SETUP_PACKET_INTERRUPT))) {
		union {
			u32			raw [2];
			struct usb_ctrlrequest	r;
		} u;
		int				tmp;
		struct net2280_request		*req;

		if (dev->gadget.speed == USB_SPEED_UNKNOWN) {
			if (readl (&dev->usb->usbstat) & (1 << HIGH_SPEED))
				dev->gadget.speed = USB_SPEED_HIGH;
			else
				dev->gadget.speed = USB_SPEED_FULL;
			net2280_led_speed (dev, dev->gadget.speed);
			DEBUG (dev, "%s speed\n",
				(dev->gadget.speed == USB_SPEED_HIGH)
					? "high" : "full");
		}

		ep = &dev->ep [0];
		ep->irqs++;

		/* make sure any leftover request state is cleared */
		stat &= ~(1 << ENDPOINT_0_INTERRUPT);
		while (!list_empty (&ep->queue)) {
			req = list_entry (ep->queue.next,
					struct net2280_request, queue);
			done (ep, req, (req->req.actual == req->req.length)
						? 0 : -EPROTO);
		}
		ep->stopped = 0;
		dev->protocol_stall = 0;

		if (ep->dev->pdev->device == 0x2280)
			tmp = (1 << FIFO_OVERFLOW)
				| (1 << FIFO_UNDERFLOW);
		else
			tmp = 0;

		writel (tmp | (1 << TIMEOUT)
			| (1 << USB_STALL_SENT)
			| (1 << USB_IN_NAK_SENT)
			| (1 << USB_IN_ACK_RCVD)
			| (1 << USB_OUT_PING_NAK_SENT)
			| (1 << USB_OUT_ACK_SENT)
			| (1 << SHORT_PACKET_OUT_DONE_INTERRUPT)
			| (1 << SHORT_PACKET_TRANSFERRED_INTERRUPT)
			| (1 << DATA_PACKET_RECEIVED_INTERRUPT)
			| (1 << DATA_PACKET_TRANSMITTED_INTERRUPT)
			| (1 << DATA_OUT_PING_TOKEN_INTERRUPT)
			| (1 << DATA_IN_TOKEN_INTERRUPT)
			, &ep->regs->ep_stat);
		u.raw [0] = readl (&dev->usb->setup0123);
		u.raw [1] = readl (&dev->usb->setup4567);
		
		cpu_to_le32s (&u.raw [0]);
		cpu_to_le32s (&u.raw [1]);

		tmp = 0;

#define	w_value		le16_to_cpup (&u.r.wValue)
#define	w_index		le16_to_cpup (&u.r.wIndex)
#define	w_length	le16_to_cpup (&u.r.wLength)

		/* ack the irq */
		writel (1 << SETUP_PACKET_INTERRUPT, &dev->regs->irqstat0);
		stat ^= (1 << SETUP_PACKET_INTERRUPT);

		/* watch control traffic at the token level, and force
		 * synchronization before letting the status stage happen.
		 * FIXME ignore tokens we'll NAK, until driver responds.
		 * that'll mean a lot less irqs for some drivers.
		 */
		ep->is_in = (u.r.bRequestType & USB_DIR_IN) != 0;
		if (ep->is_in) {
			scratch = (1 << DATA_PACKET_TRANSMITTED_INTERRUPT)
				| (1 << DATA_OUT_PING_TOKEN_INTERRUPT)
				| (1 << DATA_IN_TOKEN_INTERRUPT);
			stop_out_naking (ep);
		} else
			scratch = (1 << DATA_PACKET_RECEIVED_INTERRUPT)
				| (1 << DATA_OUT_PING_TOKEN_INTERRUPT)
				| (1 << DATA_IN_TOKEN_INTERRUPT);
		writel (scratch, &dev->epregs [0].ep_irqenb);

		/* we made the hardware handle most lowlevel requests;
		 * everything else goes uplevel to the gadget code.
		 */
		switch (u.r.bRequest) {
		case USB_REQ_GET_STATUS: {
			struct net2280_ep	*e;
			__le32			status;

			/* hw handles device and interface status */
			if (u.r.bRequestType != (USB_DIR_IN|USB_RECIP_ENDPOINT))
				goto delegate;
			if ((e = get_ep_by_addr (dev, w_index)) == 0
					|| w_length > 2)
				goto do_stall;

			if (readl (&e->regs->ep_rsp)
					& (1 << SET_ENDPOINT_HALT))
				status = __constant_cpu_to_le32 (1);
			else
				status = __constant_cpu_to_le32 (0);

			/* don't bother with a request object! */
			writel (0, &dev->epregs [0].ep_irqenb);
			set_fifo_bytecount (ep, w_length);
			writel ((__force u32)status, &dev->epregs [0].ep_data);
			allow_status (ep);
			VDEBUG (dev, "%s stat %02x\n", ep->ep.name, status);
			goto next_endpoints;
			}
			break;
		case USB_REQ_CLEAR_FEATURE: {
			struct net2280_ep	*e;

			/* hw handles device features */
			if (u.r.bRequestType != USB_RECIP_ENDPOINT)
				goto delegate;
			if (w_value != USB_ENDPOINT_HALT
					|| w_length != 0)
				goto do_stall;
			if ((e = get_ep_by_addr (dev, w_index)) == 0)
				goto do_stall;
			clear_halt (e);
			allow_status (ep);
			VDEBUG (dev, "%s clear halt\n", ep->ep.name);
			goto next_endpoints;
			}
			break;
		case USB_REQ_SET_FEATURE: {
			struct net2280_ep	*e;

			/* hw handles device features */
			if (u.r.bRequestType != USB_RECIP_ENDPOINT)
				goto delegate;
			if (w_value != USB_ENDPOINT_HALT
					|| w_length != 0)
				goto do_stall;
			if ((e = get_ep_by_addr (dev, w_index)) == 0)
				goto do_stall;
			set_halt (e);
			allow_status (ep);
			VDEBUG (dev, "%s set halt\n", ep->ep.name);
			goto next_endpoints;
			}
			break;
		default:
delegate:
			VDEBUG (dev, "setup %02x.%02x v%04x i%04x l%04x"
				"ep_cfg %08x\n",
				u.r.bRequestType, u.r.bRequest,
				w_value, w_index, w_length,
				readl (&ep->regs->ep_cfg));
			spin_unlock (&dev->lock);
			tmp = dev->driver->setup (&dev->gadget, &u.r);
			spin_lock (&dev->lock);
		}

		/* stall ep0 on error */
		if (tmp < 0) {
do_stall:
			VDEBUG (dev, "req %02x.%02x protocol STALL; stat %d\n",
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
	/* endpoint data irq ? */
	scratch = stat & 0x7f;
	stat &= ~0x7f;
	for (num = 0; scratch; num++) {
		u32		t;

		/* do this endpoint's FIFO and queue need tending? */
		t = 1 << num;
		if ((scratch & t) == 0)
			continue;
		scratch ^= t;

		ep = &dev->ep [num];
		handle_ep_small (ep);
	}

	if (stat)
		DEBUG (dev, "unhandled irqstat0 %08x\n", stat);
}

#define DMA_INTERRUPTS ( \
		  (1 << DMA_D_INTERRUPT) \
		| (1 << DMA_C_INTERRUPT) \
		| (1 << DMA_B_INTERRUPT) \
		| (1 << DMA_A_INTERRUPT))
#define	PCI_ERROR_INTERRUPTS ( \
		  (1 << PCI_MASTER_ABORT_RECEIVED_INTERRUPT) \
		| (1 << PCI_TARGET_ABORT_RECEIVED_INTERRUPT) \
		| (1 << PCI_RETRY_ABORT_INTERRUPT))

static void handle_stat1_irqs (struct net2280 *dev, u32 stat)
{
	struct net2280_ep	*ep;
	u32			tmp, num, mask, scratch;

	/* after disconnect there's nothing else to do! */
	tmp = (1 << VBUS_INTERRUPT) | (1 << ROOT_PORT_RESET_INTERRUPT);
	mask = (1 << HIGH_SPEED) | (1 << FULL_SPEED);

	/* VBUS disconnect is indicated by VBUS_PIN and VBUS_INTERRUPT set.
	 * Root Port Reset is indicated by ROOT_PORT_RESET_INTERRRUPT set and
	 * both HIGH_SPEED and FULL_SPEED clear (as ROOT_PORT_RESET_INTERRUPT 
	 * only indicates a change in the reset state).
	 */
	if (stat & tmp) {
		writel (tmp, &dev->regs->irqstat1);
		if ((((stat & (1 << ROOT_PORT_RESET_INTERRUPT)) && 
				((readl (&dev->usb->usbstat) & mask) == 0))
				|| ((readl (&dev->usb->usbctl) & (1 << VBUS_PIN)) == 0) 
			    ) && ( dev->gadget.speed != USB_SPEED_UNKNOWN)) {
			DEBUG (dev, "disconnect %s\n",
					dev->driver->driver.name);
			stop_activity (dev, dev->driver);
			ep0_start (dev);
			return;
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
	tmp = (1 << SUSPEND_REQUEST_CHANGE_INTERRUPT);
	if (stat & tmp) {
		writel (tmp, &dev->regs->irqstat1);
		if (stat & (1 << SUSPEND_REQUEST_INTERRUPT)) {
			if (dev->driver->suspend)
				dev->driver->suspend (&dev->gadget);
			if (!enable_suspend)
				stat &= ~(1 << SUSPEND_REQUEST_INTERRUPT);
		} else {
			if (dev->driver->resume)
				dev->driver->resume (&dev->gadget);
			/* at high speed, note erratum 0133 */
		}
		stat &= ~tmp;
	}

	/* clear any other status/irqs */
	if (stat)
		writel (stat, &dev->regs->irqstat1);

	/* some status we can just ignore */
	if (dev->pdev->device == 0x2280)
		stat &= ~((1 << CONTROL_STATUS_INTERRUPT)
			  | (1 << SUSPEND_REQUEST_INTERRUPT)
			  | (1 << RESUME_INTERRUPT)
			  | (1 << SOF_INTERRUPT));
	else
		stat &= ~((1 << CONTROL_STATUS_INTERRUPT)
			  | (1 << RESUME_INTERRUPT)
			  | (1 << SOF_DOWN_INTERRUPT)
			  | (1 << SOF_INTERRUPT));

	if (!stat)
		return;
	// DEBUG (dev, "irqstat1 %08x\n", stat);

	/* DMA status, for ep-{a,b,c,d} */
	scratch = stat & DMA_INTERRUPTS;
	stat &= ~DMA_INTERRUPTS;
	scratch >>= 9;
	for (num = 0; scratch; num++) {
		struct net2280_dma_regs	__iomem *dma;

		tmp = 1 << num;
		if ((tmp & scratch) == 0)
			continue;
		scratch ^= tmp;

		ep = &dev->ep [num + 1];
		dma = ep->dma;

		if (!dma)
			continue;

		/* clear ep's dma status */
		tmp = readl (&dma->dmastat);
		writel (tmp, &dma->dmastat);

		/* chaining should stop on abort, short OUT from fifo,
		 * or (stat0 codepath) short OUT transfer.
		 */
		if (!use_dma_chaining) {
			if ((tmp & (1 << DMA_TRANSACTION_DONE_INTERRUPT))
					== 0) {
				DEBUG (ep->dev, "%s no xact done? %08x\n",
					ep->ep.name, tmp);
				continue;
			}
			stop_dma (ep->dma);
		}

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
		scan_dma_completions (ep);

		/* disable dma on inactive queues; else maybe restart */
		if (list_empty (&ep->queue)) {
			if (use_dma_chaining)
				stop_dma (ep->dma);
		} else {
			tmp = readl (&dma->dmactl);
			if (!use_dma_chaining
					|| (tmp & (1 << DMA_ENABLE)) == 0)
				restart_dma (ep);
			else if (ep->is_in && use_dma_chaining) {
				struct net2280_request	*req;
				__le32			dmacount;

				/* the descriptor at the head of the chain
				 * may still have VALID_BIT clear; that's
				 * used to trigger changing DMA_FIFO_VALIDATE
				 * (affects automagic zlp writes).
				 */
				req = list_entry (ep->queue.next,
						struct net2280_request, queue);
				dmacount = req->td->dmacount;
				dmacount &= __constant_cpu_to_le32 (
						(1 << VALID_BIT)
						| DMA_BYTE_COUNT_MASK);
				if (dmacount && (dmacount & valid_bit) == 0)
					restart_dma (ep);
			}
		}
		ep->irqs++;
	}

	/* NOTE:  there are other PCI errors we might usefully notice.
	 * if they appear very often, here's where to try recovering.
	 */
	if (stat & PCI_ERROR_INTERRUPTS) {
		ERROR (dev, "pci dma error; stat %08x\n", stat);
		stat &= ~PCI_ERROR_INTERRUPTS;
		/* these are fatal errors, but "maybe" they won't
		 * happen again ...
		 */
		stop_activity (dev, dev->driver);
		ep0_start (dev);
		stat = 0;
	}

	if (stat)
		DEBUG (dev, "unhandled irqstat1 %08x\n", stat);
}

static irqreturn_t net2280_irq (int irq, void *_dev, struct pt_regs * r)
{
	struct net2280		*dev = _dev;

	spin_lock (&dev->lock);

	/* handle disconnect, dma, and more */
	handle_stat1_irqs (dev, readl (&dev->regs->irqstat1));

	/* control requests and PIO */
	handle_stat0_irqs (dev, readl (&dev->regs->irqstat0));

	spin_unlock (&dev->lock);

	return IRQ_HANDLED;
}

/*-------------------------------------------------------------------------*/

static void gadget_release (struct device *_dev)
{
	struct net2280	*dev = dev_get_drvdata (_dev);

	kfree (dev);
}

/* tear down the binding between this driver and the pci device */

static void net2280_remove (struct pci_dev *pdev)
{
	struct net2280		*dev = pci_get_drvdata (pdev);

	/* start with the driver above us */
	if (dev->driver) {
		/* should have been done already by driver model core */
		WARN (dev, "pci remove, driver '%s' is still registered\n",
				dev->driver->driver.name);
		usb_gadget_unregister_driver (dev->driver);
	}

	/* then clean up the resources we allocated during probe() */
	net2280_led_shutdown (dev);
	if (dev->requests) {
		int		i;
		for (i = 1; i < 5; i++) {
			if (!dev->ep [i].dummy)
				continue;
			pci_pool_free (dev->requests, dev->ep [i].dummy,
					dev->ep [i].td_dma);
		}
		pci_pool_destroy (dev->requests);
	}
	if (dev->got_irq)
		free_irq (pdev->irq, dev);
	if (dev->regs)
		iounmap (dev->regs);
	if (dev->region)
		release_mem_region (pci_resource_start (pdev, 0),
				pci_resource_len (pdev, 0));
	if (dev->enabled)
		pci_disable_device (pdev);
	device_unregister (&dev->gadget.dev);
	device_remove_file (&pdev->dev, &dev_attr_registers);
	pci_set_drvdata (pdev, NULL);

	INFO (dev, "unbind\n");

	the_controller = NULL;
}

/* wrap this driver around the specified device, but
 * don't respond over USB until a gadget driver binds to us.
 */

static int net2280_probe (struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct net2280		*dev;
	unsigned long		resource, len;
	void			__iomem *base = NULL;
	int			retval, i;
	char			buf [8], *bufp;

	/* if you want to support more than one controller in a system,
	 * usb_gadget_driver_{register,unregister}() must change.
	 */
	if (the_controller) {
		dev_warn (&pdev->dev, "ignoring\n");
		return -EBUSY;
	}

	/* alloc, and start init */
	dev = kmalloc (sizeof *dev, SLAB_KERNEL);
	if (dev == NULL){
		retval = -ENOMEM;
		goto done;
	}

	memset (dev, 0, sizeof *dev);
	spin_lock_init (&dev->lock);
	dev->pdev = pdev;
	dev->gadget.ops = &net2280_ops;
	dev->gadget.is_dualspeed = 1;

	/* the "gadget" abstracts/virtualizes the controller */
	strcpy (dev->gadget.dev.bus_id, "gadget");
	dev->gadget.dev.parent = &pdev->dev;
	dev->gadget.dev.dma_mask = pdev->dev.dma_mask;
	dev->gadget.dev.release = gadget_release;
	dev->gadget.name = driver_name;

	/* now all the pci goodies ... */
	if (pci_enable_device (pdev) < 0) {
   	        retval = -ENODEV;
		goto done;
	}
	dev->enabled = 1;

	/* BAR 0 holds all the registers
	 * BAR 1 is 8051 memory; unused here (note erratum 0103)
	 * BAR 2 is fifo memory; unused here
	 */
	resource = pci_resource_start (pdev, 0);
	len = pci_resource_len (pdev, 0);
	if (!request_mem_region (resource, len, driver_name)) {
		DEBUG (dev, "controller already in use\n");
		retval = -EBUSY;
		goto done;
	}
	dev->region = 1;

	base = ioremap_nocache (resource, len);
	if (base == NULL) {
		DEBUG (dev, "can't map memory\n");
		retval = -EFAULT;
		goto done;
	}
	dev->regs = (struct net2280_regs __iomem *) base;
	dev->usb = (struct net2280_usb_regs __iomem *) (base + 0x0080);
	dev->pci = (struct net2280_pci_regs __iomem *) (base + 0x0100);
	dev->dma = (struct net2280_dma_regs __iomem *) (base + 0x0180);
	dev->dep = (struct net2280_dep_regs __iomem *) (base + 0x0200);
	dev->epregs = (struct net2280_ep_regs __iomem *) (base + 0x0300);

	/* put into initial config, link up all endpoints */
	writel (0, &dev->usb->usbctl);
	usb_reset (dev);
	usb_reinit (dev);

	/* irq setup after old hardware is cleaned up */
	if (!pdev->irq) {
		ERROR (dev, "No IRQ.  Check PCI setup!\n");
		retval = -ENODEV;
		goto done;
	}
#ifndef __sparc__
	scnprintf (buf, sizeof buf, "%d", pdev->irq);
	bufp = buf;
#else
	bufp = __irq_itoa(pdev->irq);
#endif
	if (request_irq (pdev->irq, net2280_irq, SA_SHIRQ, driver_name, dev)
			!= 0) {
		ERROR (dev, "request interrupt %s failed\n", bufp);
		retval = -EBUSY;
		goto done;
	}
	dev->got_irq = 1;

	/* DMA setup */
	/* NOTE:  we know only the 32 LSBs of dma addresses may be nonzero */
	dev->requests = pci_pool_create ("requests", pdev,
		sizeof (struct net2280_dma),
		0 /* no alignment requirements */,
		0 /* or page-crossing issues */);
	if (!dev->requests) {
		DEBUG (dev, "can't get request pool\n");
		retval = -ENOMEM;
		goto done;
	}
	for (i = 1; i < 5; i++) {
		struct net2280_dma	*td;

		td = pci_pool_alloc (dev->requests, GFP_KERNEL,
				&dev->ep [i].td_dma);
		if (!td) {
			DEBUG (dev, "can't get dummy %d\n", i);
			retval = -ENOMEM;
			goto done;
		}
		td->dmacount = 0;	/* not VALID */
		td->dmaaddr = __constant_cpu_to_le32 (DMA_ADDR_INVALID);
		td->dmadesc = td->dmaaddr;
		dev->ep [i].dummy = td;
	}

	/* enable lower-overhead pci memory bursts during DMA */
	writel ( (1 << DMA_MEMORY_WRITE_AND_INVALIDATE_ENABLE)
			// 256 write retries may not be enough...
			// | (1 << PCI_RETRY_ABORT_ENABLE)
			| (1 << DMA_READ_MULTIPLE_ENABLE)
			| (1 << DMA_READ_LINE_ENABLE)
			, &dev->pci->pcimstctl);
	/* erratum 0115 shouldn't appear: Linux inits PCI_LATENCY_TIMER */
	pci_set_master (pdev);
	pci_set_mwi (pdev);

	/* ... also flushes any posted pci writes */
	dev->chiprev = get_idx_reg (dev->regs, REG_CHIPREV) & 0xffff;

	/* done */
	pci_set_drvdata (pdev, dev);
	INFO (dev, "%s\n", driver_desc);
	INFO (dev, "irq %s, pci mem %p, chip rev %04x\n",
			bufp, base, dev->chiprev);
	INFO (dev, "version: " DRIVER_VERSION "; dma %s\n",
			use_dma
				? (use_dma_chaining ? "chaining" : "enabled")
				: "disabled");
	the_controller = dev;

	device_register (&dev->gadget.dev);
	device_create_file (&pdev->dev, &dev_attr_registers);

	return 0;

done:
	if (dev)
		net2280_remove (pdev);
	return retval;
}


/*-------------------------------------------------------------------------*/

static struct pci_device_id pci_ids [] = { {
	.class = 	((PCI_CLASS_SERIAL_USB << 8) | 0xfe),
	.class_mask = 	~0,
	.vendor =	0x17cc,
	.device =	0x2280,
	.subvendor =	PCI_ANY_ID,
	.subdevice =	PCI_ANY_ID,
}, {
	.class = 	((PCI_CLASS_SERIAL_USB << 8) | 0xfe),
	.class_mask = 	~0,
	.vendor =	0x17cc,
	.device =	0x2282,
	.subvendor =	PCI_ANY_ID,
	.subdevice =	PCI_ANY_ID,

}, { /* end: all zeroes */ }
};
MODULE_DEVICE_TABLE (pci, pci_ids);

/* pci driver glue; this is a "new style" PCI driver module */
static struct pci_driver net2280_pci_driver = {
	.name =		(char *) driver_name,
	.id_table =	pci_ids,

	.probe =	net2280_probe,
	.remove =	net2280_remove,

	/* FIXME add power management support */
};

MODULE_DESCRIPTION (DRIVER_DESC);
MODULE_AUTHOR ("David Brownell");
MODULE_LICENSE ("GPL");

static int __init init (void)
{
	if (!use_dma)
		use_dma_chaining = 0;
	return pci_register_driver (&net2280_pci_driver);
}
module_init (init);

static void __exit cleanup (void)
{
	pci_unregister_driver (&net2280_pci_driver);
}
module_exit (cleanup);
