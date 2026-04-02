/* SPDX-License-Identifier: GPL-2.0 */
#if !defined(KVM_ARM64_TRACE_KVM_H) || defined(TRACE_HEADER_MULTI_READ)
#define KVM_ARM64_TRACE_KVM_H

#include <linux/tracepoint.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM kvm_arm64
#undef TRACE_SYSTEM_VAR
#define TRACE_SYSTEM_VAR kvm_arm64

TRACE_EVENT(kvm_entry,
	TP_PROTO(unsigned long vcpu_pc),
	TP_ARGS(vcpu_pc),

	TP_STRUCT__entry(
		__field(unsigned long,	vcpu_pc)
	),

	TP_fast_assign(__entry->vcpu_pc = vcpu_pc;),

	TP_printk("PC: 0x%016lx", __entry->vcpu_pc)
);

TRACE_EVENT(kvm_exit,
	TP_PROTO(unsigned int esr_ec, unsigned long vcpu_pc),
	TP_ARGS(esr_ec, vcpu_pc),

	TP_STRUCT__entry(
		__field(unsigned int,	esr_ec	)
		__field(unsigned long,	vcpu_pc	)
	),

	TP_fast_assign(
		__entry->esr_ec		= esr_ec;
		__entry->vcpu_pc	= vcpu_pc;
	),

	TP_printk("HSR_EC: 0x%04x, PC: 0x%016lx",
		  __entry->esr_ec,
		  __entry->vcpu_pc)
);

#endif /* KVM_ARM64_TRACE_KVM_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace

/* This part must be outside protection */
#include <trace/define_trace.h>
