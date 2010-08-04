#if !defined(_TRACE_KVMMMU_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_KVMMMU_H

#include <linux/tracepoint.h>
#include <linux/ftrace_event.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM kvmmmu

#define KVM_MMU_PAGE_FIELDS \
	__field(__u64, gfn) \
	__field(__u32, role) \
	__field(__u32, root_count) \
	__field(bool, unsync)

#define KVM_MMU_PAGE_ASSIGN(sp)			     \
	__entry->gfn = sp->gfn;			     \
	__entry->role = sp->role.word;		     \
	__entry->root_count = sp->root_count;        \
	__entry->unsync = sp->unsync;

#define KVM_MMU_PAGE_PRINTK() ({				        \
	const char *ret = p->buffer + p->len;				\
	static const char *access_str[] = {			        \
		"---", "--x", "w--", "w-x", "-u-", "-ux", "wu-", "wux"  \
	};							        \
	union kvm_mmu_page_role role;				        \
								        \
	role.word = __entry->role;					\
									\
	trace_seq_printf(p, "sp gfn %llx %u%s q%u%s %s%s"		\
			 " %snxe root %u %s%c",				\
			 __entry->gfn, role.level,			\
			 role.cr4_pae ? " pae" : "",			\
			 role.quadrant,					\
			 role.direct ? " direct" : "",			\
			 access_str[role.access],			\
			 role.invalid ? " invalid" : "",		\
			 role.nxe ? "" : "!",				\
			 __entry->root_count,				\
			 __entry->unsync ? "unsync" : "sync", 0);	\
	ret;								\
		})

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

DECLARE_EVENT_CLASS(kvm_mmu_set_bit_class,

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

/* We set a pte accessed bit */
DEFINE_EVENT(kvm_mmu_set_bit_class, kvm_mmu_set_accessed_bit,

	TP_PROTO(unsigned long table_gfn, unsigned index, unsigned size),

	TP_ARGS(table_gfn, index, size)
);

/* We set a pte dirty bit */
DEFINE_EVENT(kvm_mmu_set_bit_class, kvm_mmu_set_dirty_bit,

	TP_PROTO(unsigned long table_gfn, unsigned index, unsigned size),

	TP_ARGS(table_gfn, index, size)
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

TRACE_EVENT(
	kvm_mmu_get_page,
	TP_PROTO(struct kvm_mmu_page *sp, bool created),
	TP_ARGS(sp, created),

	TP_STRUCT__entry(
		KVM_MMU_PAGE_FIELDS
		__field(bool, created)
		),

	TP_fast_assign(
		KVM_MMU_PAGE_ASSIGN(sp)
		__entry->created = created;
		),

	TP_printk("%s %s", KVM_MMU_PAGE_PRINTK(),
		  __entry->created ? "new" : "existing")
);

DECLARE_EVENT_CLASS(kvm_mmu_page_class,

	TP_PROTO(struct kvm_mmu_page *sp),
	TP_ARGS(sp),

	TP_STRUCT__entry(
		KVM_MMU_PAGE_FIELDS
	),

	TP_fast_assign(
		KVM_MMU_PAGE_ASSIGN(sp)
	),

	TP_printk("%s", KVM_MMU_PAGE_PRINTK())
);

DEFINE_EVENT(kvm_mmu_page_class, kvm_mmu_sync_page,
	TP_PROTO(struct kvm_mmu_page *sp),

	TP_ARGS(sp)
);

DEFINE_EVENT(kvm_mmu_page_class, kvm_mmu_unsync_page,
	TP_PROTO(struct kvm_mmu_page *sp),

	TP_ARGS(sp)
);

DEFINE_EVENT(kvm_mmu_page_class, kvm_mmu_prepare_zap_page,
	TP_PROTO(struct kvm_mmu_page *sp),

	TP_ARGS(sp)
);
#endif /* _TRACE_KVMMMU_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE mmutrace

/* This part must be outside protection */
#include <trace/define_trace.h>
