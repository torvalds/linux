#undef TRACE_SYSTEM
#define TRACE_SYSTEM hyperv

#if !defined(_HV_TRACE_BALLOON_H) || defined(TRACE_HEADER_MULTI_READ)
#define _HV_TRACE_BALLOON_H

#include <linux/tracepoint.h>

TRACE_EVENT(balloon_status,
	    TP_PROTO(u64 available, u64 committed,
		     unsigned long vm_memory_committed,
		     unsigned long pages_ballooned,
		     unsigned long pages_added,
		     unsigned long pages_onlined),
	    TP_ARGS(available, committed, vm_memory_committed,
		    pages_ballooned, pages_added, pages_onlined),
	    TP_STRUCT__entry(
		    __field(u64, available)
		    __field(u64, committed)
		    __field(unsigned long, vm_memory_committed)
		    __field(unsigned long, pages_ballooned)
		    __field(unsigned long, pages_added)
		    __field(unsigned long, pages_onlined)
		    ),
	    TP_fast_assign(
		    __entry->available = available;
		    __entry->committed = committed;
		    __entry->vm_memory_committed = vm_memory_committed;
		    __entry->pages_ballooned = pages_ballooned;
		    __entry->pages_added = pages_added;
		    __entry->pages_onlined = pages_onlined;
		    ),
	    TP_printk("available %lld, committed %lld; vm_memory_committed %ld;"
		      " pages_ballooned %ld, pages_added %ld, pages_onlined %ld",
		      __entry->available, __entry->committed,
		      __entry->vm_memory_committed, __entry->pages_ballooned,
		      __entry->pages_added, __entry->pages_onlined
		    )
	);

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE hv_trace_balloon
#endif /* _HV_TRACE_BALLOON_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
