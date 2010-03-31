/*
 * at91_udc -- driver for at91-series USB peripheral controller
 *
 * Copyright (C) 2004 by Thomas Rathbone
 * Copyright (C) 2005 by HP Labs
 * Copyright (C) 2005 by David Brownell
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
 * along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA  02111-1307, USA.
 */

#undef	VERBOSE_DEBUG
#undef	PACKET_TRACE

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/proc_fs.h>
#include <linux/clk.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>

#include <asm/byteorder.h>
#include <mach/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/gpio.h>

#include <mach/board.h>
#include <mach/cpu.h>
#include <mach/at91sam9261_matrix.h>

#include "at91_udc.h"


/*
 * This controller is simple and PIO-only.  It's used in many AT91-series
 * full speed USB controllers, including the at91rm9200 (arm920T, with MMU),
 * at91sam926x (arm926ejs, with MMU), and several no-mmu versions.
 *
 * This driver expects the board has been wired with two GPIOs suppporting
 * a VBUS sensing IRQ, and a D+ pullup.  (They may be omitted, but the
 * testing hasn't covered such cases.)
 *
 * The pullup is most important (so it's integrated on sam926x parts).  It
 * provides software control over whether the host enumerates the device.
 *
 * The VBUS sensing helps during enumeration, and allows both USB clocks
 * (and the transceiver) to stay gated off until they're necessary, saving
 * power.  During USB suspend, the 48 MHz clock is gated off in hardware;
 * it may also be gated off by software during some Linux sleep states.
 */

#define	DRIVER_VERSION	"3 May 2006"

static const char driver_name [] = "at91_udc";
static const char ep0name[] = "ep0";


#define at91_udp_read(dev, reg) \
	__raw_readl((dev)->udp_baseaddr + (reg))
#define at91_udp_write(dev, reg, val) \
	__raw_writel((val), (dev)->udp_baseaddr + (reg))

/*-------------------------------------------------------------------------*/

#ifdef CONFIG_USB_GADGET_DEBUG_FILES

#include <linux/seq_file.h>

static const char debug_filename[] = "driver/udc";

#define FOURBITS "%s%s%s%s"
#define EIGHTBITS FOURBITS FOURBITS

static void proc_ep_show(struct seq_file *s, struct at91_ep *ep)
{
	static char		*types[] = {
		"control", "out-iso", "out-bulk", "out-int",
		"BOGUS",   "in-iso",  "in-bulk",  "in-int"};

	u32			csr;
	struct at91_request	*req;
	unsigned long	flags;

	local_irq_save(flags);

	csr = __raw_readl(ep->creg);

	/* NOTE:  not collecting per-endpoint irq statistics... */

	seq_printf(s, "\n");
	seq_printf(s, "%s, maxpacket %d %s%s %s%s\n",
			ep->ep.name, ep->ep.maxpacket,
			ep->is_in ? "in" : "out",
			ep->is_iso ? " iso" : "",
			ep->is_pingpong
				? (ep->fifo_bank ? "pong" : "ping")
				: "",
			ep->stopped ? " stopped" : "");
	seq_printf(s, "csr %08x rxbytes=%d %s %s %s" EIGHTBITS "\n",
		csr,
		(csr & 0x07ff0000) >> 16,
		(csr & (1 << 15)) ? "enabled" : "disabled",
		(csr & (1 << 11)) ? "DATA1" : "DATA0",
		types[(csr & 0x700) >> 8],

		/* iff type is control then print current direction */
		(!(csr & 0x700))
			? ((csr & (1 << 7)) ? " IN" : " OUT")
			: "",
		(csr & (1 << 6)) ? " rxdatabk1" : "",
		(csr & (1 << 5)) ? " forcestall" : "",
		(csr & (1 << 4)) ? " txpktrdy" : "",

		(csr & (1 << 3)) ? " stallsent" : "",
		(csr & (1 << 2)) ? " rxsetup" : "",
		(csr & (1 << 1)) ? " rxdatabk0" : "",
		(csr & (1 << 0)) ? " txcomp" : "");
	if (list_empty (&ep->queue))
		seq_printf(s, "\t(queue empty)\n");

	else list_for_each_entry (req, &ep->queue, queue) {
		unsigned	length = req->req.actual;

		seq_printf(s, "\treq %p len %d/%d buf %p\n",
				&req->req, length,
				req->req.length, req->req.buf);
	}
	local_irq_restore(flags);
}

static void proc_irq_show(struct seq_file *s, const char *label, u32 mask)
{
	int i;

	seq_printf(s, "%s %04x:%s%s" FOURBITS, label, mask,
		(mask & (1 << 13)) ? " wakeup" : "",
		(mask & (1 << 12)) ? " endbusres" : "",

		(mask & (1 << 11)) ? " sofint" : "",
		(mask & (1 << 10)) ? " extrsm" : "",
		(mask & (1 << 9)) ? " rxrsm" : "",
		(mask & (1 << 8)) ? " rxsusp" : "");
	for (i = 0; i < 8; i++) {
		if (mask & (1 << i))
			seq_printf(s, " ep%d", i);
	}
	seq_printf(s, "\n");
}

static int proc_udc_show(struct seq_file *s, void *unused)
{
	struct at91_udc	*udc = s->private;
	struct at91_ep	*ep;
	u32		tmp;

	seq_printf(s, "%s: version %s\n", driver_name, DRIVER_VERSION);

	seq_printf(s, "vbus %s, pullup %s, %s powered%s, gadget %s\n\n",
		udc->vbus ? "present" : "off",
		udc->enabled
			? (udc->vbus ? "active" : "enabled")
			: "disabled",
		udc->selfpowered ? "self" : "VBUS",
		udc->suspended ? ", suspended" : "",
		udc->driver ? udc->driver->driver.name : "(none)");

	/* don't access registers when interface isn't clocked */
	if (!udc->clocked) {
		seq_printf(s, "(not clocked)\n");
		return 0;
	}

	tmp = at91_udp_read(udc, AT91_UDP_FRM_NUM);
	seq_printf(s, "frame %05x:%s%s frame=%d\n", tmp,
		(tmp & AT91_UDP_FRM_OK) ? " ok" : "",
		(tmp & AT91_UDP_FRM_ERR) ? " err" : "",
		(tmp & AT91_UDP_NUM));

	tmp = at91_udp_read(udc, AT91_UDP_GLB_STAT);
	seq_printf(s, "glbstate %02x:%s" FOURBITS "\n", tmp,
		(tmp & AT91_UDP_RMWUPE) ? " rmwupe" : "",
		(tmp & AT91_UDP_RSMINPR) ? " rsminpr" : "",
		(tmp & AT91_UDP_ESR) ? " esr" : "",
		(tmp & AT91_UDP_CONFG) ? " confg" : "",
		(tmp & AT91_UDP_FADDEN) ? " fadden" : "");

	tmp = at91_udp_read(udc, AT91_UDP_FADDR);
	seq_printf(s, "faddr   %03x:%s fadd=%d\n", tmp,
		(tmp & AT91_UDP_FEN) ? " fen" : "",
		(tmp & AT91_UDP_FADD));

	proc_irq_show(s, "imr   ", at91_udp_read(udc, AT91_UDP_IMR));
	proc_irq_show(s, "isr   ", at91_udp_read(udc, AT91_UDP_ISR));

	if (udc->enabled && udc->vbus) {
		proc_ep_show(s, &udc->ep[0]);
		list_for_each_entry (ep, &udc->gadget.ep_list, ep.ep_list) {
			if (ep->desc)
				proc_ep_show(s, ep);
		}
	}
	return 0;
}

static int proc_udc_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_udc_show, PDE(inode)->data);
}

static const struct file_operations proc_ops = {
	.owner		= THIS_MODULE,
	.open		= proc_udc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static void create_debug_file(struct at91_udc *udc)
{
	udc->pde = proc_create_data(debug_filename, 0, NULL, &proc_ops, udc);
}

static void remove_debug_file(struct at91_udc *udc)
{
	if (udc->pde)
		remove_proc_entry(debug_filename, NULL);
}

#else

static inline void create_debug_file(struct at91_udc *udc) {}
static inline void remove_debug_file(struct at91_udc *udc) {}

#endif


/*-------------------------------------------------------------------------*/

static void done(struct at91_ep *ep, struct at91_request *req, int status)
{
	unsigned	stopped = ep->stopped;
	struct at91_udc	*udc = ep->udc;

	list_del_init(&req->queue);
	if (req->req.status == -EINPROGRESS)
		req->req.status = status;
	else
		status = req->req.status;
	if (status && status != -ESHUTDOWN)
		VDBG("%s done %p, status %d\n", ep->ep.name, req, status);

	ep->stopped = 1;
	req->req.complete(&ep->ep, &req->req);
	ep->stopped = stopped;

	/* ep0 is always ready; other endpoints need a non-empty queue */
	if (list_empty(&ep->queue) && ep->int_mask != (1 << 0))
		at91_udp_write(udc, AT91_UDP_IDR, ep->int_mask);
}

/*-------------------------------------------------------------------------*/

/* bits indicating OUT fifo has data ready */
#define	RX_DATA_READY	(AT91_UDP_RX_DATA_BK0 | AT91_UDP_RX_DATA_BK1)

/*
 * Endpoint FIFO CSR bits have a mix of bits, making it unsafe to just write
 * back most of the value you just read (because of side effects, including
 * bits that may change after reading and before writing).
 *
 * Except when changing a specific bit, always write values which:
 *  - clear SET_FX bits (setting them could change something)
 *  - set CLR_FX bits (clearing them could change something)
 *
 * There are also state bits like FORCESTALL, EPEDS, DIR, and EPTYPE
 * that shouldn't normally be changed.
 *
 * NOTE at91sam9260 docs mention synch between UDPCK and MCK clock domains,
 * implying a need to wait for one write to complete (test relevant bits)
 * before starting the next write.  This shouldn't be an issue given how
 * infrequently we write, except maybe for write-then-read idioms.
 */
#define	SET_FX	(AT91_UDP_TXPKTRDY)
#define	CLR_FX	(RX_DATA_READY | AT91_UDP_RXSETUP \
		| AT91_UDP_STALLSENT | AT91_UDP_TXCOMP)

/* pull OUT packet data from the endpoint's fifo */
static int read_fifo (struct at91_ep *ep, struct at91_request *req)
{
	u32 __iomem	*creg = ep->creg;
	u8 __iomem	*dreg = ep->creg + (AT91_UDP_FDR(0) - AT91_UDP_CSR(0));
	u32		csr;
	u8		*buf;
	unsigned int	count, bufferspace, is_done;

	buf = req->req.buf + req->req.actual;
	bufferspace = req->req.length - req->req.actual;

	/*
	 * there might be nothing to read if ep_queue() calls us,
	 * or if we already emptied both pingpong buffers
	 */
rescan:
	csr = __raw_readl(creg);
	if ((csr & RX_DATA_READY) == 0)
		return 0;

	count = (csr & AT91_UDP_RXBYTECNT) >> 16;
	if (count > ep->ep.maxpacket)
		count = ep->ep.maxpacket;
	if (count > bufferspace) {
		DBG("%s buffer overflow\n", ep->ep.name);
		req->req.status = -EOVERFLOW;
		count = bufferspace;
	}
	__raw_readsb(dreg, buf, count);

	/* release and swap pingpong mem bank */
	csr |= CLR_FX;
	if (ep->is_pingpong) {
		if (ep->fifo_bank == 0) {
			csr &= ~(SET_FX | AT91_UDP_RX_DATA_BK0);
			ep->fifo_bank = 1;
		} else {
			csr &= ~(SET_FX | AT91_UDP_RX_DATA_BK1);
			ep->fifo_bank = 0;
		}
	} else
		csr &= ~(SET_FX | AT91_UDP_RX_DATA_BK0);
	__raw_writel(csr, creg);

	req->req.actual += count;
	is_done = (count < ep->ep.maxpacket);
	if (count == bufferspace)
		is_done = 1;

	PACKET("%s %p out/%d%s\n", ep->ep.name, &req->req, count,
			is_done ? " (done)" : "");

	/*
	 * avoid extra trips through IRQ logic for packets already in
	 * the fifo ... maybe preventing an extra (expensive) OUT-NAK
	 */
	if (is_done)
		done(ep, req, 0);
	else if (ep->is_pingpong) {
		bufferspace -= count;
		buf += count;
		goto rescan;
	}

	return is_done;
}

/* load fifo for an IN packet */
static int write_fifo(struct at91_ep *ep, struct at91_request *req)
{
	u32 __iomem	*creg = ep->creg;
	u32		csr = __raw_readl(creg);
	u8 __iomem	*dreg = ep->creg + (AT91_UDP_FDR(0) - AT91_UDP_CSR(0));
	unsigned	total, count, is_last;
	u8		*buf;

	/*
	 * TODO: allow for writing two packets to the fifo ... that'll
	 * reduce the amount of IN-NAKing, but probably won't affect
	 * throughput much.  (Unlike preventing OUT-NAKing!)
	 */

	/*
	 * If ep_queue() calls us, the queue is empty and possibly in
	 * odd states like TXCOMP not yet cleared (we do it, saving at
	 * least one IRQ) or the fifo not yet being free.  Those aren't
	 * issues normally (IRQ handler fast path).
	 */
	if (unlikely(csr & (AT91_UDP_TXCOMP | AT91_UDP_TXPKTRDY))) {
		if (csr & AT91_UDP_TXCOMP) {
			csr |= CLR_FX;
			csr &= ~(SET_FX | AT91_UDP_TXCOMP);
			__raw_writel(csr, creg);
			csr = __raw_readl(creg);
		}
		if (csr & AT91_UDP_TXPKTRDY)
			return 0;
	}

	buf = req->req.buf + req->req.actual;
	prefetch(buf);
	total = req->req.length - req->req.actual;
	if (ep->ep.maxpacket < total) {
		count = ep->ep.maxpacket;
		is_last = 0;
	} else {
		count = total;
		is_last = (count < ep->ep.maxpacket) || !req->req.zero;
	}

	/*
	 * Write the packet, maybe it's a ZLP.
	 *
	 * NOTE:  incrementing req->actual before we receive the ACK means
	 * gadget driver IN bytecounts can be wrong in fault cases.  That's
	 * fixable with PIO drivers like this one (save "count" here, and
	 * do the increment later on TX irq), but not for most DMA hardware.
	 *
	 * So all gadget drivers must accept that potential error.  Some
	 * hardware supports precise fifo status reporting, letting them
	 * recover when the actual bytecount matters (e.g. for USB Test
	 * and Measurement Class devices).
	 */
	__raw_writesb(dreg, buf, count);
	csr &= ~SET_FX;
	csr |= CLR_FX | AT91_UDP_TXPKTRDY;
	__raw_writel(csr, creg);
	req->req.actual += count;

	PACKET("%s %p in/%d%s\n", ep->ep.name, &req->req, count,
			is_last ? " (done)" : "");
	if (is_last)
		done(ep, req, 0);
	return is_last;
}

static void nuke(struct at91_ep *ep, int status)
{
	struct at91_request *req;

	// terminer chaque requete dans la queue
	ep->stopped = 1;
	if (list_empty(&ep->queue))
		return;

	VDBG("%s %s\n", __func__, ep->ep.name);
	while (!list_empty(&ep->queue)) {
		req = list_entry(ep->queue.next, struct at91_request, queue);
		done(ep, req, status);
	}
}

/*-------------------------------------------------------------------------*/

static int at91_ep_enable(struct usb_ep *_ep,
				const struct usb_endpoint_descriptor *desc)
{
	struct at91_ep	*ep = container_of(_ep, struct at91_ep, ep);
	struct at91_udc	*dev = ep->udc;
	u16		maxpacket;
	u32		tmp;
	unsigned long	flags;

	if (!_ep || !ep
			|| !desc || ep->desc
			|| _ep->name == ep0name
			|| desc->bDescriptorType != USB_DT_ENDPOINT
			|| (maxpacket = le16_to_cpu(desc->wMaxPacketSize)) == 0
			|| maxpacket > ep->maxpacket) {
		DBG("bad ep or descriptor\n");
		return -EINVAL;
	}

	if (!dev->driver || dev->gadget.speed == USB_SPEED_UNKNOWN) {
		DBG("bogus device state\n");
		return -ESHUTDOWN;
	}

	tmp = usb_endpoint_type(desc);
	switch (tmp) {
	case USB_ENDPOINT_XFER_CONTROL:
		DBG("only one control endpoint\n");
		return -EINVAL;
	case USB_ENDPOINT_XFER_INT:
		if (maxpacket > 64)
			goto bogus_max;
		break;
	case USB_ENDPOINT_XFER_BULK:
		switch (maxpacket) {
		case 8:
		case 16:
		case 32:
		case 64:
			goto ok;
		}
bogus_max:
		DBG("bogus maxpacket %d\n", maxpacket);
		return -EINVAL;
	case USB_ENDPOINT_XFER_ISOC:
		if (!ep->is_pingpong) {
			DBG("iso requires double buffering\n");
			return -EINVAL;
		}
		break;
	}

ok:
	local_irq_save(flags);

	/* initialize endpoint to match this descriptor */
	ep->is_in = usb_endpoint_dir_in(desc);
	ep->is_iso = (tmp == USB_ENDPOINT_XFER_ISOC);
	ep->stopped = 0;
	if (ep->is_in)
		tmp |= 0x04;
	tmp <<= 8;
	tmp |= AT91_UDP_EPEDS;
	__raw_writel(tmp, ep->creg);

	ep->desc = desc;
	ep->ep.maxpacket = maxpacket;

	/*
	 * reset/init endpoint fifo.  NOTE:  leaves fifo_bank alone,
	 * since endpoint resets don't reset hw pingpong state.
	 */
	at91_udp_write(dev, AT91_UDP_RST_EP, ep->int_mask);
	at91_udp_write(dev, AT91_UDP_RST_EP, 0);

	local_irq_restore(flags);
	return 0;
}

static int at91_ep_disable (struct usb_ep * _ep)
{
	struct at91_ep	*ep = container_of(_ep, struct at91_ep, ep);
	struct at91_udc	*udc = ep->udc;
	unsigned long	flags;

	if (ep == &ep->udc->ep[0])
		return -EINVAL;

	local_irq_save(flags);

	nuke(ep, -ESHUTDOWN);

	/* restore the endpoint's pristine config */
	ep->desc = NULL;
	ep->ep.maxpacket = ep->maxpacket;

	/* reset fifos and endpoint */
	if (ep->udc->clocked) {
		at91_udp_write(udc, AT91_UDP_RST_EP, ep->int_mask);
		at91_udp_write(udc, AT91_UDP_RST_EP, 0);
		__raw_writel(0, ep->creg);
	}

	local_irq_restore(flags);
	return 0;
}

/*
 * this is a PIO-only driver, so there's nothing
 * interesting for request or buffer allocation.
 */

static struct usb_request *
at91_ep_alloc_request(struct usb_ep *_ep, gfp_t gfp_flags)
{
	struct at91_request *req;

	req = kzalloc(sizeof (struct at91_request), gfp_flags);
	if (!req)
		return NULL;

	INIT_LIST_HEAD(&req->queue);
	return &req->req;
}

static void at91_ep_free_request(struct usb_ep *_ep, struct usb_request *_req)
{
	struct at91_request *req;

	req = container_of(_req, struct at91_request, req);
	BUG_ON(!list_empty(&req->queue));
	kfree(req);
}

static int at91_ep_queue(struct usb_ep *_ep,
			struct usb_request *_req, gfp_t gfp_flags)
{
	struct at91_request	*req;
	struct at91_ep		*ep;
	struct at91_udc		*dev;
	int			status;
	unsigned long		flags;

	req = container_of(_req, struct at91_request, req);
	ep = container_of(_ep, struct at91_ep, ep);

	if (!_req || !_req->complete
			|| !_req->buf || !list_empty(&req->queue)) {
		DBG("invalid request\n");
		return -EINVAL;
	}

	if (!_ep || (!ep->desc && ep->ep.name != ep0name)) {
		DBG("invalid ep\n");
		return -EINVAL;
	}

	dev = ep->udc;

	if (!dev || !dev->driver || dev->gadget.speed == USB_SPEED_UNKNOWN) {
		DBG("invalid device\n");
		return -EINVAL;
	}

	_req->status = -EINPROGRESS;
	_req->actual = 0;

	local_irq_save(flags);

	/* try to kickstart any empty and idle queue */
	if (list_empty(&ep->queue) && !ep->stopped) {
		int	is_ep0;

		/*
		 * If this control request has a non-empty DATA stage, this
		 * will start that stage.  It works just like a non-control
		 * request (until the status stage starts, maybe early).
		 *
		 * If the data stage is empty, then this starts a successful
		 * IN/STATUS stage.  (Unsuccessful ones use set_halt.)
		 */
		is_ep0 = (ep->ep.name == ep0name);
		if (is_ep0) {
			u32	tmp;

			if (!dev->req_pending) {
				status = -EINVAL;
				goto done;
			}

			/*
			 * defer changing CONFG until after the gadget driver
			 * reconfigures the endpoints.
			 */
			if (dev->wait_for_config_ack) {
				tmp = at91_udp_read(dev, AT91_UDP_GLB_STAT);
				tmp ^= AT91_UDP_CONFG;
				VDBG("toggle config\n");
				at91_udp_write(dev, AT91_UDP_GLB_STAT, tmp);
			}
			if (req->req.length == 0) {
ep0_in_status:
				PACKET("ep0 in/status\n");
				status = 0;
				tmp = __raw_readl(ep->creg);
				tmp &= ~SET_FX;
				tmp |= CLR_FX | AT91_UDP_TXPKTRDY;
				__raw_writel(tmp, ep->creg);
				dev->req_pending = 0;
				goto done;
			}
		}

		if (ep->is_in)
			status = write_fifo(ep, req);
		else {
			status = read_fifo(ep, req);

			/* IN/STATUS stage is otherwise triggered by irq */
			if (status && is_ep0)
				goto ep0_in_status;
		}
	} else
		status = 0;

	if (req && !status) {
		list_add_tail (&req->queue, &ep->queue);
		at91_udp_write(dev, AT91_UDP_IER, ep->int_mask);
	}
done:
	local_irq_restore(flags);
	return (status < 0) ? status : 0;
}

static int at91_ep_dequeue(struct usb_ep *_ep, struct usb_request *_req)
{
	struct at91_ep	*ep;
	struct at91_request	*req;

	ep = container_of(_ep, struct at91_ep, ep);
	if (!_ep || ep->ep.name == ep0name)
		return -EINVAL;

	/* make sure it's actually queued on this endpoint */
	list_for_each_entry (req, &ep->queue, queue) {
		if (&req->req == _req)
			break;
	}
	if (&req->req != _req)
		return -EINVAL;

	done(ep, req, -ECONNRESET);
	return 0;
}

static int at91_ep_set_halt(struct usb_ep *_ep, int value)
{
	struct at91_ep	*ep = container_of(_ep, struct at91_ep, ep);
	struct at91_udc	*udc = ep->udc;
	u32 __iomem	*creg;
	u32		csr;
	unsigned long	flags;
	int		status = 0;

	if (!_ep || ep->is_iso || !ep->udc->clocked)
		return -EINVAL;

	creg = ep->creg;
	local_irq_save(flags);

	csr = __raw_readl(creg);

	/*
	 * fail with still-busy IN endpoints, ensuring correct sequencing
	 * of data tx then stall.  note that the fifo rx bytecount isn't
	 * completely accurate as a tx bytecount.
	 */
	if (ep->is_in && (!list_empty(&ep->queue) || (csr >> 16) != 0))
		status = -EAGAIN;
	else {
		csr |= CLR_FX;
		csr &= ~SET_FX;
		if (value) {
			csr |= AT91_UDP_FORCESTALL;
			VDBG("halt %s\n", ep->ep.name);
		} else {
			at91_udp_write(udc, AT91_UDP_RST_EP, ep->int_mask);
			at91_udp_write(udc, AT91_UDP_RST_EP, 0);
			csr &= ~AT91_UDP_FORCESTALL;
		}
		__raw_writel(csr, creg);
	}

	local_irq_restore(flags);
	return status;
}

static const struct usb_ep_ops at91_ep_ops = {
	.enable		= at91_ep_enable,
	.disable	= at91_ep_disable,
	.alloc_request	= at91_ep_alloc_request,
	.free_request	= at91_ep_free_request,
	.queue		= at91_ep_queue,
	.dequeue	= at91_ep_dequeue,
	.set_halt	= at91_ep_set_halt,
	// there's only imprecise fifo status reporting
};

/*-------------------------------------------------------------------------*/

static int at91_get_frame(struct usb_gadget *gadget)
{
	struct at91_udc *udc = to_udc(gadget);

	if (!to_udc(gadget)->clocked)
		return -EINVAL;
	return at91_udp_read(udc, AT91_UDP_FRM_NUM) & AT91_UDP_NUM;
}

static int at91_wakeup(struct usb_gadget *gadget)
{
	struct at91_udc	*udc = to_udc(gadget);
	u32		glbstate;
	int		status = -EINVAL;
	unsigned long	flags;

	DBG("%s\n", __func__ );
	local_irq_save(flags);

	if (!udc->clocked || !udc->suspended)
		goto done;

	/* NOTE:  some "early versions" handle ESR differently ... */

	glbstate = at91_udp_read(udc, AT91_UDP_GLB_STAT);
	if (!(glbstate & AT91_UDP_ESR))
		goto done;
	glbstate |= AT91_UDP_ESR;
	at91_udp_write(udc, AT91_UDP_GLB_STAT, glbstate);

done:
	local_irq_restore(flags);
	return status;
}

/* reinit == restore inital software state */
static void udc_reinit(struct at91_udc *udc)
{
	u32 i;

	INIT_LIST_HEAD(&udc->gadget.ep_list);
	INIT_LIST_HEAD(&udc->gadget.ep0->ep_list);

	for (i = 0; i < NUM_ENDPOINTS; i++) {
		struct at91_ep *ep = &udc->ep[i];

		if (i != 0)
			list_add_tail(&ep->ep.ep_list, &udc->gadget.ep_list);
		ep->desc = NULL;
		ep->stopped = 0;
		ep->fifo_bank = 0;
		ep->ep.maxpacket = ep->maxpacket;
		ep->creg = (void __iomem *) udc->udp_baseaddr + AT91_UDP_CSR(i);
		// initialiser une queue par endpoint
		INIT_LIST_HEAD(&ep->queue);
	}
}

static void stop_activity(struct at91_udc *udc)
{
	struct usb_gadget_driver *driver = udc->driver;
	int i;

	if (udc->gadget.speed == USB_SPEED_UNKNOWN)
		driver = NULL;
	udc->gadget.speed = USB_SPEED_UNKNOWN;
	udc->suspended = 0;

	for (i = 0; i < NUM_ENDPOINTS; i++) {
		struct at91_ep *ep = &udc->ep[i];
		ep->stopped = 1;
		nuke(ep, -ESHUTDOWN);
	}
	if (driver)
		driver->disconnect(&udc->gadget);

	udc_reinit(udc);
}

static void clk_on(struct at91_udc *udc)
{
	if (udc->clocked)
		return;
	udc->clocked = 1;
	clk_enable(udc->iclk);
	clk_enable(udc->fclk);
}

static void clk_off(struct at91_udc *udc)
{
	if (!udc->clocked)
		return;
	udc->clocked = 0;
	udc->gadget.speed = USB_SPEED_UNKNOWN;
	clk_disable(udc->fclk);
	clk_disable(udc->iclk);
}

/*
 * activate/deactivate link with host; minimize power usage for
 * inactive links by cutting clocks and transceiver power.
 */
static void pullup(struct at91_udc *udc, int is_on)
{
	int	active = !udc->board.pullup_active_low;

	if (!udc->enabled || !udc->vbus)
		is_on = 0;
	DBG("%sactive\n", is_on ? "" : "in");

	if (is_on) {
		clk_on(udc);
		at91_udp_write(udc, AT91_UDP_ICR, AT91_UDP_RXRSM);
		at91_udp_write(udc, AT91_UDP_TXVC, 0);
		if (cpu_is_at91rm9200())
			gpio_set_value(udc->board.pullup_pin, active);
		else if (cpu_is_at91sam9260() || cpu_is_at91sam9263() || cpu_is_at91sam9g20()) {
			u32	txvc = at91_udp_read(udc, AT91_UDP_TXVC);

			txvc |= AT91_UDP_TXVC_PUON;
			at91_udp_write(udc, AT91_UDP_TXVC, txvc);
		} else if (cpu_is_at91sam9261() || cpu_is_at91sam9g10()) {
			u32	usbpucr;

			usbpucr = at91_sys_read(AT91_MATRIX_USBPUCR);
			usbpucr |= AT91_MATRIX_USBPUCR_PUON;
			at91_sys_write(AT91_MATRIX_USBPUCR, usbpucr);
		}
	} else {
		stop_activity(udc);
		at91_udp_write(udc, AT91_UDP_IDR, AT91_UDP_RXRSM);
		at91_udp_write(udc, AT91_UDP_TXVC, AT91_UDP_TXVC_TXVDIS);
		if (cpu_is_at91rm9200())
			gpio_set_value(udc->board.pullup_pin, !active);
		else if (cpu_is_at91sam9260() || cpu_is_at91sam9263() || cpu_is_at91sam9g20()) {
			u32	txvc = at91_udp_read(udc, AT91_UDP_TXVC);

			txvc &= ~AT91_UDP_TXVC_PUON;
			at91_udp_write(udc, AT91_UDP_TXVC, txvc);
		} else if (cpu_is_at91sam9261() || cpu_is_at91sam9g10()) {
			u32	usbpucr;

			usbpucr = at91_sys_read(AT91_MATRIX_USBPUCR);
			usbpucr &= ~AT91_MATRIX_USBPUCR_PUON;
			at91_sys_write(AT91_MATRIX_USBPUCR, usbpucr);
		}
		clk_off(udc);
	}
}

/* vbus is here!  turn everything on that's ready */
static int at91_vbus_session(struct usb_gadget *gadget, int is_active)
{
	struct at91_udc	*udc = to_udc(gadget);
	unsigned long	flags;

	// VDBG("vbus %s\n", is_active ? "on" : "off");
	local_irq_save(flags);
	udc->vbus = (is_active != 0);
	if (udc->driver)
		pullup(udc, is_active);
	else
		pullup(udc, 0);
	local_irq_restore(flags);
	return 0;
}

static int at91_pullup(struct usb_gadget *gadget, int is_on)
{
	struct at91_udc	*udc = to_udc(gadget);
	unsigned long	flags;

	local_irq_save(flags);
	udc->enabled = is_on = !!is_on;
	pullup(udc, is_on);
	local_irq_restore(flags);
	return 0;
}

static int at91_set_selfpowered(struct usb_gadget *gadget, int is_on)
{
	struct at91_udc	*udc = to_udc(gadget);
	unsigned long	flags;

	local_irq_save(flags);
	udc->selfpowered = (is_on != 0);
	local_irq_restore(flags);
	return 0;
}

static const struct usb_gadget_ops at91_udc_ops = {
	.get_frame		= at91_get_frame,
	.wakeup			= at91_wakeup,
	.set_selfpowered	= at91_set_selfpowered,
	.vbus_session		= at91_vbus_session,
	.pullup			= at91_pullup,

	/*
	 * VBUS-powered devices may also also want to support bigger
	 * power budgets after an appropriate SET_CONFIGURATION.
	 */
	// .vbus_power		= at91_vbus_power,
};

/*-------------------------------------------------------------------------*/

static int handle_ep(struct at91_ep *ep)
{
	struct at91_request	*req;
	u32 __iomem		*creg = ep->creg;
	u32			csr = __raw_readl(creg);

	if (!list_empty(&ep->queue))
		req = list_entry(ep->queue.next,
			struct at91_request, queue);
	else
		req = NULL;

	if (ep->is_in) {
		if (csr & (AT91_UDP_STALLSENT | AT91_UDP_TXCOMP)) {
			csr |= CLR_FX;
			csr &= ~(SET_FX | AT91_UDP_STALLSENT | AT91_UDP_TXCOMP);
			__raw_writel(csr, creg);
		}
		if (req)
			return write_fifo(ep, req);

	} else {
		if (csr & AT91_UDP_STALLSENT) {
			/* STALLSENT bit == ISOERR */
			if (ep->is_iso && req)
				req->req.status = -EILSEQ;
			csr |= CLR_FX;
			csr &= ~(SET_FX | AT91_UDP_STALLSENT);
			__raw_writel(csr, creg);
			csr = __raw_readl(creg);
		}
		if (req && (csr & RX_DATA_READY))
			return read_fifo(ep, req);
	}
	return 0;
}

union setup {
	u8			raw[8];
	struct usb_ctrlrequest	r;
};

static void handle_setup(struct at91_udc *udc, struct at91_ep *ep, u32 csr)
{
	u32 __iomem	*creg = ep->creg;
	u8 __iomem	*dreg = ep->creg + (AT91_UDP_FDR(0) - AT91_UDP_CSR(0));
	unsigned	rxcount, i = 0;
	u32		tmp;
	union setup	pkt;
	int		status = 0;

	/* read and ack SETUP; hard-fail for bogus packets */
	rxcount = (csr & AT91_UDP_RXBYTECNT) >> 16;
	if (likely(rxcount == 8)) {
		while (rxcount--)
			pkt.raw[i++] = __raw_readb(dreg);
		if (pkt.r.bRequestType & USB_DIR_IN) {
			csr |= AT91_UDP_DIR;
			ep->is_in = 1;
		} else {
			csr &= ~AT91_UDP_DIR;
			ep->is_in = 0;
		}
	} else {
		// REVISIT this happens sometimes under load; why??
		ERR("SETUP len %d, csr %08x\n", rxcount, csr);
		status = -EINVAL;
	}
	csr |= CLR_FX;
	csr &= ~(SET_FX | AT91_UDP_RXSETUP);
	__raw_writel(csr, creg);
	udc->wait_for_addr_ack = 0;
	udc->wait_for_config_ack = 0;
	ep->stopped = 0;
	if (unlikely(status != 0))
		goto stall;

#define w_index		le16_to_cpu(pkt.r.wIndex)
#define w_value		le16_to_cpu(pkt.r.wValue)
#define w_length	le16_to_cpu(pkt.r.wLength)

	VDBG("SETUP %02x.%02x v%04x i%04x l%04x\n",
			pkt.r.bRequestType, pkt.r.bRequest,
			w_value, w_index, w_length);

	/*
	 * A few standard requests get handled here, ones that touch
	 * hardware ... notably for device and endpoint features.
	 */
	udc->req_pending = 1;
	csr = __raw_readl(creg);
	csr |= CLR_FX;
	csr &= ~SET_FX;
	switch ((pkt.r.bRequestType << 8) | pkt.r.bRequest) {

	case ((USB_TYPE_STANDARD|USB_RECIP_DEVICE) << 8)
			| USB_REQ_SET_ADDRESS:
		__raw_writel(csr | AT91_UDP_TXPKTRDY, creg);
		udc->addr = w_value;
		udc->wait_for_addr_ack = 1;
		udc->req_pending = 0;
		/* FADDR is set later, when we ack host STATUS */
		return;

	case ((USB_TYPE_STANDARD|USB_RECIP_DEVICE) << 8)
			| USB_REQ_SET_CONFIGURATION:
		tmp = at91_udp_read(udc, AT91_UDP_GLB_STAT) & AT91_UDP_CONFG;
		if (pkt.r.wValue)
			udc->wait_for_config_ack = (tmp == 0);
		else
			udc->wait_for_config_ack = (tmp != 0);
		if (udc->wait_for_config_ack)
			VDBG("wait for config\n");
		/* CONFG is toggled later, if gadget driver succeeds */
		break;

	/*
	 * Hosts may set or clear remote wakeup status, and
	 * devices may report they're VBUS powered.
	 */
	case ((USB_DIR_IN|USB_TYPE_STANDARD|USB_RECIP_DEVICE) << 8)
			| USB_REQ_GET_STATUS:
		tmp = (udc->selfpowered << USB_DEVICE_SELF_POWERED);
		if (at91_udp_read(udc, AT91_UDP_GLB_STAT) & AT91_UDP_ESR)
			tmp |= (1 << USB_DEVICE_REMOTE_WAKEUP);
		PACKET("get device status\n");
		__raw_writeb(tmp, dreg);
		__raw_writeb(0, dreg);
		goto write_in;
		/* then STATUS starts later, automatically */
	case ((USB_TYPE_STANDARD|USB_RECIP_DEVICE) << 8)
			| USB_REQ_SET_FEATURE:
		if (w_value != USB_DEVICE_REMOTE_WAKEUP)
			goto stall;
		tmp = at91_udp_read(udc, AT91_UDP_GLB_STAT);
		tmp |= AT91_UDP_ESR;
		at91_udp_write(udc, AT91_UDP_GLB_STAT, tmp);
		goto succeed;
	case ((USB_TYPE_STANDARD|USB_RECIP_DEVICE) << 8)
			| USB_REQ_CLEAR_FEATURE:
		if (w_value != USB_DEVICE_REMOTE_WAKEUP)
			goto stall;
		tmp = at91_udp_read(udc, AT91_UDP_GLB_STAT);
		tmp &= ~AT91_UDP_ESR;
		at91_udp_write(udc, AT91_UDP_GLB_STAT, tmp);
		goto succeed;

	/*
	 * Interfaces have no feature settings; this is pretty useless.
	 * we won't even insist the interface exists...
	 */
	case ((USB_DIR_IN|USB_TYPE_STANDARD|USB_RECIP_INTERFACE) << 8)
			| USB_REQ_GET_STATUS:
		PACKET("get interface status\n");
		__raw_writeb(0, dreg);
		__raw_writeb(0, dreg);
		goto write_in;
		/* then STATUS starts later, automatically */
	case ((USB_TYPE_STANDARD|USB_RECIP_INTERFACE) << 8)
			| USB_REQ_SET_FEATURE:
	case ((USB_TYPE_STANDARD|USB_RECIP_INTERFACE) << 8)
			| USB_REQ_CLEAR_FEATURE:
		goto stall;

	/*
	 * Hosts may clear bulk/intr endpoint halt after the gadget
	 * driver sets it (not widely used); or set it (for testing)
	 */
	case ((USB_DIR_IN|USB_TYPE_STANDARD|USB_RECIP_ENDPOINT) << 8)
			| USB_REQ_GET_STATUS:
		tmp = w_index & USB_ENDPOINT_NUMBER_MASK;
		ep = &udc->ep[tmp];
		if (tmp >= NUM_ENDPOINTS || (tmp && !ep->desc))
			goto stall;

		if (tmp) {
			if ((w_index & USB_DIR_IN)) {
				if (!ep->is_in)
					goto stall;
			} else if (ep->is_in)
				goto stall;
		}
		PACKET("get %s status\n", ep->ep.name);
		if (__raw_readl(ep->creg) & AT91_UDP_FORCESTALL)
			tmp = (1 << USB_ENDPOINT_HALT);
		else
			tmp = 0;
		__raw_writeb(tmp, dreg);
		__raw_writeb(0, dreg);
		goto write_in;
		/* then STATUS starts later, automatically */
	case ((USB_TYPE_STANDARD|USB_RECIP_ENDPOINT) << 8)
			| USB_REQ_SET_FEATURE:
		tmp = w_index & USB_ENDPOINT_NUMBER_MASK;
		ep = &udc->ep[tmp];
		if (w_value != USB_ENDPOINT_HALT || tmp >= NUM_ENDPOINTS)
			goto stall;
		if (!ep->desc || ep->is_iso)
			goto stall;
		if ((w_index & USB_DIR_IN)) {
			if (!ep->is_in)
				goto stall;
		} else if (ep->is_in)
			goto stall;

		tmp = __raw_readl(ep->creg);
		tmp &= ~SET_FX;
		tmp |= CLR_FX | AT91_UDP_FORCESTALL;
		__raw_writel(tmp, ep->creg);
		goto succeed;
	case ((USB_TYPE_STANDARD|USB_RECIP_ENDPOINT) << 8)
			| USB_REQ_CLEAR_FEATURE:
		tmp = w_index & USB_ENDPOINT_NUMBER_MASK;
		ep = &udc->ep[tmp];
		if (w_value != USB_ENDPOINT_HALT || tmp >= NUM_ENDPOINTS)
			goto stall;
		if (tmp == 0)
			goto succeed;
		if (!ep->desc || ep->is_iso)
			goto stall;
		if ((w_index & USB_DIR_IN)) {
			if (!ep->is_in)
				goto stall;
		} else if (ep->is_in)
			goto stall;

		at91_udp_write(udc, AT91_UDP_RST_EP, ep->int_mask);
		at91_udp_write(udc, AT91_UDP_RST_EP, 0);
		tmp = __raw_readl(ep->creg);
		tmp |= CLR_FX;
		tmp &= ~(SET_FX | AT91_UDP_FORCESTALL);
		__raw_writel(tmp, ep->creg);
		if (!list_empty(&ep->queue))
			handle_ep(ep);
		goto succeed;
	}

#undef w_value
#undef w_index
#undef w_length

	/* pass request up to the gadget driver */
	if (udc->driver)
		status = udc->driver->setup(&udc->gadget, &pkt.r);
	else
		status = -ENODEV;
	if (status < 0) {
stall:
		VDBG("req %02x.%02x protocol STALL; stat %d\n",
				pkt.r.bRequestType, pkt.r.bRequest, status);
		csr |= AT91_UDP_FORCESTALL;
		__raw_writel(csr, creg);
		udc->req_pending = 0;
	}
	return;

succeed:
	/* immediate successful (IN) STATUS after zero length DATA */
	PACKET("ep0 in/status\n");
write_in:
	csr |= AT91_UDP_TXPKTRDY;
	__raw_writel(csr, creg);
	udc->req_pending = 0;
	return;
}

static void handle_ep0(struct at91_udc *udc)
{
	struct at91_ep		*ep0 = &udc->ep[0];
	u32 __iomem		*creg = ep0->creg;
	u32			csr = __raw_readl(creg);
	struct at91_request	*req;

	if (unlikely(csr & AT91_UDP_STALLSENT)) {
		nuke(ep0, -EPROTO);
		udc->req_pending = 0;
		csr |= CLR_FX;
		csr &= ~(SET_FX | AT91_UDP_STALLSENT | AT91_UDP_FORCESTALL);
		__raw_writel(csr, creg);
		VDBG("ep0 stalled\n");
		csr = __raw_readl(creg);
	}
	if (csr & AT91_UDP_RXSETUP) {
		nuke(ep0, 0);
		udc->req_pending = 0;
		handle_setup(udc, ep0, csr);
		return;
	}

	if (list_empty(&ep0->queue))
		req = NULL;
	else
		req = list_entry(ep0->queue.next, struct at91_request, queue);

	/* host ACKed an IN packet that we sent */
	if (csr & AT91_UDP_TXCOMP) {
		csr |= CLR_FX;
		csr &= ~(SET_FX | AT91_UDP_TXCOMP);

		/* write more IN DATA? */
		if (req && ep0->is_in) {
			if (handle_ep(ep0))
				udc->req_pending = 0;

		/*
		 * Ack after:
		 *  - last IN DATA packet (including GET_STATUS)
		 *  - IN/STATUS for OUT DATA
		 *  - IN/STATUS for any zero-length DATA stage
		 * except for the IN DATA case, the host should send
		 * an OUT status later, which we'll ack.
		 */
		} else {
			udc->req_pending = 0;
			__raw_writel(csr, creg);

			/*
			 * SET_ADDRESS takes effect only after the STATUS
			 * (to the original address) gets acked.
			 */
			if (udc->wait_for_addr_ack) {
				u32	tmp;

				at91_udp_write(udc, AT91_UDP_FADDR,
						AT91_UDP_FEN | udc->addr);
				tmp = at91_udp_read(udc, AT91_UDP_GLB_STAT);
				tmp &= ~AT91_UDP_FADDEN;
				if (udc->addr)
					tmp |= AT91_UDP_FADDEN;
				at91_udp_write(udc, AT91_UDP_GLB_STAT, tmp);

				udc->wait_for_addr_ack = 0;
				VDBG("address %d\n", udc->addr);
			}
		}
	}

	/* OUT packet arrived ... */
	else if (csr & AT91_UDP_RX_DATA_BK0) {
		csr |= CLR_FX;
		csr &= ~(SET_FX | AT91_UDP_RX_DATA_BK0);

		/* OUT DATA stage */
		if (!ep0->is_in) {
			if (req) {
				if (handle_ep(ep0)) {
					/* send IN/STATUS */
					PACKET("ep0 in/status\n");
					csr = __raw_readl(creg);
					csr &= ~SET_FX;
					csr |= CLR_FX | AT91_UDP_TXPKTRDY;
					__raw_writel(csr, creg);
					udc->req_pending = 0;
				}
			} else if (udc->req_pending) {
				/*
				 * AT91 hardware has a hard time with this
				 * "deferred response" mode for control-OUT
				 * transfers.  (For control-IN it's fine.)
				 *
				 * The normal solution leaves OUT data in the
				 * fifo until the gadget driver is ready.
				 * We couldn't do that here without disabling
				 * the IRQ that tells about SETUP packets,
				 * e.g. when the host gets impatient...
				 *
				 * Working around it by copying into a buffer
				 * would almost be a non-deferred response,
				 * except that it wouldn't permit reliable
				 * stalling of the request.  Instead, demand
				 * that gadget drivers not use this mode.
				 */
				DBG("no control-OUT deferred responses!\n");
				__raw_writel(csr | AT91_UDP_FORCESTALL, creg);
				udc->req_pending = 0;
			}

		/* STATUS stage for control-IN; ack.  */
		} else {
			PACKET("ep0 out/status ACK\n");
			__raw_writel(csr, creg);

			/* "early" status stage */
			if (req)
				done(ep0, req, 0);
		}
	}
}

static irqreturn_t at91_udc_irq (int irq, void *_udc)
{
	struct at91_udc		*udc = _udc;
	u32			rescans = 5;

	while (rescans--) {
		u32 status;

		status = at91_udp_read(udc, AT91_UDP_ISR)
			& at91_udp_read(udc, AT91_UDP_IMR);
		if (!status)
			break;

		/* USB reset irq:  not maskable */
		if (status & AT91_UDP_ENDBUSRES) {
			at91_udp_write(udc, AT91_UDP_IDR, ~MINIMUS_INTERRUPTUS);
			at91_udp_write(udc, AT91_UDP_IER, MINIMUS_INTERRUPTUS);
			/* Atmel code clears this irq twice */
			at91_udp_write(udc, AT91_UDP_ICR, AT91_UDP_ENDBUSRES);
			at91_udp_write(udc, AT91_UDP_ICR, AT91_UDP_ENDBUSRES);
			VDBG("end bus reset\n");
			udc->addr = 0;
			stop_activity(udc);

			/* enable ep0 */
			at91_udp_write(udc, AT91_UDP_CSR(0),
					AT91_UDP_EPEDS | AT91_UDP_EPTYPE_CTRL);
			udc->gadget.speed = USB_SPEED_FULL;
			udc->suspended = 0;
			at91_udp_write(udc, AT91_UDP_IER, AT91_UDP_EP(0));

			/*
			 * NOTE:  this driver keeps clocks off unless the
			 * USB host is present.  That saves power, but for
			 * boards that don't support VBUS detection, both
			 * clocks need to be active most of the time.
			 */

		/* host initiated suspend (3+ms bus idle) */
		} else if (status & AT91_UDP_RXSUSP) {
			at91_udp_write(udc, AT91_UDP_IDR, AT91_UDP_RXSUSP);
			at91_udp_write(udc, AT91_UDP_IER, AT91_UDP_RXRSM);
			at91_udp_write(udc, AT91_UDP_ICR, AT91_UDP_RXSUSP);
			// VDBG("bus suspend\n");
			if (udc->suspended)
				continue;
			udc->suspended = 1;

			/*
			 * NOTE:  when suspending a VBUS-powered device, the
			 * gadget driver should switch into slow clock mode
			 * and then into standby to avoid drawing more than
			 * 500uA power (2500uA for some high-power configs).
			 */
			if (udc->driver && udc->driver->suspend)
				udc->driver->suspend(&udc->gadget);

		/* host initiated resume */
		} else if (status & AT91_UDP_RXRSM) {
			at91_udp_write(udc, AT91_UDP_IDR, AT91_UDP_RXRSM);
			at91_udp_write(udc, AT91_UDP_IER, AT91_UDP_RXSUSP);
			at91_udp_write(udc, AT91_UDP_ICR, AT91_UDP_RXRSM);
			// VDBG("bus resume\n");
			if (!udc->suspended)
				continue;
			udc->suspended = 0;

			/*
			 * NOTE:  for a VBUS-powered device, the gadget driver
			 * would normally want to switch out of slow clock
			 * mode into normal mode.
			 */
			if (udc->driver && udc->driver->resume)
				udc->driver->resume(&udc->gadget);

		/* endpoint IRQs are cleared by handling them */
		} else {
			int		i;
			unsigned	mask = 1;
			struct at91_ep	*ep = &udc->ep[1];

			if (status & mask)
				handle_ep0(udc);
			for (i = 1; i < NUM_ENDPOINTS; i++) {
				mask <<= 1;
				if (status & mask)
					handle_ep(ep);
				ep++;
			}
		}
	}

	return IRQ_HANDLED;
}

/*-------------------------------------------------------------------------*/

static void nop_release(struct device *dev)
{
	/* nothing to free */
}

static struct at91_udc controller = {
	.gadget = {
		.ops	= &at91_udc_ops,
		.ep0	= &controller.ep[0].ep,
		.name	= driver_name,
		.dev	= {
			.init_name = "gadget",
			.release = nop_release,
		}
	},
	.ep[0] = {
		.ep = {
			.name	= ep0name,
			.ops	= &at91_ep_ops,
		},
		.udc		= &controller,
		.maxpacket	= 8,
		.int_mask	= 1 << 0,
	},
	.ep[1] = {
		.ep = {
			.name	= "ep1",
			.ops	= &at91_ep_ops,
		},
		.udc		= &controller,
		.is_pingpong	= 1,
		.maxpacket	= 64,
		.int_mask	= 1 << 1,
	},
	.ep[2] = {
		.ep = {
			.name	= "ep2",
			.ops	= &at91_ep_ops,
		},
		.udc		= &controller,
		.is_pingpong	= 1,
		.maxpacket	= 64,
		.int_mask	= 1 << 2,
	},
	.ep[3] = {
		.ep = {
			/* could actually do bulk too */
			.name	= "ep3-int",
			.ops	= &at91_ep_ops,
		},
		.udc		= &controller,
		.maxpacket	= 8,
		.int_mask	= 1 << 3,
	},
	.ep[4] = {
		.ep = {
			.name	= "ep4",
			.ops	= &at91_ep_ops,
		},
		.udc		= &controller,
		.is_pingpong	= 1,
		.maxpacket	= 256,
		.int_mask	= 1 << 4,
	},
	.ep[5] = {
		.ep = {
			.name	= "ep5",
			.ops	= &at91_ep_ops,
		},
		.udc		= &controller,
		.is_pingpong	= 1,
		.maxpacket	= 256,
		.int_mask	= 1 << 5,
	},
	/* ep6 and ep7 are also reserved (custom silicon might use them) */
};

static irqreturn_t at91_vbus_irq(int irq, void *_udc)
{
	struct at91_udc	*udc = _udc;
	unsigned	value;

	/* vbus needs at least brief debouncing */
	udelay(10);
	value = gpio_get_value(udc->board.vbus_pin);
	if (value != udc->vbus)
		at91_vbus_session(&udc->gadget, value);

	return IRQ_HANDLED;
}

int usb_gadget_register_driver (struct usb_gadget_driver *driver)
{
	struct at91_udc	*udc = &controller;
	int		retval;

	if (!driver
			|| driver->speed < USB_SPEED_FULL
			|| !driver->bind
			|| !driver->setup) {
		DBG("bad parameter.\n");
		return -EINVAL;
	}

	if (udc->driver) {
		DBG("UDC already has a gadget driver\n");
		return -EBUSY;
	}

	udc->driver = driver;
	udc->gadget.dev.driver = &driver->driver;
	dev_set_drvdata(&udc->gadget.dev, &driver->driver);
	udc->enabled = 1;
	udc->selfpowered = 1;

	retval = driver->bind(&udc->gadget);
	if (retval) {
		DBG("driver->bind() returned %d\n", retval);
		udc->driver = NULL;
		udc->gadget.dev.driver = NULL;
		dev_set_drvdata(&udc->gadget.dev, NULL);
		udc->enabled = 0;
		udc->selfpowered = 0;
		return retval;
	}

	local_irq_disable();
	pullup(udc, 1);
	local_irq_enable();

	DBG("bound to %s\n", driver->driver.name);
	return 0;
}
EXPORT_SYMBOL (usb_gadget_register_driver);

int usb_gadget_unregister_driver (struct usb_gadget_driver *driver)
{
	struct at91_udc *udc = &controller;

	if (!driver || driver != udc->driver || !driver->unbind)
		return -EINVAL;

	local_irq_disable();
	udc->enabled = 0;
	at91_udp_write(udc, AT91_UDP_IDR, ~0);
	pullup(udc, 0);
	local_irq_enable();

	driver->unbind(&udc->gadget);
	udc->gadget.dev.driver = NULL;
	dev_set_drvdata(&udc->gadget.dev, NULL);
	udc->driver = NULL;

	DBG("unbound from %s\n", driver->driver.name);
	return 0;
}
EXPORT_SYMBOL (usb_gadget_unregister_driver);

/*-------------------------------------------------------------------------*/

static void at91udc_shutdown(struct platform_device *dev)
{
	/* force disconnect on reboot */
	pullup(platform_get_drvdata(dev), 0);
}

static int __init at91udc_probe(struct platform_device *pdev)
{
	struct device	*dev = &pdev->dev;
	struct at91_udc	*udc;
	int		retval;
	struct resource	*res;

	if (!dev->platform_data) {
		/* small (so we copy it) but critical! */
		DBG("missing platform_data\n");
		return -ENODEV;
	}

	if (pdev->num_resources != 2) {
		DBG("invalid num_resources\n");
		return -ENODEV;
	}
	if ((pdev->resource[0].flags != IORESOURCE_MEM)
			|| (pdev->resource[1].flags != IORESOURCE_IRQ)) {
		DBG("invalid resource type\n");
		return -ENODEV;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENXIO;

	if (!request_mem_region(res->start, resource_size(res), driver_name)) {
		DBG("someone's using UDC memory\n");
		return -EBUSY;
	}

	/* init software state */
	udc = &controller;
	udc->gadget.dev.parent = dev;
	udc->board = *(struct at91_udc_data *) dev->platform_data;
	udc->pdev = pdev;
	udc->enabled = 0;

	/* rm9200 needs manual D+ pullup; off by default */
	if (cpu_is_at91rm9200()) {
		if (udc->board.pullup_pin <= 0) {
			DBG("no D+ pullup?\n");
			retval = -ENODEV;
			goto fail0;
		}
		retval = gpio_request(udc->board.pullup_pin, "udc_pullup");
		if (retval) {
			DBG("D+ pullup is busy\n");
			goto fail0;
		}
		gpio_direction_output(udc->board.pullup_pin,
				udc->board.pullup_active_low);
	}

	/* newer chips have more FIFO memory than rm9200 */
	if (cpu_is_at91sam9260()) {
		udc->ep[0].maxpacket = 64;
		udc->ep[3].maxpacket = 64;
		udc->ep[4].maxpacket = 512;
		udc->ep[5].maxpacket = 512;
	} else if (cpu_is_at91sam9261() || cpu_is_at91sam9g10()) {
		udc->ep[3].maxpacket = 64;
	} else if (cpu_is_at91sam9263()) {
		udc->ep[0].maxpacket = 64;
		udc->ep[3].maxpacket = 64;
	}

	udc->udp_baseaddr = ioremap(res->start, resource_size(res));
	if (!udc->udp_baseaddr) {
		retval = -ENOMEM;
		goto fail0a;
	}

	udc_reinit(udc);

	/* get interface and function clocks */
	udc->iclk = clk_get(dev, "udc_clk");
	udc->fclk = clk_get(dev, "udpck");
	if (IS_ERR(udc->iclk) || IS_ERR(udc->fclk)) {
		DBG("clocks missing\n");
		retval = -ENODEV;
		/* NOTE: we "know" here that refcounts on these are NOPs */
		goto fail0b;
	}

	retval = device_register(&udc->gadget.dev);
	if (retval < 0)
		goto fail0b;

	/* don't do anything until we have both gadget driver and VBUS */
	clk_enable(udc->iclk);
	at91_udp_write(udc, AT91_UDP_TXVC, AT91_UDP_TXVC_TXVDIS);
	at91_udp_write(udc, AT91_UDP_IDR, 0xffffffff);
	/* Clear all pending interrupts - UDP may be used by bootloader. */
	at91_udp_write(udc, AT91_UDP_ICR, 0xffffffff);
	clk_disable(udc->iclk);

	/* request UDC and maybe VBUS irqs */
	udc->udp_irq = platform_get_irq(pdev, 0);
	retval = request_irq(udc->udp_irq, at91_udc_irq,
			IRQF_DISABLED, driver_name, udc);
	if (retval < 0) {
		DBG("request irq %d failed\n", udc->udp_irq);
		goto fail1;
	}
	if (udc->board.vbus_pin > 0) {
		retval = gpio_request(udc->board.vbus_pin, "udc_vbus");
		if (retval < 0) {
			DBG("request vbus pin failed\n");
			goto fail2;
		}
		gpio_direction_input(udc->board.vbus_pin);

		/*
		 * Get the initial state of VBUS - we cannot expect
		 * a pending interrupt.
		 */
		udc->vbus = gpio_get_value(udc->board.vbus_pin);
		if (request_irq(udc->board.vbus_pin, at91_vbus_irq,
				IRQF_DISABLED, driver_name, udc)) {
			DBG("request vbus irq %d failed\n",
					udc->board.vbus_pin);
			retval = -EBUSY;
			goto fail3;
		}
	} else {
		DBG("no VBUS detection, assuming always-on\n");
		udc->vbus = 1;
	}
	dev_set_drvdata(dev, udc);
	device_init_wakeup(dev, 1);
	create_debug_file(udc);

	INFO("%s version %s\n", driver_name, DRIVER_VERSION);
	return 0;

fail3:
	if (udc->board.vbus_pin > 0)
		gpio_free(udc->board.vbus_pin);
fail2:
	free_irq(udc->udp_irq, udc);
fail1:
	device_unregister(&udc->gadget.dev);
fail0b:
	iounmap(udc->udp_baseaddr);
fail0a:
	if (cpu_is_at91rm9200())
		gpio_free(udc->board.pullup_pin);
fail0:
	release_mem_region(res->start, resource_size(res));
	DBG("%s probe failed, %d\n", driver_name, retval);
	return retval;
}

static int __exit at91udc_remove(struct platform_device *pdev)
{
	struct at91_udc *udc = platform_get_drvdata(pdev);
	struct resource *res;

	DBG("remove\n");

	if (udc->driver)
		return -EBUSY;

	pullup(udc, 0);

	device_init_wakeup(&pdev->dev, 0);
	remove_debug_file(udc);
	if (udc->board.vbus_pin > 0) {
		free_irq(udc->board.vbus_pin, udc);
		gpio_free(udc->board.vbus_pin);
	}
	free_irq(udc->udp_irq, udc);
	device_unregister(&udc->gadget.dev);

	iounmap(udc->udp_baseaddr);

	if (cpu_is_at91rm9200())
		gpio_free(udc->board.pullup_pin);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(res->start, resource_size(res));

	clk_put(udc->iclk);
	clk_put(udc->fclk);

	return 0;
}

#ifdef CONFIG_PM
static int at91udc_suspend(struct platform_device *pdev, pm_message_t mesg)
{
	struct at91_udc *udc = platform_get_drvdata(pdev);
	int		wake = udc->driver && device_may_wakeup(&pdev->dev);

	/* Unless we can act normally to the host (letting it wake us up
	 * whenever it has work for us) force disconnect.  Wakeup requires
	 * PLLB for USB events (signaling for reset, wakeup, or incoming
	 * tokens) and VBUS irqs (on systems which support them).
	 */
	if ((!udc->suspended && udc->addr)
			|| !wake
			|| at91_suspend_entering_slow_clock()) {
		pullup(udc, 0);
		wake = 0;
	} else
		enable_irq_wake(udc->udp_irq);

	udc->active_suspend = wake;
	if (udc->board.vbus_pin > 0 && wake)
		enable_irq_wake(udc->board.vbus_pin);
	return 0;
}

static int at91udc_resume(struct platform_device *pdev)
{
	struct at91_udc *udc = platform_get_drvdata(pdev);

	if (udc->board.vbus_pin > 0 && udc->active_suspend)
		disable_irq_wake(udc->board.vbus_pin);

	/* maybe reconnect to host; if so, clocks on */
	if (udc->active_suspend)
		disable_irq_wake(udc->udp_irq);
	else
		pullup(udc, 1);
	return 0;
}
#else
#define	at91udc_suspend	NULL
#define	at91udc_resume	NULL
#endif

static struct platform_driver at91_udc_driver = {
	.remove		= __exit_p(at91udc_remove),
	.shutdown	= at91udc_shutdown,
	.suspend	= at91udc_suspend,
	.resume		= at91udc_resume,
	.driver		= {
		.name	= (char *) driver_name,
		.owner	= THIS_MODULE,
	},
};

static int __init udc_init_module(void)
{
	return platform_driver_probe(&at91_udc_driver, at91udc_probe);
}
module_init(udc_init_module);

static void __exit udc_exit_module(void)
{
	platform_driver_unregister(&at91_udc_driver);
}
module_exit(udc_exit_module);

MODULE_DESCRIPTION("AT91 udc driver");
MODULE_AUTHOR("Thomas Rathbone, David Brownell");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:at91_udc");
