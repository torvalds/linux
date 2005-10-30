/*
 * linux/drivers/usb/gadget/pxa2xx_udc.c
 * Intel PXA25x and IXP4xx on-chip full speed USB device controllers
 *
 * Copyright (C) 2002 Intrinsyc, Inc. (Frank Becker)
 * Copyright (C) 2003 Robert Schwebel, Pengutronix
 * Copyright (C) 2003 Benedikt Spranger, Pengutronix
 * Copyright (C) 2003 David Brownell
 * Copyright (C) 2003 Joshua Wise
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
 *
 */

#undef	DEBUG
// #define	VERBOSE	DBG_VERBOSE

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/types.h>
#include <linux/version.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/proc_fs.h>
#include <linux/mm.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>

#include <asm/byteorder.h>
#include <asm/dma.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/mach-types.h>
#include <asm/unaligned.h>
#include <asm/hardware.h>
#include <asm/arch/pxa-regs.h>

#include <linux/usb_ch9.h>
#include <linux/usb_gadget.h>

#include <asm/arch/udc.h>


/*
 * This driver handles the USB Device Controller (UDC) in Intel's PXA 25x
 * series processors.  The UDC for the IXP 4xx series is very similar.
 * There are fifteen endpoints, in addition to ep0.
 *
 * Such controller drivers work with a gadget driver.  The gadget driver
 * returns descriptors, implements configuration and data protocols used
 * by the host to interact with this device, and allocates endpoints to
 * the different protocol interfaces.  The controller driver virtualizes
 * usb hardware so that the gadget drivers will be more portable.
 * 
 * This UDC hardware wants to implement a bit too much USB protocol, so
 * it constrains the sorts of USB configuration change events that work.
 * The errata for these chips are misleading; some "fixed" bugs from
 * pxa250 a0/a1 b0/b1/b2 sure act like they're still there.
 */

#define	DRIVER_VERSION	"4-May-2005"
#define	DRIVER_DESC	"PXA 25x USB Device Controller driver"


static const char driver_name [] = "pxa2xx_udc";

static const char ep0name [] = "ep0";


// #define	USE_DMA
// #define	USE_OUT_DMA
// #define	DISABLE_TEST_MODE

#ifdef CONFIG_ARCH_IXP4XX
#undef USE_DMA

/* cpu-specific register addresses are compiled in to this code */
#ifdef CONFIG_ARCH_PXA
#error "Can't configure both IXP and PXA"
#endif

#endif

#include "pxa2xx_udc.h"


#ifdef	USE_DMA
static int use_dma = 1;
module_param(use_dma, bool, 0);
MODULE_PARM_DESC (use_dma, "true to use dma");

static void dma_nodesc_handler (int dmach, void *_ep, struct pt_regs *r);
static void kick_dma(struct pxa2xx_ep *ep, struct pxa2xx_request *req);

#ifdef USE_OUT_DMA
#define	DMASTR " (dma support)"
#else
#define	DMASTR " (dma in)"
#endif

#else	/* !USE_DMA */
#define	DMASTR " (pio only)"
#undef	USE_OUT_DMA
#endif

#ifdef	CONFIG_USB_PXA2XX_SMALL
#define SIZE_STR	" (small)"
#else
#define SIZE_STR	""
#endif

#ifdef DISABLE_TEST_MODE
/* (mode == 0) == no undocumented chip tweaks
 * (mode & 1)  == double buffer bulk IN
 * (mode & 2)  == double buffer bulk OUT
 * ... so mode = 3 (or 7, 15, etc) does it for both
 */
static ushort fifo_mode = 0;
module_param(fifo_mode, ushort, 0);
MODULE_PARM_DESC (fifo_mode, "pxa2xx udc fifo mode");
#endif

/* ---------------------------------------------------------------------------
 * 	endpoint related parts of the api to the usb controller hardware,
 *	used by gadget driver; and the inner talker-to-hardware core.
 * ---------------------------------------------------------------------------
 */

static void pxa2xx_ep_fifo_flush (struct usb_ep *ep);
static void nuke (struct pxa2xx_ep *, int status);

static void pio_irq_enable(int bEndpointAddress)
{
        bEndpointAddress &= 0xf;
        if (bEndpointAddress < 8)
                UICR0 &= ~(1 << bEndpointAddress);
        else {
                bEndpointAddress -= 8;
                UICR1 &= ~(1 << bEndpointAddress);
	}
}

static void pio_irq_disable(int bEndpointAddress)
{
        bEndpointAddress &= 0xf;
        if (bEndpointAddress < 8)
                UICR0 |= 1 << bEndpointAddress;
        else {
                bEndpointAddress -= 8;
                UICR1 |= 1 << bEndpointAddress;
        }
}

/* The UDCCR reg contains mask and interrupt status bits,
 * so using '|=' isn't safe as it may ack an interrupt.
 */
#define UDCCR_MASK_BITS         (UDCCR_REM | UDCCR_SRM | UDCCR_UDE)

static inline void udc_set_mask_UDCCR(int mask)
{
	UDCCR = (UDCCR & UDCCR_MASK_BITS) | (mask & UDCCR_MASK_BITS);
}

static inline void udc_clear_mask_UDCCR(int mask)
{
	UDCCR = (UDCCR & UDCCR_MASK_BITS) & ~(mask & UDCCR_MASK_BITS);
}

static inline void udc_ack_int_UDCCR(int mask)
{
	/* udccr contains the bits we dont want to change */
	__u32 udccr = UDCCR & UDCCR_MASK_BITS;

	UDCCR = udccr | (mask & ~UDCCR_MASK_BITS);
}

/*
 * endpoint enable/disable
 *
 * we need to verify the descriptors used to enable endpoints.  since pxa2xx
 * endpoint configurations are fixed, and are pretty much always enabled,
 * there's not a lot to manage here.
 *
 * because pxa2xx can't selectively initialize bulk (or interrupt) endpoints,
 * (resetting endpoint halt and toggle), SET_INTERFACE is unusable except
 * for a single interface (with only the default altsetting) and for gadget
 * drivers that don't halt endpoints (not reset by set_interface).  that also
 * means that if you use ISO, you must violate the USB spec rule that all
 * iso endpoints must be in non-default altsettings.
 */
static int pxa2xx_ep_enable (struct usb_ep *_ep,
		const struct usb_endpoint_descriptor *desc)
{
	struct pxa2xx_ep        *ep;
	struct pxa2xx_udc       *dev;

	ep = container_of (_ep, struct pxa2xx_ep, ep);
	if (!_ep || !desc || ep->desc || _ep->name == ep0name
			|| desc->bDescriptorType != USB_DT_ENDPOINT
			|| ep->bEndpointAddress != desc->bEndpointAddress
			|| ep->fifo_size < le16_to_cpu
						(desc->wMaxPacketSize)) {
		DMSG("%s, bad ep or descriptor\n", __FUNCTION__);
		return -EINVAL;
	}

	/* xfer types must match, except that interrupt ~= bulk */
	if (ep->bmAttributes != desc->bmAttributes
			&& ep->bmAttributes != USB_ENDPOINT_XFER_BULK
			&& desc->bmAttributes != USB_ENDPOINT_XFER_INT) {
		DMSG("%s, %s type mismatch\n", __FUNCTION__, _ep->name);
		return -EINVAL;
	}

	/* hardware _could_ do smaller, but driver doesn't */
	if ((desc->bmAttributes == USB_ENDPOINT_XFER_BULK
				&& le16_to_cpu (desc->wMaxPacketSize)
						!= BULK_FIFO_SIZE)
			|| !desc->wMaxPacketSize) {
		DMSG("%s, bad %s maxpacket\n", __FUNCTION__, _ep->name);
		return -ERANGE;
	}

	dev = ep->dev;
	if (!dev->driver || dev->gadget.speed == USB_SPEED_UNKNOWN) {
		DMSG("%s, bogus device state\n", __FUNCTION__);
		return -ESHUTDOWN;
	}

	ep->desc = desc;
	ep->dma = -1;
	ep->stopped = 0;
	ep->pio_irqs = ep->dma_irqs = 0;
	ep->ep.maxpacket = le16_to_cpu (desc->wMaxPacketSize);

	/* flush fifo (mostly for OUT buffers) */
	pxa2xx_ep_fifo_flush (_ep);

	/* ... reset halt state too, if we could ... */

#ifdef	USE_DMA
	/* for (some) bulk and ISO endpoints, try to get a DMA channel and
	 * bind it to the endpoint.  otherwise use PIO. 
	 */
	switch (ep->bmAttributes) {
	case USB_ENDPOINT_XFER_ISOC:
		if (le16_to_cpu(desc->wMaxPacketSize) % 32)
			break;
		// fall through
	case USB_ENDPOINT_XFER_BULK:
		if (!use_dma || !ep->reg_drcmr)
			break;
		ep->dma = pxa_request_dma ((char *)_ep->name,
 				(le16_to_cpu (desc->wMaxPacketSize) > 64)
					? DMA_PRIO_MEDIUM /* some iso */
					: DMA_PRIO_LOW,
				dma_nodesc_handler, ep);
		if (ep->dma >= 0) {
			*ep->reg_drcmr = DRCMR_MAPVLD | ep->dma;
			DMSG("%s using dma%d\n", _ep->name, ep->dma);
		}
	}
#endif

	DBG(DBG_VERBOSE, "enabled %s\n", _ep->name);
	return 0;
}

static int pxa2xx_ep_disable (struct usb_ep *_ep)
{
	struct pxa2xx_ep	*ep;
	unsigned long		flags;

	ep = container_of (_ep, struct pxa2xx_ep, ep);
	if (!_ep || !ep->desc) {
		DMSG("%s, %s not enabled\n", __FUNCTION__,
			_ep ? ep->ep.name : NULL);
		return -EINVAL;
	}
	local_irq_save(flags);

	nuke (ep, -ESHUTDOWN);

#ifdef	USE_DMA
	if (ep->dma >= 0) {
		*ep->reg_drcmr = 0;
		pxa_free_dma (ep->dma);
		ep->dma = -1;
	}
#endif

	/* flush fifo (mostly for IN buffers) */
	pxa2xx_ep_fifo_flush (_ep);

	ep->desc = NULL;
	ep->stopped = 1;

	local_irq_restore(flags);
	DBG(DBG_VERBOSE, "%s disabled\n", _ep->name);
	return 0;
}

/*-------------------------------------------------------------------------*/

/* for the pxa2xx, these can just wrap kmalloc/kfree.  gadget drivers
 * must still pass correctly initialized endpoints, since other controller
 * drivers may care about how it's currently set up (dma issues etc).
 */

/*
 * 	pxa2xx_ep_alloc_request - allocate a request data structure
 */
static struct usb_request *
pxa2xx_ep_alloc_request (struct usb_ep *_ep, gfp_t gfp_flags)
{
	struct pxa2xx_request *req;

	req = kmalloc (sizeof *req, gfp_flags);
	if (!req)
		return NULL;

	memset (req, 0, sizeof *req);
	INIT_LIST_HEAD (&req->queue);
	return &req->req;
}


/*
 * 	pxa2xx_ep_free_request - deallocate a request data structure
 */
static void
pxa2xx_ep_free_request (struct usb_ep *_ep, struct usb_request *_req)
{
	struct pxa2xx_request	*req;

	req = container_of (_req, struct pxa2xx_request, req);
	WARN_ON (!list_empty (&req->queue));
	kfree(req);
}


/* PXA cache needs flushing with DMA I/O (it's dma-incoherent), but there's
 * no device-affinity and the heap works perfectly well for i/o buffers.
 * It wastes much less memory than dma_alloc_coherent() would, and even
 * prevents cacheline (32 bytes wide) sharing problems.
 */
static void *
pxa2xx_ep_alloc_buffer(struct usb_ep *_ep, unsigned bytes,
	dma_addr_t *dma, gfp_t gfp_flags)
{
	char			*retval;

	retval = kmalloc (bytes, gfp_flags & ~(__GFP_DMA|__GFP_HIGHMEM));
	if (retval)
#ifdef	USE_DMA
		*dma = virt_to_bus (retval);
#else
		*dma = (dma_addr_t)~0;
#endif
	return retval;
}

static void
pxa2xx_ep_free_buffer(struct usb_ep *_ep, void *buf, dma_addr_t dma,
		unsigned bytes)
{
	kfree (buf);
}

/*-------------------------------------------------------------------------*/

/*
 *	done - retire a request; caller blocked irqs
 */
static void done(struct pxa2xx_ep *ep, struct pxa2xx_request *req, int status)
{
	unsigned		stopped = ep->stopped;

	list_del_init(&req->queue);

	if (likely (req->req.status == -EINPROGRESS))
		req->req.status = status;
	else
		status = req->req.status;

	if (status && status != -ESHUTDOWN)
		DBG(DBG_VERBOSE, "complete %s req %p stat %d len %u/%u\n",
			ep->ep.name, &req->req, status,
			req->req.actual, req->req.length);

	/* don't modify queue heads during completion callback */
	ep->stopped = 1;
	req->req.complete(&ep->ep, &req->req);
	ep->stopped = stopped;
}


static inline void ep0_idle (struct pxa2xx_udc *dev)
{
	dev->ep0state = EP0_IDLE;
}

static int
write_packet(volatile u32 *uddr, struct pxa2xx_request *req, unsigned max)
{
	u8		*buf;
	unsigned	length, count;

	buf = req->req.buf + req->req.actual;
	prefetch(buf);

	/* how big will this packet be? */
	length = min(req->req.length - req->req.actual, max);
	req->req.actual += length;

	count = length;
	while (likely(count--))
		*uddr = *buf++;

	return length;
}

/*
 * write to an IN endpoint fifo, as many packets as possible.
 * irqs will use this to write the rest later.
 * caller guarantees at least one packet buffer is ready (or a zlp).
 */
static int
write_fifo (struct pxa2xx_ep *ep, struct pxa2xx_request *req)
{
	unsigned		max;

	max = le16_to_cpu(ep->desc->wMaxPacketSize);
	do {
		unsigned	count;
		int		is_last, is_short;

		count = write_packet(ep->reg_uddr, req, max);

		/* last packet is usually short (or a zlp) */
		if (unlikely (count != max))
			is_last = is_short = 1;
		else {
			if (likely(req->req.length != req->req.actual)
					|| req->req.zero)
				is_last = 0;
			else
				is_last = 1;
			/* interrupt/iso maxpacket may not fill the fifo */
			is_short = unlikely (max < ep->fifo_size);
		}

		DBG(DBG_VERY_NOISY, "wrote %s %d bytes%s%s %d left %p\n",
			ep->ep.name, count,
			is_last ? "/L" : "", is_short ? "/S" : "",
			req->req.length - req->req.actual, req);

		/* let loose that packet. maybe try writing another one,
		 * double buffering might work.  TSP, TPC, and TFS
		 * bit values are the same for all normal IN endpoints.
		 */
		*ep->reg_udccs = UDCCS_BI_TPC;
		if (is_short)
			*ep->reg_udccs = UDCCS_BI_TSP;

		/* requests complete when all IN data is in the FIFO */
		if (is_last) {
			done (ep, req, 0);
			if (list_empty(&ep->queue) || unlikely(ep->dma >= 0)) {
				pio_irq_disable (ep->bEndpointAddress);
#ifdef USE_DMA
				/* unaligned data and zlps couldn't use dma */
				if (unlikely(!list_empty(&ep->queue))) {
					req = list_entry(ep->queue.next,
						struct pxa2xx_request, queue);
					kick_dma(ep,req);
					return 0;
				}
#endif
			}
			return 1;
		}

		// TODO experiment: how robust can fifo mode tweaking be?
		// double buffering is off in the default fifo mode, which
		// prevents TFS from being set here.

	} while (*ep->reg_udccs & UDCCS_BI_TFS);
	return 0;
}

/* caller asserts req->pending (ep0 irq status nyet cleared); starts
 * ep0 data stage.  these chips want very simple state transitions.
 */
static inline
void ep0start(struct pxa2xx_udc *dev, u32 flags, const char *tag)
{
	UDCCS0 = flags|UDCCS0_SA|UDCCS0_OPR;
	USIR0 = USIR0_IR0;
	dev->req_pending = 0;
	DBG(DBG_VERY_NOISY, "%s %s, %02x/%02x\n",
		__FUNCTION__, tag, UDCCS0, flags);
}

static int
write_ep0_fifo (struct pxa2xx_ep *ep, struct pxa2xx_request *req)
{
	unsigned	count;
	int		is_short;

	count = write_packet(&UDDR0, req, EP0_FIFO_SIZE);
	ep->dev->stats.write.bytes += count;

	/* last packet "must be" short (or a zlp) */
	is_short = (count != EP0_FIFO_SIZE);

	DBG(DBG_VERY_NOISY, "ep0in %d bytes %d left %p\n", count,
		req->req.length - req->req.actual, req);

	if (unlikely (is_short)) {
		if (ep->dev->req_pending)
			ep0start(ep->dev, UDCCS0_IPR, "short IN");
		else
			UDCCS0 = UDCCS0_IPR;

		count = req->req.length;
		done (ep, req, 0);
		ep0_idle(ep->dev);
#if 1
		/* This seems to get rid of lost status irqs in some cases:
		 * host responds quickly, or next request involves config
		 * change automagic, or should have been hidden, or ...
		 *
		 * FIXME get rid of all udelays possible...
		 */
		if (count >= EP0_FIFO_SIZE) {
			count = 100;
			do {
				if ((UDCCS0 & UDCCS0_OPR) != 0) {
					/* clear OPR, generate ack */
					UDCCS0 = UDCCS0_OPR;
					break;
				}
				count--;
				udelay(1);
			} while (count);
		}
#endif
	} else if (ep->dev->req_pending)
		ep0start(ep->dev, 0, "IN");
	return is_short;
}


/*
 * read_fifo -  unload packet(s) from the fifo we use for usb OUT
 * transfers and put them into the request.  caller should have made
 * sure there's at least one packet ready.
 *
 * returns true if the request completed because of short packet or the
 * request buffer having filled (and maybe overran till end-of-packet).
 */
static int
read_fifo (struct pxa2xx_ep *ep, struct pxa2xx_request *req)
{
	for (;;) {
		u32		udccs;
		u8		*buf;
		unsigned	bufferspace, count, is_short;

		/* make sure there's a packet in the FIFO.
		 * UDCCS_{BO,IO}_RPC are all the same bit value.
		 * UDCCS_{BO,IO}_RNE are all the same bit value.
		 */
		udccs = *ep->reg_udccs;
		if (unlikely ((udccs & UDCCS_BO_RPC) == 0))
			break;
		buf = req->req.buf + req->req.actual;
		prefetchw(buf);
		bufferspace = req->req.length - req->req.actual;

		/* read all bytes from this packet */
		if (likely (udccs & UDCCS_BO_RNE)) {
			count = 1 + (0x0ff & *ep->reg_ubcr);
			req->req.actual += min (count, bufferspace);
		} else /* zlp */
			count = 0;
		is_short = (count < ep->ep.maxpacket);
		DBG(DBG_VERY_NOISY, "read %s %02x, %d bytes%s req %p %d/%d\n",
			ep->ep.name, udccs, count,
			is_short ? "/S" : "",
			req, req->req.actual, req->req.length);
		while (likely (count-- != 0)) {
			u8	byte = (u8) *ep->reg_uddr;

			if (unlikely (bufferspace == 0)) {
				/* this happens when the driver's buffer
				 * is smaller than what the host sent.
				 * discard the extra data.
				 */
				if (req->req.status != -EOVERFLOW)
					DMSG("%s overflow %d\n",
						ep->ep.name, count);
				req->req.status = -EOVERFLOW;
			} else {
				*buf++ = byte;
				bufferspace--;
			}
		}
		*ep->reg_udccs =  UDCCS_BO_RPC;
		/* RPC/RSP/RNE could now reflect the other packet buffer */

		/* iso is one request per packet */
		if (ep->bmAttributes == USB_ENDPOINT_XFER_ISOC) {
			if (udccs & UDCCS_IO_ROF)
				req->req.status = -EHOSTUNREACH;
			/* more like "is_done" */
			is_short = 1;
		}

		/* completion */
		if (is_short || req->req.actual == req->req.length) {
			done (ep, req, 0);
			if (list_empty(&ep->queue))
				pio_irq_disable (ep->bEndpointAddress);
			return 1;
		}

		/* finished that packet.  the next one may be waiting... */
	}
	return 0;
}

/*
 * special ep0 version of the above.  no UBCR0 or double buffering; status
 * handshaking is magic.  most device protocols don't need control-OUT.
 * CDC vendor commands (and RNDIS), mass storage CB/CBI, and some other
 * protocols do use them.
 */
static int
read_ep0_fifo (struct pxa2xx_ep *ep, struct pxa2xx_request *req)
{
	u8		*buf, byte;
	unsigned	bufferspace;

	buf = req->req.buf + req->req.actual;
	bufferspace = req->req.length - req->req.actual;

	while (UDCCS0 & UDCCS0_RNE) {
		byte = (u8) UDDR0;

		if (unlikely (bufferspace == 0)) {
			/* this happens when the driver's buffer
			 * is smaller than what the host sent.
			 * discard the extra data.
			 */
			if (req->req.status != -EOVERFLOW)
				DMSG("%s overflow\n", ep->ep.name);
			req->req.status = -EOVERFLOW;
		} else {
			*buf++ = byte;
			req->req.actual++;
			bufferspace--;
		}
	}

	UDCCS0 = UDCCS0_OPR | UDCCS0_IPR;

	/* completion */
	if (req->req.actual >= req->req.length)
		return 1;

	/* finished that packet.  the next one may be waiting... */
	return 0;
}

#ifdef	USE_DMA

#define	MAX_IN_DMA	((DCMD_LENGTH + 1) - BULK_FIFO_SIZE)

static void
start_dma_nodesc(struct pxa2xx_ep *ep, struct pxa2xx_request *req, int is_in)
{
	u32	dcmd = req->req.length;
	u32	buf = req->req.dma;
	u32	fifo = io_v2p ((u32)ep->reg_uddr);

	/* caller guarantees there's a packet or more remaining
	 *  - IN may end with a short packet (TSP set separately),
	 *  - OUT is always full length
	 */
	buf += req->req.actual;
	dcmd -= req->req.actual;
	ep->dma_fixup = 0;

	/* no-descriptor mode can be simple for bulk-in, iso-in, iso-out */
	DCSR(ep->dma) = DCSR_NODESC;
	if (is_in) {
		DSADR(ep->dma) = buf;
		DTADR(ep->dma) = fifo;
		if (dcmd > MAX_IN_DMA)
			dcmd = MAX_IN_DMA;
		else
			ep->dma_fixup = (dcmd % ep->ep.maxpacket) != 0;
		dcmd |= DCMD_BURST32 | DCMD_WIDTH1
			| DCMD_FLOWTRG | DCMD_INCSRCADDR;
	} else {
#ifdef USE_OUT_DMA
		DSADR(ep->dma) = fifo;
		DTADR(ep->dma) = buf;
		if (ep->bmAttributes != USB_ENDPOINT_XFER_ISOC)
			dcmd = ep->ep.maxpacket;
		dcmd |= DCMD_BURST32 | DCMD_WIDTH1
			| DCMD_FLOWSRC | DCMD_INCTRGADDR;
#endif
	}
	DCMD(ep->dma) = dcmd;
	DCSR(ep->dma) = DCSR_RUN | DCSR_NODESC
		| (unlikely(is_in)
			? DCSR_STOPIRQEN	/* use dma_nodesc_handler() */
			: 0);			/* use handle_ep() */
}

static void kick_dma(struct pxa2xx_ep *ep, struct pxa2xx_request *req)
{
	int	is_in = ep->bEndpointAddress & USB_DIR_IN;

	if (is_in) {
		/* unaligned tx buffers and zlps only work with PIO */
		if ((req->req.dma & 0x0f) != 0
				|| unlikely((req->req.length - req->req.actual)
						== 0)) {
			pio_irq_enable(ep->bEndpointAddress);
			if ((*ep->reg_udccs & UDCCS_BI_TFS) != 0)
				(void) write_fifo(ep, req);
		} else {
			start_dma_nodesc(ep, req, USB_DIR_IN);
		}
	} else {
		if ((req->req.length - req->req.actual) < ep->ep.maxpacket) {
			DMSG("%s short dma read...\n", ep->ep.name);
			/* we're always set up for pio out */
			read_fifo (ep, req);
		} else {
			*ep->reg_udccs = UDCCS_BO_DME
				| (*ep->reg_udccs & UDCCS_BO_FST);
			start_dma_nodesc(ep, req, USB_DIR_OUT);
		}
	}
}

static void cancel_dma(struct pxa2xx_ep *ep)
{
	struct pxa2xx_request	*req;
	u32			tmp;

	if (DCSR(ep->dma) == 0 || list_empty(&ep->queue))
		return;

	DCSR(ep->dma) = 0;
	while ((DCSR(ep->dma) & DCSR_STOPSTATE) == 0)
		cpu_relax();

	req = list_entry(ep->queue.next, struct pxa2xx_request, queue);
	tmp = DCMD(ep->dma) & DCMD_LENGTH;
	req->req.actual = req->req.length - (tmp & DCMD_LENGTH);

	/* the last tx packet may be incomplete, so flush the fifo.
	 * FIXME correct req.actual if we can
	 */
	if (ep->bEndpointAddress & USB_DIR_IN)
		*ep->reg_udccs = UDCCS_BI_FTF;
}

/* dma channel stopped ... normal tx end (IN), or on error (IN/OUT) */
static void dma_nodesc_handler(int dmach, void *_ep, struct pt_regs *r)
{
	struct pxa2xx_ep	*ep = _ep;
	struct pxa2xx_request	*req;
	u32			tmp, completed;

	local_irq_disable();

	req = list_entry(ep->queue.next, struct pxa2xx_request, queue);

	ep->dma_irqs++;
	ep->dev->stats.irqs++;
	HEX_DISPLAY(ep->dev->stats.irqs);

	/* ack/clear */
	tmp = DCSR(ep->dma);
	DCSR(ep->dma) = tmp;
	if ((tmp & DCSR_STOPSTATE) == 0
			|| (DDADR(ep->dma) & DDADR_STOP) != 0) {
		DBG(DBG_VERBOSE, "%s, dcsr %08x ddadr %08x\n",
			ep->ep.name, DCSR(ep->dma), DDADR(ep->dma));
		goto done;
	}
	DCSR(ep->dma) = 0;	/* clear DCSR_STOPSTATE */

	/* update transfer status */
	completed = tmp & DCSR_BUSERR;
	if (ep->bEndpointAddress & USB_DIR_IN)
		tmp = DSADR(ep->dma);
	else
		tmp = DTADR(ep->dma);
	req->req.actual = tmp - req->req.dma;

	/* FIXME seems we sometimes see partial transfers... */

	if (unlikely(completed != 0))
		req->req.status = -EIO;
	else if (req->req.actual) {
		/* these registers have zeroes in low bits; they miscount
		 * some (end-of-transfer) short packets:  tx 14 as tx 12
		 */
		if (ep->dma_fixup)
			req->req.actual = min(req->req.actual + 3,
						req->req.length);

		tmp = (req->req.length - req->req.actual);
		completed = (tmp == 0);
		if (completed && (ep->bEndpointAddress & USB_DIR_IN)) {

			/* maybe validate final short packet ... */
			if ((req->req.actual % ep->ep.maxpacket) != 0)
				*ep->reg_udccs = UDCCS_BI_TSP/*|UDCCS_BI_TPC*/;

			/* ... or zlp, using pio fallback */
			else if (ep->bmAttributes == USB_ENDPOINT_XFER_BULK
					&& req->req.zero) {
				DMSG("%s zlp terminate ...\n", ep->ep.name);
				completed = 0;
			}
		}
	}

	if (likely(completed)) {
		done(ep, req, 0);

		/* maybe re-activate after completion */
		if (ep->stopped || list_empty(&ep->queue))
			goto done;
		req = list_entry(ep->queue.next, struct pxa2xx_request, queue);
	}
	kick_dma(ep, req);
done:
	local_irq_enable();
}

#endif

/*-------------------------------------------------------------------------*/

static int
pxa2xx_ep_queue(struct usb_ep *_ep, struct usb_request *_req, gfp_t gfp_flags)
{
	struct pxa2xx_request	*req;
	struct pxa2xx_ep	*ep;
	struct pxa2xx_udc	*dev;
	unsigned long		flags;

	req = container_of(_req, struct pxa2xx_request, req);
	if (unlikely (!_req || !_req->complete || !_req->buf
			|| !list_empty(&req->queue))) {
		DMSG("%s, bad params\n", __FUNCTION__);
		return -EINVAL;
	}

	ep = container_of(_ep, struct pxa2xx_ep, ep);
	if (unlikely (!_ep || (!ep->desc && ep->ep.name != ep0name))) {
		DMSG("%s, bad ep\n", __FUNCTION__);
		return -EINVAL;
	}

	dev = ep->dev;
	if (unlikely (!dev->driver
			|| dev->gadget.speed == USB_SPEED_UNKNOWN)) {
		DMSG("%s, bogus device state\n", __FUNCTION__);
		return -ESHUTDOWN;
	}

	/* iso is always one packet per request, that's the only way
	 * we can report per-packet status.  that also helps with dma.
	 */
	if (unlikely (ep->bmAttributes == USB_ENDPOINT_XFER_ISOC
			&& req->req.length > le16_to_cpu
						(ep->desc->wMaxPacketSize)))
		return -EMSGSIZE;

#ifdef	USE_DMA
	// FIXME caller may already have done the dma mapping
	if (ep->dma >= 0) {
		_req->dma = dma_map_single(dev->dev,
			_req->buf, _req->length,
			((ep->bEndpointAddress & USB_DIR_IN) != 0)
				? DMA_TO_DEVICE
				: DMA_FROM_DEVICE);
	}
#endif

	DBG(DBG_NOISY, "%s queue req %p, len %d buf %p\n",
	     _ep->name, _req, _req->length, _req->buf);

	local_irq_save(flags);

	_req->status = -EINPROGRESS;
	_req->actual = 0;

	/* kickstart this i/o queue? */
	if (list_empty(&ep->queue) && !ep->stopped) {
		if (ep->desc == 0 /* ep0 */) {
			unsigned	length = _req->length;

			switch (dev->ep0state) {
			case EP0_IN_DATA_PHASE:
				dev->stats.write.ops++;
				if (write_ep0_fifo(ep, req))
					req = NULL;
				break;

			case EP0_OUT_DATA_PHASE:
				dev->stats.read.ops++;
				/* messy ... */
				if (dev->req_config) {
					DBG(DBG_VERBOSE, "ep0 config ack%s\n",
						dev->has_cfr ?  "" : " raced");
					if (dev->has_cfr)
						UDCCFR = UDCCFR_AREN|UDCCFR_ACM
							|UDCCFR_MB1;
					done(ep, req, 0);
					dev->ep0state = EP0_END_XFER;
					local_irq_restore (flags);
					return 0;
				}
				if (dev->req_pending)
					ep0start(dev, UDCCS0_IPR, "OUT");
				if (length == 0 || ((UDCCS0 & UDCCS0_RNE) != 0
						&& read_ep0_fifo(ep, req))) {
					ep0_idle(dev);
					done(ep, req, 0);
					req = NULL;
				}
				break;

			default:
				DMSG("ep0 i/o, odd state %d\n", dev->ep0state);
				local_irq_restore (flags);
				return -EL2HLT;
			}
#ifdef	USE_DMA
		/* either start dma or prime pio pump */
		} else if (ep->dma >= 0) {
			kick_dma(ep, req);
#endif
		/* can the FIFO can satisfy the request immediately? */
		} else if ((ep->bEndpointAddress & USB_DIR_IN) != 0) {
			if ((*ep->reg_udccs & UDCCS_BI_TFS) != 0
					&& write_fifo(ep, req))
				req = NULL;
		} else if ((*ep->reg_udccs & UDCCS_BO_RFS) != 0
				&& read_fifo(ep, req)) {
			req = NULL;
		}

		if (likely (req && ep->desc) && ep->dma < 0)
			pio_irq_enable(ep->bEndpointAddress);
	}

	/* pio or dma irq handler advances the queue. */
	if (likely (req != 0))
		list_add_tail(&req->queue, &ep->queue);
	local_irq_restore(flags);

	return 0;
}


/*
 * 	nuke - dequeue ALL requests
 */
static void nuke(struct pxa2xx_ep *ep, int status)
{
	struct pxa2xx_request *req;

	/* called with irqs blocked */
#ifdef	USE_DMA
	if (ep->dma >= 0 && !ep->stopped)
		cancel_dma(ep);
#endif
	while (!list_empty(&ep->queue)) {
		req = list_entry(ep->queue.next,
				struct pxa2xx_request,
				queue);
		done(ep, req, status);
	}
	if (ep->desc)
		pio_irq_disable (ep->bEndpointAddress);
}


/* dequeue JUST ONE request */
static int pxa2xx_ep_dequeue(struct usb_ep *_ep, struct usb_request *_req)
{
	struct pxa2xx_ep	*ep;
	struct pxa2xx_request	*req;
	unsigned long		flags;

	ep = container_of(_ep, struct pxa2xx_ep, ep);
	if (!_ep || ep->ep.name == ep0name)
		return -EINVAL;

	local_irq_save(flags);

	/* make sure it's actually queued on this endpoint */
	list_for_each_entry (req, &ep->queue, queue) {
		if (&req->req == _req)
			break;
	}
	if (&req->req != _req) {
		local_irq_restore(flags);
		return -EINVAL;
	}

#ifdef	USE_DMA
	if (ep->dma >= 0 && ep->queue.next == &req->queue && !ep->stopped) {
		cancel_dma(ep);
		done(ep, req, -ECONNRESET);
		/* restart i/o */
		if (!list_empty(&ep->queue)) {
			req = list_entry(ep->queue.next,
					struct pxa2xx_request, queue);
			kick_dma(ep, req);
		}
	} else
#endif
		done(ep, req, -ECONNRESET);

	local_irq_restore(flags);
	return 0;
}

/*-------------------------------------------------------------------------*/

static int pxa2xx_ep_set_halt(struct usb_ep *_ep, int value)
{
	struct pxa2xx_ep	*ep;
	unsigned long		flags;

	ep = container_of(_ep, struct pxa2xx_ep, ep);
	if (unlikely (!_ep
			|| (!ep->desc && ep->ep.name != ep0name))
			|| ep->bmAttributes == USB_ENDPOINT_XFER_ISOC) {
		DMSG("%s, bad ep\n", __FUNCTION__);
		return -EINVAL;
	}
	if (value == 0) {
		/* this path (reset toggle+halt) is needed to implement
		 * SET_INTERFACE on normal hardware.  but it can't be
		 * done from software on the PXA UDC, and the hardware
		 * forgets to do it as part of SET_INTERFACE automagic.
		 */
		DMSG("only host can clear %s halt\n", _ep->name);
		return -EROFS;
	}

	local_irq_save(flags);

	if ((ep->bEndpointAddress & USB_DIR_IN) != 0
			&& ((*ep->reg_udccs & UDCCS_BI_TFS) == 0
			   || !list_empty(&ep->queue))) {
		local_irq_restore(flags);
		return -EAGAIN;
	}

	/* FST bit is the same for control, bulk in, bulk out, interrupt in */
	*ep->reg_udccs = UDCCS_BI_FST|UDCCS_BI_FTF;

	/* ep0 needs special care */
	if (!ep->desc) {
		start_watchdog(ep->dev);
		ep->dev->req_pending = 0;
		ep->dev->ep0state = EP0_STALL;

 	/* and bulk/intr endpoints like dropping stalls too */
 	} else {
 		unsigned i;
 		for (i = 0; i < 1000; i += 20) {
 			if (*ep->reg_udccs & UDCCS_BI_SST)
 				break;
 			udelay(20);
 		}
  	}
 	local_irq_restore(flags);

	DBG(DBG_VERBOSE, "%s halt\n", _ep->name);
	return 0;
}

static int pxa2xx_ep_fifo_status(struct usb_ep *_ep)
{
	struct pxa2xx_ep        *ep;

	ep = container_of(_ep, struct pxa2xx_ep, ep);
	if (!_ep) {
		DMSG("%s, bad ep\n", __FUNCTION__);
		return -ENODEV;
	}
	/* pxa can't report unclaimed bytes from IN fifos */
	if ((ep->bEndpointAddress & USB_DIR_IN) != 0)
		return -EOPNOTSUPP;
	if (ep->dev->gadget.speed == USB_SPEED_UNKNOWN
			|| (*ep->reg_udccs & UDCCS_BO_RFS) == 0)
		return 0;
	else
		return (*ep->reg_ubcr & 0xfff) + 1;
}

static void pxa2xx_ep_fifo_flush(struct usb_ep *_ep)
{
	struct pxa2xx_ep        *ep;

	ep = container_of(_ep, struct pxa2xx_ep, ep);
	if (!_ep || ep->ep.name == ep0name || !list_empty(&ep->queue)) {
		DMSG("%s, bad ep\n", __FUNCTION__);
		return;
	}

	/* toggle and halt bits stay unchanged */

	/* for OUT, just read and discard the FIFO contents. */
	if ((ep->bEndpointAddress & USB_DIR_IN) == 0) {
		while (((*ep->reg_udccs) & UDCCS_BO_RNE) != 0)
			(void) *ep->reg_uddr;
		return;
	}

	/* most IN status is the same, but ISO can't stall */
	*ep->reg_udccs = UDCCS_BI_TPC|UDCCS_BI_FTF|UDCCS_BI_TUR
		| (ep->bmAttributes == USB_ENDPOINT_XFER_ISOC)
			? 0 : UDCCS_BI_SST;
}


static struct usb_ep_ops pxa2xx_ep_ops = {
	.enable		= pxa2xx_ep_enable,
	.disable	= pxa2xx_ep_disable,

	.alloc_request	= pxa2xx_ep_alloc_request,
	.free_request	= pxa2xx_ep_free_request,

	.alloc_buffer	= pxa2xx_ep_alloc_buffer,
	.free_buffer	= pxa2xx_ep_free_buffer,

	.queue		= pxa2xx_ep_queue,
	.dequeue	= pxa2xx_ep_dequeue,

	.set_halt	= pxa2xx_ep_set_halt,
	.fifo_status	= pxa2xx_ep_fifo_status,
	.fifo_flush	= pxa2xx_ep_fifo_flush,
};


/* ---------------------------------------------------------------------------
 * 	device-scoped parts of the api to the usb controller hardware
 * ---------------------------------------------------------------------------
 */

static int pxa2xx_udc_get_frame(struct usb_gadget *_gadget)
{
	return ((UFNRH & 0x07) << 8) | (UFNRL & 0xff);
}

static int pxa2xx_udc_wakeup(struct usb_gadget *_gadget)
{
	/* host may not have enabled remote wakeup */
	if ((UDCCS0 & UDCCS0_DRWF) == 0)
		return -EHOSTUNREACH;
	udc_set_mask_UDCCR(UDCCR_RSM);
	return 0;
}

static void stop_activity(struct pxa2xx_udc *, struct usb_gadget_driver *);
static void udc_enable (struct pxa2xx_udc *);
static void udc_disable(struct pxa2xx_udc *);

/* We disable the UDC -- and its 48 MHz clock -- whenever it's not
 * in active use.  
 */
static int pullup(struct pxa2xx_udc *udc, int is_active)
{
	is_active = is_active && udc->vbus && udc->pullup;
	DMSG("%s\n", is_active ? "active" : "inactive");
	if (is_active)
		udc_enable(udc);
	else {
		if (udc->gadget.speed != USB_SPEED_UNKNOWN) {
			DMSG("disconnect %s\n", udc->driver
				? udc->driver->driver.name
				: "(no driver)");
			stop_activity(udc, udc->driver);
		}
		udc_disable(udc);
	}
	return 0;
}

/* VBUS reporting logically comes from a transceiver */
static int pxa2xx_udc_vbus_session(struct usb_gadget *_gadget, int is_active)
{
	struct pxa2xx_udc	*udc;

	udc = container_of(_gadget, struct pxa2xx_udc, gadget);
	udc->vbus = is_active = (is_active != 0);
	DMSG("vbus %s\n", is_active ? "supplied" : "inactive");
	pullup(udc, is_active);
	return 0;
}

/* drivers may have software control over D+ pullup */
static int pxa2xx_udc_pullup(struct usb_gadget *_gadget, int is_active)
{
	struct pxa2xx_udc	*udc;

	udc = container_of(_gadget, struct pxa2xx_udc, gadget);

	/* not all boards support pullup control */
	if (!udc->mach->udc_command)
		return -EOPNOTSUPP;

	is_active = (is_active != 0);
	udc->pullup = is_active;
	pullup(udc, is_active);
	return 0;
}

static const struct usb_gadget_ops pxa2xx_udc_ops = {
	.get_frame	= pxa2xx_udc_get_frame,
	.wakeup		= pxa2xx_udc_wakeup,
	.vbus_session	= pxa2xx_udc_vbus_session,
	.pullup		= pxa2xx_udc_pullup,

	// .vbus_draw ... boards may consume current from VBUS, up to
	// 100-500mA based on config.  the 500uA suspend ceiling means
	// that exclusively vbus-powered PXA designs violate USB specs.
};

/*-------------------------------------------------------------------------*/

#ifdef CONFIG_USB_GADGET_DEBUG_FILES

static const char proc_node_name [] = "driver/udc";

static int
udc_proc_read(char *page, char **start, off_t off, int count,
		int *eof, void *_dev)
{
	char			*buf = page;
	struct pxa2xx_udc	*dev = _dev;
	char			*next = buf;
	unsigned		size = count;
	unsigned long		flags;
	int			i, t;
	u32			tmp;

	if (off != 0)
		return 0;

	local_irq_save(flags);

	/* basic device status */
	t = scnprintf(next, size, DRIVER_DESC "\n"
		"%s version: %s\nGadget driver: %s\nHost %s\n\n",
		driver_name, DRIVER_VERSION SIZE_STR DMASTR,
		dev->driver ? dev->driver->driver.name : "(none)",
		is_vbus_present() ? "full speed" : "disconnected");
	size -= t;
	next += t;

	/* registers for device and ep0 */
	t = scnprintf(next, size,
		"uicr %02X.%02X, usir %02X.%02x, ufnr %02X.%02X\n",
		UICR1, UICR0, USIR1, USIR0, UFNRH, UFNRL);
	size -= t;
	next += t;

	tmp = UDCCR;
	t = scnprintf(next, size,
		"udccr %02X =%s%s%s%s%s%s%s%s\n", tmp,
		(tmp & UDCCR_REM) ? " rem" : "",
		(tmp & UDCCR_RSTIR) ? " rstir" : "",
		(tmp & UDCCR_SRM) ? " srm" : "",
		(tmp & UDCCR_SUSIR) ? " susir" : "",
		(tmp & UDCCR_RESIR) ? " resir" : "",
		(tmp & UDCCR_RSM) ? " rsm" : "",
		(tmp & UDCCR_UDA) ? " uda" : "",
		(tmp & UDCCR_UDE) ? " ude" : "");
	size -= t;
	next += t;

	tmp = UDCCS0;
	t = scnprintf(next, size,
		"udccs0 %02X =%s%s%s%s%s%s%s%s\n", tmp,
		(tmp & UDCCS0_SA) ? " sa" : "",
		(tmp & UDCCS0_RNE) ? " rne" : "",
		(tmp & UDCCS0_FST) ? " fst" : "",
		(tmp & UDCCS0_SST) ? " sst" : "",
		(tmp & UDCCS0_DRWF) ? " dwrf" : "",
		(tmp & UDCCS0_FTF) ? " ftf" : "",
		(tmp & UDCCS0_IPR) ? " ipr" : "",
		(tmp & UDCCS0_OPR) ? " opr" : "");
	size -= t;
	next += t;

	if (dev->has_cfr) {
		tmp = UDCCFR;
		t = scnprintf(next, size,
			"udccfr %02X =%s%s\n", tmp,
			(tmp & UDCCFR_AREN) ? " aren" : "",
			(tmp & UDCCFR_ACM) ? " acm" : "");
		size -= t;
		next += t;
	}

	if (!is_vbus_present() || !dev->driver)
		goto done;

	t = scnprintf(next, size, "ep0 IN %lu/%lu, OUT %lu/%lu\nirqs %lu\n\n",
		dev->stats.write.bytes, dev->stats.write.ops,
		dev->stats.read.bytes, dev->stats.read.ops,
		dev->stats.irqs);
	size -= t;
	next += t;

	/* dump endpoint queues */
	for (i = 0; i < PXA_UDC_NUM_ENDPOINTS; i++) {
		struct pxa2xx_ep	*ep = &dev->ep [i];
		struct pxa2xx_request	*req;
		int			t;

		if (i != 0) {
			const struct usb_endpoint_descriptor	*d;

			d = ep->desc;
			if (!d)
				continue;
			tmp = *dev->ep [i].reg_udccs;
			t = scnprintf(next, size,
				"%s max %d %s udccs %02x irqs %lu/%lu\n",
				ep->ep.name, le16_to_cpu (d->wMaxPacketSize),
				(ep->dma >= 0) ? "dma" : "pio", tmp,
				ep->pio_irqs, ep->dma_irqs);
			/* TODO translate all five groups of udccs bits! */

		} else /* ep0 should only have one transfer queued */
			t = scnprintf(next, size, "ep0 max 16 pio irqs %lu\n",
				ep->pio_irqs);
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
#ifdef	USE_DMA
			if (ep->dma >= 0 && req->queue.prev == &ep->queue)
				t = scnprintf(next, size,
					"\treq %p len %d/%d "
					"buf %p (dma%d dcmd %08x)\n",
					&req->req, req->req.actual,
					req->req.length, req->req.buf,
					ep->dma, DCMD(ep->dma)
					// low 13 bits == bytes-to-go
					);
			else
#endif
				t = scnprintf(next, size,
					"\treq %p len %d/%d buf %p\n",
					&req->req, req->req.actual,
					req->req.length, req->req.buf);
			if (t <= 0 || t > size)
				goto done;
			size -= t;
			next += t;
		}
	}

done:
	local_irq_restore(flags);
	*eof = 1;
	return count - size;
}

#define create_proc_files() \
	create_proc_read_entry(proc_node_name, 0, NULL, udc_proc_read, dev)
#define remove_proc_files() \
	remove_proc_entry(proc_node_name, NULL)

#else	/* !CONFIG_USB_GADGET_DEBUG_FILES */

#define create_proc_files() do {} while (0)
#define remove_proc_files() do {} while (0)

#endif	/* CONFIG_USB_GADGET_DEBUG_FILES */

/* "function" sysfs attribute */
static ssize_t
show_function (struct device *_dev, struct device_attribute *attr, char *buf)
{
	struct pxa2xx_udc	*dev = dev_get_drvdata (_dev);

	if (!dev->driver
			|| !dev->driver->function
			|| strlen (dev->driver->function) > PAGE_SIZE)
		return 0;
	return scnprintf (buf, PAGE_SIZE, "%s\n", dev->driver->function);
}
static DEVICE_ATTR (function, S_IRUGO, show_function, NULL);

/*-------------------------------------------------------------------------*/

/*
 * 	udc_disable - disable USB device controller
 */
static void udc_disable(struct pxa2xx_udc *dev)
{
	/* block all irqs */
	udc_set_mask_UDCCR(UDCCR_SRM|UDCCR_REM);
	UICR0 = UICR1 = 0xff;
	UFNRH = UFNRH_SIM;

	/* if hardware supports it, disconnect from usb */
	pullup_off();

	udc_clear_mask_UDCCR(UDCCR_UDE);

#ifdef	CONFIG_ARCH_PXA
        /* Disable clock for USB device */
	pxa_set_cken(CKEN11_USB, 0);
#endif

	ep0_idle (dev);
	dev->gadget.speed = USB_SPEED_UNKNOWN;
	LED_CONNECTED_OFF;
}


/*
 * 	udc_reinit - initialize software state
 */
static void udc_reinit(struct pxa2xx_udc *dev)
{
	u32	i;

	/* device/ep0 records init */
	INIT_LIST_HEAD (&dev->gadget.ep_list);
	INIT_LIST_HEAD (&dev->gadget.ep0->ep_list);
	dev->ep0state = EP0_IDLE;

	/* basic endpoint records init */
	for (i = 0; i < PXA_UDC_NUM_ENDPOINTS; i++) {
		struct pxa2xx_ep *ep = &dev->ep[i];

		if (i != 0)
			list_add_tail (&ep->ep.ep_list, &dev->gadget.ep_list);

		ep->desc = NULL;
		ep->stopped = 0;
		INIT_LIST_HEAD (&ep->queue);
		ep->pio_irqs = ep->dma_irqs = 0;
	}

	/* the rest was statically initialized, and is read-only */
}

/* until it's enabled, this UDC should be completely invisible
 * to any USB host.
 */
static void udc_enable (struct pxa2xx_udc *dev)
{
	udc_clear_mask_UDCCR(UDCCR_UDE);

#ifdef	CONFIG_ARCH_PXA
        /* Enable clock for USB device */
	pxa_set_cken(CKEN11_USB, 1);
	udelay(5);
#endif

	/* try to clear these bits before we enable the udc */
	udc_ack_int_UDCCR(UDCCR_SUSIR|/*UDCCR_RSTIR|*/UDCCR_RESIR);

	ep0_idle(dev);
	dev->gadget.speed = USB_SPEED_UNKNOWN;
	dev->stats.irqs = 0;

	/*
	 * sequence taken from chapter 12.5.10, PXA250 AppProcDevManual:
	 * - enable UDC
	 * - if RESET is already in progress, ack interrupt
	 * - unmask reset interrupt
	 */
	udc_set_mask_UDCCR(UDCCR_UDE);
	if (!(UDCCR & UDCCR_UDA))
		udc_ack_int_UDCCR(UDCCR_RSTIR);

	if (dev->has_cfr /* UDC_RES2 is defined */) {
		/* pxa255 (a0+) can avoid a set_config race that could
		 * prevent gadget drivers from configuring correctly
		 */
		UDCCFR = UDCCFR_ACM | UDCCFR_MB1;
	} else {
		/* "USB test mode" for pxa250 errata 40-42 (stepping a0, a1)
		 * which could result in missing packets and interrupts.
		 * supposedly one bit per endpoint, controlling whether it
		 * double buffers or not; ACM/AREN bits fit into the holes.
		 * zero bits (like USIR0_IRx) disable double buffering.
		 */
		UDC_RES1 = 0x00;
		UDC_RES2 = 0x00;
	}

#ifdef	DISABLE_TEST_MODE
	/* "test mode" seems to have become the default in later chip
	 * revs, preventing double buffering (and invalidating docs).
	 * this EXPERIMENT enables it for bulk endpoints by tweaking
	 * undefined/reserved register bits (that other drivers clear).
	 * Belcarra code comments noted this usage.
	 */
	if (fifo_mode & 1) {	/* IN endpoints */
		UDC_RES1 |= USIR0_IR1|USIR0_IR6;
		UDC_RES2 |= USIR1_IR11;
	}
	if (fifo_mode & 2) {	/* OUT endpoints */
		UDC_RES1 |= USIR0_IR2|USIR0_IR7;
		UDC_RES2 |= USIR1_IR12;
	}
#endif

	/* enable suspend/resume and reset irqs */
	udc_clear_mask_UDCCR(UDCCR_SRM | UDCCR_REM);

	/* enable ep0 irqs */
	UICR0 &= ~UICR0_IM0;

	/* if hardware supports it, pullup D+ and wait for reset */
	pullup_on();
}


/* when a driver is successfully registered, it will receive
 * control requests including set_configuration(), which enables
 * non-control requests.  then usb traffic follows until a
 * disconnect is reported.  then a host may connect again, or
 * the driver might get unbound.
 */
int usb_gadget_register_driver(struct usb_gadget_driver *driver)
{
	struct pxa2xx_udc	*dev = the_controller;
	int			retval;

	if (!driver
			|| driver->speed != USB_SPEED_FULL
			|| !driver->bind
			|| !driver->unbind
			|| !driver->disconnect
			|| !driver->setup)
		return -EINVAL;
	if (!dev)
		return -ENODEV;
	if (dev->driver)
		return -EBUSY;

	/* first hook up the driver ... */
	dev->driver = driver;
	dev->gadget.dev.driver = &driver->driver;
	dev->pullup = 1;

	device_add (&dev->gadget.dev);
	retval = driver->bind(&dev->gadget);
	if (retval) {
		DMSG("bind to driver %s --> error %d\n",
				driver->driver.name, retval);
		device_del (&dev->gadget.dev);

		dev->driver = NULL;
		dev->gadget.dev.driver = NULL;
		return retval;
	}
	device_create_file(dev->dev, &dev_attr_function);

	/* ... then enable host detection and ep0; and we're ready
	 * for set_configuration as well as eventual disconnect.
	 */
	DMSG("registered gadget driver '%s'\n", driver->driver.name);
	pullup(dev, 1);
	dump_state(dev);
	return 0;
}
EXPORT_SYMBOL(usb_gadget_register_driver);

static void
stop_activity(struct pxa2xx_udc *dev, struct usb_gadget_driver *driver)
{
	int i;

	/* don't disconnect drivers more than once */
	if (dev->gadget.speed == USB_SPEED_UNKNOWN)
		driver = NULL;
	dev->gadget.speed = USB_SPEED_UNKNOWN;

	/* prevent new request submissions, kill any outstanding requests  */
	for (i = 0; i < PXA_UDC_NUM_ENDPOINTS; i++) {
		struct pxa2xx_ep *ep = &dev->ep[i];

		ep->stopped = 1;
		nuke(ep, -ESHUTDOWN);
	}
	del_timer_sync(&dev->timer);

	/* report disconnect; the driver is already quiesced */
	LED_CONNECTED_OFF;
	if (driver)
		driver->disconnect(&dev->gadget);

	/* re-init driver-visible data structures */
	udc_reinit(dev);
}

int usb_gadget_unregister_driver(struct usb_gadget_driver *driver)
{
	struct pxa2xx_udc	*dev = the_controller;

	if (!dev)
		return -ENODEV;
	if (!driver || driver != dev->driver)
		return -EINVAL;

	local_irq_disable();
	pullup(dev, 0);
	stop_activity(dev, driver);
	local_irq_enable();

	driver->unbind(&dev->gadget);
	dev->driver = NULL;

	device_del (&dev->gadget.dev);
	device_remove_file(dev->dev, &dev_attr_function);

	DMSG("unregistered gadget driver '%s'\n", driver->driver.name);
	dump_state(dev);
	return 0;
}
EXPORT_SYMBOL(usb_gadget_unregister_driver);


/*-------------------------------------------------------------------------*/

#ifdef CONFIG_ARCH_LUBBOCK

/* Lubbock has separate connect and disconnect irqs.  More typical designs
 * use one GPIO as the VBUS IRQ, and another to control the D+ pullup.
 */

static irqreturn_t
lubbock_vbus_irq(int irq, void *_dev, struct pt_regs *r)
{
	struct pxa2xx_udc	*dev = _dev;
	int			vbus;

	dev->stats.irqs++;
	HEX_DISPLAY(dev->stats.irqs);
	switch (irq) {
	case LUBBOCK_USB_IRQ:
		LED_CONNECTED_ON;
		vbus = 1;
		disable_irq(LUBBOCK_USB_IRQ);
		enable_irq(LUBBOCK_USB_DISC_IRQ);
		break;
	case LUBBOCK_USB_DISC_IRQ:
		LED_CONNECTED_OFF;
		vbus = 0;
		disable_irq(LUBBOCK_USB_DISC_IRQ);
		enable_irq(LUBBOCK_USB_IRQ);
		break;
	default:
		return IRQ_NONE;
	}

	pxa2xx_udc_vbus_session(&dev->gadget, vbus);
	return IRQ_HANDLED;
}

#endif


/*-------------------------------------------------------------------------*/

static inline void clear_ep_state (struct pxa2xx_udc *dev)
{
	unsigned i;

	/* hardware SET_{CONFIGURATION,INTERFACE} automagic resets endpoint
	 * fifos, and pending transactions mustn't be continued in any case.
	 */
	for (i = 1; i < PXA_UDC_NUM_ENDPOINTS; i++)
		nuke(&dev->ep[i], -ECONNABORTED);
}

static void udc_watchdog(unsigned long _dev)
{
	struct pxa2xx_udc	*dev = (void *)_dev;

	local_irq_disable();
	if (dev->ep0state == EP0_STALL
			&& (UDCCS0 & UDCCS0_FST) == 0
			&& (UDCCS0 & UDCCS0_SST) == 0) {
		UDCCS0 = UDCCS0_FST|UDCCS0_FTF;
		DBG(DBG_VERBOSE, "ep0 re-stall\n");
		start_watchdog(dev);
	}
	local_irq_enable();
}

static void handle_ep0 (struct pxa2xx_udc *dev)
{
	u32			udccs0 = UDCCS0;
	struct pxa2xx_ep	*ep = &dev->ep [0];
	struct pxa2xx_request	*req;
	union {
		struct usb_ctrlrequest	r;
		u8			raw [8];
		u32			word [2];
	} u;

	if (list_empty(&ep->queue))
		req = NULL;
	else
		req = list_entry(ep->queue.next, struct pxa2xx_request, queue);

	/* clear stall status */
	if (udccs0 & UDCCS0_SST) {
		nuke(ep, -EPIPE);
		UDCCS0 = UDCCS0_SST;
		del_timer(&dev->timer);
		ep0_idle(dev);
	}

	/* previous request unfinished?  non-error iff back-to-back ... */
	if ((udccs0 & UDCCS0_SA) != 0 && dev->ep0state != EP0_IDLE) {
		nuke(ep, 0);
		del_timer(&dev->timer);
		ep0_idle(dev);
	}

	switch (dev->ep0state) {
	case EP0_IDLE:
		/* late-breaking status? */
		udccs0 = UDCCS0;

		/* start control request? */
		if (likely((udccs0 & (UDCCS0_OPR|UDCCS0_SA|UDCCS0_RNE))
				== (UDCCS0_OPR|UDCCS0_SA|UDCCS0_RNE))) {
			int i;

			nuke (ep, -EPROTO);

			/* read SETUP packet */
			for (i = 0; i < 8; i++) {
				if (unlikely(!(UDCCS0 & UDCCS0_RNE))) {
bad_setup:
					DMSG("SETUP %d!\n", i);
					goto stall;
				}
				u.raw [i] = (u8) UDDR0;
			}
			if (unlikely((UDCCS0 & UDCCS0_RNE) != 0))
				goto bad_setup;

got_setup:
			DBG(DBG_VERBOSE, "SETUP %02x.%02x v%04x i%04x l%04x\n",
				u.r.bRequestType, u.r.bRequest,
				le16_to_cpu(u.r.wValue),
				le16_to_cpu(u.r.wIndex),
				le16_to_cpu(u.r.wLength));

			/* cope with automagic for some standard requests. */
			dev->req_std = (u.r.bRequestType & USB_TYPE_MASK)
						== USB_TYPE_STANDARD;
			dev->req_config = 0;
			dev->req_pending = 1;
			switch (u.r.bRequest) {
			/* hardware restricts gadget drivers here! */
			case USB_REQ_SET_CONFIGURATION:
				if (u.r.bRequestType == USB_RECIP_DEVICE) {
					/* reflect hardware's automagic
					 * up to the gadget driver.
					 */
config_change:
					dev->req_config = 1;
					clear_ep_state(dev);
					/* if !has_cfr, there's no synch
					 * else use AREN (later) not SA|OPR
					 * USIR0_IR0 acts edge sensitive
					 */
				}
				break;
			/* ... and here, even more ... */
			case USB_REQ_SET_INTERFACE:
				if (u.r.bRequestType == USB_RECIP_INTERFACE) {
					/* udc hardware is broken by design:
					 *  - altsetting may only be zero;
					 *  - hw resets all interfaces' eps;
					 *  - ep reset doesn't include halt(?).
					 */
					DMSG("broken set_interface (%d/%d)\n",
						le16_to_cpu(u.r.wIndex),
						le16_to_cpu(u.r.wValue));
					goto config_change;
				}
				break;
			/* hardware was supposed to hide this */
			case USB_REQ_SET_ADDRESS:
				if (u.r.bRequestType == USB_RECIP_DEVICE) {
					ep0start(dev, 0, "address");
					return;
				}
				break;
			}

			if (u.r.bRequestType & USB_DIR_IN)
				dev->ep0state = EP0_IN_DATA_PHASE;
			else
				dev->ep0state = EP0_OUT_DATA_PHASE;

			i = dev->driver->setup(&dev->gadget, &u.r);
			if (i < 0) {
				/* hardware automagic preventing STALL... */
				if (dev->req_config) {
					/* hardware sometimes neglects to tell
					 * tell us about config change events,
					 * so later ones may fail...
					 */
					WARN("config change %02x fail %d?\n",
						u.r.bRequest, i);
					return;
					/* TODO experiment:  if has_cfr,
					 * hardware didn't ACK; maybe we
					 * could actually STALL!
					 */
				}
				DBG(DBG_VERBOSE, "protocol STALL, "
					"%02x err %d\n", UDCCS0, i);
stall:
				/* the watchdog timer helps deal with cases
				 * where udc seems to clear FST wrongly, and
				 * then NAKs instead of STALLing.
				 */
				ep0start(dev, UDCCS0_FST|UDCCS0_FTF, "stall");
				start_watchdog(dev);
				dev->ep0state = EP0_STALL;

			/* deferred i/o == no response yet */
			} else if (dev->req_pending) {
				if (likely(dev->ep0state == EP0_IN_DATA_PHASE
						|| dev->req_std || u.r.wLength))
					ep0start(dev, 0, "defer");
				else
					ep0start(dev, UDCCS0_IPR, "defer/IPR");
			}

			/* expect at least one data or status stage irq */
			return;

		} else if (likely((udccs0 & (UDCCS0_OPR|UDCCS0_SA))
				== (UDCCS0_OPR|UDCCS0_SA))) {
			unsigned i;

			/* pxa210/250 erratum 131 for B0/B1 says RNE lies.
			 * still observed on a pxa255 a0.
			 */
			DBG(DBG_VERBOSE, "e131\n");
			nuke(ep, -EPROTO);

			/* read SETUP data, but don't trust it too much */
			for (i = 0; i < 8; i++)
				u.raw [i] = (u8) UDDR0;
			if ((u.r.bRequestType & USB_RECIP_MASK)
					> USB_RECIP_OTHER)
				goto stall;
			if (u.word [0] == 0 && u.word [1] == 0)
				goto stall;
			goto got_setup;
		} else {
			/* some random early IRQ:
			 * - we acked FST
			 * - IPR cleared
			 * - OPR got set, without SA (likely status stage)
			 */
			UDCCS0 = udccs0 & (UDCCS0_SA|UDCCS0_OPR);
		}
		break;
	case EP0_IN_DATA_PHASE:			/* GET_DESCRIPTOR etc */
		if (udccs0 & UDCCS0_OPR) {
			UDCCS0 = UDCCS0_OPR|UDCCS0_FTF;
			DBG(DBG_VERBOSE, "ep0in premature status\n");
			if (req)
				done(ep, req, 0);
			ep0_idle(dev);
		} else /* irq was IPR clearing */ {
			if (req) {
				/* this IN packet might finish the request */
				(void) write_ep0_fifo(ep, req);
			} /* else IN token before response was written */
		}
		break;
	case EP0_OUT_DATA_PHASE:		/* SET_DESCRIPTOR etc */
		if (udccs0 & UDCCS0_OPR) {
			if (req) {
				/* this OUT packet might finish the request */
				if (read_ep0_fifo(ep, req))
					done(ep, req, 0);
				/* else more OUT packets expected */
			} /* else OUT token before read was issued */
		} else /* irq was IPR clearing */ {
			DBG(DBG_VERBOSE, "ep0out premature status\n");
			if (req)
				done(ep, req, 0);
			ep0_idle(dev);
		}
		break;
	case EP0_END_XFER:
		if (req)
			done(ep, req, 0);
		/* ack control-IN status (maybe in-zlp was skipped)
		 * also appears after some config change events.
		 */
		if (udccs0 & UDCCS0_OPR)
			UDCCS0 = UDCCS0_OPR;
		ep0_idle(dev);
		break;
	case EP0_STALL:
		UDCCS0 = UDCCS0_FST;
		break;
	}
	USIR0 = USIR0_IR0;
}

static void handle_ep(struct pxa2xx_ep *ep)
{
	struct pxa2xx_request	*req;
	int			is_in = ep->bEndpointAddress & USB_DIR_IN;
	int			completed;
	u32			udccs, tmp;

	do {
		completed = 0;
		if (likely (!list_empty(&ep->queue)))
			req = list_entry(ep->queue.next,
					struct pxa2xx_request, queue);
		else
			req = NULL;

		// TODO check FST handling

		udccs = *ep->reg_udccs;
		if (unlikely(is_in)) {	/* irq from TPC, SST, or (ISO) TUR */
			tmp = UDCCS_BI_TUR;
			if (likely(ep->bmAttributes == USB_ENDPOINT_XFER_BULK))
				tmp |= UDCCS_BI_SST;
			tmp &= udccs;
			if (likely (tmp))
				*ep->reg_udccs = tmp;
			if (req && likely ((udccs & UDCCS_BI_TFS) != 0))
				completed = write_fifo(ep, req);

		} else {	/* irq from RPC (or for ISO, ROF) */
			if (likely(ep->bmAttributes == USB_ENDPOINT_XFER_BULK))
				tmp = UDCCS_BO_SST | UDCCS_BO_DME;
			else
				tmp = UDCCS_IO_ROF | UDCCS_IO_DME;
			tmp &= udccs;
			if (likely(tmp))
				*ep->reg_udccs = tmp;

			/* fifos can hold packets, ready for reading... */
			if (likely(req)) {
#ifdef USE_OUT_DMA
// TODO didn't yet debug out-dma.  this approach assumes
// the worst about short packets and RPC; it might be better.

				if (likely(ep->dma >= 0)) {
					if (!(udccs & UDCCS_BO_RSP)) {
						*ep->reg_udccs = UDCCS_BO_RPC;
						ep->dma_irqs++;
						return;
					}
				}
#endif
				completed = read_fifo(ep, req);
			} else
				pio_irq_disable (ep->bEndpointAddress);
		}
		ep->pio_irqs++;
	} while (completed);
}

/*
 *	pxa2xx_udc_irq - interrupt handler
 *
 * avoid delays in ep0 processing. the control handshaking isn't always
 * under software control (pxa250c0 and the pxa255 are better), and delays
 * could cause usb protocol errors.
 */
static irqreturn_t
pxa2xx_udc_irq(int irq, void *_dev, struct pt_regs *r)
{
	struct pxa2xx_udc	*dev = _dev;
	int			handled;

	dev->stats.irqs++;
	HEX_DISPLAY(dev->stats.irqs);
	do {
		u32		udccr = UDCCR;

		handled = 0;

		/* SUSpend Interrupt Request */
		if (unlikely(udccr & UDCCR_SUSIR)) {
			udc_ack_int_UDCCR(UDCCR_SUSIR);
			handled = 1;
			DBG(DBG_VERBOSE, "USB suspend%s\n", is_vbus_present()
				? "" : "+disconnect");

			if (!is_vbus_present())
				stop_activity(dev, dev->driver);
			else if (dev->gadget.speed != USB_SPEED_UNKNOWN
					&& dev->driver
					&& dev->driver->suspend)
				dev->driver->suspend(&dev->gadget);
			ep0_idle (dev);
		}

		/* RESume Interrupt Request */
		if (unlikely(udccr & UDCCR_RESIR)) {
			udc_ack_int_UDCCR(UDCCR_RESIR);
			handled = 1;
			DBG(DBG_VERBOSE, "USB resume\n");

			if (dev->gadget.speed != USB_SPEED_UNKNOWN
					&& dev->driver
					&& dev->driver->resume
					&& is_vbus_present())
				dev->driver->resume(&dev->gadget);
		}

		/* ReSeT Interrupt Request - USB reset */
		if (unlikely(udccr & UDCCR_RSTIR)) {
			udc_ack_int_UDCCR(UDCCR_RSTIR);
			handled = 1;

			if ((UDCCR & UDCCR_UDA) == 0) {
				DBG(DBG_VERBOSE, "USB reset start\n");

				/* reset driver and endpoints,
				 * in case that's not yet done
				 */
				stop_activity (dev, dev->driver);

			} else {
				DBG(DBG_VERBOSE, "USB reset end\n");
				dev->gadget.speed = USB_SPEED_FULL;
				LED_CONNECTED_ON;
				memset(&dev->stats, 0, sizeof dev->stats);
				/* driver and endpoints are still reset */
			}

		} else {
			u32	usir0 = USIR0 & ~UICR0;
			u32	usir1 = USIR1 & ~UICR1;
			int	i;

			if (unlikely (!usir0 && !usir1))
				continue;

			DBG(DBG_VERY_NOISY, "irq %02x.%02x\n", usir1, usir0);

			/* control traffic */
			if (usir0 & USIR0_IR0) {
				dev->ep[0].pio_irqs++;
				handle_ep0(dev);
				handled = 1;
			}

			/* endpoint data transfers */
			for (i = 0; i < 8; i++) {
				u32	tmp = 1 << i;

				if (i && (usir0 & tmp)) {
					handle_ep(&dev->ep[i]);
					USIR0 |= tmp;
					handled = 1;
				}
				if (usir1 & tmp) {
					handle_ep(&dev->ep[i+8]);
					USIR1 |= tmp;
					handled = 1;
				}
			}
		}

		/* we could also ask for 1 msec SOF (SIR) interrupts */

	} while (handled);
	return IRQ_HANDLED;
}

/*-------------------------------------------------------------------------*/

static void nop_release (struct device *dev)
{
	DMSG("%s %s\n", __FUNCTION__, dev->bus_id);
}

/* this uses load-time allocation and initialization (instead of
 * doing it at run-time) to save code, eliminate fault paths, and
 * be more obviously correct.
 */
static struct pxa2xx_udc memory = {
	.gadget = {
		.ops		= &pxa2xx_udc_ops,
		.ep0		= &memory.ep[0].ep,
		.name		= driver_name,
		.dev = {
			.bus_id		= "gadget",
			.release	= nop_release,
		},
	},

	/* control endpoint */
	.ep[0] = {
		.ep = {
			.name		= ep0name,
			.ops		= &pxa2xx_ep_ops,
			.maxpacket	= EP0_FIFO_SIZE,
		},
		.dev		= &memory,
		.reg_udccs	= &UDCCS0,
		.reg_uddr	= &UDDR0,
	},

	/* first group of endpoints */
	.ep[1] = {
		.ep = {
			.name		= "ep1in-bulk",
			.ops		= &pxa2xx_ep_ops,
			.maxpacket	= BULK_FIFO_SIZE,
		},
		.dev		= &memory,
		.fifo_size	= BULK_FIFO_SIZE,
		.bEndpointAddress = USB_DIR_IN | 1,
		.bmAttributes	= USB_ENDPOINT_XFER_BULK,
		.reg_udccs	= &UDCCS1,
		.reg_uddr	= &UDDR1,
		drcmr (25)
	},
	.ep[2] = {
		.ep = {
			.name		= "ep2out-bulk",
			.ops		= &pxa2xx_ep_ops,
			.maxpacket	= BULK_FIFO_SIZE,
		},
		.dev		= &memory,
		.fifo_size	= BULK_FIFO_SIZE,
		.bEndpointAddress = 2,
		.bmAttributes	= USB_ENDPOINT_XFER_BULK,
		.reg_udccs	= &UDCCS2,
		.reg_ubcr	= &UBCR2,
		.reg_uddr	= &UDDR2,
		drcmr (26)
	},
#ifndef CONFIG_USB_PXA2XX_SMALL
	.ep[3] = {
		.ep = {
			.name		= "ep3in-iso",
			.ops		= &pxa2xx_ep_ops,
			.maxpacket	= ISO_FIFO_SIZE,
		},
		.dev		= &memory,
		.fifo_size	= ISO_FIFO_SIZE,
		.bEndpointAddress = USB_DIR_IN | 3,
		.bmAttributes	= USB_ENDPOINT_XFER_ISOC,
		.reg_udccs	= &UDCCS3,
		.reg_uddr	= &UDDR3,
		drcmr (27)
	},
	.ep[4] = {
		.ep = {
			.name		= "ep4out-iso",
			.ops		= &pxa2xx_ep_ops,
			.maxpacket	= ISO_FIFO_SIZE,
		},
		.dev		= &memory,
		.fifo_size	= ISO_FIFO_SIZE,
		.bEndpointAddress = 4,
		.bmAttributes	= USB_ENDPOINT_XFER_ISOC,
		.reg_udccs	= &UDCCS4,
		.reg_ubcr	= &UBCR4,
		.reg_uddr	= &UDDR4,
		drcmr (28)
	},
	.ep[5] = {
		.ep = {
			.name		= "ep5in-int",
			.ops		= &pxa2xx_ep_ops,
			.maxpacket	= INT_FIFO_SIZE,
		},
		.dev		= &memory,
		.fifo_size	= INT_FIFO_SIZE,
		.bEndpointAddress = USB_DIR_IN | 5,
		.bmAttributes	= USB_ENDPOINT_XFER_INT,
		.reg_udccs	= &UDCCS5,
		.reg_uddr	= &UDDR5,
	},

	/* second group of endpoints */
	.ep[6] = {
		.ep = {
			.name		= "ep6in-bulk",
			.ops		= &pxa2xx_ep_ops,
			.maxpacket	= BULK_FIFO_SIZE,
		},
		.dev		= &memory,
		.fifo_size	= BULK_FIFO_SIZE,
		.bEndpointAddress = USB_DIR_IN | 6,
		.bmAttributes	= USB_ENDPOINT_XFER_BULK,
		.reg_udccs	= &UDCCS6,
		.reg_uddr	= &UDDR6,
		drcmr (30)
	},
	.ep[7] = {
		.ep = {
			.name		= "ep7out-bulk",
			.ops		= &pxa2xx_ep_ops,
			.maxpacket	= BULK_FIFO_SIZE,
		},
		.dev		= &memory,
		.fifo_size	= BULK_FIFO_SIZE,
		.bEndpointAddress = 7,
		.bmAttributes	= USB_ENDPOINT_XFER_BULK,
		.reg_udccs	= &UDCCS7,
		.reg_ubcr	= &UBCR7,
		.reg_uddr	= &UDDR7,
		drcmr (31)
	},
	.ep[8] = {
		.ep = {
			.name		= "ep8in-iso",
			.ops		= &pxa2xx_ep_ops,
			.maxpacket	= ISO_FIFO_SIZE,
		},
		.dev		= &memory,
		.fifo_size	= ISO_FIFO_SIZE,
		.bEndpointAddress = USB_DIR_IN | 8,
		.bmAttributes	= USB_ENDPOINT_XFER_ISOC,
		.reg_udccs	= &UDCCS8,
		.reg_uddr	= &UDDR8,
		drcmr (32)
	},
	.ep[9] = {
		.ep = {
			.name		= "ep9out-iso",
			.ops		= &pxa2xx_ep_ops,
			.maxpacket	= ISO_FIFO_SIZE,
		},
		.dev		= &memory,
		.fifo_size	= ISO_FIFO_SIZE,
		.bEndpointAddress = 9,
		.bmAttributes	= USB_ENDPOINT_XFER_ISOC,
		.reg_udccs	= &UDCCS9,
		.reg_ubcr	= &UBCR9,
		.reg_uddr	= &UDDR9,
		drcmr (33)
	},
	.ep[10] = {
		.ep = {
			.name		= "ep10in-int",
			.ops		= &pxa2xx_ep_ops,
			.maxpacket	= INT_FIFO_SIZE,
		},
		.dev		= &memory,
		.fifo_size	= INT_FIFO_SIZE,
		.bEndpointAddress = USB_DIR_IN | 10,
		.bmAttributes	= USB_ENDPOINT_XFER_INT,
		.reg_udccs	= &UDCCS10,
		.reg_uddr	= &UDDR10,
	},

	/* third group of endpoints */
	.ep[11] = {
		.ep = {
			.name		= "ep11in-bulk",
			.ops		= &pxa2xx_ep_ops,
			.maxpacket	= BULK_FIFO_SIZE,
		},
		.dev		= &memory,
		.fifo_size	= BULK_FIFO_SIZE,
		.bEndpointAddress = USB_DIR_IN | 11,
		.bmAttributes	= USB_ENDPOINT_XFER_BULK,
		.reg_udccs	= &UDCCS11,
		.reg_uddr	= &UDDR11,
		drcmr (35)
	},
	.ep[12] = {
		.ep = {
			.name		= "ep12out-bulk",
			.ops		= &pxa2xx_ep_ops,
			.maxpacket	= BULK_FIFO_SIZE,
		},
		.dev		= &memory,
		.fifo_size	= BULK_FIFO_SIZE,
		.bEndpointAddress = 12,
		.bmAttributes	= USB_ENDPOINT_XFER_BULK,
		.reg_udccs	= &UDCCS12,
		.reg_ubcr	= &UBCR12,
		.reg_uddr	= &UDDR12,
		drcmr (36)
	},
	.ep[13] = {
		.ep = {
			.name		= "ep13in-iso",
			.ops		= &pxa2xx_ep_ops,
			.maxpacket	= ISO_FIFO_SIZE,
		},
		.dev		= &memory,
		.fifo_size	= ISO_FIFO_SIZE,
		.bEndpointAddress = USB_DIR_IN | 13,
		.bmAttributes	= USB_ENDPOINT_XFER_ISOC,
		.reg_udccs	= &UDCCS13,
		.reg_uddr	= &UDDR13,
		drcmr (37)
	},
	.ep[14] = {
		.ep = {
			.name		= "ep14out-iso",
			.ops		= &pxa2xx_ep_ops,
			.maxpacket	= ISO_FIFO_SIZE,
		},
		.dev		= &memory,
		.fifo_size	= ISO_FIFO_SIZE,
		.bEndpointAddress = 14,
		.bmAttributes	= USB_ENDPOINT_XFER_ISOC,
		.reg_udccs	= &UDCCS14,
		.reg_ubcr	= &UBCR14,
		.reg_uddr	= &UDDR14,
		drcmr (38)
	},
	.ep[15] = {
		.ep = {
			.name		= "ep15in-int",
			.ops		= &pxa2xx_ep_ops,
			.maxpacket	= INT_FIFO_SIZE,
		},
		.dev		= &memory,
		.fifo_size	= INT_FIFO_SIZE,
		.bEndpointAddress = USB_DIR_IN | 15,
		.bmAttributes	= USB_ENDPOINT_XFER_INT,
		.reg_udccs	= &UDCCS15,
		.reg_uddr	= &UDDR15,
	},
#endif /* !CONFIG_USB_PXA2XX_SMALL */
};

#define CP15R0_VENDOR_MASK	0xffffe000

#if	defined(CONFIG_ARCH_PXA)
#define CP15R0_XSCALE_VALUE	0x69052000	/* intel/arm/xscale */

#elif	defined(CONFIG_ARCH_IXP4XX)
#define CP15R0_XSCALE_VALUE	0x69054000	/* intel/arm/ixp4xx */

#endif

#define CP15R0_PROD_MASK	0x000003f0
#define PXA25x			0x00000100	/* and PXA26x */
#define PXA210			0x00000120

#define CP15R0_REV_MASK		0x0000000f

#define CP15R0_PRODREV_MASK	(CP15R0_PROD_MASK | CP15R0_REV_MASK)

#define PXA255_A0		0x00000106	/* or PXA260_B1 */
#define PXA250_C0		0x00000105	/* or PXA26x_B0 */
#define PXA250_B2		0x00000104
#define PXA250_B1		0x00000103	/* or PXA260_A0 */
#define PXA250_B0		0x00000102
#define PXA250_A1		0x00000101
#define PXA250_A0		0x00000100

#define PXA210_C0		0x00000125
#define PXA210_B2		0x00000124
#define PXA210_B1		0x00000123
#define PXA210_B0		0x00000122
#define IXP425_A0		0x000001c1

/*
 * 	probe - binds to the platform device
 */
static int __init pxa2xx_udc_probe(struct device *_dev)
{
	struct pxa2xx_udc *dev = &memory;
	int retval, out_dma = 1;
	u32 chiprev;

	/* insist on Intel/ARM/XScale */
	asm("mrc%? p15, 0, %0, c0, c0" : "=r" (chiprev));
	if ((chiprev & CP15R0_VENDOR_MASK) != CP15R0_XSCALE_VALUE) {
		printk(KERN_ERR "%s: not XScale!\n", driver_name);
		return -ENODEV;
	}

	/* trigger chiprev-specific logic */
	switch (chiprev & CP15R0_PRODREV_MASK) {
#if	defined(CONFIG_ARCH_PXA)
	case PXA255_A0:
		dev->has_cfr = 1;
		break;
	case PXA250_A0:
	case PXA250_A1:
		/* A0/A1 "not released"; ep 13, 15 unusable */
		/* fall through */
	case PXA250_B2: case PXA210_B2:
	case PXA250_B1: case PXA210_B1:
	case PXA250_B0: case PXA210_B0:
		out_dma = 0;
		/* fall through */
	case PXA250_C0: case PXA210_C0:
		break;
#elif	defined(CONFIG_ARCH_IXP4XX)
	case IXP425_A0:
		out_dma = 0;
		break;
#endif
	default:
		out_dma = 0;
		printk(KERN_ERR "%s: unrecognized processor: %08x\n",
			driver_name, chiprev);
		/* iop3xx, ixp4xx, ... */
		return -ENODEV;
	}

	pr_debug("%s: IRQ %d%s%s%s\n", driver_name, IRQ_USB,
		dev->has_cfr ? "" : " (!cfr)",
		out_dma ? "" : " (broken dma-out)",
		SIZE_STR DMASTR
		);

#ifdef	USE_DMA
#ifndef	USE_OUT_DMA
	out_dma = 0;
#endif
	/* pxa 250 erratum 130 prevents using OUT dma (fixed C0) */
	if (!out_dma) {
		DMSG("disabled OUT dma\n");
		dev->ep[ 2].reg_drcmr = dev->ep[ 4].reg_drcmr = 0;
		dev->ep[ 7].reg_drcmr = dev->ep[ 9].reg_drcmr = 0;
		dev->ep[12].reg_drcmr = dev->ep[14].reg_drcmr = 0;
	}
#endif

	/* other non-static parts of init */
	dev->dev = _dev;
	dev->mach = _dev->platform_data;

	init_timer(&dev->timer);
	dev->timer.function = udc_watchdog;
	dev->timer.data = (unsigned long) dev;

	device_initialize(&dev->gadget.dev);
	dev->gadget.dev.parent = _dev;
	dev->gadget.dev.dma_mask = _dev->dma_mask;

	the_controller = dev;
	dev_set_drvdata(_dev, dev);

	udc_disable(dev);
	udc_reinit(dev);

	dev->vbus = is_vbus_present();

	/* irq setup after old hardware state is cleaned up */
	retval = request_irq(IRQ_USB, pxa2xx_udc_irq,
			SA_INTERRUPT, driver_name, dev);
	if (retval != 0) {
		printk(KERN_ERR "%s: can't get irq %i, err %d\n",
			driver_name, IRQ_USB, retval);
		return -EBUSY;
	}
	dev->got_irq = 1;

#ifdef CONFIG_ARCH_LUBBOCK
	if (machine_is_lubbock()) {
		retval = request_irq(LUBBOCK_USB_DISC_IRQ,
				lubbock_vbus_irq,
				SA_INTERRUPT | SA_SAMPLE_RANDOM,
				driver_name, dev);
		if (retval != 0) {
			printk(KERN_ERR "%s: can't get irq %i, err %d\n",
				driver_name, LUBBOCK_USB_DISC_IRQ, retval);
lubbock_fail0:
			free_irq(IRQ_USB, dev);
			return -EBUSY;
		}
		retval = request_irq(LUBBOCK_USB_IRQ,
				lubbock_vbus_irq,
				SA_INTERRUPT | SA_SAMPLE_RANDOM,
				driver_name, dev);
		if (retval != 0) {
			printk(KERN_ERR "%s: can't get irq %i, err %d\n",
				driver_name, LUBBOCK_USB_IRQ, retval);
			free_irq(LUBBOCK_USB_DISC_IRQ, dev);
			goto lubbock_fail0;
		}
#ifdef DEBUG
		/* with U-Boot (but not BLOB), hex is off by default */
		HEX_DISPLAY(dev->stats.irqs);
		LUB_DISC_BLNK_LED &= 0xff;
#endif
	}
#endif
	create_proc_files();

	return 0;
}

static void pxa2xx_udc_shutdown(struct device *_dev)
{
	pullup_off();
}

static int __exit pxa2xx_udc_remove(struct device *_dev)
{
	struct pxa2xx_udc *dev = dev_get_drvdata(_dev);

	udc_disable(dev);
	remove_proc_files();
	usb_gadget_unregister_driver(dev->driver);

	if (dev->got_irq) {
		free_irq(IRQ_USB, dev);
		dev->got_irq = 0;
	}
	if (machine_is_lubbock()) {
		free_irq(LUBBOCK_USB_DISC_IRQ, dev);
		free_irq(LUBBOCK_USB_IRQ, dev);
	}
	dev_set_drvdata(_dev, NULL);
	the_controller = NULL;
	return 0;
}

/*-------------------------------------------------------------------------*/

#ifdef	CONFIG_PM

/* USB suspend (controlled by the host) and system suspend (controlled
 * by the PXA) don't necessarily work well together.  If USB is active,
 * the 48 MHz clock is required; so the system can't enter 33 MHz idle
 * mode, or any deeper PM saving state.
 *
 * For now, we punt and forcibly disconnect from the USB host when PXA
 * enters any suspend state.  While we're disconnected, we always disable
 * the 48MHz USB clock ... allowing PXA sleep and/or 33 MHz idle states. 
 * Boards without software pullup control shouldn't use those states.
 * VBUS IRQs should probably be ignored so that the PXA device just acts
 * "dead" to USB hosts until system resume.
 */
static int pxa2xx_udc_suspend(struct device *dev, pm_message_t state)
{
	struct pxa2xx_udc	*udc = dev_get_drvdata(dev);

	if (!udc->mach->udc_command)
		WARN("USB host won't detect disconnect!\n");
	pullup(udc, 0);

	return 0;
}

static int pxa2xx_udc_resume(struct device *dev)
{
	struct pxa2xx_udc	*udc = dev_get_drvdata(dev);

	pullup(udc, 1);

	return 0;
}

#else
#define	pxa2xx_udc_suspend	NULL
#define	pxa2xx_udc_resume	NULL
#endif

/*-------------------------------------------------------------------------*/

static struct device_driver udc_driver = {
	.name		= "pxa2xx-udc",
	.owner		= THIS_MODULE,
	.bus		= &platform_bus_type,
	.probe		= pxa2xx_udc_probe,
	.shutdown	= pxa2xx_udc_shutdown,
	.remove		= __exit_p(pxa2xx_udc_remove),
	.suspend	= pxa2xx_udc_suspend,
	.resume		= pxa2xx_udc_resume,
};

static int __init udc_init(void)
{
	printk(KERN_INFO "%s: version %s\n", driver_name, DRIVER_VERSION);
	return driver_register(&udc_driver);
}
module_init(udc_init);

static void __exit udc_exit(void)
{
	driver_unregister(&udc_driver);
}
module_exit(udc_exit);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR("Frank Becker, Robert Schwebel, David Brownell");
MODULE_LICENSE("GPL");

