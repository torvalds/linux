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

#ifdef CONFIG_PPC_BOOK3S
#define kvm_trace_symbol_exit \
	{0x100, "SYSTEM_RESET"}, \
	{0x200, "MACHINE_CHECK"}, \
	{0x300, "DATA_STORAGE"}, \
	{0x380, "DATA_SEGMENT"}, \
	{0x400, "INST_STORAGE"}, \
	{0x480, "INST_SEGMENT"}, \
	{0x500, "EXTERNAL"}, \
	{0x501, "EXTERNAL_LEVEL"}, \
	{0x502, "EXTERNAL_HV"}, \
	{0x600, "ALIGNMENT"}, \
	{0x700, "PROGRAM"}, \
	{0x800, "FP_UNAVAIL"}, \
	{0x900, "DECREMENTER"}, \
	{0x980, "HV_DECREMENTER"}, \
	{0xc00, "SYSCALL"}, \
	{0xd00, "TRACE"}, \
	{0xe00, "H_DATA_STORAGE"}, \
	{0xe20, "H_INST_STORAGE"}, \
	{0xe40, "H_EMUL_ASSIST"}, \
	{0xf00, "PERFMON"}, \
	{0xf20, "ALTIVEC"}, \
	{0xf40, "VSX"}
#else
#define kvm_trace_symbol_exit \
	{0, "CRITICAL"}, \
	{1, "MACHINE_CHECK"}, \
	{2, "DATA_STORAGE"}, \
	{3, "INST_STORAGE"}, \
	{4, "EXTERNAL"}, \
	{5, "ALIGNMENT"}, \
	{6, "PROGRAM"}, \
	{7, "FP_UNAVAIL"}, \
	{8, "SYSCALL"}, \
	{9, "AP_UNAVAIL"}, \
	{10, "DECREMENTER"}, \
	{11, "FIT"}, \
	{12, "WATCHDOG"}, \
	{13, "DTLB_MISS"}, \
	{14, "ITLB_MISS"}, \
	{15, "DEBUG"}, \
	{32, "SPE_UNAVAIL"}, \
	{33, "SPE_FP_DATA"}, \
	{34, "SPE_FP_ROUND"}, \
	{35, "PERFORMANCE_MONITOR"}, \
	{36, "DOORBELL"}, \
	{37, "DOORBELL_CRITICAL"}, \
	{38, "GUEST_DBELL"}, \
	{39, "GUEST_DBELL_CRIT"}, \
	{40, "HV_SYSCALL"}, \
	{41, "HV_PRIV"}
#endif

TRACE_EVENT(kvm_exit,
	TP_PROTO(unsigned int exit_nr, struct kvm_vcpu *vcpu),
	TP_ARGS(exit_nr, vcpu),

	TP_STRUCT__entry(
		__field(	unsigned int,	exit_nr		)
		__field(	unsigned long,	pc		)
		__field(	unsigned long,	msr		)
		__field(	unsigned long,	dar		)
#ifdef CONFIG_KVM_BOOK3S_PR
		__field(	unsigned long,	srr1		)
#endif
		__field(	unsigned long,	last_inst	)
	),

	TP_fast_assign(
#ifdef CONFIG_KVM_BOOK3S_PR
		struct kvmppc_book3s_shadow_vcpu *svcpu;
#endif
		__entry->exit_nr	= exit_nr;
		__entry->pc		= kvmppc_get_pc(vcpu);
		__entry->dar		= kvmppc_get_fault_dar(vcpu);
		__entry->msr		= vcpu->arch.shared->msr;
#ifdef CONFIG_KVM_BOOK3S_PR
		svcpu = svcpu_get(vcpu);
		__entry->srr1		= svcpu->shadow_srr1;
		svcpu_put(svcpu);
#endif
		__entry->last_inst	= vcpu->arch.last_inst;
	),

	TP_printk("exit=%s"
		" | pc=0x%lx"
		" | msr=0x%lx"
		" | dar=0x%lx"
#ifdef CONFIG_KVM_BOOK3S_PR
		" | srr1=0x%lx"
#endif
		" | last_inst=0x%lx"
		,
		__print_symbolic(__entry->exit_nr, kvm_trace_symbol_exit),
		__entry->pc,
		__entry->msr,
		__entry->dar,
#ifdef CONFIG_KVM_BOOK3S_PR
		__entry->srr1,
#endif
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

TRACE_EVENT(kvm_check_requests,
	TP_PROTO(struct kvm_vcpu *vcpu),
	TP_ARGS(vcpu),

	TP_STRUCT__entry(
		__field(	__u32,	cpu_nr		)
		__field(	__u32,	requests	)
	),

	TP_fast_assign(
		__entry->cpu_nr		= vcpu->vcpu_id;
		__entry->requests	= vcpu->requests;
	),

	TP_printk("vcpu=%x requests=%x",
		__entry->cpu_nr, __entry->requests)
);


/*************************************************************************
 *                         Book3S trace points                           *
 *************************************************************************/

#ifdef CONFIG_KVM_BOOK3S_PR

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

#endif /* CONFIG_PPC_BOOK3S */


/*************************************************************************
 *                         Book3E trace points                           *
 *************************************************************************/

#ifdef CONFIG_BOOKE

TRACE_EVENT(kvm_booke206_stlb_write,
	TP_PROTO(__u32 mas0, __u32 mas8, __u32 mas1, __u64 mas2, __u64 mas7_3),
	TP_ARGS(mas0, mas8, mas1, mas2, mas7_3),

	TP_STRUCT__entry(
		__field(	__u32,	mas0		)
		__field(	__u32,	mas8		)
		__field(	__u32,	mas1		)
		__field(	__u64,	mas2		)
		__field(	__u64,	mas7_3		)
	),

	TP_fast_assign(
		__entry->mas0		= mas0;
		__entry->mas8		= mas8;
		__entry->mas1		= mas1;
		__entry->mas2		= mas2;
		__entry->mas7_3		= mas7_3;
	),

	TP_printk("mas0=%x mas8=%x mas1=%x mas2=%llx mas7_3=%llx",
		__entry->mas0, __entry->mas8, __entry->mas1,
		__entry->mas2, __entry->mas7_3)
);

TRACE_EVENT(kvm_booke206_gtlb_write,
	TP_PROTO(__u32 mas0, __u32 mas1, __u64 mas2, __u64 mas7_3),
	TP_ARGS(mas0, mas1, mas2, mas7_3),

	TP_STRUCT__entry(
		__field(	__u32,	mas0		)
		__field(	__u32,	mas1		)
		__field(	__u64,	mas2		)
		__field(	__u64,	mas7_3		)
	),

	TP_fast_assign(
		__entry->mas0		= mas0;
		__entry->mas1		= mas1;
		__entry->mas2		= mas2;
		__entry->mas7_3		= mas7_3;
	),

	TP_printk("mas0=%x mas1=%x mas2=%llx mas7_3=%llx",
		__entry->mas0, __entry->mas1,
		__entry->mas2, __entry->mas7_3)
);

TRACE_EVENT(kvm_booke206_ref_release,
	TP_PROTO(__u64 pfn, __u32 flags),
	TP_ARGS(pfn, flags),

	TP_STRUCT__entry(
		__field(	__u64,	pfn		)
		__field(	__u32,	flags		)
	),

	TP_fast_assign(
		__entry->pfn		= pfn;
		__entry->flags		= flags;
	),

	TP_printk("pfn=%llx flags=%x",
		__entry->pfn, __entry->flags)
);

TRACE_EVENT(kvm_booke_queue_irqprio,
	TP_PROTO(struct kvm_vcpu *vcpu, unsigned int priority),
	TP_ARGS(vcpu, priority),

	TP_STRUCT__entry(
		__field(	__u32,	cpu_nr		)
		__field(	__u32,	priority		)
		__field(	unsigned long,	pending		)
	),

	TP_fast_assign(
		__entry->cpu_nr		= vcpu->vcpu_id;
		__entry->priority	= priority;
		__entry->pending	= vcpu->arch.pending_exceptions;
	),

	TP_printk("vcpu=%x prio=%x pending=%lx",
		__entry->cpu_nr, __entry->priority, __entry->pending)
);

#endif

#endif /* _TRACE_KVM_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
