// SPDX-License-Identifier: GPL-2.0
/*
 * Toshiba TC86C001 ("Goku-S") USB Device Controller driver
 *
 * Copyright (C) 2000-2002 Lineo
 *      by Stuart Lynne, Tom Rushworth, and Bruce Balden
 * Copyright (C) 2002 Toshiba Corporation
 * Copyright (C) 2003 MontaVista Software (source@mvista.com)
 */

/*
 * This device has ep0 and three semi-configurable bulk/interrupt endpoints.
 *
 *  - Endpoint numbering is fixed: ep{1,2,3}-bulk
 *  - Gadget drivers can choose ep maxpacket (8/16/32/64)
 *  - Gadget drivers can choose direction (IN, OUT)
 *  - DMA works with ep1 (OUT transfers) and ep2 (IN transfers).
 */

// #define	VERBOSE		/* extra debug messages (success too) */
// #define	USB_TRACE	/* packet-level success messages */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/device.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/prefetch.h>

#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/unaligned.h>


#include "goku_udc.h"

#define	DRIVER_DESC		"TC86C001 USB Device Controller"
#define	DRIVER_VERSION		"30-Oct 2003"

static const char driver_name [] = "goku_udc";
static const char driver_desc [] = DRIVER_DESC;

MODULE_AUTHOR("source@mvista.com");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");


/*
 * IN dma behaves ok under testing, though the IN-dma abort paths don't
 * seem to behave quite as expected.  Used by default.
 *
 * OUT dma documents design problems handling the common "short packet"
 * transfer termination policy; it couldn't be enabled by default, even
 * if the OUT-dma abort problems had a resolution.
 */
static unsigned use_dma = 1;

#if 0
//#include <linux/moduleparam.h>
/* "modprobe goku_udc use_dma=1" etc
 *	0 to disable dma
 *	1 to use IN dma only (normal operation)
 *	2 to use IN and OUT dma
 */
module_param(use_dma, uint, S_IRUGO);
#endif

/*-------------------------------------------------------------------------*/

static void nuke(struct goku_ep *, int status);

static inline void
command(struct goku_udc_regs __iomem *regs, int command, unsigned epnum)
{
	writel(COMMAND_EP(epnum) | command, &regs->Command);
	udelay(300);
}

static int
goku_ep_enable(struct usb_ep *_ep, const struct usb_endpoint_descriptor *desc)
{
	struct goku_udc	*dev;
	struct goku_ep	*ep;
	u32		mode;
	u16		max;
	unsigned long	flags;

	ep = container_of(_ep, struct goku_ep, ep);
	if (!_ep || !desc
			|| desc->bDescriptorType != USB_DT_ENDPOINT)
		return -EINVAL;
	dev = ep->dev;
	if (ep == &dev->ep[0])
		return -EINVAL;
	if (!dev->driver || dev->gadget.speed == USB_SPEED_UNKNOWN)
		return -ESHUTDOWN;
	if (ep->num != usb_endpoint_num(desc))
		return -EINVAL;

	switch (usb_endpoint_type(desc)) {
	case USB_ENDPOINT_XFER_BULK:
	case USB_ENDPOINT_XFER_INT:
		break;
	default:
		return -EINVAL;
	}

	if ((readl(ep->reg_status) & EPxSTATUS_EP_MASK)
			!= EPxSTATUS_EP_INVALID)
		return -EBUSY;

	/* enabling the no-toggle interrupt mode would need an api hook */
	mode = 0;
	max = get_unaligned_le16(&desc->wMaxPacketSize);
	switch (max) {
	case 64:
		mode++; /* fall through */
	case 32:
		mode++; /* fall through */
	case 16:
		mode++; /* fall through */
	case 8:
		mode <<= 3;
		break;
	default:
		return -EINVAL;
	}
	mode |= 2 << 1;		/* bulk, or intr-with-toggle */

	/* ep1/ep2 dma direction is chosen early; it works in the other
	 * direction, with pio.  be cautious with out-dma.
	 */
	ep->is_in = usb_endpoint_dir_in(desc);
	if (ep->is_in) {
		mode |= 1;
		ep->dma = (use_dma != 0) && (ep->num == UDC_MSTRD_ENDPOINT);
	} else {
		ep->dma = (use_dma == 2) && (ep->num == UDC_MSTWR_ENDPOINT);
		if (ep->dma)
			DBG(dev, "%s out-dma hides short packets\n",
				ep->ep.name);
	}

	spin_lock_irqsave(&ep->dev->lock, flags);

	/* ep1 and ep2 can do double buffering and/or dma */
	if (ep->num < 3) {
		struct goku_udc_regs __iomem	*regs = ep->dev->regs;
		u32				tmp;

		/* double buffer except (for now) with pio in */
		tmp = ((ep->dma || !ep->is_in)
				? 0x10	/* double buffered */
				: 0x11	/* single buffer */
			) << ep->num;
		tmp |= readl(&regs->EPxSingle);
		writel(tmp, &regs->EPxSingle);

		tmp = (ep->dma ? 0x10/*dma*/ : 0x11/*pio*/) << ep->num;
		tmp |= readl(&regs->EPxBCS);
		writel(tmp, &regs->EPxBCS);
	}
	writel(mode, ep->reg_mode);
	command(ep->dev->regs, COMMAND_RESET, ep->num);
	ep->ep.maxpacket = max;
	ep->stopped = 0;
	ep->ep.desc = desc;
	spin_unlock_irqrestore(&ep->dev->lock, flags);

	DBG(dev, "enable %s %s %s maxpacket %u\n", ep->ep.name,
		ep->is_in ? "IN" : "OUT",
		ep->dma ? "dma" : "pio",
		max);

	return 0;
}

static void ep_reset(struct goku_udc_regs __iomem *regs, struct goku_ep *ep)
{
	struct goku_udc		*dev = ep->dev;

	if (regs) {
		command(regs, COMMAND_INVALID, ep->num);
		if (ep->num) {
			if (ep->num == UDC_MSTWR_ENDPOINT)
				dev->int_enable &= ~(INT_MSTWREND
							|INT_MSTWRTMOUT);
			else if (ep->num == UDC_MSTRD_ENDPOINT)
				dev->int_enable &= ~INT_MSTRDEND;
			dev->int_enable &= ~INT_EPxDATASET (ep->num);
		} else
			dev->int_enable &= ~INT_EP0;
		writel(dev->int_enable, &regs->int_enable);
		readl(&regs->int_enable);
		if (ep->num < 3) {
			struct goku_udc_regs __iomem	*r = ep->dev->regs;
			u32				tmp;

			tmp = readl(&r->EPxSingle);
			tmp &= ~(0x11 << ep->num);
			writel(tmp, &r->EPxSingle);

			tmp = readl(&r->EPxBCS);
			tmp &= ~(0x11 << ep->num);
			writel(tmp, &r->EPxBCS);
		}
		/* reset dma in case we're still using it */
		if (ep->dma) {
			u32	master;

			master = readl(&regs->dma_master) & MST_RW_BITS;
			if (ep->num == UDC_MSTWR_ENDPOINT) {
				master &= ~MST_W_BITS;
				master |= MST_WR_RESET;
			} else {
				master &= ~MST_R_BITS;
				master |= MST_RD_RESET;
			}
			writel(master, &regs->dma_master);
		}
	}

	usb_ep_set_maxpacket_limit(&ep->ep, MAX_FIFO_SIZE);
	ep->ep.desc = NULL;
	ep->stopped = 1;
	ep->irqs = 0;
	ep->dma = 0;
}

static int goku_ep_disable(struct usb_ep *_ep)
{
	struct goku_ep	*ep;
	struct goku_udc	*dev;
	unsigned long	flags;

	ep = container_of(_ep, struct goku_ep, ep);
	if (!_ep || !ep->ep.desc)
		return -ENODEV;
	dev = ep->dev;
	if (dev->ep0state == EP0_SUSPEND)
		return -EBUSY;

	VDBG(dev, "disable %s\n", _ep->name);

	spin_lock_irqsave(&dev->lock, flags);
	nuke(ep, -ESHUTDOWN);
	ep_reset(dev->regs, ep);
	spin_unlock_irqrestore(&dev->lock, flags);

	return 0;
}

/*-------------------------------------------------------------------------*/

static struct usb_request *
goku_alloc_request(struct usb_ep *_ep, gfp_t gfp_flags)
{
	struct goku_request	*req;

	if (!_ep)
		return NULL;
	req = kzalloc(sizeof *req, gfp_flags);
	if (!req)
		return NULL;

	INIT_LIST_HEAD(&req->queue);
	return &req->req;
}

static void
goku_free_request(struct usb_ep *_ep, struct usb_request *_req)
{
	struct goku_request	*req;

	if (!_ep || !_req)
		return;

	req = container_of(_req, struct goku_request, req);
	WARN_ON(!list_empty(&req->queue));
	kfree(req);
}

/*-------------------------------------------------------------------------*/

static void
done(struct goku_ep *ep, struct goku_request *req, int status)
{
	struct goku_udc		*dev;
	unsigned		stopped = ep->stopped;

	list_del_init(&req->queue);

	if (likely(req->req.status == -EINPROGRESS))
		req->req.status = status;
	else
		status = req->req.status;

	dev = ep->dev;

	if (ep->dma)
		usb_gadget_unmap_request(&dev->gadget, &req->req, ep->is_in);

#ifndef USB_TRACE
	if (status && status != -ESHUTDOWN)
#endif
		VDBG(dev, "complete %s req %p stat %d len %u/%u\n",
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

static inline int
write_packet(u32 __iomem *fifo, u8 *buf, struct goku_request *req, unsigned max)
{
	unsigned	length, count;

	length = min(req->req.length - req->req.actual, max);
	req->req.actual += length;

	count = length;
	while (likely(count--))
		writel(*buf++, fifo);
	return length;
}

// return:  0 = still running, 1 = completed, negative = errno
static int write_fifo(struct goku_ep *ep, struct goku_request *req)
{
	struct goku_udc	*dev = ep->dev;
	u32		tmp;
	u8		*buf;
	unsigned	count;
	int		is_last;

	tmp = readl(&dev->regs->DataSet);
	buf = req->req.buf + req->req.actual;
	prefetch(buf);

	dev = ep->dev;
	if (unlikely(ep->num == 0 && dev->ep0state != EP0_IN))
		return -EL2HLT;

	/* NOTE:  just single-buffered PIO-IN for now.  */
	if (unlikely((tmp & DATASET_A(ep->num)) != 0))
		return 0;

	/* clear our "packet available" irq */
	if (ep->num != 0)
		writel(~INT_EPxDATASET(ep->num), &dev->regs->int_status);

	count = write_packet(ep->reg_fifo, buf, req, ep->ep.maxpacket);

	/* last packet often short (sometimes a zlp, especially on ep0) */
	if (unlikely(count != ep->ep.maxpacket)) {
		writel(~(1<<ep->num), &dev->regs->EOP);
		if (ep->num == 0) {
			dev->ep[0].stopped = 1;
			dev->ep0state = EP0_STATUS;
		}
		is_last = 1;
	} else {
		if (likely(req->req.length != req->req.actual)
				|| req->req.zero)
			is_last = 0;
		else
			is_last = 1;
	}
#if 0		/* printk seemed to trash is_last...*/
//#ifdef USB_TRACE
	VDBG(dev, "wrote %s %u bytes%s IN %u left %p\n",
		ep->ep.name, count, is_last ? "/last" : "",
		req->req.length - req->req.actual, req);
#endif

	/* requests complete when all IN data is in the FIFO,
	 * or sometimes later, if a zlp was needed.
	 */
	if (is_last) {
		done(ep, req, 0);
		return 1;
	}

	return 0;
}

static int read_fifo(struct goku_ep *ep, struct goku_request *req)
{
	struct goku_udc_regs __iomem	*regs;
	u32				size, set;
	u8				*buf;
	unsigned			bufferspace, is_short, dbuff;

	regs = ep->dev->regs;
top:
	buf = req->req.buf + req->req.actual;
	prefetchw(buf);

	if (unlikely(ep->num == 0 && ep->dev->ep0state != EP0_OUT))
		return -EL2HLT;

	dbuff = (ep->num == 1 || ep->num == 2);
	do {
		/* ack dataset irq matching the status we'll handle */
		if (ep->num != 0)
			writel(~INT_EPxDATASET(ep->num), &regs->int_status);

		set = readl(&regs->DataSet) & DATASET_AB(ep->num);
		size = readl(&regs->EPxSizeLA[ep->num]);
		bufferspace = req->req.length - req->req.actual;

		/* usually do nothing without an OUT packet */
		if (likely(ep->num != 0 || bufferspace != 0)) {
			if (unlikely(set == 0))
				break;
			/* use ep1/ep2 double-buffering for OUT */
			if (!(size & PACKET_ACTIVE))
				size = readl(&regs->EPxSizeLB[ep->num]);
			if (!(size & PACKET_ACTIVE))	/* "can't happen" */
				break;
			size &= DATASIZE;	/* EPxSizeH == 0 */

		/* ep0out no-out-data case for set_config, etc */
		} else
			size = 0;

		/* read all bytes from this packet */
		req->req.actual += size;
		is_short = (size < ep->ep.maxpacket);
#ifdef USB_TRACE
		VDBG(ep->dev, "read %s %u bytes%s OUT req %p %u/%u\n",
			ep->ep.name, size, is_short ? "/S" : "",
			req, req->req.actual, req->req.length);
#endif
		while (likely(size-- != 0)) {
			u8	byte = (u8) readl(ep->reg_fifo);

			if (unlikely(bufferspace == 0)) {
				/* this happens when the driver's buffer
				 * is smaller than what the host sent.
				 * discard the extra data in this packet.
				 */
				if (req->req.status != -EOVERFLOW)
					DBG(ep->dev, "%s overflow %u\n",
						ep->ep.name, size);
				req->req.status = -EOVERFLOW;
			} else {
				*buf++ = byte;
				bufferspace--;
			}
		}

		/* completion */
		if (unlikely(is_short || req->req.actual == req->req.length)) {
			if (unlikely(ep->num == 0)) {
				/* non-control endpoints now usable? */
				if (ep->dev->req_config)
					writel(ep->dev->configured
							? USBSTATE_CONFIGURED
							: 0,
						&regs->UsbState);
				/* ep0out status stage */
				writel(~(1<<0), &regs->EOP);
				ep->stopped = 1;
				ep->dev->ep0state = EP0_STATUS;
			}
			done(ep, req, 0);

			/* empty the second buffer asap */
			if (dbuff && !list_empty(&ep->queue)) {
				req = list_entry(ep->queue.next,
						struct goku_request, queue);
				goto top;
			}
			return 1;
		}
	} while (dbuff);
	return 0;
}

static inline void
pio_irq_enable(struct goku_udc *dev,
		struct goku_udc_regs __iomem *regs, int epnum)
{
	dev->int_enable |= INT_EPxDATASET (epnum);
	writel(dev->int_enable, &regs->int_enable);
	/* write may still be posted */
}

static inline void
pio_irq_disable(struct goku_udc *dev,
		struct goku_udc_regs __iomem *regs, int epnum)
{
	dev->int_enable &= ~INT_EPxDATASET (epnum);
	writel(dev->int_enable, &regs->int_enable);
	/* write may still be posted */
}

static inline void
pio_advance(struct goku_ep *ep)
{
	struct goku_request	*req;

	if (unlikely(list_empty (&ep->queue)))
		return;
	req = list_entry(ep->queue.next, struct goku_request, queue);
	(ep->is_in ? write_fifo : read_fifo)(ep, req);
}


/*-------------------------------------------------------------------------*/

// return:  0 = q running, 1 = q stopped, negative = errno
static int start_dma(struct goku_ep *ep, struct goku_request *req)
{
	struct goku_udc_regs __iomem	*regs = ep->dev->regs;
	u32				master;
	u32				start = req->req.dma;
	u32				end = start + req->req.length - 1;

	master = readl(&regs->dma_master) & MST_RW_BITS;

	/* re-init the bits affecting IN dma; careful with zlps */
	if (likely(ep->is_in)) {
		if (unlikely(master & MST_RD_ENA)) {
			DBG (ep->dev, "start, IN active dma %03x!!\n",
				master);
//			return -EL2HLT;
		}
		writel(end, &regs->in_dma_end);
		writel(start, &regs->in_dma_start);

		master &= ~MST_R_BITS;
		if (unlikely(req->req.length == 0))
			master = MST_RD_ENA | MST_RD_EOPB;
		else if ((req->req.length % ep->ep.maxpacket) != 0
					|| req->req.zero)
			master = MST_RD_ENA | MST_EOPB_ENA;
		else
			master = MST_RD_ENA | MST_EOPB_DIS;

		ep->dev->int_enable |= INT_MSTRDEND;

	/* Goku DMA-OUT merges short packets, which plays poorly with
	 * protocols where short packets mark the transfer boundaries.
	 * The chip supports a nonstandard policy with INT_MSTWRTMOUT,
	 * ending transfers after 3 SOFs; we don't turn it on.
	 */
	} else {
		if (unlikely(master & MST_WR_ENA)) {
			DBG (ep->dev, "start, OUT active dma %03x!!\n",
				master);
//			return -EL2HLT;
		}
		writel(end, &regs->out_dma_end);
		writel(start, &regs->out_dma_start);

		master &= ~MST_W_BITS;
		master |= MST_WR_ENA | MST_TIMEOUT_DIS;

		ep->dev->int_enable |= INT_MSTWREND|INT_MSTWRTMOUT;
	}

	writel(master, &regs->dma_master);
	writel(ep->dev->int_enable, &regs->int_enable);
	return 0;
}

static void dma_advance(struct goku_udc *dev, struct goku_ep *ep)
{
	struct goku_request		*req;
	struct goku_udc_regs __iomem	*regs = ep->dev->regs;
	u32				master;

	master = readl(&regs->dma_master);

	if (unlikely(list_empty(&ep->queue))) {
stop:
		if (ep->is_in)
			dev->int_enable &= ~INT_MSTRDEND;
		else
			dev->int_enable &= ~(INT_MSTWREND|INT_MSTWRTMOUT);
		writel(dev->int_enable, &regs->int_enable);
		return;
	}
	req = list_entry(ep->queue.next, struct goku_request, queue);

	/* normal hw dma completion (not abort) */
	if (likely(ep->is_in)) {
		if (unlikely(master & MST_RD_ENA))
			return;
		req->req.actual = readl(&regs->in_dma_current);
	} else {
		if (unlikely(master & MST_WR_ENA))
			return;

		/* hardware merges short packets, and also hides packet
		 * overruns.  a partial packet MAY be in the fifo here.
		 */
		req->req.actual = readl(&regs->out_dma_current);
	}
	req->req.actual -= req->req.dma;
	req->req.actual++;

#ifdef USB_TRACE
	VDBG(dev, "done %s %s dma, %u/%u bytes, req %p\n",
		ep->ep.name, ep->is_in ? "IN" : "OUT",
		req->req.actual, req->req.length, req);
#endif
	done(ep, req, 0);
	if (list_empty(&ep->queue))
		goto stop;
	req = list_entry(ep->queue.next, struct goku_request, queue);
	(void) start_dma(ep, req);
}

static void abort_dma(struct goku_ep *ep, int status)
{
	struct goku_udc_regs __iomem	*regs = ep->dev->regs;
	struct goku_request		*req;
	u32				curr, master;

	/* NAK future host requests, hoping the implicit delay lets the
	 * dma engine finish reading (or writing) its latest packet and
	 * empty the dma buffer (up to 16 bytes).
	 *
	 * This avoids needing to clean up a partial packet in the fifo;
	 * we can't do that for IN without side effects to HALT and TOGGLE.
	 */
	command(regs, COMMAND_FIFO_DISABLE, ep->num);
	req = list_entry(ep->queue.next, struct goku_request, queue);
	master = readl(&regs->dma_master) & MST_RW_BITS;

	/* FIXME using these resets isn't usably documented. this may
	 * not work unless it's followed by disabling the endpoint.
	 *
	 * FIXME the OUT reset path doesn't even behave consistently.
	 */
	if (ep->is_in) {
		if (unlikely((readl(&regs->dma_master) & MST_RD_ENA) == 0))
			goto finished;
		curr = readl(&regs->in_dma_current);

		writel(curr, &regs->in_dma_end);
		writel(curr, &regs->in_dma_start);

		master &= ~MST_R_BITS;
		master |= MST_RD_RESET;
		writel(master, &regs->dma_master);

		if (readl(&regs->dma_master) & MST_RD_ENA)
			DBG(ep->dev, "IN dma active after reset!\n");

	} else {
		if (unlikely((readl(&regs->dma_master) & MST_WR_ENA) == 0))
			goto finished;
		curr = readl(&regs->out_dma_current);

		writel(curr, &regs->out_dma_end);
		writel(curr, &regs->out_dma_start);

		master &= ~MST_W_BITS;
		master |= MST_WR_RESET;
		writel(master, &regs->dma_master);

		if (readl(&regs->dma_master) & MST_WR_ENA)
			DBG(ep->dev, "OUT dma active after reset!\n");
	}
	req->req.actual = (curr - req->req.dma) + 1;
	req->req.status = status;

	VDBG(ep->dev, "%s %s %s %d/%d\n", __func__, ep->ep.name,
		ep->is_in ? "IN" : "OUT",
		req->req.actual, req->req.length);

	command(regs, COMMAND_FIFO_ENABLE, ep->num);

	return;

finished:
	/* dma already completed; no abort needed */
	command(regs, COMMAND_FIFO_ENABLE, ep->num);
	req->req.actual = req->req.length;
	req->req.status = 0;
}

/*-------------------------------------------------------------------------*/

static int
goku_queue(struct usb_ep *_ep, struct usb_request *_req, gfp_t gfp_flags)
{
	struct goku_request	*req;
	struct goku_ep		*ep;
	struct goku_udc		*dev;
	unsigned long		flags;
	int			status;

	/* always require a cpu-view buffer so pio works */
	req = container_of(_req, struct goku_request, req);
	if (unlikely(!_req || !_req->complete
			|| !_req->buf || !list_empty(&req->queue)))
		return -EINVAL;
	ep = container_of(_ep, struct goku_ep, ep);
	if (unlikely(!_ep || (!ep->ep.desc && ep->num != 0)))
		return -EINVAL;
	dev = ep->dev;
	if (unlikely(!dev->driver || dev->gadget.speed == USB_SPEED_UNKNOWN))
		return -ESHUTDOWN;

	/* can't touch registers when suspended */
	if (dev->ep0state == EP0_SUSPEND)
		return -EBUSY;

	/* set up dma mapping in case the caller didn't */
	if (ep->dma) {
		status = usb_gadget_map_request(&dev->gadget, &req->req,
				ep->is_in);
		if (status)
			return status;
	}

#ifdef USB_TRACE
	VDBG(dev, "%s queue req %p, len %u buf %p\n",
			_ep->name, _req, _req->length, _req->buf);
#endif

	spin_lock_irqsave(&dev->lock, flags);

	_req->status = -EINPROGRESS;
	_req->actual = 0;

	/* for ep0 IN without premature status, zlp is required and
	 * writing EOP starts the status stage (OUT).
	 */
	if (unlikely(ep->num == 0 && ep->is_in))
		_req->zero = 1;

	/* kickstart this i/o queue? */
	status = 0;
	if (list_empty(&ep->queue) && likely(!ep->stopped)) {
		/* dma:  done after dma completion IRQ (or error)
		 * pio:  done after last fifo operation
		 */
		if (ep->dma)
			status = start_dma(ep, req);
		else
			status = (ep->is_in ? write_fifo : read_fifo)(ep, req);

		if (unlikely(status != 0)) {
			if (status > 0)
				status = 0;
			req = NULL;
		}

	} /* else pio or dma irq handler advances the queue. */

	if (likely(req != NULL))
		list_add_tail(&req->queue, &ep->queue);

	if (likely(!list_empty(&ep->queue))
			&& likely(ep->num != 0)
			&& !ep->dma
			&& !(dev->int_enable & INT_EPxDATASET (ep->num)))
		pio_irq_enable(dev, dev->regs, ep->num);

	spin_unlock_irqrestore(&dev->lock, flags);

	/* pci writes may still be posted */
	return status;
}

/* dequeue ALL requests */
static void nuke(struct goku_ep *ep, int status)
{
	struct goku_request	*req;

	ep->stopped = 1;
	if (list_empty(&ep->queue))
		return;
	if (ep->dma)
		abort_dma(ep, status);
	while (!list_empty(&ep->queue)) {
		req = list_entry(ep->queue.next, struct goku_request, queue);
		done(ep, req, status);
	}
}

/* dequeue JUST ONE request */
static int goku_dequeue(struct usb_ep *_ep, struct usb_request *_req)
{
	struct goku_request	*req;
	struct goku_ep		*ep;
	struct goku_udc		*dev;
	unsigned long		flags;

	ep = container_of(_ep, struct goku_ep, ep);
	if (!_ep || !_req || (!ep->ep.desc && ep->num != 0))
		return -EINVAL;
	dev = ep->dev;
	if (!dev->driver)
		return -ESHUTDOWN;

	/* we can't touch (dma) registers when suspended */
	if (dev->ep0state == EP0_SUSPEND)
		return -EBUSY;

	VDBG(dev, "%s %s %s %s %p\n", __func__, _ep->name,
		ep->is_in ? "IN" : "OUT",
		ep->dma ? "dma" : "pio",
		_req);

	spin_lock_irqsave(&dev->lock, flags);

	/* make sure it's actually queued on this endpoint */
	list_for_each_entry (req, &ep->queue, queue) {
		if (&req->req == _req)
			break;
	}
	if (&req->req != _req) {
		spin_unlock_irqrestore (&dev->lock, flags);
		return -EINVAL;
	}

	if (ep->dma && ep->queue.next == &req->queue && !ep->stopped) {
		abort_dma(ep, -ECONNRESET);
		done(ep, req, -ECONNRESET);
		dma_advance(dev, ep);
	} else if (!list_empty(&req->queue))
		done(ep, req, -ECONNRESET);
	else
		req = NULL;
	spin_unlock_irqrestore(&dev->lock, flags);

	return req ? 0 : -EOPNOTSUPP;
}

/*-------------------------------------------------------------------------*/

static void goku_clear_halt(struct goku_ep *ep)
{
	// assert (ep->num !=0)
	VDBG(ep->dev, "%s clear halt\n", ep->ep.name);
	command(ep->dev->regs, COMMAND_SETDATA0, ep->num);
	command(ep->dev->regs, COMMAND_STALL_CLEAR, ep->num);
	if (ep->stopped) {
		ep->stopped = 0;
		if (ep->dma) {
			struct goku_request	*req;

			if (list_empty(&ep->queue))
				return;
			req = list_entry(ep->queue.next, struct goku_request,
						queue);
			(void) start_dma(ep, req);
		} else
			pio_advance(ep);
	}
}

static int goku_set_halt(struct usb_ep *_ep, int value)
{
	struct goku_ep	*ep;
	unsigned long	flags;
	int		retval = 0;

	if (!_ep)
		return -ENODEV;
	ep = container_of (_ep, struct goku_ep, ep);

	if (ep->num == 0) {
		if (value) {
			ep->dev->ep0state = EP0_STALL;
			ep->dev->ep[0].stopped = 1;
		} else
			return -EINVAL;

	/* don't change EPxSTATUS_EP_INVALID to READY */
	} else if (!ep->ep.desc) {
		DBG(ep->dev, "%s %s inactive?\n", __func__, ep->ep.name);
		return -EINVAL;
	}

	spin_lock_irqsave(&ep->dev->lock, flags);
	if (!list_empty(&ep->queue))
		retval = -EAGAIN;
	else if (ep->is_in && value
			/* data in (either) packet buffer? */
			&& (readl(&ep->dev->regs->DataSet)
					& DATASET_AB(ep->num)))
		retval = -EAGAIN;
	else if (!value)
		goku_clear_halt(ep);
	else {
		ep->stopped = 1;
		VDBG(ep->dev, "%s set halt\n", ep->ep.name);
		command(ep->dev->regs, COMMAND_STALL, ep->num);
		readl(ep->reg_status);
	}
	spin_unlock_irqrestore(&ep->dev->lock, flags);
	return retval;
}

static int goku_fifo_status(struct usb_ep *_ep)
{
	struct goku_ep			*ep;
	struct goku_udc_regs __iomem	*regs;
	u32				size;

	if (!_ep)
		return -ENODEV;
	ep = container_of(_ep, struct goku_ep, ep);

	/* size is only reported sanely for OUT */
	if (ep->is_in)
		return -EOPNOTSUPP;

	/* ignores 16-byte dma buffer; SizeH == 0 */
	regs = ep->dev->regs;
	size = readl(&regs->EPxSizeLA[ep->num]) & DATASIZE;
	size += readl(&regs->EPxSizeLB[ep->num]) & DATASIZE;
	VDBG(ep->dev, "%s %s %u\n", __func__, ep->ep.name, size);
	return size;
}

static void goku_fifo_flush(struct usb_ep *_ep)
{
	struct goku_ep			*ep;
	struct goku_udc_regs __iomem	*regs;
	u32				size;

	if (!_ep)
		return;
	ep = container_of(_ep, struct goku_ep, ep);
	VDBG(ep->dev, "%s %s\n", __func__, ep->ep.name);

	/* don't change EPxSTATUS_EP_INVALID to READY */
	if (!ep->ep.desc && ep->num != 0) {
		DBG(ep->dev, "%s %s inactive?\n", __func__, ep->ep.name);
		return;
	}

	regs = ep->dev->regs;
	size = readl(&regs->EPxSizeLA[ep->num]);
	size &= DATASIZE;

	/* Non-desirable behavior:  FIFO_CLEAR also clears the
	 * endpoint halt feature.  For OUT, we _could_ just read
	 * the bytes out (PIO, if !ep->dma); for in, no choice.
	 */
	if (size)
		command(regs, COMMAND_FIFO_CLEAR, ep->num);
}

static const struct usb_ep_ops goku_ep_ops = {
	.enable		= goku_ep_enable,
	.disable	= goku_ep_disable,

	.alloc_request	= goku_alloc_request,
	.free_request	= goku_free_request,

	.queue		= goku_queue,
	.dequeue	= goku_dequeue,

	.set_halt	= goku_set_halt,
	.fifo_status	= goku_fifo_status,
	.fifo_flush	= goku_fifo_flush,
};

/*-------------------------------------------------------------------------*/

static int goku_get_frame(struct usb_gadget *_gadget)
{
	return -EOPNOTSUPP;
}

static struct usb_ep *goku_match_ep(struct usb_gadget *g,
		struct usb_endpoint_descriptor *desc,
		struct usb_ss_ep_comp_descriptor *ep_comp)
{
	struct goku_udc	*dev = to_goku_udc(g);
	struct usb_ep *ep;

	switch (usb_endpoint_type(desc)) {
	case USB_ENDPOINT_XFER_INT:
		/* single buffering is enough */
		ep = &dev->ep[3].ep;
		if (usb_gadget_ep_match_desc(g, ep, desc, ep_comp))
			return ep;
		break;
	case USB_ENDPOINT_XFER_BULK:
		if (usb_endpoint_dir_in(desc)) {
			/* DMA may be available */
			ep = &dev->ep[2].ep;
			if (usb_gadget_ep_match_desc(g, ep, desc, ep_comp))
				return ep;
		}
		break;
	default:
		/* nothing */ ;
	}

	return NULL;
}

static int goku_udc_start(struct usb_gadget *g,
		struct usb_gadget_driver *driver);
static int goku_udc_stop(struct usb_gadget *g);

static const struct usb_gadget_ops goku_ops = {
	.get_frame	= goku_get_frame,
	.udc_start	= goku_udc_start,
	.udc_stop	= goku_udc_stop,
	.match_ep	= goku_match_ep,
	// no remote wakeup
	// not selfpowered
};

/*-------------------------------------------------------------------------*/

static inline const char *dmastr(void)
{
	if (use_dma == 0)
		return "(dma disabled)";
	else if (use_dma == 2)
		return "(dma IN and OUT)";
	else
		return "(dma IN)";
}

#ifdef CONFIG_USB_GADGET_DEBUG_FILES

static const char proc_node_name [] = "driver/udc";

#define FOURBITS "%s%s%s%s"
#define EIGHTBITS FOURBITS FOURBITS

static void dump_intmask(struct seq_file *m, const char *label, u32 mask)
{
	/* int_status is the same format ... */
	seq_printf(m, "%s %05X =" FOURBITS EIGHTBITS EIGHTBITS "\n",
		   label, mask,
		   (mask & INT_PWRDETECT) ? " power" : "",
		   (mask & INT_SYSERROR) ? " sys" : "",
		   (mask & INT_MSTRDEND) ? " in-dma" : "",
		   (mask & INT_MSTWRTMOUT) ? " wrtmo" : "",

		   (mask & INT_MSTWREND) ? " out-dma" : "",
		   (mask & INT_MSTWRSET) ? " wrset" : "",
		   (mask & INT_ERR) ? " err" : "",
		   (mask & INT_SOF) ? " sof" : "",

		   (mask & INT_EP3NAK) ? " ep3nak" : "",
		   (mask & INT_EP2NAK) ? " ep2nak" : "",
		   (mask & INT_EP1NAK) ? " ep1nak" : "",
		   (mask & INT_EP3DATASET) ? " ep3" : "",

		   (mask & INT_EP2DATASET) ? " ep2" : "",
		   (mask & INT_EP1DATASET) ? " ep1" : "",
		   (mask & INT_STATUSNAK) ? " ep0snak" : "",
		   (mask & INT_STATUS) ? " ep0status" : "",

		   (mask & INT_SETUP) ? " setup" : "",
		   (mask & INT_ENDPOINT0) ? " ep0" : "",
		   (mask & INT_USBRESET) ? " reset" : "",
		   (mask & INT_SUSPEND) ? " suspend" : "");
}

static const char *udc_ep_state(enum ep0state state)
{
	switch (state) {
	case EP0_DISCONNECT:
		return "ep0_disconnect";
	case EP0_IDLE:
		return "ep0_idle";
	case EP0_IN:
		return "ep0_in";
	case EP0_OUT:
		return "ep0_out";
	case EP0_STATUS:
		return "ep0_status";
	case EP0_STALL:
		return "ep0_stall";
	case EP0_SUSPEND:
		return "ep0_suspend";
	}

	return "ep0_?";
}

static const char *udc_ep_status(u32 status)
{
	switch (status & EPxSTATUS_EP_MASK) {
	case EPxSTATUS_EP_READY:
		return "ready";
	case EPxSTATUS_EP_DATAIN:
		return "packet";
	case EPxSTATUS_EP_FULL:
		return "full";
	case EPxSTATUS_EP_TX_ERR:	/* host will retry */
		return "tx_err";
	case EPxSTATUS_EP_RX_ERR:
		return "rx_err";
	case EPxSTATUS_EP_BUSY:		/* ep0 only */
		return "busy";
	case EPxSTATUS_EP_STALL:
		return "stall";
	case EPxSTATUS_EP_INVALID:	/* these "can't happen" */
		return "invalid";
	}

	return "?";
}

static int udc_proc_read(struct seq_file *m, void *v)
{
	struct goku_udc			*dev = m->private;
	struct goku_udc_regs __iomem	*regs = dev->regs;
	unsigned long			flags;
	int				i, is_usb_connected;
	u32				tmp;

	local_irq_save(flags);

	/* basic device status */
	tmp = readl(&regs->power_detect);
	is_usb_connected = tmp & PW_DETECT;
	seq_printf(m,
		   "%s - %s\n"
		   "%s version: %s %s\n"
		   "Gadget driver: %s\n"
		   "Host %s, %s\n"
		   "\n",
		   pci_name(dev->pdev), driver_desc,
		   driver_name, DRIVER_VERSION, dmastr(),
		   dev->driver ? dev->driver->driver.name : "(none)",
		   is_usb_connected
			   ? ((tmp & PW_PULLUP) ? "full speed" : "powered")
			   : "disconnected",
		   udc_ep_state(dev->ep0state));

	dump_intmask(m, "int_status", readl(&regs->int_status));
	dump_intmask(m, "int_enable", readl(&regs->int_enable));

	if (!is_usb_connected || !dev->driver || (tmp & PW_PULLUP) == 0)
		goto done;

	/* registers for (active) device and ep0 */
	seq_printf(m, "\nirqs %lu\ndataset %02x single.bcs %02x.%02x state %x addr %u\n",
		   dev->irqs, readl(&regs->DataSet),
		   readl(&regs->EPxSingle), readl(&regs->EPxBCS),
		   readl(&regs->UsbState),
		   readl(&regs->address));
	if (seq_has_overflowed(m))
		goto done;

	tmp = readl(&regs->dma_master);
	seq_printf(m, "dma %03X =" EIGHTBITS "%s %s\n",
		   tmp,
		   (tmp & MST_EOPB_DIS) ? " eopb-" : "",
		   (tmp & MST_EOPB_ENA) ? " eopb+" : "",
		   (tmp & MST_TIMEOUT_DIS) ? " tmo-" : "",
		   (tmp & MST_TIMEOUT_ENA) ? " tmo+" : "",

		   (tmp & MST_RD_EOPB) ? " eopb" : "",
		   (tmp & MST_RD_RESET) ? " in_reset" : "",
		   (tmp & MST_WR_RESET) ? " out_reset" : "",
		   (tmp & MST_RD_ENA) ? " IN" : "",

		   (tmp & MST_WR_ENA) ? " OUT" : "",
		   (tmp & MST_CONNECTION) ? "ep1in/ep2out" : "ep1out/ep2in");
	if (seq_has_overflowed(m))
		goto done;

	/* dump endpoint queues */
	for (i = 0; i < 4; i++) {
		struct goku_ep		*ep = &dev->ep [i];
		struct goku_request	*req;

		if (i && !ep->ep.desc)
			continue;

		tmp = readl(ep->reg_status);
		seq_printf(m, "%s %s max %u %s, irqs %lu, status %02x (%s) " FOURBITS "\n",
			   ep->ep.name,
			   ep->is_in ? "in" : "out",
			   ep->ep.maxpacket,
			   ep->dma ? "dma" : "pio",
			   ep->irqs,
			   tmp, udc_ep_status(tmp),
			   (tmp & EPxSTATUS_TOGGLE) ? "data1" : "data0",
			   (tmp & EPxSTATUS_SUSPEND) ? " suspend" : "",
			   (tmp & EPxSTATUS_FIFO_DISABLE) ? " disable" : "",
			   (tmp & EPxSTATUS_STAGE_ERROR) ? " ep0stat" : "");
		if (seq_has_overflowed(m))
			goto done;

		if (list_empty(&ep->queue)) {
			seq_puts(m, "\t(nothing queued)\n");
			if (seq_has_overflowed(m))
				goto done;
			continue;
		}
		list_for_each_entry(req, &ep->queue, queue) {
			if (ep->dma && req->queue.prev == &ep->queue) {
				if (i == UDC_MSTRD_ENDPOINT)
					tmp = readl(&regs->in_dma_current);
				else
					tmp = readl(&regs->out_dma_current);
				tmp -= req->req.dma;
				tmp++;
			} else
				tmp = req->req.actual;

			seq_printf(m, "\treq %p len %u/%u buf %p\n",
				   &req->req, tmp, req->req.length,
				   req->req.buf);
			if (seq_has_overflowed(m))
				goto done;
		}
	}

done:
	local_irq_restore(flags);
	return 0;
}
#endif	/* CONFIG_USB_GADGET_DEBUG_FILES */

/*-------------------------------------------------------------------------*/

static void udc_reinit (struct goku_udc *dev)
{
	static char *names [] = { "ep0", "ep1-bulk", "ep2-bulk", "ep3-bulk" };

	unsigned i;

	INIT_LIST_HEAD (&dev->gadget.ep_list);
	dev->gadget.ep0 = &dev->ep [0].ep;
	dev->gadget.speed = USB_SPEED_UNKNOWN;
	dev->ep0state = EP0_DISCONNECT;
	dev->irqs = 0;

	for (i = 0; i < 4; i++) {
		struct goku_ep	*ep = &dev->ep[i];

		ep->num = i;
		ep->ep.name = names[i];
		ep->reg_fifo = &dev->regs->ep_fifo [i];
		ep->reg_status = &dev->regs->ep_status [i];
		ep->reg_mode = &dev->regs->ep_mode[i];

		ep->ep.ops = &goku_ep_ops;
		list_add_tail (&ep->ep.ep_list, &dev->gadget.ep_list);
		ep->dev = dev;
		INIT_LIST_HEAD (&ep->queue);

		ep_reset(NULL, ep);

		if (i == 0)
			ep->ep.caps.type_control = true;
		else
			ep->ep.caps.type_bulk = true;

		ep->ep.caps.dir_in = true;
		ep->ep.caps.dir_out = true;
	}

	dev->ep[0].reg_mode = NULL;
	usb_ep_set_maxpacket_limit(&dev->ep[0].ep, MAX_EP0_SIZE);
	list_del_init (&dev->ep[0].ep.ep_list);
}

static void udc_reset(struct goku_udc *dev)
{
	struct goku_udc_regs __iomem	*regs = dev->regs;

	writel(0, &regs->power_detect);
	writel(0, &regs->int_enable);
	readl(&regs->int_enable);
	dev->int_enable = 0;

	/* deassert reset, leave USB D+ at hi-Z (no pullup)
	 * don't let INT_PWRDETECT sequence begin
	 */
	udelay(250);
	writel(PW_RESETB, &regs->power_detect);
	readl(&regs->int_enable);
}

static void ep0_start(struct goku_udc *dev)
{
	struct goku_udc_regs __iomem	*regs = dev->regs;
	unsigned			i;

	VDBG(dev, "%s\n", __func__);

	udc_reset(dev);
	udc_reinit (dev);
	//writel(MST_EOPB_ENA | MST_TIMEOUT_ENA, &regs->dma_master);

	/* hw handles set_address, set_feature, get_status; maybe more */
	writel(   G_REQMODE_SET_INTF | G_REQMODE_GET_INTF
		| G_REQMODE_SET_CONF | G_REQMODE_GET_CONF
		| G_REQMODE_GET_DESC
		| G_REQMODE_CLEAR_FEAT
		, &regs->reqmode);

	for (i = 0; i < 4; i++)
		dev->ep[i].irqs = 0;

	/* can't modify descriptors after writing UsbReady */
	for (i = 0; i < DESC_LEN; i++)
		writel(0, &regs->descriptors[i]);
	writel(0, &regs->UsbReady);

	/* expect ep0 requests when the host drops reset */
	writel(PW_RESETB | PW_PULLUP, &regs->power_detect);
	dev->int_enable = INT_DEVWIDE | INT_EP0;
	writel(dev->int_enable, &dev->regs->int_enable);
	readl(&regs->int_enable);
	dev->gadget.speed = USB_SPEED_FULL;
	dev->ep0state = EP0_IDLE;
}

static void udc_enable(struct goku_udc *dev)
{
	/* start enumeration now, or after power detect irq */
	if (readl(&dev->regs->power_detect) & PW_DETECT)
		ep0_start(dev);
	else {
		DBG(dev, "%s\n", __func__);
		dev->int_enable = INT_PWRDETECT;
		writel(dev->int_enable, &dev->regs->int_enable);
	}
}

/*-------------------------------------------------------------------------*/

/* keeping it simple:
 * - one bus driver, initted first;
 * - one function driver, initted second
 */

/* when a driver is successfully registered, it will receive
 * control requests including set_configuration(), which enables
 * non-control requests.  then usb traffic follows until a
 * disconnect is reported.  then a host may connect again, or
 * the driver might get unbound.
 */
static int goku_udc_start(struct usb_gadget *g,
		struct usb_gadget_driver *driver)
{
	struct goku_udc	*dev = to_goku_udc(g);

	/* hook up the driver */
	driver->driver.bus = NULL;
	dev->driver = driver;

	/*
	 * then enable host detection and ep0; and we're ready
	 * for set_configuration as well as eventual disconnect.
	 */
	udc_enable(dev);

	return 0;
}

static void stop_activity(struct goku_udc *dev)
{
	unsigned	i;

	DBG (dev, "%s\n", __func__);

	/* disconnect gadget driver after quiesceing hw and the driver */
	udc_reset (dev);
	for (i = 0; i < 4; i++)
		nuke(&dev->ep [i], -ESHUTDOWN);

	if (dev->driver)
		udc_enable(dev);
}

static int goku_udc_stop(struct usb_gadget *g)
{
	struct goku_udc	*dev = to_goku_udc(g);
	unsigned long	flags;

	spin_lock_irqsave(&dev->lock, flags);
	dev->driver = NULL;
	stop_activity(dev);
	spin_unlock_irqrestore(&dev->lock, flags);

	return 0;
}

/*-------------------------------------------------------------------------*/

static void ep0_setup(struct goku_udc *dev)
{
	struct goku_udc_regs __iomem	*regs = dev->regs;
	struct usb_ctrlrequest		ctrl;
	int				tmp;

	/* read SETUP packet and enter DATA stage */
	ctrl.bRequestType = readl(&regs->bRequestType);
	ctrl.bRequest = readl(&regs->bRequest);
	ctrl.wValue  = cpu_to_le16((readl(&regs->wValueH)  << 8)
					| readl(&regs->wValueL));
	ctrl.wIndex  = cpu_to_le16((readl(&regs->wIndexH)  << 8)
					| readl(&regs->wIndexL));
	ctrl.wLength = cpu_to_le16((readl(&regs->wLengthH) << 8)
					| readl(&regs->wLengthL));
	writel(0, &regs->SetupRecv);

	nuke(&dev->ep[0], 0);
	dev->ep[0].stopped = 0;
	if (likely(ctrl.bRequestType & USB_DIR_IN)) {
		dev->ep[0].is_in = 1;
		dev->ep0state = EP0_IN;
		/* detect early status stages */
		writel(ICONTROL_STATUSNAK, &dev->regs->IntControl);
	} else {
		dev->ep[0].is_in = 0;
		dev->ep0state = EP0_OUT;

		/* NOTE:  CLEAR_FEATURE is done in software so that we can
		 * synchronize transfer restarts after bulk IN stalls.  data
		 * won't even enter the fifo until the halt is cleared.
		 */
		switch (ctrl.bRequest) {
		case USB_REQ_CLEAR_FEATURE:
			switch (ctrl.bRequestType) {
			case USB_RECIP_ENDPOINT:
				tmp = le16_to_cpu(ctrl.wIndex) & 0x0f;
				/* active endpoint */
				if (tmp > 3 ||
				    (!dev->ep[tmp].ep.desc && tmp != 0))
					goto stall;
				if (ctrl.wIndex & cpu_to_le16(
						USB_DIR_IN)) {
					if (!dev->ep[tmp].is_in)
						goto stall;
				} else {
					if (dev->ep[tmp].is_in)
						goto stall;
				}
				if (ctrl.wValue != cpu_to_le16(
						USB_ENDPOINT_HALT))
					goto stall;
				if (tmp)
					goku_clear_halt(&dev->ep[tmp]);
succeed:
				/* start ep0out status stage */
				writel(~(1<<0), &regs->EOP);
				dev->ep[0].stopped = 1;
				dev->ep0state = EP0_STATUS;
				return;
			case USB_RECIP_DEVICE:
				/* device remote wakeup: always clear */
				if (ctrl.wValue != cpu_to_le16(1))
					goto stall;
				VDBG(dev, "clear dev remote wakeup\n");
				goto succeed;
			case USB_RECIP_INTERFACE:
				goto stall;
			default:		/* pass to gadget driver */
				break;
			}
			break;
		default:
			break;
		}
	}

#ifdef USB_TRACE
	VDBG(dev, "SETUP %02x.%02x v%04x i%04x l%04x\n",
		ctrl.bRequestType, ctrl.bRequest,
		le16_to_cpu(ctrl.wValue), le16_to_cpu(ctrl.wIndex),
		le16_to_cpu(ctrl.wLength));
#endif

	/* hw wants to know when we're configured (or not) */
	dev->req_config = (ctrl.bRequest == USB_REQ_SET_CONFIGURATION
				&& ctrl.bRequestType == USB_RECIP_DEVICE);
	if (unlikely(dev->req_config))
		dev->configured = (ctrl.wValue != cpu_to_le16(0));

	/* delegate everything to the gadget driver.
	 * it may respond after this irq handler returns.
	 */
	spin_unlock (&dev->lock);
	tmp = dev->driver->setup(&dev->gadget, &ctrl);
	spin_lock (&dev->lock);
	if (unlikely(tmp < 0)) {
stall:
#ifdef USB_TRACE
		VDBG(dev, "req %02x.%02x protocol STALL; err %d\n",
				ctrl.bRequestType, ctrl.bRequest, tmp);
#endif
		command(regs, COMMAND_STALL, 0);
		dev->ep[0].stopped = 1;
		dev->ep0state = EP0_STALL;
	}

	/* expect at least one data or status stage irq */
}

#define ACK(irqbit) { \
		stat &= ~irqbit; \
		writel(~irqbit, &regs->int_status); \
		handled = 1; \
		}

static irqreturn_t goku_irq(int irq, void *_dev)
{
	struct goku_udc			*dev = _dev;
	struct goku_udc_regs __iomem	*regs = dev->regs;
	struct goku_ep			*ep;
	u32				stat, handled = 0;
	unsigned			i, rescans = 5;

	spin_lock(&dev->lock);

rescan:
	stat = readl(&regs->int_status) & dev->int_enable;
        if (!stat)
		goto done;
	dev->irqs++;

	/* device-wide irqs */
	if (unlikely(stat & INT_DEVWIDE)) {
		if (stat & INT_SYSERROR) {
			ERROR(dev, "system error\n");
			stop_activity(dev);
			stat = 0;
			handled = 1;
			// FIXME have a neater way to prevent re-enumeration
			dev->driver = NULL;
			goto done;
		}
		if (stat & INT_PWRDETECT) {
			writel(~stat, &regs->int_status);
			if (readl(&dev->regs->power_detect) & PW_DETECT) {
				VDBG(dev, "connect\n");
				ep0_start(dev);
			} else {
				DBG(dev, "disconnect\n");
				if (dev->gadget.speed == USB_SPEED_FULL)
					stop_activity(dev);
				dev->ep0state = EP0_DISCONNECT;
				dev->int_enable = INT_DEVWIDE;
				writel(dev->int_enable, &dev->regs->int_enable);
			}
			stat = 0;
			handled = 1;
			goto done;
		}
		if (stat & INT_SUSPEND) {
			ACK(INT_SUSPEND);
			if (readl(&regs->ep_status[0]) & EPxSTATUS_SUSPEND) {
				switch (dev->ep0state) {
				case EP0_DISCONNECT:
				case EP0_SUSPEND:
					goto pm_next;
				default:
					break;
				}
				DBG(dev, "USB suspend\n");
				dev->ep0state = EP0_SUSPEND;
				if (dev->gadget.speed != USB_SPEED_UNKNOWN
						&& dev->driver
						&& dev->driver->suspend) {
					spin_unlock(&dev->lock);
					dev->driver->suspend(&dev->gadget);
					spin_lock(&dev->lock);
				}
			} else {
				if (dev->ep0state != EP0_SUSPEND) {
					DBG(dev, "bogus USB resume %d\n",
						dev->ep0state);
					goto pm_next;
				}
				DBG(dev, "USB resume\n");
				dev->ep0state = EP0_IDLE;
				if (dev->gadget.speed != USB_SPEED_UNKNOWN
						&& dev->driver
						&& dev->driver->resume) {
					spin_unlock(&dev->lock);
					dev->driver->resume(&dev->gadget);
					spin_lock(&dev->lock);
				}
			}
		}
pm_next:
		if (stat & INT_USBRESET) {		/* hub reset done */
			ACK(INT_USBRESET);
			INFO(dev, "USB reset done, gadget %s\n",
				dev->driver->driver.name);
		}
		// and INT_ERR on some endpoint's crc/bitstuff/... problem
	}

	/* progress ep0 setup, data, or status stages.
	 * no transition {EP0_STATUS, EP0_STALL} --> EP0_IDLE; saves irqs
	 */
	if (stat & INT_SETUP) {
		ACK(INT_SETUP);
		dev->ep[0].irqs++;
		ep0_setup(dev);
	}
        if (stat & INT_STATUSNAK) {
		ACK(INT_STATUSNAK|INT_ENDPOINT0);
		if (dev->ep0state == EP0_IN) {
			ep = &dev->ep[0];
			ep->irqs++;
			nuke(ep, 0);
			writel(~(1<<0), &regs->EOP);
			dev->ep0state = EP0_STATUS;
		}
	}
        if (stat & INT_ENDPOINT0) {
		ACK(INT_ENDPOINT0);
		ep = &dev->ep[0];
		ep->irqs++;
		pio_advance(ep);
        }

	/* dma completion */
        if (stat & INT_MSTRDEND) {	/* IN */
		ACK(INT_MSTRDEND);
		ep = &dev->ep[UDC_MSTRD_ENDPOINT];
		ep->irqs++;
		dma_advance(dev, ep);
        }
        if (stat & INT_MSTWREND) {	/* OUT */
		ACK(INT_MSTWREND);
		ep = &dev->ep[UDC_MSTWR_ENDPOINT];
		ep->irqs++;
		dma_advance(dev, ep);
        }
        if (stat & INT_MSTWRTMOUT) {	/* OUT */
		ACK(INT_MSTWRTMOUT);
		ep = &dev->ep[UDC_MSTWR_ENDPOINT];
		ep->irqs++;
		ERROR(dev, "%s write timeout ?\n", ep->ep.name);
		// reset dma? then dma_advance()
        }

	/* pio */
	for (i = 1; i < 4; i++) {
		u32		tmp = INT_EPxDATASET(i);

		if (!(stat & tmp))
			continue;
		ep = &dev->ep[i];
		pio_advance(ep);
		if (list_empty (&ep->queue))
			pio_irq_disable(dev, regs, i);
		stat &= ~tmp;
		handled = 1;
		ep->irqs++;
	}

	if (rescans--)
		goto rescan;

done:
	(void)readl(&regs->int_enable);
	spin_unlock(&dev->lock);
	if (stat)
		DBG(dev, "unhandled irq status: %05x (%05x, %05x)\n", stat,
				readl(&regs->int_status), dev->int_enable);
	return IRQ_RETVAL(handled);
}

#undef ACK

/*-------------------------------------------------------------------------*/

static void gadget_release(struct device *_dev)
{
	struct goku_udc	*dev = dev_get_drvdata(_dev);

	kfree(dev);
}

/* tear down the binding between this driver and the pci device */

static void goku_remove(struct pci_dev *pdev)
{
	struct goku_udc		*dev = pci_get_drvdata(pdev);

	DBG(dev, "%s\n", __func__);

	usb_del_gadget_udc(&dev->gadget);

	BUG_ON(dev->driver);

#ifdef CONFIG_USB_GADGET_DEBUG_FILES
	remove_proc_entry(proc_node_name, NULL);
#endif
	if (dev->regs)
		udc_reset(dev);
	if (dev->got_irq)
		free_irq(pdev->irq, dev);
	if (dev->regs)
		iounmap(dev->regs);
	if (dev->got_region)
		release_mem_region(pci_resource_start (pdev, 0),
				pci_resource_len (pdev, 0));
	if (dev->enabled)
		pci_disable_device(pdev);

	dev->regs = NULL;

	INFO(dev, "unbind\n");
}

/* wrap this driver around the specified pci device, but
 * don't respond over USB until a gadget driver binds to us.
 */

static int goku_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct goku_udc		*dev = NULL;
	unsigned long		resource, len;
	void __iomem		*base = NULL;
	int			retval;

	if (!pdev->irq) {
		printk(KERN_ERR "Check PCI %s IRQ setup!\n", pci_name(pdev));
		retval = -ENODEV;
		goto err;
	}

	/* alloc, and start init */
	dev = kzalloc (sizeof *dev, GFP_KERNEL);
	if (!dev) {
		retval = -ENOMEM;
		goto err;
	}

	spin_lock_init(&dev->lock);
	dev->pdev = pdev;
	dev->gadget.ops = &goku_ops;
	dev->gadget.max_speed = USB_SPEED_FULL;

	/* the "gadget" abstracts/virtualizes the controller */
	dev->gadget.name = driver_name;

	/* now all the pci goodies ... */
	retval = pci_enable_device(pdev);
	if (retval < 0) {
		DBG(dev, "can't enable, %d\n", retval);
		goto err;
	}
	dev->enabled = 1;

	resource = pci_resource_start(pdev, 0);
	len = pci_resource_len(pdev, 0);
	if (!request_mem_region(resource, len, driver_name)) {
		DBG(dev, "controller already in use\n");
		retval = -EBUSY;
		goto err;
	}
	dev->got_region = 1;

	base = ioremap_nocache(resource, len);
	if (base == NULL) {
		DBG(dev, "can't map memory\n");
		retval = -EFAULT;
		goto err;
	}
	dev->regs = (struct goku_udc_regs __iomem *) base;

	pci_set_drvdata(pdev, dev);
	INFO(dev, "%s\n", driver_desc);
	INFO(dev, "version: " DRIVER_VERSION " %s\n", dmastr());
	INFO(dev, "irq %d, pci mem %p\n", pdev->irq, base);

	/* init to known state, then setup irqs */
	udc_reset(dev);
	udc_reinit (dev);
	if (request_irq(pdev->irq, goku_irq, IRQF_SHARED,
			driver_name, dev) != 0) {
		DBG(dev, "request interrupt %d failed\n", pdev->irq);
		retval = -EBUSY;
		goto err;
	}
	dev->got_irq = 1;
	if (use_dma)
		pci_set_master(pdev);


#ifdef CONFIG_USB_GADGET_DEBUG_FILES
	proc_create_single_data(proc_node_name, 0, NULL, udc_proc_read, dev);
#endif

	retval = usb_add_gadget_udc_release(&pdev->dev, &dev->gadget,
			gadget_release);
	if (retval)
		goto err;

	return 0;

err:
	if (dev)
		goku_remove (pdev);
	/* gadget_release is not registered yet, kfree explicitly */
	kfree(dev);
	return retval;
}


/*-------------------------------------------------------------------------*/

static const struct pci_device_id pci_ids[] = { {
	.class =	PCI_CLASS_SERIAL_USB_DEVICE,
	.class_mask =	~0,
	.vendor =	0x102f,		/* Toshiba */
	.device =	0x0107,		/* this UDC */
	.subvendor =	PCI_ANY_ID,
	.subdevice =	PCI_ANY_ID,

}, { /* end: all zeroes */ }
};
MODULE_DEVICE_TABLE (pci, pci_ids);

static struct pci_driver goku_pci_driver = {
	.name =		(char *) driver_name,
	.id_table =	pci_ids,

	.probe =	goku_probe,
	.remove =	goku_remove,

	/* FIXME add power management support */
};

module_pci_driver(goku_pci_driver);
