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

TRACE_EVENT(hyperv_nested_flush_guest_mapping,
	    TP_PROTO(u64 as, int ret),
	    TP_ARGS(as, ret),

	    TP_STRUCT__entry(
		    __field(u64, as)
		    __field(int, ret)
		    ),
	    TP_fast_assign(__entry->as = as;
			   __entry->ret = ret;
		    ),
	    TP_printk("address space %llx ret %d", __entry->as, __entry->ret)
	);

TRACE_EVENT(hyperv_nested_flush_guest_mapping_range,
	    TP_PROTO(u64 as, int ret),
	    TP_ARGS(as, ret),

	    TP_STRUCT__entry(
		    __field(u64, as)
		    __field(int, ret)
		    ),
	    TP_fast_assign(__entry->as = as;
			   __entry->ret = ret;
		    ),
	    TP_printk("address space %llx ret %d", __entry->as, __entry->ret)
	);

TRACE_EVENT(hyperv_send_ipi_mask,
	    TP_PROTO(const struct cpumask *cpus,
		     int vector),
	    TP_ARGS(cpus, vector),
	    TP_STRUCT__entry(
		    __field(unsigned int, ncpus)
		    __field(int, vector)
		    ),
	    TP_fast_assign(__entry->ncpus = cpumask_weight(cpus);
			   __entry->vector = vector;
		    ),
	    TP_printk("ncpus %d vector %x",
		      __entry->ncpus, __entry->vector)
	);

#endif /* CONFIG_HYPERV */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH asm/trace/
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE hyperv
#endif /* _TRACE_HYPERV_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
