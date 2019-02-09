/**
 * trace.h - DesignWare USB3 DRD Controller Trace Support
 *
 * Copyright (C) 2014 Texas Instruments Incorporated - http://www.ti.com
 *
 * Author: Felipe Balbi <balbi@ti.com>
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

#undef TRACE_SYSTEM
#define TRACE_SYSTEM dwc3

#if !defined(__DWC3_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define __DWC3_TRACE_H

#include <linux/types.h>
#include <linux/tracepoint.h>
#include <asm/byteorder.h>
#include "core.h"
#include "debug.h"

DECLARE_EVENT_CLASS(dwc3_log_msg,
	TP_PROTO(struct va_format *vaf),
	TP_ARGS(vaf),
	TP_STRUCT__entry(__dynamic_array(char, msg, DWC3_MSG_MAX)),
	TP_fast_assign(
		vsnprintf(__get_str(msg), DWC3_MSG_MAX, vaf->fmt, *vaf->va);
	),
	TP_printk("%s", __get_str(msg))
);

DEFINE_EVENT(dwc3_log_msg, dwc3_readl,
	TP_PROTO(struct va_format *vaf),
	TP_ARGS(vaf)
);

DEFINE_EVENT(dwc3_log_msg, dwc3_writel,
	TP_PROTO(struct va_format *vaf),
	TP_ARGS(vaf)
);

DEFINE_EVENT(dwc3_log_msg, dwc3_gadget,
	TP_PROTO(struct va_format *vaf),
	TP_ARGS(vaf)
);

DEFINE_EVENT(dwc3_log_msg, dwc3_core,
	TP_PROTO(struct va_format *vaf),
	TP_ARGS(vaf)
);

DEFINE_EVENT(dwc3_log_msg, dwc3_ep0,
	TP_PROTO(struct va_format *vaf),
	TP_ARGS(vaf)
);

DECLARE_EVENT_CLASS(dwc3_log_event,
	TP_PROTO(u32 event),
	TP_ARGS(event),
	TP_STRUCT__entry(
		__field(u32, event)
	),
	TP_fast_assign(
		__entry->event = event;
	),
	TP_printk("event %08x", __entry->event)
);

DEFINE_EVENT(dwc3_log_event, dwc3_event,
	TP_PROTO(u32 event),
	TP_ARGS(event)
);

DECLARE_EVENT_CLASS(dwc3_log_ctrl,
	TP_PROTO(struct usb_ctrlrequest *ctrl),
	TP_ARGS(ctrl),
	TP_STRUCT__entry(
		__field(__u8, bRequestType)
		__field(__u8, bRequest)
		__field(__le16, wValue)
		__field(__le16, wIndex)
		__field(__le16, wLength)
	),
	TP_fast_assign(
		__entry->bRequestType = ctrl->bRequestType;
		__entry->bRequest = ctrl->bRequest;
		__entry->wValue = ctrl->wValue;
		__entry->wIndex = ctrl->wIndex;
		__entry->wLength = ctrl->wLength;
	),
	TP_printk("bRequestType %02x bRequest %02x wValue %04x wIndex %04x wLength %d",
		__entry->bRequestType, __entry->bRequest,
		le16_to_cpu(__entry->wValue), le16_to_cpu(__entry->wIndex),
		le16_to_cpu(__entry->wLength)
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
		__dynamic_array(char, name, DWC3_MSG_MAX)
		__field(struct dwc3_request *, req)
		__field(unsigned, actual)
		__field(unsigned, length)
		__field(int, status)
	),
	TP_fast_assign(
		snprintf(__get_str(name), DWC3_MSG_MAX, "%s", req->dep->name);
		__entry->req = req;
		__entry->actual = req->request.actual;
		__entry->length = req->request.length;
		__entry->status = req->request.status;
	),
	TP_printk("%s: req %p length %u/%u ==> %d",
		__get_str(name), __entry->req, __entry->actual, __entry->length,
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
	TP_PROTO(unsigned int cmd, u32 param),
	TP_ARGS(cmd, param),
	TP_STRUCT__entry(
		__field(unsigned int, cmd)
		__field(u32, param)
	),
	TP_fast_assign(
		__entry->cmd = cmd;
		__entry->param = param;
	),
	TP_printk("cmd '%s' [%d] param %08x",
		dwc3_gadget_generic_cmd_string(__entry->cmd),
		__entry->cmd, __entry->param
	)
);

DEFINE_EVENT(dwc3_log_generic_cmd, dwc3_gadget_generic_cmd,
	TP_PROTO(unsigned int cmd, u32 param),
	TP_ARGS(cmd, param)
);

DECLARE_EVENT_CLASS(dwc3_log_gadget_ep_cmd,
	TP_PROTO(struct dwc3_ep *dep, unsigned int cmd,
		struct dwc3_gadget_ep_cmd_params *params),
	TP_ARGS(dep, cmd, params),
	TP_STRUCT__entry(
		__dynamic_array(char, name, DWC3_MSG_MAX)
		__field(unsigned int, cmd)
		__field(u32, param0)
		__field(u32, param1)
		__field(u32, param2)
	),
	TP_fast_assign(
		snprintf(__get_str(name), DWC3_MSG_MAX, "%s", dep->name);
		__entry->cmd = cmd;
		__entry->param0 = params->param0;
		__entry->param1 = params->param1;
		__entry->param2 = params->param2;
	),
	TP_printk("%s: cmd '%s' [%d] params %08x %08x %08x",
		__get_str(name), dwc3_gadget_ep_cmd_string(__entry->cmd),
		__entry->cmd, __entry->param0,
		__entry->param1, __entry->param2
	)
);

DEFINE_EVENT(dwc3_log_gadget_ep_cmd, dwc3_gadget_ep_cmd,
	TP_PROTO(struct dwc3_ep *dep, unsigned int cmd,
		struct dwc3_gadget_ep_cmd_params *params),
	TP_ARGS(dep, cmd, params)
);

DECLARE_EVENT_CLASS(dwc3_log_trb,
	TP_PROTO(struct dwc3_ep *dep, struct dwc3_trb *trb),
	TP_ARGS(dep, trb),
	TP_STRUCT__entry(
		__dynamic_array(char, name, DWC3_MSG_MAX)
		__field(struct dwc3_trb *, trb)
		__field(u32, bpl)
		__field(u32, bph)
		__field(u32, size)
		__field(u32, ctrl)
	),
	TP_fast_assign(
		snprintf(__get_str(name), DWC3_MSG_MAX, "%s", dep->name);
		__entry->trb = trb;
		__entry->bpl = trb->bpl;
		__entry->bph = trb->bph;
		__entry->size = trb->size;
		__entry->ctrl = trb->ctrl;
	),
	TP_printk("%s: trb %p bph %08x bpl %08x size %08x ctrl %08x",
		__get_str(name), __entry->trb, __entry->bph, __entry->bpl,
		__entry->size, __entry->ctrl
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

#endif /* __DWC3_TRACE_H */

/* this part has to be here */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace

#include <trace/define_trace.h>
