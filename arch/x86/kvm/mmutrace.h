/* SPDX-License-Identifier: GPL-2.0 */
#if !defined(_TRACE_KVMMMU_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_KVMMMU_H

#include <linux/tracepoint.h>
#include <linux/trace_events.h>

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
	const char *saved_ptr = trace_seq_buffer_ptr(p);		\
	static const char *access_str[] = {			        \
		"---", "--x", "w--", "w-x", "-u-", "-ux", "wu-", "wux"  \
	};							        \
	union kvm_mmu_page_role role;				        \
								        \
	role.word = __entry->role;					\
									\
	trace_seq_printf(p, "sp gfn %llx l%u %u-byte q%u%s %s%s"	\
			 " %snxe %sad root %u %s%c",			\
			 __entry->gfn, role.level,			\
			 role.gpte_is_8_bytes ? 8 : 4,			\
			 role.quadrant,					\
			 role.direct ? " direct" : "",			\
			 access_str[role.access],			\
			 role.invalid ? " invalid" : "",		\
			 role.nxe ? "" : "!",				\
			 role.ad_disabled ? "!" : "",			\
			 __entry->root_count,				\
			 __entry->unsync ? "unsync" : "sync", 0);	\
	saved_ptr;							\
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
	TP_PROTO(u64 addr, u32 pferr),
	TP_ARGS(addr, pferr),

	TP_STRUCT__entry(
		__field(__u64, addr)
		__field(__u32, pferr)
	),

	TP_fast_assign(
		__entry->addr = addr;
		__entry->pferr = pferr;
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

TRACE_EVENT(
	mark_mmio_spte,
	TP_PROTO(u64 *sptep, gfn_t gfn, unsigned access, unsigned int gen),
	TP_ARGS(sptep, gfn, access, gen),

	TP_STRUCT__entry(
		__field(void *, sptep)
		__field(gfn_t, gfn)
		__field(unsigned, access)
		__field(unsigned int, gen)
	),

	TP_fast_assign(
		__entry->sptep = sptep;
		__entry->gfn = gfn;
		__entry->access = access;
		__entry->gen = gen;
	),

	TP_printk("sptep:%p gfn %llx access %x gen %x", __entry->sptep,
		  __entry->gfn, __entry->access, __entry->gen)
);

TRACE_EVENT(
	handle_mmio_page_fault,
	TP_PROTO(u64 addr, gfn_t gfn, unsigned access),
	TP_ARGS(addr, gfn, access),

	TP_STRUCT__entry(
		__field(u64, addr)
		__field(gfn_t, gfn)
		__field(unsigned, access)
	),

	TP_fast_assign(
		__entry->addr = addr;
		__entry->gfn = gfn;
		__entry->access = access;
	),

	TP_printk("addr:%llx gfn %llx access %x", __entry->addr, __entry->gfn,
		  __entry->access)
);

#define __spte_satisfied(__spte)				\
	(__entry->retry && is_writable_pte(__entry->__spte))

TRACE_EVENT(
	fast_page_fault,
	TP_PROTO(struct kvm_vcpu *vcpu, gva_t gva, u32 error_code,
		 u64 *sptep, u64 old_spte, bool retry),
	TP_ARGS(vcpu, gva, error_code, sptep, old_spte, retry),

	TP_STRUCT__entry(
		__field(int, vcpu_id)
		__field(gva_t, gva)
		__field(u32, error_code)
		__field(u64 *, sptep)
		__field(u64, old_spte)
		__field(u64, new_spte)
		__field(bool, retry)
	),

	TP_fast_assign(
		__entry->vcpu_id = vcpu->vcpu_id;
		__entry->gva = gva;
		__entry->error_code = error_code;
		__entry->sptep = sptep;
		__entry->old_spte = old_spte;
		__entry->new_spte = *sptep;
		__entry->retry = retry;
	),

	TP_printk("vcpu %d gva %lx error_code %s sptep %p old %#llx"
		  " new %llx spurious %d fixed %d", __entry->vcpu_id,
		  __entry->gva, __print_flags(__entry->error_code, "|",
		  kvm_mmu_trace_pferr_flags), __entry->sptep,
		  __entry->old_spte, __entry->new_spte,
		  __spte_satisfied(old_spte), __spte_satisfied(new_spte)
	)
);

TRACE_EVENT(
	check_mmio_spte,
	TP_PROTO(u64 spte, unsigned int kvm_gen, unsigned int spte_gen),
	TP_ARGS(spte, kvm_gen, spte_gen),

	TP_STRUCT__entry(
		__field(unsigned int, kvm_gen)
		__field(unsigned int, spte_gen)
		__field(u64, spte)
	),

	TP_fast_assign(
		__entry->kvm_gen = kvm_gen;
		__entry->spte_gen = spte_gen;
		__entry->spte = spte;
	),

	TP_printk("spte %llx kvm_gen %x spte-gen %x valid %d", __entry->spte,
		  __entry->kvm_gen, __entry->spte_gen,
		  __entry->kvm_gen == __entry->spte_gen
	)
);
#endif /* _TRACE_KVMMMU_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE mmutrace

/* This part must be outside protection */
#include <trace/define_trace.h>
