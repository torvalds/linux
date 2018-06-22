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

TRACE_EVENT(pseudo_lock_l2,
	    TP_PROTO(u64 l2_hits, u64 l2_miss),
	    TP_ARGS(l2_hits, l2_miss),
	    TP_STRUCT__entry(__field(u64, l2_hits)
			     __field(u64, l2_miss)),
	    TP_fast_assign(__entry->l2_hits = l2_hits;
			   __entry->l2_miss = l2_miss;),
	    TP_printk("hits=%llu miss=%llu",
		      __entry->l2_hits, __entry->l2_miss));

TRACE_EVENT(pseudo_lock_l3,
	    TP_PROTO(u64 l3_hits, u64 l3_miss),
	    TP_ARGS(l3_hits, l3_miss),
	    TP_STRUCT__entry(__field(u64, l3_hits)
			     __field(u64, l3_miss)),
	    TP_fast_assign(__entry->l3_hits = l3_hits;
			   __entry->l3_miss = l3_miss;),
	    TP_printk("hits=%llu miss=%llu",
		      __entry->l3_hits, __entry->l3_miss));

#endif /* _TRACE_PSEUDO_LOCK_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE intel_rdt_pseudo_lock_event
#include <trace/define_trace.h>
