#if !defined(_TRACE_KVM_BOOKE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_KVM_BOOKE_H

#include <linux/tracepoint.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM kvm_booke

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

TRACE_EVENT(kvm_exit,
	TP_PROTO(unsigned int exit_nr, struct kvm_vcpu *vcpu),
	TP_ARGS(exit_nr, vcpu),

	TP_STRUCT__entry(
		__field(	unsigned int,	exit_nr		)
		__field(	unsigned long,	pc		)
		__field(	unsigned long,	msr		)
		__field(	unsigned long,	dar		)
		__field(	unsigned long,	last_inst	)
	),

	TP_fast_assign(
		__entry->exit_nr	= exit_nr;
		__entry->pc		= kvmppc_get_pc(vcpu);
		__entry->dar		= kvmppc_get_fault_dar(vcpu);
		__entry->msr		= vcpu->arch.shared->msr;
		__entry->last_inst	= vcpu->arch.last_inst;
	),

	TP_printk("exit=%s"
		" | pc=0x%lx"
		" | msr=0x%lx"
		" | dar=0x%lx"
		" | last_inst=0x%lx"
		,
		__print_symbolic(__entry->exit_nr, kvm_trace_symbol_exit),
		__entry->pc,
		__entry->msr,
		__entry->dar,
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

#ifdef CONFIG_SPE_POSSIBLE
#define kvm_trace_symbol_irqprio_spe \
	{BOOKE_IRQPRIO_SPE_UNAVAIL, "SPE_UNAVAIL"}, \
	{BOOKE_IRQPRIO_SPE_FP_DATA, "SPE_FP_DATA"}, \
	{BOOKE_IRQPRIO_SPE_FP_ROUND, "SPE_FP_ROUND"},
#else
#define kvm_trace_symbol_irqprio_spe
#endif

#ifdef CONFIG_PPC_E500MC
#define kvm_trace_symbol_irqprio_e500mc \
	{BOOKE_IRQPRIO_ALTIVEC_UNAVAIL, "ALTIVEC_UNAVAIL"}, \
	{BOOKE_IRQPRIO_ALTIVEC_ASSIST, "ALTIVEC_ASSIST"},
#else
#define kvm_trace_symbol_irqprio_e500mc
#endif

#define kvm_trace_symbol_irqprio \
	kvm_trace_symbol_irqprio_spe \
	kvm_trace_symbol_irqprio_e500mc \
	{BOOKE_IRQPRIO_DATA_STORAGE, "DATA_STORAGE"}, \
	{BOOKE_IRQPRIO_INST_STORAGE, "INST_STORAGE"}, \
	{BOOKE_IRQPRIO_ALIGNMENT, "ALIGNMENT"}, \
	{BOOKE_IRQPRIO_PROGRAM, "PROGRAM"}, \
	{BOOKE_IRQPRIO_FP_UNAVAIL, "FP_UNAVAIL"}, \
	{BOOKE_IRQPRIO_SYSCALL, "SYSCALL"}, \
	{BOOKE_IRQPRIO_AP_UNAVAIL, "AP_UNAVAIL"}, \
	{BOOKE_IRQPRIO_DTLB_MISS, "DTLB_MISS"}, \
	{BOOKE_IRQPRIO_ITLB_MISS, "ITLB_MISS"}, \
	{BOOKE_IRQPRIO_MACHINE_CHECK, "MACHINE_CHECK"}, \
	{BOOKE_IRQPRIO_DEBUG, "DEBUG"}, \
	{BOOKE_IRQPRIO_CRITICAL, "CRITICAL"}, \
	{BOOKE_IRQPRIO_WATCHDOG, "WATCHDOG"}, \
	{BOOKE_IRQPRIO_EXTERNAL, "EXTERNAL"}, \
	{BOOKE_IRQPRIO_FIT, "FIT"}, \
	{BOOKE_IRQPRIO_DECREMENTER, "DECREMENTER"}, \
	{BOOKE_IRQPRIO_PERFORMANCE_MONITOR, "PERFORMANCE_MONITOR"}, \
	{BOOKE_IRQPRIO_EXTERNAL_LEVEL, "EXTERNAL_LEVEL"}, \
	{BOOKE_IRQPRIO_DBELL, "DBELL"}, \
	{BOOKE_IRQPRIO_DBELL_CRIT, "DBELL_CRIT"} \

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

	TP_printk("vcpu=%x prio=%s pending=%lx",
		__entry->cpu_nr,
		__print_symbolic(__entry->priority, kvm_trace_symbol_irqprio),
		__entry->pending)
);

#endif

/* This part must be outside protection */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE

#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE trace_booke

#include <trace/define_trace.h>
