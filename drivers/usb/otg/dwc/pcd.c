/*
 * DesignWare HS OTG controller driver
 * Copyright (C) 2006 Synopsys, Inc.
 * Portions Copyright (C) 2010 Applied Micro Circuits Corporation.
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License version 2 for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see http://www.gnu.org/licenses
 * or write to the Free Software Foundation, Inc., 51 Franklin Street,
 * Suite 500, Boston, MA 02110-1335 USA.
 *
 * Based on Synopsys driver version 2.60a
 * Modified by Mark Miesfeld <mmiesfeld@apm.com>
 * Modified by Stefan Roese <sr@denx.de>, DENX Software Engineering
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING BUT NOT LIMITED TO THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL SYNOPSYS, INC. BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES
 * (INCLUDING BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/*
 * This file implements the Peripheral Controller Driver.
 *
 * The Peripheral Controller Driver (PCD) is responsible for
 * translating requests from the Function Driver into the appropriate
 * actions on the DWC_otg controller. It isolates the Function Driver
 * from the specifics of the controller by providing an API to the
 * Function Driver.
 *
 * The Peripheral Controller Driver for Linux will implement the
 * Gadget API, so that the existing Gadget drivers can be used.
 * (Gadget Driver is the Linux terminology for a Function Driver.)
 *
 * The Linux Gadget API is defined in the header file linux/usb/gadget.h. The
 * USB EP operations API is defined in the structure usb_ep_ops and the USB
 * Controller API is defined in the structure usb_gadget_ops
 *
 * An important function of the PCD is managing interrupts generated
 * by the DWC_otg controller. The implementation of the DWC_otg device
 * mode interrupt service routines is in dwc_otg_pcd_intr.c.
 */

#include <linux/dma-mapping.h>
#include <linux/delay.h>

#include "pcd.h"

/*
 * Static PCD pointer for use in usb_gadget_register_driver and
 * usb_gadget_unregister_driver.  Initialized in dwc_otg_pcd_init.
 */
static struct dwc_pcd *s_pcd;

static inline int need_stop_srp_timer(struct core_if *core_if)
{
	return core_if->srp_timer_started ? 1 : 0;
}

/**
 * Tests if the module is set to FS or if the PHY_TYPE is FS. If so, then the
 * gadget should not report as dual-speed capable.
 */
static inline int check_is_dual_speed(struct core_if *core_if)
{
	if ((DWC_HWCFG2_HS_PHY_TYPE_RD(core_if->hwcfg2) == 2 &&
	     DWC_HWCFG2_P_2_P_RD(core_if->hwcfg2) == 1))
		return 0;
	return 1;
}

/**
 * Tests if driver is OTG capable.
 */
static inline int check_is_otg(struct core_if *core_if)
{
	if (DWC_HWCFG2_OP_MODE_RD(core_if->hwcfg2) ==
	    DWC_HWCFG2_OP_MODE_NO_SRP_CAPABLE_DEVICE ||
	    DWC_HWCFG2_OP_MODE_RD(core_if->hwcfg2) ==
	    DWC_HWCFG2_OP_MODE_NO_SRP_CAPABLE_HOST ||
	    DWC_HWCFG2_OP_MODE_RD(core_if->hwcfg2) ==
	    DWC_HWCFG2_OP_MODE_SRP_CAPABLE_DEVICE ||
	    DWC_HWCFG2_OP_MODE_RD(core_if->hwcfg2) ==
	    DWC_HWCFG2_OP_MODE_SRP_CAPABLE_HOST)
		return 0;
	return 1;
}

/**
 * This function completes a request. It calls the request call back.
 */
void request_done(struct pcd_ep *ep, struct pcd_request *req, int status)
{
	unsigned stopped = ep->stopped;

	list_del_init(&req->queue);
	if (req->req.status == -EINPROGRESS)
		req->req.status = status;
	else
		status = req->req.status;

	if (GET_CORE_IF(ep->pcd)->dma_enable) {
		if (req->mapped) {
			dma_unmap_single(ep->pcd->gadget.dev.parent,
					 req->req.dma, req->req.length,
					 ep->dwc_ep.is_in ? DMA_TO_DEVICE :
					 DMA_FROM_DEVICE);
			req->req.dma = DMA_ADDR_INVALID;
			req->mapped = 0;
		} else {
			dma_sync_single_for_cpu(ep->pcd->gadget.dev.parent,
						req->req.dma, req->req.length,
						ep->dwc_ep.
						is_in ? DMA_TO_DEVICE :
						DMA_FROM_DEVICE);
		}
	}

	/* don't modify queue heads during completion callback */
	ep->stopped = 1;
	spin_unlock(&ep->pcd->lock);
	req->req.complete(&ep->ep, &req->req);
	spin_lock(&ep->pcd->lock);

	if (ep->pcd->request_pending > 0)
		--ep->pcd->request_pending;
	ep->stopped = stopped;

	/*
	 * Added-sr: 2007-07-26
	 *
	 * Finally, when the current request is done, mark this endpoint
	 * as not active, so that new requests can be processed.
	 */
	if (dwc_has_feature(GET_CORE_IF(ep->pcd), DWC_LIMITED_XFER))
		ep->dwc_ep.active = 0;
}

/**
 * This function terminates all the requsts in the EP request queue.
 */
void request_nuke(struct pcd_ep *ep)
{
	struct pcd_request *req;

	ep->stopped = 1;

	mutex_unlock(&ep->dwc_ep.xfer_mutex);

	/* called with irqs blocked?? */
	while (!list_empty(&ep->queue)) {
		req = list_entry(ep->queue.next, struct pcd_request, queue);
		request_done(ep, req, -ESHUTDOWN);
	}
}

/*
 * The following sections briefly describe the behavior of the Gadget
 * API endpoint operations implemented in the DWC_otg driver
 * software. Detailed descriptions of the generic behavior of each of
 * these functions can be found in the Linux header file
 * include/linux/usb_gadget.h.
 *
 * The Gadget API provides wrapper functions for each of the function
 * pointers defined in usb_ep_ops. The Gadget Driver calls the wrapper
 * function, which then calls the underlying PCD function. The
 * following sections are named according to the wrapper
 * functions. Within each section, the corresponding DWC_otg PCD
 * function name is specified.
 *
 */

/**
 * This function assigns periodic Tx FIFO to an periodic EP in shared Tx FIFO
 * mode
 */
static u32 assign_perio_tx_fifo(struct core_if *core_if)
{
	u32 mask = 1;
	u32 i;

	for (i = 0; i < DWC_HWCFG4_NUM_DEV_PERIO_IN_EP_RD(core_if->hwcfg4);
			++i) {
		if (!(mask & core_if->p_tx_msk)) {
			core_if->p_tx_msk |= mask;
			return i + 1;
		}
		mask <<= 1;
	}
	return 0;
}

/**
 * This function releases periodic Tx FIFO in shared Tx FIFO mode
 */
static void release_perio_tx_fifo(struct core_if *core_if, u32 fifo_num)
{
	core_if->p_tx_msk = (core_if->p_tx_msk & (1 << (fifo_num - 1)))
	    ^ core_if->p_tx_msk;
}

/**
 * This function assigns periodic Tx FIFO to an periodic EP in shared Tx FIFO
 * mode
 */
static u32 assign_tx_fifo(struct core_if *core_if)
{
	u32 mask = 1;
	u32 i;

	for (i = 0; i < DWC_HWCFG4_NUM_IN_EPS_RD(core_if->hwcfg4); ++i) {
		if (!(mask & core_if->tx_msk)) {
			core_if->tx_msk |= mask;
			return i + 1;
		}
		mask <<= 1;
	}
	return 0;
}

/**
 * This function releases periodic Tx FIFO in shared Tx FIFO mode
 */
static void release_tx_fifo(struct core_if *core_if, u32 fifo_num)
{
	core_if->tx_msk = (core_if->tx_msk & (1 << (fifo_num - 1)))
	    ^ core_if->tx_msk;
}

/**
 * Sets an in endpoint's tx fifo based on the hardware configuration.
 */
static void set_in_ep_tx_fifo(struct dwc_pcd *pcd, struct pcd_ep *ep,
			      const struct usb_endpoint_descriptor *desc)
{
	if (pcd->otg_dev->core_if->en_multiple_tx_fifo) {
		ep->dwc_ep.tx_fifo_num = assign_tx_fifo(pcd->otg_dev->core_if);
	} else {
		ep->dwc_ep.tx_fifo_num = 0;

		/* If ISOC EP then assign a Periodic Tx FIFO. */
		if ((desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) ==
		    USB_ENDPOINT_XFER_ISOC)
			ep->dwc_ep.tx_fifo_num =
			    assign_perio_tx_fifo(pcd->otg_dev->core_if);
	}
}

/**
 * This function activates an EP.  The Device EP control register for
 * the EP is configured as defined in the ep structure.  Note: This function is
 * not used for EP0.
 */
void dwc_otg_ep_activate(struct core_if *core_if, struct dwc_ep *ep)
{
	struct device_if *dev_if = core_if->dev_if;
	u32 depctl = 0;
	ulong addr;
	u32 daintmsk = 0;

	/* Read DEPCTLn register */
	if (ep->is_in == 1) {
		addr = dev_if->in_ep_regs[ep->num] + DWC_DIEPCTL;
		daintmsk = DWC_DAINTMSK_IN_EP_RW(daintmsk, ep->num);
	} else {
		addr = dev_if->out_ep_regs[ep->num] + DWC_DOEPCTL;
		daintmsk = DWC_DAINTMSK_OUT_EP_RW(daintmsk, ep->num);
	}

	/* If the EP is already active don't change the EP Control register */
	depctl = dwc_reg_read(addr, 0);
	if (!DWC_DEPCTL_ACT_EP_RD(depctl)) {
		depctl = DWC_DEPCTL_MPS_RW(depctl, ep->maxpacket);
		depctl = DWC_DEPCTL_EP_TYPE_RW(depctl, ep->type);
		depctl = DWC_DEPCTL_TX_FIFO_NUM_RW(depctl, ep->tx_fifo_num);
		depctl = DWC_DEPCTL_SET_DATA0_PID_RW(depctl, 1);
		depctl = DWC_DEPCTL_ACT_EP_RW(depctl, 1);
		dwc_reg_write(addr, 0, depctl);
	}

	/* Enable the Interrupt for this EP */
	dwc_reg_modify(dev_if->dev_global_regs, DWC_DAINTMSK, 0, daintmsk);

	ep->stall_clear_flag = 0;
}

/**
 * This function is called by the Gadget Driver for each EP to be
 * configured for the current configuration (SET_CONFIGURATION).
 *
 * This function initializes the dwc_otg_ep_t data structure, and then
 * calls dwc_otg_ep_activate.
 */
static int dwc_otg_pcd_ep_enable(struct usb_ep *_ep,
				 const struct usb_endpoint_descriptor *desc)
{
	struct pcd_ep *ep;
	struct dwc_pcd *pcd;
	unsigned long flags;

	ep = container_of(_ep, struct pcd_ep, ep);
	if (!_ep || !desc || ep->desc || desc->bDescriptorType !=
	    USB_DT_ENDPOINT) {
		pr_warning("%s, bad ep or descriptor\n", __func__);
		return -EINVAL;
	}

	if (ep == &ep->pcd->ep0) {
		pr_warning("%s, bad ep(0)\n", __func__);
		return -EINVAL;
	}

	/* Check FIFO size */
	if (!desc->wMaxPacketSize) {
		pr_warning("%s, bad %s maxpacket\n", __func__, _ep->name);
		return -ERANGE;
	}

	pcd = ep->pcd;
	if (!pcd->driver || pcd->gadget.speed == USB_SPEED_UNKNOWN) {
		pr_warning("%s, bogus device state\n", __func__);
		return -ESHUTDOWN;
	}

	spin_lock_irqsave(&pcd->lock, flags);
	ep->desc = desc;
	ep->ep.maxpacket = le16_to_cpu(desc->wMaxPacketSize);

	/* Activate the EP */
	ep->stopped = 0;
	ep->wedged = 0;
	ep->dwc_ep.is_in = (USB_DIR_IN & desc->bEndpointAddress) != 0;
	ep->dwc_ep.maxpacket = ep->ep.maxpacket;
	ep->dwc_ep.type = desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK;

	if (ep->dwc_ep.is_in)
		set_in_ep_tx_fifo(pcd, ep, desc);

	/* Set initial data PID. */
	if ((desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) ==
	    USB_ENDPOINT_XFER_BULK)
		ep->dwc_ep.data_pid_start = 0;

	dwc_otg_ep_activate(GET_CORE_IF(pcd), &ep->dwc_ep);
	spin_unlock_irqrestore(&pcd->lock, flags);
	return 0;
}

/**
 * This function deactivates an EP.  This is done by clearing the USB Active EP
 * bit in the Device EP control register.  Note: This function is not used for
 * EP0. EP0 cannot be deactivated.
 */
static void dwc_otg_ep_deactivate(struct core_if *core_if, struct dwc_ep *ep)
{
	u32 depctl = 0;
	ulong addr;
	u32 daintmsk = 0;

	/* Read DEPCTLn register */
	if (ep->is_in == 1) {
		addr = core_if->dev_if->in_ep_regs[ep->num] + DWC_DIEPCTL;
		daintmsk = DWC_DAINTMSK_IN_EP_RW(daintmsk, ep->num);
	} else {
		addr =
		    core_if->dev_if->out_ep_regs[ep->num] + DWC_DOEPCTL;
		daintmsk = DWC_DAINTMSK_OUT_EP_RW(daintmsk, ep->num);
	}

	depctl = DWC_DEPCTL_ACT_EP_RW(depctl, 0);
	dwc_reg_write(addr, 0, depctl);

	/* Disable the Interrupt for this EP */
	dwc_reg_modify(core_if->dev_if->dev_global_regs, DWC_DAINTMSK,
		     daintmsk, 0);
}

/**
 * This function is called when an EP is disabled due to disconnect or
 * change in configuration. Any pending requests will terminate with a
 * status of -ESHUTDOWN.
 *
 * This function modifies the dwc_otg_ep_t data structure for this EP,
 * and then calls dwc_otg_ep_deactivate.
 */
static int dwc_otg_pcd_ep_disable(struct usb_ep *_ep)
{
	struct pcd_ep *ep;
	struct core_if *core_if;
	unsigned long flags;

	ep = container_of(_ep, struct pcd_ep, ep);
	if (!_ep || !ep->desc)
		return -EINVAL;

	core_if = ep->pcd->otg_dev->core_if;

	spin_lock_irqsave(&ep->pcd->lock, flags);

	request_nuke(ep);
	dwc_otg_ep_deactivate(core_if, &ep->dwc_ep);

	ep->desc = NULL;
	ep->stopped = 1;
	if (ep->dwc_ep.is_in) {
		release_perio_tx_fifo(core_if, ep->dwc_ep.tx_fifo_num);
		release_tx_fifo(core_if, ep->dwc_ep.tx_fifo_num);
	}

	spin_unlock_irqrestore(&ep->pcd->lock, flags);

	return 0;
}

/**
 * This function allocates a request object to use with the specified
 * endpoint.
 */
static struct usb_request *dwc_otg_pcd_alloc_request(struct usb_ep *_ep,
						     gfp_t gfp_flags)
{
	struct pcd_request *req;

	if (!_ep) {
		pr_warning("%s() Invalid EP\n", __func__);
		return NULL;
	}

	req = kzalloc(sizeof(struct pcd_request), gfp_flags);
	if (!req) {
		pr_warning("%s() request allocation failed\n", __func__);
		return NULL;
	}

	req->req.dma = DMA_ADDR_INVALID;
	INIT_LIST_HEAD(&req->queue);

	return &req->req;
}

/**
 * This function frees a request object.
 */
static void dwc_otg_pcd_free_request(struct usb_ep *_ep,
				     struct usb_request *_req)
{
	struct pcd_request *req;

	if (!_ep || !_req) {
		pr_warning("%s() nvalid ep or req argument\n", __func__);
		return;
	}

	req = container_of(_req, struct pcd_request, req);
	kfree(req);
}

/*
 * In dedicated Tx FIFO mode, enable the Non-Periodic Tx FIFO empty interrupt.
 * Otherwise, enable the Tx FIFO epmty interrupt. The data will be written into
 * the fifo by the ISR.
 */
static void enable_tx_fifo_empty_intr(struct core_if *c_if, struct dwc_ep *ep)
{
	u32 intr_mask = 0;
	struct device_if *d_if = c_if->dev_if;
	ulong global_regs = c_if->core_global_regs;

	if (!c_if->en_multiple_tx_fifo) {
		intr_mask |= DWC_INTMSK_NP_TXFIFO_EMPT;
		dwc_reg_modify(global_regs, DWC_GINTSTS, intr_mask, 0);
		dwc_reg_modify(global_regs, DWC_GINTMSK, intr_mask, intr_mask);
	} else if (ep->xfer_len) {
		/* Enable the Tx FIFO Empty Interrupt for this EP */
		u32 fifoemptymsk = 1 << ep->num;
		dwc_reg_modify(d_if->dev_global_regs,
			     DWC_DTKNQR4FIFOEMPTYMSK, 0, fifoemptymsk);
	}
}

static void set_next_ep(struct device_if *dev_if, u8 num)
{
	u32 depctl = 0;

	depctl = dwc_reg_read(dev_if->in_ep_regs[0], 0) + DWC_DIEPCTL;
	depctl = DWC_DEPCTL_NXT_EP_RW(depctl, num);

	dwc_reg_write((dev_if->in_ep_regs[0]), DWC_DIEPCTL, depctl);
}

/**
 * This function does the setup for a data transfer for an EP and
 * starts the transfer.  For an IN transfer, the packets will be loaded into the
 * appropriate Tx FIFO in the ISR. For OUT transfers, the packets are unloaded
 * from the Rx FIFO in the ISR.
 *
 */
void dwc_otg_ep_start_transfer(struct core_if *c_if, struct dwc_ep *ep)
{
	u32 depctl = 0;
	u32 deptsiz = 0;
	struct device_if *d_if = c_if->dev_if;
	ulong glbl_regs = c_if->core_global_regs;

	if (ep->is_in) {
		ulong in_regs = d_if->in_ep_regs[ep->num];
		u32 gtxstatus;

		gtxstatus = dwc_reg_read(glbl_regs, DWC_GNPTXSTS);
		if (!c_if->en_multiple_tx_fifo
		    && !DWC_GNPTXSTS_NPTXQSPCAVAIL_RD(gtxstatus))
			return;

		depctl = dwc_reg_read(in_regs, DWC_DIEPCTL);
		deptsiz = dwc_reg_read(in_regs, DWC_DIEPTSIZ);

		/* Zero Length Packet? */
		if (!ep->xfer_len) {
			deptsiz = DWC_DEPTSIZ_XFER_SIZ_RW(deptsiz, 0);
			deptsiz = DWC_DEPTSIZ_PKT_CNT_RW(deptsiz, 1);
		} else {
			/*
			 * Program the transfer size and packet count as
			 * follows:
			 *
			 * xfersize = N * maxpacket + short_packet
			 * pktcnt = N + (short_packet exist ? 1 : 0)
			 */

			/*
			 * Added-sr: 2007-07-26
			 *
			 * Since the 405EZ (Ultra) only support 2047 bytes as
			 * max transfer size, we have to split up bigger
			 * transfers into multiple transfers of 1024 bytes sized
			 * messages. I happens often, that transfers of 4096
			 * bytes are required (zero-gadget,
			 * file_storage-gadget).
			 */
			if (dwc_has_feature(c_if, DWC_LIMITED_XFER)) {
				if (ep->xfer_len > MAX_XFER_LEN) {
					ep->bytes_pending = ep->xfer_len
					    - MAX_XFER_LEN;
					ep->xfer_len = MAX_XFER_LEN;
				}
			}

			deptsiz =
			    DWC_DEPTSIZ_XFER_SIZ_RW(deptsiz, ep->xfer_len);
			deptsiz =
			    DWC_DEPTSIZ_PKT_CNT_RW(deptsiz,
						   ((ep->xfer_len - 1 +
						     ep->maxpacket) /
						    ep->maxpacket));
		}
		dwc_reg_write(in_regs, DWC_DIEPTSIZ, deptsiz);

		if (c_if->dma_enable)
			dwc_reg_write(in_regs, DWC_DIEPDMA, ep->dma_addr);
		else if (ep->type != DWC_OTG_EP_TYPE_ISOC)
			enable_tx_fifo_empty_intr(c_if, ep);

		/* EP enable, IN data in FIFO */
		depctl = DWC_DEPCTL_CLR_NAK_RW(depctl, 1);
		depctl = DWC_DEPCTL_EPENA_RW(depctl, 1);
		dwc_reg_write(in_regs, DWC_DIEPCTL, depctl);

		if (c_if->dma_enable)
			set_next_ep(d_if, ep->num);
	} else {
		u32 out_regs = d_if->out_ep_regs[ep->num];

		depctl = dwc_reg_read(out_regs, DWC_DOEPCTL);
		deptsiz = dwc_reg_read(out_regs, DWC_DOEPTSIZ);

		/*
		 * Program the transfer size and packet count as follows:
		 *
		 * pktcnt = N
		 * xfersize = N * maxpacket
		 */
		if (!ep->xfer_len) {
			deptsiz =
			    DWC_DEPTSIZ_XFER_SIZ_RW(deptsiz, ep->maxpacket);
			deptsiz = DWC_DEPTSIZ_PKT_CNT_RW(deptsiz, 1);
		} else {
			deptsiz = DWC_DEPTSIZ_PKT_CNT_RW(deptsiz,
							 ((ep->xfer_len +
							   ep->maxpacket -
							   1) / ep->maxpacket));
			deptsiz =
			    DWC_DEPTSIZ_XFER_SIZ_RW(deptsiz,
						    DWC_DEPTSIZ_PKT_CNT_RD
						    (deptsiz) * ep->maxpacket);
		}
		dwc_reg_write(out_regs, DWC_DOEPTSIZ, deptsiz);

		if (c_if->dma_enable)
			dwc_reg_write(out_regs, DWC_DOEPDMA, ep->dma_addr);

		if (ep->type == DWC_OTG_EP_TYPE_ISOC) {
			if (ep->even_odd_frame)
				depctl = DWC_DEPCTL_SET_DATA1_PID_RW(depctl, 1);
			else
				depctl = DWC_DEPCTL_SET_DATA0_PID_RW(depctl, 1);
		}

		/* EP enable */
		depctl = DWC_DEPCTL_CLR_NAK_RW(depctl, 1);
		depctl = DWC_DEPCTL_EPENA_RW(depctl, 1);
		dwc_reg_write(out_regs, DWC_DOEPCTL, depctl);
	}
}

/**
 * This function does the setup for a data transfer for EP0 and starts
 * the transfer.  For an IN transfer, the packets will be loaded into
 * the appropriate Tx FIFO in the ISR. For OUT transfers, the packets are
 * unloaded from the Rx FIFO in the ISR.
 */
void dwc_otg_ep0_start_transfer(struct core_if *c_if, struct dwc_ep *ep)
{
	u32 depctl = 0;
	u32 deptsiz = 0;
	struct device_if *d_if = c_if->dev_if;
	ulong glbl_regs = c_if->core_global_regs;

	ep->total_len = ep->xfer_len;

	if (ep->is_in) {
		ulong in_regs = d_if->in_ep_regs[0];
		u32 gtxstatus;

		gtxstatus = dwc_reg_read(glbl_regs, DWC_GNPTXSTS);

		if (!c_if->en_multiple_tx_fifo
		    && !DWC_GNPTXSTS_NPTXQSPCAVAIL_RD(gtxstatus))
			return;

		depctl = dwc_reg_read(in_regs, DWC_DIEPCTL);
		deptsiz = dwc_reg_read(in_regs, DWC_DIEPTSIZ);

		/* Zero Length Packet? */
		if (!ep->xfer_len) {
			deptsiz = DWC_DEPTSIZ0_XFER_SIZ_RW(deptsiz, 0);
			deptsiz = DWC_DEPTSIZ0_PKT_CNT_RW(deptsiz, 1);
		} else {
			/*
			 * Program the transfer size and packet count  as
			 * follows:
			 *
			 *  xfersize = N * maxpacket + short_packet
			 *  pktcnt = N + (short_packet exist ? 1 : 0)
			 */
			if (ep->xfer_len > ep->maxpacket) {
				ep->xfer_len = ep->maxpacket;
				deptsiz = DWC_DEPTSIZ0_XFER_SIZ_RW(deptsiz,
								   ep->
								   maxpacket);
			} else {
				deptsiz = DWC_DEPTSIZ0_XFER_SIZ_RW(deptsiz,
								   ep->
								   xfer_len);
			}
			deptsiz = DWC_DEPTSIZ0_PKT_CNT_RW(deptsiz, 1);
		}
		dwc_reg_write(in_regs, DWC_DIEPTSIZ, deptsiz);

		if (c_if->dma_enable)
			dwc_reg_write(in_regs, DWC_DIEPDMA, ep->dma_addr);

		/* EP enable, IN data in FIFO */
		depctl = DWC_DEPCTL_CLR_NAK_RW(depctl, 1);
		depctl = DWC_DEPCTL_EPENA_RW(depctl, 1);
		dwc_reg_write(in_regs, DWC_DIEPCTL, depctl);

		if (!c_if->dma_enable)
			enable_tx_fifo_empty_intr(c_if, ep);
	} else {
		ulong out_regs = d_if->out_ep_regs[ep->num];

		depctl = dwc_reg_read(out_regs, DWC_DOEPCTL);
		deptsiz = dwc_reg_read(out_regs, DWC_DOEPTSIZ);

		/*
		 * Program the transfer size and packet count as follows:
		 *
		 * xfersize = N * (maxpacket + 4 - (maxpacket % 4))
		 * pktcnt = N
		 */
		if (!ep->xfer_len) {
			deptsiz = DWC_DEPTSIZ0_XFER_SIZ_RW(deptsiz,
							   ep->maxpacket);
			deptsiz = DWC_DEPTSIZ0_PKT_CNT_RW(deptsiz, 1);
		} else {
			deptsiz = DWC_DEPTSIZ0_PKT_CNT_RW(deptsiz,
							  (ep->xfer_len +
							   ep->maxpacket -
							   1) / ep->maxpacket);
			deptsiz =
			    DWC_DEPTSIZ0_XFER_SIZ_RW(deptsiz,
						     DWC_DEPTSIZ_PKT_CNT_RD
						     (deptsiz) * ep->maxpacket);
		}
		dwc_reg_write(out_regs, DWC_DOEPTSIZ, deptsiz);

		if (c_if->dma_enable)
			dwc_reg_write(out_regs, DWC_DOEPDMA, ep->dma_addr);

		/* EP enable */
		depctl = DWC_DEPCTL_CLR_NAK_RW(depctl, 1);
		depctl = DWC_DEPCTL_EPENA_RW(depctl, 1);
		dwc_reg_write(out_regs, DWC_DOEPCTL, depctl);
	}
}

/**
 * This function is used to submit an I/O Request to an EP.
 *
 *	- When the request completes the request's completion callback
 *	  is called to return the request to the driver.
 *	- An EP, except control EPs, may have multiple requests
 *	  pending.
 *	- Once submitted the request cannot be examined or modified.
 *	- Each request is turned into one or more packets.
 *	- A BULK EP can queue any amount of data; the transfer is
 *	  packetized.
 *	- Zero length Packets are specified with the request 'zero'
 *	  flag.
 */
static int dwc_otg_pcd_ep_queue(struct usb_ep *_ep, struct usb_request *_req,
				gfp_t gfp_flags)
{
	int prevented = 0;
	struct pcd_request *req;
	struct pcd_ep *ep;
	struct dwc_pcd *pcd;
	struct core_if *core_if;
	unsigned long flags = 0;

	req = container_of(_req, struct pcd_request, req);
	if (!_req || !_req->complete || !_req->buf ||
			!list_empty(&req->queue)) {
		pr_warning("%s, bad params\n", __func__);
		return -EINVAL;
	}

	ep = container_of(_ep, struct pcd_ep, ep);
	if (!_ep || (!ep->desc && ep->dwc_ep.num != 0)) {
		pr_warning("%s, bad ep\n", __func__);
		return -EINVAL;
	}

	pcd = ep->pcd;
	if (!pcd->driver || pcd->gadget.speed == USB_SPEED_UNKNOWN) {
		pr_warning("%s, bogus device state\n", __func__);
		return -ESHUTDOWN;
	}
	core_if = pcd->otg_dev->core_if;

	if (GET_CORE_IF(pcd)->dma_enable) {
		if (_req->dma == DMA_ADDR_INVALID) {
			_req->dma = dma_map_single(pcd->gadget.dev.parent,
						   _req->buf, _req->length,
						   ep->dwc_ep.
						   is_in ? DMA_TO_DEVICE :
						   DMA_FROM_DEVICE);
			req->mapped = 1;
		} else {
			dma_sync_single_for_device(pcd->gadget.dev.parent,
						   _req->dma, _req->length,
						   ep->dwc_ep.
						   is_in ? DMA_TO_DEVICE :
						   DMA_FROM_DEVICE);
			req->mapped = 0;
		}
	}

	spin_lock_irqsave(&ep->pcd->lock, flags);

	_req->status = -EINPROGRESS;
	_req->actual = 0;

	/* Start the transfer */
	if (list_empty(&ep->queue) && !ep->stopped) {
		/* EP0 Transfer? */
		if (ep->dwc_ep.num == 0) {
			switch (pcd->ep0state) {
			case EP0_IN_DATA_PHASE:
				break;
			case EP0_OUT_DATA_PHASE:
				if (pcd->request_config) {
					/* Complete STATUS PHASE */
					ep->dwc_ep.is_in = 1;
					pcd->ep0state = EP0_STATUS;
				}
				break;
			case EP0_STATUS:
				break;
			default:
				spin_unlock_irqrestore(&pcd->lock, flags);
				return -EL2HLT;
			}

			ep->dwc_ep.dma_addr = _req->dma;
			ep->dwc_ep.start_xfer_buff = _req->buf;
			ep->dwc_ep.xfer_buff = _req->buf;
			ep->dwc_ep.xfer_len = _req->length;
			ep->dwc_ep.xfer_count = 0;
			ep->dwc_ep.sent_zlp = 0;
			ep->dwc_ep.total_len = ep->dwc_ep.xfer_len;

			dwc_otg_ep0_start_transfer(core_if, &ep->dwc_ep);
		} else {
			/* Setup and start the Transfer */
			if(mutex_trylock(&ep->dwc_ep.xfer_mutex)) {
				ep->dwc_ep.dma_addr = _req->dma;
				ep->dwc_ep.start_xfer_buff = _req->buf;
				ep->dwc_ep.xfer_buff = _req->buf;
				ep->dwc_ep.xfer_len = _req->length;
				ep->dwc_ep.xfer_count = 0;
				ep->dwc_ep.sent_zlp = 0;
				ep->dwc_ep.total_len = ep->dwc_ep.xfer_len;

				dwc_otg_ep_start_transfer(core_if, &ep->dwc_ep);
			}
		}
	}

	if (req || prevented) {
		++pcd->request_pending;
		list_add_tail(&req->queue, &ep->queue);

		if (ep->dwc_ep.is_in && ep->stopped && !core_if->dma_enable) {
			/*
			 *  Device IN endpoint interrupt mask register is laid
			 *  out exactly the same as the device IN endpoint
			 *  interrupt register.
			 */
			u32 diepmsk = 0;
			diepmsk = DWC_DIEPMSK_IN_TKN_TX_EMPTY_RW(diepmsk, 1);

			dwc_reg_modify(core_if->dev_if->dev_global_regs,
				     DWC_DIEPMSK, 0, diepmsk);
		}
	}

	spin_unlock_irqrestore(&pcd->lock, flags);
	return 0;
}

/**
 * This function cancels an I/O request from an EP.
 */
static int dwc_otg_pcd_ep_dequeue(struct usb_ep *_ep, struct usb_request *_req)
{
	struct pcd_request *req;
	struct pcd_ep *ep;
	struct dwc_pcd *pcd;
	unsigned long flags;

	ep = container_of(_ep, struct pcd_ep, ep);
	if (!_ep || !_req || (!ep->desc && ep->dwc_ep.num != 0)) {
		pr_warning("%s, bad argument\n", __func__);
		return -EINVAL;
	}

	pcd = ep->pcd;
	if (!pcd->driver || pcd->gadget.speed == USB_SPEED_UNKNOWN) {
		pr_warning("%s, bogus device state\n", __func__);
		return -ESHUTDOWN;
	}

	spin_lock_irqsave(&pcd->lock, flags);

	/* make sure it's actually queued on this endpoint */
	list_for_each_entry(req, &ep->queue, queue)
	    if (&req->req == _req)
		break;

	if (&req->req != _req) {
		spin_unlock_irqrestore(&pcd->lock, flags);
		return -EINVAL;
	}

	if (!list_empty(&req->queue))
		request_done(ep, req, -ECONNRESET);
	else
		req = NULL;

	spin_unlock_irqrestore(&pcd->lock, flags);

	return req ? 0 : -EOPNOTSUPP;
}

/**
 * Set the EP STALL.
 */
void dwc_otg_ep_set_stall(struct core_if *core_if, struct dwc_ep *ep)
{
	u32 depctl = 0;
	ulong depctl_addr;

	if (ep->is_in) {
		depctl_addr =
		    (core_if->dev_if->in_ep_regs[ep->num]) + DWC_DIEPCTL;
		depctl = dwc_reg_read(depctl_addr, 0);

		/* set the disable and stall bits */
		if (DWC_DEPCTL_EPENA_RD(depctl))
			depctl = DWC_DEPCTL_EPDIS_RW(depctl, 1);
		depctl = DWC_DEPCTL_STALL_HNDSHK_RW(depctl, 1);
		dwc_reg_write(depctl_addr, 0, depctl);
	} else {
		depctl_addr =
		    (core_if->dev_if->out_ep_regs[ep->num] + DWC_DOEPCTL);
		depctl = dwc_reg_read(depctl_addr, 0);

		/* set the stall bit */
		depctl = DWC_DEPCTL_STALL_HNDSHK_RW(depctl, 1);
		dwc_reg_write(depctl_addr, 0, depctl);
	}
}

/**
 * Clear the EP STALL.
 */
void dwc_otg_ep_clear_stall(struct core_if *core_if, struct dwc_ep *ep)
{
	u32 depctl = 0;
	ulong depctl_addr;

	if (ep->is_in == 1)
		depctl_addr =
		    (core_if->dev_if->in_ep_regs[ep->num]) + DWC_DIEPCTL;
	else
		depctl_addr =
		    (core_if->dev_if->out_ep_regs[ep->num]) + DWC_DOEPCTL;

	depctl = dwc_reg_read(depctl_addr, 0);

	/* clear the stall bits */
	depctl = DWC_DEPCTL_STALL_HNDSHK_RW(depctl, 0);

	/*
	 * USB Spec 9.4.5: For endpoints using data toggle, regardless
	 * of whether an endpoint has the Halt feature set, a
	 * ClearFeature(ENDPOINT_HALT) request always results in the
	 * data toggle being reinitialized to DATA0.
	 */
	if (ep->type == DWC_OTG_EP_TYPE_INTR ||
	    ep->type == DWC_OTG_EP_TYPE_BULK)
		depctl = DWC_DEPCTL_SET_DATA0_PID_RW(depctl, 1);

	dwc_reg_write(depctl_addr, 0, depctl);
}

/**
 * usb_ep_set_halt stalls an endpoint.
 *
 * usb_ep_clear_halt clears an endpoint halt and resets its data
 * toggle.
 *
 * Both of these functions are implemented with the same underlying
 * function. The behavior depends on the val argument:
 *	- 0 means clear_halt.
 *	- 1 means set_halt,
 *	- 2 means clear stall lock flag.
 *	- 3 means set  stall lock flag.
 */
static int dwc_otg_pcd_ep_set_halt_wedge(struct usb_ep *_ep, int val, int wedged)
{
	int retval = 0;
	unsigned long flags;
	struct pcd_ep *ep;

	ep = container_of(_ep, struct pcd_ep, ep);
	if (!_ep || (!ep->desc && ep != &ep->pcd->ep0) ||
	    (ep->desc && ep->desc->bmAttributes == USB_ENDPOINT_XFER_ISOC)) {
		pr_warning("%s, bad ep\n", __func__);
		return -EINVAL;
	}

	spin_lock_irqsave(&ep->pcd->lock, flags);

	if (ep->dwc_ep.is_in && !list_empty(&ep->queue)) {
		pr_debug("%s() %s XFer In process\n", __func__, _ep->name);
		retval = -EAGAIN;
	} else if (val == 0) {
		ep->wedged = 0;
		dwc_otg_ep_clear_stall(ep->pcd->otg_dev->core_if, &ep->dwc_ep);
	} else if (val == 1) {
		if (ep->dwc_ep.num == 0)
			ep->pcd->ep0state = EP0_STALL;
		if (wedged)
			ep->wedged = 1;

		ep->stopped = 1;
		dwc_otg_ep_set_stall(ep->pcd->otg_dev->core_if, &ep->dwc_ep);
	} else if (val == 2) {
		ep->dwc_ep.stall_clear_flag = 0;
	} else if (val == 3) {
		ep->dwc_ep.stall_clear_flag = 1;
	}

	spin_unlock_irqrestore(&ep->pcd->lock, flags);
	return retval;
}
static int dwc_otg_pcd_ep_set_halt(struct usb_ep *_ep, int val)
{
	return dwc_otg_pcd_ep_set_halt_wedge(_ep, val, 0);
}
static int dwc_otg_pcd_ep_set_wedge(struct usb_ep *_ep)
{
	return dwc_otg_pcd_ep_set_halt_wedge(_ep, 1, 1);
}

static struct usb_ep_ops dwc_otg_pcd_ep_ops = {
	.enable = dwc_otg_pcd_ep_enable,
	.disable = dwc_otg_pcd_ep_disable,
	.alloc_request = dwc_otg_pcd_alloc_request,
	.free_request = dwc_otg_pcd_free_request,
	.queue = dwc_otg_pcd_ep_queue,
	.dequeue = dwc_otg_pcd_ep_dequeue,
	.set_halt = dwc_otg_pcd_ep_set_halt,
	.set_wedge = dwc_otg_pcd_ep_set_wedge,
	.fifo_status = NULL,
	.fifo_flush = NULL,
};

/**
 * Gets the current USB frame number from the DTS register. This is the frame
 * number from the last SOF packet.
 */
static u32 dwc_otg_get_frame_number(struct core_if *core_if)
{
	u32 dsts;

	dsts = dwc_reg_read(core_if->dev_if->dev_global_regs, DWC_DSTS);
	return DWC_DSTS_SOFFN_RD(dsts);
}

/**
 * The following gadget operations will be implemented in the DWC_otg
 * PCD. Functions in the API that are not described below are not
 * implemented.
 *
 * The Gadget API provides wrapper functions for each of the function
 * pointers defined in usb_gadget_ops. The Gadget Driver calls the
 * wrapper function, which then calls the underlying PCD function. The
 * following sections are named according to the wrapper functions
 * (except for ioctl, which doesn't have a wrapper function). Within
 * each section, the corresponding DWC_otg PCD function name is
 * specified.
 *
 */

/**
 *Gets the USB Frame number of the last SOF.
 */
static int dwc_otg_pcd_get_frame(struct usb_gadget *_gadget)
{
	if (!_gadget) {
		return -ENODEV;
	} else {
		struct dwc_pcd *pcd;

		pcd = container_of(_gadget, struct dwc_pcd, gadget);
		dwc_otg_get_frame_number(GET_CORE_IF(pcd));
	}

	return 0;
}

/**
 * This function is called when the SRP timer expires.  The SRP should complete
 * within 6 seconds.
 */
static void srp_timeout(unsigned long data)
{
	u32 gotgctl;
	struct dwc_pcd *pcd = (struct dwc_pcd *)data;
	struct core_if *core_if = pcd->otg_dev->core_if;
	ulong addr = otg_ctl_reg(pcd);

	gotgctl = dwc_reg_read(addr, 0);
	core_if->srp_timer_started = 0;

	if (gotgctl & DWC_GCTL_SES_REQ) {
		pr_info("SRP Timeout\n");
		pr_err("Device not connected/responding\n");

		gotgctl &= ~DWC_GCTL_SES_REQ;
		dwc_reg_write(addr, 0, gotgctl);
	} else {
		pr_info(" SRP GOTGCTL=%0x\n", gotgctl);
	}
}

/**
 * Start the SRP timer to detect when the SRP does not complete within
 * 6 seconds.
 */
static void dwc_otg_pcd_start_srp_timer(struct dwc_pcd *pcd)
{
	struct timer_list *srp_timer = &pcd->srp_timer;

	GET_CORE_IF(pcd)->srp_timer_started = 1;
	init_timer(srp_timer);
	srp_timer->function = srp_timeout;
	srp_timer->data = (unsigned long)pcd;
	srp_timer->expires = jiffies + (HZ * 6);

	add_timer(srp_timer);
}

static void dwc_otg_pcd_initiate_srp(struct dwc_pcd *pcd)
{
	u32 mem;
	u32 val;
	ulong addr = otg_ctl_reg(pcd);

	val = dwc_reg_read(addr, 0);
	if (val & DWC_GCTL_SES_REQ) {
		pr_err("Session Request Already active!\n");
		return;
	}

	pr_notice("Session Request Initated\n");
	mem = dwc_reg_read(addr, 0);
	mem |= DWC_GCTL_SES_REQ;
	dwc_reg_write(addr, 0, mem);

	/* Start the SRP timer */
	dwc_otg_pcd_start_srp_timer(pcd);
	return;
}

static void dwc_otg_pcd_remote_wakeup(struct dwc_pcd *pcd, int set)
{
	u32 dctl = 0;
	ulong addr = dev_ctl_reg(pcd);

	if (dwc_otg_is_device_mode(GET_CORE_IF(pcd))) {
		if (pcd->remote_wakeup_enable) {
			if (set) {
				dctl = DEC_DCTL_REMOTE_WAKEUP_SIG(dctl, 1);
				dwc_reg_modify(addr, 0, 0, dctl);
				msleep(20);
				dwc_reg_modify(addr, 0, dctl, 0);
			}
		}
	}
}

/**
 * Initiates Session Request Protocol (SRP) to wakeup the host if no
 * session is in progress. If a session is already in progress, but
 * the device is suspended, remote wakeup signaling is started.
 *
 */
static int dwc_otg_pcd_wakeup(struct usb_gadget *_gadget)
{
	unsigned long flags;
	struct dwc_pcd *pcd;
	u32 dsts;
	u32 gotgctl;

	if (!_gadget)
		return -ENODEV;
	else
		pcd = container_of(_gadget, struct dwc_pcd, gadget);

	spin_lock_irqsave(&pcd->lock, flags);

	/*
	 * This function starts the Protocol if no session is in progress. If
	 * a session is already in progress, but the device is suspended,
	 * remote wakeup signaling is started.
	 */

	/* Check if valid session */
	gotgctl = dwc_reg_read(otg_ctl_reg(pcd), 0);
	if (gotgctl & DWC_GCTL_BSESSION_VALID) {
		/* Check if suspend state */
		dsts = dwc_reg_read(dev_sts_reg(pcd), 0);
		if (DWC_DSTS_SUSP_STS_RD(dsts))
			dwc_otg_pcd_remote_wakeup(pcd, 1);
	} else {
		dwc_otg_pcd_initiate_srp(pcd);
	}

	spin_unlock_irqrestore(&pcd->lock, flags);
	return 0;
}

static int dwc_gadget_vbus_draw(struct usb_gadget *gadget, unsigned mA)
{
	struct dwc_pcd *pcd = container_of(gadget, struct dwc_pcd, gadget);

	if (pcd->otg_dev->core_if->xceiv)
		return -EOPNOTSUPP;

	return usb_phy_set_power(pcd->otg_dev->core_if->xceiv, mA);

}
static int dwc_gadget_start(struct usb_gadget *gadget,
				struct usb_gadget_driver *driver);
static int dwc_gadget_stop(struct usb_gadget *gadget,
				struct usb_gadget_driver *driver);

static const struct usb_gadget_ops dwc_otg_pcd_ops = {
	.get_frame = dwc_otg_pcd_get_frame,
	.wakeup = dwc_otg_pcd_wakeup,
	.udc_start	= dwc_gadget_start,
	.udc_stop	= dwc_gadget_stop,
	.vbus_draw = dwc_gadget_vbus_draw,
};

/**
 * This function updates the otg values in the gadget structure.
 */
void dwc_otg_pcd_update_otg(struct dwc_pcd *pcd, const unsigned reset)
{
	if (!pcd->gadget.is_otg)
		return;

	if (reset) {
		pcd->b_hnp_enable = 0;
		pcd->a_hnp_support = 0;
		pcd->a_alt_hnp_support = 0;
	}

	pcd->gadget.b_hnp_enable = pcd->b_hnp_enable;
	pcd->gadget.a_hnp_support = pcd->a_hnp_support;
	pcd->gadget.a_alt_hnp_support = pcd->a_alt_hnp_support;
}

/**
 * This function is the top level PCD interrupt handler.
 */
static irqreturn_t dwc_otg_pcd_irq(int _irq, void *dev)
{
	struct dwc_pcd *pcd = dev;
	int retval;

	retval = dwc_otg_pcd_handle_intr(pcd);
	return IRQ_RETVAL(retval);
}

/**
 * PCD Callback function for initializing the PCD when switching to
 * device mode.
 */
static int dwc_otg_pcd_start_cb(void *_p)
{
	struct dwc_pcd *pcd = (struct dwc_pcd *)_p;

	/* Initialize the Core for Device mode. */
	if (dwc_otg_is_device_mode(GET_CORE_IF(pcd)))
		dwc_otg_core_dev_init(GET_CORE_IF(pcd));

	return 1;
}

/**
 * PCD Callback function for stopping the PCD when switching to Host
 * mode.
 */
static int dwc_otg_pcd_stop_cb(void *_p)
{
	dwc_otg_pcd_stop((struct dwc_pcd *)_p);
	return 1;
}

/**
 * PCD Callback function for notifying the PCD when resuming from
 * suspend.
 *
 * @param _p void pointer to the <code>struct dwc_pcd</code>
 */
static int dwc_otg_pcd_suspend_cb(void *_p)
{
	struct dwc_pcd *pcd = (struct dwc_pcd *)_p;

	if (pcd->driver && pcd->driver->suspend) {
		spin_unlock(&pcd->lock);
		pcd->driver->suspend(&pcd->gadget);
		spin_lock(&pcd->lock);
	}
	return 1;
}

/**
 * PCD Callback function for notifying the PCD when resuming from
 * suspend.
 */
static int dwc_otg_pcd_resume_cb(void *_p)
{
	struct dwc_pcd *pcd = (struct dwc_pcd *)_p;
	struct core_if *core_if = pcd->otg_dev->core_if;

	if (pcd->driver && pcd->driver->resume) {
		spin_unlock(&pcd->lock);
		pcd->driver->resume(&pcd->gadget);
		spin_lock(&pcd->lock);
	}

	/* Maybe stop the SRP timeout timer. */
	if (need_stop_srp_timer(core_if)) {
		core_if->srp_timer_started = 0;
		del_timer_sync(&pcd->srp_timer);
	}
	return 1;
}

/**
 * PCD Callback structure for handling mode switching.
 */
static struct cil_callbacks pcd_callbacks = {
	.start = dwc_otg_pcd_start_cb,
	.stop = dwc_otg_pcd_stop_cb,
	.suspend = dwc_otg_pcd_suspend_cb,
	.resume_wakeup = dwc_otg_pcd_resume_cb,
	.p = NULL,			/* Set at registration */
};

/**
 * Tasklet
 *
 */
static void start_xfer_tasklet_func(unsigned long data)
{
	struct dwc_pcd *pcd = (struct dwc_pcd *)data;
	u32 diepctl = 0;
	int num = pcd->otg_dev->core_if->dev_if->num_in_eps;
	u32 i;
	unsigned long flags;

	spin_lock_irqsave(&pcd->lock, flags);
	diepctl = dwc_reg_read(in_ep_ctl_reg(pcd, 0), 0);

	if (pcd->ep0.queue_sof) {
		pcd->ep0.queue_sof = 0;
		dwc_start_next_request(&pcd->ep0);
	}

	for (i = 0; i < num; i++) {
		u32 diepctl = 0;

		diepctl = dwc_reg_read(in_ep_ctl_reg(pcd, i), 0);
		if (pcd->in_ep[i].queue_sof) {
			pcd->in_ep[i].queue_sof = 0;
			dwc_start_next_request(&pcd->in_ep[i]);
		}
	}
	spin_unlock_irqrestore(&pcd->lock, flags);
}

static struct tasklet_struct start_xfer_tasklet = {
	.next = NULL,
	.state = 0,
	.count = ATOMIC_INIT(0),
	.func = start_xfer_tasklet_func,
	.data = 0,
};

/**
 * This function initialized the pcd Dp structures to there default
 * state.
 */
static void dwc_otg_pcd_reinit(struct dwc_pcd *pcd)
{
	static const char *names[] = {
		"ep0", "ep1in", "ep2in", "ep3in", "ep4in", "ep5in",
		"ep6in", "ep7in", "ep8in", "ep9in", "ep10in", "ep11in",
		"ep12in", "ep13in", "ep14in", "ep15in", "ep1out", "ep2out",
		"ep3out", "ep4out", "ep5out", "ep6out", "ep7out", "ep8out",
		"ep9out", "ep10out", "ep11out", "ep12out", "ep13out",
		"ep14out", "ep15out"
	};
	u32 i;
	int in_ep_cntr, out_ep_cntr;
	u32 hwcfg1;
	u32 num_in_eps = (GET_CORE_IF(pcd))->dev_if->num_in_eps;
	u32 num_out_eps = (GET_CORE_IF(pcd))->dev_if->num_out_eps;
	struct pcd_ep *ep;

	INIT_LIST_HEAD(&pcd->gadget.ep_list);
	pcd->gadget.ep0 = &pcd->ep0.ep;
	pcd->gadget.speed = USB_SPEED_UNKNOWN;
	INIT_LIST_HEAD(&pcd->gadget.ep0->ep_list);

	/* Initialize the EP0 structure. */
	ep = &pcd->ep0;

	/* Init EP structure */
	ep->desc = NULL;
	ep->pcd = pcd;
	ep->stopped = 1;

	/* Init DWC ep structure */
	ep->dwc_ep.num = 0;
	ep->dwc_ep.active = 0;
	ep->dwc_ep.tx_fifo_num = 0;

	/* Control until ep is actvated */
	ep->dwc_ep.type = DWC_OTG_EP_TYPE_CONTROL;
	ep->dwc_ep.maxpacket = MAX_PACKET_SIZE;
	ep->dwc_ep.dma_addr = 0;
	ep->dwc_ep.start_xfer_buff = NULL;
	ep->dwc_ep.xfer_buff = NULL;
	ep->dwc_ep.xfer_len = 0;
	ep->dwc_ep.xfer_count = 0;
	ep->dwc_ep.sent_zlp = 0;
	ep->dwc_ep.total_len = 0;
	ep->queue_sof = 0;
	mutex_init(&ep->dwc_ep.xfer_mutex);

	/* Init the usb_ep structure. */
	ep->ep.name = names[0];
	ep->ep.ops = &dwc_otg_pcd_ep_ops;

	ep->ep.maxpacket = MAX_PACKET_SIZE;
	list_add_tail(&ep->ep.ep_list, &pcd->gadget.ep_list);
	INIT_LIST_HEAD(&ep->queue);

	/* Initialize the EP structures. */
	in_ep_cntr = 0;
	hwcfg1 = (GET_CORE_IF(pcd))->hwcfg1 >> 3;

	for (i = 1; in_ep_cntr < num_in_eps; i++) {
		if (!(hwcfg1 & 0x1)) {
			struct pcd_ep *ep = &pcd->in_ep[in_ep_cntr];

			in_ep_cntr++;
			/* Init EP structure */
			ep->desc = NULL;
			ep->pcd = pcd;
			ep->stopped = 1;

			/* Init DWC ep structure */
			ep->dwc_ep.is_in = 1;
			ep->dwc_ep.num = i;
			ep->dwc_ep.active = 0;
			ep->dwc_ep.tx_fifo_num = 0;

			/* Control until ep is actvated */
			ep->dwc_ep.type = DWC_OTG_EP_TYPE_CONTROL;
			ep->dwc_ep.maxpacket = MAX_PACKET_SIZE;
			ep->dwc_ep.dma_addr = 0;
			ep->dwc_ep.start_xfer_buff = NULL;
			ep->dwc_ep.xfer_buff = NULL;
			ep->dwc_ep.xfer_len = 0;
			ep->dwc_ep.xfer_count = 0;
			ep->dwc_ep.sent_zlp = 0;
			ep->dwc_ep.total_len = 0;
			ep->queue_sof = 0;
			mutex_init(&ep->dwc_ep.xfer_mutex);

			ep->ep.name = names[i];
			ep->ep.ops = &dwc_otg_pcd_ep_ops;

			ep->ep.maxpacket = MAX_PACKET_SIZE;
			list_add_tail(&ep->ep.ep_list, &pcd->gadget.ep_list);
			INIT_LIST_HEAD(&ep->queue);
		}
		hwcfg1 >>= 2;
	}

	out_ep_cntr = 0;
	hwcfg1 = (GET_CORE_IF(pcd))->hwcfg1 >> 2;
	for (i = 1; out_ep_cntr < num_out_eps; i++) {
		if (!(hwcfg1 & 0x1)) {
			struct pcd_ep *ep = &pcd->out_ep[out_ep_cntr];

			out_ep_cntr++;
			/* Init EP structure */
			ep->desc = NULL;
			ep->pcd = pcd;
			ep->stopped = 1;

			/* Init DWC ep structure */
			ep->dwc_ep.is_in = 0;
			ep->dwc_ep.num = i;
			ep->dwc_ep.active = 0;
			ep->dwc_ep.tx_fifo_num = 0;

			/* Control until ep is actvated */
			ep->dwc_ep.type = DWC_OTG_EP_TYPE_CONTROL;
			ep->dwc_ep.maxpacket = MAX_PACKET_SIZE;
			ep->dwc_ep.dma_addr = 0;
			ep->dwc_ep.start_xfer_buff = NULL;
			ep->dwc_ep.xfer_buff = NULL;
			ep->dwc_ep.xfer_len = 0;
			ep->dwc_ep.xfer_count = 0;
			ep->dwc_ep.sent_zlp = 0;
			ep->dwc_ep.total_len = 0;
			ep->queue_sof = 0;
			mutex_init(&ep->dwc_ep.xfer_mutex);

			ep->ep.name = names[15 + i];
			ep->ep.ops = &dwc_otg_pcd_ep_ops;

			ep->ep.maxpacket = MAX_PACKET_SIZE;
			list_add_tail(&ep->ep.ep_list, &pcd->gadget.ep_list);
			INIT_LIST_HEAD(&ep->queue);
		}
		hwcfg1 >>= 2;
	}

	/* remove ep0 from the list.  There is a ep0 pointer. */
	list_del_init(&pcd->ep0.ep.ep_list);

	pcd->ep0state = EP0_DISCONNECT;
	pcd->ep0.ep.maxpacket = MAX_EP0_SIZE;
	pcd->ep0.dwc_ep.maxpacket = MAX_EP0_SIZE;
	pcd->ep0.dwc_ep.type = DWC_OTG_EP_TYPE_CONTROL;
}

/**
 * This function releases the Gadget device.
 * required by device_unregister().
 */
static void dwc_otg_pcd_gadget_release(struct device *dev)
{
	pr_info("%s(%p)\n", __func__, dev);
}

/**
 * Allocates the buffers for the setup packets when the PCD portion of the
 * driver is first initialized.
 */
static int init_pkt_buffs(struct device *dev, struct dwc_pcd *pcd)
{
	if (pcd->otg_dev->core_if->dma_enable) {
		pcd->dwc_pool = dma_pool_create("dwc_otg_pcd", dev,
						sizeof(*pcd->setup_pkt) * 5, 32,
						0);
		if (!pcd->dwc_pool)
			return -ENOMEM;
		pcd->setup_pkt = dma_pool_alloc(pcd->dwc_pool, GFP_KERNEL,
						&pcd->setup_pkt_dma_handle);
		if (!pcd->setup_pkt)
			goto error;
		pcd->status_buf = dma_pool_alloc(pcd->dwc_pool, GFP_KERNEL,
						 &pcd->status_buf_dma_handle);
		if (!pcd->status_buf)
			goto error1;
	} else {
		pcd->setup_pkt = kmalloc(sizeof(*pcd->setup_pkt) * 5,
					 GFP_KERNEL);
		if (!pcd->setup_pkt)
			return -ENOMEM;
		pcd->status_buf = kmalloc(sizeof(u16), GFP_KERNEL);
		if (!pcd->status_buf) {
			kfree(pcd->setup_pkt);
			return -ENOMEM;
		}
	}
	return 0;

error1:
	dma_pool_free(pcd->dwc_pool, pcd->setup_pkt, pcd->setup_pkt_dma_handle);
error:
	dma_pool_destroy(pcd->dwc_pool);
	return -ENOMEM;
}

/**
 * This function initializes the PCD portion of the driver.
 */
int dwc_otg_pcd_init(struct device *dev)
{
	static const char pcd_name[] = "dwc_otg_pcd";
	struct dwc_pcd *pcd;
	struct dwc_otg_device *otg_dev = dev_get_drvdata(dev);
	struct core_if *core_if = otg_dev->core_if;
	int retval;

	/* Allocate PCD structure */
	pcd = kzalloc(sizeof(*pcd), GFP_KERNEL);
	if (!pcd) {
		retval = -ENOMEM;
		goto err;
	}

	spin_lock_init(&pcd->lock);

	otg_dev->pcd = pcd;
	s_pcd = pcd;
	pcd->gadget.name = pcd_name;

	dev_set_name(&pcd->gadget.dev, "gadget");
	pcd->otg_dev = otg_dev;
	pcd->gadget.dev.parent = dev;
	pcd->gadget.dev.release = dwc_otg_pcd_gadget_release;
	pcd->gadget.ops = &dwc_otg_pcd_ops;
	pcd->gadget.max_speed = USB_SPEED_HIGH;

	if (DWC_HWCFG4_DED_FIFO_ENA_RD(core_if->hwcfg4))
		pr_info("Dedicated Tx FIFOs mode\n");
	else
		pr_info("Shared Tx FIFO mode\n");

/*	pcd->gadget.is_dualspeed = check_is_dual_speed(core_if);*/
	pcd->gadget.is_otg = check_is_otg(core_if);

	/* Register the gadget device */
	retval = device_register(&pcd->gadget.dev);
	if (retval)
		goto unreg_device;


	/* hook up the gadget*/
	retval = usb_add_gadget_udc(dev, &pcd->gadget);
	if (retval)
		goto unreg_device;

	/* Initialized the Core for Device mode. */
	if (dwc_otg_is_device_mode(core_if))
		dwc_otg_core_dev_init(core_if);

	/*  Initialize EP structures */
	dwc_otg_pcd_reinit(pcd);

	/* Register the PCD Callbacks. */
	dwc_otg_cil_register_pcd_callbacks(core_if, &pcd_callbacks, pcd);

	/* Setup interupt handler */
	retval = request_irq(otg_dev->irq, dwc_otg_pcd_irq, IRQF_SHARED,
			     pcd->gadget.name, pcd);
	if (retval) {
		pr_err("request of irq%d failed\n", otg_dev->irq);
		retval = -EBUSY;
		goto err_cleanup;
	}

	/* Initialize the DMA buffer for SETUP packets */
	retval = init_pkt_buffs(dev, pcd);
	if (retval)
		goto err_cleanup;

	/* Initialize tasklet */
	start_xfer_tasklet.data = (unsigned long)pcd;
	pcd->start_xfer_tasklet = &start_xfer_tasklet;
	return 0;

err_cleanup:
	kfree(pcd);
	otg_dev->pcd = NULL;
	s_pcd = NULL;

unreg_device:
	device_unregister(&pcd->gadget.dev);

err:
	return retval;
}

/**
 * Cleanup the PCD.
 */
void dwc_otg_pcd_remove(struct device *dev)
{
	struct dwc_otg_device *otg_dev = dev_get_drvdata(dev);
	struct dwc_pcd *pcd = otg_dev->pcd;

	/* Free the IRQ */
	free_irq(otg_dev->irq, pcd);

	/* start with the driver above us */
	if (pcd->driver) {
		/* should have been done already by driver model core */
		pr_warning("driver '%s' is still registered\n",
			   pcd->driver->driver.name);
		usb_gadget_unregister_driver(pcd->driver);
	}
	if (pcd->start_xfer_tasklet)
		tasklet_kill(pcd->start_xfer_tasklet);
	tasklet_kill(&pcd->test_mode_tasklet);

	device_unregister(&pcd->gadget.dev);
	if (GET_CORE_IF(pcd)->dma_enable) {
		dma_pool_free(pcd->dwc_pool, pcd->setup_pkt,
			      pcd->setup_pkt_dma_handle);
		dma_pool_free(pcd->dwc_pool, pcd->status_buf,
			      pcd->status_buf_dma_handle);
		dma_pool_destroy(pcd->dwc_pool);
	} else {
		kfree(pcd->setup_pkt);
		kfree(pcd->status_buf);
	}
	kfree(pcd);
	otg_dev->pcd = NULL;
}

/**
 * This function registers a gadget driver with the PCD.
 *
 * When a driver is successfully registered, it will receive control
 * requests including set_configuration(), which enables non-control
 * requests.  then usb traffic follows until a disconnect is reported.
 * then a host may connect again, or the driver might get unbound.
 */
static int dwc_gadget_start(struct usb_gadget *gadget,
		struct usb_gadget_driver *driver)
{
	if (!driver || driver->max_speed == USB_SPEED_UNKNOWN ||
	    !driver->unbind || !driver->disconnect || !driver->setup)
		return -EINVAL;

	if (s_pcd == NULL)
		return -ENODEV;

	if (s_pcd->driver != NULL)
		return -EBUSY;

	/* hook up the driver */
	s_pcd->driver = driver;
	s_pcd->gadget.dev.driver = &driver->driver;

	return 0;
}

/**
 * This function unregisters a gadget driver
 */
static int dwc_gadget_stop(struct usb_gadget *gadget,
		struct usb_gadget_driver *driver)
{
	struct core_if *core_if;

	if (!s_pcd)
		return -ENODEV;
	if (!driver || driver != s_pcd->driver)
		return -EINVAL;

	core_if = s_pcd->otg_dev->core_if;
	core_if->xceiv->state = OTG_STATE_UNDEFINED;
	otg_set_peripheral(core_if->xceiv->otg, NULL);

	driver->unbind(&s_pcd->gadget);
	s_pcd->driver = NULL;

	return 0;
}
