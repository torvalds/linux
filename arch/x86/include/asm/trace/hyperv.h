#undef TRACE_SYSTEM
#define TRACE_SYSTEM hyperv

#if !defined(_TRACE_HYPERV_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HYPERV_H

#include <linux/tracepoint.h>

#if IS_ENABLED(CONFIG_HYPERV)

TRACE_EVENT(hyperv_mmu_flush_tlb_others,
	    TP_PROTO(const struct cpumask *cpus,
		     const struct flush_tlb_info *info),
	    TP_ARGS(cpus, info),
	    TP_STRUCT__entry(
		    __field(unsigned int, ncpus)
		    __field(struct mm_struct *, mm)
		    __field(unsigned long, addr)
		    __field(unsigned long, end)
		    ),
	    TP_fast_assign(__entry->ncpus = cpumask_weight(cpus);
			   __entry->mm = info->mm;
			   __entry->addr = info->start;
			   __entry->end = info->end;
		    ),
	    TP_printk("ncpus %d mm %p addr %lx, end %lx",
		      __entry->ncpus, __entry->mm,
		      __entry->addr, __entry->end)
	);

#endif /* CONFIG_HYPERV */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH asm/trace/
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE hyperv
#endif /* _TRACE_HYPERV_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
