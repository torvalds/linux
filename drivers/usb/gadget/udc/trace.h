/**
 * udc.c - Core UDC Framework
 *
 * Copyright (C) 2016 Intel Corporation
 * Author: Felipe Balbi <felipe.balbi@linux.intel.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2  of
 * the License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM gadget

#if !defined(__UDC_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define __UDC_TRACE_H

#include <linux/types.h>
#include <linux/tracepoint.h>
#include <asm/byteorder.h>
#include <linux/usb/gadget.h>

DECLARE_EVENT_CLASS(udc_log_gadget,
	TP_PROTO(struct usb_gadget *g, int ret),
	TP_ARGS(g, ret),
	TP_STRUCT__entry(
		__field(enum usb_device_speed, speed)
		__field(enum usb_device_speed, max_speed)
		__field(enum usb_device_state, state)
		__field(unsigned, mA)
		__field(unsigned, sg_supported)
		__field(unsigned, is_otg)
		__field(unsigned, is_a_peripheral)
		__field(unsigned, b_hnp_enable)
		__field(unsigned, a_hnp_support)
		__field(unsigned, hnp_polling_support)
		__field(unsigned, host_request_flag)
		__field(unsigned, quirk_ep_out_aligned_size)
		__field(unsigned, quirk_altset_not_supp)
		__field(unsigned, quirk_stall_not_supp)
		__field(unsigned, quirk_zlp_not_supp)
		__field(unsigned, is_selfpowered)
		__field(unsigned, deactivated)
		__field(unsigned, connected)
		__field(int, ret)
	),
	TP_fast_assign(
		__entry->speed = g->speed;
		__entry->max_speed = g->max_speed;
		__entry->state = g->state;
		__entry->mA = g->mA;
		__entry->sg_supported = g->sg_supported;
		__entry->is_otg = g->is_otg;
		__entry->is_a_peripheral = g->is_a_peripheral;
		__entry->b_hnp_enable = g->b_hnp_enable;
		__entry->a_hnp_support = g->a_hnp_support;
		__entry->hnp_polling_support = g->hnp_polling_support;
		__entry->host_request_flag = g->host_request_flag;
		__entry->quirk_ep_out_aligned_size = g->quirk_ep_out_aligned_size;
		__entry->quirk_altset_not_supp = g->quirk_altset_not_supp;
		__entry->quirk_stall_not_supp = g->quirk_stall_not_supp;
		__entry->quirk_zlp_not_supp = g->quirk_zlp_not_supp;
		__entry->is_selfpowered = g->is_selfpowered;
		__entry->deactivated = g->deactivated;
		__entry->connected = g->connected;
		__entry->ret = ret;
	),
	TP_printk("speed %d/%d state %d %dmA [%s%s%s%s%s%s%s%s%s%s%s%s%s%s] --> %d",
		__entry->speed, __entry->max_speed, __entry->state, __entry->mA,
		__entry->sg_supported ? "sg:" : "",
		__entry->is_otg ? "OTG:" : "",
		__entry->is_a_peripheral ? "a_peripheral:" : "",
		__entry->b_hnp_enable ? "b_hnp:" : "",
		__entry->a_hnp_support ? "a_hnp:" : "",
		__entry->hnp_polling_support ? "hnp_poll:" : "",
		__entry->host_request_flag ? "hostreq:" : "",
		__entry->quirk_ep_out_aligned_size ? "out_aligned:" : "",
		__entry->quirk_altset_not_supp ? "no_altset:" : "",
		__entry->quirk_stall_not_supp ? "no_stall:" : "",
		__entry->quirk_zlp_not_supp ? "no_zlp" : "",
		__entry->is_selfpowered ? "self-powered:" : "bus-powered:",
		__entry->deactivated ? "deactivated:" : "activated:",
		__entry->connected ? "connected" : "disconnected",
		__entry->ret)
);

DEFINE_EVENT(udc_log_gadget, usb_gadget_frame_number,
	TP_PROTO(struct usb_gadget *g, int ret),
	TP_ARGS(g, ret)
);

DEFINE_EVENT(udc_log_gadget, usb_gadget_wakeup,
	TP_PROTO(struct usb_gadget *g, int ret),
	TP_ARGS(g, ret)
);

DEFINE_EVENT(udc_log_gadget, usb_gadget_set_selfpowered,
	TP_PROTO(struct usb_gadget *g, int ret),
	TP_ARGS(g, ret)
);

DEFINE_EVENT(udc_log_gadget, usb_gadget_clear_selfpowered,
	TP_PROTO(struct usb_gadget *g, int ret),
	TP_ARGS(g, ret)
);

DEFINE_EVENT(udc_log_gadget, usb_gadget_vbus_connect,
	TP_PROTO(struct usb_gadget *g, int ret),
	TP_ARGS(g, ret)
);

DEFINE_EVENT(udc_log_gadget, usb_gadget_vbus_draw,
	TP_PROTO(struct usb_gadget *g, int ret),
	TP_ARGS(g, ret)
);

DEFINE_EVENT(udc_log_gadget, usb_gadget_vbus_disconnect,
	TP_PROTO(struct usb_gadget *g, int ret),
	TP_ARGS(g, ret)
);

DEFINE_EVENT(udc_log_gadget, usb_gadget_connect,
	TP_PROTO(struct usb_gadget *g, int ret),
	TP_ARGS(g, ret)
);

DEFINE_EVENT(udc_log_gadget, usb_gadget_disconnect,
	TP_PROTO(struct usb_gadget *g, int ret),
	TP_ARGS(g, ret)
);

DEFINE_EVENT(udc_log_gadget, usb_gadget_deactivate,
	TP_PROTO(struct usb_gadget *g, int ret),
	TP_ARGS(g, ret)
);

DEFINE_EVENT(udc_log_gadget, usb_gadget_activate,
	TP_PROTO(struct usb_gadget *g, int ret),
	TP_ARGS(g, ret)
);

DECLARE_EVENT_CLASS(udc_log_ep,
	TP_PROTO(struct usb_ep *ep, int ret),
	TP_ARGS(ep, ret),
	TP_STRUCT__entry(
		__dynamic_array(char, name, UDC_TRACE_STR_MAX)
		__field(unsigned, maxpacket)
		__field(unsigned, maxpacket_limit)
		__field(unsigned, max_streams)
		__field(unsigned, mult)
		__field(unsigned, maxburst)
		__field(u8, address)
		__field(bool, claimed)
		__field(bool, enabled)
		__field(int, ret)
	),
	TP_fast_assign(
		snprintf(__get_str(name), UDC_TRACE_STR_MAX, "%s", ep->name);
		__entry->maxpacket = ep->maxpacket;
		__entry->maxpacket_limit = ep->maxpacket_limit;
		__entry->max_streams = ep->max_streams;
		__entry->mult = ep->mult;
		__entry->maxburst = ep->maxburst;
		__entry->address = ep->address,
		__entry->claimed = ep->claimed;
		__entry->enabled = ep->enabled;
		__entry->ret = ret;
	),
	TP_printk("%s: mps %d/%d streams %d mult %d burst %d addr %02x %s%s --> %d",
		__get_str(name), __entry->maxpacket, __entry->maxpacket_limit,
		__entry->max_streams, __entry->mult, __entry->maxburst,
		__entry->address, __entry->claimed ? "claimed:" : "released:",
		__entry->enabled ? "enabled" : "disabled", ret)
);

DEFINE_EVENT(udc_log_ep, usb_ep_set_maxpacket_limit,
	TP_PROTO(struct usb_ep *ep, int ret),
	TP_ARGS(ep, ret)
);

DEFINE_EVENT(udc_log_ep, usb_ep_enable,
	TP_PROTO(struct usb_ep *ep, int ret),
	TP_ARGS(ep, ret)
);

DEFINE_EVENT(udc_log_ep, usb_ep_disable,
	TP_PROTO(struct usb_ep *ep, int ret),
	TP_ARGS(ep, ret)
);

DEFINE_EVENT(udc_log_ep, usb_ep_set_halt,
	TP_PROTO(struct usb_ep *ep, int ret),
	TP_ARGS(ep, ret)
);

DEFINE_EVENT(udc_log_ep, usb_ep_clear_halt,
	TP_PROTO(struct usb_ep *ep, int ret),
	TP_ARGS(ep, ret)
);

DEFINE_EVENT(udc_log_ep, usb_ep_set_wedge,
	TP_PROTO(struct usb_ep *ep, int ret),
	TP_ARGS(ep, ret)
);

DEFINE_EVENT(udc_log_ep, usb_ep_fifo_status,
	TP_PROTO(struct usb_ep *ep, int ret),
	TP_ARGS(ep, ret)
);

DEFINE_EVENT(udc_log_ep, usb_ep_fifo_flush,
	TP_PROTO(struct usb_ep *ep, int ret),
	TP_ARGS(ep, ret)
);

DECLARE_EVENT_CLASS(udc_log_req,
	TP_PROTO(struct usb_ep *ep, struct usb_request *req, int ret),
	TP_ARGS(ep, req, ret),
	TP_STRUCT__entry(
		__dynamic_array(char, name, UDC_TRACE_STR_MAX)
		__field(unsigned, length)
		__field(unsigned, actual)
		__field(unsigned, num_sgs)
		__field(unsigned, num_mapped_sgs)
		__field(unsigned, stream_id)
		__field(unsigned, no_interrupt)
		__field(unsigned, zero)
		__field(unsigned, short_not_ok)
		__field(int, status)
		__field(int, ret)
	),
	TP_fast_assign(
		snprintf(__get_str(name), UDC_TRACE_STR_MAX, "%s", ep->name);
		__entry->length = req->length;
		__entry->actual = req->actual;
		__entry->num_sgs = req->num_sgs;
		__entry->num_mapped_sgs = req->num_mapped_sgs;
		__entry->stream_id = req->stream_id;
		__entry->no_interrupt = req->no_interrupt;
		__entry->zero = req->zero;
		__entry->short_not_ok = req->short_not_ok;
		__entry->status = req->status;
		__entry->ret = ret;
	),
	TP_printk("%s: length %d/%d sgs %d/%d stream %d %s%s%s status %d --> %d",
		__get_str(name), __entry->actual, __entry->length,
		__entry->num_mapped_sgs, __entry->num_sgs, __entry->stream_id,
		__entry->zero ? "Z" : "z",
		__entry->short_not_ok ? "S" : "s",
		__entry->no_interrupt ? "i" : "I",
		__entry->status, __entry->ret
	)
);

DEFINE_EVENT(udc_log_req, usb_ep_alloc_request,
	TP_PROTO(struct usb_ep *ep, struct usb_request *req, int ret),
	TP_ARGS(ep, req, ret)
);

DEFINE_EVENT(udc_log_req, usb_ep_free_request,
	TP_PROTO(struct usb_ep *ep, struct usb_request *req, int ret),
	TP_ARGS(ep, req, ret)
);

DEFINE_EVENT(udc_log_req, usb_ep_queue,
	TP_PROTO(struct usb_ep *ep, struct usb_request *req, int ret),
	TP_ARGS(ep, req, ret)
);

DEFINE_EVENT(udc_log_req, usb_ep_dequeue,
	TP_PROTO(struct usb_ep *ep, struct usb_request *req, int ret),
	TP_ARGS(ep, req, ret)
);

DEFINE_EVENT(udc_log_req, usb_gadget_giveback_request,
	TP_PROTO(struct usb_ep *ep, struct usb_request *req, int ret),
	TP_ARGS(ep, req, ret)
);

#endif /* __UDC_TRACE_H */

/* this part has to be here */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace

#include <trace/define_trace.h>
