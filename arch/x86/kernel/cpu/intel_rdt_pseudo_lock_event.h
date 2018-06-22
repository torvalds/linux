/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM resctrl

#if !defined(_TRACE_PSEUDO_LOCK_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_PSEUDO_LOCK_H

#include <linux/tracepoint.h>

TRACE_EVENT(pseudo_lock_mem_latency,
	    TP_PROTO(u32 latency),
	    TP_ARGS(latency),
	    TP_STRUCT__entry(__field(u32, latency)),
	    TP_fast_assign(__entry->latency = latency),
	    TP_printk("latency=%u", __entry->latency)
	   );

#endif /* _TRACE_PSEUDO_LOCK_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE intel_rdt_pseudo_lock_event
#include <trace/define_trace.h>
