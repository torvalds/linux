#undef TRACE_SYSTEM
#define TRACE_SYSTEM msr

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE msr-trace

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH asm/

#if !defined(_TRACE_MSR_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_MSR_H

#include <linux/tracepoint.h>

/*
 * Tracing for x86 model specific registers. Directly maps to the
 * RDMSR/WRMSR instructions.
 */

DECLARE_EVENT_CLASS(msr_trace_class,
	    TP_PROTO(unsigned msr, u64 val, int failed),
	    TP_ARGS(msr, val, failed),
	    TP_STRUCT__entry(
		    __field(	unsigned,	msr )
		    __field(    u64,		val )
		    __field(    int,		failed )
	    ),
	    TP_fast_assign(
		    __entry->msr = msr;
		    __entry->val = val;
		    __entry->failed = failed;
	    ),
	    TP_printk("%x, value %llx%s",
		      __entry->msr,
		      __entry->val,
		      __entry->failed ? " #GP" : "")
);

DEFINE_EVENT(msr_trace_class, read_msr,
	     TP_PROTO(unsigned msr, u64 val, int failed),
	     TP_ARGS(msr, val, failed)
);

DEFINE_EVENT(msr_trace_class, write_msr,
	     TP_PROTO(unsigned msr, u64 val, int failed),
	     TP_ARGS(msr, val, failed)
);

DEFINE_EVENT(msr_trace_class, rdpmc,
	     TP_PROTO(unsigned msr, u64 val, int failed),
	     TP_ARGS(msr, val, failed)
);

#endif /* _TRACE_MSR_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
