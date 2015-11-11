
#if !defined(_TRACE_KVM_PR_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_KVM_PR_H

#include <linux/tracepoint.h>
#include "trace_book3s.h"

#undef TRACE_SYSTEM
#define TRACE_SYSTEM kvm_pr
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE trace_pr

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
		__field(	u64,		host_vpn	)
		__field(	u64,		pfn		)
		__field(	ulong,		eaddr		)
		__field(	u64,		vpage		)
		__field(	ulong,		raddr		)
		__field(	int,		flags		)
	),

	TP_fast_assign(
		__entry->host_vpn	= pte->host_vpn;
		__entry->pfn		= pte->pfn;
		__entry->eaddr		= pte->pte.eaddr;
		__entry->vpage		= pte->pte.vpage;
		__entry->raddr		= pte->pte.raddr;
		__entry->flags		= (pte->pte.may_read ? 0x4 : 0) |
					  (pte->pte.may_write ? 0x2 : 0) |
					  (pte->pte.may_execute ? 0x1 : 0);
	),

	TP_printk("Map: hvpn=%llx pfn=%llx ea=%lx vp=%llx ra=%lx [%x]",
		  __entry->host_vpn, __entry->pfn, __entry->eaddr,
		  __entry->vpage, __entry->raddr, __entry->flags)
);

TRACE_EVENT(kvm_book3s_mmu_invalidate,
	TP_PROTO(struct hpte_cache *pte),
	TP_ARGS(pte),

	TP_STRUCT__entry(
		__field(	u64,		host_vpn	)
		__field(	u64,		pfn		)
		__field(	ulong,		eaddr		)
		__field(	u64,		vpage		)
		__field(	ulong,		raddr		)
		__field(	int,		flags		)
	),

	TP_fast_assign(
		__entry->host_vpn	= pte->host_vpn;
		__entry->pfn		= pte->pfn;
		__entry->eaddr		= pte->pte.eaddr;
		__entry->vpage		= pte->pte.vpage;
		__entry->raddr		= pte->pte.raddr;
		__entry->flags		= (pte->pte.may_read ? 0x4 : 0) |
					  (pte->pte.may_write ? 0x2 : 0) |
					  (pte->pte.may_execute ? 0x1 : 0);
	),

	TP_printk("Flush: hva=%llx pfn=%llx ea=%lx vp=%llx ra=%lx [%x]",
		  __entry->host_vpn, __entry->pfn, __entry->eaddr,
		  __entry->vpage, __entry->raddr, __entry->flags)
);

TRACE_EVENT(kvm_book3s_mmu_flush,
	TP_PROTO(const char *type, struct kvm_vcpu *vcpu, unsigned long long p1,
		 unsigned long long p2),
	TP_ARGS(type, vcpu, p1, p2),

	TP_STRUCT__entry(
		__field(	int,			count		)
		__field(	unsigned long long,	p1		)
		__field(	unsigned long long,	p2		)
		__field(	const char *,		type		)
	),

	TP_fast_assign(
		__entry->count		= to_book3s(vcpu)->hpte_cache_count;
		__entry->p1		= p1;
		__entry->p2		= p2;
		__entry->type		= type;
	),

	TP_printk("Flush %d %sPTEs: %llx - %llx",
		  __entry->count, __entry->type, __entry->p1, __entry->p2)
);

TRACE_EVENT(kvm_book3s_slb_found,
	TP_PROTO(unsigned long long gvsid, unsigned long long hvsid),
	TP_ARGS(gvsid, hvsid),

	TP_STRUCT__entry(
		__field(	unsigned long long,	gvsid		)
		__field(	unsigned long long,	hvsid		)
	),

	TP_fast_assign(
		__entry->gvsid		= gvsid;
		__entry->hvsid		= hvsid;
	),

	TP_printk("%llx -> %llx", __entry->gvsid, __entry->hvsid)
);

TRACE_EVENT(kvm_book3s_slb_fail,
	TP_PROTO(u16 sid_map_mask, unsigned long long gvsid),
	TP_ARGS(sid_map_mask, gvsid),

	TP_STRUCT__entry(
		__field(	unsigned short,		sid_map_mask	)
		__field(	unsigned long long,	gvsid		)
	),

	TP_fast_assign(
		__entry->sid_map_mask	= sid_map_mask;
		__entry->gvsid		= gvsid;
	),

	TP_printk("%x/%x: %llx", __entry->sid_map_mask,
		  SID_MAP_MASK - __entry->sid_map_mask, __entry->gvsid)
);

TRACE_EVENT(kvm_book3s_slb_map,
	TP_PROTO(u16 sid_map_mask, unsigned long long gvsid,
		 unsigned long long hvsid),
	TP_ARGS(sid_map_mask, gvsid, hvsid),

	TP_STRUCT__entry(
		__field(	unsigned short,		sid_map_mask	)
		__field(	unsigned long long,	guest_vsid	)
		__field(	unsigned long long,	host_vsid	)
	),

	TP_fast_assign(
		__entry->sid_map_mask	= sid_map_mask;
		__entry->guest_vsid	= gvsid;
		__entry->host_vsid	= hvsid;
	),

	TP_printk("%x: %llx -> %llx", __entry->sid_map_mask,
		  __entry->guest_vsid, __entry->host_vsid)
);

TRACE_EVENT(kvm_book3s_slbmte,
	TP_PROTO(u64 slb_vsid, u64 slb_esid),
	TP_ARGS(slb_vsid, slb_esid),

	TP_STRUCT__entry(
		__field(	u64,	slb_vsid	)
		__field(	u64,	slb_esid	)
	),

	TP_fast_assign(
		__entry->slb_vsid	= slb_vsid;
		__entry->slb_esid	= slb_esid;
	),

	TP_printk("%llx, %llx", __entry->slb_vsid, __entry->slb_esid)
);

TRACE_EVENT(kvm_exit,
	TP_PROTO(unsigned int exit_nr, struct kvm_vcpu *vcpu),
	TP_ARGS(exit_nr, vcpu),

	TP_STRUCT__entry(
		__field(	unsigned int,	exit_nr		)
		__field(	unsigned long,	pc		)
		__field(	unsigned long,	msr		)
		__field(	unsigned long,	dar		)
		__field(	unsigned long,	srr1		)
		__field(	unsigned long,	last_inst	)
	),

	TP_fast_assign(
		__entry->exit_nr	= exit_nr;
		__entry->pc		= kvmppc_get_pc(vcpu);
		__entry->dar		= kvmppc_get_fault_dar(vcpu);
		__entry->msr		= kvmppc_get_msr(vcpu);
		__entry->srr1		= vcpu->arch.shadow_srr1;
		__entry->last_inst	= vcpu->arch.last_inst;
	),

	TP_printk("exit=%s"
		" | pc=0x%lx"
		" | msr=0x%lx"
		" | dar=0x%lx"
		" | srr1=0x%lx"
		" | last_inst=0x%lx"
		,
		__print_symbolic(__entry->exit_nr, kvm_trace_symbol_exit),
		__entry->pc,
		__entry->msr,
		__entry->dar,
		__entry->srr1,
		__entry->last_inst
		)
);

TRACE_EVENT(kvm_unmap_hva,
	TP_PROTO(unsigned long hva),
	TP_ARGS(hva),

	TP_STRUCT__entry(
		__field(	unsigned long,	hva		)
	),

	TP_fast_assign(
		__entry->hva		= hva;
	),

	TP_printk("unmap hva 0x%lx\n", __entry->hva)
);

#endif /* _TRACE_KVM_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
