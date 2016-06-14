/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2012  MIPS Technologies, Inc.  All rights reserved.
 * Authors: Sanjay Lal <sanjayl@kymasys.com>
 */

#if !defined(_TRACE_KVM_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_KVM_H

#include <linux/tracepoint.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM kvm
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE trace

/* Tracepoints for VM eists */
extern char *kvm_mips_exit_types_str[MAX_KVM_MIPS_EXIT_TYPES];

TRACE_EVENT(kvm_exit,
	    TP_PROTO(struct kvm_vcpu *vcpu, unsigned int reason),
	    TP_ARGS(vcpu, reason),
	    TP_STRUCT__entry(
			__field(unsigned long, pc)
			__field(unsigned int, reason)
	    ),

	    TP_fast_assign(
			__entry->pc = vcpu->arch.pc;
			__entry->reason = reason;
	    ),

	    TP_printk("[%s]PC: 0x%08lx",
		      kvm_mips_exit_types_str[__entry->reason],
		      __entry->pc)
);

#define KVM_TRACE_AUX_RESTORE		0
#define KVM_TRACE_AUX_SAVE		1
#define KVM_TRACE_AUX_ENABLE		2
#define KVM_TRACE_AUX_DISABLE		3
#define KVM_TRACE_AUX_DISCARD		4

#define KVM_TRACE_AUX_FPU		1
#define KVM_TRACE_AUX_MSA		2
#define KVM_TRACE_AUX_FPU_MSA		3

#define kvm_trace_symbol_aux_op		\
	{ KVM_TRACE_AUX_RESTORE, "restore" },	\
	{ KVM_TRACE_AUX_SAVE,    "save" },	\
	{ KVM_TRACE_AUX_ENABLE,  "enable" },	\
	{ KVM_TRACE_AUX_DISABLE, "disable" },	\
	{ KVM_TRACE_AUX_DISCARD, "discard" }

#define kvm_trace_symbol_aux_state		\
	{ KVM_TRACE_AUX_FPU,     "FPU" },	\
	{ KVM_TRACE_AUX_MSA,     "MSA" },	\
	{ KVM_TRACE_AUX_FPU_MSA, "FPU & MSA" }

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

#endif /* _TRACE_KVM_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
