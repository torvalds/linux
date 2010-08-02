#if !defined(_TRACE_KVM_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_KVM_H

#include <linux/tracepoint.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM kvm
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE trace

/*
 * Tracepoint for guest mode entry.
 */
TRACE_EVENT(kvm_ppc_instr,
	TP_PROTO(unsigned int inst, unsigned long _pc, unsigned int emulate),
	TP_ARGS(inst, _pc, emulate),

	TP_STRUCT__entry(
		__field(	unsigned int,	inst		)
		__field(	unsigned long,	pc		)
		__field(	unsigned int,	emulate		)
	),

	TP_fast_assign(
		__entry->inst		= inst;
		__entry->pc		= _pc;
		__entry->emulate	= emulate;
	),

	TP_printk("inst %u pc 0x%lx emulate %u\n",
		  __entry->inst, __entry->pc, __entry->emulate)
);

TRACE_EVENT(kvm_stlb_inval,
	TP_PROTO(unsigned int stlb_index),
	TP_ARGS(stlb_index),

	TP_STRUCT__entry(
		__field(	unsigned int,	stlb_index	)
	),

	TP_fast_assign(
		__entry->stlb_index	= stlb_index;
	),

	TP_printk("stlb_index %u", __entry->stlb_index)
);

TRACE_EVENT(kvm_stlb_write,
	TP_PROTO(unsigned int victim, unsigned int tid, unsigned int word0,
		 unsigned int word1, unsigned int word2),
	TP_ARGS(victim, tid, word0, word1, word2),

	TP_STRUCT__entry(
		__field(	unsigned int,	victim		)
		__field(	unsigned int,	tid		)
		__field(	unsigned int,	word0		)
		__field(	unsigned int,	word1		)
		__field(	unsigned int,	word2		)
	),

	TP_fast_assign(
		__entry->victim		= victim;
		__entry->tid		= tid;
		__entry->word0		= word0;
		__entry->word1		= word1;
		__entry->word2		= word2;
	),

	TP_printk("victim %u tid %u w0 %u w1 %u w2 %u",
		__entry->victim, __entry->tid, __entry->word0,
		__entry->word1, __entry->word2)
);

TRACE_EVENT(kvm_gtlb_write,
	TP_PROTO(unsigned int gtlb_index, unsigned int tid, unsigned int word0,
		 unsigned int word1, unsigned int word2),
	TP_ARGS(gtlb_index, tid, word0, word1, word2),

	TP_STRUCT__entry(
		__field(	unsigned int,	gtlb_index	)
		__field(	unsigned int,	tid		)
		__field(	unsigned int,	word0		)
		__field(	unsigned int,	word1		)
		__field(	unsigned int,	word2		)
	),

	TP_fast_assign(
		__entry->gtlb_index	= gtlb_index;
		__entry->tid		= tid;
		__entry->word0		= word0;
		__entry->word1		= word1;
		__entry->word2		= word2;
	),

	TP_printk("gtlb_index %u tid %u w0 %u w1 %u w2 %u",
		__entry->gtlb_index, __entry->tid, __entry->word0,
		__entry->word1, __entry->word2)
);


/*************************************************************************
 *                         Book3S trace points                           *
 *************************************************************************/

#ifdef CONFIG_PPC_BOOK3S

TRACE_EVENT(kvm_book3s_exit,
	TP_PROTO(unsigned int exit_nr, struct kvm_vcpu *vcpu),
	TP_ARGS(exit_nr, vcpu),

	TP_STRUCT__entry(
		__field(	unsigned int,	exit_nr		)
		__field(	unsigned long,	pc		)
		__field(	unsigned long,	msr		)
		__field(	unsigned long,	dar		)
		__field(	unsigned long,	srr1		)
	),

	TP_fast_assign(
		__entry->exit_nr	= exit_nr;
		__entry->pc		= kvmppc_get_pc(vcpu);
		__entry->dar		= kvmppc_get_fault_dar(vcpu);
		__entry->msr		= vcpu->arch.shared->msr;
		__entry->srr1		= to_svcpu(vcpu)->shadow_srr1;
	),

	TP_printk("exit=0x%x | pc=0x%lx | msr=0x%lx | dar=0x%lx | srr1=0x%lx",
		  __entry->exit_nr, __entry->pc, __entry->msr, __entry->dar,
		  __entry->srr1)
);

TRACE_EVENT(kvm_book3s_reenter,
	TP_PROTO(int r, struct kvm_vcpu *vcpu),
	TP_ARGS(r, vcpu),

	TP_STRUCT__entry(
		__field(	unsigned int,	r		)
		__field(	unsigned long,	pc		)
	),

	TP_fast_assign(
		__entry->r		= r;
		__entry->pc		= kvmppc_get_pc(vcpu);
	),

	TP_printk("reentry r=%d | pc=0x%lx", __entry->r, __entry->pc)
);

#ifdef CONFIG_PPC_BOOK3S_64

TRACE_EVENT(kvm_book3s_64_mmu_map,
	TP_PROTO(int rflags, ulong hpteg, ulong va, pfn_t hpaddr,
		 struct kvmppc_pte *orig_pte),
	TP_ARGS(rflags, hpteg, va, hpaddr, orig_pte),

	TP_STRUCT__entry(
		__field(	unsigned char,		flag_w		)
		__field(	unsigned char,		flag_x		)
		__field(	unsigned long,		eaddr		)
		__field(	unsigned long,		hpteg		)
		__field(	unsigned long,		va		)
		__field(	unsigned long long,	vpage		)
		__field(	unsigned long,		hpaddr		)
	),

	TP_fast_assign(
		__entry->flag_w	= ((rflags & HPTE_R_PP) == 3) ? '-' : 'w';
		__entry->flag_x	= (rflags & HPTE_R_N) ? '-' : 'x';
		__entry->eaddr	= orig_pte->eaddr;
		__entry->hpteg	= hpteg;
		__entry->va	= va;
		__entry->vpage	= orig_pte->vpage;
		__entry->hpaddr	= hpaddr;
	),

	TP_printk("KVM: %c%c Map 0x%lx: [%lx] 0x%lx (0x%llx) -> %lx",
		  __entry->flag_w, __entry->flag_x, __entry->eaddr,
		  __entry->hpteg, __entry->va, __entry->vpage, __entry->hpaddr)
);

#endif /* CONFIG_PPC_BOOK3S_64 */

TRACE_EVENT(kvm_book3s_mmu_map,
	TP_PROTO(struct hpte_cache *pte),
	TP_ARGS(pte),

	TP_STRUCT__entry(
		__field(	u64,		host_va		)
		__field(	u64,		pfn		)
		__field(	ulong,		eaddr		)
		__field(	u64,		vpage		)
		__field(	ulong,		raddr		)
		__field(	int,		flags		)
	),

	TP_fast_assign(
		__entry->host_va	= pte->host_va;
		__entry->pfn		= pte->pfn;
		__entry->eaddr		= pte->pte.eaddr;
		__entry->vpage		= pte->pte.vpage;
		__entry->raddr		= pte->pte.raddr;
		__entry->flags		= (pte->pte.may_read ? 0x4 : 0) |
					  (pte->pte.may_write ? 0x2 : 0) |
					  (pte->pte.may_execute ? 0x1 : 0);
	),

	TP_printk("Map: hva=%llx pfn=%llx ea=%lx vp=%llx ra=%lx [%x]",
		  __entry->host_va, __entry->pfn, __entry->eaddr,
		  __entry->vpage, __entry->raddr, __entry->flags)
);

TRACE_EVENT(kvm_book3s_mmu_invalidate,
	TP_PROTO(struct hpte_cache *pte),
	TP_ARGS(pte),

	TP_STRUCT__entry(
		__field(	u64,		host_va		)
		__field(	u64,		pfn		)
		__field(	ulong,		eaddr		)
		__field(	u64,		vpage		)
		__field(	ulong,		raddr		)
		__field(	int,		flags		)
	),

	TP_fast_assign(
		__entry->host_va	= pte->host_va;
		__entry->pfn		= pte->pfn;
		__entry->eaddr		= pte->pte.eaddr;
		__entry->vpage		= pte->pte.vpage;
		__entry->raddr		= pte->pte.raddr;
		__entry->flags		= (pte->pte.may_read ? 0x4 : 0) |
					  (pte->pte.may_write ? 0x2 : 0) |
					  (pte->pte.may_execute ? 0x1 : 0);
	),

	TP_printk("Flush: hva=%llx pfn=%llx ea=%lx vp=%llx ra=%lx [%x]",
		  __entry->host_va, __entry->pfn, __entry->eaddr,
		  __entry->vpage, __entry->raddr, __entry->flags)
);

#endif /* CONFIG_PPC_BOOK3S */

#endif /* _TRACE_KVM_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
