/*
 * xHCI host controller driver
 *
 * Copyright (C) 2013 Xenia Ragiadakou
 *
 * Author: Xenia Ragiadakou
 * Email : burzalodowa@gmail.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM xhci-hcd

#if !defined(__XHCI_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define __XHCI_TRACE_H

#include <linux/tracepoint.h>

#define XHCI_MSG_MAX	500

DECLARE_EVENT_CLASS(xhci_log_msg,
	TP_PROTO(struct va_format *vaf),
	TP_ARGS(vaf),
	TP_STRUCT__entry(__dynamic_array(char, msg, XHCI_MSG_MAX)),
	TP_fast_assign(
		vsnprintf(__get_str(msg), XHCI_MSG_MAX, vaf->fmt, *vaf->va);
	),
	TP_printk("%s", __get_str(msg))
);

DEFINE_EVENT(xhci_log_msg, xhci_dbg_address,
	TP_PROTO(struct va_format *vaf),
	TP_ARGS(vaf)
);

DEFINE_EVENT(xhci_log_msg, xhci_dbg_context_change,
	TP_PROTO(struct va_format *vaf),
	TP_ARGS(vaf)
);

#endif /* __XHCI_TRACE_H */

/* this part must be outside header guard */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE xhci-trace

#include <trace/define_trace.h>
