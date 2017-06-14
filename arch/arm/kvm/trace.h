#if !defined(_TRACE_ARM_KVM_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_ARM_KVM_H

#include <linux/tracepoint.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM kvm

/* Architecturally implementation defined CP15 register access */
TRACE_EVENT(kvm_emulate_cp15_imp,
	TP_PROTO(unsigned long Op1, unsigned long Rt1, unsigned long CRn,
		 unsigned long CRm, unsigned long Op2, bool is_write),
	TP_ARGS(Op1, Rt1, CRn, CRm, Op2, is_write),

	TP_STRUCT__entry(
		__field(	unsigned int,	Op1		)
		__field(	unsigned int,	Rt1		)
		__field(	unsigned int,	CRn		)
		__field(	unsigned int,	CRm		)
		__field(	unsigned int,	Op2		)
		__field(	bool,		is_write	)
	),

	TP_fast_assign(
		__entry->is_write		= is_write;
		__entry->Op1			= Op1;
		__entry->Rt1			= Rt1;
		__entry->CRn			= CRn;
		__entry->CRm			= CRm;
		__entry->Op2			= Op2;
	),

	TP_printk("Implementation defined CP15: %s\tp15, %u, r%u, c%u, c%u, %u",
			(__entry->is_write) ? "mcr" : "mrc",
			__entry->Op1, __entry->Rt1, __entry->CRn,
			__entry->CRm, __entry->Op2)
);

TRACE_EVENT(kvm_wfx,
	TP_PROTO(unsigned long vcpu_pc, bool is_wfe),
	TP_ARGS(vcpu_pc, is_wfe),

	TP_STRUCT__entry(
		__field(	unsigned long,	vcpu_pc		)
		__field(		 bool,	is_wfe		)
	),

	TP_fast_assign(
		__entry->vcpu_pc		= vcpu_pc;
		__entry->is_wfe			= is_wfe;
	),

	TP_printk("guest executed wf%c at: 0x%08lx",
		__entry->is_wfe ? 'e' : 'i', __entry->vcpu_pc)
);

TRACE_EVENT(kvm_hvc,
	TP_PROTO(unsigned long vcpu_pc, unsigned long r0, unsigned long imm),
	TP_ARGS(vcpu_pc, r0, imm),

	TP_STRUCT__entry(
		__field(	unsigned long,	vcpu_pc		)
		__field(	unsigned long,	r0		)
		__field(	unsigned long,	imm		)
	),

	TP_fast_assign(
		__entry->vcpu_pc		= vcpu_pc;
		__entry->r0		= r0;
		__entry->imm		= imm;
	),

	TP_printk("HVC at 0x%08lx (r0: 0x%08lx, imm: 0x%lx",
		  __entry->vcpu_pc, __entry->r0, __entry->imm)
);

#endif /* _TRACE_ARM_KVM_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace

/* This part must be outside protection */
#include <trace/define_trace.h>
