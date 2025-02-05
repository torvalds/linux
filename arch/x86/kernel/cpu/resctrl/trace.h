/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM resctrl

#if !defined(_TRACE_RESCTRL_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_RESCTRL_H

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

TRACE_EVENT(mon_llc_occupancy_limbo,
	    TP_PROTO(u32 ctrl_hw_id, u32 mon_hw_id, int domain_id, u64 llc_occupancy_bytes),
	    TP_ARGS(ctrl_hw_id, mon_hw_id, domain_id, llc_occupancy_bytes),
	    TP_STRUCT__entry(__field(u32, ctrl_hw_id)
			     __field(u32, mon_hw_id)
			     __field(int, domain_id)
			     __field(u64, llc_occupancy_bytes)),
	    TP_fast_assign(__entry->ctrl_hw_id = ctrl_hw_id;
			   __entry->mon_hw_id = mon_hw_id;
			   __entry->domain_id = domain_id;
			   __entry->llc_occupancy_bytes = llc_occupancy_bytes;),
	    TP_printk("ctrl_hw_id=%u mon_hw_id=%u domain_id=%d llc_occupancy_bytes=%llu",
		      __entry->ctrl_hw_id, __entry->mon_hw_id, __entry->domain_id,
		      __entry->llc_occupancy_bytes)
	   );

#endif /* _TRACE_RESCTRL_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE trace
#include <trace/define_trace.h>
