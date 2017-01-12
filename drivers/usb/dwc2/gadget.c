/**
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
#include <linux/mutex.h>
#include <linux/seq_file.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/of_platform.h>

#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/usb/phy.h>

#include "core.h"
#include "hw.h"

/* conversion functions */
static inline struct dwc2_hsotg_req *our_req(struct usb_request *req)
{
	return container_of(req, struct dwc2_hsotg_req, req);
}

static inline struct dwc2_hsotg_ep *our_ep(struct usb_ep *ep)
{
	return container_of(ep, struct dwc2_hsotg_ep, ep);
}

static inline struct dwc2_hsotg *to_hsotg(struct usb_gadget *gadget)
{
	return container_of(gadget, struct dwc2_hsotg, gadget);
}

static inline void __orr32(void __iomem *ptr, u32 val)
{
	dwc2_writel(dwc2_readl(ptr) | val, ptr);
}

static inline void __bic32(void __iomem *ptr, u32 val)
{
	dwc2_writel(dwc2_readl(ptr) & ~val, ptr);
}

static inline struct dwc2_hsotg_ep *index_to_ep(struct dwc2_hsotg *hsotg,
						u32 ep_index, u32 dir_in)
{
	if (dir_in)
		return hsotg->eps_in[ep_index];
	else
		return hsotg->eps_out[ep_index];
}

/* forward declaration of functions */
static void dwc2_hsotg_dump(struct dwc2_hsotg *hsotg);

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
 * g_using_dma is set depending on dts flag.
 */
static inline bool using_dma(struct dwc2_hsotg *hsotg)
{
	return hsotg->params.g_dma;
}

/*
 * using_desc_dma - return the descriptor DMA status of the driver.
 * @hsotg: The driver state.
 *
 * Return true if we're using descriptor DMA.
 */
static inline bool using_desc_dma(struct dwc2_hsotg *hsotg)
{
	return hsotg->params.g_dma_desc;
}

/**
 * dwc2_gadget_incr_frame_num - Increments the targeted frame number.
 * @hs_ep: The endpoint
 * @increment: The value to increment by
 *
 * This function will also check if the frame number overruns DSTS_SOFFN_LIMIT.
 * If an overrun occurs it will wrap the value and set the frame_overrun flag.
 */
static inline void dwc2_gadget_incr_frame_num(struct dwc2_hsotg_ep *hs_ep)
{
	hs_ep->target_frame += hs_ep->interval;
	if (hs_ep->target_frame > DSTS_SOFFN_LIMIT) {
		hs_ep->frame_overrun = 1;
		hs_ep->target_frame &= DSTS_SOFFN_LIMIT;
	} else {
		hs_ep->frame_overrun = 0;
	}
}

/**
 * dwc2_hsotg_en_gsint - enable one or more of the general interrupt
 * @hsotg: The device state
 * @ints: A bitmask of the interrupts to enable
 */
static void dwc2_hsotg_en_gsint(struct dwc2_hsotg *hsotg, u32 ints)
{
	u32 gsintmsk = dwc2_readl(hsotg->regs + GINTMSK);
	u32 new_gsintmsk;

	new_gsintmsk = gsintmsk | ints;

	if (new_gsintmsk != gsintmsk) {
		dev_dbg(hsotg->dev, "gsintmsk now 0x%08x\n", new_gsintmsk);
		dwc2_writel(new_gsintmsk, hsotg->regs + GINTMSK);
	}
}

/**
 * dwc2_hsotg_disable_gsint - disable one or more of the general interrupt
 * @hsotg: The device state
 * @ints: A bitmask of the interrupts to enable
 */
static void dwc2_hsotg_disable_gsint(struct dwc2_hsotg *hsotg, u32 ints)
{
	u32 gsintmsk = dwc2_readl(hsotg->regs + GINTMSK);
	u32 new_gsintmsk;

	new_gsintmsk = gsintmsk & ~ints;

	if (new_gsintmsk != gsintmsk)
		dwc2_writel(new_gsintmsk, hsotg->regs + GINTMSK);
}

/**
 * dwc2_hsotg_ctrl_epint - enable/disable an endpoint irq
 * @hsotg: The device state
 * @ep: The endpoint index
 * @dir_in: True if direction is in.
 * @en: The enable value, true to enable
 *
 * Set or clear the mask for an individual endpoint's interrupt
 * request.
 */
static void dwc2_hsotg_ctrl_epint(struct dwc2_hsotg *hsotg,
				 unsigned int ep, unsigned int dir_in,
				 unsigned int en)
{
	unsigned long flags;
	u32 bit = 1 << ep;
	u32 daint;

	if (!dir_in)
		bit <<= 16;

	local_irq_save(flags);
	daint = dwc2_readl(hsotg->regs + DAINTMSK);
	if (en)
		daint |= bit;
	else
		daint &= ~bit;
	dwc2_writel(daint, hsotg->regs + DAINTMSK);
	local_irq_restore(flags);
}

/**
 * dwc2_hsotg_init_fifo - initialise non-periodic FIFOs
 * @hsotg: The device instance.
 */
static void dwc2_hsotg_init_fifo(struct dwc2_hsotg *hsotg)
{
	unsigned int ep;
	unsigned int addr;
	int timeout;
	u32 val;
	u32 *txfsz = hsotg->params.g_tx_fifo_size;

	/* Reset fifo map if not correctly cleared during previous session */
	WARN_ON(hsotg->fifo_map);
	hsotg->fifo_map = 0;

	/* set RX/NPTX FIFO sizes */
	dwc2_writel(hsotg->params.g_rx_fifo_size, hsotg->regs + GRXFSIZ);
	dwc2_writel((hsotg->params.g_rx_fifo_size << FIFOSIZE_STARTADDR_SHIFT) |
		    (hsotg->params.g_np_tx_fifo_size << FIFOSIZE_DEPTH_SHIFT),
		    hsotg->regs + GNPTXFSIZ);

	/*
	 * arange all the rest of the TX FIFOs, as some versions of this
	 * block have overlapping default addresses. This also ensures
	 * that if the settings have been changed, then they are set to
	 * known values.
	 */

	/* start at the end of the GNPTXFSIZ, rounded up */
	addr = hsotg->params.g_rx_fifo_size + hsotg->params.g_np_tx_fifo_size;

	/*
	 * Configure fifos sizes from provided configuration and assign
	 * them to endpoints dynamically according to maxpacket size value of
	 * given endpoint.
	 */
	for (ep = 1; ep < MAX_EPS_CHANNELS; ep++) {
		if (!txfsz[ep])
			continue;
		val = addr;
		val |= txfsz[ep] << FIFOSIZE_DEPTH_SHIFT;
		WARN_ONCE(addr + txfsz[ep] > hsotg->fifo_mem,
			  "insufficient fifo memory");
		addr += txfsz[ep];

		dwc2_writel(val, hsotg->regs + DPTXFSIZN(ep));
		val = dwc2_readl(hsotg->regs + DPTXFSIZN(ep));
	}

	/*
	 * according to p428 of the design guide, we need to ensure that
	 * all fifos are flushed before continuing
	 */

	dwc2_writel(GRSTCTL_TXFNUM(0x10) | GRSTCTL_TXFFLSH |
	       GRSTCTL_RXFFLSH, hsotg->regs + GRSTCTL);

	/* wait until the fifos are both flushed */
	timeout = 100;
	while (1) {
		val = dwc2_readl(hsotg->regs + GRSTCTL);

		if ((val & (GRSTCTL_TXFFLSH | GRSTCTL_RXFFLSH)) == 0)
			break;

		if (--timeout == 0) {
			dev_err(hsotg->dev,
				"%s: timeout flushing fifos (GRSTCTL=%08x)\n",
				__func__, val);
			break;
		}

		udelay(1);
	}

	dev_dbg(hsotg->dev, "FIFOs reset, timeout at %d\n", timeout);
}

/**
 * @ep: USB endpoint to allocate request for.
 * @flags: Allocation flags
 *
 * Allocate a new USB request structure appropriate for the specified endpoint
 */
static struct usb_request *dwc2_hsotg_ep_alloc_request(struct usb_ep *ep,
						      gfp_t flags)
{
	struct dwc2_hsotg_req *req;

	req = kzalloc(sizeof(struct dwc2_hsotg_req), flags);
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
static inline int is_ep_periodic(struct dwc2_hsotg_ep *hs_ep)
{
	return hs_ep->periodic;
}

/**
 * dwc2_hsotg_unmap_dma - unmap the DMA memory being used for the request
 * @hsotg: The device state.
 * @hs_ep: The endpoint for the request
 * @hs_req: The request being processed.
 *
 * This is the reverse of dwc2_hsotg_map_dma(), called for the completion
 * of a request to ensure the buffer is ready for access by the caller.
 */
static void dwc2_hsotg_unmap_dma(struct dwc2_hsotg *hsotg,
				struct dwc2_hsotg_ep *hs_ep,
				struct dwc2_hsotg_req *hs_req)
{
	struct usb_request *req = &hs_req->req;
	usb_gadget_unmap_request(&hsotg->gadget, req, hs_ep->dir_in);
}

/*
 * dwc2_gadget_alloc_ctrl_desc_chains - allocate DMA descriptor chains
 * for Control endpoint
 * @hsotg: The device state.
 *
 * This function will allocate 4 descriptor chains for EP 0: 2 for
 * Setup stage, per one for IN and OUT data/status transactions.
 */
static int dwc2_gadget_alloc_ctrl_desc_chains(struct dwc2_hsotg *hsotg)
{
	hsotg->setup_desc[0] =
		dmam_alloc_coherent(hsotg->dev,
				    sizeof(struct dwc2_dma_desc),
				    &hsotg->setup_desc_dma[0],
				    GFP_KERNEL);
	if (!hsotg->setup_desc[0])
		goto fail;

	hsotg->setup_desc[1] =
		dmam_alloc_coherent(hsotg->dev,
				    sizeof(struct dwc2_dma_desc),
				    &hsotg->setup_desc_dma[1],
				    GFP_KERNEL);
	if (!hsotg->setup_desc[1])
		goto fail;

	hsotg->ctrl_in_desc =
		dmam_alloc_coherent(hsotg->dev,
				    sizeof(struct dwc2_dma_desc),
				    &hsotg->ctrl_in_desc_dma,
				    GFP_KERNEL);
	if (!hsotg->ctrl_in_desc)
		goto fail;

	hsotg->ctrl_out_desc =
		dmam_alloc_coherent(hsotg->dev,
				    sizeof(struct dwc2_dma_desc),
				    &hsotg->ctrl_out_desc_dma,
				    GFP_KERNEL);
	if (!hsotg->ctrl_out_desc)
		goto fail;

	return 0;

fail:
	return -ENOMEM;
}

/**
 * dwc2_hsotg_write_fifo - write packet Data to the TxFIFO
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
static int dwc2_hsotg_write_fifo(struct dwc2_hsotg *hsotg,
				struct dwc2_hsotg_ep *hs_ep,
				struct dwc2_hsotg_req *hs_req)
{
	bool periodic = is_ep_periodic(hs_ep);
	u32 gnptxsts = dwc2_readl(hsotg->regs + GNPTXSTS);
	int buf_pos = hs_req->req.actual;
	int to_write = hs_ep->size_loaded;
	void *data;
	int can_write;
	int pkt_round;
	int max_transfer;

	to_write -= (buf_pos - hs_ep->last_load);

	/* if there's nothing to write, get out early */
	if (to_write == 0)
		return 0;

	if (periodic && !hsotg->dedicated_fifos) {
		u32 epsize = dwc2_readl(hsotg->regs + DIEPTSIZ(hs_ep->index));
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
			dwc2_hsotg_en_gsint(hsotg, GINTSTS_PTXFEMP);
			return -ENOSPC;
		}

		dev_dbg(hsotg->dev, "%s: left=%d, load=%d, fifo=%d, size %d\n",
			__func__, size_left,
			hs_ep->size_loaded, hs_ep->fifo_load, hs_ep->fifo_size);

		/* how much of the data has moved */
		size_done = hs_ep->size_loaded - size_left;

		/* how much data is left in the fifo */
		can_write = hs_ep->fifo_load - size_done;
		dev_dbg(hsotg->dev, "%s: => can_write1=%d\n",
			__func__, can_write);

		can_write = hs_ep->fifo_size - can_write;
		dev_dbg(hsotg->dev, "%s: => can_write2=%d\n",
			__func__, can_write);

		if (can_write <= 0) {
			dwc2_hsotg_en_gsint(hsotg, GINTSTS_PTXFEMP);
			return -ENOSPC;
		}
	} else if (hsotg->dedicated_fifos && hs_ep->index != 0) {
		can_write = dwc2_readl(hsotg->regs +
				DTXFSTS(hs_ep->fifo_index));

		can_write &= 0xffff;
		can_write *= 4;
	} else {
		if (GNPTXSTS_NP_TXQ_SPC_AVAIL_GET(gnptxsts) == 0) {
			dev_dbg(hsotg->dev,
				"%s: no queue slots available (0x%08x)\n",
				__func__, gnptxsts);

			dwc2_hsotg_en_gsint(hsotg, GINTSTS_NPTXFEMP);
			return -ENOSPC;
		}

		can_write = GNPTXSTS_NP_TXF_SPC_AVAIL_GET(gnptxsts);
		can_write *= 4;	/* fifo size is in 32bit quantities. */
	}

	max_transfer = hs_ep->ep.maxpacket * hs_ep->mc;

	dev_dbg(hsotg->dev, "%s: GNPTXSTS=%08x, can=%d, to=%d, max_transfer %d\n",
		 __func__, gnptxsts, can_write, to_write, max_transfer);

	/*
	 * limit to 512 bytes of data, it seems at least on the non-periodic
	 * FIFO, requests of >512 cause the endpoint to get stuck with a
	 * fragment of the end of the transfer in it.
	 */
	if (can_write > 512 && !periodic)
		can_write = 512;

	/*
	 * limit the write to one max-packet size worth of data, but allow
	 * the transfer to return that it did not run out of fifo space
	 * doing it.
	 */
	if (to_write > max_transfer) {
		to_write = max_transfer;

		/* it's needed only when we do not use dedicated fifos */
		if (!hsotg->dedicated_fifos)
			dwc2_hsotg_en_gsint(hsotg,
					   periodic ? GINTSTS_PTXFEMP :
					   GINTSTS_NPTXFEMP);
	}

	/* see if we can write data */

	if (to_write > can_write) {
		to_write = can_write;
		pkt_round = to_write % max_transfer;

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

		/* it's needed only when we do not use dedicated fifos */
		if (!hsotg->dedicated_fifos)
			dwc2_hsotg_en_gsint(hsotg,
					   periodic ? GINTSTS_PTXFEMP :
					   GINTSTS_NPTXFEMP);
	}

	dev_dbg(hsotg->dev, "write %d/%d, can_write %d, done %d\n",
		 to_write, hs_req->req.length, can_write, buf_pos);

	if (to_write <= 0)
		return -ENOSPC;

	hs_req->req.actual = buf_pos + to_write;
	hs_ep->total_data += to_write;

	if (periodic)
		hs_ep->fifo_load += to_write;

	to_write = DIV_ROUND_UP(to_write, 4);
	data = hs_req->req.buf + buf_pos;

	iowrite32_rep(hsotg->regs + EPFIFO(hs_ep->index), data, to_write);

	return (to_write >= can_write) ? -ENOSPC : 0;
}

/**
 * get_ep_limit - get the maximum data legnth for this endpoint
 * @hs_ep: The endpoint
 *
 * Return the maximum data that can be queued in one go on a given endpoint
 * so that transfers that are too long can be split.
 */
static unsigned get_ep_limit(struct dwc2_hsotg_ep *hs_ep)
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
* dwc2_hsotg_read_frameno - read current frame number
* @hsotg: The device instance
*
* Return the current frame number
*/
static u32 dwc2_hsotg_read_frameno(struct dwc2_hsotg *hsotg)
{
	u32 dsts;

	dsts = dwc2_readl(hsotg->regs + DSTS);
	dsts &= DSTS_SOFFN_MASK;
	dsts >>= DSTS_SOFFN_SHIFT;

	return dsts;
}

/**
 * dwc2_gadget_get_chain_limit - get the maximum data payload value of the
 * DMA descriptor chain prepared for specific endpoint
 * @hs_ep: The endpoint
 *
 * Return the maximum data that can be queued in one go on a given endpoint
 * depending on its descriptor chain capacity so that transfers that
 * are too long can be split.
 */
static unsigned int dwc2_gadget_get_chain_limit(struct dwc2_hsotg_ep *hs_ep)
{
	int is_isoc = hs_ep->isochronous;
	unsigned int maxsize;

	if (is_isoc)
		maxsize = hs_ep->dir_in ? DEV_DMA_ISOC_TX_NBYTES_LIMIT :
					   DEV_DMA_ISOC_RX_NBYTES_LIMIT;
	else
		maxsize = DEV_DMA_NBYTES_LIMIT;

	/* Above size of one descriptor was chosen, multiple it */
	maxsize *= MAX_DMA_DESC_NUM_GENERIC;

	return maxsize;
}

/*
 * dwc2_gadget_get_desc_params - get DMA descriptor parameters.
 * @hs_ep: The endpoint
 * @mask: RX/TX bytes mask to be defined
 *
 * Returns maximum data payload for one descriptor after analyzing endpoint
 * characteristics.
 * DMA descriptor transfer bytes limit depends on EP type:
 * Control out - MPS,
 * Isochronous - descriptor rx/tx bytes bitfield limit,
 * Control In/Bulk/Interrupt - multiple of mps. This will allow to not
 * have concatenations from various descriptors within one packet.
 *
 * Selects corresponding mask for RX/TX bytes as well.
 */
static u32 dwc2_gadget_get_desc_params(struct dwc2_hsotg_ep *hs_ep, u32 *mask)
{
	u32 mps = hs_ep->ep.maxpacket;
	int dir_in = hs_ep->dir_in;
	u32 desc_size = 0;

	if (!hs_ep->index && !dir_in) {
		desc_size = mps;
		*mask = DEV_DMA_NBYTES_MASK;
	} else if (hs_ep->isochronous) {
		if (dir_in) {
			desc_size = DEV_DMA_ISOC_TX_NBYTES_LIMIT;
			*mask = DEV_DMA_ISOC_TX_NBYTES_MASK;
		} else {
			desc_size = DEV_DMA_ISOC_RX_NBYTES_LIMIT;
			*mask = DEV_DMA_ISOC_RX_NBYTES_MASK;
		}
	} else {
		desc_size = DEV_DMA_NBYTES_LIMIT;
		*mask = DEV_DMA_NBYTES_MASK;

		/* Round down desc_size to be mps multiple */
		desc_size -= desc_size % mps;
	}

	return desc_size;
}

/*
 * dwc2_gadget_config_nonisoc_xfer_ddma - prepare non ISOC DMA desc chain.
 * @hs_ep: The endpoint
 * @dma_buff: DMA address to use
 * @len: Length of the transfer
 *
 * This function will iterate over descriptor chain and fill its entries
 * with corresponding information based on transfer data.
 */
static void dwc2_gadget_config_nonisoc_xfer_ddma(struct dwc2_hsotg_ep *hs_ep,
						 dma_addr_t dma_buff,
						 unsigned int len)
{
	struct dwc2_hsotg *hsotg = hs_ep->parent;
	int dir_in = hs_ep->dir_in;
	struct dwc2_dma_desc *desc = hs_ep->desc_list;
	u32 mps = hs_ep->ep.maxpacket;
	u32 maxsize = 0;
	u32 offset = 0;
	u32 mask = 0;
	int i;

	maxsize = dwc2_gadget_get_desc_params(hs_ep, &mask);

	hs_ep->desc_count = (len / maxsize) +
				((len % maxsize) ? 1 : 0);
	if (len == 0)
		hs_ep->desc_count = 1;

	for (i = 0; i < hs_ep->desc_count; ++i) {
		desc->status = 0;
		desc->status |= (DEV_DMA_BUFF_STS_HBUSY
				 << DEV_DMA_BUFF_STS_SHIFT);

		if (len > maxsize) {
			if (!hs_ep->index && !dir_in)
				desc->status |= (DEV_DMA_L | DEV_DMA_IOC);

			desc->status |= (maxsize <<
						DEV_DMA_NBYTES_SHIFT & mask);
			desc->buf = dma_buff + offset;

			len -= maxsize;
			offset += maxsize;
		} else {
			desc->status |= (DEV_DMA_L | DEV_DMA_IOC);

			if (dir_in)
				desc->status |= (len % mps) ? DEV_DMA_SHORT :
					((hs_ep->send_zlp) ? DEV_DMA_SHORT : 0);
			if (len > maxsize)
				dev_err(hsotg->dev, "wrong len %d\n", len);

			desc->status |=
				len << DEV_DMA_NBYTES_SHIFT & mask;
			desc->buf = dma_buff + offset;
		}

		desc->status &= ~DEV_DMA_BUFF_STS_MASK;
		desc->status |= (DEV_DMA_BUFF_STS_HREADY
				 << DEV_DMA_BUFF_STS_SHIFT);
		desc++;
	}
}

/*
 * dwc2_gadget_fill_isoc_desc - fills next isochronous descriptor in chain.
 * @hs_ep: The isochronous endpoint.
 * @dma_buff: usb requests dma buffer.
 * @len: usb request transfer length.
 *
 * Finds out index of first free entry either in the bottom or up half of
 * descriptor chain depend on which is under SW control and not processed
 * by HW. Then fills that descriptor with the data of the arrived usb request,
 * frame info, sets Last and IOC bits increments next_desc. If filled
 * descriptor is not the first one, removes L bit from the previous descriptor
 * status.
 */
static int dwc2_gadget_fill_isoc_desc(struct dwc2_hsotg_ep *hs_ep,
				      dma_addr_t dma_buff, unsigned int len)
{
	struct dwc2_dma_desc *desc;
	struct dwc2_hsotg *hsotg = hs_ep->parent;
	u32 index;
	u32 maxsize = 0;
	u32 mask = 0;

	maxsize = dwc2_gadget_get_desc_params(hs_ep, &mask);
	if (len > maxsize) {
		dev_err(hsotg->dev, "wrong len %d\n", len);
		return -EINVAL;
	}

	/*
	 * If SW has already filled half of chain, then return and wait for
	 * the other chain to be processed by HW.
	 */
	if (hs_ep->next_desc == MAX_DMA_DESC_NUM_GENERIC / 2)
		return -EBUSY;

	/* Increment frame number by interval for IN */
	if (hs_ep->dir_in)
		dwc2_gadget_incr_frame_num(hs_ep);

	index = (MAX_DMA_DESC_NUM_GENERIC / 2) * hs_ep->isoc_chain_num +
		 hs_ep->next_desc;

	/* Sanity check of calculated index */
	if ((hs_ep->isoc_chain_num && index > MAX_DMA_DESC_NUM_GENERIC) ||
	    (!hs_ep->isoc_chain_num && index > MAX_DMA_DESC_NUM_GENERIC / 2)) {
		dev_err(hsotg->dev, "wrong index %d for iso chain\n", index);
		return -EINVAL;
	}

	desc = &hs_ep->desc_list[index];

	/* Clear L bit of previous desc if more than one entries in the chain */
	if (hs_ep->next_desc)
		hs_ep->desc_list[index - 1].status &= ~DEV_DMA_L;

	dev_dbg(hsotg->dev, "%s: Filling ep %d, dir %s isoc desc # %d\n",
		__func__, hs_ep->index, hs_ep->dir_in ? "in" : "out", index);

	desc->status = 0;
	desc->status |= (DEV_DMA_BUFF_STS_HBUSY	<< DEV_DMA_BUFF_STS_SHIFT);

	desc->buf = dma_buff;
	desc->status |= (DEV_DMA_L | DEV_DMA_IOC |
			 ((len << DEV_DMA_NBYTES_SHIFT) & mask));

	if (hs_ep->dir_in) {
		desc->status |= ((hs_ep->mc << DEV_DMA_ISOC_PID_SHIFT) &
				 DEV_DMA_ISOC_PID_MASK) |
				((len % hs_ep->ep.maxpacket) ?
				 DEV_DMA_SHORT : 0) |
				((hs_ep->target_frame <<
				  DEV_DMA_ISOC_FRNUM_SHIFT) &
				 DEV_DMA_ISOC_FRNUM_MASK);
	}

	desc->status &= ~DEV_DMA_BUFF_STS_MASK;
	desc->status |= (DEV_DMA_BUFF_STS_HREADY << DEV_DMA_BUFF_STS_SHIFT);

	/* Update index of last configured entry in the chain */
	hs_ep->next_desc++;

	return 0;
}

/*
 * dwc2_gadget_start_isoc_ddma - start isochronous transfer in DDMA
 * @hs_ep: The isochronous endpoint.
 *
 * Prepare first descriptor chain for isochronous endpoints. Afterwards
 * write DMA address to HW and enable the endpoint.
 *
 * Switch between descriptor chains via isoc_chain_num to give SW opportunity
 * to prepare second descriptor chain while first one is being processed by HW.
 */
static void dwc2_gadget_start_isoc_ddma(struct dwc2_hsotg_ep *hs_ep)
{
	struct dwc2_hsotg *hsotg = hs_ep->parent;
	struct dwc2_hsotg_req *hs_req, *treq;
	int index = hs_ep->index;
	int ret;
	u32 dma_reg;
	u32 depctl;
	u32 ctrl;

	if (list_empty(&hs_ep->queue)) {
		dev_dbg(hsotg->dev, "%s: No requests in queue\n", __func__);
		return;
	}

	list_for_each_entry_safe(hs_req, treq, &hs_ep->queue, queue) {
		ret = dwc2_gadget_fill_isoc_desc(hs_ep, hs_req->req.dma,
						 hs_req->req.length);
		if (ret) {
			dev_dbg(hsotg->dev, "%s: desc chain full\n", __func__);
			break;
		}
	}

	depctl = hs_ep->dir_in ? DIEPCTL(index) : DOEPCTL(index);
	dma_reg = hs_ep->dir_in ? DIEPDMA(index) : DOEPDMA(index);

	/* write descriptor chain address to control register */
	dwc2_writel(hs_ep->desc_list_dma, hsotg->regs + dma_reg);

	ctrl = dwc2_readl(hsotg->regs + depctl);
	ctrl |= DXEPCTL_EPENA | DXEPCTL_CNAK;
	dwc2_writel(ctrl, hsotg->regs + depctl);

	/* Switch ISOC descriptor chain number being processed by SW*/
	hs_ep->isoc_chain_num = (hs_ep->isoc_chain_num ^ 1) & 0x1;
	hs_ep->next_desc = 0;
}

/**
 * dwc2_hsotg_start_req - start a USB request from an endpoint's queue
 * @hsotg: The controller state.
 * @hs_ep: The endpoint to process a request for
 * @hs_req: The request to start.
 * @continuing: True if we are doing more for the current request.
 *
 * Start the given request running by setting the endpoint registers
 * appropriately, and writing any data to the FIFOs.
 */
static void dwc2_hsotg_start_req(struct dwc2_hsotg *hsotg,
				struct dwc2_hsotg_ep *hs_ep,
				struct dwc2_hsotg_req *hs_req,
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
	unsigned int dma_reg;

	if (index != 0) {
		if (hs_ep->req && !continuing) {
			dev_err(hsotg->dev, "%s: active request\n", __func__);
			WARN_ON(1);
			return;
		} else if (hs_ep->req != hs_req && continuing) {
			dev_err(hsotg->dev,
				"%s: continue different req\n", __func__);
			WARN_ON(1);
			return;
		}
	}

	dma_reg = dir_in ? DIEPDMA(index) : DOEPDMA(index);
	epctrl_reg = dir_in ? DIEPCTL(index) : DOEPCTL(index);
	epsize_reg = dir_in ? DIEPTSIZ(index) : DOEPTSIZ(index);

	dev_dbg(hsotg->dev, "%s: DxEPCTL=0x%08x, ep %d, dir %s\n",
		__func__, dwc2_readl(hsotg->regs + epctrl_reg), index,
		hs_ep->dir_in ? "in" : "out");

	/* If endpoint is stalled, we will restart request later */
	ctrl = dwc2_readl(hsotg->regs + epctrl_reg);

	if (index && ctrl & DXEPCTL_STALL) {
		dev_warn(hsotg->dev, "%s: ep%d is stalled\n", __func__, index);
		return;
	}

	length = ureq->length - ureq->actual;
	dev_dbg(hsotg->dev, "ureq->length:%d ureq->actual:%d\n",
		ureq->length, ureq->actual);

	if (!using_desc_dma(hsotg))
		maxreq = get_ep_limit(hs_ep);
	else
		maxreq = dwc2_gadget_get_chain_limit(hs_ep);

	if (length > maxreq) {
		int round = maxreq % hs_ep->ep.maxpacket;

		dev_dbg(hsotg->dev, "%s: length %d, max-req %d, r %d\n",
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

	if (hs_ep->isochronous && length > (hs_ep->mc * hs_ep->ep.maxpacket)) {
		dev_err(hsotg->dev, "req length > maxpacket*mc\n");
		return;
	}

	if (dir_in && index != 0)
		if (hs_ep->isochronous)
			epsize = DXEPTSIZ_MC(packets);
		else
			epsize = DXEPTSIZ_MC(1);
	else
		epsize = 0;

	/*
	 * zero length packet should be programmed on its own and should not
	 * be counted in DIEPTSIZ.PktCnt with other packets.
	 */
	if (dir_in && ureq->zero && !continuing) {
		/* Test if zlp is actually required. */
		if ((ureq->length >= hs_ep->ep.maxpacket) &&
					!(ureq->length % hs_ep->ep.maxpacket))
			hs_ep->send_zlp = 1;
	}

	epsize |= DXEPTSIZ_PKTCNT(packets);
	epsize |= DXEPTSIZ_XFERSIZE(length);

	dev_dbg(hsotg->dev, "%s: %d@%d/%d, 0x%08x => 0x%08x\n",
		__func__, packets, length, ureq->length, epsize, epsize_reg);

	/* store the request as the current one we're doing */
	hs_ep->req = hs_req;

	if (using_desc_dma(hsotg)) {
		u32 offset = 0;
		u32 mps = hs_ep->ep.maxpacket;

		/* Adjust length: EP0 - MPS, other OUT EPs - multiple of MPS */
		if (!dir_in) {
			if (!index)
				length = mps;
			else if (length % mps)
				length += (mps - (length % mps));
		}

		/*
		 * If more data to send, adjust DMA for EP0 out data stage.
		 * ureq->dma stays unchanged, hence increment it by already
		 * passed passed data count before starting new transaction.
		 */
		if (!index && hsotg->ep0_state == DWC2_EP0_DATA_OUT &&
		    continuing)
			offset = ureq->actual;

		/* Fill DDMA chain entries */
		dwc2_gadget_config_nonisoc_xfer_ddma(hs_ep, ureq->dma + offset,
						     length);

		/* write descriptor chain address to control register */
		dwc2_writel(hs_ep->desc_list_dma, hsotg->regs + dma_reg);

		dev_dbg(hsotg->dev, "%s: %08x pad => 0x%08x\n",
			__func__, (u32)hs_ep->desc_list_dma, dma_reg);
	} else {
		/* write size / packets */
		dwc2_writel(epsize, hsotg->regs + epsize_reg);

		if (using_dma(hsotg) && !continuing && (length != 0)) {
			/*
			 * write DMA address to control register, buffer
			 * already synced by dwc2_hsotg_ep_queue().
			 */

			dwc2_writel(ureq->dma, hsotg->regs + dma_reg);

			dev_dbg(hsotg->dev, "%s: %pad => 0x%08x\n",
				__func__, &ureq->dma, dma_reg);
		}
	}

	if (hs_ep->isochronous && hs_ep->interval == 1) {
		hs_ep->target_frame = dwc2_hsotg_read_frameno(hsotg);
		dwc2_gadget_incr_frame_num(hs_ep);

		if (hs_ep->target_frame & 0x1)
			ctrl |= DXEPCTL_SETODDFR;
		else
			ctrl |= DXEPCTL_SETEVENFR;
	}

	ctrl |= DXEPCTL_EPENA;	/* ensure ep enabled */

	dev_dbg(hsotg->dev, "ep0 state:%d\n", hsotg->ep0_state);

	/* For Setup request do not clear NAK */
	if (!(index == 0 && hsotg->ep0_state == DWC2_EP0_SETUP))
		ctrl |= DXEPCTL_CNAK;	/* clear NAK set by core */

	dev_dbg(hsotg->dev, "%s: DxEPCTL=0x%08x\n", __func__, ctrl);
	dwc2_writel(ctrl, hsotg->regs + epctrl_reg);

	/*
	 * set these, it seems that DMA support increments past the end
	 * of the packet buffer so we need to calculate the length from
	 * this information.
	 */
	hs_ep->size_loaded = length;
	hs_ep->last_load = ureq->actual;

	if (dir_in && !using_dma(hsotg)) {
		/* set these anyway, we may need them for non-periodic in */
		hs_ep->fifo_load = 0;

		dwc2_hsotg_write_fifo(hsotg, hs_ep, hs_req);
	}

	/*
	 * Note, trying to clear the NAK here causes problems with transmit
	 * on the S3C6400 ending up with the TXFIFO becoming full.
	 */

	/* check ep is enabled */
	if (!(dwc2_readl(hsotg->regs + epctrl_reg) & DXEPCTL_EPENA))
		dev_dbg(hsotg->dev,
			 "ep%d: failed to become enabled (DXEPCTL=0x%08x)?\n",
			 index, dwc2_readl(hsotg->regs + epctrl_reg));

	dev_dbg(hsotg->dev, "%s: DXEPCTL=0x%08x\n",
		__func__, dwc2_readl(hsotg->regs + epctrl_reg));

	/* enable ep interrupts */
	dwc2_hsotg_ctrl_epint(hsotg, hs_ep->index, hs_ep->dir_in, 1);
}

/**
 * dwc2_hsotg_map_dma - map the DMA memory being used for the request
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
static int dwc2_hsotg_map_dma(struct dwc2_hsotg *hsotg,
			     struct dwc2_hsotg_ep *hs_ep,
			     struct usb_request *req)
{
	int ret;

	ret = usb_gadget_map_request(&hsotg->gadget, req, hs_ep->dir_in);
	if (ret)
		goto dma_error;

	return 0;

dma_error:
	dev_err(hsotg->dev, "%s: failed to map buffer %p, %d bytes\n",
		__func__, req->buf, req->length);

	return -EIO;
}

static int dwc2_hsotg_handle_unaligned_buf_start(struct dwc2_hsotg *hsotg,
	struct dwc2_hsotg_ep *hs_ep, struct dwc2_hsotg_req *hs_req)
{
	void *req_buf = hs_req->req.buf;

	/* If dma is not being used or buffer is aligned */
	if (!using_dma(hsotg) || !((long)req_buf & 3))
		return 0;

	WARN_ON(hs_req->saved_req_buf);

	dev_dbg(hsotg->dev, "%s: %s: buf=%p length=%d\n", __func__,
			hs_ep->ep.name, req_buf, hs_req->req.length);

	hs_req->req.buf = kmalloc(hs_req->req.length, GFP_ATOMIC);
	if (!hs_req->req.buf) {
		hs_req->req.buf = req_buf;
		dev_err(hsotg->dev,
			"%s: unable to allocate memory for bounce buffer\n",
			__func__);
		return -ENOMEM;
	}

	/* Save actual buffer */
	hs_req->saved_req_buf = req_buf;

	if (hs_ep->dir_in)
		memcpy(hs_req->req.buf, req_buf, hs_req->req.length);
	return 0;
}

static void dwc2_hsotg_handle_unaligned_buf_complete(struct dwc2_hsotg *hsotg,
	struct dwc2_hsotg_ep *hs_ep, struct dwc2_hsotg_req *hs_req)
{
	/* If dma is not being used or buffer was aligned */
	if (!using_dma(hsotg) || !hs_req->saved_req_buf)
		return;

	dev_dbg(hsotg->dev, "%s: %s: status=%d actual-length=%d\n", __func__,
		hs_ep->ep.name, hs_req->req.status, hs_req->req.actual);

	/* Copy data from bounce buffer on successful out transfer */
	if (!hs_ep->dir_in && !hs_req->req.status)
		memcpy(hs_req->saved_req_buf, hs_req->req.buf,
							hs_req->req.actual);

	/* Free bounce buffer */
	kfree(hs_req->req.buf);

	hs_req->req.buf = hs_req->saved_req_buf;
	hs_req->saved_req_buf = NULL;
}

/**
 * dwc2_gadget_target_frame_elapsed - Checks target frame
 * @hs_ep: The driver endpoint to check
 *
 * Returns 1 if targeted frame elapsed. If returned 1 then we need to drop
 * corresponding transfer.
 */
static bool dwc2_gadget_target_frame_elapsed(struct dwc2_hsotg_ep *hs_ep)
{
	struct dwc2_hsotg *hsotg = hs_ep->parent;
	u32 target_frame = hs_ep->target_frame;
	u32 current_frame = dwc2_hsotg_read_frameno(hsotg);
	bool frame_overrun = hs_ep->frame_overrun;

	if (!frame_overrun && current_frame >= target_frame)
		return true;

	if (frame_overrun && current_frame >= target_frame &&
	    ((current_frame - target_frame) < DSTS_SOFFN_LIMIT / 2))
		return true;

	return false;
}

/*
 * dwc2_gadget_set_ep0_desc_chain - Set EP's desc chain pointers
 * @hsotg: The driver state
 * @hs_ep: the ep descriptor chain is for
 *
 * Called to update EP0 structure's pointers depend on stage of
 * control transfer.
 */
static int dwc2_gadget_set_ep0_desc_chain(struct dwc2_hsotg *hsotg,
					  struct dwc2_hsotg_ep *hs_ep)
{
	switch (hsotg->ep0_state) {
	case DWC2_EP0_SETUP:
	case DWC2_EP0_STATUS_OUT:
		hs_ep->desc_list = hsotg->setup_desc[0];
		hs_ep->desc_list_dma = hsotg->setup_desc_dma[0];
		break;
	case DWC2_EP0_DATA_IN:
	case DWC2_EP0_STATUS_IN:
		hs_ep->desc_list = hsotg->ctrl_in_desc;
		hs_ep->desc_list_dma = hsotg->ctrl_in_desc_dma;
		break;
	case DWC2_EP0_DATA_OUT:
		hs_ep->desc_list = hsotg->ctrl_out_desc;
		hs_ep->desc_list_dma = hsotg->ctrl_out_desc_dma;
		break;
	default:
		dev_err(hsotg->dev, "invalid EP 0 state in queue %d\n",
			hsotg->ep0_state);
		return -EINVAL;
	}

	return 0;
}

static int dwc2_hsotg_ep_queue(struct usb_ep *ep, struct usb_request *req,
			      gfp_t gfp_flags)
{
	struct dwc2_hsotg_req *hs_req = our_req(req);
	struct dwc2_hsotg_ep *hs_ep = our_ep(ep);
	struct dwc2_hsotg *hs = hs_ep->parent;
	bool first;
	int ret;

	dev_dbg(hs->dev, "%s: req %p: %d@%p, noi=%d, zero=%d, snok=%d\n",
		ep->name, req, req->length, req->buf, req->no_interrupt,
		req->zero, req->short_not_ok);

	/* Prevent new request submission when controller is suspended */
	if (hs->lx_state == DWC2_L2) {
		dev_dbg(hs->dev, "%s: don't submit request while suspended\n",
				__func__);
		return -EAGAIN;
	}

	/* initialise status of the request */
	INIT_LIST_HEAD(&hs_req->queue);
	req->actual = 0;
	req->status = -EINPROGRESS;

	ret = dwc2_hsotg_handle_unaligned_buf_start(hs, hs_ep, hs_req);
	if (ret)
		return ret;

	/* if we're using DMA, sync the buffers as necessary */
	if (using_dma(hs)) {
		ret = dwc2_hsotg_map_dma(hs, hs_ep, req);
		if (ret)
			return ret;
	}
	/* If using descriptor DMA configure EP0 descriptor chain pointers */
	if (using_desc_dma(hs) && !hs_ep->index) {
		ret = dwc2_gadget_set_ep0_desc_chain(hs, hs_ep);
		if (ret)
			return ret;
	}

	first = list_empty(&hs_ep->queue);
	list_add_tail(&hs_req->queue, &hs_ep->queue);

	/*
	 * Handle DDMA isochronous transfers separately - just add new entry
	 * to the half of descriptor chain that is not processed by HW.
	 * Transfer will be started once SW gets either one of NAK or
	 * OutTknEpDis interrupts.
	 */
	if (using_desc_dma(hs) && hs_ep->isochronous &&
	    hs_ep->target_frame != TARGET_FRAME_INITIAL) {
		ret = dwc2_gadget_fill_isoc_desc(hs_ep, hs_req->req.dma,
						 hs_req->req.length);
		if (ret)
			dev_dbg(hs->dev, "%s: ISO desc chain full\n", __func__);

		return 0;
	}

	if (first) {
		if (!hs_ep->isochronous) {
			dwc2_hsotg_start_req(hs, hs_ep, hs_req, false);
			return 0;
		}

		while (dwc2_gadget_target_frame_elapsed(hs_ep))
			dwc2_gadget_incr_frame_num(hs_ep);

		if (hs_ep->target_frame != TARGET_FRAME_INITIAL)
			dwc2_hsotg_start_req(hs, hs_ep, hs_req, false);
	}
	return 0;
}

static int dwc2_hsotg_ep_queue_lock(struct usb_ep *ep, struct usb_request *req,
			      gfp_t gfp_flags)
{
	struct dwc2_hsotg_ep *hs_ep = our_ep(ep);
	struct dwc2_hsotg *hs = hs_ep->parent;
	unsigned long flags = 0;
	int ret = 0;

	spin_lock_irqsave(&hs->lock, flags);
	ret = dwc2_hsotg_ep_queue(ep, req, gfp_flags);
	spin_unlock_irqrestore(&hs->lock, flags);

	return ret;
}

static void dwc2_hsotg_ep_free_request(struct usb_ep *ep,
				      struct usb_request *req)
{
	struct dwc2_hsotg_req *hs_req = our_req(req);

	kfree(hs_req);
}

/**
 * dwc2_hsotg_complete_oursetup - setup completion callback
 * @ep: The endpoint the request was on.
 * @req: The request completed.
 *
 * Called on completion of any requests the driver itself
 * submitted that need cleaning up.
 */
static void dwc2_hsotg_complete_oursetup(struct usb_ep *ep,
					struct usb_request *req)
{
	struct dwc2_hsotg_ep *hs_ep = our_ep(ep);
	struct dwc2_hsotg *hsotg = hs_ep->parent;

	dev_dbg(hsotg->dev, "%s: ep %p, req %p\n", __func__, ep, req);

	dwc2_hsotg_ep_free_request(ep, req);
}

/**
 * ep_from_windex - convert control wIndex value to endpoint
 * @hsotg: The driver state.
 * @windex: The control request wIndex field (in host order).
 *
 * Convert the given wIndex into a pointer to an driver endpoint
 * structure, or return NULL if it is not a valid endpoint.
 */
static struct dwc2_hsotg_ep *ep_from_windex(struct dwc2_hsotg *hsotg,
					   u32 windex)
{
	struct dwc2_hsotg_ep *ep;
	int dir = (windex & USB_DIR_IN) ? 1 : 0;
	int idx = windex & 0x7F;

	if (windex >= 0x100)
		return NULL;

	if (idx > hsotg->num_of_eps)
		return NULL;

	ep = index_to_ep(hsotg, idx, dir);

	if (idx && ep->dir_in != dir)
		return NULL;

	return ep;
}

/**
 * dwc2_hsotg_set_test_mode - Enable usb Test Modes
 * @hsotg: The driver state.
 * @testmode: requested usb test mode
 * Enable usb Test Mode requested by the Host.
 */
int dwc2_hsotg_set_test_mode(struct dwc2_hsotg *hsotg, int testmode)
{
	int dctl = dwc2_readl(hsotg->regs + DCTL);

	dctl &= ~DCTL_TSTCTL_MASK;
	switch (testmode) {
	case TEST_J:
	case TEST_K:
	case TEST_SE0_NAK:
	case TEST_PACKET:
	case TEST_FORCE_EN:
		dctl |= testmode << DCTL_TSTCTL_SHIFT;
		break;
	default:
		return -EINVAL;
	}
	dwc2_writel(dctl, hsotg->regs + DCTL);
	return 0;
}

/**
 * dwc2_hsotg_send_reply - send reply to control request
 * @hsotg: The device state
 * @ep: Endpoint 0
 * @buff: Buffer for request
 * @length: Length of reply.
 *
 * Create a request and queue it on the given endpoint. This is useful as
 * an internal method of sending replies to certain control requests, etc.
 */
static int dwc2_hsotg_send_reply(struct dwc2_hsotg *hsotg,
				struct dwc2_hsotg_ep *ep,
				void *buff,
				int length)
{
	struct usb_request *req;
	int ret;

	dev_dbg(hsotg->dev, "%s: buff %p, len %d\n", __func__, buff, length);

	req = dwc2_hsotg_ep_alloc_request(&ep->ep, GFP_ATOMIC);
	hsotg->ep0_reply = req;
	if (!req) {
		dev_warn(hsotg->dev, "%s: cannot alloc req\n", __func__);
		return -ENOMEM;
	}

	req->buf = hsotg->ep0_buff;
	req->length = length;
	/*
	 * zero flag is for sending zlp in DATA IN stage. It has no impact on
	 * STATUS stage.
	 */
	req->zero = 0;
	req->complete = dwc2_hsotg_complete_oursetup;

	if (length)
		memcpy(req->buf, buff, length);

	ret = dwc2_hsotg_ep_queue(&ep->ep, req, GFP_ATOMIC);
	if (ret) {
		dev_warn(hsotg->dev, "%s: cannot queue req\n", __func__);
		return ret;
	}

	return 0;
}

/**
 * dwc2_hsotg_process_req_status - process request GET_STATUS
 * @hsotg: The device state
 * @ctrl: USB control request
 */
static int dwc2_hsotg_process_req_status(struct dwc2_hsotg *hsotg,
					struct usb_ctrlrequest *ctrl)
{
	struct dwc2_hsotg_ep *ep0 = hsotg->eps_out[0];
	struct dwc2_hsotg_ep *ep;
	__le16 reply;
	int ret;

	dev_dbg(hsotg->dev, "%s: USB_REQ_GET_STATUS\n", __func__);

	if (!ep0->dir_in) {
		dev_warn(hsotg->dev, "%s: direction out?\n", __func__);
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
		ep = ep_from_windex(hsotg, le16_to_cpu(ctrl->wIndex));
		if (!ep)
			return -ENOENT;

		reply = cpu_to_le16(ep->halted ? 1 : 0);
		break;

	default:
		return 0;
	}

	if (le16_to_cpu(ctrl->wLength) != 2)
		return -EINVAL;

	ret = dwc2_hsotg_send_reply(hsotg, ep0, &reply, 2);
	if (ret) {
		dev_err(hsotg->dev, "%s: failed to send reply\n", __func__);
		return ret;
	}

	return 1;
}

static int dwc2_hsotg_ep_sethalt(struct usb_ep *ep, int value, bool now);

/**
 * get_ep_head - return the first request on the endpoint
 * @hs_ep: The controller endpoint to get
 *
 * Get the first request on the endpoint.
 */
static struct dwc2_hsotg_req *get_ep_head(struct dwc2_hsotg_ep *hs_ep)
{
	return list_first_entry_or_null(&hs_ep->queue, struct dwc2_hsotg_req,
					queue);
}

/**
 * dwc2_gadget_start_next_request - Starts next request from ep queue
 * @hs_ep: Endpoint structure
 *
 * If queue is empty and EP is ISOC-OUT - unmasks OUTTKNEPDIS which is masked
 * in its handler. Hence we need to unmask it here to be able to do
 * resynchronization.
 */
static void dwc2_gadget_start_next_request(struct dwc2_hsotg_ep *hs_ep)
{
	u32 mask;
	struct dwc2_hsotg *hsotg = hs_ep->parent;
	int dir_in = hs_ep->dir_in;
	struct dwc2_hsotg_req *hs_req;
	u32 epmsk_reg = dir_in ? DIEPMSK : DOEPMSK;

	if (!list_empty(&hs_ep->queue)) {
		hs_req = get_ep_head(hs_ep);
		dwc2_hsotg_start_req(hsotg, hs_ep, hs_req, false);
		return;
	}
	if (!hs_ep->isochronous)
		return;

	if (dir_in) {
		dev_dbg(hsotg->dev, "%s: No more ISOC-IN requests\n",
			__func__);
	} else {
		dev_dbg(hsotg->dev, "%s: No more ISOC-OUT requests\n",
			__func__);
		mask = dwc2_readl(hsotg->regs + epmsk_reg);
		mask |= DOEPMSK_OUTTKNEPDISMSK;
		dwc2_writel(mask, hsotg->regs + epmsk_reg);
	}
}

/**
 * dwc2_hsotg_process_req_feature - process request {SET,CLEAR}_FEATURE
 * @hsotg: The device state
 * @ctrl: USB control request
 */
static int dwc2_hsotg_process_req_feature(struct dwc2_hsotg *hsotg,
					 struct usb_ctrlrequest *ctrl)
{
	struct dwc2_hsotg_ep *ep0 = hsotg->eps_out[0];
	struct dwc2_hsotg_req *hs_req;
	bool set = (ctrl->bRequest == USB_REQ_SET_FEATURE);
	struct dwc2_hsotg_ep *ep;
	int ret;
	bool halted;
	u32 recip;
	u32 wValue;
	u32 wIndex;

	dev_dbg(hsotg->dev, "%s: %s_FEATURE\n",
		__func__, set ? "SET" : "CLEAR");

	wValue = le16_to_cpu(ctrl->wValue);
	wIndex = le16_to_cpu(ctrl->wIndex);
	recip = ctrl->bRequestType & USB_RECIP_MASK;

	switch (recip) {
	case USB_RECIP_DEVICE:
		switch (wValue) {
		case USB_DEVICE_TEST_MODE:
			if ((wIndex & 0xff) != 0)
				return -EINVAL;
			if (!set)
				return -EINVAL;

			hsotg->test_mode = wIndex >> 8;
			ret = dwc2_hsotg_send_reply(hsotg, ep0, NULL, 0);
			if (ret) {
				dev_err(hsotg->dev,
					"%s: failed to send reply\n", __func__);
				return ret;
			}
			break;
		default:
			return -ENOENT;
		}
		break;

	case USB_RECIP_ENDPOINT:
		ep = ep_from_windex(hsotg, wIndex);
		if (!ep) {
			dev_dbg(hsotg->dev, "%s: no endpoint for 0x%04x\n",
				__func__, wIndex);
			return -ENOENT;
		}

		switch (wValue) {
		case USB_ENDPOINT_HALT:
			halted = ep->halted;

			dwc2_hsotg_ep_sethalt(&ep->ep, set, true);

			ret = dwc2_hsotg_send_reply(hsotg, ep0, NULL, 0);
			if (ret) {
				dev_err(hsotg->dev,
					"%s: failed to send reply\n", __func__);
				return ret;
			}

			/*
			 * we have to complete all requests for ep if it was
			 * halted, and the halt was cleared by CLEAR_FEATURE
			 */

			if (!set && halted) {
				/*
				 * If we have request in progress,
				 * then complete it
				 */
				if (ep->req) {
					hs_req = ep->req;
					ep->req = NULL;
					list_del_init(&hs_req->queue);
					if (hs_req->req.complete) {
						spin_unlock(&hsotg->lock);
						usb_gadget_giveback_request(
							&ep->ep, &hs_req->req);
						spin_lock(&hsotg->lock);
					}
				}

				/* If we have pending request, then start it */
				if (!ep->req) {
					dwc2_gadget_start_next_request(ep);
				}
			}

			break;

		default:
			return -ENOENT;
		}
		break;
	default:
		return -ENOENT;
	}
	return 1;
}

static void dwc2_hsotg_enqueue_setup(struct dwc2_hsotg *hsotg);

/**
 * dwc2_hsotg_stall_ep0 - stall ep0
 * @hsotg: The device state
 *
 * Set stall for ep0 as response for setup request.
 */
static void dwc2_hsotg_stall_ep0(struct dwc2_hsotg *hsotg)
{
	struct dwc2_hsotg_ep *ep0 = hsotg->eps_out[0];
	u32 reg;
	u32 ctrl;

	dev_dbg(hsotg->dev, "ep0 stall (dir=%d)\n", ep0->dir_in);
	reg = (ep0->dir_in) ? DIEPCTL0 : DOEPCTL0;

	/*
	 * DxEPCTL_Stall will be cleared by EP once it has
	 * taken effect, so no need to clear later.
	 */

	ctrl = dwc2_readl(hsotg->regs + reg);
	ctrl |= DXEPCTL_STALL;
	ctrl |= DXEPCTL_CNAK;
	dwc2_writel(ctrl, hsotg->regs + reg);

	dev_dbg(hsotg->dev,
		"written DXEPCTL=0x%08x to %08x (DXEPCTL=0x%08x)\n",
		ctrl, reg, dwc2_readl(hsotg->regs + reg));

	 /*
	  * complete won't be called, so we enqueue
	  * setup request here
	  */
	 dwc2_hsotg_enqueue_setup(hsotg);
}

/**
 * dwc2_hsotg_process_control - process a control request
 * @hsotg: The device state
 * @ctrl: The control request received
 *
 * The controller has received the SETUP phase of a control request, and
 * needs to work out what to do next (and whether to pass it on to the
 * gadget driver).
 */
static void dwc2_hsotg_process_control(struct dwc2_hsotg *hsotg,
				      struct usb_ctrlrequest *ctrl)
{
	struct dwc2_hsotg_ep *ep0 = hsotg->eps_out[0];
	int ret = 0;
	u32 dcfg;

	dev_dbg(hsotg->dev,
		"ctrl Type=%02x, Req=%02x, V=%04x, I=%04x, L=%04x\n",
		ctrl->bRequestType, ctrl->bRequest, ctrl->wValue,
		ctrl->wIndex, ctrl->wLength);

	if (ctrl->wLength == 0) {
		ep0->dir_in = 1;
		hsotg->ep0_state = DWC2_EP0_STATUS_IN;
	} else if (ctrl->bRequestType & USB_DIR_IN) {
		ep0->dir_in = 1;
		hsotg->ep0_state = DWC2_EP0_DATA_IN;
	} else {
		ep0->dir_in = 0;
		hsotg->ep0_state = DWC2_EP0_DATA_OUT;
	}

	if ((ctrl->bRequestType & USB_TYPE_MASK) == USB_TYPE_STANDARD) {
		switch (ctrl->bRequest) {
		case USB_REQ_SET_ADDRESS:
			hsotg->connected = 1;
			dcfg = dwc2_readl(hsotg->regs + DCFG);
			dcfg &= ~DCFG_DEVADDR_MASK;
			dcfg |= (le16_to_cpu(ctrl->wValue) <<
				 DCFG_DEVADDR_SHIFT) & DCFG_DEVADDR_MASK;
			dwc2_writel(dcfg, hsotg->regs + DCFG);

			dev_info(hsotg->dev, "new address %d\n", ctrl->wValue);

			ret = dwc2_hsotg_send_reply(hsotg, ep0, NULL, 0);
			return;

		case USB_REQ_GET_STATUS:
			ret = dwc2_hsotg_process_req_status(hsotg, ctrl);
			break;

		case USB_REQ_CLEAR_FEATURE:
		case USB_REQ_SET_FEATURE:
			ret = dwc2_hsotg_process_req_feature(hsotg, ctrl);
			break;
		}
	}

	/* as a fallback, try delivering it to the driver to deal with */

	if (ret == 0 && hsotg->driver) {
		spin_unlock(&hsotg->lock);
		ret = hsotg->driver->setup(&hsotg->gadget, ctrl);
		spin_lock(&hsotg->lock);
		if (ret < 0)
			dev_dbg(hsotg->dev, "driver->setup() ret %d\n", ret);
	}

	/*
	 * the request is either unhandlable, or is not formatted correctly
	 * so respond with a STALL for the status stage to indicate failure.
	 */

	if (ret < 0)
		dwc2_hsotg_stall_ep0(hsotg);
}

/**
 * dwc2_hsotg_complete_setup - completion of a setup transfer
 * @ep: The endpoint the request was on.
 * @req: The request completed.
 *
 * Called on completion of any requests the driver itself submitted for
 * EP0 setup packets
 */
static void dwc2_hsotg_complete_setup(struct usb_ep *ep,
				     struct usb_request *req)
{
	struct dwc2_hsotg_ep *hs_ep = our_ep(ep);
	struct dwc2_hsotg *hsotg = hs_ep->parent;

	if (req->status < 0) {
		dev_dbg(hsotg->dev, "%s: failed %d\n", __func__, req->status);
		return;
	}

	spin_lock(&hsotg->lock);
	if (req->actual == 0)
		dwc2_hsotg_enqueue_setup(hsotg);
	else
		dwc2_hsotg_process_control(hsotg, req->buf);
	spin_unlock(&hsotg->lock);
}

/**
 * dwc2_hsotg_enqueue_setup - start a request for EP0 packets
 * @hsotg: The device state.
 *
 * Enqueue a request on EP0 if necessary to received any SETUP packets
 * received from the host.
 */
static void dwc2_hsotg_enqueue_setup(struct dwc2_hsotg *hsotg)
{
	struct usb_request *req = hsotg->ctrl_req;
	struct dwc2_hsotg_req *hs_req = our_req(req);
	int ret;

	dev_dbg(hsotg->dev, "%s: queueing setup request\n", __func__);

	req->zero = 0;
	req->length = 8;
	req->buf = hsotg->ctrl_buff;
	req->complete = dwc2_hsotg_complete_setup;

	if (!list_empty(&hs_req->queue)) {
		dev_dbg(hsotg->dev, "%s already queued???\n", __func__);
		return;
	}

	hsotg->eps_out[0]->dir_in = 0;
	hsotg->eps_out[0]->send_zlp = 0;
	hsotg->ep0_state = DWC2_EP0_SETUP;

	ret = dwc2_hsotg_ep_queue(&hsotg->eps_out[0]->ep, req, GFP_ATOMIC);
	if (ret < 0) {
		dev_err(hsotg->dev, "%s: failed queue (%d)\n", __func__, ret);
		/*
		 * Don't think there's much we can do other than watch the
		 * driver fail.
		 */
	}
}

static void dwc2_hsotg_program_zlp(struct dwc2_hsotg *hsotg,
					struct dwc2_hsotg_ep *hs_ep)
{
	u32 ctrl;
	u8 index = hs_ep->index;
	u32 epctl_reg = hs_ep->dir_in ? DIEPCTL(index) : DOEPCTL(index);
	u32 epsiz_reg = hs_ep->dir_in ? DIEPTSIZ(index) : DOEPTSIZ(index);

	if (hs_ep->dir_in)
		dev_dbg(hsotg->dev, "Sending zero-length packet on ep%d\n",
			index);
	else
		dev_dbg(hsotg->dev, "Receiving zero-length packet on ep%d\n",
			index);
	if (using_desc_dma(hsotg)) {
		/* Not specific buffer needed for ep0 ZLP */
		dma_addr_t dma = hs_ep->desc_list_dma;

		dwc2_gadget_set_ep0_desc_chain(hsotg, hs_ep);
		dwc2_gadget_config_nonisoc_xfer_ddma(hs_ep, dma, 0);
	} else {
		dwc2_writel(DXEPTSIZ_MC(1) | DXEPTSIZ_PKTCNT(1) |
			    DXEPTSIZ_XFERSIZE(0), hsotg->regs +
			    epsiz_reg);
	}

	ctrl = dwc2_readl(hsotg->regs + epctl_reg);
	ctrl |= DXEPCTL_CNAK;  /* clear NAK set by core */
	ctrl |= DXEPCTL_EPENA; /* ensure ep enabled */
	ctrl |= DXEPCTL_USBACTEP;
	dwc2_writel(ctrl, hsotg->regs + epctl_reg);
}

/**
 * dwc2_hsotg_complete_request - complete a request given to us
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
static void dwc2_hsotg_complete_request(struct dwc2_hsotg *hsotg,
				       struct dwc2_hsotg_ep *hs_ep,
				       struct dwc2_hsotg_req *hs_req,
				       int result)
{

	if (!hs_req) {
		dev_dbg(hsotg->dev, "%s: nothing to complete?\n", __func__);
		return;
	}

	dev_dbg(hsotg->dev, "complete: ep %p %s, req %p, %d => %p\n",
		hs_ep, hs_ep->ep.name, hs_req, result, hs_req->req.complete);

	/*
	 * only replace the status if we've not already set an error
	 * from a previous transaction
	 */

	if (hs_req->req.status == -EINPROGRESS)
		hs_req->req.status = result;

	if (using_dma(hsotg))
		dwc2_hsotg_unmap_dma(hsotg, hs_ep, hs_req);

	dwc2_hsotg_handle_unaligned_buf_complete(hsotg, hs_ep, hs_req);

	hs_ep->req = NULL;
	list_del_init(&hs_req->queue);

	/*
	 * call the complete request with the locks off, just in case the
	 * request tries to queue more work for this endpoint.
	 */

	if (hs_req->req.complete) {
		spin_unlock(&hsotg->lock);
		usb_gadget_giveback_request(&hs_ep->ep, &hs_req->req);
		spin_lock(&hsotg->lock);
	}

	/* In DDMA don't need to proceed to starting of next ISOC request */
	if (using_desc_dma(hsotg) && hs_ep->isochronous)
		return;

	/*
	 * Look to see if there is anything else to do. Note, the completion
	 * of the previous request may have caused a new request to be started
	 * so be careful when doing this.
	 */

	if (!hs_ep->req && result >= 0) {
		dwc2_gadget_start_next_request(hs_ep);
	}
}

/*
 * dwc2_gadget_complete_isoc_request_ddma - complete an isoc request in DDMA
 * @hs_ep: The endpoint the request was on.
 *
 * Get first request from the ep queue, determine descriptor on which complete
 * happened. SW based on isoc_chain_num discovers which half of the descriptor
 * chain is currently in use by HW, adjusts dma_address and calculates index
 * of completed descriptor based on the value of DEPDMA register. Update actual
 * length of request, giveback to gadget.
 */
static void dwc2_gadget_complete_isoc_request_ddma(struct dwc2_hsotg_ep *hs_ep)
{
	struct dwc2_hsotg *hsotg = hs_ep->parent;
	struct dwc2_hsotg_req *hs_req;
	struct usb_request *ureq;
	int index;
	dma_addr_t dma_addr;
	u32 dma_reg;
	u32 depdma;
	u32 desc_sts;
	u32 mask;

	hs_req = get_ep_head(hs_ep);
	if (!hs_req) {
		dev_warn(hsotg->dev, "%s: ISOC EP queue empty\n", __func__);
		return;
	}
	ureq = &hs_req->req;

	dma_addr = hs_ep->desc_list_dma;

	/*
	 * If lower half of  descriptor chain is currently use by SW,
	 * that means higher half is being processed by HW, so shift
	 * DMA address to higher half of descriptor chain.
	 */
	if (!hs_ep->isoc_chain_num)
		dma_addr += sizeof(struct dwc2_dma_desc) *
			    (MAX_DMA_DESC_NUM_GENERIC / 2);

	dma_reg = hs_ep->dir_in ? DIEPDMA(hs_ep->index) : DOEPDMA(hs_ep->index);
	depdma = dwc2_readl(hsotg->regs + dma_reg);

	index = (depdma - dma_addr) / sizeof(struct dwc2_dma_desc) - 1;
	desc_sts = hs_ep->desc_list[index].status;

	mask = hs_ep->dir_in ? DEV_DMA_ISOC_TX_NBYTES_MASK :
	       DEV_DMA_ISOC_RX_NBYTES_MASK;
	ureq->actual = ureq->length -
		       ((desc_sts & mask) >> DEV_DMA_ISOC_NBYTES_SHIFT);

	/* Adjust actual length for ISOC Out if length is not align of 4 */
	if (!hs_ep->dir_in && ureq->length & 0x3)
		ureq->actual += 4 - (ureq->length & 0x3);

	dwc2_hsotg_complete_request(hsotg, hs_ep, hs_req, 0);
}

/*
 * dwc2_gadget_start_next_isoc_ddma - start next isoc request, if any.
 * @hs_ep: The isochronous endpoint to be re-enabled.
 *
 * If ep has been disabled due to last descriptor servicing (IN endpoint) or
 * BNA (OUT endpoint) check the status of other half of descriptor chain that
 * was under SW control till HW was busy and restart the endpoint if needed.
 */
static void dwc2_gadget_start_next_isoc_ddma(struct dwc2_hsotg_ep *hs_ep)
{
	struct dwc2_hsotg *hsotg = hs_ep->parent;
	u32 depctl;
	u32 dma_reg;
	u32 ctrl;
	u32 dma_addr = hs_ep->desc_list_dma;
	unsigned char index = hs_ep->index;

	dma_reg = hs_ep->dir_in ? DIEPDMA(index) : DOEPDMA(index);
	depctl = hs_ep->dir_in ? DIEPCTL(index) : DOEPCTL(index);

	ctrl = dwc2_readl(hsotg->regs + depctl);

	/*
	 * EP was disabled if HW has processed last descriptor or BNA was set.
	 * So restart ep if SW has prepared new descriptor chain in ep_queue
	 * routine while HW was busy.
	 */
	if (!(ctrl & DXEPCTL_EPENA)) {
		if (!hs_ep->next_desc) {
			dev_dbg(hsotg->dev, "%s: No more ISOC requests\n",
				__func__);
			return;
		}

		dma_addr += sizeof(struct dwc2_dma_desc) *
			    (MAX_DMA_DESC_NUM_GENERIC / 2) *
			    hs_ep->isoc_chain_num;
		dwc2_writel(dma_addr, hsotg->regs + dma_reg);

		ctrl |= DXEPCTL_EPENA | DXEPCTL_CNAK;
		dwc2_writel(ctrl, hsotg->regs + depctl);

		/* Switch ISOC descriptor chain number being processed by SW*/
		hs_ep->isoc_chain_num = (hs_ep->isoc_chain_num ^ 1) & 0x1;
		hs_ep->next_desc = 0;

		dev_dbg(hsotg->dev, "%s: Restarted isochronous endpoint\n",
			__func__);
	}
}

/**
 * dwc2_hsotg_rx_data - receive data from the FIFO for an endpoint
 * @hsotg: The device state.
 * @ep_idx: The endpoint index for the data
 * @size: The size of data in the fifo, in bytes
 *
 * The FIFO status shows there is data to read from the FIFO for a given
 * endpoint, so sort out whether we need to read the data into a request
 * that has been made for that endpoint.
 */
static void dwc2_hsotg_rx_data(struct dwc2_hsotg *hsotg, int ep_idx, int size)
{
	struct dwc2_hsotg_ep *hs_ep = hsotg->eps_out[ep_idx];
	struct dwc2_hsotg_req *hs_req = hs_ep->req;
	void __iomem *fifo = hsotg->regs + EPFIFO(ep_idx);
	int to_read;
	int max_req;
	int read_ptr;


	if (!hs_req) {
		u32 epctl = dwc2_readl(hsotg->regs + DOEPCTL(ep_idx));
		int ptr;

		dev_dbg(hsotg->dev,
			 "%s: FIFO %d bytes on ep%d but no req (DXEPCTl=0x%08x)\n",
			 __func__, size, ep_idx, epctl);

		/* dump the data from the FIFO, we've nothing we can do */
		for (ptr = 0; ptr < size; ptr += 4)
			(void)dwc2_readl(fifo);

		return;
	}

	to_read = size;
	read_ptr = hs_req->req.actual;
	max_req = hs_req->req.length - read_ptr;

	dev_dbg(hsotg->dev, "%s: read %d/%d, done %d/%d\n",
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
	ioread32_rep(fifo, hs_req->req.buf + read_ptr, to_read);
}

/**
 * dwc2_hsotg_ep0_zlp - send/receive zero-length packet on control endpoint
 * @hsotg: The device instance
 * @dir_in: If IN zlp
 *
 * Generate a zero-length IN packet request for terminating a SETUP
 * transaction.
 *
 * Note, since we don't write any data to the TxFIFO, then it is
 * currently believed that we do not need to wait for any space in
 * the TxFIFO.
 */
static void dwc2_hsotg_ep0_zlp(struct dwc2_hsotg *hsotg, bool dir_in)
{
	/* eps_out[0] is used in both directions */
	hsotg->eps_out[0]->dir_in = dir_in;
	hsotg->ep0_state = dir_in ? DWC2_EP0_STATUS_IN : DWC2_EP0_STATUS_OUT;

	dwc2_hsotg_program_zlp(hsotg, hsotg->eps_out[0]);
}

static void dwc2_hsotg_change_ep_iso_parity(struct dwc2_hsotg *hsotg,
			u32 epctl_reg)
{
	u32 ctrl;

	ctrl = dwc2_readl(hsotg->regs + epctl_reg);
	if (ctrl & DXEPCTL_EOFRNUM)
		ctrl |= DXEPCTL_SETEVENFR;
	else
		ctrl |= DXEPCTL_SETODDFR;
	dwc2_writel(ctrl, hsotg->regs + epctl_reg);
}

/*
 * dwc2_gadget_get_xfersize_ddma - get transferred bytes amount from desc
 * @hs_ep - The endpoint on which transfer went
 *
 * Iterate over endpoints descriptor chain and get info on bytes remained
 * in DMA descriptors after transfer has completed. Used for non isoc EPs.
 */
static unsigned int dwc2_gadget_get_xfersize_ddma(struct dwc2_hsotg_ep *hs_ep)
{
	struct dwc2_hsotg *hsotg = hs_ep->parent;
	unsigned int bytes_rem = 0;
	struct dwc2_dma_desc *desc = hs_ep->desc_list;
	int i;
	u32 status;

	if (!desc)
		return -EINVAL;

	for (i = 0; i < hs_ep->desc_count; ++i) {
		status = desc->status;
		bytes_rem += status & DEV_DMA_NBYTES_MASK;

		if (status & DEV_DMA_STS_MASK)
			dev_err(hsotg->dev, "descriptor %d closed with %x\n",
				i, status & DEV_DMA_STS_MASK);
	}

	return bytes_rem;
}

/**
 * dwc2_hsotg_handle_outdone - handle receiving OutDone/SetupDone from RXFIFO
 * @hsotg: The device instance
 * @epnum: The endpoint received from
 *
 * The RXFIFO has delivered an OutDone event, which means that the data
 * transfer for an OUT endpoint has been completed, either by a short
 * packet or by the finish of a transfer.
 */
static void dwc2_hsotg_handle_outdone(struct dwc2_hsotg *hsotg, int epnum)
{
	u32 epsize = dwc2_readl(hsotg->regs + DOEPTSIZ(epnum));
	struct dwc2_hsotg_ep *hs_ep = hsotg->eps_out[epnum];
	struct dwc2_hsotg_req *hs_req = hs_ep->req;
	struct usb_request *req = &hs_req->req;
	unsigned size_left = DXEPTSIZ_XFERSIZE_GET(epsize);
	int result = 0;

	if (!hs_req) {
		dev_dbg(hsotg->dev, "%s: no request active\n", __func__);
		return;
	}

	if (epnum == 0 && hsotg->ep0_state == DWC2_EP0_STATUS_OUT) {
		dev_dbg(hsotg->dev, "zlp packet received\n");
		dwc2_hsotg_complete_request(hsotg, hs_ep, hs_req, 0);
		dwc2_hsotg_enqueue_setup(hsotg);
		return;
	}

	if (using_desc_dma(hsotg))
		size_left = dwc2_gadget_get_xfersize_ddma(hs_ep);

	if (using_dma(hsotg)) {
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
		dwc2_hsotg_start_req(hsotg, hs_ep, hs_req, true);
		return;
	}

	if (req->actual < req->length && req->short_not_ok) {
		dev_dbg(hsotg->dev, "%s: got %d/%d (short not ok) => error\n",
			__func__, req->actual, req->length);

		/*
		 * todo - what should we return here? there's no one else
		 * even bothering to check the status.
		 */
	}

	/* DDMA IN status phase will start from StsPhseRcvd interrupt */
	if (!using_desc_dma(hsotg) && epnum == 0 &&
	    hsotg->ep0_state == DWC2_EP0_DATA_OUT) {
		/* Move to STATUS IN */
		dwc2_hsotg_ep0_zlp(hsotg, true);
		return;
	}

	/*
	 * Slave mode OUT transfers do not go through XferComplete so
	 * adjust the ISOC parity here.
	 */
	if (!using_dma(hsotg)) {
		if (hs_ep->isochronous && hs_ep->interval == 1)
			dwc2_hsotg_change_ep_iso_parity(hsotg, DOEPCTL(epnum));
		else if (hs_ep->isochronous && hs_ep->interval > 1)
			dwc2_gadget_incr_frame_num(hs_ep);
	}

	dwc2_hsotg_complete_request(hsotg, hs_ep, hs_req, result);
}

/**
 * dwc2_hsotg_handle_rx - RX FIFO has data
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
static void dwc2_hsotg_handle_rx(struct dwc2_hsotg *hsotg)
{
	u32 grxstsr = dwc2_readl(hsotg->regs + GRXSTSP);
	u32 epnum, status, size;

	WARN_ON(using_dma(hsotg));

	epnum = grxstsr & GRXSTS_EPNUM_MASK;
	status = grxstsr & GRXSTS_PKTSTS_MASK;

	size = grxstsr & GRXSTS_BYTECNT_MASK;
	size >>= GRXSTS_BYTECNT_SHIFT;

	dev_dbg(hsotg->dev, "%s: GRXSTSP=0x%08x (%d@%d)\n",
			__func__, grxstsr, size, epnum);

	switch ((status & GRXSTS_PKTSTS_MASK) >> GRXSTS_PKTSTS_SHIFT) {
	case GRXSTS_PKTSTS_GLOBALOUTNAK:
		dev_dbg(hsotg->dev, "GLOBALOUTNAK\n");
		break;

	case GRXSTS_PKTSTS_OUTDONE:
		dev_dbg(hsotg->dev, "OutDone (Frame=0x%08x)\n",
			dwc2_hsotg_read_frameno(hsotg));

		if (!using_dma(hsotg))
			dwc2_hsotg_handle_outdone(hsotg, epnum);
		break;

	case GRXSTS_PKTSTS_SETUPDONE:
		dev_dbg(hsotg->dev,
			"SetupDone (Frame=0x%08x, DOPEPCTL=0x%08x)\n",
			dwc2_hsotg_read_frameno(hsotg),
			dwc2_readl(hsotg->regs + DOEPCTL(0)));
		/*
		 * Call dwc2_hsotg_handle_outdone here if it was not called from
		 * GRXSTS_PKTSTS_OUTDONE. That is, if the core didn't
		 * generate GRXSTS_PKTSTS_OUTDONE for setup packet.
		 */
		if (hsotg->ep0_state == DWC2_EP0_SETUP)
			dwc2_hsotg_handle_outdone(hsotg, epnum);
		break;

	case GRXSTS_PKTSTS_OUTRX:
		dwc2_hsotg_rx_data(hsotg, epnum, size);
		break;

	case GRXSTS_PKTSTS_SETUPRX:
		dev_dbg(hsotg->dev,
			"SetupRX (Frame=0x%08x, DOPEPCTL=0x%08x)\n",
			dwc2_hsotg_read_frameno(hsotg),
			dwc2_readl(hsotg->regs + DOEPCTL(0)));

		WARN_ON(hsotg->ep0_state != DWC2_EP0_SETUP);

		dwc2_hsotg_rx_data(hsotg, epnum, size);
		break;

	default:
		dev_warn(hsotg->dev, "%s: unknown status %08x\n",
			 __func__, grxstsr);

		dwc2_hsotg_dump(hsotg);
		break;
	}
}

/**
 * dwc2_hsotg_ep0_mps - turn max packet size into register setting
 * @mps: The maximum packet size in bytes.
 */
static u32 dwc2_hsotg_ep0_mps(unsigned int mps)
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
 * dwc2_hsotg_set_ep_maxpacket - set endpoint's max-packet field
 * @hsotg: The driver state.
 * @ep: The index number of the endpoint
 * @mps: The maximum packet size in bytes
 * @mc: The multicount value
 *
 * Configure the maximum packet size for the given endpoint, updating
 * the hardware control registers to reflect this.
 */
static void dwc2_hsotg_set_ep_maxpacket(struct dwc2_hsotg *hsotg,
					unsigned int ep, unsigned int mps,
					unsigned int mc, unsigned int dir_in)
{
	struct dwc2_hsotg_ep *hs_ep;
	void __iomem *regs = hsotg->regs;
	u32 reg;

	hs_ep = index_to_ep(hsotg, ep, dir_in);
	if (!hs_ep)
		return;

	if (ep == 0) {
		u32 mps_bytes = mps;

		/* EP0 is a special case */
		mps = dwc2_hsotg_ep0_mps(mps_bytes);
		if (mps > 3)
			goto bad_mps;
		hs_ep->ep.maxpacket = mps_bytes;
		hs_ep->mc = 1;
	} else {
		if (mps > 1024)
			goto bad_mps;
		hs_ep->mc = mc;
		if (mc > 3)
			goto bad_mps;
		hs_ep->ep.maxpacket = mps;
	}

	if (dir_in) {
		reg = dwc2_readl(regs + DIEPCTL(ep));
		reg &= ~DXEPCTL_MPS_MASK;
		reg |= mps;
		dwc2_writel(reg, regs + DIEPCTL(ep));
	} else {
		reg = dwc2_readl(regs + DOEPCTL(ep));
		reg &= ~DXEPCTL_MPS_MASK;
		reg |= mps;
		dwc2_writel(reg, regs + DOEPCTL(ep));
	}

	return;

bad_mps:
	dev_err(hsotg->dev, "ep%d: bad mps of %d\n", ep, mps);
}

/**
 * dwc2_hsotg_txfifo_flush - flush Tx FIFO
 * @hsotg: The driver state
 * @idx: The index for the endpoint (0..15)
 */
static void dwc2_hsotg_txfifo_flush(struct dwc2_hsotg *hsotg, unsigned int idx)
{
	int timeout;
	int val;

	dwc2_writel(GRSTCTL_TXFNUM(idx) | GRSTCTL_TXFFLSH,
		    hsotg->regs + GRSTCTL);

	/* wait until the fifo is flushed */
	timeout = 100;

	while (1) {
		val = dwc2_readl(hsotg->regs + GRSTCTL);

		if ((val & (GRSTCTL_TXFFLSH)) == 0)
			break;

		if (--timeout == 0) {
			dev_err(hsotg->dev,
				"%s: timeout flushing fifo (GRSTCTL=%08x)\n",
				__func__, val);
			break;
		}

		udelay(1);
	}
}

/**
 * dwc2_hsotg_trytx - check to see if anything needs transmitting
 * @hsotg: The driver state
 * @hs_ep: The driver endpoint to check.
 *
 * Check to see if there is a request that has data to send, and if so
 * make an attempt to write data into the FIFO.
 */
static int dwc2_hsotg_trytx(struct dwc2_hsotg *hsotg,
			   struct dwc2_hsotg_ep *hs_ep)
{
	struct dwc2_hsotg_req *hs_req = hs_ep->req;

	if (!hs_ep->dir_in || !hs_req) {
		/**
		 * if request is not enqueued, we disable interrupts
		 * for endpoints, excepting ep0
		 */
		if (hs_ep->index != 0)
			dwc2_hsotg_ctrl_epint(hsotg, hs_ep->index,
					     hs_ep->dir_in, 0);
		return 0;
	}

	if (hs_req->req.actual < hs_req->req.length) {
		dev_dbg(hsotg->dev, "trying to write more for ep%d\n",
			hs_ep->index);
		return dwc2_hsotg_write_fifo(hsotg, hs_ep, hs_req);
	}

	return 0;
}

/**
 * dwc2_hsotg_complete_in - complete IN transfer
 * @hsotg: The device state.
 * @hs_ep: The endpoint that has just completed.
 *
 * An IN transfer has been completed, update the transfer's state and then
 * call the relevant completion routines.
 */
static void dwc2_hsotg_complete_in(struct dwc2_hsotg *hsotg,
				  struct dwc2_hsotg_ep *hs_ep)
{
	struct dwc2_hsotg_req *hs_req = hs_ep->req;
	u32 epsize = dwc2_readl(hsotg->regs + DIEPTSIZ(hs_ep->index));
	int size_left, size_done;

	if (!hs_req) {
		dev_dbg(hsotg->dev, "XferCompl but no req\n");
		return;
	}

	/* Finish ZLP handling for IN EP0 transactions */
	if (hs_ep->index == 0 && hsotg->ep0_state == DWC2_EP0_STATUS_IN) {
		dev_dbg(hsotg->dev, "zlp packet sent\n");

		/*
		 * While send zlp for DWC2_EP0_STATUS_IN EP direction was
		 * changed to IN. Change back to complete OUT transfer request
		 */
		hs_ep->dir_in = 0;

		dwc2_hsotg_complete_request(hsotg, hs_ep, hs_req, 0);
		if (hsotg->test_mode) {
			int ret;

			ret = dwc2_hsotg_set_test_mode(hsotg, hsotg->test_mode);
			if (ret < 0) {
				dev_dbg(hsotg->dev, "Invalid Test #%d\n",
						hsotg->test_mode);
				dwc2_hsotg_stall_ep0(hsotg);
				return;
			}
		}
		dwc2_hsotg_enqueue_setup(hsotg);
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
	if (using_desc_dma(hsotg)) {
		size_left = dwc2_gadget_get_xfersize_ddma(hs_ep);
		if (size_left < 0)
			dev_err(hsotg->dev, "error parsing DDMA results %d\n",
				size_left);
	} else {
		size_left = DXEPTSIZ_XFERSIZE_GET(epsize);
	}

	size_done = hs_ep->size_loaded - size_left;
	size_done += hs_ep->last_load;

	if (hs_req->req.actual != size_done)
		dev_dbg(hsotg->dev, "%s: adjusting size done %d => %d\n",
			__func__, hs_req->req.actual, size_done);

	hs_req->req.actual = size_done;
	dev_dbg(hsotg->dev, "req->length:%d req->actual:%d req->zero:%d\n",
		hs_req->req.length, hs_req->req.actual, hs_req->req.zero);

	if (!size_left && hs_req->req.actual < hs_req->req.length) {
		dev_dbg(hsotg->dev, "%s trying more for req...\n", __func__);
		dwc2_hsotg_start_req(hsotg, hs_ep, hs_req, true);
		return;
	}

	/* Zlp for all endpoints, for ep0 only in DATA IN stage */
	if (hs_ep->send_zlp) {
		dwc2_hsotg_program_zlp(hsotg, hs_ep);
		hs_ep->send_zlp = 0;
		/* transfer will be completed on next complete interrupt */
		return;
	}

	if (hs_ep->index == 0 && hsotg->ep0_state == DWC2_EP0_DATA_IN) {
		/* Move to STATUS OUT */
		dwc2_hsotg_ep0_zlp(hsotg, false);
		return;
	}

	dwc2_hsotg_complete_request(hsotg, hs_ep, hs_req, 0);
}

/**
 * dwc2_gadget_read_ep_interrupts - reads interrupts for given ep
 * @hsotg: The device state.
 * @idx: Index of ep.
 * @dir_in: Endpoint direction 1-in 0-out.
 *
 * Reads for endpoint with given index and direction, by masking
 * epint_reg with coresponding mask.
 */
static u32 dwc2_gadget_read_ep_interrupts(struct dwc2_hsotg *hsotg,
					  unsigned int idx, int dir_in)
{
	u32 epmsk_reg = dir_in ? DIEPMSK : DOEPMSK;
	u32 epint_reg = dir_in ? DIEPINT(idx) : DOEPINT(idx);
	u32 ints;
	u32 mask;
	u32 diepempmsk;

	mask = dwc2_readl(hsotg->regs + epmsk_reg);
	diepempmsk = dwc2_readl(hsotg->regs + DIEPEMPMSK);
	mask |= ((diepempmsk >> idx) & 0x1) ? DIEPMSK_TXFIFOEMPTY : 0;
	mask |= DXEPINT_SETUP_RCVD;

	ints = dwc2_readl(hsotg->regs + epint_reg);
	ints &= mask;
	return ints;
}

/**
 * dwc2_gadget_handle_ep_disabled - handle DXEPINT_EPDISBLD
 * @hs_ep: The endpoint on which interrupt is asserted.
 *
 * This interrupt indicates that the endpoint has been disabled per the
 * application's request.
 *
 * For IN endpoints flushes txfifo, in case of BULK clears DCTL_CGNPINNAK,
 * in case of ISOC completes current request.
 *
 * For ISOC-OUT endpoints completes expired requests. If there is remaining
 * request starts it.
 */
static void dwc2_gadget_handle_ep_disabled(struct dwc2_hsotg_ep *hs_ep)
{
	struct dwc2_hsotg *hsotg = hs_ep->parent;
	struct dwc2_hsotg_req *hs_req;
	unsigned char idx = hs_ep->index;
	int dir_in = hs_ep->dir_in;
	u32 epctl_reg = dir_in ? DIEPCTL(idx) : DOEPCTL(idx);
	int dctl = dwc2_readl(hsotg->regs + DCTL);

	dev_dbg(hsotg->dev, "%s: EPDisbld\n", __func__);

	if (dir_in) {
		int epctl = dwc2_readl(hsotg->regs + epctl_reg);

		dwc2_hsotg_txfifo_flush(hsotg, hs_ep->fifo_index);

		if (hs_ep->isochronous) {
			dwc2_hsotg_complete_in(hsotg, hs_ep);
			return;
		}

		if ((epctl & DXEPCTL_STALL) && (epctl & DXEPCTL_EPTYPE_BULK)) {
			int dctl = dwc2_readl(hsotg->regs + DCTL);

			dctl |= DCTL_CGNPINNAK;
			dwc2_writel(dctl, hsotg->regs + DCTL);
		}
		return;
	}

	if (dctl & DCTL_GOUTNAKSTS) {
		dctl |= DCTL_CGOUTNAK;
		dwc2_writel(dctl, hsotg->regs + DCTL);
	}

	if (!hs_ep->isochronous)
		return;

	if (list_empty(&hs_ep->queue)) {
		dev_dbg(hsotg->dev, "%s: complete_ep 0x%p, ep->queue empty!\n",
			__func__, hs_ep);
		return;
	}

	do {
		hs_req = get_ep_head(hs_ep);
		if (hs_req)
			dwc2_hsotg_complete_request(hsotg, hs_ep, hs_req,
						    -ENODATA);
		dwc2_gadget_incr_frame_num(hs_ep);
	} while (dwc2_gadget_target_frame_elapsed(hs_ep));

	dwc2_gadget_start_next_request(hs_ep);
}

/**
 * dwc2_gadget_handle_out_token_ep_disabled - handle DXEPINT_OUTTKNEPDIS
 * @hs_ep: The endpoint on which interrupt is asserted.
 *
 * This is starting point for ISOC-OUT transfer, synchronization done with
 * first out token received from host while corresponding EP is disabled.
 *
 * Device does not know initial frame in which out token will come. For this
 * HW generates OUTTKNEPDIS - out token is received while EP is disabled. Upon
 * getting this interrupt SW starts calculation for next transfer frame.
 */
static void dwc2_gadget_handle_out_token_ep_disabled(struct dwc2_hsotg_ep *ep)
{
	struct dwc2_hsotg *hsotg = ep->parent;
	int dir_in = ep->dir_in;
	u32 doepmsk;
	u32 tmp;

	if (dir_in || !ep->isochronous)
		return;

	/*
	 * Store frame in which irq was asserted here, as
	 * it can change while completing request below.
	 */
	tmp = dwc2_hsotg_read_frameno(hsotg);

	dwc2_hsotg_complete_request(hsotg, ep, get_ep_head(ep), -ENODATA);

	if (using_desc_dma(hsotg)) {
		if (ep->target_frame == TARGET_FRAME_INITIAL) {
			/* Start first ISO Out */
			ep->target_frame = tmp;
			dwc2_gadget_start_isoc_ddma(ep);
		}
		return;
	}

	if (ep->interval > 1 &&
	    ep->target_frame == TARGET_FRAME_INITIAL) {
		u32 dsts;
		u32 ctrl;

		dsts = dwc2_readl(hsotg->regs + DSTS);
		ep->target_frame = dwc2_hsotg_read_frameno(hsotg);
		dwc2_gadget_incr_frame_num(ep);

		ctrl = dwc2_readl(hsotg->regs + DOEPCTL(ep->index));
		if (ep->target_frame & 0x1)
			ctrl |= DXEPCTL_SETODDFR;
		else
			ctrl |= DXEPCTL_SETEVENFR;

		dwc2_writel(ctrl, hsotg->regs + DOEPCTL(ep->index));
	}

	dwc2_gadget_start_next_request(ep);
	doepmsk = dwc2_readl(hsotg->regs + DOEPMSK);
	doepmsk &= ~DOEPMSK_OUTTKNEPDISMSK;
	dwc2_writel(doepmsk, hsotg->regs + DOEPMSK);
}

/**
* dwc2_gadget_handle_nak - handle NAK interrupt
* @hs_ep: The endpoint on which interrupt is asserted.
*
* This is starting point for ISOC-IN transfer, synchronization done with
* first IN token received from host while corresponding EP is disabled.
*
* Device does not know when first one token will arrive from host. On first
* token arrival HW generates 2 interrupts: 'in token received while FIFO empty'
* and 'NAK'. NAK interrupt for ISOC-IN means that token has arrived and ZLP was
* sent in response to that as there was no data in FIFO. SW is basing on this
* interrupt to obtain frame in which token has come and then based on the
* interval calculates next frame for transfer.
*/
static void dwc2_gadget_handle_nak(struct dwc2_hsotg_ep *hs_ep)
{
	struct dwc2_hsotg *hsotg = hs_ep->parent;
	int dir_in = hs_ep->dir_in;

	if (!dir_in || !hs_ep->isochronous)
		return;

	if (hs_ep->target_frame == TARGET_FRAME_INITIAL) {
		hs_ep->target_frame = dwc2_hsotg_read_frameno(hsotg);

		if (using_desc_dma(hsotg)) {
			dwc2_gadget_start_isoc_ddma(hs_ep);
			return;
		}

		if (hs_ep->interval > 1) {
			u32 ctrl = dwc2_readl(hsotg->regs +
					      DIEPCTL(hs_ep->index));
			if (hs_ep->target_frame & 0x1)
				ctrl |= DXEPCTL_SETODDFR;
			else
				ctrl |= DXEPCTL_SETEVENFR;

			dwc2_writel(ctrl, hsotg->regs + DIEPCTL(hs_ep->index));
		}

		dwc2_hsotg_complete_request(hsotg, hs_ep,
					    get_ep_head(hs_ep), 0);
	}

	dwc2_gadget_incr_frame_num(hs_ep);
}

/**
 * dwc2_hsotg_epint - handle an in/out endpoint interrupt
 * @hsotg: The driver state
 * @idx: The index for the endpoint (0..15)
 * @dir_in: Set if this is an IN endpoint
 *
 * Process and clear any interrupt pending for an individual endpoint
 */
static void dwc2_hsotg_epint(struct dwc2_hsotg *hsotg, unsigned int idx,
			    int dir_in)
{
	struct dwc2_hsotg_ep *hs_ep = index_to_ep(hsotg, idx, dir_in);
	u32 epint_reg = dir_in ? DIEPINT(idx) : DOEPINT(idx);
	u32 epctl_reg = dir_in ? DIEPCTL(idx) : DOEPCTL(idx);
	u32 epsiz_reg = dir_in ? DIEPTSIZ(idx) : DOEPTSIZ(idx);
	u32 ints;
	u32 ctrl;

	ints = dwc2_gadget_read_ep_interrupts(hsotg, idx, dir_in);
	ctrl = dwc2_readl(hsotg->regs + epctl_reg);

	/* Clear endpoint interrupts */
	dwc2_writel(ints, hsotg->regs + epint_reg);

	if (!hs_ep) {
		dev_err(hsotg->dev, "%s:Interrupt for unconfigured ep%d(%s)\n",
					__func__, idx, dir_in ? "in" : "out");
		return;
	}

	dev_dbg(hsotg->dev, "%s: ep%d(%s) DxEPINT=0x%08x\n",
		__func__, idx, dir_in ? "in" : "out", ints);

	/* Don't process XferCompl interrupt if it is a setup packet */
	if (idx == 0 && (ints & (DXEPINT_SETUP | DXEPINT_SETUP_RCVD)))
		ints &= ~DXEPINT_XFERCOMPL;

	/*
	 * Don't process XferCompl interrupt in DDMA if EP0 is still in SETUP
	 * stage and xfercomplete was generated without SETUP phase done
	 * interrupt. SW should parse received setup packet only after host's
	 * exit from setup phase of control transfer.
	 */
	if (using_desc_dma(hsotg) && idx == 0 && !hs_ep->dir_in &&
	    hsotg->ep0_state == DWC2_EP0_SETUP && !(ints & DXEPINT_SETUP))
		ints &= ~DXEPINT_XFERCOMPL;

	if (ints & DXEPINT_XFERCOMPL) {
		dev_dbg(hsotg->dev,
			"%s: XferCompl: DxEPCTL=0x%08x, DXEPTSIZ=%08x\n",
			__func__, dwc2_readl(hsotg->regs + epctl_reg),
			dwc2_readl(hsotg->regs + epsiz_reg));

		/* In DDMA handle isochronous requests separately */
		if (using_desc_dma(hsotg) && hs_ep->isochronous) {
			dwc2_gadget_complete_isoc_request_ddma(hs_ep);
			/* Try to start next isoc request */
			dwc2_gadget_start_next_isoc_ddma(hs_ep);
		} else if (dir_in) {
			/*
			 * We get OutDone from the FIFO, so we only
			 * need to look at completing IN requests here
			 * if operating slave mode
			 */
			if (hs_ep->isochronous && hs_ep->interval > 1)
				dwc2_gadget_incr_frame_num(hs_ep);

			dwc2_hsotg_complete_in(hsotg, hs_ep);
			if (ints & DXEPINT_NAKINTRPT)
				ints &= ~DXEPINT_NAKINTRPT;

			if (idx == 0 && !hs_ep->req)
				dwc2_hsotg_enqueue_setup(hsotg);
		} else if (using_dma(hsotg)) {
			/*
			 * We're using DMA, we need to fire an OutDone here
			 * as we ignore the RXFIFO.
			 */
			if (hs_ep->isochronous && hs_ep->interval > 1)
				dwc2_gadget_incr_frame_num(hs_ep);

			dwc2_hsotg_handle_outdone(hsotg, idx);
		}
	}

	if (ints & DXEPINT_EPDISBLD)
		dwc2_gadget_handle_ep_disabled(hs_ep);

	if (ints & DXEPINT_OUTTKNEPDIS)
		dwc2_gadget_handle_out_token_ep_disabled(hs_ep);

	if (ints & DXEPINT_NAKINTRPT)
		dwc2_gadget_handle_nak(hs_ep);

	if (ints & DXEPINT_AHBERR)
		dev_dbg(hsotg->dev, "%s: AHBErr\n", __func__);

	if (ints & DXEPINT_SETUP) {  /* Setup or Timeout */
		dev_dbg(hsotg->dev, "%s: Setup/Timeout\n",  __func__);

		if (using_dma(hsotg) && idx == 0) {
			/*
			 * this is the notification we've received a
			 * setup packet. In non-DMA mode we'd get this
			 * from the RXFIFO, instead we need to process
			 * the setup here.
			 */

			if (dir_in)
				WARN_ON_ONCE(1);
			else
				dwc2_hsotg_handle_outdone(hsotg, 0);
		}
	}

	if (ints & DXEPINT_STSPHSERCVD) {
		dev_dbg(hsotg->dev, "%s: StsPhseRcvd\n", __func__);

		/* Move to STATUS IN for DDMA */
		if (using_desc_dma(hsotg))
			dwc2_hsotg_ep0_zlp(hsotg, true);
	}

	if (ints & DXEPINT_BACK2BACKSETUP)
		dev_dbg(hsotg->dev, "%s: B2BSetup/INEPNakEff\n", __func__);

	if (ints & DXEPINT_BNAINTR) {
		dev_dbg(hsotg->dev, "%s: BNA interrupt\n", __func__);

		/*
		 * Try to start next isoc request, if any.
		 * Sometimes the endpoint remains enabled after BNA interrupt
		 * assertion, which is not expected, hence we can enter here
		 * couple of times.
		 */
		if (hs_ep->isochronous)
			dwc2_gadget_start_next_isoc_ddma(hs_ep);
	}

	if (dir_in && !hs_ep->isochronous) {
		/* not sure if this is important, but we'll clear it anyway */
		if (ints & DXEPINT_INTKNTXFEMP) {
			dev_dbg(hsotg->dev, "%s: ep%d: INTknTXFEmpMsk\n",
				__func__, idx);
		}

		/* this probably means something bad is happening */
		if (ints & DXEPINT_INTKNEPMIS) {
			dev_warn(hsotg->dev, "%s: ep%d: INTknEP\n",
				 __func__, idx);
		}

		/* FIFO has space or is empty (see GAHBCFG) */
		if (hsotg->dedicated_fifos &&
		    ints & DXEPINT_TXFEMP) {
			dev_dbg(hsotg->dev, "%s: ep%d: TxFIFOEmpty\n",
				__func__, idx);
			if (!using_dma(hsotg))
				dwc2_hsotg_trytx(hsotg, hs_ep);
		}
	}
}

/**
 * dwc2_hsotg_irq_enumdone - Handle EnumDone interrupt (enumeration done)
 * @hsotg: The device state.
 *
 * Handle updating the device settings after the enumeration phase has
 * been completed.
 */
static void dwc2_hsotg_irq_enumdone(struct dwc2_hsotg *hsotg)
{
	u32 dsts = dwc2_readl(hsotg->regs + DSTS);
	int ep0_mps = 0, ep_mps = 8;

	/*
	 * This should signal the finish of the enumeration phase
	 * of the USB handshaking, so we should now know what rate
	 * we connected at.
	 */

	dev_dbg(hsotg->dev, "EnumDone (DSTS=0x%08x)\n", dsts);

	/*
	 * note, since we're limited by the size of transfer on EP0, and
	 * it seems IN transfers must be a even number of packets we do
	 * not advertise a 64byte MPS on EP0.
	 */

	/* catch both EnumSpd_FS and EnumSpd_FS48 */
	switch ((dsts & DSTS_ENUMSPD_MASK) >> DSTS_ENUMSPD_SHIFT) {
	case DSTS_ENUMSPD_FS:
	case DSTS_ENUMSPD_FS48:
		hsotg->gadget.speed = USB_SPEED_FULL;
		ep0_mps = EP0_MPS_LIMIT;
		ep_mps = 1023;
		break;

	case DSTS_ENUMSPD_HS:
		hsotg->gadget.speed = USB_SPEED_HIGH;
		ep0_mps = EP0_MPS_LIMIT;
		ep_mps = 1024;
		break;

	case DSTS_ENUMSPD_LS:
		hsotg->gadget.speed = USB_SPEED_LOW;
		ep0_mps = 8;
		ep_mps = 8;
		/*
		 * note, we don't actually support LS in this driver at the
		 * moment, and the documentation seems to imply that it isn't
		 * supported by the PHYs on some of the devices.
		 */
		break;
	}
	dev_info(hsotg->dev, "new device is %s\n",
		 usb_speed_string(hsotg->gadget.speed));

	/*
	 * we should now know the maximum packet size for an
	 * endpoint, so set the endpoints to a default value.
	 */

	if (ep0_mps) {
		int i;
		/* Initialize ep0 for both in and out directions */
		dwc2_hsotg_set_ep_maxpacket(hsotg, 0, ep0_mps, 0, 1);
		dwc2_hsotg_set_ep_maxpacket(hsotg, 0, ep0_mps, 0, 0);
		for (i = 1; i < hsotg->num_of_eps; i++) {
			if (hsotg->eps_in[i])
				dwc2_hsotg_set_ep_maxpacket(hsotg, i, ep_mps,
							    0, 1);
			if (hsotg->eps_out[i])
				dwc2_hsotg_set_ep_maxpacket(hsotg, i, ep_mps,
							    0, 0);
		}
	}

	/* ensure after enumeration our EP0 is active */

	dwc2_hsotg_enqueue_setup(hsotg);

	dev_dbg(hsotg->dev, "EP0: DIEPCTL0=0x%08x, DOEPCTL0=0x%08x\n",
		dwc2_readl(hsotg->regs + DIEPCTL0),
		dwc2_readl(hsotg->regs + DOEPCTL0));
}

/**
 * kill_all_requests - remove all requests from the endpoint's queue
 * @hsotg: The device state.
 * @ep: The endpoint the requests may be on.
 * @result: The result code to use.
 *
 * Go through the requests on the given endpoint and mark them
 * completed with the given result code.
 */
static void kill_all_requests(struct dwc2_hsotg *hsotg,
			      struct dwc2_hsotg_ep *ep,
			      int result)
{
	struct dwc2_hsotg_req *req, *treq;
	unsigned size;

	ep->req = NULL;

	list_for_each_entry_safe(req, treq, &ep->queue, queue)
		dwc2_hsotg_complete_request(hsotg, ep, req,
					   result);

	if (!hsotg->dedicated_fifos)
		return;
	size = (dwc2_readl(hsotg->regs + DTXFSTS(ep->fifo_index)) & 0xffff) * 4;
	if (size < ep->fifo_size)
		dwc2_hsotg_txfifo_flush(hsotg, ep->fifo_index);
}

/**
 * dwc2_hsotg_disconnect - disconnect service
 * @hsotg: The device state.
 *
 * The device has been disconnected. Remove all current
 * transactions and signal the gadget driver that this
 * has happened.
 */
void dwc2_hsotg_disconnect(struct dwc2_hsotg *hsotg)
{
	unsigned ep;

	if (!hsotg->connected)
		return;

	hsotg->connected = 0;
	hsotg->test_mode = 0;

	for (ep = 0; ep < hsotg->num_of_eps; ep++) {
		if (hsotg->eps_in[ep])
			kill_all_requests(hsotg, hsotg->eps_in[ep],
								-ESHUTDOWN);
		if (hsotg->eps_out[ep])
			kill_all_requests(hsotg, hsotg->eps_out[ep],
								-ESHUTDOWN);
	}

	call_gadget(hsotg, disconnect);
	hsotg->lx_state = DWC2_L3;
}

/**
 * dwc2_hsotg_irq_fifoempty - TX FIFO empty interrupt handler
 * @hsotg: The device state:
 * @periodic: True if this is a periodic FIFO interrupt
 */
static void dwc2_hsotg_irq_fifoempty(struct dwc2_hsotg *hsotg, bool periodic)
{
	struct dwc2_hsotg_ep *ep;
	int epno, ret;

	/* look through for any more data to transmit */
	for (epno = 0; epno < hsotg->num_of_eps; epno++) {
		ep = index_to_ep(hsotg, epno, 1);

		if (!ep)
			continue;

		if (!ep->dir_in)
			continue;

		if ((periodic && !ep->periodic) ||
		    (!periodic && ep->periodic))
			continue;

		ret = dwc2_hsotg_trytx(hsotg, ep);
		if (ret < 0)
			break;
	}
}

/* IRQ flags which will trigger a retry around the IRQ loop */
#define IRQ_RETRY_MASK (GINTSTS_NPTXFEMP | \
			GINTSTS_PTXFEMP |  \
			GINTSTS_RXFLVL)

/**
 * dwc2_hsotg_core_init - issue softreset to the core
 * @hsotg: The device state
 *
 * Issue a soft reset to the core, and await the core finishing it.
 */
void dwc2_hsotg_core_init_disconnected(struct dwc2_hsotg *hsotg,
						bool is_usb_reset)
{
	u32 intmsk;
	u32 val;
	u32 usbcfg;
	u32 dcfg = 0;

	/* Kill any ep0 requests as controller will be reinitialized */
	kill_all_requests(hsotg, hsotg->eps_out[0], -ECONNRESET);

	if (!is_usb_reset)
		if (dwc2_core_reset(hsotg))
			return;

	/*
	 * we must now enable ep0 ready for host detection and then
	 * set configuration.
	 */

	/* keep other bits untouched (so e.g. forced modes are not lost) */
	usbcfg = dwc2_readl(hsotg->regs + GUSBCFG);
	usbcfg &= ~(GUSBCFG_TOUTCAL_MASK | GUSBCFG_PHYIF16 | GUSBCFG_SRPCAP |
		GUSBCFG_HNPCAP | GUSBCFG_USBTRDTIM_MASK);

	if (hsotg->params.phy_type == DWC2_PHY_TYPE_PARAM_FS &&
	    (hsotg->params.speed == DWC2_SPEED_PARAM_FULL ||
	     hsotg->params.speed == DWC2_SPEED_PARAM_LOW)) {
		/* FS/LS Dedicated Transceiver Interface */
		usbcfg |= GUSBCFG_PHYSEL;
	} else {
		/* set the PLL on, remove the HNP/SRP and set the PHY */
		val = (hsotg->phyif == GUSBCFG_PHYIF8) ? 9 : 5;
		usbcfg |= hsotg->phyif | GUSBCFG_TOUTCAL(7) |
			(val << GUSBCFG_USBTRDTIM_SHIFT);
	}
	dwc2_writel(usbcfg, hsotg->regs + GUSBCFG);

	dwc2_hsotg_init_fifo(hsotg);

	if (!is_usb_reset)
		__orr32(hsotg->regs + DCTL, DCTL_SFTDISCON);

	dcfg |= DCFG_EPMISCNT(1);

	switch (hsotg->params.speed) {
	case DWC2_SPEED_PARAM_LOW:
		dcfg |= DCFG_DEVSPD_LS;
		break;
	case DWC2_SPEED_PARAM_FULL:
		if (hsotg->params.phy_type == DWC2_PHY_TYPE_PARAM_FS)
			dcfg |= DCFG_DEVSPD_FS48;
		else
			dcfg |= DCFG_DEVSPD_FS;
		break;
	default:
		dcfg |= DCFG_DEVSPD_HS;
	}

	dwc2_writel(dcfg,  hsotg->regs + DCFG);

	/* Clear any pending OTG interrupts */
	dwc2_writel(0xffffffff, hsotg->regs + GOTGINT);

	/* Clear any pending interrupts */
	dwc2_writel(0xffffffff, hsotg->regs + GINTSTS);
	intmsk = GINTSTS_ERLYSUSP | GINTSTS_SESSREQINT |
		GINTSTS_GOUTNAKEFF | GINTSTS_GINNAKEFF |
		GINTSTS_USBRST | GINTSTS_RESETDET |
		GINTSTS_ENUMDONE | GINTSTS_OTGINT |
		GINTSTS_USBSUSP | GINTSTS_WKUPINT;

	if (!using_desc_dma(hsotg))
		intmsk |= GINTSTS_INCOMPL_SOIN | GINTSTS_INCOMPL_SOOUT;

	if (hsotg->params.external_id_pin_ctl <= 0)
		intmsk |= GINTSTS_CONIDSTSCHNG;

	dwc2_writel(intmsk, hsotg->regs + GINTMSK);

	if (using_dma(hsotg)) {
		dwc2_writel(GAHBCFG_GLBL_INTR_EN | GAHBCFG_DMA_EN |
			    (GAHBCFG_HBSTLEN_INCR4 << GAHBCFG_HBSTLEN_SHIFT),
			    hsotg->regs + GAHBCFG);

		/* Set DDMA mode support in the core if needed */
		if (using_desc_dma(hsotg))
			__orr32(hsotg->regs + DCFG, DCFG_DESCDMA_EN);

	} else {
		dwc2_writel(((hsotg->dedicated_fifos) ?
						(GAHBCFG_NP_TXF_EMP_LVL |
						 GAHBCFG_P_TXF_EMP_LVL) : 0) |
			    GAHBCFG_GLBL_INTR_EN, hsotg->regs + GAHBCFG);
	}

	/*
	 * If INTknTXFEmpMsk is enabled, it's important to disable ep interrupts
	 * when we have no data to transfer. Otherwise we get being flooded by
	 * interrupts.
	 */

	dwc2_writel(((hsotg->dedicated_fifos && !using_dma(hsotg)) ?
		DIEPMSK_TXFIFOEMPTY | DIEPMSK_INTKNTXFEMPMSK : 0) |
		DIEPMSK_EPDISBLDMSK | DIEPMSK_XFERCOMPLMSK |
		DIEPMSK_TIMEOUTMSK | DIEPMSK_AHBERRMSK,
		hsotg->regs + DIEPMSK);

	/*
	 * don't need XferCompl, we get that from RXFIFO in slave mode. In
	 * DMA mode we may need this and StsPhseRcvd.
	 */
	dwc2_writel((using_dma(hsotg) ? (DIEPMSK_XFERCOMPLMSK |
		DOEPMSK_STSPHSERCVDMSK) : 0) |
		DOEPMSK_EPDISBLDMSK | DOEPMSK_AHBERRMSK |
		DOEPMSK_SETUPMSK,
		hsotg->regs + DOEPMSK);

	/* Enable BNA interrupt for DDMA */
	if (using_desc_dma(hsotg))
		__orr32(hsotg->regs + DOEPMSK, DOEPMSK_BNAMSK);

	dwc2_writel(0, hsotg->regs + DAINTMSK);

	dev_dbg(hsotg->dev, "EP0: DIEPCTL0=0x%08x, DOEPCTL0=0x%08x\n",
		dwc2_readl(hsotg->regs + DIEPCTL0),
		dwc2_readl(hsotg->regs + DOEPCTL0));

	/* enable in and out endpoint interrupts */
	dwc2_hsotg_en_gsint(hsotg, GINTSTS_OEPINT | GINTSTS_IEPINT);

	/*
	 * Enable the RXFIFO when in slave mode, as this is how we collect
	 * the data. In DMA mode, we get events from the FIFO but also
	 * things we cannot process, so do not use it.
	 */
	if (!using_dma(hsotg))
		dwc2_hsotg_en_gsint(hsotg, GINTSTS_RXFLVL);

	/* Enable interrupts for EP0 in and out */
	dwc2_hsotg_ctrl_epint(hsotg, 0, 0, 1);
	dwc2_hsotg_ctrl_epint(hsotg, 0, 1, 1);

	if (!is_usb_reset) {
		__orr32(hsotg->regs + DCTL, DCTL_PWRONPRGDONE);
		udelay(10);  /* see openiboot */
		__bic32(hsotg->regs + DCTL, DCTL_PWRONPRGDONE);
	}

	dev_dbg(hsotg->dev, "DCTL=0x%08x\n", dwc2_readl(hsotg->regs + DCTL));

	/*
	 * DxEPCTL_USBActEp says RO in manual, but seems to be set by
	 * writing to the EPCTL register..
	 */

	/* set to read 1 8byte packet */
	dwc2_writel(DXEPTSIZ_MC(1) | DXEPTSIZ_PKTCNT(1) |
	       DXEPTSIZ_XFERSIZE(8), hsotg->regs + DOEPTSIZ0);

	dwc2_writel(dwc2_hsotg_ep0_mps(hsotg->eps_out[0]->ep.maxpacket) |
	       DXEPCTL_CNAK | DXEPCTL_EPENA |
	       DXEPCTL_USBACTEP,
	       hsotg->regs + DOEPCTL0);

	/* enable, but don't activate EP0in */
	dwc2_writel(dwc2_hsotg_ep0_mps(hsotg->eps_out[0]->ep.maxpacket) |
	       DXEPCTL_USBACTEP, hsotg->regs + DIEPCTL0);

	dwc2_hsotg_enqueue_setup(hsotg);

	dev_dbg(hsotg->dev, "EP0: DIEPCTL0=0x%08x, DOEPCTL0=0x%08x\n",
		dwc2_readl(hsotg->regs + DIEPCTL0),
		dwc2_readl(hsotg->regs + DOEPCTL0));

	/* clear global NAKs */
	val = DCTL_CGOUTNAK | DCTL_CGNPINNAK;
	if (!is_usb_reset)
		val |= DCTL_SFTDISCON;
	__orr32(hsotg->regs + DCTL, val);

	/* must be at-least 3ms to allow bus to see disconnect */
	mdelay(3);

	hsotg->lx_state = DWC2_L0;
}

static void dwc2_hsotg_core_disconnect(struct dwc2_hsotg *hsotg)
{
	/* set the soft-disconnect bit */
	__orr32(hsotg->regs + DCTL, DCTL_SFTDISCON);
}

void dwc2_hsotg_core_connect(struct dwc2_hsotg *hsotg)
{
	/* remove the soft-disconnect and let's go */
	__bic32(hsotg->regs + DCTL, DCTL_SFTDISCON);
}

/**
 * dwc2_gadget_handle_incomplete_isoc_in - handle incomplete ISO IN Interrupt.
 * @hsotg: The device state:
 *
 * This interrupt indicates one of the following conditions occurred while
 * transmitting an ISOC transaction.
 * - Corrupted IN Token for ISOC EP.
 * - Packet not complete in FIFO.
 *
 * The following actions will be taken:
 * - Determine the EP
 * - Disable EP; when 'Endpoint Disabled' interrupt is received Flush FIFO
 */
static void dwc2_gadget_handle_incomplete_isoc_in(struct dwc2_hsotg *hsotg)
{
	struct dwc2_hsotg_ep *hs_ep;
	u32 epctrl;
	u32 idx;

	dev_dbg(hsotg->dev, "Incomplete isoc in interrupt received:\n");

	for (idx = 1; idx <= hsotg->num_of_eps; idx++) {
		hs_ep = hsotg->eps_in[idx];
		epctrl = dwc2_readl(hsotg->regs + DIEPCTL(idx));
		if ((epctrl & DXEPCTL_EPENA) && hs_ep->isochronous &&
		    dwc2_gadget_target_frame_elapsed(hs_ep)) {
			epctrl |= DXEPCTL_SNAK;
			epctrl |= DXEPCTL_EPDIS;
			dwc2_writel(epctrl, hsotg->regs + DIEPCTL(idx));
		}
	}

	/* Clear interrupt */
	dwc2_writel(GINTSTS_INCOMPL_SOIN, hsotg->regs + GINTSTS);
}

/**
 * dwc2_gadget_handle_incomplete_isoc_out - handle incomplete ISO OUT Interrupt
 * @hsotg: The device state:
 *
 * This interrupt indicates one of the following conditions occurred while
 * transmitting an ISOC transaction.
 * - Corrupted OUT Token for ISOC EP.
 * - Packet not complete in FIFO.
 *
 * The following actions will be taken:
 * - Determine the EP
 * - Set DCTL_SGOUTNAK and unmask GOUTNAKEFF if target frame elapsed.
 */
static void dwc2_gadget_handle_incomplete_isoc_out(struct dwc2_hsotg *hsotg)
{
	u32 gintsts;
	u32 gintmsk;
	u32 epctrl;
	struct dwc2_hsotg_ep *hs_ep;
	int idx;

	dev_dbg(hsotg->dev, "%s: GINTSTS_INCOMPL_SOOUT\n", __func__);

	for (idx = 1; idx <= hsotg->num_of_eps; idx++) {
		hs_ep = hsotg->eps_out[idx];
		epctrl = dwc2_readl(hsotg->regs + DOEPCTL(idx));
		if ((epctrl & DXEPCTL_EPENA) && hs_ep->isochronous &&
		    dwc2_gadget_target_frame_elapsed(hs_ep)) {
			/* Unmask GOUTNAKEFF interrupt */
			gintmsk = dwc2_readl(hsotg->regs + GINTMSK);
			gintmsk |= GINTSTS_GOUTNAKEFF;
			dwc2_writel(gintmsk, hsotg->regs + GINTMSK);

			gintsts = dwc2_readl(hsotg->regs + GINTSTS);
			if (!(gintsts & GINTSTS_GOUTNAKEFF))
				__orr32(hsotg->regs + DCTL, DCTL_SGOUTNAK);
		}
	}

	/* Clear interrupt */
	dwc2_writel(GINTSTS_INCOMPL_SOOUT, hsotg->regs + GINTSTS);
}

/**
 * dwc2_hsotg_irq - handle device interrupt
 * @irq: The IRQ number triggered
 * @pw: The pw value when registered the handler.
 */
static irqreturn_t dwc2_hsotg_irq(int irq, void *pw)
{
	struct dwc2_hsotg *hsotg = pw;
	int retry_count = 8;
	u32 gintsts;
	u32 gintmsk;

	if (!dwc2_is_device_mode(hsotg))
		return IRQ_NONE;

	spin_lock(&hsotg->lock);
irq_retry:
	gintsts = dwc2_readl(hsotg->regs + GINTSTS);
	gintmsk = dwc2_readl(hsotg->regs + GINTMSK);

	dev_dbg(hsotg->dev, "%s: %08x %08x (%08x) retry %d\n",
		__func__, gintsts, gintsts & gintmsk, gintmsk, retry_count);

	gintsts &= gintmsk;

	if (gintsts & GINTSTS_RESETDET) {
		dev_dbg(hsotg->dev, "%s: USBRstDet\n", __func__);

		dwc2_writel(GINTSTS_RESETDET, hsotg->regs + GINTSTS);

		/* This event must be used only if controller is suspended */
		if (hsotg->lx_state == DWC2_L2) {
			dwc2_exit_hibernation(hsotg, true);
			hsotg->lx_state = DWC2_L0;
		}
	}

	if (gintsts & (GINTSTS_USBRST | GINTSTS_RESETDET)) {

		u32 usb_status = dwc2_readl(hsotg->regs + GOTGCTL);
		u32 connected = hsotg->connected;

		dev_dbg(hsotg->dev, "%s: USBRst\n", __func__);
		dev_dbg(hsotg->dev, "GNPTXSTS=%08x\n",
			dwc2_readl(hsotg->regs + GNPTXSTS));

		dwc2_writel(GINTSTS_USBRST, hsotg->regs + GINTSTS);

		/* Report disconnection if it is not already done. */
		dwc2_hsotg_disconnect(hsotg);

		if (usb_status & GOTGCTL_BSESVLD && connected)
			dwc2_hsotg_core_init_disconnected(hsotg, true);
	}

	if (gintsts & GINTSTS_ENUMDONE) {
		dwc2_writel(GINTSTS_ENUMDONE, hsotg->regs + GINTSTS);

		dwc2_hsotg_irq_enumdone(hsotg);
	}

	if (gintsts & (GINTSTS_OEPINT | GINTSTS_IEPINT)) {
		u32 daint = dwc2_readl(hsotg->regs + DAINT);
		u32 daintmsk = dwc2_readl(hsotg->regs + DAINTMSK);
		u32 daint_out, daint_in;
		int ep;

		daint &= daintmsk;
		daint_out = daint >> DAINT_OUTEP_SHIFT;
		daint_in = daint & ~(daint_out << DAINT_OUTEP_SHIFT);

		dev_dbg(hsotg->dev, "%s: daint=%08x\n", __func__, daint);

		for (ep = 0; ep < hsotg->num_of_eps && daint_out;
						ep++, daint_out >>= 1) {
			if (daint_out & 1)
				dwc2_hsotg_epint(hsotg, ep, 0);
		}

		for (ep = 0; ep < hsotg->num_of_eps  && daint_in;
						ep++, daint_in >>= 1) {
			if (daint_in & 1)
				dwc2_hsotg_epint(hsotg, ep, 1);
		}
	}

	/* check both FIFOs */

	if (gintsts & GINTSTS_NPTXFEMP) {
		dev_dbg(hsotg->dev, "NPTxFEmp\n");

		/*
		 * Disable the interrupt to stop it happening again
		 * unless one of these endpoint routines decides that
		 * it needs re-enabling
		 */

		dwc2_hsotg_disable_gsint(hsotg, GINTSTS_NPTXFEMP);
		dwc2_hsotg_irq_fifoempty(hsotg, false);
	}

	if (gintsts & GINTSTS_PTXFEMP) {
		dev_dbg(hsotg->dev, "PTxFEmp\n");

		/* See note in GINTSTS_NPTxFEmp */

		dwc2_hsotg_disable_gsint(hsotg, GINTSTS_PTXFEMP);
		dwc2_hsotg_irq_fifoempty(hsotg, true);
	}

	if (gintsts & GINTSTS_RXFLVL) {
		/*
		 * note, since GINTSTS_RxFLvl doubles as FIFO-not-empty,
		 * we need to retry dwc2_hsotg_handle_rx if this is still
		 * set.
		 */

		dwc2_hsotg_handle_rx(hsotg);
	}

	if (gintsts & GINTSTS_ERLYSUSP) {
		dev_dbg(hsotg->dev, "GINTSTS_ErlySusp\n");
		dwc2_writel(GINTSTS_ERLYSUSP, hsotg->regs + GINTSTS);
	}

	/*
	 * these next two seem to crop-up occasionally causing the core
	 * to shutdown the USB transfer, so try clearing them and logging
	 * the occurrence.
	 */

	if (gintsts & GINTSTS_GOUTNAKEFF) {
		u8 idx;
		u32 epctrl;
		u32 gintmsk;
		struct dwc2_hsotg_ep *hs_ep;

		/* Mask this interrupt */
		gintmsk = dwc2_readl(hsotg->regs + GINTMSK);
		gintmsk &= ~GINTSTS_GOUTNAKEFF;
		dwc2_writel(gintmsk, hsotg->regs + GINTMSK);

		dev_dbg(hsotg->dev, "GOUTNakEff triggered\n");
		for (idx = 1; idx <= hsotg->num_of_eps; idx++) {
			hs_ep = hsotg->eps_out[idx];
			epctrl = dwc2_readl(hsotg->regs + DOEPCTL(idx));

			if ((epctrl & DXEPCTL_EPENA) && hs_ep->isochronous) {
				epctrl |= DXEPCTL_SNAK;
				epctrl |= DXEPCTL_EPDIS;
				dwc2_writel(epctrl, hsotg->regs + DOEPCTL(idx));
			}
		}

		/* This interrupt bit is cleared in DXEPINT_EPDISBLD handler */
	}

	if (gintsts & GINTSTS_GINNAKEFF) {
		dev_info(hsotg->dev, "GINNakEff triggered\n");

		__orr32(hsotg->regs + DCTL, DCTL_CGNPINNAK);

		dwc2_hsotg_dump(hsotg);
	}

	if (gintsts & GINTSTS_INCOMPL_SOIN)
		dwc2_gadget_handle_incomplete_isoc_in(hsotg);

	if (gintsts & GINTSTS_INCOMPL_SOOUT)
		dwc2_gadget_handle_incomplete_isoc_out(hsotg);

	/*
	 * if we've had fifo events, we should try and go around the
	 * loop again to see if there's any point in returning yet.
	 */

	if (gintsts & IRQ_RETRY_MASK && --retry_count > 0)
			goto irq_retry;

	spin_unlock(&hsotg->lock);

	return IRQ_HANDLED;
}

static int dwc2_hsotg_wait_bit_set(struct dwc2_hsotg *hs_otg, u32 reg,
				   u32 bit, u32 timeout)
{
	u32 i;

	for (i = 0; i < timeout; i++) {
		if (dwc2_readl(hs_otg->regs + reg) & bit)
			return 0;
		udelay(1);
	}

	return -ETIMEDOUT;
}

static void dwc2_hsotg_ep_stop_xfr(struct dwc2_hsotg *hsotg,
				   struct dwc2_hsotg_ep *hs_ep)
{
	u32 epctrl_reg;
	u32 epint_reg;

	epctrl_reg = hs_ep->dir_in ? DIEPCTL(hs_ep->index) :
		DOEPCTL(hs_ep->index);
	epint_reg = hs_ep->dir_in ? DIEPINT(hs_ep->index) :
		DOEPINT(hs_ep->index);

	dev_dbg(hsotg->dev, "%s: stopping transfer on %s\n", __func__,
		hs_ep->name);

	if (hs_ep->dir_in) {
		if (hsotg->dedicated_fifos || hs_ep->periodic) {
			__orr32(hsotg->regs + epctrl_reg, DXEPCTL_SNAK);
			/* Wait for Nak effect */
			if (dwc2_hsotg_wait_bit_set(hsotg, epint_reg,
						    DXEPINT_INEPNAKEFF, 100))
				dev_warn(hsotg->dev,
					 "%s: timeout DIEPINT.NAKEFF\n",
					 __func__);
		} else {
			__orr32(hsotg->regs + DCTL, DCTL_SGNPINNAK);
			/* Wait for Nak effect */
			if (dwc2_hsotg_wait_bit_set(hsotg, GINTSTS,
						    GINTSTS_GINNAKEFF, 100))
				dev_warn(hsotg->dev,
					 "%s: timeout GINTSTS.GINNAKEFF\n",
					 __func__);
		}
	} else {
		if (!(dwc2_readl(hsotg->regs + GINTSTS) & GINTSTS_GOUTNAKEFF))
			__orr32(hsotg->regs + DCTL, DCTL_SGOUTNAK);

		/* Wait for global nak to take effect */
		if (dwc2_hsotg_wait_bit_set(hsotg, GINTSTS,
					    GINTSTS_GOUTNAKEFF, 100))
			dev_warn(hsotg->dev, "%s: timeout GINTSTS.GOUTNAKEFF\n",
				 __func__);
	}

	/* Disable ep */
	__orr32(hsotg->regs + epctrl_reg, DXEPCTL_EPDIS | DXEPCTL_SNAK);

	/* Wait for ep to be disabled */
	if (dwc2_hsotg_wait_bit_set(hsotg, epint_reg, DXEPINT_EPDISBLD, 100))
		dev_warn(hsotg->dev,
			 "%s: timeout DOEPCTL.EPDisable\n", __func__);

	/* Clear EPDISBLD interrupt */
	__orr32(hsotg->regs + epint_reg, DXEPINT_EPDISBLD);

	if (hs_ep->dir_in) {
		unsigned short fifo_index;

		if (hsotg->dedicated_fifos || hs_ep->periodic)
			fifo_index = hs_ep->fifo_index;
		else
			fifo_index = 0;

		/* Flush TX FIFO */
		dwc2_flush_tx_fifo(hsotg, fifo_index);

		/* Clear Global In NP NAK in Shared FIFO for non periodic ep */
		if (!hsotg->dedicated_fifos && !hs_ep->periodic)
			__orr32(hsotg->regs + DCTL, DCTL_CGNPINNAK);

	} else {
		/* Remove global NAKs */
		__orr32(hsotg->regs + DCTL, DCTL_CGOUTNAK);
	}
}

/**
 * dwc2_hsotg_ep_enable - enable the given endpoint
 * @ep: The USB endpint to configure
 * @desc: The USB endpoint descriptor to configure with.
 *
 * This is called from the USB gadget code's usb_ep_enable().
 */
static int dwc2_hsotg_ep_enable(struct usb_ep *ep,
			       const struct usb_endpoint_descriptor *desc)
{
	struct dwc2_hsotg_ep *hs_ep = our_ep(ep);
	struct dwc2_hsotg *hsotg = hs_ep->parent;
	unsigned long flags;
	unsigned int index = hs_ep->index;
	u32 epctrl_reg;
	u32 epctrl;
	u32 mps;
	u32 mc;
	u32 mask;
	unsigned int dir_in;
	unsigned int i, val, size;
	int ret = 0;

	dev_dbg(hsotg->dev,
		"%s: ep %s: a 0x%02x, attr 0x%02x, mps 0x%04x, intr %d\n",
		__func__, ep->name, desc->bEndpointAddress, desc->bmAttributes,
		desc->wMaxPacketSize, desc->bInterval);

	/* not to be called for EP0 */
	if (index == 0) {
		dev_err(hsotg->dev, "%s: called for EP 0\n", __func__);
		return -EINVAL;
	}

	dir_in = (desc->bEndpointAddress & USB_ENDPOINT_DIR_MASK) ? 1 : 0;
	if (dir_in != hs_ep->dir_in) {
		dev_err(hsotg->dev, "%s: direction mismatch!\n", __func__);
		return -EINVAL;
	}

	mps = usb_endpoint_maxp(desc);
	mc = usb_endpoint_maxp_mult(desc);

	/* note, we handle this here instead of dwc2_hsotg_set_ep_maxpacket */

	epctrl_reg = dir_in ? DIEPCTL(index) : DOEPCTL(index);
	epctrl = dwc2_readl(hsotg->regs + epctrl_reg);

	dev_dbg(hsotg->dev, "%s: read DxEPCTL=0x%08x from 0x%08x\n",
		__func__, epctrl, epctrl_reg);

	/* Allocate DMA descriptor chain for non-ctrl endpoints */
	if (using_desc_dma(hsotg) && !hs_ep->desc_list) {
		hs_ep->desc_list = dmam_alloc_coherent(hsotg->dev,
			MAX_DMA_DESC_NUM_GENERIC *
			sizeof(struct dwc2_dma_desc),
			&hs_ep->desc_list_dma, GFP_ATOMIC);
		if (!hs_ep->desc_list) {
			ret = -ENOMEM;
			goto error2;
		}
	}

	spin_lock_irqsave(&hsotg->lock, flags);

	epctrl &= ~(DXEPCTL_EPTYPE_MASK | DXEPCTL_MPS_MASK);
	epctrl |= DXEPCTL_MPS(mps);

	/*
	 * mark the endpoint as active, otherwise the core may ignore
	 * transactions entirely for this endpoint
	 */
	epctrl |= DXEPCTL_USBACTEP;

	/* update the endpoint state */
	dwc2_hsotg_set_ep_maxpacket(hsotg, hs_ep->index, mps, mc, dir_in);

	/* default, set to non-periodic */
	hs_ep->isochronous = 0;
	hs_ep->periodic = 0;
	hs_ep->halted = 0;
	hs_ep->interval = desc->bInterval;

	switch (desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) {
	case USB_ENDPOINT_XFER_ISOC:
		epctrl |= DXEPCTL_EPTYPE_ISO;
		epctrl |= DXEPCTL_SETEVENFR;
		hs_ep->isochronous = 1;
		hs_ep->interval = 1 << (desc->bInterval - 1);
		hs_ep->target_frame = TARGET_FRAME_INITIAL;
		hs_ep->isoc_chain_num = 0;
		hs_ep->next_desc = 0;
		if (dir_in) {
			hs_ep->periodic = 1;
			mask = dwc2_readl(hsotg->regs + DIEPMSK);
			mask |= DIEPMSK_NAKMSK;
			dwc2_writel(mask, hsotg->regs + DIEPMSK);
		} else {
			mask = dwc2_readl(hsotg->regs + DOEPMSK);
			mask |= DOEPMSK_OUTTKNEPDISMSK;
			dwc2_writel(mask, hsotg->regs + DOEPMSK);
		}
		break;

	case USB_ENDPOINT_XFER_BULK:
		epctrl |= DXEPCTL_EPTYPE_BULK;
		break;

	case USB_ENDPOINT_XFER_INT:
		if (dir_in)
			hs_ep->periodic = 1;

		if (hsotg->gadget.speed == USB_SPEED_HIGH)
			hs_ep->interval = 1 << (desc->bInterval - 1);

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
	if (dir_in && hsotg->dedicated_fifos) {
		u32 fifo_index = 0;
		u32 fifo_size = UINT_MAX;
		size = hs_ep->ep.maxpacket*hs_ep->mc;
		for (i = 1; i < hsotg->num_of_eps; ++i) {
			if (hsotg->fifo_map & (1<<i))
				continue;
			val = dwc2_readl(hsotg->regs + DPTXFSIZN(i));
			val = (val >> FIFOSIZE_DEPTH_SHIFT)*4;
			if (val < size)
				continue;
			/* Search for smallest acceptable fifo */
			if (val < fifo_size) {
				fifo_size = val;
				fifo_index = i;
			}
		}
		if (!fifo_index) {
			dev_err(hsotg->dev,
				"%s: No suitable fifo found\n", __func__);
			ret = -ENOMEM;
			goto error1;
		}
		hsotg->fifo_map |= 1 << fifo_index;
		epctrl |= DXEPCTL_TXFNUM(fifo_index);
		hs_ep->fifo_index = fifo_index;
		hs_ep->fifo_size = fifo_size;
	}

	/* for non control endpoints, set PID to D0 */
	if (index && !hs_ep->isochronous)
		epctrl |= DXEPCTL_SETD0PID;

	dev_dbg(hsotg->dev, "%s: write DxEPCTL=0x%08x\n",
		__func__, epctrl);

	dwc2_writel(epctrl, hsotg->regs + epctrl_reg);
	dev_dbg(hsotg->dev, "%s: read DxEPCTL=0x%08x\n",
		__func__, dwc2_readl(hsotg->regs + epctrl_reg));

	/* enable the endpoint interrupt */
	dwc2_hsotg_ctrl_epint(hsotg, index, dir_in, 1);

error1:
	spin_unlock_irqrestore(&hsotg->lock, flags);

error2:
	if (ret && using_desc_dma(hsotg) && hs_ep->desc_list) {
		dmam_free_coherent(hsotg->dev, MAX_DMA_DESC_NUM_GENERIC *
			sizeof(struct dwc2_dma_desc),
			hs_ep->desc_list, hs_ep->desc_list_dma);
		hs_ep->desc_list = NULL;
	}

	return ret;
}

/**
 * dwc2_hsotg_ep_disable - disable given endpoint
 * @ep: The endpoint to disable.
 */
static int dwc2_hsotg_ep_disable(struct usb_ep *ep)
{
	struct dwc2_hsotg_ep *hs_ep = our_ep(ep);
	struct dwc2_hsotg *hsotg = hs_ep->parent;
	int dir_in = hs_ep->dir_in;
	int index = hs_ep->index;
	unsigned long flags;
	u32 epctrl_reg;
	u32 ctrl;

	dev_dbg(hsotg->dev, "%s(ep %p)\n", __func__, ep);

	if (ep == &hsotg->eps_out[0]->ep) {
		dev_err(hsotg->dev, "%s: called for ep0\n", __func__);
		return -EINVAL;
	}

	epctrl_reg = dir_in ? DIEPCTL(index) : DOEPCTL(index);

	spin_lock_irqsave(&hsotg->lock, flags);

	ctrl = dwc2_readl(hsotg->regs + epctrl_reg);

	if (ctrl & DXEPCTL_EPENA)
		dwc2_hsotg_ep_stop_xfr(hsotg, hs_ep);

	ctrl &= ~DXEPCTL_EPENA;
	ctrl &= ~DXEPCTL_USBACTEP;
	ctrl |= DXEPCTL_SNAK;

	dev_dbg(hsotg->dev, "%s: DxEPCTL=0x%08x\n", __func__, ctrl);
	dwc2_writel(ctrl, hsotg->regs + epctrl_reg);

	/* disable endpoint interrupts */
	dwc2_hsotg_ctrl_epint(hsotg, hs_ep->index, hs_ep->dir_in, 0);

	/* terminate all requests with shutdown */
	kill_all_requests(hsotg, hs_ep, -ESHUTDOWN);

	hsotg->fifo_map &= ~(1 << hs_ep->fifo_index);
	hs_ep->fifo_index = 0;
	hs_ep->fifo_size = 0;

	spin_unlock_irqrestore(&hsotg->lock, flags);
	return 0;
}

/**
 * on_list - check request is on the given endpoint
 * @ep: The endpoint to check.
 * @test: The request to test if it is on the endpoint.
 */
static bool on_list(struct dwc2_hsotg_ep *ep, struct dwc2_hsotg_req *test)
{
	struct dwc2_hsotg_req *req, *treq;

	list_for_each_entry_safe(req, treq, &ep->queue, queue) {
		if (req == test)
			return true;
	}

	return false;
}

/**
 * dwc2_hsotg_ep_dequeue - dequeue given endpoint
 * @ep: The endpoint to dequeue.
 * @req: The request to be removed from a queue.
 */
static int dwc2_hsotg_ep_dequeue(struct usb_ep *ep, struct usb_request *req)
{
	struct dwc2_hsotg_req *hs_req = our_req(req);
	struct dwc2_hsotg_ep *hs_ep = our_ep(ep);
	struct dwc2_hsotg *hs = hs_ep->parent;
	unsigned long flags;

	dev_dbg(hs->dev, "ep_dequeue(%p,%p)\n", ep, req);

	spin_lock_irqsave(&hs->lock, flags);

	if (!on_list(hs_ep, hs_req)) {
		spin_unlock_irqrestore(&hs->lock, flags);
		return -EINVAL;
	}

	/* Dequeue already started request */
	if (req == &hs_ep->req->req)
		dwc2_hsotg_ep_stop_xfr(hs, hs_ep);

	dwc2_hsotg_complete_request(hs, hs_ep, hs_req, -ECONNRESET);
	spin_unlock_irqrestore(&hs->lock, flags);

	return 0;
}

/**
 * dwc2_hsotg_ep_sethalt - set halt on a given endpoint
 * @ep: The endpoint to set halt.
 * @value: Set or unset the halt.
 * @now: If true, stall the endpoint now. Otherwise return -EAGAIN if
 *       the endpoint is busy processing requests.
 *
 * We need to stall the endpoint immediately if request comes from set_feature
 * protocol command handler.
 */
static int dwc2_hsotg_ep_sethalt(struct usb_ep *ep, int value, bool now)
{
	struct dwc2_hsotg_ep *hs_ep = our_ep(ep);
	struct dwc2_hsotg *hs = hs_ep->parent;
	int index = hs_ep->index;
	u32 epreg;
	u32 epctl;
	u32 xfertype;

	dev_info(hs->dev, "%s(ep %p %s, %d)\n", __func__, ep, ep->name, value);

	if (index == 0) {
		if (value)
			dwc2_hsotg_stall_ep0(hs);
		else
			dev_warn(hs->dev,
				 "%s: can't clear halt on ep0\n", __func__);
		return 0;
	}

	if (hs_ep->isochronous) {
		dev_err(hs->dev, "%s is Isochronous Endpoint\n", ep->name);
		return -EINVAL;
	}

	if (!now && value && !list_empty(&hs_ep->queue)) {
		dev_dbg(hs->dev, "%s request is pending, cannot halt\n",
			ep->name);
		return -EAGAIN;
	}

	if (hs_ep->dir_in) {
		epreg = DIEPCTL(index);
		epctl = dwc2_readl(hs->regs + epreg);

		if (value) {
			epctl |= DXEPCTL_STALL | DXEPCTL_SNAK;
			if (epctl & DXEPCTL_EPENA)
				epctl |= DXEPCTL_EPDIS;
		} else {
			epctl &= ~DXEPCTL_STALL;
			xfertype = epctl & DXEPCTL_EPTYPE_MASK;
			if (xfertype == DXEPCTL_EPTYPE_BULK ||
				xfertype == DXEPCTL_EPTYPE_INTERRUPT)
					epctl |= DXEPCTL_SETD0PID;
		}
		dwc2_writel(epctl, hs->regs + epreg);
	} else {

		epreg = DOEPCTL(index);
		epctl = dwc2_readl(hs->regs + epreg);

		if (value)
			epctl |= DXEPCTL_STALL;
		else {
			epctl &= ~DXEPCTL_STALL;
			xfertype = epctl & DXEPCTL_EPTYPE_MASK;
			if (xfertype == DXEPCTL_EPTYPE_BULK ||
				xfertype == DXEPCTL_EPTYPE_INTERRUPT)
					epctl |= DXEPCTL_SETD0PID;
		}
		dwc2_writel(epctl, hs->regs + epreg);
	}

	hs_ep->halted = value;

	return 0;
}

/**
 * dwc2_hsotg_ep_sethalt_lock - set halt on a given endpoint with lock held
 * @ep: The endpoint to set halt.
 * @value: Set or unset the halt.
 */
static int dwc2_hsotg_ep_sethalt_lock(struct usb_ep *ep, int value)
{
	struct dwc2_hsotg_ep *hs_ep = our_ep(ep);
	struct dwc2_hsotg *hs = hs_ep->parent;
	unsigned long flags = 0;
	int ret = 0;

	spin_lock_irqsave(&hs->lock, flags);
	ret = dwc2_hsotg_ep_sethalt(ep, value, false);
	spin_unlock_irqrestore(&hs->lock, flags);

	return ret;
}

static struct usb_ep_ops dwc2_hsotg_ep_ops = {
	.enable		= dwc2_hsotg_ep_enable,
	.disable	= dwc2_hsotg_ep_disable,
	.alloc_request	= dwc2_hsotg_ep_alloc_request,
	.free_request	= dwc2_hsotg_ep_free_request,
	.queue		= dwc2_hsotg_ep_queue_lock,
	.dequeue	= dwc2_hsotg_ep_dequeue,
	.set_halt	= dwc2_hsotg_ep_sethalt_lock,
	/* note, don't believe we have any call for the fifo routines */
};

/**
 * dwc2_hsotg_init - initalize the usb core
 * @hsotg: The driver state
 */
static void dwc2_hsotg_init(struct dwc2_hsotg *hsotg)
{
	u32 trdtim;
	u32 usbcfg;
	/* unmask subset of endpoint interrupts */

	dwc2_writel(DIEPMSK_TIMEOUTMSK | DIEPMSK_AHBERRMSK |
		    DIEPMSK_EPDISBLDMSK | DIEPMSK_XFERCOMPLMSK,
		    hsotg->regs + DIEPMSK);

	dwc2_writel(DOEPMSK_SETUPMSK | DOEPMSK_AHBERRMSK |
		    DOEPMSK_EPDISBLDMSK | DOEPMSK_XFERCOMPLMSK,
		    hsotg->regs + DOEPMSK);

	dwc2_writel(0, hsotg->regs + DAINTMSK);

	/* Be in disconnected state until gadget is registered */
	__orr32(hsotg->regs + DCTL, DCTL_SFTDISCON);

	/* setup fifos */

	dev_dbg(hsotg->dev, "GRXFSIZ=0x%08x, GNPTXFSIZ=0x%08x\n",
		dwc2_readl(hsotg->regs + GRXFSIZ),
		dwc2_readl(hsotg->regs + GNPTXFSIZ));

	dwc2_hsotg_init_fifo(hsotg);

	/* keep other bits untouched (so e.g. forced modes are not lost) */
	usbcfg = dwc2_readl(hsotg->regs + GUSBCFG);
	usbcfg &= ~(GUSBCFG_TOUTCAL_MASK | GUSBCFG_PHYIF16 | GUSBCFG_SRPCAP |
		GUSBCFG_HNPCAP | GUSBCFG_USBTRDTIM_MASK);

	/* set the PLL on, remove the HNP/SRP and set the PHY */
	trdtim = (hsotg->phyif == GUSBCFG_PHYIF8) ? 9 : 5;
	usbcfg |= hsotg->phyif | GUSBCFG_TOUTCAL(7) |
		(trdtim << GUSBCFG_USBTRDTIM_SHIFT);
	dwc2_writel(usbcfg, hsotg->regs + GUSBCFG);

	if (using_dma(hsotg))
		__orr32(hsotg->regs + GAHBCFG, GAHBCFG_DMA_EN);
}

/**
 * dwc2_hsotg_udc_start - prepare the udc for work
 * @gadget: The usb gadget state
 * @driver: The usb gadget driver
 *
 * Perform initialization to prepare udc device and driver
 * to work.
 */
static int dwc2_hsotg_udc_start(struct usb_gadget *gadget,
			   struct usb_gadget_driver *driver)
{
	struct dwc2_hsotg *hsotg = to_hsotg(gadget);
	unsigned long flags;
	int ret;

	if (!hsotg) {
		pr_err("%s: called with no device\n", __func__);
		return -ENODEV;
	}

	if (!driver) {
		dev_err(hsotg->dev, "%s: no driver\n", __func__);
		return -EINVAL;
	}

	if (driver->max_speed < USB_SPEED_FULL)
		dev_err(hsotg->dev, "%s: bad speed\n", __func__);

	if (!driver->setup) {
		dev_err(hsotg->dev, "%s: missing entry points\n", __func__);
		return -EINVAL;
	}

	WARN_ON(hsotg->driver);

	driver->driver.bus = NULL;
	hsotg->driver = driver;
	hsotg->gadget.dev.of_node = hsotg->dev->of_node;
	hsotg->gadget.speed = USB_SPEED_UNKNOWN;

	if (hsotg->dr_mode == USB_DR_MODE_PERIPHERAL) {
		ret = dwc2_lowlevel_hw_enable(hsotg);
		if (ret)
			goto err;
	}

	if (!IS_ERR_OR_NULL(hsotg->uphy))
		otg_set_peripheral(hsotg->uphy->otg, &hsotg->gadget);

	spin_lock_irqsave(&hsotg->lock, flags);
	if (dwc2_hw_is_device(hsotg)) {
		dwc2_hsotg_init(hsotg);
		dwc2_hsotg_core_init_disconnected(hsotg, false);
	}

	hsotg->enabled = 0;
	spin_unlock_irqrestore(&hsotg->lock, flags);

	dev_info(hsotg->dev, "bound driver %s\n", driver->driver.name);

	return 0;

err:
	hsotg->driver = NULL;
	return ret;
}

/**
 * dwc2_hsotg_udc_stop - stop the udc
 * @gadget: The usb gadget state
 * @driver: The usb gadget driver
 *
 * Stop udc hw block and stay tunned for future transmissions
 */
static int dwc2_hsotg_udc_stop(struct usb_gadget *gadget)
{
	struct dwc2_hsotg *hsotg = to_hsotg(gadget);
	unsigned long flags = 0;
	int ep;

	if (!hsotg)
		return -ENODEV;

	/* all endpoints should be shutdown */
	for (ep = 1; ep < hsotg->num_of_eps; ep++) {
		if (hsotg->eps_in[ep])
			dwc2_hsotg_ep_disable(&hsotg->eps_in[ep]->ep);
		if (hsotg->eps_out[ep])
			dwc2_hsotg_ep_disable(&hsotg->eps_out[ep]->ep);
	}

	spin_lock_irqsave(&hsotg->lock, flags);

	hsotg->driver = NULL;
	hsotg->gadget.speed = USB_SPEED_UNKNOWN;
	hsotg->enabled = 0;

	spin_unlock_irqrestore(&hsotg->lock, flags);

	if (!IS_ERR_OR_NULL(hsotg->uphy))
		otg_set_peripheral(hsotg->uphy->otg, NULL);

	if (hsotg->dr_mode == USB_DR_MODE_PERIPHERAL)
		dwc2_lowlevel_hw_disable(hsotg);

	return 0;
}

/**
 * dwc2_hsotg_gadget_getframe - read the frame number
 * @gadget: The usb gadget state
 *
 * Read the {micro} frame number
 */
static int dwc2_hsotg_gadget_getframe(struct usb_gadget *gadget)
{
	return dwc2_hsotg_read_frameno(to_hsotg(gadget));
}

/**
 * dwc2_hsotg_pullup - connect/disconnect the USB PHY
 * @gadget: The usb gadget state
 * @is_on: Current state of the USB PHY
 *
 * Connect/Disconnect the USB PHY pullup
 */
static int dwc2_hsotg_pullup(struct usb_gadget *gadget, int is_on)
{
	struct dwc2_hsotg *hsotg = to_hsotg(gadget);
	unsigned long flags = 0;

	dev_dbg(hsotg->dev, "%s: is_on: %d op_state: %d\n", __func__, is_on,
			hsotg->op_state);

	/* Don't modify pullup state while in host mode */
	if (hsotg->op_state != OTG_STATE_B_PERIPHERAL) {
		hsotg->enabled = is_on;
		return 0;
	}

	spin_lock_irqsave(&hsotg->lock, flags);
	if (is_on) {
		hsotg->enabled = 1;
		dwc2_hsotg_core_init_disconnected(hsotg, false);
		dwc2_hsotg_core_connect(hsotg);
	} else {
		dwc2_hsotg_core_disconnect(hsotg);
		dwc2_hsotg_disconnect(hsotg);
		hsotg->enabled = 0;
	}

	hsotg->gadget.speed = USB_SPEED_UNKNOWN;
	spin_unlock_irqrestore(&hsotg->lock, flags);

	return 0;
}

static int dwc2_hsotg_vbus_session(struct usb_gadget *gadget, int is_active)
{
	struct dwc2_hsotg *hsotg = to_hsotg(gadget);
	unsigned long flags;

	dev_dbg(hsotg->dev, "%s: is_active: %d\n", __func__, is_active);
	spin_lock_irqsave(&hsotg->lock, flags);

	/*
	 * If controller is hibernated, it must exit from hibernation
	 * before being initialized / de-initialized
	 */
	if (hsotg->lx_state == DWC2_L2)
		dwc2_exit_hibernation(hsotg, false);

	if (is_active) {
		hsotg->op_state = OTG_STATE_B_PERIPHERAL;

		dwc2_hsotg_core_init_disconnected(hsotg, false);
		if (hsotg->enabled)
			dwc2_hsotg_core_connect(hsotg);
	} else {
		dwc2_hsotg_core_disconnect(hsotg);
		dwc2_hsotg_disconnect(hsotg);
	}

	spin_unlock_irqrestore(&hsotg->lock, flags);
	return 0;
}

/**
 * dwc2_hsotg_vbus_draw - report bMaxPower field
 * @gadget: The usb gadget state
 * @mA: Amount of current
 *
 * Report how much power the device may consume to the phy.
 */
static int dwc2_hsotg_vbus_draw(struct usb_gadget *gadget, unsigned mA)
{
	struct dwc2_hsotg *hsotg = to_hsotg(gadget);

	if (IS_ERR_OR_NULL(hsotg->uphy))
		return -ENOTSUPP;
	return usb_phy_set_power(hsotg->uphy, mA);
}

static const struct usb_gadget_ops dwc2_hsotg_gadget_ops = {
	.get_frame	= dwc2_hsotg_gadget_getframe,
	.udc_start		= dwc2_hsotg_udc_start,
	.udc_stop		= dwc2_hsotg_udc_stop,
	.pullup                 = dwc2_hsotg_pullup,
	.vbus_session		= dwc2_hsotg_vbus_session,
	.vbus_draw		= dwc2_hsotg_vbus_draw,
};

/**
 * dwc2_hsotg_initep - initialise a single endpoint
 * @hsotg: The device state.
 * @hs_ep: The endpoint to be initialised.
 * @epnum: The endpoint number
 *
 * Initialise the given endpoint (as part of the probe and device state
 * creation) to give to the gadget driver. Setup the endpoint name, any
 * direction information and other state that may be required.
 */
static void dwc2_hsotg_initep(struct dwc2_hsotg *hsotg,
				       struct dwc2_hsotg_ep *hs_ep,
				       int epnum,
				       bool dir_in)
{
	char *dir;

	if (epnum == 0)
		dir = "";
	else if (dir_in)
		dir = "in";
	else
		dir = "out";

	hs_ep->dir_in = dir_in;
	hs_ep->index = epnum;

	snprintf(hs_ep->name, sizeof(hs_ep->name), "ep%d%s", epnum, dir);

	INIT_LIST_HEAD(&hs_ep->queue);
	INIT_LIST_HEAD(&hs_ep->ep.ep_list);

	/* add to the list of endpoints known by the gadget driver */
	if (epnum)
		list_add_tail(&hs_ep->ep.ep_list, &hsotg->gadget.ep_list);

	hs_ep->parent = hsotg;
	hs_ep->ep.name = hs_ep->name;

	if (hsotg->params.speed == DWC2_SPEED_PARAM_LOW)
		usb_ep_set_maxpacket_limit(&hs_ep->ep, 8);
	else
		usb_ep_set_maxpacket_limit(&hs_ep->ep,
					   epnum ? 1024 : EP0_MPS_LIMIT);
	hs_ep->ep.ops = &dwc2_hsotg_ep_ops;

	if (epnum == 0) {
		hs_ep->ep.caps.type_control = true;
	} else {
		if (hsotg->params.speed != DWC2_SPEED_PARAM_LOW) {
			hs_ep->ep.caps.type_iso = true;
			hs_ep->ep.caps.type_bulk = true;
		}
		hs_ep->ep.caps.type_int = true;
	}

	if (dir_in)
		hs_ep->ep.caps.dir_in = true;
	else
		hs_ep->ep.caps.dir_out = true;

	/*
	 * if we're using dma, we need to set the next-endpoint pointer
	 * to be something valid.
	 */

	if (using_dma(hsotg)) {
		u32 next = DXEPCTL_NEXTEP((epnum + 1) % 15);
		if (dir_in)
			dwc2_writel(next, hsotg->regs + DIEPCTL(epnum));
		else
			dwc2_writel(next, hsotg->regs + DOEPCTL(epnum));
	}
}

/**
 * dwc2_hsotg_hw_cfg - read HW configuration registers
 * @param: The device state
 *
 * Read the USB core HW configuration registers
 */
static int dwc2_hsotg_hw_cfg(struct dwc2_hsotg *hsotg)
{
	u32 cfg;
	u32 ep_type;
	u32 i;

	/* check hardware configuration */

	hsotg->num_of_eps = hsotg->hw_params.num_dev_ep;

	/* Add ep0 */
	hsotg->num_of_eps++;

	hsotg->eps_in[0] = devm_kzalloc(hsotg->dev, sizeof(struct dwc2_hsotg_ep),
								GFP_KERNEL);
	if (!hsotg->eps_in[0])
		return -ENOMEM;
	/* Same dwc2_hsotg_ep is used in both directions for ep0 */
	hsotg->eps_out[0] = hsotg->eps_in[0];

	cfg = hsotg->hw_params.dev_ep_dirs;
	for (i = 1, cfg >>= 2; i < hsotg->num_of_eps; i++, cfg >>= 2) {
		ep_type = cfg & 3;
		/* Direction in or both */
		if (!(ep_type & 2)) {
			hsotg->eps_in[i] = devm_kzalloc(hsotg->dev,
				sizeof(struct dwc2_hsotg_ep), GFP_KERNEL);
			if (!hsotg->eps_in[i])
				return -ENOMEM;
		}
		/* Direction out or both */
		if (!(ep_type & 1)) {
			hsotg->eps_out[i] = devm_kzalloc(hsotg->dev,
				sizeof(struct dwc2_hsotg_ep), GFP_KERNEL);
			if (!hsotg->eps_out[i])
				return -ENOMEM;
		}
	}

	hsotg->fifo_mem = hsotg->hw_params.total_fifo_size;
	hsotg->dedicated_fifos = hsotg->hw_params.en_multiple_tx_fifo;

	dev_info(hsotg->dev, "EPs: %d, %s fifos, %d entries in SPRAM\n",
		 hsotg->num_of_eps,
		 hsotg->dedicated_fifos ? "dedicated" : "shared",
		 hsotg->fifo_mem);
	return 0;
}

/**
 * dwc2_hsotg_dump - dump state of the udc
 * @param: The device state
 */
static void dwc2_hsotg_dump(struct dwc2_hsotg *hsotg)
{
#ifdef DEBUG
	struct device *dev = hsotg->dev;
	void __iomem *regs = hsotg->regs;
	u32 val;
	int idx;

	dev_info(dev, "DCFG=0x%08x, DCTL=0x%08x, DIEPMSK=%08x\n",
		 dwc2_readl(regs + DCFG), dwc2_readl(regs + DCTL),
		 dwc2_readl(regs + DIEPMSK));

	dev_info(dev, "GAHBCFG=0x%08x, GHWCFG1=0x%08x\n",
		 dwc2_readl(regs + GAHBCFG), dwc2_readl(regs + GHWCFG1));

	dev_info(dev, "GRXFSIZ=0x%08x, GNPTXFSIZ=0x%08x\n",
		 dwc2_readl(regs + GRXFSIZ), dwc2_readl(regs + GNPTXFSIZ));

	/* show periodic fifo settings */

	for (idx = 1; idx < hsotg->num_of_eps; idx++) {
		val = dwc2_readl(regs + DPTXFSIZN(idx));
		dev_info(dev, "DPTx[%d] FSize=%d, StAddr=0x%08x\n", idx,
			 val >> FIFOSIZE_DEPTH_SHIFT,
			 val & FIFOSIZE_STARTADDR_MASK);
	}

	for (idx = 0; idx < hsotg->num_of_eps; idx++) {
		dev_info(dev,
			 "ep%d-in: EPCTL=0x%08x, SIZ=0x%08x, DMA=0x%08x\n", idx,
			 dwc2_readl(regs + DIEPCTL(idx)),
			 dwc2_readl(regs + DIEPTSIZ(idx)),
			 dwc2_readl(regs + DIEPDMA(idx)));

		val = dwc2_readl(regs + DOEPCTL(idx));
		dev_info(dev,
			 "ep%d-out: EPCTL=0x%08x, SIZ=0x%08x, DMA=0x%08x\n",
			 idx, dwc2_readl(regs + DOEPCTL(idx)),
			 dwc2_readl(regs + DOEPTSIZ(idx)),
			 dwc2_readl(regs + DOEPDMA(idx)));

	}

	dev_info(dev, "DVBUSDIS=0x%08x, DVBUSPULSE=%08x\n",
		 dwc2_readl(regs + DVBUSDIS), dwc2_readl(regs + DVBUSPULSE));
#endif
}

/**
 * dwc2_gadget_init - init function for gadget
 * @dwc2: The data structure for the DWC2 driver.
 * @irq: The IRQ number for the controller.
 */
int dwc2_gadget_init(struct dwc2_hsotg *hsotg, int irq)
{
	struct device *dev = hsotg->dev;
	int epnum;
	int ret;

	/* Dump fifo information */
	dev_dbg(dev, "NonPeriodic TXFIFO size: %d\n",
		hsotg->params.g_np_tx_fifo_size);
	dev_dbg(dev, "RXFIFO size: %d\n", hsotg->params.g_rx_fifo_size);

	hsotg->gadget.max_speed = USB_SPEED_HIGH;
	hsotg->gadget.ops = &dwc2_hsotg_gadget_ops;
	hsotg->gadget.name = dev_name(dev);
	if (hsotg->dr_mode == USB_DR_MODE_OTG)
		hsotg->gadget.is_otg = 1;
	else if (hsotg->dr_mode == USB_DR_MODE_PERIPHERAL)
		hsotg->op_state = OTG_STATE_B_PERIPHERAL;

	ret = dwc2_hsotg_hw_cfg(hsotg);
	if (ret) {
		dev_err(hsotg->dev, "Hardware configuration failed: %d\n", ret);
		return ret;
	}

	hsotg->ctrl_buff = devm_kzalloc(hsotg->dev,
			DWC2_CTRL_BUFF_SIZE, GFP_KERNEL);
	if (!hsotg->ctrl_buff)
		return -ENOMEM;

	hsotg->ep0_buff = devm_kzalloc(hsotg->dev,
			DWC2_CTRL_BUFF_SIZE, GFP_KERNEL);
	if (!hsotg->ep0_buff)
		return -ENOMEM;

	if (using_desc_dma(hsotg)) {
		ret = dwc2_gadget_alloc_ctrl_desc_chains(hsotg);
		if (ret < 0)
			return ret;
	}

	ret = devm_request_irq(hsotg->dev, irq, dwc2_hsotg_irq, IRQF_SHARED,
				dev_name(hsotg->dev), hsotg);
	if (ret < 0) {
		dev_err(dev, "cannot claim IRQ for gadget\n");
		return ret;
	}

	/* hsotg->num_of_eps holds number of EPs other than ep0 */

	if (hsotg->num_of_eps == 0) {
		dev_err(dev, "wrong number of EPs (zero)\n");
		return -EINVAL;
	}

	/* setup endpoint information */

	INIT_LIST_HEAD(&hsotg->gadget.ep_list);
	hsotg->gadget.ep0 = &hsotg->eps_out[0]->ep;

	/* allocate EP0 request */

	hsotg->ctrl_req = dwc2_hsotg_ep_alloc_request(&hsotg->eps_out[0]->ep,
						     GFP_KERNEL);
	if (!hsotg->ctrl_req) {
		dev_err(dev, "failed to allocate ctrl req\n");
		return -ENOMEM;
	}

	/* initialise the endpoints now the core has been initialised */
	for (epnum = 0; epnum < hsotg->num_of_eps; epnum++) {
		if (hsotg->eps_in[epnum])
			dwc2_hsotg_initep(hsotg, hsotg->eps_in[epnum],
								epnum, 1);
		if (hsotg->eps_out[epnum])
			dwc2_hsotg_initep(hsotg, hsotg->eps_out[epnum],
								epnum, 0);
	}

	ret = usb_add_gadget_udc(dev, &hsotg->gadget);
	if (ret)
		return ret;

	dwc2_hsotg_dump(hsotg);

	return 0;
}

/**
 * dwc2_hsotg_remove - remove function for hsotg driver
 * @pdev: The platform information for the driver
 */
int dwc2_hsotg_remove(struct dwc2_hsotg *hsotg)
{
	usb_del_gadget_udc(&hsotg->gadget);

	return 0;
}

int dwc2_hsotg_suspend(struct dwc2_hsotg *hsotg)
{
	unsigned long flags;

	if (hsotg->lx_state != DWC2_L0)
		return 0;

	if (hsotg->driver) {
		int ep;

		dev_info(hsotg->dev, "suspending usb gadget %s\n",
			 hsotg->driver->driver.name);

		spin_lock_irqsave(&hsotg->lock, flags);
		if (hsotg->enabled)
			dwc2_hsotg_core_disconnect(hsotg);
		dwc2_hsotg_disconnect(hsotg);
		hsotg->gadget.speed = USB_SPEED_UNKNOWN;
		spin_unlock_irqrestore(&hsotg->lock, flags);

		for (ep = 0; ep < hsotg->num_of_eps; ep++) {
			if (hsotg->eps_in[ep])
				dwc2_hsotg_ep_disable(&hsotg->eps_in[ep]->ep);
			if (hsotg->eps_out[ep])
				dwc2_hsotg_ep_disable(&hsotg->eps_out[ep]->ep);
		}
	}

	return 0;
}

int dwc2_hsotg_resume(struct dwc2_hsotg *hsotg)
{
	unsigned long flags;

	if (hsotg->lx_state == DWC2_L2)
		return 0;

	if (hsotg->driver) {
		dev_info(hsotg->dev, "resuming usb gadget %s\n",
			 hsotg->driver->driver.name);

		spin_lock_irqsave(&hsotg->lock, flags);
		dwc2_hsotg_core_init_disconnected(hsotg, false);
		if (hsotg->enabled)
			dwc2_hsotg_core_connect(hsotg);
		spin_unlock_irqrestore(&hsotg->lock, flags);
	}

	return 0;
}

/**
 * dwc2_backup_device_registers() - Backup controller device registers.
 * When suspending usb bus, registers needs to be backuped
 * if controller power is disabled once suspended.
 *
 * @hsotg: Programming view of the DWC_otg controller
 */
int dwc2_backup_device_registers(struct dwc2_hsotg *hsotg)
{
	struct dwc2_dregs_backup *dr;
	int i;

	dev_dbg(hsotg->dev, "%s\n", __func__);

	/* Backup dev regs */
	dr = &hsotg->dr_backup;

	dr->dcfg = dwc2_readl(hsotg->regs + DCFG);
	dr->dctl = dwc2_readl(hsotg->regs + DCTL);
	dr->daintmsk = dwc2_readl(hsotg->regs + DAINTMSK);
	dr->diepmsk = dwc2_readl(hsotg->regs + DIEPMSK);
	dr->doepmsk = dwc2_readl(hsotg->regs + DOEPMSK);

	for (i = 0; i < hsotg->num_of_eps; i++) {
		/* Backup IN EPs */
		dr->diepctl[i] = dwc2_readl(hsotg->regs + DIEPCTL(i));

		/* Ensure DATA PID is correctly configured */
		if (dr->diepctl[i] & DXEPCTL_DPID)
			dr->diepctl[i] |= DXEPCTL_SETD1PID;
		else
			dr->diepctl[i] |= DXEPCTL_SETD0PID;

		dr->dieptsiz[i] = dwc2_readl(hsotg->regs + DIEPTSIZ(i));
		dr->diepdma[i] = dwc2_readl(hsotg->regs + DIEPDMA(i));

		/* Backup OUT EPs */
		dr->doepctl[i] = dwc2_readl(hsotg->regs + DOEPCTL(i));

		/* Ensure DATA PID is correctly configured */
		if (dr->doepctl[i] & DXEPCTL_DPID)
			dr->doepctl[i] |= DXEPCTL_SETD1PID;
		else
			dr->doepctl[i] |= DXEPCTL_SETD0PID;

		dr->doeptsiz[i] = dwc2_readl(hsotg->regs + DOEPTSIZ(i));
		dr->doepdma[i] = dwc2_readl(hsotg->regs + DOEPDMA(i));
	}
	dr->valid = true;
	return 0;
}

/**
 * dwc2_restore_device_registers() - Restore controller device registers.
 * When resuming usb bus, device registers needs to be restored
 * if controller power were disabled.
 *
 * @hsotg: Programming view of the DWC_otg controller
 */
int dwc2_restore_device_registers(struct dwc2_hsotg *hsotg)
{
	struct dwc2_dregs_backup *dr;
	u32 dctl;
	int i;

	dev_dbg(hsotg->dev, "%s\n", __func__);

	/* Restore dev regs */
	dr = &hsotg->dr_backup;
	if (!dr->valid) {
		dev_err(hsotg->dev, "%s: no device registers to restore\n",
			__func__);
		return -EINVAL;
	}
	dr->valid = false;

	dwc2_writel(dr->dcfg, hsotg->regs + DCFG);
	dwc2_writel(dr->dctl, hsotg->regs + DCTL);
	dwc2_writel(dr->daintmsk, hsotg->regs + DAINTMSK);
	dwc2_writel(dr->diepmsk, hsotg->regs + DIEPMSK);
	dwc2_writel(dr->doepmsk, hsotg->regs + DOEPMSK);

	for (i = 0; i < hsotg->num_of_eps; i++) {
		/* Restore IN EPs */
		dwc2_writel(dr->diepctl[i], hsotg->regs + DIEPCTL(i));
		dwc2_writel(dr->dieptsiz[i], hsotg->regs + DIEPTSIZ(i));
		dwc2_writel(dr->diepdma[i], hsotg->regs + DIEPDMA(i));

		/* Restore OUT EPs */
		dwc2_writel(dr->doepctl[i], hsotg->regs + DOEPCTL(i));
		dwc2_writel(dr->doeptsiz[i], hsotg->regs + DOEPTSIZ(i));
		dwc2_writel(dr->doepdma[i], hsotg->regs + DOEPDMA(i));
	}

	/* Set the Power-On Programming done bit */
	dctl = dwc2_readl(hsotg->regs + DCTL);
	dctl |= DCTL_PWRONPRGDONE;
	dwc2_writel(dctl, hsotg->regs + DCTL);

	return 0;
}
