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

#if !defined(__DWC_PCD_H__)
#define __DWC_PCD_H__

#include "driver.h"

/*
 * This file contains the structures, constants, and interfaces for
 * the Perpherial Contoller Driver (PCD).
 *
 * The Peripheral Controller Driver (PCD) for Linux will implement the
 * Gadget API, so that the existing Gadget drivers can be used.	 For
 * the Mass Storage Function driver the File-backed USB Storage Gadget
 * (FBS) driver will be used.  The FBS driver supports the
 * Control-Bulk (CB), Control-Bulk-Interrupt (CBI), and Bulk-Only
 * transports.
 *
 */

/* Invalid DMA Address */
#define DMA_ADDR_INVALID			(~(dma_addr_t) 0)
/* Maxpacket size for EP0 */
#define MAX_EP0_SIZE				64
/* Maxpacket size for any EP */
#define MAX_PACKET_SIZE				1024

/*
 * Get the pointer to the core_if from the pcd pointer.
 */
#define GET_CORE_IF(_pcd) (_pcd->otg_dev->core_if)

/*
 * DWC_otg request structure.
 * This structure is a list of requests.
 */
struct pcd_request {
	struct usb_request req;	/* USB Request. */
	struct list_head queue;	/* queue of these requests. */
	unsigned mapped:1;
};

static inline ulong in_ep_int_reg(struct dwc_pcd *pd, int i)
{
	return GET_CORE_IF(pd)->dev_if->in_ep_regs[i] + DWC_DIEPINT;
}
static inline ulong out_ep_int_reg(struct dwc_pcd *pd, int i)
{
	return GET_CORE_IF(pd)->dev_if->out_ep_regs[i] + DWC_DOEPINT;
}
static inline ulong in_ep_ctl_reg(struct dwc_pcd *pd, int i)
{
	return GET_CORE_IF(pd)->dev_if->in_ep_regs[i] + DWC_DIEPCTL;
}

static inline ulong out_ep_ctl_reg(struct dwc_pcd *pd, int i)
{
	return GET_CORE_IF(pd)->dev_if->out_ep_regs[i] + DWC_DOEPCTL;
}

static inline ulong dev_ctl_reg(struct dwc_pcd *pd)
{
	return GET_CORE_IF(pd)->dev_if->dev_global_regs +
		      DWC_DCTL;
}

static inline ulong dev_diepmsk_reg(struct dwc_pcd *pd)
{
	return GET_CORE_IF(pd)->dev_if->dev_global_regs +
		      DWC_DIEPMSK;
}

static inline ulong dev_sts_reg(struct dwc_pcd *pd)
{
	return GET_CORE_IF(pd)->dev_if->dev_global_regs +
		      DWC_DSTS;
}

static inline ulong otg_ctl_reg(struct dwc_pcd *pd)
{
	return GET_CORE_IF(pd)->core_global_regs + DWC_GOTGCTL;
}

extern int __init dwc_otg_pcd_init(struct device *dev);

/*
 * The following functions support managing the DWC_otg controller in device
 * mode.
 */
extern void dwc_otg_ep_activate(struct core_if *core_if, struct dwc_ep *ep);
extern void dwc_otg_ep_start_transfer(struct core_if *_if, struct dwc_ep *ep);
extern void dwc_otg_ep_set_stall(struct core_if *core_if, struct dwc_ep *ep);
extern void dwc_otg_ep_clear_stall(struct core_if *core_if, struct dwc_ep *ep);
extern void dwc_otg_pcd_remove(struct device *dev);
extern int dwc_otg_pcd_handle_intr(struct dwc_pcd *pcd);
extern void dwc_otg_pcd_stop(struct dwc_pcd *pcd);
extern void request_nuke(struct pcd_ep *ep);
extern void dwc_otg_pcd_update_otg(struct dwc_pcd *pcd, const unsigned reset);
extern void dwc_otg_ep0_start_transfer(struct core_if *_if, struct dwc_ep *ep);

extern void request_done(struct pcd_ep *ep, struct pcd_request *req,
			 int _status);

extern void dwc_start_next_request(struct pcd_ep *ep);
#endif
