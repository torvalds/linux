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

/* The first 32 exit reasons correspond to Cause.ExcCode */
#define KVM_TRACE_EXIT_INT		 0
#define KVM_TRACE_EXIT_TLBMOD		 1
#define KVM_TRACE_EXIT_TLBMISS_LD	 2
#define KVM_TRACE_EXIT_TLBMISS_ST	 3
#define KVM_TRACE_EXIT_ADDRERR_LD	 4
#define KVM_TRACE_EXIT_ADDRERR_ST	 5
#define KVM_TRACE_EXIT_SYSCALL		 8
#define KVM_TRACE_EXIT_BREAK_INST	 9
#define KVM_TRACE_EXIT_RESVD_INST	10
#define KVM_TRACE_EXIT_COP_UNUSABLE	11
#define KVM_TRACE_EXIT_TRAP_INST	13
#define KVM_TRACE_EXIT_MSA_FPE		14
#define KVM_TRACE_EXIT_FPE		15
#define KVM_TRACE_EXIT_MSA_DISABLED	21
/* Further exit reasons */
#define KVM_TRACE_EXIT_WAIT		32
#define KVM_TRACE_EXIT_CACHE		33
#define KVM_TRACE_EXIT_SIGNAL		34

/* Tracepoints for VM exits */
#define kvm_trace_symbol_exit_types				\
	{ KVM_TRACE_EXIT_INT,		"Interrupt" },		\
	{ KVM_TRACE_EXIT_TLBMOD,	"TLB Mod" },		\
	{ KVM_TRACE_EXIT_TLBMISS_LD,	"TLB Miss (LD)" },	\
	{ KVM_TRACE_EXIT_TLBMISS_ST,	"TLB Miss (ST)" },	\
	{ KVM_TRACE_EXIT_ADDRERR_LD,	"Address Error (LD)" },	\
	{ KVM_TRACE_EXIT_ADDRERR_ST,	"Address Err (ST)" },	\
	{ KVM_TRACE_EXIT_SYSCALL,	"System Call" },	\
	{ KVM_TRACE_EXIT_BREAK_INST,	"Break Inst" },		\
	{ KVM_TRACE_EXIT_RESVD_INST,	"Reserved Inst" },	\
	{ KVM_TRACE_EXIT_COP_UNUSABLE,	"COP0/1 Unusable" },	\
	{ KVM_TRACE_EXIT_TRAP_INST,	"Trap Inst" },		\
	{ KVM_TRACE_EXIT_MSA_FPE,	"MSA FPE" },		\
	{ KVM_TRACE_EXIT_FPE,		"FPE" },		\
	{ KVM_TRACE_EXIT_MSA_DISABLED,	"MSA Disabled" },	\
	{ KVM_TRACE_EXIT_WAIT,		"WAIT" },		\
	{ KVM_TRACE_EXIT_CACHE,		"CACHE" },		\
	{ KVM_TRACE_EXIT_SIGNAL,	"Signal" }

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
		      __print_symbolic(__entry->reason,
				       kvm_trace_symbol_exit_types),
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

TRACE_EVENT(kvm_asid_change,
	    TP_PROTO(struct kvm_vcpu *vcpu, unsigned int old_asid,
		     unsigned int new_asid),
	    TP_ARGS(vcpu, old_asid, new_asid),
	    TP_STRUCT__entry(
			__field(unsigned long, pc)
			__field(u8, old_asid)
			__field(u8, new_asid)
	    ),

	    TP_fast_assign(
			__entry->pc = vcpu->arch.pc;
			__entry->old_asid = old_asid;
			__entry->new_asid = new_asid;
	    ),

	    TP_printk("PC: 0x%08lx old: 0x%02x new: 0x%02x",
		      __entry->pc,
		      __entry->old_asid,
		      __entry->new_asid)
);

#endif /* _TRACE_KVM_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
