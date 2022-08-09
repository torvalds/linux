/* SPDX-License-Identifier: GPL-2.0 */
#if !defined(_TRACE_KVMMMU_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_KVMMMU_H

#include <linux/tracepoint.h>
#include <linux/trace_events.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM kvmmmu

#define KVM_MMU_PAGE_FIELDS		\
	__field(__u8, mmu_valid_gen)	\
	__field(__u64, gfn)		\
	__field(__u32, role)		\
	__field(__u32, root_count)	\
	__field(bool, unsync)

#define KVM_MMU_PAGE_ASSIGN(sp)				\
	__entry->mmu_valid_gen = sp->mmu_valid_gen;	\
	__entry->gfn = sp->gfn;				\
	__entry->role = sp->role.word;			\
	__entry->root_count = sp->root_count;		\
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
	trace_seq_printf(p, "sp gen %u gfn %llx l%u %u-byte q%u%s %s%s"	\
			 " %snxe %sad root %u %s%c",			\
			 __entry->mmu_valid_gen,			\
			 __entry->gfn, role.level,			\
			 role.has_4_byte_gpte ? 4 : 8,			\
			 role.quadrant,					\
			 role.direct ? " direct" : "",			\
			 access_str[role.access],			\
			 role.invalid ? " invalid" : "",		\
			 role.efer_nx ? "" : "!",			\
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

TRACE_DEFINE_ENUM(RET_PF_RETRY);
TRACE_DEFINE_ENUM(RET_PF_EMULATE);
TRACE_DEFINE_ENUM(RET_PF_INVALID);
TRACE_DEFINE_ENUM(RET_PF_FIXED);
TRACE_DEFINE_ENUM(RET_PF_SPURIOUS);

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
	TP_PROTO(u64 *sptep, gfn_t gfn, u64 spte),
	TP_ARGS(sptep, gfn, spte),

	TP_STRUCT__entry(
		__field(void *, sptep)
		__field(gfn_t, gfn)
		__field(unsigned, access)
		__field(unsigned int, gen)
	),

	TP_fast_assign(
		__entry->sptep = sptep;
		__entry->gfn = gfn;
		__entry->access = spte & ACC_ALL;
		__entry->gen = get_mmio_spte_generation(spte);
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

TRACE_EVENT(
	fast_page_fault,
	TP_PROTO(struct kvm_vcpu *vcpu, struct kvm_page_fault *fault,
		 u64 *sptep, u64 old_spte, int ret),
	TP_ARGS(vcpu, fault, sptep, old_spte, ret),

	TP_STRUCT__entry(
		__field(int, vcpu_id)
		__field(gpa_t, cr2_or_gpa)
		__field(u32, error_code)
		__field(u64 *, sptep)
		__field(u64, old_spte)
		__field(u64, new_spte)
		__field(int, ret)
	),

	TP_fast_assign(
		__entry->vcpu_id = vcpu->vcpu_id;
		__entry->cr2_or_gpa = fault->addr;
		__entry->error_code = fault->error_code;
		__entry->sptep = sptep;
		__entry->old_spte = old_spte;
		__entry->new_spte = *sptep;
		__entry->ret = ret;
	),

	TP_printk("vcpu %d gva %llx error_code %s sptep %p old %#llx"
		  " new %llx spurious %d fixed %d", __entry->vcpu_id,
		  __entry->cr2_or_gpa, __print_flags(__entry->error_code, "|",
		  kvm_mmu_trace_pferr_flags), __entry->sptep,
		  __entry->old_spte, __entry->new_spte,
		  __entry->ret == RET_PF_SPURIOUS, __entry->ret == RET_PF_FIXED
	)
);

TRACE_EVENT(
	kvm_mmu_zap_all_fast,
	TP_PROTO(struct kvm *kvm),
	TP_ARGS(kvm),

	TP_STRUCT__entry(
		__field(__u8, mmu_valid_gen)
		__field(unsigned int, mmu_used_pages)
	),

	TP_fast_assign(
		__entry->mmu_valid_gen = kvm->arch.mmu_valid_gen;
		__entry->mmu_used_pages = kvm->arch.n_used_mmu_pages;
	),

	TP_printk("kvm-mmu-valid-gen %u used_pages %x",
		  __entry->mmu_valid_gen, __entry->mmu_used_pages
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

TRACE_EVENT(
	kvm_mmu_set_spte,
	TP_PROTO(int level, gfn_t gfn, u64 *sptep),
	TP_ARGS(level, gfn, sptep),

	TP_STRUCT__entry(
		__field(u64, gfn)
		__field(u64, spte)
		__field(u64, sptep)
		__field(u8, level)
		/* These depend on page entry type, so compute them now.  */
		__field(bool, r)
		__field(bool, x)
		__field(signed char, u)
	),

	TP_fast_assign(
		__entry->gfn = gfn;
		__entry->spte = *sptep;
		__entry->sptep = virt_to_phys(sptep);
		__entry->level = level;
		__entry->r = shadow_present_mask || (__entry->spte & PT_PRESENT_MASK);
		__entry->x = is_executable_pte(__entry->spte);
		__entry->u = shadow_user_mask ? !!(__entry->spte & shadow_user_mask) : -1;
	),

	TP_printk("gfn %llx spte %llx (%s%s%s%s) level %d at %llx",
		  __entry->gfn, __entry->spte,
		  __entry->r ? "r" : "-",
		  __entry->spte & PT_WRITABLE_MASK ? "w" : "-",
		  __entry->x ? "x" : "-",
		  __entry->u == -1 ? "" : (__entry->u ? "u" : "-"),
		  __entry->level, __entry->sptep
	)
);

TRACE_EVENT(
	kvm_mmu_spte_requested,
	TP_PROTO(struct kvm_page_fault *fault),
	TP_ARGS(fault),

	TP_STRUCT__entry(
		__field(u64, gfn)
		__field(u64, pfn)
		__field(u8, level)
	),

	TP_fast_assign(
		__entry->gfn = fault->gfn;
		__entry->pfn = fault->pfn | (fault->gfn & (KVM_PAGES_PER_HPAGE(fault->goal_level) - 1));
		__entry->level = fault->goal_level;
	),

	TP_printk("gfn %llx pfn %llx level %d",
		  __entry->gfn, __entry->pfn, __entry->level
	)
);

TRACE_EVENT(
	kvm_tdp_mmu_spte_changed,
	TP_PROTO(int as_id, gfn_t gfn, int level, u64 old_spte, u64 new_spte),
	TP_ARGS(as_id, gfn, level, old_spte, new_spte),

	TP_STRUCT__entry(
		__field(u64, gfn)
		__field(u64, old_spte)
		__field(u64, new_spte)
		/* Level cannot be larger than 5 on x86, so it fits in a u8. */
		__field(u8, level)
		/* as_id can only be 0 or 1 x86, so it fits in a u8. */
		__field(u8, as_id)
	),

	TP_fast_assign(
		__entry->gfn = gfn;
		__entry->old_spte = old_spte;
		__entry->new_spte = new_spte;
		__entry->level = level;
		__entry->as_id = as_id;
	),

	TP_printk("as id %d gfn %llx level %d old_spte %llx new_spte %llx",
		  __entry->as_id, __entry->gfn, __entry->level,
		  __entry->old_spte, __entry->new_spte
	)
);

TRACE_EVENT(
	kvm_mmu_split_huge_page,
	TP_PROTO(u64 gfn, u64 spte, int level, int errno),
	TP_ARGS(gfn, spte, level, errno),

	TP_STRUCT__entry(
		__field(u64, gfn)
		__field(u64, spte)
		__field(int, level)
		__field(int, errno)
	),

	TP_fast_assign(
		__entry->gfn = gfn;
		__entry->spte = spte;
		__entry->level = level;
		__entry->errno = errno;
	),

	TP_printk("gfn %llx spte %llx level %d errno %d",
		  __entry->gfn, __entry->spte, __entry->level, __entry->errno)
);

#endif /* _TRACE_KVMMMU_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH mmu
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE mmutrace

/* This part must be outside protection */
#include <trace/define_trace.h>
