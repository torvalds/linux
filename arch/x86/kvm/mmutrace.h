#if !defined(_TRACE_KVMMMU_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_KVMMMU_H

#include <linux/tracepoint.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM kvmmmu
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE mmutrace

#define kvm_mmu_trace_pferr_flags       \
	{ PFERR_PRESENT_MASK, "P" },	\
	{ PFERR_WRITE_MASK, "W" },	\
	{ PFERR_USER_MASK, "U" },	\
	{ PFERR_RSVD_MASK, "RSVD" },	\
	{ PFERR_FETCH_MASK, "F" }

/*
 * A pagetable walk has started
 */
TRACE_EVENT(
	kvm_mmu_pagetable_walk,
	TP_PROTO(u64 addr, int write_fault, int user_fault, int fetch_fault),
	TP_ARGS(addr, write_fault, user_fault, fetch_fault),

	TP_STRUCT__entry(
		__field(__u64, addr)
		__field(__u32, pferr)
	),

	TP_fast_assign(
		__entry->addr = addr;
		__entry->pferr = (!!write_fault << 1) | (!!user_fault << 2)
		                 | (!!fetch_fault << 4);
	),

	TP_printk("addr %llx pferr %x %s", __entry->addr, __entry->pferr,
		  __print_flags(__entry->pferr, "|", kvm_mmu_trace_pferr_flags))
);


/* We just walked a paging element */
TRACE_EVENT(
	kvm_mmu_paging_element,
	TP_PROTO(u64 pte, int level),
	TP_ARGS(pte, level),

	TP_STRUCT__entry(
		__field(__u64, pte)
		__field(__u32, level)
		),

	TP_fast_assign(
		__entry->pte = pte;
		__entry->level = level;
		),

	TP_printk("pte %llx level %u", __entry->pte, __entry->level)
);

/* We set a pte accessed bit */
TRACE_EVENT(
	kvm_mmu_set_accessed_bit,
	TP_PROTO(unsigned long table_gfn, unsigned index, unsigned size),
	TP_ARGS(table_gfn, index, size),

	TP_STRUCT__entry(
		__field(__u64, gpa)
		),

	TP_fast_assign(
		__entry->gpa = ((u64)table_gfn << PAGE_SHIFT)
				+ index * size;
		),

	TP_printk("gpa %llx", __entry->gpa)
);

/* We set a pte dirty bit */
TRACE_EVENT(
	kvm_mmu_set_dirty_bit,
	TP_PROTO(unsigned long table_gfn, unsigned index, unsigned size),
	TP_ARGS(table_gfn, index, size),

	TP_STRUCT__entry(
		__field(__u64, gpa)
		),

	TP_fast_assign(
		__entry->gpa = ((u64)table_gfn << PAGE_SHIFT)
				+ index * size;
		),

	TP_printk("gpa %llx", __entry->gpa)
);

TRACE_EVENT(
	kvm_mmu_walker_error,
	TP_PROTO(u32 pferr),
	TP_ARGS(pferr),

	TP_STRUCT__entry(
		__field(__u32, pferr)
		),

	TP_fast_assign(
		__entry->pferr = pferr;
		),

	TP_printk("pferr %x %s", __entry->pferr,
		  __print_flags(__entry->pferr, "|", kvm_mmu_trace_pferr_flags))
);

#endif /* _TRACE_KVMMMU_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
