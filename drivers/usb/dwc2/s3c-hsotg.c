/**
 * linux/drivers/usb/gadget/s3c-hsotg.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 *      Ben Dooks <ben@simtec.co.uk>
 *      http://armlinux.simtec.co.uk/
 *
 * S3C USB2.0 High-speed / OtG driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>

#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/usb/phy.h>
#include <linux/phy/phy.h>
#include <linux/platform_data/s3c-hsotg.h>

#include "hw.h"
#include "core.h"

/*
 * EP0_MPS_LIMIT
 *
 * Unfortunately there seems to be a limit of the amount of data that can
 * be transferred by IN transactions on EP0. This is either 127 bytes or 3
 * packets (which practically means 1 packet and 63 bytes of data) when the
 * MPS is set to 64.
 *
 * This means if we are wanting to move >127 bytes of data, we need to
 * split the transactions up, but just doing one packet at a time does
 * not work (this may be an implicit DATA0 PID on first packet of the
 * transaction) and doing 2 packets is outside the controller's limits.
 *
 * If we try to lower the MPS size for EP0, then no transfers work properly
 * for EP0, and the system will fail basic enumeration. As no cause for this
 * has currently been found, we cannot support any large IN transfers for
 * EP0.
 */
#define EP0_MPS_LIMIT	64

/* conversion functions */
static inline struct s3c_hsotg_req *our_req(struct usb_request *req)
{
	return container_of(req, struct s3c_hsotg_req, req);
}

static inline struct s3c_hsotg_ep *our_ep(struct usb_ep *ep)
{
	return container_of(ep, struct s3c_hsotg_ep, ep);
}

static inline struct dwc2_hsotg *to_hsotg(struct usb_gadget *gadget)
{
	return container_of(gadget, struct dwc2_hsotg, gadget);
}

static inline void __orr32(void __iomem *ptr, u32 val)
{
	writel(readl(ptr) | val, ptr);
}

static inline void __bic32(void __iomem *ptr, u32 val)
{
	writel(readl(ptr) & ~val, ptr);
}

/* forward decleration of functions */

/**
 * using_dma - return the DMA status of the driver.
 * @hsotg: The driver state.
 *
 * Return true if we're using DMA.
 *
 * Currently, we have the DMA support code worked into everywhere
 * that needs it, but the AMBA DMA implementation in the hardware can
 * only DMA from 32bit aligned addresses. This means that gadgets such
 * as the CDC Ethernet cannot work as they often pass packets which are
 * not 32bit aligned.
 *
 * Unfortunately the choice to use DMA or not is global to the controller
 * and seems to be only settable when the controller is being put through
 * a core reset. This means we either need to fix the gadgets to take
 * account of DMA alignment, or add bounce buffers (yuerk).
 *
 * Until this issue is sorted out, we always return 'false'.
 */
static inline bool using_dma(struct s3c_hsotg *hsotg)
{
	return false;	/* support is not complete */
}

/**
 * s3c_hsotg_en_gsint - enable one or more of the general interrupt
 * @hsotg: The device state
 * @ints: A bitmask of the interrupts to enable
 */
static void s3c_hsotg_en_gsint(struct dwc2_hsotg *dwc2, u32 ints)
{
	u32 gsintmsk = readl(dwc2->regs + GINTMSK);
	u32 new_gsintmsk;

	new_gsintmsk = gsintmsk | ints;

	if (new_gsintmsk != gsintmsk) {
		dev_dbg(dwc2->dev, "gsintmsk now 0x%08x\n", new_gsintmsk);
		writel(new_gsintmsk, dwc2->regs + GINTMSK);
	}
}

/**
 * s3c_hsotg_disable_gsint - disable one or more of the general interrupt
 * @hsotg: The device state
 * @ints: A bitmask of the interrupts to enable
 */
void s3c_hsotg_disable_gsint(struct dwc2_hsotg *dwc2, u32 ints)
{
	u32 gsintmsk = readl(dwc2->regs + GINTMSK);
	u32 new_gsintmsk;

	new_gsintmsk = gsintmsk & ~ints;

	if (new_gsintmsk != gsintmsk)
		writel(new_gsintmsk, dwc2->regs + GINTMSK);
}

/**
 * s3c_hsotg_ctrl_epint - enable/disable an endpoint irq
 * @hsotg: The device state
 * @ep: The endpoint index
 * @dir_in: True if direction is in.
 * @en: The enable value, true to enable
 *
 * Set or clear the mask for an individual endpoint's interrupt
 * request.
 */
static void s3c_hsotg_ctrl_epint(struct dwc2_hsotg *dwc2,
				 unsigned int ep, unsigned int dir_in,
				 unsigned int en)
{
	unsigned long flags;
	u32 bit = 1 << ep;
	u32 daint;

	if (!dir_in)
		bit <<= 16;

	local_irq_save(flags);
	daint = readl(dwc2->regs + DAINTMSK);
	if (en)
		daint |= bit;
	else
		daint &= ~bit;
	writel(daint, dwc2->regs + DAINTMSK);
	local_irq_restore(flags);
}

/**
 * s3c_hsotg_init_fifo - initialise non-periodic FIFOs
 * @hsotg: The device instance.
 */
static void s3c_hsotg_init_fifo(struct dwc2_hsotg *dwc2)
{
	unsigned int ep;
	unsigned int addr;
	unsigned int size;
	int timeout;
	u32 val;

	/* set FIFO sizes to 2048/1024 */

	writel(2048, dwc2->regs + GRXFSIZ);
	writel(DPTXFSIZN_DPTXFADDR(2048) |
	       DPTXFSIZN_DPTXFSIZE(1024),
	       dwc2->regs + GNPTXFSIZ);

	/*
	 * arange all the rest of the TX FIFOs, as some versions of this
	 * block have overlapping default addresses. This also ensures
	 * that if the settings have been changed, then they are set to
	 * known values.
	 */

	/* start at the end of the GNPTXFSIZ, rounded up */
	addr = 2048 + 1024;
	size = 768;

	/*
	 * currently we allocate TX FIFOs for all possible endpoints,
	 * and assume that they are all the same size.
	 */

	for (ep = 1; ep <= 15; ep++) {
		val = addr;
		val |= size << FIFOSIZE_DEPTH_SHIFT;
		addr += size;

		writel(val, dwc2->regs + DPTXFSIZN(ep));
	}

	/*
	 * according to p428 of the design guide, we need to ensure that
	 * all fifos are flushed before continuing
	 */

	writel(GRSTCTL_TXFNUM(0x10) | GRSTCTL_TXFFLSH |
	       GRSTCTL_RXFFLSH, dwc2->regs + GRSTCTL);

	/* wait until the fifos are both flushed */
	timeout = 100;
	while (1) {
		val = readl(dwc2->regs + GRSTCTL);

		if ((val & (GRSTCTL_TXFFLSH | GRSTCTL_RXFFLSH)) == 0)
			break;

		if (--timeout == 0) {
			dev_err(dwc2->dev,
				"%s: timeout flushing fifos (GRSTCTL=%08x)\n",
				__func__, val);
		}

		udelay(1);
	}

	dev_dbg(dwc2->dev, "FIFOs reset, timeout at %d\n", timeout);
}

/**
 * @ep: USB endpoint to allocate request for.
 * @flags: Allocation flags
 *
 * Allocate a new USB request structure appropriate for the specified endpoint
 */
static struct usb_request *s3c_hsotg_ep_alloc_request(struct usb_ep *ep,
						      gfp_t flags)
{
	struct s3c_hsotg_req *req;

	req = kzalloc(sizeof(struct s3c_hsotg_req), flags);
	if (!req)
		return NULL;

	INIT_LIST_HEAD(&req->queue);

	return &req->req;
}

/**
 * is_ep_periodic - return true if the endpoint is in periodic mode.
 * @hs_ep: The endpoint to query.
 *
 * Returns true if the endpoint is in periodic mode, meaning it is being
 * used for an Interrupt or ISO transfer.
 */
static inline int is_ep_periodic(struct s3c_hsotg_ep *hs_ep)
{
	return hs_ep->periodic;
}

/**
 * s3c_hsotg_unmap_dma - unmap the DMA memory being used for the request
 * @hsotg: The device state.
 * @hs_ep: The endpoint for the request
 * @hs_req: The request being processed.
 *
 * This is the reverse of s3c_hsotg_map_dma(), called for the completion
 * of a request to ensure the buffer is ready for access by the caller.
 */
static void s3c_hsotg_unmap_dma(struct dwc2_hsotg *dwc2,
				struct s3c_hsotg_ep *hs_ep,
				struct s3c_hsotg_req *hs_req)
{
	struct usb_request *req = &hs_req->req;

	/* ignore this if we're not moving any data */
	if (hs_req->req.length == 0)
		return;

	usb_gadget_unmap_request(&dwc2->gadget, req, hs_ep->dir_in);
}

/**
 * s3c_hsotg_write_fifo - write packet Data to the TxFIFO
 * @hsotg: The controller state.
 * @hs_ep: The endpoint we're going to write for.
 * @hs_req: The request to write data for.
 *
 * This is called when the TxFIFO has some space in it to hold a new
 * transmission and we have something to give it. The actual setup of
 * the data size is done elsewhere, so all we have to do is to actually
 * write the data.
 *
 * The return value is zero if there is more space (or nothing was done)
 * otherwise -ENOSPC is returned if the FIFO space was used up.
 *
 * This routine is only needed for PIO
 */
static int s3c_hsotg_write_fifo(struct dwc2_hsotg *dwc2,
				struct s3c_hsotg_ep *hs_ep,
				struct s3c_hsotg_req *hs_req)
{
	bool periodic = is_ep_periodic(hs_ep);
	u32 gnptxsts = readl(dwc2->regs + GNPTXSTS);
	int buf_pos = hs_req->req.actual;
	int to_write = hs_ep->size_loaded;
	void *data;
	int can_write;
	int pkt_round;

	to_write -= (buf_pos - hs_ep->last_load);

	/* if there's nothing to write, get out early */
	if (to_write == 0)
		return 0;

	if (periodic && !dwc2->s3c_hsotg->dedicated_fifos) {
		u32 epsize = readl(dwc2->regs + DIEPTSIZ(hs_ep->index));
		int size_left;
		int size_done;

		/*
		 * work out how much data was loaded so we can calculate
		 * how much data is left in the fifo.
		 */

		size_left = DXEPTSIZ_XFERSIZE_GET(epsize);

		/*
		 * if shared fifo, we cannot write anything until the
		 * previous data has been completely sent.
		 */
		if (hs_ep->fifo_load != 0) {
			s3c_hsotg_en_gsint(dwc2, GINTSTS_PTXFEMP);
			return -ENOSPC;
		}

		dev_dbg(dwc2->dev, "%s: left=%d, load=%d, fifo=%d, size %d\n",
			__func__, size_left,
			hs_ep->size_loaded, hs_ep->fifo_load, hs_ep->fifo_size);

		/* how much of the data has moved */
		size_done = hs_ep->size_loaded - size_left;

		/* how much data is left in the fifo */
		can_write = hs_ep->fifo_load - size_done;
		dev_dbg(dwc2->dev, "%s: => can_write1=%d\n",
			__func__, can_write);

		can_write = hs_ep->fifo_size - can_write;
		dev_dbg(dwc2->dev, "%s: => can_write2=%d\n",
			__func__, can_write);

		if (can_write <= 0) {
			s3c_hsotg_en_gsint(dwc2, GINTSTS_PTXFEMP);
			return -ENOSPC;
		}
	} else if (dwc2->s3c_hsotg->dedicated_fifos && hs_ep->index != 0) {
		can_write = readl(dwc2->regs + DTXFSTS(hs_ep->index));

		can_write &= 0xffff;
		can_write *= 4;
	} else {
		if (GNPTXSTS_NP_TXQ_SPC_AVAIL_GET(gnptxsts) == 0) {
			dev_dbg(dwc2->dev,
				"%s: no queue slots available (0x%08x)\n",
				__func__, gnptxsts);

			s3c_hsotg_en_gsint(dwc2, GINTSTS_NPTXFEMP);
			return -ENOSPC;
		}

		can_write = GNPTXSTS_NP_TXF_SPC_AVAIL_GET(gnptxsts);
		can_write *= 4;	/* fifo size is in 32bit quantities. */
	}

	dev_dbg(dwc2->dev, "%s: GNPTXSTS=%08x, can=%d, to=%d, mps %d\n",
		 __func__, gnptxsts, can_write, to_write, hs_ep->ep.maxpacket);

	/*
	 * limit to 512 bytes of data, it seems at least on the non-periodic
	 * FIFO, requests of >512 cause the endpoint to get stuck with a
	 * fragment of the end of the transfer in it.
	 */
	if (can_write > 512)
		can_write = 512;

	/*
	 * limit the write to one max-packet size worth of data, but allow
	 * the transfer to return that it did not run out of fifo space
	 * doing it.
	 */
	if (to_write > hs_ep->ep.maxpacket) {
		to_write = hs_ep->ep.maxpacket;

		s3c_hsotg_en_gsint(dwc2,
				   periodic ? GINTSTS_PTXFEMP :
				   GINTSTS_NPTXFEMP);
	}

	/* see if we can write data */

	if (to_write > can_write) {
		to_write = can_write;
		pkt_round = to_write % hs_ep->ep.maxpacket;

		/*
		 * Round the write down to an
		 * exact number of packets.
		 *
		 * Note, we do not currently check to see if we can ever
		 * write a full packet or not to the FIFO.
		 */

		if (pkt_round)
			to_write -= pkt_round;

		/*
		 * enable correct FIFO interrupt to alert us when there
		 * is more room left.
		 */

		s3c_hsotg_en_gsint(dwc2,
				   periodic ? GINTSTS_PTXFEMP :
				   GINTSTS_NPTXFEMP);
	}

	dev_dbg(dwc2->dev, "write %d/%d, can_write %d, done %d\n",
		 to_write, hs_req->req.length, can_write, buf_pos);

	if (to_write <= 0)
		return -ENOSPC;

	hs_req->req.actual = buf_pos + to_write;
	hs_ep->total_data += to_write;

	if (periodic)
		hs_ep->fifo_load += to_write;

	to_write = DIV_ROUND_UP(to_write, 4);
	data = hs_req->req.buf + buf_pos;

	writesl(dwc2->regs + EPFIFO(hs_ep->index), data, to_write);

	return (to_write >= can_write) ? -ENOSPC : 0;
}

/**
 * get_ep_limit - get the maximum data legnth for this endpoint
 * @hs_ep: The endpoint
 *
 * Return the maximum data that can be queued in one go on a given endpoint
 * so that transfers that are too long can be split.
 */
static unsigned get_ep_limit(struct s3c_hsotg_ep *hs_ep)
{
	int index = hs_ep->index;
	unsigned maxsize;
	unsigned maxpkt;

	if (index != 0) {
		maxsize = DXEPTSIZ_XFERSIZE_LIMIT + 1;
		maxpkt = DXEPTSIZ_PKTCNT_LIMIT + 1;
	} else {
		maxsize = 64+64;
		if (hs_ep->dir_in)
			maxpkt = DIEPTSIZ0_PKTCNT_LIMIT + 1;
		else
			maxpkt = 2;
	}

	/* we made the constant loading easier above by using +1 */
	maxpkt--;
	maxsize--;

	/*
	 * constrain by packet count if maxpkts*pktsize is greater
	 * than the length register size.
	 */

	if ((maxpkt * hs_ep->ep.maxpacket) < maxsize)
		maxsize = maxpkt * hs_ep->ep.maxpacket;

	return maxsize;
}

/**
 * s3c_hsotg_start_req - start a USB request from an endpoint's queue
 * @hsotg: The controller state.
 * @hs_ep: The endpoint to process a request for
 * @hs_req: The request to start.
 * @continuing: True if we are doing more for the current request.
 *
 * Start the given request running by setting the endpoint registers
 * appropriately, and writing any data to the FIFOs.
 */
static void s3c_hsotg_start_req(struct dwc2_hsotg *dwc2,
				struct s3c_hsotg_ep *hs_ep,
				struct s3c_hsotg_req *hs_req,
				bool continuing)
{
	struct usb_request *ureq = &hs_req->req;
	int index = hs_ep->index;
	int dir_in = hs_ep->dir_in;
	u32 epctrl_reg;
	u32 epsize_reg;
	u32 epsize;
	u32 ctrl;
	unsigned length;
	unsigned packets;
	unsigned maxreq;

	if (index != 0) {
		if (hs_ep->req && !continuing) {
			dev_err(dwc2->dev, "%s: active request\n", __func__);
			WARN_ON(1);
			return;
		} else if (hs_ep->req != hs_req && continuing) {
			dev_err(dwc2->dev,
				"%s: continue different req\n", __func__);
			WARN_ON(1);
			return;
		}
	}

	epctrl_reg = dir_in ? DIEPCTL(index) : DOEPCTL(index);
	epsize_reg = dir_in ? DIEPTSIZ(index) : DOEPTSIZ(index);

	dev_dbg(dwc2->dev, "%s: DXEPCTL=0x%08x, ep %d, dir %s\n",
		__func__, readl(dwc2->regs + epctrl_reg), index,
		hs_ep->dir_in ? "in" : "out");

	/* If endpoint is stalled, we will restart request later */
	ctrl = readl(dwc2->regs + epctrl_reg);

	if (ctrl & DXEPCTL_STALL) {
		dev_warn(dwc2->dev, "%s: ep%d is stalled\n", __func__, index);
		return;
	}

	length = ureq->length - ureq->actual;
	dev_dbg(dwc2->dev, "ureq->length:%d ureq->actual:%d\n",
		ureq->length, ureq->actual);
	if (0)
		dev_dbg(dwc2->dev,
			"REQ buf %p len %d dma 0x%08x noi=%d zp=%d snok=%d\n",
			ureq->buf, length, ureq->dma,
			ureq->no_interrupt, ureq->zero, ureq->short_not_ok);

	maxreq = get_ep_limit(hs_ep);
	if (length > maxreq) {
		int round = maxreq % hs_ep->ep.maxpacket;

		dev_dbg(dwc2->dev, "%s: length %d, max-req %d, r %d\n",
			__func__, length, maxreq, round);

		/* round down to multiple of packets */
		if (round)
			maxreq -= round;

		length = maxreq;
	}

	if (length)
		packets = DIV_ROUND_UP(length, hs_ep->ep.maxpacket);
	else
		packets = 1;	/* send one packet if length is zero. */

	if (dir_in && index != 0)
		epsize = DXEPTSIZ_MC(1);
	else
		epsize = 0;

	if (index != 0 && ureq->zero) {
		/*
		 * test for the packets being exactly right for the
		 * transfer
		 */

		if (length == (packets * hs_ep->ep.maxpacket))
			packets++;
	}

	epsize |= DXEPTSIZ_PKTCNT(packets);
	epsize |= DXEPTSIZ_XFERSIZE(length);

	dev_dbg(dwc2->dev, "%s: %d@%d/%d, 0x%08x => 0x%08x\n",
		__func__, packets, length, ureq->length, epsize, epsize_reg);

	/* store the request as the current one we're doing */
	hs_ep->req = hs_req;

	/* write size / packets */
	writel(epsize, dwc2->regs + epsize_reg);

	if (using_dma(dwc2->s3c_hsotg) && !continuing) {
		unsigned int dma_reg;

		/*
		 * write DMA address to control register, buffer already
		 * synced by s3c_hsotg_ep_queue().
		 */

		dma_reg = dir_in ? DIEPDMA(index) : DOEPDMA(index);
		writel(ureq->dma, dwc2->regs + dma_reg);

		dev_dbg(dwc2->dev, "%s: 0x%08x => 0x%08x\n",
			__func__, ureq->dma, dma_reg);
	}

	ctrl |= DXEPCTL_EPENA;	/* ensure ep enabled */
	ctrl |= DXEPCTL_USBACTEP;

	dev_dbg(dwc2->dev, "setup req:%d\n", dwc2->s3c_hsotg->setup);

	/* For Setup request do not clear NAK */
	if (dwc2->s3c_hsotg->setup && index == 0)
		dwc2->s3c_hsotg->setup = 0;
	else
		ctrl |= DXEPCTL_CNAK;	/* clear NAK set by core */


	dev_dbg(dwc2->dev, "%s: DXEPCTL=0x%08x\n", __func__, ctrl);
	writel(ctrl, dwc2->regs + epctrl_reg);

	/*
	 * set these, it seems that DMA support increments past the end
	 * of the packet buffer so we need to calculate the length from
	 * this information.
	 */
	hs_ep->size_loaded = length;
	hs_ep->last_load = ureq->actual;

	if (dir_in && !using_dma(dwc2->s3c_hsotg)) {
		/* set these anyway, we may need them for non-periodic in */
		hs_ep->fifo_load = 0;

		s3c_hsotg_write_fifo(dwc2, hs_ep, hs_req);
	}

	/*
	 * clear the INTknTXFEmpMsk when we start request, more as a aide
	 * to debugging to see what is going on.
	 */
	if (dir_in)
		writel(DIEPMSK_INTKNTXFEMPMSK,
		       dwc2->regs + DIEPINT(index));

	/*
	 * Note, trying to clear the NAK here causes problems with transmit
	 * on the S3C6400 ending up with the TXFIFO becoming full.
	 */

	/* check ep is enabled */
	if (!(readl(dwc2->regs + epctrl_reg) & DXEPCTL_EPENA))
		dev_warn(dwc2->dev,
			 "ep%d: failed to become enabled (DXEPCTL=0x%08x)?\n",
			 index, readl(dwc2->regs + epctrl_reg));

	dev_dbg(dwc2->dev, "%s: DxEPCTL=0x%08x\n",
		__func__, readl(dwc2->regs + epctrl_reg));
}

/**
 * s3c_hsotg_map_dma - map the DMA memory being used for the request
 * @hsotg: The device state.
 * @hs_ep: The endpoint the request is on.
 * @req: The request being processed.
 *
 * We've been asked to queue a request, so ensure that the memory buffer
 * is correctly setup for DMA. If we've been passed an extant DMA address
 * then ensure the buffer has been synced to memory. If our buffer has no
 * DMA memory, then we map the memory and mark our request to allow us to
 * cleanup on completion.
 */
static int s3c_hsotg_map_dma(struct dwc2_hsotg *dwc2,
			     struct s3c_hsotg_ep *hs_ep,
			     struct usb_request *req)
{
	struct s3c_hsotg_req *hs_req = our_req(req);
	int ret;

	/* if the length is zero, ignore the DMA data */
	if (hs_req->req.length == 0)
		return 0;

	ret = usb_gadget_map_request(&dwc2->gadget, req, hs_ep->dir_in);
	if (ret)
		goto dma_error;

	return 0;

dma_error:
	dev_err(dwc2->dev, "%s: failed to map buffer %p, %d bytes\n",
		__func__, req->buf, req->length);

	return -EIO;
}

static int s3c_hsotg_ep_queue(struct usb_ep *ep, struct usb_request *req,
			      gfp_t gfp_flags)
{
	struct s3c_hsotg_req *hs_req = our_req(req);
	struct s3c_hsotg_ep *hs_ep = our_ep(ep);
	struct dwc2_hsotg *dwc2 = hs_ep->parent;
	bool first;

	dev_dbg(dwc2->dev, "%s: req %p: %d@%p, noi=%d, zero=%d, snok=%d\n",
		ep->name, req, req->length, req->buf, req->no_interrupt,
		req->zero, req->short_not_ok);

	/* initialise status of the request */
	INIT_LIST_HEAD(&hs_req->queue);
	req->actual = 0;
	req->status = -EINPROGRESS;

	/* if we're using DMA, sync the buffers as necessary */
	if (using_dma(dwc2->s3c_hsotg)) {
		int ret = s3c_hsotg_map_dma(dwc2, hs_ep, req);
		if (ret)
			return ret;
	}

	first = list_empty(&hs_ep->queue);
	list_add_tail(&hs_req->queue, &hs_ep->queue);

	if (first)
		s3c_hsotg_start_req(dwc2, hs_ep, hs_req, false);

	return 0;
}

static int s3c_hsotg_ep_queue_lock(struct usb_ep *ep, struct usb_request *req,
			      gfp_t gfp_flags)
{
	struct s3c_hsotg_ep *hs_ep = our_ep(ep);
	struct dwc2_hsotg *dwc2 = hs_ep->parent;
	unsigned long flags = 0;
	int ret = 0;

	spin_lock_irqsave(&dwc2->lock, flags);
	ret = s3c_hsotg_ep_queue(ep, req, gfp_flags);
	spin_unlock_irqrestore(&dwc2->lock, flags);

	return ret;
}

static void s3c_hsotg_ep_free_request(struct usb_ep *ep,
				      struct usb_request *req)
{
	struct s3c_hsotg_req *hs_req = our_req(req);

	kfree(hs_req);
}

/**
 * s3c_hsotg_complete_oursetup - setup completion callback
 * @ep: The endpoint the request was on.
 * @req: The request completed.
 *
 * Called on completion of any requests the driver itself
 * submitted that need cleaning up.
 */
static void s3c_hsotg_complete_oursetup(struct usb_ep *ep,
					struct usb_request *req)
{
	struct s3c_hsotg_ep *hs_ep = our_ep(ep);
	struct dwc2_hsotg *dwc2 = hs_ep->parent;

	dev_dbg(dwc2->dev, "%s: ep %p, req %p\n", __func__, ep, req);

	s3c_hsotg_ep_free_request(ep, req);
}

/**
 * ep_from_windex - convert control wIndex value to endpoint
 * @hsotg: The driver state.
 * @windex: The control request wIndex field (in host order).
 *
 * Convert the given wIndex into a pointer to an driver endpoint
 * structure, or return NULL if it is not a valid endpoint.
 */
static struct s3c_hsotg_ep *ep_from_windex(struct dwc2_hsotg *dwc2,
					   u32 windex)
{
	struct s3c_hsotg_ep *ep = &dwc2->eps[windex & 0x7F];
	int dir = (windex & USB_DIR_IN) ? 1 : 0;
	int idx = windex & 0x7F;

	if (windex >= 0x100)
		return NULL;

	if (idx > dwc2->s3c_hsotg->num_of_eps)
		return NULL;

	if (idx && ep->dir_in != dir)
		return NULL;

	return ep;
}

/**
 * s3c_hsotg_send_reply - send reply to control request
 * @hsotg: The device state
 * @ep: Endpoint 0
 * @buff: Buffer for request
 * @length: Length of reply.
 *
 * Create a request and queue it on the given endpoint. This is useful as
 * an internal method of sending replies to certain control requests, etc.
 */
static int s3c_hsotg_send_reply(struct dwc2_hsotg *dwc2,
				struct s3c_hsotg_ep *ep,
				void *buff,
				int length)
{
	struct usb_request *req;
	int ret;

	dev_dbg(dwc2->dev, "%s: buff %p, len %d\n", __func__, buff, length);

	req = s3c_hsotg_ep_alloc_request(&ep->ep, GFP_ATOMIC);
	dwc2->s3c_hsotg->ep0_reply = req;
	if (!req) {
		dev_warn(dwc2->dev, "%s: cannot alloc req\n", __func__);
		return -ENOMEM;
	}

	req->buf = dwc2->s3c_hsotg->ep0_buff;
	req->length = length;
	req->zero = 1; /* always do zero-length final transfer */
	req->complete = s3c_hsotg_complete_oursetup;

	if (length)
		memcpy(req->buf, buff, length);
	else
		ep->sent_zlp = 1;

	ret = s3c_hsotg_ep_queue(&ep->ep, req, GFP_ATOMIC);
	if (ret) {
		dev_warn(dwc2->dev, "%s: cannot queue req\n", __func__);
		return ret;
	}

	return 0;
}

/**
 * s3c_hsotg_process_req_status - process request GET_STATUS
 * @hsotg: The device state
 * @ctrl: USB control request
 */
static int s3c_hsotg_process_req_status(struct dwc2_hsotg *dwc2,
					struct usb_ctrlrequest *ctrl)
{
	struct s3c_hsotg_ep *ep0 = &dwc2->eps[0];
	struct s3c_hsotg_ep *ep;
	__le16 reply;
	int ret;

	dev_dbg(dwc2->dev, "%s: USB_REQ_GET_STATUS\n", __func__);

	if (!ep0->dir_in) {
		dev_warn(dwc2->dev, "%s: direction out?\n", __func__);
		return -EINVAL;
	}

	switch (ctrl->bRequestType & USB_RECIP_MASK) {
	case USB_RECIP_DEVICE:
		reply = cpu_to_le16(0); /* bit 0 => self powered,
					 * bit 1 => remote wakeup */
		break;

	case USB_RECIP_INTERFACE:
		/* currently, the data result should be zero */
		reply = cpu_to_le16(0);
		break;

	case USB_RECIP_ENDPOINT:
		ep = ep_from_windex(dwc2, le16_to_cpu(ctrl->wIndex));
		if (!ep)
			return -ENOENT;

		reply = cpu_to_le16(ep->halted ? 1 : 0);
		break;

	default:
		return 0;
	}

	if (le16_to_cpu(ctrl->wLength) != 2)
		return -EINVAL;

	ret = s3c_hsotg_send_reply(dwc2, ep0, &reply, 2);
	if (ret) {
		dev_err(dwc2->dev, "%s: failed to send reply\n", __func__);
		return ret;
	}

	return 1;
}

static int s3c_hsotg_ep_sethalt(struct usb_ep *ep, int value);

/**
 * get_ep_head - return the first request on the endpoint
 * @hs_ep: The controller endpoint to get
 *
 * Get the first request on the endpoint.
 */
static struct s3c_hsotg_req *get_ep_head(struct s3c_hsotg_ep *hs_ep)
{
	if (list_empty(&hs_ep->queue))
		return NULL;

	return list_first_entry(&hs_ep->queue, struct s3c_hsotg_req, queue);
}

/**
 * s3c_hsotg_process_req_featire - process request {SET,CLEAR}_FEATURE
 * @hsotg: The device state
 * @ctrl: USB control request
 */
static int s3c_hsotg_process_req_feature(struct dwc2_hsotg *dwc2,
					 struct usb_ctrlrequest *ctrl)
{
	struct s3c_hsotg_ep *ep0 = &dwc2->eps[0];
	struct s3c_hsotg_req *hs_req;
	bool restart;
	bool set = (ctrl->bRequest == USB_REQ_SET_FEATURE);
	struct s3c_hsotg_ep *ep;
	int ret;

	dev_dbg(dwc2->dev, "%s: %s_FEATURE\n",
		__func__, set ? "SET" : "CLEAR");

	if (ctrl->bRequestType == USB_RECIP_ENDPOINT) {
		ep = ep_from_windex(dwc2, le16_to_cpu(ctrl->wIndex));
		if (!ep) {
			dev_dbg(dwc2->dev, "%s: no endpoint for 0x%04x\n",
				__func__, le16_to_cpu(ctrl->wIndex));
			return -ENOENT;
		}

		switch (le16_to_cpu(ctrl->wValue)) {
		case USB_ENDPOINT_HALT:
			s3c_hsotg_ep_sethalt(&ep->ep, set);

			ret = s3c_hsotg_send_reply(dwc2, ep0, NULL, 0);
			if (ret) {
				dev_err(dwc2->dev,
					"%s: failed to send reply\n", __func__);
				return ret;
			}

			if (!set) {
				/*
				 * If we have request in progress,
				 * then complete it
				 */
				if (ep->req) {
					hs_req = ep->req;
					ep->req = NULL;
					list_del_init(&hs_req->queue);
					hs_req->req.complete(&ep->ep,
							     &hs_req->req);
				}

				/* If we have pending request, then start it */
				restart = !list_empty(&ep->queue);
				if (restart) {
					hs_req = get_ep_head(ep);
					s3c_hsotg_start_req(dwc2, ep,
							    hs_req, false);
				}
			}

			break;

		default:
			return -ENOENT;
		}
	} else
		return -ENOENT;  /* currently only deal with endpoint */

	return 1;
}

/**
 * s3c_hsotg_process_control - process a control request
 * @hsotg: The device state
 * @ctrl: The control request received
 *
 * The controller has received the SETUP phase of a control request, and
 * needs to work out what to do next (and whether to pass it on to the
 * gadget driver).
 */
static void s3c_hsotg_process_control(struct dwc2_hsotg *dwc2,
				      struct usb_ctrlrequest *ctrl)
{
	struct s3c_hsotg_ep *ep0 = &dwc2->eps[0];
	int ret = 0;
	u32 dcfg;

	ep0->sent_zlp = 0;

	dev_dbg(dwc2->dev, "ctrl Req=%02x, Type=%02x, V=%04x, L=%04x\n",
		 ctrl->bRequest, ctrl->bRequestType,
		 ctrl->wValue, ctrl->wLength);

	/*
	 * record the direction of the request, for later use when enquing
	 * packets onto EP0.
	 */

	ep0->dir_in = (ctrl->bRequestType & USB_DIR_IN) ? 1 : 0;
	dev_dbg(dwc2->dev, "ctrl: dir_in=%d\n", ep0->dir_in);

	/*
	 * if we've no data with this request, then the last part of the
	 * transaction is going to implicitly be IN.
	 */
	if (ctrl->wLength == 0)
		ep0->dir_in = 1;

	if ((ctrl->bRequestType & USB_TYPE_MASK) == USB_TYPE_STANDARD) {
		switch (ctrl->bRequest) {
		case USB_REQ_SET_ADDRESS:
			dcfg = readl(dwc2->regs + DCFG);
			dcfg &= ~DCFG_DEVADDR_MASK;
			dcfg |= ctrl->wValue << DCFG_DEVADDR_SHIFT;
			writel(dcfg, dwc2->regs + DCFG);

			dev_info(dwc2->dev, "new address %d\n", ctrl->wValue);

			ret = s3c_hsotg_send_reply(dwc2, ep0, NULL, 0);
			return;

		case USB_REQ_GET_STATUS:
			ret = s3c_hsotg_process_req_status(dwc2, ctrl);
			break;

		case USB_REQ_CLEAR_FEATURE:
		case USB_REQ_SET_FEATURE:
			ret = s3c_hsotg_process_req_feature(dwc2, ctrl);
			break;
		}
	}

	/* as a fallback, try delivering it to the driver to deal with */

	if (ret == 0 && dwc2->driver) {
		ret = dwc2->driver->setup(&dwc2->gadget, ctrl);
		if (ret < 0)
			dev_dbg(dwc2->dev, "driver->setup() ret %d\n", ret);
	}

	/*
	 * the request is either unhandlable, or is not formatted correctly
	 * so respond with a STALL for the status stage to indicate failure.
	 */

	if (ret < 0) {
		u32 reg;
		u32 ctrl;

		dev_dbg(dwc2->dev, "ep0 stall (dir=%d)\n", ep0->dir_in);
		reg = (ep0->dir_in) ? DIEPCTL0 : DOEPCTL0;

		/*
		 * DXEPCTL_Stall will be cleared by EP once it has
		 * taken effect, so no need to clear later.
		 */

		ctrl = readl(dwc2->regs + reg);
		ctrl |= DXEPCTL_STALL;
		ctrl |= DXEPCTL_CNAK;
		writel(ctrl, dwc2->regs + reg);

		dev_dbg(dwc2->dev,
			"written DXEPCTL=0x%08x to %08x (DXEPCTL=0x%08x)\n",
			ctrl, reg, readl(dwc2->regs + reg));

		/*
		 * don't believe we need to anything more to get the EP
		 * to reply with a STALL packet
		 */
	}
}

static void s3c_hsotg_enqueue_setup(struct dwc2_hsotg *dwc2);

/**
 * s3c_hsotg_complete_setup - completion of a setup transfer
 * @ep: The endpoint the request was on.
 * @req: The request completed.
 *
 * Called on completion of any requests the driver itself submitted for
 * EP0 setup packets
 */
static void s3c_hsotg_complete_setup(struct usb_ep *ep,
				     struct usb_request *req)
{
	struct s3c_hsotg_ep *hs_ep = our_ep(ep);
	struct dwc2_hsotg *dwc2 = hs_ep->parent;

	if (req->status < 0) {
		dev_dbg(dwc2->dev, "%s: failed %d\n", __func__, req->status);
		return;
	}

	if (req->actual == 0)
		s3c_hsotg_enqueue_setup(dwc2);
	else
		s3c_hsotg_process_control(dwc2, req->buf);
}

/**
 * s3c_hsotg_enqueue_setup - start a request for EP0 packets
 * @hsotg: The device state.
 *
 * Enqueue a request on EP0 if necessary to received any SETUP packets
 * received from the host.
 */
static void s3c_hsotg_enqueue_setup(struct dwc2_hsotg *dwc2)
{
	struct usb_request *req = dwc2->s3c_hsotg->ctrl_req;
	struct s3c_hsotg_req *hs_req = our_req(req);
	int ret;

	dev_dbg(dwc2->dev, "%s: queueing setup request\n", __func__);

	req->zero = 0;
	req->length = 8;
	req->buf = dwc2->s3c_hsotg->ctrl_buff;
	req->complete = s3c_hsotg_complete_setup;

	if (!list_empty(&hs_req->queue)) {
		dev_dbg(dwc2->dev, "%s already queued???\n", __func__);
		return;
	}

	dwc2->eps[0].dir_in = 0;

	ret = s3c_hsotg_ep_queue(&dwc2->eps[0].ep, req, GFP_ATOMIC);
	if (ret < 0) {
		dev_err(dwc2->dev, "%s: failed queue (%d)\n", __func__, ret);
		/*
		 * Don't think there's much we can do other than watch the
		 * driver fail.
		 */
	}
}

/**
 * s3c_hsotg_complete_request - complete a request given to us
 * @hsotg: The device state.
 * @hs_ep: The endpoint the request was on.
 * @hs_req: The request to complete.
 * @result: The result code (0 => Ok, otherwise errno)
 *
 * The given request has finished, so call the necessary completion
 * if it has one and then look to see if we can start a new request
 * on the endpoint.
 *
 * Note, expects the ep to already be locked as appropriate.
 */
static void s3c_hsotg_complete_request(struct dwc2_hsotg *dwc2,
				       struct s3c_hsotg_ep *hs_ep,
				       struct s3c_hsotg_req *hs_req,
				       int result)
{
	bool restart;

	if (!hs_req) {
		dev_dbg(dwc2->dev, "%s: nothing to complete?\n", __func__);
		return;
	}

	dev_dbg(dwc2->dev, "complete: ep %p %s, req %p, %d => %p\n",
		hs_ep, hs_ep->ep.name, hs_req, result, hs_req->req.complete);

	/*
	 * only replace the status if we've not already set an error
	 * from a previous transaction
	 */

	if (hs_req->req.status == -EINPROGRESS)
		hs_req->req.status = result;

	hs_ep->req = NULL;
	list_del_init(&hs_req->queue);

	if (using_dma(dwc2->s3c_hsotg))
		s3c_hsotg_unmap_dma(dwc2, hs_ep, hs_req);

	/*
	 * call the complete request with the locks off, just in case the
	 * request tries to queue more work for this endpoint.
	 */

	if (hs_req->req.complete) {
		spin_unlock(&dwc2->lock);
		hs_req->req.complete(&hs_ep->ep, &hs_req->req);
		spin_lock(&dwc2->lock);
	}

	/*
	 * Look to see if there is anything else to do. Note, the completion
	 * of the previous request may have caused a new request to be started
	 * so be careful when doing this.
	 */

	if (!hs_ep->req && result >= 0) {
		restart = !list_empty(&hs_ep->queue);
		if (restart) {
			hs_req = get_ep_head(hs_ep);
			s3c_hsotg_start_req(dwc2, hs_ep, hs_req, false);
		}
	}
}

/**
 * s3c_hsotg_rx_data - receive data from the FIFO for an endpoint
 * @hsotg: The device state.
 * @ep_idx: The endpoint index for the data
 * @size: The size of data in the fifo, in bytes
 *
 * The FIFO status shows there is data to read from the FIFO for a given
 * endpoint, so sort out whether we need to read the data into a request
 * that has been made for that endpoint.
 */
static void s3c_hsotg_rx_data(struct dwc2_hsotg *dwc2, int ep_idx, int size)
{
	struct s3c_hsotg_ep *hs_ep = &dwc2->eps[ep_idx];
	struct s3c_hsotg_req *hs_req = hs_ep->req;
	void __iomem *fifo = dwc2->regs + EPFIFO(ep_idx);
	int to_read;
	int max_req;
	int read_ptr;


	if (!hs_req) {
		u32 epctl = readl(dwc2->regs + DOEPCTL(ep_idx));
		int ptr;

		dev_warn(dwc2->dev,
			 "%s: FIFO %d bytes on ep%d but no req (DXEPCTl=0x%08x)\n",
			 __func__, size, ep_idx, epctl);

		/* dump the data from the FIFO, we've nothing we can do */
		for (ptr = 0; ptr < size; ptr += 4)
			(void)readl(fifo);

		return;
	}

	to_read = size;
	read_ptr = hs_req->req.actual;
	max_req = hs_req->req.length - read_ptr;

	dev_dbg(dwc2->dev, "%s: read %d/%d, done %d/%d\n",
		__func__, to_read, max_req, read_ptr, hs_req->req.length);

	if (to_read > max_req) {
		/*
		 * more data appeared than we where willing
		 * to deal with in this request.
		 */

		/* currently we don't deal this */
		WARN_ON_ONCE(1);
	}

	hs_ep->total_data += to_read;
	hs_req->req.actual += to_read;
	to_read = DIV_ROUND_UP(to_read, 4);

	/*
	 * note, we might over-write the buffer end by 3 bytes depending on
	 * alignment of the data.
	 */
	readsl(fifo, hs_req->req.buf + read_ptr, to_read);
}

/**
 * s3c_hsotg_send_zlp - send zero-length packet on control endpoint
 * @hsotg: The device instance
 * @req: The request currently on this endpoint
 *
 * Generate a zero-length IN packet request for terminating a SETUP
 * transaction.
 *
 * Note, since we don't write any data to the TxFIFO, then it is
 * currently believed that we do not need to wait for any space in
 * the TxFIFO.
 */
static void s3c_hsotg_send_zlp(struct dwc2_hsotg *dwc2,
			       struct s3c_hsotg_req *req)
{
	u32 ctrl;

	if (!req) {
		dev_warn(dwc2->dev, "%s: no request?\n", __func__);
		return;
	}

	if (req->req.length == 0) {
		dwc2->eps[0].sent_zlp = 1;
		s3c_hsotg_enqueue_setup(dwc2);
		return;
	}

	dwc2->eps[0].dir_in = 1;
	dwc2->eps[0].sent_zlp = 1;

	dev_dbg(dwc2->dev, "sending zero-length packet\n");

	/* issue a zero-sized packet to terminate this */
	writel(DXEPTSIZ_MC(1) | DXEPTSIZ_PKTCNT(1) |
	       DXEPTSIZ_XFERSIZE(0), dwc2->regs + DIEPTSIZ(0));

	ctrl = readl(dwc2->regs + DIEPCTL0);
	ctrl |= DXEPCTL_CNAK;  /* clear NAK set by core */
	ctrl |= DXEPCTL_EPENA; /* ensure ep enabled */
	ctrl |= DXEPCTL_USBACTEP;
	writel(ctrl, dwc2->regs + DIEPCTL0);
}

/**
 * s3c_hsotg_handle_outdone - handle receiving OutDone/SetupDone from RXFIFO
 * @hsotg: The device instance
 * @epnum: The endpoint received from
 * @was_setup: Set if processing a SetupDone event.
 *
 * The RXFIFO has delivered an OutDone event, which means that the data
 * transfer for an OUT endpoint has been completed, either by a short
 * packet or by the finish of a transfer.
 */
static void s3c_hsotg_handle_outdone(struct dwc2_hsotg *dwc2,
				     int epnum, bool was_setup)
{
	u32 epsize = readl(dwc2->regs + DOEPTSIZ(epnum));
	struct s3c_hsotg_ep *hs_ep = &dwc2->eps[epnum];
	struct s3c_hsotg_req *hs_req = hs_ep->req;
	struct usb_request *req = &hs_req->req;
	unsigned size_left = DXEPTSIZ_XFERSIZE_GET(epsize);
	int result = 0;

	if (!hs_req) {
		dev_dbg(dwc2->dev, "%s: no request active\n", __func__);
		return;
	}

	if (using_dma(dwc2->s3c_hsotg)) {
		unsigned size_done;

		/*
		 * Calculate the size of the transfer by checking how much
		 * is left in the endpoint size register and then working it
		 * out from the amount we loaded for the transfer.
		 *
		 * We need to do this as DMA pointers are always 32bit aligned
		 * so may overshoot/undershoot the transfer.
		 */

		size_done = hs_ep->size_loaded - size_left;
		size_done += hs_ep->last_load;

		req->actual = size_done;
	}

	/* if there is more request to do, schedule new transfer */
	if (req->actual < req->length && size_left == 0) {
		s3c_hsotg_start_req(dwc2, hs_ep, hs_req, true);
		return;
	} else if (epnum == 0) {
		/*
		 * After was_setup = 1 =>
		 * set CNAK for non Setup requests
		 */
		dwc2->s3c_hsotg->setup = was_setup ? 0 : 1;
	}

	if (req->actual < req->length && req->short_not_ok) {
		dev_dbg(dwc2->dev, "%s: got %d/%d (short not ok) => error\n",
			__func__, req->actual, req->length);

		/*
		 * todo - what should we return here? there's no one else
		 * even bothering to check the status.
		 */
	}

	if (epnum == 0) {
		/*
		 * Condition req->complete != s3c_hsotg_complete_setup says:
		 * send ZLP when we have an asynchronous request from gadget
		 */
		if (!was_setup && req->complete != s3c_hsotg_complete_setup)
			s3c_hsotg_send_zlp(dwc2, hs_req);
	}

	s3c_hsotg_complete_request(dwc2, hs_ep, hs_req, result);
}

/**
 * s3c_hsotg_read_frameno - read current frame number
 * @hsotg: The device instance
 *
 * Return the current frame number
 */
static u32 s3c_hsotg_read_frameno(struct dwc2_hsotg *dwc2)
{
	u32 dsts;

	dsts = readl(dwc2->regs + DSTS);
	dsts &= DSTS_SOFFN_MASK;
	dsts >>= DSTS_SOFFN_SHIFT;

	return dsts;
}

/**
 * s3c_hsotg_handle_rx - RX FIFO has data
 * @hsotg: The device instance
 *
 * The IRQ handler has detected that the RX FIFO has some data in it
 * that requires processing, so find out what is in there and do the
 * appropriate read.
 *
 * The RXFIFO is a true FIFO, the packets coming out are still in packet
 * chunks, so if you have x packets received on an endpoint you'll get x
 * FIFO events delivered, each with a packet's worth of data in it.
 *
 * When using DMA, we should not be processing events from the RXFIFO
 * as the actual data should be sent to the memory directly and we turn
 * on the completion interrupts to get notifications of transfer completion.
 */
void s3c_hsotg_handle_rx(struct dwc2_hsotg *dwc2)
{
	u32 grxstsr = readl(dwc2->regs + GRXSTSP);
	u32 epnum, status, size;

	WARN_ON(using_dma(dwc2->s3c_hsotg));

	epnum = grxstsr & GRXSTS_EPNUM_MASK;
	status = grxstsr & GRXSTS_PKTSTS_MASK;

	size = grxstsr & GRXSTS_BYTECNT_MASK;
	size >>= GRXSTS_BYTECNT_SHIFT;

	if (1)
		dev_dbg(dwc2->dev, "%s: GRXSTSP=0x%08x (%d@%d)\n",
			__func__, grxstsr, size, epnum);

#define __status(x) ((x) >> GRXSTS_PKTSTS_SHIFT)

	switch (status >> GRXSTS_PKTSTS_SHIFT) {
	case __status(GRXSTS_PKTSTS_GLOBALOUTNAK):
		dev_dbg(dwc2->dev, "GLOBALOUTNAK\n");
		break;

	case __status(GRXSTS_PKTSTS_OUTDONE):
		dev_dbg(dwc2->dev, "OutDone (Frame=0x%08x)\n",
			s3c_hsotg_read_frameno(dwc2));

		if (!using_dma(dwc2->s3c_hsotg))
			s3c_hsotg_handle_outdone(dwc2, epnum, false);
		break;

	case __status(GRXSTS_PKTSTS_SETUPDONE):
		dev_dbg(dwc2->dev,
			"SetupDone (Frame=0x%08x, DOPEPCTL=0x%08x)\n",
			s3c_hsotg_read_frameno(dwc2),
			readl(dwc2->regs + DOEPCTL(0)));

		s3c_hsotg_handle_outdone(dwc2, epnum, true);
		break;

	case __status(GRXSTS_PKTSTS_OUTRX):
		s3c_hsotg_rx_data(dwc2, epnum, size);
		break;

	case __status(GRXSTS_PKTSTS_SETUPRX):
		dev_dbg(dwc2->dev,
			"SetupRX (Frame=0x%08x, DOPEPCTL=0x%08x)\n",
			s3c_hsotg_read_frameno(dwc2),
			readl(dwc2->regs + DOEPCTL(0)));

		s3c_hsotg_rx_data(dwc2, epnum, size);
		break;

	default:
		dev_warn(dwc2->dev, "%s: unknown status %08x\n",
			 __func__, grxstsr);

		s3c_hsotg_dump(dwc2);
		break;
	}
}

/**
 * s3c_hsotg_ep0_mps - turn max packet size into register setting
 * @mps: The maximum packet size in bytes.
 */
static u32 s3c_hsotg_ep0_mps(unsigned int mps)
{
	switch (mps) {
	case 64:
		return D0EPCTL_MPS_64;
	case 32:
		return D0EPCTL_MPS_32;
	case 16:
		return D0EPCTL_MPS_16;
	case 8:
		return D0EPCTL_MPS_8;
	}

	/* bad max packet size, warn and return invalid result */
	WARN_ON(1);
	return (u32)-1;
}

/**
 * s3c_hsotg_set_ep_maxpacket - set endpoint's max-packet field
 * @hsotg: The driver state.
 * @ep: The index number of the endpoint
 * @mps: The maximum packet size in bytes
 *
 * Configure the maximum packet size for the given endpoint, updating
 * the hardware control registers to reflect this.
 */
static void s3c_hsotg_set_ep_maxpacket(struct dwc2_hsotg *dwc2,
				       unsigned int ep, unsigned int mps)
{
	struct s3c_hsotg_ep *hs_ep = &dwc2->eps[ep];
	void __iomem *regs = dwc2->regs;
	u32 mpsval;
	u32 reg;

	if (ep == 0) {
		/* EP0 is a special case */
		mpsval = s3c_hsotg_ep0_mps(mps);
		if (mpsval > 3)
			goto bad_mps;
	} else {
		if (mps >= DXEPCTL_MPS_LIMIT+1)
			goto bad_mps;

		mpsval = mps;
	}

	hs_ep->ep.maxpacket = mps;

	/*
	 * update both the in and out endpoint controldir_ registers, even
	 * if one of the directions may not be in use.
	 */

	reg = readl(regs + DIEPCTL(ep));
	reg &= ~DXEPCTL_MPS_MASK;
	reg |= mpsval;
	writel(reg, regs + DIEPCTL(ep));

	if (ep) {
		reg = readl(regs + DOEPCTL(ep));
		reg &= ~DXEPCTL_MPS_MASK;
		reg |= mpsval;
		writel(reg, regs + DOEPCTL(ep));
	}

	return;

bad_mps:
	dev_err(dwc2->dev, "ep%d: bad mps of %d\n", ep, mps);
}

/**
 * s3c_hsotg_txfifo_flush - flush Tx FIFO
 * @hsotg: The driver state
 * @idx: The index for the endpoint (0..15)
 */
static void s3c_hsotg_txfifo_flush(struct dwc2_hsotg *dwc2, unsigned int idx)
{
	int timeout;
	int val;

	writel(GRSTCTL_TXFNUM(idx) | GRSTCTL_TXFFLSH,
		dwc2->regs + GRSTCTL);

	/* wait until the fifo is flushed */
	timeout = 100;

	while (1) {
		val = readl(dwc2->regs + GRSTCTL);

		if ((val & (GRSTCTL_TXFFLSH)) == 0)
			break;

		if (--timeout == 0) {
			dev_err(dwc2->dev,
				"%s: timeout flushing fifo (GRSTCTL=%08x)\n",
				__func__, val);
		}

		udelay(1);
	}
}

/**
 * s3c_hsotg_trytx - check to see if anything needs transmitting
 * @hsotg: The driver state
 * @hs_ep: The driver endpoint to check.
 *
 * Check to see if there is a request that has data to send, and if so
 * make an attempt to write data into the FIFO.
 */
static int s3c_hsotg_trytx(struct dwc2_hsotg *dwc2,
			   struct s3c_hsotg_ep *hs_ep)
{
	struct s3c_hsotg_req *hs_req = hs_ep->req;

	if (!hs_ep->dir_in || !hs_req)
		return 0;

	if (hs_req->req.actual < hs_req->req.length) {
		dev_dbg(dwc2->dev, "trying to write more for ep%d\n",
			hs_ep->index);
		return s3c_hsotg_write_fifo(dwc2, hs_ep, hs_req);
	}

	return 0;
}

/**
 * s3c_hsotg_complete_in - complete IN transfer
 * @hsotg: The device state.
 * @hs_ep: The endpoint that has just completed.
 *
 * An IN transfer has been completed, update the transfer's state and then
 * call the relevant completion routines.
 */
static void s3c_hsotg_complete_in(struct dwc2_hsotg *dwc2,
				  struct s3c_hsotg_ep *hs_ep)
{
	struct s3c_hsotg_req *hs_req = hs_ep->req;
	u32 epsize = readl(dwc2->regs + DIEPTSIZ(hs_ep->index));
	int size_left, size_done;

	if (!hs_req) {
		dev_dbg(dwc2->dev, "XferCompl but no req\n");
		return;
	}

	/* Finish ZLP handling for IN EP0 transactions */
	if (dwc2->eps[0].sent_zlp) {
		dev_dbg(dwc2->dev, "zlp packet received\n");
		s3c_hsotg_complete_request(dwc2, hs_ep, hs_req, 0);
		return;
	}

	/*
	 * Calculate the size of the transfer by checking how much is left
	 * in the endpoint size register and then working it out from
	 * the amount we loaded for the transfer.
	 *
	 * We do this even for DMA, as the transfer may have incremented
	 * past the end of the buffer (DMA transfers are always 32bit
	 * aligned).
	 */

	size_left = DXEPTSIZ_XFERSIZE_GET(epsize);

	size_done = hs_ep->size_loaded - size_left;
	size_done += hs_ep->last_load;

	if (hs_req->req.actual != size_done)
		dev_dbg(dwc2->dev, "%s: adjusting size done %d => %d\n",
			__func__, hs_req->req.actual, size_done);

	hs_req->req.actual = size_done;
	dev_dbg(dwc2->dev, "req->length:%d req->actual:%d req->zero:%d\n",
		hs_req->req.length, hs_req->req.actual, hs_req->req.zero);

	/*
	 * Check if dealing with Maximum Packet Size(MPS) IN transfer at EP0
	 * When sent data is a multiple MPS size (e.g. 64B ,128B ,192B
	 * ,256B ... ), after last MPS sized packet send IN ZLP packet to
	 * inform the host that no more data is available.
	 * The state of req.zero member is checked to be sure that the value to
	 * send is smaller than wValue expected from host.
	 * Check req.length to NOT send another ZLP when the current one is
	 * under completion (the one for which this completion has been called).
	 */
	if (hs_req->req.length && hs_ep->index == 0 && hs_req->req.zero &&
	    hs_req->req.length == hs_req->req.actual &&
	    !(hs_req->req.length % hs_ep->ep.maxpacket)) {

		dev_dbg(dwc2->dev, "ep0 zlp IN packet sent\n");
		s3c_hsotg_send_zlp(dwc2, hs_req);

		return;
	}

	if (!size_left && hs_req->req.actual < hs_req->req.length) {
		dev_dbg(dwc2->dev, "%s trying more for req...\n", __func__);
		s3c_hsotg_start_req(dwc2, hs_ep, hs_req, true);
	} else
		s3c_hsotg_complete_request(dwc2, hs_ep, hs_req, 0);
}

/**
 * s3c_hsotg_epint - handle an in/out endpoint interrupt
 * @hsotg: The driver state
 * @idx: The index for the endpoint (0..15)
 * @dir_in: Set if this is an IN endpoint
 *
 * Process and clear any interrupt pending for an individual endpoint
 */
void s3c_hsotg_epint(struct dwc2_hsotg *dwc2, unsigned int idx,
			    int dir_in)
{
	struct s3c_hsotg_ep *hs_ep = &dwc2->eps[idx];
	u32 epint_reg = dir_in ? DIEPINT(idx) : DOEPINT(idx);
	u32 epctl_reg = dir_in ? DIEPCTL(idx) : DOEPCTL(idx);
	u32 epsiz_reg = dir_in ? DIEPTSIZ(idx) : DOEPTSIZ(idx);
	u32 ints;

	ints = readl(dwc2->regs + epint_reg);

	/* Clear endpoint interrupts */
	writel(ints, dwc2->regs + epint_reg);

	dev_dbg(dwc2->dev, "%s: ep%d(%s) DXEPINT=0x%08x\n",
		__func__, idx, dir_in ? "in" : "out", ints);

	if (ints & DXEPINT_XFERCOMPL) {
		dev_dbg(dwc2->dev,
			"%s: XferCompl: DxEPCTL=0x%08x, DxEPTSIZ=%08x\n",
			__func__, readl(dwc2->regs + epctl_reg),
			readl(dwc2->regs + epsiz_reg));

		/*
		 * we get OutDone from the FIFO, so we only need to look
		 * at completing IN requests here
		 */
		if (dir_in) {
			s3c_hsotg_complete_in(dwc2, hs_ep);

			if (idx == 0 && !hs_ep->req)
				s3c_hsotg_enqueue_setup(dwc2);
		} else if (using_dma(dwc2->s3c_hsotg)) {
			/*
			 * We're using DMA, we need to fire an OutDone here
			 * as we ignore the RXFIFO.
			 */

			s3c_hsotg_handle_outdone(dwc2, idx, false);
		}
	}

	if (ints & DXEPINT_EPDISBLD) {
		dev_dbg(dwc2->dev, "%s: EPDisbld\n", __func__);

		if (dir_in) {
			int epctl = readl(dwc2->regs + epctl_reg);

			s3c_hsotg_txfifo_flush(dwc2, idx);

			if ((epctl & DXEPCTL_STALL) &&
				(epctl & DXEPCTL_EPTYPE_BULK)) {
				int dctl = readl(dwc2->regs + DCTL);

				dctl |= DCTL_CGNPINNAK;
				writel(dctl, dwc2->regs + DCTL);
			}
		}
	}

	if (ints & DXEPINT_AHBERR)
		dev_dbg(dwc2->dev, "%s: AHBErr\n", __func__);

	if (ints & DXEPINT_SETUP) {  /* Setup or Timeout */
		dev_dbg(dwc2->dev, "%s: Setup/Timeout\n",  __func__);

		if (using_dma(dwc2->s3c_hsotg) && idx == 0) {
			/*
			 * this is the notification we've received a
			 * setup packet. In non-DMA mode we'd get this
			 * from the RXFIFO, instead we need to process
			 * the setup here.
			 */

			if (dir_in)
				WARN_ON_ONCE(1);
			else
				s3c_hsotg_handle_outdone(dwc2, 0, true);
		}
	}

	if (ints & DXEPINT_BACK2BACKSETUP)
		dev_dbg(dwc2->dev, "%s: B2BSetup/INEPNakEff\n", __func__);

	if (dir_in) {
		/* not sure if this is important, but we'll clear it anyway */
		if (ints & DIEPMSK_INTKNTXFEMPMSK) {
			dev_dbg(dwc2->dev, "%s: ep%d: INTKNTXFEMPMSK\n",
				__func__, idx);
		}

		/* this probably means something bad is happening */
		if (ints & DIEPMSK_INTKNEPMISMSK) {
			dev_warn(dwc2->dev, "%s: ep%d: INTKNEP\n",
				 __func__, idx);
		}

		/* FIFO has space or is empty (see GAHBCFG) */
		if (dwc2->s3c_hsotg->dedicated_fifos &&
		    ints & DIEPMSK_TXFIFOEMPTY) {
			dev_dbg(dwc2->dev, "%s: ep%d: TXFIFOEMPTY\n",
				__func__, idx);
			if (!using_dma(dwc2->s3c_hsotg))
				s3c_hsotg_trytx(dwc2, hs_ep);
		}
	}
}

/**
 * s3c_hsotg_irq_enumdone - Handle EnumDone interrupt (enumeration done)
 * @hsotg: The device state.
 *
 * Handle updating the device settings after the enumeration phase has
 * been completed.
 */
void s3c_hsotg_irq_enumdone(struct dwc2_hsotg *dwc2)
{
	u32 dsts = readl(dwc2->regs + DSTS);
	int ep0_mps = 0, ep_mps;

	/*
	 * This should signal the finish of the enumeration phase
	 * of the USB handshaking, so we should now know what rate
	 * we connected at.
	 */

	dev_dbg(dwc2->dev, "EnumDone (DSTS=0x%08x)\n", dsts);

	/*
	 * note, since we're limited by the size of transfer on EP0, and
	 * it seems IN transfers must be a even number of packets we do
	 * not advertise a 64byte MPS on EP0.
	 */

	/* catch both EnumSpd_FS and EnumSpd_FS48 */
	switch (dsts & DSTS_ENUMSPD_MASK) {
	case DSTS_ENUMSPD_FS:
	case DSTS_ENUMSPD_FS48:
		dwc2->gadget.speed = USB_SPEED_FULL;
		ep0_mps = EP0_MPS_LIMIT;
		ep_mps = 64;
		break;

	case DSTS_ENUMSPD_HS:
		dwc2->gadget.speed = USB_SPEED_HIGH;
		ep0_mps = EP0_MPS_LIMIT;
		ep_mps = 512;
		break;

	case DSTS_ENUMSPD_LS:
		dwc2->gadget.speed = USB_SPEED_LOW;
		/*
		 * note, we don't actually support LS in this driver at the
		 * moment, and the documentation seems to imply that it isn't
		 * supported by the PHYs on some of the devices.
		 */
		break;
	}
	dev_info(dwc2->dev, "new device is %s\n",
		 usb_speed_string(dwc2->gadget.speed));

	/*
	 * we should now know the maximum packet size for an
	 * endpoint, so set the endpoints to a default value.
	 */

	if (ep0_mps) {
		int i;
		s3c_hsotg_set_ep_maxpacket(dwc2, 0, ep0_mps);
		for (i = 1; i < dwc2->s3c_hsotg->num_of_eps; i++)
			s3c_hsotg_set_ep_maxpacket(dwc2, i, ep_mps);
	}

	/* ensure after enumeration our EP0 is active */

	s3c_hsotg_enqueue_setup(dwc2);

	dev_dbg(dwc2->dev, "EP0: DIEPCTL0=0x%08x, DOEPCTL0=0x%08x\n",
		readl(dwc2->regs + DIEPCTL0),
		readl(dwc2->regs + DOEPCTL0));
}

/**
 * kill_all_requests - remove all requests from the endpoint's queue
 * @hsotg: The device state.
 * @ep: The endpoint the requests may be on.
 * @result: The result code to use.
 * @force: Force removal of any current requests
 *
 * Go through the requests on the given endpoint and mark them
 * completed with the given result code.
 */
void kill_all_requests(struct dwc2_hsotg *dwc2,
			      struct s3c_hsotg_ep *ep,
			      int result, bool force)
{
	struct s3c_hsotg_req *req, *treq;

	list_for_each_entry_safe(req, treq, &ep->queue, queue) {
		/*
		 * currently, we can't do much about an already
		 * running request on an in endpoint
		 */

		if (ep->req == req && ep->dir_in && !force)
			continue;

		s3c_hsotg_complete_request(dwc2, ep, req,
					   result);
	}
}

/**
 * s3c_hsotg_disconnect - disconnect service
 * @hsotg: The device state.
 *
 * The device has been disconnected. Remove all current
 * transactions and signal the gadget driver that this
 * has happened.
 */
void s3c_hsotg_disconnect(struct dwc2_hsotg *dwc2)
{
	unsigned ep;

	for (ep = 0; ep < dwc2->s3c_hsotg->num_of_eps; ep++)
		kill_all_requests(dwc2, &dwc2->eps[ep], -ESHUTDOWN, true);

	call_gadget(dwc2, disconnect);
}

/**
 * s3c_hsotg_irq_fifoempty - TX FIFO empty interrupt handler
 * @hsotg: The device state:
 * @periodic: True if this is a periodic FIFO interrupt
 */
void s3c_hsotg_irq_fifoempty(struct dwc2_hsotg *dwc2, bool periodic)
{
	struct s3c_hsotg_ep *ep;
	int epno, ret;

	/* look through for any more data to transmit */

	for (epno = 0; epno < dwc2->s3c_hsotg->num_of_eps; epno++) {
		ep = &dwc2->eps[epno];

		if (!ep->dir_in)
			continue;

		if ((periodic && !ep->periodic) ||
		    (!periodic && ep->periodic))
			continue;

		ret = s3c_hsotg_trytx(dwc2, ep);
		if (ret < 0)
			break;
	}
}

/* IRQ flags which will trigger a retry around the IRQ loop */
#define IRQ_RETRY_MASK (GINTSTS_NPTXFEMP | \
			GINTSTS_PTXFEMP |  \
			GINTSTS_RXFLVL)

/**
 * s3c_hsotg_corereset - issue softreset to the core
 * @hsotg: The device state
 *
 * Issue a soft reset to the core, and await the core finishing it.
 */
static int s3c_hsotg_corereset(struct dwc2_hsotg *dwc2)
{
	int timeout;
	u32 grstctl;

	dev_dbg(dwc2->dev, "resetting core\n");

	/* issue soft reset */
	writel(GRSTCTL_CSFTRST, dwc2->regs + GRSTCTL);

	timeout = 10000;
	do {
		grstctl = readl(dwc2->regs + GRSTCTL);
	} while ((grstctl & GRSTCTL_CSFTRST) && timeout-- > 0);

	if (grstctl & GRSTCTL_CSFTRST) {
		dev_err(dwc2->dev, "Failed to get CSftRst asserted\n");
		return -EINVAL;
	}

	timeout = 10000;

	while (1) {
		u32 grstctl = readl(dwc2->regs + GRSTCTL);

		if (timeout-- < 0) {
			dev_info(dwc2->dev,
				 "%s: reset failed, GRSTCTL=%08x\n",
				 __func__, grstctl);
			return -ETIMEDOUT;
		}

		if (!(grstctl & GRSTCTL_AHBIDLE))
			continue;

		break;		/* reset done */
	}

	dev_dbg(dwc2->dev, "reset successful\n");
	return 0;
}

/**
 * s3c_hsotg_core_init - issue softreset to the core
 * @hsotg: The device state
 *
 * Issue a soft reset to the core, and await the core finishing it.
 */
#if defined(CONFIG_USB_DWC2_DUAL_ROLE) || defined(CONFIG_USB_DWC2_GADGET)
void s3c_hsotg_core_init(struct dwc2_hsotg *dwc2)
{
	s3c_hsotg_corereset(dwc2);

	/*
	 * we must now enable ep0 ready for host detection and then
	 * set configuration.
	 */

	/* set the PLL on, remove the HNP/SRP and set the PHY */
	writel(GUSBCFG_PHYIF16 | GUSBCFG_TOUTCAL(7) |
	       (0x5 << 10), dwc2->regs + GUSBCFG);

	s3c_hsotg_init_fifo(dwc2);

	__orr32(dwc2->regs + DCTL, DCTL_SFTDISCON);

	writel(1 << 18 | DCFG_DEVSPD_HS,  dwc2->regs + DCFG);

	/* Clear any pending OTG interrupts */
	writel(0xffffffff, dwc2->regs + GOTGINT);

	/* Clear any pending interrupts */
	writel(0xffffffff, dwc2->regs + GINTSTS);

	writel(GINTSTS_ERLYSUSP | GINTSTS_SESSREQINT |
	       GINTSTS_GOUTNAKEFF | GINTSTS_GINNAKEFF |
	       GINTSTS_CONIDSTSCHNG | GINTSTS_USBRST |
	       GINTSTS_ENUMDONE | GINTSTS_OTGINT |
	       GINTSTS_USBSUSP | GINTSTS_WKUPINT,
	       dwc2->regs + GINTMSK);

	if (using_dma(dwc2->s3c_hsotg))
		writel(GAHBCFG_GLBL_INTR_EN | GAHBCFG_DMA_EN |
		       GAHBCFG_HBSTLEN_INCR4,
		       dwc2->regs + GAHBCFG);
	else
		writel(GAHBCFG_GLBL_INTR_EN, dwc2->regs + GAHBCFG);

	/*
	 * Enabling INTknTXFEmpMsk here seems to be a big mistake, we end
	 * up being flooded with interrupts if the host is polling the
	 * endpoint to try and read data.
	 */

	writel(((dwc2->s3c_hsotg->dedicated_fifos) ? DIEPMSK_TXFIFOEMPTY : 0) |
		DIEPMSK_EPDISBLDMSK | DIEPMSK_XFERCOMPLMSK |
		DIEPMSK_TIMEOUTMSK | DIEPMSK_AHBERRMSK |
		DIEPMSK_INTKNEPMISMSK,
		dwc2->regs + DIEPMSK);

	/*
	 * don't need XferCompl, we get that from RXFIFO in slave mode. In
	 * DMA mode we may need this.
	 */
	writel((using_dma(dwc2->s3c_hsotg) ? (DIEPMSK_XFERCOMPLMSK |
				    DIEPMSK_TIMEOUTMSK) : 0) |
	       DOEPMSK_EPDISBLDMSK | DOEPMSK_AHBERRMSK |
	       DOEPMSK_SETUPMSK,
	       dwc2->regs + DOEPMSK);

	writel(0, dwc2->regs + DAINTMSK);

	dev_dbg(dwc2->dev, "EP0: DIEPCTL0=0x%08x, DOEPCTL0=0x%08x\n",
		readl(dwc2->regs + DIEPCTL0),
		readl(dwc2->regs + DOEPCTL0));

	/* enable in and out endpoint interrupts */
	s3c_hsotg_en_gsint(dwc2, GINTSTS_OEPINT | GINTSTS_IEPINT);

	/*
	 * Enable the RXFIFO when in slave mode, as this is how we collect
	 * the data. In DMA mode, we get events from the FIFO but also
	 * things we cannot process, so do not use it.
	 */
	if (!using_dma(dwc2->s3c_hsotg))
		s3c_hsotg_en_gsint(dwc2, GINTSTS_RXFLVL);

	/* Enable interrupts for EP0 in and out */
	s3c_hsotg_ctrl_epint(dwc2, 0, 0, 1);
	s3c_hsotg_ctrl_epint(dwc2, 0, 1, 1);

	__orr32(dwc2->regs + DCTL, DCTL_PWRONPRGDONE);
	udelay(10);  /* see openiboot */
	__bic32(dwc2->regs + DCTL, DCTL_PWRONPRGDONE);

	dev_dbg(dwc2->dev, "DCTL=0x%08x\n", readl(dwc2->regs + DCTL));

	/*
	 * DXEPCTL_USBActEp says RO in manual, but seems to be set by
	 * writing to the EPCTL register..
	 */

	/* set to read 1 8byte packet */
	writel(DXEPTSIZ_MC(1) | DXEPTSIZ_PKTCNT(1) |
	       DXEPTSIZ_XFERSIZE(8), dwc2->regs + DOEPTSIZ0);

	writel(s3c_hsotg_ep0_mps(dwc2->eps[0].ep.maxpacket) |
	       DXEPCTL_CNAK | DXEPCTL_EPENA |
	       DXEPCTL_USBACTEP,
	       dwc2->regs + DOEPCTL0);

	/* enable, but don't activate EP0in */
	writel(s3c_hsotg_ep0_mps(dwc2->eps[0].ep.maxpacket) |
	       DXEPCTL_USBACTEP, dwc2->regs + DIEPCTL0);

	s3c_hsotg_enqueue_setup(dwc2);

	dev_dbg(dwc2->dev, "EP0: DIEPCTL0=0x%08x, DOEPCTL0=0x%08x\n",
		readl(dwc2->regs + DIEPCTL0),
		readl(dwc2->regs + DOEPCTL0));

	/* clear global NAKs */
	writel(DCTL_CGOUTNAK | DCTL_CGNPINNAK,
	       dwc2->regs + DCTL);

	/* must be at-least 3ms to allow bus to see disconnect */
	mdelay(3);

	/* remove the soft-disconnect and let's go */
	__bic32(dwc2->regs + DCTL, DCTL_SFTDISCON);
}
#else
void s3c_hsotg_core_init(struct dwc2_hsotg *dwc2)
{
}
#endif

/**
 * s3c_hsotg_ep_enable - enable the given endpoint
 * @ep: The USB endpint to configure
 * @desc: The USB endpoint descriptor to configure with.
 *
 * This is called from the USB gadget code's usb_ep_enable().
 */
static int s3c_hsotg_ep_enable(struct usb_ep *ep,
			       const struct usb_endpoint_descriptor *desc)
{
	struct s3c_hsotg_ep *hs_ep = our_ep(ep);
	struct dwc2_hsotg *dwc2 = hs_ep->parent;
	unsigned long flags;
	int index = hs_ep->index;
	u32 epctrl_reg;
	u32 epctrl;
	u32 mps;
	int dir_in;
	int ret = 0;

	/*dev_info(dwc2->dev,
		"%s: ep %s: a 0x%02x, attr 0x%02x, mps 0x%04x, intr %d\n",
		__func__, ep->name, desc->bEndpointAddress, desc->bmAttributes,
		desc->wMaxPacketSize, desc->bInterval);
	*/
	/* not to be called for EP0 */
	WARN_ON(index == 0);

	dir_in = (desc->bEndpointAddress & USB_ENDPOINT_DIR_MASK) ? 1 : 0;
	if (dir_in != hs_ep->dir_in) {
		dev_err(dwc2->dev, "%s: direction mismatch!\n", __func__);
		return -EINVAL;
	}

	mps = usb_endpoint_maxp(desc);

	/* note, we handle this here instead of s3c_hsotg_set_ep_maxpacket */

	epctrl_reg = dir_in ? DIEPCTL(index) : DOEPCTL(index);
	epctrl = readl(dwc2->regs + epctrl_reg);

	/*dev_info(dwc2->dev, "%s: read DXEPCTL=0x%08x from 0x%08x\n",
		__func__, epctrl, epctrl_reg);
	*/
	spin_lock_irqsave(&dwc2->lock, flags);

	epctrl &= ~(DXEPCTL_EPTYPE_MASK | DXEPCTL_MPS_MASK);
	epctrl |= DXEPCTL_MPS(mps);

	/*
	 * mark the endpoint as active, otherwise the core may ignore
	 * transactions entirely for this endpoint
	 */
	epctrl |= DXEPCTL_USBACTEP;

	/*
	 * set the NAK status on the endpoint, otherwise we might try and
	 * do something with data that we've yet got a request to process
	 * since the RXFIFO will take data for an endpoint even if the
	 * size register hasn't been set.
	 */

	epctrl |= DXEPCTL_SNAK;

	/* update the endpoint state */
	hs_ep->ep.maxpacket = mps;

	/* default, set to non-periodic */
	hs_ep->periodic = 0;

	switch (desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) {
	case USB_ENDPOINT_XFER_ISOC:
		dev_err(dwc2->dev, "no current ISOC support\n");
		ret = -EINVAL;
		goto out;

	case USB_ENDPOINT_XFER_BULK:
		epctrl |= DXEPCTL_EPTYPE_BULK;
		break;

	case USB_ENDPOINT_XFER_INT:
		if (dir_in) {
			/*
			 * Allocate our TxFNum by simply using the index
			 * of the endpoint for the moment. We could do
			 * something better if the host indicates how
			 * many FIFOs we are expecting to use.
			 */

			hs_ep->periodic = 1;
			epctrl |= DXEPCTL_TXFNUM(index);
		}

		epctrl |= DXEPCTL_EPTYPE_INTERRUPT;
		break;

	case USB_ENDPOINT_XFER_CONTROL:
		epctrl |= DXEPCTL_EPTYPE_CONTROL;
		break;
	}

	/*
	 * if the hardware has dedicated fifos, we must give each IN EP
	 * a unique tx-fifo even if it is non-periodic.
	 */
	if (dir_in && dwc2->s3c_hsotg->dedicated_fifos)
		epctrl |= DXEPCTL_TXFNUM(index);

	/* for non control endpoints, set PID to D0 */
	if (index)
		epctrl |= DXEPCTL_SETD0PID;

	/*dev_info(dwc2->dev, "%s: write DXEPCTL=0x%08x\n",
		__func__, epctrl);
	*/
	writel(epctrl, dwc2->regs + epctrl_reg);
	/*dev_info(dwc2->dev, "%s: read DXEPCTL=0x%08x\n",
		__func__, readl(dwc2->regs + epctrl_reg));
	*/
	/* enable the endpoint interrupt */
	s3c_hsotg_ctrl_epint(dwc2, index, dir_in, 1);

out:
	spin_unlock_irqrestore(&dwc2->lock, flags);
	return ret;
}

/**
 * s3c_hsotg_ep_disable - disable given endpoint
 * @ep: The endpoint to disable.
 */
static int s3c_hsotg_ep_disable(struct usb_ep *ep)
{
	struct s3c_hsotg_ep *hs_ep = our_ep(ep);
	struct dwc2_hsotg *dwc2 = hs_ep->parent;
	int dir_in = hs_ep->dir_in;
	int index = hs_ep->index;
	unsigned long flags;
	u32 epctrl_reg;
	u32 ctrl;

	dev_info(dwc2->dev, "%s(ep %p)\n", __func__, ep);

	if (ep == &dwc2->eps[0].ep) {
		dev_err(dwc2->dev, "%s: called for ep0\n", __func__);
		return -EINVAL;
	}

	epctrl_reg = dir_in ? DIEPCTL(index) : DOEPCTL(index);

	spin_lock_irqsave(&dwc2->lock, flags);
	/* terminate all requests with shutdown */
	kill_all_requests(dwc2, hs_ep, -ESHUTDOWN, false);


	ctrl = readl(dwc2->regs + epctrl_reg);
	ctrl &= ~DXEPCTL_EPENA;
	ctrl &= ~DXEPCTL_USBACTEP;
	ctrl |= DXEPCTL_SNAK;

	dev_dbg(dwc2->dev, "%s: DXEPCTL=0x%08x\n", __func__, ctrl);
	writel(ctrl, dwc2->regs + epctrl_reg);

	/* disable endpoint interrupts */
	s3c_hsotg_ctrl_epint(dwc2, hs_ep->index, hs_ep->dir_in, 0);

	spin_unlock_irqrestore(&dwc2->lock, flags);
	return 0;
}

/**
 * on_list - check request is on the given endpoint
 * @ep: The endpoint to check.
 * @test: The request to test if it is on the endpoint.
 */
static bool on_list(struct s3c_hsotg_ep *ep, struct s3c_hsotg_req *test)
{
	struct s3c_hsotg_req *req, *treq;

	list_for_each_entry_safe(req, treq, &ep->queue, queue) {
		if (req == test)
			return true;
	}

	return false;
}

/**
 * s3c_hsotg_ep_dequeue - dequeue given endpoint
 * @ep: The endpoint to dequeue.
 * @req: The request to be removed from a queue.
 */
static int s3c_hsotg_ep_dequeue(struct usb_ep *ep, struct usb_request *req)
{
	struct s3c_hsotg_req *hs_req = our_req(req);
	struct s3c_hsotg_ep *hs_ep = our_ep(ep);
	struct dwc2_hsotg *dwc2 = hs_ep->parent;
	unsigned long flags;

	dev_info(dwc2->dev, "ep_dequeue(%p,%p)\n", ep, req);

	spin_lock_irqsave(&dwc2->lock, flags);

	if (!on_list(hs_ep, hs_req)) {
		spin_unlock_irqrestore(&dwc2->lock, flags);
		return -EINVAL;
	}

	s3c_hsotg_complete_request(dwc2, hs_ep, hs_req, -ECONNRESET);
	spin_unlock_irqrestore(&dwc2->lock, flags);

	return 0;
}

/**
 * s3c_hsotg_ep_sethalt - set halt on a given endpoint
 * @ep: The endpoint to set halt.
 * @value: Set or unset the halt.
 */
static int s3c_hsotg_ep_sethalt(struct usb_ep *ep, int value)
{
	struct s3c_hsotg_ep *hs_ep = our_ep(ep);
	struct dwc2_hsotg *dwc2 = hs_ep->parent;
	int index = hs_ep->index;
	u32 epreg;
	u32 epctl;
	u32 xfertype;

	dev_dbg(dwc2->dev, "%s(ep %p %s, %d)\n", __func__, ep, ep->name, value);

	/* write both IN and OUT control registers */

	epreg = DIEPCTL(index);
	epctl = readl(dwc2->regs + epreg);

	if (value) {
		epctl |= DXEPCTL_STALL + DXEPCTL_SNAK;
		if (epctl & DXEPCTL_EPENA)
			epctl |= DXEPCTL_EPDIS;
	} else {
		epctl &= ~DXEPCTL_STALL;
		xfertype = epctl & DXEPCTL_EPTYPE_MASK;
		if (xfertype == DXEPCTL_EPTYPE_BULK ||
			xfertype == DXEPCTL_EPTYPE_INTERRUPT)
				epctl |= DXEPCTL_SETD0PID;
	}

	writel(epctl, dwc2->regs + epreg);

	epreg = DOEPCTL(index);
	epctl = readl(dwc2->regs + epreg);

	if (value)
		epctl |= DXEPCTL_STALL;
	else {
		epctl &= ~DXEPCTL_STALL;
		xfertype = epctl & DXEPCTL_EPTYPE_MASK;
		if (xfertype == DXEPCTL_EPTYPE_BULK ||
			xfertype == DXEPCTL_EPTYPE_INTERRUPT)
				epctl |= DXEPCTL_SETD0PID;
	}

	writel(epctl, dwc2->regs + epreg);

	return 0;
}

/**
 * s3c_hsotg_ep_sethalt_lock - set halt on a given endpoint with lock held
 * @ep: The endpoint to set halt.
 * @value: Set or unset the halt.
 */
static int s3c_hsotg_ep_sethalt_lock(struct usb_ep *ep, int value)
{
	struct s3c_hsotg_ep *hs_ep = our_ep(ep);
	struct dwc2_hsotg *dwc2 = hs_ep->parent;
	unsigned long flags = 0;
	int ret = 0;

	spin_lock_irqsave(&dwc2->lock, flags);
	ret = s3c_hsotg_ep_sethalt(ep, value);
	spin_unlock_irqrestore(&dwc2->lock, flags);

	return ret;
}

static struct usb_ep_ops s3c_hsotg_ep_ops = {
	.enable		= s3c_hsotg_ep_enable,
	.disable	= s3c_hsotg_ep_disable,
	.alloc_request	= s3c_hsotg_ep_alloc_request,
	.free_request	= s3c_hsotg_ep_free_request,
	.queue		= s3c_hsotg_ep_queue_lock,
	.dequeue	= s3c_hsotg_ep_dequeue,
	.set_halt	= s3c_hsotg_ep_sethalt_lock,
	/* note, don't believe we have any call for the fifo routines */
};

/**
 * s3c_hsotg_phy_enable - enable platform phy dev
 * @hsotg: The driver state
 *
 * A wrapper for platform code responsible for controlling
 * low-level USB code
 */
static void s3c_hsotg_phy_enable(struct dwc2_hsotg *dwc2)
{
	struct platform_device *pdev = to_platform_device(dwc2->dev);

	dev_dbg(dwc2->dev, "pdev 0x%p\n", pdev);

	if (dwc2->s3c_hsotg->gphy) {
		phy_init(dwc2->s3c_hsotg->gphy);
		phy_power_on(dwc2->s3c_hsotg->gphy);
	} else if (dwc2->s3c_hsotg->phy) {
		usb_phy_init(dwc2->s3c_hsotg->phy);
	} else if (dwc2->s3c_hsotg->plat->phy_init)
		dwc2->s3c_hsotg->plat->phy_init(pdev, dwc2->s3c_hsotg->plat->phy_type);
}

/**
 * s3c_hsotg_phy_disable - disable platform phy dev
 * @hsotg: The driver state
 *
 * A wrapper for platform code responsible for controlling
 * low-level USB code
 */
static void s3c_hsotg_phy_disable(struct dwc2_hsotg *dwc2)
{
	struct platform_device *pdev = to_platform_device(dwc2->dev);

	if (dwc2->s3c_hsotg->gphy) {
		phy_power_off(dwc2->s3c_hsotg->gphy);
		phy_exit(dwc2->s3c_hsotg->gphy);
	} else if (dwc2->s3c_hsotg->phy) {
		usb_phy_shutdown(dwc2->s3c_hsotg->phy);
	} else if (dwc2->s3c_hsotg->plat->phy_exit)
		dwc2->s3c_hsotg->plat->phy_exit(pdev, dwc2->s3c_hsotg->plat->phy_type);
}

/**
 * s3c_hsotg_init - initalize the usb core
 * @hsotg: The driver state
 */
static void s3c_hsotg_init(struct dwc2_hsotg *dwc2)
{
	/* unmask subset of endpoint interrupts */

	writel(DIEPMSK_TIMEOUTMSK | DIEPMSK_AHBERRMSK |
	       DIEPMSK_EPDISBLDMSK | DIEPMSK_XFERCOMPLMSK,
	       dwc2->regs + DIEPMSK);

	writel(DOEPMSK_SETUPMSK | DOEPMSK_AHBERRMSK |
	       DOEPMSK_EPDISBLDMSK | DOEPMSK_XFERCOMPLMSK,
	       dwc2->regs + DOEPMSK);

	writel(0, dwc2->regs + DAINTMSK);

	/* Be in disconnected state until gadget is registered */
	__orr32(dwc2->regs + DCTL, DCTL_SFTDISCON);

	if (0) {
		/* post global nak until we're ready */
		writel(DCTL_SGNPINNAK | DCTL_SGOUTNAK,
		       dwc2->regs + DCTL);
	}

	/* setup fifos */

	dev_dbg(dwc2->dev, "GRXFSIZ=0x%08x, GNPTXFSIZ=0x%08x\n",
		readl(dwc2->regs + GRXFSIZ),
		readl(dwc2->regs + GNPTXFSIZ));

	s3c_hsotg_init_fifo(dwc2);

	/* set the PLL on, remove the HNP/SRP and set the PHY */
	writel(GUSBCFG_PHYIF16 | GUSBCFG_TOUTCAL(7) | (0x5 << 10),
	       dwc2->regs + GUSBCFG);

	writel(using_dma(dwc2->s3c_hsotg) ? GAHBCFG_DMA_EN : 0x0,
	       dwc2->regs + GAHBCFG);
}

/**
 * s3c_hsotg_udc_start - prepare the udc for work
 * @gadget: The usb gadget state
 * @driver: The usb gadget driver
 *
 * Perform initialization to prepare udc device and driver
 * to work.
 */
static int s3c_hsotg_udc_start(struct usb_gadget *gadget,
			   struct usb_gadget_driver *driver)
{
	struct dwc2_hsotg *dwc2 = to_hsotg(gadget);
	int ret;

	if (!dwc2) {
		printk(KERN_ERR "%s: called with no device\n", __func__);
		return -ENODEV;
	}

	if (!driver) {
		dev_err(dwc2->dev, "%s: no driver\n", __func__);
		return -EINVAL;
	}

	if (driver->max_speed < USB_SPEED_FULL)
		dev_err(dwc2->dev, "%s: bad speed\n", __func__);

	if (!driver->setup) {
		dev_err(dwc2->dev, "%s: missing entry points\n", __func__);
		return -EINVAL;
	}

	WARN_ON(dwc2->driver);

	driver->driver.bus = NULL;
	dwc2->driver = driver;
	dwc2->gadget.dev.of_node = dwc2->dev->of_node;
	dwc2->gadget.speed = USB_SPEED_UNKNOWN;
	ret = regulator_bulk_enable(ARRAY_SIZE(dwc2->s3c_hsotg->supplies),
				    dwc2->s3c_hsotg->supplies);

	if (ret) {
		dev_err(dwc2->dev, "failed to enable supplies: %d\n", ret);
		goto err;
	}

	dwc2->s3c_hsotg->last_rst = jiffies;
	dev_info(dwc2->dev, "bound driver %s\n", driver->driver.name);
	return 0;

err:
	dwc2->driver = NULL;
	return ret;
}

/**
 * s3c_hsotg_udc_stop - stop the udc
 * @gadget: The usb gadget state
 * @driver: The usb gadget driver
 *
 * Stop udc hw block and stay tunned for future transmissions
 */
static int s3c_hsotg_udc_stop(struct usb_gadget *gadget,
			  struct usb_gadget_driver *driver)
{
	struct dwc2_hsotg *dwc2 = to_hsotg(gadget);
	unsigned long flags = 0;
	int ep;

	if (!dwc2)
		return -ENODEV;

	if (!driver || driver != dwc2->driver || !driver->unbind)
		return -EINVAL;

	/* all endpoints should be shutdown */
	for (ep = 0; ep < dwc2->s3c_hsotg->num_of_eps; ep++)
		s3c_hsotg_ep_disable(&dwc2->eps[ep].ep);

	spin_lock_irqsave(&dwc2->lock, flags);

	s3c_hsotg_phy_disable(dwc2);
	regulator_bulk_disable(ARRAY_SIZE(dwc2->s3c_hsotg->supplies), dwc2->s3c_hsotg->supplies);

	dwc2->driver = NULL;
	dwc2->gadget.speed = USB_SPEED_UNKNOWN;

	spin_unlock_irqrestore(&dwc2->lock, flags);

	dev_info(dwc2->dev, "unregistered gadget driver '%s'\n",
		 driver->driver.name);

	return 0;
}

/**
 * s3c_hsotg_gadget_getframe - read the frame number
 * @gadget: The usb gadget state
 *
 * Read the {micro} frame number
 */
static int s3c_hsotg_gadget_getframe(struct usb_gadget *gadget)
{
	return s3c_hsotg_read_frameno(to_hsotg(gadget));
}

/**
 * s3c_hsotg_pullup - connect/disconnect the USB PHY
 * @gadget: The usb gadget state
 * @is_on: Current state of the USB PHY
 *
 * Connect/Disconnect the USB PHY pullup
 */
static int s3c_hsotg_pullup(struct usb_gadget *gadget, int is_on)
{
	struct dwc2_hsotg *dwc2 = to_hsotg(gadget);
	unsigned long flags = 0;

	dev_dbg(dwc2->dev, "%s: is_in: %d\n", __func__, is_on);

	spin_lock_irqsave(&dwc2->lock, flags);
	if (is_on) {
		s3c_hsotg_phy_enable(dwc2);
		s3c_hsotg_core_init(dwc2);
	} else {
		s3c_hsotg_disconnect(dwc2);
		s3c_hsotg_phy_disable(dwc2);
	}

	dwc2->gadget.speed = USB_SPEED_UNKNOWN;
	spin_unlock_irqrestore(&dwc2->lock, flags);

	return 0;
}

static const struct usb_gadget_ops s3c_hsotg_gadget_ops = {
	.get_frame	= s3c_hsotg_gadget_getframe,
	.udc_start		= s3c_hsotg_udc_start,
	.udc_stop		= s3c_hsotg_udc_stop,
	.pullup                 = s3c_hsotg_pullup,
};

/**
 * s3c_hsotg_initep - initialise a single endpoint
 * @hsotg: The device state.
 * @hs_ep: The endpoint to be initialised.
 * @epnum: The endpoint number
 *
 * Initialise the given endpoint (as part of the probe and device state
 * creation) to give to the gadget driver. Setup the endpoint name, any
 * direction information and other state that may be required.
 */
static void s3c_hsotg_initep(struct dwc2_hsotg *dwc2,
				       struct s3c_hsotg_ep *hs_ep,
				       int epnum)
{
	u32 ptxfifo;
	char *dir;

	if (epnum == 0)
		dir = "";
	else if ((epnum % 2) == 0) {
		dir = "out";
	} else {
		dir = "in";
		hs_ep->dir_in = 1;
	}

	hs_ep->index = epnum;

	snprintf(hs_ep->name, sizeof(hs_ep->name), "ep%d%s", epnum, dir);

	INIT_LIST_HEAD(&hs_ep->queue);
	INIT_LIST_HEAD(&hs_ep->ep.ep_list);

	/* add to the list of endpoints known by the gadget driver */
	if (epnum)
		list_add_tail(&hs_ep->ep.ep_list, &dwc2->gadget.ep_list);

	hs_ep->parent = dwc2;
	hs_ep->ep.name = hs_ep->name;
	hs_ep->ep.maxpacket = epnum ? 512 : EP0_MPS_LIMIT;
	hs_ep->ep.ops = &s3c_hsotg_ep_ops;

	/*
	 * Read the FIFO size for the Periodic TX FIFO, even if we're
	 * an OUT endpoint, we may as well do this if in future the
	 * code is changed to make each endpoint's direction changeable.
	 */

	ptxfifo = readl(dwc2->regs + DPTXFSIZN(epnum));
	hs_ep->fifo_size = DPTXFSIZN_DPTXFSIZE_GET(ptxfifo) * 4;

	/*
	 * if we're using dma, we need to set the next-endpoint pointer
	 * to be something valid.
	 */

	if (using_dma(dwc2->s3c_hsotg)) {
		u32 next = DXEPCTL_NEXTEP((epnum + 1) % 15);
		writel(next, dwc2->regs + DIEPCTL(epnum));
		writel(next, dwc2->regs + DOEPCTL(epnum));
	}
}

/**
 * s3c_hsotg_hw_cfg - read HW configuration registers
 * @param: The device state
 *
 * Read the USB core HW configuration registers
 */
static void s3c_hsotg_hw_cfg(struct dwc2_hsotg *dwc2)
{
	u32 cfg2, cfg4;
	/* check hardware configuration */

	cfg2 = readl(dwc2->regs + 0x48);
	dwc2->s3c_hsotg->num_of_eps = (cfg2 >> 10) & 0xF;

	dev_info(dwc2->dev, "EPs:%d\n", dwc2->s3c_hsotg->num_of_eps);

	cfg4 = readl(dwc2->regs + 0x50);
	dwc2->s3c_hsotg->dedicated_fifos = (cfg4 >> 25) & 1;

	dev_info(dwc2->dev, "%s fifos\n",
		 dwc2->s3c_hsotg->dedicated_fifos ? "dedicated" : "shared");
}

/**
 * s3c_hsotg_dump - dump state of the udc
 * @param: The device state
 */
void s3c_hsotg_dump(struct dwc2_hsotg *dwc2)
{
#ifdef DEBUG
	struct device *dev = dwc2->dev;
	void __iomem *regs = dwc2->regs;
	u32 val;
	int idx;

	dev_info(dev, "DCFG=0x%08x, DCTL=0x%08x, DIEPMSK=%08x\n",
		 readl(regs + DCFG), readl(regs + DCTL),
		 readl(regs + DIEPMSK));

	dev_info(dev, "GAHBCFG=0x%08x, 0x44=0x%08x\n",
		 readl(regs + GAHBCFG), readl(regs + 0x44));

	dev_info(dev, "GRXFSIZ=0x%08x, GNPTXFSIZ=0x%08x\n",
		 readl(regs + GRXFSIZ), readl(regs + GNPTXFSIZ));

	/* show periodic fifo settings */

	for (idx = 1; idx <= 15; idx++) {
		val = readl(regs + DPTXFSIZN(idx));
		dev_info(dev, "DPTx[%d] FSize=%d, StAddr=0x%08x\n", idx,
			 val >> FIFOSIZE_DEPTH_SHIFT,
			 val & FIFOSIZE_STARTADDR_MASK);
	}

	for (idx = 0; idx < 15; idx++) {
		dev_info(dev,
			 "ep%d-in: EPCTL=0x%08x, SIZ=0x%08x, DMA=0x%08x\n", idx,
			 readl(regs + DIEPCTL(idx)),
			 readl(regs + DIEPTSIZ(idx)),
			 readl(regs + DIEPDMA(idx)));

		val = readl(regs + DOEPCTL(idx));
		dev_info(dev,
			 "ep%d-out: EPCTL=0x%08x, SIZ=0x%08x, DMA=0x%08x\n",
			 idx, readl(regs + DOEPCTL(idx)),
			 readl(regs + DOEPTSIZ(idx)),
			 readl(regs + DOEPDMA(idx)));

	}

	dev_info(dev, "DVBUSDIS=0x%08x, DVBUSPULSE=%08x\n",
		 readl(regs + DVBUSDIS), readl(regs + DVBUSPULSE));
#endif
}

/**
 * state_show - debugfs: show overall driver and device state.
 * @seq: The seq file to write to.
 * @v: Unused parameter.
 *
 * This debugfs entry shows the overall state of the hardware and
 * some general information about each of the endpoints available
 * to the system.
 */
static int state_show(struct seq_file *seq, void *v)
{
	struct dwc2_hsotg *dwc2 = seq->private;
	void __iomem *regs = dwc2->regs;
	int idx;

	seq_printf(seq, "DCFG=0x%08x, DCTL=0x%08x, DSTS=0x%08x\n",
		 readl(regs + DCFG),
		 readl(regs + DCTL),
		 readl(regs + DSTS));

	seq_printf(seq, "DIEPMSK=0x%08x, DOEPMASK=0x%08x\n",
		   readl(regs + DIEPMSK), readl(regs + DOEPMSK));

	seq_printf(seq, "GINTMSK=0x%08x, GINTSTS=0x%08x\n",
		   readl(regs + GINTMSK),
		   readl(regs + GINTSTS));

	seq_printf(seq, "DAINTMSK=0x%08x, DAINT=0x%08x\n",
		   readl(regs + DAINTMSK),
		   readl(regs + DAINT));

	seq_printf(seq, "GNPTXSTS=0x%08x, GRXSTSR=%08x\n",
		   readl(regs + GNPTXSTS),
		   readl(regs + GRXSTSR));

	seq_printf(seq, "\nEndpoint status:\n");

	for (idx = 0; idx < 15; idx++) {
		u32 in, out;

		in = readl(regs + DIEPCTL(idx));
		out = readl(regs + DOEPCTL(idx));

		seq_printf(seq, "ep%d: DIEPCTL=0x%08x, DOEPCTL=0x%08x",
			   idx, in, out);

		in = readl(regs + DIEPTSIZ(idx));
		out = readl(regs + DOEPTSIZ(idx));

		seq_printf(seq, ", DIEPTSIZ=0x%08x, DOEPTSIZ=0x%08x",
			   in, out);

		seq_printf(seq, "\n");
	}

	return 0;
}

static int state_open(struct inode *inode, struct file *file)
{
	return single_open(file, state_show, inode->i_private);
}

static const struct file_operations state_fops = {
	.owner		= THIS_MODULE,
	.open		= state_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/**
 * fifo_show - debugfs: show the fifo information
 * @seq: The seq_file to write data to.
 * @v: Unused parameter.
 *
 * Show the FIFO information for the overall fifo and all the
 * periodic transmission FIFOs.
 */
static int fifo_show(struct seq_file *seq, void *v)
{
	struct dwc2_hsotg *dwc2 = seq->private;
	void __iomem *regs = dwc2->regs;
	u32 val;
	int idx;

	seq_printf(seq, "Non-periodic FIFOs:\n");
	seq_printf(seq, "RXFIFO: Size %d\n", readl(regs + GRXFSIZ));

	val = readl(regs + GNPTXFSIZ);
	seq_printf(seq, "NPTXFIFO: Size %d, Start 0x%08x\n",
		   val >> FIFOSIZE_DEPTH_SHIFT,
		   val & FIFOSIZE_DEPTH_MASK);

	seq_printf(seq, "\nPeriodic TXFIFOs:\n");

	for (idx = 1; idx <= 15; idx++) {
		val = readl(regs + DPTXFSIZN(idx));

		seq_printf(seq, "\tDPTXFIFO%2d: Size %d, Start 0x%08x\n", idx,
			   val >> FIFOSIZE_DEPTH_SHIFT,
			   val & FIFOSIZE_STARTADDR_MASK);
	}

	return 0;
}

static int fifo_open(struct inode *inode, struct file *file)
{
	return single_open(file, fifo_show, inode->i_private);
}

static const struct file_operations fifo_fops = {
	.owner		= THIS_MODULE,
	.open		= fifo_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};


static const char *decode_direction(int is_in)
{
	return is_in ? "in" : "out";
}

/**
 * ep_show - debugfs: show the state of an endpoint.
 * @seq: The seq_file to write data to.
 * @v: Unused parameter.
 *
 * This debugfs entry shows the state of the given endpoint (one is
 * registered for each available).
 */
static int ep_show(struct seq_file *seq, void *v)
{
	struct s3c_hsotg_ep *ep = seq->private;
	struct dwc2_hsotg *dwc2 = ep->parent;
	struct s3c_hsotg_req *req;
	void __iomem *regs = dwc2->regs;
	int index = ep->index;
	int show_limit = 15;
	unsigned long flags;

	seq_printf(seq, "Endpoint index %d, named %s,  dir %s:\n",
		   ep->index, ep->ep.name, decode_direction(ep->dir_in));

	/* first show the register state */

	seq_printf(seq, "\tDIEPCTL=0x%08x, DOEPCTL=0x%08x\n",
		   readl(regs + DIEPCTL(index)),
		   readl(regs + DOEPCTL(index)));

	seq_printf(seq, "\tDIEPDMA=0x%08x, DOEPDMA=0x%08x\n",
		   readl(regs + DIEPDMA(index)),
		   readl(regs + DOEPDMA(index)));

	seq_printf(seq, "\tDIEPINT=0x%08x, DOEPINT=0x%08x\n",
		   readl(regs + DIEPINT(index)),
		   readl(regs + DOEPINT(index)));

	seq_printf(seq, "\tDIEPTSIZ=0x%08x, DOEPTSIZ=0x%08x\n",
		   readl(regs + DIEPTSIZ(index)),
		   readl(regs + DOEPTSIZ(index)));

	seq_printf(seq, "\n");
	seq_printf(seq, "mps %d\n", ep->ep.maxpacket);
	seq_printf(seq, "total_data=%ld\n", ep->total_data);

	seq_printf(seq, "request list (%p,%p):\n",
		   ep->queue.next, ep->queue.prev);

	spin_lock_irqsave(&dwc2->lock, flags);

	list_for_each_entry(req, &ep->queue, queue) {
		if (--show_limit < 0) {
			seq_printf(seq, "not showing more requests...\n");
			break;
		}

		seq_printf(seq, "%c req %p: %d bytes @%p, ",
			   req == ep->req ? '*' : ' ',
			   req, req->req.length, req->req.buf);
		seq_printf(seq, "%d done, res %d\n",
			   req->req.actual, req->req.status);
	}

	spin_unlock_irqrestore(&dwc2->lock, flags);

	return 0;
}

static int ep_open(struct inode *inode, struct file *file)
{
	return single_open(file, ep_show, inode->i_private);
}

static const struct file_operations ep_fops = {
	.owner		= THIS_MODULE,
	.open		= ep_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/**
 * s3c_hsotg_create_debug - create debugfs directory and files
 * @hsotg: The driver state
 *
 * Create the debugfs files to allow the user to get information
 * about the state of the system. The directory name is created
 * with the same name as the device itself, in case we end up
 * with multiple blocks in future systems.
 */
static void s3c_hsotg_create_debug(struct dwc2_hsotg *dwc2)
{
	struct dentry *root;
	unsigned epidx;

	root = debugfs_create_dir(dev_name(dwc2->dev), NULL);
	dwc2->s3c_hsotg->debug_root = root;
	if (IS_ERR(root)) {
		dev_err(dwc2->dev, "cannot create debug root\n");
		return;
	}

	/* create general state file */

	dwc2->s3c_hsotg->debug_file = debugfs_create_file("state", 0444, root,
						dwc2, &state_fops);

	if (IS_ERR(dwc2->s3c_hsotg->debug_file))
		dev_err(dwc2->dev, "%s: failed to create state\n", __func__);

	dwc2->s3c_hsotg->debug_fifo = debugfs_create_file("fifo", 0444, root,
						dwc2, &fifo_fops);

	if (IS_ERR(dwc2->s3c_hsotg->debug_fifo))
		dev_err(dwc2->dev, "%s: failed to create fifo\n", __func__);

	/* create one file for each endpoint */

	for (epidx = 0; epidx < dwc2->s3c_hsotg->num_of_eps; epidx++) {
		struct s3c_hsotg_ep *ep = &dwc2->eps[epidx];

		ep->debugfs = debugfs_create_file(ep->name, 0444,
						  root, ep, &ep_fops);

		if (IS_ERR(ep->debugfs))
			dev_err(dwc2->dev, "failed to create %s debug file\n",
				ep->name);
	}
}

/**
 * s3c_hsotg_delete_debug - cleanup debugfs entries
 * @hsotg: The driver state
 *
 * Cleanup (remove) the debugfs files for use on module exit.
 */
static void s3c_hsotg_delete_debug(struct dwc2_hsotg *dwc2)
{
	unsigned epidx;

	for (epidx = 0; epidx < dwc2->s3c_hsotg->num_of_eps; epidx++) {
		struct s3c_hsotg_ep *ep = &dwc2->eps[epidx];
		debugfs_remove(ep->debugfs);
	}

	debugfs_remove(dwc2->s3c_hsotg->debug_file);
	debugfs_remove(dwc2->s3c_hsotg->debug_fifo);
	debugfs_remove(dwc2->s3c_hsotg->debug_root);
}

/**
 * dwc2_gadget_init - probe function for hsotg driver
 * @pdev: The platform information for the driver
 */

int dwc2_gadget_init(struct dwc2_hsotg *dwc2, int irq)
{
	struct s3c_hsotg_plat *plat = dev_get_platdata(dwc2->dev);
	struct usb_phy *phy;
	struct phy *gphy;
	struct device *dev = dwc2->dev;
	struct s3c_hsotg_ep *eps;
	int epnum;
	int ret;
	int i;

	dwc2->s3c_hsotg = devm_kzalloc(dwc2->dev, sizeof(struct s3c_hsotg), GFP_KERNEL);
	if (!dwc2->s3c_hsotg) {
		dev_err(dev, "cannot get memory\n");
		return -ENOMEM;
	}

	/*
	 * Attempt to find a generic PHY, then look for an old style
	 *  USB PHY, finally fall back to pdata
	 */
	gphy = devm_phy_get(dev, "usb2-phy");
	if (IS_ERR(gphy)) {
		phy = devm_usb_get_phy(dev, USB_PHY_TYPE_USB2);
		if (IS_ERR(phy)) {
			/* Fallback for pdata */
			plat = dev_get_platdata(dwc2->dev);
			if (!plat) {
				dev_err(dwc2->dev,
					"no platform data or transceiver defined\n");
				return -EPROBE_DEFER;
			} else {
				dwc2->s3c_hsotg->plat = plat;
			}
		} else
			dwc2->s3c_hsotg->phy = phy;
	} else
		dwc2->s3c_hsotg->gphy = gphy;

	dwc2->s3c_hsotg->clk = devm_clk_get(dwc2->dev, "otg");
	if (IS_ERR(dwc2->s3c_hsotg->clk)) {
		dev_err(dev, "cannot get otg clock\n");
		return PTR_ERR(dwc2->s3c_hsotg->clk);
	}

	dwc2->gadget.max_speed = USB_SPEED_HIGH;
	dwc2->gadget.ops = &s3c_hsotg_gadget_ops;
	dwc2->gadget.name = dev_name(dev);

	/* reset the system */

	clk_prepare_enable(dwc2->s3c_hsotg->clk);
	/* regulators */

	for (i = 0; i < ARRAY_SIZE(dwc2->s3c_hsotg->supplies); i++)
		dwc2->s3c_hsotg->supplies[i].supply = s3c_hsotg_supply_names[i];

	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(dwc2->s3c_hsotg->supplies),
				 dwc2->s3c_hsotg->supplies);
	if (ret) {
		dev_err(dev, "failed to request supplies: %d\n", ret);
		goto err_clk;
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(dwc2->s3c_hsotg->supplies),
				    dwc2->s3c_hsotg->supplies);

	if (ret) {
		dev_err(dwc2->dev, "failed to enable supplies: %d\n", ret);
		goto err_supplies;
	}

	/* usb phy enable */
	s3c_hsotg_phy_enable(dwc2);

	s3c_hsotg_corereset(dwc2);
	s3c_hsotg_init(dwc2);
	s3c_hsotg_hw_cfg(dwc2);

	/* hsotg->num_of_eps holds number of EPs other than ep0 */

	if (dwc2->s3c_hsotg->num_of_eps == 0) {
		dev_err(dev, "wrong number of EPs (zero)\n");
		ret = -EINVAL;
		goto err_supplies;
	}

	eps = kcalloc(dwc2->s3c_hsotg->num_of_eps + 1, sizeof(struct s3c_hsotg_ep),
		      GFP_KERNEL);
	if (!eps) {
		dev_err(dev, "cannot get memory\n");
		ret = -ENOMEM;
		goto err_supplies;
	}

	dwc2->eps = eps;

	/* setup endpoint information */

	INIT_LIST_HEAD(&dwc2->gadget.ep_list);
	dwc2->gadget.ep0 = &dwc2->eps[0].ep;

	/* allocate EP0 request */

	dwc2->s3c_hsotg->ctrl_req = s3c_hsotg_ep_alloc_request(&dwc2->eps[0].ep,
						     GFP_KERNEL);
	if (!dwc2->s3c_hsotg->ctrl_req) {
		dev_err(dev, "failed to allocate ctrl req\n");
		ret = -ENOMEM;
		goto err_ep_mem;
	}

	/* initialise the endpoints now the core has been initialised */
	for (epnum = 0; epnum < dwc2->s3c_hsotg->num_of_eps; epnum++)
		s3c_hsotg_initep(dwc2, &dwc2->eps[epnum], epnum);

	/* disable power and clock */

	ret = regulator_bulk_disable(ARRAY_SIZE(dwc2->s3c_hsotg->supplies),
				    dwc2->s3c_hsotg->supplies);
	if (ret) {
		dev_err(dwc2->dev, "failed to disable supplies: %d\n", ret);
		goto err_ep_mem;
	}

	s3c_hsotg_phy_disable(dwc2);

	ret = usb_add_gadget_udc(dwc2->dev, &dwc2->gadget);
	if (ret)
		goto err_ep_mem;

	s3c_hsotg_create_debug(dwc2);

	s3c_hsotg_dump(dwc2);

	return 0;

err_ep_mem:
	kfree(eps);
err_supplies:
	s3c_hsotg_phy_disable(dwc2);
err_clk:
	clk_disable_unprepare(dwc2->s3c_hsotg->clk);

	return ret;
}

void s3c_hsotg_remove(struct dwc2_hsotg *dwc2)
{
	usb_del_gadget_udc(&dwc2->gadget);

	s3c_hsotg_delete_debug(dwc2);

	if (dwc2->driver) {
		/* should have been done already by driver model core */
		usb_gadget_unregister_driver(dwc2->driver);
	}

	s3c_hsotg_phy_disable(dwc2);
	clk_disable_unprepare(dwc2->s3c_hsotg->clk);
}
