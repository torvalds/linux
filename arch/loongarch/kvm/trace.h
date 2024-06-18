/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2023 Loongson Technology Corporation Limited
 */

#if !defined(_TRACE_KVM_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_KVM_H

#include <linux/tracepoint.h>
#include <asm/kvm_csr.h>

#undef	TRACE_SYSTEM
#define TRACE_SYSTEM	kvm

/*
 * Tracepoints for VM enters
 */
DECLARE_EVENT_CLASS(kvm_transition,
	TP_PROTO(struct kvm_vcpu *vcpu),
	TP_ARGS(vcpu),
	TP_STRUCT__entry(
		__field(unsigned int, vcpu_id)
		__field(unsigned long, pc)
	),

	TP_fast_assign(
		__entry->vcpu_id = vcpu->vcpu_id;
		__entry->pc = vcpu->arch.pc;
	),

	TP_printk("vcpu %u PC: 0x%08lx", __entry->vcpu_id, __entry->pc)
);

DEFINE_EVENT(kvm_transition, kvm_enter,
	     TP_PROTO(struct kvm_vcpu *vcpu),
	     TP_ARGS(vcpu));

DEFINE_EVENT(kvm_transition, kvm_reenter,
	     TP_PROTO(struct kvm_vcpu *vcpu),
	     TP_ARGS(vcpu));

DEFINE_EVENT(kvm_transition, kvm_out,
	     TP_PROTO(struct kvm_vcpu *vcpu),
	     TP_ARGS(vcpu));

/* Further exit reasons */
#define KVM_TRACE_EXIT_IDLE		64
#define KVM_TRACE_EXIT_CACHE		65

/* Tracepoints for VM exits */
#define kvm_trace_symbol_exit_types			\
	{ KVM_TRACE_EXIT_IDLE,		"IDLE" },	\
	{ KVM_TRACE_EXIT_CACHE,		"CACHE" }

DECLARE_EVENT_CLASS(kvm_exit,
	    TP_PROTO(struct kvm_vcpu *vcpu, unsigned int reason),
	    TP_ARGS(vcpu, reason),
	    TP_STRUCT__entry(
			__field(unsigned int, vcpu_id)
			__field(unsigned long, pc)
			__field(unsigned int, reason)
	    ),

	    TP_fast_assign(
			__entry->vcpu_id = vcpu->vcpu_id;
			__entry->pc = vcpu->arch.pc;
			__entry->reason = reason;
	    ),

	    TP_printk("vcpu %u [%s] PC: 0x%08lx",
			__entry->vcpu_id,
			__print_symbolic(__entry->reason,
				kvm_trace_symbol_exit_types),
			__entry->pc)
);

DEFINE_EVENT(kvm_exit, kvm_exit_idle,
	     TP_PROTO(struct kvm_vcpu *vcpu, unsigned int reason),
	     TP_ARGS(vcpu, reason));

DEFINE_EVENT(kvm_exit, kvm_exit_cache,
	     TP_PROTO(struct kvm_vcpu *vcpu, unsigned int reason),
	     TP_ARGS(vcpu, reason));

DEFINE_EVENT(kvm_exit, kvm_exit,
	     TP_PROTO(struct kvm_vcpu *vcpu, unsigned int reason),
	     TP_ARGS(vcpu, reason));

TRACE_EVENT(kvm_exit_gspr,
	    TP_PROTO(struct kvm_vcpu *vcpu, unsigned int inst_word),
	    TP_ARGS(vcpu, inst_word),
	    TP_STRUCT__entry(
			__field(unsigned int, vcpu_id)
			__field(unsigned int, inst_word)
	    ),

	    TP_fast_assign(
			__entry->vcpu_id = vcpu->vcpu_id;
			__entry->inst_word = inst_word;
	    ),

	    TP_printk("vcpu %u Inst word: 0x%08x", __entry->vcpu_id,
			__entry->inst_word)
);

#define KVM_TRACE_AUX_SAVE		0
#define KVM_TRACE_AUX_RESTORE		1
#define KVM_TRACE_AUX_ENABLE		2
#define KVM_TRACE_AUX_DISABLE		3
#define KVM_TRACE_AUX_DISCARD		4

#define KVM_TRACE_AUX_FPU		1
#define KVM_TRACE_AUX_LSX		2
#define KVM_TRACE_AUX_LASX		3

#define kvm_trace_symbol_aux_op				\
	{ KVM_TRACE_AUX_SAVE,		"save" },	\
	{ KVM_TRACE_AUX_RESTORE,	"restore" },	\
	{ KVM_TRACE_AUX_ENABLE,		"enable" },	\
	{ KVM_TRACE_AUX_DISABLE,	"disable" },	\
	{ KVM_TRACE_AUX_DISCARD,	"discard" }

#define kvm_trace_symbol_aux_state			\
	{ KVM_TRACE_AUX_FPU,     "FPU" },		\
	{ KVM_TRACE_AUX_LSX,     "LSX" },		\
	{ KVM_TRACE_AUX_LASX,    "LASX" }

TRACE_EVENT(kvm_aux,
	    TP_PROTO(struct kvm_vcpu *vcpu, unsigned int op,
		     unsigned int state),
	    TP_ARGS(vcpu, op, state),
	    TP_STRUCT__entry(
			__field(unsigned long, pc)
			__field(u8, op)
			__field(u8, state)
	    ),

	    TP_fast_assign(
			__entry->pc = vcpu->arch.pc;
			__entry->op = op;
			__entry->state = state;
	    ),

	    TP_printk("%s %s PC: 0x%08lx",
		      __print_symbolic(__entry->op,
				       kvm_trace_symbol_aux_op),
		      __print_symbolic(__entry->state,
				       kvm_trace_symbol_aux_state),
		      __entry->pc)
);

TRACE_EVENT(kvm_vpid_change,
	    TP_PROTO(struct kvm_vcpu *vcpu, unsigned long vpid),
	    TP_ARGS(vcpu, vpid),
	    TP_STRUCT__entry(
			__field(unsigned long, vpid)
	    ),

	    TP_fast_assign(
			__entry->vpid = vpid;
	    ),

	    TP_printk("VPID: 0x%08lx", __entry->vpid)
);

#endif /* _TRACE_KVM_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../arch/loongarch/kvm
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace

/* This part must be outside protection */
#include <trace/define_trace.h>
