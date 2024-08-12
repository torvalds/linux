/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Tracepoint header for hiperdispatch
 *
 * Copyright IBM Corp. 2024
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM s390

#if !defined(_TRACE_S390_HIPERDISPATCH_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_S390_HIPERDISPATCH_H

#include <linux/tracepoint.h>

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE

#define TRACE_INCLUDE_PATH asm/trace
#define TRACE_INCLUDE_FILE hiperdispatch

TRACE_EVENT(s390_hd_work_fn,
	    TP_PROTO(int steal_time_percentage,
		     int entitled_core_count,
		     int highcap_core_count),
	    TP_ARGS(steal_time_percentage,
		    entitled_core_count,
		    highcap_core_count),
	    TP_STRUCT__entry(__field(int, steal_time_percentage)
			     __field(int, entitled_core_count)
			     __field(int, highcap_core_count)),
	    TP_fast_assign(__entry->steal_time_percentage = steal_time_percentage;
			   __entry->entitled_core_count = entitled_core_count;
			   __entry->highcap_core_count = highcap_core_count;),
	    TP_printk("steal: %d entitled_core_count: %d highcap_core_count: %d",
		      __entry->steal_time_percentage,
		      __entry->entitled_core_count,
		      __entry->highcap_core_count)
);

TRACE_EVENT(s390_hd_rebuild_domains,
	    TP_PROTO(int current_highcap_core_count,
		     int new_highcap_core_count),
	    TP_ARGS(current_highcap_core_count,
		    new_highcap_core_count),
	    TP_STRUCT__entry(__field(int, current_highcap_core_count)
			     __field(int, new_highcap_core_count)),
	    TP_fast_assign(__entry->current_highcap_core_count = current_highcap_core_count;
			   __entry->new_highcap_core_count = new_highcap_core_count),
	    TP_printk("change highcap_core_count: %u -> %u",
		      __entry->current_highcap_core_count,
		      __entry->new_highcap_core_count)
);

#endif /* _TRACE_S390_HIPERDISPATCH_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
