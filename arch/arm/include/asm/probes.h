/*
 * arch/arm/include/asm/probes.h
 *
 * Original contents copied from arch/arm/include/asm/kprobes.h
 * which contains the following notice...
 *
 * Copyright (C) 2006, 2007 Motorola Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#ifndef _ASM_PROBES_H
#define _ASM_PROBES_H

struct kprobe;

struct arch_specific_insn;
typedef void (kprobe_insn_handler_t)(kprobe_opcode_t,
				     struct arch_specific_insn *,
				     struct pt_regs *);
typedef unsigned long (kprobe_check_cc)(unsigned long);
typedef void (kprobe_insn_singlestep_t)(kprobe_opcode_t,
					struct arch_specific_insn *,
					struct pt_regs *);
typedef void (kprobe_insn_fn_t)(void);

/* Architecture specific copy of original instruction. */
struct arch_specific_insn {
	kprobe_opcode_t			*insn;
	kprobe_insn_handler_t		*insn_handler;
	kprobe_check_cc			*insn_check_cc;
	kprobe_insn_singlestep_t	*insn_singlestep;
	kprobe_insn_fn_t		*insn_fn;
};

#endif
