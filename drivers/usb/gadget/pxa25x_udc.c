/*
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
 */

/* #define VERBOSE_DEBUG */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <linux/platform_data/pxa2xx_udc.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/irq.h>
#include <linux/clk.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>
#include <linux/io.h>
#include <linux/prefetch.h>

#include <asm/byteorder.h>
#include <asm/dma.h>
#include <asm/gpio.h>
#include <asm/mach-types.h>
#include <asm/unaligned.h>

#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/usb/otg.h>

/*
 * This driver is PXA25x only.  Grab the right register definitions.
 */
#ifdef CONFIG_ARCH_PXA
#include <mach/pxa25x-udc.h>
#include <mach/hardware.h>
#endif

#ifdef CONFIG_ARCH_LUBBOCK
#include <mach/lubbock.h>
#endif

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
 *
 * Note that the UDC hardware supports DMA (except on IXP) but that's
 * not used here.  IN-DMA (to host) is simple enough, when the data is
 * suitably aligned (16 bytes) ... the network stack doesn't do that,
 * other software can.  OUT-DMA is buggy in most chip versions, as well
 * as poorly designed (data toggle not automatic).  So this driver won't
 * bother using DMA.  (Mostly-working IN-DMA support was available in
 * kernels before 2.6.23, but was never enabled or well tested.)
 */

#define	DRIVER_VERSION	"30-June-2007"
#define	DRIVER_DESC	"PXA 25x USB Device Controller driver"


static const char driver_name [] = "pxa25x_udc";

static const char ep0name [] = "ep0";


#ifdef CONFIG_ARCH_IXP4XX

/* cpu-specific register addresses are compiled in to this code */
#ifdef CONFIG_ARCH_PXA
#error "Can't configure both IXP and PXA"
#endif

/* IXP doesn't yet support <linux/clk.h> */
#define clk_get(dev,name)	NULL
#define clk_enable(clk)		do { } while (0)
#define clk_disable(clk)	do { } while (0)
#define clk_put(clk)		do { } while (0)

#endif

#include "pxa25x_udc.h"


#ifdef	CONFIG_USB_PXA25X_SMALL
#define SIZE_STR	" (small)"
#else
#define SIZE_STR	""
#endif

/* ---------------------------------------------------------------------------
 *	endpoint related parts of the api to the usb controller hardware,
 *	used by gadget driver; and the inner talker-to-hardware core.
 * ---------------------------------------------------------------------------
 */

static void pxa25x_ep_fifo_flush (struct usb_ep *ep);
static void nuke (struct pxa25x_ep *, int status);

/* one GPIO should control a D+ pullup, so host sees this device (or not) */
static void pullup_off(void)
{
	struct pxa2xx_udc_mach_info		*mach = the_controller->mach;
	int off_level = mach->gpio_pullup_inverted;

	if (gpio_is_valid(mach->gpio_pullup))
		gpio_set_value(mach->gpio_pullup, off_level);
	else if (mach->udc_command)
		mach->udc_command(PXA2XX_UDC_CMD_DISCONNECT);
}

static void pullup_on(void)
{
	struct pxa2xx_udc_mach_info		*mach = the_controller->mach;
	int on_level = !mach->gpio_pullup_inverted;

	if (gpio_is_valid(mach->gpio_pullup))
		gpio_set_value(mach->gpio_pullup, on_level);
	else if (mach->udc_command)
		mach->udc_command(PXA2XX_UDC_CMD_CONNECT);
}

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
 * we need to verify the descriptors used to enable endpoints.  since pxa25x
 * endpoint configurations are fixed, and are pretty much always enabled,
 * there's not a lot to manage here.
 *
 * because pxa25x can't selectively initialize bulk (or interrupt) endpoints,
 * (resetting endpoint halt and toggle), SET_INTERFACE is unusable except
 * for a single interface (with only the default altsetting) and for gadget
 * drivers that don't halt endpoints (not reset by set_interface).  that also
 * means that if you use ISO, you must violate the USB spec rule that all
 * iso endpoints must be in non-default altsettings.
 */
static int pxa25x_ep_enable (struct usb_ep *_ep,
		const struct usb_endpoint_descriptor *desc)
{
	struct pxa25x_ep        *ep;
	struct pxa25x_udc       *dev;

	ep = container_of (_ep, struct pxa25x_ep, ep);
	if (!_ep || !desc || _ep->name == ep0name
			|| desc->bDescriptorType != USB_DT_ENDPOINT
			|| ep->bEndpointAddress != desc->bEndpointAddress
			|| ep->fifo_size < usb_endpoint_maxp (desc)) {
		DMSG("%s, bad ep or descriptor\n", __func__);
		return -EINVAL;
	}

	/* xfer types must match, except that interrupt ~= bulk */
	if (ep->bmAttributes != desc->bmAttributes
			&& ep->bmAttributes != USB_ENDPOINT_XFER_BULK
			&& desc->bmAttributes != USB_ENDPOINT_XFER_INT) {
		DMSG("%s, %s type mismatch\n", __func__, _ep->name);
		return -EINVAL;
	}

	/* hardware _could_ do smaller, but driver doesn't */
	if ((desc->bmAttributes == USB_ENDPOINT_XFER_BULK
				&& usb_endpoint_maxp (desc)
						!= BULK_FIFO_SIZE)
			|| !desc->wMaxPacketSize) {
		DMSG("%s, bad %s maxpacket\n", __func__, _ep->name);
		return -ERANGE;
	}

	dev = ep->dev;
	if (!dev->driver || dev->gadget.speed == USB_SPEED_UNKNOWN) {
		DMSG("%s, bogus device state\n", __func__);
		return -ESHUTDOWN;
	}

	ep->ep.desc = desc;
	ep->stopped = 0;
	ep->pio_irqs = 0;
	ep->ep.maxpacket = usb_endpoint_maxp (desc);

	/* flush fifo (mostly for OUT buffers) */
	pxa25x_ep_fifo_flush (_ep);

	/* ... reset halt state too, if we could ... */

	DBG(DBG_VERBOSE, "enabled %s\n", _ep->name);
	return 0;
}

static int pxa25x_ep_disable (struct usb_ep *_ep)
{
	struct pxa25x_ep	*ep;
	unsigned long		flags;

	ep = container_of (_ep, struct pxa25x_ep, ep);
	if (!_ep || !ep->ep.desc) {
		DMSG("%s, %s not enabled\n", __func__,
			_ep ? ep->ep.name : NULL);
		return -EINVAL;
	}
	local_irq_save(flags);

	nuke (ep, -ESHUTDOWN);

	/* flush fifo (mostly for IN buffers) */
	pxa25x_ep_fifo_flush (_ep);

	ep->ep.desc = NULL;
	ep->stopped = 1;

	local_irq_restore(flags);
	DBG(DBG_VERBOSE, "%s disabled\n", _ep->name);
	return 0;
}

/*-------------------------------------------------------------------------*/

/* for the pxa25x, these can just wrap kmalloc/kfree.  gadget drivers
 * must still pass correctly initialized endpoints, since other controller
 * drivers may care about how it's currently set up (dma issues etc).
 */

/*
 *	pxa25x_ep_alloc_request - allocate a request data structure
 */
static struct usb_request *
pxa25x_ep_alloc_request (struct usb_ep *_ep, gfp_t gfp_flags)
{
	struct pxa25x_request *req;

	req = kzalloc(sizeof(*req), gfp_flags);
	if (!req)
		return NULL;

	INIT_LIST_HEAD (&req->queue);
	return &req->req;
}


/*
 *	pxa25x_ep_free_request - deallocate a request data structure
 */
static void
pxa25x_ep_free_request (struct usb_ep *_ep, struct usb_request *_req)
{
	struct pxa25x_request	*req;

	req = container_of (_req, struct pxa25x_request, req);
	WARN_ON(!list_empty (&req->queue));
	kfree(req);
}

/*-------------------------------------------------------------------------*/

/*
 *	done - retire a request; caller blocked irqs
 */
static void done(struct pxa25x_ep *ep, struct pxa25x_request *req, int status)
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


static inline void ep0_idle (struct pxa25x_udc *dev)
{
	dev->ep0state = EP0_IDLE;
}

static int
write_packet(volatile u32 *uddr, struct pxa25x_request *req, unsigned max)
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
write_fifo (struct pxa25x_ep *ep, struct pxa25x_request *req)
{
	unsigned		max;

	max = usb_endpoint_maxp(ep->ep.desc);
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
			if (list_empty(&ep->queue))
				pio_irq_disable (ep->bEndpointAddress);
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
void ep0start(struct pxa25x_udc *dev, u32 flags, const char *tag)
{
	UDCCS0 = flags|UDCCS0_SA|UDCCS0_OPR;
	USIR0 = USIR0_IR0;
	dev->req_pending = 0;
	DBG(DBG_VERY_NOISY, "%s %s, %02x/%02x\n",
		__func__, tag, UDCCS0, flags);
}

static int
write_ep0_fifo (struct pxa25x_ep *ep, struct pxa25x_request *req)
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
#ifndef CONFIG_ARCH_IXP4XX
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
read_fifo (struct pxa25x_ep *ep, struct pxa25x_request *req)
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
read_ep0_fifo (struct pxa25x_ep *ep, struct pxa25x_request *req)
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

/*-------------------------------------------------------------------------*/

static int
pxa25x_ep_queue(struct usb_ep *_ep, struct usb_request *_req, gfp_t gfp_flags)
{
	struct pxa25x_request	*req;
	struct pxa25x_ep	*ep;
	struct pxa25x_udc	*dev;
	unsigned long		flags;

	req = container_of(_req, struct pxa25x_request, req);
	if (unlikely (!_req || !_req->complete || !_req->buf
			|| !list_empty(&req->queue))) {
		DMSG("%s, bad params\n", __func__);
		return -EINVAL;
	}

	ep = container_of(_ep, struct pxa25x_ep, ep);
	if (unlikely(!_ep || (!ep->ep.desc && ep->ep.name != ep0name))) {
		DMSG("%s, bad ep\n", __func__);
		return -EINVAL;
	}

	dev = ep->dev;
	if (unlikely (!dev->driver
			|| dev->gadget.speed == USB_SPEED_UNKNOWN)) {
		DMSG("%s, bogus device state\n", __func__);
		return -ESHUTDOWN;
	}

	/* iso is always one packet per request, that's the only way
	 * we can report per-packet status.  that also helps with dma.
	 */
	if (unlikely (ep->bmAttributes == USB_ENDPOINT_XFER_ISOC
			&& req->req.length > usb_endpoint_maxp(ep->ep.desc)))
		return -EMSGSIZE;

	DBG(DBG_NOISY, "%s queue req %p, len %d buf %p\n",
		_ep->name, _req, _req->length, _req->buf);

	local_irq_save(flags);

	_req->status = -EINPROGRESS;
	_req->actual = 0;

	/* kickstart this i/o queue? */
	if (list_empty(&ep->queue) && !ep->stopped) {
		if (ep->ep.desc == NULL/* ep0 */) {
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
		/* can the FIFO can satisfy the request immediately? */
		} else if ((ep->bEndpointAddress & USB_DIR_IN) != 0) {
			if ((*ep->reg_udccs & UDCCS_BI_TFS) != 0
					&& write_fifo(ep, req))
				req = NULL;
		} else if ((*ep->reg_udccs & UDCCS_BO_RFS) != 0
				&& read_fifo(ep, req)) {
			req = NULL;
		}

		if (likely(req && ep->ep.desc))
			pio_irq_enable(ep->bEndpointAddress);
	}

	/* pio or dma irq handler advances the queue. */
	if (likely(req != NULL))
		list_add_tail(&req->queue, &ep->queue);
	local_irq_restore(flags);

	return 0;
}


/*
 *	nuke - dequeue ALL requests
 */
static void nuke(struct pxa25x_ep *ep, int status)
{
	struct pxa25x_request *req;

	/* called with irqs blocked */
	while (!list_empty(&ep->queue)) {
		req = list_entry(ep->queue.next,
				struct pxa25x_request,
				queue);
		done(ep, req, status);
	}
	if (ep->ep.desc)
		pio_irq_disable (ep->bEndpointAddress);
}


/* dequeue JUST ONE request */
static int pxa25x_ep_dequeue(struct usb_ep *_ep, struct usb_request *_req)
{
	struct pxa25x_ep	*ep;
	struct pxa25x_request	*req;
	unsigned long		flags;

	ep = container_of(_ep, struct pxa25x_ep, ep);
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

	done(ep, req, -ECONNRESET);

	local_irq_restore(flags);
	return 0;
}

/*-------------------------------------------------------------------------*/

static int pxa25x_ep_set_halt(struct usb_ep *_ep, int value)
{
	struct pxa25x_ep	*ep;
	unsigned long		flags;

	ep = container_of(_ep, struct pxa25x_ep, ep);
	if (unlikely (!_ep
			|| (!ep->ep.desc && ep->ep.name != ep0name))
			|| ep->bmAttributes == USB_ENDPOINT_XFER_ISOC) {
		DMSG("%s, bad ep\n", __func__);
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
	if (!ep->ep.desc) {
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

static int pxa25x_ep_fifo_status(struct usb_ep *_ep)
{
	struct pxa25x_ep        *ep;

	ep = container_of(_ep, struct pxa25x_ep, ep);
	if (!_ep) {
		DMSG("%s, bad ep\n", __func__);
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

static void pxa25x_ep_fifo_flush(struct usb_ep *_ep)
{
	struct pxa25x_ep        *ep;

	ep = container_of(_ep, struct pxa25x_ep, ep);
	if (!_ep || ep->ep.name == ep0name || !list_empty(&ep->queue)) {
		DMSG("%s, bad ep\n", __func__);
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
		| (ep->bmAttributes == USB_ENDPOINT_XFER_ISOC
			? 0 : UDCCS_BI_SST);
}


static struct usb_ep_ops pxa25x_ep_ops = {
	.enable		= pxa25x_ep_enable,
	.disable	= pxa25x_ep_disable,

	.alloc_request	= pxa25x_ep_alloc_request,
	.free_request	= pxa25x_ep_free_request,

	.queue		= pxa25x_ep_queue,
	.dequeue	= pxa25x_ep_dequeue,

	.set_halt	= pxa25x_ep_set_halt,
	.fifo_status	= pxa25x_ep_fifo_status,
	.fifo_flush	= pxa25x_ep_fifo_flush,
};


/* ---------------------------------------------------------------------------
 *	device-scoped parts of the api to the usb controller hardware
 * ---------------------------------------------------------------------------
 */

static int pxa25x_udc_get_frame(struct usb_gadget *_gadget)
{
	return ((UFNRH & 0x07) << 8) | (UFNRL & 0xff);
}

static int pxa25x_udc_wakeup(struct usb_gadget *_gadget)
{
	/* host may not have enabled remote wakeup */
	if ((UDCCS0 & UDCCS0_DRWF) == 0)
		return -EHOSTUNREACH;
	udc_set_mask_UDCCR(UDCCR_RSM);
	return 0;
}

static void stop_activity(struct pxa25x_udc *, struct usb_gadget_driver *);
static void udc_enable (struct pxa25x_udc *);
static void udc_disable(struct pxa25x_udc *);

/* We disable the UDC -- and its 48 MHz clock -- whenever it's not
 * in active use.
 */
static int pullup(struct pxa25x_udc *udc)
{
	int is_active = udc->vbus && udc->pullup && !udc->suspended;
	DMSG("%s\n", is_active ? "active" : "inactive");
	if (is_active) {
		if (!udc->active) {
			udc->active = 1;
			/* Enable clock for USB device */
			clk_enable(udc->clk);
			udc_enable(udc);
		}
	} else {
		if (udc->active) {
			if (udc->gadget.speed != USB_SPEED_UNKNOWN) {
				DMSG("disconnect %s\n", udc->driver
					? udc->driver->driver.name
					: "(no driver)");
				stop_activity(udc, udc->driver);
			}
			udc_disable(udc);
			/* Disable clock for USB device */
			clk_disable(udc->clk);
			udc->active = 0;
		}

	}
	return 0;
}

/* VBUS reporting logically comes from a transceiver */
static int pxa25x_udc_vbus_session(struct usb_gadget *_gadget, int is_active)
{
	struct pxa25x_udc	*udc;

	udc = container_of(_gadget, struct pxa25x_udc, gadget);
	udc->vbus = is_active;
	DMSG("vbus %s\n", is_active ? "supplied" : "inactive");
	pullup(udc);
	return 0;
}

/* drivers may have software control over D+ pullup */
static int pxa25x_udc_pullup(struct usb_gadget *_gadget, int is_active)
{
	struct pxa25x_udc	*udc;

	udc = container_of(_gadget, struct pxa25x_udc, gadget);

	/* not all boards support pullup control */
	if (!gpio_is_valid(udc->mach->gpio_pullup) && !udc->mach->udc_command)
		return -EOPNOTSUPP;

	udc->pullup = (is_active != 0);
	pullup(udc);
	return 0;
}

/* boards may consume current from VBUS, up to 100-500mA based on config.
 * the 500uA suspend ceiling means that exclusively vbus-powered PXA designs
 * violate USB specs.
 */
static int pxa25x_udc_vbus_draw(struct usb_gadget *_gadget, unsigned mA)
{
	struct pxa25x_udc	*udc;

	udc = container_of(_gadget, struct pxa25x_udc, gadget);

	if (!IS_ERR_OR_NULL(udc->transceiver))
		return usb_phy_set_power(udc->transceiver, mA);
	return -EOPNOTSUPP;
}

static int pxa25x_udc_start(struct usb_gadget *g,
		struct usb_gadget_driver *driver);
static int pxa25x_udc_stop(struct usb_gadget *g,
		struct usb_gadget_driver *driver);

static const struct usb_gadget_ops pxa25x_udc_ops = {
	.get_frame	= pxa25x_udc_get_frame,
	.wakeup		= pxa25x_udc_wakeup,
	.vbus_session	= pxa25x_udc_vbus_session,
	.pullup		= pxa25x_udc_pullup,
	.vbus_draw	= pxa25x_udc_vbus_draw,
	.udc_start	= pxa25x_udc_start,
	.udc_stop	= pxa25x_udc_stop,
};

/*-------------------------------------------------------------------------*/

#ifdef CONFIG_USB_GADGET_DEBUG_FS

static int
udc_seq_show(struct seq_file *m, void *_d)
{
	struct pxa25x_udc	*dev = m->private;
	unsigned long		flags;
	int			i;
	u32			tmp;

	local_irq_save(flags);

	/* basic device status */
	seq_printf(m, DRIVER_DESC "\n"
		"%s version: %s\nGadget driver: %s\nHost %s\n\n",
		driver_name, DRIVER_VERSION SIZE_STR "(pio)",
		dev->driver ? dev->driver->driver.name : "(none)",
		dev->gadget.speed == USB_SPEED_FULL ? "full speed" : "disconnected");

	/* registers for device and ep0 */
	seq_printf(m,
		"uicr %02X.%02X, usir %02X.%02x, ufnr %02X.%02X\n",
		UICR1, UICR0, USIR1, USIR0, UFNRH, UFNRL);

	tmp = UDCCR;
	seq_printf(m,
		"udccr %02X =%s%s%s%s%s%s%s%s\n", tmp,
		(tmp & UDCCR_REM) ? " rem" : "",
		(tmp & UDCCR_RSTIR) ? " rstir" : "",
		(tmp & UDCCR_SRM) ? " srm" : "",
		(tmp & UDCCR_SUSIR) ? " susir" : "",
		(tmp & UDCCR_RESIR) ? " resir" : "",
		(tmp & UDCCR_RSM) ? " rsm" : "",
		(tmp & UDCCR_UDA) ? " uda" : "",
		(tmp & UDCCR_UDE) ? " ude" : "");

	tmp = UDCCS0;
	seq_printf(m,
		"udccs0 %02X =%s%s%s%s%s%s%s%s\n", tmp,
		(tmp & UDCCS0_SA) ? " sa" : "",
		(tmp & UDCCS0_RNE) ? " rne" : "",
		(tmp & UDCCS0_FST) ? " fst" : "",
		(tmp & UDCCS0_SST) ? " sst" : "",
		(tmp & UDCCS0_DRWF) ? " dwrf" : "",
		(tmp & UDCCS0_FTF) ? " ftf" : "",
		(tmp & UDCCS0_IPR) ? " ipr" : "",
		(tmp & UDCCS0_OPR) ? " opr" : "");

	if (dev->has_cfr) {
		tmp = UDCCFR;
		seq_printf(m,
			"udccfr %02X =%s%s\n", tmp,
			(tmp & UDCCFR_AREN) ? " aren" : "",
			(tmp & UDCCFR_ACM) ? " acm" : "");
	}

	if (dev->gadget.speed != USB_SPEED_FULL || !dev->driver)
		goto done;

	seq_printf(m, "ep0 IN %lu/%lu, OUT %lu/%lu\nirqs %lu\n\n",
		dev->stats.write.bytes, dev->stats.write.ops,
		dev->stats.read.bytes, dev->stats.read.ops,
		dev->stats.irqs);

	/* dump endpoint queues */
	for (i = 0; i < PXA_UDC_NUM_ENDPOINTS; i++) {
		struct pxa25x_ep	*ep = &dev->ep [i];
		struct pxa25x_request	*req;

		if (i != 0) {
			const struct usb_endpoint_descriptor	*desc;

			desc = ep->ep.desc;
			if (!desc)
				continue;
			tmp = *dev->ep [i].reg_udccs;
			seq_printf(m,
				"%s max %d %s udccs %02x irqs %lu\n",
				ep->ep.name, usb_endpoint_maxp(desc),
				"pio", tmp, ep->pio_irqs);
			/* TODO translate all five groups of udccs bits! */

		} else /* ep0 should only have one transfer queued */
			seq_printf(m, "ep0 max 16 pio irqs %lu\n",
				ep->pio_irqs);

		if (list_empty(&ep->queue)) {
			seq_printf(m, "\t(nothing queued)\n");
			continue;
		}
		list_for_each_entry(req, &ep->queue, queue) {
			seq_printf(m,
					"\treq %p len %d/%d buf %p\n",
					&req->req, req->req.actual,
					req->req.length, req->req.buf);
		}
	}

done:
	local_irq_restore(flags);
	return 0;
}

static int
udc_debugfs_open(struct inode *inode, struct file *file)
{
	return single_open(file, udc_seq_show, inode->i_private);
}

static const struct file_operations debug_fops = {
	.open		= udc_debugfs_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.owner		= THIS_MODULE,
};

#define create_debug_files(dev) \
	do { \
		dev->debugfs_udc = debugfs_create_file(dev->gadget.name, \
			S_IRUGO, NULL, dev, &debug_fops); \
	} while (0)
#define remove_debug_files(dev) \
	do { \
		if (dev->debugfs_udc) \
			debugfs_remove(dev->debugfs_udc); \
	} while (0)

#else	/* !CONFIG_USB_GADGET_DEBUG_FILES */

#define create_debug_files(dev) do {} while (0)
#define remove_debug_files(dev) do {} while (0)

#endif	/* CONFIG_USB_GADGET_DEBUG_FILES */

/*-------------------------------------------------------------------------*/

/*
 *	udc_disable - disable USB device controller
 */
static void udc_disable(struct pxa25x_udc *dev)
{
	/* block all irqs */
	udc_set_mask_UDCCR(UDCCR_SRM|UDCCR_REM);
	UICR0 = UICR1 = 0xff;
	UFNRH = UFNRH_SIM;

	/* if hardware supports it, disconnect from usb */
	pullup_off();

	udc_clear_mask_UDCCR(UDCCR_UDE);

	ep0_idle (dev);
	dev->gadget.speed = USB_SPEED_UNKNOWN;
}


/*
 *	udc_reinit - initialize software state
 */
static void udc_reinit(struct pxa25x_udc *dev)
{
	u32	i;

	/* device/ep0 records init */
	INIT_LIST_HEAD (&dev->gadget.ep_list);
	INIT_LIST_HEAD (&dev->gadget.ep0->ep_list);
	dev->ep0state = EP0_IDLE;

	/* basic endpoint records init */
	for (i = 0; i < PXA_UDC_NUM_ENDPOINTS; i++) {
		struct pxa25x_ep *ep = &dev->ep[i];

		if (i != 0)
			list_add_tail (&ep->ep.ep_list, &dev->gadget.ep_list);

		ep->ep.desc = NULL;
		ep->stopped = 0;
		INIT_LIST_HEAD (&ep->queue);
		ep->pio_irqs = 0;
		usb_ep_set_maxpacket_limit(&ep->ep, ep->ep.maxpacket);
	}

	/* the rest was statically initialized, and is read-only */
}

/* until it's enabled, this UDC should be completely invisible
 * to any USB host.
 */
static void udc_enable (struct pxa25x_udc *dev)
{
	udc_clear_mask_UDCCR(UDCCR_UDE);

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
static int pxa25x_udc_start(struct usb_gadget *g,
		struct usb_gadget_driver *driver)
{
	struct pxa25x_udc	*dev = to_pxa25x(g);
	int			retval;

	/* first hook up the driver ... */
	dev->driver = driver;
	dev->pullup = 1;

	/* ... then enable host detection and ep0; and we're ready
	 * for set_configuration as well as eventual disconnect.
	 */
	/* connect to bus through transceiver */
	if (!IS_ERR_OR_NULL(dev->transceiver)) {
		retval = otg_set_peripheral(dev->transceiver->otg,
						&dev->gadget);
		if (retval)
			goto bind_fail;
	}

	pullup(dev);
	dump_state(dev);
	return 0;
bind_fail:
	return retval;
}

static void
stop_activity(struct pxa25x_udc *dev, struct usb_gadget_driver *driver)
{
	int i;

	/* don't disconnect drivers more than once */
	if (dev->gadget.speed == USB_SPEED_UNKNOWN)
		driver = NULL;
	dev->gadget.speed = USB_SPEED_UNKNOWN;

	/* prevent new request submissions, kill any outstanding requests  */
	for (i = 0; i < PXA_UDC_NUM_ENDPOINTS; i++) {
		struct pxa25x_ep *ep = &dev->ep[i];

		ep->stopped = 1;
		nuke(ep, -ESHUTDOWN);
	}
	del_timer_sync(&dev->timer);

	/* report disconnect; the driver is already quiesced */
	if (driver)
		driver->disconnect(&dev->gadget);

	/* re-init driver-visible data structures */
	udc_reinit(dev);
}

static int pxa25x_udc_stop(struct usb_gadget*g,
		struct usb_gadget_driver *driver)
{
	struct pxa25x_udc	*dev = to_pxa25x(g);

	local_irq_disable();
	dev->pullup = 0;
	pullup(dev);
	stop_activity(dev, driver);
	local_irq_enable();

	if (!IS_ERR_OR_NULL(dev->transceiver))
		(void) otg_set_peripheral(dev->transceiver->otg, NULL);

	dev->driver = NULL;

	dump_state(dev);

	return 0;
}

/*-------------------------------------------------------------------------*/

#ifdef CONFIG_ARCH_LUBBOCK

/* Lubbock has separate connect and disconnect irqs.  More typical designs
 * use one GPIO as the VBUS IRQ, and another to control the D+ pullup.
 */

static irqreturn_t
lubbock_vbus_irq(int irq, void *_dev)
{
	struct pxa25x_udc	*dev = _dev;
	int			vbus;

	dev->stats.irqs++;
	switch (irq) {
	case LUBBOCK_USB_IRQ:
		vbus = 1;
		disable_irq(LUBBOCK_USB_IRQ);
		enable_irq(LUBBOCK_USB_DISC_IRQ);
		break;
	case LUBBOCK_USB_DISC_IRQ:
		vbus = 0;
		disable_irq(LUBBOCK_USB_DISC_IRQ);
		enable_irq(LUBBOCK_USB_IRQ);
		break;
	default:
		return IRQ_NONE;
	}

	pxa25x_udc_vbus_session(&dev->gadget, vbus);
	return IRQ_HANDLED;
}

#endif


/*-------------------------------------------------------------------------*/

static inline void clear_ep_state (struct pxa25x_udc *dev)
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
	struct pxa25x_udc	*dev = (void *)_dev;

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

static void handle_ep0 (struct pxa25x_udc *dev)
{
	u32			udccs0 = UDCCS0;
	struct pxa25x_ep	*ep = &dev->ep [0];
	struct pxa25x_request	*req;
	union {
		struct usb_ctrlrequest	r;
		u8			raw [8];
		u32			word [2];
	} u;

	if (list_empty(&ep->queue))
		req = NULL;
	else
		req = list_entry(ep->queue.next, struct pxa25x_request, queue);

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
					WARNING("config change %02x fail %d?\n",
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

static void handle_ep(struct pxa25x_ep *ep)
{
	struct pxa25x_request	*req;
	int			is_in = ep->bEndpointAddress & USB_DIR_IN;
	int			completed;
	u32			udccs, tmp;

	do {
		completed = 0;
		if (likely (!list_empty(&ep->queue)))
			req = list_entry(ep->queue.next,
					struct pxa25x_request, queue);
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
				completed = read_fifo(ep, req);
			} else
				pio_irq_disable (ep->bEndpointAddress);
		}
		ep->pio_irqs++;
	} while (completed);
}

/*
 *	pxa25x_udc_irq - interrupt handler
 *
 * avoid delays in ep0 processing. the control handshaking isn't always
 * under software control (pxa250c0 and the pxa255 are better), and delays
 * could cause usb protocol errors.
 */
static irqreturn_t
pxa25x_udc_irq(int irq, void *_dev)
{
	struct pxa25x_udc	*dev = _dev;
	int			handled;

	dev->stats.irqs++;
	do {
		u32		udccr = UDCCR;

		handled = 0;

		/* SUSpend Interrupt Request */
		if (unlikely(udccr & UDCCR_SUSIR)) {
			udc_ack_int_UDCCR(UDCCR_SUSIR);
			handled = 1;
			DBG(DBG_VERBOSE, "USB suspend\n");

			if (dev->gadget.speed != USB_SPEED_UNKNOWN
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
					&& dev->driver->resume)
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
#ifndef	CONFIG_USB_PXA25X_SMALL
				if (usir1 & tmp) {
					handle_ep(&dev->ep[i+8]);
					USIR1 |= tmp;
					handled = 1;
				}
#endif
			}
		}

		/* we could also ask for 1 msec SOF (SIR) interrupts */

	} while (handled);
	return IRQ_HANDLED;
}

/*-------------------------------------------------------------------------*/

static void nop_release (struct device *dev)
{
	DMSG("%s %s\n", __func__, dev_name(dev));
}

/* this uses load-time allocation and initialization (instead of
 * doing it at run-time) to save code, eliminate fault paths, and
 * be more obviously correct.
 */
static struct pxa25x_udc memory = {
	.gadget = {
		.ops		= &pxa25x_udc_ops,
		.ep0		= &memory.ep[0].ep,
		.name		= driver_name,
		.dev = {
			.init_name	= "gadget",
			.release	= nop_release,
		},
	},

	/* control endpoint */
	.ep[0] = {
		.ep = {
			.name		= ep0name,
			.ops		= &pxa25x_ep_ops,
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
			.ops		= &pxa25x_ep_ops,
			.maxpacket	= BULK_FIFO_SIZE,
		},
		.dev		= &memory,
		.fifo_size	= BULK_FIFO_SIZE,
		.bEndpointAddress = USB_DIR_IN | 1,
		.bmAttributes	= USB_ENDPOINT_XFER_BULK,
		.reg_udccs	= &UDCCS1,
		.reg_uddr	= &UDDR1,
	},
	.ep[2] = {
		.ep = {
			.name		= "ep2out-bulk",
			.ops		= &pxa25x_ep_ops,
			.maxpacket	= BULK_FIFO_SIZE,
		},
		.dev		= &memory,
		.fifo_size	= BULK_FIFO_SIZE,
		.bEndpointAddress = 2,
		.bmAttributes	= USB_ENDPOINT_XFER_BULK,
		.reg_udccs	= &UDCCS2,
		.reg_ubcr	= &UBCR2,
		.reg_uddr	= &UDDR2,
	},
#ifndef CONFIG_USB_PXA25X_SMALL
	.ep[3] = {
		.ep = {
			.name		= "ep3in-iso",
			.ops		= &pxa25x_ep_ops,
			.maxpacket	= ISO_FIFO_SIZE,
		},
		.dev		= &memory,
		.fifo_size	= ISO_FIFO_SIZE,
		.bEndpointAddress = USB_DIR_IN | 3,
		.bmAttributes	= USB_ENDPOINT_XFER_ISOC,
		.reg_udccs	= &UDCCS3,
		.reg_uddr	= &UDDR3,
	},
	.ep[4] = {
		.ep = {
			.name		= "ep4out-iso",
			.ops		= &pxa25x_ep_ops,
			.maxpacket	= ISO_FIFO_SIZE,
		},
		.dev		= &memory,
		.fifo_size	= ISO_FIFO_SIZE,
		.bEndpointAddress = 4,
		.bmAttributes	= USB_ENDPOINT_XFER_ISOC,
		.reg_udccs	= &UDCCS4,
		.reg_ubcr	= &UBCR4,
		.reg_uddr	= &UDDR4,
	},
	.ep[5] = {
		.ep = {
			.name		= "ep5in-int",
			.ops		= &pxa25x_ep_ops,
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
			.ops		= &pxa25x_ep_ops,
			.maxpacket	= BULK_FIFO_SIZE,
		},
		.dev		= &memory,
		.fifo_size	= BULK_FIFO_SIZE,
		.bEndpointAddress = USB_DIR_IN | 6,
		.bmAttributes	= USB_ENDPOINT_XFER_BULK,
		.reg_udccs	= &UDCCS6,
		.reg_uddr	= &UDDR6,
	},
	.ep[7] = {
		.ep = {
			.name		= "ep7out-bulk",
			.ops		= &pxa25x_ep_ops,
			.maxpacket	= BULK_FIFO_SIZE,
		},
		.dev		= &memory,
		.fifo_size	= BULK_FIFO_SIZE,
		.bEndpointAddress = 7,
		.bmAttributes	= USB_ENDPOINT_XFER_BULK,
		.reg_udccs	= &UDCCS7,
		.reg_ubcr	= &UBCR7,
		.reg_uddr	= &UDDR7,
	},
	.ep[8] = {
		.ep = {
			.name		= "ep8in-iso",
			.ops		= &pxa25x_ep_ops,
			.maxpacket	= ISO_FIFO_SIZE,
		},
		.dev		= &memory,
		.fifo_size	= ISO_FIFO_SIZE,
		.bEndpointAddress = USB_DIR_IN | 8,
		.bmAttributes	= USB_ENDPOINT_XFER_ISOC,
		.reg_udccs	= &UDCCS8,
		.reg_uddr	= &UDDR8,
	},
	.ep[9] = {
		.ep = {
			.name		= "ep9out-iso",
			.ops		= &pxa25x_ep_ops,
			.maxpacket	= ISO_FIFO_SIZE,
		},
		.dev		= &memory,
		.fifo_size	= ISO_FIFO_SIZE,
		.bEndpointAddress = 9,
		.bmAttributes	= USB_ENDPOINT_XFER_ISOC,
		.reg_udccs	= &UDCCS9,
		.reg_ubcr	= &UBCR9,
		.reg_uddr	= &UDDR9,
	},
	.ep[10] = {
		.ep = {
			.name		= "ep10in-int",
			.ops		= &pxa25x_ep_ops,
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
			.ops		= &pxa25x_ep_ops,
			.maxpacket	= BULK_FIFO_SIZE,
		},
		.dev		= &memory,
		.fifo_size	= BULK_FIFO_SIZE,
		.bEndpointAddress = USB_DIR_IN | 11,
		.bmAttributes	= USB_ENDPOINT_XFER_BULK,
		.reg_udccs	= &UDCCS11,
		.reg_uddr	= &UDDR11,
	},
	.ep[12] = {
		.ep = {
			.name		= "ep12out-bulk",
			.ops		= &pxa25x_ep_ops,
			.maxpacket	= BULK_FIFO_SIZE,
		},
		.dev		= &memory,
		.fifo_size	= BULK_FIFO_SIZE,
		.bEndpointAddress = 12,
		.bmAttributes	= USB_ENDPOINT_XFER_BULK,
		.reg_udccs	= &UDCCS12,
		.reg_ubcr	= &UBCR12,
		.reg_uddr	= &UDDR12,
	},
	.ep[13] = {
		.ep = {
			.name		= "ep13in-iso",
			.ops		= &pxa25x_ep_ops,
			.maxpacket	= ISO_FIFO_SIZE,
		},
		.dev		= &memory,
		.fifo_size	= ISO_FIFO_SIZE,
		.bEndpointAddress = USB_DIR_IN | 13,
		.bmAttributes	= USB_ENDPOINT_XFER_ISOC,
		.reg_udccs	= &UDCCS13,
		.reg_uddr	= &UDDR13,
	},
	.ep[14] = {
		.ep = {
			.name		= "ep14out-iso",
			.ops		= &pxa25x_ep_ops,
			.maxpacket	= ISO_FIFO_SIZE,
		},
		.dev		= &memory,
		.fifo_size	= ISO_FIFO_SIZE,
		.bEndpointAddress = 14,
		.bmAttributes	= USB_ENDPOINT_XFER_ISOC,
		.reg_udccs	= &UDCCS14,
		.reg_ubcr	= &UBCR14,
		.reg_uddr	= &UDDR14,
	},
	.ep[15] = {
		.ep = {
			.name		= "ep15in-int",
			.ops		= &pxa25x_ep_ops,
			.maxpacket	= INT_FIFO_SIZE,
		},
		.dev		= &memory,
		.fifo_size	= INT_FIFO_SIZE,
		.bEndpointAddress = USB_DIR_IN | 15,
		.bmAttributes	= USB_ENDPOINT_XFER_INT,
		.reg_udccs	= &UDCCS15,
		.reg_uddr	= &UDDR15,
	},
#endif /* !CONFIG_USB_PXA25X_SMALL */
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
#define IXP425_B0		0x000001f1
#define IXP465_AD		0x00000200

/*
 *	probe - binds to the platform device
 */
static int pxa25x_udc_probe(struct platform_device *pdev)
{
	struct pxa25x_udc *dev = &memory;
	int retval, irq;
	u32 chiprev;

	pr_info("%s: version %s\n", driver_name, DRIVER_VERSION);

	/* insist on Intel/ARM/XScale */
	asm("mrc%? p15, 0, %0, c0, c0" : "=r" (chiprev));
	if ((chiprev & CP15R0_VENDOR_MASK) != CP15R0_XSCALE_VALUE) {
		pr_err("%s: not XScale!\n", driver_name);
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
		/* OUT-DMA is broken ... */
		/* fall through */
	case PXA250_C0: case PXA210_C0:
		break;
#elif	defined(CONFIG_ARCH_IXP4XX)
	case IXP425_A0:
	case IXP425_B0:
	case IXP465_AD:
		dev->has_cfr = 1;
		break;
#endif
	default:
		pr_err("%s: unrecognized processor: %08x\n",
			driver_name, chiprev);
		/* iop3xx, ixp4xx, ... */
		return -ENODEV;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return -ENODEV;

	dev->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(dev->clk)) {
		retval = PTR_ERR(dev->clk);
		goto err_clk;
	}

	pr_debug("%s: IRQ %d%s%s\n", driver_name, irq,
		dev->has_cfr ? "" : " (!cfr)",
		SIZE_STR "(pio)"
		);

	/* other non-static parts of init */
	dev->dev = &pdev->dev;
	dev->mach = dev_get_platdata(&pdev->dev);

	dev->transceiver = usb_get_phy(USB_PHY_TYPE_USB2);

	if (gpio_is_valid(dev->mach->gpio_pullup)) {
		if ((retval = gpio_request(dev->mach->gpio_pullup,
				"pca25x_udc GPIO PULLUP"))) {
			dev_dbg(&pdev->dev,
				"can't get pullup gpio %d, err: %d\n",
				dev->mach->gpio_pullup, retval);
			goto err_gpio_pullup;
		}
		gpio_direction_output(dev->mach->gpio_pullup, 0);
	}

	init_timer(&dev->timer);
	dev->timer.function = udc_watchdog;
	dev->timer.data = (unsigned long) dev;

	the_controller = dev;
	platform_set_drvdata(pdev, dev);

	udc_disable(dev);
	udc_reinit(dev);

	dev->vbus = 0;

	/* irq setup after old hardware state is cleaned up */
	retval = request_irq(irq, pxa25x_udc_irq,
			0, driver_name, dev);
	if (retval != 0) {
		pr_err("%s: can't get irq %d, err %d\n",
			driver_name, irq, retval);
		goto err_irq1;
	}
	dev->got_irq = 1;

#ifdef CONFIG_ARCH_LUBBOCK
	if (machine_is_lubbock()) {
		retval = request_irq(LUBBOCK_USB_DISC_IRQ, lubbock_vbus_irq,
				     0, driver_name, dev);
		if (retval != 0) {
			pr_err("%s: can't get irq %i, err %d\n",
				driver_name, LUBBOCK_USB_DISC_IRQ, retval);
			goto err_irq_lub;
		}
		retval = request_irq(LUBBOCK_USB_IRQ, lubbock_vbus_irq,
				     0, driver_name, dev);
		if (retval != 0) {
			pr_err("%s: can't get irq %i, err %d\n",
				driver_name, LUBBOCK_USB_IRQ, retval);
			goto lubbock_fail0;
		}
	} else
#endif
	create_debug_files(dev);

	retval = usb_add_gadget_udc(&pdev->dev, &dev->gadget);
	if (!retval)
		return retval;

	remove_debug_files(dev);
#ifdef	CONFIG_ARCH_LUBBOCK
lubbock_fail0:
	free_irq(LUBBOCK_USB_DISC_IRQ, dev);
 err_irq_lub:
	free_irq(irq, dev);
#endif
 err_irq1:
	if (gpio_is_valid(dev->mach->gpio_pullup))
		gpio_free(dev->mach->gpio_pullup);
 err_gpio_pullup:
	if (!IS_ERR_OR_NULL(dev->transceiver)) {
		usb_put_phy(dev->transceiver);
		dev->transceiver = NULL;
	}
	clk_put(dev->clk);
 err_clk:
	return retval;
}

static void pxa25x_udc_shutdown(struct platform_device *_dev)
{
	pullup_off();
}

static int pxa25x_udc_remove(struct platform_device *pdev)
{
	struct pxa25x_udc *dev = platform_get_drvdata(pdev);

	if (dev->driver)
		return -EBUSY;

	usb_del_gadget_udc(&dev->gadget);
	dev->pullup = 0;
	pullup(dev);

	remove_debug_files(dev);

	if (dev->got_irq) {
		free_irq(platform_get_irq(pdev, 0), dev);
		dev->got_irq = 0;
	}
#ifdef CONFIG_ARCH_LUBBOCK
	if (machine_is_lubbock()) {
		free_irq(LUBBOCK_USB_DISC_IRQ, dev);
		free_irq(LUBBOCK_USB_IRQ, dev);
	}
#endif
	if (gpio_is_valid(dev->mach->gpio_pullup))
		gpio_free(dev->mach->gpio_pullup);

	clk_put(dev->clk);

	if (!IS_ERR_OR_NULL(dev->transceiver)) {
		usb_put_phy(dev->transceiver);
		dev->transceiver = NULL;
	}

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
static int pxa25x_udc_suspend(struct platform_device *dev, pm_message_t state)
{
	struct pxa25x_udc	*udc = platform_get_drvdata(dev);
	unsigned long flags;

	if (!gpio_is_valid(udc->mach->gpio_pullup) && !udc->mach->udc_command)
		WARNING("USB host won't detect disconnect!\n");
	udc->suspended = 1;

	local_irq_save(flags);
	pullup(udc);
	local_irq_restore(flags);

	return 0;
}

static int pxa25x_udc_resume(struct platform_device *dev)
{
	struct pxa25x_udc	*udc = platform_get_drvdata(dev);
	unsigned long flags;

	udc->suspended = 0;
	local_irq_save(flags);
	pullup(udc);
	local_irq_restore(flags);

	return 0;
}

#else
#define	pxa25x_udc_suspend	NULL
#define	pxa25x_udc_resume	NULL
#endif

/*-------------------------------------------------------------------------*/

static struct platform_driver udc_driver = {
	.shutdown	= pxa25x_udc_shutdown,
	.probe		= pxa25x_udc_probe,
	.remove		= pxa25x_udc_remove,
	.suspend	= pxa25x_udc_suspend,
	.resume		= pxa25x_udc_resume,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "pxa25x-udc",
	},
};

module_platform_driver(udc_driver);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR("Frank Becker, Robert Schwebel, David Brownell");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:pxa25x-udc");
