/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_INCLUDE_PATH ../../drivers/dma-buf
#define TRACE_SYSTEM sync_trace

#if !defined(_TRACE_SYNC_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_SYNC_H

#include "sync_debug.h"
#include <linux/tracepoint.h>

TRACE_EVENT(sync_timeline,
	TP_PROTO(struct sync_timeline *timeline),

	TP_ARGS(timeline),

	TP_STRUCT__entry(
			__string(name, timeline->name)
			__field(u32, value)
	),

	TP_fast_assign(
			__assign_str(name);
			__entry->value = timeline->value;
	),

	TP_printk("name=%s value=%d", __get_str(name), __entry->value)
);

#endif /* if !defined(_TRACE_SYNC_H) || defined(TRACE_HEADER_MULTI_READ) */

/* This part must be outside protection */
#include <trace/define_trace.h>
