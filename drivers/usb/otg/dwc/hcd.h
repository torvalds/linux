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
 * Modified by Chuck Meade <chuck@theptrgroup.com>
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

#if !defined(__DWC_HCD_H__)
#define __DWC_HCD_H__

#include <linux/usb.h>
#include <linux/usb/hcd.h>

#include "driver.h"

/*
 * This file contains the structures, constants, and interfaces for
 * the Host Contoller Driver (HCD).
 *
 * The Host Controller Driver (HCD) is responsible for translating requests
 * from the USB Driver into the appropriate actions on the DWC_otg controller.
 * It isolates the USBD from the specifics of the controller by providing an
 * API to the USBD.
 */

/* Phases for control transfers. */
enum dwc_control_phase {
	DWC_OTG_CONTROL_SETUP,
	DWC_OTG_CONTROL_DATA,
	DWC_OTG_CONTROL_STATUS
};

/* Transaction types. */
enum dwc_transaction_type {
	DWC_OTG_TRANSACTION_NONE,
	DWC_OTG_TRANSACTION_PERIODIC,
	DWC_OTG_TRANSACTION_NON_PERIODIC,
	DWC_OTG_TRANSACTION_ALL
};

/*
 * A Queue Transfer Descriptor (QTD) holds the state of a bulk, control,
 * interrupt, or isochronous transfer. A single QTD is created for each URB
 * (of one of these types) submitted to the HCD. The transfer associated with
 * a QTD may require one or multiple transactions.
 *
 * A QTD is linked to a Queue Head, which is entered in either the
 * non-periodic or periodic schedule for execution. When a QTD is chosen for
 * execution, some or all of its transactions may be executed. After
 * execution, the state of the QTD is updated. The QTD may be retired if all
 * its transactions are complete or if an error occurred. Otherwise, it
 * remains in the schedule so more transactions can be executed later.
 */
struct dwc_qtd {
	/*
	 * Determines the PID of the next data packet for the data phase of
	 * control transfers. Ignored for other transfer types.
	 * One of the following values:
	 *      - DWC_OTG_HC_PID_DATA0
	 *      - DWC_OTG_HC_PID_DATA1
	 */
	u8 data_toggle;

	/* Current phase for control transfers (Setup, Data, or Status). */
	enum dwc_control_phase control_phase;

	/*
	 * Keep track of the current split type
	 * for FS/LS endpoints on a HS Hub
	 */
	u8 complete_split;

	/* How many bytes transferred during SSPLIT OUT */
	u32 ssplit_out_xfer_count;

	/*
	 * Holds the number of bus errors that have occurred for a transaction
	 * within this transfer.
	 */
	u8 error_count;

	/*
	 * Index of the next frame descriptor for an isochronous transfer. A
	 * frame descriptor describes the buffer position and length of the
	 * data to be transferred in the next scheduled (micro)frame of an
	 * isochronous transfer. It also holds status for that transaction.
	 * The frame index starts at 0.
	 */
	int isoc_frame_index;

	/* Position of the ISOC split on full/low speed */
	u8 isoc_split_pos;

	/* Position of the ISOC split in the buffer for the current frame */
	u16 isoc_split_offset;

	/* URB for this transfer */
	struct urb *urb;

	/* This list of QTDs */
	struct list_head qtd_list_entry;

	/* Field to track the qh pointer */
	struct dwc_qh *qtd_qh_ptr;
};

/*
 * A Queue Head (QH) holds the static characteristics of an endpoint and
 * maintains a list of transfers (QTDs) for that endpoint. A QH structure may
 * be entered in either the non-periodic or periodic schedule.
 */
struct dwc_qh {
	/*
	 * Endpoint type.
	 * One of the following values:
	 *      - USB_ENDPOINT_XFER_CONTROL
	 *      - USB_ENDPOINT_XFER_ISOC
	 *      - USB_ENDPOINT_XFER_BULK
	 *      - USB_ENDPOINT_XFER_INT
	 */
	u8 ep_type;
	u8 ep_is_in;

	/* wMaxPacketSize Field of Endpoint Descriptor. */
	u16 maxp;

	/*
	 * Determines the PID of the next data packet for non-control
	 * transfers. Ignored for control transfers.
	 * One of the following values:
	 *      - DWC_OTG_HC_PID_DATA0
	 *      - DWC_OTG_HC_PID_DATA1
	 */
	u8 data_toggle;

	/* Ping state if 1. */
	u8 ping_state;

	/* List of QTDs for this QH. */
	struct list_head qtd_list;

	/* Host channel currently processing transfers for this QH. */
	struct dwc_hc *channel;

	/* QTD currently assigned to a host channel for this QH. */
	struct dwc_qtd *qtd_in_process;

	/* Full/low speed endpoint on high-speed hub requires split. */
	u8 do_split;

	/* Periodic schedule information */

	/* Bandwidth in microseconds per (micro)frame. */
	u8 usecs;

	/* Interval between transfers in (micro)frames. */
	u16 interval;

	/*
	 * (micro)frame to initialize a periodic transfer. The transfer
	 * executes in the following (micro)frame.
	 */
	u16 sched_frame;

	/* (micro)frame at which last start split was initialized. */
	u16 start_split_frame;

	u16 speed;
	u16 frame_usecs[8];

	/* Entry for QH in either the periodic or non-periodic schedule. */
	struct list_head qh_list_entry;
};

/* Gets the struct usb_hcd that contains a struct dwc_hcd. */
static inline struct usb_hcd *dwc_otg_hcd_to_hcd(struct dwc_hcd *dwc_hcd)
{
	return container_of((void *)dwc_hcd, struct usb_hcd, hcd_priv);
}

/* HCD Create/Destroy Functions */
extern int __init dwc_otg_hcd_init(struct device *_dev,
				   struct dwc_otg_device *dwc_dev);
extern void dwc_otg_hcd_remove(struct device *_dev);

/*
 * The following functions support managing the DWC_otg controller in host
 * mode.
 */
extern int dwc_otg_hcd_get_frame_number(struct usb_hcd *hcd);
extern void dwc_otg_hc_cleanup(struct core_if *core_if, struct dwc_hc *hc);
extern void dwc_otg_hc_halt(struct core_if *core_if, struct dwc_hc *hc,
			    enum dwc_halt_status _halt_status);

/* Transaction Execution Functions */
extern enum dwc_transaction_type dwc_otg_hcd_select_transactions(struct dwc_hcd
								 *hcd);
extern void dwc_otg_hcd_queue_transactions(struct dwc_hcd *hcd,
					   enum dwc_transaction_type tr_type);
extern void dwc_otg_hcd_complete_urb(struct dwc_hcd *_hcd, struct urb *urb,
				     int status);

/* Interrupt Handler Functions */
extern int dwc_otg_hcd_handle_intr(struct dwc_hcd *hcd);

/* Schedule Queue Functions */
extern int init_hcd_usecs(struct dwc_hcd *hcd);
extern void dwc_otg_hcd_qh_free(struct dwc_qh *qh);
extern void dwc_otg_hcd_qh_remove(struct dwc_hcd *hcd, struct dwc_qh *qh);
extern void dwc_otg_hcd_qh_deactivate(struct dwc_hcd *hcd, struct dwc_qh *qh,
				      int sched_csplit);
extern int dwc_otg_hcd_qh_deferr(struct dwc_hcd *hcd, struct dwc_qh *qh,
				 int delay);
extern struct dwc_qtd *dwc_otg_hcd_qtd_create(struct urb *urb,
					      gfp_t _mem_flags);
extern int dwc_otg_hcd_qtd_add(struct dwc_qtd *qtd, struct dwc_hcd *dwc_hcd);

/*
 * Frees the memory for a QTD structure.  QTD should already be removed from
 * list.
 */
static inline void dwc_otg_hcd_qtd_free(struct dwc_qtd *_qtd)
{
	kfree(_qtd);
}

/* Removes a QTD from list. */
static inline void dwc_otg_hcd_qtd_remove(struct dwc_qtd *_qtd)
{
	list_del(&_qtd->qtd_list_entry);
}

/* Remove and free a QTD */
static inline void dwc_otg_hcd_qtd_remove_and_free(struct dwc_qtd *_qtd)
{
	dwc_otg_hcd_qtd_remove(_qtd);
	dwc_otg_hcd_qtd_free(_qtd);
}

struct dwc_qh *dwc_urb_to_qh(struct urb *_urb);

/* Gets the usb_host_endpoint associated with an URB. */
static inline struct usb_host_endpoint *dwc_urb_to_endpoint(struct urb *_urb)
{
	struct usb_device *dev = _urb->dev;
	int ep_num = usb_pipeendpoint(_urb->pipe);

	if (usb_pipein(_urb->pipe))
		return dev->ep_in[ep_num];
	else
		return dev->ep_out[ep_num];
}

/*
 * Gets the endpoint number from a _bEndpointAddress argument. The endpoint is
 * qualified with its direction (possible 32 endpoints per device).
 */
#define dwc_ep_addr_to_endpoint(_bEndpointAddress_) \
		((_bEndpointAddress_ & USB_ENDPOINT_NUMBER_MASK) | \
		((_bEndpointAddress_ & USB_DIR_IN) != 0) << 4)

/* Gets the QH that contains the list_head */
#define dwc_list_to_qh(_list_head_ptr_) \
		(container_of(_list_head_ptr_, struct dwc_qh, qh_list_entry))

/* Gets the QTD that contains the list_head */
#define dwc_list_to_qtd(_list_head_ptr_) \
		(container_of(_list_head_ptr_, struct dwc_qtd, qtd_list_entry))

/* Check if QH is non-periodic  */
#define dwc_qh_is_non_per(_qh_ptr_) \
		((_qh_ptr_->ep_type == USB_ENDPOINT_XFER_BULK) || \
		(_qh_ptr_->ep_type == USB_ENDPOINT_XFER_CONTROL))

/* High bandwidth multiplier as encoded in highspeed endpoint descriptors */
#define dwc_hb_mult(wMaxPacketSize)	(1 + (((wMaxPacketSize) >> 11) & 0x03))

/* Packet size for any kind of endpoint descriptor */
#define dwc_max_packet(wMaxPacketSize)	((wMaxPacketSize) & 0x07ff)

/*
 * Returns true if _frame1 is less than or equal to _frame2. The comparison is
 * done modulo DWC_HFNUM_MAX_FRNUM. This accounts for the rollover of the
 * frame number when the max frame number is reached.
 */
static inline int dwc_frame_num_le(u16 _frame1, u16 _frame2)
{
	return ((_frame2 - _frame1) & DWC_HFNUM_MAX_FRNUM) <=
	    (DWC_HFNUM_MAX_FRNUM >> 1);
}

/*
 * Returns true if _frame1 is greater than _frame2. The comparison is done
 * modulo DWC_HFNUM_MAX_FRNUM. This accounts for the rollover of the frame
 * number when the max frame number is reached.
 */
static inline int dwc_frame_num_gt(u16 _frame1, u16 _frame2)
{
	return (_frame1 != _frame2) &&
	    (((_frame1 - _frame2) &
	      DWC_HFNUM_MAX_FRNUM) < (DWC_HFNUM_MAX_FRNUM >> 1));
}

/*
 * Increments _frame by the amount specified by _inc. The addition is done
 * modulo DWC_HFNUM_MAX_FRNUM. Returns the incremented value.
 */
static inline u16 dwc_frame_num_inc(u16 _frame, u16 _inc)
{
	return (_frame + _inc) & DWC_HFNUM_MAX_FRNUM;
}

static inline u16 dwc_full_frame_num(u16 _frame)
{
	return ((_frame) & DWC_HFNUM_MAX_FRNUM) >> 3;
}

static inline u16 dwc_micro_frame_num(u16 _frame)
{
	return (_frame) & 0x7;
}

static inline ulong gintsts_reg(struct dwc_hcd *hcd)
{
	ulong global_regs = hcd->core_if->core_global_regs;
	return global_regs + DWC_GINTSTS;
}

static inline ulong gintmsk_reg(struct dwc_hcd *hcd)
{
	ulong global_regs = hcd->core_if->core_global_regs;
	return global_regs + DWC_GINTMSK;
}

static inline ulong gahbcfg_reg(struct dwc_hcd *hcd)
{
	ulong global_regs = hcd->core_if->core_global_regs;
	return global_regs + DWC_GAHBCFG;
}

static inline const char *pipetype_str(unsigned int pipe)
{
	switch (usb_pipetype(pipe)) {
	case PIPE_CONTROL:
		return "control";
	case PIPE_BULK:
		return "bulk";
	case PIPE_INTERRUPT:
		return "interrupt";
	case PIPE_ISOCHRONOUS:
		return "isochronous";
	default:
		return "unknown";
	}
}

static inline const char *dev_speed_str(enum usb_device_speed speed)
{
	switch (speed) {
	case USB_SPEED_HIGH:
		return "high";
	case USB_SPEED_FULL:
		return "full";
	case USB_SPEED_LOW:
		return "low";
	default:
		return "unknown";
	}
}

static inline const char *ep_type_str(u8 type)
{
	switch (type) {
	case USB_ENDPOINT_XFER_ISOC:
		return "isochronous";
	case USB_ENDPOINT_XFER_INT:
		return "interrupt";
	case USB_ENDPOINT_XFER_CONTROL:
		return "control";
	case USB_ENDPOINT_XFER_BULK:
		return "bulk";
	default:
		return "?";
	}
}
#endif
