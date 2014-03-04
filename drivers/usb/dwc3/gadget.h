/**
 * gadget.h - DesignWare USB3 DRD Gadget Header
 *
 * Copyright (C) 2010-2011 Texas Instruments Incorporated - http://www.ti.com
 *
 * Authors: Felipe Balbi <balbi@ti.com>,
 *	    Sebastian Andrzej Siewior <bigeasy@linutronix.de>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2  of
 * the License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __DRIVERS_USB_DWC3_GADGET_H
#define __DRIVERS_USB_DWC3_GADGET_H

#include <linux/list.h>
#include <linux/usb/gadget.h>
#include "io.h"

struct dwc3;
#define to_dwc3_ep(ep)		(container_of(ep, struct dwc3_ep, endpoint))
#define gadget_to_dwc(g)	(container_of(g, struct dwc3, gadget))

/* DEPCFG parameter 1 */
#define DWC3_DEPCFG_INT_NUM(n)		((n) << 0)
#define DWC3_DEPCFG_XFER_COMPLETE_EN	(1 << 8)
#define DWC3_DEPCFG_XFER_IN_PROGRESS_EN	(1 << 9)
#define DWC3_DEPCFG_XFER_NOT_READY_EN	(1 << 10)
#define DWC3_DEPCFG_FIFO_ERROR_EN	(1 << 11)
#define DWC3_DEPCFG_STREAM_EVENT_EN	(1 << 13)
#define DWC3_DEPCFG_BINTERVAL_M1(n)	((n) << 16)
#define DWC3_DEPCFG_STREAM_CAPABLE	(1 << 24)
#define DWC3_DEPCFG_EP_NUMBER(n)	((n) << 25)
#define DWC3_DEPCFG_BULK_BASED		(1 << 30)
#define DWC3_DEPCFG_FIFO_BASED		(1 << 31)

/* DEPCFG parameter 0 */
#define DWC3_DEPCFG_EP_TYPE(n)		((n) << 1)
#define DWC3_DEPCFG_MAX_PACKET_SIZE(n)	((n) << 3)
#define DWC3_DEPCFG_FIFO_NUMBER(n)	((n) << 17)
#define DWC3_DEPCFG_BURST_SIZE(n)	((n) << 22)
#define DWC3_DEPCFG_DATA_SEQ_NUM(n)	((n) << 26)
/* This applies for core versions earlier than 1.94a */
#define DWC3_DEPCFG_IGN_SEQ_NUM		(1 << 31)
/* These apply for core versions 1.94a and later */
#define DWC3_DEPCFG_ACTION_INIT		(0 << 30)
#define DWC3_DEPCFG_ACTION_RESTORE	(1 << 30)
#define DWC3_DEPCFG_ACTION_MODIFY	(2 << 30)

/* DEPXFERCFG parameter 0 */
#define DWC3_DEPXFERCFG_NUM_XFER_RES(n)	((n) & 0xffff)

/* -------------------------------------------------------------------------- */

#define to_dwc3_request(r)	(container_of(r, struct dwc3_request, request))

static inline struct dwc3_request *next_request(struct list_head *list)
{
	if (list_empty(list))
		return NULL;

	return list_first_entry(list, struct dwc3_request, list);
}

static inline void dwc3_gadget_move_request_queued(struct dwc3_request *req)
{
	struct dwc3_ep		*dep = req->dep;

	req->queued = true;
	list_move_tail(&req->list, &dep->req_queued);
}

void dwc3_gadget_giveback(struct dwc3_ep *dep, struct dwc3_request *req,
		int status);

void dwc3_ep0_interrupt(struct dwc3 *dwc,
		const struct dwc3_event_depevt *event);
void dwc3_ep0_out_start(struct dwc3 *dwc);
int dwc3_gadget_ep0_set_halt(struct usb_ep *ep, int value);
int dwc3_gadget_ep0_queue(struct usb_ep *ep, struct usb_request *request,
		gfp_t gfp_flags);
int __dwc3_gadget_ep_set_halt(struct dwc3_ep *dep, int value);

/**
 * dwc3_gadget_ep_get_transfer_index - Gets transfer index from HW
 * @dwc: DesignWare USB3 Pointer
 * @number: DWC endpoint number
 *
 * Caller should take care of locking
 */
static inline u32 dwc3_gadget_ep_get_transfer_index(struct dwc3 *dwc, u8 number)
{
	u32			res_id;

	res_id = dwc3_readl(dwc->regs, DWC3_DEPCMD(number));

	return DWC3_DEPCMD_GET_RSC_IDX(res_id);
}

/**
 * dwc3_gadget_event_string - returns event name
 * @event: the event code
 */
static inline const char *dwc3_gadget_event_string(u8 event)
{
	switch (event) {
	case DWC3_DEVICE_EVENT_DISCONNECT:
		return "Disconnect";
	case DWC3_DEVICE_EVENT_RESET:
		return "Reset";
	case DWC3_DEVICE_EVENT_CONNECT_DONE:
		return "Connection Done";
	case DWC3_DEVICE_EVENT_LINK_STATUS_CHANGE:
		return "Link Status Change";
	case DWC3_DEVICE_EVENT_WAKEUP:
		return "WakeUp";
	case DWC3_DEVICE_EVENT_EOPF:
		return "End-Of-Frame";
	case DWC3_DEVICE_EVENT_SOF:
		return "Start-Of-Frame";
	case DWC3_DEVICE_EVENT_ERRATIC_ERROR:
		return "Erratic Error";
	case DWC3_DEVICE_EVENT_CMD_CMPL:
		return "Command Complete";
	case DWC3_DEVICE_EVENT_OVERFLOW:
		return "Overflow";
	}

	return "UNKNOWN";
}

/**
 * dwc3_ep_event_string - returns event name
 * @event: then event code
 */
static inline const char *dwc3_ep_event_string(u8 event)
{
	switch (event) {
	case DWC3_DEPEVT_XFERCOMPLETE:
		return "Transfer Complete";
	case DWC3_DEPEVT_XFERINPROGRESS:
		return "Transfer In-Progress";
	case DWC3_DEPEVT_XFERNOTREADY:
		return "Transfer Not Ready";
	case DWC3_DEPEVT_RXTXFIFOEVT:
		return "FIFO";
	case DWC3_DEPEVT_STREAMEVT:
		return "Stream";
	case DWC3_DEPEVT_EPCMDCMPLT:
		return "Endpoint Command Complete";
	}

	return "UNKNOWN";
}

#endif /* __DRIVERS_USB_DWC3_GADGET_H */
