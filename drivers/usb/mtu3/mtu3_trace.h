/* SPDX-License-Identifier: GPL-2.0 */
/**
 * mtu3_trace.h - trace support
 *
 * Copyright (C) 2019 MediaTek Inc.
 *
 * Author: Chunfeng Yun <chunfeng.yun@mediatek.com>
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM mtu3

#if !defined(__MTU3_TRACE_H__) || defined(TRACE_HEADER_MULTI_READ)
#define __MTU3_TRACE_H__

#include <linux/types.h>
#include <linux/tracepoint.h>

#include "mtu3.h"

#define MTU3_MSG_MAX	256

TRACE_EVENT(mtu3_log,
	TP_PROTO(struct device *dev, struct va_format *vaf),
	TP_ARGS(dev, vaf),
	TP_STRUCT__entry(
		__string(name, dev_name(dev))
		__dynamic_array(char, msg, MTU3_MSG_MAX)
	),
	TP_fast_assign(
		__assign_str(name, dev_name(dev));
		vsnprintf(__get_str(msg), MTU3_MSG_MAX, vaf->fmt, *vaf->va);
	),
	TP_printk("%s: %s", __get_str(name), __get_str(msg))
);

TRACE_EVENT(mtu3_u3_ltssm_isr,
	TP_PROTO(u32 intr),
	TP_ARGS(intr),
	TP_STRUCT__entry(
		__field(u32, intr)
	),
	TP_fast_assign(
		__entry->intr = intr;
	),
	TP_printk("(%08x) %s %s %s %s %s %s", __entry->intr,
		__entry->intr & HOT_RST_INTR ? "HOT_RST" : "",
		__entry->intr & WARM_RST_INTR ? "WARM_RST" : "",
		__entry->intr & ENTER_U3_INTR ? "ENT_U3" : "",
		__entry->intr & EXIT_U3_INTR ? "EXIT_U3" : "",
		__entry->intr & VBUS_RISE_INTR ? "VBUS_RISE" : "",
		__entry->intr & VBUS_FALL_INTR ? "VBUS_FALL" : ""
	)
);

TRACE_EVENT(mtu3_u2_common_isr,
	TP_PROTO(u32 intr),
	TP_ARGS(intr),
	TP_STRUCT__entry(
		__field(u32, intr)
	),
	TP_fast_assign(
		__entry->intr = intr;
	),
	TP_printk("(%08x) %s %s %s", __entry->intr,
		__entry->intr & SUSPEND_INTR ? "SUSPEND" : "",
		__entry->intr & RESUME_INTR ? "RESUME" : "",
		__entry->intr & RESET_INTR ? "RESET" : ""
	)
);

TRACE_EVENT(mtu3_qmu_isr,
	TP_PROTO(u32 done_intr, u32 exp_intr),
	TP_ARGS(done_intr, exp_intr),
	TP_STRUCT__entry(
		__field(u32, done_intr)
		__field(u32, exp_intr)
	),
	TP_fast_assign(
		__entry->done_intr = done_intr;
		__entry->exp_intr = exp_intr;
	),
	TP_printk("done (tx %04x, rx %04x), exp (%08x)",
		__entry->done_intr & 0xffff,
		__entry->done_intr >> 16,
		__entry->exp_intr
	)
);

DECLARE_EVENT_CLASS(mtu3_log_setup,
	TP_PROTO(struct usb_ctrlrequest *setup),
	TP_ARGS(setup),
	TP_STRUCT__entry(
		__field(__u8, bRequestType)
		__field(__u8, bRequest)
		__field(__u16, wValue)
		__field(__u16, wIndex)
		__field(__u16, wLength)
	),
	TP_fast_assign(
		__entry->bRequestType = setup->bRequestType;
		__entry->bRequest = setup->bRequest;
		__entry->wValue = le16_to_cpu(setup->wValue);
		__entry->wIndex = le16_to_cpu(setup->wIndex);
		__entry->wLength = le16_to_cpu(setup->wLength);
	),
	TP_printk("setup - %02x %02x %04x %04x %04x",
		__entry->bRequestType, __entry->bRequest,
		__entry->wValue, __entry->wIndex, __entry->wLength
	)
);

DEFINE_EVENT(mtu3_log_setup, mtu3_handle_setup,
	TP_PROTO(struct usb_ctrlrequest *setup),
	TP_ARGS(setup)
);

DECLARE_EVENT_CLASS(mtu3_log_request,
	TP_PROTO(struct mtu3_request *mreq),
	TP_ARGS(mreq),
	TP_STRUCT__entry(
		__string(name, mreq->mep->name)
		__field(struct mtu3_request *, mreq)
		__field(struct qmu_gpd *, gpd)
		__field(unsigned int, actual)
		__field(unsigned int, length)
		__field(int, status)
		__field(int, zero)
		__field(int, no_interrupt)
	),
	TP_fast_assign(
		__assign_str(name, mreq->mep->name);
		__entry->mreq = mreq;
		__entry->gpd = mreq->gpd;
		__entry->actual = mreq->request.actual;
		__entry->length = mreq->request.length;
		__entry->status = mreq->request.status;
		__entry->zero = mreq->request.zero;
		__entry->no_interrupt = mreq->request.no_interrupt;
	),
	TP_printk("%s: req %p gpd %p len %u/%u %s%s --> %d",
		__get_str(name), __entry->mreq, __entry->gpd,
		__entry->actual, __entry->length,
		__entry->zero ? "Z" : "z",
		__entry->no_interrupt ? "i" : "I",
		__entry->status
	)
);

DEFINE_EVENT(mtu3_log_request, mtu3_alloc_request,
	TP_PROTO(struct mtu3_request *req),
	TP_ARGS(req)
);

DEFINE_EVENT(mtu3_log_request, mtu3_free_request,
	TP_PROTO(struct mtu3_request *req),
	TP_ARGS(req)
);

DEFINE_EVENT(mtu3_log_request, mtu3_gadget_queue,
	TP_PROTO(struct mtu3_request *req),
	TP_ARGS(req)
);

DEFINE_EVENT(mtu3_log_request, mtu3_gadget_dequeue,
	TP_PROTO(struct mtu3_request *req),
	TP_ARGS(req)
);

DEFINE_EVENT(mtu3_log_request, mtu3_req_complete,
	TP_PROTO(struct mtu3_request *req),
	TP_ARGS(req)
);

DECLARE_EVENT_CLASS(mtu3_log_gpd,
	TP_PROTO(struct mtu3_ep *mep, struct qmu_gpd *gpd),
	TP_ARGS(mep, gpd),
	TP_STRUCT__entry(
		__string(name, mep->name)
		__field(struct qmu_gpd *, gpd)
		__field(u32, dw0)
		__field(u32, dw1)
		__field(u32, dw2)
		__field(u32, dw3)
	),
	TP_fast_assign(
		__assign_str(name, mep->name);
		__entry->gpd = gpd;
		__entry->dw0 = le32_to_cpu(gpd->dw0_info);
		__entry->dw1 = le32_to_cpu(gpd->next_gpd);
		__entry->dw2 = le32_to_cpu(gpd->buffer);
		__entry->dw3 = le32_to_cpu(gpd->dw3_info);
	),
	TP_printk("%s: gpd %p - %08x %08x %08x %08x",
		__get_str(name), __entry->gpd,
		__entry->dw0, __entry->dw1,
		__entry->dw2, __entry->dw3
	)
);

DEFINE_EVENT(mtu3_log_gpd, mtu3_prepare_gpd,
	TP_PROTO(struct mtu3_ep *mep, struct qmu_gpd *gpd),
	TP_ARGS(mep, gpd)
);

DEFINE_EVENT(mtu3_log_gpd, mtu3_complete_gpd,
	TP_PROTO(struct mtu3_ep *mep, struct qmu_gpd *gpd),
	TP_ARGS(mep, gpd)
);

DEFINE_EVENT(mtu3_log_gpd, mtu3_zlp_exp_gpd,
	TP_PROTO(struct mtu3_ep *mep, struct qmu_gpd *gpd),
	TP_ARGS(mep, gpd)
);

DECLARE_EVENT_CLASS(mtu3_log_ep,
	TP_PROTO(struct mtu3_ep *mep),
	TP_ARGS(mep),
	TP_STRUCT__entry(
		__string(name, mep->name)
		__field(unsigned int, type)
		__field(unsigned int, slot)
		__field(unsigned int, maxp)
		__field(unsigned int, mult)
		__field(unsigned int, maxburst)
		__field(unsigned int, flags)
		__field(unsigned int, direction)
		__field(struct mtu3_gpd_ring *, gpd_ring)
	),
	TP_fast_assign(
		__assign_str(name, mep->name);
		__entry->type = mep->type;
		__entry->slot = mep->slot;
		__entry->maxp = mep->ep.maxpacket;
		__entry->mult = mep->ep.mult;
		__entry->maxburst = mep->ep.maxburst;
		__entry->flags = mep->flags;
		__entry->direction = mep->is_in;
		__entry->gpd_ring = &mep->gpd_ring;
	),
	TP_printk("%s: type %d maxp %d slot %d mult %d burst %d ring %p/%pad flags %c:%c%c%c:%c",
		__get_str(name), __entry->type,
		__entry->maxp, __entry->slot,
		__entry->mult, __entry->maxburst,
		__entry->gpd_ring, &__entry->gpd_ring->dma,
		__entry->flags & MTU3_EP_ENABLED ? 'E' : 'e',
		__entry->flags & MTU3_EP_STALL ? 'S' : 's',
		__entry->flags & MTU3_EP_WEDGE ? 'W' : 'w',
		__entry->flags & MTU3_EP_BUSY ? 'B' : 'b',
		__entry->direction ? '<' : '>'
	)
);

DEFINE_EVENT(mtu3_log_ep, mtu3_gadget_ep_enable,
	TP_PROTO(struct mtu3_ep *mep),
	TP_ARGS(mep)
);

DEFINE_EVENT(mtu3_log_ep, mtu3_gadget_ep_disable,
	TP_PROTO(struct mtu3_ep *mep),
	TP_ARGS(mep)
);

DEFINE_EVENT(mtu3_log_ep, mtu3_gadget_ep_set_halt,
	TP_PROTO(struct mtu3_ep *mep),
	TP_ARGS(mep)
);

#endif /* __MTU3_TRACE_H__ */

/* this part has to be here */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE mtu3_trace

#include <trace/define_trace.h>
