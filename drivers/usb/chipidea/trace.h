/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Trace support header file for device mode
 *
 * Copyright (C) 2020 NXP
 *
 * Author: Peter Chen <peter.chen@nxp.com>
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM chipidea

#if !defined(__LINUX_CHIPIDEA_TRACE) || defined(TRACE_HEADER_MULTI_READ)
#define __LINUX_CHIPIDEA_TRACE

#include <linux/types.h>
#include <linux/tracepoint.h>
#include <linux/usb/chipidea.h>
#include "ci.h"
#include "udc.h"

#define CHIPIDEA_MSG_MAX	500

void ci_log(struct ci_hdrc *ci, const char *fmt, ...);

TRACE_EVENT(ci_log,
	TP_PROTO(struct ci_hdrc *ci, struct va_format *vaf),
	TP_ARGS(ci, vaf),
	TP_STRUCT__entry(
		__string(name, dev_name(ci->dev))
		__vstring(msg, vaf->fmt, vaf->va)
	),
	TP_fast_assign(
		__assign_str(name);
		__assign_vstr(msg, vaf->fmt, vaf->va);
	),
	TP_printk("%s: %s", __get_str(name), __get_str(msg))
);

DECLARE_EVENT_CLASS(ci_log_trb,
	TP_PROTO(struct ci_hw_ep *hwep, struct ci_hw_req *hwreq, struct td_node *td),
	TP_ARGS(hwep, hwreq, td),
	TP_STRUCT__entry(
		__string(name, hwep->name)
		__field(struct td_node *, td)
		__field(struct usb_request *, req)
		__field(dma_addr_t, dma)
		__field(s32, td_remaining_size)
		__field(u32, next)
		__field(u32, token)
		__field(u32, type)
	),
	TP_fast_assign(
		__assign_str(name);
		__entry->req = &hwreq->req;
		__entry->td = td;
		__entry->dma = td->dma;
		__entry->td_remaining_size = td->td_remaining_size;
		__entry->next = le32_to_cpu(td->ptr->next);
		__entry->token = le32_to_cpu(td->ptr->token);
		__entry->type = usb_endpoint_type(hwep->ep.desc);
	),
	TP_printk("%s: req: %p, td: %p, td_dma_address: %pad, remaining_size: %d, "
	       "next: %x, total bytes: %d, status: %lx",
		__get_str(name), __entry->req, __entry->td, &__entry->dma,
		__entry->td_remaining_size, __entry->next,
		(int)((__entry->token & TD_TOTAL_BYTES) >> __ffs(TD_TOTAL_BYTES)),
		__entry->token & TD_STATUS
	)
);

DEFINE_EVENT(ci_log_trb, ci_prepare_td,
	TP_PROTO(struct ci_hw_ep *hwep, struct ci_hw_req *hwreq, struct td_node *td),
	TP_ARGS(hwep, hwreq, td)
);

DEFINE_EVENT(ci_log_trb, ci_complete_td,
	TP_PROTO(struct ci_hw_ep *hwep, struct ci_hw_req *hwreq, struct td_node *td),
	TP_ARGS(hwep, hwreq, td)
);

#endif /* __LINUX_CHIPIDEA_TRACE */

/* this part must be outside header guard */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace

#include <trace/define_trace.h>
