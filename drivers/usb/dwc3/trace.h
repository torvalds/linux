// SPDX-License-Identifier: GPL-2.0
/**
 * trace.h - DesignWare USB3 DRD Controller Trace Support
 *
 * Copyright (C) 2014 Texas Instruments Incorporated - http://www.ti.com
 *
 * Author: Felipe Balbi <balbi@ti.com>
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM dwc3

#if !defined(__DWC3_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define __DWC3_TRACE_H

#include <linux/types.h>
#include <linux/tracepoint.h>
#include <asm/byteorder.h>
#include "core.h"
#include "debug.h"

DECLARE_EVENT_CLASS(dwc3_log_io,
	TP_PROTO(void *base, u32 offset, u32 value),
	TP_ARGS(base, offset, value),
	TP_STRUCT__entry(
		__field(void *, base)
		__field(u32, offset)
		__field(u32, value)
	),
	TP_fast_assign(
		__entry->base = base;
		__entry->offset = offset;
		__entry->value = value;
	),
	TP_printk("addr %p value %08x", __entry->base + __entry->offset,
			__entry->value)
);

DEFINE_EVENT(dwc3_log_io, dwc3_readl,
	TP_PROTO(void __iomem *base, u32 offset, u32 value),
	TP_ARGS(base, offset, value)
);

DEFINE_EVENT(dwc3_log_io, dwc3_writel,
	TP_PROTO(void __iomem *base, u32 offset, u32 value),
	TP_ARGS(base, offset, value)
);

DECLARE_EVENT_CLASS(dwc3_log_event,
	TP_PROTO(u32 event, struct dwc3 *dwc),
	TP_ARGS(event, dwc),
	TP_STRUCT__entry(
		__field(u32, event)
		__field(u32, ep0state)
		__dynamic_array(char, str, DWC3_MSG_MAX)
	),
	TP_fast_assign(
		__entry->event = event;
		__entry->ep0state = dwc->ep0state;
	),
	TP_printk("event (%08x): %s", __entry->event,
			dwc3_decode_event(__get_str(str), __entry->event,
					  __entry->ep0state))
);

DEFINE_EVENT(dwc3_log_event, dwc3_event,
	TP_PROTO(u32 event, struct dwc3 *dwc),
	TP_ARGS(event, dwc)
);

DECLARE_EVENT_CLASS(dwc3_log_ctrl,
	TP_PROTO(struct usb_ctrlrequest *ctrl),
	TP_ARGS(ctrl),
	TP_STRUCT__entry(
		__field(__u8, bRequestType)
		__field(__u8, bRequest)
		__field(__u16, wValue)
		__field(__u16, wIndex)
		__field(__u16, wLength)
		__dynamic_array(char, str, DWC3_MSG_MAX)
	),
	TP_fast_assign(
		__entry->bRequestType = ctrl->bRequestType;
		__entry->bRequest = ctrl->bRequest;
		__entry->wValue = le16_to_cpu(ctrl->wValue);
		__entry->wIndex = le16_to_cpu(ctrl->wIndex);
		__entry->wLength = le16_to_cpu(ctrl->wLength);
	),
	TP_printk("%s", dwc3_decode_ctrl(__get_str(str), __entry->bRequestType,
					__entry->bRequest, __entry->wValue,
					__entry->wIndex, __entry->wLength)
	)
);

DEFINE_EVENT(dwc3_log_ctrl, dwc3_ctrl_req,
	TP_PROTO(struct usb_ctrlrequest *ctrl),
	TP_ARGS(ctrl)
);

DECLARE_EVENT_CLASS(dwc3_log_request,
	TP_PROTO(struct dwc3_request *req),
	TP_ARGS(req),
	TP_STRUCT__entry(
		__string(name, req->dep->name)
		__field(struct dwc3_request *, req)
		__field(unsigned, actual)
		__field(unsigned, length)
		__field(int, status)
		__field(int, zero)
		__field(int, short_not_ok)
		__field(int, no_interrupt)
	),
	TP_fast_assign(
		__assign_str(name, req->dep->name);
		__entry->req = req;
		__entry->actual = req->request.actual;
		__entry->length = req->request.length;
		__entry->status = req->request.status;
		__entry->zero = req->request.zero;
		__entry->short_not_ok = req->request.short_not_ok;
		__entry->no_interrupt = req->request.no_interrupt;
	),
	TP_printk("%s: req %p length %u/%u %s%s%s ==> %d",
		__get_str(name), __entry->req, __entry->actual, __entry->length,
		__entry->zero ? "Z" : "z",
		__entry->short_not_ok ? "S" : "s",
		__entry->no_interrupt ? "i" : "I",
		__entry->status
	)
);

DEFINE_EVENT(dwc3_log_request, dwc3_alloc_request,
	TP_PROTO(struct dwc3_request *req),
	TP_ARGS(req)
);

DEFINE_EVENT(dwc3_log_request, dwc3_free_request,
	TP_PROTO(struct dwc3_request *req),
	TP_ARGS(req)
);

DEFINE_EVENT(dwc3_log_request, dwc3_ep_queue,
	TP_PROTO(struct dwc3_request *req),
	TP_ARGS(req)
);

DEFINE_EVENT(dwc3_log_request, dwc3_ep_dequeue,
	TP_PROTO(struct dwc3_request *req),
	TP_ARGS(req)
);

DEFINE_EVENT(dwc3_log_request, dwc3_gadget_giveback,
	TP_PROTO(struct dwc3_request *req),
	TP_ARGS(req)
);

DECLARE_EVENT_CLASS(dwc3_log_generic_cmd,
	TP_PROTO(unsigned int cmd, u32 param, int status),
	TP_ARGS(cmd, param, status),
	TP_STRUCT__entry(
		__field(unsigned int, cmd)
		__field(u32, param)
		__field(int, status)
	),
	TP_fast_assign(
		__entry->cmd = cmd;
		__entry->param = param;
		__entry->status = status;
	),
	TP_printk("cmd '%s' [%x] param %08x --> status: %s",
		dwc3_gadget_generic_cmd_string(__entry->cmd),
		__entry->cmd, __entry->param,
		dwc3_gadget_generic_cmd_status_string(__entry->status)
	)
);

DEFINE_EVENT(dwc3_log_generic_cmd, dwc3_gadget_generic_cmd,
	TP_PROTO(unsigned int cmd, u32 param, int status),
	TP_ARGS(cmd, param, status)
);

DECLARE_EVENT_CLASS(dwc3_log_gadget_ep_cmd,
	TP_PROTO(struct dwc3_ep *dep, unsigned int cmd,
		struct dwc3_gadget_ep_cmd_params *params, int cmd_status),
	TP_ARGS(dep, cmd, params, cmd_status),
	TP_STRUCT__entry(
		__string(name, dep->name)
		__field(unsigned int, cmd)
		__field(u32, param0)
		__field(u32, param1)
		__field(u32, param2)
		__field(int, cmd_status)
	),
	TP_fast_assign(
		__assign_str(name, dep->name);
		__entry->cmd = cmd;
		__entry->param0 = params->param0;
		__entry->param1 = params->param1;
		__entry->param2 = params->param2;
		__entry->cmd_status = cmd_status;
	),
	TP_printk("%s: cmd '%s' [%d] params %08x %08x %08x --> status: %s",
		__get_str(name), dwc3_gadget_ep_cmd_string(__entry->cmd),
		__entry->cmd, __entry->param0,
		__entry->param1, __entry->param2,
		dwc3_ep_cmd_status_string(__entry->cmd_status)
	)
);

DEFINE_EVENT(dwc3_log_gadget_ep_cmd, dwc3_gadget_ep_cmd,
	TP_PROTO(struct dwc3_ep *dep, unsigned int cmd,
		struct dwc3_gadget_ep_cmd_params *params, int cmd_status),
	TP_ARGS(dep, cmd, params, cmd_status)
);

DECLARE_EVENT_CLASS(dwc3_log_trb,
	TP_PROTO(struct dwc3_ep *dep, struct dwc3_trb *trb),
	TP_ARGS(dep, trb),
	TP_STRUCT__entry(
		__string(name, dep->name)
		__field(struct dwc3_trb *, trb)
		__field(u32, allocated)
		__field(u32, queued)
		__field(u32, bpl)
		__field(u32, bph)
		__field(u32, size)
		__field(u32, ctrl)
		__field(u32, type)
	),
	TP_fast_assign(
		__assign_str(name, dep->name);
		__entry->trb = trb;
		__entry->allocated = dep->allocated_requests;
		__entry->queued = dep->queued_requests;
		__entry->bpl = trb->bpl;
		__entry->bph = trb->bph;
		__entry->size = trb->size;
		__entry->ctrl = trb->ctrl;
		__entry->type = usb_endpoint_type(dep->endpoint.desc);
	),
	TP_printk("%s: %d/%d trb %p buf %08x%08x size %s%d ctrl %08x (%c%c%c%c:%c%c:%s)",
		__get_str(name), __entry->queued, __entry->allocated,
		__entry->trb, __entry->bph, __entry->bpl,
		({char *s;
		int pcm = ((__entry->size >> 24) & 3) + 1;
		switch (__entry->type) {
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
				s = "3x ";
				break;
			}
		default:
			s = "";
		} s; }),
		DWC3_TRB_SIZE_LENGTH(__entry->size), __entry->ctrl,
		__entry->ctrl & DWC3_TRB_CTRL_HWO ? 'H' : 'h',
		__entry->ctrl & DWC3_TRB_CTRL_LST ? 'L' : 'l',
		__entry->ctrl & DWC3_TRB_CTRL_CHN ? 'C' : 'c',
		__entry->ctrl & DWC3_TRB_CTRL_CSP ? 'S' : 's',
		__entry->ctrl & DWC3_TRB_CTRL_ISP_IMI ? 'S' : 's',
		__entry->ctrl & DWC3_TRB_CTRL_IOC ? 'C' : 'c',
		  dwc3_trb_type_string(DWC3_TRBCTL_TYPE(__entry->ctrl))
	)
);

DEFINE_EVENT(dwc3_log_trb, dwc3_prepare_trb,
	TP_PROTO(struct dwc3_ep *dep, struct dwc3_trb *trb),
	TP_ARGS(dep, trb)
);

DEFINE_EVENT(dwc3_log_trb, dwc3_complete_trb,
	TP_PROTO(struct dwc3_ep *dep, struct dwc3_trb *trb),
	TP_ARGS(dep, trb)
);

DECLARE_EVENT_CLASS(dwc3_log_ep,
	TP_PROTO(struct dwc3_ep *dep),
	TP_ARGS(dep),
	TP_STRUCT__entry(
		__string(name, dep->name)
		__field(unsigned, maxpacket)
		__field(unsigned, maxpacket_limit)
		__field(unsigned, max_streams)
		__field(unsigned, maxburst)
		__field(unsigned, flags)
		__field(unsigned, direction)
		__field(u8, trb_enqueue)
		__field(u8, trb_dequeue)
	),
	TP_fast_assign(
		__assign_str(name, dep->name);
		__entry->maxpacket = dep->endpoint.maxpacket;
		__entry->maxpacket_limit = dep->endpoint.maxpacket_limit;
		__entry->max_streams = dep->endpoint.max_streams;
		__entry->maxburst = dep->endpoint.maxburst;
		__entry->flags = dep->flags;
		__entry->direction = dep->direction;
		__entry->trb_enqueue = dep->trb_enqueue;
		__entry->trb_dequeue = dep->trb_dequeue;
	),
	TP_printk("%s: mps %d/%d streams %d burst %d ring %d/%d flags %c:%c%c%c%c%c:%c:%c",
		__get_str(name), __entry->maxpacket,
		__entry->maxpacket_limit, __entry->max_streams,
		__entry->maxburst, __entry->trb_enqueue,
		__entry->trb_dequeue,
		__entry->flags & DWC3_EP_ENABLED ? 'E' : 'e',
		__entry->flags & DWC3_EP_STALL ? 'S' : 's',
		__entry->flags & DWC3_EP_WEDGE ? 'W' : 'w',
		__entry->flags & DWC3_EP_BUSY ? 'B' : 'b',
		__entry->flags & DWC3_EP_PENDING_REQUEST ? 'P' : 'p',
		__entry->flags & DWC3_EP_MISSED_ISOC ? 'M' : 'm',
		__entry->flags & DWC3_EP_END_TRANSFER_PENDING ? 'E' : 'e',
		__entry->direction ? '<' : '>'
	)
);

DEFINE_EVENT(dwc3_log_ep, dwc3_gadget_ep_enable,
	TP_PROTO(struct dwc3_ep *dep),
	TP_ARGS(dep)
);

DEFINE_EVENT(dwc3_log_ep, dwc3_gadget_ep_disable,
	TP_PROTO(struct dwc3_ep *dep),
	TP_ARGS(dep)
);

#endif /* __DWC3_TRACE_H */

/* this part has to be here */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace

#include <trace/define_trace.h>
