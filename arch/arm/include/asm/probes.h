/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * arch/arm/include/asm/probes.h
 *
 * Original contents copied from arch/arm/include/asm/kprobes.h
 * which contains the following notice...
 *
 * Copyright (C) 2006, 2007 Motorola Inc.
 */

#ifndef _ASM_PROBES_H
#define _ASM_PROBES_H

#ifndef __ASSEMBLY__

typedef u32 probes_opcode_t;

struct arch_probes_insn;
typedef void (probes_insn_handler_t)(probes_opcode_t,
				     struct arch_probes_insn *,
				     struct pt_regs *);
typedef unsigned long (probes_check_cc)(unsigned long);
typedef void (probes_insn_singlestep_t)(probes_opcode_t,
					struct arch_probes_insn *,
					struct pt_regs *);
typedef void (probes_insn_fn_t)(void);

/* Architecture specific copy of original instruction. */
struct arch_probes_insn {
	probes_opcode_t			*insn;
	probes_insn_handler_t		*insn_handler;
	probes_check_cc			*insn_check_cc;
	probes_insn_singlestep_t	*insn_singlestep;
	probes_insn_fn_t		*insn_fn;
	int				stack_space;
	unsigned long			register_usage_flags;
	bool				kprobe_direct_exec;
};

#endif /* __ASSEMBLY__ */

/*
 * We assume one instruction can consume at most 64 bytes stack, which is
 * 'push {r0-r15}'. Instructions consume more or unknown stack space like
 * 'str r0, [sp, #-80]' and 'str r0, [sp, r1]' should be prohibit to probe.
 */
#define MAX_STACK_SIZE			64

#endif
