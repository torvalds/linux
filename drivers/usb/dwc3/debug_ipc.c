// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "debug-ipc.h"

#include <linux/moduleparam.h>

static unsigned int ep_addr_rxdbg_mask = 1;
module_param(ep_addr_rxdbg_mask, uint, 0644);
static unsigned int ep_addr_txdbg_mask = 1;
module_param(ep_addr_txdbg_mask, uint, 0644);

static int allow_dbg_print(u8 ep_num)
{
	int dir, num;

	/* allow bus wide events */
	if (ep_num == 0xff)
		return 1;

	dir = ep_num & 0x1;
	num = ep_num >> 1;
	num = 1 << num;

	if (dir && (num & ep_addr_txdbg_mask))
		return 1;
	if (!dir && (num & ep_addr_rxdbg_mask))
		return 1;

	return 0;
}

void dwc3_dbg_trace_log_ctrl(void *log_ctxt, struct usb_ctrlrequest *ctrl)
{
	char *ctrl_req_str;

	if (ctrl == NULL)
		return;

	ctrl_req_str = kzalloc(DWC3_MSG_MAX, GFP_ATOMIC);
	if (!ctrl_req_str)
		return;

	usb_decode_ctrl(ctrl_req_str, DWC3_MSG_MAX, ctrl->bRequestType,
				ctrl->bRequest, le16_to_cpu(ctrl->wValue),
				le16_to_cpu(ctrl->wIndex),
				le16_to_cpu(ctrl->wLength));
	ipc_log_string(log_ctxt, "dbg_trace_log_ctrl: %s", ctrl_req_str);
	kfree(ctrl_req_str);
}

void dwc3_dbg_trace_log_request(void *log_ctxt, struct dwc3_request *req,
				char *tag)
{
	struct dwc3_ep *dep;

	if (req == NULL)
		return;

	dep = req->dep;
	ipc_log_string(log_ctxt, "%s: %s: req %p length %u/%u %s%s%s ==> %d",
			tag, dep->name, req, req->request.actual,
			req->request.length,
			req->request.zero ? "Z" : "z",
			req->request.short_not_ok ? "S" : "s",
			req->request.no_interrupt ? "i" : "I",
			req->request.status);
}

void dwc3_dbg_trace_ep_cmd(void *log_ctxt, struct dwc3_ep *dep,
				unsigned int cmd,
				struct dwc3_gadget_ep_cmd_params *params,
				int cmd_status)
{
	ipc_log_string(log_ctxt,
			"dbg_send_ep_cmd: %s: cmd '%s' [%x] params %08x %08x %08x --> status: %s",
			dep->name, dwc3_gadget_ep_cmd_string(cmd), cmd, params->param0,
			params->param1, params->param2, dwc3_ep_cmd_status_string(cmd_status));
}

void dwc3_dbg_trace_trb_complete(void *log_ctxt, struct dwc3_ep *dep,
					struct dwc3_trb *trb, char *tag)
{
	char *s;
	int pcm = ((trb->size >> 24) & 3) + 1;

	switch (usb_endpoint_type(dep->endpoint.desc)) {
	case USB_ENDPOINT_XFER_INT:
	case USB_ENDPOINT_XFER_ISOC:
		switch (pcm) {
		case 1:
			s = "1x ";
			break;
		case 2:
			s = "2x ";
			break;
		case 3:
		default:
			s = "3x ";
			break;
		}
		break;
	default:
		s = "";
	}

	ipc_log_string(log_ctxt,
		       "%s: %s: trb %p (E%d:D%d) buf %08x%08x sz %s%d ctrl %08x (%c%c%c%c:%c%c:%s)",
		       tag, dep->name, trb, dep->trb_enqueue,
		       dep->trb_dequeue, trb->bph, trb->bpl, s, trb->size, trb->ctrl,
		       trb->ctrl & DWC3_TRB_CTRL_HWO ? 'H' : 'h',
		       trb->ctrl & DWC3_TRB_CTRL_LST ? 'L' : 'l',
		       trb->ctrl & DWC3_TRB_CTRL_CHN ? 'C' : 'c',
		       trb->ctrl & DWC3_TRB_CTRL_CSP ? 'S' : 's',
		       trb->ctrl & DWC3_TRB_CTRL_ISP_IMI ? 'S' : 's',
		       trb->ctrl & DWC3_TRB_CTRL_IOC ? 'C' : 'c',
		       dwc3_trb_type_string(DWC3_TRBCTL_TYPE(trb->ctrl)));
}

void dwc3_dbg_trace_event(void *log_ctxt, u32 event, struct dwc3 *dwc)
{
	char *event_str;

	event_str = kzalloc(DWC3_MSG_MAX, GFP_ATOMIC);
	if (!event_str)
		return;

	ipc_log_string(log_ctxt, "event (%08x): %s", event,
			dwc3_decode_event(event_str, DWC3_MSG_MAX,
					event, dwc->ep0state));
	kfree(event_str);
}

void dwc3_dbg_trace_ep(void *log_ctxt, struct dwc3_ep *dep)
{

	ipc_log_string(log_ctxt,
		"%s: mps %d/%d streams %d burst %d ring %d/%d flags %c:%c%c%c%c:%c",
		dep->name, dep->endpoint.maxpacket,
		dep->endpoint.maxpacket_limit, dep->endpoint.max_streams,
		dep->endpoint.maxburst, dep->trb_enqueue,
		dep->trb_dequeue,
		dep->flags & DWC3_EP_ENABLED ? 'E' : 'e',
		dep->flags & DWC3_EP_STALL ? 'S' : 's',
		dep->flags & DWC3_EP_WEDGE ? 'W' : 'w',
		dep->flags & DWC3_EP_TRANSFER_STARTED ? 'B' : 'b',
		dep->flags & DWC3_EP_PENDING_REQUEST ? 'P' : 'p',
		dep->direction ? '<' : '>');
}

/**
 * dwc3_dbg_print:  prints the common part of the event
 * @addr:   endpoint address
 * @name:   event name
 * @status: status
 * @extra:  extra information
 * @dwc3: pointer to struct dwc3
 */
void dwc3_dbg_print(void *log_ctxt, u8 ep_num, const char *name,
			int status, const char *extra)
{
	if (!allow_dbg_print(ep_num))
		return;

	if (name == NULL)
		return;

	ipc_log_string(log_ctxt, "%02X %-25.25s %4i ?\t%s",
			ep_num, name, status, extra);
}

/**
 * dwc3_dbg_done: prints a DONE event
 * @addr:   endpoint address
 * @td:     transfer descriptor
 * @status: status
 * @dwc3: pointer to struct dwc3
 */
void dwc3_dbg_done(void *log_ctxt, u8 ep_num,
		const u32 count, int status)
{
	if (!allow_dbg_print(ep_num))
		return;

	ipc_log_string(log_ctxt, "%02X %-25.25s %4i ?\t%d",
			ep_num, "DONE", status, count);
}

/**
 * dwc3_dbg_event: prints a generic event
 * @addr:   endpoint address
 * @name:   event name
 * @status: status
 */
void dwc3_dbg_event(void *log_ctxt, u8 ep_num, const char *name, int status)
{
	if (!allow_dbg_print(ep_num))
		return;

	if (name != NULL)
		dwc3_dbg_print(log_ctxt, ep_num, name, status, "");
}

/*
 * dwc3_dbg_queue: prints a QUEUE event
 * @addr:   endpoint address
 * @req:    USB request
 * @status: status
 */
void dwc3_dbg_queue(void *log_ctxt, u8 ep_num,
		const struct usb_request *req, int status)
{
	if (!allow_dbg_print(ep_num))
		return;

	if (req != NULL) {
		ipc_log_string(log_ctxt,
			"%02X %-25.25s %4i ?\t%d %d", ep_num, "QUEUE", status,
			!req->no_interrupt, req->length);
	}
}

/**
 * dwc3_dbg_setup: prints a SETUP event
 * @addr: endpoint address
 * @req:  setup request
 */
void dwc3_dbg_setup(void *log_ctxt, u8 ep_num,
		const struct usb_ctrlrequest *req)
{
	if (!allow_dbg_print(ep_num))
		return;

	if (req != NULL) {
		ipc_log_string(log_ctxt,
			"%02X %-25.25s ?\t%02X %02X %04X %04X %d",
			ep_num, "SETUP", req->bRequestType,
			req->bRequest, le16_to_cpu(req->wValue),
			le16_to_cpu(req->wIndex), le16_to_cpu(req->wLength));
	}
}

/**
 * dwc3_dbg_print_reg: prints a reg value
 * @name:   reg name
 * @reg: reg value to be printed
 */
void dwc3_dbg_print_reg(void *log_ctxt, const char *name, int reg)
{
	if (name == NULL)
		return;

	ipc_log_string(log_ctxt, "%s = 0x%08x", name, reg);
}

void dwc3_dbg_dma_unmap(void *log_ctxt, u8 ep_num, struct dwc3_request *req)
{
	if (ep_num < 2)
		return;
	ipc_log_string(log_ctxt,
		"%02X-%-3.3s %-25.25s 0x%pK %pad %u %pad %s", ep_num >> 1,
		ep_num & 1 ? "IN":"OUT", "UNMAP", &req->request,
		&req->request.dma, req->request.length, &req->trb_dma,
		req->trb->ctrl & DWC3_TRB_CTRL_HWO ? "HWO" : "");
}

void dwc3_dbg_dma_map(void *log_ctxt, u8 ep_num, struct dwc3_request *req)
{
	if (ep_num < 2)
		return;

	ipc_log_string(log_ctxt,
		"%02X-%-3.3s %-25.25s 0x%pK %pad %u %pad", ep_num >> 1,
		ep_num & 1 ? "IN":"OUT", "MAP", &req->request,
		&req->request.dma, req->request.length, &req->trb_dma);
}

void dwc3_dbg_dma_dequeue(void *log_ctxt, u8 ep_num, struct dwc3_request *req)
{
	if (ep_num < 2)
		return;

	ipc_log_string(log_ctxt,
		"%02X-%-3.3s %-25.25s 0x%pK %pad %pad", ep_num >> 1,
		ep_num & 1 ? "IN":"OUT", "DEQUEUE", &req->request,
		&req->request.dma, &req->trb_dma);
}

void dwc3_dbg_dma_queue(void *log_ctxt, u8 ep_num, struct dwc3_request *req)
{
	if (ep_num < 2)
		return;

	ipc_log_string(log_ctxt,
		"%02X-%-3.3s %-25.25s 0x%pK", ep_num >> 1,
		ep_num & 1 ? "IN":"OUT", "QUEUE", &req->request);
}
