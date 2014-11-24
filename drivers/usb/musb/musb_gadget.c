/*
 * MUSB OTG driver peripheral support
 *
 * Copyright 2005 Mentor Graphics Corporation
 * Copyright (C) 2005-2006 by Texas Instruments
 * Copyright (C) 2006-2007 Nokia Corporation
 * Copyright (C) 2009 MontaVista Software, Inc. <source@mvista.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 * NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/module.h>
#include <linux/smp.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>

#include "musb_core.h"


/* ----------------------------------------------------------------------- */

#define is_buffer_mapped(req) (is_dma_capable() && \
					(req->map_state != UN_MAPPED))

/* Maps the buffer to dma  */

static inline void map_dma_buffer(struct musb_request *request,
			struct musb *musb, struct musb_ep *musb_ep)
{
	int compatible = true;
	struct dma_controller *dma = musb->dma_controller;

	request->map_state = UN_MAPPED;

	if (!is_dma_capable() || !musb_ep->dma)
		return;

	/* Check if DMA engine can handle this request.
	 * DMA code must reject the USB request explicitly.
	 * Default behaviour is to map the request.
	 */
	if (dma->is_compatible)
		compatible = dma->is_compatible(musb_ep->dma,
				musb_ep->packet_sz, request->request.buf,
				request->request.length);
	if (!compatible)
		return;

	if (request->request.dma == DMA_ADDR_INVALID) {
		dma_addr_t dma_addr;
		int ret;

		dma_addr = dma_map_single(
				musb->controller,
				request->request.buf,
				request->request.length,
				request->tx
					? DMA_TO_DEVICE
					: DMA_FROM_DEVICE);
		ret = dma_mapping_error(musb->controller, dma_addr);
		if (ret)
			return;

		request->request.dma = dma_addr;
		request->map_state = MUSB_MAPPED;
	} else {
		dma_sync_single_for_device(musb->controller,
			request->request.dma,
			request->request.length,
			request->tx
				? DMA_TO_DEVICE
				: DMA_FROM_DEVICE);
		request->map_state = PRE_MAPPED;
	}
}

/* Unmap the buffer from dma and maps it back to cpu */
static inline void unmap_dma_buffer(struct musb_request *request,
				struct musb *musb)
{
	struct musb_ep *musb_ep = request->ep;

	if (!is_buffer_mapped(request) || !musb_ep->dma)
		return;

	if (request->request.dma == DMA_ADDR_INVALID) {
		dev_vdbg(musb->controller,
				"not unmapping a never mapped buffer\n");
		return;
	}
	if (request->map_state == MUSB_MAPPED) {
		dma_unmap_single(musb->controller,
			request->request.dma,
			request->request.length,
			request->tx
				? DMA_TO_DEVICE
				: DMA_FROM_DEVICE);
		request->request.dma = DMA_ADDR_INVALID;
	} else { /* PRE_MAPPED */
		dma_sync_single_for_cpu(musb->controller,
			request->request.dma,
			request->request.length,
			request->tx
				? DMA_TO_DEVICE
				: DMA_FROM_DEVICE);
	}
	request->map_state = UN_MAPPED;
}

/*
 * Immediately complete a request.
 *
 * @param request the request to complete
 * @param status the status to complete the request with
 * Context: controller locked, IRQs blocked.
 */
void musb_g_giveback(
	struct musb_ep		*ep,
	struct usb_request	*request,
	int			status)
__releases(ep->musb->lock)
__acquires(ep->musb->lock)
{
	struct musb_request	*req;
	struct musb		*musb;
	int			busy = ep->busy;

	req = to_musb_request(request);

	list_del(&req->list);
	if (req->request.status == -EINPROGRESS)
		req->request.status = status;
	musb = req->musb;

	ep->busy = 1;
	spin_unlock(&musb->lock);

	if (!dma_mapping_error(&musb->g.dev, request->dma))
		unmap_dma_buffer(req, musb);

	if (request->status == 0)
		dev_dbg(musb->controller, "%s done request %p,  %d/%d\n",
				ep->end_point.name, request,
				req->request.actual, req->request.length);
	else
		dev_dbg(musb->controller, "%s request %p, %d/%d fault %d\n",
				ep->end_point.name, request,
				req->request.actual, req->request.length,
				request->status);
	usb_gadget_giveback_request(&req->ep->end_point, &req->request);
	spin_lock(&musb->lock);
	ep->busy = busy;
}

/* ----------------------------------------------------------------------- */

/*
 * Abort requests queued to an endpoint using the status. Synchronous.
 * caller locked controller and blocked irqs, and selected this ep.
 */
static void nuke(struct musb_ep *ep, const int status)
{
	struct musb		*musb = ep->musb;
	struct musb_request	*req = NULL;
	void __iomem *epio = ep->musb->endpoints[ep->current_epnum].regs;

	ep->busy = 1;

	if (is_dma_capable() && ep->dma) {
		struct dma_controller	*c = ep->musb->dma_controller;
		int value;

		if (ep->is_in) {
			/*
			 * The programming guide says that we must not clear
			 * the DMAMODE bit before DMAENAB, so we only
			 * clear it in the second write...
			 */
			musb_writew(epio, MUSB_TXCSR,
				    MUSB_TXCSR_DMAMODE | MUSB_TXCSR_FLUSHFIFO);
			musb_writew(epio, MUSB_TXCSR,
					0 | MUSB_TXCSR_FLUSHFIFO);
		} else {
			musb_writew(epio, MUSB_RXCSR,
					0 | MUSB_RXCSR_FLUSHFIFO);
			musb_writew(epio, MUSB_RXCSR,
					0 | MUSB_RXCSR_FLUSHFIFO);
		}

		value = c->channel_abort(ep->dma);
		dev_dbg(musb->controller, "%s: abort DMA --> %d\n",
				ep->name, value);
		c->channel_release(ep->dma);
		ep->dma = NULL;
	}

	while (!list_empty(&ep->req_list)) {
		req = list_first_entry(&ep->req_list, struct musb_request, list);
		musb_g_giveback(ep, &req->request, status);
	}
}

/* ----------------------------------------------------------------------- */

/* Data transfers - pure PIO, pure DMA, or mixed mode */

/*
 * This assumes the separate CPPI engine is responding to DMA requests
 * from the usb core ... sequenced a bit differently from mentor dma.
 */

static inline int max_ep_writesize(struct musb *musb, struct musb_ep *ep)
{
	if (can_bulk_split(musb, ep->type))
		return ep->hw_ep->max_packet_sz_tx;
	else
		return ep->packet_sz;
}

/*
 * An endpoint is transmitting data. This can be called either from
 * the IRQ routine or from ep.queue() to kickstart a request on an
 * endpoint.
 *
 * Context: controller locked, IRQs blocked, endpoint selected
 */
static void txstate(struct musb *musb, struct musb_request *req)
{
	u8			epnum = req->epnum;
	struct musb_ep		*musb_ep;
	void __iomem		*epio = musb->endpoints[epnum].regs;
	struct usb_request	*request;
	u16			fifo_count = 0, csr;
	int			use_dma = 0;

	musb_ep = req->ep;

	/* Check if EP is disabled */
	if (!musb_ep->desc) {
		dev_dbg(musb->controller, "ep:%s disabled - ignore request\n",
						musb_ep->end_point.name);
		return;
	}

	/* we shouldn't get here while DMA is active ... but we do ... */
	if (dma_channel_status(musb_ep->dma) == MUSB_DMA_STATUS_BUSY) {
		dev_dbg(musb->controller, "dma pending...\n");
		return;
	}

	/* read TXCSR before */
	csr = musb_readw(epio, MUSB_TXCSR);

	request = &req->request;
	fifo_count = min(max_ep_writesize(musb, musb_ep),
			(int)(request->length - request->actual));

	if (csr & MUSB_TXCSR_TXPKTRDY) {
		dev_dbg(musb->controller, "%s old packet still ready , txcsr %03x\n",
				musb_ep->end_point.name, csr);
		return;
	}

	if (csr & MUSB_TXCSR_P_SENDSTALL) {
		dev_dbg(musb->controller, "%s stalling, txcsr %03x\n",
				musb_ep->end_point.name, csr);
		return;
	}

	dev_dbg(musb->controller, "hw_ep%d, maxpacket %d, fifo count %d, txcsr %03x\n",
			epnum, musb_ep->packet_sz, fifo_count,
			csr);

#ifndef	CONFIG_MUSB_PIO_ONLY
	if (is_buffer_mapped(req)) {
		struct dma_controller	*c = musb->dma_controller;
		size_t request_size;

		/* setup DMA, then program endpoint CSR */
		request_size = min_t(size_t, request->length - request->actual,
					musb_ep->dma->max_len);

		use_dma = (request->dma != DMA_ADDR_INVALID && request_size);

		/* MUSB_TXCSR_P_ISO is still set correctly */

#if defined(CONFIG_USB_INVENTRA_DMA) || defined(CONFIG_USB_UX500_DMA)
		{
			if (request_size < musb_ep->packet_sz)
				musb_ep->dma->desired_mode = 0;
			else
				musb_ep->dma->desired_mode = 1;

			use_dma = use_dma && c->channel_program(
					musb_ep->dma, musb_ep->packet_sz,
					musb_ep->dma->desired_mode,
					request->dma + request->actual, request_size);
			if (use_dma) {
				if (musb_ep->dma->desired_mode == 0) {
					/*
					 * We must not clear the DMAMODE bit
					 * before the DMAENAB bit -- and the
					 * latter doesn't always get cleared
					 * before we get here...
					 */
					csr &= ~(MUSB_TXCSR_AUTOSET
						| MUSB_TXCSR_DMAENAB);
					musb_writew(epio, MUSB_TXCSR, csr
						| MUSB_TXCSR_P_WZC_BITS);
					csr &= ~MUSB_TXCSR_DMAMODE;
					csr |= (MUSB_TXCSR_DMAENAB |
							MUSB_TXCSR_MODE);
					/* against programming guide */
				} else {
					csr |= (MUSB_TXCSR_DMAENAB
							| MUSB_TXCSR_DMAMODE
							| MUSB_TXCSR_MODE);
					/*
					 * Enable Autoset according to table
					 * below
					 * bulk_split hb_mult	Autoset_Enable
					 *	0	0	Yes(Normal)
					 *	0	>0	No(High BW ISO)
					 *	1	0	Yes(HS bulk)
					 *	1	>0	Yes(FS bulk)
					 */
					if (!musb_ep->hb_mult ||
						(musb_ep->hb_mult &&
						 can_bulk_split(musb,
						    musb_ep->type)))
						csr |= MUSB_TXCSR_AUTOSET;
				}
				csr &= ~MUSB_TXCSR_P_UNDERRUN;

				musb_writew(epio, MUSB_TXCSR, csr);
			}
		}

#endif
		if (is_cppi_enabled()) {
			/* program endpoint CSR first, then setup DMA */
			csr &= ~(MUSB_TXCSR_P_UNDERRUN | MUSB_TXCSR_TXPKTRDY);
			csr |= MUSB_TXCSR_DMAENAB | MUSB_TXCSR_DMAMODE |
				MUSB_TXCSR_MODE;
			musb_writew(epio, MUSB_TXCSR, (MUSB_TXCSR_P_WZC_BITS &
						~MUSB_TXCSR_P_UNDERRUN) | csr);

			/* ensure writebuffer is empty */
			csr = musb_readw(epio, MUSB_TXCSR);

			/*
			 * NOTE host side sets DMAENAB later than this; both are
			 * OK since the transfer dma glue (between CPPI and
			 * Mentor fifos) just tells CPPI it could start. Data
			 * only moves to the USB TX fifo when both fifos are
			 * ready.
			 */
			/*
			 * "mode" is irrelevant here; handle terminating ZLPs
			 * like PIO does, since the hardware RNDIS mode seems
			 * unreliable except for the
			 * last-packet-is-already-short case.
			 */
			use_dma = use_dma && c->channel_program(
					musb_ep->dma, musb_ep->packet_sz,
					0,
					request->dma + request->actual,
					request_size);
			if (!use_dma) {
				c->channel_release(musb_ep->dma);
				musb_ep->dma = NULL;
				csr &= ~MUSB_TXCSR_DMAENAB;
				musb_writew(epio, MUSB_TXCSR, csr);
				/* invariant: prequest->buf is non-null */
			}
		} else if (tusb_dma_omap())
			use_dma = use_dma && c->channel_program(
					musb_ep->dma, musb_ep->packet_sz,
					request->zero,
					request->dma + request->actual,
					request_size);
	}
#endif

	if (!use_dma) {
		/*
		 * Unmap the dma buffer back to cpu if dma channel
		 * programming fails
		 */
		unmap_dma_buffer(req, musb);

		musb_write_fifo(musb_ep->hw_ep, fifo_count,
				(u8 *) (request->buf + request->actual));
		request->actual += fifo_count;
		csr |= MUSB_TXCSR_TXPKTRDY;
		csr &= ~MUSB_TXCSR_P_UNDERRUN;
		musb_writew(epio, MUSB_TXCSR, csr);
	}

	/* host may already have the data when this message shows... */
	dev_dbg(musb->controller, "%s TX/IN %s len %d/%d, txcsr %04x, fifo %d/%d\n",
			musb_ep->end_point.name, use_dma ? "dma" : "pio",
			request->actual, request->length,
			musb_readw(epio, MUSB_TXCSR),
			fifo_count,
			musb_readw(epio, MUSB_TXMAXP));
}

/*
 * FIFO state update (e.g. data ready).
 * Called from IRQ,  with controller locked.
 */
void musb_g_tx(struct musb *musb, u8 epnum)
{
	u16			csr;
	struct musb_request	*req;
	struct usb_request	*request;
	u8 __iomem		*mbase = musb->mregs;
	struct musb_ep		*musb_ep = &musb->endpoints[epnum].ep_in;
	void __iomem		*epio = musb->endpoints[epnum].regs;
	struct dma_channel	*dma;

	musb_ep_select(mbase, epnum);
	req = next_request(musb_ep);
	request = &req->request;

	csr = musb_readw(epio, MUSB_TXCSR);
	dev_dbg(musb->controller, "<== %s, txcsr %04x\n", musb_ep->end_point.name, csr);

	dma = is_dma_capable() ? musb_ep->dma : NULL;

	/*
	 * REVISIT: for high bandwidth, MUSB_TXCSR_P_INCOMPTX
	 * probably rates reporting as a host error.
	 */
	if (csr & MUSB_TXCSR_P_SENTSTALL) {
		csr |=	MUSB_TXCSR_P_WZC_BITS;
		csr &= ~MUSB_TXCSR_P_SENTSTALL;
		musb_writew(epio, MUSB_TXCSR, csr);
		return;
	}

	if (csr & MUSB_TXCSR_P_UNDERRUN) {
		/* We NAKed, no big deal... little reason to care. */
		csr |=	 MUSB_TXCSR_P_WZC_BITS;
		csr &= ~(MUSB_TXCSR_P_UNDERRUN | MUSB_TXCSR_TXPKTRDY);
		musb_writew(epio, MUSB_TXCSR, csr);
		dev_vdbg(musb->controller, "underrun on ep%d, req %p\n",
				epnum, request);
	}

	if (dma_channel_status(dma) == MUSB_DMA_STATUS_BUSY) {
		/*
		 * SHOULD NOT HAPPEN... has with CPPI though, after
		 * changing SENDSTALL (and other cases); harmless?
		 */
		dev_dbg(musb->controller, "%s dma still busy?\n", musb_ep->end_point.name);
		return;
	}

	if (request) {
		u8	is_dma = 0;

		if (dma && (csr & MUSB_TXCSR_DMAENAB)) {
			is_dma = 1;
			csr |= MUSB_TXCSR_P_WZC_BITS;
			csr &= ~(MUSB_TXCSR_DMAENAB | MUSB_TXCSR_P_UNDERRUN |
				 MUSB_TXCSR_TXPKTRDY | MUSB_TXCSR_AUTOSET);
			musb_writew(epio, MUSB_TXCSR, csr);
			/* Ensure writebuffer is empty. */
			csr = musb_readw(epio, MUSB_TXCSR);
			request->actual += musb_ep->dma->actual_len;
			dev_dbg(musb->controller, "TXCSR%d %04x, DMA off, len %zu, req %p\n",
				epnum, csr, musb_ep->dma->actual_len, request);
		}

		/*
		 * First, maybe a terminating short packet. Some DMA
		 * engines might handle this by themselves.
		 */
		if ((request->zero && request->length
			&& (request->length % musb_ep->packet_sz == 0)
			&& (request->actual == request->length))
#if defined(CONFIG_USB_INVENTRA_DMA) || defined(CONFIG_USB_UX500_DMA)
			|| (is_dma && (!dma->desired_mode ||
				(request->actual &
					(musb_ep->packet_sz - 1))))
#endif
		) {
			/*
			 * On DMA completion, FIFO may not be
			 * available yet...
			 */
			if (csr & MUSB_TXCSR_TXPKTRDY)
				return;

			dev_dbg(musb->controller, "sending zero pkt\n");
			musb_writew(epio, MUSB_TXCSR, MUSB_TXCSR_MODE
					| MUSB_TXCSR_TXPKTRDY);
			request->zero = 0;
		}

		if (request->actual == request->length) {
			musb_g_giveback(musb_ep, request, 0);
			/*
			 * In the giveback function the MUSB lock is
			 * released and acquired after sometime. During
			 * this time period the INDEX register could get
			 * changed by the gadget_queue function especially
			 * on SMP systems. Reselect the INDEX to be sure
			 * we are reading/modifying the right registers
			 */
			musb_ep_select(mbase, epnum);
			req = musb_ep->desc ? next_request(musb_ep) : NULL;
			if (!req) {
				dev_dbg(musb->controller, "%s idle now\n",
					musb_ep->end_point.name);
				return;
			}
		}

		txstate(musb, req);
	}
}

/* ------------------------------------------------------------ */

/*
 * Context: controller locked, IRQs blocked, endpoint selected
 */
static void rxstate(struct musb *musb, struct musb_request *req)
{
	const u8		epnum = req->epnum;
	struct usb_request	*request = &req->request;
	struct musb_ep		*musb_ep;
	void __iomem		*epio = musb->endpoints[epnum].regs;
	unsigned		len = 0;
	u16			fifo_count;
	u16			csr = musb_readw(epio, MUSB_RXCSR);
	struct musb_hw_ep	*hw_ep = &musb->endpoints[epnum];
	u8			use_mode_1;

	if (hw_ep->is_shared_fifo)
		musb_ep = &hw_ep->ep_in;
	else
		musb_ep = &hw_ep->ep_out;

	fifo_count = musb_ep->packet_sz;

	/* Check if EP is disabled */
	if (!musb_ep->desc) {
		dev_dbg(musb->controller, "ep:%s disabled - ignore request\n",
						musb_ep->end_point.name);
		return;
	}

	/* We shouldn't get here while DMA is active, but we do... */
	if (dma_channel_status(musb_ep->dma) == MUSB_DMA_STATUS_BUSY) {
		dev_dbg(musb->controller, "DMA pending...\n");
		return;
	}

	if (csr & MUSB_RXCSR_P_SENDSTALL) {
		dev_dbg(musb->controller, "%s stalling, RXCSR %04x\n",
		    musb_ep->end_point.name, csr);
		return;
	}

	if (is_cppi_enabled() && is_buffer_mapped(req)) {
		struct dma_controller	*c = musb->dma_controller;
		struct dma_channel	*channel = musb_ep->dma;

		/* NOTE:  CPPI won't actually stop advancing the DMA
		 * queue after short packet transfers, so this is almost
		 * always going to run as IRQ-per-packet DMA so that
		 * faults will be handled correctly.
		 */
		if (c->channel_program(channel,
				musb_ep->packet_sz,
				!request->short_not_ok,
				request->dma + request->actual,
				request->length - request->actual)) {

			/* make sure that if an rxpkt arrived after the irq,
			 * the cppi engine will be ready to take it as soon
			 * as DMA is enabled
			 */
			csr &= ~(MUSB_RXCSR_AUTOCLEAR
					| MUSB_RXCSR_DMAMODE);
			csr |= MUSB_RXCSR_DMAENAB | MUSB_RXCSR_P_WZC_BITS;
			musb_writew(epio, MUSB_RXCSR, csr);
			return;
		}
	}

	if (csr & MUSB_RXCSR_RXPKTRDY) {
		fifo_count = musb_readw(epio, MUSB_RXCOUNT);

		/*
		 * Enable Mode 1 on RX transfers only when short_not_ok flag
		 * is set. Currently short_not_ok flag is set only from
		 * file_storage and f_mass_storage drivers
		 */

		if (request->short_not_ok && fifo_count == musb_ep->packet_sz)
			use_mode_1 = 1;
		else
			use_mode_1 = 0;

		if (request->actual < request->length) {
#ifdef CONFIG_USB_INVENTRA_DMA
			if (is_buffer_mapped(req)) {
				struct dma_controller	*c;
				struct dma_channel	*channel;
				int			use_dma = 0;
				unsigned int transfer_size;

				c = musb->dma_controller;
				channel = musb_ep->dma;

	/* We use DMA Req mode 0 in rx_csr, and DMA controller operates in
	 * mode 0 only. So we do not get endpoint interrupts due to DMA
	 * completion. We only get interrupts from DMA controller.
	 *
	 * We could operate in DMA mode 1 if we knew the size of the tranfer
	 * in advance. For mass storage class, request->length = what the host
	 * sends, so that'd work.  But for pretty much everything else,
	 * request->length is routinely more than what the host sends. For
	 * most these gadgets, end of is signified either by a short packet,
	 * or filling the last byte of the buffer.  (Sending extra data in
	 * that last pckate should trigger an overflow fault.)  But in mode 1,
	 * we don't get DMA completion interrupt for short packets.
	 *
	 * Theoretically, we could enable DMAReq irq (MUSB_RXCSR_DMAMODE = 1),
	 * to get endpoint interrupt on every DMA req, but that didn't seem
	 * to work reliably.
	 *
	 * REVISIT an updated g_file_storage can set req->short_not_ok, which
	 * then becomes usable as a runtime "use mode 1" hint...
	 */

				/* Experimental: Mode1 works with mass storage use cases */
				if (use_mode_1) {
					csr |= MUSB_RXCSR_AUTOCLEAR;
					musb_writew(epio, MUSB_RXCSR, csr);
					csr |= MUSB_RXCSR_DMAENAB;
					musb_writew(epio, MUSB_RXCSR, csr);

					/*
					 * this special sequence (enabling and then
					 * disabling MUSB_RXCSR_DMAMODE) is required
					 * to get DMAReq to activate
					 */
					musb_writew(epio, MUSB_RXCSR,
						csr | MUSB_RXCSR_DMAMODE);
					musb_writew(epio, MUSB_RXCSR, csr);

					transfer_size = min_t(unsigned int,
							request->length -
							request->actual,
							channel->max_len);
					musb_ep->dma->desired_mode = 1;
				} else {
					if (!musb_ep->hb_mult &&
						musb_ep->hw_ep->rx_double_buffered)
						csr |= MUSB_RXCSR_AUTOCLEAR;
					csr |= MUSB_RXCSR_DMAENAB;
					musb_writew(epio, MUSB_RXCSR, csr);

					transfer_size = min(request->length - request->actual,
							(unsigned)fifo_count);
					musb_ep->dma->desired_mode = 0;
				}

				use_dma = c->channel_program(
						channel,
						musb_ep->packet_sz,
						channel->desired_mode,
						request->dma
						+ request->actual,
						transfer_size);

				if (use_dma)
					return;
			}
#elif defined(CONFIG_USB_UX500_DMA)
			if ((is_buffer_mapped(req)) &&
				(request->actual < request->length)) {

				struct dma_controller *c;
				struct dma_channel *channel;
				unsigned int transfer_size = 0;

				c = musb->dma_controller;
				channel = musb_ep->dma;

				/* In case first packet is short */
				if (fifo_count < musb_ep->packet_sz)
					transfer_size = fifo_count;
				else if (request->short_not_ok)
					transfer_size =	min_t(unsigned int,
							request->length -
							request->actual,
							channel->max_len);
				else
					transfer_size = min_t(unsigned int,
							request->length -
							request->actual,
							(unsigned)fifo_count);

				csr &= ~MUSB_RXCSR_DMAMODE;
				csr |= (MUSB_RXCSR_DMAENAB |
					MUSB_RXCSR_AUTOCLEAR);

				musb_writew(epio, MUSB_RXCSR, csr);

				if (transfer_size <= musb_ep->packet_sz) {
					musb_ep->dma->desired_mode = 0;
				} else {
					musb_ep->dma->desired_mode = 1;
					/* Mode must be set after DMAENAB */
					csr |= MUSB_RXCSR_DMAMODE;
					musb_writew(epio, MUSB_RXCSR, csr);
				}

				if (c->channel_program(channel,
							musb_ep->packet_sz,
							channel->desired_mode,
							request->dma
							+ request->actual,
							transfer_size))

					return;
			}
#endif	/* Mentor's DMA */

			len = request->length - request->actual;
			dev_dbg(musb->controller, "%s OUT/RX pio fifo %d/%d, maxpacket %d\n",
					musb_ep->end_point.name,
					fifo_count, len,
					musb_ep->packet_sz);

			fifo_count = min_t(unsigned, len, fifo_count);

#ifdef	CONFIG_USB_TUSB_OMAP_DMA
			if (tusb_dma_omap() && is_buffer_mapped(req)) {
				struct dma_controller *c = musb->dma_controller;
				struct dma_channel *channel = musb_ep->dma;
				u32 dma_addr = request->dma + request->actual;
				int ret;

				ret = c->channel_program(channel,
						musb_ep->packet_sz,
						channel->desired_mode,
						dma_addr,
						fifo_count);
				if (ret)
					return;
			}
#endif
			/*
			 * Unmap the dma buffer back to cpu if dma channel
			 * programming fails. This buffer is mapped if the
			 * channel allocation is successful
			 */
			 if (is_buffer_mapped(req)) {
				unmap_dma_buffer(req, musb);

				/*
				 * Clear DMAENAB and AUTOCLEAR for the
				 * PIO mode transfer
				 */
				csr &= ~(MUSB_RXCSR_DMAENAB | MUSB_RXCSR_AUTOCLEAR);
				musb_writew(epio, MUSB_RXCSR, csr);
			}

			musb_read_fifo(musb_ep->hw_ep, fifo_count, (u8 *)
					(request->buf + request->actual));
			request->actual += fifo_count;

			/* REVISIT if we left anything in the fifo, flush
			 * it and report -EOVERFLOW
			 */

			/* ack the read! */
			csr |= MUSB_RXCSR_P_WZC_BITS;
			csr &= ~MUSB_RXCSR_RXPKTRDY;
			musb_writew(epio, MUSB_RXCSR, csr);
		}
	}

	/* reach the end or short packet detected */
	if (request->actual == request->length ||
	    fifo_count < musb_ep->packet_sz)
		musb_g_giveback(musb_ep, request, 0);
}

/*
 * Data ready for a request; called from IRQ
 */
void musb_g_rx(struct musb *musb, u8 epnum)
{
	u16			csr;
	struct musb_request	*req;
	struct usb_request	*request;
	void __iomem		*mbase = musb->mregs;
	struct musb_ep		*musb_ep;
	void __iomem		*epio = musb->endpoints[epnum].regs;
	struct dma_channel	*dma;
	struct musb_hw_ep	*hw_ep = &musb->endpoints[epnum];

	if (hw_ep->is_shared_fifo)
		musb_ep = &hw_ep->ep_in;
	else
		musb_ep = &hw_ep->ep_out;

	musb_ep_select(mbase, epnum);

	req = next_request(musb_ep);
	if (!req)
		return;

	request = &req->request;

	csr = musb_readw(epio, MUSB_RXCSR);
	dma = is_dma_capable() ? musb_ep->dma : NULL;

	dev_dbg(musb->controller, "<== %s, rxcsr %04x%s %p\n", musb_ep->end_point.name,
			csr, dma ? " (dma)" : "", request);

	if (csr & MUSB_RXCSR_P_SENTSTALL) {
		csr |= MUSB_RXCSR_P_WZC_BITS;
		csr &= ~MUSB_RXCSR_P_SENTSTALL;
		musb_writew(epio, MUSB_RXCSR, csr);
		return;
	}

	if (csr & MUSB_RXCSR_P_OVERRUN) {
		/* csr |= MUSB_RXCSR_P_WZC_BITS; */
		csr &= ~MUSB_RXCSR_P_OVERRUN;
		musb_writew(epio, MUSB_RXCSR, csr);

		dev_dbg(musb->controller, "%s iso overrun on %p\n", musb_ep->name, request);
		if (request->status == -EINPROGRESS)
			request->status = -EOVERFLOW;
	}
	if (csr & MUSB_RXCSR_INCOMPRX) {
		/* REVISIT not necessarily an error */
		dev_dbg(musb->controller, "%s, incomprx\n", musb_ep->end_point.name);
	}

	if (dma_channel_status(dma) == MUSB_DMA_STATUS_BUSY) {
		/* "should not happen"; likely RXPKTRDY pending for DMA */
		dev_dbg(musb->controller, "%s busy, csr %04x\n",
			musb_ep->end_point.name, csr);
		return;
	}

	if (dma && (csr & MUSB_RXCSR_DMAENAB)) {
		csr &= ~(MUSB_RXCSR_AUTOCLEAR
				| MUSB_RXCSR_DMAENAB
				| MUSB_RXCSR_DMAMODE);
		musb_writew(epio, MUSB_RXCSR,
			MUSB_RXCSR_P_WZC_BITS | csr);

		request->actual += musb_ep->dma->actual_len;

		dev_dbg(musb->controller, "RXCSR%d %04x, dma off, %04x, len %zu, req %p\n",
			epnum, csr,
			musb_readw(epio, MUSB_RXCSR),
			musb_ep->dma->actual_len, request);

#if defined(CONFIG_USB_INVENTRA_DMA) || defined(CONFIG_USB_TUSB_OMAP_DMA) || \
	defined(CONFIG_USB_UX500_DMA)
		/* Autoclear doesn't clear RxPktRdy for short packets */
		if ((dma->desired_mode == 0 && !hw_ep->rx_double_buffered)
				|| (dma->actual_len
					& (musb_ep->packet_sz - 1))) {
			/* ack the read! */
			csr &= ~MUSB_RXCSR_RXPKTRDY;
			musb_writew(epio, MUSB_RXCSR, csr);
		}

		/* incomplete, and not short? wait for next IN packet */
		if ((request->actual < request->length)
				&& (musb_ep->dma->actual_len
					== musb_ep->packet_sz)) {
			/* In double buffer case, continue to unload fifo if
 			 * there is Rx packet in FIFO.
 			 **/
			csr = musb_readw(epio, MUSB_RXCSR);
			if ((csr & MUSB_RXCSR_RXPKTRDY) &&
				hw_ep->rx_double_buffered)
				goto exit;
			return;
		}
#endif
		musb_g_giveback(musb_ep, request, 0);
		/*
		 * In the giveback function the MUSB lock is
		 * released and acquired after sometime. During
		 * this time period the INDEX register could get
		 * changed by the gadget_queue function especially
		 * on SMP systems. Reselect the INDEX to be sure
		 * we are reading/modifying the right registers
		 */
		musb_ep_select(mbase, epnum);

		req = next_request(musb_ep);
		if (!req)
			return;
	}
#if defined(CONFIG_USB_INVENTRA_DMA) || defined(CONFIG_USB_TUSB_OMAP_DMA) || \
	defined(CONFIG_USB_UX500_DMA)
exit:
#endif
	/* Analyze request */
	rxstate(musb, req);
}

/* ------------------------------------------------------------ */

static int musb_gadget_enable(struct usb_ep *ep,
			const struct usb_endpoint_descriptor *desc)
{
	unsigned long		flags;
	struct musb_ep		*musb_ep;
	struct musb_hw_ep	*hw_ep;
	void __iomem		*regs;
	struct musb		*musb;
	void __iomem	*mbase;
	u8		epnum;
	u16		csr;
	unsigned	tmp;
	int		status = -EINVAL;

	if (!ep || !desc)
		return -EINVAL;

	musb_ep = to_musb_ep(ep);
	hw_ep = musb_ep->hw_ep;
	regs = hw_ep->regs;
	musb = musb_ep->musb;
	mbase = musb->mregs;
	epnum = musb_ep->current_epnum;

	spin_lock_irqsave(&musb->lock, flags);

	if (musb_ep->desc) {
		status = -EBUSY;
		goto fail;
	}
	musb_ep->type = usb_endpoint_type(desc);

	/* check direction and (later) maxpacket size against endpoint */
	if (usb_endpoint_num(desc) != epnum)
		goto fail;

	/* REVISIT this rules out high bandwidth periodic transfers */
	tmp = usb_endpoint_maxp(desc);
	if (tmp & ~0x07ff) {
		int ok;

		if (usb_endpoint_dir_in(desc))
			ok = musb->hb_iso_tx;
		else
			ok = musb->hb_iso_rx;

		if (!ok) {
			dev_dbg(musb->controller, "no support for high bandwidth ISO\n");
			goto fail;
		}
		musb_ep->hb_mult = (tmp >> 11) & 3;
	} else {
		musb_ep->hb_mult = 0;
	}

	musb_ep->packet_sz = tmp & 0x7ff;
	tmp = musb_ep->packet_sz * (musb_ep->hb_mult + 1);

	/* enable the interrupts for the endpoint, set the endpoint
	 * packet size (or fail), set the mode, clear the fifo
	 */
	musb_ep_select(mbase, epnum);
	if (usb_endpoint_dir_in(desc)) {

		if (hw_ep->is_shared_fifo)
			musb_ep->is_in = 1;
		if (!musb_ep->is_in)
			goto fail;

		if (tmp > hw_ep->max_packet_sz_tx) {
			dev_dbg(musb->controller, "packet size beyond hardware FIFO size\n");
			goto fail;
		}

		musb->intrtxe |= (1 << epnum);
		musb_writew(mbase, MUSB_INTRTXE, musb->intrtxe);

		/* REVISIT if can_bulk_split(), use by updating "tmp";
		 * likewise high bandwidth periodic tx
		 */
		/* Set TXMAXP with the FIFO size of the endpoint
		 * to disable double buffering mode.
		 */
		if (musb->double_buffer_not_ok) {
			musb_writew(regs, MUSB_TXMAXP, hw_ep->max_packet_sz_tx);
		} else {
			if (can_bulk_split(musb, musb_ep->type))
				musb_ep->hb_mult = (hw_ep->max_packet_sz_tx /
							musb_ep->packet_sz) - 1;
			musb_writew(regs, MUSB_TXMAXP, musb_ep->packet_sz
					| (musb_ep->hb_mult << 11));
		}

		csr = MUSB_TXCSR_MODE | MUSB_TXCSR_CLRDATATOG;
		if (musb_readw(regs, MUSB_TXCSR)
				& MUSB_TXCSR_FIFONOTEMPTY)
			csr |= MUSB_TXCSR_FLUSHFIFO;
		if (musb_ep->type == USB_ENDPOINT_XFER_ISOC)
			csr |= MUSB_TXCSR_P_ISO;

		/* set twice in case of double buffering */
		musb_writew(regs, MUSB_TXCSR, csr);
		/* REVISIT may be inappropriate w/o FIFONOTEMPTY ... */
		musb_writew(regs, MUSB_TXCSR, csr);

	} else {

		if (hw_ep->is_shared_fifo)
			musb_ep->is_in = 0;
		if (musb_ep->is_in)
			goto fail;

		if (tmp > hw_ep->max_packet_sz_rx) {
			dev_dbg(musb->controller, "packet size beyond hardware FIFO size\n");
			goto fail;
		}

		musb->intrrxe |= (1 << epnum);
		musb_writew(mbase, MUSB_INTRRXE, musb->intrrxe);

		/* REVISIT if can_bulk_combine() use by updating "tmp"
		 * likewise high bandwidth periodic rx
		 */
		/* Set RXMAXP with the FIFO size of the endpoint
		 * to disable double buffering mode.
		 */
		if (musb->double_buffer_not_ok)
			musb_writew(regs, MUSB_RXMAXP, hw_ep->max_packet_sz_tx);
		else
			musb_writew(regs, MUSB_RXMAXP, musb_ep->packet_sz
					| (musb_ep->hb_mult << 11));

		/* force shared fifo to OUT-only mode */
		if (hw_ep->is_shared_fifo) {
			csr = musb_readw(regs, MUSB_TXCSR);
			csr &= ~(MUSB_TXCSR_MODE | MUSB_TXCSR_TXPKTRDY);
			musb_writew(regs, MUSB_TXCSR, csr);
		}

		csr = MUSB_RXCSR_FLUSHFIFO | MUSB_RXCSR_CLRDATATOG;
		if (musb_ep->type == USB_ENDPOINT_XFER_ISOC)
			csr |= MUSB_RXCSR_P_ISO;
		else if (musb_ep->type == USB_ENDPOINT_XFER_INT)
			csr |= MUSB_RXCSR_DISNYET;

		/* set twice in case of double buffering */
		musb_writew(regs, MUSB_RXCSR, csr);
		musb_writew(regs, MUSB_RXCSR, csr);
	}

	/* NOTE:  all the I/O code _should_ work fine without DMA, in case
	 * for some reason you run out of channels here.
	 */
	if (is_dma_capable() && musb->dma_controller) {
		struct dma_controller	*c = musb->dma_controller;

		musb_ep->dma = c->channel_alloc(c, hw_ep,
				(desc->bEndpointAddress & USB_DIR_IN));
	} else
		musb_ep->dma = NULL;

	musb_ep->desc = desc;
	musb_ep->busy = 0;
	musb_ep->wedged = 0;
	status = 0;

	pr_debug("%s periph: enabled %s for %s %s, %smaxpacket %d\n",
			musb_driver_name, musb_ep->end_point.name,
			({ char *s; switch (musb_ep->type) {
			case USB_ENDPOINT_XFER_BULK:	s = "bulk"; break;
			case USB_ENDPOINT_XFER_INT:	s = "int"; break;
			default:			s = "iso"; break;
			} s; }),
			musb_ep->is_in ? "IN" : "OUT",
			musb_ep->dma ? "dma, " : "",
			musb_ep->packet_sz);

	schedule_work(&musb->irq_work);

fail:
	spin_unlock_irqrestore(&musb->lock, flags);
	return status;
}

/*
 * Disable an endpoint flushing all requests queued.
 */
static int musb_gadget_disable(struct usb_ep *ep)
{
	unsigned long	flags;
	struct musb	*musb;
	u8		epnum;
	struct musb_ep	*musb_ep;
	void __iomem	*epio;
	int		status = 0;

	musb_ep = to_musb_ep(ep);
	musb = musb_ep->musb;
	epnum = musb_ep->current_epnum;
	epio = musb->endpoints[epnum].regs;

	spin_lock_irqsave(&musb->lock, flags);
	musb_ep_select(musb->mregs, epnum);

	/* zero the endpoint sizes */
	if (musb_ep->is_in) {
		musb->intrtxe &= ~(1 << epnum);
		musb_writew(musb->mregs, MUSB_INTRTXE, musb->intrtxe);
		musb_writew(epio, MUSB_TXMAXP, 0);
	} else {
		musb->intrrxe &= ~(1 << epnum);
		musb_writew(musb->mregs, MUSB_INTRRXE, musb->intrrxe);
		musb_writew(epio, MUSB_RXMAXP, 0);
	}

	musb_ep->desc = NULL;
	musb_ep->end_point.desc = NULL;

	/* abort all pending DMA and requests */
	nuke(musb_ep, -ESHUTDOWN);

	schedule_work(&musb->irq_work);

	spin_unlock_irqrestore(&(musb->lock), flags);

	dev_dbg(musb->controller, "%s\n", musb_ep->end_point.name);

	return status;
}

/*
 * Allocate a request for an endpoint.
 * Reused by ep0 code.
 */
struct usb_request *musb_alloc_request(struct usb_ep *ep, gfp_t gfp_flags)
{
	struct musb_ep		*musb_ep = to_musb_ep(ep);
	struct musb		*musb = musb_ep->musb;
	struct musb_request	*request = NULL;

	request = kzalloc(sizeof *request, gfp_flags);
	if (!request) {
		dev_dbg(musb->controller, "not enough memory\n");
		return NULL;
	}

	request->request.dma = DMA_ADDR_INVALID;
	request->epnum = musb_ep->current_epnum;
	request->ep = musb_ep;

	return &request->request;
}

/*
 * Free a request
 * Reused by ep0 code.
 */
void musb_free_request(struct usb_ep *ep, struct usb_request *req)
{
	kfree(to_musb_request(req));
}

static LIST_HEAD(buffers);

struct free_record {
	struct list_head	list;
	struct device		*dev;
	unsigned		bytes;
	dma_addr_t		dma;
};

/*
 * Context: controller locked, IRQs blocked.
 */
void musb_ep_restart(struct musb *musb, struct musb_request *req)
{
	dev_dbg(musb->controller, "<== %s request %p len %u on hw_ep%d\n",
		req->tx ? "TX/IN" : "RX/OUT",
		&req->request, req->request.length, req->epnum);

	musb_ep_select(musb->mregs, req->epnum);
	if (req->tx)
		txstate(musb, req);
	else
		rxstate(musb, req);
}

static int musb_gadget_queue(struct usb_ep *ep, struct usb_request *req,
			gfp_t gfp_flags)
{
	struct musb_ep		*musb_ep;
	struct musb_request	*request;
	struct musb		*musb;
	int			status = 0;
	unsigned long		lockflags;

	if (!ep || !req)
		return -EINVAL;
	if (!req->buf)
		return -ENODATA;

	musb_ep = to_musb_ep(ep);
	musb = musb_ep->musb;

	request = to_musb_request(req);
	request->musb = musb;

	if (request->ep != musb_ep)
		return -EINVAL;

	dev_dbg(musb->controller, "<== to %s request=%p\n", ep->name, req);

	/* request is mine now... */
	request->request.actual = 0;
	request->request.status = -EINPROGRESS;
	request->epnum = musb_ep->current_epnum;
	request->tx = musb_ep->is_in;

	map_dma_buffer(request, musb, musb_ep);

	spin_lock_irqsave(&musb->lock, lockflags);

	/* don't queue if the ep is down */
	if (!musb_ep->desc) {
		dev_dbg(musb->controller, "req %p queued to %s while ep %s\n",
				req, ep->name, "disabled");
		status = -ESHUTDOWN;
		unmap_dma_buffer(request, musb);
		goto unlock;
	}

	/* add request to the list */
	list_add_tail(&request->list, &musb_ep->req_list);

	/* it this is the head of the queue, start i/o ... */
	if (!musb_ep->busy && &request->list == musb_ep->req_list.next)
		musb_ep_restart(musb, request);

unlock:
	spin_unlock_irqrestore(&musb->lock, lockflags);
	return status;
}

static int musb_gadget_dequeue(struct usb_ep *ep, struct usb_request *request)
{
	struct musb_ep		*musb_ep = to_musb_ep(ep);
	struct musb_request	*req = to_musb_request(request);
	struct musb_request	*r;
	unsigned long		flags;
	int			status = 0;
	struct musb		*musb = musb_ep->musb;

	if (!ep || !request || to_musb_request(request)->ep != musb_ep)
		return -EINVAL;

	spin_lock_irqsave(&musb->lock, flags);

	list_for_each_entry(r, &musb_ep->req_list, list) {
		if (r == req)
			break;
	}
	if (r != req) {
		dev_dbg(musb->controller, "request %p not queued to %s\n", request, ep->name);
		status = -EINVAL;
		goto done;
	}

	/* if the hardware doesn't have the request, easy ... */
	if (musb_ep->req_list.next != &req->list || musb_ep->busy)
		musb_g_giveback(musb_ep, request, -ECONNRESET);

	/* ... else abort the dma transfer ... */
	else if (is_dma_capable() && musb_ep->dma) {
		struct dma_controller	*c = musb->dma_controller;

		musb_ep_select(musb->mregs, musb_ep->current_epnum);
		if (c->channel_abort)
			status = c->channel_abort(musb_ep->dma);
		else
			status = -EBUSY;
		if (status == 0)
			musb_g_giveback(musb_ep, request, -ECONNRESET);
	} else {
		/* NOTE: by sticking to easily tested hardware/driver states,
		 * we leave counting of in-flight packets imprecise.
		 */
		musb_g_giveback(musb_ep, request, -ECONNRESET);
	}

done:
	spin_unlock_irqrestore(&musb->lock, flags);
	return status;
}

/*
 * Set or clear the halt bit of an endpoint. A halted enpoint won't tx/rx any
 * data but will queue requests.
 *
 * exported to ep0 code
 */
static int musb_gadget_set_halt(struct usb_ep *ep, int value)
{
	struct musb_ep		*musb_ep = to_musb_ep(ep);
	u8			epnum = musb_ep->current_epnum;
	struct musb		*musb = musb_ep->musb;
	void __iomem		*epio = musb->endpoints[epnum].regs;
	void __iomem		*mbase;
	unsigned long		flags;
	u16			csr;
	struct musb_request	*request;
	int			status = 0;

	if (!ep)
		return -EINVAL;
	mbase = musb->mregs;

	spin_lock_irqsave(&musb->lock, flags);

	if ((USB_ENDPOINT_XFER_ISOC == musb_ep->type)) {
		status = -EINVAL;
		goto done;
	}

	musb_ep_select(mbase, epnum);

	request = next_request(musb_ep);
	if (value) {
		if (request) {
			dev_dbg(musb->controller, "request in progress, cannot halt %s\n",
			    ep->name);
			status = -EAGAIN;
			goto done;
		}
		/* Cannot portably stall with non-empty FIFO */
		if (musb_ep->is_in) {
			csr = musb_readw(epio, MUSB_TXCSR);
			if (csr & MUSB_TXCSR_FIFONOTEMPTY) {
				dev_dbg(musb->controller, "FIFO busy, cannot halt %s\n", ep->name);
				status = -EAGAIN;
				goto done;
			}
		}
	} else
		musb_ep->wedged = 0;

	/* set/clear the stall and toggle bits */
	dev_dbg(musb->controller, "%s: %s stall\n", ep->name, value ? "set" : "clear");
	if (musb_ep->is_in) {
		csr = musb_readw(epio, MUSB_TXCSR);
		csr |= MUSB_TXCSR_P_WZC_BITS
			| MUSB_TXCSR_CLRDATATOG;
		if (value)
			csr |= MUSB_TXCSR_P_SENDSTALL;
		else
			csr &= ~(MUSB_TXCSR_P_SENDSTALL
				| MUSB_TXCSR_P_SENTSTALL);
		csr &= ~MUSB_TXCSR_TXPKTRDY;
		musb_writew(epio, MUSB_TXCSR, csr);
	} else {
		csr = musb_readw(epio, MUSB_RXCSR);
		csr |= MUSB_RXCSR_P_WZC_BITS
			| MUSB_RXCSR_FLUSHFIFO
			| MUSB_RXCSR_CLRDATATOG;
		if (value)
			csr |= MUSB_RXCSR_P_SENDSTALL;
		else
			csr &= ~(MUSB_RXCSR_P_SENDSTALL
				| MUSB_RXCSR_P_SENTSTALL);
		musb_writew(epio, MUSB_RXCSR, csr);
	}

	/* maybe start the first request in the queue */
	if (!musb_ep->busy && !value && request) {
		dev_dbg(musb->controller, "restarting the request\n");
		musb_ep_restart(musb, request);
	}

done:
	spin_unlock_irqrestore(&musb->lock, flags);
	return status;
}

/*
 * Sets the halt feature with the clear requests ignored
 */
static int musb_gadget_set_wedge(struct usb_ep *ep)
{
	struct musb_ep		*musb_ep = to_musb_ep(ep);

	if (!ep)
		return -EINVAL;

	musb_ep->wedged = 1;

	return usb_ep_set_halt(ep);
}

static int musb_gadget_fifo_status(struct usb_ep *ep)
{
	struct musb_ep		*musb_ep = to_musb_ep(ep);
	void __iomem		*epio = musb_ep->hw_ep->regs;
	int			retval = -EINVAL;

	if (musb_ep->desc && !musb_ep->is_in) {
		struct musb		*musb = musb_ep->musb;
		int			epnum = musb_ep->current_epnum;
		void __iomem		*mbase = musb->mregs;
		unsigned long		flags;

		spin_lock_irqsave(&musb->lock, flags);

		musb_ep_select(mbase, epnum);
		/* FIXME return zero unless RXPKTRDY is set */
		retval = musb_readw(epio, MUSB_RXCOUNT);

		spin_unlock_irqrestore(&musb->lock, flags);
	}
	return retval;
}

static void musb_gadget_fifo_flush(struct usb_ep *ep)
{
	struct musb_ep	*musb_ep = to_musb_ep(ep);
	struct musb	*musb = musb_ep->musb;
	u8		epnum = musb_ep->current_epnum;
	void __iomem	*epio = musb->endpoints[epnum].regs;
	void __iomem	*mbase;
	unsigned long	flags;
	u16		csr;

	mbase = musb->mregs;

	spin_lock_irqsave(&musb->lock, flags);
	musb_ep_select(mbase, (u8) epnum);

	/* disable interrupts */
	musb_writew(mbase, MUSB_INTRTXE, musb->intrtxe & ~(1 << epnum));

	if (musb_ep->is_in) {
		csr = musb_readw(epio, MUSB_TXCSR);
		if (csr & MUSB_TXCSR_FIFONOTEMPTY) {
			csr |= MUSB_TXCSR_FLUSHFIFO | MUSB_TXCSR_P_WZC_BITS;
			/*
			 * Setting both TXPKTRDY and FLUSHFIFO makes controller
			 * to interrupt current FIFO loading, but not flushing
			 * the already loaded ones.
			 */
			csr &= ~MUSB_TXCSR_TXPKTRDY;
			musb_writew(epio, MUSB_TXCSR, csr);
			/* REVISIT may be inappropriate w/o FIFONOTEMPTY ... */
			musb_writew(epio, MUSB_TXCSR, csr);
		}
	} else {
		csr = musb_readw(epio, MUSB_RXCSR);
		csr |= MUSB_RXCSR_FLUSHFIFO | MUSB_RXCSR_P_WZC_BITS;
		musb_writew(epio, MUSB_RXCSR, csr);
		musb_writew(epio, MUSB_RXCSR, csr);
	}

	/* re-enable interrupt */
	musb_writew(mbase, MUSB_INTRTXE, musb->intrtxe);
	spin_unlock_irqrestore(&musb->lock, flags);
}

static const struct usb_ep_ops musb_ep_ops = {
	.enable		= musb_gadget_enable,
	.disable	= musb_gadget_disable,
	.alloc_request	= musb_alloc_request,
	.free_request	= musb_free_request,
	.queue		= musb_gadget_queue,
	.dequeue	= musb_gadget_dequeue,
	.set_halt	= musb_gadget_set_halt,
	.set_wedge	= musb_gadget_set_wedge,
	.fifo_status	= musb_gadget_fifo_status,
	.fifo_flush	= musb_gadget_fifo_flush
};

/* ----------------------------------------------------------------------- */

static int musb_gadget_get_frame(struct usb_gadget *gadget)
{
	struct musb	*musb = gadget_to_musb(gadget);

	return (int)musb_readw(musb->mregs, MUSB_FRAME);
}

static int musb_gadget_wakeup(struct usb_gadget *gadget)
{
	struct musb	*musb = gadget_to_musb(gadget);
	void __iomem	*mregs = musb->mregs;
	unsigned long	flags;
	int		status = -EINVAL;
	u8		power, devctl;
	int		retries;

	spin_lock_irqsave(&musb->lock, flags);

	switch (musb->xceiv->state) {
	case OTG_STATE_B_PERIPHERAL:
		/* NOTE:  OTG state machine doesn't include B_SUSPENDED;
		 * that's part of the standard usb 1.1 state machine, and
		 * doesn't affect OTG transitions.
		 */
		if (musb->may_wakeup && musb->is_suspended)
			break;
		goto done;
	case OTG_STATE_B_IDLE:
		/* Start SRP ... OTG not required. */
		devctl = musb_readb(mregs, MUSB_DEVCTL);
		dev_dbg(musb->controller, "Sending SRP: devctl: %02x\n", devctl);
		devctl |= MUSB_DEVCTL_SESSION;
		musb_writeb(mregs, MUSB_DEVCTL, devctl);
		devctl = musb_readb(mregs, MUSB_DEVCTL);
		retries = 100;
		while (!(devctl & MUSB_DEVCTL_SESSION)) {
			devctl = musb_readb(mregs, MUSB_DEVCTL);
			if (retries-- < 1)
				break;
		}
		retries = 10000;
		while (devctl & MUSB_DEVCTL_SESSION) {
			devctl = musb_readb(mregs, MUSB_DEVCTL);
			if (retries-- < 1)
				break;
		}

		spin_unlock_irqrestore(&musb->lock, flags);
		otg_start_srp(musb->xceiv->otg);
		spin_lock_irqsave(&musb->lock, flags);

		/* Block idling for at least 1s */
		musb_platform_try_idle(musb,
			jiffies + msecs_to_jiffies(1 * HZ));

		status = 0;
		goto done;
	default:
		dev_dbg(musb->controller, "Unhandled wake: %s\n",
			usb_otg_state_string(musb->xceiv->state));
		goto done;
	}

	status = 0;

	power = musb_readb(mregs, MUSB_POWER);
	power |= MUSB_POWER_RESUME;
	musb_writeb(mregs, MUSB_POWER, power);
	dev_dbg(musb->controller, "issue wakeup\n");

	/* FIXME do this next chunk in a timer callback, no udelay */
	mdelay(2);

	power = musb_readb(mregs, MUSB_POWER);
	power &= ~MUSB_POWER_RESUME;
	musb_writeb(mregs, MUSB_POWER, power);
done:
	spin_unlock_irqrestore(&musb->lock, flags);
	return status;
}

static int
musb_gadget_set_self_powered(struct usb_gadget *gadget, int is_selfpowered)
{
	struct musb	*musb = gadget_to_musb(gadget);

	musb->is_self_powered = !!is_selfpowered;
	return 0;
}

static void musb_pullup(struct musb *musb, int is_on)
{
	u8 power;

	power = musb_readb(musb->mregs, MUSB_POWER);
	if (is_on)
		power |= MUSB_POWER_SOFTCONN;
	else
		power &= ~MUSB_POWER_SOFTCONN;

	/* FIXME if on, HdrcStart; if off, HdrcStop */

	dev_dbg(musb->controller, "gadget D+ pullup %s\n",
		is_on ? "on" : "off");
	musb_writeb(musb->mregs, MUSB_POWER, power);
}

#if 0
static int musb_gadget_vbus_session(struct usb_gadget *gadget, int is_active)
{
	dev_dbg(musb->controller, "<= %s =>\n", __func__);

	/*
	 * FIXME iff driver's softconnect flag is set (as it is during probe,
	 * though that can clear it), just musb_pullup().
	 */

	return -EINVAL;
}
#endif

static int musb_gadget_vbus_draw(struct usb_gadget *gadget, unsigned mA)
{
	struct musb	*musb = gadget_to_musb(gadget);

	if (!musb->xceiv->set_power)
		return -EOPNOTSUPP;
	return usb_phy_set_power(musb->xceiv, mA);
}

static int musb_gadget_pullup(struct usb_gadget *gadget, int is_on)
{
	struct musb	*musb = gadget_to_musb(gadget);
	unsigned long	flags;

	is_on = !!is_on;

	pm_runtime_get_sync(musb->controller);

	/* NOTE: this assumes we are sensing vbus; we'd rather
	 * not pullup unless the B-session is active.
	 */
	spin_lock_irqsave(&musb->lock, flags);
	if (is_on != musb->softconnect) {
		musb->softconnect = is_on;
		musb_pullup(musb, is_on);
	}
	spin_unlock_irqrestore(&musb->lock, flags);

	pm_runtime_put(musb->controller);

	return 0;
}

static int musb_gadget_start(struct usb_gadget *g,
		struct usb_gadget_driver *driver);
static int musb_gadget_stop(struct usb_gadget *g,
		struct usb_gadget_driver *driver);

static const struct usb_gadget_ops musb_gadget_operations = {
	.get_frame		= musb_gadget_get_frame,
	.wakeup			= musb_gadget_wakeup,
	.set_selfpowered	= musb_gadget_set_self_powered,
	/* .vbus_session		= musb_gadget_vbus_session, */
	.vbus_draw		= musb_gadget_vbus_draw,
	.pullup			= musb_gadget_pullup,
	.udc_start		= musb_gadget_start,
	.udc_stop		= musb_gadget_stop,
};

/* ----------------------------------------------------------------------- */

/* Registration */

/* Only this registration code "knows" the rule (from USB standards)
 * about there being only one external upstream port.  It assumes
 * all peripheral ports are external...
 */

static void
init_peripheral_ep(struct musb *musb, struct musb_ep *ep, u8 epnum, int is_in)
{
	struct musb_hw_ep	*hw_ep = musb->endpoints + epnum;

	memset(ep, 0, sizeof *ep);

	ep->current_epnum = epnum;
	ep->musb = musb;
	ep->hw_ep = hw_ep;
	ep->is_in = is_in;

	INIT_LIST_HEAD(&ep->req_list);

	sprintf(ep->name, "ep%d%s", epnum,
			(!epnum || hw_ep->is_shared_fifo) ? "" : (
				is_in ? "in" : "out"));
	ep->end_point.name = ep->name;
	INIT_LIST_HEAD(&ep->end_point.ep_list);
	if (!epnum) {
		usb_ep_set_maxpacket_limit(&ep->end_point, 64);
		ep->end_point.ops = &musb_g_ep0_ops;
		musb->g.ep0 = &ep->end_point;
	} else {
		if (is_in)
			usb_ep_set_maxpacket_limit(&ep->end_point, hw_ep->max_packet_sz_tx);
		else
			usb_ep_set_maxpacket_limit(&ep->end_point, hw_ep->max_packet_sz_rx);
		ep->end_point.ops = &musb_ep_ops;
		list_add_tail(&ep->end_point.ep_list, &musb->g.ep_list);
	}
}

/*
 * Initialize the endpoints exposed to peripheral drivers, with backlinks
 * to the rest of the driver state.
 */
static inline void musb_g_init_endpoints(struct musb *musb)
{
	u8			epnum;
	struct musb_hw_ep	*hw_ep;
	unsigned		count = 0;

	/* initialize endpoint list just once */
	INIT_LIST_HEAD(&(musb->g.ep_list));

	for (epnum = 0, hw_ep = musb->endpoints;
			epnum < musb->nr_endpoints;
			epnum++, hw_ep++) {
		if (hw_ep->is_shared_fifo /* || !epnum */) {
			init_peripheral_ep(musb, &hw_ep->ep_in, epnum, 0);
			count++;
		} else {
			if (hw_ep->max_packet_sz_tx) {
				init_peripheral_ep(musb, &hw_ep->ep_in,
							epnum, 1);
				count++;
			}
			if (hw_ep->max_packet_sz_rx) {
				init_peripheral_ep(musb, &hw_ep->ep_out,
							epnum, 0);
				count++;
			}
		}
	}
}

/* called once during driver setup to initialize and link into
 * the driver model; memory is zeroed.
 */
int musb_gadget_setup(struct musb *musb)
{
	int status;

	/* REVISIT minor race:  if (erroneously) setting up two
	 * musb peripherals at the same time, only the bus lock
	 * is probably held.
	 */

	musb->g.ops = &musb_gadget_operations;
	musb->g.max_speed = USB_SPEED_HIGH;
	musb->g.speed = USB_SPEED_UNKNOWN;

	MUSB_DEV_MODE(musb);
	musb->xceiv->otg->default_a = 0;
	musb->xceiv->state = OTG_STATE_B_IDLE;

	/* this "gadget" abstracts/virtualizes the controller */
	musb->g.name = musb_driver_name;
#if IS_ENABLED(CONFIG_USB_MUSB_DUAL_ROLE)
	musb->g.is_otg = 1;
#elif IS_ENABLED(CONFIG_USB_MUSB_GADGET)
	musb->g.is_otg = 0;
#endif

	musb_g_init_endpoints(musb);

	musb->is_active = 0;
	musb_platform_try_idle(musb, 0);

	status = usb_add_gadget_udc(musb->controller, &musb->g);
	if (status)
		goto err;

	return 0;
err:
	musb->g.dev.parent = NULL;
	device_unregister(&musb->g.dev);
	return status;
}

void musb_gadget_cleanup(struct musb *musb)
{
	if (musb->port_mode == MUSB_PORT_MODE_HOST)
		return;
	usb_del_gadget_udc(&musb->g);
}

/*
 * Register the gadget driver. Used by gadget drivers when
 * registering themselves with the controller.
 *
 * -EINVAL something went wrong (not driver)
 * -EBUSY another gadget is already using the controller
 * -ENOMEM no memory to perform the operation
 *
 * @param driver the gadget driver
 * @return <0 if error, 0 if everything is fine
 */
static int musb_gadget_start(struct usb_gadget *g,
		struct usb_gadget_driver *driver)
{
	struct musb		*musb = gadget_to_musb(g);
	struct usb_otg		*otg = musb->xceiv->otg;
	unsigned long		flags;
	int			retval = 0;

	if (driver->max_speed < USB_SPEED_HIGH) {
		retval = -EINVAL;
		goto err;
	}

	pm_runtime_get_sync(musb->controller);

	dev_dbg(musb->controller, "registering driver %s\n", driver->function);

	musb->softconnect = 0;
	musb->gadget_driver = driver;

	spin_lock_irqsave(&musb->lock, flags);
	musb->is_active = 1;

	otg_set_peripheral(otg, &musb->g);
	musb->xceiv->state = OTG_STATE_B_IDLE;
	spin_unlock_irqrestore(&musb->lock, flags);

	musb_start(musb);

	/* REVISIT:  funcall to other code, which also
	 * handles power budgeting ... this way also
	 * ensures HdrcStart is indirectly called.
	 */
	if (musb->xceiv->last_event == USB_EVENT_ID)
		musb_platform_set_vbus(musb, 1);

	if (musb->xceiv->last_event == USB_EVENT_NONE)
		pm_runtime_put(musb->controller);

	return 0;

err:
	return retval;
}

static void stop_activity(struct musb *musb, struct usb_gadget_driver *driver)
{
	int			i;
	struct musb_hw_ep	*hw_ep;

	/* don't disconnect if it's not connected */
	if (musb->g.speed == USB_SPEED_UNKNOWN)
		driver = NULL;
	else
		musb->g.speed = USB_SPEED_UNKNOWN;

	/* deactivate the hardware */
	if (musb->softconnect) {
		musb->softconnect = 0;
		musb_pullup(musb, 0);
	}
	musb_stop(musb);

	/* killing any outstanding requests will quiesce the driver;
	 * then report disconnect
	 */
	if (driver) {
		for (i = 0, hw_ep = musb->endpoints;
				i < musb->nr_endpoints;
				i++, hw_ep++) {
			musb_ep_select(musb->mregs, i);
			if (hw_ep->is_shared_fifo /* || !epnum */) {
				nuke(&hw_ep->ep_in, -ESHUTDOWN);
			} else {
				if (hw_ep->max_packet_sz_tx)
					nuke(&hw_ep->ep_in, -ESHUTDOWN);
				if (hw_ep->max_packet_sz_rx)
					nuke(&hw_ep->ep_out, -ESHUTDOWN);
			}
		}
	}
}

/*
 * Unregister the gadget driver. Used by gadget drivers when
 * unregistering themselves from the controller.
 *
 * @param driver the gadget driver to unregister
 */
static int musb_gadget_stop(struct usb_gadget *g,
		struct usb_gadget_driver *driver)
{
	struct musb	*musb = gadget_to_musb(g);
	unsigned long	flags;

	if (musb->xceiv->last_event == USB_EVENT_NONE)
		pm_runtime_get_sync(musb->controller);

	/*
	 * REVISIT always use otg_set_peripheral() here too;
	 * this needs to shut down the OTG engine.
	 */

	spin_lock_irqsave(&musb->lock, flags);

	musb_hnp_stop(musb);

	(void) musb_gadget_vbus_draw(&musb->g, 0);

	musb->xceiv->state = OTG_STATE_UNDEFINED;
	stop_activity(musb, driver);
	otg_set_peripheral(musb->xceiv->otg, NULL);

	dev_dbg(musb->controller, "unregistering driver %s\n",
				  driver ? driver->function : "(removed)");

	musb->is_active = 0;
	musb->gadget_driver = NULL;
	musb_platform_try_idle(musb, 0);
	spin_unlock_irqrestore(&musb->lock, flags);

	/*
	 * FIXME we need to be able to register another
	 * gadget driver here and have everything work;
	 * that currently misbehaves.
	 */

	pm_runtime_put(musb->controller);

	return 0;
}

/* ----------------------------------------------------------------------- */

/* lifecycle operations called through plat_uds.c */

void musb_g_resume(struct musb *musb)
{
	musb->is_suspended = 0;
	switch (musb->xceiv->state) {
	case OTG_STATE_B_IDLE:
		break;
	case OTG_STATE_B_WAIT_ACON:
	case OTG_STATE_B_PERIPHERAL:
		musb->is_active = 1;
		if (musb->gadget_driver && musb->gadget_driver->resume) {
			spin_unlock(&musb->lock);
			musb->gadget_driver->resume(&musb->g);
			spin_lock(&musb->lock);
		}
		break;
	default:
		WARNING("unhandled RESUME transition (%s)\n",
				usb_otg_state_string(musb->xceiv->state));
	}
}

/* called when SOF packets stop for 3+ msec */
void musb_g_suspend(struct musb *musb)
{
	u8	devctl;

	devctl = musb_readb(musb->mregs, MUSB_DEVCTL);
	dev_dbg(musb->controller, "devctl %02x\n", devctl);

	switch (musb->xceiv->state) {
	case OTG_STATE_B_IDLE:
		if ((devctl & MUSB_DEVCTL_VBUS) == MUSB_DEVCTL_VBUS)
			musb->xceiv->state = OTG_STATE_B_PERIPHERAL;
		break;
	case OTG_STATE_B_PERIPHERAL:
		musb->is_suspended = 1;
		if (musb->gadget_driver && musb->gadget_driver->suspend) {
			spin_unlock(&musb->lock);
			musb->gadget_driver->suspend(&musb->g);
			spin_lock(&musb->lock);
		}
		break;
	default:
		/* REVISIT if B_HOST, clear DEVCTL.HOSTREQ;
		 * A_PERIPHERAL may need care too
		 */
		WARNING("unhandled SUSPEND transition (%s)\n",
				usb_otg_state_string(musb->xceiv->state));
	}
}

/* Called during SRP */
void musb_g_wakeup(struct musb *musb)
{
	musb_gadget_wakeup(&musb->g);
}

/* called when VBUS drops below session threshold, and in other cases */
void musb_g_disconnect(struct musb *musb)
{
	void __iomem	*mregs = musb->mregs;
	u8	devctl = musb_readb(mregs, MUSB_DEVCTL);

	dev_dbg(musb->controller, "devctl %02x\n", devctl);

	/* clear HR */
	musb_writeb(mregs, MUSB_DEVCTL, devctl & MUSB_DEVCTL_SESSION);

	/* don't draw vbus until new b-default session */
	(void) musb_gadget_vbus_draw(&musb->g, 0);

	musb->g.speed = USB_SPEED_UNKNOWN;
	if (musb->gadget_driver && musb->gadget_driver->disconnect) {
		spin_unlock(&musb->lock);
		musb->gadget_driver->disconnect(&musb->g);
		spin_lock(&musb->lock);
	}

	switch (musb->xceiv->state) {
	default:
		dev_dbg(musb->controller, "Unhandled disconnect %s, setting a_idle\n",
			usb_otg_state_string(musb->xceiv->state));
		musb->xceiv->state = OTG_STATE_A_IDLE;
		MUSB_HST_MODE(musb);
		break;
	case OTG_STATE_A_PERIPHERAL:
		musb->xceiv->state = OTG_STATE_A_WAIT_BCON;
		MUSB_HST_MODE(musb);
		break;
	case OTG_STATE_B_WAIT_ACON:
	case OTG_STATE_B_HOST:
	case OTG_STATE_B_PERIPHERAL:
	case OTG_STATE_B_IDLE:
		musb->xceiv->state = OTG_STATE_B_IDLE;
		break;
	case OTG_STATE_B_SRP_INIT:
		break;
	}

	musb->is_active = 0;
}

void musb_g_reset(struct musb *musb)
__releases(musb->lock)
__acquires(musb->lock)
{
	void __iomem	*mbase = musb->mregs;
	u8		devctl = musb_readb(mbase, MUSB_DEVCTL);
	u8		power;

	dev_dbg(musb->controller, "<== %s driver '%s'\n",
			(devctl & MUSB_DEVCTL_BDEVICE)
				? "B-Device" : "A-Device",
			musb->gadget_driver
				? musb->gadget_driver->driver.name
				: NULL
			);

	/* report disconnect, if we didn't already (flushing EP state) */
	if (musb->g.speed != USB_SPEED_UNKNOWN)
		musb_g_disconnect(musb);

	/* clear HR */
	else if (devctl & MUSB_DEVCTL_HR)
		musb_writeb(mbase, MUSB_DEVCTL, MUSB_DEVCTL_SESSION);


	/* what speed did we negotiate? */
	power = musb_readb(mbase, MUSB_POWER);
	musb->g.speed = (power & MUSB_POWER_HSMODE)
			? USB_SPEED_HIGH : USB_SPEED_FULL;

	/* start in USB_STATE_DEFAULT */
	musb->is_active = 1;
	musb->is_suspended = 0;
	MUSB_DEV_MODE(musb);
	musb->address = 0;
	musb->ep0_state = MUSB_EP0_STAGE_SETUP;

	musb->may_wakeup = 0;
	musb->g.b_hnp_enable = 0;
	musb->g.a_alt_hnp_support = 0;
	musb->g.a_hnp_support = 0;

	/* Normal reset, as B-Device;
	 * or else after HNP, as A-Device
	 */
	if (!musb->g.is_otg) {
		/* USB device controllers that are not OTG compatible
		 * may not have DEVCTL register in silicon.
		 * In that case, do not rely on devctl for setting
		 * peripheral mode.
		 */
		musb->xceiv->state = OTG_STATE_B_PERIPHERAL;
		musb->g.is_a_peripheral = 0;
	} else if (devctl & MUSB_DEVCTL_BDEVICE) {
		musb->xceiv->state = OTG_STATE_B_PERIPHERAL;
		musb->g.is_a_peripheral = 0;
	} else {
		musb->xceiv->state = OTG_STATE_A_PERIPHERAL;
		musb->g.is_a_peripheral = 1;
	}

	/* start with default limits on VBUS power draw */
	(void) musb_gadget_vbus_draw(&musb->g, 8);
}
