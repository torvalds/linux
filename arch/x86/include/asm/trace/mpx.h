#undef TRACE_SYSTEM
#define TRACE_SYSTEM mpx

#if !defined(_TRACE_MPX_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_MPX_H

#include <linux/tracepoint.h>

#ifdef CONFIG_X86_INTEL_MPX

TRACE_EVENT(mpx_bounds_register_exception,

	TP_PROTO(void *addr_referenced,
		 const struct bndreg *bndreg),
	TP_ARGS(addr_referenced, bndreg),

	TP_STRUCT__entry(
		__field(void *, addr_referenced)
		__field(u64, lower_bound)
		__field(u64, upper_bound)
	),

	TP_fast_assign(
		__entry->addr_referenced = addr_referenced;
		__entry->lower_bound = bndreg->lower_bound;
		__entry->upper_bound = bndreg->upper_bound;
	),
	/*
	 * Note that we are printing out the '~' of the upper
	 * bounds register here.  It is actually stored in its
	 * one's complement form so that its 'init' state
	 * corresponds to all 0's.  But, that looks like
	 * gibberish when printed out, so print out the 1's
	 * complement instead of the actual value here.  Note
	 * though that you still need to specify filters for the
	 * actual value, not the displayed one.
	 */
	TP_printk("address referenced: 0x%p bounds: lower: 0x%llx ~upper: 0x%llx",
		__entry->addr_referenced,
		__entry->lower_bound,
		~__entry->upper_bound
	)
);

TRACE_EVENT(bounds_exception_mpx,

	TP_PROTO(const struct bndcsr *bndcsr),
	TP_ARGS(bndcsr),

	TP_STRUCT__entry(
		__field(u64, bndcfgu)
		__field(u64, bndstatus)
	),

	TP_fast_assign(
		/* need to get rid of the 'const' on bndcsr */
		__entry->bndcfgu   = (u64)bndcsr->bndcfgu;
		__entry->bndstatus = (u64)bndcsr->bndstatus;
	),

	TP_printk("bndcfgu:0x%llx bndstatus:0x%llx",
		__entry->bndcfgu,
		__entry->bndstatus)
);

DECLARE_EVENT_CLASS(mpx_range_trace,

	TP_PROTO(unsigned long start,
		 unsigned long end),
	TP_ARGS(start, end),

	TP_STRUCT__entry(
		__field(unsigned long, start)
		__field(unsigned long, end)
	),

	TP_fast_assign(
		__entry->start = start;
		__entry->end   = end;
	),

	TP_printk("[0x%p:0x%p]",
		(void *)__entry->start,
		(void *)__entry->end
	)
);

DEFINE_EVENT(mpx_range_trace, mpx_unmap_zap,
	TP_PROTO(unsigned long start, unsigned long end),
	TP_ARGS(start, end)
);

DEFINE_EVENT(mpx_range_trace, mpx_unmap_search,
	TP_PROTO(unsigned long start, unsigned long end),
	TP_ARGS(start, end)
);

TRACE_EVENT(mpx_new_bounds_table,

	TP_PROTO(unsigned long table_vaddr),
	TP_ARGS(table_vaddr),

	TP_STRUCT__entry(
		__field(unsigned long, table_vaddr)
	),

	TP_fast_assign(
		__entry->table_vaddr = table_vaddr;
	),

	TP_printk("table vaddr:%p", (void *)__entry->table_vaddr)
);

#else

/*
 * This gets used outside of MPX-specific code, so we need a stub.
 */
static inline void trace_bounds_exception_mpx(const struct bndcsr *bndcsr)
{
}

#endif /* CONFIG_X86_INTEL_MPX */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH asm/trace/
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE mpx
#endif /* _TRACE_MPX_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
