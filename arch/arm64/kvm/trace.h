#if !defined(_TRACE_ARM64_KVM_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_ARM64_KVM_H

#include <linux/tracepoint.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM kvm

TRACE_EVENT(kvm_wfx_arm64,
	TP_PROTO(unsigned long vcpu_pc, bool is_wfe),
	TP_ARGS(vcpu_pc, is_wfe),

	TP_STRUCT__entry(
		__field(unsigned long,	vcpu_pc)
		__field(bool,		is_wfe)
	),

	TP_fast_assign(
		__entry->vcpu_pc = vcpu_pc;
		__entry->is_wfe  = is_wfe;
	),

	TP_printk("guest executed wf%c at: 0x%08lx",
		  __entry->is_wfe ? 'e' : 'i', __entry->vcpu_pc)
);

TRACE_EVENT(kvm_hvc_arm64,
	TP_PROTO(unsigned long vcpu_pc, unsigned long r0, unsigned long imm),
	TP_ARGS(vcpu_pc, r0, imm),

	TP_STRUCT__entry(
		__field(unsigned long, vcpu_pc)
		__field(unsigned long, r0)
		__field(unsigned long, imm)
	),

	TP_fast_assign(
		__entry->vcpu_pc = vcpu_pc;
		__entry->r0 = r0;
		__entry->imm = imm;
	),

	TP_printk("HVC at 0x%08lx (r0: 0x%08lx, imm: 0x%lx)",
		  __entry->vcpu_pc, __entry->r0, __entry->imm)
);

#endif /* _TRACE_ARM64_KVM_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace

/* This part must be outside protection */
#include <trace/define_trace.h>
